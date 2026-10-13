// vio::jpeg - compression pipeline: color conversion, downsampling,
// preprocessing, main buffer, coefficient buffering, lossless differencing.
#pragma once

#include "fdct.hpp"
#include "huff_encode.hpp"
#include "../simd/dispatch.hpp"

namespace vio::jpeg::detail {

// ---------------------------------------------------------------------------
// Color converter
template <class T>
class ColorConverter {
 public:
  using ConvertFn = void (ColorConverter::*)(T**, T***, unsigned, int);

  explicit ColorConverter(CState& cs) : cs_(cs) {
    const ColorSpace in = cs.in_color_space, j = cs.jpeg_color_space;
    switch (in) {
      case ColorSpace::Gray:
        if (cs.input_components != 1) VIO_FAIL("Bogus input colorspace");
        break;
      case ColorSpace::YCbCr:
        if (cs.input_components != 3) VIO_FAIL("Bogus input colorspace");
        break;
      case ColorSpace::CMYK:
      case ColorSpace::YCCK:
        if (cs.input_components != 4) VIO_FAIL("Bogus input colorspace");
        break;
      default:
        if (is_rgb_family(in)) {
          if (cs.input_components != rgb_pixelsize[idx(in)]) VIO_FAIL("Bogus input colorspace");
        } else if (cs.input_components < 1) {
          VIO_FAIL("Bogus input colorspace");
        }
    }
    auto notimpl = [] { VIO_FAIL("Unsupported color conversion request"); };
    constexpr bool can_table = kBits<T> != 16;
    switch (j) {
      case ColorSpace::Gray:
        if (cs.lossless && in != j) notimpl();
        if (cs.num_components != 1) VIO_FAIL("Bogus JPEG colorspace");
        if (in == ColorSpace::Gray || in == ColorSpace::YCbCr) {
          convert_ = &ColorConverter::grayscale_convert;
        } else if (is_rgb_family(in)) {
          if (!can_table) notimpl();
          need_table_ = true;
          convert_ = select<RgbGray>(in);
        } else {
          notimpl();
        }
        break;
      case ColorSpace::RGB:
        if (cs.lossless && !is_rgb_family(in)) notimpl();
        if (cs.num_components != 3) VIO_FAIL("Bogus JPEG colorspace");
        if (rgb_red[idx(in)] == 0 && rgb_green[idx(in)] == 1 && rgb_blue[idx(in)] == 2 &&
            rgb_pixelsize[idx(in)] == 3)
          convert_ = &ColorConverter::null_convert;
        else if (is_rgb_family(in))
          convert_ = select<RgbRgb>(in);
        else
          notimpl();
        break;
      case ColorSpace::YCbCr:
        if (cs.lossless && in != j) notimpl();
        if (cs.num_components != 3) VIO_FAIL("Bogus JPEG colorspace");
        if (is_rgb_family(in)) {
          if (!can_table) notimpl();
          need_table_ = true;
          convert_ = select<RgbYcc>(in);
        } else if (in == ColorSpace::YCbCr) {
          convert_ = &ColorConverter::null_convert;
        } else {
          notimpl();
        }
        break;
      case ColorSpace::CMYK:
        if (cs.lossless && in != j) notimpl();
        if (cs.num_components != 4) VIO_FAIL("Bogus JPEG colorspace");
        if (in == ColorSpace::CMYK) convert_ = &ColorConverter::null_convert;
        else notimpl();
        break;
      case ColorSpace::YCCK:
        if (cs.lossless && in != j) notimpl();
        if (cs.num_components != 4) VIO_FAIL("Bogus JPEG colorspace");
        if (in == ColorSpace::CMYK) {
          if (!can_table) notimpl();
          need_table_ = true;
          convert_ = &ColorConverter::cmyk_ycck_convert;
        } else if (in == ColorSpace::YCCK) {
          convert_ = &ColorConverter::null_convert;
        } else {
          notimpl();
        }
        break;
      default:
        if (j != in || cs.num_components != cs.input_components) notimpl();
        convert_ = &ColorConverter::null_convert;
    }
    if constexpr (kBits<T> == 8) {
      if (is_rgb_family(in)) {
        if (j == ColorSpace::YCbCr) simd_ = simd::rgb_ycc_kernel(cs.simd, in);
        else if (j == ColorSpace::Gray) simd_ = simd::rgb_gray_kernel(cs.simd, in);
      }
    }
  }

  void start_pass() {
    if (need_table_ && tab_.empty()) build_table();
  }

  VIO_INLINE void color_convert(T** input_buf, T*** output_buf, unsigned output_row,
                                int num_rows) {
    if constexpr (kBits<T> == 8) {
      if (simd_) return simd_(cs_.image_width, input_buf, output_buf, output_row, num_rows);
    }
    (this->*convert_)(input_buf, output_buf, output_row, num_rows);
  }

  void set_convert(ConvertFn fn) { convert_ = fn; }

 private:
  using SimdFn = void (*)(unsigned, T**, T***, unsigned, int);
  SimdFn simd_ = nullptr;

 public:

 private:
  enum Kind { RgbYcc, RgbGray, RgbRgb };
  static constexpr int N = kMaxSample<T> + 1;
  static constexpr int R_Y_OFF = 0, G_Y_OFF = N, B_Y_OFF = 2 * N, R_CB_OFF = 3 * N,
                       G_CB_OFF = 4 * N, B_CB_OFF = 5 * N, R_CR_OFF = B_CB_OFF, G_CR_OFF = 6 * N,
                       B_CR_OFF = 7 * N;

  static VIO_INLINE int range_limit(int v) {
    if constexpr (kBits<T> == 12) return v & 0xFFF;
    else return v;
  }

  void build_table() {
    tab_.assign(8 * N, 0);
    const JLong CBCR_OFFSET = static_cast<JLong>(kCenterSample<T>) << 16;
    const JLong half = JLong(1) << 15;
    auto fix = [](double x) { return static_cast<JLong>(x * 65536.0 + 0.5); };
    for (JLong i = 0; i < N; i++) {
      tab_[i + R_Y_OFF] = fix(0.29900) * i;
      tab_[i + G_Y_OFF] = fix(0.58700) * i;
      tab_[i + B_Y_OFF] = fix(0.11400) * i + half;
      tab_[i + R_CB_OFF] = (-fix(0.16874)) * i;
      tab_[i + G_CB_OFF] = (-fix(0.33126)) * i;
      tab_[i + B_CB_OFF] = fix(0.50000) * i + CBCR_OFFSET + half - 1;
      tab_[i + G_CR_OFF] = (-fix(0.41869)) * i;
      tab_[i + B_CR_OFF] = (-fix(0.08131)) * i;
    }
  }

  template <Kind K>
  ConvertFn select(ColorSpace in) {
    switch (in) {
      case ColorSpace::ExtRGBX:
      case ColorSpace::ExtRGBA: return &ColorConverter::conv<K, 0, 1, 2, 4>;
      case ColorSpace::ExtBGR: return &ColorConverter::conv<K, 2, 1, 0, 3>;
      case ColorSpace::ExtBGRX:
      case ColorSpace::ExtBGRA: return &ColorConverter::conv<K, 2, 1, 0, 4>;
      case ColorSpace::ExtXBGR:
      case ColorSpace::ExtABGR: return &ColorConverter::conv<K, 3, 2, 1, 4>;
      case ColorSpace::ExtXRGB:
      case ColorSpace::ExtARGB: return &ColorConverter::conv<K, 1, 2, 3, 4>;
      default: return &ColorConverter::conv<K, 0, 1, 2, 3>;
    }
  }

