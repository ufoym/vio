// vio::jpeg - compression master control (initialization, pass logic,
// transcoding, ICC profiles).
#pragma once

#include "arith_encode.hpp"
#include "compress_pipeline.hpp"

namespace vio::jpeg::detail {

class CPipelineBase {
 public:
  virtual ~CPipelineBase() = default;
  virtual int sample_bits() const = 0;
  virtual void start_main_pass(bool multipass) = 0;
  virtual void start_coef_pass(bool crank) = 0;
  virtual void crank_imcu_row() = 0;
};

template <class T>
class CPipeline final : public CPipelineBase {
 public:
  explicit CPipeline(CState& cs, EntropyEncoder** entropy) : cs_(cs), entropy_(entropy) {}

  int sample_bits() const override { return kBits<T>; }

  void select() {
    CState& cs = cs_;
    if (!cs.raw_data_in) {
      cconvert = std::make_unique<ColorConverter<T>>(cs);
      downsample = std::make_unique<Downsampler<T>>(cs);
      prep = std::make_unique<PrepController<T>>(cs, cconvert.get(), downsample.get());
    }
    bool full = cs.num_scans > 1 || cs.optimize_coding;
    if (cs.lossless) {
      lossless = std::make_unique<LosslessCompressor<T>>(cs);
      coef = std::make_unique<DiffControllerC<T>>(cs, lossless.get(), entropy_, full);
    } else {
      if constexpr (kBits<T> == 16) {
        VIO_FAIL("Unsupported JPEG data precision %d", cs.data_precision);
      } else {
        fdct = std::make_unique<ForwardDct<T>>(cs.dct_method, cs.simd);
        coef = std::make_unique<CoefControllerC<T>>(cs, fdct.get(), entropy_, full);
      }
    }
    main = std::make_unique<MainControllerC<T>>(cs, prep.get(), coef.get());
  }

  void start_main_pass(bool multipass) override {
    if (!cs_.raw_data_in) {
      cconvert->start_pass();
      prep->start_pass();
    }
    if (lossless) {
      lossless->start_pass();
    } else if (fdct) {
      int qn[MAX_COMPONENTS];
      for (int ci = 0; ci < cs_.num_components; ci++) qn[ci] = cs_.comp_info[ci].quant_tbl_no;
      fdct->start_pass(cs_.quant_tbl_ptrs, qn, cs_.num_components);
    }
  }

  void start_coef_pass(bool crank) override {
    using Mode = typename CCoefController<T>::Mode;
    if (crank) {
      coef->start_pass(Mode::CrankDest);
    } else {
      coef->start_pass(cs_.total_passes > 1 ? Mode::SaveAndPass : Mode::PassThru);
      main->start_pass();
    }
  }

  void crank_imcu_row() override { coef->compress_data(nullptr); }

  std::unique_ptr<ColorConverter<T>> cconvert;
  std::unique_ptr<Downsampler<T>> downsample;
  std::unique_ptr<PrepController<T>> prep;
  std::unique_ptr<ForwardDct<T>> fdct;
  std::unique_ptr<LosslessCompressor<T>> lossless;
  std::unique_ptr<CCoefController<T>> coef;
  std::unique_ptr<MainControllerC<T>> main;

 private:
  CState& cs_;
  EntropyEncoder** entropy_;
};

class Compressor {
 public:
  enum class State { Start, Scanning, RawOk, WrCoefs };

  CState cs;
  State state = State::Start;
  Diagnostics diag;

  Compressor() { cs.diag = &diag; }

  // jpeg_start_compress()
  void start_compress(bool write_all_tables = true) {
    if (state != State::Start)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    if (write_all_tables) cs.suppress_tables(false);
    cs.dest.reset();
    cs.init_master_control(false);
    make_entropy();
    if (cs.data_precision <= 8) make_pipeline<std::uint8_t>();
    else if (cs.data_precision <= 12) make_pipeline<std::int16_t>();
    else make_pipeline<std::uint16_t>();
    cs.write_file_header();
    prepare_for_pass();
    cs.next_scanline = 0;
    state = cs.raw_data_in ? State::RawOk : State::Scanning;
  }

