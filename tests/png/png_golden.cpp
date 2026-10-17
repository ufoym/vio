// png-golden: writes png/expected.txt, what libpng produces for the
// cases of png_cases.hpp, for png-test to compare vio with.  Built only
// against libpng (VIO_PNG_GOLDEN=ON); run as
//
//   png-golden LIBPNG_DIR > tests/png/expected.txt
//
// Lines are "section key value":
//   decode FILE HASH|error     samples as decoded (PNG layout, 16-bit
//                              big-endian), or an error
//   xform FILE HASH            all read transformation cases on FILE
//   vxform SET HASH            the cases of SET on its generated images
//   valid SET HASH             the generated images of SET as decoded
//   format FILE HASH           FILE read in every pixel format
//   unknown RULE CHUNKS        the unknown chunks kept from pngtest.png
#include <png.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "png_cases.hpp"

namespace {

using namespace png_cases;

std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

struct Buf {
  const std::vector<std::uint8_t>* d;
  std::size_t pos;
};
void rd(png_structp p, png_bytep o, png_size_t n) {
  auto* b = static_cast<Buf*>(png_get_io_ptr(p));
  if (b->pos + n > b->d->size()) png_error(p, "eof");
  std::memcpy(o, b->d->data() + b->pos, n);
  b->pos += n;
}
std::string last_error;
void quiet(png_structp, png_const_charp) {}
void error_fn(png_structp p, png_const_charp m) {
  last_error = m;
  png_longjmp(p, 1);
}

void apply(png_structp p, png_infop i, const Xform& x) {
  const std::uint32_t b = x.bits;
  if (b & kExpand) png_set_expand(p);
  if (b & kPaletteToRgb) png_set_palette_to_rgb(p);
  if (b & kGray124To8) png_set_expand_gray_1_2_4_to_8(p);
  if (b & kTrnsToAlpha) png_set_tRNS_to_alpha(p);
  if (b & kExpand16) png_set_expand_16(p);
  if (b & kScale16) png_set_scale_16(p);
  if (b & kStrip16) png_set_strip_16(p);
  if (b & kStripAlpha) png_set_strip_alpha(p);
  if (b & kGrayToRgb) png_set_gray_to_rgb(p);
  if (b & kRgbToGray) {
    if (b & kRgbToGrayCoef) png_set_rgb_to_gray_fixed(p, 1, 30000, 59000);
    else png_set_rgb_to_gray_fixed(p, 1, -1, -1);
  }
  if (x.gamma) {
    const double f = kFiles[(x.gamma - 1) % 4];
    png_set_gamma(p, kScreens[(x.gamma - 1) / 4], f == 0 ? PNG_DEFAULT_sRGB : f);
  }
  if (x.alpha) png_set_alpha_mode(p, x.alpha - 1, x.alpha_linear ? 1.0 : PNG_DEFAULT_sRGB);
  if (x.background) {
    png_color_16 c{static_cast<png_byte>(kBackground[0]), kBackground[1], kBackground[2], kBackground[3],
                   kBackground[4]};
    if (x.background == 1) {
      png_set_background(p, &c, PNG_BACKGROUND_GAMMA_SCREEN, 0, 1.0);
    } else if (x.background == 2) {
      png_set_background(p, &c, PNG_BACKGROUND_GAMMA_UNIQUE, 0, 0.7);
    } else {
      png_color_16p bg;
      if (png_get_bKGD(p, i, &bg)) png_set_background(p, bg, PNG_BACKGROUND_GAMMA_FILE, 1, 1.0);
    }
  }
  if (b & kQuantize) {
    std::vector<png_color> pal;
    for (auto& e : quantize_palette()) pal.push_back({e.red, e.green, e.blue});
    std::vector<png_uint_16> hist = quantize_histogram();
    png_set_quantize(p, pal.data(), static_cast<int>(pal.size()), 12, (b & kQuantizeHist) ? hist.data() : nullptr, 1);
  }
  if (b & kShift) {
    png_color_8p sb;
    if (png_get_sBIT(p, i, &sb)) png_set_shift(p, sb);
  }
  if (b & kPacking) png_set_packing(p);
  if (b & kPackswap) png_set_packswap(p);
  if (b & kBgr) png_set_bgr(p);
  if (b & kSwapAlpha) png_set_swap_alpha(p);
  if (b & kInvertAlpha) png_set_invert_alpha(p);
  if (b & kInvertMono) png_set_invert_mono(p);
  if (x.filler == 1) png_set_filler(p, 0x1234, PNG_FILLER_AFTER);
  if (x.filler == 2) png_set_filler(p, 0x1234, PNG_FILLER_BEFORE);
  if (x.filler == 3) png_set_add_alpha(p, 0x1234, PNG_FILLER_AFTER);
  if (x.filler == 4) png_set_add_alpha(p, 0x1234, PNG_FILLER_BEFORE);
  if (b & kSwap) png_set_swap(p);
}

// Reads with a case (nullptr: no transformation, samples in PNG layout).
Result read_libpng(const std::vector<std::uint8_t>& data, const Xform* x) {
  Result r;
  png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, x ? error_fn : quiet, quiet);
  png_infop i = png_create_info_struct(p);
  png_set_benign_errors(p, 1);
  std::vector<png_bytep> rows;
  if (setjmp(png_jmpbuf(p))) {
    png_destroy_read_struct(&p, &i, nullptr);
    Result e;
    e.error = true;
    return e;
  }
  Buf b{&data, 0};
  png_set_read_fn(p, &b, rd);
  png_read_info(p, i);
  if (x) apply(p, i, *x);
  png_set_interlace_handling(p);
  png_read_update_info(p, i);
  r.row_bytes = png_get_rowbytes(p, i);
  r.channels = png_get_channels(p, i);
  r.bit_depth = png_get_bit_depth(p, i);
  r.color_type = png_get_color_type(p, i);
  png_colorp pal;
  int np;
  if (x && r.color_type == PNG_COLOR_TYPE_PALETTE && png_get_PLTE(p, i, &pal, &np))
    for (int k = 0; k < np; k++) r.palette.insert(r.palette.end(), {pal[k].red, pal[k].green, pal[k].blue});
  const std::uint32_t h = png_get_image_height(p, i);
  r.pixels.assign(r.row_bytes * h, 0);
  for (std::uint32_t y = 0; y < h; y++) rows.push_back(r.pixels.data() + y * r.row_bytes);
  png_read_image(p, rows.data());
  png_read_end(p, nullptr);
  png_destroy_read_struct(&p, &i, nullptr);
  return r;
}

