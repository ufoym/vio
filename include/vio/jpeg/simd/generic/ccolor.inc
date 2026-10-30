// vio::jpeg::simd (generic, 128-bit lanes) - RGB -> YCbCr / grayscale color conversion.
// Lane-generic: each 128-bit lane converts 16 pixels.

namespace detail_cc {

constexpr int F_0_081 = 5329, F_0_114 = 7471, F_0_168 = 11059, F_0_250 = 16384,
              F_0_299 = 19595, F_0_331 = 21709, F_0_418 = 27439, F_0_587 = 38470,
              F_0_337 = F_0_587 - F_0_250;
constexpr int ONE_HALF = 1 << 15;
constexpr int ONEHALFM1_CJ = ONE_HALF - 1 + (128 << 16);

// Splits 16 pixels per lane (lane stride 16 * PS bytes) into the even- and
// odd-numbered samples of each component, zero-extended to 16 bits.
template <int PS>
VIO_INLINE void deinterleave(const std::uint8_t* p, V::T e[4], V::T o[4]) {
  using T = V::T;
  if constexpr (PS == 3) {
    T a = V::loadu_lanes(p, 48);       // A0 B0 C0 A1 B1 C1 A2 B2 C2 A3 B3 C3 A4 B4 C4 A5
    T f = V::loadu_lanes(p + 16, 48);  // B5 C5 A6 B6 C6 A7 B7 C7 A8 B8 C8 A9 B9 C9 Aa Ba
    T b = V::loadu_lanes(p + 32, 48);  // Ca Ab Bb Cb Ac Bc Cc Ad Bd Cd Ae Be Ce Af Bf Cf
    T g = V::bsrli<8>(a);
    a = V::unpackhi_8(V::bslli<8>(a), f);  // A0 A8 B0 B8 C0 C8 A1 A9 B1 B9 C1 C9 A2 Aa B2 Ba
    g = V::unpacklo_8(g, b);               // C2 Ca A3 Ab B3 Bb C3 Cb A4 Ac B4 Bc C4 Cc A5 Ad
    f = V::unpackhi_8(V::bslli<8>(f), b);  // B5 Bd C5 Cd A6 Ae B6 Be C6 Ce A7 Af B7 Bf C7 Cf
    T d = V::bsrli<8>(a);
    a = V::unpackhi_8(V::bslli<8>(a), g);  // A0 A4 A8 Ac B0 B4 B8 Bc C0 C4 C8 Cc A1 A5 A9 Ad
    d = V::unpacklo_8(d, f);               // B1 B5 B9 Bd C1 C5 C9 Cd A2 A6 Aa Ae B2 B6 Ba Be
    g = V::unpackhi_8(V::bslli<8>(g), f);  // C2 C6 Ca Ce A3 A7 Ab Af B3 B7 Bb Bf C3 C7 Cb Cf
    T ee = V::bsrli<8>(a);
    a = V::unpackhi_8(V::bslli<8>(a), d);  // A0 A2 .. Ae B0 B2 .. Be
    ee = V::unpacklo_8(ee, g);             // C0 C2 .. Ce A1 A3 .. Af
    d = V::unpackhi_8(V::bslli<8>(d), g);  // B1 B3 .. Bf C1 C3 .. Cf
    e[0] = V::widenlo_u8(a);
    e[1] = V::widenhi_u8(a);
    e[2] = V::widenlo_u8(ee);
    o[0] = V::widenhi_u8(ee);
    o[1] = V::widenlo_u8(d);
    o[2] = V::widenhi_u8(d);
  } else {
    T a = V::loadu_lanes(p, 64);       // A0 B0 C0 D0 .. A3 B3 C3 D3
    T ee = V::loadu_lanes(p + 16, 64);  // pixels 4-7
    T f = V::loadu_lanes(p + 32, 64);  // pixels 8-11
    T h = V::loadu_lanes(p + 48, 64);  // pixels 12-15
    T d = V::unpackhi_8(a, ee);        // A2 A6 B2 B6 C2 C6 D2 D6 A3 A7 B3 B7 C3 C7 D3 D7
    a = V::unpacklo_8(a, ee);          // A0 A4 B0 B4 C0 C4 D0 D4 A1 A5 B1 B5 C1 C5 D1 D5
    T c = V::unpackhi_8(f, h);         // Aa Ae Ba Be Ca Ce Da De Ab Af Bb Bf Cb Cf Db Df
    f = V::unpacklo_8(f, h);           // A8 Ac B8 Bc C8 Cc D8 Dc A9 Ad B9 Bd C9 Cd D9 Dd
    T b = V::unpackhi_16(a, f);        // A1 A5 A9 Ad B1 B5 B9 Bd C1 C5 C9 Cd D1 D5 D9 Dd
    a = V::unpacklo_16(a, f);          // A0 A4 A8 Ac B0 B4 B8 Bc C0 C4 C8 Cc D0 D4 D8 Dc
    T g = V::unpackhi_16(d, c);        // A3 A7 Ab Af B3 B7 Bb Bf C3 C7 Cb Cf D3 D7 Db Df
    d = V::unpacklo_16(d, c);          // A2 A6 Aa Ae B2 B6 Ba Be C2 C6 Ca Ce D2 D6 Da De
    ee = V::unpackhi_8(a, d);          // C0 C2 .. Ce D0 D2 .. De
    a = V::unpacklo_8(a, d);           // A0 A2 .. Ae B0 B2 .. Be
    h = V::unpackhi_8(b, g);           // C1 C3 .. Cf D1 D3 .. Df
    b = V::unpacklo_8(b, g);           // A1 A3 .. Af B1 B3 .. Bf
    e[0] = V::widenlo_u8(a);
    e[1] = V::widenhi_u8(a);
    e[2] = V::widenlo_u8(ee);
    e[3] = V::widenhi_u8(ee);
    o[0] = V::widenlo_u8(b);
    o[1] = V::widenhi_u8(b);
    o[2] = V::widenlo_u8(h);
    o[3] = V::widenhi_u8(h);
  }
}

// (x * kx + y * ky) for 16-bit x, y as two 32-bit halves.
struct Pair32 {
  V::T lo, hi;
};
VIO_INLINE Pair32 madd2(V::T x, V::T y, V::T k) {
  return {V::madd_16(V::unpacklo_16(x, y), k), V::madd_16(V::unpackhi_16(x, y), k)};
}

// Y = (0.29900 R + 0.58700 G + 0.11400 B + ONE_HALF) >> 16 (G split as
// 0.33700 + 0.25000 so that the coefficients fit pmaddwd).
VIO_INLINE V::T luma(V::T r, V::T g, V::T b) {
  const V::T k_rg = V::set2_16(F_0_299, F_0_337), k_bg = V::set2_16(F_0_114, F_0_250);
  const V::T half = V::set1_32(ONE_HALF);
  Pair32 rg = madd2(r, g, k_rg), bg = madd2(b, g, k_bg);
  V::T lo = V::srli_32<16>(V::add_32(V::add_32(rg.lo, bg.lo), half));
  V::T hi = V::srli_32<16>(V::add_32(V::add_32(rg.hi, bg.hi), half));
  return V::packs_32(lo, hi);
}

// Cb (x=R, y=G, z=B) or Cr (x=B, y=G, z=R):
// (kx x + ky y + 0.5 z + ONE_HALF - 1 + (CENTERJSAMPLE << 16)) >> 16
VIO_INLINE V::T chroma(V::T x, V::T y, V::T z, V::T k) {
  const V::T bias = V::set1_32(ONEHALFM1_CJ);
  Pair32 xy = madd2(x, y, k);
  V::T lo = V::add_32(V::add_32(xy.lo, V::srli_32<1>(V::lo16_shl16(z))), bias);
  V::T hi = V::add_32(V::add_32(xy.hi, V::srli_32<1>(V::hi16_shl16(z))), bias);
  return V::packs_32(V::srli_32<16>(lo), V::srli_32<16>(hi));
}

// Back ends with byte shuffles take (R, G) and (B, G) pairs of 4 pixels
// straight from the pixel bytes, ready for pmaddwd.
template <class X, class = void>
struct HasShuffle8 : std::false_type {};
template <class X>
struct HasShuffle8<X, std::void_t<decltype(X::kShuffle8)>> : std::true_type {};

// Windows of 4 pixels (pixel k at byte PS * k) of the 16 pixels per lane.
// (W is V; a template parameter so that back ends without these operations
// never compile them.)
template <int PS, class W>
VIO_INLINE void windows(const std::uint8_t* p, typename W::T w[4]) {
  if constexpr (PS == 3) {
    const auto a = W::loadu_lanes(p, 48), f = W::loadu_lanes(p + 16, 48),
               b = W::loadu_lanes(p + 32, 48);
    w[0] = a;                                // bytes 0-15
    w[1] = W::template alignr<12>(f, a);     // 12-27
    w[2] = W::template alignr<8>(b, f);      // 24-39
    w[3] = W::template bsrli<4>(b);          // 36-47
  } else {
    for (int i = 0; i < 4; i++) w[i] = W::loadu_lanes(p + 16 * i, 64);
  }
}

template <class W>
VIO_INLINE typename W::T shuffle8(typename W::T a, typename W::T idx) {
  return W::shuffle_8(a, idx);
}

// Shuffle index: 16-bit (x, y) pairs of the 4 pixels, zero-extended.
template <int X, int Y, int PS, class W>
VIO_INLINE typename W::T pair_index() {
  alignas(16) static constexpr std::int8_t idx[16] = {
      X,          -128, Y,          -128, PS + X,     -128, PS + Y,     -128,
      2 * PS + X, -128, 2 * PS + Y, -128, 3 * PS + X, -128, 3 * PS + Y, -128};
  return W::load_bcast16(opaque(idx));
}

// Y of 4 pixels (32-bit) from their (R, G) and (B, G) pairs.
VIO_INLINE V::T luma4(V::T rg, V::T bg) {
  const V::T k_rg = V::set2_16(F_0_299, F_0_337), k_bg = V::set2_16(F_0_114, F_0_250);
  return V::srli_32<16>(
      V::add_32(V::add_32(V::madd_16(rg, k_rg), V::madd_16(bg, k_bg)), V::set1_32(ONE_HALF)));
}
// Cb from (x, y) = (R, G) and z = B, or Cr from (B, G) and z = R (z in the
// low half of the other pair).
VIO_INLINE V::T chroma4(V::T xy, V::T zpair, V::T k) {
  const V::T z15 = V::slli_32<15>(V::and_(zpair, V::set1_32(0xFFFF)));
  return V::srli_32<16>(
      V::add_32(V::add_32(V::madd_16(xy, k), z15), V::set1_32(ONEHALFM1_CJ)));
}
VIO_INLINE V::T pack4x4(V::T a, V::T b, V::T c, V::T d) {
  return V::packus_16(V::packs_32(a, b), V::packs_32(c, d));
}

}  // namespace detail_cc

