// vio::jpeg - lossless transformations of JPEG coefficient data (rotation,
// flipping, transposition, cropping incl. extension, wipe, drop, roll,
// grayscale conversion) and marker copying.
//
// Typical use (see tests/jpeg/tools/jpeg_transform.cpp):
//   Decoder src; copy_markers_setup(src, CopyOption::Comments); src.open(data);
//   Transform t; t.transform = TransformOp::Rot90;
//   if (!request_workspace(src, t)) ...;          // -perfect failure
//   BlockArray* coefs = src.core().read_coefficients();
//   Encoder dst; copy_critical_parameters(src, dst);
//   BlockArray* out = adjust_parameters(src, dst, coefs, t);
//   dst.core().write_coefficients(out);
//   copy_markers_execute(src, dst, CopyOption::Comments);
//   execute_transform(src, dst, coefs, t);
//   auto jpeg = dst.finish();
#pragma once

#include <cctype>

#include "decoder.hpp"
#include "encoder.hpp"

namespace vio::jpeg {

enum class TransformOp {
  None, FlipH, FlipV, Transpose, Transverse, Rot90, Rot180, Rot270, Wipe, Drop, Roll
};

enum class CropCode { Unset, Pos, Neg, Force, Reflect };

enum class CopyOption { None, Comments, All, AllExceptICC, ICC };

struct Transform {
  TransformOp transform = TransformOp::None;
  bool perfect = false;
  bool trim = false;
  bool force_grayscale = false;
  bool crop = false;
  bool slow_hflip = false;

  unsigned crop_width = 0;
  CropCode crop_width_set = CropCode::Unset;
  unsigned crop_height = 0;
  CropCode crop_height_set = CropCode::Unset;
  unsigned crop_xoffset = 0;
  CropCode crop_xoffset_set = CropCode::Unset;
  unsigned crop_yoffset = 0;
  CropCode crop_yoffset_set = CropCode::Unset;

  // Drop source (JXFORM_DROP)
  detail::DState* drop_ptr = nullptr;
  detail::BlockArray* drop_coef_arrays = nullptr;

  // ---- computed by request_workspace() ----
  int num_components = 0;
  std::vector<detail::BlockArray> workspace;
  unsigned output_width = 0, output_height = 0;
  unsigned x_crop_offset = 0, y_crop_offset = 0;
  unsigned drop_width = 0, drop_height = 0;
  int iMCU_sample_width = 0, iMCU_sample_height = 0;
};

namespace detail::xform {

using jpeg::Block;
using detail::BlockArray;
using detail::ComponentInfo;
using detail::CState;
using detail::DState;

inline Block* row(BlockArray* a, int ci, unsigned r) { return a[ci].row(r); }

inline void copy_blocks(const Block* src, Block* dst, unsigned n) {
  std::memcpy(dst, src, n * sizeof(Block));
}
inline void zero_blocks(Block* dst, unsigned n) { std::memset(dst, 0, n * sizeof(Block)); }

inline void dequant_comp(const ComponentInfo& c, BlockArray& arr, const QuantTable& q1) {
  const QuantTable& q = *c.quant_table;
  for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor)
    for (int oy = 0; oy < c.v_samp_factor; oy++) {
      Block* blk = arr.row(by + oy);
      for (unsigned bx = 0; bx < c.width_in_blocks; bx++) {
        Coef* ptr = blk[bx].data();
        for (int k = 0; k < DCTSIZE2; k++)
          if (q.quantval[k] != q1.quantval[k]) {
            ptr[k] = static_cast<Coef>(ptr[k] * q.quantval[k]);
            if (q1.quantval[k] != 0) ptr[k] = static_cast<Coef>(ptr[k] / q1.quantval[k]);
          }
      }
    }
}

inline void requant_comp(const ComponentInfo& c, BlockArray& arr, const QuantTable& q1) {
  const QuantTable& q = *c.quant_table;
  for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor)
    for (int oy = 0; oy < c.v_samp_factor; oy++) {
      Block* blk = arr.row(by + oy);
      for (unsigned bx = 0; bx < c.width_in_blocks; bx++) {
        Coef* ptr = blk[bx].data();
        for (int k = 0; k < DCTSIZE2; k++) {
          Coef temp = static_cast<Coef>(q.quantval[k]);
          Coef qval = static_cast<Coef>(q1.quantval[k]);
          if (temp != qval && qval != 0) {
            temp = static_cast<Coef>(temp * ptr[k]);
            if (temp < 0) {
              temp = static_cast<Coef>(-temp);
              temp = static_cast<Coef>(temp + (qval >> 1));
              temp = temp >= qval ? static_cast<Coef>(temp / qval) : Coef{0};
              temp = static_cast<Coef>(-temp);
            } else {
              temp = static_cast<Coef>(temp + (qval >> 1));
              temp = temp >= qval ? static_cast<Coef>(temp / qval) : Coef{0};
            }
            ptr[k] = temp;
          }
        }
      }
    }
}

inline Coef largest_common_denominator(Coef a, Coef b) {
  if (a == 0) return b;
  if (b == 0) return a;
  Coef c;
  do {
    c = static_cast<Coef>(a % b);
    a = b;
    b = c;
  } while (c);
  return a;
}

inline void adjust_quant(DState& src, BlockArray* src_arrays, DState& drop, BlockArray* drop_arrays,
                         bool trim, CState& dst) {
  for (int ci = 0; ci < dst.num_components && ci < drop.num_components; ci++) {
    ComponentInfo& c1 = src.comp_info[ci];
    ComponentInfo& c2 = drop.comp_info[ci];
    if (!c1.quant_table) VIO_FAIL("Quantization table 0x%02x was not defined", c1.quant_tbl_no);
    if (!c2.quant_table) VIO_FAIL("Quantization table 0x%02x was not defined", c2.quant_tbl_no);
    const QuantTable& q1 = *c1.quant_table;
    const QuantTable& q2 = *c2.quant_table;
    for (int k = 0; k < DCTSIZE2; k++) {
      if (q1.quantval[k] != q2.quantval[k]) {
        if (trim) {
          requant_comp(c2, drop_arrays[ci], q1);
        } else {
          QuantTable& q3 = *dst.quant_tbl_ptrs[c1.quant_tbl_no];
          for (k = 0; k < DCTSIZE2; k++)
            if (q1.quantval[k] != q2.quantval[k])
              q3.quantval[k] = static_cast<std::uint16_t>(largest_common_denominator(
                  static_cast<Coef>(q1.quantval[k]), static_cast<Coef>(q2.quantval[k])));
          dequant_comp(c1, src_arrays[ci], q3);
          dequant_comp(c2, drop_arrays[ci], q3);
        }
        break;
      }
    }
  }
}

inline void do_drop(CState& dst, unsigned x_off, unsigned y_off, BlockArray* src_arrays,
                    DState& drop, BlockArray* drop_arrays, unsigned dw, unsigned dh) {
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned cw = dw * c.h_samp_factor, ch = dh * c.v_samp_factor;
    unsigned xb = x_off * c.h_samp_factor, yb = y_off * c.v_samp_factor;
    for (unsigned by = 0; by < ch; by += c.v_samp_factor) {
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* d = row(src_arrays, ci, by + yb + oy) + xb;
        if (ci < drop.num_components) copy_blocks(row(drop_arrays, ci, by + oy), d, cw);
        else zero_blocks(d, cw);
      }
    }
  }
}

