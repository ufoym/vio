// vio::jpeg - inverse DCT kernels (accurate and fast integer, floating-point
// and scaled).
#pragma once

#include <cmath>

#include "common.hpp"

namespace vio::jpeg::detail {

template <class T>
using IdctElem = std::conditional_t<kBits<T> == 8, int, JLong>;
template <class T>
using IfastMult = std::conditional_t<kBits<T> == 8, std::int32_t, JLong>;
template <class T>
inline constexpr int kIfastScaleBits = kBits<T> == 8 ? 2 : 13;

template <class T>
using IdctFn = void (*)(const void *, const Coef *, T **, unsigned, const T *, int);

#define ONE ((JLong)1)
#define RIGHT_SHIFT(x, shft) ((x) >> (shft))
#define LEFT_SHIFT(a, b) ((JLong)((unsigned long long)(a) << (b)))
#define RANGE_MASK (kMaxSample<T> * 4 + 3)
#define PASS1_BITS (kBits<T> == 8 ? 2 : 1)

namespace idct_int {
#define CONST_BITS 13
#define CONST_SCALE (ONE << CONST_BITS)
#define FIX(x) ((JLong)((x) * CONST_SCALE + 0.5))
#define DESCALE(x, n) RIGHT_SHIFT((x) + (ONE << ((n) - 1)), n)
#define MULTIPLY(var, const) ((var) * (const))
#define DEQUANTIZE(coef, quantval) (((JLong)(std::int32_t)(coef)) * (quantval) * scaling_factor)
inline constexpr JLong FIX_0_298631336 = 2446, FIX_0_390180644 = 3196, FIX_0_541196100 = 4433,
    FIX_0_765366865 = 6270, FIX_0_899976223 = 7373, FIX_1_175875602 = 9633,
    FIX_1_501321110 = 12299, FIX_1_847759065 = 15137, FIX_1_961570560 = 16069,
    FIX_2_053119869 = 16819, FIX_2_562915447 = 20995, FIX_3_072711026 = 25172;

template <class T>
void idct_islow(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp0, tmp1, tmp2, tmp3;
  JLong tmp10, tmp11, tmp12, tmp13;
  JLong z1, z2, z3, z4, z5;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[DCTSIZE2];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = DCTSIZE; ctr > 0; ctr--) {
    if (inptr[DCTSIZE * 1] == 0 && inptr[DCTSIZE * 2] == 0 &&
        inptr[DCTSIZE * 3] == 0 && inptr[DCTSIZE * 4] == 0 &&
        inptr[DCTSIZE * 5] == 0 && inptr[DCTSIZE * 6] == 0 &&
        inptr[DCTSIZE * 7] == 0) {
      int dcval = LEFT_SHIFT(DEQUANTIZE(inptr[DCTSIZE * 0],
                             quantptr[DCTSIZE * 0]), PASS1_BITS);
      wsptr[DCTSIZE * 0] = dcval;
      wsptr[DCTSIZE * 1] = dcval;
      wsptr[DCTSIZE * 2] = dcval;
      wsptr[DCTSIZE * 3] = dcval;
      wsptr[DCTSIZE * 4] = dcval;
      wsptr[DCTSIZE * 5] = dcval;
      wsptr[DCTSIZE * 6] = dcval;
      wsptr[DCTSIZE * 7] = dcval;
      inptr++;
      quantptr++;
      wsptr++;
      continue;
    }
    z2 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    z1 = MULTIPLY(z2 + z3, FIX_0_541196100);
    tmp2 = z1 + MULTIPLY(z3, -FIX_1_847759065);
    tmp3 = z1 + MULTIPLY(z2, FIX_0_765366865);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    tmp0 = LEFT_SHIFT(z2 + z3, CONST_BITS);
    tmp1 = LEFT_SHIFT(z2 - z3, CONST_BITS);
    tmp10 = tmp0 + tmp3;
    tmp13 = tmp0 - tmp3;
    tmp11 = tmp1 + tmp2;
    tmp12 = tmp1 - tmp2;
    tmp0 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    tmp1 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    tmp2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    tmp3 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z1 = tmp0 + tmp3;
    z2 = tmp1 + tmp2;
    z3 = tmp0 + tmp2;
    z4 = tmp1 + tmp3;
    z5 = MULTIPLY(z3 + z4, FIX_1_175875602);
    tmp0 = MULTIPLY(tmp0, FIX_0_298631336);
    tmp1 = MULTIPLY(tmp1, FIX_2_053119869);
    tmp2 = MULTIPLY(tmp2, FIX_3_072711026);
    tmp3 = MULTIPLY(tmp3, FIX_1_501321110);
    z1 = MULTIPLY(z1, -FIX_0_899976223);
    z2 = MULTIPLY(z2, -FIX_2_562915447);
    z3 = MULTIPLY(z3, -FIX_1_961570560);
    z4 = MULTIPLY(z4, -FIX_0_390180644);
    z3 += z5;
    z4 += z5;
    tmp0 += z1 + z3;
    tmp1 += z2 + z4;
    tmp2 += z2 + z3;
    tmp3 += z1 + z4;
    wsptr[DCTSIZE * 0] = (int)DESCALE(tmp10 + tmp3, CONST_BITS - PASS1_BITS);
    wsptr[DCTSIZE * 7] = (int)DESCALE(tmp10 - tmp3, CONST_BITS - PASS1_BITS);
    wsptr[DCTSIZE * 1] = (int)DESCALE(tmp11 + tmp2, CONST_BITS - PASS1_BITS);
    wsptr[DCTSIZE * 6] = (int)DESCALE(tmp11 - tmp2, CONST_BITS - PASS1_BITS);
    wsptr[DCTSIZE * 2] = (int)DESCALE(tmp12 + tmp1, CONST_BITS - PASS1_BITS);
    wsptr[DCTSIZE * 5] = (int)DESCALE(tmp12 - tmp1, CONST_BITS - PASS1_BITS);
    wsptr[DCTSIZE * 3] = (int)DESCALE(tmp13 + tmp0, CONST_BITS - PASS1_BITS);
    wsptr[DCTSIZE * 4] = (int)DESCALE(tmp13 - tmp0, CONST_BITS - PASS1_BITS);
    inptr++;
    quantptr++;
    wsptr++;
  }
  wsptr = workspace;
  for (ctr = 0; ctr < DCTSIZE; ctr++) {
    outptr = output_buf[ctr] + output_col;
#ifndef NO_ZERO_ROW_TEST
    if (wsptr[1] == 0 && wsptr[2] == 0 && wsptr[3] == 0 && wsptr[4] == 0 &&
        wsptr[5] == 0 && wsptr[6] == 0 && wsptr[7] == 0) {
      T dcval = range_limit[(int)DESCALE((JLong)wsptr[0],
                                                PASS1_BITS + 3) & RANGE_MASK];
      outptr[0] = dcval;
      outptr[1] = dcval;
      outptr[2] = dcval;
      outptr[3] = dcval;
      outptr[4] = dcval;
      outptr[5] = dcval;
      outptr[6] = dcval;
      outptr[7] = dcval;
      wsptr += DCTSIZE;
      continue;
    }
#endif
    z2 = (JLong)wsptr[2];
    z3 = (JLong)wsptr[6];
    z1 = MULTIPLY(z2 + z3, FIX_0_541196100);
    tmp2 = z1 + MULTIPLY(z3, -FIX_1_847759065);
    tmp3 = z1 + MULTIPLY(z2, FIX_0_765366865);
    tmp0 = LEFT_SHIFT((JLong)wsptr[0] + (JLong)wsptr[4], CONST_BITS);
    tmp1 = LEFT_SHIFT((JLong)wsptr[0] - (JLong)wsptr[4], CONST_BITS);
    tmp10 = tmp0 + tmp3;
    tmp13 = tmp0 - tmp3;
    tmp11 = tmp1 + tmp2;
    tmp12 = tmp1 - tmp2;
    tmp0 = (JLong)wsptr[7];
    tmp1 = (JLong)wsptr[5];
    tmp2 = (JLong)wsptr[3];
    tmp3 = (JLong)wsptr[1];
    z1 = tmp0 + tmp3;
    z2 = tmp1 + tmp2;
    z3 = tmp0 + tmp2;
    z4 = tmp1 + tmp3;
    z5 = MULTIPLY(z3 + z4, FIX_1_175875602);
    tmp0 = MULTIPLY(tmp0, FIX_0_298631336);
    tmp1 = MULTIPLY(tmp1, FIX_2_053119869);
    tmp2 = MULTIPLY(tmp2, FIX_3_072711026);
    tmp3 = MULTIPLY(tmp3, FIX_1_501321110);
    z1 = MULTIPLY(z1, -FIX_0_899976223);
    z2 = MULTIPLY(z2, -FIX_2_562915447);
    z3 = MULTIPLY(z3, -FIX_1_961570560);
    z4 = MULTIPLY(z4, -FIX_0_390180644);
    z3 += z5;
    z4 += z5;
    tmp0 += z1 + z3;
    tmp1 += z2 + z4;
    tmp2 += z2 + z3;
    tmp3 += z1 + z4;
    outptr[0] = range_limit[(int)DESCALE(tmp10 + tmp3,
                                         CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[7] = range_limit[(int)DESCALE(tmp10 - tmp3,
                                         CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[1] = range_limit[(int)DESCALE(tmp11 + tmp2,
                                         CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[6] = range_limit[(int)DESCALE(tmp11 - tmp2,
                                         CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[2] = range_limit[(int)DESCALE(tmp12 + tmp1,
                                         CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[5] = range_limit[(int)DESCALE(tmp12 - tmp1,
                                         CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[3] = range_limit[(int)DESCALE(tmp13 + tmp0,
                                         CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[4] = range_limit[(int)DESCALE(tmp13 - tmp0,
                                         CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    wsptr += DCTSIZE;
  }
}

template <class T>
void idct_7x7(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp0, tmp1, tmp2, tmp10, tmp11, tmp12, tmp13;
  JLong z1, z2, z3;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[7 * 7];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 7; ctr++, inptr++, quantptr++, wsptr++) {
    tmp13 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    tmp13 = LEFT_SHIFT(tmp13, CONST_BITS);
    tmp13 += ONE << (CONST_BITS - PASS1_BITS - 1);
    z1 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    tmp10 = MULTIPLY(z2 - z3, FIX(0.881747734));
    tmp12 = MULTIPLY(z1 - z2, FIX(0.314692123));
    tmp11 = tmp10 + tmp12 + tmp13 - MULTIPLY(z2, FIX(1.841218003));
    tmp0 = z1 + z3;
    z2 -= tmp0;
    tmp0 = MULTIPLY(tmp0, FIX(1.274162392)) + tmp13;
    tmp10 += tmp0 - MULTIPLY(z3, FIX(0.077722536));
    tmp12 += tmp0 - MULTIPLY(z1, FIX(2.470602249));
    tmp13 += MULTIPLY(z2, FIX(1.414213562));
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    tmp1 = MULTIPLY(z1 + z2, FIX(0.935414347));
    tmp2 = MULTIPLY(z1 - z2, FIX(0.170262339));
    tmp0 = tmp1 - tmp2;
    tmp1 += tmp2;
    tmp2 = MULTIPLY(z2 + z3, -FIX(1.378756276));
    tmp1 += tmp2;
    z2 = MULTIPLY(z1 + z3, FIX(0.613604268));
    tmp0 += z2;
    tmp2 += z2 + MULTIPLY(z3, FIX(1.870828693));
    wsptr[7 * 0] = (int)RIGHT_SHIFT(tmp10 + tmp0, CONST_BITS - PASS1_BITS);
    wsptr[7 * 6] = (int)RIGHT_SHIFT(tmp10 - tmp0, CONST_BITS - PASS1_BITS);
    wsptr[7 * 1] = (int)RIGHT_SHIFT(tmp11 + tmp1, CONST_BITS - PASS1_BITS);
    wsptr[7 * 5] = (int)RIGHT_SHIFT(tmp11 - tmp1, CONST_BITS - PASS1_BITS);
    wsptr[7 * 2] = (int)RIGHT_SHIFT(tmp12 + tmp2, CONST_BITS - PASS1_BITS);
    wsptr[7 * 4] = (int)RIGHT_SHIFT(tmp12 - tmp2, CONST_BITS - PASS1_BITS);
    wsptr[7 * 3] = (int)RIGHT_SHIFT(tmp13, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 7; ctr++) {
    outptr = output_buf[ctr] + output_col;
    tmp13 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    tmp13 = LEFT_SHIFT(tmp13, CONST_BITS);
    z1 = (JLong)wsptr[2];
    z2 = (JLong)wsptr[4];
    z3 = (JLong)wsptr[6];
    tmp10 = MULTIPLY(z2 - z3, FIX(0.881747734));
    tmp12 = MULTIPLY(z1 - z2, FIX(0.314692123));
    tmp11 = tmp10 + tmp12 + tmp13 - MULTIPLY(z2, FIX(1.841218003));
    tmp0 = z1 + z3;
    z2 -= tmp0;
    tmp0 = MULTIPLY(tmp0, FIX(1.274162392)) + tmp13;
    tmp10 += tmp0 - MULTIPLY(z3, FIX(0.077722536));
    tmp12 += tmp0 - MULTIPLY(z1, FIX(2.470602249));
    tmp13 += MULTIPLY(z2, FIX(1.414213562));
    z1 = (JLong)wsptr[1];
    z2 = (JLong)wsptr[3];
    z3 = (JLong)wsptr[5];
    tmp1 = MULTIPLY(z1 + z2, FIX(0.935414347));
    tmp2 = MULTIPLY(z1 - z2, FIX(0.170262339));
    tmp0 = tmp1 - tmp2;
    tmp1 += tmp2;
    tmp2 = MULTIPLY(z2 + z3, -FIX(1.378756276));
    tmp1 += tmp2;
    z2 = MULTIPLY(z1 + z3, FIX(0.613604268));
    tmp0 += z2;
    tmp2 += z2 + MULTIPLY(z3, FIX(1.870828693));
    outptr[0] = range_limit[(int)RIGHT_SHIFT(tmp10 + tmp0,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[6] = range_limit[(int)RIGHT_SHIFT(tmp10 - tmp0,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[1] = range_limit[(int)RIGHT_SHIFT(tmp11 + tmp1,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[5] = range_limit[(int)RIGHT_SHIFT(tmp11 - tmp1,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[2] = range_limit[(int)RIGHT_SHIFT(tmp12 + tmp2,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[4] = range_limit[(int)RIGHT_SHIFT(tmp12 - tmp2,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[3] = range_limit[(int)RIGHT_SHIFT(tmp13,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    wsptr += 7;
  }
}

template <class T>
void idct_6x6(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp0, tmp1, tmp2, tmp10, tmp11, tmp12;
  JLong z1, z2, z3;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[6 * 6];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 6; ctr++, inptr++, quantptr++, wsptr++) {
    tmp0 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    tmp0 = LEFT_SHIFT(tmp0, CONST_BITS);
    tmp0 += ONE << (CONST_BITS - PASS1_BITS - 1);
    tmp2 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    tmp10 = MULTIPLY(tmp2, FIX(0.707106781));
    tmp1 = tmp0 + tmp10;
    tmp11 = RIGHT_SHIFT(tmp0 - tmp10 - tmp10, CONST_BITS - PASS1_BITS);
    tmp10 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    tmp0 = MULTIPLY(tmp10, FIX(1.224744871));
    tmp10 = tmp1 + tmp0;
    tmp12 = tmp1 - tmp0;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    tmp1 = MULTIPLY(z1 + z3, FIX(0.366025404));
    tmp0 = tmp1 + LEFT_SHIFT(z1 + z2, CONST_BITS);
    tmp2 = tmp1 + LEFT_SHIFT(z3 - z2, CONST_BITS);
    tmp1 = LEFT_SHIFT(z1 - z2 - z3, PASS1_BITS);
    wsptr[6 * 0] = (int)RIGHT_SHIFT(tmp10 + tmp0, CONST_BITS - PASS1_BITS);
    wsptr[6 * 5] = (int)RIGHT_SHIFT(tmp10 - tmp0, CONST_BITS - PASS1_BITS);
    wsptr[6 * 1] = (int)(tmp11 + tmp1);
    wsptr[6 * 4] = (int)(tmp11 - tmp1);
    wsptr[6 * 2] = (int)RIGHT_SHIFT(tmp12 + tmp2, CONST_BITS - PASS1_BITS);
    wsptr[6 * 3] = (int)RIGHT_SHIFT(tmp12 - tmp2, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 6; ctr++) {
    outptr = output_buf[ctr] + output_col;
    tmp0 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    tmp0 = LEFT_SHIFT(tmp0, CONST_BITS);
    tmp2 = (JLong)wsptr[4];
    tmp10 = MULTIPLY(tmp2, FIX(0.707106781));
    tmp1 = tmp0 + tmp10;
    tmp11 = tmp0 - tmp10 - tmp10;
    tmp10 = (JLong)wsptr[2];
    tmp0 = MULTIPLY(tmp10, FIX(1.224744871));
    tmp10 = tmp1 + tmp0;
    tmp12 = tmp1 - tmp0;
    z1 = (JLong)wsptr[1];
    z2 = (JLong)wsptr[3];
    z3 = (JLong)wsptr[5];
    tmp1 = MULTIPLY(z1 + z3, FIX(0.366025404));
    tmp0 = tmp1 + LEFT_SHIFT(z1 + z2, CONST_BITS);
    tmp2 = tmp1 + LEFT_SHIFT(z3 - z2, CONST_BITS);
    tmp1 = LEFT_SHIFT(z1 - z2 - z3, CONST_BITS);
    outptr[0] = range_limit[(int)RIGHT_SHIFT(tmp10 + tmp0,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[5] = range_limit[(int)RIGHT_SHIFT(tmp10 - tmp0,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[1] = range_limit[(int)RIGHT_SHIFT(tmp11 + tmp1,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[4] = range_limit[(int)RIGHT_SHIFT(tmp11 - tmp1,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[2] = range_limit[(int)RIGHT_SHIFT(tmp12 + tmp2,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[3] = range_limit[(int)RIGHT_SHIFT(tmp12 - tmp2,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    wsptr += 6;
  }
}

template <class T>
void idct_5x5(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp0, tmp1, tmp10, tmp11, tmp12;
  JLong z1, z2, z3;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[5 * 5];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 5; ctr++, inptr++, quantptr++, wsptr++) {
    tmp12 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    tmp12 = LEFT_SHIFT(tmp12, CONST_BITS);
    tmp12 += ONE << (CONST_BITS - PASS1_BITS - 1);
    tmp0 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    tmp1 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    z1 = MULTIPLY(tmp0 + tmp1, FIX(0.790569415));
    z2 = MULTIPLY(tmp0 - tmp1, FIX(0.353553391));
    z3 = tmp12 + z2;
    tmp10 = z3 + z1;
    tmp11 = z3 - z1;
    tmp12 -= LEFT_SHIFT(z2, 2);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z1 = MULTIPLY(z2 + z3, FIX(0.831253876));
    tmp0 = z1 + MULTIPLY(z2, FIX(0.513743148));
    tmp1 = z1 - MULTIPLY(z3, FIX(2.176250899));
    wsptr[5 * 0] = (int)RIGHT_SHIFT(tmp10 + tmp0, CONST_BITS - PASS1_BITS);
    wsptr[5 * 4] = (int)RIGHT_SHIFT(tmp10 - tmp0, CONST_BITS - PASS1_BITS);
    wsptr[5 * 1] = (int)RIGHT_SHIFT(tmp11 + tmp1, CONST_BITS - PASS1_BITS);
    wsptr[5 * 3] = (int)RIGHT_SHIFT(tmp11 - tmp1, CONST_BITS - PASS1_BITS);
    wsptr[5 * 2] = (int)RIGHT_SHIFT(tmp12, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 5; ctr++) {
    outptr = output_buf[ctr] + output_col;
    tmp12 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    tmp12 = LEFT_SHIFT(tmp12, CONST_BITS);
    tmp0 = (JLong)wsptr[2];
    tmp1 = (JLong)wsptr[4];
    z1 = MULTIPLY(tmp0 + tmp1, FIX(0.790569415));
    z2 = MULTIPLY(tmp0 - tmp1, FIX(0.353553391));
    z3 = tmp12 + z2;
    tmp10 = z3 + z1;
    tmp11 = z3 - z1;
    tmp12 -= LEFT_SHIFT(z2, 2);
    z2 = (JLong)wsptr[1];
    z3 = (JLong)wsptr[3];
    z1 = MULTIPLY(z2 + z3, FIX(0.831253876));
    tmp0 = z1 + MULTIPLY(z2, FIX(0.513743148));
    tmp1 = z1 - MULTIPLY(z3, FIX(2.176250899));
    outptr[0] = range_limit[(int)RIGHT_SHIFT(tmp10 + tmp0,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[4] = range_limit[(int)RIGHT_SHIFT(tmp10 - tmp0,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[1] = range_limit[(int)RIGHT_SHIFT(tmp11 + tmp1,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[3] = range_limit[(int)RIGHT_SHIFT(tmp11 - tmp1,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[2] = range_limit[(int)RIGHT_SHIFT(tmp12,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    wsptr += 5;
  }
}

template <class T>
void idct_3x3(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp0, tmp2, tmp10, tmp12;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[3 * 3];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 3; ctr++, inptr++, quantptr++, wsptr++) {
    tmp0 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    tmp0 = LEFT_SHIFT(tmp0, CONST_BITS);
    tmp0 += ONE << (CONST_BITS - PASS1_BITS - 1);
    tmp2 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    tmp12 = MULTIPLY(tmp2, FIX(0.707106781));
    tmp10 = tmp0 + tmp12;
    tmp2 = tmp0 - tmp12 - tmp12;
    tmp12 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    tmp0 = MULTIPLY(tmp12, FIX(1.224744871));
    wsptr[3 * 0] = (int)RIGHT_SHIFT(tmp10 + tmp0, CONST_BITS - PASS1_BITS);
    wsptr[3 * 2] = (int)RIGHT_SHIFT(tmp10 - tmp0, CONST_BITS - PASS1_BITS);
    wsptr[3 * 1] = (int)RIGHT_SHIFT(tmp2, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 3; ctr++) {
    outptr = output_buf[ctr] + output_col;
    tmp0 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    tmp0 = LEFT_SHIFT(tmp0, CONST_BITS);
    tmp2 = (JLong)wsptr[2];
    tmp12 = MULTIPLY(tmp2, FIX(0.707106781));
    tmp10 = tmp0 + tmp12;
    tmp2 = tmp0 - tmp12 - tmp12;
    tmp12 = (JLong)wsptr[1];
    tmp0 = MULTIPLY(tmp12, FIX(1.224744871));
    outptr[0] = range_limit[(int)RIGHT_SHIFT(tmp10 + tmp0,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[2] = range_limit[(int)RIGHT_SHIFT(tmp10 - tmp0,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[1] = range_limit[(int)RIGHT_SHIFT(tmp2,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    wsptr += 3;
  }
}

template <class T>
void idct_9x9(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp0, tmp1, tmp2, tmp3, tmp10, tmp11, tmp12, tmp13, tmp14;
  JLong z1, z2, z3, z4;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[8 * 9];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 8; ctr++, inptr++, quantptr++, wsptr++) {
    tmp0 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    tmp0 = LEFT_SHIFT(tmp0, CONST_BITS);
    tmp0 += ONE << (CONST_BITS - PASS1_BITS - 1);
    z1 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    tmp3 = MULTIPLY(z3, FIX(0.707106781));
    tmp1 = tmp0 + tmp3;
    tmp2 = tmp0 - tmp3 - tmp3;
    tmp0 = MULTIPLY(z1 - z2, FIX(0.707106781));
    tmp11 = tmp2 + tmp0;
    tmp14 = tmp2 - tmp0 - tmp0;
    tmp0 = MULTIPLY(z1 + z2, FIX(1.328926049));
    tmp2 = MULTIPLY(z1, FIX(1.083350441));
    tmp3 = MULTIPLY(z2, FIX(0.245575608));
    tmp10 = tmp1 + tmp0 - tmp3;
    tmp12 = tmp1 - tmp0 + tmp2;
    tmp13 = tmp1 - tmp2 + tmp3;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    z2 = MULTIPLY(z2, -FIX(1.224744871));
    tmp2 = MULTIPLY(z1 + z3, FIX(0.909038955));
    tmp3 = MULTIPLY(z1 + z4, FIX(0.483689525));
    tmp0 = tmp2 + tmp3 - z2;
    tmp1 = MULTIPLY(z3 - z4, FIX(1.392728481));
    tmp2 += z2 - tmp1;
    tmp3 += z2 + tmp1;
    tmp1 = MULTIPLY(z1 - z3 - z4, FIX(1.224744871));
    wsptr[8 * 0] = (int)RIGHT_SHIFT(tmp10 + tmp0, CONST_BITS - PASS1_BITS);
    wsptr[8 * 8] = (int)RIGHT_SHIFT(tmp10 - tmp0, CONST_BITS - PASS1_BITS);
    wsptr[8 * 1] = (int)RIGHT_SHIFT(tmp11 + tmp1, CONST_BITS - PASS1_BITS);
    wsptr[8 * 7] = (int)RIGHT_SHIFT(tmp11 - tmp1, CONST_BITS - PASS1_BITS);
    wsptr[8 * 2] = (int)RIGHT_SHIFT(tmp12 + tmp2, CONST_BITS - PASS1_BITS);
    wsptr[8 * 6] = (int)RIGHT_SHIFT(tmp12 - tmp2, CONST_BITS - PASS1_BITS);
    wsptr[8 * 3] = (int)RIGHT_SHIFT(tmp13 + tmp3, CONST_BITS - PASS1_BITS);
    wsptr[8 * 5] = (int)RIGHT_SHIFT(tmp13 - tmp3, CONST_BITS - PASS1_BITS);
    wsptr[8 * 4] = (int)RIGHT_SHIFT(tmp14, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 9; ctr++) {
    outptr = output_buf[ctr] + output_col;
    tmp0 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    tmp0 = LEFT_SHIFT(tmp0, CONST_BITS);
    z1 = (JLong)wsptr[2];
    z2 = (JLong)wsptr[4];
    z3 = (JLong)wsptr[6];
    tmp3 = MULTIPLY(z3, FIX(0.707106781));
    tmp1 = tmp0 + tmp3;
    tmp2 = tmp0 - tmp3 - tmp3;
    tmp0 = MULTIPLY(z1 - z2, FIX(0.707106781));
    tmp11 = tmp2 + tmp0;
    tmp14 = tmp2 - tmp0 - tmp0;
    tmp0 = MULTIPLY(z1 + z2, FIX(1.328926049));
    tmp2 = MULTIPLY(z1, FIX(1.083350441));
    tmp3 = MULTIPLY(z2, FIX(0.245575608));
    tmp10 = tmp1 + tmp0 - tmp3;
    tmp12 = tmp1 - tmp0 + tmp2;
    tmp13 = tmp1 - tmp2 + tmp3;
    z1 = (JLong)wsptr[1];
    z2 = (JLong)wsptr[3];
    z3 = (JLong)wsptr[5];
    z4 = (JLong)wsptr[7];
    z2 = MULTIPLY(z2, -FIX(1.224744871));
    tmp2 = MULTIPLY(z1 + z3, FIX(0.909038955));
    tmp3 = MULTIPLY(z1 + z4, FIX(0.483689525));
    tmp0 = tmp2 + tmp3 - z2;
    tmp1 = MULTIPLY(z3 - z4, FIX(1.392728481));
    tmp2 += z2 - tmp1;
    tmp3 += z2 + tmp1;
    tmp1 = MULTIPLY(z1 - z3 - z4, FIX(1.224744871));
    outptr[0] = range_limit[(int)RIGHT_SHIFT(tmp10 + tmp0,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[8] = range_limit[(int)RIGHT_SHIFT(tmp10 - tmp0,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[1] = range_limit[(int)RIGHT_SHIFT(tmp11 + tmp1,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[7] = range_limit[(int)RIGHT_SHIFT(tmp11 - tmp1,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[2] = range_limit[(int)RIGHT_SHIFT(tmp12 + tmp2,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[6] = range_limit[(int)RIGHT_SHIFT(tmp12 - tmp2,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[3] = range_limit[(int)RIGHT_SHIFT(tmp13 + tmp3,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[5] = range_limit[(int)RIGHT_SHIFT(tmp13 - tmp3,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[4] = range_limit[(int)RIGHT_SHIFT(tmp14,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    wsptr += 8;
  }
}

template <class T>
void idct_10x10(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp10, tmp11, tmp12, tmp13, tmp14;
  JLong tmp20, tmp21, tmp22, tmp23, tmp24;
  JLong z1, z2, z3, z4, z5;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[8 * 10];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 8; ctr++, inptr++, quantptr++, wsptr++) {
    z3 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    z3 = LEFT_SHIFT(z3, CONST_BITS);
    z3 += ONE << (CONST_BITS - PASS1_BITS - 1);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    z1 = MULTIPLY(z4, FIX(1.144122806));
    z2 = MULTIPLY(z4, FIX(0.437016024));
    tmp10 = z3 + z1;
    tmp11 = z3 - z2;
    tmp22 = RIGHT_SHIFT(z3 - LEFT_SHIFT(z1 - z2, 1),
                        CONST_BITS - PASS1_BITS);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    z1 = MULTIPLY(z2 + z3, FIX(0.831253876));
    tmp12 = z1 + MULTIPLY(z2, FIX(0.513743148));
    tmp13 = z1 - MULTIPLY(z3, FIX(2.176250899));
    tmp20 = tmp10 + tmp12;
    tmp24 = tmp10 - tmp12;
    tmp21 = tmp11 + tmp13;
    tmp23 = tmp11 - tmp13;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    tmp11 = z2 + z4;
    tmp13 = z2 - z4;
    tmp12 = MULTIPLY(tmp13, FIX(0.309016994));
    z5 = LEFT_SHIFT(z3, CONST_BITS);
    z2 = MULTIPLY(tmp11, FIX(0.951056516));
    z4 = z5 + tmp12;
    tmp10 = MULTIPLY(z1, FIX(1.396802247)) + z2 + z4;
    tmp14 = MULTIPLY(z1, FIX(0.221231742)) - z2 + z4;
    z2 = MULTIPLY(tmp11, FIX(0.587785252));
    z4 = z5 - tmp12 - LEFT_SHIFT(tmp13, CONST_BITS - 1);
    tmp12 = LEFT_SHIFT(z1 - tmp13 - z3, PASS1_BITS);
    tmp11 = MULTIPLY(z1, FIX(1.260073511)) - z2 - z4;
    tmp13 = MULTIPLY(z1, FIX(0.642039522)) - z2 + z4;
    wsptr[8 * 0] = (int)RIGHT_SHIFT(tmp20 + tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 9] = (int)RIGHT_SHIFT(tmp20 - tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 1] = (int)RIGHT_SHIFT(tmp21 + tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 8] = (int)RIGHT_SHIFT(tmp21 - tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 2] = (int)(tmp22 + tmp12);
    wsptr[8 * 7] = (int)(tmp22 - tmp12);
    wsptr[8 * 3] = (int)RIGHT_SHIFT(tmp23 + tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 6] = (int)RIGHT_SHIFT(tmp23 - tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 4] = (int)RIGHT_SHIFT(tmp24 + tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 5] = (int)RIGHT_SHIFT(tmp24 - tmp14, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 10; ctr++) {
    outptr = output_buf[ctr] + output_col;
    z3 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    z3 = LEFT_SHIFT(z3, CONST_BITS);
    z4 = (JLong)wsptr[4];
    z1 = MULTIPLY(z4, FIX(1.144122806));
    z2 = MULTIPLY(z4, FIX(0.437016024));
    tmp10 = z3 + z1;
    tmp11 = z3 - z2;
    tmp22 = z3 - LEFT_SHIFT(z1 - z2, 1);
    z2 = (JLong)wsptr[2];
    z3 = (JLong)wsptr[6];
    z1 = MULTIPLY(z2 + z3, FIX(0.831253876));
    tmp12 = z1 + MULTIPLY(z2, FIX(0.513743148));
    tmp13 = z1 - MULTIPLY(z3, FIX(2.176250899));
    tmp20 = tmp10 + tmp12;
    tmp24 = tmp10 - tmp12;
    tmp21 = tmp11 + tmp13;
    tmp23 = tmp11 - tmp13;
    z1 = (JLong)wsptr[1];
    z2 = (JLong)wsptr[3];
    z3 = (JLong)wsptr[5];
    z3 = LEFT_SHIFT(z3, CONST_BITS);
    z4 = (JLong)wsptr[7];
    tmp11 = z2 + z4;
    tmp13 = z2 - z4;
    tmp12 = MULTIPLY(tmp13, FIX(0.309016994));
    z2 = MULTIPLY(tmp11, FIX(0.951056516));
    z4 = z3 + tmp12;
    tmp10 = MULTIPLY(z1, FIX(1.396802247)) + z2 + z4;
    tmp14 = MULTIPLY(z1, FIX(0.221231742)) - z2 + z4;
    z2 = MULTIPLY(tmp11, FIX(0.587785252));
    z4 = z3 - tmp12 - LEFT_SHIFT(tmp13, CONST_BITS - 1);
    tmp12 = LEFT_SHIFT(z1 - tmp13, CONST_BITS) - z3;
    tmp11 = MULTIPLY(z1, FIX(1.260073511)) - z2 - z4;
    tmp13 = MULTIPLY(z1, FIX(0.642039522)) - z2 + z4;
    outptr[0] = range_limit[(int)RIGHT_SHIFT(tmp20 + tmp10,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[9] = range_limit[(int)RIGHT_SHIFT(tmp20 - tmp10,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[1] = range_limit[(int)RIGHT_SHIFT(tmp21 + tmp11,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[8] = range_limit[(int)RIGHT_SHIFT(tmp21 - tmp11,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[2] = range_limit[(int)RIGHT_SHIFT(tmp22 + tmp12,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[7] = range_limit[(int)RIGHT_SHIFT(tmp22 - tmp12,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[3] = range_limit[(int)RIGHT_SHIFT(tmp23 + tmp13,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[6] = range_limit[(int)RIGHT_SHIFT(tmp23 - tmp13,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[4] = range_limit[(int)RIGHT_SHIFT(tmp24 + tmp14,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    outptr[5] = range_limit[(int)RIGHT_SHIFT(tmp24 - tmp14,
                                             CONST_BITS + PASS1_BITS + 3) &
                            RANGE_MASK];
    wsptr += 8;
  }
}

template <class T>
void idct_11x11(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp10, tmp11, tmp12, tmp13, tmp14;
  JLong tmp20, tmp21, tmp22, tmp23, tmp24, tmp25;
  JLong z1, z2, z3, z4;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[8 * 11];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 8; ctr++, inptr++, quantptr++, wsptr++) {
    tmp10 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    tmp10 = LEFT_SHIFT(tmp10, CONST_BITS);
    tmp10 += ONE << (CONST_BITS - PASS1_BITS - 1);
    z1 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    tmp20 = MULTIPLY(z2 - z3, FIX(2.546640132));
    tmp23 = MULTIPLY(z2 - z1, FIX(0.430815045));
    z4 = z1 + z3;
    tmp24 = MULTIPLY(z4, -FIX(1.155664402));
    z4 -= z2;
    tmp25 = tmp10 + MULTIPLY(z4, FIX(1.356927976));
    tmp21 = tmp20 + tmp23 + tmp25 -
            MULTIPLY(z2, FIX(1.821790775));
    tmp20 += tmp25 + MULTIPLY(z3, FIX(2.115825087));
    tmp23 += tmp25 - MULTIPLY(z1, FIX(1.513598477));
    tmp24 += tmp25;
    tmp22 = tmp24 - MULTIPLY(z3, FIX(0.788749120));
    tmp24 += MULTIPLY(z2, FIX(1.944413522)) -
             MULTIPLY(z1, FIX(1.390975730));
    tmp25 = tmp10 - MULTIPLY(z4, FIX(1.414213562));
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    tmp11 = z1 + z2;
    tmp14 = MULTIPLY(tmp11 + z3 + z4, FIX(0.398430003));
    tmp11 = MULTIPLY(tmp11, FIX(0.887983902));
    tmp12 = MULTIPLY(z1 + z3, FIX(0.670361295));
    tmp13 = tmp14 + MULTIPLY(z1 + z4, FIX(0.366151574));
    tmp10 = tmp11 + tmp12 + tmp13 -
            MULTIPLY(z1, FIX(0.923107866));
    z1    = tmp14 - MULTIPLY(z2 + z3, FIX(1.163011579));
    tmp11 += z1 + MULTIPLY(z2, FIX(2.073276588));
    tmp12 += z1 - MULTIPLY(z3, FIX(1.192193623));
    z1    = MULTIPLY(z2 + z4, -FIX(1.798248910));
    tmp11 += z1;
    tmp13 += z1 + MULTIPLY(z4, FIX(2.102458632));
    tmp14 += MULTIPLY(z2, -FIX(1.467221301)) +
             MULTIPLY(z3, FIX(1.001388905)) -
             MULTIPLY(z4, FIX(1.684843907));
    wsptr[8 * 0]  = (int)RIGHT_SHIFT(tmp20 + tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 10] = (int)RIGHT_SHIFT(tmp20 - tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 1]  = (int)RIGHT_SHIFT(tmp21 + tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 9]  = (int)RIGHT_SHIFT(tmp21 - tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 2]  = (int)RIGHT_SHIFT(tmp22 + tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 8]  = (int)RIGHT_SHIFT(tmp22 - tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 3]  = (int)RIGHT_SHIFT(tmp23 + tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 7]  = (int)RIGHT_SHIFT(tmp23 - tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 4]  = (int)RIGHT_SHIFT(tmp24 + tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 6]  = (int)RIGHT_SHIFT(tmp24 - tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 5]  = (int)RIGHT_SHIFT(tmp25, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 11; ctr++) {
    outptr = output_buf[ctr] + output_col;
    tmp10 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    tmp10 = LEFT_SHIFT(tmp10, CONST_BITS);
    z1 = (JLong)wsptr[2];
    z2 = (JLong)wsptr[4];
    z3 = (JLong)wsptr[6];
    tmp20 = MULTIPLY(z2 - z3, FIX(2.546640132));
    tmp23 = MULTIPLY(z2 - z1, FIX(0.430815045));
    z4 = z1 + z3;
    tmp24 = MULTIPLY(z4, -FIX(1.155664402));
    z4 -= z2;
    tmp25 = tmp10 + MULTIPLY(z4, FIX(1.356927976));
    tmp21 = tmp20 + tmp23 + tmp25 -
            MULTIPLY(z2, FIX(1.821790775));
    tmp20 += tmp25 + MULTIPLY(z3, FIX(2.115825087));
    tmp23 += tmp25 - MULTIPLY(z1, FIX(1.513598477));
    tmp24 += tmp25;
    tmp22 = tmp24 - MULTIPLY(z3, FIX(0.788749120));
    tmp24 += MULTIPLY(z2, FIX(1.944413522)) -
             MULTIPLY(z1, FIX(1.390975730));
    tmp25 = tmp10 - MULTIPLY(z4, FIX(1.414213562));
    z1 = (JLong)wsptr[1];
    z2 = (JLong)wsptr[3];
    z3 = (JLong)wsptr[5];
    z4 = (JLong)wsptr[7];
    tmp11 = z1 + z2;
    tmp14 = MULTIPLY(tmp11 + z3 + z4, FIX(0.398430003));
    tmp11 = MULTIPLY(tmp11, FIX(0.887983902));
    tmp12 = MULTIPLY(z1 + z3, FIX(0.670361295));
    tmp13 = tmp14 + MULTIPLY(z1 + z4, FIX(0.366151574));
    tmp10 = tmp11 + tmp12 + tmp13 -
            MULTIPLY(z1, FIX(0.923107866));
    z1    = tmp14 - MULTIPLY(z2 + z3, FIX(1.163011579));
    tmp11 += z1 + MULTIPLY(z2, FIX(2.073276588));
    tmp12 += z1 - MULTIPLY(z3, FIX(1.192193623));
    z1    = MULTIPLY(z2 + z4, -FIX(1.798248910));
    tmp11 += z1;
    tmp13 += z1 + MULTIPLY(z4, FIX(2.102458632));
    tmp14 += MULTIPLY(z2, -FIX(1.467221301)) +
             MULTIPLY(z3, FIX(1.001388905)) -
             MULTIPLY(z4, FIX(1.684843907));
    outptr[0]  = range_limit[(int)RIGHT_SHIFT(tmp20 + tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[10] = range_limit[(int)RIGHT_SHIFT(tmp20 - tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[1]  = range_limit[(int)RIGHT_SHIFT(tmp21 + tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[9]  = range_limit[(int)RIGHT_SHIFT(tmp21 - tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[2]  = range_limit[(int)RIGHT_SHIFT(tmp22 + tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[8]  = range_limit[(int)RIGHT_SHIFT(tmp22 - tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[3]  = range_limit[(int)RIGHT_SHIFT(tmp23 + tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[7]  = range_limit[(int)RIGHT_SHIFT(tmp23 - tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[4]  = range_limit[(int)RIGHT_SHIFT(tmp24 + tmp14,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[6]  = range_limit[(int)RIGHT_SHIFT(tmp24 - tmp14,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[5]  = range_limit[(int)RIGHT_SHIFT(tmp25,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    wsptr += 8;
  }
}

template <class T>
void idct_12x12(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp10, tmp11, tmp12, tmp13, tmp14, tmp15;
  JLong tmp20, tmp21, tmp22, tmp23, tmp24, tmp25;
  JLong z1, z2, z3, z4;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[8 * 12];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 8; ctr++, inptr++, quantptr++, wsptr++) {
    z3 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    z3 = LEFT_SHIFT(z3, CONST_BITS);
    z3 += ONE << (CONST_BITS - PASS1_BITS - 1);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    z4 = MULTIPLY(z4, FIX(1.224744871));
    tmp10 = z3 + z4;
    tmp11 = z3 - z4;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z4 = MULTIPLY(z1, FIX(1.366025404));
    z1 = LEFT_SHIFT(z1, CONST_BITS);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    z2 = LEFT_SHIFT(z2, CONST_BITS);
    tmp12 = z1 - z2;
    tmp21 = z3 + tmp12;
    tmp24 = z3 - tmp12;
    tmp12 = z4 + z2;
    tmp20 = tmp10 + tmp12;
    tmp25 = tmp10 - tmp12;
    tmp12 = z4 - z1 - z2;
    tmp22 = tmp11 + tmp12;
    tmp23 = tmp11 - tmp12;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    tmp11 = MULTIPLY(z2, FIX(1.306562965));
    tmp14 = MULTIPLY(z2, -FIX_0_541196100);
    tmp10 = z1 + z3;
    tmp15 = MULTIPLY(tmp10 + z4, FIX(0.860918669));
    tmp12 = tmp15 + MULTIPLY(tmp10, FIX(0.261052384));
    tmp10 = tmp12 + tmp11 + MULTIPLY(z1, FIX(0.280143716));
    tmp13 = MULTIPLY(z3 + z4, -FIX(1.045510580));
    tmp12 += tmp13 + tmp14 - MULTIPLY(z3, FIX(1.478575242));
    tmp13 += tmp15 - tmp11 + MULTIPLY(z4, FIX(1.586706681));
    tmp15 += tmp14 - MULTIPLY(z1, FIX(0.676326758)) -
             MULTIPLY(z4, FIX(1.982889723));
    z1 -= z4;
    z2 -= z3;
    z3 = MULTIPLY(z1 + z2, FIX_0_541196100);
    tmp11 = z3 + MULTIPLY(z1, FIX_0_765366865);
    tmp14 = z3 - MULTIPLY(z2, FIX_1_847759065);
    wsptr[8 * 0]  = (int)RIGHT_SHIFT(tmp20 + tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 11] = (int)RIGHT_SHIFT(tmp20 - tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 1]  = (int)RIGHT_SHIFT(tmp21 + tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 10] = (int)RIGHT_SHIFT(tmp21 - tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 2]  = (int)RIGHT_SHIFT(tmp22 + tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 9]  = (int)RIGHT_SHIFT(tmp22 - tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 3]  = (int)RIGHT_SHIFT(tmp23 + tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 8]  = (int)RIGHT_SHIFT(tmp23 - tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 4]  = (int)RIGHT_SHIFT(tmp24 + tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 7]  = (int)RIGHT_SHIFT(tmp24 - tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 5]  = (int)RIGHT_SHIFT(tmp25 + tmp15, CONST_BITS - PASS1_BITS);
    wsptr[8 * 6]  = (int)RIGHT_SHIFT(tmp25 - tmp15, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 12; ctr++) {
    outptr = output_buf[ctr] + output_col;
    z3 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    z3 = LEFT_SHIFT(z3, CONST_BITS);
    z4 = (JLong)wsptr[4];
    z4 = MULTIPLY(z4, FIX(1.224744871));
    tmp10 = z3 + z4;
    tmp11 = z3 - z4;
    z1 = (JLong)wsptr[2];
    z4 = MULTIPLY(z1, FIX(1.366025404));
    z1 = LEFT_SHIFT(z1, CONST_BITS);
    z2 = (JLong)wsptr[6];
    z2 = LEFT_SHIFT(z2, CONST_BITS);
    tmp12 = z1 - z2;
    tmp21 = z3 + tmp12;
    tmp24 = z3 - tmp12;
    tmp12 = z4 + z2;
    tmp20 = tmp10 + tmp12;
    tmp25 = tmp10 - tmp12;
    tmp12 = z4 - z1 - z2;
    tmp22 = tmp11 + tmp12;
    tmp23 = tmp11 - tmp12;
    z1 = (JLong)wsptr[1];
    z2 = (JLong)wsptr[3];
    z3 = (JLong)wsptr[5];
    z4 = (JLong)wsptr[7];
    tmp11 = MULTIPLY(z2, FIX(1.306562965));
    tmp14 = MULTIPLY(z2, -FIX_0_541196100);
    tmp10 = z1 + z3;
    tmp15 = MULTIPLY(tmp10 + z4, FIX(0.860918669));
    tmp12 = tmp15 + MULTIPLY(tmp10, FIX(0.261052384));
    tmp10 = tmp12 + tmp11 + MULTIPLY(z1, FIX(0.280143716));
    tmp13 = MULTIPLY(z3 + z4, -FIX(1.045510580));
    tmp12 += tmp13 + tmp14 - MULTIPLY(z3, FIX(1.478575242));
    tmp13 += tmp15 - tmp11 + MULTIPLY(z4, FIX(1.586706681));
    tmp15 += tmp14 - MULTIPLY(z1, FIX(0.676326758)) -
             MULTIPLY(z4, FIX(1.982889723));
    z1 -= z4;
    z2 -= z3;
    z3 = MULTIPLY(z1 + z2, FIX_0_541196100);
    tmp11 = z3 + MULTIPLY(z1, FIX_0_765366865);
    tmp14 = z3 - MULTIPLY(z2, FIX_1_847759065);
    outptr[0]  = range_limit[(int)RIGHT_SHIFT(tmp20 + tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[11] = range_limit[(int)RIGHT_SHIFT(tmp20 - tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[1]  = range_limit[(int)RIGHT_SHIFT(tmp21 + tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[10] = range_limit[(int)RIGHT_SHIFT(tmp21 - tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[2]  = range_limit[(int)RIGHT_SHIFT(tmp22 + tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[9]  = range_limit[(int)RIGHT_SHIFT(tmp22 - tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[3]  = range_limit[(int)RIGHT_SHIFT(tmp23 + tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[8]  = range_limit[(int)RIGHT_SHIFT(tmp23 - tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[4]  = range_limit[(int)RIGHT_SHIFT(tmp24 + tmp14,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[7]  = range_limit[(int)RIGHT_SHIFT(tmp24 - tmp14,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[5]  = range_limit[(int)RIGHT_SHIFT(tmp25 + tmp15,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[6]  = range_limit[(int)RIGHT_SHIFT(tmp25 - tmp15,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    wsptr += 8;
  }
}

template <class T>
void idct_13x13(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp10, tmp11, tmp12, tmp13, tmp14, tmp15;
  JLong tmp20, tmp21, tmp22, tmp23, tmp24, tmp25, tmp26;
  JLong z1, z2, z3, z4;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[8 * 13];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 8; ctr++, inptr++, quantptr++, wsptr++) {
    z1 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    z1 = LEFT_SHIFT(z1, CONST_BITS);
    z1 += ONE << (CONST_BITS - PASS1_BITS - 1);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    tmp10 = z3 + z4;
    tmp11 = z3 - z4;
    tmp12 = MULTIPLY(tmp10, FIX(1.155388986));
    tmp13 = MULTIPLY(tmp11, FIX(0.096834934)) + z1;
    tmp20 = MULTIPLY(z2, FIX(1.373119086)) + tmp12 + tmp13;
    tmp22 = MULTIPLY(z2, FIX(0.501487041)) - tmp12 + tmp13;
    tmp12 = MULTIPLY(tmp10, FIX(0.316450131));
    tmp13 = MULTIPLY(tmp11, FIX(0.486914739)) + z1;
    tmp21 = MULTIPLY(z2, FIX(1.058554052)) - tmp12 + tmp13;
    tmp25 = MULTIPLY(z2, -FIX(1.252223920)) + tmp12 + tmp13;
    tmp12 = MULTIPLY(tmp10, FIX(0.435816023));
    tmp13 = MULTIPLY(tmp11, FIX(0.937303064)) - z1;
    tmp23 = MULTIPLY(z2, -FIX(0.170464608)) - tmp12 - tmp13;
    tmp24 = MULTIPLY(z2, -FIX(0.803364869)) + tmp12 - tmp13;
    tmp26 = MULTIPLY(tmp11 - z2, FIX(1.414213562)) + z1;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    tmp11 = MULTIPLY(z1 + z2, FIX(1.322312651));
    tmp12 = MULTIPLY(z1 + z3, FIX(1.163874945));
    tmp15 = z1 + z4;
    tmp13 = MULTIPLY(tmp15, FIX(0.937797057));
    tmp10 = tmp11 + tmp12 + tmp13 -
            MULTIPLY(z1, FIX(2.020082300));
    tmp14 = MULTIPLY(z2 + z3, -FIX(0.338443458));
    tmp11 += tmp14 + MULTIPLY(z2, FIX(0.837223564));
    tmp12 += tmp14 - MULTIPLY(z3, FIX(1.572116027));
    tmp14 = MULTIPLY(z2 + z4, -FIX(1.163874945));
    tmp11 += tmp14;
    tmp13 += tmp14 + MULTIPLY(z4, FIX(2.205608352));
    tmp14 = MULTIPLY(z3 + z4, -FIX(0.657217813));
    tmp12 += tmp14;
    tmp13 += tmp14;
    tmp15 = MULTIPLY(tmp15, FIX(0.338443458));
    tmp14 = tmp15 + MULTIPLY(z1, FIX(0.318774355)) -
            MULTIPLY(z2, FIX(0.466105296));
    z1    = MULTIPLY(z3 - z2, FIX(0.937797057));
    tmp14 += z1;
    tmp15 += z1 + MULTIPLY(z3, FIX(0.384515595)) -
             MULTIPLY(z4, FIX(1.742345811));
    wsptr[8 * 0]  = (int)RIGHT_SHIFT(tmp20 + tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 12] = (int)RIGHT_SHIFT(tmp20 - tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 1]  = (int)RIGHT_SHIFT(tmp21 + tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 11] = (int)RIGHT_SHIFT(tmp21 - tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 2]  = (int)RIGHT_SHIFT(tmp22 + tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 10] = (int)RIGHT_SHIFT(tmp22 - tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 3]  = (int)RIGHT_SHIFT(tmp23 + tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 9]  = (int)RIGHT_SHIFT(tmp23 - tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 4]  = (int)RIGHT_SHIFT(tmp24 + tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 8]  = (int)RIGHT_SHIFT(tmp24 - tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 5]  = (int)RIGHT_SHIFT(tmp25 + tmp15, CONST_BITS - PASS1_BITS);
    wsptr[8 * 7]  = (int)RIGHT_SHIFT(tmp25 - tmp15, CONST_BITS - PASS1_BITS);
    wsptr[8 * 6]  = (int)RIGHT_SHIFT(tmp26, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 13; ctr++) {
    outptr = output_buf[ctr] + output_col;
    z1 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    z1 = LEFT_SHIFT(z1, CONST_BITS);
    z2 = (JLong)wsptr[2];
    z3 = (JLong)wsptr[4];
    z4 = (JLong)wsptr[6];
    tmp10 = z3 + z4;
    tmp11 = z3 - z4;
    tmp12 = MULTIPLY(tmp10, FIX(1.155388986));
    tmp13 = MULTIPLY(tmp11, FIX(0.096834934)) + z1;
    tmp20 = MULTIPLY(z2, FIX(1.373119086)) + tmp12 + tmp13;
    tmp22 = MULTIPLY(z2, FIX(0.501487041)) - tmp12 + tmp13;
    tmp12 = MULTIPLY(tmp10, FIX(0.316450131));
    tmp13 = MULTIPLY(tmp11, FIX(0.486914739)) + z1;
    tmp21 = MULTIPLY(z2, FIX(1.058554052)) - tmp12 + tmp13;
    tmp25 = MULTIPLY(z2, -FIX(1.252223920)) + tmp12 + tmp13;
    tmp12 = MULTIPLY(tmp10, FIX(0.435816023));
    tmp13 = MULTIPLY(tmp11, FIX(0.937303064)) - z1;
    tmp23 = MULTIPLY(z2, -FIX(0.170464608)) - tmp12 - tmp13;
    tmp24 = MULTIPLY(z2, -FIX(0.803364869)) + tmp12 - tmp13;
    tmp26 = MULTIPLY(tmp11 - z2, FIX(1.414213562)) + z1;
    z1 = (JLong)wsptr[1];
    z2 = (JLong)wsptr[3];
    z3 = (JLong)wsptr[5];
    z4 = (JLong)wsptr[7];
    tmp11 = MULTIPLY(z1 + z2, FIX(1.322312651));
    tmp12 = MULTIPLY(z1 + z3, FIX(1.163874945));
    tmp15 = z1 + z4;
    tmp13 = MULTIPLY(tmp15, FIX(0.937797057));
    tmp10 = tmp11 + tmp12 + tmp13 -
            MULTIPLY(z1, FIX(2.020082300));
    tmp14 = MULTIPLY(z2 + z3, -FIX(0.338443458));
    tmp11 += tmp14 + MULTIPLY(z2, FIX(0.837223564));
    tmp12 += tmp14 - MULTIPLY(z3, FIX(1.572116027));
    tmp14 = MULTIPLY(z2 + z4, -FIX(1.163874945));
    tmp11 += tmp14;
    tmp13 += tmp14 + MULTIPLY(z4, FIX(2.205608352));
    tmp14 = MULTIPLY(z3 + z4, -FIX(0.657217813));
    tmp12 += tmp14;
    tmp13 += tmp14;
    tmp15 = MULTIPLY(tmp15, FIX(0.338443458));
    tmp14 = tmp15 + MULTIPLY(z1, FIX(0.318774355)) -
            MULTIPLY(z2, FIX(0.466105296));
    z1    = MULTIPLY(z3 - z2, FIX(0.937797057));
    tmp14 += z1;
    tmp15 += z1 + MULTIPLY(z3, FIX(0.384515595)) -
             MULTIPLY(z4, FIX(1.742345811));
    outptr[0]  = range_limit[(int)RIGHT_SHIFT(tmp20 + tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[12] = range_limit[(int)RIGHT_SHIFT(tmp20 - tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[1]  = range_limit[(int)RIGHT_SHIFT(tmp21 + tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[11] = range_limit[(int)RIGHT_SHIFT(tmp21 - tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[2]  = range_limit[(int)RIGHT_SHIFT(tmp22 + tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[10] = range_limit[(int)RIGHT_SHIFT(tmp22 - tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[3]  = range_limit[(int)RIGHT_SHIFT(tmp23 + tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[9]  = range_limit[(int)RIGHT_SHIFT(tmp23 - tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[4]  = range_limit[(int)RIGHT_SHIFT(tmp24 + tmp14,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[8]  = range_limit[(int)RIGHT_SHIFT(tmp24 - tmp14,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[5]  = range_limit[(int)RIGHT_SHIFT(tmp25 + tmp15,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[7]  = range_limit[(int)RIGHT_SHIFT(tmp25 - tmp15,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[6]  = range_limit[(int)RIGHT_SHIFT(tmp26,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    wsptr += 8;
  }
}

template <class T>
void idct_14x14(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, tmp16;
  JLong tmp20, tmp21, tmp22, tmp23, tmp24, tmp25, tmp26;
  JLong z1, z2, z3, z4;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[8 * 14];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 8; ctr++, inptr++, quantptr++, wsptr++) {
    z1 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    z1 = LEFT_SHIFT(z1, CONST_BITS);
    z1 += ONE << (CONST_BITS - PASS1_BITS - 1);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    z2 = MULTIPLY(z4, FIX(1.274162392));
    z3 = MULTIPLY(z4, FIX(0.314692123));
    z4 = MULTIPLY(z4, FIX(0.881747734));
    tmp10 = z1 + z2;
    tmp11 = z1 + z3;
    tmp12 = z1 - z4;
    tmp23 = RIGHT_SHIFT(z1 - LEFT_SHIFT(z2 + z3 - z4, 1),
                        CONST_BITS - PASS1_BITS);
    z1 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    z3 = MULTIPLY(z1 + z2, FIX(1.105676686));
    tmp13 = z3 + MULTIPLY(z1, FIX(0.273079590));
    tmp14 = z3 - MULTIPLY(z2, FIX(1.719280954));
    tmp15 = MULTIPLY(z1, FIX(0.613604268)) -
            MULTIPLY(z2, FIX(1.378756276));
    tmp20 = tmp10 + tmp13;
    tmp26 = tmp10 - tmp13;
    tmp21 = tmp11 + tmp14;
    tmp25 = tmp11 - tmp14;
    tmp22 = tmp12 + tmp15;
    tmp24 = tmp12 - tmp15;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    tmp13 = LEFT_SHIFT(z4, CONST_BITS);
    tmp14 = z1 + z3;
    tmp11 = MULTIPLY(z1 + z2, FIX(1.334852607));
    tmp12 = MULTIPLY(tmp14, FIX(1.197448846));
    tmp10 = tmp11 + tmp12 + tmp13 - MULTIPLY(z1, FIX(1.126980169));
    tmp14 = MULTIPLY(tmp14, FIX(0.752406978));
    tmp16 = tmp14 - MULTIPLY(z1, FIX(1.061150426));
    z1    -= z2;
    tmp15 = MULTIPLY(z1, FIX(0.467085129)) - tmp13;
    tmp16 += tmp15;
    z1    += z4;
    z4    = MULTIPLY(z2 + z3, -FIX(0.158341681)) - tmp13;
    tmp11 += z4 - MULTIPLY(z2, FIX(0.424103948));
    tmp12 += z4 - MULTIPLY(z3, FIX(2.373959773));
    z4    = MULTIPLY(z3 - z2, FIX(1.405321284));
    tmp14 += z4 + tmp13 - MULTIPLY(z3, FIX(1.6906431334));
    tmp15 += z4 + MULTIPLY(z2, FIX(0.674957567));
    tmp13 = LEFT_SHIFT(z1 - z3, PASS1_BITS);
    wsptr[8 * 0]  = (int)RIGHT_SHIFT(tmp20 + tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 13] = (int)RIGHT_SHIFT(tmp20 - tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 1]  = (int)RIGHT_SHIFT(tmp21 + tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 12] = (int)RIGHT_SHIFT(tmp21 - tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 2]  = (int)RIGHT_SHIFT(tmp22 + tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 11] = (int)RIGHT_SHIFT(tmp22 - tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 3]  = (int)(tmp23 + tmp13);
    wsptr[8 * 10] = (int)(tmp23 - tmp13);
    wsptr[8 * 4]  = (int)RIGHT_SHIFT(tmp24 + tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 9]  = (int)RIGHT_SHIFT(tmp24 - tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 5]  = (int)RIGHT_SHIFT(tmp25 + tmp15, CONST_BITS - PASS1_BITS);
    wsptr[8 * 8]  = (int)RIGHT_SHIFT(tmp25 - tmp15, CONST_BITS - PASS1_BITS);
    wsptr[8 * 6]  = (int)RIGHT_SHIFT(tmp26 + tmp16, CONST_BITS - PASS1_BITS);
    wsptr[8 * 7]  = (int)RIGHT_SHIFT(tmp26 - tmp16, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 14; ctr++) {
    outptr = output_buf[ctr] + output_col;
    z1 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    z1 = LEFT_SHIFT(z1, CONST_BITS);
    z4 = (JLong)wsptr[4];
    z2 = MULTIPLY(z4, FIX(1.274162392));
    z3 = MULTIPLY(z4, FIX(0.314692123));
    z4 = MULTIPLY(z4, FIX(0.881747734));
    tmp10 = z1 + z2;
    tmp11 = z1 + z3;
    tmp12 = z1 - z4;
    tmp23 = z1 - LEFT_SHIFT(z2 + z3 - z4, 1);
    z1 = (JLong)wsptr[2];
    z2 = (JLong)wsptr[6];
    z3 = MULTIPLY(z1 + z2, FIX(1.105676686));
    tmp13 = z3 + MULTIPLY(z1, FIX(0.273079590));
    tmp14 = z3 - MULTIPLY(z2, FIX(1.719280954));
    tmp15 = MULTIPLY(z1, FIX(0.613604268)) -
            MULTIPLY(z2, FIX(1.378756276));
    tmp20 = tmp10 + tmp13;
    tmp26 = tmp10 - tmp13;
    tmp21 = tmp11 + tmp14;
    tmp25 = tmp11 - tmp14;
    tmp22 = tmp12 + tmp15;
    tmp24 = tmp12 - tmp15;
    z1 = (JLong)wsptr[1];
    z2 = (JLong)wsptr[3];
    z3 = (JLong)wsptr[5];
    z4 = (JLong)wsptr[7];
    z4 = LEFT_SHIFT(z4, CONST_BITS);
    tmp14 = z1 + z3;
    tmp11 = MULTIPLY(z1 + z2, FIX(1.334852607));
    tmp12 = MULTIPLY(tmp14, FIX(1.197448846));
    tmp10 = tmp11 + tmp12 + z4 - MULTIPLY(z1, FIX(1.126980169));
    tmp14 = MULTIPLY(tmp14, FIX(0.752406978));
    tmp16 = tmp14 - MULTIPLY(z1, FIX(1.061150426));
    z1    -= z2;
    tmp15 = MULTIPLY(z1, FIX(0.467085129)) - z4;
    tmp16 += tmp15;
    tmp13 = MULTIPLY(z2 + z3, -FIX(0.158341681)) - z4;
    tmp11 += tmp13 - MULTIPLY(z2, FIX(0.424103948));
    tmp12 += tmp13 - MULTIPLY(z3, FIX(2.373959773));
    tmp13 = MULTIPLY(z3 - z2, FIX(1.405321284));
    tmp14 += tmp13 + z4 - MULTIPLY(z3, FIX(1.6906431334));
    tmp15 += tmp13 + MULTIPLY(z2, FIX(0.674957567));
    tmp13 = LEFT_SHIFT(z1 - z3, CONST_BITS) + z4;
    outptr[0]  = range_limit[(int)RIGHT_SHIFT(tmp20 + tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[13] = range_limit[(int)RIGHT_SHIFT(tmp20 - tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[1]  = range_limit[(int)RIGHT_SHIFT(tmp21 + tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[12] = range_limit[(int)RIGHT_SHIFT(tmp21 - tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[2]  = range_limit[(int)RIGHT_SHIFT(tmp22 + tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[11] = range_limit[(int)RIGHT_SHIFT(tmp22 - tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[3]  = range_limit[(int)RIGHT_SHIFT(tmp23 + tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[10] = range_limit[(int)RIGHT_SHIFT(tmp23 - tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[4]  = range_limit[(int)RIGHT_SHIFT(tmp24 + tmp14,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[9]  = range_limit[(int)RIGHT_SHIFT(tmp24 - tmp14,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[5]  = range_limit[(int)RIGHT_SHIFT(tmp25 + tmp15,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[8]  = range_limit[(int)RIGHT_SHIFT(tmp25 - tmp15,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[6]  = range_limit[(int)RIGHT_SHIFT(tmp26 + tmp16,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[7]  = range_limit[(int)RIGHT_SHIFT(tmp26 - tmp16,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    wsptr += 8;
  }
}

template <class T>
void idct_15x15(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, tmp16;
  JLong tmp20, tmp21, tmp22, tmp23, tmp24, tmp25, tmp26, tmp27;
  JLong z1, z2, z3, z4;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[8 * 15];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 8; ctr++, inptr++, quantptr++, wsptr++) {
    z1 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    z1 = LEFT_SHIFT(z1, CONST_BITS);
    z1 += ONE << (CONST_BITS - PASS1_BITS - 1);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    tmp10 = MULTIPLY(z4, FIX(0.437016024));
    tmp11 = MULTIPLY(z4, FIX(1.144122806));
    tmp12 = z1 - tmp10;
    tmp13 = z1 + tmp11;
    z1 -= LEFT_SHIFT(tmp11 - tmp10, 1);
    z4 = z2 - z3;
    z3 += z2;
    tmp10 = MULTIPLY(z3, FIX(1.337628990));
    tmp11 = MULTIPLY(z4, FIX(0.045680613));
    z2 = MULTIPLY(z2, FIX(1.439773946));
    tmp20 = tmp13 + tmp10 + tmp11;
    tmp23 = tmp12 - tmp10 + tmp11 + z2;
    tmp10 = MULTIPLY(z3, FIX(0.547059574));
    tmp11 = MULTIPLY(z4, FIX(0.399234004));
    tmp25 = tmp13 - tmp10 - tmp11;
    tmp26 = tmp12 + tmp10 - tmp11 - z2;
    tmp10 = MULTIPLY(z3, FIX(0.790569415));
    tmp11 = MULTIPLY(z4, FIX(0.353553391));
    tmp21 = tmp12 + tmp10 + tmp11;
    tmp24 = tmp13 - tmp10 + tmp11;
    tmp11 += tmp11;
    tmp22 = z1 + tmp11;
    tmp27 = z1 - tmp11 - tmp11;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    z3 = MULTIPLY(z4, FIX(1.224744871));
    z4 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    tmp13 = z2 - z4;
    tmp15 = MULTIPLY(z1 + tmp13, FIX(0.831253876));
    tmp11 = tmp15 + MULTIPLY(z1, FIX(0.513743148));
    tmp14 = tmp15 - MULTIPLY(tmp13, FIX(2.176250899));
    tmp13 = MULTIPLY(z2, -FIX(0.831253876));
    tmp15 = MULTIPLY(z2, -FIX(1.344997024));
    z2 = z1 - z4;
    tmp12 = z3 + MULTIPLY(z2, FIX(1.406466353));
    tmp10 = tmp12 + MULTIPLY(z4, FIX(2.457431844)) - tmp15;
    tmp16 = tmp12 - MULTIPLY(z1, FIX(1.112434820)) + tmp13;
    tmp12 = MULTIPLY(z2, FIX(1.224744871)) - z3;
    z2 = MULTIPLY(z1 + z4, FIX(0.575212477));
    tmp13 += z2 + MULTIPLY(z1, FIX(0.475753014)) - z3;
    tmp15 += z2 - MULTIPLY(z4, FIX(0.869244010)) + z3;
    wsptr[8 * 0]  = (int)RIGHT_SHIFT(tmp20 + tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 14] = (int)RIGHT_SHIFT(tmp20 - tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 1]  = (int)RIGHT_SHIFT(tmp21 + tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 13] = (int)RIGHT_SHIFT(tmp21 - tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 2]  = (int)RIGHT_SHIFT(tmp22 + tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 12] = (int)RIGHT_SHIFT(tmp22 - tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 3]  = (int)RIGHT_SHIFT(tmp23 + tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 11] = (int)RIGHT_SHIFT(tmp23 - tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 4]  = (int)RIGHT_SHIFT(tmp24 + tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 10] = (int)RIGHT_SHIFT(tmp24 - tmp14, CONST_BITS - PASS1_BITS);
    wsptr[8 * 5]  = (int)RIGHT_SHIFT(tmp25 + tmp15, CONST_BITS - PASS1_BITS);
    wsptr[8 * 9]  = (int)RIGHT_SHIFT(tmp25 - tmp15, CONST_BITS - PASS1_BITS);
    wsptr[8 * 6]  = (int)RIGHT_SHIFT(tmp26 + tmp16, CONST_BITS - PASS1_BITS);
    wsptr[8 * 8]  = (int)RIGHT_SHIFT(tmp26 - tmp16, CONST_BITS - PASS1_BITS);
    wsptr[8 * 7]  = (int)RIGHT_SHIFT(tmp27, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 15; ctr++) {
    outptr = output_buf[ctr] + output_col;
    z1 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    z1 = LEFT_SHIFT(z1, CONST_BITS);
    z2 = (JLong)wsptr[2];
    z3 = (JLong)wsptr[4];
    z4 = (JLong)wsptr[6];
    tmp10 = MULTIPLY(z4, FIX(0.437016024));
    tmp11 = MULTIPLY(z4, FIX(1.144122806));
    tmp12 = z1 - tmp10;
    tmp13 = z1 + tmp11;
    z1 -= LEFT_SHIFT(tmp11 - tmp10, 1);
    z4 = z2 - z3;
    z3 += z2;
    tmp10 = MULTIPLY(z3, FIX(1.337628990));
    tmp11 = MULTIPLY(z4, FIX(0.045680613));
    z2 = MULTIPLY(z2, FIX(1.439773946));
    tmp20 = tmp13 + tmp10 + tmp11;
    tmp23 = tmp12 - tmp10 + tmp11 + z2;
    tmp10 = MULTIPLY(z3, FIX(0.547059574));
    tmp11 = MULTIPLY(z4, FIX(0.399234004));
    tmp25 = tmp13 - tmp10 - tmp11;
    tmp26 = tmp12 + tmp10 - tmp11 - z2;
    tmp10 = MULTIPLY(z3, FIX(0.790569415));
    tmp11 = MULTIPLY(z4, FIX(0.353553391));
    tmp21 = tmp12 + tmp10 + tmp11;
    tmp24 = tmp13 - tmp10 + tmp11;
    tmp11 += tmp11;
    tmp22 = z1 + tmp11;
    tmp27 = z1 - tmp11 - tmp11;
    z1 = (JLong)wsptr[1];
    z2 = (JLong)wsptr[3];
    z4 = (JLong)wsptr[5];
    z3 = MULTIPLY(z4, FIX(1.224744871));
    z4 = (JLong)wsptr[7];
    tmp13 = z2 - z4;
    tmp15 = MULTIPLY(z1 + tmp13, FIX(0.831253876));
    tmp11 = tmp15 + MULTIPLY(z1, FIX(0.513743148));
    tmp14 = tmp15 - MULTIPLY(tmp13, FIX(2.176250899));
    tmp13 = MULTIPLY(z2, -FIX(0.831253876));
    tmp15 = MULTIPLY(z2, -FIX(1.344997024));
    z2 = z1 - z4;
    tmp12 = z3 + MULTIPLY(z2, FIX(1.406466353));
    tmp10 = tmp12 + MULTIPLY(z4, FIX(2.457431844)) - tmp15;
    tmp16 = tmp12 - MULTIPLY(z1, FIX(1.112434820)) + tmp13;
    tmp12 = MULTIPLY(z2, FIX(1.224744871)) - z3;
    z2 = MULTIPLY(z1 + z4, FIX(0.575212477));
    tmp13 += z2 + MULTIPLY(z1, FIX(0.475753014)) - z3;
    tmp15 += z2 - MULTIPLY(z4, FIX(0.869244010)) + z3;
    outptr[0]  = range_limit[(int)RIGHT_SHIFT(tmp20 + tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[14] = range_limit[(int)RIGHT_SHIFT(tmp20 - tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[1]  = range_limit[(int)RIGHT_SHIFT(tmp21 + tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[13] = range_limit[(int)RIGHT_SHIFT(tmp21 - tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[2]  = range_limit[(int)RIGHT_SHIFT(tmp22 + tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[12] = range_limit[(int)RIGHT_SHIFT(tmp22 - tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[3]  = range_limit[(int)RIGHT_SHIFT(tmp23 + tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[11] = range_limit[(int)RIGHT_SHIFT(tmp23 - tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[4]  = range_limit[(int)RIGHT_SHIFT(tmp24 + tmp14,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[10] = range_limit[(int)RIGHT_SHIFT(tmp24 - tmp14,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[5]  = range_limit[(int)RIGHT_SHIFT(tmp25 + tmp15,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[9]  = range_limit[(int)RIGHT_SHIFT(tmp25 - tmp15,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[6]  = range_limit[(int)RIGHT_SHIFT(tmp26 + tmp16,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[8]  = range_limit[(int)RIGHT_SHIFT(tmp26 - tmp16,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[7]  = range_limit[(int)RIGHT_SHIFT(tmp27,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    wsptr += 8;
  }
}

template <class T>
void idct_16x16(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp0, tmp1, tmp2, tmp3, tmp10, tmp11, tmp12, tmp13;
  JLong tmp20, tmp21, tmp22, tmp23, tmp24, tmp25, tmp26, tmp27;
  JLong z1, z2, z3, z4;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[8 * 16];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = 0; ctr < 8; ctr++, inptr++, quantptr++, wsptr++) {
    tmp0 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    tmp0 = LEFT_SHIFT(tmp0, CONST_BITS);
    tmp0 += ONE << (CONST_BITS - PASS1_BITS - 1);
    z1 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    tmp1 = MULTIPLY(z1, FIX(1.306562965));
    tmp2 = MULTIPLY(z1, FIX_0_541196100);
    tmp10 = tmp0 + tmp1;
    tmp11 = tmp0 - tmp1;
    tmp12 = tmp0 + tmp2;
    tmp13 = tmp0 - tmp2;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    z3 = z1 - z2;
    z4 = MULTIPLY(z3, FIX(0.275899379));
    z3 = MULTIPLY(z3, FIX(1.387039845));
    tmp0 = z3 + MULTIPLY(z2, FIX_2_562915447);
    tmp1 = z4 + MULTIPLY(z1, FIX_0_899976223);
    tmp2 = z3 - MULTIPLY(z1, FIX(0.601344887));
    tmp3 = z4 - MULTIPLY(z2, FIX(0.509795579));
    tmp20 = tmp10 + tmp0;
    tmp27 = tmp10 - tmp0;
    tmp21 = tmp12 + tmp1;
    tmp26 = tmp12 - tmp1;
    tmp22 = tmp13 + tmp2;
    tmp25 = tmp13 - tmp2;
    tmp23 = tmp11 + tmp3;
    tmp24 = tmp11 - tmp3;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    tmp11 = z1 + z3;
    tmp1  = MULTIPLY(z1 + z2, FIX(1.353318001));
    tmp2  = MULTIPLY(tmp11,   FIX(1.247225013));
    tmp3  = MULTIPLY(z1 + z4, FIX(1.093201867));
    tmp10 = MULTIPLY(z1 - z4, FIX(0.897167586));
    tmp11 = MULTIPLY(tmp11,   FIX(0.666655658));
    tmp12 = MULTIPLY(z1 - z2, FIX(0.410524528));
    tmp0  = tmp1 + tmp2 + tmp3 -
            MULTIPLY(z1, FIX(2.286341144));
    tmp13 = tmp10 + tmp11 + tmp12 -
            MULTIPLY(z1, FIX(1.835730603));
    z1    = MULTIPLY(z2 + z3, FIX(0.138617169));
    tmp1  += z1 + MULTIPLY(z2, FIX(0.071888074));
    tmp2  += z1 - MULTIPLY(z3, FIX(1.125726048));
    z1    = MULTIPLY(z3 - z2, FIX(1.407403738));
    tmp11 += z1 - MULTIPLY(z3, FIX(0.766367282));
    tmp12 += z1 + MULTIPLY(z2, FIX(1.971951411));
    z2    += z4;
    z1    = MULTIPLY(z2, -FIX(0.666655658));
    tmp1  += z1;
    tmp3  += z1 + MULTIPLY(z4, FIX(1.065388962));
    z2    = MULTIPLY(z2, -FIX(1.247225013));
    tmp10 += z2 + MULTIPLY(z4, FIX(3.141271809));
    tmp12 += z2;
    z2    = MULTIPLY(z3 + z4, -FIX(1.353318001));
    tmp2  += z2;
    tmp3  += z2;
    z2    = MULTIPLY(z4 - z3, FIX(0.410524528));
    tmp10 += z2;
    tmp11 += z2;
    wsptr[8 * 0]  = (int)RIGHT_SHIFT(tmp20 + tmp0,  CONST_BITS - PASS1_BITS);
    wsptr[8 * 15] = (int)RIGHT_SHIFT(tmp20 - tmp0,  CONST_BITS - PASS1_BITS);
    wsptr[8 * 1]  = (int)RIGHT_SHIFT(tmp21 + tmp1,  CONST_BITS - PASS1_BITS);
    wsptr[8 * 14] = (int)RIGHT_SHIFT(tmp21 - tmp1,  CONST_BITS - PASS1_BITS);
    wsptr[8 * 2]  = (int)RIGHT_SHIFT(tmp22 + tmp2,  CONST_BITS - PASS1_BITS);
    wsptr[8 * 13] = (int)RIGHT_SHIFT(tmp22 - tmp2,  CONST_BITS - PASS1_BITS);
    wsptr[8 * 3]  = (int)RIGHT_SHIFT(tmp23 + tmp3,  CONST_BITS - PASS1_BITS);
    wsptr[8 * 12] = (int)RIGHT_SHIFT(tmp23 - tmp3,  CONST_BITS - PASS1_BITS);
    wsptr[8 * 4]  = (int)RIGHT_SHIFT(tmp24 + tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 11] = (int)RIGHT_SHIFT(tmp24 - tmp10, CONST_BITS - PASS1_BITS);
    wsptr[8 * 5]  = (int)RIGHT_SHIFT(tmp25 + tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 10] = (int)RIGHT_SHIFT(tmp25 - tmp11, CONST_BITS - PASS1_BITS);
    wsptr[8 * 6]  = (int)RIGHT_SHIFT(tmp26 + tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 9]  = (int)RIGHT_SHIFT(tmp26 - tmp12, CONST_BITS - PASS1_BITS);
    wsptr[8 * 7]  = (int)RIGHT_SHIFT(tmp27 + tmp13, CONST_BITS - PASS1_BITS);
    wsptr[8 * 8]  = (int)RIGHT_SHIFT(tmp27 - tmp13, CONST_BITS - PASS1_BITS);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 16; ctr++) {
    outptr = output_buf[ctr] + output_col;
    tmp0 = (JLong)wsptr[0] + (ONE << (PASS1_BITS + 2));
    tmp0 = LEFT_SHIFT(tmp0, CONST_BITS);
    z1 = (JLong)wsptr[4];
    tmp1 = MULTIPLY(z1, FIX(1.306562965));
    tmp2 = MULTIPLY(z1, FIX_0_541196100);
    tmp10 = tmp0 + tmp1;
    tmp11 = tmp0 - tmp1;
    tmp12 = tmp0 + tmp2;
    tmp13 = tmp0 - tmp2;
    z1 = (JLong)wsptr[2];
    z2 = (JLong)wsptr[6];
    z3 = z1 - z2;
    z4 = MULTIPLY(z3, FIX(0.275899379));
    z3 = MULTIPLY(z3, FIX(1.387039845));
    tmp0 = z3 + MULTIPLY(z2, FIX_2_562915447);
    tmp1 = z4 + MULTIPLY(z1, FIX_0_899976223);
    tmp2 = z3 - MULTIPLY(z1, FIX(0.601344887));
    tmp3 = z4 - MULTIPLY(z2, FIX(0.509795579));
    tmp20 = tmp10 + tmp0;
    tmp27 = tmp10 - tmp0;
    tmp21 = tmp12 + tmp1;
    tmp26 = tmp12 - tmp1;
    tmp22 = tmp13 + tmp2;
    tmp25 = tmp13 - tmp2;
    tmp23 = tmp11 + tmp3;
    tmp24 = tmp11 - tmp3;
    z1 = (JLong)wsptr[1];
    z2 = (JLong)wsptr[3];
    z3 = (JLong)wsptr[5];
    z4 = (JLong)wsptr[7];
    tmp11 = z1 + z3;
    tmp1  = MULTIPLY(z1 + z2, FIX(1.353318001));
    tmp2  = MULTIPLY(tmp11,   FIX(1.247225013));
    tmp3  = MULTIPLY(z1 + z4, FIX(1.093201867));
    tmp10 = MULTIPLY(z1 - z4, FIX(0.897167586));
    tmp11 = MULTIPLY(tmp11,   FIX(0.666655658));
    tmp12 = MULTIPLY(z1 - z2, FIX(0.410524528));
    tmp0  = tmp1 + tmp2 + tmp3 -
            MULTIPLY(z1, FIX(2.286341144));
    tmp13 = tmp10 + tmp11 + tmp12 -
            MULTIPLY(z1, FIX(1.835730603));
    z1    = MULTIPLY(z2 + z3, FIX(0.138617169));
    tmp1  += z1 + MULTIPLY(z2, FIX(0.071888074));
    tmp2  += z1 - MULTIPLY(z3, FIX(1.125726048));
    z1    = MULTIPLY(z3 - z2, FIX(1.407403738));
    tmp11 += z1 - MULTIPLY(z3, FIX(0.766367282));
    tmp12 += z1 + MULTIPLY(z2, FIX(1.971951411));
    z2    += z4;
    z1    = MULTIPLY(z2, -FIX(0.666655658));
    tmp1  += z1;
    tmp3  += z1 + MULTIPLY(z4, FIX(1.065388962));
    z2    = MULTIPLY(z2, -FIX(1.247225013));
    tmp10 += z2 + MULTIPLY(z4, FIX(3.141271809));
    tmp12 += z2;
    z2    = MULTIPLY(z3 + z4, -FIX(1.353318001));
    tmp2  += z2;
    tmp3  += z2;
    z2    = MULTIPLY(z4 - z3, FIX(0.410524528));
    tmp10 += z2;
    tmp11 += z2;
    outptr[0]  = range_limit[(int)RIGHT_SHIFT(tmp20 + tmp0,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[15] = range_limit[(int)RIGHT_SHIFT(tmp20 - tmp0,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[1]  = range_limit[(int)RIGHT_SHIFT(tmp21 + tmp1,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[14] = range_limit[(int)RIGHT_SHIFT(tmp21 - tmp1,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[2]  = range_limit[(int)RIGHT_SHIFT(tmp22 + tmp2,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[13] = range_limit[(int)RIGHT_SHIFT(tmp22 - tmp2,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[3]  = range_limit[(int)RIGHT_SHIFT(tmp23 + tmp3,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[12] = range_limit[(int)RIGHT_SHIFT(tmp23 - tmp3,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[4]  = range_limit[(int)RIGHT_SHIFT(tmp24 + tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[11] = range_limit[(int)RIGHT_SHIFT(tmp24 - tmp10,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[5]  = range_limit[(int)RIGHT_SHIFT(tmp25 + tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[10] = range_limit[(int)RIGHT_SHIFT(tmp25 - tmp11,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[6]  = range_limit[(int)RIGHT_SHIFT(tmp26 + tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[9]  = range_limit[(int)RIGHT_SHIFT(tmp26 - tmp12,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[7]  = range_limit[(int)RIGHT_SHIFT(tmp27 + tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    outptr[8]  = range_limit[(int)RIGHT_SHIFT(tmp27 - tmp13,
                                              CONST_BITS + PASS1_BITS + 3) &
                             RANGE_MASK];
    wsptr += 8;
  }
}

}  // namespace idct_int

namespace idct_red {
inline constexpr JLong FIX_0_211164243 = 1730, FIX_0_509795579 = 4176, FIX_0_601344887 = 4926,
    FIX_0_720959822 = 5906, FIX_0_765366865 = 6270, FIX_0_850430095 = 6967,
    FIX_0_899976223 = 7373, FIX_1_061594337 = 8697, FIX_1_272758580 = 10426,
    FIX_1_451774981 = 11893, FIX_1_847759065 = 15137, FIX_2_172734803 = 17799,
    FIX_2_562915447 = 20995, FIX_3_624509785 = 29692;

template <class T>
void idct_4x4(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp0, tmp2, tmp10, tmp12;
  JLong z1, z2, z3, z4;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[DCTSIZE * 4];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = DCTSIZE; ctr > 0; inptr++, quantptr++, wsptr++, ctr--) {
    if (ctr == DCTSIZE - 4)
      continue;
    if (inptr[DCTSIZE * 1] == 0 && inptr[DCTSIZE * 2] == 0 &&
        inptr[DCTSIZE * 3] == 0 && inptr[DCTSIZE * 5] == 0 &&
        inptr[DCTSIZE * 6] == 0 && inptr[DCTSIZE * 7] == 0) {
      int dcval = LEFT_SHIFT(DEQUANTIZE(inptr[DCTSIZE * 0],
                                        quantptr[DCTSIZE * 0]), PASS1_BITS);
      wsptr[DCTSIZE * 0] = dcval;
      wsptr[DCTSIZE * 1] = dcval;
      wsptr[DCTSIZE * 2] = dcval;
      wsptr[DCTSIZE * 3] = dcval;
      continue;
    }
    tmp0 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    tmp0 = LEFT_SHIFT(tmp0, CONST_BITS + 1);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    tmp2 = MULTIPLY(z2, FIX_1_847759065) + MULTIPLY(z3, -FIX_0_765366865);
    tmp10 = tmp0 + tmp2;
    tmp12 = tmp0 - tmp2;
    z1 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    z2 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    z3 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    z4 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    tmp0 = MULTIPLY(z1, -FIX_0_211164243) +
           MULTIPLY(z2,  FIX_1_451774981) +
           MULTIPLY(z3, -FIX_2_172734803) +
           MULTIPLY(z4,  FIX_1_061594337);
    tmp2 = MULTIPLY(z1, -FIX_0_509795579) +
           MULTIPLY(z2, -FIX_0_601344887) +
           MULTIPLY(z3,  FIX_0_899976223) +
           MULTIPLY(z4,  FIX_2_562915447);
    wsptr[DCTSIZE * 0] =
      (int)DESCALE(tmp10 + tmp2, CONST_BITS - PASS1_BITS + 1);
    wsptr[DCTSIZE * 3] =
      (int)DESCALE(tmp10 - tmp2, CONST_BITS - PASS1_BITS + 1);
    wsptr[DCTSIZE * 1] =
      (int)DESCALE(tmp12 + tmp0, CONST_BITS - PASS1_BITS + 1);
    wsptr[DCTSIZE * 2] =
      (int)DESCALE(tmp12 - tmp0, CONST_BITS - PASS1_BITS + 1);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 4; ctr++) {
    outptr = output_buf[ctr] + output_col;
#ifndef NO_ZERO_ROW_TEST
    if (wsptr[1] == 0 && wsptr[2] == 0 && wsptr[3] == 0 &&
        wsptr[5] == 0 && wsptr[6] == 0 && wsptr[7] == 0) {
      T dcval = range_limit[(int)DESCALE((JLong)wsptr[0],
                                                PASS1_BITS + 3) & RANGE_MASK];
      outptr[0] = dcval;
      outptr[1] = dcval;
      outptr[2] = dcval;
      outptr[3] = dcval;
      wsptr += DCTSIZE;
      continue;
    }
#endif
    tmp0 = LEFT_SHIFT((JLong)wsptr[0], CONST_BITS + 1);
    tmp2 = MULTIPLY((JLong)wsptr[2],  FIX_1_847759065) +
           MULTIPLY((JLong)wsptr[6], -FIX_0_765366865);
    tmp10 = tmp0 + tmp2;
    tmp12 = tmp0 - tmp2;
    z1 = (JLong)wsptr[7];
    z2 = (JLong)wsptr[5];
    z3 = (JLong)wsptr[3];
    z4 = (JLong)wsptr[1];
    tmp0 = MULTIPLY(z1, -FIX_0_211164243) +
           MULTIPLY(z2,  FIX_1_451774981) +
           MULTIPLY(z3, -FIX_2_172734803) +
           MULTIPLY(z4,  FIX_1_061594337);
    tmp2 = MULTIPLY(z1, -FIX_0_509795579) +
           MULTIPLY(z2, -FIX_0_601344887) +
           MULTIPLY(z3, FIX_0_899976223) +
           MULTIPLY(z4, FIX_2_562915447);
    outptr[0] = range_limit[(int)DESCALE(tmp10 + tmp2,
                                         CONST_BITS + PASS1_BITS + 3 + 1) &
                            RANGE_MASK];
    outptr[3] = range_limit[(int)DESCALE(tmp10 - tmp2,
                                         CONST_BITS + PASS1_BITS + 3 + 1) &
                            RANGE_MASK];
    outptr[1] = range_limit[(int)DESCALE(tmp12 + tmp0,
                                         CONST_BITS + PASS1_BITS + 3 + 1) &
                            RANGE_MASK];
    outptr[2] = range_limit[(int)DESCALE(tmp12 - tmp0,
                                         CONST_BITS + PASS1_BITS + 3 + 1) &
                            RANGE_MASK];
    wsptr += DCTSIZE;
  }
}

template <class T>
void idct_2x2(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  JLong tmp0, tmp10, z1;
  const Coef *inptr;
  const std::int32_t *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[DCTSIZE * 2];
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  inptr = coef_block;
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  wsptr = workspace;
  for (ctr = DCTSIZE; ctr > 0; inptr++, quantptr++, wsptr++, ctr--) {
    if (ctr == DCTSIZE - 2 || ctr == DCTSIZE - 4 || ctr == DCTSIZE - 6)
      continue;
    if (inptr[DCTSIZE * 1] == 0 && inptr[DCTSIZE * 3] == 0 &&
        inptr[DCTSIZE * 5] == 0 && inptr[DCTSIZE * 7] == 0) {
      int dcval = LEFT_SHIFT(DEQUANTIZE(inptr[DCTSIZE * 0],
                             quantptr[DCTSIZE * 0]), PASS1_BITS);
      wsptr[DCTSIZE * 0] = dcval;
      wsptr[DCTSIZE * 1] = dcval;
      continue;
    }
    z1 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    tmp10 = LEFT_SHIFT(z1, CONST_BITS + 2);
    z1 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    tmp0 = MULTIPLY(z1, -FIX_0_720959822);
    z1 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    tmp0 += MULTIPLY(z1, FIX_0_850430095);
    z1 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    tmp0 += MULTIPLY(z1, -FIX_1_272758580);
    z1 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    tmp0 += MULTIPLY(z1, FIX_3_624509785);
    wsptr[DCTSIZE * 0] =
      (int)DESCALE(tmp10 + tmp0, CONST_BITS - PASS1_BITS + 2);
    wsptr[DCTSIZE * 1] =
      (int)DESCALE(tmp10 - tmp0, CONST_BITS - PASS1_BITS + 2);
  }
  wsptr = workspace;
  for (ctr = 0; ctr < 2; ctr++) {
    outptr = output_buf[ctr] + output_col;
#ifndef NO_ZERO_ROW_TEST
    if (wsptr[1] == 0 && wsptr[3] == 0 && wsptr[5] == 0 && wsptr[7] == 0) {
      T dcval = range_limit[(int)DESCALE((JLong)wsptr[0],
                                                PASS1_BITS + 3) & RANGE_MASK];
      outptr[0] = dcval;
      outptr[1] = dcval;
      wsptr += DCTSIZE;
      continue;
    }
#endif
    tmp10 = LEFT_SHIFT((JLong)wsptr[0], CONST_BITS + 2);
    tmp0 = MULTIPLY((JLong)wsptr[7], -FIX_0_720959822) +
           MULTIPLY((JLong)wsptr[5],  FIX_0_850430095) +
           MULTIPLY((JLong)wsptr[3], -FIX_1_272758580) +
           MULTIPLY((JLong)wsptr[1],  FIX_3_624509785);
    outptr[0] = range_limit[(int)DESCALE(tmp10 + tmp0,
                                         CONST_BITS + PASS1_BITS + 3 + 2) &
                            RANGE_MASK];
    outptr[1] = range_limit[(int)DESCALE(tmp10 - tmp0,
                                         CONST_BITS + PASS1_BITS + 3 + 2) &
                            RANGE_MASK];
    wsptr += DCTSIZE;
  }
}

template <class T>
void idct_1x1(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  int dcval;
  const std::int32_t *quantptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  [[maybe_unused]] const JLong scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<JLong>(scaling_factor_);  // 16 only for 12-bit output
  quantptr = static_cast<const std::int32_t *>(dct_table_);
  dcval = DEQUANTIZE(coef_block[0], quantptr[0]);
  dcval = (int)DESCALE((JLong)dcval, 3);
  output_buf[0][output_col] = range_limit[dcval & RANGE_MASK];
}

}  // namespace idct_red
#undef CONST_BITS
#undef CONST_SCALE
#undef FIX
#undef DESCALE
#undef MULTIPLY
#undef DEQUANTIZE

namespace idct_fst {
#define CONST_BITS 8
#define DESCALE(x, n) RIGHT_SHIFT(x, n)
#define MULTIPLY(var, const) ((IdctElem<T>)DESCALE((var) * (const), CONST_BITS))
#define DEQUANTIZE(coef, quantval) \
  (kBits<T> == 8 ? (IdctElem<T>)(((IfastMult<T>)(coef)) * (quantval)) \
                 : (IdctElem<T>)DESCALE((JLong)(coef) * (quantval) * scaling_factor, \
                                        kIfastScaleBits<T> - PASS1_BITS))
#define IRIGHT_SHIFT(x, shft) ((x) >> (shft))
#define IDESCALE(x, n) ((int)IRIGHT_SHIFT(x, n))
inline constexpr JLong FIX_1_082392200 = 277, FIX_1_414213562 = 362, FIX_1_847759065 = 473,
    FIX_2_613125930 = 669;

template <class T>
void idct_ifast(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  IdctElem<T> tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  IdctElem<T> tmp10, tmp11, tmp12, tmp13;
  IdctElem<T> z5, z10, z11, z12, z13;
  const Coef *inptr;
  const IfastMult<T> *quantptr;
  int *wsptr;
  T * outptr;
  const T *range_limit = (sample_range_limit + kCenterSample<T>);
  int ctr;
  int workspace[DCTSIZE2];
  [[maybe_unused]] const IdctElem<T> scaling_factor =
      kBits<T> == 8 ? 1 : static_cast<IdctElem<T>>(scaling_factor_);
  inptr = coef_block;
  quantptr = static_cast<const IfastMult<T> *>(dct_table_);
  wsptr = workspace;
  for (ctr = DCTSIZE; ctr > 0; ctr--) {
    if (inptr[DCTSIZE * 1] == 0 && inptr[DCTSIZE * 2] == 0 &&
        inptr[DCTSIZE * 3] == 0 && inptr[DCTSIZE * 4] == 0 &&
        inptr[DCTSIZE * 5] == 0 && inptr[DCTSIZE * 6] == 0 &&
        inptr[DCTSIZE * 7] == 0) {
      int dcval = (int)DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
      wsptr[DCTSIZE * 0] = dcval;
      wsptr[DCTSIZE * 1] = dcval;
      wsptr[DCTSIZE * 2] = dcval;
      wsptr[DCTSIZE * 3] = dcval;
      wsptr[DCTSIZE * 4] = dcval;
      wsptr[DCTSIZE * 5] = dcval;
      wsptr[DCTSIZE * 6] = dcval;
      wsptr[DCTSIZE * 7] = dcval;
      inptr++;
      quantptr++;
      wsptr++;
      continue;
    }
    tmp0 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0]);
    tmp1 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2]);
    tmp2 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4]);
    tmp3 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6]);
    tmp10 = tmp0 + tmp2;
    tmp11 = tmp0 - tmp2;
    tmp13 = tmp1 + tmp3;
    tmp12 = MULTIPLY(tmp1 - tmp3, FIX_1_414213562) - tmp13;
    tmp0 = tmp10 + tmp13;
    tmp3 = tmp10 - tmp13;
    tmp1 = tmp11 + tmp12;
    tmp2 = tmp11 - tmp12;
    tmp4 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1]);
    tmp5 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3]);
    tmp6 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5]);
    tmp7 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7]);
    z13 = tmp6 + tmp5;
    z10 = tmp6 - tmp5;
    z11 = tmp4 + tmp7;
    z12 = tmp4 - tmp7;
    tmp7 = z11 + z13;
    tmp11 = MULTIPLY(z11 - z13, FIX_1_414213562);
    z5 = MULTIPLY(z10 + z12, FIX_1_847759065);
    tmp10 = MULTIPLY(z12, FIX_1_082392200) - z5;
    tmp12 = MULTIPLY(z10, -FIX_2_613125930) + z5;
    tmp6 = tmp12 - tmp7;
    tmp5 = tmp11 - tmp6;
    tmp4 = tmp10 + tmp5;
    wsptr[DCTSIZE * 0] = (int)(tmp0 + tmp7);
    wsptr[DCTSIZE * 7] = (int)(tmp0 - tmp7);
    wsptr[DCTSIZE * 1] = (int)(tmp1 + tmp6);
    wsptr[DCTSIZE * 6] = (int)(tmp1 - tmp6);
    wsptr[DCTSIZE * 2] = (int)(tmp2 + tmp5);
    wsptr[DCTSIZE * 5] = (int)(tmp2 - tmp5);
    wsptr[DCTSIZE * 4] = (int)(tmp3 + tmp4);
    wsptr[DCTSIZE * 3] = (int)(tmp3 - tmp4);
    inptr++;
    quantptr++;
    wsptr++;
  }
  wsptr = workspace;
  for (ctr = 0; ctr < DCTSIZE; ctr++) {
    outptr = output_buf[ctr] + output_col;
#ifndef NO_ZERO_ROW_TEST
    if (wsptr[1] == 0 && wsptr[2] == 0 && wsptr[3] == 0 && wsptr[4] == 0 &&
        wsptr[5] == 0 && wsptr[6] == 0 && wsptr[7] == 0) {
      T dcval =
        range_limit[IDESCALE(wsptr[0], PASS1_BITS + 3) & RANGE_MASK];
      outptr[0] = dcval;
      outptr[1] = dcval;
      outptr[2] = dcval;
      outptr[3] = dcval;
      outptr[4] = dcval;
      outptr[5] = dcval;
      outptr[6] = dcval;
      outptr[7] = dcval;
      wsptr += DCTSIZE;
      continue;
    }
#endif
    tmp10 = ((IdctElem<T>)wsptr[0] + (IdctElem<T>)wsptr[4]);
    tmp11 = ((IdctElem<T>)wsptr[0] - (IdctElem<T>)wsptr[4]);
    tmp13 = ((IdctElem<T>)wsptr[2] + (IdctElem<T>)wsptr[6]);
    tmp12 =
      MULTIPLY((IdctElem<T>)wsptr[2] - (IdctElem<T>)wsptr[6], FIX_1_414213562) - tmp13;
    tmp0 = tmp10 + tmp13;
    tmp3 = tmp10 - tmp13;
    tmp1 = tmp11 + tmp12;
    tmp2 = tmp11 - tmp12;
    z13 = (IdctElem<T>)wsptr[5] + (IdctElem<T>)wsptr[3];
    z10 = (IdctElem<T>)wsptr[5] - (IdctElem<T>)wsptr[3];
    z11 = (IdctElem<T>)wsptr[1] + (IdctElem<T>)wsptr[7];
    z12 = (IdctElem<T>)wsptr[1] - (IdctElem<T>)wsptr[7];
    tmp7 = z11 + z13;
    tmp11 = MULTIPLY(z11 - z13, FIX_1_414213562);
    z5 = MULTIPLY(z10 + z12, FIX_1_847759065);
    tmp10 = MULTIPLY(z12, FIX_1_082392200) - z5;
    tmp12 = MULTIPLY(z10, -FIX_2_613125930) + z5;
    tmp6 = tmp12 - tmp7;
    tmp5 = tmp11 - tmp6;
    tmp4 = tmp10 + tmp5;
    outptr[0] =
      range_limit[IDESCALE(tmp0 + tmp7, PASS1_BITS + 3) & RANGE_MASK];
    outptr[7] =
      range_limit[IDESCALE(tmp0 - tmp7, PASS1_BITS + 3) & RANGE_MASK];
    outptr[1] =
      range_limit[IDESCALE(tmp1 + tmp6, PASS1_BITS + 3) & RANGE_MASK];
    outptr[6] =
      range_limit[IDESCALE(tmp1 - tmp6, PASS1_BITS + 3) & RANGE_MASK];
    outptr[2] =
      range_limit[IDESCALE(tmp2 + tmp5, PASS1_BITS + 3) & RANGE_MASK];
    outptr[5] =
      range_limit[IDESCALE(tmp2 - tmp5, PASS1_BITS + 3) & RANGE_MASK];
    outptr[4] =
      range_limit[IDESCALE(tmp3 + tmp4, PASS1_BITS + 3) & RANGE_MASK];
    outptr[3] =
      range_limit[IDESCALE(tmp3 - tmp4, PASS1_BITS + 3) & RANGE_MASK];
    wsptr += DCTSIZE;
  }
}

#undef CONST_BITS
#undef DESCALE
#undef MULTIPLY
#undef DEQUANTIZE
#undef IRIGHT_SHIFT
#undef IDESCALE
}  // namespace idct_fst

namespace idct_flt {
#define DEQUANTIZE(coef, quantval) (((float)(coef)) * (quantval) * scaling_factor)

template <class T>
VIO_NO_FP_CONTRACT void idct_float(const void *dct_table_, const Coef *coef_block, T **output_buf,
          unsigned output_col, const T *sample_range_limit, int scaling_factor_) {
  VIO_FP_CONTRACT_OFF
  float tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
  float tmp10, tmp11, tmp12, tmp13;
  float z5, z10, z11, z12, z13;
  const Coef *inptr;
  const float *quantptr;
  float *wsptr;
  T * outptr;
  const T *range_limit = sample_range_limit;
  int ctr;
  float workspace[DCTSIZE2];
  [[maybe_unused]] const float scaling_factor =
      kBits<T> == 8 ? 1.0f : static_cast<float>(scaling_factor_);
  inptr = coef_block;
  quantptr = static_cast<const float *>(dct_table_);
  wsptr = workspace;
  for (ctr = DCTSIZE; ctr > 0; ctr--) {
    if (inptr[DCTSIZE * 1] == 0 && inptr[DCTSIZE * 2] == 0 &&
        inptr[DCTSIZE * 3] == 0 && inptr[DCTSIZE * 4] == 0 &&
        inptr[DCTSIZE * 5] == 0 && inptr[DCTSIZE * 6] == 0 &&
        inptr[DCTSIZE * 7] == 0) {
      float dcval = DEQUANTIZE(inptr[DCTSIZE * 0],
                                    quantptr[DCTSIZE * 0] * 0.125f);
      wsptr[DCTSIZE * 0] = dcval;
      wsptr[DCTSIZE * 1] = dcval;
      wsptr[DCTSIZE * 2] = dcval;
      wsptr[DCTSIZE * 3] = dcval;
      wsptr[DCTSIZE * 4] = dcval;
      wsptr[DCTSIZE * 5] = dcval;
      wsptr[DCTSIZE * 6] = dcval;
      wsptr[DCTSIZE * 7] = dcval;
      inptr++;
      quantptr++;
      wsptr++;
      continue;
    }
    tmp0 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0] * 0.125f);
    tmp1 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2] * 0.125f);
    tmp2 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4] * 0.125f);
    tmp3 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6] * 0.125f);
    tmp10 = tmp0 + tmp2;
    tmp11 = tmp0 - tmp2;
    tmp13 = tmp1 + tmp3;
    tmp12 = std::fma(tmp1 - tmp3, (float)1.414213562, -tmp13);
    tmp0 = tmp10 + tmp13;
    tmp3 = tmp10 - tmp13;
    tmp1 = tmp11 + tmp12;
    tmp2 = tmp11 - tmp12;
    tmp4 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1] * 0.125f);
    tmp5 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3] * 0.125f);
    tmp6 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5] * 0.125f);
    tmp7 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7] * 0.125f);
    z13 = tmp6 + tmp5;
    z10 = tmp6 - tmp5;
    z11 = tmp4 + tmp7;
    z12 = tmp4 - tmp7;
    tmp7 = z11 + z13;
    tmp11 = (z11 - z13) * ((float)1.414213562);
    z5 = (z10 + z12) * ((float)1.847759065);
    tmp10 = std::fma(-z12, (float)1.082392200, z5);
    tmp12 = std::fma(-z10, (float)2.613125930, z5);
    tmp6 = tmp12 - tmp7;
    tmp5 = tmp11 - tmp6;
    tmp4 = tmp10 - tmp5;
    wsptr[DCTSIZE * 0] = tmp0 + tmp7;
    wsptr[DCTSIZE * 7] = tmp0 - tmp7;
    wsptr[DCTSIZE * 1] = tmp1 + tmp6;
    wsptr[DCTSIZE * 6] = tmp1 - tmp6;
    wsptr[DCTSIZE * 2] = tmp2 + tmp5;
    wsptr[DCTSIZE * 5] = tmp2 - tmp5;
    wsptr[DCTSIZE * 3] = tmp3 + tmp4;
    wsptr[DCTSIZE * 4] = tmp3 - tmp4;
    inptr++;
    quantptr++;
    wsptr++;
  }
  wsptr = workspace;
  for (ctr = 0; ctr < DCTSIZE; ctr++) {
    outptr = output_buf[ctr] + output_col;
    z5 = wsptr[0] + ((float)kCenterSample<T> + (float)0.5);
    tmp10 = z5 + wsptr[4];
    tmp11 = z5 - wsptr[4];
    tmp13 = wsptr[2] + wsptr[6];
    tmp12 = std::fma(wsptr[2] - wsptr[6], (float)1.414213562, -tmp13);
    tmp0 = tmp10 + tmp13;
    tmp3 = tmp10 - tmp13;
    tmp1 = tmp11 + tmp12;
    tmp2 = tmp11 - tmp12;
    z13 = wsptr[5] + wsptr[3];
    z10 = wsptr[5] - wsptr[3];
    z11 = wsptr[1] + wsptr[7];
    z12 = wsptr[1] - wsptr[7];
    tmp7 = z11 + z13;
    tmp11 = (z11 - z13) * ((float)1.414213562);
    z5 = (z10 + z12) * ((float)1.847759065);
    tmp10 = std::fma(-z12, (float)1.082392200, z5);
    tmp12 = std::fma(-z10, (float)2.613125930, z5);
    tmp6 = tmp12 - tmp7;
    tmp5 = tmp11 - tmp6;
    tmp4 = tmp10 - tmp5;
    outptr[0] = range_limit[((int)(tmp0 + tmp7)) & RANGE_MASK];
    outptr[7] = range_limit[((int)(tmp0 - tmp7)) & RANGE_MASK];
    outptr[1] = range_limit[((int)(tmp1 + tmp6)) & RANGE_MASK];
    outptr[6] = range_limit[((int)(tmp1 - tmp6)) & RANGE_MASK];
    outptr[2] = range_limit[((int)(tmp2 + tmp5)) & RANGE_MASK];
    outptr[5] = range_limit[((int)(tmp2 - tmp5)) & RANGE_MASK];
    outptr[3] = range_limit[((int)(tmp3 + tmp4)) & RANGE_MASK];
    outptr[4] = range_limit[((int)(tmp3 - tmp4)) & RANGE_MASK];
    wsptr += DCTSIZE;
  }
}

#undef DEQUANTIZE
}  // namespace idct_flt

#undef ONE
#undef RIGHT_SHIFT
#undef LEFT_SHIFT
#undef RANGE_MASK
#undef PASS1_BITS

}  // namespace vio::jpeg::detail
