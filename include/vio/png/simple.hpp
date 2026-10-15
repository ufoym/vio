// vio::png - reading and writing images in a chosen pixel format.
//
// png::FormatReader converts any PNG to one of a set of simple layouts:
// gray, gray+alpha, RGB or RGBA (alpha first or last, BGR order), with
// 8-bit sRGB-encoded samples or 16-bit linear ones (alpha premultiplied),
// or a color-mapped image (one byte per pixel and a colormap in one of
// those formats).  Alpha that the format cannot hold is composited on a
// background color, or on what the buffer already holds.  png::FormatWriter
// writes such buffers.
//
//   png::FormatReader r(file);          // reads the header
//   r.format = png::kFormatRgba;       // what is wanted
//   std::vector<std::uint8_t> px(r.buffer_size());
//   r.read(px.data());
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "decoder.hpp"
#include "encoder.hpp"

namespace vio::png {

// Pixel format bits.
enum : std::uint32_t {
  kFormatAlpha = 0x01,
  kFormatColor = 0x02,
  kFormatLinear = 0x04,    // 16-bit linear samples, else 8-bit sRGB
  kFormatColormap = 0x08,  // one byte per pixel indexing a colormap
  kFormatBgr = 0x10,
  kFormatAlphaFirst = 0x20,
  kFormatAssociatedAlpha = 0x40,  // 8-bit: premultiplied, not linear
};
inline constexpr std::uint32_t kFormatGray = 0, kFormatGa = kFormatAlpha,
                               kFormatAg = kFormatGa | kFormatAlphaFirst, kFormatRgb = kFormatColor,
                               kFormatBgr_ = kFormatColor | kFormatBgr, kFormatRgba = kFormatRgb | kFormatAlpha,
                               kFormatArgb = kFormatRgba | kFormatAlphaFirst,
                               kFormatBgra = kFormatBgr_ | kFormatAlpha,
                               kFormatAbgr = kFormatBgra | kFormatAlphaFirst,
                               kFormatLinearY = kFormatLinear, kFormatLinearYAlpha = kFormatLinear | kFormatAlpha,
                               kFormatLinearRgb = kFormatLinear | kFormatColor,
                               kFormatLinearRgbAlpha = kFormatLinear | kFormatColor | kFormatAlpha;

// Image flags.
enum : std::uint32_t {
  kImageNotSrgb = 0x01,  // the file's colors are not sRGB's
  kImageFast = 0x02,     // writing: compress fast
  kImage16BitSrgb = 0x04,  // 16-bit samples are sRGB-encoded, not linear
};

// Samples per pixel, bytes per sample (a colormapped pixel is one byte).
inline unsigned format_channels(std::uint32_t f) { return ((f & kFormatColor) ? 3 : 1) + ((f & kFormatAlpha) ? 1 : 0); }
inline unsigned format_component_size(std::uint32_t f) { return (f & kFormatLinear) ? 2 : 1; }
inline unsigned pixel_channels(std::uint32_t f) { return (f & kFormatColormap) ? 1 : format_channels(f); }
inline unsigned pixel_component_size(std::uint32_t f) { return (f & kFormatColormap) ? 1 : format_component_size(f); }

namespace detail {

// sRGB <-> linear.  srgb_to_linear() gives 16-bit linear values;
// srgb_from_linear() takes linear values scaled to 0..255*65535.
inline const std::uint16_t* srgb_to_linear_table() {
  static const auto t = [] {
    std::array<std::uint16_t, 256> a{};
    for (int i = 0; i < 256; i++) {
      const double v = i / 255.0;
      const double l = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
      a[i] = static_cast<std::uint16_t>(std::lround(65535 * l));
    }
    return a;
  }();
  return t.data();
}
inline constexpr std::uint16_t kSrgbBase[512] = {
    128, 1782, 3383, 4644, 5675, 6564, 7357, 8074, 8732, 9346, 9921, 10463,
    10977, 11466, 11935, 12384, 12816, 13233, 13634, 14024, 14402, 14769, 15125, 15473,
    15812, 16142, 16466, 16781, 17090, 17393, 17690, 17981, 18266, 18546, 18822, 19093,
    19359, 19621, 19879, 20133, 20383, 20630, 20873, 21113, 21349, 21583, 21813, 22041,
    22265, 22487, 22707, 22923, 23138, 23350, 23559, 23767, 23972, 24175, 24376, 24575,
    24772, 24967, 25160, 25352, 25542, 25730, 25916, 26101, 26284, 26465, 26645, 26823,
    27000, 27176, 27350, 27523, 27695, 27865, 28034, 28201, 28368, 28533, 28697, 28860,
    29021, 29182, 29341, 29500, 29657, 29813, 29969, 30123, 30276, 30429, 30580, 30730,
    30880, 31028, 31176, 31323, 31469, 31614, 31758, 31902, 32045, 32186, 32327, 32468,
    32607, 32746, 32884, 33021, 33158, 33294, 33429, 33564, 33697, 33831, 33963, 34095,
    34226, 34357, 34486, 34616, 34744, 34873, 35000, 35127, 35253, 35379, 35504, 35629,
    35753, 35876, 35999, 36122, 36244, 36365, 36486, 36606, 36726, 36845, 36964, 37083,
    37201, 37318, 37435, 37551, 37668, 37783, 37898, 38013, 38127, 38241, 38354, 38467,
    38580, 38692, 38803, 38915, 39026, 39136, 39246, 39356, 39465, 39574, 39682, 39790,
    39898, 40005, 40112, 40219, 40325, 40431, 40537, 40642, 40747, 40851, 40955, 41059,
    41163, 41266, 41369, 41471, 41573, 41675, 41777, 41878, 41979, 42079, 42179, 42279,
    42379, 42478, 42577, 42676, 42775, 42873, 42971, 43068, 43165, 43262, 43359, 43456,
    43552, 43648, 43743, 43839, 43934, 44028, 44123, 44217, 44311, 44405, 44499, 44592,
    44685, 44778, 44870, 44962, 45054, 45146, 45238, 45329, 45420, 45511, 45601, 45692,
    45782, 45872, 45961, 46051, 46140, 46229, 46318, 46406, 46494, 46583, 46670, 46758,
    46846, 46933, 47020, 47107, 47193, 47280, 47366, 47452, 47538, 47623, 47709, 47794,
    47879, 47964, 48048, 48133, 48217, 48301, 48385, 48468, 48552, 48635, 48718, 48801,
    48884, 48966, 49048, 49131, 49213, 49294, 49376, 49458, 49539, 49620, 49701, 49782,
    49862, 49943, 50023, 50103, 50183, 50263, 50342, 50422, 50501, 50580, 50659, 50738,
    50816, 50895, 50973, 51051, 51129, 51207, 51285, 51362, 51439, 51517, 51594, 51671,
    51747, 51824, 51900, 51977, 52053, 52129, 52205, 52280, 52356, 52432, 52507, 52582,
    52657, 52732, 52807, 52881, 52956, 53030, 53104, 53178, 53252, 53326, 53400, 53473,
    53546, 53620, 53693, 53766, 53839, 53911, 53984, 54056, 54129, 54201, 54273, 54345,
    54417, 54489, 54560, 54632, 54703, 54774, 54845, 54916, 54987, 55058, 55129, 55199,
    55269, 55340, 55410, 55480, 55550, 55620, 55689, 55759, 55828, 55898, 55967, 56036,
    56105, 56174, 56243, 56311, 56380, 56448, 56517, 56585, 56653, 56721, 56789, 56857,
    56924, 56992, 57059, 57127, 57194, 57261, 57328, 57395, 57462, 57529, 57595, 57662,
    57728, 57795, 57861, 57927, 57993, 58059, 58125, 58191, 58256, 58322, 58387, 58453,
    58518, 58583, 58648, 58713, 58778, 58843, 58908, 58972, 59037, 59101, 59165, 59230,
    59294, 59358, 59422, 59486, 59549, 59613, 59677, 59740, 59804, 59867, 59930, 59993,
    60056, 60119, 60182, 60245, 60308, 60370, 60433, 60495, 60558, 60620, 60682, 60744,
    60806, 60868, 60930, 60992, 61054, 61115, 61177, 61238, 61300, 61361, 61422, 61483,
    61544, 61605, 61666, 61727, 61788, 61848, 61909, 61969, 62030, 62090, 62150, 62211,
    62271, 62331, 62391, 62450, 62510, 62570, 62630, 62689, 62749, 62808, 62867, 62927,
    62986, 63045, 63104, 63163, 63222, 63281, 63340, 63398, 63457, 63515, 63574, 63632,
    63691, 63749, 63807, 63865, 63923, 63981, 64039, 64097, 64155, 64212, 64270, 64328,
    64385, 64443, 64500, 64557, 64614, 64672, 64729, 64786, 64843, 64900, 64956, 65013,
    65070, 65126, 65183, 65239, 65296, 65352, 65409, 65465,
};
inline constexpr std::uint8_t kSrgbDelta[512] = {
    207, 201, 158, 129, 113, 100, 90, 82, 77, 72, 68, 64, 61, 59, 56, 54,
    52, 50, 49, 47, 46, 45, 43, 42, 41, 40, 39, 39, 38, 37, 36, 36,
    35, 34, 34, 33, 33, 32, 32, 31, 31, 30, 30, 30, 29, 29, 28, 28,
    28, 27, 27, 27, 27, 26, 26, 26, 25, 25, 25, 25, 24, 24, 24, 24,
    23, 23, 23, 23, 23, 22, 22, 22, 22, 22, 22, 21, 21, 21, 21, 21,
    21, 20, 20, 20, 20, 20, 20, 20, 20, 19, 19, 19, 19, 19, 19, 19,
    19, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 17, 17, 17, 17, 17,
    17, 17, 17, 17, 17, 17, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
};

inline std::uint32_t srgb_from_linear(std::uint32_t linear) {
  return static_cast<std::uint8_t>(
      (kSrgbBase[linear >> 15] + (((linear & 0x7fff) * kSrgbDelta[linear >> 15]) >> 12)) >> 8);
}
inline std::uint32_t div257(std::uint32_t v16) { return (v16 * 255 + 32895) >> 16; }
inline std::uint32_t div51(std::uint32_t v8) { return (v8 * 5 + 130) >> 8; }

inline bool gamma_not_srgb(std::int32_t g) {
  if (g < 1000 || g > 10000000) return false;
  return gamma_significant((g * 11 + 2) / 5);
}

}  // namespace detail

class FormatReader {
 public:
  std::uint32_t width = 0, height = 0;
  // The file's own format on construction; set it to the format wanted
  // before read().
  std::uint32_t format = 0;
  std::uint32_t flags = 0;
  // Colormap entries: the file's natural number; set to the room in the
  // colormap given to read() (256 is always enough), which sets it to the
  // number used.
  std::uint32_t colormap_entries = 0;

