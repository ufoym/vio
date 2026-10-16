// Test cases shared by png-test (tools/png_test.cpp, checks vio) and
// png-golden (tools/png_golden.cpp, records what libpng produces for the
// same cases in libpng-expected.txt):
//
//   - read transformation cases (libpng's png_set_* read transforms and
//     vio's png::Transforms for the same request),
//   - generated image sets (pngvalid's images: every color type and bit
//     depth, sizes, interlacing, gAMA/sBIT/tRNS/bKGD variants),
//   - pixel formats of the format-conversion API (pngstest),
//   - unknown-chunk rules (pngunknown).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "vio/png.hpp"

namespace png_cases {

// ---------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------

struct Fnv {
  std::uint64_t h = 0xcbf29ce484222325ull;
  void add(const void* p, std::size_t n) {
    const std::uint8_t* b = static_cast<const std::uint8_t*>(p);
    for (std::size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001b3ull;
  }
  // (little-endian bytes, as the reference values were computed)
  void add(std::uint64_t v) {
    std::uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = static_cast<std::uint8_t>(v >> (8 * i));
    add(b, sizeof b);
  }
  void add(const std::string& s) { add(s.data(), s.size()); }
  std::string hex() const {
    char s[17];
    std::snprintf(s, sizeof s, "%016llx", static_cast<unsigned long long>(h));
    return s;
  }
};

// The result of reading an image with a transformation case: its format,
// palette and rows, or an error.
struct Result {
  bool error = false;
  std::size_t row_bytes = 0;
  int channels = 0, bit_depth = 0, color_type = 0;
  std::vector<std::uint8_t> palette;  // RGB triples (palette output only)
  std::vector<std::uint8_t> pixels;   // rows of row_bytes
  std::uint64_t hash() const {
    Fnv f;
    if (error) {
      f.add(std::string("error"));
      return f.h;
    }
    f.add(static_cast<std::uint64_t>(row_bytes));
    f.add(static_cast<std::uint64_t>(channels * 65536 + bit_depth * 256 + color_type));
    f.add(palette.data(), palette.size());
    f.add(pixels.data(), pixels.size());
    return f.h;
  }
};

// ---------------------------------------------------------------------------
// Read transformation cases
// ---------------------------------------------------------------------------

enum : std::uint32_t {
  kExpand = 1u << 0, kPaletteToRgb = 1u << 1, kGray124To8 = 1u << 2, kTrnsToAlpha = 1u << 3,
  kExpand16 = 1u << 4, kScale16 = 1u << 5, kStrip16 = 1u << 6, kStripAlpha = 1u << 7,
  kGrayToRgb = 1u << 8, kRgbToGray = 1u << 9, kRgbToGrayCoef = 1u << 10, kQuantize = 1u << 11,
  kQuantizeHist = 1u << 12, kShift = 1u << 13, kPacking = 1u << 14, kPackswap = 1u << 15,
  kBgr = 1u << 16, kSwapAlpha = 1u << 17, kInvertAlpha = 1u << 18, kInvertMono = 1u << 19,
  kSwap = 1u << 20,
};

struct Xform {
  std::uint32_t bits = 0;
  int gamma = 0;  // 1..16: screen kScreens[(g-1)/4], file kFiles[(g-1)%4]
  int alpha = 0;  // 1..4: alpha mode (PNG, associated, optimized, broken)
  int alpha_linear = 0;  // the alpha mode's output gamma is 1.0 (else sRGB)
  int background = 0;  // 1: fixed color, screen gamma; 2: unique gamma; 3: bKGD, file gamma
  int filler = 0;      // 1: after, 2: before, 3: alpha after, 4: alpha before
};

inline constexpr double kScreens[4] = {2.2, 1.0, 1.8, 0.45};
inline constexpr double kFiles[4] = {0.45455, 1.0, 0.6, 0.0};  // 0: sRGB
inline constexpr std::uint16_t kBackground[5] = {1, 0x30, 0x90, 0xc0, 0x50};  // index, r, g, b, gray

// A quantization palette and histogram.
inline std::vector<vio::png::PaletteEntry> quantize_palette() {
  std::vector<vio::png::PaletteEntry> p;
  for (int k = 0; k < 24; k++)
    p.push_back({static_cast<std::uint8_t>(k * 37 % 256), static_cast<std::uint8_t>(k * 91 % 256),
                 static_cast<std::uint8_t>(k * 53 % 256)});
  return p;
}
inline std::vector<std::uint16_t> quantize_histogram() {
  std::vector<std::uint16_t> h;
  for (int k = 0; k < 24; k++) h.push_back(static_cast<std::uint16_t>(k * 7919 % 1000));
  return h;
}

// The cases: each transformation alone, some useful combinations, then
// random combinations.
inline std::vector<Xform> xforms(int random_count = 64) {
  std::vector<Xform> v;
  for (std::uint32_t b = 0; b <= 20; b++) {
    if ((1u << b) == kRgbToGrayCoef || (1u << b) == kQuantizeHist) continue;
    v.push_back(Xform{1u << b});
  }
  v.push_back(Xform{kRgbToGray | kRgbToGrayCoef});
  v.push_back(Xform{kQuantize | kQuantizeHist});
  for (int g = 1; g <= 16; g++) v.push_back(Xform{0, g});
  for (int a = 1; a <= 4; a++)
    for (int l = 0; l < 2; l++) v.push_back(Xform{0, 0, a, l});
  for (int b = 1; b <= 3; b++) v.push_back(Xform{kStrip16, 0, 0, 0, b});
  for (int f = 1; f <= 4; f++) v.push_back(Xform{0, 0, 0, 0, 0, f});
  v.push_back(Xform{kExpand | kGrayToRgb, 0, 0, 0, 0, 1});
  v.push_back(Xform{kExpand | kRgbToGray, 1});
  v.push_back(Xform{kExpand | kStrip16, 1, 0, 0, 1});
  v.push_back(Xform{kExpand16 | kRgbToGray, 5, 2});
  std::mt19937 rng(20261015);
  for (int i = 0; i < random_count; i++) {
    auto pick = [&](int pct) { return static_cast<int>(rng() % 100) < pct; };
    Xform x;
    const int pct[21] = {20, 10, 10, 15, 10, 15, 10, 10, 20, 15, 50, 5, 50, 15, 15, 10, 10, 10, 10, 10, 30};
    for (int b = 0; b <= 20; b++)
      if (pick(pct[b])) x.bits |= 1u << b;
    x.gamma = pick(40) ? static_cast<int>(rng() % 16) + 1 : 0;
    x.alpha = pick(20) ? static_cast<int>(rng() % 4) + 1 : 0;
    x.alpha_linear = static_cast<int>(rng() % 2);
    x.background = pick(25) ? static_cast<int>(rng() % 3) + 1 : 0;
    x.filler = pick(15) ? static_cast<int>(rng() % 4) + 1 : 0;
    v.push_back(x);
  }
  return v;
}

// The same request for vio.
inline void to_vio(const Xform& x, const vio::png::Info& info, vio::png::Transforms& t) {
  namespace png = vio::png;
  const std::uint32_t b = x.bits;
  t.byte_order = (b & kSwap) && info.header.bit_depth == 16 ? png::ByteOrder::kLittle : png::ByteOrder::kBig;
  if (b & (kExpand | kPaletteToRgb)) t.expand_palette = true;
  if (b & kExpand) t.expand_gray = t.trns_to_alpha = true;
  if (b & kGray124To8) t.expand_gray = true;
  if (b & kTrnsToAlpha) t.trns_to_alpha = true;
  t.expand_16 = b & kExpand16;
  t.scale_16 = b & kScale16;
  t.strip_16 = b & kStrip16;
  t.strip_alpha = b & kStripAlpha;
  t.gray_to_rgb = b & kGrayToRgb;
  if (b & kRgbToGray) {
    png::Transforms::RgbToGray r;
    if (b & kRgbToGrayCoef) r.red = 30000, r.green = 59000;
    t.rgb_to_gray = r;
  }
  if (x.gamma) {
    const double f = kFiles[(x.gamma - 1) % 4];
    t.screen_gamma = kScreens[(x.gamma - 1) / 4];
    t.file_gamma = f == 0 ? png::kGammaSrgb : f;
  }
  if (x.alpha) t.alpha_mode = png::Transforms::Alpha{png::AlphaMode(x.alpha - 1), x.alpha_linear ? 1.0 : png::kGammaSrgb};
  if (x.background) {
    png::Transforms::Background bg;
    bg.color = {static_cast<std::uint8_t>(kBackground[0]), kBackground[1], kBackground[2], kBackground[3],
                kBackground[4]};
    if (x.background == 1) {
      t.background = bg;
    } else if (x.background == 2) {
      bg.gamma_type = png::BackgroundGamma::kUnique;
      bg.gamma = 0.7;
      t.background = bg;
    } else if (info.background) {
      bg.color = *info.background;
      bg.gamma_type = png::BackgroundGamma::kFile;
      bg.need_expand = true;
      t.background = bg;
    }
  }
  if (b & kQuantize) {
    png::Transforms::Quantize q;
    q.palette = quantize_palette();
    q.max_colors = 12;
    if (b & kQuantizeHist) q.histogram = quantize_histogram();
    q.full = true;
    t.quantize = q;
  }
  if ((b & kShift) && info.sbit) t.shift = info.sbit;
  t.packing = b & kPacking;
  t.packswap = b & kPackswap;
  t.bgr = b & kBgr;
  t.swap_alpha = b & kSwapAlpha;
  t.invert_alpha = b & kInvertAlpha;
  t.invert_mono = b & kInvertMono;
  if (x.filler) t.filler = png::Transforms::Filler{0x1234, x.filler == 2 || x.filler == 4, x.filler >= 3};
}

// Cases libpng cannot run ("internal row size calculation error"), listed
// in libpng-expected.txt after the hash ("HASH/1,5,9"): left out of the
// hash on both sides.
inline std::vector<std::size_t> parse_skips(const std::string& value, std::string* hash) {
  std::vector<std::size_t> v;
  const auto slash = value.find('/');
  *hash = value.substr(0, slash);
  if (slash == std::string::npos) return v;
  std::size_t p = slash + 1;
  while (p < value.size()) {
    std::size_t q = value.find(',', p);
    if (q == std::string::npos) q = value.size();
    v.push_back(std::stoul(value.substr(p, q - p)));
    p = q + 1;
  }
  return v;
}

// vio's result for a case.
inline Result read_vio(const std::vector<std::uint8_t>& file, const Xform& x) {
  Result r;
  try {
    vio::png::Decoder d;
    d.decode(file);
    vio::png::Transforms t;
    to_vio(x, d.info, t);
    auto img = d.transform(t);
    r.row_bytes = img.row_bytes;
    r.channels = img.format.channels;
    r.bit_depth = img.format.bit_depth;
    r.color_type = img.format.color_type;
    // (libpng gives the palette of palette images only)
    if (r.color_type == vio::png::kIndexed && d.header.color_type == vio::png::kIndexed)
      for (auto& p : img.palette) r.palette.insert(r.palette.end(), {p.red, p.green, p.blue});
    r.pixels = std::move(img.pixels);
  } catch (const std::exception&) {
    r = Result();
    r.error = true;
  }
  return r;
}

// ---------------------------------------------------------------------------
// Generated images
// ---------------------------------------------------------------------------

namespace detail {

inline void put_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<std::uint8_t>(x >> s));
}
inline void put_chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
  put_u32(out, static_cast<std::uint32_t>(data.size()));
  const std::size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put_u32(out, vio::png::zlib::crc32(0, out.data() + start, out.size() - start));
}
inline int channels(int ct) { return vio::png::num_channels(static_cast<std::uint8_t>(ct)); }
inline std::size_t row_bytes(const vio::png::Header& h, std::uint32_t w) {
  return (static_cast<std::size_t>(w) * channels(h.color_type) * h.bit_depth + 7) / 8;
}

