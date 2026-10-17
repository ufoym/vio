// vio::jpeg::simd - 128-bit vector operations with the interface of
// x86/v128.inc, written with GCC/Clang fixed-length vector extensions
// (vector_size, __builtin_shufflevector, __builtin_convertvector), which the
// compiler lowers to the target's vector instructions (e.g. RVV).  Vector
// element k is the k-th element in memory on any byte order.

struct V {
  using U8 = std::uint8_t __attribute__((vector_size(16)));
  using I8 = std::int8_t __attribute__((vector_size(16)));
  using U16 = std::uint16_t __attribute__((vector_size(16)));
  using I16 = std::int16_t __attribute__((vector_size(16)));
  using U32 = std::uint32_t __attribute__((vector_size(16)));
  using I32 = std::int32_t __attribute__((vector_size(16)));
  using U64 = std::uint64_t __attribute__((vector_size(16)));
  using I16x16 = std::int16_t __attribute__((vector_size(32)));
  using I32x8 = std::int32_t __attribute__((vector_size(32)));
  using U32x8 = std::uint32_t __attribute__((vector_size(32)));
  using T = U8;
  static constexpr int kLanes = 1;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  static constexpr bool kBigEndian = true;
#else
  static constexpr bool kBigEndian = false;
#endif

  static VIO_INLINE T zero() { return T{}; }
  static VIO_INLINE T set1_8(int v) { return (T)(I8{} + static_cast<std::int8_t>(v)); }
  static VIO_INLINE T set1_16(int v) { return (T)(I16{} + static_cast<std::int16_t>(v)); }
  static VIO_INLINE T set1_32(int v) { return (T)(I32{} + v); }
  static VIO_INLINE T set2_16(int a, int b) {
    const std::int16_t x = static_cast<std::int16_t>(a), y = static_cast<std::int16_t>(b);
    return (T)(I16{x, y, x, y, x, y, x, y});
  }

  static VIO_INLINE T loadu(const void* p) {
    T v;
    std::memcpy(&v, p, 16);
    return v;
  }
  static VIO_INLINE void storeu(void* p, T v) { std::memcpy(p, &v, 16); }
  static VIO_INLINE T loadu_lanes(const void* p, std::ptrdiff_t) { return loadu(p); }
  static VIO_INLINE void storeu_lanes(void* p, std::ptrdiff_t, T v) { storeu(p, v); }

  static VIO_INLINE T add_8(T a, T b) { return a + b; }
  static VIO_INLINE T add_16(T a, T b) { return (T)((U16)a + (U16)b); }
  static VIO_INLINE T add_32(T a, T b) { return (T)((U32)a + (U32)b); }
  static VIO_INLINE T sub_16(T a, T b) { return (T)((U16)a - (U16)b); }
  static VIO_INLINE T sub_32(T a, T b) { return (T)((U32)a - (U32)b); }
  static VIO_INLINE I32x8 widen(T a) { return __builtin_convertvector((I16)a, I32x8); }
  static VIO_INLINE I16 narrow_sat(I32x8 x) {
    const I32x8 lo = I32x8{} - 32768, hi = I32x8{} + 32767;
    x = x < lo ? lo : x;
    x = x > hi ? hi : x;
    return __builtin_convertvector(x, I16);
  }
  static VIO_INLINE T adds_16(T a, T b) { return (T)narrow_sat(widen(a) + widen(b)); }
  static VIO_INLINE T mullo_16(T a, T b) { return (T)((U16)a * (U16)b); }
  static VIO_INLINE T mulhi_16(T a, T b) {
    U32x8 p = (U32x8)widen(a) * (U32x8)widen(b);  // two's complement product
    return (T)__builtin_convertvector((I32x8)p >> 16, I16);
  }
  static VIO_INLINE T mulhi_u16(T a, T b) {
    U32x8 p = __builtin_convertvector((U16)a, U32x8) * __builtin_convertvector((U16)b, U32x8);
    return (T)__builtin_convertvector(p >> 16, U16);
  }
  static VIO_INLINE T madd_16(T a, T b) {
    U32x8 p = (U32x8)widen(a) * (U32x8)widen(b);
    return (T)(__builtin_shufflevector(p, p, 0, 2, 4, 6) + __builtin_shufflevector(p, p, 1, 3, 5, 7));
  }

  template <int N> static VIO_INLINE T slli_16(T a) { return (T)((U16)a << N); }
  template <int N> static VIO_INLINE T srli_16(T a) { return (T)((U16)a >> N); }
  template <int N> static VIO_INLINE T srai_16(T a) { return (T)((I16)a >> N); }
  template <int N> static VIO_INLINE T slli_32(T a) { return (T)((U32)a << N); }
  template <int N> static VIO_INLINE T srli_32(T a) { return (T)((U32)a >> N); }
  template <int N> static VIO_INLINE T srai_32(T a) { return (T)((I32)a >> N); }

