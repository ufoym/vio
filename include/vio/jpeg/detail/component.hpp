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

// Returns the 16-bit multiplier table belonging to a dct_table pointer.
inline const std::int16_t* idct_table16(const void* dct_table) {
  static_assert(offsetof(ComponentInfo, dct_table16) ==
                offsetof(ComponentInfo, dct_table) + sizeof(ComponentInfo::DctTable));
  static_assert(sizeof(ComponentInfo::DctTable) == vio::jpeg::simd::kDctTableBytes);
  return reinterpret_cast<const std::int16_t*>(static_cast<const char*>(dct_table) +
                                               sizeof(ComponentInfo::DctTable));
}

}  // namespace vio::jpeg::detail
