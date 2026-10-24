/*
 * turbojpeg.h - the TurboJPEG API (version 3.2, with the 1.x/2.x functions
 * kept for backward compatibility), implemented by vio.
 *
 * Whole-image compression, decompression and lossless transformation of
 * JPEG images in memory, YUV planar conversion, and BMP/PPM image file I/O.
 */

#ifndef __TURBOJPEG_H__
#define __TURBOJPEG_H__

#include <stddef.h>

#define TURBOJPEG_VERSION_NUMBER  3002000

#ifndef DLLEXPORT
#if defined(_WIN32) && defined(DLLDEFINE)
#define DLLEXPORT  __declspec(dllexport)
#else
#define DLLEXPORT
#endif
#endif
#define DLLCALL


/* Instance types (tj3Init()) */
#define TJ_NUMINIT  3

enum TJINIT {
  TJINIT_COMPRESS,
  TJINIT_DECOMPRESS,
  TJINIT_TRANSFORM
};


/* Chrominance subsampling options */
#define TJ_NUMSAMP  9

enum TJSAMP {
  TJSAMP_444,                   /* no subsampling */
  TJSAMP_422,                   /* 2x1 */
  TJSAMP_420,                   /* 2x2 */
  TJSAMP_GRAY,                  /* grayscale */
  TJSAMP_440,                   /* 1x2 */
  TJSAMP_411,                   /* 4x1 */
  TJSAMP_441,                   /* 1x4 */
  TJSAMP_410,                   /* 4x2 */
  TJSAMP_24,                    /* 2x4 */
  TJSAMP_UNKNOWN = -1
};

/* MCU block width and height for each subsampling option */
static const int tjMCUWidth[TJ_NUMSAMP]  = { 8, 16, 16, 8, 8, 32, 8, 32, 16 };
static const int tjMCUHeight[TJ_NUMSAMP] = { 8, 8, 16, 8, 16, 8, 32, 16, 32 };


/* Pixel formats */
#define TJ_NUMPF  12

enum TJPF {
  TJPF_RGB,
  TJPF_BGR,
  TJPF_RGBX,
  TJPF_BGRX,
  TJPF_XBGR,
  TJPF_XRGB,
  TJPF_GRAY,
  TJPF_RGBA,
  TJPF_BGRA,
  TJPF_ABGR,
  TJPF_ARGB,
  TJPF_CMYK,
  TJPF_UNKNOWN = -1
};

/* Component offsets (-1: none) and pixel sizes for each pixel format */
static const int tjRedOffset[TJ_NUMPF] = {
  0, 2, 0, 2, 3, 1, -1, 0, 2, 3, 1, -1
};
static const int tjGreenOffset[TJ_NUMPF] = {
  1, 1, 1, 1, 2, 2, -1, 1, 1, 2, 2, -1
};
static const int tjBlueOffset[TJ_NUMPF] = {
  2, 0, 2, 0, 1, 3, -1, 2, 0, 1, 3, -1
};
static const int tjAlphaOffset[TJ_NUMPF] = {
  -1, -1, -1, -1, -1, -1, -1, 3, 3, 0, 0, -1
};
static const int tjPixelSize[TJ_NUMPF] = {
  3, 3, 4, 4, 4, 4, 1, 4, 4, 4, 4, 4
};


/* JPEG colorspaces */
#define TJ_NUMCS  5

enum TJCS {
  TJCS_RGB,
  TJCS_YCbCr,
  TJCS_GRAY,
  TJCS_CMYK,
  TJCS_YCCK,
  TJCS_DEFAULT = -1
};