  template <int I> static VIO_INLINE T shuffle_32(T a) {
    return (T)__builtin_shufflevector((U32)a, (U32)a, I & 3, (I >> 2) & 3, (I >> 4) & 3, (I >> 6) & 3);
  }
  template <int N, std::size_t... K>
  static VIO_INLINE T bslli_(T a, std::index_sequence<K...>) {  // byte k <- byte k - N
    return __builtin_shufflevector(zero(), a, (K < N ? 0 : 16 + K - N)...);
  }
  template <int N, std::size_t... K>
  static VIO_INLINE T bsrli_(T a, std::index_sequence<K...>) {  // byte k <- byte k + N
    return __builtin_shufflevector(a, zero(), (K + N)...);
  }
  template <int N> static VIO_INLINE T bslli(T a) { return bslli_<N>(a, std::make_index_sequence<16>()); }
  template <int N> static VIO_INLINE T bsrli(T a) { return bsrli_<N>(a, std::make_index_sequence<16>()); }

  static VIO_INLINE T unpacklo_8(T a, T b) {
    return __builtin_shufflevector(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
  }
  static VIO_INLINE T unpackhi_8(T a, T b) {
    return __builtin_shufflevector(a, b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
  }
  static VIO_INLINE T unpacklo_16(T a, T b) {
    return (T)__builtin_shufflevector((U16)a, (U16)b, 0, 8, 1, 9, 2, 10, 3, 11);
  }
  static VIO_INLINE T unpackhi_16(T a, T b) {
    return (T)__builtin_shufflevector((U16)a, (U16)b, 4, 12, 5, 13, 6, 14, 7, 15);
  }
  static VIO_INLINE T unpacklo_32(T a, T b) {
    return (T)__builtin_shufflevector((U32)a, (U32)b, 0, 4, 1, 5);
  }
  static VIO_INLINE T unpackhi_32(T a, T b) {
    return (T)__builtin_shufflevector((U32)a, (U32)b, 2, 6, 3, 7);
  }
  static VIO_INLINE T unpacklo_64(T a, T b) { return (T)__builtin_shufflevector((U64)a, (U64)b, 0, 2); }
  static VIO_INLINE T unpackhi_64(T a, T b) { return (T)__builtin_shufflevector((U64)a, (U64)b, 1, 3); }
  static VIO_INLINE T packs_32(T a, T b) {
    I32x8 x = __builtin_shufflevector((I32)a, (I32)b, 0, 1, 2, 3, 4, 5, 6, 7);
    return (T)narrow_sat(x);
  }
  static VIO_INLINE I16x16 cat16(T a, T b) {
    return __builtin_shufflevector((I16)a, (I16)b, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
  }
  static VIO_INLINE T packs_16(T a, T b) {
    I16x16 x = cat16(a, b);
    const I16x16 lo = I16x16{} - 128, hi = I16x16{} + 127;
    x = x < lo ? lo : x;
    x = x > hi ? hi : x;
    return (T)__builtin_convertvector(x, I8);
  }
  static VIO_INLINE T packus_16(T a, T b) {
    I16x16 x = cat16(a, b);
    const I16x16 lo = I16x16{}, hi = I16x16{} + 255;
    x = x < lo ? lo : x;
    x = x > hi ? hi : x;
    return __builtin_convertvector(x, U8);
  }

  static VIO_INLINE T and_(T a, T b) { return a & b; }
  static VIO_INLINE T or_(T a, T b) { return a | b; }
  static VIO_INLINE T xor_(T a, T b) { return a ^ b; }
  static VIO_INLINE T andnot(T a, T b) { return ~a & b; }
  static VIO_INLINE T cmpeq_16(T a, T b) { return (T)((I16)a == (I16)b); }
  static VIO_INLINE T cmpgt_16(T a, T b) { return (T)((I16)a > (I16)b); }

  static VIO_INLINE T loadl_64(const void* p) {
    T v{};
    std::memcpy(&v, p, 8);
    return v;
  }
  static VIO_INLINE void storel_64(void* p, T v) { std::memcpy(p, &v, 8); }
  static VIO_INLINE void storel_32(void* p, T v) { std::memcpy(p, &v, 4); }
  static VIO_INLINE bool all_zero(T v) {
    U64 q = (U64)v;
    return (q[0] | q[1]) == 0;
  }

  // Endianness-neutral helpers (see v128.inc).
  static VIO_INLINE T widenlo_u8(T a) { return kBigEndian ? unpacklo_8(zero(), a) : unpacklo_8(a, zero()); }
  static VIO_INLINE T widenhi_u8(T a) { return kBigEndian ? unpackhi_8(zero(), a) : unpackhi_8(a, zero()); }
  static VIO_INLINE T even_u8(T a) { return kBigEndian ? srli_16<8>(a) : and_(a, set1_16(0xFF)); }
  static VIO_INLINE T odd_u8(T a) { return kBigEndian ? and_(a, set1_16(0xFF)) : srli_16<8>(a); }
  static VIO_INLINE T interleave_u8(T even, T odd) {
    return kBigEndian ? or_(slli_16<8>(even), odd) : or_(even, slli_16<8>(odd));
  }
  static VIO_INLINE T lo16_shl16(T a) { return kBigEndian ? unpacklo_16(a, zero()) : unpacklo_16(zero(), a); }
  static VIO_INLINE T hi16_shl16(T a) { return kBigEndian ? unpackhi_16(a, zero()) : unpackhi_16(zero(), a); }
};
