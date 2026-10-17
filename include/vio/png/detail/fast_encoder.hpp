// vio::png - single-pass PNG image data encoder.
//
// FastEncoder turns rows of PNG samples (big-endian 16-bit samples) into
// the zlib stream of an image's IDAT data, trading some compression for
// speed:
//   - each row's filter is the one whose residuals have the smallest sum of
//     absolute values, the five candidates being computed with SIMD;
//   - one DEFLATE block holds the whole image, with a Huffman code built
//     from the residuals of a sample of rows spread over the image;
//   - the only matches are runs of zero residuals (distance 1).
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "checksum.hpp"
#include "filter.hpp"
#include "../simd/vec.hpp"

#if VIO_SIMD_X86
#include <immintrin.h>
#endif

namespace vio::png::detail {

// ---------------------------------------------------------------------------
// Filters and filter selection
// ---------------------------------------------------------------------------

inline unsigned ctz32(std::uint32_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long r;
  _BitScanForward(&r, v);
  return static_cast<unsigned>(r);
#else
  return static_cast<unsigned>(__builtin_ctz(v));
#endif
}
inline unsigned clz32(std::uint32_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long r;
  _BitScanReverse(&r, v);
  return 31 - static_cast<unsigned>(r);
#else
  return static_cast<unsigned>(__builtin_clz(v));
#endif
}

// Rows are kept with kRowPad zero bytes before their first sample, so that
// the left neighbors of the first pixel read as zero.
inline constexpr std::size_t kRowPad = 16;

// The filter with the smallest cost among the allowed ones (bit f of
// `allowed` for filter type f).
inline unsigned pick_filter(const std::uint64_t* cost, unsigned allowed) {
  unsigned best = 5;
  for (unsigned f = 0; f < 5; f++)
    if ((allowed >> f) & 1)
      if (best == 5 || cost[f] < cost[best]) best = f;
  return best == 5 ? 0 : best;
}

// Paeth predictor and absolute value of a signed byte residual written
// without branches, so that GCC and Clang vectorize the loops below (8-bit
// arithmetic as in the vector kernels: pc is |pa - pb| when a - c and b - c
// differ in sign, else pa + pb saturated).  MSVC does not vectorize them,
// and its scalar code is faster with int arithmetic (VIO_PNG_SCALAR_INT=1
// tests that form with other compilers).
#if defined(_MSC_VER) && !defined(__clang__) && !defined(VIO_PNG_SCALAR_INT)
#define VIO_PNG_SCALAR_INT 1
#endif
#if VIO_PNG_SCALAR_INT
// (selections by arithmetic masks: MSVC vectorizes no loop with ?:)
VIO_INLINE int paeth_select_int(int a, int b, int c) {
  const int pa = std::abs(b - c), pb = std::abs(a - c), pc = std::abs(a + b - 2 * c);
  const int use_b = ~((pc - pb) >> 31), use_a = ~((pb - pa) >> 31) & ~((pc - pa) >> 31);
  const int bc = c ^ ((b ^ c) & use_b);
  return bc ^ ((a ^ bc) & use_a);
}
VIO_INLINE std::uint8_t paeth_select(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
  return static_cast<std::uint8_t>(paeth_select_int(a, b, c));
}
#else
VIO_INLINE std::uint8_t paeth_select(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
  const std::uint8_t pa = static_cast<std::uint8_t>(b > c ? b - c : c - b);
  const std::uint8_t pb = static_cast<std::uint8_t>(a > c ? a - c : c - a);
  const std::uint8_t d = static_cast<std::uint8_t>(pa > pb ? pa - pb : pb - pa);
  const std::uint8_t sum = static_cast<std::uint8_t>(pa + pb);
  const std::uint8_t pc = (a < c) != (b < c) ? d : sum < pa ? std::uint8_t(255) : sum;
  const std::uint8_t bc = pb <= pc ? b : c;
  return pa <= pb && pa <= pc ? a : bc;
}
#endif
// floor((a + b) / 2)
VIO_INLINE std::uint8_t avg_floor(std::uint8_t a, std::uint8_t b) {
  return static_cast<std::uint8_t>((a & b) + ((a ^ b) >> 1));
}
VIO_INLINE std::uint8_t abs_byte(std::uint8_t v) {
  const std::uint8_t w = static_cast<std::uint8_t>(0u - v);
  return v < w ? v : w;
}
#if VIO_PNG_SCALAR_INT
// |r| of the residual r (mod 256) as a signed byte
VIO_INLINE int abs_residual(int r) { return std::abs(static_cast<int>(static_cast<unsigned>(r) << 24) >> 24); }

// Adds the sums of absolute residuals of n bytes x for the five filters
// (a, b, c: the bytes left, above and above left) to cost[0..4]; in the
// form MSVC vectorizes (no ?:, operands through distinct pointers).
// The Paeth residuals are also stored in paeth[0..n), which saves filtering
// the row again when Paeth is chosen (the most common choice).
inline void filter_costs_int(const std::uint8_t* VIO_RESTRICT x, const std::uint8_t* VIO_RESTRICT a,
                             const std::uint8_t* VIO_RESTRICT b, const std::uint8_t* VIO_RESTRICT c, int n,
                             std::uint64_t* cost, std::uint8_t* VIO_RESTRICT paeth) {
  int c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0;  // n < 2^23
  for (int i = 0; i < n; i++) {
    const int xi = x[i], ai = a[i], bi = b[i], ci = c[i];
    const int r4 = xi - paeth_select_int(ai, bi, ci);
    paeth[i] = static_cast<std::uint8_t>(r4);
    c0 += abs_residual(xi);
    c1 += abs_residual(xi - ai);
    c2 += abs_residual(xi - bi);
    c3 += abs_residual(xi - ((ai + bi) >> 1));
    c4 += abs_residual(r4);
  }
  cost[0] += static_cast<std::uint32_t>(c0), cost[1] += static_cast<std::uint32_t>(c1);
  cost[2] += static_cast<std::uint32_t>(c2), cost[3] += static_cast<std::uint32_t>(c3);
  cost[4] += static_cast<std::uint32_t>(c4);
}
#endif

#if !defined(VIO_PNG_SCALAR_I16) && !VIO_PNG_SCALAR_INT && defined(__clang__) && \
    (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))
// On x86, whose SSE2 has signed 16-bit minimum, maximum and comparisons but
// no unsigned 8-bit comparisons, Clang vectorizes the scalar code below
// best on 16-bit values.
#define VIO_PNG_SCALAR_I16 1
#endif
#if VIO_PNG_SCALAR_I16
// Paeth with t = 3c - a - b: max(a, b) if t <= min(a, b), else min(a, b)
// if max(a, b) <= t, else c.
VIO_INLINE std::int16_t paeth_select16(std::int16_t a, std::int16_t b, std::int16_t c) {
  const std::int16_t t = static_cast<std::int16_t>(3 * c - a - b);
  const std::int16_t lo = std::min(a, b), hi = std::max(a, b);
  const std::int16_t t0 = hi <= t ? lo : c;
  return t <= lo ? hi : t0;
}
// |r| of a residual r (-255..255, mod 256) as a signed byte
VIO_INLINE std::int16_t abs_residual16(std::int16_t r) {
  const std::int16_t m = std::max(r, static_cast<std::int16_t>(-r));
  return std::min(m, static_cast<std::int16_t>(256 - m));
}
#endif

// Computes the residuals of all five filters for one row and returns the
// filter whose residuals have the smallest sum of absolute values (as
// signed bytes).  cur and up point at padded rows (up is all zeros for the
// first row).
// (VIO_PNG_SCALAR_INT: with paeth, the Paeth residuals are stored there.)
inline unsigned choose_filter_scalar(const std::uint8_t* cur, const std::uint8_t* up, std::size_t n,
                                  unsigned bpp, unsigned allowed = 0x1f,
                                  [[maybe_unused]] std::uint8_t* paeth = nullptr) {
  std::uint64_t cost[5] = {};
#if VIO_PNG_SCALAR_INT
  std::uint8_t scratch[4096];
  for (std::size_t i = 0; i < n; i += sizeof(scratch)) {
    const int m = static_cast<int>(std::min(n - i, sizeof(scratch)));
    filter_costs_int(cur + i, cur + i - bpp, up + i, up + i - bpp, m, cost, paeth ? paeth + i : scratch);
  }
#elif VIO_PNG_SCALAR_I16
  for (std::size_t i = 0; i < n;) {
    // 16-bit sums over at most 256 bytes
    const std::size_t end = std::min(n, i + 256);
    std::uint16_t c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0;
    for (; i < end; i++) {
      const std::int16_t x = cur[i], a = cur[i - bpp], b = up[i], c = up[i - bpp];
      c0 = static_cast<std::uint16_t>(c0 + abs_residual16(x));
      c1 = static_cast<std::uint16_t>(c1 + abs_residual16(static_cast<std::int16_t>(x - a)));
      c2 = static_cast<std::uint16_t>(c2 + abs_residual16(static_cast<std::int16_t>(x - b)));
      c3 = static_cast<std::uint16_t>(c3 + abs_residual16(static_cast<std::int16_t>(x - ((a + b) >> 1))));
      c4 = static_cast<std::uint16_t>(c4 + abs_residual16(static_cast<std::int16_t>(x - paeth_select16(a, b, c))));
    }
    cost[0] += c0, cost[1] += c1, cost[2] += c2, cost[3] += c3, cost[4] += c4;
  }
#else
  for (std::size_t i = 0; i < n;) {
    // 16-bit sums over at most 256 bytes
    const std::size_t end = std::min(n, i + 256);
    std::uint16_t c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0;
    for (; i < end; i++) {
      const std::uint8_t x = cur[i], a = cur[i - bpp], b = up[i], c = up[i - bpp];
      // residuals as bytes (wider types make Clang widen the vectors)
      c0 = static_cast<std::uint16_t>(c0 + abs_byte(x));
      c1 = static_cast<std::uint16_t>(c1 + abs_byte(static_cast<std::uint8_t>(x - a)));
      c2 = static_cast<std::uint16_t>(c2 + abs_byte(static_cast<std::uint8_t>(x - b)));
      c3 = static_cast<std::uint16_t>(c3 + abs_byte(static_cast<std::uint8_t>(x - avg_floor(a, b))));
      c4 = static_cast<std::uint16_t>(c4 + abs_byte(static_cast<std::uint8_t>(x - paeth_select(a, b, c))));
    }
    cost[0] += c0, cost[1] += c1, cost[2] += c2, cost[3] += c3, cost[4] += c4;
  }
#endif
  return pick_filter(cost, allowed);
}

