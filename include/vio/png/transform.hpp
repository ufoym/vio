// vio::png - transformations applied to decoded rows.
//
// png::Transforms selects conversions of the decoded image: expansion of
// palettes, low bit depths and tRNS transparency, 16 <-> 8 bit conversion,
// gray <-> RGB, alpha and filler channels and their order, gamma
// correction, alpha premultiplication modes, compositing on a background,
// quantization to a palette, bit shifts, sample packing and byte order.
//
// The requests are applied as if made in the order of the fields below
// (later ones override earlier ones where they overlap: an alpha mode sets
// the screen gamma, a background replaces the alpha mode's compositing).
// Rows are then transformed in a fixed order, the one PNG libraries have
// traditionally used, so that combinations behave as applications expect:
//
//   expand, strip alpha (without compositing), RGB to gray, gray to RGB
//   (when the background is not gray), compose / alpha mode, gamma,
//   strip alpha (after compositing), encode alpha, 16 to 8 bits, quantize,
//   8 to 16 bits, gray to RGB (gray background), invert gray, invert alpha,
//   shift, unpack, BGR, packswap, filler, swap alpha, byte order, user.
//
// Gamma values are fixed-point numbers scaled by 100000 internally; tables
// are computed in double precision and a correction within 5% of 1 is not
// applied.  16-bit tables drop the low bits that sBIT marks insignificant
// (and keep only 11 bits when the result is reduced to 8 bits).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "detail/common.hpp"
#include "types.hpp"

namespace vio::png {

enum class ErrorAction : std::uint8_t { kNone, kWarn, kError };
enum class AlphaMode : std::uint8_t {
  kPng,         // alpha as stored, color not premultiplied
  kAssociated,  // premultiplied, linear output
  kOptimized,   // premultiplied; opaque pixels encoded, others linear
  kBroken,      // premultiplied and encoded, alpha encoded too
};
enum class BackgroundGamma : std::uint8_t { kScreen = 1, kFile = 2, kUnique = 3 };
enum class ByteOrder : std::uint8_t { kHost, kBig, kLittle };

// Gamma values are exponents as in gAMA (1/2.2 for a file in sRGB), or, for
// a screen, the display exponent (2.2); a value of 128 or more is taken as
// already scaled by 100000.  These special values select the sRGB
// approximation (2.2 / 1/2.2) and the old Mac one (1.8 = 2.2/1.45).
inline constexpr double kGammaSrgb = -1.0, kGammaMac = -2.0;

// The format of a row of samples.
struct RowInfo {
  std::uint32_t width = 0;
  std::uint8_t color_type = 0;  // ColorType bits: 1 palette, 2 color, 4 alpha
  std::uint8_t bit_depth = 8;
  std::uint8_t channels = 1;
};

struct Transforms {
  bool expand_palette = false;  // palette images to RGB (RGBA with tRNS)
  bool expand_gray = false;     // gray of 1, 2 or 4 bits to 8 bits
  bool trns_to_alpha = false;   // tRNS to an alpha channel (expands too)
  bool expand_16 = false;       // to 16 bits (expands, tRNS to alpha too)
  bool scale_16 = false;        // 16 to 8 bits, rounded
  bool strip_16 = false;        // 16 to 8 bits, the high byte
  bool strip_alpha = false;
  bool gray_to_rgb = false;     // (expands low bit depths too)
  struct RgbToGray {
    ErrorAction action = ErrorAction::kNone;
    // Weights scaled by 100000; negative: from cHRM/mDCV, else Rec. 709.
    std::int32_t red = -1, green = -1;
  };
  std::optional<RgbToGray> rgb_to_gray;
  // Gamma correction: the display's exponent and the file's encoding
  // exponent (overriding gAMA/sRGB).  0 = not set.
  double screen_gamma = 0, file_gamma = 0;
  // The file's gamma when it has no gAMA or sRGB chunk (0: from the alpha
  // mode's output gamma, else none).
  double default_file_gamma = 0;
  struct Alpha {
    AlphaMode mode = AlphaMode::kPng;
    double output_gamma = kGammaSrgb;
  };
  std::optional<Alpha> alpha_mode;
  struct Background {
    Color16 color;
    BackgroundGamma gamma_type = BackgroundGamma::kScreen;
    // The color is in the file's terms (a palette index, a gray level of
    // the file's bit depth) and is expanded with the image; else it is in
    // the output's terms.
    bool need_expand = false;
    double gamma = 1.0;  // for kUnique
  };
  std::optional<Background> background;
  struct Quantize {
    std::vector<PaletteEntry> palette;  // the colors to use
    int max_colors = 256;
    std::vector<std::uint16_t> histogram;  // optional, per palette entry
    bool full = false;  // build a lookup for RGB images
  };
  std::optional<Quantize> quantize;
  std::optional<SignificantBits> shift;  // shift samples down to these depths
  bool packing = false;   // samples of 1, 2 or 4 bits one per byte
  bool packswap = false;  // packed samples least significant first
  bool bgr = false;
  bool swap_alpha = false;    // alpha first
  bool invert_alpha = false;  // transparency instead of opacity
  bool invert_mono = false;   // invert gray samples
  struct Filler {
    std::uint16_t value = 0xffff;
    bool before = false;  // first in the pixel, else last
    bool alpha = false;   // it is an alpha channel (else just filler)
  };
  std::optional<Filler> filler;
  ByteOrder byte_order = ByteOrder::kHost;  // of 16-bit samples
  // Called last for each row: may change the row in place; its format is
  // given in `info` (with the depth and channels declared below, if
  // nonzero, for the image's output format).
  std::function<void(RowInfo& info, std::uint8_t* row)> user;
  std::uint8_t user_depth = 0, user_channels = 0;
};

// Conversions of the rows given to the encoder into the image's format,
// applied in this order: user callback, filler removal, unpacking (packed
// least significant first, or one sample per byte), byte order, scaling up
// from significant bits, alpha order, alpha inversion, BGR, gray inversion.
struct WriteTransforms {
  std::function<void(RowInfo& info, std::uint8_t* row)> user;  // first
  std::optional<Transforms::Filler> strip_filler;  // drop a filler channel
  bool packing = false;   // samples of 1, 2 or 4 bits one per byte
  bool packswap = false;  // packed samples least significant first
  ByteOrder byte_order = ByteOrder::kHost;  // of 16-bit samples
  // Samples hold only this many bits; they are scaled to the bit depth
  // (the high bits repeated in the low ones).
  std::optional<SignificantBits> shift;
  bool swap_alpha = false;    // alpha first
  bool invert_alpha = false;  // transparency instead of opacity
  bool bgr = false;
  bool invert_mono = false;

