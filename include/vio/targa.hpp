// vio::targa - Targa reader (colormapped/RGB/gray, optional RLE) and writer.
// 8-bit samples only.
#pragma once

#include "core.hpp"

namespace vio::targa {

class Reader : public ImageReader<std::uint8_t> {
 protected:
  void start_input() override {
    if (precision != 8) VIO_FAIL("Unsupported JPEG data precision %d", precision);
    std::uint8_t h[18];
    if (!in_.read(h, 18)) VIO_FAIL("Premature end of input file");
    if (h[16] == 15) h[16] = 16;
    int idlen = h[0], cmaptype = h[1], subtype = h[2];
    unsigned maplen = get2(h, 5), w = get2(h, 12), ht = get2(h, 14);
    pixel_size_ = h[16] >> 3;
    int flags = h[17];
    bool bottom_up = (flags & 0x20) == 0;
    int interlace_type = flags >> 6;
    if (cmaptype > 1 || pixel_size_ < 1 || pixel_size_ > 4 || (h[16] & 7) != 0 ||
        interlace_type != 0 || w == 0 || ht == 0)
      VIO_FAIL("Invalid or unsupported Targa file");
    check_dimensions(w, ht);
    rle_ = subtype > 8;
    if (rle_) subtype -= 8;
    block_count_ = dup_count_ = 0;
    components = 3;
    color = ColorSpace::RGB;
    switch (subtype) {
      case 1:
        if (pixel_size_ != 1 || cmaptype != 1) VIO_FAIL("Invalid or unsupported Targa file");
        kind_ = 1;
        break;
      case 2:
        if (pixel_size_ < 2) VIO_FAIL("Invalid or unsupported Targa file");
        kind_ = 2;
        break;
      case 3:
        components = 1;
        color = ColorSpace::Gray;
        if (pixel_size_ != 1) VIO_FAIL("Invalid or unsupported Targa file");
        kind_ = 3;
        break;
      default:
        VIO_FAIL("Invalid or unsupported Targa file");
    }
    while (idlen--) read_byte();
    if (maplen > 0) {
      if (maplen > 256 || get2(h, 3) != 0) VIO_FAIL("Unsupported Targa colormap format");
      if (h[7] != 24) VIO_FAIL("Unsupported Targa colormap format");
      for (auto& c : cmap_) c.assign(maplen, 0);
      for (unsigned i = 0; i < maplen; i++) {
        cmap_[2][i] = static_cast<std::uint8_t>(read_byte());
        cmap_[1][i] = static_cast<std::uint8_t>(read_byte());
        cmap_[0][i] = static_cast<std::uint8_t>(read_byte());
      }
      cmap_length_ = static_cast<int>(maplen);
    } else {
      if (cmaptype) VIO_FAIL("Invalid or unsupported Targa file");
      cmap_length_ = 0;
    }
    width = w;
    height = ht;
    std::size_t row = static_cast<std::size_t>(w) * components;
    bottom_up_ = bottom_up;
    preloaded_ = false;
    if (!bottom_up) buf_.allocate(row, 1);
    row_ptr_ = bottom_up ? nullptr : buf_[0];
    buffer_ = &row_ptr_;
    buffer_height_ = 1;
  }

  unsigned get_rows() override {
    if (bottom_up_) {
      std::size_t row = static_cast<std::size_t>(width) * components;
      if (!preloaded_) {
        // Read the whole (bottom-up) image on the first call.
        whole_.resize(row * height);
        for (unsigned r = 0; r < height; r++) decode_row(whole_.data() + row * r);
        preloaded_ = true;
      }
      row_ptr_ = whole_.data() + row * (height - next_row - 1);
    } else {
      decode_row(row_ptr_);
    }
    return 1;
  }

