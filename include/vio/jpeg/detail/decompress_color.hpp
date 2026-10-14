// vio::jpeg - output color conversion (including RGB565).
#pragma once

#include "decompress_state.hpp"
#include "../simd/dispatch.hpp"

namespace vio::jpeg::detail {

inline constexpr int SCALEBITS = 16;
inline constexpr JLong ONE_HALF = JLong(1) << (SCALEBITS - 1);
constexpr JLong fix16(double x) { return static_cast<JLong>(x * (1L << SCALEBITS) + 0.5); }

// YCbCr->RGB lookup tables (shared with the merged upsampler).
template <class T>
struct YccRgbTables {
  std::vector<int> Cr_r, Cb_b;
  // Green terms: 32 bits hold them (and their sum) up to 12-bit samples.
  using G = std::conditional_t<kBits<T> <= 12, int, JLong>;
  std::vector<G> Cr_g, Cb_g;
  // 8-bit samples: the terms of Cr and of Cb packed in one word each, so
  // that one addition gives all three: green sum + 2^30 in bits 0-31 (the
  // halves biased by 2^29 never carry), red + 512 in bits 32-47 and blue +
  // 512 in bits 48-63.
  std::vector<std::uint64_t> Cr_pack, Cb_pack;
  static constexpr int kPackBias = 512, kPackGreenBias = 1 << 14;  // (2^30 >> 16)
  void build() {
    constexpr int N = kMaxSample<T> + 1;
    Cr_r.resize(N);
    Cb_b.resize(N);
    Cr_g.resize(N);
    Cb_g.resize(N);
    JLong x = -kCenterSample<T>;
    for (int i = 0; i < N; i++, x++) {
      Cr_r[i] = static_cast<int>((fix16(1.40200) * x + ONE_HALF) >> SCALEBITS);
      Cb_b[i] = static_cast<int>((fix16(1.77200) * x + ONE_HALF) >> SCALEBITS);
      Cr_g[i] = static_cast<G>((-fix16(0.71414)) * x);
      Cb_g[i] = static_cast<G>((-fix16(0.34414)) * x + ONE_HALF);
    }
    if constexpr (kBits<T> == 8) {
      Cr_pack.resize(N);
      Cb_pack.resize(N);
      for (int i = 0; i < N; i++) {
        Cr_pack[i] = static_cast<std::uint64_t>(Cr_g[i] + (1 << 29)) |
                     static_cast<std::uint64_t>(Cr_r[i] + kPackBias) << 32;
        Cb_pack[i] = static_cast<std::uint64_t>(Cb_g[i] + (1 << 29)) |
                     static_cast<std::uint64_t>(Cb_b[i] + kPackBias) << 48;
      }
    }
  }
};

inline constexpr JLong dither_matrix_565[4] = {0x0008020A, 0x0C040E06, 0x030B0109, 0x0F070D05};
VIO_INLINE JLong dither_rotate(JLong x) { return ((x & 0xFF) << 24) | ((x >> 8) & 0x00FFFFFF); }
VIO_INLINE unsigned pack565(unsigned r, unsigned g, unsigned b) {
  return ((r << 8) & 0xF800) | ((g << 3) & 0x7E0) | (b >> 3);
}

template <class T>
class ColorDeconverter {
 public:
  using ConvertFn = void (ColorDeconverter::*)(T***, unsigned, T**, int);