  bool any() const {
    return user || strip_filler || packing || packswap || byte_order != ByteOrder::kHost || shift ||
           swap_alpha || invert_alpha || bgr || invert_mono;
  }
};

namespace detail {

// Fixed-point (x 100000) arithmetic, rounded as the gamma code expects.
inline std::int32_t fp_round(double r) {
  r = std::floor(r + .5);
  if (r > 2147483647. || r < -2147483648.) return 0;
  return static_cast<std::int32_t>(r);
}
inline std::int32_t fp_from_double(double v) { return fp_round(v * 100000); }
inline bool muldiv(std::int32_t* res, std::int32_t a, std::int32_t times, std::int32_t divisor) {
  if (divisor == 0) return false;
  if (a == 0 || times == 0) {
    *res = 0;
    return true;
  }
  double r = std::floor(static_cast<double>(a) * times / divisor + .5);
  if (r > 2147483647. || r < -2147483648.) return false;
  *res = static_cast<std::int32_t>(r);
  return true;
}
inline std::int32_t reciprocal(std::int32_t a) { return a ? fp_round(1e10 / a) : 0; }
inline std::int32_t reciprocal2(std::int32_t a, std::int32_t b) {
  return a && b ? fp_round(1e15 / a / b) : 0;
}
inline bool gamma_significant(std::int32_t g) { return g < 95000 || g > 105000; }
inline std::int32_t fp_sub(std::int32_t a, std::int32_t b, bool* error) {
  std::int64_t r = static_cast<std::int64_t>(a) - b;
  if (r > 0x7fffffff || r < -0x7fffffff) {
    *error = true;
    return 50000;
  }
  return static_cast<std::int32_t>(r);
}
inline std::uint8_t gamma_8bit_correct(unsigned v, std::int32_t g) {
  if (v > 0 && v < 255) return static_cast<std::uint8_t>(std::floor(255 * std::pow(v / 255., g * .00001) + .5));
  return static_cast<std::uint8_t>(v);
}
inline std::uint16_t gamma_16bit_correct(unsigned v, std::int32_t g) {
  if (v > 0 && v < 65535)
    return static_cast<std::uint16_t>(std::floor(65535 * std::pow(v / 65535., g * .00001) + .5));
  return static_cast<std::uint16_t>(v);
}

// CIE Y of the primaries for xy chromaticities (x 100000); false if the
// values make no sense.
inline bool primaries_y(const Chromaticities& xy, std::int32_t* ry, std::int32_t* gy, std::int32_t* by) {
  const std::int32_t lim = 110000;
  if (xy.red_x < 0 || xy.red_x > lim || xy.red_y < 0 || xy.red_y > lim - xy.red_x) return false;
  if (xy.green_x < 0 || xy.green_x > lim || xy.green_y < 0 || xy.green_y > lim - xy.green_x) return false;
  if (xy.blue_x < 0 || xy.blue_x > lim || xy.blue_y < 0 || xy.blue_y > lim - xy.blue_x) return false;
  if (xy.white_x < 0 || xy.white_x > lim || xy.white_y < 5 || xy.white_y > lim - xy.white_x) return false;
  std::int32_t left, right, denominator, red_inverse, green_inverse, blue_scale;
  bool error = false;
  if (!muldiv(&left, xy.green_x - xy.blue_x, xy.red_y - xy.blue_y, 8)) return false;
  if (!muldiv(&right, xy.green_y - xy.blue_y, xy.red_x - xy.blue_x, 8)) return false;
  denominator = fp_sub(left, right, &error);
  if (error) return false;
  if (!muldiv(&left, xy.green_x - xy.blue_x, xy.white_y - xy.blue_y, 8)) return false;
  if (!muldiv(&right, xy.green_y - xy.blue_y, xy.white_x - xy.blue_x, 8)) return false;
  if (!muldiv(&red_inverse, xy.white_y, denominator, fp_sub(left, right, &error)) || error ||
      red_inverse <= xy.white_y)
    return false;
  if (!muldiv(&left, xy.red_y - xy.blue_y, xy.white_x - xy.blue_x, 8)) return false;
  if (!muldiv(&right, xy.red_x - xy.blue_x, xy.white_y - xy.blue_y, 8)) return false;
  if (!muldiv(&green_inverse, xy.white_y, denominator, fp_sub(left, right, &error)) || error ||
      green_inverse <= xy.white_y)
    return false;
  blue_scale = fp_sub(fp_sub(reciprocal(xy.white_y), reciprocal(red_inverse), &error), reciprocal(green_inverse),
                      &error);
  if (error || blue_scale <= 0) return false;
  if (!muldiv(ry, xy.red_y, 100000, red_inverse)) return false;
  if (!muldiv(gy, xy.green_y, 100000, green_inverse)) return false;
  if (!muldiv(by, xy.blue_y, blue_scale, 100000)) return false;
  return true;
}

// Transformation plan for one image: what to do, decided once.
class TransformPlan {
 public:
  void init(const Info& info, const Transforms& t, std::function<void(const std::string&)> warn) {
    t_ = t;
    warn_ = std::move(warn);
    const Header& h = info.header;
    color_type_ = h.color_type;
    bit_depth_ = h.bit_depth;
    palette_.assign(256, PaletteEntry{0, 0, 0});
    std::copy(info.palette.begin(), info.palette.end(), palette_.begin());
    num_palette_ = file_palette_size_ = static_cast<int>(info.palette.size());
    trans_alpha_ = info.trans_alpha;
    if (color_type_ == kIndexed) {
      num_trans_ = static_cast<int>(trans_alpha_.size());
    } else {
      num_trans_ = info.trans_color ? 1 : 0;
      if (info.trans_color) trans_color_ = *info.trans_color;
    }
    if (info.sbit) sig_bit_ = *info.sbit;
    chunk_gamma_ = info.srgb_intent ? 45455 : info.gamma ? static_cast<std::int32_t>(*info.gamma) : 0;
    set_requests(t);
    init_transformations(info);
    compute_output(h.width);
  }

  // The format after all transformations; the palette after palette-level
  // transformations (gamma, compositing, shift, quantization).
  const RowInfo& output() const { return out_; }
  std::vector<PaletteEntry> palette() const {
    const int n = color_type_ == kIndexed ? file_palette_size_ : num_palette_;
    return std::vector<PaletteEntry>(palette_.begin(), palette_.begin() + n);
  }
  // Packed output samples are least significant first.
  bool packswapped() const { return has(kPackswap) && out_.bit_depth < 8; }
  // Rows are composited (alpha mode or background) row by row.
  bool composing() const { return has(kCompose); }
  // Partially transparent pixels are left linear (optimized alpha mode).
  bool optimize_alpha() const { return optimize_alpha_; }
  // The gamma values in use (x 100000) once set up.
  std::int32_t file_gamma() const { return file_gamma_; }
  std::int32_t screen_gamma() const { return screen_gamma_; }
  // An RGB-to-gray conversion met a pixel that was not gray.
  bool rgb_to_gray_found_color() const { return rgb_error_seen_; }
  // Room needed for a transformed row (it may grow before it shrinks).
  std::size_t max_row_bytes(std::uint32_t width) const { return static_cast<std::size_t>(width) * 8 + 16; }

  // Transforms one row: `in` holds the native row (PNG byte order, packed);
  // the result goes to `out`, in the output format.
  void run(const std::uint8_t* in, std::uint32_t width, std::uint8_t* out) {
    RowInfo r{width, color_type_, bit_depth_, static_cast<std::uint8_t>(num_channels(color_type_))};
    s_.resize(static_cast<std::size_t>(width) * 4 + 4);
    unpack(in, r);
    if (has(kExpand)) {
      if (r.color_type == kIndexed) expand_palette(r);
      else expand(r, num_trans_ != 0 && has(kExpandTrns));
    }
    if (has(kStripAlpha) && !has(kCompose) && (r.color_type == kGrayAlpha || r.color_type == kTruecolorAlpha))
      strip_alpha(r);
    if (has(kRgbToGray) && rgb_to_gray(r)) {
      rgb_error_seen_ = true;
      if (t_.rgb_to_gray->action == ErrorAction::kWarn && warn_) warn_("RGB to gray found a nongray pixel");
      if (t_.rgb_to_gray->action == ErrorAction::kError) fail("RGB to gray found a nongray pixel");
    }
    if (has(kGrayToRgb) && !background_is_gray_) gray_to_rgb(r);
    if (has(kCompose)) compose(r);
    if (has(kGamma) && !has(kRgbToGray) && !(has(kCompose) && (num_trans_ != 0 || (color_type_ & 4))) &&
        color_type_ != kIndexed)
      gamma(r);
    if (has(kStripAlpha) && has(kCompose) && (r.color_type == kGrayAlpha || r.color_type == kTruecolorAlpha))
      strip_alpha(r);
    if (has(kEncodeAlpha) && (r.color_type & 4)) encode_alpha(r);
    if (has(kScale16) && r.bit_depth == 16) scale_16(r);
    if (has(kStrip16) && r.bit_depth == 16) strip_16(r);
    if (has(kQuantize)) quantize(r);
    if (has(kExpand16) && r.bit_depth == 8 && r.color_type != kIndexed) {
      for (std::size_t i = 0, n = samples(r); i < n; i++) s_[i] = static_cast<std::uint16_t>(s_[i] * 257);
      r.bit_depth = 16;
    }
    if (has(kGrayToRgb) && background_is_gray_) gray_to_rgb(r);
    if (has(kInvertMono)) invert_mono(r);
    if (has(kInvertAlpha) && (r.color_type == kGrayAlpha || r.color_type == kTruecolorAlpha)) {
      const unsigned max = (1u << r.bit_depth) - 1, ch = r.channels;
      for (std::size_t x = 0; x < r.width; x++)
        s_[x * ch + ch - 1] = static_cast<std::uint16_t>(max - s_[x * ch + ch - 1]);
    }
    if (has(kShift)) unshift(r);
    if (has(kPack) && r.bit_depth < 8) r.bit_depth = 8;  // values unchanged
    if (has(kBgr) && (r.color_type == kTruecolor || r.color_type == kTruecolorAlpha) &&
        (r.bit_depth == 8 || r.bit_depth == 16)) {
      const unsigned ch = r.channels;
      for (std::size_t x = 0; x < r.width; x++) std::swap(s_[x * ch], s_[x * ch + 2]);
    }
    if (has(kFiller)) filler(r);
    if (has(kSwapAlpha) && (r.color_type == kGrayAlpha || r.color_type == kTruecolorAlpha)) {
      const unsigned ch = r.channels;
      for (std::size_t x = 0; x < r.width; x++) {
        std::uint16_t* p = &s_[x * ch];
        const std::uint16_t a = p[ch - 1];
        for (unsigned c = ch - 1; c > 0; c--) p[c] = p[c - 1];
        p[0] = a;
      }
    }
    pack(out, r, has(kPackswap));
    if (has(kUser)) {
      RowInfo u = r;
      t_.user(u, out);
    }
  }

 private:
  enum : std::uint32_t {
    kExpand = 1u << 0, kExpandTrns = 1u << 1, kExpand16 = 1u << 2, kScale16 = 1u << 3,
    kStrip16 = 1u << 4, kGrayToRgb = 1u << 5, kRgbToGray = 1u << 6, kStripAlpha = 1u << 7,
    kCompose = 1u << 8, kBackgroundExpand = 1u << 9, kEncodeAlpha = 1u << 10, kGamma = 1u << 11,
    kShift = 1u << 12, kPack = 1u << 13, kPackswap = 1u << 14, kBgr = 1u << 15, kFiller = 1u << 16,
    kAddAlpha = 1u << 17, kSwapAlpha = 1u << 18, kInvertAlpha = 1u << 19, kInvertMono = 1u << 20,
    kQuantize = 1u << 21, kSwapBytes = 1u << 22, kUser = 1u << 23,
  };
  bool has(std::uint32_t f) const { return (tf_ & f) != 0; }
  static std::size_t samples(const RowInfo& r) { return static_cast<std::size_t>(r.width) * r.channels; }

  // A gamma value as given (exponent, or already scaled), then the special
  // values resolved for a screen or a file.
  static std::int32_t convert_gamma(double g) {
    if (g > 0 && g < 128) g *= 100000;
    return fp_round(g);
  }
  static std::int32_t translate_gamma(std::int32_t g, bool screen) {
    if (g == -1 || g == -100000) return screen ? 220000 : 45455;
    if (g == -2 || g == -50000) return screen ? 151724 : 65909;
    return g;
  }
  bool unsupported_gamma(std::int32_t g) {
    if (g < 1000 || g > 10000000) {
      if (warn_) warn_("gamma out of supported range");
      return true;
    }
    return false;
  }

