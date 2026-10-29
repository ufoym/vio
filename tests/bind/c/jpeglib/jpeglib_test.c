/*
 * jpeglib-test: checks of the jpeglib.h API (bind/c/jpeglib) beyond what
 * libjpeg-turbo's programs exercise: suspending data sources, buffered-image
 * mode, marker processors, abbreviated datastreams, error recovery,
 * concatenated images, raw (downsampled) data and progress monitoring.
 * Results are compared with plain decoding/encoding of the same images.
 *
 *   jpeglib-test <testimages directory>
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpeglib.h"
#include "jerror.h"
#include "turbojpeg.h"

static int failures = 0;

#define CHECK(cond, ...)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__);      \
      fprintf(stderr, __VA_ARGS__);                               \
      fprintf(stderr, "\n");                                      \
      failures++;                                                 \
    }                                                             \
  } while (0)

/* ---- helpers ------------------------------------------------------------ */

typedef struct {
  unsigned char *data;
  size_t size;
} buffer;

static buffer read_file(const char *dir, const char *name)
{
  char path[1024];
  buffer b = { NULL, 0 };
  FILE *f;
  long n;

  snprintf(path, sizeof(path), "%s/%s", dir, name);
  if ((f = fopen(path, "rb")) == NULL) {
    fprintf(stderr, "can't open %s\n", path);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  b.data = (unsigned char *)malloc(n);
  b.size = fread(b.data, 1, n, f);
  fclose(f);
  return b;
}

struct error_mgr {
  struct jpeg_error_mgr pub;
  jmp_buf jmp;
  char msg[JMSG_LENGTH_MAX];
};

static void error_exit(j_common_ptr cinfo)
{
  struct error_mgr *err = (struct error_mgr *)cinfo->err;

  (*cinfo->err->format_message) (cinfo, err->msg);
  longjmp(err->jmp, 1);
}

static void quiet_output(j_common_ptr cinfo)
{
  (void)cinfo;
}

static void init_err(struct jpeg_error_mgr *pub, struct error_mgr *err)
{
  (void)pub;
  jpeg_std_error(&err->pub);
  err->pub.error_exit = error_exit;
  err->pub.output_message = quiet_output;
  err->msg[0] = 0;
}

/* Decodes a whole image with an in-memory source: samples, row by row. */
static buffer decode_plain(const buffer *jpeg, J_COLOR_SPACE cs,
                           JDIMENSION *w, JDIMENSION *h, int *ncomp)
{
  struct jpeg_decompress_struct d;
  struct error_mgr err;
  buffer out = { NULL, 0 };

  d.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    fprintf(stderr, "decode_plain: %s\n", err.msg);
    jpeg_destroy_decompress(&d);
    failures++;
    return out;
  }
  jpeg_create_decompress(&d);
  jpeg_mem_src(&d, jpeg->data, (unsigned long)jpeg->size);
  jpeg_read_header(&d, TRUE);
  d.out_color_space = cs;
  jpeg_start_decompress(&d);
  *w = d.output_width;
  *h = d.output_height;
  *ncomp = d.output_components;
  out.size = (size_t)d.output_width * d.output_height * d.output_components;
  out.data = (unsigned char *)malloc(out.size);
  while (d.output_scanline < d.output_height) {
    JSAMPROW row = out.data + (size_t)d.output_scanline * d.output_width *
                   d.output_components;
    jpeg_read_scanlines(&d, &row, 1);
  }
  jpeg_finish_decompress(&d);
  jpeg_destroy_decompress(&d);
  return out;
}

/* Encodes interleaved RGB samples into memory. */
static buffer encode_plain(const unsigned char *pixels, JDIMENSION w,
                           JDIMENSION h, int quality, boolean progressive)
{
  struct jpeg_compress_struct c;
  struct error_mgr err;
  buffer out = { NULL, 0 };
  unsigned long size = 0;

  c.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    fprintf(stderr, "encode_plain: %s\n", err.msg);
    jpeg_destroy_compress(&c);
    failures++;
    return out;
  }
  jpeg_create_compress(&c);
  jpeg_mem_dest(&c, &out.data, &size);
  c.image_width = w;
  c.image_height = h;
  c.input_components = 3;
  c.in_color_space = JCS_RGB;
  jpeg_set_defaults(&c);
  jpeg_set_quality(&c, quality, TRUE);
  if (progressive) jpeg_simple_progression(&c);
  jpeg_start_compress(&c, TRUE);
  while (c.next_scanline < c.image_height) {
    JSAMPROW row = (JSAMPROW)pixels + (size_t)c.next_scanline * w * 3;
    jpeg_write_scanlines(&c, &row, 1);
  }
  jpeg_finish_compress(&c);
  jpeg_destroy_compress(&c);
  out.size = size;
  return out;
}

