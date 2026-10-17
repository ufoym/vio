// vio::png::simd - 128-bit vector interface `V` used by the PNG generic
// vector kernels (PowerPC AltiVec, Loongson MMI, RISC-V Vector, and x86-64
// SSE2 for Adler-32).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "cpu.hpp"

#if VIO_SIMD_ALTIVEC
#include <altivec.h>
// altivec.h defines these as macros in C++, which breaks bool/std::vector.
#undef vector
#undef pixel
#undef bool
#endif
#if VIO_SIMD_MMI
#include <loongson-mmiintrin.h>
#endif
#if VIO_SIMD_X86_64
#include <immintrin.h>
#endif

namespace vio::png::simd {

#if VIO_SIMD_X86_64
// SSE2, the x86-64 baseline: generic kernels where SSE2 is all there is.
namespace sse2 {
struct V {
  using T = __m128i;

  static VIO_INLINE T zero() { return _mm_setzero_si128(); }
  static VIO_INLINE T set1_8(int v) { return _mm_set1_epi8(static_cast<char>(v)); }
  static VIO_INLINE T set1_16(int v) { return _mm_set1_epi16(static_cast<short>(v)); }
  static VIO_INLINE T loadu(const void* p) { return _mm_loadu_si128(static_cast<const T*>(p)); }
  static VIO_INLINE void storeu(void* p, T v) { _mm_storeu_si128(static_cast<T*>(p), v); }
  static VIO_INLINE T add_16(T a, T b) { return _mm_add_epi16(a, b); }
  static VIO_INLINE T add_32(T a, T b) { return _mm_add_epi32(a, b); }
  static VIO_INLINE T madd_16(T a, T b) { return _mm_madd_epi16(a, b); }
  static VIO_INLINE T widenlo_u8(T a) { return _mm_unpacklo_epi8(a, zero()); }
  static VIO_INLINE T widenhi_u8(T a) { return _mm_unpackhi_epi8(a, zero()); }
};
}  // namespace sse2
#endif

#if VIO_SIMD_ALTIVEC
namespace altivec {
struct V {
  using U8 = __vector unsigned char;
  using S16 = __vector signed short;
  using U16 = __vector unsigned short;
  using U32 = __vector unsigned int;
  using T = U8;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  static constexpr bool kBigEndian = true;
#else
  static constexpr bool kBigEndian = false;
#endif

  static VIO_INLINE T zero() { return vec_splats(static_cast<unsigned char>(0)); }
  static VIO_INLINE T set1_8(int v) { return (T)vec_splats(static_cast<signed char>(v)); }
  static VIO_INLINE T set1_16(int v) { return (T)vec_splats(static_cast<short>(v)); }

  static VIO_INLINE T loadu(const void* p) {
#if defined(__VSX__)
    return vec_xl(0, static_cast<const unsigned char*>(p));
#else
    const unsigned char* c = static_cast<const unsigned char*>(p);
    return vec_perm(vec_ld(0, c), vec_ld(15, c), vec_lvsl(0, c));
#endif
  }
  static VIO_INLINE void storeu(void* p, T v) {
#if defined(__VSX__)
    vec_xst(v, 0, static_cast<unsigned char*>(p));
#else
    alignas(16) unsigned char t[16];
    vec_st(v, 0, t);
    std::memcpy(p, t, 16);
#endif
  }
  static VIO_INLINE T loadl_32(const void* p) {
    alignas(16) unsigned char t[16] = {};
    std::memcpy(t, p, 4);
    return vec_ld(0, t);
  }
  static VIO_INLINE T loadl_64(const void* p) {
    alignas(16) unsigned char t[16] = {};
    std::memcpy(t, p, 8);
    return vec_ld(0, t);
  }
  static VIO_INLINE void storel_32(void* p, T v) {
    alignas(16) unsigned char t[16];
    vec_st(v, 0, t);
    std::memcpy(p, t, 4);
  }
  static VIO_INLINE void storel_64(void* p, T v) {
    alignas(16) unsigned char t[16];
    vec_st(v, 0, t);
    std::memcpy(p, t, 8);
  }

  static VIO_INLINE T add_8(T a, T b) { return vec_add(a, b); }
  static VIO_INLINE T sub_8(T a, T b) { return vec_sub(a, b); }
  static VIO_INLINE T avg_u8(T a, T b) { return vec_avg(a, b); }
  static VIO_INLINE T min_u8(T a, T b) { return vec_min(a, b); }
  static VIO_INLINE T adds_u8(T a, T b) { return vec_adds(a, b); }
  static VIO_INLINE T subs_u8(T a, T b) { return vec_subs(a, b); }
  static VIO_INLINE T cmpeq_8(T a, T b) { return (T)vec_cmpeq(a, b); }

