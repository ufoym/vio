// vio::png::zlib - DEFLATE decompression (RFC 1950/1951).
//
// Inflater decodes into a caller-provided buffer of known capacity, which is
// what PNG needs: the size of the decompressed image data is known up front.
//
// The Huffman decoder uses a 64-bit bit buffer refilled eight bytes at a time
// without branches, a 12-bit literal/length table whose entries hold up to
// two literals (pairs are written once, at the table size of their combined
// code length, and reach their other places as the table is doubled), an
// 8-bit distance table (longer codes go through
// sub-tables), and match copies in 16-byte (or, for short distances,
// repeated-pattern) stores.  A careful byte-at-a-time loop finishes the
// last few hundred bytes of input and output.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "../core.hpp"
#include "checksum.hpp"


namespace vio::png::zlib {

namespace detail {

// Decode table entries (32 bits):
//   bits 0..5    number of bits the entry consumes (code plus extra bits)
//   bits 8..12   code length (the extra bits follow the code); for a pair of
//                literals, the length of the first one's code
//   bit 13       exceptional: end of block, sub-table pointer or invalid code
//   bit 14       literal: two literals; exceptional: sub-table pointer
//   bit 15       literal
//   bits 16..31  literal(s) (bits 16..23 and 24..31), base length or
//                distance, or the sub-table's offset
// A sub-table pointer holds the main-table bits in bits 0..5 and the
// sub-table's index bits in bits 8..11; sub-table entries describe the whole
// code, main-table bits included.
enum : std::uint32_t {
  kEntExceptional = 1u << 13,
  kEntSecond = 1u << 14,
  kEntLiteral = 1u << 15,
  kEntEndOfBlock = 1u << 12,
};

inline constexpr int kLitBits = 12, kDistBits = 8, kCodeLenBits = 7;
// Main table plus room for every possible sub-table.
inline constexpr int kLitTableSize = (1 << kLitBits) + 288 * (1 << (15 - kLitBits));
inline constexpr int kDistTableSize = (1 << kDistBits) + 32 * (1 << (15 - kDistBits));

inline constexpr std::uint16_t len_base[29] = {3,  4,  5,  6,  7,  8,  9,  10,  11,  13,
                                               15, 17, 19, 23, 27, 31, 35, 43,  51,  59,
                                               67, 83, 99, 115, 131, 163, 195, 227, 258};
inline constexpr std::uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
inline constexpr std::uint16_t dist_base[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
inline constexpr std::uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

enum class Code { kLitLen, kDist, kCodeLen };

// Entry for `sym` with a code of `len` bits, before the length fields.
inline std::uint32_t symbol_entry(Code kind, unsigned sym, unsigned& extra) {
  extra = 0;
  if (kind == Code::kCodeLen) return sym << 16;
  if (kind == Code::kLitLen) {
    if (sym < 256) return kEntLiteral | sym << 16;
    if (sym == 256) return kEntExceptional | kEntEndOfBlock;
    if (sym >= 286) return kEntExceptional;
    extra = len_extra[sym - 257];
    return std::uint32_t(len_base[sym - 257]) << 16;
  }
  if (sym >= 30) return kEntExceptional;
  extra = dist_extra[sym];
  return std::uint32_t(dist_base[sym]) << 16;
}

// Next codeword of the same length in bit-reversed form (codes are sent
// most significant bit first, so table indices are reversed codewords).
VIO_INLINE unsigned next_reversed(unsigned rev, int len) {
  unsigned bit = 1u << (len - 1);
  while (rev & bit) bit >>= 1;
  return (rev & (bit - 1)) | bit;
}

// Builds a decode table from code lengths.  Returns false for an
// over-subscribed code, or an incomplete one other than a single 1-bit code
// (accepted for literal/length and distance codes, as is an empty code).
inline bool build_table(std::uint32_t* table, const std::uint8_t* lens, unsigned n, Code kind,
                        int table_bits) {
  unsigned count[16] = {};
  for (unsigned i = 0; i < n; i++) count[lens[i]]++;
  count[0] = 0;
  int left = 1, max_len = 0;
  for (int len = 1; len < 16; len++) {
    left = 2 * left - static_cast<int>(count[len]);
    if (left < 0) return false;
    if (count[len]) max_len = len;
  }
  if (max_len != 0 && left > 0 && (kind == Code::kCodeLen || max_len != 1)) return false;
  unsigned offs[16];
  offs[1] = 0;
  for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + count[len];
  unsigned start[16];
  std::memcpy(start, offs, sizeof(start));
  std::uint16_t sorted[288];
  for (unsigned i = 0; i < n; i++)
    if (lens[i]) sorted[offs[lens[i]]++] = static_cast<std::uint16_t>(i);
  const unsigned main_size = 1u << table_bits;
  const unsigned total = offs[15];  // number of codes
  auto entry = [&](unsigned sym, int len) {
    unsigned extra;
    std::uint32_t e = symbol_entry(kind, sym, extra);
    return e | static_cast<std::uint32_t>(len) << 8 | (len + extra);
  };
  // Codes up to table_bits long: fill the first 2^len entries for each
  // length, doubling the filled part when moving to the next length.
  table[0] = table[1] = kEntExceptional;
  unsigned filled = 2, idx = 0, rev = 0;
  std::uint16_t rev_of[288];
  int len = 1;
  for (; len <= table_bits && len <= max_len; len++) {
    if (len > 1) {
      std::memcpy(table + filled, table, filled * sizeof(std::uint32_t));
      filled *= 2;
    }
    for (unsigned k = 0; k < count[len]; k++) {
      table[rev] = entry(sorted[idx], len);
      rev_of[idx] = static_cast<std::uint16_t>(rev);
      if (++idx < total) rev = next_reversed(rev, len);
    }
    if (kind == Code::kLitLen) {
      // Pairs of literals whose codes total len bits, written once here:
      // the doubling copies give them their other places.  (Within a
      // length, symbols are in increasing order: literals come first.)
      for (int l1 = 1; l1 < len; l1++) {
        const int l2 = len - l1;
        for (unsigned i1 = start[l1]; i1 < start[l1] + count[l1] && sorted[i1] < 256; i1++) {
          const std::uint32_t first = kEntLiteral | kEntSecond | std::uint32_t(sorted[i1]) << 16 |
                                      static_cast<std::uint32_t>(l1) << 8 | static_cast<std::uint32_t>(len);
          const unsigned r1 = rev_of[i1];
          for (unsigned i2 = start[l2]; i2 < start[l2] + count[l2] && sorted[i2] < 256; i2++)
            table[r1 | static_cast<unsigned>(rev_of[i2]) << l1] = first | std::uint32_t(sorted[i2]) << 24;
        }
      }
    }
  }
  while (filled < main_size) {
    std::memcpy(table + filled, table, filled * sizeof(std::uint32_t));
    filled *= 2;
  }
  // Longer codes: one sub-table per main-table prefix, just large enough
  // for the codes that share it.
  unsigned next = main_size, sub_start = 0, sub_bits = 0, prefix = ~0u;
  unsigned remaining[16];
  std::memcpy(remaining, count, sizeof(remaining));
  for (; len <= max_len; len++) {
    for (unsigned k = 0; k < count[len]; k++, idx++) {
      if ((rev & (main_size - 1)) != prefix) {
        prefix = rev & (main_size - 1);
        sub_bits = static_cast<unsigned>(len - table_bits);
        unsigned used = remaining[len];
        while (used < (1u << sub_bits)) {
          sub_bits++;
          used = 2 * used + remaining[table_bits + sub_bits];
        }
        sub_start = next;
        next += 1u << sub_bits;
        table[prefix] = kEntExceptional | kEntSecond | sub_start << 16 | sub_bits << 8 |
                        static_cast<unsigned>(table_bits);
      }
      std::uint32_t e = entry(sorted[idx], len);
      for (unsigned fill = rev >> table_bits; fill < (1u << sub_bits);
           fill += 1u << (len - table_bits))
        table[sub_start + fill] = e;
      remaining[len]--;
      if (idx + 1 < total) rev = next_reversed(rev, len);
    }
  }
  return true;
}

struct FixedTables {
  std::uint32_t lit[kLitTableSize];
  std::uint32_t dist[kDistTableSize];
  FixedTables() {
    std::uint8_t l[288];
    int i = 0;
    for (; i < 144; i++) l[i] = 8;
    for (; i < 256; i++) l[i] = 9;
    for (; i < 280; i++) l[i] = 7;
    for (; i < 288; i++) l[i] = 8;
    build_table(lit, l, 288, Code::kLitLen, kLitBits);
    for (i = 0; i < 32; i++) l[i] = 5;
    build_table(dist, l, 32, Code::kDist, kDistBits);
  }
};

inline const FixedTables& fixed_tables() {
  static const FixedTables t;
  return t;
}

VIO_INLINE std::uint64_t load_le64(const std::uint8_t* p) {
  std::uint64_t v;
  std::memcpy(&v, p, 8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  v = __builtin_bswap64(v);
#endif
  return v;
}

// Plain data movement (16-byte copies, byte fills) uses vector registers
// wherever the target has them, whatever the JSIMD_* variables select; the
// compiler emits the same instructions for the memcpy forms on x86.
VIO_INLINE void copy16(std::uint8_t* d, const std::uint8_t* s) {
#if VIO_SIMD_NEON && !VIO_SIMD_NEON_RUNTIME
  vst1q_u8(d, vld1q_u8(s));
#else
  std::uint8_t t[16];
  std::memcpy(t, s, 16);
  std::memcpy(d, t, 16);
#endif
}

#if VIO_SIMD_NEON_A64
// Repeats the dist-byte pattern before out (dist < 8) through 16-byte
// vectors advanced by the largest multiple of dist that fits.
VIO_INLINE void copy_pattern_neon(std::uint8_t* out, std::uint8_t* end, std::size_t dist) {
  static constexpr std::uint8_t idx[8][16] = {
      {}, {},
      {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
      {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0},
      {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3},
      {0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0},
      {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3},
      {0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1}};
  static constexpr std::uint8_t step[8] = {0, 0, 16, 15, 16, 15, 12, 14};
  const uint8x16_t v = vqtbl1q_u8(vld1q_u8(out - dist), vld1q_u8(idx[dist]));
  const unsigned s = step[dist];
  do {
    vst1q_u8(out, v);
    out += s;
  } while (out < end);
}
#endif

// Copies a match of `len` bytes from `dist` bytes back; may write up to 15
// bytes past the end of the match.  `pattern_simd`: copy_pattern_neon for
// distances under 8 (when NEON is enabled).
VIO_INLINE void copy_match(std::uint8_t* out, std::size_t dist, unsigned len, [[maybe_unused]] bool pattern_simd) {
  std::uint8_t* end = out + len;
  const std::uint8_t* src = out - dist;
  if (dist >= 16) {
    do {
      copy16(out, src);
      out += 16;
      src += 16;
    } while (out < end);
  } else if (dist == 1) {
#if VIO_SIMD_NEON && !VIO_SIMD_NEON_RUNTIME
    const uint8x16_t v = vdupq_n_u8(*src);
    do {
      vst1q_u8(out, v);
      out += 16;
    } while (out < end);
#else
    std::uint8_t v[16];
    std::memset(v, *src, 16);
    do {
      std::memcpy(out, v, 16);
      out += 16;
    } while (out < end);
#endif
  } else if (dist >= 8) {
    do {
      std::uint64_t v;
      std::memcpy(&v, src, 8);
      std::memcpy(out, &v, 8);
      out += 8;
      src += 8;
    } while (out < end);
  } else {
#if VIO_SIMD_NEON_A64
    if (pattern_simd) return copy_pattern_neon(out, end, dist);
#endif
    // 16 bytes of the pattern, stored at steps of the largest multiple of
    // dist that fits
    std::uint8_t v[16];
    for (unsigned i = 0; i < 16; i++) v[i] = src[i % dist];
    const std::size_t step = 16 - 16 % dist;
    do {
      std::memcpy(out, v, 16);
      out += step;
    } while (out < end);
  }
}

// The state of the fast loop (inflate_fast.inc).
struct FastState {
  const std::uint8_t* in;
  std::uint8_t* out;
  std::uint64_t bitbuf;
  unsigned bitcnt;
  const std::uint8_t* in_fast_end;
  std::uint8_t* out_fast_end;
  const std::uint8_t* out_begin;
  bool pattern_simd;
};
[[noreturn]] inline void inflate_fail(const char* msg) { VIO_FAIL("zlib: %s", msg); }

#define VIO_PNG_INFLATE_FAST inflate_fast
#include "inflate_fast.inc"
#undef VIO_PNG_INFLATE_FAST
// The same, with BMI2 shifts (shlx, shrx: one micro-op each, unlike shifts
// by cl) where the CPU has them (not at the JSIMD_FORCESSE2 level).
#if VIO_SIMD_X86_64 && (defined(__GNUC__) || defined(__clang__))
#define VIO_PNG_INFLATE_BMI2 1
VIO_TARGET_PUSH_("bmi,bmi2")
#define VIO_PNG_INFLATE_FAST inflate_fast_bmi2
#include "inflate_fast.inc"
#undef VIO_PNG_INFLATE_FAST
VIO_TARGET_POP()
#endif

}  // namespace detail

class Inflater {
 public:
  // raw = true decodes a bare DEFLATE stream (no zlib header/trailer).
  explicit Inflater(bool raw = false, bool verify_adler = true)
      : raw_(raw), verify_adler_(verify_adler), bmi2_(simd::has(jpeg::simd::kBMI2)) {}

  // An inflater kept per thread (its tables are large to allocate for
  // every small image), set up for a new stream.
  static Inflater& per_thread(bool raw = false, bool verify_adler = true) {
    thread_local std::unique_ptr<Inflater> inf;
    if (!inf) inf = std::make_unique<Inflater>();
    inf->raw_ = raw;
    inf->verify_adler_ = verify_adler;
    return *inf;
  }

  // Decompresses `src` into out[0..out_size).  Stops at the end of the
  // stream or when the output is full, whichever comes first, and returns
  // the number of bytes written; stream_end() tells which.  Throws vio::Error
  // on malformed or truncated data.
  std::size_t decompress(std::span<const std::uint8_t> src, std::uint8_t* out,
                         std::size_t out_size) {
    in_ = src.data();
    in_begin_ = in_;
    in_end_ = in_ + src.size();
    bitbuf_ = 0;
    bitcnt_ = 0;
    overrun_ = 0;
    out_begin_ = out;
    out_ = out;
    out_end_ = out + out_size;
    stream_end_ = false;
    if (!raw_) {
      unsigned cmf = header_byte(), flg = header_byte();
      if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0)
        fail("incorrect header check");
      if (flg & 0x20) fail("need dictionary");
    }
    bool last = false;
    while (!last) {
      refill_slow();
      last = bits(1) != 0;
      unsigned type = bits(2);
      bool done;
      if (type == 0) {
        done = stored();
      } else if (type == 1) {
        const detail::FixedTables& t = detail::fixed_tables();
        done = codes(t.lit, t.dist);
      } else if (type == 2) {
        dynamic();
        done = codes(lit_, dist_);
      } else {
        fail("invalid block type");
      }
      check_overrun();
      if (!done) return static_cast<std::size_t>(out_ - out_begin_);  // output full
    }
    stream_end_ = true;
    // Return whole bytes still in the bit buffer to the input.
    in_ -= bitcnt_ >> 3;
    bitbuf_ = 0;
    bitcnt_ = 0;
    if (!raw_) {
      std::uint32_t check = 0;
      for (int i = 0; i < 4; i++) check = check << 8 | header_byte();
      if (verify_adler_ && check != adler32(1, out_begin_, static_cast<std::size_t>(out_ - out_begin_)))
        fail("incorrect data check");
    }
    return static_cast<std::size_t>(out_ - out_begin_);
  }

  bool stream_end() const { return stream_end_; }
  // Input bytes consumed by the last decompress() that reached the end.
  std::size_t consumed() const { return static_cast<std::size_t>(in_ - in_begin_); }

 private:
  [[noreturn]] static void fail(const char* msg) { VIO_FAIL("zlib: %s", msg); }

  unsigned header_byte() {
    if (in_ >= in_end_) fail("unexpected end of stream");
    return *in_++;
  }

  // Fills the bit buffer to at least 56 bits; past the end of the input it
  // supplies zero bytes, which check_overrun() rejects if they get consumed.
  void refill_slow() {
    while (bitcnt_ <= 56) {
      if (in_ < in_end_) {
        bitbuf_ |= static_cast<std::uint64_t>(*in_++) << bitcnt_;
      } else {
        if (overrun_ > 16) fail("unexpected end of stream");
        overrun_++;
        in_++;
      }
      bitcnt_ += 8;
    }
  }
  // refill_slow(), eight bytes at once while the input allows.
  void refill() {
    if (in_end_ - in_ >= 8 && in_ <= in_end_) {
      bitbuf_ |= detail::load_le64(in_) << bitcnt_;
      in_ += (63 - bitcnt_) >> 3;
      bitcnt_ |= 56;
    } else {
      refill_slow();
    }
  }
  void check_overrun() {
    std::size_t past = static_cast<std::size_t>(in_ > in_end_ ? in_ - in_end_ : 0);
    if (past * 8 > static_cast<std::size_t>(bitcnt_)) fail("unexpected end of stream");
  }
  unsigned bits(int n) {
    unsigned v = static_cast<unsigned>(bitbuf_ & ((1ull << n) - 1));
    bitbuf_ >>= n;
    bitcnt_ -= n;
    return v;
  }

  // Stored block; returns false if the output filled up.
  bool stored() {
    bits(bitcnt_ & 7);
    in_ -= bitcnt_ >> 3;  // return whole buffered bytes to the input
    bitbuf_ = 0;
    bitcnt_ = 0;
    if (in_ > in_end_) fail("unexpected end of stream");
    overrun_ = 0;
    unsigned len = header_byte();
    len |= header_byte() << 8;
    unsigned nlen = header_byte();
    nlen |= header_byte() << 8;
    if ((len ^ 0xffff) != nlen) fail("invalid stored block lengths");
    std::size_t room = static_cast<std::size_t>(out_end_ - out_);
    std::size_t avail = static_cast<std::size_t>(in_end_ - in_);
    std::size_t n = std::min<std::size_t>({len, room, avail});
    std::memcpy(out_, in_, n);
    out_ += n;
    in_ += n;
    if (n < len) {
      if (n == room) return false;
      fail("unexpected end of stream");
    }
    return true;
  }

  void dynamic() {
    static constexpr std::uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                               11, 4,  12, 3, 13, 2, 14, 1, 15};
    refill();
    unsigned nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
    if (nlen > 286 || ndist > 30) fail("too many length or distance symbols");
    std::uint8_t lengths[320] = {};
    for (unsigned i = 0; i < ncode; i++) {
      if (bitcnt_ < 3) refill();
      lengths[order[i]] = static_cast<std::uint8_t>(bits(3));
    }
    std::uint32_t cl[1 << detail::kCodeLenBits];
    if (!detail::build_table(cl, lengths, 19, detail::Code::kCodeLen, detail::kCodeLenBits))
      fail("invalid code lengths set");
    unsigned idx = 0;
    std::memset(lengths, 0, sizeof(lengths));
    while (idx < nlen + ndist) {
      if (bitcnt_ < 14) refill();
      std::uint32_t e = cl[bitbuf_ & ((1u << detail::kCodeLenBits) - 1)];
      if (e & detail::kEntExceptional) fail("invalid code lengths set");
      bits(static_cast<int>(e & 63));
      unsigned sym = e >> 16;
      if (sym < 16) {
        lengths[idx++] = static_cast<std::uint8_t>(sym);
      } else {
        unsigned rep;
        std::uint8_t val = 0;
        if (sym == 16) {
          if (idx == 0) fail("invalid bit length repeat");
          val = lengths[idx - 1];
          rep = 3 + bits(2);
        } else if (sym == 17) {
          rep = 3 + bits(3);
        } else {
          rep = 11 + bits(7);
        }
        if (idx + rep > nlen + ndist) fail("invalid bit length repeat");
        while (rep--) lengths[idx++] = val;
      }
    }
    if (lengths[256] == 0) fail("invalid code -- missing end-of-block");
    if (!detail::build_table(lit_, lengths, nlen, detail::Code::kLitLen, detail::kLitBits))
      fail("invalid literal/lengths set");
    if (!detail::build_table(dist_, lengths + nlen, ndist, detail::Code::kDist, detail::kDistBits))
      fail("invalid distances set");
  }

  // Decodes one Huffman-coded block; returns false if the output filled up
  // before its end.
  bool codes(const std::uint32_t* lit, const std::uint32_t* dist) {
    using namespace detail;
    constexpr std::uint64_t kLitMask = (1u << kLitBits) - 1, kDistMask = (1u << kDistBits) - 1;
    // Fast loop: enough input for 8-byte refills and enough output room for
    // a maximal match plus a 16-byte overrun (two refills per iteration).
    if (in_end_ - in_ >= 32 && out_end_ - out_ >= 258 + 32) {
      FastState fs{in_, out_, bitbuf_, static_cast<unsigned>(bitcnt_), in_end_ - 16, out_end_ - (258 + 32),
                   out_begin_, simd::has(jpeg::simd::kNEON)};
#if VIO_PNG_INFLATE_BMI2
      const bool end_of_block = bmi2_ ? inflate_fast_bmi2(lit, dist, fs) : inflate_fast(lit, dist, fs);
#else
      const bool end_of_block = inflate_fast(lit, dist, fs);
#endif
      in_ = fs.in;
      out_ = fs.out;
      bitbuf_ = fs.bitbuf;
      bitcnt_ = static_cast<int>(fs.bitcnt);
      if (end_of_block) return true;
    }
    // Careful loop: one symbol at a time with bounds checks.
    for (;;) {
      refill_slow();
      std::uint32_t e = lit[bitbuf_ & kLitMask];
      if ((e & kEntExceptional) && (e & kEntSecond))
        e = lit[(e >> 16) + ((bitbuf_ >> kLitBits) & ((1u << ((e >> 8) & 15)) - 1))];
      if (e & kEntLiteral) {
        if (out_ == out_end_) return false;
        *out_++ = static_cast<std::uint8_t>(e >> 16);
        if (e & kEntSecond) {
          if (out_ == out_end_) {
            bits(static_cast<int>((e >> 8) & 31));
            return false;
          }
          *out_++ = static_cast<std::uint8_t>(e >> 24);
        }
        bits(static_cast<int>(e & 63));
        continue;
      }
      if (e & kEntExceptional) {
        if (e & kEntEndOfBlock) {
          bits(static_cast<int>(e & 63));
          return true;
        }
        fail("invalid literal/length code");
      }
      std::uint64_t saved = bitbuf_;
      bits(static_cast<int>(e & 63));
      unsigned len = (e >> 16) +
                     static_cast<unsigned>((saved & ((1ull << (e & 63)) - 1)) >> ((e >> 8) & 31));
      std::uint32_t d = dist[bitbuf_ & kDistMask];
      if (d & kEntExceptional) {
        if (d & kEntSecond)
          d = dist[(d >> 16) + ((bitbuf_ >> kDistBits) & ((1u << ((d >> 8) & 15)) - 1))];
        if (d & kEntExceptional) fail("invalid distance code");
      }
      saved = bitbuf_;
      bits(static_cast<int>(d & 63));
      std::size_t distance =
          (d >> 16) + static_cast<std::size_t>((saved & ((1ull << (d & 63)) - 1)) >> ((d >> 8) & 31));
      if (distance > static_cast<std::size_t>(out_ - out_begin_))
        fail("invalid distance too far back");
      std::size_t room = static_cast<std::size_t>(out_end_ - out_);
      std::size_t n = len < room ? len : room;
      if (distance == 1) {
        std::memset(out_, out_[-1], n);
      } else if (distance >= n) {
        std::memcpy(out_, out_ - distance, n);
      } else {
        for (std::size_t i = 0; i < n; i++)
          out_[i] = out_[static_cast<std::ptrdiff_t>(i) - static_cast<std::ptrdiff_t>(distance)];
      }
      out_ += n;
      if (n < len) return false;
    }
  }

  bool raw_, verify_adler_, bmi2_;
  bool stream_end_ = false;
  const std::uint8_t* in_ = nullptr;
  const std::uint8_t* in_begin_ = nullptr;
  const std::uint8_t* in_end_ = nullptr;
  std::uint64_t bitbuf_ = 0;
  int bitcnt_ = 0;
  int overrun_ = 0;
  std::uint8_t* out_begin_ = nullptr;
  std::uint8_t* out_ = nullptr;
  std::uint8_t* out_end_ = nullptr;
  std::uint32_t lit_[detail::kLitTableSize];
  std::uint32_t dist_[detail::kDistTableSize];
};

// StreamInflater decompresses a stream given in pieces of any size, as they
// arrive: each feed() decodes as far as the input allows and passes the
// output to a sink.  It keeps the last 32 KiB of output as the window and
// the input of a symbol (or block header) not yet complete.
class StreamInflater {
 public:
  using Sink = std::function<void(const std::uint8_t* data, std::size_t n)>;