static int same(const buffer *a, const buffer *b)
{
  return a->size == b->size && a->data && b->data &&
         memcmp(a->data, b->data, a->size) == 0;
}

/* ---- suspending data source --------------------------------------------- */

/* Delivers the data a few bytes at a time and suspends before every other
   chunk. */
struct trickle_src {
  struct jpeg_source_mgr pub;
  const unsigned char *data;
  size_t size, pos;
  int calls;
};

static void trickle_init(j_decompress_ptr cinfo)
{
  (void)cinfo;
}

static boolean trickle_fill(j_decompress_ptr cinfo)
{
  struct trickle_src *src = (struct trickle_src *)cinfo->src;
  size_t n;

  if (src->calls++ % 2 == 0) return FALSE;
  if (src->pos >= src->size) {
    static const JOCTET eoi[2] = { 0xFF, JPEG_EOI };
    src->pub.next_input_byte = eoi;
    src->pub.bytes_in_buffer = 2;
    return TRUE;
  }
  n = 1 + (src->pos * 7919) % 333;
  if (n > src->size - src->pos) n = src->size - src->pos;
  src->pub.next_input_byte = src->data + src->pos;
  src->pub.bytes_in_buffer = n;
  src->pos += n;
  return TRUE;
}

static void trickle_skip(j_decompress_ptr cinfo, long num_bytes)
{
  struct trickle_src *src = (struct trickle_src *)cinfo->src;

  while (num_bytes > (long)src->pub.bytes_in_buffer) {
    num_bytes -= (long)src->pub.bytes_in_buffer;
    src->pub.bytes_in_buffer = 0;
    while (!trickle_fill(cinfo)) {}
  }
  src->pub.next_input_byte += num_bytes;
  src->pub.bytes_in_buffer -= num_bytes;
}

static void trickle_term(j_decompress_ptr cinfo)
{
  (void)cinfo;
}

static void test_suspension(const buffer *jpeg, const char *name)
{
  struct jpeg_decompress_struct d;
  struct error_mgr err;
  struct trickle_src src;
  buffer ref, out = { NULL, 0 };
  JDIMENSION w, h;
  int ncomp, suspensions = 0;

  ref = decode_plain(jpeg, JCS_RGB, &w, &h, &ncomp);
  d.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    CHECK(0, "%s: suspension: %s", name, err.msg);
    jpeg_destroy_decompress(&d);
    free(ref.data);
    free(out.data);
    return;
  }
  jpeg_create_decompress(&d);
  memset(&src, 0, sizeof(src));
  src.pub.init_source = trickle_init;
  src.pub.fill_input_buffer = trickle_fill;
  src.pub.skip_input_data = trickle_skip;
  src.pub.resync_to_restart = jpeg_resync_to_restart;
  src.pub.term_source = trickle_term;
  src.data = jpeg->data;
  src.size = jpeg->size;
  d.src = &src.pub;
  while (jpeg_read_header(&d, TRUE) == JPEG_SUSPENDED) suspensions++;
  d.out_color_space = JCS_RGB;
  while (!jpeg_start_decompress(&d)) suspensions++;
  out.size = (size_t)d.output_width * d.output_height * d.output_components;
  out.data = (unsigned char *)malloc(out.size);
  while (d.output_scanline < d.output_height) {
    JSAMPROW row = out.data + (size_t)d.output_scanline * d.output_width *
                   d.output_components;
    if (jpeg_read_scanlines(&d, &row, 1) == 0) suspensions++;
  }
  while (!jpeg_finish_decompress(&d)) suspensions++;
  jpeg_destroy_decompress(&d);
  CHECK(suspensions > 0, "%s: the source never suspended", name);
  CHECK(same(&ref, &out), "%s: suspended decoding differs", name);
  free(ref.data);
  free(out.data);
}

