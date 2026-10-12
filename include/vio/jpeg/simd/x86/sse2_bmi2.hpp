// vio::jpeg::simd::sse2_bmi2 - the SSE2 Huffman encoder compiled for CPUs
// that also have BMI1, BMI2 and LZCNT (selected at run time): the encoder
// loop is dominated by bit counts and variable shifts, which then compile
// to lzcnt/tzcnt and the flag-free shlx/shrx.
#pragma once

#include <emmintrin.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../../config.hpp"
#include "../../types.hpp"
#include "../common.hpp"

VIO_TARGET_PUSH_SSE2_BMI2()
namespace vio::jpeg::simd::sse2_bmi2 {

#define VIO_HUFF_BMI2 1  // MSVC: spell out lzcnt, shlx, ... (no target attributes)
#include "sse2_huff.inc"
#undef VIO_HUFF_BMI2

}  // namespace vio::jpeg::simd::sse2_bmi2
VIO_TARGET_POP()