 private:
  static constexpr std::uint8_t c5to8[32] = {0,   8,   16,  25,  33,  41,  49,  58,
                                             66,  74,  82,  90,  99,  107, 115, 123,
                                             132, 140, 148, 156, 165, 173, 181, 189,
                                             197, 206, 214, 222, 230, 239, 247, 255};
  static unsigned get2(const std::uint8_t* a, int o) { return a[o] + (unsigned(a[o + 1]) << 8); }
  int read_byte() {
    int c = in_.getc();
    if (c < 0) VIO_FAIL("Premature end of input file");
    return c;
  }
  void read_pixel() {
    if (rle_) {
      if (dup_count_ > 0) {
        dup_count_--;
        return;
      }
      if (--block_count_ < 0) {
        int i = read_byte();
        if (i & 0x80) {
          dup_count_ = i & 0x7F;
          block_count_ = 0;
        } else {
          block_count_ = i & 0x7F;
        }
      }
    }
    for (int i = 0; i < pixel_size_; i++) pixel_[i] = static_cast<std::uint8_t>(read_byte());
  }
  void decode_row(std::uint8_t* ptr) {
    for (unsigned col = 0; col < width; col++) {
      read_pixel();
      switch (kind_) {
        case 1: {
          int t = pixel_[0];
          if (t >= cmap_length_) VIO_FAIL("Invalid or unsupported Targa file");
          *ptr++ = cmap_[0][t];
          *ptr++ = cmap_[1][t];
          *ptr++ = cmap_[2][t];
          break;
        }
        case 2:
          if (pixel_size_ == 2) {
            int t = pixel_[0] + (pixel_[1] << 8);
            ptr[2] = c5to8[t & 0x1F];
            t >>= 5;
            ptr[1] = c5to8[t & 0x1F];
            t >>= 5;
            ptr[0] = c5to8[t & 0x1F];
            ptr += 3;
          } else {
            *ptr++ = pixel_[2];
            *ptr++ = pixel_[1];
            *ptr++ = pixel_[0];
          }
          break;
        default:
          *ptr++ = pixel_[0];
      }
    }
  }

  int pixel_size_ = 1, kind_ = 2;
  bool rle_ = false, preloaded_ = false, bottom_up_ = false;
  int block_count_ = 0, dup_count_ = 0;
  std::uint8_t pixel_[6] = {};
  std::vector<std::uint8_t> cmap_[3];
  int cmap_length_ = 0;
  std::vector<std::uint8_t> whole_;
  detail::RowBuffer<std::uint8_t> buf_;
  std::uint8_t* row_ptr_ = nullptr;
};

class Writer : public ImageWriter<std::uint8_t> {
 public:
  void write_rows(std::uint8_t** rows, unsigned n) override {
    for (unsigned r = 0; r < n; r++) {
      const std::uint8_t* in = rows[r];
      std::uint8_t* out = out_.grow(quantized() || color == ColorSpace::Gray
                                        ? width
                                        : static_cast<std::size_t>(width) * 3);
      if (color == ColorSpace::Gray) {
        if (quantized()) {
          for (unsigned c = 0; c < width; c++) out[c] = static_cast<std::uint8_t>(colormap[0][in[c]]);
        } else {
          std::memcpy(out, in, width);
        }
      } else if (quantized()) {
        std::memcpy(out, in, width);
      } else {
        for (unsigned c = 0; c < width; c++, in += 3, out += 3) {
          out[0] = in[2];
          out[1] = in[1];
          out[2] = in[0];
        }
      }
    }
  }

 protected:
  void start_output() override {
    if (precision != 8) VIO_FAIL("Unsupported JPEG data precision %d", precision);
    if (color == ColorSpace::Gray) {
      write_header(0);
    } else if (color == ColorSpace::RGB) {
      if (quantized()) {
        int num_colors = static_cast<int>(colormap[0].size());
        if (num_colors > 256)
          VIO_FAIL("Output file format cannot handle %d colormap entries", num_colors);
        write_header(num_colors);
        for (int i = 0; i < num_colors; i++) {
          out_.put(static_cast<std::uint8_t>(colormap[2][i]));
          out_.put(static_cast<std::uint8_t>(colormap[1][i]));
          out_.put(static_cast<std::uint8_t>(colormap[0][i]));
        }
      } else {
        write_header(0);
      }
    } else {
      VIO_FAIL("Targa output must be grayscale or RGB");
    }
  }

 private:
  void write_header(int num_colors) {
    std::uint8_t h[18] = {};
    if (num_colors > 0) {
      h[1] = 1;
      h[5] = static_cast<std::uint8_t>(num_colors & 0xFF);
      h[6] = static_cast<std::uint8_t>(num_colors >> 8);
      h[7] = 24;
    }
    h[12] = static_cast<std::uint8_t>(width & 0xFF);
    h[13] = static_cast<std::uint8_t>(width >> 8);
    h[14] = static_cast<std::uint8_t>(height & 0xFF);
    h[15] = static_cast<std::uint8_t>(height >> 8);
    h[17] = 0x20;
    if (color == ColorSpace::Gray) {
      h[2] = 3;
      h[16] = 8;
    } else if (num_colors > 0) {
      h[2] = 1;
      h[16] = 8;
    } else {
      h[2] = 2;
      h[16] = 24;
    }
    out_.put(h, 18);
  }
};

}  // namespace vio::targa