/* ---- buffered-image mode ------------------------------------------------ */

static void test_buffered_image(const buffer *jpeg, const char *name)
{
  struct jpeg_decompress_struct d;
  struct error_mgr err;
  buffer ref, out = { NULL, 0 };
  JDIMENSION w, h;
  int ncomp, passes = 0;

  ref = decode_plain(jpeg, JCS_RGB, &w, &h, &ncomp);
  d.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    CHECK(0, "%s: buffered image: %s", name, err.msg);
    jpeg_destroy_decompress(&d);
    free(ref.data);
    free(out.data);
    return;
  }
  jpeg_create_decompress(&d);
  jpeg_mem_src(&d, jpeg->data, (unsigned long)jpeg->size);
  jpeg_read_header(&d, TRUE);
  CHECK(jpeg_has_multiple_scans(&d), "%s: not a multi-scan image", name);
  d.out_color_space = JCS_RGB;
  d.buffered_image = TRUE;
  jpeg_start_decompress(&d);
  out.size = (size_t)d.output_width * d.output_height * d.output_components;
  out.data = (unsigned char *)malloc(out.size);
  for (;;) {
    int status;
    do {
      status = jpeg_consume_input(&d);
    } while (status != JPEG_SUSPENDED && status != JPEG_REACHED_EOI &&
             status != JPEG_REACHED_SOS);
    jpeg_start_output(&d, d.input_scan_number);
    while (d.output_scanline < d.output_height) {
      JSAMPROW row = out.data + (size_t)d.output_scanline * d.output_width *
                     d.output_components;
      jpeg_read_scanlines(&d, &row, 1);
    }
    jpeg_finish_output(&d);
    passes++;
    if (jpeg_input_complete(&d) && d.output_scan_number == d.input_scan_number)
      break;
  }
  jpeg_finish_decompress(&d);
  jpeg_destroy_decompress(&d);
  CHECK(passes > 1, "%s: only %d output pass(es)", name, passes);
  CHECK(same(&ref, &out), "%s: the final buffered-image pass differs", name);
  free(ref.data);
  free(out.data);
}

/* ---- markers ------------------------------------------------------------ */

static char seen_com[256], seen_app1[256];

static int next_byte(j_decompress_ptr cinfo)
{
  struct jpeg_source_mgr *src = cinfo->src;

  if (src->bytes_in_buffer == 0 && !(*src->fill_input_buffer) (cinfo))
    ERREXIT(cinfo, JERR_CANT_SUSPEND);
  src->bytes_in_buffer--;
  return *src->next_input_byte++;
}

static boolean read_text_marker(j_decompress_ptr cinfo)
{
  char *dst = cinfo->unread_marker == JPEG_COM ? seen_com : seen_app1;
  long length = (long)next_byte(cinfo) << 8;
  int i = 0;

  length += next_byte(cinfo) - 2;
  while (length-- > 0) {
    int c = next_byte(cinfo);
    if (i < 255) dst[i++] = (char)c;
  }
  dst[i] = 0;
  return TRUE;
}

