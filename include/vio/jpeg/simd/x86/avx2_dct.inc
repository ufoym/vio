// vio::jpeg::simd::avx2 - AVX2 sample conversion, accurate integer FDCT/IDCT
// and quantization.  The 1-D transforms keep two of
// their eight vectors per 256-bit register ([a|b] = a in the low lane, b in
// the high lane), so each pmaddwd computes two outputs with per-lane
// constants.  Results are bit-identical to the SSE2/scalar code.

namespace detail_dct {

using X = __m128i;
using Y = __m256i;

constexpr int CONST_BITS = 13, PASS1_BITS = 2;
constexpr int F_0_298 = 2446, F_0_390 = 3196, F_0_541 = 4433, F_0_765 = 6270, F_0_899 = 7373,
              F_1_175 = 9633, F_1_501 = 12299, F_1_847 = 15137, F_1_961 = 16069, F_2_053 = 16819,
              F_2_562 = 20995, F_3_072 = 25172;

VIO_INLINE Y pair2(X lo, X hi) { return _mm256_inserti128_si256(_mm256_castsi128_si256(lo), hi, 1); }
VIO_INLINE X lo(Y v) { return _mm256_castsi256_si128(v); }
VIO_INLINE X hi(Y v) { return _mm256_extracti128_si256(v, 1); }
VIO_INLINE Y swap(Y v) { return _mm256_permute2x128_si256(v, v, 0x01); }
// [a.lo | b.lo]
VIO_INLINE Y lows(Y a, Y b) { return _mm256_permute2x128_si256(a, b, 0x20); }
// (a, b) constant pairs for the low lane and (c, d) for the high lane
VIO_INLINE Y k2(int a, int b, int c, int d) {
  auto p = [](int x, int y) {
    return static_cast<int>((static_cast<unsigned>(y) << 16) | (static_cast<unsigned>(x) & 0xFFFF));
  };
  return _mm256_setr_epi32(p(a, b), p(a, b), p(a, b), p(a, b), p(c, d), p(c, d), p(c, d), p(c, d));
}

struct D_ {
  Y lo, hi;
};
// Per lane: madd of the interleaved 16-bit pairs (x, y) with k.
VIO_INLINE D_ madd(Y x, Y y, Y k) {
  return {_mm256_madd_epi16(_mm256_unpacklo_epi16(x, y), k),
          _mm256_madd_epi16(_mm256_unpackhi_epi16(x, y), k)};
}
VIO_INLINE D_ add(D_ a, D_ b) { return {_mm256_add_epi32(a.lo, b.lo), _mm256_add_epi32(a.hi, b.hi)}; }
VIO_INLINE D_ sub(D_ a, D_ b) { return {_mm256_sub_epi32(a.lo, b.lo), _mm256_sub_epi32(a.hi, b.hi)}; }
VIO_INLINE D_ swap(D_ a) { return {swap(a.lo), swap(a.hi)}; }
template <int N>
VIO_INLINE D_ widen_shl(Y x) {
  const Y zero = _mm256_setzero_si256();
  return {_mm256_srai_epi32(_mm256_unpacklo_epi16(zero, x), 16 - N),
          _mm256_srai_epi32(_mm256_unpackhi_epi16(zero, x), 16 - N)};
}
template <int N>
VIO_INLINE Y descale(D_ x) {
  const Y r = _mm256_set1_epi32(1 << (N - 1));
  return _mm256_packs_epi32(_mm256_srai_epi32(_mm256_add_epi32(x.lo, r), N),
                            _mm256_srai_epi32(_mm256_add_epi32(x.hi, r), N));
}

// 8x8 16-bit transpose of 8 row vectors.
VIO_INLINE void transpose8(X* r) {
  X a0 = _mm_unpacklo_epi16(r[0], r[1]), a1 = _mm_unpackhi_epi16(r[0], r[1]);
  X a2 = _mm_unpacklo_epi16(r[2], r[3]), a3 = _mm_unpackhi_epi16(r[2], r[3]);
  X a4 = _mm_unpacklo_epi16(r[4], r[5]), a5 = _mm_unpackhi_epi16(r[4], r[5]);
  X a6 = _mm_unpacklo_epi16(r[6], r[7]), a7 = _mm_unpackhi_epi16(r[6], r[7]);
  X b0 = _mm_unpacklo_epi32(a0, a2), b1 = _mm_unpackhi_epi32(a0, a2);
  X b2 = _mm_unpacklo_epi32(a1, a3), b3 = _mm_unpackhi_epi32(a1, a3);
  X b4 = _mm_unpacklo_epi32(a4, a6), b5 = _mm_unpackhi_epi32(a4, a6);
  X b6 = _mm_unpacklo_epi32(a5, a7), b7 = _mm_unpackhi_epi32(a5, a7);
  r[0] = _mm_unpacklo_epi64(b0, b4);
  r[1] = _mm_unpackhi_epi64(b0, b4);
  r[2] = _mm_unpacklo_epi64(b1, b5);
  r[3] = _mm_unpackhi_epi64(b1, b5);
  r[4] = _mm_unpacklo_epi64(b2, b6);
  r[5] = _mm_unpackhi_epi64(b2, b6);
  r[6] = _mm_unpacklo_epi64(b3, b7);
  r[7] = _mm_unpackhi_epi64(b3, b7);
}

// Transposes an 8x8 block held as A = [r0|r4], B = [r1|r5], C = [r2|r6],
// D = [r3|r7] into the column pairs p = [c0|c1], q = [c7|c6], r = [c3|c2],
// s = [c4|c5] that fdct_islow_pass() takes: in-lane unpacks, then one
// 64-bit permute per vector that also puts each pair in the wanted order.
VIO_INLINE void transpose_pairs(Y A, Y B, Y C, Y D, Y& p, Y& q, Y& r, Y& s) {
  Y ab0 = _mm256_unpacklo_epi16(A, B), ab1 = _mm256_unpackhi_epi16(A, B);
  Y cd0 = _mm256_unpacklo_epi16(C, D), cd1 = _mm256_unpackhi_epi16(C, D);
  // 64-bit units: [cN rows 0-3, cN+1 rows 0-3 | cN rows 4-7, cN+1 rows 4-7]
  p = _mm256_permute4x64_epi64(_mm256_unpacklo_epi32(ab0, cd0), 0xD8);
  r = _mm256_permute4x64_epi64(_mm256_unpackhi_epi32(ab0, cd0), 0x8D);
  s = _mm256_permute4x64_epi64(_mm256_unpacklo_epi32(ab1, cd1), 0xD8);
  q = _mm256_permute4x64_epi64(_mm256_unpackhi_epi32(ab1, cd1), 0x8D);
}

// Accurate FDCT pass on the pairs p = [d0|d1], q = [d7|d6], r = [d3|d2],
// s = [d4|d5] (one 8-lane vector per sample index); returns the outputs as
// A = [o0|o4], B = [o1|o5], C = [o2|o6], D = [o3|o7].
template <bool kPass1>
VIO_INLINE void fdct_islow_pass(Y p, Y q, Y r, Y s, Y& A, Y& B, Y& C, Y& D) {
  constexpr int DS = kPass1 ? CONST_BITS - PASS1_BITS : CONST_BITS + PASS1_BITS;
  Y t01 = _mm256_add_epi16(p, q), t76 = _mm256_sub_epi16(p, q);  // [tmp0|tmp1], [tmp7|tmp6]
  Y t32 = _mm256_add_epi16(r, s), t45 = _mm256_sub_epi16(r, s);  // [tmp3|tmp2], [tmp4|tmp5]
  Y t1011 = _mm256_add_epi16(t01, t32), t1312 = _mm256_sub_epi16(t01, t32);
  // [out0|out4] = [tmp10 + tmp11 | tmp10 - tmp11]
  Y sw = swap(t1011);
  Y o04 = lows(_mm256_add_epi16(t1011, sw), _mm256_sub_epi16(t1011, sw));
  if constexpr (kPass1) {
    o04 = _mm256_slli_epi16(o04, PASS1_BITS);
  } else {
    o04 = _mm256_srai_epi16(_mm256_add_epi16(o04, _mm256_set1_epi16(1 << (PASS1_BITS - 1))), PASS1_BITS);
  }
  // [out2|out6] from (tmp13, tmp12) / (tmp12, tmp13) pairs
  Y o26 = descale<DS>(madd(t1312, swap(t1312),
                           k2(F_0_541 + F_0_765, F_0_541, F_0_541 - F_1_847, F_0_541)));
  // [z3|z4] = [tmp4 + tmp6 | tmp5 + tmp7]
  Y z34 = _mm256_add_epi16(t45, swap(t76));
  D_ z = madd(z34, swap(z34), k2(F_1_175 - F_1_961, F_1_175, F_1_175 - F_0_390, F_1_175));  // [z3m|z4m]
  D_ m75 = madd(t45, t76, k2(F_0_298 - F_0_899, -F_0_899, F_2_053 - F_2_562, -F_2_562));
  D_ m13 = madd(t45, t76, k2(-F_0_899, F_1_501 - F_0_899, -F_2_562, F_3_072 - F_2_562));
  Y o75 = descale<DS>(add(m75, z));         // [out7|out5]
  Y o13 = descale<DS>(add(m13, swap(z)));   // [out1|out3]
  A = o04;
  B = _mm256_blend_epi32(o13, o75, 0xF0);   // [out1|out5]
  C = o26;
  D = _mm256_permute2x128_si256(o13, o75, 0x21);  // [out3|out7]
}

// Accurate IDCT pass on dequantized in[0..7].
template <bool kPass1>
VIO_INLINE void idct_islow_pass(X* in) {
  constexpr int DS = kPass1 ? CONST_BITS - PASS1_BITS : CONST_BITS + PASS1_BITS + 3;
  Y e04 = pair2(in[0], in[4]), e26 = pair2(in[2], in[6]);
  // [tmp3|tmp2]
  D_ t32 = madd(e26, swap(e26), k2(F_0_541 + F_0_765, F_0_541, F_0_541 - F_1_847, F_0_541));
  Y s04 = swap(e04);
  D_ t01 = widen_shl<CONST_BITS>(lows(_mm256_add_epi16(e04, s04), _mm256_sub_epi16(e04, s04)));
  D_ t1011 = add(t01, t32), t1312 = sub(t01, t32);
  Y x75 = pair2(in[7], in[5]), y13 = pair2(in[1], in[3]);
  Y z34 = _mm256_add_epi16(swap(y13), x75);  // [in3 + in7 | in1 + in5] = [z3|z4]
  D_ z = madd(z34, swap(z34), k2(F_1_175 - F_1_961, F_1_175, F_1_175 - F_0_390, F_1_175));
  D_ o01 = add(madd(x75, y13, k2(F_0_298 - F_0_899, -F_0_899, F_2_053 - F_2_562, -F_2_562)), z);
  D_ o32 = add(madd(x75, y13, k2(-F_0_899, F_1_501 - F_0_899, -F_2_562, F_3_072 - F_2_562)), swap(z));
  Y r01 = descale<DS>(add(t1011, o32)), r76 = descale<DS>(sub(t1011, o32));
  Y r32 = descale<DS>(add(t1312, o01)), r45 = descale<DS>(sub(t1312, o01));
  in[0] = lo(r01); in[1] = hi(r01);
  in[7] = lo(r76); in[6] = hi(r76);
  in[3] = lo(r32); in[2] = hi(r32);
  in[4] = lo(r45); in[5] = hi(r45);
}

}  // namespace detail_dct

