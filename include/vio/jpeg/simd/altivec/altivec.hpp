// vio::jpeg::simd::altivec - PowerPC AltiVec/VSX kernels.  The lane-generic
// and integer DCT kernels (simd/generic) are instantiated with an AltiVec
// implementation of the vector interface (color conversion, sampling, DCTs,
// quantization).
#pragma once

#include <altivec.h>
// altivec.h defines these as macros in C++, which breaks bool/std::vector.
#undef vector
#undef pixel
#undef bool

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "../../config.hpp"
#include "../../types.hpp"
#include "../common.hpp"

namespace vio::jpeg::simd::altivec {

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

}  // namespace vio::jpeg::simd::altivec
