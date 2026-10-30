// vio::jpeg - decompressor state, data source, marker reader and input
// controller.
#pragma once

#include <climits>
#include <optional>
#include <string>

#include "component.hpp"
#include "../simd/cpu.hpp"

namespace vio::jpeg::detail {




// REACHED_CUSTOM_MARKER: an APPn/COM marker registered with
// custom_markers is pending (unread_marker); the caller processes its data
// segment, resets unread_marker and consumes input again.
enum Status { SUSPENDED = 0, REACHED_SOS = 1, REACHED_EOI = 2, ROW_COMPLETED = 3,
              SCAN_COMPLETED = 4, REACHED_CUSTOM_MARKER = 5 };

struct DState;

class EntropyDecoder {
 public:
  virtual ~EntropyDecoder() = default;
  virtual void start_pass() = 0;
  // Lossy: decode one MCU into MCU_data (may be null to discard).
  virtual bool decode_mcu(Block** MCU_data) = 0;
  // Lossless: decode a run of MCUs into difference rows.
  virtual unsigned decode_mcus(int** const* /*diff_buf*/, unsigned /*MCU_row_num*/,
                               unsigned /*MCU_col_num*/, unsigned nMCU) {
    return nMCU;
  }
  // Lossy, single-component scans: decode n MCUs (one block each) into
  // consecutive blocks; returns false if the decoder has no such path.
  virtual bool decode_blocks(Block* /*blocks*/, unsigned /*n*/) { return false; }
  virtual bool process_restart() { return true; }
  bool insufficient_data = false;
};

class CoefControllerBase {
 public:
  virtual ~CoefControllerBase() = default;
  virtual void start_input_pass() = 0;
  virtual int consume_data() = 0;
  virtual void start_output_pass() = 0;
  // Full-image coefficient arrays (multi-scan / transcoding), else null.
  BlockArray* coef_arrays = nullptr;
  int MCU_rows_per_iMCU_row = 0;
};

// Decompression state (including master, input and marker state).  Pipeline
// modules hold a reference to it.
struct DState {
  Diagnostics* diag = nullptr;
  // SIMD instruction sets this codec may use (fixed at construction).
  vio::jpeg::simd::Support simd = vio::jpeg::simd::support();

  // ---- data source -------------------------------------------------------
  const std::uint8_t* next_input_byte = nullptr;
  std::size_t bytes_in_buffer = 0;

  // ---- header data -------------------------------------------------------
  unsigned image_width = 0, image_height = 0;
  int num_components = 0;
  ColorSpace jpeg_color_space = ColorSpace::Unknown;

  // ---- decompression parameters ------------------------------------------
  ColorSpace out_color_space = ColorSpace::Unknown;
  unsigned scale_num = 1, scale_denom = 1;
  bool buffered_image = false;
  bool raw_data_out = false;
  DctMethod dct_method = DctMethod::IntSlow;
  bool do_fancy_upsampling = true;
  bool do_block_smoothing = true;
  bool quantize_colors = false;
  DitherMode dither_mode = DitherMode::FloydSteinberg;
  bool two_pass_quantize = true;
  int desired_number_of_colors = 256;
  bool enable_1pass_quant = false, enable_external_quant = false, enable_2pass_quant = false;

  // ---- output description ------------------------------------------------
  unsigned output_width = 0, output_height = 0;
  int out_color_components = 0;
  int output_components = 0;
  int rec_outbuf_height = 1;
  int actual_number_of_colors = 0;
  // colormap[ci][index], stored with 16-bit-capable elements
  std::vector<std::vector<int>> colormap;
  bool colormap_external = false;

  unsigned output_scanline = 0;
  int input_scan_number = 0;
  unsigned input_iMCU_row = 0;
  int output_scan_number = 0;
  unsigned output_iMCU_row = 0;
  // coef_bits[ci][k] for ci in [0, 2*num_components)
  std::vector<std::array<int, DCTSIZE2>> coef_bits;
  bool coef_bits_valid = false;

  // ---- tables --------------------------------------------------------------
  std::array<std::optional<QuantTable>, NUM_QUANT_TBLS> quant_tbl_ptrs;
  std::array<std::optional<HuffTable>, NUM_HUFF_TBLS> dc_huff_tbl_ptrs, ac_huff_tbl_ptrs;

