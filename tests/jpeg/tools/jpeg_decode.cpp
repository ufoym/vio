// jpeg-decode - decompress a JPEG file to an image file (command-line compatible
// with libjpeg-turbo's djpeg).
#include <cstring>
#include <memory>

#include "cli.hpp"
#include "vio/bmp.hpp"
#include "vio/gif.hpp"
#include "vio/png.hpp"
#include "vio/ppm.hpp"
#include "vio/targa.hpp"
#include "colormap.hpp"
#include "vio/jpeg/decoder.hpp"

using namespace vio;
using cli::keymatch;

namespace {

enum Format { FMT_BMP, FMT_GIF, FMT_GIF0, FMT_OS2, FMT_PNG, FMT_PPM, FMT_TARGA };

const char* progname = "jpeg-decode";

struct Options {
  Format fmt = FMT_PPM;
  const char* icc_filename = nullptr;
  bool noicc = false;
  unsigned max_scans = 0;
  const char* outfilename = nullptr;
  bool nooverwrite = false;
  bool memsrc = false, report = false, strict = false;
  bool skip = false, crop = false;
  unsigned skip_start = 0, skip_end = 0;
  unsigned crop_x = 0, crop_y = 0, crop_width = 0, crop_height = 0;
  const char* map_file = nullptr;
  int trace_level = 0;
};

[[noreturn]] void usage() {
  std::fprintf(stderr, "usage: %s [switches] [inputfile]\n", progname);
  std::fprintf(stderr, "Switches (names may be abbreviated):\n");
  std::fprintf(stderr, "  -colors N      Reduce image to no more than N colors [legacy feature]\n");
  std::fprintf(stderr, "  -fast          Low-quality processing [legacy feature]\n");
  std::fprintf(stderr, "  -grayscale     Force grayscale output\n");
  std::fprintf(stderr, "  -rgb           Force RGB output\n");
  std::fprintf(stderr, "  -rgb565        Force RGB565 output\n");
  std::fprintf(stderr, "  -scale M/N     Scale output image by fraction M/N, eg, 1/8\n");
  std::fprintf(stderr, "  -bmp           Select BMP output format (Windows style)\n");
  std::fprintf(stderr, "  -gif           Select GIF output format (LZW-compressed) [legacy feature]\n");
  std::fprintf(stderr, "  -gif0          Select GIF output format (uncompressed) [legacy feature]\n");
  std::fprintf(stderr, "  -os2           Select BMP output format (OS/2 style) [legacy feature]\n");
  std::fprintf(stderr, "  -png           Select PNG output format\n");
  std::fprintf(stderr, "  -pnm           Select PBMPLUS (PPM/PGM) output format (default)\n");
  std::fprintf(stderr, "  -targa         Select Targa output format [legacy feature]\n");
  std::fprintf(stderr, "Switches for advanced users:\n");
  std::fprintf(stderr, "  -precision 12  Force 12-bit-per-sample output [useful for shadow recovery]\n");
  std::fprintf(stderr, "  -dct int       Use accurate integer DCT method (default)\n");
  std::fprintf(stderr, "  -dct fast      Use less accurate integer DCT method [legacy feature]\n");
  std::fprintf(stderr, "  -dct float     Use floating-point DCT method [legacy feature]\n");
  std::fprintf(stderr, "  -dither fs     Use Floyd-Steinberg dithering when quantizing colors (default)\n");
  std::fprintf(stderr, "  -dither none   Don't use dithering when quantizing colors [legacy feature]\n");
  std::fprintf(stderr, "  -dither ordered  Use ordered dithering when quantizing colors\n");
  std::fprintf(stderr, "  -icc FILE      Extract ICC profile to FILE\n");
  std::fprintf(stderr, "  -noicc         Do not transfer ICC profile to PNG output file\n");
  std::fprintf(stderr, "  -map FILE      Quantize to colors used in named image file [legacy feature]\n");
  std::fprintf(stderr, "  -nosmooth      Use faster, lower-quality upsampling\n");
  std::fprintf(stderr, "  -onepass       Use 1-pass color quantization (low quality) [legacy feature]\n");
  std::fprintf(stderr, "  -maxmemory N   Maximum memory to use (in kbytes)\n");
  std::fprintf(stderr, "  -maxscans N    Maximum number of scans to allow in input file\n");
  std::fprintf(stderr, "  -outfile name  Specify name for output file\n");
  std::fprintf(stderr, "  -nooverwrite   Don't overwrite output file if it exists\n");
  std::fprintf(stderr, "  -memsrc        Load input file into memory before decompressing\n");
  std::fprintf(stderr, "  -report        Report decompression progress\n");
  std::fprintf(stderr, "  -skip Y0,Y1    Decompress all rows except those between Y0 and Y1 (inclusive)\n");
  std::fprintf(stderr, "  -crop WxH+X+Y  Decompress only a rectangular subregion of the image\n");
  std::fprintf(stderr, "                 [requires PNG, PBMPLUS (PPM/PGM), Targa, or GIF output format]\n");
  std::fprintf(stderr, "  -strict        Treat all warnings as fatal\n");
  std::fprintf(stderr, "  -verbose  or  -debug   Emit debug output\n");
  std::fprintf(stderr, "  -version       Print version information and exit\n");
  std::exit(EXIT_FAILURE);
}

// Parses switches; when `dec` is non-null, decompression parameters are
// applied to it.  Returns the index of the first file name argument.
int parse_switches(Options& o, jpeg::Decoder* dec, int argc, char** argv) {
  o = Options();
  int argn;
  for (argn = 1; argn < argc; argn++) {
    char* arg = argv[argn];
    if (*arg != '-') break;
    arg++;
    if (keymatch(arg, "bmp", 1)) {
      o.fmt = FMT_BMP;
    } else if (keymatch(arg, "colors", 1) || keymatch(arg, "colours", 1) ||
               keymatch(arg, "quantize", 1) || keymatch(arg, "quantise", 1)) {
      int val;
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%d", &val) != 1) usage();
      if (dec) dec->set_desired_colors(val).set_quantize_colors(true);
    } else if (keymatch(arg, "dct", 2)) {
      if (++argn >= argc) usage();
      jpeg::DctMethod m;
      if (keymatch(argv[argn], "int", 1)) m = jpeg::DctMethod::IntSlow;
      else if (keymatch(argv[argn], "fast", 2)) m = jpeg::DctMethod::IntFast;
      else if (keymatch(argv[argn], "float", 2)) m = jpeg::DctMethod::Float;
      else usage();
      if (dec) dec->set_dct_method(m);
    } else if (keymatch(arg, "dither", 2)) {
      if (++argn >= argc) usage();
      jpeg::DitherMode m;
      if (keymatch(argv[argn], "fs", 2)) m = jpeg::DitherMode::FloydSteinberg;
      else if (keymatch(argv[argn], "none", 2)) m = jpeg::DitherMode::None;
      else if (keymatch(argv[argn], "ordered", 2)) m = jpeg::DitherMode::Ordered;
      else usage();
      if (dec) dec->set_dither(m);
    } else if (keymatch(arg, "debug", 1) || keymatch(arg, "verbose", 1)) {
      static bool printed_version = false;
      if (!printed_version) {
        std::fprintf(stderr, "vio djpeg\n");
        printed_version = true;
      }
      o.trace_level++;
    } else if (keymatch(arg, "version", 4)) {
      std::fprintf(stderr, "vio djpeg\n");
      std::exit(EXIT_SUCCESS);
    } else if (keymatch(arg, "fast", 1)) {
      if (dec) {
        dec->set_two_pass_quantize(false).set_dither(jpeg::DitherMode::Ordered);
        if (!dec->quantize_colors()) dec->set_desired_colors(216);
        dec->set_dct_method(jpeg::DctMethod::IntFast).set_fancy_upsampling(false);
      }
    } else if (keymatch(arg, "gif", 1)) {
      o.fmt = FMT_GIF;
    } else if (keymatch(arg, "gif0", 4)) {
      o.fmt = FMT_GIF0;
    } else if (keymatch(arg, "grayscale", 2) || keymatch(arg, "greyscale", 2)) {
      if (dec) dec->set_output_color(ColorSpace::Gray);
    } else if (keymatch(arg, "rgb", 2)) {
      if (dec) dec->set_output_color(ColorSpace::RGB);
    } else if (keymatch(arg, "rgb565", 2)) {
      if (dec) dec->set_output_color(ColorSpace::RGB565);
    } else if (keymatch(arg, "icc", 1)) {
      if (++argn >= argc) usage();
      o.icc_filename = argv[argn];
    } else if (keymatch(arg, "noicc", 3)) {
      o.noicc = true;
    } else if (keymatch(arg, "map", 3)) {
      if (++argn >= argc) usage();
      o.map_file = argv[argn];
    } else if (keymatch(arg, "maxmemory", 3)) {
      long lval;
      char ch = 'x';
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%ld%c", &lval, &ch) < 1) usage();
    } else if (keymatch(arg, "maxscans", 4)) {
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%u", &o.max_scans) != 1) usage();
    } else if (keymatch(arg, "nosmooth", 3)) {
      if (dec) dec->set_fancy_upsampling(false);
    } else if (keymatch(arg, "onepass", 3)) {
      if (dec) dec->set_two_pass_quantize(false);
    } else if (keymatch(arg, "os2", 3)) {
      o.fmt = FMT_OS2;
    } else if (keymatch(arg, "outfile", 4)) {
      if (++argn >= argc) usage();
      o.outfilename = argv[argn];
    } else if (keymatch(arg, "nooverwrite", 3)) {
      o.nooverwrite = true;
    } else if (keymatch(arg, "memsrc", 2)) {
      o.memsrc = true;
    } else if (keymatch(arg, "pnm", 1) || keymatch(arg, "ppm", 1)) {
      o.fmt = FMT_PPM;
    } else if (keymatch(arg, "png", 3)) {
      o.fmt = FMT_PNG;
    } else if (keymatch(arg, "precision", 2)) {
      int val;
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%d", &val) != 1) usage();
      if (val != 12) usage();
      if (dec) dec->set_output_precision(val);
    } else if (keymatch(arg, "report", 2)) {
      o.report = true;
    } else if (keymatch(arg, "scale", 1)) {
      unsigned num, denom;
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%u/%u", &num, &denom) != 2) usage();
      if (dec) dec->set_scale(num, denom);
    } else if (keymatch(arg, "skip", 2)) {
      int s = -1, e = -1;
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%d,%d", &s, &e) != 2 || s < 0 || e < 0 || s > e) usage();
      o.skip = true;
      o.skip_start = static_cast<unsigned>(s);
      o.skip_end = static_cast<unsigned>(e);
    } else if (keymatch(arg, "crop", 2)) {
      int w = -1, h = -1, x = -1, y = -1;
      char c;
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%d%c%d+%d+%d", &w, &c, &h, &x, &y) != 5 ||
          (c != 'X' && c != 'x') || w < 1 || h < 1 || x < 0 || y < 0)
        usage();
      o.crop = true;
      o.crop_width = static_cast<unsigned>(w);
      o.crop_height = static_cast<unsigned>(h);
      o.crop_x = static_cast<unsigned>(x);
      o.crop_y = static_cast<unsigned>(y);
    } else if (keymatch(arg, "strict", 2)) {
      o.strict = true;
    } else if (keymatch(arg, "targa", 1)) {
      o.fmt = FMT_TARGA;
    } else {
      usage();
    }
  }
  return argn;
}

