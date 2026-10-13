// vio::jpeg - decompression coefficient controller and IDCT manager
// plus the lossless difference controller and undifferencer.
#pragma once

#include "decompress_state.hpp"
#include "idct.hpp"
#include "../simd/dispatch.hpp"

namespace vio::jpeg::detail {

// ---------------------------------------------------------------------------
// IDCT manager
template <class T>
class IdctManager {
 public:
  IdctManager(DState& st, const T* range_limit) : st_(st), range_limit_(range_limit) {
    for (int ci = 0; ci < MAX_COMPONENTS; ci++) cur_method_[ci] = -1;
    if constexpr (kBits<T> == 12)
      scaling_factor_ = (st.jpeg_data_precision == 8 && st.data_precision == 12) ? 16 : 1;
    if constexpr (kBits<T> == 8) {
      const simd::IdctKernels k = simd::idct_kernels(st.simd);
      if (k.islow) kernels.islow = k.islow;
      if (k.ifast) kernels.ifast = k.ifast;
      if (k.flt) kernels.flt = k.flt;
      if (k.s4) kernels.s4 = k.s4;
      if (k.s2) kernels.s2 = k.s2;
    }
  }

  // Optional per-size/method overrides (SIMD back ends install these).
  struct Kernels {
    IdctFn<T> islow = idct_int::idct_islow<T>;
    IdctFn<T> ifast = idct_fst::idct_ifast<T>;
    IdctFn<T> flt = idct_flt::idct_float<T>;
    IdctFn<T> s1 = idct_red::idct_1x1<T>;
    IdctFn<T> s2 = idct_red::idct_2x2<T>;
    IdctFn<T> s4 = idct_red::idct_4x4<T>;
  };
  Kernels kernels;

  void start_pass() {
    enum { ISLOW, IFAST, FLOAT };
    for (int ci = 0; ci < st_.num_components; ci++) {
      ComponentInfo& c = st_.comp_info[ci];
      int method = ISLOW;
      IdctFn<T> fn = nullptr;
      switch (c.DCT_scaled_size) {
        case 1: fn = kernels.s1; break;
        case 2: fn = kernels.s2; break;
        case 3: fn = idct_int::idct_3x3<T>; break;
        case 4: fn = kernels.s4; break;
        case 5: fn = idct_int::idct_5x5<T>; break;
        case 6: fn = idct_int::idct_6x6<T>; break;
        case 7: fn = idct_int::idct_7x7<T>; break;
        case 8:
          switch (st_.dct_method) {
            case DctMethod::IntSlow: fn = kernels.islow; method = ISLOW; break;
            case DctMethod::IntFast: fn = kernels.ifast; method = IFAST; break;
            case DctMethod::Float: fn = kernels.flt; method = FLOAT; break;
          }
          break;
        case 9: fn = idct_int::idct_9x9<T>; break;
        case 10: fn = idct_int::idct_10x10<T>; break;
        case 11: fn = idct_int::idct_11x11<T>; break;
        case 12: fn = idct_int::idct_12x12<T>; break;
        case 13: fn = idct_int::idct_13x13<T>; break;
        case 14: fn = idct_int::idct_14x14<T>; break;
        case 15: fn = idct_int::idct_15x15<T>; break;
        case 16: fn = idct_int::idct_16x16<T>; break;
        default: VIO_FAIL("IDCT output block size %d not supported", c.DCT_scaled_size);
      }
      fn_[ci] = fn;
      if (!c.component_needed || cur_method_[ci] == method) continue;
      if (!c.quant_table) continue;
      const QuantTable& q = *c.quant_table;
      cur_method_[ci] = method;
      switch (method) {
        case ISLOW:
          for (int i = 0; i < DCTSIZE2; i++) c.dct_table.islow[i] = q.quantval[i];
          for (int i = 0; i < DCTSIZE2; i++)
            c.dct_table16[i] = static_cast<std::int16_t>(q.quantval[i]);
          break;
        case IFAST: {
          static constexpr std::int16_t aanscales[DCTSIZE2] = {
              16384, 22725, 21407, 19266, 16384, 12873, 8867,  4520,  22725, 31521, 29692,
              26722, 22725, 17855, 12299, 6270,  21407, 29692, 27969, 25172, 21407, 16819,
              11585, 5906,  19266, 26722, 25172, 22654, 19266, 15137, 10426, 5315,  16384,
              22725, 21407, 19266, 16384, 12873, 8867,  4520,  12873, 17855, 16819, 15137,
              12873, 10114, 6967,  3552,  8867,  12299, 11585, 10426, 8867,  6967,  4799,
              2446,  4520,  6270,  5906,  5315,  4520,  3552,  2446,  1247};
          constexpr int shift = 14 - kIfastScaleBits<T>;
          for (int i = 0; i < DCTSIZE2; i++) {
            JLong v = (static_cast<JLong>(q.quantval[i]) * aanscales[i] + (JLong(1) << (shift - 1))) >>
                      shift;
            if constexpr (kBits<T> == 8) {
              c.dct_table.islow[i] = static_cast<std::int32_t>(v);
              c.dct_table16[i] = static_cast<std::int16_t>(v);
            } else {
              c.dct_table.ifast_wide[i] = v;
            }
          }
          break;
        }
        case FLOAT: {
          static constexpr double aanscalefactor[DCTSIZE] = {
              1.0, 1.387039845, 1.306562965, 1.175875602, 1.0, 0.785694958, 0.541196100,
              0.275899379};
          int i = 0;
          for (int row = 0; row < DCTSIZE; row++)
            for (int col = 0; col < DCTSIZE; col++, i++)
              c.dct_table.flt[i] = static_cast<float>(static_cast<double>(q.quantval[i]) *
                                                      aanscalefactor[row] * aanscalefactor[col]);
          break;
        }
      }
    }
  }

