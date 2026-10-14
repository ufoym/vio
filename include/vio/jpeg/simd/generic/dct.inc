// vio::jpeg::simd - integer forward/inverse DCTs and quantization for 128-bit
// vector back ends, written against the V interface of v128.inc so that
// SSE2, AltiVec, RVV and MMI share it.  Bit-exact with the scalar code for
// valid data.

namespace detail_dct {

using T = V::T;

VIO_INLINE T load(const void* p) { return V::loadu(p); }
VIO_INLINE void store(void* p, T v) { V::storeu(p, v); }
VIO_INLINE T pair(int a, int b) { return V::set2_16(a, b); }

// In-place transpose of an 8x8 matrix of 16-bit values.
VIO_INLINE void transpose8(T* r) {
  T a0 = V::unpacklo_16(r[0], r[1]), a1 = V::unpackhi_16(r[0], r[1]);
  T a2 = V::unpacklo_16(r[2], r[3]), a3 = V::unpackhi_16(r[2], r[3]);
  T a4 = V::unpacklo_16(r[4], r[5]), a5 = V::unpackhi_16(r[4], r[5]);
  T a6 = V::unpacklo_16(r[6], r[7]), a7 = V::unpackhi_16(r[6], r[7]);
  T b0 = V::unpacklo_32(a0, a2), b1 = V::unpackhi_32(a0, a2);
  T b2 = V::unpacklo_32(a1, a3), b3 = V::unpackhi_32(a1, a3);
  T b4 = V::unpacklo_32(a4, a6), b5 = V::unpackhi_32(a4, a6);
  T b6 = V::unpacklo_32(a5, a7), b7 = V::unpackhi_32(a5, a7);
  r[0] = V::unpacklo_64(b0, b4);
  r[1] = V::unpackhi_64(b0, b4);
  r[2] = V::unpacklo_64(b1, b5);
  r[3] = V::unpackhi_64(b1, b5);
  r[4] = V::unpacklo_64(b2, b6);
  r[5] = V::unpackhi_64(b2, b6);
  r[6] = V::unpacklo_64(b3, b7);
  r[7] = V::unpackhi_64(b3, b7);
}

// transpose8() on named vectors (MSVC keeps arrays of vectors in memory).
VIO_INLINE void transpose8(T& r0, T& r1, T& r2, T& r3, T& r4, T& r5, T& r6, T& r7) {
  T a0 = V::unpacklo_16(r0, r1), a1 = V::unpackhi_16(r0, r1);
  T a2 = V::unpacklo_16(r2, r3), a3 = V::unpackhi_16(r2, r3);
  T a4 = V::unpacklo_16(r4, r5), a5 = V::unpackhi_16(r4, r5);
  T a6 = V::unpacklo_16(r6, r7), a7 = V::unpackhi_16(r6, r7);
  T b0 = V::unpacklo_32(a0, a2), b1 = V::unpackhi_32(a0, a2);
  T b2 = V::unpacklo_32(a1, a3), b3 = V::unpackhi_32(a1, a3);
  T b4 = V::unpacklo_32(a4, a6), b5 = V::unpackhi_32(a4, a6);
  T b6 = V::unpacklo_32(a5, a7), b7 = V::unpackhi_32(a5, a7);
  r0 = V::unpacklo_64(b0, b4);
  r1 = V::unpackhi_64(b0, b4);
  r2 = V::unpacklo_64(b1, b5);
  r3 = V::unpackhi_64(b1, b5);
  r4 = V::unpacklo_64(b2, b6);
  r5 = V::unpackhi_64(b2, b6);
  r6 = V::unpacklo_64(b3, b7);
  r7 = V::unpackhi_64(b3, b7);
}

// 32-bit (lo, hi) halves of x * kx + y * ky for 16-bit x, y.
struct D {
  T lo, hi;
};
VIO_INLINE D madd(T x, T y, T k) {
  return {V::madd_16(V::unpacklo_16(x, y), k), V::madd_16(V::unpackhi_16(x, y), k)};
}
VIO_INLINE D add(D a, D b) { return {V::add_32(a.lo, b.lo), V::add_32(a.hi, b.hi)}; }
VIO_INLINE D sub(D a, D b) { return {V::sub_32(a.lo, b.lo), V::sub_32(a.hi, b.hi)}; }
// x << n as 32-bit values (n <= 16)
template <int N>
VIO_INLINE D widen_shl(T x) {
  return {V::srai_32<16 - N>(V::lo16_shl16(x)), V::srai_32<16 - N>(V::hi16_shl16(x))};
}
// (x + 2^(N-1)) >> N, saturated to 16 bits
template <int N>
VIO_INLINE T descale(D x) {
  const T r = V::set1_32(1 << (N - 1));
  return V::packs_32(V::srai_32<N>(V::add_32(x.lo, r)), V::srai_32<N>(V::add_32(x.hi, r)));
}

constexpr int CONST_BITS = 13, PASS1_BITS = 2;
constexpr int F_0_298 = 2446, F_0_390 = 3196, F_0_541 = 4433, F_0_765 = 6270, F_0_899 = 7373,
              F_1_175 = 9633, F_1_501 = 12299, F_1_847 = 15137, F_1_961 = 16069, F_2_053 = 16819,
              F_2_562 = 20995, F_3_072 = 25172;

// Accurate integer FDCT on 8 vectors (one per input sample index).
template <bool kPass1>
VIO_INLINE void fdct_islow_pass(T* d) {
  T tmp0 = V::add_16(d[0], d[7]), tmp7 = V::sub_16(d[0], d[7]);
  T tmp1 = V::add_16(d[1], d[6]), tmp6 = V::sub_16(d[1], d[6]);
  T tmp2 = V::add_16(d[2], d[5]), tmp5 = V::sub_16(d[2], d[5]);
  T tmp3 = V::add_16(d[3], d[4]), tmp4 = V::sub_16(d[3], d[4]);
  T tmp10 = V::add_16(tmp0, tmp3), tmp13 = V::sub_16(tmp0, tmp3);
  T tmp11 = V::add_16(tmp1, tmp2), tmp12 = V::sub_16(tmp1, tmp2);
  constexpr int DS = kPass1 ? CONST_BITS - PASS1_BITS : CONST_BITS + PASS1_BITS;
  if constexpr (kPass1) {
    d[0] = V::slli_16<PASS1_BITS>(V::add_16(tmp10, tmp11));
    d[4] = V::slli_16<PASS1_BITS>(V::sub_16(tmp10, tmp11));
  } else {
    const T r = V::set1_16(1 << (PASS1_BITS - 1));
    d[0] = V::srai_16<PASS1_BITS>(V::add_16(V::add_16(tmp10, tmp11), r));
    d[4] = V::srai_16<PASS1_BITS>(V::add_16(V::sub_16(tmp10, tmp11), r));
  }
  d[2] = descale<DS>(madd(tmp13, tmp12, pair(F_0_541 + F_0_765, F_0_541)));
  d[6] = descale<DS>(madd(tmp13, tmp12, pair(F_0_541, F_0_541 - F_1_847)));
  T z3 = V::add_16(tmp4, tmp6), z4 = V::add_16(tmp5, tmp7);
  D z3m = madd(z3, z4, pair(F_1_175 - F_1_961, F_1_175));
  D z4m = madd(z3, z4, pair(F_1_175, F_1_175 - F_0_390));
  d[7] = descale<DS>(add(madd(tmp4, tmp7, pair(F_0_298 - F_0_899, -F_0_899)), z3m));
  d[1] = descale<DS>(add(madd(tmp4, tmp7, pair(-F_0_899, F_1_501 - F_0_899)), z4m));
  d[5] = descale<DS>(add(madd(tmp5, tmp6, pair(F_2_053 - F_2_562, -F_2_562)), z4m));
  d[3] = descale<DS>(add(madd(tmp5, tmp6, pair(-F_2_562, F_3_072 - F_2_562)), z3m));
}

// Fast (AAN) integer FDCT; MULTIPLY(x, c) = (x * c) >> 8 computed as
// pmulhw(x << 2, c << 6).
constexpr int IFAST_SHIFT = 16 - 2 - 8;
VIO_INLINE void fdct_ifast_pass(T* d) {
  const T k0707 = V::set1_16(181 << IFAST_SHIFT), k0382 = V::set1_16(98 << IFAST_SHIFT),
          k0541 = V::set1_16(139 << IFAST_SHIFT), k1306 = V::set1_16(334 << IFAST_SHIFT);
  T tmp0 = V::add_16(d[0], d[7]), tmp7 = V::sub_16(d[0], d[7]);
  T tmp1 = V::add_16(d[1], d[6]), tmp6 = V::sub_16(d[1], d[6]);
  T tmp2 = V::add_16(d[2], d[5]), tmp5 = V::sub_16(d[2], d[5]);
  T tmp3 = V::add_16(d[3], d[4]), tmp4 = V::sub_16(d[3], d[4]);
  T tmp10 = V::add_16(tmp0, tmp3), tmp13 = V::sub_16(tmp0, tmp3);
  T tmp11 = V::add_16(tmp1, tmp2), tmp12 = V::sub_16(tmp1, tmp2);
  d[0] = V::add_16(tmp10, tmp11);
  d[4] = V::sub_16(tmp10, tmp11);
  T z1 = V::mulhi_16(V::slli_16<2>(V::add_16(tmp12, tmp13)), k0707);
  d[2] = V::add_16(tmp13, z1);
  d[6] = V::sub_16(tmp13, z1);
  tmp10 = V::slli_16<2>(V::add_16(tmp4, tmp5));
  tmp11 = V::slli_16<2>(V::add_16(tmp5, tmp6));
  tmp12 = V::slli_16<2>(V::add_16(tmp6, tmp7));
  T z5 = V::mulhi_16(V::sub_16(tmp10, tmp12), k0382);
  T z2 = V::add_16(V::mulhi_16(tmp10, k0541), z5);
  T z4 = V::add_16(V::mulhi_16(tmp12, k1306), z5);
  T z3 = V::mulhi_16(tmp11, k0707);
  T z11 = V::add_16(tmp7, z3), z13 = V::sub_16(tmp7, z3);
  d[5] = V::add_16(z13, z2);
  d[3] = V::sub_16(z13, z2);
  d[1] = V::add_16(z11, z4);
  d[7] = V::sub_16(z11, z4);
}

// Accurate integer IDCT on 8 dequantized vectors (one per coefficient
// index); kPass1 descales to the workspace, otherwise to output scale.
template <bool kPass1>
VIO_INLINE void idct_islow_pass(T* in) {
  constexpr int DS = kPass1 ? CONST_BITS - PASS1_BITS : CONST_BITS + PASS1_BITS + 3;
  D tmp3 = madd(in[2], in[6], pair(F_0_541 + F_0_765, F_0_541));
  D tmp2 = madd(in[2], in[6], pair(F_0_541, F_0_541 - F_1_847));
  D tmp0 = widen_shl<CONST_BITS>(V::add_16(in[0], in[4]));
  D tmp1 = widen_shl<CONST_BITS>(V::sub_16(in[0], in[4]));
  D tmp10 = add(tmp0, tmp3), tmp13 = sub(tmp0, tmp3);
  D tmp11 = add(tmp1, tmp2), tmp12 = sub(tmp1, tmp2);
  T z3 = V::add_16(in[3], in[7]), z4 = V::add_16(in[1], in[5]);
  D z3m = madd(z3, z4, pair(F_1_175 - F_1_961, F_1_175));
  D z4m = madd(z3, z4, pair(F_1_175, F_1_175 - F_0_390));
  D o0 = add(madd(in[7], in[1], pair(F_0_298 - F_0_899, -F_0_899)), z3m);
  D o3 = add(madd(in[7], in[1], pair(-F_0_899, F_1_501 - F_0_899)), z4m);
  D o1 = add(madd(in[5], in[3], pair(F_2_053 - F_2_562, -F_2_562)), z4m);
  D o2 = add(madd(in[5], in[3], pair(-F_2_562, F_3_072 - F_2_562)), z3m);
  in[0] = descale<DS>(add(tmp10, o3));
  in[7] = descale<DS>(sub(tmp10, o3));
  in[1] = descale<DS>(add(tmp11, o2));
  in[6] = descale<DS>(sub(tmp11, o2));
  in[2] = descale<DS>(add(tmp12, o1));
  in[5] = descale<DS>(sub(tmp12, o1));
  in[3] = descale<DS>(add(tmp13, o0));
  in[4] = descale<DS>(sub(tmp13, o0));
}

// idct_islow_pass() on named vectors.
template <bool kPass1>
VIO_INLINE void idct_islow_pass(T& i0, T& i1, T& i2, T& i3, T& i4, T& i5, T& i6, T& i7) {
  constexpr int DS = kPass1 ? CONST_BITS - PASS1_BITS : CONST_BITS + PASS1_BITS + 3;
  D tmp3 = madd(i2, i6, pair(F_0_541 + F_0_765, F_0_541));
  D tmp2 = madd(i2, i6, pair(F_0_541, F_0_541 - F_1_847));
  D tmp0 = widen_shl<CONST_BITS>(V::add_16(i0, i4));
  D tmp1 = widen_shl<CONST_BITS>(V::sub_16(i0, i4));
  D tmp10 = add(tmp0, tmp3), tmp13 = sub(tmp0, tmp3);
  D tmp11 = add(tmp1, tmp2), tmp12 = sub(tmp1, tmp2);
  T z3 = V::add_16(i3, i7), z4 = V::add_16(i1, i5);
  D z3m = madd(z3, z4, pair(F_1_175 - F_1_961, F_1_175));
  D z4m = madd(z3, z4, pair(F_1_175, F_1_175 - F_0_390));
  D o0 = add(madd(i7, i1, pair(F_0_298 - F_0_899, -F_0_899)), z3m);
  D o3 = add(madd(i7, i1, pair(-F_0_899, F_1_501 - F_0_899)), z4m);
  D o1 = add(madd(i5, i3, pair(F_2_053 - F_2_562, -F_2_562)), z4m);
  D o2 = add(madd(i5, i3, pair(-F_2_562, F_3_072 - F_2_562)), z3m);
  i0 = descale<DS>(add(tmp10, o3));
  i7 = descale<DS>(sub(tmp10, o3));
  i1 = descale<DS>(add(tmp11, o2));
  i6 = descale<DS>(sub(tmp11, o2));
  i2 = descale<DS>(add(tmp12, o1));
  i5 = descale<DS>(sub(tmp12, o1));
  i3 = descale<DS>(add(tmp13, o0));
  i4 = descale<DS>(sub(tmp13, o0));
}

// Fast (AAN) integer IDCT pass, in 16 bits.
VIO_INLINE void idct_ifast_pass(T* in) {
  const T k1414 = V::set1_16(362 << IFAST_SHIFT), k1847 = V::set1_16(473 << IFAST_SHIFT),
          km1613 = V::set1_16(-(669 - 256) << IFAST_SHIFT),
          k1082 = V::set1_16(277 << IFAST_SHIFT);
  T tmp10 = V::add_16(in[0], in[4]), tmp11 = V::sub_16(in[0], in[4]);
  T tmp13 = V::add_16(in[2], in[6]);
  T tmp12 = V::sub_16(
      V::mulhi_16(V::slli_16<2>(V::sub_16(in[2], in[6])), k1414), tmp13);
  T tmp0 = V::add_16(tmp10, tmp13), tmp3 = V::sub_16(tmp10, tmp13);
  T tmp1 = V::add_16(tmp11, tmp12), tmp2 = V::sub_16(tmp11, tmp12);
  T z13 = V::add_16(in[5], in[3]), z10 = V::sub_16(in[5], in[3]);
  T z11 = V::add_16(in[1], in[7]), z12 = V::sub_16(in[1], in[7]);
  T tmp7 = V::add_16(z11, z13);
  T t11 = V::mulhi_16(V::slli_16<2>(V::sub_16(z11, z13)), k1414);
  T z10s = V::slli_16<2>(z10), z12s = V::slli_16<2>(z12);
  T z5 = V::mulhi_16(V::add_16(z10s, z12s), k1847);
  T t10 = V::sub_16(V::mulhi_16(z12s, k1082), z5);
  // MULTIPLY(z10, -FIX(2.613)) = MULTIPLY(z10, -FIX(1.613)) - z10
  T t12 = V::add_16(V::sub_16(V::mulhi_16(z10s, km1613), z10), z5);
  T tmp6 = V::sub_16(t12, tmp7);
  T tmp5 = V::sub_16(t11, tmp6);
  T tmp4 = V::add_16(t10, tmp5);
  in[0] = V::add_16(tmp0, tmp7);
  in[7] = V::sub_16(tmp0, tmp7);
  in[1] = V::add_16(tmp1, tmp6);
  in[6] = V::sub_16(tmp1, tmp6);
  in[2] = V::add_16(tmp2, tmp5);
  in[5] = V::sub_16(tmp2, tmp5);
  in[4] = V::add_16(tmp3, tmp4);
  in[3] = V::sub_16(tmp3, tmp4);
}

// Loads and dequantizes coefficient rows; returns true if rows 1-7 are all
// zero (the block is DC-only).
VIO_INLINE bool load_dequantize(const std::int16_t* coef, const std::int16_t* quant, T* r) {
  T ac = V::zero();
  for (int i = 0; i < 8; i++) {
    T c = load(coef + i * 8);
    if (i) ac = V::or_(ac, c);
    r[i] = V::mullo_16(c, load(quant + i * 8));
  }
  return V::all_zero(ac);
}

// Packs 8 rows of output-scale values to samples (clamped, + CENTERJSAMPLE).
VIO_INLINE void store_rows(T* r, std::uint8_t** output_buf, unsigned output_col) {
  const T center = V::set1_8(-128);
  for (int i = 0; i < 8; i += 2) {
    T b = V::add_8(V::packs_16(r[i], r[i + 1]), center);
    V::storel_64(output_buf[i] + output_col, b);
    V::storel_64(output_buf[i + 1] + output_col, V::bsrli<8>(b));
  }
}

}  // namespace detail_dct

