// vio::jpeg - color quantization: 1-pass and 2-pass median-cut
// quantizers.
#pragma once

#include "decompress_sample.hpp"

namespace vio::jpeg::detail {

template <class T>
using FsError = std::conditional_t<kBits<T> == 8, std::int16_t, JLong>;
template <class T>
using LocFsError = std::conditional_t<kBits<T> == 8, int, JLong>;

// Publishes a colormap ([component][index]) to the decoder state.
template <class T>
inline void publish_colormap(DState& st, T** cmap, int ncomp, int ncolors) {
  st.colormap.assign(ncomp, std::vector<int>(ncolors));
  for (int c = 0; c < ncomp; c++)
    for (int i = 0; i < ncolors; i++) st.colormap[c][i] = cmap[c][i];
  st.actual_number_of_colors = ncolors;
}

// ---------------------------------------------------------------------------
template <class T>
class Quantizer1Pass final : public ColorQuantizer<T> {
 public:
  static constexpr int ODITHER_SIZE = 16;
  static constexpr int ODITHER_CELLS = ODITHER_SIZE * ODITHER_SIZE;
  static constexpr int ODITHER_MASK = ODITHER_SIZE - 1;
  static constexpr int MAX_Q_COMPS = 4;
  using OditherMatrix = std::array<std::array<int, ODITHER_SIZE>, ODITHER_SIZE>;

  Quantizer1Pass(DState& st, const T* range_limit) : st_(st), range_limit_(range_limit) {
    if (st.lossless) VIO_FAIL("Requested features are incompatible");
    if (st.out_color_components > MAX_Q_COMPS)
      VIO_FAIL("Cannot quantize more than %d color components", MAX_Q_COMPS);
    if (st.desired_number_of_colors > kMaxSample<T> + 1)
      VIO_FAIL("Cannot quantize to more than %d colors", kMaxSample<T> + 1);
    create_colormap();
    create_colorindex();
    if (st.dither_mode == DitherMode::FloydSteinberg) alloc_fs_workspace();
  }

  void start_pass(bool) override {
    publish_colormap(st_, sv_colormap_.rows(), st_.out_color_components, sv_actual_);
    switch (st_.dither_mode) {
      case DitherMode::None:
        break;
      case DitherMode::Ordered:
        row_index_ = 0;
        if (!is_padded_) create_colorindex();
        if (!odither_[0]) create_odither_tables();
        break;
      case DitherMode::FloydSteinberg:
        on_odd_row_ = false;
        if (fserrors_[0].empty()) alloc_fs_workspace();
        for (int i = 0; i < st_.out_color_components; i++)
          std::fill(fserrors_[i].begin(), fserrors_[i].end(), 0);
        break;
    }
  }

  void color_quantize(T** input_buf, T** output_buf, int num_rows) override {
    switch (st_.dither_mode) {
      case DitherMode::None: quantize_none(input_buf, output_buf, num_rows); break;
      case DitherMode::Ordered: quantize_ord(input_buf, output_buf, num_rows); break;
      case DitherMode::FloydSteinberg: quantize_fs(input_buf, output_buf, num_rows); break;
    }
  }

  void new_color_map() override { VIO_FAIL("Invalid color quantization mode change"); }

 private:
  int select_ncolors(int Ncolors[]) {
    int nc = st_.out_color_components;
    int max_colors = st_.desired_number_of_colors;
    int RGB_order[3] = {rgb_green[idx(st_.out_color_space)], rgb_red[idx(st_.out_color_space)],
                        rgb_blue[idx(st_.out_color_space)]};
    int iroot = 1;
    long temp;
    do {
      iroot++;
      temp = iroot;
      for (int i = 1; i < nc; i++) temp *= iroot;
    } while (temp <= static_cast<long>(max_colors));
    iroot--;
    if (iroot < 2) VIO_FAIL("Cannot quantize to fewer than %d colors", static_cast<int>(temp));
    int total_colors = 1;
    for (int i = 0; i < nc; i++) {
      Ncolors[i] = iroot;
      total_colors *= iroot;
    }
    bool changed;
    do {
      changed = false;
      for (int i = 0; i < nc; i++) {
        int j = (st_.out_color_space == ColorSpace::RGB ? RGB_order[i] : i);
        temp = total_colors / Ncolors[j];
        temp *= Ncolors[j] + 1;
        if (temp > static_cast<long>(max_colors)) break;
        Ncolors[j]++;
        total_colors = static_cast<int>(temp);
        changed = true;
      }
    } while (changed);
    return total_colors;
  }

  static int output_value(int j, int maxj) {
    return static_cast<int>((static_cast<JLong>(j) * kMaxSample<T> + maxj / 2) / maxj);
  }
  static int largest_input_value(int j, int maxj) {
    return static_cast<int>((static_cast<JLong>(2 * j + 1) * kMaxSample<T> + maxj) / (2 * maxj));
  }

