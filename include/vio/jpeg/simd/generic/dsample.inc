// vio::jpeg::simd (generic, 128-bit lanes) - h2v1/h2v2 plain and "fancy" (triangle filter)
// upsampling.  Lane-generic:
// each 128-bit lane handles 16 input samples.  Output rows may be written up
// to 64 samples past their end; input rows are read up to 32 samples past.

namespace detail_us {

constexpr int N = 16 * V::kLanes;  // input samples per iteration

// Loads the samples at offsets -1, 0 and +1 from inptr[col], replicating the
// row's first and last samples beyond its ends (where the triangle filter
// uses the edge sample itself).
struct Taps {
  V::T prev, cur, next;
};
VIO_INLINE Taps load_taps(const std::uint8_t* inptr, unsigned col, unsigned width,
                          std::uint8_t* tmp /* N + 2 bytes */) {
  if (col > 0 && col + N < width)
    return {V::loadu_lanes(inptr + col - 1, 16), V::loadu_lanes(inptr + col, 16),
            V::loadu_lanes(inptr + col + 1, 16)};
  for (int j = 0; j < N + 2; j++) {
    long x = static_cast<long>(col) - 1 + j;
    x = x < 0 ? 0 : (x >= static_cast<long>(width) ? static_cast<long>(width) - 1 : x);
    tmp[j] = inptr[x];
  }
  return {V::loadu_lanes(tmp, 16), V::loadu_lanes(tmp + 1, 16), V::loadu_lanes(tmp + 2, 16)};
}

// Interleaves even/odd outputs (16-bit) into 32 bytes per lane.
VIO_INLINE void store_pairs(std::uint8_t* out, V::T even_lo, V::T odd_lo, V::T even_hi,
                            V::T odd_hi) {
  V::storeu_lanes(out, 32, V::interleave_u8(even_lo, odd_lo));
  V::storeu_lanes(out + 16, 32, V::interleave_u8(even_hi, odd_hi));
}

}  // namespace detail_us

// out[2i] = (3 in[i] + in[i-1] + 1) >> 2, out[2i+1] = (3 in[i] + in[i+1] + 2) >> 2
inline void h2v1_fancy_upsample(int max_v_samp_factor, unsigned downsampled_width,
                                std::uint8_t** input_data, std::uint8_t** output_data) {
  using namespace detail_us;
  alignas(32) std::uint8_t tmp[N + 2];
  const V::T one = V::set1_16(1), two = V::set1_16(2), three = V::set1_16(3);
  for (int row = 0; row < max_v_samp_factor; row++) {
    const std::uint8_t* inptr = input_data[row];
    std::uint8_t* outptr = output_data[row];
    for (unsigned col = 0; col < downsampled_width; col += N, outptr += 2 * N) {
      Taps t = load_taps(inptr, col, downsampled_width, tmp);
      V::T c_lo = V::mullo_16(V::widenlo_u8(t.cur), three);
      V::T c_hi = V::mullo_16(V::widenhi_u8(t.cur), three);
      V::T e_lo = V::srli_16<2>(V::add_16(V::add_16(c_lo, V::widenlo_u8(t.prev)), one));
      V::T e_hi = V::srli_16<2>(V::add_16(V::add_16(c_hi, V::widenhi_u8(t.prev)), one));
      V::T o_lo = V::srli_16<2>(V::add_16(V::add_16(c_lo, V::widenlo_u8(t.next)), two));
      V::T o_hi = V::srli_16<2>(V::add_16(V::add_16(c_hi, V::widenhi_u8(t.next)), two));
      store_pairs(outptr, e_lo, o_lo, e_hi, o_hi);
    }
  }
}