inline void do_crop(CState& dst, unsigned x_off, unsigned y_off, BlockArray* src_arrays,
                    BlockArray* dst_arrays) {
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned xb = x_off * c.h_samp_factor, yb = y_off * c.v_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++)
        copy_blocks(row(src_arrays, ci, by + yb + oy) + xb, row(dst_arrays, ci, by + oy),
                    c.width_in_blocks);
  }
}

// Shared geometry for the crop-extension variants.
inline void ext_mcu_counts(DState& src, CState& dst, const Transform& info, unsigned& MCU_cols,
                           unsigned& MCU_rows) {
  MCU_cols = src.output_width / (dst.max_h_samp_factor * DCTSIZE);
  MCU_rows = src.output_height / (dst.max_v_samp_factor * DCTSIZE);
  if (!info.trim) {
    int MCU_width, MCU_height;
    if (info.num_components == 1) {
      MCU_width = src.min_DCT_scaled_size;
      MCU_height = src.min_DCT_scaled_size;
    } else {
      MCU_width = src.max_h_samp_factor * src.min_DCT_scaled_size;
      MCU_height = src.max_v_samp_factor * src.min_DCT_scaled_size;
    }
    if (src.output_width % static_cast<unsigned>(MCU_width)) MCU_cols++;
    if (src.output_height % static_cast<unsigned>(MCU_height)) MCU_rows++;
  }
}

enum class Ext { Zero, Flat, Reflect };

template <Ext kind>
inline void do_crop_ext(DState& src, CState& dst, const Transform& info, BlockArray* src_arrays,
                        BlockArray* dst_arrays) {
  unsigned MCU_cols, MCU_rows;
  ext_mcu_counts(src, dst, info, MCU_cols, MCU_rows);
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned comp_width = MCU_cols * c.h_samp_factor;
    if (kind != Ext::Zero && comp_width == 0) VIO_FAIL("Invalid crop request");
    unsigned comp_height = MCU_rows * c.v_samp_factor;
    unsigned xb = info.x_crop_offset * c.h_samp_factor, yb = info.y_crop_offset * c.v_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor) {
      unsigned src_row;
      if (dst.image_height > src.output_height) {
        if (by < yb || by >= yb + comp_height) {
          for (int oy = 0; oy < c.v_samp_factor; oy++)
            zero_blocks(row(dst_arrays, ci, by + oy), c.width_in_blocks);
          continue;
        }
        src_row = by - yb;
      } else {
        src_row = by + yb;
      }
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* d = row(dst_arrays, ci, by + oy);
        Block* s = row(src_arrays, ci, src_row + oy);
        if (kind == Ext::Zero) {
          if (dst.image_width > src.output_width) {
            if (xb > 0) zero_blocks(d, xb);
            copy_blocks(s, d + xb, comp_width);
            if (c.width_in_blocks > xb + comp_width)
              zero_blocks(d + xb + comp_width, c.width_in_blocks - xb - comp_width);
          } else {
            copy_blocks(s + xb, d, c.width_in_blocks);
          }
        } else if (kind == Ext::Flat) {
          if (xb > 0) {
            zero_blocks(d, xb);
            Coef dc = s[0][0];
            for (unsigned bx = 0; bx < xb; bx++) d[bx][0] = dc;
          }
          copy_blocks(s, d + xb, comp_width);
          if (c.width_in_blocks > xb + comp_width) {
            zero_blocks(d + xb + comp_width, c.width_in_blocks - xb - comp_width);
            Coef dc = s[comp_width - 1][0];
            for (unsigned bx = xb + comp_width; bx < c.width_in_blocks; bx++) d[bx][0] = dc;
          }
        } else {
          copy_blocks(s, d + xb, comp_width);
          if (xb > 0) {
            Block* dst_row_ptr = d + xb;
            for (unsigned dbx = xb; dbx > 0;) {
              Block* src_row_ptr = dst_row_ptr;
              for (unsigned sbx = comp_width; sbx > 0 && dbx > 0; sbx--, dbx--) {
                Coef* dp = (*(--dst_row_ptr)).data();
                const Coef* sp = (*src_row_ptr++).data();
                for (int k = 0; k < DCTSIZE2; k += 2) {
                  *dp++ = *sp++;
                  *dp++ = static_cast<Coef>(-(*sp++));
                }
              }
            }
          }
          if (c.width_in_blocks > xb + comp_width) {
            Block* dst_row_ptr = d + xb + comp_width;
            for (unsigned dbx = c.width_in_blocks - xb - comp_width; dbx > 0;) {
              Block* src_row_ptr = dst_row_ptr;
              for (unsigned sbx = comp_width; sbx > 0 && dbx > 0; sbx--, dbx--) {
                Coef* dp = (*dst_row_ptr++).data();
                const Coef* sp = (*(--src_row_ptr)).data();
                for (int k = 0; k < DCTSIZE2; k += 2) {
                  *dp++ = *sp++;
                  *dp++ = static_cast<Coef>(-(*sp++));
                }
              }
            }
          }
        }
      }
    }
  }
}

inline void do_wipe(CState& dst, unsigned x_off, unsigned y_off, BlockArray* src_arrays,
                    unsigned dw, unsigned dh) {
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned xb = x_off * c.h_samp_factor, ww = dw * c.h_samp_factor;
    unsigned yb = y_off * c.v_samp_factor, bottom = dh * c.v_samp_factor + yb;
    for (; yb < bottom; yb += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++) zero_blocks(row(src_arrays, ci, yb + oy) + xb, ww);
  }
}

inline void do_flatten(CState& dst, unsigned x_off, unsigned y_off, BlockArray* src_arrays,
                       unsigned dw, unsigned dh) {
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned xb = x_off * c.h_samp_factor, ww = dw * c.h_samp_factor, right = ww + xb;
    unsigned yb = y_off * c.v_samp_factor, bottom = dh * c.v_samp_factor + yb;
    for (; yb < bottom; yb += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* b = row(src_arrays, ci, yb + oy);
        zero_blocks(b + xb, ww);
        int average;
        if (xb > 0) {
          int left = b[xb - 1][0];
          if (right < c.width_in_blocks) average = (left + b[right][0]) >> 1;
          else average = left;
        } else if (right < c.width_in_blocks) {
          average = b[right][0];
        } else {
          continue;
        }
        for (unsigned bx = xb; bx < right; bx++) b[bx][0] = static_cast<Coef>(average);
      }
  }
}