  void create_colormap() {
    int total_colors = select_ncolors(Ncolors_);
    sv_colormap_.allocate(total_colors, st_.out_color_components);
    int blkdist = total_colors;
    for (int i = 0; i < st_.out_color_components; i++) {
      int nci = Ncolors_[i];
      int blksize = blkdist / nci;
      for (int j = 0; j < nci; j++) {
        int val = output_value(j, nci - 1);
        for (int ptr = j * blksize; ptr < total_colors; ptr += blkdist)
          for (int k = 0; k < blksize; k++) sv_colormap_[i][ptr + k] = static_cast<T>(val);
      }
      blkdist = blksize;
    }
    sv_actual_ = total_colors;
  }

  void create_colorindex() {
    is_padded_ = st_.dither_mode == DitherMode::Ordered;
    int pad = is_padded_ ? kMaxSample<T> * 2 : 0;
    colorindex_store_.allocate(kMaxSample<T> + 1 + pad, st_.out_color_components);
    int blksize = sv_actual_;
    for (int i = 0; i < st_.out_color_components; i++) {
      int nci = Ncolors_[i];
      blksize = blksize / nci;
      T* indexptr = colorindex_store_[i] + (pad ? kMaxSample<T> : 0);
      colorindex_[i] = indexptr;
      int val = 0;
      int k = largest_input_value(0, nci - 1);
      for (int j = 0; j <= kMaxSample<T>; j++) {
        while (j > k) k = largest_input_value(++val, nci - 1);
        indexptr[j] = static_cast<T>(val * blksize);
      }
      if (pad)
        for (int j = 1; j <= kMaxSample<T>; j++) {
          indexptr[-j] = indexptr[0];
          indexptr[kMaxSample<T> + j] = indexptr[kMaxSample<T>];
        }
    }
  }

  static constexpr std::uint8_t base_dither_matrix[ODITHER_SIZE][ODITHER_SIZE] = {
      {0, 192, 48, 240, 12, 204, 60, 252, 3, 195, 51, 243, 15, 207, 63, 255},
      {128, 64, 176, 112, 140, 76, 188, 124, 131, 67, 179, 115, 143, 79, 191, 127},
      {32, 224, 16, 208, 44, 236, 28, 220, 35, 227, 19, 211, 47, 239, 31, 223},
      {160, 96, 144, 80, 172, 108, 156, 92, 163, 99, 147, 83, 175, 111, 159, 95},
      {8, 200, 56, 248, 4, 196, 52, 244, 11, 203, 59, 251, 7, 199, 55, 247},
      {136, 72, 184, 120, 132, 68, 180, 116, 139, 75, 187, 123, 135, 71, 183, 119},
      {40, 232, 24, 216, 36, 228, 20, 212, 43, 235, 27, 219, 39, 231, 23, 215},
      {168, 104, 152, 88, 164, 100, 148, 84, 171, 107, 155, 91, 167, 103, 151, 87},
      {2, 194, 50, 242, 14, 206, 62, 254, 1, 193, 49, 241, 13, 205, 61, 253},
      {130, 66, 178, 114, 142, 78, 190, 126, 129, 65, 177, 113, 141, 77, 189, 125},
      {34, 226, 18, 210, 46, 238, 30, 222, 33, 225, 17, 209, 45, 237, 29, 221},
      {162, 98, 146, 82, 174, 110, 158, 94, 161, 97, 145, 81, 173, 109, 157, 93},
      {10, 202, 58, 250, 6, 198, 54, 246, 9, 201, 57, 249, 5, 197, 53, 245},
      {138, 74, 186, 122, 134, 70, 182, 118, 137, 73, 185, 121, 133, 69, 181, 117},
      {42, 234, 26, 218, 38, 230, 22, 214, 41, 233, 25, 217, 37, 229, 21, 213},
      {170, 106, 154, 90, 166, 102, 150, 86, 169, 105, 153, 89, 165, 101, 149, 85}};

  std::shared_ptr<OditherMatrix> make_odither_array(int ncolors) {
    auto od = std::make_shared<OditherMatrix>();
    JLong den = 2 * ODITHER_CELLS * static_cast<JLong>(ncolors - 1);
    for (int j = 0; j < ODITHER_SIZE; j++)
      for (int k = 0; k < ODITHER_SIZE; k++) {
        JLong num =
            static_cast<JLong>(ODITHER_CELLS - 1 - 2 * static_cast<int>(base_dither_matrix[j][k])) *
            kMaxSample<T>;
        (*od)[j][k] = static_cast<int>(num / den);
      }
    return od;
  }

  void create_odither_tables() {
    for (int i = 0; i < st_.out_color_components; i++) {
      int nci = Ncolors_[i];
      std::shared_ptr<OditherMatrix> od;
      for (int j = 0; j < i; j++)
        if (nci == Ncolors_[j]) {
          od = odither_[j];
          break;
        }
      if (!od) od = make_odither_array(nci);
      odither_[i] = od;
    }
  }

  void alloc_fs_workspace() {
    for (int i = 0; i < st_.out_color_components; i++)
      fserrors_[i].assign(st_.output_width + 2, 0);
  }

