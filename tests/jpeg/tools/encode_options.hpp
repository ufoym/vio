// Compression parameter switches shared by jpeg-encode and jpeg-transform
// (port of libjpeg-turbo's rdswitch.c).
#pragma once

#include <cctype>
#include <cstdio>

#include "cli.hpp"
#include "vio/jpeg/encoder.hpp"

namespace cli {

namespace jpeg = vio::jpeg;

// libjpeg-turbo builds rdswitch.c with JPEG_LIB_VERSION 62, which keeps the
// per-table quality scale factors here rather than in the compressor.
inline int q_scale_factor[jpeg::NUM_QUANT_TBLS] = {100, 100, 100, 100};

// ---- rdswitch.c ----------------------------------------------------------
struct TextReader {
  std::vector<std::uint8_t> data;
  std::size_t pos = 0;
  int getc() { return pos < data.size() ? data[pos++] : EOF; }
  void ungetc() { pos--; }
  int text_getc() {
    int ch = getc();
    if (ch == '#') {
      do {
        ch = getc();
      } while (ch != '\n' && ch != EOF);
    }
    return ch;
  }
  bool read_text_integer(long* result, int* termchar) {
    int ch;
    do {
      ch = text_getc();
      if (ch == EOF) {
        *termchar = ch;
        return false;
      }
    } while (std::isspace(ch));
    if (!std::isdigit(ch)) {
      *termchar = ch;
      return false;
    }
    long val = ch - '0';
    while ((ch = text_getc()) != EOF) {
      if (!std::isdigit(ch)) break;
      val = val * 10 + (ch - '0');
    }
    *result = val;
    *termchar = ch;
    return true;
  }
  bool read_scan_integer(long* result, int* termchar) {
    if (!read_text_integer(result, termchar)) return false;
    int ch = *termchar;
    while (ch != EOF && std::isspace(ch)) ch = text_getc();
    if (std::isdigit(ch)) {
      ungetc();
      ch = ' ';
    } else if (ch != EOF && ch != ';' && ch != ':') {
      ch = ' ';
    }
    *termchar = ch;
    return true;
  }
};

inline bool read_quant_tables(jpeg::detail::CState& cs, const char* filename, bool force_baseline) {
  TextReader r;
  if (!cli::read_file(filename, r.data)) {
    std::fprintf(stderr, "Can't open table file %s\n", filename);
    return false;
  }
  int tblno = 0, termchar;
  long val;
  unsigned table[jpeg::DCTSIZE2];
  while (r.read_text_integer(&val, &termchar)) {
    if (tblno >= jpeg::NUM_QUANT_TBLS) {
      std::fprintf(stderr, "Too many tables in file %s\n", filename);
      return false;
    }
    table[0] = static_cast<unsigned>(val);
    for (int i = 1; i < jpeg::DCTSIZE2; i++) {
      if (!r.read_text_integer(&val, &termchar)) {
        std::fprintf(stderr, "Invalid table data in file %s\n", filename);
        return false;
      }
      table[i] = static_cast<unsigned>(val);
    }
    cs.add_quant_table(tblno, table, q_scale_factor[tblno], force_baseline);
    tblno++;
  }
  if (termchar != EOF) {
    std::fprintf(stderr, "Non-numeric data in file %s\n", filename);
    return false;
  }
  return true;
}

inline bool read_scan_script(jpeg::detail::CState& cs, const char* filename) {
  TextReader r;
  if (!cli::read_file(filename, r.data)) {
    std::fprintf(stderr, "Can't open scan definition file %s\n", filename);
    return false;
  }
  constexpr int MAX_SCANS = 100;
  std::vector<jpeg::ScanInfo> scans;
  int termchar;
  long val;
  auto bogus = [&] {
    std::fprintf(stderr, "Invalid scan entry format in file %s\n", filename);
    return false;
  };
  while (r.read_scan_integer(&val, &termchar)) {
    if (static_cast<int>(scans.size()) >= MAX_SCANS) {
      std::fprintf(stderr, "Too many scans defined in file %s\n", filename);
      return false;
    }
    jpeg::ScanInfo s;
    s.component_index[0] = static_cast<int>(val);
    int ncomps = 1;
    while (termchar == ' ') {
      if (ncomps >= jpeg::MAX_COMPS_IN_SCAN) {
        std::fprintf(stderr, "Too many components in one scan in file %s\n", filename);
        return false;
      }
      if (!r.read_scan_integer(&val, &termchar)) return bogus();
      s.component_index[ncomps++] = static_cast<int>(val);
    }
    s.comps_in_scan = ncomps;
    if (termchar == ':') {
      if (!r.read_scan_integer(&val, &termchar) || termchar != ' ') return bogus();
      s.Ss = static_cast<int>(val);
      if (!r.read_scan_integer(&val, &termchar) || termchar != ' ') return bogus();
      s.Se = static_cast<int>(val);
      if (!r.read_scan_integer(&val, &termchar) || termchar != ' ') return bogus();
      s.Ah = static_cast<int>(val);
      if (!r.read_scan_integer(&val, &termchar)) return bogus();
      s.Al = static_cast<int>(val);
    } else {
      s.Ss = 0;
      s.Se = jpeg::DCTSIZE2 - 1;
      s.Ah = 0;
      s.Al = 0;
    }
    if (termchar != ';' && termchar != EOF) return bogus();
    scans.push_back(s);
  }
  if (termchar != EOF) {
    std::fprintf(stderr, "Non-numeric data in file %s\n", filename);
    return false;
  }
  if (!scans.empty()) {
    cs.num_scans = static_cast<int>(scans.size());
    cs.scan_info = std::move(scans);
  }
  return true;
}

inline bool set_quality_ratings(jpeg::detail::CState& cs, const char* arg, bool force_baseline) {
  int val = 75;
  for (int tblno = 0; tblno < jpeg::NUM_QUANT_TBLS; tblno++) {
    if (*arg) {
      char ch = ',';
      if (std::sscanf(arg, "%d%c", &val, &ch) < 1) return false;
      if (ch != ',') return false;
      q_scale_factor[tblno] = jpeg::detail::quality_scaling(val);
      while (*arg && *arg++ != ',') {
      }
    } else {
      q_scale_factor[tblno] = jpeg::detail::quality_scaling(val);
    }
  }
  cs.add_quant_table(0, jpeg::detail::std_luminance_quant_tbl, q_scale_factor[0], force_baseline);
  cs.add_quant_table(1, jpeg::detail::std_chrominance_quant_tbl, q_scale_factor[1],
                     force_baseline);
  return true;
}

inline bool set_quant_slots(jpeg::detail::CState& cs, const char* arg) {
  int val = 0;
  for (int ci = 0; ci < jpeg::MAX_COMPONENTS; ci++) {
    if (*arg) {
      char ch = ',';
      if (std::sscanf(arg, "%d%c", &val, &ch) < 1) return false;
      if (ch != ',') return false;
      if (val < 0 || val >= jpeg::NUM_QUANT_TBLS) {
        std::fprintf(stderr, "JPEG quantization tables are numbered 0..%d\n",
                     jpeg::NUM_QUANT_TBLS - 1);
        return false;
      }
      cs.comp_info[ci].quant_tbl_no = val;
      while (*arg && *arg++ != ',') {
      }
    } else {
      cs.comp_info[ci].quant_tbl_no = val;
    }
  }
  return true;
}

inline bool set_sample_factors(jpeg::detail::CState& cs, const char* arg) {
  for (int ci = 0; ci < jpeg::MAX_COMPONENTS; ci++) {
    if (*arg) {
      int v1, v2;
      char ch1, ch2 = ',';
      if (std::sscanf(arg, "%d%c%d%c", &v1, &ch1, &v2, &ch2) < 3) return false;
      if ((ch1 != 'x' && ch1 != 'X') || ch2 != ',') return false;
      if (v1 <= 0 || v1 > 4 || v2 <= 0 || v2 > 4) {
        std::fprintf(stderr, "JPEG sampling factors must be 1..4\n");
        return false;
      }
      cs.comp_info[ci].h_samp_factor = v1;
      cs.comp_info[ci].v_samp_factor = v2;
      while (*arg && *arg++ != ',') {
      }
    } else {
      cs.comp_info[ci].h_samp_factor = 1;
      cs.comp_info[ci].v_samp_factor = 1;
    }
  }
  return true;
}

}  // namespace cli
