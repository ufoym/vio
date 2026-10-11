// vio::jpeg::simd::neon - accurate integer inverse DCT (bit-exact with the
// scalar code for valid data).
// Dequantization multipliers are 16-bit.
#pragma once

#include <arm_neon.h>

#include <cstdint>

#include "../../config.hpp"

namespace vio::jpeg::simd::neon {

namespace detail_idct {

constexpr int CONST_BITS = 13, PASS1_BITS = 2;
constexpr int DESCALE_P1 = CONST_BITS - PASS1_BITS;
constexpr int DESCALE_P2 = CONST_BITS + PASS1_BITS + 3;

constexpr std::int16_t F_0_298 = 2446, F_0_390 = 3196, F_0_541 = 4433, F_0_765 = 6270,
                       F_0_899 = 7373, F_1_175 = 9633, F_1_501 = 12299, F_1_847 = 15137,
                       F_1_961 = 16069, F_2_053 = 16819, F_2_562 = 20995, F_3_072 = 25172;

alignas(16) inline constexpr std::int16_t islow_consts[16] = {
    F_0_899,           F_0_541,           F_2_562,           F_0_298 - F_0_899,
    F_1_501 - F_0_899, F_2_053 - F_2_562, F_0_541 + F_0_765, F_1_175,
    F_1_175 - F_0_390, F_0_541 - F_1_847, F_3_072 - F_2_562, F_1_175 - F_1_961,
    0,                 0,                 0,                 0};

VIO_INLINE int16x4x3_t load_consts() {
  return {{vld1_s16(opaque(islow_consts)), vld1_s16(opaque(islow_consts) + 4), vld1_s16(opaque(islow_consts) + 8)}};
}

struct Rows {
  int16x4_t r0, r1, r2, r3, r4, r5, r6, r7;
};

VIO_INLINE void pass1_regular(Rows row, Rows q, std::int16_t* ws1, std::int16_t* ws2) {
  const int16x4x3_t k = load_consts();
  int16x4_t z2_s16 = vmul_s16(row.r2, q.r2);
  int16x4_t z3_s16 = vmul_s16(row.r6, q.r6);
  int32x4_t tmp2 = vmull_lane_s16(z2_s16, k.val[0], 1);
  int32x4_t tmp3 = vmull_lane_s16(z2_s16, k.val[1], 2);
  tmp2 = vmlal_lane_s16(tmp2, z3_s16, k.val[2], 1);
  tmp3 = vmlal_lane_s16(tmp3, z3_s16, k.val[0], 1);
  z2_s16 = vmul_s16(row.r0, q.r0);
  z3_s16 = vmul_s16(row.r4, q.r4);
  int32x4_t tmp0 = vshll_n_s16(vadd_s16(z2_s16, z3_s16), CONST_BITS);
  int32x4_t tmp1 = vshll_n_s16(vsub_s16(z2_s16, z3_s16), CONST_BITS);
  int32x4_t tmp10 = vaddq_s32(tmp0, tmp3), tmp13 = vsubq_s32(tmp0, tmp3);
  int32x4_t tmp11 = vaddq_s32(tmp1, tmp2), tmp12 = vsubq_s32(tmp1, tmp2);
  int16x4_t tmp0_s16 = vmul_s16(row.r7, q.r7);
  int16x4_t tmp1_s16 = vmul_s16(row.r5, q.r5);
  int16x4_t tmp2_s16 = vmul_s16(row.r3, q.r3);
  int16x4_t tmp3_s16 = vmul_s16(row.r1, q.r1);
  z3_s16 = vadd_s16(tmp0_s16, tmp2_s16);
  int16x4_t z4_s16 = vadd_s16(tmp1_s16, tmp3_s16);
  int32x4_t z3 = vmull_lane_s16(z3_s16, k.val[2], 3);
  int32x4_t z4 = vmull_lane_s16(z3_s16, k.val[1], 3);
  z3 = vmlal_lane_s16(z3, z4_s16, k.val[1], 3);
  z4 = vmlal_lane_s16(z4, z4_s16, k.val[2], 0);
  tmp0 = vmull_lane_s16(tmp0_s16, k.val[0], 3);
  tmp1 = vmull_lane_s16(tmp1_s16, k.val[1], 1);
  tmp2 = vmull_lane_s16(tmp2_s16, k.val[2], 2);
  tmp3 = vmull_lane_s16(tmp3_s16, k.val[1], 0);
  tmp0 = vmlsl_lane_s16(tmp0, tmp3_s16, k.val[0], 0);
  tmp1 = vmlsl_lane_s16(tmp1, tmp2_s16, k.val[0], 2);
  tmp2 = vmlsl_lane_s16(tmp2, tmp1_s16, k.val[0], 2);
  tmp3 = vmlsl_lane_s16(tmp3, tmp0_s16, k.val[0], 0);
  tmp0 = vaddq_s32(tmp0, z3);
  tmp1 = vaddq_s32(tmp1, z4);
  tmp2 = vaddq_s32(tmp2, z3);
  tmp3 = vaddq_s32(tmp3, z4);
  int16x4x4_t rows_0123 = {{vrshrn_n_s32(vaddq_s32(tmp10, tmp3), DESCALE_P1),
                            vrshrn_n_s32(vaddq_s32(tmp11, tmp2), DESCALE_P1),
                            vrshrn_n_s32(vaddq_s32(tmp12, tmp1), DESCALE_P1),
                            vrshrn_n_s32(vaddq_s32(tmp13, tmp0), DESCALE_P1)}};
  int16x4x4_t rows_4567 = {{vrshrn_n_s32(vsubq_s32(tmp13, tmp0), DESCALE_P1),
                            vrshrn_n_s32(vsubq_s32(tmp12, tmp1), DESCALE_P1),
                            vrshrn_n_s32(vsubq_s32(tmp11, tmp2), DESCALE_P1),
                            vrshrn_n_s32(vsubq_s32(tmp10, tmp3), DESCALE_P1)}};
  vst4_s16(ws1, rows_0123);
  vst4_s16(ws2, rows_4567);
}

VIO_INLINE void pass1_sparse(Rows row, Rows q, std::int16_t* ws1, std::int16_t* ws2) {
  const int16x4x3_t k = load_consts();
  int16x4_t z2_s16 = vmul_s16(row.r2, q.r2);
  int32x4_t tmp2 = vmull_lane_s16(z2_s16, k.val[0], 1);
  int32x4_t tmp3 = vmull_lane_s16(z2_s16, k.val[1], 2);
  z2_s16 = vmul_s16(row.r0, q.r0);
  int32x4_t tmp0 = vshll_n_s16(z2_s16, CONST_BITS);
  int32x4_t tmp1 = vshll_n_s16(z2_s16, CONST_BITS);
  int32x4_t tmp10 = vaddq_s32(tmp0, tmp3), tmp13 = vsubq_s32(tmp0, tmp3);
  int32x4_t tmp11 = vaddq_s32(tmp1, tmp2), tmp12 = vsubq_s32(tmp1, tmp2);
  int16x4_t tmp2_s16 = vmul_s16(row.r3, q.r3);
  int16x4_t tmp3_s16 = vmul_s16(row.r1, q.r1);
  int16x4_t z3_s16 = tmp2_s16, z4_s16 = tmp3_s16;
  int32x4_t z3 = vmull_lane_s16(z3_s16, k.val[2], 3);
  int32x4_t z4 = vmull_lane_s16(z3_s16, k.val[1], 3);
  z3 = vmlal_lane_s16(z3, z4_s16, k.val[1], 3);
  z4 = vmlal_lane_s16(z4, z4_s16, k.val[2], 0);
  tmp0 = vmlsl_lane_s16(z3, tmp3_s16, k.val[0], 0);
  tmp1 = vmlsl_lane_s16(z4, tmp2_s16, k.val[0], 2);
  tmp2 = vmlal_lane_s16(z3, tmp2_s16, k.val[2], 2);
  tmp3 = vmlal_lane_s16(z4, tmp3_s16, k.val[1], 0);
  int16x4x4_t rows_0123 = {{vrshrn_n_s32(vaddq_s32(tmp10, tmp3), DESCALE_P1),
                            vrshrn_n_s32(vaddq_s32(tmp11, tmp2), DESCALE_P1),
                            vrshrn_n_s32(vaddq_s32(tmp12, tmp1), DESCALE_P1),
                            vrshrn_n_s32(vaddq_s32(tmp13, tmp0), DESCALE_P1)}};
  int16x4x4_t rows_4567 = {{vrshrn_n_s32(vsubq_s32(tmp13, tmp0), DESCALE_P1),
                            vrshrn_n_s32(vsubq_s32(tmp12, tmp1), DESCALE_P1),
                            vrshrn_n_s32(vsubq_s32(tmp11, tmp2), DESCALE_P1),
                            vrshrn_n_s32(vsubq_s32(tmp10, tmp3), DESCALE_P1)}};
  vst4_s16(ws1, rows_0123);
  vst4_s16(ws2, rows_4567);
}

VIO_INLINE void pass2_store(int32x4_t tmp10, int32x4_t tmp11, int32x4_t tmp12, int32x4_t tmp13,
                            int32x4_t tmp0, int32x4_t tmp1, int32x4_t tmp2, int32x4_t tmp3,
                            std::uint8_t** output_buf, unsigned output_col, unsigned buf_offset) {
  int16x8_t c02 = vcombine_s16(vaddhn_s32(tmp10, tmp3), vaddhn_s32(tmp12, tmp1));
  int16x8_t c13 = vcombine_s16(vaddhn_s32(tmp11, tmp2), vaddhn_s32(tmp13, tmp0));
  int16x8_t c46 = vcombine_s16(vsubhn_s32(tmp13, tmp0), vsubhn_s32(tmp11, tmp2));
  int16x8_t c57 = vcombine_s16(vsubhn_s32(tmp12, tmp1), vsubhn_s32(tmp10, tmp3));
  const uint8x8_t center = vdup_n_u8(128);
  uint8x8_t u02 = vadd_u8(vreinterpret_u8_s8(vqrshrn_n_s16(c02, DESCALE_P2 - 16)), center);
  uint8x8_t u13 = vadd_u8(vreinterpret_u8_s8(vqrshrn_n_s16(c13, DESCALE_P2 - 16)), center);
  uint8x8_t u46 = vadd_u8(vreinterpret_u8_s8(vqrshrn_n_s16(c46, DESCALE_P2 - 16)), center);
  uint8x8_t u57 = vadd_u8(vreinterpret_u8_s8(vqrshrn_n_s16(c57, DESCALE_P2 - 16)), center);
  uint8x8x2_t c01_23 = vzip_u8(u02, u13);
  uint8x8x2_t c45_67 = vzip_u8(u46, u57);
  uint16x4x4_t out = {{vreinterpret_u16_u8(c01_23.val[0]), vreinterpret_u16_u8(c01_23.val[1]),
                       vreinterpret_u16_u8(c45_67.val[0]), vreinterpret_u16_u8(c45_67.val[1])}};
  vst4_lane_u16(reinterpret_cast<std::uint16_t*>(output_buf[buf_offset + 0] + output_col), out, 0);
  vst4_lane_u16(reinterpret_cast<std::uint16_t*>(output_buf[buf_offset + 1] + output_col), out, 1);
  vst4_lane_u16(reinterpret_cast<std::uint16_t*>(output_buf[buf_offset + 2] + output_col), out, 2);
  vst4_lane_u16(reinterpret_cast<std::uint16_t*>(output_buf[buf_offset + 3] + output_col), out, 3);
}

VIO_INLINE void pass2_regular(const std::int16_t* ws, std::uint8_t** output_buf,
                              unsigned output_col, unsigned buf_offset) {
  const int16x4x3_t k = load_consts();
  int16x4_t z2_s16 = vld1_s16(ws + 2 * 4);
  int16x4_t z3_s16 = vld1_s16(ws + 6 * 4);
  int32x4_t tmp2 = vmull_lane_s16(z2_s16, k.val[0], 1);
  int32x4_t tmp3 = vmull_lane_s16(z2_s16, k.val[1], 2);
  tmp2 = vmlal_lane_s16(tmp2, z3_s16, k.val[2], 1);
  tmp3 = vmlal_lane_s16(tmp3, z3_s16, k.val[0], 1);
  z2_s16 = vld1_s16(ws + 0 * 4);
  z3_s16 = vld1_s16(ws + 4 * 4);
  int32x4_t tmp0 = vshll_n_s16(vadd_s16(z2_s16, z3_s16), CONST_BITS);
  int32x4_t tmp1 = vshll_n_s16(vsub_s16(z2_s16, z3_s16), CONST_BITS);
  int32x4_t tmp10 = vaddq_s32(tmp0, tmp3), tmp13 = vsubq_s32(tmp0, tmp3);
  int32x4_t tmp11 = vaddq_s32(tmp1, tmp2), tmp12 = vsubq_s32(tmp1, tmp2);
  int16x4_t tmp0_s16 = vld1_s16(ws + 7 * 4);
  int16x4_t tmp1_s16 = vld1_s16(ws + 5 * 4);
  int16x4_t tmp2_s16 = vld1_s16(ws + 3 * 4);
  int16x4_t tmp3_s16 = vld1_s16(ws + 1 * 4);
  z3_s16 = vadd_s16(tmp0_s16, tmp2_s16);
  int16x4_t z4_s16 = vadd_s16(tmp1_s16, tmp3_s16);
  int32x4_t z3 = vmull_lane_s16(z3_s16, k.val[2], 3);
  int32x4_t z4 = vmull_lane_s16(z3_s16, k.val[1], 3);
  z3 = vmlal_lane_s16(z3, z4_s16, k.val[1], 3);
  z4 = vmlal_lane_s16(z4, z4_s16, k.val[2], 0);
  tmp0 = vmull_lane_s16(tmp0_s16, k.val[0], 3);
  tmp1 = vmull_lane_s16(tmp1_s16, k.val[1], 1);
  tmp2 = vmull_lane_s16(tmp2_s16, k.val[2], 2);
  tmp3 = vmull_lane_s16(tmp3_s16, k.val[1], 0);
  tmp0 = vmlsl_lane_s16(tmp0, tmp3_s16, k.val[0], 0);
  tmp1 = vmlsl_lane_s16(tmp1, tmp2_s16, k.val[0], 2);
  tmp2 = vmlsl_lane_s16(tmp2, tmp1_s16, k.val[0], 2);
  tmp3 = vmlsl_lane_s16(tmp3, tmp0_s16, k.val[0], 0);
  tmp0 = vaddq_s32(tmp0, z3);
  tmp1 = vaddq_s32(tmp1, z4);
  tmp2 = vaddq_s32(tmp2, z3);
  tmp3 = vaddq_s32(tmp3, z4);
  pass2_store(tmp10, tmp11, tmp12, tmp13, tmp0, tmp1, tmp2, tmp3, output_buf, output_col,
              buf_offset);
}

VIO_INLINE void pass2_sparse(const std::int16_t* ws, std::uint8_t** output_buf,
                             unsigned output_col, unsigned buf_offset) {
  const int16x4x3_t k = load_consts();
  int16x4_t z2_s16 = vld1_s16(ws + 2 * 4);
  int32x4_t tmp2 = vmull_lane_s16(z2_s16, k.val[0], 1);
  int32x4_t tmp3 = vmull_lane_s16(z2_s16, k.val[1], 2);
  z2_s16 = vld1_s16(ws + 0 * 4);
  int32x4_t tmp0 = vshll_n_s16(z2_s16, CONST_BITS);
  int32x4_t tmp1 = vshll_n_s16(z2_s16, CONST_BITS);
  int32x4_t tmp10 = vaddq_s32(tmp0, tmp3), tmp13 = vsubq_s32(tmp0, tmp3);
  int32x4_t tmp11 = vaddq_s32(tmp1, tmp2), tmp12 = vsubq_s32(tmp1, tmp2);
  int16x4_t tmp2_s16 = vld1_s16(ws + 3 * 4);
  int16x4_t tmp3_s16 = vld1_s16(ws + 1 * 4);
  int16x4_t z3_s16 = tmp2_s16, z4_s16 = tmp3_s16;
  int32x4_t z3 = vmull_lane_s16(z3_s16, k.val[2], 3);
  z3 = vmlal_lane_s16(z3, z4_s16, k.val[1], 3);
  int32x4_t z4 = vmull_lane_s16(z3_s16, k.val[1], 3);
  z4 = vmlal_lane_s16(z4, z4_s16, k.val[2], 0);
  tmp0 = vmlsl_lane_s16(z3, tmp3_s16, k.val[0], 0);
  tmp1 = vmlsl_lane_s16(z4, tmp2_s16, k.val[0], 2);
  tmp2 = vmlal_lane_s16(z3, tmp2_s16, k.val[2], 2);
  tmp3 = vmlal_lane_s16(z4, tmp3_s16, k.val[1], 0);
  pass2_store(tmp10, tmp11, tmp12, tmp13, tmp0, tmp1, tmp2, tmp3, output_buf, output_col,
              buf_offset);
}

}  // namespace detail_idct

// quantptr: 16-bit dequantization multipliers (natural order).
inline void idct_islow(const std::int16_t* quantptr, const std::int16_t* coef_block,
                       std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_idct;
  alignas(16) std::int16_t workspace_l[32];
  alignas(16) std::int16_t workspace_r[32];
  auto load = [](const std::int16_t* p) {
    return Rows{vld1_s16(p + 0 * 8), vld1_s16(p + 1 * 8), vld1_s16(p + 2 * 8), vld1_s16(p + 3 * 8),
                vld1_s16(p + 4 * 8), vld1_s16(p + 5 * 8), vld1_s16(p + 6 * 8), vld1_s16(p + 7 * 8)};
  };
  Rows row = load(coef_block), q = load(quantptr);
  int16x4_t bitmap = vorr_s16(vorr_s16(vorr_s16(row.r7, row.r6), row.r5), row.r4);
  std::int64_t bitmap_rows_4567 = vget_lane_s64(vreinterpret_s64_s16(bitmap), 0);
  if (bitmap_rows_4567 == 0) {
    bitmap = vorr_s16(vorr_s16(vorr_s16(bitmap, row.r3), row.r2), row.r1);
    std::int64_t left_ac = vget_lane_s64(vreinterpret_s64_s16(bitmap), 0);
    if (left_ac == 0) {
      int16x4_t dcval = vshl_n_s16(vmul_s16(row.r0, q.r0), PASS1_BITS);
      int16x4x4_t quadrant = {{dcval, dcval, dcval, dcval}};
      vst4_s16(workspace_l, quadrant);
      vst4_s16(workspace_r, quadrant);
    } else {
      pass1_sparse(row, q, workspace_l, workspace_r);
    }
  } else {
    pass1_regular(row, q, workspace_l, workspace_r);
  }

  row = load(coef_block + 4);
  q = load(quantptr + 4);
  bitmap = vorr_s16(vorr_s16(vorr_s16(row.r7, row.r6), row.r5), row.r4);
  bitmap_rows_4567 = vget_lane_s64(vreinterpret_s64_s16(bitmap), 0);
  bitmap = vorr_s16(vorr_s16(vorr_s16(bitmap, row.r3), row.r2), row.r1);
  std::int64_t right_ac = vget_lane_s64(vreinterpret_s64_s16(bitmap), 0);
  std::int64_t right_ac_dc = 1;
  if (right_ac == 0) {
    bitmap = vorr_s16(bitmap, row.r0);
    right_ac_dc = vget_lane_s64(vreinterpret_s64_s16(bitmap), 0);
    if (right_ac_dc != 0) {
      int16x4_t dcval = vshl_n_s16(vmul_s16(row.r0, q.r0), PASS1_BITS);
      int16x4x4_t quadrant = {{dcval, dcval, dcval, dcval}};
      vst4_s16(workspace_l + 16, quadrant);
      vst4_s16(workspace_r + 16, quadrant);
    }
  } else if (bitmap_rows_4567 == 0) {
    pass1_sparse(row, q, workspace_l + 16, workspace_r + 16);
  } else {
    pass1_regular(row, q, workspace_l + 16, workspace_r + 16);
  }

  if (right_ac_dc == 0) {
    pass2_sparse(workspace_l, output_buf, output_col, 0);
    pass2_sparse(workspace_r, output_buf, output_col, 4);
  } else {
    pass2_regular(workspace_l, output_buf, output_col, 0);
    pass2_regular(workspace_r, output_buf, output_col, 4);
  }
}

}  // namespace vio::jpeg::simd::neon