  int data_precision = 8;       // precision of output samples
  int jpeg_data_precision = 0;  // precision of the JPEG image

  std::vector<ComponentInfo> comp_info;

  bool is_baseline = false;
  bool progressive_mode = false;
  bool arith_code = false;
  bool lossless = false;
  std::uint8_t arith_dc_L[NUM_ARITH_TBLS]{};
  std::uint8_t arith_dc_U[NUM_ARITH_TBLS]{};
  std::uint8_t arith_ac_K[NUM_ARITH_TBLS]{};
  unsigned restart_interval = 0;

  bool saw_JFIF_marker = false;
  std::uint8_t JFIF_major_version = 1, JFIF_minor_version = 1;
  std::uint8_t density_unit = 0;
  std::uint16_t X_density = 1, Y_density = 1;
  bool saw_Adobe_marker = false;
  std::uint8_t Adobe_transform = 0;
  bool CCIR601_sampling = false;

  std::vector<SavedMarker> marker_list;

  // ---- derived / per-scan state -------------------------------------------
  int max_h_samp_factor = 1, max_v_samp_factor = 1;
  int min_DCT_scaled_size = DCTSIZE;
  unsigned total_iMCU_rows = 0;

  int comps_in_scan = 0;
  ComponentInfo* cur_comp_info[MAX_COMPS_IN_SCAN]{};
  unsigned MCUs_per_row = 0, MCU_rows_in_scan = 0;
  int blocks_in_MCU = 0;
  int MCU_membership[D_MAX_BLOCKS_IN_MCU]{};
  int Ss = 0, Se = 0, Ah = 0, Al = 0;
  int unread_marker = 0;

  // ---- "master" private state ----------------------------------------------
  unsigned first_iMCU_col = 0, last_iMCU_col = 0;
  unsigned first_MCU_col[MAX_COMPONENTS]{}, last_MCU_col[MAX_COMPONENTS]{};
  unsigned last_good_iMCU_row = 0;
  bool using_merged_upsample = false;
  bool is_dummy_pass = false;

  // ---- marker reader state ---------------------------------------------------
  bool saw_SOI = false, saw_SOF = false;
  int next_restart_num = 0;
  unsigned discarded_bytes = 0;
  unsigned length_limit_APPn[16]{};
  unsigned length_limit_COM = 0;
  bool save_APPn[16]{};
  bool save_COM = false;
  // Markers left to the caller: APP0..APP15 (bits 0..15) and COM (bit 16).
  std::uint32_t custom_markers = 0;

  // ---- input controller state ----------------------------------------------
  bool has_multiple_scans = false;
  bool eoi_reached = false;
  bool inheaders = true;
  bool consuming_markers = true;  // consume_input == consume_markers

  // ---- modules --------------------------------------------------------------
  std::unique_ptr<EntropyDecoder> entropy;
  CoefControllerBase* coef = nullptr;
  std::function<void()> idct_start_pass;  // lossless: undifferencer reset

  // ===========================================================================
  void warn(const std::string& msg) {
    if (diag) diag->warn(msg);
  }

  // ---- data source ----------------------------------------------------------
  void set_source(const std::uint8_t* data, std::size_t size) {
    if (!data || size == 0) VIO_FAIL("Empty input file");
    next_input_byte = data;
    bytes_in_buffer = size;
  }

  // Equivalent of fill_mem_input_buffer(): on EOF, warn and insert fake EOI.
  void fill_input_buffer() {
    static const std::uint8_t eoi[4] = {0xFF, M_EOI, 0, 0};
    warn("Premature end of JPEG file");
    next_input_byte = eoi;
    bytes_in_buffer = 2;
  }

  int read_byte() {
    if (bytes_in_buffer == 0) fill_input_buffer();
    bytes_in_buffer--;
    return *next_input_byte++;
  }
  unsigned read_2bytes() {
    unsigned hi = static_cast<unsigned>(read_byte());
    return (hi << 8) + static_cast<unsigned>(read_byte());
  }
  void skip_input_data(long num_bytes) {
    if (num_bytes > 0) {
      while (num_bytes > static_cast<long>(bytes_in_buffer)) {
        num_bytes -= static_cast<long>(bytes_in_buffer);
        fill_input_buffer();
      }
      next_input_byte += num_bytes;
      bytes_in_buffer -= static_cast<std::size_t>(num_bytes);
    }
  }

