// vio::jpeg::Encoder - JPEG compression (baseline, extended, progressive,
// arithmetic-coded and lossless; 2..16-bit samples).
//
//   vio::jpeg::Encoder enc;
//   enc.set_image(w, h, 3, ColorSpace::RGB).set_quality(90);
//   enc.start();
//   for (unsigned y = 0; y < h; y++) { uint8_t* r = row(y); enc.write_scanlines(&r, 1); }
//   std::vector<uint8_t> jpeg = enc.finish();
#pragma once

#include <span>

#include "detail/compress.hpp"

namespace vio::jpeg {

using ScanInfo = detail::ScanInfo;

class Encoder {
 public:
  Encoder() = default;
  Encoder(const Encoder&) = delete;
  Encoder& operator=(const Encoder&) = delete;

  Diagnostics& diagnostics() { return c_.diag; }

  // Describes the source image and resets all parameters to their defaults
  // (jpeg_set_defaults()).
  Encoder& set_image(unsigned width, unsigned height, int components, ColorSpace in_color,
                     int precision = 8) {
    auto& cs = c_.cs;
    cs.image_width = width;
    cs.image_height = height;
    cs.input_components = components;
    cs.in_color_space = in_color;
    cs.data_precision = precision;
    cs.set_defaults();
    return *this;
  }
  // Re-applies defaults for the current image description.
  Encoder& set_defaults() { c_.cs.set_defaults(); return *this; }

  Encoder& set_precision(int bits) { c_.cs.data_precision = bits; return *this; }
  Encoder& set_colorspace(ColorSpace cs) { c_.cs.set_colorspace(cs); return *this; }
  Encoder& set_quality(int quality, bool force_baseline = true) {
    c_.cs.set_quality(quality, force_baseline);
    return *this;
  }
  Encoder& set_linear_quality(int scale_factor, bool force_baseline = true) {
    c_.cs.set_linear_quality(scale_factor, force_baseline);
    return *this;
  }
  Encoder& add_quant_table(int slot, const unsigned* basic_table, int scale_factor,
                           bool force_baseline) {
    c_.cs.add_quant_table(slot, basic_table, scale_factor, force_baseline);
    return *this;
  }
  Encoder& set_component_quant_table(int ci, int slot) {
    c_.cs.comp_info[ci].quant_tbl_no = slot;
    return *this;
  }
  Encoder& set_sampling(int ci, int h, int v) {
    c_.cs.comp_info[ci].h_samp_factor = h;
    c_.cs.comp_info[ci].v_samp_factor = v;
    return *this;
  }
  Encoder& set_dct_method(DctMethod m) { c_.cs.dct_method = m; return *this; }
  Encoder& set_optimize_coding(bool on) { c_.cs.optimize_coding = on; return *this; }
  Encoder& set_arithmetic(bool on) { c_.cs.arith_code = on; return *this; }
  Encoder& set_progressive() { c_.cs.simple_progression(); return *this; }
  Encoder& set_scan_script(std::vector<ScanInfo> scans) {
    c_.cs.scan_info = std::move(scans);
    c_.cs.num_scans = static_cast<int>(c_.cs.scan_info.size());
    return *this;
  }
  Encoder& set_lossless(int predictor, int point_transform) {
    c_.cs.enable_lossless(predictor, point_transform);
    return *this;
  }
  Encoder& set_restart_interval(unsigned mcus) { c_.cs.restart_interval = mcus; return *this; }
  Encoder& set_restart_rows(int rows) { c_.cs.restart_in_rows = rows; return *this; }
  Encoder& set_smoothing(int factor) { c_.cs.smoothing_factor = factor; return *this; }
  Encoder& set_raw_input(bool on) { c_.cs.raw_data_in = on; return *this; }
  Encoder& set_write_jfif(bool on) { c_.cs.write_JFIF_header = on; return *this; }
  Encoder& set_write_adobe(bool on) { c_.cs.write_Adobe_marker = on; return *this; }
  Encoder& set_density(int unit, int x, int y) {
    c_.cs.density_unit = static_cast<std::uint8_t>(unit);
    c_.cs.X_density = static_cast<std::uint16_t>(x);
    c_.cs.Y_density = static_cast<std::uint16_t>(y);
    return *this;
  }
  Encoder& set_huff_table(bool ac, int slot, const HuffTable& t) {
    (ac ? c_.cs.ac_huff_tbl_ptrs : c_.cs.dc_huff_tbl_ptrs)[slot] = t;
    return *this;
  }

  int num_components() const { return c_.cs.num_components; }
  ColorSpace jpeg_color() const { return c_.cs.jpeg_color_space; }
  int max_v_samp_factor() const { return c_.cs.max_v_samp_factor; }
  int max_h_samp_factor() const { return c_.cs.max_h_samp_factor; }

  // ---- compression ----------------------------------------------------------
  void start(bool write_all_tables = true) { c_.start_compress(write_all_tables); }

  void write_marker(int marker, std::span<const std::uint8_t> data) {
    c_.write_marker(marker, data.data(), static_cast<unsigned>(data.size()));
  }
  void write_icc_profile(std::span<const std::uint8_t> icc) {
    c_.write_icc_profile(icc.data(), static_cast<unsigned>(icc.size()));
  }

  template <class T>
  unsigned write_scanlines(T** rows, unsigned num_lines) {
    return c_.write_scanlines<T>(rows, num_lines);
  }
  template <class T>
  unsigned write_raw_data(T*** planes, unsigned num_lines) {
    return c_.write_raw_data<T>(planes, num_lines);
  }
  unsigned next_scanline() const { return c_.cs.next_scanline; }

  // Completes compression and returns the JPEG datastream.
  std::vector<std::uint8_t> finish() {
    c_.finish_compress();
    return c_.take_output();
  }

  // Emits a tables-only (abbreviated) datastream.
  std::vector<std::uint8_t> write_tables() {
    c_.write_tables();
    return c_.take_output();
  }

  detail::Compressor& core() { return c_; }

 private:
  detail::Compressor c_;
};

}  // namespace vio::jpeg
