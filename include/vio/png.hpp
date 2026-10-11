// vio::png - PNG codec.
//
// png::Encoder produces files byte-identical to libspng 0.7: per-scanline
// adaptive filter selection, zlib level 6 with Z_FILTERED, 8192-byte IDAT
// chunks, iCCP compressed at the default level.
// png::Decoder decodes all standard PNG variants (all bit depths and color
// types, Adam7 interlacing) into the PNG's native sample layout.
//
// png::Reader/Writer adapt these to vio::ImageReader/ImageWriter.
//
// The zlib functionality PNG needs is included (namespace vio::png::zlib):
//   crc32(), adler32()        checksums
//   Inflater, uncompress()    DEFLATE decompression (RFC 1950/1951)
//   Deflater, compress()      DEFLATE compression with output identical to
//                             zlib 1.3 (port of zlib's deflate.c and trees.c;
//                             zlib license, (C) Jean-loup Gailly and Mark
//                             Adler): zlib-wrapped and raw streams, levels
//                             0..9 and all zlib strategies
#pragma once

#include <array>
#include <cstdlib>

#include "core.hpp"

namespace vio::png::zlib {

// ---------------------------------------------------------------------------
// Checksums
// ---------------------------------------------------------------------------

inline std::uint32_t adler32(std::uint32_t adler, const std::uint8_t* buf, std::size_t len) {
  constexpr std::uint32_t BASE = 65521;
  constexpr std::size_t NMAX = 5552;
  std::uint32_t a = adler & 0xffff, b = (adler >> 16) & 0xffff;
  while (len > 0) {
    std::size_t n = len < NMAX ? len : NMAX;
    len -= n;
    while (n--) {
      a += *buf++;
      b += a;
    }
    a %= BASE;
    b %= BASE;
  }
  return (b << 16) | a;
}

namespace detail {
constexpr std::array<std::array<std::uint32_t, 256>, 8> make_crc_tables() {
  std::array<std::array<std::uint32_t, 256>, 8> t{};
  for (std::uint32_t n = 0; n < 256; n++) {
    std::uint32_t c = n;
    for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    t[0][n] = c;
  }
  for (std::uint32_t n = 0; n < 256; n++)
    for (int k = 1; k < 8; k++) t[k][n] = (t[k - 1][n] >> 8) ^ t[0][t[k - 1][n] & 0xff];
  return t;
}
inline constexpr auto crc_tables = make_crc_tables();
}  // namespace detail

inline std::uint32_t crc32(std::uint32_t crc, const std::uint8_t* buf, std::size_t len) {
  const auto& t = detail::crc_tables;
  crc = ~crc;
  while (len >= 8) {
    std::uint32_t lo = crc ^ (std::uint32_t(buf[0]) | std::uint32_t(buf[1]) << 8 |
                              std::uint32_t(buf[2]) << 16 | std::uint32_t(buf[3]) << 24);
    crc = t[7][lo & 0xff] ^ t[6][(lo >> 8) & 0xff] ^ t[5][(lo >> 16) & 0xff] ^ t[4][lo >> 24] ^
          t[3][buf[4]] ^ t[2][buf[5]] ^ t[1][buf[6]] ^ t[0][buf[7]];
    buf += 8;
    len -= 8;
  }
  while (len--) crc = t[0][(crc ^ *buf++) & 0xff] ^ (crc >> 8);
  return ~crc;
}

// ---------------------------------------------------------------------------
// Inflate
// ---------------------------------------------------------------------------

namespace detail {

// Canonical Huffman decoding table: a primary table indexed by kFastBits
// bit-reversed bits, with overflow resolved by a canonical walk.
struct InflateTable {
  static constexpr int kFastBits = 10;
  // entry: symbol << 4 | length (length 0 = slow path)
  std::uint16_t fast[1 << kFastBits];
  std::uint16_t count[16];
  std::uint16_t symbol[320];
  int max_len = 0;

  // Returns false for an over-subscribed or incomplete code (zlib's rules:
  // an incomplete code is accepted only for literal/distance codes consisting
  // of a single 1-bit code; an empty code is accepted).
  bool build(const std::uint8_t* lengths, int n, bool code_lengths = false) {
    std::memset(count, 0, sizeof(count));
    for (int i = 0; i < n; i++) count[lengths[i]]++;
    count[0] = 0;
    int left = 1;
    max_len = 0;
    for (int len = 1; len < 16; len++) {
      left <<= 1;
      left -= count[len];
      if (left < 0) return false;
      if (count[len]) max_len = len;
    }
    if (max_len != 0 && left > 0 && (code_lengths || max_len != 1)) return false;
    std::uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = static_cast<std::uint16_t>(offs[len] + count[len]);
    for (int i = 0; i < n; i++)
      if (lengths[i]) symbol[offs[lengths[i]]++] = static_cast<std::uint16_t>(i);
    std::memset(fast, 0, sizeof(fast));
    unsigned code = 0;
    int idx = 0;
    for (int len = 1; len <= max_len && len <= kFastBits; len++) {
      for (int k = 0; k < count[len]; k++, idx++, code++) {
        unsigned rev = 0;
        for (int b = 0; b < len; b++) rev |= ((code >> b) & 1u) << (len - 1 - b);
        for (unsigned fill = rev; fill < (1u << kFastBits); fill += 1u << len)
          fast[fill] = static_cast<std::uint16_t>(symbol[idx] << 4 | len);
      }
      code <<= 1;
    }
    return true;
  }
};

inline constexpr std::uint16_t len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                                               15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                                               67, 83, 99, 115, 131, 163, 195, 227, 258};
inline constexpr std::uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
inline constexpr std::uint16_t dist_base[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
inline constexpr std::uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

}  // namespace detail

class Inflater {
 public:
  // raw = true decodes a bare DEFLATE stream (no zlib header/trailer).
  explicit Inflater(bool raw = false) : raw_(raw) {}

  // Decompresses `src` and appends the result to `out`.  Returns the number
  // of input bytes consumed.  If `limit` is nonzero, decoding stops with an
  // error once the output would exceed it.
  std::size_t decompress(std::span<const std::uint8_t> src, std::vector<std::uint8_t>& out,
                         std::size_t limit = 0) {
    in_ = src.data();
    end_ = in_ + src.size();
    bitbuf_ = 0;
    bitcnt_ = 0;
    overrun_ = 0;
    limit_ = limit;
    std::size_t start = out.size();
    if (!raw_) {
      unsigned cmf = byte(), flg = byte();
      if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0)
        fail("incorrect header check");
      if (flg & 0x20) fail("need dictionary");
    }
    int last;
    do {
      last = static_cast<int>(bits(1));
      unsigned type = bits(2);
      if (type == 0) stored(out);
      else if (type == 1) fixed(out);
      else if (type == 2) dynamic(out);
      else fail("invalid block type");
    } while (!last);
    // discard the remaining bits of the last byte, rewinding any whole bytes
    // that were read ahead
    in_ -= bitcnt_ >> 3;
    bitcnt_ = 0;
    if (!raw_) {
      std::uint32_t check = 0;
      for (int i = 0; i < 4; i++) check = check << 8 | byte();
      if (check != adler32(1, out.data() + start, out.size() - start))
        fail("incorrect data check");
    }
    return static_cast<std::size_t>(in_ - src.data());
  }

 private:
  [[noreturn]] static void fail(const char* msg) { VIO_FAIL("zlib: %s", msg); }

