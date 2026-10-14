// vio::jpeg - Huffman entropy decoding (sequential, progressive, lossless).
#pragma once

#include <bit>
#include <utility>

// Vector forms of a few entropy-decoding helpers (used when the SIMD level
// in use allows).
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define VIO_HUFF_SSE2 1
#elif defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#define VIO_HUFF_NEON 1
#endif

#include "decompress_state.hpp"
#include "../simd/common.hpp"

namespace vio::jpeg::detail {

inline constexpr int HUFF_LOOKAHEAD = 8;

struct DerivedTable {
  JLong maxcode[18];
  JLong valoffset[18];
  const HuffTable* pub = nullptr;
  std::uint8_t huffval[256];
  int lookup[1 << HUFF_LOOKAHEAD];
};

inline void make_d_derived_tbl(DState& st, bool isDC, int tblno, DerivedTable& dtbl) {
  if (tblno < 0 || tblno >= NUM_HUFF_TBLS) VIO_FAIL("Huffman table 0x%02x was not defined", tblno);
  auto& slot = isDC ? st.dc_huff_tbl_ptrs[tblno] : st.ac_huff_tbl_ptrs[tblno];
  if (!slot) VIO_FAIL("Huffman table 0x%02x was not defined", tblno);
  const HuffTable& htbl = *slot;
  dtbl.pub = &htbl;
  std::memcpy(dtbl.huffval, htbl.huffval.data(), 256);

  char huffsize[257];
  unsigned huffcode[257];
  int p = 0;
  for (int l = 1; l <= 16; l++) {
    int i = htbl.bits[l];
    if (p + i > 256) VIO_FAIL("Bogus Huffman table definition");
    while (i--) huffsize[p++] = static_cast<char>(l);
  }
  huffsize[p] = 0;
  int numsymbols = p;

  unsigned code = 0;
  int si = huffsize[0];
  p = 0;
  while (huffsize[p]) {
    while (huffsize[p] == si) {
      huffcode[p++] = code;
      code++;
    }
    if (static_cast<JLong>(code) >= (static_cast<JLong>(1) << si))
      VIO_FAIL("Bogus Huffman table definition");
    code <<= 1;
    si++;
  }

  p = 0;
  for (int l = 1; l <= 16; l++) {
    if (htbl.bits[l]) {
      dtbl.valoffset[l] = static_cast<JLong>(p) - static_cast<JLong>(huffcode[p]);
      p += htbl.bits[l];
      dtbl.maxcode[l] = huffcode[p - 1];
    } else {
      dtbl.maxcode[l] = -1;
    }
  }
  dtbl.valoffset[17] = 0;
  dtbl.maxcode[17] = 0xFFFFFL;

  for (int i = 0; i < (1 << HUFF_LOOKAHEAD); i++)
    dtbl.lookup[i] = (HUFF_LOOKAHEAD + 1) << HUFF_LOOKAHEAD;
  p = 0;
  for (int l = 1; l <= HUFF_LOOKAHEAD; l++) {
    for (int i = 1; i <= static_cast<int>(htbl.bits[l]); i++, p++) {
      int lookbits = static_cast<int>(huffcode[p] << (HUFF_LOOKAHEAD - l));
      for (int ctr = 1 << (HUFF_LOOKAHEAD - l); ctr > 0; ctr--) {
        dtbl.lookup[lookbits] = (l << HUFF_LOOKAHEAD) | htbl.huffval[p];
        lookbits++;
      }
    }
  }

  if (isDC) {
    for (int i = 0; i < numsymbols; i++) {
      int sym = htbl.huffval[i];
      if (sym < 0 || sym > (st.lossless ? 16 : 15)) VIO_FAIL("Bogus Huffman table definition");
    }
  }
}

VIO_INLINE int huff_extend(int x, int s) {
  return x + ((static_cast<int>(static_cast<unsigned>(x) - (1u << (s - 1))) >> 31) &
              static_cast<int>((~0u << s) + 1));
}

// Bit reader over the DState data source (non-suspending).
class BitReader {
 public:
  using buf_t = std::uint64_t;
  static constexpr int BIT_BUF_SIZE = 64;
  static constexpr int MIN_GET_BITS = BIT_BUF_SIZE - 7;

  explicit BitReader(DState& st) : st_(&st) {}

  void reset() {
    get_buffer = 0;
    bits_left = 0;
  }

  // The out-of-line helpers take and return the bit state by value (as
  // jpeg_fill_bit_buffer() does) so that a local BitReader's get_buffer and
  // bits_left can stay in registers in the decoding loops.
  struct State {
    buf_t get_buffer;
    int bits_left;
  };
  struct Decoded {
    State state;
    int value;
  };

  // jpeg_fill_bit_buffer
  VIO_NOINLINE static State fill(DState* st, State bs, int nbits, bool& insufficient_data) {
    if (st->unread_marker == 0) {
      while (bs.bits_left < MIN_GET_BITS) {
        int c = st->read_byte();
        if (c == 0xFF) {
          do {
            c = st->read_byte();
          } while (c == 0xFF);
          if (c == 0) {
            c = 0xFF;
          } else {
            st->unread_marker = c;
            goto no_more_bytes;
          }
        }
        bs.get_buffer = (bs.get_buffer << 8) | static_cast<unsigned>(c);
        bs.bits_left += 8;
      }
      return bs;
    }
  no_more_bytes:
    if (nbits > bs.bits_left) {
      if (!insufficient_data) {
        st->warn("Corrupt JPEG data: premature end of data segment");
        insufficient_data = true;
      }
      bs.get_buffer <<= MIN_GET_BITS - bs.bits_left;
      bs.bits_left = MIN_GET_BITS;
    }
    return bs;
  }

  // jpeg_huff_decode
  VIO_NOINLINE static Decoded huff_decode_slow(DState* st, State bs, const DerivedTable& htbl,
                                               int min_bits, bool& insuff) {
    BitReader br(*st);
    br.get_buffer = bs.get_buffer;
    br.bits_left = bs.bits_left;
    int l = min_bits;
    br.check(l, insuff);
    JLong code = br.get_bits(l);
    while (code > htbl.maxcode[l]) {
      code <<= 1;
      br.check(1, insuff);
      code |= br.get_bits(1);
      l++;
    }
    State out{br.get_buffer, br.bits_left};
    if (l > 16) {
      st->warn("Corrupt JPEG data: bad Huffman code");
      return {out, 0};
    }
    return {out, htbl.huffval[static_cast<int>(code + htbl.valoffset[l]) & 0xFF]};
  }

  VIO_INLINE void fill(int nbits, bool& insuff) {
    // Fast path: the bytes needed are in the buffer and none is 0xFF; they
    // are shifted in at once (as many as the byte loop below would take).
    if (VIO_LIKELY(st_->bytes_in_buffer >= 8 && st_->unread_marker == 0 && bits_left < MIN_GET_BITS)) {
      std::uint64_t v;
      std::memcpy(&v, st_->next_input_byte, 8);
      v = ::vio::jpeg::simd::HuffBitWriter::to_big_endian(v);
      const int k = (BIT_BUF_SIZE - bits_left) >> 3;  // 1..8
      const std::uint64_t ones = ~v;                  // 0xFF bytes -> 0x00
      const std::uint64_t ff = (ones - 0x0101010101010101ULL) & v & 0x8080808080808080ULL;
      if ((ff >> (BIT_BUF_SIZE - 8 * k)) == 0) {
        get_buffer = k == 8 ? v : (get_buffer << (8 * k)) | (v >> (BIT_BUF_SIZE - 8 * k));
        bits_left += 8 * k;
        st_->next_input_byte += k;
        st_->bytes_in_buffer -= static_cast<std::size_t>(k);
        return;
      }
    }
    State bs = fill(st_, State{get_buffer, bits_left}, nbits, insuff);
    get_buffer = bs.get_buffer;
    bits_left = bs.bits_left;
  }
  VIO_INLINE int huff_decode_slow(const DerivedTable& htbl, int min_bits, bool& insuff) {
    Decoded d = huff_decode_slow(st_, State{get_buffer, bits_left}, htbl, min_bits, insuff);
    get_buffer = d.state.get_buffer;
    bits_left = d.state.bits_left;
    return d.value;
  }