inline void filter_rows(std::vector<std::uint8_t>& out, const std::uint8_t* rows, std::size_t rb, std::uint32_t n,
                        unsigned bpp, std::mt19937& rng) {
  for (std::uint32_t y = 0; y < n; y++) {
    const std::uint8_t* cur = rows + y * rb;
    const std::uint8_t* up = y ? cur - rb : nullptr;
    const unsigned f = rng() % 5;
    out.push_back(static_cast<std::uint8_t>(f));
    for (std::size_t i = 0; i < rb; i++) {
      const unsigned a = i >= bpp ? cur[i - bpp] : 0, b = up ? up[i] : 0, c = up && i >= bpp ? up[i - bpp] : 0;
      unsigned p = 0;
      switch (f) {
        case 1: p = a; break;
        case 2: p = b; break;
        case 3: p = (a + b) / 2; break;
        case 4:
          p = vio::png::detail::paeth(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b),
                                      static_cast<std::uint8_t>(c));
          break;
      }
      out.push_back(static_cast<std::uint8_t>(cur[i] - p));
    }
  }
}

}  // namespace detail

// Ancillary chunks a generated image may carry.
struct Extras {
  std::uint32_t gamma = 0;  // gAMA (x 100000), 0 = none
  bool srgb = false;
  bool sbit = false;
  bool trns = false;
  bool bkgd = false;
};