// (operands through distinct pointers, which lets MSVC vectorize the loops)
inline void apply_filter_scalar(unsigned filter, std::uint8_t* VIO_RESTRICT out, const std::uint8_t* VIO_RESTRICT x,
                                const std::uint8_t* VIO_RESTRICT b, std::size_t n, unsigned bpp) {
  const std::uint8_t* VIO_RESTRICT a = x - bpp;
  const std::uint8_t* VIO_RESTRICT c = b - bpp;
  switch (filter) {
    case kFilterNone:
      std::memcpy(out, x, n);
      break;
    case kFilterSub:
      for (std::size_t i = 0; i < n; i++) out[i] = static_cast<std::uint8_t>(x[i] - a[i]);
      break;
    case kFilterUp:
      for (std::size_t i = 0; i < n; i++) out[i] = static_cast<std::uint8_t>(x[i] - b[i]);
      break;
    case kFilterAvg:
      for (std::size_t i = 0; i < n; i++) out[i] = static_cast<std::uint8_t>(x[i] - ((a[i] + b[i]) >> 1));
      break;
    case kFilterPaeth:
      for (std::size_t i = 0; i < n; i++) out[i] = static_cast<std::uint8_t>(x[i] - paeth_select(a[i], b[i], c[i]));
      break;
  }
}

#if VIO_SIMD_NEON
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_PUSH_("fpu=neon")
#endif
// Paeth predictor for 16 lanes.
VIO_INLINE uint8x16_t paeth_predict_neon(uint8x16_t a, uint8x16_t b, uint8x16_t c) {
  uint8x16_t pa = vabdq_u8(b, c), pb = vabdq_u8(a, c);
  uint8x16_t diff_sign = veorq_u8(vcltq_u8(a, c), vcltq_u8(b, c));
  uint8x16_t pc = vbslq_u8(diff_sign, vabdq_u8(pa, pb), vqaddq_u8(pa, pb));
  uint8x16_t use_a = vandq_u8(vcleq_u8(pa, pb), vcleq_u8(pa, pc));
  uint8x16_t use_b = vcleq_u8(pb, pc);
  return vbslq_u8(use_a, a, vbslq_u8(use_b, b, c));
}

VIO_INLINE uint8x16_t abs_residual_neon(uint8x16_t r) {
  return vreinterpretq_u8_s8(vabsq_s8(vreinterpretq_s8_u8(r)));
}

inline unsigned choose_filter_neon(const std::uint8_t* cur, const std::uint8_t* up, std::size_t n,
                                  unsigned bpp, unsigned allowed = 0x1f,
                                  std::uint8_t* paeth_out = nullptr) {
  uint32x4_t total[5];
  for (auto& t : total) t = vdupq_n_u32(0);
  std::size_t i = 0;
  while (i + 16 <= n) {
    // 16-bit accumulators for at most 64 vectors (each lane gains <= 256
    // per vector), then widened.
    uint16x8_t acc[5];
    for (auto& a : acc) a = vdupq_n_u16(0);
    std::size_t end = std::min(n & ~std::size_t(15), i + 64 * 16);
    for (; i < end; i += 16) {
      uint8x16_t x = vld1q_u8(cur + i), a = vld1q_u8(cur + i - bpp), b = vld1q_u8(up + i),
                 c = vld1q_u8(up + i - bpp);
      acc[0] = vpadalq_u8(acc[0], abs_residual_neon(x));
      acc[1] = vpadalq_u8(acc[1], abs_residual_neon(vsubq_u8(x, a)));
      acc[2] = vpadalq_u8(acc[2], abs_residual_neon(vsubq_u8(x, b)));
      acc[3] = vpadalq_u8(acc[3], abs_residual_neon(vsubq_u8(x, vhaddq_u8(a, b))));
      const uint8x16_t r4 = vsubq_u8(x, paeth_predict_neon(a, b, c));
      if (paeth_out) vst1q_u8(paeth_out + i, r4);
      acc[4] = vpadalq_u8(acc[4], abs_residual_neon(r4));
    }
    for (int f = 0; f < 5; f++) total[f] = vpadalq_u16(total[f], acc[f]);
  }
  std::uint64_t cost[5];
  for (int f = 0; f < 5; f++) {
    const uint64x2_t t = vpaddlq_u32(total[f]);
    cost[f] = vgetq_lane_u64(t, 0) + vgetq_lane_u64(t, 1);
  }
  for (; i < n; i++) {
    std::uint8_t x = cur[i], a = cur[i - bpp], b = up[i], c = up[i - bpp];
    std::uint8_t r[5] = {x, static_cast<std::uint8_t>(x - a), static_cast<std::uint8_t>(x - b),
                         static_cast<std::uint8_t>(x - ((a + b) >> 1)),
                         static_cast<std::uint8_t>(x - paeth(a, b, c))};
    if (paeth_out) paeth_out[i] = r[4];
    for (int f = 0; f < 5; f++) cost[f] += r[f] < 128 ? r[f] : 256 - r[f];
  }
  return pick_filter(cost, allowed);
}

inline void apply_filter_neon(unsigned filter, std::uint8_t* out, const std::uint8_t* cur,
                              const std::uint8_t* up, std::size_t n, unsigned bpp) {
  std::size_t i = 0;
  switch (filter) {
    case kFilterNone:
      std::memcpy(out, cur, n);
      return;
    case kFilterSub:
      for (; i + 16 <= n; i += 16) vst1q_u8(out + i, vsubq_u8(vld1q_u8(cur + i), vld1q_u8(cur + i - bpp)));
      break;
    case kFilterUp:
      for (; i + 16 <= n; i += 16) vst1q_u8(out + i, vsubq_u8(vld1q_u8(cur + i), vld1q_u8(up + i)));
      break;
    case kFilterAvg:
      for (; i + 16 <= n; i += 16)
        vst1q_u8(out + i, vsubq_u8(vld1q_u8(cur + i), vhaddq_u8(vld1q_u8(cur + i - bpp), vld1q_u8(up + i))));
      break;
    case kFilterPaeth:
      for (; i + 16 <= n; i += 16)
        vst1q_u8(out + i, vsubq_u8(vld1q_u8(cur + i),
                                   paeth_predict_neon(vld1q_u8(cur + i - bpp), vld1q_u8(up + i),
                                                      vld1q_u8(up + i - bpp))));
      break;
  }
  apply_filter_scalar(filter, out + i, cur + i, up + i, n - i, bpp);
}
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_POP()
#endif
#endif

#if VIO_SIMD_X86
VIO_TARGET_PUSH_SSE2()
VIO_INLINE __m128i loadu_sse2(const std::uint8_t* p) {
  return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
}
// floor((a + b) / 2)
VIO_INLINE __m128i avg_floor_sse2(__m128i a, __m128i b) {
  return _mm_sub_epi8(_mm_avg_epu8(a, b), _mm_and_si128(_mm_xor_si128(a, b), _mm_set1_epi8(1)));
}
// Paeth predictor: with lo = min(a, b) and hi = max(a, b), the prediction is
// lo, hi or c depending on where c lies (equal to the definition, which
// prefers a, then b, then c on ties).
VIO_INLINE __m128i paeth_predict_sse2(__m128i a, __m128i b, __m128i c) {
  const __m128i z = _mm_setzero_si128();
  const __m128i ca = _mm_subs_epu8(c, a), cb = _mm_subs_epu8(c, b);
  const __m128i pa = _mm_or_si128(_mm_subs_epu8(b, c), cb), pb = _mm_or_si128(_mm_subs_epu8(a, c), ca);
  // a < c differs from b < c (x < c where c - x does not saturate to 0)
  const __m128i diff_sign = _mm_xor_si128(_mm_cmpeq_epi8(ca, z), _mm_cmpeq_epi8(cb, z));
  const __m128i pc = select_sse2(diff_sign, absdiff_sse2(pa, pb), _mm_adds_epu8(pa, pb));
  const __m128i m = _mm_min_epu8(pb, pc);
  const __m128i use_a = _mm_cmpeq_epi8(_mm_min_epu8(pa, m), pa);  // pa <= pb and pa <= pc
  const __m128i use_b = _mm_cmpeq_epi8(m, pb);                     // pb <= pc
  return select_sse2(use_a, a, select_sse2(use_b, b, c));
}
// Sum of absolute values of signed bytes, in the two 64-bit lanes.
VIO_INLINE __m128i abs_sum_sse2(__m128i r) {
  return _mm_sad_epu8(_mm_min_epu8(r, _mm_sub_epi8(_mm_setzero_si128(), r)), _mm_setzero_si128());
}