inline void do_reflect(CState& dst, unsigned x_off, BlockArray* src_arrays, unsigned dw,
                       unsigned dh) {
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned xb = x_off * c.h_samp_factor, ww = dw * c.h_samp_factor;
    unsigned bottom = dh * c.v_samp_factor;
    for (unsigned yb = 0; yb < bottom; yb += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* b = row(src_arrays, ci, yb + oy);
        if (xb > 0) {
          Block* dst_row_ptr = b + xb;
          for (unsigned dbx = ww; dbx > 0;) {
            Block* src_row_ptr = dst_row_ptr;
            for (unsigned sbx = xb; sbx > 0 && dbx > 0; sbx--, dbx--) {
              Coef* dp = (*dst_row_ptr++).data();
              const Coef* sp = (*(--src_row_ptr)).data();
              for (int k = 0; k < DCTSIZE2; k += 2) {
                *dp++ = *sp++;
                *dp++ = static_cast<Coef>(-(*sp++));
              }
            }
          }
        } else if (c.width_in_blocks > xb + ww) {
          Block* dst_row_ptr = b + xb + ww;
          for (unsigned dbx = ww; dbx > 0;) {
            Block* src_row_ptr = dst_row_ptr;
            unsigned sbx = c.width_in_blocks - xb - ww;
            for (; sbx > 0 && dbx > 0; sbx--, dbx--) {
              Coef* dp = (*(--dst_row_ptr)).data();
              const Coef* sp = (*src_row_ptr++).data();
              for (int k = 0; k < DCTSIZE2; k += 2) {
                *dp++ = *sp++;
                *dp++ = static_cast<Coef>(-(*sp++));
              }
            }
          }
        } else {
          zero_blocks(b + xb, ww);
        }
      }
  }
}

inline void do_flip_h_no_crop(DState& src, CState& dst, unsigned x_off, BlockArray* src_arrays) {
  unsigned MCU_cols = src.output_width / (dst.max_h_samp_factor * DCTSIZE);
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned comp_width = MCU_cols * c.h_samp_factor;
    unsigned xb = x_off * c.h_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* b = row(src_arrays, ci, by + oy);
        for (unsigned bx = 0; bx * 2 < comp_width; bx++) {
          Coef* p1 = b[bx].data();
          Coef* p2 = b[comp_width - bx - 1].data();
          for (int k = 0; k < DCTSIZE2; k += 2) {
            Coef t1 = *p1, t2 = *p2;
            *p1++ = t2;
            *p2++ = t1;
            t1 = *p1;
            t2 = *p2;
            *p1++ = static_cast<Coef>(-t2);
            *p2++ = static_cast<Coef>(-t1);
          }
        }
        if (xb > 0)
          for (unsigned bx = 0; bx < c.width_in_blocks; bx++) b[bx] = b[bx + xb];
      }
  }
}

inline void do_flip_h(DState& src, CState& dst, unsigned x_off, unsigned y_off,
                      BlockArray* src_arrays, BlockArray* dst_arrays) {
  unsigned MCU_cols = src.output_width / (dst.max_h_samp_factor * DCTSIZE);
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned comp_width = MCU_cols * c.h_samp_factor;
    unsigned xb = x_off * c.h_samp_factor, yb = y_off * c.v_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* d = row(dst_arrays, ci, by + oy);
        Block* s = row(src_arrays, ci, by + yb + oy);
        for (unsigned bx = 0; bx < c.width_in_blocks; bx++) {
          if (xb + bx < comp_width) {
            Coef* dp = d[bx].data();
            const Coef* sp = s[comp_width - xb - bx - 1].data();
            for (int k = 0; k < DCTSIZE2; k += 2) {
              *dp++ = *sp++;
              *dp++ = static_cast<Coef>(-(*sp++));
            }
          } else {
            d[bx] = s[bx + xb];
          }
        }
      }
  }
}

inline void do_flip_v(DState& src, CState& dst, unsigned x_off, unsigned y_off,
                      BlockArray* src_arrays, BlockArray* dst_arrays) {
  unsigned MCU_rows = src.output_height / (dst.max_v_samp_factor * DCTSIZE);
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned comp_height = MCU_rows * c.v_samp_factor;
    unsigned xb = x_off * c.h_samp_factor, yb = y_off * c.v_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor) {
      bool mirror = yb + by < comp_height;
      unsigned src_row = mirror ? comp_height - yb - by - c.v_samp_factor : by + yb;
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* d = row(dst_arrays, ci, by + oy);
        if (mirror) {
          Block* s = row(src_arrays, ci, src_row + (c.v_samp_factor - oy - 1)) + xb;
          for (unsigned bx = 0; bx < c.width_in_blocks; bx++) {
            Coef* dp = d[bx].data();
            const Coef* sp = s[bx].data();
            for (int i = 0; i < DCTSIZE; i += 2) {
              for (int j = 0; j < DCTSIZE; j++) *dp++ = *sp++;
              for (int j = 0; j < DCTSIZE; j++) *dp++ = static_cast<Coef>(-(*sp++));
            }
          }
        } else {
          copy_blocks(row(src_arrays, ci, src_row + oy) + xb, d, c.width_in_blocks);
        }
      }
    }
  }
}

inline void transpose_block(Coef* dp, const Coef* sp) {
  for (int i = 0; i < DCTSIZE; i++)
    for (int j = 0; j < DCTSIZE; j++) dp[j * DCTSIZE + i] = sp[i * DCTSIZE + j];
}

inline void do_transpose(CState& dst, unsigned x_off, unsigned y_off, BlockArray* src_arrays,
                         BlockArray* dst_arrays) {
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned xb = x_off * c.h_samp_factor, yb = y_off * c.v_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* d = row(dst_arrays, ci, by + oy);
        for (unsigned bx = 0; bx < c.width_in_blocks; bx += c.h_samp_factor)
          for (int ox = 0; ox < c.h_samp_factor; ox++)
            transpose_block(d[bx + ox].data(),
                            row(src_arrays, ci, bx + xb + ox)[by + oy + yb].data());
      }
  }
}

inline void do_rot_90(DState& src, CState& dst, unsigned x_off, unsigned y_off,
                      BlockArray* src_arrays, BlockArray* dst_arrays) {
  unsigned MCU_cols = src.output_height / (dst.max_h_samp_factor * DCTSIZE);
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned comp_width = MCU_cols * c.h_samp_factor;
    unsigned xb = x_off * c.h_samp_factor, yb = y_off * c.v_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* d = row(dst_arrays, ci, by + oy);
        for (unsigned bx = 0; bx < c.width_in_blocks; bx += c.h_samp_factor) {
          bool mirror = xb + bx < comp_width;
          unsigned sr = mirror ? comp_width - xb - bx - c.h_samp_factor : bx + xb;
          for (int ox = 0; ox < c.h_samp_factor; ox++) {
            Coef* dp = d[bx + ox].data();
            if (mirror) {
              const Coef* sp =
                  row(src_arrays, ci, sr + (c.h_samp_factor - ox - 1))[by + oy + yb].data();
              for (int i = 0; i < DCTSIZE; i++) {
                for (int j = 0; j < DCTSIZE; j++) dp[j * DCTSIZE + i] = sp[i * DCTSIZE + j];
                i++;
                for (int j = 0; j < DCTSIZE; j++)
                  dp[j * DCTSIZE + i] = static_cast<Coef>(-sp[i * DCTSIZE + j]);
              }
            } else {
              transpose_block(dp, row(src_arrays, ci, sr + ox)[by + oy + yb].data());
            }
          }
        }
      }
  }
}

