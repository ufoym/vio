// vio::jpeg::simd::neon - coefficient preparation for progressive Huffman
// encoding.  Gathers a spectral band in zig-zag order, computes the
// point-transformed magnitudes and the zero/sign/EOB bitmaps that drive the
// scalar entropy coder.
#pragma once

#include <arm_neon.h>

#include <cstdint>

#include "../../config.hpp"
#include "huff.hpp"

namespace vio::jpeg::simd::neon {

namespace detail_phuff {

// Loads block[order[L..n-1]] into lanes L.. of v (lanes >= n are untouched).
template <int L>
VIO_INLINE int16x8_t gather(int16x8_t v, const std::int16_t* block, const int* order, int n) {
  if constexpr (L < 8) {
    if (L < n) return gather<L + 1>(vld1q_lane_s16(block + order[L], v, L), block, order, n);
  }
  return v;
}

#if VIO_SIMD_NEON_A64
// Rows of the band starting at zig-zag position S (lanes past Sl zeroed),
// from the whole block put in zig-zag order with table lookups.
template <int S, class F>
VIO_INLINE void band_rows(const std::int16_t* block, int Sl, F&& f) {
  using namespace detail_huff;
  const uint8x16x4_t idx_0123 = {{vld1q_u8(zigzag_idx + 0), vld1q_u8(zigzag_idx + 16),
                                  vld1q_u8(zigzag_idx + 32), vld1q_u8(zigzag_idx + 48)}};
  const uint8x16x4_t idx_4567 = {{vld1q_u8(zigzag_idx + 64), vld1q_u8(zigzag_idx + 80),
                                  vld1q_u8(zigzag_idx + 96), vld1q_u8(zigzag_idx + 112)}};
  const auto* b8 = reinterpret_cast<const std::int8_t*>(block);
  const int8x16x4_t t0123 = {{vld1q_s8(b8 + 0), vld1q_s8(b8 + 16), vld1q_s8(b8 + 32),
                              vld1q_s8(b8 + 48)}};
  const int8x16x4_t t4567 = {{vld1q_s8(b8 + 64), vld1q_s8(b8 + 80), vld1q_s8(b8 + 96),
                              vld1q_s8(b8 + 112)}};
  const int8x16x4_t t2345 = {{t0123.val[2], t0123.val[3], t4567.val[0], t4567.val[1]}};
  const int8x16x3_t t567 = {{t4567.val[1], t4567.val[2], t4567.val[3]}};
  int16x8_t z0 = vreinterpretq_s16_s8(vqtbl4q_s8(t0123, idx_0123.val[0]));
  int16x8_t z1 = vreinterpretq_s16_s8(vqtbl4q_s8(t0123, idx_0123.val[1]));
  int16x8_t z2 = vreinterpretq_s16_s8(vqtbl4q_s8(t2345, idx_0123.val[2]));
  int16x8_t z3 = vreinterpretq_s16_s8(vqtbl4q_s8(t0123, idx_0123.val[3]));
  int16x8_t z4 = vreinterpretq_s16_s8(vqtbl4q_s8(t4567, idx_4567.val[0]));
  int16x8_t z5 = vreinterpretq_s16_s8(vqtbl4q_s8(t2345, idx_4567.val[1]));
  int16x8_t z6 = vreinterpretq_s16_s8(vqtbl4q_s8(t4567, idx_4567.val[2]));
  int16x8_t z7 = vreinterpretq_s16_s8(vqtbl3q_s8(t567, idx_4567.val[3]));
  z1 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t4567.val[0]), 0), z1, 2);
  z2 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t0123.val[1]), 4), z2, 0);
  z2 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t4567.val[2]), 0), z2, 5);
  z5 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t0123.val[1]), 7), z5, 2);
  z5 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t4567.val[2]), 3), z5, 7);
  z6 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t0123.val[3]), 7), z6, 5);
  const int16x8_t zero = vdupq_n_s16(0);
  const int16x8_t lane = {0, 1, 2, 3, 4, 5, 6, 7}, sl = vdupq_n_s16(static_cast<std::int16_t>(Sl));
  auto row = [&](int r, int16x8_t a, int16x8_t b) VIO_LAMBDA_INLINE {
    const uint16x8_t in = vcltq_s16(vaddq_s16(lane, vdupq_n_s16(static_cast<std::int16_t>(8 * r))), sl);
    f(r, vandq_s16(vextq_s16(a, b, S), vreinterpretq_s16_u16(in)));
  };
  row(0, z0, z1);
  row(1, z1, z2);
  row(2, z2, z3);
  row(3, z3, z4);
  row(4, z4, z5);
  row(5, z5, z6);
  row(6, z6, z7);
  row(7, z7, zero);
}
#endif

