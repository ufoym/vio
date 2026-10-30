// bmp-test-size: verifies that the image readers and writers limit image
// dimensions appropriately and do not overflow 32-bit integers.
//
// The test table (TESTREAD/TESTWRITE macros and main()) is libjpeg-turbo's
// src/bmpsizetest.c, reused verbatim; the per-format probes below drive vio's
// format modules the same way the original drives the cdjpeg modules: each
// probe parses (or emits) only the file header.
//
// Copyright (C)2024-2025 D. R. Commander (original test).  See
// tests/third_party/libjpeg-turbo/LICENSE.md.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "vio/bmp.hpp"
#include "vio/gif.hpp"
#include "vio/png.hpp"
#include "vio/ppm.hpp"
#include "vio/targa.hpp"

#define JPEG_MAX_DIMENSION  65500L
#define JMSG_LENGTH_MAX  200
#define FALSE  0
#define TRUE  1
typedef unsigned int JDIMENSION;
typedef int boolean;

static char error_str[JMSG_LENGTH_MAX] = { 0 };

#define PUT16(array, index, value) { \
  array[index] = (unsigned int)(value) & 0xFF; \
  array[index + 1] = (unsigned int)(value) >> 8; \
}
#define PUT32(array, index, value) { \
  array[index] = (unsigned int)(value) & 0xFF; \
  array[index + 1] = (unsigned int)(value) >> 8; \
  array[index + 2] = (unsigned int)(value) >> 16; \
  array[index + 3] = (unsigned int)(value) >> 24; \
}
#define BYTESWAP32(array, index, value) { \
  array[index] = (unsigned int)(value) >> 24; \
  array[index + 1] = (unsigned int)(value) >> 16; \
  array[index + 2] = (unsigned int)(value) >> 8; \
  array[index + 3] = (unsigned int)(value) & 0xFF; \
}

// CRC-32 (ISO-HDLC, as used by PNG chunks and zlib's crc32()).
static unsigned int crc32(const unsigned char *buf, size_t len)
{
  unsigned int crc = 0xFFFFFFFF;
  while (len--) {
    crc ^= *buf++;
    for (int k = 0; k < 8; k++) crc = crc & 1 ? 0xEDB88320 ^ (crc >> 1) : crc >> 1;
  }
  return ~crc;
}

// Runs f(); returns 0 on success or -1 (recording the message) on error.
template <class F>
static int probe(F&& f) {
  try {
    f();
    return 0;
  } catch (const std::exception& e) {
    std::snprintf(error_str, JMSG_LENGTH_MAX, "%s", e.what());
    return -1;
  }
}

template <class Src>
static void start_source(Src& src, const unsigned char* data, std::size_t len, int precision,
                         JDIMENSION max_pixels) {
  src.color = vio::ColorSpace::RGB;
  src.precision = precision;
  src.max_pixels = max_pixels;
  src.start(std::span<const std::uint8_t>(data, len));
}

template <class Dst>
static void start_dest(Dst& dst, int width, int height, int precision) {
  dst.width = static_cast<unsigned>(width);
  dst.height = static_cast<unsigned>(height);
  dst.color = vio::ColorSpace::RGB;
  dst.components = 3;
  dst.precision = precision;
  dst.start();
}

static int test_read_BMP(int width, int height, JDIMENSION max_pixels,
                         boolean use_inversion_array)
{
  unsigned char bh[54];
  unsigned int pitch = ((unsigned int)width * 3 + 3) & ~3U;
  bh[0] = 'B';  bh[1] = 'M';
  PUT32(bh, 2, 54 + pitch * height);
  bh[6] = bh[7] = bh[8] = bh[9] = 0;
  PUT32(bh, 10, 54);
  PUT32(bh, 14, 40);
  PUT32(bh, 18, width);
  PUT32(bh, 22, height);
  PUT16(bh, 26, 1);
  PUT16(bh, 28, 24);
  for (int i = 30; i < 54; i++) bh[i] = 0;
  return probe([&] {
    vio::bmp::Reader src;
    src.use_inversion_array = use_inversion_array;
    start_source(src, bh, 54, 8, max_pixels);
  });
}

static int test_write_BMP(int width, int height, boolean use_inversion_array)
{
  return probe([&] {
    vio::bmp::Writer dst;
    dst.use_inversion_array = use_inversion_array;
    start_dest(dst, width, height, 8);
  });
}

