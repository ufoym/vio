// vio::png::zlib - CRC-32 and Adler-32.
//
// crc32() folds 64-byte blocks with carry-less multiplication where the CPU
// has it (x86 PCLMULQDQ, Arm PMULL), else uses the Arm CRC32 instructions or
// braided slicing-by-8 tables.  adler32() sums 32-byte blocks with SSSE3,
// NEON or the generic vector kernel (also SSE2 on x86-64 without SSSE3).
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "simd.hpp"

#if VIO_SIMD_X86
#include <immintrin.h>
#endif
#if VIO_SIMD_NEON
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_PUSH_("fpu=neon")
#endif
#include <arm_neon.h>
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_POP()
#endif
#endif
#if VIO_SIMD_NEON_A64 && (defined(__GNUC__) || defined(__clang__))
#include <arm_acle.h>
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

// Braided CRC (as in zlib 1.3): five 64-bit words at a time, each with its
// own CRC register, the five combined over the last block.  Polynomial
// arithmetic modulo the (bit-reflected) CRC polynomial builds the tables:
// braid_table[k][i] advances byte i at position k of a word over the
// remaining bytes of a five-word block.
constexpr std::uint32_t crc_multmodp(std::uint32_t a, std::uint32_t b) {
  std::uint32_t m = 1u << 31, p = 0;
  for (;;) {
    if (a & m) {
      p ^= b;
      if ((a & (m - 1)) == 0) break;
    }
    m >>= 1;
    b = b & 1 ? (b >> 1) ^ 0xedb88320u : b >> 1;
  }
  return p;
}
// x^(8n) modulo the polynomial
constexpr std::uint32_t crc_x8nmodp(unsigned n) {
  std::uint32_t p = 1u << 31, x8 = 1u << 23;  // x^0, x^8
  for (; n; n >>= 1, x8 = crc_multmodp(x8, x8))
    if (n & 1) p = crc_multmodp(x8, p);
  return p;
}
inline constexpr unsigned kBraids = 5;
constexpr std::array<std::array<std::uint32_t, 256>, 8> make_braid_tables() {
  std::array<std::array<std::uint32_t, 256>, 8> t{};
  for (unsigned k = 0; k < 8; k++) {
    const std::uint32_t p = crc_x8nmodp(kBraids * 8 + 3 - k);
    for (std::uint32_t i = 1; i < 256; i++) t[k][i] = crc_multmodp(i << 24, p);
  }
  return t;
}
inline constexpr auto braid_tables = make_braid_tables();

inline std::uint64_t crc_load_le64(const std::uint8_t* p) {
  std::uint64_t v;
  std::memcpy(&v, p, 8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  v = __builtin_bswap64(v);
#endif
  return v;
}
// The CRC register through one 64-bit word.
inline std::uint32_t crc_word(std::uint64_t v) {
  for (int k = 0; k < 8; k++) v = (v >> 8) ^ crc_tables[0][v & 0xff];
  return static_cast<std::uint32_t>(v);
}

inline std::uint32_t crc32_braided(std::uint32_t crc, const std::uint8_t* buf, std::size_t len) {
  constexpr std::size_t kBlock = kBraids * 8;
  std::size_t blocks = len / kBlock;
  if (blocks >= 2) {
    len -= blocks * kBlock;
    const auto& t = braid_tables;
    std::uint64_t c[kBraids] = {crc};
    while (--blocks) {
      std::uint64_t w[kBraids];
      for (unsigned j = 0; j < kBraids; j++) w[j] = c[j] ^ crc_load_le64(buf + 8 * j);
      buf += kBlock;
      for (unsigned j = 0; j < kBraids; j++) c[j] = t[0][w[j] & 0xff];
      for (unsigned k = 1; k < 8; k++)
        for (unsigned j = 0; j < kBraids; j++) c[j] ^= t[k][(w[j] >> (8 * k)) & 0xff];
    }
    crc = 0;
    for (unsigned j = 0; j < kBraids; j++) crc = crc_word(c[j] ^ crc_load_le64(buf + 8 * j) ^ crc);
    buf += kBlock;
  }
  return crc32_tables(crc, buf, len);
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

#endif

#if VIO_SIMD_NEON
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_PUSH_("fpu=neon")
#endif
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
    c1 = vaddw_u8(c1, vget_high_u8(b0));
    c2 = vaddw_u8(c2, vget_low_u8(b1));
    c3 = vaddw_u8(c3, vget_high_u8(b1));
  }
  static constexpr std::uint16_t w[32] = {32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22,
                                          21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11,
                                          10, 9,  8,  7,  6,  5,  4,  3,  2,  1};
  uint16x8_t w0 = vld1q_u16(w), w1 = vld1q_u16(w + 8), w2 = vld1q_u16(w + 16),
             w3 = vld1q_u16(w + 24);
  uint32x4_t acc = vmull_u16(vget_low_u16(c0), vget_low_u16(w0));
  acc = vmlal_u16(acc, vget_high_u16(c0), vget_high_u16(w0));
  acc = vmlal_u16(acc, vget_low_u16(c1), vget_low_u16(w1));
  acc = vmlal_u16(acc, vget_high_u16(c1), vget_high_u16(w1));
  acc = vmlal_u16(acc, vget_low_u16(c2), vget_low_u16(w2));
  acc = vmlal_u16(acc, vget_high_u16(c2), vget_high_u16(w2));
  acc = vmlal_u16(acc, vget_low_u16(c3), vget_low_u16(w3));
  acc = vmlal_u16(acc, vget_high_u16(c3), vget_high_u16(w3));
  auto hsum = [](uint32x4_t v) {
    const uint64x2_t t = vpaddlq_u32(v);
    return static_cast<std::uint32_t>(vgetq_lane_u64(t, 0) + vgetq_lane_u64(t, 1));
  };
  s2 += static_cast<std::uint32_t>(nblocks * 32) * s1 + 32 * hsum(vs2) + hsum(acc);
  s1 += hsum(vs1);
}
#if VIO_SIMD_NEON_RUNTIME
VIO_TARGET_POP()
#endif
#endif

