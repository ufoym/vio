// vio::png::zlib - CRC-32 and Adler-32.
//
// crc32() folds 64-byte blocks with carry-less multiplication where the CPU
// has it (x86 PCLMULQDQ, Arm PMULL), else uses the Arm CRC32 instructions or
// slicing-by-8 tables.  adler32() sums 32-byte blocks with SSSE3 or NEON.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../jpeg/config.hpp"
#include "../jpeg/simd/cpu.hpp"

#if VIO_SIMD_X86
#include <immintrin.h>
#endif
#if VIO_SIMD_NEON_A64
#include <arm_neon.h>
#if defined(__GNUC__) || defined(__clang__)
#include <arm_acle.h>
#endif
#endif

// Function attributes for the optional Arm extensions.
#if VIO_SIMD_NEON_A64 && defined(__clang__)
#define VIO_TARGET_ARM_CRC __attribute__((target("crc")))
#define VIO_TARGET_ARM_PMULL __attribute__((target("aes")))
#elif VIO_SIMD_NEON_A64 && defined(__GNUC__)
#define VIO_TARGET_ARM_CRC __attribute__((target("+crc")))
#define VIO_TARGET_ARM_PMULL __attribute__((target("+crypto")))
#else
#define VIO_TARGET_ARM_CRC
#define VIO_TARGET_ARM_PMULL
#endif

namespace vio::png::zlib {

namespace detail {

constexpr std::array<std::array<std::uint32_t, 256>, 8> make_crc_tables() {
  std::array<std::array<std::uint32_t, 256>, 8> t{};
  for (std::uint32_t n = 0; n < 256; n++) {
    std::uint32_t c = n;
    for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    t[0][n] = c;
  }
  for (std::uint32_t n = 0; n < 256; n++)
    for (int k = 1; k < 8; k++) t[k][n] = (t[k - 1][n] >> 8) ^ t[0][t[k - 1][n] & 0xff];
  return t;
}
inline constexpr auto crc_tables = make_crc_tables();

// The CRC register (pre- and post-inversion are left to the caller).
inline std::uint32_t crc32_tables(std::uint32_t crc, const std::uint8_t* buf, std::size_t len) {
  const auto& t = crc_tables;
  while (len >= 8) {
    std::uint32_t lo = crc ^ (std::uint32_t(buf[0]) | std::uint32_t(buf[1]) << 8 |
                              std::uint32_t(buf[2]) << 16 | std::uint32_t(buf[3]) << 24);
    crc = t[7][lo & 0xff] ^ t[6][(lo >> 8) & 0xff] ^ t[5][(lo >> 16) & 0xff] ^ t[4][lo >> 24] ^
          t[3][buf[4]] ^ t[2][buf[5]] ^ t[1][buf[6]] ^ t[0][buf[7]];
    buf += 8;
    len -= 8;
  }
  while (len--) crc = t[0][(crc ^ *buf++) & 0xff] ^ (crc >> 8);
  return crc;
}

// Folding constants for the bit-reflected CRC-32 polynomial: x^(512+32),
// x^(512-32) mod P (4-way fold), x^(128+32), x^(128-32) mod P (1-way fold),
// x^64 mod P, and the Barrett reduction pair P' and floor(x^64 / P).
alignas(16) inline constexpr std::uint64_t crc_k1k2[2] = {0x0154442bd4, 0x01c6e41596};
alignas(16) inline constexpr std::uint64_t crc_k3k4[2] = {0x01751997d0, 0x00ccaa009e};
alignas(16) inline constexpr std::uint64_t crc_k5k0[2] = {0x0163cd6124, 0x0000000000};
alignas(16) inline constexpr std::uint64_t crc_poly[2] = {0x01db710641, 0x01f7011641};

#if VIO_SIMD_X86
VIO_TARGET_PUSH_("sse2,sse4.1,pclmul")
// len >= 64, a multiple of 16.
inline std::uint32_t crc32_pclmul(std::uint32_t crc, const std::uint8_t* buf, std::size_t len) {
  __m128i x0, x1, x2, x3, x4, x5, x6, x7, x8;
  auto load = [](const std::uint8_t* p) { return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p)); };
  x1 = _mm_xor_si128(load(buf), _mm_cvtsi32_si128(static_cast<int>(crc)));
  x2 = load(buf + 16);
  x3 = load(buf + 32);
  x4 = load(buf + 48);
  x0 = _mm_load_si128(reinterpret_cast<const __m128i*>(crc_k1k2));
  buf += 64;
  len -= 64;
  while (len >= 64) {
    x5 = _mm_clmulepi64_si128(x1, x0, 0x00);
    x6 = _mm_clmulepi64_si128(x2, x0, 0x00);
    x7 = _mm_clmulepi64_si128(x3, x0, 0x00);
    x8 = _mm_clmulepi64_si128(x4, x0, 0x00);
    x1 = _mm_clmulepi64_si128(x1, x0, 0x11);
    x2 = _mm_clmulepi64_si128(x2, x0, 0x11);
    x3 = _mm_clmulepi64_si128(x3, x0, 0x11);
    x4 = _mm_clmulepi64_si128(x4, x0, 0x11);
    x1 = _mm_xor_si128(_mm_xor_si128(x1, x5), load(buf));
    x2 = _mm_xor_si128(_mm_xor_si128(x2, x6), load(buf + 16));
    x3 = _mm_xor_si128(_mm_xor_si128(x3, x7), load(buf + 32));
    x4 = _mm_xor_si128(_mm_xor_si128(x4, x8), load(buf + 48));
    buf += 64;
    len -= 64;
  }
  x0 = _mm_load_si128(reinterpret_cast<const __m128i*>(crc_k3k4));
  auto fold = [&](__m128i a, __m128i b) {
    __m128i lo = _mm_clmulepi64_si128(a, x0, 0x00);
    __m128i hi = _mm_clmulepi64_si128(a, x0, 0x11);
    return _mm_xor_si128(_mm_xor_si128(hi, lo), b);
  };
  x1 = fold(x1, x2);
  x1 = fold(x1, x3);
  x1 = fold(x1, x4);
  while (len >= 16) {
    x1 = fold(x1, load(buf));
    buf += 16;
    len -= 16;
  }
  // 128 -> 64 bits
  x2 = _mm_clmulepi64_si128(x1, x0, 0x10);
  x3 = _mm_setr_epi32(~0, 0, ~0, 0);
  x1 = _mm_xor_si128(_mm_srli_si128(x1, 8), x2);
  x0 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(crc_k5k0));
  x2 = _mm_srli_si128(x1, 4);
  x1 = _mm_and_si128(x1, x3);
  x1 = _mm_xor_si128(_mm_clmulepi64_si128(x1, x0, 0x00), x2);
  // Barrett reduction to 32 bits
  x0 = _mm_load_si128(reinterpret_cast<const __m128i*>(crc_poly));
  x2 = _mm_and_si128(x1, x3);
  x2 = _mm_clmulepi64_si128(x2, x0, 0x10);
  x2 = _mm_and_si128(x2, x3);
  x2 = _mm_clmulepi64_si128(x2, x0, 0x00);
  x1 = _mm_xor_si128(x1, x2);
  return static_cast<std::uint32_t>(_mm_extract_epi32(x1, 1));
}
VIO_TARGET_POP()