// Calls f(row, coefs) for the eight rows of the band (zero beyond Sl).
template <class F>
VIO_INLINE void for_each_row(const std::int16_t* block, const int* order, int Sl, F&& f) {
#if VIO_SIMD_NEON_A64
  // The bands of the usual scripts start at zig-zag position 1 or 6
  // (natural index 1 or 3).
  if (order[0] == 1) return band_rows<1>(block, Sl, f);
  if (order[0] == 3) return band_rows<6>(block, Sl, f);
#endif
  int row = 0;
  for (; row < Sl / 8; row++) f(row, gather<0>(vdupq_n_s16(0), block, order + row * 8, 8));
  if (Sl % 8) {
    f(row, gather<0>(vdupq_n_s16(0), block, order + row * 8, Sl % 8));
    row++;
  }
  for (; row < 8; row++) f(row, vdupq_n_s16(0));
}

// Packs eight rows of 0x00/0xFF byte flags into a 64-bit mask (bit k = coef k).
VIO_INLINE std::uint64_t pack_bits(const uint8x8_t* rows) {
  const uint8x8_t mask = vreinterpret_u8_u64(vmov_n_u64(0x8040201008040201ULL));
  uint8x8_t r01 = vpadd_u8(vand_u8(rows[0], mask), vand_u8(rows[1], mask));
  uint8x8_t r23 = vpadd_u8(vand_u8(rows[2], mask), vand_u8(rows[3], mask));
  uint8x8_t r45 = vpadd_u8(vand_u8(rows[4], mask), vand_u8(rows[5], mask));
  uint8x8_t r67 = vpadd_u8(vand_u8(rows[6], mask), vand_u8(rows[7], mask));
  uint8x8_t all = vpadd_u8(vpadd_u8(r01, r23), vpadd_u8(r45, r67));
  return vget_lane_u64(vreinterpret_u64_u8(all), 0);
}

}  // namespace detail_phuff

// AC first pass: values[k] = |coef_k| >> Al, values[64 + k] = the value's
// bits to emit (ones' complement for negative coefficients).  Returns the
// bitmap of non-zero values.
inline VIO_NO_STACK_CHECK std::uint64_t ac_first_prepare(const std::int16_t* block, const int* order, int Sl, int Al,
                                      std::uint16_t* values) {
  using namespace detail_phuff;
  const int16x8_t shift = vdupq_n_s16(static_cast<std::int16_t>(-Al));
  uint8x8_t nz[8];
  for_each_row(block, order, Sl, [&](int row, int16x8_t coefs) VIO_LAMBDA_INLINE {
    uint16x8_t sign = vreinterpretq_u16_s16(vshrq_n_s16(coefs, 15));
    uint16x8_t abs = vshlq_u16(vreinterpretq_u16_s16(vabsq_s16(coefs)), shift);
    vst1q_u16(values + row * 8, abs);
    vst1q_u16(values + 64 + row * 8, veorq_u16(abs, sign));
    nz[row] = vmovn_u16(vtstq_u16(abs, abs));
  });
  return pack_bits(nz);
}

// AC refinement pass: absvalues[k] = |coef_k| >> Al; *zerobits = non-zero
// bitmap, *signbits = bitmap of non-negative coefficients.  Returns the index
// of the last coefficient whose magnitude is 1 (0 if none).
inline VIO_NO_STACK_CHECK int ac_refine_prepare(const std::int16_t* block, const int* order, int Sl, int Al,
                             std::uint16_t* absvalues, std::uint64_t* zerobits,
                             std::uint64_t* signbits) {
  using namespace detail_phuff;
  const int16x8_t shift = vdupq_n_s16(static_cast<std::int16_t>(-Al));
  uint8x8_t nz[8], neg[8], eq1[8];
  for_each_row(block, order, Sl, [&](int row, int16x8_t coefs) VIO_LAMBDA_INLINE {
    neg[row] = vmovn_u16(vreinterpretq_u16_s16(vshrq_n_s16(coefs, 15)));
    uint16x8_t abs = vshlq_u16(vreinterpretq_u16_s16(vabsq_s16(coefs)), shift);
    vst1q_u16(absvalues + row * 8, abs);
    nz[row] = vmovn_u16(vtstq_u16(abs, abs));
    eq1[row] = vmovn_u16(vceqq_u16(abs, vdupq_n_u16(1)));
  });
  *zerobits = pack_bits(nz);
  *signbits = ~pack_bits(neg);
  std::uint64_t eob = pack_bits(eq1);
  return eob == 0 ? 0 : 63 - __builtin_clzll(eob);
}

}  // namespace vio::jpeg::simd::neon