  VIO_INLINE void inverse_dct(int ci, const Coef* block, T** out, unsigned col) const {
    fn_[ci](&st_.comp_info[ci].dct_table, block, out, col, range_limit_, scaling_factor_);
  }

 private:
  DState& st_;
  const T* range_limit_;
  int scaling_factor_ = 1;
  IdctFn<T> fn_[MAX_COMPONENTS]{};

 public:
  // Per-component kernel selected by start_pass() (may be overridden,
  // e.g. for scaled decompression to YUV).
  IdctFn<T> kernel(int ci) const { return fn_[ci]; }
  void set_kernel(int ci, IdctFn<T> f) { fn_[ci] = f; }

 private:
  int cur_method_[MAX_COMPONENTS];
};

// Output side of a coefficient/difference controller: produces one iMCU row
// of component samples.
template <class T>
class DCoefController : public CoefControllerBase {
 public:
  virtual int decompress_data(T*** output_buf) = 0;
  // Discards the next iMCU row without producing output (skip_scanlines).
  virtual void skip_imcu_row() {}
};

// ---------------------------------------------------------------------------
// Lossy coefficient controller
template <class T>
class CoefController final : public DCoefController<T> {
 public:
  CoefController(DState& st, IdctManager<T>* idct, bool need_full_buffer)
      : st_(st), idct_(idct) {
    if (need_full_buffer) {
      whole_image_.resize(st.num_components);
      for (int ci = 0; ci < st.num_components; ci++) {
        ComponentInfo& c = st.comp_info[ci];
        whole_image_[ci].allocate(
            static_cast<std::size_t>(round_up(c.width_in_blocks, c.h_samp_factor)),
            static_cast<std::size_t>(round_up(c.height_in_blocks, c.v_samp_factor)));
      }
      this->coef_arrays = whole_image_.data();
      multi_ = true;
    } else {
      mcu_blocks_ = aligned_alloc_zeroed<Block>(D_MAX_BLOCKS_IN_MCU);
      for (int i = 0; i < D_MAX_BLOCKS_IN_MCU; i++) MCU_buffer_[i] = &mcu_blocks_[i];
      this->coef_arrays = nullptr;
      multi_ = false;
    }
    mode_ = multi_ ? MULTI : ONEPASS;
  }

  void start_input_pass() override {
    st_.input_iMCU_row = 0;
    start_iMCU_row();
  }

  void start_output_pass() override {
    if (multi_) mode_ = (st_.do_block_smoothing && smoothing_ok()) ? SMOOTH : MULTI;
    st_.output_iMCU_row = 0;
  }

  int consume_data() override {
    if (!multi_) return SUSPENDED;  // dummy_consume_data
    BlockArray* buffer[MAX_COMPS_IN_SCAN];
    for (int ci = 0; ci < st_.comps_in_scan; ci++)
      buffer[ci] = &whole_image_[st_.cur_comp_info[ci]->component_index];
    for (int yoffset = MCU_vert_offset_; yoffset < this->MCU_rows_per_iMCU_row; yoffset++) {
      if (st_.comps_in_scan == 1 && !st_.entropy->insufficient_data) {
        // One block per MCU, consecutive in the row.
        ComponentInfo* c = st_.cur_comp_info[0];
        st_.last_good_iMCU_row = st_.input_iMCU_row;
        Block* row = buffer[0]->row(st_.input_iMCU_row * c->v_samp_factor + yoffset);
        if (st_.entropy->decode_blocks(row + MCU_ctr_, st_.MCUs_per_row - MCU_ctr_)) {
          MCU_ctr_ = 0;
          continue;
        }
      }
      for (unsigned MCU_col_num = MCU_ctr_; MCU_col_num < st_.MCUs_per_row; MCU_col_num++) {
        int blkn = 0;
        for (int ci = 0; ci < st_.comps_in_scan; ci++) {
          ComponentInfo* c = st_.cur_comp_info[ci];
          unsigned start_col = MCU_col_num * c->MCU_width;
          for (int yindex = 0; yindex < c->MCU_height; yindex++) {
            Block* buffer_ptr =
                buffer[ci]->row(st_.input_iMCU_row * c->v_samp_factor + yindex + yoffset) +
                start_col;
            for (int xindex = 0; xindex < c->MCU_width; xindex++)
              MCU_buffer_[blkn++] = buffer_ptr++;
          }
        }
        if (!st_.entropy->insufficient_data) st_.last_good_iMCU_row = st_.input_iMCU_row;
        st_.entropy->decode_mcu(MCU_buffer_);
      }
      MCU_ctr_ = 0;
    }
    if (++st_.input_iMCU_row < st_.total_iMCU_rows) {
      start_iMCU_row();
      return ROW_COMPLETED;
    }
    st_.finish_input_pass();
    return SCAN_COMPLETED;
  }

