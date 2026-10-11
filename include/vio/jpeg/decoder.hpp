// vio::jpeg::Decoder - JPEG decompression (baseline, extended, progressive,
// arithmetic-coded and lossless; 2..16-bit samples).
//
//   vio::jpeg::Decoder dec(bytes);            // parses the header
//   dec.set_output_color(ColorSpace::RGB).set_scale(1, 2);
//   dec.start();
//   std::vector<uint8_t> row(dec.output_width() * dec.output_components());
//   while (dec.output_scanline() < dec.output_height()) {
//     uint8_t* r = row.data();
//     dec.read_scanlines(&r, 1);
//   }
//   dec.finish();
#pragma once

#include <span>

#include "detail/decompress.hpp"

namespace vio::jpeg {

struct ComponentHeader {
  int id = 0;
  int h_samp_factor = 1;
  int v_samp_factor = 1;
  int quant_tbl_no = 0;
};

struct Header {
  unsigned width = 0, height = 0;
  int num_components = 0;
  int precision = 8;
  ColorSpace color_space = ColorSpace::Unknown;
  bool progressive = false, arithmetic = false, lossless = false, baseline = false;
  bool has_multiple_scans = false;
  int max_h_samp_factor = 1, max_v_samp_factor = 1;
  std::vector<ComponentHeader> components;
  bool saw_jfif = false, saw_adobe = false;
  int adobe_transform = 0;
  int jfif_major = 1, jfif_minor = 1, density_unit = 0, x_density = 1, y_density = 1;
  unsigned restart_interval = 0;
};

class Decoder {
 public:
  Decoder() = default;
  explicit Decoder(std::span<const std::uint8_t> data) { open(data); }

  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  // Request that APPn/COM markers be retained (call before open()).
  Decoder& save_markers(int marker_code, unsigned length_limit = 0xFFFF) {
    d_.st.save_markers(marker_code, length_limit);
    return *this;
  }

  Diagnostics& diagnostics() { return d_.diag; }

  // Attach a JPEG datastream and parse its header.  The data must stay alive
  // until decoding completes.  Returns false for a tables-only datastream
  // (whose tables are retained for a subsequent abbreviated image).
  bool open(std::span<const std::uint8_t> data, bool require_image = true) {
    d_.st.marker_list.clear();
    d_.set_source(data.data(), data.size());
    return d_.read_header(require_image);
  }

  Header header() const {
    const detail::DState& s = d_.st;
    Header h;
    h.width = s.image_width;
    h.height = s.image_height;
    h.num_components = s.num_components;
    h.precision = s.jpeg_data_precision;
    h.color_space = s.jpeg_color_space;
    h.progressive = s.progressive_mode;
    h.arithmetic = s.arith_code;
    h.lossless = s.lossless;
    h.baseline = s.is_baseline;
    h.has_multiple_scans = s.has_multiple_scans;
    h.max_h_samp_factor = s.max_h_samp_factor;
    h.max_v_samp_factor = s.max_v_samp_factor;
    for (const auto& c : s.comp_info)
      h.components.push_back({c.component_id, c.h_samp_factor, c.v_samp_factor, c.quant_tbl_no});
    h.saw_jfif = s.saw_JFIF_marker;
    h.saw_adobe = s.saw_Adobe_marker;
    h.adobe_transform = s.Adobe_transform;
    h.jfif_major = s.JFIF_major_version;
    h.jfif_minor = s.JFIF_minor_version;
    h.density_unit = s.density_unit;
    h.x_density = s.X_density;
    h.y_density = s.Y_density;
    h.restart_interval = s.restart_interval;
    return h;
  }

  // ---- decompression parameters (valid between open() and start()) ------
  Decoder& set_output_color(ColorSpace cs) { d_.st.out_color_space = cs; return *this; }
  Decoder& set_jpeg_color(ColorSpace cs) { d_.st.jpeg_color_space = cs; return *this; }
  Decoder& set_scale(unsigned num, unsigned denom) {
    d_.st.scale_num = num;
    d_.st.scale_denom = denom;
    return *this;
  }
  Decoder& set_dct_method(DctMethod m) { d_.st.dct_method = m; return *this; }
  Decoder& set_fancy_upsampling(bool on) { d_.st.do_fancy_upsampling = on; return *this; }
  Decoder& set_block_smoothing(bool on) { d_.st.do_block_smoothing = on; return *this; }
  Decoder& set_raw_output(bool on) { d_.st.raw_data_out = on; return *this; }
  // Output sample precision (defaults to the image precision; an 8-bit
  // lossy image may be decoded to 12-bit samples).
  Decoder& set_output_precision(int bits) { d_.st.data_precision = bits; return *this; }
  Decoder& set_quantize(int colors, DitherMode dither, bool two_pass) {
    d_.st.quantize_colors = true;
    d_.st.desired_number_of_colors = colors;
    d_.st.dither_mode = dither;
    d_.st.two_pass_quantize = two_pass;
    return *this;
  }
  Decoder& set_dither(DitherMode dither) { d_.st.dither_mode = dither; return *this; }
  Decoder& set_quantize_colors(bool on) { d_.st.quantize_colors = on; return *this; }
  Decoder& set_desired_colors(int n) { d_.st.desired_number_of_colors = n; return *this; }
  Decoder& set_two_pass_quantize(bool on) { d_.st.two_pass_quantize = on; return *this; }
  bool quantize_colors() const { return d_.st.quantize_colors; }
  Decoder& set_colormap(std::vector<std::vector<int>> cmap) {
    d_.st.quantize_colors = true;
    d_.st.colormap = std::move(cmap);
    d_.st.colormap_external = true;
    return *this;
  }

