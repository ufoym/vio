// vio::jpeg::simd::sse2 - sequential Huffman encoding of one block and the
// progressive AC preparation kernels.  Coefficients are gathered in
// zig-zag order, SSE2 computes the zero/sign/magnitude bitmaps, and the
// non-zero coefficients are visited with count-trailing-zeros.

namespace detail_huff {

using T = __m128i;

VIO_INLINE unsigned ctz64(std::uint64_t v) {  // v != 0
#if defined(_MSC_VER) && !defined(__clang__)
#if VIO_SIMD_X86_64
  // tzcnt runs as bsf on CPUs without BMI1 (same result for v != 0), and
  // unlike bsf it does not depend on the previous value of its destination.
  return static_cast<unsigned>(_tzcnt_u64(v));
#else
  unsigned long i;
  if (static_cast<std::uint32_t>(v)) _BitScanForward(&i, static_cast<std::uint32_t>(v));
  else { _BitScanForward(&i, static_cast<std::uint32_t>(v >> 32)); i += 32; }
  return i;
#endif
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

#if VIO_HUFF_BMI2
VIO_INLINE int ac_bit_length(unsigned v) {  // number of bits in v, 0 < v <= 32768
#if defined(_MSC_VER) && !defined(__clang__)
  return 32 - static_cast<int>(_lzcnt_u32(v));
#else
  return 32 - __builtin_clz(v);
#endif
}
#else
// Bit lengths of 0..32768.  Without LZCNT, bsr would chain each coefficient
// to the previous one: its destination register is also an input.
inline constexpr auto kBitLength = [] {
  std::array<std::uint8_t, 32769> t{};
  for (unsigned v = 1, n = 1; v < t.size(); v++) {
    if (v >> n) n++;
    t[v] = static_cast<std::uint8_t>(n);
  }
  return t;
}();
VIO_INLINE int ac_bit_length(unsigned v) { return kBitLength[v]; }  // v <= 32768
#endif

VIO_INLINE int bit_length(unsigned v) {  // number of bits in v (0 for 0)
#if defined(_MSC_VER) && !defined(__clang__) && VIO_HUFF_BMI2
  return 32 - static_cast<int>(_lzcnt_u32(v));
#elif defined(_MSC_VER) && !defined(__clang__)
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

// (1 << n) - 1
alignas(64) inline constexpr std::uint32_t kMaskBits[17] = {
    0x0000, 0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
    0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff};

// Variable shifts and low-bit masks.  MSVC has no per-function targets, so
// the BMI2 build spells out shlx/shrx/bzhi; other compilers derive them.
#if defined(_MSC_VER) && !defined(__clang__) && VIO_HUFF_BMI2 && VIO_SIMD_X86_64
VIO_INLINE std::uint64_t shl64(std::uint64_t v, unsigned n) { return _shlx_u64(v, n); }
VIO_INLINE std::uint64_t shr64(std::uint64_t v, unsigned n) { return _shrx_u64(v, n); }
VIO_INLINE unsigned low_bits(unsigned v, int n) { return _bzhi_u32(v, static_cast<unsigned>(n)); }
VIO_INLINE std::uint64_t clear_lowest(std::uint64_t v) { return _blsr_u64(v); }
#else
VIO_INLINE std::uint64_t shl64(std::uint64_t v, unsigned n) { return v << n; }
VIO_INLINE std::uint64_t shr64(std::uint64_t v, unsigned n) { return v >> n; }
#if VIO_HUFF_BMI2  // compilers do not turn the table mask into bzhi
VIO_INLINE unsigned low_bits(unsigned v, int n) { return _bzhi_u32(v, static_cast<unsigned>(n)); }
#else
VIO_INLINE unsigned low_bits(unsigned v, int n) { return v & kMaskBits[n]; }  // n <= 16
#endif
VIO_INLINE std::uint64_t clear_lowest(std::uint64_t v) { return v & (v - 1); }
#endif

// Raw bit writer for the encoder loop (see HuffBitWriter), with the shifts
// spelled out for the BMI2 build.
struct BitWriter {
  std::uint64_t bits;
  unsigned count;  // unsigned: count >> 3 needs no sign extension
  std::uint8_t* buffer;

  VIO_INLINE void append(std::uint64_t code, int size) {
    bits = shl64(bits, static_cast<unsigned>(size)) | code;
    count += static_cast<unsigned>(size);
  }
  VIO_INLINE void flush() {
    std::uint64_t be = HuffBitWriter::to_big_endian(shl64(bits, (0u - count) & 63));
    std::memcpy(buffer, &be, 8);
    buffer += count >> 3;
    count &= 7;
  }
  VIO_INLINE void put_bits(std::uint64_t code, int size) {
    append(code, size);
    flush();
  }
};

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
inline VIO_NO_STACK_CHECK std::uint8_t* encode_one_block(HuffBitState& st, std::uint8_t* buffer,
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

  BitWriter w{st.bits, static_cast<unsigned>(st.count), buffer};
  // DC difference (32-bit, as in the scalar encoder).
  int temp = block[0] - last_dc_val;
  int sign = temp >> 31;
  int nbits = bit_length(static_cast<unsigned>(temp ^ sign) - static_cast<unsigned>(sign));
  w.put_bits(static_cast<std::uint32_t>(dctbl.emit[nbits]) |
                 low_bits(static_cast<unsigned>(temp + sign), nbits),
             static_cast<int>(dctbl.emit[nbits] >> 32));

  // Each non-zero coefficient is found from the bitmap with its lowest bit
  // cleared, so the only dependency between iterations is that one
  // operation (shifting the bitmap by the run would chain tzcnt and shifts).
  unsigned k = 0;  // zig-zag position of the last coefficient coded
  auto next = [&]() VIO_LAMBDA_INLINE {  // appends the next coefficient (at most 31 bits)
    const unsigned pos = ctz64(index);  // coefficient pos + 1
    index = clear_lowest(index);
    unsigned run = pos - k;
    k = pos + 1;
    while (VIO_UNLIKELY(run > 15)) {
      w.put_bits(actbl.ehufco[0xf0], actbl.ehufsi[0xf0]);
      run -= 16;
    }
    const int code = t[pos];
    nbits = ac_bit_length(static_cast<unsigned>(code ^ (code >> 31)));
    const std::uint64_t e = actbl.emit[(run << 4) + static_cast<unsigned>(nbits)];
    w.append(static_cast<std::uint32_t>(e) | low_bits(static_cast<unsigned>(code), nbits),
             static_cast<int>(e >> 32));
  };
  // The output is written after every second coefficient, or after one
  // when (rarely) another one might not fit.
  while (index) {
    next();
    if (VIO_UNLIKELY(w.count > 32)) w.flush();
    if (!index) break;
    next();
    w.flush();
  }
  if (k != 63) w.append(actbl.ehufco[0], actbl.ehufsi[0]);
  w.flush();
  // Member-wise: a 16-byte copy would read back two narrower stores.
  st.bits = w.bits;
  st.count = static_cast<int>(w.count);
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

inline VIO_NO_STACK_CHECK std::uint64_t ac_first_prepare(const std::int16_t* block, const int* order, int Sl, int Al,
                                      std::uint16_t* values) {
  using namespace detail_huff;
  // Bands starting at coefficient 1 (the usual case) are loaded in
  // registers; lanes past the band only need their bitmap bits cleared.
  const bool from1 = order[0] == 1;
  alignas(16) std::int16_t c[64];
  T z[8];
  if (from1) {
    load_zigzag(block, z);
  } else {
    gather_band(block, order, Sl, c);
    for (int i = 0; i < 8; i++) z[i] = _mm_load_si128(reinterpret_cast<const T*>(c + i * 8));
  }
  const T zero = _mm_setzero_si128();
  const T shift = _mm_cvtsi32_si128(Al);
  T eq[8];
  for (int i = 0; i < 8; i++) {
    T x = z[i];
    T sign = _mm_srai_epi16(x, 15);
    T a = _mm_srl_epi16(_mm_sub_epi16(_mm_xor_si128(x, sign), sign), shift);
    _mm_storeu_si128(reinterpret_cast<T*>(values + i * 8), a);
    _mm_storeu_si128(reinterpret_cast<T*>(values + 64 + i * 8), _mm_xor_si128(a, sign));
    eq[i] = _mm_cmpeq_epi16(a, zero);
  }
  std::uint64_t bits = ~movemask64(eq);
  if (from1) bits &= (std::uint64_t(1) << Sl) - 1;  // Sl <= 63
  return bits;
}

inline VIO_NO_STACK_CHECK int ac_refine_prepare(const std::int16_t* block, const int* order, int Sl, int Al,
                             std::uint16_t* absvalues, std::uint64_t* zerobits,
                             std::uint64_t* signbits) {
  using namespace detail_huff;
  const bool from1 = order[0] == 1;  // see ac_first_prepare()
  alignas(16) std::int16_t c[64];
  T z[8];
  if (from1) {
    load_zigzag(block, z);
  } else {
    gather_band(block, order, Sl, c);
    for (int i = 0; i < 8; i++) z[i] = _mm_load_si128(reinterpret_cast<const T*>(c + i * 8));
  }
  const T zero = _mm_setzero_si128(), one = _mm_set1_epi16(1);
  const T shift = _mm_cvtsi32_si128(Al);
  T eq0[8], neg[8], eq1[8];
  for (int i = 0; i < 8; i++) {
    T x = z[i];
    T sign = _mm_srai_epi16(x, 15);
    T a = _mm_srl_epi16(_mm_sub_epi16(_mm_xor_si128(x, sign), sign), shift);
    _mm_storeu_si128(reinterpret_cast<T*>(absvalues + i * 8), a);
    eq0[i] = _mm_cmpeq_epi16(a, zero);
    neg[i] = sign;
    eq1[i] = _mm_cmpeq_epi16(a, one);
  }
  // Lanes past the band count as zero coefficients.
  const std::uint64_t band = from1 ? (std::uint64_t(1) << Sl) - 1 : ~std::uint64_t(0);
  *zerobits = ~movemask64(eq0) & band;
  *signbits = ~(movemask64(neg) & band);
  std::uint64_t eob = movemask64(eq1) & band;
  return eob ? 63 - clz64(eob) : 0;
}