  VIO_INLINE void check(int nbits, bool& insuff) {
    if (bits_left < nbits) fill(nbits, insuff);
  }
  VIO_INLINE int get_bits(int nbits) {
    return static_cast<int>(get_buffer >> (bits_left -= nbits)) & ((1 << nbits) - 1);
  }
  VIO_INLINE int peek_bits(int nbits) const {
    return static_cast<int>(get_buffer >> (bits_left - nbits)) & ((1 << nbits) - 1);
  }
  VIO_INLINE void drop_bits(int nbits) { bits_left -= nbits; }

  // HUFF_DECODE
  VIO_INLINE int decode(const DerivedTable& htbl, bool& insuff) {
    int nb;
    if (bits_left < HUFF_LOOKAHEAD) {
      fill(0, insuff);
      if (bits_left < HUFF_LOOKAHEAD) return huff_decode_slow(htbl, 1, insuff);
    }
    int look = peek_bits(HUFF_LOOKAHEAD);
    if ((nb = (htbl.lookup[look] >> HUFF_LOOKAHEAD)) <= HUFF_LOOKAHEAD) {
      drop_bits(nb);
      return htbl.lookup[look] & ((1 << HUFF_LOOKAHEAD) - 1);
    }
    return huff_decode_slow(htbl, nb, insuff);
  }

  buf_t get_buffer = 0;
  int bits_left = 0;

 private:
  DState* st_;
};

// Combined lookup over the next B bits of the stream.  An entry whose code
// and magnitude bits both fit in the window holds the decoded value: bits
// 16-31 the value, bits 0-7 the total length, and bits 8-15 the zig-zag
// increment run + 1 (AC) or bit 15 set (DC).  Other entries hold the
// symbol in bits 8-15 and the code length in bits 0-7, or 0 for codes
// longer than the window.  max_s limits the magnitude sizes decoded in the
// table (refinement scans have one correction-sign bit, s = 1).  With
// code_len, entries with a value also hold the code length: in bits 4-7 (the
// total length then in bits 0-3) for AC, in bits 8-11 for DC.
inline constexpr int DC_FAST_BITS = 9;
inline constexpr int AC_FAST_BITS = 11;
inline constexpr int LL_FAST_BITS = 12;  // lossless differences: longer codes and values
template <int B>
using FastTable = std::array<std::int32_t, 1 << B>;

template <int B>
inline void make_fast_table(const DerivedTable& d, bool isDC, FastTable<B>& t, int max_s = 16,
                            bool code_len = false) {
  t.fill(0);
  const HuffTable& h = *d.pub;
  int p = 0;
  unsigned code = 0;
  for (int l = 1; l <= B; l++, code <<= 1) {
    for (int i = 0; i < static_cast<int>(h.bits[l]); i++, p++, code++) {
      const int sym = h.huffval[p];
      const int s = isDC ? sym : sym & 15;
      const int base = static_cast<int>(code) << (B - l);
      for (int j = 0; j < 1 << (B - l); j++) {
        std::int32_t e;
        if (l + s <= B && (isDC || s) && s <= max_s) {
          int v = s ? huff_extend((j >> (B - l - s)) & ((1 << s) - 1), s) : 0;
          e = static_cast<std::int32_t>(static_cast<unsigned>(v) << 16) |
              (isDC ? 0x8000 : (((sym >> 4) + 1) << 8)) | (l + s);
          if (code_len) e |= isDC ? l << 8 : l << 4;
        } else {
          e = (sym << 8) | l;
        }
        t[static_cast<std::size_t>(base + j)] = e;
      }
    }
  }
}

// AC lookup decoding up to two symbols: when the first symbol of the window
// has a value and is followed (within the window) by a second one with a
// value, or by end-of-block, the entry holds both.  Entries with a value:
// bits 0-3 the total length, 4-8 the first zig-zag increment (run + 1),
// 9-13 the second one (0: none), bit 14 end-of-block after them, bits 16-23
// the first value and 24-31 the second (the first again when there is no
// second, so that storing it is harmless).  Values beyond 8 bits are left
// to the symbol entries (symbol in bits 8-15, code length in bits 0-7),
// taken from t0, a table of symbol entries only (make_fast_table, max_s 0);
// t1 is the table of single values.
inline void make_pair_table(const FastTable<AC_FAST_BITS>& t0, const FastTable<AC_FAST_BITS>& t1,
                            FastTable<AC_FAST_BITS>& t2) {
  constexpr int B = AC_FAST_BITS;
  auto value_entry = [](std::int32_t e) { return static_cast<std::uint32_t>(e) >= 0x10000; };
  for (int w = 0; w < 1 << B; w++) {
    const std::int32_t e1 = t1[static_cast<std::size_t>(w)];
    const int v1 = e1 >> 16;
    if (!value_entry(e1) || v1 < -128 || v1 > 127) {
      t2[static_cast<std::size_t>(w)] = t0[static_cast<std::size_t>(w)];
      continue;
    }
    const int len1 = e1 & 0xFF, inc1 = e1 >> 8 & 0xFF;
    int len = len1, inc2 = 0, v2 = v1, eob = 0;
    const int rem = B - len1;
    if (rem > 0) {
      // The entry for the remaining bits (zero-padded): valid when it uses
      // no more than rem bits.
      const std::int32_t e2 = t1[static_cast<std::size_t>((w << len1) & ((1 << B) - 1))];
      const int len2 = e2 & 0xFF;
      if (len2 > 0 && len2 <= rem) {
        if (value_entry(e2)) {
          if (e2 >> 16 >= -128 && e2 >> 16 <= 127) {
            len += len2;
            inc2 = e2 >> 8 & 0xFF;
            v2 = e2 >> 16;
          }
        } else if ((e2 >> 8 & 0xFF) == 0) {  // end-of-block
          len += len2;
          eob = 1;
        }
      }
    }
    t2[static_cast<std::size_t>(w)] = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(v2) << 24 | (static_cast<std::uint32_t>(v1) & 0xFF) << 16 |
        static_cast<std::uint32_t>(eob << 14 | inc2 << 9 | inc1 << 4 | len));
  }
}

inline const std::uint8_t* find_ff(const std::uint8_t* p, const std::uint8_t* end) {
  const void* q = std::memchr(p, 0xFF, static_cast<std::size_t>(end - p));
  return q ? static_cast<const std::uint8_t*>(q) : end;
}

// The next marker at or after p: a 0xFF byte not followed by a stuffed zero
// (or the buffer's last byte).
inline const std::uint8_t* find_marker(const std::uint8_t* p, const std::uint8_t* end) {
  for (;;) {
    p = find_ff(p, end);
    if (end - p < 2 || p[1] != 0) return p;
    p += 2;
  }
}

// Left-aligned bit buffer over input known to hold no marker within reach,
// so that refilling never stops early.  Bits below the cnt valid ones are
// kept zero; each refill leaves at least 56 valid bits.
//
// The bytes read ahead differ from those of the byte-wise BitReader, which
// reads only when a request needs more bits than it has, and then up to at
// least MIN_GET_BITS.  Where the input stops being decoded (corrupt data),
// that position is visible: the bytes skipped to the next marker are
// reported.  So `lazy` follows the bit count the BitReader would have,
// request by request as the slow paths make them, and sync() moves the
// input position to match before the state is handed back.
struct MarkerFreeBits {
  std::uint64_t buf;
  int cnt;
  int lazy;
  const std::uint8_t* p;
  const std::uint8_t* ff;  // next 0xFF byte at or after p (a stuffed 0xFF00)
  const std::uint8_t* end;

  // A request for n bits, then n bits used.
  VIO_INLINE void need(int n) { lazy = lazy < n ? fill_level(lazy) : lazy; }
  VIO_INLINE void used(int n) { lazy -= n; }
  VIO_INLINE void bits(int n) {  // n >= 1 bits requested and used
    need(n);
    used(n);
  }
  // n (<= 63) bits requested and used one at a time.
  VIO_INLINE void single_bits(int n) { lazy = lazy - n + (64 & -static_cast<int>(lazy < n)); }
  // A Huffman code of length l, decoded with an 8-bit lookahead and then (for
  // longer codes) 9 bits and one more at a time.
  VIO_INLINE void code(int l) {
    if (VIO_LIKELY(l <= 16)) {
      code_bits(l, l);
    } else {
      branch_barrier();
      need(HUFF_LOOKAHEAD);
      bits(HUFF_LOOKAHEAD + 1);
      single_bits(l - HUFF_LOOKAHEAD - 1);
    }
  }
  // Gives back the whole bytes read beyond those the BitReader would have
  // read, or reads the ones it would have read on top.  (Called on copies,
  // see synced(): the decoding loops' state must not escape to it.)
  void sync() {
    if (cnt > lazy) {
      do {
        const unsigned c = static_cast<unsigned>(buf >> (64 - cnt)) & 0xFF;
        p -= c == 0xFF ? 2 : 1;
        cnt -= 8;
      } while (cnt > lazy);
      buf = cnt ? buf & ~(~std::uint64_t(0) >> cnt) : 0;
      ff = find_ff(p, end);
    }
    while (cnt < lazy) {
      const unsigned c = *p;
      p += c == 0xFF ? 2 : 1;
      buf |= static_cast<std::uint64_t>(c) << (56 - cnt);
      cnt += 8;
    }
    if (p > ff) ff = find_ff(p, end);
  }