  void quantize_none(T** input_buf, T** output_buf, int num_rows) {
    unsigned width = st_.output_width;
    int nc = st_.out_color_components;
    for (int row = 0; row < num_rows; row++) {
      const T* ptrin = input_buf[row];
      T* ptrout = output_buf[row];
      for (unsigned col = width; col > 0; col--) {
        int pixcode = 0;
        for (int ci = 0; ci < nc; ci++) pixcode += colorindex_[ci][*ptrin++];
        *ptrout++ = static_cast<T>(pixcode);
      }
    }
  }

  void quantize_ord(T** input_buf, T** output_buf, int num_rows) {
    unsigned width = st_.output_width;
    int nc = st_.out_color_components;
    for (int row = 0; row < num_rows; row++) {
      if (nc == 3) {
        const T* input_ptr = input_buf[row];
        T* output_ptr = output_buf[row];
        const int* d0 = (*odither_[0])[row_index_].data();
        const int* d1 = (*odither_[1])[row_index_].data();
        const int* d2 = (*odither_[2])[row_index_].data();
        int col_index = 0;
        for (unsigned col = width; col > 0; col--) {
          int pixcode = colorindex_[0][(*input_ptr++) + d0[col_index]];
          pixcode += colorindex_[1][(*input_ptr++) + d1[col_index]];
          pixcode += colorindex_[2][(*input_ptr++) + d2[col_index]];
          *output_ptr++ = static_cast<T>(pixcode);
          col_index = (col_index + 1) & ODITHER_MASK;
        }
      } else {
        std::memset(output_buf[row], 0, width * sizeof(T));
        for (int ci = 0; ci < nc; ci++) {
          const T* input_ptr = input_buf[row] + ci;
          T* output_ptr = output_buf[row];
          const T* colorindex_ci = colorindex_[ci];
          const int* dither = (*odither_[ci])[row_index_].data();
          int col_index = 0;
          for (unsigned col = width; col > 0; col--) {
            *output_ptr = static_cast<T>(*output_ptr + colorindex_ci[*input_ptr + dither[col_index]]);
            input_ptr += nc;
            output_ptr++;
            col_index = (col_index + 1) & ODITHER_MASK;
          }
        }
      }
      row_index_ = (row_index_ + 1) & ODITHER_MASK;
    }
  }

  void quantize_fs(T** input_buf, T** output_buf, int num_rows) {
    unsigned width = st_.output_width;
    int nc = st_.out_color_components;
    const T* range_limit = range_limit_;
    for (int row = 0; row < num_rows; row++) {
      std::memset(output_buf[row], 0, width * sizeof(T));
      for (int ci = 0; ci < nc; ci++) {
        const T* input_ptr = input_buf[row] + ci;
        T* output_ptr = output_buf[row];
        int dir, dirnc;
        FsError<T>* errorptr;
        if (on_odd_row_) {
          input_ptr += (width - 1) * nc;
          output_ptr += width - 1;
          dir = -1;
          dirnc = -nc;
          errorptr = fserrors_[ci].data() + (width + 1);
        } else {
          dir = 1;
          dirnc = nc;
          errorptr = fserrors_[ci].data();
        }
        const T* colorindex_ci = colorindex_[ci];
        const T* colormap_ci = sv_colormap_[ci];
        LocFsError<T> cur = 0, belowerr = 0, bpreverr = 0;
        for (unsigned col = width; col > 0; col--) {
          cur = (cur + errorptr[dir] + 8) >> 4;
          cur += *input_ptr;
          cur = range_limit[cur];
          int pixcode = colorindex_ci[cur];
          *output_ptr = static_cast<T>(*output_ptr + pixcode);
          cur -= colormap_ci[pixcode];
          LocFsError<T> bnexterr = cur;
          LocFsError<T> delta = cur * 2;
          cur += delta;
          errorptr[0] = static_cast<FsError<T>>(bpreverr + cur);
          cur += delta;
          bpreverr = belowerr + cur;
          belowerr = bnexterr;
          cur += delta;
          input_ptr += dirnc;
          output_ptr += dir;
          errorptr += dir;
        }
        errorptr[0] = static_cast<FsError<T>>(bpreverr);
      }
      on_odd_row_ = !on_odd_row_;
    }
  }

  DState& st_;
  const T* range_limit_;
  SampleArray<T> sv_colormap_;
  int sv_actual_ = 0;
  SampleArray<T> colorindex_store_;
  T* colorindex_[MAX_Q_COMPS]{};
  bool is_padded_ = false;
  int Ncolors_[MAX_Q_COMPS]{};
  int row_index_ = 0;
  std::shared_ptr<OditherMatrix> odither_[MAX_Q_COMPS];
  std::vector<FsError<T>> fserrors_[MAX_Q_COMPS];
  bool on_odd_row_ = false;
};

// ---------------------------------------------------------------------------
template <class T>
class Quantizer2Pass final : public ColorQuantizer<T> {
 public:
  static constexpr int MAXNUMCOLORS = kMaxSample<T> + 1;
  static constexpr int HIST_C0_BITS = 5, HIST_C1_BITS = 6, HIST_C2_BITS = 5;
  static constexpr int HIST_C0_ELEMS = 1 << HIST_C0_BITS, HIST_C1_ELEMS = 1 << HIST_C1_BITS,
                       HIST_C2_ELEMS = 1 << HIST_C2_BITS;
  static constexpr int C0_SHIFT = kBits<T> - HIST_C0_BITS, C1_SHIFT = kBits<T> - HIST_C1_BITS,
                       C2_SHIFT = kBits<T> - HIST_C2_BITS;
  static constexpr int BOX_C0_LOG = HIST_C0_BITS - 3, BOX_C1_LOG = HIST_C1_BITS - 3,
                       BOX_C2_LOG = HIST_C2_BITS - 3;
  static constexpr int BOX_C0_ELEMS = 1 << BOX_C0_LOG, BOX_C1_ELEMS = 1 << BOX_C1_LOG,
                       BOX_C2_ELEMS = 1 << BOX_C2_LOG;
  static constexpr int BOX_C0_SHIFT = C0_SHIFT + BOX_C0_LOG, BOX_C1_SHIFT = C1_SHIFT + BOX_C1_LOG,
                       BOX_C2_SHIFT = C2_SHIFT + BOX_C2_LOG;
  using histcell = std::uint16_t;

