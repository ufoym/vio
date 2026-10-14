# vio

Header-only C++20 image I/O.  The JPEG codec supports sequential,
progressive, arithmetic-coded and lossless JPEG with 8- to 16-bit samples,
lossless transforms, and SIMD acceleration for x86 (MMX through AVX2), Arm
NEON, PowerPC AltiVec, Loongson MMI and RISC-V Vector.  It is faster than
libjpeg-turbo in every configuration measured (see
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
is the best of 5 runs.  Results from commit `5e4033e`.  Hosted Linux x86-64
runners come with different processors; the on-demand workflow
`bench-x64.yml` runs the benchmark on several at once, which measured the
three AMD EPYC generations below (where several runners had the same
processor, each figure is the median over them).

How much faster vio is than libjpeg-turbo and than IJG libjpeg (which has no
SIMD code): +20% means 1.2 times the throughput.  Geometric mean over the
configurations below except lossless, which libjpeg does not support.

JPEG compression:

| Platform       | CPU                      | SIMD   | vs libjpeg-turbo | vs libjpeg |
| -------------- | ------------------------ | ------ | ---------------: | ---------: |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | AVX2   |             +34% |      +534% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | SSE2   |             +29% |      +465% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | scalar |             +50% |      +111% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | AVX2   |             +41% |      +594% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | SSE2   |             +34% |      +484% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | scalar |             +54% |      +127% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | AVX2   |             +42% |      +651% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | SSE2   |             +37% |      +529% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | scalar |             +71% |      +156% |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   |             +33% |      +490% |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |             +19% |      +379% |
| macOS x86-64   | Intel Core i7-8700B      | scalar |             +72% |      +136% |
| Linux AArch64  | Neoverse-N2              | NEON   |             +24% |      +324% |
| Linux AArch64  | Neoverse-N2              | scalar |             +35% |      +177% |
| macOS AArch64  | Apple M4¹                | NEON   |             +54% |      +580% |
| macOS AArch64  | Apple M4¹                | scalar |             +63% |      +299% |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | AVX2   |             +33% |          — |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | SSE2   |             +28% |          — |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | scalar |             +47% |          — |

JPEG decompression:

| Platform       | CPU                      | SIMD   | vs libjpeg-turbo | vs libjpeg |
| -------------- | ------------------------ | ------ | ---------------: | ---------: |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | AVX2   |             +32% |      +184% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | SSE2   |             +34% |      +170% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | scalar |             +20% |       +45% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | AVX2   |             +42% |      +201% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | SSE2   |             +41% |      +176% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | scalar |             +22% |       +47% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | AVX2   |             +43% |      +196% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | SSE2   |             +44% |      +167% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | scalar |             +26% |       +58% |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   |             +23% |      +140% |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |             +14% |      +115% |
| macOS x86-64   | Intel Core i7-8700B      | scalar |             +20% |       +44% |
| Linux AArch64  | Neoverse-N2              | NEON   |             +37% |      +115% |
| Linux AArch64  | Neoverse-N2              | scalar |             +28% |       +35% |
| macOS AArch64  | Apple M4¹                | NEON   |             +48% |      +142% |
| macOS AArch64  | Apple M4¹                | scalar |             +47% |       +64% |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | AVX2   |             +30% |          — |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | SSE2   |             +26% |          — |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | scalar |             +22% |          — |

¹ Measured on a local Mac (native, same build and benchmark script)
instead of the GitHub-hosted runner, which is a virtualized Apple M1.
² The GitHub-hosted Windows runner, with MSVC; it has no IJG libjpeg build,
hence no comparison with libjpeg.

Per configuration, on the same machines (by CPU; the summary gives their
platforms), each summary figure above being the geometric mean of the first
seven columns of its row. Columns: 4:4:4, 4:2:2, 4:2:0 and gray at quality
95; fastDCT: 4:2:0 with the fast integer DCT; prog.: 4:2:0 progressive;
arith.: 4:2:0 arithmetic-coded.

JPEG compression, vio vs libjpeg-turbo:

| CPU, SIMD            | 4:4:4 | 4:2:2 | 4:2:0 |  gray | fastDCT | prog. | arith. | lossless |
| -------------------- | ----: | ----: | ----: | ----: | ------: | ----: | -----: | -------: |
| EPYC 7763 AVX2       |  +35% |  +31% |  +29% |  +40% |    +38% |  +35% |   +33% |     +33% |
| EPYC 7763 SSE2       |  +30% |  +26% |  +23% |  +30% |    +25% |  +34% |   +33% |     +33% |
| EPYC 7763 scalar     |  +67% |  +57% |  +49% |  +65% |    +45% |  +28% |   +41% |     +33% |
| EPYC 9V74 AVX2       |  +37% |  +34% |  +33% |  +47% |    +44% |  +37% |   +52% |     +34% |
| EPYC 9V74 SSE2       |  +37% |  +30% |  +27% |  +33% |    +28% |  +36% |   +51% |     +34% |
| EPYC 9V74 scalar     |  +74% |  +61% |  +53% |  +66% |    +49% |  +25% |   +55% |     +34% |
| EPYC 9V45 AVX2       |  +33% |  +32% |  +33% |  +50% |    +32% |  +59% |   +60% |     +34% |
| EPYC 9V45 SSE2       |  +34% |  +29% |  +26% |  +36% |    +26% |  +57% |   +58% |     +32% |
| EPYC 9V45 scalar     | +100% |  +80% |  +70% |  +94% |    +67% |  +30% |   +67% |     +31% |
| i7-8700B AVX2        |  +42% |  +30% |  +29% |  +35% |    +25% |  +62% |   +16% |     +36% |
| i7-8700B SSE2        |  +14% |  +13% |  +11% |  +17% |    +11% |  +60% |   +15% |     +57% |
| i7-8700B scalar      |  +94% |  +90% |  +86% |  +95% |    +59% |  +44% |   +46% |     +38% |
| Neoverse-N2 NEON     |  +17% |  +15% |  +14% |  +20% |    +16% |  +47% |   +44% |     +75% |
| Neoverse-N2 scalar   |  +28% |  +30% |  +32% |  +32% |    +34% |  +43% |   +49% |     +75% |
| M4¹ NEON             |  +40% |  +39% |  +37% |  +60% |    +35% | +104% |   +74% |     +55% |
| M4¹ scalar           |  +54% |  +57% |  +59% |  +70% |    +54% |  +67% |   +80% |     +55% |
| EPYC 7763² AVX2      |  +28% |  +24% |  +22% |  +29% |    +28% |  +41% |   +68% |     +52% |
| EPYC 7763² SSE2      |  +22% |  +16% |  +17% |  +24% |    +19% |  +40% |   +66% |     +52% |
| EPYC 7763² scalar    |  +55% |  +47% |  +42% |  +48% |    +41% |  +29% |   +69% |     +52% |

JPEG decompression, vio vs libjpeg-turbo:

| CPU, SIMD            | 4:4:4 | 4:2:2 | 4:2:0 |  gray | fastDCT | prog. | arith. | lossless |
| -------------------- | ----: | ----: | ----: | ----: | ------: | ----: | -----: | -------: |
| EPYC 7763 AVX2       |  +26% |  +29% |  +29% |  +25% |    +35% |  +49% |   +32% |     +31% |
| EPYC 7763 SSE2       |  +33% |  +33% |  +30% |  +31% |    +30% |  +49% |   +32% |     +31% |
| EPYC 7763 scalar     |  +12% |  +12% |  +17% |  +19% |    +21% |  +29% |   +29% |     +31% |
| EPYC 9V74 AVX2       |  +40% |  +37% |  +37% |  +41% |    +41% |  +54% |   +44% |     +23% |
| EPYC 9V74 SSE2       |  +42% |  +38% |  +35% |  +43% |    +35% |  +53% |   +43% |     +22% |
| EPYC 9V74 scalar     |  +14% |  +12% |  +17% |  +25% |    +22% |  +29% |   +38% |     +22% |
| EPYC 9V45 AVX2       |  +40% |  +36% |  +35% |  +37% |    +39% |  +75% |   +46% |     +39% |
| EPYC 9V45 SSE2       |  +46% |  +39% |  +37% |  +41% |    +32% |  +72% |   +44% |     +38% |
| EPYC 9V45 scalar     |  +19% |  +14% |  +17% |  +28% |    +23% |  +45% |   +40% |     +38% |
| i7-8700B AVX2        |  +13% |  +18% |  +22% |  +11% |    +19% |  +30% |   +49% |     +23% |
| i7-8700B SSE2        |  +10% |   +9% |  +11% |  +12% |     +8% |  +24% |   +26% |     +28% |
| i7-8700B scalar      |  +11% |  +12% |  +26% |   +2% |    +21% |  +26% |   +48% |     +33% |
| Neoverse-N2 NEON     |  +31% |  +31% |  +33% |  +39% |    +37% |  +48% |   +39% |     +48% |
| Neoverse-N2 scalar   |  +29% |  +18% |  +21% |  +33% |    +29% |  +28% |   +35% |     +48% |
| M4¹ NEON             |  +43% |  +38% |  +40% |  +52% |    +46% |  +72% |   +47% |     +79% |
| M4¹ scalar           |  +40% |  +41% |  +50% |  +37% |    +58% |  +57% |   +48% |     +78% |
| EPYC 7763² AVX2      |  +25% |  +26% |  +25% |  +25% |    +27% |  +40% |   +45% |     +40% |
| EPYC 7763² SSE2      |  +24% |  +15% |  +20% |  +28% |    +21% |  +37% |   +42% |     +41% |
| EPYC 7763² scalar    |  +11% |  +15% |  +19% |  +16% |    +23% |  +29% |   +41% |     +42% |

With SIMD, baseline compression is 11–60% faster than libjpeg-turbo (11–17%
on the macOS x86-64 runner with SSE2) and sequential decompression 8–52%;
without SIMD, compression is 41–100% faster on x86 and 28–70% on AArch64,
and sequential decompression 2–28% on x86 and 18–58% on AArch64.
Progressive compression is 25–104% faster, arithmetic-coded 15–80% and
lossless 31–75%; progressive decompression is 24–75% faster, arithmetic-coded
26–49% and lossless 22–79%.  Shared CI
runners are noisy (the macOS x86-64 runner most of all), so differences of a
few percent are within run-to-run variation.

### Optimization log

Throughput gain of each change over the build before it (A/B on the same machine).

- Marker-free 64-bit bit reader for progressive scans, bitmap-driven refinement, two-symbol AC lookup ([3e78226](https://github.com/ufoym/vio/commit/3e78226)): progressive decompression +42% (Cortex-X925), arithmetic decoding +30–34%, sequential decoding +7–9%.
- Batched progressive AC encoding with word-packed correction bits; branch-free lossless and arithmetic encoders ([545fc4b](https://github.com/ufoym/vio/commit/545fc4b)): progressive compression +30–60%, arithmetic compression +17–33%.
- Per-sample decompression color conversion and upsampling only with 32-bit vector multiplies ([8a012f5](https://github.com/ufoym/vio/commit/8a012f5)): scalar decompression +7–9% (x86-64 baseline).
- SIMD non-zero masks for refinement decoding ([3b14b7b](https://github.com/ufoym/vio/commit/3b14b7b)): progressive decompression +5% (Zen 3, AVX2).
- Register-resident Huffman bit writer under MSVC ([4dd1653](https://github.com/ufoym/vio/commit/4dd1653)): compression +5% (AVX2), +3% (SSE2).
- calloc-backed zeroed buffers ([3193dec](https://github.com/ufoym/vio/commit/3193dec)): progressive decompression +1.5–2% (Windows).
- Low-to-high correction-bit application, removing an lzcnt dependency chain ([bd858b0](https://github.com/ufoym/vio/commit/bd858b0)): progressive decompression +4–14%.
- SWAR fancy upsampling (four 16-bit lanes per 64-bit word) ([c1674e9](https://github.com/ufoym/vio/commit/c1674e9), [0504ca5](https://github.com/ufoym/vio/commit/0504ca5)): scalar decompression +2.5–5%.
- 16-byte SWAR scan for byte stuffing ([76d0383](https://github.com/ufoym/vio/commit/76d0383)): compression +0.8% (Apple M4).
- Packed 64-bit YCbCr→RGB tables for scalar 8-bit decoding ([4e2bea0](https://github.com/ufoym/vio/commit/4e2bea0)): scalar decompression +3–4% (MSVC).
- Huffman encoding software-pipelined across blocks over 32-block MCU runs ([692e995](https://github.com/ufoym/vio/commit/692e995)): compression +4–26%.
- Software-pipelined progressive AC encoding ([3e6f49f](https://github.com/ufoym/vio/commit/3e6f49f)): progressive compression +5–19%.
- Refinement non-zero masks computed one block ahead ([c7a099f](https://github.com/ufoym/vio/commit/c7a099f)): progressive decompression +2–5% (Arm).
- Row-batched forward DCT, two interleaved blocks per Neon call ([5e4033e](https://github.com/ufoym/vio/commit/5e4033e)): compression +1–9%.
- Byte-level transpose shared by both blocks of the Neon two-block forward DCT ([68f3a1d](https://github.com/ufoym/vio/commit/68f3a1d)): compression +0.3–2.5%.
- Base-pointer table indexing in the Huffman encoder loops, sparing GCC per-coefficient address arithmetic ([62e36ff](https://github.com/ufoym/vio/commit/62e36ff)): compression +3–8% (GCC on Arm).
- Progressive AC bands from zig-zag position 1 or 6 built in registers by table lookups and lane extracts instead of per-coefficient gathers ([571291d](https://github.com/ufoym/vio/commit/571291d)): progressive compression +7–10% (Arm), +6% (x86).
- New-coefficient bitmap from the refinement prepare kernels' vector compares, so correction bits are gathered only over previously non-zero coefficients ([f375a10](https://github.com/ufoym/vio/commit/f375a10)): progressive compression +11–24% (Arm), +19% (x86).
- Grayscale-to-RGB output as one 32-bit store per pixel on x86, where the byte-store loop stays scalar ([3317d93](https://github.com/ufoym/vio/commit/3317d93)): grayscale decompression +2–23% (x86).

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