  VIO_INLINE void refill() {
    const int n = (63 - cnt) >> 3;  // whole bytes that fit
    if (VIO_LIKELY(p + n <= ff)) {
      std::uint64_t w;
      std::memcpy(&w, p, 8);
      const int c = cnt | 56;  // cnt + 8 * n
      buf = (buf | ::vio::jpeg::simd::HuffBitWriter::to_big_endian(w) >> cnt) &
            ~(~std::uint64_t(0) >> c);
      p += n;
      cnt = c;
      return;
    }
    branch_barrier();  // rare: a stuffed 0xFF00 within reach
    while (cnt < 56) {
      const unsigned c = *p;
      p += c == 0xFF ? 2 : 1;
      buf |= static_cast<std::uint64_t>(c) << (56 - cnt);
      cnt += 8;
    }
    if (p > ff) ff = find_ff(p, end);
  }
  // Table index.  The barrier stops the compiler from merging the shift
  // with the index scaling into two dependent instructions.
  VIO_INLINE std::size_t peek(int bits) const {
    std::size_t i = static_cast<std::size_t>(buf >> (64 - bits));
#if defined(__GNUC__) || defined(__clang__)
    __asm__("" : "+r"(i));
#endif
    return i;
  }
  VIO_INLINE void consume(int n) {
    buf <<= n;
    cnt -= n;
  }
  VIO_INLINE int get(int n) {  // 1 <= n <= cnt
    int v = static_cast<int>(buf >> (64 - n));
    consume(n);
    return v;
  }
  // Symbol of a fast-table entry without a decoded value.
  VIO_INLINE int symbol(std::int32_t e, const DerivedTable& d, int bits, DState& st) {
    if (VIO_LIKELY(e & 0xFF)) {
      consume(e & 0xFF);
      code(e & 0xFF);
      return (e >> 8) & 0xFF;
    }
    const unsigned c = long_code(buf, d, bits);  // the bit state stays in registers
    consume(static_cast<int>(c >> 8));
    code(static_cast<int>(c >> 8));
    if (VIO_UNLIKELY(c >> 8 > 16)) bad_code(st);
    return static_cast<int>(c & 0xFF);
  }
  // A code of length l (<= 16) and value bits making t bits in all, from a
  // table entry.  At most one fill: at the code (fewer than h bits: 8 for
  // the lookahead, 9 for a longer code, whose bits past 9 come one at a time
  // and so fill only from 0 bits) or past it (fewer than t), adding the
  // whole bytes up to 57 bits or more.  Without branches (masks): the
  // fills come at irregular intervals.
  VIO_INLINE void code_bits(int l, int t) {
    const int h = HUFF_LOOKAHEAD + (l > HUFF_LOOKAHEAD);
    const int x = lazy - (l & -static_cast<int>(lazy >= h));  // bits at the fill
    const int added = ((BIT_BUF - x) >> 3) << 3;
    const int most = t ^ ((t ^ HUFF_LOOKAHEAD) & -static_cast<int>(t < HUFF_LOOKAHEAD));
    const int fill = -static_cast<int>(lazy < most);  // max(8, t): no fill from there
    lazy = lazy - t + (added & fill);
  }
  static constexpr int BIT_BUF = BitReader::BIT_BUF_SIZE;
  // The bit count after a fill from x bits.
  static VIO_INLINE int fill_level(int x) { return ((x - 1) & 7) + BitReader::MIN_GET_BITS; }
  // A code longer than the table's window: its length (bits 8+) and symbol.
  static VIO_NOINLINE unsigned long_code(std::uint64_t buf, const DerivedTable& d, int bits) {
    int l = bits + 1;
    JLong code = static_cast<JLong>(buf >> (64 - l));
    while (code > d.maxcode[l]) {
      l++;
      code = static_cast<JLong>(buf >> (64 - l));
    }
    const unsigned sym = l > 16 ? 0 : d.huffval[static_cast<int>(code + d.valoffset[l]) & 0xFF];
    return static_cast<unsigned>(l) << 8 | sym;
  }
  static VIO_NOINLINE void bad_code(DState& st) { st.warn("Corrupt JPEG data: bad Huffman code"); }
};

VIO_NOINLINE inline MarkerFreeBits synced(MarkerFreeBits b) {
  b.sync();
  return b;
}

// ===========================================================================
// Sequential Huffman decoder
class HuffDecoder final : public EntropyDecoder {
 public:
  explicit HuffDecoder(DState& st) : st_(st), br_(st) {
    // Motion-JPEG frames often omit default tables.
    std_huff_tables(st.dc_huff_tbl_ptrs, st.ac_huff_tbl_ptrs, true);
  }

