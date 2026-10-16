// vio::jpeg::simd::mmi - 128-bit vector operations with the interface of
// x86/v128.inc, emulated with pairs of 64-bit Loongson MMI registers (GCC's
// loongson-mmiintrin.h).  `lo` holds bytes 0-7 and `hi` bytes 8-15 (Loongson
// is little-endian).  Included inside namespace vio::jpeg::simd::mmi.

struct V {
  using U8 = uint8x8_t;
  using S16 = int16x4_t;
  using U16 = uint16x4_t;
  using S32 = int32x2_t;
  struct T {
    U8 lo, hi;
  };
  static constexpr int kLanes = 1;

  template <class X> static VIO_INLINE X as(U8 v) { return (X)v; }
  template <class X> static VIO_INLINE U8 u8(X v) { return (U8)v; }
  static VIO_INLINE std::uint64_t q(U8 v) { return (std::uint64_t)v; }
  static VIO_INLINE U8 from_q(std::uint64_t v) { return (U8)v; }
  // Applies a 64-bit operation to both halves.
  template <class F> static VIO_INLINE T map(T a, T b, F f) { return {f(a.lo, b.lo), f(a.hi, b.hi)}; }

  static VIO_INLINE T zero() { return {U8{}, U8{}}; }
  static VIO_INLINE T set1_8(int v) {
    const std::uint8_t x = static_cast<std::uint8_t>(v);
    U8 h = {x, x, x, x, x, x, x, x};
    return {h, h};
  }
  static VIO_INLINE T set1_16(int v) {
    const std::int16_t x = static_cast<std::int16_t>(v);
    U8 h = u8(S16{x, x, x, x});
    return {h, h};
  }
  static VIO_INLINE T set1_32(int v) {
    U8 h = u8(S32{v, v});
    return {h, h};
  }
  static VIO_INLINE T set2_16(int a, int b) {
    const std::int16_t x = static_cast<std::int16_t>(a), y = static_cast<std::int16_t>(b);
    U8 h = u8(S16{x, y, x, y});
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

  static VIO_INLINE T add_8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return paddb_u(x, y); }); }
  static VIO_INLINE T add_16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(paddh_s(as<S16>(x), as<S16>(y))); });
  }
  static VIO_INLINE T add_32(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(paddw_s(as<S32>(x), as<S32>(y))); });
  }
  static VIO_INLINE T sub_16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(psubh_s(as<S16>(x), as<S16>(y))); });
  }
  static VIO_INLINE T sub_32(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(psubw_s(as<S32>(x), as<S32>(y))); });
  }
  static VIO_INLINE T adds_16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(paddsh(as<S16>(x), as<S16>(y))); });
  }
  static VIO_INLINE T mullo_16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(pmullh(as<S16>(x), as<S16>(y))); });
  }
  static VIO_INLINE T mulhi_16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(pmulhh(as<S16>(x), as<S16>(y))); });
  }
  static VIO_INLINE T mulhi_u16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(pmulhuh(as<U16>(x), as<U16>(y))); });
  }
  static VIO_INLINE T madd_16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(pmaddhw(as<S16>(x), as<S16>(y))); });
  }

  template <int N> static VIO_INLINE T slli_16(T a) {
    return {u8(psllh_s(as<S16>(a.lo), N)), u8(psllh_s(as<S16>(a.hi), N))};
  }
  template <int N> static VIO_INLINE T srli_16(T a) {
    return {u8(psrlh_u(as<U16>(a.lo), N)), u8(psrlh_u(as<U16>(a.hi), N))};
  }
  template <int N> static VIO_INLINE T srai_16(T a) {
    return {u8(psrah_s(as<S16>(a.lo), N)), u8(psrah_s(as<S16>(a.hi), N))};
  }
  template <int N> static VIO_INLINE T slli_32(T a) {
    return {u8(psllw_s(as<S32>(a.lo), N)), u8(psllw_s(as<S32>(a.hi), N))};
  }
  template <int N> static VIO_INLINE T srli_32(T a) {
    return {u8(psrlw_u((uint32x2_t)a.lo, N)), u8(psrlw_u((uint32x2_t)a.hi, N))};
  }
  template <int N> static VIO_INLINE T srai_32(T a) {
    return {u8(psraw_s(as<S32>(a.lo), N)), u8(psraw_s(as<S32>(a.hi), N))};
  }
  template <int I> static VIO_INLINE T shuffle_32(T a) {
    S32 lo = as<S32>(a.lo), hi = as<S32>(a.hi);
    auto e = [&](int k) { return k < 2 ? lo[k] : hi[k - 2]; };
    return {u8(S32{e(I & 3), e((I >> 2) & 3)}), u8(S32{e((I >> 4) & 3), e((I >> 6) & 3)})};
  }
  template <int N> static VIO_INLINE T bslli(T a) {  // byte i <- byte i - N
    std::uint64_t lo = q(a.lo), hi = q(a.hi);
    if constexpr (N == 0) return a;
    else if constexpr (N < 8) return {from_q(lo << (8 * N)), from_q((hi << (8 * N)) | (lo >> (64 - 8 * N)))};
    else if constexpr (N == 8) return {from_q(0), from_q(lo)};
    else return {from_q(0), from_q(lo << (8 * (N - 8)))};
  }
  template <int N> static VIO_INLINE T bsrli(T a) {  // byte i <- byte i + N
    std::uint64_t lo = q(a.lo), hi = q(a.hi);
    if constexpr (N == 0) return a;
    else if constexpr (N < 8) return {from_q((lo >> (8 * N)) | (hi << (64 - 8 * N))), from_q(hi >> (8 * N))};
    else if constexpr (N == 8) return {from_q(hi), from_q(0)};
    else return {from_q(hi >> (8 * (N - 8))), from_q(0)};
  }

  // x86 unpack semantics on 128 bits: unpacklo interleaves the low halves.
  static VIO_INLINE T unpacklo_8(T a, T b) { return {punpcklbh_u(a.lo, b.lo), punpckhbh_u(a.lo, b.lo)}; }
  static VIO_INLINE T unpackhi_8(T a, T b) { return {punpcklbh_u(a.hi, b.hi), punpckhbh_u(a.hi, b.hi)}; }
  static VIO_INLINE T unpacklo_16(T a, T b) {
    return {u8(punpcklhw_u(as<U16>(a.lo), as<U16>(b.lo))), u8(punpckhhw_u(as<U16>(a.lo), as<U16>(b.lo)))};
  }
  static VIO_INLINE T unpackhi_16(T a, T b) {
    return {u8(punpcklhw_u(as<U16>(a.hi), as<U16>(b.hi))), u8(punpckhhw_u(as<U16>(a.hi), as<U16>(b.hi)))};
  }
  static VIO_INLINE T unpacklo_32(T a, T b) {
    return {u8(punpcklwd_u((uint32x2_t)a.lo, (uint32x2_t)b.lo)),
            u8(punpckhwd_u((uint32x2_t)a.lo, (uint32x2_t)b.lo))};
  }
  static VIO_INLINE T unpackhi_32(T a, T b) {
    return {u8(punpcklwd_u((uint32x2_t)a.hi, (uint32x2_t)b.hi)),
            u8(punpckhwd_u((uint32x2_t)a.hi, (uint32x2_t)b.hi))};
  }
  static VIO_INLINE T unpacklo_64(T a, T b) { return {a.lo, b.lo}; }
  static VIO_INLINE T unpackhi_64(T a, T b) { return {a.hi, b.hi}; }
  static VIO_INLINE T packs_32(T a, T b) {
    return {u8(packsswh(as<S32>(a.lo), as<S32>(a.hi))), u8(packsswh(as<S32>(b.lo), as<S32>(b.hi)))};
  }
  static VIO_INLINE T packs_16(T a, T b) {
    return {u8(packsshb(as<S16>(a.lo), as<S16>(a.hi))), u8(packsshb(as<S16>(b.lo), as<S16>(b.hi)))};
  }
  // Clamped to [0, 255] first: then truncating and saturating packs agree
  // (QEMU's emulation of packushb does not saturate signed inputs).
  static VIO_INLINE T packus_16(T a, T b) {
    const S16 lo = {0, 0, 0, 0}, hi = {255, 255, 255, 255};
    auto c = [&](U8 x) { return as<U16>(u8(pminsh(pmaxsh(as<S16>(x), lo), hi))); };
    return {packushb(c(a.lo), c(a.hi)), packushb(c(b.lo), c(b.hi))};
  }

  static VIO_INLINE T and_(T a, T b) { return {a.lo & b.lo, a.hi & b.hi}; }
  static VIO_INLINE T or_(T a, T b) { return {a.lo | b.lo, a.hi | b.hi}; }
  static VIO_INLINE T xor_(T a, T b) { return {a.lo ^ b.lo, a.hi ^ b.hi}; }
  static VIO_INLINE T andnot(T a, T b) { return {~a.lo & b.lo, ~a.hi & b.hi}; }
  static VIO_INLINE T cmpeq_16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(pcmpeqh_s(as<S16>(x), as<S16>(y))); });
  }
  static VIO_INLINE T cmpgt_16(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return u8(pcmpgth_s(as<S16>(x), as<S16>(y))); });
  }

  // Unsigned bytes (PNG filters); avg_u8 rounds up.
  static VIO_INLINE T sub_8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return psubb_u(x, y); }); }
  static VIO_INLINE T avg_u8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return pavgb(x, y); }); }
  // a - (a - b saturated), not pminub, which QEMU emulates on 4 bytes only
  static VIO_INLINE T min_u8(T a, T b) {
    return map(a, b, [](U8 x, U8 y) { return psubb_u(x, psubusb(x, y)); });
  }
  static VIO_INLINE T adds_u8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return paddusb(x, y); }); }
  static VIO_INLINE T subs_u8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return psubusb(x, y); }); }
  static VIO_INLINE T cmpeq_8(T a, T b) { return map(a, b, [](U8 x, U8 y) { return pcmpeqb_u(x, y); }); }
  static VIO_INLINE T loadl_32(const void* p) {
    std::uint32_t x;
    std::memcpy(&x, p, 4);
    return {from_q(x), U8{}};
  }

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