VIO_TARGET_PUSH_("sse2,ssse3")
// Adler-32 sums of whole 32-byte blocks (nblocks <= 173).
inline void adler32_ssse3(std::uint32_t& s1, std::uint32_t& s2, const std::uint8_t* p,
                          std::size_t nblocks) {
  const __m128i w_hi = _mm_setr_epi8(32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17);
  const __m128i w_lo = _mm_setr_epi8(16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1);
  const __m128i ones = _mm_set1_epi16(1), zero = _mm_setzero_si128();
  __m128i vs1 = zero, vs2 = zero, vw = zero;
  for (std::size_t i = 0; i < nblocks; i++, p += 32) {
    __m128i b0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
    __m128i b1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + 16));
    vs2 = _mm_add_epi32(vs2, vs1);
    vs1 = _mm_add_epi32(vs1, _mm_add_epi32(_mm_sad_epu8(b0, zero), _mm_sad_epu8(b1, zero)));
    vw = _mm_add_epi32(vw, _mm_madd_epi16(_mm_add_epi16(_mm_maddubs_epi16(b0, w_hi),
                                                        _mm_maddubs_epi16(b1, w_lo)),
                                          ones));
  }
  auto hsum = [](__m128i v) {
    v = _mm_add_epi32(v, _mm_shuffle_epi32(v, 0x4e));
    v = _mm_add_epi32(v, _mm_shuffle_epi32(v, 0xb1));
    return static_cast<std::uint32_t>(_mm_cvtsi128_si32(v));
  };
  s2 += static_cast<std::uint32_t>(nblocks * 32) * s1 + 32 * hsum(vs2) + hsum(vw);
  s1 += hsum(vs1);
}
VIO_TARGET_POP()
#endif