  int decompress_data(T*** output_buf) override {
    switch (mode_) {
      case ONEPASS: return decompress_onepass(output_buf);
      case MULTI: return decompress_multi(output_buf);
      default: return decompress_smooth(output_buf);
    }
  }

  // Used by skip_scanlines for single-scan images: decode and discard.
  void skip_imcu_row() override {
    for (int y = 0; y < this->MCU_rows_per_iMCU_row; y++)
      for (unsigned x = 0; x < st_.MCUs_per_row; x++) {
        if (!st_.entropy->insufficient_data) st_.last_good_iMCU_row = st_.input_iMCU_row;
        st_.entropy->decode_mcu(nullptr);
      }
    st_.input_iMCU_row++;
    st_.output_iMCU_row++;
    if (st_.input_iMCU_row < st_.total_iMCU_rows) start_iMCU_row();
    else st_.finish_input_pass();
  }

  void start_iMCU_row() {
    if (st_.comps_in_scan > 1) {
      this->MCU_rows_per_iMCU_row = 1;
    } else {
      if (st_.input_iMCU_row < st_.total_iMCU_rows - 1)
        this->MCU_rows_per_iMCU_row = st_.cur_comp_info[0]->v_samp_factor;
      else
        this->MCU_rows_per_iMCU_row = st_.cur_comp_info[0]->last_row_height;
    }
    MCU_ctr_ = 0;
    MCU_vert_offset_ = 0;
  }

 private:
  enum Mode { ONEPASS, MULTI, SMOOTH };

  int decompress_onepass(T*** output_buf) {
    unsigned last_MCU_col = st_.MCUs_per_row - 1;
    unsigned last_iMCU_row = st_.total_iMCU_rows - 1;
    for (int yoffset = MCU_vert_offset_; yoffset < this->MCU_rows_per_iMCU_row; yoffset++) {
      for (unsigned MCU_col_num = MCU_ctr_; MCU_col_num <= last_MCU_col; MCU_col_num++) {
        std::memset(mcu_blocks_.get(), 0, sizeof(Block) * st_.blocks_in_MCU);
        if (!st_.entropy->insufficient_data) st_.last_good_iMCU_row = st_.input_iMCU_row;
        st_.entropy->decode_mcu(MCU_buffer_);
        if (MCU_col_num >= st_.first_iMCU_col && MCU_col_num <= st_.last_iMCU_col) {
          int blkn = 0;
          for (int ci = 0; ci < st_.comps_in_scan; ci++) {
            ComponentInfo* c = st_.cur_comp_info[ci];
            if (!c->component_needed) {
              blkn += c->MCU_blocks;
              continue;
            }
            int useful_width = (MCU_col_num < last_MCU_col) ? c->MCU_width : c->last_col_width;
            T** output_ptr = output_buf[c->component_index] + yoffset * c->DCT_scaled_size;
            unsigned start_col = (MCU_col_num - st_.first_iMCU_col) * c->MCU_sample_width;
            for (int yindex = 0; yindex < c->MCU_height; yindex++) {
              if (st_.input_iMCU_row < last_iMCU_row || yoffset + yindex < c->last_row_height) {
                unsigned output_col = start_col;
                for (int xindex = 0; xindex < useful_width; xindex++) {
                  idct_->inverse_dct(c->component_index, MCU_buffer_[blkn + xindex]->data(),
                                     output_ptr, output_col);
                  output_col += c->DCT_scaled_size;
                }
              }
              blkn += c->MCU_width;
              output_ptr += c->DCT_scaled_size;
            }
          }
        }
      }
      MCU_ctr_ = 0;
    }
    st_.output_iMCU_row++;
    if (++st_.input_iMCU_row < st_.total_iMCU_rows) {
      start_iMCU_row();
      return ROW_COMPLETED;
    }
    st_.finish_input_pass();
    return SCAN_COMPLETED;
  }

  int decompress_multi(T*** output_buf) {
    unsigned last_iMCU_row = st_.total_iMCU_rows - 1;
    while (st_.input_scan_number < st_.output_scan_number ||
           (st_.input_scan_number == st_.output_scan_number &&
            st_.input_iMCU_row <= st_.output_iMCU_row)) {
      if (st_.consume_input() == SUSPENDED) return SUSPENDED;
    }
    for (int ci = 0; ci < st_.num_components; ci++) {
      ComponentInfo& c = st_.comp_info[ci];
      if (!c.component_needed) continue;
      int block_rows;
      if (st_.output_iMCU_row < last_iMCU_row) {
        block_rows = c.v_samp_factor;
      } else {
        block_rows = static_cast<int>(c.height_in_blocks % c.v_samp_factor);
        if (block_rows == 0) block_rows = c.v_samp_factor;
      }
      T** output_ptr = output_buf[ci];
      for (int block_row = 0; block_row < block_rows; block_row++) {
        Block* buffer_ptr =
            whole_image_[ci].row(st_.output_iMCU_row * c.v_samp_factor + block_row) +
            st_.first_MCU_col[ci];
        unsigned output_col = 0;
        for (unsigned block_num = st_.first_MCU_col[ci]; block_num <= st_.last_MCU_col[ci];
             block_num++) {
          idct_->inverse_dct(ci, buffer_ptr->data(), output_ptr, output_col);
          buffer_ptr++;
          output_col += c.DCT_scaled_size;
        }
        output_ptr += c.DCT_scaled_size;
      }
    }
    if (++st_.output_iMCU_row < st_.total_iMCU_rows) return ROW_COMPLETED;
    return SCAN_COMPLETED;
  }

