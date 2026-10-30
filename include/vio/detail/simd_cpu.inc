// Included inside each codec's detail_cpu namespace.
#if VIO_SIMD_X86
inline void cpuid(unsigned leaf, unsigned subleaf, unsigned r[4]) {
#if defined(_MSC_VER) && !defined(__clang__)
  int v[4];
  __cpuidex(v, static_cast<int>(leaf), static_cast<int>(subleaf));
  for (int i = 0; i < 4; i++) r[i] = static_cast<unsigned>(v[i]);
#else
  __cpuid_count(leaf, subleaf, r[0], r[1], r[2], r[3]);
#endif
}

inline unsigned long long xgetbv0() {
#if defined(_MSC_VER) && !defined(__clang__)
  return _xgetbv(0);
#else
  unsigned eax, edx;
  __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  return (static_cast<unsigned long long>(edx) << 32) | eax;
#endif
}

inline unsigned detect_x86() {
  unsigned flags = 0;
  unsigned r[4];
  cpuid(0, 0, r);
  const unsigned max_leaf = r[0];
#if VIO_SIMD_X86_64
  flags |= kSSE | kSSE2;  // baseline on x86-64
#else
  if (max_leaf >= 1) {
    cpuid(1, 0, r);
    if (r[3] & (1u << 23)) flags |= kMMX;
    if (r[3] & (1u << 25)) flags |= kSSE;
    if (r[3] & (1u << 26)) flags |= kSSE2;
  }
#endif
#if VIO_SIMD_CPU_PNG
  if (max_leaf >= 1) {
    cpuid(1, 0, r);
    if (r[2] & (1u << 9)) flags |= kSSSE3;
    if (r[2] & (1u << 19)) flags |= kSSE41;
    if (r[2] & (1u << 1)) flags |= kPCLMUL;
  }
#endif
#if VIO_SIMD_AVX2
  // AVX2 needs CPU support (leaf 7 EBX bit 5, leaf 1 ECX bits 27-28) and
  // operating-system management of the YMM state (XCR0 bits 1-2).
  if (max_leaf >= 7) {
    cpuid(7, 0, r);
    const unsigned ebx7 = r[1];
    if (ebx7 & (1u << 5)) {
      cpuid(1, 0, r);
      if ((r[2] & (1u << 27)) && (r[2] & (1u << 28)) && (xgetbv0() & 6) == 6) flags |= kAVX2;
    }
    // BMI1 (leaf 7 EBX bit 3), BMI2 (bit 8) and LZCNT (leaf 0x80000001 ECX bit 5)
    cpuid(0x80000000u, 0, r);
    if ((ebx7 & (1u << 3)) && (ebx7 & (1u << 8)) && r[0] >= 0x80000001u) {
      cpuid(0x80000001u, 0, r);
      if (r[2] & (1u << 5)) flags |= kBMI2;
    }
  }
#endif
  return flags;
}
#endif

// What the compiled-in back ends can use on this CPU.
inline unsigned detect() {
  unsigned flags = 0;
#if VIO_SIMD_X86
  flags |= detect_x86();
#endif
#if VIO_SIMD_NEON_RUNTIME
  if (getauxval(AT_HWCAP) & (1ul << 12)) flags |= kNEON;  // HWCAP_NEON
#elif VIO_SIMD_NEON
  flags |= kNEON;  // AArch64, or AArch32 code built for NEON
#endif
#if VIO_SIMD_CPU_PNG
#if VIO_SIMD_NEON_A64
#if defined(__linux__)
  const unsigned long hwcap = getauxval(AT_HWCAP);
  if (hwcap & (1ul << 7)) flags |= kCRC32;  // HWCAP_CRC32
  if (hwcap & (1ul << 4)) flags |= kPMULL;  // HWCAP_PMULL
#else
#if defined(__ARM_FEATURE_CRC32)
  flags |= kCRC32;
#endif
#if defined(__ARM_FEATURE_AES) || defined(__ARM_FEATURE_CRYPTO)
  flags |= kPMULL;
#endif
#endif
#endif
#endif
#if VIO_SIMD_ALTIVEC
  flags |= kAltiVec;
#endif
#if VIO_SIMD_RVV
  flags |= kRVV;
#endif
#if VIO_SIMD_MMI
  flags |= kMMI;
#endif
  return flags;
}

// True if the environment variable `name` is set to exactly "1".
inline bool env_is_1(const char* name) {
#if defined(_MSC_VER)
  char buf[2] = {};
  std::size_t len = 0;
  return getenv_s(&len, buf, sizeof(buf), name) == 0 && len == 2 && buf[0] == '1';
#else
  const char* v = std::getenv(name);
  return v && std::strcmp(v, "1") == 0;
#endif
}
