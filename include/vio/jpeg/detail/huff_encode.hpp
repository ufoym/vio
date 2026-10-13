// vio::jpeg - Huffman entropy encoding (sequential, progressive, lossless).
#pragma once

#include <climits>
#include <utility>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#include "compress_state.hpp"
#include "../simd/dispatch.hpp"

namespace vio::jpeg::detail {

class EntropyEncoder {
 public:
  virtual ~EntropyEncoder() = default;
  virtual void start_pass(bool gather_statistics) = 0;
  virtual void encode_mcu(Block** MCU_data) { (void)MCU_data; }
  virtual unsigned encode_mcus(int** const* /*diff_buf*/, unsigned /*row*/, unsigned /*col*/,
                               unsigned nMCU) {
    return nMCU;
  }
  virtual void finish_pass() = 0;
};

#if defined(_MSC_VER) && !defined(__clang__)
VIO_INLINE int nbits_nonzero(unsigned x) {
  unsigned long i;
  _BitScanReverse(&i, x);
  return static_cast<int>(i) + 1;
}
VIO_INLINE int ctz64(std::uint64_t x) {  // x != 0
#if defined(_M_X64)
  // tzcnt runs as bsf on CPUs without BMI1 (same result for x != 0), and
  // unlike bsf it does not depend on the previous value of its destination.
  return static_cast<int>(_tzcnt_u64(x));
#elif defined(_M_ARM64)
  unsigned long i;
  _BitScanForward64(&i, x);
  return static_cast<int>(i);
#else
  unsigned long i;
  if (_BitScanForward(&i, static_cast<std::uint32_t>(x))) return static_cast<int>(i);
  _BitScanForward(&i, static_cast<std::uint32_t>(x >> 32));
  return static_cast<int>(i) + 32;
#endif
}
#else
VIO_INLINE int nbits_nonzero(unsigned x) { return 32 - __builtin_clz(x); }
VIO_INLINE int ctz64(std::uint64_t x) { return __builtin_ctzll(x); }  // x != 0
#endif
VIO_INLINE int nbits(unsigned x) { return x ? nbits_nonzero(x) : 0; }

struct CDerivedTable {
  unsigned ehufco[256];
  char ehufsi[256];
  // Each symbol's code shifted past the n magnitude bits that follow it
  // (n = symbol for DC, its low 4 bits for AC), with size + n in bits 32-39.
  std::uint64_t emit[256];
};

inline void make_c_derived_tbl(CState& cs, bool isDC, int tblno, CDerivedTable& dtbl) {
  if (tblno < 0 || tblno >= NUM_HUFF_TBLS) VIO_FAIL("Huffman table 0x%02x was not defined", tblno);
  auto& slot = isDC ? cs.dc_huff_tbl_ptrs[tblno] : cs.ac_huff_tbl_ptrs[tblno];
  if (!slot) VIO_FAIL("Huffman table 0x%02x was not defined", tblno);
  const HuffTable& htbl = *slot;
  char huffsize[257];
  unsigned huffcode[257];
  int p = 0;
  for (int l = 1; l <= 16; l++) {
    int i = htbl.bits[l];
    if (p + i > 256) VIO_FAIL("Bogus Huffman table definition");
    while (i--) huffsize[p++] = static_cast<char>(l);
  }
  huffsize[p] = 0;
  int lastp = p;
  unsigned code = 0;
  int si = huffsize[0];
  p = 0;
  while (huffsize[p]) {
    while (huffsize[p] == si) {
      huffcode[p++] = code;
      code++;
    }
    if (static_cast<JLong>(code) >= (JLong(1) << si)) VIO_FAIL("Bogus Huffman table definition");
    code <<= 1;
    si++;
  }
  std::memset(dtbl.ehufco, 0, sizeof(dtbl.ehufco));
  std::memset(dtbl.ehufsi, 0, sizeof(dtbl.ehufsi));
  int maxsymbol = isDC ? (cs.lossless ? 16 : 15) : 255;
  for (p = 0; p < lastp; p++) {
    int i = htbl.huffval[p];
    if (i < 0 || i > maxsymbol || dtbl.ehufsi[i]) VIO_FAIL("Bogus Huffman table definition");
    dtbl.ehufco[i] = huffcode[p];
    dtbl.ehufsi[i] = huffsize[p];
  }
  for (int i = 0; i < 256; i++) {
    int n = isDC ? std::min(i, 16) : i & 15;
    dtbl.emit[i] = (static_cast<std::uint64_t>(dtbl.ehufsi[i] + n) << 32) |
                   (static_cast<std::uint64_t>(dtbl.ehufco[i]) << n);
  }
}

// jpeg_gen_optimal_table()
inline void gen_optimal_table(HuffTable& htbl, long freq[]) {
  constexpr int MAX_CLEN = 32;
  std::uint8_t bits[MAX_CLEN + 2] = {};
  int bit_pos[MAX_CLEN + 1];
  int codesize[257] = {};
  int nz_index[257];
  int others[257];
  for (int i = 0; i < 257; i++) others[i] = -1;
  freq[256] = 1;
  int num_nz_symbols = 0;
  for (int i = 0; i < 257; i++) {
    if (freq[i]) {
      nz_index[num_nz_symbols] = i;
      freq[num_nz_symbols] = freq[i];
      num_nz_symbols++;
    }
  }
  for (;;) {
    int c1 = -1, c2 = -1;
    long v = 1000000000L, v2 = 1000000000L;
    for (int i = 0; i < num_nz_symbols; i++) {
      if (freq[i] <= v2) {
        if (freq[i] <= v) {
          c2 = c1;
          v2 = v;
          v = freq[i];
          c1 = i;
        } else {
          v2 = freq[i];
          c2 = i;
        }
      }
    }
    if (c2 < 0) break;
    freq[c1] += freq[c2];
    freq[c2] = 1000000001L;
    codesize[c1]++;
    while (others[c1] >= 0) {
      c1 = others[c1];
      codesize[c1]++;
    }
    others[c1] = c2;
    codesize[c2]++;
    while (others[c2] >= 0) {
      c2 = others[c2];
      codesize[c2]++;
    }
  }
  for (int i = 0; i < num_nz_symbols; i++) {
    if (codesize[i] > MAX_CLEN) VIO_FAIL("Huffman code size table overflow");
    bits[codesize[i]]++;
  }
  int p = 0;
  for (int i = 1; i <= MAX_CLEN; i++) {
    bit_pos[i] = p;
    p += bits[i];
  }
  int i;
  for (i = MAX_CLEN; i > 16; i--) {
    while (bits[i] > 0) {
      int j = i - 2;
      while (bits[j] == 0) j--;
      bits[i] -= 2;
      bits[i - 1]++;
      bits[j + 1] += 2;
      bits[j]--;
    }
  }
  while (bits[i] == 0) i--;
  bits[i]--;
  std::memcpy(htbl.bits.data(), bits, 17);
  for (i = 0; i < num_nz_symbols - 1; i++) {
    htbl.huffval[bit_pos[codesize[i]]] = static_cast<std::uint8_t>(nz_index[i]);
    bit_pos[codesize[i]]++;
  }
  htbl.sent_table = false;
}

// ===========================================================================
// Sequential Huffman encoder
class HuffEncoder final : public EntropyEncoder {
 public:
  explicit HuffEncoder(CState& cs)
      : cs_(cs), simd_huff_(simd::huff_encode_kernel<CDerivedTable>(cs.simd)) {}

