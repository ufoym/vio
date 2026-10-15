// PNG throughput benchmark over a corpus of PNG files.
//
//   png-bench <impl> <mode> [reps] < file-list
//
// impl: vio, or fpnge when built with VIO_FPNGE_SOURCE_DIR (x86 only).
// mode: decode   decode to the PNG's own sample layout (vio only)
//       encode   decode to 8-bit RGB or RGBA (RGBA when the file has alpha or
//                tRNS), then time encoding that buffer at the default effort
//
// Prints one line per file: "<impl> <mode> <path> <megapixels> <seconds>
// <bytes>", seconds being the fastest of the repetitions and bytes the size
// of the decoded image (decode) or of the encoded file (encode).  Encoded
// files are checked by decoding them again.  tests/png-bench/png_bench.py
// aggregates the lines of several implementations.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "vio/png.hpp"

#ifdef VIO_BENCH_FPNGE
#include "fpnge.h"
#endif

namespace {

using Clock = std::chrono::steady_clock;

std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// 8-bit RGB or RGBA pixels of a PNG (16-bit samples keep their high byte).
struct Rgb8 {
  std::uint32_t width = 0, height = 0;
  int channels = 3;
  std::vector<std::uint8_t> px;
};

Rgb8 to_rgb8(std::span<const std::uint8_t> file) {
  vio::png::Decoder d;
  d.decode(file);
  const vio::png::Header& h = d.header;
  // tRNS, if any
  std::vector<std::uint8_t> trns;
  for (std::size_t p = 8; p + 12 <= file.size();) {
    std::uint32_t len = std::uint32_t(file[p]) << 24 | std::uint32_t(file[p + 1]) << 16 |
                        std::uint32_t(file[p + 2]) << 8 | file[p + 3];
    if (std::memcmp(&file[p + 4], "tRNS", 4) == 0 && p + 12 + len <= file.size())
      trns.assign(&file[p + 8], &file[p + 8] + len);
    if (std::memcmp(&file[p + 4], "IDAT", 4) == 0) break;
    p += 12 + std::size_t(len);
  }
  int ct = h.color_type, bd = h.bit_depth;
  bool alpha = ct == vio::png::kGrayAlpha || ct == vio::png::kTruecolorAlpha || !trns.empty();
  Rgb8 r;
  r.width = h.width;
  r.height = h.height;
  r.channels = alpha ? 4 : 3;
  r.px.resize(std::size_t(h.width) * h.height * r.channels);
  std::uint8_t* o = r.px.data();
  const int nc = vio::png::num_channels(h.color_type);
  for (std::uint32_t y = 0; y < h.height; y++) {
    const std::uint8_t* row = d.row(y);
    for (std::uint32_t x = 0; x < h.width; x++) {
      // raw samples of this pixel, at their original depth
      unsigned s[4] = {};
      for (int c = 0; c < nc; c++) {
        std::size_t i = std::size_t(x) * nc + c;
        if (bd == 16) {
          std::uint16_t v;
          std::memcpy(&v, row + 2 * i, 2);
          s[c] = v;
        } else if (bd == 8) {
          s[c] = row[i];
        } else {
          s[c] = (row[i * bd / 8] >> (8 - bd - (i * bd) % 8)) & ((1u << bd) - 1);
        }
      }
      auto to8 = [&](unsigned v) -> std::uint8_t {
        if (bd == 16) return static_cast<std::uint8_t>(v >> 8);
        return static_cast<std::uint8_t>(v * 255 / ((1u << bd) - 1));
      };
      std::uint8_t rgba[4];
      unsigned a = 255;
      switch (ct) {
        case vio::png::kIndexed: {
          const auto& e = d.palette[s[0] < d.palette.size() ? s[0] : 0];
          rgba[0] = e.red, rgba[1] = e.green, rgba[2] = e.blue;
          if (s[0] < trns.size()) a = trns[s[0]];
          break;
        }
        case vio::png::kGray:
        case vio::png::kGrayAlpha:
          rgba[0] = rgba[1] = rgba[2] = to8(s[0]);
          if (ct == vio::png::kGrayAlpha) a = to8(s[1]);
          else if (trns.size() >= 2 && s[0] == (unsigned(trns[0]) << 8 | trns[1])) a = 0;
          break;
        default:
          rgba[0] = to8(s[0]), rgba[1] = to8(s[1]), rgba[2] = to8(s[2]);
          if (ct == vio::png::kTruecolorAlpha) a = to8(s[3]);
          else if (trns.size() >= 6 && s[0] == (unsigned(trns[0]) << 8 | trns[1]) &&
                   s[1] == (unsigned(trns[2]) << 8 | trns[3]) &&
                   s[2] == (unsigned(trns[4]) << 8 | trns[5]))
            a = 0;
      }
      rgba[3] = static_cast<std::uint8_t>(a);
      std::memcpy(o, rgba, r.channels);
      o += r.channels;
    }
  }
  return r;
}

std::vector<std::uint8_t> encode_vio(const Rgb8& img) {
  vio::png::Encoder e;
  e.header.width = img.width;
  e.header.height = img.height;
  e.header.bit_depth = 8;
  e.header.color_type = img.channels == 4 ? vio::png::kTruecolorAlpha : vio::png::kTruecolor;
  e.begin();
  const std::size_t stride = std::size_t(img.width) * img.channels;
  for (std::uint32_t y = 0; y < img.height; y++) e.write_row(img.px.data() + y * stride);
  return e.finish();
}

#ifdef VIO_BENCH_FPNGE
std::vector<std::uint8_t> encode_fpnge(const Rgb8& img) {
  FPNGEOptions opt;
  FPNGEFillOptions(&opt, FPNGE_COMPRESS_LEVEL_DEFAULT, FPNGE_CICP_NONE);
  std::vector<std::uint8_t> out(FPNGEOutputAllocSize(1, img.channels, img.width, img.height));
  std::size_t n = FPNGEEncode(1, img.channels, img.px.data(), img.width,
                              std::size_t(img.width) * img.channels, img.height, out.data(), &opt);
  out.resize(n);
  return out;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <impl> <decode|encode> [reps] < file-list\n", argv[0]);
    return 1;
  }
  const std::string impl = argv[1], mode = argv[2];
  const int reps = argc > 3 ? std::atoi(argv[3]) : 5;
  std::string path;
  int failures = 0;
  while (std::getline(std::cin, path)) {
    if (path.empty()) continue;
    std::vector<std::uint8_t> file = read_file(path);
    double best = 1e30, mp = 0;
    std::size_t bytes = 0;
    try {
      if (mode == "hash") {
        // FNV-1a of the decoded samples (16-bit ones big-endian)
        vio::png::Decoder d;
        d.decode(file);
        auto px = d.pixels();
        bool swap = d.header.bit_depth == 16;
        std::uint64_t h = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < px.size(); i++) {
          std::uint8_t b = px[swap ? i ^ 1 : i];
          h = (h ^ b) * 0x100000001b3ull;
        }
        std::printf("%s %016llx\n", path.c_str(), static_cast<unsigned long long>(h));
        continue;
      }
      if (mode == "decode") {
        if (impl != "vio") throw std::runtime_error("decode: vio only");
        for (int r = 0; r < reps; r++) {
          auto t0 = Clock::now();
          vio::png::Decoder d;
          d.decode(file);
          double s = std::chrono::duration<double>(Clock::now() - t0).count();
          best = std::min(best, s);
          mp = double(d.header.width) * d.header.height * 1e-6;
          bytes = d.pixels().size();
        }
      } else {
        Rgb8 img = to_rgb8(file);
        mp = double(img.width) * img.height * 1e-6;
        std::vector<std::uint8_t> out;
        for (int r = 0; r < reps; r++) {
          auto t0 = Clock::now();
#ifdef VIO_BENCH_FPNGE
          if (impl == "fpnge") out = encode_fpnge(img);
          else
#endif
          out = encode_vio(img);
          double s = std::chrono::duration<double>(Clock::now() - t0).count();
          best = std::min(best, s);
        }
        bytes = out.size();
        if (to_rgb8(out).px != img.px) throw std::runtime_error("round trip mismatch");
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
      failures++;
      continue;
    }
    std::printf("%s %s %s %.6f %.9f %zu\n", impl.c_str(), mode.c_str(), path.c_str(), mp, best,
                bytes);
    std::fflush(stdout);
  }
  return failures ? 2 : 0;
}