// Writes a PNG of `img` (PNG byte order) with random filters (interlaced if
// h.interlace) and the extra chunks.
inline std::vector<std::uint8_t> write_png(const vio::png::Header& h, const std::vector<std::uint8_t>& img,
                                           const Extras& ex, std::mt19937& rng) {
  using detail::put_chunk;
  using detail::put_u32;
  std::vector<std::uint8_t> out = {137, 80, 78, 71, 13, 10, 26, 10}, ihdr;
  put_u32(ihdr, h.width);
  put_u32(ihdr, h.height);
  ihdr.insert(ihdr.end(), {h.bit_depth, h.color_type, 0, 0, h.interlace});
  put_chunk(out, "IHDR", ihdr);
  if (ex.gamma) {
    std::vector<std::uint8_t> g;
    put_u32(g, ex.gamma);
    put_chunk(out, "gAMA", g);
  }
  if (ex.srgb) put_chunk(out, "sRGB", {0});
  const int depth = h.color_type == vio::png::kIndexed ? 8 : h.bit_depth;
  if (ex.sbit) {
    std::vector<std::uint8_t> s;
    const int n = (h.color_type & 2) ? 3 : 1;
    for (int i = 0; i < n + ((h.color_type & 4) ? 1 : 0); i++) s.push_back(static_cast<std::uint8_t>(1 + rng() % depth));
    put_chunk(out, "sBIT", s);
  }
  const unsigned max = (1u << h.bit_depth) - 1;
  if (h.color_type == vio::png::kIndexed) {
    std::vector<std::uint8_t> plte(3u << h.bit_depth);
    for (auto& b : plte) b = static_cast<std::uint8_t>(rng());
    put_chunk(out, "PLTE", plte);
    if (ex.trns) {
      std::vector<std::uint8_t> t(1 + rng() % (1u << h.bit_depth));
      for (auto& b : t) b = static_cast<std::uint8_t>(rng() % 3 == 0 ? 0 : rng() % 3 == 0 ? 255 : rng());
      put_chunk(out, "tRNS", t);
    }
  } else if (ex.trns && !(h.color_type & 4)) {
    std::vector<std::uint8_t> t;
    for (int i = 0; i < ((h.color_type & 2) ? 3 : 1); i++) {
      const unsigned v = rng() & max;
      t.push_back(static_cast<std::uint8_t>(v >> 8));
      t.push_back(static_cast<std::uint8_t>(v));
    }
    put_chunk(out, "tRNS", t);
  }
  if (ex.bkgd) {
    std::vector<std::uint8_t> b;
    if (h.color_type == vio::png::kIndexed) {
      b.push_back(static_cast<std::uint8_t>(rng() % (1u << h.bit_depth)));
    } else {
      for (int i = 0; i < ((h.color_type & 2) ? 3 : 1); i++) {
        const unsigned v = rng() & max;
        b.push_back(static_cast<std::uint8_t>(v >> 8));
        b.push_back(static_cast<std::uint8_t>(v));
      }
    }
    put_chunk(out, "bKGD", b);
  }
  const unsigned bits = static_cast<unsigned>(detail::channels(h.color_type) * h.bit_depth),
                 bpp = bits < 8 ? 1 : bits / 8;
  const std::size_t rb = detail::row_bytes(h, h.width);
  std::vector<std::uint8_t> raw;
  if (!h.interlace) {
    detail::filter_rows(raw, img.data(), rb, h.height, bpp, rng);
  } else {
    using namespace vio::png::detail;
    for (int p = 0; p < 7; p++) {
      if (h.width <= static_cast<std::uint32_t>(adam7_x_start[p]) ||
          h.height <= static_cast<std::uint32_t>(adam7_y_start[p]))
        continue;
      const std::uint32_t pw = (h.width - adam7_x_start[p] + adam7_x_delta[p] - 1) / adam7_x_delta[p];
      const std::uint32_t ph = (h.height - adam7_y_start[p] + adam7_y_delta[p] - 1) / adam7_y_delta[p];
      const std::size_t prb = detail::row_bytes(h, pw);
      std::vector<std::uint8_t> pass(prb * ph, 0);
      for (std::uint32_t y = 0; y < ph; y++)
        for (std::uint32_t x = 0; x < pw; x++) {
          const std::size_t sx = adam7_x_start[p] + static_cast<std::size_t>(x) * adam7_x_delta[p];
          const std::size_t sy = adam7_y_start[p] + static_cast<std::size_t>(y) * adam7_y_delta[p];
          const std::uint8_t* src = img.data() + sy * rb;
          std::uint8_t* dst = pass.data() + y * prb;
          if (bits >= 8) {
            std::memcpy(dst + x * bpp, src + sx * bpp, bpp);
          } else {
            const unsigned spb = 8 / bits, s = (src[sx / spb] >> (8 - bits - (sx % spb) * bits)) & ((1u << bits) - 1);
            dst[x / spb] |= static_cast<std::uint8_t>(s << (8 - bits - (x % spb) * bits));
          }
        }
      detail::filter_rows(raw, pass.data(), prb, ph, bpp, rng);
    }
  }
  put_chunk(out, "IDAT", vio::png::zlib::compress(raw, 1 + static_cast<int>(rng() % 9)));
  put_chunk(out, "IEND", {});
  return out;
}