  // ---- requests ----------------------------------------------------------------

  void set_requests(const Transforms& t) {
    tf_ = 0;
    if (t.expand_palette || t.trns_to_alpha) tf_ |= kExpand | kExpandTrns;
    if (t.expand_gray) tf_ |= kExpand;
    if (t.expand_16) tf_ |= kExpand16 | kExpand | kExpandTrns;
    if (t.scale_16) tf_ |= kScale16;
    if (t.strip_16) tf_ |= kStrip16;
    if (t.strip_alpha) tf_ |= kStripAlpha;
    if (t.gray_to_rgb) tf_ |= kExpand | kGrayToRgb;
    if (t.rgb_to_gray) {
      tf_ |= kRgbToGray;
      if (color_type_ == kIndexed) tf_ |= kExpand;
      const auto& q = *t.rgb_to_gray;
      if (q.red >= 0 && q.green >= 0 && q.red + q.green <= 100000) {
        rc_ = static_cast<std::uint16_t>(static_cast<std::uint32_t>(q.red) * 32768 / 100000);
        gc_ = static_cast<std::uint16_t>(static_cast<std::uint32_t>(q.green) * 32768 / 100000);
        coefficients_set_ = true;
      } else if (q.red >= 0 && q.green >= 0 && warn_) {
        warn_("ignoring out of range rgb_to_gray coefficients");
      }
    }
    if (t.screen_gamma != 0 || t.file_gamma != 0) {
      const std::int32_t sg = t.screen_gamma != 0 ? translate_gamma(convert_gamma(t.screen_gamma), true) : 0;
      const std::int32_t fg = t.file_gamma != 0 ? translate_gamma(convert_gamma(t.file_gamma), false) : 0;
      if ((sg == 0 || !unsupported_gamma(sg)) && (fg == 0 || !unsupported_gamma(fg))) {
        if (fg > 0) file_gamma_ = fg;
        if (sg > 0) screen_gamma_ = sg;
      }
    }
    if (t.default_file_gamma != 0) default_gamma_ = translate_gamma(convert_gamma(t.default_file_gamma), false);
    if (t.alpha_mode) {
      std::int32_t og = translate_gamma(convert_gamma(t.alpha_mode->output_gamma), true);
      if (og < 1000 || og > 10000000) fail("gamma out of supported range");
      std::int32_t fg = default_gamma_;
      if (fg == 0) default_gamma_ = fg = reciprocal(og);
      bool compose = false;
      switch (t.alpha_mode->mode) {
        case AlphaMode::kPng:
          tf_ &= ~kEncodeAlpha;
          optimize_alpha_ = false;
          break;
        case AlphaMode::kAssociated:
          compose = true;
          tf_ &= ~kEncodeAlpha;
          optimize_alpha_ = false;
          og = 100000;
          break;
        case AlphaMode::kOptimized:
          compose = true;
          tf_ &= ~kEncodeAlpha;
          optimize_alpha_ = true;
          break;
        case AlphaMode::kBroken:
          compose = true;
          tf_ |= kEncodeAlpha;
          optimize_alpha_ = false;
          break;
      }
      screen_gamma_ = og;
      if (compose) {
        background_ = Color16{};
        background_gamma_ = fg;
        background_gamma_type_ = BackgroundGamma::kFile;
        tf_ &= ~kBackgroundExpand;
        tf_ |= kCompose;
      }
    }
    if (t.background) {
      tf_ |= kCompose | kStripAlpha;
      tf_ &= ~kEncodeAlpha;
      optimize_alpha_ = false;
      background_ = t.background->color;
      background_gamma_ = fp_from_double(t.background->gamma);
      background_gamma_type_ = t.background->gamma_type;
      if (t.background->need_expand) tf_ |= kBackgroundExpand;
      else tf_ &= ~kBackgroundExpand;
    }
    if (t.quantize) set_quantize(*t.quantize);
    if (t.shift) {
      // Bits not given: a gray value's for the colors (an image may be
      // converted to color), the depth's for alpha.
      SignificantBits b = *t.shift;
      const std::uint8_t depth = color_type_ == kIndexed ? 8 : bit_depth_;
      if (!(color_type_ & 2)) {
        if (!b.red) b.red = b.gray;
        if (!b.green) b.green = b.gray;
        if (!b.blue) b.blue = b.gray;
      }
      if (!b.alpha) b.alpha = depth;
      bool invalid;
      if (color_type_ & 2) {
        invalid = b.red == 0 || b.red > bit_depth_ || b.green == 0 || b.green > bit_depth_ || b.blue == 0 ||
                  b.blue > bit_depth_;
      } else {
        invalid = b.gray == 0 || b.gray > bit_depth_;
      }
      if ((color_type_ & 4) && (b.alpha == 0 || b.alpha > bit_depth_)) invalid = true;
      if (invalid) {
        if (warn_) warn_("invalid shift values");
      } else {
        tf_ |= kShift;
        shift_ = b;
      }
    }
    if (t.packing && bit_depth_ < 8) tf_ |= kPack;
    if (t.packswap && bit_depth_ < 8) tf_ |= kPackswap;
    if (t.bgr) tf_ |= kBgr;
    if (t.swap_alpha) tf_ |= kSwapAlpha;
    if (t.invert_alpha) tf_ |= kInvertAlpha;
    if (t.invert_mono) tf_ |= kInvertMono;
    if (t.filler) {
      tf_ |= kFiller;
      if (t.filler->alpha) tf_ |= kAddAlpha;
    }
    bool little = t.byte_order == ByteOrder::kLittle;
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
    if (t.byte_order == ByteOrder::kHost) little = true;
#endif
    if (little) tf_ |= kSwapBytes;
    if (t.user) tf_ |= kUser;
  }

  void set_quantize(const Transforms::Quantize& q) {
    std::vector<PaletteEntry> pal = q.palette;
    const int num_palette = static_cast<int>(pal.size());
    const int maxc = q.max_colors;
    const bool full = q.full;
    if (pal.empty()) return;
    tf_ |= kQuantize;
    auto dist = [](const PaletteEntry& a, const PaletteEntry& b) {
      return std::abs(int(a.red) - b.red) + std::abs(int(a.green) - b.green) + std::abs(int(a.blue) - b.blue);
    };
    if (!full) {
      quantize_index_.resize(256);
      for (int i = 0; i < 256; i++) quantize_index_[i] = static_cast<std::uint8_t>(i);
    }
    int n = num_palette;
    if (num_palette > maxc) {
      const auto& hist = q.histogram;
      if (static_cast<int>(hist.size()) >= num_palette) {
        std::vector<std::uint8_t> sort(num_palette);
        for (int i = 0; i < num_palette; i++) sort[i] = static_cast<std::uint8_t>(i);
        for (int i = num_palette - 1; i >= maxc; i--) {
          bool done = true;
          for (int j = 0; j < i; j++)
            if (hist[sort[j]] < hist[sort[j + 1]]) {
              std::swap(sort[j], sort[j + 1]);
              done = false;
            }
          if (done) break;
        }
        int j = num_palette;
        for (int i = 0; i < maxc; i++) {
          if (sort[i] < maxc) continue;
          do j--;
          while (sort[j] >= maxc);
          if (full) {
            pal[i] = pal[j];
          } else {
            std::swap(pal[i], pal[j]);
            quantize_index_[j] = static_cast<std::uint8_t>(i);
            quantize_index_[i] = static_cast<std::uint8_t>(j);
          }
        }
        if (!full)
          for (int i = 0; i < num_palette; i++) {
            if (quantize_index_[i] < maxc) continue;
            const int d_index = quantize_index_[i];
            int min_d = dist(pal[d_index], pal[0]), min_k = 0;
            for (int k = 1; k < maxc; k++) {
              const int d = dist(pal[d_index], pal[k]);
              if (d < min_d) min_d = d, min_k = k;
            }
            quantize_index_[i] = static_cast<std::uint8_t>(min_k);
          }
      } else {
        // Merge the closest pairs of colors until few enough remain.
        std::vector<std::uint8_t> i2p(num_palette), p2i(num_palette);
        for (int i = 0; i < num_palette; i++) i2p[i] = p2i[i] = static_cast<std::uint8_t>(i);
        int max_d = 96;
        while (n > maxc) {
          // (pairs are collected only until a color with no close pair
          // before it has been found, as the traditional algorithm does)
          std::vector<std::vector<std::pair<int, int>>> hash(769);
          bool found = false;
          for (int i = 0; i < n - 1; i++) {
            for (int j = i + 1; j < n; j++) {
              const int d = dist(pal[i], pal[j]);
              if (d <= max_d) hash[d].push_back({p2i[i], p2i[j]}), found = true;
            }
            if (!found) break;
          }
          for (int d = 0; d <= max_d && n > maxc; d++) {
            for (auto it = hash[d].rbegin(); it != hash[d].rend(); ++it) {
              const int left = it->first, right = it->second;
              if (i2p[left] < n && i2p[right] < n) {
                int j, next_j;
                if (n & 1) j = left, next_j = right;
                else j = right, next_j = left;
                n--;
                pal[i2p[j]] = pal[n];
                if (!full)
                  for (int k = 0; k < num_palette; k++) {
                    if (quantize_index_[k] == i2p[j]) quantize_index_[k] = i2p[next_j];
                    if (quantize_index_[k] == n) quantize_index_[k] = i2p[j];
                  }
                i2p[p2i[n]] = i2p[j];
                p2i[i2p[j]] = p2i[n];
                i2p[j] = static_cast<std::uint8_t>(n);
                p2i[n] = static_cast<std::uint8_t>(j);
              }
              if (n <= maxc) break;
            }
          }
          max_d += 96;
        }
      }
      n = maxc;
    }
    // A palette image keeps its own palette (only the first n entries go
    // through palette-level transformations); others take the new one.
    if (color_type_ != kIndexed) std::copy(pal.begin(), pal.begin() + n, palette_.begin());
    num_palette_ = n;
    if (full) {
      palette_lookup_.assign(1u << 15, 0);
      std::vector<std::uint8_t> distance(1u << 15, 0xff);
      for (int i = 0; i < n; i++) {
        const int r = pal[i].red >> 3, g = pal[i].green >> 3, b = pal[i].blue >> 3;
        for (int ir = 0; ir < 32; ir++) {
          const int dr = std::abs(ir - r);
          for (int ig = 0; ig < 32; ig++) {
            const int dg = std::abs(ig - g), dt = dr + dg, dm = std::max(dr, dg);
            for (int ib = 0; ib < 32; ib++) {
              const int db = std::abs(ib - b), d = std::max(dm, db) + dt + db;
              const unsigned idx = static_cast<unsigned>(ir << 10 | ig << 5 | ib);
              if (d < distance[idx]) {
                distance[idx] = static_cast<std::uint8_t>(d);
                palette_lookup_[idx] = static_cast<std::uint8_t>(i);
              }
            }
          }
        }
      }
    }
  }

