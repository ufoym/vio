// vio::png - PNG decoder.
//
// png::Decoder decodes all standard PNG variants (all bit depths and color
// types, Adam7 interlacing) into the PNG's native sample layout.
//
// A non-interlaced image is inflated straight into the image buffer, filter
// bytes included, and each row is then reconstructed in place, moving left
// over the filter bytes of the rows above it.
#pragma once

#include <memory>
#include <span>
#include <vector>

#include "common.hpp"
#include "filter.hpp"
#include "inflate.hpp"

namespace vio::png {

class Decoder {
 public:
  Header header;
  std::vector<PaletteEntry> palette;
  std::vector<std::uint8_t> transparency;  // tRNS chunk contents
  std::vector<std::uint8_t> icc_profile;
  bool has_icc = false;

  // Parses the signature and IHDR chunk.
  void read_header(std::span<const std::uint8_t> file) {
    using namespace detail;
    file_ = file;
    if (file.size() < 8 || std::memcmp(file.data(), signature, 8) != 0) fail("invalid signature");
    pos_ = 8;
    const std::uint8_t *type, *data;
    std::uint32_t len;
    if (!next_chunk(type, data, len)) fail("invalid chunk checksum");
    if (std::memcmp(type, "IHDR", 4) != 0 || len != 13) fail("missing IHDR chunk");
    parse_ihdr(data);
    have_header_ = true;
  }

  // Parses the remaining chunks and decodes the image.  Samples are stored
  // in native PNG layout, rows of row_bytes() bytes, 16-bit samples in host
  // byte order, sub-byte samples packed MSB first.
  void decode(std::span<const std::uint8_t> file, unsigned long long max_pixels = 0) {
    if (!have_header_ || file.data() != file_.data()) read_header(file);
    if (max_pixels && static_cast<unsigned long long>(header.width) * header.height > max_pixels)
      VIO_FAIL("Maximum supported image dimension is %u pixels",
               static_cast<unsigned>(max_pixels));
    decode_rest();
  }

  std::size_t row_bytes() const { return row_bytes_; }
  const std::uint8_t* row(std::uint32_t y) const { return pixels_.get() + y * row_bytes_; }
  std::span<std::uint8_t> pixels() { return {pixels_.get(), row_bytes_ * header.height}; }

 private:
  // Reads the next chunk; returns false if its CRC is bad.
  bool next_chunk(const std::uint8_t*& type, const std::uint8_t*& data, std::uint32_t& len) {
    using namespace detail;
    if (file_.size() - pos_ < 12) fail("end of stream");
    len = read_u32(file_.data() + pos_);
    if (len > 0x7fffffffu) fail("chunk length exceeds maximum");
    type = file_.data() + pos_ + 4;
    if (file_.size() - pos_ - 12 < len) fail("end of stream");
    data = type + 4;
    std::uint32_t crc = read_u32(data + len);
    pos_ += 12 + static_cast<std::size_t>(len);
    return zlib::crc32(0, type, 4 + static_cast<std::size_t>(len)) == crc;
  }

  void decode_rest() {
    bool seen_idat = false, idat_done = false, seen_iend = false;
    // IDAT data: a single chunk is used in place, several are concatenated.
    std::span<const std::uint8_t> idat;
    std::vector<std::uint8_t> joined;
    while (!seen_iend) {
      const std::uint8_t *type, *data;
      std::uint32_t len;
      bool crc_ok = next_chunk(type, data, len);
      bool critical = !(type[0] & 0x20);
      if (!crc_ok) {
        if (critical) fail("invalid chunk checksum");
        continue;  // ancillary chunks with bad CRCs are discarded
      }
      auto is = [&](const char* t) { return std::memcmp(type, t, 4) == 0; };
      if (is("IDAT")) {
        if (idat_done) fail("non-contiguous IDAT chunks");
        if (header.color_type == kIndexed && palette.empty()) fail("missing palette");
        if (!seen_idat) {
          idat = {data, len};
        } else {
          if (joined.empty()) joined.assign(idat.begin(), idat.end());
          joined.insert(joined.end(), data, data + len);
          idat = joined;
        }
        seen_idat = true;
      } else {
        if (seen_idat) idat_done = true;
        if (is("IEND")) {
          seen_iend = true;
        } else if (is("PLTE")) {
          if (len % 3 != 0 || len == 0 || len / 3 > 256) fail("invalid palette");
          palette.resize(len / 3);
          for (std::uint32_t i = 0; i < len / 3; i++)
            palette[i] = {data[3 * i], data[3 * i + 1], data[3 * i + 2]};
        } else if (is("tRNS") && !seen_idat) {
          transparency.assign(data, data + len);
        } else if (is("iCCP") && !seen_idat) {
          parse_iccp(data, len);
        } else if (is("IHDR")) {
          fail("duplicate IHDR chunk");
        } else if (critical) {
          fail("unknown critical chunk");
        }
      }
    }
    if (!seen_idat) fail("missing IDAT chunk");
    decompress(idat);
  }

