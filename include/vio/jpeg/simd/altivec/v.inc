// vio::jpeg::simd::altivec - 128-bit vector operations with the interface of
// x86/v128.inc, implemented with AltiVec/VSX intrinsics (big- and
// little-endian).  All operations use memory element order: element k of a
// vector of N-byte elements is the k-th N-byte group in memory, as with
// SSE2.  Included inside namespace vio::jpeg::simd::altivec.

struct V {
  using U8 = __vector unsigned char;
  using S8 = __vector signed char;
  using U16 = __vector unsigned short;
  using S16 = __vector signed short;
  using U32 = __vector unsigned int;
  using S32 = __vector signed int;
  using T = U8;
  static constexpr int kLanes = 1;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  static constexpr bool kBigEndian = true;
#else
  static constexpr bool kBigEndian = false;
#endif

  static VIO_INLINE T zero() { return vec_splats(static_cast<unsigned char>(0)); }
  static VIO_INLINE T set1_8(int v) { return (T)vec_splats(static_cast<signed char>(v)); }
  static VIO_INLINE T set1_16(int v) { return (T)vec_splats(static_cast<short>(v)); }
  static VIO_INLINE T set1_32(int v) { return (T)vec_splats(v); }
  static VIO_INLINE T set2_16(int a, int b) {
    const short x = static_cast<short>(a), y = static_cast<short>(b);
    return (T)(S16){x, y, x, y, x, y, x, y};
  }

  static VIO_INLINE T loadu(const void* p) {
#if defined(__VSX__)
    return vec_xl(0, static_cast<const unsigned char*>(p));
#else
    const unsigned char* c = static_cast<const unsigned char*>(p);
    return vec_perm(vec_ld(0, c), vec_ld(15, c), vec_lvsl(0, c));
#endif
  }
  static VIO_INLINE void storeu(void* p, T v) {
#if defined(__VSX__)
    vec_xst(v, 0, static_cast<unsigned char*>(p));
#else
    alignas(16) unsigned char t[16];
    vec_st(v, 0, t);
    std::memcpy(p, t, 16);
#endif
  }
  static VIO_INLINE T loadu_lanes(const void* p, std::ptrdiff_t) { return loadu(p); }
  static VIO_INLINE void storeu_lanes(void* p, std::ptrdiff_t, T v) { storeu(p, v); }

  static VIO_INLINE T add_8(T a, T b) { return vec_add(a, b); }
  static VIO_INLINE T add_16(T a, T b) { return (T)vec_add((U16)a, (U16)b); }
  static VIO_INLINE T add_32(T a, T b) { return (T)vec_add((U32)a, (U32)b); }
  static VIO_INLINE T sub_16(T a, T b) { return (T)vec_sub((U16)a, (U16)b); }
  static VIO_INLINE T sub_32(T a, T b) { return (T)vec_sub((U32)a, (U32)b); }
  static VIO_INLINE T adds_16(T a, T b) { return (T)vec_adds((S16)a, (S16)b); }
  static VIO_INLINE T mullo_16(T a, T b) {
    return (T)vec_mladd((S16)a, (S16)b, vec_splats(static_cast<short>(0)));
  }
  // High halves of the 32-bit products, from even/odd products interleaved
  // back into element order.
  static VIO_INLINE T mulhi_16(T a, T b) {
    S32 e = vec_mule((S16)a, (S16)b), o = vec_mulo((S16)a, (S16)b);
    const U32 s16 = vec_splats(16u);
    return (T)vec_packs(vec_sra(vec_mergeh(e, o), s16), vec_sra(vec_mergel(e, o), s16));
  }
  static VIO_INLINE T mulhi_u16(T a, T b) {
    U32 e = vec_mule((U16)a, (U16)b), o = vec_mulo((U16)a, (U16)b);
    const U32 s16 = vec_splats(16u);
    return (T)vec_pack(vec_sr(vec_mergeh(e, o), s16), vec_sr(vec_mergel(e, o), s16));
  }
  static VIO_INLINE T madd_16(T a, T b) {
    return (T)vec_msums((S16)a, (S16)b, vec_splats(0));
  }

  template <int N> static VIO_INLINE T slli_16(T a) {
    return (T)vec_sl((U16)a, vec_splats(static_cast<unsigned short>(N)));
  }
  template <int N> static VIO_INLINE T srli_16(T a) {
    return (T)vec_sr((U16)a, vec_splats(static_cast<unsigned short>(N)));
  }
  template <int N> static VIO_INLINE T srai_16(T a) {
    return (T)vec_sra((S16)a, vec_splats(static_cast<unsigned short>(N)));
  }
  template <int N> static VIO_INLINE T slli_32(T a) {
    return (T)vec_sl((U32)a, vec_splats(static_cast<unsigned>(N)));
  }
  template <int N> static VIO_INLINE T srli_32(T a) {
    return (T)vec_sr((U32)a, vec_splats(static_cast<unsigned>(N)));
  }
  template <int N> static VIO_INLINE T srai_32(T a) {
    return (T)vec_sra((S32)a, vec_splats(static_cast<unsigned>(N)));
  }

