// vio::jpeg - decompression master control.
#pragma once

#include "arith_decode.hpp"
#include "decompress_quantize.hpp"
#include "huff_decode.hpp"

namespace vio::jpeg::detail {

inline void init_lossy_entropy(DState& st) {
  if (st.arith_code) st.entropy = std::make_unique<ArithDecoder>(st);
  else if (st.progressive_mode) st.entropy = std::make_unique<PhuffDecoder>(st);
  else st.entropy = std::make_unique<HuffDecoder>(st);
}

class PipelineBase {
 public:
  virtual ~PipelineBase() = default;
  virtual void prepare_for_output_pass() = 0;
  virtual void finish_output_pass() = 0;
  virtual void process_dummy_pass_rows() = 0;
  virtual int sample_bits() const = 0;
};

template <class T>
class Pipeline final : public PipelineBase {
 public:
  explicit Pipeline(DState& st) : st_(st) {}

  int sample_bits() const override { return kBits<T>; }

  void select(bool use_c_buffer) {
    DState& st = st_;
    // Color quantizer selection
    if (!st.quantize_colors || !st.buffered_image) {
      st.enable_1pass_quant = st.enable_external_quant = st.enable_2pass_quant = false;
    }
    if (st.quantize_colors) {
      if (st.raw_data_out) VIO_FAIL("Requested features are incompatible");
      if constexpr (kBits<T> == 16) VIO_FAIL("Unsupported JPEG data precision %d", st.data_precision);
      if (st.out_color_components != 3 || st.out_color_space == ColorSpace::RGB565) {
        st.enable_1pass_quant = true;
        st.enable_external_quant = st.enable_2pass_quant = false;
        st.colormap_external = false;
        st.colormap.clear();
      } else if (st.colormap_external) {
        st.enable_external_quant = true;
      } else if (st.two_pass_quantize) {
        st.enable_2pass_quant = true;
      } else {
        st.enable_1pass_quant = true;
      }
      if (st.enable_1pass_quant) quant1_ = std::make_unique<Quantizer1Pass<T>>(st, range_.limit());
      if (st.enable_2pass_quant || st.enable_external_quant) {
        quant2_ = std::make_unique<Quantizer2Pass<T>>(st, range_.limit());
        if (st.enable_external_quant) quant2_->set_external_colormap(st.colormap);
        cquantize = quant2_.get();
      } else {
        cquantize = quant1_.get();
      }
    }

    if (!st.raw_data_out) {
      if (st.using_merged_upsample) {
        merged_ = std::make_unique<MergedUpsampler<T>>(st, range_.limit());
        upsample = merged_.get();
      } else {
        cconvert = std::make_unique<ColorDeconverter<T>>(st, range_.limit());
        sep_ = std::make_unique<SepUpsampler<T>>(st, cconvert.get());
        upsample = sep_.get();
      }
      post = std::make_unique<PostController<T>>(st, st.enable_2pass_quant);
    }

    if (st.lossless) {
      lossless_ = std::make_unique<LosslessDecompressor<T>>(st);
      st.idct_start_pass = [this] { lossless_->start_pass(); };
      if (st.arith_code) VIO_FAIL("Sorry, arithmetic coding is not implemented");
      st.entropy = std::make_unique<LhuffDecoder>(st);
      coef = std::make_unique<DiffController<T>>(st, lossless_.get(), use_c_buffer);
    } else {
      if constexpr (kBits<T> == 16) {
        VIO_FAIL("Unsupported JPEG data precision %d", st.data_precision);
      } else {
        idct = std::make_unique<IdctManager<T>>(st, range_.limit());
        init_lossy_entropy(st);
        coef = std::make_unique<CoefController<T>>(st, idct.get(), use_c_buffer);
      }
    }
    st.coef = coef.get();

    if (!st.raw_data_out) {
      main = std::make_unique<MainController<T>>(st, upsample->need_context_rows);
      main->coef = coef.get();
      main->post = post.get();
    }
  }

