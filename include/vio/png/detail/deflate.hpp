// vio::png::zlib - DEFLATE compression with output identical to zlib 1.3.
//
// Deflater and compress() port zlib's deflate.c and trees.c (zlib license,
// (C) Jean-loup Gailly and Mark Adler): zlib-wrapped and raw streams, levels
// 0..9 and all zlib strategies.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "../../core.hpp"
#include "checksum.hpp"
#include "deflate_tables.hpp"

namespace vio::png::zlib {

// Constants keep zlib's numeric values (names avoid clashing with zlib.h macros).
enum : int { kNoFlush = 0, kPartialFlush = 1, kSyncFlush = 2, kFullFlush = 3, kFinish = 4,
             kBlock = 5 };
enum : int { kOk = 0, kStreamEnd = 1, kStreamError = -2, kDataError = -3, kBufError = -5 };
enum : int { kDefaultStrategy = 0, kFiltered = 1, kHuffmanOnly = 2, kRle = 3, kFixed = 4 };
inline constexpr int kDefaultCompression = -1;

namespace detail {

inline constexpr int LENGTH_CODES = 29, LITERALS = 256, L_CODES = LITERALS + 1 + LENGTH_CODES,
                     D_CODES = 30, BL_CODES = 19, HEAP_SIZE = 2 * L_CODES + 1, MAX_BITS = 15,
                     Buf_size = 16, MIN_MATCH = 3, MAX_MATCH = 258,
                     MIN_LOOKAHEAD = MAX_MATCH + MIN_MATCH + 1, MAX_BL_BITS = 7,
                     END_BLOCK = 256, REP_3_6 = 16, REPZ_3_10 = 17, REPZ_11_138 = 18,
                     STORED_BLOCK = 0, STATIC_TREES = 1, DYN_TREES = 2, TOO_FAR = 4096,
                     WIN_INIT = MAX_MATCH, DIST_CODE_LEN = 512;

inline constexpr int extra_blbits[BL_CODES] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 2, 3, 7};

struct CtData {
  std::uint16_t fc = 0;  // Freq / Code
  std::uint16_t dl = 0;  // Dad / Len
};

constexpr unsigned bi_reverse(unsigned code, int len) {
  unsigned res = 0;
  do {
    res |= code & 1;
    code >>= 1, res <<= 1;
  } while (--len > 0);
  return res >> 1;
}

constexpr void gen_codes(CtData* tree, int max_code, const std::uint16_t* bl_count) {
  std::uint16_t next_code[MAX_BITS + 1] = {};
  unsigned code = 0;
  for (int bits = 1; bits <= MAX_BITS; bits++) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = static_cast<std::uint16_t>(code);
  }
  for (int n = 0; n <= max_code; n++) {
    int len = tree[n].dl;
    if (len == 0) continue;
    tree[n].fc = static_cast<std::uint16_t>(bi_reverse(next_code[len]++, len));
  }
}

struct StaticTables {
  CtData static_ltree[L_CODES + 2]{};
  CtData static_dtree[D_CODES]{};
  std::uint8_t dist_code[DIST_CODE_LEN]{};
  std::uint8_t length_code[MAX_MATCH - MIN_MATCH + 1]{};
  int base_length[LENGTH_CODES]{};
  int base_dist[D_CODES]{};
};

constexpr StaticTables make_static_tables() {
  StaticTables t{};
  int length = 0, code = 0;
  for (code = 0; code < LENGTH_CODES - 1; code++) {
    t.base_length[code] = length;
    for (int n = 0; n < (1 << len_extra[code]); n++)
      t.length_code[length++] = static_cast<std::uint8_t>(code);
  }
  t.length_code[length - 1] = static_cast<std::uint8_t>(code);
  int dist = 0;
  for (code = 0; code < 16; code++) {
    t.base_dist[code] = dist;
    for (int n = 0; n < (1 << dist_extra[code]); n++)
      t.dist_code[dist++] = static_cast<std::uint8_t>(code);
  }
  dist >>= 7;
  for (; code < D_CODES; code++) {
    t.base_dist[code] = dist << 7;
    for (int n = 0; n < (1 << (dist_extra[code] - 7)); n++)
      t.dist_code[256 + dist++] = static_cast<std::uint8_t>(code);
  }
  std::uint16_t bl_count[MAX_BITS + 1] = {};
  int n = 0;
  while (n <= 143) t.static_ltree[n++].dl = 8, bl_count[8]++;
  while (n <= 255) t.static_ltree[n++].dl = 9, bl_count[9]++;
  while (n <= 279) t.static_ltree[n++].dl = 7, bl_count[7]++;
  while (n <= 287) t.static_ltree[n++].dl = 8, bl_count[8]++;
  gen_codes(t.static_ltree, L_CODES + 1, bl_count);
  for (n = 0; n < D_CODES; n++) {
    t.static_dtree[n].dl = 5;
    t.static_dtree[n].fc = static_cast<std::uint16_t>(bi_reverse(static_cast<unsigned>(n), 5));
  }
  return t;
}

inline constexpr StaticTables tables = make_static_tables();

struct StaticTreeDesc {
  const CtData* static_tree;
  const int* extra_bits;
  int extra_base;
  int elems;
  int max_length;
};
inline constexpr StaticTreeDesc static_l_desc = {tables.static_ltree, len_extra, LITERALS + 1,
                                                 L_CODES, MAX_BITS};
inline constexpr StaticTreeDesc static_d_desc = {tables.static_dtree, dist_extra, 0, D_CODES,
                                                 MAX_BITS};
inline constexpr StaticTreeDesc static_bl_desc = {nullptr, extra_blbits, 0, BL_CODES,
                                                  MAX_BL_BITS};

struct TreeDesc {
  CtData* dyn_tree = nullptr;
  int max_code = 0;
  const StaticTreeDesc* stat_desc = nullptr;
};

}  // namespace detail