  // jpeg_write_scanlines()
  template <class T>
  unsigned write_scanlines(T** scanlines, unsigned num_lines) {
    if (state != State::Scanning)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    if (cs.next_scanline >= cs.image_height) cs.warn("Application transferred too many scanlines");
    if (cs.call_pass_startup) pass_startup();
    unsigned rows_left = cs.image_height - cs.next_scanline;
    if (num_lines > rows_left) num_lines = rows_left;
    unsigned row_ctr = 0;
    pipeline<T>().main->process_data(scanlines, &row_ctr, num_lines);
    cs.next_scanline += row_ctr;
    return row_ctr;
  }

  // jpeg_write_raw_data()
  template <class T>
  unsigned write_raw_data(T*** data, unsigned num_lines) {
    if (cs.lossless) VIO_FAIL("Requested features are incompatible");
    if (state != State::RawOk)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    if (cs.next_scanline >= cs.image_height) {
      cs.warn("Application transferred too many scanlines");
      return 0;
    }
    if (cs.call_pass_startup) pass_startup();
    unsigned lines_per_iMCU_row = cs.max_v_samp_factor * DCTSIZE;
    if (num_lines < lines_per_iMCU_row) VIO_FAIL("Buffer passed to JPEG library is too small");
    pipeline<T>().coef->compress_data(data);
    cs.next_scanline += lines_per_iMCU_row;
    return lines_per_iMCU_row;
  }

  // jpeg_write_marker()
  void write_marker(int marker, const std::uint8_t* data, unsigned datalen) {
    if (cs.next_scanline != 0 ||
        (state != State::Scanning && state != State::RawOk && state != State::WrCoefs))
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    cs.write_marker_header(marker, datalen);
    for (unsigned i = 0; i < datalen; i++) cs.emit_byte(data[i]);
  }

  // jpeg_write_icc_profile()
  void write_icc_profile(const std::uint8_t* icc, unsigned icc_len) {
    if (!icc || icc_len == 0) VIO_FAIL("Buffer passed to JPEG library is too small");
    if (state == State::Start)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    constexpr unsigned MAX_DATA = 65533 - 14;
    unsigned num_markers = icc_len / MAX_DATA;
    if (num_markers * MAX_DATA != icc_len) num_markers++;
    int cur_marker = 1;
    while (icc_len > 0) {
      unsigned length = std::min(icc_len, MAX_DATA);
      icc_len -= length;
      if (cs.next_scanline != 0 ||
          (state != State::Scanning && state != State::RawOk && state != State::WrCoefs))
        VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
      cs.write_marker_header(M_APP2, length + 14);
      static const std::uint8_t id[12] = {0x49, 0x43, 0x43, 0x5F, 0x50, 0x52,
                                          0x4F, 0x46, 0x49, 0x4C, 0x45, 0x00};
      for (std::uint8_t b : id) cs.emit_byte(b);
      cs.emit_byte(cur_marker);
      cs.emit_byte(static_cast<int>(num_markers));
      for (unsigned i = 0; i < length; i++) cs.emit_byte(icc[i]);
      icc += length;
      cur_marker++;
    }
  }

  // jpeg_finish_compress()
  void finish_compress() {
    if (state == State::Scanning || state == State::RawOk) {
      if (cs.next_scanline < cs.image_height)
        VIO_FAIL("Application transferred too few scanlines");
      finish_pass_master();
    } else if (state != State::WrCoefs) {
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    }
    while (!cs.is_last_pass) {
      prepare_for_pass();
      for (unsigned iMCU_row = 0; iMCU_row < cs.total_iMCU_rows; iMCU_row++) {
        if (transcode_) transcode_->compress_output();
        else pipeline_->crank_imcu_row();
      }
      finish_pass_master();
    }
    cs.write_file_trailer();
    abort();
  }

  void abort() {
    pipeline_.reset();
    transcode_.reset();
    entropy_.reset();
    entropy_raw_ = nullptr;
    state = State::Start;
  }

  // jpeg_write_tables(): abbreviated table-specification datastream
  void write_tables() {
    if (state != State::Start)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    cs.dest.reset();
    cs.write_tables_only();
  }