  // Reads the header and the chunks before the image data.  `file` must
  // stay valid until read().
  explicit FormatReader(std::span<const std::uint8_t> file, ReadOptions options = {}) : file_(file) {
    d_.options = options;
    d_.read_info(file);
    const Info& in = d_.info;
    const Header& h = in.header;
    width = h.width;
    height = h.height;
    if (h.color_type & 2) format |= kFormatColor;
    if ((h.color_type & 4) || in.has_trns()) format |= kFormatAlpha;
    if (h.bit_depth == 16) format |= kFormatLinear;
    if (h.color_type == kIndexed) format |= kFormatColormap;
    if ((format & kFormatColor) && not_srgb()) flags |= kImageNotSrgb;
    colormap_entries = h.color_type == kGray ? 1u << h.bit_depth
                       : h.color_type == kIndexed ? static_cast<std::uint32_t>(in.palette.size())
                                                  : 256;
    if (colormap_entries > 256) colormap_entries = 256;
  }

  const Info& info() const { return d_.info; }
  const std::vector<std::string>& warnings() const { return d_.warnings; }

  // Samples per row, and buffer bytes needed, for `format` (row_stride in
  // samples, 0 = width * channels).
  std::size_t row_stride() const { return static_cast<std::size_t>(width) * pixel_channels(format); }
  std::size_t buffer_size(std::ptrdiff_t stride = 0) const {
    const std::size_t s = stride ? static_cast<std::size_t>(stride < 0 ? -stride : stride) : row_stride();
    return s * pixel_component_size(format) * height;
  }
  std::size_t colormap_size() const { return format_channels(format) * format_component_size(format) * colormap_entries; }

  // Decodes the image into `buffer` in `format`: rows `row_stride` samples
  // apart (0: packed; negative: bottom-up), 16-bit samples in host byte
  // order.  Colormapped formats also fill `colormap`.  `background` (8-bit
  // sRGB) is what alpha is removed against; without it, 8-bit formats
  // composite on the buffer's contents (linear ones on black).
  void read(void* buffer, std::ptrdiff_t row_stride = 0, void* colormap = nullptr,
            const PaletteEntry* background = nullptr) {
    const std::size_t channels = pixel_channels(format);
    const std::size_t packed = static_cast<std::size_t>(width) * channels;
    if (row_stride == 0) row_stride = static_cast<std::ptrdiff_t>(packed);
    if (static_cast<std::size_t>(row_stride < 0 ? -row_stride : row_stride) < packed)
      VIO_FAIL("FormatReader::read: row stride too small");
    if ((format & kFormatColormap) && (!colormap || colormap_entries == 0))
      VIO_FAIL("FormatReader::read: no colormap");
    d_.decode(file_);
    buffer_ = static_cast<std::uint8_t*>(buffer);
    colormap_ = colormap;
    background_ = background;
    // first row and step in bytes
    step_ = row_stride * static_cast<std::ptrdiff_t>(pixel_component_size(format));
    first_ = buffer_ + (step_ < 0 ? (static_cast<std::ptrdiff_t>(height) - 1) * -step_ : 0);
    if (format & kFormatColormap) {
      read_colormap();
      read_colormapped();
    } else {
      read_direct();
    }
  }