// With column sums s[i] = 3 near[i] + far[i] (near = this input row, far =
// the row above/below): out[2i] = (3 s[i] + s[i-1] + 8) >> 4,
// out[2i+1] = (3 s[i] + s[i+1] + 7) >> 4.
inline void h2v2_fancy_upsample(int max_v_samp_factor, unsigned downsampled_width,
                                std::uint8_t** input_data, std::uint8_t** output_data) {
  using namespace detail_us;
  alignas(32) std::uint8_t tmp0[N + 2], tmp1[N + 2];
  const V::T three = V::set1_16(3), seven = V::set1_16(7),
             eight = V::set1_16(8);
  auto colsum_lo = [&](V::T near, V::T far) VIO_LAMBDA_INLINE {
    return V::add_16(V::mullo_16(V::widenlo_u8(near), three), V::widenlo_u8(far));
  };
  auto colsum_hi = [&](V::T near, V::T far) VIO_LAMBDA_INLINE {
    return V::add_16(V::mullo_16(V::widenhi_u8(near), three), V::widenhi_u8(far));
  };
  for (int inrow = 0, outrow = 0; outrow < max_v_samp_factor; inrow++) {
    for (int v = 0; v < 2; v++) {
      const std::uint8_t* inptr0 = input_data[inrow];
      const std::uint8_t* inptr1 = input_data[v == 0 ? inrow - 1 : inrow + 1];
      std::uint8_t* outptr = output_data[outrow++];
      for (unsigned col = 0; col < downsampled_width; col += N, outptr += 2 * N) {
        Taps a = load_taps(inptr0, col, downsampled_width, tmp0);
        Taps b = load_taps(inptr1, col, downsampled_width, tmp1);
        V::T s_lo = V::mullo_16(colsum_lo(a.cur, b.cur), three);
        V::T s_hi = V::mullo_16(colsum_hi(a.cur, b.cur), three);
        V::T e_lo = V::srli_16<4>(V::add_16(V::add_16(s_lo, colsum_lo(a.prev, b.prev)), eight));
        V::T e_hi = V::srli_16<4>(V::add_16(V::add_16(s_hi, colsum_hi(a.prev, b.prev)), eight));
        V::T o_lo = V::srli_16<4>(V::add_16(V::add_16(s_lo, colsum_lo(a.next, b.next)), seven));
        V::T o_hi = V::srli_16<4>(V::add_16(V::add_16(s_hi, colsum_hi(a.next, b.next)), seven));
        store_pairs(outptr, e_lo, o_lo, e_hi, o_hi);
      }
    }
  }
}

inline void h2v1_upsample(int max_v_samp_factor, unsigned output_width, std::uint8_t** input_data,
                          std::uint8_t** output_data) {
  using detail_us::N;
  for (int row = 0; row < max_v_samp_factor; row++) {
    const std::uint8_t* inptr = input_data[row];
    std::uint8_t* outptr = output_data[row];
    for (unsigned col = 0; 2 * col < output_width; col += N, outptr += 2 * N) {
      V::T x = V::loadu_lanes(inptr + col, 16);
      V::storeu_lanes(outptr, 32, V::unpacklo_8(x, x));
      V::storeu_lanes(outptr + 16, 32, V::unpackhi_8(x, x));
    }
  }
}

inline void h2v2_upsample(int max_v_samp_factor, unsigned output_width, std::uint8_t** input_data,
                          std::uint8_t** output_data) {
  using detail_us::N;
  for (int inrow = 0, outrow = 0; outrow < max_v_samp_factor; inrow++, outrow += 2) {
    const std::uint8_t* inptr = input_data[inrow];
    std::uint8_t* outptr0 = output_data[outrow];
    std::uint8_t* outptr1 = output_data[outrow + 1];
    for (unsigned col = 0; 2 * col < output_width; col += N, outptr0 += 2 * N, outptr1 += 2 * N) {
      V::T x = V::loadu_lanes(inptr + col, 16);
      V::T lo = V::unpacklo_8(x, x), hi = V::unpackhi_8(x, x);
      V::storeu_lanes(outptr0, 32, lo);
      V::storeu_lanes(outptr0 + 16, 32, hi);
      V::storeu_lanes(outptr1, 32, lo);
      V::storeu_lanes(outptr1 + 16, 32, hi);
    }
  }
}

// kind: detail::simd::Upsample (H2V1, H2V2, H2V1Fancy, H2V2Fancy, H1V2Fancy)
inline UpsampleFn upsample_kernel(int kind) {
  switch (kind) {
    case 0: return h2v1_upsample;
    case 1: return h2v2_upsample;
    case 2: return h2v1_fancy_upsample;
    case 3: return h2v2_fancy_upsample;
    default: return nullptr;  // no h1v2 fancy kernel on x86
  }
}