  void start_pass(bool gather) override {
    gather_ = gather;
    for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
      ComponentInfo* c = cs_.cur_comp_info[ci];
      int dctbl = c->dc_tbl_no, actbl = c->ac_tbl_no;
      if (gather) {
        if (dctbl < 0 || dctbl >= NUM_HUFF_TBLS) VIO_FAIL("Huffman table 0x%02x was not defined", dctbl);
        if (actbl < 0 || actbl >= NUM_HUFF_TBLS) VIO_FAIL("Huffman table 0x%02x was not defined", actbl);
        std::memset(dc_count_[dctbl], 0, sizeof(dc_count_[dctbl]));
        std::memset(ac_count_[actbl], 0, sizeof(ac_count_[actbl]));
      } else {
        make_c_derived_tbl(cs_, true, dctbl, dc_derived_[dctbl]);
        make_c_derived_tbl(cs_, false, actbl, ac_derived_[actbl]);
      }
      last_dc_val_[ci] = 0;
    }
    for (int blkn = 0; blkn < cs_.blocks_in_MCU; blkn++) {
      ComponentInfo* c = cs_.cur_comp_info[cs_.MCU_membership[blkn]];
      dc_cur_[blkn] = &dc_derived_[c->dc_tbl_no];
      ac_cur_[blkn] = &ac_derived_[c->ac_tbl_no];
    }
    bits_ = {};
    raw_next_ = raw_.get();
    restarts_to_go_ = cs_.restart_interval;
    next_restart_num_ = 0;
  }

  void encode_mcu(Block** MCU_data) override {
    if (gather_) {
      encode_mcu_gather(MCU_data);
      return;
    }
    if (cs_.restart_interval && restarts_to_go_ == 0) emit_restart(next_restart_num_);
    if (raw_.get() + RAW_SIZE - raw_next_ <
        static_cast<std::ptrdiff_t>(cs_.blocks_in_MCU) * (BUFSIZE + 16))
      flush_raw();
    if (simd_huff_) {
      // The SIMD path is used for any data precision (coefficients are
      // 16-bit).
      const std::int16_t* blocks[C_MAX_BLOCKS_IN_MCU];
      int last_dc[C_MAX_BLOCKS_IN_MCU];
      for (int blkn = 0; blkn < cs_.blocks_in_MCU; blkn++) {
        int ci = cs_.MCU_membership[blkn];
        blocks[blkn] = MCU_data[blkn]->data();
        last_dc[blkn] = last_dc_val_[ci];
        last_dc_val_[ci] = blocks[blkn][0];
      }
      raw_next_ = simd_huff_(bits_, raw_next_, blocks, last_dc, dc_cur_, ac_cur_,
                             cs_.blocks_in_MCU);
    } else {
      for (int blkn = 0; blkn < cs_.blocks_in_MCU; blkn++) {
        int ci = cs_.MCU_membership[blkn];
        encode_one_block(MCU_data[blkn]->data(), last_dc_val_[ci], *dc_cur_[blkn],
                         *ac_cur_[blkn]);
        last_dc_val_[ci] = (*MCU_data[blkn])[0];
      }
    }
    if (cs_.restart_interval) {
      if (restarts_to_go_ == 0) {
        restarts_to_go_ = cs_.restart_interval;
        next_restart_num_ = (next_restart_num_ + 1) & 7;
      }
      restarts_to_go_--;
    }
  }

  void finish_pass() override {
    if (gather_) finish_pass_gather();
    else flush_bits();
  }

