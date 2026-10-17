// vio::png - PNG encoder.
//
// png::Encoder writes the image and every chunk in png::Info (png/types.hpp),
// interlaced or not, and APNG animations (frames added after the default
// image).  Image data is compressed in one of two modes:
//   kFast (default)  single-pass SIMD encoder (png/detail/fast_encoder.hpp)
//   kZlib            per-scanline filter selection and zlib compression at
//                    a chosen level and strategy; with the default options,
//                    files byte-identical to libspng 0.7's
// Compressed text and ICC profiles use zlib at WriteOptions::text_level.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "detail/common.hpp"
#include "detail/deflate.hpp"
#include "detail/fast_encoder.hpp"
#include "detail/filter.hpp"
#include "transform.hpp"
#include "types.hpp"

namespace vio::png {

// Filter types allowed when writing (bit f for filter type f).
enum FilterBits : unsigned {
  kFilterBitNone = 1u << 0,
  kFilterBitSub = 1u << 1,
  kFilterBitUp = 1u << 2,
  kFilterBitAvg = 1u << 3,
  kFilterBitPaeth = 1u << 4,
  kFilterBitsAll = 0x1f,
};

struct WriteOptions {
  int level = -1;     // zlib level 0..9 in kZlib mode (-1 = 6)
  int strategy = -1;  // zlib strategy (-1: filtered if rows are filtered)
  int window_bits = 15, mem_level = 8;
  // Allowed filter types; 0 = all of them for images of 8 or more bits per
  // sample without a palette, else type 0 only.
  unsigned filters = 0;
  std::size_t idat_size = 8192;  // image data bytes per chunk (kZlib mode)
  int text_level = -1;           // zTXt, compressed iTXt and iCCP
};

namespace detail {

inline void put_u16(std::vector<std::uint8_t>& v, unsigned x) {
  v.push_back(static_cast<std::uint8_t>(x >> 8));
  v.push_back(static_cast<std::uint8_t>(x));
}
inline void put_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<std::uint8_t>(x >> s));
}
inline void put_str(std::vector<std::uint8_t>& v, const std::string& s, bool nul = true) {
  v.insert(v.end(), s.begin(), s.end());
  if (nul) v.push_back(0);
}

// Keywords: 1-79 printable Latin-1 characters, no leading, trailing or
// consecutive spaces.
inline void check_keyword(const std::string& k, const char* chunk) {
  bool ok = !k.empty() && k.size() <= 79 && k.front() != ' ' && k.back() != ' ';
  for (std::size_t i = 0; ok && i < k.size(); i++) {
    unsigned char c = static_cast<unsigned char>(k[i]);
    if (c < 32 || (c > 126 && c < 161) || (c == ' ' && i > 0 && k[i - 1] == ' ')) ok = false;
  }
  if (!ok) VIO_FAIL("%s: invalid keyword", chunk);
}

}  // namespace detail

class Encoder {
 public:
  enum class Mode { kFast, kZlib };

  Info info;
  Mode mode = Mode::kFast;
  WriteOptions options;
  // How the rows given differ from native layout (none by default).
  WriteTransforms transforms;
  // Old names, kept as references into info.
  Header& header = info.header;
  std::vector<PaletteEntry>& palette = info.palette;

  Encoder() = default;
  Encoder(const Encoder&) = delete;
  Encoder& operator=(const Encoder&) = delete;

  // Bytes per row in native layout.
  std::size_t row_bytes() const { return row_bytes_for(header.width); }
  // Bytes per row as given (with `transforms`).
  std::size_t input_row_bytes() const {
    if (!transforms.any()) return row_bytes();
    detail::WritePlan plan;
    plan.init(header, transforms);
    return plan.input_row_bytes(header.width);
  }

  // Encodes a whole image: rows of row_bytes() bytes in native layout
  // (16-bit samples in host byte order), `stride` bytes apart.
  std::vector<std::uint8_t> encode(const void* pixels, std::size_t stride) {
    begin();
    write_image(pixels, stride);
    return finish();
  }