  template <Kind K, int R, int G, int B, int PS>
  void conv(T** input_buf, T*** output_buf, unsigned output_row, int num_rows) {
    const JLong* ctab = tab_.data();
    unsigned num_cols = cs_.image_width;
    while (--num_rows >= 0) {
      const T* inptr = *input_buf++;
      T* outptr0 = output_buf[0][output_row];
      T* outptr1 = K == RgbGray ? nullptr : output_buf[1][output_row];
      T* outptr2 = K == RgbGray ? nullptr : output_buf[2][output_row];
      output_row++;
      for (unsigned col = 0; col < num_cols; col++) {
        if constexpr (K == RgbRgb) {
          outptr0[col] = inptr[R];
          outptr1[col] = inptr[G];
          outptr2[col] = inptr[B];
        } else {
          // The sums of the table entries (see build_table()), computed
          // directly where the loop vectorizes (32 bits hold them for up to
          // 12-bit samples).
          int r = range_limit(inptr[R]), g = range_limit(inptr[G]), b = range_limit(inptr[B]);
          if constexpr (kBits<T> <= 12 && kVectorizes) {
            constexpr int half = 1 << 15, cbcr = (kCenterSample<T> << 16) + half - 1;
            outptr0[col] = static_cast<T>((19595 * r + 38470 * g + 7471 * b + half) >> 16);
            if constexpr (K == RgbYcc) {
              outptr1[col] = static_cast<T>((-11059 * r - 21709 * g + 32768 * b + cbcr) >> 16);
              outptr2[col] = static_cast<T>((32768 * r - 27439 * g - 5329 * b + cbcr) >> 16);
            }
          } else {
            outptr0[col] =
                static_cast<T>((ctab[r + R_Y_OFF] + ctab[g + G_Y_OFF] + ctab[b + B_Y_OFF]) >> 16);
            if constexpr (K == RgbYcc) {
              outptr1[col] = static_cast<T>(
                  (ctab[r + R_CB_OFF] + ctab[g + G_CB_OFF] + ctab[b + B_CB_OFF]) >> 16);
              outptr2[col] = static_cast<T>(
                  (ctab[r + R_CR_OFF] + ctab[g + G_CR_OFF] + ctab[b + B_CR_OFF]) >> 16);
            }
          }
        }
        inptr += PS;
      }
    }
  }

  void cmyk_ycck_convert(T** input_buf, T*** output_buf, unsigned output_row, int num_rows) {
    const JLong* ctab = tab_.data();
    unsigned num_cols = cs_.image_width;
    while (--num_rows >= 0) {
      const T* inptr = *input_buf++;
      T* outptr0 = output_buf[0][output_row];
      T* outptr1 = output_buf[1][output_row];
      T* outptr2 = output_buf[2][output_row];
      T* outptr3 = output_buf[3][output_row];
      output_row++;
      for (unsigned col = 0; col < num_cols; col++) {
        int r = kMaxSample<T> - range_limit(inptr[0]);
        int g = kMaxSample<T> - range_limit(inptr[1]);
        int b = kMaxSample<T> - range_limit(inptr[2]);
        outptr3[col] = inptr[3];
        inptr += 4;
        outptr0[col] =
            static_cast<T>((ctab[r + R_Y_OFF] + ctab[g + G_Y_OFF] + ctab[b + B_Y_OFF]) >> 16);
        outptr1[col] =
            static_cast<T>((ctab[r + R_CB_OFF] + ctab[g + G_CB_OFF] + ctab[b + B_CB_OFF]) >> 16);
        outptr2[col] =
            static_cast<T>((ctab[r + R_CR_OFF] + ctab[g + G_CR_OFF] + ctab[b + B_CR_OFF]) >> 16);
      }
    }
  }

  void grayscale_convert(T** input_buf, T*** output_buf, unsigned output_row, int num_rows) {
    unsigned num_cols = cs_.image_width;
    int instride = cs_.input_components;
    while (--num_rows >= 0) {
      const T* inptr = *input_buf++;
      T* outptr = output_buf[0][output_row++];
      for (unsigned col = 0; col < num_cols; col++) {
        outptr[col] = inptr[0];
        inptr += instride;
      }
    }
  }

  void null_convert(T** input_buf, T*** output_buf, unsigned output_row, int num_rows) {
    int nc = cs_.num_components;
    unsigned num_cols = cs_.image_width;
    while (--num_rows >= 0) {
      for (int ci = 0; ci < nc; ci++) {
        const T* inptr = *input_buf;
        T* outptr = output_buf[ci][output_row];
        for (unsigned col = 0; col < num_cols; col++) {
          outptr[col] = inptr[ci];
          inptr += nc;
        }
      }
      input_buf++;
      output_row++;
    }
  }

  CState& cs_;
  ConvertFn convert_ = nullptr;
  bool need_table_ = false;
  std::vector<JLong> tab_;
};

// ---------------------------------------------------------------------------
// Downsampler
template <class T>
class Downsampler {
 public:
  using MethodFn = void (Downsampler::*)(const ComponentInfo&, T**, T**);

  explicit Downsampler(CState& cs) : cs_(cs) {
    if (cs.CCIR601_sampling) VIO_FAIL("CCIR601 sampling not implemented yet");
    for (int ci = 0; ci < cs.num_components; ci++) {
      const ComponentInfo& c = cs.comp_info[ci];
      if (c.h_samp_factor == cs.max_h_samp_factor && c.v_samp_factor == cs.max_v_samp_factor) {
        if (cs.smoothing_factor) {
          methods_[ci] = &Downsampler::fullsize_smooth_downsample;
          need_context_rows = true;
        } else {
          methods_[ci] = &Downsampler::fullsize_downsample;
        }
      } else if (c.h_samp_factor * 2 == cs.max_h_samp_factor &&
                 c.v_samp_factor == cs.max_v_samp_factor) {
        methods_[ci] = h2v1;
        h2v1_slots_[ci] = true;
        if constexpr (kBits<T> == 8) if (!cs.lossless) simd_[ci] = simd::h2v1_downsample_kernel(cs.simd);
      } else if (c.h_samp_factor * 2 == cs.max_h_samp_factor &&
                 c.v_samp_factor * 2 == cs.max_v_samp_factor) {
        if (cs.smoothing_factor) {
          methods_[ci] = &Downsampler::h2v2_smooth_downsample;
          need_context_rows = true;
        } else {
          methods_[ci] = h2v2;
          h2v2_slots_[ci] = true;
          if constexpr (kBits<T> == 8) if (!cs.lossless) simd_[ci] = simd::h2v2_downsample_kernel(cs.simd);
        }
      } else if ((cs.max_h_samp_factor % c.h_samp_factor) == 0 &&
                 (cs.max_v_samp_factor % c.v_samp_factor) == 0) {
        methods_[ci] = &Downsampler::int_downsample;
      } else {
        VIO_FAIL("Fractional sampling not implemented yet");
      }
    }
  }

  // Lets SIMD back ends replace the plain h2v1/h2v2 methods.
  void set_h2v1(MethodFn fn) {
    for (int ci = 0; ci < cs_.num_components; ci++)
      if (h2v1_slots_[ci]) methods_[ci] = fn;
  }
  void set_h2v2(MethodFn fn) {
    for (int ci = 0; ci < cs_.num_components; ci++)
      if (h2v2_slots_[ci]) methods_[ci] = fn;
  }

