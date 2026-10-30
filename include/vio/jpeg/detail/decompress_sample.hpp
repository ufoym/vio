// vio::jpeg - upsampling (separate and merged), main buffer controller and
// post-processing controller.
#pragma once

#include "decompress_coef.hpp"
#include "decompress_color.hpp"

namespace vio::jpeg::detail {

template <class T>
class Upsampler {
 public:
  virtual ~Upsampler() = default;
  virtual void start_pass() = 0;
  virtual void upsample(T*** input_buf, unsigned* in_row_group_ctr, unsigned in_row_groups_avail,
                        T** output_buf, unsigned* out_row_ctr, unsigned out_rows_avail) = 0;
  bool need_context_rows = false;
};

template <class T>
class ColorQuantizer {
 public:
  virtual ~ColorQuantizer() = default;
  virtual void start_pass(bool is_pre_scan) = 0;
  virtual void color_quantize(T** input_buf, T** output_buf, int num_rows) = 0;
  virtual void finish_pass() {}
  virtual void new_color_map() {}
  bool noop = false;  // set while discarding scanlines
};

// Fancy upsampling of 8-bit samples four at a time in 16-bit lanes of a
// 64-bit word, where per-sample loops are not vectorized (the lanes never
// carry: sums stay below 4096).  Little-endian only: the even and odd
// outputs of a lane are then adjacent bytes.
namespace swar {
template <class T>
inline constexpr bool kFancy =
    !kVectorizesWide && kBits<T> == 8 && std::endian::native == std::endian::little;
inline constexpr std::uint64_t kLanes = 0x0001000100010001, kLow8 = 0x00FF00FF00FF00FF;

// Samples p[0..3] in 16-bit lanes (the rows are padded: reading past the
// end is safe).
template <class T>
VIO_INLINE std::uint64_t load4(const T* p) {
  std::uint32_t x;
  std::memcpy(&x, p, 4);
  std::uint64_t y = x;
  y = (y | y << 16) & 0x0000FFFF0000FFFF;
  return (y | y << 8) & kLow8;
}
// Writes the low bytes of the lanes of even and odd interleaved.
template <class T>
VIO_INLINE void store8(T* p, std::uint64_t even, std::uint64_t odd) {
  const std::uint64_t v = even | odd << 8;
  std::memcpy(p, &v, 8);
}
}  // namespace swar

// ---------------------------------------------------------------------------
// Separate upsampling + color conversion
template <class T>
class SepUpsampler final : public Upsampler<T> {
 public:
  using MethodFn = void (SepUpsampler::*)(const ComponentInfo&, T**, T**&);

  SepUpsampler(DState& st, ColorDeconverter<T>* cconvert) : st_(st), cconvert_(cconvert) {
    init(true);
  }

  // _jinit_upsampler (also re-run by crop_scanline with alloc == false)
  void init(bool alloc) {
    if (st_.CCIR601_sampling) VIO_FAIL("CCIR601 sampling not implemented yet");
    if (alloc) {
      this->need_context_rows = false;
      color_buf_.resize(st_.num_components);
      color_ptr_.assign(st_.num_components, nullptr);
    }
    bool do_fancy = st_.do_fancy_upsampling && st_.min_DCT_scaled_size > 1;
    for (int ci = 0; ci < st_.num_components; ci++) {
      ComponentInfo& c = st_.comp_info[ci];
      int h_in_group = (c.h_samp_factor * c.DCT_scaled_size) / st_.min_DCT_scaled_size;
      int v_in_group = (c.v_samp_factor * c.DCT_scaled_size) / st_.min_DCT_scaled_size;
      int h_out_group = st_.max_h_samp_factor;
      int v_out_group = st_.max_v_samp_factor;
      rowgroup_height_[ci] = v_in_group;
      bool need_buffer = true;
      simd_[ci] = nullptr;
      if (!c.component_needed) {
        methods_[ci] = &SepUpsampler::noop_upsample;
        need_buffer = false;
      } else if (h_in_group == h_out_group && v_in_group == v_out_group) {
        methods_[ci] = &SepUpsampler::fullsize_upsample;
        need_buffer = false;
      } else if (h_in_group * 2 == h_out_group && v_in_group == v_out_group) {
        if (do_fancy && c.downsampled_width > 2) {
          methods_[ci] = h2v1_fancy;
          set_simd(ci, simd::upsample_kernel(st_.simd, simd::Upsample::H2V1Fancy), true);
        } else {
          methods_[ci] = h2v1;
          set_simd(ci, simd::upsample_kernel(st_.simd, simd::Upsample::H2V1), false);
        }
      } else if (h_in_group == h_out_group && v_in_group * 2 == v_out_group && do_fancy) {
        methods_[ci] = h1v2_fancy;
        set_simd(ci, simd::upsample_kernel(st_.simd, simd::Upsample::H1V2Fancy), true);
        this->need_context_rows = true;
      } else if (h_in_group * 2 == h_out_group && v_in_group * 2 == v_out_group) {
        if (do_fancy && c.downsampled_width > 2) {
          methods_[ci] = h2v2_fancy;
          set_simd(ci, simd::upsample_kernel(st_.simd, simd::Upsample::H2V2Fancy), true);
          this->need_context_rows = true;
        } else {
          methods_[ci] = h2v2;
          set_simd(ci, simd::upsample_kernel(st_.simd, simd::Upsample::H2V2), false);
        }
      } else if ((h_out_group % h_in_group) == 0 && (v_out_group % v_in_group) == 0) {
        methods_[ci] = &SepUpsampler::int_upsample;
        h_expand_[ci] = static_cast<std::uint8_t>(h_out_group / h_in_group);
        v_expand_[ci] = static_cast<std::uint8_t>(v_out_group / v_in_group);
      } else {
        VIO_FAIL("Fractional sampling not implemented yet");
      }
      if (need_buffer && alloc)
        color_buf_[ci].allocate(
            static_cast<std::size_t>(round_up(st_.output_width, st_.max_h_samp_factor)),
            static_cast<std::size_t>(st_.max_v_samp_factor));
      if (need_buffer && !color_buf_[ci].empty()) color_ptr_[ci] = color_buf_[ci].rows();
    }
  }

