// vio::jpeg - per-component parameters shared by encoder and decoder
// (equivalent of jpeg_component_info).
#pragma once

#include <cstddef>
#include <optional>

#include "common.hpp"
#include "../simd/common.hpp"

namespace vio::jpeg::detail {

struct ComponentInfo {
  int component_id = 0;
  int component_index = 0;
  int h_samp_factor = 0;
  int v_samp_factor = 0;
  int quant_tbl_no = 0;
  int dc_tbl_no = 0;
  int ac_tbl_no = 0;

  unsigned width_in_blocks = 0;
  unsigned height_in_blocks = 0;
  int DCT_scaled_size = DCTSIZE;
  unsigned downsampled_width = 0;
  unsigned downsampled_height = 0;
  bool component_needed = true;

  int MCU_width = 0, MCU_height = 0, MCU_blocks = 0, MCU_sample_width = 0;
  int last_col_width = 0, last_row_height = 0;

  std::optional<QuantTable> quant_table;  // latched at first scan

  // IDCT multiplier table (layout depends on the IDCT method)
  union alignas(32) DctTable {
    std::int32_t islow[DCTSIZE2];
    long long ifast_wide[DCTSIZE2];
    std::int16_t i16[DCTSIZE2];
    float flt[DCTSIZE2];
  } dct_table{};
  // 16-bit copy of the islow/ifast multipliers for SIMD kernels; must
  // immediately follow dct_table (see idct_table16()).
  alignas(32) std::int16_t dct_table16[DCTSIZE2]{};
};

template <class State>
inline int imcu_rows_in_scan(const State& st, unsigned row) {
  if (st.comps_in_scan > 1) return 1;
  const ComponentInfo& c = *st.cur_comp_info[0];
  return row < st.total_iMCU_rows - 1 ? c.v_samp_factor : c.last_row_height;
}

inline int component_rows_in_imcu(const ComponentInfo& c, bool full) {
  if (full) return c.v_samp_factor;
  int rows = static_cast<int>(c.height_in_blocks % c.v_samp_factor);
  return rows ? rows : c.v_samp_factor;
}

template <class Array, class State>
inline void allocate_component_buffers(std::vector<Array>& buffers, const State& st) {
  buffers.resize(st.num_components);
  for (int ci = 0; ci < st.num_components; ci++) {
    const ComponentInfo& c = st.comp_info[ci];
    buffers[ci].allocate(
        static_cast<std::size_t>(round_up(c.width_in_blocks, c.h_samp_factor)),
        static_cast<std::size_t>(round_up(c.height_in_blocks, c.v_samp_factor)));
  }
}

// Returns the 16-bit multiplier table belonging to a dct_table pointer.
inline const std::int16_t* idct_table16(const void* dct_table) {
  static_assert(offsetof(ComponentInfo, dct_table16) ==
                offsetof(ComponentInfo, dct_table) + sizeof(ComponentInfo::DctTable));
  static_assert(sizeof(ComponentInfo::DctTable) == vio::jpeg::simd::kDctTableBytes);
  return reinterpret_cast<const std::int16_t*>(static_cast<const char*>(dct_table) +
                                               sizeof(ComponentInfo::DctTable));
}

}  // namespace vio::jpeg::detail
