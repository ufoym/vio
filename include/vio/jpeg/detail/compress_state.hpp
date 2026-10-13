// vio::jpeg - compressor state, destination, marker writer, parameter setup
// and master control.
#pragma once

#include "component.hpp"
#include "../simd/cpu.hpp"

namespace vio::jpeg::detail {

struct ScanInfo {
  int comps_in_scan = 0;
  int component_index[MAX_COMPS_IN_SCAN]{};
  int Ss = 0, Se = 0, Ah = 0, Al = 0;
};

// Growable in-memory destination (jpeg_mem_dest equivalent).
class ByteSink {
 public:
  ByteSink() { reset(); }

  // Starts a datastream: in the buffer given to use_buffer() since the last
  // reset (while the data fit, after which it moves to an internal one), or
  // in the internal buffer.
  void reset() {
    if (ext_) {
      base_ = ext_;
      next = ext_;
      free_in_buffer = ext_size_;
      ext_ = nullptr;
      return;
    }
    if (buf_.size() < 4096) buf_.resize(4096);
    base_ = buf_.data();
    next = base_;
    free_in_buffer = buf_.size();
  }
  void use_buffer(std::uint8_t* p, std::size_t n) {
    ext_ = n ? p : nullptr;
    ext_size_ = n;
  }

  // Called when free_in_buffer reaches 0: enlarges the buffer.
  void empty_output_buffer() { grow(capacity()); }

  void ensure(std::size_t n) {
    if (free_in_buffer < n) grow(n);
  }

  VIO_INLINE void put(int v) {
    *next++ = static_cast<std::uint8_t>(v);
    if (--free_in_buffer == 0) empty_output_buffer();
  }

  std::size_t size() const { return static_cast<std::size_t>(next - base_); }
  std::vector<std::uint8_t> take() {
    std::vector<std::uint8_t> out;
    if (base_ == buf_.data()) {
      buf_.resize(size());
      out = std::move(buf_);
      buf_.clear();
    } else {
      out.assign(base_, next);
    }
    reset();
    return out;
  }
  const std::uint8_t* data() const { return base_; }

  std::uint8_t* next = nullptr;
  std::size_t free_in_buffer = 0;

