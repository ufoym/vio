// vio::gif - GIF reader (first image, interlaced or not) and writer
// (LZW-compressed or uncompressed GIF87a).
#pragma once

#include "core.hpp"

namespace vio::gif {

class Reader : public ImageReader<std::uint8_t> {
 protected:
  static constexpr int kMaxLzwBits = 12;
  static constexpr int kLzwTableSize = 1 << kMaxLzwBits;

  void start_input() override {
    if (precision != 8) VIO_FAIL("Unsupported JPEG data precision %d", precision);
    std::uint8_t hdr[10];
    if (!in_.read(hdr, 6)) VIO_FAIL("Not a GIF file");
    if (hdr[0] != 'G' || hdr[1] != 'I' || hdr[2] != 'F') VIO_FAIL("Not a GIF file");
    if (!in_.read(hdr, 7)) VIO_FAIL("Premature end of input file");
    unsigned w = lm(hdr, 0), h = lm(hdr, 2);
    if (w == 0 || h == 0) VIO_FAIL("Empty GIF image");
    check_dimensions(w, h);
    for (auto& c : cmap_) c.assign(256, 0);
    int colormaplen = 0;
    if (hdr[4] & 0x80) {
      colormaplen = 2 << (hdr[4] & 0x07);
      read_colormap(colormaplen);
    }
    for (;;) {
      int c = read_byte();
      if (c == ';') VIO_FAIL("Too few images in GIF file");
      if (c == '!') {
        read_byte();
        skip_data_blocks();
        continue;
      }
      if (c != ',') {
        diag.warn(strformat("Bogus char 0x%02x in GIF file, ignoring", c));
        continue;
      }
      if (!in_.read(hdr, 9)) VIO_FAIL("Premature end of input file");
      w = lm(hdr, 4);
      h = lm(hdr, 6);
      if (w == 0 || h == 0) VIO_FAIL("Empty GIF image");
      check_dimensions(w, h);
      interlaced_ = (hdr[8] & 0x40) != 0;
      if (hdr[8] & 0x80) {
        colormaplen = 2 << (hdr[8] & 0x07);
        read_colormap(colormaplen);
      }
      input_code_size_ = read_byte();
      if (input_code_size_ < 2 || input_code_size_ > 8)
        VIO_FAIL("Bogus GIF codesize %d", input_code_size_);
      break;
    }
    symbol_head_.assign(kLzwTableSize, 0);
    symbol_tail_.assign(kLzwTableSize, 0);
    symbol_stack_.assign(kLzwTableSize, 0);
    init_lzw_code();
    if (color != ColorSpace::Gray) color = ColorSpace::RGB;
    components = color == ColorSpace::Gray ? 1 : 3;
    for (int c = colormaplen; c < clear_code_; c++) cmap_[0][c] = cmap_[1][c] = cmap_[2][c] = 128;
    width = w;
    height = h;
    buf_.allocate(static_cast<std::size_t>(w) * components, 1);
    row_ptr_ = buf_[0];
    buffer_ = &row_ptr_;
    buffer_height_ = 1;
    loaded_ = false;
  }

  unsigned get_rows() override {
    std::uint8_t* ptr = row_ptr_;
    const std::uint8_t* sptr = nullptr;
    if (interlaced_) {
      if (!loaded_) {
        image_.resize(static_cast<std::size_t>(width) * height);
        for (std::size_t i = 0; i < image_.size(); i++)
          image_[i] = static_cast<std::uint8_t>(lzw_read_byte());
        loaded_ = true;
        pass2_ = (height + 7) / 8;
        pass3_ = pass2_ + (height + 3) / 8;
        pass4_ = pass3_ + (height + 1) / 4;
      }
      unsigned r = next_row, irow;
      switch (r & 7) {
        case 0: irow = r >> 3; break;
        case 4: irow = (r >> 3) + pass2_; break;
        case 2: case 6: irow = (r >> 2) + pass3_; break;
        default: irow = (r >> 1) + pass4_;
      }
      sptr = image_.data() + static_cast<std::size_t>(irow) * width;
    }
    for (unsigned col = 0; col < width; col++) {
      int c = sptr ? sptr[col] : lzw_read_byte();
      *ptr++ = cmap_[0][c];
      if (color != ColorSpace::Gray) {
        *ptr++ = cmap_[1][c];
        *ptr++ = cmap_[2][c];
      }
    }
    return 1;
  }