 private:
  static constexpr int BUFSIZE = DCTSIZE2 * 8;
  // Raw (unstuffed) output collected before it is copied to the destination.
  static constexpr std::ptrdiff_t RAW_SIZE = 32768;

  // Pads the pending bits with ones to a byte boundary and writes out all
  // the data.
  void flush_bits() {
    if (bits_.count)
      *raw_next_++ = static_cast<std::uint8_t>((bits_.bits << (8 - bits_.count)) |
                                               (0xFFu >> bits_.count));
    bits_ = {};
    flush_raw();
  }

  // Copies the raw data to the destination, with a zero byte after each 0xFF.
  void flush_raw() {
    const std::uint8_t* p = raw_.get();
    const std::uint8_t* const end = raw_next_;
    cs_.dest.ensure(2 * static_cast<std::size_t>(end - p));
    std::uint8_t* out = cs_.dest.next;
    while (p < end) {
      const void* ff = std::memchr(p, 0xFF, static_cast<std::size_t>(end - p));
      const std::uint8_t* stop = ff ? static_cast<const std::uint8_t*>(ff) + 1 : end;
      std::memcpy(out, p, static_cast<std::size_t>(stop - p));
      out += stop - p;
      p = stop;
      if (ff) *out++ = 0;
    }
    store(out);
    raw_next_ = raw_.get();
  }

  VIO_INLINE void store(std::uint8_t* buffer) {
    cs_.dest.free_in_buffer -= static_cast<std::size_t>(buffer - cs_.dest.next);
    cs_.dest.next = buffer;
  }

  using BitWriter = vio::jpeg::simd::HuffBitWriter;

  // encode_one_block()'s operations are objects holding references to its
  // locals, as lambdas capturing by reference would (MSVC force-inlines
  // lambdas only from C++20 on); in this shape GCC keeps the block pointer
  // and the coder state in registers.

  // Writers bound to the bit writer.
  struct PutBits {
    BitWriter& w;
    VIO_INLINE void operator()(std::uint64_t code, int size) const { w.put_bits(code, size); }
  };
  struct PutCode {
    BitWriter& w;
    VIO_INLINE void operator()(int temp, int nb, unsigned code, int size) const {
      w.put_code(code, size, static_cast<unsigned>(temp) & ((1u << nb) - 1), nb);
    }
  };
  // Codes an AC coefficient after the zero run r (in units of 16), which a
  // zero coefficient only extends.
  struct AcCoder {
    int& r;
    const int& max_coef_bits;
    const PutBits& put_bits;
    const PutCode& put_code;
    const CDerivedTable& actbl;
    VIO_INLINE void operator()(int temp) const {
      if (temp == 0) {
        r += 16;
      } else {
        int nb = temp >> (CHAR_BIT * sizeof(int) - 1);
        temp += nb;
        nb ^= temp;
        nb = nbits_nonzero(static_cast<unsigned>(nb));
        if (nb > max_coef_bits)
          VIO_FAIL("DCT coefficient (lossy) or spatial difference (lossless) out of range");
        while (r >= 16 * 16) {
          r -= 16 * 16;
          put_bits(actbl.ehufco[0xf0], actbl.ehufsi[0xf0]);
        }
        r += nb;
        put_code(temp, nb, actbl.ehufco[r], actbl.ehufsi[r]);
        r = 0;
      }
    }
  };
  // Codes the coefficient at zig-zag position K + 1.
  struct AcAt {
    const AcCoder& ac;
    const Coef* const& block;
    template <class K>
    VIO_INLINE void operator()(K) const {
      ac(block[natural_order[K::value + 1]]);
    }
  };
  // Calls f(std::integral_constant<std::size_t, K>{}) for each K, unrolled.
  template <class F, std::size_t... K>
  static VIO_INLINE void for_each_index(const F& f, std::index_sequence<K...>) {
    (f(std::integral_constant<std::size_t, K>{}), ...);
  }

  void encode_one_block(const Coef* block, int last_dc_val, const CDerivedTable& dctbl,
                        const CDerivedTable& actbl) {
    BitWriter w(bits_, raw_next_);
    const int max_coef_bits = cs_.data_precision + 2;
    const PutBits put_bits{w};
    const PutCode put_code{w};

    int temp = block[0] - last_dc_val;
    int nb = temp >> (CHAR_BIT * sizeof(int) - 1);
    temp += nb;
    nb ^= temp;
    nb = nbits(static_cast<unsigned>(nb));
    if (nb > max_coef_bits + 1)
      VIO_FAIL("DCT coefficient (lossy) or spatial difference (lossless) out of range");
    put_code(temp, nb, dctbl.ehufco[nb], dctbl.ehufsi[nb]);

    int r = 0;
    const AcCoder ac{r, max_coef_bits, put_bits, put_code, actbl};
    // Fully unrolled: every zigzag position becomes a constant offset.
    for_each_index(AcAt{ac, block}, std::make_index_sequence<DCTSIZE2 - 1>{});
    if (r > 0) put_bits(actbl.ehufco[0], actbl.ehufsi[0]);

    bits_ = w.state();
    raw_next_ = w.buffer;
  }

  void emit_restart(int restart_num) {
    flush_bits();
    cs_.emit_byte(0xFF);
    cs_.emit_byte(M_RST0 + restart_num);
    for (int ci = 0; ci < cs_.comps_in_scan; ci++) last_dc_val_[ci] = 0;
  }