inline void convsamp(std::uint8_t** sample_data, unsigned start_col, std::int16_t* workspace) {
  const __m256i center = _mm256_set1_epi16(128);
  for (int r = 0; r < 8; r += 2) {
    __m128i a = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(sample_data[r] + start_col));
    __m128i b = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(sample_data[r + 1] + start_col));
    __m256i w = _mm256_sub_epi16(_mm256_cvtepu8_epi16(_mm_unpacklo_epi64(a, b)), center);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(workspace + r * 8), w);
  }
}

inline void quantize(std::int16_t* coef_block, const std::uint16_t* divisors,
                     const std::int16_t* workspace) {
  for (int i = 0; i < 64; i += 16) {
    __m256i x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(workspace + i));
    __m256i a = _mm256_abs_epi16(x);
    a = _mm256_add_epi16(a, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(divisors + 64 + i)));
    a = _mm256_mulhi_epu16(a, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(divisors + i)));
    a = _mm256_mulhi_epu16(a, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(divisors + 128 + i)));
    // a is 0 wherever x is, so sign() (which zeroes those lanes) is exact
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(coef_block + i), _mm256_sign_epi16(a, x));
  }
}

inline void fdct_islow(std::int16_t* data) {
  using namespace detail_dct;
  auto row = [&](int i) { return _mm_loadu_si128(reinterpret_cast<const X*>(data + i * 8)); };
  Y A = pair2(row(0), row(4)), B = pair2(row(1), row(5));
  Y C = pair2(row(2), row(6)), D = pair2(row(3), row(7));
  Y p, q, r, s;
  transpose_pairs(A, B, C, D, p, q, r, s);
  fdct_islow_pass<true>(p, q, r, s, A, B, C, D);
  transpose_pairs(A, B, C, D, p, q, r, s);
  fdct_islow_pass<false>(p, q, r, s, A, B, C, D);
  // Store whole row pairs: the quantizer reloads the block as 256-bit row
  // pairs, and a load spanning two narrower stores cannot be forwarded.
  auto st = [&](int i, Y v) { _mm256_storeu_si256(reinterpret_cast<Y*>(data + i * 8), v); };
  st(0, _mm256_permute2x128_si256(A, B, 0x20));  // [o0|o1]
  st(2, _mm256_permute2x128_si256(C, D, 0x20));  // [o2|o3]
  st(4, _mm256_permute2x128_si256(A, B, 0x31));  // [o4|o5]
  st(6, _mm256_permute2x128_si256(C, D, 0x31));  // [o6|o7]
}