  void downsample(T*** input_buf, unsigned in_row_index, T*** output_buf,
                  unsigned out_row_group_index) {
    for (int ci = 0; ci < cs_.num_components; ci++) {
      const ComponentInfo& c = cs_.comp_info[ci];
      if constexpr (kBits<T> == 8) {
        if (simd_[ci]) {
          simd_[ci](cs_.image_width, cs_.max_v_samp_factor, static_cast<unsigned>(c.v_samp_factor),
                    c.width_in_blocks, input_buf[ci] + in_row_index,
                    output_buf[ci] + out_row_group_index * c.v_samp_factor);
          continue;
        }
      }
      (this->*methods_[ci])(c, input_buf[ci] + in_row_index,
                            output_buf[ci] + out_row_group_index * c.v_samp_factor);
    }
  }

  bool need_context_rows = false;

 private:
  // SIMD kernels process width_in_blocks * DCTSIZE output columns, so they
  // are not used for lossless images, whose blocks are single samples (the
  // results would be the same, but the buffers are not that wide).
  using SimdFn = void (*)(unsigned, int, unsigned, unsigned, T**, T**);
  SimdFn simd_[MAX_COMPONENTS]{};

 public:

  static void expand_right_edge(T** image_data, int num_rows, unsigned input_cols,
                                unsigned output_cols) {
    int numcols = static_cast<int>(output_cols - input_cols);
    if (numcols > 0) {
      for (int row = 0; row < num_rows; row++) {
        T* ptr = image_data[row] + input_cols;
        T pixval = ptr[-1];
        for (int count = numcols; count > 0; count--) *ptr++ = pixval;
      }
    }
  }

  void h2v1_downsample(const ComponentInfo& c, T** input_data, T** output_data) {
    unsigned output_cols = c.width_in_blocks * data_unit();
    expand_right_edge(input_data, cs_.max_v_samp_factor, cs_.image_width, output_cols * 2);
    for (int outrow = 0; outrow < c.v_samp_factor; outrow++) {
      T* outptr = output_data[outrow];
      const T* inptr = input_data[outrow];
      // Rounding bias 0, 1, 0, 1, ... (pairs of columns, so that the loop
      // vectorizes).
      unsigned outcol = 0;
      for (; outcol + 1 < output_cols; outcol += 2) {
        outptr[outcol] = static_cast<T>((inptr[2 * outcol] + inptr[2 * outcol + 1]) >> 1);
        outptr[outcol + 1] =
            static_cast<T>((inptr[2 * outcol + 2] + inptr[2 * outcol + 3] + 1) >> 1);
      }
      if (outcol < output_cols)
        outptr[outcol] = static_cast<T>((inptr[2 * outcol] + inptr[2 * outcol + 1]) >> 1);
    }
  }

  void h2v2_downsample(const ComponentInfo& c, T** input_data, T** output_data) {
    unsigned output_cols = c.width_in_blocks * data_unit();
    expand_right_edge(input_data, cs_.max_v_samp_factor, cs_.image_width, output_cols * 2);
    int inrow = 0;
    for (int outrow = 0; outrow < c.v_samp_factor; outrow++) {
      T* outptr = output_data[outrow];
      const T* inptr0 = input_data[inrow];
      const T* inptr1 = input_data[inrow + 1];
      // Rounding bias 1, 2, 1, 2, ...
      auto sum = [&](unsigned i) VIO_LAMBDA_INLINE {
        return inptr0[2 * i] + inptr0[2 * i + 1] + inptr1[2 * i] + inptr1[2 * i + 1];
      };
      unsigned outcol = 0;
      for (; outcol + 1 < output_cols; outcol += 2) {
        outptr[outcol] = static_cast<T>((sum(outcol) + 1) >> 2);
        outptr[outcol + 1] = static_cast<T>((sum(outcol + 1) + 2) >> 2);
      }
      if (outcol < output_cols) outptr[outcol] = static_cast<T>((sum(outcol) + 1) >> 2);
      inrow += 2;
    }
  }

 private:
  int data_unit() const { return cs_.lossless ? 1 : DCTSIZE; }

  void int_downsample(const ComponentInfo& c, T** input_data, T** output_data) {
    unsigned output_cols = c.width_in_blocks * data_unit();
    int h_expand = cs_.max_h_samp_factor / c.h_samp_factor;
    int v_expand = cs_.max_v_samp_factor / c.v_samp_factor;
    int numpix = h_expand * v_expand;
    int numpix2 = numpix / 2;
    expand_right_edge(input_data, cs_.max_v_samp_factor, cs_.image_width, output_cols * h_expand);
    int inrow = 0;
    for (int outrow = 0; outrow < c.v_samp_factor; outrow++) {
      T* outptr = output_data[outrow];
      for (unsigned outcol = 0, outcol_h = 0; outcol < output_cols;
           outcol++, outcol_h += h_expand) {
        JLong outvalue = 0;
        for (int v = 0; v < v_expand; v++) {
          const T* inptr = input_data[inrow + v] + outcol_h;
          for (int h = 0; h < h_expand; h++) outvalue += static_cast<JLong>(*inptr++);
        }
        *outptr++ = static_cast<T>((outvalue + numpix2) / numpix);
      }
      inrow += v_expand;
    }
  }

  void fullsize_downsample(const ComponentInfo& c, T** input_data, T** output_data) {
    copy_sample_rows(input_data, 0, output_data, 0, cs_.max_v_samp_factor, cs_.image_width);
    expand_right_edge(output_data, cs_.max_v_samp_factor, cs_.image_width,
                      c.width_in_blocks * data_unit());
  }

  void h2v2_smooth_downsample(const ComponentInfo& c, T** input_data, T** output_data) {
    unsigned output_cols = c.width_in_blocks * data_unit();
    expand_right_edge(input_data - 1, cs_.max_v_samp_factor + 2, cs_.image_width,
                      output_cols * 2);
    JLong memberscale = 16384 - cs_.smoothing_factor * 80;
    JLong neighscale = cs_.smoothing_factor * 16;
    int inrow = 0;
    for (int outrow = 0; outrow < c.v_samp_factor; outrow++) {
      T* outptr = output_data[outrow];
      const T* inptr0 = input_data[inrow];
      const T* inptr1 = input_data[inrow + 1];
      const T* above_ptr = input_data[inrow - 1];
      const T* below_ptr = input_data[inrow + 2];
      JLong membersum = inptr0[0] + inptr0[1] + inptr1[0] + inptr1[1];
      JLong neighsum = above_ptr[0] + above_ptr[1] + below_ptr[0] + below_ptr[1] + inptr0[0] +
                       inptr0[2] + inptr1[0] + inptr1[2];
      neighsum += neighsum;
      neighsum += above_ptr[0] + above_ptr[2] + below_ptr[0] + below_ptr[2];
      membersum = membersum * memberscale + neighsum * neighscale;
      *outptr++ = static_cast<T>((membersum + 32768) >> 16);
      inptr0 += 2;
      inptr1 += 2;
      above_ptr += 2;
      below_ptr += 2;
      for (unsigned colctr = output_cols - 2; colctr > 0; colctr--) {
        membersum = inptr0[0] + inptr0[1] + inptr1[0] + inptr1[1];
        neighsum = above_ptr[0] + above_ptr[1] + below_ptr[0] + below_ptr[1] + inptr0[-1] +
                   inptr0[2] + inptr1[-1] + inptr1[2];
        neighsum += neighsum;
        neighsum += above_ptr[-1] + above_ptr[2] + below_ptr[-1] + below_ptr[2];
        membersum = membersum * memberscale + neighsum * neighscale;
        *outptr++ = static_cast<T>((membersum + 32768) >> 16);
        inptr0 += 2;
        inptr1 += 2;
        above_ptr += 2;
        below_ptr += 2;
      }
      membersum = inptr0[0] + inptr0[1] + inptr1[0] + inptr1[1];
      neighsum = above_ptr[0] + above_ptr[1] + below_ptr[0] + below_ptr[1] + inptr0[-1] +
                 inptr0[1] + inptr1[-1] + inptr1[1];
      neighsum += neighsum;
      neighsum += above_ptr[-1] + above_ptr[1] + below_ptr[-1] + below_ptr[1];
      membersum = membersum * memberscale + neighsum * neighscale;
      *outptr = static_cast<T>((membersum + 32768) >> 16);
      inrow += 2;
    }
  }