// Image content: random samples, gradients or flat areas.
inline std::vector<std::uint8_t> make_image(const vio::png::Header& h, std::mt19937& rng) {
  const std::size_t rb = detail::row_bytes(h, h.width);
  std::vector<std::uint8_t> img(rb * h.height, 0);
  const unsigned kind = rng() % 3;
  const unsigned bits = static_cast<unsigned>(detail::channels(h.color_type) * h.bit_depth);
  for (std::uint32_t y = 0; y < h.height; y++)
    for (std::size_t i = 0; i < rb; i++)
      img[y * rb + i] = kind == 0   ? static_cast<std::uint8_t>(rng())
                        : kind == 1 ? static_cast<std::uint8_t>(i * 3 + y * 5)
                                    : static_cast<std::uint8_t>((i / 7 + y / 5) % 2 ? 0x55 : 0);
  if (const unsigned used = (h.width * bits) % 8)
    for (std::uint32_t y = 0; y < h.height; y++) img[y * rb + rb - 1] &= static_cast<std::uint8_t>(0xff00u >> used);
  return img;
}

inline constexpr std::uint8_t kFormats[15][2] = {{0, 1}, {0, 2}, {0, 4}, {0, 8},  {0, 16}, {2, 8}, {2, 16}, {3, 1},
                                                 {3, 2}, {3, 4}, {3, 8}, {4, 8}, {4, 16}, {6, 8}, {6, 16}};

