// vio::jpeg::simd::sse2 - floating-point DCT kernels (the FDCT and the
// 1-D passes are in float_common.inc).  No fused multiply-adds: results are
// the "sse" variant of the floating-point test expectations.

#include "float_common.inc"


inline void convsamp_float(std::uint8_t** sample_data, unsigned start_col, float* workspace) {
  const __m128i zero = _mm_setzero_si128(), center = _mm_set1_epi16(128);
  for (int r = 0; r < 8; r++) {
    __m128i s = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(sample_data[r] + start_col));
    __m128i w = _mm_sub_epi16(_mm_unpacklo_epi8(s, zero), center);
    __m128i lo = _mm_srai_epi32(_mm_unpacklo_epi16(w, w), 16);
    __m128i hi = _mm_srai_epi32(_mm_unpackhi_epi16(w, w), 16);
    _mm_storeu_ps(workspace + r * 8, _mm_cvtepi32_ps(lo));
    _mm_storeu_ps(workspace + r * 8 + 4, _mm_cvtepi32_ps(hi));
  }
}


// coef = round-to-nearest-even(workspace * divisor), saturated to 16 bits.
inline void quantize_float(std::int16_t* coef_block, const float* divisors, const float* workspace) {
  for (int i = 0; i < 64; i += 8) {
    __m128i a = _mm_cvtps_epi32(_mm_mul_ps(_mm_loadu_ps(workspace + i), _mm_loadu_ps(divisors + i)));
    __m128i b = _mm_cvtps_epi32(_mm_mul_ps(_mm_loadu_ps(workspace + i + 4), _mm_loadu_ps(divisors + i + 4)));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(coef_block + i), _mm_packs_epi32(a, b));
  }
}

inline void idct_float(const float* quantptr, const std::int16_t* coef_block,
                       std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_flt;
  alignas(16) float ws[64];
  F c[8];
  for (int half = 0; half < 2; half++) {  // pass 1: columns (4 at a time)
    for (int k = 0; k < 8; k++) {
      __m128i v = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(coef_block + k * 8 + half * 4));
      v = _mm_srai_epi32(_mm_unpacklo_epi16(v, v), 16);
      c[k] = _mm_mul_ps(_mm_cvtepi32_ps(v), _mm_loadu_ps(quantptr + k * 8 + half * 4));
    }
    idct_pass(c);
    for (int k = 0; k < 8; k++) _mm_storeu_ps(ws + k * 8 + half * 4, c[k]);
  }
  for (int half = 0; half < 2; half++) {  // pass 2: rows (4 at a time)
    load_cols(ws + half * 32, c);
    idct_pass(c);
    store_cols(ws + half * 32, c);
  }
  // Descale by 8 with round-to-nearest-even via the float magic number
  // 1.5 * 2^26, then clamp and recenter.
  const F magic = _mm_set1_ps(100663296.0f);
  const __m128i center = _mm_set1_epi8(static_cast<char>(-128));
  for (int r = 0; r < 8; r += 2) {
    __m128i v[4];
    for (int j = 0; j < 4; j++) {
      __m128i x = _mm_castps_si128(_mm_add_ps(_mm_loadu_ps(ws + r * 8 + j * 4), magic));
      v[j] = _mm_srai_epi32(_mm_slli_epi32(x, 16), 16);
    }
    __m128i b = _mm_add_epi8(_mm_packs_epi16(_mm_packs_epi32(v[0], v[1]), _mm_packs_epi32(v[2], v[3])),
                             center);
    _mm_storel_epi64(reinterpret_cast<__m128i*>(output_buf[r] + output_col), b);
    _mm_storel_epi64(reinterpret_cast<__m128i*>(output_buf[r + 1] + output_col), _mm_srli_si128(b, 8));
  }
}
