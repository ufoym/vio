// vio::jpeg::simd (generic, 128-bit lanes) - YCbCr -> RGB color conversion and merged
// (h2v1/h2v2) upsampling + color conversion.  Lane-generic.

namespace detail_dc {

constexpr int F_0_344 = 22554, F_0_714 = 46802, F_1_402 = 91881, F_1_772 = 116130;
constexpr int F_0_402 = F_1_402 - 65536, F_0_285 = 65536 - F_0_714, F_0_228 = 131072 - F_1_772;

// Chroma contributions R-Y, G-Y, B-Y for eight 16-bit (Cb - 128, Cr - 128)
// pairs per lane, exactly as the scalar tables compute them:
//   R-Y = (FIX(1.40200) cr + ONE_HALF) >> 16          (as 0.402 cr + cr)
//   B-Y = (FIX(1.77200) cb + ONE_HALF) >> 16          (as -0.228 cb + 2 cb)
//   G-Y = (-FIX(0.34414) cb - FIX(0.71414) cr + ONE_HALF) >> 16
struct Chroma {
  V::T rmy, gmy, bmy;
};
VIO_INLINE Chroma chroma(V::T cb, V::T cr) {
  const V::T one = V::set1_16(1);
  V::T bmy = V::srai_16<1>(V::add_16(V::mulhi_16(V::add_16(cb, cb), V::set1_16(-F_0_228)), one));
  bmy = V::add_16(V::add_16(bmy, cb), cb);
  V::T rmy = V::srai_16<1>(V::add_16(V::mulhi_16(V::add_16(cr, cr), V::set1_16(F_0_402)), one));
  rmy = V::add_16(rmy, cr);
  const V::T k = V::set2_16(-F_0_344, F_0_285), half = V::set1_32(1 << 15);
  V::T lo = V::srai_32<16>(V::add_32(V::madd_16(V::unpacklo_16(cb, cr), k), half));
  V::T hi = V::srai_32<16>(V::add_32(V::madd_16(V::unpackhi_16(cb, cr), k), half));
  V::T gmy = V::sub_16(V::packs_32(lo, hi), cr);
  return {rmy, gmy, bmy};
}

// Stores 16 pixels per lane given their even- and odd-numbered component
// samples (8 bytes each, in the low half of each lane), indexed by position
// within the pixel.  Lane i's pixels go to out + i * lane_stride.
template <int PS>
VIO_INLINE void store_pixels(std::uint8_t* out, std::ptrdiff_t lane_stride, const V::T* pe,
                             const V::T* po) {
  using T = V::T;
  if constexpr (PS == 3) {
    T a = V::unpacklo_8(pe[0], pe[1]);  // A0 B0 A2 B2 ..
    T e = V::unpacklo_8(pe[2], po[0]);  // C0 A1 C2 A3 ..
    T d = V::unpacklo_8(po[1], po[2]);  // B1 C1 B3 C3 ..
    T g = V::unpackhi_16(a, e);         // A8 B8 C8 A9 ..
    T h = V::bsrli<2>(a);
    a = V::unpacklo_16(a, e);           // A0 B0 C0 A1 A2 B2 C2 A3 ..
    e = V::bsrli<2>(e);
    T c = V::unpackhi_16(d, h);         // B9 C9 Aa Ba ..
    T b = V::bsrli<2>(d);
    d = V::unpacklo_16(d, h);           // B1 C1 A2 B2 ..
    T f = V::unpackhi_16(e, b);         // Ca Ab Bb Cb ..
    e = V::unpacklo_16(e, b);           // C2 A3 B3 C3 ..
    h = V::shuffle_32<0x4E>(a);
    b = e;
    a = V::unpacklo_32(a, d);
    e = V::unpacklo_32(e, h);
    d = V::unpackhi_32(d, b);
    h = V::shuffle_32<0x4E>(g);
    b = f;
    g = V::unpacklo_32(g, c);
    f = V::unpacklo_32(f, h);
    c = V::unpackhi_32(c, b);
    V::storeu_lanes(out, lane_stride, V::unpacklo_64(a, e));
    V::storeu_lanes(out + 16, lane_stride, V::unpacklo_64(d, g));
    V::storeu_lanes(out + 32, lane_stride, V::unpacklo_64(f, c));
  } else {
    T a = V::unpacklo_8(pe[0], pe[1]);  // A0 B0 A2 B2 ..
    T e = V::unpacklo_8(pe[2], pe[3]);  // C0 D0 C2 D2 ..
    T b = V::unpacklo_8(po[0], po[1]);  // A1 B1 A3 B3 ..
    T f = V::unpacklo_8(po[2], po[3]);  // C1 D1 C3 D3 ..
    T c = V::unpackhi_16(a, e);
    a = V::unpacklo_16(a, e);
    T g = V::unpackhi_16(b, f);
    b = V::unpacklo_16(b, f);
    V::storeu_lanes(out, lane_stride, V::unpacklo_32(a, b));
    V::storeu_lanes(out + 16, lane_stride, V::unpackhi_32(a, b));
    V::storeu_lanes(out + 32, lane_stride, V::unpacklo_32(c, g));
    V::storeu_lanes(out + 48, lane_stride, V::unpackhi_32(c, g));
  }
}

// Converts and stores 16 pixels per lane: even/odd luma (16-bit) with the
// chroma terms that apply to each even/odd pair.
template <int R, int G, int B, int PS>
VIO_INLINE void emit16(std::uint8_t* out, std::ptrdiff_t lane_stride, V::T ye, V::T yo,
                       const Chroma& ce, const Chroma& co) {
  V::T pe[4], po[4];
  pe[R] = V::packus_16(V::add_16(ce.rmy, ye), V::zero());
  pe[G] = V::packus_16(V::add_16(ce.gmy, ye), V::zero());
  pe[B] = V::packus_16(V::add_16(ce.bmy, ye), V::zero());
  po[R] = V::packus_16(V::add_16(co.rmy, yo), V::zero());
  po[G] = V::packus_16(V::add_16(co.gmy, yo), V::zero());
  po[B] = V::packus_16(V::add_16(co.bmy, yo), V::zero());
  if constexpr (PS == 4) pe[6 - R - G - B] = po[6 - R - G - B] = V::set1_8(-1);
  store_pixels<PS>(out, lane_stride, pe, po);
}

// Back ends with byte shuffles (see ccolor.inc) interleave the pixels from
// one register per component, its even pixels in bytes 0-7 and odd ones in
// 8-15 of each lane: each 16 output bytes gather from the three registers.
template <class X, class = void>
struct HasShuffle8 : std::false_type {};
template <class X>
struct HasShuffle8<X, std::void_t<decltype(X::kShuffle8)>> : std::true_type {};

// Index of output chunk C's bytes taken from component S (by position in
// the pixel), or -128.
template <int PS, int C, int S>
struct ChunkIndex {
  static constexpr std::int8_t at(int j) {
    const int byte = 16 * C + j, pixel = byte / PS, pos = byte % PS;
    return pos == S ? static_cast<std::int8_t>((pixel & 1) * 8 + pixel / 2) : std::int8_t(-128);
  }
  alignas(16) static constexpr std::int8_t idx[16] = {at(0), at(1), at(2),  at(3),  at(4),  at(5),
                                                      at(6), at(7), at(8),  at(9),  at(10), at(11),
                                                      at(12), at(13), at(14), at(15)};
};

template <int PS, int C, class W>
VIO_INLINE typename W::T chunk(const typename W::T comp[3]) {
  using T = typename W::T;
  T v = W::shuffle_8(comp[0], W::load_bcast16(opaque(ChunkIndex<PS, C, 0>::idx)));
  v = W::or_(v, W::shuffle_8(comp[1], W::load_bcast16(opaque(ChunkIndex<PS, C, 1>::idx))));
  v = W::or_(v, W::shuffle_8(comp[2], W::load_bcast16(opaque(ChunkIndex<PS, C, 2>::idx))));
  if constexpr (PS == 4) {  // the fourth byte of each pixel: 0xFF
    alignas(16) static constexpr std::int8_t alpha[16] = {0, 0, 0, -1, 0, 0, 0, -1,
                                                          0, 0, 0, -1, 0, 0, 0, -1};
    v = W::or_(v, W::load_bcast16(opaque(alpha)));
  }
  return v;
}

// comp[k]: the samples of the component at position k in the pixel.
template <int PS, class W>
VIO_INLINE void store_shuffled(std::uint8_t* out, std::ptrdiff_t lane_stride,
                               const typename W::T comp[3]) {
  W::storeu_lanes(out, lane_stride, chunk<PS, 0, W>(comp));
  W::storeu_lanes(out + 16, lane_stride, chunk<PS, 1, W>(comp));
  W::storeu_lanes(out + 32, lane_stride, chunk<PS, 2, W>(comp));
  if constexpr (PS == 4) W::storeu_lanes(out + 48, lane_stride, chunk<PS, 3, W>(comp));
}

}  // namespace detail_dc