  void fullsize_smooth_downsample(const ComponentInfo& c, T** input_data, T** output_data) {
    unsigned output_cols = c.width_in_blocks * data_unit();
    expand_right_edge(input_data - 1, cs_.max_v_samp_factor + 2, cs_.image_width, output_cols);
    JLong memberscale = 65536L - cs_.smoothing_factor * 512L;
    JLong neighscale = cs_.smoothing_factor * 64;
    for (int outrow = 0; outrow < c.v_samp_factor; outrow++) {
      T* outptr = output_data[outrow];
      const T* inptr = input_data[outrow];
      const T* above_ptr = input_data[outrow - 1];
      const T* below_ptr = input_data[outrow + 1];
      JLong colsum = (*above_ptr++) + (*below_ptr++) + inptr[0];
      JLong membersum = *inptr++;
      JLong nextcolsum = above_ptr[0] + below_ptr[0] + inptr[0];
      JLong neighsum = colsum + (colsum - membersum) + nextcolsum;
      membersum = membersum * memberscale + neighsum * neighscale;
      *outptr++ = static_cast<T>((membersum + 32768) >> 16);
      JLong lastcolsum = colsum;
      colsum = nextcolsum;
      for (unsigned colctr = output_cols - 2; colctr > 0; colctr--) {
        membersum = *inptr++;
        above_ptr++;
        below_ptr++;
        nextcolsum = above_ptr[0] + below_ptr[0] + inptr[0];
        neighsum = lastcolsum + (colsum - membersum) + nextcolsum;
        membersum = membersum * memberscale + neighsum * neighscale;
        *outptr++ = static_cast<T>((membersum + 32768) >> 16);
        lastcolsum = colsum;
        colsum = nextcolsum;
      }
      membersum = *inptr;
      neighsum = lastcolsum + (colsum - membersum) + colsum;
      membersum = membersum * memberscale + neighsum * neighscale;
      *outptr = static_cast<T>((membersum + 32768) >> 16);
    }
  }

  CState& cs_;
  MethodFn methods_[MAX_COMPONENTS]{};
  MethodFn h2v1 = &Downsampler::h2v1_downsample;
  MethodFn h2v2 = &Downsampler::h2v2_downsample;
  bool h2v1_slots_[MAX_COMPONENTS]{};
  bool h2v2_slots_[MAX_COMPONENTS]{};
};

// ---------------------------------------------------------------------------
// Preprocessing controller
template <class T>
class PrepController {
 public:
  PrepController(CState& cs, ColorConverter<T>* cconvert, Downsampler<T>* downsample)
      : cs_(cs), cconvert_(cconvert), downsample_(downsample) {
    int data_unit = cs.lossless ? 1 : DCTSIZE;
    color_buf_.resize(cs.num_components);
    if (downsample->need_context_rows) {
      context_ = true;
      int rgroup_height = cs.max_v_samp_factor;
      fake_.resize(cs.num_components);
      for (int ci = 0; ci < cs.num_components; ci++) {
        const ComponentInfo& c = cs.comp_info[ci];
        true_buf_.emplace_back(
            static_cast<std::size_t>((static_cast<long>(c.width_in_blocks) * data_unit *
                                      cs.max_h_samp_factor) /
                                     c.h_samp_factor),
            static_cast<std::size_t>(3 * rgroup_height));
        fake_[ci].assign(5 * rgroup_height, nullptr);
        T** tb = true_buf_.back().rows();
        for (int i = 0; i < 3 * rgroup_height; i++) fake_[ci][rgroup_height + i] = tb[i];
        for (int i = 0; i < rgroup_height; i++) {
          fake_[ci][i] = tb[2 * rgroup_height + i];
          fake_[ci][4 * rgroup_height + i] = tb[i];
        }
        color_buf_[ci] = fake_[ci].data() + rgroup_height;
      }
    } else {
      for (int ci = 0; ci < cs.num_components; ci++) {
        const ComponentInfo& c = cs.comp_info[ci];
        true_buf_.emplace_back(
            static_cast<std::size_t>((static_cast<long>(c.width_in_blocks) * data_unit *
                                      cs.max_h_samp_factor) /
                                     c.h_samp_factor),
            static_cast<std::size_t>(cs.max_v_samp_factor));
        color_buf_[ci] = true_buf_.back().rows();
      }
    }
  }

  void start_pass() {
    rows_to_go_ = cs_.image_height;
    next_buf_row_ = 0;
    this_row_group_ = 0;
    next_buf_stop_ = 2 * cs_.max_v_samp_factor;
  }

  void pre_process_data(T** input_buf, unsigned* in_row_ctr, unsigned in_rows_avail,
                        T*** output_buf, unsigned* out_row_group_ctr,
                        unsigned out_row_groups_avail) {
    if (context_)
      pre_process_context(input_buf, in_row_ctr, in_rows_avail, output_buf, out_row_group_ctr,
                          out_row_groups_avail);
    else
      pre_process_simple(input_buf, in_row_ctr, in_rows_avail, output_buf, out_row_group_ctr,
                         out_row_groups_avail);
  }

 private:
  static void expand_bottom_edge(T** image_data, unsigned num_cols, int input_rows,
                                 int output_rows) {
    for (int row = input_rows; row < output_rows; row++)
      copy_sample_rows(image_data, input_rows - 1, image_data, row, 1, num_cols);
  }

