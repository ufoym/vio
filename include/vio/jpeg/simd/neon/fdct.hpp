// vio::jpeg::simd::neon - forward DCT, sample conversion and quantization
// (bit-exact with the scalar code).  These kernels operate on 16-bit DCT
// elements.
#pragma once

#include <arm_neon.h>

#include <cstdint>

#include "../../config.hpp"

namespace vio::jpeg::simd::neon {

// 8x8 samples -> centered 16-bit workspace.
inline void convsamp(std::uint8_t** sample_data, unsigned start_col, std::int16_t* workspace) {
  for (int r = 0; r < 8; r++) {
    uint8x8_t s = vld1_u8(sample_data[r] + start_col);
    vst1q_s16(workspace + r * 8, vreinterpretq_s16_u16(vsubl_u8(s, vdup_n_u8(128))));
  }
}

namespace detail_fdct {

struct Block8 {
  int16x8_t v[8];
};

// Loads a block transposed: v[i] holds column i of the block.
VIO_INLINE Block8 load_transposed(const std::int16_t* data) {
  int16x8x4_t r0123 = vld4q_s16(data);
  int16x8x4_t r4567 = vld4q_s16(data + 4 * 8);
  int16x8x2_t c04 = vuzpq_s16(r0123.val[0], r4567.val[0]);
  int16x8x2_t c15 = vuzpq_s16(r0123.val[1], r4567.val[1]);
  int16x8x2_t c26 = vuzpq_s16(r0123.val[2], r4567.val[2]);
  int16x8x2_t c37 = vuzpq_s16(r0123.val[3], r4567.val[3]);
  return {{c04.val[0], c15.val[0], c26.val[0], c37.val[0], c04.val[1], c15.val[1], c26.val[1],
           c37.val[1]}};
}

VIO_INLINE Block8 transpose(const Block8& c) {
  int16x8x2_t c01 = vtrnq_s16(c.v[0], c.v[1]);
  int16x8x2_t c23 = vtrnq_s16(c.v[2], c.v[3]);
  int16x8x2_t c45 = vtrnq_s16(c.v[4], c.v[5]);
  int16x8x2_t c67 = vtrnq_s16(c.v[6], c.v[7]);
  int32x4x2_t c0145_l = vtrnq_s32(vreinterpretq_s32_s16(c01.val[0]), vreinterpretq_s32_s16(c45.val[0]));
  int32x4x2_t c0145_h = vtrnq_s32(vreinterpretq_s32_s16(c01.val[1]), vreinterpretq_s32_s16(c45.val[1]));
  int32x4x2_t c2367_l = vtrnq_s32(vreinterpretq_s32_s16(c23.val[0]), vreinterpretq_s32_s16(c67.val[0]));
  int32x4x2_t c2367_h = vtrnq_s32(vreinterpretq_s32_s16(c23.val[1]), vreinterpretq_s32_s16(c67.val[1]));
  int32x4x2_t r04 = vzipq_s32(c0145_l.val[0], c2367_l.val[0]);
  int32x4x2_t r15 = vzipq_s32(c0145_h.val[0], c2367_h.val[0]);
  int32x4x2_t r26 = vzipq_s32(c0145_l.val[1], c2367_l.val[1]);
  int32x4x2_t r37 = vzipq_s32(c0145_h.val[1], c2367_h.val[1]);
  return {{vreinterpretq_s16_s32(r04.val[0]), vreinterpretq_s16_s32(r15.val[0]),
           vreinterpretq_s16_s32(r26.val[0]), vreinterpretq_s16_s32(r37.val[0]),
           vreinterpretq_s16_s32(r04.val[1]), vreinterpretq_s16_s32(r15.val[1]),
           vreinterpretq_s16_s32(r26.val[1]), vreinterpretq_s16_s32(r37.val[1])}};
}

constexpr std::int16_t F_0_298 = 2446, F_0_390 = 3196, F_0_541 = 4433, F_0_765 = 6270,
                       F_0_899 = 7373, F_1_175 = 9633, F_1_501 = 12299, F_1_847 = 15137,
                       F_1_961 = 16069, F_2_053 = 16819, F_2_562 = 20995, F_3_072 = 25172;
alignas(16) inline constexpr std::int16_t islow_consts[12] = {
    F_0_298, -F_0_390, F_0_541,  F_0_765,  -F_0_899, F_1_175,
    F_1_501, -F_1_847, -F_1_961, F_2_053, -F_2_562, F_3_072};

// One islow pass over 8 vectors (in = columns for pass 1, rows for pass 2).
template <int kPass>
VIO_INLINE Block8 islow_pass(const Block8& in, const int16x4x3_t& k) {
  constexpr int CONST_BITS = 13, PASS1_BITS = 2;
  constexpr int DESCALE = kPass == 1 ? CONST_BITS - PASS1_BITS : CONST_BITS + PASS1_BITS;
  const int16x8_t* c = in.v;
  Block8 o;
  int16x8_t tmp0 = vaddq_s16(c[0], c[7]), tmp7 = vsubq_s16(c[0], c[7]);
  int16x8_t tmp1 = vaddq_s16(c[1], c[6]), tmp6 = vsubq_s16(c[1], c[6]);
  int16x8_t tmp2 = vaddq_s16(c[2], c[5]), tmp5 = vsubq_s16(c[2], c[5]);
  int16x8_t tmp3 = vaddq_s16(c[3], c[4]), tmp4 = vsubq_s16(c[3], c[4]);
  int16x8_t tmp10 = vaddq_s16(tmp0, tmp3), tmp13 = vsubq_s16(tmp0, tmp3);
  int16x8_t tmp11 = vaddq_s16(tmp1, tmp2), tmp12 = vsubq_s16(tmp1, tmp2);
  if constexpr (kPass == 1) {
    o.v[0] = vshlq_n_s16(vaddq_s16(tmp10, tmp11), PASS1_BITS);
    o.v[4] = vshlq_n_s16(vsubq_s16(tmp10, tmp11), PASS1_BITS);
  } else {
    o.v[0] = vrshrq_n_s16(vaddq_s16(tmp10, tmp11), PASS1_BITS);
    o.v[4] = vrshrq_n_s16(vsubq_s16(tmp10, tmp11), PASS1_BITS);
  }
  int16x8_t t1213 = vaddq_s16(tmp12, tmp13);
  int32x4_t z1_l = vmull_lane_s16(vget_low_s16(t1213), k.val[0], 2);
  int32x4_t z1_h = vmull_lane_s16(vget_high_s16(t1213), k.val[0], 2);
  o.v[2] = vcombine_s16(vrshrn_n_s32(vmlal_lane_s16(z1_l, vget_low_s16(tmp13), k.val[0], 3), DESCALE),
                        vrshrn_n_s32(vmlal_lane_s16(z1_h, vget_high_s16(tmp13), k.val[0], 3), DESCALE));
  o.v[6] = vcombine_s16(vrshrn_n_s32(vmlal_lane_s16(z1_l, vget_low_s16(tmp12), k.val[1], 3), DESCALE),
                        vrshrn_n_s32(vmlal_lane_s16(z1_h, vget_high_s16(tmp12), k.val[1], 3), DESCALE));
  int16x8_t z1 = vaddq_s16(tmp4, tmp7), z2 = vaddq_s16(tmp5, tmp6);
  int16x8_t z3 = vaddq_s16(tmp4, tmp6), z4 = vaddq_s16(tmp5, tmp7);
  int32x4_t z5_l = vmull_lane_s16(vget_low_s16(z3), k.val[1], 1);
  int32x4_t z5_h = vmull_lane_s16(vget_high_s16(z3), k.val[1], 1);
  z5_l = vmlal_lane_s16(z5_l, vget_low_s16(z4), k.val[1], 1);
  z5_h = vmlal_lane_s16(z5_h, vget_high_s16(z4), k.val[1], 1);
  int32x4_t tmp4_l = vmull_lane_s16(vget_low_s16(tmp4), k.val[0], 0);
  int32x4_t tmp4_h = vmull_lane_s16(vget_high_s16(tmp4), k.val[0], 0);
  int32x4_t tmp5_l = vmull_lane_s16(vget_low_s16(tmp5), k.val[2], 1);
  int32x4_t tmp5_h = vmull_lane_s16(vget_high_s16(tmp5), k.val[2], 1);
  int32x4_t tmp6_l = vmull_lane_s16(vget_low_s16(tmp6), k.val[2], 3);
  int32x4_t tmp6_h = vmull_lane_s16(vget_high_s16(tmp6), k.val[2], 3);
  int32x4_t tmp7_l = vmull_lane_s16(vget_low_s16(tmp7), k.val[1], 2);
  int32x4_t tmp7_h = vmull_lane_s16(vget_high_s16(tmp7), k.val[1], 2);
  z1_l = vmull_lane_s16(vget_low_s16(z1), k.val[1], 0);
  z1_h = vmull_lane_s16(vget_high_s16(z1), k.val[1], 0);
  int32x4_t z2_l = vmull_lane_s16(vget_low_s16(z2), k.val[2], 2);
  int32x4_t z2_h = vmull_lane_s16(vget_high_s16(z2), k.val[2], 2);
  int32x4_t z3_l = vmull_lane_s16(vget_low_s16(z3), k.val[2], 0);
  int32x4_t z3_h = vmull_lane_s16(vget_high_s16(z3), k.val[2], 0);
  int32x4_t z4_l = vmull_lane_s16(vget_low_s16(z4), k.val[0], 1);
  int32x4_t z4_h = vmull_lane_s16(vget_high_s16(z4), k.val[0], 1);
  z3_l = vaddq_s32(z3_l, z5_l);
  z3_h = vaddq_s32(z3_h, z5_h);
  z4_l = vaddq_s32(z4_l, z5_l);
  z4_h = vaddq_s32(z4_h, z5_h);
  tmp4_l = vaddq_s32(vaddq_s32(tmp4_l, z1_l), z3_l);
  tmp4_h = vaddq_s32(vaddq_s32(tmp4_h, z1_h), z3_h);
  o.v[7] = vcombine_s16(vrshrn_n_s32(tmp4_l, DESCALE), vrshrn_n_s32(tmp4_h, DESCALE));
  tmp5_l = vaddq_s32(vaddq_s32(tmp5_l, z2_l), z4_l);
  tmp5_h = vaddq_s32(vaddq_s32(tmp5_h, z2_h), z4_h);
  o.v[5] = vcombine_s16(vrshrn_n_s32(tmp5_l, DESCALE), vrshrn_n_s32(tmp5_h, DESCALE));
  tmp6_l = vaddq_s32(vaddq_s32(tmp6_l, z2_l), z3_l);
  tmp6_h = vaddq_s32(vaddq_s32(tmp6_h, z2_h), z3_h);
  o.v[3] = vcombine_s16(vrshrn_n_s32(tmp6_l, DESCALE), vrshrn_n_s32(tmp6_h, DESCALE));
  tmp7_l = vaddq_s32(vaddq_s32(tmp7_l, z1_l), z4_l);
  tmp7_h = vaddq_s32(vaddq_s32(tmp7_h, z1_h), z4_h);
  o.v[1] = vcombine_s16(vrshrn_n_s32(tmp7_l, DESCALE), vrshrn_n_s32(tmp7_h, DESCALE));
  return o;
}

alignas(16) inline constexpr std::int16_t ifast_consts[4] = {12544, 17792, 23168, 9984};

VIO_INLINE Block8 ifast_pass(const Block8& in, int16x4_t k) {
  const int16x8_t* c = in.v;
  Block8 o;
  int16x8_t tmp0 = vaddq_s16(c[0], c[7]), tmp7 = vsubq_s16(c[0], c[7]);
  int16x8_t tmp1 = vaddq_s16(c[1], c[6]), tmp6 = vsubq_s16(c[1], c[6]);
  int16x8_t tmp2 = vaddq_s16(c[2], c[5]), tmp5 = vsubq_s16(c[2], c[5]);
  int16x8_t tmp3 = vaddq_s16(c[3], c[4]), tmp4 = vsubq_s16(c[3], c[4]);
  int16x8_t tmp10 = vaddq_s16(tmp0, tmp3), tmp13 = vsubq_s16(tmp0, tmp3);
  int16x8_t tmp11 = vaddq_s16(tmp1, tmp2), tmp12 = vsubq_s16(tmp1, tmp2);
  o.v[0] = vaddq_s16(tmp10, tmp11);
  o.v[4] = vsubq_s16(tmp10, tmp11);
  int16x8_t z1 = vqdmulhq_lane_s16(vaddq_s16(tmp12, tmp13), k, 2);
  o.v[2] = vaddq_s16(tmp13, z1);
  o.v[6] = vsubq_s16(tmp13, z1);
  tmp10 = vaddq_s16(tmp4, tmp5);
  tmp11 = vaddq_s16(tmp5, tmp6);
  tmp12 = vaddq_s16(tmp6, tmp7);
  int16x8_t z5 = vqdmulhq_lane_s16(vsubq_s16(tmp10, tmp12), k, 0);
  int16x8_t z2 = vaddq_s16(vqdmulhq_lane_s16(tmp10, k, 1), z5);
  int16x8_t z4 = vqdmulhq_lane_s16(tmp12, k, 3);
  z5 = vaddq_s16(tmp12, z5);
  z4 = vaddq_s16(z4, z5);
  int16x8_t z3 = vqdmulhq_lane_s16(tmp11, k, 2);
  int16x8_t z11 = vaddq_s16(tmp7, z3), z13 = vsubq_s16(tmp7, z3);
  o.v[5] = vaddq_s16(z13, z2);
  o.v[3] = vsubq_s16(z13, z2);
  o.v[1] = vaddq_s16(z11, z4);
  o.v[7] = vsubq_s16(z11, z4);
  return o;
}

VIO_INLINE void store(std::int16_t* data, const Block8& r) {
  for (int i = 0; i < 8; i++) vst1q_s16(data + i * 8, r.v[i]);
}

}  // namespace detail_fdct

inline void fdct_islow(std::int16_t* data) {
  using namespace detail_fdct;
  const int16x4x3_t k = {{vld1_s16(opaque(islow_consts)), vld1_s16(opaque(islow_consts) + 4),
                          vld1_s16(opaque(islow_consts) + 8)}};
  Block8 cols = load_transposed(data);
  Block8 p1 = islow_pass<1>(cols, k);
  store(data, islow_pass<2>(transpose(p1), k));
}

inline void fdct_ifast(std::int16_t* data) {
  using namespace detail_fdct;
  const int16x4_t k = vld1_s16(opaque(ifast_consts));
  Block8 cols = load_transposed(data);
  Block8 p1 = ifast_pass(cols, k);
  store(data, ifast_pass(transpose(p1), k));
}

// divisors: 16-bit reciprocal [0..63], correction [64..127], scale
// [128..191], shift [192..255].
inline void quantize(std::int16_t* coef_block, const std::uint16_t* divisors,
                     const std::int16_t* workspace) {
  const std::uint16_t* recip_ptr = divisors;
  const std::uint16_t* corr_ptr = divisors + 64;
  const std::int16_t* shift_ptr = reinterpret_cast<const std::int16_t*>(divisors + 3 * 64);
  for (int i = 0; i < 64; i += 8) {
    int16x8_t row = vld1q_s16(workspace + i);
    uint16x8_t recip = vld1q_u16(recip_ptr + i);
    uint16x8_t corr = vld1q_u16(corr_ptr + i);
    int16x8_t shift = vld1q_s16(shift_ptr + i);
    int16x8_t sign = vshrq_n_s16(row, 15);
    uint16x8_t abs_row = vaddq_u16(vreinterpretq_u16_s16(vabsq_s16(row)), corr);
    int32x4_t lo = vreinterpretq_s32_u32(vmull_u16(vget_low_u16(abs_row), vget_low_u16(recip)));
    int32x4_t hi = vreinterpretq_s32_u32(vmull_u16(vget_high_u16(abs_row), vget_high_u16(recip)));
    row = vcombine_s16(vshrn_n_s32(lo, 16), vshrn_n_s32(hi, 16));
    row = vreinterpretq_s16_u16(vshlq_u16(vreinterpretq_u16_s16(row), vnegq_s16(shift)));
    row = vsubq_s16(veorq_s16(row, sign), sign);
    vst1q_s16(coef_block + i, row);
  }
}

}  // namespace vio::jpeg::simd::neon