template <int R, int G, int B, int PS>
void ycc_rgb_convert(unsigned output_width, std::uint8_t*** input_buf, unsigned input_row,
                     std::uint8_t** output_buf, int num_rows) {
  using namespace detail_dc;
  constexpr int N = 16 * V::kLanes;
  alignas(32) std::uint8_t tmp[N * PS];
  const V::T m128 = V::set1_16(-128);
  while (--num_rows >= 0) {
    const std::uint8_t* inptr0 = input_buf[0][input_row];
    const std::uint8_t* inptr1 = input_buf[1][input_row];
    const std::uint8_t* inptr2 = input_buf[2][input_row];
    input_row++;
    std::uint8_t* outptr = *output_buf++;
    for (int cols = static_cast<int>(output_width); cols > 0; cols -= N) {
      V::T y = V::loadu(inptr0), cb = V::loadu(inptr1), cr = V::loadu(inptr2);
      Chroma ce = chroma(V::add_16(V::even_u8(cb), m128), V::add_16(V::even_u8(cr), m128));
      Chroma co = chroma(V::add_16(V::odd_u8(cb), m128), V::add_16(V::odd_u8(cr), m128));
      std::uint8_t* dst = cols < N ? tmp : outptr;
      if constexpr (HasShuffle8<V>::value &&
                    (PS == 3 || (R < 3 && G < 3 && B < 3))) {  // (alpha last)
        const V::T ye = V::even_u8(y), yo = V::odd_u8(y);
        V::T comp[3];
        comp[R] = V::packus_16(V::add_16(ce.rmy, ye), V::add_16(co.rmy, yo));
        comp[G] = V::packus_16(V::add_16(ce.gmy, ye), V::add_16(co.gmy, yo));
        comp[B] = V::packus_16(V::add_16(ce.bmy, ye), V::add_16(co.bmy, yo));
        store_shuffled<PS, V>(dst, 16 * PS, comp);
      } else {
        emit16<R, G, B, PS>(dst, 16 * PS, V::even_u8(y), V::odd_u8(y), ce, co);
      }
      if (cols < N) std::memcpy(outptr, tmp, static_cast<std::size_t>(cols) * PS);
      inptr0 += N;
      inptr1 += N;
      inptr2 += N;
      outptr += N * PS;
    }
  }
}

