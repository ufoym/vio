// vio::jpeg::simd - floating-point DCT parts that need only SSE (shared by
// the SSE and SSE2 kernels).  The operation order is part of the result:
// do not reorder or fuse operations.

namespace detail_flt {

using F = __m128;

// In-place transpose of the 4x4 block held in r[0..3].
VIO_INLINE void transpose4(F& r0, F& r1, F& r2, F& r3) { _MM_TRANSPOSE4_PS(r0, r1, r2, r3); }

// AAN forward DCT on 8 vectors (one per sample index).
VIO_INLINE void fdct_pass(F* d) {
  const F k0382 = _mm_set1_ps(0.382683432365089771728460f);
  const F k0707 = _mm_set1_ps(0.707106781186547524400844f);
  const F k0541 = _mm_set1_ps(0.541196100146196984399723f);
  const F k1306 = _mm_set1_ps(1.306562964876376527856643f);
  F tmp0 = _mm_add_ps(d[0], d[7]), tmp7 = _mm_sub_ps(d[0], d[7]);
  F tmp1 = _mm_add_ps(d[1], d[6]), tmp6 = _mm_sub_ps(d[1], d[6]);
  F tmp2 = _mm_add_ps(d[2], d[5]), tmp5 = _mm_sub_ps(d[2], d[5]);
  F tmp3 = _mm_add_ps(d[3], d[4]), tmp4 = _mm_sub_ps(d[3], d[4]);
  F tmp10 = _mm_add_ps(tmp0, tmp3), tmp13 = _mm_sub_ps(tmp0, tmp3);
  F tmp11 = _mm_add_ps(tmp1, tmp2), tmp12 = _mm_sub_ps(tmp1, tmp2);
  d[0] = _mm_add_ps(tmp10, tmp11);
  d[4] = _mm_sub_ps(tmp10, tmp11);
  F z1 = _mm_mul_ps(_mm_add_ps(tmp12, tmp13), k0707);
  d[2] = _mm_add_ps(tmp13, z1);
  d[6] = _mm_sub_ps(tmp13, z1);
  tmp10 = _mm_add_ps(tmp4, tmp5);
  tmp11 = _mm_add_ps(tmp5, tmp6);
  tmp12 = _mm_add_ps(tmp6, tmp7);
  F z5 = _mm_mul_ps(_mm_sub_ps(tmp10, tmp12), k0382);
  F z2 = _mm_add_ps(_mm_mul_ps(tmp10, k0541), z5);
  F z4 = _mm_add_ps(_mm_mul_ps(tmp12, k1306), z5);
  F z3 = _mm_mul_ps(tmp11, k0707);
  F z11 = _mm_add_ps(tmp7, z3), z13 = _mm_sub_ps(tmp7, z3);
  d[5] = _mm_add_ps(z13, z2);
  d[3] = _mm_sub_ps(z13, z2);
  d[1] = _mm_add_ps(z11, z4);
  d[7] = _mm_sub_ps(z11, z4);
}

// AAN inverse DCT on 8 dequantized vectors (one per coefficient index).
VIO_INLINE void idct_pass(F* in) {
  const F k1414 = _mm_set1_ps(1.414213562373095048801689f);
  const F k1847 = _mm_set1_ps(1.847759065022573512256366f);
  const F k1082 = _mm_set1_ps(1.082392200292393968799446f);
  const F km2613 = _mm_set1_ps(-2.613125929752753055713286f);
  F tmp10 = _mm_add_ps(in[0], in[4]), tmp11 = _mm_sub_ps(in[0], in[4]);
  F tmp13 = _mm_add_ps(in[2], in[6]);
  F tmp12 = _mm_sub_ps(_mm_mul_ps(_mm_sub_ps(in[2], in[6]), k1414), tmp13);
  F tmp0 = _mm_add_ps(tmp10, tmp13), tmp3 = _mm_sub_ps(tmp10, tmp13);
  F tmp1 = _mm_add_ps(tmp11, tmp12), tmp2 = _mm_sub_ps(tmp11, tmp12);
  F z11 = _mm_add_ps(in[1], in[7]), z13 = _mm_add_ps(in[5], in[3]);
  F z12 = _mm_sub_ps(in[1], in[7]), z10 = _mm_sub_ps(in[5], in[3]);
  F tmp7 = _mm_add_ps(z11, z13);
  F t11 = _mm_mul_ps(_mm_sub_ps(z11, z13), k1414);
  F z5 = _mm_mul_ps(_mm_add_ps(z10, z12), k1847);
  F t12 = _mm_add_ps(_mm_mul_ps(z10, km2613), z5);
  F t10 = _mm_sub_ps(_mm_mul_ps(z12, k1082), z5);
  F tmp6 = _mm_sub_ps(t12, tmp7);
  F tmp5 = _mm_sub_ps(t11, tmp6);
  F tmp4 = _mm_add_ps(t10, tmp5);
  in[0] = _mm_add_ps(tmp0, tmp7);
  in[7] = _mm_sub_ps(tmp0, tmp7);
  in[1] = _mm_add_ps(tmp1, tmp6);
  in[6] = _mm_sub_ps(tmp1, tmp6);
  in[2] = _mm_add_ps(tmp2, tmp5);
  in[5] = _mm_sub_ps(tmp2, tmp5);
  in[4] = _mm_add_ps(tmp3, tmp4);
  in[3] = _mm_sub_ps(tmp3, tmp4);
}

// Transposed load/store of rows r..r+3 (8 floats each) as 8 column vectors.
VIO_INLINE void load_cols(const float* p, F* c) {
  F a0 = _mm_loadu_ps(p), a1 = _mm_loadu_ps(p + 8), a2 = _mm_loadu_ps(p + 16), a3 = _mm_loadu_ps(p + 24);
  F b0 = _mm_loadu_ps(p + 4), b1 = _mm_loadu_ps(p + 12), b2 = _mm_loadu_ps(p + 20), b3 = _mm_loadu_ps(p + 28);
  transpose4(a0, a1, a2, a3);
  transpose4(b0, b1, b2, b3);
  c[0] = a0; c[1] = a1; c[2] = a2; c[3] = a3;
  c[4] = b0; c[5] = b1; c[6] = b2; c[7] = b3;
}
VIO_INLINE void store_cols(float* p, F* c) {
  transpose4(c[0], c[1], c[2], c[3]);
  transpose4(c[4], c[5], c[6], c[7]);
  _mm_storeu_ps(p, c[0]); _mm_storeu_ps(p + 8, c[1]); _mm_storeu_ps(p + 16, c[2]); _mm_storeu_ps(p + 24, c[3]);
  _mm_storeu_ps(p + 4, c[4]); _mm_storeu_ps(p + 12, c[5]); _mm_storeu_ps(p + 20, c[6]); _mm_storeu_ps(p + 28, c[7]);
}

}  // namespace detail_flt

inline void fdct_float(float* data) {
  using namespace detail_flt;
  F c[8];
  for (int half = 0; half < 2; half++) {  // pass 1: rows (4 at a time)
    load_cols(data + half * 32, c);
    fdct_pass(c);
    store_cols(data + half * 32, c);
  }
  for (int half = 0; half < 2; half++) {  // pass 2: columns (4 at a time)
    for (int k = 0; k < 8; k++) c[k] = _mm_loadu_ps(data + k * 8 + half * 4);
    fdct_pass(c);
    for (int k = 0; k < 8; k++) _mm_storeu_ps(data + k * 8 + half * 4, c[k]);
  }
}