// zlib-compatible streaming compressor.
class Deflater {
 public:
  // window_bits: 8..15 for a zlib stream, -8..-15 for raw deflate.
  explicit Deflater(int level = kDefaultCompression, int window_bits = 15, int mem_level = 8,
                    int strategy = kDefaultStrategy) {
    int wrap = 1;
    if (level == kDefaultCompression) level = 6;
    if (window_bits < 0) {
      wrap = 0;
      if (window_bits < -15) throw_error();
      window_bits = -window_bits;
    }
    if (mem_level < 1 || mem_level > 9 || window_bits < 8 || window_bits > 15 || level < 0 ||
        level > 9 || strategy < 0 || strategy > kFixed || (window_bits == 8 && wrap != 1))
      throw_error();
    if (window_bits == 8) window_bits = 9;
    wrap_ = wrap;
    w_bits = static_cast<unsigned>(window_bits);
    w_size = 1u << w_bits;
    w_mask = w_size - 1;
    const unsigned hash_bits = static_cast<unsigned>(mem_level) + 7;
    hash_size = 1u << hash_bits;
    hash_mask = hash_size - 1;
    hash_shift = (hash_bits + detail::MIN_MATCH - 1) / detail::MIN_MATCH;
    window_store_.assign(static_cast<std::size_t>(w_size) * 2, 0);
    window = window_store_.data();
    prev_.assign(w_size, 0);
    head_.assign(hash_size, 0);
    high_water = 0;
    const unsigned lit_bufsize = 1u << (mem_level + 6);
    pending_store_.assign(static_cast<std::size_t>(lit_bufsize) * 4, 0);
    pending_buf = pending_store_.data();
    pending_buf_size = static_cast<unsigned long>(lit_bufsize) * 4;
    // (zlib overlays the symbols on pending_buf; kept apart here, and as
    // 16-bit fields, so that their stores do not alias the state.)
    syms_.assign(lit_bufsize, {});
    sym_end = lit_bufsize - 1;
    this->level = level;
    this->strategy = strategy;
    reset();
  }

  // Stream fields (as in z_stream)
  const std::uint8_t* next_in = nullptr;
  unsigned avail_in = 0;
  unsigned long total_in = 0;
  std::uint8_t* next_out = nullptr;
  unsigned avail_out = 0;
  unsigned long total_out = 0;
  std::uint32_t adler = 1;

  void reset() {
    total_in = total_out = 0;
    pending = 0;
    pending_out = pending_buf;
    if (wrap_ < 0) wrap_ = -wrap_;
    status = INIT_STATE;
    adler = 1;
    last_flush = -2;
    tr_init();
    lm_init();
  }

  int deflate(int flush) {
    if (flush > kBlock || flush < 0) return kStreamError;
    if (next_out == nullptr || (avail_in != 0 && next_in == nullptr) ||
        (status == FINISH_STATE && flush != kFinish))
      return kStreamError;
    if (avail_out == 0) return kBufError;
    int old_flush = last_flush;
    last_flush = flush;
    if (pending != 0) {
      flush_pending();
      if (avail_out == 0) {
        last_flush = -1;
        return kOk;
      }
    } else if (avail_in == 0 && rank(flush) <= rank(old_flush) && flush != kFinish) {
      return kBufError;
    }
    if (status == FINISH_STATE && avail_in != 0) return kBufError;
    if (status == INIT_STATE && wrap_ == 0) status = BUSY_STATE;
    if (status == INIT_STATE) {
      unsigned header = (8 + ((w_bits - 8) << 4)) << 8;
      unsigned level_flags;
      if (strategy >= kHuffmanOnly || level < 2) level_flags = 0;
      else if (level < 6) level_flags = 1;
      else if (level == 6) level_flags = 2;
      else level_flags = 3;
      header |= (level_flags << 6);
      if (strstart != 0) header |= 0x20;
      header += 31 - (header % 31);
      put_short_msb(header);
      if (strstart != 0) {
        put_short_msb(adler >> 16);
        put_short_msb(adler & 0xffff);
      }
      adler = 1;
      status = BUSY_STATE;
      flush_pending();
      if (pending != 0) {
        last_flush = -1;
        return kOk;
      }
    }
    if (avail_in != 0 || lookahead != 0 || (flush != kNoFlush && status != FINISH_STATE)) {
      BlockState bstate = level == 0                   ? deflate_stored(flush)
                          : strategy == kHuffmanOnly ? deflate_huff(flush)
                          : strategy == kRle          ? deflate_rle(flush)
                          : level <= 3                 ? deflate_fast(flush)
                                                       : deflate_slow(flush);
      if (bstate == finish_started || bstate == finish_done) status = FINISH_STATE;
      if (bstate == need_more || bstate == finish_started) {
        if (avail_out == 0) last_flush = -1;
        return kOk;
      }
      if (bstate == block_done) {
        if (flush == kPartialFlush) {
          tr_align();
        } else if (flush != kBlock) {
          tr_stored_block(nullptr, 0L, 0);
          if (flush == kFullFlush) {
            clear_hash();
            if (lookahead == 0) {
              strstart = 0;
              block_start = 0L;
              insert = 0;
            }
          }
        }
        flush_pending();
        if (avail_out == 0) {
          last_flush = -1;
          return kOk;
        }
      }
    }
    if (flush != kFinish) return kOk;
    if (wrap_ <= 0) return kStreamEnd;
    put_short_msb(adler >> 16);
    put_short_msb(adler & 0xffff);
    flush_pending();
    if (wrap_ > 0) wrap_ = -wrap_;
    return pending != 0 ? kOk : kStreamEnd;
  }

  // compressBound()
  static std::size_t bound(std::size_t sourceLen) {
    return sourceLen + (sourceLen >> 12) + (sourceLen >> 14) + (sourceLen >> 25) + 13;
  }