  // raw = true decodes a bare DEFLATE stream (no zlib header/trailer).
  // Output beyond `limit` bytes (0 = none) is not decoded.
  explicit StreamInflater(Sink sink, bool raw = false, bool verify_adler = true, std::uint64_t limit = 0)
      : sink_(std::move(sink)), raw_(raw), verify_adler_(verify_adler), limit_(limit) {
    state_ = raw ? kBlock : kHeader;
  }

  // Decodes as much of the stream as `data` (following the earlier pieces)
  // allows.  Throws vio::Error on malformed data.
  void feed(const std::uint8_t* data, std::size_t n) {
    if (state_ == kDone || state_ == kFull) {
      extra_ += n;
      return;
    }
    in_.insert(in_.end(), data, data + n);
    run();
    flush();
    // keep only the input not yet consumed
    if (pos_ > 0) {
      in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(pos_));
      pos_ = 0;
    }
    if (state_ == kDone || state_ == kFull) {
      extra_ += in_.size();
      in_.clear();
    }
  }

  bool stream_end() const { return state_ == kDone; }
  // The output reached the limit before the end of the stream.
  bool limited() const { return state_ == kFull; }
  std::uint64_t total_out() const { return total_; }
  // Input bytes given after the end of the stream (or of the limit).
  std::uint64_t extra_input() const { return extra_; }

