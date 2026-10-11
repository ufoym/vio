// TurboJPEG API (turbojpeg.h, version 3.2) implemented on top of vio.
//
// This compatibility layer lets programs written against libjpeg-turbo's
// TurboJPEG API -- in particular libjpeg-turbo's own tjunittest and tjbench --
// run unchanged on vio.  The logic follows libjpeg-turbo's turbojpeg.c and
// turbojpeg-mp.c so that results are identical.
#include "turbojpeg.h"

#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <strings.h>
#include <vector>

#include "vio/bmp.hpp"
#include "vio/png.hpp"
#include "vio/ppm.hpp"
#include "vio/jpeg/decoder.hpp"
#include "vio/jpeg/encoder.hpp"
#include "vio/jpeg/transform.hpp"

namespace {

using vio::ColorSpace;
namespace vj = vio::jpeg;
namespace vd = vio::jpeg::detail;

constexpr int JMSG_LENGTH_MAX = 200;
thread_local char errStr[JMSG_LENGTH_MAX] = "No error";

constexpr ColorSpace pf2cs[TJ_NUMPF] = {
    ColorSpace::ExtRGB,  ColorSpace::ExtBGR,  ColorSpace::ExtRGBX, ColorSpace::ExtBGRX,
    ColorSpace::ExtXBGR, ColorSpace::ExtXRGB, ColorSpace::Gray,    ColorSpace::ExtRGBA,
    ColorSpace::ExtBGRA, ColorSpace::ExtABGR, ColorSpace::ExtARGB, ColorSpace::CMYK};

constexpr int cs2pf[vio::kNumColorSpaces] = {
    TJPF_UNKNOWN, TJPF_GRAY, TJPF_RGB,  TJPF_UNKNOWN, TJPF_CMYK, TJPF_UNKNOWN,
    TJPF_RGB,     TJPF_RGBX, TJPF_BGR,  TJPF_BGRX,    TJPF_XBGR, TJPF_XRGB,
    TJPF_RGBA,    TJPF_BGRA, TJPF_ABGR, TJPF_ARGB,    TJPF_UNKNOWN};

constexpr int NUMSF = 16;
const tjscalingfactor sf[NUMSF] = {{2, 1}, {15, 8}, {7, 4}, {13, 8}, {3, 2}, {11, 8},
                                   {5, 4}, {9, 8},  {1, 1}, {7, 8},  {3, 4}, {5, 8},
                                   {1, 2}, {3, 8},  {1, 4}, {1, 8}};

constexpr vj::TransformOp xformtypes[TJ_NUMXOP] = {
    vj::TransformOp::None,       vj::TransformOp::FlipH, vj::TransformOp::FlipV,
    vj::TransformOp::Transpose,  vj::TransformOp::Transverse, vj::TransformOp::Rot90,
    vj::TransformOp::Rot180,     vj::TransformOp::Rot270};

enum { COMPRESS = 1, DECOMPRESS = 2 };

inline long long PAD(long long v, long long p) { return (v + p - 1) & ~(p - 1); }
inline bool IS_POW2(long long x) { return (x & (x - 1)) == 0; }

// Thrown by THROW(): message already formatted into the error strings.
struct TjThrow {};

struct tjinstance {
  vj::Encoder enc;
  vj::Decoder dec;
  int init = 0, apiVersion = 0, numSamp = 0;
  char errStr[JMSG_LENGTH_MAX] = "No error";
  bool isInstanceError = false;
  std::vector<unsigned char> iccBuf, decompICCBuf;
  bool warning = false, stopOnWarning = false;

  bool bottomUp = false;
  bool noRealloc = false;
  int quality = -1;
  int subsamp = TJSAMP_UNKNOWN;
  int jpegWidth = -1, jpegHeight = -1;
  int precision = 8;
  int colorspace = TJCS_DEFAULT;
  bool fastUpsample = false, fastDCT = false, optimize = false, progressive = false;
  int scanLimit = 0;
  bool arithmetic = false, lossless = false;
  int losslessPSV = 1, losslessPt = 0;
  int restartIntervalBlocks = 0, restartIntervalRows = 0;
  int xDensity = 1, yDensity = 1, densityUnits = 0;
  tjscalingfactor scalingFactor = {1, 1};
  tjregion croppingRegion = {0, 0, 0, 0};
  int maxMemory = 0, maxPixels = 0;
  int saveMarkers = 2;

  tjinstance() {
    auto on_warning = [this](vio::Diagnostics& d, const std::string& msg) {
      // libjpeg's emit_message(-1) prints only the first warning.
      if (d.num_warnings == 1) {
        isInstanceError = true;
        std::snprintf(errStr, JMSG_LENGTH_MAX, "%s", msg.c_str());
      }
      warning = true;
      if (stopOnWarning) throw vio::Error(msg);
    };
    enc.diagnostics().on_warning = [this, on_warning](const std::string& m) {
      on_warning(enc.diagnostics(), m);
    };
    dec.diagnostics().on_warning = [this, on_warning](const std::string& m) {
      on_warning(dec.diagnostics(), m);
    };
  }

  vd::Compressor& c() { return enc.core(); }
  vd::CState& cs() { return enc.core().cs; }
  vd::Decompressor& d() { return dec.core(); }
  vd::DState& ds() { return dec.core().st; }

