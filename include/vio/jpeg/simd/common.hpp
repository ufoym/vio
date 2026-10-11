// vio::jpeg::simd - definitions shared by the scalar code and all SIMD back
// ends.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "../config.hpp"

namespace vio::jpeg::simd {

// Sequential Huffman encoder bit buffer (64-bit put_buffer/free_bits).
struct HuffBitState {
  std::uint64_t put_buffer = 0;
  int free_bits = 64;
};

// Appends codes to an output buffer that has room for at least one block's
// worth of output, stuffing a zero byte after each 0xFF.
struct HuffBitWriter {
  std::uint64_t put_buffer;
  int free_bits;
  std::uint8_t* buffer;

  VIO_INLINE HuffBitWriter(const HuffBitState& st, std::uint8_t* out)
      : put_buffer(st.put_buffer), free_bits(st.free_bits), buffer(out) {}
  VIO_INLINE HuffBitState state() const { return {put_buffer, free_bits}; }

  VIO_INLINE void flush() {
    if (put_buffer & 0x8080808080808080ULL & ~(put_buffer + 0x0101010101010101ULL)) {
      for (int sh = 56; sh >= 0; sh -= 8) {
        std::uint8_t b = static_cast<std::uint8_t>(put_buffer >> sh);
        buffer[0] = b;
        buffer[1] = 0;
        buffer += 1 + (b == 0xFF);
      }
    } else {
      std::uint64_t be = to_big_endian(put_buffer);
      std::memcpy(buffer, &be, 8);
      buffer += 8;
    }
  }

  // Appends the low `size` bits of `code` (code must have no higher bits set).
  VIO_INLINE void put_bits(std::uint64_t code, int size) {
    free_bits -= size;
    if (free_bits < 0) {
      put_buffer = (put_buffer << (size + free_bits)) | (code >> -free_bits);
      flush();
      free_bits += 64;
      put_buffer = code;
    } else {
      put_buffer = (put_buffer << size) | code;
    }
  }

  // Appends a Huffman code followed by `nbits` value bits (already masked).
  VIO_INLINE void put_code(unsigned code, int size, unsigned bits, int nbits) {
    put_bits((static_cast<std::uint64_t>(code) << nbits) | bits, size + nbits);
  }

  static VIO_INLINE std::uint64_t to_big_endian(std::uint64_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return v;
#else
    return bswap64(v);
#endif
  }
  static VIO_INLINE std::uint64_t bswap64(std::uint64_t v) {
#if defined(_MSC_VER) && !defined(__clang__)
    return _byteswap_uint64(v);
#else
    return __builtin_bswap64(v);
#endif
  }
};

// ---------------------------------------------------------------------------
// Kernel tables filled in by the back ends (null entries: use scalar code).

// Forward DCT and quantization (8-bit samples, 16-bit DCT elements).
struct FdctKernels {
  void (*convsamp)(std::uint8_t** sample_data, unsigned start_col, std::int16_t* ws) = nullptr;
  void (*fdct_islow)(std::int16_t* data) = nullptr;
  void (*fdct_ifast)(std::int16_t* data) = nullptr;
  // divisors: 16-bit reciprocal/correction/scale/shift table
  void (*quantize)(std::int16_t* coef, const std::uint16_t* divisors,
                   const std::int16_t* ws) = nullptr;
  void (*convsamp_float)(std::uint8_t** sample_data, unsigned start_col, float* ws) = nullptr;
  void (*fdct_float)(float* data) = nullptr;
  void (*quantize_float)(std::int16_t* coef, const float* divisors, const float* ws) = nullptr;
};

// Progressive Huffman AC scans (see neon/phuff.hpp for the contracts).
struct PhuffKernels {
  std::uint64_t (*ac_first_prepare)(const std::int16_t* block, const int* order, int Sl, int Al,
                                    std::uint16_t* values) = nullptr;
  int (*ac_refine_prepare)(const std::int16_t* block, const int* order, int Sl, int Al,
                           std::uint16_t* absvalues, std::uint64_t* zerobits,
                           std::uint64_t* signbits) = nullptr;
};

// Sequential Huffman encoding of one block; returns the advanced pointer.
template <class Table>
using HuffEncodeFn = std::uint8_t* (*)(HuffBitState& st, std::uint8_t* buffer,
                                       const std::int16_t* block, int last_dc_val,
                                       const Table& dctbl, const Table& actbl);

// Inverse DCT with the decoder's IdctFn<uint8_t> signature.  dct_table points
// at ComponentInfo::dct_table; the 16-bit multipliers used by the integer
// kernels immediately follow it (kDctTableBytes later).
using IdctFn8 = void (*)(const void* dct_table, const std::int16_t* coef, std::uint8_t** out,
                         unsigned col, const std::uint8_t* range_limit, int scaling);
inline constexpr std::size_t kDctTableBytes = 64 * 8;

template <void (*F)(const std::int16_t*, const std::int16_t*, std::uint8_t**, unsigned)>
inline void idct16_adapter(const void* dct_table, const std::int16_t* coef_block,
                           std::uint8_t** output_buf, unsigned output_col, const std::uint8_t*,
                           int) {
  F(reinterpret_cast<const std::int16_t*>(static_cast<const char*>(dct_table) + kDctTableBytes),
    coef_block, output_buf, output_col);
}

template <void (*F)(const float*, const std::int16_t*, std::uint8_t**, unsigned)>
inline void idct_float_adapter(const void* dct_table, const std::int16_t* coef_block,
                               std::uint8_t** output_buf, unsigned output_col,
                               const std::uint8_t*, int) {
  F(static_cast<const float*>(dct_table), coef_block, output_buf, output_col);
}

struct IdctKernels {
  IdctFn8 islow = nullptr, ifast = nullptr, flt = nullptr, s4 = nullptr, s2 = nullptr;
};

}  // namespace vio::jpeg::simd
