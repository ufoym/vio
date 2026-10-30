// vio::ppm - PPM/PGM reader and writer (binary P5/P6 and text P2/P3,
// maxval up to 65535).
#pragma once

#include "core.hpp"

namespace vio::ppm {

template <class T>
class Reader : public ImageReader<T> {
  using Base = ImageReader<T>;

 public:
  using Base::color;
  using Base::precision;
  using Base::width;
  using Base::height;
  using Base::components;
  using Base::in_;

 protected:
  void start_input() override {
    if (in_.getc() != 'P') VIO_FAIL("Not a PPM/PGM file");
    int fmt = in_.getc();
    if (fmt != '2' && fmt != '3' && fmt != '5' && fmt != '6') VIO_FAIL("Not a PPM/PGM file");
    unsigned w = read_integer(65535), h = read_integer(65535);
    maxval_ = read_integer(65535);
    if (w == 0 || h == 0 || maxval_ == 0) VIO_FAIL("Not a PPM/PGM file");
    this->check_dimensions(w, h);
    width = w;
    height = h;
    bool gray_file = fmt == '2' || fmt == '5';
    if (gray_file) {
      if (color == ColorSpace::Unknown || color == ColorSpace::RGB) color = ColorSpace::Gray;
    } else {
      if (color == ColorSpace::Unknown) color = ColorSpace::ExtRGB;
      if (color == ColorSpace::Gray) VIO_FAIL("Bogus input colorspace");
    }
    if (color != ColorSpace::Gray && color != ColorSpace::CMYK && !detail::is_rgb_family(color))
      VIO_FAIL("Bogus input colorspace");
    this->set_components_from_color();
    file_comps_ = gray_file ? 1 : 3;
    text_ = fmt == '2' || fmt == '3';
    word_ = !text_ && maxval_ > 255;
    if (!text_) iobuf_width_ = static_cast<std::size_t>(w) * file_comps_ * (word_ ? 2 : 1);

    // Rescaling table.  For byte-per-sample files whose maxval matches the
    // target precision, samples are used as-is (even beyond maxval).
    unsigned target = (1u << precision) - 1u;
    std::size_t tsize = std::max(maxval_, 255u) + 1;
    rescale_.assign(tsize, T{0});
    if (!text_ && !word_ && maxval_ == target) {
      for (std::size_t v = 0; v < 256; v++) rescale_[v] = static_cast<T>(v);
    } else {
      std::size_t half = maxval_ / 2;
      for (std::size_t v = 0; v <= maxval_; v++)
        rescale_[v] = static_cast<T>((v * target + half) / maxval_);
    }
    buf_.allocate(static_cast<std::size_t>(w) * components, 1);
    row_ptr_ = buf_[0];
    this->buffer_ = &row_ptr_;
    this->buffer_height_ = 1;
  }

  unsigned get_rows() override {
    const std::uint8_t* raw = nullptr;
    if (!text_) {
      raw = in_.take(iobuf_width_);
      if (!raw) VIO_FAIL("Premature end of input file");
    }
    auto sample = [&]() -> T {
      if (text_) return rescale_[read_integer(maxval_)];
      if (word_) {
        unsigned v = static_cast<unsigned>(raw[0]) << 8 | raw[1];
        raw += 2;
        if (v > maxval_) VIO_FAIL("Numeric value out of range in PPM file");
        return rescale_[v];
      }
      return rescale_[*raw++];
    };
    T* ptr = row_ptr_;
    const int ci = detail::idx(color);
    const T alpha = static_cast<T>((1 << precision) - 1);
    const int cmyk_max = (1 << precision) - 1;
    if (color == ColorSpace::Gray) {
      for (unsigned col = 0; col < width; col++) *ptr++ = sample();
    } else if (color == ColorSpace::CMYK) {
      for (unsigned col = 0; col < width; col++) {
        T r, g, b;
        if (file_comps_ == 1) {
          r = g = b = sample();
        } else {
          r = sample();
          g = sample();
          b = sample();
        }
        detail::rgb_to_cmyk<T>(cmyk_max, r, g, b, ptr, ptr + 1, ptr + 2, ptr + 3);
        ptr += 4;
      }
    } else {
      const int ri = detail::rgb_red[ci], gi = detail::rgb_green[ci], bi = detail::rgb_blue[ci];
      const int ai = detail::alpha_index[ci], ps = detail::rgb_pixelsize[ci];
      for (unsigned col = 0; col < width; col++) {
        if (file_comps_ == 1) {
          ptr[ri] = ptr[gi] = ptr[bi] = sample();
        } else {
          ptr[ri] = sample();
          ptr[gi] = sample();
          ptr[bi] = sample();
        }
        if (ai >= 0) ptr[ai] = alpha;
        ptr += ps;
      }
    }
    return 1;
  }