static void test_markers(const unsigned char *pixels, JDIMENSION w,
                         JDIMENSION h)
{
  static const char comment[] = "a comment", app1[] = "APP1 data";
  struct jpeg_compress_struct c;
  struct jpeg_decompress_struct d;
  struct error_mgr err;
  buffer jpeg = { NULL, 0 }, ref, out = { NULL, 0 };
  unsigned long size = 0;
  JDIMENSION ow, oh;
  int ncomp;

  c.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    CHECK(0, "markers: %s", err.msg);
    return;
  }
  jpeg_create_compress(&c);
  jpeg_mem_dest(&c, &jpeg.data, &size);
  c.image_width = w;
  c.image_height = h;
  c.input_components = 3;
  c.in_color_space = JCS_RGB;
  jpeg_set_defaults(&c);
  jpeg_start_compress(&c, TRUE);
  jpeg_write_marker(&c, JPEG_COM, (const JOCTET *)comment,
                    (unsigned int)strlen(comment));
  jpeg_write_m_header(&c, JPEG_APP0 + 1, (unsigned int)strlen(app1));
  {
    const char *p;
    for (p = app1; *p; p++) jpeg_write_m_byte(&c, *p);
  }
  while (c.next_scanline < c.image_height) {
    JSAMPROW row = (JSAMPROW)pixels + (size_t)c.next_scanline * w * 3;
    jpeg_write_scanlines(&c, &row, 1);
  }
  jpeg_finish_compress(&c);
  jpeg_destroy_compress(&c);
  jpeg.size = size;

  /* Saved markers */
  d.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    CHECK(0, "markers: %s", err.msg);
    jpeg_destroy_decompress(&d);
    free(jpeg.data);
    return;
  }
  jpeg_create_decompress(&d);
  jpeg_save_markers(&d, JPEG_COM, 0xFFFF);
  jpeg_save_markers(&d, JPEG_APP0 + 1, 4);
  jpeg_mem_src(&d, jpeg.data, (unsigned long)jpeg.size);
  jpeg_read_header(&d, TRUE);
  {
    jpeg_saved_marker_ptr m = d.marker_list;
    CHECK(m && m->marker == JPEG_COM && m->data_length == strlen(comment) &&
          memcmp(m->data, comment, m->data_length) == 0, "saved COM marker");
    m = m ? m->next : NULL;
    CHECK(m && m->marker == JPEG_APP0 + 1 && m->data_length == 4 &&
          m->original_length == strlen(app1) && memcmp(m->data, app1, 4) == 0,
          "saved APP1 marker (truncated)");
  }
  jpeg_abort_decompress(&d);

  /* Marker processors */
  jpeg_set_marker_processor(&d, JPEG_COM, read_text_marker);
  jpeg_set_marker_processor(&d, JPEG_APP0 + 1, read_text_marker);
  jpeg_mem_src(&d, jpeg.data, (unsigned long)jpeg.size);
  jpeg_read_header(&d, TRUE);
  CHECK(strcmp(seen_com, comment) == 0, "COM processor saw \"%s\"", seen_com);
  CHECK(strcmp(seen_app1, app1) == 0, "APP1 processor saw \"%s\"", seen_app1);
  d.out_color_space = JCS_RGB;
  jpeg_start_decompress(&d);
  out.size = (size_t)d.output_width * d.output_height * d.output_components;
  out.data = (unsigned char *)malloc(out.size);
  while (d.output_scanline < d.output_height) {
    JSAMPROW row = out.data + (size_t)d.output_scanline * d.output_width *
                   d.output_components;
    jpeg_read_scanlines(&d, &row, 1);
  }
  jpeg_finish_decompress(&d);
  jpeg_destroy_decompress(&d);
  ref = decode_plain(&jpeg, JCS_RGB, &ow, &oh, &ncomp);
  CHECK(same(&ref, &out), "decoding with marker processors differs");
  free(ref.data);
  free(out.data);
  free(jpeg.data);
}

/* ---- abbreviated datastreams -------------------------------------------- */

