// vio::jpeg::simd::sse2 - SSE2 kernels.  Compiled with an SSE2 target so
// that 32-bit x86 builds without -msse2 can still select them at run time.
#pragma once

#include <emmintrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../config.hpp"
#include "../../types.hpp"
#include "../common.hpp"

VIO_TARGET_PUSH_SSE2()
namespace vio::jpeg::simd::sse2 {

using ColorConvertFn = void (*)(unsigned, std::uint8_t**, std::uint8_t***, unsigned, int);
using ColorDeconvertFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**, int);
using MergedUpsampleFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**);
using UpsampleFn = void (*)(int, unsigned, std::uint8_t**, std::uint8_t**);

#include "v128.inc"
#include "../generic/ccolor.inc"
#include "../generic/csample.inc"
#include "../generic/dcolor.inc"
#include "../generic/dsample.inc"
#include "../generic/dct.inc"
#include "sse2_huff.inc"
#include "sse2_float.inc"

}  // namespace vio::jpeg::simd::sse2
VIO_TARGET_POP()
