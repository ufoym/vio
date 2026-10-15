// vio::png - PNG decoder.
//
// png::Decoder reads every standard chunk into png::Info (png/info.hpp) and
// decodes all standard PNG variants (all bit depths and color types, Adam7
// interlacing, APNG frames) into the PNG's native sample layout.
//
// Chunk handling follows ReadOptions: CRC errors, recoverable ("benign")
// errors in ancillary chunks, size limits, and which unknown chunks to keep.
// Problems that do not stop decoding are reported as warnings
// (Decoder::warnings, and ReadOptions::on_warning).
//
// A non-interlaced image is inflated straight into the image buffer, filter
// bytes included, and each row is then reconstructed in place, moving left
// over the filter bytes of the rows above it.
#pragma once

#include <array>
#include <cstdio>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "common.hpp"
#include "filter.hpp"
#include "inflate.hpp"
#include "info.hpp"
#include "transform.hpp"

namespace vio::png {

// What to do with a chunk whose CRC is wrong.  Critical chunks accept
// kError, kWarnUse and kQuietUse.
enum class CrcAction : std::uint8_t { kError, kWarnDiscard, kQuietDiscard, kWarnUse, kQuietUse };

// Which chunks the decoder does not interpret to keep in Info::unknown.
enum class ChunkKeep : std::uint8_t {
  kDefault,  // per-chunk entries: use ReadOptions::keep_unknown
  kNever,
  kIfSafe,  // ancillary chunks only
  kAlways,
};

struct ReadOptions {
  CrcAction critical_crc = CrcAction::kError;
  CrcAction ancillary_crc = CrcAction::kWarnDiscard;
  bool verify_adler = true;
  // Recoverable errors (an invalid ancillary chunk, extra image data) are
  // warnings; if false they are errors.
  bool benign_errors_warn = true;
  std::uint32_t max_width = 1000000, max_height = 1000000;
  // Text, sPLT and unknown chunks kept in Info, at most (0 = no limit).
  std::uint32_t max_cached_chunks = 1000;
  // Largest ancillary chunk, or decompressed text or profile, accepted.
  std::size_t max_chunk_size = 8000000;
  ChunkKeep keep_unknown = ChunkKeep::kNever;
  // Per chunk type; a known ancillary chunk listed here is not interpreted
  // but treated as unknown.
  std::vector<std::pair<std::array<char, 4>, ChunkKeep>> keep;
  // Called for each chunk not interpreted: return > 0 if handled (it is then
  // not kept), 0 to apply the keep rules (an unhandled chunk is kept at
  // least if safe to copy), < 0 to fail.
  std::function<int(const UnknownChunk&)> user_chunk;
  std::function<void(const std::string&)> on_warning;
};

// A decoded APNG frame (sub-image) in native sample layout.
struct Frame {
  FrameControl control;
  std::size_t row_bytes = 0;
  std::vector<std::uint8_t> pixels;
  const std::uint8_t* row(std::uint32_t y) const { return pixels.data() + y * row_bytes; }
};

namespace detail {

// Chunk types the decoder interprets, with their placement rules.
enum ChunkIndex : int {
  kIHDR, kPLTE, kIDAT, kIEND, kTRNS, kCHRM, kGAMA, kICCP, kSBIT, kSRGB, kCICP, kMDCV, kEXIF,
  kCLLI, kTEXT, kZTXT, kITXT, kBKGD, kHIST, kPHYS, kSPLT, kTIME, kACTL, kFCTL, kFDAT, kOFFS,
  kPCAL, kSCAL, kChunkCount, kUnknown = kChunkCount
};

enum : std::uint32_t {
  kModeIHDR = 0x01, kModePLTE = 0x02, kModeIDAT = 0x04, kModeAfterIDAT = 0x08, kModeIEND = 0x10,
  kModeACTL = 0x20, kModeFCTL = 0x40,
};

struct ChunkRule {
  char type[5];
  std::uint32_t max_len;  // kNoCheck: none; kLimit: ReadOptions::max_chunk_size
  std::uint32_t min_len;
  std::uint32_t not_after;  // mode bits that must not be set
  std::uint32_t needs;      // mode bits that must be set
  bool multiple;
};
inline constexpr std::uint32_t kNoCheck = 0xffffffffu, kLimit = 0xfffffffeu;
inline constexpr std::uint32_t kColorChunk = kModePLTE | kModeIDAT;
inline constexpr ChunkRule chunk_rules[kChunkCount] = {
    {"IHDR", 13, 13, kModeIHDR, 0, false},
    {"PLTE", kNoCheck, 0, 0, kModeIHDR, true},
    {"IDAT", kNoCheck, 0, kModeAfterIDAT, kModeIHDR, true},
    {"IEND", kNoCheck, 0, 0, kModeAfterIDAT, false},
    {"tRNS", 256, 0, kModeIDAT, kModeIHDR, false},
    {"cHRM", 32, 32, kColorChunk, kModeIHDR, false},
    {"gAMA", 4, 4, kColorChunk, kModeIHDR, false},
    {"iCCP", kNoCheck, 14, kColorChunk, kModeIHDR, false},
    {"sBIT", 4, 1, kColorChunk, kModeIHDR, false},
    {"sRGB", 1, 1, kColorChunk, kModeIHDR, false},
    {"cICP", 4, 4, kColorChunk, kModeIHDR, false},
    {"mDCV", 24, 24, kColorChunk, kModeIHDR, false},
    {"eXIf", kLimit, 4, 0, kModeIHDR, false},
    {"cLLI", 8, 8, kColorChunk, kModeIHDR, false},
    {"tEXt", kNoCheck, 2, 0, kModeIHDR, true},
    {"zTXt", kLimit, 14, 0, kModeIHDR, true},
    {"iTXt", kNoCheck, 6, 0, kModeIHDR, true},
    {"bKGD", 6, 1, kModeIDAT, kModeIHDR, false},
    {"hIST", 1024, 0, kModeIDAT, kModePLTE, false},
    {"pHYs", 9, 9, kModeIDAT, kModeIHDR, false},
    {"sPLT", kNoCheck, 3, kModeIDAT, kModeIHDR, true},
    {"tIME", 7, 7, 0, kModeIHDR, false},
    {"acTL", 8, 8, kModeIDAT, kModeIHDR, false},
    {"fcTL", 26, 26, 0, kModeIHDR, true},
    {"fdAT", kNoCheck, 4, 0, kModeIHDR, true},
    {"oFFs", 9, 9, kModeIDAT, kModeIHDR, false},
    {"pCAL", kNoCheck, 14, kModeIDAT, kModeIHDR, false},
    {"sCAL", kLimit, 4, kModeIDAT, kModeIHDR, false},
};

inline int chunk_index(const std::uint8_t* type) {
  for (int i = 0; i < kChunkCount; i++)
    if (std::memcmp(type, chunk_rules[i].type, 4) == 0) return i;
  return kUnknown;
}

inline bool is_critical(const std::uint8_t* type) { return (type[0] & 0x20) == 0; }

inline std::uint16_t read_u16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] << 8 | p[1]);
}
inline std::int32_t read_i32(const std::uint8_t* p) {
  return static_cast<std::int32_t>(read_u32(p));
}

}  // namespace detail