inline void convsamp(std::uint8_t** sample_data, unsigned start_col, std::int16_t* workspace) {
  using namespace detail_dct;
  const T center = V::set1_16(128);
  for (int r = 0; r < 8; r++) {
    T s = V::loadl_64(sample_data[r] + start_col);
    store(workspace + r * 8, V::sub_16(V::widenlo_u8(s), center));
  }
}

inline void fdct_islow(std::int16_t* data) {
  using namespace detail_dct;
  T d[8];
  for (int i = 0; i < 8; i++) d[i] = load(data + i * 8);
  transpose8(d);
  fdct_islow_pass<true>(d);
  transpose8(d);
  fdct_islow_pass<false>(d);
  for (int i = 0; i < 8; i++) store(data + i * 8, d[i]);
}

inline void fdct_ifast(std::int16_t* data) {
  using namespace detail_dct;
  T d[8];
  for (int i = 0; i < 8; i++) d[i] = load(data + i * 8);
  transpose8(d);
  fdct_ifast_pass(d);
  transpose8(d);
  fdct_ifast_pass(d);
  for (int i = 0; i < 8; i++) store(data + i * 8, d[i]);
}

// convsamp(), the forward DCT and quantize() in one: the block stays in
// registers.
template <bool kIslow>
VIO_INLINE void forward_dct_quantize(std::uint8_t** sample_data, unsigned start_col,
                                     const std::uint16_t* divisors, std::int16_t* coef_block) {
  using namespace detail_dct;
  const T center = V::set1_16(128);
  T d[8];
  for (int r = 0; r < 8; r++)
    d[r] = V::sub_16(V::widenlo_u8(V::loadl_64(sample_data[r] + start_col)), center);
  transpose8(d);
  if constexpr (kIslow) fdct_islow_pass<true>(d);
  else fdct_ifast_pass(d);
  transpose8(d);
  if constexpr (kIslow) fdct_islow_pass<false>(d);
  else fdct_ifast_pass(d);
  for (int i = 0; i < 8; i++) {
    T x = d[i];
    T sign = V::srai_16<15>(x);
    T a = V::sub_16(V::xor_(x, sign), sign);
    a = V::add_16(a, load(divisors + 64 + 8 * i));
    a = V::mulhi_u16(a, load(divisors + 8 * i));
    a = V::mulhi_u16(a, load(divisors + 128 + 8 * i));
    store(coef_block + 8 * i, V::sub_16(V::xor_(a, sign), sign));
  }
}
inline void forward_islow(std::uint8_t** sample_data, unsigned start_col,
                          const std::uint16_t* divisors, std::int16_t* coef_block) {
  forward_dct_quantize<true>(sample_data, start_col, divisors, coef_block);
}
inline void forward_ifast(std::uint8_t** sample_data, unsigned start_col,
                          const std::uint16_t* divisors, std::int16_t* coef_block) {
  forward_dct_quantize<false>(sample_data, start_col, divisors, coef_block);
}

