/* Tests of the C interface (bind/c/vio/vio.h).  Usage: c-test <testimages dir> <scratch dir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vio.h"

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      fprintf(stderr, "%s:%d: CHECK(%s) failed (%s)\n", __FILE__, __LINE__, #cond, \
              vio_last_error());                                       \
      failures++;                                                      \
    }                                                                  \
  } while (0)

static const char* dir;
static const char* scratch;

static const char* path(const char* base, const char* name) {
  static char buf[4][1024];
  static int k = 0;
  k = (k + 1) & 3;
  snprintf(buf[k], sizeof buf[k], "%s/%s", base, name);
  return buf[k];
}

static size_t image_bytes(const vio_image* im) { return im->stride * im->height; }

static int same_pixels(const vio_image* a, const vio_image* b) {
  uint32_t y;
  size_t row;
  if (a->width != b->width || a->height != b->height || a->channels != b->channels || a->depth != b->depth)
    return 0;
  row = (size_t)a->width * a->channels * (a->depth / 8);
  for (y = 0; y < a->height; y++)
    if (memcmp((const char*)a->data + y * a->stride, (const char*)b->data + y * b->stride, row) != 0) return 0;
  return 1;
}

/* Lossless formats give back what was written, in every layout they hold. */
static void test_roundtrip(void) {
  static const vio_format formats[] = {VIO_FORMAT_PNG, VIO_FORMAT_PPM, VIO_FORMAT_BMP, VIO_FORMAT_TGA};
  vio_image src;
  size_t f;
  uint32_t ch, depth;
  CHECK(vio_load(path(dir, "testorig.ppm"), 0, 0, &src) == VIO_OK);
  CHECK(src.width == 227 && src.height == 149 && src.channels == 3 && src.depth == 8);
  for (f = 0; f < sizeof formats / sizeof formats[0]; f++) {
    for (ch = 1; ch <= 4; ch++) {
      for (depth = 8; depth <= 16; depth += 8) {
        vio_image in, out;
        void* file;
        size_t size;
        const vio_format fmt = formats[f];
        const int keeps_alpha = fmt == VIO_FORMAT_PNG;
        const int keeps_16 = fmt == VIO_FORMAT_PNG || fmt == VIO_FORMAT_PPM;
        const uint32_t want_ch = keeps_alpha ? ch : (ch >= 3 ? 3 : 1);
        const uint32_t want_depth = keeps_16 ? depth : 8;
        /* source in the layout under test */
        {
          void* png;
          size_t png_size;
          CHECK(vio_encode(&src, VIO_FORMAT_PNG, NULL, &png, &png_size) == VIO_OK);
          CHECK(vio_decode(png, png_size, ch, depth, &in) == VIO_OK);
          vio_free(png);
        }
        CHECK(in.channels == ch && in.depth == depth);
        CHECK(vio_encode(&in, fmt, NULL, &file, &size) == VIO_OK);
        CHECK(vio_detect(file, size) == fmt);
        CHECK(vio_decode(file, size, 0, 0, &out) == VIO_OK);
        CHECK(out.channels == want_ch && out.depth == want_depth);
        if (want_ch == ch && want_depth == depth) {
          CHECK(same_pixels(&in, &out));
        } else {
          vio_image ref;
          void* png;
          size_t png_size;
          CHECK(vio_encode(&in, VIO_FORMAT_PNG, NULL, &png, &png_size) == VIO_OK);
          CHECK(vio_decode(png, png_size, want_ch, want_depth, &ref) == VIO_OK);
          CHECK(same_pixels(&ref, &out));
          vio_free(png);
          vio_image_free(&ref);
        }
        vio_free(file);
        vio_image_free(&in);
        vio_image_free(&out);
      }
    }
  }
  vio_image_free(&src);
}