class StreamDecoder;
class FormatReader;

class Decoder {
  friend class StreamDecoder;
  friend class FormatReader;

 public:
  ReadOptions options;
  Info info;
  std::vector<std::string> warnings;
  // Old names, kept as references into info.
  Header& header = info.header;
  std::vector<PaletteEntry>& palette = info.palette;

  Decoder() = default;
  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  // Parses the signature and IHDR chunk.
  void read_header(std::span<const std::uint8_t> file) {
    reset(file);
    if (file.size() < 8 || std::memcmp(file.data(), detail::signature, 8) != 0) {
      if (file.size() >= 8 && std::memcmp(file.data(), detail::signature, 4) == 0)
        fail("PNG file corrupted by ASCII conversion");
      fail("Not a PNG file");
    }
    pos_ = 8;
    if (file.size() < 16 || std::memcmp(file.data() + 12, "IHDR", 4) != 0)
      fail("missing IHDR");
    handle_next_chunk();
  }

  // Parses the chunks that precede the image data.
  void read_info(std::span<const std::uint8_t> file) {
    if (!(mode_ & detail::kModeIHDR) || file.data() != file_.data()) read_header(file);
    while (!(mode_ & detail::kModeIDAT)) handle_next_chunk();
  }

  // Parses the remaining chunks and decodes the image.  Samples are stored
  // in native PNG layout, rows of row_bytes() bytes, 16-bit samples in host
  // byte order, sub-byte samples packed MSB first.
  void decode(std::span<const std::uint8_t> file, unsigned long long max_pixels = 0) {
    read_info(file);
    if (max_pixels && static_cast<unsigned long long>(header.width) * header.height > max_pixels)
      VIO_FAIL("Maximum supported image dimension is %u pixels",
               static_cast<unsigned>(max_pixels));
    while (!(mode_ & detail::kModeIEND)) handle_next_chunk();
    if (info.header.color_type == kIndexed && info.palette.empty()) fail("missing PLTE");
    std::span<const std::uint8_t> idat =
        idat_.size() == 1 ? idat_[0] : std::span<const std::uint8_t>(join(idat_));
    decode_image(idat, header.width, header.height, pixels_, row_bytes_);
  }

  std::size_t row_bytes() const { return row_bytes_; }
  const std::uint8_t* row(std::uint32_t y) const { return pixels_.get() + y * row_bytes_; }
  std::span<std::uint8_t> pixels() { return {pixels_.get(), row_bytes_ * header.height}; }

  // APNG frames found by decode(), not counting a default image that is not
  // part of the animation.
  std::size_t frame_count() const { return info.frames.size(); }
  // Decodes frame i (frame 0 may be the default image, already decoded).
  Frame decode_frame(std::size_t i) {
    if (i >= info.frames.size()) fail("no such frame");
    Frame f;
    f.control = info.frames[i];
    std::unique_ptr<std::uint8_t[]> buf;
    if (i == 0 && info.default_image_is_frame) {
      f.row_bytes = row_bytes_;
      f.pixels.assign(pixels_.get(), pixels_.get() + row_bytes_ * header.height);
      return f;
    }
    const std::size_t k = i - (info.default_image_is_frame ? 1 : 0);
    if (k >= frame_data_.size() || frame_data_[k].empty()) fail("Not enough image data");
    const auto& parts = frame_data_[k];
    std::span<const std::uint8_t> data =
        parts.size() == 1 ? parts[0] : std::span<const std::uint8_t>(join(parts));
    decode_image(data, f.control.width, f.control.height, buf, f.row_bytes);
    f.pixels.assign(buf.get(), buf.get() + f.row_bytes * f.control.height);
    return f;
  }

  // The decoded image after `t` (decode() first): rows of `row_bytes`
  // bytes in the format of `format`; `palette` is the palette after
  // palette-level transformations (for palette output).
  struct Image {
    RowInfo format;
    std::size_t row_bytes = 0;
    std::vector<std::uint8_t> pixels;
    std::vector<PaletteEntry> palette;
    const std::uint8_t* row(std::uint32_t y) const { return pixels.data() + y * row_bytes; }
  };
  Image transform(const Transforms& t) {
    return transform_rows(t, header.width, header.height, pixels_.get(), row_bytes_);
  }
  Image transform(const Transforms& t, const Frame& f) {
    return transform_rows(t, f.control.width, f.control.height, f.pixels.data(), f.row_bytes);
  }

  // ICC profile, if any (kept for older callers).
  bool has_icc() const { return info.icc.has_value(); }

 private:
  [[noreturn]] void chunk_error(const char* msg) const {
    VIO_FAIL("%.4s: %s", reinterpret_cast<const char*>(chunk_type_), msg);
  }
  void warn(const std::string& msg) {
    warnings.push_back(msg);
    if (options.on_warning) options.on_warning(msg);
  }
  void chunk_warning(const char* msg) {
    warn(std::string(reinterpret_cast<const char*>(chunk_type_), 4) + ": " + msg);
  }
  // A recoverable error not tied to a chunk.
  void benign_error_text(const char* msg) {
    if (options.benign_errors_warn) warn(msg);
    else fail(msg);
  }
  void benign_error(const char* msg) {
    if (options.benign_errors_warn) chunk_warning(msg);
    else chunk_error(msg);
  }

