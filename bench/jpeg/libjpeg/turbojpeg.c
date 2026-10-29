/*
 * The subset of the TurboJPEG API that jpeg-bench needs to measure 8-bit
 * compression and decompression, implemented on IJG libjpeg (version 9 or
 * later), so that the same benchmark program can time vio, libjpeg-turbo and
 * libjpeg.  Compression and decompression set up libjpeg exactly as
 * libjpeg-turbo's TurboJPEG layer sets up its own libjpeg API.
 *
 * Supported: packed RGB and grayscale pixels, every subsampling level,
 * quality, fast DCT, fast upsampling, optimized/progressive/arithmetic
 * coding, restart markers, bottom-up buffers, PPM/PGM input.  Everything else
 * (lossless, 12/16-bit, YUV, transforms, scaling, cropping, ICC profiles,
 * image output) fails with an error.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpeglib.h"
#include "jerror.h"
#include "turbojpeg.h"

#define NUMPARAMS  (TJPARAM_SAVEMARKERS + 1)
#define PAD(v, p)  (((v) + (p) - 1) & (~((p) - 1)))

struct error_mgr {
  struct jpeg_error_mgr pub;
  jmp_buf jmp;
};

typedef struct {
  int init;
  int params[NUMPARAMS];
  struct jpeg_compress_struct cinfo;
  struct jpeg_decompress_struct dinfo;
  struct error_mgr cerr, derr;
  char errStr[JMSG_LENGTH_MAX];
  int isInstanceError;
} tjinstance;

static char errStr[JMSG_LENGTH_MAX] = "No error";

static int fail(tjinstance *this, const char *fmt, ...)
{
  va_list ap;
  char *dst = this ? this->errStr : errStr;

  va_start(ap, fmt);
  vsnprintf(dst, JMSG_LENGTH_MAX, fmt, ap);
  va_end(ap);
  if (this) this->isInstanceError = 1;
  else snprintf(errStr, JMSG_LENGTH_MAX, "%s", dst);
  return -1;
}

static void error_exit(j_common_ptr cinfo)
{
  struct error_mgr *err = (struct error_mgr *)cinfo->err;

  longjmp(err->jmp, 1);
}

static void output_message(j_common_ptr cinfo)
{
  (void)cinfo;
}

static void lib_error(tjinstance *this, j_common_ptr cinfo)
{
  char buf[JMSG_LENGTH_MAX];

  (*cinfo->err->format_message) (cinfo, buf);
  fail(this, "%s", buf);
}

static int param(tjinstance *this, int p)
{
  return this->params[p];
}

DLLEXPORT tjhandle tj3InitVersion(int initType, int apiVersion)
{
  tjinstance *this;
  int i;

  (void)apiVersion;
  if ((this = (tjinstance *)calloc(1, sizeof(tjinstance))) == NULL) {
    fail(NULL, "tj3Init(): Memory allocation failure");
    return NULL;
  }
  for (i = 0; i < NUMPARAMS; i++) this->params[i] = 0;
  this->params[TJPARAM_QUALITY] = -1;
  this->params[TJPARAM_SUBSAMP] = TJSAMP_UNKNOWN;
  this->params[TJPARAM_PRECISION] = 8;
  this->params[TJPARAM_COLORSPACE] = -1;
  this->params[TJPARAM_LOSSLESSPSV] = 1;
  this->params[TJPARAM_SAVEMARKERS] = 2;
  snprintf(this->errStr, JMSG_LENGTH_MAX, "No error");

  this->cinfo.err = jpeg_std_error(&this->cerr.pub);
  this->cerr.pub.error_exit = error_exit;
  this->cerr.pub.output_message = output_message;
  this->dinfo.err = jpeg_std_error(&this->derr.pub);
  this->derr.pub.error_exit = error_exit;
  this->derr.pub.output_message = output_message;

  if (initType == TJINIT_COMPRESS || initType == TJINIT_TRANSFORM) {
    if (setjmp(this->cerr.jmp)) {
      free(this);
      fail(NULL, "tj3Init(): Could not create compressor");
      return NULL;
    }
    jpeg_create_compress(&this->cinfo);
    this->init |= 1;
  }
  if (initType == TJINIT_DECOMPRESS || initType == TJINIT_TRANSFORM) {
    if (setjmp(this->derr.jmp)) {
      if (this->init & 1) jpeg_destroy_compress(&this->cinfo);
      free(this);
      fail(NULL, "tj3Init(): Could not create decompressor");
      return NULL;
    }
    jpeg_create_decompress(&this->dinfo);
    this->init |= 2;
  }
  return (tjhandle)this;
}

#undef tj3Init
DLLEXPORT tjhandle tj3Init(int initType)
{
  return tj3InitVersion(initType, 3002000);
}

DLLEXPORT void tj3Destroy(tjhandle handle)
{
  tjinstance *this = (tjinstance *)handle;

  if (!this) return;
  if (this->init & 1) jpeg_destroy_compress(&this->cinfo);
  if (this->init & 2) jpeg_destroy_decompress(&this->dinfo);
  free(this);
}

DLLEXPORT char *tj3GetErrorStr(tjhandle handle)
{
  tjinstance *this = (tjinstance *)handle;

  if (this && this->isInstanceError) {
    this->isInstanceError = 0;
    return this->errStr;
  }
  return errStr;
}

DLLEXPORT int tj3GetErrorCode(tjhandle handle)
{
  (void)handle;
  return TJERR_FATAL;
}

DLLEXPORT int tj3Set(tjhandle handle, int p, int value)
{
  tjinstance *this = (tjinstance *)handle;

  if (!this) return fail(NULL, "tj3Set(): Invalid handle");
  if (p < 0 || p >= NUMPARAMS) return fail(this, "tj3Set(): Invalid parameter");
  this->params[p] = value;
  return 0;
}

DLLEXPORT int tj3Get(tjhandle handle, int p)
{
  tjinstance *this = (tjinstance *)handle;

  if (!this || p < 0 || p >= NUMPARAMS) return -1;
  return this->params[p];
}

DLLEXPORT void *tj3Alloc(size_t bytes)
{
  return malloc(bytes);
}

DLLEXPORT void tj3Free(void *buf)
{
  free(buf);
}

DLLEXPORT size_t tj3JPEGBufSize(int width, int height, int jpegSubsamp)
{
  int mcuw, mcuh, chromasf;

  if (width < 1 || height < 1 || jpegSubsamp < 0 || jpegSubsamp >= TJ_NUMSAMP)
    return 0;
  /* Worst case: an uncompressible image plus headers. */
  mcuw = tjMCUWidth[jpegSubsamp];
  mcuh = tjMCUHeight[jpegSubsamp];
  chromasf = jpegSubsamp == TJSAMP_GRAY ? 0 : 4 * 64 / (mcuw * mcuh);
  return (size_t)PAD(width, mcuw) * PAD(height, mcuh) * (2 + chromasf) + 2048;
}