  // Byte permutations given as memory-order index tables (vec_perm indexes
  // the 32-byte concatenation of its operands in memory order).
  template <class F, std::size_t... I>
  static VIO_INLINE T perm_index(F f, std::index_sequence<I...>) {
    return (T){static_cast<unsigned char>(f(static_cast<int>(I)))...};
  }
  template <int I> static VIO_INLINE T shuffle_32(T a) {
    constexpr auto f = [](int i) { return 4 * ((I >> (2 * (i / 4))) & 3) + (i & 3); };
    return vec_perm(a, a, perm_index(f, std::make_index_sequence<16>()));
  }
  template <int N> static VIO_INLINE T bslli(T a) {  // byte i <- byte i - N
    constexpr auto f = [](int i) { return i < N ? 0 : 16 + i - N; };
    return vec_perm(zero(), a, perm_index(f, std::make_index_sequence<16>()));
  }
  template <int N> static VIO_INLINE T bsrli(T a) {  // byte i <- byte i + N
    constexpr auto f = [](int i) { return i + N; };
    return vec_perm(a, zero(), perm_index(f, std::make_index_sequence<16>()));
  }

  static VIO_INLINE T unpacklo_8(T a, T b) { return vec_mergeh(a, b); }
  static VIO_INLINE T unpackhi_8(T a, T b) { return vec_mergel(a, b); }
  static VIO_INLINE T unpacklo_16(T a, T b) { return (T)vec_mergeh((U16)a, (U16)b); }
  static VIO_INLINE T unpackhi_16(T a, T b) { return (T)vec_mergel((U16)a, (U16)b); }
  static VIO_INLINE T unpacklo_32(T a, T b) { return (T)vec_mergeh((U32)a, (U32)b); }
  static VIO_INLINE T unpackhi_32(T a, T b) { return (T)vec_mergel((U32)a, (U32)b); }
  static VIO_INLINE T unpacklo_64(T a, T b) {
    constexpr auto f = [](int i) { return i < 8 ? i : 8 + i; };
    return vec_perm(a, b, perm_index(f, std::make_index_sequence<16>()));
  }
  static VIO_INLINE T unpackhi_64(T a, T b) {
    constexpr auto f = [](int i) { return i < 8 ? 8 + i : 16 + i; };
    return vec_perm(a, b, perm_index(f, std::make_index_sequence<16>()));
  }
  static VIO_INLINE T packs_32(T a, T b) { return (T)vec_packs((S32)a, (S32)b); }
  static VIO_INLINE T packs_16(T a, T b) { return (T)vec_packs((S16)a, (S16)b); }
  static VIO_INLINE T packus_16(T a, T b) { return vec_packsu((S16)a, (S16)b); }

  static VIO_INLINE T and_(T a, T b) { return vec_and(a, b); }
  static VIO_INLINE T or_(T a, T b) { return vec_or(a, b); }
  static VIO_INLINE T xor_(T a, T b) { return vec_xor(a, b); }
  static VIO_INLINE T andnot(T a, T b) { return vec_andc(b, a); }
  static VIO_INLINE T cmpeq_16(T a, T b) { return (T)vec_cmpeq((S16)a, (S16)b); }
  static VIO_INLINE T cmpgt_16(T a, T b) { return (T)vec_cmpgt((S16)a, (S16)b); }

  static VIO_INLINE T loadl_64(const void* p) {
    alignas(16) unsigned char t[16] = {};
    std::memcpy(t, p, 8);
    return vec_ld(0, t);
  }
  static VIO_INLINE void storel_64(void* p, T v) {
    alignas(16) unsigned char t[16];
    vec_st(v, 0, t);
    std::memcpy(p, t, 8);
  }
  static VIO_INLINE void storel_32(void* p, T v) {
    alignas(16) unsigned char t[16];
    vec_st(v, 0, t);
    std::memcpy(p, t, 4);
  }
  static VIO_INLINE bool all_zero(T v) { return vec_all_eq(v, zero()); }

  // Endianness-neutral helpers (see v128.inc).
  static VIO_INLINE T widenlo_u8(T a) { return kBigEndian ? vec_mergeh(zero(), a) : vec_mergeh(a, zero()); }
  static VIO_INLINE T widenhi_u8(T a) { return kBigEndian ? vec_mergel(zero(), a) : vec_mergel(a, zero()); }
  static VIO_INLINE T even_u8(T a) { return kBigEndian ? srli_16<8>(a) : and_(a, set1_16(0xFF)); }
  static VIO_INLINE T odd_u8(T a) { return kBigEndian ? and_(a, set1_16(0xFF)) : srli_16<8>(a); }
  static VIO_INLINE T interleave_u8(T even, T odd) {
    return kBigEndian ? or_(slli_16<8>(even), odd) : or_(even, slli_16<8>(odd));
  }
  static VIO_INLINE T lo16_shl16(T a) { return kBigEndian ? unpacklo_16(a, zero()) : unpacklo_16(zero(), a); }
  static VIO_INLINE T hi16_shl16(T a) { return kBigEndian ? unpackhi_16(a, zero()) : unpackhi_16(zero(), a); }
};
