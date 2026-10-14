// vio::jpeg::simd::neon - sequential Huffman encoding of one 8x8 block.
// Zig-zag reordering, the non-zero coefficient bitmap and the magnitude
// categories are computed with NEON; the caller supplies the bit-buffer
// primitives.
#pragma once

#include <arm_neon.h>

#include <cstdint>

#include "../../config.hpp"
#include "../common.hpp"

namespace vio::jpeg::simd::neon {

#if VIO_SIMD_NEON_A64
namespace detail_huff {
alignas(16) inline constexpr std::uint8_t zigzag_idx[128] = {
    0,   1,   2,   3,   16,  17,  32,  33,  18,  19,  4,   5,   6,   7,   20,  21,
    34,  35,  48,  49,  255, 255, 50,  51,  36,  37,  22,  23,  8,   9,   10,  11,
    255, 255, 6,   7,   20,  21,  34,  35,  48,  49,  255, 255, 50,  51,  36,  37,
    54,  55,  40,  41,  26,  27,  12,  13,  14,  15,  28,  29,  42,  43,  56,  57,
    6,   7,   20,  21,  34,  35,  48,  49,  50,  51,  36,  37,  22,  23,  8,   9,
    26,  27,  12,  13,  255, 255, 14,  15,  28,  29,  42,  43,  56,  57,  255, 255,
    52,  53,  54,  55,  40,  41,  26,  27,  12,  13,  255, 255, 14,  15,  28,  29,
    26,  27,  40,  41,  42,  43,  28,  29,  14,  15,  30,  31,  44,  45,  46,  47};
}  // namespace detail_huff

namespace detail_huff {
// A block ready for the bit writer: the magnitude category and the bits of
// each coefficient in zig-zag order (the DC difference first), and the
// bitmap of the non-zero AC coefficients.
struct Prepared {
  alignas(16) std::uint16_t diff[64];
  alignas(16) std::uint8_t nbits[64];
  std::uint64_t bitmap;
};

VIO_INLINE void prepare(const std::int16_t* block, int last_dc_val, Prepared& p) {
  const uint8x16x4_t idx_0123 = {{vld1q_u8(zigzag_idx + 0), vld1q_u8(zigzag_idx + 16),
                                  vld1q_u8(zigzag_idx + 32), vld1q_u8(zigzag_idx + 48)}};
  const uint8x16x4_t idx_4567 = {{vld1q_u8(zigzag_idx + 64), vld1q_u8(zigzag_idx + 80),
                                  vld1q_u8(zigzag_idx + 96), vld1q_u8(zigzag_idx + 112)}};
  const auto* b8 = reinterpret_cast<const std::int8_t*>(block);
  const int8x16x4_t t0123 = {{vld1q_s8(b8 + 0), vld1q_s8(b8 + 16), vld1q_s8(b8 + 32),
                              vld1q_s8(b8 + 48)}};
  const int8x16x4_t t4567 = {{vld1q_s8(b8 + 64), vld1q_s8(b8 + 80), vld1q_s8(b8 + 96),
                              vld1q_s8(b8 + 112)}};
  const int8x16x4_t t2345 = {{t0123.val[2], t0123.val[3], t4567.val[0], t4567.val[1]}};
  const int8x16x3_t t567 = {{t4567.val[1], t4567.val[2], t4567.val[3]}};

  int16x8_t row0 = vreinterpretq_s16_s8(vqtbl4q_s8(t0123, idx_0123.val[0]));
  int16x8_t row1 = vreinterpretq_s16_s8(vqtbl4q_s8(t0123, idx_0123.val[1]));
  int16x8_t row2 = vreinterpretq_s16_s8(vqtbl4q_s8(t2345, idx_0123.val[2]));
  int16x8_t row3 = vreinterpretq_s16_s8(vqtbl4q_s8(t0123, idx_0123.val[3]));
  int16x8_t row4 = vreinterpretq_s16_s8(vqtbl4q_s8(t4567, idx_4567.val[0]));
  int16x8_t row5 = vreinterpretq_s16_s8(vqtbl4q_s8(t2345, idx_4567.val[1]));
  int16x8_t row6 = vreinterpretq_s16_s8(vqtbl4q_s8(t4567, idx_4567.val[2]));
  int16x8_t row7 = vreinterpretq_s16_s8(vqtbl3q_s8(t567, idx_4567.val[3]));

  row0 = vsetq_lane_s16(static_cast<std::int16_t>(block[0] - last_dc_val), row0, 0);
  row1 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t4567.val[0]), 0), row1, 2);
  row2 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t0123.val[1]), 4), row2, 0);
  row2 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t4567.val[2]), 0), row2, 5);
  row5 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t0123.val[1]), 7), row5, 2);
  row5 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t4567.val[2]), 3), row5, 7);
  row6 = vsetq_lane_s16(vgetq_lane_s16(vreinterpretq_s16_s8(t0123.val[3]), 7), row6, 5);

  const int16x8_t rows[8] = {row0, row1, row2, row3, row4, row5, row6, row7};
  uint16x8_t ne[8];
  for (int i = 0; i < 8; i++) ne[i] = vtstq_s16(rows[i], rows[i]);
  uint8x16_t r10 = vuzp1q_u8(vreinterpretq_u8_u16(ne[1]), vreinterpretq_u8_u16(ne[0]));
  uint8x16_t r32 = vuzp1q_u8(vreinterpretq_u8_u16(ne[3]), vreinterpretq_u8_u16(ne[2]));
  uint8x16_t r54 = vuzp1q_u8(vreinterpretq_u8_u16(ne[5]), vreinterpretq_u8_u16(ne[4]));
  uint8x16_t r76 = vuzp1q_u8(vreinterpretq_u8_u16(ne[7]), vreinterpretq_u8_u16(ne[6]));
  const uint8x16_t mask = vreinterpretq_u8_u64(vdupq_n_u64(0x0102040810204080ULL));
  uint8x16_t b3210 = vpaddq_u8(vandq_u8(r32, mask), vandq_u8(r10, mask));
  uint8x16_t b7654 = vpaddq_u8(vandq_u8(r76, mask), vandq_u8(r54, mask));
  uint8x16_t b76543210 = vpaddq_u8(b7654, b3210);
  uint8x8_t bitmap_all = vpadd_u8(vget_low_u8(b76543210), vget_high_u8(b76543210));
  // Bit k of the bitmap is set when AC coefficient k (zig-zag order) is
  // non-zero: walking it from the lowest bit keeps the loop-carried
  // dependency to clearing that bit.
  bitmap_all = vrev64_u8(vrbit_u8(bitmap_all));
  p.bitmap = vget_lane_u64(vreinterpret_u64_u8(bitmap_all), 0) & ~std::uint64_t(1);

  int16x8_t lz_rows[8];
  for (int k = 0; k < 8; k++) {
    const int16x8_t a = vabsq_s16(rows[k]);
    lz_rows[k] = vclzq_s16(a);
    const uint16x8_t m = vshlq_u16(vcltzq_s16(rows[k]), vnegq_s16(lz_rows[k]));
    vst1q_u16(p.diff + k * 8, veorq_u16(vreinterpretq_u16_s16(a), m));
  }
  for (int k = 0; k < 8; k += 2) {
    uint8x16_t lz8 = vuzp1q_u8(vreinterpretq_u8_s16(lz_rows[k]), vreinterpretq_u8_s16(lz_rows[k + 1]));
    vst1q_u8(p.nbits + k * 8, vsubq_u8(vdupq_n_u8(16), lz8));
  }
}
}  // namespace detail_huff