  ColorDeconverter(DState& st, const T* range_limit) : st_(st), range_limit_(range_limit) {
    switch (st.jpeg_color_space) {
      case ColorSpace::Gray:
        if (st.num_components != 1) VIO_FAIL("Bogus JPEG colorspace");
        break;
      case ColorSpace::RGB:
      case ColorSpace::YCbCr:
        if (st.num_components != 3) VIO_FAIL("Bogus JPEG colorspace");
        break;
      case ColorSpace::CMYK:
      case ColorSpace::YCCK:
        if (st.num_components != 4) VIO_FAIL("Bogus JPEG colorspace");
        break;
      default:
        if (st.num_components < 1) VIO_FAIL("Bogus JPEG colorspace");
    }
    const ColorSpace in = st.jpeg_color_space, out = st.out_color_space;
    auto notimpl = [] { VIO_FAIL("Unsupported color conversion request"); };
    constexpr bool can_table = kBits<T> != 16;

    if (out == ColorSpace::Gray) {
      if (st.lossless && in != out) notimpl();
      st.out_color_components = 1;
      if (in == ColorSpace::Gray || in == ColorSpace::YCbCr) {
        convert_ = &ColorDeconverter::grayscale_convert;
        for (int ci = 1; ci < st.num_components; ci++) st.comp_info[ci].component_needed = false;
      } else if (in == ColorSpace::RGB) {
        if (!can_table) notimpl();
        build_rgb_y_table();
        convert_ = &ColorDeconverter::rgb_gray_convert;
      } else {
        notimpl();
      }
    } else if (is_rgb_family(out)) {
      if (st.lossless && in != ColorSpace::RGB) notimpl();
      st.out_color_components = rgb_pixelsize[idx(out)];
      if (in == ColorSpace::YCbCr) {
        if (!can_table) notimpl();
        tables_.build();
        convert_ = select<YccRgb>(out);
        if constexpr (kBits<T> == 8) simd_ = simd::ycc_rgb_kernel(st.simd, out);
      } else if (in == ColorSpace::Gray) {
        convert_ = select<GrayRgb>(out);
      } else if (in == ColorSpace::RGB) {
        if (rgb_red[idx(out)] == 0 && rgb_green[idx(out)] == 1 && rgb_blue[idx(out)] == 2 &&
            rgb_pixelsize[idx(out)] == 3)
          convert_ = &ColorDeconverter::null_convert;
        else
          convert_ = select<RgbRgb>(out);
      } else {
        notimpl();
      }
    } else if (out == ColorSpace::RGB565) {
      if (st.lossless) notimpl();
      if constexpr (kBits<T> != 8) notimpl();
      st.out_color_components = 3;
      bool dither = st.dither_mode != DitherMode::None;
      if (in == ColorSpace::YCbCr) {
        tables_.build();
        convert_ = dither ? &ColorDeconverter::ycc_rgb565D_convert
                          : &ColorDeconverter::ycc_rgb565_convert;
        if constexpr (kBits<T> == 8) {
          if (!dither) simd_ = simd::ycc_rgb_kernel(st.simd, out);
        }
      } else if (in == ColorSpace::Gray) {
        convert_ = dither ? &ColorDeconverter::gray_rgb565D_convert
                          : &ColorDeconverter::gray_rgb565_convert;
      } else if (in == ColorSpace::RGB) {
        convert_ = dither ? &ColorDeconverter::rgb_rgb565D_convert
                          : &ColorDeconverter::rgb_rgb565_convert;
      } else {
        notimpl();
      }
    } else if (out == ColorSpace::CMYK) {
      if (st.lossless && in != out) notimpl();
      st.out_color_components = 4;
      if (in == ColorSpace::YCCK) {
        if (!can_table) notimpl();
        tables_.build();
        convert_ = &ColorDeconverter::ycck_cmyk_convert;
      } else if (in == ColorSpace::CMYK) {
        convert_ = &ColorDeconverter::null_convert;
      } else {
        notimpl();
      }
    } else {
      if (out == in) {
        st.out_color_components = st.num_components;
        convert_ = &ColorDeconverter::null_convert;
      } else {
        notimpl();
      }
    }
    st.output_components = st.quantize_colors ? 1 : st.out_color_components;
  }

  void start_pass() {}

  VIO_INLINE void color_convert(T*** input_buf, unsigned input_row, T** output_buf,
                                int num_rows) {
    if constexpr (kBits<T> == 8) {
      if (simd_) return simd_(st_.output_width, input_buf, input_row, output_buf, num_rows);
    }
    (this->*convert_)(input_buf, input_row, output_buf, num_rows);
  }