static int test_read_GIF(int width, int height, JDIMENSION max_pixels,
                         boolean interlaced)
{
  unsigned char gh[30] = { 0 };
  int retval = 0;
  gh[0] = 'G';  gh[1] = 'I';  gh[2] = 'F';  gh[3] = '8';  gh[4] = '7';  gh[5] = 'a';
  PUT16(gh, 6, (unsigned int)width / 2);
  PUT16(gh, 8, (unsigned int)height / 2);
  gh[10] = 0x80;
  gh[16] = gh[17] = gh[18] = 255;
  gh[19] = ',';
  PUT16(gh, 24, (unsigned int)width);
  PUT16(gh, 26, (unsigned int)height);
  gh[28] = interlaced ? 0x40 : 0;
  gh[29] = 2;
  auto read = [&] {
    vio::gif::Reader src;
    start_source(src, gh, 30, 8, max_pixels);
  };
  retval += probe(read);
  PUT16(gh, 6, (unsigned int)width);
  PUT16(gh, 8, (unsigned int)height);
  PUT16(gh, 24, (unsigned int)width / 2);
  PUT16(gh, 26, (unsigned int)height / 2);
  retval += probe(read);
  return retval;
}

static int test_write_GIF(int width, int height, int unused)
{
  (void)unused;
  return probe([&] {
    vio::gif::Writer<std::uint8_t> dst;
    dst.is_lzw = false;
    // GIF output forces color quantization; supply a trivial colormap.
    dst.colormap = { {0, 255}, {0, 255}, {0, 255} };
    start_dest(dst, width, height, 8);
  });
}

template <class T>
static void png_source(const unsigned char* ph, JDIMENSION max_pixels, int data_precision) {
  vio::png::Reader<T> src;
  start_source(src, ph, 33, data_precision, max_pixels);
}

static int test_read_PNG(int width, int height, JDIMENSION max_pixels,
                         int data_precision)
{
  unsigned char ph[33] = { 0 };
  ph[0] = 0x89;  ph[1] = 'P';  ph[2] = 'N';  ph[3] = 'G';
  ph[4] = 13;  ph[5] = 10;  ph[6] = 26;  ph[7] = 10;
  BYTESWAP32(ph, 8, 13);
  ph[12] = 'I';  ph[13] = 'H';  ph[14] = 'D';  ph[15] = 'R';
  BYTESWAP32(ph, 16, width);
  BYTESWAP32(ph, 20, height);
  ph[24] = (data_precision > 8 ? 16 : 8);
  ph[25] = 2;
  unsigned int crc = crc32(&ph[12], 17);
  BYTESWAP32(ph, 29, crc);
  return probe([&] {
    if (data_precision <= 8) png_source<std::uint8_t>(ph, max_pixels, data_precision);
    else if (data_precision <= 12) png_source<std::int16_t>(ph, max_pixels, data_precision);
    else png_source<std::uint16_t>(ph, max_pixels, data_precision);
  });
}

template <class T>
static void png_dest(int width, int height, int data_precision) {
  vio::png::Writer<T> dst;
  start_dest(dst, width, height, data_precision);
}

static int test_write_PNG(int width, int height, int data_precision)
{
  return probe([&] {
    if (data_precision <= 8) png_dest<std::uint8_t>(width, height, data_precision);
    else if (data_precision <= 12) png_dest<std::int16_t>(width, height, data_precision);
    else png_dest<std::uint16_t>(width, height, data_precision);
  });
}

template <class T>
static void ppm_source(const unsigned char* ph, JDIMENSION max_pixels, int data_precision) {
  vio::ppm::Reader<T> src;
  start_source(src, ph, 80, data_precision, max_pixels);
}

static int test_read_PPM(int width, int height, JDIMENSION max_pixels,
                         int data_precision)
{
  unsigned char ph[80] = { 0 };
  std::snprintf((char *)ph, 80, "P6\n%d %d\n%d\n", width, height, (1 << data_precision) - 1);
  return probe([&] {
    if (data_precision <= 8) ppm_source<std::uint8_t>(ph, max_pixels, data_precision);
    else if (data_precision <= 12) ppm_source<std::int16_t>(ph, max_pixels, data_precision);
    else ppm_source<std::uint16_t>(ph, max_pixels, data_precision);
  });
}