inline void do_rot_270(DState& src, CState& dst, unsigned x_off, unsigned y_off,
                       BlockArray* src_arrays, BlockArray* dst_arrays) {
  unsigned MCU_rows = src.output_width / (dst.max_v_samp_factor * DCTSIZE);
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned comp_height = MCU_rows * c.v_samp_factor;
    unsigned xb = x_off * c.h_samp_factor, yb = y_off * c.v_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* d = row(dst_arrays, ci, by + oy);
        for (unsigned bx = 0; bx < c.width_in_blocks; bx += c.h_samp_factor)
          for (int ox = 0; ox < c.h_samp_factor; ox++) {
            Coef* dp = d[bx + ox].data();
            Block* s = row(src_arrays, ci, bx + xb + ox);
            if (yb + by < comp_height) {
              const Coef* sp = s[comp_height - yb - by - oy - 1].data();
              for (int i = 0; i < DCTSIZE; i++)
                for (int j = 0; j < DCTSIZE; j++) {
                  dp[j * DCTSIZE + i] = sp[i * DCTSIZE + j];
                  j++;
                  dp[j * DCTSIZE + i] = static_cast<Coef>(-sp[i * DCTSIZE + j]);
                }
            } else {
              transpose_block(dp, s[by + oy + yb].data());
            }
          }
      }
  }
}

inline void do_roll(DState& src, CState& dst, unsigned x_shift, unsigned y_shift,
                    BlockArray* src_arrays, BlockArray* dst_arrays) {
  unsigned MCU_cols = src.output_width / (dst.max_h_samp_factor * DCTSIZE);
  unsigned MCU_rows = src.output_height / (dst.max_v_samp_factor * DCTSIZE);
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned comp_width = MCU_cols * c.h_samp_factor, comp_height = MCU_rows * c.v_samp_factor;
    unsigned xs = x_shift * c.h_samp_factor, ys = y_shift * c.v_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* d = row(dst_arrays, ci, by + oy);
        for (unsigned bx = 0; bx < c.width_in_blocks; bx++) {
          unsigned sy = by < ys ? comp_height - ys + by : by - ys;
          unsigned sx = bx < xs ? comp_width - xs + bx : bx - xs;
          if (by >= comp_height) sy = by;
          if (bx >= comp_width) sx = bx;
          d[bx] = row(src_arrays, ci, sy + oy)[sx];
        }
      }
  }
}

inline void do_rot_180(DState& src, CState& dst, unsigned x_off, unsigned y_off,
                       BlockArray* src_arrays, BlockArray* dst_arrays) {
  unsigned MCU_cols = src.output_width / (dst.max_h_samp_factor * DCTSIZE);
  unsigned MCU_rows = src.output_height / (dst.max_v_samp_factor * DCTSIZE);
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned comp_width = MCU_cols * c.h_samp_factor, comp_height = MCU_rows * c.v_samp_factor;
    unsigned xb = x_off * c.h_samp_factor, yb = y_off * c.v_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor) {
      bool vmirror = yb + by < comp_height;
      unsigned sr = vmirror ? comp_height - yb - by - c.v_samp_factor : by + yb;
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* d = row(dst_arrays, ci, by + oy);
        if (vmirror) {
          Block* s = row(src_arrays, ci, sr + (c.v_samp_factor - oy - 1));
          for (unsigned bx = 0; bx < c.width_in_blocks; bx++) {
            Coef* dp = d[bx].data();
            if (xb + bx < comp_width) {
              const Coef* sp = s[comp_width - xb - bx - 1].data();
              for (int i = 0; i < DCTSIZE; i += 2) {
                for (int j = 0; j < DCTSIZE; j += 2) {
                  *dp++ = *sp++;
                  *dp++ = static_cast<Coef>(-(*sp++));
                }
                for (int j = 0; j < DCTSIZE; j += 2) {
                  *dp++ = static_cast<Coef>(-(*sp++));
                  *dp++ = *sp++;
                }
              }
            } else {
              const Coef* sp = s[xb + bx].data();
              for (int i = 0; i < DCTSIZE; i += 2) {
                for (int j = 0; j < DCTSIZE; j++) *dp++ = *sp++;
                for (int j = 0; j < DCTSIZE; j++) *dp++ = static_cast<Coef>(-(*sp++));
              }
            }
          }
        } else {
          Block* s = row(src_arrays, ci, sr + oy);
          for (unsigned bx = 0; bx < c.width_in_blocks; bx++) {
            if (xb + bx < comp_width) {
              Coef* dp = d[bx].data();
              const Coef* sp = s[comp_width - xb - bx - 1].data();
              for (int i = 0; i < DCTSIZE2; i += 2) {
                *dp++ = *sp++;
                *dp++ = static_cast<Coef>(-(*sp++));
              }
            } else {
              d[bx] = s[bx + xb];
            }
          }
        }
      }
    }
  }
}

inline void do_transverse(DState& src, CState& dst, unsigned x_off, unsigned y_off,
                          BlockArray* src_arrays, BlockArray* dst_arrays) {
  unsigned MCU_cols = src.output_height / (dst.max_h_samp_factor * DCTSIZE);
  unsigned MCU_rows = src.output_width / (dst.max_v_samp_factor * DCTSIZE);
  for (int ci = 0; ci < dst.num_components; ci++) {
    const ComponentInfo& c = dst.comp_info[ci];
    unsigned comp_width = MCU_cols * c.h_samp_factor, comp_height = MCU_rows * c.v_samp_factor;
    unsigned xb = x_off * c.h_samp_factor, yb = y_off * c.v_samp_factor;
    for (unsigned by = 0; by < c.height_in_blocks; by += c.v_samp_factor)
      for (int oy = 0; oy < c.v_samp_factor; oy++) {
        Block* d = row(dst_arrays, ci, by + oy);
        for (unsigned bx = 0; bx < c.width_in_blocks; bx += c.h_samp_factor) {
          bool xm = xb + bx < comp_width;
          unsigned sr = xm ? comp_width - xb - bx - c.h_samp_factor : bx + xb;
          for (int ox = 0; ox < c.h_samp_factor; ox++) {
            Coef* dp = d[bx + ox].data();
            if (yb + by < comp_height) {
              if (xm) {
                const Coef* sp = row(src_arrays, ci, sr + (c.h_samp_factor - ox - 1))
                                     [comp_height - yb - by - oy - 1].data();
                for (int i = 0; i < DCTSIZE; i++) {
                  for (int j = 0; j < DCTSIZE; j++) {
                    dp[j * DCTSIZE + i] = sp[i * DCTSIZE + j];
                    j++;
                    dp[j * DCTSIZE + i] = static_cast<Coef>(-sp[i * DCTSIZE + j]);
                  }
                  i++;
                  for (int j = 0; j < DCTSIZE; j++) {
                    dp[j * DCTSIZE + i] = static_cast<Coef>(-sp[i * DCTSIZE + j]);
                    j++;
                    dp[j * DCTSIZE + i] = sp[i * DCTSIZE + j];
                  }
                }
              } else {
                const Coef* sp =
                    row(src_arrays, ci, sr + ox)[comp_height - yb - by - oy - 1].data();
                for (int i = 0; i < DCTSIZE; i++)
                  for (int j = 0; j < DCTSIZE; j++) {
                    dp[j * DCTSIZE + i] = sp[i * DCTSIZE + j];
                    j++;
                    dp[j * DCTSIZE + i] = static_cast<Coef>(-sp[i * DCTSIZE + j]);
                  }
              }
            } else {
              if (xm) {
                const Coef* sp =
                    row(src_arrays, ci, sr + (c.h_samp_factor - ox - 1))[by + oy + yb].data();
                for (int i = 0; i < DCTSIZE; i++) {
                  for (int j = 0; j < DCTSIZE; j++) dp[j * DCTSIZE + i] = sp[i * DCTSIZE + j];
                  i++;
                  for (int j = 0; j < DCTSIZE; j++)
                    dp[j * DCTSIZE + i] = static_cast<Coef>(-sp[i * DCTSIZE + j]);
                }
              } else {
                transpose_block(dp, row(src_arrays, ci, sr + ox)[by + oy + yb].data());
              }
            }
          }
        }
      }
  }
}