  static constexpr int SAVED_COEFS = 10;
  static constexpr int Q01_POS = 1, Q10_POS = 8, Q20_POS = 16, Q11_POS = 9, Q02_POS = 2,
                       Q03_POS = 3, Q12_POS = 10, Q21_POS = 17, Q30_POS = 24;

  bool smoothing_ok() {
    bool smoothing_useful = false;
    if (!st_.progressive_mode || !st_.coef_bits_valid) return false;
    coef_bits_latch_.assign(static_cast<std::size_t>(st_.num_components) * 2 * SAVED_COEFS, 0);
    int* coef_bits_latch = coef_bits_latch_.data();
    int* prev_coef_bits_latch = coef_bits_latch_.data() + st_.num_components * SAVED_COEFS;
    for (int ci = 0; ci < st_.num_components; ci++) {
      ComponentInfo& c = st_.comp_info[ci];
      if (!c.quant_table) return false;
      const auto& qv = c.quant_table->quantval;
      if (qv[0] == 0 || qv[Q01_POS] == 0 || qv[Q10_POS] == 0 || qv[Q20_POS] == 0 ||
          qv[Q11_POS] == 0 || qv[Q02_POS] == 0 || qv[Q03_POS] == 0 || qv[Q12_POS] == 0 ||
          qv[Q21_POS] == 0 || qv[Q30_POS] == 0)
        return false;
      const int* coef_bits = st_.coef_bits[ci].data();
      const int* prev_coef_bits = st_.coef_bits[ci + st_.num_components].data();
      if (coef_bits[0] < 0) return false;
      coef_bits_latch[0] = coef_bits[0];
      for (int coefi = 1; coefi < SAVED_COEFS; coefi++) {
        if (st_.input_scan_number > 1) prev_coef_bits_latch[coefi] = prev_coef_bits[coefi];
        else prev_coef_bits_latch[coefi] = -1;
        coef_bits_latch[coefi] = coef_bits[coefi];
        if (coef_bits[coefi] != 0) smoothing_useful = true;
      }
      coef_bits_latch += SAVED_COEFS;
      prev_coef_bits_latch += SAVED_COEFS;
    }
    return smoothing_useful;
  }

  static int predict(JLong num, JLong Q, int Al) {
    int pred;
    if (num >= 0) {
      pred = static_cast<int>(((Q << 7) + num) / (Q << 8));
      if (Al > 0 && pred >= (1 << Al)) pred = (1 << Al) - 1;
    } else {
      pred = static_cast<int>(((Q << 7) - num) / (Q << 8));
      if (Al > 0 && pred >= (1 << Al)) pred = (1 << Al) - 1;
      pred = -pred;
    }
    return pred;
  }

