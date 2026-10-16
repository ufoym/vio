// png-test: the PNG codec's counterparts of libpng's tests, run by
// tests/libpng.cmake.  Values are compared with libpng-expected.txt, what
// libpng produces for the same cases (tools/png_golden.cpp).
//
//   png-test check [--roundtrip] EXPECTED ROOT FILE...       (pngtest)
//       Decodes each FILE (relative to ROOT); the samples must be libpng's,
//       or the file must be rejected where libpng rejects it.  --roundtrip
//       re-encodes each image, chunks included, in both encoder modes.
//   png-test xform EXPECTED ROOT FILE...                     (pngimage)
//       Reads each FILE with every read transformation case; the results
//       must be libpng's.  Then reads with each reversible transformation
//       and writes back with the matching write transformation: the image
//       must come back unchanged.
//   png-test valid EXPECTED SET [--progressive]              (pngvalid)
//       Generated images (SET = standard, interlace, size) must decode to
//       their samples (and as libpng decodes them) and re-encode unchanged;
//       with --progressive, fed to the stream decoder in small pieces.
//   png-test vxform EXPECTED KIND                            (pngvalid)
//       Transformation (KIND = transform) or gamma (gamma-*) cases on
//       generated images: the results must be libpng's.
//   png-test format EXPECTED ROOT [--stride-extra N] [--negative-stride] FILE...
//       (pngstest) Reads each FILE in every pixel format of the format
//       API (the results must be libpng's; with a stride option, the same
//       as with packed rows), writes each result back and reads it again.
//   png-test unknown EXPECTED FILE [RULE]                    (pngunknown)
//       Reads FILE with each rule for unknown chunks (or RULE only): the
//       chunks kept must be libpng's; with "discard", unknown ancillary
//       chunks must be skipped anywhere and critical ones rejected.
//   png-test getset                                          (pnggetset)
//       Every chunk value set in png::Info is written and read back.
//
// Exit status 0 if every check passes.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "png_cases.hpp"

namespace {

using namespace png_cases;
using vio::png::Decoder;
using vio::png::Encoder;
using vio::png::Header;
namespace png = vio::png;

int failures = 0;

void failure(const std::string& what) {
  std::fprintf(stderr, "FAIL: %s\n", what.c_str());
  failures++;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    failure("cannot open " + path);
    return {};
  }
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// libpng-expected.txt: "section key value" (keys may contain spaces).
struct Expected {
  std::map<std::string, std::string> values;
  explicit Expected(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
      if (line.empty() || line[0] == '#') continue;
      const auto sp = line.rfind(' ');
      values[line.substr(0, sp)] = line.substr(sp + 1);
    }
    if (values.empty()) failure("no expected values in " + path);
  }
  // The value for section/key, or "" (a failure) if there is none.
  std::string get(const std::string& section, const std::string& key) const {
    auto it = values.find(section + " " + key);
    if (it == values.end()) {
      failure(section + " " + key + ": no expected value");
      return "";
    }
    return it->second;
  }
};

// Samples in PNG byte order (16-bit samples big-endian).
std::vector<std::uint8_t> png_order(Decoder& d) {
  auto px = d.pixels();
  std::vector<std::uint8_t> v(px.begin(), px.end());
  if (d.header.bit_depth == 16)
    for (std::size_t i = 0; i + 1 < v.size(); i += 2) {
      std::uint16_t s;
      std::memcpy(&s, &v[i], 2);
      v[i] = static_cast<std::uint8_t>(s >> 8);
      v[i + 1] = static_cast<std::uint8_t>(s);
    }
  return v;
}

Result decode_result(Decoder& d) {
  Result r;
  r.row_bytes = d.row_bytes();
  r.channels = vio::png::num_channels(d.header.color_type);
  r.bit_depth = d.header.bit_depth;
  r.color_type = d.header.color_type;
  r.pixels = png_order(d);
  return r;
}