inline void idct_islow(const std::int16_t* quantptr, const std::int16_t* coef_block,
                       std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_dct;
  X r[8];
  X ac = _mm_setzero_si128();
  for (int i = 0; i < 8; i++) {
    X c = _mm_loadu_si128(reinterpret_cast<const X*>(coef_block + i * 8));
    if (i) ac = _mm_or_si128(ac, c);
    r[i] = _mm_mullo_epi16(c, _mm_loadu_si128(reinterpret_cast<const X*>(quantptr + i * 8)));
  }
  if (_mm_movemask_epi8(_mm_cmpeq_epi8(ac, _mm_setzero_si128())) == 0xFFFF) {
    X dc = _mm_slli_epi16(r[0], PASS1_BITS);
    for (int i = 0; i < 8; i++) r[i] = dc;
  } else {
    idct_islow_pass<true>(r);
  }
  transpose8(r);
  idct_islow_pass<false>(r);
  transpose8(r);
  const X center = _mm_set1_epi8(static_cast<char>(-128));
  for (int i = 0; i < 8; i += 2) {
    X b = _mm_add_epi8(_mm_packs_epi16(r[i], r[i + 1]), center);
    _mm_storel_epi64(reinterpret_cast<X*>(output_buf[i] + output_col), b);
    _mm_storel_epi64(reinterpret_cast<X*>(output_buf[i + 1] + output_col), _mm_srli_si128(b, 8));
  }
}