  void pre_process_simple(T** input_buf, unsigned* in_row_ctr, unsigned in_rows_avail,
                          T*** output_buf, unsigned* out_row_group_ctr,
                          unsigned out_row_groups_avail) {
    int data_unit = cs_.lossless ? 1 : DCTSIZE;
    while (*in_row_ctr < in_rows_avail && *out_row_group_ctr < out_row_groups_avail) {
      unsigned inrows = in_rows_avail - *in_row_ctr;
      int numrows = cs_.max_v_samp_factor - next_buf_row_;
      numrows = static_cast<int>(std::min(static_cast<unsigned>(numrows), inrows));
      cconvert_->color_convert(input_buf + *in_row_ctr, color_buf_.data(),
                               static_cast<unsigned>(next_buf_row_), numrows);
      *in_row_ctr += numrows;
      next_buf_row_ += numrows;
      rows_to_go_ -= numrows;
      if (rows_to_go_ == 0 && next_buf_row_ < cs_.max_v_samp_factor) {
        for (int ci = 0; ci < cs_.num_components; ci++)
          expand_bottom_edge(color_buf_[ci], cs_.image_width, next_buf_row_,
                             cs_.max_v_samp_factor);
        next_buf_row_ = cs_.max_v_samp_factor;
      }
      if (next_buf_row_ == cs_.max_v_samp_factor) {
        downsample_->downsample(color_buf_.data(), 0, output_buf, *out_row_group_ctr);
        next_buf_row_ = 0;
        (*out_row_group_ctr)++;
      }
      if (rows_to_go_ == 0 && *out_row_group_ctr < out_row_groups_avail) {
        for (int ci = 0; ci < cs_.num_components; ci++) {
          const ComponentInfo& c = cs_.comp_info[ci];
          expand_bottom_edge(output_buf[ci], c.width_in_blocks * data_unit,
                             static_cast<int>(*out_row_group_ctr * c.v_samp_factor),
                             static_cast<int>(out_row_groups_avail * c.v_samp_factor));
        }
        *out_row_group_ctr = out_row_groups_avail;
        break;
      }
    }
  }

  void pre_process_context(T** input_buf, unsigned* in_row_ctr, unsigned in_rows_avail,
                           T*** output_buf, unsigned* out_row_group_ctr,
                           unsigned out_row_groups_avail) {
    int buf_height = cs_.max_v_samp_factor * 3;
    while (*out_row_group_ctr < out_row_groups_avail) {
      if (*in_row_ctr < in_rows_avail) {
        unsigned inrows = in_rows_avail - *in_row_ctr;
        int numrows = next_buf_stop_ - next_buf_row_;
        numrows = static_cast<int>(std::min(static_cast<unsigned>(numrows), inrows));
        cconvert_->color_convert(input_buf + *in_row_ctr, color_buf_.data(),
                                 static_cast<unsigned>(next_buf_row_), numrows);
        if (rows_to_go_ == cs_.image_height) {
          for (int ci = 0; ci < cs_.num_components; ci++)
            for (int row = 1; row <= cs_.max_v_samp_factor; row++)
              copy_sample_rows(color_buf_[ci], 0, color_buf_[ci], -row, 1, cs_.image_width);
        }
        *in_row_ctr += numrows;
        next_buf_row_ += numrows;
        rows_to_go_ -= numrows;
      } else {
        if (rows_to_go_ != 0) break;
        if (next_buf_row_ < next_buf_stop_) {
          for (int ci = 0; ci < cs_.num_components; ci++)
            expand_bottom_edge(color_buf_[ci], cs_.image_width, next_buf_row_, next_buf_stop_);
          next_buf_row_ = next_buf_stop_;
        }
      }
      if (next_buf_row_ == next_buf_stop_) {
        downsample_->downsample(color_buf_.data(), static_cast<unsigned>(this_row_group_),
                                output_buf, *out_row_group_ctr);
        (*out_row_group_ctr)++;
        this_row_group_ += cs_.max_v_samp_factor;
        if (this_row_group_ >= buf_height) this_row_group_ = 0;
        if (next_buf_row_ >= buf_height) next_buf_row_ = 0;
        next_buf_stop_ = next_buf_row_ + cs_.max_v_samp_factor;
      }
    }
  }

  CState& cs_;
  ColorConverter<T>* cconvert_;
  Downsampler<T>* downsample_;
  bool context_ = false;
  std::vector<SampleArray<T>> true_buf_;
  std::vector<std::vector<T*>> fake_;
  std::vector<T**> color_buf_;
  unsigned rows_to_go_ = 0;
  int next_buf_row_ = 0, this_row_group_ = 0, next_buf_stop_ = 0;
};

// ---------------------------------------------------------------------------
// Coefficient / difference controller interface (compression side)
template <class T>
class CCoefController {
 public:
  enum class Mode { PassThru, SaveAndPass, CrankDest };
  virtual ~CCoefController() = default;
  virtual void start_pass(Mode mode) = 0;
  virtual bool compress_data(T*** input_buf) = 0;
};

// Lossy coefficient controller
template <class T>
class CoefControllerC final : public CCoefController<T> {
 public:
  using Mode = typename CCoefController<T>::Mode;

  CoefControllerC(CState& cs, ForwardDct<T>* fdct, EntropyEncoder** entropy, bool need_full_buffer)
      : cs_(cs), fdct_(fdct), entropy_(entropy) {
    if (need_full_buffer) {
      whole_image_.resize(cs.num_components);
      for (int ci = 0; ci < cs.num_components; ci++) {
        const ComponentInfo& c = cs.comp_info[ci];
        whole_image_[ci].allocate(
            static_cast<std::size_t>(round_up(c.width_in_blocks, c.h_samp_factor)),
            static_cast<std::size_t>(round_up(c.height_in_blocks, c.v_samp_factor)));
      }
    } else {
      mcu_blocks_ = aligned_alloc_zeroed<Block>(C_MAX_BLOCKS_IN_MCU);
      for (int i = 0; i < C_MAX_BLOCKS_IN_MCU; i++) MCU_buffer_[i] = &mcu_blocks_[i];
    }
  }

  void start_pass(Mode mode) override {
    iMCU_row_num_ = 0;
    start_iMCU_row();
    if (mode == Mode::PassThru) {
      if (!whole_image_.empty()) VIO_FAIL("Bogus buffer control mode");
    } else if (whole_image_.empty()) {
      VIO_FAIL("Bogus buffer control mode");
    }
    mode_ = mode;
  }

  bool compress_data(T*** input_buf) override {
    switch (mode_) {
      case Mode::PassThru: return compress_onepass(input_buf);
      case Mode::SaveAndPass: return compress_first_pass(input_buf);
      default: return compress_output();
    }
  }

 private:
  void start_iMCU_row() {
    if (cs_.comps_in_scan > 1) {
      MCU_rows_per_iMCU_row_ = 1;
    } else {
      if (iMCU_row_num_ < cs_.total_iMCU_rows - 1)
        MCU_rows_per_iMCU_row_ = cs_.cur_comp_info[0]->v_samp_factor;
      else
        MCU_rows_per_iMCU_row_ = cs_.cur_comp_info[0]->last_row_height;
    }
    mcu_ctr_ = 0;
    MCU_vert_offset_ = 0;
  }

