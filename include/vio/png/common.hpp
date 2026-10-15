// vio::png - types shared by the PNG encoder and decoder.
#pragma once

#include <cstdint>

#include "../core.hpp"

namespace vio::png {

enum ColorType : std::uint8_t {
  kGray = 0,
  kTruecolor = 2,
  kIndexed = 3,
  kGrayAlpha = 4,
  kTruecolorAlpha = 6,
};

struct Header {
  std::uint32_t width = 0, height = 0;
  std::uint8_t bit_depth = 8;
  std::uint8_t color_type = kTruecolor;
  std::uint8_t interlace = 0;
  bool operator==(const Header&) const = default;
};

struct PaletteEntry {
  std::uint8_t red, green, blue;
  bool operator==(const PaletteEntry&) const = default;
};

inline int num_channels(std::uint8_t color_type) {
  switch (color_type) {
    case kGray: case kIndexed: return 1;
    case kGrayAlpha: return 2;
    case kTruecolor: return 3;
    default: return 4;
  }
}

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
