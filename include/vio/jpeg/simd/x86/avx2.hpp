// vio::jpeg::simd::avx2 - AVX2 kernels.  Compiled with an AVX2 target and
// selected at run time.  Streaming kernels share the lane-generic sources
// with SSE2 and process two independent 16-pixel groups per vector.
#pragma once

#include <immintrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../config.hpp"
#include "../../types.hpp"
#include "../common.hpp"

VIO_TARGET_PUSH_AVX2()
namespace vio::jpeg::simd::avx2 {

using ColorConvertFn = void (*)(unsigned, std::uint8_t**, std::uint8_t***, unsigned, int);
using ColorDeconvertFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**, int);
using MergedUpsampleFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**);
using UpsampleFn = void (*)(int, unsigned, std::uint8_t**, std::uint8_t**);

#include "v256.inc"
#include "../generic/ccolor.inc"
#include "../generic/csample.inc"
#include "../generic/dcolor.inc"
#include "../generic/dsample.inc"
#include "avx2_dct.inc"

}  // namespace vio::jpeg::simd::avx2
VIO_TARGET_POP()
