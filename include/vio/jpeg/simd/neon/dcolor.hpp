// vio::jpeg::simd::neon - YCbCr -> RGB / RGB565 color conversion for
// decompression (bit-exact with the scalar code).
#pragma once

#include <arm_neon.h>

#include <cstdint>
#include <cstring>

#include "../../types.hpp"

namespace vio::jpeg::simd::neon {

using ColorDeconvertFn = void (*)(unsigned output_width, std::uint8_t*** input_buf,
                                  unsigned input_row, std::uint8_t** output_buf, int num_rows);

namespace detail_dc {

constexpr std::int16_t F_0_344 = 11277, F_0_714 = 23401, F_1_402 = 22971, F_1_772 = 29033;
alignas(16) inline constexpr std::int16_t ycc_rgb_consts[4] = {-F_0_344, F_0_714, F_1_402,
                                                               F_1_772};

struct Rgb16 {
  int16x8_t r, g, b;
};

// Chroma contributions (R-Y, G-Y, B-Y) for 8 pixels.
VIO_INLINE Rgb16 chroma8(uint8x8_t cb, uint8x8_t cr, int16x4_t k) {
  const int16x8_t neg_128 = vdupq_n_s16(-128);
  int16x8_t cr_128 = vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(neg_128), cr));
  int16x8_t cb_128 = vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(neg_128), cb));
  int32x4_t g_l = vmull_lane_s16(vget_low_s16(cb_128), k, 0);
  int32x4_t g_h = vmull_lane_s16(vget_high_s16(cb_128), k, 0);
  g_l = vmlsl_lane_s16(g_l, vget_low_s16(cr_128), k, 1);
  g_h = vmlsl_lane_s16(g_h, vget_high_s16(cr_128), k, 1);
  return {vqrdmulhq_lane_s16(vshlq_n_s16(cr_128, 1), k, 2),
          vcombine_s16(vrshrn_n_s32(g_l, 15), vrshrn_n_s32(g_h, 15)),
          vqrdmulhq_lane_s16(vshlq_n_s16(cb_128, 1), k, 3)};
}

VIO_INLINE Rgb16 add_luma(const Rgb16& c, uint8x8_t y) {
  return {vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(c.r), y)),
          vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(c.g), y)),
          vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(c.b), y))};
}

VIO_INLINE Rgb16 convert8(uint8x8_t y, uint8x8_t cb, uint8x8_t cr, int16x4_t k) {
  const int16x8_t neg_128 = vdupq_n_s16(-128);
  int16x8_t cr_128 = vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(neg_128), cr));
  int16x8_t cb_128 = vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(neg_128), cb));
  int32x4_t g_l = vmull_lane_s16(vget_low_s16(cb_128), k, 0);
  int32x4_t g_h = vmull_lane_s16(vget_high_s16(cb_128), k, 0);
  g_l = vmlsl_lane_s16(g_l, vget_low_s16(cr_128), k, 1);
  g_h = vmlsl_lane_s16(g_h, vget_high_s16(cr_128), k, 1);
  int16x8_t g_sub_y = vcombine_s16(vrshrn_n_s32(g_l, 15), vrshrn_n_s32(g_h, 15));
  int16x8_t r_sub_y = vqrdmulhq_lane_s16(vshlq_n_s16(cr_128, 1), k, 2);
  int16x8_t b_sub_y = vqrdmulhq_lane_s16(vshlq_n_s16(cb_128, 1), k, 3);
  return {vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(r_sub_y), y)),
          vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(g_sub_y), y)),
          vreinterpretq_s16_u16(vaddw_u8(vreinterpretq_u16_s16(b_sub_y), y))};
}

VIO_INLINE uint16x8_t pack565(const Rgb16& c) {
  uint16x8_t v = vqshluq_n_s16(c.r, 8);
  v = vsriq_n_u16(v, vqshluq_n_s16(c.g, 8), 5);
  return vsriq_n_u16(v, vqshluq_n_s16(c.b, 8), 11);
}