static void test_abbreviated(const unsigned char *pixels, JDIMENSION w,
                             JDIMENSION h)
{
  struct jpeg_compress_struct c;
  struct jpeg_decompress_struct d;
  struct error_mgr err;
  buffer tables = { NULL, 0 }, image = { NULL, 0 }, full, ref, out = { NULL, 0 };
  unsigned long tsize = 0, isize = 0;
  JDIMENSION ow, oh;
  int ncomp;

  c.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    CHECK(0, "abbreviated: %s", err.msg);
    return;
  }
  jpeg_create_compress(&c);
  c.image_width = w;
  c.image_height = h;
  c.input_components = 3;
  c.in_color_space = JCS_RGB;
  jpeg_set_defaults(&c);
  jpeg_set_quality(&c, 85, TRUE);
  jpeg_mem_dest(&c, &tables.data, &tsize);
  jpeg_write_tables(&c);
  CHECK(c.quant_tbl_ptrs[0]->sent_table, "tables not marked as sent");
  jpeg_mem_dest(&c, &image.data, &isize);
  jpeg_start_compress(&c, FALSE);
  while (c.next_scanline < c.image_height) {
    JSAMPROW row = (JSAMPROW)pixels + (size_t)c.next_scanline * w * 3;
    jpeg_write_scanlines(&c, &row, 1);
  }
  jpeg_finish_compress(&c);
  jpeg_destroy_compress(&c);
  tables.size = tsize;
  image.size = isize;
  full = encode_plain(pixels, w, h, 85, FALSE);
  CHECK(image.size < full.size, "abbreviated image is not smaller");

  d.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    CHECK(0, "abbreviated: %s", err.msg);
    jpeg_destroy_decompress(&d);
    return;
  }
  jpeg_create_decompress(&d);
  jpeg_mem_src(&d, tables.data, (unsigned long)tables.size);
  CHECK(jpeg_read_header(&d, FALSE) == JPEG_HEADER_TABLES_ONLY,
        "tables-only datastream not recognized");
  jpeg_mem_src(&d, image.data, (unsigned long)image.size);
  CHECK(jpeg_read_header(&d, TRUE) == JPEG_HEADER_OK, "abbreviated image");
  d.out_color_space = JCS_RGB;
  jpeg_start_decompress(&d);
  out.size = (size_t)d.output_width * d.output_height * d.output_components;
  out.data = (unsigned char *)malloc(out.size);
  while (d.output_scanline < d.output_height) {
    JSAMPROW row = out.data + (size_t)d.output_scanline * d.output_width *
                   d.output_components;
    jpeg_read_scanlines(&d, &row, 1);
  }
  jpeg_finish_decompress(&d);
  jpeg_destroy_decompress(&d);
  ref = decode_plain(&full, JCS_RGB, &ow, &oh, &ncomp);
  CHECK(same(&ref, &out), "abbreviated decoding differs");
  free(tables.data);
  free(image.data);
  free(full.data);
  free(ref.data);
  free(out.data);
}

/* ---- error recovery, concatenated images --------------------------------- */

static void test_recovery(const buffer *jpeg)
{
  struct jpeg_decompress_struct d;
  struct error_mgr err;
  static const unsigned char garbage[] = { 0x12, 0x34, 0x56, 0x78 };
  buffer two, ref, out = { NULL, 0 };
  JDIMENSION w, h;
  int ncomp, images = 0, failed = 0;
  volatile int stage = 0;

  ref = decode_plain(jpeg, JCS_RGB, &w, &h, &ncomp);
  d.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  jpeg_create_decompress(&d);
  if (setjmp(err.jmp)) {
    failed = 1;
    jpeg_abort_decompress(&d);
  }
  if (stage == 0) {
    stage = 1;
    jpeg_mem_src(&d, garbage, sizeof(garbage));
    jpeg_read_header(&d, TRUE);
    CHECK(0, "no error for a non-JPEG input");
  }
  CHECK(failed && strstr(err.msg, "Not a JPEG file"), "error: \"%s\"", err.msg);
  if (stage == 2) {
    CHECK(0, "concatenated images: %s", err.msg);
    jpeg_destroy_decompress(&d);
    free(ref.data);
    free(out.data);
    return;
  }
  stage = 2;

  /* The same object, on two concatenated images */
  two.size = jpeg->size * 2;
  two.data = (unsigned char *)malloc(two.size);
  memcpy(two.data, jpeg->data, jpeg->size);
  memcpy(two.data + jpeg->size, jpeg->data, jpeg->size);
  jpeg_mem_src(&d, two.data, (unsigned long)two.size);
  out.size = ref.size;
  out.data = (unsigned char *)malloc(out.size);
  while (images < 2 && jpeg_read_header(&d, TRUE) == JPEG_HEADER_OK) {
    d.out_color_space = JCS_RGB;
    jpeg_start_decompress(&d);
    memset(out.data, 0, out.size);
    while (d.output_scanline < d.output_height) {
      JSAMPROW row = out.data + (size_t)d.output_scanline * d.output_width *
                     d.output_components;
      jpeg_read_scanlines(&d, &row, 1);
    }
    jpeg_finish_decompress(&d);
    CHECK(same(&ref, &out), "concatenated image %d differs", images + 1);
    images++;
  }
  CHECK(images == 2, "%d of 2 concatenated images decoded", images);
  CHECK(d.src->bytes_in_buffer == 0, "%lu bytes left after the images",
        (unsigned long)d.src->bytes_in_buffer);
  jpeg_destroy_decompress(&d);
  free(two.data);
  free(ref.data);
  free(out.data);
}

/* ---- raw data ----------------------------------------------------------- */

/* Raw (downsampled) decoding and encoding must agree with the TurboJPEG
   YUV functions. */