  // jpeg_write_coefficients(): transcoding from caller-supplied arrays
  void write_coefficients(BlockArray* coef_arrays) {
    if (cs.lossless) VIO_FAIL("Requested features are incompatible");
    if (state != State::Start)
      VIO_FAIL("Improper call to JPEG library in state %d", static_cast<int>(state));
    cs.suppress_tables(false);
    cs.dest.reset();
    cs.input_components = 1;
    cs.init_master_control(true);
    make_entropy();
    transcode_ = std::make_unique<TranscodeCoefController>(cs, &entropy_raw_, coef_arrays);
    cs.write_file_header();
    cs.next_scanline = 0;
    state = State::WrCoefs;
  }

  std::vector<std::uint8_t> take_output() { return cs.dest.take(); }

  template <class T>
  CPipeline<T>& pipeline() {
    if (!pipeline_ || pipeline_->sample_bits() != kBits<T>)
      VIO_FAIL("Unsupported JPEG data precision %d", cs.data_precision);
    return static_cast<CPipeline<T>&>(*pipeline_);
  }
  CPipelineBase* pipeline_base() { return pipeline_.get(); }

 private:
  void make_entropy() {
    if (cs.lossless) {
      if (cs.arith_code) VIO_FAIL("Sorry, arithmetic coding is not implemented");
      entropy_ = std::make_unique<LhuffEncoder>(cs);
    } else if (cs.arith_code) {
      entropy_ = std::make_unique<ArithEncoder>(cs);
    } else if (cs.progressive_mode) {
      entropy_ = std::make_unique<PhuffEncoder>(cs);
    } else {
      entropy_ = std::make_unique<HuffEncoder>(cs);
    }
    entropy_raw_ = entropy_.get();
  }

  template <class T>
  void make_pipeline() {
    auto p = std::make_unique<CPipeline<T>>(cs, &entropy_raw_);
    p->select();
    pipeline_ = std::move(p);
  }

  void pass_startup() {
    cs.call_pass_startup = false;
    cs.write_frame_header();
    cs.write_scan_header();
  }

  void start_coef_crank() {
    if (transcode_) transcode_->start_pass();
    else pipeline_->start_coef_pass(true);
  }

  void prepare_for_pass() {
    switch (cs.pass_type) {
      case CState::main_pass:
        cs.select_scan_parameters();
        cs.per_scan_setup();
        pipeline_->start_main_pass(cs.total_passes > 1);
        entropy_->start_pass(cs.optimize_coding);
        pipeline_->start_coef_pass(false);
        cs.call_pass_startup = !cs.optimize_coding;
        break;
      case CState::huff_opt_pass:
        cs.select_scan_parameters();
        cs.per_scan_setup();
        if (cs.Ss != 0 || cs.Ah == 0 || cs.arith_code || cs.lossless) {
          entropy_->start_pass(true);
          start_coef_crank();
          cs.call_pass_startup = false;
          break;
        }
        cs.pass_type = CState::output_pass;
        cs.pass_number++;
        [[fallthrough]];
      case CState::output_pass:
        if (!cs.optimize_coding) {
          cs.select_scan_parameters();
          cs.per_scan_setup();
        }
        entropy_->start_pass(false);
        start_coef_crank();
        if (cs.scan_number == 0) cs.write_frame_header();
        cs.write_scan_header();
        cs.call_pass_startup = false;
        break;
    }
    cs.is_last_pass = cs.pass_number == cs.total_passes - 1;
  }

  void finish_pass_master() {
    entropy_->finish_pass();
    switch (cs.pass_type) {
      case CState::main_pass:
        cs.pass_type = CState::output_pass;
        if (!cs.optimize_coding) cs.scan_number++;
        break;
      case CState::huff_opt_pass:
        cs.pass_type = CState::output_pass;
        break;
      case CState::output_pass:
        if (cs.optimize_coding) cs.pass_type = CState::huff_opt_pass;
        cs.scan_number++;
        break;
    }
    cs.pass_number++;
  }

  std::unique_ptr<EntropyEncoder> entropy_;
  EntropyEncoder* entropy_raw_ = nullptr;
  std::unique_ptr<CPipelineBase> pipeline_;
  std::unique_ptr<TranscodeCoefController> transcode_;
};

}  // namespace vio::jpeg::detail