// Stores 8 pixels (n <= 8 of them).
template <int R, int G, int B, int PS>
VIO_INLINE void store8(std::uint8_t* out, const Rgb16& c, int n) {
  if constexpr (PS == 2) {
    uint16x8_t v = pack565(c);
    if (n == 8) {
      vst1q_u16(reinterpret_cast<std::uint16_t*>(out), v);
    } else {
      alignas(16) std::uint16_t tmp[8];
      vst1q_u16(tmp, v);
      std::memcpy(out, tmp, static_cast<std::size_t>(n) * 2);
    }
  } else if constexpr (PS == 4) {
    uint8x8x4_t px;
    px.val[R] = vqmovun_s16(c.r);
    px.val[G] = vqmovun_s16(c.g);
    px.val[B] = vqmovun_s16(c.b);
    px.val[6 - R - G - B] = vdup_n_u8(0xFF);
    if (n == 8) {
      vst4_u8(out, px);
    } else {
      alignas(16) std::uint8_t tmp[32];
      vst4_u8(tmp, px);
      std::memcpy(out, tmp, static_cast<std::size_t>(n) * 4);
    }
  } else {
    uint8x8x3_t px;
    px.val[R] = vqmovun_s16(c.r);
    px.val[G] = vqmovun_s16(c.g);
    px.val[B] = vqmovun_s16(c.b);
    if (n == 8) {
      vst3_u8(out, px);
    } else {
      alignas(16) std::uint8_t tmp[24];
      vst3_u8(tmp, px);
      std::memcpy(out, tmp, static_cast<std::size_t>(n) * 3);
    }
  }
}

// Stores 16 pixels given as even/odd 8-pixel halves (n <= 16 of them).
template <int R, int G, int B, int PS>
VIO_INLINE void store16_pairs(std::uint8_t* out, const Rgb16& even, const Rgb16& odd, int n) {
  uint8x8x2_t r = vzip_u8(vqmovun_s16(even.r), vqmovun_s16(odd.r));
  uint8x8x2_t g = vzip_u8(vqmovun_s16(even.g), vqmovun_s16(odd.g));
  uint8x8x2_t b = vzip_u8(vqmovun_s16(even.b), vqmovun_s16(odd.b));
  alignas(16) std::uint8_t tmp[16 * PS];
  std::uint8_t* dst = n == 16 ? out : tmp;
  if constexpr (PS == 4) {
    uint8x16x4_t px;
    px.val[R] = vcombine_u8(r.val[0], r.val[1]);
    px.val[G] = vcombine_u8(g.val[0], g.val[1]);
    px.val[B] = vcombine_u8(b.val[0], b.val[1]);
    px.val[6 - R - G - B] = vdupq_n_u8(0xFF);
    vst4q_u8(dst, px);
  } else {
    uint8x16x3_t px;
    px.val[R] = vcombine_u8(r.val[0], r.val[1]);
    px.val[G] = vcombine_u8(g.val[0], g.val[1]);
    px.val[B] = vcombine_u8(b.val[0], b.val[1]);
    vst3q_u8(dst, px);
  }
  if (n != 16) std::memcpy(out, tmp, static_cast<std::size_t>(n) * PS);
}

}  // namespace detail_dc