  // ---- marker reader ----------------------------------------------------------
  void reset_marker_reader() {
    comp_info.clear();
    input_scan_number = 0;
    unread_marker = 0;
    saw_SOI = saw_SOF = false;
    discarded_bytes = 0;
  }

  void save_markers(int marker_code, unsigned length_limit) {
    if (length_limit) {
      if (marker_code == M_APP0 && length_limit < 14) length_limit = 14;
      else if (marker_code == M_APP14 && length_limit < 12) length_limit = 12;
    }
    if (marker_code == M_COM) {
      save_COM = length_limit != 0;
      length_limit_COM = length_limit;
    } else if (marker_code >= M_APP0 && marker_code <= M_APP15) {
      save_APPn[marker_code - M_APP0] = length_limit != 0;
      length_limit_APPn[marker_code - M_APP0] = length_limit;
    } else {
      VIO_FAIL("Unsupported marker type 0x%02x", marker_code);
    }
  }

  void get_soi() {
    if (saw_SOI) VIO_FAIL("Invalid JPEG file structure: two SOI markers");
    reset_arith_tables(arith_dc_L, arith_dc_U, arith_ac_K);
    restart_interval = 0;
    jpeg_color_space = ColorSpace::Unknown;
    CCIR601_sampling = false;
    saw_JFIF_marker = false;
    JFIF_major_version = 1;
    JFIF_minor_version = 1;
    density_unit = 0;
    X_density = 1;
    Y_density = 1;
    saw_Adobe_marker = false;
    Adobe_transform = 0;
    saw_SOI = true;
  }

  void get_sof(bool is_prog, bool is_lossless, bool is_arith, bool is_base) {
    if (saw_SOF) VIO_FAIL("Invalid JPEG file structure: two SOF markers");
    progressive_mode = is_prog;
    lossless = is_lossless;
    arith_code = is_arith;
    is_baseline = is_base;
    long length = read_2bytes();
    data_precision = read_byte();
    jpeg_data_precision = data_precision;
    image_height = read_2bytes();
    image_width = read_2bytes();
    num_components = read_byte();
    length -= 8;
    if (image_height <= 0 || image_width <= 0 || num_components <= 0)
      VIO_FAIL("Empty JPEG image (DNL not supported)");
    if (length != num_components * 3) VIO_FAIL("Bogus marker length");
    comp_info.assign(num_components, ComponentInfo{});
    for (int ci = 0; ci < num_components; ci++) {
      ComponentInfo& c = comp_info[ci];
      c.component_index = ci;
      c.component_id = read_byte();
      int v = read_byte();
      c.h_samp_factor = (v >> 4) & 15;
      c.v_samp_factor = v & 15;
      c.quant_tbl_no = read_byte();
    }
    saw_SOF = true;
  }

  void get_sos() {
    if (!saw_SOF) VIO_FAIL("Invalid JPEG file structure: SOS before SOF");
    long length = read_2bytes();
    int n = read_byte();
    if (length != (n * 2 + 6) || n < 1 || n > MAX_COMPS_IN_SCAN)
      VIO_FAIL("Bogus marker length");
    comps_in_scan = n;
    for (int i = 0; i < MAX_COMPS_IN_SCAN; i++) cur_comp_info[i] = nullptr;
    for (int i = 0; i < n; i++) {
      int cc = read_byte();
      int c = read_byte();
      ComponentInfo* compptr = nullptr;
      for (int ci = 0; ci < num_components && ci < MAX_COMPS_IN_SCAN; ci++) {
        // cur_comp_info[] is indexed by component here, not by scan position
        if (cc == comp_info[ci].component_id && !cur_comp_info[ci]) {
          compptr = &comp_info[ci];
          break;
        }
      }
      if (!compptr) VIO_FAIL("Invalid component ID %d in SOS", cc);
      cur_comp_info[i] = compptr;
      compptr->dc_tbl_no = (c >> 4) & 15;
      compptr->ac_tbl_no = c & 15;
      for (int pi = 0; pi < i; pi++)
        if (cur_comp_info[pi] == compptr) VIO_FAIL("Invalid component ID %d in SOS", cc);
    }
    Ss = read_byte();
    Se = read_byte();
    int c = read_byte();
    Ah = (c >> 4) & 15;
    Al = c & 15;
    next_restart_num = 0;
    input_scan_number++;
  }