  // Replaceable by SIMD back ends.
  void set_convert(ConvertFn fn) {
    convert_ = fn;
    simd_ = nullptr;
  }
  ConvertFn convert_fn() const { return convert_; }
  const YccRgbTables<T>& tables() const { return tables_; }

 private:
  enum Kind { YccRgb, GrayRgb, RgbRgb };

  template <Kind K>
  ConvertFn select(ColorSpace out) {
    switch (out) {
      case ColorSpace::ExtRGBX:
      case ColorSpace::ExtRGBA: return &ColorDeconverter::conv<K, 0, 1, 2, 3, 4>;
      case ColorSpace::ExtBGR: return &ColorDeconverter::conv<K, 2, 1, 0, -1, 3>;
      case ColorSpace::ExtBGRX:
      case ColorSpace::ExtBGRA: return &ColorDeconverter::conv<K, 2, 1, 0, 3, 4>;
      case ColorSpace::ExtXBGR:
      case ColorSpace::ExtABGR: return &ColorDeconverter::conv<K, 3, 2, 1, 0, 4>;
      case ColorSpace::ExtXRGB:
      case ColorSpace::ExtARGB: return &ColorDeconverter::conv<K, 1, 2, 3, 0, 4>;
      default: return &ColorDeconverter::conv<K, 0, 1, 2, -1, 3>;
    }
  }

  template <Kind K, int R, int G, int B, int A, int PS>
  void conv(T*** input_buf, unsigned input_row, T** output_buf, int num_rows) {
    const unsigned num_cols = st_.output_width;
    const T* range_limit = range_limit_;
    [[maybe_unused]] const int* Crrtab = tables_.Cr_r.data();
    [[maybe_unused]] const int* Cbbtab = tables_.Cb_b.data();
    [[maybe_unused]] const auto* Crgtab = tables_.Cr_g.data();
    [[maybe_unused]] const auto* Cbgtab = tables_.Cb_g.data();
    [[maybe_unused]] const std::uint64_t* Crpack = tables_.Cr_pack.data();
    [[maybe_unused]] const std::uint64_t* Cbpack = tables_.Cb_pack.data();
    while (--num_rows >= 0) {
      const T* inptr0 = input_buf[0][input_row];
      const T* inptr1 = K == GrayRgb ? inptr0 : input_buf[1][input_row];
      const T* inptr2 = K == GrayRgb ? inptr0 : input_buf[2][input_row];
      input_row++;
      T* outptr = *output_buf++;
      unsigned col = 0;
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
      if constexpr (K == GrayRgb && kBits<T> == 8) {
        // On x86, where compilers leave this loop scalar, a 32-bit store
        // per pixel instead of three or four byte stores (stores are what
        // limits it; Arm compilers vectorize it).  With 3-byte pixels each
        // store also writes the next pixel's first byte, which that pixel
        // then overwrites: the last pixel is left to the loop below.
        constexpr int a8 = 8 * (A >= 0 ? A : 0);
        constexpr std::uint32_t alpha = A >= 0 ? std::uint32_t(0xFF) << a8 : 0;
        constexpr std::uint32_t mask = A >= 0 ? ~(std::uint32_t(0xFF) << a8) : ~0u;
        for (const unsigned end = PS == 4 ? num_cols : num_cols - (num_cols > 0); col < end; col++) {
          const std::uint32_t v = (inptr0[col] * 0x01010101u & mask) | alpha;
          std::memcpy(outptr, &v, 4);
          outptr += PS;
        }
      }
#endif
      for (; col < num_cols; col++) {
        if constexpr (K == YccRgb && kBits<T> <= 12 && kVectorizesWide) {
          // The table entries (YccRgbTables), computed directly in 32 bits
          // and clamped, so that the loop vectorizes.
          const int y = inptr0[col], cb = inptr1[col] - kCenterSample<T>,
                    cr = inptr2[col] - kCenterSample<T>;
          auto clamp = [](int v) VIO_LAMBDA_INLINE {
            return static_cast<T>(v < 0 ? 0 : v > kMaxSample<T> ? kMaxSample<T> : v);
          };
          constexpr int half = 1 << (SCALEBITS - 1);
          outptr[R] = clamp(y + ((91881 * cr + half) >> SCALEBITS));
          outptr[G] = clamp(y + ((-22554 * cb - 46802 * cr + half) >> SCALEBITS));
          outptr[B] = clamp(y + ((116130 * cb + half) >> SCALEBITS));
        } else if constexpr (K == YccRgb && kBits<T> == 8) {
          using Tables = YccRgbTables<T>;
          const int y = inptr0[col];
          const std::uint64_t s = Crpack[inptr2[col]] + Cbpack[inptr1[col]];
          outptr[R] = range_limit[y + static_cast<int>(s >> 32 & 0xFFFF) - Tables::kPackBias];
          outptr[G] = range_limit[y + static_cast<int>(static_cast<std::uint32_t>(s) >> 16) -
                                  Tables::kPackGreenBias];
          outptr[B] = range_limit[y + static_cast<int>(s >> 48) - Tables::kPackBias];
        } else if constexpr (K == YccRgb) {
          int y = inptr0[col], cb = inptr1[col], cr = inptr2[col];
          outptr[R] = range_limit[y + Crrtab[cr]];
          outptr[G] = range_limit[y + static_cast<int>((Cbgtab[cb] + Crgtab[cr]) >> SCALEBITS)];
          outptr[B] = range_limit[y + Cbbtab[cb]];
        } else if constexpr (K == GrayRgb) {
          outptr[R] = outptr[G] = outptr[B] = inptr0[col];
        } else {
          outptr[R] = inptr0[col];
          outptr[G] = inptr1[col];
          outptr[B] = inptr2[col];
        }
        if constexpr (A >= 0) outptr[A] = static_cast<T>(kMaxSample<T>);
        outptr += PS;
      }
    }
  }