// A generated image: its name, file and samples (PNG byte order).
struct Generated {
  std::string name;
  std::vector<std::uint8_t> file, samples;
};

// The image sets:
//   standard     every format, sizes 1x1 to 1023x3, not interlaced
//   interlace    the same, interlaced
//   size         every format, every size up to 16x16, both
//   transform    every format, with and without tRNS, sBIT, bKGD
//   gamma        every format with gAMA values (and sRGB), sBIT, tRNS, bKGD
inline void generate(const std::string& set, const std::function<void(const Generated&)>& f) {
  using vio::png::Header;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes;
  if (set == "size") {
    for (std::uint32_t w = 1; w <= 16; w++)
      for (std::uint32_t h = 1; h <= 16; h++) sizes.push_back({w, h});
  } else if (set == "standard" || set == "interlace") {
    sizes = {{1, 1},   {2, 3},  {7, 5},   {8, 8},    {15, 31}, {16, 16}, {17, 9},  {31, 2},
             {32, 32}, {33, 17}, {64, 3}, {100, 37}, {129, 65}, {257, 4}, {1023, 3}};
  } else {
    sizes = {{17, 9}, {32, 8}, {7, 33}};
  }
  std::mt19937 rng(12345);
  std::vector<Extras> extras = {Extras{}};
  if (set == "transform") {
    extras = {Extras{}, Extras{0, false, false, true}, Extras{0, false, true}, Extras{0, false, false, false, true},
              Extras{45455, false, true, true, true}};
  } else if (set == "gamma") {
    extras.clear();
    for (std::uint32_t g : {45455u, 100000u, 50000u, 220000u, 35000u}) extras.push_back(Extras{g});
    extras.push_back(Extras{0, true});
    extras.push_back(Extras{45455, false, true, true, true});
    extras.push_back(Extras{100000, false, true, true, true});
    extras.push_back(Extras{0, false, true, true, true});
  }
  for (auto fmt : kFormats)
    for (auto [w, hh] : sizes)
      for (const Extras& ex : extras)
        for (int il = 0; il < 2; il++) {
          if (set == "standard" && il) continue;
          if (set == "interlace" && !il) continue;
          Header h;
          h.width = w;
          h.height = hh;
          h.color_type = fmt[0];
          h.bit_depth = fmt[1];
          h.interlace = static_cast<std::uint8_t>(il);
          Generated g;
          g.samples = make_image(h, rng);
          g.file = write_png(h, g.samples, ex, rng);
          char name[96];
          std::snprintf(name, sizeof name, "ct%d bd%d %ux%u%s g%u%s%s%s%s", fmt[0], fmt[1], w, hh, il ? " i" : "",
                        ex.gamma, ex.srgb ? " sRGB" : "", ex.sbit ? " sBIT" : "", ex.trns ? " tRNS" : "",
                        ex.bkgd ? " bKGD" : "");
          g.name = name;
          f(g);
        }
}

