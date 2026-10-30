// Shared helpers for the command-line tools (jpeg-encode, jpeg-decode,
// jpeg-transform, jpeg-example), whose command lines match libjpeg-turbo's
// cjpeg, djpeg, jpegtran and example.
#pragma once

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "vio/core.hpp"

namespace cli {

inline constexpr int EXIT_WARNING = 2;

inline void warn(const std::string& msg, bool strict, int trace_level, bool& first_warning) {
  if (strict) throw vio::Error(msg);
  if (first_warning || trace_level >= 3) std::fprintf(stderr, "%s\n", msg.c_str());
  first_warning = false;
}

// Case-insensitive keyword match allowing unique abbreviations (keymatch()).
inline bool keymatch(const char* arg, const char* keyword, int minchars) {
  int nmatched = 0;
  int ca;
  while ((ca = static_cast<unsigned char>(*arg++)) != '\0') {
    int ck = static_cast<unsigned char>(*keyword++);
    if (ck == '\0') return false;
    if (std::isupper(ca)) ca = std::tolower(ca);
    if (ca != ck) return false;
    nmatched++;
  }
  return nmatched >= minchars;
}

inline bool valid_maxmemory(const char* arg) {
  long value;
  return std::sscanf(arg, "%ld", &value) == 1;
}

inline std::vector<std::uint8_t> read_stream(std::FILE* f) {
  std::vector<std::uint8_t> data;
  std::uint8_t buf[65536];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
  return data;
}

inline bool read_file(const char* name, std::vector<std::uint8_t>& out) {
  std::FILE* f = std::fopen(name, "rb");
  if (!f) return false;
  out = read_stream(f);
  std::fclose(f);
  return true;
}

inline bool read_input(const char* name, std::vector<std::uint8_t>& out, const char* progname,
                       const char* suffix = "") {
  if (!name) {
    out = read_stream(stdin);
    return true;
  }
  if (read_file(name, out)) return true;
  std::fprintf(stderr, "%s: can't open %s%s\n", progname, name, suffix);
  return false;
}

inline bool read_icc(const char* name, std::vector<std::uint8_t>& out, const char* progname) {
  if (!read_input(name, out, progname)) return false;
  if (!out.empty()) return true;
  std::fprintf(stderr, "%s: can't determine size of %s\n", progname, name);
  return false;
}

inline std::FILE* open_output(const char* name, bool nooverwrite, const char* progname,
                              const char* suffix = "") {
  if (nooverwrite) {
    if (std::FILE* f = std::fopen(name, "rb")) {
      std::fclose(f);
      std::fprintf(stderr, "%s: can't open %s; file exists\n", progname, name);
      return nullptr;
    }
  }
  std::FILE* f = std::fopen(name, "wb");
  if (!f) std::fprintf(stderr, "%s: can't open %s%s\n", progname, name, suffix);
  return f;
}

inline bool write_file(std::FILE* f, const std::vector<std::uint8_t>& data) {
  if (!data.empty() && std::fwrite(data.data(), 1, data.size(), f) != data.size()) return false;
  std::fflush(f);
  return !std::ferror(f);
}

}  // namespace cli