// Encodes a prepared block with the bit writer w (HuffBitWriter).
template <class Table, class W>
VIO_INLINE void huff_encode_prepared(const detail_huff::Prepared& p, const Table& dctbl,
                                     const Table& actbl, W& w) {
  w.put_bits(static_cast<std::uint32_t>(dctbl.emit[p.nbits[0]]) | p.diff[0],
             static_cast<int>(dctbl.emit[p.nbits[0]] >> 32));
  std::uint64_t bitmap = p.bitmap;
  unsigned i = 1;
  const unsigned code_0xf0 = actbl.ehufco[0xf0];
  const int size_0xf0 = actbl.ehufsi[0xf0];
  // Base pointers in registers: one indexed load each per coefficient.
  const std::uint16_t* const diff = p.diff;
  const std::uint8_t* const nbits = p.nbits;
  const std::uint64_t* const emit = actbl.emit;
  // next() appends a coefficient (at most 31 bits); the output is written
  // after every second one, or after one when another might not fit.
  auto next = [&]() VIO_LAMBDA_INLINE {
    const unsigned k = static_cast<unsigned>(__builtin_ctzll(bitmap));
    bitmap &= bitmap - 1;
    unsigned r = k - i;
    i = k + 1;
    while (r > 15) {
      w.put_bits(code_0xf0, size_0xf0);
      r -= 16;
    }
    const std::uint64_t e = emit[(r << 4) + nbits[k]];
    w.append(static_cast<std::uint32_t>(e) | diff[k], static_cast<int>(e >> 32));
  };
  while (bitmap != 0) {
    next();
    if (VIO_UNLIKELY(w.count > 32)) w.flush();
    if (bitmap == 0) break;
    next();
    w.flush();
  }
  if (i != 64) w.append(actbl.ehufco[0], actbl.ehufsi[0]);
  w.flush();
}

