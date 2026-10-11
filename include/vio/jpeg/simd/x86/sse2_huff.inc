// vio::jpeg::simd::sse2 - sequential Huffman encoding of one block and the
// progressive AC preparation kernels.  Coefficients are gathered in
// zig-zag order, SSE2 computes the zero/sign/magnitude bitmaps, and the
// non-zero coefficients are visited with count-trailing-zeros.

namespace detail_huff {

using T = __m128i;

// Natural-order index of each zig-zag position.
alignas(16) inline constexpr std::uint8_t zigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

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

template <class Table>
inline std::uint8_t* encode_one_block(HuffBitState& st, std::uint8_t* buffer,
                                      const std::int16_t* block, int last_dc_val,
                                      const Table& dctbl, const Table& actbl) {
  using namespace detail_huff;
  alignas(16) std::int16_t zz[64];
  zz[0] = 0;
  for (int k = 1; k < 64; k++) zz[k] = block[zigzag[k]];
  T eq[8];
  const T zero = _mm_setzero_si128();
  for (int i = 0; i < 8; i++)
    eq[i] = _mm_cmpeq_epi16(_mm_load_si128(reinterpret_cast<const T*>(zz + i * 8)), zero);
  std::uint64_t nonzero = ~movemask64(eq) & ~std::uint64_t(1);

  HuffBitWriter w(st, buffer);
  // DC difference (32-bit, as in the scalar encoder).
  int temp = block[0] - last_dc_val;
  int temp2 = temp;
  if (temp < 0) {
    temp = -temp;
    temp2--;
  }
  int nbits = bit_length(static_cast<unsigned>(temp));
  w.put_code(dctbl.ehufco[nbits], dctbl.ehufsi[nbits],
             static_cast<unsigned>(temp2) & ((1u << nbits) - 1), nbits);

  int last = 0;
  const unsigned code_0xf0 = actbl.ehufco[0xf0];
  const int size_0xf0 = actbl.ehufsi[0xf0];
  while (nonzero) {
    const int k = static_cast<int>(ctz64(nonzero));
    nonzero &= nonzero - 1;
    int run = k - last - 1;
    last = k;
    while (run > 15) {
      w.put_bits(code_0xf0, size_0xf0);
      run -= 16;
    }
    int c = zz[k];
    int bits = c + (c >> 31);  // one's complement for negative values
    nbits = bit_length(static_cast<unsigned>(c < 0 ? -c : c));
    const int rs = (run << 4) + nbits;
    w.put_code(actbl.ehufco[rs], actbl.ehufsi[rs], static_cast<unsigned>(bits) & ((1u << nbits) - 1),
               nbits);
  }
  if (last != 63) w.put_bits(actbl.ehufco[0], actbl.ehufsi[0]);
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
