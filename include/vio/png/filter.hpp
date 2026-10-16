// vio::png - scanline filters (PNG filter method 0).
//
// unfilter_row() reconstructs one row in place: the output may start before
// the filtered input (dst <= src), which lets the decoder inflate straight
// into the image buffer and close the gaps left by the filter-type bytes as
// it goes.  Paeth prediction uses only 8-bit arithmetic: with pa = |b - c|
// and pb = |a - c|, pc = |a + b - 2c| is pa + pb (saturated, which keeps the
// comparisons exact) when a - c and b - c have the same sign, else |pa - pb|.
#pragma once

#include <algorithm>
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

// Paeth with the prediction's dependence on a precomputed: for given b and
// c, the a that predict b or c form at most two adjacent ranges, [L, S) and
// [S, U], each predicting one of them (lo, hi); every other a predicts
// itself.  With e = |b - c|, for b >= c: L = c - 2e + 1, S = c - (e >> 1),
// U = b - 1, lo = c, hi = b; for b < c: L = b + 1, S = c + (e >> 1) + 1,
// U = c + 2e - 1, lo = b, hi = c (bounds saturated to [0, 255]; when S
// saturates hi becomes lo; b = c gives L = U = b, whose prediction b is a).
// A first pass computes the bounds and x + lo, x + hi (x the filtered byte)
// for a block of the row, 16 bytes at a time; the pass along the row is
// then three comparisons and two selections per pixel.
struct PaethBlock {
  static constexpr std::size_t kBytes = 192;  // a multiple of 3, 4, 6 and 8
  alignas(16) std::uint8_t l[kBytes + 16], u[kBytes + 16], s[kBytes + 16], xl[kBytes + 16], xh[kBytes + 16];
};

// The bounds of bytes [j0, j1) of a block, from the filtered bytes x[j],
// the bytes b[j] above them and c[j - j0] above and left (scalar code, in a loop compilers
// vectorize: b >= c is chosen by masks).
VIO_INLINE void paeth_bounds_scalar(PaethBlock& t, std::size_t j0, std::size_t j1, const std::uint8_t* x,
                                    const std::uint8_t* b, const std::uint8_t* c) {
  std::uint8_t* VIO_RESTRICT tl = t.l;
  std::uint8_t* VIO_RESTRICT tu = t.u;
  std::uint8_t* VIO_RESTRICT ts = t.s;
  std::uint8_t* VIO_RESTRICT txl = t.xl;
  std::uint8_t* VIO_RESTRICT txh = t.xh;
  for (std::size_t j = j0; j < j1; j++) {
    const int bj = b[j], cj = c[j - j0];
    const int ge = -(bj >= cj);  // all ones if b >= c
    const int e = std::abs(bj - cj), eh = e >> 1;
    const int l1 = std::max(0, cj - std::max(2 * e - 1, 0)), l2 = bj + 1;
    const int u1 = bj - (e > 0), u2 = std::min(255, cj + 2 * e - 1);
    const int s1 = std::max(0, cj - eh), s2 = std::min(255, cj + eh + 1);
    const int hi2 = cj + eh < 255 ? cj : bj;
    tl[j] = static_cast<std::uint8_t>((l1 & ge) | (l2 & ~ge));
    tu[j] = static_cast<std::uint8_t>((u1 & ge) | (u2 & ~ge));
    ts[j] = static_cast<std::uint8_t>((s1 & ge) | (s2 & ~ge));
    txl[j] = static_cast<std::uint8_t>(x[j] + ((cj & ge) | (bj & ~ge)));
    txh[j] = static_cast<std::uint8_t>(x[j] + ((bj & ge) | (hi2 & ~ge)));
  }
}