/* Parameters (tj3Set()/tj3Get()) */
enum TJPARAM {
  TJPARAM_STOPONWARNING,        /* treat warnings as errors */
  TJPARAM_BOTTOMUP,             /* bottom-up row order */
  TJPARAM_NOREALLOC,            /* do not reallocate JPEG buffers */
  TJPARAM_QUALITY,              /* 1..100 */
  TJPARAM_SUBSAMP,              /* TJSAMP */
  TJPARAM_JPEGWIDTH,            /* image width (read-only) */
  TJPARAM_JPEGHEIGHT,           /* image height (read-only) */
  TJPARAM_PRECISION,            /* bits per sample */
  TJPARAM_COLORSPACE,           /* TJCS */
  TJPARAM_FASTUPSAMPLE,         /* nearest-neighbor upsampling */
  TJPARAM_FASTDCT,              /* fast integer DCT */
  TJPARAM_OPTIMIZE,             /* optimal Huffman tables */
  TJPARAM_PROGRESSIVE,          /* progressive JPEG */
  TJPARAM_SCANLIMIT,            /* maximum number of progressive scans */
  TJPARAM_ARITHMETIC,           /* arithmetic coding */
  TJPARAM_LOSSLESS,             /* lossless JPEG */
  TJPARAM_LOSSLESSPSV,          /* lossless predictor, 1..7 */
  TJPARAM_LOSSLESSPT,           /* lossless point transform */
  TJPARAM_RESTARTBLOCKS,        /* restart interval in MCUs */
  TJPARAM_RESTARTROWS,          /* restart interval in MCU rows */
  TJPARAM_XDENSITY,
  TJPARAM_YDENSITY,
  TJPARAM_DENSITYUNITS,         /* 0: unknown, 1: dpi, 2: dpcm */
  TJPARAM_MAXMEMORY,            /* memory limit in megabytes */
  TJPARAM_MAXPIXELS,            /* image size limit for tj3LoadImage*() */
  TJPARAM_SAVEMARKERS           /* markers copied by tj3Transform() */
};


/* Error codes (tj3GetErrorCode()) */
#define TJ_NUMERR  2

enum TJERR {
  TJERR_WARNING,
  TJERR_FATAL
};


/* Lossless transform operations */
#define TJ_NUMXOP  8

enum TJXOP {
  TJXOP_NONE,
  TJXOP_HFLIP,
  TJXOP_VFLIP,
  TJXOP_TRANSPOSE,
  TJXOP_TRANSVERSE,
  TJXOP_ROT90,
  TJXOP_ROT180,
  TJXOP_ROT270
};

/* Transform options */
#define TJXOPT_PERFECT  (1 << 0)     /* fail if the transform is imperfect */
#define TJXOPT_TRIM  (1 << 1)        /* discard partial MCUs */
#define TJXOPT_CROP  (1 << 2)        /* crop to tjtransform.r */
#define TJXOPT_GRAY  (1 << 3)        /* discard the color components */
#define TJXOPT_NOOUTPUT  (1 << 4)    /* produce no output (custom filter) */
#define TJXOPT_PROGRESSIVE  (1 << 5) /* progressive output */
#define TJXOPT_COPYNONE  (1 << 6)    /* copy no markers */
#define TJXOPT_ARITHMETIC  (1 << 7)  /* arithmetic-coded output */
#define TJXOPT_OPTIMIZE  (1 << 8)    /* optimal Huffman tables */


/* Scaling factor */
typedef struct {
  int num;
  int denom;
} tjscalingfactor;

/* Cropping region */
typedef struct {
  int x;
  int y;
  int w;                        /* 0: to the right edge */
  int h;                        /* 0: to the bottom edge */
} tjregion;

static const tjregion TJUNCROPPED = { 0, 0, 0, 0 };

/* Lossless transform */
typedef struct tjtransform {
  tjregion r;                   /* cropping region */
  int op;                       /* TJXOP */
  int options;                  /* TJXOPT_* */
  void *data;                   /* for customFilter */
  /* Called for each block of coefficients; returns 0 on success */
  int (*customFilter) (short *coeffs, tjregion arrayRegion,
                       tjregion planeRegion, int componentID, int transformID,
                       struct tjtransform *transform);
} tjtransform;

typedef void *tjhandle;

/* A dimension scaled by a tjscalingfactor */
#define TJSCALED(dimension, scalingFactor) \
  (((dimension) * scalingFactor.num + scalingFactor.denom - 1) / \
   scalingFactor.denom)

static const tjscalingfactor TJUNSCALED = { 1, 1 };