 private:
  static unsigned lm(const std::uint8_t* a, int o) { return a[o] + (unsigned(a[o + 1]) << 8); }
  int read_byte() {
    int c = in_.getc();
    if (c < 0) VIO_FAIL("Premature end of input file");
    return c;
  }
  int get_data_block(std::uint8_t* buf) {
    int count = read_byte();
    if (count > 0 && !in_.read(buf, static_cast<std::size_t>(count)))
      VIO_FAIL("Premature end of input file");
    return count;
  }
  void skip_data_blocks() {
    while (int count = read_byte())
      if (!in_.take(static_cast<std::size_t>(count))) VIO_FAIL("Premature end of input file");
  }
  void read_colormap(int len) {
    bool gray = true;
    for (int i = 0; i < len; i++) {
      cmap_[0][i] = static_cast<std::uint8_t>(read_byte());
      cmap_[1][i] = static_cast<std::uint8_t>(read_byte());
      cmap_[2][i] = static_cast<std::uint8_t>(read_byte());
      if (cmap_[0][i] != cmap_[1][i] || cmap_[1][i] != cmap_[2][i]) gray = false;
    }
    if (color == ColorSpace::RGB && gray) color = ColorSpace::Gray;
  }
  void reinit_lzw() {
    code_size_ = input_code_size_ + 1;
    limit_code_ = clear_code_ << 1;
    max_code_ = clear_code_ + 2;
    sp_ = 0;
  }
  void init_lzw_code() {
    last_byte_ = 2;
    code_buf_[0] = code_buf_[1] = 0;
    last_bit_ = 0;
    cur_bit_ = 0;
    first_time_ = true;
    out_of_blocks_ = false;
    clear_code_ = 1 << input_code_size_;
    end_code_ = clear_code_ + 1;
    reinit_lzw();
  }
  int get_code() {
    while (cur_bit_ + code_size_ > last_bit_) {
      if (first_time_) {
        first_time_ = false;
        return clear_code_;
      }
      if (out_of_blocks_) {
        diag.warn("Ran out of GIF bits");
        return end_code_;
      }
      code_buf_[0] = code_buf_[last_byte_ - 2];
      code_buf_[1] = code_buf_[last_byte_ - 1];
      int count = get_data_block(&code_buf_[2]);
      if (count == 0) {
        out_of_blocks_ = true;
        diag.warn("Ran out of GIF bits");
        return end_code_;
      }
      cur_bit_ = (cur_bit_ - last_bit_) + 16;
      last_byte_ = 2 + count;
      last_bit_ = last_byte_ * 8;
    }
    int offs = cur_bit_ >> 3;
    int accum = code_buf_[offs + 2];
    accum <<= 8;
    accum |= code_buf_[offs + 1];
    accum <<= 8;
    accum |= code_buf_[offs];
    accum >>= (cur_bit_ & 7);
    cur_bit_ += code_size_;
    return accum & ((1 << code_size_) - 1);
  }
  int lzw_read_byte() {
    if (sp_ > 0) return symbol_stack_[--sp_];
    int code = get_code();
    if (code == clear_code_) {
      reinit_lzw();
      do {
        code = get_code();
      } while (code == clear_code_);
      if (code > clear_code_) {
        diag.warn("Corrupt data in GIF file");
        code = 0;
      }
      firstcode_ = oldcode_ = code;
      return code;
    }
    if (code == end_code_) {
      if (!out_of_blocks_) {
        skip_data_blocks();
        out_of_blocks_ = true;
      }
      diag.warn("Premature end of GIF image");
      return 0;
    }
    int incode = code;
    if (code >= max_code_) {
      if (code > max_code_) {
        diag.warn("Corrupt data in GIF file");
        incode = 0;
      }
      symbol_stack_[sp_++] = static_cast<std::uint8_t>(firstcode_);
      code = oldcode_;
    }
    while (code >= clear_code_) {
      symbol_stack_[sp_++] = symbol_tail_[code];
      code = symbol_head_[code];
    }
    firstcode_ = code;
    if ((code = max_code_) < kLzwTableSize) {
      symbol_head_[code] = static_cast<std::uint16_t>(oldcode_);
      symbol_tail_[code] = static_cast<std::uint8_t>(firstcode_);
      max_code_++;
      if (max_code_ >= limit_code_ && code_size_ < kMaxLzwBits) {
        code_size_++;
        limit_code_ <<= 1;
      }
    }
    oldcode_ = incode;
    return firstcode_;
  }