  void htest_one_block(const Coef* block, int last_dc_val, long dc_counts[], long ac_counts[]) {
    int max_coef_bits = cs_.data_precision + 2;
    int temp = block[0] - last_dc_val;
    if (temp < 0) temp = -temp;
    int nb = 0;
    while (temp) {
      nb++;
      temp >>= 1;
    }
    if (nb > max_coef_bits + 1)
      VIO_FAIL("DCT coefficient (lossy) or spatial difference (lossless) out of range");
    dc_counts[nb]++;
    int r = 0;
    for (int k = 1; k < DCTSIZE2; k++) {
      if ((temp = block[natural_order[k]]) == 0) {
        r++;
      } else {
        while (r > 15) {
          ac_counts[0xF0]++;
          r -= 16;
        }
        if (temp < 0) temp = -temp;
        nb = 1;
        while ((temp >>= 1)) nb++;
        if (nb > max_coef_bits)
          VIO_FAIL("DCT coefficient (lossy) or spatial difference (lossless) out of range");
        ac_counts[(r << 4) + nb]++;
        r = 0;
      }
    }
    if (r > 0) ac_counts[0]++;
  }

  void encode_mcu_gather(Block** MCU_data) {
    if (cs_.restart_interval) {
      if (restarts_to_go_ == 0) {
        for (int ci = 0; ci < cs_.comps_in_scan; ci++) last_dc_val_[ci] = 0;
        restarts_to_go_ = cs_.restart_interval;
      }
      restarts_to_go_--;
    }
    for (int blkn = 0; blkn < cs_.blocks_in_MCU; blkn++) {
      int ci = cs_.MCU_membership[blkn];
      ComponentInfo* c = cs_.cur_comp_info[ci];
      htest_one_block(MCU_data[blkn]->data(), last_dc_val_[ci], dc_count_[c->dc_tbl_no],
                      ac_count_[c->ac_tbl_no]);
      last_dc_val_[ci] = (*MCU_data[blkn])[0];
    }
  }

  void finish_pass_gather() {
    bool did_dc[NUM_HUFF_TBLS] = {}, did_ac[NUM_HUFF_TBLS] = {};
    for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
      ComponentInfo* c = cs_.cur_comp_info[ci];
      int dctbl = c->dc_tbl_no, actbl = c->ac_tbl_no;
      if (!did_dc[dctbl]) {
        auto& h = cs_.dc_huff_tbl_ptrs[dctbl];
        if (!h) h.emplace();
        gen_optimal_table(*h, dc_count_[dctbl]);
        did_dc[dctbl] = true;
      }
      if (!did_ac[actbl]) {
        auto& h = cs_.ac_huff_tbl_ptrs[actbl];
        if (!h) h.emplace();
        gen_optimal_table(*h, ac_count_[actbl]);
        did_ac[actbl] = true;
      }
    }
  }

  CState& cs_;
  bool gather_ = false;
  const CDerivedTable* dc_cur_[C_MAX_BLOCKS_IN_MCU]{};  // tables of each block of the MCU
  const CDerivedTable* ac_cur_[C_MAX_BLOCKS_IN_MCU]{};
  simd::HuffBitState bits_;
  std::unique_ptr<std::uint8_t[]> raw_{new std::uint8_t[RAW_SIZE]};
  std::uint8_t* raw_next_ = raw_.get();
  simd::HuffEncodeFn<CDerivedTable> simd_huff_ = nullptr;
  int last_dc_val_[MAX_COMPS_IN_SCAN]{};
  unsigned restarts_to_go_ = 0;
  int next_restart_num_ = 0;
  CDerivedTable dc_derived_[NUM_HUFF_TBLS];
  CDerivedTable ac_derived_[NUM_HUFF_TBLS];
  long dc_count_[NUM_HUFF_TBLS][257]{};
  long ac_count_[NUM_HUFF_TBLS][257]{};
};

// Simple byte-wise bit writer used by the progressive and lossless encoders.
struct SimpleBitWriter {
  std::size_t put_buffer = 0;
  int put_bits = 0;

  VIO_INLINE void emit_bits(ByteSink& dest, unsigned code, int size) {
    if (size == 0) VIO_FAIL("Missing Huffman code table entry");
    std::size_t pb = static_cast<std::size_t>(code);
    pb &= (static_cast<std::size_t>(1) << size) - 1;
    int pbits = put_bits + size;
    pb <<= 24 - pbits;
    pb |= put_buffer;
    while (pbits >= 8) {
      int c = static_cast<int>((pb >> 16) & 0xFF);
      dest.put(c);
      if (c == 0xFF) dest.put(0);
      pb <<= 8;
      pbits -= 8;
    }
    put_buffer = pb;
    put_bits = pbits;
  }
  void flush(ByteSink& dest) {
    emit_bits(dest, 0x7F, 7);
    put_buffer = 0;
    put_bits = 0;
  }
};

// Bit writer of the progressive encoder: bits collect in a 64-bit buffer and
// are written 32 at a time (0xFF bytes followed by a stuffed 0x00).
struct WordBitWriter {
  std::uint64_t acc = 0;  // the low n bits are pending (higher bits: stale)
  int n = 0;              // < 32 between calls