  int decompress_smooth(T*** output_buf) {
    unsigned last_iMCU_row = st_.total_iMCU_rows - 1;
    while (st_.input_scan_number <= st_.output_scan_number && !st_.eoi_reached) {
      if (st_.input_scan_number == st_.output_scan_number) {
        unsigned delta = (st_.Ss == 0) ? 2 : 0;
        if (st_.input_iMCU_row > st_.output_iMCU_row + delta) break;
      }
      if (st_.consume_input() == SUSPENDED) return SUSPENDED;
    }
    Coef* workspace = workspace_.data();
    for (int ci = 0; ci < st_.num_components; ci++) {
      ComponentInfo& c = st_.comp_info[ci];
      if (!c.component_needed) continue;
      int block_rows;
      if (st_.output_iMCU_row + 1 < last_iMCU_row) {
        block_rows = c.v_samp_factor;
      } else if (st_.output_iMCU_row < last_iMCU_row) {
        block_rows = c.v_samp_factor;
      } else {
        block_rows = static_cast<int>(c.height_in_blocks % c.v_samp_factor);
        if (block_rows == 0) block_rows = c.v_samp_factor;
      }
      // buffer[r] == row (output_iMCU_row * v_samp + r) of the whole image
      BlockArray& whole = whole_image_[ci];
      long base_row = static_cast<long>(st_.output_iMCU_row) * c.v_samp_factor;
      auto buffer = [&](long r) { return whole.row(static_cast<std::size_t>(base_row + r)); };

      const int* coef_bits;
      if (st_.output_iMCU_row > st_.last_good_iMCU_row)
        coef_bits = coef_bits_latch_.data() + (ci + st_.num_components) * SAVED_COEFS;
      else
        coef_bits = coef_bits_latch_.data() + ci * SAVED_COEFS;

      bool change_dc = coef_bits[1] == -1 && coef_bits[2] == -1 && coef_bits[3] == -1 &&
                       coef_bits[4] == -1 && coef_bits[5] == -1 && coef_bits[6] == -1 &&
                       coef_bits[7] == -1 && coef_bits[8] == -1 && coef_bits[9] == -1;

      const auto& qv = c.quant_table->quantval;
      JLong Q00 = qv[0], Q01 = qv[Q01_POS], Q10 = qv[Q10_POS], Q20 = qv[Q20_POS],
            Q11 = qv[Q11_POS], Q02 = qv[Q02_POS];
      JLong Q03 = 0, Q12 = 0, Q21 = 0, Q30 = 0;
      if (change_dc) {
        Q03 = qv[Q03_POS];
        Q12 = qv[Q12_POS];
        Q21 = qv[Q21_POS];
        Q30 = qv[Q30_POS];
      }
      T** output_ptr = output_buf[ci];
      long image_block_rows = static_cast<long>(block_rows) * st_.total_iMCU_rows;
      for (int block_row = 0; block_row < block_rows; block_row++) {
        long image_block_row = static_cast<long>(st_.output_iMCU_row) * block_rows + block_row;
        unsigned first = st_.first_MCU_col[ci];
        const Block* buffer_ptr = buffer(block_row) + first;
        const Block* prev_block_row =
            image_block_row > 0 ? buffer(block_row - 1) + first : buffer_ptr;
        const Block* prev_prev_block_row =
            image_block_row > 1 ? buffer(block_row - 2) + first : prev_block_row;
        const Block* next_block_row =
            image_block_row < image_block_rows - 1 ? buffer(block_row + 1) + first : buffer_ptr;
        const Block* next_next_block_row = image_block_row < image_block_rows - 2
                                               ? buffer(block_row + 2) + first
                                               : next_block_row;
        int DC01, DC02, DC03, DC04, DC05, DC06, DC07, DC08, DC09, DC10, DC11, DC12, DC13,
            DC14, DC15, DC16, DC17, DC18, DC19, DC20, DC21, DC22, DC23, DC24, DC25;
        DC01 = DC02 = DC03 = DC04 = DC05 = prev_prev_block_row[0][0];
        DC06 = DC07 = DC08 = DC09 = DC10 = prev_block_row[0][0];
        DC11 = DC12 = DC13 = DC14 = DC15 = buffer_ptr[0][0];
        DC16 = DC17 = DC18 = DC19 = DC20 = next_block_row[0][0];
        DC21 = DC22 = DC23 = DC24 = DC25 = next_next_block_row[0][0];
        unsigned output_col = 0;
        unsigned last_block_column = c.width_in_blocks - 1;
        for (unsigned block_num = first; block_num <= st_.last_MCU_col[ci]; block_num++) {
          std::memcpy(workspace, buffer_ptr->data(), sizeof(Block));
          if (block_num == first && block_num < last_block_column) {
            DC04 = DC05 = prev_prev_block_row[1][0];
            DC09 = DC10 = prev_block_row[1][0];
            DC14 = DC15 = buffer_ptr[1][0];
            DC19 = DC20 = next_block_row[1][0];
            DC24 = DC25 = next_next_block_row[1][0];
          }
          if (block_num + 1 < last_block_column) {
            DC05 = prev_prev_block_row[2][0];
            DC10 = prev_block_row[2][0];
            DC15 = buffer_ptr[2][0];
            DC20 = next_block_row[2][0];
            DC25 = next_next_block_row[2][0];
          }
          int Al;
          JLong num;
          if ((Al = coef_bits[1]) != 0 && workspace[1] == 0) {
            num = Q00 * (change_dc ? (-DC01 - DC02 + DC04 + DC05 - 3 * DC06 + 13 * DC07 -
                                      13 * DC09 + 3 * DC10 - 3 * DC11 + 38 * DC12 - 38 * DC14 +
                                      3 * DC15 - 3 * DC16 + 13 * DC17 - 13 * DC19 + 3 * DC20 -
                                      DC21 - DC22 + DC24 + DC25)
                                   : (-7 * DC11 + 50 * DC12 - 50 * DC14 + 7 * DC15));
            workspace[1] = static_cast<Coef>(predict(num, Q01, Al));
          }
          if ((Al = coef_bits[2]) != 0 && workspace[8] == 0) {
            num = Q00 * (change_dc ? (-DC01 - 3 * DC02 - 3 * DC03 - 3 * DC04 - DC05 - DC06 +
                                      13 * DC07 + 38 * DC08 + 13 * DC09 - DC10 + DC16 -
                                      13 * DC17 - 38 * DC18 - 13 * DC19 + DC20 + DC21 +
                                      3 * DC22 + 3 * DC23 + 3 * DC24 + DC25)
                                   : (-7 * DC03 + 50 * DC08 - 50 * DC18 + 7 * DC23));
            workspace[8] = static_cast<Coef>(predict(num, Q10, Al));
          }
          if ((Al = coef_bits[3]) != 0 && workspace[16] == 0) {
            num = Q00 * (change_dc ? (DC03 + 2 * DC07 + 7 * DC08 + 2 * DC09 - 5 * DC12 -
                                      14 * DC13 - 5 * DC14 + 2 * DC17 + 7 * DC18 + 2 * DC19 +
                                      DC23)
                                   : (-DC03 + 13 * DC08 - 24 * DC13 + 13 * DC18 - DC23));
            workspace[16] = static_cast<Coef>(predict(num, Q20, Al));
          }
          if ((Al = coef_bits[4]) != 0 && workspace[9] == 0) {
            num = Q00 * (change_dc ? (-DC01 + DC05 + 9 * DC07 - 9 * DC09 - 9 * DC17 +
                                      9 * DC19 + DC21 - DC25)
                                   : (DC10 + DC16 - 10 * DC17 + 10 * DC19 - DC02 - DC20 +
                                      DC22 - DC24 + DC04 - DC06 + 10 * DC07 - 10 * DC09));
            workspace[9] = static_cast<Coef>(predict(num, Q11, Al));
          }
          if ((Al = coef_bits[5]) != 0 && workspace[2] == 0) {
            num = Q00 * (change_dc ? (2 * DC07 - 5 * DC08 + 2 * DC09 + DC11 + 7 * DC12 -
                                      14 * DC13 + 7 * DC14 + DC15 + 2 * DC17 - 5 * DC18 +
                                      2 * DC19)
                                   : (-DC11 + 13 * DC12 - 24 * DC13 + 13 * DC14 - DC15));
            workspace[2] = static_cast<Coef>(predict(num, Q02, Al));
          }
          if (change_dc) {
            if ((Al = coef_bits[6]) != 0 && workspace[3] == 0) {
              num = Q00 * (DC07 - DC09 + 2 * DC12 - 2 * DC14 + DC17 - DC19);
              workspace[3] = static_cast<Coef>(predict(num, Q03, Al));
            }
            if ((Al = coef_bits[7]) != 0 && workspace[10] == 0) {
              num = Q00 * (DC07 - 3 * DC08 + DC09 - DC17 + 3 * DC18 - DC19);
              workspace[10] = static_cast<Coef>(predict(num, Q12, Al));
            }
            if ((Al = coef_bits[8]) != 0 && workspace[17] == 0) {
              num = Q00 * (DC07 - DC09 - 3 * DC12 + 3 * DC14 + DC17 - DC19);
              workspace[17] = static_cast<Coef>(predict(num, Q21, Al));
            }
            if ((Al = coef_bits[9]) != 0 && workspace[24] == 0) {
              num = Q00 * (DC07 + 2 * DC08 + DC09 - DC17 - 2 * DC18 - DC19);
              workspace[24] = static_cast<Coef>(predict(num, Q30, Al));
            }
            num = Q00 * (-2 * DC01 - 6 * DC02 - 8 * DC03 - 6 * DC04 - 2 * DC05 - 6 * DC06 +
                         6 * DC07 + 42 * DC08 + 6 * DC09 - 6 * DC10 - 8 * DC11 + 42 * DC12 +
                         152 * DC13 + 42 * DC14 - 8 * DC15 - 6 * DC16 + 6 * DC17 + 42 * DC18 +
                         6 * DC19 - 6 * DC20 - 2 * DC21 - 6 * DC22 - 8 * DC23 - 6 * DC24 -
                         2 * DC25);
            workspace[0] = static_cast<Coef>(predict(num, Q00, 0));
          }
          idct_->inverse_dct(ci, workspace, output_ptr, output_col);
          DC01 = DC02; DC02 = DC03; DC03 = DC04; DC04 = DC05;
          DC06 = DC07; DC07 = DC08; DC08 = DC09; DC09 = DC10;
          DC11 = DC12; DC12 = DC13; DC13 = DC14; DC14 = DC15;
          DC16 = DC17; DC17 = DC18; DC18 = DC19; DC19 = DC20;
          DC21 = DC22; DC22 = DC23; DC23 = DC24; DC24 = DC25;
          buffer_ptr++; prev_block_row++; next_block_row++;
          prev_prev_block_row++; next_next_block_row++;
          output_col += c.DCT_scaled_size;
        }
        output_ptr += c.DCT_scaled_size;
      }
    }
    if (++st_.output_iMCU_row < st_.total_iMCU_rows) return ROW_COMPLETED;
    return SCAN_COMPLETED;
  }