inline void trim_right_edge(Transform& info, unsigned full_width) {
  unsigned x_off = info.transform == TransformOp::Roll ? 0 : info.x_crop_offset;
  unsigned MCU_cols = info.output_width / info.iMCU_sample_width;
  if (MCU_cols > 0 && x_off + MCU_cols == full_width / info.iMCU_sample_width)
    info.output_width = MCU_cols * info.iMCU_sample_width;
}
inline void trim_bottom_edge(Transform& info, unsigned full_height) {
  unsigned y_off = info.transform == TransformOp::Roll ? 0 : info.y_crop_offset;
  unsigned MCU_rows = info.output_height / info.iMCU_sample_height;
  if (MCU_rows > 0 && y_off + MCU_rows == full_height / info.iMCU_sample_height)
    info.output_height = MCU_rows * info.iMCU_sample_height;
}

inline void transpose_critical_parameters(CState& dst) {
  std::swap(dst.image_width, dst.image_height);
  for (int ci = 0; ci < dst.num_components; ci++)
    std::swap(dst.comp_info[ci].h_samp_factor, dst.comp_info[ci].v_samp_factor);
  for (auto& q : dst.quant_tbl_ptrs)
    if (q)
      for (int i = 0; i < DCTSIZE; i++)
        for (int j = 0; j < i; j++) std::swap(q->quantval[i * DCTSIZE + j], q->quantval[j * DCTSIZE + i]);
}

inline void adjust_exif_parameters(std::uint8_t* data, unsigned length, unsigned new_width,
                                   unsigned new_height) {
  if (length < 12) return;
  bool motorola;
  if (data[0] == 0x49 && data[1] == 0x49) motorola = false;
  else if (data[0] == 0x4D && data[1] == 0x4D) motorola = true;
  else return;
  auto u16 = [&](unsigned o) {
    return motorola ? (unsigned(data[o]) << 8) + data[o + 1] : (unsigned(data[o + 1]) << 8) + data[o];
  };
  if (motorola) {
    if (data[2] != 0 || data[3] != 0x2A) return;
  } else {
    if (data[3] != 0 || data[2] != 0x2A) return;
  }
  unsigned firstoffset;
  if (motorola) {
    if (data[4] != 0 || data[5] != 0) return;
    firstoffset = (unsigned(data[6]) << 8) + data[7];
  } else {
    if (data[7] != 0 || data[6] != 0) return;
    firstoffset = (unsigned(data[5]) << 8) + data[4];
  }
  if (firstoffset > length - 2) return;
  unsigned number_of_tags = u16(firstoffset);
  if (number_of_tags == 0) return;
  firstoffset += 2;
  for (;;) {
    if (firstoffset > length - 12) return;
    if (u16(firstoffset) == 0x8769) break;
    if (--number_of_tags == 0) return;
    firstoffset += 12;
  }
  unsigned offset;
  if (motorola) {
    if (data[firstoffset + 8] != 0 || data[firstoffset + 9] != 0) return;
    offset = (unsigned(data[firstoffset + 10]) << 8) + data[firstoffset + 11];
  } else {
    if (data[firstoffset + 11] != 0 || data[firstoffset + 10] != 0) return;
    offset = (unsigned(data[firstoffset + 9]) << 8) + data[firstoffset + 8];
  }
  if (offset > length - 2) return;
  number_of_tags = u16(offset);
  if (number_of_tags < 2) return;
  offset += 2;
  do {
    if (offset > length - 12) return;
    unsigned tagnum = u16(offset);
    if (tagnum == 0xA002 || tagnum == 0xA003) {
      unsigned v = tagnum == 0xA002 ? new_width : new_height;
      std::uint8_t* p = data + offset;
      if (motorola) {
        p[2] = 0; p[3] = 4; p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 1; p[8] = 0; p[9] = 0;
        p[10] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        p[11] = static_cast<std::uint8_t>(v & 0xFF);
      } else {
        p[2] = 4; p[3] = 0; p[4] = 1; p[5] = 0; p[6] = 0; p[7] = 0;
        p[8] = static_cast<std::uint8_t>(v & 0xFF);
        p[9] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        p[10] = 0; p[11] = 0;
      }
    }
    offset += 12;
  } while (--number_of_tags);
}

}  // namespace detail::xform

// jtransform_parse_crop_spec(): parses "W[f|r]xH[f|r]{+-}X{+-}Y".
inline bool parse_crop_spec(Transform& info, const char* spec) {
  auto read_int = [](const char*& p, unsigned& result) {
    const char* start = p;
    unsigned val = 0;
    for (; std::isdigit(static_cast<unsigned char>(*p)); p++) val = val * 10 + unsigned(*p - '0');
    result = val;
    return p != start;
  };
  auto suffix = [&](CropCode& set) {
    if (*spec == 'f' || *spec == 'F') {
      spec++;
      set = CropCode::Force;
    } else if (*spec == 'r' || *spec == 'R') {
      spec++;
      set = CropCode::Reflect;
    } else {
      set = CropCode::Pos;
    }
  };
  info.crop = false;
  info.crop_width_set = info.crop_height_set = CropCode::Unset;
  info.crop_xoffset_set = info.crop_yoffset_set = CropCode::Unset;
  if (std::isdigit(static_cast<unsigned char>(*spec))) {
    if (!read_int(spec, info.crop_width)) return false;
    suffix(info.crop_width_set);
  }
  if (*spec == 'x' || *spec == 'X') {
    spec++;
    if (!read_int(spec, info.crop_height)) return false;
    suffix(info.crop_height_set);
  }
  if (*spec == '+' || *spec == '-') {
    info.crop_xoffset_set = *spec == '-' ? CropCode::Neg : CropCode::Pos;
    spec++;
    if (!read_int(spec, info.crop_xoffset)) return false;
  }
  if (*spec == '+' || *spec == '-') {
    info.crop_yoffset_set = *spec == '-' ? CropCode::Neg : CropCode::Pos;
    spec++;
    if (!read_int(spec, info.crop_yoffset)) return false;
  }
  if (*spec != '\0') return false;
  info.crop = true;
  return true;
}

// jtransform_perfect_transform()
inline bool perfect_transform(unsigned image_width, unsigned image_height, int MCU_width,
                              int MCU_height, const Transform& info) {
  bool result = true;
  switch (info.transform) {
    case TransformOp::FlipH:
    case TransformOp::Rot270:
      if (image_width % unsigned(MCU_width)) result = false;
      break;
    case TransformOp::FlipV:
    case TransformOp::Rot90:
      if (image_height % unsigned(MCU_height)) result = false;
      break;
    case TransformOp::Transverse:
    case TransformOp::Rot180:
      if (image_width % unsigned(MCU_width)) result = false;
      if (image_height % unsigned(MCU_height)) result = false;
      break;
    case TransformOp::Roll:
      if (image_width % unsigned(MCU_width) && info.crop_xoffset) result = false;
      if (image_height % unsigned(MCU_height) && info.crop_yoffset) result = false;
      break;
    default:
      break;
  }
  return result;
}