std::string hex(std::uint64_t v) {
  char s[17];
  std::snprintf(s, sizeof s, "%016llx", static_cast<unsigned long long>(v));
  return s;
}

// Hash of the results of `cases` on `data`, the cases libpng cannot run
// listed after it.
std::string xform_value(const std::vector<std::vector<std::uint8_t>>& files, const std::vector<Xform>& cases) {
  Fnv f;
  std::string skips;
  std::size_t index = 0;
  for (const auto& data : files)
    for (const Xform& x : cases) {
      last_error.clear();
      const Result r = read_libpng(data, &x);
      if (r.error && last_error == "internal row size calculation error") {
        skips += (skips.empty() ? "/" : ",") + std::to_string(index);
      } else {
        f.add(r.hash());
      }
      index++;
    }
  return f.hex() + skips;
}

// The format-conversion reads of a file (every format, without and with a
// background, a top-down and a bottom-up buffer prefilled with a pattern).
std::uint64_t format_hash(const std::vector<std::uint8_t>& data) {
  Fnv f;
  for (std::uint32_t fmt : formats())
    for (int bg = 0; bg < 2; bg++) {
      png_image im{};
      im.version = PNG_IMAGE_VERSION;
      if (!png_image_begin_read_from_memory(&im, data.data(), data.size())) {
        Fnv e;
        e.add(std::string("error"));
        f.add(e.h);
        continue;
      }
      im.format = fmt;
      im.colormap_entries = 256;
      std::vector<std::uint8_t> buf(PNG_IMAGE_SIZE(im)), cm(256 * 8, 0);
      for (std::size_t k = 0; k < buf.size(); k++) buf[k] = static_cast<std::uint8_t>(k * 7 + 3);
      const png_color c{0x40, 0x80, 0xc0};
      const int stride = static_cast<int>(PNG_IMAGE_ROW_STRIDE(im)) * (bg ? -1 : 1);
      if (!png_image_finish_read(&im, bg ? &c : nullptr, buf.data(), stride, cm.data())) {
        Fnv e;
        e.add(std::string("error"));
        f.add(e.h);
        continue;
      }
      Fnv one;
      one.add(buf.data(), buf.size());
      if (fmt & PNG_FORMAT_FLAG_COLORMAP) {
        one.add(static_cast<std::uint64_t>(im.colormap_entries));
        one.add(cm.data(), im.colormap_entries * PNG_IMAGE_SAMPLE_SIZE(fmt));
      }
      f.add(one.h);
    }
  return f.h;
}