 private:
  enum Encoding { kNotSet, kSrgb, kLinear, kFile, kLinear8 };
  enum Processing { kCmapNone, kCmapGa, kCmapTrans, kCmapRgb, kCmapRgbAlpha };

  static bool match_srgb(const Chromaticities& c) {
    auto out = [](std::int32_t v, std::int32_t ref) { return v < ref - 1000 || v > ref + 1000; };
    return !(out(c.white_x, 31270) || out(c.white_y, 32900) || out(c.red_x, 64000) || out(c.red_y, 33000) ||
             out(c.green_x, 30000) || out(c.green_y, 60000) || out(c.blue_x, 15000) || out(c.blue_y, 6000));
  }
  bool not_srgb() const {
    const Info& in = d_.info;
    Chromaticities c{};
    if (in.mastering) {
      const MasteringDisplay& m = *in.mastering;
      c = Chromaticities{m.white_x * 2, m.white_y * 2, m.red_x * 2,  m.red_y * 2,
                         m.green_x * 2, m.green_y * 2, m.blue_x * 2, m.blue_y * 2};
    } else if (in.chromaticities) {
      c = *in.chromaticities;
    }
    if (in.cicp || in.mastering) return !match_srgb(c);
    if (in.srgb_intent) return false;
    if (in.chromaticities) return !match_srgb(c);
    return false;
  }
  std::int32_t chunk_gamma() const {
    const Info& in = d_.info;
    return in.srgb_intent ? 45455 : in.gamma ? static_cast<std::int32_t>(*in.gamma) : 0;
  }
  std::int32_t resolve_file_gamma() const {
    const std::int32_t g = chunk_gamma();
    return g ? g : default_gamma_;
  }

  // Transforms the decoded image row by row; calls f(y, row).
  template <typename F>
  void transform_rows(const Transforms& t, F f) {
    init_plan(t);
    run_rows(f);
  }
  void init_plan(const Transforms& t) { plan_.init(d_.info, t, [this](const std::string& m) { d_.warn(m); }); }
  template <typename F>
  void run_rows(F f) {
    const Header& h = d_.info.header;
    const std::size_t rb = d_.row_bytes();
    std::vector<std::uint8_t> in(rb + 8), out(plan_.max_row_bytes(width));
    for (std::uint32_t y = 0; y < height; y++) {
      const std::uint8_t* src = d_.row(y);
      if (h.bit_depth == 16) {
        for (std::size_t i = 0; i + 1 < rb; i += 2) {
          std::uint16_t v;
          std::memcpy(&v, src + i, 2);
          in[i] = static_cast<std::uint8_t>(v >> 8), in[i + 1] = static_cast<std::uint8_t>(v);
        }
        src = in.data();
      }
      plan_.run(src, width, out.data());
      f(y, out.data());
    }
  }
  std::uint8_t* row_ptr(std::uint32_t y) const { return first_ + static_cast<std::ptrdiff_t>(y) * step_; }

  // ---- direct (not colormapped) ----------------------------------------------

  void read_direct() {
    const Header& h = d_.info.header;
    std::uint32_t fmt = format;
    const bool linear = (fmt & kFormatLinear) != 0;
    int local_compose = 0, local_background = 0;
    Transforms t;
    t.expand_palette = t.expand_gray = t.trns_to_alpha = true;
    std::uint32_t base = 0;
    if (h.color_type & 2) base |= kFormatColor;
    if ((h.color_type & 4) || d_.info.has_trns()) base |= kFormatAlpha;
    if (h.bit_depth == 16) base |= kFormatLinear;
    std::uint32_t change = fmt ^ base;
    if (change & kFormatColor) {
      if (fmt & kFormatColor) {
        t.gray_to_rgb = true;
      } else {
        if (base & kFormatAlpha) local_background = 1;
        t.rgb_to_gray = Transforms::RgbToGray{};
      }
      change &= ~kFormatColor;
    }
    const bool input_linear = (base & kFormatLinear) && !(flags & kImage16BitSrgb);
    default_gamma_ = input_linear ? 100000 : 45455;
    t.default_file_gamma = input_linear ? 1.0 : 0.45455;
    AlphaMode mode;
    std::int32_t output_gamma;  // as given: -1 is the sRGB marker
    if (linear) {
      mode = (base & kFormatAlpha) ? AlphaMode::kAssociated : AlphaMode::kPng;
      output_gamma = 100000;
    } else {
      mode = AlphaMode::kPng;
      output_gamma = -1;
    }
    if (change & kFormatAssociatedAlpha) {
      mode = AlphaMode::kOptimized;
      change &= ~kFormatAssociatedAlpha;
    }
    if (local_background) {
      std::int32_t g;
      if (detail::muldiv(&g, output_gamma, resolve_file_gamma(), 100000) && !detail::gamma_significant(g)) {
        local_background = 0;
      } else if (mode == AlphaMode::kAssociated) {
        local_background = 2;
        mode = AlphaMode::kPng;
      }
    }
    if (change & kFormatLinear) {
      if (linear) t.expand_16 = true;
      else t.scale_16 = true;
      change &= ~kFormatLinear;
    }
    if (change & kFormatAlpha) {
      if (base & kFormatAlpha) {
        if (local_background) {
          local_background = 2;
        } else if (linear) {
          t.strip_alpha = true;
        } else if (background_) {
          Transforms::Background b;
          b.color = Color16{0, background_->red, background_->green, background_->blue, background_->green};
          b.gamma = 0;
          t.background = b;
        } else {
          local_compose = 1;
          mode = AlphaMode::kOptimized;
        }
      } else {
        Transforms::Filler f;
        f.value = linear ? 65535 : 255;
        f.alpha = true;
        if (fmt & kFormatAlphaFirst) {
          f.before = true;
          change &= ~kFormatAlphaFirst;
        }
        t.filler = f;
      }
      change &= ~kFormatAlpha;
    }
    if (t.background && mode != AlphaMode::kPng) VIO_FAIL("conflicting calls to set alpha mode and background");
    t.alpha_mode = Transforms::Alpha{mode, output_gamma == -1 ? kGammaSrgb : output_gamma / 100000.0};
    if (change & kFormatBgr) {
      if (fmt & kFormatColor) t.bgr = true;
      else fmt &= ~kFormatBgr;
      change &= ~kFormatBgr;
    }
    if (change & kFormatAlphaFirst) {
      if (fmt & kFormatAlpha) {
        if (local_background != 2) t.swap_alpha = true;
      } else {
        fmt &= ~kFormatAlphaFirst;
      }
      change &= ~kFormatAlphaFirst;
    }
    t.byte_order = ByteOrder::kHost;
    if (change) VIO_FAIL("FormatReader: unsupported transformation");

    const std::uint32_t width_ = width;
    const unsigned out_size = pixel_component_size(fmt);
    if (local_compose) {
      const unsigned channels = (fmt & kFormatColor) ? 3 : 1;
      transform_rows(t, [&](std::uint32_t y, const std::uint8_t* in) {
        std::uint8_t* out = row_ptr(y);
        for (std::uint32_t x = 0; x < width_; x++, in += channels + 1, out += channels) {
          const unsigned alpha = in[channels];
          if (alpha == 0) continue;
          for (unsigned c = 0; c < channels; c++) {
            std::uint32_t component = in[c];
            if (alpha < 255) {
              if (plan_optimized()) {
                component *= 257 * 255;
                component += (255 - alpha) * detail::srgb_to_linear_table()[out[c]];
                if (component > 255 * 65535) component = 255 * 65535;
                component = detail::srgb_from_linear(component);
              } else {
                component += ((255 - alpha) * out[c] + 127) / 255;
                if (component > 255) component = 255;
              }
            }
            out[c] = static_cast<std::uint8_t>(component);
          }
        }
      });
      return;
    }
    if (local_background == 2) {
      init_plan(t);
      if (plan_.composing()) VIO_FAIL("unexpected compose");
      run_rows([&](std::uint32_t y, const std::uint8_t* inb) {
        if (plan_.output().bit_depth == 8) {
          std::uint8_t* out = row_ptr(y);
          const std::uint16_t* lin = detail::srgb_to_linear_table();
          for (std::uint32_t x = 0; x < width_; x++, inb += 2, out++) {
            const unsigned alpha = inb[1];
            if (alpha > 0) {
              std::uint32_t component = inb[0];
              if (alpha < 255) {
                const std::uint32_t bg = background_ ? lin[background_->green] : lin[out[0]];
                component = lin[component] * alpha + bg * (255 - alpha);
                component = detail::srgb_from_linear(component);
              }
              out[0] = static_cast<std::uint8_t>(component);
            } else if (background_) {
              out[0] = background_->green;
            }
          }
        } else {
          const bool preserve = (fmt & kFormatAlpha) != 0;
          const unsigned swap = preserve && (fmt & kFormatAlphaFirst) ? 1 : 0;
          std::uint16_t* out = reinterpret_cast<std::uint16_t*>(row_ptr(y));
          const unsigned outch = preserve ? 2 : 1;
          for (std::uint32_t x = 0; x < width_; x++, inb += 4, out += outch) {
            std::uint16_t v[2];
            std::memcpy(v, inb, 4);
            std::uint32_t component = v[0];
            const std::uint16_t alpha = v[1];
            if (alpha > 0) {
              if (alpha < 65535) component = (component * alpha + 32767) / 65535;
            } else {
              component = 0;
            }
            out[swap] = static_cast<std::uint16_t>(component);
            if (preserve) out[1 ^ swap] = alpha;
          }
        }
      });
      return;
    }
    const std::size_t bytes = static_cast<std::size_t>(width_) * format_channels(fmt) * out_size;
    transform_rows(t, [&](std::uint32_t y, const std::uint8_t* in) {
      const RowInfo& o = plan_.output();
      if (static_cast<std::size_t>(width_) * o.channels * o.bit_depth / 8 != bytes)
        VIO_FAIL("FormatReader: invalid transformations");
      std::memcpy(row_ptr(y), in, bytes);
    });
  }
  bool plan_optimized() const { return plan_.optimize_alpha(); }

