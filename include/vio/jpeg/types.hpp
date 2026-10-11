// vio::jpeg - public vocabulary types shared by the encoder and decoder.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "../core.hpp"
#include "config.hpp"

namespace vio::jpeg {

using vio::ColorSpace;
using vio::kNumColorSpaces;

enum class DctMethod { IntSlow, IntFast, Float };
enum class DitherMode { None, Ordered, FloydSteinberg };

namespace detail {
using vio::detail::idx;
using vio::detail::is_rgb_family;
using vio::detail::rgb_blue;
using vio::detail::rgb_green;
using vio::detail::rgb_pixelsize;
using vio::detail::rgb_red;
}  // namespace detail

// Constants of the JPEG standard / implementation limits.
inline constexpr int DCTSIZE = 8;
inline constexpr int DCTSIZE2 = 64;
inline constexpr int NUM_QUANT_TBLS = 4;
inline constexpr int NUM_HUFF_TBLS = 4;
inline constexpr int NUM_ARITH_TBLS = 16;
inline constexpr int MAX_COMPS_IN_SCAN = 4;
inline constexpr int MAX_SAMP_FACTOR = 4;
inline constexpr int C_MAX_BLOCKS_IN_MCU = 10;
inline constexpr int D_MAX_BLOCKS_IN_MCU = 10;
inline constexpr int MAX_COMPONENTS = 10;
inline constexpr unsigned JPEG_MAX_DIMENSION = 65500;

using Coef = std::int16_t;
using Block = std::array<Coef, DCTSIZE2>;

struct QuantTable {
  std::array<std::uint16_t, DCTSIZE2> quantval{};  // natural order
  bool sent_table = false;
};

struct HuffTable {
  std::array<std::uint8_t, 17> bits{};      // bits[k] = # of symbols with k-bit codes
  std::array<std::uint8_t, 256> huffval{};  // symbols in order of increasing length
  bool sent_table = false;
};

struct SavedMarker {
  std::uint8_t marker = 0;
  unsigned original_length = 0;
  std::vector<std::uint8_t> data;
};

// Marker codes
enum Marker : int {
  M_SOF0 = 0xc0, M_SOF1 = 0xc1, M_SOF2 = 0xc2, M_SOF3 = 0xc3,
  M_SOF5 = 0xc5, M_SOF6 = 0xc6, M_SOF7 = 0xc7, M_JPG = 0xc8,
  M_SOF9 = 0xc9, M_SOF10 = 0xca, M_SOF11 = 0xcb, M_SOF13 = 0xcd,
  M_SOF14 = 0xce, M_SOF15 = 0xcf, M_DHT = 0xc4, M_DAC = 0xcc,
  M_RST0 = 0xd0, M_RST7 = 0xd7, M_SOI = 0xd8, M_EOI = 0xd9,
  M_SOS = 0xda, M_DQT = 0xdb, M_DNL = 0xdc, M_DRI = 0xdd,
  M_DHP = 0xde, M_EXP = 0xdf, M_APP0 = 0xe0, M_APP1 = 0xe1,
  M_APP2 = 0xe2, M_APP14 = 0xee, M_APP15 = 0xef, M_JPG0 = 0xf0,
  M_JPG13 = 0xfd, M_COM = 0xfe, M_TEM = 0x01,
};

namespace detail {

// zigzag index -> natural index (with 16 guard entries for corrupt data)
inline constexpr int natural_order[DCTSIZE2 + 16] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
    63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};

// natural index -> zigzag index
inline constexpr int zigzag_order[DCTSIZE2] = {
    0,  1,  5,  6,  14, 15, 27, 28, 2,  4,  7,  13, 16, 26, 29, 42,
    3,  8,  12, 17, 25, 30, 41, 43, 9,  11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63};

inline constexpr std::uint8_t bits_dc_luminance[17] = {0, 0, 1, 5, 1, 1, 1, 1, 1,
                                                       1, 0, 0, 0, 0, 0, 0, 0};
inline constexpr std::uint8_t val_dc_luminance[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
inline constexpr std::uint8_t bits_dc_chrominance[17] = {0, 0, 3, 1, 1, 1, 1, 1, 1,
                                                         1, 1, 1, 0, 0, 0, 0, 0};
inline constexpr std::uint8_t val_dc_chrominance[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
inline constexpr std::uint8_t bits_ac_luminance[17] = {0, 0, 2, 1, 3, 3, 2, 4,   3,
                                                       5, 5, 4, 4, 0, 0, 1, 0x7d};
inline constexpr std::uint8_t val_ac_luminance[] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51,
    0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1,
    0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92,
    0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8,
    0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2,
    0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};
inline constexpr std::uint8_t bits_ac_chrominance[17] = {0, 0, 2, 1, 2, 4, 4, 3,   4,
                                                         7, 5, 4, 4, 0, 1, 2, 0x77};
inline constexpr std::uint8_t val_ac_chrominance[] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07,
    0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09,
    0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25,
    0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
    0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
    0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2,
    0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa};

inline void set_huff_table(HuffTable& t, const std::uint8_t* bits, const std::uint8_t* val) {
  int nsymbols = 0;
  for (int len = 0; len <= 16; len++) t.bits[len] = bits[len];
  for (int len = 1; len <= 16; len++) nsymbols += bits[len];
  if (nsymbols < 1 || nsymbols > 256) VIO_FAIL("Bogus Huffman table definition");
  t.huffval.fill(0);
  for (int i = 0; i < nsymbols; i++) t.huffval[i] = val[i];
  t.sent_table = false;
}

// Equivalent of std_huff_tables(): installs the standard (Annex K) tables.
// When `only_missing` is set (decompressor), existing tables are retained.
template <class TablePtrArray>
void std_huff_tables(TablePtrArray& dc, TablePtrArray& ac, bool only_missing) {
  auto add = [&](auto& slot, const std::uint8_t* bits, const std::uint8_t* val) {
    if (slot) {
      if (only_missing) return;
    } else {
      slot.emplace();
    }
    set_huff_table(*slot, bits, val);
  };
  add(dc[0], bits_dc_luminance, val_dc_luminance);
  add(ac[0], bits_ac_luminance, val_ac_luminance);
  add(dc[1], bits_dc_chrominance, val_dc_chrominance);
  add(ac[1], bits_ac_chrominance, val_ac_chrominance);
}

}  // namespace detail
}  // namespace vio::jpeg