  Quantizer2Pass(DState& st, const T* range_limit) : st_(st), range_limit_(range_limit) {
    static constexpr int c_scales[3] = {2, 3, 1};
    C0_SCALE = c_scales[rgb_red[idx(st.out_color_space)]];
    C1_SCALE = c_scales[rgb_green[idx(st.out_color_space)]];
    C2_SCALE = c_scales[rgb_blue[idx(st.out_color_space)]];
    if (st.out_color_components != 3 || st.out_color_space == ColorSpace::RGB565 || st.lossless)
      VIO_FAIL("Requested features are incompatible");
    histogram_.assign(static_cast<std::size_t>(HIST_C0_ELEMS) * HIST_C1_ELEMS * HIST_C2_ELEMS, 0);
    if (st.enable_2pass_quant) {
      int desired = st.desired_number_of_colors;
      if (desired < 8) VIO_FAIL("Cannot quantize to fewer than %d colors", 8);
      if (desired > MAXNUMCOLORS) VIO_FAIL("Cannot quantize to more than %d colors", MAXNUMCOLORS);
      sv_colormap_.allocate(desired, 3);
      desired_ = desired;
    }
    if (st.dither_mode != DitherMode::None) st.dither_mode = DitherMode::FloydSteinberg;
    if (st.dither_mode == DitherMode::FloydSteinberg) {
      fserrors_.assign(static_cast<std::size_t>(st.output_width + 2) * 3, 0);
      init_error_limit();
    }
  }

  // Install an application-supplied colormap (enable_external_quant).
  void set_external_colormap(const std::vector<std::vector<int>>& cmap) {
    int n = static_cast<int>(cmap[0].size());
    ext_colormap_.allocate(n, 3);
    for (int c = 0; c < 3; c++)
      for (int i = 0; i < n; i++) ext_colormap_[c][i] = static_cast<T>(cmap[c][i]);
    colormap_ = ext_colormap_.rows();
    actual_ = n;
  }

  void start_pass(bool is_pre_scan) override {
    if (st_.dither_mode != DitherMode::None) st_.dither_mode = DitherMode::FloydSteinberg;
    pre_scan_ = is_pre_scan;
    if (is_pre_scan) {
      needs_zeroed_ = true;
    } else {
      int i = actual_;
      if (i < 1) VIO_FAIL("Cannot quantize to fewer than %d colors", 1);
      if (i > MAXNUMCOLORS) VIO_FAIL("Cannot quantize to more than %d colors", MAXNUMCOLORS);
      if (st_.dither_mode == DitherMode::FloydSteinberg) {
        fserrors_.assign(static_cast<std::size_t>(st_.output_width + 2) * 3, 0);
        if (error_limiter_store_.empty()) init_error_limit();
        on_odd_row_ = false;
      }
      publish_colormap(st_, colormap_, 3, actual_);
    }
    if (needs_zeroed_) {
      std::fill(histogram_.begin(), histogram_.end(), 0);
      needs_zeroed_ = false;
    }
  }

  void color_quantize(T** input_buf, T** output_buf, int num_rows) override {
    if (pre_scan_) prescan_quantize(input_buf, num_rows);
    else if (st_.dither_mode == DitherMode::FloydSteinberg) pass2_fs_dither(input_buf, output_buf, num_rows);
    else pass2_no_dither(input_buf, output_buf, num_rows);
  }

  void finish_pass() override {
    if (pre_scan_) {
      colormap_ = sv_colormap_.rows();
      select_colors(desired_);
      needs_zeroed_ = true;
    }
  }

  void new_color_map() override { needs_zeroed_ = true; }

 private:
  histcell* hist(int c0, int c1, int c2) {
    return &histogram_[(static_cast<std::size_t>(c0) * HIST_C1_ELEMS + c1) * HIST_C2_ELEMS + c2];
  }

  void prescan_quantize(T** input_buf, int num_rows) {
    unsigned width = st_.output_width;
    for (int row = 0; row < num_rows; row++) {
      const T* ptr = input_buf[row];
      for (unsigned col = width; col > 0; col--) {
        histcell* histp = hist(ptr[0] >> C0_SHIFT, ptr[1] >> C1_SHIFT, ptr[2] >> C2_SHIFT);
        if (++(*histp) <= 0) (*histp)--;
        ptr += 3;
      }
    }
  }