  unsigned byte() {
    if (in_ >= end_) fail("unexpected end of stream");
    return *in_++;
  }
  void refill() {
    while (bitcnt_ <= 56) {
      if (in_ < end_) {
        bitbuf_ |= static_cast<std::uint64_t>(*in_++) << bitcnt_;
      } else {
        if (overrun_ > 8) fail("unexpected end of stream");
        overrun_++;
        in_++;  // virtual zero byte; accounted for by rewinding below
      }
      bitcnt_ += 8;
    }
  }
  unsigned bits(int n) {
    if (bitcnt_ < n) refill();
    unsigned v = static_cast<unsigned>(bitbuf_ & ((1ull << n) - 1));
    bitbuf_ >>= n;
    bitcnt_ -= n;
    return v;
  }
  void check_overrun() {
    // bytes past the end that are still in the bit buffer are fine; bytes that
    // have been consumed are not
    std::size_t past = static_cast<std::size_t>(in_ > end_ ? in_ - end_ : 0);
    if (past * 8 > static_cast<std::size_t>(bitcnt_)) fail("unexpected end of stream");
  }

  int decode(const detail::InflateTable& t) {
    if (bitcnt_ < 15) refill();
    unsigned e = t.fast[bitbuf_ & ((1u << detail::InflateTable::kFastBits) - 1)];
    if (e & 15) {
      bitbuf_ >>= e & 15;
      bitcnt_ -= static_cast<int>(e & 15);
      return static_cast<int>(e >> 4);
    }
    // slow canonical decode (bits are stored LSB first, codes MSB first)
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
      code |= static_cast<int>(bitbuf_ & 1);
      bitbuf_ >>= 1;
      bitcnt_--;
      int count = t.count[len];
      if (code - count < first) return t.symbol[index + (code - first)];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    fail("invalid code");
  }

  void grow_check(std::vector<std::uint8_t>& out, std::size_t n) {
    if (limit_ && out.size() + n > limit_) fail("output exceeds limit");
  }

  void stored(std::vector<std::uint8_t>& out) {
    // align to byte boundary
    bits(bitcnt_ & 7);
    in_ -= bitcnt_ >> 3;  // return whole buffered bytes to the input
    bitbuf_ = 0;
    bitcnt_ = 0;
    if (in_ > end_) fail("unexpected end of stream");
    overrun_ = 0;
    unsigned len = byte();
    len |= byte() << 8;
    unsigned nlen = byte();
    nlen |= byte() << 8;
    if ((len ^ 0xffff) != nlen) fail("invalid stored block lengths");
    if (static_cast<std::size_t>(end_ - in_) < len) fail("unexpected end of stream");
    grow_check(out, len);
    out.insert(out.end(), in_, in_ + len);
    in_ += len;
  }

  void fixed(std::vector<std::uint8_t>& out) {
    if (!fixed_built_) {
      std::uint8_t l[288];
      int i = 0;
      for (; i < 144; i++) l[i] = 8;
      for (; i < 256; i++) l[i] = 9;
      for (; i < 280; i++) l[i] = 7;
      for (; i < 288; i++) l[i] = 8;
      fixed_lit_.build(l, 288);
      for (i = 0; i < 32; i++) l[i] = 5;
      fixed_dist_.build(l, 32);
      fixed_built_ = true;
    }
    codes(out, fixed_lit_, fixed_dist_);
  }

  void dynamic(std::vector<std::uint8_t>& out) {
    static constexpr std::uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                               11, 4,  12, 3, 13, 2, 14, 1, 15};
    unsigned nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
    if (nlen > 286 || ndist > 30) fail("too many length or distance symbols");
    std::uint8_t lengths[320] = {};
    for (unsigned i = 0; i < ncode; i++) lengths[order[i]] = static_cast<std::uint8_t>(bits(3));
    detail::InflateTable cl;
    if (!cl.build(lengths, 19, true)) fail("invalid code lengths set");
    unsigned idx = 0;
    std::memset(lengths, 0, sizeof(lengths));
    while (idx < nlen + ndist) {
      int sym = decode(cl);
      if (sym < 16) {
        lengths[idx++] = static_cast<std::uint8_t>(sym);
      } else {
        unsigned rep;
        std::uint8_t val = 0;
        if (sym == 16) {
          if (idx == 0) fail("invalid bit length repeat");
          val = lengths[idx - 1];
          rep = 3 + bits(2);
        } else if (sym == 17) {
          rep = 3 + bits(3);
        } else {
          rep = 11 + bits(7);
        }
        if (idx + rep > nlen + ndist) fail("invalid bit length repeat");
        while (rep--) lengths[idx++] = val;
      }
    }
    if (lengths[256] == 0) fail("invalid code -- missing end-of-block");
    if (!lit_.build(lengths, static_cast<int>(nlen))) fail("invalid literal/lengths set");
    if (!dist_.build(lengths + nlen, static_cast<int>(ndist))) fail("invalid distances set");
    codes(out, lit_, dist_);
  }

  void codes(std::vector<std::uint8_t>& out, const detail::InflateTable& lit,
             const detail::InflateTable& dist) {
    std::size_t pos = out.size();
    std::size_t cap = out.size();
    auto reserve = [&](std::size_t n) {
      if (pos + n > cap) {
        std::size_t ncap = std::max<std::size_t>(cap * 2, pos + n + 65536);
        if (limit_) ncap = std::min(ncap, std::max(limit_, pos + n));
        out.resize(ncap);
        cap = ncap;
      }
    };
    out.resize(cap);
    for (;;) {
      int sym = decode(lit);
      if (sym < 256) {
        if (limit_ && pos + 1 > limit_) fail("output exceeds limit");
        reserve(1);
        out[pos++] = static_cast<std::uint8_t>(sym);
      } else if (sym == 256) {
        break;
      } else {
        sym -= 257;
        if (sym >= 29) fail("invalid literal/length code");
        unsigned len = detail::len_base[sym] + bits(detail::len_extra[sym]);
        int ds = decode(dist);
        if (ds >= 30) fail("invalid distance code");
        unsigned d = detail::dist_base[ds] + bits(detail::dist_extra[ds]);
        if (d > pos) fail("invalid distance too far back");
        if (limit_ && pos + len > limit_) fail("output exceeds limit");
        reserve(len);
        std::uint8_t* p = out.data() + pos;
        const std::uint8_t* q = p - d;
        if (d >= len) {
          std::memcpy(p, q, len);
        } else {
          for (unsigned i = 0; i < len; i++) p[i] = q[i];
        }
        pos += len;
      }
    }
    check_overrun();
    out.resize(pos);
  }

  bool raw_;
  const std::uint8_t* in_ = nullptr;
  const std::uint8_t* end_ = nullptr;
  std::uint64_t bitbuf_ = 0;
  int bitcnt_ = 0;
  int overrun_ = 0;
  std::size_t limit_ = 0;
  bool fixed_built_ = false;
  detail::InflateTable fixed_lit_, fixed_dist_, lit_, dist_;
};

// uncompress(): one-shot zlib-format decompression.
inline std::vector<std::uint8_t> uncompress(std::span<const std::uint8_t> src,
                                            std::size_t limit = 0) {
  std::vector<std::uint8_t> out;
  Inflater().decompress(src, out, limit);
  return out;
}

// ---------------------------------------------------------------------------
// Deflate
// ---------------------------------------------------------------------------

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

inline constexpr int extra_lbits[LENGTH_CODES] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
inline constexpr int extra_dbits[D_CODES] = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,  6,
                                             6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