  // Error reporting helpers (THROW/THROWI/THROWG in turbojpeg.c).  Messages
  // are truncated to JMSG_LENGTH_MAX, as in turbojpeg.c.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
  [[noreturn]] void fail(const char* fn, const char* fmt, ...) {
    char msg[JMSG_LENGTH_MAX];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    std::snprintf(errStr, JMSG_LENGTH_MAX, "%s(): %s", fn, msg);
    isInstanceError = true;
    std::snprintf(::errStr, JMSG_LENGTH_MAX, "%s(): %s", fn, msg);
    throw TjThrow{};
  }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
  // A libjpeg error (my_output_message): instance error string only.
  void library_error(const char* msg) {
    isInstanceError = true;
    std::snprintf(errStr, JMSG_LENGTH_MAX, "%s", msg);
  }
  void begin() {
    warning = false;
    isInstanceError = false;
    enc.diagnostics().num_warnings = 0;
    dec.diagnostics().num_warnings = 0;
  }
};

[[noreturn]] void throwg(const char* fn, const char* m) {
  std::snprintf(errStr, JMSG_LENGTH_MAX, "%s(): %s", fn, m);
  throw TjThrow{};
}

// Runs `body` with turbojpeg.c's error semantics; returns `error_value` on
// failure.  `cleanup` runs on every exit (the "bailout" label).
template <class R, class F, class C>
R guarded(tjinstance* self, R error_value, F&& body, C&& cleanup) {
  R retval = error_value;
  try {
    retval = body();
  } catch (const TjThrow&) {
    retval = error_value;
  } catch (const vio::Error& e) {
    if (self) self->library_error(e.what());
    retval = error_value;
  } catch (const std::bad_alloc&) {
    if (self) self->library_error("Insufficient memory");
    retval = error_value;
  }
  cleanup();
  return retval;
}

#define GET_TJINSTANCE(handle, errorReturn)                                        \
  tjinstance* self = static_cast<tjinstance*>(handle);                             \
  if (!self) {                                                                     \
    std::snprintf(errStr, JMSG_LENGTH_MAX, "%s(): Invalid handle", FUNCTION_NAME); \
    return errorReturn;                                                            \
  }                                                                                \
  self->begin();

#define THROW(m) self->fail(FUNCTION_NAME, "%s", m)

// ---------------------------------------------------------------------------
// Memory destination (jdatadst-tj.c semantics)
void store_output(tjinstance* self, const std::vector<std::uint8_t>& data, unsigned char** buf,
                  size_t* size, bool alloc) {
  if (!alloc) {
    if (data.size() > *size) {
      throw vio::Error("Buffer passed to JPEG library is too small");
    }
    std::memcpy(*buf, data.data(), data.size());
  } else if (*buf == nullptr || *size < data.size() || *size == 0) {
    unsigned char* nb = static_cast<unsigned char*>(std::malloc(data.size() ? data.size() : 1));
    if (!nb) throw vio::Error("Insufficient memory (case 10)");
    std::memcpy(nb, data.data(), data.size());
    std::free(*buf);
    *buf = nb;
  } else {
    std::memcpy(*buf, data.data(), data.size());
  }
  *size = data.size();
  (void)self;
}

void check_dest(unsigned char** buf, size_t* size, bool alloc) {
  if (buf == nullptr || size == nullptr) throw vio::Error("Buffer passed to JPEG library is too small");
  if ((*buf == nullptr || *size == 0) && !alloc)
    throw vio::Error("Buffer passed to JPEG library is too small");
}

// ---------------------------------------------------------------------------
void setCompDefaults(tjinstance* self, int pixelFormat, bool yuv) {
  vd::CState& cs = self->cs();
  int colorspace = yuv ? TJCS_DEFAULT : self->colorspace;
  cs.in_color_space = pf2cs[pixelFormat];
  cs.input_components = tjPixelSize[pixelFormat];
  cs.set_defaults();
  cs.restart_interval = static_cast<unsigned>(self->restartIntervalBlocks);
  cs.restart_in_rows = self->restartIntervalRows;
  cs.X_density = static_cast<std::uint16_t>(self->xDensity);
  cs.Y_density = static_cast<std::uint16_t>(self->yDensity);
  cs.density_unit = static_cast<std::uint8_t>(self->densityUnits);
  if (self->lossless && !yuv) {
    cs.enable_lossless(self->losslessPSV, self->losslessPt);
    return;
  }
  cs.set_quality(self->quality, true);
  cs.dct_method = self->fastDCT ? vj::DctMethod::IntFast : vj::DctMethod::IntSlow;
  switch (colorspace) {
    case TJCS_RGB: cs.set_colorspace(ColorSpace::RGB); break;
    case TJCS_YCbCr: cs.set_colorspace(ColorSpace::YCbCr); break;
    case TJCS_GRAY: cs.set_colorspace(ColorSpace::Gray); break;
    case TJCS_CMYK: cs.set_colorspace(ColorSpace::CMYK); break;
    case TJCS_YCCK: cs.set_colorspace(ColorSpace::YCCK); break;
    default:
      if (self->subsamp == TJSAMP_GRAY) cs.set_colorspace(ColorSpace::Gray);
      else if (pixelFormat == TJPF_CMYK) cs.set_colorspace(ColorSpace::YCCK);
      else cs.set_colorspace(ColorSpace::YCbCr);
  }
  if (cs.data_precision == 8) cs.optimize_coding = self->optimize;
  if (self->progressive) cs.simple_progression();
  cs.arith_code = self->arithmetic;
  cs.comp_info[0].h_samp_factor = tjMCUWidth[self->subsamp] / 8;
  cs.comp_info[1].h_samp_factor = 1;
  cs.comp_info[2].h_samp_factor = 1;
  if (cs.num_components > 3) cs.comp_info[3].h_samp_factor = tjMCUWidth[self->subsamp] / 8;
  cs.comp_info[0].v_samp_factor = tjMCUHeight[self->subsamp] / 8;
  cs.comp_info[1].v_samp_factor = 1;
  cs.comp_info[2].v_samp_factor = 1;
  if (cs.num_components > 3) cs.comp_info[3].v_samp_factor = tjMCUHeight[self->subsamp] / 8;
}

int getSubsamp(tjinstance* self) {
  vd::DState& d = self->ds();
  int retval = TJSAMP_UNKNOWN;
  if (d.num_components == 1 && d.jpeg_color_space == ColorSpace::Gray) return TJSAMP_GRAY;
  const bool cmyk = d.jpeg_color_space == ColorSpace::YCCK || d.jpeg_color_space == ColorSpace::CMYK;
  for (int i = 0; i < self->numSamp; i++) {
    if (i == TJSAMP_GRAY) continue;
    if (d.num_components == 3 || (cmyk && d.num_components == 4)) {
      const auto& c = d.comp_info;
      if (c[0].h_samp_factor == tjMCUWidth[i] / 8 && c[0].v_samp_factor == tjMCUHeight[i] / 8) {
        int match = 0;
        for (int k = 1; k < d.num_components; k++) {
          int href = 1, vref = 1;
          if (cmyk && k == 3) {
            href = tjMCUWidth[i] / 8;
            vref = tjMCUHeight[i] / 8;
          }
          if (c[k].h_samp_factor == href && c[k].v_samp_factor == vref) match++;
        }
        if (match == d.num_components - 1) {
          retval = i;
          break;
        }
      }
      if (c[0].h_samp_factor == 2 && c[0].v_samp_factor == 2 &&
          (i == TJSAMP_422 || i == TJSAMP_440)) {
        int match = 0;
        for (int k = 1; k < d.num_components; k++) {
          int href = tjMCUHeight[i] / 8, vref = tjMCUWidth[i] / 8;
          if (cmyk && k == 3) href = vref = 2;
          if (c[k].h_samp_factor == href && c[k].v_samp_factor == vref) match++;
        }
        if (match == d.num_components - 1) {
          retval = i;
          break;
        }
      }
      if (c[0].h_samp_factor * c[0].v_samp_factor <= vj::D_MAX_BLOCKS_IN_MCU / 3 &&
          i == TJSAMP_444) {
        int match = 0;
        for (int k = 1; k < d.num_components; k++) {
          if (c[k].h_samp_factor == c[0].h_samp_factor && c[k].v_samp_factor == c[0].v_samp_factor)
            match++;
          if (match == d.num_components - 1) {
            retval = i;
            break;
          }
        }
      }
    }
  }
  return retval;
}

void setDecompParameters(tjinstance* self) {
  vd::DState& d = self->ds();
  self->subsamp = getSubsamp(self);
  self->jpegWidth = static_cast<int>(d.image_width);
  self->jpegHeight = static_cast<int>(d.image_height);
  self->precision = d.data_precision;
  switch (d.jpeg_color_space) {
    case ColorSpace::Gray: self->colorspace = TJCS_GRAY; break;
    case ColorSpace::RGB: self->colorspace = TJCS_RGB; break;
    case ColorSpace::YCbCr: self->colorspace = TJCS_YCbCr; break;
    case ColorSpace::CMYK: self->colorspace = TJCS_CMYK; break;
    case ColorSpace::YCCK: self->colorspace = TJCS_YCCK; break;
    default: self->colorspace = TJCS_DEFAULT; break;
  }
  self->progressive = d.progressive_mode;
  self->arithmetic = d.arith_code;
  self->lossless = d.lossless;
  self->losslessPSV = d.Ss;
  self->losslessPt = d.Al;
  self->xDensity = d.X_density;
  self->yDensity = d.Y_density;
  self->densityUnits = d.density_unit;
}

void processFlags(tjinstance* self, int flags, int operation) {
  self->bottomUp = !!(flags & TJFLAG_BOTTOMUP);
  self->fastUpsample = !!(flags & TJFLAG_FASTUPSAMPLE);
  self->noRealloc = !!(flags & TJFLAG_NOREALLOC);
  if (operation == COMPRESS) {
    self->fastDCT = !(self->quality >= 96 || (flags & TJFLAG_ACCURATEDCT));
  } else {
    self->fastDCT = !!(flags & TJFLAG_FASTDCT);
  }
  self->stopOnWarning = !!(flags & TJFLAG_STOPONWARNING);
  self->progressive = !!(flags & TJFLAG_PROGRESSIVE);
  if (flags & TJFLAG_LIMITSCANS) self->scanLimit = 500;
}

int getPixelFormat(int pixelSize, int flags) {
  if (pixelSize == 1) return TJPF_GRAY;
  if (pixelSize == 3) return (flags & TJ_BGR) ? TJPF_BGR : TJPF_RGB;
  if (pixelSize == 4) {
    if (flags & TJ_ALPHAFIRST) return (flags & TJ_BGR) ? TJPF_XBGR : TJPF_XRGB;
    return (flags & TJ_BGR) ? TJPF_BGRX : TJPF_RGBX;
  }
  return -1;
}

// Installs the scan-limit progress hook for decompression operations.
void setup_progress(tjinstance* self) {
  if (self->scanLimit) {
    self->d().progress_monitor = [self] {
      int scan_no = self->ds().input_scan_number;
      if (scan_no > self->scanLimit) {
        std::snprintf(self->errStr, JMSG_LENGTH_MAX,
                      "Progressive JPEG image has more than %d scans", self->scanLimit);
        std::snprintf(errStr, JMSG_LENGTH_MAX, "Progressive JPEG image has more than %d scans",
                      self->scanLimit);
        self->isInstanceError = true;
        self->warning = false;
        throw TjThrow{};
      }
    };
  } else {
    self->d().progress_monitor = nullptr;
  }
}

bool dstate_before_header(tjinstance* self) {
  auto s = self->d().state;
  return s == vd::Decompressor::State::Start || s == vd::Decompressor::State::InHeader;
}

void abort_compress(tjinstance* self) {
  if (self->c().state != vd::Compressor::State::Start) self->c().abort();
}
void abort_decompress(tjinstance* self) {
  if (self->d().state != vd::Decompressor::State::Start) self->d().abort();
}

template <class T>
constexpr int kBits = std::is_same_v<T, std::uint8_t> ? 8 : std::is_same_v<T, std::int16_t> ? 12 : 16;

// ---------------------------------------------------------------------------
// tj3Compress*()
template <class T>
int compress_impl(tjhandle handle, const T* srcBuf, int width, int pitch, int height,
                  int pixelFormat, unsigned char** jpegBuf, size_t* jpegSize,
                  const char* FUNCTION_NAME) {
  GET_TJINSTANCE(handle, -1)
  return guarded<int>(self, -1, [&]() -> int {
    if ((self->init & COMPRESS) == 0) THROW("Instance has not been initialized for compression");
    if (srcBuf == nullptr || width <= 0 || pitch < 0 || height <= 0 || pixelFormat < 0 ||
        pixelFormat >= TJ_NUMPF || jpegBuf == nullptr || jpegSize == nullptr)
      THROW("Invalid argument");
    if (!self->lossless && self->quality == -1) THROW("TJPARAM_QUALITY must be specified");
    if (!self->lossless && self->subsamp == TJSAMP_UNKNOWN)
      THROW("TJPARAM_SUBSAMP must be specified");
    if (pitch == 0) pitch = width * tjPixelSize[pixelFormat];
    else if (pitch < width * tjPixelSize[pixelFormat]) THROW("Invalid argument");
    vd::CState& cs = self->cs();
    cs.image_width = static_cast<unsigned>(width);
    cs.image_height = static_cast<unsigned>(height);
    cs.data_precision = kBits<T>;
    constexpr int lo = kBits<T> == 8 ? 2 : kBits<T> - 3;
    if (self->lossless && self->precision >= lo && self->precision <= kBits<T>)
      cs.data_precision = self->precision;
    setCompDefaults(self, pixelFormat, false);
    bool alloc = !self->noRealloc;
    check_dest(jpegBuf, jpegSize, alloc);
    self->c().start_compress(true);
    if (!self->iccBuf.empty()) self->enc.write_icc_profile(self->iccBuf);
    std::vector<T*> rows(static_cast<std::size_t>(height));
    for (int i = 0; i < height; i++) {
      const T* p = self->bottomUp ? &srcBuf[(height - i - 1) * static_cast<std::size_t>(pitch)]
                                  : &srcBuf[i * static_cast<std::size_t>(pitch)];
      rows[i] = const_cast<T*>(p);
    }
    while (cs.next_scanline < cs.image_height)
      self->c().write_scanlines<T>(&rows[cs.next_scanline], cs.image_height - cs.next_scanline);
    self->c().finish_compress();
    store_output(self, self->c().take_output(), jpegBuf, jpegSize, alloc);
    return 0;
  }, [&] {
    abort_compress(self);
    if (self->warning) { /* retval adjusted below */ }
  }) == -1 || self->warning ? -1 : 0;
}

// tj3Decompress*()
template <class T>
int decompress_impl(tjhandle handle, const unsigned char* jpegBuf, size_t jpegSize, T* dstBuf,
                    int pitch, int pixelFormat, const char* FUNCTION_NAME) {
  GET_TJINSTANCE(handle, -1)
  int r = guarded<int>(self, -1, [&]() -> int {
    if ((self->init & DECOMPRESS) == 0)
      THROW("Instance has not been initialized for decompression");
    if (jpegBuf == nullptr || jpegSize <= 0 || dstBuf == nullptr || pitch < 0 ||
        pixelFormat < 0 || pixelFormat >= TJ_NUMPF)
      THROW("Invalid argument");
    setup_progress(self);
    vd::Decompressor& d = self->d();
    vd::DState& ds = d.st;
    if (dstate_before_header(self)) {
      d.set_source(jpegBuf, jpegSize);
      d.read_header(true);
    }
    setDecompParameters(self);
    if constexpr (kBits<T> == 12) {
      if (self->precision == 8 && !self->lossless) ds.data_precision = 12;
    }
    if (self->maxPixels && static_cast<unsigned long long>(self->jpegWidth) * self->jpegHeight >
                               static_cast<unsigned long long>(self->maxPixels))
      THROW("Image is too large");
    ds.out_color_space = pf2cs[pixelFormat];
    int scaledWidth = TJSCALED(static_cast<int>(ds.image_width), self->scalingFactor);
    ds.do_fancy_upsampling = !self->fastUpsample;
    ds.dct_method = self->fastDCT ? vj::DctMethod::IntFast : vj::DctMethod::IntSlow;
    ds.scale_num = static_cast<unsigned>(self->scalingFactor.num);
    ds.scale_denom = static_cast<unsigned>(self->scalingFactor.denom);
    d.start_decompress();
    const tjregion& cr = self->croppingRegion;
    if constexpr (kBits<T> != 16) {
      if (cr.x != 0 || (cr.w != 0 && cr.w != scaledWidth)) {
        unsigned crop_x = static_cast<unsigned>(cr.x), crop_w = static_cast<unsigned>(cr.w);
        d.crop_scanline<T>(&crop_x, &crop_w);
        if (static_cast<int>(crop_x) != cr.x)
          self->fail(FUNCTION_NAME,
                     "Unexplained mismatch between specified (%d) and\n"
                     "actual (%d) cropping region left boundary",
                     cr.x, static_cast<int>(crop_x));
        if (static_cast<int>(crop_w) != cr.w)
          self->fail(FUNCTION_NAME,
                     "Unexplained mismatch between specified (%d) and\n"
                     "actual (%d) cropping region width",
                     cr.w, static_cast<int>(crop_w));
      }
    }
    if (pitch == 0) pitch = static_cast<int>(ds.output_width) * tjPixelSize[pixelFormat];
    else if (static_cast<unsigned>(pitch) < ds.output_width * tjPixelSize[pixelFormat])
      THROW("Invalid argument");
    int croppedHeight = static_cast<int>(ds.output_height);
    if constexpr (kBits<T> != 16) {
      if (cr.y != 0 || cr.h != 0) croppedHeight = cr.h;
    }
    std::vector<T*> rows(static_cast<std::size_t>(croppedHeight));
    for (int i = 0; i < croppedHeight; i++)
      rows[i] = self->bottomUp ? &dstBuf[(croppedHeight - i - 1) * static_cast<std::size_t>(pitch)]
                               : &dstBuf[i * static_cast<std::size_t>(pitch)];
    bool cropped = false;
    if constexpr (kBits<T> != 16) cropped = cr.y != 0 || cr.h != 0;
    if (cropped) {
      if (cr.y != 0) {
        unsigned lines = d.skip_scanlines<T>(static_cast<unsigned>(cr.y));
        if (static_cast<int>(lines) != cr.y)
          self->fail(FUNCTION_NAME,
                     "Unexplained mismatch between specified (%d) and\n"
                     "actual (%d) cropping region upper boundary",
                     cr.y, static_cast<int>(lines));
      }
      while (static_cast<int>(ds.output_scanline) < cr.y + cr.h)
        d.read_scanlines<T>(&rows[ds.output_scanline - cr.y],
                            static_cast<unsigned>(cr.y + cr.h) - ds.output_scanline);
      if (cr.y + cr.h != static_cast<int>(ds.output_height)) {
        unsigned want = ds.output_height - cr.y - cr.h;
        unsigned lines = d.skip_scanlines<T>(want);
        if (lines != want)
          self->fail(FUNCTION_NAME,
                     "Unexplained mismatch between specified (%d) and\n"
                     "actual (%d) cropping region lower boundary",
                     cr.y + cr.h, static_cast<int>(ds.output_height - lines));
      }
    } else {
      while (ds.output_scanline < ds.output_height)
        d.read_scanlines<T>(&rows[ds.output_scanline], ds.output_height - ds.output_scanline);
    }
    d.finish_decompress();
    return 0;
  }, [&] { abort_decompress(self); });
  return (r == -1 || self->warning) ? -1 : 0;
}

// ---------------------------------------------------------------------------
// Packed-pixel image I/O (tj3LoadImage*/tj3SaveImage*)
template <class T>
T* load_image_impl(tjhandle handle, const char* filename, int* width, int align, int* height,
                   int* pixelFormat, const char* FUNCTION_NAME) {
  GET_TJINSTANCE(handle, nullptr)
  T* dstBuf = nullptr;
  T* r = guarded<T*>(self, nullptr, [&]() -> T* {
    if (!filename) THROW("Invalid argument");
    std::FILE* file = std::fopen(filename, "rb");
    if (!file) {
      std::snprintf(self->errStr, JMSG_LENGTH_MAX, "%s(): %s\n%s", FUNCTION_NAME,
                    "Cannot open input file", std::strerror(errno));
      self->isInstanceError = true;
      std::snprintf(errStr, JMSG_LENGTH_MAX, "%s(): %s\n%s", FUNCTION_NAME,
                    "Cannot open input file", std::strerror(errno));
      throw TjThrow{};
    }
    std::vector<std::uint8_t> data;
    std::uint8_t tmp[65536];
    std::size_t n;
    while ((n = std::fread(tmp, 1, sizeof(tmp), file)) > 0) data.insert(data.end(), tmp, tmp + n);
    std::fclose(file);
    if (!width || align < 1 || !height || !pixelFormat || *pixelFormat < TJPF_UNKNOWN ||
        *pixelFormat >= TJ_NUMPF)
      THROW("Invalid argument");
    if ((align & (align - 1)) != 0) THROW("Alignment must be a power of 2");
    if (data.empty()) THROW("Input file contains no data");
    int tempc = data[0];
    std::unique_ptr<vio::ImageReader<T>> src;
    bool invert;
    int precision = kBits<T>;
    constexpr int lo = kBits<T> == 8 ? 2 : kBits<T> - 3;
    if (tempc == 'B') {
      if constexpr (kBits<T> == 8) {
        auto bmp = std::make_unique<vio::bmp::Reader>();
        bmp->use_inversion_array = false;
        src = std::move(bmp);
      } else {
        throw vio::Error(vio::strformat("Unsupported JPEG data precision %d", kBits<T>));
      }
      invert = !self->bottomUp;
    } else if (tempc == 0x89) {
      if (self->precision >= lo && self->precision <= kBits<T>) precision = self->precision;
      src = std::make_unique<vio::png::Reader<T>>();
      invert = self->bottomUp;
    } else if (tempc == 'P') {
      if (self->precision >= lo && self->precision <= kBits<T>) precision = self->precision;
      src = std::make_unique<vio::ppm::Reader<T>>();
      invert = self->bottomUp;
    } else {
      THROW("Unsupported file type");
    }
    src->precision = precision;
    src->color = *pixelFormat == TJPF_UNKNOWN ? ColorSpace::Unknown : pf2cs[*pixelFormat];
    src->max_pixels = static_cast<unsigned long long>(self->maxPixels);
    src->start(data);
    if (tempc == 'B') {
      if (src->x_density && src->y_density) {
        self->xDensity = src->x_density;
        self->yDensity = src->y_density;
        self->densityUnits = src->density_unit;
      }
    } else if (tempc == 0x89 && (self->init & COMPRESS) &&
               (self->saveMarkers == 2 || self->saveMarkers == 4)) {
      std::vector<std::uint8_t> icc = src->icc_profile();
      if (!icc.empty()) self->iccBuf = icc;
    }
    *width = static_cast<int>(src->width);
    *height = static_cast<int>(src->height);
    *pixelFormat = cs2pf[static_cast<int>(src->color)];
    std::size_t pitch = static_cast<std::size_t>(PAD(static_cast<long long>(*width) * tjPixelSize[*pixelFormat], align));
    dstBuf = static_cast<T*>(std::malloc(pitch * (*height) * sizeof(T)));
    if (!dstBuf) THROW("Memory allocation failure");
    while (src->next_row < src->height) {
      unsigned row0 = src->next_row;
      unsigned nlines = src->read_rows();
      for (unsigned i = 0; i < nlines; i++) {
        int row = static_cast<int>(row0 + i);
        T* dstptr = invert ? &dstBuf[(*height - row - 1) * pitch] : &dstBuf[row * pitch];
        std::memcpy(dstptr, src->rows()[i], static_cast<std::size_t>(*width) * tjPixelSize[*pixelFormat] * sizeof(T));
      }
    }
    return dstBuf;
  }, [] {});
  if (!r) std::free(dstBuf);
  return r;
}

template <class T>
int save_image_impl(tjhandle handle, const char* filename, const T* buffer, int width, int pitch,
                    int height, int pixelFormat, const char* FUNCTION_NAME) {
  GET_TJINSTANCE(handle, -1)
  return guarded<int>(self, -1, [&]() -> int {
    if (!filename || !buffer || width < 1 || pitch < 0 || height < 1 || pixelFormat < 0 ||
        pixelFormat >= TJ_NUMPF)
      THROW("Invalid argument");
    std::FILE* file = std::fopen(filename, "wb");
    if (!file) {
      std::snprintf(self->errStr, JMSG_LENGTH_MAX, "%s(): %s\n%s", FUNCTION_NAME,
                    "Cannot open output file", std::strerror(errno));
      self->isInstanceError = true;
      std::snprintf(errStr, JMSG_LENGTH_MAX, "%s(): %s\n%s", FUNCTION_NAME,
                    "Cannot open output file", std::strerror(errno));
      throw TjThrow{};
    }
    struct Closer { std::FILE* f; ~Closer() { std::fclose(f); } } closer{file};
    std::unique_ptr<vio::ImageWriter<T>> dst;
    bool invert;
    int precision = kBits<T>;
    constexpr int lo = kBits<T> == 8 ? 2 : kBits<T> - 3;
    const char* ptr = std::strrchr(filename, '.');
    if (ptr && !strcasecmp(ptr, ".bmp")) {
      if constexpr (kBits<T> == 8) {
        auto bmp = std::make_unique<vio::bmp::Writer>();
        bmp->use_inversion_array = false;
        dst = std::move(bmp);
      } else {
        throw vio::Error(vio::strformat("Unsupported JPEG data precision %d", kBits<T>));
      }
      invert = !self->bottomUp;
      dst->x_density = static_cast<std::uint16_t>(self->xDensity);
      dst->y_density = static_cast<std::uint16_t>(self->yDensity);
      dst->density_unit = static_cast<std::uint8_t>(self->densityUnits);
    } else if (ptr && !strcasecmp(ptr, ".png")) {
      if (self->precision >= lo && self->precision <= kBits<T>) precision = self->precision;
      dst = std::make_unique<vio::png::Writer<T>>();
      invert = self->bottomUp;
      if ((self->init & DECOMPRESS) && !self->decompICCBuf.empty())
        dst->set_icc_profile(self->decompICCBuf);
    } else {
      if (self->precision >= lo && self->precision <= kBits<T>) precision = self->precision;
      dst = std::make_unique<vio::ppm::Writer<T>>();
      invert = self->bottomUp;
    }
    dst->width = static_cast<unsigned>(width);
    dst->height = static_cast<unsigned>(height);
    dst->color = pf2cs[pixelFormat];
    dst->components = pixelFormat == TJPF_GRAY ? 1 : tjPixelSize[pixelFormat];
    dst->precision = precision;
    dst->start();
    if (pitch == 0) pitch = width * tjPixelSize[pixelFormat];
    else if (pitch < width * tjPixelSize[pixelFormat]) THROW("Invalid argument");
    std::vector<T> row(static_cast<std::size_t>(width) * tjPixelSize[pixelFormat] + 16);
    for (int y = 0; y < height; y++) {
      const T* rowptr = invert ? &buffer[(height - y - 1) * static_cast<std::size_t>(pitch)]
                               : &buffer[y * static_cast<std::size_t>(pitch)];
      std::memcpy(row.data(), rowptr, static_cast<std::size_t>(width) * tjPixelSize[pixelFormat] * sizeof(T));
      T* rp = row.data();
      dst->write_rows(&rp, 1);
    }
    std::vector<std::uint8_t> out = dst->finish();
    if (!out.empty() && std::fwrite(out.data(), 1, out.size(), file) != out.size())
      throw vio::Error("Output file write error --- out of disk space?");
    return 0;
  }, [] {});
}

}  // namespace

