// vio::jpeg - arithmetic entropy encoding.
#pragma once

#include "arith_table.hpp"
#include "huff_encode.hpp"

namespace vio::jpeg::detail {

class ArithEncoder final : public EntropyEncoder {
 public:
  explicit ArithEncoder(CState& cs) : cs_(cs) { fixed_bin_[0] = 113; }

  void start_pass(bool gather) override {
    if (gather) VIO_FAIL("Requested features are incompatible");
    if (cs_.progressive_mode) {
      if (cs_.Ah == 0) mode_ = cs_.Ss == 0 ? DC_FIRST : AC_FIRST;
      else mode_ = cs_.Ss == 0 ? DC_REFINE : AC_REFINE;
    } else {
      mode_ = SEQUENTIAL;
    }
    for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
      ComponentInfo* c = cs_.cur_comp_info[ci];
      if (!cs_.progressive_mode || (cs_.Ss == 0 && cs_.Ah == 0)) {
        int tbl = c->dc_tbl_no;
        if (tbl < 0 || tbl >= NUM_ARITH_TBLS) VIO_FAIL("Arithmetic table 0x%02x was not defined", tbl);
        std::memset(dc_stats_[tbl], 0, DC_STAT_BINS);
        last_dc_val_[ci] = 0;
        dc_context_[ci] = 0;
      }
      if (!cs_.progressive_mode || cs_.Se) {
        int tbl = c->ac_tbl_no;
        if (tbl < 0 || tbl >= NUM_ARITH_TBLS) VIO_FAIL("Arithmetic table 0x%02x was not defined", tbl);
        std::memset(ac_stats_[tbl], 0, AC_STAT_BINS);
      }
    }
    reset_coder();
    restarts_to_go_ = cs_.restart_interval;
    next_restart_num_ = 0;
  }

  void encode_mcu(Block** MCU_data) override {
    if (cs_.restart_interval) {
      if (restarts_to_go_ == 0) {
        emit_restart(next_restart_num_);
        restarts_to_go_ = cs_.restart_interval;
        next_restart_num_ = (next_restart_num_ + 1) & 7;
      }
      restarts_to_go_--;
    }
    switch (mode_) {
      case SEQUENTIAL: sequential(MCU_data); break;
      case DC_FIRST: dc_first(MCU_data); break;
      case AC_FIRST: ac_first(MCU_data); break;
      case DC_REFINE: dc_refine(MCU_data); break;
      case AC_REFINE: ac_refine(MCU_data); break;
    }
  }

  void finish_pass() override {
    JLong temp;
    if ((temp = (a_ - 1 + c_) & 0xFFFF0000L) < c_) c_ = temp + 0x8000L;
    else c_ = temp;
    c_ <<= ct_;
    if (c_ & 0xF8000000L) {
      if (buffer_ >= 0) {
        flush_zeros();
        emit_byte(buffer_ + 1);
        if (buffer_ + 1 == 0xFF) emit_byte(0x00);
      }
      zc_ += sc_;
      sc_ = 0;
    } else {
      if (buffer_ == 0) {
        ++zc_;
      } else if (buffer_ >= 0) {
        flush_zeros();
        emit_byte(buffer_);
      }
      if (sc_) {
        flush_zeros();
        do {
          emit_byte(0xFF);
          emit_byte(0x00);
        } while (--sc_);
      }
    }
    if (c_ & 0x7FFF800L) {
      flush_zeros();
      emit_byte(static_cast<int>((c_ >> 19) & 0xFF));
      if (((c_ >> 19) & 0xFF) == 0xFF) emit_byte(0x00);
      if (c_ & 0x7F800L) {
        emit_byte(static_cast<int>((c_ >> 11) & 0xFF));
        if (((c_ >> 11) & 0xFF) == 0xFF) emit_byte(0x00);
      }
    }
  }

 private:
  enum Mode { SEQUENTIAL, DC_FIRST, AC_FIRST, DC_REFINE, AC_REFINE };
  static constexpr int DC_STAT_BINS = 64;
  static constexpr int AC_STAT_BINS = 256;