  void prepare_for_output_pass() override {
    DState& st = st_;
    if (st.is_dummy_pass) {
      st.is_dummy_pass = false;
      cquantize->start_pass(false);
      post->start_pass(PostController<T>::Mode::CrankDest, upsample, cquantize);
      main->start_pass(true);
    } else {
      if (st.quantize_colors && !st.colormap_external) {
        if (st.two_pass_quantize && st.enable_2pass_quant) {
          cquantize = quant2_.get();
          st.is_dummy_pass = true;
        } else if (st.enable_1pass_quant) {
          cquantize = quant1_.get();
        } else {
          VIO_FAIL("Invalid color quantization mode change");
        }
      }
      if (idct) idct->start_pass();
      coef->start_output_pass();
      if (!st.raw_data_out) {
        if (!st.using_merged_upsample) cconvert->start_pass();
        upsample->start_pass();
        if (st.quantize_colors) cquantize->start_pass(st.is_dummy_pass);
        post->start_pass(st.is_dummy_pass ? PostController<T>::Mode::SaveAndPass
                                          : PostController<T>::Mode::PassThru,
                         upsample, cquantize);
        main->start_pass(false);
      }
    }
  }

  void finish_output_pass() override {
    if (st_.quantize_colors) cquantize->finish_pass();
  }

  void process_dummy_pass_rows() override {
    main->process_data(nullptr, &st_.output_scanline, 0);
  }

  // ---- members (public for the master / skip logic) ----
  RangeLimit<T> range_;
  std::unique_ptr<IdctManager<T>> idct;
  std::unique_ptr<LosslessDecompressor<T>> lossless_;
  std::unique_ptr<DCoefController<T>> coef;
  std::unique_ptr<ColorDeconverter<T>> cconvert;
  std::unique_ptr<SepUpsampler<T>> sep_;
  std::unique_ptr<MergedUpsampler<T>> merged_;
  Upsampler<T>* upsample = nullptr;
  std::unique_ptr<PostController<T>> post;
  std::unique_ptr<MainController<T>> main;
  std::unique_ptr<Quantizer1Pass<T>> quant1_;
  std::unique_ptr<Quantizer2Pass<T>> quant2_;
  ColorQuantizer<T>* cquantize = nullptr;

 private:
  DState& st_;
};

// ===========================================================================
class Decompressor {
 public:
  enum class State { Start, InHeader, Ready, Preload, Prescan, Scanning, RawOk, RdCoefs, Stopping };

  DState st;
  State state = State::Start;
  Diagnostics diag;
  // Called before each chunk of input is absorbed by start_decompress() and
  // read_coefficients() (progress monitor hook); may throw.
  std::function<void()> progress_monitor;

  Decompressor() { st.diag = &diag; }

  void set_source(const std::uint8_t* data, std::size_t size) {
    st.set_source(data, size);
    state = State::Start;
  }

  // jpeg_read_header(): returns false for a tables-only datastream.
  bool read_header(bool require_image = true) {
    if (state != State::Start && state != State::InHeader)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    int ret = consume_input();
    if (ret == REACHED_EOI) {
      if (require_image) VIO_FAIL("JPEG datastream contains no image");
      abort();
      return false;
    }
    return true;
  }

  int consume_input() {
    switch (state) {
      case State::Start:
        st.reset_input_controller();
        state = State::InHeader;
        [[fallthrough]];
      case State::InHeader: {
        int ret = st.consume_input();
        if (ret == REACHED_SOS) {
          st.default_decompress_parms();
          state = State::Ready;
        }
        return ret;
      }
      case State::Ready:
        return REACHED_SOS;
      default:
        return st.consume_input();
    }
  }

  void abort() {
    pipeline_.reset();
    transcoef_.reset();
    st.entropy.reset();
    st.coef = nullptr;
    state = State::Start;
  }

