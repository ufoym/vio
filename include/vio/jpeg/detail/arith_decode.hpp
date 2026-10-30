// vio::jpeg - arithmetic entropy decoding.
#pragma once

#include <bit>

#include "arith_table.hpp"
#include "huff_decode.hpp"  // init_coef_bits / prepare_progressive_scan

namespace vio::jpeg::detail {

class ArithDecoder final : public EntropyDecoder {
 public:
  explicit ArithDecoder(DState& st) : st_(st) {
    fixed_bin_[0] = 113;
    if (st.progressive_mode) init_coef_bits(st);
  }

  void start_pass() override {
    if (st_.progressive_mode) {
      prepare_progressive_scan(st_);
      if (st_.Ah == 0) mode_ = st_.Ss == 0 ? DC_FIRST : AC_FIRST;
      else mode_ = st_.Ss == 0 ? DC_REFINE : AC_REFINE;
    } else {
      if (st_.Ss != 0 || st_.Se != DCTSIZE2 - 1 || st_.Ah != 0 || st_.Al != 0)
        st_.warn("Invalid SOS parameters for sequential JPEG");
      mode_ = SEQUENTIAL;
    }
    reset_statistics<true>();
    c_ = 0;
    a_ = 0;
    ct_ = -16;
    insufficient_data = false;
    restarts_to_go_ = st_.restart_interval;
  }

  bool decode_mcu(Block** MCU_data) override {
    if (st_.restart_interval) {
      if (restarts_to_go_ == 0) process_restart();
      restarts_to_go_--;
    }
    switch (mode_) {
      case SEQUENTIAL: sequential(MCU_data); break;
      case DC_FIRST: dc_first(MCU_data); break;
      case AC_FIRST: ac_first(MCU_data); break;
      case DC_REFINE: dc_refine(MCU_data); break;
      case AC_REFINE: ac_refine(MCU_data); break;
    }
    return true;
  }

  bool process_restart() override {
    st_.read_restart_marker();
    reset_statistics<false>();
    c_ = 0;
    a_ = 0;
    ct_ = -16;
    restarts_to_go_ = st_.restart_interval;
    return true;
  }

 private:
  enum Mode { SEQUENTIAL, DC_FIRST, AC_FIRST, DC_REFINE, AC_REFINE };
  static constexpr int DC_STAT_BINS = 64;
  static constexpr int AC_STAT_BINS = 256;

  template <bool validate>
  VIO_INLINE void reset_statistics() {
    for (int ci = 0; ci < st_.comps_in_scan; ci++) {
      ComponentInfo* c = st_.cur_comp_info[ci];
      if (!st_.progressive_mode || (st_.Ss == 0 && st_.Ah == 0)) {
        int tbl = c->dc_tbl_no;
        if (validate && (tbl < 0 || tbl >= NUM_ARITH_TBLS))
          VIO_FAIL("Arithmetic table 0x%02x was not defined", tbl);
        std::memset(dc_stats_[tbl], 0, DC_STAT_BINS);
        last_dc_val_[ci] = 0;
        dc_context_[ci] = 0;
      }
      if (!st_.progressive_mode || st_.Ss) {
        int tbl = c->ac_tbl_no;
        if (validate && (tbl < 0 || tbl >= NUM_ARITH_TBLS))
          VIO_FAIL("Arithmetic table 0x%02x was not defined", tbl);
        std::memset(ac_stats_[tbl], 0, AC_STAT_BINS);
      }
    }
  }

  // Coder registers.  The decoding routines work on a local copy, which the
  // compiler can keep in registers (the statistics bins are bytes, which
  // could alias the members).
  struct Coder {
    JLong a, c;
    int ct;
  };
  Coder load() const { return {a_, c_, ct_}; }
  // Member-wise, so that q is not where an out-of-line call returns its
  // result (MSVC would then keep q in memory throughout).
  static VIO_INLINE void set(Coder& q, const Coder& r) {
    q.a = r.a;
    q.c = r.c;
    q.ct = r.ct;
  }
  void save(Coder q) {
    unread(q);
    a_ = q.a;
    c_ = q.c;
    ct_ = q.ct;
  }

  // Bytes are read ahead while none of them is 0xFF: c takes 8 more low
  // bits per byte and ct grows by 8, which leaves every comparison of c
  // with a << ct unchanged.  Between MCUs the bytes read ahead are given
  // back, so that the input position (and what follows at a restart
  // marker or at the end of the scan) is that of byte-at-a-time reading.
  VIO_INLINE void read_ahead(Coder& q) {
    if (st_.bytes_in_buffer < 8 || st_.unread_marker || q.ct < 0) return;
    std::uint64_t w;
    std::memcpy(&w, st_.next_input_byte, 8);
    w = ::vio::jpeg::simd::HuffBitWriter::to_big_endian(w);
    // 0x80 in each 0xFF byte (exact: no borrow between bytes)
    const std::uint64_t lo = 0x7F7F7F7F7F7F7F7FULL;
    const std::uint64_t ff = ((w & lo) + 0x0101010101010101ULL) & w & ~lo;
    int k = (40 - q.ct) >> 3;  // c stays below 2^56
    const int clean = ff ? std::countl_zero(ff) >> 3 : 8;
    if (clean < k) k = clean;
    if (k <= 0) return;
    q.c = (q.c << (8 * k)) | static_cast<JLong>(w >> (64 - 8 * k));
    q.ct += 8 * k;
    st_.next_input_byte += k;
    st_.bytes_in_buffer -= static_cast<std::size_t>(k);
  }
  VIO_INLINE void unread(Coder& q) {
    if (q.ct >= 8) {
      const int j = q.ct >> 3;
      q.c >>= 8 * j;
      q.ct -= 8 * j;
      st_.next_input_byte -= j;
      st_.bytes_in_buffer += static_cast<std::size_t>(j);
    }
  }