template <class T>
std::unique_ptr<vio::ImageWriter<T>> make_destination(const Options& o, int precision) {
  switch (o.fmt) {
    case FMT_BMP:
    case FMT_OS2:
      if constexpr (std::is_same_v<T, std::uint8_t>) {
        auto d = std::make_unique<vio::bmp::Writer>();
        d->is_os2 = o.fmt == FMT_OS2;
        return d;
      }
      VIO_FAIL("Unsupported JPEG data precision %d", precision);
    case FMT_GIF:
    case FMT_GIF0:
      if constexpr (!std::is_same_v<T, std::uint16_t>) {
        if (o.fmt == FMT_GIF0 && !std::is_same_v<T, std::uint8_t>)
          VIO_FAIL("Unsupported JPEG data precision %d", precision);
        auto d = std::make_unique<vio::gif::Writer<T>>();
        d->is_lzw = o.fmt == FMT_GIF;
        return d;
      }
      VIO_FAIL("Unsupported JPEG data precision %d", precision);
    case FMT_PNG:
      return std::make_unique<vio::png::Writer<T>>();
    case FMT_PPM:
      return std::make_unique<vio::ppm::Writer<T>>();
    case FMT_TARGA:
      if constexpr (std::is_same_v<T, std::uint8_t>) return std::make_unique<vio::targa::Writer>();
      VIO_FAIL("Unsupported JPEG data precision %d", precision);
  }
  VIO_FAIL("Unsupported output file format");
}

