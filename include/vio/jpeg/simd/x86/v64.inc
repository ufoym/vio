// vio::jpeg::simd::mmx - 128-bit vector operations with the interface of
// v128.inc, emulated with pairs of 64-bit MMX registers (lo = bytes 0-7,
// hi = bytes 8-15), for 32-bit x86 CPUs without SSE2.  Kernels using it
// must end with _mm_empty() (see mmx.hpp).  Included inside namespace
// vio::jpeg::simd::mmx.

struct V {
  using M = __m64;
  struct T {
    M lo, hi;
  };
  static constexpr int kLanes = 1;

  template <class F> static VIO_INLINE T map(T a, T b, F f) { return {f(a.lo, b.lo), f(a.hi, b.hi)}; }
  static VIO_INLINE std::uint64_t q(M v) {
    std::uint64_t x;
    std::memcpy(&x, &v, 8);
    return x;
  }
  static VIO_INLINE M from_q(std::uint64_t x) {
    M v;
    std::memcpy(&v, &x, 8);
    return v;
  }

  static VIO_INLINE T zero() { return {_mm_setzero_si64(), _mm_setzero_si64()}; }
  static VIO_INLINE T set1_8(int v) {
    M h = _mm_set1_pi8(static_cast<char>(v));
    return {h, h};
  }
  static VIO_INLINE T set1_16(int v) {
    M h = _mm_set1_pi16(static_cast<short>(v));
    return {h, h};
  }
  static VIO_INLINE T set1_32(int v) {
    M h = _mm_set1_pi32(v);
    return {h, h};
  }
  static VIO_INLINE T set2_16(int a, int b) {
    M h = _mm_set_pi16(static_cast<short>(b), static_cast<short>(a), static_cast<short>(b),
                       static_cast<short>(a));
    return {h, h};
  }

  static VIO_INLINE T loadu(const void* p) {
    T v;
    std::memcpy(&v.lo, p, 8);
    std::memcpy(&v.hi, static_cast<const char*>(p) + 8, 8);
    return v;
  }
  static VIO_INLINE void storeu(void* p, T v) {
    std::memcpy(p, &v.lo, 8);
    std::memcpy(static_cast<char*>(p) + 8, &v.hi, 8);
  }
  static VIO_INLINE T loadu_lanes(const void* p, std::ptrdiff_t) { return loadu(p); }
  static VIO_INLINE void storeu_lanes(void* p, std::ptrdiff_t, T v) { storeu(p, v); }

  static VIO_INLINE T add_8(T a, T b) { return map(a, b, [](M x, M y) { return _mm_add_pi8(x, y); }); }
  static VIO_INLINE T add_16(T a, T b) { return map(a, b, [](M x, M y) { return _mm_add_pi16(x, y); }); }
  static VIO_INLINE T add_32(T a, T b) { return map(a, b, [](M x, M y) { return _mm_add_pi32(x, y); }); }
  static VIO_INLINE T sub_16(T a, T b) { return map(a, b, [](M x, M y) { return _mm_sub_pi16(x, y); }); }
  static VIO_INLINE T sub_32(T a, T b) { return map(a, b, [](M x, M y) { return _mm_sub_pi32(x, y); }); }
  static VIO_INLINE T adds_16(T a, T b) { return map(a, b, [](M x, M y) { return _mm_adds_pi16(x, y); }); }
  static VIO_INLINE T mullo_16(T a, T b) { return map(a, b, [](M x, M y) { return _mm_mullo_pi16(x, y); }); }
  static VIO_INLINE T mulhi_16(T a, T b) { return map(a, b, [](M x, M y) { return _mm_mulhi_pi16(x, y); }); }
  // Unsigned high product from the signed one:
  // hi_u(a, b) = hi_s(a, b) + (a < 0 ? b : 0) + (b < 0 ? a : 0)  (mod 2^16)
  static VIO_INLINE T mulhi_u16(T a, T b) {
    return map(a, b, [](M x, M y) {
      M h = _mm_mulhi_pi16(x, y);
      h = _mm_add_pi16(h, _mm_and_si64(_mm_srai_pi16(x, 15), y));
      return _mm_add_pi16(h, _mm_and_si64(_mm_srai_pi16(y, 15), x));
    });
  }
  static VIO_INLINE T madd_16(T a, T b) { return map(a, b, [](M x, M y) { return _mm_madd_pi16(x, y); }); }