inline unsigned choose_filter_sse2(const std::uint8_t* cur, const std::uint8_t* up, std::size_t n,
                                  unsigned bpp, unsigned allowed = 0x1f,
                                  std::uint8_t* paeth_out = nullptr) {
  __m128i acc[5];
  for (auto& a : acc) a = _mm_setzero_si128();
  std::size_t i = 0;
  for (; i + 16 <= n; i += 16) {
    __m128i x = loadu_sse2(cur + i), a = loadu_sse2(cur + i - bpp), b = loadu_sse2(up + i),
            c = loadu_sse2(up + i - bpp);
    acc[0] = _mm_add_epi64(acc[0], abs_sum_sse2(x));
    acc[1] = _mm_add_epi64(acc[1], abs_sum_sse2(_mm_sub_epi8(x, a)));
    acc[2] = _mm_add_epi64(acc[2], abs_sum_sse2(_mm_sub_epi8(x, b)));
    acc[3] = _mm_add_epi64(acc[3], abs_sum_sse2(_mm_sub_epi8(x, avg_floor_sse2(a, b))));
    const __m128i r4 = _mm_sub_epi8(x, paeth_predict_sse2(a, b, c));
    if (paeth_out) _mm_storeu_si128(reinterpret_cast<__m128i*>(paeth_out + i), r4);
    acc[4] = _mm_add_epi64(acc[4], abs_sum_sse2(r4));
  }
  std::uint64_t cost[5];
  for (int f = 0; f < 5; f++) {
    alignas(16) std::uint64_t v[2];
    _mm_store_si128(reinterpret_cast<__m128i*>(v), acc[f]);
    cost[f] = v[0] + v[1];
  }
  for (; i < n; i++) {
    std::uint8_t x = cur[i], a = cur[i - bpp], b = up[i], c = up[i - bpp];
    std::uint8_t r[5] = {x, static_cast<std::uint8_t>(x - a), static_cast<std::uint8_t>(x - b),
                         static_cast<std::uint8_t>(x - ((a + b) >> 1)),
                         static_cast<std::uint8_t>(x - paeth(a, b, c))};
    if (paeth_out) paeth_out[i] = r[4];
    for (int f = 0; f < 5; f++) cost[f] += r[f] < 128 ? r[f] : 256 - r[f];
  }
  return pick_filter(cost, allowed);
}

inline void apply_filter_sse2(unsigned filter, std::uint8_t* out, const std::uint8_t* cur,
                              const std::uint8_t* up, std::size_t n, unsigned bpp) {
  std::size_t i = 0;
  auto store = [](std::uint8_t* p, __m128i v) { _mm_storeu_si128(reinterpret_cast<__m128i*>(p), v); };
  switch (filter) {
    case kFilterNone:
      std::memcpy(out, cur, n);
      return;
    case kFilterSub:
      for (; i + 16 <= n; i += 16) store(out + i, _mm_sub_epi8(loadu_sse2(cur + i), loadu_sse2(cur + i - bpp)));
      break;
    case kFilterUp:
      for (; i + 16 <= n; i += 16) store(out + i, _mm_sub_epi8(loadu_sse2(cur + i), loadu_sse2(up + i)));
      break;
    case kFilterAvg:
      for (; i + 16 <= n; i += 16)
        store(out + i, _mm_sub_epi8(loadu_sse2(cur + i),
                                    avg_floor_sse2(loadu_sse2(cur + i - bpp), loadu_sse2(up + i))));
      break;
    case kFilterPaeth:
      for (; i + 16 <= n; i += 16)
        store(out + i, _mm_sub_epi8(loadu_sse2(cur + i),
                                    paeth_predict_sse2(loadu_sse2(cur + i - bpp), loadu_sse2(up + i),
                                                       loadu_sse2(up + i - bpp))));
      break;
  }
  apply_filter_scalar(filter, out + i, cur + i, up + i, n - i, bpp);
}
VIO_TARGET_POP()
#endif

#if VIO_SIMD_AVX2
VIO_TARGET_PUSH_("avx2,bmi,bmi2")
VIO_INLINE __m256i loadu_avx2(const std::uint8_t* p) {
  return _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
}
VIO_INLINE __m256i paeth_predict_avx2(__m256i a, __m256i b, __m256i c) {
  auto absdiff = [](__m256i x, __m256i y) { return _mm256_or_si256(_mm256_subs_epu8(x, y), _mm256_subs_epu8(y, x)); };
  auto le = [](__m256i x, __m256i y) { return _mm256_cmpeq_epi8(_mm256_min_epu8(x, y), x); };
  const __m256i z = _mm256_setzero_si256();
  const __m256i ca = _mm256_subs_epu8(c, a), cb = _mm256_subs_epu8(c, b);
  const __m256i pa = _mm256_or_si256(_mm256_subs_epu8(b, c), cb), pb = _mm256_or_si256(_mm256_subs_epu8(a, c), ca);
  // a < c differs from b < c (x < c where c - x does not saturate to 0)
  const __m256i diff_sign = _mm256_xor_si256(_mm256_cmpeq_epi8(ca, z), _mm256_cmpeq_epi8(cb, z));
  const __m256i pc = _mm256_blendv_epi8(_mm256_adds_epu8(pa, pb), absdiff(pa, pb), diff_sign);
  const __m256i m = _mm256_min_epu8(pb, pc);
  const __m256i use_a = le(pa, m);                  // pa <= pb and pa <= pc
  const __m256i use_b = _mm256_cmpeq_epi8(m, pb);  // pb <= pc
  return _mm256_blendv_epi8(_mm256_blendv_epi8(c, b, use_b), a, use_a);
}
VIO_INLINE __m256i avg_floor_avx2(__m256i a, __m256i b) {
  return _mm256_sub_epi8(_mm256_avg_epu8(a, b), _mm256_and_si256(_mm256_xor_si256(a, b), _mm256_set1_epi8(1)));
}
VIO_INLINE __m256i abs_sum_avx2(__m256i r) {
  return _mm256_sad_epu8(_mm256_min_epu8(r, _mm256_sub_epi8(_mm256_setzero_si256(), r)), _mm256_setzero_si256());
}

inline unsigned choose_filter_avx2(const std::uint8_t* cur, const std::uint8_t* up, std::size_t n,
                                  unsigned bpp, unsigned allowed = 0x1f,
                                  std::uint8_t* paeth_out = nullptr) {
  __m256i acc[5];
  for (auto& a : acc) a = _mm256_setzero_si256();
  std::size_t i = 0;
  for (; i + 32 <= n; i += 32) {
    __m256i x = loadu_avx2(cur + i), a = loadu_avx2(cur + i - bpp), b = loadu_avx2(up + i),
            c = loadu_avx2(up + i - bpp);
    acc[0] = _mm256_add_epi64(acc[0], abs_sum_avx2(x));
    acc[1] = _mm256_add_epi64(acc[1], abs_sum_avx2(_mm256_sub_epi8(x, a)));
    acc[2] = _mm256_add_epi64(acc[2], abs_sum_avx2(_mm256_sub_epi8(x, b)));
    acc[3] = _mm256_add_epi64(acc[3], abs_sum_avx2(_mm256_sub_epi8(x, avg_floor_avx2(a, b))));
    const __m256i r4 = _mm256_sub_epi8(x, paeth_predict_avx2(a, b, c));
    if (paeth_out) _mm256_storeu_si256(reinterpret_cast<__m256i*>(paeth_out + i), r4);
    acc[4] = _mm256_add_epi64(acc[4], abs_sum_avx2(r4));
  }
  std::uint64_t cost[5];
  for (int f = 0; f < 5; f++) {
    alignas(32) std::uint64_t v[4];
    _mm256_store_si256(reinterpret_cast<__m256i*>(v), acc[f]);
    cost[f] = v[0] + v[1] + v[2] + v[3];
  }
  for (; i < n; i++) {
    std::uint8_t x = cur[i], a = cur[i - bpp], b = up[i], c = up[i - bpp];
    std::uint8_t r[5] = {x, static_cast<std::uint8_t>(x - a), static_cast<std::uint8_t>(x - b),
                         static_cast<std::uint8_t>(x - ((a + b) >> 1)),
                         static_cast<std::uint8_t>(x - paeth(a, b, c))};
    if (paeth_out) paeth_out[i] = r[4];
    for (int f = 0; f < 5; f++) cost[f] += r[f] < 128 ? r[f] : 256 - r[f];
  }
  return pick_filter(cost, allowed);
}

inline void apply_filter_avx2(unsigned filter, std::uint8_t* out, const std::uint8_t* cur,
                              const std::uint8_t* up, std::size_t n, unsigned bpp) {
  std::size_t i = 0;
  auto store = [](std::uint8_t* p, __m256i v) { _mm256_storeu_si256(reinterpret_cast<__m256i*>(p), v); };
  switch (filter) {
    case kFilterNone:
      std::memcpy(out, cur, n);
      return;
    case kFilterSub:
      for (; i + 32 <= n; i += 32) store(out + i, _mm256_sub_epi8(loadu_avx2(cur + i), loadu_avx2(cur + i - bpp)));
      break;
    case kFilterUp:
      for (; i + 32 <= n; i += 32) store(out + i, _mm256_sub_epi8(loadu_avx2(cur + i), loadu_avx2(up + i)));
      break;
    case kFilterAvg:
      for (; i + 32 <= n; i += 32)
        store(out + i, _mm256_sub_epi8(loadu_avx2(cur + i), avg_floor_avx2(loadu_avx2(cur + i - bpp), loadu_avx2(up + i))));
      break;
    case kFilterPaeth:
      for (; i + 32 <= n; i += 32)
        store(out + i, _mm256_sub_epi8(loadu_avx2(cur + i),
                                       paeth_predict_avx2(loadu_avx2(cur + i - bpp), loadu_avx2(up + i),
                                                          loadu_avx2(up + i - bpp))));
      break;
  }
  apply_filter_scalar(filter, out + i, cur + i, up + i, n - i, bpp);
}
VIO_TARGET_POP()
#endif

// Generic kernels for a 128-bit vector interface V (AltiVec, MMI, RVV).
template <class V>
struct VecFilterSelect {
  using F = VecFilter<V>;
  using T = typename V::T;

