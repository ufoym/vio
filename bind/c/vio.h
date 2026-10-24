/* vio - C interface: decode and encode JPEG, PNG, BMP, GIF, PPM and Targa.
 *
 * Header-only.  The implementation is C++20: in exactly one C++ source file
 * of the program, define VIO_IMPLEMENTATION before including this header
 * (with include/ on the include path):
 *
 *   // vio.cpp
 *   #define VIO_IMPLEMENTATION
 *   #include "vio.h"
 *
 * Other files, C or C++, include it as usual.  Define VIO_API (for example
 * as __declspec(dllexport)) to give the functions other linkage.
 *
 * Images are exchanged in one layout: rows top-down, `channels` interleaved
 * samples per pixel (1 gray, 2 gray + alpha, 3 RGB, 4 RGBA) of `depth` bits
 * (8: uint8_t; 16: uint16_t in host byte order, 0..65535).
 *
 *   vio_image img;
 *   if (vio_load("in.png", 0, 0, &img) != VIO_OK) {
 *     fprintf(stderr, "%s\n", vio_last_error());
 *     return 1;
 *   }
 *   vio_options opt = {0};
 *   opt.quality = 85;
 *   vio_save("out.jpg", &img, &opt);
 *   vio_image_free(&img);
 *
 * Every function returning vio_status leaves a description of a failure in
 * vio_last_error() (per thread).  All functions are thread-safe.
 */
#ifndef VIO_H
#define VIO_H

#include <stddef.h>
#include <stdint.h>

#ifndef VIO_API
#define VIO_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VIO_VERSION_MAJOR 0
#define VIO_VERSION_MINOR 1
#define VIO_VERSION_PATCH 0

typedef enum vio_format {
  VIO_FORMAT_UNKNOWN = 0,
  VIO_FORMAT_JPEG,
  VIO_FORMAT_PNG,
  VIO_FORMAT_BMP,
  VIO_FORMAT_GIF, /* decoding only (the first image) */
  VIO_FORMAT_PPM, /* PPM/PGM, binary or text */
  VIO_FORMAT_TGA
} vio_format;

typedef enum vio_status {
  VIO_OK = 0,
  VIO_ERROR_ARGUMENT,       /* invalid argument */
  VIO_ERROR_FORMAT,         /* unrecognized file format */
  VIO_ERROR_UNSUPPORTED,    /* the format cannot hold this image */
  VIO_ERROR_DECODE,         /* corrupt or unsupported data */
  VIO_ERROR_ENCODE,
  VIO_ERROR_IO,             /* a file could not be read or written */
  VIO_ERROR_MEMORY
} vio_status;

typedef struct vio_image {
  void* data;        /* width x height pixels, rows `stride` bytes apart */
  uint32_t width;
  uint32_t height;
  uint32_t channels; /* 1 gray, 2 gray + alpha, 3 RGB, 4 RGBA */
  uint32_t depth;    /* bits per sample: 8 or 16 */
  size_t stride;     /* bytes per row; as input, 0 means packed */
} vio_image;

typedef struct vio_info {
  vio_format format;
  uint32_t width;
  uint32_t height;
  uint32_t channels; /* what vio_decode() gives by default */
  uint32_t depth;
} vio_info;

/* Encoding options.  Zero-initialize, then set what is needed: zero fields
 * take the defaults. */
typedef struct vio_options {
  int quality;     /* JPEG: 1..100 (default 90) */
  int subsampling; /* JPEG chroma: 444, 422 or 420 (default 420) */
  int progressive; /* JPEG: nonzero for progressive */
  int compression; /* PNG: 0 = fast single-pass (default), 1..9 = zlib level */
} vio_options;

/* Library version, "major.minor.patch". */
VIO_API const char* vio_version(void);

/* The last error on this thread ("" if none). */
VIO_API const char* vio_last_error(void);

/* The format of a file from its first bytes, or of a path from its
 * extension (.jpg .jpeg .png .bmp .gif .ppm .pgm .pnm .tga). */
VIO_API vio_format vio_detect(const void* data, size_t size);
VIO_API vio_format vio_format_from_path(const char* path);
VIO_API const char* vio_format_name(vio_format format);

/* Reads the header only. */
VIO_API vio_status vio_probe(const void* data, size_t size, vio_info* info);