  // Writes the signature and the chunks that precede the image data.
  void begin() {
    validate();
    out_ = detail::ByteBuffer();
    out_.reserve(1024);
    bpp_ = header.bit_depth < 8
               ? 1u
               : static_cast<unsigned>(num_channels(header.color_type) * (header.bit_depth / 8));
    row_bytes_ = row_bytes();
    filters_ = options.filters                                               ? options.filters
               : (header.color_type == kIndexed || header.bit_depth < 8) ? kFilterBitNone
                                                                           : kFilterBitsAll;
    rows_done_ = 0;
    seq_ = 0;
    image_done_ = false;
    image_.reset();
    streaming_ = false;
    write_header_chunks();
    begun_ = true;
  }

  // Writes the image data of the default image.
  void write_image(const void* pixels, std::size_t stride) {
    if (!begun_) begin();
    std::vector<std::uint8_t> tmp;
    const std::uint8_t* p = convert(static_cast<const std::uint8_t*>(pixels), stride, header.width, header.height, tmp);
    write_data(p, stride, header.width, header.height, false);
    rows_done_ = header.height;
    image_done_ = true;
  }

  // Alternatively, one row at a time (native layout, 16-bit samples in host
  // byte order).  Non-interlaced images in kZlib mode are compressed as the
  // rows come; otherwise the rows are kept until the last one.
  void write_row(const void* row) {
    if (!begun_) begin();
    if (rows_done_ >= header.height) return;
    if (transforms.any()) {
      std::size_t stride = 0;
      row = convert(static_cast<const std::uint8_t*>(row), stride, header.width, 1, row_tmp_);
    }
    if (mode == Mode::kZlib && !header.interlace) {
      if (!streaming_) start_row_stream();
      stream_row(static_cast<const std::uint8_t*>(row));
      if (++rows_done_ == header.height) {
        finish_row_stream();
        image_done_ = true;
      }
      return;
    }
    if (!image_) image_.reset(new std::uint8_t[row_bytes_ * header.height]);
    std::memcpy(image_.get() + rows_done_ * row_bytes_, row, row_bytes_);
    if (++rows_done_ == header.height) {
      write_data(image_.get(), row_bytes_, header.width, header.height, false);
      image_.reset();
      image_done_ = true;
    }
  }

  // APNG: writes a frame (fcTL and fdAT chunks) after the default image.
  void add_frame(const FrameControl& fc, const void* pixels, std::size_t stride) {
    if (!image_done_) fail("add_frame() before the default image");
    if (!info.animation) fail("add_frame() without Info::animation");
    check_frame(fc);
    write_fctl(fc);
    std::vector<std::uint8_t> tmp;
    const std::uint8_t* p = convert(static_cast<const std::uint8_t*>(pixels), stride, fc.width, fc.height, tmp);
    write_data(p, stride, fc.width, fc.height, true);
  }

  // Writes the chunks that follow the image data and IEND; returns the file.
  std::vector<std::uint8_t> finish() {
    if (!image_done_) fail("image incomplete");
    for (const Text& t : info.text)
      if (t.location & kAfterIDAT) write_text(t);
    write_unknown(kAfterIDAT);
    put_chunk("IEND", nullptr, 0);
    begun_ = false;
    return out_.to_vector();
  }

 private:
  // Rows given with `transforms` into native layout (in `tmp`; `stride`
  // becomes that of the converted rows).
  const std::uint8_t* convert(const std::uint8_t* pixels, std::size_t& stride, std::uint32_t w, std::uint32_t h,
                              std::vector<std::uint8_t>& tmp) {
    if (!transforms.any()) return pixels;
    detail::WritePlan plan;
    plan.init(header, transforms);
    const std::size_t rb = row_bytes_for(w), in_rb = plan.input_row_bytes(w);
    tmp.resize(rb * h + 8);
    for (std::uint32_t y = 0; y < h; y++) plan.run(pixels + y * (h > 1 ? stride : in_rb), w, tmp.data() + y * rb);
    stride = rb;
    return tmp.data();
  }