DLLEXPORT size_t tj3YUVBufSize(int width, int align, int height, int subsamp)
{
  (void)width;  (void)align;  (void)height;  (void)subsamp;
  fail(NULL, "tj3YUVBufSize(): YUV is not supported");
  return 0;
}

static tjscalingfactor scalingFactors[] = { { 1, 1 } };

DLLEXPORT tjscalingfactor *tj3GetScalingFactors(int *numScalingFactors)
{
  if (numScalingFactors) *numScalingFactors = 1;
  return scalingFactors;
}

DLLEXPORT int tj3SetScalingFactor(tjhandle handle, tjscalingfactor sf)
{
  tjinstance *this = (tjinstance *)handle;

  if (sf.num != sf.denom)
    return fail(this, "tj3SetScalingFactor(): Scaling is not supported");
  return 0;
}

DLLEXPORT int tj3SetCroppingRegion(tjhandle handle, tjregion cr)
{
  tjinstance *this = (tjinstance *)handle;

  if (cr.x || cr.y || cr.w || cr.h)
    return fail(this, "tj3SetCroppingRegion(): Cropping is not supported");
  return 0;
}

static J_COLOR_SPACE pf2cs(int pf)
{
  return pf == TJPF_GRAY ? JCS_GRAYSCALE : pf == TJPF_RGB ? JCS_RGB : JCS_UNKNOWN;
}