static void test_jpeg(void) {
  vio_image a, b, c;
  vio_info info;
  void* file;
  size_t size;
  vio_options opt;
  CHECK(vio_load(path(dir, "testorig.jpg"), 0, 0, &a) == VIO_OK);
  CHECK(a.width == 227 && a.height == 149 && a.channels == 3 && a.depth == 8);
  CHECK(vio_load(path(dir, "testorig.jpg"), 4, 0, &b) == VIO_OK);
  CHECK(b.channels == 4 && ((unsigned char*)b.data)[3] == 255);
  CHECK(memcmp(a.data, b.data, 3) == 0);
  vio_image_free(&b);
  CHECK(vio_load(path(dir, "testorig.jpg"), 1, 16, &b) == VIO_OK);
  CHECK(b.channels == 1 && b.depth == 16 && b.stride == 227 * 2);
  vio_image_free(&b);

  memset(&opt, 0, sizeof opt);
  opt.quality = 95;
  opt.subsampling = 444;
  opt.progressive = 1;
  CHECK(vio_encode(&a, VIO_FORMAT_JPEG, &opt, &file, &size) == VIO_OK);
  CHECK(vio_probe(file, size, &info) == VIO_OK);
  CHECK(info.format == VIO_FORMAT_JPEG && info.width == 227 && info.height == 149 && info.channels == 3);
  CHECK(vio_decode(file, size, 0, 0, &c) == VIO_OK);
  {
    /* close to the original */
    size_t i, n = image_bytes(&a);
    double err = 0;
    for (i = 0; i < n; i++) {
      int d = ((unsigned char*)a.data)[i] - ((unsigned char*)c.data)[i];
      err += d < 0 ? -d : d;
    }
    CHECK(err / (double)n < 4);
  }
  vio_free(file);
  vio_image_free(&c);
  vio_image_free(&a);

  /* 12-bit JPEG: 16-bit output, full range */
  CHECK(vio_load(path(dir, "monkey12.jpg"), 0, 0, &a) == VIO_OK);
  CHECK(a.depth == 16);
  vio_image_free(&a);
}

static void test_png16(void) {
  vio_image a, b;
  vio_info info;
  CHECK(vio_load(path(dir, "monkey16.png"), 0, 0, &a) == VIO_OK);
  CHECK(a.width == 149 && a.height == 227 && a.channels == 3 && a.depth == 16);
  CHECK(vio_load(path(dir, "monkey16.pgm"), 0, 0, &b) == VIO_OK);
  CHECK(b.width == 149 && b.channels == 1 && b.depth == 16);
  vio_image_free(&b);
  CHECK(vio_load(path(dir, "monkey16.png"), 0, 8, &b) == VIO_OK);
  CHECK(b.depth == 8 && ((unsigned char*)b.data)[0] == (((uint16_t*)a.data)[0] * 255u + 32767) / 65535);
  vio_image_free(&a);
  vio_image_free(&b);
  CHECK(vio_load(path(dir, "testorig.png"), 0, 0, &a) == VIO_OK);
  CHECK(a.width == 227 && a.channels == 1 && a.depth == 8);
  vio_image_free(&a);
  {
    FILE* f = fopen(path(dir, "monkey16.png"), "rb");
    static unsigned char buf[1 << 16];
    size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    CHECK(vio_probe(buf, n, &info) == VIO_OK);
    CHECK(info.format == VIO_FORMAT_PNG && info.depth == 16);
  }
}

static void test_files(void) {
  vio_image a, b;
  CHECK(vio_load(path(dir, "testorig.ppm"), 0, 0, &a) == VIO_OK);
  CHECK(vio_save(path(scratch, "c-test.png"), &a, NULL) == VIO_OK);
  CHECK(vio_load(path(scratch, "c-test.png"), 0, 0, &b) == VIO_OK);
  CHECK(same_pixels(&a, &b));
  vio_image_free(&b);
  CHECK(vio_save(path(scratch, "c-test.JPG"), &a, NULL) == VIO_OK);
  CHECK(vio_load(path(scratch, "c-test.JPG"), 0, 0, &b) == VIO_OK);
  vio_image_free(&b);
  CHECK(vio_load(path(dir, "shira_bird8.bmp"), 0, 0, &b) == VIO_OK);
  vio_image_free(&b);
  vio_image_free(&a);
}

static void test_errors(void) {
  vio_image a;
  static const unsigned char junk[] = "not an image";
  static const unsigned char trunc[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0};
  CHECK(vio_decode(junk, sizeof junk, 0, 0, &a) == VIO_ERROR_FORMAT);
  CHECK(strlen(vio_last_error()) > 0);
  CHECK(vio_decode(trunc, sizeof trunc, 0, 0, &a) == VIO_ERROR_DECODE);
  CHECK(a.data == NULL);
  CHECK(vio_decode(junk, sizeof junk, 5, 0, &a) == VIO_ERROR_ARGUMENT);
  CHECK(vio_load(path(scratch, "does-not-exist.png"), 0, 0, &a) == VIO_ERROR_IO);
  CHECK(vio_save(path(scratch, "x.unknown"), &a, NULL) == VIO_ERROR_FORMAT);
  CHECK(vio_format_from_path("a/b.Jpeg") == VIO_FORMAT_JPEG);
  CHECK(strcmp(vio_format_name(VIO_FORMAT_TGA), "tga") == 0);
  CHECK(strcmp(vio_version(), "0.1.0") == 0);
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <testimages dir> <scratch dir>\n", argv[0]);
    return 2;
  }
  dir = argv[1];
  scratch = argv[2];
  test_roundtrip();
  test_jpeg();
  test_png16();
  test_files();
  test_errors();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all C interface tests passed\n");
  return 0;
}