  void reset(std::span<const std::uint8_t> file) {
    file_ = file;
    pos_ = 0;
    mode_ = 0;
    seen_ = 0;
    info = Info();
    warnings.clear();
    idat_.clear();
    frame_data_.clear();
    joined_.clear();
    cache_left_ = options.max_cached_chunks ? options.max_cached_chunks + 1 : 0;
    next_seq_ = 0;
  }

  // Concatenates data spans (kept alive in joined_).
  std::vector<std::uint8_t>& join(const std::vector<std::span<const std::uint8_t>>& parts) {
    joined_.emplace_back();
    auto& v = joined_.back();
    std::size_t n = 0;
    for (auto p : parts) n += p.size();
    v.reserve(n);
    for (auto p : parts) v.insert(v.end(), p.begin(), p.end());
    return v;
  }

  // Applies the CRC action to a chunk whose CRC is wrong; returns true if
  // the chunk is to be used anyway.
  bool crc_mismatch(bool critical) {
    CrcAction a = critical ? options.critical_crc : options.ancillary_crc;
    if (critical && (a == CrcAction::kWarnDiscard || a == CrcAction::kQuietDiscard))
      a = CrcAction::kError;
    switch (a) {
      case CrcAction::kError: chunk_error("CRC error");
      case CrcAction::kWarnDiscard: chunk_warning("CRC error"); return false;
      case CrcAction::kQuietDiscard: return false;
      case CrcAction::kWarnUse: chunk_warning("CRC error"); return true;
      case CrcAction::kQuietUse: return true;
    }
    return false;
  }

  ChunkKeep keep_rule(const std::uint8_t* type) const {
    for (const auto& k : options.keep)
      if (std::memcmp(k.first.data(), type, 4) == 0) return k.second;
    return ChunkKeep::kDefault;
  }

  // Reads and handles the chunk at pos_.
  void handle_next_chunk() {
    using namespace detail;
    if (file_.size() - pos_ < 12) {
      if (mode_ & kModeIDAT) fail("Not enough image data");
      fail("unexpected end of file");
    }
    const std::uint8_t* p = file_.data() + pos_;
    const std::uint32_t len = read_u32(p);
    check_chunk_header(p);
    if (file_.size() - pos_ - 12 < len) {
      if (mode_ & kModeIDAT) fail("Not enough image data");
      fail("unexpected end of file");
    }
    pos_ += 12 + static_cast<std::size_t>(len);
    process_chunk(p);
  }

  // Checks the length and type of the chunk whose header is at p.
  void check_chunk_header(const std::uint8_t* p) {
    chunk_type_ = p + 4;
    if (p[0] >= 0x80) chunk_error("bad header (invalid length)");
    if (!detail::valid_chunk_type(chunk_type_)) chunk_error("bad header (invalid type)");
  }

  // The index the chunk is handled as (kUnknown for chunks to treat as
  // unknown).
  int handling_index(const std::uint8_t* type) const {
    using namespace detail;
    int index = chunk_index(type);
    if (index != kUnknown && !is_critical(type) && keep_rule(type) != ChunkKeep::kDefault &&
        index != kACTL && index != kFCTL && index != kFDAT)
      index = kUnknown;
    return index;
  }

  // Checks a known chunk's placement and length; returns false if it is to
  // be skipped (after a warning).
  bool check_rules(int index, std::uint32_t len) {
    using namespace detail;
    if (index != kIHDR && !(mode_ & kModeIHDR)) chunk_error("missing IHDR");
    const ChunkRule& rule = chunk_rules[index];
    const char* err = nullptr;
    if ((mode_ & rule.not_after) || (mode_ & rule.needs) != rule.needs) err = "out of place";
    else if (!rule.multiple && (seen_ & (1u << index))) err = "duplicate";
    else if (len < rule.min_len) err = "too short";
    else if (rule.max_len == kLimit ? len > options.max_chunk_size
                                    : rule.max_len != kNoCheck && len > rule.max_len)
      err = rule.max_len == kLimit ? "length exceeds the limit" : "too long";
    if (index == kIDAT && (mode_ & kModeAfterIDAT)) {
      // IDAT after other chunks that followed the image data
      benign_error("Too many IDATs found");
      return false;
    }
    if (err) {
      if (is_critical(chunk_type_)) chunk_error(err);
      benign_error(err);
      return false;
    }
    return true;
  }

  // Handles the complete chunk at p (length, type, data and CRC).
  void process_chunk(const std::uint8_t* p) {
    using namespace detail;
    const std::uint32_t len = read_u32(p);
    chunk_type_ = p + 4;
    const std::uint8_t* data = p + 8;
    const bool critical = is_critical(chunk_type_);
    // Critical chunks whose CRC check is off, and ancillary ones set to be
    // used quietly, are not checked.
    const CrcAction action = critical ? options.critical_crc : options.ancillary_crc;
    if (action != CrcAction::kQuietUse &&
        zlib::crc32(0, chunk_type_, 4 + static_cast<std::size_t>(len)) != read_u32(data + len)) {
      if (!crc_mismatch(critical)) return;
    }
    const int index = handling_index(chunk_type_);
    // Anything but IDAT (or fdAT) ends the image data.
    if ((mode_ & kModeIDAT) && index != kIDAT && index != kFDAT && index != kFCTL)
      mode_ |= kModeAfterIDAT;
    if (index == kUnknown) {
      if (!(mode_ & kModeIHDR)) fail("missing IHDR");
      handle_unknown(data, len);
      return;
    }
    if (!check_rules(index, len)) return;
    if (handle_known(index, data, len)) seen_ |= 1u << index;
  }