  struct Box {
    int c0min, c0max, c1min, c1max, c2min, c2max;
    JLong volume;
    long colorcount;
  };

  static Box* find_biggest_color_pop(Box* boxlist, int numboxes) {
    long maxc = 0;
    Box* which = nullptr;
    for (int i = 0; i < numboxes; i++) {
      Box* b = &boxlist[i];
      if (b->colorcount > maxc && b->volume > 0) {
        which = b;
        maxc = b->colorcount;
      }
    }
    return which;
  }

  static Box* find_biggest_volume(Box* boxlist, int numboxes) {
    JLong maxv = 0;
    Box* which = nullptr;
    for (int i = 0; i < numboxes; i++) {
      Box* b = &boxlist[i];
      if (b->volume > maxv) {
        which = b;
        maxv = b->volume;
      }
    }
    return which;
  }

  void update_box(Box* boxp) {
    int c0, c1, c2;
    int c0min = boxp->c0min, c0max = boxp->c0max, c1min = boxp->c1min, c1max = boxp->c1max,
        c2min = boxp->c2min, c2max = boxp->c2max;
    if (c0max > c0min)
      for (c0 = c0min; c0 <= c0max; c0++)
        for (c1 = c1min; c1 <= c1max; c1++) {
          histcell* histp = hist(c0, c1, c2min);
          for (c2 = c2min; c2 <= c2max; c2++)
            if (*histp++ != 0) {
              boxp->c0min = c0min = c0;
              goto have_c0min;
            }
        }
  have_c0min:
    if (c0max > c0min)
      for (c0 = c0max; c0 >= c0min; c0--)
        for (c1 = c1min; c1 <= c1max; c1++) {
          histcell* histp = hist(c0, c1, c2min);
          for (c2 = c2min; c2 <= c2max; c2++)
            if (*histp++ != 0) {
              boxp->c0max = c0max = c0;
              goto have_c0max;
            }
        }
  have_c0max:
    if (c1max > c1min)
      for (c1 = c1min; c1 <= c1max; c1++)
        for (c0 = c0min; c0 <= c0max; c0++) {
          histcell* histp = hist(c0, c1, c2min);
          for (c2 = c2min; c2 <= c2max; c2++)
            if (*histp++ != 0) {
              boxp->c1min = c1min = c1;
              goto have_c1min;
            }
        }
  have_c1min:
    if (c1max > c1min)
      for (c1 = c1max; c1 >= c1min; c1--)
        for (c0 = c0min; c0 <= c0max; c0++) {
          histcell* histp = hist(c0, c1, c2min);
          for (c2 = c2min; c2 <= c2max; c2++)
            if (*histp++ != 0) {
              boxp->c1max = c1max = c1;
              goto have_c1max;
            }
        }
  have_c1max:
    if (c2max > c2min)
      for (c2 = c2min; c2 <= c2max; c2++)
        for (c0 = c0min; c0 <= c0max; c0++) {
          histcell* histp = hist(c0, c1min, c2);
          for (c1 = c1min; c1 <= c1max; c1++, histp += HIST_C2_ELEMS)
            if (*histp != 0) {
              boxp->c2min = c2min = c2;
              goto have_c2min;
            }
        }
  have_c2min:
    if (c2max > c2min)
      for (c2 = c2max; c2 >= c2min; c2--)
        for (c0 = c0min; c0 <= c0max; c0++) {
          histcell* histp = hist(c0, c1min, c2);
          for (c1 = c1min; c1 <= c1max; c1++, histp += HIST_C2_ELEMS)
            if (*histp != 0) {
              boxp->c2max = c2max = c2;
              goto have_c2max;
            }
        }
  have_c2max:
    JLong dist0 = static_cast<JLong>((c0max - c0min) << C0_SHIFT) * C0_SCALE;
    JLong dist1 = static_cast<JLong>((c1max - c1min) << C1_SHIFT) * C1_SCALE;
    JLong dist2 = static_cast<JLong>((c2max - c2min) << C2_SHIFT) * C2_SCALE;
    boxp->volume = dist0 * dist0 + dist1 * dist1 + dist2 * dist2;
    long ccount = 0;
    for (c0 = c0min; c0 <= c0max; c0++)
      for (c1 = c1min; c1 <= c1max; c1++) {
        histcell* histp = hist(c0, c1, c2min);
        for (c2 = c2min; c2 <= c2max; c2++, histp++)
          if (*histp != 0) ccount++;
      }
    boxp->colorcount = ccount;
  }

