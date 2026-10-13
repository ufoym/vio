// vio::jpeg - compiler / platform configuration and SIMD back-end selection.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER) && !defined(__clang__)
#define VIO_INLINE __forceinline
#define VIO_NOINLINE __declspec(noinline)
// No VIO_LAMBDA_INLINE: before C++20, MSVC can force-inline lambdas only with
// /Zc:lambda.  Code that MSVC compiles uses VIO_INLINE functions instead.
#define VIO_RESTRICT __restrict
#define VIO_LIKELY(x) (x)
#define VIO_UNLIKELY(x) (x)
#else
#define VIO_INLINE inline __attribute__((always_inline))
#define VIO_NOINLINE __attribute__((noinline))
// Only for code that MSVC never compiles (the NEON back end).
#define VIO_LAMBDA_INLINE __attribute__((always_inline))
#define VIO_RESTRICT __restrict__
#define VIO_LIKELY(x) __builtin_expect(!!(x), 1)
#define VIO_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

// Hot per-block functions whose small local arrays are only indexed within
// bounds skip the stack-protector instrumentation.
#if defined(_MSC_VER) && !defined(__clang__)
#define VIO_NO_STACK_CHECK __declspec(safebuffers)
#elif defined(__has_attribute)
#if __has_attribute(no_stack_protector)
#define VIO_NO_STACK_CHECK __attribute__((no_stack_protector))
#endif
#endif
#ifndef VIO_NO_STACK_CHECK
#define VIO_NO_STACK_CHECK
#endif

// Floating-point kernels whose results must be reproducible use explicit
// std::fma() where the expected results assume a fused multiply-add;
// implicit contraction is disabled for them.
#if defined(__clang__)
#define VIO_NO_FP_CONTRACT
#define VIO_FP_CONTRACT_OFF _Pragma("clang fp contract(off)")
#elif defined(__GNUC__)
#define VIO_NO_FP_CONTRACT __attribute__((optimize("fp-contract=off")))
#define VIO_FP_CONTRACT_OFF
#else
#define VIO_NO_FP_CONTRACT
#define VIO_FP_CONTRACT_OFF
#endif

// SIMD back ends.  Each one is compiled in automatically when the target
// architecture has it; the instruction sets actually used are chosen at run
// time from the CPU's capabilities (see vio/jpeg/simd/cpu.hpp).  Define
// VIO_NO_SIMD to build the portable scalar code only, or VIO_NO_AVX2 to omit
// the x86 AVX2 kernels.
//   VIO_SIMD_X86      SSE2 and AVX2 (x86-64 and 32-bit x86)
//   VIO_SIMD_NEON     Arm NEON (AArch64, and AArch32 built with NEON enabled)
//   VIO_SIMD_ALTIVEC  PowerPC AltiVec (built with AltiVec enabled)
//   VIO_SIMD_RVV      RISC-V Vector extension 1.0 (built with V enabled)
//   VIO_SIMD_MMI      Loongson MMI (MIPS64, built with MMI enabled)
#if !defined(VIO_NO_SIMD)
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#define VIO_SIMD_X86 1
#if defined(__x86_64__) || defined(_M_X64)
#define VIO_SIMD_X86_64 1
#endif
#if !defined(VIO_NO_AVX2)
#define VIO_SIMD_AVX2 1
#endif
#endif
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON) || defined(__ARM_NEON__)
#define VIO_SIMD_NEON 1
#elif defined(__arm__) && defined(__GNUC__) && !defined(__clang__) && !defined(__SOFTFP__) && \
    defined(__ARM_ARCH) && __ARM_ARCH >= 7 && !defined(VIO_NO_NEON_RUNTIME)
// 32-bit Arm built without NEON (e.g. -mfpu=vfpv3-d16): compile the NEON
// kernels with a NEON target and enable them when the CPU has NEON.
#define VIO_SIMD_NEON 1
#define VIO_SIMD_NEON_RUNTIME 1
#endif
// VIO_NEON_FORCE_A32 (testing only) uses the AArch32 code paths on AArch64.
#if (defined(__aarch64__) || defined(_M_ARM64)) && !defined(VIO_NEON_FORCE_A32)
#define VIO_SIMD_NEON_A64 1
#endif
#if defined(__ALTIVEC__)
#define VIO_SIMD_ALTIVEC 1
#endif
// (RVV kernels use fixed-length vector extensions, which GCC 14+ and Clang
// 17+ lower to RVV instructions; older compilers would emit scalar code.)
#if defined(__riscv_v) && __riscv_v >= 1000000 && !defined(VIO_NO_RVV) &&      \
    ((defined(__clang__) && __clang_major__ >= 17) ||                          \
     (!defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 14) ||           \
     defined(VIO_FORCE_RVV))
#define VIO_SIMD_RVV 1
#endif
#if defined(__mips_loongson_mmi) && defined(__MIPSEL__)
#define VIO_SIMD_MMI 1
#endif
#endif

// Per-function instruction-set targets for the x86 kernels, so that SSE2 and
// AVX2 code can be compiled without the corresponding -m flags and selected
// at run time.  VIO_TARGET_PUSH_*() / VIO_TARGET_POP() bracket the kernel
// definitions (MSVC needs no annotation).  Kernels use functions rather than
// lambdas: before C++20, clang-cl parses function template bodies at the end
// of the translation unit, where the pragma no longer reaches lambdas.
#if defined(__clang__)
#define VIO_TARGET_PUSH_(isa) \
  _Pragma(VIO_STR_(clang attribute push(__attribute__((target(isa))), apply_to = function)))
#define VIO_TARGET_POP() _Pragma("clang attribute pop")
#elif defined(__GNUC__)
#define VIO_TARGET_PUSH_(isa) _Pragma("GCC push_options") _Pragma(VIO_STR_(GCC target(isa)))
#define VIO_TARGET_POP() _Pragma("GCC pop_options")
#else
#define VIO_TARGET_PUSH_(isa)
#define VIO_TARGET_POP()
#endif
#define VIO_STR_(...) #__VA_ARGS__
#define VIO_TARGET_PUSH_SSE2() VIO_TARGET_PUSH_("sse2")
#define VIO_TARGET_PUSH_AVX2() VIO_TARGET_PUSH_("avx2")
#define VIO_TARGET_PUSH_SSE2_BMI2() VIO_TARGET_PUSH_("sse2,bmi,bmi2,lzcnt")

namespace vio {

using byte = std::uint8_t;

// Hides a pointer's target from the optimizer.  SIMD kernels load constant
// tables through it so that the compiler keeps a single vector load plus
// lane-indexed arithmetic instead of materializing each constant separately.
// Placed in a rarely taken branch, stops the compiler from if-converting it
// into unconditionally executed selects.
VIO_INLINE void branch_barrier() {
#if defined(__GNUC__) || defined(__clang__)
  __asm__ volatile("");
#endif
}

template <class T>
VIO_INLINE const T* opaque(const T* p) {
#if defined(__GNUC__) || defined(__clang__)
  __asm__("" : "+r"(p));
#endif
  return p;
}

}  // namespace vio