  // Appends the low `size` (<= 32) bits of `code`, which has no higher bits.
  VIO_INLINE void put(ByteSink& dest, std::uint32_t code, int size) {
    acc = (acc << size) | code;
    n += size;
    if (n >= 32) {
      n -= 32;
      write32(dest, static_cast<std::uint32_t>(acc >> n));
    }
  }
  void flush(ByteSink& dest) {
    put(dest, 0x7F, 7);  // pad the last byte with 1 bits
    while (n >= 8) {
      n -= 8;
      write_byte(dest, static_cast<std::uint8_t>(acc >> n));
    }
    acc = 0;
    n = 0;
  }

 private:
  static VIO_INLINE void write32(ByteSink& dest, std::uint32_t w) {
    dest.ensure(9);  // at most 8 bytes, and put() needs room for one more
    // Any 0xFF byte (or, rarely, a false alarm) takes the byte-wise path.
    if (VIO_UNLIKELY((~w - 0x01010101u) & w & 0x80808080u)) {
      for (int sh = 24; sh >= 0; sh -= 8) write_byte(dest, static_cast<std::uint8_t>(w >> sh));
      return;
    }
    std::uint8_t* p = dest.next;
    p[0] = static_cast<std::uint8_t>(w >> 24);
    p[1] = static_cast<std::uint8_t>(w >> 16);
    p[2] = static_cast<std::uint8_t>(w >> 8);
    p[3] = static_cast<std::uint8_t>(w);
    dest.next = p + 4;
    dest.free_in_buffer -= 4;
  }
  static VIO_INLINE void write_byte(ByteSink& dest, std::uint8_t b) {
    dest.put(b);
    if (b == 0xFF) dest.put(0);
  }
};

// ===========================================================================
// Progressive Huffman encoder
class PhuffEncoder final : public EntropyEncoder {
 public:
  explicit PhuffEncoder(CState& cs) : cs_(cs), simd_(simd::phuff_kernels(cs.simd)) {}