  DState& st_;
  IdctManager<T>* idct_;
  bool multi_ = false;
  Mode mode_ = ONEPASS;
  unsigned MCU_ctr_ = 0;
  int MCU_vert_offset_ = 0;
  Block* MCU_buffer_[D_MAX_BLOCKS_IN_MCU]{};
  AlignedPtr<Block> mcu_blocks_;
  std::vector<BlockArray> whole_image_;
  std::vector<int> coef_bits_latch_;
  alignas(32) std::array<Coef, DCTSIZE2> workspace_{};
};

// ---------------------------------------------------------------------------
// Lossless undifferencer + point-transform scaler
template <class T>
class LosslessDecompressor {
 public:
  explicit LosslessDecompressor(DState& st) : st_(st) {}

  void start_pass() {
    if (st_.Ss < 1 || st_.Ss > 7 || st_.Se != 0 || st_.Ah != 0 || st_.Al < 0 ||
        st_.Al >= st_.data_precision)
      VIO_FAIL("Invalid progressive/lossless parameters Ss=%d Se=%d Ah=%d Al=%d", st_.Ss, st_.Se,
               st_.Ah, st_.Al);
    for (int ci = 0; ci < st_.num_components; ci++) predictor_[ci] = 0;  // first row
  }

  void undifference(int ci, const int* diff_buf, const int* prev_row, int* undiff_buf,
                    unsigned width) {
    int psv = predictor_[ci];
    if (psv == 0) {
      int initial = 1 << (st_.data_precision - st_.Al - 1);
      undiff_1d(diff_buf, undiff_buf, width, initial);
      predictor_[ci] = st_.Ss;
      return;
    }
    switch (psv) {
      case 1: undiff_1d(diff_buf, undiff_buf, width, prev_row[0]); break;
      case 2: undiff_2d<2>(diff_buf, prev_row, undiff_buf, width); break;
      case 3: undiff_2d<3>(diff_buf, prev_row, undiff_buf, width); break;
      case 4: undiff_2d<4>(diff_buf, prev_row, undiff_buf, width); break;
      case 5: undiff_2d<5>(diff_buf, prev_row, undiff_buf, width); break;
      case 6: undiff_2d<6>(diff_buf, prev_row, undiff_buf, width); break;
      case 7: undiff_2d<7>(diff_buf, prev_row, undiff_buf, width); break;
      default:
        VIO_FAIL("Invalid progressive/lossless parameters Ss=%d Se=%d Ah=%d Al=%d", st_.Ss,
                 st_.Se, st_.Ah, st_.Al);
    }
  }

