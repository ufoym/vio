// vio - definitions shared by all codecs: color spaces, error reporting, and
// the row-oriented reader/writer interfaces of the packed-pixel image file
// formats (PPM/PGM, BMP, PNG, GIF, Targa).
//
// Readers (`ImageReader<T>`) and writers (`ImageWriter<T>`) exchange rows of
// samples of type T, where T is the sample container for the data precision:
//   uint8_t  for  2..8-bit samples
//   int16_t  for  9..12-bit samples
//   uint16_t for 13..16-bit samples
// Rows use the interleaved pixel layout named by a vio::ColorSpace.
#pragma once

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace vio {

// ---------------------------------------------------------------------------
// Color spaces
// ---------------------------------------------------------------------------

// The numbering is fixed so that per-space lookup tables stay simple.
enum class ColorSpace : int {
  Unknown = 0,
  Gray,
  RGB,
  YCbCr,
  CMYK,
  YCCK,
  ExtRGB,
  ExtRGBX,
  ExtBGR,
  ExtBGRX,
  ExtXBGR,
  ExtXRGB,
  ExtRGBA,
  ExtBGRA,
  ExtABGR,
  ExtARGB,
  RGB565,
};
inline constexpr int kNumColorSpaces = 17;

namespace detail {

inline constexpr int rgb_red[kNumColorSpaces] = {-1, -1, 0, -1, -1, -1, 0, 0, 2,
                                                 2,  3,  1, 0,  2,  3,  1, -1};
inline constexpr int rgb_green[kNumColorSpaces] = {-1, -1, 1, -1, -1, -1, 1, 1, 1,
                                                   1,  2,  2, 1,  1,  2,  2, -1};
inline constexpr int rgb_blue[kNumColorSpaces] = {-1, -1, 2, -1, -1, -1, 2, 2, 0,
                                                  0,  1,  3, 2,  0,  1,  3, -1};
inline constexpr int rgb_pixelsize[kNumColorSpaces] = {-1, -1, 3, -1, -1, -1, 3, 4, 3,
                                                       4,  4,  4, 4,  4,  4,  4, -1};
// Position of a genuine alpha channel (RGBA/BGRA/ABGR/ARGB) or -1.  Readers
// fill alpha with the maximum sample value and leave X padding untouched.
inline constexpr int alpha_index[kNumColorSpaces] = {-1, -1, -1, -1, -1, -1, -1, -1, -1,
                                                     -1, -1, -1, 3,  3,  0,  0,  -1};

inline constexpr int idx(ColorSpace cs) { return static_cast<int>(cs); }

inline constexpr bool is_rgb_family(ColorSpace cs) {
  return cs == ColorSpace::RGB || (cs >= ColorSpace::ExtRGB && cs <= ColorSpace::ExtARGB);
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Error reporting
// ---------------------------------------------------------------------------

// All fatal errors are reported by throwing vio::Error.
class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline std::string strformat(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return buf;
}

#define VIO_FAIL(...) throw ::vio::Error(::vio::strformat(__VA_ARGS__))

// Non-fatal problems (corrupt data that can be worked around, etc.) are
// reported through a Diagnostics sink.  By default warnings are counted and
// the last message is kept; applications may install a callback, which may
// itself throw to turn warnings into errors.
struct Diagnostics {
  std::function<void(const std::string&)> on_warning;
  int num_warnings = 0;
  std::string last_warning;

  void warn(const std::string& msg) {
    num_warnings++;
    last_warning = msg;
    if (on_warning) on_warning(msg);
  }
};

// ---------------------------------------------------------------------------
// Image reader/writer interfaces
// ---------------------------------------------------------------------------

namespace detail {

// Range of data precisions handled by sample type T.
template <class T>
constexpr int min_precision() {
  return std::is_same_v<T, std::uint8_t> ? 2 : std::is_same_v<T, std::int16_t> ? 9 : 13;
}
template <class T>
constexpr int max_precision() {
  return std::is_same_v<T, std::uint8_t> ? 8 : std::is_same_v<T, std::int16_t> ? 12 : 16;
}
template <class T>
void check_precision(int precision) {
  if (precision < min_precision<T>() || precision > max_precision<T>())
    VIO_FAIL("Unsupported JPEG data precision %d", precision);
}

// RGB <-> CMYK conversion (fully reversible).
template <class T>
inline void rgb_to_cmyk(int maxval, T r, T g, T b, T* c, T* m, T* y, T* k) {
  double ctmp = 1.0 - (static_cast<double>(r) / static_cast<double>(maxval));
  double mtmp = 1.0 - (static_cast<double>(g) / static_cast<double>(maxval));
  double ytmp = 1.0 - (static_cast<double>(b) / static_cast<double>(maxval));
  double ktmp = std::min(std::min(ctmp, mtmp), ytmp);
  if (ktmp == 1.0) {
    ctmp = mtmp = ytmp = 0.0;
  } else {
    ctmp = (ctmp - ktmp) / (1.0 - ktmp);
    mtmp = (mtmp - ktmp) / (1.0 - ktmp);
    ytmp = (ytmp - ktmp) / (1.0 - ktmp);
  }
  *c = static_cast<T>(static_cast<double>(maxval) - ctmp * static_cast<double>(maxval) + 0.5);
  *m = static_cast<T>(static_cast<double>(maxval) - mtmp * static_cast<double>(maxval) + 0.5);
  *y = static_cast<T>(static_cast<double>(maxval) - ytmp * static_cast<double>(maxval) + 0.5);
  *k = static_cast<T>(static_cast<double>(maxval) - ktmp * static_cast<double>(maxval) + 0.5);
}

template <class T>
inline void cmyk_to_rgb(int maxval, T c, T m, T y, T k, T* r, T* g, T* b) {
  *r = static_cast<T>(static_cast<double>(c) * static_cast<double>(k) /
                          static_cast<double>(maxval) + 0.5);
  *g = static_cast<T>(static_cast<double>(m) * static_cast<double>(k) /
                          static_cast<double>(maxval) + 0.5);
  *b = static_cast<T>(static_cast<double>(y) * static_cast<double>(k) /
                          static_cast<double>(maxval) + 0.5);
}

// Rows x width sample buffer with row pointers.
template <class T>
class RowBuffer {
 public:
  void allocate(std::size_t samples_per_row, unsigned rows) {
    data_.assign(samples_per_row * rows + 16, T{});
    ptrs_.resize(rows);
    for (unsigned r = 0; r < rows; r++) ptrs_[r] = data_.data() + samples_per_row * r;
  }
  T** rows() { return ptrs_.data(); }
  T* operator[](unsigned r) { return ptrs_[r]; }
  unsigned height() const { return static_cast<unsigned>(ptrs_.size()); }

 private:
  std::vector<T> data_;
  std::vector<T*> ptrs_;
};

// Growable output byte buffer.
class OutBuffer {
 public:
  void put(std::uint8_t b) { buf_.push_back(b); }
  void put(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    buf_.insert(buf_.end(), b, b + n);
  }
  void put_str(const std::string& s) { put(s.data(), s.size()); }
  std::uint8_t* grow(std::size_t n) {
    std::size_t old = buf_.size();
    buf_.resize(old + n);
    return buf_.data() + old;
  }
  std::size_t size() const { return buf_.size(); }
  std::vector<std::uint8_t>& bytes() { return buf_; }
  std::vector<std::uint8_t> take() { return std::move(buf_); }

 private:
  std::vector<std::uint8_t> buf_;
};

// Sequential reader over an in-memory file image.
class InBuffer {
 public:
  InBuffer() = default;
  explicit InBuffer(std::span<const std::uint8_t> d) : data_(d) {}
  int getc() { return pos_ < data_.size() ? data_[pos_++] : -1; }
  bool read(void* dst, std::size_t n) {
    if (data_.size() - pos_ < n) {
      pos_ = data_.size();
      return false;
    }
    std::memcpy(dst, data_.data() + pos_, n);
    pos_ += n;
    return true;
  }
  // Returns a pointer to the next n bytes, or nullptr on EOF.
  const std::uint8_t* take(std::size_t n) {
    if (data_.size() - pos_ < n) {
      pos_ = data_.size();
      return nullptr;
    }
    const std::uint8_t* p = data_.data() + pos_;
    pos_ += n;
    return p;
  }
  std::size_t pos() const { return pos_; }
  void seek(std::size_t p) { pos_ = p < data_.size() ? p : data_.size(); }
  std::size_t remaining() const { return data_.size() - pos_; }
  std::span<const std::uint8_t> data() const { return data_; }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
};

}  // namespace detail

inline constexpr unsigned kMaxDimension = 65500;  // JPEG_MAX_DIMENSION

// Image-reader base.  Usage:
//   src.color = requested layout (ColorSpace::Unknown = use the file's own)
//   src.precision = desired sample precision
//   src.start(file_bytes);   // fills width/height/color/components
//   while (src.next_row < src.height) { unsigned n = src.read_rows(); use src.rows()[0..n) }
template <class T>
class ImageReader {
 public:
  virtual ~ImageReader() = default;

  // ---- parameters (set before start) ----
  ColorSpace color = ColorSpace::Unknown;  // in: requested; out: actual
  int precision = 8;                       // sample precision of the produced rows
  unsigned long long max_pixels = 0;       // refuse larger images when nonzero

  // ---- image description (valid after start) ----
  unsigned width = 0, height = 0;
  int components = 0;
  std::uint8_t density_unit = 0;  // 0 = none, 1 = dots/inch, 2 = dots/cm
  std::uint16_t x_density = 0, y_density = 0;
  unsigned next_row = 0;  // rows delivered so far (maintained by read_rows)
  Diagnostics diag;       // non-fatal problems in the input file

  void start(std::span<const std::uint8_t> file) {
    detail::check_precision<T>(precision);
    in_ = detail::InBuffer(file);
    next_row = 0;
    start_input();
  }

  // Reads the next group of rows; returns how many rows are in rows().
  unsigned read_rows() {
    unsigned n = get_rows();
    next_row += n;
    return n;
  }
  T** rows() { return buffer_; }
  unsigned buffer_height() const { return buffer_height_; }

  // Embedded ICC profile, if the format carries one.
  virtual std::vector<std::uint8_t> icc_profile() { return {}; }

 protected:
  virtual void start_input() = 0;
  virtual unsigned get_rows() = 0;

  void set_components_from_color() {
    if (detail::is_rgb_family(color)) components = detail::rgb_pixelsize[detail::idx(color)];
    else if (color == ColorSpace::Gray) components = 1;
    else if (color == ColorSpace::CMYK) components = 4;
  }
  void check_dimensions(unsigned long long w, unsigned long long h) {
    if (w > kMaxDimension || h > kMaxDimension)
      VIO_FAIL("Maximum supported image dimension is %u pixels", kMaxDimension);
    if (max_pixels && w * h > max_pixels)
      VIO_FAIL("Maximum supported image dimension is %u pixels",
               static_cast<unsigned>(max_pixels));
  }

  detail::InBuffer in_;
  T** buffer_ = nullptr;
  unsigned buffer_height_ = 1;
};

// Image-writer base.  Usage:
//   dst.width/height/color/components/precision (+ colormap) = ...
//   dst.set_icc_profile(...);   // optional, before start()
//   dst.start();
//   for each row: dst.write_rows(&row, 1);
//   std::vector<uint8_t> file = dst.finish();
template <class T>
class ImageWriter {
 public:
  virtual ~ImageWriter() = default;

  unsigned width = 0, height = 0;
  ColorSpace color = ColorSpace::RGB;  // layout of the supplied rows
  int components = 3;                  // out_color_components (1 for gray)
  int precision = 8;
  // When non-empty, rows hold colormap indexes; colormap[c][i] is component c
  // of entry i (c indexes the components of `color`).
  std::vector<std::vector<int>> colormap;
  std::uint8_t density_unit = 0;
  std::uint16_t x_density = 0, y_density = 0;

  bool quantized() const { return !colormap.empty(); }

  virtual void set_icc_profile(std::span<const std::uint8_t>) {}
  // Whether the format can be written with a width/height that changes after
  // construction (cropped decoding); BMP cannot.
  virtual bool supports_partial() const { return true; }

  void start() {
    detail::check_precision<T>(precision);
    if (width > kMaxDimension || height > kMaxDimension)
      VIO_FAIL("Maximum supported image dimension is %u pixels", kMaxDimension);
    start_output();
  }
  virtual void write_rows(T** rows, unsigned n) = 0;
  std::vector<std::uint8_t> finish() {
    finish_output();
    return out_.take();
  }

 protected:
  virtual void start_output() = 0;
  virtual void finish_output() {}
  detail::OutBuffer out_;
};

}  // namespace vio
