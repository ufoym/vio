// vio::jpeg::simd::altivec - kernel selection.
#pragma once

#include "altivec.hpp"
#include "../cpu.hpp"

namespace vio::jpeg::simd::altivec {

inline void fdct_kernels(const Support& s, FdctKernels& k) {
  if (!s.has(kAltiVec)) return;
  k.convsamp = convsamp;
  k.fdct_islow = fdct_islow;
  k.fdct_ifast = fdct_ifast;
  k.quantize = quantize;
  k.forward_islow = forward_islow;
  k.forward_ifast = forward_ifast;
}

inline void idct_kernels(const Support& s, IdctKernels& k) {
  if (!s.has(kAltiVec)) return;
  k.islow = idct16_adapter<idct_islow>;
  k.ifast = idct16_adapter<idct_ifast>;
  k.s4 = idct16_adapter<idct_4x4>;
  k.s2 = idct16_adapter<idct_2x2>;
}

}  // namespace vio::jpeg::simd::altivec