  std::vector<std::uint8_t> cmap_[3];
  std::uint8_t code_buf_[256 + 4] = {};
  int last_byte_ = 0, last_bit_ = 0, cur_bit_ = 0;
  bool first_time_ = true, out_of_blocks_ = false;
  int input_code_size_ = 0, clear_code_ = 0, end_code_ = 0, code_size_ = 0, limit_code_ = 0,
      max_code_ = 0, oldcode_ = 0, firstcode_ = 0;
  std::vector<std::uint16_t> symbol_head_;
  std::vector<std::uint8_t> symbol_tail_, symbol_stack_;
  std::size_t sp_ = 0;
  bool interlaced_ = false, loaded_ = false;
  std::vector<std::uint8_t> image_;
  unsigned pass2_ = 0, pass3_ = 0, pass4_ = 0;
  detail::RowBuffer<std::uint8_t> buf_;
  std::uint8_t* row_ptr_ = nullptr;
};

// GIF writer.  Rows contain colormap indexes (when `colormap` is set) or
// 8-bit grayscale values.  The JPEG decoder must be configured with
// gif_decoder_requirements() first (GIF output requires quantization of
// color images).
template <class T>
class Writer : public ImageWriter<T> {
 public:
  bool is_lzw = true;

  void write_rows(T** rows, unsigned n) override {
    for (unsigned r = 0; r < n; r++) {
      const T* ptr = rows[r];
      for (unsigned col = 0; col < this->width; col++) {
        int c = static_cast<int>(ptr[col]);
        if (is_lzw) put_lzw(c);
        else put_raw(c);
      }
    }
  }

 protected:
  static constexpr int kMaxLzwBits = 12;
  static constexpr int kLzwTableSize = 1 << kMaxLzwBits;
  static constexpr int kHSize = 5003;

  void start_output() override {
    if (this->precision != detail::max_precision<T>())
      VIO_FAIL("Unsupported JPEG data precision %d", this->precision);
    if (this->color != ColorSpace::Gray && this->color != ColorSpace::RGB)
      VIO_FAIL("GIF output must be grayscale or RGB");
    if (this->quantized())
      emit_header(static_cast<int>(this->colormap[0].size()), true);
    else
      emit_header(256, false);
  }

  void finish_output() override {
    compress_term();
    this->out_.put(0);
    this->out_.put(';');
  }

 private:
  static int maxcode_of(int n) { return (1 << n) - 1; }

  void put_word(unsigned w) {
    this->out_.put(static_cast<std::uint8_t>(w));
    this->out_.put(static_cast<std::uint8_t>(w >> 8));
  }
  void put3(int v) {
    for (int i = 0; i < 3; i++) this->out_.put(static_cast<std::uint8_t>(v));
  }

  void emit_header(int num_colors, bool have_map) {
    int cshift = this->precision - 8;
    if (num_colors > 256) VIO_FAIL("Output file format cannot handle %d colormap entries", num_colors);
    int bpp = 1;
    while (num_colors > (1 << bpp)) bpp++;
    int cmsize = 1 << bpp;
    int init_code_size = bpp <= 1 ? 2 : bpp;
    this->out_.put_str("GIF87a");
    put_word(this->width);
    put_word(this->height);
    int flag = 0x80 | ((bpp - 1) << 4) | (bpp - 1);
    this->out_.put(static_cast<std::uint8_t>(flag));
    this->out_.put(0);
    this->out_.put(0);
    const int center = 1 << (this->precision - 1);
    for (int i = 0; i < cmsize; i++) {
      if (i < num_colors) {
        if (have_map) {
          const auto& cm = this->colormap;
          if (this->color == ColorSpace::RGB) {
            this->out_.put(static_cast<std::uint8_t>(cm[0][i] >> cshift));
            this->out_.put(static_cast<std::uint8_t>(cm[1][i] >> cshift));
            this->out_.put(static_cast<std::uint8_t>(cm[2][i] >> cshift));
          } else {
            put3(cm[0][i] >> cshift);
          }
        } else {
          put3((i * 255 + (num_colors - 1) / 2) / (num_colors - 1));
        }
      } else {
        put3(center >> cshift);
      }
    }
    this->out_.put(',');
    put_word(0);
    put_word(0);
    put_word(this->width);
    put_word(this->height);
    this->out_.put(0x00);
    this->out_.put(static_cast<std::uint8_t>(init_code_size));
    compress_init(init_code_size + 1);
  }