  // Renormalization one bit at a time, reading bytes as needed.
  VIO_NOINLINE Coder renorm(Coder q) {
    while (q.a < 0x8000L) {
      if (--q.ct < 0) {
        int data;
        if (st_.unread_marker) {
          data = 0;
        } else {
          data = st_.read_byte();
          if (data == 0xFF) {
            do data = st_.read_byte();
            while (data == 0xFF);
            if (data == 0) {
              data = 0xFF;
            } else {
              st_.unread_marker = data;
              data = 0;
            }
          }
        }
        q.c = (q.c << 8) | data;
        if ((q.ct += 8) < 0)
          if (++q.ct == 0) q.a = 0x8000L;
      }
      q.a <<= 1;
    }
    return q;
  }

  VIO_INLINE int decode(Coder& q, std::uint8_t* st) {
    if (q.a < 0x8000L) {
      // All the shifts at once when no byte has to be read.
      int n = q.a ? std::countl_zero(static_cast<std::uint32_t>(q.a)) - 16 : 64;
      if (VIO_UNLIKELY(q.ct < n)) read_ahead(q);
      if (VIO_LIKELY(q.ct >= n)) {
        q.a <<= n;
        q.ct -= n;
      } else {
        set(q, renorm(q));
      }
    }
    int sv = *st;
    JLong qe = aritab[sv & 0x7F];
    std::uint8_t nl = static_cast<std::uint8_t>(qe & 0xFF);
    qe >>= 8;
    std::uint8_t nm = static_cast<std::uint8_t>(qe & 0xFF);
    qe >>= 8;
    // Without branches: the sub-interval c falls in (lps) and the
    // conditional exchange (a < qe) select the new state and the bit.
    const JLong a1 = q.a - qe;
    const JLong t = a1 << q.ct;
    const bool lps = q.c >= t;
    const bool exchange = a1 < qe;
    const bool flip = lps != exchange;  // the less probable symbol
    const JLong m = -static_cast<JLong>(lps);  // masks: compilers may branch on selects
    q.c -= t & m;
    q.a = a1 ^ ((a1 ^ qe) & m);
    const int next = (sv & 0x80) ^ (flip ? nl : nm);
#if defined(_MSC_VER) && !defined(__clang__)
    // (MSVC branches on the select below; GCC and Clang do best with it.)
    const int update = -static_cast<int>(lps | (a1 < 0x8000L));
    *st = static_cast<std::uint8_t>(sv ^ ((next ^ sv) & update));
#else
    *st = static_cast<std::uint8_t>(lps || a1 < 0x8000L ? next : sv);
#endif
    return (sv >> 7) ^ static_cast<int>(flip);
  }

  // (By value: the decoding loops' Coder stays out of memory.)
  VIO_NOINLINE Coder bad_code(Coder q) {
    st_.warn("Corrupt JPEG data: bad arithmetic code");
    unread(q);
    q.ct = -1;
    return q;
  }

  // Decode a DC difference for component ci; returns false on error.
  VIO_INLINE bool decode_dc(Coder& q, int ci, int tbl) {
    std::uint8_t* st = dc_stats_[tbl] + dc_context_[ci];
    if (decode(q, st) == 0) {
      dc_context_[ci] = 0;
    } else {
      int sign = decode(q, st + 1);
      st += 2;
      st += sign;
      int m;
      if ((m = decode(q, st)) != 0) {
        st = dc_stats_[tbl] + 20;
        while (decode(q, st)) {
          if ((m <<= 1) == 0x8000) {
            set(q, bad_code(q));
            return false;
          }
          st += 1;
        }
      }
      if (m < static_cast<int>((1L << st_.arith_dc_L[tbl]) >> 1))
        dc_context_[ci] = 0;
      else if (m > static_cast<int>((1L << st_.arith_dc_U[tbl]) >> 1))
        dc_context_[ci] = 12 + (sign * 4);
      else
        dc_context_[ci] = 4 + (sign * 4);
      int v = m;
      st += 14;
      while (m >>= 1)
        if (decode(q, st)) v |= m;
      v += 1;
      if (sign) v = -v;
      last_dc_val_[ci] = (last_dc_val_[ci] + v) & 0xffff;
    }
    return true;
  }

