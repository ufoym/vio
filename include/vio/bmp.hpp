// vio::bmp - Windows/OS2 BMP reader (8-bit colormapped, 24-bit, 32-bit,
// uncompressed) and writer (8-bit colormapped/gray, 24-bit).  8-bit
// sample precision only.
#pragma once

#include "core.hpp"

namespace vio::bmp {

class Reader : public ImageReader<std::uint8_t> {
 public:
  // true: deliver rows top-down (the file stores them bottom-up);
  // false: deliver rows in file order (bottom row first).
  bool use_inversion_array = true;

 protected:
  void start_input() override {
    if (precision != 8) VIO_FAIL("Unsupported JPEG data precision %d", precision);
    std::uint8_t fh[14], ih[64];
    if (!in_.read(fh, 14)) VIO_FAIL("Premature end of input file");
    if (get2(fh, 0) != 0x4D42) VIO_FAIL("Not a BMP file - does not start with BM");
    int bfOffBits = static_cast<int>(get4(fh, 10));
    if (!in_.read(ih, 4)) VIO_FAIL("Premature end of input file");
    int headerSize = static_cast<int>(get4(ih, 0));
    if (headerSize < 12 || headerSize > 64 || (headerSize + 14) > bfOffBits)
      VIO_FAIL("Invalid BMP file: bad header length");
    if (!in_.read(ih + 4, static_cast<std::size_t>(headerSize - 4)))
      VIO_FAIL("Premature end of input file");
    int biWidth, biHeight, biClrUsed = 0, mapentrysize = 0;
    unsigned biPlanes;
    switch (headerSize) {
      case 12:
        biWidth = static_cast<int>(get2(ih, 4));
        biHeight = static_cast<int>(get2(ih, 6));
        biPlanes = get2(ih, 8);
        bits_ = static_cast<int>(get2(ih, 10));
        if (bits_ == 8) mapentrysize = 3;
        else if (bits_ != 24 && bits_ != 32)
          VIO_FAIL("Only 8-, 24-, and 32-bit BMP files are supported");
        break;
      case 40:
      case 64: {
        biWidth = static_cast<int>(get4(ih, 4));
        biHeight = static_cast<int>(get4(ih, 8));
        biPlanes = get2(ih, 12);
        bits_ = static_cast<int>(get2(ih, 14));
        unsigned biCompression = get4(ih, 16);
        int xppm = static_cast<int>(get4(ih, 24)), yppm = static_cast<int>(get4(ih, 28));
        biClrUsed = static_cast<int>(get4(ih, 32));
        if (bits_ == 8) mapentrysize = 4;
        else if (bits_ != 24 && bits_ != 32)
          VIO_FAIL("Only 8-, 24-, and 32-bit BMP files are supported");
        if (biCompression != 0) VIO_FAIL("Sorry, compressed BMPs not yet supported");
        if (xppm > 0 && yppm > 0) {
          x_density = static_cast<std::uint16_t>(xppm / 100);
          y_density = static_cast<std::uint16_t>(yppm / 100);
          density_unit = 2;
        }
        break;
      }
      default:
        VIO_FAIL("Invalid BMP file: bad header length");
    }
    if (biWidth <= 0 || biHeight <= 0) VIO_FAIL("Empty BMP image");
    check_dimensions(static_cast<unsigned>(biWidth), static_cast<unsigned>(biHeight));
    if (biPlanes != 1) VIO_FAIL("Invalid BMP file: biPlanes not equal to 1");
    int bPad = bfOffBits - (headerSize + 14);
    if (mapentrysize > 0) {
      if (biClrUsed <= 0) biClrUsed = 256;
      else if (biClrUsed > 256) VIO_FAIL("Unsupported BMP colormap format");
      cmap_length_ = biClrUsed;
      read_colormap(biClrUsed, mapentrysize);
      bPad -= biClrUsed * mapentrysize;
    }
    if (bPad < 0) VIO_FAIL("Invalid BMP file: bad header length");
    if (!in_.take(static_cast<std::size_t>(bPad))) VIO_FAIL("Premature end of input file");

    unsigned row_width = 0;
    switch (bits_) {
      case 8:
        if (color == ColorSpace::Unknown) color = ColorSpace::ExtRGB;
        if (!detail::is_rgb_family(color) && color != ColorSpace::Gray &&
            color != ColorSpace::CMYK)
          VIO_FAIL("Bogus input colorspace");
        row_width = static_cast<unsigned>(biWidth);
        break;
      case 24:
        if (color == ColorSpace::Unknown) color = ColorSpace::ExtBGR;
        if (!detail::is_rgb_family(color) && color != ColorSpace::CMYK)
          VIO_FAIL("Bogus input colorspace");
        row_width = static_cast<unsigned>(biWidth) * 3;
        break;
      default:
        if (color == ColorSpace::Unknown) color = ColorSpace::ExtBGRA;
        if (!detail::is_rgb_family(color) && color != ColorSpace::CMYK)
          VIO_FAIL("Bogus input colorspace");
        row_width = static_cast<unsigned>(biWidth) * 4;
        break;
    }
    set_components_from_color();
    while ((row_width & 3) != 0) row_width++;
    row_width_ = row_width;
    width = static_cast<unsigned>(biWidth);
    height = static_cast<unsigned>(biHeight);
    data_start_ = in_.pos();
    buf_.allocate(static_cast<std::size_t>(width) * components, 1);
    row_ptr_ = buf_[0];
    buffer_ = &row_ptr_;
    buffer_height_ = 1;
    preloaded_ = false;
  }