  // Computes output dimensions for the current parameters without starting.
  void calc_output_dimensions() { d_.calc_output_dimensions(); }

  // ---- decompression --------------------------------------------------------
  void start() { d_.start_decompress(); }

  unsigned output_width() const { return d_.st.output_width; }
  unsigned output_height() const { return d_.st.output_height; }
  int output_components() const { return d_.st.output_components; }
  int out_color_components() const { return d_.st.out_color_components; }
  ColorSpace output_color() const { return d_.st.out_color_space; }
  int output_precision() const { return d_.st.data_precision; }
  unsigned output_scanline() const { return d_.st.output_scanline; }
  int rec_outbuf_height() const { return d_.st.rec_outbuf_height; }
  int min_dct_scaled_size() const { return d_.st.min_DCT_scaled_size; }
  int max_v_samp_factor() const { return d_.st.max_v_samp_factor; }
  int max_h_samp_factor() const { return d_.st.max_h_samp_factor; }
  // Per-component scaled block size and downsampled dimensions (raw output)
  int component_dct_scaled_size(int ci) const { return d_.st.comp_info[ci].DCT_scaled_size; }
  unsigned component_width_in_blocks(int ci) const { return d_.st.comp_info[ci].width_in_blocks; }
  unsigned component_downsampled_width(int ci) const {
    return d_.st.comp_info[ci].downsampled_width;
  }
  unsigned component_downsampled_height(int ci) const {
    return d_.st.comp_info[ci].downsampled_height;
  }
  // Colormap ([component][index]) after start() when quantizing.
  const std::vector<std::vector<int>>& colormap() const { return d_.st.colormap; }
  int actual_number_of_colors() const { return d_.st.actual_number_of_colors; }

  template <class T>
  void crop_scanline(unsigned& xoffset, unsigned& width) {
    d_.crop_scanline<T>(&xoffset, &width);
  }
  void crop_scanline(unsigned& xoffset, unsigned& width) {
    dispatch([&](auto t) { d_.crop_scanline<decltype(t)>(&xoffset, &width); });
  }

  template <class T>
  unsigned read_scanlines(T** rows, unsigned max_lines) {
    return d_.read_scanlines<T>(rows, max_lines);
  }

  unsigned skip_scanlines(unsigned n) {
    unsigned r = 0;
    dispatch([&](auto t) { r = d_.skip_scanlines<decltype(t)>(n); });
    return r;
  }

  template <class T>
  unsigned read_raw_data(T*** planes, unsigned max_lines) {
    return d_.read_raw_data<T>(planes, max_lines);
  }

  void finish() { d_.finish_decompress(); }
  void abort() { d_.abort(); }

  // ---- metadata -------------------------------------------------------------
  const std::vector<SavedMarker>& markers() const { return d_.st.marker_list; }

  // Reassembles an ICC profile from saved APP2 markers (requires
  // save_markers(M_APP2) before open()).  Returns an empty vector if none.
  std::vector<std::uint8_t> icc_profile() {
    constexpr int OVERHEAD = 14;
    auto is_icc = [](const SavedMarker& m) {
      static const char id[12] = {'I', 'C', 'C', '_', 'P', 'R', 'O', 'F', 'I', 'L', 'E', 0};
      return m.marker == M_APP2 && m.data.size() >= OVERHEAD &&
             std::memcmp(m.data.data(), id, 12) == 0;
    };
    int num_markers = 0;
    bool present[256] = {};
    unsigned length[256] = {}, offset[256] = {};
    auto bogus = [&] {
      d_.st.warn("Corrupt JPEG data: bad ICC marker");
      return std::vector<std::uint8_t>();
    };
    for (const auto& m : d_.st.marker_list) {
      if (!is_icc(m)) continue;
      if (num_markers == 0) num_markers = m.data[13];
      else if (num_markers != m.data[13]) return bogus();
      int seq = m.data[12];
      if (seq <= 0 || seq > num_markers) return bogus();
      if (present[seq]) return bogus();
      present[seq] = true;
      length[seq] = static_cast<unsigned>(m.data.size() - OVERHEAD);
    }
    if (num_markers == 0) return {};
    unsigned total = 0;
    for (int seq = 1; seq <= num_markers; seq++) {
      if (!present[seq]) return bogus();
      offset[seq] = total;
      total += length[seq];
    }
    if (total == 0) return bogus();
    std::vector<std::uint8_t> out(total);
    for (const auto& m : d_.st.marker_list)
      if (is_icc(m)) {
        int seq = m.data[12];
        std::memcpy(out.data() + offset[seq], m.data.data() + OVERHEAD, length[seq]);
      }
    return out;
  }

  // Low-level access for advanced users (transcoding, compatibility layers).
  detail::Decompressor& core() { return d_; }

 private:
  template <class F>
  void dispatch(F&& f) {
    int p = d_.st.data_precision;
    if (p <= 8) f(std::uint8_t{});
    else if (p <= 12) f(std::int16_t{});
    else f(std::uint16_t{});
  }

  detail::Decompressor d_;
};

}  // namespace vio::jpeg