DLLEXPORT int tj3Compress8(tjhandle handle, const unsigned char *srcBuf,
                           int width, int pitch, int height, int pixelFormat,
                           unsigned char **jpegBuf, size_t *jpegSize)
{
  tjinstance *this = (tjinstance *)handle;
  j_compress_ptr cinfo;
  JSAMPROW *rows = NULL;
  int i, subsamp;

  if (!this) return fail(NULL, "tj3Compress8(): Invalid handle");
  cinfo = &this->cinfo;
  if (!(this->init & 1))
    return fail(this, "tj3Compress8(): Instance has not been initialized for compression");
  if (!srcBuf || width <= 0 || height <= 0 || !jpegBuf || !jpegSize ||
      pf2cs(pixelFormat) == JCS_UNKNOWN)
    return fail(this, "tj3Compress8(): Invalid argument or unsupported pixel format");
  subsamp = param(this, TJPARAM_SUBSAMP);
  if (param(this, TJPARAM_QUALITY) == -1)
    return fail(this, "tj3Compress8(): TJPARAM_QUALITY must be specified");
  if (subsamp < 0 || subsamp >= TJ_NUMSAMP)
    return fail(this, "tj3Compress8(): TJPARAM_SUBSAMP must be specified");
  if (param(this, TJPARAM_LOSSLESS))
    return fail(this, "tj3Compress8(): Lossless JPEG is not supported");
  if (pitch == 0) pitch = width * tjPixelSize[pixelFormat];

  if ((rows = (JSAMPROW *)malloc(sizeof(JSAMPROW) * height)) == NULL)
    return fail(this, "tj3Compress8(): Memory allocation failure");

  if (setjmp(this->cerr.jmp)) {
    lib_error(this, (j_common_ptr)cinfo);
    jpeg_abort_compress(cinfo);
    free(rows);
    return -1;
  }

  cinfo->image_width = width;
  cinfo->image_height = height;
  {
    /* libjpeg's memory destination writes into the given buffer and
       allocates a new one only if it overflows, which the worst-case size
       of a preallocated (TJPARAM_NOREALLOC) buffer rules out. */
    unsigned char *out = *jpegBuf;
    size_t outSize = param(this, TJPARAM_NOREALLOC) ?
                     tj3JPEGBufSize(width, height, subsamp) : *jpegSize;

    jpeg_mem_dest(cinfo, &out, &outSize);

    cinfo->in_color_space = pf2cs(pixelFormat);
    cinfo->input_components = tjPixelSize[pixelFormat];
    jpeg_set_defaults(cinfo);
    cinfo->restart_interval = param(this, TJPARAM_RESTARTBLOCKS);
    cinfo->restart_in_rows = param(this, TJPARAM_RESTARTROWS);
    jpeg_set_quality(cinfo, param(this, TJPARAM_QUALITY), TRUE);
    cinfo->dct_method = param(this, TJPARAM_FASTDCT) ? JDCT_FASTEST : JDCT_ISLOW;
    if (subsamp == TJSAMP_GRAY)
      jpeg_set_colorspace(cinfo, JCS_GRAYSCALE);
    else
      jpeg_set_colorspace(cinfo, JCS_YCbCr);
    cinfo->optimize_coding = param(this, TJPARAM_OPTIMIZE);
    if (param(this, TJPARAM_PROGRESSIVE)) jpeg_simple_progression(cinfo);
    cinfo->arith_code = param(this, TJPARAM_ARITHMETIC);
    cinfo->comp_info[0].h_samp_factor = tjMCUWidth[subsamp] / 8;
    cinfo->comp_info[0].v_samp_factor = tjMCUHeight[subsamp] / 8;
    if (cinfo->num_components > 1) {
      cinfo->comp_info[1].h_samp_factor = cinfo->comp_info[1].v_samp_factor = 1;
      cinfo->comp_info[2].h_samp_factor = cinfo->comp_info[2].v_samp_factor = 1;
    }

    jpeg_start_compress(cinfo, TRUE);
    for (i = 0; i < height; i++) {
      int row = param(this, TJPARAM_BOTTOMUP) ? height - i - 1 : i;

      rows[i] = (JSAMPROW)&srcBuf[(size_t)row * pitch];
    }
    while (cinfo->next_scanline < cinfo->image_height)
      jpeg_write_scanlines(cinfo, &rows[cinfo->next_scanline],
                           cinfo->image_height - cinfo->next_scanline);
    jpeg_finish_compress(cinfo);

    if (param(this, TJPARAM_NOREALLOC) && out != *jpegBuf) {
      free(out);
      free(rows);
      return fail(this, "tj3Compress8(): Destination buffer is not large enough");
    }
    *jpegBuf = out;
    *jpegSize = outSize;
  }
  free(rows);
  return 0;
}