  bool compress_onepass(T*** input_buf) {
    unsigned last_MCU_col = cs_.MCUs_per_row - 1;
    unsigned last_iMCU_row = cs_.total_iMCU_rows - 1;
    for (int yoffset = MCU_vert_offset_; yoffset < MCU_rows_per_iMCU_row_; yoffset++) {
      for (unsigned MCU_col_num = mcu_ctr_; MCU_col_num <= last_MCU_col; MCU_col_num++) {
        int blkn = 0;
        for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
          ComponentInfo* c = cs_.cur_comp_info[ci];
          int blockcnt = (MCU_col_num < last_MCU_col) ? c->MCU_width : c->last_col_width;
          unsigned xpos = MCU_col_num * c->MCU_sample_width;
          unsigned ypos = yoffset * DCTSIZE;
          for (int yindex = 0; yindex < c->MCU_height; yindex++) {
            if (iMCU_row_num_ < last_iMCU_row || yoffset + yindex < c->last_row_height) {
              fdct_->forward_dct(c->quant_tbl_no, input_buf[c->component_index],
                                 MCU_buffer_[blkn], ypos, xpos, static_cast<unsigned>(blockcnt));
              if (blockcnt < c->MCU_width) {
                std::memset(MCU_buffer_[blkn + blockcnt], 0,
                            (c->MCU_width - blockcnt) * sizeof(Block));
                for (int bi = blockcnt; bi < c->MCU_width; bi++)
                  (*MCU_buffer_[blkn + bi])[0] = (*MCU_buffer_[blkn + bi - 1])[0];
              }
            } else {
              std::memset(MCU_buffer_[blkn], 0, c->MCU_width * sizeof(Block));
              for (int bi = 0; bi < c->MCU_width; bi++)
                (*MCU_buffer_[blkn + bi])[0] = (*MCU_buffer_[blkn - 1])[0];
            }
            blkn += c->MCU_width;
            ypos += DCTSIZE;
          }
        }
        (*entropy_)->encode_mcu(MCU_buffer_);
      }
      mcu_ctr_ = 0;
    }
    iMCU_row_num_++;
    start_iMCU_row();
    return true;
  }

  bool compress_first_pass(T*** input_buf) {
    unsigned last_iMCU_row = cs_.total_iMCU_rows - 1;
    for (int ci = 0; ci < cs_.num_components; ci++) {
      ComponentInfo& c = cs_.comp_info[ci];
      BlockArray& whole = whole_image_[ci];
      std::size_t base = static_cast<std::size_t>(iMCU_row_num_) * c.v_samp_factor;
      int block_rows;
      if (iMCU_row_num_ < last_iMCU_row) {
        block_rows = c.v_samp_factor;
      } else {
        block_rows = static_cast<int>(c.height_in_blocks % c.v_samp_factor);
        if (block_rows == 0) block_rows = c.v_samp_factor;
      }
      unsigned blocks_across = c.width_in_blocks;
      int h_samp_factor = c.h_samp_factor;
      int ndummy = static_cast<int>(blocks_across % h_samp_factor);
      if (ndummy > 0) ndummy = h_samp_factor - ndummy;
      for (int block_row = 0; block_row < block_rows; block_row++) {
        Block* thisblockrow = whole.row(base + block_row);
        fdct_->forward_dct(c.quant_tbl_no, input_buf[ci], thisblockrow,
                           static_cast<unsigned>(block_row * DCTSIZE), 0, blocks_across);
        if (ndummy > 0) {
          thisblockrow += blocks_across;
          std::memset(thisblockrow, 0, ndummy * sizeof(Block));
          Coef lastDC = thisblockrow[-1][0];
          for (int bi = 0; bi < ndummy; bi++) thisblockrow[bi][0] = lastDC;
        }
      }
      if (iMCU_row_num_ == last_iMCU_row) {
        blocks_across += ndummy;
        unsigned MCUs_across = blocks_across / h_samp_factor;
        for (int block_row = block_rows; block_row < c.v_samp_factor; block_row++) {
          Block* thisblockrow = whole.row(base + block_row);
          const Block* lastblockrow = whole.row(base + block_row - 1);
          std::memset(thisblockrow, 0, blocks_across * sizeof(Block));
          for (unsigned MCUindex = 0; MCUindex < MCUs_across; MCUindex++) {
            Coef lastDC = lastblockrow[h_samp_factor - 1][0];
            for (int bi = 0; bi < h_samp_factor; bi++) thisblockrow[bi][0] = lastDC;
            thisblockrow += h_samp_factor;
            lastblockrow += h_samp_factor;
          }
        }
      }
    }
    return compress_output();
  }

  bool compress_output() {
    for (int yoffset = MCU_vert_offset_; yoffset < MCU_rows_per_iMCU_row_; yoffset++) {
      if (cs_.comps_in_scan == 1) {
        // One block per MCU, consecutive in the row.
        ComponentInfo* c = cs_.cur_comp_info[0];
        const Block* row = whole_image_[c->component_index].row(
            static_cast<std::size_t>(iMCU_row_num_) * c->v_samp_factor + yoffset);
        if ((*entropy_)->encode_blocks(row + mcu_ctr_, cs_.MCUs_per_row - mcu_ctr_)) {
          mcu_ctr_ = 0;
          continue;
        }
      }
      for (unsigned MCU_col_num = mcu_ctr_; MCU_col_num < cs_.MCUs_per_row; MCU_col_num++) {
        int blkn = 0;
        for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
          ComponentInfo* c = cs_.cur_comp_info[ci];
          BlockArray& whole = whole_image_[c->component_index];
          unsigned start_col = MCU_col_num * c->MCU_width;
          for (int yindex = 0; yindex < c->MCU_height; yindex++) {
            Block* buffer_ptr =
                whole.row(static_cast<std::size_t>(iMCU_row_num_) * c->v_samp_factor + yindex +
                          yoffset) +
                start_col;
            for (int xindex = 0; xindex < c->MCU_width; xindex++)
              MCU_buffer_[blkn++] = buffer_ptr++;
          }
        }
        (*entropy_)->encode_mcu(MCU_buffer_);
      }
      mcu_ctr_ = 0;
    }
    iMCU_row_num_++;
    start_iMCU_row();
    return true;
  }

  CState& cs_;
  ForwardDct<T>* fdct_;
  EntropyEncoder** entropy_;
  Mode mode_ = Mode::PassThru;
  unsigned iMCU_row_num_ = 0, mcu_ctr_ = 0;
  int MCU_vert_offset_ = 0, MCU_rows_per_iMCU_row_ = 0;
  Block* MCU_buffer_[C_MAX_BLOCKS_IN_MCU]{};
  AlignedPtr<Block> mcu_blocks_;
  std::vector<BlockArray> whole_image_;
};

// Transcoding coefficient controller: emits caller-supplied
// coefficient arrays.
class TranscodeCoefController {
 public:
  TranscodeCoefController(CState& cs, EntropyEncoder** entropy, BlockArray* coef_arrays)
      : cs_(cs), entropy_(entropy), whole_image_(coef_arrays) {
    dummy_ = aligned_alloc_zeroed<Block>(C_MAX_BLOCKS_IN_MCU);
  }

  void start_pass() {
    iMCU_row_num_ = 0;
    start_iMCU_row();
  }