// Merged upsampling: each chroma sample covers two horizontally adjacent
// pixels (and, with V2, two rows).  Per lane: 16 chroma samples, 32 pixels.
template <bool V2, int R, int G, int B, int PS>
void merged_upsample(unsigned output_width, std::uint8_t*** input_buf, unsigned in_row_group_ctr,
                     std::uint8_t** output_buf) {
  using namespace detail_dc;
  constexpr int N = 32 * V::kLanes;  // pixels per iteration
  alignas(32) std::uint8_t tmp[N * PS];
  const V::T m128 = V::set1_16(-128);
  const std::uint8_t* inptr00 = input_buf[0][V2 ? in_row_group_ctr * 2 : in_row_group_ctr];
  const std::uint8_t* inptr01 = V2 ? input_buf[0][in_row_group_ctr * 2 + 1] : nullptr;
  const std::uint8_t* inptr1 = input_buf[1][in_row_group_ctr];
  const std::uint8_t* inptr2 = input_buf[2][in_row_group_ctr];
  std::uint8_t* outptr0 = output_buf[0];
  std::uint8_t* outptr1 = V2 ? output_buf[1] : nullptr;
  auto row = [&](const std::uint8_t* yp, std::uint8_t* out, int cols, const Chroma& lo,
                 const Chroma& hi) VIO_LAMBDA_INLINE {
    V::T ya = V::loadu_lanes(yp, 32), yb = V::loadu_lanes(yp + 16, 32);
    std::uint8_t* dst = cols < N ? tmp : out;
    emit16<R, G, B, PS>(dst, 32 * PS, V::even_u8(ya), V::odd_u8(ya), lo, lo);
    emit16<R, G, B, PS>(dst + 16 * PS, 32 * PS, V::even_u8(yb), V::odd_u8(yb), hi, hi);
    if (cols < N) std::memcpy(out, tmp, static_cast<std::size_t>(cols) * PS);
  };
  for (int cols = static_cast<int>(output_width); cols > 0; cols -= N) {
    V::T cb = V::loadu(inptr1), cr = V::loadu(inptr2);
    Chroma lo = chroma(V::add_16(V::widenlo_u8(cb), m128), V::add_16(V::widenlo_u8(cr), m128));
    Chroma hi = chroma(V::add_16(V::widenhi_u8(cb), m128), V::add_16(V::widenhi_u8(cr), m128));
    row(inptr00, outptr0, cols, lo, hi);
    if constexpr (V2) {
      row(inptr01, outptr1, cols, lo, hi);
      inptr01 += N;
      outptr1 += N * PS;
    }
    inptr00 += N;
    inptr1 += N / 2;
    inptr2 += N / 2;
    outptr0 += N * PS;
  }
}

#define VIO_YCC_RGB565 0
#include "ycc_rgb_select.inc"
#undef VIO_YCC_RGB565

#include "merged_select.inc"