  // Absolute values of the signed bytes of r, summed in pairs into 16-bit
  // lanes.
  static VIO_INLINE T abs_pairs(T r) {
    const T m = V::min_u8(r, V::sub_8(V::zero(), r));
    return V::add_16(V::widenlo_u8(m), V::widenhi_u8(m));
  }

  static unsigned choose(const std::uint8_t* cur, const std::uint8_t* up, std::size_t n, unsigned bpp,
                         unsigned allowed) {
    std::uint64_t cost[5] = {};
    std::size_t i = 0;
    while (i + 16 <= n) {
      // 16-bit sums of at most 64 vectors (each lane gains <= 256 per
      // vector), then added up in pairs as 32 bits.
      T acc[5];
      for (auto& a : acc) a = V::zero();
      const std::size_t end = std::min(n & ~std::size_t(15), i + 64 * 16);
      for (; i < end; i += 16) {
        const T x = V::loadu(cur + i), a = V::loadu(cur + i - bpp), b = V::loadu(up + i),
                c = V::loadu(up + i - bpp);
        acc[0] = V::add_16(acc[0], abs_pairs(x));
        acc[1] = V::add_16(acc[1], abs_pairs(V::sub_8(x, a)));
        acc[2] = V::add_16(acc[2], abs_pairs(V::sub_8(x, b)));
        acc[3] = V::add_16(acc[3], abs_pairs(V::sub_8(x, F::avg_floor(a, b))));
        acc[4] = V::add_16(acc[4], abs_pairs(V::sub_8(x, F::paeth(a, b, c))));
      }
      for (int f = 0; f < 5; f++) {
        std::int32_t s[4];
        V::storeu(s, V::madd_16(acc[f], V::set1_16(1)));
        cost[f] += static_cast<std::uint64_t>(s[0]) + s[1] + s[2] + s[3];
      }
    }
    for (; i < n; i++) {
      std::uint8_t x = cur[i], a = cur[i - bpp], b = up[i], c = up[i - bpp];
      std::uint8_t r[5] = {x, static_cast<std::uint8_t>(x - a), static_cast<std::uint8_t>(x - b),
                           static_cast<std::uint8_t>(x - ((a + b) >> 1)),
                           static_cast<std::uint8_t>(x - paeth(a, b, c))};
      for (int f = 0; f < 5; f++) cost[f] += r[f] < 128 ? r[f] : 256 - r[f];
    }
    return pick_filter(cost, allowed);
  }

  static void apply(unsigned filter, std::uint8_t* out, const std::uint8_t* cur, const std::uint8_t* up,
                    std::size_t n, unsigned bpp) {
    std::size_t i = 0;
    switch (filter) {
      case kFilterNone:
        std::memcpy(out, cur, n);
        return;
      case kFilterSub:
        for (; i + 16 <= n; i += 16) V::storeu(out + i, V::sub_8(V::loadu(cur + i), V::loadu(cur + i - bpp)));
        break;
      case kFilterUp:
        for (; i + 16 <= n; i += 16) V::storeu(out + i, V::sub_8(V::loadu(cur + i), V::loadu(up + i)));
        break;
      case kFilterAvg:
        for (; i + 16 <= n; i += 16)
          V::storeu(out + i, V::sub_8(V::loadu(cur + i), F::avg_floor(V::loadu(cur + i - bpp), V::loadu(up + i))));
        break;
      case kFilterPaeth:
        for (; i + 16 <= n; i += 16)
          V::storeu(out + i, V::sub_8(V::loadu(cur + i), F::paeth(V::loadu(cur + i - bpp), V::loadu(up + i),
                                                                   V::loadu(up + i - bpp))));
        break;
    }
    apply_filter_scalar(filter, out + i, cur + i, up + i, n - i, bpp);
  }
};

// The filter kernels in use, chosen once per image.
enum class FilterIsa : unsigned char { kScalar, kSse2, kAvx2, kNeon, kVec };

inline FilterIsa filter_isa() {
  using namespace simd;
#if VIO_SIMD_AVX2
  if (simd::has(kAVX2) && simd::has(kBMI2)) return FilterIsa::kAvx2;
#endif
#if VIO_SIMD_X86
  if (simd::has(kSSE2)) return FilterIsa::kSse2;
#elif VIO_SIMD_NEON
  if (simd::has(kNEON)) return FilterIsa::kNeon;
#elif VIO_PNG_GENERIC
  if (simd::has(simd::kGenericIsa)) return FilterIsa::kVec;
#endif
  return FilterIsa::kScalar;
}

#if VIO_PNG_SCALAR_INT
inline constexpr bool kScalarInt = true;
#else
inline constexpr bool kScalarInt = false;
#endif
// (paeth: where the vector and MSVC scalar paths also store the Paeth
// residuals; returns whether they did.)
inline unsigned choose_filter(const std::uint8_t* cur, const std::uint8_t* up, std::size_t n, unsigned bpp,
                              unsigned allowed, FilterIsa isa, std::uint8_t* paeth = nullptr,
                              bool* stored = nullptr) {
  if (stored) *stored = isa == FilterIsa::kAvx2 || isa == FilterIsa::kSse2 || isa == FilterIsa::kNeon ||
                        (kScalarInt && isa == FilterIsa::kScalar);
  switch (isa) {
#if VIO_SIMD_AVX2
    case FilterIsa::kAvx2: return choose_filter_avx2(cur, up, n, bpp, allowed, paeth);
#endif
#if VIO_SIMD_X86
    case FilterIsa::kSse2: return choose_filter_sse2(cur, up, n, bpp, allowed, paeth);
#endif
#if VIO_SIMD_NEON
    case FilterIsa::kNeon: return choose_filter_neon(cur, up, n, bpp, allowed, paeth);
#endif
#if VIO_PNG_GENERIC
    case FilterIsa::kVec: return VecFilterSelect<simd::GV>::choose(cur, up, n, bpp, allowed);
#endif
    default: return choose_filter_scalar(cur, up, n, bpp, allowed, paeth);
  }
}

inline void apply_filter(unsigned filter, std::uint8_t* out, const std::uint8_t* cur, const std::uint8_t* up,
                         std::size_t n, unsigned bpp, FilterIsa isa) {
  switch (isa) {
#if VIO_SIMD_AVX2
    case FilterIsa::kAvx2: return apply_filter_avx2(filter, out, cur, up, n, bpp);
#endif
#if VIO_SIMD_X86
    case FilterIsa::kSse2: return apply_filter_sse2(filter, out, cur, up, n, bpp);
#endif
#if VIO_SIMD_NEON
    case FilterIsa::kNeon: return apply_filter_neon(filter, out, cur, up, n, bpp);
#endif
#if VIO_PNG_GENERIC
    case FilterIsa::kVec: return VecFilterSelect<simd::GV>::apply(filter, out, cur, up, n, bpp);
#endif
    default: return apply_filter_scalar(filter, out, cur, up, n, bpp);
  }
}

// Filters the padded row at cur (above it: up) into dst, the filter type
// byte first: the one allowed filter, or the best of those allowed.
inline void filter_row(std::uint8_t* dst, const std::uint8_t* cur, const std::uint8_t* up,
                       std::size_t n, unsigned bpp, unsigned allowed, FilterIsa isa) {
  const bool choose = (allowed & (allowed - 1)) != 0;  // more than one allowed
  bool stored = false;  // the Paeth residuals are in dst + 1 already
  const unsigned f = choose ? choose_filter(cur, up, n, bpp, allowed, isa, dst + 1, &stored) : ctz32(allowed);
  dst[0] = static_cast<std::uint8_t>(f);
  if (f != kFilterPaeth || !stored) apply_filter(f, dst + 1, cur, up, n, bpp, isa);
}

// Consecutive rows of up to max_row_bytes bytes filtered one by one: load a
// row into row(), then filter() writes its filter type byte and residuals
// and keeps it as the row above the next one.
class RowFilter {
 public:
  void start(std::size_t max_row_bytes, unsigned bpp, unsigned filters) {
    bpp_ = bpp;
    allowed_ = filters ? filters : 1;
    isa_ = filter_isa();
    stride_ = kRowPad + ((max_row_bytes + 15) & ~std::size_t(15)) + 16;
    rows_.assign(2 * stride_, 0);
    cur_ = 0;
  }
  // Starts a new sequence of rows (the first row's "above" is zeros).
  void restart() {
    std::memset(rows_.data(), 0, rows_.size());
    cur_ = 0;
  }
  std::uint8_t* row() { return rows_.data() + kRowPad + cur_ * stride_; }
  void filter(std::uint8_t* dst, std::size_t n) {
    const std::uint8_t* up = rows_.data() + kRowPad + (cur_ ^ 1) * stride_;
    filter_row(dst, row(), up, n, bpp_, allowed_, isa_);
    cur_ ^= 1;
  }

 private:
  std::vector<std::uint8_t> rows_;
  std::size_t stride_ = 0, cur_ = 0;
  unsigned bpp_ = 1, allowed_ = 1;
  FilterIsa isa_ = FilterIsa::kScalar;
};

// ---------------------------------------------------------------------------
// Huffman codes
// ---------------------------------------------------------------------------

