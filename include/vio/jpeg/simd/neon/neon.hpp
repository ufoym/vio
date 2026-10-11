// vio::jpeg::simd::neon - Arm NEON kernels (AArch64 and AArch32).  On 32-bit
// Arm builds without NEON enabled, the kernels are compiled with a NEON
// target and selected only when run-time detection finds NEON.
#pragma once

#include "../../config.hpp"

#if VIO_SIMD_NEON_RUNTIME
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
VIO_TARGET_PUSH_("fpu=neon")
#endif
#include "ccolor.hpp"
#include "fdct.hpp"
#include "huff.hpp"
#include "phuff.hpp"
#include "csample.hpp"
#include "idct.hpp"
#include "idct_fast.hpp"
#include "dcolor.hpp"
#include "dsample.hpp"
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_POP()
#endif