  void compress_output() {
    unsigned last_MCU_col = cs_.MCUs_per_row - 1;
    unsigned last_iMCU_row = cs_.total_iMCU_rows - 1;
    Block* MCU_buffer[C_MAX_BLOCKS_IN_MCU];
    for (int yoffset = MCU_vert_offset_; yoffset < MCU_rows_per_iMCU_row_; yoffset++) {
      if (cs_.comps_in_scan == 1) {
        // One block per MCU, consecutive in the row.
        ComponentInfo* c = cs_.cur_comp_info[0];
        const Block* row = whole_image_[c->component_index].row(
            static_cast<std::size_t>(iMCU_row_num_) * c->v_samp_factor + yoffset);
        if ((*entropy_)->encode_blocks(row + mcu_ctr_, cs_.MCUs_per_row - mcu_ctr_)) {
          mcu_ctr_ = 0;
          continue;
        }
      }
      for (unsigned MCU_col_num = mcu_ctr_; MCU_col_num < cs_.MCUs_per_row; MCU_col_num++) {
        int blkn = 0;
        for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
          ComponentInfo* c = cs_.cur_comp_info[ci];
          BlockArray& whole = whole_image_[c->component_index];
          unsigned start_col = MCU_col_num * c->MCU_width;
          int blockcnt = (MCU_col_num < last_MCU_col) ? c->MCU_width : c->last_col_width;
          for (int yindex = 0; yindex < c->MCU_height; yindex++) {
            int xindex;
            if (iMCU_row_num_ < last_iMCU_row || yindex + yoffset < c->last_row_height) {
              Block* buffer_ptr =
                  whole.row(static_cast<std::size_t>(iMCU_row_num_) * c->v_samp_factor + yindex +
                            yoffset) +
                  start_col;
              for (xindex = 0; xindex < blockcnt; xindex++) MCU_buffer[blkn++] = buffer_ptr++;
            } else {
              xindex = 0;
            }
            for (; xindex < c->MCU_width; xindex++) {
              MCU_buffer[blkn] = &dummy_[blkn];
              (*MCU_buffer[blkn])[0] = (*MCU_buffer[blkn - 1])[0];
              blkn++;
            }
          }
        }
        (*entropy_)->encode_mcu(MCU_buffer);
      }
      mcu_ctr_ = 0;
    }
    iMCU_row_num_++;
    start_iMCU_row();
  }

 private:
  void start_iMCU_row() {
    if (cs_.comps_in_scan > 1) {
      MCU_rows_per_iMCU_row_ = 1;
    } else {
      if (iMCU_row_num_ < cs_.total_iMCU_rows - 1)
        MCU_rows_per_iMCU_row_ = cs_.cur_comp_info[0]->v_samp_factor;
      else
        MCU_rows_per_iMCU_row_ = cs_.cur_comp_info[0]->last_row_height;
    }
    mcu_ctr_ = 0;
    MCU_vert_offset_ = 0;
  }

  CState& cs_;
  EntropyEncoder** entropy_;
  BlockArray* whole_image_;
  AlignedPtr<Block> dummy_;
  unsigned iMCU_row_num_ = 0, mcu_ctr_ = 0;
  int MCU_vert_offset_ = 0, MCU_rows_per_iMCU_row_ = 0;
};

// ---------------------------------------------------------------------------
// Lossless compressor: differencing + point transform
template <class T>
class LosslessCompressor {
 public:
  explicit LosslessCompressor(CState& cs) : cs_(cs) {}

  void start_pass() {
    if (cs_.restart_interval % cs_.MCUs_per_row != 0)
      VIO_FAIL("Invalid restart interval %d; must be an integer multiple of the number of MCUs "
               "in an MCU row (%d)",
               cs_.restart_interval, cs_.MCUs_per_row);
    for (int ci = 0; ci < cs_.num_components; ci++) reset_predictor(ci);
  }

  void scale(const T* input_buf, T* output_buf, unsigned width) const {
    if (cs_.Al) {
      for (unsigned i = 0; i < width; i++) output_buf[i] = static_cast<T>(input_buf[i] >> cs_.Al);
    } else {
      std::memcpy(output_buf, input_buf, width * sizeof(T));
    }
  }

  void difference(int ci, const T* input_buf, const T* prev_row, int* diff_buf, unsigned width) {
    int psv = predictor_[ci];
    if (psv == 0) {
      bool restart = diff_1d(ci, input_buf, diff_buf, width,
                             1 << (cs_.data_precision - cs_.Al - 1));
      if (!restart) {
        if (cs_.Ss < 1 || cs_.Ss > 7)
          VIO_FAIL("Invalid progressive/lossless parameters Ss=%d Se=%d Ah=%d Al=%d", cs_.Ss,
                   cs_.Se, cs_.Ah, cs_.Al);
        predictor_[ci] = cs_.Ss;
      }
      return;
    }
    switch (psv) {
      case 1: diff_1d(ci, input_buf, diff_buf, width, prev_row[0]); break;
      case 2: diff_2d<2>(ci, input_buf, prev_row, diff_buf, width); break;
      case 3: diff_2d<3>(ci, input_buf, prev_row, diff_buf, width); break;
      case 4: diff_2d<4>(ci, input_buf, prev_row, diff_buf, width); break;
      case 5: diff_2d<5>(ci, input_buf, prev_row, diff_buf, width); break;
      case 6: diff_2d<6>(ci, input_buf, prev_row, diff_buf, width); break;
      default: diff_2d<7>(ci, input_buf, prev_row, diff_buf, width); break;
    }
  }

 private:
  void reset_predictor(int ci) {
    restart_rows_to_go_[ci] = cs_.restart_interval / cs_.MCUs_per_row;
    predictor_[ci] = 0;
  }

  bool diff_1d(int ci, const T* input_buf, int* diff_buf, unsigned width, int initial) {
    bool restart = false;
    int samp = *input_buf++;
    *diff_buf++ = samp - initial;
    while (--width) {
      int Ra = samp;
      samp = *input_buf++;
      *diff_buf++ = samp - Ra;
    }
    if (cs_.restart_interval) {
      if (--restart_rows_to_go_[ci] == 0) {
        reset_predictor(ci);
        restart = true;
      }
    }
    return restart;
  }

  template <int P>
  void diff_2d(int ci, const T* input_buf, const T* prev_row, int* diff_buf, unsigned width) {
    int Ra = 0, Rb, Rc = 0;
    Rb = *prev_row++;
    int samp = *input_buf++;
    *diff_buf++ = samp - Rb;
    while (--width) {
      Rc = Rb;
      Rb = *prev_row++;
      Ra = samp;
      samp = *input_buf++;
      int pred;
      if constexpr (P == 2) pred = Rb;
      else if constexpr (P == 3) pred = Rc;
      else if constexpr (P == 4) pred = static_cast<int>(JLong(Ra) + JLong(Rb) - JLong(Rc));
      else if constexpr (P == 5) pred = static_cast<int>(JLong(Ra) + ((JLong(Rb) - JLong(Rc)) >> 1));
      else if constexpr (P == 6) pred = static_cast<int>(JLong(Rb) + ((JLong(Ra) - JLong(Rc)) >> 1));
      else pred = static_cast<int>((JLong(Ra) + JLong(Rb)) >> 1);
      *diff_buf++ = samp - pred;
    }
    if (cs_.restart_interval) {
      if (--restart_rows_to_go_[ci] == 0) reset_predictor(ci);
    }
  }

  CState& cs_;
  int predictor_[MAX_COMPONENTS]{};
  unsigned restart_rows_to_go_[MAX_COMPONENTS]{};
};

// Lossless difference controller
template <class T>
class DiffControllerC final : public CCoefController<T> {
 public:
  using Mode = typename CCoefController<T>::Mode;

  DiffControllerC(CState& cs, LosslessCompressor<T>* lc, EntropyEncoder** entropy,
                  bool need_full_buffer)
      : cs_(cs), lc_(lc), entropy_(entropy) {
    cur_row_.resize(cs.num_components);
    prev_row_.resize(cs.num_components);
    diff_buf_.resize(cs.num_components);
    diff_rows_.resize(cs.num_components);
    for (int ci = 0; ci < cs.num_components; ci++) {
      const ComponentInfo& c = cs.comp_info[ci];
      std::size_t w = static_cast<std::size_t>(round_up(c.width_in_blocks, c.h_samp_factor));
      cur_store_.emplace_back(w, 1);
      prev_store_.emplace_back(w, 1);
      cur_row_[ci] = cur_store_.back()[0];
      prev_row_[ci] = prev_store_.back()[0];
      diff_buf_[ci].allocate(w, c.v_samp_factor);
      diff_rows_[ci] = diff_buf_[ci].rows();
    }
    if (need_full_buffer) {
      whole_image_.resize(cs.num_components);
      for (int ci = 0; ci < cs.num_components; ci++) {
        const ComponentInfo& c = cs.comp_info[ci];
        whole_image_[ci].allocate(
            static_cast<std::size_t>(round_up(c.width_in_blocks, c.h_samp_factor)),
            static_cast<std::size_t>(round_up(c.height_in_blocks, c.v_samp_factor)));
      }
    }
  }