// ===========================================================================
// Exported API
// ===========================================================================
extern "C" {

#undef tj3Init

DLLEXPORT tjhandle tj3InitVersion(int initType, int apiVersion) {
  static const char FUNCTION_NAME[] = "tj3Init";
  try {
    if (initType < 0 || initType >= TJ_NUMINIT || apiVersion < 1000000 || apiVersion > 999999999)
      throwg(FUNCTION_NAME, "Invalid argument");
    auto* self = new tjinstance();
    self->apiVersion = apiVersion;
    self->numSamp = apiVersion >= 3002000 ? TJ_NUMSAMP : 7;
    switch (initType) {
      case TJINIT_COMPRESS: self->init = COMPRESS; break;
      case TJINIT_DECOMPRESS: self->init = DECOMPRESS; break;
      case TJINIT_TRANSFORM: self->init = COMPRESS | DECOMPRESS; break;
    }
    return self;
  } catch (const TjThrow&) {
    return nullptr;
  } catch (const std::bad_alloc&) {
    std::snprintf(errStr, JMSG_LENGTH_MAX, "%s(): Memory allocation failure", FUNCTION_NAME);
    return nullptr;
  }
}

DLLEXPORT tjhandle tj3Init(int initType) { return tj3InitVersion(initType, 3001000); }

DLLEXPORT void tj3Destroy(tjhandle handle) { delete static_cast<tjinstance*>(handle); }

DLLEXPORT char* tj3GetErrorStr(tjhandle handle) {
  tjinstance* self = static_cast<tjinstance*>(handle);
  if (self && self->isInstanceError) {
    self->isInstanceError = false;
    return self->errStr;
  }
  return errStr;
}

DLLEXPORT int tj3GetErrorCode(tjhandle handle) {
  tjinstance* self = static_cast<tjinstance*>(handle);
  if (self && self->warning) return TJERR_WARNING;
  return TJERR_FATAL;
}

DLLEXPORT int tj3Set(tjhandle handle, int param, int value) {
  static const char FUNCTION_NAME[] = "tj3Set";
  GET_TJINSTANCE(handle, -1)
  return guarded<int>(self, -1, [&]() -> int {
    auto set = [&](int& field, int minValue, int maxValue) {
      if (value < minValue || (maxValue > 0 && value > maxValue))
        THROW("Parameter value out of range");
      field = value;
    };
    auto setb = [&](bool& field) {
      if (value < 0 || value > 1) THROW("Parameter value out of range");
      field = value != 0;
    };
    const bool c = self->init & COMPRESS, d = self->init & DECOMPRESS;
    switch (param) {
      case TJPARAM_STOPONWARNING: setb(self->stopOnWarning); break;
      case TJPARAM_BOTTOMUP: setb(self->bottomUp); break;
      case TJPARAM_NOREALLOC:
        if (!c) THROW("TJPARAM_NOREALLOC is not applicable to decompression instances.");
        setb(self->noRealloc);
        break;
      case TJPARAM_QUALITY:
        if (!c) THROW("TJPARAM_QUALITY is not applicable to decompression instances.");
        set(self->quality, 1, 100);
        break;
      case TJPARAM_SUBSAMP: set(self->subsamp, 0, self->numSamp - 1); break;
      case TJPARAM_JPEGWIDTH:
        if (!d) THROW("TJPARAM_JPEGWIDTH is not applicable to compression instances.");
        THROW("TJPARAM_JPEGWIDTH is read-only in decompression instances.");
      case TJPARAM_JPEGHEIGHT:
        if (!d) THROW("TJPARAM_JPEGHEIGHT is not applicable to compression instances.");
        THROW("TJPARAM_JPEGHEIGHT is read-only in decompression instances.");
      case TJPARAM_PRECISION: set(self->precision, 2, 16); break;
      case TJPARAM_COLORSPACE:
        if (!c) THROW("TJPARAM_COLORSPACE is read-only in decompression instances.");
        set(self->colorspace, TJCS_DEFAULT, TJ_NUMCS - 1);
        break;
      case TJPARAM_FASTUPSAMPLE:
        if (!d) THROW("TJPARAM_FASTUPSAMPLE is not applicable to compression instances.");
        setb(self->fastUpsample);
        break;
      case TJPARAM_FASTDCT: setb(self->fastDCT); break;
      case TJPARAM_OPTIMIZE:
        if (!c) THROW("TJPARAM_OPTIMIZE is not applicable to decompression instances.");
        setb(self->optimize);
        break;
      case TJPARAM_PROGRESSIVE:
        if (!c) THROW("TJPARAM_PROGRESSIVE is read-only in decompression instances.");
        setb(self->progressive);
        break;
      case TJPARAM_SCANLIMIT:
        if (!d) THROW("TJPARAM_SCANLIMIT is not applicable to compression instances.");
        set(self->scanLimit, 0, -1);
        break;
      case TJPARAM_ARITHMETIC:
        if (!c) THROW("TJPARAM_ARITHMETIC is read-only in decompression instances.");
        setb(self->arithmetic);
        break;
      case TJPARAM_LOSSLESS:
        if (!c) THROW("TJPARAM_LOSSLESS is read-only in decompression instances.");
        setb(self->lossless);
        break;
      case TJPARAM_LOSSLESSPSV:
        if (!c) THROW("TJPARAM_LOSSLESSPSV is read-only in decompression instances.");
        set(self->losslessPSV, 1, 7);
        break;
      case TJPARAM_LOSSLESSPT:
        if (!c) THROW("TJPARAM_LOSSLESSPT is read-only in decompression instances.");
        set(self->losslessPt, 0, 15);
        break;
      case TJPARAM_RESTARTBLOCKS:
        if (!c) THROW("TJPARAM_RESTARTBLOCKS is not applicable to decompression instances.");
        set(self->restartIntervalBlocks, 0, 65535);
        if (value != 0) self->restartIntervalRows = 0;
        break;
      case TJPARAM_RESTARTROWS:
        if (!c) THROW("TJPARAM_RESTARTROWS is not applicable to decompression instances.");
        set(self->restartIntervalRows, 0, 65535);
        if (value != 0) self->restartIntervalBlocks = 0;
        break;
      case TJPARAM_XDENSITY:
        if (!c) THROW("TJPARAM_XDENSITY is read-only in decompression instances.");
        set(self->xDensity, 1, 65535);
        break;
      case TJPARAM_YDENSITY:
        if (!c) THROW("TJPARAM_YDENSITY is read-only in decompression instances.");
        set(self->yDensity, 1, 65535);
        break;
      case TJPARAM_DENSITYUNITS:
        if (!c) THROW("TJPARAM_DENSITYUNITS is read-only in decompression instances.");
        set(self->densityUnits, 0, 2);
        break;
      case TJPARAM_MAXMEMORY:
        set(self->maxMemory, 0, static_cast<int>(std::min(LONG_MAX / 1048576L, static_cast<long>(INT_MAX))));
        break;
      case TJPARAM_MAXPIXELS: set(self->maxPixels, 0, -1); break;
      case TJPARAM_SAVEMARKERS: set(self->saveMarkers, 0, 4); break;
      default: THROW("Invalid parameter");
    }
    return 0;
  }, [] {});
}

DLLEXPORT int tj3Get(tjhandle handle, int param) {
  tjinstance* self = static_cast<tjinstance*>(handle);
  if (!self) return -1;
  switch (param) {
    case TJPARAM_STOPONWARNING: return self->stopOnWarning;
    case TJPARAM_BOTTOMUP: return self->bottomUp;
    case TJPARAM_NOREALLOC: return self->noRealloc;
    case TJPARAM_QUALITY: return self->quality;
    case TJPARAM_SUBSAMP: return self->subsamp;
    case TJPARAM_JPEGWIDTH: return self->jpegWidth;
    case TJPARAM_JPEGHEIGHT: return self->jpegHeight;
    case TJPARAM_PRECISION: return self->precision;
    case TJPARAM_COLORSPACE: return self->colorspace;
    case TJPARAM_FASTUPSAMPLE: return self->fastUpsample;
    case TJPARAM_FASTDCT: return self->fastDCT;
    case TJPARAM_OPTIMIZE: return self->optimize;
    case TJPARAM_PROGRESSIVE: return self->progressive;
    case TJPARAM_SCANLIMIT: return self->scanLimit;
    case TJPARAM_ARITHMETIC: return self->arithmetic;
    case TJPARAM_LOSSLESS: return self->lossless;
    case TJPARAM_LOSSLESSPSV: return self->losslessPSV;
    case TJPARAM_LOSSLESSPT: return self->losslessPt;
    case TJPARAM_RESTARTBLOCKS: return self->restartIntervalBlocks;
    case TJPARAM_RESTARTROWS: return self->restartIntervalRows;
    case TJPARAM_XDENSITY: return self->xDensity;
    case TJPARAM_YDENSITY: return self->yDensity;
    case TJPARAM_DENSITYUNITS: return self->densityUnits;
    case TJPARAM_MAXMEMORY: return self->maxMemory;
    case TJPARAM_MAXPIXELS: return self->maxPixels;
    case TJPARAM_SAVEMARKERS: return self->saveMarkers;
  }
  return -1;
}

DLLEXPORT void* tj3Alloc(size_t bytes) { return std::malloc(bytes); }
DLLEXPORT void tj3Free(void* buf) { std::free(buf); }

DLLEXPORT size_t tj3JPEGBufSize(int width, int height, int jpegSubsamp) {
  static const char FUNCTION_NAME[] = "tj3JPEGBufSize";
  try {
    if (width < 1 || height < 1 || jpegSubsamp < TJSAMP_UNKNOWN || jpegSubsamp >= TJ_NUMSAMP)
      throwg(FUNCTION_NAME, "Invalid argument");
    if (jpegSubsamp == TJSAMP_UNKNOWN) jpegSubsamp = TJSAMP_444;
    int mcuw = tjMCUWidth[jpegSubsamp], mcuh = tjMCUHeight[jpegSubsamp];
    int chromasf = jpegSubsamp == TJSAMP_GRAY ? 0 : 4 * 64 / (mcuw * mcuh);
    unsigned long long retval = static_cast<unsigned long long>(PAD(width, mcuw)) *
                                    PAD(height, mcuh) * (2ULL + chromasf) + 2048ULL;
    if (retval > static_cast<unsigned long long>(static_cast<unsigned long>(-1)))
      throwg(FUNCTION_NAME, "Image is too large");
    return static_cast<size_t>(retval);
  } catch (const TjThrow&) {
    return 0;
  }
}

DLLEXPORT unsigned long tjBufSize(int width, int height, int jpegSubsamp) {
  static const char FUNCTION_NAME[] = "tjBufSize";
  size_t retval;
  if (jpegSubsamp < 0) {
    std::snprintf(errStr, JMSG_LENGTH_MAX, "%s(): %s", FUNCTION_NAME, "Invalid argument");
    retval = 0;
  } else {
    retval = tj3JPEGBufSize(width, height, jpegSubsamp);
  }
  return retval == 0 ? static_cast<unsigned long>(-1) : static_cast<unsigned long>(retval);
}

DLLEXPORT unsigned long TJBUFSIZE(int width, int height) {
  static const char FUNCTION_NAME[] = "TJBUFSIZE";
  if (width < 1 || height < 1) {
    std::snprintf(errStr, JMSG_LENGTH_MAX, "%s(): %s", FUNCTION_NAME, "Invalid argument");
    return static_cast<unsigned long>(-1);
  }
  unsigned long long retval =
      static_cast<unsigned long long>(PAD(width, 16)) * PAD(height, 16) * 6ULL + 2048ULL;
  if (retval > static_cast<unsigned long long>(static_cast<unsigned long>(-1))) {
    std::snprintf(errStr, JMSG_LENGTH_MAX, "%s(): %s", FUNCTION_NAME, "Image is too large");
    return static_cast<unsigned long>(-1);
  }
  return static_cast<unsigned long>(retval);
}

DLLEXPORT int tj3YUVPlaneWidth(int componentID, int width, int subsamp) {
  static const char FUNCTION_NAME[] = "tj3YUVPlaneWidth";
  try {
    if (width < 1 || subsamp < 0 || subsamp >= TJ_NUMSAMP) throwg(FUNCTION_NAME, "Invalid argument");
    int nc = subsamp == TJSAMP_GRAY ? 1 : 3;
    if (componentID < 0 || componentID >= nc) throwg(FUNCTION_NAME, "Invalid argument");
    unsigned long long pw = static_cast<unsigned long long>(PAD(width, tjMCUWidth[subsamp] / 8));
    unsigned long long retval = componentID == 0 ? pw : pw * 8 / tjMCUWidth[subsamp];
    if (retval > static_cast<unsigned long long>(INT_MAX)) throwg(FUNCTION_NAME, "Width is too large");
    return static_cast<int>(retval);
  } catch (const TjThrow&) {
    return 0;
  }
}

DLLEXPORT int tj3YUVPlaneHeight(int componentID, int height, int subsamp) {
  static const char FUNCTION_NAME[] = "tj3YUVPlaneHeight";
  try {
    if (height < 1 || subsamp < 0 || subsamp >= TJ_NUMSAMP) throwg(FUNCTION_NAME, "Invalid argument");
    int nc = subsamp == TJSAMP_GRAY ? 1 : 3;
    if (componentID < 0 || componentID >= nc) throwg(FUNCTION_NAME, "Invalid argument");
    unsigned long long ph = static_cast<unsigned long long>(PAD(height, tjMCUHeight[subsamp] / 8));
    unsigned long long retval = componentID == 0 ? ph : ph * 8 / tjMCUHeight[subsamp];
    if (retval > static_cast<unsigned long long>(INT_MAX)) throwg(FUNCTION_NAME, "Height is too large");
    return static_cast<int>(retval);
  } catch (const TjThrow&) {
    return 0;
  }
}

DLLEXPORT int tjPlaneWidth(int componentID, int width, int subsamp) {
  int r = tj3YUVPlaneWidth(componentID, width, subsamp);
  return r == 0 ? -1 : r;
}
DLLEXPORT int tjPlaneHeight(int componentID, int height, int subsamp) {
  int r = tj3YUVPlaneHeight(componentID, height, subsamp);
  return r == 0 ? -1 : r;
}

DLLEXPORT size_t tj3YUVBufSize(int width, int align, int height, int subsamp) {
  static const char FUNCTION_NAME[] = "tj3YUVBufSize";
  try {
    if (align < 1 || !IS_POW2(align) || subsamp < 0 || subsamp >= TJ_NUMSAMP)
      throwg(FUNCTION_NAME, "Invalid argument");
    int nc = subsamp == TJSAMP_GRAY ? 1 : 3;
    unsigned long long retval = 0;
    for (int i = 0; i < nc; i++) {
      int pw = tj3YUVPlaneWidth(i, width, subsamp);
      unsigned long long stride = static_cast<unsigned long long>(PAD(pw, align));
      int ph = tj3YUVPlaneHeight(i, height, subsamp);
      if (pw == 0 || ph == 0) return 0;
      retval += stride * ph;
    }
    if (retval > static_cast<unsigned long long>(static_cast<unsigned long>(-1)))
      throwg(FUNCTION_NAME, "Image is too large");
    return static_cast<size_t>(retval);
  } catch (const TjThrow&) {
    return 0;
  }
}

DLLEXPORT unsigned long tjBufSizeYUV2(int width, int align, int height, int subsamp) {
  size_t r = tj3YUVBufSize(width, align, height, subsamp);
  return r == 0 ? static_cast<unsigned long>(-1) : static_cast<unsigned long>(r);
}
DLLEXPORT unsigned long tjBufSizeYUV(int width, int height, int subsamp) {
  return tjBufSizeYUV2(width, 4, height, subsamp);
}
DLLEXPORT unsigned long TJBUFSIZEYUV(int width, int height, int subsamp) {
  return tjBufSizeYUV(width, height, subsamp);
}

DLLEXPORT size_t tj3YUVPlaneSize(int componentID, int width, int stride, int height, int subsamp) {
  static const char FUNCTION_NAME[] = "tj3YUVPlaneSize";
  try {
    if (width < 1 || height < 1 || subsamp < 0 || subsamp >= TJ_NUMSAMP)
      throwg(FUNCTION_NAME, "Invalid argument");
    int pw = tj3YUVPlaneWidth(componentID, width, subsamp);
    int ph = tj3YUVPlaneHeight(componentID, height, subsamp);
    if (pw == 0 || ph == 0) return 0;
    if (stride == 0) stride = pw;
    else stride = std::abs(stride);
    unsigned long long retval = static_cast<unsigned long long>(stride) * (ph - 1) + pw;
    if (retval > static_cast<unsigned long long>(static_cast<unsigned long>(-1)))
      throwg(FUNCTION_NAME, "Image is too large");
    return static_cast<size_t>(retval);
  } catch (const TjThrow&) {
    return 0;
  }
}

DLLEXPORT unsigned long tjPlaneSizeYUV(int componentID, int width, int stride, int height,
                                       int subsamp) {
  size_t r = tj3YUVPlaneSize(componentID, width, stride, height, subsamp);
  return r == 0 ? static_cast<unsigned long>(-1) : static_cast<unsigned long>(r);
}

DLLEXPORT int tj3SetICCProfile(tjhandle handle, unsigned char* iccBuf, size_t iccSize) {
  static const char FUNCTION_NAME[] = "tj3SetICCProfile";
  GET_TJINSTANCE(handle, -1)
  return guarded<int>(self, -1, [&]() -> int {
    if ((self->init & COMPRESS) == 0) THROW("Instance has not been initialized for compression");
    self->iccBuf.clear();
    if (iccBuf && iccSize) self->iccBuf.assign(iccBuf, iccBuf + iccSize);
    return 0;
  }, [] {});
}

DLLEXPORT int tj3GetICCProfile(tjhandle handle, unsigned char** iccBuf, size_t* iccSize) {
  static const char FUNCTION_NAME[] = "tj3GetICCProfile";
  GET_TJINSTANCE(handle, -1)
  return guarded<int>(self, -1, [&]() -> int {
    if (iccSize == nullptr) THROW("Invalid argument");
    const std::vector<unsigned char>* src;
    if (self->init & DECOMPRESS) {
      if (self->decompICCBuf.empty()) {
        if (iccBuf) *iccBuf = nullptr;
        *iccSize = 0;
        self->warning = true;
        THROW("No ICC profile data has been extracted");
      }
      src = &self->decompICCBuf;
    } else {
      src = &self->iccBuf;
    }
    *iccSize = src->size();
    if (iccBuf == nullptr) return 0;
    if (*iccSize) {
      *iccBuf = static_cast<unsigned char*>(std::malloc(*iccSize));
      if (!*iccBuf) THROW("Memory allocation failure");
      std::memcpy(*iccBuf, src->data(), *iccSize);
    } else {
      *iccBuf = nullptr;
    }
    return 0;
  }, [] {});
}

DLLEXPORT int tj3Compress8(tjhandle handle, const unsigned char* srcBuf, int width, int pitch,
                           int height, int pixelFormat, unsigned char** jpegBuf, size_t* jpegSize) {
  return compress_impl<std::uint8_t>(handle, srcBuf, width, pitch, height, pixelFormat, jpegBuf,
                                     jpegSize, "tj3Compress8");
}
DLLEXPORT int tj3Compress12(tjhandle handle, const short* srcBuf, int width, int pitch,
                            int height, int pixelFormat, unsigned char** jpegBuf, size_t* jpegSize) {
  return compress_impl<std::int16_t>(handle, srcBuf, width, pitch, height, pixelFormat, jpegBuf,
                                     jpegSize, "tj3Compress12");
}
DLLEXPORT int tj3Compress16(tjhandle handle, const unsigned short* srcBuf, int width, int pitch,
                            int height, int pixelFormat, unsigned char** jpegBuf, size_t* jpegSize) {
  return compress_impl<std::uint16_t>(handle, srcBuf, width, pitch, height, pixelFormat, jpegBuf,
                                      jpegSize, "tj3Compress16");
}

DLLEXPORT int tj3CompressFromYUVPlanes8(tjhandle handle, const unsigned char* const* srcPlanes,
                                        int width, const int* strides, int height,
                                        unsigned char** jpegBuf, size_t* jpegSize) {
  static const char FUNCTION_NAME[] = "tj3CompressFromYUVPlanes8";
  GET_TJINSTANCE(handle, -1)
  int r = guarded<int>(self, -1, [&]() -> int {
    if ((self->init & COMPRESS) == 0) THROW("Instance has not been initialized for compression");
    if (!srcPlanes || !srcPlanes[0] || width <= 0 || height <= 0 || jpegBuf == nullptr ||
        jpegSize == nullptr)
      THROW("Invalid argument");
    if (self->subsamp != TJSAMP_GRAY && (!srcPlanes[1] || !srcPlanes[2])) THROW("Invalid argument");
    if (self->quality == -1) THROW("TJPARAM_QUALITY must be specified");
    if (self->subsamp == TJSAMP_UNKNOWN) THROW("TJPARAM_SUBSAMP must be specified");
    vd::CState& cs = self->cs();
    cs.image_width = static_cast<unsigned>(width);
    cs.image_height = static_cast<unsigned>(height);
    cs.data_precision = 8;
    bool alloc = !self->noRealloc;
    check_dest(jpegBuf, jpegSize, alloc);
    setCompDefaults(self, TJPF_RGB, true);
    cs.raw_data_in = true;
    self->c().start_compress(true);
    if (!self->iccBuf.empty()) self->enc.write_icc_profile(self->iccBuf);
    const int nc = cs.num_components;
    int pw[vj::MAX_COMPONENTS], ph[vj::MAX_COMPONENTS], iw[vj::MAX_COMPONENTS],
        th[vj::MAX_COMPONENTS];
    bool usetmpbuf = false;
    std::vector<std::vector<std::uint8_t*>> inbuf(nc), tmpbuf(nc);
    std::vector<std::vector<std::uint8_t>> tmpstore(nc);
    for (int i = 0; i < nc; i++) {
      const vd::ComponentInfo& comp = cs.comp_info[i];
      iw[i] = static_cast<int>(comp.width_in_blocks) * vj::DCTSIZE;
      int ih = static_cast<int>(comp.height_in_blocks) * vj::DCTSIZE;
      pw[i] = static_cast<int>(PAD(cs.image_width, cs.max_h_samp_factor) * comp.h_samp_factor /
                               cs.max_h_samp_factor);
      if (strides && strides[i] != 0 && strides[i] < pw[i]) THROW("Invalid argument");
      ph[i] = static_cast<int>(PAD(cs.image_height, cs.max_v_samp_factor) * comp.v_samp_factor /
                               cs.max_v_samp_factor);
      if (iw[i] != pw[i] || ih != ph[i]) usetmpbuf = true;
      th[i] = comp.v_samp_factor * vj::DCTSIZE;
      inbuf[i].resize(static_cast<std::size_t>(ph[i]));
      const unsigned char* ptr = srcPlanes[i];
      for (int row = 0; row < ph[i]; row++) {
        inbuf[i][row] = const_cast<std::uint8_t*>(ptr);
        ptr += (strides && strides[i] != 0) ? strides[i] : pw[i];
      }
    }
    if (usetmpbuf) {
      for (int i = 0; i < nc; i++) {
        tmpstore[i].assign(static_cast<std::size_t>(iw[i]) * th[i], 0);
        tmpbuf[i].resize(static_cast<std::size_t>(th[i]));
        for (int row = 0; row < th[i]; row++) tmpbuf[i][row] = tmpstore[i].data() + static_cast<std::size_t>(iw[i]) * row;
      }
    }
    for (int row = 0; row < static_cast<int>(cs.image_height);
         row += cs.max_v_samp_factor * vj::DCTSIZE) {
      std::uint8_t** yuvptr[vj::MAX_COMPONENTS];
      for (int i = 0; i < nc; i++) {
        const vd::ComponentInfo& comp = cs.comp_info[i];
        int crow = row * comp.v_samp_factor / cs.max_v_samp_factor;
        if (usetmpbuf) {
          int j;
          for (j = 0; j < std::min(th[i], ph[i] - crow); j++) {
            std::memcpy(tmpbuf[i][j], inbuf[i][crow + j], static_cast<std::size_t>(pw[i]));
            for (int k = pw[i]; k < iw[i]; k++) tmpbuf[i][j][k] = tmpbuf[i][j][pw[i] - 1];
          }
          for (j = ph[i] - crow; j < th[i]; j++)
            std::memcpy(tmpbuf[i][j], tmpbuf[i][ph[i] - crow - 1], static_cast<std::size_t>(iw[i]));
          yuvptr[i] = tmpbuf[i].data();
        } else {
          yuvptr[i] = &inbuf[i][crow];
        }
      }
      self->c().write_raw_data<std::uint8_t>(yuvptr, static_cast<unsigned>(cs.max_v_samp_factor * vj::DCTSIZE));
    }
    self->c().finish_compress();
    store_output(self, self->c().take_output(), jpegBuf, jpegSize, alloc);
    return 0;
  }, [&] { abort_compress(self); });
  return (r == -1 || self->warning) ? -1 : 0;
}

DLLEXPORT int tj3CompressFromYUV8(tjhandle handle, const unsigned char* srcBuf, int width,
                                  int align, int height, unsigned char** jpegBuf,
                                  size_t* jpegSize) {
  static const char FUNCTION_NAME[] = "tj3CompressFromYUV8";
  GET_TJINSTANCE(handle, -1)
  const unsigned char* srcPlanes[3];
  int strides[3];
  int r = guarded<int>(self, -1, [&]() -> int {
    if (srcBuf == nullptr || width <= 0 || align < 1 || !IS_POW2(align) || height <= 0)
      THROW("Invalid argument");
    if (self->subsamp == TJSAMP_UNKNOWN) THROW("TJPARAM_SUBSAMP must be specified");
    int pw0 = tj3YUVPlaneWidth(0, width, self->subsamp);
    int ph0 = tj3YUVPlaneHeight(0, height, self->subsamp);
    srcPlanes[0] = srcBuf;
    strides[0] = static_cast<int>(PAD(pw0, align));
    if (self->subsamp == TJSAMP_GRAY) {
      strides[1] = strides[2] = 0;
      srcPlanes[1] = srcPlanes[2] = nullptr;
    } else {
      int pw1 = tjPlaneWidth(1, width, self->subsamp);
      int ph1 = tjPlaneHeight(1, height, self->subsamp);
      strides[1] = strides[2] = static_cast<int>(PAD(pw1, align));
      if (static_cast<unsigned long long>(strides[0]) * ph0 > INT_MAX ||
          static_cast<unsigned long long>(strides[1]) * ph1 > INT_MAX)
        THROW("Image or row alignment is too large");
      srcPlanes[1] = srcPlanes[0] + strides[0] * ph0;
      srcPlanes[2] = srcPlanes[1] + strides[1] * ph1;
    }
    return 1;
  }, [] {});
  if (r != 1) return -1;
  return tj3CompressFromYUVPlanes8(handle, srcPlanes, width, strides, height, jpegBuf, jpegSize);
}

DLLEXPORT int tj3EncodeYUVPlanes8(tjhandle handle, const unsigned char* srcBuf, int width,
                                  int pitch, int height, int pixelFormat,
                                  unsigned char** dstPlanes, int* strides) {
  static const char FUNCTION_NAME[] = "tj3EncodeYUVPlanes8";
  GET_TJINSTANCE(handle, -1)
  int r = guarded<int>(self, -1, [&]() -> int {
    if ((self->init & COMPRESS) == 0) THROW("Instance has not been initialized for compression");
    if (srcBuf == nullptr || width <= 0 || pitch < 0 || height <= 0 || pixelFormat < 0 ||
        pixelFormat >= TJ_NUMPF || !dstPlanes || !dstPlanes[0])
      THROW("Invalid argument");
    if (self->subsamp != TJSAMP_GRAY && (!dstPlanes[1] || !dstPlanes[2])) THROW("Invalid argument");
    if (self->subsamp == TJSAMP_UNKNOWN) THROW("TJPARAM_SUBSAMP must be specified");
    if (pixelFormat == TJPF_CMYK) THROW("Cannot generate YUV images from packed-pixel CMYK images");
    if (pitch == 0) pitch = width * tjPixelSize[pixelFormat];
    else if (pitch < width * tjPixelSize[pixelFormat]) THROW("Invalid argument");
    vd::CState& cs = self->cs();
    cs.image_width = static_cast<unsigned>(width);
    cs.image_height = static_cast<unsigned>(height);
    cs.data_precision = 8;
    setCompDefaults(self, pixelFormat, true);
    if (self->c().state != vd::Compressor::State::Start) THROW("libjpeg API is in the wrong state");
    cs.init_master_control(false);
    vd::ColorConverter<std::uint8_t> cconvert(cs);
    vd::Downsampler<std::uint8_t> downsample(cs);
    cconvert.start_pass();
    const int nc = cs.num_components;
    int pw0 = static_cast<int>(PAD(width, cs.max_h_samp_factor));
    int ph0 = static_cast<int>(PAD(height, cs.max_v_samp_factor));
    std::vector<std::uint8_t*> row_pointer(static_cast<std::size_t>(ph0));
    for (int i = 0; i < height; i++)
      row_pointer[i] = const_cast<std::uint8_t*>(
          self->bottomUp ? &srcBuf[(height - i - 1) * static_cast<std::size_t>(pitch)]
                         : &srcBuf[i * static_cast<std::size_t>(pitch)]);
    for (int i = height; i < ph0; i++) row_pointer[i] = row_pointer[height - 1];
    std::vector<std::vector<std::uint8_t>> s1(nc), s2(nc);
    std::vector<std::vector<std::uint8_t*>> tmpbuf(nc), tmpbuf2(nc), outbuf(nc);
    int pw[vj::MAX_COMPONENTS], ph[vj::MAX_COMPONENTS];
    for (int i = 0; i < nc; i++) {
      const vd::ComponentInfo& comp = cs.comp_info[i];
      std::size_t w1 = static_cast<std::size_t>(PAD(
          (comp.width_in_blocks * cs.max_h_samp_factor * vj::DCTSIZE) / comp.h_samp_factor, 32));
      s1[i].assign(w1 * cs.max_v_samp_factor + 32, 0);
      tmpbuf[i].resize(static_cast<std::size_t>(cs.max_v_samp_factor));
      for (int row = 0; row < cs.max_v_samp_factor; row++) tmpbuf[i][row] = s1[i].data() + w1 * row;
      std::size_t w2 = static_cast<std::size_t>(PAD(comp.width_in_blocks * vj::DCTSIZE, 32));
      s2[i].assign(w2 * comp.v_samp_factor + 32, 0);
      tmpbuf2[i].resize(static_cast<std::size_t>(comp.v_samp_factor));
      for (int row = 0; row < comp.v_samp_factor; row++) tmpbuf2[i][row] = s2[i].data() + w2 * row;
      pw[i] = pw0 * comp.h_samp_factor / cs.max_h_samp_factor;
      if (strides && strides[i] != 0 && strides[i] < pw[i]) THROW("Invalid argument");
      ph[i] = ph0 * comp.v_samp_factor / cs.max_v_samp_factor;
      outbuf[i].resize(static_cast<std::size_t>(ph[i]));
      unsigned char* ptr = dstPlanes[i];
      for (int row = 0; row < ph[i]; row++) {
        outbuf[i][row] = ptr;
        ptr += (strides && strides[i] != 0) ? strides[i] : pw[i];
      }
    }
    std::vector<std::uint8_t**> tb(nc), tb2(nc);
    for (int i = 0; i < nc; i++) {
      tb[i] = tmpbuf[i].data();
      tb2[i] = tmpbuf2[i].data();
    }
    for (int row = 0; row < ph0; row += cs.max_v_samp_factor) {
      cconvert.color_convert(&row_pointer[row], tb.data(), 0, cs.max_v_samp_factor);
      downsample.downsample(tb.data(), 0, tb2.data(), 0);
      for (int i = 0; i < nc; i++) {
        const vd::ComponentInfo& comp = cs.comp_info[i];
        vd::copy_sample_rows(tb2[i], 0, outbuf[i].data(),
                             row * comp.v_samp_factor / cs.max_v_samp_factor, comp.v_samp_factor,
                             static_cast<std::size_t>(pw[i]));
      }
    }
    return 0;
  }, [&] { abort_compress(self); });
  return (r == -1 || self->warning) ? -1 : 0;
}

DLLEXPORT int tj3EncodeYUV8(tjhandle handle, const unsigned char* srcBuf, int width, int pitch,
                            int height, int pixelFormat, unsigned char* dstBuf, int align) {
  static const char FUNCTION_NAME[] = "tj3EncodeYUV8";
  GET_TJINSTANCE(handle, -1)
  unsigned char* dstPlanes[3];
  int strides[3];
  int r = guarded<int>(self, -1, [&]() -> int {
    if (width <= 0 || height <= 0 || dstBuf == nullptr || align < 1 || !IS_POW2(align))
      THROW("Invalid argument");
    if (self->subsamp == TJSAMP_UNKNOWN) THROW("TJPARAM_SUBSAMP must be specified");
    int pw0 = tj3YUVPlaneWidth(0, width, self->subsamp);
    int ph0 = tj3YUVPlaneHeight(0, height, self->subsamp);
    dstPlanes[0] = dstBuf;
    strides[0] = static_cast<int>(PAD(pw0, align));
    if (self->subsamp == TJSAMP_GRAY) {
      strides[1] = strides[2] = 0;
      dstPlanes[1] = dstPlanes[2] = nullptr;
    } else {
      int pw1 = tj3YUVPlaneWidth(1, width, self->subsamp);
      int ph1 = tj3YUVPlaneHeight(1, height, self->subsamp);
      strides[1] = strides[2] = static_cast<int>(PAD(pw1, align));
      if (static_cast<unsigned long long>(strides[0]) * ph0 > INT_MAX ||
          static_cast<unsigned long long>(strides[1]) * ph1 > INT_MAX)
        THROW("Image or row alignment is too large");
      dstPlanes[1] = dstPlanes[0] + strides[0] * ph0;
      dstPlanes[2] = dstPlanes[1] + strides[1] * ph1;
    }
    return 1;
  }, [] {});
  if (r != 1) return -1;
  return tj3EncodeYUVPlanes8(handle, srcBuf, width, pitch, height, pixelFormat, dstPlanes, strides);
}

DLLEXPORT int tj3DecompressHeader(tjhandle handle, const unsigned char* jpegBuf, size_t jpegSize) {
  static const char FUNCTION_NAME[] = "tj3DecompressHeader";
  GET_TJINSTANCE(handle, -1)
  int r = guarded<int>(self, -1, [&]() -> int {
    if ((self->init & DECOMPRESS) == 0)
      THROW("Instance has not been initialized for decompression");
    if (jpegBuf == nullptr || jpegSize <= 0) THROW("Invalid argument");
    vd::Decompressor& d = self->d();
    if (d.state != vd::Decompressor::State::Start) d.abort();
    d.set_source(jpegBuf, jpegSize);
    if (self->saveMarkers == 2 || self->saveMarkers == 4) self->dec.save_markers(vj::M_APP2, 0xFFFF);
    d.st.marker_list.clear();
    if (!d.read_header(false)) return 0;
    setDecompParameters(self);
    if (self->saveMarkers == 2 || self->saveMarkers == 4) {
      std::vector<std::uint8_t> icc = self->dec.icc_profile();
      if (!icc.empty()) self->decompICCBuf = icc;
    }
    d.abort();
    if (self->colorspace == TJCS_DEFAULT) THROW("Could not determine colorspace of JPEG image");
    if (self->jpegWidth < 1 || self->jpegHeight < 1) THROW("Invalid data returned in header");
    return 0;
  }, [] {});
  return (r == -1 || self->warning) ? -1 : 0;
}

DLLEXPORT tjscalingfactor* tj3GetScalingFactors(int* numScalingFactors) {
  if (numScalingFactors == nullptr) {
    std::snprintf(errStr, JMSG_LENGTH_MAX, "tj3GetScalingFactors(): Invalid argument");
    return nullptr;
  }
  *numScalingFactors = NUMSF;
  return const_cast<tjscalingfactor*>(sf);
}

DLLEXPORT int tj3SetScalingFactor(tjhandle handle, tjscalingfactor scalingFactor) {
  static const char FUNCTION_NAME[] = "tj3SetScalingFactor";
  GET_TJINSTANCE(handle, -1)
  return guarded<int>(self, -1, [&]() -> int {
    if ((self->init & DECOMPRESS) == 0)
      THROW("Instance has not been initialized for decompression");
    int i;
    for (i = 0; i < NUMSF; i++)
      if (scalingFactor.num == sf[i].num && scalingFactor.denom == sf[i].denom) break;
    if (i >= NUMSF) THROW("Unsupported scaling factor");
    self->scalingFactor = scalingFactor;
    return 0;
  }, [] {});
}

DLLEXPORT int tj3SetCroppingRegion(tjhandle handle, tjregion croppingRegion) {
  static const char FUNCTION_NAME[] = "tj3SetCroppingRegion";
  GET_TJINSTANCE(handle, -1)
  return guarded<int>(self, -1, [&]() -> int {
    if ((self->init & DECOMPRESS) == 0)
      THROW("Instance has not been initialized for decompression");
    if (croppingRegion.x == 0 && croppingRegion.y == 0 && croppingRegion.w == 0 &&
        croppingRegion.h == 0) {
      self->croppingRegion = croppingRegion;
      return 0;
    }
    if (croppingRegion.x < 0 || croppingRegion.y < 0 || croppingRegion.w < 0 ||
        croppingRegion.h < 0)
      THROW("Invalid cropping region");
    if (self->jpegWidth < 0 || self->jpegHeight < 0) THROW("JPEG header has not yet been read");
    if ((self->precision != 8 && self->precision != 12) || self->lossless)
      THROW("Cannot partially decompress lossless JPEG images");
    if (self->subsamp == TJSAMP_UNKNOWN) THROW("Could not determine subsampling level of JPEG image");
    int scaledWidth = TJSCALED(self->jpegWidth, self->scalingFactor);
    int scaledHeight = TJSCALED(self->jpegHeight, self->scalingFactor);
    int mcuw = TJSCALED(tjMCUWidth[self->subsamp], self->scalingFactor);
    if (croppingRegion.x % mcuw != 0)
      self->fail(FUNCTION_NAME,
                 "The left boundary of the cropping region (%d) is not\n"
                 "divisible by the scaled iMCU width (%d)",
                 croppingRegion.x, mcuw);
    if (croppingRegion.w == 0) croppingRegion.w = scaledWidth - croppingRegion.x;
    if (croppingRegion.h == 0) croppingRegion.h = scaledHeight - croppingRegion.y;
    if (croppingRegion.w <= 0 || croppingRegion.h <= 0 ||
        croppingRegion.x + croppingRegion.w > scaledWidth ||
        croppingRegion.y + croppingRegion.h > scaledHeight)
      THROW("The cropping region exceeds the scaled image dimensions");
    self->croppingRegion = croppingRegion;
    return 0;
  }, [] {});
}

DLLEXPORT int tj3Decompress8(tjhandle handle, const unsigned char* jpegBuf, size_t jpegSize,
                             unsigned char* dstBuf, int pitch, int pixelFormat) {
  return decompress_impl<std::uint8_t>(handle, jpegBuf, jpegSize, dstBuf, pitch, pixelFormat,
                                       "tj3Decompress8");
}
DLLEXPORT int tj3Decompress12(tjhandle handle, const unsigned char* jpegBuf, size_t jpegSize,
                              short* dstBuf, int pitch, int pixelFormat) {
  return decompress_impl<std::int16_t>(handle, jpegBuf, jpegSize, dstBuf, pitch, pixelFormat,
                                       "tj3Decompress12");
}
DLLEXPORT int tj3Decompress16(tjhandle handle, const unsigned char* jpegBuf, size_t jpegSize,
                              unsigned short* dstBuf, int pitch, int pixelFormat) {
  return decompress_impl<std::uint16_t>(handle, jpegBuf, jpegSize, dstBuf, pitch, pixelFormat,
                                        "tj3Decompress16");
}

DLLEXPORT int tj3DecompressToYUVPlanes8(tjhandle handle, const unsigned char* jpegBuf,
                                        size_t jpegSize, unsigned char** dstPlanes, int* strides) {
  static const char FUNCTION_NAME[] = "tj3DecompressToYUVPlanes8";
  GET_TJINSTANCE(handle, -1)
  int r = guarded<int>(self, -1, [&]() -> int {
    if ((self->init & DECOMPRESS) == 0)
      THROW("Instance has not been initialized for decompression");
    if (jpegBuf == nullptr || jpegSize <= 0 || !dstPlanes || !dstPlanes[0])
      THROW("Invalid argument");
    setup_progress(self);
    vd::Decompressor& d = self->d();
    vd::DState& ds = d.st;
    if (dstate_before_header(self)) {
      d.set_source(jpegBuf, jpegSize);
      d.read_header(true);
    }
    setDecompParameters(self);
    if (self->maxPixels && static_cast<unsigned long long>(self->jpegWidth) * self->jpegHeight >
                               static_cast<unsigned long long>(self->maxPixels))
      THROW("Image is too large");
    if (self->subsamp == TJSAMP_UNKNOWN) THROW("Could not determine subsampling level of JPEG image");
    if (self->subsamp != TJSAMP_GRAY && (!dstPlanes[1] || !dstPlanes[2])) THROW("Invalid argument");
    if (ds.num_components > 3) THROW("JPEG image must have 3 or fewer components");
    ds.scale_num = static_cast<unsigned>(self->scalingFactor.num);
    ds.scale_denom = static_cast<unsigned>(self->scalingFactor.denom);
    d.calc_output_dimensions();
    const int dctsize = vj::DCTSIZE * self->scalingFactor.num / self->scalingFactor.denom;
    const int nc = ds.num_components;
    int pw[vj::MAX_COMPONENTS], ph[vj::MAX_COMPONENTS], iw[vj::MAX_COMPONENTS],
        th[vj::MAX_COMPONENTS];
    bool usetmpbuf = false;
    std::vector<std::vector<std::uint8_t*>> outbuf(nc), tmpbuf(nc);
    std::vector<std::vector<std::uint8_t>> tmpstore(nc);
    for (int i = 0; i < nc; i++) {
      const vd::ComponentInfo& comp = ds.comp_info[i];
      iw[i] = static_cast<int>(comp.width_in_blocks) * dctsize;
      int ih = static_cast<int>(comp.height_in_blocks) * dctsize;
      pw[i] = tj3YUVPlaneWidth(i, static_cast<int>(ds.output_width), self->subsamp);
      if (strides && strides[i] != 0 && strides[i] < pw[i]) THROW("Invalid argument");
      ph[i] = tj3YUVPlaneHeight(i, static_cast<int>(ds.output_height), self->subsamp);
      if (iw[i] != pw[i] || ih != ph[i]) usetmpbuf = true;
      th[i] = comp.v_samp_factor * dctsize;
      outbuf[i].resize(static_cast<std::size_t>(ph[i]));
      unsigned char* ptr = dstPlanes[i];
      for (int row = 0; row < ph[i]; row++) {
        outbuf[i][row] = ptr;
        ptr += (strides && strides[i] != 0) ? strides[i] : pw[i];
      }
    }
    if (usetmpbuf) {
      for (int i = 0; i < nc; i++) {
        tmpstore[i].assign(static_cast<std::size_t>(iw[i]) * th[i] + 64, 0);
        tmpbuf[i].resize(static_cast<std::size_t>(th[i]));
        for (int row = 0; row < th[i]; row++)
          tmpbuf[i][row] = tmpstore[i].data() + static_cast<std::size_t>(iw[i]) * row;
      }
    }
    ds.do_fancy_upsampling = !self->fastUpsample;
    ds.dct_method = self->fastDCT ? vj::DctMethod::IntFast : vj::DctMethod::IntSlow;
    ds.raw_data_out = true;
    d.start_decompress();
    auto& pipe = d.pipeline<std::uint8_t>();
    for (int row = 0; row < static_cast<int>(ds.output_height);
         row += ds.max_v_samp_factor * ds.min_DCT_scaled_size) {
      std::uint8_t** yuvptr[vj::MAX_COMPONENTS];
      int crow[vj::MAX_COMPONENTS];
      for (int i = 0; i < nc; i++) {
        vd::ComponentInfo& comp = ds.comp_info[i];
        if (self->subsamp == TJSAMP_420 || self->subsamp == TJSAMP_410 ||
            self->subsamp == TJSAMP_24) {
          comp.DCT_scaled_size = dctsize;
          comp.MCU_sample_width = tjMCUWidth[self->subsamp] * self->scalingFactor.num /
                                  self->scalingFactor.denom * comp.h_samp_factor /
                                  ds.max_h_samp_factor;
          pipe.idct->set_kernel(i, pipe.idct->kernel(0));
        }
        crow[i] = row * comp.v_samp_factor / ds.max_v_samp_factor;
        yuvptr[i] = usetmpbuf ? tmpbuf[i].data() : &outbuf[i][crow[i]];
      }
      d.read_raw_data<std::uint8_t>(yuvptr, static_cast<unsigned>(ds.max_v_samp_factor * ds.min_DCT_scaled_size));
      if (usetmpbuf) {
        for (int i = 0; i < nc; i++)
          for (int j = 0; j < std::min(th[i], ph[i] - crow[i]); j++)
            std::memcpy(outbuf[i][crow[i] + j], tmpbuf[i][j], static_cast<std::size_t>(pw[i]));
      }
    }
    d.finish_decompress();
    return 0;
  }, [&] { abort_decompress(self); });
  return (r == -1 || self->warning) ? -1 : 0;
}

DLLEXPORT int tj3DecompressToYUV8(tjhandle handle, const unsigned char* jpegBuf, size_t jpegSize,
                                  unsigned char* dstBuf, int align) {
  static const char FUNCTION_NAME[] = "tj3DecompressToYUV8";
  GET_TJINSTANCE(handle, -1)
  unsigned char* dstPlanes[3];
  int strides[3];
  int r = guarded<int>(self, -1, [&]() -> int {
    if (jpegBuf == nullptr || jpegSize <= 0 || dstBuf == nullptr || align < 1 || !IS_POW2(align))
      THROW("Invalid argument");
    vd::Decompressor& d = self->d();
    if (dstate_before_header(self)) {
      d.set_source(jpegBuf, jpegSize);
      d.read_header(true);
    }
    setDecompParameters(self);
    if (self->subsamp == TJSAMP_UNKNOWN) THROW("Could not determine subsampling level of JPEG image");
    int width = TJSCALED(static_cast<int>(d.st.image_width), self->scalingFactor);
    int height = TJSCALED(static_cast<int>(d.st.image_height), self->scalingFactor);
    int pw0 = tj3YUVPlaneWidth(0, width, self->subsamp);
    int ph0 = tj3YUVPlaneHeight(0, height, self->subsamp);
    dstPlanes[0] = dstBuf;
    strides[0] = static_cast<int>(PAD(pw0, align));
    if (self->subsamp == TJSAMP_GRAY) {
      strides[1] = strides[2] = 0;
      dstPlanes[1] = dstPlanes[2] = nullptr;
    } else {
      int pw1 = tj3YUVPlaneWidth(1, width, self->subsamp);
      int ph1 = tj3YUVPlaneHeight(1, height, self->subsamp);
      strides[1] = strides[2] = static_cast<int>(PAD(pw1, align));
      if (static_cast<unsigned long long>(strides[0]) * ph0 > INT_MAX ||
          static_cast<unsigned long long>(strides[1]) * ph1 > INT_MAX)
        THROW("Image or row alignment is too large");
      dstPlanes[1] = dstPlanes[0] + strides[0] * ph0;
      dstPlanes[2] = dstPlanes[1] + strides[1] * ph1;
    }
    return 1;
  }, [&] {});
  if (r != 1) {
    abort_decompress(self);
    return -1;
  }
  return tj3DecompressToYUVPlanes8(handle, jpegBuf, jpegSize, dstPlanes, strides);
}

DLLEXPORT int tj3DecodeYUVPlanes8(tjhandle handle, const unsigned char* const* srcPlanes,
                                  const int* strides, unsigned char* dstBuf, int width, int pitch,
                                  int height, int pixelFormat) {
  static const char FUNCTION_NAME[] = "tj3DecodeYUVPlanes8";
  GET_TJINSTANCE(handle, -1)
  int r = guarded<int>(self, -1, [&]() -> int {
    if ((self->init & DECOMPRESS) == 0)
      THROW("Instance has not been initialized for decompression");
    if (!srcPlanes || !srcPlanes[0] || dstBuf == nullptr || width <= 0 || pitch < 0 ||
        height <= 0 || pixelFormat < 0 || pixelFormat >= TJ_NUMPF)
      THROW("Invalid argument");
    if (self->subsamp != TJSAMP_GRAY && (!srcPlanes[1] || !srcPlanes[2])) THROW("Invalid argument");
    if (self->subsamp == TJSAMP_UNKNOWN) THROW("TJPARAM_SUBSAMP must be specified");
    if (pixelFormat == TJPF_CMYK) THROW("Cannot decode YUV images into packed-pixel CMYK images.");
    if (pitch == 0) pitch = width * tjPixelSize[pixelFormat];
    else if (pitch < width * tjPixelSize[pixelFormat]) THROW("Invalid argument");

    // Fabricate a decompressor state for a baseline YCbCr/grayscale image
    // (setDecodeDefaults() + jpeg_read_header() with a no-op marker reader).
    vd::Decompressor& d = self->d();
    if (d.state != vd::Decompressor::State::Start) d.abort();
    vd::DState& ds = d.st;
    ds.reset_input_controller();
    ds.image_width = static_cast<unsigned>(width);
    ds.image_height = static_cast<unsigned>(height);
    ds.progressive_mode = false;
    ds.has_multiple_scans = false;
    ds.lossless = false;
    ds.arith_code = false;
    ds.Ss = ds.Ah = ds.Al = 0;
    ds.Se = vj::DCTSIZE2 - 1;
    ds.scale_num = ds.scale_denom = 1;
    const bool gray = self->subsamp == TJSAMP_GRAY;
    ds.num_components = ds.comps_in_scan = gray ? 1 : 3;
    ds.jpeg_color_space = gray ? ColorSpace::Gray : ColorSpace::YCbCr;
    ds.comp_info.assign(static_cast<std::size_t>(ds.num_components), vd::ComponentInfo{});
    for (int i = 0; i < ds.num_components; i++) {
      vd::ComponentInfo& c = ds.comp_info[i];
      c.h_samp_factor = i == 0 ? tjMCUWidth[self->subsamp] / 8 : 1;
      c.v_samp_factor = i == 0 ? tjMCUHeight[self->subsamp] / 8 : 1;
      c.component_index = i;
      c.component_id = i + 1;
      c.quant_tbl_no = c.dc_tbl_no = c.ac_tbl_no = i == 0 ? 0 : 1;
      ds.cur_comp_info[i] = &c;
    }
    ds.data_precision = 8;
    ds.jpeg_data_precision = 8;
    for (int i = 0; i < 2; i++)
      if (!ds.quant_tbl_ptrs[i]) ds.quant_tbl_ptrs[i].emplace();
    ds.initial_setup();
    ds.inheaders = false;
    ds.default_decompress_parms();
    d.state = vd::Decompressor::State::Ready;
    ds.out_color_space = pf2cs[pixelFormat];
    ds.dct_method = self->fastDCT ? vj::DctMethod::IntFast : vj::DctMethod::IntSlow;
    ds.do_fancy_upsampling = false;
    ds.Se = vj::DCTSIZE2 - 1;
    d.master_selection();
    auto& pipe = d.pipeline<std::uint8_t>();
    pipe.upsample->start_pass();

    const int nc = ds.num_components;
    int pw0 = static_cast<int>(PAD(width, ds.max_h_samp_factor));
    int ph0 = static_cast<int>(PAD(height, ds.max_v_samp_factor));
    std::vector<std::uint8_t*> row_pointer(static_cast<std::size_t>(ph0));
    for (int i = 0; i < height; i++)
      row_pointer[i] = self->bottomUp ? &dstBuf[(height - i - 1) * static_cast<std::size_t>(pitch)]
                                      : &dstBuf[i * static_cast<std::size_t>(pitch)];
    for (int i = height; i < ph0; i++) row_pointer[i] = row_pointer[height - 1];
    std::vector<std::vector<std::uint8_t>> store(nc);
    std::vector<std::vector<std::uint8_t*>> tmpbuf(nc), inbuf(nc);
    std::vector<std::uint8_t**> tb(nc);
    int pw[vj::MAX_COMPONENTS], ph[vj::MAX_COMPONENTS];
    for (int i = 0; i < nc; i++) {
      const vd::ComponentInfo& comp = ds.comp_info[i];
      std::size_t w = static_cast<std::size_t>(PAD(comp.width_in_blocks * vj::DCTSIZE, 32));
      store[i].assign(w * comp.v_samp_factor + 32, 0);
      tmpbuf[i].resize(static_cast<std::size_t>(comp.v_samp_factor));
      for (int row = 0; row < comp.v_samp_factor; row++) tmpbuf[i][row] = store[i].data() + w * row;
      tb[i] = tmpbuf[i].data();
      pw[i] = pw0 * comp.h_samp_factor / ds.max_h_samp_factor;
      if (strides && strides[i] != 0 && strides[i] < pw[i]) THROW("Invalid argument");
      ph[i] = ph0 * comp.v_samp_factor / ds.max_v_samp_factor;
      inbuf[i].resize(static_cast<std::size_t>(ph[i]));
      const unsigned char* ptr = srcPlanes[i];
      for (int row = 0; row < ph[i]; row++) {
        inbuf[i][row] = const_cast<std::uint8_t*>(ptr);
        ptr += (strides && strides[i] != 0) ? strides[i] : pw[i];
      }
    }
    for (int row = 0; row < ph0; row += ds.max_v_samp_factor) {
      unsigned inrow = 0, outrow = 0;
      for (int i = 0; i < nc; i++) {
        const vd::ComponentInfo& comp = ds.comp_info[i];
        vd::copy_sample_rows(inbuf[i].data(), row * comp.v_samp_factor / ds.max_v_samp_factor,
                             tmpbuf[i].data(), 0, comp.v_samp_factor, static_cast<std::size_t>(pw[i]));
      }
      pipe.upsample->upsample(tb.data(), &inrow, static_cast<unsigned>(ds.max_v_samp_factor),
                              &row_pointer[row], &outrow,
                              static_cast<unsigned>(ds.max_v_samp_factor));
    }
    d.abort();
    return 0;
  }, [&] { abort_decompress(self); });
  return (r == -1 || self->warning) ? -1 : 0;
}

DLLEXPORT int tj3DecodeYUV8(tjhandle handle, const unsigned char* srcBuf, int align,
                            unsigned char* dstBuf, int width, int pitch, int height,
                            int pixelFormat) {
  static const char FUNCTION_NAME[] = "tj3DecodeYUV8";
  GET_TJINSTANCE(handle, -1)
  const unsigned char* srcPlanes[3];
  int strides[3];
  int r = guarded<int>(self, -1, [&]() -> int {
    if (srcBuf == nullptr || align < 1 || !IS_POW2(align) || width <= 0 || height <= 0)
      THROW("Invalid argument");
    if (self->subsamp == TJSAMP_UNKNOWN) THROW("TJPARAM_SUBSAMP must be specified");
    int pw0 = tj3YUVPlaneWidth(0, width, self->subsamp);
    int ph0 = tj3YUVPlaneHeight(0, height, self->subsamp);
    srcPlanes[0] = srcBuf;
    strides[0] = static_cast<int>(PAD(pw0, align));
    if (self->subsamp == TJSAMP_GRAY) {
      strides[1] = strides[2] = 0;
      srcPlanes[1] = srcPlanes[2] = nullptr;
    } else {
      int pw1 = tj3YUVPlaneWidth(1, width, self->subsamp);
      int ph1 = tj3YUVPlaneHeight(1, height, self->subsamp);
      strides[1] = strides[2] = static_cast<int>(PAD(pw1, align));
      if (static_cast<unsigned long long>(strides[0]) * ph0 > INT_MAX ||
          static_cast<unsigned long long>(strides[1]) * ph1 > INT_MAX)
        THROW("Image or row alignment is too large");
      srcPlanes[1] = srcPlanes[0] + strides[0] * ph0;
      srcPlanes[2] = srcPlanes[1] + strides[1] * ph1;
    }
    return 1;
  }, [] {});
  if (r != 1) return -1;
  return tj3DecodeYUVPlanes8(handle, srcPlanes, strides, dstBuf, width, pitch, height, pixelFormat);
}

}  // extern "C"