template <class T>
void read_rows(jpeg::Decoder& dec, vio::ImageWriter<T>& dst, std::vector<T>& buf,
               unsigned rows_per_call, unsigned until) {
  std::size_t stride = static_cast<std::size_t>(dec.output_width()) * dec.output_components();
  if (buf.size() < stride * rows_per_call) buf.resize(stride * rows_per_call);
  std::vector<T*> ptrs(rows_per_call);
  for (unsigned i = 0; i < rows_per_call; i++) ptrs[i] = buf.data() + stride * i;
  while (dec.output_scanline() < until) {
    unsigned n = dec.read_scanlines<T>(ptrs.data(), 1);
    dst.write_rows(ptrs.data(), n);
  }
}

template <class T>
std::vector<std::uint8_t> decompress(const Options& o, jpeg::Decoder& dec,
                                     const std::vector<std::uint8_t>& icc) {
  auto dst = make_destination<T>(o, dec.output_precision());
  if (o.crop && !dst->supports_partial()) VIO_FAIL("Unsupported output file format");
  if (o.fmt == FMT_PNG && !o.noicc && !icc.empty()) dst->set_icc_profile(icc);
  dec.start();
  auto describe = [&](unsigned height) {
    dst->width = dec.output_width();
    dst->height = height;
    dst->color = dec.output_color();
    dst->components = dec.out_color_components();
    dst->precision = dec.output_precision();
    if (dec.quantize_colors()) dst->colormap = dec.colormap();
    auto h = dec.header();
    dst->density_unit = static_cast<std::uint8_t>(h.density_unit);
    dst->x_density = static_cast<std::uint16_t>(h.x_density);
    dst->y_density = static_cast<std::uint16_t>(h.y_density);
  };
  std::vector<T> buf;
  if (o.skip) {
    if (o.skip_end > dec.output_height() - 1) {
      std::fprintf(stderr, "%s: skip region exceeds image height %u\n", progname,
                   dec.output_height());
      std::exit(EXIT_FAILURE);
    }
    describe(dec.output_height() - (o.skip_end - o.skip_start + 1));
    dst->start();
    read_rows<T>(dec, *dst, buf, 1, o.skip_start);
    unsigned n = o.skip_end - o.skip_start + 1;
    unsigned tmp = dec.skip_scanlines(n);
    if (tmp != n) {
      std::fprintf(stderr, "%s: jpeg_skip_scanlines() returned %u rather than %u\n", progname,
                   tmp, n);
      std::exit(EXIT_FAILURE);
    }
    read_rows<T>(dec, *dst, buf, 1, dec.output_height());
  } else if (o.crop) {
    if (static_cast<unsigned long long>(o.crop_x) + o.crop_width > dec.output_width() ||
        static_cast<unsigned long long>(o.crop_y) + o.crop_height > dec.output_height()) {
      std::fprintf(stderr, "%s: crop dimensions exceed image dimensions %u x %u\n", progname,
                   dec.output_width(), dec.output_height());
      std::exit(EXIT_FAILURE);
    }
    unsigned cx = o.crop_x, cw = o.crop_width;
    dec.crop_scanline<T>(cx, cw);
    describe(o.crop_height);
    dst->start();
    unsigned tmp = dec.skip_scanlines(o.crop_y);
    if (tmp != o.crop_y) {
      std::fprintf(stderr, "%s: jpeg_skip_scanlines() returned %u rather than %u\n", progname,
                   tmp, o.crop_y);
      std::exit(EXIT_FAILURE);
    }
    read_rows<T>(dec, *dst, buf, 1, o.crop_y + o.crop_height);
    unsigned rest = dec.output_height() - o.crop_y - o.crop_height;
    tmp = dec.skip_scanlines(rest);
    if (tmp != rest) {
      std::fprintf(stderr, "%s: jpeg_skip_scanlines() returned %u rather than %u\n", progname,
                   tmp, rest);
      std::exit(EXIT_FAILURE);
    }
  } else {
    describe(dec.output_height());
    dst->start();
    read_rows<T>(dec, *dst, buf, 1, dec.output_height());
  }
  std::vector<std::uint8_t> out = dst->finish();
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argv[0] && argv[0][0]) progname = argv[0];
  Options o;
  int file_index = parse_switches(o, nullptr, argc, argv);
  if (file_index < argc - 1) {
    std::fprintf(stderr, "%s: only one input file\n", progname);
    usage();
  }
  std::vector<std::uint8_t> input;
  if (file_index < argc) {
    if (!cli::read_file(argv[file_index], input)) {
      std::fprintf(stderr, "%s: can't open %s\n", progname, argv[file_index]);
      return EXIT_FAILURE;
    }
  } else {
    input = cli::read_stream(stdin);
  }
  std::FILE* output_file = stdout;
  if (o.outfilename) {
    if (o.nooverwrite) {
      if (std::FILE* f = std::fopen(o.outfilename, "rb")) {
        std::fclose(f);
        std::fprintf(stderr, "%s: can't open %s; file exists\n", progname, o.outfilename);
        return EXIT_FAILURE;
      }
    }
    if ((output_file = std::fopen(o.outfilename, "wb")) == nullptr) {
      std::fprintf(stderr, "%s: can't open %s\n", progname, o.outfilename);
      return EXIT_FAILURE;
    }
  }
  if (o.memsrc) std::fprintf(stderr, "Compressed size:  %lu bytes\n",
                             static_cast<unsigned long>(input.size()));

  jpeg::Decoder dec;
  bool first_warning = true;
  dec.diagnostics().on_warning = [&](const std::string& msg) {
    if (o.strict) throw Error(msg);
    if (first_warning || o.trace_level >= 3) std::fprintf(stderr, "%s\n", msg.c_str());
    first_warning = false;
  };
  try {
    if (o.fmt == FMT_PNG || o.icc_filename) dec.save_markers(jpeg::M_APP2, 0xFFFF);
    dec.open(input);
    parse_switches(o, &dec, argc, argv);
    if (o.fmt == FMT_GIF || o.fmt == FMT_GIF0) {
      // GIF output forces quantization of color or >8-bit images.
      dec.calc_output_dimensions();
      auto& st = dec.core().st;
      if (st.out_color_space != ColorSpace::Gray && st.out_color_space != ColorSpace::RGB)
        VIO_FAIL("GIF output must be grayscale or RGB");
      if (st.out_color_space != ColorSpace::Gray || st.data_precision > 8) {
        st.quantize_colors = true;
        if (st.desired_number_of_colors > 256) st.desired_number_of_colors = 256;
      }
    }
    if (o.map_file) {
      std::vector<std::uint8_t> map;
      if (!cli::read_file(o.map_file, map)) {
        std::fprintf(stderr, "%s: can't open %s\n", progname, o.map_file);
        return EXIT_FAILURE;
      }
      dec.set_colormap(cli::read_color_map(map, dec.output_precision()));
    }
    std::vector<std::uint8_t> icc = dec.icc_profile();
    std::vector<std::uint8_t> out;
    int p = dec.output_precision();
    if (p <= 8) out = decompress<std::uint8_t>(o, dec, icc);
    else if (p <= 12) out = decompress<std::int16_t>(o, dec, icc);
    else out = decompress<std::uint16_t>(o, dec, icc);

    if (o.icc_filename) {
      std::FILE* f = std::fopen(o.icc_filename, "wb");
      if (!f) {
        std::fprintf(stderr, "%s: can't open %s\n", progname, o.icc_filename);
        return EXIT_FAILURE;
      }
      if (!icc.empty()) {
        if (std::fwrite(icc.data(), icc.size(), 1, f) < 1) {
          std::fprintf(stderr, "%s: can't read ICC profile from %s\n", progname,
                       o.icc_filename);
          std::fclose(f);
          return EXIT_FAILURE;
        }
      } else {
        std::fprintf(stderr, "%s: no ICC profile data in JPEG file\n", progname);
      }
      std::fclose(f);
    }
    if (!cli::write_file(output_file, out)) {
      std::fprintf(stderr, "Output file write error --- out of disk space?\n");
      return EXIT_FAILURE;
    }
    dec.finish();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    if (output_file != stdout) std::fclose(output_file);
    return EXIT_FAILURE;
  }
  if (output_file != stdout) std::fclose(output_file);
  return dec.diagnostics().num_warnings ? cli::EXIT_WARNING : EXIT_SUCCESS;
}