/* Decodes an image.  channels (1..4) and depth (8 or 16) select the output
 * layout; 0 keeps the file's own.  Missing alpha is opaque; removed alpha
 * is dropped; gray from color is 0.299 R + 0.587 G + 0.114 B.  The pixels
 * are released with vio_image_free(). */
VIO_API vio_status vio_decode(const void* data, size_t size, uint32_t channels, uint32_t depth,
                              vio_image* image);
VIO_API vio_status vio_load(const char* path, uint32_t channels, uint32_t depth, vio_image* image);

/* Encodes an image (options may be NULL).  The file is returned in *out,
 * *out_size bytes, released with vio_free().  JPEG, BMP and Targa drop
 * alpha; JPEG, BMP and Targa take 16-bit samples as 8-bit. */
VIO_API vio_status vio_encode(const vio_image* image, vio_format format, const vio_options* options,
                              void** out, size_t* out_size);
/* Writes a file in the format of the path's extension. */
VIO_API vio_status vio_save(const char* path, const vio_image* image, const vio_options* options);

VIO_API void vio_image_free(vio_image* image);
VIO_API void vio_free(void* p);

#ifdef __cplusplus
}
#endif

#endif /* VIO_H */

/* ---- implementation ---------------------------------------------------- */

#if defined(VIO_IMPLEMENTATION) && !defined(VIO_IMPLEMENTATION_DONE)
#define VIO_IMPLEMENTATION_DONE
#ifndef __cplusplus
#error "VIO_IMPLEMENTATION must be defined in a C++ (C++20) source file"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "vio/bmp.hpp"
#include "vio/gif.hpp"
#include "vio/jpeg.hpp"
#include "vio/png.hpp"
#include "vio/ppm.hpp"
#include "vio/targa.hpp"