 private:
  enum State { kHeader, kBlock, kStored, kCodes, kTrailer, kDone, kFull };
  struct NeedInput {};

  [[noreturn]] static void fail(const char* msg) { VIO_FAIL("zlib: %s", msg); }

  // Bit input: fills the buffer from the pending input as far as it goes.
  void fill() {
    while (bitcnt_ <= 56 && pos_ < in_.size()) {
      bitbuf_ |= static_cast<std::uint64_t>(in_[pos_++]) << bitcnt_;
      bitcnt_ += 8;
    }
  }
  void need(int n) {
    if (bitcnt_ < n) fill();
    if (bitcnt_ < n) throw NeedInput{};
  }
  unsigned bits(int n) {
    need(n);
    const unsigned v = static_cast<unsigned>(bitbuf_ & ((1ull << n) - 1));
    bitbuf_ >>= n;
    bitcnt_ -= n;
    return v;
  }
  void align() {
    bitbuf_ >>= bitcnt_ & 7;
    bitcnt_ &= ~7;
  }

  // Restart point: the state before the current symbol or header.
  struct Mark {
    std::size_t pos;
    std::uint64_t bitbuf;
    int bitcnt;
  };
  Mark mark() const { return {pos_, bitbuf_, bitcnt_}; }
  void restore(const Mark& m) {
    pos_ = m.pos;
    bitbuf_ = m.bitbuf;
    bitcnt_ = m.bitcnt;
  }