  unsigned get_rows() override {
    const std::uint8_t* inptr;
    if (use_inversion_array) {
      if (!preloaded_) {
        if (in_.remaining() < static_cast<std::size_t>(row_width_) * height)
          VIO_FAIL("Premature end of input file");
        preloaded_ = true;
      }
      std::size_t file_row = height - 1 - next_row;
      inptr = in_.data().data() + data_start_ + file_row * row_width_;
    } else {
      inptr = in_.take(row_width_);
      if (!inptr) VIO_FAIL("Premature end of input file");
    }
    std::uint8_t* outptr = row_ptr_;
    const int ci = detail::idx(color);
    if (bits_ == 8) {
      for (unsigned col = 0; col < width; col++) {
        int t = *inptr++;
        if (t >= cmap_length_) VIO_FAIL("Numeric value out of range in BMP file");
        if (color == ColorSpace::Gray) {
          *outptr++ = cmap_[0][t];
        } else if (color == ColorSpace::CMYK) {
          detail::rgb_to_cmyk<std::uint8_t>(255, cmap_[0][t], cmap_[1][t], cmap_[2][t], outptr,
                                            outptr + 1, outptr + 2, outptr + 3);
          outptr += 4;
        } else {
          outptr[detail::rgb_red[ci]] = cmap_[0][t];
          outptr[detail::rgb_green[ci]] = cmap_[1][t];
          outptr[detail::rgb_blue[ci]] = cmap_[2][t];
          if (detail::alpha_index[ci] >= 0) outptr[detail::alpha_index[ci]] = 0xFF;
          outptr += detail::rgb_pixelsize[ci];
        }
      }
      return 1;
    }
    const int step = bits_ / 8;
    if ((bits_ == 24 && color == ColorSpace::ExtBGR) ||
        (bits_ == 32 && (color == ColorSpace::ExtBGRX || color == ColorSpace::ExtBGRA))) {
      std::memcpy(outptr, inptr, static_cast<std::size_t>(width) * step);
    } else if (color == ColorSpace::CMYK) {
      for (unsigned col = 0; col < width; col++, inptr += step, outptr += 4)
        detail::rgb_to_cmyk<std::uint8_t>(255, inptr[2], inptr[1], inptr[0], outptr, outptr + 1,
                                          outptr + 2, outptr + 3);
    } else {
      const int ri = detail::rgb_red[ci], gi = detail::rgb_green[ci], bi = detail::rgb_blue[ci];
      const int ai = detail::alpha_index[ci], ps = detail::rgb_pixelsize[ci];
      for (unsigned col = 0; col < width; col++, inptr += step, outptr += ps) {
        outptr[bi] = inptr[0];
        outptr[gi] = inptr[1];
        outptr[ri] = inptr[2];
        if (ai >= 0) outptr[ai] = bits_ == 32 ? inptr[3] : 0xFF;
      }
    }
    return 1;
  }