  void get_dac() {
    long length = static_cast<long>(read_2bytes()) - 2;
    while (length > 0) {
      int index = read_byte();
      int val = read_byte();
      length -= 2;
      if (index < 0 || index >= 2 * NUM_ARITH_TBLS) VIO_FAIL("Bogus DAC index %d", index);
      if (index >= NUM_ARITH_TBLS) {
        arith_ac_K[index - NUM_ARITH_TBLS] = static_cast<std::uint8_t>(val);
      } else {
        arith_dc_L[index] = static_cast<std::uint8_t>(val & 0x0F);
        arith_dc_U[index] = static_cast<std::uint8_t>(val >> 4);
        if (arith_dc_L[index] > arith_dc_U[index]) VIO_FAIL("Bogus DAC value 0x%x", val);
      }
    }
    if (length != 0) VIO_FAIL("Bogus marker length");
  }

  void get_dht() {
    long length = static_cast<long>(read_2bytes()) - 2;
    while (length > 16) {
      int index = read_byte();
      std::uint8_t bits[17];
      std::uint8_t huffval[256];
      bits[0] = 0;
      int count = 0;
      for (int i = 1; i <= 16; i++) {
        bits[i] = static_cast<std::uint8_t>(read_byte());
        count += bits[i];
      }
      length -= 1 + 16;
      if (count > 256 || static_cast<long>(count) > length) VIO_FAIL("Bogus Huffman table definition");
      for (int i = 0; i < count; i++) huffval[i] = static_cast<std::uint8_t>(read_byte());
      std::memset(&huffval[count], 0, 256 - count);
      length -= count;
      bool ac = (index & 0x10) != 0;
      if (ac) index -= 0x10;
      if (index < 0 || index >= NUM_HUFF_TBLS) VIO_FAIL("Bogus DHT index %d", index);
      auto& slot = ac ? ac_huff_tbl_ptrs[index] : dc_huff_tbl_ptrs[index];
      if (!slot) slot.emplace();
      std::memcpy(slot->bits.data(), bits, 17);
      std::memcpy(slot->huffval.data(), huffval, 256);
    }
    if (length != 0) VIO_FAIL("Bogus marker length");
  }

  void get_dqt() {
    long length = static_cast<long>(read_2bytes()) - 2;
    while (length > 0) {
      int n = read_byte();
      int prec = n >> 4;
      n &= 0x0F;
      if (n >= NUM_QUANT_TBLS) VIO_FAIL("Bogus DQT index %d", n);
      if (!quant_tbl_ptrs[n]) quant_tbl_ptrs[n].emplace();
      QuantTable& q = *quant_tbl_ptrs[n];
      for (int i = 0; i < DCTSIZE2; i++) {
        unsigned tmp = prec ? read_2bytes() : static_cast<unsigned>(read_byte());
        q.quantval[natural_order[i]] = static_cast<std::uint16_t>(tmp);
      }
      length -= DCTSIZE2 + 1;
      if (prec) length -= DCTSIZE2;
    }
    if (length != 0) VIO_FAIL("Bogus marker length");
  }

  void get_dri() {
    unsigned length = read_2bytes();
    if (length != 4) VIO_FAIL("Bogus marker length");
    restart_interval = read_2bytes();
  }

  void examine_app0(const std::uint8_t* data, unsigned datalen, long /*remaining*/) {
    if (datalen >= 14 && data[0] == 0x4A && data[1] == 0x46 && data[2] == 0x49 &&
        data[3] == 0x46 && data[4] == 0) {
      saw_JFIF_marker = true;
      JFIF_major_version = data[5];
      JFIF_minor_version = data[6];
      density_unit = data[7];
      X_density = static_cast<std::uint16_t>((data[8] << 8) + data[9]);
      Y_density = static_cast<std::uint16_t>((data[10] << 8) + data[11]);
      if (JFIF_major_version != 1)
        warn(strformat("Warning: unknown JFIF revision number %d.%02d",
                            JFIF_major_version, JFIF_minor_version));
    }
  }