 private:
  int pbm_getc() {
    int ch = in_.getc();
    if (ch == '#') {
      do {
        ch = in_.getc();
      } while (ch != '\n' && ch != -1);
    }
    return ch;
  }
  unsigned read_integer(unsigned maxval) {
    int ch;
    do {
      ch = pbm_getc();
      if (ch == -1) VIO_FAIL("Premature end of input file");
    } while (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
    if (ch < '0' || ch > '9') VIO_FAIL("Nonnumeric data in PPM file");
    unsigned val = static_cast<unsigned>(ch - '0');
    while ((ch = pbm_getc()) >= '0' && ch <= '9') {
      val *= 10;
      val += static_cast<unsigned>(ch - '0');
      if (val > maxval) VIO_FAIL("Numeric value out of range in PPM file");
    }
    return val;
  }

  unsigned maxval_ = 0;
  int file_comps_ = 3;
  bool text_ = false, word_ = false;
  std::size_t iobuf_width_ = 0;
  std::vector<T> rescale_;
  detail::RowBuffer<T> buf_;
  T* row_ptr_ = nullptr;
};

template <class T>
class Writer : public ImageWriter<T> {
 public:
  void write_rows(T** rows, unsigned n) override {
    for (unsigned r = 0; r < n; r++) put_row(rows[r]);
  }

 protected:
  void start_output() override {
    const ColorSpace cs = this->color;
    const int maxval = (1 << this->precision) - 1;
    const char* magic;
    if (cs == ColorSpace::Gray) {
      magic = "P5";
    } else if (detail::is_rgb_family(cs) || cs == ColorSpace::CMYK) {
      if (!detail::is_rgb_family(cs) && this->quantized())
        VIO_FAIL("PPM output must be grayscale or RGB");
      magic = "P6";
    } else {
      VIO_FAIL("PPM output must be grayscale or RGB");
    }
    this->out_.put_str(strformat("%s\n%ld %ld\n%d\n", magic, static_cast<long>(this->width),
                                 static_cast<long>(this->height), maxval));
    samples_per_row_ = static_cast<std::size_t>(this->width) *
                       (cs == ColorSpace::Gray ? this->components : 3);
  }

 private:
  static constexpr int kBytes = sizeof(T) == 1 ? 1 : 2;

  static std::uint8_t* put(std::uint8_t* p, int v) {
    if constexpr (kBytes != 1) *p++ = static_cast<std::uint8_t>(v >> 8);
    *p++ = static_cast<std::uint8_t>(v);
    return p;
  }

  void put_row(const T* ptr) {
    const ColorSpace cs = this->color;
    const int ci = detail::idx(cs);
    const unsigned w = this->width;
    std::uint8_t* p = this->out_.grow(samples_per_row_ * kBytes);
    if (this->quantized()) {
      const auto& cmap = this->colormap;
      if (cs == ColorSpace::Gray) {
        for (unsigned col = 0; col < w; col++) p = put(p, cmap[0][ptr[col]]);
      } else {
        const auto& c0 = cmap[detail::rgb_red[ci]];
        const auto& c1 = cmap[detail::rgb_green[ci]];
        const auto& c2 = cmap[detail::rgb_blue[ci]];
        for (unsigned col = 0; col < w; col++) {
          int v = ptr[col];
          p = put(p, c0[v]);
          p = put(p, c1[v]);
          p = put(p, c2[v]);
        }
      }
    } else if (detail::is_rgb_family(cs)) {
      const int ri = detail::rgb_red[ci], gi = detail::rgb_green[ci], bi = detail::rgb_blue[ci];
      const int ps = detail::rgb_pixelsize[ci];
      for (unsigned col = 0; col < w; col++, ptr += ps) {
        p = put(p, ptr[ri]);
        p = put(p, ptr[gi]);
        p = put(p, ptr[bi]);
      }
    } else if (cs == ColorSpace::CMYK) {
      const int maxval = (1 << this->precision) - 1;
      for (unsigned col = 0; col < w; col++, ptr += 4) {
        T r, g, b;
        detail::cmyk_to_rgb<T>(maxval, ptr[0], ptr[1], ptr[2], ptr[3], &r, &g, &b);
        p = put(p, r);
        p = put(p, g);
        p = put(p, b);
      }
    } else {
      for (std::size_t i = 0; i < samples_per_row_; i++) p = put(p, ptr[i]);
    }
  }

  std::size_t samples_per_row_ = 0;
};

}  // namespace vio::ppm
