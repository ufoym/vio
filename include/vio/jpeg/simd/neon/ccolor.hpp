// vio::jpeg::simd::neon - RGB -> YCbCr / grayscale color conversion for
// compression (bit-exact with the scalar code).
#pragma once

#include <arm_neon.h>

#include <cstring>

#include "../../types.hpp"
#include "a32_compat.hpp"

namespace vio::jpeg::simd::neon {

using ColorConvertFn = void (*)(unsigned width, std::uint8_t** in, std::uint8_t*** out,
                                unsigned row, int num_rows);

namespace detail_cc {

constexpr std::uint16_t F_0_298 = 19595, F_0_587 = 38470, F_0_113 = 7471, F_0_168 = 11059,
                        F_0_331 = 21709, F_0_500 = 32768, F_0_418 = 27439, F_0_081 = 5329;

alignas(16) inline constexpr std::uint16_t rgb_ycc_consts[8] = {
    F_0_298, F_0_587, F_0_113, F_0_168, F_0_331, F_0_500, F_0_418, F_0_081};

struct Ycc8 {
  uint16x8_t y, cb, cr;
};

// Converts 8 pixels (r, g, b widened to 16 bits).
VIO_INLINE Ycc8 ycc8(uint16x8_t r, uint16x8_t g, uint16x8_t b, uint16x8_t consts,
                     uint32x4_t scaled_128_5) {
  uint32x4_t y_l = compat::mull_laneq<0>(vget_low_u16(r), consts);
  y_l = compat::mlal_laneq<1>(y_l, vget_low_u16(g), consts);
  y_l = compat::mlal_laneq<2>(y_l, vget_low_u16(b), consts);
  uint32x4_t y_h = compat::mull_laneq<0>(vget_high_u16(r), consts);
  y_h = compat::mlal_laneq<1>(y_h, vget_high_u16(g), consts);
  y_h = compat::mlal_laneq<2>(y_h, vget_high_u16(b), consts);
  uint32x4_t cb_l = scaled_128_5;
  cb_l = compat::mlsl_laneq<3>(cb_l, vget_low_u16(r), consts);
  cb_l = compat::mlsl_laneq<4>(cb_l, vget_low_u16(g), consts);
  cb_l = compat::mlal_laneq<5>(cb_l, vget_low_u16(b), consts);
  uint32x4_t cb_h = scaled_128_5;
  cb_h = compat::mlsl_laneq<3>(cb_h, vget_high_u16(r), consts);
  cb_h = compat::mlsl_laneq<4>(cb_h, vget_high_u16(g), consts);
  cb_h = compat::mlal_laneq<5>(cb_h, vget_high_u16(b), consts);
  uint32x4_t cr_l = scaled_128_5;
  cr_l = compat::mlal_laneq<5>(cr_l, vget_low_u16(r), consts);
  cr_l = compat::mlsl_laneq<6>(cr_l, vget_low_u16(g), consts);
  cr_l = compat::mlsl_laneq<7>(cr_l, vget_low_u16(b), consts);
  uint32x4_t cr_h = scaled_128_5;
  cr_h = compat::mlal_laneq<5>(cr_h, vget_high_u16(r), consts);
  cr_h = compat::mlsl_laneq<6>(cr_h, vget_high_u16(g), consts);
  cr_h = compat::mlsl_laneq<7>(cr_h, vget_high_u16(b), consts);
  return {vcombine_u16(vrshrn_n_u32(y_l, 16), vrshrn_n_u32(y_h, 16)),
          vcombine_u16(vshrn_n_u32(cb_l, 16), vshrn_n_u32(cb_h, 16)),
          vcombine_u16(vshrn_n_u32(cr_l, 16), vshrn_n_u32(cr_h, 16))};
}

template <int PS>
struct Load16;
template <>
struct Load16<3> {
  static VIO_INLINE uint8x16x4_t load(const std::uint8_t* p) {
    uint8x16x3_t v = vld3q_u8(p);
    return {{v.val[0], v.val[1], v.val[2], v.val[2]}};
  }
  static VIO_INLINE uint8x8x4_t load8(const std::uint8_t* p) {
    uint8x8x3_t v = vld3_u8(p);
    return {{v.val[0], v.val[1], v.val[2], v.val[2]}};
  }
};
template <>
struct Load16<4> {
  static VIO_INLINE uint8x16x4_t load(const std::uint8_t* p) { return vld4q_u8(p); }
  static VIO_INLINE uint8x8x4_t load8(const std::uint8_t* p) { return vld4_u8(p); }
};

}  // namespace detail_cc

template <int R, int G, int B, int PS>
void rgb_ycc_convert(unsigned image_width, std::uint8_t** input_buf, std::uint8_t*** output_buf,
                     unsigned output_row, int num_rows) {
  using namespace detail_cc;
  alignas(16) std::uint8_t tmp_buf[16 * PS];
  const uint16x8_t consts = vld1q_u16(opaque(rgb_ycc_consts));
  const uint32x4_t scaled_128_5 = vdupq_n_u32((128 << 16) + 32767);
  while (--num_rows >= 0) {
    const std::uint8_t* inptr = *input_buf++;
    std::uint8_t* outptr0 = output_buf[0][output_row];
    std::uint8_t* outptr1 = output_buf[1][output_row];
    std::uint8_t* outptr2 = output_buf[2][output_row];
    output_row++;
    int cols_remaining = static_cast<int>(image_width);
    auto do16 = [&](const std::uint8_t* p) {
      uint8x16x4_t px = Load16<PS>::load(p);
      Ycc8 lo = ycc8(vmovl_u8(vget_low_u8(px.val[R])), vmovl_u8(vget_low_u8(px.val[G])),
                     vmovl_u8(vget_low_u8(px.val[B])), consts, scaled_128_5);
      Ycc8 hi = ycc8(vmovl_u8(vget_high_u8(px.val[R])), vmovl_u8(vget_high_u8(px.val[G])),
                     vmovl_u8(vget_high_u8(px.val[B])), consts, scaled_128_5);
      vst1q_u8(outptr0, vcombine_u8(vmovn_u16(lo.y), vmovn_u16(hi.y)));
      vst1q_u8(outptr1, vcombine_u8(vmovn_u16(lo.cb), vmovn_u16(hi.cb)));
      vst1q_u8(outptr2, vcombine_u8(vmovn_u16(lo.cr), vmovn_u16(hi.cr)));
    };
    for (; cols_remaining >= 16; cols_remaining -= 16) {
      do16(inptr);
      inptr += 16 * PS;
      outptr0 += 16;
      outptr1 += 16;
      outptr2 += 16;
    }
    if (cols_remaining > 8) {
      std::memcpy(tmp_buf, inptr, static_cast<std::size_t>(cols_remaining) * PS);
      do16(tmp_buf);
    } else if (cols_remaining > 0) {
      std::memcpy(tmp_buf, inptr, static_cast<std::size_t>(cols_remaining) * PS);
      uint8x8x4_t px = Load16<PS>::load8(tmp_buf);
      Ycc8 v = ycc8(vmovl_u8(px.val[R]), vmovl_u8(px.val[G]), vmovl_u8(px.val[B]), consts,
                    scaled_128_5);
      vst1_u8(outptr0, vmovn_u16(v.y));
      vst1_u8(outptr1, vmovn_u16(v.cb));
      vst1_u8(outptr2, vmovn_u16(v.cr));
    }
  }
}

template <int R, int G, int B, int PS>
void rgb_gray_convert(unsigned image_width, std::uint8_t** input_buf, std::uint8_t*** output_buf,
                      unsigned output_row, int num_rows) {
  using namespace detail_cc;
  alignas(16) std::uint8_t tmp_buf[16 * PS];
  while (--num_rows >= 0) {
    const std::uint8_t* inptr = *input_buf++;
    std::uint8_t* outptr = output_buf[0][output_row];
    output_row++;
    for (int cols_remaining = static_cast<int>(image_width); cols_remaining > 0;
         cols_remaining -= 16) {
      if (cols_remaining < 16) {
        std::memcpy(tmp_buf, inptr, static_cast<std::size_t>(cols_remaining) * PS);
        inptr = tmp_buf;
      }
      uint8x16x4_t px = Load16<PS>::load(inptr);
      uint16x8_t r_l = vmovl_u8(vget_low_u8(px.val[R])), r_h = vmovl_u8(vget_high_u8(px.val[R]));
      uint16x8_t g_l = vmovl_u8(vget_low_u8(px.val[G])), g_h = vmovl_u8(vget_high_u8(px.val[G]));
      uint16x8_t b_l = vmovl_u8(vget_low_u8(px.val[B])), b_h = vmovl_u8(vget_high_u8(px.val[B]));
      uint32x4_t y_ll = vmull_n_u16(vget_low_u16(r_l), F_0_298);
      uint32x4_t y_lh = vmull_n_u16(vget_high_u16(r_l), F_0_298);
      uint32x4_t y_hl = vmull_n_u16(vget_low_u16(r_h), F_0_298);
      uint32x4_t y_hh = vmull_n_u16(vget_high_u16(r_h), F_0_298);
      y_ll = vmlal_n_u16(y_ll, vget_low_u16(g_l), F_0_587);
      y_lh = vmlal_n_u16(y_lh, vget_high_u16(g_l), F_0_587);
      y_hl = vmlal_n_u16(y_hl, vget_low_u16(g_h), F_0_587);
      y_hh = vmlal_n_u16(y_hh, vget_high_u16(g_h), F_0_587);
      y_ll = vmlal_n_u16(y_ll, vget_low_u16(b_l), F_0_113);
      y_lh = vmlal_n_u16(y_lh, vget_high_u16(b_l), F_0_113);
      y_hl = vmlal_n_u16(y_hl, vget_low_u16(b_h), F_0_113);
      y_hh = vmlal_n_u16(y_hh, vget_high_u16(b_h), F_0_113);
      uint16x8_t y_l = vcombine_u16(vrshrn_n_u32(y_ll, 16), vrshrn_n_u32(y_lh, 16));
      uint16x8_t y_h = vcombine_u16(vrshrn_n_u32(y_hl, 16), vrshrn_n_u32(y_hh, 16));
      vst1q_u8(outptr, vcombine_u8(vmovn_u16(y_l), vmovn_u16(y_h)));
      inptr += 16 * PS;
      outptr += 16;
    }
  }
}

// Kernels for an RGB-family input layout (nullptr if not RGB-family).
#include "../generic/ccolor_select.inc"

}  // namespace vio::jpeg::simd::neon
