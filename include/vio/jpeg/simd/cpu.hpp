// vio::jpeg::simd - run-time instruction-set detection.
//
// support() returns the instruction sets that the compiled-in SIMD back ends
// may use on this CPU, adjusted by these environment variables:
//   JSIMD_FORCENONE=1   disable all SIMD kernels
//   JSIMD_FORCESSE2=1   x86: do not use AVX2
//   JSIMD_FORCENEON=1   Arm: use NEON
//   JSIMD_FORCERVV=1    RISC-V: use RVV
//   JSIMD_FORCEMMI=1    MIPS64: use Loongson MMI
//   JSIMD_NOHUFFENC=1   do not use the SIMD Huffman encoders
// The environment is consulted each time a codec object is created; CPU
// detection itself runs once.
#pragma once

#include <cstdlib>
#include <cstring>

#include "../config.hpp"

#if VIO_SIMD_X86
#if defined(_MSC_VER)
// clang-cl: <intrin.h> declares a function __cpuid that <cpuid.h> defines
// as a macro; included first, later inclusions do not trip on the macro.
#include <intrin.h>
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#include <immintrin.h>
#else
#include <cpuid.h>
#endif
#endif

#if VIO_SIMD_NEON_RUNTIME
#include <sys/auxv.h>
#endif

namespace vio::jpeg::simd {

// Instruction-set flags.
enum : unsigned {
  kNone = 0x000,
  kMMX = 0x001,
  k3DNow = 0x002,
  kSSE = 0x004,
  kSSE2 = 0x008,
  kNEON = 0x010,
  kAltiVec = 0x040,
  kAVX2 = 0x080,
  kMMI = 0x100,
  kRVV = 0x200,
  kBMI2 = 0x400,  // x86 BMI1, BMI2 and LZCNT (scalar bit manipulation)
};

struct Support {
  unsigned flags = kNone;  // usable instruction sets
  bool huffman = true;     // SIMD Huffman encoding allowed
  bool has(unsigned f) const { return (flags & f) != 0; }
};

namespace detail_cpu {

#define VIO_SIMD_CPU_PNG 0
#include "../../detail/simd_cpu.inc"
#undef VIO_SIMD_CPU_PNG

}  // namespace detail_cpu

// CPU capabilities usable by the compiled-in back ends (detected once).
inline unsigned cpu_flags() {
  static const unsigned flags = detail_cpu::detect();
  return flags;
}

// cpu_flags() adjusted by the JSIMD_* environment variables.
inline Support support() {
  using detail_cpu::env_is_1;
  Support s;
  s.flags = cpu_flags();
#if VIO_SIMD_X86
  if (env_is_1("JSIMD_FORCESSE2")) s.flags &= kSSE2 | kSSE;
#if !VIO_SIMD_X86_64
  if (env_is_1("JSIMD_FORCESSE")) s.flags &= kSSE | kMMX;
  if (env_is_1("JSIMD_FORCEMMX")) s.flags &= kMMX;
  if (env_is_1("JSIMD_FORCE3DNOW")) s.flags &= k3DNow | kMMX;
#endif
#endif
#if VIO_SIMD_NEON
  if (env_is_1("JSIMD_FORCENEON")) s.flags = kNEON;
#endif
#if VIO_SIMD_RVV
  if (env_is_1("JSIMD_FORCERVV")) s.flags = kRVV;
#endif
#if VIO_SIMD_MMI
  if (env_is_1("JSIMD_FORCEMMI")) s.flags = kMMI;
#endif
  if (env_is_1("JSIMD_FORCENONE")) s.flags = kNone;
  if (env_is_1("JSIMD_NOHUFFENC")) s.huffman = false;
  return s;
}

}  // namespace vio::jpeg::simd
