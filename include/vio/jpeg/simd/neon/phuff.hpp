// vio::jpeg::simd::neon - coefficient preparation for progressive Huffman
// encoding.  Gathers a spectral band in zig-zag order, computes the
// point-transformed magnitudes and the zero/sign/EOB bitmaps that drive the
// scalar entropy coder.
#pragma once

#include <arm_neon.h>

#include <cstdint>

#include "../../config.hpp"

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

// Calls f(row, coefs) for the eight rows of the band (zero beyond Sl).
template <class F>
VIO_INLINE void for_each_row(const std::int16_t* block, const int* order, int Sl, F&& f) {
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