#if VIO_SIMD_NEON_A64
VIO_TARGET_ARM_PMULL inline uint64x2_t clmul_lo(uint64x2_t a, uint64x2_t b) {
  return vreinterpretq_u64_p128(vmull_p64(vgetq_lane_u64(a, 0), vgetq_lane_u64(b, 0)));
}
VIO_TARGET_ARM_PMULL inline uint64x2_t clmul_hi(uint64x2_t a, uint64x2_t b) {
  return vreinterpretq_u64_p128(vmull_p64(vgetq_lane_u64(a, 1), vgetq_lane_u64(b, 1)));
}
// a.lo * b.hi
VIO_TARGET_ARM_PMULL inline uint64x2_t clmul_lohi(uint64x2_t a, uint64x2_t b) {
  return vreinterpretq_u64_p128(vmull_p64(vgetq_lane_u64(a, 0), vgetq_lane_u64(b, 1)));
}
VIO_TARGET_ARM_PMULL inline uint64x2_t crc_fold(uint64x2_t a, uint64x2_t k, uint64x2_t b) {
  return veorq_u64(veorq_u64(clmul_hi(a, k), clmul_lo(a, k)), b);
}
inline uint64x2_t load_u64x2(const std::uint8_t* p) { return vreinterpretq_u64_u8(vld1q_u8(p)); }

// len >= 64, a multiple of 16.
VIO_TARGET_ARM_PMULL
inline std::uint32_t crc32_pmull(std::uint32_t crc, const std::uint8_t* buf, std::size_t len) {
  uint64x2_t x0, x1, x2, x3, x4;
  x1 = veorq_u64(load_u64x2(buf), vreinterpretq_u64_u32(vsetq_lane_u32(crc, vdupq_n_u32(0), 0)));
  x2 = load_u64x2(buf + 16);
  x3 = load_u64x2(buf + 32);
  x4 = load_u64x2(buf + 48);
  x0 = vld1q_u64(crc_k1k2);
  buf += 64;
  len -= 64;
  while (len >= 64) {
    x1 = crc_fold(x1, x0, load_u64x2(buf));
    x2 = crc_fold(x2, x0, load_u64x2(buf + 16));
    x3 = crc_fold(x3, x0, load_u64x2(buf + 32));
    x4 = crc_fold(x4, x0, load_u64x2(buf + 48));
    buf += 64;
    len -= 64;
  }
  x0 = vld1q_u64(crc_k3k4);
  x1 = crc_fold(x1, x0, x2);
  x1 = crc_fold(x1, x0, x3);
  x1 = crc_fold(x1, x0, x4);
  while (len >= 16) {
    x1 = crc_fold(x1, x0, load_u64x2(buf));
    buf += 16;
    len -= 16;
  }
  const uint8x16_t zero = vdupq_n_u8(0);
  const uint64x2_t mask = vreinterpretq_u64_u32(vcombine_u32(vcreate_u32(0xffffffffu), vcreate_u32(0xffffffffu)));
  // 128 -> 64 bits
  x2 = clmul_lohi(x1, x0);
  x1 = veorq_u64(vreinterpretq_u64_u8(vextq_u8(vreinterpretq_u8_u64(x1), zero, 8)), x2);
  x0 = vld1q_u64(crc_k5k0);
  x2 = vreinterpretq_u64_u8(vextq_u8(vreinterpretq_u8_u64(x1), zero, 4));
  x1 = vandq_u64(x1, mask);
  x1 = veorq_u64(clmul_lo(x1, x0), x2);
  // Barrett reduction to 32 bits
  x0 = vld1q_u64(crc_poly);
  x2 = vandq_u64(x1, mask);
  x2 = vandq_u64(clmul_lohi(x2, x0), mask);
  x2 = clmul_lo(x2, x0);
  x1 = veorq_u64(x1, x2);
  return vgetq_lane_u32(vreinterpretq_u32_u64(x1), 1);
}

VIO_TARGET_ARM_CRC
inline std::uint32_t crc32_arm(std::uint32_t crc, const std::uint8_t* buf, std::size_t len) {
  while (len >= 8) {
    std::uint64_t v;
    std::memcpy(&v, buf, 8);
    crc = __crc32d(crc, v);
    buf += 8;
    len -= 8;
  }
  while (len--) crc = __crc32b(crc, *buf++);
  return crc;
}