static int getSubsamp(j_decompress_ptr dinfo)
{
  int i;

  if (dinfo->num_components == 1 && dinfo->jpeg_color_space == JCS_GRAYSCALE)
    return TJSAMP_GRAY;
  if (dinfo->num_components != 3) return TJSAMP_UNKNOWN;
  for (i = 1; i < 3; i++)
    if (dinfo->comp_info[i].h_samp_factor != 1 ||
        dinfo->comp_info[i].v_samp_factor != 1)
      return TJSAMP_UNKNOWN;
  for (i = 0; i < TJ_NUMSAMP; i++)
    if (i != TJSAMP_GRAY &&
        dinfo->comp_info[0].h_samp_factor == tjMCUWidth[i] / 8 &&
        dinfo->comp_info[0].v_samp_factor == tjMCUHeight[i] / 8)
      return i;
  return TJSAMP_UNKNOWN;
}

DLLEXPORT int tj3DecompressHeader(tjhandle handle, const unsigned char *jpegBuf,
                                  size_t jpegSize)
{
  tjinstance *this = (tjinstance *)handle;
  j_decompress_ptr dinfo;

  if (!this) return fail(NULL, "tj3DecompressHeader(): Invalid handle");
  dinfo = &this->dinfo;
  if (!(this->init & 2))
    return fail(this, "tj3DecompressHeader(): Instance has not been initialized for decompression");
  if (!jpegBuf || jpegSize == 0)
    return fail(this, "tj3DecompressHeader(): Invalid argument");

  if (setjmp(this->derr.jmp)) {
    lib_error(this, (j_common_ptr)dinfo);
    jpeg_abort_decompress(dinfo);
    return -1;
  }
  jpeg_mem_src(dinfo, jpegBuf, jpegSize);
  if (jpeg_read_header(dinfo, FALSE) == JPEG_HEADER_TABLES_ONLY) return 0;

  this->params[TJPARAM_JPEGWIDTH] = dinfo->image_width;
  this->params[TJPARAM_JPEGHEIGHT] = dinfo->image_height;
  this->params[TJPARAM_SUBSAMP] = getSubsamp(dinfo);
  this->params[TJPARAM_PRECISION] = dinfo->data_precision;
  this->params[TJPARAM_COLORSPACE] =
    dinfo->jpeg_color_space == JCS_GRAYSCALE ? TJCS_GRAY :
    dinfo->jpeg_color_space == JCS_YCbCr ? TJCS_YCbCr :
    dinfo->jpeg_color_space == JCS_RGB ? TJCS_RGB :
    dinfo->jpeg_color_space == JCS_CMYK ? TJCS_CMYK :
    dinfo->jpeg_color_space == JCS_YCCK ? TJCS_YCCK : -1;
  this->params[TJPARAM_PROGRESSIVE] = dinfo->progressive_mode;
  this->params[TJPARAM_ARITHMETIC] = dinfo->arith_code;
  this->params[TJPARAM_LOSSLESS] = 0;
  jpeg_abort_decompress(dinfo);
  return 0;
}

