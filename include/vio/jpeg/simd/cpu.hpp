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
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
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
};

struct Support {
  unsigned flags = kNone;  // usable instruction sets
  bool huffman = true;     // SIMD Huffman encoding allowed
  bool has(unsigned f) const { return (flags & f) != 0; }
};

namespace detail_cpu {

#if VIO_SIMD_X86
inline void cpuid(unsigned leaf, unsigned subleaf, unsigned r[4]) {
#if defined(_MSC_VER) && !defined(__clang__)
  int v[4];
  __cpuidex(v, static_cast<int>(leaf), static_cast<int>(subleaf));
  for (int i = 0; i < 4; i++) r[i] = static_cast<unsigned>(v[i]);
#else
  __cpuid_count(leaf, subleaf, r[0], r[1], r[2], r[3]);
#endif
}

inline unsigned long long xgetbv0() {
#if defined(_MSC_VER) && !defined(__clang__)
  return _xgetbv(0);
#else
  unsigned eax, edx;
  __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  return (static_cast<unsigned long long>(edx) << 32) | eax;
#endif
}

inline unsigned detect_x86() {
  unsigned flags = 0;
  unsigned r[4];
  cpuid(0, 0, r);
  const unsigned max_leaf = r[0];
#if VIO_SIMD_X86_64
  flags |= kSSE | kSSE2;  // baseline on x86-64
#else
  if (max_leaf >= 1) {
    cpuid(1, 0, r);
    if (r[3] & (1u << 23)) flags |= kMMX;
    if (r[3] & (1u << 25)) flags |= kSSE;
    if (r[3] & (1u << 26)) flags |= kSSE2;
  }
#endif
#if VIO_SIMD_AVX2
  // AVX2 needs CPU support (leaf 7 EBX bit 5, leaf 1 ECX bits 27-28) and
  // operating-system management of the YMM state (XCR0 bits 1-2).
  if (max_leaf >= 7) {
    cpuid(7, 0, r);
    if (r[1] & (1u << 5)) {
      cpuid(1, 0, r);
      if ((r[2] & (1u << 27)) && (r[2] & (1u << 28)) && (xgetbv0() & 6) == 6) flags |= kAVX2;
    }
  }
#endif
  return flags;
}
#endif

// What the compiled-in back ends can use on this CPU.
inline unsigned detect() {
  unsigned flags = 0;
#if VIO_SIMD_X86
  flags |= detect_x86();
#endif
#if VIO_SIMD_NEON_RUNTIME
  if (getauxval(AT_HWCAP) & (1ul << 12)) flags |= kNEON;  // HWCAP_NEON
#elif VIO_SIMD_NEON
  flags |= kNEON;  // AArch64, or AArch32 code built for NEON
#endif
#if VIO_SIMD_ALTIVEC
  flags |= kAltiVec;
#endif
#if VIO_SIMD_RVV
  flags |= kRVV;
#endif
#if VIO_SIMD_MMI
  flags |= kMMI;
#endif
  return flags;
}

// True if the environment variable `name` is set to exactly "1".
inline bool env_is_1(const char* name) {
#if defined(_MSC_VER) && !defined(__clang__)
  char buf[2] = {};
  std::size_t len = 0;
  return getenv_s(&len, buf, sizeof(buf), name) == 0 && len == 2 && buf[0] == '1';
#else
  const char* v = std::getenv(name);
  return v && std::strcmp(v, "1") == 0;
#endif
}

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