// Adler-32 sums of whole 32-byte blocks (nblocks <= 173).
inline void adler32_neon(std::uint32_t& s1, std::uint32_t& s2, const std::uint8_t* p,
                         std::size_t nblocks) {
  uint32x4_t vs1 = vdupq_n_u32(0), vs2 = vdupq_n_u32(0);
  uint16x8_t c0 = vdupq_n_u16(0), c1 = c0, c2 = c0, c3 = c0;
  for (std::size_t i = 0; i < nblocks; i++, p += 32) {
    uint8x16_t b0 = vld1q_u8(p), b1 = vld1q_u8(p + 16);
    vs2 = vaddq_u32(vs2, vs1);
    vs1 = vpadalq_u16(vs1, vpadalq_u8(vpaddlq_u8(b0), b1));
    c0 = vaddw_u8(c0, vget_low_u8(b0));
    c1 = vaddw_high_u8(c1, b0);
    c2 = vaddw_u8(c2, vget_low_u8(b1));
    c3 = vaddw_high_u8(c3, b1);
  }
  static constexpr std::uint16_t w[32] = {32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22,
                                          21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11,
                                          10, 9,  8,  7,  6,  5,  4,  3,  2,  1};
  uint16x8_t w0 = vld1q_u16(w), w1 = vld1q_u16(w + 8), w2 = vld1q_u16(w + 16),
             w3 = vld1q_u16(w + 24);
  uint32x4_t acc = vmull_u16(vget_low_u16(c0), vget_low_u16(w0));
  acc = vmlal_high_u16(acc, c0, w0);
  acc = vmlal_u16(acc, vget_low_u16(c1), vget_low_u16(w1));
  acc = vmlal_high_u16(acc, c1, w1);
  acc = vmlal_u16(acc, vget_low_u16(c2), vget_low_u16(w2));
  acc = vmlal_high_u16(acc, c2, w2);
  acc = vmlal_u16(acc, vget_low_u16(c3), vget_low_u16(w3));
  acc = vmlal_high_u16(acc, c3, w3);
  s2 += static_cast<std::uint32_t>(nblocks * 32) * s1 + 32 * vaddvq_u32(vs2) + vaddvq_u32(acc);
  s1 += vaddvq_u32(vs1);
}
#endif

}  // namespace detail

inline std::uint32_t crc32(std::uint32_t crc, const std::uint8_t* buf, std::size_t len) {
  crc = ~crc;
  [[maybe_unused]] const unsigned flags = jpeg::simd::cpu_flags();
#if VIO_SIMD_X86
  if (len >= 64 && (flags & jpeg::simd::kPCLMUL) && (flags & jpeg::simd::kSSE41)) {
    std::size_t n = len & ~std::size_t(15);
    crc = detail::crc32_pclmul(crc, buf, n);
    buf += n;
    len -= n;
  }
#elif VIO_SIMD_NEON_A64
  if (len >= 64 && (flags & jpeg::simd::kPMULL)) {
    std::size_t n = len & ~std::size_t(15);
    crc = detail::crc32_pmull(crc, buf, n);
    buf += n;
    len -= n;
  }
  if (flags & jpeg::simd::kCRC32) return ~detail::crc32_arm(crc, buf, len);
#endif
  return ~detail::crc32_tables(crc, buf, len);
}

inline std::uint32_t adler32(std::uint32_t adler, const std::uint8_t* buf, std::size_t len) {
  constexpr std::uint32_t BASE = 65521;
  constexpr std::size_t NMAX = 5552;
  std::uint32_t a = adler & 0xffff, b = (adler >> 16) & 0xffff;
  [[maybe_unused]] const unsigned flags = jpeg::simd::cpu_flags();
  while (len > 0) {
    std::size_t n = len < NMAX ? len : NMAX;
    len -= n;
    std::size_t blocks = n / 32;
#if VIO_SIMD_X86
    if (flags & jpeg::simd::kSSSE3) {
      detail::adler32_ssse3(a, b, buf, blocks);
      buf += blocks * 32;
      n -= blocks * 32;
    }
#elif VIO_SIMD_NEON_A64
    detail::adler32_neon(a, b, buf, blocks);
    buf += blocks * 32;
    n -= blocks * 32;
#endif
    (void)blocks;
    while (n--) {
      a += *buf++;
      b += a;
    }
    a %= BASE;
    b %= BASE;
  }
  return (b << 16) | a;
}

}  // namespace vio::png::zlib