  void start_pass() override {
    if (st_.Ss != 0 || st_.Se != DCTSIZE2 - 1 || st_.Ah != 0 || st_.Al != 0)
      st_.warn("Invalid SOS parameters for sequential JPEG");
    for (int ci = 0; ci < st_.comps_in_scan; ci++) {
      ComponentInfo* c = st_.cur_comp_info[ci];
      make_d_derived_tbl(st_, true, c->dc_tbl_no, dc_derived_[c->dc_tbl_no]);
      make_d_derived_tbl(st_, false, c->ac_tbl_no, ac_derived_[c->ac_tbl_no]);
      make_fast_table<DC_FAST_BITS>(dc_derived_[c->dc_tbl_no], true, dc_fast_[c->dc_tbl_no]);
      make_fast_table<AC_FAST_BITS>(ac_derived_[c->ac_tbl_no], false, ac_fast_[c->ac_tbl_no]);
      make_fast_table<AC_FAST_BITS>(ac_derived_[c->ac_tbl_no], false, ac_sym_[c->ac_tbl_no], 0);
      make_pair_table(ac_sym_[c->ac_tbl_no], ac_fast_[c->ac_tbl_no], ac_pair_[c->ac_tbl_no]);
      last_dc_val_[ci] = 0;
    }
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      int ci = st_.MCU_membership[blkn];
      ComponentInfo* c = st_.cur_comp_info[ci];
      dc_cur_[blkn] = &dc_derived_[c->dc_tbl_no];
      ac_cur_[blkn] = &ac_derived_[c->ac_tbl_no];
      dc_fast_cur_[blkn] = &dc_fast_[c->dc_tbl_no];
      ac_pair_cur_[blkn] = &ac_pair_[c->ac_tbl_no];
      ac_sym_cur_[blkn] = &ac_sym_[c->ac_tbl_no];
      if (c->component_needed) {
        dc_needed_[blkn] = true;
        ac_needed_[blkn] = c->DCT_scaled_size > 1;
      } else {
        dc_needed_[blkn] = ac_needed_[blkn] = false;
      }
    }
    br_.reset();
    ff_end_ = nullptr;
    insufficient_data = false;
    restarts_to_go_ = st_.restart_interval;
  }

  bool decode_mcu(Block** MCU_data) override {
    // The fast path is also used within restart
    // intervals (except for each interval's final MCU, which ends at the RST
    // marker).  It stops, leaving all state untouched, at any marker, and the
    // bytes left in the bit buffer are accounted for by process_restart()
    // either way, so the results are identical.
    bool usefast = true;
    if (st_.restart_interval) {
      if (restarts_to_go_ == 0) process_restart();
      usefast = restarts_to_go_ > 1;
    }
    if (st_.bytes_in_buffer < static_cast<std::size_t>(DCTSIZE2 * 8) * st_.blocks_in_MCU ||
        st_.unread_marker != 0)
      usefast = false;
    if (!insufficient_data) {
      if (!usefast || !decode_mcu_fast(MCU_data)) decode_mcu_slow(MCU_data);
    }
    if (st_.restart_interval) restarts_to_go_--;
    return true;
  }

  bool process_restart() override {
    st_.discarded_bytes += static_cast<unsigned>(br_.bits_left / 8);
    br_.bits_left = 0;
    st_.read_restart_marker();
    for (int ci = 0; ci < st_.comps_in_scan; ci++) last_dc_val_[ci] = 0;
    restarts_to_go_ = st_.restart_interval;
    if (st_.unread_marker == 0) insufficient_data = false;
    return true;
  }

 private:
  void decode_mcu_slow(Block** MCU_data) {
    bool& insuff = insufficient_data;
    BitReader br = br_;  // register-resident working copy
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      Coef* block = MCU_data ? MCU_data[blkn]->data() : nullptr;
      const DerivedTable& dctbl = *dc_cur_[blkn];
      const DerivedTable& actbl = *ac_cur_[blkn];
      int s = br.decode(dctbl, insuff);
      if (s) {
        br.check(s, insuff);
        int r = br.get_bits(s);
        s = huff_extend(r, s);
      }
      if (dc_needed_[blkn]) {
        int ci = st_.MCU_membership[blkn];
        s = static_cast<int>(static_cast<unsigned>(s) + static_cast<unsigned>(last_dc_val_[ci]));
        last_dc_val_[ci] = s;
        if (block) block[0] = static_cast<Coef>(s);
      }
      if (ac_needed_[blkn] && block) {
        for (int k = 1; k < DCTSIZE2; k++) {
          s = br.decode(actbl, insuff);
          int r = s >> 4;
          s &= 15;
          if (s) {
            k += r;
            br.check(s, insuff);
            r = br.get_bits(s);
            s = huff_extend(r, s);
            block[natural_order[k]] = static_cast<Coef>(s);
          } else {
            if (r != 15) break;
            k += 15;
          }
        }
      } else {
        for (int k = 1; k < DCTSIZE2; k++) {
          s = br.decode(actbl, insuff);
          int r = s >> 4;
          s &= 15;
          if (s) {
            k += r;
            br.check(s, insuff);
            br.drop_bits(s);
          } else {
            if (r != 15) break;
            k += 15;
          }
        }
      }
    }
    br_ = br;
  }

  // Fast path: operates directly on the input buffer; bails out (returning
  // false, with all state untouched) if a marker is encountered.  The bit
  // buffer is kept left-aligned and refilled 8 bytes at a time until a 0xFF
  // byte is reached; ff_ caches the position of the next 0xFF byte.
  VIO_NO_STACK_CHECK bool decode_mcu_fast(Block** MCU_data) {
    if (br_.bits_left > 63) return false;
    std::uint64_t buf = br_.bits_left ? br_.get_buffer << (64 - br_.bits_left) : 0;
    int cnt = br_.bits_left;
    const std::uint8_t* p = st_.next_input_byte;
    const std::uint8_t* const end = p + st_.bytes_in_buffer;
    if (end != ff_end_ || ff_ < p) {
      ff_end_ = end;
      ff_ = find_ff(p, end);
    }
    const std::uint8_t* ff = ff_;
    int last_dc[MAX_COMPS_IN_SCAN];
    for (int i = 0; i < st_.comps_in_scan; i++) last_dc[i] = last_dc_val_[i];
    int marker = 0;

    // Bits below the cnt valid ones are kept zero.
    auto refill = [&]() VIO_LAMBDA_INLINE {
      const int n = (63 - cnt) >> 3;  // whole bytes that fit
      if (VIO_LIKELY(p + n <= ff)) {
        std::uint64_t w;
        std::memcpy(&w, p, 8);
        const int c = cnt | 56;  // cnt + 8 * n
        buf = (buf | ::vio::jpeg::simd::HuffBitWriter::to_big_endian(w) >> cnt) &
              ~(~std::uint64_t(0) >> c);
        p += n;
        cnt = c;
        return;
      }
      branch_barrier();  // rare: a 0xFF byte (stuffed 0xFF00 or a marker)
      while (cnt < 56) {  // at most 63 bits
        if (marker) {
          cnt = 63;  // zero bits from here on
          break;
        }
        unsigned c = *p;
        if (c == 0xFF) {
          if (p[1] != 0) {
            marker = p[1];
            continue;
          }
          p += 2;
        } else {
          p++;
        }
        buf |= static_cast<std::uint64_t>(c) << (56 - cnt);
        cnt += 8;
      }
      if (p > ff) ff = find_ff(p, end);
    };
    // Table index.  The barrier stops the compiler from merging the shift
    // with the index scaling into two dependent instructions.
    auto peek = [&](int bits) VIO_LAMBDA_INLINE {
      std::size_t i = static_cast<std::size_t>(buf >> (64 - bits));
#if defined(__GNUC__) || defined(__clang__)
      __asm__("" : "+r"(i));
#endif
      return i;
    };
    auto consume = [&](int n) VIO_LAMBDA_INLINE {
      buf <<= n;
      cnt -= n;
    };
    auto get_value = [&](int s) VIO_LAMBDA_INLINE {  // s >= 1
      int v = huff_extend(static_cast<int>(buf >> (64 - s)), s);
      consume(s);
      return v;
    };
    // Symbol of an entry without a decoded value.
    auto symbol = [&](std::int32_t e, const DerivedTable& d, int bits) VIO_LAMBDA_INLINE {
      if (VIO_LIKELY(e & 0xFF)) {
        consume(e & 0xFF);
        return (e >> 8) & 0xFF;
      }
      branch_barrier();
      int l = bits + 1;
      JLong code = static_cast<JLong>(buf >> (64 - l));
      while (code > d.maxcode[l]) {
        l++;
        code = static_cast<JLong>(buf >> (64 - l));
      }
      consume(l);
      return l > 16 ? 0 : static_cast<int>(d.huffval[static_cast<int>(code + d.valoffset[l]) & 0xFF]);
    };
    // Decodes the AC coefficients of one block; Store = false only skips them.
    auto decode_ac = [&]<bool Store>(Coef* block, const DerivedTable& d,
                                     const FastTable<AC_FAST_BITS>& t,
                                     const FastTable<AC_FAST_BITS>& t0) VIO_LAMBDA_INLINE {
      int k = 0;
      do {
        std::int32_t e = t[peek(AC_FAST_BITS)];
        refill();
        if (VIO_LIKELY(static_cast<std::uint32_t>(e) >= 0x10000)) {
          // One or two values (see make_pair_table).
          k += (e >> 4) & 31;
          if constexpr (Store) block[natural_order[k]] = static_cast<Coef>(static_cast<std::int8_t>(e >> 16));
          if (VIO_UNLIKELY(k >= DCTSIZE2 - 1)) {
            // The block ends with the first value: only its bits are used.
            branch_barrier();
            const std::int32_t e0 = t0[peek(AC_FAST_BITS)];
            consume((e0 & 0xFF) + ((e0 >> 8) & 15));
            break;
          }
          consume(e & 15);
          k += (e >> 9) & 31;
          if constexpr (Store) block[natural_order[k]] = static_cast<Coef>(e >> 24);
          k |= (e >> 8) & 64;  // end-of-block
        } else {
          int sym = symbol(e, d, AC_FAST_BITS);
          int r = sym >> 4, s = sym & 15;
          if (s) {
            k += r + 1;
            int v = get_value(s);
            if constexpr (Store) block[natural_order[k]] = static_cast<Coef>(v);
          } else if (r == 15) {
            k += 16;
          } else {
            break;
          }
        }
      } while (k < DCTSIZE2 - 1);
    };

    // Each symbol takes at most 32 bits and each refill leaves at least 56,
    // so the window bits are always available: the table is looked up before
    // the refill, which keeps the refill off the critical path.
    refill();
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      Coef* block = MCU_data ? MCU_data[blkn]->data() : nullptr;
      std::int32_t e = (*dc_fast_cur_[blkn])[peek(DC_FAST_BITS)];
      refill();
      int s;
      if (VIO_LIKELY(e & 0x8000)) {
        consume(e & 0xFF);
        s = e >> 16;
      } else {
        s = symbol(e, *dc_cur_[blkn], DC_FAST_BITS);
        if (s) s = get_value(s);
      }
      if (dc_needed_[blkn]) {
        int ci = st_.MCU_membership[blkn];
        s = static_cast<int>(static_cast<unsigned>(s) + static_cast<unsigned>(last_dc[ci]));
        last_dc[ci] = s;
        if (block) block[0] = static_cast<Coef>(s);
      }
      if (ac_needed_[blkn] && block)
        decode_ac.template operator()<true>(block, *ac_cur_[blkn], *ac_pair_cur_[blkn], *ac_sym_cur_[blkn]);
      else
        decode_ac.template operator()<false>(block, *ac_cur_[blkn], *ac_pair_cur_[blkn], *ac_sym_cur_[blkn]);
    }
    if (marker != 0) {
      // Discard partial results; the caller re-decodes via the slow path.
      // (Coefficients already written are overwritten identically.)
      ff_end_ = nullptr;
      return false;
    }
    ff_ = ff;
    st_.bytes_in_buffer -= static_cast<std::size_t>(p - st_.next_input_byte);
    st_.next_input_byte = p;
    br_.get_buffer = cnt ? buf >> (64 - cnt) : 0;
    br_.bits_left = cnt;
    for (int i = 0; i < st_.comps_in_scan; i++) last_dc_val_[i] = last_dc[i];
    return true;
  }

  DState& st_;
  BitReader br_;
  int last_dc_val_[MAX_COMPS_IN_SCAN]{};
  unsigned restarts_to_go_ = 0;
  DerivedTable dc_derived_[NUM_HUFF_TBLS];
  DerivedTable ac_derived_[NUM_HUFF_TBLS];
  const DerivedTable* dc_cur_[D_MAX_BLOCKS_IN_MCU]{};
  const DerivedTable* ac_cur_[D_MAX_BLOCKS_IN_MCU]{};
  bool dc_needed_[D_MAX_BLOCKS_IN_MCU]{};
  bool ac_needed_[D_MAX_BLOCKS_IN_MCU]{};
  FastTable<DC_FAST_BITS> dc_fast_[NUM_HUFF_TBLS];
  FastTable<AC_FAST_BITS> ac_fast_[NUM_HUFF_TBLS];  // single values (to build ac_pair_)
  FastTable<AC_FAST_BITS> ac_sym_[NUM_HUFF_TBLS];   // symbols only
  FastTable<AC_FAST_BITS> ac_pair_[NUM_HUFF_TBLS];  // up to two values
  const FastTable<DC_FAST_BITS>* dc_fast_cur_[D_MAX_BLOCKS_IN_MCU]{};
  const FastTable<AC_FAST_BITS>* ac_pair_cur_[D_MAX_BLOCKS_IN_MCU]{};
  const FastTable<AC_FAST_BITS>* ac_sym_cur_[D_MAX_BLOCKS_IN_MCU]{};
  const std::uint8_t* ff_ = nullptr;      // next 0xFF byte at or after the input position
  const std::uint8_t* ff_end_ = nullptr;  // end of the buffer ff_ was found in
};