  void examine_app14(const std::uint8_t* data, unsigned datalen, long /*remaining*/) {
    if (datalen >= 12 && data[0] == 0x41 && data[1] == 0x64 && data[2] == 0x6F &&
        data[3] == 0x62 && data[4] == 0x65) {
      saw_Adobe_marker = true;
      Adobe_transform = data[11];
    }
  }

  void get_interesting_appn() {
    long length = static_cast<long>(read_2bytes()) - 2;
    std::uint8_t b[14];
    unsigned numtoread = length >= 14 ? 14u : (length > 0 ? static_cast<unsigned>(length) : 0u);
    for (unsigned i = 0; i < numtoread; i++) b[i] = static_cast<std::uint8_t>(read_byte());
    length -= numtoread;
    if (unread_marker == M_APP0) examine_app0(b, numtoread, length);
    else examine_app14(b, numtoread, length);
    if (length > 0) skip_input_data(length);
  }

  void save_marker() {
    long length = static_cast<long>(read_2bytes()) - 2;
    if (length < 0) return;  // bogus length word
    unsigned limit = unread_marker == M_COM ? length_limit_COM
                                            : length_limit_APPn[unread_marker - M_APP0];
    if (static_cast<unsigned>(length) < limit) limit = static_cast<unsigned>(length);
    SavedMarker m;
    m.marker = static_cast<std::uint8_t>(unread_marker);
    m.original_length = static_cast<unsigned>(length);
    m.data.resize(limit);
    for (unsigned i = 0; i < limit; i++) m.data[i] = static_cast<std::uint8_t>(read_byte());
    long remaining = static_cast<long>(m.original_length) - limit;
    if (unread_marker == M_APP0) examine_app0(m.data.data(), limit, remaining);
    else if (unread_marker == M_APP14) examine_app14(m.data.data(), limit, remaining);
    marker_list.push_back(std::move(m));
    if (remaining > 0) skip_input_data(remaining);
  }

  void skip_variable() {
    long length = static_cast<long>(read_2bytes()) - 2;
    if (length > 0) skip_input_data(length);
  }

  void next_marker() {
    for (;;) {
      int c = read_byte();
      while (c != 0xFF) {
        discarded_bytes++;
        c = read_byte();
      }
      do {
        c = read_byte();
      } while (c == 0xFF);
      if (c != 0) {
        if (discarded_bytes != 0) {
          warn(strformat(
              "Corrupt JPEG data: %u extraneous bytes before marker 0x%02x",
              discarded_bytes, c));
          discarded_bytes = 0;
        }
        unread_marker = c;
        return;
      }
      discarded_bytes += 2;
    }
  }

  void first_marker() {
    int c = read_byte();
    int c2 = read_byte();
    if (c != 0xFF || c2 != M_SOI) VIO_FAIL("Not a JPEG file: starts with 0x%02x 0x%02x", c, c2);
    unread_marker = c2;
  }

  int read_markers() {
    for (;;) {
      if (unread_marker == 0) {
        if (!saw_SOI) first_marker();
        else next_marker();
      }
      switch (unread_marker) {
        case M_SOI: get_soi(); break;
        case M_SOF0: get_sof(false, false, false, true); break;
        case M_SOF1: get_sof(false, false, false, false); break;
        case M_SOF2: get_sof(true, false, false, false); break;
        case M_SOF3: get_sof(false, true, false, false); break;
        case M_SOF9: get_sof(false, false, true, false); break;
        case M_SOF10: get_sof(true, false, true, false); break;
        case M_SOF11: get_sof(false, true, true, false); break;
        case M_SOF5: case M_SOF6: case M_SOF7: case M_JPG:
        case M_SOF13: case M_SOF14: case M_SOF15:
          VIO_FAIL("Unsupported JPEG process: SOF type 0x%02x", unread_marker);
        case M_SOS:
          get_sos();
          unread_marker = 0;
          return REACHED_SOS;
        case M_EOI:
          unread_marker = 0;
          return REACHED_EOI;
        case M_DAC: get_dac(); break;
        case M_DHT: get_dht(); break;
        case M_DQT: get_dqt(); break;
        case M_DRI: get_dri(); break;
        case M_COM:
          if (custom_markers & (1u << 16)) return REACHED_CUSTOM_MARKER;
          if (save_COM) save_marker();
          else skip_variable();
          break;
        case 0xd0: case 0xd1: case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6:
        case 0xd7: case M_TEM:
          break;
        case M_DNL: skip_variable(); break;
        default:
          if (unread_marker >= M_APP0 && unread_marker <= M_APP15) {
            int n = unread_marker - M_APP0;
            if (custom_markers & (1u << n)) return REACHED_CUSTOM_MARKER;
            if (save_APPn[n]) save_marker();
            else if (n == 0 || n == 14) get_interesting_appn();
            else skip_variable();
            break;
          }
          VIO_FAIL("Unsupported marker type 0x%02x", unread_marker);
      }
      unread_marker = 0;
    }
  }

