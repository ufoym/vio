// vio::png - PNG codec.
//
//   png/encoder.hpp   png::Encoder
//   png/decoder.hpp   png::Decoder: all standard PNG variants (bit depths,
//                     color types, Adam7 interlacing) in the PNG's native
//                     sample layout
//   png/inflate.hpp, png/deflate.hpp, png/checksum.hpp
//                     the zlib functionality PNG needs (vio::png::zlib)
//
// png::Reader/Writer adapt the codec to vio::ImageReader/ImageWriter.
#pragma once

#include "png/decoder.hpp"
#include "png/encoder.hpp"

namespace vio::png {

// ---------------------------------------------------------------------------
// Source / Destination adapters
// ---------------------------------------------------------------------------
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

 public:
  std::vector<std::uint8_t> icc_profile() override {
    return reader_.has_icc ? reader_.icc_profile : std::vector<std::uint8_t>{};
  }

 protected:
  void start_input() override {
    reader_ = Decoder();
    reader_.read_header(in_.data());
    const png::Header& h = reader_.header;
    if (h.width > kMaxDimension || h.height > kMaxDimension)
      VIO_FAIL("Maximum supported image dimension is %u pixels", kMaxDimension);
    if (h.bit_depth != 8 && h.bit_depth != 16)
      VIO_FAIL("Only 8- and 16-bit-per-channel PNG files are supported");
    if (this->max_pixels && static_cast<unsigned long long>(h.width) * h.height > this->max_pixels)
      VIO_FAIL("Maximum supported image dimension is %u pixels",
               static_cast<unsigned>(this->max_pixels));
    reader_.decode(in_.data());
    width = h.width;
    height = h.height;
    switch (h.color_type) {
      case png::kGray:
      case png::kGrayAlpha:
        if (color == ColorSpace::Unknown || color == ColorSpace::RGB) color = ColorSpace::Gray;
        if (color != ColorSpace::Gray && color != ColorSpace::CMYK && !vio::detail::is_rgb_family(color))
          VIO_FAIL("Bogus input colorspace");
        png_comps_ = h.color_type == png::kGrayAlpha ? 2 : 1;
        break;
      case png::kTruecolor:
      case png::kTruecolorAlpha:
        if (color == ColorSpace::Unknown) color = ColorSpace::ExtRGB;
        if (color != ColorSpace::CMYK && !vio::detail::is_rgb_family(color))
          VIO_FAIL("Bogus input colorspace");
        png_comps_ = h.color_type == png::kTruecolorAlpha ? 4 : 3;
        break;
      case png::kIndexed: {
        if (h.bit_depth != 8 || reader_.palette.size() > 256)
          VIO_FAIL("Numeric value out of range in PNG file");
        bool gray = true;
        for (const auto& e : reader_.palette)
          if (e.red != e.green || e.green != e.blue) gray = false;
        if ((color == ColorSpace::Unknown || color == ColorSpace::RGB) && gray)
          color = ColorSpace::Gray;
        if (color == ColorSpace::Gray && !gray) VIO_FAIL("Bogus input colorspace");
        if (color == ColorSpace::Unknown) color = ColorSpace::ExtRGB;
        png_comps_ = 1;
        break;
      }
      default:
        VIO_FAIL("Numeric value out of range in PNG file");
    }
    this->set_components_from_color();
    unsigned maxval = h.bit_depth == 16 ? 65535 : 255;
    rescale_.assign(static_cast<std::size_t>(maxval) + 1, T{0});
    std::size_t half = maxval / 2;
    for (std::size_t v = 0; v <= maxval; v++)
      rescale_[v] = static_cast<T>((v * ((1u << precision) - 1) + half) / maxval);
    buf_.allocate(static_cast<std::size_t>(width) * std::max(components, 1), 1);
    row_ptr_ = buf_[0];
    this->buffer_ = &row_ptr_;
    this->buffer_height_ = 1;
  }