// Defined in a namespace so that the including file sees none of the helpers.
namespace vio::c_impl {
namespace {

using std::size_t;
using std::uint16_t;
using std::uint32_t;
using std::uint8_t;
using Bytes = std::span<const uint8_t>;

thread_local std::string g_error;

// A failure with its status code.
struct Failure {
  vio_status status;
  std::string message;
};
[[noreturn]] void fail(vio_status s, std::string msg) { throw Failure{s, std::move(msg)}; }

// Runs f, turning exceptions into a status (default_status for codec errors).
template <class F>
vio_status guard(vio_status default_status, F&& f) {
  try {
    f();
    g_error.clear();
    return VIO_OK;
  } catch (const Failure& e) {
    g_error = e.message;
    return e.status;
  } catch (const std::bad_alloc&) {
    g_error = "out of memory";
    return VIO_ERROR_MEMORY;
  } catch (const std::exception& e) {
    g_error = e.what();
    return default_status;
  } catch (...) {
    g_error = "unknown error";
    return default_status;
  }
}

// malloc'ed pixel buffer, released unless handed over.
struct Buffer {
  uint8_t* p = nullptr;
  Buffer() = default;
  explicit Buffer(size_t n) : p(static_cast<uint8_t*>(std::malloc(n ? n : 1))) {
    if (!p) throw std::bad_alloc();
  }
  Buffer(Buffer&& o) noexcept : p(o.p) { o.p = nullptr; }
  Buffer& operator=(Buffer&& o) noexcept {
    std::swap(p, o.p);
    return *this;
  }
  ~Buffer() { std::free(p); }
  uint8_t* release() {
    uint8_t* r = p;
    p = nullptr;
    return r;
  }
};

// A packed image in the interface's layout.
struct Image {
  Buffer data;
  uint32_t width = 0, height = 0, channels = 0, depth = 8;
  size_t stride() const { return size_t{width} * channels * (depth / 8); }
};

size_t checked_size(uint32_t w, uint32_t h, uint32_t ch, uint32_t bytes) {
  const unsigned long long n = 1ull * w * h * ch * bytes;
  if (n > static_cast<size_t>(-1) / 2) throw std::bad_alloc();
  return static_cast<size_t>(n);
}

// ---- sample conversion -----------------------------------------------------

inline uint32_t to_depth(uint32_t v, uint32_t from_max, uint32_t to_max) {
  if (from_max == to_max) return v;
  return (v * 1ull * to_max + from_max / 2) / from_max;
}

template <class S, class D>
void convert_rows(const uint8_t* src, size_t src_stride, uint32_t src_ch, uint32_t src_max, uint8_t* dst,
                  size_t dst_stride, uint32_t dst_ch, uint32_t dst_max, uint32_t w, uint32_t h) {
  const bool src_color = src_ch >= 3, src_alpha = src_ch == 2 || src_ch == 4;
  const bool dst_color = dst_ch >= 3, dst_alpha = dst_ch == 2 || dst_ch == 4;
  for (uint32_t y = 0; y < h; y++) {
    const S* s = reinterpret_cast<const S*>(src + y * src_stride);
    D* d = reinterpret_cast<D*>(dst + y * dst_stride);
    for (uint32_t x = 0; x < w; x++, s += src_ch, d += dst_ch) {
      uint32_t r = s[0], g = r, b = r;
      if (src_color) {
        g = s[1];
        b = s[2];
      }
      const uint32_t a = src_alpha ? s[src_ch - 1] : src_max;
      if (dst_color) {
        d[0] = static_cast<D>(to_depth(r, src_max, dst_max));
        d[1] = static_cast<D>(to_depth(g, src_max, dst_max));
        d[2] = static_cast<D>(to_depth(b, src_max, dst_max));
      } else {
        const uint32_t v = src_color ? static_cast<uint32_t>((299ull * r + 587ull * g + 114ull * b + 500) / 1000) : r;
        d[0] = static_cast<D>(to_depth(v, src_max, dst_max));
      }
      if (dst_alpha) d[dst_ch - 1] = static_cast<D>(to_depth(a, src_max, dst_max));
    }
  }
}

// Converts rows of samples with maximum src_max (in 8- or 16-bit containers)
// to dst_ch channels of dst_depth bits.
void convert(const uint8_t* src, size_t src_stride, uint32_t src_ch, uint32_t src_bytes, uint32_t src_max,
             uint8_t* dst, size_t dst_stride, uint32_t dst_ch, uint32_t dst_depth, uint32_t w, uint32_t h) {
  const uint32_t dst_max = dst_depth == 16 ? 65535 : 255;
  if (src_bytes == 1 && dst_depth == 8)
    convert_rows<uint8_t, uint8_t>(src, src_stride, src_ch, src_max, dst, dst_stride, dst_ch, dst_max, w, h);
  else if (src_bytes == 1)
    convert_rows<uint8_t, uint16_t>(src, src_stride, src_ch, src_max, dst, dst_stride, dst_ch, dst_max, w, h);
  else if (dst_depth == 8)
    convert_rows<uint16_t, uint8_t>(src, src_stride, src_ch, src_max, dst, dst_stride, dst_ch, dst_max, w, h);
  else
    convert_rows<uint16_t, uint16_t>(src, src_stride, src_ch, src_max, dst, dst_stride, dst_ch, dst_max, w, h);
}

// Converts img in place to the requested layout (0 = keep).
void conform(Image& img, uint32_t channels, uint32_t depth, uint32_t sample_max = 0) {
  const uint32_t ch = channels ? channels : img.channels;
  const uint32_t dp = depth ? depth : img.depth;
  const uint32_t natural_max = img.depth == 16 ? 65535 : 255;
  if (!sample_max) sample_max = natural_max;
  if (ch == img.channels && dp == img.depth && sample_max == natural_max) return;
  Image out;
  out.width = img.width;
  out.height = img.height;
  out.channels = ch;
  out.depth = dp;
  out.data = Buffer(checked_size(out.width, out.height, ch, dp / 8));
  convert(img.data.p, img.stride(), img.channels, img.depth / 8, sample_max, out.data.p, out.stride(), ch, dp,
          img.width, img.height);
  img = std::move(out);
}

// ---- format detection ------------------------------------------------------

bool looks_like_tga(const uint8_t* p, size_t n) {
  if (n < 18) return false;
  const int cmaptype = p[1], type = p[2], bits = p[16];
  const bool type_ok = type == 1 || type == 2 || type == 3 || type == 9 || type == 10 || type == 11;
  const bool bits_ok = bits == 8 || bits == 15 || bits == 16 || bits == 24 || bits == 32;
  const unsigned w = p[12] | p[13] << 8, h = p[14] | p[15] << 8;
  return cmaptype <= 1 && type_ok && bits_ok && w && h && (p[17] & 0xC0) == 0;
}

vio_format detect(const uint8_t* p, size_t n) {
  if (!p) return VIO_FORMAT_UNKNOWN;
  if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return VIO_FORMAT_JPEG;
  if (n >= 8 && std::memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0) return VIO_FORMAT_PNG;
  if (n >= 4 && std::memcmp(p, "GIF8", 4) == 0) return VIO_FORMAT_GIF;
  if (n >= 2 && p[0] == 'B' && p[1] == 'M') return VIO_FORMAT_BMP;
  if (n >= 2 && p[0] == 'P' && (p[1] == '2' || p[1] == '3' || p[1] == '5' || p[1] == '6')) return VIO_FORMAT_PPM;
  if (looks_like_tga(p, n)) return VIO_FORMAT_TGA;
  return VIO_FORMAT_UNKNOWN;
}

vio_format require_format(Bytes file) {
  const vio_format f = detect(file.data(), file.size());
  if (f == VIO_FORMAT_UNKNOWN) fail(VIO_ERROR_FORMAT, "unrecognized image format");
  return f;
}

// Maximum value in a PPM/PGM header (0 if it cannot be parsed).
unsigned ppm_maxval(Bytes f) {
  size_t i = 2;
  unsigned v[3] = {};
  for (int k = 0; k < 3; k++) {
    for (;;) {
      if (i >= f.size()) return 0;
      if (f[i] == '#') {
        while (i < f.size() && f[i] != '\n') i++;
      } else if (f[i] == ' ' || f[i] == '\t' || f[i] == '\n' || f[i] == '\r') {
        i++;
      } else {
        break;
      }
    }
    if (f[i] < '0' || f[i] > '9') return 0;
    while (i < f.size() && f[i] >= '0' && f[i] <= '9' && v[k] < 100000) v[k] = v[k] * 10 + (f[i++] - '0');
  }
  return v[2];
}

// ---- decoding --------------------------------------------------------------

template <class T>
void read_jpeg_rows(vio::jpeg::Decoder& dec, uint8_t* data, size_t stride) {
  std::vector<T*> rows(dec.output_height());
  for (size_t y = 0; y < rows.size(); y++) rows[y] = reinterpret_cast<T*>(data + y * stride);
  while (dec.output_scanline() < dec.output_height())
    dec.read_scanlines<T>(rows.data() + dec.output_scanline(), dec.output_height() - dec.output_scanline());
}

void decode_jpeg(Bytes file, uint32_t want_ch, Image& img, uint32_t& sample_max) {
  using vio::ColorSpace;
  vio::jpeg::Decoder dec(file);
  const vio::jpeg::Header h = dec.header();
  ColorSpace cs = h.num_components == 1 ? ColorSpace::Gray
                  : (h.color_space == ColorSpace::CMYK || h.color_space == ColorSpace::YCCK) ? ColorSpace::CMYK
                                                                                               : ColorSpace::RGB;
  // Let the decoder produce the requested layout where it can.
  if (cs == ColorSpace::RGB && !h.lossless) {
    if (want_ch == 4) cs = ColorSpace::ExtRGBA;
    else if ((want_ch == 1 || want_ch == 2) && h.color_space == ColorSpace::YCbCr) cs = ColorSpace::Gray;
  }
  dec.set_output_color(cs);
  dec.start();
  const int p = dec.output_precision();
  const uint32_t ch = static_cast<uint32_t>(dec.output_components());
  const uint32_t bytes = p <= 8 ? 1 : 2;
  img.width = dec.output_width();
  img.height = dec.output_height();
  img.channels = ch;
  img.depth = bytes * 8;
  img.data = Buffer(checked_size(img.width, img.height, ch, bytes));
  if (p <= 8) read_jpeg_rows<uint8_t>(dec, img.data.p, img.stride());
  else if (p <= 12) read_jpeg_rows<std::int16_t>(dec, img.data.p, img.stride());
  else read_jpeg_rows<uint16_t>(dec, img.data.p, img.stride());
  dec.finish();
  sample_max = (1u << p) - 1;
  if (cs == ColorSpace::CMYK) {
    // Inverted (Adobe) CMYK to RGB.
    const size_t n = size_t{img.width} * img.height;
    Image rgb;
    rgb.width = img.width;
    rgb.height = img.height;
    rgb.channels = 3;
    rgb.depth = img.depth;
    rgb.data = Buffer(checked_size(img.width, img.height, 3, bytes));
    auto run = [&](auto t) {
      using T = decltype(t);
      const T* s = reinterpret_cast<const T*>(img.data.p);
      T* d = reinterpret_cast<T*>(rgb.data.p);
      for (size_t i = 0; i < n; i++, s += 4, d += 3)
        vio::detail::cmyk_to_rgb<T>(static_cast<int>(sample_max), s[0], s[1], s[2], s[3], d, d + 1, d + 2);
    };
    if (bytes == 1) run(uint8_t{});
    else run(uint16_t{});
    img = std::move(rgb);
  }
}

void decode_png(Bytes file, Image& img) {
  namespace png = vio::png;
  png::Decoder d;
  d.decode(file);
  const png::Header& h = d.header;
  img.width = h.width;
  img.height = h.height;
  const bool direct = h.color_type != png::kIndexed && h.bit_depth >= 8 && !d.info.has_trns();
  if (direct) {
    img.channels = png::num_channels(h.color_type);
    img.depth = h.bit_depth;
    img.data = Buffer(checked_size(img.width, img.height, img.channels, img.depth / 8));
    const size_t stride = img.stride();
    for (uint32_t y = 0; y < img.height; y++) std::memcpy(img.data.p + y * stride, d.row(y), stride);
    return;
  }
  png::Transforms t;
  t.expand_palette = true;
  t.expand_gray = true;
  t.trns_to_alpha = true;
  const png::Decoder::Image out = d.transform(t);
  img.channels = out.format.channels;
  img.depth = out.format.bit_depth;
  img.data = Buffer(checked_size(img.width, img.height, img.channels, img.depth / 8));
  const size_t stride = img.stride();
  for (uint32_t y = 0; y < img.height; y++) std::memcpy(img.data.p + y * stride, out.row(y), stride);
}

template <class T>
void decode_rows(vio::ImageReader<T>& r, int precision, Bytes file, Image& img) {
  r.color = vio::ColorSpace::RGB;
  r.precision = precision;
  r.start(file);
  img.width = r.width;
  img.height = r.height;
  img.channels = static_cast<uint32_t>(r.components);
  img.depth = sizeof(T) * 8;
  img.data = Buffer(checked_size(img.width, img.height, img.channels, sizeof(T)));
  const size_t stride = img.stride();
  while (r.next_row < r.height) {
    const unsigned y0 = r.next_row;
    const unsigned n = r.read_rows();
    for (unsigned i = 0; i < n && y0 + i < img.height; i++) std::memcpy(img.data.p + (y0 + i) * stride, r.rows()[i], stride);
  }
}

void decode(Bytes file, uint32_t channels, uint32_t depth, Image& img) {
  if (channels > 4 || (depth != 0 && depth != 8 && depth != 16))
    fail(VIO_ERROR_ARGUMENT, "channels must be 0..4 and depth 0, 8 or 16");
  uint32_t sample_max = 0;
  switch (require_format(file)) {
    case VIO_FORMAT_JPEG: decode_jpeg(file, channels, img, sample_max); break;
    case VIO_FORMAT_PNG: decode_png(file, img); break;
    case VIO_FORMAT_BMP: {
      vio::bmp::Reader r;
      decode_rows(r, 8, file, img);
      break;
    }
    case VIO_FORMAT_GIF: {
      vio::gif::Reader r;
      decode_rows(r, 8, file, img);
      break;
    }
    case VIO_FORMAT_PPM: {
      if (ppm_maxval(file) > 255) {
        vio::ppm::Reader<uint16_t> r;
        decode_rows(r, 16, file, img);
      } else {
        vio::ppm::Reader<uint8_t> r;
        decode_rows(r, 8, file, img);
      }
      break;
    }
    case VIO_FORMAT_TGA: {
      vio::targa::Reader r;
      decode_rows(r, 8, file, img);
      break;
    }
    default: fail(VIO_ERROR_FORMAT, "unrecognized image format");
  }
  // Samples of other precisions are widened to the container's full range.
  if (sample_max && sample_max != 255 && sample_max != 65535 && !depth) depth = sample_max > 255 ? 16 : 8;
  conform(img, channels, depth, sample_max);
}

void probe(Bytes file, vio_info& info) {
  info = vio_info{};
  info.format = require_format(file);
  switch (info.format) {
    case VIO_FORMAT_JPEG: {
      vio::jpeg::Decoder dec(file);
      const vio::jpeg::Header h = dec.header();
      info.width = h.width;
      info.height = h.height;
      info.channels = h.num_components == 1 ? 1 : 3;
      info.depth = h.precision <= 8 ? 8 : 16;
      return;
    }
    case VIO_FORMAT_PNG: {
      namespace png = vio::png;
      png::Decoder d;
      d.read_header(file);
      // tRNS adds alpha; the chunks before the image data may be cut off.
      bool trns = false;
      try {
        d.read_info(file);
        trns = d.info.has_trns();
      } catch (const std::exception&) {
      }
      const png::Header& h = d.header;
      info.width = h.width;
      info.height = h.height;
      uint32_t ch = h.color_type == png::kIndexed ? 3 : png::num_channels(h.color_type);
      if (trns && (ch == 1 || ch == 3)) ch++;
      info.channels = ch;
      info.depth = h.bit_depth == 16 ? 16 : 8;
      return;
    }
    default: {
      // The other formats are small to decode; their headers vary widely.
      Image img;
      decode(file, 0, 0, img);
      info.width = img.width;
      info.height = img.height;
      info.channels = img.channels;
      info.depth = img.depth;
      return;
    }
  }
}

// ---- encoding --------------------------------------------------------------

// The pixels of a vio_image as rows, converted when the target needs another
// layout.
struct Source {
  const uint8_t* data = nullptr;
  size_t stride = 0;
  uint32_t width = 0, height = 0, channels = 0, depth = 8;
  Image converted;