// 16-bit reciprocal quantization:
// |x| -> ((|x| + corr) * recip >> 16) * scale >> 16, sign restored.
inline void quantize(std::int16_t* coef_block, const std::uint16_t* divisors,
                     const std::int16_t* workspace) {
  using namespace detail_dct;
  for (int i = 0; i < 64; i += 8) {
    T x = load(workspace + i);
    T sign = V::srai_16<15>(x);
    T a = V::sub_16(V::xor_(x, sign), sign);
    a = V::add_16(a, load(divisors + 64 + i));
    a = V::mulhi_u16(a, load(divisors + i));
    a = V::mulhi_u16(a, load(divisors + 128 + i));
    store(coef_block + i, V::sub_16(V::xor_(a, sign), sign));
  }
}

// The block in named vectors throughout (MSVC keeps arrays of vectors in
// memory).
inline void idct_islow(const std::int16_t* quantptr, const std::int16_t* coef_block,
                       std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_dct;
  auto c = [&](int i) VIO_LAMBDA_INLINE { return load(coef_block + i * 8); };
  auto q = [&](int i) VIO_LAMBDA_INLINE { return load(quantptr + i * 8); };
  T c1 = c(1), c2 = c(2), c3 = c(3), c4 = c(4), c5 = c(5), c6 = c(6), c7 = c(7);
  T r0 = V::mullo_16(c(0), q(0));
  T r1, r2, r3, r4, r5, r6, r7;
  const T ac = V::or_(V::or_(V::or_(c1, c2), V::or_(c3, c4)), V::or_(V::or_(c5, c6), c7));
  if (V::all_zero(ac)) {
    // DC-only block: every pass-1 output equals DC << PASS1_BITS.
    r0 = r1 = r2 = r3 = r4 = r5 = r6 = r7 = V::slli_16<PASS1_BITS>(r0);
  } else {
    r1 = V::mullo_16(c1, q(1));
    r2 = V::mullo_16(c2, q(2));
    r3 = V::mullo_16(c3, q(3));
    r4 = V::mullo_16(c4, q(4));
    r5 = V::mullo_16(c5, q(5));
    r6 = V::mullo_16(c6, q(6));
    r7 = V::mullo_16(c7, q(7));
    idct_islow_pass<true>(r0, r1, r2, r3, r4, r5, r6, r7);
  }
  transpose8(r0, r1, r2, r3, r4, r5, r6, r7);
  idct_islow_pass<false>(r0, r1, r2, r3, r4, r5, r6, r7);
  transpose8(r0, r1, r2, r3, r4, r5, r6, r7);
  const T center = V::set1_8(-128);
  auto rows = [&](int i, T a, T b) VIO_LAMBDA_INLINE {
    T v = V::add_8(V::packs_16(a, b), center);
    V::storel_64(output_buf[i] + output_col, v);
    V::storel_64(output_buf[i + 1] + output_col, V::bsrli<8>(v));
  };
  rows(0, r0, r1);
  rows(2, r2, r3);
  rows(4, r4, r5);
  rows(6, r6, r7);
}