 private:
  using Pos = std::uint16_t;
  enum BlockState { need_more, block_done, finish_started, finish_done };
  static constexpr int INIT_STATE = 42, BUSY_STATE = 113, FINISH_STATE = 666;

  struct Config {
    std::uint16_t good_length, max_lazy, nice_length, max_chain;
  };
  static constexpr Config configuration_table[10] = {
      {0, 0, 0, 0},       {4, 4, 8, 4},       {4, 5, 16, 8},      {4, 6, 32, 32},
      {4, 4, 16, 16},     {8, 16, 32, 32},    {8, 16, 128, 128},  {8, 32, 128, 256},
      {32, 128, 258, 1024}, {32, 258, 258, 4096}};

  [[noreturn]] static void throw_error() { throw std::invalid_argument("zlib: bad parameters"); }

  static int rank(int f) { return (f * 2) - (f > 4 ? 9 : 0); }

  unsigned max_dist() const { return w_size - detail::MIN_LOOKAHEAD; }

  void update_hash(unsigned& h, unsigned c) { h = ((h << hash_shift) ^ c) & hash_mask; }

  unsigned insert_string(unsigned str) {
    update_hash(ins_h, window[str + (detail::MIN_MATCH - 1)]);
    unsigned match_head = prev_[str & w_mask] = head_[ins_h];
    head_[ins_h] = static_cast<Pos>(str);
    return match_head;
  }

  void clear_hash() {
    std::memset(head_.data(), 0, hash_size * sizeof(Pos));
  }

  void slide_hash() {
    unsigned wsize = w_size;
    unsigned n = hash_size;
    Pos* p = &head_[n];
    do {
      unsigned m = *--p;
      *p = static_cast<Pos>(m >= wsize ? m - wsize : 0);
    } while (--n);
    n = wsize;
    p = &prev_[n];
    do {
      unsigned m = *--p;
      *p = static_cast<Pos>(m >= wsize ? m - wsize : 0);
    } while (--n);
  }

  unsigned read_buf(std::uint8_t* buf, unsigned size) {
    unsigned len = avail_in;
    if (len > size) len = size;
    if (len == 0) return 0;
    avail_in -= len;
    std::memcpy(buf, next_in, len);
    if (wrap_ == 1) adler = adler32(adler, buf, len);
    next_in += len;
    total_in += len;
    return len;
  }

  void fill_window() {
    using namespace detail;
    unsigned wsize = w_size;
    do {
      unsigned more = static_cast<unsigned>(window_size - lookahead - strstart);
      if (strstart >= wsize + max_dist()) {
        std::memcpy(window, window + wsize, wsize - more);
        match_start -= wsize;
        strstart -= wsize;
        block_start -= static_cast<long>(wsize);
        if (insert > strstart) insert = strstart;
        slide_hash();
        more += wsize;
      }
      if (avail_in == 0) break;
      unsigned n = read_buf(window + strstart + lookahead, more);
      lookahead += n;
      if (lookahead + insert >= MIN_MATCH) {
        unsigned str = strstart - insert;
        ins_h = window[str];
        update_hash(ins_h, window[str + 1]);
        while (insert) {
          update_hash(ins_h, window[str + MIN_MATCH - 1]);
          prev_[str & w_mask] = head_[ins_h];
          head_[ins_h] = static_cast<Pos>(str);
          str++;
          insert--;
          if (lookahead + insert < MIN_MATCH) break;
        }
      }
    } while (lookahead < MIN_LOOKAHEAD && avail_in != 0);
    if (high_water < window_size) {
      unsigned long curr = strstart + static_cast<unsigned long>(lookahead);
      unsigned long init;
      if (high_water < curr) {
        init = window_size - curr;
        if (init > WIN_INIT) init = WIN_INIT;
        std::memset(window + curr, 0, init);
        high_water = curr + init;
      } else if (high_water < curr + WIN_INIT) {
        init = curr + WIN_INIT - high_water;
        if (init > window_size - high_water) init = window_size - high_water;
        std::memset(window + high_water, 0, init);
        high_water += init;
      }
    }
  }

  void lm_init() {
    window_size = 2UL * w_size;
    clear_hash();
    max_lazy_match = configuration_table[level].max_lazy;
    good_match = configuration_table[level].good_length;
    nice_match = configuration_table[level].nice_length;
    max_chain_length = configuration_table[level].max_chain;
    strstart = 0;
    block_start = 0L;
    lookahead = 0;
    insert = 0;
    match_length = detail::MIN_MATCH - 1;
    match_available = 0;
    ins_h = 0;
  }

  void put_byte(unsigned c) { pending_buf[pending++] = static_cast<std::uint8_t>(c); }
  void put_short(unsigned w) {
    put_byte(w);
    put_byte(w >> 8);
  }
  void put_short_msb(unsigned b) {
    put_byte(b >> 8);
    put_byte(b);
  }

  void flush_pending() {
    bi_flush();
    unsigned len = pending > avail_out ? avail_out : static_cast<unsigned>(pending);
    if (len == 0) return;
    std::memcpy(next_out, pending_out, len);
    next_out += len;
    pending_out += len;
    total_out += len;
    avail_out -= len;
    pending -= len;
    if (pending == 0) pending_out = pending_buf;
  }