  void start_pass() override {
    next_row_out = st_.max_v_samp_factor;
    rows_to_go = st_.output_height;
  }

  void upsample(T*** input_buf, unsigned* in_row_group_ctr, unsigned /*in_row_groups_avail*/,
                T** output_buf, unsigned* out_row_ctr, unsigned out_rows_avail) override {
    if (next_row_out >= st_.max_v_samp_factor) {
      for (int ci = 0; ci < st_.num_components; ci++) {
        T** in = input_buf[ci] + (*in_row_group_ctr * rowgroup_height_[ci]);
        if constexpr (kBits<T> == 8) {
          if (simd_[ci]) {
            simd_[ci](st_.max_v_samp_factor,
                      simd_fancy_[ci] ? st_.comp_info[ci].downsampled_width : st_.output_width, in,
                      color_ptr_[ci]);
            continue;
          }
        }
        (this->*methods_[ci])(st_.comp_info[ci], in, color_ptr_[ci]);
      }
      next_row_out = 0;
    }
    unsigned num_rows = static_cast<unsigned>(st_.max_v_samp_factor - next_row_out);
    if (num_rows > rows_to_go) num_rows = rows_to_go;
    out_rows_avail -= *out_row_ctr;
    if (num_rows > out_rows_avail) num_rows = out_rows_avail;
    if (!convert_noop)
      cconvert_->color_convert(color_ptr_.data(), static_cast<unsigned>(next_row_out),
                               output_buf + *out_row_ctr, static_cast<int>(num_rows));
    *out_row_ctr += num_rows;
    rows_to_go -= num_rows;
    next_row_out += static_cast<int>(num_rows);
    if (next_row_out >= st_.max_v_samp_factor) (*in_row_group_ctr)++;
  }

  int next_row_out = 0;
  unsigned rows_to_go = 0;
  bool convert_noop = false;

  // Replaceable by SIMD back ends.
  MethodFn h2v1 = &SepUpsampler::h2v1_upsample;
  MethodFn h2v2 = &SepUpsampler::h2v2_upsample;
  MethodFn h2v1_fancy = &SepUpsampler::h2v1_fancy_upsample;
  MethodFn h2v2_fancy = &SepUpsampler::h2v2_fancy_upsample;
  MethodFn h1v2_fancy = &SepUpsampler::h1v2_fancy_upsample;

 private:
  using SimdFn = void (*)(int, unsigned, T**, T**);
  SimdFn simd_[MAX_COMPONENTS]{};
  bool simd_fancy_[MAX_COMPONENTS]{};
  template <class F>
  void set_simd(int ci, F f, bool fancy) {
    if constexpr (kBits<T> == 8) {
      simd_[ci] = f;
      simd_fancy_[ci] = fancy;
    }
  }

  void fullsize_upsample(const ComponentInfo&, T** input_data, T**& output_data_ptr) {
    output_data_ptr = input_data;
  }
  void noop_upsample(const ComponentInfo&, T**, T**& output_data_ptr) { output_data_ptr = nullptr; }

  void int_upsample(const ComponentInfo& c, T** input_data, T**& output_data_ptr) {
    T** output_data = output_data_ptr;
    int h_expand = h_expand_[c.component_index];
    int v_expand = v_expand_[c.component_index];
    int inrow = 0, outrow = 0;
    while (outrow < st_.max_v_samp_factor) {
      const T* inptr = input_data[inrow];
      T* outptr = output_data[outrow];
      T* outend = outptr + st_.output_width;
      while (outptr < outend) {
        T invalue = *inptr++;
        for (int h = h_expand; h > 0; h--) *outptr++ = invalue;
      }
      if (v_expand > 1)
        copy_sample_rows(output_data, outrow, output_data, outrow + 1, v_expand - 1,
                         st_.output_width);
      inrow++;
      outrow += v_expand;
    }
  }

 public:
  void h2v1_upsample(const ComponentInfo&, T** input_data, T**& output_data_ptr) {
    T** output_data = output_data_ptr;
    for (int inrow = 0; inrow < st_.max_v_samp_factor; inrow++) {
      const T* inptr = input_data[inrow];
      T* outptr = output_data[inrow];
      T* outend = outptr + st_.output_width;
      while (outptr < outend) {
        T invalue = *inptr++;
        *outptr++ = invalue;
        *outptr++ = invalue;
      }
    }
  }