  void scale(const int* diff_buf, T* output_buf, unsigned width) const {
    int Al = st_.Al;
    if (Al) {
      for (unsigned i = 0; i < width; i++) output_buf[i] = static_cast<T>(diff_buf[i] << Al);
    } else {
      for (unsigned i = 0; i < width; i++) output_buf[i] = static_cast<T>(diff_buf[i]);
    }
  }

 private:
  static void undiff_1d(const int* diff_buf, int* undiff_buf, unsigned width, int initial) {
    // Sums modulo 2^16: the running sum wraps modulo 2^32 and only the
    // stored values are masked, which keeps the mask off the dependency chain.
    unsigned Ra = static_cast<unsigned>(initial);
    for (unsigned i = 0; i < width; i++) {
      Ra += static_cast<unsigned>(diff_buf[i]);
      undiff_buf[i] = static_cast<int>(Ra & 0xFFFF);
    }
  }

  template <int P>
  static void undiff_2d(const int* diff_buf, const int* prev_row, int* undiff_buf,
                        unsigned width) {
    int Ra, Rb, Rc = 0;
    Rb = *prev_row++;
    Ra = (*diff_buf++ + Rb) & 0xFFFF;  // PREDICTOR2 for the first column
    *undiff_buf++ = Ra;
    while (--width) {
      Rc = Rb;
      Rb = *prev_row++;
      int pred;
      if constexpr (P == 2) pred = Rb;
      else if constexpr (P == 3) pred = Rc;
      else if constexpr (P == 4) pred = static_cast<int>(JLong(Ra) + JLong(Rb) - JLong(Rc));
      else if constexpr (P == 5) pred = static_cast<int>(JLong(Ra) + ((JLong(Rb) - JLong(Rc)) >> 1));
      else if constexpr (P == 6) pred = static_cast<int>(JLong(Rb) + ((JLong(Ra) - JLong(Rc)) >> 1));
      else pred = static_cast<int>((JLong(Ra) + JLong(Rb)) >> 1);
      Ra = (*diff_buf++ + pred) & 0xFFFF;
      *undiff_buf++ = Ra;
    }
  }

  DState& st_;
  int predictor_[MAX_COMPONENTS]{};
};

// ---------------------------------------------------------------------------
// Lossless difference controller
template <class T>
class DiffController final : public DCoefController<T> {
 public:
  DiffController(DState& st, LosslessDecompressor<T>* ld, bool need_full_buffer)
      : st_(st), ld_(ld) {
    diff_buf_.resize(st.num_components);
    undiff_buf_.resize(st.num_components);
    diff_rows_.resize(st.num_components);
    for (int ci = 0; ci < st.num_components; ci++) {
      ComponentInfo& c = st.comp_info[ci];
      std::size_t w = static_cast<std::size_t>(round_up(c.width_in_blocks, c.h_samp_factor));
      diff_buf_[ci].allocate(w, c.v_samp_factor);
      undiff_buf_[ci].allocate(w, c.v_samp_factor);
      diff_rows_[ci] = diff_buf_[ci].rows();
    }
    if (need_full_buffer) {
      whole_image_.resize(st.num_components);
      for (int ci = 0; ci < st.num_components; ci++) {
        ComponentInfo& c = st.comp_info[ci];
        whole_image_[ci].allocate(
            static_cast<std::size_t>(round_up(c.width_in_blocks, c.h_samp_factor)),
            static_cast<std::size_t>(round_up(c.height_in_blocks, c.v_samp_factor)));
      }
      multi_ = true;
    }
  }