  // ---- set-up --------------------------------------------------------------------

  void init_transformations(const Info& info) {
    // Gamma values: explicit file gamma, else the file's chunks, else the
    // alpha mode's default, else the inverse of the screen's.
    std::int32_t fg = file_gamma_ ? file_gamma_ : chunk_gamma_ ? chunk_gamma_ : default_gamma_;
    if (fg == 0 && screen_gamma_ != 0) fg = reciprocal(screen_gamma_);
    std::int32_t sg = screen_gamma_;
    bool correction = false;
    if (fg > 0) {
      if (sg > 0) {
        std::int32_t g;
        correction = !muldiv(&g, sg, fg, 100000) || gamma_significant(g);
      } else {
        sg = reciprocal(fg);
      }
    } else {
      fg = sg = 100000;
    }
    file_gamma_ = fg;
    screen_gamma_ = sg;
    if (correction) tf_ |= kGamma;
    else tf_ &= ~kGamma;

    if (has(kStripAlpha) && !has(kCompose)) {
      tf_ &= ~(kBackgroundExpand | kEncodeAlpha | kExpandTrns);
      optimize_alpha_ = false;
      num_trans_ = 0;
    }
    if (!gamma_significant(screen_gamma_)) {
      tf_ &= ~kEncodeAlpha;
      optimize_alpha_ = false;
    }
    if (has(kRgbToGray)) set_rgb_coefficients(info);
    if (has(kBackgroundExpand)) {
      if (!(color_type_ & 2)) background_is_gray_ = true;
    } else if (has(kCompose) && has(kGrayToRgb)) {
      if (background_.red == background_.green && background_.red == background_.blue) {
        background_is_gray_ = true;
        background_.gray = background_.red;
      }
    }
    if (color_type_ == kIndexed) init_palette_transformations();
    else init_rgb_transformations();
    if (has(kExpand16) && has(kCompose) && !has(kBackgroundExpand) && bit_depth_ != 16) {
      auto chop = [](std::uint16_t& v) { v = static_cast<std::uint16_t>((v * 255u + 32895u) >> 16); };
      chop(background_.red), chop(background_.green), chop(background_.blue), chop(background_.gray);
    }
    if ((has(kStrip16) || has(kScale16)) && has(kCompose) && !has(kBackgroundExpand) && bit_depth_ == 16) {
      background_.red = static_cast<std::uint16_t>(background_.red * 257);
      background_.green = static_cast<std::uint16_t>(background_.green * 257);
      background_.blue = static_cast<std::uint16_t>(background_.blue * 257);
      background_.gray = static_cast<std::uint16_t>(background_.gray * 257);
    }
    background_1_ = background_;
    const bool sig_f = gamma_significant(file_gamma_), sig_s = gamma_significant(screen_gamma_);
    if (has(kGamma) || (has(kRgbToGray) && (sig_f || sig_s)) ||
        (has(kCompose) && (sig_f || sig_s ||
                           (background_gamma_type_ == BackgroundGamma::kUnique &&
                            gamma_significant(background_gamma_)))) ||
        (has(kEncodeAlpha) && sig_s)) {
      build_gamma_tables();
      if (has(kCompose)) {
        if (has(kRgbToGray) && warn_) warn_("gamma correction with a background and RGB to gray is not supported");
        if (color_type_ == kIndexed) compose_palette_with_gamma();
        else init_background_gamma();
      } else if (color_type_ == kIndexed && (!has(kExpand) || !has(kRgbToGray))) {
        for (int i = 0; i < num_palette_; i++) {
          palette_[i].red = gamma_table_[palette_[i].red];
          palette_[i].green = gamma_table_[palette_[i].green];
          palette_[i].blue = gamma_table_[palette_[i].blue];
        }
        tf_ &= ~kGamma;
      }
    } else if (has(kCompose) && color_type_ == kIndexed) {
      const PaletteEntry back{static_cast<std::uint8_t>(background_.red), static_cast<std::uint8_t>(background_.green),
                              static_cast<std::uint8_t>(background_.blue)};
      for (int i = 0; i < num_trans_; i++) {
        const unsigned a = trans_alpha_[i];
        if (a == 0) {
          palette_[i] = back;
        } else if (a != 0xff) {
          palette_[i].red = composite8(palette_[i].red, a, back.red);
          palette_[i].green = composite8(palette_[i].green, a, back.green);
          palette_[i].blue = composite8(palette_[i].blue, a, back.blue);
        }
      }
      tf_ &= ~kCompose;
    }
    if (has(kShift) && !has(kExpand) && color_type_ == kIndexed) {
      tf_ &= ~kShift;
      auto sh = [&](int bits, std::uint8_t PaletteEntry::*c) {
        const int s = 8 - bits;
        if (s > 0 && s < 8)
          for (int i = 0; i < num_palette_; i++) palette_[i].*c = static_cast<std::uint8_t>(palette_[i].*c >> s);
      };
      sh(shift_.red, &PaletteEntry::red), sh(shift_.green, &PaletteEntry::green), sh(shift_.blue, &PaletteEntry::blue);
    }
  }

  void set_rgb_coefficients(const Info& info) {
    if (coefficients_set_) return;
    std::optional<Chromaticities> c;
    if (info.mastering) {
      const MasteringDisplay& m = *info.mastering;
      c = Chromaticities{m.white_x * 2, m.white_y * 2, m.red_x * 2,  m.red_y * 2,
                         m.green_x * 2, m.green_y * 2, m.blue_x * 2, m.blue_y * 2};
    } else if (!info.srgb_intent && info.chromaticities) {
      c = info.chromaticities;
    }
    std::int32_t r, g, b;
    if (c && primaries_y(*c, &r, &g, &b)) {
      const std::int32_t total = r + g + b;
      if (total > 0 && r >= 0 && muldiv(&r, r, 32768, total) && r >= 0 && r <= 32768 && g >= 0 &&
          muldiv(&g, g, 32768, total) && g >= 0 && g <= 32768 && b >= 0 && muldiv(&b, b, 32768, total) &&
          b >= 0 && b <= 32768 && r + g + b <= 32769) {
        const int add = r + g + b > 32768 ? -1 : r + g + b < 32768 ? 1 : 0;
        if (add) {
          if (g >= r && g >= b) g += add;
          else if (r >= g && r >= b) r += add;
          else b += add;
        }
        rc_ = static_cast<std::uint16_t>(r), gc_ = static_cast<std::uint16_t>(g);
      }
      return;
    }
    rc_ = 6968, gc_ = 23434;
  }

  void init_palette_transformations() {
    bool has_alpha = false, has_transparency = false;
    for (int i = 0; i < num_trans_; i++) {
      if (trans_alpha_[i] == 255) continue;
      has_transparency = true;
      if (trans_alpha_[i] != 0) {
        has_alpha = true;
        break;
      }
    }
    if (!has_alpha) {
      tf_ &= ~kEncodeAlpha;
      optimize_alpha_ = false;
      if (!has_transparency) tf_ &= ~(kCompose | kBackgroundExpand);
    }
    if (has(kBackgroundExpand) && has(kExpand)) {
      const PaletteEntry& p = palette_[background_.index];
      background_.red = p.red, background_.green = p.green, background_.blue = p.blue;
      if (has(kInvertAlpha) && !has(kExpandTrns))
        for (int i = 0; i < num_trans_; i++) trans_alpha_[i] = static_cast<std::uint8_t>(255 - trans_alpha_[i]);
    }
  }