  // Decode AC coefficients k in [kstart, kend]; returns false on error.
  VIO_INLINE bool decode_ac(Coder& q, Coef* block, int tbl, int kstart, int kend, int Al) {
    for (int k = kstart; k <= kend; k++) {
      std::uint8_t* st = ac_stats_[tbl] + 3 * (k - 1);
      if (decode(q, st)) break;
      while (decode(q, st + 1) == 0) {
        st += 3;
        k++;
        if (k > kend) {
          set(q, bad_code(q));
          return false;
        }
      }
      int sign = decode(q, fixed_bin_);
      st += 2;
      int m;
      if ((m = decode(q, st)) != 0) {
        if (decode(q, st)) {
          m <<= 1;
          st = ac_stats_[tbl] + (k <= st_.arith_ac_K[tbl] ? 189 : 217);
          while (decode(q, st)) {
            if ((m <<= 1) == 0x8000) {
              set(q, bad_code(q));
              return false;
            }
            st += 1;
          }
        }
      }
      int v = m;
      st += 14;
      while (m >>= 1)
        if (decode(q, st)) v |= m;
      v += 1;
      if (sign) v = -v;
      if (block) block[natural_order[k]] = static_cast<Coef>(static_cast<unsigned>(v) << Al);
    }
    return true;
  }

  void sequential(Block** MCU_data) {
    Coder q = load();
    if (q.ct == -1) return save(q);
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      Coef* block = MCU_data ? MCU_data[blkn]->data() : nullptr;
      int ci = st_.MCU_membership[blkn];
      ComponentInfo* c = st_.cur_comp_info[ci];
      if (!decode_dc(q, ci, c->dc_tbl_no)) return save(q);
      if (block) block[0] = static_cast<Coef>(last_dc_val_[ci]);
      if (!decode_ac(q, block, c->ac_tbl_no, 1, DCTSIZE2 - 1, 0)) return save(q);
    }
    save(q);
  }

  void dc_first(Block** MCU_data) {
    Coder q = load();
    if (q.ct == -1) return save(q);
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      Coef* block = MCU_data[blkn]->data();
      int ci = st_.MCU_membership[blkn];
      if (!decode_dc(q, ci, st_.cur_comp_info[ci]->dc_tbl_no)) return save(q);
      block[0] = static_cast<Coef>(lshift(last_dc_val_[ci], st_.Al));
    }
    save(q);
  }

  void ac_first(Block** MCU_data) {
    Coder q = load();
    if (q.ct == -1) return save(q);
    decode_ac(q, MCU_data[0]->data(), st_.cur_comp_info[0]->ac_tbl_no, st_.Ss, st_.Se, st_.Al);
    save(q);
  }

  void dc_refine(Block** MCU_data) {
    Coder q = load();
    int p1 = 1 << st_.Al;
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++)
      if (decode(q, fixed_bin_)) (*MCU_data[blkn])[0] = static_cast<Coef>((*MCU_data[blkn])[0] | p1);
    save(q);
  }

  void ac_refine(Block** MCU_data) {
    Coder q = load();
    if (q.ct == -1) return save(q);
    Coef* block = MCU_data[0]->data();
    int tbl = st_.cur_comp_info[0]->ac_tbl_no;
    int p1 = 1 << st_.Al;
    int m1 = static_cast<int>(~0u << st_.Al);
    int kex;
    for (kex = st_.Se; kex > 0; kex--)
      if (block[natural_order[kex]]) break;
    for (int k = st_.Ss; k <= st_.Se; k++) {
      std::uint8_t* st = ac_stats_[tbl] + 3 * (k - 1);
      if (k > kex)
        if (decode(q, st)) break;
      for (;;) {
        Coef* thiscoef = block + natural_order[k];
        if (*thiscoef) {
          if (decode(q, st + 2)) {
            if (*thiscoef < 0) *thiscoef = static_cast<Coef>(*thiscoef + m1);
            else *thiscoef = static_cast<Coef>(*thiscoef + p1);
          }
          break;
        }
        if (decode(q, st + 1)) {
          *thiscoef = static_cast<Coef>(decode(q, fixed_bin_) ? m1 : p1);
          break;
        }
        st += 3;
        k++;
        if (k > st_.Se) {
          set(q, bad_code(q));
          return save(q);
        }
      }
    }
    save(q);
  }

  DState& st_;
  Mode mode_ = SEQUENTIAL;
  JLong c_ = 0, a_ = 0;
  int ct_ = -16;
  int last_dc_val_[MAX_COMPS_IN_SCAN]{};
  int dc_context_[MAX_COMPS_IN_SCAN]{};
  unsigned restarts_to_go_ = 0;
  std::uint8_t dc_stats_[NUM_ARITH_TBLS][DC_STAT_BINS]{};
  std::uint8_t ac_stats_[NUM_ARITH_TBLS][AC_STAT_BINS]{};
  std::uint8_t fixed_bin_[4]{};
};

}  // namespace vio::jpeg::detail