  // Returns true if the chunk was accepted.
  bool handle_known(int index, const std::uint8_t* d, std::uint32_t len) {
    using namespace detail;
    Info& in = info;
    const Header& h = in.header;
    switch (index) {
      case kIHDR: {
        mode_ |= kModeIHDR;
        parse_ihdr(d);
        return true;
      }
      case kPLTE: {
        const char* err = nullptr;
        if (mode_ & kModePLTE) err = "duplicate";
        else if (mode_ & kModeIDAT) err = "out of place";
        else if (!(h.color_type & 2)) err = "ignored in grayscale PNG";
        else if (len > 3 * 256 || len % 3 != 0) err = "invalid";
        else if (h.color_type != kIndexed && ((seen_ & (1u << kTRNS)) || (seen_ & (1u << kBKGD))))
          err = "out of place";
        if (err) {
          if (h.color_type == kIndexed) chunk_error(err);
          benign_error(err);
          return false;
        }
        unsigned max = h.color_type == kIndexed ? 1u << h.bit_depth : 256u;
        unsigned num = len / 3 > max ? max : len / 3;
        if (h.color_type == kIndexed && num == 0) chunk_error("invalid");
        in.palette.resize(num);
        for (unsigned i = 0; i < num; i++) in.palette[i] = {d[3 * i], d[3 * i + 1], d[3 * i + 2]};
        mode_ |= kModePLTE;
        return true;
      }
      case kIDAT: {
        if (h.color_type == kIndexed && !(mode_ & kModePLTE)) chunk_error("missing PLTE");
        mode_ |= kModeIDAT;
        idat_.emplace_back(d, len);
        return true;
      }
      case kIEND:
        mode_ |= kModeAfterIDAT | kModeIEND;
        if (len != 0) benign_error("invalid");
        return true;
      case kTRNS: {
        if (h.color_type == kGray) {
          if (len != 2) return benign_error("invalid"), false;
          in.trans_color = Color16{0, 0, 0, 0, read_u16(d)};
        } else if (h.color_type == kTruecolor) {
          if (len != 6) return benign_error("invalid"), false;
          in.trans_color = Color16{0, read_u16(d), read_u16(d + 2), read_u16(d + 4), 0};
        } else if (h.color_type == kIndexed) {
          if (!(mode_ & kModePLTE)) return benign_error("out of place"), false;
          if (len > in.palette.size() || len == 0) return benign_error("invalid"), false;
          in.trans_alpha.assign(d, d + len);
        } else {
          return benign_error("invalid with alpha channel"), false;
        }
        return true;
      }
      case kGAMA: {
        std::uint32_t g = read_u32(d);
        if (g > 0x7fffffffu || g == 0) return benign_error("invalid"), false;
        in.gamma = g;
        return true;
      }
      case kCHRM: {
        Chromaticities c;
        std::int32_t* v[8] = {&c.white_x, &c.white_y, &c.red_x, &c.red_y,
                              &c.green_x, &c.green_y, &c.blue_x, &c.blue_y};
        for (int i = 0; i < 8; i++) {
          std::uint32_t u = read_u32(d + 4 * i);
          if (u > 0x7fffffffu) return benign_error("invalid"), false;
          *v[i] = static_cast<std::int32_t>(u);
        }
        in.chromaticities = c;
        return true;
      }
      case kSRGB:
        if (d[0] > 3) return benign_error("invalid"), false;
        in.srgb_intent = d[0];
        return true;
      case kICCP: return parse_iccp(d, len);
      case kSBIT: {
        unsigned n = h.color_type == kIndexed ? 3 : static_cast<unsigned>(num_channels(h.color_type));
        unsigned depth = h.color_type == kIndexed ? 8 : h.bit_depth;
        if (len != n) return benign_error("bad length"), false;
        for (unsigned i = 0; i < n; i++)
          if (d[i] == 0 || d[i] > depth) return benign_error("invalid"), false;
        SignificantBits s;
        if (h.color_type & 2) {
          s.red = d[0], s.green = d[1], s.blue = d[2];
          if (h.color_type & 4) s.alpha = d[3];
        } else {
          s.gray = d[0];
          if (h.color_type & 4) s.alpha = d[1];
        }
        in.sbit = s;
        return true;
      }
      case kCICP:
        in.cicp = Cicp{d[0], d[1], d[2], d[3]};
        return true;
      case kMDCV: {
        MasteringDisplay m;
        m.red_x = read_u16(d), m.red_y = read_u16(d + 2), m.green_x = read_u16(d + 4);
        m.green_y = read_u16(d + 6), m.blue_x = read_u16(d + 8), m.blue_y = read_u16(d + 10);
        m.white_x = read_u16(d + 12), m.white_y = read_u16(d + 14);
        m.max_luminance = read_u32(d + 16), m.min_luminance = read_u32(d + 20);
        in.mastering = m;
        return true;
      }
      case kCLLI:
        in.content_light = ContentLight{read_u32(d), read_u32(d + 4)};
        return true;
      case kEXIF: {
        std::uint32_t magic = read_u32(d);
        if (magic != 0x49492A00u && magic != 0x4D4D002Au) return benign_error("invalid"), false;
        in.exif.emplace(d, d + len);
        return true;
      }
      case kBKGD: {
        unsigned n = h.color_type == kIndexed ? 1 : (h.color_type & 2) ? 6 : 2;
        if (h.color_type == kIndexed && !(mode_ & kModePLTE)) return benign_error("out of place"), false;
        if (len != n) return benign_error("invalid"), false;
        Color16 c;
        if (h.color_type == kIndexed) {
          if (d[0] >= in.palette.size()) return benign_error("invalid index"), false;
          c.index = d[0];
          c.red = in.palette[d[0]].red, c.green = in.palette[d[0]].green, c.blue = in.palette[d[0]].blue;
        } else if (!(h.color_type & 2)) {
          if (h.bit_depth <= 8 && (d[0] != 0 || d[1] >= (1u << h.bit_depth)))
            return benign_error("invalid gray level"), false;
          c.red = c.green = c.blue = c.gray = read_u16(d);
        } else {
          if (h.bit_depth <= 8 && (d[0] != 0 || d[2] != 0 || d[4] != 0))
            return benign_error("invalid color"), false;
          c.red = read_u16(d), c.green = read_u16(d + 2), c.blue = read_u16(d + 4);
        }
        in.background = c;
        return true;
      }
      case kHIST: {
        if (len % 2 || len / 2 != in.palette.size()) return benign_error("invalid"), false;
        in.histogram.resize(len / 2);
        for (std::uint32_t i = 0; i < len / 2; i++) in.histogram[i] = read_u16(d + 2 * i);
        return true;
      }
      case kPHYS:
        in.phys = Physical{read_u32(d), read_u32(d + 4), d[8]};
        return true;
      case kOFFS:
        in.offs = Offset{read_i32(d), read_i32(d + 4), d[8]};
        return true;
      case kTIME:
        if (mode_ & kModeIDAT) mode_ |= kModeAfterIDAT;
        in.time = Time{read_u16(d), d[2], d[3], d[4], d[5], d[6]};
        return true;
      case kPCAL: return parse_pcal(d, len);
      case kSCAL: return parse_scal(d, len);
      case kSPLT: return parse_splt(d, len);
      case kTEXT:
      case kZTXT:
      case kITXT: return parse_text(index, d, len);
      case kACTL: return parse_actl(d, len);
      case kFCTL: return parse_fctl(d, len);
      case kFDAT: return parse_fdat(d, len);
      default: return false;
    }
  }