static void test_raw(const buffer *jpeg)
{
  struct jpeg_decompress_struct d;
  struct jpeg_compress_struct c;
  struct error_mgr err;
  unsigned char *planes[3] = { NULL, NULL, NULL }, *tjplanes[3];
  int strides[3], ci, ok = 1;
  JDIMENSION pw[3], ph[3];
  buffer raw = { NULL, 0 }, tj = { NULL, 0 };
  unsigned long rawsize = 0;
  tjhandle handle;

  d.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    CHECK(0, "raw data: %s", err.msg);
    return;
  }
  jpeg_create_decompress(&d);
  jpeg_mem_src(&d, jpeg->data, (unsigned long)jpeg->size);
  jpeg_read_header(&d, TRUE);
  d.raw_data_out = TRUE;
  jpeg_start_decompress(&d);
  for (ci = 0; ci < 3; ci++) {
    jpeg_component_info *comp = &d.comp_info[ci];
    pw[ci] = comp->width_in_blocks * DCTSIZE;
    ph[ci] = comp->height_in_blocks * DCTSIZE +
             d.max_v_samp_factor * DCTSIZE;   /* room for the last iMCU row */
    planes[ci] = (unsigned char *)malloc((size_t)pw[ci] * ph[ci]);
  }
  while (d.output_scanline < d.output_height) {
    JSAMPROW rows[3][32];
    JSAMPARRAY arrays[3];
    int r;
    for (ci = 0; ci < 3; ci++) {
      jpeg_component_info *comp = &d.comp_info[ci];
      JDIMENSION y0 = d.output_scanline / d.max_v_samp_factor *
                      comp->v_samp_factor;
      for (r = 0; r < comp->v_samp_factor * DCTSIZE; r++)
        rows[ci][r] = planes[ci] + (size_t)(y0 + r) * pw[ci];
      arrays[ci] = rows[ci];
    }
    jpeg_read_raw_data(&d, arrays, d.max_v_samp_factor * DCTSIZE);
  }
  jpeg_finish_decompress(&d);

  /* The same planes from the TurboJPEG API */
  handle = tj3Init(TJINIT_DECOMPRESS);
  for (ci = 0; ci < 3; ci++) {
    strides[ci] = (int)pw[ci];
    tjplanes[ci] = (unsigned char *)malloc((size_t)pw[ci] * ph[ci]);
  }
  if (tj3DecompressToYUVPlanes8(handle, jpeg->data, jpeg->size, tjplanes,
                                strides) < 0) {
    CHECK(0, "tj3DecompressToYUVPlanes8: %s", tj3GetErrorStr(handle));
    ok = 0;
  }
  for (ci = 0; ok && ci < 3; ci++) {
    int pwidth = tj3YUVPlaneWidth(ci, (int)d.image_width, TJSAMP_420);
    int pheight = tj3YUVPlaneHeight(ci, (int)d.image_height, TJSAMP_420);
    int y;
    for (y = 0; y < pheight; y++)
      if (memcmp(planes[ci] + (size_t)y * pw[ci],
                 tjplanes[ci] + (size_t)y * pw[ci], pwidth) != 0) {
        CHECK(0, "raw decoding: plane %d row %d differs", ci, y);
        break;
      }
  }
  tj3Destroy(handle);

  /* Raw encoding of those planes, with their right and bottom edges
     replicated to whole blocks (as the TurboJPEG API does) */
  for (ci = 0; ci < 3; ci++) {
    int pwidth = tj3YUVPlaneWidth(ci, (int)d.image_width, TJSAMP_420);
    int pheight = tj3YUVPlaneHeight(ci, (int)d.image_height, TJSAMP_420);
    JDIMENSION x, y;
    for (y = 0; y < ph[ci]; y++) {
      unsigned char *row = planes[ci] + (size_t)y * pw[ci];
      if ((int)y >= pheight)
        memcpy(row, planes[ci] + (size_t)(pheight - 1) * pw[ci], pw[ci]);
      for (x = (JDIMENSION)pwidth; x < pw[ci]; x++) row[x] = row[pwidth - 1];
    }
  }
  c.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    CHECK(0, "raw encoding: %s", err.msg);
    jpeg_destroy_compress(&c);
    jpeg_destroy_decompress(&d);
    return;
  }
  jpeg_create_compress(&c);
  jpeg_mem_dest(&c, &raw.data, &rawsize);
  c.image_width = d.image_width;
  c.image_height = d.image_height;
  c.input_components = 3;
  c.in_color_space = JCS_YCbCr;
  jpeg_set_defaults(&c);
  jpeg_set_quality(&c, 95, TRUE);
  c.raw_data_in = TRUE;
  c.dct_method = JDCT_ISLOW;
  jpeg_start_compress(&c, TRUE);
  while (c.next_scanline < c.image_height) {
    JSAMPROW rows[3][32];
    JSAMPARRAY arrays[3];
    int r;
    for (ci = 0; ci < 3; ci++) {
      JDIMENSION y0 = c.next_scanline / c.max_v_samp_factor *
                      c.comp_info[ci].v_samp_factor;
      for (r = 0; r < c.comp_info[ci].v_samp_factor * DCTSIZE; r++) {
        rows[ci][r] = planes[ci] + (size_t)(y0 + r) * pw[ci];
      }
      arrays[ci] = rows[ci];
    }
    jpeg_write_raw_data(&c, arrays, c.max_v_samp_factor * DCTSIZE);
  }
  jpeg_finish_compress(&c);
  jpeg_destroy_compress(&c);
  raw.size = rawsize;

  handle = tj3Init(TJINIT_COMPRESS);
  tj3Set(handle, TJPARAM_QUALITY, 95);
  tj3Set(handle, TJPARAM_SUBSAMP, TJSAMP_420);
  {
    size_t tjsize = 0;
    if (tj3CompressFromYUVPlanes8(handle, (const unsigned char * const *)planes,
                                  (int)d.image_width, strides,
                                  (int)d.image_height, &tj.data, &tjsize) < 0)
      CHECK(0, "tj3CompressFromYUVPlanes8: %s", tj3GetErrorStr(handle));
    tj.size = tjsize;
  }
  tj3Destroy(handle);
  CHECK(same(&raw, &tj), "raw encoding differs from tj3CompressFromYUVPlanes8"
        " (%lu vs %lu bytes)", (unsigned long)raw.size, (unsigned long)tj.size);
  jpeg_destroy_decompress(&d);
  for (ci = 0; ci < 3; ci++) {
    free(planes[ci]);
    free(tjplanes[ci]);
  }
  free(raw.data);
  tj3Free(tj.data);
}