// Progression status table setup shared by progressive decoders.
inline void init_coef_bits(DState& st) {
  st.coef_bits.assign(static_cast<std::size_t>(st.num_components) * 2, {});
  for (int ci = 0; ci < st.num_components; ci++) st.coef_bits[ci].fill(-1);
  st.coef_bits_valid = true;
}

inline void update_progression(DState& st, bool warn_ac_without_dc) {
  bool is_DC_band = st.Ss == 0;
  for (int ci = 0; ci < st.comps_in_scan; ci++) {
    int cindex = st.cur_comp_info[ci]->component_index;
    int* coef_bit_ptr = st.coef_bits[cindex].data();
    int* prev_coef_bit_ptr = st.coef_bits[cindex + st.num_components].data();
    if (warn_ac_without_dc && !is_DC_band && coef_bit_ptr[0] < 0)
      st.warn(strformat(
          "Inconsistent progression sequence for component %d coefficient %d", cindex, 0));
    for (int coefi = std::min(st.Ss, 1); coefi <= std::max(st.Se, 9); coefi++) {
      if (st.input_scan_number > 1) prev_coef_bit_ptr[coefi] = coef_bit_ptr[coefi];
      else prev_coef_bit_ptr[coefi] = 0;
    }
    for (int coefi = st.Ss; coefi <= st.Se; coefi++) {
      int expected = coef_bit_ptr[coefi] < 0 ? 0 : coef_bit_ptr[coefi];
      if (st.Ah != expected)
        st.warn(strformat(
            "Inconsistent progression sequence for component %d coefficient %d", cindex,
            coefi));
      coef_bit_ptr[coefi] = st.Al;
    }
  }
}

// For nibble i of a natural-order coefficient mask, the corresponding
// zig-zag-order bits.
struct ZigzagNibbleTable {
  std::uint64_t v[16][16];
  constexpr ZigzagNibbleTable() : v() {
    for (int i = 0; i < 16; i++)
      for (int x = 0; x < 16; x++)
        for (int b = 0; b < 4; b++)
          if (x >> b & 1) v[i][x] |= std::uint64_t(1) << zigzag_order[4 * i + b];
  }
};
alignas(64) inline constexpr ZigzagNibbleTable kZigzagNibble{};

// ===========================================================================
// Progressive Huffman decoder
class PhuffDecoder final : public EntropyDecoder {
 public:
  explicit PhuffDecoder(DState& st) : st_(st), br_(st) {
    init_coef_bits(st);
#if VIO_HUFF_SSE2
    vector_mask_ = st.simd.has(::vio::jpeg::simd::kSSE2);
#elif VIO_HUFF_NEON
    vector_mask_ = st.simd.has(::vio::jpeg::simd::kNEON);
#endif
  }

  void start_pass() override {
    bool is_DC_band = st_.Ss == 0;
    bool bad = false;
    if (is_DC_band) {
      if (st_.Se != 0) bad = true;
    } else {
      if (st_.Ss > st_.Se || st_.Se >= DCTSIZE2) bad = true;
      if (st_.comps_in_scan != 1) bad = true;
    }
    if (st_.Ah != 0 && st_.Al != st_.Ah - 1) bad = true;
    if (st_.Al > 13) bad = true;
    if (bad)
      VIO_FAIL("Invalid progressive/lossless parameters Ss=%d Se=%d Ah=%d Al=%d", st_.Ss, st_.Se,
               st_.Ah, st_.Al);
    update_progression(st_, true);

    if (st_.Ah == 0) mode_ = is_DC_band ? DC_FIRST : AC_FIRST;
    else mode_ = is_DC_band ? DC_REFINE : AC_REFINE;

    for (int ci = 0; ci < st_.comps_in_scan; ci++) {
      ComponentInfo* c = st_.cur_comp_info[ci];
      if (is_DC_band) {
        if (st_.Ah == 0) make_d_derived_tbl(st_, true, c->dc_tbl_no, derived_[c->dc_tbl_no]);
      } else {
        make_d_derived_tbl(st_, false, c->ac_tbl_no, derived_[c->ac_tbl_no]);
        ac_tbl_ = &derived_[c->ac_tbl_no];
        make_fast_table<AC_FAST_BITS>(*ac_tbl_, false, ac_fast_, st_.Ah ? 1 : 16, true);
      }
      last_dc_val_[ci] = 0;
    }
    br_.reset();
    insufficient_data = false;
    EOBRUN_ = 0;
    restarts_to_go_ = st_.restart_interval;
    mk_end_ = nullptr;
  }

  bool decode_mcu(Block** MCU_data) override {
    if (st_.restart_interval && restarts_to_go_ == 0) process_restart();
    switch (mode_) {
      case DC_FIRST: dc_first(MCU_data); break;
      case AC_FIRST: ac_first(MCU_data); break;
      case DC_REFINE: dc_refine(MCU_data); break;
      case AC_REFINE: ac_refine(MCU_data); break;
    }
    if (st_.restart_interval) restarts_to_go_--;
    return true;
  }

  bool decode_blocks(Block* blocks, unsigned n) override {
    if (mode_ == AC_FIRST) decode_ac_blocks<false>(blocks, n);
    else if (mode_ == AC_REFINE) decode_ac_blocks<true>(blocks, n);
    else return false;
    return true;
  }

  bool process_restart() override {
    st_.discarded_bytes += static_cast<unsigned>(br_.bits_left / 8);
    br_.bits_left = 0;
    st_.read_restart_marker();
    for (int ci = 0; ci < st_.comps_in_scan; ci++) last_dc_val_[ci] = 0;
    EOBRUN_ = 0;
    restarts_to_go_ = st_.restart_interval;
    if (st_.unread_marker == 0) insufficient_data = false;
    return true;
  }

 private:
  enum Mode { DC_FIRST, AC_FIRST, DC_REFINE, AC_REFINE };

  static int extend(int x, int s) {
    return x < (1 << (s - 1)) ? x + static_cast<int>((~0u << s) + 1) : x;
  }