template <class T>
static void ppm_dest(int width, int height, int data_precision) {
  vio::ppm::Writer<T> dst;
  start_dest(dst, width, height, data_precision);
}

static int test_write_PPM(int width, int height, int data_precision)
{
  return probe([&] {
    if (data_precision <= 8) ppm_dest<std::uint8_t>(width, height, data_precision);
    else if (data_precision <= 12) ppm_dest<std::int16_t>(width, height, data_precision);
    else ppm_dest<std::uint16_t>(width, height, data_precision);
  });
}

static int test_read_TGA(int width, int height, JDIMENSION max_pixels,
                         boolean top_down)
{
  unsigned char th[18] = { 0 };
  th[2] = 2;
  PUT16(th, 12, width);
  PUT16(th, 14, height);
  th[16] = 24;
  th[17] = top_down ? 0x20 : 0;
  return probe([&] {
    vio::targa::Reader src;
    start_source(src, th, 18, 8, max_pixels);
  });
}

static int test_write_TGA(int width, int height, int unused)
{
  (void)unused;
  return probe([&] {
    vio::targa::Writer dst;
    start_dest(dst, width, height, 8);
  });
}

#define TESTREAD(EXT, w, h, max_pixels, opt_desc, opt, retval, \
  expected_error_str) { \
  error_str[0] = 0; \
  if (test_read_##EXT(w, h, max_pixels, opt) != retval) { \
    fprintf(stderr, "%ld x %ld " opt_desc " " #EXT " error not caught\n", \
            w, h); \
    return -1; \
  } \
  if (strncmp(error_str, expected_error_str, JMSG_LENGTH_MAX)) { \
    fprintf(stderr, "%ld x %ld " opt_desc " " #EXT \
            " error string incorrect\n" \
            "  Expected: %s\n  Actual: %s\n", w, h, expected_error_str, \
            error_str); \
    return -1; \
  } \
}

#define TESTWRITE(EXT, w, h, opt_desc, opt, retval, expected_error_str) { \
  error_str[0] = 0; \
  if (test_write_##EXT(w, h, opt) != retval) { \
    fprintf(stderr, "%ld x %ld " opt_desc " " #EXT " error not caught\n", \
            w, h); \
    return -1; \
  } \
  if (strncmp(error_str, expected_error_str, JMSG_LENGTH_MAX)) { \
    fprintf(stderr, "%ld x %ld " opt_desc " " #EXT \
            " error string incorrect\n" \
            "  Expected: %s\n  Actual: %s\n", w, h, expected_error_str, \
            error_str); \
    return -1; \
  } \
}