  void emit_byte(int v) { cs_.dest.put(v); }
  void flush_zeros() {
    if (zc_)
      do emit_byte(0x00);
      while (--zc_);
  }

  void reset_coder() {
    c_ = 0;
    a_ = 0x10000L;
    sc_ = 0;
    zc_ = 0;
    ct_ = 11;
    buffer_ = -1;
  }

  // Coder registers.  The encoding routines work on a local copy, which the
  // compiler can keep in registers (the statistics bins and the output are
  // bytes, which could alias the members).
  struct Coder {
    JLong a, c;
    int ct;
  };
  Coder load() const { return {a_, c_, ct_}; }
  void save(const Coder& k) {
    a_ = k.a;
    c_ = k.c;
    ct_ = k.ct;
  }

  // Outputs the byte completed in c (ct has reached 0); returns c without it.
  VIO_NOINLINE JLong byte_out(JLong c) {
    JLong temp = c >> 19;
    if (temp > 0xFF) {
      if (buffer_ >= 0) {
        flush_zeros();
        emit_byte(buffer_ + 1);
        if (buffer_ + 1 == 0xFF) emit_byte(0x00);
      }
      zc_ += sc_;
      sc_ = 0;
      buffer_ = static_cast<int>(temp & 0xFF);
    } else if (temp == 0xFF) {
      ++sc_;
    } else {
      if (buffer_ == 0) {
        ++zc_;
      } else if (buffer_ >= 0) {
        flush_zeros();
        emit_byte(buffer_);
      }
      if (sc_) {
        flush_zeros();
        do {
          emit_byte(0xFF);
          emit_byte(0x00);
        } while (--sc_);
      }
      buffer_ = static_cast<int>(temp & 0xFF);
    }
    return c & 0x7FFFFL;
  }