  // ---- colormapped ------------------------------------------------------------

  unsigned decode_gamma(std::uint32_t value, int encoding) {
    if (encoding == kFile) encoding = file_encoding_;
    if (encoding == kNotSet) {
      set_file_encoding();
      encoding = file_encoding_;
    }
    switch (encoding) {
      case kFile: return detail::gamma_16bit_correct(value * 257, gamma_to_linear_);
      case kSrgb: return detail::srgb_to_linear_table()[value];
      case kLinear: return value;
      case kLinear8: return value * 257;
      default: VIO_FAIL("FormatReader: unexpected encoding");
    }
  }
  void set_file_encoding() {
    const std::int32_t g = resolve_file_gamma();
    if (detail::gamma_significant(g)) {
      if (detail::gamma_not_srgb(g)) {
        file_encoding_ = kFile;
        gamma_to_linear_ = detail::reciprocal(g);
      } else {
        file_encoding_ = kSrgb;
      }
    } else {
      file_encoding_ = kLinear8;
    }
  }
  std::uint32_t colormap_compose(std::uint32_t fg, int fg_encoding, std::uint32_t alpha, std::uint32_t bg,
                                 int encoding) {
    std::uint32_t f = decode_gamma(fg, fg_encoding);
    const std::uint32_t b = decode_gamma(bg, encoding);
    f = f * alpha + b * (255 - alpha);
    if (encoding == kLinear) {
      f *= 257;
      f += f >> 16;
      f = (f + 32768) >> 16;
    } else {
      f = detail::srgb_from_linear(f);
    }
    return f;
  }

  void cmap_entry(std::uint32_t ip, std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha,
                  int encoding) {
    const int output_encoding = (format & kFormatLinear) ? kLinear : kSrgb;
    const bool convert_to_y = !(format & kFormatColor) && (red != green || green != blue);
    if (ip > 255) VIO_FAIL("FormatReader: colormap index out of range");
    if (encoding == kFile) {
      if (file_encoding_ == kNotSet) set_file_encoding();
      encoding = file_encoding_;
    }
    const std::uint16_t* lin = detail::srgb_to_linear_table();
    if (encoding == kFile) {
      const std::int32_t g = gamma_to_linear_;
      red = detail::gamma_16bit_correct(red * 257, g);
      green = detail::gamma_16bit_correct(green * 257, g);
      blue = detail::gamma_16bit_correct(blue * 257, g);
      if (convert_to_y || output_encoding == kLinear) {
        alpha *= 257;
        encoding = kLinear;
      } else {
        red = detail::srgb_from_linear(red * 255);
        green = detail::srgb_from_linear(green * 255);
        blue = detail::srgb_from_linear(blue * 255);
        encoding = kSrgb;
      }
    } else if (encoding == kLinear8) {
      red *= 257, green *= 257, blue *= 257, alpha *= 257;
      encoding = kLinear;
    } else if (encoding == kSrgb && (convert_to_y || output_encoding == kLinear)) {
      red = lin[red], green = lin[green], blue = lin[blue];
      alpha *= 257;
      encoding = kLinear;
    }
    if (encoding == kLinear) {
      if (convert_to_y) {
        std::uint32_t y = 6968u * red + 23434u * green + 2366u * blue;
        if (output_encoding == kLinear) {
          y = (y + 16384) >> 15;
        } else {
          y = (y + 128) >> 8;
          y *= 255;
          y = detail::srgb_from_linear((y + 64) >> 7);
          alpha = detail::div257(alpha);
          encoding = kSrgb;
        }
        blue = red = green = y;
      } else if (output_encoding == kSrgb) {
        red = detail::srgb_from_linear(red * 255);
        green = detail::srgb_from_linear(green * 255);
        blue = detail::srgb_from_linear(blue * 255);
        alpha = detail::div257(alpha);
        encoding = kSrgb;
      }
    }
    if (encoding != output_encoding) VIO_FAIL("FormatReader: bad encoding");
    const unsigned afirst = (format & kFormatAlphaFirst) && (format & kFormatAlpha) ? 1 : 0;
    const unsigned bgr = (format & kFormatBgr) ? 2 : 0;
    const unsigned ch = format_channels(format);
    if (output_encoding == kLinear) {
      std::uint16_t* e = static_cast<std::uint16_t*>(colormap_) + ip * ch;
      switch (ch) {
        case 4: e[afirst ? 0 : 3] = static_cast<std::uint16_t>(alpha); [[fallthrough]];
        case 3:
          if (alpha < 65535) {
            if (alpha > 0) {
              blue = (blue * alpha + 32767U) / 65535U;
              green = (green * alpha + 32767U) / 65535U;
              red = (red * alpha + 32767U) / 65535U;
            } else {
              red = green = blue = 0;
            }
          }
          e[afirst + (2 ^ bgr)] = static_cast<std::uint16_t>(blue);
          e[afirst + 1] = static_cast<std::uint16_t>(green);
          e[afirst + bgr] = static_cast<std::uint16_t>(red);
          break;
        case 2: e[1 ^ afirst] = static_cast<std::uint16_t>(alpha); [[fallthrough]];
        case 1:
          if (alpha < 65535) green = alpha > 0 ? (green * alpha + 32767U) / 65535U : 0;
          e[afirst] = static_cast<std::uint16_t>(green);
          break;
      }
    } else {
      std::uint8_t* e = static_cast<std::uint8_t*>(colormap_) + ip * ch;
      switch (ch) {
        case 4: e[afirst ? 0 : 3] = static_cast<std::uint8_t>(alpha); [[fallthrough]];
        case 3:
          e[afirst + (2 ^ bgr)] = static_cast<std::uint8_t>(blue);
          e[afirst + 1] = static_cast<std::uint8_t>(green);
          e[afirst + bgr] = static_cast<std::uint8_t>(red);
          break;
        case 2: e[1 ^ afirst] = static_cast<std::uint8_t>(alpha); [[fallthrough]];
        case 1: e[afirst] = static_cast<std::uint8_t>(green); break;
      }
    }
  }