// The fields of two Infos that differ.
std::string info_diff(const png::Info& a, const png::Info& b) {
  std::string d;
#define VIO_FIELD(f) \
  if (!(a.f == b.f)) d += " " #f;
  VIO_FIELD(header) VIO_FIELD(palette) VIO_FIELD(trans_alpha) VIO_FIELD(trans_color) VIO_FIELD(gamma)
  VIO_FIELD(chromaticities) VIO_FIELD(srgb_intent) VIO_FIELD(icc) VIO_FIELD(cicp) VIO_FIELD(mastering)
  VIO_FIELD(content_light) VIO_FIELD(sbit) VIO_FIELD(background) VIO_FIELD(histogram) VIO_FIELD(phys)
  VIO_FIELD(offs) VIO_FIELD(pcal) VIO_FIELD(scal) VIO_FIELD(splt) VIO_FIELD(time) VIO_FIELD(text) VIO_FIELD(exif)
  VIO_FIELD(unknown) VIO_FIELD(animation) VIO_FIELD(frames)
#undef VIO_FIELD
  return d;
}

// Encodes a decoded image in `mode` and checks that it decodes unchanged,
// chunks included.
void check_roundtrip(Decoder& d, Encoder::Mode mode, const std::string& name) {
  Encoder e;
  e.mode = mode;
  e.info = d.info;
  e.info.animation.reset();
  e.info.frames.clear();
  e.info.default_image_is_frame = false;
  std::vector<std::uint8_t> file;
  try {
    file = e.encode(d.pixels().data(), d.row_bytes());
  } catch (const std::exception& ex) {
    failure(name + ": cannot re-encode: " + ex.what());
    return;
  }
  Decoder r;
  r.options.keep_unknown = d.options.keep_unknown;
  try {
    r.decode(file);
  } catch (const std::exception& ex) {
    failure(name + ": re-encoded file does not decode: " + ex.what());
    return;
  }
  const char* m = mode == Encoder::Mode::kFast ? "fast" : "zlib";
  png::Info a = d.info;
  a.animation.reset();
  a.frames.clear();
  a.default_image_is_frame = false;
  if (!(a.header == r.info.header)) failure(name + ": header changed in " + m + " mode");
  else if (png_order(r) != png_order(d)) failure(name + ": samples changed in " + m + " mode");
  else if (!(a == r.info)) failure(name + ": chunks changed in " + m + " mode:" + info_diff(a, r.info));
}

// ---------------------------------------------------------------------------
// check (pngtest)
// ---------------------------------------------------------------------------

