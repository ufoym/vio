// jpeg-encode - compress an image file to a JPEG file (command-line compatible
// with libjpeg-turbo's cjpeg).
#include <cstring>
#include <memory>

#include "cli.hpp"
#include "encode_options.hpp"
#include "vio/bmp.hpp"
#include "vio/gif.hpp"
#include "vio/png.hpp"
#include "vio/ppm.hpp"
#include "vio/targa.hpp"
#include "vio/jpeg/encoder.hpp"

using namespace vio;
using cli::keymatch;
using namespace cli;

namespace {

const char* progname = "jpeg-encode";

struct Options {
  bool is_targa = false;
  const char* icc_filename = nullptr;
  bool noicc = false;
  const char* outfilename = nullptr;
  bool nooverwrite = false, memdst = false, report = false, strict = false;
  int trace_level = 0;
};

[[noreturn]] void usage() {
  std::fprintf(stderr, "usage: %s [switches] [inputfile]\n", progname);
  std::fprintf(stderr, "Switches (names may be abbreviated):\n");
  std::fprintf(stderr, "  -quality N[,...]   Compression quality (0..100; 5-95 is most useful range,\n");
  std::fprintf(stderr, "                     default is 75)\n");
  std::fprintf(stderr, "  -grayscale     Create monochrome JPEG file\n");
  std::fprintf(stderr, "  -rgb           Create RGB JPEG file\n");
  std::fprintf(stderr, "  -optimize      Optimize Huffman table (smaller file, but slow compression)\n");
  std::fprintf(stderr, "  -progressive   Create progressive JPEG file\n");
  std::fprintf(stderr, "  -targa         Input file is Targa format [legacy feature]\n");
  std::fprintf(stderr, "Switches for advanced users:\n");
  std::fprintf(stderr, "  -precision N   Create JPEG file with N-bit data precision\n");
  std::fprintf(stderr, "                 (N=2..16; default is 8; if N is not 8 or 12, then -lossless\n");
  std::fprintf(stderr, "                 must also be specified)\n");
  std::fprintf(stderr, "  -lossless psv[,Pt]  Create lossless JPEG file\n");
  std::fprintf(stderr, "  -arithmetic    Use arithmetic coding\n");
  std::fprintf(stderr, "  -dct int       Use accurate integer DCT method (default)\n");
  std::fprintf(stderr, "  -dct fast      Use less accurate integer DCT method [legacy feature]\n");
  std::fprintf(stderr, "  -dct float     Use floating-point DCT method [legacy feature]\n");
  std::fprintf(stderr, "  -icc FILE      Embed ICC profile contained in FILE\n");
  std::fprintf(stderr, "  -noicc         Do not transfer ICC profile from PNG input file\n");
  std::fprintf(stderr, "  -restart N     Set restart interval in rows, or in blocks with B\n");
  std::fprintf(stderr, "  -smooth N      Smooth dithered input (N=1..100 is strength)\n");
  std::fprintf(stderr, "  -maxmemory N   Maximum memory to use (in kbytes)\n");
  std::fprintf(stderr, "  -outfile name  Specify name for output file\n");
  std::fprintf(stderr, "  -nooverwrite   Don't overwrite output file if it exists\n");
  std::fprintf(stderr, "  -memdst        Compress to memory instead of file (useful for benchmarking)\n");
  std::fprintf(stderr, "  -report        Report compression progress\n");
  std::fprintf(stderr, "  -strict        Treat all warnings as fatal\n");
  std::fprintf(stderr, "  -verbose  or  -debug   Emit debug output\n");
  std::fprintf(stderr, "  -version       Print version information and exit\n");
  std::fprintf(stderr, "Switches for wizards:\n");
  std::fprintf(stderr, "  -baseline      Force baseline quantization tables\n");
  std::fprintf(stderr, "  -qtables FILE  Use quantization tables given in FILE\n");
  std::fprintf(stderr, "  -qslots N[,...]    Set component quantization tables\n");
  std::fprintf(stderr, "  -sample HxV[,...]  Set component sampling factors\n");
  std::fprintf(stderr, "  -scans FILE    Create multi-scan JPEG per script FILE\n");
  std::exit(EXIT_FAILURE);
}

// ---- switches --------------------------------------------------------------
int parse_switches(Options& o, jpeg::detail::CState& cs, int argc, char** argv, bool for_real) {
  int psv = 0, pt = 0;
  bool force_baseline = false, simple_progressive = false;
  const char *qualityarg = nullptr, *qtablefile = nullptr, *qslotsarg = nullptr,
             *samplearg = nullptr, *scansarg = nullptr;
  o = Options();
  int argn;
  for (argn = 1; argn < argc; argn++) {
    char* arg = argv[argn];
    if (*arg != '-') break;
    arg++;
    if (keymatch(arg, "arithmetic", 1)) {
      cs.arith_code = true;
    } else if (keymatch(arg, "baseline", 1)) {
      force_baseline = true;
    } else if (keymatch(arg, "dct", 2)) {
      if (++argn >= argc) usage();
      if (keymatch(argv[argn], "int", 1)) cs.dct_method = jpeg::DctMethod::IntSlow;
      else if (keymatch(argv[argn], "fast", 2)) cs.dct_method = jpeg::DctMethod::IntFast;
      else if (keymatch(argv[argn], "float", 2)) cs.dct_method = jpeg::DctMethod::Float;
      else usage();
    } else if (keymatch(arg, "debug", 1) || keymatch(arg, "verbose", 1)) {
      static bool printed_version = false;
      if (!printed_version) {
        std::fprintf(stderr, "vio cjpeg\n");
        printed_version = true;
      }
      o.trace_level++;
    } else if (keymatch(arg, "version", 4)) {
      std::fprintf(stderr, "vio cjpeg\n");
      std::exit(EXIT_SUCCESS);
    } else if (keymatch(arg, "grayscale", 2) || keymatch(arg, "greyscale", 2)) {
      cs.set_colorspace(ColorSpace::Gray);
    } else if (keymatch(arg, "rgb", 3)) {
      cs.set_colorspace(ColorSpace::RGB);
    } else if (keymatch(arg, "icc", 1)) {
      if (++argn >= argc) usage();
      o.icc_filename = argv[argn];
    } else if (keymatch(arg, "noicc", 3)) {
      o.noicc = true;
    } else if (keymatch(arg, "lossless", 1)) {
      char ch = ',';
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%d%c", &psv, &ch) < 1 || ch != ',') usage();
      const char* ptr = argv[argn];
      while (*ptr && *ptr++ != ',') {
      }
      if (*ptr) std::sscanf(ptr, "%d", &pt);
    } else if (keymatch(arg, "maxmemory", 3)) {
      long lval;
      char ch = 'x';
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%ld%c", &lval, &ch) < 1) usage();
    } else if (keymatch(arg, "optimize", 1) || keymatch(arg, "optimise", 1)) {
      cs.optimize_coding = true;
    } else if (keymatch(arg, "outfile", 4)) {
      if (++argn >= argc) usage();
      o.outfilename = argv[argn];
    } else if (keymatch(arg, "nooverwrite", 3)) {
      o.nooverwrite = true;
    } else if (keymatch(arg, "precision", 3)) {
      int val;
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%d", &val) != 1) usage();
      if (val < 2 || val > 16) usage();
      cs.data_precision = val;
    } else if (keymatch(arg, "progressive", 1)) {
      simple_progressive = true;
    } else if (keymatch(arg, "memdst", 2)) {
      o.memdst = true;
    } else if (keymatch(arg, "quality", 1)) {
      if (++argn >= argc) usage();
      qualityarg = argv[argn];
    } else if (keymatch(arg, "qslots", 2)) {
      if (++argn >= argc) usage();
      qslotsarg = argv[argn];
    } else if (keymatch(arg, "qtables", 2)) {
      if (++argn >= argc) usage();
      qtablefile = argv[argn];
    } else if (keymatch(arg, "report", 3)) {
      o.report = true;
    } else if (keymatch(arg, "restart", 1)) {
      long lval;
      char ch = 'x';
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%ld%c", &lval, &ch) < 1) usage();
      if (lval < 0 || lval > 65535L) usage();
      if (ch == 'b' || ch == 'B') {
        cs.restart_interval = static_cast<unsigned>(lval);
        cs.restart_in_rows = 0;
      } else {
        cs.restart_in_rows = static_cast<int>(lval);
      }
    } else if (keymatch(arg, "sample", 2)) {
      if (++argn >= argc) usage();
      samplearg = argv[argn];
    } else if (keymatch(arg, "scans", 2)) {
      if (++argn >= argc) usage();
      scansarg = argv[argn];
    } else if (keymatch(arg, "smooth", 2)) {
      int val;
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%d", &val) != 1) usage();
      if (val < 0 || val > 100) usage();
      cs.smoothing_factor = val;
    } else if (keymatch(arg, "strict", 2)) {
      o.strict = true;
    } else if (keymatch(arg, "targa", 1)) {
      o.is_targa = true;
    } else {
      usage();
    }
  }
  if (for_real) {
    if (qualityarg && !set_quality_ratings(cs, qualityarg, force_baseline)) usage();
    if (qtablefile && !read_quant_tables(cs, qtablefile, force_baseline)) usage();
    if (qslotsarg && !set_quant_slots(cs, qslotsarg)) usage();
    if (samplearg && !set_sample_factors(cs, samplearg)) usage();
    if (simple_progressive) cs.simple_progression();
    if (psv != 0) cs.enable_lossless(psv, pt);
    if (scansarg && !read_scan_script(cs, scansarg)) usage();
  }
  return argn;
}

template <class T>
std::unique_ptr<vio::ImageReader<T>> select_file_type(const Options& o,
                                                    const std::vector<std::uint8_t>& in,
                                                    int precision) {
  auto need8 = [&]() {
    if constexpr (!std::is_same_v<T, std::uint8_t>) VIO_FAIL("Unsupported JPEG data precision %d", precision);
  };
  if (o.is_targa) {
    need8();
    if constexpr (std::is_same_v<T, std::uint8_t>) return std::make_unique<vio::targa::Reader>();
  }
  if (in.empty()) VIO_FAIL("Empty input file");
  switch (in[0]) {
    case 'B':
      need8();
      if constexpr (std::is_same_v<T, std::uint8_t>) return std::make_unique<vio::bmp::Reader>();
      break;
    case 'G':
      need8();
      if constexpr (std::is_same_v<T, std::uint8_t>) return std::make_unique<vio::gif::Reader>();
      break;
    case 0x89:
      return std::make_unique<vio::png::Reader<T>>();
    case 'P':
      return std::make_unique<vio::ppm::Reader<T>>();
    case 0x00:
      need8();
      if constexpr (std::is_same_v<T, std::uint8_t>) return std::make_unique<vio::targa::Reader>();
      break;
    default:
      break;
  }
  VIO_FAIL("Unrecognized input file format");
}

template <class T>
void compress(Options& o, jpeg::Encoder& enc, const std::vector<std::uint8_t>& input,
              const std::vector<std::uint8_t>& icc_profile, int argc, char** argv) {
  auto& cs = enc.core().cs;
  auto src = select_file_type<T>(o, input, cs.data_precision);
  src->color = cs.in_color_space;
  src->precision = cs.data_precision;
  if (auto* bmp = dynamic_cast<vio::bmp::Reader*>(src.get())) bmp->use_inversion_array = true;
  src->diag.on_warning = [&](const std::string& msg) { enc.core().cs.warn(msg); };
  src->start(input);
  cs.in_color_space = src->color;
  cs.input_components = src->components;
  cs.data_precision = src->precision;
  cs.image_width = src->width;
  cs.image_height = src->height;
  if (src->density_unit) {
    cs.density_unit = src->density_unit;
    cs.X_density = src->x_density;
    cs.Y_density = src->y_density;
  }
  cs.default_colorspace();
  parse_switches(o, cs, argc, argv, true);
  enc.start(true);
  if (!icc_profile.empty()) {
    enc.write_icc_profile(icc_profile);
  } else if (!o.noicc) {
    std::vector<std::uint8_t> src_icc = src->icc_profile();
    if (!src_icc.empty()) enc.write_icc_profile(src_icc);
  }
  while (cs.next_scanline < cs.image_height) {
    unsigned n = src->read_rows();
    enc.write_scanlines<T>(src->rows(), n);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argv[0] && argv[0][0]) progname = argv[0];
  jpeg::Encoder enc;
  auto& cs = enc.core().cs;
  cs.in_color_space = ColorSpace::RGB;
  cs.set_defaults();
  Options o;
  int file_index = parse_switches(o, cs, argc, argv, false);
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
  std::FILE* output_file = nullptr;
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
  } else if (!o.memdst) {
    output_file = stdout;
  }
  std::vector<std::uint8_t> icc_profile;
  if (o.icc_filename) {
    if (!cli::read_file(o.icc_filename, icc_profile)) {
      std::fprintf(stderr, "%s: can't open %s\n", progname, o.icc_filename);
      return EXIT_FAILURE;
    }
    if (icc_profile.empty()) {
      std::fprintf(stderr, "%s: can't determine size of %s\n", progname, o.icc_filename);
      return EXIT_FAILURE;
    }
  }
  bool first_warning = true;
  enc.diagnostics().on_warning = [&](const std::string& msg) {
    if (o.strict) throw Error(msg);
    if (first_warning || o.trace_level >= 3) std::fprintf(stderr, "%s\n", msg.c_str());
    first_warning = false;
  };
  std::vector<std::uint8_t> out;
  try {
    int p = cs.data_precision;
    if (p <= 8) compress<std::uint8_t>(o, enc, input, icc_profile, argc, argv);
    else if (p <= 12) compress<std::int16_t>(o, enc, input, icc_profile, argc, argv);
    else compress<std::uint16_t>(o, enc, input, icc_profile, argc, argv);
    out = enc.finish();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    if (output_file && output_file != stdout) std::fclose(output_file);
    return EXIT_FAILURE;
  }
  if (output_file) {
    if (!cli::write_file(output_file, out)) {
      std::fprintf(stderr, "Output file write error --- out of disk space?\n");
      return EXIT_FAILURE;
    }
    if (output_file != stdout) std::fclose(output_file);
  }
  if (o.memdst)
    std::fprintf(stderr, "Compressed size:  %lu bytes\n", static_cast<unsigned long>(out.size()));
  return enc.diagnostics().num_warnings ? cli::EXIT_WARNING : EXIT_SUCCESS;
}