  void put(std::uint8_t b) {
    win_[total_ & kWinMask] = b;
    total_++;
    pending_.push_back(b);
    if (pending_.size() >= 16384) flush();
  }
  void flush() {
    if (pending_.empty()) return;
    if (!raw_ && verify_adler_) adler_ = adler32(adler_, pending_.data(), pending_.size());
    sink_(pending_.data(), pending_.size());
    pending_.clear();
  }
  bool full() const { return limit_ && total_ >= limit_; }

  void run() {
    try {
      for (;;) {
        Mark m = mark();
        try {
          switch (state_) {
            case kHeader: {
              const unsigned cmf = bits(8), flg = bits(8);
              if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0)
                fail("incorrect header check");
              if (flg & 0x20) fail("need dictionary");
              state_ = kBlock;
              break;
            }
            case kBlock: block_header(); break;
            case kStored:
              while (stored_left_ > 0) {
                if (full()) {
                  state_ = kFull;
                  return;
                }
                if (bitcnt_ >= 8) {
                  put(static_cast<std::uint8_t>(bits(8)));
                } else {
                  if (pos_ >= in_.size()) return;
                  put(in_[pos_++]);
                }
                stored_left_--;
              }
              state_ = last_ ? (raw_ ? kDone : kTrailer) : kBlock;
              break;
            case kCodes:
              if (!codes()) return;  // more input needed, or the limit reached
              state_ = last_ ? (raw_ ? kDone : kTrailer) : kBlock;
              break;
            case kTrailer: {
              align();
              std::uint32_t check = 0;
              for (int i = 0; i < 4; i++) check = check << 8 | bits(8);
              flush();
              if (verify_adler_ && check != adler_) fail("incorrect data check");
              state_ = kDone;
              break;
            }
            case kDone:
            case kFull: return;
          }
        } catch (const NeedInput&) {
          restore(m);
          return;
        }
        if (state_ == kDone) {
          // give back whole bytes still in the bit buffer
          pos_ -= static_cast<std::size_t>(bitcnt_ >> 3);
          bitbuf_ = 0;
          bitcnt_ = 0;
          return;
        }
      }
    } catch (...) {
      flush();
      throw;
    }
  }

