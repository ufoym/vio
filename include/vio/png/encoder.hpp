// vio::png - PNG encoder.
//
// png::Encoder produces files byte-identical to libspng 0.7: per-scanline
// adaptive filter selection, zlib level 6 with Z_FILTERED, 8192-byte IDAT
// chunks, iCCP compressed at the default level.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common.hpp"
#include "deflate.hpp"
#include "filter.hpp"

namespace vio::png {

class Encoder {
 public:
  Header header;
  std::vector<PaletteEntry> palette;  // required for kIndexed
  std::vector<std::uint8_t> icc_profile;
  std::string icc_name = "ICC Profile";

  // Writes the signature and the chunks preceding IDAT.
  void begin() {
    using namespace detail;
    const Header& h = header;
    if (h.width == 0 || h.height == 0) fail("invalid image width");
    if (h.color_type == kIndexed && palette.empty()) fail("missing palette");
    out_.put(signature, 8);
    std::uint8_t ihdr[13];
    write_u32(ihdr, h.width);
    write_u32(ihdr + 4, h.height);
    ihdr[8] = h.bit_depth;
    ihdr[9] = h.color_type;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    write_chunk("IHDR", ihdr, 13);
    if (!icc_profile.empty()) {
      std::vector<std::uint8_t> z = zlib::compress(icc_profile, zlib::kDefaultCompression);
      std::vector<std::uint8_t> data(icc_name.begin(), icc_name.end());
      data.push_back(0);
      data.push_back(0);  // compression method
      data.insert(data.end(), z.begin(), z.end());
      write_chunk("iCCP", data.data(), data.size());
    }
    if (!palette.empty()) {
      std::vector<std::uint8_t> data;
      for (const auto& e : palette) {
        data.push_back(e.red);
        data.push_back(e.green);
        data.push_back(e.blue);
      }
      write_chunk("PLTE", data.data(), data.size());
    }

    bpp_ = h.bit_depth < 8 ? 1u : static_cast<unsigned>(num_channels(h.color_type) * (h.bit_depth / 8));
    row_bytes_ = (static_cast<std::size_t>(h.width) * num_channels(h.color_type) * h.bit_depth + 7) / 8;
    filter_ = !(h.color_type == kIndexed || h.bit_depth < 8);
    deflater_ = std::make_unique<zlib::Deflater>(zlib::kDefaultCompression, 15, 8,
                                                 filter_ ? zlib::kFiltered : zlib::kDefaultStrategy);
    scanline_.assign(row_bytes_ + 1, 0);
    prev_.assign(row_bytes_ + 1, 0);
    filtered_.assign(row_bytes_ + 1, 0);
    rows_done_ = 0;
    open_idat();
  }

  // Encodes one row in native layout (16-bit samples in host byte order).
  void write_row(const void* row) {
    if (rows_done_ >= header.height) return;
    std::uint8_t* cur = scanline_.data() + 1;
    std::memcpy(cur, row, row_bytes_);
    if (header.bit_depth == 16) {
      for (std::size_t i = 0; i + 1 < row_bytes_ + 1 && i < row_bytes_; i += 2) {
        std::uint16_t v;
        std::memcpy(&v, cur + i, 2);
        cur[i] = static_cast<std::uint8_t>(v >> 8);
        cur[i + 1] = static_cast<std::uint8_t>(v);
      }
    }
    const std::uint8_t* prev = prev_.data() + 1;  // zeros for the first scanline
    unsigned filter = filter_ ? best_filter(prev, cur) : 0;
    const std::uint8_t* emit;
    if (filter == 0) {
      scanline_[0] = 0;
      emit = scanline_.data();
    } else {
      apply_filter(filtered_.data() + 1, prev, cur, filter);
      filtered_[0] = static_cast<std::uint8_t>(filter);
      emit = filtered_.data();
    }
    write_idat_bytes(emit, row_bytes_ + 1);
    std::swap(prev_, scanline_);
    if (++rows_done_ == header.height) finish_idat();
  }

  // Completes the file (IEND) and returns it.
  std::vector<std::uint8_t> finish() {
    if (rows_done_ < header.height) fail("image incomplete");
    write_chunk("IEND", nullptr, 0);
    return out_.take();
  }