DLLEXPORT int tj3Decompress8(tjhandle handle, const unsigned char *jpegBuf,
                             size_t jpegSize, unsigned char *dstBuf, int pitch,
                             int pixelFormat)
{
  tjinstance *this = (tjinstance *)handle;
  j_decompress_ptr dinfo;
  JSAMPROW *rows = NULL;
  unsigned int i;

  if (!this) return fail(NULL, "tj3Decompress8(): Invalid handle");
  dinfo = &this->dinfo;
  if (!(this->init & 2))
    return fail(this, "tj3Decompress8(): Instance has not been initialized for decompression");
  if (!jpegBuf || jpegSize == 0 || !dstBuf || pf2cs(pixelFormat) == JCS_UNKNOWN)
    return fail(this, "tj3Decompress8(): Invalid argument or unsupported pixel format");

  if (setjmp(this->derr.jmp)) {
    lib_error(this, (j_common_ptr)dinfo);
    jpeg_abort_decompress(dinfo);
    free(rows);
    return -1;
  }
  jpeg_mem_src(dinfo, jpegBuf, jpegSize);
  jpeg_read_header(dinfo, TRUE);
  dinfo->out_color_space = pf2cs(pixelFormat);
  if (param(this, TJPARAM_FASTDCT)) dinfo->dct_method = JDCT_FASTEST;
  if (param(this, TJPARAM_FASTUPSAMPLE)) dinfo->do_fancy_upsampling = FALSE;
  jpeg_start_decompress(dinfo);
  if (pitch == 0) pitch = dinfo->output_width * tjPixelSize[pixelFormat];

  if ((rows = (JSAMPROW *)malloc(sizeof(JSAMPROW) * dinfo->output_height)) == NULL) {
    jpeg_abort_decompress(dinfo);
    return fail(this, "tj3Decompress8(): Memory allocation failure");
  }
  for (i = 0; i < dinfo->output_height; i++) {
    unsigned int row = param(this, TJPARAM_BOTTOMUP) ?
                       dinfo->output_height - i - 1 : i;

    rows[i] = &dstBuf[(size_t)row * pitch];
  }
  while (dinfo->output_scanline < dinfo->output_height)
    jpeg_read_scanlines(dinfo, &rows[dinfo->output_scanline],
                        dinfo->output_height - dinfo->output_scanline);
  jpeg_finish_decompress(dinfo);
  free(rows);
  return 0;
}