inline void idct_ifast(const std::int16_t* quantptr, const std::int16_t* coef_block,
                       std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_dct;
  T r[8];
  if (load_dequantize(coef_block, quantptr, r)) {
    for (int i = 1; i < 8; i++) r[i] = r[0];
  } else {
    idct_ifast_pass(r);
  }
  transpose8(r);
  idct_ifast_pass(r);
  for (int i = 0; i < 8; i++) r[i] = V::srai_16<PASS1_BITS + 3>(r[i]);
  transpose8(r);
  store_rows(r, output_buf, output_col);
}

// Reduced-size IDCTs, producing 4x4 and 2x2 outputs.
namespace detail_dct {
constexpr int F_0_211 = 1730, F_0_509 = 4176, F_0_601 = 4926, F_0_720 = 5906, F_0_850 = 6967,
              F_1_061 = 8697, F_1_272 = 10426, F_1_451 = 11893, F_2_172 = 17799,
              F_3_624 = 29692;

// 4-point odd/even halves for one pass of the 4x4 IDCT (row 4 unused).
template <int DS>
VIO_INLINE void idct4_pass(const T* in, T* out) {
  D tmp0 = widen_shl<CONST_BITS + 1>(in[0]);
  D tmp2 = madd(in[2], in[6], pair(F_1_847, -F_0_765));
  D tmp10 = add(tmp0, tmp2), tmp12 = sub(tmp0, tmp2);
  D t0 = add(madd(in[7], in[5], pair(-F_0_211, F_1_451)), madd(in[3], in[1], pair(-F_2_172, F_1_061)));
  D t2 = add(madd(in[7], in[5], pair(-F_0_509, -F_0_601)), madd(in[3], in[1], pair(F_0_899, F_2_562)));
  out[0] = descale<DS>(add(tmp10, t2));
  out[3] = descale<DS>(sub(tmp10, t2));
  out[1] = descale<DS>(add(tmp12, t0));
  out[2] = descale<DS>(sub(tmp12, t0));
}

template <int DS>
VIO_INLINE void idct2_pass(const T* in, T* out) {
  D tmp10 = widen_shl<CONST_BITS + 2>(in[0]);
  D tmp0 = add(madd(in[7], in[5], pair(-F_0_720, F_0_850)), madd(in[3], in[1], pair(-F_1_272, F_3_624)));
  out[0] = descale<DS>(add(tmp10, tmp0));
  out[1] = descale<DS>(sub(tmp10, tmp0));
}
}  // namespace detail_dct