/* ---- suspending data destination ---------------------------------------- */

/* A small buffer that the application drains between calls: the destination
   suspends when it is full (except at the end, where suspension is not
   allowed). */
struct pool_dest {
  struct jpeg_destination_mgr pub;
  JOCTET buf[1000];
  buffer out;
  int suspended, finishing, suspensions;
};

static void pool_drain(struct pool_dest *dest)
{
  size_t n = sizeof(dest->buf) - dest->pub.free_in_buffer;

  dest->out.data = (unsigned char *)realloc(dest->out.data, dest->out.size + n);
  memcpy(dest->out.data + dest->out.size, dest->buf, n);
  dest->out.size += n;
  dest->pub.next_output_byte = dest->buf;
  dest->pub.free_in_buffer = sizeof(dest->buf);
}

static void pool_init(j_compress_ptr cinfo)
{
  struct pool_dest *dest = (struct pool_dest *)cinfo->dest;

  dest->pub.next_output_byte = dest->buf;
  dest->pub.free_in_buffer = sizeof(dest->buf);
}

static boolean pool_empty(j_compress_ptr cinfo)
{
  struct pool_dest *dest = (struct pool_dest *)cinfo->dest;

  if (!dest->finishing) {
    dest->suspended = 1;
    dest->suspensions++;
    return FALSE;
  }
  dest->pub.free_in_buffer = 0;
  pool_drain(dest);
  return TRUE;
}

static void pool_term(j_compress_ptr cinfo)
{
  pool_drain((struct pool_dest *)cinfo->dest);
}