  void start_input_pass() override {
    ld_->start_pass();
    if (st_.restart_interval % st_.MCUs_per_row != 0)
      VIO_FAIL("Invalid restart interval %d; must be an integer multiple of the number of MCUs "
               "in an MCU row (%d)",
               st_.restart_interval, st_.MCUs_per_row);
    restart_rows_to_go_ = st_.restart_interval / st_.MCUs_per_row;
    st_.input_iMCU_row = 0;
    start_iMCU_row();
  }

  void start_output_pass() override { st_.output_iMCU_row = 0; }

  int consume_data() override {
    if (!multi_) return SUSPENDED;
    T** buffer[MAX_COMPONENTS];
    for (int ci = 0; ci < st_.comps_in_scan; ci++) {
      ComponentInfo* c = st_.cur_comp_info[ci];
      int compi = c->component_index;
      buffer[compi] = whole_image_[compi].rows() + st_.input_iMCU_row * c->v_samp_factor;
    }
    return decompress_into(buffer);
  }

  int decompress_data(T*** output_buf) override {
    if (!multi_) return decompress_into(output_buf);
    // output_data()
    unsigned last_iMCU_row = st_.total_iMCU_rows - 1;
    while (st_.input_scan_number < st_.output_scan_number ||
           (st_.input_scan_number == st_.output_scan_number &&
            st_.input_iMCU_row <= st_.output_iMCU_row)) {
      if (st_.consume_input() == SUSPENDED) return SUSPENDED;
    }
    for (int ci = 0; ci < st_.num_components; ci++) {
      ComponentInfo& c = st_.comp_info[ci];
      T** buffer = whole_image_[ci].rows() + st_.output_iMCU_row * c.v_samp_factor;
      int samp_rows;
      if (st_.output_iMCU_row < last_iMCU_row) {
        samp_rows = c.v_samp_factor;
      } else {
        samp_rows = static_cast<int>(c.height_in_blocks % c.v_samp_factor);
        if (samp_rows == 0) samp_rows = c.v_samp_factor;
      }
      for (int row = 0; row < samp_rows; row++)
        std::memcpy(output_buf[ci][row], buffer[row], c.width_in_blocks * sizeof(T));
    }
    if (++st_.output_iMCU_row < st_.total_iMCU_rows) return ROW_COMPLETED;
    return SCAN_COMPLETED;
  }

  void start_iMCU_row() {
    if (st_.comps_in_scan > 1) {
      this->MCU_rows_per_iMCU_row = 1;
    } else {
      if (st_.input_iMCU_row < st_.total_iMCU_rows - 1)
        this->MCU_rows_per_iMCU_row = st_.cur_comp_info[0]->v_samp_factor;
      else
        this->MCU_rows_per_iMCU_row = st_.cur_comp_info[0]->last_row_height;
    }
    MCU_ctr_ = 0;
    MCU_vert_offset_ = 0;
  }

 private:
  int decompress_into(T*** output_buf) {
    unsigned last_iMCU_row = st_.total_iMCU_rows - 1;
    for (unsigned yoffset = MCU_vert_offset_;
         yoffset < static_cast<unsigned>(this->MCU_rows_per_iMCU_row); yoffset++) {
      if (st_.restart_interval) {
        if (restart_rows_to_go_ == 0) {
          st_.entropy->process_restart();
          ld_->start_pass();
          restart_rows_to_go_ = st_.restart_interval / st_.MCUs_per_row;
        }
      }
      unsigned MCU_col_num = MCU_ctr_;
      st_.entropy->decode_mcus(diff_rows_.data(), yoffset, MCU_col_num,
                               st_.MCUs_per_row - MCU_col_num);
      if (st_.restart_interval) restart_rows_to_go_--;
      MCU_ctr_ = 0;
    }
    for (int ci = 0; ci < st_.comps_in_scan; ci++) {
      ComponentInfo* c = st_.cur_comp_info[ci];
      int compi = c->component_index;
      int nrows = st_.input_iMCU_row == last_iMCU_row ? c->last_row_height : c->v_samp_factor;
      for (int row = 0, prev_row = c->v_samp_factor - 1; row < nrows; prev_row = row, row++) {
        ld_->undifference(compi, diff_buf_[compi][row], undiff_buf_[compi][prev_row],
                          undiff_buf_[compi][row], c->width_in_blocks);
        ld_->scale(undiff_buf_[compi][row], output_buf[compi][row], c->width_in_blocks);
      }
    }
    if (++st_.input_iMCU_row < st_.total_iMCU_rows) {
      start_iMCU_row();
      return ROW_COMPLETED;
    }
    st_.finish_input_pass();
    return SCAN_COMPLETED;
  }

  DState& st_;
  LosslessDecompressor<T>* ld_;
  bool multi_ = false;
  unsigned MCU_ctr_ = 0, MCU_vert_offset_ = 0;
  unsigned restart_rows_to_go_ = 0;
  std::vector<SampleArray<int>> diff_buf_, undiff_buf_;
  std::vector<int**> diff_rows_;
  std::vector<SampleArray<T>> whole_image_;
};

}  // namespace vio::jpeg::detail
