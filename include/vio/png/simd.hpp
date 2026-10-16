// vio::png - instruction sets used by the PNG kernels.
//
// The kernels are chosen at run time from the CPU's capabilities, restricted
// by the JSIMD_* environment variables (see vio/jpeg/simd/cpu.hpp), which the
// PNG code reads once per process.  Besides the hand-written x86 and NEON
// kernels, the PowerPC AltiVec, Loongson MMI and RISC-V Vector back ends
// instantiate generic kernels with the 128-bit vector interface `V` that the
// JPEG code uses (vio/jpeg/simd/*/v.inc).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "../jpeg/config.hpp"
#include "../jpeg/simd/cpu.hpp"

#if VIO_SIMD_ALTIVEC
#include <altivec.h>
// altivec.h defines these as macros in C++, which breaks bool/std::vector.
#undef vector
#undef pixel
#undef bool
#endif
#if VIO_SIMD_MMI
#include <loongson-mmiintrin.h>
#endif

namespace vio::png::simd {

// The usable instruction sets (jpeg::simd flags).
inline const jpeg::simd::Support& support() {
  static const jpeg::simd::Support s = jpeg::simd::support();
  return s;
}
inline bool has(unsigned isa) { return support().has(isa); }

#if VIO_SIMD_ALTIVEC
namespace altivec {
#include "../jpeg/simd/altivec/v.inc"
}  // namespace altivec
#endif
#if VIO_SIMD_MMI
namespace mmi {
#include "../jpeg/simd/mmi/v.inc"
}  // namespace mmi
#endif
#if VIO_SIMD_RVV
namespace rvv {
#include "../jpeg/simd/vecext/v.inc"
}  // namespace rvv
#endif

// The back end of the generic kernels, if any: GV is its vector interface,
// kGenericIsa the flag that enables it.
#if VIO_SIMD_ALTIVEC
#define VIO_PNG_GENERIC 1
using GV = altivec::V;
inline constexpr unsigned kGenericIsa = jpeg::simd::kAltiVec;
#elif VIO_SIMD_MMI
#define VIO_PNG_GENERIC 1
using GV = mmi::V;
inline constexpr unsigned kGenericIsa = jpeg::simd::kMMI;
#elif VIO_SIMD_RVV
#define VIO_PNG_GENERIC 1
using GV = rvv::V;
inline constexpr unsigned kGenericIsa = jpeg::simd::kRVV;
#endif

}  // namespace vio::png::simd