  unsigned longest_match(unsigned cur_match, unsigned prev_length) {
    using namespace detail;
    unsigned chain_length = max_chain_length;
    const std::uint8_t* scan = window + strstart;
    int best_len = static_cast<int>(prev_length);
    int nice = nice_match;
    unsigned limit = strstart > max_dist() ? strstart - max_dist() : 0;
    const Pos* prev = prev_.data();
    unsigned wmask = w_mask;
    if (prev_length >= good_match) chain_length >>= 2;
    if (static_cast<unsigned>(nice) > lookahead) nice = static_cast<int>(lookahead);
    std::uint16_t scan_start = load16(scan), scan_end = load16(scan + best_len - 1);
    do {
      const std::uint8_t* match = window + cur_match;
      if (load16(match + best_len - 1) != scan_end || load16(match) != scan_start) continue;
      int len = static_cast<int>(compare_from_3(scan, match));
      if (len > best_len) {
        match_start = cur_match;
        best_len = len;
        if (len >= nice) break;
        scan_end = load16(scan + best_len - 1);
      }
    } while ((cur_match = prev[cur_match & wmask]) > limit && --chain_length != 0);
    if (static_cast<unsigned>(best_len) <= lookahead) return static_cast<unsigned>(best_len);
    return lookahead;
  }

  static std::uint16_t load16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
  }

  // Length of the match of s and m whose first two bytes are equal: the
  // third is taken as equal too (as zlib does: with equal hashes it is), the
  // bytes from the fourth on are compared, up to MAX_MATCH.
  static unsigned compare_from_3(const std::uint8_t* s, const std::uint8_t* m) {
    using namespace detail;
#if (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ && \
     (defined(__x86_64__) || defined(__aarch64__))) ||                       \
    (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64)))
    for (unsigned len = 3; len < static_cast<unsigned>(MAX_MATCH); len += 8) {
      std::uint64_t a, b;
      std::memcpy(&a, s + len, 8);
      std::memcpy(&b, m + len, 8);
      if (std::uint64_t x = a ^ b) {
#if defined(_MSC_VER) && !defined(__clang__)
        unsigned long bit;
        _BitScanForward64(&bit, x);
#else
        unsigned bit = static_cast<unsigned>(__builtin_ctzll(x));
#endif
        len += static_cast<unsigned>(bit) >> 3;
        return len < static_cast<unsigned>(MAX_MATCH) ? len : MAX_MATCH;
      }
    }
    return MAX_MATCH;
#else
    unsigned len = 3;
    while (len < static_cast<unsigned>(MAX_MATCH) && s[len] == m[len]) len++;
    return len;