static void test_suspending_dest(void)
{
  struct jpeg_compress_struct c;
  struct error_mgr err;
  struct pool_dest dest;
  buffer ref;
  /* Noisy enough for the compressed data to exceed the library's buffers */
  JDIMENSION w = 640, h = 480, i;
  unsigned char *pixels = (unsigned char *)malloc((size_t)w * h * 3);
  unsigned int seed = 1;

  for (i = 0; i < w * h * 3; i++) {
    seed = seed * 1103515245u + 12345u;
    pixels[i] = (unsigned char)(((i / 3) % w) / 3 + (seed >> 26));
  }
  ref = encode_plain(pixels, w, h, 90, FALSE);
  memset(&dest, 0, sizeof(dest));
  c.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    CHECK(0, "suspending destination: %s", err.msg);
    jpeg_destroy_compress(&c);
    free(ref.data);
    free(dest.out.data);
    free(pixels);
    return;
  }
  jpeg_create_compress(&c);
  dest.pub.init_destination = pool_init;
  dest.pub.empty_output_buffer = pool_empty;
  dest.pub.term_destination = pool_term;
  c.dest = &dest.pub;
  c.image_width = w;
  c.image_height = h;
  c.input_components = 3;
  c.in_color_space = JCS_RGB;
  jpeg_set_defaults(&c);
  jpeg_set_quality(&c, 90, TRUE);
  jpeg_start_compress(&c, TRUE);
  while (c.next_scanline < c.image_height) {
    JSAMPROW row = (JSAMPROW)pixels + (size_t)c.next_scanline * w * 3;
    jpeg_write_scanlines(&c, &row, 1);
    if (dest.suspended) {
      pool_drain(&dest);
      dest.suspended = 0;
    }
  }
  dest.finishing = 1;
  jpeg_finish_compress(&c);
  jpeg_destroy_compress(&c);
  CHECK(dest.suspensions > 0, "the destination never suspended");
  CHECK(same(&ref, &dest.out), "output through a suspending destination "
        "differs (%lu vs %lu bytes)", (unsigned long)dest.out.size,
        (unsigned long)ref.size);
  free(ref.data);
  free(dest.out.data);
  free(pixels);
}

/* ---- progress monitor --------------------------------------------------- */

static long progress_calls;
static long progress_bad;

static void progress_monitor(j_common_ptr cinfo)
{
  progress_calls++;
  if (cinfo->progress->pass_counter < 0 ||
      cinfo->progress->pass_counter > cinfo->progress->pass_limit)
    progress_bad++;
}

static void test_progress(const buffer *jpeg)
{
  struct jpeg_decompress_struct d;
  struct jpeg_progress_mgr prog;
  struct error_mgr err;
  JSAMPROW row;

  d.err = jpeg_std_error(&err.pub);
  init_err(&err.pub, &err);
  if (setjmp(err.jmp)) {
    CHECK(0, "progress: %s", err.msg);
    return;
  }
  jpeg_create_decompress(&d);
  prog.progress_monitor = progress_monitor;
  d.progress = &prog;
  jpeg_mem_src(&d, jpeg->data, (unsigned long)jpeg->size);
  jpeg_read_header(&d, TRUE);
  jpeg_start_decompress(&d);
  row = (JSAMPROW)malloc((size_t)d.output_width * d.output_components);
  while (d.output_scanline < d.output_height) jpeg_read_scanlines(&d, &row, 1);
  jpeg_finish_decompress(&d);
  jpeg_destroy_decompress(&d);
  free(row);
  CHECK(progress_calls > 0 && progress_bad == 0,
        "progress monitor: %ld calls, %ld bad", progress_calls, progress_bad);
}

int main(int argc, char **argv)
{
  buffer seq, prog;
  unsigned char *pixels;
  JDIMENSION w, h;
  int ncomp;
  buffer decoded;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <testimages directory>\n", argv[0]);
    return 1;
  }
  seq = read_file(argv[1], "testorig.jpg");
  decoded = decode_plain(&seq, JCS_RGB, &w, &h, &ncomp);
  pixels = decoded.data;
  prog = encode_plain(pixels, w, h, 90, TRUE);

  test_suspension(&seq, "testorig.jpg");
  test_suspension(&prog, "progressive");
  test_buffered_image(&prog, "progressive");
  test_markers(pixels, w, h);
  test_abbreviated(pixels, w, h);
  test_recovery(&seq);
  test_raw(&seq);
  test_suspending_dest();
  test_progress(&prog);

  free(pixels);
  free(seq.data);
  free(prog.data);
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