  VIO_INLINE void encode(Coder& k, std::uint8_t* st, int val) {
    int sv = *st;
    JLong qe = aritab[sv & 0x7F];
    std::uint8_t nl = static_cast<std::uint8_t>(qe & 0xFF);
    qe >>= 8;
    std::uint8_t nm = static_cast<std::uint8_t>(qe & 0xFF);
    qe >>= 8;
    k.a -= qe;
    if (val != (sv >> 7)) {
      if (k.a >= qe) {
        k.c += k.a;
        k.a = qe;
      }
      *st = static_cast<std::uint8_t>((sv & 0x80) ^ nl);
    } else {
      if (k.a >= 0x8000L) return;
      if (k.a < qe) {
        k.c += k.a;
        k.a = qe;
      }
      *st = static_cast<std::uint8_t>((sv & 0x80) ^ nm);
    }
    // Renormalize: shift a (0 < a < 0x8000) and c left until a >= 0x8000,
    // outputting a byte each time ct counts down to 0.
    int n = clz32(static_cast<std::uint32_t>(k.a)) - 16;
    k.a <<= n;
    if (VIO_LIKELY(n < k.ct)) {
      k.c <<= n;
      k.ct -= n;
      return;
    }
    n -= k.ct;
    k.c = byte_out(k.c << k.ct);
    if (n >= 8) {
      n -= 8;
      k.c = byte_out(k.c << 8);
    }
    k.c <<= n;
    k.ct = 8 - n;
  }
  void emit_restart(int restart_num) {
    finish_pass();
    emit_byte(0xFF);
    emit_byte(M_RST0 + restart_num);
    for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
      ComponentInfo* c = cs_.cur_comp_info[ci];
      if (!cs_.progressive_mode || (cs_.Ss == 0 && cs_.Ah == 0)) {
        std::memset(dc_stats_[c->dc_tbl_no], 0, DC_STAT_BINS);
        last_dc_val_[ci] = 0;
        dc_context_[ci] = 0;
      }
      if (!cs_.progressive_mode || cs_.Se) std::memset(ac_stats_[c->ac_tbl_no], 0, AC_STAT_BINS);
    }
    reset_coder();
  }

  // Encodes DC difference v for component ci (shared by sequential/DC first).
  VIO_INLINE void encode_dc(Coder& q, int ci, int tbl, int v) {
    std::uint8_t* st = dc_stats_[tbl] + dc_context_[ci];
    if (v == 0) {
      encode(q, st, 0);
      dc_context_[ci] = 0;
      return;
    }
    encode(q, st, 1);
    if (v > 0) {
      encode(q, st + 1, 0);
      st += 2;
      dc_context_[ci] = 4;
    } else {
      v = -v;
      encode(q, st + 1, 1);
      st += 3;
      dc_context_[ci] = 8;
    }
    int m = 0;
    if (v -= 1) {
      encode(q, st, 1);
      m = 1;
      int v2 = v;
      st = dc_stats_[tbl] + 20;
      while (v2 >>= 1) {
        encode(q, st, 1);
        m <<= 1;
        st += 1;
      }
    }
    encode(q, st, 0);
    if (m < static_cast<int>((1L << cs_.arith_dc_L[tbl]) >> 1)) dc_context_[ci] = 0;
    else if (m > static_cast<int>((1L << cs_.arith_dc_U[tbl]) >> 1)) dc_context_[ci] += 8;
    st += 14;
    while (m >>= 1) encode(q, st, (m & v) ? 1 : 0);
  }

  // Encodes the magnitude (v >= 1) of an AC coefficient at index k.
  VIO_INLINE void encode_ac_magnitude(Coder& q, std::uint8_t* st, int tbl, int k, int v) {
    st += 2;
    int m = 0;
    if (v -= 1) {
      encode(q, st, 1);
      m = 1;
      int v2 = v;
      if (v2 >>= 1) {
        encode(q, st, 1);
        m <<= 1;
        st = ac_stats_[tbl] + (k <= cs_.arith_ac_K[tbl] ? 189 : 217);
        while (v2 >>= 1) {
          encode(q, st, 1);
          m <<= 1;
          st += 1;
        }
      }
    }
    encode(q, st, 0);
    st += 14;
    while (m >>= 1) encode(q, st, (m & v) ? 1 : 0);
  }

  void sequential(Block** MCU_data) {
    Coder q = load();
    for (int blkn = 0; blkn < cs_.blocks_in_MCU; blkn++) {
      const Coef* block = MCU_data[blkn]->data();
      int ci = cs_.MCU_membership[blkn];
      ComponentInfo* c = cs_.cur_comp_info[ci];
      int v = block[0] - last_dc_val_[ci];
      if (v != 0) last_dc_val_[ci] = block[0];
      encode_dc(q, ci, c->dc_tbl_no, v);
      int tbl = c->ac_tbl_no;
      int ke;
      for (ke = DCTSIZE2 - 1; ke > 0; ke--)
        if (block[natural_order[ke]]) break;
      int k;
      for (k = 1; k <= ke; k++) {
        std::uint8_t* st = ac_stats_[tbl] + 3 * (k - 1);
        encode(q, st, 0);
        while ((v = block[natural_order[k]]) == 0) {
          encode(q, st + 1, 0);
          st += 3;
          k++;
        }
        encode(q, st + 1, 1);
        if (v > 0) {
          encode(q, fixed_bin_, 0);
        } else {
          v = -v;
          encode(q, fixed_bin_, 1);
        }
        encode_ac_magnitude(q, st, tbl, k, v);
      }
      if (k <= DCTSIZE2 - 1) encode(q, ac_stats_[tbl] + 3 * (k - 1), 1);
    }
    save(q);
  }

  void dc_first(Block** MCU_data) {
    Coder q = load();
    for (int blkn = 0; blkn < cs_.blocks_in_MCU; blkn++) {
      const Coef* block = MCU_data[blkn]->data();
      int ci = cs_.MCU_membership[blkn];
      int tbl = cs_.cur_comp_info[ci]->dc_tbl_no;
      int m = static_cast<int>(block[0]) >> cs_.Al;
      int v = m - last_dc_val_[ci];
      if (v != 0) last_dc_val_[ci] = m;
      encode_dc(q, ci, tbl, v);
    }
    save(q);
  }

  void ac_first(Block** MCU_data) {
    Coder q = load();
    const Coef* block = MCU_data[0]->data();
    int tbl = cs_.cur_comp_info[0]->ac_tbl_no;
    int Al = cs_.Al;
    int ke, v;
    for (ke = cs_.Se; ke > 0; ke--) {
      if ((v = block[natural_order[ke]]) >= 0) {
        if (v >>= Al) break;
      } else {
        v = -v;
        if (v >>= Al) break;
      }
    }
    int k;
    for (k = cs_.Ss; k <= ke; k++) {
      std::uint8_t* st = ac_stats_[tbl] + 3 * (k - 1);
      encode(q, st, 0);
      for (;;) {
        if ((v = block[natural_order[k]]) >= 0) {
          if (v >>= Al) {
            encode(q, st + 1, 1);
            encode(q, fixed_bin_, 0);
            break;
          }
        } else {
          v = -v;
          if (v >>= Al) {
            encode(q, st + 1, 1);
            encode(q, fixed_bin_, 1);
            break;
          }
        }
        encode(q, st + 1, 0);
        st += 3;
        k++;
      }
      encode_ac_magnitude(q, st, tbl, k, v);
    }
    if (k <= cs_.Se) encode(q, ac_stats_[tbl] + 3 * (k - 1), 1);
    save(q);
  }

  void dc_refine(Block** MCU_data) {
    Coder q = load();
    int Al = cs_.Al;
    for (int blkn = 0; blkn < cs_.blocks_in_MCU; blkn++)
      encode(q, fixed_bin_, ((*MCU_data[blkn])[0] >> Al) & 1);
    save(q);
  }

  void ac_refine(Block** MCU_data) {
    Coder q = load();
    const Coef* block = MCU_data[0]->data();
    int tbl = cs_.cur_comp_info[0]->ac_tbl_no;
    int Al = cs_.Al, Ah = cs_.Ah;
    int ke, kex, v;
    for (ke = cs_.Se; ke > 0; ke--) {
      if ((v = block[natural_order[ke]]) >= 0) {
        if (v >>= Al) break;
      } else {
        v = -v;
        if (v >>= Al) break;
      }
    }
    for (kex = ke; kex > 0; kex--) {
      if ((v = block[natural_order[kex]]) >= 0) {
        if (v >>= Ah) break;
      } else {
        v = -v;
        if (v >>= Ah) break;
      }
    }
    int k;
    for (k = cs_.Ss; k <= ke; k++) {
      std::uint8_t* st = ac_stats_[tbl] + 3 * (k - 1);
      if (k > kex) encode(q, st, 0);
      for (;;) {
        if ((v = block[natural_order[k]]) >= 0) {
          if (v >>= Al) {
            if (v >> 1) {
              encode(q, st + 2, v & 1);
            } else {
              encode(q, st + 1, 1);
              encode(q, fixed_bin_, 0);
            }
            break;
          }
        } else {
          v = -v;
          if (v >>= Al) {
            if (v >> 1) {
              encode(q, st + 2, v & 1);
            } else {
              encode(q, st + 1, 1);
              encode(q, fixed_bin_, 1);
            }
            break;
          }
        }
        encode(q, st + 1, 0);
        st += 3;
        k++;
      }
    }
    if (k <= cs_.Se) encode(q, ac_stats_[tbl] + 3 * (k - 1), 1);
    save(q);
  }

  CState& cs_;
  Mode mode_ = SEQUENTIAL;
  JLong c_ = 0, a_ = 0, sc_ = 0, zc_ = 0;
  int ct_ = 0;
  int buffer_ = -1;
  int last_dc_val_[MAX_COMPS_IN_SCAN]{};
  int dc_context_[MAX_COMPS_IN_SCAN]{};
  unsigned restarts_to_go_ = 0;
  int next_restart_num_ = 0;
  std::uint8_t dc_stats_[NUM_ARITH_TBLS][DC_STAT_BINS]{};
  std::uint8_t ac_stats_[NUM_ARITH_TBLS][AC_STAT_BINS]{};
  std::uint8_t fixed_bin_[4]{};
};

}  // namespace vio::jpeg::detail