  unsigned make_gray_file_colormap() {
    for (unsigned i = 0; i < 256; i++) cmap_entry(i, i, i, i, 255, kFile);
    return 256;
  }
  unsigned make_gray_colormap() {
    for (unsigned i = 0; i < 256; i++) cmap_entry(i, i, i, i, 255, kSrgb);
    return 256;
  }
  unsigned make_ga_colormap() {
    unsigned i = 0;
    while (i < 231) {
      const unsigned gray = (i * 256 + 115) / 231;
      cmap_entry(i++, gray, gray, gray, 255, kSrgb);
    }
    cmap_entry(i++, 255, 255, 255, 0, kSrgb);
    for (unsigned a = 1; a < 5; ++a)
      for (unsigned g = 0; g < 6; ++g) cmap_entry(i++, g * 51, g * 51, g * 51, a * 51, kSrgb);
    return i;
  }
  unsigned make_rgb_colormap() {
    unsigned i = 0;
    for (unsigned r = 0; r < 6; ++r)
      for (unsigned g = 0; g < 6; ++g)
        for (unsigned b = 0; b < 6; ++b) cmap_entry(i++, r * 51, g * 51, b * 51, 255, kSrgb);
    return i;
  }
  static std::uint8_t rgb_index(unsigned r, unsigned g, unsigned b) {
    return static_cast<std::uint8_t>(6 * (6 * detail::div51(r) + detail::div51(g)) + detail::div51(b));
  }

