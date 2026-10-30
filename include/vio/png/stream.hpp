// vio::png - progressive decoding.
//
// png::StreamDecoder takes a PNG file in pieces of any size, as they arrive
// (from a network, say), and delivers rows as soon as they can be decoded:
//
//   png::StreamDecoder s;
//   s.on_info = [&] { /* s.info() is complete up to the image data; set
//                        s.transforms here */ };
//   s.on_row = [&](std::uint32_t y, int pass, const std::uint8_t* row) { ... };
//   s.on_end = [&] { ... };
//   while (n = read(buf)) s.feed(buf, n);
//
// Rows are in the output format (output(), after `transforms`; with none,
// the native layout of png::Decoder).  Interlaced images give the rows of
// each pass as they come, holding that pass's pixels only (pass 0..6;
// non-interlaced images give pass -1): combine_row() places them in a
// full row.  APNG frames after the default image follow, each announced by
// on_frame and given row by row the same way.
//
// Chunks are checked and stored in info() as by png::Decoder, with the same
// options.  The image data is decompressed as it comes, so that memory use
// is a few rows plus the largest non-image chunk.
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "decoder.hpp"

namespace vio::png {

class StreamDecoder {
 public:
  ReadOptions options;
  Transforms transforms;
  // The chunks before the image data have been read (info() complete up to
  // there); transforms may still be changed.
  std::function<void()> on_info;
  // A row of the image (or frame) being decoded: its row number in the
  // image, the interlace pass (-1 if not interlaced) and the samples.
  std::function<void(std::uint32_t y, int pass, const std::uint8_t* row)> on_row;
  // APNG: frame `index` (counting the default image if it is a frame)
  // starts; its rows follow.
  std::function<void(std::size_t index, const FrameControl& fc)> on_frame;
  // IEND has been read.
  std::function<void()> on_end;

  StreamDecoder() = default;
  StreamDecoder(const StreamDecoder&) = delete;
  StreamDecoder& operator=(const StreamDecoder&) = delete;

  // Consumes the next piece of the file.  Throws vio::Error on errors (the
  // decoder cannot continue after one).
  void feed(const void* data, std::size_t n) {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    const std::uint8_t* const end = p + n;
    if (state_ == kStart) start();
    while (p < end && state_ != kEnd) {
      switch (state_) {
        case kSignature:
          if (!gather(p, end, 8)) return;
          if (std::memcmp(buf_.data(), detail::signature, 8) != 0) {
            if (std::memcmp(buf_.data(), detail::signature, 4) == 0) fail("PNG file corrupted by ASCII conversion");
            fail("Not a PNG file");
          }
          buf_.clear();
          state_ = kHeader;
          break;
        case kHeader:
          if (!gather(p, end, 8)) return;
          begin_chunk();
          break;
        case kBody:
          if (!gather(p, end, 12 + static_cast<std::size_t>(len_))) return;
          end_chunk();
          break;
        case kData: {
          // image data: straight to the decompressor
          std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(left_, end - p));
          if (seq_left_) {
            k = std::min<std::size_t>(k, seq_left_);
            buf_.insert(buf_.end(), p, p + k);
            seq_left_ -= static_cast<unsigned>(k);
            if (!seq_left_) {
              d_.chunk_type_ = type_.data();
              d_.check_sequence(buf_.data() + 8);
            }
          } else if (skip_) {
            // dropped
          } else {
            image_->feed(p, k);
          }
          crc_ = zlib::crc32(crc_, p, k);
          p += k;
          left_ -= k;
          if (left_ == 0) {
            buf_.clear();
            state_ = kCrc;
          }
          break;
        }
        case kCrc:
          if (!gather(p, end, 4)) return;
          end_data_chunk();
          break;
        case kSkip: {
          const std::size_t k = static_cast<std::size_t>(std::min<std::uint64_t>(left_, end - p));
          p += k;
          left_ -= k;
          if (left_ == 0) {
            buf_.clear();
            state_ = kHeader;
          }
          break;
        }
        default: return;
      }
    }
  }