  unsigned get_rows() override {
    const png::Header& h = reader_.header;
    const std::uint8_t* raw = reader_.row(this->next_row);
    T* ptr = row_ptr_;
    const int ci = vio::detail::idx(color);
    const T maxs = static_cast<T>((1 << precision) - 1);
    const int alpha = png_comps_ == 2 || png_comps_ == 4 ? 1 : 0;
    if (h.color_type == png::kIndexed) {
      const auto& pal = reader_.palette;
      for (unsigned col = 0; col < width; col++) {
        unsigned index = raw[col];
        if (index >= pal.size()) VIO_FAIL("Numeric value out of range in PNG file");
        T r = rescale_[pal[index].red], g = rescale_[pal[index].green],
          b = rescale_[pal[index].blue];
        if (color == ColorSpace::Gray) {
          *ptr++ = r;
        } else if (color == ColorSpace::CMYK) {
          vio::detail::rgb_to_cmyk<T>(maxs, r, g, b, ptr, ptr + 1, ptr + 2, ptr + 3);
          ptr += 4;
        } else {
          ptr[vio::detail::rgb_red[ci]] = r;
          ptr[vio::detail::rgb_green[ci]] = g;
          ptr[vio::detail::rgb_blue[ci]] = b;
          if (vio::detail::alpha_index[ci] >= 0) ptr[vio::detail::alpha_index[ci]] = maxs;
          ptr += vio::detail::rgb_pixelsize[ci];
        }
      }
      return 1;
    }
    const bool sixteen = h.bit_depth == 16;
    std::size_t si = 0;
    auto sample = [&]() -> T {
      unsigned v;
      if (sixteen) {
        std::uint16_t w;
        std::memcpy(&w, raw + 2 * si, 2);
        v = w;
      } else {
        v = raw[si];
      }
      si++;
      return rescale_[v];
    };
    const bool gray_file = png_comps_ <= 2;
    for (unsigned col = 0; col < width; col++) {
      T r, g, b;
      if (gray_file) {
        r = g = b = sample();
      } else {
        r = sample();
        g = sample();
        b = sample();
      }
      si += static_cast<std::size_t>(alpha);
      if (color == ColorSpace::Gray) {
        *ptr++ = r;
      } else if (color == ColorSpace::CMYK) {
        vio::detail::rgb_to_cmyk<T>(maxs, r, g, b, ptr, ptr + 1, ptr + 2, ptr + 3);
        ptr += 4;
      } else {
        ptr[vio::detail::rgb_red[ci]] = r;
        ptr[vio::detail::rgb_green[ci]] = g;
        ptr[vio::detail::rgb_blue[ci]] = b;
        if (vio::detail::alpha_index[ci] >= 0) ptr[vio::detail::alpha_index[ci]] = maxs;
        ptr += vio::detail::rgb_pixelsize[ci];
      }
    }
    return 1;
  }

 private:
  Decoder reader_;
  int png_comps_ = 3;
  std::vector<T> rescale_;
  vio::detail::RowBuffer<T> buf_;
  T* row_ptr_ = nullptr;
};

template <class T>
class Writer : public ImageWriter<T> {
  using Base = ImageWriter<T>;
  static constexpr int kBitDepth = sizeof(T) == 1 ? 8 : 16;
  using PngSample = std::conditional_t<sizeof(T) == 1, std::uint8_t, std::uint16_t>;

 public:
  void set_icc_profile(std::span<const std::uint8_t> icc) override {
    if (icc.empty()) VIO_FAIL("Empty input file");
    writer_.icc_profile.assign(icc.begin(), icc.end());
  }

  void write_rows(T** rows, unsigned n) override {
    for (unsigned r = 0; r < n; r++) put_row(rows[r]);
  }

 protected:
  void start_output() override {
    const ColorSpace cs = this->color;
    std::uint8_t color_type;
    indexed_ = false;
    if (cs == ColorSpace::Gray) {
      color_type = png::kGray;
    } else if (vio::detail::is_rgb_family(cs) || cs == ColorSpace::CMYK) {
      if (!vio::detail::is_rgb_family(cs) && this->quantized())
        VIO_FAIL("PNG output must be grayscale, truecolor, or indexed-color");
      indexed_ = kBitDepth == 8 && this->quantized() && this->precision == 8 &&
                 vio::detail::is_rgb_family(cs);
      color_type = indexed_ ? png::kIndexed : png::kTruecolor;
    } else {
      VIO_FAIL("PNG output must be grayscale, truecolor, or indexed-color");
    }
    writer_.header.width = this->width;
    writer_.header.height = this->height;
    writer_.header.bit_depth = kBitDepth;
    writer_.header.color_type = color_type;
    if (indexed_) {
      const int ci = vio::detail::idx(cs);
      const auto& cm = this->colormap;
      std::size_t n = cm[0].size();
      writer_.palette.resize(n);
      for (std::size_t i = 0; i < n; i++)
        writer_.palette[i] = {static_cast<std::uint8_t>(cm[vio::detail::rgb_red[ci]][i]),
                              static_cast<std::uint8_t>(cm[vio::detail::rgb_green[ci]][i]),
                              static_cast<std::uint8_t>(cm[vio::detail::rgb_blue[ci]][i])};
    }
    std::size_t samples = static_cast<std::size_t>(this->width) *
                          (cs == ColorSpace::Gray || indexed_ ? 1 : 3);
    iobuf_.assign(samples, 0);
    maxval_ = (1u << this->precision) - 1u;
    if (static_cast<int>(this->precision) != kBitDepth) {
      rescale_.assign(static_cast<std::size_t>(maxval_) + 1, 0);
      long half = maxval_ / 2;
      for (long v = 0; v <= static_cast<long>(maxval_); v++)
        rescale_[static_cast<std::size_t>(v)] =
            static_cast<PngSample>((v * ((1L << kBitDepth) - 1) + half) / maxval_);
    }
    writer_.mode = png::Encoder::Mode::kZlib;
    writer_.begin();
  }

