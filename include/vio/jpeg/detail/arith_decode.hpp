// vio::jpeg - arithmetic entropy decoding.
#pragma once

#include "arith_table.hpp"
#include "huff_decode.hpp"  // init_coef_bits / update_progression

namespace vio::jpeg::detail {

class ArithDecoder final : public EntropyDecoder {
 public:
  explicit ArithDecoder(DState& st) : st_(st) {
    fixed_bin_[0] = 113;
    if (st.progressive_mode) init_coef_bits(st);
  }

  void start_pass() override {
    if (st_.progressive_mode) {
      bool bad = false;
      if (st_.Ss == 0) {
        if (st_.Se != 0) bad = true;
      } else {
        if (st_.Se < st_.Ss || st_.Se > DCTSIZE2 - 1) bad = true;
        if (st_.comps_in_scan != 1) bad = true;
      }
      if (st_.Ah != 0 && st_.Ah - 1 != st_.Al) bad = true;
      if (st_.Al > 13) bad = true;
      if (bad)
        VIO_FAIL("Invalid progressive/lossless parameters Ss=%d Se=%d Ah=%d Al=%d", st_.Ss,
                 st_.Se, st_.Ah, st_.Al);
      // Note: only warn about an AC scan without a prior DC scan when
      // Ss != 0, which is equivalent to the Huffman decoder's check.
      update_progression(st_, true);
      if (st_.Ah == 0) mode_ = st_.Ss == 0 ? DC_FIRST : AC_FIRST;
      else mode_ = st_.Ss == 0 ? DC_REFINE : AC_REFINE;
    } else {
      if (st_.Ss != 0 || st_.Se != DCTSIZE2 - 1 || st_.Ah != 0 || st_.Al != 0)
        st_.warn("Invalid SOS parameters for sequential JPEG");
      mode_ = SEQUENTIAL;
    }
    for (int ci = 0; ci < st_.comps_in_scan; ci++) {
      ComponentInfo* c = st_.cur_comp_info[ci];
      if (!st_.progressive_mode || (st_.Ss == 0 && st_.Ah == 0)) {
        int tbl = c->dc_tbl_no;
        if (tbl < 0 || tbl >= NUM_ARITH_TBLS)
          VIO_FAIL("Arithmetic table 0x%02x was not defined", tbl);
        std::memset(dc_stats_[tbl], 0, DC_STAT_BINS);
        last_dc_val_[ci] = 0;
        dc_context_[ci] = 0;
      }
      if (!st_.progressive_mode || st_.Ss) {
        int tbl = c->ac_tbl_no;
        if (tbl < 0 || tbl >= NUM_ARITH_TBLS)
          VIO_FAIL("Arithmetic table 0x%02x was not defined", tbl);
        std::memset(ac_stats_[tbl], 0, AC_STAT_BINS);
      }
    }
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
    for (int ci = 0; ci < st_.comps_in_scan; ci++) {
      ComponentInfo* c = st_.cur_comp_info[ci];
      if (!st_.progressive_mode || (st_.Ss == 0 && st_.Ah == 0)) {
        std::memset(dc_stats_[c->dc_tbl_no], 0, DC_STAT_BINS);
        last_dc_val_[ci] = 0;
        dc_context_[ci] = 0;
      }
      if (!st_.progressive_mode || st_.Ss) std::memset(ac_stats_[c->ac_tbl_no], 0, AC_STAT_BINS);
    }
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

  int decode(std::uint8_t* st) {
    while (a_ < 0x8000L) {
      if (--ct_ < 0) {
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
        c_ = (c_ << 8) | data;
        if ((ct_ += 8) < 0)
          if (++ct_ == 0) a_ = 0x8000L;
      }
      a_ <<= 1;
    }
    int sv = *st;
    JLong qe = aritab[sv & 0x7F];
    std::uint8_t nl = static_cast<std::uint8_t>(qe & 0xFF);
    qe >>= 8;
    std::uint8_t nm = static_cast<std::uint8_t>(qe & 0xFF);
    qe >>= 8;
    JLong temp = a_ - qe;
    a_ = temp;
    temp <<= ct_;
    if (c_ >= temp) {
      c_ -= temp;
      if (a_ < qe) {
        a_ = qe;
        *st = static_cast<std::uint8_t>((sv & 0x80) ^ nm);
      } else {
        a_ = qe;
        *st = static_cast<std::uint8_t>((sv & 0x80) ^ nl);
        sv ^= 0x80;
      }
    } else if (a_ < 0x8000L) {
      if (a_ < qe) {
        *st = static_cast<std::uint8_t>((sv & 0x80) ^ nl);
        sv ^= 0x80;
      } else {
        *st = static_cast<std::uint8_t>((sv & 0x80) ^ nm);
      }
    }
    return sv >> 7;
  }

  void bad_code() {
    st_.warn("Corrupt JPEG data: bad arithmetic code");
    ct_ = -1;
  }

  // Decode a DC difference for component ci; returns false on error.
  bool decode_dc(int ci, int tbl) {
    std::uint8_t* st = dc_stats_[tbl] + dc_context_[ci];
    if (decode(st) == 0) {
      dc_context_[ci] = 0;
    } else {
      int sign = decode(st + 1);
      st += 2;
      st += sign;
      int m;
      if ((m = decode(st)) != 0) {
        st = dc_stats_[tbl] + 20;
        while (decode(st)) {
          if ((m <<= 1) == 0x8000) {
            bad_code();
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
        if (decode(st)) v |= m;
      v += 1;
      if (sign) v = -v;
      last_dc_val_[ci] = (last_dc_val_[ci] + v) & 0xffff;
    }
    return true;
  }

  // Decode AC coefficients k in [kstart, kend]; returns false on error.
  bool decode_ac(Coef* block, int tbl, int kstart, int kend, int Al) {
    for (int k = kstart; k <= kend; k++) {
      std::uint8_t* st = ac_stats_[tbl] + 3 * (k - 1);
      if (decode(st)) break;
      while (decode(st + 1) == 0) {
        st += 3;
        k++;
        if (k > kend) {
          bad_code();
          return false;
        }
      }
      int sign = decode(fixed_bin_);
      st += 2;
      int m;
      if ((m = decode(st)) != 0) {
        if (decode(st)) {
          m <<= 1;
          st = ac_stats_[tbl] + (k <= st_.arith_ac_K[tbl] ? 189 : 217);
          while (decode(st)) {
            if ((m <<= 1) == 0x8000) {
              bad_code();
              return false;
            }
            st += 1;
          }
        }
      }
      int v = m;
      st += 14;
      while (m >>= 1)
        if (decode(st)) v |= m;
      v += 1;
      if (sign) v = -v;
      if (block) block[natural_order[k]] = static_cast<Coef>(static_cast<unsigned>(v) << Al);
    }
    return true;
  }

  void sequential(Block** MCU_data) {
    if (ct_ == -1) return;
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      Coef* block = MCU_data ? MCU_data[blkn]->data() : nullptr;
      int ci = st_.MCU_membership[blkn];
      ComponentInfo* c = st_.cur_comp_info[ci];
      if (!decode_dc(ci, c->dc_tbl_no)) return;
      if (block) block[0] = static_cast<Coef>(last_dc_val_[ci]);
      if (!decode_ac(block, c->ac_tbl_no, 1, DCTSIZE2 - 1, 0)) return;
    }
  }

  void dc_first(Block** MCU_data) {
    if (ct_ == -1) return;
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++) {
      Coef* block = MCU_data[blkn]->data();
      int ci = st_.MCU_membership[blkn];
      if (!decode_dc(ci, st_.cur_comp_info[ci]->dc_tbl_no)) return;
      block[0] = static_cast<Coef>(lshift(last_dc_val_[ci], st_.Al));
    }
  }

  void ac_first(Block** MCU_data) {
    if (ct_ == -1) return;
    decode_ac(MCU_data[0]->data(), st_.cur_comp_info[0]->ac_tbl_no, st_.Ss, st_.Se, st_.Al);
  }

  void dc_refine(Block** MCU_data) {
    int p1 = 1 << st_.Al;
    for (int blkn = 0; blkn < st_.blocks_in_MCU; blkn++)
      if (decode(fixed_bin_)) (*MCU_data[blkn])[0] = static_cast<Coef>((*MCU_data[blkn])[0] | p1);
  }

  void ac_refine(Block** MCU_data) {
    if (ct_ == -1) return;
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
        if (decode(st)) break;
      for (;;) {
        Coef* thiscoef = block + natural_order[k];
        if (*thiscoef) {
          if (decode(st + 2)) {
            if (*thiscoef < 0) *thiscoef = static_cast<Coef>(*thiscoef + m1);
            else *thiscoef = static_cast<Coef>(*thiscoef + p1);
          }
          break;
        }
        if (decode(st + 1)) {
          *thiscoef = static_cast<Coef>(decode(fixed_bin_) ? m1 : p1);
          break;
        }
        st += 3;
        k++;
        if (k > st_.Se) {
          bad_code();
          return;
        }
      }
    }
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