  void h2v2_upsample(const ComponentInfo&, T** input_data, T**& output_data_ptr) {
    T** output_data = output_data_ptr;
    int inrow = 0, outrow = 0;
    while (outrow < st_.max_v_samp_factor) {
      const T* inptr = input_data[inrow];
      T* outptr = output_data[outrow];
      T* outend = outptr + st_.output_width;
      while (outptr < outend) {
        T invalue = *inptr++;
        *outptr++ = invalue;
        *outptr++ = invalue;
      }
      copy_sample_rows(output_data, outrow, output_data, outrow + 1, 1, st_.output_width);
      inrow++;
      outrow += 2;
    }
  }

  // Fancy upsampling.  Each output sample is computed on its own where the
  // compiler vectorizes the loops well; running sums do less work otherwise.
  void h2v1_fancy_upsample(const ComponentInfo& c, T** input_data, T**& output_data_ptr) {
    if constexpr (kVectorizesWide) {
      T** output_data = output_data_ptr;
      const unsigned w = c.downsampled_width;
      for (int inrow = 0; inrow < st_.max_v_samp_factor; inrow++) {
        const T* in = input_data[inrow];
        T* out = output_data[inrow];
        out[0] = in[0];
        out[1] = static_cast<T>((in[0] * 3 + in[1] + 2) >> 2);
        for (unsigned i = 1; i < w - 1; i++) {
          const int v = in[i] * 3;
          out[2 * i] = static_cast<T>((v + in[i - 1] + 1) >> 2);
          out[2 * i + 1] = static_cast<T>((v + in[i + 1] + 2) >> 2);
        }
        out[2 * w - 2] = static_cast<T>((in[w - 1] * 3 + in[w - 2] + 1) >> 2);
        out[2 * w - 1] = in[w - 1];
      }
    } else if constexpr (swar::kFancy<T> && !kVectorizes) {
      // (GCC and Clang do better with the running sums below.)
      using namespace swar;
      T** output_data = output_data_ptr;
      const unsigned w = c.downsampled_width;
      for (int inrow = 0; inrow < st_.max_v_samp_factor; inrow++) {
        const T* in = input_data[inrow];
        T* out = output_data[inrow];
        out[0] = in[0];
        out[1] = static_cast<T>((in[0] * 3 + in[1] + 2) >> 2);
        unsigned i = 1;
        std::uint64_t prev = in[0], cur = load4(in + 1);  // in[i - 1], in[i..i + 3]
        for (; i + 5 <= w; i += 4) {
          const std::uint64_t next = load4(in + i + 4), t = cur * 3;
          store8(out + 2 * i, ((t + (cur << 16 | prev) + kLanes) >> 2) & kLow8,
                 ((t + (cur >> 16 | next << 48) + 2 * kLanes) >> 2) & kLow8);
          prev = cur >> 48;
          cur = next;
        }
        for (; i < w - 1; i++) {
          const int v = in[i] * 3;
          out[2 * i] = static_cast<T>((v + in[i - 1] + 1) >> 2);
          out[2 * i + 1] = static_cast<T>((v + in[i + 1] + 2) >> 2);
        }
        out[2 * w - 2] = static_cast<T>((in[w - 1] * 3 + in[w - 2] + 1) >> 2);
        out[2 * w - 1] = in[w - 1];
      }
    } else {  // running sums
      T** output_data = output_data_ptr;
      for (int inrow = 0; inrow < st_.max_v_samp_factor; inrow++) {
        const T* inptr = input_data[inrow];
        T* outptr = output_data[inrow];
        int invalue = *inptr++;
        *outptr++ = static_cast<T>(invalue);
        *outptr++ = static_cast<T>((invalue * 3 + inptr[0] + 2) >> 2);
        for (unsigned colctr = c.downsampled_width - 2; colctr > 0; colctr--) {
          invalue = (*inptr++) * 3;
          *outptr++ = static_cast<T>((invalue + inptr[-2] + 1) >> 2);
          *outptr++ = static_cast<T>((invalue + inptr[0] + 2) >> 2);
        }
        invalue = *inptr;
        *outptr++ = static_cast<T>((invalue * 3 + inptr[-1] + 1) >> 2);
        *outptr++ = static_cast<T>(invalue);
      }
    }
  }

  void h1v2_fancy_upsample(const ComponentInfo& c, T** input_data, T**& output_data_ptr) {
    T** output_data = output_data_ptr;
    int inrow = 0, outrow = 0;
    while (outrow < st_.max_v_samp_factor) {
      for (int v = 0; v < 2; v++) {
        const T* inptr0 = input_data[inrow];
        const T* inptr1 = input_data[inrow + (v == 0 ? -1 : 1)];
        JLong bias = v + 1;
        T* outptr = output_data[outrow++];
        for (unsigned colctr = 0; colctr < c.downsampled_width; colctr++) {
          JLong thiscolsum = JLong(*inptr0++) * 3 + *inptr1++;
          *outptr++ = static_cast<T>((thiscolsum + bias) >> 2);
        }
      }
      inrow++;
    }
  }