// The bounds for bytes [i0, i0 + m) of a row.
inline void paeth_bounds_scalar(PaethBlock& t, const std::uint8_t* src, const std::uint8_t* prev, std::size_t i0,
                                std::size_t m, unsigned bpp) {
  std::size_t j = 0;
  if (i0 == 0) {  // c = 0 left of the first pixel
    static constexpr std::uint8_t zero[8] = {};
    paeth_bounds_scalar(t, 0, bpp, src, prev, zero);
    j = bpp;
  }
  paeth_bounds_scalar(t, j, m, src + i0, prev + i0, prev + i0 + j - bpp);
}

// Pixels of B bytes (B <= 8): the previous pixel stays in registers (B
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
      if constexpr (B <= 2) {
        // one or two chains along the row: the precomputed bounds shorten them
        PaethBlock t;
        for (std::size_t i0 = 0; i0 < n; i0 += PaethBlock::kBytes) {
          const std::size_t m = std::min(PaethBlock::kBytes, n - i0);
          paeth_bounds_scalar(t, src, prev, i0, m, B);
          for (std::size_t j = 0; j < m; j += B)
            for (unsigned k = 0; k < B; k++) {
              // selections by masks (compilers branch otherwise, unpredictably)
              const unsigned ak = a[k], xa = (src[i0 + j + k] + ak) & 0xff;
              const unsigned ml = 0u - (ak >= t.l[j + k]), mu = 0u - (ak > t.u[j + k]), ms = 0u - (ak >= t.s[j + k]);
              const unsigned v1 = xa ^ ((xa ^ t.xl[j + k]) & ml), v2 = t.xh[j + k] ^ ((xa ^ t.xh[j + k]) & mu);
              dst[i0 + j + k] = static_cast<std::uint8_t>(a[k] = v1 ^ ((v1 ^ v2) & ms));
            }
        }
      } else {
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
      }
      break;
  }
}

