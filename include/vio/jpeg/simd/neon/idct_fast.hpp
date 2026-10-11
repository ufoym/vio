// vio::jpeg::simd::neon - fast integer and reduced-size (4x4, 2x2) inverse
// DCTs (bit-exact with the scalar code for valid data).  Dequantization
// multipliers are 16-bit.
#pragma once

#include <arm_neon.h>

#include <cstdint>

#include "../../config.hpp"

namespace vio::jpeg::simd::neon {

namespace detail_ifast {

// Q15 fractional parts of 1.082392200, 1.414213562, 1.847759065 and
// 2.613125930 (-2 is applied as neg_z10 + neg_z10).
alignas(16) inline constexpr std::int16_t consts[4] = {2688, 13568, 27776, 20096};

VIO_INLINE int16x8_t add(int16x8_t a, int16x8_t b) { return vaddq_s16(a, b); }
VIO_INLINE int16x4_t add(int16x4_t a, int16x4_t b) { return vadd_s16(a, b); }
VIO_INLINE int16x8_t sub(int16x8_t a, int16x8_t b) { return vsubq_s16(a, b); }
VIO_INLINE int16x4_t sub(int16x4_t a, int16x4_t b) { return vsub_s16(a, b); }
template <int L>
VIO_INLINE int16x8_t qdmulh(int16x8_t a, int16x4_t k) { return vqdmulhq_lane_s16(a, k, L); }
template <int L>
VIO_INLINE int16x4_t qdmulh(int16x4_t a, int16x4_t k) { return vqdmulh_lane_s16(a, k, L); }

// One 1-D AAN butterfly over dequantized inputs r[0..7], in place.
template <class V>
VIO_INLINE void butterfly(V* r, int16x4_t k) {
  V tmp10 = add(r[0], r[4]);  // phase 3
  V tmp11 = sub(r[0], r[4]);
  V tmp13 = add(r[2], r[6]);  // phases 5-3
  V d26 = sub(r[2], r[6]);
  V tmp12 = sub(add(qdmulh<1>(d26, k), d26), tmp13);
  V tmp0 = add(tmp10, tmp13);  // phase 2
  V tmp3 = sub(tmp10, tmp13);
  V tmp1 = add(tmp11, tmp12);
  V tmp2 = sub(tmp11, tmp12);

  V z13 = add(r[5], r[3]);  // phase 6
  V neg_z10 = sub(r[3], r[5]);
  V z11 = add(r[1], r[7]);
  V z12 = sub(r[1], r[7]);
  V tmp7 = add(z11, z13);  // phase 5
  V z11_sub_z13 = sub(z11, z13);
  tmp11 = add(qdmulh<1>(z11_sub_z13, k), z11_sub_z13);
  V z10_add_z12 = sub(z12, neg_z10);
  V z5 = add(qdmulh<2>(z10_add_z12, k), z10_add_z12);
  tmp10 = sub(add(qdmulh<0>(z12, k), z12), z5);
  tmp12 = add(add(qdmulh<3>(neg_z10, k), add(neg_z10, neg_z10)), z5);
  V tmp6 = sub(tmp12, tmp7);  // phase 2
  V tmp5 = sub(tmp11, tmp6);
  V tmp4 = add(tmp10, tmp5);

  r[0] = add(tmp0, tmp7);
  r[7] = sub(tmp0, tmp7);
  r[1] = add(tmp1, tmp6);
  r[6] = sub(tmp1, tmp6);
  r[2] = add(tmp2, tmp5);
  r[5] = sub(tmp2, tmp5);
  r[4] = add(tmp3, tmp4);
  r[3] = sub(tmp3, tmp4);
}

}  // namespace detail_ifast

inline void idct_ifast(const std::int16_t* quantptr, const std::int16_t* coef_block,
                       std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_ifast;
  int16x8_t row[8];
  for (int i = 0; i < 8; i++) row[i] = vld1q_s16(coef_block + i * 8);
  row[0] = vmulq_s16(row[0], vld1q_s16(quantptr));

  int16x8_t bitmap = vorrq_s16(row[1], row[2]);
  for (int i = 3; i < 8; i++) bitmap = vorrq_s16(bitmap, row[i]);
  const std::int64_t left_ac = vgetq_lane_s64(vreinterpretq_s64_s16(bitmap), 0);
  const std::int64_t right_ac = vgetq_lane_s64(vreinterpretq_s64_s16(bitmap), 1);
  const int16x4_t k = vld1_s16(opaque(consts));

  // Pass 1: columns.  Halves whose AC terms are all zero reduce to DC.
  if (left_ac == 0 && right_ac == 0) {
    for (int i = 1; i < 8; i++) row[i] = row[0];
  } else if (left_ac == 0) {
    const int16x4_t dcval = vget_low_s16(row[0]);
    int16x4_t h[8];
    h[0] = vget_high_s16(row[0]);
    for (int i = 1; i < 8; i++) h[i] = vmul_s16(vget_high_s16(row[i]), vld1_s16(quantptr + i * 8 + 4));
    butterfly(h, k);
    for (int i = 0; i < 8; i++) row[i] = vcombine_s16(dcval, h[i]);
  } else if (right_ac == 0) {
    const int16x4_t dcval = vget_high_s16(row[0]);
    int16x4_t h[8];
    h[0] = vget_low_s16(row[0]);
    for (int i = 1; i < 8; i++) h[i] = vmul_s16(vget_low_s16(row[i]), vld1_s16(quantptr + i * 8));
    butterfly(h, k);
    for (int i = 0; i < 8; i++) row[i] = vcombine_s16(h[i], dcval);
  } else {
    for (int i = 1; i < 8; i++) row[i] = vmulq_s16(row[i], vld1q_s16(quantptr + i * 8));
    butterfly(row, k);
  }

  // Transpose.
  int16x8x2_t rows_01 = vtrnq_s16(row[0], row[1]);
  int16x8x2_t rows_23 = vtrnq_s16(row[2], row[3]);
  int16x8x2_t rows_45 = vtrnq_s16(row[4], row[5]);
  int16x8x2_t rows_67 = vtrnq_s16(row[6], row[7]);
  int32x4x2_t rows_0145_l = vtrnq_s32(vreinterpretq_s32_s16(rows_01.val[0]), vreinterpretq_s32_s16(rows_45.val[0]));
  int32x4x2_t rows_0145_h = vtrnq_s32(vreinterpretq_s32_s16(rows_01.val[1]), vreinterpretq_s32_s16(rows_45.val[1]));
  int32x4x2_t rows_2367_l = vtrnq_s32(vreinterpretq_s32_s16(rows_23.val[0]), vreinterpretq_s32_s16(rows_67.val[0]));
  int32x4x2_t rows_2367_h = vtrnq_s32(vreinterpretq_s32_s16(rows_23.val[1]), vreinterpretq_s32_s16(rows_67.val[1]));
  int32x4x2_t cols_04 = vzipq_s32(rows_0145_l.val[0], rows_2367_l.val[0]);
  int32x4x2_t cols_15 = vzipq_s32(rows_0145_h.val[0], rows_2367_h.val[0]);
  int32x4x2_t cols_26 = vzipq_s32(rows_0145_l.val[1], rows_2367_l.val[1]);
  int32x4x2_t cols_37 = vzipq_s32(rows_0145_h.val[1], rows_2367_h.val[1]);
  int16x8_t col[8] = {vreinterpretq_s16_s32(cols_04.val[0]), vreinterpretq_s16_s32(cols_15.val[0]),
                      vreinterpretq_s16_s32(cols_26.val[0]), vreinterpretq_s16_s32(cols_37.val[0]),
                      vreinterpretq_s16_s32(cols_04.val[1]), vreinterpretq_s16_s32(cols_15.val[1]),
                      vreinterpretq_s16_s32(cols_26.val[1]), vreinterpretq_s16_s32(cols_37.val[1])};

  // Pass 2: rows.
  butterfly(col, k);

  // Descale (PASS1_BITS + 3), range-limit and transpose back.
  constexpr int shift = 2 + 3;
  const int8x16_t center = vreinterpretq_s8_u8(vdupq_n_u8(128));
  auto narrow = [&](int16x8_t a, int16x8_t b) VIO_LAMBDA_INLINE {
    return vreinterpretq_u8_s8(
        vaddq_s8(vcombine_s8(vqshrn_n_s16(a, shift), vqshrn_n_s16(b, shift)), center));
  };
  uint8x16_t cols_01 = narrow(col[0], col[1]);
  uint8x16_t cols_45 = narrow(col[4], col[5]);
  uint8x16_t cols_23 = narrow(col[2], col[3]);
  uint8x16_t cols_67 = narrow(col[6], col[7]);
  uint32x4x2_t cols_0415 = vzipq_u32(vreinterpretq_u32_u8(cols_01), vreinterpretq_u32_u8(cols_45));
  uint32x4x2_t cols_2637 = vzipq_u32(vreinterpretq_u32_u8(cols_23), vreinterpretq_u32_u8(cols_67));
  uint8x16x2_t cols_0145 = vtrnq_u8(vreinterpretq_u8_u32(cols_0415.val[0]), vreinterpretq_u8_u32(cols_0415.val[1]));
  uint8x16x2_t cols_2367 = vtrnq_u8(vreinterpretq_u8_u32(cols_2637.val[0]), vreinterpretq_u8_u32(cols_2637.val[1]));
  uint16x8x2_t rows_0426 = vtrnq_u16(vreinterpretq_u16_u8(cols_0145.val[0]), vreinterpretq_u16_u8(cols_2367.val[0]));
  uint16x8x2_t rows_1537 = vtrnq_u16(vreinterpretq_u16_u8(cols_0145.val[1]), vreinterpretq_u16_u8(cols_2367.val[1]));
  uint64x2_t rows_04 = vreinterpretq_u64_u16(rows_0426.val[0]);
  uint64x2_t rows_15 = vreinterpretq_u64_u16(rows_1537.val[0]);
  uint64x2_t rows_26 = vreinterpretq_u64_u16(rows_0426.val[1]);
  uint64x2_t rows_37 = vreinterpretq_u64_u16(rows_1537.val[1]);
  auto out = [&](int i) { return reinterpret_cast<std::uint64_t*>(output_buf[i] + output_col); };
  vst1q_lane_u64(out(0), rows_04, 0);
  vst1q_lane_u64(out(1), rows_15, 0);
  vst1q_lane_u64(out(2), rows_26, 0);
  vst1q_lane_u64(out(3), rows_37, 0);
  vst1q_lane_u64(out(4), rows_04, 1);
  vst1q_lane_u64(out(5), rows_15, 1);
  vst1q_lane_u64(out(6), rows_26, 1);
  vst1q_lane_u64(out(7), rows_37, 1);
}

namespace detail_idred {

constexpr int CONST_BITS = 13, PASS1_BITS = 2;
constexpr std::int16_t F_0_211 = 1730, F_0_509 = 4176, F_0_601 = 4926, F_0_720 = 5906,
                       F_0_765 = 6270, F_0_850 = 6967, F_0_899 = 7373, F_1_061 = 8697,
                       F_1_272 = 10426, F_1_451 = 11893, F_1_847 = 15137, F_2_172 = 17799,
                       F_2_562 = 20995, F_3_624 = 29692;

alignas(16) inline constexpr std::int16_t consts_2x2[4] = {-F_0_720, F_0_850, -F_1_272, F_3_624};
alignas(16) inline constexpr std::int16_t consts_4x4[12] = {
    F_1_847, -F_0_765, -F_0_211, F_1_451, -F_2_172, F_1_061, -F_0_509, -F_0_601,
    F_0_899, F_2_562,  0,        0};

// Pass 1 of the 4x4 IDCT for four columns (dequantized inputs; row 4 unused).
VIO_INLINE void pass1_4x4(int16x4_t r0, int16x4_t r1, int16x4_t r2, int16x4_t r3, int16x4_t r5,
                          int16x4_t r6, int16x4_t r7, int16x4x3_t k, int16x4_t* out) {
  constexpr int shift = CONST_BITS - PASS1_BITS + 1;
  int32x4_t tmp0 = vshll_n_s16(r0, CONST_BITS + 1);
  int32x4_t tmp2 = vmull_lane_s16(r2, k.val[0], 0);
  tmp2 = vmlal_lane_s16(tmp2, r6, k.val[0], 1);
  int32x4_t tmp10 = vaddq_s32(tmp0, tmp2);
  int32x4_t tmp12 = vsubq_s32(tmp0, tmp2);
  tmp0 = vmull_lane_s16(r7, k.val[0], 2);
  tmp0 = vmlal_lane_s16(tmp0, r5, k.val[0], 3);
  tmp0 = vmlal_lane_s16(tmp0, r3, k.val[1], 0);
  tmp0 = vmlal_lane_s16(tmp0, r1, k.val[1], 1);
  tmp2 = vmull_lane_s16(r7, k.val[1], 2);
  tmp2 = vmlal_lane_s16(tmp2, r5, k.val[1], 3);
  tmp2 = vmlal_lane_s16(tmp2, r3, k.val[2], 0);
  tmp2 = vmlal_lane_s16(tmp2, r1, k.val[2], 1);
  out[0] = vrshrn_n_s32(vaddq_s32(tmp10, tmp2), shift);
  out[3] = vrshrn_n_s32(vsubq_s32(tmp10, tmp2), shift);
  out[1] = vrshrn_n_s32(vaddq_s32(tmp12, tmp0), shift);
  out[2] = vrshrn_n_s32(vsubq_s32(tmp12, tmp0), shift);
}

}  // namespace detail_idred

inline void idct_4x4(const std::int16_t* quantptr, const std::int16_t* coef_block,
                     std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_idred;
  int16x8_t row[8];
  for (int i = 0; i < 8; i++)
    if (i != 4) row[i] = vld1q_s16(coef_block + i * 8);
  row[0] = vmulq_s16(row[0], vld1q_s16(quantptr));
  int16x8_t bitmap = vorrq_s16(vorrq_s16(vorrq_s16(row[1], row[2]), vorrq_s16(row[3], row[5])),
                               vorrq_s16(row[6], row[7]));
  const std::int64_t left_ac = vgetq_lane_s64(vreinterpretq_s64_s16(bitmap), 0);
  const std::int64_t right_ac = vgetq_lane_s64(vreinterpretq_s64_s16(bitmap), 1);
  const std::int16_t* kp = opaque(consts_4x4);
  const int16x4x3_t k = {{vld1_s16(kp), vld1_s16(kp + 4), vld1_s16(kp + 8)}};

  // Pass 1 (row 4 is not used).  Output rows 0..3, eight columns.
  int16x8_t r[4];
  auto half = [&](bool high, int16x4_t* o) VIO_LAMBDA_INLINE {
    auto dq = [&](int i) {
      return high ? vmul_s16(vget_high_s16(row[i]), vld1_s16(quantptr + i * 8 + 4))
                  : vmul_s16(vget_low_s16(row[i]), vld1_s16(quantptr + i * 8));
    };
    pass1_4x4(high ? vget_high_s16(row[0]) : vget_low_s16(row[0]), dq(1), dq(2), dq(3), dq(5), dq(6),
              dq(7), k, o);
  };
  if (left_ac == 0 && right_ac == 0) {
    int16x8_t dcval = vshlq_n_s16(row[0], PASS1_BITS);
    r[0] = r[1] = r[2] = r[3] = dcval;
  } else if (left_ac == 0) {
    int16x4_t dcval = vshl_n_s16(vget_low_s16(row[0]), PASS1_BITS);
    int16x4_t h[4];
    half(true, h);
    for (int i = 0; i < 4; i++) r[i] = vcombine_s16(dcval, h[i]);
  } else if (right_ac == 0) {
    int16x4_t dcval = vshl_n_s16(vget_high_s16(row[0]), PASS1_BITS);
    int16x4_t l[4];
    half(false, l);
    for (int i = 0; i < 4; i++) r[i] = vcombine_s16(l[i], dcval);
  } else {
    int16x4_t l[4], h[4];
    half(false, l);
    half(true, h);
    for (int i = 0; i < 4; i++) r[i] = vcombine_s16(l[i], h[i]);
  }

  // Transpose to columns.
  int16x8x2_t row_01 = vtrnq_s16(r[0], r[1]);
  int16x8x2_t row_23 = vtrnq_s16(r[2], r[3]);
  int32x4x2_t cols_0426 = vtrnq_s32(vreinterpretq_s32_s16(row_01.val[0]), vreinterpretq_s32_s16(row_23.val[0]));
  int32x4x2_t cols_1537 = vtrnq_s32(vreinterpretq_s32_s16(row_01.val[1]), vreinterpretq_s32_s16(row_23.val[1]));
  int16x4_t col0 = vreinterpret_s16_s32(vget_low_s32(cols_0426.val[0]));
  int16x4_t col1 = vreinterpret_s16_s32(vget_low_s32(cols_1537.val[0]));
  int16x4_t col2 = vreinterpret_s16_s32(vget_low_s32(cols_0426.val[1]));
  int16x4_t col3 = vreinterpret_s16_s32(vget_low_s32(cols_1537.val[1]));
  int16x4_t col5 = vreinterpret_s16_s32(vget_high_s32(cols_1537.val[0]));
  int16x4_t col6 = vreinterpret_s16_s32(vget_high_s32(cols_0426.val[1]));
  int16x4_t col7 = vreinterpret_s16_s32(vget_high_s32(cols_1537.val[1]));

  // Pass 2.
  int32x4_t tmp0 = vshll_n_s16(col0, CONST_BITS + 1);
  int32x4_t tmp2 = vmull_lane_s16(col2, k.val[0], 0);
  tmp2 = vmlal_lane_s16(tmp2, col6, k.val[0], 1);
  int32x4_t tmp10 = vaddq_s32(tmp0, tmp2);
  int32x4_t tmp12 = vsubq_s32(tmp0, tmp2);
  tmp0 = vmull_lane_s16(col7, k.val[0], 2);
  tmp0 = vmlal_lane_s16(tmp0, col5, k.val[0], 3);
  tmp0 = vmlal_lane_s16(tmp0, col3, k.val[1], 0);
  tmp0 = vmlal_lane_s16(tmp0, col1, k.val[1], 1);
  tmp2 = vmull_lane_s16(col7, k.val[1], 2);
  tmp2 = vmlal_lane_s16(tmp2, col5, k.val[1], 3);
  tmp2 = vmlal_lane_s16(tmp2, col3, k.val[2], 0);
  tmp2 = vmlal_lane_s16(tmp2, col1, k.val[2], 1);
  int16x8_t out_02 = vcombine_s16(vaddhn_s32(tmp10, tmp2), vsubhn_s32(tmp12, tmp0));
  int16x8_t out_13 = vcombine_s16(vaddhn_s32(tmp12, tmp0), vsubhn_s32(tmp10, tmp2));
  constexpr int shift = CONST_BITS + PASS1_BITS + 3 + 1 - 16;
  out_02 = vrsraq_n_s16(vdupq_n_s16(128), out_02, shift);
  out_13 = vrsraq_n_s16(vdupq_n_s16(128), out_13, shift);
  uint8x8x2_t out_0123 = vzip_u8(vqmovun_s16(out_02), vqmovun_s16(out_13));
  uint16x4x2_t out_01_23 = {{vreinterpret_u16_u8(out_0123.val[0]), vreinterpret_u16_u8(out_0123.val[1])}};
  auto out = [&](int i) { return reinterpret_cast<std::uint16_t*>(output_buf[i] + output_col); };
  vst2_lane_u16(out(0), out_01_23, 0);
  vst2_lane_u16(out(1), out_01_23, 1);
  vst2_lane_u16(out(2), out_01_23, 2);
  vst2_lane_u16(out(3), out_01_23, 3);
}

inline void idct_2x2(const std::int16_t* quantptr, const std::int16_t* coef_block,
                     std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_idred;
  auto dq = [&](int i) { return vmulq_s16(vld1q_s16(coef_block + i * 8), vld1q_s16(quantptr + i * 8)); };
  int16x8_t row0 = dq(0), row1 = dq(1), row3 = dq(3), row5 = dq(5), row7 = dq(7);
  const int16x4_t k = vld1_s16(opaque(consts_2x2));

  // Pass 1.
  int32x4_t tmp10_l = vshll_n_s16(vget_low_s16(row0), CONST_BITS + 2);
  int32x4_t tmp10_h = vshll_n_s16(vget_high_s16(row0), CONST_BITS + 2);
  int32x4_t tmp0_l = vmull_lane_s16(vget_low_s16(row1), k, 3);
  tmp0_l = vmlal_lane_s16(tmp0_l, vget_low_s16(row3), k, 2);
  tmp0_l = vmlal_lane_s16(tmp0_l, vget_low_s16(row5), k, 1);
  tmp0_l = vmlal_lane_s16(tmp0_l, vget_low_s16(row7), k, 0);
  int32x4_t tmp0_h = vmull_lane_s16(vget_high_s16(row1), k, 3);
  tmp0_h = vmlal_lane_s16(tmp0_h, vget_high_s16(row3), k, 2);
  tmp0_h = vmlal_lane_s16(tmp0_h, vget_high_s16(row5), k, 1);
  tmp0_h = vmlal_lane_s16(tmp0_h, vget_high_s16(row7), k, 0);
  row0 = vcombine_s16(vrshrn_n_s32(vaddq_s32(tmp10_l, tmp0_l), CONST_BITS),
                      vrshrn_n_s32(vaddq_s32(tmp10_h, tmp0_h), CONST_BITS));
  row1 = vcombine_s16(vrshrn_n_s32(vsubq_s32(tmp10_l, tmp0_l), CONST_BITS),
                      vrshrn_n_s32(vsubq_s32(tmp10_h, tmp0_h), CONST_BITS));

  // Transpose (only columns 0, 1, 3, 5, 7 are needed).
  int16x8x2_t cols_0246_1357 = vtrnq_s16(row0, row1);
  int16x8_t cols_0246 = cols_0246_1357.val[0];
  int16x8_t cols_1357 = cols_0246_1357.val[1];
  int32x4x2_t cols_1155_3377 = vtrnq_s32(vreinterpretq_s32_s16(cols_1357), vreinterpretq_s32_s16(cols_1357));
  int16x8_t cols_1155 = vreinterpretq_s16_s32(cols_1155_3377.val[0]);
  int16x8_t cols_3377 = vreinterpretq_s16_s32(cols_1155_3377.val[1]);

  // Pass 2.
  int32x4_t tmp10 = vshll_n_s16(vget_low_s16(cols_0246), CONST_BITS + 2);
  int32x4_t tmp0 = vmull_lane_s16(vget_low_s16(cols_1155), k, 3);
  tmp0 = vmlal_lane_s16(tmp0, vget_low_s16(cols_3377), k, 2);
  tmp0 = vmlal_lane_s16(tmp0, vget_high_s16(cols_1155), k, 1);
  tmp0 = vmlal_lane_s16(tmp0, vget_high_s16(cols_3377), k, 0);
  int16x8_t output_s16 = vcombine_s16(vaddhn_s32(tmp10, tmp0), vsubhn_s32(tmp10, tmp0));
  output_s16 = vrsraq_n_s16(vdupq_n_s16(128), output_s16, CONST_BITS + PASS1_BITS + 3 + 2 - 16);
  uint8x8_t output_u8 = vqmovun_s16(output_s16);
  vst1_lane_u8(output_buf[0] + output_col, output_u8, 0);
  vst1_lane_u8(output_buf[1] + output_col, output_u8, 1);
  vst1_lane_u8(output_buf[0] + output_col + 1, output_u8, 4);
  vst1_lane_u8(output_buf[1] + output_col + 1, output_u8, 5);
}

}  // namespace vio::jpeg::simd::neon