  void h2v2_fancy_upsample(const ComponentInfo& c, T** input_data, T**& output_data_ptr) {
    if constexpr (kVectorizesWide) {
      T** output_data = output_data_ptr;
      const unsigned w = c.downsampled_width;
      int inrow = 0, outrow = 0;
      while (outrow < st_.max_v_samp_factor) {
        for (int v = 0; v < 2; v++) {
          const T* in0 = input_data[inrow];
          const T* in1 = v == 0 ? input_data[inrow - 1] : input_data[inrow + 1];
          T* out = output_data[outrow++];
          // Column sums of the vertical pass.
          auto col = [&](unsigned i) VIO_LAMBDA_INLINE { return in0[i] * 3 + in1[i]; };
          out[0] = static_cast<T>((col(0) * 4 + 8) >> 4);
          out[1] = static_cast<T>((col(0) * 3 + col(1) + 7) >> 4);
          for (unsigned i = 1; i < w - 1; i++) {
            const int t = col(i) * 3;
            out[2 * i] = static_cast<T>((t + col(i - 1) + 8) >> 4);
            out[2 * i + 1] = static_cast<T>((t + col(i + 1) + 7) >> 4);
          }
          out[2 * w - 2] = static_cast<T>((col(w - 1) * 3 + col(w - 2) + 8) >> 4);
          out[2 * w - 1] = static_cast<T>((col(w - 1) * 4 + 7) >> 4);
        }
        inrow++;
      }
    } else if constexpr (swar::kFancy<T>) {
      using namespace swar;
      T** output_data = output_data_ptr;
      const unsigned w = c.downsampled_width;
      int inrow = 0, outrow = 0;
      while (outrow < st_.max_v_samp_factor) {
        for (int v = 0; v < 2; v++) {
          const T* in0 = input_data[inrow];
          const T* in1 = v == 0 ? input_data[inrow - 1] : input_data[inrow + 1];
          T* out = output_data[outrow++];
          // Column sums of the vertical pass.
          auto col = [&](unsigned i) VIO_LAMBDA_INLINE { return in0[i] * 3 + in1[i]; };
          out[0] = static_cast<T>((col(0) * 4 + 8) >> 4);
          out[1] = static_cast<T>((col(0) * 3 + col(1) + 7) >> 4);
          unsigned i = 1;
          // col(i - 1), col(i..i + 3)
          std::uint64_t prev = static_cast<unsigned>(col(0)),
                        cur = load4(in0 + 1) * 3 + load4(in1 + 1);
          for (; i + 5 <= w; i += 4) {
            const std::uint64_t next = load4(in0 + i + 4) * 3 + load4(in1 + i + 4), t = cur * 3;
            store8(out + 2 * i, ((t + (cur << 16 | prev) + 8 * kLanes) >> 4) & kLow8,
                   ((t + (cur >> 16 | next << 48) + 7 * kLanes) >> 4) & kLow8);
            prev = cur >> 48;
            cur = next;
          }
          for (; i < w - 1; i++) {
            const int t = col(i) * 3;
            out[2 * i] = static_cast<T>((t + col(i - 1) + 8) >> 4);
            out[2 * i + 1] = static_cast<T>((t + col(i + 1) + 7) >> 4);
          }
          out[2 * w - 2] = static_cast<T>((col(w - 1) * 3 + col(w - 2) + 8) >> 4);
          out[2 * w - 1] = static_cast<T>((col(w - 1) * 4 + 7) >> 4);
        }
        inrow++;
      }
    } else {  // running sums
      T** output_data = output_data_ptr;
      int inrow = 0, outrow = 0;
      while (outrow < st_.max_v_samp_factor) {
        for (int v = 0; v < 2; v++) {
          const T* inptr0 = input_data[inrow];
          const T* inptr1 = v == 0 ? input_data[inrow - 1] : input_data[inrow + 1];
          T* outptr = output_data[outrow++];
          JLong thiscolsum = JLong(*inptr0++) * 3 + *inptr1++;
          JLong nextcolsum = JLong(*inptr0++) * 3 + *inptr1++;
          *outptr++ = static_cast<T>((thiscolsum * 4 + 8) >> 4);
          *outptr++ = static_cast<T>((thiscolsum * 3 + nextcolsum + 7) >> 4);
          JLong lastcolsum = thiscolsum;
          thiscolsum = nextcolsum;
          for (unsigned colctr = c.downsampled_width - 2; colctr > 0; colctr--) {
            nextcolsum = JLong(*inptr0++) * 3 + *inptr1++;
            *outptr++ = static_cast<T>((thiscolsum * 3 + lastcolsum + 8) >> 4);
            *outptr++ = static_cast<T>((thiscolsum * 3 + nextcolsum + 7) >> 4);
            lastcolsum = thiscolsum;
            thiscolsum = nextcolsum;
          }
          *outptr++ = static_cast<T>((thiscolsum * 3 + lastcolsum + 8) >> 4);
          *outptr++ = static_cast<T>((thiscolsum * 4 + 7) >> 4);
        }
        inrow++;
      }
    }
  }

 private:
  DState& st_;
  ColorDeconverter<T>* cconvert_;
  std::vector<SampleArray<T>> color_buf_;
  std::vector<T**> color_ptr_;
  MethodFn methods_[MAX_COMPONENTS]{};
  int rowgroup_height_[MAX_COMPONENTS]{};
  std::uint8_t h_expand_[MAX_COMPONENTS]{};
  std::uint8_t v_expand_[MAX_COMPONENTS]{};
};

// ---------------------------------------------------------------------------
// Merged upsampling + YCbCr->RGB conversion
template <class T>
class MergedUpsampler final : public Upsampler<T> {
 public:
  using MethodFn = void (MergedUpsampler::*)(T***, unsigned, T**);