// Code lengths for `n` <= 286 symbols with the given frequencies (all
// nonzero frequencies get a code), limited to max_len bits: a Huffman tree
// built from the symbols sorted by frequency, then, if it is too deep, the
// number of codes of each length adjusted as zlib does, the most frequent
// symbols getting the shortest codes.
inline void huffman_lengths(const std::uint64_t* freq, int n, int max_len, std::uint8_t* len) {
  std::uint64_t key[286];  // frequency << 9 | symbol, sorted
  // Symbols with the smallest frequency (often most of them) are already in
  // order; only the others are sorted, then the two lists merged.
  std::uint64_t fmin = ~std::uint64_t(0);
  for (int i = 0; i < n; i++)
    if (freq[i] && freq[i] < fmin) fmin = freq[i];
  std::uint64_t rest[286];
  int m = 0, nrest = 0;
  for (int i = 0; i < n; i++) {
    len[i] = 0;
    if (freq[i] == fmin) key[m++] = fmin << 9 | static_cast<unsigned>(i);
    else if (freq[i]) rest[nrest++] = freq[i] << 9 | static_cast<unsigned>(i);
  }
  if (m + nrest == 0) return;
  if (m + nrest == 1) {
    len[(m ? key[0] : rest[0]) & 511] = 1;
    return;
  }
  std::sort(rest, rest + nrest);
  std::memcpy(key + m, rest, nrest * sizeof(std::uint64_t));  // all larger than fmin
  m += nrest;
  // Two-queue merge: leaves 0..m-1 in order, internal nodes m.. in creation
  // order (their weights do not decrease).
  std::uint64_t weight[2 * 286];
  std::int16_t parent[2 * 286];
  for (int i = 0; i < m; i++) weight[i] = key[i] >> 9;
  int leaf = 0, inner = m, next = m;
  auto pop = [&]() {
    if (inner >= next || (leaf < m && weight[leaf] <= weight[inner])) return leaf++;
    return inner++;
  };
  while (next < 2 * m - 1) {
    int x = pop(), y = pop();
    weight[next] = weight[x] + weight[y];
    parent[x] = parent[y] = static_cast<std::int16_t>(next);
    next++;
  }
  // Depths from the root (the last node) down; count leaves per length.
  std::uint8_t depth[2 * 286];
  depth[next - 1] = 0;
  unsigned count[64] = {};
  for (int k = next - 2; k >= 0; k--) {
    depth[k] = static_cast<std::uint8_t>(depth[parent[k]] + 1);
    if (k < m) count[std::min<int>(depth[k], max_len)]++;
  }
  // Clamping to max_len may over-subscribe the code.  Each step moves a
  // leaf one level down, next to a leaf taken from the bottom level, which
  // lowers the Kraft sum by 2^-max_len.
  std::uint64_t kraft = 0;
  for (int l = 1; l <= max_len; l++) kraft += std::uint64_t(count[l]) << (max_len - l);
  for (; kraft > (std::uint64_t(1) << max_len); kraft--) {
    int bits = max_len - 1;
    while (count[bits] == 0) bits--;
    count[bits]--;
    count[bits + 1] += 2;
    count[max_len]--;
  }
  // Most frequent symbols (end of key[]) get the shortest codes.
  int k = m - 1;
  for (int l = 1; l <= max_len; l++)
    for (unsigned c = 0; c < count[l]; c++) len[key[k--] & 511] = static_cast<std::uint8_t>(l);
}

// Canonical codes (bit-reversed, ready to be sent least significant bit
// first) for the given lengths.
inline void canonical_codes(const std::uint8_t* len, int n, std::uint16_t* code,
                            std::uint16_t* msb = nullptr) {
  unsigned count[16] = {}, next[16] = {};
  for (int i = 0; i < n; i++) count[len[i]]++;
  count[0] = 0;
  unsigned c = 0;
  for (int l = 1; l < 16; l++) {
    c = (c + count[l - 1]) << 1;
    next[l] = c;
  }
  static constexpr auto rev8 = [] {
    std::array<std::uint8_t, 256> t{};
    for (unsigned i = 0; i < 256; i++) {
      unsigned r = 0;
      for (int b = 0; b < 8; b++) r |= ((i >> b) & 1u) << (7 - b);
      t[i] = static_cast<std::uint8_t>(r);
    }
    return t;
  }();
  for (int i = 0; i < n; i++) {
    code[i] = 0;
    if (msb) msb[i] = 0;
    if (!len[i]) continue;
    unsigned v = next[len[i]]++;
    if (msb) msb[i] = static_cast<std::uint16_t>(v);
    unsigned r = static_cast<unsigned>(rev8[v & 255]) << 8 | rev8[v >> 8];  // 16-bit reversal
    code[i] = static_cast<std::uint16_t>(r >> (16 - len[i]));
  }
}

// ---------------------------------------------------------------------------
// Bit writer
// ---------------------------------------------------------------------------

// Writes up to 56 bits per put(); the output must have 8 bytes of slack.
struct BitWriter {
  std::uint8_t* out;
  std::uint64_t buf = 0;
  unsigned n = 0;

  VIO_INLINE void put(std::uint64_t bits, unsigned len) {
    buf |= bits << n;
    n += len;
    std::uint64_t v = buf;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap64(v);
#endif
    std::memcpy(out, &v, 8);
    out += n >> 3;
    buf >>= n & ~7u;
    n &= 7;
  }
  // Pads to a byte boundary.
  void align() {
    if (n) put(0, 8 - n);
  }
  // Field by field: MSVC copies the struct with a 16-byte load right after
  // 8-byte stores of the fields, which stalls store forwarding.
  VIO_INLINE void copy_to(BitWriter& o) const {
    o.out = out;
    o.buf = buf;
    o.n = n;
  }
};

// ---------------------------------------------------------------------------
// The encoder
// ---------------------------------------------------------------------------

// Growable byte buffer that is not zero-filled, written through raw
// pointers; keeps 16 bytes of slack past its capacity for wide stores.
class ByteBuffer {
 public:
  ByteBuffer() = default;
  ByteBuffer(ByteBuffer&& o) noexcept : data_(std::move(o.data_)), size_(o.size_), cap_(o.cap_) {
    o.size_ = o.cap_ = 0;
  }
  ByteBuffer& operator=(ByteBuffer&& o) noexcept {
    if (this != &o) {
      release();
      data_ = std::move(o.data_);
      size_ = o.size_;
      cap_ = o.cap_;
      o.size_ = o.cap_ = 0;
    }
    return *this;
  }
  ~ByteBuffer() { release(); }

  std::uint8_t* data() { return data_.get(); }
  std::size_t size() const { return size_; }
  std::uint8_t* end() { return data_.get() + size_; }
  void set_end(std::uint8_t* p) { size_ = static_cast<std::size_t>(p - data_.get()); }
  std::size_t room(const std::uint8_t* p) const {
    return cap_ - static_cast<std::size_t>(p - data_.get());
  }
  void reserve(std::size_t n) {
    if (n <= cap_) return;
    Cache& c = cache();
    if (!data_ && c.data && c.cap >= n) {
      data_ = std::move(c.data);
      cap_ = c.cap;
      c.cap = 0;
      return;
    }
    std::size_t cap = std::max(n, cap_ + cap_ / 2);
    std::unique_ptr<std::uint8_t[]> d(new std::uint8_t[cap + 16]);
    if (size_) std::memcpy(d.get(), data_.get(), size_);
    data_ = std::move(d);
    cap_ = cap;
  }
  // Makes room for n more bytes after p (a write position); returns the
  // write position in the (possibly moved) buffer.
  std::uint8_t* grow(std::uint8_t* p, std::size_t n) {
    std::size_t pos = static_cast<std::size_t>(p - data_.get());
    std::size_t old = size_;
    size_ = pos;  // keep the bytes written so far
    reserve(pos + n);
    size_ = old;
    return data_.get() + pos;
  }
  void append(const void* p, std::size_t n) {
    reserve(size_ + n);
    std::memcpy(data_.get() + size_, p, n);
    size_ += n;
  }
  std::vector<std::uint8_t> to_vector() const { return {data_.get(), data_.get() + size_}; }

 private:
  // The storage of the last buffer released on a thread is kept for the
  // next one there (allocating megabytes per image costs a page fault per
  // 4 KiB page on systems whose allocator returns large blocks to the OS).
  struct Cache {
    std::unique_ptr<std::uint8_t[]> data;
    std::size_t cap = 0;
  };
  static Cache& cache() {
    thread_local Cache c;
    return c;
  }
  void release() {
    Cache& c = cache();
    if (data_ && cap_ <= (std::size_t(64) << 20) && cap_ > c.cap) {
      c.data = std::move(data_);
      c.cap = cap_;
    }
    data_.reset();
    size_ = cap_ = 0;
  }

  std::unique_ptr<std::uint8_t[]> data_;
  std::size_t size_ = 0, cap_ = 0;
};

// Length symbol (0..28) and extra bits of match lengths 3..258.
inline constexpr std::uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
inline constexpr std::uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                                               15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                                               67, 83, 99, 115, 131, 163, 195, 227, 258};
inline unsigned length_symbol(unsigned len) {
  unsigned s = 28;
  while (kLenBase[s] > len) s--;
  return s;
}

// Splits the m zeros that follow a literal zero into matches of lengths
// 3..258: calls fn(length) for each; returns the 0..2 zeros left over.
template <class Fn>
std::size_t for_run_matches(std::size_t m, Fn&& fn) {
  while (m >= 3) {
    std::size_t l = m > 258 ? 258 : m;
    if (m - l == 1 || m - l == 2) l = m - 3;
    fn(static_cast<unsigned>(l));
    m -= l;
  }
  return m;
}

inline unsigned ctz64(std::uint64_t v) {
#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_ARM64))
  unsigned long r;
  _BitScanForward64(&r, v);
  return static_cast<unsigned>(r);
#elif defined(_MSC_VER) && !defined(__clang__)
  const std::uint32_t lo = static_cast<std::uint32_t>(v);
  return lo ? ctz32(lo) : 32 + ctz32(static_cast<std::uint32_t>(v >> 32));
#else
  return static_cast<unsigned>(__builtin_ctzll(v));
#endif
}