  // Counts a chunk against the cache limit; returns false if there is no
  // room left.
  bool take_cache_slot() {
    if (cache_left_ == 0) return true;
    if (cache_left_ == 1) return false;
    if (--cache_left_ == 1) {
      benign_error("no space in chunk cache");
      return false;
    }
    return true;
  }

  void handle_unknown(const std::uint8_t* d, std::uint32_t len) {
    const bool critical = detail::is_critical(chunk_type_);
    UnknownChunk c;
    std::memcpy(c.type.data(), chunk_type_, 4);
    c.location = (mode_ & detail::kModeIDAT)   ? kAfterIDAT
                 : (mode_ & detail::kModePLTE) ? kBeforeIDAT
                                               : kBeforePLTE;
    ChunkKeep keep = keep_rule(chunk_type_);
    bool handled = false;
    if (options.user_chunk) {
      if (len > options.max_chunk_size) {
        benign_error("unknown chunk exceeds memory limits");
        keep = ChunkKeep::kNever;
      } else {
        c.data.assign(d, d + len);
        int r = options.user_chunk(c);
        if (r < 0) chunk_error("error in user chunk");
        if (r > 0) {
          handled = true;
          keep = ChunkKeep::kNever;
        } else if (keep < ChunkKeep::kIfSafe && options.keep_unknown < ChunkKeep::kIfSafe) {
          keep = ChunkKeep::kIfSafe;
        }
      }
    }
    if (keep == ChunkKeep::kDefault) keep = options.keep_unknown;
    if (keep == ChunkKeep::kAlways || (keep == ChunkKeep::kIfSafe && !critical)) {
      if (len > options.max_chunk_size) {
        benign_error("unknown chunk exceeds memory limits");
      } else if (take_cache_slot()) {
        if (c.data.empty()) c.data.assign(d, d + len);
        info.unknown.push_back(std::move(c));
        handled = true;
      }
    }
    if (!handled && critical) chunk_error("unhandled critical chunk");
  }

  void parse_ihdr(const std::uint8_t* d) {
    using namespace detail;
    Header& h = info.header;
    std::uint32_t w = read_u32(d), ht = read_u32(d + 4);
    if (w > 0x7fffffffu || ht > 0x7fffffffu) fail("PNG unsigned integer out of range");
    h.width = w;
    h.height = ht;
    h.bit_depth = d[8];
    h.color_type = d[9];
    h.interlace = d[12];
    // All problems are reported, then the first is raised.
    const char* err = nullptr;
    auto check = [&](bool bad, const char* msg) {
      if (bad && !err) err = msg;
    };
    check(w == 0, "Image width is zero in IHDR");
    check(w > options.max_width, "Image width exceeds user limit in IHDR");
    check(ht == 0, "Image height is zero in IHDR");
    check(ht > options.max_height, "Image height exceeds user limit in IHDR");
    unsigned bd = h.bit_depth;
    check(bd != 1 && bd != 2 && bd != 4 && bd != 8 && bd != 16, "Invalid bit depth in IHDR");
    check(h.color_type == 1 || h.color_type == 5 || h.color_type > 6, "Invalid color type in IHDR");
    check((h.color_type == kIndexed && bd > 8) ||
              ((h.color_type == kTruecolor || h.color_type == kGrayAlpha ||
                h.color_type == kTruecolorAlpha) && bd < 8),
          "Invalid color type/bit depth combination in IHDR");
    check(d[12] > 1, "Unknown interlace method in IHDR");
    check(d[10] != 0, "Unknown compression method in IHDR");
    check(d[11] != 0, "Unknown filter method in IHDR");
    if (err) fail(err);
    row_bytes_ = (static_cast<std::size_t>(h.width) * num_channels(h.color_type) * h.bit_depth + 7) / 8;
  }

  // Inflates a compressed text or profile; nullopt (with a benign error) on
  // failure.
  std::optional<std::vector<std::uint8_t>> inflate_chunk_data(const std::uint8_t* d, std::size_t n) {
    try {
      return zlib::uncompress(std::span(d, n), options.max_chunk_size);
    } catch (const Error& e) {
      const char* m = std::strstr(e.what(), "zlib: ");
      benign_error(m ? m + 6 : e.what());
      return std::nullopt;
    }
  }

  static std::size_t keyword_length(const std::uint8_t* d, std::uint32_t len) {
    std::size_t n = 0;
    while (n < len && d[n] != 0) n++;
    return n;
  }

  bool parse_iccp(const std::uint8_t* d, std::uint32_t len) {
    std::size_t n = keyword_length(d, len < 81 ? len : 81);
    if (n < 1 || n > 79 || n >= len) return benign_error("bad keyword"), false;
    if (n + 2 > len || d[n + 1] != 0) return benign_error("bad compression method"), false;
    auto prof = inflate_chunk_data(d + n + 2, len - n - 2);
    if (!prof) return false;
    const auto& p = *prof;
    // Profile header checks.
    if (p.size() < 132) return benign_error("too short"), false;
    if (detail::read_u32(p.data()) != p.size()) return benign_error("length does not match profile"), false;
    if (std::memcmp(p.data() + 36, "acsp", 4) != 0) return benign_error("invalid signature"), false;
    const bool color = (info.header.color_type & 2) != 0;
    const char* space = reinterpret_cast<const char*>(p.data() + 16);
    if (color ? std::memcmp(space, "RGB ", 4) != 0 : std::memcmp(space, "GRAY", 4) != 0)
      return benign_error(color ? "RGB color space not permitted on grayscale PNG"
                                : "Gray color space not permitted on RGB PNG"),
             false;
    std::uint32_t tags = detail::read_u32(p.data() + 128);
    if (tags > (p.size() - 132) / 12) return benign_error("tag count too large"), false;
    info.icc = IccProfile{std::string(reinterpret_cast<const char*>(d), n), std::move(*prof)};
    return true;
  }

