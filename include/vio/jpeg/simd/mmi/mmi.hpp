// vio::jpeg::simd::mmi - Loongson MMI kernels (MIPS64, little-endian).  The
// lane-generic and integer DCT kernels (simd/generic) are instantiated with
// an MMI implementation of the vector interface (color conversion, sampling,
// DCTs, quantization).
#pragma once

#include <loongson-mmiintrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../config.hpp"
#include "../../types.hpp"
#include "../common.hpp"

namespace vio::jpeg::simd::mmi {

using ColorConvertFn = void (*)(unsigned, std::uint8_t**, std::uint8_t***, unsigned, int);
using ColorDeconvertFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**, int);
using MergedUpsampleFn = void (*)(unsigned, std::uint8_t***, unsigned, std::uint8_t**);
using UpsampleFn = void (*)(int, unsigned, std::uint8_t**, std::uint8_t**);

#include "v.inc"
#include "../generic/ccolor.inc"
#include "../generic/csample.inc"
#include "../generic/dcolor.inc"
#include "../generic/dsample.inc"
#include "../generic/dct.inc"

}  // namespace vio::jpeg::simd::mmi