  void init_rgb_transformations() {
    if (!(color_type_ & 4)) {
      tf_ &= ~kEncodeAlpha;
      optimize_alpha_ = false;
      if (num_trans_ == 0) tf_ &= ~(kCompose | kBackgroundExpand);
    }
    if (has(kBackgroundExpand) && has(kExpand) && !(color_type_ & 2)) {
      int gray = background_.gray, trans_gray = trans_color_.gray;
      const int mul = bit_depth_ == 1 ? 0xff : bit_depth_ == 2 ? 0x55 : bit_depth_ == 4 ? 0x11 : 1;
      gray *= mul, trans_gray *= mul;
      background_.red = background_.green = background_.blue = static_cast<std::uint16_t>(gray);
      if (!has(kExpandTrns))
        trans_color_.red = trans_color_.green = trans_color_.blue = static_cast<std::uint16_t>(trans_gray);
    }
  }

  // ---- gamma tables --------------------------------------------------------------

  static void build_8bit_table(std::vector<std::uint8_t>& t, std::int32_t g) {
    t.resize(256);
    const bool sig = gamma_significant(g);
    for (unsigned i = 0; i < 256; i++) t[i] = sig ? gamma_8bit_correct(i, g) : static_cast<std::uint8_t>(i);
  }
  // 16-bit tables are indexed by the value shifted right by gamma_shift_.
  void build_16bit_table(std::vector<std::uint16_t>& t, std::int32_t g) const {
    const unsigned shift = gamma_shift_, max = (1u << (16 - shift)) - 1, max_by_2 = 1u << (15 - shift);
    t.resize(max + 1);
    const bool sig = gamma_significant(g);
    const double fmax = 1.0 / max;
    for (unsigned ig = 0; ig <= max; ig++) {
      if (sig) t[ig] = static_cast<std::uint16_t>(std::floor(65535. * std::pow(ig * fmax, g * .00001) + .5));
      else t[ig] = static_cast<std::uint16_t>(shift ? (ig * 65535u + max_by_2) / max : ig);
    }
  }
  // 16-bit input to 8-bit output (as 16-bit values): `g` is the inverse of
  // the correction; each output level covers the inputs closest to it.
  void build_16to8_table(std::vector<std::uint16_t>& t, std::int32_t g) const {
    const unsigned shift = gamma_shift_, max = (1u << (16 - shift)) - 1;
    t.assign(max + 1, 0);
    std::uint32_t last = 0;
    for (unsigned i = 0; i < 255; i++) {
      const std::uint16_t out = static_cast<std::uint16_t>(i * 257u);
      std::uint32_t bound = gamma_16bit_correct(out + 128u, g);
      bound = (bound * max + 32768u) / 65535u + 1u;
      while (last < bound && last <= max) t[last++] = out;
    }
    while (last <= max) t[last++] = 65535;
  }

  void build_gamma_tables() {
    const std::int32_t file_to_linear = reciprocal(file_gamma_);
    std::int32_t linear_to_screen, correction;
    if (screen_gamma_ > 0) {
      linear_to_screen = reciprocal(screen_gamma_);
      correction = reciprocal2(screen_gamma_, file_gamma_);
    } else {
      linear_to_screen = file_gamma_;
      correction = 100000;
    }
    const bool lin = has(kCompose) || has(kRgbToGray);
    if (bit_depth_ <= 8) {
      build_8bit_table(gamma_table_, correction);
      if (lin) {
        build_8bit_table(gamma_to_1_, file_to_linear);
        build_8bit_table(gamma_from_1_, linear_to_screen);
      }
      return;
    }
    const unsigned sig_bit =
        (color_type_ & 2) ? std::max({sig_bit_.red, sig_bit_.green, sig_bit_.blue}) : sig_bit_.gray;
    unsigned shift = sig_bit > 0 && sig_bit < 16 ? 16 - sig_bit : 0;
    if ((has(kStrip16) || has(kScale16)) && shift < 5) shift = 5;
    if (shift > 8) shift = 8;
    gamma_shift_ = shift;
    if (has(kStrip16) || has(kScale16)) build_16to8_table(gamma_16_, reciprocal(correction));
    else build_16bit_table(gamma_16_, correction);
    if (lin) {
      build_16bit_table(gamma_16_to_1_, file_to_linear);
      build_16bit_table(gamma_16_from_1_, linear_to_screen);
    }
  }
  bool lin_tables() const { return !gamma_to_1_.empty() || !gamma_16_to_1_.empty(); }
  std::uint16_t g16(const std::vector<std::uint16_t>& t, unsigned v) const { return t[v >> gamma_shift_]; }

  // A value of the file's depth corrected by g (8-bit arithmetic only for
  // 8-bit files).
  std::uint16_t gamma_correct(unsigned v, std::int32_t g) const {
    return bit_depth_ == 8 ? gamma_8bit_correct(v, g) : gamma_16bit_correct(v, g);
  }

  void compose_palette_with_gamma() {
    PaletteEntry back, back_1;
    if (background_gamma_type_ == BackgroundGamma::kFile) {
      back = {gamma_table_[background_.red & 0xff], gamma_table_[background_.green & 0xff],
              gamma_table_[background_.blue & 0xff]};
      back_1 = {gamma_to_1_[background_.red & 0xff], gamma_to_1_[background_.green & 0xff],
                gamma_to_1_[background_.blue & 0xff]};
    } else {
      std::int32_t g = 100000, gs = 100000;
      if (background_gamma_type_ == BackgroundGamma::kScreen) {
        g = screen_gamma_;
      } else {
        g = reciprocal(background_gamma_);
        gs = reciprocal2(background_gamma_, screen_gamma_);
      }
      auto corr = [](std::uint16_t v, std::int32_t gg) {
        return gamma_significant(gg) ? gamma_8bit_correct(v, gg) : static_cast<std::uint8_t>(v);
      };
      back = {corr(background_.red, gs), corr(background_.green, gs), corr(background_.blue, gs)};
      back_1 = {corr(background_.red, g), corr(background_.green, g), corr(background_.blue, g)};
    }
    for (int i = 0; i < num_palette_; i++) {
      PaletteEntry& p = palette_[i];
      if (i < num_trans_ && trans_alpha_[i] != 0xff) {
        const unsigned a = trans_alpha_[i];
        if (a == 0) {
          p = back;
        } else if (optimize_alpha_) {
          auto c = [&](std::uint8_t v) { return gamma_from_1_[(gamma_to_1_[v] * a + 128) / 255]; };
          p = {c(p.red), c(p.green), c(p.blue)};
        } else {
          auto c = [&](std::uint8_t v, std::uint8_t b) { return gamma_from_1_[composite8(gamma_to_1_[v], a, b)]; };
          p = {c(p.red, back_1.red), c(p.green, back_1.green), c(p.blue, back_1.blue)};
        }
      } else {
        p = {gamma_table_[p.red], gamma_table_[p.green], gamma_table_[p.blue]};
      }
    }
    tf_ &= ~(kCompose | kGamma);
    optimize_alpha_ = false;
  }

  void init_background_gamma() {
    std::int32_t g = 100000, gs = 100000;
    switch (background_gamma_type_) {
      case BackgroundGamma::kScreen: g = screen_gamma_; break;
      case BackgroundGamma::kFile:
        g = reciprocal(file_gamma_);
        gs = reciprocal2(file_gamma_, screen_gamma_);
        break;
      case BackgroundGamma::kUnique:
        g = reciprocal(background_gamma_);
        gs = reciprocal2(background_gamma_, screen_gamma_);
        break;
    }
    const bool g_sig = gamma_significant(g), gs_sig = gamma_significant(gs);
    if (g_sig) background_1_.gray = gamma_correct(background_.gray, g);
    if (gs_sig) background_.gray = gamma_correct(background_.gray, gs);
    if (background_.red != background_.green || background_.red != background_.blue ||
        background_.red != background_.gray) {
      if (g_sig) {
        background_1_.red = gamma_correct(background_.red, g);
        background_1_.green = gamma_correct(background_.green, g);
        background_1_.blue = gamma_correct(background_.blue, g);
      }
      if (gs_sig) {
        background_.red = gamma_correct(background_.red, gs);
        background_.green = gamma_correct(background_.green, gs);
        background_.blue = gamma_correct(background_.blue, gs);
      }
    } else {
      background_1_.red = background_1_.green = background_1_.blue = background_1_.gray;
      background_.red = background_.green = background_.blue = background_.gray;
    }
    background_gamma_type_ = BackgroundGamma::kScreen;
  }

  static std::uint8_t composite8(unsigned fg, unsigned a, unsigned bg) {
    const unsigned temp = (fg * a + bg * (255 - a) + 128) & 0xffff;
    return static_cast<std::uint8_t>((temp + (temp >> 8)) >> 8);
  }
  static std::uint16_t composite16(std::uint32_t fg, std::uint32_t a, std::uint32_t bg) {
    const std::uint32_t temp = fg * a + bg * (65535 - a) + 32768;
    return static_cast<std::uint16_t>((temp + (temp >> 16)) >> 16);
  }

  // ---- output format --------------------------------------------------------------

  void compute_output(std::uint32_t width) {
    RowInfo o{width, color_type_, bit_depth_, 1};
    if (has(kExpand)) {
      if (o.color_type == kIndexed) {
        o.color_type = num_trans_ > 0 ? kTruecolorAlpha : kTruecolor;
        o.bit_depth = 8;
      } else {
        if (num_trans_ != 0 && has(kExpandTrns)) o.color_type |= 4;
        if (o.bit_depth < 8) o.bit_depth = 8;
      }
    }
    if (o.bit_depth == 16 && (has(kScale16) || has(kStrip16))) o.bit_depth = 8;
    if (has(kGrayToRgb)) o.color_type |= 2;
    if (has(kRgbToGray)) o.color_type &= ~2;
    if (has(kQuantize) && (o.color_type == kTruecolor || o.color_type == kTruecolorAlpha) &&
        !palette_lookup_.empty() && o.bit_depth == 8)
      o.color_type = kIndexed;
    if (has(kExpand16) && o.bit_depth == 8 && o.color_type != kIndexed) o.bit_depth = 16;
    if (has(kPack) && o.bit_depth < 8) o.bit_depth = 8;
    o.channels = o.color_type == kIndexed ? 1 : (o.color_type & 2) ? 3 : 1;
    if (has(kStripAlpha)) o.color_type &= ~4;
    if (o.color_type & 4) o.channels++;
    if (has(kFiller) && (o.color_type == kTruecolor || o.color_type == kGray)) {
      o.channels++;
      if (has(kAddAlpha)) o.color_type |= 4;
    }
    if (has(kUser)) {
      if (t_.user_depth) o.bit_depth = t_.user_depth;
      if (t_.user_channels) o.channels = t_.user_channels;
    }
    out_ = o;
  }