  // ---------------------------------------------------------------------------
  // jpeg_core_output_dimensions / jpeg_calc_output_dimensions
  void calc_output_dimensions() {
    if (state != State::Ready) VIO_FAIL("Improper call to JPEG library in state %d",
                                         static_cast<int>(state));
    if (!st.lossless) {
      unsigned n = st.scale_num, d = st.scale_denom;
      int size = 16;
      for (int s = 1; s <= 15; s++)
        if (n * DCTSIZE <= d * static_cast<unsigned>(s)) {
          size = s;
          break;
        }
      st.output_width = static_cast<unsigned>(
          div_round_up(static_cast<long>(st.image_width) * size, DCTSIZE));
      st.output_height = static_cast<unsigned>(
          div_round_up(static_cast<long>(st.image_height) * size, DCTSIZE));
      st.min_DCT_scaled_size = size;
      for (auto& c : st.comp_info) {
        int ssize = st.min_DCT_scaled_size;
        while (ssize < DCTSIZE &&
               (st.max_h_samp_factor * st.min_DCT_scaled_size) % (c.h_samp_factor * ssize * 2) ==
                   0 &&
               (st.max_v_samp_factor * st.min_DCT_scaled_size) % (c.v_samp_factor * ssize * 2) ==
                   0)
          ssize = ssize * 2;
        c.DCT_scaled_size = ssize;
        c.downsampled_width = static_cast<unsigned>(
            div_round_up(static_cast<long>(st.image_width) * (c.h_samp_factor * c.DCT_scaled_size),
                         st.max_h_samp_factor * DCTSIZE));
        c.downsampled_height = static_cast<unsigned>(div_round_up(
            static_cast<long>(st.image_height) * (c.v_samp_factor * c.DCT_scaled_size),
            st.max_v_samp_factor * DCTSIZE));
      }
    } else {
      st.output_width = st.image_width;
      st.output_height = st.image_height;
    }

    switch (st.out_color_space) {
      case ColorSpace::Gray: st.out_color_components = 1; break;
      case ColorSpace::RGB: case ColorSpace::ExtRGB: case ColorSpace::ExtRGBX:
      case ColorSpace::ExtBGR: case ColorSpace::ExtBGRX: case ColorSpace::ExtXBGR:
      case ColorSpace::ExtXRGB: case ColorSpace::ExtRGBA: case ColorSpace::ExtBGRA:
      case ColorSpace::ExtABGR: case ColorSpace::ExtARGB:
        st.out_color_components = rgb_pixelsize[idx(st.out_color_space)];
        break;
      case ColorSpace::YCbCr: case ColorSpace::RGB565: st.out_color_components = 3; break;
      case ColorSpace::CMYK: case ColorSpace::YCCK: st.out_color_components = 4; break;
      default: st.out_color_components = st.num_components;
    }
    st.output_components = st.quantize_colors ? 1 : st.out_color_components;
    st.rec_outbuf_height = use_merged_upsample() ? st.max_v_samp_factor : 1;
  }

  bool use_merged_upsample() const {
    if (st.lossless) return false;
    if (st.do_fancy_upsampling || st.CCIR601_sampling) return false;
    ColorSpace o = st.out_color_space;
    if (st.jpeg_color_space != ColorSpace::YCbCr || st.num_components != 3 ||
        !(o == ColorSpace::RGB565 || is_rgb_family(o)))
      return false;
    if ((o == ColorSpace::RGB565 && st.out_color_components != 3) ||
        (o != ColorSpace::RGB565 && st.out_color_components != rgb_pixelsize[idx(o)]))
      return false;
    const auto& c = st.comp_info;
    if (c[0].h_samp_factor != 2 || c[1].h_samp_factor != 1 || c[2].h_samp_factor != 1 ||
        c[0].v_samp_factor > 2 || c[1].v_samp_factor != 1 || c[2].v_samp_factor != 1)
      return false;
    if (c[0].DCT_scaled_size != st.min_DCT_scaled_size ||
        c[1].DCT_scaled_size != st.min_DCT_scaled_size ||
        c[2].DCT_scaled_size != st.min_DCT_scaled_size)
      return false;
    return true;
  }

  // master_selection()
  void master_selection() {
    if (st.jpeg_data_precision && st.data_precision != st.jpeg_data_precision &&
        (st.lossless || st.data_precision != 12))
      VIO_FAIL("Unsupported JPEG data precision %d", st.data_precision);
    if (st.lossless) {
      st.raw_data_out = false;
      st.scale_num = st.scale_denom = 1;
    }
    calc_output_dimensions();
    long samplesperrow = static_cast<long>(st.output_width) * st.out_color_components;
    if (static_cast<unsigned>(samplesperrow) != static_cast<unsigned long>(samplesperrow))
      VIO_FAIL("Image too wide for this implementation");
    st.using_merged_upsample = use_merged_upsample();
    st.is_dummy_pass = false;
    bool use_c_buffer = st.has_multiple_scans || st.buffered_image;
    if (st.data_precision <= 8) make_pipeline<std::uint8_t>(use_c_buffer);
    else if (st.data_precision <= 12) make_pipeline<std::int16_t>(use_c_buffer);
    else make_pipeline<std::uint16_t>(use_c_buffer);

    st.start_input_pass();
    st.first_iMCU_col = 0;
    st.last_iMCU_col = st.MCUs_per_row - 1;
    st.last_good_iMCU_row = 0;
  }

  template <class T>
  void make_pipeline(bool use_c_buffer) {
    auto p = std::make_unique<Pipeline<T>>(st);
    p->select(use_c_buffer);
    pipeline_ = std::move(p);
  }