  void read_colormap() {
    const Header& h = d_.info.header;
    const Info& in = d_.info;
    const std::uint32_t out_fmt = format;
    const int output_encoding = (out_fmt & kFormatLinear) ? kLinear : kSrgb;
    const unsigned num_trans = h.color_type == kIndexed ? static_cast<unsigned>(in.trans_alpha.size())
                                                        : (in.trans_color ? 1 : 0);
    unsigned cmap_entries = 0, background_index = 256;
    int processing = kCmapNone, data_encoding = kNotSet;
    std::uint32_t back_r, back_g, back_b;
    bool expand_trns = false;
    Transforms& t = cmap_t_;
    t = Transforms();
    auto set_background = [&](std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t gray) {
      Transforms::Background bg;
      bg.color = Color16{0, static_cast<std::uint16_t>(r), static_cast<std::uint16_t>(g),
                         static_cast<std::uint16_t>(b), static_cast<std::uint16_t>(gray)};
      bg.gamma = 0;
      t.background = bg;
    };
    if (((h.color_type & 4) || num_trans > 0) && !(out_fmt & kFormatAlpha)) {
      if (output_encoding == kLinear) {
        back_b = back_g = back_r = 0;
      } else if (!background_) {
        VIO_FAIL("background color must be supplied to remove alpha/transparency");
      } else {
        back_g = background_->green;
        if (out_fmt & kFormatColor) back_r = background_->red, back_b = background_->blue;
        else back_b = back_r = back_g;
      }
    } else if (output_encoding == kLinear) {
      back_b = back_r = back_g = 65535;
    } else {
      back_b = back_r = back_g = 255;
    }
    default_gamma_ = h.bit_depth == 16 && !(flags & kImage16BitSrgb) ? 100000 : 45455;
    t.default_file_gamma = default_gamma_ == 100000 ? 1.0 : 0.45455;
    const std::uint16_t* lin = detail::srgb_to_linear_table();
    switch (h.color_type) {
      case kGray:
        if (h.bit_depth <= 8) {
          unsigned trans = 256, back_alpha = 0;
          cmap_entries = 1u << h.bit_depth;
          if (cmap_entries > colormap_entries) VIO_FAIL("gray[8] color-map: too few entries");
          const unsigned step = 255 / (cmap_entries - 1);
          processing = kCmapNone;
          if (num_trans > 0) {
            trans = in.trans_color->gray;
            if (!(out_fmt & kFormatAlpha)) back_alpha = output_encoding == kLinear ? 65535 : 255;
          }
          for (unsigned i = 0, val = 0; i < cmap_entries; ++i, val += step) {
            if (i != trans) cmap_entry(i, val, val, val, 255, kFile);
            else cmap_entry(i, back_r, back_g, back_b, back_alpha, output_encoding);
          }
          data_encoding = kFile;
          if (h.bit_depth < 8) t.packing = true;
        } else {
          data_encoding = kSrgb;
          if (256 > colormap_entries) VIO_FAIL("gray[16] color-map: too few entries");
          cmap_entries = make_gray_colormap();
          if (num_trans > 0) {
            unsigned back_alpha;
            if (out_fmt & kFormatAlpha) {
              back_alpha = 0;
            } else {
              if (back_r == back_g && back_g == back_b) {
                std::uint32_t gray = back_g;
                if (output_encoding == kLinear) {
                  gray = detail::srgb_from_linear(gray * 255);
                  cmap_entry(gray, back_g, back_g, back_g, 65535, kLinear);
                }
                set_background(gray, gray, gray, gray);
                processing = kCmapNone;
                break;
              }
              back_alpha = output_encoding == kLinear ? 65535 : 255;
            }
            expand_trns = true;
            processing = kCmapTrans;
            background_index = 254;
            cmap_entry(254, back_r, back_g, back_b, back_alpha, output_encoding);
          } else {
            processing = kCmapNone;
          }
        }
        break;
      case kGrayAlpha:
        data_encoding = kSrgb;
        if (out_fmt & kFormatAlpha) {
          if (256 > colormap_entries) VIO_FAIL("gray+alpha color-map: too few entries");
          cmap_entries = make_ga_colormap();
          background_index = 231;
          processing = kCmapGa;
        } else if (!(out_fmt & kFormatColor) || (back_r == back_g && back_g == back_b)) {
          std::uint32_t gray = back_g;
          if (256 > colormap_entries) VIO_FAIL("gray-alpha color-map: too few entries");
          cmap_entries = make_gray_colormap();
          if (output_encoding == kLinear) {
            gray = detail::srgb_from_linear(gray * 255);
            cmap_entry(gray, back_g, back_g, back_g, 65535, kLinear);
          }
          set_background(gray, gray, gray, gray);
          processing = kCmapNone;
        } else {
          if (256 > colormap_entries) VIO_FAIL("ga-alpha color-map: too few entries");
          std::uint32_t i = 0;
          while (i < 231) {
            const std::uint32_t gray = (i * 256 + 115) / 231;
            cmap_entry(i++, gray, gray, gray, 255, kSrgb);
          }
          background_index = i;
          cmap_entry(i++, back_r, back_g, back_b, output_encoding == kLinear ? 65535U : 255U, output_encoding);
          if (output_encoding == kSrgb) back_r = lin[back_r], back_g = lin[back_g], back_b = lin[back_b];
          for (std::uint32_t a = 1; a < 5; ++a) {
            const std::uint32_t alpha = 51 * a;
            const std::uint32_t rx = (255 - alpha) * back_r, gx = (255 - alpha) * back_g, bx = (255 - alpha) * back_b;
            for (unsigned g = 0; g < 6; ++g) {
              const std::uint32_t gray = lin[g * 51] * alpha;
              cmap_entry(i++, detail::srgb_from_linear(gray + rx), detail::srgb_from_linear(gray + gx),
                         detail::srgb_from_linear(gray + bx), 255, kSrgb);
            }
          }
          cmap_entries = i;
          processing = kCmapGa;
        }
        break;
      case kTruecolor:
      case kTruecolorAlpha:
        if (!(out_fmt & kFormatColor)) {
          t.rgb_to_gray = Transforms::RgbToGray{};
          data_encoding = kSrgb;
          const bool alpha_in = h.color_type == kTruecolorAlpha || num_trans > 0;
          if (alpha_in && (out_fmt & kFormatAlpha)) {
            expand_trns = true;
            if (256 > colormap_entries) VIO_FAIL("rgb[ga] color-map: too few entries");
            cmap_entries = make_ga_colormap();
            background_index = 231;
            processing = kCmapGa;
          } else {
            const std::int32_t gamma = resolve_file_gamma();
            if (256 > colormap_entries) VIO_FAIL("rgb[gray] color-map: too few entries");
            if (alpha_in && detail::gamma_not_srgb(gamma)) {
              cmap_entries = make_gray_file_colormap();
              data_encoding = kFile;
            } else {
              cmap_entries = make_gray_colormap();
            }
            if (alpha_in) {
              std::uint32_t gray = back_g;
              if (data_encoding == kFile) {
                if (output_encoding == kSrgb) gray = lin[gray];
                gray = detail::div257(detail::gamma_16bit_correct(gray, gamma));
                cmap_entry(gray, back_g, back_g, back_g, 0, output_encoding);
              } else if (output_encoding == kLinear) {
                gray = detail::srgb_from_linear(gray * 255);
                cmap_entry(gray, back_g, back_g, back_g, 0, kLinear);
              }
              expand_trns = true;
              set_background(gray, gray, gray, gray);
            }
            processing = kCmapNone;
          }
        } else {
          data_encoding = kSrgb;
          if (h.color_type == kTruecolorAlpha || num_trans > 0) {
            if (out_fmt & kFormatAlpha) {
              if (216 + 1 + 27 > colormap_entries) VIO_FAIL("rgb+alpha color-map: too few entries");
              cmap_entries = make_rgb_colormap();
              cmap_entry(cmap_entries, 255, 255, 255, 0, kSrgb);
              background_index = cmap_entries++;
              for (std::uint32_t r = 0; r < 256; r = (r << 1) | 0x7f)
                for (std::uint32_t g = 0; g < 256; g = (g << 1) | 0x7f)
                  for (std::uint32_t b = 0; b < 256; b = (b << 1) | 0x7f) cmap_entry(cmap_entries++, r, g, b, 128, kSrgb);
              expand_trns = true;
              processing = kCmapRgbAlpha;
            } else {
              const unsigned sample_size = format_channels(out_fmt) * format_component_size(out_fmt);
              if (216 + 1 + 27 > colormap_entries) VIO_FAIL("rgb-alpha color-map: too few entries");
              cmap_entries = make_rgb_colormap();
              cmap_entry(cmap_entries, back_r, back_g, back_b, 0, output_encoding);
              std::uint32_t r, g, b;
              if (output_encoding == kLinear) {
                r = detail::srgb_from_linear(back_r * 255);
                g = detail::srgb_from_linear(back_g * 255);
                b = detail::srgb_from_linear(back_b * 255);
              } else {
                r = back_r, g = back_g, b = back_b;
              }
              const std::uint8_t* cm = static_cast<const std::uint8_t*>(colormap_);
              if (std::memcmp(cm + sample_size * cmap_entries, cm + sample_size * rgb_index(r, g, b), sample_size) != 0) {
                background_index = cmap_entries++;
                for (r = 0; r < 256; r = (r << 1) | 0x7f)
                  for (g = 0; g < 256; g = (g << 1) | 0x7f)
                    for (b = 0; b < 256; b = (b << 1) | 0x7f)
                      cmap_entry(cmap_entries++, colormap_compose(r, kSrgb, 128, back_r, output_encoding),
                                 colormap_compose(g, kSrgb, 128, back_g, output_encoding),
                                 colormap_compose(b, kSrgb, 128, back_b, output_encoding), 0, output_encoding);
                expand_trns = true;
                processing = kCmapRgbAlpha;
              } else {
                set_background(back_r, back_g, back_b, back_g);
                processing = kCmapRgb;
              }
            }
          } else {
            if (216 > colormap_entries) VIO_FAIL("rgb color-map: too few entries");
            cmap_entries = make_rgb_colormap();
            processing = kCmapRgb;
          }
        }
        break;
      case kIndexed: {
        const std::vector<std::uint8_t>& trans = in.trans_alpha;
        const unsigned nt = static_cast<unsigned>(trans.size());
        const bool do_background = nt > 0 && !(out_fmt & kFormatAlpha);
        processing = kCmapNone;
        data_encoding = kFile;
        cmap_entries = static_cast<unsigned>(in.palette.size());
        if (cmap_entries > 256) cmap_entries = 256;
        if (cmap_entries > colormap_entries) VIO_FAIL("palette color-map: too few entries");
        for (unsigned i = 0; i < cmap_entries; ++i) {
          const PaletteEntry& p = in.palette[i];
          if (do_background && i < nt && trans[i] < 255) {
            if (trans[i] == 0) {
              cmap_entry(i, back_r, back_g, back_b, 0, output_encoding);
            } else {
              cmap_entry(i, colormap_compose(p.red, kFile, trans[i], back_r, output_encoding),
                         colormap_compose(p.green, kFile, trans[i], back_g, output_encoding),
                         colormap_compose(p.blue, kFile, trans[i], back_b, output_encoding),
                         output_encoding == kLinear ? trans[i] * 257U : trans[i], output_encoding);
            }
          } else {
            cmap_entry(i, p.red, p.green, p.blue, i < nt ? trans[i] : 255U, kFile);
          }
        }
        if (h.bit_depth < 8) t.packing = true;
        break;
      }
    }
    if (expand_trns && num_trans > 0 && !(h.color_type & 4)) t.trns_to_alpha = true;
    if (data_encoding == kSrgb) t.alpha_mode = Transforms::Alpha{AlphaMode::kPng, kGammaSrgb};
    if (h.bit_depth > 8) t.scale_16 = true;
    if (cmap_entries > 256 || cmap_entries > colormap_entries) VIO_FAIL("color map overflow");
    colormap_entries = cmap_entries;
    const unsigned expect[] = {256, 231, 254, 256, 216};
    if (background_index != expect[processing]) VIO_FAIL("bad background index");
    processing_ = processing;
  }