#ifdef __cplusplus
extern "C" {
#endif

/* Instances and parameters */
#define tj3Init(initType)  tj3InitVersion(initType, TURBOJPEG_VERSION_NUMBER)
DLLEXPORT tjhandle tj3InitVersion(int initType, int apiVersion);
DLLEXPORT void tj3Destroy(tjhandle handle);
DLLEXPORT char *tj3GetErrorStr(tjhandle handle);
DLLEXPORT int tj3GetErrorCode(tjhandle handle);
DLLEXPORT int tj3Set(tjhandle handle, int param, int value);
DLLEXPORT int tj3Get(tjhandle handle, int param);

/* Buffers */
DLLEXPORT void *tj3Alloc(size_t bytes);
DLLEXPORT void tj3Free(void *buffer);
DLLEXPORT size_t tj3JPEGBufSize(int width, int height, int jpegSubsamp);
DLLEXPORT size_t tj3YUVBufSize(int width, int align, int height, int subsamp);
DLLEXPORT size_t tj3YUVPlaneSize(int componentID, int width, int stride,
                                 int height, int subsamp);
DLLEXPORT int tj3YUVPlaneWidth(int componentID, int width, int subsamp);
DLLEXPORT int tj3YUVPlaneHeight(int componentID, int height, int subsamp);

/* Compression */
DLLEXPORT int tj3SetICCProfile(tjhandle handle, unsigned char *iccBuf,
                               size_t iccSize);
DLLEXPORT int tj3Compress8(tjhandle handle, const unsigned char *srcBuf,
                           int width, int pitch, int height, int pixelFormat,
                           unsigned char **jpegBuf, size_t *jpegSize);
DLLEXPORT int tj3Compress12(tjhandle handle, const short *srcBuf, int width,
                            int pitch, int height, int pixelFormat,
                            unsigned char **jpegBuf, size_t *jpegSize);
DLLEXPORT int tj3Compress16(tjhandle handle, const unsigned short *srcBuf,
                            int width, int pitch, int height, int pixelFormat,
                            unsigned char **jpegBuf, size_t *jpegSize);
DLLEXPORT int tj3CompressFromYUVPlanes8(tjhandle handle,
                                        const unsigned char * const *srcPlanes,
                                        int width, const int *strides,
                                        int height, unsigned char **jpegBuf,
                                        size_t *jpegSize);
DLLEXPORT int tj3CompressFromYUV8(tjhandle handle,
                                  const unsigned char *srcBuf, int width,
                                  int align, int height,
                                  unsigned char **jpegBuf, size_t *jpegSize);
DLLEXPORT int tj3EncodeYUVPlanes8(tjhandle handle, const unsigned char *srcBuf,
                                  int width, int pitch, int height,
                                  int pixelFormat, unsigned char **dstPlanes,
                                  int *strides);
DLLEXPORT int tj3EncodeYUV8(tjhandle handle, const unsigned char *srcBuf,
                            int width, int pitch, int height, int pixelFormat,
                            unsigned char *dstBuf, int align);

/* Decompression */
DLLEXPORT int tj3DecompressHeader(tjhandle handle,
                                  const unsigned char *jpegBuf,
                                  size_t jpegSize);
DLLEXPORT int tj3GetICCProfile(tjhandle handle, unsigned char **iccBuf,
                               size_t *iccSize);
DLLEXPORT tjscalingfactor *tj3GetScalingFactors(int *numScalingFactors);
DLLEXPORT int tj3SetScalingFactor(tjhandle handle,
                                  tjscalingfactor scalingFactor);
DLLEXPORT int tj3SetCroppingRegion(tjhandle handle, tjregion croppingRegion);
DLLEXPORT int tj3Decompress8(tjhandle handle, const unsigned char *jpegBuf,
                             size_t jpegSize, unsigned char *dstBuf, int pitch,
                             int pixelFormat);
DLLEXPORT int tj3Decompress12(tjhandle handle, const unsigned char *jpegBuf,
                              size_t jpegSize, short *dstBuf, int pitch,
                              int pixelFormat);
DLLEXPORT int tj3Decompress16(tjhandle handle, const unsigned char *jpegBuf,
                              size_t jpegSize, unsigned short *dstBuf,
                              int pitch, int pixelFormat);
DLLEXPORT int tj3DecompressToYUVPlanes8(tjhandle handle,
                                        const unsigned char *jpegBuf,
                                        size_t jpegSize,
                                        unsigned char **dstPlanes,
                                        int *strides);
DLLEXPORT int tj3DecompressToYUV8(tjhandle handle,
                                  const unsigned char *jpegBuf,
                                  size_t jpegSize,
                                  unsigned char *dstBuf, int align);
DLLEXPORT int tj3DecodeYUVPlanes8(tjhandle handle,
                                  const unsigned char * const *srcPlanes,
                                  const int *strides, unsigned char *dstBuf,
                                  int width, int pitch, int height,
                                  int pixelFormat);
DLLEXPORT int tj3DecodeYUV8(tjhandle handle, const unsigned char *srcBuf,
                            int align, unsigned char *dstBuf, int width,
                            int pitch, int height, int pixelFormat);

/* Lossless transformation */
DLLEXPORT size_t tj3TransformBufSize(tjhandle handle,
                                     const tjtransform *transform);
DLLEXPORT int tj3Transform(tjhandle handle, const unsigned char *jpegBuf,
                           size_t jpegSize, int n, unsigned char **dstBufs,
                           size_t *dstSizes, const tjtransform *transforms);

/* Image files (BMP and PPM/PGM) */
DLLEXPORT unsigned char *tj3LoadImage8(tjhandle handle, const char *filename,
                                       int *width, int align, int *height,
                                       int *pixelFormat);
DLLEXPORT short *tj3LoadImage12(tjhandle handle, const char *filename,
                                int *width, int align, int *height,
                                int *pixelFormat);
DLLEXPORT unsigned short *tj3LoadImage16(tjhandle handle, const char *filename,
                                         int *width, int align, int *height,
                                         int *pixelFormat);
DLLEXPORT int tj3SaveImage8(tjhandle handle, const char *filename,
                            const unsigned char *buffer, int width, int pitch,
                            int height, int pixelFormat);
DLLEXPORT int tj3SaveImage12(tjhandle handle, const char *filename,
                             const short *buffer, int width, int pitch,
                             int height, int pixelFormat);
DLLEXPORT int tj3SaveImage16(tjhandle handle, const char *filename,
                             const unsigned short *buffer, int width,
                             int pitch, int height, int pixelFormat);


/* Backward compatibility: the 1.x and 2.x API */

#define NUMSUBOPT  TJ_NUMSAMP
#define TJ_444  TJSAMP_444
#define TJ_422  TJSAMP_422
#define TJ_420  TJSAMP_420
#define TJ_411  TJSAMP_420
#define TJ_GRAYSCALE  TJSAMP_GRAY

#define TJ_BGR  1
#define TJ_BOTTOMUP  TJFLAG_BOTTOMUP
#define TJ_FORCEMMX  TJFLAG_FORCEMMX
#define TJ_FORCESSE  TJFLAG_FORCESSE
#define TJ_FORCESSE2  TJFLAG_FORCESSE2
#define TJ_ALPHAFIRST  64
#define TJ_FORCESSE3  TJFLAG_FORCESSE3
#define TJ_FASTUPSAMPLE  TJFLAG_FASTUPSAMPLE

#define TJPAD(width)  (((width) + 3) & (~3))

DLLEXPORT unsigned long TJBUFSIZE(int width, int height);

DLLEXPORT int tjCompress(tjhandle handle, unsigned char *srcBuf, int width,
                         int pitch, int height, int pixelSize,
                         unsigned char *dstBuf, unsigned long *compressedSize,
                         int jpegSubsamp, int jpegQual, int flags);

DLLEXPORT int tjDecompress(tjhandle handle, unsigned char *jpegBuf,
                           unsigned long jpegSize, unsigned char *dstBuf,
                           int width, int pitch, int height, int pixelSize,
                           int flags);

DLLEXPORT int tjDecompressHeader(tjhandle handle, unsigned char *jpegBuf,
                                 unsigned long jpegSize, int *width,
                                 int *height);

DLLEXPORT int tjDestroy(tjhandle handle);

DLLEXPORT char *tjGetErrorStr(void);

DLLEXPORT tjhandle tjInitCompress(void);

DLLEXPORT tjhandle tjInitDecompress(void);

#define TJ_YUV  512

DLLEXPORT unsigned long TJBUFSIZEYUV(int width, int height, int jpegSubsamp);

DLLEXPORT int tjDecompressHeader2(tjhandle handle, unsigned char *jpegBuf,
                                  unsigned long jpegSize, int *width,
                                  int *height, int *jpegSubsamp);

DLLEXPORT int tjDecompressToYUV(tjhandle handle, unsigned char *jpegBuf,
                                unsigned long jpegSize, unsigned char *dstBuf,
                                int flags);

DLLEXPORT int tjEncodeYUV(tjhandle handle, unsigned char *srcBuf, int width,
                          int pitch, int height, int pixelSize,
                          unsigned char *dstBuf, int subsamp, int flags);

#define TJFLAG_BOTTOMUP  2
#define TJFLAG_FORCEMMX  8
#define TJFLAG_FORCESSE  16
#define TJFLAG_FORCESSE2  32
#define TJFLAG_FORCESSE3  128
#define TJFLAG_FASTUPSAMPLE  256
#define TJFLAG_NOREALLOC  1024

DLLEXPORT unsigned char *tjAlloc(int bytes);

DLLEXPORT unsigned long tjBufSize(int width, int height, int jpegSubsamp);

DLLEXPORT unsigned long tjBufSizeYUV(int width, int height, int subsamp);

DLLEXPORT int tjCompress2(tjhandle handle, const unsigned char *srcBuf,
                          int width, int pitch, int height, int pixelFormat,
                          unsigned char **jpegBuf, unsigned long *jpegSize,
                          int jpegSubsamp, int jpegQual, int flags);

DLLEXPORT int tjDecompress2(tjhandle handle, const unsigned char *jpegBuf,
                            unsigned long jpegSize, unsigned char *dstBuf,
                            int width, int pitch, int height, int pixelFormat,
                            int flags);

DLLEXPORT int tjEncodeYUV2(tjhandle handle, unsigned char *srcBuf, int width,
                           int pitch, int height, int pixelFormat,
                           unsigned char *dstBuf, int subsamp, int flags);

DLLEXPORT void tjFree(unsigned char *buffer);

DLLEXPORT tjscalingfactor *tjGetScalingFactors(int *numscalingfactors);

DLLEXPORT tjhandle tjInitTransform(void);

DLLEXPORT int tjTransform(tjhandle handle, const unsigned char *jpegBuf,
                            unsigned long jpegSize, int n,
                            unsigned char **dstBufs, unsigned long *dstSizes,
                            tjtransform *transforms, int flags);

#define TJFLAG_FASTDCT  2048
#define TJFLAG_ACCURATEDCT  4096

DLLEXPORT unsigned long tjBufSizeYUV2(int width, int align, int height,
                                      int subsamp);

DLLEXPORT int tjCompressFromYUV(tjhandle handle, const unsigned char *srcBuf,
                                int width, int align, int height, int subsamp,
                                unsigned char **jpegBuf,
                                unsigned long *jpegSize, int jpegQual,
                                int flags);

DLLEXPORT int tjCompressFromYUVPlanes(tjhandle handle,
                                      const unsigned char **srcPlanes,
                                      int width, const int *strides,
                                      int height, int subsamp,
                                      unsigned char **jpegBuf,
                                      unsigned long *jpegSize, int jpegQual,
                                      int flags);

DLLEXPORT int tjDecodeYUV(tjhandle handle, const unsigned char *srcBuf,
                          int align, int subsamp, unsigned char *dstBuf,
                          int width, int pitch, int height, int pixelFormat,
                          int flags);

DLLEXPORT int tjDecodeYUVPlanes(tjhandle handle,
                                const unsigned char **srcPlanes,
                                const int *strides, int subsamp,
                                unsigned char *dstBuf, int width, int pitch,
                                int height, int pixelFormat, int flags);

DLLEXPORT int tjDecompressHeader3(tjhandle handle,
                                  const unsigned char *jpegBuf,
                                  unsigned long jpegSize, int *width,
                                  int *height, int *jpegSubsamp,
                                  int *jpegColorspace);

DLLEXPORT int tjDecompressToYUV2(tjhandle handle, const unsigned char *jpegBuf,
                                 unsigned long jpegSize, unsigned char *dstBuf,
                                 int width, int align, int height, int flags);

DLLEXPORT int tjDecompressToYUVPlanes(tjhandle handle,
                                      const unsigned char *jpegBuf,
                                      unsigned long jpegSize,
                                      unsigned char **dstPlanes, int width,
                                      int *strides, int height, int flags);

DLLEXPORT int tjEncodeYUV3(tjhandle handle, const unsigned char *srcBuf,
                           int width, int pitch, int height, int pixelFormat,
                           unsigned char *dstBuf, int align, int subsamp,
                           int flags);

DLLEXPORT int tjEncodeYUVPlanes(tjhandle handle, const unsigned char *srcBuf,
                                int width, int pitch, int height,
                                int pixelFormat, unsigned char **dstPlanes,
                                int *strides, int subsamp, int flags);

DLLEXPORT int tjPlaneHeight(int componentID, int height, int subsamp);

DLLEXPORT unsigned long tjPlaneSizeYUV(int componentID, int width, int stride,
                                       int height, int subsamp);

DLLEXPORT int tjPlaneWidth(int componentID, int width, int subsamp);

#define TJFLAG_STOPONWARNING  8192
#define TJFLAG_PROGRESSIVE  16384

DLLEXPORT int tjGetErrorCode(tjhandle handle);

DLLEXPORT char *tjGetErrorStr2(tjhandle handle);

DLLEXPORT unsigned char *tjLoadImage(const char *filename, int *width,
                                     int align, int *height, int *pixelFormat,
                                     int flags);

DLLEXPORT int tjSaveImage(const char *filename, unsigned char *buffer,
                          int width, int pitch, int height, int pixelFormat,
                          int flags);

#define TJFLAG_LIMITSCANS  32768

#ifdef __cplusplus
}
#endif

#endif /* __TURBOJPEG_H__ */
