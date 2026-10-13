// vio::jpeg::detail::simd - SIMD kernel selection.
//
// Each query returns the best accelerated kernel that the codec's SIMD
// support mask (see cpu.hpp) allows, or nullptr when none applies, in which
// case the scalar code is used.  Each function is selected independently
// (e.g. an AVX2 quantizer with an SSE2 fast DCT).
// Accelerated kernels produce results bit-identical to the scalar code,
// except for the x86 floating-point DCT kernels, whose results follow the
// SSE instruction sequence.
#pragma once

#include "../types.hpp"
#include "../detail/component.hpp"
#include "common.hpp"
#include "cpu.hpp"

#if VIO_SIMD_NEON
#include "neon/neon.hpp"
#endif
#if VIO_SIMD_X86
#include "x86/kernels.hpp"
#endif
#if VIO_SIMD_ALTIVEC
#include "altivec/kernels.hpp"
#endif
#if VIO_SIMD_MMI
#include "mmi/kernels.hpp"
#endif
#if VIO_SIMD_RVV
#include "rvv/kernels.hpp"
#endif

namespace vio::jpeg::detail::simd {

using Support = vio::jpeg::simd::Support;
using vio::jpeg::simd::FdctKernels;
using vio::jpeg::simd::HuffBitState;
using vio::jpeg::simd::HuffEncodeFn;
using vio::jpeg::simd::IdctFn8;
using vio::jpeg::simd::IdctKernels;
using vio::jpeg::simd::PhuffKernels;
using vio::jpeg::simd::idct16_adapter;
using vio::jpeg::simd::idct_float_adapter;
namespace isa = vio::jpeg::simd;

// Back ends built from the generic kernels (simd/generic) with one flag.
#if VIO_SIMD_ALTIVEC
#define VIO_GENERIC_ISA(s) ((s).has(isa::kAltiVec))
namespace gen = vio::jpeg::simd::altivec;
#elif VIO_SIMD_MMI
#define VIO_GENERIC_ISA(s) ((s).has(isa::kMMI))
namespace gen = vio::jpeg::simd::mmi;
#elif VIO_SIMD_RVV
#define VIO_GENERIC_ISA(s) ((s).has(isa::kRVV))
namespace gen = vio::jpeg::simd::rvv;
#endif

// ---------------------------------------------------------------------------
// Color conversion (compression)
using ColorConvertFn = void (*)(unsigned width, std::uint8_t** in, std::uint8_t*** out,
                                unsigned row, int num_rows);

inline ColorConvertFn rgb_ycc_kernel([[maybe_unused]] const Support& s,
                                     [[maybe_unused]] ColorSpace in) {
#if VIO_SIMD_X86
  if (auto f = isa::x86::rgb_ycc_kernel(s, in)) return f;
#endif
#if VIO_SIMD_NEON
  if (s.has(isa::kNEON)) return vio::jpeg::simd::neon::rgb_ycc_kernel(in);
#endif
#ifdef VIO_GENERIC_ISA
  if (VIO_GENERIC_ISA(s)) return gen::rgb_ycc_kernel(in);
#endif
  return nullptr;
}

inline ColorConvertFn rgb_gray_kernel([[maybe_unused]] const Support& s,
                                      [[maybe_unused]] ColorSpace in) {
#if VIO_SIMD_X86
  if (auto f = isa::x86::rgb_gray_kernel(s, in)) return f;
#endif
#if VIO_SIMD_NEON
  if (s.has(isa::kNEON)) return vio::jpeg::simd::neon::rgb_gray_kernel(in);
#endif
#ifdef VIO_GENERIC_ISA
  if (VIO_GENERIC_ISA(s)) return gen::rgb_gray_kernel(in);
#endif
  return nullptr;
}

// ---------------------------------------------------------------------------
// Downsampling
using DownsampleFn = void (*)(unsigned image_width, int max_v_samp_factor, unsigned v_samp_factor,
                              unsigned width_in_blocks, std::uint8_t** in, std::uint8_t** out);

inline DownsampleFn h2v1_downsample_kernel([[maybe_unused]] const Support& s) {
#if VIO_SIMD_X86
  if (auto f = isa::x86::h2v1_downsample_kernel(s)) return f;
#endif
#if VIO_SIMD_NEON
  if (s.has(isa::kNEON)) return vio::jpeg::simd::neon::h2v1_downsample;
#endif
#ifdef VIO_GENERIC_ISA
  if (VIO_GENERIC_ISA(s)) return &gen::h2v1_downsample;
#endif
  return nullptr;
}

inline DownsampleFn h2v2_downsample_kernel([[maybe_unused]] const Support& s) {
#if VIO_SIMD_X86
  if (auto f = isa::x86::h2v2_downsample_kernel(s)) return f;
#endif
#if VIO_SIMD_NEON
  if (s.has(isa::kNEON)) return vio::jpeg::simd::neon::h2v2_downsample;
#endif
#ifdef VIO_GENERIC_ISA
  if (VIO_GENERIC_ISA(s)) return &gen::h2v2_downsample;
#endif
  return nullptr;
}

// ---------------------------------------------------------------------------
// Color conversion (decompression)
using ColorDeconvertFn = void (*)(unsigned width, std::uint8_t*** in, unsigned row,
                                  std::uint8_t** out, int num_rows);

inline ColorDeconvertFn ycc_rgb_kernel([[maybe_unused]] const Support& s,
                                       [[maybe_unused]] ColorSpace out) {
  if (out == ColorSpace::RGB565) {  // NEON only
#if VIO_SIMD_NEON
    if (s.has(isa::kNEON)) return vio::jpeg::simd::neon::ycc_rgb_kernel(out);
#endif
    return nullptr;
  }
#if VIO_SIMD_X86
  if (auto f = isa::x86::ycc_rgb_kernel(s, out)) return f;
#endif
#if VIO_SIMD_NEON
  if (s.has(isa::kNEON)) return vio::jpeg::simd::neon::ycc_rgb_kernel(out);
#endif
#ifdef VIO_GENERIC_ISA
  if (VIO_GENERIC_ISA(s)) return gen::ycc_rgb_kernel(out);
#endif
  return nullptr;
}

// ---------------------------------------------------------------------------
// Upsampling
using MergedUpsampleFn = void (*)(unsigned output_width, std::uint8_t*** in,
                                  unsigned in_row_group_ctr, std::uint8_t** out);

inline MergedUpsampleFn merged_upsample_kernel([[maybe_unused]] const Support& s,
                                               [[maybe_unused]] bool v2,
                                               [[maybe_unused]] ColorSpace out) {
#if VIO_SIMD_X86
  if (auto f = isa::x86::merged_upsample_kernel(s, v2, out)) return f;
#endif
#if VIO_SIMD_NEON
  namespace n = vio::jpeg::simd::neon;
  if (s.has(isa::kNEON))
    return v2 ? n::merged_upsample_kernel<true>(out) : n::merged_upsample_kernel<false>(out);
#endif
#ifdef VIO_GENERIC_ISA
  if (VIO_GENERIC_ISA(s)) return (v2 ? gen::merged_upsample_kernel<true>(out) : gen::merged_upsample_kernel<false>(out));
#endif
  return nullptr;
}

enum class Upsample { H2V1, H2V2, H2V1Fancy, H2V2Fancy, H1V2Fancy };
using UpsampleFn = void (*)(int max_v_samp_factor, unsigned width, std::uint8_t** in,
                            std::uint8_t** out);

inline UpsampleFn upsample_kernel([[maybe_unused]] const Support& s,
                                  [[maybe_unused]] Upsample kind) {
#if VIO_SIMD_X86
  if (auto f = isa::x86::upsample_kernel(s, static_cast<int>(kind))) return f;
#endif
#if VIO_SIMD_NEON
  namespace n = vio::jpeg::simd::neon;
  if (s.has(isa::kNEON)) {
    switch (kind) {
      case Upsample::H2V1: return n::h2v1_upsample;
      case Upsample::H2V2: return n::h2v2_upsample;
      case Upsample::H2V1Fancy: return n::h2v1_fancy_upsample;
      case Upsample::H2V2Fancy: return n::h2v2_fancy_upsample;
      case Upsample::H1V2Fancy: return n::h1v2_fancy_upsample;
    }
  }
#endif
#ifdef VIO_GENERIC_ISA
  if (VIO_GENERIC_ISA(s)) return gen::upsample_kernel(static_cast<int>(kind));
#endif
  return nullptr;
}

// ---------------------------------------------------------------------------
// Forward DCT and quantization (8-bit samples, 16-bit DCT elements)
inline FdctKernels fdct_kernels([[maybe_unused]] const Support& s) {
  FdctKernels k;
#if VIO_SIMD_X86
  isa::x86::fdct_kernels(s, k);
#endif
#if VIO_SIMD_NEON
  namespace n = vio::jpeg::simd::neon;
  if (s.has(isa::kNEON)) {
    k.convsamp = n::convsamp;
    k.fdct_islow = n::fdct_islow;
    k.fdct_ifast = n::fdct_ifast;
    k.quantize = n::quantize;
    k.forward_islow = n::forward_islow;
    k.forward_ifast = n::forward_ifast;
  }
#endif
#ifdef VIO_GENERIC_ISA
  gen::fdct_kernels(s, k);
#endif
  return k;
}

// ---------------------------------------------------------------------------
// Huffman encoding
template <class Table>
inline HuffEncodeFn<Table> huff_encode_kernel([[maybe_unused]] const Support& s) {
  if (!s.huffman) return nullptr;
#if VIO_SIMD_X86
  if (s.has(isa::kSSE2)) return isa::x86::huff_encode_kernel<Table>(s);
#endif
#if VIO_SIMD_NEON
  if (s.has(isa::kNEON)) return vio::jpeg::simd::neon::encode_blocks<Table>;
#endif
  return nullptr;
}

inline PhuffKernels phuff_kernels([[maybe_unused]] const Support& s) {
  PhuffKernels k;
  if (!s.huffman) return k;
#if VIO_SIMD_X86
  isa::x86::phuff_kernels(s, k);
#endif
#if VIO_SIMD_NEON
  if (s.has(isa::kNEON)) {
    k.ac_first_prepare = vio::jpeg::simd::neon::ac_first_prepare;
    k.ac_refine_prepare = vio::jpeg::simd::neon::ac_refine_prepare;
  }
#endif
  return k;
}

// ---------------------------------------------------------------------------
// Inverse DCT.  Kernels take the IdctFn<uint8_t> signature; dct_table points
// at ComponentInfo::dct_table (integer kernels use the adjacent 16-bit copy).
inline IdctKernels idct_kernels([[maybe_unused]] const Support& s) {
  IdctKernels k;
#if VIO_SIMD_X86
  isa::x86::idct_kernels(s, k);
#endif
#if VIO_SIMD_NEON
  namespace n = vio::jpeg::simd::neon;
  if (s.has(isa::kNEON)) {
    k.islow = idct16_adapter<n::idct_islow>;
    k.ifast = idct16_adapter<n::idct_ifast>;
    k.s4 = idct16_adapter<n::idct_4x4>;
    k.s2 = idct16_adapter<n::idct_2x2>;
  }
#endif
#ifdef VIO_GENERIC_ISA
  gen::idct_kernels(s, k);
#endif
  return k;
}

}  // namespace vio::jpeg::detail::simd
