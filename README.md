# vio

Header-only C++20 image I/O.  The JPEG codec supports sequential,
progressive, arithmetic-coded and lossless JPEG with 8- to 16-bit samples,
lossless transforms, and SIMD acceleration for x86 (MMX through AVX2), Arm
NEON, PowerPC AltiVec, Loongson MMI and RISC-V Vector.  It is faster than
libjpeg-turbo in almost every configuration measured (see
[Performance](#performance)).

## Using the library

Add `include/` to the include path (or link the `vio::vio` CMake target) and
include only the formats you need:

| Header           | API                                                      |
| ---------------- | -------------------------------------------------------- |
| `vio/jpeg.hpp`   | `vio::jpeg::Decoder`, `Encoder`, lossless transforms (or include `vio/jpeg/decoder.hpp` etc. individually) |
| `vio/png.hpp`    | `vio::png::Reader<T>`, `Writer<T>`; whole-image `Decoder`, `Encoder` |
| `vio/bmp.hpp`    | `vio::bmp::Reader`, `Writer`                             |
| `vio/gif.hpp`    | `vio::gif::Reader`, `Writer<T>`                          |
| `vio/ppm.hpp`    | `vio::ppm::Reader<T>`, `Writer<T>`                       |
| `vio/targa.hpp`  | `vio::targa::Reader`, `Writer`                           |

The packed-pixel readers and writers implement `vio::ImageReader<T>` /
`vio::ImageWriter<T>` (`vio/core.hpp`), which exchange rows of
samples; `T` is `uint8_t`, `int16_t` or `uint16_t` for 8-, 12- and 16-bit
data.

## Features

Compared with libjpeg-turbo 3.2 and IJG libjpeg 10.  All three run on any
architecture through portable code; libjpeg, written in plain ANSI C, builds
with the widest range of compilers and systems, while vio needs a C++20
compiler.

| Feature                                                    | vio | libjpeg-turbo | libjpeg |
| ---------------------------------------------------------- | :-: | :-----------: | :-----: |
| SIMD: x86-64 SSE2, AVX2                                    |  ✓  |       ✓       |    ✗    |
| SIMD: i386 MMX, SSE, SSE2, AVX2                            |  ✓  |       ✓       |    ✗    |
| SIMD: AArch64 NEON                                         |  ✓  |       ✓       |    ✗    |
| SIMD: AArch32 NEON                                         |  ✓  |       ✓       |    ✗    |
| SIMD: AArch32 NEON detected at run time                    | ✓¹  |       ✓       |    ✗    |
| SIMD: PowerPC AltiVec                                      |  ✓  |       ✓       |    ✗    |
| SIMD: MIPS64 Loongson MMI                                  | ✓²  |       ✓       |    ✗    |
| SIMD: RISC-V 64 RVV 1.0                                    | ✓³  |       ✓       |    ✗    |
| SIMD: run-time selection via `JSIMD_*` variables           |  ✓  |       ✓       |    ✗    |
| Toolchains: Linux GCC, Clang                               |  ✓  |       ✓       |    ✓    |
| Toolchains: macOS Apple Clang                              |  ✓  |       ✓       |    ✓    |
| Toolchains: Windows MSVC, clang-cl                         |  ✓  |       ✓       |    ✓    |
| Toolchains: compilers without C++20                        |  ✗  |       ✓       |    ✓    |
| JPEG: baseline and extended sequential                     |  ✓  |       ✓       |    ✓    |
| JPEG: progressive                                          |  ✓  |       ✓       |    ✓    |
| JPEG: arithmetic coding                                    |  ✓  |       ✓       |    ✓    |
| JPEG: lossless (2- to 16-bit)                              |  ✓  |       ✓       |    ✗    |
| JPEG: 12-bit lossy                                         |  ✓  |       ✓       |   ✓⁴    |
| JPEG: lossless transforms (rotate, flip, crop, wipe, drop) |  ✓  |       ✓       |    ✓    |
| JPEG: lossless roll transform                              |  ✓  |       ✓       |    ✗    |
| JPEG: lossless negate transform                            |  ✗  |       ✗       |    ✓    |
| JPEG: SmartScale (non-standard DCT block sizes)            |  ✗  |       ✗       |    ✓    |
| JPEG: scaled decoding                                      |  ✓  |       ✓       |    ✓    |
| JPEG: partial decoding (crop, skip rows)                   |  ✓  |       ✓       |    ✗    |
| JPEG: raw (planar YCbCr) input and output                  |  ✓  |       ✓       |    ✓    |
| JPEG: saved markers                                        |  ✓  |       ✓       |    ✓    |
| JPEG: ICC profile read/write API                           |  ✓  |       ✓       |    ✗    |
| JPEG: color quantization and dithering                     |  ✓  |       ✓       |    ✓    |
| Integration: header-only, no build step                    |  ✓  |       ✗       |    ✗    |
| Integration: no assembler needed                           |  ✓  |      ✗⁵       |    ✓    |
| Integration: C++ API                                       |  ✓  |       ✗       |    ✗    |
| Integration: libjpeg C API                                 |  ✗  |       ✓       |    ✓    |
| Integration: TurboJPEG C API                               |  ✗  |       ✓       |    ✗    |
| Integration: Java binding                                  |  ✗  |       ✓       |    ✗    |

¹ GCC only. ² Little-endian only. ³ GCC ≥ 14 or Clang ≥ 17.
⁴ A separate build of the library (8- or 12-bit, not both).
⁵ NASM or Yasm for the x86 SIMD code.

## Performance

Throughput measured by `tjbench` on GitHub-hosted runners: vio, libjpeg-turbo
3.2 (built from the test submodule) and IJG libjpeg 10 (driven through a thin
TurboJPEG layer, `tests/libjpeg/`), all built on the same machine with the
same compiler (GCC on Linux, Apple Clang on macOS, MSVC on Windows; NASM for
libjpeg-turbo's x86 SIMD).  The image is a 3632×2384 RGB tiling of `testorig.ppm`; each number
is the best of 5 runs.  Results from commit `648a2e1`.  Hosted Linux x86-64
runners come with different processors; the on-demand workflow
`bench-x64.yml` runs the benchmark on several at once, which measured the
three AMD EPYC generations below.

How much faster vio is than libjpeg-turbo and than IJG libjpeg (which has no
SIMD code): +20% means 1.2 times the throughput.  Geometric mean over the
configurations below except lossless, which libjpeg does not support.

JPEG compression:

| Platform       | CPU                      | SIMD   | vs libjpeg-turbo | vs libjpeg |
| -------------- | ------------------------ | ------ | ---------------: | ---------: |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | AVX2   |             +21% |      +472% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | SSE2   |             +19% |      +420% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | scalar |             +40% |       +98% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | AVX2   |             +24% |      +510% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | SSE2   |             +23% |      +436% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | scalar |             +51% |      +122% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | AVX2   |             +22% |      +548% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | SSE2   |             +25% |      +482% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | scalar |             +63% |      +145% |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   |             +19% |      +453% |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |             +10% |      +370% |
| macOS x86-64   | Intel Core i7-8700B      | scalar |             +56% |      +135% |
| Linux AArch64  | Neoverse-N2              | NEON   |             +19% |      +306% |
| Linux AArch64  | Neoverse-N2              | scalar |             +10% |      +126% |
| macOS AArch64  | Apple M4¹                | NEON   |             +24% |      +445% |
| macOS AArch64  | Apple M4¹                | scalar |             +23% |      +203% |
| Windows x86-64 | Intel Core Ultra 9 285K² | AVX2   |              +9% |      +418% |
| Windows x86-64 | Intel Core Ultra 9 285K² | SSE2   |             +11% |      +369% |
| Windows x86-64 | Intel Core Ultra 9 285K² | scalar |             +59% |      +126% |

JPEG decompression:

| Platform       | CPU                      | SIMD   | vs libjpeg-turbo | vs libjpeg |
| -------------- | ------------------------ | ------ | ---------------: | ---------: |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | AVX2   |             +15% |      +147% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | SSE2   |             +17% |      +134% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | scalar |              +6% |       +28% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | AVX2   |             +22% |      +158% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | SSE2   |             +22% |      +139% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | scalar |              +8% |       +30% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | AVX2   |             +24% |      +153% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | SSE2   |             +25% |      +134% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | scalar |             +12% |       +41% |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   |             +15% |      +131% |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |             +12% |      +116% |
| macOS x86-64   | Intel Core i7-8700B      | scalar |              +7% |       +36% |
| Linux AArch64  | Neoverse-N2              | NEON   |             +23% |       +93% |
| Linux AArch64  | Neoverse-N2              | scalar |             +16% |       +22% |
| macOS AArch64  | Apple M4¹                | NEON   |             +25% |      +101% |
| macOS AArch64  | Apple M4¹                | scalar |             +12% |       +24% |
| Windows x86-64 | Intel Core Ultra 9 285K² | AVX2   |             +13% |       +82% |
| Windows x86-64 | Intel Core Ultra 9 285K² | SSE2   |             +13% |       +68% |
| Windows x86-64 | Intel Core Ultra 9 285K² | scalar |              +5% |        +9% |

¹ Measured on a local Mac (native, same build and benchmark script)
instead of the GitHub-hosted runner, which is a virtualized Apple M1.
² Measured on a local Windows 11 machine with MSVC 19.44 (Visual Studio
2022), pinned to the performance cores, at commit `0e6f24d` (before the
latest compression work).  Built with clang-cl 19 instead (vio and
libjpeg-turbo), vio compresses 16% (AVX2), 11% (SSE2) and 81% (scalar)
faster than libjpeg-turbo and decompresses 22%, 23% and 13% faster.

Per configuration, on the same machines (by CPU; the summary gives their
platforms), each summary figure above being the geometric mean of the first
seven columns of its row. Columns: 4:4:4, 4:2:2, 4:2:0 and gray at quality
95; fastDCT: 4:2:0 with the fast integer DCT; prog.: 4:2:0 progressive;
arith.: 4:2:0 arithmetic-coded.

JPEG compression, vio vs libjpeg-turbo:

| CPU, SIMD            | 4:4:4 | 4:2:2 | 4:2:0 |  gray | fastDCT | prog. | arith. | lossless |
| -------------------- | ----: | ----: | ----: | ----: | ------: | ----: | -----: | -------: |
| EPYC 7763 AVX2       |  +27% |  +24% |  +22% |  +17% |    +27% |  +13% |   +16% |     +51% |
| EPYC 7763 SSE2       |  +20% |  +20% |  +20% |  +17% |    +23% |  +14% |   +16% |     +51% |
| EPYC 7763 scalar     |  +59% |  +49% |  +44% |  +52% |    +41% |  +18% |   +24% |     +51% |
| EPYC 9V74 AVX2       |  +27% |  +23% |  +23% |  +23% |    +29% |  +17% |   +25% |     +60% |
| EPYC 9V74 SSE2       |  +29% |  +24% |  +23% |  +21% |    +25% |  +18% |   +25% |     +59% |
| EPYC 9V74 scalar     |  +74% |  +62% |  +58% |  +64% |    +55% |  +16% |   +33% |     +60% |
| EPYC 9V45 AVX2       |  +24% |  +20% |  +20% |  +19% |    +18% |  +28% |   +28% |     +55% |
| EPYC 9V45 SSE2       |  +28% |  +24% |  +22% |  +22% |    +23% |  +29% |   +28% |     +55% |
| EPYC 9V45 scalar     |  +97% |  +77% |  +71% |  +85% |    +67% |  +21% |   +38% |     +55% |
| i7-8700B AVX2        |  +28% |  +15% |  +20% |  +17% |    +12% |  +18% |   +27% |     +31% |
| i7-8700B SSE2        |   +6% |   +1% |   +7% |   +2% |     +8% |  +14% |   +35% |     +28% |
| i7-8700B scalar      |  +79% |  +69% |  +59% |  +70% |    +48% |  +21% |   +50% |     +31% |
| Neoverse-N2 NEON     |  +14% |  +18% |  +18% |  +18% |    +20% |  +17% |   +27% |     +69% |
| Neoverse-N2 scalar   |   +7% |   +6% |   +6% |   +9% |     +5% |  +14% |   +24% |     +69% |
| M4¹ NEON             |  +24% |  +22% |  +20% |  +24% |    +21% |  +13% |   +43% |     +18% |
| M4¹ scalar           |  +31% |  +17% |  +16% |  +36% |    +13% |   +9% |   +40% |     +17% |
| Ultra 9 285K² AVX2   |   +5% |   +6% |   +7% |   +4% |    +10% |   +6% |   +30% |     +69% |
| Ultra 9 285K² SSE2   |   +8% |   +7% |   +9% |   +8% |    +10% |   +6% |   +30% |     +68% |
| Ultra 9 285K² scalar |  +90% |  +75% |  +67% |  +71% |    +62% |  +17% |   +42% |     +69% |

JPEG decompression, vio vs libjpeg-turbo:

| CPU, SIMD            | 4:4:4 | 4:2:2 | 4:2:0 |  gray | fastDCT | prog. | arith. | lossless |
| -------------------- | ----: | ----: | ----: | ----: | ------: | ----: | -----: | -------: |
| EPYC 7763 AVX2       |  +20% |  +21% |  +21% |  +21% |    +27% |   +6% |    −7% |      +5% |
| EPYC 7763 SSE2       |  +25% |  +25% |  +23% |  +23% |    +24% |   +7% |    −7% |      +5% |
| EPYC 7763 scalar     |   +7% |   +7% |   +7% |  +11% |    +14% |   +3% |    −7% |      +5% |
| EPYC 9V74 AVX2       |  +32% |  +29% |  +29% |  +35% |    +32% |   +5% |    −2% |      +9% |
| EPYC 9V74 SSE2       |  +34% |  +31% |  +29% |  +36% |    +28% |   +6% |    −2% |      +9% |
| EPYC 9V74 scalar     |  +10% |   +8% |   +8% |  +16% |    +16% |   +2% |    −2% |      +9% |
| EPYC 9V45 AVX2       |  +33% |  +30% |  +29% |  +31% |    +32% |  +13% |    +3% |     +10% |
| EPYC 9V45 SSE2       |  +38% |  +34% |  +31% |  +35% |    +28% |  +12% |    +0% |      +9% |
| EPYC 9V45 scalar     |  +16% |  +12% |  +12% |  +21% |    +17% |   +8% |    +0% |     +10% |
| i7-8700B AVX2        |  +22% |  +10% |  +18% |  +15% |    +18% |   −0% |   +20% |     +30% |
| i7-8700B SSE2        |  +15% |  +13% |  +14% |  +13% |     +8% |   +3% |   +18% |     +22% |
| i7-8700B scalar      |   +5% |   +6% |   +5% |  +11% |     +7% |   −1% |   +15% |     +22% |
| Neoverse-N2 NEON     |  +27% |  +28% |  +29% |  +35% |    +32% |   +8% |    +4% |      +9% |
| Neoverse-N2 scalar   |  +16% |  +16% |  +16% |  +27% |    +26% |   +6% |    +4% |      +9% |
| M4¹ NEON             |  +41% |  +24% |  +25% |  +33% |    +28% |  +11% |   +18% |     +10% |
| M4¹ scalar           |  +14% |  +12% |   +5% |  +22% |     +9% |   +5% |   +17% |     +12% |
| Ultra 9 285K² AVX2   |  +16% |  +15% |  +14% |  +14% |    +17% |  +11% |    +4% |      +9% |
| Ultra 9 285K² SSE2   |  +15% |  +15% |  +14% |  +14% |    +16% |  +12% |    +5% |      +9% |
| Ultra 9 285K² scalar |   +0% |   +1% |   +3% |   +9% |    +11% |   +8% |    +3% |      +9% |

With SIMD, baseline compression is 4–30% faster than libjpeg-turbo (1–8% on
the macOS x86-64 runner with SSE2) and sequential decompression 10–41%;
without SIMD, compression is 41–97% faster on x86 and 5–36% on AArch64.
Progressive compression is 6–29% faster, arithmetic-coded 16–50% and
lossless 17–69%.  Progressive decompression ranges from 1% slower to 13%
faster, lossless decompression is 5–30% faster, and arithmetic-coded
decompression ranges from 7% slower (Zen 3) to 20% faster.  Shared CI
runners are noisy (the macOS x86-64 runner most of all), so differences of a
few percent are within run-to-run variation.

## Building and testing

The test programs and images come from a submodule:

```sh
git submodule update --init
cmake --preset native            # build tree in build/native
cmake --build --preset native
ctest --preset native -j 8
```

Other presets: `debug`, `x64` (macOS x86-64 under Rosetta), `a32` (AArch32
NEON code paths on an AArch64 host).  Every build tree lives under `build/`.
Options: `VIO_BUILD_TESTS` (on when vio is the top-level project),
`VIO_TEST_SCALAR` (also test every program with SIMD disabled, as the
`-scalar` variant).

On Windows, configure from a Visual Studio developer command prompt with
Ninja (add `-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl` for
clang-cl):

```sh
cmake -S . -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/msvc
ctest --test-dir build/msvc -j 8
```

To benchmark against libjpeg-turbo, build it from the submodule into a tree
of its own; to add IJG libjpeg, unpack its source (`jpegsrc.v10.tar.gz` from
ijg.org) and point `VIO_LIBJPEG_SOURCE_DIR` at it, which builds
`tjbench-libjpeg`:

```sh
cmake -S tests/libjpeg-turbo -B build/libjpeg-turbo -DCMAKE_BUILD_TYPE=Release
cmake --build build/libjpeg-turbo -j 8   # tjbench
cmake --preset native -DVIO_LIBJPEG_SOURCE_DIR=$PWD/build/jpeg-10
cmake --build build/native --target tjbench-libjpeg
```

## Repository layout

```
include/vio/
├── core.hpp                          shared by all codecs: color spaces,
│                                     vio::Error, row reader/writer interfaces
├── bmp.hpp gif.hpp ppm.hpp targa.hpp one file per format; needs only core.hpp
├── png.hpp                           own DEFLATE (zlib-identical output) and
│                                     checksums
├── jpeg.hpp                          includes the jpeg/ public API
└── jpeg/
    ├── decoder.hpp encoder.hpp       public API
    │   transform.hpp types.hpp
    ├── config.hpp                    compiler macros, SIMD back-end selection
    │                                 (VIO_NO_SIMD, ...)
    ├── detail/                       codec internals
    │   ├── compress*.hpp             compression
    │   ├── decompress*.hpp           decompression
    │   ├── huff_*.hpp arith_*.hpp    entropy coding
    │   └── fdct.hpp idct.hpp         scalar DCTs
    └── simd/
        ├── common.hpp                types shared by scalar code and kernels
        ├── cpu.hpp                   run-time ISA detection, JSIMD_* env vars
        ├── dispatch.hpp              per-function kernel selection
        ├── generic/                  kernels written once against a small
        │                             vector interface `V`
        └── x86/ neon/ altivec/       back ends: <isa>.hpp instantiates the
            mmi/ rvv/ vecext/         kernels, kernels.hpp selects within an
                                      ISA family, v*.inc implements `V`
                                      (vecext/: compiler vector extensions)
tests/                                conformance suite: test definitions,
                                      test programs and images (submodule),
                                      command-line tools the tests run
                                      (tools/: jpeg-encode, jpeg-decode,
                                      jpeg-transform, jpeg-example)
```

## License

MIT; `LICENSE` also contains the third-party notices that apply.