  std::size_t row_bytes_for(std::uint32_t w) const {
    return (static_cast<std::size_t>(w) * num_channels(header.color_type) * header.bit_depth + 7) / 8;
  }

  void validate() const {
    const Header& h = header;
    if (h.width == 0 || h.height == 0 || h.width > 0x7fffffffu || h.height > 0x7fffffffu)
      fail("Invalid image size in IHDR");
    unsigned bd = h.bit_depth;
    bool ok;
    switch (h.color_type) {
      case kGray: ok = bd == 1 || bd == 2 || bd == 4 || bd == 8 || bd == 16; break;
      case kIndexed: ok = bd == 1 || bd == 2 || bd == 4 || bd == 8; break;
      case kTruecolor: case kGrayAlpha: case kTruecolorAlpha: ok = bd == 8 || bd == 16; break;
      default: fail("Invalid color type in IHDR");
    }
    if (!ok) fail("Invalid color type/bit depth combination in IHDR");
    if (h.interlace > 1) fail("Unknown interlace method in IHDR");
    if (h.color_type == kIndexed) {
      if (info.palette.empty()) fail("Valid palette required for paletted images");
      if (info.palette.size() > (1u << bd)) fail("Invalid palette length");
    } else if (info.palette.size() > 256) {
      fail("Invalid palette length");
    }
  }

  // ---- chunks ---------------------------------------------------------------

  void put_chunk(const char* type, const std::uint8_t* data, std::size_t len) {
    std::uint8_t hdr[8];
    detail::write_u32(hdr, static_cast<std::uint32_t>(len));
    std::memcpy(hdr + 4, type, 4);
    out_.append(hdr, 8);
    if (len) out_.append(data, len);
    std::uint32_t crc = zlib::crc32(0, hdr + 4, 4);
    if (len) crc = zlib::crc32(crc, data, len);
    std::uint8_t c[4];
    detail::write_u32(c, crc);
    out_.append(c, 4);
  }
  void put_chunk(const char* type, const std::vector<std::uint8_t>& v) {
    put_chunk(type, v.data(), v.size());
  }

