// vio::jpeg::simd::x86 - 128-bit integer vector operations (SSE2).
// Included inside namespace vio::jpeg::simd::sse2 in an SSE2 target region.
// v256.inc provides the same interface for AVX2, operating on each 128-bit
// lane independently, so that "lane-generic" kernels (x86/*.inc) compile to
// both widths with identical per-lane arithmetic.

struct V {
  using T = __m128i;
  static constexpr int kLanes = 1;  // 128-bit lanes per vector

  static VIO_INLINE T zero() { return _mm_setzero_si128(); }
  static VIO_INLINE T set1_8(int v) { return _mm_set1_epi8(static_cast<char>(v)); }
  static VIO_INLINE T set1_16(int v) { return _mm_set1_epi16(static_cast<short>(v)); }
  static VIO_INLINE T set1_32(int v) { return _mm_set1_epi32(v); }
  // (a, b) repeated: constant pairs for madd_16
  static VIO_INLINE T set2_16(int a, int b) {
    return _mm_set1_epi32(static_cast<int>((static_cast<unsigned>(b) << 16) |
                                           (static_cast<unsigned>(a) & 0xFFFF)));
  }

  // Contiguous 16 * kLanes bytes.
  static VIO_INLINE T loadu(const void* p) { return _mm_loadu_si128(static_cast<const T*>(p)); }
  static VIO_INLINE void storeu(void* p, T v) { _mm_storeu_si128(static_cast<T*>(p), v); }
  // Lane i <-> 16 bytes at p + i * stride.
  static VIO_INLINE T loadu_lanes(const void* p, std::ptrdiff_t) { return loadu(p); }
  static VIO_INLINE void storeu_lanes(void* p, std::ptrdiff_t, T v) { storeu(p, v); }

  static VIO_INLINE T add_8(T a, T b) { return _mm_add_epi8(a, b); }
  static VIO_INLINE T add_16(T a, T b) { return _mm_add_epi16(a, b); }
  static VIO_INLINE T add_32(T a, T b) { return _mm_add_epi32(a, b); }
  static VIO_INLINE T sub_16(T a, T b) { return _mm_sub_epi16(a, b); }
  static VIO_INLINE T sub_32(T a, T b) { return _mm_sub_epi32(a, b); }
  static VIO_INLINE T adds_16(T a, T b) { return _mm_adds_epi16(a, b); }
  static VIO_INLINE T mullo_16(T a, T b) { return _mm_mullo_epi16(a, b); }
  static VIO_INLINE T mulhi_16(T a, T b) { return _mm_mulhi_epi16(a, b); }
  static VIO_INLINE T madd_16(T a, T b) { return _mm_madd_epi16(a, b); }

  template <int N> static VIO_INLINE T slli_16(T a) { return _mm_slli_epi16(a, N); }
  template <int N> static VIO_INLINE T srli_16(T a) { return _mm_srli_epi16(a, N); }
  template <int N> static VIO_INLINE T srai_16(T a) { return _mm_srai_epi16(a, N); }
  template <int N> static VIO_INLINE T slli_32(T a) { return _mm_slli_epi32(a, N); }
  template <int N> static VIO_INLINE T srli_32(T a) { return _mm_srli_epi32(a, N); }
  template <int N> static VIO_INLINE T srai_32(T a) { return _mm_srai_epi32(a, N); }
  template <int I> static VIO_INLINE T shuffle_32(T a) { return _mm_shuffle_epi32(a, I); }
  template <int N> static VIO_INLINE T bslli(T a) { return _mm_slli_si128(a, N); }
  template <int N> static VIO_INLINE T bsrli(T a) { return _mm_srli_si128(a, N); }