  void dc_first(Block** MCU_data) {
    if (insufficient_data) return;
    bool& insuff = insufficient_data;
    BitReader br = br_;  // register-resident working copy
    int Al = st_.Al;
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      Coef* block = MCU_data[blkn]->data();
      int ci = st_.MCU_membership[blkn];
      ComponentInfo* c = st_.cur_comp_info[ci];
      const DerivedTable& tbl = derived_[c->dc_tbl_no];
      int s = br.decode(tbl, insuff);
      if (s) {
        br.check(s, insuff);
        int r = br.get_bits(s);
        s = extend(r, s);
      }
      if ((last_dc_val_[ci] >= 0 && s > INT_MAX - last_dc_val_[ci]) ||
          (last_dc_val_[ci] < 0 && s < INT_MIN - last_dc_val_[ci]))
        VIO_FAIL("DCT coefficient (lossy) or spatial difference (lossless) out of range");
      s += last_dc_val_[ci];
      last_dc_val_[ci] = s;
      block[0] = static_cast<Coef>(lshift(s, Al));
    }
    br_ = br;
  }

  void ac_first(Block** MCU_data) {
    if (insufficient_data) return;
    bool& insuff = insufficient_data;
    BitReader br = br_;  // register-resident working copy
    int Se = st_.Se, Al = st_.Al;
    unsigned EOBRUN = EOBRUN_;
    if (EOBRUN > 0) {
      EOBRUN--;
    } else {
      Coef* block = MCU_data[0]->data();
      const DerivedTable& tbl = *ac_tbl_;
      for (int k = st_.Ss; k <= Se; k++) {
        int s = br.decode(tbl, insuff);
        int r = s >> 4;
        s &= 15;
        if (s) {
          k += r;
          br.check(s, insuff);
          r = br.get_bits(s);
          s = extend(r, s);
          block[natural_order[k]] = static_cast<Coef>(lshift(s, Al));
        } else {
          if (r == 15) {
            k += 15;
          } else {
            EOBRUN = 1u << r;
            if (r) {
              br.check(r, insuff);
              r = br.get_bits(r);
              EOBRUN += static_cast<unsigned>(r);
            }
            EOBRUN--;
            break;
          }
        }
      }
    }
    EOBRUN_ = EOBRUN;
    br_ = br;
  }

  // The fast paths decode blocks from a left-aligned bit buffer while no
  // marker lies within FAST_MARGIN bytes, more than any block can take
  // (with every byte stuffed), so that they never run out of data.
  static constexpr std::ptrdiff_t FAST_MARGIN = 1024;

  VIO_INLINE bool fast_ok() {
    if (insufficient_data || st_.unread_marker != 0 || br_.bits_left > 63) return false;
    const std::uint8_t* p = st_.next_input_byte;
    const std::uint8_t* end = p + st_.bytes_in_buffer;
    if (end != mk_end_ || p > mk_) {
      mk_end_ = end;
      mk_ = find_marker(p, end);
      ff_ = find_ff(p, end);
    }
    return mk_ - p >= FAST_MARGIN;
  }

  VIO_INLINE MarkerFreeBits bits_in() {
    MarkerFreeBits b;
    b.cnt = br_.bits_left;
    b.lazy = b.cnt;
    b.buf = b.cnt ? br_.get_buffer << (64 - b.cnt) : 0;
    b.p = st_.next_input_byte;
    b.end = mk_end_;
    if (ff_ < b.p) ff_ = find_ff(b.p, b.end);
    b.ff = ff_;
    return b;
  }

  // (By value: the caller's copy, out of reach of the out-of-line sync(),
  // stays in registers.)
  VIO_INLINE void bits_out(MarkerFreeBits b) {
    b.sync();
    br_.get_buffer = b.cnt ? b.buf >> (64 - b.cnt) : 0;
    br_.bits_left = b.cnt;
    st_.bytes_in_buffer -= static_cast<std::size_t>(b.p - st_.next_input_byte);
    st_.next_input_byte = b.p;
    ff_ = b.ff;
  }

  // Runs of blocks: the fast path keeps the bit state in registers from
  // block to block while no marker is within reach.
  template <bool Refine>
  VIO_NO_STACK_CHECK void decode_ac_blocks(Block* blocks, unsigned n) {
    for (unsigned i = 0; i < n;) {
      if (st_.restart_interval && restarts_to_go_ == 0) process_restart();
      if (!fast_ok()) {
        Block* mcu[1] = {&blocks[i++]};
        if constexpr (Refine) ac_refine(mcu);
        else ac_first(mcu);
        if (st_.restart_interval) restarts_to_go_--;
        continue;
      }
      unsigned m = n - i;
      if (st_.restart_interval && restarts_to_go_ < m) m = restarts_to_go_;
      MarkerFreeBits b = bits_in();
      unsigned EOBRUN = EOBRUN_;
      const unsigned first = i;
      do {
        Coef* block = blocks[i++].data();
        if constexpr (Refine) {
          ac_refine_block(b, block, EOBRUN);
        } else {
          if (EOBRUN > 0) EOBRUN--;
          else ac_first_block(b, block, EOBRUN);
        }
      } while (i - first < m && mk_ - b.p >= FAST_MARGIN);
      EOBRUN_ = EOBRUN;
      bits_out(b);
      if (st_.restart_interval) restarts_to_go_ -= i - first;
    }
  }

  VIO_INLINE void ac_first_block(MarkerFreeBits& b, Coef* block, unsigned& EOBRUN) {
    const FastTable<AC_FAST_BITS>& t = ac_fast_;
    const int Se = st_.Se, Al = st_.Al;
    int k = st_.Ss - 1;  // the last position decoded
    b.refill();
    do {
      std::int32_t e = t[b.peek(AC_FAST_BITS)];
      b.refill();
      if (VIO_LIKELY(static_cast<std::uint32_t>(e) >= 0x10000)) {
        b.consume(e & 15);
        b.code_bits((e >> 4) & 15, e & 15);
        k += (e >> 8) & 0xFF;
        block[natural_order[k]] = static_cast<Coef>(lshift(e >> 16, Al));
      } else {
        int sym = b.symbol(e, *ac_tbl_, AC_FAST_BITS, st_);
        int r = sym >> 4, s = sym & 15;
        if (s) {
          k += r + 1;
          b.bits(s);
          block[natural_order[k]] = static_cast<Coef>(lshift(huff_extend(b.get(s), s), Al));
        } else if (r == 15) {
          k += 16;
        } else {
          EOBRUN = 1u << r;
          if (r) {
            b.bits(r);
            EOBRUN += static_cast<unsigned>(b.get(r));
          }
          EOBRUN--;
          break;
        }
      }
    } while (k < Se);
  }

  // The next n bits (0 <= n <= 63), refilling as needed.
  static VIO_INLINE std::uint64_t take(MarkerFreeBits& b, int n) {
    if (VIO_UNLIKELY(n > b.cnt)) {
      branch_barrier();
      b.refill();
      if (n > b.cnt) {  // more than a refill holds: take the bits in two parts
        const std::uint64_t hi = b.buf >> (96 - n);
        b.consume(n - 32);
        b.refill();
        const std::uint64_t lo = b.buf >> 32;
        b.consume(32);
        return hi << 32 | lo;
      }
    }
    const std::uint64_t v = b.buf >> 1 >> (63 - n);
    b.consume(n);
    return v;
  }

  // Non-zero coefficients of a block by zig-zag position.
  template <int... K>
  static VIO_INLINE std::uint64_t nonzero_mask(const Coef* block, std::integer_sequence<int, K...>) {
    return ((static_cast<std::uint64_t>(block[natural_order[K]] != 0) << K) | ...);
  }
  static VIO_INLINE std::uint64_t nonzero_mask(const Coef* block) {
    return nonzero_mask(block, std::make_integer_sequence<int, DCTSIZE2>());
  }
#if VIO_HUFF_SSE2
  // The natural-order mask from 16-bit compares, then its bits moved to
  // zig-zag positions a nibble at a time.
  static VIO_INLINE std::uint64_t nonzero_mask_vector(const Coef* block) {
    const __m128i zero = _mm_setzero_si128();
    std::uint64_t nat = 0;
    for (int i = 0; i < 4; i++) {
      const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 16 * i));
      const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(block + 16 * i + 8));
      const __m128i z = _mm_packs_epi16(_mm_cmpeq_epi16(a, zero), _mm_cmpeq_epi16(b, zero));
      nat |= static_cast<std::uint64_t>(_mm_movemask_epi8(z) ^ 0xFFFF) << (16 * i);
    }
    std::uint64_t m = 0;
    for (int i = 0; i < 16; i++) m |= kZigzagNibble.v[i][(nat >> (4 * i)) & 15];
    return m;
  }