/* PPM (P6) and PGM (P5) with 8-bit samples. */
DLLEXPORT unsigned char *tj3LoadImage8(tjhandle handle, const char *filename,
                                       int *width, int align, int *height,
                                       int *pixelFormat)
{
  tjinstance *this = (tjinstance *)handle;
  FILE *f;
  char magic[3] = { 0 };
  int w, h, maxval, pf, ps, pitch, y;
  unsigned char *buf, *rowbuf;

  if (!filename || !width || !height || !pixelFormat || align < 1) {
    fail(this, "tj3LoadImage8(): Invalid argument");
    return NULL;
  }
  if ((f = fopen(filename, "rb")) == NULL) {
    fail(this, "tj3LoadImage8(): Cannot open %s", filename);
    return NULL;
  }
  if (fscanf(f, "%2s %d %d %d", magic, &w, &h, &maxval) != 4 ||
      (strcmp(magic, "P6") && strcmp(magic, "P5")) || maxval != 255 ||
      w < 1 || h < 1 || fgetc(f) == EOF) {
    fclose(f);
    fail(this, "tj3LoadImage8(): Only 8-bit binary PPM/PGM files are supported");
    return NULL;
  }
  pf = *pixelFormat;
  if (pf == TJPF_UNKNOWN) pf = magic[1] == '6' ? TJPF_RGB : TJPF_GRAY;
  if (pf != TJPF_RGB && pf != TJPF_GRAY) {
    fclose(f);
    fail(this, "tj3LoadImage8(): Only RGB and grayscale pixel formats are supported");
    return NULL;
  }
  ps = tjPixelSize[pf];
  pitch = PAD(w * ps, align);
  buf = (unsigned char *)malloc((size_t)pitch * h);
  rowbuf = (unsigned char *)malloc((size_t)w * 3);
  if (!buf || !rowbuf) {
    free(buf);  free(rowbuf);  fclose(f);
    fail(this, "tj3LoadImage8(): Memory allocation failure");
    return NULL;
  }
  for (y = 0; y < h; y++) {
    int row = this && param(this, TJPARAM_BOTTOMUP) ? h - y - 1 : y;
    unsigned char *dst = &buf[(size_t)row * pitch];
    int fileps = magic[1] == '6' ? 3 : 1, x;

    if (fread(rowbuf, fileps, w, f) != (size_t)w) {
      free(buf);  free(rowbuf);  fclose(f);
      fail(this, "tj3LoadImage8(): Unexpected end of file");
      return NULL;
    }
    if (fileps == ps)
      memcpy(dst, rowbuf, (size_t)w * ps);
    else if (ps == 3)
      for (x = 0; x < w; x++) dst[3 * x] = dst[3 * x + 1] = dst[3 * x + 2] = rowbuf[x];
    else
      for (x = 0; x < w; x++)
        dst[x] = (unsigned char)((rowbuf[3 * x] * 19595 + rowbuf[3 * x + 1] * 38470 +
                                  rowbuf[3 * x + 2] * 7471 + 32768) >> 16);
  }
  free(rowbuf);
  fclose(f);
  *width = w;  *height = h;  *pixelFormat = pf;
  return buf;
}

/* ---- not supported -------------------------------------------------------- */

#define UNSUPPORTED(name) \
  return fail((tjinstance *)handle, name "(): Not supported by this build")

DLLEXPORT int tj3Compress12(tjhandle handle, const short *srcBuf, int width,
                            int pitch, int height, int pixelFormat,
                            unsigned char **jpegBuf, size_t *jpegSize)
{
  (void)srcBuf; (void)width; (void)pitch; (void)height; (void)pixelFormat;
  (void)jpegBuf; (void)jpegSize;
  UNSUPPORTED("tj3Compress12");
}

DLLEXPORT int tj3Compress16(tjhandle handle, const unsigned short *srcBuf,
                            int width, int pitch, int height, int pixelFormat,
                            unsigned char **jpegBuf, size_t *jpegSize)
{
  (void)srcBuf; (void)width; (void)pitch; (void)height; (void)pixelFormat;
  (void)jpegBuf; (void)jpegSize;
  UNSUPPORTED("tj3Compress16");
}

DLLEXPORT int tj3Decompress12(tjhandle handle, const unsigned char *jpegBuf,
                              size_t jpegSize, short *dstBuf, int pitch,
                              int pixelFormat)
{
  (void)jpegBuf; (void)jpegSize; (void)dstBuf; (void)pitch; (void)pixelFormat;
  UNSUPPORTED("tj3Decompress12");
}

DLLEXPORT int tj3Decompress16(tjhandle handle, const unsigned char *jpegBuf,
                              size_t jpegSize, unsigned short *dstBuf,
                              int pitch, int pixelFormat)
{
  (void)jpegBuf; (void)jpegSize; (void)dstBuf; (void)pitch; (void)pixelFormat;
  UNSUPPORTED("tj3Decompress16");
}

DLLEXPORT int tj3EncodeYUV8(tjhandle handle, const unsigned char *srcBuf,
                            int width, int pitch, int height, int pixelFormat,
                            unsigned char *dstBuf, int align)
{
  (void)srcBuf; (void)width; (void)pitch; (void)height; (void)pixelFormat;
  (void)dstBuf; (void)align;
  UNSUPPORTED("tj3EncodeYUV8");
}