  void start_pass(bool gather) override {
    gather_ = gather;
    bool is_DC_band = cs_.Ss == 0;
    if (cs_.Ah == 0) mode_ = is_DC_band ? DC_FIRST : AC_FIRST;
    else mode_ = is_DC_band ? DC_REFINE : AC_REFINE;
    for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
      ComponentInfo* c = cs_.cur_comp_info[ci];
      last_dc_val_[ci] = 0;
      int tbl;
      if (is_DC_band) {
        if (cs_.Ah != 0) continue;
        tbl = c->dc_tbl_no;
      } else {
        ac_tbl_no_ = tbl = c->ac_tbl_no;
      }
      if (gather) {
        if (tbl < 0 || tbl >= NUM_HUFF_TBLS) VIO_FAIL("Huffman table 0x%02x was not defined", tbl);
        std::memset(count_[tbl], 0, sizeof(count_[tbl]));
      } else {
        make_c_derived_tbl(cs_, is_DC_band, tbl, derived_[tbl]);
      }
    }
    EOBRUN_ = 0;
    BE_ = 0;
    bw_ = {};
    restarts_to_go_ = cs_.restart_interval;
    next_restart_num_ = 0;
  }

  void encode_mcu(Block** MCU_data) override {
    if (cs_.restart_interval && restarts_to_go_ == 0) emit_restart(next_restart_num_);
    switch (mode_) {
      case DC_FIRST: dc_first(MCU_data); break;
      case AC_FIRST: ac_first(MCU_data); break;
      case DC_REFINE: dc_refine(MCU_data); break;
      case AC_REFINE: ac_refine(MCU_data); break;
    }
    if (cs_.restart_interval) {
      if (restarts_to_go_ == 0) {
        restarts_to_go_ = cs_.restart_interval;
        next_restart_num_ = (next_restart_num_ + 1) & 7;
      }
      restarts_to_go_--;
    }
  }

  void finish_pass() override {
    emit_eobrun();
    if (gather_) {
      bool is_DC_band = cs_.Ss == 0;
      bool did[NUM_HUFF_TBLS] = {};
      for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
        ComponentInfo* c = cs_.cur_comp_info[ci];
        int tbl;
        if (is_DC_band) {
          if (cs_.Ah != 0) continue;
          tbl = c->dc_tbl_no;
        } else {
          tbl = c->ac_tbl_no;
        }
        if (!did[tbl]) {
          auto& h = is_DC_band ? cs_.dc_huff_tbl_ptrs[tbl] : cs_.ac_huff_tbl_ptrs[tbl];
          if (!h) h.emplace();
          gen_optimal_table(*h, count_[tbl]);
          did[tbl] = true;
        }
      }
    } else {
      bw_.flush(cs_.dest);
    }
  }

 private:
  enum Mode { DC_FIRST, AC_FIRST, DC_REFINE, AC_REFINE };
  static constexpr unsigned MAX_CORR_BITS = 1000;

  // The low `size` (<= 16) bits of `code`.
  void emit_bits(unsigned code, int size) {
    if (size == 0) VIO_FAIL("Missing Huffman code table entry");
    if (gather_) return;
    bw_.put(cs_.dest, code & ((1u << size) - 1), size);
  }

  void emit_symbol(int tbl_no, int symbol) {
    if (gather_) count_[tbl_no][symbol]++;
    else emit_bits(derived_[tbl_no].ehufco[symbol], derived_[tbl_no].ehufsi[symbol]);
  }

  // emit_symbol() followed by emit_bits(bits, nb), 0 < nb <= 15, in one write.
  void emit_symbol_bits(int tbl_no, int symbol, unsigned bits, int nb) {
    if (gather_) {
      count_[tbl_no][symbol]++;
      return;
    }
    int size = derived_[tbl_no].ehufsi[symbol];
    if (size == 0) VIO_FAIL("Missing Huffman code table entry");
    bw_.put(cs_.dest, (derived_[tbl_no].ehufco[symbol] << nb) | (bits & ((1u << nb) - 1)),
            size + nb);
  }

  // The correction bits (one per char), up to 16 per write.  Out of line:
  // inlined into encode_mcu(), its loop slows down the coding loops there.
  VIO_NOINLINE void emit_buffered_bits(const char* bufstart, unsigned nb) {
    if (gather_) return;
    while (nb > 0) {
      int n = nb < 16 ? static_cast<int>(nb) : 16;
      unsigned w = 0;
      for (int i = 0; i < n; i++) w = (w << 1) | static_cast<unsigned>(bufstart[i] & 1);
      bw_.put(cs_.dest, w, n);
      bufstart += n;
      nb -= static_cast<unsigned>(n);
    }
  }

  void emit_eobrun() {
    if (EOBRUN_ > 0) {
      int temp = static_cast<int>(EOBRUN_);
      int nb = nbits_nonzero(static_cast<unsigned>(temp)) - 1;
      if (nb > 14) VIO_FAIL("Missing Huffman code table entry");
      emit_symbol(ac_tbl_no_, nb << 4);
      if (nb) emit_bits(EOBRUN_, nb);
      EOBRUN_ = 0;
      emit_buffered_bits(bit_buffer_, BE_);
      BE_ = 0;
    }
  }

  void emit_restart(int restart_num) {
    emit_eobrun();
    if (!gather_) {
      bw_.flush(cs_.dest);
      cs_.emit_byte(0xFF);
      cs_.emit_byte(M_RST0 + restart_num);
    }
    if (cs_.Ss == 0) {
      for (int ci = 0; ci < cs_.comps_in_scan; ci++) last_dc_val_[ci] = 0;
    } else {
      EOBRUN_ = 0;
      BE_ = 0;
    }
  }

  void dc_first(Block** MCU_data) {
    int Al = cs_.Al;
    int max_coef_bits = cs_.data_precision + 2;
    for (int blkn = 0; blkn < cs_.blocks_in_MCU; blkn++) {
      Coef* block = MCU_data[blkn]->data();
      int ci = cs_.MCU_membership[blkn];
      ComponentInfo* c = cs_.cur_comp_info[ci];
      int temp2 = static_cast<int>(block[0]) >> Al;
      int temp = temp2 - last_dc_val_[ci];
      last_dc_val_[ci] = temp2;
      int temp3 = temp >> (CHAR_BIT * sizeof(int) - 1);
      temp ^= temp3;
      temp -= temp3;
      temp2 = temp ^ temp3;
      int nb = nbits(static_cast<unsigned>(temp));
      if (nb > max_coef_bits + 1)
        VIO_FAIL("DCT coefficient (lossy) or spatial difference (lossless) out of range");
      emit_symbol(c->dc_tbl_no, nb);
      if (nb) emit_bits(static_cast<unsigned>(temp2), nb);
    }
  }

  VIO_NO_STACK_CHECK void ac_first(Block** MCU_data) {
    const Coef* block = MCU_data[0]->data();
    int Sl = cs_.Se - cs_.Ss + 1;
    int Al = cs_.Al;
    int max_coef_bits = cs_.data_precision + 2;
    alignas(16) std::uint16_t values[2 * DCTSIZE2];
    std::uint64_t zerobits = 0;
    const int* order = natural_order + cs_.Ss;
    if (simd_.ac_first_prepare) {
      zerobits = simd_.ac_first_prepare(block, order, Sl, Al, values);
    } else {
    for (int k = 0; k < Sl; k++) {
      int temp = block[order[k]];
      if (temp == 0) continue;
      int temp2 = temp >> (CHAR_BIT * sizeof(int) - 1);
      temp ^= temp2;
      temp -= temp2;
      temp >>= Al;
      if (temp == 0) continue;
      temp2 ^= temp;
      values[k] = static_cast<std::uint16_t>(temp);
      values[k + DCTSIZE2] = static_cast<std::uint16_t>(temp2);
      zerobits |= std::uint64_t(1) << k;
    }
    }
    if (zerobits && EOBRUN_ > 0) emit_eobrun();
    const std::uint16_t* cvalue = values;
    while (zerobits) {
      int r = ctz64(zerobits);
      zerobits >>= r;
      cvalue += r;
      int temp = cvalue[0];
      int temp2 = cvalue[DCTSIZE2];
      while (r > 15) {
        emit_symbol(ac_tbl_no_, 0xF0);
        r -= 16;
      }
      int nb = nbits_nonzero(static_cast<unsigned>(temp));
      if (nb > max_coef_bits)
        VIO_FAIL("DCT coefficient (lossy) or spatial difference (lossless) out of range");
      emit_symbol_bits(ac_tbl_no_, (r << 4) + nb, static_cast<unsigned>(temp2), nb);
      cvalue++;
      zerobits >>= 1;
    }
    if (cvalue < values + Sl) {
      EOBRUN_++;
      if (EOBRUN_ == 0x7FFF) emit_eobrun();
    }
  }

  void dc_refine(Block** MCU_data) {
    int Al = cs_.Al;
    for (int blkn = 0; blkn < cs_.blocks_in_MCU; blkn++) {
      int temp = (*MCU_data[blkn])[0];
      emit_bits(static_cast<unsigned>(temp >> Al), 1);
    }
  }

  VIO_NO_STACK_CHECK void ac_refine(Block** MCU_data) {
    const Coef* block = MCU_data[0]->data();
    int Sl = cs_.Se - cs_.Ss + 1;
    int Al = cs_.Al;
    alignas(16) std::uint16_t absvalues[DCTSIZE2];
    std::uint64_t zerobits = 0, signbits = 0;
    int EOB = 0;
    const int* order = natural_order + cs_.Ss;
    if (simd_.ac_refine_prepare) {
      EOB = simd_.ac_refine_prepare(block, order, Sl, Al, absvalues, &zerobits, &signbits);
    } else {
    for (int k = 0; k < Sl; k++) {
      int temp = block[order[k]];
      int temp2 = temp >> (CHAR_BIT * sizeof(int) - 1);
      temp ^= temp2;
      temp -= temp2;
      temp >>= Al;
      if (temp != 0) {
        zerobits |= std::uint64_t(1) << k;
        signbits |= static_cast<std::uint64_t>(temp2 + 1) << k;
      }
      absvalues[k] = static_cast<std::uint16_t>(temp);
      if (temp == 1) EOB = k;
    }
    }
    const std::uint16_t* EOBPTR = absvalues + EOB;
    const std::uint16_t* cabsvalue = absvalues;
    int r = 0;
    unsigned BR = 0;
    char* BR_buffer = bit_buffer_ + BE_;
    while (zerobits) {
      int idx = ctz64(zerobits);
      zerobits >>= idx;
      r += idx;
      cabsvalue += idx;
      signbits >>= idx;
      while (r > 15 && cabsvalue <= EOBPTR) {
        emit_eobrun();
        emit_symbol(ac_tbl_no_, 0xF0);
        r -= 16;
        emit_buffered_bits(BR_buffer, BR);
        BR_buffer = bit_buffer_;
        BR = 0;
      }
      int temp = *cabsvalue++;
      if (temp > 1) {
        BR_buffer[BR++] = static_cast<char>(temp & 1);
        signbits >>= 1;
        zerobits >>= 1;
        continue;
      }
      emit_eobrun();
      emit_symbol_bits(ac_tbl_no_, (r << 4) + 1, static_cast<unsigned>(signbits & 1), 1);
      emit_buffered_bits(BR_buffer, BR);
      BR_buffer = bit_buffer_;
      BR = 0;
      r = 0;
      signbits >>= 1;
      zerobits >>= 1;
    }
    r |= static_cast<int>((absvalues + Sl) - cabsvalue);
    if (r > 0 || BR > 0) {
      EOBRUN_++;
      BE_ += BR;
      if (EOBRUN_ == 0x7FFF || BE_ > (MAX_CORR_BITS - DCTSIZE2 + 1)) emit_eobrun();
    }
  }

  CState& cs_;
  simd::PhuffKernels simd_;
  bool gather_ = false;
  Mode mode_ = DC_FIRST;
  WordBitWriter bw_;
  int last_dc_val_[MAX_COMPS_IN_SCAN]{};
  int ac_tbl_no_ = 0;
  unsigned EOBRUN_ = 0, BE_ = 0;
  char bit_buffer_[MAX_CORR_BITS]{};
  unsigned restarts_to_go_ = 0;
  int next_restart_num_ = 0;
  CDerivedTable derived_[NUM_HUFF_TBLS];
  long count_[NUM_HUFF_TBLS][257]{};
};

