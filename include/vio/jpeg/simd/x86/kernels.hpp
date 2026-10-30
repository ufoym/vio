// vio::jpeg::simd::x86 - SSE2 / AVX2 kernel selection.
#pragma once

#include "../common.hpp"
#include "../cpu.hpp"
#include "../../types.hpp"
#include "sse2.hpp"
#if VIO_SIMD_AVX2
#include "avx2.hpp"
#include "sse2_bmi2.hpp"
#endif
#if !VIO_SIMD_X86_64
#include "mmx.hpp"
#include "sse.hpp"
#endif

namespace vio::jpeg::simd::x86 {

using ColorConvertFn = void (*)(unsigned, std::uint8_t**, std::uint8_t***, unsigned, int);
using ColorDeconvertFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**, int);
using DownsampleFn = void (*)(unsigned, int, unsigned, unsigned, std::uint8_t**, std::uint8_t**);
using UpsampleFn = void (*)(int, unsigned, std::uint8_t**, std::uint8_t**);
using MergedUpsampleFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**);

// Highest-priority instruction set first.
#define VIO_X86_TIER(s, flag, nsname, expr)  \
  if ((s).has(flag)) {                       \
    namespace ns = vio::jpeg::simd::nsname;  \
    if (auto f_ = (expr)) return f_;         \
  }
#if VIO_SIMD_AVX2
#define VIO_X86_TIER_AVX2(s, expr) VIO_X86_TIER(s, kAVX2, avx2, expr)
#else
#define VIO_X86_TIER_AVX2(s, expr)
#endif
#if !VIO_SIMD_X86_64
#define VIO_X86_TIER_MMX(s, expr) VIO_X86_TIER(s, kMMX, mmx, expr)
#else
#define VIO_X86_TIER_MMX(s, expr)
#endif
#define VIO_X86_PICK(s, expr)                     \
  do {                                            \
    VIO_X86_TIER_AVX2(s, expr)                    \
    VIO_X86_TIER(s, kSSE2, sse2, expr)            \
    VIO_X86_TIER_MMX(s, expr)                     \
  } while (0)

inline ColorConvertFn rgb_ycc_kernel(const Support& s, ColorSpace in) {
  VIO_X86_PICK(s, ns::rgb_ycc_kernel(in));
  return nullptr;
}
inline ColorConvertFn rgb_gray_kernel(const Support& s, ColorSpace in) {
  VIO_X86_PICK(s, ns::rgb_gray_kernel(in));
  return nullptr;
}
inline ColorDeconvertFn ycc_rgb_kernel(const Support& s, ColorSpace out) {
  VIO_X86_PICK(s, ns::ycc_rgb_kernel(out));
  return nullptr;
}
inline DownsampleFn h2v1_downsample_kernel(const Support& s) {
  VIO_X86_PICK(s, ns::h2v1_downsample);
  return nullptr;
}
inline DownsampleFn h2v2_downsample_kernel(const Support& s) {
  VIO_X86_PICK(s, ns::h2v2_downsample);
  return nullptr;
}
inline UpsampleFn upsample_kernel(const Support& s, int kind) {
  VIO_X86_PICK(s, ns::upsample_kernel(kind));
  return nullptr;
}
inline MergedUpsampleFn merged_upsample_kernel(const Support& s, bool v2, ColorSpace out) {
  VIO_X86_PICK(s, v2 ? ns::merged_upsample_kernel<true>(out) : ns::merged_upsample_kernel<false>(out));
  return nullptr;
}
inline void fdct_kernels(const Support& s, FdctKernels& k) {
#if !VIO_SIMD_X86_64
  if (s.has(kMMX) && !s.has(kSSE2)) {
    namespace n = vio::jpeg::simd::mmx;
    k.convsamp = n::convsamp;
    k.fdct_islow = n::fdct_islow;
    k.fdct_ifast = n::fdct_ifast;
    k.quantize = n::quantize;
  }
  if (s.has(kSSE) && !s.has(kSSE2)) {
    namespace f = vio::jpeg::simd::sse;
    k.convsamp_float = f::convsamp_float;
    k.fdct_float = f::fdct_float;
    k.quantize_float = f::quantize_float;
  }
#endif
  if (s.has(kSSE2)) {
    namespace n = vio::jpeg::simd::sse2;
    k.convsamp = n::convsamp;
    k.fdct_islow = n::fdct_islow;
    k.fdct_ifast = n::fdct_ifast;
    k.quantize = n::quantize;
    k.forward_islow = n::forward_islow;
    k.forward_ifast = n::forward_ifast;
    k.convsamp_float = n::convsamp_float;
    k.fdct_float = n::fdct_float;
    k.quantize_float = n::quantize_float;
  }
#if VIO_SIMD_AVX2
  if (s.has(kAVX2)) {
    namespace a = vio::jpeg::simd::avx2;
    k.convsamp = a::convsamp;
    k.fdct_islow = a::fdct_islow;
    k.quantize = a::quantize;
    k.forward_islow = a::forward_islow;
  }
#endif
}
inline void phuff_kernels(const Support& s, PhuffKernels& k) {
  if (s.has(kSSE2)) {
    k.ac_first_prepare = vio::jpeg::simd::sse2::ac_first_prepare;
    k.ac_refine_prepare = vio::jpeg::simd::sse2::ac_refine_prepare;
  }
}
inline void idct_kernels(const Support& s, IdctKernels& k) {
#if !VIO_SIMD_X86_64
  if (s.has(kMMX) && !s.has(kSSE2)) {
    namespace n = vio::jpeg::simd::mmx;
    k.islow = idct16_adapter<n::idct_islow>;
    k.ifast = idct16_adapter<n::idct_ifast>;
    k.s4 = idct16_adapter<n::idct_4x4>;
    k.s2 = idct16_adapter<n::idct_2x2>;
  }
  if (s.has(kSSE) && !s.has(kSSE2)) k.flt = idct_float_adapter<vio::jpeg::simd::sse::idct_float>;
#endif
  if (s.has(kSSE2)) {
    namespace n = vio::jpeg::simd::sse2;
    k.islow = idct16_adapter<n::idct_islow>;
    k.ifast = idct16_adapter<n::idct_ifast>;
    k.s4 = idct16_adapter<n::idct_4x4>;
    k.s2 = idct16_adapter<n::idct_2x2>;
    k.flt = idct_float_adapter<n::idct_float>;
  }
#if VIO_SIMD_AVX2
  if (s.has(kAVX2)) k.islow = idct16_adapter<vio::jpeg::simd::avx2::idct_islow>;
#endif
}
template <class Table>
inline HuffEncodeFn<Table> huff_encode_kernel(const Support& s) {
  if (!s.has(kSSE2)) return nullptr;
#if VIO_SIMD_AVX2
  if (s.has(kBMI2)) return vio::jpeg::simd::sse2_bmi2::encode_blocks<Table>;
#endif
  return vio::jpeg::simd::sse2::encode_blocks<Table>;
}

}  // namespace vio::jpeg::simd::x86