  void parse_ihdr(const std::uint8_t* d) {
    using namespace detail;
    header.width = read_u32(d);
    header.height = read_u32(d + 4);
    header.bit_depth = d[8];
    header.color_type = d[9];
    header.interlace = d[12];
    if (header.width == 0 || header.width > 0x7fffffffu) fail("invalid image width");
    if (header.height == 0 || header.height > 0x7fffffffu) fail("invalid image height");
    unsigned bd = header.bit_depth;
    bool ok;
    switch (header.color_type) {
      case kGray: ok = bd == 1 || bd == 2 || bd == 4 || bd == 8 || bd == 16; break;
      case kIndexed: ok = bd == 1 || bd == 2 || bd == 4 || bd == 8; break;
      case kTruecolor: case kGrayAlpha: case kTruecolorAlpha: ok = bd == 8 || bd == 16; break;
      default: fail("invalid color type");
    }
    if (!ok) fail("invalid bit depth");
    if (d[10] != 0) fail("invalid compression method");
    if (d[11] != 0) fail("invalid filter method");
    if (d[12] > 1) fail("invalid interlace method");
    row_bytes_ = (static_cast<std::size_t>(header.width) * num_channels(header.color_type) *
                      header.bit_depth + 7) / 8;
  }

  void parse_iccp(const std::uint8_t* d, std::uint32_t len) {
    std::uint32_t n = 0;
    while (n < len && n < 80 && d[n] != 0) n++;
    // A malformed profile is ignored, as other ancillary chunks would be.
    if (n == 0 || n >= 80 || n + 2 > len || d[n + 1] != 0) return;
    try {
      icc_profile = zlib::uncompress(std::span(d + n + 2, len - n - 2));
      has_icc = true;
    } catch (const Error&) {
      icc_profile.clear();
    }
  }

  // Inflates exactly `size` bytes of filtered image data into `out`.
  static void inflate(std::span<const std::uint8_t> idat, std::uint8_t* out, std::size_t size) {
    auto inf = std::make_unique<zlib::Inflater>();
    std::size_t n = 0;
    try {
      n = inf->decompress(idat, out, size);
    } catch (const Error&) {
      fail("end of stream");
    }
    if (n < size) fail("end of stream");
  }

  // Reconstructs `rows` filtered rows of `rb` bytes stored at `buf` with
  // their filter bytes (rows of rb + 1 bytes), moving them to rows of rb
  // bytes at the same address.
  static void unfilter_rows(std::uint8_t* buf, std::size_t rb, std::uint32_t rows, unsigned bpp) {
    const std::uint8_t* prev = nullptr;
    for (std::uint32_t y = 0; y < rows; y++) {
      const std::uint8_t* src = buf + y * (rb + 1);
      std::uint8_t* dst = buf + y * rb;
      unsigned filter = src[0];
      if (filter > 4) detail::fail("invalid filter type");
      detail::unfilter_row(filter, dst, src + 1, prev, rb, bpp);
      prev = dst;
    }
  }

  static void swap16(std::uint8_t* p, std::size_t n) {
    for (std::size_t i = 0; i + 1 < n; i += 2) {
      std::uint16_t v = static_cast<std::uint16_t>(p[i] << 8 | p[i + 1]);
      std::memcpy(p + i, &v, 2);
    }
  }

