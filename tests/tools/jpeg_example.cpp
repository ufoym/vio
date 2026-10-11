// jpeg-example - demonstrates vio's JPEG encoder and decoder APIs.  Equivalent to
// libjpeg-turbo's example.c (same command line and output), written against
// vio::jpeg::Encoder/Decoder.
//
//   jpeg-example compress [-precision 8|12] [-quality N] output.jpg
//   jpeg-example decompress input.jpg output.ppm
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <vector>

#include "cli.hpp"
#include "vio/jpeg/decoder.hpp"
#include "vio/jpeg/encoder.hpp"

namespace {

constexpr unsigned WIDTH = 640, HEIGHT = 480;

template <class T>
void fill_pattern(std::vector<T>& image, int maxsample) {
  image.resize(static_cast<std::size_t>(WIDTH) * 3 * HEIGHT);
  for (unsigned row = 0; row < HEIGHT; row++)
    for (unsigned col = 0; col < WIDTH; col++) {
      T* p = &image[(static_cast<std::size_t>(row) * WIDTH + col) * 3];
      p[0] = static_cast<T>((col * (maxsample + 1) / WIDTH) % (maxsample + 1));
      p[1] = static_cast<T>((row * (maxsample + 1) / HEIGHT) % (maxsample + 1));
      p[2] = static_cast<T>((row * (maxsample + 1) / HEIGHT + col * (maxsample + 1) / WIDTH) %
                            (maxsample + 1));
    }
}

template <class T>
std::vector<std::uint8_t> compress_pattern(int quality, int precision) {
  vio::jpeg::Encoder enc;
  enc.set_image(WIDTH, HEIGHT, 3, vio::ColorSpace::RGB, precision)
      .set_quality(quality, true)
      .set_sampling(0, 1, 1);  // 4:4:4
  enc.start();
  std::vector<T> image;
  fill_pattern(image, (1 << precision) - 1);
  while (enc.next_scanline() < HEIGHT) {
    T* row = &image[static_cast<std::size_t>(enc.next_scanline()) * WIDTH * 3];
    enc.write_scanlines(&row, 1);
  }
  return enc.finish();
}

int write_JPEG_file(const char* filename, int quality, int precision) {
  std::FILE* outfile = std::fopen(filename, "wb");
  if (!outfile) {
    std::fprintf(stderr, "Output file write error --- out of disk space?\n");
    return EXIT_FAILURE;
  }
  std::vector<std::uint8_t> jpeg = precision == 12 ? compress_pattern<std::int16_t>(quality, 12)
                                                   : compress_pattern<std::uint8_t>(quality, 8);
  cli::write_file(outfile, jpeg);
  std::fclose(outfile);
  return 0;
}

template <class T>
void read_rows(vio::jpeg::Decoder& dec, std::FILE* outfile) {
  std::size_t stride = static_cast<std::size_t>(dec.output_width()) * dec.output_components();
  std::vector<T> buffer(stride);
  std::vector<std::uint8_t> bytes(stride * sizeof(T));
  while (dec.output_scanline() < dec.output_height()) {
    T* row = buffer.data();
    dec.read_scanlines(&row, 1);
    if constexpr (sizeof(T) == 1) {
      std::fwrite(buffer.data(), 1, stride, outfile);
    } else {
      // PPM stores 16-bit samples most-significant byte first.
      for (std::size_t i = 0; i < stride; i++) {
        unsigned v = static_cast<std::uint16_t>(buffer[i]);
        bytes[2 * i] = static_cast<std::uint8_t>(v >> 8);
        bytes[2 * i + 1] = static_cast<std::uint8_t>(v);
      }
      std::fwrite(bytes.data(), 1, bytes.size(), outfile);
    }
  }
}

int read_JPEG_file(const char* infilename, const char* outfilename) {
  std::vector<std::uint8_t> data;
  if (!cli::read_file(infilename, data)) {
    std::fprintf(stderr, "can't open %s\n", infilename);
    return 0;
  }
  std::FILE* outfile = std::fopen(outfilename, "wb");
  if (!outfile) {
    std::fprintf(stderr, "can't open %s\n", outfilename);
    return 0;
  }
  try {
    vio::jpeg::Decoder dec(data);
    auto h = dec.header();
    std::fprintf(outfile, "P6\n%u %u\n%d\n", h.width, h.height, h.precision == 12 ? 4095 : 255);
    dec.start();
    if (dec.output_precision() == 12) read_rows<std::int16_t>(dec, outfile);
    else read_rows<std::uint8_t>(dec, outfile);
    dec.finish();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    std::fclose(outfile);
    return 0;
  }
  std::fclose(outfile);
  return 1;
}

[[noreturn]] void usage(const char* progname) {
  std::fprintf(stderr, "usage: %s compress [switches] outputfile[.jpg]\n", progname);
  std::fprintf(stderr, "       %s decompress inputfile[.jpg] outputfile[.ppm]\n", progname);
  std::fprintf(stderr, "Switches (names may be abbreviated):\n");
  std::fprintf(stderr, "  -precision N   Create JPEG file with N-bit data precision\n");
  std::fprintf(stderr, "                 (N is 8 or 12; default is 8)\n");
  std::fprintf(stderr, "  -quality N     Compression quality (0..100; 5-95 is most useful range,\n");
  std::fprintf(stderr, "                 default is 75)\n");
  std::exit(EXIT_FAILURE);
}

}  // namespace

int main(int argc, char** argv) {
  int quality = 75, data_precision = 8;
  enum { COMPRESS, DECOMPRESS } mode;
  const char* filename = nullptr;
  if (argc < 3) usage(argv[0]);
  if (!strcasecmp(argv[1], "compress")) mode = COMPRESS;
  else if (!strcasecmp(argv[1], "decompress")) mode = DECOMPRESS;
  else usage(argv[0]);
  int argn;
  for (argn = 2; argn < argc; argn++) {
    const char* arg = argv[argn];
    if (*arg != '-') {
      filename = arg;
      break;
    }
    arg++;
    if (!strncasecmp(arg, "p", 1)) {
      if (++argn >= argc) usage(argv[0]);
      if (std::sscanf(argv[argn], "%d", &data_precision) < 1 ||
          (data_precision != 8 && data_precision != 12))
        usage(argv[0]);
    } else if (!strncasecmp(arg, "q", 1)) {
      if (++argn >= argc) usage(argv[0]);
      if (std::sscanf(argv[argn], "%d", &quality) < 1 || quality < 0 || quality > 100)
        usage(argv[0]);
      if (quality < 1) quality = 1;
    }
  }
  if (!filename) usage(argv[0]);
  if (mode == COMPRESS) return write_JPEG_file(filename, quality, data_precision);
  if (argc - argn < 2) usage(argv[0]);
  read_JPEG_file(argv[argn], argv[argn + 1]);
  return 0;
}