 private:
  std::size_t capacity() const { return size() + free_in_buffer; }
  void grow(std::size_t extra) {
    std::size_t used = size();
    std::size_t nsize = std::max(capacity() * 2, used + extra + 4096);
    if (base_ != buf_.data()) {  // leave the caller's buffer
      std::vector<std::uint8_t> nb(nsize);
      std::memcpy(nb.data(), base_, used);
      buf_ = std::move(nb);
    } else {
      buf_.resize(nsize);
    }
    base_ = buf_.data();
    next = base_ + used;
    free_in_buffer = nsize - used;
  }
  std::vector<std::uint8_t> buf_;
  std::uint8_t* base_ = nullptr;
  std::uint8_t* ext_ = nullptr;
  std::size_t ext_size_ = 0;
};

inline constexpr unsigned std_luminance_quant_tbl[DCTSIZE2] = {
    16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
    14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
inline constexpr unsigned std_chrominance_quant_tbl[DCTSIZE2] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99,
    99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

inline int quality_scaling(int quality) {
  if (quality <= 0) quality = 1;
  if (quality > 100) quality = 100;
  return quality < 50 ? 5000 / quality : 200 - quality * 2;
}

struct CState {
  Diagnostics* diag = nullptr;
  // SIMD instruction sets this codec may use (fixed at construction).
  vio::jpeg::simd::Support simd = vio::jpeg::simd::support();
  ByteSink dest;

  // ---- source image description ----
  unsigned image_width = 0, image_height = 0;
  int input_components = 0;
  ColorSpace in_color_space = ColorSpace::Unknown;

  // ---- compression parameters ----
  int data_precision = 8;
  int num_components = 0;
  ColorSpace jpeg_color_space = ColorSpace::Unknown;
  std::vector<ComponentInfo> comp_info = std::vector<ComponentInfo>(MAX_COMPONENTS);
  std::array<std::optional<QuantTable>, NUM_QUANT_TBLS> quant_tbl_ptrs;
  int q_scale_factor[NUM_QUANT_TBLS] = {100, 100, 100, 100};
  std::array<std::optional<HuffTable>, NUM_HUFF_TBLS> dc_huff_tbl_ptrs, ac_huff_tbl_ptrs;
  std::uint8_t arith_dc_L[NUM_ARITH_TBLS]{};
  std::uint8_t arith_dc_U[NUM_ARITH_TBLS]{};
  std::uint8_t arith_ac_K[NUM_ARITH_TBLS]{};
  std::vector<ScanInfo> scan_info;  // empty == single sequential scan
  int num_scans = 0;
  bool raw_data_in = false;
  bool arith_code = false;
  bool optimize_coding = false;
  bool CCIR601_sampling = false;
  int smoothing_factor = 0;
  DctMethod dct_method = DctMethod::IntSlow;
  unsigned restart_interval = 0;
  int restart_in_rows = 0;
  bool write_JFIF_header = false;
  std::uint8_t JFIF_major_version = 1, JFIF_minor_version = 1;
  std::uint8_t density_unit = 0;
  std::uint16_t X_density = 1, Y_density = 1;
  bool write_Adobe_marker = false;

  unsigned next_scanline = 0;

  // ---- derived state ----
  bool progressive_mode = false;
  bool lossless = false;
  int max_h_samp_factor = 1, max_v_samp_factor = 1;
  unsigned total_iMCU_rows = 0;
  int comps_in_scan = 0;
  ComponentInfo* cur_comp_info[MAX_COMPS_IN_SCAN]{};
  unsigned MCUs_per_row = 0, MCU_rows_in_scan = 0;
  int blocks_in_MCU = 0;
  int MCU_membership[C_MAX_BLOCKS_IN_MCU]{};
  int Ss = 0, Se = 0, Ah = 0, Al = 0;

  // ---- master control ----
  enum PassType { main_pass, huff_opt_pass, output_pass };
  PassType pass_type = main_pass;
  int pass_number = 0, total_passes = 0, scan_number = 0;
  bool call_pass_startup = false;
  bool is_last_pass = false;

  unsigned last_restart_interval = 0;

  void warn(const std::string& msg) {
    if (diag) diag->warn(msg);
  }

  // ===========================================================================
  // Parameter setup
  void add_quant_table(int which_tbl, const unsigned* basic_table, int scale_factor,
                       bool force_baseline) {
    if (which_tbl < 0 || which_tbl >= NUM_QUANT_TBLS) VIO_FAIL("Bogus DQT index %d", which_tbl);
    auto& q = quant_tbl_ptrs[which_tbl];
    if (!q) q.emplace();
    for (int i = 0; i < DCTSIZE2; i++) {
      long temp = (static_cast<long>(basic_table[i]) * scale_factor + 50L) / 100L;
      if (temp <= 0L) temp = 1L;
      if (temp > 32767L) temp = 32767L;
      if (force_baseline && temp > 255L) temp = 255L;
      q->quantval[i] = static_cast<std::uint16_t>(temp);
    }
    q->sent_table = false;
  }

  void set_linear_quality(int scale_factor, bool force_baseline) {
    add_quant_table(0, std_luminance_quant_tbl, scale_factor, force_baseline);
    add_quant_table(1, std_chrominance_quant_tbl, scale_factor, force_baseline);
  }

  void set_quality(int quality, bool force_baseline) {
    set_linear_quality(quality_scaling(quality), force_baseline);
  }

  void set_defaults() {
    set_quality(75, true);
    std_huff_tables(dc_huff_tbl_ptrs, ac_huff_tbl_ptrs, false);
    for (int i = 0; i < NUM_ARITH_TBLS; i++) {
      arith_dc_L[i] = 0;
      arith_dc_U[i] = 1;
      arith_ac_K[i] = 5;
    }
    scan_info.clear();
    num_scans = 0;
    lossless = false;
    raw_data_in = false;
    arith_code = false;
    optimize_coding = data_precision == 12;
    CCIR601_sampling = false;
    smoothing_factor = 0;
    dct_method = DctMethod::IntSlow;
    restart_interval = 0;
    restart_in_rows = 0;
    JFIF_major_version = 1;
    JFIF_minor_version = 1;
    density_unit = 0;
    X_density = 1;
    Y_density = 1;
    default_colorspace();
  }

  void default_colorspace() {
    switch (in_color_space) {
      case ColorSpace::Gray: set_colorspace(ColorSpace::Gray); break;
      case ColorSpace::RGB: case ColorSpace::ExtRGB: case ColorSpace::ExtRGBX:
      case ColorSpace::ExtBGR: case ColorSpace::ExtBGRX: case ColorSpace::ExtXBGR:
      case ColorSpace::ExtXRGB: case ColorSpace::ExtRGBA: case ColorSpace::ExtBGRA:
      case ColorSpace::ExtABGR: case ColorSpace::ExtARGB:
        set_colorspace(lossless ? ColorSpace::RGB : ColorSpace::YCbCr);
        break;
      case ColorSpace::YCbCr: set_colorspace(ColorSpace::YCbCr); break;
      case ColorSpace::CMYK: set_colorspace(ColorSpace::CMYK); break;
      case ColorSpace::YCCK: set_colorspace(ColorSpace::YCCK); break;
      case ColorSpace::Unknown: set_colorspace(ColorSpace::Unknown); break;
      default: VIO_FAIL("Bogus input colorspace");
    }
  }

  void set_comp(int index, int id, int hsamp, int vsamp, int quant, int dctbl, int actbl) {
    ComponentInfo& c = comp_info[index];
    c.component_id = id;
    c.h_samp_factor = hsamp;
    c.v_samp_factor = vsamp;
    c.quant_tbl_no = quant;
    c.dc_tbl_no = dctbl;
    c.ac_tbl_no = actbl;
  }

  void set_colorspace(ColorSpace colorspace) {
    jpeg_color_space = colorspace;
    write_JFIF_header = false;
    write_Adobe_marker = false;
    switch (colorspace) {
      case ColorSpace::Gray:
        write_JFIF_header = true;
        num_components = 1;
        set_comp(0, 1, 1, 1, 0, 0, 0);
        break;
      case ColorSpace::RGB:
        write_Adobe_marker = true;
        num_components = 3;
        set_comp(0, 0x52, 1, 1, 0, 0, 0);
        set_comp(1, 0x47, 1, 1, 0, 0, 0);
        set_comp(2, 0x42, 1, 1, 0, 0, 0);
        break;
      case ColorSpace::YCbCr:
        write_JFIF_header = true;
        num_components = 3;
        set_comp(0, 1, 2, 2, 0, 0, 0);
        set_comp(1, 2, 1, 1, 1, 1, 1);
        set_comp(2, 3, 1, 1, 1, 1, 1);
        break;
      case ColorSpace::CMYK:
        write_Adobe_marker = true;
        num_components = 4;
        set_comp(0, 0x43, 1, 1, 0, 0, 0);
        set_comp(1, 0x4D, 1, 1, 0, 0, 0);
        set_comp(2, 0x59, 1, 1, 0, 0, 0);
        set_comp(3, 0x4B, 1, 1, 0, 0, 0);
        break;
      case ColorSpace::YCCK:
        write_Adobe_marker = true;
        num_components = 4;
        set_comp(0, 1, 2, 2, 0, 0, 0);
        set_comp(1, 2, 1, 1, 1, 1, 1);
        set_comp(2, 3, 1, 1, 1, 1, 1);
        set_comp(3, 4, 2, 2, 0, 0, 0);
        break;
      case ColorSpace::Unknown:
        num_components = input_components;
        if (num_components < 1 || num_components > MAX_COMPONENTS)
          VIO_FAIL("Too many color components: %d, max %d", num_components, MAX_COMPONENTS);
        for (int ci = 0; ci < num_components; ci++) set_comp(ci, ci, 1, 1, 0, 0, 0);
        break;
      default:
        VIO_FAIL("Bogus JPEG colorspace");
    }
  }

  void simple_progression() {
    if (lossless) {
      lossless = false;
      default_colorspace();
    }
    int ncomps = num_components;
    scan_info.clear();
    auto fill_a_scan = [&](int ci, int ss, int se, int ah, int al) {
      ScanInfo s;
      s.comps_in_scan = 1;
      s.component_index[0] = ci;
      s.Ss = ss; s.Se = se; s.Ah = ah; s.Al = al;
      scan_info.push_back(s);
    };
    auto fill_scans = [&](int ss, int se, int ah, int al) {
      for (int ci = 0; ci < ncomps; ci++) fill_a_scan(ci, ss, se, ah, al);
    };
    auto fill_dc_scans = [&](int ah, int al) {
      if (ncomps <= MAX_COMPS_IN_SCAN) {
        ScanInfo s;
        s.comps_in_scan = ncomps;
        for (int ci = 0; ci < ncomps; ci++) s.component_index[ci] = ci;
        s.Ss = s.Se = 0;
        s.Ah = ah;
        s.Al = al;
        scan_info.push_back(s);
      } else {
        fill_scans(0, 0, ah, al);
      }
    };
    if (ncomps == 3 && jpeg_color_space == ColorSpace::YCbCr) {
      fill_dc_scans(0, 1);
      fill_a_scan(0, 1, 5, 0, 2);
      fill_a_scan(2, 1, 63, 0, 1);
      fill_a_scan(1, 1, 63, 0, 1);
      fill_a_scan(0, 6, 63, 0, 2);
      fill_a_scan(0, 1, 63, 2, 1);
      fill_dc_scans(1, 0);
      fill_a_scan(2, 1, 63, 1, 0);
      fill_a_scan(1, 1, 63, 1, 0);
      fill_a_scan(0, 1, 63, 1, 0);
    } else {
      fill_dc_scans(0, 1);
      fill_scans(1, 5, 0, 2);
      fill_scans(6, 63, 0, 2);
      fill_scans(1, 63, 2, 1);
      fill_dc_scans(1, 0);
      fill_scans(1, 63, 1, 0);
    }
    num_scans = static_cast<int>(scan_info.size());
  }

  void enable_lossless(int predictor_selection_value, int point_transform) {
    lossless = true;
    Ss = predictor_selection_value;
    Se = 0;
    Ah = 0;
    Al = point_transform;
    if (Ss < 1 || Ss > 7 || Al < 0 || Al >= data_precision)
      VIO_FAIL("Invalid progressive/lossless parameters Ss=%d Se=%d Ah=%d Al=%d", Ss, Se, Ah, Al);
  }

  void suppress_tables(bool suppress) {
    for (auto& q : quant_tbl_ptrs)
      if (q) q->sent_table = suppress;
    for (int i = 0; i < NUM_HUFF_TBLS; i++) {
      if (dc_huff_tbl_ptrs[i]) dc_huff_tbl_ptrs[i]->sent_table = suppress;
      if (ac_huff_tbl_ptrs[i]) ac_huff_tbl_ptrs[i]->sent_table = suppress;
    }
  }

  // ===========================================================================
  // Marker writer
  void emit_byte(int v) { dest.put(v); }
  void emit_marker(int mark) {
    emit_byte(0xFF);
    emit_byte(mark);
  }
  void emit_2bytes(int value) {
    emit_byte((value >> 8) & 0xFF);
    emit_byte(value & 0xFF);
  }

  int emit_dqt(int index) {
    auto& q = quant_tbl_ptrs[index];
    if (!q) VIO_FAIL("Quantization table 0x%02x was not defined", index);
    int prec = 0;
    for (int i = 0; i < DCTSIZE2; i++)
      if (q->quantval[i] > 255) prec = 1;
    if (!q->sent_table) {
      emit_marker(M_DQT);
      emit_2bytes(prec ? DCTSIZE2 * 2 + 1 + 2 : DCTSIZE2 + 1 + 2);
      emit_byte(index + (prec << 4));
      for (int i = 0; i < DCTSIZE2; i++) {
        unsigned qval = q->quantval[natural_order[i]];
        if (prec) emit_byte(static_cast<int>(qval >> 8));
        emit_byte(static_cast<int>(qval & 0xFF));
      }
      q->sent_table = true;
    }
    return prec;
  }

  void emit_dht(int index, bool is_ac) {
    auto& h = is_ac ? ac_huff_tbl_ptrs[index] : dc_huff_tbl_ptrs[index];
    if (is_ac) index += 0x10;
    if (!h) VIO_FAIL("Huffman table 0x%02x was not defined", index);
    if (!h->sent_table) {
      emit_marker(M_DHT);
      int length = 0;
      for (int i = 1; i <= 16; i++) length += h->bits[i];
      emit_2bytes(length + 2 + 1 + 16);
      emit_byte(index);
      for (int i = 1; i <= 16; i++) emit_byte(h->bits[i]);
      for (int i = 0; i < length; i++) emit_byte(h->huffval[i]);
      h->sent_table = true;
    }
  }

  void emit_dac() {
    char dc_in_use[NUM_ARITH_TBLS] = {}, ac_in_use[NUM_ARITH_TBLS] = {};
    for (int i = 0; i < comps_in_scan; i++) {
      ComponentInfo* c = cur_comp_info[i];
      if (Ss == 0 && Ah == 0) dc_in_use[c->dc_tbl_no] = 1;
      if (Se) ac_in_use[c->ac_tbl_no] = 1;
    }
    int length = 0;
    for (int i = 0; i < NUM_ARITH_TBLS; i++) length += dc_in_use[i] + ac_in_use[i];
    if (length) {
      emit_marker(M_DAC);
      emit_2bytes(length * 2 + 2);
      for (int i = 0; i < NUM_ARITH_TBLS; i++) {
        if (dc_in_use[i]) {
          emit_byte(i);
          emit_byte(arith_dc_L[i] + (arith_dc_U[i] << 4));
        }
        if (ac_in_use[i]) {
          emit_byte(i + 0x10);
          emit_byte(arith_ac_K[i]);
        }
      }
    }
  }

  void emit_dri() {
    emit_marker(M_DRI);
    emit_2bytes(4);
    emit_2bytes(static_cast<int>(restart_interval));
  }

  void emit_sof(int code) {
    emit_marker(code);
    emit_2bytes(3 * num_components + 2 + 5 + 1);
    if (image_height > 65535u || image_width > 65535u)
      VIO_FAIL("Maximum supported image dimension is %u pixels", 65535u);
    emit_byte(data_precision);
    emit_2bytes(static_cast<int>(image_height));
    emit_2bytes(static_cast<int>(image_width));
    emit_byte(num_components);
    for (int ci = 0; ci < num_components; ci++) {
      const ComponentInfo& c = comp_info[ci];
      emit_byte(c.component_id);
      emit_byte((c.h_samp_factor << 4) + c.v_samp_factor);
      emit_byte(c.quant_tbl_no);
    }
  }

  void emit_sos() {
    emit_marker(M_SOS);
    emit_2bytes(2 * comps_in_scan + 2 + 1 + 3);
    emit_byte(comps_in_scan);
    for (int i = 0; i < comps_in_scan; i++) {
      ComponentInfo* c = cur_comp_info[i];
      emit_byte(c->component_id);
      int td = Ss == 0 && Ah == 0 ? c->dc_tbl_no : 0;
      int ta = Se ? c->ac_tbl_no : 0;
      emit_byte((td << 4) + ta);
    }
    emit_byte(Ss);
    emit_byte(Se);
    emit_byte((Ah << 4) + Al);
  }

  void emit_jfif_app0() {
    emit_marker(M_APP0);
    emit_2bytes(2 + 4 + 1 + 2 + 1 + 2 + 2 + 1 + 1);
    emit_byte(0x4A);
    emit_byte(0x46);
    emit_byte(0x49);
    emit_byte(0x46);
    emit_byte(0);
    emit_byte(JFIF_major_version);
    emit_byte(JFIF_minor_version);
    emit_byte(density_unit);
    emit_2bytes(X_density);
    emit_2bytes(Y_density);
    emit_byte(0);
    emit_byte(0);
  }

  void emit_adobe_app14() {
    emit_marker(M_APP14);
    emit_2bytes(2 + 5 + 2 + 2 + 2 + 1);
    emit_byte(0x41);
    emit_byte(0x64);
    emit_byte(0x6F);
    emit_byte(0x62);
    emit_byte(0x65);
    emit_2bytes(100);
    emit_2bytes(0);
    emit_2bytes(0);
    switch (jpeg_color_space) {
      case ColorSpace::YCbCr: emit_byte(1); break;
      case ColorSpace::YCCK: emit_byte(2); break;
      default: emit_byte(0);
    }
  }

  void write_marker_header(int marker, unsigned datalen) {
    if (datalen > 65533u) VIO_FAIL("Bogus marker length");
    emit_marker(marker);
    emit_2bytes(static_cast<int>(datalen + 2));
  }

  void write_file_header() {
    emit_marker(M_SOI);
    last_restart_interval = 0;
    if (write_JFIF_header) emit_jfif_app0();
    if (write_Adobe_marker) emit_adobe_app14();
  }

  void write_frame_header() {
    int prec = 0;
    if (!lossless)
      for (int ci = 0; ci < num_components; ci++) prec += emit_dqt(comp_info[ci].quant_tbl_no);
    bool is_baseline;
    if (arith_code || progressive_mode || lossless || data_precision != 8) {
      is_baseline = false;
    } else {
      is_baseline = true;
      for (int ci = 0; ci < num_components; ci++)
        if (comp_info[ci].dc_tbl_no > 1 || comp_info[ci].ac_tbl_no > 1) is_baseline = false;
      if (prec && is_baseline) is_baseline = false;
    }
    if (arith_code) emit_sof(progressive_mode ? M_SOF10 : M_SOF9);
    else if (progressive_mode) emit_sof(M_SOF2);
    else if (lossless) emit_sof(M_SOF3);
    else if (is_baseline) emit_sof(M_SOF0);
    else emit_sof(M_SOF1);
  }

  void write_scan_header() {
    if (arith_code) {
      emit_dac();
    } else {
      for (int i = 0; i < comps_in_scan; i++) {
        ComponentInfo* c = cur_comp_info[i];
        if ((Ss == 0 && Ah == 0) || lossless) emit_dht(c->dc_tbl_no, false);
        if (Se && !lossless) emit_dht(c->ac_tbl_no, true);
      }
    }
    if (restart_interval != last_restart_interval) {
      emit_dri();
      last_restart_interval = restart_interval;
    }
    emit_sos();
  }

  void write_file_trailer() { emit_marker(M_EOI); }

  void write_tables_only() {
    emit_marker(M_SOI);
    for (int i = 0; i < NUM_QUANT_TBLS; i++)
      if (quant_tbl_ptrs[i]) emit_dqt(i);
    if (!arith_code)
      for (int i = 0; i < NUM_HUFF_TBLS; i++) {
        if (dc_huff_tbl_ptrs[i]) emit_dht(i, false);
        if (ac_huff_tbl_ptrs[i]) emit_dht(i, true);
      }
    emit_marker(M_EOI);
  }

  // ===========================================================================
  // Master control
  bool using_std_huff_tables() const {
    if (!dc_huff_tbl_ptrs[0] || !ac_huff_tbl_ptrs[0] || !dc_huff_tbl_ptrs[1] ||
        !ac_huff_tbl_ptrs[1])
      return false;
    for (int i = 2; i < NUM_HUFF_TBLS; i++)
      if (dc_huff_tbl_ptrs[i] || ac_huff_tbl_ptrs[i]) return false;
    auto same = [](const HuffTable& t, const std::uint8_t* bits, const std::uint8_t* val,
                   std::size_t nval) {
      return std::memcmp(t.bits.data(), bits, 17) == 0 &&
             std::memcmp(t.huffval.data(), val, nval) == 0;
    };
    return same(*dc_huff_tbl_ptrs[0], bits_dc_luminance, val_dc_luminance,
                sizeof(val_dc_luminance)) &&
           same(*ac_huff_tbl_ptrs[0], bits_ac_luminance, val_ac_luminance,
                sizeof(val_ac_luminance)) &&
           same(*dc_huff_tbl_ptrs[1], bits_dc_chrominance, val_dc_chrominance,
                sizeof(val_dc_chrominance)) &&
           same(*ac_huff_tbl_ptrs[1], bits_ac_chrominance, val_ac_chrominance,
                sizeof(val_ac_chrominance));
  }

  void initial_setup() {
    int data_unit = lossless ? 1 : DCTSIZE;
    if (image_height <= 0 || image_width <= 0 || num_components <= 0 || input_components <= 0)
      VIO_FAIL("Empty JPEG image (DNL not supported)");
    if (image_height > JPEG_MAX_DIMENSION || image_width > JPEG_MAX_DIMENSION)
      VIO_FAIL("Maximum supported image dimension is %u pixels", JPEG_MAX_DIMENSION);
    long samplesperrow = static_cast<long>(image_width) * input_components;
    if (static_cast<long>(static_cast<unsigned>(samplesperrow)) != samplesperrow)
      VIO_FAIL("Image too wide for this implementation");
    if (lossless) {
      if (data_precision < 2 || data_precision > 16)
        VIO_FAIL("Unsupported JPEG data precision %d", data_precision);
    } else if (data_precision != 8 && data_precision != 12) {
      VIO_FAIL("Unsupported JPEG data precision %d", data_precision);
    }
    if (num_components > MAX_COMPONENTS)
      VIO_FAIL("Too many color components: %d, max %d", num_components, MAX_COMPONENTS);
    max_h_samp_factor = max_v_samp_factor = 1;
    for (int ci = 0; ci < num_components; ci++) {
      ComponentInfo& c = comp_info[ci];
      if (c.h_samp_factor <= 0 || c.h_samp_factor > MAX_SAMP_FACTOR || c.v_samp_factor <= 0 ||
          c.v_samp_factor > MAX_SAMP_FACTOR)
        VIO_FAIL("Bogus sampling factors");
      max_h_samp_factor = std::max(max_h_samp_factor, c.h_samp_factor);
      max_v_samp_factor = std::max(max_v_samp_factor, c.v_samp_factor);
    }
    for (int ci = 0; ci < num_components; ci++) {
      ComponentInfo& c = comp_info[ci];
      c.component_index = ci;
      c.DCT_scaled_size = data_unit;
      c.width_in_blocks = static_cast<unsigned>(div_round_up(
          static_cast<long>(image_width) * c.h_samp_factor, max_h_samp_factor * data_unit));
      c.height_in_blocks = static_cast<unsigned>(div_round_up(
          static_cast<long>(image_height) * c.v_samp_factor, max_v_samp_factor * data_unit));
      c.downsampled_width = static_cast<unsigned>(
          div_round_up(static_cast<long>(image_width) * c.h_samp_factor, max_h_samp_factor));
      c.downsampled_height = static_cast<unsigned>(
          div_round_up(static_cast<long>(image_height) * c.v_samp_factor, max_v_samp_factor));
      c.component_needed = true;
    }
    total_iMCU_rows = static_cast<unsigned>(
        div_round_up(static_cast<long>(image_height), max_v_samp_factor * data_unit));
  }

  void validate_script() {
    if (num_scans <= 0) VIO_FAIL("Invalid scan script at entry %d", 0);
    const ScanInfo* scanptr = scan_info.data();
    bool component_sent[MAX_COMPONENTS] = {};
    int last_bitpos[MAX_COMPONENTS][DCTSIZE2];
    if (scanptr->Ss != 0 && scanptr->Se == 0) {
      lossless = true;
      progressive_mode = false;
    } else if (scanptr->Ss != 0 || scanptr->Se != DCTSIZE2 - 1) {
      progressive_mode = true;
      lossless = false;
      for (int ci = 0; ci < num_components; ci++)
        for (int k = 0; k < DCTSIZE2; k++) last_bitpos[ci][k] = -1;
    } else {
      progressive_mode = lossless = false;
    }
    for (int scanno = 1; scanno <= num_scans; scanptr++, scanno++) {
      int ncomps = scanptr->comps_in_scan;
      if (ncomps <= 0 || ncomps > MAX_COMPS_IN_SCAN)
        VIO_FAIL("Too many color components: %d, max %d", ncomps, MAX_COMPS_IN_SCAN);
      for (int ci = 0; ci < ncomps; ci++) {
        int thisi = scanptr->component_index[ci];
        if (thisi < 0 || thisi >= num_components)
          VIO_FAIL("Invalid scan script at entry %d", scanno);
        if (ci > 0 && thisi <= scanptr->component_index[ci - 1])
          VIO_FAIL("Invalid scan script at entry %d", scanno);
      }
      int Ss_ = scanptr->Ss, Se_ = scanptr->Se, Ah_ = scanptr->Ah, Al_ = scanptr->Al;
      if (progressive_mode) {
        int max_Ah_Al = data_precision == 12 ? 13 : 10;
        if (Ss_ < 0 || Ss_ >= DCTSIZE2 || Se_ < Ss_ || Se_ >= DCTSIZE2 || Ah_ < 0 ||
            Ah_ > max_Ah_Al || Al_ < 0 || Al_ > max_Ah_Al)
          VIO_FAIL("Invalid progressive/lossless parameters at scan script entry %d", scanno);
        if (Ss_ == 0) {
          if (Se_ != 0)
            VIO_FAIL("Invalid progressive/lossless parameters at scan script entry %d", scanno);
        } else if (ncomps != 1) {
          VIO_FAIL("Invalid progressive/lossless parameters at scan script entry %d", scanno);
        }
        for (int ci = 0; ci < ncomps; ci++) {
          int* lbp = last_bitpos[scanptr->component_index[ci]];
          if (Ss_ != 0 && lbp[0] < 0)
            VIO_FAIL("Invalid progressive/lossless parameters at scan script entry %d", scanno);
          for (int coefi = Ss_; coefi <= Se_; coefi++) {
            if (lbp[coefi] < 0) {
              if (Ah_ != 0)
                VIO_FAIL("Invalid progressive/lossless parameters at scan script entry %d",
                         scanno);
            } else if (Ah_ != lbp[coefi] || Al_ != Ah_ - 1) {
              VIO_FAIL("Invalid progressive/lossless parameters at scan script entry %d", scanno);
            }
            lbp[coefi] = Al_;
          }
        }
      } else {
        if (lossless) {
          if (Ss_ < 1 || Ss_ > 7 || Se_ != 0 || Ah_ != 0 || Al_ < 0 || Al_ >= data_precision)
            VIO_FAIL("Invalid progressive/lossless parameters at scan script entry %d", scanno);
        } else if (Ss_ != 0 || Se_ != DCTSIZE2 - 1 || Ah_ != 0 || Al_ != 0) {
          VIO_FAIL("Invalid progressive/lossless parameters at scan script entry %d", scanno);
        }
        for (int ci = 0; ci < ncomps; ci++) {
          int thisi = scanptr->component_index[ci];
          if (component_sent[thisi]) VIO_FAIL("Invalid scan script at entry %d", scanno);
          component_sent[thisi] = true;
        }
      }
    }
    if (progressive_mode) {
      for (int ci = 0; ci < num_components; ci++)
        if (last_bitpos[ci][0] < 0) VIO_FAIL("Scan script does not transmit all data");
    } else {
      for (int ci = 0; ci < num_components; ci++)
        if (!component_sent[ci]) VIO_FAIL("Scan script does not transmit all data");
    }
  }

  void select_scan_parameters() {
    if (!scan_info.empty()) {
      const ScanInfo& s = scan_info[scan_number];
      comps_in_scan = s.comps_in_scan;
      for (int ci = 0; ci < s.comps_in_scan; ci++)
        cur_comp_info[ci] = &comp_info[s.component_index[ci]];
      Ss = s.Ss;
      Se = s.Se;
      Ah = s.Ah;
      Al = s.Al;
    } else {
      if (num_components > MAX_COMPS_IN_SCAN)
        VIO_FAIL("Too many color components: %d, max %d", num_components, MAX_COMPS_IN_SCAN);
      comps_in_scan = num_components;
      for (int ci = 0; ci < num_components; ci++) cur_comp_info[ci] = &comp_info[ci];
      if (!lossless) {
        Ss = 0;
        Se = DCTSIZE2 - 1;
        Ah = 0;
        Al = 0;
      }
    }
  }

  void per_scan_setup() {
    int data_unit = lossless ? 1 : DCTSIZE;
    if (comps_in_scan == 1) {
      ComponentInfo* c = cur_comp_info[0];
      MCUs_per_row = c->width_in_blocks;
      MCU_rows_in_scan = c->height_in_blocks;
      c->MCU_width = 1;
      c->MCU_height = 1;
      c->MCU_blocks = 1;
      c->MCU_sample_width = data_unit;
      c->last_col_width = 1;
      int tmp = static_cast<int>(c->height_in_blocks % c->v_samp_factor);
      if (tmp == 0) tmp = c->v_samp_factor;
      c->last_row_height = tmp;
      blocks_in_MCU = 1;
      MCU_membership[0] = 0;
    } else {
      if (comps_in_scan <= 0 || comps_in_scan > MAX_COMPS_IN_SCAN)
        VIO_FAIL("Too many color components: %d, max %d", comps_in_scan, MAX_COMPS_IN_SCAN);
      MCUs_per_row = static_cast<unsigned>(
          div_round_up(static_cast<long>(image_width), max_h_samp_factor * data_unit));
      MCU_rows_in_scan = static_cast<unsigned>(
          div_round_up(static_cast<long>(image_height), max_v_samp_factor * data_unit));
      blocks_in_MCU = 0;
      for (int ci = 0; ci < comps_in_scan; ci++) {
        ComponentInfo* c = cur_comp_info[ci];
        c->MCU_width = c->h_samp_factor;
        c->MCU_height = c->v_samp_factor;
        c->MCU_blocks = c->MCU_width * c->MCU_height;
        c->MCU_sample_width = c->MCU_width * data_unit;
        int tmp = static_cast<int>(c->width_in_blocks % c->MCU_width);
        if (tmp == 0) tmp = c->MCU_width;
        c->last_col_width = tmp;
        tmp = static_cast<int>(c->height_in_blocks % c->MCU_height);
        if (tmp == 0) tmp = c->MCU_height;
        c->last_row_height = tmp;
        int mcublks = c->MCU_blocks;
        if (blocks_in_MCU + mcublks > C_MAX_BLOCKS_IN_MCU)
          VIO_FAIL("Sampling factors too large for interleaved scan");
        while (mcublks-- > 0) MCU_membership[blocks_in_MCU++] = ci;
      }
    }
    if (restart_in_rows > 0) {
      long nominal = static_cast<long>(restart_in_rows) * static_cast<long>(MCUs_per_row);
      restart_interval = static_cast<unsigned>(std::min(nominal, 65535L));
    }
  }

  // jinit_c_master_control()
  void init_master_control(bool transcode_only) {
    is_last_pass = false;
    if (!scan_info.empty()) {
      num_scans = static_cast<int>(scan_info.size());
      validate_script();
    } else {
      progressive_mode = false;
      num_scans = 1;
    }
    if (lossless) {
      raw_data_in = false;
      smoothing_factor = 0;
      default_colorspace();
      for (int ci = 0; ci < num_components; ci++)
        comp_info[ci].h_samp_factor = comp_info[ci].v_samp_factor = 1;
    }
    initial_setup();
    if (arith_code) {
      optimize_coding = false;
    } else {
      if (lossless || progressive_mode) optimize_coding = true;
      bool empty_huff_tables = true;
      for (int i = 0; i < NUM_HUFF_TBLS; i++)
        if (dc_huff_tbl_ptrs[i] || ac_huff_tbl_ptrs[i]) {
          empty_huff_tables = false;
          break;
        }
      if (data_precision == 12 && !optimize_coding &&
          (empty_huff_tables || using_std_huff_tables()))
        optimize_coding = true;
    }
    if (transcode_only) pass_type = optimize_coding ? huff_opt_pass : output_pass;
    else pass_type = main_pass;
    scan_number = 0;
    pass_number = 0;
    total_passes = optimize_coding ? num_scans * 2 : num_scans;
  }
};

}  // namespace vio::jpeg::detail
