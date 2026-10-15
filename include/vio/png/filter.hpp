// vio::png - scanline filters (PNG filter method 0).
//
// unfilter_row() reconstructs one row in place: the output may start before
// the filtered input (dst <= src), which lets the decoder inflate straight
// into the image buffer and close the gaps left by the filter-type bytes as
// it goes.  Paeth prediction uses only 8-bit arithmetic: with pa = |b - c|
// and pb = |a - c|, pc = |a + b - 2c| is pa + pb (saturated, which keeps the
// comparisons exact) when a - c and b - c have the same sign, else |pa - pb|.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "../jpeg/config.hpp"

#if VIO_SIMD_NEON
#include <arm_neon.h>
#endif
#if VIO_SIMD_X86
#include <emmintrin.h>
#endif

namespace vio::png::detail {

enum : unsigned { kFilterNone = 0, kFilterSub = 1, kFilterUp = 2, kFilterAvg = 3, kFilterPaeth = 4 };

inline std::uint8_t paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c) {
  int p = a + b - c;
  int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

// Scalar reconstruction; prev == nullptr for the first row.
inline void unfilter_row_scalar(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                                const std::uint8_t* prev, std::size_t n, unsigned bpp) {
  switch (filter) {
    case kFilterNone:
      std::memmove(dst, src, n);
      break;
    case kFilterSub:
      for (std::size_t i = 0; i < n; i++)
        dst[i] = static_cast<std::uint8_t>(src[i] + (i >= bpp ? dst[i - bpp] : 0));
      break;
    case kFilterUp:
      if (!prev) {
        std::memmove(dst, src, n);
      } else {
        for (std::size_t i = 0; i < n; i++) dst[i] = static_cast<std::uint8_t>(src[i] + prev[i]);
      }
      break;
    case kFilterAvg:
      for (std::size_t i = 0; i < n; i++) {
        unsigned a = i >= bpp ? dst[i - bpp] : 0, b = prev ? prev[i] : 0;
        dst[i] = static_cast<std::uint8_t>(src[i] + ((a + b) >> 1));
      }
      break;
    case kFilterPaeth:
      for (std::size_t i = 0; i < n; i++) {
        std::uint8_t a = i >= bpp ? dst[i - bpp] : 0, b = prev ? prev[i] : 0,
                     c = i >= bpp && prev ? prev[i - bpp] : 0;
        dst[i] = static_cast<std::uint8_t>(src[i] + paeth(a, b, c));
      }
      break;
  }
}

#if VIO_SIMD_NEON
// One pixel of bpp (3 or 4) bytes per step, held in the low lanes of a
// 64-bit vector.  Loads read, and stores write, 4 bytes: the decoder's
// buffer layout keeps the extra byte in bounds and unused.
VIO_INLINE uint8x8_t load_px(const std::uint8_t* p) {
  std::uint32_t v;
  std::memcpy(&v, p, 4);
  return vreinterpret_u8_u32(vdup_n_u32(v));
}
VIO_INLINE void store_px(std::uint8_t* p, uint8x8_t v) {
  std::uint32_t w = vget_lane_u32(vreinterpret_u32_u8(v), 0);
  std::memcpy(p, &w, 4);
}

VIO_INLINE uint8x8_t paeth_neon(uint8x8_t a, uint8x8_t b, uint8x8_t c) {
  uint8x8_t pa = vabd_u8(b, c), pb = vabd_u8(a, c);
  uint8x8_t diff_sign = veor_u8(vclt_u8(a, c), vclt_u8(b, c));
  uint8x8_t pc = vbsl_u8(diff_sign, vabd_u8(pa, pb), vqadd_u8(pa, pb));
  uint8x8_t use_a = vand_u8(vcle_u8(pa, pb), vcle_u8(pa, pc));
  uint8x8_t use_b = vcle_u8(pb, pc);
  return vbsl_u8(use_a, a, vbsl_u8(use_b, b, c));
}

inline void unfilter_row_neon(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                              const std::uint8_t* prev, std::size_t n, unsigned bpp) {
  if (filter == kFilterUp && prev) {
    std::size_t i = 0;
    for (; i + 16 <= n; i += 16) vst1q_u8(dst + i, vaddq_u8(vld1q_u8(src + i), vld1q_u8(prev + i)));
    for (; i < n; i++) dst[i] = static_cast<std::uint8_t>(src[i] + prev[i]);
    return;
  }
  if ((bpp != 3 && bpp != 4) || filter == kFilterNone || filter == kFilterUp) {
    unfilter_row_scalar(filter, dst, src, prev, n, bpp);
    return;
  }
  // n is a multiple of bpp here.
  uint8x8_t a = vdup_n_u8(0), c = a;
  switch (filter) {
    case kFilterSub:
      for (std::size_t i = 0; i < n; i += bpp) {
        a = vadd_u8(load_px(src + i), a);
        store_px(dst + i, a);
      }
      break;
    case kFilterAvg:
      if (!prev) {
        for (std::size_t i = 0; i < n; i += bpp) {
          a = vadd_u8(load_px(src + i), vshr_n_u8(a, 1));
          store_px(dst + i, a);
        }
      } else {
        for (std::size_t i = 0; i < n; i += bpp) {
          a = vadd_u8(load_px(src + i), vhadd_u8(a, load_px(prev + i)));
          store_px(dst + i, a);
        }
      }
      break;
    case kFilterPaeth:
      if (!prev) {  // b = c = 0: the prediction is a
        for (std::size_t i = 0; i < n; i += bpp) {
          a = vadd_u8(load_px(src + i), a);
          store_px(dst + i, a);
        }
      } else {
        for (std::size_t i = 0; i < n; i += bpp) {
          uint8x8_t b = load_px(prev + i);
          a = vadd_u8(load_px(src + i), paeth_neon(a, b, c));
          c = b;
          store_px(dst + i, a);
        }
      }
      break;
  }
}
#endif

#if VIO_SIMD_X86
VIO_TARGET_PUSH_SSE2()
VIO_INLINE __m128i load_px_sse2(const std::uint8_t* p) {
  std::int32_t v;
  std::memcpy(&v, p, 4);
  return _mm_cvtsi32_si128(v);
}
VIO_INLINE void store_px_sse2(std::uint8_t* p, __m128i v) {
  std::int32_t w = _mm_cvtsi128_si32(v);
  std::memcpy(p, &w, 4);
}
VIO_INLINE __m128i absdiff_sse2(__m128i x, __m128i y) {
  return _mm_or_si128(_mm_subs_epu8(x, y), _mm_subs_epu8(y, x));
}
// x <= y (unsigned bytes)
VIO_INLINE __m128i le_sse2(__m128i x, __m128i y) { return _mm_cmpeq_epi8(_mm_min_epu8(x, y), x); }
VIO_INLINE __m128i select_sse2(__m128i m, __m128i x, __m128i y) {
  return _mm_or_si128(_mm_and_si128(m, x), _mm_andnot_si128(m, y));
}
VIO_INLINE __m128i paeth_sse2(__m128i a, __m128i b, __m128i c) {
  __m128i pa = absdiff_sse2(b, c), pb = absdiff_sse2(a, c);
  // a < c differs from b < c
  __m128i a_lt = _mm_andnot_si128(le_sse2(c, a), _mm_set1_epi8(-1));
  __m128i b_lt = _mm_andnot_si128(le_sse2(c, b), _mm_set1_epi8(-1));
  __m128i pc = select_sse2(_mm_xor_si128(a_lt, b_lt), absdiff_sse2(pa, pb), _mm_adds_epu8(pa, pb));
  __m128i use_a = _mm_and_si128(le_sse2(pa, pb), le_sse2(pa, pc));
  __m128i use_b = le_sse2(pb, pc);
  return select_sse2(use_a, a, select_sse2(use_b, b, c));
}

inline void unfilter_row_sse2(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                              const std::uint8_t* prev, std::size_t n, unsigned bpp) {
  if (filter == kFilterUp && prev) {
    std::size_t i = 0;
    for (; i + 16 <= n; i += 16)
      _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i),
                       _mm_add_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i)),
                                    _mm_loadu_si128(reinterpret_cast<const __m128i*>(prev + i))));
    for (; i < n; i++) dst[i] = static_cast<std::uint8_t>(src[i] + prev[i]);
    return;
  }
  if ((bpp != 3 && bpp != 4) || filter == kFilterNone || filter == kFilterUp) {
    unfilter_row_scalar(filter, dst, src, prev, n, bpp);
    return;
  }
  __m128i a = _mm_setzero_si128(), c = a;
  switch (filter) {
    case kFilterSub:
      for (std::size_t i = 0; i < n; i += bpp) {
        a = _mm_add_epi8(load_px_sse2(src + i), a);
        store_px_sse2(dst + i, a);
      }
      break;
    case kFilterAvg: {
      const __m128i one = _mm_set1_epi8(1);
      if (!prev) {
        for (std::size_t i = 0; i < n; i += bpp) {
          // (a >> 1) per byte
          __m128i half = _mm_and_si128(_mm_srli_epi16(a, 1), _mm_set1_epi8(0x7f));
          a = _mm_add_epi8(load_px_sse2(src + i), half);
          store_px_sse2(dst + i, a);
        }
      } else {
        for (std::size_t i = 0; i < n; i += bpp) {
          __m128i b = load_px_sse2(prev + i);
          // floor((a + b) / 2) = rounded average minus the dropped low bit
          __m128i avg = _mm_sub_epi8(_mm_avg_epu8(a, b), _mm_and_si128(_mm_xor_si128(a, b), one));
          a = _mm_add_epi8(load_px_sse2(src + i), avg);
          store_px_sse2(dst + i, a);
        }
      }
      break;
    }
    case kFilterPaeth:
      if (!prev) {
        for (std::size_t i = 0; i < n; i += bpp) {
          a = _mm_add_epi8(load_px_sse2(src + i), a);
          store_px_sse2(dst + i, a);
        }
      } else {
        for (std::size_t i = 0; i < n; i += bpp) {
          __m128i b = load_px_sse2(prev + i);
          a = _mm_add_epi8(load_px_sse2(src + i), paeth_sse2(a, b, c));
          c = b;
          store_px_sse2(dst + i, a);
        }
      }
      break;
  }
}
VIO_TARGET_POP()
#endif

inline void unfilter_row(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                         const std::uint8_t* prev, std::size_t n, unsigned bpp) {
#if VIO_SIMD_NEON_A64
  unfilter_row_neon(filter, dst, src, prev, n, bpp);
#elif VIO_SIMD_X86_64
  unfilter_row_sse2(filter, dst, src, prev, n, bpp);
#else
  unfilter_row_scalar(filter, dst, src, prev, n, bpp);
#endif
}

}  // namespace vio::png::detail
