// Reads a color quantization map from a GIF or PPM file
// (rdcolmap.c semantics: every distinct color of the file becomes a map
// entry; returns colormap[component][index]).
#pragma once

#include "vio/core.hpp"

namespace cli {

inline std::vector<std::vector<int>> read_color_map(vio::span<const std::uint8_t> file,
                                                    int precision) {
  if (precision != 8 && precision != 12) VIO_FAIL("Unsupported JPEG data precision %d", precision);
  const int maxj = (1 << precision) - 1;
  std::vector<std::vector<int>> cmap(3);
  auto add = [&](int r, int g, int b) {
    for (std::size_t i = 0; i < cmap[0].size(); i++)
      if (cmap[0][i] == r && cmap[1][i] == g && cmap[2][i] == b) return;
    if (static_cast<int>(cmap[0].size()) >= maxj + 1)
      VIO_FAIL("Cannot quantize to more than %d colors", maxj + 1);
    cmap[0].push_back(r);
    cmap[1].push_back(g);
    cmap[2].push_back(b);
  };
  const char* bad = "Unsupported color map file format";
  vio::detail::InBuffer in(file);
  auto getc_or_fail = [&] {
    int c = in.getc();
    if (c < 0) VIO_FAIL("%s", bad);
    return c;
  };
  int first = in.getc();
  if (first == 'G') {
    int header[13];
    for (int i = 1; i < 13; i++) header[i] = getc_or_fail();
    if (header[1] != 'I' || header[2] != 'F') VIO_FAIL("%s", bad);
    if ((header[10] & 0x80) == 0) VIO_FAIL("%s", bad);
    int len = 2 << (header[10] & 0x07);
    for (int i = 0; i < len; i++) {
      int r = getc_or_fail(), g = getc_or_fail(), b = getc_or_fail();
      add(r << (precision - 8), g << (precision - 8), b << (precision - 8));
    }
  } else if (first == 'P') {
    auto pbm_getc = [&] {
      int ch = in.getc();
      if (ch == '#') {
        do {
          ch = in.getc();
        } while (ch != '\n' && ch != -1);
      }
      return ch;
    };
    auto read_int = [&](unsigned maxval) {
      int ch;
      do {
        ch = pbm_getc();
        if (ch == -1) VIO_FAIL("%s", bad);
      } while (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
      if (ch < '0' || ch > '9') VIO_FAIL("%s", bad);
      unsigned val = static_cast<unsigned>(ch - '0');
      while ((ch = pbm_getc()) >= '0' && ch <= '9') {
        val = val * 10 + static_cast<unsigned>(ch - '0');
        if (val > maxval) VIO_FAIL("Numeric value out of range in PPM file");
      }
      return val;
    };
    int c = in.getc();
    unsigned w = read_int(65535), h = read_int(65535), maxval = read_int(65535);
    if (w == 0 || h == 0 || maxval == 0) VIO_FAIL("%s", bad);
    if (w > vio::kMaxDimension || h > vio::kMaxDimension)
      VIO_FAIL("Maximum supported image dimension is %u pixels", vio::kMaxDimension);
    if (maxval != static_cast<unsigned>(maxj)) VIO_FAIL("%s", bad);
    for (unsigned row = 0; row < h; row++)
      for (unsigned col = 0; col < w; col++) {
        if (c == '3') {
          int r = static_cast<int>(read_int(maxval)), g = static_cast<int>(read_int(maxval)),
              b = static_cast<int>(read_int(maxval));
          add(r, g, b);
        } else if (c == '6') {
          int r = getc_or_fail(), g = getc_or_fail(), b = getc_or_fail();
          add(r, g, b);
        } else {
          VIO_FAIL("%s", bad);
        }
      }
  } else {
    VIO_FAIL("%s", bad);
  }
  return cmap;
}

}  // namespace cli