inline constexpr int extra_blbits[BL_CODES] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 2, 3, 7};
inline constexpr std::uint8_t bl_order[BL_CODES] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                    11, 4,  12, 3, 13, 2, 14, 1, 15};

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
    for (int n = 0; n < (1 << extra_lbits[code]); n++)
      t.length_code[length++] = static_cast<std::uint8_t>(code);
  }
  t.length_code[length - 1] = static_cast<std::uint8_t>(code);
  int dist = 0;
  for (code = 0; code < 16; code++) {
    t.base_dist[code] = dist;
    for (int n = 0; n < (1 << extra_dbits[code]); n++)
      t.dist_code[dist++] = static_cast<std::uint8_t>(code);
  }
  dist >>= 7;
  for (; code < D_CODES; code++) {
    t.base_dist[code] = dist << 7;
    for (int n = 0; n < (1 << (extra_dbits[code] - 7)); n++)
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
inline constexpr StaticTreeDesc static_l_desc = {tables.static_ltree, extra_lbits, LITERALS + 1,
                                                 L_CODES, MAX_BITS};
inline constexpr StaticTreeDesc static_d_desc = {tables.static_dtree, extra_dbits, 0, D_CODES,
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
    hash_bits = static_cast<unsigned>(mem_level) + 7;
    hash_size = 1u << hash_bits;
    hash_mask = hash_size - 1;
    hash_shift = (hash_bits + detail::MIN_MATCH - 1) / detail::MIN_MATCH;
    window_store_.assign(static_cast<std::size_t>(w_size) * 2, 0);
    window = window_store_.data();
    prev_.assign(w_size, 0);
    head_.assign(hash_size, 0);
    high_water = 0;
    lit_bufsize = 1u << (mem_level + 6);
    pending_store_.assign(static_cast<std::size_t>(lit_bufsize) * 4, 0);
    pending_buf = pending_store_.data();
    pending_buf_size = static_cast<unsigned long>(lit_bufsize) * 4;
    sym_buf = pending_buf + lit_bufsize;
    sym_end = (lit_bufsize - 1) * 3;
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
    head_[hash_size - 1] = 0;
    std::memset(head_.data(), 0, (hash_size - 1) * sizeof(Pos));
    slid = 0;
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
    slid = 1;
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
    match_length = prev_length = detail::MIN_MATCH - 1;
    match_available = 0;
    ins_h = 0;
  }

  void put_byte(unsigned c) { pending_buf[pending++] = static_cast<std::uint8_t>(c); }
  void put_short(unsigned w) {
    put_byte(w & 0xff);
    put_byte((w & 0xffff) >> 8);
  }
  void put_short_msb(unsigned b) {
    put_byte((b >> 8) & 0xff);
    put_byte(b & 0xff);
  }

  void flush_pending() {
    tr_flush_bits();
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

  unsigned longest_match(unsigned cur_match) {
    using namespace detail;
    unsigned chain_length = max_chain_length;
    std::uint8_t* scan = window + strstart;
    std::uint8_t* match;
    int len;
    int best_len = static_cast<int>(prev_length);
    int nice = nice_match;
    unsigned limit = strstart > max_dist() ? strstart - max_dist() : 0;
    const Pos* prev = prev_.data();
    unsigned wmask = w_mask;
    std::uint8_t* strend = window + strstart + MAX_MATCH;
    std::uint8_t scan_end1 = scan[best_len - 1];
    std::uint8_t scan_end = scan[best_len];
    if (prev_length >= good_match) chain_length >>= 2;
    if (static_cast<unsigned>(nice) > lookahead) nice = static_cast<int>(lookahead);
    do {
      match = window + cur_match;
      if (match[best_len] != scan_end || match[best_len - 1] != scan_end1 || *match != *scan ||
          *++match != scan[1])
        continue;
      scan += 2, match++;
      do {
      } while (*++scan == *++match && *++scan == *++match && *++scan == *++match &&
               *++scan == *++match && *++scan == *++match && *++scan == *++match &&
               *++scan == *++match && *++scan == *++match && scan < strend);
      len = MAX_MATCH - static_cast<int>(strend - scan);
      scan = strend - MAX_MATCH;
      if (len > best_len) {
        match_start = cur_match;
        best_len = len;
        if (len >= nice) break;
        scan_end1 = scan[best_len - 1];
        scan_end = scan[best_len];
      }
    } while ((cur_match = prev[cur_match & wmask]) > limit && --chain_length != 0);
    if (static_cast<unsigned>(best_len) <= lookahead) return static_cast<unsigned>(best_len);
    return lookahead;
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
        matches = 2;
        std::memcpy(window, next_in - w_size, w_size);
        strstart = w_size;
        insert = strstart;
      } else {
        if (window_size - strstart <= used) {
          strstart -= w_size;
          std::memcpy(window, window + w_size, strstart);
          if (matches < 2) matches++;
          if (insert > strstart) insert = strstart;
        }
        std::memcpy(window + strstart, next_in - used, used);
        strstart += used;
        insert += std::min(used, w_size - insert);
      }
      block_start = strstart;
    }
    if (high_water < strstart) high_water = strstart;
    if (last) {
      bi_used = 8;
      return finish_done;
    }
    if (flush != kNoFlush && flush != kFinish && avail_in == 0 &&
        static_cast<long>(strstart) == block_start)
      return block_done;
    have = static_cast<unsigned>(window_size - strstart);
    if (avail_in > have && block_start >= static_cast<long>(w_size)) {
      block_start -= w_size;
      strstart -= w_size;
      std::memcpy(window, window + w_size, strstart);
      if (matches < 2) matches++;
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
    if (last) bi_used = 8;
    return last ? finish_started : need_more;
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
        match_length = longest_match(hash_head);
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
      prev_length = match_length, prev_match = match_start;
      match_length = MIN_MATCH - 1;
      if (hash_head != 0 && prev_length < max_lazy_match && strstart - hash_head <= max_dist()) {
        match_length = longest_match(hash_head);
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

  // ---- trees.c ---------------------------------------------------------------
  static unsigned d_code(unsigned dist) {
    return dist < 256 ? detail::tables.dist_code[dist] : detail::tables.dist_code[256 + (dist >> 7)];
  }

  bool tally_lit(unsigned c) {
    sym_buf[sym_next++] = 0;
    sym_buf[sym_next++] = 0;
    sym_buf[sym_next++] = static_cast<std::uint8_t>(c);
    dyn_ltree[c].fc++;
    return sym_next == sym_end;
  }
  bool tally_dist(unsigned distance, unsigned length) {
    std::uint8_t len = static_cast<std::uint8_t>(length);
    std::uint16_t dist = static_cast<std::uint16_t>(distance);
    sym_buf[sym_next++] = static_cast<std::uint8_t>(dist);
    sym_buf[sym_next++] = static_cast<std::uint8_t>(dist >> 8);
    sym_buf[sym_next++] = len;
    dist--;
    dyn_ltree[detail::tables.length_code[len] + detail::LITERALS + 1].fc++;
    dyn_dtree[d_code(dist)].fc++;
    return sym_next == sym_end;
  }

  void send_bits(int value, int length) {
    int len = length;
    if (bi_valid > detail::Buf_size - len) {
      int val = value;
      bi_buf = static_cast<std::uint16_t>(bi_buf | (static_cast<std::uint16_t>(val) << bi_valid));
      put_short(bi_buf);
      bi_buf = static_cast<std::uint16_t>(static_cast<std::uint16_t>(val) >>
                                          (detail::Buf_size - bi_valid));
      bi_valid += len - detail::Buf_size;
    } else {
      bi_buf = static_cast<std::uint16_t>(bi_buf | (static_cast<std::uint16_t>(value) << bi_valid));
      bi_valid += len;
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
    bi_used = ((bi_valid - 1) & 7) + 1;
    bi_buf = 0;
    bi_valid = 0;
  }

  void init_block() {
    for (int n = 0; n < detail::L_CODES; n++) dyn_ltree[n].fc = 0;
    for (int n = 0; n < detail::D_CODES; n++) dyn_dtree[n].fc = 0;
    for (int n = 0; n < detail::BL_CODES; n++) bl_tree[n].fc = 0;
    dyn_ltree[detail::END_BLOCK].fc = 1;
    opt_len = static_len = 0L;
    sym_next = matches = 0;
  }

  void tr_init() {
    l_desc = {dyn_ltree, 0, &detail::static_l_desc};
    d_desc = {dyn_dtree, 0, &detail::static_d_desc};
    bl_desc = {bl_tree, 0, &detail::static_bl_desc};
    bi_buf = 0;
    bi_valid = 0;
    bi_used = 0;
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

  void scan_tree(detail::CtData* tree, int max_code) {
    using namespace detail;
    int prevlen = -1, curlen, nextlen = tree[0].dl, count = 0, max_count = 7, min_count = 4;
    if (nextlen == 0) max_count = 138, min_count = 3;
    tree[max_code + 1].dl = 0xffff;
    for (int n = 0; n <= max_code; n++) {
      curlen = nextlen;
      nextlen = tree[n + 1].dl;
      if (++count < max_count && curlen == nextlen) {
        continue;
      } else if (count < min_count) {
        bl_tree[curlen].fc = static_cast<std::uint16_t>(bl_tree[curlen].fc + count);
      } else if (curlen != 0) {
        if (curlen != prevlen) bl_tree[curlen].fc++;
        bl_tree[REP_3_6].fc++;
      } else if (count <= 10) {
        bl_tree[REPZ_3_10].fc++;
      } else {
        bl_tree[REPZ_11_138].fc++;
      }
      count = 0;
      prevlen = curlen;
      if (nextlen == 0) max_count = 138, min_count = 3;
      else if (curlen == nextlen) max_count = 6, min_count = 3;
      else max_count = 7, min_count = 4;
    }
  }

  void send_tree(const detail::CtData* tree, int max_code) {
    using namespace detail;
    int prevlen = -1, curlen, nextlen = tree[0].dl, count = 0, max_count = 7, min_count = 4;
    if (nextlen == 0) max_count = 138, min_count = 3;
    for (int n = 0; n <= max_code; n++) {
      curlen = nextlen;
      nextlen = tree[n + 1].dl;
      if (++count < max_count && curlen == nextlen) {
        continue;
      } else if (count < min_count) {
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
      count = 0;
      prevlen = curlen;
      if (nextlen == 0) max_count = 138, min_count = 3;
      else if (curlen == nextlen) max_count = 6, min_count = 3;
      else max_count = 7, min_count = 4;
    }
  }

  int build_bl_tree() {
    using namespace detail;
    scan_tree(dyn_ltree, l_desc.max_code);
    scan_tree(dyn_dtree, d_desc.max_code);
    build_tree(&bl_desc);
    int max_blindex;
    for (max_blindex = BL_CODES - 1; max_blindex >= 3; max_blindex--)
      if (bl_tree[bl_order[max_blindex]].dl != 0) break;
    opt_len += 3 * (static_cast<unsigned long>(max_blindex) + 1) + 5 + 5 + 4;
    return max_blindex;
  }

  void send_all_trees(int lcodes, int dcodes, int blcodes) {
    using namespace detail;
    send_bits(lcodes - 257, 5);
    send_bits(dcodes - 1, 5);
    send_bits(blcodes - 4, 4);
    for (int rank = 0; rank < blcodes; rank++) send_bits(bl_tree[bl_order[rank]].dl, 3);
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

  void tr_flush_bits() { bi_flush(); }

  void tr_align() {
    send_bits(detail::STATIC_TREES << 1, 3);
    send_code(detail::END_BLOCK, detail::tables.static_ltree);
    bi_flush();
  }

  void compress_block(const detail::CtData* ltree, const detail::CtData* dtree) {
    using namespace detail;
    unsigned sx = 0;
    if (sym_next != 0) do {
        unsigned dist = sym_buf[sx++] & 0xff;
        dist += static_cast<unsigned>(sym_buf[sx++] & 0xff) << 8;
        int lc = sym_buf[sx++];
        if (dist == 0) {
          send_code(lc, ltree);
        } else {
          unsigned code = tables.length_code[lc];
          send_code(static_cast<int>(code) + LITERALS + 1, ltree);
          int extra = extra_lbits[code];
          if (extra != 0) {
            lc -= tables.base_length[code];
            send_bits(lc, extra);
          }
          dist--;
          code = d_code(dist);
          send_code(static_cast<int>(code), dtree);
          extra = extra_dbits[code];
          if (extra != 0) {
            dist -= static_cast<unsigned>(tables.base_dist[code]);
            send_bits(static_cast<int>(dist), extra);
          }
        }
      } while (sx < sym_next);
    send_code(END_BLOCK, ltree);
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
  unsigned ins_h = 0, hash_size = 0, hash_bits = 0, hash_mask = 0, hash_shift = 0;
  long block_start = 0;
  unsigned match_length = 0, prev_match = 0;
  int match_available = 0;
  unsigned strstart = 0, match_start = 0, lookahead = 0, prev_length = 0;
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
  std::uint8_t* sym_buf = nullptr;
  unsigned lit_bufsize = 0, sym_next = 0, sym_end = 0;
  unsigned long opt_len = 0, static_len = 0;
  unsigned matches = 0, insert = 0;
  std::uint16_t bi_buf = 0;
  int bi_valid = 0, bi_used = 0;
  unsigned long high_water = 0;
  int slid = 0;
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

namespace vio::png {

enum ColorType : std::uint8_t {
  kGray = 0,
  kTruecolor = 2,
  kIndexed = 3,
  kGrayAlpha = 4,
  kTruecolorAlpha = 6,
};

struct Header {
  std::uint32_t width = 0, height = 0;
  std::uint8_t bit_depth = 8;
  std::uint8_t color_type = kTruecolor;
  std::uint8_t interlace = 0;
};

struct PaletteEntry {
  std::uint8_t red, green, blue;
};

inline int num_channels(std::uint8_t color_type) {
  switch (color_type) {
    case kGray: case kIndexed: return 1;
    case kGrayAlpha: return 2;
    case kTruecolor: return 3;
    default: return 4;
  }
}

namespace detail {
inline constexpr std::uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

inline std::uint32_t read_u32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
}
inline void write_u32(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>(v >> 24);
  p[1] = static_cast<std::uint8_t>(v >> 16);
  p[2] = static_cast<std::uint8_t>(v >> 8);
  p[3] = static_cast<std::uint8_t>(v);
}

inline std::uint8_t paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
  int p = a + b - c;
  int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

inline constexpr int adam7_x_start[7] = {0, 4, 0, 2, 0, 1, 0};
inline constexpr int adam7_y_start[7] = {0, 0, 4, 0, 2, 0, 1};
inline constexpr int adam7_x_delta[7] = {8, 8, 4, 4, 2, 2, 1};
inline constexpr int adam7_y_delta[7] = {8, 8, 8, 4, 4, 2, 2};

[[noreturn]] inline void fail(const char* msg) { VIO_FAIL("libspng error: %s", msg); }
}  // namespace detail
using detail::fail;

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------
class Encoder {
 public:
  Header header;
  std::vector<PaletteEntry> palette;  // required for kIndexed
  std::vector<std::uint8_t> icc_profile;
  std::string icc_name = "ICC Profile";

  // Writes the signature and the chunks preceding IDAT.
  void begin() {
    using namespace detail;
    const Header& h = header;
    if (h.width == 0 || h.height == 0) fail("invalid image width");
    if (h.color_type == kIndexed && palette.empty()) fail("missing palette");
    out_.put(signature, 8);
    std::uint8_t ihdr[13];
    write_u32(ihdr, h.width);
    write_u32(ihdr + 4, h.height);
    ihdr[8] = h.bit_depth;
    ihdr[9] = h.color_type;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    write_chunk("IHDR", ihdr, 13);
    if (!icc_profile.empty()) {
      std::vector<std::uint8_t> z = zlib::compress(icc_profile, zlib::kDefaultCompression);
      std::vector<std::uint8_t> data(icc_name.begin(), icc_name.end());
      data.push_back(0);
      data.push_back(0);  // compression method
      data.insert(data.end(), z.begin(), z.end());
      write_chunk("iCCP", data.data(), data.size());
    }
    if (!palette.empty()) {
      std::vector<std::uint8_t> data;
      for (const auto& e : palette) {
        data.push_back(e.red);
        data.push_back(e.green);
        data.push_back(e.blue);
      }
      write_chunk("PLTE", data.data(), data.size());
    }

    bpp_ = h.bit_depth < 8 ? 1u : static_cast<unsigned>(num_channels(h.color_type) * (h.bit_depth / 8));
    row_bytes_ = (static_cast<std::size_t>(h.width) * num_channels(h.color_type) * h.bit_depth + 7) / 8;
    filter_ = !(h.color_type == kIndexed || h.bit_depth < 8);
    deflater_ = std::make_unique<zlib::Deflater>(zlib::kDefaultCompression, 15, 8,
                                                 filter_ ? zlib::kFiltered : zlib::kDefaultStrategy);
    scanline_.assign(row_bytes_ + 1, 0);
    prev_.assign(row_bytes_ + 1, 0);
    filtered_.assign(row_bytes_ + 1, 0);
    rows_done_ = 0;
    open_idat();
  }

  // Encodes one row in native layout (16-bit samples in host byte order).
  void write_row(const void* row) {
    if (rows_done_ >= header.height) return;
    std::uint8_t* cur = scanline_.data() + 1;
    std::memcpy(cur, row, row_bytes_);
    if (header.bit_depth == 16) {
      for (std::size_t i = 0; i + 1 < row_bytes_ + 1 && i < row_bytes_; i += 2) {
        std::uint16_t v;
        std::memcpy(&v, cur + i, 2);
        cur[i] = static_cast<std::uint8_t>(v >> 8);
        cur[i + 1] = static_cast<std::uint8_t>(v);
      }
    }
    const std::uint8_t* prev = prev_.data() + 1;  // zeros for the first scanline
    unsigned filter = filter_ ? best_filter(prev, cur) : 0;
    const std::uint8_t* emit;
    if (filter == 0) {
      scanline_[0] = 0;
      emit = scanline_.data();
    } else {
      apply_filter(filtered_.data() + 1, prev, cur, filter);
      filtered_[0] = static_cast<std::uint8_t>(filter);
      emit = filtered_.data();
    }
    write_idat_bytes(emit, row_bytes_ + 1);
    std::swap(prev_, scanline_);
    if (++rows_done_ == header.height) finish_idat();
  }

  // Completes the file (IEND) and returns it.
  std::vector<std::uint8_t> finish() {
    if (rows_done_ < header.height) fail("image incomplete");
    write_chunk("IEND", nullptr, 0);
    return out_.take();
  }

 private:
  void write_chunk(const char* type, const std::uint8_t* data, std::size_t len) {
    std::uint8_t hdr[8];
    detail::write_u32(hdr, static_cast<std::uint32_t>(len));
    std::memcpy(hdr + 4, type, 4);
    out_.put(hdr, 8);
    if (len) out_.put(data, len);
    std::uint32_t crc = zlib::crc32(0, hdr + 4, 4);
    if (len) crc = zlib::crc32(crc, data, len);
    std::uint8_t c[4];
    detail::write_u32(c, crc);
    out_.put(c, 4);
  }

  static constexpr unsigned kWriteSize = 8192;

  void open_idat() {
    chunk_start_ = out_.size();
    std::uint8_t* p = out_.grow(8 + kWriteSize + 4);
    std::memcpy(p + 4, "IDAT", 4);
    deflater_->next_out = p + 8;
    deflater_->avail_out = kWriteSize;
  }
  void close_idat(unsigned length) {
    auto& b = out_.bytes();
    std::uint8_t* hdr = b.data() + chunk_start_;
    detail::write_u32(hdr, length);
    std::uint32_t crc = zlib::crc32(0, hdr + 4, 4 + length);
    detail::write_u32(hdr + 8 + length, crc);
    b.resize(chunk_start_ + 12 + length);
  }
  void write_idat_bytes(const std::uint8_t* data, std::size_t len) {
    auto& z = *deflater_;
    z.next_in = data;
    z.avail_in = static_cast<unsigned>(len);
    int ret;
    do {
      ret = z.deflate(zlib::kNoFlush);
      if (z.avail_out == 0) {
        close_idat(kWriteSize);
        open_idat();
      }
    } while (z.avail_in);
    if (ret != zlib::kOk) fail("zlib error");
  }
  void finish_idat() {
    auto& z = *deflater_;
    int ret = 0;
    while (ret != zlib::kStreamEnd) {
      ret = z.deflate(zlib::kFinish);
      if (ret == zlib::kStreamEnd) break;
      if (ret != zlib::kOk && ret != zlib::kBufError) fail("zlib error");
      if (z.avail_out == 0) {
        close_idat(kWriteSize);
        open_idat();
      }
    }
    close_idat(kWriteSize - z.avail_out);
    deflater_.reset();
  }

  void apply_filter(std::uint8_t* out, const std::uint8_t* prev, const std::uint8_t* cur,
                    unsigned filter) const {
    for (std::size_t i = 0; i < row_bytes_; i++) {
      std::uint8_t a = i >= bpp_ ? cur[i - bpp_] : 0, b = prev[i],
                   c = i >= bpp_ ? prev[i - bpp_] : 0, x = cur[i];
      switch (filter) {
        case 1: x = static_cast<std::uint8_t>(x - a); break;
        case 2: x = static_cast<std::uint8_t>(x - b); break;
        case 3: x = static_cast<std::uint8_t>(x - (a + b) / 2); break;
        case 4: x = static_cast<std::uint8_t>(x - detail::paeth(a, b, c)); break;
      }
      out[i] = x;
    }
  }

  unsigned best_filter(const std::uint8_t* prev, const std::uint8_t* cur) const {
    if (row_bytes_ > INT32_MAX / 128) return 0;
    std::int32_t best_score = INT32_MAX;
    unsigned best = 0;
    for (unsigned f = 0; f < 5; f++) {
      std::int32_t sum = 0;
      for (std::size_t i = 0; i < row_bytes_; i++) {
        std::uint8_t a = i >= bpp_ ? cur[i - bpp_] : 0, b = prev[i],
                     c = i >= bpp_ ? prev[i - bpp_] : 0, x = cur[i];
        switch (f) {
          case 1: x = static_cast<std::uint8_t>(x - a); break;
          case 2: x = static_cast<std::uint8_t>(x - b); break;
          case 3: x = static_cast<std::uint8_t>(x - (a + b) / 2); break;
          case 4: x = static_cast<std::uint8_t>(x - detail::paeth(a, b, c)); break;
          default: break;
        }
        sum += 128 - std::abs(static_cast<int>(x) - 128);
      }
      std::int32_t score = std::abs(sum);
      if (score < best_score) {
        best_score = score;
        best = f;
      }
    }
    return best;
  }

  vio::detail::OutBuffer out_;
  std::unique_ptr<zlib::Deflater> deflater_;
  std::vector<std::uint8_t> scanline_, prev_, filtered_;
  std::size_t row_bytes_ = 0, chunk_start_ = 0;
  unsigned bpp_ = 1;
  bool filter_ = true;
  std::uint32_t rows_done_ = 0;
};

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------
class Decoder {
 public:
  Header header;
  std::vector<PaletteEntry> palette;
  std::vector<std::uint8_t> icc_profile;
  bool has_icc = false;

  // Parses the signature and IHDR chunk (spng_get_ihdr()).
  void read_header(std::span<const std::uint8_t> file) {
    using namespace detail;
    file_ = file;
    if (file.size() < 8 || std::memcmp(file.data(), signature, 8) != 0) fail("invalid signature");
    pos_ = 8;
    const std::uint8_t *type, *data;
    std::uint32_t len;
    if (!next_chunk(type, data, len)) fail("invalid chunk checksum");
    if (std::memcmp(type, "IHDR", 4) != 0 || len != 13) fail("missing IHDR chunk");
    parse_ihdr(data);
    have_header_ = true;
  }

  // Parses the remaining chunks and decodes the image.  Samples are stored
  // in native PNG layout, rows of row_bytes() bytes, 16-bit samples in host
  // byte order, sub-byte samples packed MSB first.
  void decode(std::span<const std::uint8_t> file, unsigned long long max_pixels = 0) {
    if (!have_header_ || file.data() != file_.data()) read_header(file);
    if (max_pixels && static_cast<unsigned long long>(header.width) * header.height > max_pixels)
      VIO_FAIL("Maximum supported image dimension is %u pixels",
               static_cast<unsigned>(max_pixels));
    decode_rest();
  }

 private:
  // Reads the next chunk; returns false if its CRC is bad.
  bool next_chunk(const std::uint8_t*& type, const std::uint8_t*& data, std::uint32_t& len) {
    using namespace detail;
    if (file_.size() - pos_ < 12) fail("end of stream");
    len = read_u32(file_.data() + pos_);
    if (len > 0x7fffffffu) fail("chunk length exceeds maximum");
    type = file_.data() + pos_ + 4;
    if (file_.size() - pos_ - 12 < len) fail("end of stream");
    data = type + 4;
    std::uint32_t crc = read_u32(data + len);
    pos_ += 12 + static_cast<std::size_t>(len);
    return zlib::crc32(0, type, 4 + static_cast<std::size_t>(len)) == crc;
  }

  void decode_rest() {
    bool seen_idat = false, idat_done = false, seen_iend = false;
    std::vector<std::uint8_t> idat;
    while (!seen_iend) {
      const std::uint8_t *type, *data;
      std::uint32_t len;
      bool crc_ok = next_chunk(type, data, len);
      bool critical = !(type[0] & 0x20);
      if (!crc_ok) {
        if (critical) fail("invalid chunk checksum");
        continue;  // ancillary chunks with bad CRCs are discarded
      }
      auto is = [&](const char* t) { return std::memcmp(type, t, 4) == 0; };
      if (is("IDAT")) {
        if (idat_done) fail("non-contiguous IDAT chunks");
        if (header.color_type == kIndexed && palette.empty()) fail("missing palette");
        seen_idat = true;
        idat.insert(idat.end(), data, data + len);
      } else {
        if (seen_idat) idat_done = true;
        if (is("IEND")) {
          seen_iend = true;
        } else if (is("PLTE")) {
          if (len % 3 != 0 || len == 0 || len / 3 > 256) fail("invalid palette");
          palette.resize(len / 3);
          for (std::uint32_t i = 0; i < len / 3; i++)
            palette[i] = {data[3 * i], data[3 * i + 1], data[3 * i + 2]};
        } else if (is("iCCP") && !seen_idat) {
          parse_iccp(data, len);
        } else if (is("IHDR")) {
          fail("duplicate IHDR chunk");
        } else if (critical) {
          fail("unknown critical chunk");
        }
      }
    }
    if (!seen_idat) fail("missing IDAT chunk");
    decompress(idat);
  }

 public:
  std::size_t row_bytes() const { return row_bytes_; }
  const std::uint8_t* row(std::uint32_t y) const { return pixels_.data() + y * row_bytes_; }
  std::vector<std::uint8_t>& pixels() { return pixels_; }

 private:
  void parse_ihdr(const std::uint8_t* d) {
    using namespace detail;
    header.width = read_u32(d);
    header.height = read_u32(d + 4);
    header.bit_depth = d[8];
    header.color_type = d[9];
    header.interlace = d[12];
    if (header.width == 0 || header.width > 0x7fffffffu) fail("invalid image width");
    if (header.height == 0 || header.height > 0x7fffffffu) fail("invalid image height");
    unsigned bd = header.bit_depth;
    bool ok;
    switch (header.color_type) {
      case kGray: ok = bd == 1 || bd == 2 || bd == 4 || bd == 8 || bd == 16; break;
      case kIndexed: ok = bd == 1 || bd == 2 || bd == 4 || bd == 8; break;
      case kTruecolor: case kGrayAlpha: case kTruecolorAlpha: ok = bd == 8 || bd == 16; break;
      default: fail("invalid color type");
    }
    if (!ok) fail("invalid bit depth");
    if (d[10] != 0) fail("invalid compression method");
    if (d[11] != 0) fail("invalid filter method");
    if (d[12] > 1) fail("invalid interlace method");
    row_bytes_ = (static_cast<std::size_t>(header.width) * num_channels(header.color_type) *
                      header.bit_depth + 7) / 8;
  }

  void parse_iccp(const std::uint8_t* d, std::uint32_t len) {
    std::uint32_t n = 0;
    while (n < len && n < 80 && d[n] != 0) n++;
    if (n == 0 || n >= 80 || n + 2 > len) fail("invalid iCCP profile name");
    if (d[n + 1] != 0) fail("invalid iCCP compression method");
    try {
      icc_profile = zlib::uncompress(std::span(d + n + 2, len - n - 2));
      has_icc = true;
    } catch (const Error&) {
      fail("inflate error");
    }
  }

  void unfilter(std::uint8_t* cur, const std::uint8_t* prev, std::size_t n, unsigned bpp,
                unsigned filter) {
    switch (filter) {
      case 0: break;
      case 1:
        for (std::size_t i = bpp; i < n; i++) cur[i] = static_cast<std::uint8_t>(cur[i] + cur[i - bpp]);
        break;
      case 2:
        for (std::size_t i = 0; i < n; i++) cur[i] = static_cast<std::uint8_t>(cur[i] + prev[i]);
        break;
      case 3:
        for (std::size_t i = 0; i < n; i++) {
          unsigned a = i >= bpp ? cur[i - bpp] : 0;
          cur[i] = static_cast<std::uint8_t>(cur[i] + ((a + prev[i]) >> 1));
        }
        break;
      case 4:
        for (std::size_t i = 0; i < n; i++) {
          std::uint8_t a = i >= bpp ? cur[i - bpp] : 0, c = i >= bpp ? prev[i - bpp] : 0;
          cur[i] = static_cast<std::uint8_t>(cur[i] + detail::paeth(a, prev[i], c));
        }
        break;
      default:
        detail::fail("invalid filter type");
    }
  }

  void decompress(const std::vector<std::uint8_t>& idat) {
    using namespace detail;
    const Header& h = header;
    const int ch = num_channels(h.color_type);
    const unsigned bits_pp = static_cast<unsigned>(ch * h.bit_depth);
    const unsigned bpp = bits_pp < 8 ? 1u : bits_pp / 8;
    // Expected size of the filtered data stream.
    std::size_t expected = 0;
    struct Pass { std::uint32_t w, h; int p; };
    std::vector<Pass> passes;
    if (h.interlace) {
      for (int p = 0; p < 7; p++) {
        std::uint32_t pw = (h.width - adam7_x_start[p] + adam7_x_delta[p] - 1) / adam7_x_delta[p];
        std::uint32_t ph = (h.height - adam7_y_start[p] + adam7_y_delta[p] - 1) / adam7_y_delta[p];
        if (h.width <= static_cast<std::uint32_t>(adam7_x_start[p])) pw = 0;
        if (h.height <= static_cast<std::uint32_t>(adam7_y_start[p])) ph = 0;
        passes.push_back({pw, ph, p});
        if (pw && ph) expected += ph * ((static_cast<std::size_t>(pw) * bits_pp + 7) / 8 + 1);
      }
    } else {
      passes.push_back({h.width, h.height, -1});
      expected = static_cast<std::size_t>(h.height) * (row_bytes_ + 1);
    }
    std::vector<std::uint8_t> raw;
    raw.reserve(expected);
    try {
      zlib::Inflater().decompress(idat, raw, expected);
    } catch (const Error& e) {
      if (raw.size() < expected) fail("end of stream");
    }
    if (raw.size() < expected) fail("end of stream");
    pixels_.assign(row_bytes_ * h.height, 0);
    std::size_t off = 0;
    for (const Pass& ps : passes) {
      if (!ps.w || !ps.h) continue;
      std::size_t rb = (static_cast<std::size_t>(ps.w) * bits_pp + 7) / 8;
      std::vector<std::uint8_t> prev(rb, 0);
      for (std::uint32_t y = 0; y < ps.h; y++) {
        std::uint8_t* line = raw.data() + off;
        unfilter(line + 1, prev.data(), rb, bpp, line[0]);
        std::memcpy(prev.data(), line + 1, rb);
        off += rb + 1;
        if (ps.p < 0) {
          std::memcpy(pixels_.data() + y * row_bytes_, line + 1, rb);
        } else {
          std::uint32_t oy = adam7_y_start[ps.p] + y * adam7_y_delta[ps.p];
          std::uint8_t* dst = pixels_.data() + oy * row_bytes_;
          for (std::uint32_t x = 0; x < ps.w; x++) {
            std::size_t ox = adam7_x_start[ps.p] + static_cast<std::size_t>(x) * adam7_x_delta[ps.p];
            if (bits_pp >= 8) {
              std::memcpy(dst + ox * bpp, line + 1 + x * bpp, bpp);
            } else {
              unsigned spb = 8 / bits_pp;
              unsigned sample = (line[1 + x / spb] >> (8 - bits_pp - (x % spb) * bits_pp)) &
                                ((1u << bits_pp) - 1);
              dst[ox / spb] |= static_cast<std::uint8_t>(sample << (8 - bits_pp - (ox % spb) * bits_pp));
            }
          }
        }
      }
    }
    if (h.bit_depth == 16) {
      for (std::size_t i = 0; i + 1 < pixels_.size(); i += 2) {
        std::uint16_t v = static_cast<std::uint16_t>(pixels_[i] << 8 | pixels_[i + 1]);
        std::memcpy(&pixels_[i], &v, 2);
      }
    }
  }

  std::span<const std::uint8_t> file_;
  std::size_t pos_ = 0;
  bool have_header_ = false;
  std::size_t row_bytes_ = 0;
  std::vector<std::uint8_t> pixels_;
};


// ---------------------------------------------------------------------------
// Source / Destination adapters
// ---------------------------------------------------------------------------
template <class T>
class Reader : public ImageReader<T> {
  using Base = ImageReader<T>;

 public:
  using Base::color;
  using Base::precision;
  using Base::width;
  using Base::height;
  using Base::components;
  using Base::in_;

 public:
  std::vector<std::uint8_t> icc_profile() override {
    return reader_.has_icc ? reader_.icc_profile : std::vector<std::uint8_t>{};
  }

 protected:
  void start_input() override {
    reader_ = Decoder();
    reader_.read_header(in_.data());
    const png::Header& h = reader_.header;
    if (h.width > kMaxDimension || h.height > kMaxDimension)
      VIO_FAIL("Maximum supported image dimension is %u pixels", kMaxDimension);
    if (h.bit_depth != 8 && h.bit_depth != 16)
      VIO_FAIL("Only 8- and 16-bit-per-channel PNG files are supported");
    if (this->max_pixels && static_cast<unsigned long long>(h.width) * h.height > this->max_pixels)
      VIO_FAIL("Maximum supported image dimension is %u pixels",
               static_cast<unsigned>(this->max_pixels));
    reader_.decode(in_.data());
    width = h.width;
    height = h.height;
    switch (h.color_type) {
      case png::kGray:
      case png::kGrayAlpha:
        if (color == ColorSpace::Unknown || color == ColorSpace::RGB) color = ColorSpace::Gray;
        if (color != ColorSpace::Gray && color != ColorSpace::CMYK && !vio::detail::is_rgb_family(color))
          VIO_FAIL("Bogus input colorspace");
        png_comps_ = h.color_type == png::kGrayAlpha ? 2 : 1;
        break;
      case png::kTruecolor:
      case png::kTruecolorAlpha:
        if (color == ColorSpace::Unknown) color = ColorSpace::ExtRGB;
        if (color != ColorSpace::CMYK && !vio::detail::is_rgb_family(color))
          VIO_FAIL("Bogus input colorspace");
        png_comps_ = h.color_type == png::kTruecolorAlpha ? 4 : 3;
        break;
      case png::kIndexed: {
        if (h.bit_depth != 8 || reader_.palette.size() > 256)
          VIO_FAIL("Numeric value out of range in PNG file");
        bool gray = true;
        for (const auto& e : reader_.palette)
          if (e.red != e.green || e.green != e.blue) gray = false;
        if ((color == ColorSpace::Unknown || color == ColorSpace::RGB) && gray)
          color = ColorSpace::Gray;
        if (color == ColorSpace::Gray && !gray) VIO_FAIL("Bogus input colorspace");
        if (color == ColorSpace::Unknown) color = ColorSpace::ExtRGB;
        png_comps_ = 1;
        break;
      }
      default:
        VIO_FAIL("Numeric value out of range in PNG file");
    }
    this->set_components_from_color();
    unsigned maxval = h.bit_depth == 16 ? 65535 : 255;
    rescale_.assign(static_cast<std::size_t>(maxval) + 1, T{0});
    std::size_t half = maxval / 2;
    for (std::size_t v = 0; v <= maxval; v++)
      rescale_[v] = static_cast<T>((v * ((1u << precision) - 1) + half) / maxval);
    buf_.allocate(static_cast<std::size_t>(width) * std::max(components, 1), 1);
    row_ptr_ = buf_[0];
    this->buffer_ = &row_ptr_;
    this->buffer_height_ = 1;
  }

  unsigned get_rows() override {
    const png::Header& h = reader_.header;
    const std::uint8_t* raw = reader_.row(this->next_row);
    T* ptr = row_ptr_;
    const int ci = vio::detail::idx(color);
    const T maxs = static_cast<T>((1 << precision) - 1);
    const int alpha = png_comps_ == 2 || png_comps_ == 4 ? 1 : 0;
    if (h.color_type == png::kIndexed) {
      const auto& pal = reader_.palette;
      for (unsigned col = 0; col < width; col++) {
        unsigned index = raw[col];
        if (index >= pal.size()) VIO_FAIL("Numeric value out of range in PNG file");
        T r = rescale_[pal[index].red], g = rescale_[pal[index].green],
          b = rescale_[pal[index].blue];
        if (color == ColorSpace::Gray) {
          *ptr++ = r;
        } else if (color == ColorSpace::CMYK) {
          vio::detail::rgb_to_cmyk<T>(maxs, r, g, b, ptr, ptr + 1, ptr + 2, ptr + 3);
          ptr += 4;
        } else {
          ptr[vio::detail::rgb_red[ci]] = r;
          ptr[vio::detail::rgb_green[ci]] = g;
          ptr[vio::detail::rgb_blue[ci]] = b;
          if (vio::detail::alpha_index[ci] >= 0) ptr[vio::detail::alpha_index[ci]] = maxs;
          ptr += vio::detail::rgb_pixelsize[ci];
        }
      }
      return 1;
    }
    const bool sixteen = h.bit_depth == 16;
    std::size_t si = 0;
    auto sample = [&]() -> T {
      unsigned v;
      if (sixteen) {
        std::uint16_t w;
        std::memcpy(&w, raw + 2 * si, 2);
        v = w;
      } else {
        v = raw[si];
      }
      si++;
      return rescale_[v];
    };
    const bool gray_file = png_comps_ <= 2;
    for (unsigned col = 0; col < width; col++) {
      T r, g, b;
      if (gray_file) {
        r = g = b = sample();
      } else {
        r = sample();
        g = sample();
        b = sample();
      }
      si += static_cast<std::size_t>(alpha);
      if (color == ColorSpace::Gray) {
        *ptr++ = r;
      } else if (color == ColorSpace::CMYK) {
        vio::detail::rgb_to_cmyk<T>(maxs, r, g, b, ptr, ptr + 1, ptr + 2, ptr + 3);
        ptr += 4;
      } else {
        ptr[vio::detail::rgb_red[ci]] = r;
        ptr[vio::detail::rgb_green[ci]] = g;
        ptr[vio::detail::rgb_blue[ci]] = b;
        if (vio::detail::alpha_index[ci] >= 0) ptr[vio::detail::alpha_index[ci]] = maxs;
        ptr += vio::detail::rgb_pixelsize[ci];
      }
    }
    return 1;
  }

 private:
  Decoder reader_;
  int png_comps_ = 3;
  std::vector<T> rescale_;
  vio::detail::RowBuffer<T> buf_;
  T* row_ptr_ = nullptr;
};

template <class T>
class Writer : public ImageWriter<T> {
  using Base = ImageWriter<T>;
  static constexpr int kBitDepth = sizeof(T) == 1 ? 8 : 16;
  using PngSample = std::conditional_t<sizeof(T) == 1, std::uint8_t, std::uint16_t>;

 public:
  void set_icc_profile(std::span<const std::uint8_t> icc) override {
    if (icc.empty()) VIO_FAIL("Empty input file");
    writer_.icc_profile.assign(icc.begin(), icc.end());
  }

  void write_rows(T** rows, unsigned n) override {
    for (unsigned r = 0; r < n; r++) put_row(rows[r]);
  }

 protected:
  void start_output() override {
    const ColorSpace cs = this->color;
    std::uint8_t color_type;
    indexed_ = false;
    if (cs == ColorSpace::Gray) {
      color_type = png::kGray;
    } else if (vio::detail::is_rgb_family(cs) || cs == ColorSpace::CMYK) {
      if (!vio::detail::is_rgb_family(cs) && this->quantized())
        VIO_FAIL("PNG output must be grayscale, truecolor, or indexed-color");
      indexed_ = kBitDepth == 8 && this->quantized() && this->precision == 8 &&
                 vio::detail::is_rgb_family(cs);
      color_type = indexed_ ? png::kIndexed : png::kTruecolor;
    } else {
      VIO_FAIL("PNG output must be grayscale, truecolor, or indexed-color");
    }
    writer_.header.width = this->width;
    writer_.header.height = this->height;
    writer_.header.bit_depth = kBitDepth;
    writer_.header.color_type = color_type;
    if (indexed_) {
      const int ci = vio::detail::idx(cs);
      const auto& cm = this->colormap;
      std::size_t n = cm[0].size();
      writer_.palette.resize(n);
      for (std::size_t i = 0; i < n; i++)
        writer_.palette[i] = {static_cast<std::uint8_t>(cm[vio::detail::rgb_red[ci]][i]),
                              static_cast<std::uint8_t>(cm[vio::detail::rgb_green[ci]][i]),
                              static_cast<std::uint8_t>(cm[vio::detail::rgb_blue[ci]][i])};
    }
    std::size_t samples = static_cast<std::size_t>(this->width) *
                          (cs == ColorSpace::Gray || indexed_ ? 1 : 3);
    iobuf_.assign(samples, 0);
    maxval_ = (1u << this->precision) - 1u;
    if (static_cast<int>(this->precision) != kBitDepth) {
      rescale_.assign(static_cast<std::size_t>(maxval_) + 1, 0);
      long half = maxval_ / 2;
      for (long v = 0; v <= static_cast<long>(maxval_); v++)
        rescale_[static_cast<std::size_t>(v)] =
            static_cast<PngSample>((v * ((1L << kBitDepth) - 1) + half) / maxval_);
    }
    writer_.begin();
  }

  void finish_output() override {
    std::vector<std::uint8_t> file = writer_.finish();
    this->out_.put(file.data(), file.size());
  }

 private:
  PngSample map(unsigned v) const {
    if (rescale_.empty()) return static_cast<PngSample>(v);
    if (v > maxval_) VIO_FAIL("Numeric value out of range in PNG file");
    return rescale_[v];
  }

  void put_row(const T* ptr) {
    const ColorSpace cs = this->color;
    const int ci = vio::detail::idx(cs);
    const unsigned w = this->width;
    PngSample* out = iobuf_.data();
    if (this->quantized()) {
      const auto& cm = this->colormap;
      if (indexed_) {
        for (unsigned col = 0; col < w; col++) *out++ = static_cast<PngSample>(ptr[col]);
      } else if (cs == ColorSpace::Gray) {
        for (unsigned col = 0; col < w; col++)
          *out++ = map(static_cast<unsigned>(static_cast<PngSample>(cm[0][ptr[col]])));
      } else {
        const auto& c0 = cm[vio::detail::rgb_red[ci]];
        const auto& c1 = cm[vio::detail::rgb_green[ci]];
        const auto& c2 = cm[vio::detail::rgb_blue[ci]];
        for (unsigned col = 0; col < w; col++) {
          int v = ptr[col];
          unsigned r = static_cast<PngSample>(c0[v]), g = static_cast<PngSample>(c1[v]),
                   b = static_cast<PngSample>(c2[v]);
          if (!rescale_.empty() && (r > maxval_ || g > maxval_ || b > maxval_))
            VIO_FAIL("Numeric value out of range in PNG file");
          *out++ = map(r);
          *out++ = map(g);
          *out++ = map(b);
        }
      }
    } else if (cs == ColorSpace::Gray) {
      for (unsigned col = 0; col < w; col++)
        *out++ = map(static_cast<PngSample>(ptr[col]));
    } else if (cs == ColorSpace::CMYK) {
      for (unsigned col = 0; col < w; col++, ptr += 4) {
        T r, g, b;
        vio::detail::cmyk_to_rgb<T>(static_cast<int>(maxval_), ptr[0], ptr[1], ptr[2], ptr[3], &r, &g,
                               &b);
        unsigned ur = static_cast<PngSample>(r), ug = static_cast<PngSample>(g),
                 ub = static_cast<PngSample>(b);
        if (!rescale_.empty() && (ur > maxval_ || ug > maxval_ || ub > maxval_))
          VIO_FAIL("Numeric value out of range in PNG file");
        *out++ = map(ur);
        *out++ = map(ug);
        *out++ = map(ub);
      }
    } else {
      const int ri = vio::detail::rgb_red[ci], gi = vio::detail::rgb_green[ci], bi = vio::detail::rgb_blue[ci];
      const int ps = vio::detail::rgb_pixelsize[ci];
      for (unsigned col = 0; col < w; col++, ptr += ps) {
        unsigned r = static_cast<PngSample>(ptr[ri]), g = static_cast<PngSample>(ptr[gi]),
                 b = static_cast<PngSample>(ptr[bi]);
        if (!rescale_.empty() && (r > maxval_ || g > maxval_ || b > maxval_))
          VIO_FAIL("Numeric value out of range in PNG file");
        *out++ = map(r);
        *out++ = map(g);
        *out++ = map(b);
      }
    }
    writer_.write_row(iobuf_.data());
  }

  Encoder writer_;
  bool indexed_ = false;
  unsigned maxval_ = 255;
  std::vector<PngSample> iobuf_;
  std::vector<PngSample> rescale_;
};

}  // namespace vio::png
