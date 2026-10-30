// vio::jpeg::simd::altivec - kernel selection.
#pragma once

#include "altivec.hpp"
#include "../cpu.hpp"

namespace vio::jpeg::simd::altivec {

#define VIO_GENERIC_SUPPORT kAltiVec
#include "../generic/kernels.inc"
#undef VIO_GENERIC_SUPPORT

}  // namespace vio::jpeg::simd::altivec
