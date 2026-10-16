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

#include "simd.hpp"

#if VIO_SIMD_NEON
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_PUSH_("fpu=neon")
#endif
#include <arm_neon.h>
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_POP()
#endif
#endif
#if VIO_SIMD_X86
#include <immintrin.h>
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

// Pixels of 1 or 2 bytes (B): the previous pixel stays in registers (B
// independent chains) instead of being reloaded from the row just stored.
template <unsigned B>
inline void unfilter_small(unsigned filter, std::uint8_t* dst, const std::uint8_t* src, const std::uint8_t* prev,
                           std::size_t n) {
  unsigned a[B] = {}, c[B] = {};
  switch (filter) {
    case kFilterSub:
      for (std::size_t i = 0; i < n; i += B)
        for (unsigned k = 0; k < B; k++) dst[i + k] = static_cast<std::uint8_t>(a[k] = (a[k] + src[i + k]) & 0xff);
      break;
    case kFilterAvg:
      for (std::size_t i = 0; i < n; i += B)
        for (unsigned k = 0; k < B; k++) {
          const unsigned b = prev ? prev[i + k] : 0;
          dst[i + k] = static_cast<std::uint8_t>(a[k] = (src[i + k] + ((a[k] + b) >> 1)) & 0xff);
        }
      break;
    case kFilterPaeth:
      for (std::size_t i = 0; i < n; i += B)
        for (unsigned k = 0; k < B; k++) {
          const int b = prev[i + k], cc = static_cast<int>(c[k]), ak = static_cast<int>(a[k]);
          const int x = ak - cc, e = b - cc, y = x + e;
#if !defined(__clang__)
          // explicitly branchless (GCC and MSVC branch otherwise; the choice
          // is data-dependent and unpredictable)
          const int pa = (e ^ (e >> 31)) - (e >> 31), pb = (x ^ (x >> 31)) - (x >> 31),
                    pc = (y ^ (y >> 31)) - (y >> 31);
          const int use_a = -static_cast<int>((pa <= pb) & (pa <= pc)), use_b = -static_cast<int>(pb <= pc);
          const int bc = cc ^ ((b ^ cc) & use_b);
          const int pred = bc ^ ((ak ^ bc) & use_a);
#else
          const int pa = e < 0 ? -e : e, pb = x < 0 ? -x : x, pc = y < 0 ? -y : y;
          const int pred = pa <= pb && pa <= pc ? ak : pb <= pc ? b : cc;
#endif
          dst[i + k] = static_cast<std::uint8_t>(a[k] = static_cast<unsigned>(src[i + k] + pred) & 0xff);
          c[k] = static_cast<unsigned>(b);
        }
      break;
  }
}

