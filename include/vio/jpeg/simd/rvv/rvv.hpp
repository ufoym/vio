// vio::jpeg::simd::rvv - RISC-V Vector (RVV 1.0) kernels.  The lane-generic
// and integer DCT kernels (simd/generic) are instantiated with fixed-length
// 128-bit vectors written with compiler vector extensions (simd/vecext),
// which GCC/Clang lower to RVV instructions (color conversion, sampling,
// DCTs, quantization).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "../../config.hpp"
#include "../../types.hpp"
#include "../common.hpp"

namespace vio::jpeg::simd::rvv {

using ColorConvertFn = void (*)(unsigned, std::uint8_t**, std::uint8_t***, unsigned, int);
using ColorDeconvertFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**, int);
using MergedUpsampleFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**);
using UpsampleFn = void (*)(int, unsigned, std::uint8_t**, std::uint8_t**);

#include "../vecext/v.inc"
#include "../generic/ccolor.inc"
#include "../generic/csample.inc"
#include "../generic/dcolor.inc"
#include "../generic/dsample.inc"
#include "../generic/dct.inc"

}  // namespace vio::jpeg::simd::rvv