// jtransform_request_workspace(): computes the output geometry and allocates
// workspace arrays.  Call after the header is read and before
// read_coefficients().  Returns false if `perfect` is set and the
// transformation is not perfect.
inline bool request_workspace(detail::DState& src, Transform& info) {
  using namespace detail::xform;
  if (info.force_grayscale && src.jpeg_color_space == ColorSpace::YCbCr && src.num_components == 3)
    info.num_components = 1;
  else
    info.num_components = src.num_components;
  src.output_width = src.image_width;
  src.output_height = src.image_height;
  const int s = src.min_DCT_scaled_size;
  int MCU_width, MCU_height;
  if (info.num_components == 1) {
    MCU_width = s;
    MCU_height = s;
  } else {
    MCU_width = src.max_h_samp_factor * s;
    MCU_height = src.max_v_samp_factor * s;
  }
  if (info.perfect &&
      !perfect_transform(src.output_width, src.output_height, MCU_width, MCU_height, info))
    return false;
  switch (info.transform) {
    case TransformOp::Transpose:
    case TransformOp::Transverse:
    case TransformOp::Rot90:
    case TransformOp::Rot270:
      info.output_width = src.output_height;
      info.output_height = src.output_width;
      if (info.num_components == 1) {
        info.iMCU_sample_width = s;
        info.iMCU_sample_height = s;
      } else {
        info.iMCU_sample_width = src.max_v_samp_factor * s;
        info.iMCU_sample_height = src.max_h_samp_factor * s;
      }
      break;
    default:
      info.output_width = src.output_width;
      info.output_height = src.output_height;
      if (info.num_components == 1) {
        info.iMCU_sample_width = s;
        info.iMCU_sample_height = s;
      } else {
        info.iMCU_sample_width = src.max_h_samp_factor * s;
        info.iMCU_sample_height = src.max_v_samp_factor * s;
      }
      break;
  }
  auto bad = [] { VIO_FAIL("Invalid crop request"); };
  if (info.crop) {
    if (info.crop_xoffset_set == CropCode::Unset) info.crop_xoffset = 0;
    if (info.crop_yoffset_set == CropCode::Unset) info.crop_yoffset = 0;
    if (info.crop_width_set == CropCode::Unset) {
      if (info.crop_xoffset >= info.output_width) bad();
      info.crop_width =
          info.transform == TransformOp::Roll ? 0 : info.output_width - info.crop_xoffset;
    } else if (info.crop_width > info.output_width) {
      if (info.transform != TransformOp::None || info.crop_xoffset >= info.crop_width ||
          info.crop_xoffset > info.crop_width - info.output_width)
        bad();
      if (info.perfect && src.output_width % unsigned(MCU_width)) return false;
    } else if (info.crop_xoffset >= info.output_width || info.crop_width <= 0 ||
               info.crop_xoffset > info.output_width - info.crop_width) {
      bad();
    }
    if (info.crop_height_set == CropCode::Unset) {
      if (info.crop_yoffset >= info.output_height) bad();
      info.crop_height =
          info.transform == TransformOp::Roll ? 0 : info.output_height - info.crop_yoffset;
    } else if (info.crop_height > info.output_height) {
      if (info.transform != TransformOp::None || info.crop_yoffset >= info.crop_height ||
          info.crop_yoffset > info.crop_height - info.output_height)
        bad();
      if (info.perfect && src.output_height % unsigned(MCU_height)) return false;
    } else if (info.crop_yoffset >= info.output_height || info.crop_height <= 0 ||
               info.crop_yoffset > info.output_height - info.crop_height) {
      bad();
    }
    unsigned xoffset, yoffset;
    if (info.crop_xoffset_set != CropCode::Neg) xoffset = info.crop_xoffset;
    else if (info.crop_width > info.output_width)
      xoffset = info.crop_width - info.output_width - info.crop_xoffset;
    else
      xoffset = info.output_width - info.crop_width - info.crop_xoffset;
    if (info.crop_yoffset_set != CropCode::Neg) yoffset = info.crop_yoffset;
    else if (info.crop_height > info.output_height)
      yoffset = info.crop_height - info.output_height - info.crop_yoffset;
    else
      yoffset = info.output_height - info.crop_height - info.crop_yoffset;
    switch (info.transform) {
      case TransformOp::Drop: {
        unsigned itemp = unsigned(info.iMCU_sample_width);
        unsigned dtemp = itemp - 1 - ((xoffset + itemp - 1) % itemp);
        xoffset += dtemp;
        if (info.crop_width <= dtemp) info.drop_width = 0;
        else if (xoffset + info.crop_width - dtemp == info.output_width)
          info.drop_width = (info.crop_width - dtemp + itemp - 1) / itemp;
        else
          info.drop_width = (info.crop_width - dtemp) / itemp;
        itemp = unsigned(info.iMCU_sample_height);
        dtemp = itemp - 1 - ((yoffset + itemp - 1) % itemp);
        yoffset += dtemp;
        if (info.crop_height <= dtemp) info.drop_height = 0;
        else if (yoffset + info.crop_height - dtemp == info.output_height)
          info.drop_height = (info.crop_height - dtemp + itemp - 1) / itemp;
        else
          info.drop_height = (info.crop_height - dtemp) / itemp;
        if (info.drop_width != 0 && info.drop_height != 0) {
          detail::DState& d = *info.drop_ptr;
          for (int ci = 0; ci < info.num_components && ci < d.num_components; ci++) {
            if (d.comp_info[ci].h_samp_factor * src.max_h_samp_factor !=
                src.comp_info[ci].h_samp_factor * d.max_h_samp_factor)
              VIO_FAIL("Component index %d: mismatching sampling ratio %d:%d, %d:%d, %c", ci,
                       d.comp_info[ci].h_samp_factor, d.max_h_samp_factor,
                       src.comp_info[ci].h_samp_factor, src.max_h_samp_factor, 'h');
            if (d.comp_info[ci].v_samp_factor * src.max_v_samp_factor !=
                src.comp_info[ci].v_samp_factor * d.max_v_samp_factor)
              VIO_FAIL("Component index %d: mismatching sampling ratio %d:%d, %d:%d, %c", ci,
                       d.comp_info[ci].v_samp_factor, d.max_v_samp_factor,
                       src.comp_info[ci].v_samp_factor, src.max_v_samp_factor, 'v');
          }
        }
        break;
      }
      case TransformOp::Wipe:
        info.drop_width = static_cast<unsigned>(detail::div_round_up(
            static_cast<long>(info.crop_width + (xoffset % unsigned(info.iMCU_sample_width))),
            static_cast<long>(info.iMCU_sample_width)));
        info.drop_height = static_cast<unsigned>(detail::div_round_up(
            static_cast<long>(info.crop_height + (yoffset % unsigned(info.iMCU_sample_height))),
            static_cast<long>(info.iMCU_sample_height)));
        break;
      default:
        if (info.crop_width_set == CropCode::Force || info.crop_width > info.output_width)
          info.output_width = info.crop_width;
        else if (info.transform != TransformOp::Roll)
          info.output_width = info.crop_width + (xoffset % unsigned(info.iMCU_sample_width));
        if (info.crop_height_set == CropCode::Force || info.crop_height > info.output_height)
          info.output_height = info.crop_height;
        else if (info.transform != TransformOp::Roll)
          info.output_height = info.crop_height + (yoffset % unsigned(info.iMCU_sample_height));
    }
    info.x_crop_offset = xoffset / unsigned(info.iMCU_sample_width);
    info.y_crop_offset = yoffset / unsigned(info.iMCU_sample_height);
  } else {
    info.x_crop_offset = 0;
    info.y_crop_offset = 0;
  }

  bool need_workspace = false, transpose_it = false;
  switch (info.transform) {
    case TransformOp::None:
      if (info.x_crop_offset != 0 || info.y_crop_offset != 0 ||
          info.output_width > src.output_width || info.output_height > src.output_height)
        need_workspace = true;
      break;
    case TransformOp::FlipH:
      if (info.trim) trim_right_edge(info, src.output_width);
      if (info.y_crop_offset != 0 || info.slow_hflip) need_workspace = true;
      break;
    case TransformOp::FlipV:
      if (info.trim) trim_bottom_edge(info, src.output_height);
      need_workspace = true;
      break;
    case TransformOp::Transpose:
      need_workspace = transpose_it = true;
      break;
    case TransformOp::Transverse:
      if (info.trim) {
        trim_right_edge(info, src.output_height);
        trim_bottom_edge(info, src.output_width);
      }
      need_workspace = transpose_it = true;
      break;
    case TransformOp::Rot90:
      if (info.trim) trim_right_edge(info, src.output_height);
      need_workspace = transpose_it = true;
      break;
    case TransformOp::Rot180:
      if (info.trim) {
        trim_right_edge(info, src.output_width);
        trim_bottom_edge(info, src.output_height);
      }
      need_workspace = true;
      break;
    case TransformOp::Rot270:
      if (info.trim) trim_bottom_edge(info, src.output_width);
      need_workspace = transpose_it = true;
      break;
    case TransformOp::Wipe:
    case TransformOp::Drop:
      break;
    case TransformOp::Roll:
      if (info.trim) {
        if (info.crop_xoffset) trim_right_edge(info, src.output_width);
        if (info.crop_yoffset) trim_bottom_edge(info, src.output_height);
      }
      need_workspace = true;
      break;
  }
  info.workspace.clear();
  if (need_workspace) {
    unsigned wi = static_cast<unsigned>(
        detail::div_round_up(static_cast<long>(info.output_width), info.iMCU_sample_width));
    unsigned hi = static_cast<unsigned>(
        detail::div_round_up(static_cast<long>(info.output_height), info.iMCU_sample_height));
    info.workspace.resize(static_cast<std::size_t>(info.num_components));
    for (int ci = 0; ci < info.num_components; ci++) {
      const detail::ComponentInfo& c = src.comp_info[ci];
      int h, v;
      if (info.num_components == 1) h = v = 1;
      else if (transpose_it) h = c.v_samp_factor, v = c.h_samp_factor;
      else h = c.h_samp_factor, v = c.v_samp_factor;
      info.workspace[ci].allocate(static_cast<std::size_t>(wi) * h, static_cast<std::size_t>(hi) * v);
    }
  }
  return true;
}
inline bool request_workspace(Decoder& src, Transform& info) {
  return request_workspace(src.core().st, info);
}