  int median_cut(Box* boxlist, int numboxes, int desired_colors) {
    while (numboxes < desired_colors) {
      Box* b1 = (numboxes * 2 <= desired_colors) ? find_biggest_color_pop(boxlist, numboxes)
                                                 : find_biggest_volume(boxlist, numboxes);
      if (!b1) break;
      Box* b2 = &boxlist[numboxes];
      *b2 = *b1;
      int c0 = ((b1->c0max - b1->c0min) << C0_SHIFT) * C0_SCALE;
      int c1 = ((b1->c1max - b1->c1min) << C1_SHIFT) * C1_SCALE;
      int c2 = ((b1->c2max - b1->c2min) << C2_SHIFT) * C2_SCALE;
      int cmax, n;
      if (rgb_red[idx(st_.out_color_space)] == 0) {
        cmax = c1;
        n = 1;
        if (c0 > cmax) {
          cmax = c0;
          n = 0;
        }
        if (c2 > cmax) n = 2;
      } else {
        cmax = c1;
        n = 1;
        if (c2 > cmax) {
          cmax = c2;
          n = 2;
        }
        if (c0 > cmax) n = 0;
      }
      int lb;
      switch (n) {
        case 0:
          lb = (b1->c0max + b1->c0min) / 2;
          b1->c0max = lb;
          b2->c0min = lb + 1;
          break;
        case 1:
          lb = (b1->c1max + b1->c1min) / 2;
          b1->c1max = lb;
          b2->c1min = lb + 1;
          break;
        default:
          lb = (b1->c2max + b1->c2min) / 2;
          b1->c2max = lb;
          b2->c2min = lb + 1;
          break;
      }
      update_box(b1);
      update_box(b2);
      numboxes++;
    }
    return numboxes;
  }

  void compute_color(Box* boxp, int icolor) {
    long total = 0, c0total = 0, c1total = 0, c2total = 0;
    for (int c0 = boxp->c0min; c0 <= boxp->c0max; c0++)
      for (int c1 = boxp->c1min; c1 <= boxp->c1max; c1++) {
        histcell* histp = hist(c0, c1, boxp->c2min);
        for (int c2 = boxp->c2min; c2 <= boxp->c2max; c2++) {
          long count;
          if ((count = *histp++) != 0) {
            total += count;
            c0total += ((c0 << C0_SHIFT) + ((1 << C0_SHIFT) >> 1)) * count;
            c1total += ((c1 << C1_SHIFT) + ((1 << C1_SHIFT) >> 1)) * count;
            c2total += ((c2 << C2_SHIFT) + ((1 << C2_SHIFT) >> 1)) * count;
          }
        }
      }
    colormap_[0][icolor] = static_cast<T>((c0total + (total >> 1)) / total);
    colormap_[1][icolor] = static_cast<T>((c1total + (total >> 1)) / total);
    colormap_[2][icolor] = static_cast<T>((c2total + (total >> 1)) / total);
  }

  void select_colors(int desired_colors) {
    std::vector<Box> boxlist(desired_colors);
    int numboxes = 1;
    boxlist[0] = {0, kMaxSample<T> >> C0_SHIFT, 0, kMaxSample<T> >> C1_SHIFT, 0,
                  kMaxSample<T> >> C2_SHIFT, 0, 0};
    update_box(&boxlist[0]);
    numboxes = median_cut(boxlist.data(), numboxes, desired_colors);
    for (int i = 0; i < numboxes; i++) compute_color(&boxlist[i], i);
    actual_ = numboxes;
    st_.actual_number_of_colors = numboxes;
  }

  int find_nearby_colors(int minc0, int minc1, int minc2, T colorlist[]) {
    int numcolors = actual_;
    int maxc0 = minc0 + ((1 << BOX_C0_SHIFT) - (1 << C0_SHIFT));
    int centerc0 = (minc0 + maxc0) >> 1;
    int maxc1 = minc1 + ((1 << BOX_C1_SHIFT) - (1 << C1_SHIFT));
    int centerc1 = (minc1 + maxc1) >> 1;
    int maxc2 = minc2 + ((1 << BOX_C2_SHIFT) - (1 << C2_SHIFT));
    int centerc2 = (minc2 + maxc2) >> 1;
    JLong minmaxdist = 0x7FFFFFFFL;
    std::vector<JLong>& mindist = mindist_;
    mindist.resize(MAXNUMCOLORS);
    auto axis = [](int x, int minc, int maxc, int centerc, int scale, JLong& min_d, JLong& max_d) {
      JLong tdist, mn, mx;
      if (x < minc) {
        tdist = static_cast<JLong>(x - minc) * scale;
        mn = tdist * tdist;
        tdist = static_cast<JLong>(x - maxc) * scale;
        mx = tdist * tdist;
      } else if (x > maxc) {
        tdist = static_cast<JLong>(x - maxc) * scale;
        mn = tdist * tdist;
        tdist = static_cast<JLong>(x - minc) * scale;
        mx = tdist * tdist;
      } else {
        mn = 0;
        if (x <= centerc) {
          tdist = static_cast<JLong>(x - maxc) * scale;
          mx = tdist * tdist;
        } else {
          tdist = static_cast<JLong>(x - minc) * scale;
          mx = tdist * tdist;
        }
      }
      min_d += mn;
      max_d += mx;
    };
    for (int i = 0; i < numcolors; i++) {
      JLong min_dist = 0, max_dist = 0;
      axis(colormap_[0][i], minc0, maxc0, centerc0, C0_SCALE, min_dist, max_dist);
      axis(colormap_[1][i], minc1, maxc1, centerc1, C1_SCALE, min_dist, max_dist);
      axis(colormap_[2][i], minc2, maxc2, centerc2, C2_SCALE, min_dist, max_dist);
      mindist[i] = min_dist;
      if (max_dist < minmaxdist) minmaxdist = max_dist;
    }
    int ncolors = 0;
    for (int i = 0; i < numcolors; i++)
      if (mindist[i] <= minmaxdist) colorlist[ncolors++] = static_cast<T>(i);
    return ncolors;
  }