namespace {

int getDstSubsamp(int srcSubsamp, const tjtransform* transform) {
  if (!transform) return srcSubsamp;
  int dstSubsamp = (transform->options & TJXOPT_GRAY) ? TJSAMP_GRAY : srcSubsamp;
  if (transform->op == TJXOP_TRANSPOSE || transform->op == TJXOP_TRANSVERSE ||
      transform->op == TJXOP_ROT90 || transform->op == TJXOP_ROT270) {
    if (dstSubsamp == TJSAMP_422) dstSubsamp = TJSAMP_440;
    else if (dstSubsamp == TJSAMP_440) dstSubsamp = TJSAMP_422;
    else if (dstSubsamp == TJSAMP_411) dstSubsamp = TJSAMP_441;
    else if (dstSubsamp == TJSAMP_441) dstSubsamp = TJSAMP_411;
    else if (dstSubsamp == TJSAMP_410) dstSubsamp = TJSAMP_24;
    else if (dstSubsamp == TJSAMP_24) dstSubsamp = TJSAMP_410;
  }
  return dstSubsamp;
}

void getTransformedSpecs(tjinstance* self, int* width, int* height, int* subsamp,
                         const tjtransform* transform, const char* FUNCTION_NAME) {
  if ((self->init & COMPRESS) == 0 || (self->init & DECOMPRESS) == 0)
    THROW("Instance has not been initialized for transformation");
  if (!width || !height || !subsamp || !transform || *width < 1 || *height < 1 ||
      *subsamp < TJSAMP_UNKNOWN || *subsamp >= self->numSamp)
    THROW("Invalid argument");
  int dstWidth = *width, dstHeight = *height;
  if (transform->op == TJXOP_TRANSPOSE || transform->op == TJXOP_TRANSVERSE ||
      transform->op == TJXOP_ROT90 || transform->op == TJXOP_ROT270) {
    dstWidth = *height;
    dstHeight = *width;
  }
  int dstSubsamp = getDstSubsamp(*subsamp, transform);
  if (transform->options & TJXOPT_CROP) {
    if (transform->r.x < 0 || transform->r.y < 0 || transform->r.w < 0 || transform->r.h < 0)
      THROW("Invalid cropping region");
    if (dstSubsamp == TJSAMP_UNKNOWN) THROW("Could not determine subsampling level of JPEG image");
    if ((transform->r.x % tjMCUWidth[dstSubsamp]) != 0 ||
        (transform->r.y % tjMCUHeight[dstSubsamp]) != 0)
      self->fail(FUNCTION_NAME,
                 "To crop this JPEG image, x must be a multiple of %d\n"
                 "and y must be a multiple of %d.",
                 tjMCUWidth[dstSubsamp], tjMCUHeight[dstSubsamp]);
    if (transform->r.x >= dstWidth || transform->r.y >= dstHeight)
      THROW("The cropping region exceeds the destination image dimensions");
    int cw = transform->r.w == 0 ? dstWidth - transform->r.x : transform->r.w;
    int ch = transform->r.h == 0 ? dstHeight - transform->r.y : transform->r.h;
    if (transform->r.x + cw > dstWidth || transform->r.y + ch > dstHeight)
      THROW("The cropping region exceeds the destination image dimensions");
    dstWidth = cw;
    dstHeight = ch;
  }
  *width = dstWidth;
  *height = dstHeight;
  *subsamp = dstSubsamp;
}

}  // namespace