  void decompress(std::span<const std::uint8_t> idat) {
    using namespace detail;
    const Header& h = header;
    const int ch = num_channels(h.color_type);
    const unsigned bits_pp = static_cast<unsigned>(ch * h.bit_depth);
    const unsigned bpp = bits_pp < 8 ? 1u : bits_pp / 8;
    // Slack past the data: the reconstruction kernels read and write a few
    // bytes beyond a row's last pixel.
    constexpr std::size_t kSlack = 16;
    if (!h.interlace) {
      const std::size_t expected = static_cast<std::size_t>(h.height) * (row_bytes_ + 1);
      pixels_.reset(new std::uint8_t[expected + kSlack]);
      inflate(idat, pixels_.get(), expected);
      unfilter_rows(pixels_.get(), row_bytes_, h.height, bpp);
      if (h.bit_depth == 16) swap16(pixels_.get(), row_bytes_ * h.height);
      if (const unsigned used = (static_cast<std::size_t>(h.width) * bits_pp) % 8) {
        // Clear the padding bits after the last sample of each row (only
        // now: reconstructing the next row needs them as they were).
        const std::uint8_t mask = static_cast<std::uint8_t>(0xff00u >> used);
        for (std::uint32_t y = 0; y < h.height; y++) pixels_[y * row_bytes_ + row_bytes_ - 1] &= mask;
      }
      return;
    }
    struct Pass {
      std::uint32_t w, h;
      int p;
      std::size_t offset, rb;
    };
    Pass passes[7];
    std::size_t expected = 0;
    for (int p = 0; p < 7; p++) {
      std::uint32_t pw = (h.width - adam7_x_start[p] + adam7_x_delta[p] - 1) / adam7_x_delta[p];
      std::uint32_t ph = (h.height - adam7_y_start[p] + adam7_y_delta[p] - 1) / adam7_y_delta[p];
      if (h.width <= static_cast<std::uint32_t>(adam7_x_start[p])) pw = 0;
      if (h.height <= static_cast<std::uint32_t>(adam7_y_start[p])) ph = 0;
      std::size_t rb = (static_cast<std::size_t>(pw) * bits_pp + 7) / 8;
      passes[p] = {pw, ph, p, expected, rb};
      if (pw && ph) expected += ph * (rb + 1);
    }
    std::unique_ptr<std::uint8_t[]> raw(new std::uint8_t[expected + kSlack]);
    inflate(idat, raw.get(), expected);
    pixels_.reset(new std::uint8_t[row_bytes_ * h.height + kSlack]());
    for (const Pass& ps : passes) {
      if (!ps.w || !ps.h) continue;
      std::uint8_t* base = raw.get() + ps.offset;
      unfilter_rows(base, ps.rb, ps.h, bpp);
      for (std::uint32_t y = 0; y < ps.h; y++) {
        const std::uint8_t* line = base + y * ps.rb;
        std::uint32_t oy = adam7_y_start[ps.p] + y * adam7_y_delta[ps.p];
        std::uint8_t* dst = pixels_.get() + oy * row_bytes_;
        for (std::uint32_t x = 0; x < ps.w; x++) {
          std::size_t ox = adam7_x_start[ps.p] + static_cast<std::size_t>(x) * adam7_x_delta[ps.p];
          if (bits_pp >= 8) {
            std::memcpy(dst + ox * bpp, line + x * bpp, bpp);
          } else {
            unsigned spb = 8 / bits_pp;
            unsigned sample = (line[x / spb] >> (8 - bits_pp - (x % spb) * bits_pp)) &
                              ((1u << bits_pp) - 1);
            dst[ox / spb] |= static_cast<std::uint8_t>(sample << (8 - bits_pp - (ox % spb) * bits_pp));
          }
        }
      }
    }
    if (h.bit_depth == 16) swap16(pixels_.get(), row_bytes_ * h.height);
  }

  std::span<const std::uint8_t> file_;
  std::size_t pos_ = 0;
  bool have_header_ = false;
  std::size_t row_bytes_ = 0;
  std::unique_ptr<std::uint8_t[]> pixels_;
};

}  // namespace vio::png