  void find_best_colors(int minc0, int minc1, int minc2, int numcolors, const T colorlist[],
                        T bestcolor[]) {
    constexpr int N = BOX_C0_ELEMS * BOX_C1_ELEMS * BOX_C2_ELEMS;
    JLong bestdist[N];
    for (int i = 0; i < N; i++) bestdist[i] = 0x7FFFFFFFL;
    const JLong STEP_C0 = (1 << C0_SHIFT) * C0_SCALE;
    const JLong STEP_C1 = (1 << C1_SHIFT) * C1_SCALE;
    const JLong STEP_C2 = (1 << C2_SHIFT) * C2_SCALE;
    for (int i = 0; i < numcolors; i++) {
      int icolor = colorlist[i];
      JLong inc0 = static_cast<JLong>(minc0 - colormap_[0][icolor]) * C0_SCALE;
      JLong dist0 = inc0 * inc0;
      JLong inc1 = static_cast<JLong>(minc1 - colormap_[1][icolor]) * C1_SCALE;
      dist0 += inc1 * inc1;
      JLong inc2 = static_cast<JLong>(minc2 - colormap_[2][icolor]) * C2_SCALE;
      dist0 += inc2 * inc2;
      inc0 = inc0 * (2 * STEP_C0) + STEP_C0 * STEP_C0;
      inc1 = inc1 * (2 * STEP_C1) + STEP_C1 * STEP_C1;
      inc2 = inc2 * (2 * STEP_C2) + STEP_C2 * STEP_C2;
      JLong* bptr = bestdist;
      T* cptr = bestcolor;
      JLong xx0 = inc0;
      for (int ic0 = BOX_C0_ELEMS - 1; ic0 >= 0; ic0--) {
        JLong dist1 = dist0;
        JLong xx1 = inc1;
        for (int ic1 = BOX_C1_ELEMS - 1; ic1 >= 0; ic1--) {
          JLong dist2 = dist1;
          JLong xx2 = inc2;
          for (int ic2 = BOX_C2_ELEMS - 1; ic2 >= 0; ic2--) {
            if (dist2 < *bptr) {
              *bptr = dist2;
              *cptr = static_cast<T>(icolor);
            }
            dist2 += xx2;
            xx2 += 2 * STEP_C2 * STEP_C2;
            bptr++;
            cptr++;
          }
          dist1 += xx1;
          xx1 += 2 * STEP_C1 * STEP_C1;
        }
        dist0 += xx0;
        xx0 += 2 * STEP_C0 * STEP_C0;
      }
    }
  }

  void fill_inverse_cmap(int c0, int c1, int c2) {
    std::vector<T> colorlist(MAXNUMCOLORS);
    T bestcolor[BOX_C0_ELEMS * BOX_C1_ELEMS * BOX_C2_ELEMS];
    c0 >>= BOX_C0_LOG;
    c1 >>= BOX_C1_LOG;
    c2 >>= BOX_C2_LOG;
    int minc0 = (c0 << BOX_C0_SHIFT) + ((1 << C0_SHIFT) >> 1);
    int minc1 = (c1 << BOX_C1_SHIFT) + ((1 << C1_SHIFT) >> 1);
    int minc2 = (c2 << BOX_C2_SHIFT) + ((1 << C2_SHIFT) >> 1);
    int numcolors = find_nearby_colors(minc0, minc1, minc2, colorlist.data());
    find_best_colors(minc0, minc1, minc2, numcolors, colorlist.data(), bestcolor);
    c0 <<= BOX_C0_LOG;
    c1 <<= BOX_C1_LOG;
    c2 <<= BOX_C2_LOG;
    const T* cptr = bestcolor;
    for (int ic0 = 0; ic0 < BOX_C0_ELEMS; ic0++)
      for (int ic1 = 0; ic1 < BOX_C1_ELEMS; ic1++) {
        histcell* cachep = hist(c0 + ic0, c1 + ic1, c2);
        for (int ic2 = 0; ic2 < BOX_C2_ELEMS; ic2++)
          *cachep++ = static_cast<histcell>((*cptr++) + 1);
      }
  }

  void pass2_no_dither(T** input_buf, T** output_buf, int num_rows) {
    unsigned width = st_.output_width;
    for (int row = 0; row < num_rows; row++) {
      const T* inptr = input_buf[row];
      T* outptr = output_buf[row];
      for (unsigned col = width; col > 0; col--) {
        int c0 = (*inptr++) >> C0_SHIFT;
        int c1 = (*inptr++) >> C1_SHIFT;
        int c2 = (*inptr++) >> C2_SHIFT;
        histcell* cachep = hist(c0, c1, c2);
        if (*cachep == 0) fill_inverse_cmap(c0, c1, c2);
        *outptr++ = static_cast<T>(*cachep - 1);
      }
    }
  }