  template <int N> static VIO_INLINE T slli_16(T a) { return {_mm_slli_pi16(a.lo, N), _mm_slli_pi16(a.hi, N)}; }
  template <int N> static VIO_INLINE T srli_16(T a) { return {_mm_srli_pi16(a.lo, N), _mm_srli_pi16(a.hi, N)}; }
  template <int N> static VIO_INLINE T srai_16(T a) { return {_mm_srai_pi16(a.lo, N), _mm_srai_pi16(a.hi, N)}; }
  template <int N> static VIO_INLINE T slli_32(T a) { return {_mm_slli_pi32(a.lo, N), _mm_slli_pi32(a.hi, N)}; }
  template <int N> static VIO_INLINE T srli_32(T a) { return {_mm_srli_pi32(a.lo, N), _mm_srli_pi32(a.hi, N)}; }
  template <int N> static VIO_INLINE T srai_32(T a) { return {_mm_srai_pi32(a.lo, N), _mm_srai_pi32(a.hi, N)}; }
  template <int I> static VIO_INLINE T shuffle_32(T a) {
    std::uint64_t lo = q(a.lo), hi = q(a.hi);
    auto e = [&](int k) -> std::uint64_t {
      return static_cast<std::uint32_t>(k < 2 ? lo >> (32 * k) : hi >> (32 * (k - 2)));
    };
    return {from_q(e(I & 3) | (e((I >> 2) & 3) << 32)), from_q(e((I >> 4) & 3) | (e((I >> 6) & 3) << 32))};
  }
  template <int N> static VIO_INLINE T bslli(T a) {  // byte i <- byte i - N
    if constexpr (N == 0) return a;
    else if constexpr (N < 8) return {_mm_slli_si64(a.lo, 8 * N), _mm_or_si64(_mm_slli_si64(a.hi, 8 * N), _mm_srli_si64(a.lo, 64 - 8 * N))};
    else if constexpr (N == 8) return {_mm_setzero_si64(), a.lo};
    else return {_mm_setzero_si64(), _mm_slli_si64(a.lo, 8 * (N - 8))};
  }
  template <int N> static VIO_INLINE T bsrli(T a) {  // byte i <- byte i + N
    if constexpr (N == 0) return a;
    else if constexpr (N < 8) return {_mm_or_si64(_mm_srli_si64(a.lo, 8 * N), _mm_slli_si64(a.hi, 64 - 8 * N)), _mm_srli_si64(a.hi, 8 * N)};
    else if constexpr (N == 8) return {a.hi, _mm_setzero_si64()};
    else return {_mm_srli_si64(a.hi, 8 * (N - 8)), _mm_setzero_si64()};
  }

  static VIO_INLINE T unpacklo_8(T a, T b) { return {_mm_unpacklo_pi8(a.lo, b.lo), _mm_unpackhi_pi8(a.lo, b.lo)}; }
  static VIO_INLINE T unpackhi_8(T a, T b) { return {_mm_unpacklo_pi8(a.hi, b.hi), _mm_unpackhi_pi8(a.hi, b.hi)}; }
  static VIO_INLINE T unpacklo_16(T a, T b) { return {_mm_unpacklo_pi16(a.lo, b.lo), _mm_unpackhi_pi16(a.lo, b.lo)}; }
  static VIO_INLINE T unpackhi_16(T a, T b) { return {_mm_unpacklo_pi16(a.hi, b.hi), _mm_unpackhi_pi16(a.hi, b.hi)}; }
  static VIO_INLINE T unpacklo_32(T a, T b) { return {_mm_unpacklo_pi32(a.lo, b.lo), _mm_unpackhi_pi32(a.lo, b.lo)}; }
  static VIO_INLINE T unpackhi_32(T a, T b) { return {_mm_unpacklo_pi32(a.hi, b.hi), _mm_unpackhi_pi32(a.hi, b.hi)}; }
  static VIO_INLINE T unpacklo_64(T a, T b) { return {a.lo, b.lo}; }
  static VIO_INLINE T unpackhi_64(T a, T b) { return {a.hi, b.hi}; }
  static VIO_INLINE T packs_32(T a, T b) { return {_mm_packs_pi32(a.lo, a.hi), _mm_packs_pi32(b.lo, b.hi)}; }
  static VIO_INLINE T packs_16(T a, T b) { return {_mm_packs_pi16(a.lo, a.hi), _mm_packs_pi16(b.lo, b.hi)}; }
  static VIO_INLINE T packus_16(T a, T b) { return {_mm_packs_pu16(a.lo, a.hi), _mm_packs_pu16(b.lo, b.hi)}; }

  static VIO_INLINE T and_(T a, T b) { return map(a, b, [](M x, M y) { return _mm_and_si64(x, y); }); }
  static VIO_INLINE T or_(T a, T b) { return map(a, b, [](M x, M y) { return _mm_or_si64(x, y); }); }
  static VIO_INLINE T xor_(T a, T b) { return map(a, b, [](M x, M y) { return _mm_xor_si64(x, y); }); }
  static VIO_INLINE T andnot(T a, T b) { return map(a, b, [](M x, M y) { return _mm_andnot_si64(x, y); }); }
  static VIO_INLINE T cmpeq_16(T a, T b) { return map(a, b, [](M x, M y) { return _mm_cmpeq_pi16(x, y); }); }
  static VIO_INLINE T cmpgt_16(T a, T b) { return map(a, b, [](M x, M y) { return _mm_cmpgt_pi16(x, y); }); }

  static VIO_INLINE T loadl_64(const void* p) {
    T v = zero();
    std::memcpy(&v.lo, p, 8);
    return v;
  }
  static VIO_INLINE void storel_64(void* p, T v) { std::memcpy(p, &v.lo, 8); }
  static VIO_INLINE void storel_32(void* p, T v) { std::memcpy(p, &v.lo, 4); }
  static VIO_INLINE bool all_zero(T v) { return (q(v.lo) | q(v.hi)) == 0; }

  // Endianness-neutral helpers (little-endian forms; see v128.inc).
  static VIO_INLINE T widenlo_u8(T a) { return unpacklo_8(a, zero()); }
  static VIO_INLINE T widenhi_u8(T a) { return unpackhi_8(a, zero()); }
  static VIO_INLINE T even_u8(T a) { return and_(a, set1_16(0xFF)); }
  static VIO_INLINE T odd_u8(T a) { return srli_16<8>(a); }
  static VIO_INLINE T interleave_u8(T even, T odd) { return or_(even, slli_16<8>(odd)); }
  static VIO_INLINE T lo16_shl16(T a) { return unpacklo_16(zero(), a); }
  static VIO_INLINE T hi16_shl16(T a) { return unpackhi_16(zero(), a); }
};
