// vio::png internal utilities: signature, Adam7 tables, big-endian I/O and
// chunk validation helpers.
#pragma once

#include <cstdint>

#include "../types.hpp"

namespace vio::png {

namespace detail {
inline constexpr std::uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

inline std::uint32_t read_u32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
}
inline void write_u32(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>(v >> 24);
  p[1] = static_cast<std::uint8_t>(v >> 16);
  p[2] = static_cast<std::uint8_t>(v >> 8);
  p[3] = static_cast<std::uint8_t>(v);
}

inline constexpr int adam7_x_start[7] = {0, 4, 0, 2, 0, 1, 0};
inline constexpr int adam7_y_start[7] = {0, 0, 4, 0, 2, 0, 1};
inline constexpr int adam7_x_delta[7] = {8, 8, 4, 4, 2, 2, 1};
inline constexpr int adam7_y_delta[7] = {8, 8, 8, 4, 4, 2, 2};

[[noreturn]] inline void fail(const char* msg) { VIO_FAIL("%s", msg); }

// Chunk type bytes must be ASCII letters.
inline bool valid_chunk_type(const std::uint8_t* t) {
  for (int i = 0; i < 4; i++) {
    std::uint8_t c = t[i] & ~0x20;
    if (c < 'A' || c > 'Z') return false;
  }
  return true;
}

// Checks a floating-point number in text form (sCAL, pCAL): optional sign,
// digits with an optional point, optional exponent.  Sets *positive.
inline bool check_fp_number(const char* s, std::size_t n, bool* positive) {
  std::size_t i = 0;
  bool neg = false, digits = false, nonzero = false;
  if (i < n && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
  while (i < n && s[i] >= '0' && s[i] <= '9') {
    digits = true;
    nonzero |= s[i++] != '0';
  }
  if (i < n && s[i] == '.') {
    i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
      digits = true;
      nonzero |= s[i++] != '0';
    }
  }
  if (!digits) return false;
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    i++;
    if (i < n && (s[i] == '+' || s[i] == '-')) i++;
    bool exp_digits = false;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
      exp_digits = true;
      i++;
    }
    if (!exp_digits) return false;
  }
  if (i != n) return false;
  if (positive) *positive = nonzero && !neg;
  return true;
}

}  // namespace detail
using detail::fail;

}  // namespace vio::png