  int text_level() const {
    return options.text_level < 0 ? zlib::kDefaultCompression : options.text_level;
  }
  std::vector<std::uint8_t> compress_text(const std::string& s) const {
    std::span<const std::uint8_t> in(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    return zlib::compress(in, text_level());
  }

  void write_header_chunks() {
    using namespace detail;
    const Info& in = info;
    const Header& h = header;
    out_.append(signature, 8);
    std::vector<std::uint8_t> v;
    put_u32(v, h.width);
    put_u32(v, h.height);
    v.insert(v.end(), {h.bit_depth, h.color_type, 0, 0, h.interlace});
    put_chunk("IHDR", v);
    if (in.animation) {
      v.clear();
      put_u32(v, in.animation->num_frames);
      put_u32(v, in.animation->num_plays);
      put_chunk("acTL", v);
    }
    if (in.cicp) {
      const Cicp& c = *in.cicp;
      const std::uint8_t d[4] = {c.colour_primaries, c.transfer_function, c.matrix_coefficients,
                                 c.full_range};
      put_chunk("cICP", d, 4);
    }
    if (in.content_light) {
      v.clear();
      put_u32(v, in.content_light->max_cll);
      put_u32(v, in.content_light->max_fall);
      put_chunk("cLLI", v);
    }
    if (in.mastering) {
      const MasteringDisplay& m = *in.mastering;
      v.clear();
      for (unsigned x : {m.red_x, m.red_y, m.green_x, m.green_y, m.blue_x, m.blue_y, m.white_x,
                         m.white_y})
        put_u16(v, x);
      put_u32(v, m.max_luminance);
      put_u32(v, m.min_luminance);
      put_chunk("mDCV", v);
    }
    if (in.gamma) {
      v.clear();
      put_u32(v, *in.gamma);
      put_chunk("gAMA", v);
    }
    if (in.icc) {
      check_keyword(in.icc->name, "iCCP");
      v.clear();
      put_str(v, in.icc->name);
      v.push_back(0);  // compression method
      std::vector<std::uint8_t> z = zlib::compress(in.icc->data, text_level());
      v.insert(v.end(), z.begin(), z.end());
      put_chunk("iCCP", v);
    } else if (in.srgb_intent) {
      std::uint8_t d = *in.srgb_intent;
      put_chunk("sRGB", &d, 1);
    }
    if (in.chromaticities) {
      const Chromaticities& c = *in.chromaticities;
      v.clear();
      for (std::int32_t x : {c.white_x, c.white_y, c.red_x, c.red_y, c.green_x, c.green_y, c.blue_x,
                             c.blue_y})
        put_u32(v, static_cast<std::uint32_t>(x));
      put_chunk("cHRM", v);
    }
    if (in.sbit) {
      const SignificantBits& s = *in.sbit;
      v.clear();
      if (h.color_type & 2) v.insert(v.end(), {s.red, s.green, s.blue});
      else v.push_back(s.gray);
      if (h.color_type & 4) v.push_back(s.alpha);
      unsigned depth = h.color_type == kIndexed ? 8 : h.bit_depth;
      for (std::uint8_t b : v)
        if (b == 0 || b > depth) fail("sBIT: invalid");
      put_chunk("sBIT", v);
    }
    write_unknown(kBeforePLTE);
    if (!in.palette.empty()) {
      v.clear();
      for (const auto& e : in.palette) v.insert(v.end(), {e.red, e.green, e.blue});
      put_chunk("PLTE", v);
    }
    if (in.has_trns()) {
      v.clear();
      if (h.color_type == kIndexed) {
        if (in.trans_alpha.size() > in.palette.size()) fail("tRNS: invalid");
        v = in.trans_alpha;
      } else if (in.trans_color && (h.color_type == kGray || h.color_type == kTruecolor)) {
        const Color16& c = *in.trans_color;
        unsigned max = (1u << h.bit_depth) - 1;
        if (h.color_type == kGray) {
          if (c.gray > max) fail("tRNS: gray value out of range");
          put_u16(v, c.gray);
        } else {
          if (c.red > max || c.green > max || c.blue > max) fail("tRNS: value out of range");
          put_u16(v, c.red), put_u16(v, c.green), put_u16(v, c.blue);
        }
      } else {
        fail("tRNS: invalid for this color type");
      }
      put_chunk("tRNS", v);
    }
    if (in.background) {
      const Color16& c = *in.background;
      v.clear();
      if (h.color_type == kIndexed) {
        if (c.index >= in.palette.size()) fail("bKGD: invalid index");
        v.push_back(c.index);
      } else if (h.color_type & 2) {
        put_u16(v, c.red), put_u16(v, c.green), put_u16(v, c.blue);
      } else {
        put_u16(v, c.gray);
      }
      put_chunk("bKGD", v);
    }
    if (in.exif) put_chunk("eXIf", *in.exif);
    if (!in.histogram.empty()) {
      if (in.histogram.size() != in.palette.size()) fail("hIST: invalid");
      v.clear();
      for (std::uint16_t x : in.histogram) put_u16(v, x);
      put_chunk("hIST", v);
    }
    if (in.offs) {
      v.clear();
      put_u32(v, static_cast<std::uint32_t>(in.offs->x));
      put_u32(v, static_cast<std::uint32_t>(in.offs->y));
      v.push_back(in.offs->unit);
      put_chunk("oFFs", v);
    }
    if (in.pcal) {
      const Calibration& c = *in.pcal;
      check_keyword(c.purpose, "pCAL");
      v.clear();
      put_str(v, c.purpose);
      put_u32(v, static_cast<std::uint32_t>(c.x0));
      put_u32(v, static_cast<std::uint32_t>(c.x1));
      v.push_back(c.type);
      v.push_back(static_cast<std::uint8_t>(c.params.size()));
      put_str(v, c.units, !c.params.empty());
      for (std::size_t i = 0; i < c.params.size(); i++) {
        if (!check_fp_number(c.params[i].data(), c.params[i].size(), nullptr))
          fail("pCAL: invalid parameter");
        put_str(v, c.params[i], i + 1 < c.params.size());
      }
      put_chunk("pCAL", v);
    }
    if (in.phys) {
      v.clear();
      put_u32(v, in.phys->x);
      put_u32(v, in.phys->y);
      v.push_back(in.phys->unit);
      put_chunk("pHYs", v);
    }
    if (in.scal) {
      bool pw = false, ph = false;
      if (!check_fp_number(in.scal->width.data(), in.scal->width.size(), &pw) || !pw ||
          !check_fp_number(in.scal->height.data(), in.scal->height.size(), &ph) || !ph)
        fail("sCAL: invalid");
      v.clear();
      v.push_back(in.scal->unit);
      put_str(v, in.scal->width);
      put_str(v, in.scal->height, false);
      put_chunk("sCAL", v);
    }
    for (const SuggestedPalette& s : in.splt) {
      check_keyword(s.name, "sPLT");
      if (s.depth != 8 && s.depth != 16) fail("sPLT: invalid sample depth");
      v.clear();
      put_str(v, s.name);
      v.push_back(s.depth);
      for (const auto& e : s.entries) {
        if (s.depth == 8) {
          v.insert(v.end(), {static_cast<std::uint8_t>(e.red), static_cast<std::uint8_t>(e.green),
                             static_cast<std::uint8_t>(e.blue), static_cast<std::uint8_t>(e.alpha)});
        } else {
          put_u16(v, e.red), put_u16(v, e.green), put_u16(v, e.blue), put_u16(v, e.alpha);
        }
        put_u16(v, e.frequency);
      }
      put_chunk("sPLT", v);
    }
    if (in.time) {
      const Time& t = *in.time;
      if (t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 || t.hour > 23 || t.minute > 59 ||
          t.second > 60)
        fail("tIME: invalid");
      v.clear();
      put_u16(v, t.year);
      v.insert(v.end(), {t.month, t.day, t.hour, t.minute, t.second});
      put_chunk("tIME", v);
    }
    for (const Text& t : in.text)
      if (!(t.location & kAfterIDAT)) write_text(t);
    write_unknown(kBeforeIDAT);
    if (in.animation && in.default_image_is_frame) {
      if (in.frames.empty()) fail("default image frame without its FrameControl");
      const FrameControl& f = in.frames.front();
      if (f.x || f.y || f.width != h.width || f.height != h.height)
        fail("the default image's frame must cover the image");
      write_fctl(f);
    }
  }

  void write_text(const Text& t) {
    using namespace detail;
    const char* name = t.type == Text::kText ? "tEXt" : t.type == Text::kZtxt ? "zTXt" : "iTXt";
    check_keyword(t.keyword, name);
    std::vector<std::uint8_t> v;
    put_str(v, t.keyword);
    switch (t.type) {
      case Text::kText:
        put_str(v, t.text, false);
        break;
      case Text::kZtxt: {
        v.push_back(0);
        std::vector<std::uint8_t> z = compress_text(t.text);
        v.insert(v.end(), z.begin(), z.end());
        break;
      }
      default: {
        const bool compressed = t.type == Text::kItxtCompressed;
        v.push_back(compressed ? 1 : 0);
        v.push_back(0);
        put_str(v, t.language);
        put_str(v, t.translated_keyword);
        if (compressed) {
          std::vector<std::uint8_t> z = compress_text(t.text);
          v.insert(v.end(), z.begin(), z.end());
        } else {
          put_str(v, t.text, false);
        }
      }
    }
    put_chunk(name, v);
  }

  // Unknown chunks whose location is `where` (a chunk with several location
  // bits goes to the last of them).
  void write_unknown(std::uint8_t where) {
    for (const UnknownChunk& c : info.unknown) {
      std::uint8_t loc = c.location;
      loc = (loc & kAfterIDAT) ? kAfterIDAT : (loc & kBeforeIDAT) ? kBeforeIDAT : kBeforePLTE;
      if (loc != where) continue;
      if (!detail::valid_chunk_type(reinterpret_cast<const std::uint8_t*>(c.type.data())))
        fail("invalid unknown chunk type");
      put_chunk(c.type.data(), c.data);
    }
  }

  void check_frame(const FrameControl& f) const {
    if (f.width == 0 || f.height == 0 || f.x > header.width || f.y > header.height ||
        f.width > header.width - f.x || f.height > header.height - f.y || f.dispose > 2 ||
        f.blend > 1)
      fail("Invalid fcTL");
  }

  void write_fctl(const FrameControl& f) {
    using namespace detail;
    std::vector<std::uint8_t> v;
    put_u32(v, seq_++);
    put_u32(v, f.width), put_u32(v, f.height), put_u32(v, f.x), put_u32(v, f.y);
    put_u16(v, f.delay_num), put_u16(v, f.delay_den);
    v.push_back(f.dispose), v.push_back(f.blend);
    put_chunk("fcTL", v);
  }

  // ---- image data -----------------------------------------------------------

  // Stores part of a zlib stream as IDAT or fdAT chunks of at most `chunk`
  // bytes.
  void put_data_chunks(const std::uint8_t* z, std::size_t n, bool frame, std::size_t chunk) {
    do {
      std::size_t k = n < chunk ? n : chunk;
      if (!frame) {
        put_chunk("IDAT", z, k);
      } else {
        std::vector<std::uint8_t> v;
        detail::put_u32(v, seq_++);
        v.insert(v.end(), z, z + k);
        put_chunk("fdAT", v);
      }
      z += k;
      n -= k;
    } while (n);
  }

  // Whether 16-bit samples (in host byte order) need swapping into PNG's
  // big-endian order.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  static constexpr bool kSwap16 = false;
#else
  static constexpr bool kSwap16 = true;
#endif

  // Copies a row into PNG byte order.
  static void load_row(std::uint8_t* dst, const std::uint8_t* src, std::size_t rb, bool swap) {
    if (!swap) {
      std::memcpy(dst, src, rb);
    } else {
      for (std::size_t i = 0; i + 1 < rb; i += 2) {
        dst[i] = src[i + 1];
        dst[i + 1] = src[i];
      }
    }
  }

  // The rows of an image of w x h pixels: one segment, or the seven
  // interlace passes (pass rows gathered from the image rows).
  std::vector<detail::FastEncoder::Segment> segments(const std::uint8_t* pixels, std::size_t stride,
                                                     std::uint32_t w, std::uint32_t h) const {
    using namespace detail;
    const bool swap = kSwap16 && header.bit_depth == 16;
    const std::size_t rb = row_bytes_for(w);
    std::vector<FastEncoder::Segment> segs;
    if (!header.interlace) {
      segs.push_back({rb, h, [=](std::uint32_t y, std::uint8_t* dst) {
                        load_row(dst, pixels + y * stride, rb, swap);
                      }});
      return segs;
    }
    const unsigned bits = static_cast<unsigned>(num_channels(header.color_type) * header.bit_depth);
    for (int p = 0; p < 7; p++) {
      if (w <= static_cast<std::uint32_t>(adam7_x_start[p]) ||
          h <= static_cast<std::uint32_t>(adam7_y_start[p]))
        continue;
      const std::uint32_t pw = (w - adam7_x_start[p] + adam7_x_delta[p] - 1) / adam7_x_delta[p];
      const std::uint32_t ph = (h - adam7_y_start[p] + adam7_y_delta[p] - 1) / adam7_y_delta[p];
      const std::size_t prb = row_bytes_for(pw);
      segs.push_back({prb, ph, [=](std::uint32_t y, std::uint8_t* dst) {
                        const std::uint8_t* src =
                            pixels + (adam7_y_start[p] + static_cast<std::size_t>(y) * adam7_y_delta[p]) * stride;
                        if (bits >= 8) {
                          const unsigned bpp = bits / 8;
                          for (std::uint32_t x = 0; x < pw; x++) {
                            std::size_t sx = adam7_x_start[p] + static_cast<std::size_t>(x) * adam7_x_delta[p];
                            std::memcpy(dst + x * bpp, src + sx * bpp, bpp);
                          }
                          if (swap)
                            for (std::size_t i = 0; i + 1 < prb; i += 2) std::swap(dst[i], dst[i + 1]);
                        } else {
                          std::memset(dst, 0, prb);
                          const unsigned spb = 8 / bits;
                          for (std::uint32_t x = 0; x < pw; x++) {
                            std::size_t sx = adam7_x_start[p] + static_cast<std::size_t>(x) * adam7_x_delta[p];
                            unsigned s = (src[sx / spb] >> (8 - bits - (sx % spb) * bits)) & ((1u << bits) - 1);
                            dst[x / spb] |= static_cast<std::uint8_t>(s << (8 - bits - (x % spb) * bits));
                          }
                        }
                      }});
    }
    return segs;
  }

  void write_data(const std::uint8_t* pixels, std::size_t stride, std::uint32_t w, std::uint32_t h,
                  bool frame) {
    using namespace detail;
    std::vector<FastEncoder::Segment> segs = segments(pixels, stride, w, h);
    if (mode == Mode::kFast) {
      // One chunk, the stream compressed straight into the output; length
      // and CRC filled in afterwards.
      const std::size_t start = out_.size();
      std::uint8_t hdr[12] = {0, 0, 0, 0, 'I', 'D', 'A', 'T'};
      if (frame) {
        std::memcpy(hdr + 4, "fdAT", 4);
        detail::write_u32(hdr + 8, seq_++);
      }
      out_.append(hdr, frame ? 12 : 8);
      FastEncoder fe;
      fe.encode(segs.data(), segs.size(), bpp_, filters_, out_);
      const std::size_t len = out_.size() - start - 8;
      if (len > 0x7fffffffu) fail("image data too large for one chunk");
      detail::write_u32(out_.data() + start, static_cast<std::uint32_t>(len));
      std::uint8_t crc[4];
      detail::write_u32(crc, zlib::crc32(0, out_.data() + start + 4, len + 4));
      out_.append(crc, 4);
      return;
    }
    // kZlib: rows filtered one by one and deflated.
    std::unique_ptr<zlib::Deflater> dp = make_deflater();
    zlib::Deflater& d = *dp;
    const std::size_t chunk = options.idat_size ? options.idat_size : 8192;
    std::vector<std::uint8_t> z(chunk), pending;
    auto drain = [&](int flush) {
      int ret;
      do {
        d.next_out = z.data();
        d.avail_out = static_cast<unsigned>(z.size());
        ret = d.deflate(flush);
        if (ret != zlib::kOk && ret != zlib::kStreamEnd && ret != zlib::kBufError) fail("zlib error");
        pending.insert(pending.end(), z.data(), z.data() + (z.size() - d.avail_out));
        if (pending.size() >= chunk) {
          std::size_t whole = pending.size() / chunk * chunk;
          put_data_chunks(pending.data(), whole, frame, chunk);
          pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(whole));
        }
      } while (d.avail_out == 0 || (flush == zlib::kFinish && ret != zlib::kStreamEnd));
    };
    std::size_t max_rb = 0;
    for (auto& seg : segs) max_rb = std::max(max_rb, seg.row_bytes);
    detail::RowFilter rf;
    rf.start(max_rb, bpp_, filters_);
    std::vector<std::uint8_t> filtered(max_rb + 1);
    for (auto& seg : segs) {
      rf.restart();
      for (std::uint32_t y = 0; y < seg.rows; y++) {
        seg.load(y, rf.row());
        rf.filter(filtered.data(), seg.row_bytes);
        d.next_in = filtered.data();
        d.avail_in = static_cast<unsigned>(seg.row_bytes + 1);
        drain(zlib::kNoFlush);
      }
    }
    d.next_in = nullptr;
    d.avail_in = 0;
    drain(zlib::kFinish);
    if (!pending.empty()) put_data_chunks(pending.data(), pending.size(), frame, chunk);
  }