// Scalar reconstruction; prev == nullptr for the first row.
inline void unfilter_row_scalar(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                                const std::uint8_t* prev, std::size_t n, unsigned bpp) {
  if (bpp <= 8 && (filter == kFilterSub || filter == kFilterAvg || (filter == kFilterPaeth && prev))) {
    // n is a multiple of bpp
    switch (bpp) {
      case 1: return unfilter_small<1>(filter, dst, src, prev, n);
      case 2: return unfilter_small<2>(filter, dst, src, prev, n);
      case 3: return unfilter_small<3>(filter, dst, src, prev, n);
      case 4: return unfilter_small<4>(filter, dst, src, prev, n);
      case 6: return unfilter_small<6>(filter, dst, src, prev, n);
      case 8: return unfilter_small<8>(filter, dst, src, prev, n);
      default: break;
    }
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

VIO_INLINE void paeth_bounds_neon(PaethBlock& t, std::size_t j, uint8x16_t x, uint8x16_t b, uint8x16_t c) {
  const uint8x16_t one = vdupq_n_u8(1);
  const uint8x16_t ge = vcgeq_u8(b, c), gt = vcgtq_u8(b, c);
  const uint8x16_t e = vabdq_u8(b, c), e2 = vqaddq_u8(e, e), eh = vshrq_n_u8(e, 1);
  const uint8x16_t l = vbslq_u8(ge, vqsubq_u8(c, vqsubq_u8(e2, one)), vaddq_u8(b, one));
  const uint8x16_t u = vbslq_u8(ge, vaddq_u8(b, gt), vqaddq_u8(vsubq_u8(c, one), e2));
  const uint8x16_t t2 = vqaddq_u8(c, eh);
  const uint8x16_t s = vbslq_u8(ge, vqsubq_u8(c, eh), vqaddq_u8(t2, one));
  const uint8x16_t lo = vbslq_u8(ge, c, b);
  const uint8x16_t hi = vbslq_u8(vbicq_u8(vceqq_u8(t2, vdupq_n_u8(0xff)), ge), lo, vbslq_u8(ge, b, c));
  vst1q_u8(t.l + j, l);
  vst1q_u8(t.u + j, u);
  vst1q_u8(t.s + j, s);
  vst1q_u8(t.xl + j, vaddq_u8(x, lo));
  vst1q_u8(t.xh + j, vaddq_u8(x, hi));
}

template <unsigned W>
inline void paeth_row_neon(std::uint8_t* dst, const std::uint8_t* src, const std::uint8_t* prev, std::size_t n,
                           unsigned bpp) {
  using P = PxNeon<W>;
  PaethBlock t;
  uint8x8_t a = vdup_n_u8(0);
  for (std::size_t i0 = 0; i0 < n; i0 += PaethBlock::kBytes) {
    const std::size_t m = std::min(PaethBlock::kBytes, n - i0);
    for (std::size_t j = 0; j < m; j += 16) {
      const std::size_t i = i0 + j;
      uint8x16_t c;
      if (i >= bpp) {
        c = vld1q_u8(prev + i - bpp);
      } else {
        std::uint8_t z[16] = {};
        std::memcpy(z + bpp, prev, 16 - bpp);
        c = vld1q_u8(z);
      }
      paeth_bounds_neon(t, j, vld1q_u8(src + i), vld1q_u8(prev + i), c);
    }
    for (std::size_t j = 0; j < m; j += bpp) {
      const uint8x8_t xa = vadd_u8(P::load(src + i0 + j), a);
      const uint8x8_t v1 = vbsl_u8(vcge_u8(a, P::load(t.l + j)), P::load(t.xl + j), xa);
      const uint8x8_t v2 = vbsl_u8(vcgt_u8(a, P::load(t.u + j)), xa, P::load(t.xh + j));
      a = vbsl_u8(vcge_u8(a, P::load(t.s + j)), v2, v1);
      P::store(dst + i0 + j, a, bpp);
    }
  }
}

template <unsigned W>
VIO_INLINE void unfilter_px_neon(unsigned filter, std::uint8_t* dst, const std::uint8_t* src,
                                 const std::uint8_t* prev, std::size_t n, unsigned bpp) {
  using P = PxNeon<W>;
  uint8x8_t a = vdup_n_u8(0);
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
        paeth_row_neon<W>(dst, src, prev, n, bpp);
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

// Paeth as in the NEON kernel (see PaethBlock), with every byte of the pass
// along the row offset by 0x80 so that signed comparisons order them.
VIO_TARGET_PUSH_SSE2()
VIO_INLINE __m128i select8_sse2(__m128i m, __m128i x, __m128i y) {
  return _mm_or_si128(_mm_and_si128(m, x), _mm_andnot_si128(m, y));
}
inline void paeth_bounds_sse2(PaethBlock& t, std::size_t j, __m128i x, __m128i b, __m128i c) {
  const __m128i one = _mm_set1_epi8(1), ff = _mm_set1_epi8(-1), bias = _mm_set1_epi8(-128);
  const __m128i ge = _mm_cmpeq_epi8(_mm_max_epu8(b, c), b), eq = _mm_cmpeq_epi8(b, c);
  const __m128i gt = _mm_andnot_si128(eq, ge);
  const __m128i e = _mm_or_si128(_mm_subs_epu8(b, c), _mm_subs_epu8(c, b));
  const __m128i e2 = _mm_adds_epu8(e, e), eh = _mm_and_si128(_mm_srli_epi16(e, 1), _mm_set1_epi8(0x7f));
  const __m128i l = select8_sse2(ge, _mm_subs_epu8(c, _mm_subs_epu8(e2, one)), _mm_add_epi8(b, one));
  const __m128i u = select8_sse2(ge, _mm_add_epi8(b, gt), _mm_adds_epu8(_mm_sub_epi8(c, one), e2));
  const __m128i t2 = _mm_adds_epu8(c, eh);
  const __m128i s = select8_sse2(ge, _mm_subs_epu8(c, eh), _mm_adds_epu8(t2, one));
  const __m128i lo = select8_sse2(ge, c, b);
  const __m128i hi = select8_sse2(_mm_andnot_si128(ge, _mm_cmpeq_epi8(t2, ff)), lo, select8_sse2(ge, b, c));
  _mm_store_si128(reinterpret_cast<__m128i*>(t.l + j), _mm_xor_si128(l, bias));
  _mm_store_si128(reinterpret_cast<__m128i*>(t.u + j), _mm_xor_si128(u, bias));
  _mm_store_si128(reinterpret_cast<__m128i*>(t.s + j), _mm_xor_si128(s, bias));
  _mm_store_si128(reinterpret_cast<__m128i*>(t.xl + j), _mm_xor_si128(_mm_add_epi8(x, lo), bias));
  _mm_store_si128(reinterpret_cast<__m128i*>(t.xh + j), _mm_xor_si128(_mm_add_epi8(x, hi), bias));
}
VIO_TARGET_POP()

// The pass along the row, for SSE2 and for SSE4.1 (pblendvb); each copy is
// compiled for its instruction set.
#define VIO_PNG_PAETH(NAME, SELECT)                                                                  \
  template <unsigned W>                                                                           \
  inline void NAME(std::uint8_t* dst, const std::uint8_t* src, const std::uint8_t* prev,          \
                   std::size_t n, unsigned bpp) {                                                 \
    using P = PxSse2<W>;                                                                          \
    const __m128i bias = _mm_set1_epi8(-128);                                                     \
    PaethBlock t;                                                                                 \
    __m128i a = bias; /* a = 0, offset */                                                         \
    for (std::size_t i0 = 0; i0 < n; i0 += PaethBlock::kBytes) {                                  \
      const std::size_t m = std::min(PaethBlock::kBytes, n - i0);                                 \
      for (std::size_t j = 0; j < m; j += 16) {                                                   \
        const std::size_t i = i0 + j;                                                             \
        __m128i c;                                                                                \
        if (i >= bpp) {                                                                           \
          c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(prev + i - bpp));                  \
        } else {                                                                                  \
          alignas(16) std::uint8_t z[16] = {};                                                    \
          std::memcpy(z + bpp, prev, 16 - bpp);                                                   \
          c = _mm_load_si128(reinterpret_cast<const __m128i*>(z));                                \
        }                                                                                         \
        paeth_bounds_sse2(t, j, _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i)),      \
                          _mm_loadu_si128(reinterpret_cast<const __m128i*>(prev + i)), c);        \
      }                                                                                           \
      for (std::size_t j = 0; j < m; j += bpp) {                                                  \
        const __m128i xa = _mm_add_epi8(P::load(src + i0 + j), a);                                \
        const __m128i v1 = SELECT(_mm_cmpgt_epi8(P::load(t.l + j), a), xa, P::load(t.xl + j));    \
        const __m128i v2 = SELECT(_mm_cmpgt_epi8(a, P::load(t.u + j)), xa, P::load(t.xh + j));    \
        a = SELECT(_mm_cmpgt_epi8(P::load(t.s + j), a), v1, v2);                                  \
        P::store(dst + i0 + j, _mm_xor_si128(a, bias), bpp);                                      \
      }                                                                                           \
    }                                                                                             \
  }

VIO_TARGET_PUSH_SSE2()
VIO_PNG_PAETH(paeth_rows_sse2, select8_sse2)
VIO_TARGET_POP()

VIO_TARGET_PUSH_("sse4.1")
VIO_INLINE __m128i select8_sse41(__m128i m, __m128i x, __m128i y) { return _mm_blendv_epi8(y, x, m); }
VIO_PNG_PAETH(paeth_rows_sse41_, select8_sse41)
inline void paeth_rows_sse41(std::uint8_t* dst, const std::uint8_t* src, const std::uint8_t* prev, std::size_t n,
                             unsigned bpp) {
  if (bpp <= 4) paeth_rows_sse41_<4>(dst, src, prev, n, bpp);
  else paeth_rows_sse41_<8>(dst, src, prev, n, bpp);
}
VIO_TARGET_POP()
#undef VIO_PNG_PAETH

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