inline void idct_4x4(const std::int16_t* quantptr, const std::int16_t* coef_block,
                     std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_dct;
  T r[8], ws[8];
  load_dequantize(coef_block, quantptr, r);
  // Pass 1: columns -> 4 workspace rows (all 8 columns; column 4 unused).
  idct4_pass<CONST_BITS - PASS1_BITS + 1>(r, ws);
  for (int i = 4; i < 8; i++) ws[i] = V::zero();
  transpose8(ws);  // ws[k] = column k of the 4 rows (lanes 0-3)
  T o[4];
  idct4_pass<CONST_BITS + PASS1_BITS + 3 + 1>(ws, o);  // o[j] = output column j, lanes = rows
  // o[j] lanes 0-3 hold rows 0-3; transpose 4x4 and pack.
  T p01 = V::unpacklo_16(o[0], o[1]), p23 = V::unpacklo_16(o[2], o[3]);
  T rows01 = V::unpacklo_32(p01, p23), rows23 = V::unpackhi_32(p01, p23);
  alignas(16) std::uint8_t b[16];
  V::storeu(b, V::add_8(V::packs_16(rows01, rows23), V::set1_8(-128)));
  for (int i = 0; i < 4; i++) std::memcpy(output_buf[i] + output_col, b + 4 * i, 4);
}

inline void idct_2x2(const std::int16_t* quantptr, const std::int16_t* coef_block,
                     std::uint8_t** output_buf, unsigned output_col) {
  using namespace detail_dct;
  T r[8], ws[8];
  load_dequantize(coef_block, quantptr, r);
  idct2_pass<CONST_BITS - PASS1_BITS + 2>(r, ws);  // ws[0], ws[1]: 2 rows x 8 columns
  for (int i = 2; i < 8; i++) ws[i] = V::zero();
  transpose8(ws);  // ws[k] lanes 0-1 = column k of the 2 rows
  T o[2];
  idct2_pass<CONST_BITS + PASS1_BITS + 3 + 2>(ws, o);  // o[j] lanes 0-1 = rows 0-1
  T rows = V::unpacklo_16(o[0], o[1]);  // r0c0 r0c1 r1c0 r1c1
  alignas(16) std::uint8_t b[16];
  V::storeu(b, V::add_8(V::packs_16(rows, rows), V::set1_8(-128)));
  output_buf[0][output_col] = b[0];
  output_buf[0][output_col + 1] = b[1];
  output_buf[1][output_col] = b[2];
  output_buf[1][output_col + 1] = b[3];
}