#elif VIO_HUFF_NEON
  // Non-zero flags as bytes, put in zig-zag order by table lookups, then
  // gathered into bits.
  static VIO_INLINE std::uint64_t nonzero_mask_vector(const Coef* block) {
    uint8x16x4_t nz;
    for (int i = 0; i < 4; i++) {
      const int16x8_t a = vld1q_s16(block + 16 * i), b = vld1q_s16(block + 16 * i + 8);
      nz.val[i] = vcombine_u8(vmovn_u16(vtstq_s16(a, a)), vmovn_u16(vtstq_s16(b, b)));
    }
    alignas(16) static constexpr std::uint8_t weights[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                                                             1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t w = vld1q_u8(opaque(weights));
    uint8x16_t t[4];
    for (int i = 0; i < 4; i++) {
      alignas(16) static constexpr std::uint8_t order[64] = {
          0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
          12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
          35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
          58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
      t[i] = vandq_u8(vqtbl4q_u8(nz, vld1q_u8(opaque(order) + 16 * i)), w);
    }
    uint8x16_t sum = vpaddq_u8(vpaddq_u8(t[0], t[1]), vpaddq_u8(t[2], t[3]));
    sum = vpaddq_u8(sum, sum);
    return vgetq_lane_u64(vreinterpretq_u64_u8(sum), 0);
  }
#endif

  // Every non-zero coefficient of the band gets one correction bit, in
  // zig-zag order, interleaved in the stream with the symbols that place
  // new coefficients (at positions that were zero).  The bits are gathered
  // in acc as they come and applied at the end of the block.
  VIO_INLINE void ac_refine_block(MarkerFreeBits& b, Coef* block, unsigned& EOBRUN) {
    const FastTable<AC_FAST_BITS>& t = ac_fast_;
    const int Se = st_.Se, Al = st_.Al;
    const int p1 = 1 << Al;
    const int m1 = static_cast<int>(~0u << Al);
    int k = st_.Ss;
    const std::uint64_t band = (~std::uint64_t(0) << k) & (~std::uint64_t(0) >> (63 - Se));
#if VIO_HUFF_SSE2 || VIO_HUFF_NEON
    const std::uint64_t nz =
        (vector_mask_ ? nonzero_mask_vector(block) : nonzero_mask(block)) & band;
#else
    const std::uint64_t nz = nonzero_mask(block) & band;
#endif
    std::uint64_t acc = 0;  // correction bits read so far
    int nacc = 0;           // their number
    int late = 0;           // a coefficient placed past the band (corrupt data)
    if (EOBRUN == 0) {
      std::uint64_t zeros = ~nz & band;  // zero coefficients from k on
      b.refill();
      for (;;) {
        std::int32_t e = t[b.peek(AC_FAST_BITS)];
        b.refill();
        int r, v = 0;
        if (VIO_LIKELY(static_cast<std::uint32_t>(e) >= 0x10000)) {
          b.consume(e & 15);
          b.code_bits((e >> 4) & 15, e & 15);
          r = ((e >> 8) & 0xFF) - 1;
          v = lshift(e >> 16, Al);
        } else {
          int sym = b.symbol(e, *ac_tbl_, AC_FAST_BITS, st_);
          r = sym >> 4;
          if (int s = sym & 15) {
            if (s != 1) MarkerFreeBits::bad_code(st_);
            b.bits(1);
            v = b.get(1) ? p1 : m1;
          } else if (r != 15) {
            EOBRUN = 1u << r;
            if (r) {
              b.bits(r);
              EOBRUN += static_cast<unsigned>(b.get(r));
            }
            break;
          }
        }
        // The new coefficient (or the end of a run of 16 zeros) goes to the
        // (r + 1)th zero coefficient from k; the r zeros before it are
        // passed, and so are the non-zero ones in between, each taking a
        // correction bit.  Small runs (the most common) without branches.
        const std::uint64_t z1 = zeros & (zeros - 1), z2 = z1 & (z1 - 1), z3 = z2 & (z2 - 1);
        std::uint64_t z = r == 0 ? zeros : r == 1 ? z1 : r == 2 ? z2 : z3;
        if (VIO_UNLIKELY(r > 3))
          for (int i = r - 3; i > 0; i--) z &= z - 1;
        int target, n;
        if (VIO_LIKELY(z != 0)) {
          target = std::countr_zero(z);
          n = target - k - r;
          zeros = z & (z - 1);
        } else {
          branch_barrier();
          target = Se + 1;
          n = popcount64(nz & (~std::uint64_t(0) << k));
          late = v;
          v = 0;
        }
        acc = acc << n | take(b, n);
        nacc += n;
        b.single_bits(n);
        if (v) block[natural_order[target]] = static_cast<Coef>(v);
        k = target + 1;
        if (k > Se) break;
        // The next lookup precedes its refill; correction bits may have
        // used up more than a symbol's worth.
        if (VIO_UNLIKELY(b.cnt < AC_FAST_BITS)) b.refill();
      }
    }
    if (EOBRUN > 0) {
      const int n = popcount64(nz & (~std::uint64_t(0) << k));
      acc = acc << n | take(b, n);
      nacc += n;
      b.single_bits(n);
      EOBRUN--;
    }
    // One bit per non-zero coefficient (nacc = popcount(nz) < 64), the
    // first one's highest.  From the lowest position up, so the only
    // dependency between iterations is clearing a bit of m (finding the
    // highest takes a slow lzcnt on x86 each time).
    std::uint64_t bits = acc << 1 << (63 - nacc);
    for (std::uint64_t m = nz; m; m &= m - 1) {
      refine_coef(block + natural_order[std::countr_zero(m)], static_cast<int>(bits >> 63), p1);
      bits <<= 1;
    }
    if (late) block[natural_order[Se + 1]] = static_cast<Coef>(late);
  }

  void dc_refine(Block** MCU_data) {
    bool& insuff = insufficient_data;
    BitReader br = br_;  // register-resident working copy
    int p1 = 1 << st_.Al;
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      Coef* block = MCU_data[blkn]->data();
      br.check(1, insuff);
      if (br.get_bits(1)) block[0] = static_cast<Coef>(block[0] | p1);
    }
    br_ = br;
  }

  // Applies a correction bit to a non-zero coefficient: if the bit is set and
  // the coefficient's bit Al is not, its magnitude grows by p1 = 1 << Al.
  // Branch-free, since the coefficient's sign is unpredictable.
  static VIO_INLINE void refine_coef(Coef* thiscoef, int bit, int p1) {
    int c = *thiscoef;
    int sign = c >> (CHAR_BIT * sizeof(int) - 1);
    int add = (p1 ^ sign) - sign;  // p1, or m1 = -p1 for a negative coefficient
    int apply = bit & static_cast<int>((c & p1) == 0);
    *thiscoef = static_cast<Coef>(c + (add & -apply));
  }

  void ac_refine(Block** MCU_data) {
    if (insufficient_data) return;
    bool& insuff = insufficient_data;
    BitReader br = br_;  // register-resident working copy
    int Se = st_.Se;
    int p1 = 1 << st_.Al;
    int m1 = static_cast<int>(~0u << st_.Al);
    unsigned EOBRUN = EOBRUN_;
    Coef* block = MCU_data[0]->data();
    const DerivedTable& tbl = *ac_tbl_;
    int k = st_.Ss;
    if (EOBRUN == 0) {
      for (; k <= Se; k++) {
        int s = br.decode(tbl, insuff);
        int r = s >> 4;
        s &= 15;
        if (s) {
          if (s != 1) st_.warn("Corrupt JPEG data: bad Huffman code");
          br.check(1, insuff);
          s = br.get_bits(1) ? p1 : m1;
        } else {
          if (r != 15) {
            EOBRUN = 1u << r;
            if (r) {
              br.check(r, insuff);
              r = br.get_bits(r);
              EOBRUN += static_cast<unsigned>(r);
            }
            break;
          }
        }
        do {
          Coef* thiscoef = block + natural_order[k];
          if (*thiscoef != 0) {
            br.check(1, insuff);
            refine_coef(thiscoef, br.get_bits(1), p1);
          } else {
            if (--r < 0) break;
          }
          k++;
        } while (k <= Se);
        if (s) block[natural_order[k]] = static_cast<Coef>(s);
      }
    }
    if (EOBRUN > 0) {
      for (; k <= Se; k++) {
        Coef* thiscoef = block + natural_order[k];
        if (*thiscoef != 0) {
          br.check(1, insuff);
          refine_coef(thiscoef, br.get_bits(1), p1);
        }
      }
      EOBRUN--;
    }
    EOBRUN_ = EOBRUN;
    br_ = br;
  }

  DState& st_;
  BitReader br_;
  Mode mode_ = DC_FIRST;
  unsigned EOBRUN_ = 0;
  int last_dc_val_[MAX_COMPS_IN_SCAN]{};
  unsigned restarts_to_go_ = 0;
  DerivedTable derived_[NUM_HUFF_TBLS];
  const DerivedTable* ac_tbl_ = nullptr;
  FastTable<AC_FAST_BITS> ac_fast_;
  bool vector_mask_ = false;  // nonzero_mask_vector() allowed by the SIMD level in use
  const std::uint8_t* ff_ = nullptr;      // next 0xFF byte at or after the input position
  const std::uint8_t* mk_ = nullptr;      // next marker at or after the input position
  const std::uint8_t* mk_end_ = nullptr;  // end of the buffer ff_ and mk_ were found in
};