int cmd_check(std::vector<std::string> args) {
  bool roundtrip = false;
  if (!args.empty() && args[0] == "--roundtrip") {
    roundtrip = true;
    args.erase(args.begin());
  }
  if (args.size() < 3) return 2;
  const Expected expected(args[0]);
  const std::string root = args[1];
  for (std::size_t i = 2; i < args.size(); i++) {
    const std::string& name = args[i];
    const std::string want = expected.get("decode", name);
    if (want.empty()) continue;
    std::vector<std::uint8_t> file = read_file(root + "/" + name);
    Decoder d;
    std::string error;
    try {
      d.decode(file);
    } catch (const vio::Error& ex) {
      error = ex.what();
    }
    if (want == "error") {
      if (error.empty()) failure(name + ": decoded, expected an error");
      continue;
    }
    if (!error.empty()) {
      failure(name + ": " + error);
      continue;
    }
    Fnv f;
    const std::vector<std::uint8_t> s = png_order(d);
    f.add(s.data(), s.size());
    if (want != f.hex()) {
      failure(name + ": samples differ (" + f.hex() + ", expected " + want + ")");
      continue;
    }
    if (roundtrip) {
      check_roundtrip(d, Encoder::Mode::kFast, name);
      check_roundtrip(d, Encoder::Mode::kZlib, name);
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// xform (pngimage)
// ---------------------------------------------------------------------------

// Reads with each reversible transformation, writes back with its inverse,
// and checks that the image is unchanged.
void check_reversible(const std::vector<std::uint8_t>& file, const std::string& name) {
  Decoder d;
  try {
    d.decode(file);
  } catch (const std::exception&) {
    return;
  }
  const std::vector<std::uint8_t> original = png_order(d);
  struct Case {
    const char* what;
    void (*set)(png::Transforms&, png::WriteTransforms&);
  };
  static const Case cases[] = {
      {"packing", [](png::Transforms& r, png::WriteTransforms& w) { r.packing = w.packing = true; }},
      {"packswap", [](png::Transforms& r, png::WriteTransforms& w) { r.packswap = w.packswap = true; }},
      {"invert mono", [](png::Transforms& r, png::WriteTransforms& w) { r.invert_mono = w.invert_mono = true; }},
      {"bgr", [](png::Transforms& r, png::WriteTransforms& w) { r.bgr = w.bgr = true; }},
      {"swap alpha", [](png::Transforms& r, png::WriteTransforms& w) { r.swap_alpha = w.swap_alpha = true; }},
      {"invert alpha", [](png::Transforms& r, png::WriteTransforms& w) { r.invert_alpha = w.invert_alpha = true; }},
      {"little endian",
       [](png::Transforms& r, png::WriteTransforms& w) { r.byte_order = w.byte_order = png::ByteOrder::kLittle; }},
      {"filler",
       [](png::Transforms& r, png::WriteTransforms& w) {
         r.filler = png::Transforms::Filler{0xff, false, false};
         w.strip_filler = r.filler;
       }},
      {"filler first",
       [](png::Transforms& r, png::WriteTransforms& w) {
         r.filler = png::Transforms::Filler{0xff, true, false};
         w.strip_filler = r.filler;
       }},
  };
  for (const Case& c : cases) {
    png::Transforms rt;
    png::WriteTransforms wt;
    rt.byte_order = png::ByteOrder::kBig;
    wt.byte_order = png::ByteOrder::kBig;
    c.set(rt, wt);
    try {
      const png::Decoder::Image img = d.transform(rt);
      Encoder e;
      e.info = d.info;
      e.info.animation.reset();
      e.info.frames.clear();
      e.info.default_image_is_frame = false;
      e.transforms = wt;
      Decoder r;
      r.decode(e.encode(img.pixels.data(), img.row_bytes));
      if (png_order(r) != original) failure(name + ": " + c.what + " does not round-trip");
    } catch (const std::exception& ex) {
      failure(name + ": " + c.what + ": " + ex.what());
    }
  }
}

int cmd_xform(const std::vector<std::string>& args) {
  if (args.size() < 3) return 2;
  const Expected expected(args[0]);
  const std::vector<Xform> cases = xforms();
  for (std::size_t i = 2; i < args.size(); i++) {
    const std::string& name = args[i];
    std::string want;
    const std::vector<std::size_t> skips = parse_skips(expected.get("xform", name), &want);
    const std::vector<std::uint8_t> file = read_file(args[1] + "/" + name);
    Fnv f;
    for (std::size_t k = 0; k < cases.size(); k++)
      if (std::find(skips.begin(), skips.end(), k) == skips.end()) f.add(read_vio(file, cases[k]).hash());
    if (!want.empty() && f.hex() != want) failure(name + ": transformed images differ from libpng's");
    check_reversible(file, name);
  }
  return 0;
}

// ---------------------------------------------------------------------------
// valid, vxform (pngvalid)
// ---------------------------------------------------------------------------

// Decodes `file` with the stream decoder, fed in pieces of random sizes.
bool stream_decode(const std::vector<std::uint8_t>& file, std::vector<std::uint8_t>& samples, std::mt19937& rng) {
  png::StreamDecoder s;
  std::size_t rb = 0;
  std::uint32_t width = 0;
  s.transforms.byte_order = png::ByteOrder::kBig;
  s.on_info = [&] {
    const Header& h = s.info().header;
    width = h.width;
    rb = (static_cast<std::size_t>(width) * vio::png::num_channels(h.color_type) * h.bit_depth + 7) / 8;
    samples.assign(rb * h.height, 0);
  };
  s.on_row = [&](std::uint32_t y, int pass, const std::uint8_t* row) {
    s.combine_row(samples.data() + y * rb, row, pass, width);
  };
  for (std::size_t pos = 0; pos < file.size();) {
    const std::size_t n = std::min<std::size_t>(file.size() - pos, 1 + rng() % 64);
    s.feed(file.data() + pos, n);
    pos += n;
  }
  return s.done();
}

int cmd_valid(const std::vector<std::string>& args) {
  if (args.size() < 2) return 2;
  const Expected expected(args[0]);
  const std::string set = args[1];
  const bool progressive = args.size() > 2 && args[2] == "--progressive";
  const std::string want = expected.get("valid", set);
  std::mt19937 rng(7);
  Fnv f;
  int images = 0;
  generate(set, [&](const Generated& g) {
    images++;
    Decoder d;
    try {
      d.decode(g.file);
    } catch (const std::exception& ex) {
      failure(g.name + ": " + ex.what());
      return;
    }
    f.add(decode_result(d).hash());
    if (png_order(d) != g.samples) {
      failure(g.name + ": samples differ");
      return;
    }
    if (progressive) {
      std::vector<std::uint8_t> s;
      try {
        if (!stream_decode(g.file, s, rng)) failure(g.name + ": stream decoding did not finish");
        else if (s != g.samples) failure(g.name + ": stream-decoded samples differ");
      } catch (const std::exception& ex) {
        failure(g.name + ": stream decoding: " + ex.what());
      }
      return;
    }
    check_roundtrip(d, Encoder::Mode::kFast, g.name);
    check_roundtrip(d, Encoder::Mode::kZlib, g.name);
  });
  if (!want.empty() && f.hex() != want) failure(set + ": decoded images differ from libpng's");
  std::printf("%d images\n", images);
  return 0;
}

int cmd_vxform(const std::vector<std::string>& args) {
  if (args.size() < 2) return 2;
  const Expected expected(args[0]);
  const std::string kind = args[1];
  std::string want;
  const std::vector<std::size_t> skips = parse_skips(expected.get("vxform", kind), &want);
  const std::vector<Xform> cases = set_xforms(kind);
  if (cases.empty()) failure(kind + ": no cases");
  Fnv f;
  int images = 0;
  std::size_t index = 0;
  generate(kind == "transform" ? "transform" : "gamma", [&](const Generated& g) {
    images++;
    for (const Xform& x : cases) {
      if (std::find(skips.begin(), skips.end(), index) == skips.end()) f.add(read_vio(g.file, x).hash());
      index++;
    }
  });
  if (!want.empty() && f.hex() != want) failure(kind + ": transformed images differ from libpng's");
  std::printf("%d images, %zu cases\n", images, cases.size());
  return 0;
}

// ---------------------------------------------------------------------------
// format (pngstest)
// ---------------------------------------------------------------------------

struct FormatImage {
  std::uint32_t width = 0, height = 0, entries = 0;
  std::vector<std::uint8_t> pixels, colormap;  // top-down, packed
};

// Reads `file` in format `fmt` (bg: with a background color, into a
// bottom-up buffer; rows `extra` samples longer than needed), the buffer
// prefilled as png-golden does; returns the hash png-golden computes.
std::uint64_t format_read(const std::vector<std::uint8_t>& file, std::uint32_t fmt, int bg, std::ptrdiff_t extra,
                          FormatImage* image) {
  Fnv f;
  try {
    png::FormatReader r(file);
    r.format = fmt;
    r.colormap_entries = 256;
    const unsigned cs = png::pixel_component_size(fmt);
    const std::size_t row = r.row_stride() * cs;
    const std::size_t stride = (r.row_stride() + static_cast<std::size_t>(extra)) * cs;
    std::vector<std::uint8_t> buf(stride * r.height), cm(256 * 8, 0);
    for (std::uint32_t y = 0; y < r.height; y++)
      for (std::size_t k = 0; k < row; k++) buf[y * stride + k] = static_cast<std::uint8_t>((y * row + k) * 7 + 3);
    const png::PaletteEntry c{0x40, 0x80, 0xc0};
    const std::ptrdiff_t samples = static_cast<std::ptrdiff_t>(r.row_stride()) + extra;
    r.read(buf.data(), bg ? -samples : samples, cm.data(), bg ? &c : nullptr);
    std::vector<std::uint8_t> out(row * r.height);
    for (std::uint32_t y = 0; y < r.height; y++) std::memcpy(out.data() + y * row, buf.data() + y * stride, row);
    // 16-bit samples are in host byte order; hashed little-endian, as the
    // reference values were computed
    auto add_samples = [&](const std::uint8_t* p, std::size_t n, unsigned size) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      if (size == 2) {
        std::vector<std::uint8_t> le(p, p + n);
        for (std::size_t i = 0; i + 1 < n; i += 2) std::swap(le[i], le[i + 1]);
        f.add(le.data(), n);
        return;
      }
#endif
      (void)size;
      f.add(p, n);
    };
    add_samples(out.data(), out.size(), cs);
    const std::size_t cms = png::format_channels(fmt) * png::format_component_size(fmt);
    if (fmt & png::kFormatColormap) {
      f.add(static_cast<std::uint64_t>(r.colormap_entries));
      add_samples(cm.data(), r.colormap_entries * cms, png::format_component_size(fmt));
    }
    if (image) {
      image->width = r.width;
      image->height = r.height;
      image->entries = r.colormap_entries;
      image->pixels.resize(out.size());
      for (std::uint32_t y = 0; y < r.height; y++)
        std::memcpy(image->pixels.data() + y * row, out.data() + (bg ? r.height - 1 - y : y) * row, row);
      image->colormap.assign(cm.begin(), cm.begin() + static_cast<std::ptrdiff_t>(r.colormap_entries * cms));
    }
  } catch (const std::exception&) {
    Fnv e;
    e.add(std::string("error"));
    return e.h;
  }
  return f.h;
}

// Writes an image read in `fmt`, reads it back in the same format: it must
// come back within rounding (8-bit: 1 for opaque pixels; 16-bit linear: the
// precision of premultiplication; colors of transparent pixels are lost).
void check_format_roundtrip(const std::string& name, std::uint32_t fmt, const FormatImage& img) {
  if (fmt & png::kFormatAssociatedAlpha) return;  // not a format to write
  const std::string what = name + ": format " + std::to_string(fmt);
  try {
    png::FormatWriter wr;
    wr.width = img.width;
    wr.height = img.height;
    wr.format = fmt;
    wr.colormap_entries = img.entries;
    const std::vector<std::uint8_t> file = wr.write(img.pixels.data(), 0, img.colormap.data());
    png::FormatReader r(file);
    r.format = fmt;
    r.colormap_entries = 256;
    std::vector<std::uint8_t> back(r.buffer_size()), cm(256 * 8, 0);
    const png::PaletteEntry c{0x40, 0x80, 0xc0};
    r.read(back.data(), 0, cm.data(), &c);
    const unsigned ch = png::format_channels(fmt), size = png::format_component_size(fmt);
    auto expand = [&](const std::vector<std::uint8_t>& p, const std::uint8_t* map) {
      if (!(fmt & png::kFormatColormap)) return p;
      std::vector<std::uint8_t> v;
      for (std::uint8_t i : p) v.insert(v.end(), map + i * ch * size, map + (i + 1) * ch * size);
      return v;
    };
    const std::vector<std::uint8_t> a = expand(img.pixels, img.colormap.data()), b = expand(back, cm.data());
    if (a.size() != b.size()) {
      failure(what + ": round trip changed the size");
      return;
    }
    const bool alpha = (fmt & png::kFormatAlpha) != 0, afirst = alpha && (fmt & png::kFormatAlphaFirst);
    long worst = 0;
    for (std::size_t p = 0; p < a.size() / (ch * size); p++) {
      // a colormap is stored as 8-bit sRGB: linear ones compare at that
      // precision
      const bool via8 = size == 2 && (fmt & png::kFormatColormap);
      auto sample = [&](const std::vector<std::uint8_t>& v, unsigned c) -> long {
        const std::size_t k = (p * ch + c) * size;
        if (size == 1) return v[k];
        std::uint16_t s;
        std::memcpy(&s, &v[k], 2);
        if (via8) return static_cast<long>(png::detail::srgb_from_linear(s * 255u));
        return s;
      };
      const long max = size == 1 || via8 ? 255 : 65535;
      const long a_alpha = alpha ? sample(a, afirst ? 0 : ch - 1) : max;
      for (unsigned c = 0; c < ch; c++) {
        long d = std::labs(sample(a, c) - sample(b, c));
        const bool is_alpha = alpha && c == (afirst ? 0u : ch - 1);
        if (!is_alpha && a_alpha < max) d = 0;  // colors of partly transparent pixels are rounded more
        worst = std::max(worst, d);
      }
    }
    const long limit = size == 1 ? 1 : 2;
    if (worst > limit) failure(what + ": round trip error " + std::to_string(worst));
  } catch (const std::exception& ex) {
    failure(what + ": write/read: " + ex.what());
  }
}

int cmd_format(const std::vector<std::string>& args) {
  if (args.size() < 3) return 2;
  const Expected expected(args[0]);
  const std::string root = args[1];
  std::ptrdiff_t extra = 0;
  bool negative_only = false, golden = true;
  std::size_t i = 2;
  for (; i < args.size() && args[i].rfind("--", 0) == 0; i++) {
    if (args[i] == "--stride-extra" && i + 1 < args.size()) {
      extra = std::stol(args[++i]);
      golden = false;
    } else if (args[i] == "--negative-stride") {
      negative_only = true;
      golden = false;
    }
  }
  for (; i < args.size(); i++) {
    const std::string& name = args[i];
    const std::vector<std::uint8_t> file = read_file(root + "/" + name);
    Fnv f;
    for (std::uint32_t fmt : formats())
      for (int bg = 0; bg < 2; bg++) {
        if (negative_only && !bg) continue;
        FormatImage img;
        const std::uint64_t h = format_read(file, fmt, bg, extra, &img);
        f.add(h);
        if (!golden && format_read(file, fmt, bg, 0, nullptr) != h)
          failure(name + ": format " + std::to_string(fmt) + " differs with another stride");
        if (bg && !img.pixels.empty()) check_format_roundtrip(name, fmt, img);
      }
    if (golden) {
      const std::string want = expected.get("format", name);
      if (!want.empty() && f.hex() != want) failure(name + ": formats differ from libpng's");
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// unknown (pngunknown)
// ---------------------------------------------------------------------------

int cmd_unknown(const std::vector<std::string>& args) {
  if (args.size() < 2) return 2;
  const Expected expected(args[0]);
  const std::vector<std::uint8_t> file = read_file(args[1]);
  Decoder ref;
  ref.decode(file);
  const std::vector<std::uint8_t> samples = png_order(ref);
  std::vector<UnknownRule> rules = unknown_rules();
  // IDAT cannot be handled as an unknown chunk: the same as discarding
  rules.push_back({"IDAT", png::ChunkKeep::kNever, {{"IDAT", png::ChunkKeep::kAlways}}});
  auto sorted = [](const std::string& s) {
    std::vector<std::string> v;
    for (std::size_t p = 0; p + 4 <= s.size(); p += 5) v.push_back(s.substr(p, 4));
    std::sort(v.begin(), v.end());
    return v;
  };
  const std::string only = args.size() > 2 ? args[2] : "";
  for (const UnknownRule& rule : rules) {
    if (!only.empty() && only != rule.name) continue;
    Decoder d;
    d.options.keep_unknown = rule.default_keep;
    for (const auto& [n, k] : rule.chunks) {
      std::array<char, 4> t;
      std::memcpy(t.data(), n, 4);
      d.options.keep.push_back({t, k});
    }
    std::string kept;
    try {
      d.decode(file);
      for (const auto& u : d.info.unknown) kept += (kept.empty() ? "" : ",") + u.name();
      if (png_order(d) != samples) failure(std::string(rule.name) + ": samples differ");
    } catch (const std::exception& ex) {
      failure(std::string(rule.name) + ": " + ex.what());
      continue;
    }
    if (kept.empty()) kept = "-";
    const std::string want = expected.get("unknown", std::string(rule.name) == "IDAT" ? "discard" : rule.name);
    if (!want.empty() && sorted(kept) != sorted(want))
      failure(std::string(rule.name) + ": kept " + kept + ", libpng keeps " + want);
    if (rule.default_keep == png::ChunkKeep::kAlways)
      check_roundtrip(d, Encoder::Mode::kZlib, std::string("unknown ") + rule.name);
  }
  if (!only.empty() && only != "discard") return 0;
  // Unknown ancillary chunks anywhere are skipped; critical ones rejected.
  auto with_chunk = [&](const char* type, bool at_end) {
    std::vector<std::uint8_t> chunk;
    detail::put_chunk(chunk, type, {1, 2, 3, 4, 5});
    std::vector<std::uint8_t> f = file;
    f.insert(at_end ? f.end() - 12 : f.begin() + 33, chunk.begin(), chunk.end());
    return f;
  };
  for (const char* type : {"vpAg", "sTER", "juNk", "juNK", "zzzz"})
    for (bool at_end : {false, true}) {
      Decoder d;
      try {
        d.decode(with_chunk(type, at_end));
        if (png_order(d) != samples) failure(std::string(type) + ": samples differ");
      } catch (const std::exception& ex) {
        failure(std::string(type) + ": " + ex.what());
      }
    }
  for (const char* type : {"ZZZZ", "Abcd"}) {
    Decoder d;
    try {
      d.decode(with_chunk(type, false));
      failure(std::string(type) + ": unknown critical chunk accepted");
    } catch (const vio::Error&) {
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// getset (pnggetset)
// ---------------------------------------------------------------------------

int cmd_getset() {
  std::mt19937 rng(99);
  for (int it = 0; it < 60; it++) {
    const auto& fmt = kFormats[it % 15];
    Encoder e;
    png::Info& in = e.info;
    in.header.width = 1 + rng() % 40;
    in.header.height = 1 + rng() % 20;
    in.header.color_type = fmt[0];
    in.header.bit_depth = fmt[1];
    in.header.interlace = static_cast<std::uint8_t>(rng() % 2);
    const unsigned max = (1u << fmt[1]) - 1;
    auto sample = [&] { return static_cast<std::uint16_t>(rng() & max); };
    if (fmt[0] == png::kIndexed) {
      for (unsigned i = 0; i <= max; i++)
        in.palette.push_back({static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng()),
                              static_cast<std::uint8_t>(rng())});
      const unsigned nt = 1 + rng() % (max + 1);
      for (unsigned i = 0; i < nt; i++) in.trans_alpha.push_back(static_cast<std::uint8_t>(rng()));
      in.histogram.resize(in.palette.size());
      for (auto& v : in.histogram) v = static_cast<std::uint16_t>(rng());
      const std::uint8_t bi = static_cast<std::uint8_t>(rng() % (max + 1));
      // (the decoder gives the entry's color too)
      in.background = png::Color16{bi, in.palette[bi].red, in.palette[bi].green, in.palette[bi].blue, 0};
    } else if (fmt[0] & 2) {
      if (!(fmt[0] & 4)) in.trans_color = png::Color16{0, sample(), sample(), sample(), 0};
      in.background = png::Color16{0, sample(), sample(), sample(), 0};
    } else {
      if (!(fmt[0] & 4)) in.trans_color = png::Color16{0, 0, 0, 0, sample()};
      const std::uint16_t g = sample();
      in.background = png::Color16{0, g, g, g, g};
    }
    const int depth = fmt[0] == png::kIndexed ? 8 : fmt[1];
    in.sbit = png::SignificantBits{};
    if (fmt[0] & 2) {
      in.sbit->red = static_cast<std::uint8_t>(1 + rng() % depth);
      in.sbit->green = static_cast<std::uint8_t>(1 + rng() % depth);
      in.sbit->blue = static_cast<std::uint8_t>(1 + rng() % depth);
    } else {
      in.sbit->gray = static_cast<std::uint8_t>(1 + rng() % depth);
    }
    if (fmt[0] & 4) in.sbit->alpha = static_cast<std::uint8_t>(1 + rng() % depth);
    if (rng() % 2) {
      in.srgb_intent = static_cast<std::uint8_t>(rng() % 4);
      in.gamma = 45455;
    } else {
      in.gamma = 10000 + rng() % 300000;
      // a profile header the decoder accepts: size, color space, signature
      std::vector<std::uint8_t> profile(132 + 12 * (rng() % 8), 7);
      const std::uint32_t n = static_cast<std::uint32_t>(profile.size());
      for (int k = 0; k < 4; k++) profile[k] = static_cast<std::uint8_t>(n >> (24 - 8 * k));
      std::memcpy(profile.data() + 16, (fmt[0] & 2) ? "RGB " : "GRAY", 4);
      std::memcpy(profile.data() + 36, "acsp", 4);
      std::memset(profile.data() + 128, 0, 4);
      in.icc = png::IccProfile{"profile", profile};
    }
    in.chromaticities = png::Chromaticities{31270, 32900, 64000, 33000, 30000, 60000, 15000, 6000};
    in.cicp = png::Cicp{1, 13, 0, 1};
    in.mastering = png::MasteringDisplay{35400, 14600, 8500, 39850, 6550, 2300, 15635, 16450, 10000000, 1};
    in.content_light = png::ContentLight{static_cast<std::uint32_t>(rng() % 100000),
                                         static_cast<std::uint32_t>(rng() % 100000)};
    in.phys = png::Physical{static_cast<std::uint32_t>(rng() % 10000), static_cast<std::uint32_t>(rng() % 10000),
                            static_cast<std::uint8_t>(rng() % 2)};
    in.offs = png::Offset{static_cast<std::int32_t>(rng() % 1000) - 500, static_cast<std::int32_t>(rng() % 1000),
                          static_cast<std::uint8_t>(rng() % 2)};
    in.pcal = png::Calibration{"calibration", 0, 65535, 0, "unit", {"1.5", "2e3"}};
    in.scal = png::Scale{1, "1.5", "2.25"};
    in.splt.push_back(png::SuggestedPalette{"suggested", 8, {{1, 2, 3, 4, 5}, {6, 7, 8, 9, 10}}});
    in.time = png::Time{2026, 10, 15, 12, 30, 59};
    in.exif = std::vector<std::uint8_t>{'M', 'M', 0, 42, 0, 0, 0, 8};
    in.text.push_back(png::Text{png::Text::kText, "Title", "a title", "", "", png::kBeforeIDAT});
    in.text.push_back(png::Text{png::Text::kZtxt, "Comment", std::string(200, 'z'), "", "", png::kBeforeIDAT});
    in.text.push_back(png::Text{png::Text::kItxt, "Author", "\xc3\xa9", "fr", "Auteur", png::kBeforeIDAT});
    in.text.push_back(
        png::Text{png::Text::kItxtCompressed, "Description", std::string(300, 'd'), "en", "", png::kAfterIDAT});
    png::UnknownChunk u;
    std::memcpy(u.type.data(), "prVt", 4);
    u.data = {1, 2, 3};
    u.location = png::kBeforePLTE;
    in.unknown.push_back(u);
    const std::size_t rb = e.row_bytes();
    std::vector<std::uint8_t> px(rb * in.header.height);
    for (auto& v : px) v = static_cast<std::uint8_t>(rng());
    const unsigned bits = static_cast<unsigned>(vio::png::num_channels(fmt[0]) * fmt[1]);
    if (const unsigned used = (in.header.width * bits) % 8)
      for (std::uint32_t y = 0; y < in.header.height; y++) px[y * rb + rb - 1] &= static_cast<std::uint8_t>(0xff00u >> used);
    for (Encoder::Mode mode : {Encoder::Mode::kFast, Encoder::Mode::kZlib}) {
      e.mode = mode;
      Decoder d;
      d.options.keep_unknown = png::ChunkKeep::kAlways;
      try {
        d.decode(e.encode(px.data(), rb));
      } catch (const std::exception& ex) {
        failure(std::string("getset: ") + ex.what());
        continue;
      }
      const std::string what = "getset: format " + std::to_string(fmt[0]) + "/" + std::to_string(fmt[1]);
      if (!(d.info == in)) failure(what + ": chunk values changed:" + info_diff(in, d.info));
      if (std::memcmp(d.pixels().data(), px.data(), px.size()) != 0) failure(what + ": samples changed");
    }
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: png-test check|xform|valid|vxform|format|unknown|getset ...\n");
    return 2;
  }
  const std::string cmd = argv[1];
  const std::vector<std::string> args(argv + 2, argv + argc);
  int status = 2;
  if (cmd == "check") status = cmd_check(args);
  else if (cmd == "xform") status = cmd_xform(args);
  else if (cmd == "valid") status = cmd_valid(args);
  else if (cmd == "vxform") status = cmd_vxform(args);
  else if (cmd == "format") status = cmd_format(args);
  else if (cmd == "unknown") status = cmd_unknown(args);
  else if (cmd == "getset") status = cmd_getset();
  if (status) return status;
  if (failures) std::fprintf(stderr, "%d failures\n", failures);
  return failures ? 1 : 0;
}