DLLEXPORT int tj3CompressFromYUV8(tjhandle handle, const unsigned char *srcBuf,
                                  int width, int align, int height,
                                  unsigned char **jpegBuf, size_t *jpegSize)
{
  (void)srcBuf; (void)width; (void)align; (void)height; (void)jpegBuf;
  (void)jpegSize;
  UNSUPPORTED("tj3CompressFromYUV8");
}

DLLEXPORT int tj3DecompressToYUV8(tjhandle handle, const unsigned char *jpegBuf,
                                  size_t jpegSize, unsigned char *dstBuf,
                                  int align)
{
  (void)jpegBuf; (void)jpegSize; (void)dstBuf; (void)align;
  UNSUPPORTED("tj3DecompressToYUV8");
}

DLLEXPORT int tj3DecodeYUV8(tjhandle handle, const unsigned char *srcBuf,
                            int align, unsigned char *dstBuf, int width,
                            int pitch, int height, int pixelFormat)
{
  (void)srcBuf; (void)align; (void)dstBuf; (void)width; (void)pitch;
  (void)height; (void)pixelFormat;
  UNSUPPORTED("tj3DecodeYUV8");
}

DLLEXPORT size_t tj3TransformBufSize(tjhandle handle, const tjtransform *transform)
{
  (void)transform;
  fail((tjinstance *)handle, "tj3TransformBufSize(): Not supported by this build");
  return 0;
}

DLLEXPORT int tj3Transform(tjhandle handle, const unsigned char *jpegBuf,
                           size_t jpegSize, int n, unsigned char **dstBufs,
                           size_t *dstSizes, const tjtransform *transforms)
{
  (void)jpegBuf; (void)jpegSize; (void)n; (void)dstBufs; (void)dstSizes;
  (void)transforms;
  UNSUPPORTED("tj3Transform");
}

/* No ICC profiles are read, so there is never one to return. */
DLLEXPORT int tj3GetICCProfile(tjhandle handle, unsigned char **iccBuf,
                               size_t *iccSize)
{
  (void)handle;
  if (iccBuf) *iccBuf = NULL;
  if (iccSize) *iccSize = 0;
  return 0;
}

DLLEXPORT short *tj3LoadImage12(tjhandle handle, const char *filename,
                                int *width, int align, int *height,
                                int *pixelFormat)
{
  (void)filename; (void)width; (void)align; (void)height; (void)pixelFormat;
  fail((tjinstance *)handle, "tj3LoadImage12(): Not supported by this build");
  return NULL;
}

DLLEXPORT unsigned short *tj3LoadImage16(tjhandle handle, const char *filename,
                                         int *width, int align, int *height,
                                         int *pixelFormat)
{
  (void)filename; (void)width; (void)align; (void)height; (void)pixelFormat;
  fail((tjinstance *)handle, "tj3LoadImage16(): Not supported by this build");
  return NULL;
}

DLLEXPORT int tj3SaveImage8(tjhandle handle, const char *filename,
                            const unsigned char *buffer, int width, int pitch,
                            int height, int pixelFormat)
{
  (void)filename; (void)buffer; (void)width; (void)pitch; (void)height;
  (void)pixelFormat;
  UNSUPPORTED("tj3SaveImage8");
}

DLLEXPORT int tj3SaveImage12(tjhandle handle, const char *filename,
                             const short *buffer, int width, int pitch,
                             int height, int pixelFormat)
{
  (void)filename; (void)buffer; (void)width; (void)pitch; (void)height;
  (void)pixelFormat;
  UNSUPPORTED("tj3SaveImage12");
}

DLLEXPORT int tj3SaveImage16(tjhandle handle, const char *filename,
                             const unsigned short *buffer, int width,
                             int pitch, int height, int pixelFormat)
{
  (void)filename; (void)buffer; (void)width; (void)pitch; (void)height;
  (void)pixelFormat;
  UNSUPPORTED("tj3SaveImage16");
}