  MergedUpsampler(DState& st, const T* range_limit) : st_(st), range_limit_(range_limit) {
    this->need_context_rows = false;
    out_row_width = st.output_width * static_cast<unsigned>(st.out_color_components);
    bool is565 = st.out_color_space == ColorSpace::RGB565;
    if constexpr (kBits<T> != 8) {
      if (is565) VIO_FAIL("Unsupported color conversion request");
    }
    bool dither = st.dither_mode != DitherMode::None;
    if (st.max_v_samp_factor == 2) {
      two_v_ = true;
      upmethod = is565 ? (dither ? &MergedUpsampler::h2v_565<true, true>
                                 : &MergedUpsampler::h2v_565<true, false>)
                       : select<true>(st.out_color_space);
      spare_.allocate(std::max<std::size_t>(out_row_width, st.output_width * 2), 1);
      spare_row = spare_[0];
    } else {
      two_v_ = false;
      upmethod = is565 ? (dither ? &MergedUpsampler::h2v_565<false, true>
                                 : &MergedUpsampler::h2v_565<false, false>)
                       : select<false>(st.out_color_space);
      spare_row = nullptr;
    }
    tables_.build();
    if constexpr (kBits<T> == 8) {
      if (!is565) simd_ = simd::merged_upsample_kernel(st.simd, two_v_, st.out_color_space);
    }
  }

  void start_pass() override {
    spare_full = false;
    rows_to_go = st_.output_height;
  }

  void upsample(T*** input_buf, unsigned* in_row_group_ctr, unsigned in_row_groups_avail,
                T** output_buf, unsigned* out_row_ctr, unsigned out_rows_avail) override {
    if (two_v_) merged_2v(input_buf, in_row_group_ctr, in_row_groups_avail, output_buf, out_row_ctr,
                          out_rows_avail);
    else merged_1v(input_buf, in_row_group_ctr, output_buf, out_row_ctr);
  }

  T* spare_row = nullptr;
  bool spare_full = false;
  unsigned out_row_width = 0;
  unsigned rows_to_go = 0;
  MethodFn upmethod = nullptr;
  const YccRgbTables<T>& tables() const { return tables_; }

 private:
  void merged_2v(T*** input_buf, unsigned* in_row_group_ctr, unsigned, T** output_buf,
                 unsigned* out_row_ctr, unsigned out_rows_avail) {
    T* work_ptrs[2];
    unsigned num_rows;
    if (spare_full) {
      std::size_t size = out_row_width;
      if (st_.out_color_space == ColorSpace::RGB565) size = st_.output_width * 2;
      std::memcpy(output_buf[*out_row_ctr], spare_row, size * sizeof(T));
      num_rows = 1;
      spare_full = false;
    } else {
      num_rows = 2;
      if (num_rows > rows_to_go) num_rows = rows_to_go;
      out_rows_avail -= *out_row_ctr;
      if (num_rows > out_rows_avail) num_rows = out_rows_avail;
      work_ptrs[0] = output_buf[*out_row_ctr];
      if (num_rows > 1) {
        work_ptrs[1] = output_buf[*out_row_ctr + 1];
      } else {
        work_ptrs[1] = spare_row;
        spare_full = true;
      }
      run(input_buf, *in_row_group_ctr, work_ptrs);
    }
    *out_row_ctr += num_rows;
    rows_to_go -= num_rows;
    if (!spare_full) (*in_row_group_ctr)++;
  }

  void run(T*** input_buf, unsigned in_row_group_ctr, T** output_buf) {
    if constexpr (kBits<T> == 8) {
      if (simd_) return simd_(st_.output_width, input_buf, in_row_group_ctr, output_buf);
    }
    (this->*upmethod)(input_buf, in_row_group_ctr, output_buf);
  }

  void merged_1v(T*** input_buf, unsigned* in_row_group_ctr, T** output_buf,
                 unsigned* out_row_ctr) {
    run(input_buf, *in_row_group_ctr, output_buf + *out_row_ctr);
    (*out_row_ctr)++;
    (*in_row_group_ctr)++;
  }

  template <bool V2>
  MethodFn select(ColorSpace out) {
    return select_rgb_layout(out, []<int R, int G, int B, int A, int PS> {
      return &MergedUpsampler::merged<V2, R, G, B, A, PS>;
    });
  }

 public:
  template <bool V2, int R, int G, int B, int A, int PS>
  void merged(T*** input_buf, unsigned in_row_group_ctr, T** output_buf) {
    const T* range_limit = range_limit_;
    const int* Crrtab = tables_.Cr_r.data();
    const int* Cbbtab = tables_.Cb_b.data();
    const auto* Crgtab = tables_.Cr_g.data();
    const auto* Cbgtab = tables_.Cb_g.data();
    const T* inptr00 = input_buf[0][V2 ? in_row_group_ctr * 2 : in_row_group_ctr];
    const T* inptr01 = V2 ? input_buf[0][in_row_group_ctr * 2 + 1] : nullptr;
    const T* inptr1 = input_buf[1][in_row_group_ctr];
    const T* inptr2 = input_buf[2][in_row_group_ctr];
    T* outptr0 = output_buf[0];
    T* outptr1 = V2 ? output_buf[1] : nullptr;
    auto put = [&](T*& o, int y, int cred, int cgreen, int cblue) {
      o[R] = range_limit[y + cred];
      o[G] = range_limit[y + cgreen];
      o[B] = range_limit[y + cblue];
      if constexpr (A >= 0) o[A] = static_cast<T>(kMaxSample<T>);
      o += PS;
    };
    for (unsigned col = st_.output_width >> 1; col > 0; col--) {
      int cb = *inptr1++, cr = *inptr2++;
      int cred = Crrtab[cr];
      int cgreen = static_cast<int>((Cbgtab[cb] + Crgtab[cr]) >> SCALEBITS);
      int cblue = Cbbtab[cb];
      put(outptr0, *inptr00++, cred, cgreen, cblue);
      put(outptr0, *inptr00++, cred, cgreen, cblue);
      if constexpr (V2) {
        put(outptr1, *inptr01++, cred, cgreen, cblue);
        put(outptr1, *inptr01++, cred, cgreen, cblue);
      }
    }
    if (st_.output_width & 1) {
      int cb = *inptr1, cr = *inptr2;
      int cred = Crrtab[cr];
      int cgreen = static_cast<int>((Cbgtab[cb] + Crgtab[cr]) >> SCALEBITS);
      int cblue = Cbbtab[cb];
      put(outptr0, *inptr00, cred, cgreen, cblue);
      if constexpr (V2) put(outptr1, *inptr01, cred, cgreen, cblue);
    }
  }