// PS: bytes per output pixel (3, 4, or 2 for RGB565).
template <int R, int G, int B, int PS>
void ycc_rgb_convert(unsigned output_width, std::uint8_t*** input_buf, unsigned input_row,
                     std::uint8_t** output_buf, int num_rows) {
  using namespace detail_dc;
  const int16x4_t k = vld1_s16(opaque(ycc_rgb_consts));
  while (--num_rows >= 0) {
    const std::uint8_t* inptr0 = input_buf[0][input_row];
    const std::uint8_t* inptr1 = input_buf[1][input_row];
    const std::uint8_t* inptr2 = input_buf[2][input_row];
    input_row++;
    std::uint8_t* outptr = *output_buf++;
    int cols_remaining = static_cast<int>(output_width);
    for (; cols_remaining >= 16; cols_remaining -= 16) {
      uint8x16_t y = vld1q_u8(inptr0), cb = vld1q_u8(inptr1), cr = vld1q_u8(inptr2);
      Rgb16 lo = convert8(vget_low_u8(y), vget_low_u8(cb), vget_low_u8(cr), k);
      Rgb16 hi = convert8(vget_high_u8(y), vget_high_u8(cb), vget_high_u8(cr), k);
      if constexpr (PS == 2) {
        vst1q_u16(reinterpret_cast<std::uint16_t*>(outptr), pack565(lo));
        vst1q_u16(reinterpret_cast<std::uint16_t*>(outptr) + 8, pack565(hi));
      } else if constexpr (PS == 4) {
        uint8x16x4_t px;
        px.val[R] = vcombine_u8(vqmovun_s16(lo.r), vqmovun_s16(hi.r));
        px.val[G] = vcombine_u8(vqmovun_s16(lo.g), vqmovun_s16(hi.g));
        px.val[B] = vcombine_u8(vqmovun_s16(lo.b), vqmovun_s16(hi.b));
        px.val[6 - R - G - B] = vdupq_n_u8(0xFF);
        vst4q_u8(outptr, px);
      } else {
        uint8x16x3_t px;
        px.val[R] = vcombine_u8(vqmovun_s16(lo.r), vqmovun_s16(hi.r));
        px.val[G] = vcombine_u8(vqmovun_s16(lo.g), vqmovun_s16(hi.g));
        px.val[B] = vcombine_u8(vqmovun_s16(lo.b), vqmovun_s16(hi.b));
        vst3q_u8(outptr, px);
      }
      inptr0 += 16;
      inptr1 += 16;
      inptr2 += 16;
      outptr += 16 * PS;
    }
    for (; cols_remaining > 0; cols_remaining -= 8) {
      Rgb16 c = convert8(vld1_u8(inptr0), vld1_u8(inptr1), vld1_u8(inptr2), k);
      store8<R, G, B, PS>(outptr, c, cols_remaining < 8 ? cols_remaining : 8);
      inptr0 += 8;
      inptr1 += 8;
      inptr2 += 8;
      outptr += 8 * PS;
    }
  }
}

// Merged h2v1/h2v2 upsampling + YCbCr->RGB.
using MergedUpsampleFn = void (*)(unsigned output_width, std::uint8_t*** input_buf,
                                  unsigned in_row_group_ctr, std::uint8_t** output_buf);

template <bool V2, int R, int G, int B, int PS>
void merged_upsample(unsigned output_width, std::uint8_t*** input_buf, unsigned in_row_group_ctr,
                     std::uint8_t** output_buf) {
  using namespace detail_dc;
  const int16x4_t k = vld1_s16(opaque(ycc_rgb_consts));
  const std::uint8_t* inptr00 = input_buf[0][V2 ? in_row_group_ctr * 2 : in_row_group_ctr];
  const std::uint8_t* inptr01 = V2 ? input_buf[0][in_row_group_ctr * 2 + 1] : nullptr;
  const std::uint8_t* inptr1 = input_buf[1][in_row_group_ctr];
  const std::uint8_t* inptr2 = input_buf[2][in_row_group_ctr];
  std::uint8_t* outptr0 = output_buf[0];
  std::uint8_t* outptr1 = V2 ? output_buf[1] : nullptr;
  for (int cols_remaining = static_cast<int>(output_width); cols_remaining > 0;
       cols_remaining -= 16) {
    const int n = cols_remaining < 16 ? cols_remaining : 16;
    Rgb16 c = chroma8(vld1_u8(inptr1), vld1_u8(inptr2), k);
    uint8x8x2_t y0 = vld2_u8(inptr00);
    store16_pairs<R, G, B, PS>(outptr0, add_luma(c, y0.val[0]), add_luma(c, y0.val[1]), n);
    if constexpr (V2) {
      uint8x8x2_t y1 = vld2_u8(inptr01);
      store16_pairs<R, G, B, PS>(outptr1, add_luma(c, y1.val[0]), add_luma(c, y1.val[1]), n);
      inptr01 += 16;
      outptr1 += 16 * PS;
    }
    inptr00 += 16;
    inptr1 += 8;
    inptr2 += 8;
    outptr0 += 16 * PS;
  }
}

#include "../generic/merged_select.inc"

#define VIO_YCC_RGB565 1
#include "../generic/ycc_rgb_select.inc"
#undef VIO_YCC_RGB565

}  // namespace vio::jpeg::simd::neon