  void read_colormapped() {
    const std::uint32_t w = width;
    const int proc = processing_;
    transform_rows(cmap_t_, [&](std::uint32_t y, const std::uint8_t* in) {
      std::uint8_t* out = row_ptr(y);
      const RowInfo& o = plan_.output();
      switch (proc) {
        case kCmapNone:
          if (o.bit_depth != 8 || o.channels != 1) VIO_FAIL("bad color-map processing");
          std::memcpy(out, in, w);
          break;
        case kCmapGa:
          for (std::uint32_t x = 0; x < w; x++, in += 2) {
            const unsigned gray = in[0], alpha = in[1];
            unsigned entry;
            if (alpha > 229) entry = (231 * gray + 128) >> 8;
            else if (alpha < 26) entry = 231;
            else entry = 226 + 6 * detail::div51(alpha) + detail::div51(gray);
            out[x] = static_cast<std::uint8_t>(entry);
          }
          break;
        case kCmapTrans:
          for (std::uint32_t x = 0; x < w; x++, in += 2) {
            const std::uint8_t gray = in[0], alpha = in[1];
            out[x] = alpha == 0 ? 254 : gray != 254 ? gray : 255;
          }
          break;
        case kCmapRgb:
          for (std::uint32_t x = 0; x < w; x++, in += 3) out[x] = rgb_index(in[0], in[1], in[2]);
          break;
        case kCmapRgbAlpha:
          for (std::uint32_t x = 0; x < w; x++, in += 4) {
            const unsigned alpha = in[3];
            if (alpha >= 196) {
              out[x] = rgb_index(in[0], in[1], in[2]);
            } else if (alpha < 64) {
              out[x] = 216;
            } else {
              unsigned back_i = 217;
              if (in[0] & 0x80) back_i += 9;
              if (in[0] & 0x40) back_i += 9;
              if (in[1] & 0x80) back_i += 3;
              if (in[1] & 0x40) back_i += 3;
              if (in[2] & 0x80) back_i += 1;
              if (in[2] & 0x40) back_i += 1;
              out[x] = static_cast<std::uint8_t>(back_i);
            }
          }
          break;
      }
    });
  }

  std::span<const std::uint8_t> file_;
  Decoder d_;
  detail::TransformPlan plan_;
  Transforms cmap_t_;
  std::uint8_t* buffer_ = nullptr;
  std::uint8_t* first_ = nullptr;
  std::ptrdiff_t step_ = 0;
  void* colormap_ = nullptr;
  const PaletteEntry* background_ = nullptr;
  std::int32_t default_gamma_ = 0, gamma_to_linear_ = 0;
  int file_encoding_ = kNotSet, processing_ = kCmapNone;
};


// Writes images given in the formats FormatReader produces: 8-bit sRGB
// samples, 16-bit linear ones (alpha premultiplied; written as 16-bit
// linear, or converted to 8-bit sRGB), or colormapped pixels.
class FormatWriter {
 public:
  std::uint32_t width = 0, height = 0, format = 0, flags = 0;
  std::uint32_t colormap_entries = 0;  // colormapped formats
  Encoder::Mode mode = Encoder::Mode::kFast;

  // `row_stride` in samples (0: packed; negative: bottom-up); 16-bit
  // samples in host byte order.
  std::vector<std::uint8_t> write(const void* buffer, std::ptrdiff_t row_stride = 0, const void* colormap = nullptr,
                                  bool convert_to_8bit = false) {
    const bool cmap = (format & kFormatColormap) != 0;
    const bool linear = !cmap && (format & kFormatLinear);
    const bool alpha = !cmap && (format & kFormatAlpha);
    const bool write_16bit = linear && !convert_to_8bit;
    const std::size_t channels = pixel_channels(format);
    const std::size_t packed = static_cast<std::size_t>(width) * channels;
    if (row_stride == 0) row_stride = static_cast<std::ptrdiff_t>(packed);
    if (static_cast<std::size_t>(row_stride < 0 ? -row_stride : row_stride) < packed)
      VIO_FAIL("supplied row stride too small");
    Encoder e;
    e.mode = mode;
    Header& h = e.info.header;
    h.width = width;
    h.height = height;
    if (cmap) {
      if (!colormap || colormap_entries == 0) VIO_FAIL("no color-map for color-mapped image");
      const std::uint32_t n = colormap_entries;
      h.bit_depth = n > 16 ? 8 : n > 4 ? 4 : n > 2 ? 2 : 1;
      h.color_type = kIndexed;
      set_palette(e.info, colormap);
    } else {
      h.bit_depth = write_16bit ? 16 : 8;
      h.color_type = static_cast<std::uint8_t>(((format & kFormatColor) ? 2 : 0) + ((format & kFormatAlpha) ? 4 : 0));
    }
    if (write_16bit) {
      e.info.gamma = 100000;
      if (!(flags & kImageNotSrgb)) e.info.chromaticities = Chromaticities{31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000};
    } else if (!(flags & kImageNotSrgb)) {
      e.info.srgb_intent = 0;
    } else {
      e.info.gamma = 45455;
    }
    WriteTransforms& t = e.transforms;
    t.byte_order = write_16bit ? ByteOrder::kHost : ByteOrder::kBig;
    std::uint32_t fmt = format;
    if (fmt & kFormatBgr) {
      if (!cmap && (fmt & kFormatColor)) t.bgr = true;
      fmt &= ~kFormatBgr;
    }
    if (fmt & kFormatAlphaFirst) {
      if (!cmap && (fmt & kFormatAlpha)) t.swap_alpha = true;
      fmt &= ~kFormatAlphaFirst;
    }
    if (cmap && colormap_entries <= 16) t.packing = true;
    if (fmt & ~(kFormatColor | kFormatLinear | kFormatAlpha | kFormatColormap))
      VIO_FAIL("FormatWriter: unsupported transformation");
    if (flags & kImageFast) e.mode = Encoder::Mode::kFast;
    const std::uint8_t* first = static_cast<const std::uint8_t*>(buffer);
    std::ptrdiff_t step = row_stride * (linear ? 2 : 1);
    if (step < 0) first += (static_cast<std::ptrdiff_t>(height) - 1) * -step;
    e.begin();
    if (linear && (alpha || convert_to_8bit)) {
      const unsigned nc = (format & kFormatColor) ? 3 : 1;
      std::vector<std::uint8_t> row((nc + 1) * static_cast<std::size_t>(width) * 2 + 8);
      for (std::uint32_t y = 0; y < height; y++) {
        const std::uint16_t* in = reinterpret_cast<const std::uint16_t*>(first + static_cast<std::ptrdiff_t>(y) * step);
        if (write_16bit) unpremultiply16(in, reinterpret_cast<std::uint16_t*>(row.data()), nc);
        else to8(in, row.data(), nc, alpha);
        e.write_row(row.data());
      }
    } else {
      for (std::uint32_t y = 0; y < height; y++) e.write_row(first + static_cast<std::ptrdiff_t>(y) * step);
    }
    return e.finish();
  }