// The transformation cases run on a generated set: all of them for
// "transform"; gamma-related ones for the gamma sets.
inline std::vector<Xform> set_xforms(const std::string& kind) {
  if (kind == "transform") return xforms(64);
  std::vector<Xform> v;
  const bool e16 = kind.size() > 9 && kind.compare(kind.size() - 9, 9, "-expand16") == 0;
  const std::string k = e16 ? kind.substr(0, kind.size() - 9) : kind;
  const std::uint32_t extra = e16 ? kExpand16 : 0;
  if (k == "gamma-threshold" || k == "gamma-transform") {
    for (int g = 1; g <= 16; g++) v.push_back(Xform{extra, g});
    if (k == "gamma-transform")
      for (int g = 1; g <= 16; g++) v.push_back(Xform{kExpand | extra, g});
  } else if (k == "gamma-sbit") {
    for (int g = 1; g <= 16; g++) v.push_back(Xform{kShift | extra, g});
  } else if (k == "gamma-16-to-8") {
    for (int g = 1; g <= 16; g++) v.push_back(Xform{kScale16, g}), v.push_back(Xform{kStrip16, g});
  } else if (k == "gamma-background") {
    for (int b = 1; b <= 3; b++)
      for (int g : {0, 1, 2, 5, 9, 14}) v.push_back(Xform{kExpand | extra, g, 0, 0, b});
  } else if (k == "gamma-alpha-mode") {
    for (int a = 1; a <= 4; a++)
      for (int l = 0; l < 2; l++)
        for (int g : {0, 1, 6}) v.push_back(Xform{kExpand | extra, g, a, l});
  }
  return v;
}

// ---------------------------------------------------------------------------
// Pixel formats of the format-conversion API
// ---------------------------------------------------------------------------

// Every format: color/alpha/linear/colormap, BGR, alpha first, associated
// alpha (8-bit only).
inline std::vector<std::uint32_t> formats() {
  std::vector<std::uint32_t> v;
  for (std::uint32_t f = 0; f < 0x80; f++)
    if (!((f & 0x40) && (f & 4))) v.push_back(f);
  return v;
}

// ---------------------------------------------------------------------------
// Unknown chunk rules (pngunknown): name, default, then per-chunk rules
// ---------------------------------------------------------------------------

struct UnknownRule {
  const char* name;
  vio::png::ChunkKeep default_keep;
  std::vector<std::pair<const char*, vio::png::ChunkKeep>> chunks;
};
inline std::vector<UnknownRule> unknown_rules() {
  using K = vio::png::ChunkKeep;
  return {
      {"discard", K::kNever, {}},
      {"if-safe", K::kIfSafe, {}},
      {"save", K::kAlways, {}},
      {"sAPI", K::kNever,
       {{"bKGD", K::kAlways}, {"cHRM", K::kAlways}, {"gAMA", K::kAlways}, {"iCCP", K::kAlways},
        {"sBIT", K::kAlways}, {"sRGB", K::kAlways}, {"eXIf", K::kAlways}}},
      {"sTER", K::kNever, {{"sTER", K::kIfSafe}}},
      {"vpAg", K::kNever, {{"vpAg", K::kIfSafe}}},
  };
}

}  // namespace png_cases