  // IEND has been read.
  bool done() const { return state_ == kEnd; }
  const Info& info() const { return d_.info; }
  const std::vector<std::string>& warnings() const { return d_.warnings; }
  // The format of the rows given to on_row (from on_info on; the width is
  // that of the whole image).
  const RowInfo& output() const { return plan_.output(); }
  std::size_t output_row_bytes(std::uint32_t width) const {
    return plan_.output_row_bytes(width);
  }
  // The palette of palette output.
  std::vector<PaletteEntry> output_palette() const { return plan_.palette(); }

  // Places the pixels of a row of interlace pass `pass` (output format) in
  // a full row of the image (or frame) `width` pixels wide.
  void combine_row(std::uint8_t* row, const std::uint8_t* pass_row, int pass, std::uint32_t width) const {
    using namespace detail;
    const RowInfo& o = plan_.output();
    const unsigned bits = static_cast<unsigned>(o.channels * o.bit_depth);
    if (pass < 0) {
      std::memcpy(row, pass_row, output_row_bytes(width));
      return;
    }
    const std::uint32_t x0 = adam7_x_start[pass], dx = adam7_x_delta[pass];
    for (std::uint32_t x = x0, i = 0; x < width; x += dx, i++) {
      if (bits >= 8) {
        std::memcpy(row + static_cast<std::size_t>(x) * (bits / 8), pass_row + static_cast<std::size_t>(i) * (bits / 8),
                    bits / 8);
      } else {
        const unsigned spb = 8 / bits, mask = (1u << bits) - 1;
        const unsigned v = (pass_row[i / spb] >> (8 - bits - (i % spb) * bits)) & mask;
        const unsigned shift = 8 - bits - (x % spb) * bits;
        row[x / spb] = static_cast<std::uint8_t>((row[x / spb] & ~(mask << shift)) | v << shift);
      }
    }
  }

 private:
  enum State { kStart, kSignature, kHeader, kBody, kData, kCrc, kSkip, kEnd };

  // Rows of one image (the default image or a frame) as they decompress.
  class ImageStream {
   public:
    ImageStream(StreamDecoder& s, std::uint32_t w, std::uint32_t h) : s_(s), width_(w), height_(h) {
      const Header& hd = s.d_.info.header;
      bits_ = static_cast<unsigned>(num_channels(hd.color_type) * hd.bit_depth);
      bpp_ = bits_ < 8 ? 1u : bits_ / 8;
      interlace_ = hd.interlace != 0;
      std::uint64_t total = 0;
      for (int p = 0; p < (interlace_ ? 7 : 1); p++) {
        const std::uint32_t pw = pass_width(p), ph = pass_height(p);
        if (pw && ph) total += static_cast<std::uint64_t>(ph) * (row_bytes(pw) + 1);
      }
      pass_ = -1;
      next_pass();
      inf_ = std::make_unique<zlib::StreamInflater>(
          [this](const std::uint8_t* d, std::size_t n) { take(d, n); }, false, s.d_.options.verify_adler,
          total + 1);
      out_.resize(s.output_row_bytes(w) + s.plan_.max_row_bytes(w));
    }

    void feed(const std::uint8_t* p, std::size_t n) {
      try {
        inf_->feed(p, n);
      } catch (const Error& e) {
        VIO_FAIL("IDAT: %s", Decoder::zlib_error(e));
      }
    }
    bool complete() const { return pass_ >= 7 || (!interlace_ && pass_ > 0); }

    // The image data ended: reports what is missing or left over.
    void finish() {
      if (!complete()) fail("Not enough image data");
      if (!inf_->stream_end()) s_.d_.benign_error_text("IDAT: Too much image data");
      else if (inf_->extra_input()) s_.d_.benign_error_text("IDAT: Extra compressed data");
    }