// Scalar reconstruction; prev == nullptr for the first row.
inline void unfilter_row_scalar(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                                const std::uint8_t* prev, std::size_t n, unsigned bpp) {
  if ((bpp == 1 || bpp == 2) && (filter == kFilterSub || filter == kFilterAvg || (filter == kFilterPaeth && prev))) {
    // n is a multiple of bpp
    if (bpp == 1) unfilter_small<1>(filter, dst, src, prev, n);
    else unfilter_small<2>(filter, dst, src, prev, n);
    return;
  }
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
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_PUSH_("fpu=neon")
#endif
// One pixel per step, in the low lanes of a 64-bit vector: pixels of 3 or 4
// bytes through 4-byte loads and stores (one byte too many is stored for 3:
// the next pixel's source byte it overwrites, when unfiltering in place, was
// read already), pixels of 6 or 8 bytes through 8-byte loads and stores of
// exactly the pixel.
template <unsigned W>
struct PxNeon;
template <>
struct PxNeon<4> {
  VIO_INLINE static uint8x8_t load(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return vreinterpret_u8_u32(vdup_n_u32(v));
  }
  VIO_INLINE static void store(std::uint8_t* p, uint8x8_t v, unsigned) {
    const std::uint32_t w = vget_lane_u32(vreinterpret_u32_u8(v), 0);
    std::memcpy(p, &w, 4);
  }
};
template <>
struct PxNeon<8> {
  VIO_INLINE static uint8x8_t load(const std::uint8_t* p) { return vld1_u8(p); }
  VIO_INLINE static void store(std::uint8_t* p, uint8x8_t v, unsigned bpp) {
    if (bpp == 8) {
      vst1_u8(p, v);
    } else {
      const std::uint32_t lo = vget_lane_u32(vreinterpret_u32_u8(v), 0);
      const std::uint16_t hi = vget_lane_u16(vreinterpret_u16_u8(v), 2);
      std::memcpy(p, &lo, 4);
      std::memcpy(p + 4, &hi, 2);
    }
  }
};

VIO_INLINE uint8x8_t paeth_neon(uint8x8_t a, uint8x8_t b, uint8x8_t c) {
  uint8x8_t pa = vabd_u8(b, c), pb = vabd_u8(a, c);
  uint8x8_t diff_sign = veor_u8(vclt_u8(a, c), vclt_u8(b, c));
  uint8x8_t pc = vbsl_u8(diff_sign, vabd_u8(pa, pb), vqadd_u8(pa, pb));
  uint8x8_t use_a = vand_u8(vcle_u8(pa, pb), vcle_u8(pa, pc));
  uint8x8_t use_b = vcle_u8(pb, pc);
  return vbsl_u8(use_a, a, vbsl_u8(use_b, b, c));
}

template <unsigned W>
VIO_INLINE void unfilter_px_neon(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                                 const std::uint8_t* prev, std::size_t n, unsigned bpp) {
  using P = PxNeon<W>;
  uint8x8_t a = vdup_n_u8(0), c = a;
  switch (filter) {
    case kFilterSub:
      for (std::size_t i = 0; i < n; i += bpp) {
        a = vadd_u8(P::load(src + i), a);
        P::store(dst + i, a, bpp);
      }
      break;
    case kFilterAvg:
      if (!prev) {
        for (std::size_t i = 0; i < n; i += bpp) {
          a = vadd_u8(P::load(src + i), vshr_n_u8(a, 1));
          P::store(dst + i, a, bpp);
        }
      } else {
        for (std::size_t i = 0; i < n; i += bpp) {
          a = vadd_u8(P::load(src + i), vhadd_u8(a, P::load(prev + i)));
          P::store(dst + i, a, bpp);
        }
      }
      break;
    case kFilterPaeth:
      if (!prev) {  // b = c = 0: the prediction is a
        for (std::size_t i = 0; i < n; i += bpp) {
          a = vadd_u8(P::load(src + i), a);
          P::store(dst + i, a, bpp);
        }
      } else {
        for (std::size_t i = 0; i < n; i += bpp) {
          const uint8x8_t b = P::load(prev + i);
          a = vadd_u8(P::load(src + i), paeth_neon(a, b, c));
          c = b;
          P::store(dst + i, a, bpp);
        }
      }
      break;
  }
}

inline void unfilter_row_neon(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                              const std::uint8_t* prev, std::size_t n, unsigned bpp) {
  if (filter == kFilterUp && prev) {
    std::size_t i = 0;
    for (; i + 16 <= n; i += 16) vst1q_u8(dst + i, vaddq_u8(vld1q_u8(src + i), vld1q_u8(prev + i)));
    for (; i < n; i++) dst[i] = static_cast<std::uint8_t>(src[i] + prev[i]);
    return;
  }
  if (filter == kFilterNone || filter == kFilterUp || bpp < 3 || bpp == 5 || bpp == 7 || bpp > 8) {
    unfilter_row_scalar(filter, dst, src, prev, n, bpp);
    return;
  }
  // n is a multiple of bpp here.
  if (bpp <= 4) unfilter_px_neon<4>(filter, dst, src, prev, n, bpp);
  else unfilter_px_neon<8>(filter, dst, src, prev, n, bpp);
}
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_POP()
#endif
#endif

#if VIO_SIMD_X86
// Pixels of 3 or 4 bytes go through 4-byte loads (one byte too many is
// stored for 3: the next pixel's source byte it overwrites, when unfiltering
// in place, was read already), pixels of 6 or 8 bytes through 8-byte loads
// and stores of exactly the pixel.
//
// Paeth works on 16-bit lanes: with x = a - c and e = b - c (e and
// pa = |e| computed off the critical path), pb = |x| and pc = |x + e|, so
// the chain from one pixel to the next is a subtraction, two absolute
// values, an addition, a minimum, a comparison and two selections.
VIO_TARGET_PUSH_SSE2()
template <unsigned W>
struct PxSse2;
template <>
struct PxSse2<4> {
  VIO_INLINE static __m128i load(const std::uint8_t* p) {
    std::int32_t v;
    std::memcpy(&v, p, 4);
    return _mm_cvtsi32_si128(v);
  }
  VIO_INLINE static void store(std::uint8_t* p, __m128i v, unsigned) {
    const std::int32_t w = _mm_cvtsi128_si32(v);
    std::memcpy(p, &w, 4);
  }
};
template <>
struct PxSse2<8> {
  VIO_INLINE static __m128i load(const std::uint8_t* p) { return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)); }
  VIO_INLINE static void store(std::uint8_t* p, __m128i v, unsigned bpp) {
    if (bpp == 8) {
      _mm_storel_epi64(reinterpret_cast<__m128i*>(p), v);
    } else {
      const std::int32_t lo = _mm_cvtsi128_si32(v);
      const std::int16_t hi = static_cast<std::int16_t>(_mm_extract_epi16(v, 2));
      std::memcpy(p, &lo, 4);
      std::memcpy(p + 4, &hi, 2);
    }
  }
};
VIO_TARGET_POP()