// ===========================================================================
// Lossless Huffman decoder.  Difference rows are int (JDIFF).
class LhuffDecoder final : public EntropyDecoder {
 public:
  explicit LhuffDecoder(DState& st) : st_(st), br_(st) {}

  void start_pass() override {
    for (int ci = 0; ci < st_.comps_in_scan; ci++) {
      ComponentInfo* c = st_.cur_comp_info[ci];
      int dctbl = c->dc_tbl_no;
      if (dctbl < 0 || dctbl >= NUM_HUFF_TBLS || !st_.dc_huff_tbl_ptrs[dctbl])
        VIO_FAIL("Huffman table 0x%02x was not defined", dctbl);
      make_d_derived_tbl(st_, true, dctbl, derived_[dctbl]);
      make_fast_table<LL_FAST_BITS>(derived_[dctbl], true, fast_[dctbl], 16, true);
    }
    int ptrn = 0;
    for (int sampn = 0; sampn < st_.blocks_in_MCU;) {
      ComponentInfo* c = st_.cur_comp_info[st_.MCU_membership[sampn]];
      int ci = c->component_index;
      for (int yoffset = 0; yoffset < c->MCU_height; yoffset++, ptrn++) {
        info_[ptrn] = {ci, yoffset, c->MCU_width};
        for (int xoffset = 0; xoffset < c->MCU_width; xoffset++, sampn++) {
          ptr_index_[sampn] = ptrn;
          cur_tbls_[sampn] = &derived_[c->dc_tbl_no];
          cur_fast_[sampn] = &fast_[c->dc_tbl_no];
        }
      }
    }
    num_output_ptrs_ = ptrn;
    br_.reset();
    insufficient_data = false;
    mk_end_ = nullptr;
  }

  bool decode_mcu(Block**) override { return true; }

  unsigned decode_mcus(int** const* diff_buf, unsigned MCU_row_num, unsigned MCU_col_num,
                       unsigned nMCU) override {
    int* output_ptr[D_MAX_BLOCKS_IN_MCU];
    for (int ptrn = 0; ptrn < num_output_ptrs_; ptrn++) {
      const auto& inf = info_[ptrn];
      output_ptr[ptrn] = diff_buf[inf.ci][MCU_row_num + inf.yoffset] + MCU_col_num * inf.MCU_width;
    }
    if (insufficient_data) {
      for (int ptrn = 0; ptrn < num_output_ptrs_; ptrn++)
        std::memset(output_ptr[ptrn], 0, nMCU * info_[ptrn].MCU_width * sizeof(int));
      if (st_.idct_start_pass) st_.idct_start_pass();
    } else {
      bool& insuff = insufficient_data;
      // Local copies: the int stores below could otherwise alias the bit
      // reader and tables, forcing them to be reloaded per sample.
      BitReader br = br_;
      const int blocks = st_.blocks_in_MCU;
      const DerivedTable* tbls[D_MAX_BLOCKS_IN_MCU];
      int pidx[D_MAX_BLOCKS_IN_MCU];
      for (int sampn = 0; sampn < blocks; sampn++) {
        tbls[sampn] = cur_tbls_[sampn];
        pidx[sampn] = ptr_index_[sampn];
      }
      unsigned mcu_num = 0;
      if (st_.unread_marker == 0 && br.bits_left <= 63) {
        // Fast path while no marker is within reach: each sample takes at
        // most 31 bits, and the bit buffer holds up to 63 more.
        const std::uint8_t* p = st_.next_input_byte;
        const std::uint8_t* end = p + st_.bytes_in_buffer;
        if (end != mk_end_ || p > mk_) {
          mk_end_ = end;
          mk_ = find_marker(p, end);
          ff_ = find_ff(p, end);
        }
        const std::ptrdiff_t margin = 8 * blocks + 32;
        if (mk_ - p >= margin) {
          const FastTable<LL_FAST_BITS>* fast[D_MAX_BLOCKS_IN_MCU];
          for (int sampn = 0; sampn < blocks; sampn++) fast[sampn] = cur_fast_[sampn];
          MarkerFreeBits b;
          b.cnt = br.bits_left;
          b.lazy = b.cnt;
          b.buf = b.cnt ? br.get_buffer << (64 - b.cnt) : 0;
          b.p = p;
          b.end = end;
          if (ff_ < p) ff_ = find_ff(p, end);
          b.ff = ff_;
          b.refill();
          do {
            for (int sampn = 0; sampn < blocks; sampn++) {
              std::int32_t e = (*fast[sampn])[b.peek(LL_FAST_BITS)];
              b.refill();
              int s;
              if (VIO_LIKELY(e & 0x8000)) {
                b.consume(e & 0xFF);
                b.code_bits((e >> 8) & 15, e & 0xFF);
                s = e >> 16;
              } else {
                s = b.symbol(e, *tbls[sampn], LL_FAST_BITS, st_);
                if (s && s != 16) b.bits(s);
                if (s) s = s == 16 ? 32768 : huff_extend(b.get(s), s);
              }
              *output_ptr[pidx[sampn]]++ = s;
            }
          } while (++mcu_num < nMCU && mk_ - b.p >= margin);
          const MarkerFreeBits t = synced(b);
          br.get_buffer = t.cnt ? t.buf >> (64 - t.cnt) : 0;
          br.bits_left = t.cnt;
          st_.bytes_in_buffer -= static_cast<std::size_t>(t.p - st_.next_input_byte);
          st_.next_input_byte = t.p;
          ff_ = t.ff;
        }
      }
      for (; mcu_num < nMCU; mcu_num++) {
        for (int sampn = 0; sampn < blocks; sampn++) {
          int s = br.decode(*tbls[sampn], insuff);
          if (s) {
            if (s == 16) {
              s = 32768;
            } else {
              br.check(s, insuff);
              int r = br.get_bits(s);
              s = huff_extend(r, s);
            }
          }
          *output_ptr[pidx[sampn]]++ = s;
        }
      }
      br_ = br;
    }
    return nMCU;
  }

  bool process_restart() override {
    st_.discarded_bytes += static_cast<unsigned>(br_.bits_left / 8);
    br_.bits_left = 0;
    st_.read_restart_marker();
    if (st_.unread_marker == 0) insufficient_data = false;
    return true;
  }

 private:
  struct PtrInfo {
    int ci, yoffset, MCU_width;
  };
  DState& st_;
  BitReader br_;
  DerivedTable derived_[NUM_HUFF_TBLS];
  const DerivedTable* cur_tbls_[D_MAX_BLOCKS_IN_MCU]{};
  FastTable<LL_FAST_BITS> fast_[NUM_HUFF_TBLS];
  const FastTable<LL_FAST_BITS>* cur_fast_[D_MAX_BLOCKS_IN_MCU]{};
  const std::uint8_t* ff_ = nullptr;      // next 0xFF byte at or after the input position
  const std::uint8_t* mk_ = nullptr;      // next marker at or after the input position
  const std::uint8_t* mk_end_ = nullptr;  // end of the buffer ff_ and mk_ were found in
  PtrInfo info_[D_MAX_BLOCKS_IN_MCU]{};
  int ptr_index_[D_MAX_BLOCKS_IN_MCU]{};
  int num_output_ptrs_ = 0;
};

}  // namespace vio::jpeg::detail