  void block_header() {
    last_ = bits(1) != 0;
    const unsigned type = bits(2);
    if (type == 0) {
      align();
      const unsigned len = bits(16), nlen = bits(16);
      if ((len ^ 0xffff) != nlen) fail("invalid stored block lengths");
      stored_left_ = len;
      state_ = kStored;
    } else if (type == 1) {
      lit_ = detail::fixed_tables().lit;
      dist_ = detail::fixed_tables().dist;
      state_ = kCodes;
    } else if (type == 2) {
      dynamic();
      lit_ = dyn_lit_.data();
      dist_ = dyn_dist_.data();
      state_ = kCodes;
    } else {
      fail("invalid block type");
    }
  }

  void dynamic() {
    static constexpr std::uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const unsigned nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
    if (nlen > 286 || ndist > 30) fail("too many length or distance symbols");
    std::uint8_t lengths[320] = {};
    for (unsigned i = 0; i < ncode; i++) lengths[order[i]] = static_cast<std::uint8_t>(bits(3));
    std::uint32_t cl[1 << detail::kCodeLenBits];
    if (!detail::build_table(cl, lengths, 19, detail::Code::kCodeLen, detail::kCodeLenBits))
      fail("invalid code lengths set");
    std::memset(lengths, 0, sizeof(lengths));
    unsigned idx = 0;
    while (idx < nlen + ndist) {
      fill();
      const std::uint32_t e = cl[bitbuf_ & ((1u << detail::kCodeLenBits) - 1)];
      if (e & detail::kEntExceptional) fail("invalid code lengths set");
      bits(static_cast<int>(e & 63));
      const unsigned sym = e >> 16;
      if (sym < 16) {
        lengths[idx++] = static_cast<std::uint8_t>(sym);
        continue;
      }
      unsigned rep;
      std::uint8_t val = 0;
      if (sym == 16) {
        if (idx == 0) fail("invalid bit length repeat");
        val = lengths[idx - 1];
        rep = 3 + bits(2);
      } else if (sym == 17) {
        rep = 3 + bits(3);
      } else {
        rep = 11 + bits(7);
      }
      if (idx + rep > nlen + ndist) fail("invalid bit length repeat");
      while (rep--) lengths[idx++] = val;
    }
    if (lengths[256] == 0) fail("invalid code -- missing end-of-block");
    dyn_lit_.resize(detail::kLitTableSize);
    dyn_dist_.resize(detail::kDistTableSize);
    if (!detail::build_table(dyn_lit_.data(), lengths, nlen, detail::Code::kLitLen, detail::kLitBits))
      fail("invalid literal/lengths set");
    if (!detail::build_table(dyn_dist_.data(), lengths + nlen, ndist, detail::Code::kDist, detail::kDistBits))
      fail("invalid distances set");
  }