  static VIO_INLINE T add_16(T a, T b) { return (T)vec_add((U16)a, (U16)b); }
  static VIO_INLINE T add_32(T a, T b) { return (T)vec_add((U32)a, (U32)b); }
  static VIO_INLINE T madd_16(T a, T b) { return (T)vec_msums((S16)a, (S16)b, vec_splats(0)); }

  static VIO_INLINE T and_(T a, T b) { return vec_and(a, b); }
  static VIO_INLINE T or_(T a, T b) { return vec_or(a, b); }
  static VIO_INLINE T xor_(T a, T b) { return vec_xor(a, b); }
  static VIO_INLINE T andnot(T a, T b) { return vec_andc(b, a); }

  static VIO_INLINE T widenlo_u8(T a) { return kBigEndian ? vec_mergeh(zero(), a) : vec_mergeh(a, zero()); }
  static VIO_INLINE T widenhi_u8(T a) { return kBigEndian ? vec_mergel(zero(), a) : vec_mergel(a, zero()); }
};
}  // namespace altivec
#endif

#if VIO_SIMD_MMI
namespace mmi {
struct V {
  using U8 = uint8x8_t;
  using S16 = int16x4_t;
  using S32 = int32x2_t;
  struct T {
    U8 lo, hi;
  };

  template <class X> static VIO_INLINE X as(U8 v) { return (X)v; }
  template <class X> static VIO_INLINE U8 u8(X v) { return (U8)v; }
  static VIO_INLINE U8 from_q(std::uint64_t v) { return (U8)v; }
  template <class F> static VIO_INLINE T map(T a, T b, F f) { return {f(a.lo, b.lo), f(a.hi, b.hi)}; }

  static VIO_INLINE T zero() { return {U8{}, U8{}}; }
  static VIO_INLINE T set1_8(int v) {
    const std::uint8_t x = static_cast<std::uint8_t>(v);
    U8 h = {x, x, x, x, x, x, x, x};
    return {h, h};
  }
  static VIO_INLINE T set1_16(int v) {
    const std::int16_t x = static_cast<std::int16_t>(v);
    U8 h = u8(S16{x, x, x, x});
    return {h, h};
  }

  static VIO_INLINE T loadu(const void* p) {
    T v;
    std::memcpy(&v.lo, p, 8);
    std::memcpy(&v.hi, static_cast<const char*>(p) + 8, 8);
    return v;
  }
  static VIO_INLINE void storeu(void* p, T v) {
    std::memcpy(p, &v.lo, 8);
    std::memcpy(static_cast<char*>(p) + 8, &v.hi, 8);
  }
  static VIO_INLINE T loadl_32(const void* p) {
    std::uint32_t x;
    std::memcpy(&x, p, 4);
    return {from_q(x), U8{}};
  }
  static VIO_INLINE T loadl_64(const void* p) {
    T v = zero();
    std::memcpy(&v.lo, p, 8);
    return v;
  }
  static VIO_INLINE void storel_32(void* p, T v) { std::memcpy(p, &v.lo, 4); }
  static VIO_INLINE void storel_64(void* p, T v) { std::memcpy(p, &v.lo, 8); }

  static VIO_INLINE T add_8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return paddb_u(x, y); }); }
  static VIO_INLINE T sub_8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return psubb_u(x, y); }); }
  static VIO_INLINE T avg_u8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return pavgb(x, y); }); }
  // min(a, b) = avg(a, b) rounded up - ceil(|a - b| / 2), a - min(a, b):
  // not pminub or psubusb, which QEMU emulates wrongly (pminub on 4 bytes,
  // psubusb without the lower bound), nor pcmpgtb, signed on hardware but
  // unsigned under QEMU.
  static VIO_INLINE U8 min8(U8 x, U8 y) { return psubb_u(pavgb(x, y), pavgb(pasubub(x, y), U8{})); }
  static VIO_INLINE T min_u8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return min8(x, y); }); }
  static VIO_INLINE T subs_u8(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return psubb_u(x, min8(x, y)); });
  }
  static VIO_INLINE T adds_u8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return paddusb(x, y); }); }
  static VIO_INLINE T cmpeq_8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return pcmpeqb_u(x, y); }); }

  static VIO_INLINE T add_16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(paddh_s(as<S16>(x), as<S16>(y))); });
  }
  static VIO_INLINE T add_32(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(paddw_s(as<S32>(x), as<S32>(y))); });
  }
  static VIO_INLINE T madd_16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(pmaddhw(as<S16>(x), as<S16>(y))); });
  }

  static VIO_INLINE T and_(T a, T b) { return {a.lo & b.lo, a.hi & b.hi}; }
  static VIO_INLINE T or_(T a, T b) { return {a.lo | b.lo, a.hi | b.hi}; }
  static VIO_INLINE T xor_(T a, T b) { return {a.lo ^ b.lo, a.hi ^ b.hi}; }
  static VIO_INLINE T andnot(T a, T b) { return {~a.lo & b.lo, ~a.hi & b.hi}; }

  static VIO_INLINE T widenlo_u8(T a) { return {punpcklbh_u(a.lo, U8{}), punpckhbh_u(a.lo, U8{})}; }
  static VIO_INLINE T widenhi_u8(T a) { return {punpcklbh_u(a.hi, U8{}), punpckhbh_u(a.hi, U8{})}; }
};
}  // namespace mmi
#endif