  void pass2_fs_dither(T** input_buf, T** output_buf, int num_rows) {
    unsigned width = st_.output_width;
    const T* range_limit = range_limit_;
    const int* error_limit = error_limiter_;
    const T* colormap0 = colormap_[0];
    const T* colormap1 = colormap_[1];
    const T* colormap2 = colormap_[2];
    for (int row = 0; row < num_rows; row++) {
      const T* inptr = input_buf[row];
      T* outptr = output_buf[row];
      int dir, dir3;
      FsError<T>* errorptr;
      if (on_odd_row_) {
        inptr += (width - 1) * 3;
        outptr += width - 1;
        dir = -1;
        dir3 = -3;
        errorptr = fserrors_.data() + (width + 1) * 3;
        on_odd_row_ = false;
      } else {
        dir = 1;
        dir3 = 3;
        errorptr = fserrors_.data();
        on_odd_row_ = true;
      }
      LocFsError<T> cur0 = 0, cur1 = 0, cur2 = 0;
      LocFsError<T> belowerr0 = 0, belowerr1 = 0, belowerr2 = 0;
      LocFsError<T> bpreverr0 = 0, bpreverr1 = 0, bpreverr2 = 0;
      for (unsigned col = width; col > 0; col--) {
        cur0 = (cur0 + errorptr[dir3 + 0] + 8) >> 4;
        cur1 = (cur1 + errorptr[dir3 + 1] + 8) >> 4;
        cur2 = (cur2 + errorptr[dir3 + 2] + 8) >> 4;
        cur0 = error_limit[cur0];
        cur1 = error_limit[cur1];
        cur2 = error_limit[cur2];
        cur0 += inptr[0];
        cur1 += inptr[1];
        cur2 += inptr[2];
        cur0 = range_limit[cur0];
        cur1 = range_limit[cur1];
        cur2 = range_limit[cur2];
        histcell* cachep = hist(static_cast<int>(cur0 >> C0_SHIFT), static_cast<int>(cur1 >> C1_SHIFT),
                                static_cast<int>(cur2 >> C2_SHIFT));
        if (*cachep == 0)
          fill_inverse_cmap(static_cast<int>(cur0 >> C0_SHIFT), static_cast<int>(cur1 >> C1_SHIFT),
                            static_cast<int>(cur2 >> C2_SHIFT));
        int pixcode = *cachep - 1;
        *outptr = static_cast<T>(pixcode);
        cur0 -= colormap0[pixcode];
        cur1 -= colormap1[pixcode];
        cur2 -= colormap2[pixcode];
        LocFsError<T> bnexterr;
        bnexterr = cur0;
        errorptr[0] = static_cast<FsError<T>>(bpreverr0 + cur0 * 3);
        bpreverr0 = belowerr0 + cur0 * 5;
        belowerr0 = bnexterr;
        cur0 *= 7;
        bnexterr = cur1;
        errorptr[1] = static_cast<FsError<T>>(bpreverr1 + cur1 * 3);
        bpreverr1 = belowerr1 + cur1 * 5;
        belowerr1 = bnexterr;
        cur1 *= 7;
        bnexterr = cur2;
        errorptr[2] = static_cast<FsError<T>>(bpreverr2 + cur2 * 3);
        bpreverr2 = belowerr2 + cur2 * 5;
        belowerr2 = bnexterr;
        cur2 *= 7;
        inptr += dir3;
        outptr += dir;
        errorptr += dir3;
      }
      errorptr[0] = static_cast<FsError<T>>(bpreverr0);
      errorptr[1] = static_cast<FsError<T>>(bpreverr1);
      errorptr[2] = static_cast<FsError<T>>(bpreverr2);
    }
  }

  void init_error_limit() {
    error_limiter_store_.assign(kMaxSample<T> * 2 + 1, 0);
    int* table = error_limiter_store_.data() + kMaxSample<T>;
    error_limiter_ = table;
    constexpr int STEPSIZE = (kMaxSample<T> + 1) / 16;
    int in, out = 0;
    for (in = 0; in < STEPSIZE; in++, out++) {
      table[in] = out;
      table[-in] = -out;
    }
    for (; in < STEPSIZE * 3; in++, out += (in & 1) ? 0 : 1) {
      table[in] = out;
      table[-in] = -out;
    }
    for (; in <= kMaxSample<T>; in++) {
      table[in] = out;
      table[-in] = -out;
    }
  }

  DState& st_;
  const T* range_limit_;
  int C0_SCALE = 2, C1_SCALE = 3, C2_SCALE = 1;
  std::vector<histcell> histogram_;
  bool needs_zeroed_ = true;
  bool pre_scan_ = false;
  SampleArray<T> sv_colormap_, ext_colormap_;
  T** colormap_ = nullptr;
  int desired_ = 0;
  int actual_ = 0;
  std::vector<FsError<T>> fserrors_;
  bool on_odd_row_ = false;
  std::vector<int> error_limiter_store_;
  int* error_limiter_ = nullptr;
  std::vector<JLong> mindist_;
};

}  // namespace vio::jpeg::detail