// Adler-32 sums of whole 32-byte blocks without vector intrinsics (nblocks
// <= 173, as within NMAX bytes).
inline void adler32_blocks(std::uint32_t& s1, std::uint32_t& s2, const std::uint8_t* p, std::size_t nblocks) {
#if defined(__GNUC__) && !defined(__clang__)
  // In a form GCC vectorizes: per byte position k of the blocks, the sum
  // c[k] of its bytes and the sum t[k] of those sums at each block's start;
  // s2 gains 32 * nblocks * s1 + 32 * sum(t) + sum((32 - k) * c[k]).
  std::uint16_t c[32] = {};
  std::uint32_t t[32] = {};
  for (std::size_t i = 0; i < nblocks; i++, p += 32) {
    for (unsigned k = 0; k < 32; k++) t[k] += c[k];
    for (unsigned k = 0; k < 32; k++) c[k] = static_cast<std::uint16_t>(c[k] + p[k]);
  }
  std::uint32_t sum = 0, weighted = 0, tsum = 0;
  for (unsigned k = 0; k < 32; k++) {
    sum += c[k];
    weighted += (32 - k) * c[k];
    tsum += t[k];
  }
  s2 += static_cast<std::uint32_t>(nblocks * 32) * s1 + 32 * tsum + weighted;
  s1 += sum;
#elif defined(__clang__) && (defined(__aarch64__) || defined(_M_ARM64))
  // The n bytes add n * s1 + sum((n - j) * p[j]) to s2: a sum and a sum
  // weighted by the index (within 32 bits for n <= NMAX), which Clang
  // vectorizes well on AArch64.
  const std::uint32_t n = static_cast<std::uint32_t>(nblocks * 32);
  std::uint32_t sum = 0, indexed = 0;
  for (std::uint32_t j = 0; j < n; j++) {
    sum += p[j];
    indexed += j * p[j];
  }
  s2 += n * s1 + n * sum - indexed;
  s1 += sum;
#else
  // 8 bytes per 64-bit word (MSVC and Clang on x86 vectorize the forms
  // above poorly): the running sum of the words' byte sums (each
  // summed by a multiply) and its total over the words, which counts each
  // word's bytes once per word from it to the end, and per byte position k
  // of the words the sum of its bytes (in 16-bit lanes, even and odd
  // positions apart), which corrects for k: the n bytes add
  // n * s1 + 8 * total - sum(k * column[k]) to s2.
  constexpr std::uint64_t kLow = 0x00ff00ff00ff00ffu, kLanes = 0x0001000100010001u;
  const std::size_t nwords = nblocks * 4;
  std::uint64_t run = 0, total = 0, column[8] = {};
  for (std::size_t j0 = 0; j0 < nwords; j0 += 256) {  // 16-bit lanes for 257 words
    const std::size_t j1 = std::min(nwords, j0 + 256);
    std::uint64_t even = 0, odd = 0;
    for (std::size_t j = j0; j < j1; j++) {
      std::uint64_t w;
      std::memcpy(&w, p + 8 * j, 8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      w = __builtin_bswap64(w);
#endif
      const std::uint64_t e = w & kLow, o = (w >> 8) & kLow;
      even += e;
      odd += o;
      run += ((e + o) * kLanes) >> 48;
      total += run;
    }
    for (unsigned k = 0; k < 4; k++) {
      column[2 * k] += (even >> (16 * k)) & 0xffff;
      column[2 * k + 1] += (odd >> (16 * k)) & 0xffff;
    }
  }
  std::uint64_t by_k = 0;
  for (unsigned k = 1; k < 8; k++) by_k += k * column[k];
  s2 += static_cast<std::uint32_t>(nwords * 8 * s1 + 8 * total - by_k);
  s1 += static_cast<std::uint32_t>(run);
#endif
}

// Adler-32 sums of whole 32-byte blocks (nblocks <= 128, which keeps the
// per-position byte sums within signed 16 bits) with a 128-bit vector
// interface V (AltiVec, MMI, RVV).
template <class V>
inline void adler32_vec(std::uint32_t& s1, std::uint32_t& s2, const std::uint8_t* p, std::size_t nblocks) {
  using T = typename V::T;
  const T ones = V::set1_16(1);
  T vs1 = V::zero(), vs2 = vs1, c0 = vs1, c1 = vs1, c2 = vs1, c3 = vs1;
  for (std::size_t i = 0; i < nblocks; i++, p += 32) {
    const T b0 = V::loadu(p), b1 = V::loadu(p + 16);
    const T w0 = V::widenlo_u8(b0), w1 = V::widenhi_u8(b0), w2 = V::widenlo_u8(b1), w3 = V::widenhi_u8(b1);
    vs2 = V::add_32(vs2, vs1);
    vs1 = V::add_32(vs1, V::madd_16(V::add_16(V::add_16(w0, w1), V::add_16(w2, w3)), ones));
    c0 = V::add_16(c0, w0);
    c1 = V::add_16(c1, w1);
    c2 = V::add_16(c2, w2);
    c3 = V::add_16(c3, w3);
  }
  static constexpr std::int16_t w[32] = {32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17,
                                         16, 15, 14, 13, 12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1};
  const T acc = V::add_32(V::add_32(V::madd_16(c0, V::loadu(w)), V::madd_16(c1, V::loadu(w + 8))),
                          V::add_32(V::madd_16(c2, V::loadu(w + 16)), V::madd_16(c3, V::loadu(w + 24))));
  auto hsum = [](T v) {
    std::uint32_t t[4];
    V::storeu(t, v);
    return t[0] + t[1] + t[2] + t[3];
  };
  s2 += static_cast<std::uint32_t>(nblocks * 32) * s1 + 32 * hsum(vs2) + hsum(acc);
  s1 += hsum(vs1);
}

}  // namespace detail

