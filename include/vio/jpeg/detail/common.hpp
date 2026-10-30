// vio::jpeg internal utilities: sample traits, buffers, small helpers.
#pragma once

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>

#include "../types.hpp"

namespace vio::jpeg::detail {

// Sample containers: 8-bit samples use uint8_t, 9..12-bit samples use int16_t
// (the conventional 12-bit sample type), 13..16-bit use uint16_t.
template <class T>
struct SampleTraits;
template <>
struct SampleTraits<std::uint8_t> {
  static constexpr int bits = 8;
};
template <>
struct SampleTraits<std::int16_t> {
  static constexpr int bits = 12;
};
template <>
struct SampleTraits<std::uint16_t> {
  static constexpr int bits = 16;
};

template <class T>
inline constexpr int kBits = SampleTraits<T>::bits;
template <class T>
inline constexpr int kMaxSample = (1 << kBits<T>) - 1;
template <class T>
inline constexpr int kCenterSample = 1 << (kBits<T> - 1);

using JLong = long long;  // 64-bit intermediate arithmetic

inline constexpr char icc_signature[] = "ICC_PROFILE";

inline void reset_arith_tables(std::uint8_t* dc_l, std::uint8_t* dc_u, std::uint8_t* ac_k) {
  for (int i = 0; i < NUM_ARITH_TBLS; i++) {
    dc_l[i] = 0;
    dc_u[i] = 1;
    ac_k[i] = 5;
  }
}

template <class F>
auto select_rgb_layout(ColorSpace out, F&& select) {
  switch (out) {
    case ColorSpace::ExtRGBX:
    case ColorSpace::ExtRGBA: return select.template operator()<0, 1, 2, 3, 4>();
    case ColorSpace::ExtBGR: return select.template operator()<2, 1, 0, -1, 3>();
    case ColorSpace::ExtBGRX:
    case ColorSpace::ExtBGRA: return select.template operator()<2, 1, 0, 3, 4>();
    case ColorSpace::ExtXBGR:
    case ColorSpace::ExtABGR: return select.template operator()<3, 2, 1, 0, 4>();
    case ColorSpace::ExtXRGB:
    case ColorSpace::ExtARGB: return select.template operator()<1, 2, 3, 0, 4>();
    default: return select.template operator()<0, 1, 2, -1, 3>();
  }
}

// Generate canonical Huffman code lengths and codes for encoding or decoding.
VIO_INLINE int make_huffman_codes(const HuffTable& htbl, char* huffsize, unsigned* huffcode) {
  int p = 0;
  for (int l = 1; l <= 16; l++) {
    int i = htbl.bits[l];
    if (p + i > 256) VIO_FAIL("Bogus Huffman table definition");
    while (i--) huffsize[p++] = static_cast<char>(l);
  }
  huffsize[p] = 0;
  int count = p;
  unsigned code = 0;
  int si = huffsize[0];
  p = 0;
  while (huffsize[p]) {
    while (huffsize[p] == si) {
      huffcode[p++] = code;
      code++;
    }
    if (static_cast<JLong>(code) >= (static_cast<JLong>(1) << si))
      VIO_FAIL("Bogus Huffman table definition");
    code <<= 1;
    si++;
  }
  return count;
}

// Population count.  The x86-64 baseline has no POPCNT instruction, and
// GCC then calls a library routine: the bit-parallel count is inline.
VIO_INLINE int popcount64(std::uint64_t x) {
#if (defined(__x86_64__) || defined(__i386__)) && !defined(__POPCNT__)
  x -= (x >> 1) & 0x5555555555555555ULL;
  x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
  x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
  return static_cast<int>((x * 0x0101010101010101ULL) >> 56);
#else
  return std::popcount(x);
#endif
}

// Whether the compiler vectorizes per-sample loops written as independent
// arithmetic (MSVC does not: table lookups and running sums are faster
// there).  The decompression side's forms (32-bit products, and sums the
// compiler keeps in 32 bits) pay only with 32-bit vector multiplies: Arm
// NEON, or x86 from SSE4.1 (not the x86-64 baseline).
#if defined(_MSC_VER) && !defined(__clang__)
inline constexpr bool kVectorizes = false;
inline constexpr bool kVectorizesWide = false;
#else
inline constexpr bool kVectorizes = true;
#if defined(__ARM_NEON) || defined(__aarch64__) || defined(__SSE4_1__)
inline constexpr bool kVectorizesWide = true;
#else
inline constexpr bool kVectorizesWide = false;
#endif
#endif

inline constexpr long div_round_up(long a, long b) { return (a + b - 1) / b; }
inline constexpr long round_up(long a, long b) { return div_round_up(a, b) * b; }

// Arithmetic right shift (well-defined in C++20, and on all supported
// compilers before that).
template <class I>
VIO_INLINE constexpr I rshift(I x, int n) {
  return x >> n;
}
template <class I>
VIO_INLINE constexpr I lshift(I x, int n) {
  return static_cast<I>(static_cast<std::make_unsigned_t<I>>(x) << n);
}

// ---------------------------------------------------------------------------
// Aligned heap storage.
inline constexpr std::size_t kAlign = 64;

// The block from calloc() is stored just before the aligned pointer.
struct AlignedFree {
  void operator()(void* p) const noexcept {
    if (p) std::free(static_cast<void**>(p)[-1]);
  }
};

template <class T>
using AlignedPtr = std::unique_ptr<T[], AlignedFree>;

// calloc() rather than new + memset: large blocks then come straight from
// the system, already zeroed, without a pass over the memory (whole-image
// coefficient buffers, for instance).
template <class T>
AlignedPtr<T> aligned_alloc_zeroed(std::size_t n) {
  constexpr std::size_t extra = kAlign + sizeof(void*);
  n = std::max<std::size_t>(n, 1);
  if (n > (static_cast<std::size_t>(-1) - extra) / sizeof(T)) throw std::bad_alloc();
  void* raw = std::calloc(n * sizeof(T) + extra, 1);
  if (!raw) throw std::bad_alloc();
  const std::uintptr_t a =
      (reinterpret_cast<std::uintptr_t>(raw) + extra) & ~static_cast<std::uintptr_t>(kAlign - 1);
  reinterpret_cast<void**>(a)[-1] = raw;
  return AlignedPtr<T>(reinterpret_cast<T*>(a));
}

// A 2-D array of samples addressed through a row pointer table.  Rows are
// padded and aligned so that SIMD code may safely over-read/over-write a
// little past the row end.
template <class T>
class SampleArray {
 public:
  SampleArray() = default;
  SampleArray(std::size_t width, std::size_t height) { allocate(width, height); }