// jpeg_copy_critical_parameters()
inline void copy_critical_parameters(detail::DState& src, detail::CState& dst) {
  if (src.lossless) VIO_FAIL("Requested features are incompatible");
  dst.image_width = src.image_width;
  dst.image_height = src.image_height;
  dst.input_components = src.num_components;
  dst.in_color_space = src.jpeg_color_space;
  dst.data_precision = 8;
  dst.set_defaults();
  dst.set_colorspace(src.jpeg_color_space);
  dst.data_precision = src.data_precision;
  dst.CCIR601_sampling = src.CCIR601_sampling;
  for (int t = 0; t < NUM_QUANT_TBLS; t++) {
    if (src.quant_tbl_ptrs[t]) {
      if (!dst.quant_tbl_ptrs[t]) dst.quant_tbl_ptrs[t].emplace();
      dst.quant_tbl_ptrs[t]->quantval = src.quant_tbl_ptrs[t]->quantval;
      dst.quant_tbl_ptrs[t]->sent_table = false;
    }
  }
  dst.num_components = src.num_components;
  if (dst.num_components < 1 || dst.num_components > MAX_COMPONENTS)
    VIO_FAIL("Too many color components: %d, max %d", dst.num_components, MAX_COMPONENTS);
  for (int ci = 0; ci < dst.num_components; ci++) {
    const detail::ComponentInfo& in = src.comp_info[ci];
    detail::ComponentInfo& out = dst.comp_info[ci];
    out.component_id = in.component_id;
    out.h_samp_factor = in.h_samp_factor;
    out.v_samp_factor = in.v_samp_factor;
    out.quant_tbl_no = in.quant_tbl_no;
    int t = out.quant_tbl_no;
    if (t < 0 || t >= NUM_QUANT_TBLS || !src.quant_tbl_ptrs[t])
      VIO_FAIL("Quantization table 0x%02x was not defined", t);
    if (in.quant_table) {
      for (int k = 0; k < DCTSIZE2; k++)
        if (in.quant_table->quantval[k] != src.quant_tbl_ptrs[t]->quantval[k])
          VIO_FAIL("Cannot transcode due to multiple use of quantization table %d", t);
    }
  }
  if (src.saw_JFIF_marker) {
    if (src.JFIF_major_version == 1) {
      dst.JFIF_major_version = src.JFIF_major_version;
      dst.JFIF_minor_version = src.JFIF_minor_version;
    }
    dst.density_unit = src.density_unit;
    dst.X_density = src.X_density;
    dst.Y_density = src.Y_density;
  }
}
inline void copy_critical_parameters(Decoder& src, Encoder& dst) {
  copy_critical_parameters(src.core().st, dst.core().cs);
}