 private:
  template <bool D>
  void row565(const T* iny, const T* in1, const T* in2, std::uint8_t* out, JLong d) {
    const T* range_limit = range_limit_;
    const int* Crrtab = tables_.Cr_r.data();
    const int* Cbbtab = tables_.Cb_b.data();
    const auto* Crgtab = tables_.Cr_g.data();
    const auto* Cbgtab = tables_.Cb_g.data();
    auto pix = [&](int y, int cred, int cgreen, int cblue) {
      if constexpr (D) {
        unsigned r = range_limit[y + cred + (d & 0xFF)];
        unsigned g = range_limit[y + cgreen + ((d & 0xFF) >> 1)];
        unsigned b = range_limit[y + cblue + (d & 0xFF)];
        d = dither_rotate(d);
        return pack565(r, g, b);
      } else {
        return pack565(range_limit[y + cred], range_limit[y + cgreen], range_limit[y + cblue]);
      }
    };
    auto put = [&](unsigned v) {
      out[0] = static_cast<std::uint8_t>(v);
      out[1] = static_cast<std::uint8_t>(v >> 8);
      out += 2;
    };
    for (unsigned col = st_.output_width >> 1; col > 0; col--) {
      int cb = *in1++, cr = *in2++;
      int cred = Crrtab[cr];
      int cgreen = static_cast<int>((Cbgtab[cb] + Crgtab[cr]) >> SCALEBITS);
      int cblue = Cbbtab[cb];
      put(pix(*iny++, cred, cgreen, cblue));
      put(pix(*iny++, cred, cgreen, cblue));
    }
    if (st_.output_width & 1) {
      int cb = *in1, cr = *in2;
      int cred = Crrtab[cr];
      int cgreen = static_cast<int>((Cbgtab[cb] + Crgtab[cr]) >> SCALEBITS);
      int cblue = Cbbtab[cb];
      put(pix(*iny, cred, cgreen, cblue));
    }
  }

  template <bool V2, bool D>
  void h2v_565(T*** input_buf, unsigned g, T** output_buf) {
    row565<D>(input_buf[0][V2 ? g * 2 : g], input_buf[1][g], input_buf[2][g],
              reinterpret_cast<std::uint8_t*>(output_buf[0]),
              dither_matrix_565[st_.output_scanline & 3]);
    if constexpr (V2)
      row565<D>(input_buf[0][g * 2 + 1], input_buf[1][g], input_buf[2][g],
                reinterpret_cast<std::uint8_t*>(output_buf[1]),
                dither_matrix_565[(st_.output_scanline + 1) & 3]);
  }

  DState& st_;
  const T* range_limit_;
  YccRgbTables<T> tables_;
  simd::MergedUpsampleFn simd_ = nullptr;
  SampleArray<T> spare_;
  bool two_v_ = false;
};

// ---------------------------------------------------------------------------
// Post-processing controller
template <class T>
class PostController {
 public:
  enum class Mode { PassThru, SaveAndPass, CrankDest };

  PostController(DState& st, bool need_full_buffer) : st_(st) {
    if (st.quantize_colors) {
      if constexpr (kBits<T> == 16) VIO_FAIL("Requested features are incompatible");
      strip_height_ = static_cast<unsigned>(st.max_v_samp_factor);
      std::size_t w = static_cast<std::size_t>(st.output_width) * st.out_color_components;
      if (need_full_buffer) {
        whole_image_.allocate(w, static_cast<std::size_t>(
                                     round_up(st.output_height, strip_height_)));
      } else {
        strip_.allocate(w, strip_height_);
        buffer_ = strip_.rows();
      }
    }
  }

  void start_pass(Mode mode, Upsampler<T>* up, ColorQuantizer<T>* cq) {
    up_ = up;
    cq_ = cq;
    mode_ = mode;
    switch (mode) {
      case Mode::PassThru:
        if (st_.quantize_colors) {
          if (!buffer_) buffer_ = whole_image_.rows();
        }
        break;
      case Mode::SaveAndPass:
      case Mode::CrankDest:
        if (whole_image_.empty()) VIO_FAIL("Bogus buffer control mode");
        break;
    }
    starting_row_ = next_row_ = 0;
  }