 private:
  void write_chunk(const char* type, const std::uint8_t* data, std::size_t len) {
    std::uint8_t hdr[8];
    detail::write_u32(hdr, static_cast<std::uint32_t>(len));
    std::memcpy(hdr + 4, type, 4);
    out_.put(hdr, 8);
    if (len) out_.put(data, len);
    std::uint32_t crc = zlib::crc32(0, hdr + 4, 4);
    if (len) crc = zlib::crc32(crc, data, len);
    std::uint8_t c[4];
    detail::write_u32(c, crc);
    out_.put(c, 4);
  }

  static constexpr unsigned kWriteSize = 8192;

  void open_idat() {
    chunk_start_ = out_.size();
    std::uint8_t* p = out_.grow(8 + kWriteSize + 4);
    std::memcpy(p + 4, "IDAT", 4);
    deflater_->next_out = p + 8;
    deflater_->avail_out = kWriteSize;
  }
  void close_idat(unsigned length) {
    auto& b = out_.bytes();
    std::uint8_t* hdr = b.data() + chunk_start_;
    detail::write_u32(hdr, length);
    std::uint32_t crc = zlib::crc32(0, hdr + 4, 4 + length);
    detail::write_u32(hdr + 8 + length, crc);
    b.resize(chunk_start_ + 12 + length);
  }
  void write_idat_bytes(const std::uint8_t* data, std::size_t len) {
    auto& z = *deflater_;
    z.next_in = data;
    z.avail_in = static_cast<unsigned>(len);
    int ret;
    do {
      ret = z.deflate(zlib::kNoFlush);
      if (z.avail_out == 0) {
        close_idat(kWriteSize);
        open_idat();
      }
    } while (z.avail_in);
    if (ret != zlib::kOk) fail("zlib error");
  }
  void finish_idat() {
    auto& z = *deflater_;
    int ret = 0;
    while (ret != zlib::kStreamEnd) {
      ret = z.deflate(zlib::kFinish);
      if (ret == zlib::kStreamEnd) break;
      if (ret != zlib::kOk && ret != zlib::kBufError) fail("zlib error");
      if (z.avail_out == 0) {
        close_idat(kWriteSize);
        open_idat();
      }
    }
    close_idat(kWriteSize - z.avail_out);
    deflater_.reset();
  }

  void apply_filter(std::uint8_t* out, const std::uint8_t* prev, const std::uint8_t* cur,
                    unsigned filter) const {
    for (std::size_t i = 0; i < row_bytes_; i++) {
      std::uint8_t a = i >= bpp_ ? cur[i - bpp_] : 0, b = prev[i],
                   c = i >= bpp_ ? prev[i - bpp_] : 0, x = cur[i];
      switch (filter) {
        case 1: x = static_cast<std::uint8_t>(x - a); break;
        case 2: x = static_cast<std::uint8_t>(x - b); break;
        case 3: x = static_cast<std::uint8_t>(x - (a + b) / 2); break;
        case 4: x = static_cast<std::uint8_t>(x - detail::paeth(a, b, c)); break;
      }
      out[i] = x;
    }
  }

  unsigned best_filter(const std::uint8_t* prev, const std::uint8_t* cur) const {
    if (row_bytes_ > INT32_MAX / 128) return 0;
    std::int32_t best_score = INT32_MAX;
    unsigned best = 0;
    for (unsigned f = 0; f < 5; f++) {
      std::int32_t sum = 0;
      for (std::size_t i = 0; i < row_bytes_; i++) {
        std::uint8_t a = i >= bpp_ ? cur[i - bpp_] : 0, b = prev[i],
                     c = i >= bpp_ ? prev[i - bpp_] : 0, x = cur[i];
        switch (f) {
          case 1: x = static_cast<std::uint8_t>(x - a); break;
          case 2: x = static_cast<std::uint8_t>(x - b); break;
          case 3: x = static_cast<std::uint8_t>(x - (a + b) / 2); break;
          case 4: x = static_cast<std::uint8_t>(x - detail::paeth(a, b, c)); break;
          default: break;
        }
        sum += 128 - std::abs(static_cast<int>(x) - 128);
      }
      std::int32_t score = std::abs(sum);
      if (score < best_score) {
        best_score = score;
        best = f;
      }
    }
    return best;
  }

  vio::detail::OutBuffer out_;
  std::unique_ptr<zlib::Deflater> deflater_;
  std::vector<std::uint8_t> scanline_, prev_, filtered_;
  std::size_t row_bytes_ = 0, chunk_start_ = 0;
  unsigned bpp_ = 1;
  bool filter_ = true;
  std::uint32_t rows_done_ = 0;
};

}  // namespace vio::png
