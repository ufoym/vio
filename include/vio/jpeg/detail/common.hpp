// vio::jpeg internal utilities: sample traits, buffers, small helpers.
#pragma once

#include <algorithm>
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

struct AlignedFree {
  void operator()(void* p) const noexcept { ::operator delete(p, std::align_val_t(kAlign)); }
};

template <class T>
using AlignedPtr = std::unique_ptr<T[], AlignedFree>;

template <class T>
AlignedPtr<T> aligned_alloc_zeroed(std::size_t n) {
  void* p = ::operator new(std::max<std::size_t>(n, 1) * sizeof(T), std::align_val_t(kAlign));
  std::memset(p, 0, std::max<std::size_t>(n, 1) * sizeof(T));
  return AlignedPtr<T>(static_cast<T*>(p));
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
  }
  Block* row(std::size_t r) { return data_.get() + r * per_row_; }
  const Block* row(std::size_t r) const { return data_.get() + r * per_row_; }
  std::size_t blocks_per_row() const { return per_row_; }
  std::size_t num_rows() const { return rows_; }
  bool empty() const { return !data_; }

 private:
  AlignedPtr<Block> data_;
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