  void resync_to_restart(int desired) {
    int marker = unread_marker;
    warn(strformat(
        "Corrupt JPEG data: found marker 0x%02x instead of RST%d", marker, desired));
    for (;;) {
      int action;
      if (marker < M_SOF0) action = 2;
      else if (marker < M_RST0 || marker > M_RST7) action = 3;
      else {
        if (marker == M_RST0 + ((desired + 1) & 7) || marker == M_RST0 + ((desired + 2) & 7))
          action = 3;
        else if (marker == M_RST0 + ((desired - 1) & 7) ||
                 marker == M_RST0 + ((desired - 2) & 7))
          action = 2;
        else
          action = 1;
      }
      switch (action) {
        case 1: unread_marker = 0; return;
        case 2: next_marker(); marker = unread_marker; break;
        default: return;
      }
    }
  }

  bool read_restart_marker() {
    if (unread_marker == 0) next_marker();
    if (unread_marker == M_RST0 + next_restart_num) unread_marker = 0;
    else resync_to_restart(next_restart_num);
    next_restart_num = (next_restart_num + 1) & 7;
    return true;
  }

  // ---- input controller ---------------------------------------------------
  void initial_setup() {
    int data_unit = lossless ? 1 : DCTSIZE;
    if (image_height > JPEG_MAX_DIMENSION || image_width > JPEG_MAX_DIMENSION)
      VIO_FAIL("Maximum supported image dimension is %u pixels", JPEG_MAX_DIMENSION);
    if (lossless) {
      if (data_precision < 2 || data_precision > 16)
        VIO_FAIL("Unsupported JPEG data precision %d", data_precision);
    } else if (data_precision != 8 && data_precision != 12) {
      VIO_FAIL("Unsupported JPEG data precision %d", data_precision);
    }
    if (num_components > MAX_COMPONENTS)
      VIO_FAIL("Too many color components: %d, max %d", num_components, MAX_COMPONENTS);
    max_h_samp_factor = max_v_samp_factor = 1;
    for (auto& c : comp_info) {
      if (c.h_samp_factor <= 0 || c.h_samp_factor > MAX_SAMP_FACTOR || c.v_samp_factor <= 0 ||
          c.v_samp_factor > MAX_SAMP_FACTOR)
        VIO_FAIL("Bogus sampling factors");
      max_h_samp_factor = std::max(max_h_samp_factor, c.h_samp_factor);
      max_v_samp_factor = std::max(max_v_samp_factor, c.v_samp_factor);
    }
    min_DCT_scaled_size = data_unit;
    for (int ci = 0; ci < num_components; ci++) {
      ComponentInfo& c = comp_info[ci];
      c.DCT_scaled_size = data_unit;
      c.width_in_blocks = static_cast<unsigned>(div_round_up(
          static_cast<long>(image_width) * c.h_samp_factor, max_h_samp_factor * data_unit));
      c.height_in_blocks = static_cast<unsigned>(div_round_up(
          static_cast<long>(image_height) * c.v_samp_factor, max_v_samp_factor * data_unit));
      first_MCU_col[ci] = 0;
      last_MCU_col[ci] = c.width_in_blocks - 1;
      c.downsampled_width = static_cast<unsigned>(
          div_round_up(static_cast<long>(image_width) * c.h_samp_factor, max_h_samp_factor));
      c.downsampled_height = static_cast<unsigned>(
          div_round_up(static_cast<long>(image_height) * c.v_samp_factor, max_v_samp_factor));
      c.component_needed = true;
      c.quant_table.reset();
    }
    total_iMCU_rows = static_cast<unsigned>(
        div_round_up(static_cast<long>(image_height), max_v_samp_factor * data_unit));
    has_multiple_scans = comps_in_scan < num_components || progressive_mode;
  }

#define VIO_SCAN_DECODER 1
#include "scan_setup.inc"
#undef VIO_SCAN_DECODER