  template <class T>
  Pipeline<T>& pipeline() {
    if (!pipeline_ || pipeline_->sample_bits() != kBits<T>)
      VIO_FAIL("Unsupported JPEG data precision %d", st.data_precision);
    return static_cast<Pipeline<T>&>(*pipeline_);
  }

  // jpeg_read_coefficients(): decodes the whole datastream into per-component
  // coefficient arrays (for transcoding).  The arrays stay valid until
  // finish_decompress()/abort().
  BlockArray* read_coefficients() {
    if (st.lossless) VIO_FAIL("Requested features are incompatible");
    if (state == State::Ready) begin_read_coefficients();
    if (state == State::RdCoefs) {
      for (;;) {
        if (progress_monitor) progress_monitor();
        int ret = st.consume_input();
        if (ret == REACHED_EOI) break;
      }
      state = State::Stopping;
    }
    if (state == State::Stopping && st.buffered_image) return st.coef->coef_arrays;
    VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
  }

  // First step of read_coefficients(): sets up the coefficient buffers, after
  // which consume_input() absorbs the datastream.
  void begin_read_coefficients() {
    st.buffered_image = true;
    init_lossy_entropy(st);
    transcoef_ = std::make_unique<CoefController<std::uint8_t>>(st, nullptr, true);
    st.coef = transcoef_.get();
    st.start_input_pass();
    state = State::RdCoefs;
  }

  // jpeg_start_decompress()
  void start_decompress() {
    if (state == State::Ready) {
      if (st.buffered_image) VIO_FAIL("Requested features are incompatible");
      master_selection();
      state = State::Preload;
    }
    if (state == State::Preload) {
      if (st.has_multiple_scans) {
        for (;;) {
          if (progress_monitor) progress_monitor();
          int ret = st.consume_input();
          if (ret == REACHED_EOI) break;
        }
      }
      st.output_scan_number = st.input_scan_number;
    } else if (state != State::Prescan) {
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    }
    output_pass_setup();
  }

  void output_pass_setup() {
    if (state != State::Prescan) {
      pipeline_->prepare_for_output_pass();
      st.output_scanline = 0;
      state = State::Prescan;
    }
    while (st.is_dummy_pass) {
      while (st.output_scanline < st.output_height) {
        unsigned last = st.output_scanline;
        pipeline_->process_dummy_pass_rows();
        if (st.output_scanline == last) VIO_FAIL("Suspension not allowed here");
      }
      pipeline_->finish_output_pass();
      pipeline_->prepare_for_output_pass();
      st.output_scanline = 0;
    }
    state = st.raw_data_out ? State::RawOk : State::Scanning;
  }

  // jpeg_read_scanlines()
  template <class T>
  unsigned read_scanlines(T** scanlines, unsigned max_lines) {
    if (state != State::Scanning)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    if (st.output_scanline >= st.output_height) {
      st.warn("Application transferred too many scanlines");
      return 0;
    }
    Pipeline<T>& p = pipeline<T>();
    unsigned row_ctr = 0;
    p.main->process_data(scanlines, &row_ctr, max_lines);
    st.output_scanline += row_ctr;
    return row_ctr;
  }

  // jpeg_read_raw_data()
  template <class T>
  unsigned read_raw_data(T*** data, unsigned max_lines) {
    if (st.lossless) VIO_FAIL("Requested features are incompatible");
    if (state != State::RawOk)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    if (st.output_scanline >= st.output_height) {
      st.warn("Application transferred too many scanlines");
      return 0;
    }
    unsigned lines_per_iMCU_row = st.max_v_samp_factor * st.min_DCT_scaled_size;
    if (max_lines < lines_per_iMCU_row) VIO_FAIL("Buffer passed to JPEG library is too small");
    Pipeline<T>& p = pipeline<T>();
    if (!p.coef->decompress_data(data)) return 0;
    st.output_scanline += lines_per_iMCU_row;
    return lines_per_iMCU_row;
  }

  // jpeg_finish_decompress()
  void finish_decompress() {
    if ((state == State::Scanning || state == State::RawOk) && !st.buffered_image) {
      if (st.output_scanline < st.output_height)
        VIO_FAIL("Application transferred too few scanlines");
      pipeline_->finish_output_pass();
      state = State::Stopping;
    } else if (state != State::Stopping) {
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    }
    while (!st.eoi_reached) st.consume_input();
    abort();
  }

