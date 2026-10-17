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

inline bool write_file(std::FILE* f, const std::vector<std::uint8_t>& data) {
  if (!data.empty() && std::fwrite(data.data(), 1, data.size(), f) != data.size()) return false;
  std::fflush(f);
  return !std::ferror(f);
}

}  // namespace cli