  void latch_quant_tables() {
    for (int ci = 0; ci < comps_in_scan; ci++) {
      ComponentInfo* c = cur_comp_info[ci];
      if (c->quant_table) continue;
      int qtblno = c->quant_tbl_no;
      if (qtblno < 0 || qtblno >= NUM_QUANT_TBLS || !quant_tbl_ptrs[qtblno])
        VIO_FAIL("Quantization table 0x%02x was not defined", qtblno);
      c->quant_table = *quant_tbl_ptrs[qtblno];
    }
  }

  void start_input_pass() {
    per_scan_setup();
    if (!lossless) latch_quant_tables();
    entropy->start_pass();
    coef->start_input_pass();
    consuming_markers = false;
  }

  void finish_input_pass() { consuming_markers = true; }

  int consume_markers() {
    if (eoi_reached) return REACHED_EOI;
    int val = read_markers();
    switch (val) {
      case REACHED_SOS:
        if (inheaders) {
          initial_setup();
          inheaders = false;
        } else {
          if (!has_multiple_scans) VIO_FAIL("Didn't expect more than one scan");
          start_input_pass();
        }
        break;
      case REACHED_EOI:
        eoi_reached = true;
        if (inheaders) {
          if (saw_SOF) VIO_FAIL("Invalid JPEG file structure: missing SOS marker");
        } else if (output_scan_number > input_scan_number) {
          output_scan_number = input_scan_number;
        }
        break;
      default:
        break;
    }
    return val;
  }

  int consume_input() { return consuming_markers ? consume_markers() : coef->consume_data(); }

  void reset_input_controller() {
    consuming_markers = true;
    has_multiple_scans = false;
    eoi_reached = false;
    inheaders = true;
    reset_marker_reader();
    coef_bits.clear();
    coef_bits_valid = false;
  }

  void default_decompress_parms() {
    switch (num_components) {
      case 1:
        jpeg_color_space = ColorSpace::Gray;
        out_color_space = ColorSpace::Gray;
        break;
      case 3:
        if (saw_JFIF_marker) {
          jpeg_color_space = ColorSpace::YCbCr;
        } else if (saw_Adobe_marker) {
          switch (Adobe_transform) {
            case 0: jpeg_color_space = ColorSpace::RGB; break;
            case 1: jpeg_color_space = ColorSpace::YCbCr; break;
            default:
              warn(strformat("Unknown Adobe color transform code %d", Adobe_transform));
              jpeg_color_space = ColorSpace::YCbCr;
          }
        } else {
          int cid0 = comp_info[0].component_id, cid1 = comp_info[1].component_id,
              cid2 = comp_info[2].component_id;
          if (cid0 == 82 && cid1 == 71 && cid2 == 66)
            jpeg_color_space = ColorSpace::RGB;
          else
            jpeg_color_space = lossless ? ColorSpace::RGB : ColorSpace::YCbCr;
        }
        out_color_space = ColorSpace::RGB;
        break;
      case 4:
        if (saw_Adobe_marker) {
          switch (Adobe_transform) {
            case 0: jpeg_color_space = ColorSpace::CMYK; break;
            case 2: jpeg_color_space = ColorSpace::YCCK; break;
            default:
              warn(strformat("Unknown Adobe color transform code %d", Adobe_transform));
              jpeg_color_space = ColorSpace::YCCK;
          }
        } else {
          jpeg_color_space = ColorSpace::CMYK;
        }
        out_color_space = ColorSpace::CMYK;
        break;
      default:
        jpeg_color_space = ColorSpace::Unknown;
        out_color_space = ColorSpace::Unknown;
    }
    scale_num = scale_denom = 1;
    buffered_image = false;
    raw_data_out = false;
    dct_method = DctMethod::IntSlow;
    do_fancy_upsampling = true;
    do_block_smoothing = true;
    quantize_colors = false;
    dither_mode = DitherMode::FloydSteinberg;
    two_pass_quantize = true;
    desired_number_of_colors = 256;
    colormap.clear();
    colormap_external = false;
    enable_1pass_quant = enable_external_quant = enable_2pass_quant = false;
  }
};

}  // namespace vio::jpeg::detail