  bool parse_text(int index, const std::uint8_t* d, std::uint32_t len) {
    using namespace detail;
    if (!take_cache_slot()) return false;
    Text t;
    t.location = (mode_ & kModeIDAT) ? kAfterIDAT : kBeforeIDAT;
    std::size_t n = keyword_length(d, len);
    if (index == kTEXT) {
      t.type = Text::kText;
      t.keyword.assign(reinterpret_cast<const char*>(d), n);
      if (n < len) t.text.assign(reinterpret_cast<const char*>(d + n + 1), len - n - 1);
      // stops at an embedded NUL
      t.text.resize(std::strlen(t.text.c_str()));
    } else if (index == kZTXT) {
      if (n < 1 || n > 79) return benign_error("bad keyword"), false;
      if (n + 3 > len) return benign_error("truncated"), false;
      if (d[n + 1] != 0) return benign_error("unknown compression type"), false;
      auto txt = inflate_chunk_data(d + n + 2, len - n - 2);
      if (!txt) return false;
      t.type = Text::kZtxt;
      t.keyword.assign(reinterpret_cast<const char*>(d), n);
      t.text.assign(txt->begin(), txt->end());
    } else {
      if (n < 1 || n > 79) return benign_error("bad keyword"), false;
      if (n + 5 > len) return benign_error("truncated"), false;
      unsigned flag = d[n + 1], method = d[n + 2];
      if (!(flag == 0 || (flag == 1 && method == 0))) return benign_error("bad compression info"), false;
      std::size_t p = n + 3, lang = p;
      while (p < len && d[p]) p++;
      std::size_t tkey = ++p;
      while (p < len && d[p]) p++;
      ++p;
      t.keyword.assign(reinterpret_cast<const char*>(d), n);
      t.language.assign(reinterpret_cast<const char*>(d + lang), std::min<std::size_t>(tkey, len) - 1 - lang);
      if (tkey <= len)
        t.translated_keyword.assign(reinterpret_cast<const char*>(d + tkey),
                                    std::min<std::size_t>(p, len + 1) - 1 - tkey);
      if (flag == 0) {
        if (p > len) return benign_error("truncated"), false;
        t.type = Text::kItxt;
        t.text.assign(reinterpret_cast<const char*>(d + p), len - p);
      } else {
        if (p >= len) return benign_error("truncated"), false;
        auto txt = inflate_chunk_data(d + p, len - p);
        if (!txt) return false;
        t.type = Text::kItxtCompressed;
        t.text.assign(txt->begin(), txt->end());
      }
    }
    info.text.push_back(std::move(t));
    return true;
  }

  bool parse_splt(const std::uint8_t* d, std::uint32_t len) {
    if (!take_cache_slot()) return false;
    std::size_t n = keyword_length(d, len);
    if (len < 2 || n + 2 > len) {
      warn("malformed sPLT chunk");
      return false;
    }
    SuggestedPalette s;
    s.name.assign(reinterpret_cast<const char*>(d), n);
    s.depth = d[n + 1];
    std::size_t entry = s.depth == 8 ? 6 : 10;
    std::size_t data = len - n - 2;
    if (data % entry) {
      warn("sPLT chunk has bad length");
      return false;
    }
    const std::uint8_t* p = d + n + 2;
    for (std::size_t i = 0; i < data / entry; i++, p += entry) {
      SuggestedPalette::Entry e;
      if (s.depth == 8) {
        e.red = p[0], e.green = p[1], e.blue = p[2], e.alpha = p[3];
        e.frequency = detail::read_u16(p + 4);
      } else {
        e.red = detail::read_u16(p), e.green = detail::read_u16(p + 2);
        e.blue = detail::read_u16(p + 4), e.alpha = detail::read_u16(p + 6);
        e.frequency = detail::read_u16(p + 8);
      }
      s.entries.push_back(e);
    }
    info.splt.push_back(std::move(s));
    return true;
  }

  bool parse_pcal(const std::uint8_t* d, std::uint32_t len) {
    std::size_t n = keyword_length(d, len);
    if (len - n <= 12 || n >= len) return benign_error("invalid"), false;
    Calibration c;
    c.purpose.assign(reinterpret_cast<const char*>(d), n);
    const std::uint8_t* b = d + n;
    c.x0 = detail::read_i32(b + 1);
    c.x1 = detail::read_i32(b + 5);
    c.type = b[9];
    unsigned nparams = b[10];
    if ((c.type == 0 && nparams != 2) || (c.type == 1 && nparams != 3) ||
        (c.type == 2 && nparams != 3) || (c.type == 3 && nparams != 4))
      return benign_error("invalid parameter count"), false;
    if (c.type > 3) benign_error("unrecognized equation type");
    const std::uint8_t* end = d + len;
    const std::uint8_t* q = b + 11;
    const std::uint8_t* u = q;
    while (q < end && *q) q++;
    c.units.assign(reinterpret_cast<const char*>(u), static_cast<std::size_t>(q - u));
    for (unsigned i = 0; i < nparams; i++) {
      if (q >= end) return benign_error("invalid data"), false;
      const std::uint8_t* s = ++q;
      while (q < end && *q) q++;
      c.params.emplace_back(reinterpret_cast<const char*>(s), static_cast<std::size_t>(q - s));
    }
    info.pcal = std::move(c);
    return true;
  }