// The loop, for SSE2 and for SSE4.1 (pabsw, pblendvb); each copy is
// compiled for its instruction set.
#define VIO_PNG_PAETH16(NAME, ABS, SELECT)                                                          \
  template <unsigned W>                                                                           \
  inline void NAME(std::uint8_t* dst, const std::uint8_t* src, const std::uint8_t* prev,          \
                   std::size_t n, unsigned bpp) {                                                 \
    using P = PxSse2<W>;                                                                          \
    const __m128i z = _mm_setzero_si128(), lo = _mm_set1_epi16(0xff);                             \
    __m128i a = z, c = z;                                                                         \
    for (std::size_t i = 0; i < n; i += bpp) {                                                    \
      const __m128i b = _mm_unpacklo_epi8(P::load(prev + i), z),                                  \
                    s = _mm_unpacklo_epi8(P::load(src + i), z);                                   \
      const __m128i e = _mm_sub_epi16(b, c), x = _mm_sub_epi16(a, c), y = _mm_add_epi16(x, e);    \
      const __m128i pa = ABS(e), pb = ABS(x), pc = ABS(y);                                        \
      const __m128i use_c = _mm_cmpgt_epi16(pb, pc);                                              \
      const __m128i not_a = _mm_cmpgt_epi16(pa, _mm_min_epi16(pb, pc));                           \
      const __m128i pred = SELECT(not_a, SELECT(use_c, c, b), a);                                 \
      a = _mm_and_si128(_mm_add_epi16(pred, s), lo);                                              \
      c = b;                                                                                      \
      P::store(dst + i, _mm_packus_epi16(a, a), bpp);                                             \
    }                                                                                             \
  }

VIO_TARGET_PUSH_SSE2()
VIO_INLINE __m128i abs16_sse2(__m128i x) { return _mm_max_epi16(x, _mm_sub_epi16(_mm_setzero_si128(), x)); }
VIO_INLINE __m128i select16_sse2(__m128i m, __m128i x, __m128i y) {
  return _mm_or_si128(_mm_and_si128(m, x), _mm_andnot_si128(m, y));
}
VIO_PNG_PAETH16(paeth_rows_sse2, abs16_sse2, select16_sse2)
VIO_TARGET_POP()

VIO_TARGET_PUSH_("sse4.1")
VIO_INLINE __m128i select16_sse41(__m128i m, __m128i x, __m128i y) { return _mm_blendv_epi8(y, x, m); }
VIO_PNG_PAETH16(paeth_rows_sse41_, _mm_abs_epi16, select16_sse41)
inline void paeth_rows_sse41(std::uint8_t* dst, const std::uint8_t* src, const std::uint8_t* prev, std::size_t n,
                             unsigned bpp) {
  if (bpp <= 4) paeth_rows_sse41_<4>(dst, src, prev, n, bpp);
  else paeth_rows_sse41_<8>(dst, src, prev, n, bpp);
}
VIO_TARGET_POP()
#undef VIO_PNG_PAETH16