#endif
  }

  // FLUSH_BLOCK_ONLY / FLUSH_BLOCK
  void flush_block_only(int last) {
    tr_flush_block(block_start >= 0L ? window + static_cast<unsigned>(block_start) : nullptr,
                   static_cast<unsigned long>(static_cast<long>(strstart) - block_start), last);
    block_start = strstart;
    flush_pending();
  }

  BlockState deflate_stored(int flush) {
    constexpr unsigned MAX_STORED = 65535;
    unsigned min_block = static_cast<unsigned>(std::min<unsigned long>(pending_buf_size - 5, w_size));
    int last = 0;
    unsigned len, left, have;
    unsigned used = avail_in;
    do {
      len = MAX_STORED;
      have = (static_cast<unsigned>(bi_valid) + 42) >> 3;
      if (avail_out < have) break;
      have = avail_out - have;
      left = static_cast<unsigned>(strstart - block_start);
      if (len > static_cast<unsigned long>(left) + avail_in) len = left + avail_in;
      if (len > have) len = have;
      if (len < min_block && ((len == 0 && flush != kFinish) || flush == kNoFlush ||
                              len != left + avail_in))
        break;
      last = flush == kFinish && len == left + avail_in ? 1 : 0;
      tr_stored_block(nullptr, 0L, last);
      pending_buf[pending - 4] = static_cast<std::uint8_t>(len);
      pending_buf[pending - 3] = static_cast<std::uint8_t>(len >> 8);
      pending_buf[pending - 2] = static_cast<std::uint8_t>(~len);
      pending_buf[pending - 1] = static_cast<std::uint8_t>(~len >> 8);
      flush_pending();
      if (left) {
        if (left > len) left = len;
        std::memcpy(next_out, window + block_start, left);
        next_out += left;
        avail_out -= left;
        total_out += left;
        block_start += left;
        len -= left;
      }
      if (len) {
        read_buf(next_out, len);
        next_out += len;
        avail_out -= len;
        total_out += len;
      }
    } while (last == 0);
    used -= avail_in;
    if (used) {
      if (used >= w_size) {
        std::memcpy(window, next_in - w_size, w_size);
        strstart = w_size;
        insert = strstart;
      } else {
        if (window_size - strstart <= used) {
          strstart -= w_size;
          std::memcpy(window, window + w_size, strstart);
          if (insert > strstart) insert = strstart;
        }
        std::memcpy(window + strstart, next_in - used, used);
        strstart += used;
        insert += std::min(used, w_size - insert);
      }
      block_start = strstart;
    }
    if (high_water < strstart) high_water = strstart;
    if (last) return finish_done;
    if (flush != kNoFlush && flush != kFinish && avail_in == 0 &&
        static_cast<long>(strstart) == block_start)
      return block_done;
    have = static_cast<unsigned>(window_size - strstart);
    if (avail_in > have && block_start >= static_cast<long>(w_size)) {
      block_start -= w_size;
      strstart -= w_size;
      std::memcpy(window, window + w_size, strstart);
      have += w_size;
      if (insert > strstart) insert = strstart;
    }
    if (have > avail_in) have = avail_in;
    if (have) {
      read_buf(window + strstart, have);
      strstart += have;
      insert += std::min(have, w_size - insert);
    }
    if (high_water < strstart) high_water = strstart;
    have = (static_cast<unsigned>(bi_valid) + 42) >> 3;
    have = static_cast<unsigned>(std::min<unsigned long>(pending_buf_size - have, MAX_STORED));
    min_block = std::min(have, w_size);
    left = static_cast<unsigned>(strstart - block_start);
    if (left >= min_block ||
        ((left || flush == kFinish) && flush != kNoFlush && avail_in == 0 && left <= have)) {
      len = std::min(left, have);
      last = flush == kFinish && avail_in == 0 && len == left ? 1 : 0;
      tr_stored_block(window + block_start, len, last);
      block_start += len;
      flush_pending();
    }
    return last ? finish_started : need_more;
  }

  VIO_INLINE BlockState finish_block(int flush) {
    if (flush == kFinish) {
      flush_block_only(1);
      if (avail_out == 0) return finish_started;
      return finish_done;
    }
    if (sym_next) {
      flush_block_only(0);
      if (avail_out == 0) return need_more;
    }
    return block_done;
  }

  BlockState deflate_fast(int flush) {
    using namespace detail;
    unsigned hash_head;
    bool bflush;
    for (;;) {
      if (lookahead < MIN_LOOKAHEAD) {
        fill_window();
        if (lookahead < MIN_LOOKAHEAD && flush == kNoFlush) return need_more;
        if (lookahead == 0) break;
      }
      hash_head = 0;
      if (lookahead >= MIN_MATCH) hash_head = insert_string(strstart);
      if (hash_head != 0 && strstart - hash_head <= max_dist())
        match_length = longest_match(hash_head, MIN_MATCH - 1);
      if (match_length >= MIN_MATCH) {
        bflush = tally_dist(strstart - match_start, match_length - MIN_MATCH);
        lookahead -= match_length;
        if (match_length <= max_lazy_match && lookahead >= MIN_MATCH) {
          match_length--;
          do {
            strstart++;
            hash_head = insert_string(strstart);
          } while (--match_length != 0);
          strstart++;
        } else {
          strstart += match_length;
          match_length = 0;
          ins_h = window[strstart];
          update_hash(ins_h, window[strstart + 1]);
        }
      } else {
        bflush = tally_lit(window[strstart]);
        lookahead--;
        strstart++;
      }
      if (bflush) {
        flush_block_only(0);
        if (avail_out == 0) return need_more;
      }
    }
    insert = strstart < MIN_MATCH - 1 ? strstart : MIN_MATCH - 1;
    return finish_block(flush);
  }

  BlockState deflate_slow(int flush) {
    using namespace detail;
    unsigned hash_head;
    bool bflush;
    for (;;) {
      if (lookahead < MIN_LOOKAHEAD) {
        fill_window();
        if (lookahead < MIN_LOOKAHEAD && flush == kNoFlush) return need_more;
        if (lookahead == 0) break;
      }
      hash_head = 0;
      if (lookahead >= MIN_MATCH) hash_head = insert_string(strstart);
      unsigned prev_length = match_length;
      const unsigned prev_match = match_start;
      match_length = MIN_MATCH - 1;
      if (hash_head != 0 && prev_length < max_lazy_match && strstart - hash_head <= max_dist()) {
        match_length = longest_match(hash_head, prev_length);
        if (match_length <= 5 &&
            (strategy == kFiltered ||
             (match_length == MIN_MATCH && strstart - match_start > TOO_FAR)))
          match_length = MIN_MATCH - 1;
      }
      if (prev_length >= MIN_MATCH && match_length <= prev_length) {
        unsigned max_insert = strstart + lookahead - MIN_MATCH;
        bflush = tally_dist(strstart - 1 - prev_match, prev_length - MIN_MATCH);
        lookahead -= prev_length - 1;
        prev_length -= 2;
        do {
          if (++strstart <= max_insert) hash_head = insert_string(strstart);
        } while (--prev_length != 0);
        match_available = 0;
        match_length = MIN_MATCH - 1;
        strstart++;
        if (bflush) {
          flush_block_only(0);
          if (avail_out == 0) return need_more;
        }
      } else if (match_available) {
        bflush = tally_lit(window[strstart - 1]);
        if (bflush) flush_block_only(0);
        strstart++;
        lookahead--;
        if (avail_out == 0) return need_more;
      } else {
        match_available = 1;
        strstart++;
        lookahead--;
      }
    }
    if (match_available) {
      tally_lit(window[strstart - 1]);
      match_available = 0;
    }
    insert = strstart < MIN_MATCH - 1 ? strstart : MIN_MATCH - 1;
    return finish_block(flush);
  }

  BlockState deflate_rle(int flush) {
    using namespace detail;
    bool bflush;
    for (;;) {
      if (lookahead <= static_cast<unsigned>(MAX_MATCH)) {
        fill_window();
        if (lookahead <= static_cast<unsigned>(MAX_MATCH) && flush == kNoFlush) return need_more;
        if (lookahead == 0) break;
      }
      match_length = 0;
      if (lookahead >= MIN_MATCH && strstart > 0) {
        std::uint8_t* scan = window + strstart - 1;
        unsigned prev = *scan;
        if (prev == *++scan && prev == *++scan && prev == *++scan) {
          std::uint8_t* strend = window + strstart + MAX_MATCH;
          do {
          } while (prev == *++scan && prev == *++scan && prev == *++scan && prev == *++scan &&
                   prev == *++scan && prev == *++scan && prev == *++scan && prev == *++scan &&
                   scan < strend);
          match_length = MAX_MATCH - static_cast<unsigned>(strend - scan);
          if (match_length > lookahead) match_length = lookahead;
        }
      }
      if (match_length >= MIN_MATCH) {
        bflush = tally_dist(1, match_length - MIN_MATCH);
        lookahead -= match_length;
        strstart += match_length;
        match_length = 0;
      } else {
        bflush = tally_lit(window[strstart]);
        lookahead--;
        strstart++;
      }
      if (bflush) {
        flush_block_only(0);
        if (avail_out == 0) return need_more;
      }
    }
    insert = 0;
    return finish_block(flush);
  }

  BlockState deflate_huff(int flush) {
    bool bflush;
    for (;;) {
      if (lookahead == 0) {
        fill_window();
        if (lookahead == 0) {
          if (flush == kNoFlush) return need_more;
          break;
        }
      }
      match_length = 0;
      bflush = tally_lit(window[strstart]);
      lookahead--;
      strstart++;
      if (bflush) {
        flush_block_only(0);
        if (avail_out == 0) return need_more;
      }
    }
    insert = 0;
    return finish_block(flush);
  }

  // ---- trees.c ---------------------------------------------------------------
  static unsigned d_code(unsigned dist) {
    return dist < 256 ? detail::tables.dist_code[dist] : detail::tables.dist_code[256 + (dist >> 7)];
  }

  bool tally_lit(unsigned c) {
    syms_[sym_next++] = {0, static_cast<std::uint16_t>(c)};
    dyn_ltree[c].fc++;
    return sym_next == sym_end;
  }
  bool tally_dist(unsigned distance, unsigned length) {
    std::uint8_t len = static_cast<std::uint8_t>(length);
    std::uint16_t dist = static_cast<std::uint16_t>(distance);
    syms_[sym_next++] = {dist, len};
    dist--;
    dyn_ltree[detail::tables.length_code[len] + detail::LITERALS + 1].fc++;
    dyn_dtree[d_code(dist)].fc++;
    return sym_next == sym_end;
  }

  void send_bits(int value, int length) {
    if (bi_valid > detail::Buf_size - length) {
      bi_buf = static_cast<std::uint16_t>(bi_buf | (static_cast<std::uint16_t>(value) << bi_valid));
      put_short(bi_buf);
      bi_buf = static_cast<std::uint16_t>(static_cast<std::uint16_t>(value) >>
                                          (detail::Buf_size - bi_valid));
      bi_valid += length - detail::Buf_size;
    } else {
      bi_buf = static_cast<std::uint16_t>(bi_buf | (static_cast<std::uint16_t>(value) << bi_valid));
      bi_valid += length;
    }
  }
  void send_code(int c, const detail::CtData* tree) { send_bits(tree[c].fc, tree[c].dl); }

  void bi_flush() {
    if (bi_valid == 16) {
      put_short(bi_buf);
      bi_buf = 0;
      bi_valid = 0;
    } else if (bi_valid >= 8) {
      put_byte(bi_buf & 0xff);
      bi_buf >>= 8;
      bi_valid -= 8;
    }
  }
  void bi_windup() {
    if (bi_valid > 8) put_short(bi_buf);
    else if (bi_valid > 0) put_byte(bi_buf & 0xff);
    bi_buf = 0;
    bi_valid = 0;
  }

  void init_block() {
    for (int n = 0; n < detail::L_CODES; n++) dyn_ltree[n].fc = 0;
    for (int n = 0; n < detail::D_CODES; n++) dyn_dtree[n].fc = 0;
    for (int n = 0; n < detail::BL_CODES; n++) bl_tree[n].fc = 0;
    dyn_ltree[detail::END_BLOCK].fc = 1;
    opt_len = static_len = 0L;
    sym_next = 0;
  }

  void tr_init() {
    l_desc = {dyn_ltree, 0, &detail::static_l_desc};
    d_desc = {dyn_dtree, 0, &detail::static_d_desc};
    bl_desc = {bl_tree, 0, &detail::static_bl_desc};
    bi_buf = 0;
    bi_valid = 0;
    init_block();
  }

  bool smaller(const detail::CtData* tree, int n, int m) const {
    return tree[n].fc < tree[m].fc || (tree[n].fc == tree[m].fc && depth[n] <= depth[m]);
  }

  void pqdownheap(const detail::CtData* tree, int k) {
    int v = heap[k];
    int j = k << 1;
    while (j <= heap_len) {
      if (j < heap_len && smaller(tree, heap[j + 1], heap[j])) j++;
      if (smaller(tree, v, heap[j])) break;
      heap[k] = heap[j];
      k = j;
      j <<= 1;
    }
    heap[k] = v;
  }

  void gen_bitlen(detail::TreeDesc* desc) {
    using namespace detail;
    CtData* tree = desc->dyn_tree;
    int max_code = desc->max_code;
    const CtData* stree = desc->stat_desc->static_tree;
    const int* extra = desc->stat_desc->extra_bits;
    int base = desc->stat_desc->extra_base;
    int max_length = desc->stat_desc->max_length;
    int h, n, m, bits, xbits;
    std::uint16_t f;
    int overflow = 0;
    for (bits = 0; bits <= MAX_BITS; bits++) bl_count[bits] = 0;
    tree[heap[heap_max]].dl = 0;
    for (h = heap_max + 1; h < HEAP_SIZE; h++) {
      n = heap[h];
      bits = tree[tree[n].dl].dl + 1;
      if (bits > max_length) bits = max_length, overflow++;
      tree[n].dl = static_cast<std::uint16_t>(bits);
      if (n > max_code) continue;
      bl_count[bits]++;
      xbits = 0;
      if (n >= base) xbits = extra[n - base];
      f = tree[n].fc;
      opt_len += static_cast<unsigned long>(f) * static_cast<unsigned>(bits + xbits);
      if (stree) static_len += static_cast<unsigned long>(f) * static_cast<unsigned>(stree[n].dl + xbits);
    }
    if (overflow == 0) return;
    do {
      bits = max_length - 1;
      while (bl_count[bits] == 0) bits--;
      bl_count[bits]--;
      bl_count[bits + 1] += 2;
      bl_count[max_length]--;
      overflow -= 2;
    } while (overflow > 0);
    for (bits = max_length; bits != 0; bits--) {
      n = bl_count[bits];
      while (n != 0) {
        m = heap[--h];
        if (m > max_code) continue;
        if (static_cast<unsigned>(tree[m].dl) != static_cast<unsigned>(bits)) {
          opt_len += (static_cast<unsigned long>(bits) - tree[m].dl) * tree[m].fc;
          tree[m].dl = static_cast<std::uint16_t>(bits);
        }
        n--;
      }
    }
  }

  void build_tree(detail::TreeDesc* desc) {
    using namespace detail;
    CtData* tree = desc->dyn_tree;
    const CtData* stree = desc->stat_desc->static_tree;
    int elems = desc->stat_desc->elems;
    int n, m;
    int max_code = -1;
    int node;
    heap_len = 0, heap_max = HEAP_SIZE;
    for (n = 0; n < elems; n++) {
      if (tree[n].fc != 0) {
        heap[++heap_len] = max_code = n;
        depth[n] = 0;
      } else {
        tree[n].dl = 0;
      }
    }
    while (heap_len < 2) {
      node = heap[++heap_len] = (max_code < 2 ? ++max_code : 0);
      tree[node].fc = 1;
      depth[node] = 0;
      opt_len--;
      if (stree) static_len -= stree[node].dl;
    }
    desc->max_code = max_code;
    for (n = heap_len / 2; n >= 1; n--) pqdownheap(tree, n);
    node = elems;
    do {
      n = heap[1];
      heap[1] = heap[heap_len--];
      pqdownheap(tree, 1);
      m = heap[1];
      heap[--heap_max] = n;
      heap[--heap_max] = m;
      tree[node].fc = static_cast<std::uint16_t>(tree[n].fc + tree[m].fc);
      depth[node] = static_cast<std::uint8_t>((depth[n] >= depth[m] ? depth[n] : depth[m]) + 1);
      tree[n].dl = tree[m].dl = static_cast<std::uint16_t>(node);
      heap[1] = node++;
      pqdownheap(tree, 1);
    } while (heap_len >= 2);
    heap[--heap_max] = heap[1];
    gen_bitlen(desc);
    gen_codes(tree, max_code, bl_count);
  }

  template <class Emit>
  VIO_INLINE void for_code_length_runs(const detail::CtData* tree, int max_code, Emit&& emit) {
    int prevlen = -1, nextlen = tree[0].dl, count = 0, max_count = 7, min_count = 4;
    if (nextlen == 0) max_count = 138, min_count = 3;
    for (int n = 0; n <= max_code; n++) {
      int curlen = nextlen;
      nextlen = tree[n + 1].dl;
      if (++count < max_count && curlen == nextlen) continue;
      emit(curlen, prevlen, count, min_count);
      count = 0;
      prevlen = curlen;
      if (nextlen == 0) max_count = 138, min_count = 3;
      else if (curlen == nextlen) max_count = 6, min_count = 3;
      else max_count = 7, min_count = 4;
    }
  }

  void scan_tree(detail::CtData* tree, int max_code) {
    using namespace detail;
    tree[max_code + 1].dl = 0xffff;
    for_code_length_runs(tree, max_code, [&](int curlen, int prevlen, int count, int min_count) {
      if (count < min_count) {
        bl_tree[curlen].fc = static_cast<std::uint16_t>(bl_tree[curlen].fc + count);
      } else if (curlen != 0) {
        if (curlen != prevlen) bl_tree[curlen].fc++;
        bl_tree[REP_3_6].fc++;
      } else if (count <= 10) {
        bl_tree[REPZ_3_10].fc++;
      } else {
        bl_tree[REPZ_11_138].fc++;
      }
    });
  }

  void send_tree(const detail::CtData* tree, int max_code) {
    using namespace detail;
    for_code_length_runs(tree, max_code, [&](int curlen, int prevlen, int count, int min_count) {
      if (count < min_count) {
        do {
          send_code(curlen, bl_tree);
        } while (--count != 0);
      } else if (curlen != 0) {
        if (curlen != prevlen) {
          send_code(curlen, bl_tree);
          count--;
        }
        send_code(REP_3_6, bl_tree);
        send_bits(count - 3, 2);
      } else if (count <= 10) {
        send_code(REPZ_3_10, bl_tree);
        send_bits(count - 3, 3);
      } else {
        send_code(REPZ_11_138, bl_tree);
        send_bits(count - 11, 7);
      }
    });
  }

  int build_bl_tree() {
    using namespace detail;
    scan_tree(dyn_ltree, l_desc.max_code);
    scan_tree(dyn_dtree, d_desc.max_code);
    build_tree(&bl_desc);
    int max_blindex;
    for (max_blindex = BL_CODES - 1; max_blindex >= 3; max_blindex--)
      if (bl_tree[kCodeLengthOrder[max_blindex]].dl != 0) break;
    opt_len += 3 * (static_cast<unsigned long>(max_blindex) + 1) + 5 + 5 + 4;
    return max_blindex;
  }

  void send_all_trees(int lcodes, int dcodes, int blcodes) {
    using namespace detail;
    send_bits(lcodes - 257, 5);
    send_bits(dcodes - 1, 5);
    send_bits(blcodes - 4, 4);
    for (int rank = 0; rank < blcodes; rank++) send_bits(bl_tree[kCodeLengthOrder[rank]].dl, 3);
    send_tree(dyn_ltree, lcodes - 1);
    send_tree(dyn_dtree, dcodes - 1);
  }

  void tr_stored_block(const std::uint8_t* buf, unsigned long stored_len, int last) {
    send_bits((detail::STORED_BLOCK << 1) + last, 3);
    bi_windup();
    put_short(static_cast<std::uint16_t>(stored_len));
    put_short(static_cast<std::uint16_t>(~stored_len));
    if (stored_len) std::memcpy(pending_buf + pending, buf, stored_len);
    pending += stored_len;
  }

  void tr_align() {
    send_bits(detail::STATIC_TREES << 1, 3);
    send_code(detail::END_BLOCK, detail::tables.static_ltree);
    bi_flush();
  }

  void compress_block(const detail::CtData* ltree, const detail::CtData* dtree) {
    using namespace detail;
    // Bits gathered in 64 bits and written 32 at a time; at the end, the
    // state send_bits() would have left (whole 16-bit words written while
    // more than 16 bits are pending).
    std::uint64_t acc = bi_buf;
    unsigned n = static_cast<unsigned>(bi_valid);
    auto put = [&](unsigned code, unsigned len) {
      acc |= static_cast<std::uint64_t>(code) << n;
      n += len;
    };
    auto spill = [&] {
      if (n >= 32) {
        std::uint8_t* p = pending_buf + pending;
        p[0] = static_cast<std::uint8_t>(acc);
        p[1] = static_cast<std::uint8_t>(acc >> 8);
        p[2] = static_cast<std::uint8_t>(acc >> 16);
        p[3] = static_cast<std::uint8_t>(acc >> 24);
        pending += 4;
        acc >>= 32;
        n -= 32;
      }
    };
    for (unsigned sx = 0; sx < sym_next; sx++) {
      unsigned dist = syms_[sx].dist;
      unsigned lc = syms_[sx].lc;
      if (dist == 0) {
        put(ltree[lc].fc, ltree[lc].dl);
      } else {
        unsigned code = tables.length_code[lc];
        put(ltree[code + LITERALS + 1].fc, ltree[code + LITERALS + 1].dl);
        // (no extra bits for code 28, whose base_length is not set)
        if (len_extra[code] != 0)
          put(lc - static_cast<unsigned>(tables.base_length[code]),
              static_cast<unsigned>(len_extra[code]));
        spill();
        dist--;
        code = d_code(dist);
        put(dtree[code].fc, dtree[code].dl);
        put(dist - static_cast<unsigned>(tables.base_dist[code]),
            static_cast<unsigned>(dist_extra[code]));
      }
      spill();
    }
    put(ltree[END_BLOCK].fc, ltree[END_BLOCK].dl);
    while (n > 16) {
      put_short(static_cast<unsigned>(acc));
      acc >>= 16;
      n -= 16;
    }
    bi_buf = static_cast<std::uint16_t>(acc);
    bi_valid = static_cast<int>(n);
  }

  void tr_flush_block(const std::uint8_t* buf, unsigned long stored_len, int last) {
    using namespace detail;
    unsigned long opt_lenb, static_lenb;
    int max_blindex = 0;
    if (level > 0) {
      build_tree(&l_desc);
      build_tree(&d_desc);
      max_blindex = build_bl_tree();
      opt_lenb = (opt_len + 3 + 7) >> 3;
      static_lenb = (static_len + 3 + 7) >> 3;
      if (static_lenb <= opt_lenb || strategy == kFixed) opt_lenb = static_lenb;
    } else {
      opt_lenb = static_lenb = stored_len + 5;
    }
    if (stored_len + 4 <= opt_lenb && buf != nullptr) {
      tr_stored_block(buf, stored_len, last);
    } else if (static_lenb == opt_lenb) {
      send_bits((STATIC_TREES << 1) + last, 3);
      compress_block(tables.static_ltree, tables.static_dtree);
    } else {
      send_bits((DYN_TREES << 1) + last, 3);
      send_all_trees(l_desc.max_code + 1, d_desc.max_code + 1, max_blindex + 1);
      compress_block(dyn_ltree, dyn_dtree);
    }
    init_block();
    if (last) bi_windup();
  }

  // ---- state -------------------------------------------------------------------
  int status = INIT_STATE;
  std::vector<std::uint8_t> pending_store_;
  std::uint8_t* pending_buf = nullptr;
  unsigned long pending_buf_size = 0;
  std::uint8_t* pending_out = nullptr;
  unsigned long pending = 0;
  int wrap_ = 1;
  int last_flush = -2;
  unsigned w_size = 0, w_bits = 0, w_mask = 0;
  std::vector<std::uint8_t> window_store_;
  std::uint8_t* window = nullptr;
  unsigned long window_size = 0;
  std::vector<Pos> prev_, head_;
  unsigned ins_h = 0, hash_size = 0, hash_mask = 0, hash_shift = 0;
  long block_start = 0;
  unsigned match_length = 0;
  int match_available = 0;
  unsigned strstart = 0, match_start = 0, lookahead = 0;
  unsigned max_chain_length = 0, max_lazy_match = 0;
  int level = 6, strategy = 0;
  unsigned good_match = 0;
  int nice_match = 0;
  detail::CtData dyn_ltree[detail::HEAP_SIZE]{};
  detail::CtData dyn_dtree[2 * detail::D_CODES + 1]{};
  detail::CtData bl_tree[2 * detail::BL_CODES + 1]{};
  detail::TreeDesc l_desc, d_desc, bl_desc;
  std::uint16_t bl_count[detail::MAX_BITS + 1]{};
  int heap[2 * detail::L_CODES + 1]{};
  int heap_len = 0, heap_max = 0;
  std::uint8_t depth[2 * detail::L_CODES + 1]{};
  struct Sym {
    std::uint16_t dist, lc;  // distance (0 for a literal), literal or length - MIN_MATCH
  };
  std::vector<Sym> syms_;
  unsigned sym_next = 0, sym_end = 0;  // (counts of symbols)
  unsigned long opt_len = 0, static_len = 0;
  unsigned insert = 0;
  std::uint16_t bi_buf = 0;
  int bi_valid = 0;
  unsigned long high_water = 0;
};

// compress2(): one-shot zlib-format compression.
inline std::vector<std::uint8_t> compress(std::span<const std::uint8_t> src,
                                          int level = kDefaultCompression) {
  Deflater d(level);
  std::vector<std::uint8_t> out(Deflater::bound(src.size()));
  d.next_in = src.data();
  d.avail_in = static_cast<unsigned>(src.size());
  d.next_out = out.data();
  d.avail_out = static_cast<unsigned>(out.size());
  int ret = d.deflate(kFinish);
  if (ret != kStreamEnd) throw std::runtime_error("zlib: compression failed");
  out.resize(d.total_out);
  return out;
}

}  // namespace vio::png::zlib