  // (A Deflater holds pointers into itself: it is created in place.)
  std::unique_ptr<zlib::Deflater> make_deflater() const {
    int strategy = options.strategy >= 0         ? options.strategy
                   : filters_ != kFilterBitNone ? zlib::kFiltered
                                                : zlib::kDefaultStrategy;
    return std::make_unique<zlib::Deflater>(
        options.level < 0 ? zlib::kDefaultCompression : options.level, options.window_bits,
        options.mem_level, strategy);
  }

  // Row-by-row kZlib compression of a non-interlaced default image, writing
  // IDAT chunks as the deflater fills its output.
  void start_row_stream() {
    deflater_ = make_deflater();
    row_filter_.start(row_bytes_, bpp_, filters_);
    filtered_.assign(row_bytes_ + 1, 0);
    zbuf_.resize(options.idat_size ? options.idat_size : 8192);
    deflater_->next_out = zbuf_.data();
    deflater_->avail_out = static_cast<unsigned>(zbuf_.size());
    streaming_ = true;
  }
  void flush_zbuf() {
    std::size_t n = zbuf_.size() - deflater_->avail_out;
    if (n) put_chunk("IDAT", zbuf_.data(), n);
    deflater_->next_out = zbuf_.data();
    deflater_->avail_out = static_cast<unsigned>(zbuf_.size());
  }
  void stream_row(const std::uint8_t* row) {
    load_row(row_filter_.row(), row, row_bytes_, kSwap16 && header.bit_depth == 16);
    row_filter_.filter(filtered_.data(), row_bytes_);
    auto& z = *deflater_;
    z.next_in = filtered_.data();
    z.avail_in = static_cast<unsigned>(row_bytes_ + 1);
    do {
      if (z.deflate(zlib::kNoFlush) != zlib::kOk) fail("zlib error");
      if (z.avail_out == 0) flush_zbuf();
    } while (z.avail_in);
  }
  void finish_row_stream() {
    auto& z = *deflater_;
    int ret = 0;
    while (ret != zlib::kStreamEnd) {
      ret = z.deflate(zlib::kFinish);
      if (ret != zlib::kStreamEnd && ret != zlib::kOk && ret != zlib::kBufError) fail("zlib error");
      if (z.avail_out == 0 || ret == zlib::kStreamEnd) flush_zbuf();
    }
    deflater_.reset();
    streaming_ = false;
  }

  detail::ByteBuffer out_;
  std::unique_ptr<std::uint8_t[]> image_;  // rows kept by write_row()
  std::unique_ptr<zlib::Deflater> deflater_;
  detail::RowFilter row_filter_;
  std::vector<std::uint8_t> filtered_, zbuf_, row_tmp_;
  std::size_t row_bytes_ = 0;
  unsigned bpp_ = 1, filters_ = kFilterBitsAll;
  std::uint32_t rows_done_ = 0, seq_ = 0;
  bool image_done_ = false, streaming_ = false, begun_ = false;
};

}  // namespace vio::png
