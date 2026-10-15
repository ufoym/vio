// png-test: checks of the PNG codec, run by the tests in tests/libpng.cmake.
//
//   png-test check [--roundtrip] EXPECTED ROOT FILE...
//       Decodes each FILE (a path relative to ROOT) and compares the samples
//       with the value EXPECTED lists for it (decoded by libpng), or expects
//       an error where EXPECTED says "error".  --roundtrip also encodes each
//       image in both encoder modes and checks that it decodes unchanged.
//   png-test valid standard|size|interlace
//       Generates images of every color type and bit depth, writes them with
//       random filters (interlaced or not), and checks that they decode to
//       the same samples; then checks the encoder round trip on them.
//   png-test unknown FILE
//       Adds unknown chunks to FILE: ancillary ones must be skipped, a
//       critical one must be rejected.
//
// Exit status 0 if every check passes.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "vio/png.hpp"

namespace {

using vio::png::Decoder;
using vio::png::Encoder;
using vio::png::Header;
namespace zlib = vio::png::zlib;

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

std::uint64_t fnv(const std::vector<std::uint8_t>& v) {
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (std::uint8_t b : v) h = (h ^ b) * 0x100000001b3ull;
  return h;
}

// Encodes a decoded image in `mode` and checks that it decodes unchanged.
void check_roundtrip(Decoder& d, Encoder::Mode mode, const std::string& name) {
  Encoder e;
  e.mode = mode;
  e.header = d.header;
  e.header.interlace = 0;
  e.palette = d.palette;
  e.transparency = d.transparency;
  if (d.has_icc) e.icc_profile = d.icc_profile;
  std::vector<std::uint8_t> file = e.encode(d.pixels().data(), d.row_bytes());
  Decoder r;
  try {
    r.decode(file);
  } catch (const std::exception& ex) {
    failure(name + ": re-encoded file does not decode: " + ex.what());
    return;
  }
  const char* m = mode == Encoder::Mode::kFast ? "fast" : "zlib";
  if (r.header.width != d.header.width || r.header.height != d.header.height ||
      r.header.bit_depth != d.header.bit_depth || r.header.color_type != d.header.color_type)
    failure(name + ": header changed in " + m + " mode");
  else if (png_order(r) != png_order(d))
    failure(name + ": samples changed in " + m + " mode");
  else if (r.palette.size() != d.palette.size() || r.transparency != d.transparency ||
           r.icc_profile != (d.has_icc ? d.icc_profile : std::vector<std::uint8_t>{}))
    failure(name + ": PLTE, tRNS or iCCP changed in " + m + " mode");
}

int cmd_check(int argc, char** argv) {
  bool roundtrip = false;
  int i = 2;
  if (i < argc && std::strcmp(argv[i], "--roundtrip") == 0) {
    roundtrip = true;
    i++;
  }
  if (argc - i < 3) return 2;
  std::map<std::string, std::string> expected;
  {
    std::ifstream f(argv[i]);
    std::string line;
    while (std::getline(f, line)) {
      if (line.empty() || line[0] == '#') continue;
      auto sp = line.rfind(' ');
      expected[line.substr(0, sp)] = line.substr(sp + 1);
    }
  }
  const std::string root = argv[i + 1];
  for (i += 2; i < argc; i++) {
    const std::string name = argv[i];
    auto it = expected.find(name);
    if (it == expected.end()) {
      failure(name + ": no expected value");
      continue;
    }
    std::vector<std::uint8_t> file = read_file(root + "/" + name);
    Decoder d;
    std::string error;
    try {
      d.decode(file);
    } catch (const vio::Error& ex) {
      error = ex.what();
    }
    if (it->second == "error") {
      if (error.empty()) failure(name + ": decoded, expected an error");
      continue;
    }
    if (!error.empty()) {
      failure(name + ": " + error);
      continue;
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(fnv(png_order(d))));
    if (it->second != hex) {
      failure(name + ": samples differ (" + hex + ", expected " + it->second + ")");
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
// Generated images
// ---------------------------------------------------------------------------

void put_u32(std::vector<std::uint8_t>& v, std::uint32_t x) {
  for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<std::uint8_t>(x >> s));
}

void put_chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
  put_u32(out, static_cast<std::uint32_t>(data.size()));
  std::size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put_u32(out, zlib::crc32(0, out.data() + start, out.size() - start));
}

int channels(int ct) { return vio::png::num_channels(static_cast<std::uint8_t>(ct)); }

std::size_t row_bytes(const Header& h, std::uint32_t w) {
  return (static_cast<std::size_t>(w) * channels(h.color_type) * h.bit_depth + 7) / 8;
}

// Filters rows (PNG byte order) with the given filter types.
void filter_rows(std::vector<std::uint8_t>& out, const std::uint8_t* rows, std::size_t rb,
                 std::uint32_t n, unsigned bpp, std::mt19937& rng) {
  for (std::uint32_t y = 0; y < n; y++) {
    const std::uint8_t* cur = rows + y * rb;
    const std::uint8_t* up = y ? cur - rb : nullptr;
    unsigned f = rng() % 5;
    out.push_back(static_cast<std::uint8_t>(f));
    for (std::size_t i = 0; i < rb; i++) {
      unsigned a = i >= bpp ? cur[i - bpp] : 0, b = up ? up[i] : 0,
               c = up && i >= bpp ? up[i - bpp] : 0, p = 0;
      switch (f) {
        case 1: p = a; break;
        case 2: p = b; break;
        case 3: p = (a + b) / 2; break;
        case 4: p = vio::png::detail::paeth(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b),
                                            static_cast<std::uint8_t>(c)); break;
      }
      out.push_back(static_cast<std::uint8_t>(cur[i] - p));
    }
  }
}

// Writes a PNG of `img` (PNG byte order, rows of row_bytes(h, width)) with
// random filters, interlaced if h.interlace.
std::vector<std::uint8_t> write_png(const Header& h, const std::vector<std::uint8_t>& img,
                                    std::mt19937& rng) {
  std::vector<std::uint8_t> out = {137, 80, 78, 71, 13, 10, 26, 10}, ihdr;
  put_u32(ihdr, h.width);
  put_u32(ihdr, h.height);
  ihdr.insert(ihdr.end(), {h.bit_depth, h.color_type, 0, 0, h.interlace});
  put_chunk(out, "IHDR", ihdr);
  if (h.color_type == vio::png::kIndexed) {
    std::vector<std::uint8_t> plte(3u << h.bit_depth);
    for (auto& b : plte) b = static_cast<std::uint8_t>(rng());
    put_chunk(out, "PLTE", plte);
  }
  const unsigned bits = channels(h.color_type) * h.bit_depth, bpp = bits < 8 ? 1 : bits / 8;
  const std::size_t rb = row_bytes(h, h.width);
  std::vector<std::uint8_t> raw;
  if (!h.interlace) {
    filter_rows(raw, img.data(), rb, h.height, bpp, rng);
  } else {
    using namespace vio::png::detail;
    for (int p = 0; p < 7; p++) {
      if (h.width <= static_cast<std::uint32_t>(adam7_x_start[p]) ||
          h.height <= static_cast<std::uint32_t>(adam7_y_start[p]))
        continue;
      std::uint32_t pw = (h.width - adam7_x_start[p] + adam7_x_delta[p] - 1) / adam7_x_delta[p];
      std::uint32_t ph = (h.height - adam7_y_start[p] + adam7_y_delta[p] - 1) / adam7_y_delta[p];
      std::size_t prb = row_bytes(h, pw);
      std::vector<std::uint8_t> pass(prb * ph, 0);
      for (std::uint32_t y = 0; y < ph; y++)
        for (std::uint32_t x = 0; x < pw; x++) {
          std::size_t sx = adam7_x_start[p] + static_cast<std::size_t>(x) * adam7_x_delta[p];
          std::size_t sy = adam7_y_start[p] + static_cast<std::size_t>(y) * adam7_y_delta[p];
          const std::uint8_t* src = img.data() + sy * rb;
          std::uint8_t* dst = pass.data() + y * prb;
          if (bits >= 8) {
            std::memcpy(dst + x * bpp, src + sx * bpp, bpp);
          } else {
            unsigned spb = 8 / bits, s = (src[sx / spb] >> (8 - bits - (sx % spb) * bits)) & ((1u << bits) - 1);
            dst[x / spb] |= static_cast<std::uint8_t>(s << (8 - bits - (x % spb) * bits));
          }
        }
      filter_rows(raw, pass.data(), prb, ph, bpp, rng);
    }
  }
  put_chunk(out, "IDAT", zlib::compress(raw, 1 + static_cast<int>(rng() % 9)));
  put_chunk(out, "IEND", {});
  return out;
}

// Image content: random samples, smooth gradients or flat areas.
std::vector<std::uint8_t> make_image(const Header& h, std::mt19937& rng) {
  const std::size_t rb = row_bytes(h, h.width);
  std::vector<std::uint8_t> img(rb * h.height, 0);
  const unsigned kind = rng() % 3;
  const unsigned bits = channels(h.color_type) * h.bit_depth;
  for (std::uint32_t y = 0; y < h.height; y++)
    for (std::size_t i = 0; i < rb; i++) {
      std::uint8_t v = kind == 0 ? static_cast<std::uint8_t>(rng())
                     : kind == 1 ? static_cast<std::uint8_t>(i * 3 + y * 5)
                                 : static_cast<std::uint8_t>((i / 7 + y / 5) % 2 ? 0x55 : 0);
      img[y * rb + i] = v;
    }
  // padding bits after the last sample are zero
  if (const unsigned used = (h.width * bits) % 8)
    for (std::uint32_t y = 0; y < h.height; y++) img[y * rb + rb - 1] &= static_cast<std::uint8_t>(0xff00u >> used);
  return img;
}

int cmd_valid(const std::string& what) {
  static constexpr std::uint8_t formats[][2] = {
      {0, 1}, {0, 2}, {0, 4}, {0, 8}, {0, 16}, {2, 8}, {2, 16}, {3, 1}, {3, 2},
      {3, 4}, {3, 8}, {4, 8}, {4, 16}, {6, 8}, {6, 16}};
  std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes;
  if (what == "size") {
    for (std::uint32_t w = 1; w <= 16; w++)
      for (std::uint32_t h = 1; h <= 16; h++) sizes.push_back({w, h});
  } else {
    sizes = {{1, 1}, {2, 3}, {7, 5}, {8, 8}, {15, 31}, {16, 16}, {17, 9},  {31, 2},
             {32, 32}, {33, 17}, {64, 3}, {100, 37}, {129, 65}, {257, 4}, {1023, 3}};
  }
  std::mt19937 rng(12345);
  int images = 0;
  for (auto [ct, bd] : formats)
    for (auto [w, hh] : sizes)
      for (int il = 0; il < 2; il++) {
        if (what == "interlace" && !il) continue;
        if (what == "standard" && il) continue;
        Header h;
        h.width = w;
        h.height = hh;
        h.color_type = ct;
        h.bit_depth = bd;
        h.interlace = static_cast<std::uint8_t>(il);
        std::vector<std::uint8_t> img = make_image(h, rng);
        std::vector<std::uint8_t> file = write_png(h, img, rng);
        char name[64];
        std::snprintf(name, sizeof(name), "ct%d bd%d %ux%u%s", ct, bd, w, hh, il ? " interlaced" : "");
        Decoder d;
        try {
          d.decode(file);
        } catch (const std::exception& ex) {
          failure(std::string(name) + ": " + ex.what());
          continue;
        }
        if (png_order(d) != img) {
          failure(std::string(name) + ": samples differ");
          continue;
        }
        check_roundtrip(d, Encoder::Mode::kFast, name);
        check_roundtrip(d, Encoder::Mode::kZlib, name);
        images++;
      }
  std::printf("%d images\n", images);
  return 0;
}

int cmd_unknown(const std::string& path) {
  std::vector<std::uint8_t> file = read_file(path);
  if (file.size() < 33) return 2;
  Decoder ref;
  ref.decode(file);
  const std::vector<std::uint8_t> samples = png_order(ref);
  // after IHDR (8 + 25 bytes) and before IEND
  auto with_chunk = [&](const char* type, bool at_end) {
    std::vector<std::uint8_t> chunk;
    put_chunk(chunk, type, {1, 2, 3, 4, 5});
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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: png-test check|valid|unknown ...\n");
    return 2;
  }
  const std::string cmd = argv[1];
  int status = 2;
  if (cmd == "check") status = cmd_check(argc, argv);
  else if (cmd == "valid") status = cmd_valid(argv[2]);
  else if (cmd == "unknown") status = cmd_unknown(argv[2]);
  if (status) return status;
  if (failures) std::fprintf(stderr, "%d failures\n", failures);
  return failures ? 1 : 0;
}