extern "C" {

DLLEXPORT size_t tj3TransformBufSize(tjhandle handle, const tjtransform* transform) {
  static const char FUNCTION_NAME[] = "tj3TransformBufSize";
  GET_TJINSTANCE(handle, 0)
  return guarded<size_t>(self, 0, [&]() -> size_t {
    if ((self->init & COMPRESS) == 0 || (self->init & DECOMPRESS) == 0)
      THROW("Instance has not been initialized for transformation");
    if (transform == nullptr) THROW("Invalid argument");
    if (self->jpegWidth < 0 || self->jpegHeight < 0) THROW("JPEG header has not yet been read");
    int w = self->jpegWidth, h = self->jpegHeight, s = self->subsamp;
    getTransformedSpecs(self, &w, &h, &s, transform, FUNCTION_NAME);
    size_t retval = tj3JPEGBufSize(w, h, s);
    if ((self->saveMarkers == 2 || self->saveMarkers == 4) &&
        !(transform->options & TJXOPT_COPYNONE))
      retval += self->decompICCBuf.size();
    else
      retval += self->iccBuf.size();
    return retval;
  }, [] {});
}

DLLEXPORT int tj3Transform(tjhandle handle, const unsigned char* jpegBuf, size_t jpegSize, int n,
                           unsigned char** dstBufs, size_t* dstSizes, const tjtransform* t) {
  static const char FUNCTION_NAME[] = "tj3Transform";
  GET_TJINSTANCE(handle, -1)
  int r = guarded<int>(self, -1, [&]() -> int {
    if ((self->init & COMPRESS) == 0 || (self->init & DECOMPRESS) == 0)
      THROW("Instance has not been initialized for transformation");
    if (jpegBuf == nullptr || jpegSize <= 0 || n < 1 || dstBufs == nullptr ||
        dstSizes == nullptr || t == nullptr)
      THROW("Invalid argument");
    setup_progress(self);
    std::vector<vj::Transform> xinfo(static_cast<std::size_t>(n));
    vd::Decompressor& d = self->d();
    bool before = dstate_before_header(self);
    if (before) d.set_source(jpegBuf, jpegSize);
    bool saveMarkers = false;
    for (int i = 0; i < n; i++) {
      if (t[i].op < 0 || t[i].op >= TJ_NUMXOP) THROW("Invalid transform operation");
      vj::Transform& x = xinfo[i];
      x.transform = xformtypes[t[i].op];
      x.perfect = (t[i].options & TJXOPT_PERFECT) != 0;
      x.trim = (t[i].options & TJXOPT_TRIM) != 0;
      x.force_grayscale = (t[i].options & TJXOPT_GRAY) != 0;
      x.crop = (t[i].options & TJXOPT_CROP) != 0;
      x.slow_hflip = n != 1 && t[i].op == TJXOP_HFLIP;
      if (x.crop) {
        if (t[i].r.x < 0 || t[i].r.y < 0 || t[i].r.w < 0 || t[i].r.h < 0)
          THROW("Invalid cropping region");
        x.crop_xoffset = static_cast<unsigned>(t[i].r.x);
        x.crop_xoffset_set = vj::CropCode::Pos;
        x.crop_yoffset = static_cast<unsigned>(t[i].r.y);
        x.crop_yoffset_set = vj::CropCode::Pos;
        if (t[i].r.w != 0) {
          x.crop_width = static_cast<unsigned>(t[i].r.w);
          x.crop_width_set = vj::CropCode::Pos;
        }
        if (t[i].r.h != 0) {
          x.crop_height = static_cast<unsigned>(t[i].r.h);
          x.crop_height_set = vj::CropCode::Pos;
        }
      }
      if (!(t[i].options & TJXOPT_COPYNONE)) saveMarkers = true;
    }
    vj::copy_markers_setup(self->dec, saveMarkers ? static_cast<vj::CopyOption>(self->saveMarkers)
                                                  : vj::CopyOption::None);
    if (before) {
      d.st.marker_list.clear();
      d.read_header(true);
    }
    vd::DState& ds = d.st;
    if (self->maxPixels && static_cast<unsigned long long>(ds.image_width) * ds.image_height >
                               static_cast<unsigned long long>(self->maxPixels))
      THROW("Image is too large");
    int srcSubsamp = getSubsamp(self);
    for (int i = 0; i < n; i++) {
      if (!vj::request_workspace(ds, xinfo[i])) THROW("Transform is not perfect");
      if (xinfo[i].crop) {
        int dstSubsamp = getDstSubsamp(srcSubsamp, &t[i]);
        if (dstSubsamp == TJSAMP_UNKNOWN)
          THROW("Could not determine subsampling level of destination image");
        if ((t[i].r.x % tjMCUWidth[dstSubsamp]) != 0 || (t[i].r.y % tjMCUHeight[dstSubsamp]) != 0)
          self->fail(FUNCTION_NAME,
                     "To crop this JPEG image, x must be a multiple of %d\n"
                     "and y must be a multiple of %d.",
                     tjMCUWidth[dstSubsamp], tjMCUHeight[dstSubsamp]);
      }
    }
    vd::BlockArray* srccoefs = d.read_coefficients();
    vd::Compressor& c = self->c();
    vd::CState& cs = c.cs;
    for (int i = 0; i < n; i++) {
      bool alloc = !self->noRealloc;
      bool output = !(t[i].options & TJXOPT_NOOUTPUT);
      if (output) check_dest(&dstBufs[i], &dstSizes[i], alloc);
      vj::copy_critical_parameters(ds, cs);
      vd::BlockArray* dstcoefs = vj::adjust_parameters(ds, cs, srccoefs, xinfo[i]);
      if (self->optimize || (t[i].options & TJXOPT_OPTIMIZE)) cs.optimize_coding = true;
      if (self->progressive || (t[i].options & TJXOPT_PROGRESSIVE)) cs.simple_progression();
      if (self->arithmetic || (t[i].options & TJXOPT_ARITHMETIC)) {
        cs.arith_code = true;
        cs.optimize_coding = false;
      }
      cs.restart_interval = static_cast<unsigned>(self->restartIntervalBlocks);
      cs.restart_in_rows = self->restartIntervalRows;
      if (output) {
        vj::CopyOption copyOption = (t[i].options & TJXOPT_COPYNONE)
                                        ? vj::CopyOption::None
                                        : static_cast<vj::CopyOption>(self->saveMarkers);
        c.write_coefficients(dstcoefs);
        vj::copy_markers_execute(self->dec, self->enc, copyOption);
        if (!self->iccBuf.empty() && copyOption != vj::CopyOption::All &&
            copyOption != vj::CopyOption::ICC)
          self->enc.write_icc_profile(self->iccBuf);
      } else {
        cs.init_master_control(true);
      }
      vj::execute_transform(ds, cs, srccoefs, xinfo[i]);
      if (t[i].customFilter) {
        for (int ci = 0; ci < cs.num_components; ci++) {
          const vd::ComponentInfo& comp = cs.comp_info[ci];
          tjregion arrayRegion = {0, 0, 0, 0}, planeRegion = {0, 0, 0, 0};
          arrayRegion.w = static_cast<int>(comp.width_in_blocks) * vj::DCTSIZE;
          arrayRegion.h = vj::DCTSIZE;
          planeRegion.w = static_cast<int>(comp.width_in_blocks) * vj::DCTSIZE;
          planeRegion.h = static_cast<int>(comp.height_in_blocks) * vj::DCTSIZE;
          for (unsigned by = 0; by < comp.height_in_blocks; by += comp.v_samp_factor) {
            for (int y = 0; y < comp.v_samp_factor; y++) {
              short* coeffs = reinterpret_cast<short*>(dstcoefs[ci].row(by + y)->data());
              if (t[i].customFilter(coeffs, arrayRegion, planeRegion, ci, i,
                                    const_cast<tjtransform*>(&t[i])) == -1)
                THROW("Error in custom filter");
              arrayRegion.y += vj::DCTSIZE;
            }
          }
        }
      }
      if (output) {
        c.finish_compress();
        store_output(self, c.take_output(), &dstBufs[i], &dstSizes[i], alloc);
      }
    }
    d.finish_decompress();
    return 0;
  }, [&] {
    abort_compress(self);
    abort_decompress(self);
  });
  return (r == -1 || self->warning) ? -1 : 0;
}

DLLEXPORT unsigned char* tj3LoadImage8(tjhandle handle, const char* filename, int* width,
                                       int align, int* height, int* pixelFormat) {
  return load_image_impl<std::uint8_t>(handle, filename, width, align, height, pixelFormat,
                                       "tj3LoadImage8");
}
DLLEXPORT short* tj3LoadImage12(tjhandle handle, const char* filename, int* width, int align,
                                int* height, int* pixelFormat) {
  return load_image_impl<std::int16_t>(handle, filename, width, align, height, pixelFormat,
                                       "tj3LoadImage12");
}
DLLEXPORT unsigned short* tj3LoadImage16(tjhandle handle, const char* filename, int* width,
                                         int align, int* height, int* pixelFormat) {
  return load_image_impl<std::uint16_t>(handle, filename, width, align, height, pixelFormat,
                                        "tj3LoadImage16");
}

DLLEXPORT int tj3SaveImage8(tjhandle handle, const char* filename, const unsigned char* buffer,
                            int width, int pitch, int height, int pixelFormat) {
  return save_image_impl<std::uint8_t>(handle, filename, buffer, width, pitch, height, pixelFormat,
                                       "tj3SaveImage8");
}
DLLEXPORT int tj3SaveImage12(tjhandle handle, const char* filename, const short* buffer, int width,
                             int pitch, int height, int pixelFormat) {
  return save_image_impl<std::int16_t>(handle, filename, buffer, width, pitch, height, pixelFormat,
                                       "tj3SaveImage12");
}
DLLEXPORT int tj3SaveImage16(tjhandle handle, const char* filename, const unsigned short* buffer,
                             int width, int pitch, int height, int pixelFormat) {
  return save_image_impl<std::uint16_t>(handle, filename, buffer, width, pitch, height,
                                        pixelFormat, "tj3SaveImage16");
}

// ---- TurboJPEG 1.x/2.x convenience wrappers --------------------------------
DLLEXPORT char* tjGetErrorStr2(tjhandle handle) { return tj3GetErrorStr(handle); }
DLLEXPORT char* tjGetErrorStr(void) { return errStr; }
DLLEXPORT int tjGetErrorCode(tjhandle handle) { return tj3GetErrorCode(handle); }
DLLEXPORT tjhandle tjInitCompress(void) { return tj3Init(TJINIT_COMPRESS); }
DLLEXPORT tjhandle tjInitDecompress(void) { return tj3Init(TJINIT_DECOMPRESS); }
DLLEXPORT tjhandle tjInitTransform(void) { return tj3Init(TJINIT_TRANSFORM); }
DLLEXPORT int tjDestroy(tjhandle handle) {
  if (!handle) {
    std::snprintf(errStr, JMSG_LENGTH_MAX, "tjDestroy(): Invalid handle");
    return -1;
  }
  std::snprintf(errStr, JMSG_LENGTH_MAX, "No error");
  tj3Destroy(handle);
  return std::strcmp(errStr, "No error") ? -1 : 0;
}
DLLEXPORT unsigned char* tjAlloc(int bytes) {
  return static_cast<unsigned char*>(tj3Alloc(static_cast<size_t>(bytes)));
}
DLLEXPORT void tjFree(unsigned char* buf) { tj3Free(buf); }
DLLEXPORT tjscalingfactor* tjGetScalingFactors(int* numScalingFactors) {
  return tj3GetScalingFactors(numScalingFactors);
}

}  // extern "C"
