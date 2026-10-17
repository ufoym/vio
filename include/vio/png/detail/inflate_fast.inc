// vio::png - the inflater's fast loop (included by inflate.hpp, once per
// instruction set it is compiled for, with VIO_PNG_INFLATE_FAST naming the
// function): decodes the symbols of a Huffman-coded block while there is
// room for a maximal match in the output and enough input for 8-byte
// refills; returns whether it reached the end of the block.

inline bool VIO_PNG_INFLATE_FAST(const std::uint32_t* lit, const std::uint32_t* dist, FastState& s) {
  constexpr std::uint64_t kLitMask = (1u << kLitBits) - 1, kDistMask = (1u << kDistBits) - 1;
  const std::uint8_t* in = s.in;
  std::uint8_t* out = s.out;
  const std::uint8_t* const in_fast_end = s.in_fast_end;
  std::uint8_t* const out_fast_end = s.out_fast_end;
  std::uint64_t bitbuf = s.bitbuf;
  unsigned bitcnt = s.bitcnt;
  const bool pattern_simd = s.pattern_simd;
#define VIO_PNG_REFILL()               \
  do {                                 \
    bitbuf |= load_le64(in) << bitcnt; \
    in += (63 - bitcnt) >> 3;          \
    bitcnt |= 56;                      \
  } while (0)
  bool end_of_block = false;
  VIO_PNG_REFILL();
  std::uint32_t e = lit[bitbuf & kLitMask];
  while (in < in_fast_end && out < out_fast_end) {
    // Up to three literal entries (each one or two literals) per
    // refill.  The lookups for the second and third are made before it
    // is known whether the first ones are literals.
    if (e & kEntLiteral) {
      // Each lookup shifts the bits left by the one before it, keeping
      // the dependency chain to a shift, a mask and a load.
      const std::uint64_t b1 = bitbuf >> (e & 63);
      const std::uint32_t e2 = lit[b1 & kLitMask];
      const std::uint64_t b2 = b1 >> (e2 & 63);
      const std::uint32_t e3 = lit[b2 & kLitMask];
      out[0] = static_cast<std::uint8_t>(e >> 16);
      out[1] = static_cast<std::uint8_t>(e >> 24);
      out += 1 + ((e >> 14) & 1);
      if (e2 & kEntLiteral) {
        out[0] = static_cast<std::uint8_t>(e2 >> 16);
        out[1] = static_cast<std::uint8_t>(e2 >> 24);
        out += 1 + ((e2 >> 14) & 1);
        if (e3 & kEntLiteral) {
          const std::uint64_t b3 = b2 >> (e3 & 63);
          const std::uint32_t e4 = lit[b3 & kLitMask];
          out[0] = static_cast<std::uint8_t>(e3 >> 16);
          out[1] = static_cast<std::uint8_t>(e3 >> 24);
          out += 1 + ((e3 >> 14) & 1);
          bitbuf = b3;
          bitcnt -= (e & 63) + (e2 & 63) + (e3 & 63);
          VIO_PNG_REFILL();
          e = e4;
          continue;
        }
        bitbuf = b2;
        bitcnt -= (e & 63) + (e2 & 63);
        e = e3;
      } else {
        bitbuf = b1;
        bitcnt -= e & 63;
        e = e2;
      }
      VIO_PNG_REFILL();
    }
    if (VIO_UNLIKELY(e & kEntExceptional)) {
      if (e & kEntSecond) {
        e = lit[(e >> 16) + ((bitbuf >> kLitBits) & ((1u << ((e >> 8) & 15)) - 1))];
        if (e & kEntLiteral) {
          *out++ = static_cast<std::uint8_t>(e >> 16);
          bitbuf >>= e & 63;
          bitcnt -= e & 63;
          VIO_PNG_REFILL();
          e = lit[bitbuf & kLitMask];
          continue;
        }
      }
      if (e & kEntExceptional) {
        if (e & kEntEndOfBlock) {
          bitbuf >>= e & 63;
          bitcnt -= e & 63;
          end_of_block = true;
          break;
        }
        inflate_fail("invalid literal/length code");
      }
    }
    std::uint64_t saved = bitbuf;
    bitbuf >>= e & 63;
    bitcnt -= e & 63;
    unsigned len = (e >> 16) +
                   static_cast<unsigned>((saved & ((1ull << (e & 63)) - 1)) >> ((e >> 8) & 31));
    std::uint32_t d = dist[bitbuf & kDistMask];
    if (VIO_UNLIKELY(d & kEntExceptional)) {
      if (d & kEntSecond)
        d = dist[(d >> 16) + ((bitbuf >> kDistBits) & ((1u << ((d >> 8) & 15)) - 1))];
      if (d & kEntExceptional) inflate_fail("invalid distance code");
    }
    saved = bitbuf;
    bitbuf >>= d & 63;
    bitcnt -= d & 63;
    std::size_t distance =
        (d >> 16) + static_cast<std::size_t>((saved & ((1ull << (d & 63)) - 1)) >> ((d >> 8) & 31));
    if (VIO_UNLIKELY(distance > static_cast<std::size_t>(out - s.out_begin)))
      inflate_fail("invalid distance too far back");
    VIO_PNG_REFILL();
    e = lit[bitbuf & kLitMask];
    copy_match(out, distance, len, pattern_simd);
    out += len;
  }
  // Give back whole bytes not yet consumed so that the careful loop
  // continues from an exact position.
  in -= bitcnt >> 3;
  bitcnt &= 7;
  bitbuf &= (1ull << bitcnt) - 1;
  s.in = in;
  s.out = out;
  s.bitbuf = bitbuf;
  s.bitcnt = bitcnt;
#undef VIO_PNG_REFILL
  return end_of_block;
}