 private:
  static unsigned get2(const std::uint8_t* a, int o) { return a[o] + (unsigned(a[o + 1]) << 8); }
  static unsigned get4(const std::uint8_t* a, int o) {
    return a[o] + (unsigned(a[o + 1]) << 8) + (unsigned(a[o + 2]) << 16) +
           (unsigned(a[o + 3]) << 24);
  }
  int read_byte() {
    int c = in_.getc();
    if (c < 0) VIO_FAIL("Premature end of input file");
    return c;
  }
  void read_colormap(int cmaplen, int mapentrysize) {
    for (auto& c : cmap_) c.assign(static_cast<std::size_t>(cmaplen), 0);
    bool gray = true;
    for (int i = 0; i < cmaplen; i++) {
      cmap_[2][i] = static_cast<std::uint8_t>(read_byte());
      cmap_[1][i] = static_cast<std::uint8_t>(read_byte());
      cmap_[0][i] = static_cast<std::uint8_t>(read_byte());
      if (mapentrysize == 4) read_byte();
      if (cmap_[2][i] != cmap_[1][i] || cmap_[1][i] != cmap_[0][i]) gray = false;
    }
    if ((color == ColorSpace::Unknown || color == ColorSpace::RGB) && gray)
      color = ColorSpace::Gray;
    if (color == ColorSpace::Gray && !gray) VIO_FAIL("Bogus input colorspace");
  }

  int bits_ = 0;
  int cmap_length_ = 0;
  std::vector<std::uint8_t> cmap_[3];
  unsigned row_width_ = 0;
  std::size_t data_start_ = 0;
  bool preloaded_ = false;
  detail::RowBuffer<std::uint8_t> buf_;
  std::uint8_t* row_ptr_ = nullptr;
};

class Writer : public ImageWriter<std::uint8_t> {
 public:
  bool is_os2 = false;
  // true: accept rows top-down (written bottom-up at finish());
  // false: rows are supplied in file order (bottom row first).
  bool use_inversion_array = true;

  bool supports_partial() const override { return false; }

  void write_rows(std::uint8_t** rows, unsigned n) override {
    for (unsigned r = 0; r < n; r++) {
      std::uint8_t* outptr;
      if (use_inversion_array) {
        if (image_.empty()) image_.assign(static_cast<std::size_t>(row_width_) * height, 0);
        outptr = image_.data() + static_cast<std::size_t>(cur_row_++) * row_width_;
      } else {
        outptr = out_.grow(row_width_);
      }
      put_row(rows[r], outptr);
    }
  }

 protected:
  void start_output() override {
    if (precision != 8) VIO_FAIL("Unsupported JPEG data precision %d", precision);
    gray_rows_ = color == ColorSpace::Gray || (detail::is_rgb_family(color) && quantized());
    if (!gray_rows_ && !(detail::is_rgb_family(color) ||
                         (!quantized() && (color == ColorSpace::RGB565 ||
                                           color == ColorSpace::CMYK))))
      VIO_FAIL("BMP output must be grayscale or RGB");
    if (gray_rows_) row_width_ = width * (quantized() ? 1u : static_cast<unsigned>(components));
    else row_width_ = width * 3;
    while ((row_width_ & 3) != 0) row_width_++;
    cur_row_ = 0;
    image_.clear();
    if (!use_inversion_array) write_header();
  }

  void finish_output() override {
    if (use_inversion_array) {
      write_header();
      if (image_.empty()) image_.assign(static_cast<std::size_t>(row_width_) * height, 0);
      for (unsigned row = height; row > 0; row--)
        out_.put(image_.data() + static_cast<std::size_t>(row - 1) * row_width_, row_width_);
      image_.clear();
    }
  }

 private:
  void put_row(const std::uint8_t* inptr, std::uint8_t* outptr) {
    std::uint8_t* start = outptr;
    if (gray_rows_) {
      std::memcpy(outptr, inptr, width);
      outptr += width;
    } else if (color == ColorSpace::ExtBGR) {
      std::memcpy(outptr, inptr, static_cast<std::size_t>(width) * 3);
      outptr += static_cast<std::size_t>(width) * 3;
    } else if (color == ColorSpace::RGB565) {
      const auto* in2 = reinterpret_cast<const std::uint16_t*>(inptr);
      for (unsigned col = 0; col < width; col++, outptr += 3) {
        std::uint16_t v;
        std::memcpy(&v, in2 + col, 2);
        outptr[0] = static_cast<std::uint8_t>((v << 3) & 0xF8);
        outptr[1] = static_cast<std::uint8_t>((v >> 3) & 0xFC);
        outptr[2] = static_cast<std::uint8_t>((v >> 8) & 0xF8);
      }
    } else if (color == ColorSpace::CMYK) {
      for (unsigned col = 0; col < width; col++, inptr += 4, outptr += 3)
        detail::cmyk_to_rgb<std::uint8_t>(255, inptr[0], inptr[1], inptr[2], inptr[3],
                                          outptr + 2, outptr + 1, outptr);
    } else {
      const int ci = detail::idx(color);
      const int ri = detail::rgb_red[ci], gi = detail::rgb_green[ci], bi = detail::rgb_blue[ci];
      const int ps = detail::rgb_pixelsize[ci];
      for (unsigned col = 0; col < width; col++, inptr += ps, outptr += 3) {
        outptr[0] = inptr[bi];
        outptr[1] = inptr[gi];
        outptr[2] = inptr[ri];
      }
    }
    std::size_t used = static_cast<std::size_t>(outptr - start);
    std::memset(outptr, 0, row_width_ - used);
  }

