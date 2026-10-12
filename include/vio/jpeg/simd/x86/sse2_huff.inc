// vio::jpeg::simd::sse2 - sequential Huffman encoding of one block and the
// progressive AC preparation kernels.  Coefficients are gathered in
// zig-zag order, SSE2 computes the zero/sign/magnitude bitmaps, and the
// non-zero coefficients are visited with count-trailing-zeros.

namespace detail_huff {

using T = __m128i;

VIO_INLINE unsigned ctz64(std::uint64_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long i;
#if VIO_SIMD_X86_64
  _BitScanForward64(&i, v);
#else
  if (static_cast<std::uint32_t>(v)) _BitScanForward(&i, static_cast<std::uint32_t>(v));
  else { _BitScanForward(&i, static_cast<std::uint32_t>(v >> 32)); i += 32; }
#endif
  return i;
#else
  return static_cast<unsigned>(__builtin_ctzll(v));
#endif
}

VIO_INLINE int clz64(std::uint64_t v) {  // v != 0
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long i;
#if VIO_SIMD_X86_64
  _BitScanReverse64(&i, v);
  return 63 - static_cast<int>(i);
#else
  if (v >> 32) { _BitScanReverse(&i, static_cast<std::uint32_t>(v >> 32)); return 31 - static_cast<int>(i); }
  _BitScanReverse(&i, static_cast<std::uint32_t>(v));
  return 63 - static_cast<int>(i);
#endif
#else
  return __builtin_clzll(v);
#endif
}

VIO_INLINE int bit_length_nonzero(unsigned v) {  // number of bits in v != 0
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long i;
  _BitScanReverse(&i, v);
  return static_cast<int>(i) + 1;
#else
  return 32 - __builtin_clz(v);
#endif
}

VIO_INLINE int bit_length(unsigned v) {  // number of bits in v (0 for 0)
#if defined(_MSC_VER) && !defined(__clang__)
  unsigned long i;
  return _BitScanReverse(&i, v) ? static_cast<int>(i) + 1 : 0;
#else
  return v ? 32 - __builtin_clz(v) : 0;
#endif
}

// Bit k set when mask lane k (0..63, 16-bit lanes in r[0..7]) is all ones.
VIO_INLINE std::uint64_t movemask64(const T* r) {
  std::uint64_t m = 0;
  for (int i = 0; i < 4; i++)
    m |= static_cast<std::uint64_t>(
             static_cast<unsigned>(_mm_movemask_epi8(_mm_packs_epi16(r[2 * i], r[2 * i + 1]))))
         << (16 * i);
  return m;
}

}  // namespace detail_huff