  void build_rgb_y_table() {
    constexpr int N = kMaxSample<T> + 1;
    rgb_y_tab_.resize(3 * N);
    for (JLong i = 0; i < N; i++) {
      rgb_y_tab_[i] = fix16(0.29900) * i;
      rgb_y_tab_[i + N] = fix16(0.58700) * i;
      rgb_y_tab_[i + 2 * N] = fix16(0.11400) * i + ONE_HALF;
    }
  }

  void rgb_gray_convert(T*** input_buf, unsigned input_row, T** output_buf, int num_rows) {
    constexpr int N = kMaxSample<T> + 1;
    const JLong* ctab = rgb_y_tab_.data();
    unsigned num_cols = st_.output_width;
    while (--num_rows >= 0) {
      const T* inptr0 = input_buf[0][input_row];
      const T* inptr1 = input_buf[1][input_row];
      const T* inptr2 = input_buf[2][input_row];
      input_row++;
      T* outptr = *output_buf++;
      for (unsigned col = 0; col < num_cols; col++) {
        int r = inptr0[col], g = inptr1[col], b = inptr2[col];
        outptr[col] = static_cast<T>((ctab[r] + ctab[g + N] + ctab[b + 2 * N]) >> SCALEBITS);
      }
    }
  }

  void null_convert(T*** input_buf, unsigned input_row, T** output_buf, int num_rows) {
    int num_components = st_.num_components;
    unsigned num_cols = st_.output_width;
    if (num_components == 3) {
      while (--num_rows >= 0) {
        const T* inptr0 = input_buf[0][input_row];
        const T* inptr1 = input_buf[1][input_row];
        const T* inptr2 = input_buf[2][input_row];
        input_row++;
        T* outptr = *output_buf++;
        for (unsigned col = 0; col < num_cols; col++) {
          *outptr++ = inptr0[col];
          *outptr++ = inptr1[col];
          *outptr++ = inptr2[col];
        }
      }
    } else if (num_components == 4) {
      while (--num_rows >= 0) {
        const T* inptr0 = input_buf[0][input_row];
        const T* inptr1 = input_buf[1][input_row];
        const T* inptr2 = input_buf[2][input_row];
        const T* inptr3 = input_buf[3][input_row];
        input_row++;
        T* outptr = *output_buf++;
        for (unsigned col = 0; col < num_cols; col++) {
          *outptr++ = inptr0[col];
          *outptr++ = inptr1[col];
          *outptr++ = inptr2[col];
          *outptr++ = inptr3[col];
        }
      }
    } else {
      while (--num_rows >= 0) {
        for (int ci = 0; ci < num_components; ci++) {
          const T* inptr = input_buf[ci][input_row];
          T* outptr = *output_buf;
          for (unsigned col = 0; col < num_cols; col++) {
            outptr[ci] = inptr[col];
            outptr += num_components;
          }
        }
        output_buf++;
        input_row++;
      }
    }
  }