#else
namespace detail_huff {
// Natural-order index of each zig-zag position.
inline constexpr std::uint8_t zigzag_natural[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
}  // namespace detail_huff

// AArch32: the coefficients are gathered in zig-zag order with scalar loads
// (32-bit NEON lacks the 4-register table lookups used on AArch64), NEON
// builds the non-zero bitmap, and the non-zero coefficients are visited with
// count-trailing-zeros.  Same contract as the AArch64 version.
template <class Table, class W>
VIO_INLINE void huff_encode_one_block(const std::int16_t* block, int last_dc_val,
                                      const Table& dctbl, const Table& actbl, W& w) {
  auto put_bits = [&](std::uint64_t code, int size) VIO_LAMBDA_INLINE { w.put_bits(code, size); };
  auto put_code = [&](unsigned code, int size, unsigned diff, unsigned nb) VIO_LAMBDA_INLINE {
    w.put_code(code, size, diff, static_cast<int>(nb));
  };
  using namespace detail_huff;
  alignas(16) std::int16_t zz[64];
  zz[0] = 0;
  for (int k = 1; k < 64; k++) zz[k] = block[zigzag_natural[k]];
  const uint8x8_t bit = vreinterpret_u8_u64(vdup_n_u64(0x8040201008040201ULL));
  std::uint64_t nonzero = 0;
  for (int i = 0; i < 8; i++) {
    int16x8_t v = vld1q_s16(zz + i * 8);
    uint8x8_t m = vand_u8(vmovn_u16(vtstq_s16(v, v)), bit);
    m = vpadd_u8(m, m);
    m = vpadd_u8(m, m);
    m = vpadd_u8(m, m);
    nonzero |= static_cast<std::uint64_t>(vget_lane_u8(m, 0)) << (8 * i);
  }
  nonzero &= ~std::uint64_t(1);

  int temp = block[0] - last_dc_val;
  int temp2 = temp;
  if (temp < 0) {
    temp = -temp;
    temp2--;
  }
  unsigned nbits = temp ? 32 - static_cast<unsigned>(__builtin_clz(static_cast<unsigned>(temp))) : 0;
  put_code(dctbl.ehufco[nbits], dctbl.ehufsi[nbits], static_cast<unsigned>(temp2) & ((1u << nbits) - 1),
           nbits);
  int last = 0;
  const unsigned code_0xf0 = actbl.ehufco[0xf0];
  const int size_0xf0 = actbl.ehufsi[0xf0];
  while (nonzero) {
    const int k = __builtin_ctzll(nonzero);
    nonzero &= nonzero - 1;
    unsigned r = static_cast<unsigned>(k - last - 1);
    last = k;
    while (r > 15) {
      put_bits(code_0xf0, size_0xf0);
      r -= 16;
    }
    int c = zz[k];
    int bits = c + (c >> 31);
    nbits = 32 - static_cast<unsigned>(__builtin_clz(static_cast<unsigned>(c < 0 ? -c : c)));
    unsigned rs = (r << 4) + nbits;
    put_code(actbl.ehufco[rs], actbl.ehufsi[rs], static_cast<unsigned>(bits) & ((1u << nbits) - 1),
             nbits);
  }
  if (last != 63) put_bits(actbl.ehufco[0], actbl.ehufsi[0]);
}

#endif

// Common SIMD Huffman kernel signature (HuffEncodeFn): encodes the blocks of
// an MCU into `buffer` and returns the advanced output pointer.
template <class Table>
inline VIO_NO_STACK_CHECK std::uint8_t* encode_blocks(HuffBitState& st, std::uint8_t* buffer,
                                                      const std::int16_t* const* blocks,
                                                      const int* last_dc,
                                                      const Table* const* dctbl,
                                                      const Table* const* actbl, int n) {
  HuffBitWriter w(st, buffer);
#if VIO_SIMD_NEON_A64
  // Each block is prepared before the previous one is written out, so that
  // the vector work overlaps the bit writing (and is not lost when the
  // branch at the end of a block's coefficients is mispredicted).
  detail_huff::Prepared p[2];
  detail_huff::prepare(blocks[0], last_dc[0], p[0]);
  for (int i = 0; i < n; i++) {
    if (i + 1 < n) detail_huff::prepare(blocks[i + 1], last_dc[i + 1], p[(i + 1) & 1]);
    huff_encode_prepared(p[i & 1], *dctbl[i], *actbl[i], w);
  }
#else
  for (int i = 0; i < n; i++) huff_encode_one_block(blocks[i], last_dc[i], *dctbl[i], *actbl[i], w);
#endif
  st = w.state();
  return w.buffer;
}

}  // namespace vio::jpeg::simd::neon