  // ---- row steps ----------------------------------------------------------------
  // s_ holds the samples of the row (one per entry), r describes them.

  void unpack(const std::uint8_t* in, const RowInfo& r) {
    const std::size_t n = samples(r);
    if (r.bit_depth == 16) {
      for (std::size_t i = 0; i < n; i++) s_[i] = static_cast<std::uint16_t>(in[2 * i] << 8 | in[2 * i + 1]);
    } else if (r.bit_depth == 8) {
      for (std::size_t i = 0; i < n; i++) s_[i] = in[i];
    } else {
      const unsigned b = r.bit_depth, mask = (1u << b) - 1;
      for (std::size_t i = 0; i < n; i++)
        s_[i] = static_cast<std::uint16_t>((in[i * b / 8] >> (8 - b - (i * b) % 8)) & mask);
    }
  }

  void pack(std::uint8_t* out, const RowInfo& r, bool packswap) const {
    const std::size_t n = samples(r);
    if (r.bit_depth == 16) {
      const unsigned swap = has(kSwapBytes) ? 1 : 0;
      for (std::size_t i = 0; i < n; i++) {
        out[2 * i + swap] = static_cast<std::uint8_t>(s_[i] >> 8);
        out[2 * i + 1 - swap] = static_cast<std::uint8_t>(s_[i]);
      }
    } else if (r.bit_depth == 8) {
      for (std::size_t i = 0; i < n; i++) out[i] = static_cast<std::uint8_t>(s_[i]);
    } else {
      const unsigned depth = r.bit_depth;
      std::memset(out, 0, (n * depth + 7) / 8);
      for (std::size_t i = 0; i < n; i++) {
        const unsigned pos = static_cast<unsigned>((i * depth) % 8);
        const unsigned shift = packswap ? pos : 8 - depth - pos;
        out[i * depth / 8] |= static_cast<std::uint8_t>((s_[i] & ((1u << depth) - 1)) << shift);
      }
    }
  }

  void expand_palette(RowInfo& r) {
    const bool alpha = num_trans_ > 0;
    const unsigned ch = alpha ? 4 : 3;
    for (std::size_t x = r.width; x-- > 0;) {
      const unsigned i = s_[x] & 0xff;
      const PaletteEntry p = palette_[i];
      s_[x * ch] = p.red, s_[x * ch + 1] = p.green, s_[x * ch + 2] = p.blue;
      if (alpha) s_[x * ch + 3] = static_cast<int>(i) < num_trans_ ? trans_alpha_[i] : 255;
    }
    r.color_type = alpha ? kTruecolorAlpha : kTruecolor;
    r.bit_depth = 8;
    r.channels = static_cast<std::uint8_t>(ch);
  }

  void expand(RowInfo& r, bool trns) {
    const std::size_t w = r.width;
    if (r.color_type == kGray) {
      unsigned gray = trns ? trans_color_.gray : 0;
      if (r.bit_depth < 8) {
        const unsigned mul = r.bit_depth == 1 ? 0xff : r.bit_depth == 2 ? 0x55 : 0x11;
        gray = (gray & ((1u << r.bit_depth) - 1)) * mul;
        for (std::size_t i = 0; i < w; i++) s_[i] = static_cast<std::uint16_t>(s_[i] * mul);
        r.bit_depth = 8;
      }
      if (trns) {
        const std::uint16_t max = r.bit_depth == 16 ? 0xffff : 0xff;
        if (r.bit_depth == 8) gray &= 0xff;
        for (std::size_t x = w; x-- > 0;) {
          const std::uint16_t g = s_[x];
          s_[x * 2] = g;
          s_[x * 2 + 1] = g == gray ? 0 : max;
        }
        r.color_type = kGrayAlpha;
        r.channels = 2;
      }
    } else if (r.color_type == kTruecolor && trns) {
      const std::uint16_t max = r.bit_depth == 16 ? 0xffff : 0xff;
      const std::uint16_t kr = r.bit_depth == 16 ? trans_color_.red : trans_color_.red & 0xff;
      const std::uint16_t kg = r.bit_depth == 16 ? trans_color_.green : trans_color_.green & 0xff;
      const std::uint16_t kb = r.bit_depth == 16 ? trans_color_.blue : trans_color_.blue & 0xff;
      for (std::size_t x = w; x-- > 0;) {
        const std::uint16_t c0 = s_[x * 3], c1 = s_[x * 3 + 1], c2 = s_[x * 3 + 2];
        s_[x * 4] = c0, s_[x * 4 + 1] = c1, s_[x * 4 + 2] = c2;
        s_[x * 4 + 3] = c0 == kr && c1 == kg && c2 == kb ? 0 : max;
      }
      r.color_type = kTruecolorAlpha;
      r.channels = 4;
    }
  }

  void strip_alpha(RowInfo& r) {
    const unsigned ch = r.channels;
    std::size_t d = 0;
    for (std::size_t x = 0; x < r.width; x++)
      for (unsigned c = 0; c + 1 < ch; c++) s_[d++] = s_[x * ch + c];
    r.channels--;
    r.color_type &= ~4;
  }

  bool rgb_to_gray(RowInfo& r) {
    if (r.color_type == kIndexed || !(r.color_type & 2)) return false;
    const std::uint32_t rc = rc_, gc = gc_, bc = 32768 - rc - gc;
    const bool alpha = (r.color_type & 4) != 0;
    const unsigned ch = r.channels, out = alpha ? 2 : 1;
    bool err = false;
    const bool lin = lin_tables();
    for (std::size_t x = 0; x < r.width; x++) {
      const unsigned red = s_[x * ch], green = s_[x * ch + 1], blue = s_[x * ch + 2];
      const std::uint16_t a = alpha ? s_[x * ch + 3] : 0;
      unsigned g;
      if (r.bit_depth == 8) {
        if (red != green || red != blue) {
          err = true;
          if (lin)
            g = gamma_from_1_[(rc * gamma_to_1_[red] + gc * gamma_to_1_[green] + bc * gamma_to_1_[blue] + 16384) >> 15];
          else
            g = (rc * red + gc * green + bc * blue) >> 15;
        } else {
          g = lin && !gamma_table_.empty() ? gamma_table_[red] : red;
        }
      } else if (lin) {
        if (red == green && red == blue) {
          g = !gamma_16_.empty() ? g16(gamma_16_, red) : red;
        } else {
          err = true;
          const unsigned gray16 = ((rc * g16(gamma_16_to_1_, red) + gc * g16(gamma_16_to_1_, green) +
                                    bc * g16(gamma_16_to_1_, blue) + 16384) >> 15) & 0xffff;
          g = g16(gamma_16_from_1_, gray16);
        }
      } else {
        if (red != green || red != blue) err = true;
        g = ((rc * red + gc * green + bc * blue + 16384) >> 15) & 0xffff;
      }
      s_[x * out] = static_cast<std::uint16_t>(g);
      if (alpha) s_[x * out + 1] = a;
    }
    r.color_type &= ~2;
    r.channels = static_cast<std::uint8_t>(out);
    return err;
  }

  void gray_to_rgb(RowInfo& r) {
    if (r.bit_depth < 8 || (r.color_type & 2)) return;
    const bool alpha = (r.color_type & 4) != 0;
    const unsigned in = alpha ? 2 : 1, out = alpha ? 4 : 3;
    for (std::size_t x = r.width; x-- > 0;) {
      const std::uint16_t g = s_[x * in], a = alpha ? s_[x * in + 1] : 0;
      s_[x * out] = s_[x * out + 1] = s_[x * out + 2] = g;
      if (alpha) s_[x * out + 3] = a;
    }
    r.color_type |= 2;
    r.channels = static_cast<std::uint8_t>(out);
  }

  // Gamma correction of a sample of the row's depth by the main table.
  std::uint16_t correct(unsigned v, unsigned depth) const {
    switch (depth) {
      case 2: return static_cast<std::uint16_t>(gamma_table_[v * 0x55] >> 6);
      case 4: return static_cast<std::uint16_t>(gamma_table_[v * 0x11] >> 4);
      case 8: return gamma_table_[v];
      case 16: return g16(gamma_16_, v);
      default: return static_cast<std::uint16_t>(v);
    }
  }