VIO_TARGET_PUSH_SSE2()
// 8-bit helpers (also used by the encoder's filters).
VIO_INLINE __m128i absdiff_sse2(__m128i x, __m128i y) {
  return _mm_or_si128(_mm_subs_epu8(x, y), _mm_subs_epu8(y, x));
}
// x <= y (unsigned bytes)
VIO_INLINE __m128i le_sse2(__m128i x, __m128i y) { return _mm_cmpeq_epi8(_mm_min_epu8(x, y), x); }
VIO_INLINE __m128i select_sse2(__m128i m, __m128i x, __m128i y) {
  return _mm_or_si128(_mm_and_si128(m, x), _mm_andnot_si128(m, y));
}

template <unsigned W>
VIO_INLINE void unfilter_px_sse2(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                                 const std::uint8_t* prev, std::size_t n, unsigned bpp) {
  using P = PxSse2<W>;
  __m128i a = _mm_setzero_si128();
  switch (filter) {
    case kFilterSub:
      for (std::size_t i = 0; i < n; i += bpp) {
        a = _mm_add_epi8(P::load(src + i), a);
        P::store(dst + i, a, bpp);
      }
      break;
    case kFilterAvg: {
      const __m128i one = _mm_set1_epi8(1);
      if (!prev) {
        for (std::size_t i = 0; i < n; i += bpp) {
          // (a >> 1) per byte
          const __m128i half = _mm_and_si128(_mm_srli_epi16(a, 1), _mm_set1_epi8(0x7f));
          a = _mm_add_epi8(P::load(src + i), half);
          P::store(dst + i, a, bpp);
        }
      } else {
        for (std::size_t i = 0; i < n; i += bpp) {
          const __m128i b = P::load(prev + i);
          // floor((a + b) / 2) = rounded average minus the dropped low bit
          const __m128i avg = _mm_sub_epi8(_mm_avg_epu8(a, b), _mm_and_si128(_mm_xor_si128(a, b), one));
          a = _mm_add_epi8(P::load(src + i), avg);
          P::store(dst + i, a, bpp);
        }
      }
      break;
    }
    case kFilterPaeth:
      if (!prev) {  // b = c = 0: the prediction is a
        for (std::size_t i = 0; i < n; i += bpp) {
          a = _mm_add_epi8(P::load(src + i), a);
          P::store(dst + i, a, bpp);
        }
      } else {
        if (simd::has(jpeg::simd::kSSE41)) paeth_rows_sse41(dst, src, prev, n, bpp);
        else paeth_rows_sse2<W>(dst, src, prev, n, bpp);
      }
      break;
  }
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
  if (filter == kFilterNone || filter == kFilterUp || bpp < 3 || bpp == 5 || bpp == 7 || bpp > 8) {
    unfilter_row_scalar(filter, dst, src, prev, n, bpp);
    return;
  }
  // n is a multiple of bpp here.
  if (bpp <= 4) unfilter_px_sse2<4>(filter, dst, src, prev, n, bpp);
  else unfilter_px_sse2<8>(filter, dst, src, prev, n, bpp);
}
VIO_TARGET_POP()
#endif

// Generic kernels for a 128-bit vector interface V (AltiVec, MMI, RVV), the
// same algorithms as the SSE2 ones with 8-bit Paeth arithmetic.
template <class V>
struct VecFilter {
  using T = typename V::T;