// The image's Huffman code.  Literal codes have the structure that the
// vector coders rely on: residuals 0..15 and 240..255 have codes of their
// own, and residuals 16 * h .. 16 * h + 15 (h = 1..14) share one length.
// All literal codes are at most 14 bits long.
struct CodeTables {
  std::uint8_t len[286];
  std::uint16_t code[286];      // bit-reversed
  std::uint32_t lit[256];       // code | length << 16
  std::uint32_t run_bits[259];  // match of length l, distance 1
  std::uint8_t run_len[259];
  std::size_t min_run = 259;    // shortest run of zeros coded as a match
  // For the vector coders: residuals 0..15 / 240..255 (index lo | top << 4)
  alignas(32) std::uint8_t near_len[32];
  alignas(32) std::uint8_t near_lo[32];  // bit-reversed code, low byte
  alignas(32) std::uint8_t near_hi[32];  // high byte
  // residuals with high nibble h = 1..14: length, first code (most
  // significant bit first), 2^length
  alignas(16) std::uint8_t grp_len[16];
  alignas(16) std::uint8_t grp_base_lo[16];
  alignas(16) std::uint8_t grp_base_hi[16];
  alignas(16) std::uint8_t grp_pow_lo[16];
  alignas(16) std::uint8_t grp_pow_hi[16];
};

// Symbol statistics of filtered data, with runs of zeros as the coders code
// them (assuming that runs of 8 or more become matches).
struct Histogram {
  std::uint32_t lit[4][256] = {};
  std::uint64_t len[29] = {};
  std::uint64_t zeros = 0;  // literal zeros from runs

  void add_run(std::size_t r) {
    if (r < 8) {
      zeros += r;
      return;
    }
    zeros += 1 + for_run_matches(r - 1, [&](unsigned l) { len[length_symbol(l)]++; });
  }
  void count(const std::uint8_t* p, std::size_t n) {
    std::size_t i = 0, run = 0;
    for (; i + 8 <= n; i += 8) {
      std::uint64_t v;
      std::memcpy(&v, p + i, 8);
      if (v == 0) {
        run += 8;
        continue;
      }
      if (VIO_LIKELY(run == 0)) {
        const std::uint8_t* q = p + i;
        lit[0][q[0]]++;
        lit[1][q[1]]++;
        lit[2][q[2]]++;
        lit[3][q[3]]++;
        lit[0][q[4]]++;
        lit[1][q[5]]++;
        lit[2][q[6]]++;
        lit[3][q[7]]++;
        continue;
      }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      unsigned k = static_cast<unsigned>(__builtin_clzll(v)) / 8;
#else
      unsigned k = ctz64(v) / 8;
#endif
      add_run(run + k);
      run = 0;
      for (; k < 8; k++) lit[k & 3][p[i + k]]++;
    }
    for (; i < n; i++) {
      if (p[i] == 0) {
        run++;
      } else {
        if (run) add_run(run);
        run = 0;
        lit[0][p[i]]++;
      }
    }
    if (run) add_run(run);
  }
};

inline void build_code_tables(const Histogram& h, CodeTables& t) {
  // Every literal and length symbol gets a code (a sample may miss some);
  // the counts are scaled up so that this matters little.
  std::uint64_t freq[286];
  for (int i = 0; i < 256; i++)
    freq[i] = std::uint64_t(h.lit[0][i]) + h.lit[1][i] + h.lit[2][i] + h.lit[3][i];
  freq[0] += h.zeros;
  for (int i = 0; i < 29; i++) freq[257 + i] = h.len[i];
  for (int i = 0; i < 286; i++) freq[i] = freq[i] * 16 + 1;
  freq[256] = 1;
  // Code lengths over a reduced alphabet: 32 single residuals, 14 groups of
  // 16, end of block and the length symbols; limited to 10 bits so that
  // group members (4 more bits) stay within 14.
  std::uint64_t rf[76];
  for (int i = 0; i < 16; i++) {
    rf[i] = freq[i];
    rf[16 + i] = freq[240 + i];
  }
  for (int g = 0; g < 14; g++) {
    std::uint64_t sum = 0;
    for (int i = 0; i < 16; i++) sum += freq[16 * (g + 1) + i];
    rf[32 + g] = sum;
  }
  for (int i = 0; i < 30; i++) rf[46 + i] = freq[256 + i];
  std::uint8_t rl[76];
  huffman_lengths(rf, 76, 10, rl);
  for (int i = 0; i < 16; i++) {
    t.len[i] = rl[i];
    t.len[240 + i] = rl[16 + i];
  }
  for (int g = 0; g < 14; g++)
    for (int i = 0; i < 16; i++) t.len[16 * (g + 1) + i] = static_cast<std::uint8_t>(rl[32 + g] + 4);
  for (int i = 0; i < 30; i++) t.len[256 + i] = rl[46 + i];
  // Canonical codes, assigned over the reduced alphabet in symbol order: a
  // group's 16 symbols are consecutive, share a length and so get
  // consecutive codes.
  std::uint16_t msb[286];
  {
    unsigned count[16] = {}, next[16] = {};
    for (int i = 0; i < 76; i++) count[rl[i]] += i >= 32 && i < 46 ? 16 : 1;
    for (int i = 32; i < 46; i++) count[rl[i]] -= 16, count[rl[i] + 4] += 16;
    count[0] = 0;
    unsigned c = 0;
    for (int l = 1; l < 16; l++) {
      c = (c + count[l - 1]) << 1;
      next[l] = c;
    }
    auto one = [&](int sym, unsigned l) {
      if (!l) {
        msb[sym] = 0;
        return;
      }
      msb[sym] = static_cast<std::uint16_t>(next[l]++);
    };
    for (int i = 0; i < 16; i++) one(i, rl[i]);
    for (int g = 0; g < 14; g++) {
      const unsigned l = rl[32 + g] + 4u;
      const unsigned v = next[l];
      next[l] += 16;
      for (int i = 0; i < 16; i++) msb[16 * (g + 1) + i] = static_cast<std::uint16_t>(v + i);
    }
    for (int i = 0; i < 16; i++) one(240 + i, rl[16 + i]);
    for (int i = 0; i < 30; i++) one(256 + i, rl[46 + i]);
    static constexpr auto rev8 = [] {
      std::array<std::uint8_t, 256> r{};
      for (unsigned i = 0; i < 256; i++) {
        unsigned v = 0;
        for (int b = 0; b < 8; b++) v |= ((i >> b) & 1u) << (7 - b);
        r[i] = static_cast<std::uint8_t>(v);
      }
      return r;
    }();
    for (int i = 0; i < 286; i++) {
      const unsigned v = msb[i], r = static_cast<unsigned>(rev8[v & 255]) << 8 | rev8[v >> 8];
      t.code[i] = t.len[i] ? static_cast<std::uint16_t>(r >> (16 - t.len[i])) : 0;
    }
  }
  for (unsigned s = 0; s < 29; s++) {
    for (unsigned e = 0; e < (1u << kLenExtra[s]); e++) {
      unsigned l = kLenBase[s] + e;
      t.run_bits[l] = static_cast<std::uint32_t>(t.code[257 + s] | e << t.len[257 + s]);
      t.run_len[l] = static_cast<std::uint8_t>(t.len[257 + s] + kLenExtra[s] + 1);
    }
  }
  for (int i = 0; i < 256; i++)
    t.lit[i] = static_cast<std::uint32_t>(t.code[i]) | static_cast<std::uint32_t>(t.len[i]) << 16;
  t.min_run = 259;
  for (unsigned r = 4; r <= 258; r++) {
    unsigned cost = t.len[0];
    std::size_t left = for_run_matches(r - 1, [&](unsigned l) { cost += t.run_len[l]; });
    cost += static_cast<unsigned>(left) * t.len[0];
    if (cost < r * t.len[0]) {
      t.min_run = r;
      break;
    }
  }
  for (int i = 0; i < 32; i++) {
    int sym = i < 16 ? i : 224 + i;
    t.near_len[i] = t.len[sym];
    t.near_lo[i] = static_cast<std::uint8_t>(t.code[sym]);
    t.near_hi[i] = static_cast<std::uint8_t>(t.code[sym] >> 8);
  }
  for (int g = 0; g < 16; g++) {
    bool mid = g >= 1 && g <= 14;
    unsigned l = mid ? t.len[16 * g] : 0, base = mid ? msb[16 * g] : 0, pow = mid ? 1u << l : 0;
    t.grp_len[g] = static_cast<std::uint8_t>(l);
    t.grp_base_lo[g] = static_cast<std::uint8_t>(base);
    t.grp_base_hi[g] = static_cast<std::uint8_t>(base >> 8);
    t.grp_pow_lo[g] = static_cast<std::uint8_t>(pow);
    t.grp_pow_hi[g] = static_cast<std::uint8_t>(pow >> 8);
  }
}

inline void emit_run(BitWriter& w, const CodeTables& t, std::size_t run) {
  const std::uint32_t zero = t.lit[0];
  if (run < t.min_run) {
    for (std::size_t i = 0; i < run; i++) w.put(zero & 0xffff, zero >> 16);
    return;
  }
  w.put(zero & 0xffff, zero >> 16);
  std::size_t m = for_run_matches(run - 1, [&](unsigned l) { w.put(t.run_bits[l], t.run_len[l]); });
  for (std::size_t i = 0; i < m; i++) w.put(zero & 0xffff, zero >> 16);
}

VIO_INLINE void emit_literals(BitWriter& w, const CodeTables& t, const std::uint8_t* p,
                              std::size_t n) {
  for (std::size_t i = 0; i < n; i++) w.put(t.lit[p[i]] & 0xffff, t.lit[p[i]] >> 16);
}