  // Looks up the entry for the next code (input permitting); the bits are
  // consumed by the caller once the whole symbol is known to be there.
  std::uint32_t lookup(const std::uint32_t* table, int table_bits) {
    fill();
    std::uint32_t e = table[bitbuf_ & ((1u << table_bits) - 1)];
    if ((e & detail::kEntExceptional) && (e & detail::kEntSecond))
      e = table[(e >> 16) + ((bitbuf_ >> table_bits) & ((1u << ((e >> 8) & 15)) - 1))];
    return e;
  }

  // Decodes symbols up to the end of the block; returns false if the input
  // ran out (the symbol under way is left for the next feed) or the output
  // limit was reached first.
  bool codes() {
    using namespace detail;
    for (;;) {
      if (full()) {
        state_ = kFull;
        return false;
      }
      const Mark m = mark();
      std::uint32_t e = lookup(lit_, kLitBits);
      if ((e & 63) > static_cast<unsigned>(bitcnt_)) return false;
      if (e & kEntLiteral) {
        bits(static_cast<int>(e & 63));
        put(static_cast<std::uint8_t>(e >> 16));
        if (e & kEntSecond) put(static_cast<std::uint8_t>(e >> 24));
        continue;
      }
      if (e & kEntExceptional) {
        if (e & kEntEndOfBlock) {
          bits(static_cast<int>(e & 63));
          return true;
        }
        if (bitcnt_ < 15) return false;  // may be a code not all there yet
        fail("invalid literal/length code");
      }
      std::uint64_t saved = bitbuf_;
      bits(static_cast<int>(e & 63));
      const unsigned len =
          (e >> 16) + static_cast<unsigned>((saved & ((1ull << (e & 63)) - 1)) >> ((e >> 8) & 31));
      const std::uint32_t d = lookup(dist_, kDistBits);
      if ((d & kEntExceptional) && bitcnt_ < 15) {
        restore(m);
        return false;
      }
      if ((d & 63) > static_cast<unsigned>(bitcnt_)) {
        restore(m);
        return false;
      }
      if (d & kEntExceptional) fail("invalid distance code");
      saved = bitbuf_;
      bits(static_cast<int>(d & 63));
      const std::size_t distance =
          (d >> 16) + static_cast<std::size_t>((saved & ((1ull << (d & 63)) - 1)) >> ((d >> 8) & 31));
      if (distance > total_ || distance > kWinSize) fail("invalid distance too far back");
      for (unsigned i = 0; i < len; i++) put(win_[(total_ - distance) & kWinMask]);
    }
  }

