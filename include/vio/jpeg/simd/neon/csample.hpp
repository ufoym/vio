// vio::jpeg::simd::neon - h2v1/h2v2 downsampling (bit-exact with
// the scalar code).
#pragma once

#include <arm_neon.h>

#include <cstdint>

#include "../../config.hpp"
#include "a32_compat.hpp"

namespace vio::jpeg::simd::neon {

using DownsampleFn = void (*)(unsigned image_width, int max_v_samp_factor, unsigned v_samp_factor,
                              unsigned width_in_blocks, std::uint8_t** input_data,
                              std::uint8_t** output_data);

namespace detail_ds {
// Row i (16 entries): table indices that replicate the last valid sample when
// i samples of the final block are padding.
constexpr auto make_expand_masks() {
  struct T { std::uint8_t v[16 * 16]; } t{};
  for (int pad = 0; pad < 16; pad++)
    for (int j = 0; j < 16; j++) t.v[pad * 16 + j] = static_cast<std::uint8_t>(j < 16 - pad ? j : 15 - pad);
  return t;
}
alignas(16) inline constexpr auto expand_masks = make_expand_masks();
}  // namespace detail_ds

inline void h2v1_downsample(unsigned image_width, int, unsigned v_samp_factor,
                            unsigned width_in_blocks, std::uint8_t** input_data,
                            std::uint8_t** output_data) {
  const int mask_offset = 16 * static_cast<int>(width_in_blocks * 16 - image_width);
  const uint8x16_t expand_mask = vld1q_u8(&detail_ds::expand_masks.v[mask_offset]);
  const uint16x8_t bias = vreinterpretq_u16_u32(vdupq_n_u32(0x00010000));
  for (unsigned outrow = 0; outrow < v_samp_factor; outrow++) {
    std::uint8_t* outptr = output_data[outrow];
    const std::uint8_t* inptr = input_data[outrow];
    for (unsigned i = 0; i + 1 < width_in_blocks; i++)
      vst1_u8(outptr + i * 8, vshrn_n_u16(vpadalq_u8(bias, vld1q_u8(inptr + i * 16)), 1));
    uint8x16_t c = compat::tbl1q(vld1q_u8(inptr + (width_in_blocks - 1) * 16), expand_mask);
    vst1_u8(outptr + (width_in_blocks - 1) * 8, vshrn_n_u16(vpadalq_u8(bias, c), 1));
  }
}

inline void h2v2_downsample(unsigned image_width, int, unsigned v_samp_factor,
                            unsigned width_in_blocks, std::uint8_t** input_data,
                            std::uint8_t** output_data) {
  const int mask_offset = 16 * static_cast<int>(width_in_blocks * 16 - image_width);
  const uint8x16_t expand_mask = vld1q_u8(&detail_ds::expand_masks.v[mask_offset]);
  const uint16x8_t bias = vreinterpretq_u16_u32(vdupq_n_u32(0x00020001));
  for (unsigned outrow = 0; outrow < v_samp_factor; outrow++) {
    std::uint8_t* outptr = output_data[outrow];
    const std::uint8_t* inptr0 = input_data[outrow * 2];
    const std::uint8_t* inptr1 = input_data[outrow * 2 + 1];
    for (unsigned i = 0; i + 1 < width_in_blocks; i++) {
      uint16x8_t s = vpadalq_u8(bias, vld1q_u8(inptr0 + i * 16));
      s = vpadalq_u8(s, vld1q_u8(inptr1 + i * 16));
      vst1_u8(outptr + i * 8, vshrn_n_u16(s, 2));
    }
    const unsigned last = (width_in_blocks - 1) * 16;
    uint16x8_t s = vpadalq_u8(bias, compat::tbl1q(vld1q_u8(inptr0 + last), expand_mask));
    s = vpadalq_u8(s, compat::tbl1q(vld1q_u8(inptr1 + last), expand_mask));
    vst1_u8(outptr + (width_in_blocks - 1) * 8, vshrn_n_u16(s, 2));
  }
}

}  // namespace vio::jpeg::simd::neon