  static void put2(std::uint8_t* a, int o, unsigned long v) {
    a[o] = static_cast<std::uint8_t>(v);
    a[o + 1] = static_cast<std::uint8_t>(v >> 8);
  }
  static void put4(std::uint8_t* a, int o, unsigned long v) {
    put2(a, o, v);
    put2(a, o + 2, v >> 16);
  }

  void write_header() {
    int bits_per_pixel, cmap_entries;
    if (detail::is_rgb_family(color)) {
      bits_per_pixel = quantized() ? 8 : 24;
      cmap_entries = quantized() ? 256 : 0;
    } else if (color == ColorSpace::RGB565 || color == ColorSpace::CMYK) {
      bits_per_pixel = 24;
      cmap_entries = 0;
    } else {
      bits_per_pixel = 8;
      cmap_entries = 256;
    }
    std::uint8_t fh[14] = {}, ih[40] = {};
    const int entry = is_os2 ? 3 : 4;
    unsigned long headersize = 14 + (is_os2 ? 12 : 40) + static_cast<unsigned long>(cmap_entries) * entry;
    unsigned long bfSize = headersize + static_cast<unsigned long>(row_width_) * height;
    fh[0] = 0x42;
    fh[1] = 0x4D;
    put4(fh, 2, bfSize);
    put4(fh, 10, headersize);
    out_.put(fh, 14);
    if (is_os2) {
      put2(ih, 0, 12);
      put2(ih, 4, width);
      put2(ih, 6, height);
      put2(ih, 8, 1);
      put2(ih, 10, static_cast<unsigned long>(bits_per_pixel));
      out_.put(ih, 12);
    } else {
      put2(ih, 0, 40);
      put4(ih, 4, width);
      put4(ih, 8, height);
      put2(ih, 12, 1);
      put2(ih, 14, static_cast<unsigned long>(bits_per_pixel));
      if (density_unit == 2) {
        put4(ih, 24, static_cast<unsigned long>(x_density * 100L));
        put4(ih, 28, static_cast<unsigned long>(y_density * 100L));
      }
      put2(ih, 32, static_cast<unsigned long>(cmap_entries));
      out_.put(ih, 40);
    }
    if (cmap_entries > 0) write_colormap(cmap_entries, entry);
  }

  void write_colormap(int map_colors, int entry) {
    int i = 0;
    auto emit = [&](int b, int g, int r) {
      out_.put(static_cast<std::uint8_t>(b));
      out_.put(static_cast<std::uint8_t>(g));
      out_.put(static_cast<std::uint8_t>(r));
      if (entry == 4) out_.put(0);
    };
    if (quantized()) {
      int num_colors = static_cast<int>(colormap[0].size());
      if (colormap.size() == 3) {
        const int ci = detail::idx(color);
        const auto& rc = colormap[detail::rgb_red[ci]];
        const auto& gc = colormap[detail::rgb_green[ci]];
        const auto& bc = colormap[detail::rgb_blue[ci]];
        for (; i < num_colors; i++) emit(bc[i], gc[i], rc[i]);
      } else {
        for (; i < num_colors; i++) emit(colormap[0][i], colormap[0][i], colormap[0][i]);
      }
    } else {
      for (; i < 256; i++) emit(i, i, i);
    }
    if (i > map_colors) VIO_FAIL("Output file format cannot handle %d colormap entries", i);
    for (; i < map_colors; i++) emit(0, 0, 0);
  }

  bool gray_rows_ = false;
  unsigned row_width_ = 0, cur_row_ = 0;
  std::vector<std::uint8_t> image_;
};

}  // namespace vio::bmp