  void finish_output() override {
    std::vector<std::uint8_t> file = writer_.finish();
    this->out_.put(file.data(), file.size());
  }

 private:
  PngSample map(unsigned v) const {
    if (rescale_.empty()) return static_cast<PngSample>(v);
    if (v > maxval_) VIO_FAIL("Numeric value out of range in PNG file");
    return rescale_[v];
  }

  void put_row(const T* ptr) {
    const ColorSpace cs = this->color;
    const int ci = vio::detail::idx(cs);
    const unsigned w = this->width;
    PngSample* out = iobuf_.data();
    if (this->quantized()) {
      const auto& cm = this->colormap;
      if (indexed_) {
        for (unsigned col = 0; col < w; col++) *out++ = static_cast<PngSample>(ptr[col]);
      } else if (cs == ColorSpace::Gray) {
        for (unsigned col = 0; col < w; col++)
          *out++ = map(static_cast<unsigned>(static_cast<PngSample>(cm[0][ptr[col]])));
      } else {
        const auto& c0 = cm[vio::detail::rgb_red[ci]];
        const auto& c1 = cm[vio::detail::rgb_green[ci]];
        const auto& c2 = cm[vio::detail::rgb_blue[ci]];
        for (unsigned col = 0; col < w; col++) {
          int v = ptr[col];
          unsigned r = static_cast<PngSample>(c0[v]), g = static_cast<PngSample>(c1[v]),
                   b = static_cast<PngSample>(c2[v]);
          if (!rescale_.empty() && (r > maxval_ || g > maxval_ || b > maxval_))
            VIO_FAIL("Numeric value out of range in PNG file");
          *out++ = map(r);
          *out++ = map(g);
          *out++ = map(b);
        }
      }
    } else if (cs == ColorSpace::Gray) {
      for (unsigned col = 0; col < w; col++)
        *out++ = map(static_cast<PngSample>(ptr[col]));
    } else if (cs == ColorSpace::CMYK) {
      for (unsigned col = 0; col < w; col++, ptr += 4) {
        T r, g, b;
        vio::detail::cmyk_to_rgb<T>(static_cast<int>(maxval_), ptr[0], ptr[1], ptr[2], ptr[3], &r, &g,
                               &b);
        unsigned ur = static_cast<PngSample>(r), ug = static_cast<PngSample>(g),
                 ub = static_cast<PngSample>(b);
        if (!rescale_.empty() && (ur > maxval_ || ug > maxval_ || ub > maxval_))
          VIO_FAIL("Numeric value out of range in PNG file");
        *out++ = map(ur);
        *out++ = map(ug);
        *out++ = map(ub);
      }
    } else {
      const int ri = vio::detail::rgb_red[ci], gi = vio::detail::rgb_green[ci], bi = vio::detail::rgb_blue[ci];
      const int ps = vio::detail::rgb_pixelsize[ci];
      for (unsigned col = 0; col < w; col++, ptr += ps) {
        unsigned r = static_cast<PngSample>(ptr[ri]), g = static_cast<PngSample>(ptr[gi]),
                 b = static_cast<PngSample>(ptr[bi]);
        if (!rescale_.empty() && (r > maxval_ || g > maxval_ || b > maxval_))
          VIO_FAIL("Numeric value out of range in PNG file");
        *out++ = map(r);
        *out++ = map(g);
        *out++ = map(b);
      }
    }
    writer_.write_row(iobuf_.data());
  }

  Encoder writer_;
  bool indexed_ = false;
  unsigned maxval_ = 255;
  std::vector<PngSample> iobuf_;
  std::vector<PngSample> rescale_;
};

}  // namespace vio::png