#if VIO_SIMD_RVV
namespace rvv {
struct V {
  using U8 = std::uint8_t __attribute__((vector_size(16)));
  using I8 = std::int8_t __attribute__((vector_size(16)));
  using U16 = std::uint16_t __attribute__((vector_size(16)));
  using I16 = std::int16_t __attribute__((vector_size(16)));
  using U32 = std::uint32_t __attribute__((vector_size(16)));
  using I32x8 = std::int32_t __attribute__((vector_size(32)));
  using U32x8 = std::uint32_t __attribute__((vector_size(32)));
  using T = U8;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  static constexpr bool kBigEndian = true;
#else
  static constexpr bool kBigEndian = false;
#endif

  static VIO_INLINE T zero() { return T{}; }
  static VIO_INLINE T set1_8(int v) { return (T)(I8{} + static_cast<std::int8_t>(v)); }
  static VIO_INLINE T set1_16(int v) { return (T)(I16{} + static_cast<std::int16_t>(v)); }

  static VIO_INLINE T loadu(const void* p) {
    T v;
    std::memcpy(&v, p, 16);
    return v;
  }
  static VIO_INLINE void storeu(void* p, T v) { std::memcpy(p, &v, 16); }
  static VIO_INLINE T loadl_32(const void* p) {
    T v{};
    std::memcpy(&v, p, 4);
    return v;
  }
  static VIO_INLINE T loadl_64(const void* p) {
    T v{};
    std::memcpy(&v, p, 8);
    return v;
  }
  static VIO_INLINE void storel_32(void* p, T v) { std::memcpy(p, &v, 4); }
  static VIO_INLINE void storel_64(void* p, T v) { std::memcpy(p, &v, 8); }

  static VIO_INLINE T add_8(T a, T b) { return a + b; }
  static VIO_INLINE T sub_8(T a, T b) { return a - b; }
  static VIO_INLINE T avg_u8(T a, T b) { return (a | b) - ((a ^ b) >> 1); }
  static VIO_INLINE T min_u8(T a, T b) { return a < b ? a : b; }
  static VIO_INLINE T adds_u8(T a, T b) {
    const T s = a + b;
    return s | (T)(s < a);
  }
  static VIO_INLINE T subs_u8(T a, T b) { return (a - b) & (T)(a >= b); }
  static VIO_INLINE T cmpeq_8(T a, T b) { return (T)(a == b); }

  static VIO_INLINE T add_16(T a, T b) { return (T)((U16)a + (U16)b); }
  static VIO_INLINE T add_32(T a, T b) { return (T)((U32)a + (U32)b); }
  static VIO_INLINE T madd_16(T a, T b) {
    I32x8 wa = __builtin_convertvector((I16)a, I32x8), wb = __builtin_convertvector((I16)b, I32x8);
    U32x8 p = (U32x8)wa * (U32x8)wb;
    return (T)(__builtin_shufflevector(p, p, 0, 2, 4, 6) + __builtin_shufflevector(p, p, 1, 3, 5, 7));
  }

  static VIO_INLINE T and_(T a, T b) { return a & b; }
  static VIO_INLINE T or_(T a, T b) { return a | b; }
  static VIO_INLINE T xor_(T a, T b) { return a ^ b; }
  static VIO_INLINE T andnot(T a, T b) { return ~a & b; }

  static VIO_INLINE T unpacklo_8(T a, T b) {
    return __builtin_shufflevector(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
  }
  static VIO_INLINE T unpackhi_8(T a, T b) {
    return __builtin_shufflevector(a, b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
  }
  static VIO_INLINE T widenlo_u8(T a) { return kBigEndian ? unpacklo_8(zero(), a) : unpacklo_8(a, zero()); }
  static VIO_INLINE T widenhi_u8(T a) { return kBigEndian ? unpackhi_8(zero(), a) : unpackhi_8(a, zero()); }
};
}  // namespace rvv
#endif

// The back end of the generic kernels, if any: GV is its vector interface,
// kGenericIsa the flag that enables it.
#if VIO_SIMD_ALTIVEC
#define VIO_PNG_GENERIC 1
using GV = altivec::V;
inline constexpr unsigned kGenericIsa = kAltiVec;
#elif VIO_SIMD_MMI
#define VIO_PNG_GENERIC 1
using GV = mmi::V;
inline constexpr unsigned kGenericIsa = kMMI;
#elif VIO_SIMD_RVV
#define VIO_PNG_GENERIC 1
using GV = rvv::V;
inline constexpr unsigned kGenericIsa = kRVV;
#endif

}  // namespace vio::png::simd