  static constexpr std::size_t kWinSize = 32768, kWinMask = kWinSize - 1;
  Sink sink_;
  bool raw_, verify_adler_;
  std::uint64_t limit_;
  State state_;
  bool last_ = false;
  unsigned stored_left_ = 0;
  std::vector<std::uint8_t> in_;
  std::size_t pos_ = 0;
  std::uint64_t bitbuf_ = 0;
  int bitcnt_ = 0;
  std::uint64_t total_ = 0, extra_ = 0;
  std::uint32_t adler_ = 1;
  const std::uint32_t* lit_ = nullptr;
  const std::uint32_t* dist_ = nullptr;
  std::vector<std::uint32_t> dyn_lit_, dyn_dist_;
  std::vector<std::uint8_t> pending_;
  std::uint8_t win_[kWinSize];
};

// uncompress(): one-shot zlib-format decompression.  If `limit` is nonzero,
// output beyond it is an error.
inline std::vector<std::uint8_t> uncompress(std::span<const std::uint8_t> src,
                                            std::size_t limit = 0) {
  std::size_t cap = std::max<std::size_t>(src.size() * 4, 1024);
  if (limit && cap > limit) cap = limit;
  auto inf = std::make_unique<Inflater>();
  for (;;) {
    std::vector<std::uint8_t> out(cap);
    std::size_t n = inf->decompress(src, out.data(), out.size());
    if (inf->stream_end()) {
      out.resize(n);
      return out;
    }
    if (limit && cap >= limit) VIO_FAIL("zlib: output exceeds limit");
    cap *= 2;
    if (limit && cap > limit) cap = limit;
  }
}

}  // namespace vio::png::zlib