  Source(const vio_image& im) {
    if (!im.data || im.width == 0 || im.height == 0) fail(VIO_ERROR_ARGUMENT, "empty image");
    if (im.channels < 1 || im.channels > 4) fail(VIO_ERROR_ARGUMENT, "channels must be 1..4");
    if (im.depth != 8 && im.depth != 16) fail(VIO_ERROR_ARGUMENT, "depth must be 8 or 16");
    data = static_cast<const uint8_t*>(im.data);
    width = im.width;
    height = im.height;
    channels = im.channels;
    depth = im.depth;
    const size_t packed = size_t{width} * channels * (depth / 8);
    stride = im.stride ? im.stride : packed;
    if (stride < packed) fail(VIO_ERROR_ARGUMENT, "stride too small");
  }
  // Converts to ch channels (0 = keep) of dp bits (0 = keep).
  void to(uint32_t ch, uint32_t dp) {
    if (!ch) ch = channels;
    if (!dp) dp = depth;
    if (ch == channels && dp == depth) return;
    converted.width = width;
    converted.height = height;
    converted.channels = ch;
    converted.depth = dp;
    converted.data = Buffer(checked_size(width, height, ch, dp / 8));
    convert(data, stride, channels, depth / 8, depth == 16 ? 65535 : 255, converted.data.p, converted.stride(), ch,
            dp, width, height);
    data = converted.data.p;
    stride = converted.stride();
    channels = ch;
    depth = dp;
  }
  template <class T>
  std::vector<T*> rows() const {
    std::vector<T*> r(height);
    for (uint32_t y = 0; y < height; y++) r[y] = const_cast<T*>(reinterpret_cast<const T*>(data + y * stride));
    return r;
  }
};

std::vector<uint8_t> encode_jpeg(Source& s, const vio_options& o) {
  using vio::ColorSpace;
  s.to(s.channels == 2 ? 1 : 0, 8);
  const ColorSpace cs = s.channels == 1 ? ColorSpace::Gray : s.channels == 3 ? ColorSpace::RGB : ColorSpace::ExtRGBA;
  const int quality = o.quality ? o.quality : 90;
  if (quality < 1 || quality > 100) fail(VIO_ERROR_ARGUMENT, "quality must be 1..100");
  vio::jpeg::Encoder enc;
  enc.set_image(s.width, s.height, static_cast<int>(s.channels), cs).set_quality(quality);
  if (s.channels != 1) {
    switch (o.subsampling) {
      case 0: case 420: break;
      case 422: enc.set_sampling(0, 2, 1); break;
      case 444: enc.set_sampling(0, 1, 1); break;
      default: fail(VIO_ERROR_ARGUMENT, "subsampling must be 444, 422 or 420");
    }
  }
  if (o.progressive) enc.set_progressive();
  enc.start();
  std::vector<uint8_t*> rows = s.rows<uint8_t>();
  enc.write_scanlines<uint8_t>(rows.data(), s.height);
  return enc.finish();
}

std::vector<uint8_t> encode_png(Source& s, const vio_options& o) {
  namespace png = vio::png;
  if (o.compression < 0 || o.compression > 9) fail(VIO_ERROR_ARGUMENT, "compression must be 0..9");
  png::Encoder e;
  e.header.width = s.width;
  e.header.height = s.height;
  e.header.bit_depth = static_cast<uint8_t>(s.depth);
  static constexpr uint8_t kType[5] = {0, png::kGray, png::kGrayAlpha, png::kTruecolor, png::kTruecolorAlpha};
  e.header.color_type = kType[s.channels];
  if (o.compression) {
    e.mode = png::Encoder::Mode::kZlib;
    e.options.level = o.compression;
  }
  return e.encode(s.data, s.stride);
}

template <class T, class W>
std::vector<uint8_t> encode_rows(Source& s, W& w) {
  s.to(s.channels >= 3 ? 3 : 1, sizeof(T) * 8);
  w.width = s.width;
  w.height = s.height;
  w.color = s.channels == 1 ? vio::ColorSpace::Gray : vio::ColorSpace::RGB;
  w.components = static_cast<int>(s.channels);
  w.precision = sizeof(T) * 8;
  w.start();
  std::vector<T*> rows = s.template rows<T>();
  w.write_rows(rows.data(), s.height);
  return w.finish();
}

std::vector<uint8_t> encode(const vio_image& image, vio_format format, const vio_options* options) {
  const vio_options o = options ? *options : vio_options{};
  Source s(image);
  switch (format) {
    case VIO_FORMAT_JPEG: return encode_jpeg(s, o);
    case VIO_FORMAT_PNG: return encode_png(s, o);
    case VIO_FORMAT_BMP: {
      vio::bmp::Writer w;
      return encode_rows<uint8_t>(s, w);
    }
    case VIO_FORMAT_TGA: {
      vio::targa::Writer w;
      return encode_rows<uint8_t>(s, w);
    }
    case VIO_FORMAT_PPM: {
      if (s.depth == 16) {
        vio::ppm::Writer<uint16_t> w;
        return encode_rows<uint16_t>(s, w);
      }
      vio::ppm::Writer<uint8_t> w;
      return encode_rows<uint8_t>(s, w);
    }
    case VIO_FORMAT_GIF: fail(VIO_ERROR_UNSUPPORTED, "GIF encoding is not supported");
    default: fail(VIO_ERROR_ARGUMENT, "unknown output format");
  }
}

// ---- files -----------------------------------------------------------------

std::vector<uint8_t> read_file(const char* path) {
  if (!path) fail(VIO_ERROR_ARGUMENT, "no path");
  std::FILE* f = std::fopen(path, "rb");
  if (!f) fail(VIO_ERROR_IO, std::string("cannot open ") + path);
  std::vector<uint8_t> data;
  uint8_t buf[1 << 16];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
  const bool bad = std::ferror(f) != 0;
  std::fclose(f);
  if (bad) fail(VIO_ERROR_IO, std::string("cannot read ") + path);
  return data;
}

void write_file(const char* path, const std::vector<uint8_t>& data) {
  std::FILE* f = std::fopen(path, "wb");
  if (!f) fail(VIO_ERROR_IO, std::string("cannot create ") + path);
  const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  if (std::fclose(f) != 0 || !ok) fail(VIO_ERROR_IO, std::string("cannot write ") + path);
}

void hand_over(Image& img, vio_image* out) {
  out->width = img.width;
  out->height = img.height;
  out->channels = img.channels;
  out->depth = img.depth;
  out->stride = img.stride();
  out->data = img.data.release();
}

}  // namespace

extern "C" {

const char* vio_version(void) { return "0.1.0"; }

const char* vio_last_error(void) { return g_error.c_str(); }

vio_format vio_detect(const void* data, size_t size) { return detect(static_cast<const uint8_t*>(data), size); }

vio_format vio_format_from_path(const char* path) {
  if (!path) return VIO_FORMAT_UNKNOWN;
  const char* dot = std::strrchr(path, '.');
  if (!dot) return VIO_FORMAT_UNKNOWN;
  std::string ext(dot + 1);
  for (char& c : ext) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  if (ext == "jpg" || ext == "jpeg" || ext == "jpe" || ext == "jfif") return VIO_FORMAT_JPEG;
  if (ext == "png" || ext == "apng") return VIO_FORMAT_PNG;
  if (ext == "bmp" || ext == "dib") return VIO_FORMAT_BMP;
  if (ext == "gif") return VIO_FORMAT_GIF;
  if (ext == "ppm" || ext == "pgm" || ext == "pnm") return VIO_FORMAT_PPM;
  if (ext == "tga" || ext == "targa") return VIO_FORMAT_TGA;
  return VIO_FORMAT_UNKNOWN;
}

const char* vio_format_name(vio_format format) {
  switch (format) {
    case VIO_FORMAT_JPEG: return "jpeg";
    case VIO_FORMAT_PNG: return "png";
    case VIO_FORMAT_BMP: return "bmp";
    case VIO_FORMAT_GIF: return "gif";
    case VIO_FORMAT_PPM: return "ppm";
    case VIO_FORMAT_TGA: return "tga";
    default: return "unknown";
  }
}

vio_status vio_probe(const void* data, size_t size, vio_info* info) {
  return guard(VIO_ERROR_DECODE, [&] {
    if (!info || (!data && size)) fail(VIO_ERROR_ARGUMENT, "null argument");
    probe(Bytes(static_cast<const uint8_t*>(data), size), *info);
  });
}

vio_status vio_decode(const void* data, size_t size, uint32_t channels, uint32_t depth, vio_image* image) {
  return guard(VIO_ERROR_DECODE, [&] {
    if (!image || (!data && size)) fail(VIO_ERROR_ARGUMENT, "null argument");
    *image = vio_image{};
    Image img;
    decode(Bytes(static_cast<const uint8_t*>(data), size), channels, depth, img);
    hand_over(img, image);
  });
}

vio_status vio_load(const char* path, uint32_t channels, uint32_t depth, vio_image* image) {
  return guard(VIO_ERROR_DECODE, [&] {
    if (!image) fail(VIO_ERROR_ARGUMENT, "null argument");
    *image = vio_image{};
    const std::vector<uint8_t> file = read_file(path);
    Image img;
    decode(file, channels, depth, img);
    hand_over(img, image);
  });
}

vio_status vio_encode(const vio_image* image, vio_format format, const vio_options* options, void** out,
                      size_t* out_size) {
  return guard(VIO_ERROR_ENCODE, [&] {
    if (!image || !out || !out_size) fail(VIO_ERROR_ARGUMENT, "null argument");
    *out = nullptr;
    *out_size = 0;
    const std::vector<uint8_t> file = encode(*image, format, options);
    Buffer b(file.size());
    std::memcpy(b.p, file.data(), file.size());
    *out_size = file.size();
    *out = b.release();
  });
}

vio_status vio_save(const char* path, const vio_image* image, const vio_options* options) {
  return guard(VIO_ERROR_ENCODE, [&] {
    if (!image || !path) fail(VIO_ERROR_ARGUMENT, "null argument");
    const vio_format f = vio_format_from_path(path);
    if (f == VIO_FORMAT_UNKNOWN) fail(VIO_ERROR_FORMAT, std::string("unknown file extension: ") + path);
    write_file(path, encode(*image, f, options));
  });
}

void vio_image_free(vio_image* image) {
  if (!image) return;
  std::free(image->data);
  *image = vio_image{};
}

void vio_free(void* p) { std::free(p); }

}  // extern "C"
}  // namespace vio::c_impl

#endif  // VIO_IMPLEMENTATION