  void flush_packet() {
    if (bytesinpkt_ > 0) {
      packetbuf_[0] = static_cast<std::uint8_t>(bytesinpkt_++);
      this->out_.put(packetbuf_, static_cast<std::size_t>(bytesinpkt_));
      bytesinpkt_ = 0;
    }
  }
  void char_out(int c) {
    packetbuf_[++bytesinpkt_] = static_cast<std::uint8_t>(c);
    if (bytesinpkt_ >= 255) flush_packet();
  }
  void output(int code) {
    cur_accum_ |= static_cast<long>(code) << cur_bits_;
    cur_bits_ += n_bits_;
    while (cur_bits_ >= 8) {
      char_out(static_cast<int>(cur_accum_ & 0xFF));
      cur_accum_ >>= 8;
      cur_bits_ -= 8;
    }
    if (free_code_ > maxcode_) {
      n_bits_++;
      if (n_bits_ == kMaxLzwBits) maxcode_ = kLzwTableSize;
      else maxcode_ = maxcode_of(n_bits_);
    }
  }
  void clear_hash() { std::fill(hash_code_.begin(), hash_code_.end(), 0); }
  void clear_block() {
    clear_hash();
    free_code_ = clear_code_ + 2;
    output(clear_code_);
    n_bits_ = init_bits_;
    maxcode_ = maxcode_of(n_bits_);
  }
  void compress_init(int i_bits) {
    n_bits_ = init_bits_ = i_bits;
    maxcode_ = maxcode_of(n_bits_);
    clear_code_ = 1 << (i_bits - 1);
    eof_code_ = clear_code_ + 1;
    code_counter_ = free_code_ = clear_code_ + 2;
    first_byte_ = true;
    bytesinpkt_ = 0;
    cur_accum_ = 0;
    cur_bits_ = 0;
    if (is_lzw) {
      hash_code_.assign(kHSize, 0);
      hash_value_.assign(kHSize, 0);
    }
    output(clear_code_);
  }
  void compress_term() {
    if (!first_byte_) output(waiting_code_);
    output(eof_code_);
    if (cur_bits_ > 0) char_out(static_cast<int>(cur_accum_ & 0xFF));
    flush_packet();
  }
  void put_lzw(int c) {
    if (first_byte_) {
      waiting_code_ = c;
      first_byte_ = false;
      return;
    }
    int i = (c << (kMaxLzwBits - 8)) + waiting_code_;
    if (i >= kHSize) i -= kHSize;
    int probe = (waiting_code_ << 8) | c;
    auto miss = [&] {
      output(waiting_code_);
      if (free_code_ < kLzwTableSize) {
        hash_code_[i] = static_cast<std::int16_t>(free_code_++);
        hash_value_[i] = probe;
      } else {
        clear_block();
      }
      waiting_code_ = c;
    };
    if (hash_code_[i] == 0) return miss();
    if (hash_value_[i] == probe) {
      waiting_code_ = hash_code_[i];
      return;
    }
    int disp = i == 0 ? 1 : kHSize - i;
    for (;;) {
      i -= disp;
      if (i < 0) i += kHSize;
      if (hash_code_[i] == 0) return miss();
      if (hash_value_[i] == probe) {
        waiting_code_ = hash_code_[i];
        return;
      }
    }
  }
  void put_raw(int c) {
    output(c);
    if (code_counter_ < maxcode_) {
      code_counter_++;
    } else {
      output(clear_code_);
      code_counter_ = clear_code_ + 2;
    }
  }

  int n_bits_ = 0, maxcode_ = 0, init_bits_ = 0;
  long cur_accum_ = 0;
  int cur_bits_ = 0;
  int waiting_code_ = 0;
  bool first_byte_ = true;
  int clear_code_ = 0, eof_code_ = 0, free_code_ = 0, code_counter_ = 0;
  std::vector<std::int16_t> hash_code_;
  std::vector<int> hash_value_;
  int bytesinpkt_ = 0;
  std::uint8_t packetbuf_[256] = {};
};

}  // namespace vio::gif