   private:
    std::size_t row_bytes(std::uint32_t w) const { return detail::packed_row_bytes(w, bits_); }
    std::uint32_t pass_width(int p) const {
      if (!interlace_) return width_;
      using namespace detail;
      return adam7_extent(width_, adam7_x_start[p], adam7_x_delta[p]);
    }
    std::uint32_t pass_height(int p) const {
      if (!interlace_) return height_;
      using namespace detail;
      return adam7_extent(height_, adam7_y_start[p], adam7_y_delta[p]);
    }
    // Moves to the next pass with pixels (pass_ 7, or 1 when not
    // interlaced, once all are done).
    void next_pass() {
      if (!interlace_) {
        if (pass_ < 0) {
          pass_ = 0;
        } else {
          pass_ = 1;
          return;
        }
      } else {
        do pass_++;
        while (pass_ < 7 && (!pass_width(pass_) || !pass_height(pass_)));
        if (pass_ >= 7) return;
      }
      pw_ = pass_width(pass_);
      ph_ = pass_height(pass_);
      rb_ = row_bytes(pw_);
      cur_.assign(rb_ + 1 + 32, 0);
      prev_.assign(rb_ + 32, 0);
      have_prev_ = false;
      filled_ = 0;
      y_ = 0;
    }

    void take(const std::uint8_t* d, std::size_t n) {
      while (n && !complete()) {
        const std::size_t k = std::min(n, rb_ + 1 - filled_);
        std::memcpy(cur_.data() + filled_, d, k);
        filled_ += k;
        d += k;
        n -= k;
        if (filled_ == rb_ + 1) row();
      }
    }

    void row() {
      const unsigned filter = cur_[0];
      if (filter > 4) fail("bad adaptive filter value");
      // reconstruct into prev_ (which then holds this row)
      std::vector<std::uint8_t> out(rb_ + 32);
      detail::unfilter_row(filter, out.data(), cur_.data() + 1, have_prev_ ? prev_.data() : nullptr, rb_, bpp_);
      if (const unsigned used = (static_cast<std::size_t>(pw_) * bits_) % 8)
        out[rb_ - 1] &= static_cast<std::uint8_t>(0xff00u >> used);
      prev_.swap(out);
      have_prev_ = true;
      filled_ = 0;
      const std::uint32_t y =
          interlace_ ? detail::adam7_y_start[pass_] + y_ * static_cast<std::uint32_t>(detail::adam7_y_delta[pass_]) : y_;
      s_.plan_.run(prev_.data(), pw_, out_.data());
      if (s_.on_row) s_.on_row(y, interlace_ ? pass_ : -1, out_.data());
      if (++y_ == ph_) next_pass();
    }

    StreamDecoder& s_;
    std::uint32_t width_, height_;
    unsigned bits_ = 8, bpp_ = 1;
    bool interlace_ = false, have_prev_ = false;
    int pass_ = 0;
    std::uint32_t pw_ = 0, ph_ = 0, y_ = 0;
    std::size_t rb_ = 0, filled_ = 0;
    std::vector<std::uint8_t> cur_, prev_, out_;
    std::unique_ptr<zlib::StreamInflater> inf_;
  };

  void start() {
    d_.options = options;
    d_.reset({});
    state_ = kSignature;
  }

  // Appends input to buf_ until it holds n bytes.
  bool gather(const std::uint8_t*& p, const std::uint8_t* end, std::size_t n) {
    if (buf_.size() < n) {
      const std::size_t k = std::min<std::size_t>(n - buf_.size(), static_cast<std::size_t>(end - p));
      buf_.insert(buf_.end(), p, p + k);
      p += k;
    }
    return buf_.size() >= n;
  }

