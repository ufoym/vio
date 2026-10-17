// jpeg-transform - lossless transcoding/transformation of JPEG files (command-line
// compatible with libjpeg-turbo's jpegtran).
#include <cstring>

#include "cli.hpp"
#include "encode_options.hpp"
#include "vio/jpeg/transform.hpp"

using namespace vio;
using cli::keymatch;

namespace {

const char* progname = "jpeg-transform";

struct Options {
  const char* icc_filename = nullptr;
  unsigned max_scans = 0;
  const char* outfilename = nullptr;
  const char* dropfilename = nullptr;
  bool nooverwrite = false, report = false, strict = false;
  jpeg::CopyOption copyoption = jpeg::CopyOption::Comments;
  jpeg::Transform transform;
  int trace_level = 0;
};

[[noreturn]] void usage() {
  std::fprintf(stderr, "usage: %s [switches] [inputfile]\n", progname);
  std::fprintf(stderr, "Switches (names may be abbreviated):\n");
  std::fprintf(stderr, "  -copy none     Copy no extra markers from source file\n");
  std::fprintf(stderr, "  -copy comments Copy only comment markers (default)\n");
  std::fprintf(stderr, "  -copy icc      Copy only ICC profile markers\n");
  std::fprintf(stderr, "  -copy all      Copy all extra markers\n");
  std::fprintf(stderr, "  -optimize      Optimize Huffman table (smaller file, but slow compression)\n");
  std::fprintf(stderr, "  -progressive   Create progressive JPEG file\n");
  std::fprintf(stderr, "Switches for modifying the image:\n");
  std::fprintf(stderr, "  -crop WxH+X+Y  Crop to a rectangular region\n");
  std::fprintf(stderr, "  -drop +X+Y filename          Drop (insert) another image\n");
  std::fprintf(stderr, "  -flip [horizontal|vertical]  Mirror image (left-right or top-bottom)\n");
  std::fprintf(stderr, "  -grayscale     Reduce to grayscale (omit color data)\n");
  std::fprintf(stderr, "  -perfect       Fail if there is non-transformable edge blocks\n");
  std::fprintf(stderr, "  -rotate [90|180|270]         Rotate image (degrees clockwise)\n");
  std::fprintf(stderr, "  -roll +X+Y     Shift image with wraparound\n");
  std::fprintf(stderr, "  -transpose     Transpose image\n");
  std::fprintf(stderr, "  -transverse    Transverse transpose image\n");
  std::fprintf(stderr, "  -trim          Drop non-transformable edge blocks\n");
  std::fprintf(stderr, "                 with -drop: Requantize drop file to match source file\n");
  std::fprintf(stderr, "  -wipe WxH+X+Y  Wipe (gray out) a rectangular region\n");
  std::fprintf(stderr, "Switches for advanced users:\n");
  std::fprintf(stderr, "  -arithmetic    Use arithmetic coding\n");
  std::fprintf(stderr, "  -icc FILE      Embed ICC profile contained in FILE\n");
  std::fprintf(stderr, "  -restart N     Set restart interval in rows, or in blocks with B\n");
  std::fprintf(stderr, "  -maxmemory N   Maximum memory to use (in kbytes)\n");
  std::fprintf(stderr, "  -maxscans N    Maximum number of scans to allow in input file\n");
  std::fprintf(stderr, "  -outfile name  Specify name for output file\n");
  std::fprintf(stderr, "  -nooverwrite   Don't overwrite output file if it exists\n");
  std::fprintf(stderr, "  -report        Report transformation progress\n");
  std::fprintf(stderr, "  -strict        Treat all warnings as fatal\n");
  std::fprintf(stderr, "  -verbose  or  -debug   Emit debug output\n");
  std::fprintf(stderr, "  -version       Print version information and exit\n");
  std::fprintf(stderr, "Switches for wizards:\n");
  std::fprintf(stderr, "  -scans FILE    Create multi-scan JPEG per script FILE\n");
  std::exit(EXIT_FAILURE);
}

void select_transform(Options& o, jpeg::TransformOp t) {
  if (o.transform.transform == jpeg::TransformOp::None || o.transform.transform == t) {
    o.transform.transform = t;
  } else {
    std::fprintf(stderr, "%s: can only do one image transformation at a time\n", progname);
    usage();
  }
}

int parse_switches(Options& o, jpeg::detail::CState& cs, int argc, char** argv, bool for_real) {
  bool simple_progressive = false;
  const char* scansarg = nullptr;
  o.icc_filename = nullptr;
  o.max_scans = 0;
  o.outfilename = nullptr;
  o.nooverwrite = o.report = o.strict = false;
  o.copyoption = jpeg::CopyOption::Comments;
  o.transform.transform = jpeg::TransformOp::None;
  o.transform.perfect = o.transform.trim = o.transform.force_grayscale = false;
  o.transform.crop = o.transform.slow_hflip = false;
  o.trace_level = 0;
  auto& t = o.transform;
  int argn;
  for (argn = 1; argn < argc; argn++) {
    char* arg = argv[argn];
    if (*arg != '-') break;
    arg++;
    if (keymatch(arg, "arithmetic", 1)) {
      cs.arith_code = true;
    } else if (keymatch(arg, "copy", 1)) {
      if (++argn >= argc) usage();
      if (keymatch(argv[argn], "none", 1)) o.copyoption = jpeg::CopyOption::None;
      else if (keymatch(argv[argn], "comments", 1)) o.copyoption = jpeg::CopyOption::Comments;
      else if (keymatch(argv[argn], "icc", 1)) o.copyoption = jpeg::CopyOption::ICC;
      else if (keymatch(argv[argn], "all", 1)) o.copyoption = jpeg::CopyOption::All;
      else usage();
    } else if (keymatch(arg, "crop", 2)) {
      if (++argn >= argc) usage();
      if (t.crop || !jpeg::parse_crop_spec(t, argv[argn])) {
        std::fprintf(stderr, "%s: bogus -crop argument '%s'\n", progname, argv[argn]);
        std::exit(EXIT_FAILURE);
      }
    } else if (keymatch(arg, "drop", 2)) {
      if (++argn >= argc) usage();
      if (t.crop || !jpeg::parse_crop_spec(t, argv[argn]) ||
          t.crop_width_set != jpeg::CropCode::Unset || t.crop_height_set != jpeg::CropCode::Unset) {
        std::fprintf(stderr, "%s: bogus -drop argument '%s'\n", progname, argv[argn]);
        std::exit(EXIT_FAILURE);
      }
      if (++argn >= argc) usage();
      o.dropfilename = argv[argn];
      select_transform(o, jpeg::TransformOp::Drop);
    } else if (keymatch(arg, "debug", 1) || keymatch(arg, "verbose", 1)) {
      static bool printed_version = false;
      if (!printed_version) {
        std::fprintf(stderr, "vio jpegtran\n");
        printed_version = true;
      }
      o.trace_level++;
    } else if (keymatch(arg, "version", 4)) {
      std::fprintf(stderr, "vio jpegtran\n");
      std::exit(EXIT_SUCCESS);
    } else if (keymatch(arg, "flip", 1)) {
      if (++argn >= argc) usage();
      if (keymatch(argv[argn], "horizontal", 1)) select_transform(o, jpeg::TransformOp::FlipH);
      else if (keymatch(argv[argn], "vertical", 1)) select_transform(o, jpeg::TransformOp::FlipV);
      else usage();
    } else if (keymatch(arg, "grayscale", 1) || keymatch(arg, "greyscale", 1)) {
      t.force_grayscale = true;
    } else if (keymatch(arg, "icc", 1)) {
      if (++argn >= argc) usage();
      o.icc_filename = argv[argn];
    } else if (keymatch(arg, "maxmemory", 3)) {
      long lval;
      char ch = 'x';
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%ld%c", &lval, &ch) < 1) usage();
    } else if (keymatch(arg, "maxscans", 4)) {
      if (++argn >= argc) usage();
      if (std::sscanf(argv[argn], "%u", &o.max_scans) != 1) usage();
    } else if (keymatch(arg, "optimize", 1) || keymatch(arg, "optimise", 1)) {
      cs.optimize_coding = true;
    } else if (keymatch(arg, "outfile", 4)) {
      if (++argn >= argc) usage();
      o.outfilename = argv[argn];
    } else if (keymatch(arg, "nooverwrite", 1)) {
      o.nooverwrite = true;
    } else if (keymatch(arg, "perfect", 2)) {
      t.perfect = true;
    } else if (keymatch(arg, "progressive", 1)) {
      simple_progressive = true;
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
    } else if (keymatch(arg, "roll", 3)) {
      if (++argn >= argc) usage();
      if (t.crop || !jpeg::parse_crop_spec(t, argv[argn]) ||
          t.crop_width_set != jpeg::CropCode::Unset || t.crop_height_set != jpeg::CropCode::Unset) {
        std::fprintf(stderr, "%s: bogus -roll argument '%s'\n", progname, argv[argn]);
        std::exit(EXIT_FAILURE);
      }
      select_transform(o, jpeg::TransformOp::Roll);
    } else if (keymatch(arg, "rotate", 2)) {
      if (++argn >= argc) usage();
      if (keymatch(argv[argn], "90", 2)) select_transform(o, jpeg::TransformOp::Rot90);
      else if (keymatch(argv[argn], "180", 3)) select_transform(o, jpeg::TransformOp::Rot180);
      else if (keymatch(argv[argn], "270", 3)) select_transform(o, jpeg::TransformOp::Rot270);
      else usage();
    } else if (keymatch(arg, "scans", 1)) {
      if (++argn >= argc) usage();
      scansarg = argv[argn];
    } else if (keymatch(arg, "strict", 2)) {
      o.strict = true;
    } else if (keymatch(arg, "transpose", 1)) {
      select_transform(o, jpeg::TransformOp::Transpose);
    } else if (keymatch(arg, "transverse", 6)) {
      select_transform(o, jpeg::TransformOp::Transverse);
    } else if (keymatch(arg, "trim", 3)) {
      t.trim = true;
    } else if (keymatch(arg, "wipe", 1)) {
      if (++argn >= argc) usage();
      if (t.crop || !jpeg::parse_crop_spec(t, argv[argn])) {
        std::fprintf(stderr, "%s: bogus -wipe argument '%s'\n", progname, argv[argn]);
        std::exit(EXIT_FAILURE);
      }
      select_transform(o, jpeg::TransformOp::Wipe);
    } else {
      usage();
    }
  }
  if (for_real) {
    if (simple_progressive) cs.simple_progression();
    if (scansarg && !cli::read_scan_script(cs, scansarg)) usage();
  }
  return argn;
}

}  // namespace

