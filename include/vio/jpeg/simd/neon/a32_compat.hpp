// vio::jpeg::simd::neon - AArch64-only intrinsics with AArch32 equivalents,
// so that the NEON kernels also build for 32-bit Arm with NEON enabled.
#pragma once

#include <arm_neon.h>

#include "../../config.hpp"

namespace vio::jpeg::simd::neon::compat {

#if VIO_SIMD_NEON_A64
template <int L>
VIO_INLINE uint32x4_t mull_laneq(uint16x4_t a, uint16x8_t v) { return vmull_laneq_u16(a, v, L); }
template <int L>
VIO_INLINE uint32x4_t mlal_laneq(uint32x4_t acc, uint16x4_t a, uint16x8_t v) {
  return vmlal_laneq_u16(acc, a, v, L);
}
template <int L>
VIO_INLINE uint32x4_t mlsl_laneq(uint32x4_t acc, uint16x4_t a, uint16x8_t v) {
  return vmlsl_laneq_u16(acc, a, v, L);
}
VIO_INLINE uint8x16_t tbl1q(uint8x16_t table, uint8x16_t idx) { return vqtbl1q_u8(table, idx); }
VIO_INLINE uint8x16_t uzp1q_u8(uint8x16_t a, uint8x16_t b) { return vuzp1q_u8(a, b); }
VIO_INLINE uint8x16_t paddq_u8(uint8x16_t a, uint8x16_t b) { return vpaddq_u8(a, b); }
VIO_INLINE uint16x8_t cltzq_s16(int16x8_t a) { return vcltzq_s16(a); }
VIO_INLINE unsigned addv_u8(uint8x8_t a) { return vaddv_u8(a); }
#else
template <int L>
VIO_INLINE uint16x4_t lane_half(uint16x8_t v) {
  return L < 4 ? vget_low_u16(v) : vget_high_u16(v);
}
template <int L>
VIO_INLINE uint32x4_t mull_laneq(uint16x4_t a, uint16x8_t v) {
  return vmull_lane_u16(a, lane_half<L>(v), L & 3);
}
template <int L>
VIO_INLINE uint32x4_t mlal_laneq(uint32x4_t acc, uint16x4_t a, uint16x8_t v) {
  return vmlal_lane_u16(acc, a, lane_half<L>(v), L & 3);
}
template <int L>
VIO_INLINE uint32x4_t mlsl_laneq(uint32x4_t acc, uint16x4_t a, uint16x8_t v) {
  return vmlsl_lane_u16(acc, a, lane_half<L>(v), L & 3);
}
VIO_INLINE uint8x16_t tbl1q(uint8x16_t table, uint8x16_t idx) {
  uint8x8x2_t t = {{vget_low_u8(table), vget_high_u8(table)}};
  return vcombine_u8(vtbl2_u8(t, vget_low_u8(idx)), vtbl2_u8(t, vget_high_u8(idx)));
}
VIO_INLINE uint8x16_t uzp1q_u8(uint8x16_t a, uint8x16_t b) { return vuzpq_u8(a, b).val[0]; }
VIO_INLINE uint8x16_t paddq_u8(uint8x16_t a, uint8x16_t b) {
  return vcombine_u8(vpadd_u8(vget_low_u8(a), vget_high_u8(a)),
                     vpadd_u8(vget_low_u8(b), vget_high_u8(b)));
}
VIO_INLINE uint16x8_t cltzq_s16(int16x8_t a) { return vreinterpretq_u16_s16(vshrq_n_s16(a, 15)); }
VIO_INLINE unsigned addv_u8(uint8x8_t a) {
  return static_cast<unsigned>(vget_lane_u64(vpaddl_u32(vpaddl_u16(vpaddl_u8(a))), 0));
}
#endif

}  // namespace vio::jpeg::simd::neon::compat
