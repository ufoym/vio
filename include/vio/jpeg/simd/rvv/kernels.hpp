// vio::jpeg::simd::rvv - kernel selection.
#pragma once

#include "rvv.hpp"
#include "../cpu.hpp"

namespace vio::jpeg::simd::rvv {

#define VIO_GENERIC_SUPPORT kRVV
#include "../generic/kernels.inc"
#undef VIO_GENERIC_SUPPORT

}  // namespace vio::jpeg::simd::rvv
