// vio::jpeg::simd::x86 - 256-bit integer vector operations (AVX2), with the
// interface of v128.inc.  Every operation acts on the two 128-bit lanes
// independently (AVX2's unpack/pack/byte-shift instructions already do), so
// a lane-generic kernel processes two independent groups of data per vector.
// Included inside namespace vio::jpeg::simd::avx2 in an AVX2 target region.

struct V {
  using T = __m256i;
  static constexpr int kLanes = 2;

  static VIO_INLINE T zero() { return _mm256_setzero_si256(); }
  static VIO_INLINE T set1_8(int v) { return _mm256_set1_epi8(static_cast<char>(v)); }
  static VIO_INLINE T set1_16(int v) { return _mm256_set1_epi16(static_cast<short>(v)); }
  static VIO_INLINE T set1_32(int v) { return _mm256_set1_epi32(v); }
  static VIO_INLINE T set2_16(int a, int b) {
    return _mm256_set1_epi32(static_cast<int>((static_cast<unsigned>(b) << 16) |
                                              (static_cast<unsigned>(a) & 0xFFFF)));
  }

  static VIO_INLINE T loadu(const void* p) {
    return _mm256_loadu_si256(static_cast<const T*>(p));
  }
  static VIO_INLINE void storeu(void* p, T v) { _mm256_storeu_si256(static_cast<T*>(p), v); }
  static VIO_INLINE T loadu_lanes(const void* p, std::ptrdiff_t stride) {
    const char* c = static_cast<const char*>(p);
    return _mm256_inserti128_si256(
        _mm256_castsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(c))),
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(c + stride)), 1);
  }
  static VIO_INLINE void storeu_lanes(void* p, std::ptrdiff_t stride, T v) {
    char* c = static_cast<char*>(p);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(c), _mm256_castsi256_si128(v));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(c + stride), _mm256_extracti128_si256(v, 1));
  }

  static VIO_INLINE T add_8(T a, T b) { return _mm256_add_epi8(a, b); }
  static VIO_INLINE T add_16(T a, T b) { return _mm256_add_epi16(a, b); }
  static VIO_INLINE T add_32(T a, T b) { return _mm256_add_epi32(a, b); }
  static VIO_INLINE T sub_16(T a, T b) { return _mm256_sub_epi16(a, b); }
  static VIO_INLINE T sub_32(T a, T b) { return _mm256_sub_epi32(a, b); }
  static VIO_INLINE T adds_16(T a, T b) { return _mm256_adds_epi16(a, b); }
  static VIO_INLINE T mullo_16(T a, T b) { return _mm256_mullo_epi16(a, b); }
  static VIO_INLINE T mulhi_16(T a, T b) { return _mm256_mulhi_epi16(a, b); }
  static VIO_INLINE T madd_16(T a, T b) { return _mm256_madd_epi16(a, b); }

  template <int N> static VIO_INLINE T slli_16(T a) { return _mm256_slli_epi16(a, N); }
  template <int N> static VIO_INLINE T srli_16(T a) { return _mm256_srli_epi16(a, N); }
  template <int N> static VIO_INLINE T srai_16(T a) { return _mm256_srai_epi16(a, N); }
  template <int N> static VIO_INLINE T slli_32(T a) { return _mm256_slli_epi32(a, N); }
  template <int N> static VIO_INLINE T srli_32(T a) { return _mm256_srli_epi32(a, N); }
  template <int N> static VIO_INLINE T srai_32(T a) { return _mm256_srai_epi32(a, N); }
  template <int I> static VIO_INLINE T shuffle_32(T a) { return _mm256_shuffle_epi32(a, I); }
  template <int N> static VIO_INLINE T bslli(T a) { return _mm256_slli_si256(a, N); }
  template <int N> static VIO_INLINE T bsrli(T a) { return _mm256_srli_si256(a, N); }

  static VIO_INLINE T unpacklo_8(T a, T b) { return _mm256_unpacklo_epi8(a, b); }
  static VIO_INLINE T unpackhi_8(T a, T b) { return _mm256_unpackhi_epi8(a, b); }
  static VIO_INLINE T unpacklo_16(T a, T b) { return _mm256_unpacklo_epi16(a, b); }
  static VIO_INLINE T unpackhi_16(T a, T b) { return _mm256_unpackhi_epi16(a, b); }
  static VIO_INLINE T unpacklo_32(T a, T b) { return _mm256_unpacklo_epi32(a, b); }
  static VIO_INLINE T unpackhi_32(T a, T b) { return _mm256_unpackhi_epi32(a, b); }
  static VIO_INLINE T unpacklo_64(T a, T b) { return _mm256_unpacklo_epi64(a, b); }
  static VIO_INLINE T unpackhi_64(T a, T b) { return _mm256_unpackhi_epi64(a, b); }
  static VIO_INLINE T packs_32(T a, T b) { return _mm256_packs_epi32(a, b); }
  static VIO_INLINE T packs_16(T a, T b) { return _mm256_packs_epi16(a, b); }
  static VIO_INLINE T packus_16(T a, T b) { return _mm256_packus_epi16(a, b); }

  // Byte shuffles within each 128-bit lane (index bit 7: zero).
  static constexpr bool kShuffle8 = true;
  static VIO_INLINE T shuffle_8(T a, T idx) { return _mm256_shuffle_epi8(a, idx); }
  static VIO_INLINE T load_bcast16(const void* p) {  // the same 16 bytes in each lane
    return _mm256_broadcastsi128_si256(_mm_loadu_si128(static_cast<const __m128i*>(p)));
  }
  template <int N> static VIO_INLINE T alignr(T hi, T lo) { return _mm256_alignr_epi8(hi, lo, N); }
  static VIO_INLINE T and_(T a, T b) { return _mm256_and_si256(a, b); }
  static VIO_INLINE T or_(T a, T b) { return _mm256_or_si256(a, b); }
  static VIO_INLINE T xor_(T a, T b) { return _mm256_xor_si256(a, b); }
  static VIO_INLINE T andnot(T a, T b) { return _mm256_andnot_si256(a, b); }
  static VIO_INLINE T cmpeq_16(T a, T b) { return _mm256_cmpeq_epi16(a, b); }
  static VIO_INLINE T cmpgt_16(T a, T b) { return _mm256_cmpgt_epi16(a, b); }

  static VIO_INLINE T mulhi_u16(T a, T b) { return _mm256_mulhi_epu16(a, b); }

#include "../generic/vector_le.inc"
};