  void grayscale_convert(T*** input_buf, unsigned input_row, T** output_buf, int num_rows) {
    copy_sample_rows(input_buf[0], static_cast<int>(input_row), output_buf, 0, num_rows,
                     st_.output_width);
  }

  void ycck_cmyk_convert(T*** input_buf, unsigned input_row, T** output_buf, int num_rows) {
    const T* range_limit = range_limit_;
    const int* Crrtab = tables_.Cr_r.data();
    const int* Cbbtab = tables_.Cb_b.data();
    const auto* Crgtab = tables_.Cr_g.data();
    const auto* Cbgtab = tables_.Cb_g.data();
    unsigned num_cols = st_.output_width;
    while (--num_rows >= 0) {
      const T* inptr0 = input_buf[0][input_row];
      const T* inptr1 = input_buf[1][input_row];
      const T* inptr2 = input_buf[2][input_row];
      const T* inptr3 = input_buf[3][input_row];
      input_row++;
      T* outptr = *output_buf++;
      for (unsigned col = 0; col < num_cols; col++) {
        int y = inptr0[col], cb = inptr1[col], cr = inptr2[col];
        outptr[0] = range_limit[kMaxSample<T> - (y + Crrtab[cr])];
        outptr[1] = range_limit[kMaxSample<T> -
                                (y + static_cast<int>((Cbgtab[cb] + Crgtab[cr]) >> SCALEBITS))];
        outptr[2] = range_limit[kMaxSample<T> - (y + Cbbtab[cb])];
        outptr[3] = inptr3[col];
        outptr += 4;
      }
    }
  }

  // ---- RGB565 (8-bit only).  Output rows hold 2 bytes per pixel.  An
  // unaligned row start emits one pixel separately and the dither pattern
  // advances only within pixel pairs.
  template <class PixelFn>
  void emit565(T* outbase, unsigned num_cols, bool dither, JLong& d0, PixelFn&& pixel) {
    auto* outptr = reinterpret_cast<std::uint8_t*>(outbase);
    auto put = [&](unsigned v) {
      std::uint16_t s = static_cast<std::uint16_t>(v);
      std::memcpy(outptr, &s, 2);
      outptr += 2;
    };
    unsigned col = 0;
    if (reinterpret_cast<std::uintptr_t>(outptr) & 3) {
      put(pixel(col++, d0));
      num_cols--;
    }
    for (unsigned i = 0; i < (num_cols >> 1); i++) {
      put(pixel(col++, d0));
      if (dither) d0 = dither_rotate(d0);
      put(pixel(col++, d0));
      if (dither) d0 = dither_rotate(d0);
    }
    if (num_cols & 1) put(pixel(col, d0));
  }

