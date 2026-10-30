// vio::jpeg::simd (generic, 128-bit lanes) - h2v1/h2v2 downsampling.
// Lane-generic: each 128-bit lane produces 16 output samples.  Rows may be
// read and written up to 64 samples past their end (the sample buffers are
// padded accordingly).

namespace detail_ds {

// Replicates each row's last sample into columns [image_width, output_cols * 2).
VIO_INLINE void expand_right_edge(std::uint8_t** rows, int num_rows, unsigned image_width,
                                  unsigned output_cols) {
  const unsigned end = output_cols * 2;
  if (end <= image_width) return;
  for (int r = 0; r < num_rows; r++)
    std::memset(rows[r] + image_width, rows[r][image_width - 1], end - image_width);
}

VIO_INLINE V::T sum_pairs(V::T samples) {
  return V::add_16(V::even_u8(samples), V::odd_u8(samples));
}

}  // namespace detail_ds

inline void h2v1_downsample(unsigned image_width, int max_v_samp_factor, unsigned v_samp_factor,
                            unsigned width_in_blocks, std::uint8_t** input_data,
                            std::uint8_t** output_data) {
  constexpr int N = 16 * V::kLanes;
  const unsigned output_cols = width_in_blocks * 8;
  if (output_cols == 0) return;
  detail_ds::expand_right_edge(input_data, max_v_samp_factor, image_width, output_cols);
  const V::T bias = V::set2_16(0, 1);
  for (unsigned row = 0; row < v_samp_factor; row++) {
    const std::uint8_t* inptr = input_data[row];
    std::uint8_t* outptr = output_data[row];
    for (unsigned col = 0; col < output_cols; col += N, inptr += 2 * N, outptr += N) {
      V::T a = V::loadu_lanes(inptr, 32), b = V::loadu_lanes(inptr + 16, 32);
      a = V::srli_16<1>(V::add_16(detail_ds::sum_pairs(a), bias));
      b = V::srli_16<1>(V::add_16(detail_ds::sum_pairs(b), bias));
      V::storeu(outptr, V::packus_16(a, b));
    }
  }
}

inline void h2v2_downsample(unsigned image_width, int max_v_samp_factor, unsigned v_samp_factor,
                            unsigned width_in_blocks, std::uint8_t** input_data,
                            std::uint8_t** output_data) {
  constexpr int N = 16 * V::kLanes;
  const unsigned output_cols = width_in_blocks * 8;
  if (output_cols == 0) return;
  detail_ds::expand_right_edge(input_data, max_v_samp_factor, image_width, output_cols);
  const V::T bias = V::set2_16(1, 2);
  for (unsigned row = 0; row < v_samp_factor; row++) {
    const std::uint8_t* inptr0 = input_data[row * 2];
    const std::uint8_t* inptr1 = input_data[row * 2 + 1];
    std::uint8_t* outptr = output_data[row];
    for (unsigned col = 0; col < output_cols; col += N, inptr0 += 2 * N, inptr1 += 2 * N, outptr += N) {
      V::T a0 = V::loadu_lanes(inptr0, 32), b0 = V::loadu_lanes(inptr0 + 16, 32);
      V::T a1 = V::loadu_lanes(inptr1, 32), b1 = V::loadu_lanes(inptr1 + 16, 32);
      V::T a = V::add_16(detail_ds::sum_pairs(a0), detail_ds::sum_pairs(a1));
      V::T b = V::add_16(detail_ds::sum_pairs(b0), detail_ds::sum_pairs(b1));
      a = V::srli_16<2>(V::add_16(a, bias));
      b = V::srli_16<2>(V::add_16(b, bias));
      V::storeu(outptr, V::packus_16(a, b));
    }
  }
}
