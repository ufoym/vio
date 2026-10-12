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

// Accurate IDCT pass on the dequantized pairs e04 = [in0|in4],
// e26 = [in2|in6], x75 = [in7|in5], y13 = [in1|in3]; returns the outputs as
// A = [o0|o4], B = [o1|o5], C = [o2|o6], D = [o3|o7].
template <bool kPass1>
VIO_INLINE void idct_islow_pass(Y e04, Y e26, Y x75, Y y13, Y& A, Y& B, Y& C, Y& D) {
  constexpr int DS = kPass1 ? CONST_BITS - PASS1_BITS : CONST_BITS + PASS1_BITS + 3;
  // [tmp3|tmp2]
  D_ t32 = madd(e26, swap(e26), k2(F_0_541 + F_0_765, F_0_541, F_0_541 - F_1_847, F_0_541));
  Y s04 = swap(e04);
  D_ t01 = widen_shl<CONST_BITS>(lows(_mm256_add_epi16(e04, s04), _mm256_sub_epi16(e04, s04)));
  D_ t1011 = add(t01, t32), t1312 = sub(t01, t32);
  Y z34 = _mm256_add_epi16(swap(y13), x75);  // [in3 + in7 | in1 + in5] = [z3|z4]
  D_ z = madd(z34, swap(z34), k2(F_1_175 - F_1_961, F_1_175, F_1_175 - F_0_390, F_1_175));
  D_ o01 = add(madd(x75, y13, k2(F_0_298 - F_0_899, -F_0_899, F_2_053 - F_2_562, -F_2_562)), z);
  D_ o32 = add(madd(x75, y13, k2(-F_0_899, F_1_501 - F_0_899, -F_2_562, F_3_072 - F_2_562)), swap(z));
  Y r01 = descale<DS>(add(t1011, o32)), r76 = descale<DS>(sub(t1011, o32));
  Y r32 = descale<DS>(add(t1312, o01)), r45 = descale<DS>(sub(t1312, o01));
  A = _mm256_permute2x128_si256(r01, r45, 0x20);
  B = _mm256_permute2x128_si256(r01, r45, 0x31);
  C = _mm256_permute2x128_si256(r32, r76, 0x31);
  D = _mm256_permute2x128_si256(r32, r76, 0x20);
}

// First steps of the transpose of an 8x8 block held as A = [r0|r4],
// B = [r1|r5], C = [r2|r6], D = [r3|r7]: u0..u3 hold the columns (c0, c1),
// (c2, c3), (c4, c5), (c6, c7) as 64-bit units
// [cN rows 0-3, cN+1 rows 0-3 | cN rows 4-7, cN+1 rows 4-7].
VIO_INLINE void transpose_units(Y A, Y B, Y C, Y D, Y& u0, Y& u1, Y& u2, Y& u3) {
  Y ab0 = _mm256_unpacklo_epi16(A, B), ab1 = _mm256_unpackhi_epi16(A, B);
  Y cd0 = _mm256_unpacklo_epi16(C, D), cd1 = _mm256_unpackhi_epi16(C, D);
  u0 = _mm256_unpacklo_epi32(ab0, cd0);
  u1 = _mm256_unpackhi_epi32(ab0, cd0);
  u2 = _mm256_unpacklo_epi32(ab1, cd1);
  u3 = _mm256_unpackhi_epi32(ab1, cd1);
}

// [a.cN|b.cN] from units a = [cN, x | cN, x], b = [cM, y | cM, y], in the
// order the 64-bit permute `imm` selects.
template <int imm>
VIO_INLINE Y join_lo(Y a, Y b) { return _mm256_permute4x64_epi64(_mm256_unpacklo_epi64(a, b), imm); }
template <int imm>
VIO_INLINE Y join_hi(Y a, Y b) { return _mm256_permute4x64_epi64(_mm256_unpackhi_epi64(a, b), imm); }

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
  auto pair = [&](const std::int16_t* p, int a, int b) {
    return pair2(_mm_loadu_si128(reinterpret_cast<const X*>(p + a * 8)),
                 _mm_loadu_si128(reinterpret_cast<const X*>(p + b * 8)));
  };
  Y c04 = pair(coef_block, 0, 4), c26 = pair(coef_block, 2, 6);
  Y c75 = pair(coef_block, 7, 5), c13 = pair(coef_block, 1, 3);
  Y e04 = _mm256_mullo_epi16(c04, pair(quantptr, 0, 4));
  Y A, B, C, D;
  // Rows 1-7 all zero: every column is its DC term.
  Y ac = _mm256_or_si256(_mm256_or_si256(c26, c75),
                         _mm256_or_si256(c13, _mm256_blend_epi32(c04, _mm256_setzero_si256(), 0x0F)));
  if (_mm256_testz_si256(ac, ac)) {
    A = B = C = D = _mm256_slli_epi16(lows(e04, e04), PASS1_BITS);
  } else {
    idct_islow_pass<true>(e04, _mm256_mullo_epi16(c26, pair(quantptr, 2, 6)),
                          _mm256_mullo_epi16(c75, pair(quantptr, 7, 5)),
                          _mm256_mullo_epi16(c13, pair(quantptr, 1, 3)), A, B, C, D);
  }
  Y u0, u1, u2, u3;
  transpose_units(A, B, C, D, u0, u1, u2, u3);
  idct_islow_pass<false>(join_lo<0xD8>(u0, u2), join_lo<0xD8>(u1, u3),  // [c0|c4], [c2|c6]
                         join_hi<0x8D>(u2, u3), join_hi<0xD8>(u0, u1),  // [c7|c5], [c1|c3]
                         A, B, C, D);
  transpose_units(A, B, C, D, u0, u1, u2, u3);
  const Y center = _mm256_set1_epi8(static_cast<char>(-128));
  // [rows i, i+1 | rows i+4, i+5] as bytes
  auto store = [&](int i, Y lo, Y hi) {
    Y b = _mm256_add_epi8(_mm256_packs_epi16(lo, hi), center);
    X l = _mm256_castsi256_si128(b), h = _mm256_extracti128_si256(b, 1);
    _mm_storel_epi64(reinterpret_cast<X*>(output_buf[i] + output_col), l);
    _mm_storel_epi64(reinterpret_cast<X*>(output_buf[i + 1] + output_col), _mm_unpackhi_epi64(l, l));
    _mm_storel_epi64(reinterpret_cast<X*>(output_buf[i + 4] + output_col), h);
    _mm_storel_epi64(reinterpret_cast<X*>(output_buf[i + 5] + output_col), _mm_unpackhi_epi64(h, h));
  };
  store(0, join_lo<0xD8>(u0, u2), join_hi<0xD8>(u0, u2));  // [r0|r4], [r1|r5]
  store(2, join_lo<0xD8>(u1, u3), join_hi<0xD8>(u1, u3));  // [r2|r6], [r3|r7]
}