  // ---------------------------------------------------------------------------
  // jpeg_crop_scanline()
  template <class T>
  void crop_scanline(unsigned* xoffset, unsigned* width) {
    if (st.data_precision != kBits<T> && !(kBits<T> == 12 && st.data_precision == 12))
      VIO_FAIL("Unsupported JPEG data precision %d", st.data_precision);
    if (st.lossless || st.raw_data_out) VIO_FAIL("Requested features are incompatible");
    if (state != State::Scanning || st.output_scanline != 0)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    if (!xoffset || !width) VIO_FAIL("Invalid crop request");
    if (*width == 0 ||
        static_cast<unsigned long long>(*xoffset) + *width > st.output_width)
      VIO_FAIL("Image too wide for this implementation");
    if (*width == st.output_width) return;
    Pipeline<T>& p = pipeline<T>();
    int align = (st.comps_in_scan == 1 && st.num_components == 1)
                    ? st.min_DCT_scaled_size
                    : st.min_DCT_scaled_size * st.max_h_samp_factor;
    unsigned input_xoffset = *xoffset;
    *xoffset = (input_xoffset / align) * align;
    *width = *width + input_xoffset - *xoffset;
    st.output_width = *width;
    if (st.using_merged_upsample && st.max_v_samp_factor == 2)
      p.merged_->out_row_width = st.output_width * st.out_color_components;
    st.first_iMCU_col = static_cast<unsigned>(static_cast<long>(*xoffset) / align);
    st.last_iMCU_col =
        static_cast<unsigned>(div_round_up(static_cast<long>(*xoffset + st.output_width), align)) -
        1;
    bool reinit_upsampler = false;
    for (int ci = 0; ci < st.num_components; ci++) {
      ComponentInfo& c = st.comp_info[ci];
      int hsf = (st.comps_in_scan == 1 && st.num_components == 1) ? 1 : c.h_samp_factor;
      unsigned orig_downsampled_width = c.downsampled_width;
      c.downsampled_width = static_cast<unsigned>(
          div_round_up(static_cast<long>(st.output_width) * (c.h_samp_factor * c.DCT_scaled_size),
                       st.max_h_samp_factor * st.min_DCT_scaled_size));
      if (c.downsampled_width < 2 && orig_downsampled_width >= 2) reinit_upsampler = true;
      st.first_MCU_col[ci] = static_cast<unsigned>(static_cast<long>(*xoffset * hsf) / align);
      st.last_MCU_col[ci] = static_cast<unsigned>(div_round_up(
                                static_cast<long>((*xoffset + st.output_width) * hsf), align)) -
                            1;
    }
    if (reinit_upsampler && p.sep_) p.sep_->init(false);
  }