  bool parse_scal(const std::uint8_t* d, std::uint32_t len) {
    if (d[0] != 1 && d[0] != 2) return benign_error("invalid unit"), false;
    const char* s = reinterpret_cast<const char*>(d + 1);
    std::size_t n = len - 1, w = 0;
    while (w < n && s[w]) w++;
    bool pos = false;
    if (w == n || !detail::check_fp_number(s, w, &pos)) return benign_error("bad width format"), false;
    if (!pos) return benign_error("non-positive width"), false;
    const char* hs = s + w + 1;
    std::size_t hn = n - w - 1;
    if (!detail::check_fp_number(hs, hn, &pos)) return benign_error("bad height format"), false;
    if (!pos) return benign_error("non-positive height"), false;
    info.scal = Scale{d[0], std::string(s, w), std::string(hs, hn)};
    return true;
  }

  // APNG chunks.
  void check_sequence(const std::uint8_t* d) {
    std::uint32_t seq = detail::read_u32(d);
    if (seq > 0x7fffffffu) fail("PNG unsigned integer out of range");
    if (seq != next_seq_) fail("Out-of-order sequence number in fcTL or fdAT");
    next_seq_++;
  }

  bool parse_actl(const std::uint8_t* d, std::uint32_t len) {
    (void)len;
    std::uint32_t frames = detail::read_u32(d), plays = detail::read_u32(d + 4);
    if (frames == 0 || frames > 0x7fffffffu || plays > 0x7fffffffu) {
      warn("Ignoring invalid acTL");
      return false;
    }
    info.animation = Animation{frames, plays};
    mode_ |= detail::kModeACTL;
    return true;
  }

  bool parse_fctl(const std::uint8_t* d, std::uint32_t len) {
    (void)len;
    check_sequence(d);
    if (!info.animation) {
      warn("Ignoring fcTL without acTL");
      return false;
    }
    FrameControl f;
    f.width = detail::read_u32(d + 4), f.height = detail::read_u32(d + 8);
    f.x = detail::read_u32(d + 12), f.y = detail::read_u32(d + 16);
    f.delay_num = detail::read_u16(d + 20), f.delay_den = detail::read_u16(d + 22);
    f.dispose = d[24], f.blend = d[25];
    const Header& h = info.header;
    if (f.width == 0 || f.height == 0 || f.x > h.width || f.y > h.height ||
        f.width > h.width - f.x || f.height > h.height - f.y || f.dispose > 2 || f.blend > 1)
      fail("Invalid fcTL");
    const bool before_idat = !(mode_ & detail::kModeIDAT);
    if (before_idat) {
      if (!info.frames.empty()) fail("duplicate fcTL before IDAT");
      if (f.x != 0 || f.y != 0 || f.width != h.width || f.height != h.height)
        fail("fcTL of the default image does not cover the image");
      info.default_image_is_frame = true;
    } else {
      frame_data_.emplace_back();
    }
    info.frames.push_back(f);
    mode_ |= detail::kModeFCTL;
    return true;
  }

  bool parse_fdat(const std::uint8_t* d, std::uint32_t len) {
    check_sequence(d);
    if (frame_data_.empty()) fail("fdAT without fcTL");
    frame_data_.back().emplace_back(d + 4, len - 4);
    return true;
  }