inline std::uint32_t crc32(std::uint32_t crc, const std::uint8_t* buf, std::size_t len) {
  crc = ~crc;
  using namespace jpeg::simd;
#if VIO_SIMD_X86
  if (len >= 64 && simd::has(kPCLMUL) && simd::has(kSSE41)) {
    std::size_t n = len & ~std::size_t(15);
    crc = detail::crc32_pclmul(crc, buf, n);
    buf += n;
    len -= n;
  }
#elif VIO_SIMD_NEON_A64
  if (len >= 64 && simd::has(kPMULL)) {
    std::size_t n = len & ~std::size_t(15);
    crc = detail::crc32_pmull(crc, buf, n);
    buf += n;
    len -= n;
  }
  if (simd::has(kCRC32)) return ~detail::crc32_arm(crc, buf, len);
#endif
  return ~detail::crc32_braided(crc, buf, len);
}

inline std::uint32_t adler32(std::uint32_t adler, const std::uint8_t* buf, std::size_t len) {
  constexpr std::uint32_t BASE = 65521;
  constexpr std::size_t NMAX = 5552;
  std::uint32_t a = adler & 0xffff, b = (adler >> 16) & 0xffff;
  using namespace jpeg::simd;
#if VIO_SIMD_X86_64
  const bool ssse3 = simd::has(kSSSE3), vec = simd::has(kSSE2);
#elif VIO_SIMD_X86
  const bool vec = simd::has(kSSSE3);
#elif VIO_SIMD_NEON
  const bool vec = simd::has(kNEON);
#elif VIO_PNG_GENERIC
  const bool vec = simd::has(simd::kGenericIsa);
#else
  const bool vec = false;
#endif
  while (len > 0) {
    std::size_t n = len < NMAX ? len : NMAX;
    len -= n;
    const std::size_t blocks = n / 32;
    if (!vec) {
      detail::adler32_blocks(a, b, buf, blocks);
    } else {
#if VIO_SIMD_X86_64
      if (ssse3) {
        detail::adler32_ssse3(a, b, buf, blocks);
      } else {
        for (std::size_t k = 0; k < blocks; k += 128)
          detail::adler32_vec<simd::sse2::V>(a, b, buf + k * 32, std::min<std::size_t>(128, blocks - k));
      }
#elif VIO_SIMD_X86
      detail::adler32_ssse3(a, b, buf, blocks);
#elif VIO_SIMD_NEON
      detail::adler32_neon(a, b, buf, blocks);
#elif VIO_PNG_GENERIC
      // (at most 128 blocks at a time; NMAX bytes have 173)
      for (std::size_t k = 0; k < blocks; k += 128)
        detail::adler32_vec<simd::GV>(a, b, buf + k * 32, std::min<std::size_t>(128, blocks - k));
#endif
    }
    buf += blocks * 32;
    n -= blocks * 32;
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