// jtransform_adjust_parameters(): returns the coefficient arrays to pass to
// write_coefficients().
inline detail::BlockArray* adjust_parameters(detail::DState& src, detail::CState& dst,
                                             detail::BlockArray* src_arrays, Transform& info) {
  using namespace detail::xform;
  if (info.force_grayscale) {
    if (((dst.jpeg_color_space == ColorSpace::YCbCr && dst.num_components == 3) ||
         (dst.jpeg_color_space == ColorSpace::Gray && dst.num_components == 1)) &&
        src.comp_info[0].h_samp_factor == src.max_h_samp_factor &&
        src.comp_info[0].v_samp_factor == src.max_v_samp_factor) {
      int sv = dst.comp_info[0].quant_tbl_no;
      dst.set_colorspace(ColorSpace::Gray);
      dst.comp_info[0].quant_tbl_no = sv;
    } else {
      VIO_FAIL("Unsupported color conversion request");
    }
  } else if (info.num_components == 1) {
    dst.comp_info[0].h_samp_factor = 1;
    dst.comp_info[0].v_samp_factor = 1;
  }
  switch (info.transform) {
    case TransformOp::Transpose:
    case TransformOp::Transverse:
    case TransformOp::Rot90:
    case TransformOp::Rot270:
      dst.image_width = info.output_height;
      dst.image_height = info.output_width;
      transpose_critical_parameters(dst);
      break;
    case TransformOp::Drop:
      if (info.drop_width != 0 && info.drop_height != 0)
        adjust_quant(src, src_arrays, *info.drop_ptr, info.drop_coef_arrays, info.trim, dst);
      break;
    default:
      dst.image_width = info.output_width;
      dst.image_height = info.output_height;
      break;
  }
  if (!src.marker_list.empty()) {
    SavedMarker& m = src.marker_list.front();
    if (m.marker == M_APP0 + 1 && m.data.size() >= 6 && m.data[0] == 0x45 && m.data[1] == 0x78 &&
        m.data[2] == 0x69 && m.data[3] == 0x66 && m.data[4] == 0 && m.data[5] == 0) {
      dst.write_JFIF_header = false;
      if (dst.image_width != src.image_width || dst.image_height != src.image_height)
        adjust_exif_parameters(m.data.data() + 6, static_cast<unsigned>(m.data.size() - 6),
                               dst.image_width, dst.image_height);
    }
  }
  if (!info.workspace.empty()) return info.workspace.data();
  return src_arrays;
}
inline detail::BlockArray* adjust_parameters(Decoder& src, Encoder& dst,
                                             detail::BlockArray* src_arrays, Transform& info) {
  return adjust_parameters(src.core().st, dst.core().cs, src_arrays, info);
}

// jtransform_execute_transform(): call after write_coefficients().  Some
// transforms modify the source arrays in place.
inline void execute_transform(detail::DState& src, detail::CState& dst,
                              detail::BlockArray* src_arrays, Transform& info) {
  using namespace detail::xform;
  BlockArray* dst_arrays = info.workspace.empty() ? nullptr : info.workspace.data();
  switch (info.transform) {
    case TransformOp::None:
      if (info.output_width > src.output_width || info.output_height > src.output_height) {
        if (info.output_width > src.output_width && info.crop_width_set == CropCode::Reflect)
          do_crop_ext<Ext::Reflect>(src, dst, info, src_arrays, dst_arrays);
        else if (info.output_width > src.output_width && info.crop_width_set == CropCode::Force)
          do_crop_ext<Ext::Flat>(src, dst, info, src_arrays, dst_arrays);
        else
          do_crop_ext<Ext::Zero>(src, dst, info, src_arrays, dst_arrays);
      } else if (info.x_crop_offset != 0 || info.y_crop_offset != 0) {
        do_crop(dst, info.x_crop_offset, info.y_crop_offset, src_arrays, dst_arrays);
      }
      break;
    case TransformOp::FlipH:
      if (info.y_crop_offset != 0 || info.slow_hflip)
        do_flip_h(src, dst, info.x_crop_offset, info.y_crop_offset, src_arrays, dst_arrays);
      else
        do_flip_h_no_crop(src, dst, info.x_crop_offset, src_arrays);
      break;
    case TransformOp::FlipV:
      do_flip_v(src, dst, info.x_crop_offset, info.y_crop_offset, src_arrays, dst_arrays);
      break;
    case TransformOp::Transpose:
      do_transpose(dst, info.x_crop_offset, info.y_crop_offset, src_arrays, dst_arrays);
      break;
    case TransformOp::Transverse:
      do_transverse(src, dst, info.x_crop_offset, info.y_crop_offset, src_arrays, dst_arrays);
      break;
    case TransformOp::Rot90:
      do_rot_90(src, dst, info.x_crop_offset, info.y_crop_offset, src_arrays, dst_arrays);
      break;
    case TransformOp::Rot180:
      do_rot_180(src, dst, info.x_crop_offset, info.y_crop_offset, src_arrays, dst_arrays);
      break;
    case TransformOp::Rot270:
      do_rot_270(src, dst, info.x_crop_offset, info.y_crop_offset, src_arrays, dst_arrays);
      break;
    case TransformOp::Wipe:
      if (info.crop_width_set == CropCode::Reflect && info.y_crop_offset == 0 &&
          info.drop_height == static_cast<unsigned>(detail::div_round_up(
                                  static_cast<long>(info.output_height), info.iMCU_sample_height)) &&
          (info.x_crop_offset == 0 ||
           info.x_crop_offset + info.drop_width ==
               static_cast<unsigned>(detail::div_round_up(static_cast<long>(info.output_width),
                                                          info.iMCU_sample_width))))
        do_reflect(dst, info.x_crop_offset, src_arrays, info.drop_width, info.drop_height);
      else if (info.crop_width_set == CropCode::Force)
        do_flatten(dst, info.x_crop_offset, info.y_crop_offset, src_arrays, info.drop_width,
                   info.drop_height);
      else
        do_wipe(dst, info.x_crop_offset, info.y_crop_offset, src_arrays, info.drop_width,
                info.drop_height);
      break;
    case TransformOp::Drop:
      if (info.drop_width != 0 && info.drop_height != 0)
        do_drop(dst, info.x_crop_offset, info.y_crop_offset, src_arrays, *info.drop_ptr,
                info.drop_coef_arrays, info.drop_width, info.drop_height);
      break;
    case TransformOp::Roll:
      do_roll(src, dst, info.x_crop_offset, info.y_crop_offset, src_arrays, dst_arrays);
      break;
  }
}
inline void execute_transform(Decoder& src, Encoder& dst, detail::BlockArray* src_arrays,
                              Transform& info) {
  execute_transform(src.core().st, dst.core().cs, src_arrays, info);
}

// jcopy_markers_setup(): call before Decoder::open().
inline void copy_markers_setup(Decoder& src, CopyOption option) {
  if (option != CopyOption::None && option != CopyOption::ICC) src.save_markers(M_COM, 0xFFFF);
  if (option == CopyOption::All || option == CopyOption::AllExceptICC) {
    for (int m = 0; m < 16; m++) {
      if (option == CopyOption::AllExceptICC && m == 2) continue;
      src.save_markers(M_APP0 + m, 0xFFFF);
    }
  }
  if (option == CopyOption::ICC) src.save_markers(M_APP0 + 2, 0xFFFF);
}

// jcopy_markers_execute(): call just after write_coefficients()/start().
inline void copy_markers_execute(Decoder& src, Encoder& dst, CopyOption option) {
  const auto& cs = dst.core().cs;
  for (const SavedMarker& m : src.markers()) {
    if (option == CopyOption::None) continue;
    if (option == CopyOption::Comments && m.marker != M_COM) continue;
    if (option == CopyOption::AllExceptICC && m.marker == M_APP0 + 2) continue;
    if (option == CopyOption::ICC && m.marker != M_APP0 + 2) continue;
    const auto& d = m.data;
    if (cs.write_JFIF_header && m.marker == M_APP0 && d.size() >= 5 && d[0] == 0x4A &&
        d[1] == 0x46 && d[2] == 0x49 && d[3] == 0x46 && d[4] == 0)
      continue;
    if (cs.write_Adobe_marker && m.marker == M_APP0 + 14 && d.size() >= 5 && d[0] == 0x41 &&
        d[1] == 0x64 && d[2] == 0x6F && d[3] == 0x62 && d[4] == 0x65)
      continue;
    dst.write_marker(m.marker, d);
  }
}

}  // namespace vio::jpeg