  static VIO_INLINE T unpacklo_8(T a, T b) { return _mm_unpacklo_epi8(a, b); }
  static VIO_INLINE T unpackhi_8(T a, T b) { return _mm_unpackhi_epi8(a, b); }
  static VIO_INLINE T unpacklo_16(T a, T b) { return _mm_unpacklo_epi16(a, b); }
  static VIO_INLINE T unpackhi_16(T a, T b) { return _mm_unpackhi_epi16(a, b); }
  static VIO_INLINE T unpacklo_32(T a, T b) { return _mm_unpacklo_epi32(a, b); }
  static VIO_INLINE T unpackhi_32(T a, T b) { return _mm_unpackhi_epi32(a, b); }
  static VIO_INLINE T unpacklo_64(T a, T b) { return _mm_unpacklo_epi64(a, b); }
  static VIO_INLINE T unpackhi_64(T a, T b) { return _mm_unpackhi_epi64(a, b); }
  static VIO_INLINE T packs_32(T a, T b) { return _mm_packs_epi32(a, b); }
  static VIO_INLINE T packs_16(T a, T b) { return _mm_packs_epi16(a, b); }
  static VIO_INLINE T packus_16(T a, T b) { return _mm_packus_epi16(a, b); }

  static VIO_INLINE T and_(T a, T b) { return _mm_and_si128(a, b); }
  static VIO_INLINE T or_(T a, T b) { return _mm_or_si128(a, b); }
  static VIO_INLINE T xor_(T a, T b) { return _mm_xor_si128(a, b); }
  static VIO_INLINE T andnot(T a, T b) { return _mm_andnot_si128(a, b); }
  static VIO_INLINE T cmpeq_16(T a, T b) { return _mm_cmpeq_epi16(a, b); }
  static VIO_INLINE T cmpgt_16(T a, T b) { return _mm_cmpgt_epi16(a, b); }

  // Unsigned bytes (PNG filters); avg_u8 rounds up.
  static VIO_INLINE T sub_8(T a, T b) { return _mm_sub_epi8(a, b); }
  static VIO_INLINE T avg_u8(T a, T b) { return _mm_avg_epu8(a, b); }
  static VIO_INLINE T min_u8(T a, T b) { return _mm_min_epu8(a, b); }
  static VIO_INLINE T adds_u8(T a, T b) { return _mm_adds_epu8(a, b); }
  static VIO_INLINE T subs_u8(T a, T b) { return _mm_subs_epu8(a, b); }
  static VIO_INLINE T cmpeq_8(T a, T b) { return _mm_cmpeq_epi8(a, b); }
  static VIO_INLINE T loadl_32(const void* p) {
    int x;
    std::memcpy(&x, p, 4);
    return _mm_cvtsi32_si128(x);
  }

  static VIO_INLINE T mulhi_u16(T a, T b) { return _mm_mulhi_epu16(a, b); }
  // Low 8 / 4 bytes.
  static VIO_INLINE T loadl_64(const void* p) { return _mm_loadl_epi64(static_cast<const T*>(p)); }
  static VIO_INLINE void storel_64(void* p, T v) { _mm_storel_epi64(static_cast<T*>(p), v); }
  static VIO_INLINE void storel_32(void* p, T v) {
    int x = _mm_cvtsi128_si32(v);
    std::memcpy(p, &x, 4);
  }
  static VIO_INLINE bool all_zero(T v) {
    return _mm_movemask_epi8(_mm_cmpeq_epi8(v, _mm_setzero_si128())) == 0xFFFF;
  }

  // Endianness-neutral helpers (byte k = k-th byte in memory).
  // Bytes 0-7 / 8-15 zero-extended to 16 bits.
  static VIO_INLINE T widenlo_u8(T a) { return unpacklo_8(a, zero()); }
  static VIO_INLINE T widenhi_u8(T a) { return unpackhi_8(a, zero()); }
  // Even- / odd-numbered bytes zero-extended to 16 bits.
  static VIO_INLINE T even_u8(T a) { return and_(a, set1_16(0xFF)); }
  static VIO_INLINE T odd_u8(T a) { return srli_16<8>(a); }
  // Inverse of even_u8/odd_u8 for 16-bit values < 256.
  static VIO_INLINE T interleave_u8(T even, T odd) { return or_(even, slli_16<8>(odd)); }
  // 16-bit elements 0-3 / 4-7 as 32-bit values times 65536.
  static VIO_INLINE T lo16_shl16(T a) { return unpacklo_16(zero(), a); }
  static VIO_INLINE T hi16_shl16(T a) { return unpackhi_16(zero(), a); }
};