namespace detail_huff {

// Loads coefficients 1..63 of a natural-order block in zig-zag order into
// eight vectors (z[r] lane i = zig-zag position 8r+i+1; z[7] lane 7 = 0),
// using shuffles and a few word inserts instead of a round trip through
// memory, which would defeat store-to-load forwarding.
VIO_INLINE void load_zigzag(const std::int16_t* block, T z[8]) {
  auto ld = [&](int i) { return _mm_loadu_si128(reinterpret_cast<const T*>(block + i)); };
  auto ldl = [&](int i) { return _mm_loadl_epi64(reinterpret_cast<const T*>(block + i)); };
  auto ps = [](T v) { return _mm_castsi128_ps(v); };
  auto pd = [](T v) { return _mm_castsi128_pd(v); };
  T r0 = ld(0), r1 = ld(8), r6 = ld(48), r7 = ld(56);

  // Row 0: 01 08 16 09 02 03 10 17
  T a = _mm_shufflelo_epi16(_mm_unpacklo_epi32(r0, r1), 0xC9);  // 01 08 xx 09 02 03 10 11
  a = _mm_insert_epi16(a, block[16], 2);
  z[0] = _mm_insert_epi16(a, block[17], 7);

  // Row 1: 24 32 25 18 11 04 05 12
  T d = _mm_unpackhi_epi32(r0, r1);                              // 04 05 12 13 06 07 14 15
  T b = _mm_slli_si128(_mm_unpacklo_epi64(r1, d), 2);            // -- 08 09 10 11 04 05 12
  T c = _mm_shufflelo_epi16(ldl(24), 0xD8);                      // 24 26 25 27 -- -- -- --
  b = _mm_castpd_si128(_mm_move_sd(pd(b), pd(c)));               // 24 26 25 27 11 04 05 12
  b = _mm_insert_epi16(b, block[32], 1);
  z[1] = _mm_insert_epi16(b, block[18], 3);

  // Row 2: 19 26 33 40 48 41 34 27
  c = _mm_unpacklo_epi64(c, r6);                                 // 24 26 25 27 48 49 50 51
  c = _mm_insert_epi16(c, block[19], 0);
  c = _mm_insert_epi16(c, block[33], 2);
  c = _mm_insert_epi16(c, block[40], 3);
  c = _mm_insert_epi16(c, block[41], 5);
  c = _mm_insert_epi16(c, block[34], 6);
  z[2] = _mm_insert_epi16(c, block[27], 7);

  // Row 3: 20 13 06 07 14 21 28 35
  d = _mm_srli_si128(d, 4);                                      // 12 13 06 07 14 15 -- --
  d = _mm_insert_epi16(d, block[20], 0);
  d = _mm_insert_epi16(d, block[21], 5);
  d = _mm_insert_epi16(d, block[28], 6);
  z[3] = _mm_insert_epi16(d, block[35], 7);

  // Row 4: 42 49 56 57 50 43 36 29
  T e = _mm_castps_si128(_mm_unpacklo_ps(ps(r6), ps(r7)));      // 48 49 56 57 50 51 58 59
  T f = _mm_unpacklo_epi32(ldl(36), ldl(44));                    // 36 37 44 45 38 39 46 47
  T g = _mm_srli_si128(f, 2);                                    // 37 44 45 38 39 46 47 --
  f = _mm_castpd_si128(_mm_shuffle_pd(pd(f), pd(e), 2));         // 36 37 44 45 50 51 58 59
  e = _mm_insert_epi16(e, block[42], 0);
  e = _mm_insert_epi16(e, block[43], 5);
  e = _mm_insert_epi16(e, block[36], 6);
  z[4] = _mm_insert_epi16(e, block[29], 7);

  // Row 5: 22 15 23 30 37 44 51 58
  f = _mm_shuffle_epi32(_mm_slli_si128(f, 2), 0xD8);             // -- 36 45 50 37 44 51 58
  f = _mm_castps_si128(_mm_loadl_pi(ps(f), reinterpret_cast<const __m64*>(block + 20)));
  f = _mm_shufflelo_epi16(f, 0x72);                              // 22 20 23 21 37 44 51 58
  f = _mm_insert_epi16(f, block[15], 1);
  z[5] = _mm_insert_epi16(f, block[30], 3);

  // Row 6: 59 52 45 38 31 39 46 53
  g = _mm_shufflehi_epi16(g, 0xD3);                              // 37 44 45 38 -- 39 46 --
  g = _mm_insert_epi16(g, block[59], 0);
  g = _mm_insert_epi16(g, block[52], 1);
  g = _mm_insert_epi16(g, block[31], 4);
  z[6] = _mm_insert_epi16(g, block[53], 7);

  // Row 7: 60 61 54 47 55 62 63 (lane 7 = 0)
  T h = _mm_srli_si128(_mm_unpackhi_epi32(r6, r7), 2);           // 53 60 61 54 55 62 63 --
  h = _mm_shufflelo_epi16(h, 0x39);                              // 60 61 54 53 55 62 63 --
  z[7] = _mm_insert_epi16(h, block[47], 3);
}

}  // namespace detail_huff