  template <bool D>
  void ycc565(T*** input_buf, unsigned input_row, T** output_buf, int num_rows) {
    const T* range_limit = range_limit_;
    const int* Crrtab = tables_.Cr_r.data();
    const int* Cbbtab = tables_.Cb_b.data();
    const auto* Crgtab = tables_.Cr_g.data();
    const auto* Cbgtab = tables_.Cb_g.data();
    JLong d0 = dither_matrix_565[st_.output_scanline & 3];
    while (--num_rows >= 0) {
      const T* in0 = input_buf[0][input_row];
      const T* in1 = input_buf[1][input_row];
      const T* in2 = input_buf[2][input_row];
      input_row++;
      emit565(*output_buf++, st_.output_width, D, d0, [&](unsigned c, JLong d) {
        int y = in0[c], cb = in1[c], cr = in2[c];
        int rr = y + Crrtab[cr];
        int gg = y + static_cast<int>((Cbgtab[cb] + Crgtab[cr]) >> SCALEBITS);
        int bb = y + Cbbtab[cb];
        if constexpr (D) {
          rr += static_cast<int>(d & 0xFF);
          gg += static_cast<int>((d & 0xFF) >> 1);
          bb += static_cast<int>(d & 0xFF);
        }
        return pack565(range_limit[rr], range_limit[gg], range_limit[bb]);
      });
    }
  }
  void ycc_rgb565_convert(T*** i, unsigned r, T** o, int n) { ycc565<false>(i, r, o, n); }
  void ycc_rgb565D_convert(T*** i, unsigned r, T** o, int n) { ycc565<true>(i, r, o, n); }

  template <bool D>
  void rgb565(T*** input_buf, unsigned input_row, T** output_buf, int num_rows) {
    const T* range_limit = range_limit_;
    JLong d0 = dither_matrix_565[st_.output_scanline & 3];
    while (--num_rows >= 0) {
      const T* in0 = input_buf[0][input_row];
      const T* in1 = input_buf[1][input_row];
      const T* in2 = input_buf[2][input_row];
      input_row++;
      emit565(*output_buf++, st_.output_width, D, d0, [&](unsigned c, JLong d) {
        if constexpr (D)
          return pack565(range_limit[in0[c] + (d & 0xFF)], range_limit[in1[c] + ((d & 0xFF) >> 1)],
                         range_limit[in2[c] + (d & 0xFF)]);
        else
          return pack565(in0[c], in1[c], in2[c]);
      });
    }
  }
  void rgb_rgb565_convert(T*** i, unsigned r, T** o, int n) { rgb565<false>(i, r, o, n); }
  void rgb_rgb565D_convert(T*** i, unsigned r, T** o, int n) { rgb565<true>(i, r, o, n); }

  template <bool D>
  void gray565(T*** input_buf, unsigned input_row, T** output_buf, int num_rows) {
    const T* range_limit = range_limit_;
    JLong d0 = dither_matrix_565[st_.output_scanline & 3];
    while (--num_rows >= 0) {
      const T* in = input_buf[0][input_row++];
      emit565(*output_buf++, st_.output_width, D, d0, [&](unsigned c, JLong d) {
        unsigned g = in[c];
        if constexpr (D) g = range_limit[g + (d & 0xFF)];
        return pack565(g, g, g);
      });
    }
  }
  void gray_rgb565_convert(T*** i, unsigned r, T** o, int n) { gray565<false>(i, r, o, n); }
  void gray_rgb565D_convert(T*** i, unsigned r, T** o, int n) { gray565<true>(i, r, o, n); }

  DState& st_;
  const T* range_limit_;
  YccRgbTables<T> tables_;
  std::vector<JLong> rgb_y_tab_;
  ConvertFn convert_ = nullptr;
  using SimdFn = void (*)(unsigned, T***, unsigned, T**, int);
  SimdFn simd_ = nullptr;
};

}  // namespace vio::jpeg::detail