  void compose(RowInfo& r) {
    const unsigned ch = r.channels, depth = r.bit_depth;
    // the background as stored in a sample (8-bit rows keep the low byte)
    const unsigned keep = depth == 16 ? 0xffff : 0xff;
    const bool tab = depth == 16 ? !gamma_16_.empty() : !gamma_table_.empty();
    switch (r.color_type) {
      case kGray:
        if (depth < 8) {
          // On the packed bytes: a background wider than the samples spills
          // into the pixels before it in the byte (kept for compatibility).
          std::vector<std::uint8_t> b((r.width * depth + 7) / 8);
          pack(b.data(), r, false);
          const unsigned mask = (1u << depth) - 1;
          for (std::size_t x = 0; x < r.width; x++) {
            std::uint8_t& byte = b[x * depth / 8];
            const unsigned shift = 8 - depth - static_cast<unsigned>((x * depth) % 8);
            const unsigned v = (byte >> shift) & mask;
            unsigned keep = byte & ~(mask << shift) & 0xff;
            if (v == trans_color_.gray) byte = static_cast<std::uint8_t>(keep | (background_.gray << shift));
            else if (tab && depth != 1) byte = static_cast<std::uint8_t>(keep | (correct(v, depth) & mask) << shift);
          }
          unpack(b.data(), r);
        } else {
          for (std::size_t x = 0; x < r.width; x++) {
            std::uint16_t& v = s_[x];
            if (v == trans_color_.gray) v = static_cast<std::uint16_t>(background_.gray & keep);
            else if (tab) v = correct(v, depth);
          }
        }
        break;
      case kTruecolor:
        for (std::size_t x = 0; x < r.width; x++) {
          std::uint16_t* p = &s_[x * 3];
          if (p[0] == trans_color_.red && p[1] == trans_color_.green && p[2] == trans_color_.blue) {
            p[0] = static_cast<std::uint16_t>(background_.red & keep);
            p[1] = static_cast<std::uint16_t>(background_.green & keep);
            p[2] = static_cast<std::uint16_t>(background_.blue & keep);
          } else if (tab) {
            for (int c = 0; c < 3; c++) p[c] = correct(p[c], depth);
          }
        }
        break;
      case kGrayAlpha:
      case kTruecolorAlpha: {
        const unsigned nc = ch - 1;
        const std::uint16_t bg[3] = {nc == 1 ? background_.gray : background_.red, background_.green,
                                     background_.blue};
        const std::uint16_t bgk[3] = {static_cast<std::uint16_t>(bg[0] & keep),
                                      static_cast<std::uint16_t>(bg[1] & keep),
                                      static_cast<std::uint16_t>(bg[2] & keep)};
        const std::uint16_t bg1[3] = {nc == 1 ? background_1_.gray : background_1_.red, background_1_.green,
                                      background_1_.blue};
        const bool lin = depth == 8 ? !gamma_to_1_.empty() && !gamma_from_1_.empty() && !gamma_table_.empty()
                                    : !gamma_16_.empty() && !gamma_16_from_1_.empty() && !gamma_16_to_1_.empty();
        const unsigned max = depth == 8 ? 0xff : 0xffff;
        for (std::size_t x = 0; x < r.width; x++) {
          std::uint16_t* p = &s_[x * ch];
          const unsigned a = p[nc];
          if (lin) {
            if (a == max) {
              for (unsigned c = 0; c < nc; c++) p[c] = correct(p[c], depth);
            } else if (a == 0) {
              for (unsigned c = 0; c < nc; c++) p[c] = bgk[c];
            } else if (depth == 8) {
              for (unsigned c = 0; c < nc; c++) {
                std::uint8_t w = composite8(gamma_to_1_[p[c]], a, bg1[c]);
                if (!optimize_alpha_) w = gamma_from_1_[w];
                p[c] = w;
              }
            } else {
              for (unsigned c = 0; c < nc; c++) {
                const std::uint16_t v = composite16(g16(gamma_16_to_1_, p[c]), a, bg1[c]);
                p[c] = optimize_alpha_ ? v : g16(gamma_16_from_1_, v);
              }
            }
          } else if (a == 0) {
            for (unsigned c = 0; c < nc; c++) p[c] = bgk[c];
          } else if (a < max) {
            for (unsigned c = 0; c < nc; c++)
              p[c] = depth == 8 ? composite8(p[c], a, bg[c]) : composite16(p[c], a, bg[c]);
          }
        }
        break;
      }
      default: break;
    }
  }

  void gamma(RowInfo& r) {
    const unsigned depth = r.bit_depth;
    if (depth == 16 ? gamma_16_.empty() : gamma_table_.empty()) return;
    if (r.color_type == kIndexed || depth == 1) return;
    const unsigned ch = r.channels, nc = (r.color_type & 4) ? ch - 1 : ch;
    for (std::size_t x = 0; x < r.width; x++)
      for (unsigned c = 0; c < nc; c++) s_[x * ch + c] = correct(s_[x * ch + c], depth);
  }

  void encode_alpha(RowInfo& r) {
    const unsigned ch = r.channels;
    if (r.bit_depth == 8 && !gamma_from_1_.empty()) {
      for (std::size_t x = 0; x < r.width; x++) s_[x * ch + ch - 1] = gamma_from_1_[s_[x * ch + ch - 1]];
    } else if (r.bit_depth == 16 && !gamma_16_from_1_.empty()) {
      for (std::size_t x = 0; x < r.width; x++) s_[x * ch + ch - 1] = g16(gamma_16_from_1_, s_[x * ch + ch - 1]);
    } else if (warn_) {
      warn_("cannot encode alpha");
    }
  }

  void scale_16(RowInfo& r) {
    for (std::size_t i = 0, n = samples(r); i < n; i++) {
      const std::int32_t hi = s_[i] >> 8, lo = s_[i] & 0xff;
      s_[i] = static_cast<std::uint16_t>(hi + (((lo - hi + 128) * 65535) >> 24));
    }
    r.bit_depth = 8;
  }
  void strip_16(RowInfo& r) {
    for (std::size_t i = 0, n = samples(r); i < n; i++) s_[i] = static_cast<std::uint16_t>(s_[i] >> 8);
    r.bit_depth = 8;
  }

  void quantize(RowInfo& r) {
    if (r.bit_depth != 8) return;
    if ((r.color_type == kTruecolor || r.color_type == kTruecolorAlpha) && !palette_lookup_.empty()) {
      const unsigned ch = r.channels;
      for (std::size_t x = 0; x < r.width; x++) {
        const std::uint16_t* p = &s_[x * ch];
        s_[x] = palette_lookup_[((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3)];
      }
      r.color_type = kIndexed;
      r.channels = 1;
    } else if (r.color_type == kIndexed && !quantize_index_.empty()) {
      for (std::size_t x = 0; x < r.width; x++) s_[x] = quantize_index_[s_[x] & 0xff];
    }
  }

  void invert_mono(RowInfo& r) {
    if (r.color_type == kGray) {
      const unsigned max = (1u << r.bit_depth) - 1;
      for (std::size_t x = 0; x < r.width; x++) s_[x] = static_cast<std::uint16_t>(max - s_[x]);
    } else if (r.color_type == kGrayAlpha && (r.bit_depth == 8 || r.bit_depth == 16)) {
      const unsigned max = (1u << r.bit_depth) - 1;
      for (std::size_t x = 0; x < r.width; x++) s_[x * 2] = static_cast<std::uint16_t>(max - s_[x * 2]);
    }
  }

  void unshift(RowInfo& r) {
    if (r.color_type == kIndexed) return;
    const int depth = r.bit_depth;
    int shift[4], n = 0;
    if (r.color_type & 2) {
      shift[n++] = depth - shift_.red;
      shift[n++] = depth - shift_.green;
      shift[n++] = depth - shift_.blue;
    } else {
      shift[n++] = depth - shift_.gray;
    }
    if (r.color_type & 4) shift[n++] = depth - shift_.alpha;
    bool any = false;
    for (int c = 0; c < n; c++) {
      if (shift[c] <= 0 || shift[c] >= depth) shift[c] = 0;
      else any = true;
    }
    if (!any || depth == 1) return;
    if (depth < 8) {
      // packed gray samples (2 bits: only a shift by 1 is possible)
      for (std::size_t i = 0, k = samples(r); i < k; i++) s_[i] = static_cast<std::uint16_t>(s_[i] >> shift[0]);
      return;
    }
    const unsigned ch = r.channels;
    for (std::size_t x = 0; x < r.width; x++)
      for (unsigned c = 0; c < ch; c++) s_[x * ch + c] = static_cast<std::uint16_t>(s_[x * ch + c] >> shift[c % n]);
  }

  void filler(RowInfo& r) {
    if (!(r.color_type == kGray || r.color_type == kTruecolor) || (r.bit_depth != 8 && r.bit_depth != 16)) return;
    const std::uint16_t v =
        r.bit_depth == 8 ? static_cast<std::uint16_t>(t_.filler->value & 0xff) : t_.filler->value;
    const unsigned in = r.channels, out = in + 1, before = t_.filler->before ? 1 : 0;
    for (std::size_t x = r.width; x-- > 0;) {
      std::uint16_t c[3];
      for (unsigned k = 0; k < in; k++) c[k] = s_[x * in + k];
      std::uint16_t* p = &s_[x * out];
      for (unsigned k = 0; k < in; k++) p[k + before] = c[k];
      p[before ? 0 : in] = v;
    }
    // (the row's color type is left alone: an added alpha channel is not
    // swapped or inverted)
    r.channels = static_cast<std::uint8_t>(out);
  }

  Transforms t_;
  std::function<void(const std::string&)> warn_;
  std::uint32_t tf_ = 0;
  std::uint8_t color_type_ = 0, bit_depth_ = 8;
  std::vector<PaletteEntry> palette_;  // 256 entries, num_palette_ used
  int num_palette_ = 0, file_palette_size_ = 0, num_trans_ = 0;
  std::vector<std::uint8_t> trans_alpha_;
  Color16 trans_color_, background_, background_1_;
  SignificantBits sig_bit_, shift_;
  BackgroundGamma background_gamma_type_ = BackgroundGamma::kScreen;
  std::int32_t file_gamma_ = 0, screen_gamma_ = 0, default_gamma_ = 0, chunk_gamma_ = 0, background_gamma_ = 0;
  bool optimize_alpha_ = false, background_is_gray_ = false, coefficients_set_ = false, rgb_error_seen_ = false;
  std::uint16_t rc_ = 6968, gc_ = 23434;
  std::vector<std::uint8_t> gamma_table_, gamma_to_1_, gamma_from_1_;
  std::vector<std::uint16_t> gamma_16_, gamma_16_to_1_, gamma_16_from_1_;
  unsigned gamma_shift_ = 0;
  std::vector<std::uint8_t> palette_lookup_, quantize_index_;
  RowInfo out_;
  std::vector<std::uint16_t> s_;
};

// Converts rows given to the encoder (in the format WriteTransforms
// describes) into native layout: PNG sample order, 16-bit samples in host
// byte order.
class WritePlan {
 public:
  void init(const Header& h, const WriteTransforms& t) {
    t_ = t;
    color_type_ = h.color_type;
    bit_depth_ = h.bit_depth;
    channels_ = static_cast<std::uint8_t>(num_channels(h.color_type));
    in_channels_ = channels_;
    if (t.strip_filler && (h.color_type == kTruecolor || (h.color_type == kGray && h.bit_depth >= 8)))
      in_channels_++;
    in_depth_ = t.packing && h.bit_depth < 8 ? 8 : h.bit_depth;
    shift_ = false;
    if (t.shift) {
      // Bits not given: a gray value's for the colors (an image may be
      // converted to color), the depth's for alpha.
      SignificantBits b = *t.shift;
      const std::uint8_t depth = color_type_ == kIndexed ? 8 : bit_depth_;
      if (!(color_type_ & 2)) {
        if (!b.red) b.red = b.gray;
        if (!b.green) b.green = b.gray;
        if (!b.blue) b.blue = b.gray;
      }
      if (!b.alpha) b.alpha = depth;
      const unsigned d = h.bit_depth;
      bool ok = (h.color_type & 2) ? b.red && b.red <= d && b.green && b.green <= d && b.blue && b.blue <= d
                                   : b.gray && b.gray <= d;
      if (h.color_type & 4) ok = ok && b.alpha && b.alpha <= d;
      if (h.color_type == kIndexed) ok = false;
      shift_ = ok;
    }
    swap_in_ = t.byte_order == ByteOrder::kLittle;
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
    if (t.byte_order == ByteOrder::kHost) swap_in_ = true;
#endif
  }
  // Bytes per input row of `width` pixels.
  std::size_t input_row_bytes(std::uint32_t width) const {
    return (static_cast<std::size_t>(width) * in_channels_ * in_depth_ + 7) / 8;
  }
  std::size_t row_bytes(std::uint32_t width) const {
    return (static_cast<std::size_t>(width) * channels_ * bit_depth_ + 7) / 8;
  }