  void start_pass(Mode mode) override {
    if (mode == Mode::CrankDest) lc_->start_pass();
    iMCU_row_num_ = 0;
    start_iMCU_row();
    if (mode == Mode::PassThru) {
      if (!whole_image_.empty()) VIO_FAIL("Bogus buffer control mode");
    } else if (whole_image_.empty()) {
      VIO_FAIL("Bogus buffer control mode");
    }
    mode_ = mode;
  }

  bool compress_data(T*** input_buf) override {
    if (mode_ == Mode::PassThru) return compress(input_buf);
    T** buffer[MAX_COMPONENTS];
    if (mode_ == Mode::SaveAndPass) {
      unsigned last_iMCU_row = cs_.total_iMCU_rows - 1;
      for (int ci = 0; ci < cs_.num_components; ci++) {
        const ComponentInfo& c = cs_.comp_info[ci];
        T** rows = whole_image_[ci].rows() + iMCU_row_num_ * c.v_samp_factor;
        int samp_rows;
        if (iMCU_row_num_ < last_iMCU_row) {
          samp_rows = c.v_samp_factor;
        } else {
          samp_rows = static_cast<int>(c.height_in_blocks % c.v_samp_factor);
          if (samp_rows == 0) samp_rows = c.v_samp_factor;
        }
        for (int r = 0; r < samp_rows; r++)
          std::memcpy(rows[r], input_buf[ci][r], c.width_in_blocks * sizeof(T));
      }
    }
    for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
      ComponentInfo* c = cs_.cur_comp_info[ci];
      int compi = c->component_index;
      buffer[compi] = whole_image_[compi].rows() + iMCU_row_num_ * c->v_samp_factor;
    }
    return compress(buffer);
  }

 private:
  void start_iMCU_row() {
    if (cs_.comps_in_scan > 1) {
      MCU_rows_per_iMCU_row_ = 1;
    } else {
      if (iMCU_row_num_ < cs_.total_iMCU_rows - 1)
        MCU_rows_per_iMCU_row_ = cs_.cur_comp_info[0]->v_samp_factor;
      else
        MCU_rows_per_iMCU_row_ = cs_.cur_comp_info[0]->last_row_height;
    }
    mcu_ctr_ = 0;
    MCU_vert_offset_ = 0;
  }

  bool compress(T*** input_buf) {
    unsigned last_iMCU_row = cs_.total_iMCU_rows - 1;
    for (int yoffset = MCU_vert_offset_; yoffset < MCU_rows_per_iMCU_row_; yoffset++) {
      unsigned MCU_col_num = mcu_ctr_;
      if (MCU_col_num == 0) {
        for (int ci = 0; ci < cs_.comps_in_scan; ci++) {
          ComponentInfo* c = cs_.cur_comp_info[ci];
          int compi = c->component_index;
          int samp_rows;
          if (iMCU_row_num_ < last_iMCU_row) {
            samp_rows = c->v_samp_factor;
          } else {
            samp_rows = static_cast<int>(c->height_in_blocks % c->v_samp_factor);
            if (samp_rows == 0) {
              samp_rows = c->v_samp_factor;
            } else {
              for (int samp_row = samp_rows; samp_row < c->v_samp_factor; samp_row++)
                std::memset(diff_buf_[compi][samp_row], 0,
                            round_up(c->width_in_blocks, c->h_samp_factor) * sizeof(int));
            }
          }
          unsigned samps_across = c->width_in_blocks;
          for (int samp_row = 0; samp_row < samp_rows; samp_row++) {
            lc_->scale(input_buf[compi][samp_row], cur_row_[compi], samps_across);
            lc_->difference(compi, cur_row_[compi], prev_row_[compi], diff_buf_[compi][samp_row],
                            samps_across);
            std::swap(cur_row_[compi], prev_row_[compi]);
          }
        }
      }
      (*entropy_)->encode_mcus(diff_rows_.data(), static_cast<unsigned>(yoffset), MCU_col_num,
                               cs_.MCUs_per_row - MCU_col_num);
      mcu_ctr_ = 0;
    }
    iMCU_row_num_++;
    start_iMCU_row();
    return true;
  }

  CState& cs_;
  LosslessCompressor<T>* lc_;
  EntropyEncoder** entropy_;
  Mode mode_ = Mode::PassThru;
  unsigned iMCU_row_num_ = 0, mcu_ctr_ = 0;
  int MCU_vert_offset_ = 0, MCU_rows_per_iMCU_row_ = 0;
  std::vector<SampleArray<T>> cur_store_, prev_store_;
  std::vector<T*> cur_row_, prev_row_;
  std::vector<SampleArray<int>> diff_buf_;
  std::vector<int**> diff_rows_;
  std::vector<SampleArray<T>> whole_image_;
};

// ---------------------------------------------------------------------------
// Main buffer controller
template <class T>
class MainControllerC {
 public:
  MainControllerC(CState& cs, PrepController<T>* prep, CCoefController<T>* coef)
      : cs_(cs), prep_(prep), coef_(coef) {
    if (cs.raw_data_in) return;
    int data_unit = cs.lossless ? 1 : DCTSIZE;
    buffer_.resize(cs.num_components);
    rows_.resize(cs.num_components);
    for (int ci = 0; ci < cs.num_components; ci++) {
      const ComponentInfo& c = cs.comp_info[ci];
      buffer_[ci].allocate(static_cast<std::size_t>(c.width_in_blocks) * data_unit,
                           static_cast<std::size_t>(c.v_samp_factor) * data_unit);
      rows_[ci] = buffer_[ci].rows();
    }
  }

  void start_pass() {
    if (cs_.raw_data_in) return;
    cur_iMCU_row_ = 0;
    rowgroup_ctr_ = 0;
  }

  void process_data(T** input_buf, unsigned* in_row_ctr, unsigned in_rows_avail) {
    unsigned data_unit = cs_.lossless ? 1 : DCTSIZE;
    while (cur_iMCU_row_ < cs_.total_iMCU_rows) {
      if (rowgroup_ctr_ < data_unit)
        prep_->pre_process_data(input_buf, in_row_ctr, in_rows_avail, rows_.data(),
                                &rowgroup_ctr_, data_unit);
      if (rowgroup_ctr_ != data_unit) return;
      coef_->compress_data(rows_.data());
      rowgroup_ctr_ = 0;
      cur_iMCU_row_++;
    }
  }

 private:
  CState& cs_;
  PrepController<T>* prep_;
  CCoefController<T>* coef_;
  std::vector<SampleArray<T>> buffer_;
  std::vector<T**> rows_;
  unsigned cur_iMCU_row_ = 0, rowgroup_ctr_ = 0;
};

}  // namespace vio::jpeg::detail