  // floor((a + b) / 2): rounded average minus the dropped low bit
  static VIO_INLINE T avg_floor(T a, T b) {
    return V::sub_8(V::avg_u8(a, b), V::and_(V::xor_(a, b), V::set1_8(1)));
  }
  static VIO_INLINE T absdiff(T x, T y) { return V::or_(V::subs_u8(x, y), V::subs_u8(y, x)); }
  // x <= y (unsigned bytes)
  static VIO_INLINE T le(T x, T y) { return V::cmpeq_8(V::min_u8(x, y), x); }
  static VIO_INLINE T select(T m, T x, T y) { return V::or_(V::and_(m, x), V::andnot(m, y)); }
  static VIO_INLINE T paeth(T a, T b, T c) {
    const T pa = absdiff(b, c), pb = absdiff(a, c);
    const T diff_sign = V::xor_(le(c, a), le(c, b));  // a < c differs from b < c
    const T pc = select(diff_sign, absdiff(pa, pb), V::adds_u8(pa, pb));
    const T use_a = V::and_(le(pa, pb), le(pa, pc)), use_b = le(pb, pc);
    return select(use_a, a, select(use_b, b, c));
  }

  // Pixels of 3 or 4 bytes (W = 4, one byte too many stored for 3, as in
  // the SSE2 kernels) or of 6 or 8 bytes (W = 8).
  template <unsigned W>
  static VIO_INLINE T load(const std::uint8_t* p) {
    if constexpr (W == 4) return V::loadl_32(p);
    else return V::loadl_64(p);
  }
  template <unsigned W>
  static VIO_INLINE void store(std::uint8_t* p, T v, unsigned bpp) {
    if constexpr (W == 4) {
      V::storel_32(p, v);
    } else if (bpp == 8) {
      V::storel_64(p, v);
    } else {
      std::uint8_t t[8];
      V::storel_64(t, v);
      std::memcpy(p, t, 6);
    }
  }

  template <unsigned W>
  static void unfilter_px(unsigned filter, std::uint8_t* dst, const std::uint8_t* src, const std::uint8_t* prev,
                          std::size_t n, unsigned bpp) {
    T a = V::zero(), c = a;
    if (filter == kFilterSub || (filter == kFilterPaeth && !prev)) {  // Paeth: b = c = 0, the prediction is a
      for (std::size_t i = 0; i < n; i += bpp) {
        a = V::add_8(load<W>(src + i), a);
        store<W>(dst + i, a, bpp);
      }
    } else if (filter == kFilterAvg) {
      for (std::size_t i = 0; i < n; i += bpp) {
        a = V::add_8(load<W>(src + i), avg_floor(a, prev ? load<W>(prev + i) : V::zero()));
        store<W>(dst + i, a, bpp);
      }
    } else {
      for (std::size_t i = 0; i < n; i += bpp) {
        const T b = load<W>(prev + i);
        a = V::add_8(load<W>(src + i), paeth(a, b, c));
        c = b;
        store<W>(dst + i, a, bpp);
      }
    }
  }

  static void unfilter_row(unsigned filter, std::uint8_t* dst, const std::uint8_t* src, const std::uint8_t* prev,
                           std::size_t n, unsigned bpp) {
    if (filter == kFilterUp && prev) {
      std::size_t i = 0;
      for (; i + 16 <= n; i += 16) V::storeu(dst + i, V::add_8(V::loadu(src + i), V::loadu(prev + i)));
      for (; i < n; i++) dst[i] = static_cast<std::uint8_t>(src[i] + prev[i]);
      return;
    }
    if (filter == kFilterNone || filter == kFilterUp || bpp < 3 || bpp == 5 || bpp == 7 || bpp > 8) {
      unfilter_row_scalar(filter, dst, src, prev, n, bpp);
      return;
    }
    if (bpp <= 4) unfilter_px<4>(filter, dst, src, prev, n, bpp);
    else unfilter_px<8>(filter, dst, src, prev, n, bpp);
  }
};

inline void unfilter_row(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                         const std::uint8_t* prev, std::size_t n, unsigned bpp) {
#if VIO_SIMD_NEON
  if (simd::has(jpeg::simd::kNEON)) return unfilter_row_neon(filter, dst, src, prev, n, bpp);
#elif VIO_SIMD_X86
  if (simd::has(jpeg::simd::kSSE2)) return unfilter_row_sse2(filter, dst, src, prev, n, bpp);
#elif VIO_PNG_GENERIC
  if (simd::has(simd::kGenericIsa)) return VecFilter<simd::GV>::unfilter_row(filter, dst, src, prev, n, bpp);
#endif
  unfilter_row_scalar(filter, dst, src, prev, n, bpp);
}

}  // namespace vio::png::detail