  void post_process_data(T*** input_buf, unsigned* in_row_group_ctr,
                         unsigned in_row_groups_avail, T** output_buf, unsigned* out_row_ctr,
                         unsigned out_rows_avail) {
    switch (mode_) {
      case Mode::PassThru:
        if (!st_.quantize_colors) {
          up_->upsample(input_buf, in_row_group_ctr, in_row_groups_avail, output_buf,
                        out_row_ctr, out_rows_avail);
        } else {
          unsigned max_rows = out_rows_avail - *out_row_ctr;
          if (max_rows > strip_height_) max_rows = strip_height_;
          unsigned num_rows = 0;
          up_->upsample(input_buf, in_row_group_ctr, in_row_groups_avail, buffer_, &num_rows,
                        max_rows);
          if (!cq_->noop) cq_->color_quantize(buffer_, output_buf + *out_row_ctr,
                                              static_cast<int>(num_rows));
          *out_row_ctr += num_rows;
        }
        break;
      case Mode::SaveAndPass: {
        if (next_row_ == 0) buffer_ = whole_image_.rows() + starting_row_;
        unsigned old_next_row = next_row_;
        up_->upsample(input_buf, in_row_group_ctr, in_row_groups_avail, buffer_, &next_row_,
                      strip_height_);
        if (next_row_ > old_next_row) {
          unsigned num_rows = next_row_ - old_next_row;
          cq_->color_quantize(buffer_ + old_next_row, nullptr, static_cast<int>(num_rows));
          *out_row_ctr += num_rows;
        }
        if (next_row_ >= strip_height_) {
          starting_row_ += strip_height_;
          next_row_ = 0;
        }
        break;
      }
      case Mode::CrankDest: {
        if (next_row_ == 0) buffer_ = whole_image_.rows() + starting_row_;
        unsigned num_rows = strip_height_ - next_row_;
        unsigned max_rows = out_rows_avail - *out_row_ctr;
        if (num_rows > max_rows) num_rows = max_rows;
        max_rows = st_.output_height - starting_row_;
        if (num_rows > max_rows) num_rows = max_rows;
        cq_->color_quantize(buffer_ + next_row_, output_buf + *out_row_ctr,
                            static_cast<int>(num_rows));
        *out_row_ctr += num_rows;
        next_row_ += num_rows;
        if (next_row_ >= strip_height_) {
          starting_row_ += strip_height_;
          next_row_ = 0;
        }
        break;
      }
    }
  }

 private:
  DState& st_;
  Mode mode_ = Mode::PassThru;
  Upsampler<T>* up_ = nullptr;
  ColorQuantizer<T>* cq_ = nullptr;
  SampleArray<T> whole_image_, strip_;
  T** buffer_ = nullptr;
  unsigned strip_height_ = 0, starting_row_ = 0, next_row_ = 0;
};

// ---------------------------------------------------------------------------
// Main buffer controller
template <class T>
class MainController {
 public:
  enum { CTX_PREPARE_FOR_IMCU = 0, CTX_PROCESS_IMCU = 1, CTX_POSTPONED_ROW = 2 };

  MainController(DState& st, bool need_context_rows) : st_(st), context_(need_context_rows) {
    int M = st.min_DCT_scaled_size;
    int ngroups = M + (need_context_rows ? 2 : 0);
    if (need_context_rows) {
      if (M < 2) VIO_FAIL("Requested features are incompatible");
      for (int w = 0; w < 2; w++) {
        xbuf_store_[w].resize(st.num_components);
        xbuffer_[w].resize(st.num_components);
      }
      for (int ci = 0; ci < st.num_components; ci++) {
        ComponentInfo& c = st.comp_info[ci];
        int rgroup = (c.v_samp_factor * c.DCT_scaled_size) / M;
        for (int w = 0; w < 2; w++) {
          xbuf_store_[w][ci].assign(static_cast<std::size_t>(rgroup) * (M + 4), nullptr);
          xbuffer_[w][ci] = xbuf_store_[w][ci].data() + rgroup;  // one row group at negative offsets
        }
      }
    }
    buffer_.resize(st.num_components);
    buffer_rows_.resize(st.num_components);
    for (int ci = 0; ci < st.num_components; ci++) {
      ComponentInfo& c = st.comp_info[ci];
      int rgroup = (c.v_samp_factor * c.DCT_scaled_size) / M;
      buffer_[ci].allocate(static_cast<std::size_t>(c.width_in_blocks) * c.DCT_scaled_size,
                           static_cast<std::size_t>(rgroup) * ngroups);
      buffer_rows_[ci] = buffer_[ci].rows();
    }
  }

  // JBUF_PASS_THRU when crank == false, else JBUF_CRANK_DEST
  void start_pass(bool crank) {
    crank_ = crank;
    if (crank) return;
    if (context_) {
      make_funny_pointers();
      whichptr = 0;
      context_state = CTX_PREPARE_FOR_IMCU;
      iMCU_row_ctr = 0;
    }
    buffer_full = false;
    rowgroup_ctr = 0;
  }

  void process_data(T** output_buf, unsigned* out_row_ctr, unsigned out_rows_avail) {
    if (crank_) {
      post->post_process_data(nullptr, nullptr, 0, output_buf, out_row_ctr, out_rows_avail);
    } else if (context_) {
      process_data_context(output_buf, out_row_ctr, out_rows_avail);
    } else {
      process_data_simple(output_buf, out_row_ctr, out_rows_avail);
    }
  }