  void allocate(std::size_t width, std::size_t height) {
    stride_ = round_up(static_cast<long>(width) + 64, kAlign / sizeof(T) * 2);
    height_ = height;
    width_ = width;
    data_ = aligned_alloc_zeroed<T>(stride_ * std::max<std::size_t>(height, 1));
    rows_.assign(height, nullptr);
    for (std::size_t i = 0; i < height; i++) rows_[i] = data_.get() + i * stride_;
  }

  T** rows() { return rows_.data(); }
  T* operator[](std::size_t r) { return rows_[r]; }
  std::size_t width() const { return width_; }
  std::size_t height() const { return height_; }
  std::size_t stride() const { return stride_; }
  bool empty() const { return rows_.empty(); }

 private:
  AlignedPtr<T> data_;
  std::vector<T*> rows_;
  std::size_t stride_ = 0, width_ = 0, height_ = 0;
};

// A 2-D array of coefficient blocks (JBLOCKARRAY equivalent).
class BlockArray {
 public:
  BlockArray() = default;
  void allocate(std::size_t blocks_per_row, std::size_t rows) {
    per_row_ = blocks_per_row;
    rows_ = rows;
    data_ = aligned_alloc_zeroed<Block>(blocks_per_row * rows);
    base_ = data_.get();
  }
  // Refers to caller-owned rows of `blocks_per_row` blocks at `base`.
  void view(Block* base, std::size_t blocks_per_row, std::size_t rows) {
    data_.reset();
    per_row_ = blocks_per_row;
    rows_ = rows;
    base_ = base;
  }
  Block* row(std::size_t r) { return base_ + r * per_row_; }
  const Block* row(std::size_t r) const { return base_ + r * per_row_; }
  std::size_t blocks_per_row() const { return per_row_; }
  std::size_t num_rows() const { return rows_; }
  bool empty() const { return !base_; }

 private:
  AlignedPtr<Block> data_;
  Block* base_ = nullptr;
  std::size_t per_row_ = 0, rows_ = 0;
};

template <class T>
inline void copy_sample_rows(T** input_array, int source_row, T** output_array, int dest_row,
                             int num_rows, std::size_t num_cols) {
  for (int i = 0; i < num_rows; i++)
    std::memcpy(output_array[dest_row + i], input_array[source_row + i], num_cols * sizeof(T));
}

// Sample range-limiting table.
// `limit()` points to the "simple" table: limit()[x] == clamp(x) for x in
// [-(MAX+1), 2*(MAX+1)+CENTER).  `idct_limit()` is offset by CENTER and is to
// be indexed by (x & range_mask()).
template <class T>
class RangeLimit {
 public:
  RangeLimit() {
    constexpr int N = kMaxSample<T> + 1, C = kCenterSample<T>;
    table_.assign(5 * N + C, 0);
    T* table = table_.data() + N;
    for (int i = 0; i <= kMaxSample<T>; i++) table[i] = static_cast<T>(i);
    T* t2 = table + C;
    for (int i = C; i < 2 * N; i++) t2[i] = static_cast<T>(kMaxSample<T>);
    // second half (2N - C entries) zero already; then copy of table[0..C)
    for (int i = 0; i < C; i++) t2[4 * N - C + i] = table[i];
  }
  const T* limit() const { return table_.data() + kMaxSample<T> + 1; }
  const T* idct_limit() const { return limit() + kCenterSample<T>; }
  static constexpr int range_mask() { return kMaxSample<T> * 4 + 3; }

 private:
  std::vector<T> table_;
};

}  // namespace vio::jpeg::detail