  // The chunk header is in buf_.
  void begin_chunk() {
    using namespace detail;
    if (d_.mode_ & kModeIEND) return;
    d_.check_chunk_header(buf_.data());
    len_ = read_u32(buf_.data());
    std::memcpy(type_.data(), buf_.data() + 4, 4);
    d_.chunk_type_ = type_.data();
    if (d_.mode_ == 0 && std::memcmp(type_.data(), "IHDR", 4) != 0) fail("missing IHDR");
    const int index = d_.handling_index(type_.data());
    if (index == kIDAT || index == kFDAT) {
      // Image data is decompressed as it arrives; the CRC is checked at the
      // end of the chunk.
      crc_ = zlib::crc32(0, type_.data(), 4);
      left_ = len_;
      skip_ = !d_.check_rules(index, len_);
      seq_left_ = 0;
      data_index_ = index;
      if (!skip_ && index == kIDAT) {
        if (d_.info.header.color_type == kIndexed && !(d_.mode_ & kModePLTE)) d_.chunk_error("missing PLTE");
        if (!(d_.mode_ & kModeIDAT)) {
          d_.mode_ |= kModeIDAT;
          begin_image();
        }
        d_.seen_ |= 1u << kIDAT;
      } else if (!skip_) {
        if (!fdat_ok_) fail("fdAT without fcTL");
        seq_left_ = 4;
        d_.seen_ |= 1u << kFDAT;
      }
      if (len_ == 0) {
        buf_.clear();
        state_ = kCrc;
      } else {
        state_ = kData;
      }
      buf_.resize(8);
      return;
    }
    if (len_ > options_limit()) fail("chunk data is too large");
    state_ = kBody;
  }

  std::size_t options_limit() const {
    // no non-image chunk may exceed the larger of the ancillary limit and
    // what a palette or text chunk needs
    return std::max<std::size_t>(d_.options.max_chunk_size, 1u << 20) + 64;
  }

  // A whole non-image chunk is in buf_.
  void end_chunk() {
    using namespace detail;
    const std::uint32_t mode = d_.mode_;
    const std::size_t frames = d_.info.frames.size();
    if ((mode & kModeIDAT) && !(mode & kModeAfterIDAT)) {
      const int index = d_.handling_index(type_.data());
      if (index != kIDAT && index != kFDAT && index != kFCTL) end_image_data();
    }
    d_.process_chunk(buf_.data());
    buf_.clear();
    state_ = kHeader;
    if (d_.info.frames.size() > frames && (d_.mode_ & kModeIDAT)) {
      // fcTL of a frame after the default image
      end_image_data();
      begin_frame();
    }
    if (d_.mode_ & kModeIEND) {
      if (!info_done_) fail("missing IDAT");
      if (image_) end_image_data();
      state_ = kEnd;
      if (on_end) on_end();
    }
  }

  void end_data_chunk() {
    using namespace detail;
    const bool critical = is_critical(type_.data());
    d_.chunk_type_ = type_.data();
    const CrcAction action = critical ? d_.options.critical_crc : d_.options.ancillary_crc;
    if (action != CrcAction::kQuietUse && crc_ != read_u32(buf_.data())) d_.crc_mismatch(critical);
    if (len_ < 4 && data_index_ == kFDAT && !skip_) d_.chunk_error("too short");
    buf_.clear();
    state_ = kHeader;
  }

  // The first IDAT: the transformations are set, rows can start.
  void begin_image() {
    info_done_ = true;
    if (on_info) on_info();
    plan_.init(d_.info, transforms, [this](const std::string& m) { d_.warn(m); });
    if (d_.info.default_image_is_frame && on_frame) on_frame(0, d_.info.frames[0]);
    frame_ = d_.info.default_image_is_frame ? 1 : 0;
    image_ = std::make_unique<ImageStream>(*this, d_.info.header.width, d_.info.header.height);
  }

  void begin_frame() {
    const FrameControl& fc = d_.info.frames.back();
    if (on_frame) on_frame(frame_, fc);
    frame_++;
    fdat_ok_ = true;
    image_ = std::make_unique<ImageStream>(*this, fc.width, fc.height);
  }

  void end_image_data() {
    if (image_) image_->finish();
    image_.reset();
    fdat_ok_ = false;
  }

  Decoder d_;
  detail::TransformPlan plan_;
  State state_ = kStart;
  std::vector<std::uint8_t> buf_;
  std::array<std::uint8_t, 4> type_{};
  std::uint32_t len_ = 0, crc_ = 0;
  std::uint64_t left_ = 0;
  unsigned seq_left_ = 0;
  int data_index_ = 0;
  bool skip_ = false, info_done_ = false, fdat_ok_ = false;
  std::size_t frame_ = 0;
  std::unique_ptr<ImageStream> image_;
};

}  // namespace vio::png