template <int R, int G, int B, int PS>
void rgb_ycc_convert(unsigned image_width, std::uint8_t** input_buf, std::uint8_t*** output_buf,
                     unsigned output_row, int num_rows) {
  using namespace detail_cc;
  constexpr int N = 16 * V::kLanes;
  alignas(32) std::uint8_t tmp[N * PS] = {};
  const V::T k_cb = V::set2_16(-F_0_168, -F_0_331), k_cr = V::set2_16(-F_0_081, -F_0_418);
  while (--num_rows >= 0) {
    const std::uint8_t* inptr = *input_buf++;
    std::uint8_t* outptr0 = output_buf[0][output_row];
    std::uint8_t* outptr1 = output_buf[1][output_row];
    std::uint8_t* outptr2 = output_buf[2][output_row];
    output_row++;
    for (int cols = static_cast<int>(image_width); cols > 0; cols -= N) {
      const std::uint8_t* src = inptr;
      if (cols < N) {
        std::memcpy(tmp, inptr, static_cast<std::size_t>(cols) * PS);
        src = tmp;
      }
      if constexpr (HasShuffle8<V>::value) {
        V::T w[4], y[4], cb[4], cr[4];
        windows<PS, V>(src, w);
        const V::T irg = pair_index<R, G, PS, V>(), ibg = pair_index<B, G, PS, V>();
        for (int i = 0; i < 4; i++) {
          const V::T rg = shuffle8<V>(w[i], irg), bg = shuffle8<V>(w[i], ibg);
          y[i] = luma4(rg, bg);
          cb[i] = chroma4(rg, bg, k_cb);
          cr[i] = chroma4(bg, rg, k_cr);
        }
        V::storeu(outptr0, pack4x4(y[0], y[1], y[2], y[3]));
        V::storeu(outptr1, pack4x4(cb[0], cb[1], cb[2], cb[3]));
        V::storeu(outptr2, pack4x4(cr[0], cr[1], cr[2], cr[3]));
      } else {
        V::T e[4], o[4];
        deinterleave<PS>(src, e, o);
        V::storeu(outptr0, V::interleave_u8(luma(e[R], e[G], e[B]), luma(o[R], o[G], o[B])));
        V::storeu(outptr1, V::interleave_u8(chroma(e[R], e[G], e[B], k_cb),
                                            chroma(o[R], o[G], o[B], k_cb)));
        V::storeu(outptr2, V::interleave_u8(chroma(e[B], e[G], e[R], k_cr),
                                            chroma(o[B], o[G], o[R], k_cr)));
      }
      inptr += N * PS;
      outptr0 += N;
      outptr1 += N;
      outptr2 += N;
    }
  }
}

