// vio::jpeg::simd::sse - floating-point DCT kernels for 32-bit x86 CPUs with
// SSE but not SSE2.  The arithmetic is the SSE2 kernels' (float_common.inc);
// only the integer <-> float conversions, which need SSE2 or MMX, are done
// with scalar code reproducing cvtps2dq/packssdw semantics, so results are
// identical to the SSE2 kernels'.
#pragma once

#include <xmmintrin.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "../../config.hpp"

VIO_TARGET_PUSH_("sse")
namespace vio::jpeg::simd::sse {

#include "float_common.inc"

namespace detail_sse {
// cvtps2dq: round to nearest (current rounding mode), 0x80000000 if out of range
VIO_INLINE std::int32_t cvt(float v) {
  if (!(v > -2147483648.0f && v < 2147483648.0f)) return INT32_MIN;
  return static_cast<std::int32_t>(std::nearbyint(v));
}
VIO_INLINE std::int16_t sat16(std::int32_t v) {
  return static_cast<std::int16_t>(v < -32768 ? -32768 : (v > 32767 ? 32767 : v));
}
}  // namespace detail_sse

inline void convsamp_float(std::uint8_t** sample_data, unsigned start_col, float* workspace) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) workspace[r * 8 + c] = static_cast<float>(sample_data[r][start_col + c] - 128);
}

inline void quantize_float(std::int16_t* coef_block, const float* divisors, const float* workspace) {
  alignas(16) float t[64];
  for (int i = 0; i < 64; i += 4)
    _mm_storeu_ps(t + i, _mm_mul_ps(_mm_loadu_ps(workspace + i), _mm_loadu_ps(divisors + i)));
  for (int i = 0; i < 64; i++) coef_block[i] = detail_sse::sat16(detail_sse::cvt(t[i]));
}

inline void idct_float(const float* quantptr, const std::int16_t* coef_block,
                       std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_flt;
  alignas(16) float ws[64], in[64];
  for (int i = 0; i < 64; i++) in[i] = static_cast<float>(coef_block[i]);
  F c[8];
  for (int half = 0; half < 2; half++) {  // pass 1: columns
    for (int k = 0; k < 8; k++)
      c[k] = _mm_mul_ps(_mm_loadu_ps(in + k * 8 + half * 4), _mm_loadu_ps(quantptr + k * 8 + half * 4));
    idct_pass(c);
    for (int k = 0; k < 8; k++) _mm_storeu_ps(ws + k * 8 + half * 4, c[k]);
  }
  for (int half = 0; half < 2; half++) {  // pass 2: rows
    load_cols(ws + half * 32, c);
    idct_pass(c);
    store_cols(ws + half * 32, c);
  }
  // Descale by 8 via the magic number 1.5 * 2^26 (low 16 bits of the sum's
  // representation), then clamp and recenter, as the SSE2 kernel does.
  const F magic = _mm_set1_ps(100663296.0f);
  for (int i = 0; i < 64; i += 4) _mm_storeu_ps(ws + i, _mm_add_ps(_mm_loadu_ps(ws + i), magic));
  for (int r = 0; r < 8; r++) {
    for (int c2 = 0; c2 < 8; c2++) {
      std::uint32_t bits;
      std::memcpy(&bits, ws + r * 8 + c2, 4);
      int v = static_cast<std::int16_t>(bits & 0xFFFF);
      v = v < -128 ? -128 : (v > 127 ? 127 : v);
      output_buf[r][output_col + c2] = static_cast<std::uint8_t>(v + 128);
    }
  }
}

}  // namespace vio::jpeg::simd::sse
VIO_TARGET_POP()
