// vio::jpeg - Huffman entropy decoding (sequential, progressive, lossless).
#pragma once

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
      make_fast_table<DC_BITS>(dc_derived_[c->dc_tbl_no], true, dc_fast_[c->dc_tbl_no]);
      make_fast_table<AC_BITS>(ac_derived_[c->ac_tbl_no], false, ac_fast_[c->ac_tbl_no]);
      last_dc_val_[ci] = 0;
    }
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      int ci = st_.MCU_membership[blkn];
      ComponentInfo* c = st_.cur_comp_info[ci];
      dc_cur_[blkn] = &dc_derived_[c->dc_tbl_no];
      ac_cur_[blkn] = &ac_derived_[c->ac_tbl_no];
      dc_fast_cur_[blkn] = &dc_fast_[c->dc_tbl_no];
      ac_fast_cur_[blkn] = &ac_fast_[c->ac_tbl_no];
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

  // Combined lookup over the next B bits of the stream.  An entry whose code
  // and magnitude bits both fit in the window holds the decoded value: bits
  // 16-31 the value, bits 0-7 the total length, and bits 8-15 the zig-zag
  // increment run + 1 (AC) or bit 15 set (DC).  Other entries hold the
  // symbol in bits 8-15 and the code length in bits 0-7, or 0 for codes
  // longer than the window.
  static constexpr int DC_BITS = 9;
  static constexpr int AC_BITS = 11;
  template <int B>
  using FastTable = std::array<std::int32_t, 1 << B>;

  template <int B>
  static void make_fast_table(const DerivedTable& d, bool isDC, FastTable<B>& t) {
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
          if (l + s <= B && (isDC || s)) {
            int v = s ? huff_extend((j >> (B - l - s)) & ((1 << s) - 1), s) : 0;
            e = static_cast<std::int32_t>(static_cast<unsigned>(v) << 16) |
                (isDC ? 0x8000 : (((sym >> 4) + 1) << 8)) | (l + s);
          } else {
            e = (sym << 8) | l;
          }
          t[static_cast<std::size_t>(base + j)] = e;
        }
      }
    }
  }

  // The bit reader of decode_mcu_fast().  The bit buffer is kept
  // left-aligned, with the bits below the cnt valid ones zero, and refilled 8
  // bytes at a time until a 0xFF byte is reached.  Each operation is an
  // object holding references to decode_mcu_fast()'s locals, as a lambda
  // capturing by reference would (MSVC force-inlines lambdas only from C++20
  // on); in this shape GCC keeps the reader state in registers.
  struct Refill {
    std::uint64_t& buf;
    int& cnt;
    const std::uint8_t*& p;
    const std::uint8_t*& ff;  // next 0xFF byte at or after p, or end
    const std::uint8_t* const& end;
    int& marker;
    VIO_INLINE void operator()() const {
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
    }
  };
  // Table index.  The barrier stops the compiler from merging the shift with
  // the index scaling into two dependent instructions.
  struct Peek {
    const std::uint64_t& buf;
    VIO_INLINE std::size_t operator()(int bits) const {
      std::size_t i = static_cast<std::size_t>(buf >> (64 - bits));
#if defined(__GNUC__) || defined(__clang__)
      __asm__("" : "+r"(i));
#endif
      return i;
    }
  };
  struct Consume {
    std::uint64_t& buf;
    int& cnt;
    VIO_INLINE void operator()(int n) const {
      buf <<= n;
      cnt -= n;
    }
  };
  struct GetValue {
    const std::uint64_t& buf;
    const Consume& consume;
    VIO_INLINE int operator()(int s) const {  // s >= 1
      int v = huff_extend(static_cast<int>(buf >> (64 - s)), s);
      consume(s);
      return v;
    }
  };
  // Symbol of an entry without a decoded value.
  struct Symbol {
    const Consume& consume;
    const std::uint64_t& buf;
    VIO_INLINE int operator()(std::int32_t e, const DerivedTable& d, int bits) const {
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
    }
  };
  // Decodes the AC coefficients of one block; Store = false only skips them.
  struct DecodeAc {
    const Peek& peek;
    const Refill& refill;
    const Consume& consume;
    const Symbol& symbol;
    const GetValue& get_value;
    template <bool Store>
    VIO_INLINE void run(Coef* block, const DerivedTable& d, const FastTable<AC_BITS>& t) const {
      int k = 0;
      do {
        std::int32_t e = t[peek(AC_BITS)];
        refill();
        if (VIO_LIKELY(static_cast<std::uint32_t>(e) >= 0x10000)) {
          consume(e & 0xFF);
          k += (e >> 8) & 0xFF;
          if constexpr (Store) block[natural_order[k]] = static_cast<Coef>(e >> 16);
        } else {
          int sym = symbol(e, d, AC_BITS);
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
    }
  };

  // Fast path: operates directly on the input buffer; bails out (returning
  // false, with all state untouched) if a marker is encountered.  ff_ caches
  // the position of the next 0xFF byte.
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

    const Refill refill{buf, cnt, p, ff, end, marker};
    const Peek peek{buf};
    const Consume consume{buf, cnt};
    const GetValue get_value{buf, consume};
    const Symbol symbol{consume, buf};
    const DecodeAc decode_ac{peek, refill, consume, symbol, get_value};

    // Each symbol takes at most 32 bits and each refill leaves at least 56,
    // so the window bits are always available: the table is looked up before
    // the refill, which keeps the refill off the critical path.
    refill();
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      Coef* block = MCU_data ? MCU_data[blkn]->data() : nullptr;
      std::int32_t e = (*dc_fast_cur_[blkn])[peek(DC_BITS)];
      refill();
      int s;
      if (VIO_LIKELY(e & 0x8000)) {
        consume(e & 0xFF);
        s = e >> 16;
      } else {
        s = symbol(e, *dc_cur_[blkn], DC_BITS);
        if (s) s = get_value(s);
      }
      if (dc_needed_[blkn]) {
        int ci = st_.MCU_membership[blkn];
        s = static_cast<int>(static_cast<unsigned>(s) + static_cast<unsigned>(last_dc[ci]));
        last_dc[ci] = s;
        if (block) block[0] = static_cast<Coef>(s);
      }
      if (ac_needed_[blkn] && block)
        decode_ac.run<true>(block, *ac_cur_[blkn], *ac_fast_cur_[blkn]);
      else
        decode_ac.run<false>(block, *ac_cur_[blkn], *ac_fast_cur_[blkn]);
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

  static const std::uint8_t* find_ff(const std::uint8_t* p, const std::uint8_t* end) {
    const void* q = std::memchr(p, 0xFF, static_cast<std::size_t>(end - p));
    return q ? static_cast<const std::uint8_t*>(q) : end;
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
  FastTable<DC_BITS> dc_fast_[NUM_HUFF_TBLS];
  FastTable<AC_BITS> ac_fast_[NUM_HUFF_TBLS];
  const FastTable<DC_BITS>* dc_fast_cur_[D_MAX_BLOCKS_IN_MCU]{};
  const FastTable<AC_BITS>* ac_fast_cur_[D_MAX_BLOCKS_IN_MCU]{};
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

// ===========================================================================
// Progressive Huffman decoder
class PhuffDecoder final : public EntropyDecoder {
 public:
  explicit PhuffDecoder(DState& st) : st_(st), br_(st) { init_coef_bits(st); }

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
      }
      last_dc_val_[ci] = 0;
    }
    br_.reset();
    insufficient_data = false;
    EOBRUN_ = 0;
    restarts_to_go_ = st_.restart_interval;
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
        }
      }
    }
    num_output_ptrs_ = ptrn;
    br_.reset();
    insufficient_data = false;
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
      for (unsigned mcu_num = 0; mcu_num < nMCU; mcu_num++) {
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
  PtrInfo info_[D_MAX_BLOCKS_IN_MCU]{};
  int ptr_index_[D_MAX_BLOCKS_IN_MCU]{};
  int num_output_ptrs_ = 0;
};

}  // namespace vio::jpeg::detail