std::string unknown_kept(const std::vector<std::uint8_t>& data, const UnknownRule& rule) {
  png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, quiet, quiet);
  png_infop i = png_create_info_struct(p);
  png_set_benign_errors(p, 1);
  std::string out;
  if (setjmp(png_jmpbuf(p))) {
    png_destroy_read_struct(&p, &i, nullptr);
    return "error";
  }
  auto keep = [](vio::png::ChunkKeep k) {
    switch (k) {
      case vio::png::ChunkKeep::kNever: return PNG_HANDLE_CHUNK_NEVER;
      case vio::png::ChunkKeep::kIfSafe: return PNG_HANDLE_CHUNK_IF_SAFE;
      case vio::png::ChunkKeep::kAlways: return PNG_HANDLE_CHUNK_ALWAYS;
      default: return PNG_HANDLE_CHUNK_AS_DEFAULT;
    }
  };
  png_set_keep_unknown_chunks(p, keep(rule.default_keep), nullptr, 0);
  for (auto& [name, k] : rule.chunks) {
    png_byte n[5];
    std::memcpy(n, name, 5);
    png_set_keep_unknown_chunks(p, keep(k), n, 1);
  }
  Buf b{&data, 0};
  png_set_read_fn(p, &b, rd);
  png_read_png(p, i, PNG_TRANSFORM_IDENTITY, nullptr);
  png_unknown_chunkp u;
  const int n = png_get_unknown_chunks(p, i, &u);
  for (int k = 0; k < n; k++) {
    if (!out.empty()) out += ',';
    out.append(reinterpret_cast<const char*>(u[k].name), 4);
  }
  png_destroy_read_struct(&p, &i, nullptr);
  return out.empty() ? "-" : out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: png-golden LIBPNG_DIR\n");
    return 2;
  }
  namespace fs = std::filesystem;
  if (std::string(argv[1]) == "--diff") {
    // png-golden --diff FILE...: the cases where vio differs from libpng
    const bool set = argc > 2 && std::string(argv[2]) == "--set";
    const std::vector<Xform> cases = set ? set_xforms(argv[3]) : xforms();
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files;
    if (set) {
      generate(std::string(argv[3]) == "transform" ? "transform" : "gamma",
               [&](const Generated& g) { files.push_back({g.name, g.file}); });
    } else {
      for (int a = 2; a < argc; a++) files.push_back({argv[a], read_file(argv[a])});
    }
    for (auto& [name, data] : files) {
      for (std::size_t k = 0; k < cases.size(); k++) {
        last_error.clear();
        const Result l = read_libpng(data, &cases[k]), v = read_vio(data, cases[k]);
        if (l.hash() != v.hash()) {
          const Xform& x = cases[k];
          std::printf("%s case %zu bits %x gamma %d alpha %d/%d bg %d filler %d: libpng %s vio %s\n", name.c_str(), k,
                      x.bits, x.gamma, x.alpha, x.alpha_linear, x.background, x.filler,
                      l.error ? ("error: " + last_error).c_str() : "ok", v.error ? "error" : "ok");
        }
      }
    }
    return 0;
  }
  if (std::string(argv[1]) == "--format") {
    for (int a = 2; a < argc; a++) std::printf("%s\n", hex(format_hash(read_file(argv[a]))).c_str());
    return 0;
  }
  const fs::path root = argv[1];
  auto list = [&](const char* dir) {
    std::vector<std::string> v;
    for (auto& e : fs::directory_iterator(root / dir))
      if (e.path().extension() == ".png") v.push_back(fs::relative(e.path(), root).generic_string());
    std::sort(v.begin(), v.end());
    return v;
  };
  std::printf("# What libpng %s produces for the cases of tools/png_cases.hpp\n", PNG_LIBPNG_VER_STRING);
  std::printf("# (written by tools/png_golden.cpp; see there for the format).\n");
  std::vector<std::string> all = {"pngtest.png"};
  for (const char* d : {"contrib/pngsuite", "contrib/pngsuite/interlaced", "contrib/testpngs", "contrib/testpngs/png-3",
                        "contrib/testpngs/crashers", "contrib/testpngs/badpal"})
    for (auto& f : list(d)) all.push_back(f);
  for (auto& name : all) {
    const Result r = read_libpng(read_file((root / name).string()), nullptr);
    Fnv f;
    f.add(r.pixels.data(), r.pixels.size());
    std::printf("decode %s %s\n", name.c_str(), r.error ? "error" : f.hex().c_str());
  }
  for (const char* d : {"contrib/pngsuite", "contrib/testpngs"})
    for (auto& name : list(d))
      std::printf("xform %s %s\n", name.c_str(), xform_value({read_file((root / name).string())}, xforms()).c_str());
  for (const char* set : {"standard", "interlace", "size"}) {
    Fnv f;
    generate(set, [&](const Generated& g) {
      const Result r = read_libpng(g.file, nullptr);
      f.add(r.hash());
    });
    std::printf("valid %s %s\n", set, f.hex().c_str());
  }
  for (const char* kind : {"transform", "gamma-threshold", "gamma-transform", "gamma-sbit", "gamma-16-to-8",
                           "gamma-background", "gamma-alpha-mode", "gamma-transform-expand16",
                           "gamma-background-expand16", "gamma-alpha-mode-expand16"}) {
    std::vector<std::vector<std::uint8_t>> files;
    generate(std::string(kind) == "transform" ? "transform" : "gamma", [&](const Generated& g) { files.push_back(g.file); });
    std::printf("vxform %s %s\n", kind, xform_value(files, set_xforms(kind)).c_str());
  }
  for (const char* d : {"contrib/testpngs", "contrib/pngsuite"})
    for (auto& name : list(d)) std::printf("format %s %s\n", name.c_str(), hex(format_hash(read_file((root / name).string()))).c_str());
  const std::vector<std::uint8_t> pngtest = read_file((root / "pngtest.png").string());
  for (const UnknownRule& rule : unknown_rules())
    std::printf("unknown %s %s\n", rule.name, unknown_kept(pngtest, rule).c_str());
  return 0;
}