  void set_wraparound_pointers() {
    int M = st_.min_DCT_scaled_size;
    for (int ci = 0; ci < st_.num_components; ci++) {
      ComponentInfo& c = st_.comp_info[ci];
      int rgroup = (c.v_samp_factor * c.DCT_scaled_size) / M;
      T** xbuf0 = xbuffer_[0][ci];
      T** xbuf1 = xbuffer_[1][ci];
      for (int i = 0; i < rgroup; i++) {
        xbuf0[i - rgroup] = xbuf0[rgroup * (M + 1) + i];
        xbuf1[i - rgroup] = xbuf1[rgroup * (M + 1) + i];
        xbuf0[rgroup * (M + 2) + i] = xbuf0[i];
        xbuf1[rgroup * (M + 2) + i] = xbuf1[i];
      }
    }
  }

  bool context() const { return context_; }

  // state (accessed by skip_scanlines)
  bool buffer_full = false;
  unsigned rowgroup_ctr = 0;
  int whichptr = 0;
  int context_state = 0;
  unsigned rowgroups_avail = 0;
  unsigned iMCU_row_ctr = 0;

  DCoefController<T>* coef = nullptr;
  PostController<T>* post = nullptr;

 private:
  void make_funny_pointers() {
    int M = st_.min_DCT_scaled_size;
    for (int ci = 0; ci < st_.num_components; ci++) {
      ComponentInfo& c = st_.comp_info[ci];
      int rgroup = (c.v_samp_factor * c.DCT_scaled_size) / M;
      T** xbuf0 = xbuffer_[0][ci];
      T** xbuf1 = xbuffer_[1][ci];
      T** buf = buffer_rows_[ci];
      for (int i = 0; i < rgroup * (M + 2); i++) xbuf0[i] = xbuf1[i] = buf[i];
      for (int i = 0; i < rgroup * 2; i++) {
        xbuf1[rgroup * (M - 2) + i] = buf[rgroup * M + i];
        xbuf1[rgroup * M + i] = buf[rgroup * (M - 2) + i];
      }
      for (int i = 0; i < rgroup; i++) xbuf0[i - rgroup] = xbuf0[0];
    }
  }

  void set_bottom_pointers() {
    int M = st_.min_DCT_scaled_size;
    for (int ci = 0; ci < st_.num_components; ci++) {
      ComponentInfo& c = st_.comp_info[ci];
      int iMCUheight = c.v_samp_factor * c.DCT_scaled_size;
      int rgroup = iMCUheight / M;
      int rows_left = static_cast<int>(c.downsampled_height % static_cast<unsigned>(iMCUheight));
      if (rows_left == 0) rows_left = iMCUheight;
      if (ci == 0) rowgroups_avail = static_cast<unsigned>((rows_left - 1) / rgroup + 1);
      T** xbuf = xbuffer_[whichptr][ci];
      for (int i = 0; i < rgroup * 2; i++) xbuf[rows_left + i] = xbuf[rows_left - 1];
    }
  }

  void process_data_simple(T** output_buf, unsigned* out_row_ctr, unsigned out_rows_avail) {
    if (!buffer_full) {
      if (!coef->decompress_data(buffer_rows_.data())) return;
      buffer_full = true;
    }
    unsigned avail = static_cast<unsigned>(st_.min_DCT_scaled_size);
    post->post_process_data(buffer_rows_.data(), &rowgroup_ctr, avail, output_buf, out_row_ctr,
                            out_rows_avail);
    if (rowgroup_ctr >= avail) {
      buffer_full = false;
      rowgroup_ctr = 0;
    }
  }

  void process_data_context(T** output_buf, unsigned* out_row_ctr, unsigned out_rows_avail) {
    int M = st_.min_DCT_scaled_size;
    if (!buffer_full) {
      if (!coef->decompress_data(xbuffer_[whichptr].data())) return;
      buffer_full = true;
      iMCU_row_ctr++;
    }
    switch (context_state) {
      case CTX_POSTPONED_ROW:
        post->post_process_data(xbuffer_[whichptr].data(), &rowgroup_ctr, rowgroups_avail,
                                output_buf, out_row_ctr, out_rows_avail);
        if (rowgroup_ctr < rowgroups_avail) return;
        context_state = CTX_PREPARE_FOR_IMCU;
        if (*out_row_ctr >= out_rows_avail) return;
        [[fallthrough]];
      case CTX_PREPARE_FOR_IMCU:
        rowgroup_ctr = 0;
        rowgroups_avail = static_cast<unsigned>(M - 1);
        if (iMCU_row_ctr == st_.total_iMCU_rows) set_bottom_pointers();
        context_state = CTX_PROCESS_IMCU;
        [[fallthrough]];
      case CTX_PROCESS_IMCU:
        post->post_process_data(xbuffer_[whichptr].data(), &rowgroup_ctr, rowgroups_avail,
                                output_buf, out_row_ctr, out_rows_avail);
        if (rowgroup_ctr < rowgroups_avail) return;
        if (iMCU_row_ctr == 1) set_wraparound_pointers();
        whichptr ^= 1;
        buffer_full = false;
        rowgroup_ctr = static_cast<unsigned>(M + 1);
        rowgroups_avail = static_cast<unsigned>(M + 2);
        context_state = CTX_POSTPONED_ROW;
    }
  }

  DState& st_;
  bool context_;
  bool crank_ = false;
  std::vector<SampleArray<T>> buffer_;
  std::vector<T**> buffer_rows_;
  std::vector<std::vector<T*>> xbuf_store_[2];
  std::vector<T**> xbuffer_[2];
};

}  // namespace vio::jpeg::detail