VIO_INLINE void emit8(BitWriter& w, const std::uint32_t* lit, const std::uint8_t* q) {
  // Two groups of four codes of up to 14 bits per put().
  for (int k = 0; k < 8; k += 4) {
    std::uint64_t e0 = lit[q[k]], e1 = lit[q[k + 1]], e2 = lit[q[k + 2]], e3 = lit[q[k + 3]];
    unsigned l0 = static_cast<unsigned>(e0 >> 16), l1 = static_cast<unsigned>(e1 >> 16),
             l2 = static_cast<unsigned>(e2 >> 16), l3 = static_cast<unsigned>(e3 >> 16);
    std::uint64_t lo = (e0 & 0xffff) | (e1 & 0xffff) << l0;
    std::uint64_t hi = (e2 & 0xffff) | (e3 & 0xffff) << l2;
    w.put(lo | hi << (l0 + l1), l0 + l1 + l2 + l3);
  }
}

// Huffman-codes n bytes, continuing a run of `run` zeros from before;
// returns the run of zeros at the end (not yet written).  Runs start at
// 8-byte words that are all zero and extend over the zero bytes that begin
// the next word.
inline std::size_t encode_bytes_scalar(BitWriter& writer, const CodeTables& t,
                                       const std::uint8_t* p, std::size_t n, std::size_t run) {
  // A local copy of the writer stays in registers: stores through the
  // output pointer could alias it otherwise.
  BitWriter w = writer;
  std::size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    std::uint64_t v;
    std::memcpy(&v, p + i, 8);
    if (v == 0) {
      run += 8;
      continue;
    }
    if (VIO_LIKELY(run == 0)) {
      emit8(w, t.lit, p + i);
      continue;
    }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    unsigned lead = static_cast<unsigned>(__builtin_clzll(v)) / 8;
#else
    unsigned lead = ctz64(v) / 8;
#endif
    w.copy_to(writer);
    emit_run(writer, t, run + lead);
    writer.copy_to(w);
    run = 0;
    emit_literals(w, t, p + i + lead, 8 - lead);
  }
  for (; i < n; i++) {
    if (p[i] == 0) {
      run++;
    } else {
      if (run) {
        w.copy_to(writer);
        emit_run(writer, t, run);
        writer.copy_to(w);
      }
      run = 0;
      w.put(t.lit[p[i]] & 0xffff, t.lit[p[i]] >> 16);
    }
  }
  w.copy_to(writer);
  return run;
}

#if VIO_SIMD_AVX2
VIO_TARGET_PUSH_("avx2,bmi,bmi2")
// Codes for 32 residuals: in each 64-bit lane of *code the codes of four
// consecutive residuals, their total length in the same lane of *len.  The
// lanes come in this order: lo[0], lo[1], hi[0], hi[1], lo[2], lo[3],
// hi[2], hi[3] (residuals 0-3, 4-7, 8-11, ...).  Residuals whose byte of
// `keep` is zero get no code.
VIO_INLINE void code32_avx2(const CodeTables& t, __m256i b, __m256i keep, __m256i& code_lo,
                            __m256i& len_lo, __m256i& code_hi, __m256i& len_hi) {
  auto bcast = [](const std::uint8_t* p) {
    return _mm256_broadcastsi128_si256(_mm_load_si128(reinterpret_cast<const __m128i*>(p)));
  };
  const __m256i nib = _mm256_set1_epi8(0x0f);
  const __m256i lo = _mm256_and_si256(b, nib);
  const __m256i hi = _mm256_and_si256(_mm256_srli_epi16(b, 4), nib);
  const __m256i top = _mm256_cmpgt_epi8(_mm256_setzero_si256(), b);  // residual >= 128
  auto near = [&](const std::uint8_t* tab) {
    return _mm256_blendv_epi8(_mm256_shuffle_epi8(bcast(tab), lo), _mm256_shuffle_epi8(bcast(tab + 16), lo),
                              top);
  };
  const __m256i nlen = near(t.near_len), nlo = near(t.near_lo), nhi = near(t.near_hi);
  const __m256i mid = _mm256_and_si256(_mm256_cmpgt_epi8(hi, _mm256_setzero_si256()),
                                       _mm256_cmpgt_epi8(_mm256_set1_epi8(15), hi));
  const __m256i glen = _mm256_shuffle_epi8(bcast(t.grp_len), hi);
  const __m256i gblo = _mm256_shuffle_epi8(bcast(t.grp_base_lo), hi);
  const __m256i gbhi = _mm256_shuffle_epi8(bcast(t.grp_base_hi), hi);
  const __m256i gplo = _mm256_shuffle_epi8(bcast(t.grp_pow_lo), hi);
  const __m256i gphi = _mm256_shuffle_epi8(bcast(t.grp_pow_hi), hi);
  const __m256i len8 = _mm256_and_si256(_mm256_blendv_epi8(nlen, glen, mid), keep);
  // Bit reversal of the bytes of 16-bit values, then byte swap: reverses
  // each 16-bit lane.
  const __m256i rev4 = _mm256_setr_epi8(0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15, 0,
                                        8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15);
  const __m256i rev4hi = _mm256_slli_epi16(rev4, 4);  // within bytes: values < 16
  const __m256i swap = _mm256_setr_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14, 1,
                                        0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
  auto half = [&](auto unpack, __m256i& code64, __m256i& len64) VIO_LAMBDA_INLINE {
    const __m256i z = _mm256_setzero_si256();
    // mid codes: reverse(base + lo) >> (16 - length) = reverse(..) * 2^length >> 16
    __m256i v = _mm256_add_epi16(unpack(gblo, gbhi), unpack(lo, z));
    __m256i vl = _mm256_and_si256(v, nib), vh = _mm256_and_si256(_mm256_srli_epi16(v, 4), nib);
    __m256i rb = _mm256_or_si256(_mm256_shuffle_epi8(rev4hi, vl), _mm256_shuffle_epi8(rev4, vh));
    __m256i mc = _mm256_mulhi_epu16(_mm256_shuffle_epi8(rb, swap), unpack(gplo, gphi));
    __m256i m16 = unpack(mid, mid);
    __m256i c16 = _mm256_blendv_epi8(unpack(nlo, nhi), mc, m16);
    __m256i l16 = unpack(len8, z);
    c16 = _mm256_and_si256(c16, _mm256_cmpgt_epi16(l16, z));  // no bits for dropped residuals
    // pairs -> 32-bit lanes
    const __m256i m16lo = _mm256_set1_epi32(0xffff);
    __m256i le = _mm256_and_si256(l16, m16lo);
    __m256i c32 = _mm256_or_si256(_mm256_and_si256(c16, m16lo), _mm256_sllv_epi32(_mm256_srli_epi32(c16, 16), le));
    __m256i l32 = _mm256_add_epi32(le, _mm256_srli_epi32(l16, 16));
    // pairs -> 64-bit lanes
    const __m256i m32lo = _mm256_set1_epi64x(0xffffffff);
    __m256i le2 = _mm256_and_si256(l32, m32lo);
    code64 = _mm256_or_si256(_mm256_and_si256(c32, m32lo), _mm256_sllv_epi64(_mm256_srli_epi64(c32, 32), le2));
    len64 = _mm256_add_epi64(le2, _mm256_srli_epi64(l32, 32));
  };
  half([](__m256i a, __m256i b2) { return _mm256_unpacklo_epi8(a, b2); }, code_lo, len_lo);
  half([](__m256i a, __m256i b2) { return _mm256_unpackhi_epi8(a, b2); }, code_hi, len_hi);
}

// encode_bytes_scalar() with 32 residuals per step; runs start at 32-byte
// blocks that are all zero.  p must have 32 readable bytes past n.
inline std::size_t encode_bytes_avx2(BitWriter& writer, const CodeTables& t,
                                     const std::uint8_t* p, std::size_t n, std::size_t run) {
  BitWriter w = writer;
  const __m256i z = _mm256_setzero_si256();
  const __m256i lane = _mm256_setr_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                                        18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
  for (std::size_t i = 0; i < n; i += 32) {
    const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
    const std::size_t avail = n - i;
    const unsigned valid = avail >= 32 ? 0xffffffffu : (1u << avail) - 1;
    const unsigned zero = static_cast<unsigned>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(b, z))) & valid;
    if (zero == valid) {
      run += avail >= 32 ? 32 : avail;
      continue;
    }
    __m256i keep = _mm256_set1_epi8(-1);
    unsigned first = 0;
    if (run) {
      first = ctz32(~zero);
      w.copy_to(writer);
      emit_run(writer, t, run + first);
      writer.copy_to(w);
      run = 0;
    }
    unsigned end = avail >= 32 ? 32 : static_cast<unsigned>(avail);
    // Eight or more zeros at the end start a run.
    const unsigned nonzero = ~zero & valid;
    const unsigned trail = end - 32 + clz32(nonzero);
    if (trail >= 8) {
      end -= trail;
      run = trail;
    }
    if (first || end < 32)
      keep = _mm256_and_si256(_mm256_cmpgt_epi8(lane, _mm256_set1_epi8(static_cast<char>(first - 1))),
                              _mm256_cmpgt_epi8(_mm256_set1_epi8(static_cast<char>(end)), lane));
    __m256i clo, llo, chi, lhi;
    code32_avx2(t, b, keep, clo, llo, chi, lhi);
    alignas(32) std::uint64_t c[2][4], l[2][4];
    _mm256_store_si256(reinterpret_cast<__m256i*>(c[0]), clo);
    _mm256_store_si256(reinterpret_cast<__m256i*>(c[1]), chi);
    _mm256_store_si256(reinterpret_cast<__m256i*>(l[0]), llo);
    _mm256_store_si256(reinterpret_cast<__m256i*>(l[1]), lhi);
    w.put(c[0][0], static_cast<unsigned>(l[0][0]));
    w.put(c[0][1], static_cast<unsigned>(l[0][1]));
    w.put(c[1][0], static_cast<unsigned>(l[1][0]));
    w.put(c[1][1], static_cast<unsigned>(l[1][1]));
    w.put(c[0][2], static_cast<unsigned>(l[0][2]));
    w.put(c[0][3], static_cast<unsigned>(l[0][3]));
    w.put(c[1][2], static_cast<unsigned>(l[1][2]));
    w.put(c[1][3], static_cast<unsigned>(l[1][3]));
  }
  w.copy_to(writer);
  return run;
}
VIO_TARGET_POP()
#endif