template <class Table>
inline std::uint8_t* encode_one_block(HuffBitState& st, std::uint8_t* buffer,
                                      const std::int16_t* block, int last_dc_val,
                                      const Table& dctbl, const Table& actbl) {
  using namespace detail_huff;
  // t[i]: zig-zag coefficient i+1, negative values minus one (the bits to
  // emit); `index` bit i is set when it is non-zero.
  alignas(16) std::int16_t t[64];
  T z[8];
  load_zigzag(block, z);
  const T zero = _mm_setzero_si128();
  T eq[8];
  for (int i = 0; i < 8; i++) {
    T v = _mm_add_epi16(z[i], _mm_cmpgt_epi16(zero, z[i]));
    _mm_store_si128(reinterpret_cast<T*>(t + 8 * i), v);
    eq[i] = _mm_cmpeq_epi16(v, zero);
  }
  std::uint64_t index = ~movemask64(eq);

  HuffBitWriter w(st, buffer);
  // DC difference (32-bit, as in the scalar encoder).
  int temp = block[0] - last_dc_val;
  int sign = temp >> 31;
  int nbits = bit_length(static_cast<unsigned>(temp ^ sign) - static_cast<unsigned>(sign));
  w.put_code(dctbl.ehufco[nbits], dctbl.ehufsi[nbits],
             static_cast<unsigned>(temp + sign) & ((1u << nbits) - 1), nbits);

  const std::int16_t* p = t - 1;  // coefficient k is p[k]
  while (index) {
    unsigned run = ctz64(index);
    index = (index >> run) >> 1;
    p += run + 1;
    while (run > 15) {
      w.put_bits(actbl.ehufco[0xf0], actbl.ehufsi[0xf0]);
      run -= 16;
    }
    const int code = *p;
    nbits = bit_length_nonzero(static_cast<unsigned>(code ^ (code >> 31)));
    const unsigned rs = (run << 4) + static_cast<unsigned>(nbits);
    w.put_code(actbl.ehufco[rs], actbl.ehufsi[rs], static_cast<unsigned>(code) & ((1u << nbits) - 1),
               nbits);
  }
  if (p != t + 62) w.put_bits(actbl.ehufco[0], actbl.ehufsi[0]);
  st = w.state();
  return w.buffer;
}

namespace detail_huff {
// Gathers coefficients Ss..Ss+Sl-1 in zig-zag order (zero beyond Sl).
VIO_INLINE void gather_band(const std::int16_t* block, const int* order, int Sl, std::int16_t* out) {
  int k = 0;
  for (; k < Sl; k++) out[k] = block[order[k]];
  for (; k < 64; k++) out[k] = 0;
}
}  // namespace detail_huff

inline std::uint64_t ac_first_prepare(const std::int16_t* block, const int* order, int Sl, int Al,
                                      std::uint16_t* values) {
  using namespace detail_huff;
  alignas(16) std::int16_t c[64];
  gather_band(block, order, Sl, c);
  const T zero = _mm_setzero_si128();
  const T shift = _mm_cvtsi32_si128(Al);
  T eq[8];
  for (int i = 0; i < 8; i++) {
    T x = _mm_load_si128(reinterpret_cast<const T*>(c + i * 8));
    T sign = _mm_srai_epi16(x, 15);
    T a = _mm_srl_epi16(_mm_sub_epi16(_mm_xor_si128(x, sign), sign), shift);
    _mm_storeu_si128(reinterpret_cast<T*>(values + i * 8), a);
    _mm_storeu_si128(reinterpret_cast<T*>(values + 64 + i * 8), _mm_xor_si128(a, sign));
    eq[i] = _mm_cmpeq_epi16(a, zero);
  }
  return ~movemask64(eq);
}

inline int ac_refine_prepare(const std::int16_t* block, const int* order, int Sl, int Al,
                             std::uint16_t* absvalues, std::uint64_t* zerobits,
                             std::uint64_t* signbits) {
  using namespace detail_huff;
  alignas(16) std::int16_t c[64];
  gather_band(block, order, Sl, c);
  const T zero = _mm_setzero_si128(), one = _mm_set1_epi16(1);
  const T shift = _mm_cvtsi32_si128(Al);
  T eq0[8], neg[8], eq1[8];
  for (int i = 0; i < 8; i++) {
    T x = _mm_load_si128(reinterpret_cast<const T*>(c + i * 8));
    T sign = _mm_srai_epi16(x, 15);
    T a = _mm_srl_epi16(_mm_sub_epi16(_mm_xor_si128(x, sign), sign), shift);
    _mm_storeu_si128(reinterpret_cast<T*>(absvalues + i * 8), a);
    eq0[i] = _mm_cmpeq_epi16(a, zero);
    neg[i] = sign;
    eq1[i] = _mm_cmpeq_epi16(a, one);
  }
  *zerobits = ~movemask64(eq0);
  *signbits = ~movemask64(neg);
  std::uint64_t eob = movemask64(eq1);
  return eob ? 63 - clz64(eob) : 0;
}
