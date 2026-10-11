// vio::jpeg::simd::neon - upsampling for decompression (bit-exact
// with the scalar code).
// Kernels read and write up to 32 samples past the end of each row, which
// the sample buffers' padding accommodates.
#pragma once

#include <arm_neon.h>

#include <cstdint>

#include "../../config.hpp"

namespace vio::jpeg::simd::neon {

// width: downsampled width for the fancy kernels, output width otherwise.
using UpsampleFn = void (*)(int max_v_samp_factor, unsigned width, std::uint8_t** input_data,
                            std::uint8_t** output_data);

inline void h2v1_fancy_upsample(int max_v_samp_factor, unsigned downsampled_width,
                                std::uint8_t** input_data, std::uint8_t** output_data) {
  const uint16x8_t one_u16 = vdupq_n_u16(1);
  const uint8x8_t three_u8 = vdup_n_u8(3);
  for (int inrow = 0; inrow < max_v_samp_factor; inrow++) {
    const std::uint8_t* inptr = input_data[inrow];
    std::uint8_t* outptr = output_data[inrow];
    *outptr = *inptr;
    uint8x16_t s0 = vld1q_u8(inptr);
    uint8x16_t s1 = vld1q_u8(inptr + 1);
    uint16x8_t s1_3s0_l = vmlal_u8(vmovl_u8(vget_low_u8(s1)), vget_low_u8(s0), three_u8);
    uint16x8_t s1_3s0_h = vmlal_u8(vmovl_u8(vget_high_u8(s1)), vget_high_u8(s0), three_u8);
    uint16x8_t s0_3s1_l = vaddq_u16(vmlal_u8(vmovl_u8(vget_low_u8(s0)), vget_low_u8(s1), three_u8), one_u16);
    uint16x8_t s0_3s1_h = vaddq_u16(vmlal_u8(vmovl_u8(vget_high_u8(s0)), vget_high_u8(s1), three_u8), one_u16);
    unsigned outptr_offset = 1;
    uint8x16x2_t out;
    for (unsigned colctr = 16; colctr < downsampled_width; colctr += 16) {
      s0 = vld1q_u8(inptr + colctr - 1);
      s1 = vld1q_u8(inptr + colctr);
      out.val[0] = vcombine_u8(vrshrn_n_u16(s1_3s0_l, 2), vrshrn_n_u16(s1_3s0_h, 2));
      out.val[1] = vcombine_u8(vshrn_n_u16(s0_3s1_l, 2), vshrn_n_u16(s0_3s1_h, 2));
      s1_3s0_l = vmlal_u8(vmovl_u8(vget_low_u8(s1)), vget_low_u8(s0), three_u8);
      s1_3s0_h = vmlal_u8(vmovl_u8(vget_high_u8(s1)), vget_high_u8(s0), three_u8);
      s0_3s1_l = vaddq_u16(vmlal_u8(vmovl_u8(vget_low_u8(s0)), vget_low_u8(s1), three_u8), one_u16);
      s0_3s1_h = vaddq_u16(vmlal_u8(vmovl_u8(vget_high_u8(s0)), vget_high_u8(s1), three_u8), one_u16);
      vst2q_u8(outptr + outptr_offset, out);
      outptr_offset = 2 * colctr - 1;
    }
    out.val[0] = vcombine_u8(vrshrn_n_u16(s1_3s0_l, 2), vrshrn_n_u16(s1_3s0_h, 2));
    out.val[1] = vcombine_u8(vshrn_n_u16(s0_3s1_l, 2), vshrn_n_u16(s0_3s1_h, 2));
    vst2q_u8(outptr + outptr_offset, out);
    outptr[2 * downsampled_width - 1] = inptr[downsampled_width - 1];
  }
}

namespace detail_us {
struct ColSums {
  uint16x8_t c0_l, c0_h, c1_l, c1_h;  // above-weighted / below-weighted
};
VIO_INLINE ColSums colsums(uint8x16_t a, uint8x16_t b, uint8x16_t c) {
  const uint8x8_t three = vdup_n_u8(3);
  return {vmlal_u8(vmovl_u8(vget_low_u8(a)), vget_low_u8(b), three),
          vmlal_u8(vmovl_u8(vget_high_u8(a)), vget_high_u8(b), three),
          vmlal_u8(vmovl_u8(vget_low_u8(c)), vget_low_u8(b), three),
          vmlal_u8(vmovl_u8(vget_high_u8(c)), vget_high_u8(b), three)};
}
VIO_INLINE void h2v2_store(const ColSums& s0, const ColSums& s1, std::uint8_t* outptr0,
                           std::uint8_t* outptr1) {
  const uint16x8_t seven = vdupq_n_u16(7), three = vdupq_n_u16(3);
  uint16x8_t o0c1_l = vaddq_u16(vmlaq_u16(s1.c0_l, s0.c0_l, three), seven);
  uint16x8_t o0c1_h = vaddq_u16(vmlaq_u16(s1.c0_h, s0.c0_h, three), seven);
  uint16x8_t o0c2_l = vmlaq_u16(s0.c0_l, s1.c0_l, three);
  uint16x8_t o0c2_h = vmlaq_u16(s0.c0_h, s1.c0_h, three);
  uint16x8_t o1c1_l = vaddq_u16(vmlaq_u16(s1.c1_l, s0.c1_l, three), seven);
  uint16x8_t o1c1_h = vaddq_u16(vmlaq_u16(s1.c1_h, s0.c1_h, three), seven);
  uint16x8_t o1c2_l = vmlaq_u16(s0.c1_l, s1.c1_l, three);
  uint16x8_t o1c2_h = vmlaq_u16(s0.c1_h, s1.c1_h, three);
  uint8x16x2_t out0 = {{vcombine_u8(vshrn_n_u16(o0c1_l, 4), vshrn_n_u16(o0c1_h, 4)),
                        vcombine_u8(vrshrn_n_u16(o0c2_l, 4), vrshrn_n_u16(o0c2_h, 4))}};
  uint8x16x2_t out1 = {{vcombine_u8(vshrn_n_u16(o1c1_l, 4), vshrn_n_u16(o1c1_h, 4)),
                        vcombine_u8(vrshrn_n_u16(o1c2_l, 4), vrshrn_n_u16(o1c2_h, 4))}};
  vst2q_u8(outptr0, out0);
  vst2q_u8(outptr1, out1);
}
}  // namespace detail_us

inline void h2v2_fancy_upsample(int max_v_samp_factor, unsigned downsampled_width,
                                std::uint8_t** input_data, std::uint8_t** output_data) {
  using namespace detail_us;
  int inrow = 0, outrow = 0;
  while (outrow < max_v_samp_factor) {
    const std::uint8_t* inptr0 = input_data[inrow - 1];
    const std::uint8_t* inptr1 = input_data[inrow];
    const std::uint8_t* inptr2 = input_data[inrow + 1];
    std::uint8_t* outptr0 = output_data[outrow++];
    std::uint8_t* outptr1 = output_data[outrow++];
    int s0colsum0 = inptr1[0] * 3 + inptr0[0];
    *outptr0 = static_cast<std::uint8_t>((s0colsum0 * 4 + 8) >> 4);
    int s0colsum1 = inptr1[0] * 3 + inptr2[0];
    *outptr1 = static_cast<std::uint8_t>((s0colsum1 * 4 + 8) >> 4);
    ColSums s0 = colsums(vld1q_u8(inptr0), vld1q_u8(inptr1), vld1q_u8(inptr2));
    ColSums s1 = colsums(vld1q_u8(inptr0 + 1), vld1q_u8(inptr1 + 1), vld1q_u8(inptr2 + 1));
    h2v2_store(s0, s1, outptr0 + 1, outptr1 + 1);
    for (unsigned colctr = 16; colctr < downsampled_width; colctr += 16) {
      s0 = colsums(vld1q_u8(inptr0 + colctr - 1), vld1q_u8(inptr1 + colctr - 1),
                   vld1q_u8(inptr2 + colctr - 1));
      s1 = colsums(vld1q_u8(inptr0 + colctr), vld1q_u8(inptr1 + colctr), vld1q_u8(inptr2 + colctr));
      h2v2_store(s0, s1, outptr0 + 2 * colctr - 1, outptr1 + 2 * colctr - 1);
    }
    const unsigned last = downsampled_width - 1;
    int s1colsum0 = inptr1[last] * 3 + inptr0[last];
    outptr0[2 * downsampled_width - 1] = static_cast<std::uint8_t>((s1colsum0 * 4 + 7) >> 4);
    int s1colsum1 = inptr1[last] * 3 + inptr2[last];
    outptr1[2 * downsampled_width - 1] = static_cast<std::uint8_t>((s1colsum1 * 4 + 7) >> 4);
    inrow++;
  }
}

inline void h1v2_fancy_upsample(int max_v_samp_factor, unsigned downsampled_width,
                                std::uint8_t** input_data, std::uint8_t** output_data) {
  const uint16x8_t one_u16 = vdupq_n_u16(1);
  int inrow = 0, outrow = 0;
  while (outrow < max_v_samp_factor) {
    const std::uint8_t* inptr0 = input_data[inrow - 1];
    const std::uint8_t* inptr1 = input_data[inrow];
    const std::uint8_t* inptr2 = input_data[inrow + 1];
    std::uint8_t* outptr0 = output_data[outrow++];
    std::uint8_t* outptr1 = output_data[outrow++];
    inrow++;
    for (unsigned colctr = 0; colctr < downsampled_width; colctr += 16) {
      detail_us::ColSums s = detail_us::colsums(vld1q_u8(inptr0 + colctr), vld1q_u8(inptr1 + colctr),
                                                vld1q_u8(inptr2 + colctr));
      uint16x8_t c0_l = vaddq_u16(s.c0_l, one_u16), c0_h = vaddq_u16(s.c0_h, one_u16);
      vst1q_u8(outptr0 + colctr, vcombine_u8(vshrn_n_u16(c0_l, 2), vshrn_n_u16(c0_h, 2)));
      vst1q_u8(outptr1 + colctr, vcombine_u8(vrshrn_n_u16(s.c1_l, 2), vrshrn_n_u16(s.c1_h, 2)));
    }
  }
}

inline void h2v1_upsample(int max_v_samp_factor, unsigned output_width, std::uint8_t** input_data,
                          std::uint8_t** output_data) {
  for (int inrow = 0; inrow < max_v_samp_factor; inrow++) {
    const std::uint8_t* inptr = input_data[inrow];
    std::uint8_t* outptr = output_data[inrow];
    for (unsigned colctr = 0; 2 * colctr < output_width; colctr += 16) {
      uint8x16_t s = vld1q_u8(inptr + colctr);
      const uint8x16x2_t ss = {{s, s}};
      vst2q_u8(outptr + 2 * colctr, ss);
    }
  }
}

inline void h2v2_upsample(int max_v_samp_factor, unsigned output_width, std::uint8_t** input_data,
                          std::uint8_t** output_data) {
  for (int inrow = 0, outrow = 0; outrow < max_v_samp_factor; inrow++) {
    const std::uint8_t* inptr = input_data[inrow];
    std::uint8_t* outptr0 = output_data[outrow++];
    std::uint8_t* outptr1 = output_data[outrow++];
    for (unsigned colctr = 0; 2 * colctr < output_width; colctr += 16) {
      uint8x16_t s = vld1q_u8(inptr + colctr);
      const uint8x16x2_t ss = {{s, s}};
      vst2q_u8(outptr0 + 2 * colctr, ss);
      vst2q_u8(outptr1 + 2 * colctr, ss);
    }
  }
}

}  // namespace vio::jpeg::simd::neon
