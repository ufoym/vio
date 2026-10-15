// vio::png - the contents of a PNG file other than the image data.
//
// png::Info holds every standard chunk, the APNG animation chunks, text, and
// the chunks the codec does not know (kept on request).  Values keep the
// file's own representation: gamma and chromaticities are integers scaled
// by 100000, mastering-display primaries by 50000, luminances by 10000.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common.hpp"

namespace vio::png {

// Where a chunk is, or is to be written, relative to PLTE and IDAT.
enum ChunkLocation : std::uint8_t {
  kBeforePLTE = 0x01,
  kBeforeIDAT = 0x02,  // after PLTE
  kAfterIDAT = 0x08,
};

// A background or transparent color: a palette index, a gray level or an
// RGB triple, in the image's sample depth.
struct Color16 {
  std::uint8_t index = 0;
  std::uint16_t red = 0, green = 0, blue = 0, gray = 0;
  bool operator==(const Color16&) const = default;
};

// sBIT: significant bits per channel.
struct SignificantBits {
  std::uint8_t red = 0, green = 0, blue = 0, gray = 0, alpha = 0;
  bool operator==(const SignificantBits&) const = default;
};

// cHRM: CIE xy chromaticities, scaled by 100000.
struct Chromaticities {
  std::int32_t white_x = 0, white_y = 0, red_x = 0, red_y = 0, green_x = 0, green_y = 0,
               blue_x = 0, blue_y = 0;
  bool operator==(const Chromaticities&) const = default;
};

// cICP: coding-independent code points (ITU-T H.273).
struct Cicp {
  std::uint8_t colour_primaries = 0, transfer_function = 0, matrix_coefficients = 0,
               full_range = 0;
  bool operator==(const Cicp&) const = default;
};

// mDCV: mastering display color volume.  Chromaticities scaled by 50000,
// luminances in cd/m^2 scaled by 10000.
struct MasteringDisplay {
  std::uint16_t red_x = 0, red_y = 0, green_x = 0, green_y = 0, blue_x = 0, blue_y = 0,
                white_x = 0, white_y = 0;
  std::uint32_t max_luminance = 0, min_luminance = 0;
  bool operator==(const MasteringDisplay&) const = default;
};

// cLLI: content light levels in cd/m^2 scaled by 10000.
struct ContentLight {
  std::uint32_t max_cll = 0, max_fall = 0;
  bool operator==(const ContentLight&) const = default;
};

// pHYs; unit 0 = unknown (aspect ratio only), 1 = meter.
struct Physical {
  std::uint32_t x = 0, y = 0;
  std::uint8_t unit = 0;
  bool operator==(const Physical&) const = default;
};

// oFFs; unit 0 = pixel, 1 = micrometer.
struct Offset {
  std::int32_t x = 0, y = 0;
  std::uint8_t unit = 0;
  bool operator==(const Offset&) const = default;
};

// pCAL: calibration of pixel values; equation types 0 linear, 1 base-e
// exponential, 2 arbitrary-base exponential, 3 hyperbolic.
struct Calibration {
  std::string purpose;
  std::int32_t x0 = 0, x1 = 0;
  std::uint8_t type = 0;
  std::string units;
  std::vector<std::string> params;
  bool operator==(const Calibration&) const = default;
};

// sCAL: physical scale; unit 1 = meter, 2 = radian; width and height as
// floating-point numbers in text form.
struct Scale {
  std::uint8_t unit = 1;
  std::string width, height;
  bool operator==(const Scale&) const = default;
};

// tIME: last modification, UTC.
struct Time {
  std::uint16_t year = 0;
  std::uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
  bool operator==(const Time&) const = default;
};

// tEXt, zTXt, iTXt.
struct Text {
  enum Type : std::uint8_t { kText, kZtxt, kItxt, kItxtCompressed };
  Type type = kText;
  std::string keyword;
  std::string text;  // Latin-1 (tEXt, zTXt) or UTF-8 (iTXt)
  std::string language, translated_keyword;  // iTXt only
  // Where it was found, or where to write it (before or after IDAT).
  std::uint8_t location = kBeforeIDAT;
  bool operator==(const Text&) const = default;
};

// sPLT: a suggested palette.
struct SuggestedPalette {
  struct Entry {
    std::uint16_t red = 0, green = 0, blue = 0, alpha = 0, frequency = 0;
    bool operator==(const Entry&) const = default;
  };
  std::string name;
  std::uint8_t depth = 8;  // 8 or 16
  std::vector<Entry> entries;
  bool operator==(const SuggestedPalette&) const = default;
};

// iCCP.
struct IccProfile {
  std::string name;
  std::vector<std::uint8_t> data;  // decompressed
  bool operator==(const IccProfile&) const = default;
};

// A chunk the codec does not interpret.
struct UnknownChunk {
  std::array<char, 4> type{};
  std::vector<std::uint8_t> data;
  std::uint8_t location = kBeforeIDAT;
  std::string name() const { return std::string(type.data(), 4); }
  bool operator==(const UnknownChunk&) const = default;
};

// APNG.
struct Animation {
  std::uint32_t num_frames = 1;  // including the default image if shown
  std::uint32_t num_plays = 0;   // 0 = loop forever
  bool operator==(const Animation&) const = default;
};

struct FrameControl {
  enum Dispose : std::uint8_t { kDisposeNone, kDisposeBackground, kDisposePrevious };
  enum Blend : std::uint8_t { kBlendSource, kBlendOver };
  std::uint32_t width = 0, height = 0, x = 0, y = 0;
  std::uint16_t delay_num = 0, delay_den = 0;
  std::uint8_t dispose = kDisposeNone, blend = kBlendSource;
  bool operator==(const FrameControl&) const = default;
};

struct Info {
  Header header;
  std::vector<PaletteEntry> palette;
  // tRNS: alpha per palette entry, or the transparent gray/RGB value
  std::vector<std::uint8_t> trans_alpha;
  std::optional<Color16> trans_color;
  std::optional<std::uint32_t> gamma;  // gAMA, scaled by 100000
  std::optional<Chromaticities> chromaticities;
  std::optional<std::uint8_t> srgb_intent;  // sRGB rendering intent 0..3
  std::optional<IccProfile> icc;
  std::optional<Cicp> cicp;
  std::optional<MasteringDisplay> mastering;
  std::optional<ContentLight> content_light;
  std::optional<SignificantBits> sbit;
  std::optional<Color16> background;
  std::vector<std::uint16_t> histogram;
  std::optional<Physical> phys;
  std::optional<Offset> offs;
  std::optional<Calibration> pcal;
  std::optional<Scale> scal;
  std::vector<SuggestedPalette> splt;
  std::optional<Time> time;
  std::vector<Text> text;
  std::optional<std::vector<std::uint8_t>> exif;
  std::vector<UnknownChunk> unknown;
  std::optional<Animation> animation;
  // fcTL of every frame, in order; the first one is the default image's
  // unless the default image is not part of the animation
  std::vector<FrameControl> frames;
  bool default_image_is_frame = false;

  bool has_trns() const { return !trans_alpha.empty() || trans_color.has_value(); }
  bool operator==(const Info&) const = default;
};

}  // namespace vio::png