 private:
  static std::uint8_t unpremultiply(std::uint32_t component, std::uint32_t alpha, std::uint32_t reciprocal) {
    if (component >= alpha || alpha < 128) return 255;
    if (component == 0) return 0;
    if (alpha < 65407) {
      component *= reciprocal;
      component += 64;
      component >>= 7;
    } else {
      component *= 255;
    }
    return static_cast<std::uint8_t>(detail::srgb_from_linear(component));
  }
  static std::uint32_t unp_reciprocal(std::uint32_t alpha) { return (((0xffffu * 0xff) << 7) + (alpha >> 1)) / alpha; }

  // Alpha position in a pixel of the given layout.
  int alpha_index(unsigned nc) const { return (format & kFormatAlphaFirst) ? 0 : static_cast<int>(nc); }
  int color_start() const { return (format & kFormatAlphaFirst) ? 1 : 0; }

  void unpremultiply16(const std::uint16_t* in, std::uint16_t* out, unsigned nc) const {
    const int ai = alpha_index(nc), cs = color_start();
    for (std::uint32_t x = 0; x < width; x++, in += nc + 1, out += nc + 1) {
      const std::uint16_t alpha = in[ai];
      std::uint32_t reciprocal = 0;
      out[ai] = alpha;
      if (alpha > 0 && alpha < 65535) reciprocal = ((0xffffu << 15) + (alpha >> 1)) / alpha;
      for (unsigned c = 0; c < nc; c++) {
        std::uint16_t component = in[cs + c];
        if (component >= alpha) {
          component = 65535;
        } else if (component > 0 && alpha < 65535) {
          std::uint32_t calc = component * reciprocal;
          calc += 16384;
          component = static_cast<std::uint16_t>(calc >> 15);
        }
        out[cs + c] = component;
      }
    }
  }

  void to8(const std::uint16_t* in, std::uint8_t* out, unsigned nc, bool alpha) const {
    if (alpha) {
      const int ai = alpha_index(nc), cs = color_start();
      for (std::uint32_t x = 0; x < width; x++, in += nc + 1, out += nc + 1) {
        const std::uint16_t a = in[ai];
        const std::uint8_t ab = static_cast<std::uint8_t>(detail::div257(a));
        std::uint32_t reciprocal = 0;
        out[ai] = ab;
        if (ab > 0 && ab < 255) reciprocal = unp_reciprocal(a);
        for (unsigned c = 0; c < nc; c++) out[cs + c] = unpremultiply(in[cs + c], a, reciprocal);
      }
    } else {
      for (std::size_t i = 0, n = static_cast<std::size_t>(width) * nc; i < n; i++)
        out[i] = static_cast<std::uint8_t>(detail::srgb_from_linear(in[i] * 255u));
    }
  }

  void set_palette(Info& info, const void* cmap) const {
    const int entries = colormap_entries > 256 ? 256 : static_cast<int>(colormap_entries);
    const unsigned channels = format_channels(format);
    const unsigned afirst = (format & kFormatAlphaFirst) && (format & kFormatAlpha) ? 1 : 0;
    const unsigned bgr = (format & kFormatBgr) ? 2 : 0;
    std::vector<PaletteEntry> palette(entries, PaletteEntry{0, 0, 0});
    std::uint8_t trns[256];
    std::memset(trns, 255, sizeof trns);
    int num_trans = 0;
    for (int i = 0; i < entries; ++i) {
      PaletteEntry& p = palette[i];
      if (format & kFormatLinear) {
        const std::uint16_t* e = static_cast<const std::uint16_t*>(cmap) + static_cast<unsigned>(i) * channels;
        if (channels & 1) {
          if (channels >= 3) {
            p.blue = static_cast<std::uint8_t>(detail::srgb_from_linear(255u * e[2 ^ bgr]));
            p.green = static_cast<std::uint8_t>(detail::srgb_from_linear(255u * e[1]));
            p.red = static_cast<std::uint8_t>(detail::srgb_from_linear(255u * e[bgr]));
          } else {
            p.blue = p.red = p.green = static_cast<std::uint8_t>(detail::srgb_from_linear(255u * e[0]));
          }
        } else {
          const std::uint16_t alpha = e[afirst ? 0 : channels - 1];
          const std::uint8_t ab = static_cast<std::uint8_t>(detail::div257(alpha));
          std::uint32_t reciprocal = 0;
          if (ab > 0 && ab < 255) reciprocal = unp_reciprocal(alpha);
          trns[i] = ab;
          if (ab < 255) num_trans = i + 1;
          if (channels >= 3) {
            p.blue = unpremultiply(e[afirst + (2 ^ bgr)], alpha, reciprocal);
            p.green = unpremultiply(e[afirst + 1], alpha, reciprocal);
            p.red = unpremultiply(e[afirst + bgr], alpha, reciprocal);
          } else {
            p.blue = p.red = p.green = unpremultiply(e[afirst], alpha, reciprocal);
          }
        }
      } else {
        const std::uint8_t* e = static_cast<const std::uint8_t*>(cmap) + static_cast<unsigned>(i) * channels;
        switch (channels) {
          case 4:
            trns[i] = e[afirst ? 0 : 3];
            if (trns[i] < 255) num_trans = i + 1;
            [[fallthrough]];
          case 3:
            p.blue = e[afirst + (2 ^ bgr)];
            p.green = e[afirst + 1];
            p.red = e[afirst + bgr];
            break;
          case 2:
            trns[i] = e[1 ^ afirst];
            if (trns[i] < 255) num_trans = i + 1;
            [[fallthrough]];
          case 1: p.blue = p.red = p.green = e[afirst]; break;
        }
      }
    }
    info.palette = palette;
    if (num_trans > 0) info.trans_alpha.assign(trns, trns + num_trans);
  }
};

}  // namespace vio::png
