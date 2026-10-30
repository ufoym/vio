// vio::jpeg::simd::mmi - kernel selection.
#pragma once

#include "mmi.hpp"
#include "../cpu.hpp"

namespace vio::jpeg::simd::mmi {

#define VIO_GENERIC_SUPPORT kMMI
#include "../generic/kernels.inc"
#undef VIO_GENERIC_SUPPORT

}  // namespace vio::jpeg::simd::mmi