class FastEncoder {
 public:
  // A run of rows of the same length (an image, an interlace pass, an APNG
  // frame); load(y, dst) stores row y's samples at dst (big-endian 16-bit
  // samples).
  struct Segment {
    std::size_t row_bytes;
    std::uint32_t rows;
    std::function<void(std::uint32_t, std::uint8_t*)> load;
  };

  // Writes a zlib stream holding the filtered rows of an image with `height`
  // rows of `row_bytes` bytes and `bpp` bytes per complete pixel (1 for
  // sub-byte depths), appending it to `out`.  `filters` is the set of filter
  // types allowed (bit f for type f); the one with the smallest residuals is
  // chosen per row.
  template <class RowFn>
  void encode(std::size_t row_bytes, std::uint32_t height, unsigned bpp, unsigned filters,
              RowFn&& load_row, ByteBuffer& out) {
    Segment seg{row_bytes, height, std::forward<RowFn>(load_row)};
    encode(&seg, 1, bpp, filters, out);
  }

  // The same for consecutive segments in one stream.
  void encode(const Segment* segs, std::size_t nsegs, unsigned bpp, unsigned filters,
              ByteBuffer& out) {
    bpp_ = bpp;
    allowed_ = filters ? filters : 1;
    isa_ = filter_isa();
    huff_avx2_ = isa_ == FilterIsa::kAvx2 && simd::support().huffman;
    std::size_t max_rb = 0, total = 0;
    std::uint64_t rows = 0;
    for (std::size_t k = 0; k < nsegs; k++) {
      if (!segs[k].rows || !segs[k].row_bytes) continue;
      max_rb = std::max(max_rb, segs[k].row_bytes);
      total += static_cast<std::size_t>(segs[k].rows) * (segs[k].row_bytes + 1);
      rows += segs[k].rows;
    }
    const std::size_t stride = kRowPad + ((max_rb + 15) & ~std::size_t(15)) + 16;
    rows_.assign(2 * stride, 0);
    Histogram hist;
    std::uint32_t adler = 1;
    std::size_t run = 0;
    // Filters every row of a segment, calling emit(filtered, length).
    auto each_row = [&](const Segment& seg, auto&& emit) {
      rb_ = seg.row_bytes;
      std::memset(rows_.data(), 0, rows_.size());
      for (std::uint32_t y = 0; y < seg.rows; y++) {
        std::uint8_t* cur = rows_.data() + kRowPad + (y & 1) * stride;
        const std::uint8_t* up = rows_.data() + kRowPad + ((y + 1) & 1) * stride;
        seg.load(y, cur);
        emit(cur, up);
      }
    };
    if (total <= kTwoPassBytes) {
      // Small image: filter it all, take the statistics of part of it, then
      // code it in one go.
      std::unique_ptr<std::uint8_t[]> data(new std::uint8_t[total + 32]);
      std::size_t pos = 0;
      for (std::size_t k = 0; k < nsegs; k++) {
        if (!segs[k].rows || !segs[k].row_bytes) continue;
        each_row(segs[k], [&](const std::uint8_t* cur, const std::uint8_t* up) {
          filter_row(data.get() + pos, cur, up);
          pos += rb_ + 1;
        });
      }
      // Statistics of about 8 KiB spread over the data (more would cost
      // small images time and barely change the code).
      const std::size_t step = std::max<std::size_t>(1, total / (8 * 1024));
      const std::size_t piece = std::max<std::size_t>(1, total / std::max<std::uint64_t>(1, rows));
      for (std::size_t p = 0; p < total; p += step * piece)
        hist.count(data.get() + p, std::min(piece, total - p));
      build_code_tables(hist, t_);
      adler = zlib::adler32(1, data.get(), total);
      out.reserve(out.size() + 1024 + 2 * total);
      BitWriter w{out.end()};
      write_headers(w);
      run = encode_bytes(w, data.get(), total, 0);
      finish(w, run, adler, out);
      return;
    }
    // Huffman code from a sample of rows.
    filtered_.assign(max_rb + 1 + 32, 0);
    for (std::size_t k = 0; k < nsegs; k++) {
      const Segment& seg = segs[k];
      if (!seg.rows || !seg.row_bytes) continue;
      rb_ = seg.row_bytes;
      const std::uint32_t step = std::max<std::uint32_t>(2, static_cast<std::uint32_t>(
          std::max<std::uint64_t>(1, rows / 48)));
      std::uint8_t* cur = rows_.data() + kRowPad;
      std::uint8_t* up = cur + stride;
      for (std::uint32_t y = 0; y < seg.rows; y += step) {
        seg.load(y, cur);
        if (y > 0) seg.load(y - 1, up);
        else std::memset(up, 0, rb_);
        filter_row(filtered_.data(), cur, up);
        hist.count(filtered_.data(), rb_ + 1);
      }
    }
    build_code_tables(hist, t_);
    out.reserve(out.size() + 1024 + total / 2);
    BitWriter w{out.end()};
    write_headers(w);
    for (std::size_t k = 0; k < nsegs; k++) {
      if (!segs[k].rows || !segs[k].row_bytes) continue;
      each_row(segs[k], [&](const std::uint8_t* cur, const std::uint8_t* up) {
        const std::size_t line = rb_ + 1;
        filter_row(filtered_.data(), cur, up);
        adler = zlib::adler32(adler, filtered_.data(), line);
        // Grow the output if the remaining room might not do (codes of up
        // to 14 bits per byte).
        if (out.room(w.out) < 2 * line + 64) w.out = out.grow(w.out, 2 * line + 64);
        run = encode_bytes(w, filtered_.data(), line, run);
      });
    }
    finish(w, run, adler, out);
  }

 private:
  static constexpr std::size_t kTwoPassBytes = 256 * 1024;

  std::size_t encode_bytes(BitWriter& w, const std::uint8_t* p, std::size_t n, std::size_t run) {
#if VIO_SIMD_AVX2
    if (huff_avx2_) return encode_bytes_avx2(w, t_, p, n, run);
#endif
    return encode_bytes_scalar(w, t_, p, n, run);
  }

  void filter_row(std::uint8_t* dst, const std::uint8_t* cur, const std::uint8_t* up) {
    detail::filter_row(dst, cur, up, rb_, bpp_, allowed_, isa_);
  }

  void write_headers(BitWriter& w) {
    w.put(0x78, 8);
    w.put(0x01, 8);
    write_block_header(w);
  }

  void finish(BitWriter& w, std::size_t run, std::uint32_t adler, ByteBuffer& out) {
    if (run) emit_run(w, t_, run);
    w.put(t_.code[256], t_.len[256]);
    w.align();
    for (int i = 0; i < 4; i++) w.out[i] = static_cast<std::uint8_t>(adler >> (24 - 8 * i));
    out.set_end(w.out + 4);
  }

  // Dynamic block header: code lengths for 286 literal/length codes and one
  // distance code, run-length coded.
  void write_block_header(BitWriter& w) {
    std::uint8_t lens[287];
    std::memcpy(lens, t_.len, 286);
    lens[286] = 1;  // the distance code
    struct Sym {
      std::uint8_t sym, extra;
    };
    Sym syms[287];
    int nsyms = 0;
    auto push = [&](int sym, int extra) {
      syms[nsyms++] = {static_cast<std::uint8_t>(sym), static_cast<std::uint8_t>(extra)};
    };
    for (int i = 0; i < 287;) {
      int j = i;
      while (j < 287 && lens[j] == lens[i]) j++;
      int r = j - i;
      if (lens[i] == 0) {
        while (r >= 11) {
          int k = std::min(r, 138);
          push(18, k - 11);
          r -= k;
        }
        if (r >= 3) {
          push(17, r - 3);
          r = 0;
        }
      } else {
        push(lens[i], 0);
        r--;
        while (r >= 3) {
          int k = std::min(r, 6);
          push(16, k - 3);
          r -= k;
        }
      }
      while (r-- > 0) push(lens[i], 0);
      i = j;
    }
    std::uint64_t f[19] = {};
    for (int k = 0; k < nsyms; k++) f[syms[k].sym]++;
    std::uint8_t cl[19];
    std::uint16_t cc[19];
    huffman_lengths(f, 19, 7, cl);
    canonical_codes(cl, 19, cc);
    static constexpr std::uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                               11, 4,  12, 3, 13, 2, 14, 1, 15};
    int hclen = 19;
    while (hclen > 4 && cl[order[hclen - 1]] == 0) hclen--;
    w.put(1 | 2 << 1, 3);  // final block, dynamic codes
    w.put(286 - 257, 5);
    w.put(1 - 1, 5);
    w.put(static_cast<unsigned>(hclen - 4), 4);
    for (int i = 0; i < hclen; i++) w.put(cl[order[i]], 3);
    static constexpr std::uint8_t extra_bits[3] = {2, 3, 7};
    for (int k = 0; k < nsyms; k++) {
      const Sym& s = syms[k];
      w.put(cc[s.sym], cl[s.sym]);
      if (s.sym >= 16) w.put(s.extra, extra_bits[s.sym - 16]);
    }
  }

  std::size_t rb_ = 0;
  unsigned bpp_ = 1;
  unsigned allowed_ = 0x1f;
  FilterIsa isa_ = FilterIsa::kScalar;
  bool huff_avx2_ = false;  // AVX2 Huffman coder
  std::vector<std::uint8_t> rows_, filtered_;
  CodeTables t_;
};

}  // namespace vio::png::detail