// ===========================================================================
// Lossless Huffman encoder
class LhuffEncoder final : public EntropyEncoder {
 public:
  explicit LhuffEncoder(CState& cs) : cs_(cs) {}

  void start_pass(bool gather) override {
    gather_ = gather;
    for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
      ComponentInfo* c = cs_.cur_comp_info[ci];
      int dctbl = c->dc_tbl_no;
      if (gather) {
        if (dctbl < 0 || dctbl >= NUM_HUFF_TBLS) VIO_FAIL("Huffman table 0x%02x was not defined", dctbl);
        std::memset(count_[dctbl], 0, sizeof(count_[dctbl]));
      } else {
        make_c_derived_tbl(cs_, true, dctbl, derived_[dctbl]);
      }
    }
    int ptrn = 0;
    for (int sampn = 0; sampn < cs_.blocks_in_MCU;) {
      ComponentInfo* c = cs_.cur_comp_info[cs_.MCU_membership[sampn]];
      int ci = c->component_index;
      for (int yoffset = 0; yoffset < c->MCU_height; yoffset++, ptrn++) {
        info_[ptrn] = {ci, yoffset, c->MCU_width};
        for (int xoffset = 0; xoffset < c->MCU_width; xoffset++, sampn++) {
          ptr_index_[sampn] = ptrn;
          cur_tbls_[sampn] = &derived_[c->dc_tbl_no];
          cur_counts_[sampn] = count_[c->dc_tbl_no];
        }
      }
    }
    num_input_ptrs_ = ptrn;
    bw_ = {};
    restarts_to_go_ = cs_.restart_interval;
    next_restart_num_ = 0;
  }

  unsigned encode_mcus(int** const* diff_buf, unsigned MCU_row_num, unsigned MCU_col_num,
                       unsigned nMCU) override {
    const int* input_ptr[C_MAX_BLOCKS_IN_MCU];
    if (gather_) {
      if (cs_.restart_interval) {
        if (restarts_to_go_ == 0) restarts_to_go_ = cs_.restart_interval;
        restarts_to_go_--;
      }
    } else if (cs_.restart_interval && restarts_to_go_ == 0) {
      bw_.flush(cs_.dest);
      cs_.emit_byte(0xFF);
      cs_.emit_byte(M_RST0 + next_restart_num_);
    }
    for (int ptrn = 0; ptrn < num_input_ptrs_; ptrn++) {
      const auto& inf = info_[ptrn];
      input_ptr[ptrn] = diff_buf[inf.ci][MCU_row_num + inf.yoffset] + MCU_col_num * inf.MCU_width;
    }
    const int blocks = cs_.blocks_in_MCU;
    if (gather_) {
      for (unsigned mcu_num = 0; mcu_num < nMCU; mcu_num++) {
        for (int sampn = 0; sampn < blocks; sampn++) {
          int nb = magnitude_bits(*input_ptr[ptr_index_[sampn]]++).nbits;
          cur_counts_[sampn][nb]++;
        }
      }
      return nMCU;
    }

    // Emit with a local bit accumulator and output pointer.  Each sample
    // produces at most 31 bits (<= 8 bytes after 0xFF stuffing).
    ByteSink& dest = cs_.dest;
    dest.ensure(static_cast<std::size_t>(nMCU) * static_cast<std::size_t>(blocks) * 8 + 16);
    std::uint8_t* out = dest.next;
    std::uint64_t acc = bw_.put_bits ? bw_.put_buffer >> (24 - bw_.put_bits) : 0;
    int n = bw_.put_bits;
    const CDerivedTable* tbls[C_MAX_BLOCKS_IN_MCU];
    const int* ptrs[C_MAX_BLOCKS_IN_MCU];
    int ptr_index[C_MAX_BLOCKS_IN_MCU];
    for (int sampn = 0; sampn < blocks; sampn++) {
      tbls[sampn] = cur_tbls_[sampn];
      ptr_index[sampn] = ptr_index_[sampn];
    }
    for (int ptrn = 0; ptrn < num_input_ptrs_; ptrn++) ptrs[ptrn] = input_ptr[ptrn];
    for (unsigned mcu_num = 0; mcu_num < nMCU; mcu_num++) {
      for (int sampn = 0; sampn < blocks; sampn++) {
        Magnitude m = magnitude_bits(*ptrs[ptr_index[sampn]]++);
        const CDerivedTable& dctbl = *tbls[sampn];
        int size = dctbl.ehufsi[m.nbits];
        if (size == 0) VIO_FAIL("Missing Huffman code table entry");
        std::uint64_t code = dctbl.ehufco[m.nbits];
        if (m.nbits && m.nbits != 16) {
          code = (code << m.nbits) | (static_cast<unsigned>(m.bits) & ((1u << m.nbits) - 1));
          size += m.nbits;
        }
        acc = (acc << size) | code;
        n += size;
        while (n >= 8) {
          n -= 8;
          std::uint8_t c = static_cast<std::uint8_t>(acc >> n);
          *out++ = c;
          if (VIO_UNLIKELY(c == 0xFF)) *out++ = 0;
        }
      }
      if (cs_.restart_interval) {
        if (restarts_to_go_ == 0) {
          restarts_to_go_ = cs_.restart_interval;
          next_restart_num_ = (next_restart_num_ + 1) & 7;
        }
        restarts_to_go_--;
      }
    }
    dest.free_in_buffer -= static_cast<std::size_t>(out - dest.next);
    dest.next = out;
    bw_.put_bits = n;
    bw_.put_buffer = n ? static_cast<std::size_t>(acc & ((std::uint64_t(1) << n) - 1)) << (24 - n) : 0;
    return nMCU;
  }

  void finish_pass() override {
    if (gather_) {
      bool did_dc[NUM_HUFF_TBLS] = {};
      for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
        int dctbl = cs_.cur_comp_info[ci]->dc_tbl_no;
        if (!did_dc[dctbl]) {
          auto& h = cs_.dc_huff_tbl_ptrs[dctbl];
          if (!h) h.emplace();
          gen_optimal_table(*h, count_[dctbl]);
          did_dc[dctbl] = true;
        }
      }
    } else {
      bw_.flush(cs_.dest);
    }
  }

 private:
  struct PtrInfo {
    int ci, yoffset, MCU_width;
  };
  struct Magnitude {
    int nbits, bits;  // magnitude category and the value's bits to emit
  };
  VIO_INLINE static Magnitude magnitude_bits(int temp) {
    int temp2;
    if (temp & 0x8000) {
      temp = (-temp) & 0x7FFF;
      if (temp == 0) temp = 0x8000;
      temp2 = ~temp;
    } else {
      temp &= 0x7FFF;
      temp2 = temp;
    }
    int nb = nbits(static_cast<unsigned>(temp));
    return {nb, temp2};
  }
  CState& cs_;
  bool gather_ = false;
  SimpleBitWriter bw_;
  unsigned restarts_to_go_ = 0;
  int next_restart_num_ = 0;
  CDerivedTable derived_[NUM_HUFF_TBLS];
  long count_[NUM_HUFF_TBLS][257]{};
  const CDerivedTable* cur_tbls_[C_MAX_BLOCKS_IN_MCU]{};
  long* cur_counts_[C_MAX_BLOCKS_IN_MCU]{};
  PtrInfo info_[C_MAX_BLOCKS_IN_MCU]{};
  int ptr_index_[C_MAX_BLOCKS_IN_MCU]{};
  int num_input_ptrs_ = 0;
};

}  // namespace vio::jpeg::detail