  // Inflates exactly `size` bytes of filtered image data into `out`.
  void inflate(std::span<const std::uint8_t> idat, std::uint8_t* out, std::size_t size) {
    auto inf = std::make_unique<zlib::Inflater>(false, options.verify_adler);
    std::size_t n = 0;
    try {
      n = inf->decompress(idat, out, size);
    } catch (const Error& e) {
      const char* m = std::strstr(e.what(), "zlib: ");
      VIO_FAIL("IDAT: %s", m ? m + 6 : e.what());
    }
    if (n < size) fail("Not enough image data");
    if (!inf->stream_end()) {
      if (options.benign_errors_warn) warn("IDAT: Too much image data");
      else fail("IDAT: Too much image data");
    } else if (inf->consumed() < idat.size()) {
      if (options.benign_errors_warn) warn("IDAT: Extra compressed data");
      else fail("IDAT: Extra compressed data");
    }
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
      if (filter > 4) fail("bad adaptive filter value");
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

  // Decodes an image (or APNG frame) of w x h pixels from its zlib stream.
  void decode_image(std::span<const std::uint8_t> idat, std::uint32_t width, std::uint32_t height,
                    std::unique_ptr<std::uint8_t[]>& pixels, std::size_t& row_bytes) {
    using namespace detail;
    const Header& h = header;
    const int ch = num_channels(h.color_type);
    const unsigned bits_pp = static_cast<unsigned>(ch * h.bit_depth);
    const unsigned bpp = bits_pp < 8 ? 1u : bits_pp / 8;
    row_bytes = (static_cast<std::size_t>(width) * bits_pp + 7) / 8;
    const std::size_t rb = row_bytes;
    // Slack past the data: the reconstruction kernels read and write a few
    // bytes beyond a row's last pixel.
    constexpr std::size_t kSlack = 16;
    if (!h.interlace) {
      const std::size_t expected = static_cast<std::size_t>(height) * (rb + 1);
      pixels.reset(new std::uint8_t[expected + kSlack]);
      inflate(idat, pixels.get(), expected);
      unfilter_rows(pixels.get(), rb, height, bpp);
      if (h.bit_depth == 16) swap16(pixels.get(), rb * height);
      if (const unsigned used = (static_cast<std::size_t>(width) * bits_pp) % 8) {
        // Clear the padding bits after the last sample of each row (only
        // now: reconstructing the next row needs them as they were).
        const std::uint8_t mask = static_cast<std::uint8_t>(0xff00u >> used);
        for (std::uint32_t y = 0; y < height; y++) pixels[y * rb + rb - 1] &= mask;
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
      std::uint32_t pw = (width - adam7_x_start[p] + adam7_x_delta[p] - 1) / adam7_x_delta[p];
      std::uint32_t ph = (height - adam7_y_start[p] + adam7_y_delta[p] - 1) / adam7_y_delta[p];
      if (width <= static_cast<std::uint32_t>(adam7_x_start[p])) pw = 0;
      if (height <= static_cast<std::uint32_t>(adam7_y_start[p])) ph = 0;
      std::size_t prb = (static_cast<std::size_t>(pw) * bits_pp + 7) / 8;
      passes[p] = {pw, ph, p, expected, prb};
      if (pw && ph) expected += ph * (prb + 1);
    }
    std::unique_ptr<std::uint8_t[]> raw(new std::uint8_t[expected + kSlack]);
    inflate(idat, raw.get(), expected);
    pixels.reset(new std::uint8_t[rb * height + kSlack]());
    for (const Pass& ps : passes) {
      if (!ps.w || !ps.h) continue;
      std::uint8_t* base = raw.get() + ps.offset;
      unfilter_rows(base, ps.rb, ps.h, bpp);
      for (std::uint32_t y = 0; y < ps.h; y++) {
        const std::uint8_t* line = base + y * ps.rb;
        std::uint32_t oy = adam7_y_start[ps.p] + y * adam7_y_delta[ps.p];
        std::uint8_t* dst = pixels.get() + oy * rb;
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
    if (h.bit_depth == 16) swap16(pixels.get(), rb * height);
  }

  Image transform_rows(const Transforms& t, std::uint32_t width, std::uint32_t height,
                       const std::uint8_t* pixels, std::size_t rb) {
    detail::TransformPlan plan;
    plan.init(info, t, [this](const std::string& m) { warn(m); });
    Image img;
    img.format = plan.output();
    img.format.width = width;
    img.palette = plan.palette();
    img.row_bytes = (static_cast<std::size_t>(width) * img.format.channels * img.format.bit_depth + 7) / 8;
    img.pixels.resize(img.row_bytes * height + plan.max_row_bytes(width));
    transform_image(plan, width, height, pixels, rb, img.pixels.data(), img.row_bytes);
    img.pixels.resize(img.row_bytes * height);
    return img;
  }

  // Runs `plan` on a decoded image (native layout) into rows of `orb`
  // bytes.  Interlaced images are transformed pass row by pass row, as the
  // rows come in the file, then combined.
  void transform_image(detail::TransformPlan& plan, std::uint32_t width, std::uint32_t height,
                       const std::uint8_t* pixels, std::size_t rb, std::uint8_t* out, std::size_t orb) {
    using namespace detail;
    const unsigned bits = static_cast<unsigned>(num_channels(header.color_type) * header.bit_depth);
    std::vector<std::uint8_t> in(rb + 8), row(plan.max_row_bytes(width));
    // Copies `n` pixels of `bits` bits from src (pixel sx0, step sdx) to dst
    // (pixel dx0, step ddx).
    auto move_pixels = [](const std::uint8_t* src, std::size_t sx0, std::size_t sdx, std::uint8_t* dst,
                          std::size_t dx0, std::size_t ddx, std::size_t n, unsigned b, bool lsb_first) {
      for (std::size_t i = 0; i < n; i++) {
        const std::size_t sx = sx0 + i * sdx, dx = dx0 + i * ddx;
        if (b >= 8) {
          std::memcpy(dst + dx * (b / 8), src + sx * (b / 8), b / 8);
        } else {
          const unsigned mask = (1u << b) - 1;
          const unsigned spos = static_cast<unsigned>((sx * b) % 8), dpos = static_cast<unsigned>((dx * b) % 8);
          const unsigned v = (src[sx * b / 8] >> (lsb_first ? spos : 8 - b - spos)) & mask;
          const unsigned sh = lsb_first ? dpos : 8 - b - dpos;
          std::uint8_t& o = dst[dx * b / 8];
          o = static_cast<std::uint8_t>((o & ~(mask << sh)) | v << sh);
        }
      }
    };
    auto to_png_order = [&](const std::uint8_t* src, std::size_t n) {
      if (header.bit_depth != 16) return src;
      for (std::size_t i = 0; i + 1 < n; i += 2) {
        std::uint16_t v;
        std::memcpy(&v, src + i, 2);
        in[i] = static_cast<std::uint8_t>(v >> 8), in[i + 1] = static_cast<std::uint8_t>(v);
      }
      return static_cast<const std::uint8_t*>(in.data());
    };
    if (!header.interlace) {
      for (std::uint32_t y = 0; y < height; y++) {
        plan.run(to_png_order(pixels + y * rb, rb), width, row.data());
        std::memcpy(out + y * orb, row.data(), orb);
      }
      return;
    }
    const RowInfo& o = plan.output();
    const unsigned obits = static_cast<unsigned>(o.channels * o.bit_depth);
    std::vector<std::uint8_t> pass_row(rb + 8);
    std::memset(out, 0, orb * height);
    for (int p = 0; p < 7; p++) {
      if (width <= static_cast<std::uint32_t>(adam7_x_start[p]) ||
          height <= static_cast<std::uint32_t>(adam7_y_start[p]))
        continue;
      const std::uint32_t pw = (width - adam7_x_start[p] + adam7_x_delta[p] - 1) / adam7_x_delta[p];
      const std::size_t prb = (static_cast<std::size_t>(pw) * bits + 7) / 8;
      for (std::uint32_t y = adam7_y_start[p]; y < height; y += adam7_y_delta[p]) {
        std::memset(pass_row.data(), 0, prb);
        move_pixels(pixels + y * rb, adam7_x_start[p], adam7_x_delta[p], pass_row.data(), 0, 1, pw, bits, false);
        plan.run(to_png_order(pass_row.data(), prb), pw, row.data());
        move_pixels(row.data(), 0, 1, out + y * orb, adam7_x_start[p], adam7_x_delta[p], pw, obits,
                    plan.packswapped());
      }
    }
  }

  std::span<const std::uint8_t> file_;
  std::size_t pos_ = 0;
  std::uint32_t mode_ = 0, seen_ = 0;
  const std::uint8_t* chunk_type_ = reinterpret_cast<const std::uint8_t*>("    ");
  std::uint32_t cache_left_ = 0, next_seq_ = 0;
  std::vector<std::span<const std::uint8_t>> idat_;
  std::vector<std::vector<std::span<const std::uint8_t>>> frame_data_;
  std::vector<std::vector<std::uint8_t>> joined_;
  std::size_t row_bytes_ = 0;
  std::unique_ptr<std::uint8_t[]> pixels_;
};

}  // namespace vio::png