  // ---------------------------------------------------------------------------
  // jpeg_skip_scanlines()
  template <class T>
  unsigned skip_scanlines(unsigned num_lines) {
    if (st.lossless) VIO_FAIL("Requested features are incompatible");
    if (st.quantize_colors && st.two_pass_quantize) VIO_FAIL("Requested features are incompatible");
    if (state != State::Scanning)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    Pipeline<T>& p = pipeline<T>();
    MainController<T>& m = *p.main;

    if (static_cast<unsigned long long>(st.output_scanline) + num_lines >= st.output_height) {
      num_lines = st.output_height - st.output_scanline;
      st.output_scanline = st.output_height;
      st.finish_input_pass();
      st.eoi_reached = true;
      return num_lines;
    }
    if (num_lines == 0) return 0;

    unsigned lines_per_iMCU_row = st.min_DCT_scaled_size * st.max_v_samp_factor;
    unsigned lines_left_in_iMCU_row =
        (lines_per_iMCU_row - (st.output_scanline % lines_per_iMCU_row)) % lines_per_iMCU_row;
    unsigned lines_after_iMCU_row = num_lines - lines_left_in_iMCU_row;

    auto reset_upsampler_rows = [&] {
      if (!st.using_merged_upsample) {
        p.sep_->next_row_out = st.max_v_samp_factor;
        p.sep_->rows_to_go = st.output_height - st.output_scanline;
      }
    };

    if (p.upsample->need_context_rows) {
      if ((num_lines < lines_left_in_iMCU_row + 1) ||
          (lines_left_in_iMCU_row <= 1 && m.buffer_full &&
           lines_after_iMCU_row < lines_per_iMCU_row + 1)) {
        read_and_discard_scanlines(p, num_lines);
        return num_lines;
      }
      if (lines_left_in_iMCU_row <= 1 && m.buffer_full) {
        st.output_scanline += lines_left_in_iMCU_row + lines_per_iMCU_row;
        lines_after_iMCU_row -= lines_per_iMCU_row;
      } else {
        st.output_scanline += lines_left_in_iMCU_row;
      }
      if (m.iMCU_row_ctr == 0 || (m.iMCU_row_ctr == 1 && lines_left_in_iMCU_row > 2))
        m.set_wraparound_pointers();
      m.buffer_full = false;
      m.rowgroup_ctr = 0;
      m.context_state = MainController<T>::CTX_PREPARE_FOR_IMCU;
      reset_upsampler_rows();
    } else {
      if (num_lines < lines_left_in_iMCU_row) {
        increment_simple_rowgroup_ctr(p, num_lines);
        return num_lines;
      }
      st.output_scanline += lines_left_in_iMCU_row;
      m.buffer_full = false;
      m.rowgroup_ctr = 0;
      reset_upsampler_rows();
    }

    unsigned lines_to_skip;
    if (p.upsample->need_context_rows)
      lines_to_skip = ((lines_after_iMCU_row - 1) / lines_per_iMCU_row) * lines_per_iMCU_row;
    else
      lines_to_skip = (lines_after_iMCU_row / lines_per_iMCU_row) * lines_per_iMCU_row;
    unsigned lines_to_read = lines_after_iMCU_row - lines_to_skip;

    if (st.has_multiple_scans || st.buffered_image) {
      st.output_scanline += lines_to_skip;
      st.output_iMCU_row += lines_to_skip / lines_per_iMCU_row;
      if (p.upsample->need_context_rows) {
        m.iMCU_row_ctr += lines_to_skip / lines_per_iMCU_row;
        read_and_discard_scanlines(p, lines_to_read);
      } else {
        increment_simple_rowgroup_ctr(p, lines_to_read);
      }
      if (!st.using_merged_upsample) p.sep_->rows_to_go = st.output_height - st.output_scanline;
      return num_lines;
    }

    for (unsigned i = 0; i < lines_to_skip; i += lines_per_iMCU_row) p.coef->skip_imcu_row();
    st.output_scanline += lines_to_skip;

    if (p.upsample->need_context_rows) {
      m.iMCU_row_ctr += lines_to_skip / lines_per_iMCU_row;
      read_and_discard_scanlines(p, lines_to_read);
    } else {
      increment_simple_rowgroup_ctr(p, lines_to_read);
    }
    if (!st.using_merged_upsample) p.sep_->rows_to_go = st.output_height - st.output_scanline;
    return num_lines;
  }

  DState& state_ref() { return st; }
  PipelineBase* pipeline_base() { return pipeline_.get(); }

 private:
  template <class T>
  void read_and_discard_scanlines(Pipeline<T>& p, unsigned num_lines) {
    std::vector<T> dummy(static_cast<std::size_t>(st.output_width) * 4 + 64);
    T* dummy_row = dummy.data();
    T** scanlines = &dummy_row;
    bool had_convert = false;
    if (!st.using_merged_upsample && p.sep_) {
      p.sep_->convert_noop = true;
      had_convert = true;
    }
    bool had_quant = false;
    if (st.quantize_colors && p.cquantize) {
      p.cquantize->noop = true;
      had_quant = true;
    }
    if (st.using_merged_upsample && st.max_v_samp_factor == 2) scanlines = &p.merged_->spare_row;
    for (unsigned n = 0; n < num_lines; n++) read_scanlines<T>(scanlines, 1);
    if (had_convert) p.sep_->convert_noop = false;
    if (had_quant) p.cquantize->noop = false;
  }

  template <class T>
  void increment_simple_rowgroup_ctr(Pipeline<T>& p, unsigned rows) {
    if (st.using_merged_upsample && st.max_v_samp_factor == 2) {
      read_and_discard_scanlines(p, rows);
      return;
    }
    p.main->rowgroup_ctr += rows / st.max_v_samp_factor;
    unsigned rows_left = rows % st.max_v_samp_factor;
    st.output_scanline += rows - rows_left;
    read_and_discard_scanlines(p, rows_left);
  }

  std::unique_ptr<PipelineBase> pipeline_;
  std::unique_ptr<DCoefController<std::uint8_t>> transcoef_;
};

}  // namespace vio::jpeg::detail