int main(void)
{
  fprintf(stderr, "Testing BMP reader ...\n");

  TESTREAD(BMP, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, 0, "bottom-up",
           FALSE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(BMP, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, 0, "bottom-up",
           FALSE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(BMP, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, 0, "bottom-up", FALSE,
           0, "");
  TESTREAD(BMP, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, 0, "top-down",
           TRUE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(BMP, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, 0, "top-down",
           TRUE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(BMP, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, 0, "top-down", TRUE, 0,
           "");

  TESTREAD(BMP, JPEG_MAX_DIMENSION, 15268L, 1000000000, "bottom-up", FALSE, -1,
           "Maximum supported image dimension is 1000000000 pixels");
  TESTREAD(BMP, JPEG_MAX_DIMENSION, 15267L, 1000000000, "bottom-up", FALSE, 0,
           "");
  TESTREAD(BMP, 1527L, JPEG_MAX_DIMENSION, 100000000, "bottom-up", FALSE, -1,
           "Maximum supported image dimension is 100000000 pixels");
  TESTREAD(BMP, 1526L, JPEG_MAX_DIMENSION, 100000000, "bottom-up", FALSE, 0,
           "");
  TESTREAD(BMP, 1000L, 1001L, 1000000, "top-down", TRUE, -1,
           "Maximum supported image dimension is 1000000 pixels");
  TESTREAD(BMP, 1000L, 1000L, 1000000, "top-down", TRUE, 0, "");

  fprintf(stderr, "\n");

  fprintf(stderr, "Testing BMP writer ...\n");

  TESTWRITE(BMP, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, "bottom-up",
            FALSE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(BMP, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, "bottom-up",
            FALSE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(BMP, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, "bottom-up", FALSE, 0,
            "");
  TESTWRITE(BMP, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, "top-down", TRUE,
            -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(BMP, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, "top-down", TRUE,
            -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(BMP, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, "top-down", TRUE, 0,
            "");

  fprintf(stderr, "\n");

  /***************************************************************************/

  fprintf(stderr, "Testing GIF reader ...\n");

  TESTREAD(GIF, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, 0,
           "non-interlaced", FALSE, -2,
           "Maximum supported image dimension is 65500 pixels");
  TESTREAD(GIF, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, 0,
           "non-interlaced", FALSE, -2,
           "Maximum supported image dimension is 65500 pixels");
  TESTREAD(GIF, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, 0, "non-interlaced",
           FALSE, 0, "");
  TESTREAD(GIF, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, 0, "interlaced",
           TRUE, -2, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(GIF, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, 0, "interlaced",
           TRUE, -2, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(GIF, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, 0, "interlaced", TRUE,
           0, "");

  TESTREAD(GIF, JPEG_MAX_DIMENSION, 15268L, 1000000000, "non-interlaced",
           FALSE, -2,
           "Maximum supported image dimension is 1000000000 pixels");
  TESTREAD(GIF, JPEG_MAX_DIMENSION, 15267L, 1000000000, "non-interlaced",
           FALSE, 0, "");
  TESTREAD(GIF, 1527L, JPEG_MAX_DIMENSION, 100000000, "non-interlaced", FALSE,
           -2, "Maximum supported image dimension is 100000000 pixels");
  TESTREAD(GIF, 1526L, JPEG_MAX_DIMENSION, 100000000, "non-interlaced", FALSE,
           0, "");
  TESTREAD(GIF, 1000L, 1001L, 1000000, "interlaced", TRUE, -2,
           "Maximum supported image dimension is 1000000 pixels");
  TESTREAD(GIF, 1000L, 1000L, 1000000, "interlaced", TRUE, 0, "");

  fprintf(stderr, "\n");

  fprintf(stderr, "Testing GIF writer ...\n");

  TESTWRITE(GIF, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, "non-interlaced",
            FALSE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(GIF, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, "non-interlaced",
            FALSE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(GIF, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, "non-interlaced",
            FALSE, 0, "");
  TESTWRITE(GIF, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, "interlaced",
            TRUE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(GIF, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, "interlaced",
            TRUE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(GIF, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, "interlaced", TRUE, 0,
            "");

  fprintf(stderr, "\n");

  /***************************************************************************/

  fprintf(stderr, "Testing PNG reader ...\n");

  TESTREAD(PNG, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, 0, "8-bit", 8, -1,
           "Maximum supported image dimension is 65500 pixels");
  TESTREAD(PNG, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, 0, "12-bit", 12,
           -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(PNG, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, 0, "16-bit", 16,
           -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(PNG, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, 0, "16-bit", 16, -1,
           "unexpected end of file");
  TESTREAD(PNG, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, 0, "17-bit", 17,
           -1, "Unsupported JPEG data precision 17");

  TESTREAD(PNG, JPEG_MAX_DIMENSION, 15268L, 1000000000, "8-bit", 8, -1,
           "Maximum supported image dimension is 1000000000 pixels");
  TESTREAD(PNG, JPEG_MAX_DIMENSION, 15267L, 1000000000, "8-bit", 8, -1,
           "unexpected end of file");
  TESTREAD(PNG, 1527L, JPEG_MAX_DIMENSION, 100000000, "12-bit", 12, -1,
           "Maximum supported image dimension is 100000000 pixels");
  TESTREAD(PNG, 1526L, JPEG_MAX_DIMENSION, 100000000, "12-bit", 12, -1,
           "unexpected end of file");
  TESTREAD(PNG, 1000L, 1001L, 1000000, "16-bit", 16, -1,
           "Maximum supported image dimension is 1000000 pixels");
  TESTREAD(PNG, 1000L, 1000L, 1000000, "16-bit", 16, -1,
           "unexpected end of file");

  fprintf(stderr, "\n");

  fprintf(stderr, "Testing PNG writer ...\n");

  TESTWRITE(PNG, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, "8-bit", 8,
            -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(PNG, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, "12-bit", 12,
            -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(PNG, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, "16-bit", 16,
            -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(PNG, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, "16-bit", 16, 0, "");
  TESTWRITE(PNG, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, "17-bit", 17,
            -1, "Unsupported JPEG data precision 17");

  fprintf(stderr, "\n");

  /***************************************************************************/

  fprintf(stderr, "Testing PPM reader ...\n");

  TESTREAD(PPM, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, 0, "8-bit", 8, -1,
           "Maximum supported image dimension is 65500 pixels");
  TESTREAD(PPM, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, 0, "12-bit", 12,
           -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(PPM, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, 0, "16-bit", 16,
           -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(PPM, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, 0, "16-bit", 16, 0,
           "");
  TESTREAD(PPM, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, 0, "17-bit", 17,
           -1, "Unsupported JPEG data precision 17");

  TESTREAD(PPM, JPEG_MAX_DIMENSION, 15268L, 1000000000, "8-bit", 8, -1,
           "Maximum supported image dimension is 1000000000 pixels");
  TESTREAD(PPM, JPEG_MAX_DIMENSION, 15267L, 1000000000, "8-bit", 8, 0, "");
  TESTREAD(PPM, 1527L, JPEG_MAX_DIMENSION, 100000000, "12-bit", 12, -1,
           "Maximum supported image dimension is 100000000 pixels");
  TESTREAD(PPM, 1526L, JPEG_MAX_DIMENSION, 100000000, "12-bit", 12, 0, "");
  TESTREAD(PPM, 1000L, 1001L, 1000000, "16-bit", 16, -1,
           "Maximum supported image dimension is 1000000 pixels");
  TESTREAD(PPM, 1000L, 1000L, 1000000, "16-bit", 16, 0, "");

  fprintf(stderr, "\n");

  fprintf(stderr, "Testing PPM writer ...\n");

  TESTWRITE(PPM, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, "8-bit", 8,
            -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(PPM, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, "12-bit", 12,
            -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(PPM, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, "16-bit", 16,
            -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(PPM, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, "16-bit", 16, 0, "");
  TESTWRITE(PPM, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, "17-bit", 17,
            -1, "Unsupported JPEG data precision 17");

  fprintf(stderr, "\n");

  /***************************************************************************/

  fprintf(stderr, "Testing Targa reader ...\n");

  TESTREAD(TGA, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, 0, "bottom-up",
           FALSE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(TGA, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, 0, "bottom-up",
           FALSE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(TGA, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, 0, "bottom-up", FALSE,
           0, "");
  TESTREAD(TGA, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, 0, "top-down",
           TRUE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(TGA, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, 0, "top-down",
           TRUE, -1, "Maximum supported image dimension is 65500 pixels");
  TESTREAD(TGA, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, 0, "top-down", TRUE,
           0, "");

  TESTREAD(TGA, JPEG_MAX_DIMENSION, 15268L, 1000000000, "bottom-up", FALSE, -1,
           "Maximum supported image dimension is 1000000000 pixels");
  TESTREAD(TGA, JPEG_MAX_DIMENSION, 15267L, 1000000000, "bottom-up", FALSE, 0,
           "");
  TESTREAD(TGA, 1527L, JPEG_MAX_DIMENSION, 100000000, "bottom-up", FALSE, -1,
           "Maximum supported image dimension is 100000000 pixels");
  TESTREAD(TGA, 1526L, JPEG_MAX_DIMENSION, 100000000, "bottom-up", FALSE, 0,
           "");
  TESTREAD(TGA, 1000L, 1001L, 1000000, "top-down", TRUE, -1,
           "Maximum supported image dimension is 1000000 pixels");
  TESTREAD(TGA, 1000L, 1000L, 1000000, "top-down", TRUE, 0, "");

  fprintf(stderr, "\n");

  fprintf(stderr, "Testing Targa writer ...\n");

  TESTWRITE(TGA, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION + 1, "top-down", 0,
            -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(TGA, JPEG_MAX_DIMENSION + 1, JPEG_MAX_DIMENSION, "top-down", 0,
            -1, "Maximum supported image dimension is 65500 pixels");
  TESTWRITE(TGA, JPEG_MAX_DIMENSION, JPEG_MAX_DIMENSION, "top-down", 0, 0, "");

  fprintf(stderr, "\n");

  /***************************************************************************/

  fprintf(stderr, "GREAT SUCCESS!\n");
  return 0;
}