int main(int argc, char** argv) {
  if (argv[0] && argv[0][0]) progname = argv[0];
  jpeg::Decoder src, drop;
  jpeg::Encoder dst;
  auto& cs = dst.core().cs;
  Options o;
  int file_index = parse_switches(o, cs, argc, argv, false);
  if (file_index < argc - 1) {
    std::fprintf(stderr, "%s: only one input file\n", progname);
    usage();
  }
  std::vector<std::uint8_t> input, drop_data, icc_profile;
  if (file_index < argc) {
    if (!cli::read_file(argv[file_index], input)) {
      std::fprintf(stderr, "%s: can't open %s for reading\n", progname, argv[file_index]);
      return EXIT_FAILURE;
    }
  } else {
    input = cli::read_stream(stdin);
  }
  if (o.icc_filename) {
    if (!cli::read_file(o.icc_filename, icc_profile)) {
      std::fprintf(stderr, "%s: can't open %s\n", progname, o.icc_filename);
      return EXIT_FAILURE;
    }
    if (icc_profile.empty()) {
      std::fprintf(stderr, "%s: can't determine size of %s\n", progname, o.icc_filename);
      return EXIT_FAILURE;
    }
    if (o.copyoption == jpeg::CopyOption::All) o.copyoption = jpeg::CopyOption::AllExceptICC;
    if (o.copyoption == jpeg::CopyOption::ICC) o.copyoption = jpeg::CopyOption::None;
  }
  if (o.dropfilename && !cli::read_file(o.dropfilename, drop_data)) {
    std::fprintf(stderr, "%s: can't open %s for reading\n", progname, o.dropfilename);
    return EXIT_FAILURE;
  }

  bool first_warning = true;
  auto on_warning = [&](const std::string& msg) {
    if (o.strict) throw Error(msg);
    if (first_warning || o.trace_level >= 3) std::fprintf(stderr, "%s\n", msg.c_str());
    first_warning = false;
  };
  src.diagnostics().on_warning = on_warning;
  drop.diagnostics().on_warning = on_warning;
  dst.diagnostics().on_warning = on_warning;

  std::vector<std::uint8_t> out;
  try {
    jpeg::copy_markers_setup(src, o.copyoption);
    src.open(input);
    auto& t = o.transform;
    if (o.dropfilename) {
      drop.open(drop_data);
      t.crop_width = drop.header().width;
      t.crop_width_set = jpeg::CropCode::Pos;
      t.crop_height = drop.header().height;
      t.crop_height_set = jpeg::CropCode::Pos;
      t.drop_ptr = &drop.core().st;
    }
    if (!jpeg::request_workspace(src, t)) {
      std::fprintf(stderr, "%s: transformation is not perfect\n", progname);
      return EXIT_FAILURE;
    }
    jpeg::detail::BlockArray* src_coefs = src.core().read_coefficients();
    if (o.dropfilename) t.drop_coef_arrays = drop.core().read_coefficients();
    jpeg::copy_critical_parameters(src, dst);
    jpeg::detail::BlockArray* dst_coefs = jpeg::adjust_parameters(src, dst, src_coefs, t);
    // Re-parse switches for real (they adjust the destination parameters);
    // keep the transform state computed above.
    jpeg::Transform saved = std::move(t);
    parse_switches(o, cs, argc, argv, true);
    o.transform = std::move(saved);
    if (!o.transform.workspace.empty()) dst_coefs = o.transform.workspace.data();
    if (o.icc_filename) {
      if (o.copyoption == jpeg::CopyOption::All) o.copyoption = jpeg::CopyOption::AllExceptICC;
      if (o.copyoption == jpeg::CopyOption::ICC) o.copyoption = jpeg::CopyOption::None;
    }
    dst.core().write_coefficients(dst_coefs);
    jpeg::copy_markers_execute(src, dst, o.copyoption);
    if (!icc_profile.empty()) dst.write_icc_profile(icc_profile);
    jpeg::execute_transform(src, dst, src_coefs, o.transform);
    out = dst.finish();
    if (o.dropfilename) drop.finish();
    src.finish();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return EXIT_FAILURE;
  }

  std::FILE* fp = stdout;
  if (o.outfilename) {
    if (o.nooverwrite) {
      if (std::FILE* f = std::fopen(o.outfilename, "rb")) {
        std::fclose(f);
        std::fprintf(stderr, "%s: can't open %s; file exists\n", progname, o.outfilename);
        return EXIT_FAILURE;
      }
    }
    if ((fp = std::fopen(o.outfilename, "wb")) == nullptr) {
      std::fprintf(stderr, "%s: can't open %s for writing\n", progname, o.outfilename);
      return EXIT_FAILURE;
    }
  }
  if (!cli::write_file(fp, out)) {
    std::fprintf(stderr, "Output file write error --- out of disk space?\n");
    return EXIT_FAILURE;
  }
  if (fp != stdout) std::fclose(fp);
  int warnings = src.diagnostics().num_warnings + drop.diagnostics().num_warnings +
                 dst.diagnostics().num_warnings;
  return warnings ? cli::EXIT_WARNING : EXIT_SUCCESS;
}
