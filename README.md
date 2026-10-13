# vio

Header-only C++17 image I/O.  The JPEG codec supports sequential,
progressive, arithmetic-coded and lossless JPEG with 8- to 16-bit samples,
lossless transforms, and SIMD acceleration for x86 (MMX through AVX2), Arm
NEON, PowerPC AltiVec, Loongson MMI and RISC-V Vector.  It is faster than
libjpeg-turbo in almost every configuration measured (see
[Performance](#performance)).

## Using the library

Add `include/` to the include path (or link the `vio::vio` CMake target) and
include only the formats you need.  vio compiles as C++17 or later and needs
no other compiler options or macros:

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
data.  Encoded files are passed as `vio::span<const uint8_t>`, which converts
from `std::vector`, `std::array`, `{pointer, size}` and, in C++20, `std::span`.

## Features

Compared with libjpeg-turbo 3.2 and IJG libjpeg 10.  All three run on any
architecture through portable code; libjpeg, written in plain ANSI C, builds
with the widest range of compilers and systems, while vio needs a C++17
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
| Toolchains: compilers without C++17                        |  ✗  |       ✓       |    ✓    |
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
is the best of 5 runs (of 10 or 15 for the machines measured two or three
times).  Results from commit `76856a6`, built as C++17.  Hosted Linux x86-64
runners come with different processors; the on-demand workflow
`bench-x64.yml` runs the benchmark on several at once, which measured the
three AMD EPYC generations below.

How much faster vio is than libjpeg-turbo and than IJG libjpeg (which has no
SIMD code): +20% means 1.2 times the throughput.  Geometric mean over the
configurations below except lossless, which libjpeg does not support.

JPEG compression:

| Platform       | CPU                      | SIMD   | vs libjpeg-turbo | vs libjpeg |
| -------------- | ------------------------ | ------ | ---------------: | ---------: |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | AVX2   |             +22% |      +476% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | SSE2   |             +19% |      +422% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | scalar |             +40% |       +98% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | AVX2   |             +25% |      +519% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | SSE2   |             +23% |      +436% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | scalar |             +50% |      +121% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | AVX2   |             +22% |      +548% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | SSE2   |             +24% |      +478% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | scalar |             +64% |      +139% |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   |             +18% |      +437% |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |             +10% |      +334% |
| macOS x86-64   | Intel Core i7-8700B      | scalar |             +55% |      +127% |
| Linux AArch64  | Neoverse-N2              | NEON   |             +19% |      +303% |
| Linux AArch64  | Neoverse-N2              | scalar |             +10% |      +124% |
| macOS AArch64  | Apple M4¹                | NEON   |             +23% |      +443% |
| macOS AArch64  | Apple M4¹                | scalar |             +23% |      +200% |
| Windows x86-64 | Intel Core Ultra 9 285K² | AVX2   |             +19% |      +464% |
| Windows x86-64 | Intel Core Ultra 9 285K² | SSE2   |             +19% |      +401% |
| Windows x86-64 | Intel Core Ultra 9 285K² | scalar |             +70% |      +142% |

JPEG decompression:

| Platform       | CPU                      | SIMD   | vs libjpeg-turbo | vs libjpeg |
| -------------- | ------------------------ | ------ | ---------------: | ---------: |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | AVX2   |             +15% |      +147% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | SSE2   |             +17% |      +136% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | scalar |              +6% |       +28% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | AVX2   |             +20% |      +155% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | SSE2   |             +21% |      +136% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | scalar |              +7% |       +29% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | AVX2   |             +23% |      +151% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | SSE2   |             +26% |      +136% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | scalar |             +13% |       +37% |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   |              +7% |      +114% |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |              +5% |       +90% |
| macOS x86-64   | Intel Core i7-8700B      | scalar |              +1% |       +27% |
| Linux AArch64  | Neoverse-N2              | NEON   |             +22% |       +92% |
| Linux AArch64  | Neoverse-N2              | scalar |             +15% |       +22% |
| macOS AArch64  | Apple M4¹                | NEON   |             +24% |      +103% |
| macOS AArch64  | Apple M4¹                | scalar |             +11% |       +24% |
| Windows x86-64 | Intel Core Ultra 9 285K² | AVX2   |             +12% |       +81% |
| Windows x86-64 | Intel Core Ultra 9 285K² | SSE2   |             +12% |       +67% |
| Windows x86-64 | Intel Core Ultra 9 285K² | scalar |              +5% |        +9% |

¹ Measured on a local Mac (native, same build and benchmark script)
instead of the GitHub-hosted runner, which is a virtualized Apple M1.
² Measured on a local Windows 11 machine with MSVC 19.44 (Visual Studio
2022), pinned to the performance cores.  Built with clang-cl 19 instead
(vio and libjpeg-turbo), vio compresses 25% (AVX2), 18% (SSE2) and 81%
(scalar) faster than libjpeg-turbo and decompresses 21%, 22% and 12% faster.

Per configuration, on the same machines (by CPU; the summary gives their
platforms), each summary figure above being the geometric mean of the first
seven columns of its row. Columns: 4:4:4, 4:2:2, 4:2:0 and gray at quality
95; fastDCT: 4:2:0 with the fast integer DCT; prog.: 4:2:0 progressive;
arith.: 4:2:0 arithmetic-coded.

JPEG compression, vio vs libjpeg-turbo:

| CPU, SIMD            | 4:4:4 | 4:2:2 | 4:2:0 |  gray | fastDCT | prog. | arith. | lossless |
| -------------------- | ----: | ----: | ----: | ----: | ------: | ----: | -----: | -------: |
| EPYC 7763 AVX2       |  +27% |  +24% |  +23% |  +18% |    +28% |  +19% |   +15% |     +56% |
| EPYC 7763 SSE2       |  +20% |  +20% |  +20% |  +15% |    +22% |  +20% |   +15% |     +56% |
| EPYC 7763 scalar     |  +58% |  +48% |  +44% |  +51% |    +41% |  +19% |   +24% |     +56% |
| EPYC 9V74 AVX2       |  +28% |  +24% |  +24% |  +24% |    +31% |  +23% |   +23% |     +67% |
| EPYC 9V74 SSE2       |  +29% |  +23% |  +22% |  +19% |    +24% |  +24% |   +23% |     +67% |
| EPYC 9V74 scalar     |  +72% |  +61% |  +58% |  +62% |    +55% |  +17% |   +31% |     +67% |
| EPYC 9V45 AVX2       |  +23% |  +21% |  +19% |  +16% |    +18% |  +30% |   +27% |     +56% |
| EPYC 9V45 SSE2       |  +27% |  +21% |  +21% |  +19% |    +20% |  +28% |   +29% |     +55% |
| EPYC 9V45 scalar     |  +95% |  +77% |  +75% |  +81% |    +72% |  +20% |   +38% |     +55% |
| i7-8700B AVX2        |  +16% |  +17% |  +19% |  +15% |    +14% |  +17% |   +33% |     +22% |
| i7-8700B SSE2        |   +7% |   +4% |   +7% |   +5% |     +3% |  +13% |   +30% |     +21% |
| i7-8700B scalar      |  +88% |  +69% |  +65% |  +67% |    +46% |  +18% |   +44% |     +21% |
| Neoverse-N2 NEON     |  +14% |  +17% |  +16% |  +19% |    +18% |  +20% |   +27% |     +68% |
| Neoverse-N2 scalar   |   +7% |   +6% |   +6% |   +8% |     +5% |  +12% |   +24% |     +68% |
| M4¹ NEON             |  +18% |  +23% |  +21% |  +24% |    +22% |  +12% |   +45% |     +14% |
| M4¹ scalar           |  +31% |  +17% |  +16% |  +36% |    +12% |   +8% |   +42% |     +14% |
| Ultra 9 285K² AVX2   |  +18% |  +19% |  +20% |  +19% |    +18% |   +9% |   +30% |     +70% |
| Ultra 9 285K² SSE2   |  +19% |  +17% |  +18% |  +18% |    +20% |   +9% |   +30% |     +69% |
| Ultra 9 285K² scalar | +105% |  +91% |  +84% |  +92% |    +80% |  +19% |   +42% |     +70% |

JPEG decompression, vio vs libjpeg-turbo:

| CPU, SIMD            | 4:4:4 | 4:2:2 | 4:2:0 |  gray | fastDCT | prog. | arith. | lossless |
| -------------------- | ----: | ----: | ----: | ----: | ------: | ----: | -----: | -------: |
| EPYC 7763 AVX2       |  +21% |  +22% |  +22% |  +21% |    +28% |   +4% |    −8% |      +5% |
| EPYC 7763 SSE2       |  +28% |  +26% |  +24% |  +27% |    +25% |   +5% |    −8% |      +5% |
| EPYC 7763 scalar     |   +7% |   +7% |   +7% |  +12% |    +14% |   +2% |    −8% |      +5% |
| EPYC 9V74 AVX2       |  +32% |  +29% |  +30% |  +28% |    +32% |   +5% |    −6% |      +9% |
| EPYC 9V74 SSE2       |  +35% |  +31% |  +29% |  +29% |    +28% |   +6% |    −6% |      +9% |
| EPYC 9V74 scalar     |  +10% |   +9% |   +8% |  +12% |    +16% |   +3% |    −6% |      +8% |
| EPYC 9V45 AVX2       |  +32% |  +31% |  +26% |  +31% |    +32% |  +10% |    +2% |     +11% |
| EPYC 9V45 SSE2       |  +41% |  +33% |  +30% |  +35% |    +29% |  +12% |    +3% |     +10% |
| EPYC 9V45 scalar     |  +17% |  +13% |  +12% |  +20% |    +19% |   +7% |    +1% |     +12% |
| i7-8700B AVX2        |   +5% |   +7% |   +8% |   +5% |     +7% |   +2% |   +17% |     +22% |
| i7-8700B SSE2        |   +6% |   +5% |   +4% |   +4% |     +0% |   −2% |   +18% |     +21% |
| i7-8700B scalar      |   −1% |   −4% |   −2% |   +1% |     −2% |   +0% |   +14% |     +20% |
| Neoverse-N2 NEON     |  +27% |  +27% |  +29% |  +35% |    +32% |   +8% |    +3% |      +7% |
| Neoverse-N2 scalar   |  +16% |  +16% |  +16% |  +26% |    +26% |   +6% |    +3% |      +7% |
| M4¹ NEON             |  +30% |  +24% |  +25% |  +31% |    +28% |  +13% |   +17% |     +10% |
| M4¹ scalar           |  +14% |  +10% |   +5% |  +22% |     +9% |   +5% |   +15% |     +11% |
| Ultra 9 285K² AVX2   |  +14% |  +13% |  +13% |  +12% |    +15% |  +12% |    +5% |      +8% |
| Ultra 9 285K² SSE2   |  +15% |  +14% |  +14% |  +12% |    +15% |  +12% |    +6% |      +8% |
| Ultra 9 285K² scalar |   +0% |   +1% |   +2% |   +8% |    +11% |   +9% |    +5% |      +9% |

With SIMD, baseline compression is 14–31% faster than libjpeg-turbo (3–7% on
the macOS x86-64 runner with SSE2) and sequential decompression 12–41% (0–8%
on the macOS x86-64 runner); without SIMD, compression is 41–105% faster on
x86 and 5–36% on AArch64.  Progressive compression is 8–30% faster,
arithmetic-coded 15–45% and lossless 14–70%.  Progressive decompression
ranges from 2% slower to 13% faster, lossless decompression is 5–22% faster,
and arithmetic-coded decompression ranges from 8% slower (Zen 3) to 18%
faster.  Shared CI
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
`-scalar` variant), `CMAKE_CXX_STANDARD` (17 by default; CI also builds the
tests as C++20 with GCC and MSVC).

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
├── core.hpp                          shared by all codecs: vio::span, color
│                                     spaces, vio::Error, row reader/writer
│                                     interfaces
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