  void run(const std::uint8_t* in, std::uint32_t width, std::uint8_t* out) {
    const std::size_t irb = input_row_bytes(width);
    if (t_.user) {
      buf_.assign(in, in + irb);
      RowInfo u{width, color_type_, in_depth_, in_channels_};
      t_.user(u, buf_.data());
      in = buf_.data();
    }
    // samples
    const std::size_t n = static_cast<std::size_t>(width) * in_channels_;
    s_.resize(n + 4);
    if (in_depth_ == 16) {
      for (std::size_t i = 0; i < n; i++)
        s_[i] = swap_in_ ? static_cast<std::uint16_t>(in[2 * i + 1] << 8 | in[2 * i])
                         : static_cast<std::uint16_t>(in[2 * i] << 8 | in[2 * i + 1]);
    } else if (in_depth_ == 8) {
      for (std::size_t i = 0; i < n; i++) s_[i] = in[i];
    } else {
      const unsigned b = in_depth_, mask = (1u << b) - 1;
      for (std::size_t i = 0; i < n; i++) {
        const unsigned pos = static_cast<unsigned>((i * b) % 8);
        s_[i] = static_cast<std::uint16_t>((in[i * b / 8] >> (t_.packswap ? pos : 8 - b - pos)) & mask);
      }
    }
    unsigned ch = in_channels_;
    if (ch != channels_) {  // drop the filler
      const unsigned skip = t_.strip_filler->before ? 0 : ch - 1;
      std::size_t d = 0;
      for (std::size_t x = 0; x < width; x++)
        for (unsigned c = 0; c < ch; c++)
          if (c != skip) s_[d++] = s_[x * ch + c];
      ch = channels_;
    }
    if (in_depth_ != bit_depth_) {
      // one sample per byte: 1-bit samples are set if nonzero, others keep
      // their low bits
      for (std::size_t i = 0, k = static_cast<std::size_t>(width) * ch; i < k; i++)
        s_[i] = bit_depth_ == 1 ? (s_[i] != 0) : static_cast<std::uint16_t>(s_[i] & ((1u << bit_depth_) - 1));
    }
    if (shift_) scale_up(width, ch);
    const bool alpha = (color_type_ & 4) != 0;
    if (t_.swap_alpha && alpha)
      for (std::size_t x = 0; x < width; x++) {
        std::uint16_t* p = &s_[x * ch];
        const std::uint16_t a = p[0];
        for (unsigned c = 0; c + 1 < ch; c++) p[c] = p[c + 1];
        p[ch - 1] = a;
      }
    if (t_.invert_alpha && alpha) {
      const unsigned max = (1u << bit_depth_) - 1;
      for (std::size_t x = 0; x < width; x++) s_[x * ch + ch - 1] = static_cast<std::uint16_t>(max - s_[x * ch + ch - 1]);
    }
    if (t_.bgr && (color_type_ == kTruecolor || color_type_ == kTruecolorAlpha))
      for (std::size_t x = 0; x < width; x++) std::swap(s_[x * ch], s_[x * ch + 2]);
    if (t_.invert_mono && (color_type_ == kGray || (color_type_ == kGrayAlpha && bit_depth_ >= 8))) {
      const unsigned max = (1u << bit_depth_) - 1;
      for (std::size_t x = 0; x < width; x++) s_[x * ch] = static_cast<std::uint16_t>(max - s_[x * ch]);
    }
    // native layout
    const std::size_t k = static_cast<std::size_t>(width) * ch;
    if (bit_depth_ == 16) {
      std::memcpy(out, s_.data(), k * 2);
    } else if (bit_depth_ == 8) {
      for (std::size_t i = 0; i < k; i++) out[i] = static_cast<std::uint8_t>(s_[i]);
    } else {
      pack_msb(out, k);
    }
  }

 private:
  void pack_msb(std::uint8_t* out, std::size_t k) const {
    const unsigned b = bit_depth_;
    std::memset(out, 0, (k * b + 7) / 8);
    for (std::size_t i = 0; i < k; i++)
      out[i * b / 8] |= static_cast<std::uint8_t>((s_[i] & ((1u << b) - 1)) << (8 - b - (i * b) % 8));
  }

  // Each sample's significant bits repeated down to fill the bit depth.
  void scale_up(std::uint32_t width, unsigned ch) {
    const SignificantBits& b = *t_.shift;
    int start[4], dec[4], n = 0;
    const int depth = bit_depth_;
    if (color_type_ & 2) {
      start[n] = depth - b.red, dec[n++] = b.red;
      start[n] = depth - b.green, dec[n++] = b.green;
      start[n] = depth - b.blue, dec[n++] = b.blue;
    } else {
      start[n] = depth - b.gray, dec[n++] = b.gray;
    }
    if (color_type_ & 4) start[n] = depth - b.alpha, dec[n++] = b.alpha;
    if (depth < 8) {
      // on whole bytes, as the traditional code does
      std::vector<std::uint8_t> bytes((static_cast<std::size_t>(width) * depth + 7) / 8);
      pack_msb(bytes.data(), width);
      const unsigned mask = (b.gray == 1 && depth == 2) ? 0x55 : (depth == 4 && b.gray == 3) ? 0x11 : 0xff;
      for (std::uint8_t& v8 : bytes) {
        const unsigned v = v8;
        unsigned o = 0;
        for (int j = start[0]; j > -dec[0]; j -= dec[0]) o |= j > 0 ? v << j : (v >> -j) & mask;
        v8 = static_cast<std::uint8_t>(o);
      }
      for (std::size_t i = 0; i < width; i++)
        s_[i] = static_cast<std::uint16_t>((bytes[i * depth / 8] >> (8 - depth - (i * depth) % 8)) & ((1u << depth) - 1));
      return;
    }
    const unsigned keep = depth == 16 ? 0xffff : 0xff;
    for (std::size_t x = 0; x < width; x++)
      for (unsigned c = 0; c < ch; c++) {
        const unsigned v = s_[x * ch + c];
        unsigned o = 0;
        for (int j = start[c]; j > -dec[c]; j -= dec[c]) o |= j > 0 ? v << j : v >> -j;
        s_[x * ch + c] = static_cast<std::uint16_t>(o & keep);
      }
  }

  WriteTransforms t_;
  std::uint8_t color_type_ = 0, bit_depth_ = 8, channels_ = 1, in_channels_ = 1, in_depth_ = 8;
  bool shift_ = false, swap_in_ = false;
  std::vector<std::uint8_t> buf_;
  std::vector<std::uint16_t> s_;
};

}  // namespace detail

}  // namespace vio::png