template <int R, int G, int B, int PS>
void rgb_gray_convert(unsigned image_width, std::uint8_t** input_buf, std::uint8_t*** output_buf,
                      unsigned output_row, int num_rows) {
  using namespace detail_cc;
  constexpr int N = 16 * V::kLanes;
  alignas(32) std::uint8_t tmp[N * PS] = {};
  while (--num_rows >= 0) {
    const std::uint8_t* inptr = *input_buf++;
    std::uint8_t* outptr = output_buf[0][output_row++];
    for (int cols = static_cast<int>(image_width); cols > 0; cols -= N) {
      const std::uint8_t* src = inptr;
      if (cols < N) {
        std::memcpy(tmp, inptr, static_cast<std::size_t>(cols) * PS);
        src = tmp;
      }
      if constexpr (HasShuffle8<V>::value) {
        V::T w[4], y[4];
        windows<PS, V>(src, w);
        const V::T irg = pair_index<R, G, PS, V>(), ibg = pair_index<B, G, PS, V>();
        for (int i = 0; i < 4; i++) y[i] = luma4(shuffle8<V>(w[i], irg), shuffle8<V>(w[i], ibg));
        V::storeu(outptr, pack4x4(y[0], y[1], y[2], y[3]));
      } else {
        V::T e[4], o[4];
        deinterleave<PS>(src, e, o);
        V::storeu(outptr, V::interleave_u8(luma(e[R], e[G], e[B]), luma(o[R], o[G], o[B])));
      }
      inptr += N * PS;
      outptr += N;
    }
  }
}

#include "ccolor_select.inc"
