# vio

Header-only C++20 image I/O.  The JPEG codec supports sequential,
progressive, arithmetic-coded and lossless JPEG with 8- to 16-bit samples,
lossless transforms, and SIMD acceleration for x86 (MMX through AVX2), Arm
NEON, PowerPC AltiVec, Loongson MMI and RISC-V Vector.  It is faster than
libjpeg-turbo in every configuration measured.  The PNG codec, with its own
DEFLATE implementation, decodes faster than image-png and encodes faster
than fpnge and image-png with SIMD, and faster than libpng at the same
compression at every SIMD level, with byte-identical files in its zlib mode
(see [Performance](#performance)).

## Using the library

Add `include/` to the include path (or link the `vio::vio` CMake target) and
include only the formats you need:

| Header           | API                                                      |
| ---------------- | -------------------------------------------------------- |
| `vio/jpeg.hpp`   | `vio::jpeg::Decoder`, `Encoder`, lossless transforms (or include `vio/jpeg/decoder.hpp` etc. individually) |
| `vio/png.hpp`    | `vio::png::Reader<T>`, `Writer<T>`; whole-image `Decoder`, `Encoder` (with `Transforms`, `WriteTransforms`); `StreamDecoder` (progressive); `FormatReader`, `FormatWriter` (pixel format conversion) |
| `vio/bmp.hpp`    | `vio::bmp::Reader`, `Writer`                             |
| `vio/gif.hpp`    | `vio::gif::Reader`, `Writer<T>`                          |
| `vio/ppm.hpp`    | `vio::ppm::Reader<T>`, `Writer<T>`                       |
| `vio/targa.hpp`  | `vio::targa::Reader`, `Writer`                           |

The packed-pixel readers and writers implement `vio::ImageReader<T>` /
`vio::ImageWriter<T>` (`vio/core.hpp`), which exchange rows of
samples; `T` is `uint8_t`, `int16_t` or `uint16_t` for 8-, 12- and 16-bit
data.

## Features

Compared with libjpeg-turbo 3.2, IJG libjpeg 10 and stb_image 2.30 (with
stb_image_write 1.16 for encoding).  All four run on any architecture through
portable code; libjpeg, written in plain ANSI C, builds with the widest range
of compilers and systems, while vio needs a C++20 compiler.

| Feature                                                    | vio | libjpeg-turbo | libjpeg | stb_image |
| ---------------------------------------------------------- | :-: | :-----------: | :-----: | :-------: |
| SIMD: x86-64 SSE2, AVX2                                    |  ✓  |       ✓       |    ✗    |     ◐⁶    |
| SIMD: i386 MMX, SSE, SSE2, AVX2                            |  ✓  |       ✓       |    ✗    |     ◐⁶    |
| SIMD: AArch64 NEON                                         |  ✓  |       ✓       |    ✗    |     ✓     |
| SIMD: AArch32 NEON                                         |  ✓  |       ✓       |    ✗    |     ✓     |
| SIMD: AArch32 NEON detected at run time                    |  ✓¹ |       ✓       |    ✗    |     ✗     |
| SIMD: PowerPC AltiVec                                      |  ✓  |       ✓       |    ✗    |     ✗     |
| SIMD: MIPS64 Loongson MMI                                  |  ✓² |       ✓       |    ✗    |     ✗     |
| SIMD: RISC-V 64 RVV 1.0                                    |  ✓³ |       ✓       |    ✗    |     ✗     |
| SIMD: run-time selection via `JSIMD_*` variables           |  ✓  |       ✓       |    ✗    |     ✗     |
| Toolchains: Linux GCC, Clang                               |  ✓  |       ✓       |    ✓    |     ✓     |
| Toolchains: macOS Apple Clang                              |  ✓  |       ✓       |    ✓    |     ✓     |
| Toolchains: Windows MSVC, clang-cl                         |  ✓  |       ✓       |    ✓    |     ✓     |
| Toolchains: compilers without C++20                        |  ✗  |       ✓       |    ✓    |     ✓     |
| JPEG: baseline and extended sequential                     |  ✓  |       ✓       |    ✓    |     ✓     |
| JPEG: progressive                                          |  ✓  |       ✓       |    ✓    |     ◐⁷    |
| JPEG: arithmetic coding                                    |  ✓  |       ✓       |    ✓    |     ✗     |
| JPEG: lossless (2- to 16-bit)                              |  ✓  |       ✓       |    ✗    |     ✗     |
| JPEG: 12-bit lossy                                         |  ✓  |       ✓       |    ✓⁴   |     ✗     |
| JPEG: lossless transforms (rotate, flip, crop, wipe, drop) |  ✓  |       ✓       |    ✓    |     ✗     |
| JPEG: lossless roll transform                              |  ✓  |       ✓       |    ✗    |     ✗     |
| JPEG: lossless negate transform                            |  ✗  |       ✗       |    ✓    |     ✗     |
| JPEG: SmartScale (non-standard DCT block sizes)            |  ✗  |       ✗       |    ✓    |     ✗     |
| JPEG: scaled decoding                                      |  ✓  |       ✓       |    ✓    |     ✗     |
| JPEG: partial decoding (crop, skip rows)                   |  ✓  |       ✓       |    ✗    |     ✗     |
| JPEG: raw (planar YCbCr) input and output                  |  ✓  |       ✓       |    ✓    |     ✗     |
| JPEG: saved markers                                        |  ✓  |       ✓       |    ✓    |     ✗     |
| JPEG: ICC profile read/write API                           |  ✓  |       ✓       |    ✗    |     ✗     |
| JPEG: color quantization and dithering                     |  ✓  |       ✓       |    ✓    |     ✗     |
| Integration: header-only, no build step                    |  ✓  |       ✗       |    ✗    |     ✓     |
| Integration: no assembler needed                           |  ✓  |       ✗⁵      |    ✓    |     ✓     |
| Integration: C++ API                                       |  ✓  |       ✗       |    ✗    |     ✗     |
| Integration: libjpeg C API                                 |  ✗  |       ✓       |    ✓    |     ✗     |
| Integration: TurboJPEG C API                               |  ✗  |       ✓       |    ✗    |     ✗     |
| Integration: Java binding                                  |  ✗  |       ✓       |    ✗    |     ✗     |

¹ GCC only. ² Little-endian only. ³ GCC ≥ 14 or Clang ≥ 17.
⁴ A separate build of the library (8- or 12-bit, not both).
⁵ NASM or Yasm for the x86 SIMD code.
⁶ SSE2 only (IDCT, color conversion, upsampling), detected at run time.
⁷ Decoding only; stb_image_write encodes baseline JPEG.

PNG features, compared with libpng 1.8 (the libpng18 branch, with zlib), the
reference PNG library, libspng 0.7.4, image-png 0.18.1 (Rust) and stb_image
2.30 with stb_image_write 1.16; ◐ marks partial support.  The tests run
libpng's test suite (same names, images and checks, see [Building and
testing](#building-and-testing)); read transformations and format conversions
give libpng's results bit for bit.

| Feature                                            |    vio     |  libpng | libspng | image-png | stb_image |
| -------------------------------------------------- | :--------: | :-----: | :-----: | :-------: | :-------: |
| Decoding: all color types and bit depths           |     ✓      |    ✓    |    ✓    |     ✓     |     ✓     |
| Decoding: Adam7 interlacing                        |     ✓      |    ✓    |    ✓    |     ✓     |     ✓     |
| Decoding: all standard chunks                      |     ✓      |    ✓    |    ◐    |     ◐     |     ✗     |
| Decoding: unknown-chunk handling                   |     ✓      |    ✓    |    ✓    |     ✗     |     ✗     |
| Decoding: CRC-32 and Adler-32 checks               |     ✓      |    ✓    |    ✓    |     ✓     |     ✗     |
| Decoding: configurable errors and limits           |     ✓      |    ✓    |    ✓    |     ✓     |     ✗     |
| Decoding: progressive (push) input                 |     ✓      |    ✓    |    ✗⁴   |     ✓     |     ✗     |
| Decoding: read transformations                     |     ✓      |    ✓    |    ◐    |     ◐     |     ◐     |
| Encoding: all color types, bit depths, interlacing |     ✓      |    ✓    |    ✓    |     ◐     |     ◐     |
| Encoding: all standard and custom chunks           |     ✓      |    ✓    |    ◐    |     ◐     |     ✗     |
| Encoding: write transformations                    |     ✓      |    ✓    |    ✗    |     ✗     |     ✗     |
| Encoding: adaptive filters, zlib tuning            |     ✓      |    ✓    |    ✓    |     ◐     |     ◐     |
| Encoding: single-pass fast mode                    |     ✓      |    ✗    |    ✗    |     ✓     |     ✗     |
| APNG: reading and writing                          |     ✓      |    ✓    |    ✗    |     ✓     |     ✗     |
| Format conversion (simplified API)                 |     ✓      |    ✓    |    ◐    |     ✗     |     ◐     |
| SIMD: decoding filters                             |     ✓¹     |    ✓²   |    ✓⁵   |     ✗⁶    |     ✗     |
| SIMD: inflate, CRC-32, Adler-32                    |     ✓¹     |    ✗³   |    ✗³   |     ◐⁶    |     ✗     |
| SIMD: encoding                                     |     ✓¹     |    ✗    |    ✗    |     ✗     |     ✗     |
| Integration: header-only, no build step            |     ✓      |    ✗    |    ✗    |     ✗     |     ✓     |
| Integration: no external zlib                      |     ✓      |    ✗    |    ✗    |     ✓     |     ✓     |
| Integration: C API                                 |     ✗      |    ✓    |    ✓    |     ✗     |     ✓     |

¹ x86 SSE2/SSSE3/SSE4.1/PCLMUL/AVX2, Arm NEON/CRC32/PMULL (AArch64; NEON
on AArch32), PowerPC AltiVec, Loongson MMI and RISC-V RVV, chosen at run time
and restricted by the same `JSIMD_*` variables as the JPEG codec.
² Unfiltering only: NEON, SSE2, MIPS MSA/MMI, PowerPC VSX, LoongArch LSX,
RISC-V RVV.
³ zlib's portable code (Arm CRC32 instructions only when compiled for them);
zlib-ng and Chromium's zlib add SIMD.
⁴ Rows are decoded one at a time, but input is pulled through a read callback.
⁵ Unfiltering only: SSE2 (SSSE3/SSE4.1 chosen at build time), AArch64 NEON.
⁶ CRC-32 and Adler-32 only (the crc32fast and simd-adler32 crates); filters
rely on compiler auto-vectorization.

## Performance

### JPEG

Throughput measured by `tjbench` on GitHub-hosted runners: vio, libjpeg-turbo
3.2 (built from the test submodule) and IJG libjpeg 10 (driven through a thin
TurboJPEG layer, `tests/libjpeg/`), all built on the same machine with the
same compiler (GCC on Linux, Apple Clang on macOS, MSVC on Windows; NASM for
libjpeg-turbo's x86 SIMD).  The image is a 3632×2384 RGB tiling of `testorig.ppm`; each number
is the best of 5 runs.  Results from commit `57212e5`.  Hosted Linux x86-64
runners come with different processors; the on-demand workflow
`bench-x64.yml` runs the benchmark on several at once, which measured the
three AMD EPYC generations and the Intel Xeon below (where several runs had
the same processor, each figure is the median over them).

How much faster vio is than libjpeg-turbo and than IJG libjpeg (which has no
SIMD code): +20% means 1.2 times the throughput.  Geometric mean over the
configurations below except lossless, which libjpeg does not support.

JPEG compression:

| Platform       | CPU                      | SIMD   | vs libjpeg-turbo | vs libjpeg |
| -------------- | ------------------------ | ------ | ---------------: | ---------: |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | AVX2   |             +39% |      +554% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | SSE2   |             +32% |      +480% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | scalar |             +51% |      +113% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | AVX2   |             +44% |      +614% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | SSE2   |             +38% |      +499% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | scalar |             +55% |      +128% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | AVX2   |             +45% |      +663% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | SSE2   |             +39% |      +547% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | scalar |             +71% |      +157% |
| Linux x86-64   | Intel Xeon 8573C         | AVX2   |             +44% |      +570% |
| Linux x86-64   | Intel Xeon 8573C         | SSE2   |             +34% |      +463% |
| Linux x86-64   | Intel Xeon 8573C         | scalar |             +64% |      +128% |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   |             +34% |      +500% |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |             +21% |      +388% |
| macOS x86-64   | Intel Core i7-8700B      | scalar |             +70% |      +153% |
| Linux AArch64  | Neoverse-N2              | NEON   |             +33% |      +353% |
| Linux AArch64  | Neoverse-N2              | scalar |             +36% |      +179% |
| macOS AArch64  | Apple M4¹                | NEON   |             +61% |      +612% |
| macOS AArch64  | Apple M4¹                | scalar |             +61% |      +296% |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | AVX2   |             +37% |          — |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | SSE2   |             +32% |          — |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | scalar |             +46% |          — |

JPEG decompression:

| Platform       | CPU                      | SIMD   | vs libjpeg-turbo | vs libjpeg |
| -------------- | ------------------------ | ------ | ---------------: | ---------: |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | AVX2   |             +34% |      +187% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | SSE2   |             +35% |      +172% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | scalar |             +20% |       +45% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | AVX2   |             +45% |      +210% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | SSE2   |             +42% |      +178% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | scalar |             +22% |       +48% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | AVX2   |             +44% |      +196% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | SSE2   |             +45% |      +173% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | scalar |             +27% |       +60% |
| Linux x86-64   | Intel Xeon 8573C         | AVX2   |             +47% |      +163% |
| Linux x86-64   | Intel Xeon 8573C         | SSE2   |             +42% |      +135% |
| Linux x86-64   | Intel Xeon 8573C         | scalar |             +27% |       +55% |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   |             +30% |      +162% |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |             +20% |      +123% |
| macOS x86-64   | Intel Core i7-8700B      | scalar |             +21% |       +57% |
| Linux AArch64  | Neoverse-N2              | NEON   |             +36% |      +114% |
| Linux AArch64  | Neoverse-N2              | scalar |             +27% |       +35% |
| macOS AArch64  | Apple M4¹                | NEON   |             +49% |      +142% |
| macOS AArch64  | Apple M4¹                | scalar |             +47% |       +63% |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | AVX2   |             +29% |          — |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | SSE2   |             +28% |          — |
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
| EPYC 7763 AVX2       |  +34% |  +30% |  +28% |  +39% |    +38% |  +74% |   +32% |     +29% |
| EPYC 7763 SSE2       |  +29% |  +25% |  +22% |  +28% |    +23% |  +71% |   +32% |     +30% |
| EPYC 7763 scalar     |  +67% |  +57% |  +49% |  +67% |    +45% |  +34% |   +40% |     +29% |
| EPYC 9V74 AVX2       |  +36% |  +33% |  +32% |  +47% |    +42% |  +75% |   +50% |     +30% |
| EPYC 9V74 SSE2       |  +36% |  +29% |  +25% |  +31% |    +27% |  +72% |   +49% |     +30% |
| EPYC 9V74 scalar     |  +74% |  +61% |  +53% |  +66% |    +49% |  +30% |   +54% |     +30% |
| EPYC 9V45 AVX2       |  +34% |  +31% |  +29% |  +48% |    +30% |  +96% |   +57% |     +33% |
| EPYC 9V45 SSE2       |  +31% |  +27% |  +25% |  +33% |    +24% |  +91% |   +55% |     +31% |
| EPYC 9V45 scalar     |  +99% |  +78% |  +69% |  +94% |    +67% |  +34% |   +63% |     +34% |
| Xeon 8573C AVX2      |  +36% |  +35% |  +34% |  +50% |    +31% |  +86% |   +46% |     +86% |
| Xeon 8573C SSE2      |  +24% |  +24% |  +23% |  +29% |    +25% |  +81% |   +44% |     +88% |
| Xeon 8573C scalar    |  +86% |  +72% |  +64% |  +81% |    +61% |  +35% |   +53% |     +86% |
| i7-8700B AVX2        |  +18% |  +33% |  +27% |  +37% |    +33% |  +78% |   +18% |     +28% |
| i7-8700B SSE2        |  +15% |  +14% |  +11% |   +9% |    +12% |  +76% |   +18% |     +31% |
| i7-8700B scalar      | +105% |  +93% |  +81% |  +91% |    +63% |  +35% |   +36% |     +34% |
| Neoverse-N2 NEON     |  +22% |  +21% |  +19% |  +29% |    +21% |  +85% |   +45% |     +75% |
| Neoverse-N2 scalar   |  +28% |  +30% |  +32% |  +31% |    +34% |  +53% |   +49% |     +76% |
| M4¹ NEON             |  +47% |  +43% |  +40% |  +65% |    +34% | +148% |   +76% |     +54% |
| M4¹ scalar           |  +55% |  +58% |  +60% |  +71% |    +54% |  +50% |   +79% |     +54% |
| EPYC 7763² AVX2      |  +27% |  +24% |  +22% |  +29% |    +28% |  +73% |   +68% |     +51% |
| EPYC 7763² SSE2      |  +18% |  +19% |  +17% |  +24% |    +18% |  +70% |   +67% |     +52% |
| EPYC 7763² scalar    |  +54% |  +45% |  +40% |  +48% |    +43% |  +30% |   +68% |     +52% |

JPEG decompression, vio vs libjpeg-turbo:

| CPU, SIMD            | 4:4:4 | 4:2:2 | 4:2:0 |  gray | fastDCT | prog. | arith. | lossless |
| -------------------- | ----: | ----: | ----: | ----: | ------: | ----: | -----: | -------: |
| EPYC 7763 AVX2       |  +26% |  +28% |  +28% |  +36% |    +35% |  +51% |   +32% |     +31% |
| EPYC 7763 SSE2       |  +32% |  +32% |  +30% |  +42% |    +30% |  +49% |   +32% |     +31% |
| EPYC 7763 scalar     |  +11% |  +10% |  +15% |  +25% |    +19% |  +28% |   +29% |     +32% |
| EPYC 9V74 AVX2       |  +42% |  +39% |  +39% |  +52% |    +42% |  +61% |   +44% |     +23% |
| EPYC 9V74 SSE2       |  +43% |  +36% |  +33% |  +50% |    +34% |  +53% |   +44% |     +23% |
| EPYC 9V74 scalar     |  +14% |  +11% |  +16% |  +29% |    +21% |  +30% |   +39% |     +22% |
| EPYC 9V45 AVX2       |  +40% |  +37% |  +36% |  +39% |    +38% |  +76% |   +47% |     +38% |
| EPYC 9V45 SSE2       |  +49% |  +42% |  +36% |  +44% |    +34% |  +72% |   +43% |     +37% |
| EPYC 9V45 scalar     |  +18% |  +18% |  +19% |  +28% |    +23% |  +43% |   +42% |     +39% |
| Xeon 8573C AVX2      |  +39% |  +42% |  +40% |  +49% |    +43% |  +75% |   +43% |     +37% |
| Xeon 8573C SSE2      |  +34% |  +37% |  +34% |  +45% |    +34% |  +68% |   +42% |     +38% |
| Xeon 8573C scalar    |  +17% |  +17% |  +18% |  +33% |    +24% |  +43% |   +39% |     +37% |
| i7-8700B AVX2        |  +11% |  +24% |  +17% |  +44% |    +28% |  +48% |   +47% |      +6% |
| i7-8700B SSE2        |   +9% |   +6% |  +12% |  +33% |    +10% |  +33% |   +40% |     +12% |
| i7-8700B scalar      |  +14% |  +17% |  +12% |  +22% |    +24% |  +23% |   +36% |      +9% |
| Neoverse-N2 NEON     |  +30% |  +30% |  +33% |  +38% |    +36% |  +48% |   +39% |     +48% |
| Neoverse-N2 scalar   |  +29% |  +18% |  +21% |  +33% |    +29% |  +28% |   +35% |     +48% |
| M4¹ NEON             |  +45% |  +39% |  +40% |  +52% |    +45% |  +73% |   +47% |     +78% |
| M4¹ scalar           |  +40% |  +40% |  +50% |  +36% |    +58% |  +58% |   +48% |     +78% |
| EPYC 7763² AVX2      |  +23% |  +25% |  +24% |  +28% |    +25% |  +39% |   +45% |     +41% |
| EPYC 7763² SSE2      |  +22% |  +23% |  +21% |  +30% |    +19% |  +39% |   +44% |     +42% |
| EPYC 7763² scalar    |  +10% |  +16% |  +18% |  +16% |    +24% |  +29% |   +40% |     +42% |

With SIMD, baseline compression is 17–65% faster than libjpeg-turbo and
sequential decompression 19–52% (9–37% and 6–44% on the macOS x86-64
runner, whose timings vary the most); without SIMD, compression is 40–105%
faster on x86 and 28–71% on AArch64, and sequential decompression 10–33% on
x86 and 18–58% on AArch64.  Progressive compression is 30–148% faster,
arithmetic-coded 18–79% and lossless 28–88%; progressive decompression is
23–76% faster, arithmetic-coded 29–48% and lossless 6–78% (22–78% except on
the macOS x86-64 runner).  Shared CI
runners are noisy (the macOS x86-64 runner most of all), so differences of a
few percent are within run-to-run variation.

### JPEG optimization log

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
- Sequential Huffman decoding fast path compiled a second time for BMI2 (shlx/shrx in place of shifts by cl) and chosen at run time ([680f0d8](https://github.com/ufoym/vio/commit/680f0d8)): decompression +3–6% (Intel Xeon, AVX2).
- 128-bit accurate IDCT kept in named vectors instead of an array MSVC left in memory ([1f83f2d](https://github.com/ufoym/vio/commit/1f83f2d)): SSE2 decompression +2% (MSVC).

### PNG

Single-threaded throughput on the 2848 images of the
[QOI benchmark corpus](https://qoiformat.org/benchmark/) (icons, photos,
textures, screenshots of games and web pages), the corpus image-png's own
benchmark uses.  vio is compared at each SIMD level with the same libraries
on every platform, one column each: libpng 1.6.59 with zlib 1.3.2, the most
widely used combination, and the fastest decoder and encoders measured.
Each figure is the geometric mean over the images of the speed-up in
megapixels per second, each image timed as the fastest of 3 runs.

- vio's SIMD level is set with the `JSIMD_*` variables; libpng (whose SIMD
  code only unfilters rows when decoding; zlib has none) is built without
  it for the scalar rows.
- The fastest decoder measured is image-png 0.18.1, ahead of Wuffs 0.4,
  zune-png 0.5 and libspng or libpng with zlib-ng 2.3.  It has no build
  without SIMD (the CRC-32 and Adler-32 crates it uses choose SIMD code at
  run time), so its default build appears in every row.
- Decoding produces the image's own sample layout (`png::Decoder`; image-png
  with `Transformations::IDENTITY`, built by `cargo --release`; libpng
  without transformations).  Checksums are verified, except Adler-32 by
  image-png, which skips it by default.
- Encoding starts from 8-bit RGB or RGBA.  Against libpng, vio runs in its
  zlib mode (`Encoder::Mode::kZlib`), whose files are byte for byte libpng's
  at the same zlib level, so the figures are speed alone: levels 1 and 6 (6
  being libpng's default; adaptive filters in both).  vio's default fast
  mode (`png::Encoder`) is a different, single-pass algorithm, compared with
  the two fastest encoders at about the same file size: fpnge (x86 only; its
  AVX2 build in the AVX2 rows, its SSE4.1 build in the SSE2 rows) and
  image-png's fast mode.  vio's files are 4–5% smaller than fpnge's and 4%
  smaller than image-png's.  Both modes are lossless: they change file size
  and speed, never the pixels.

All libraries are built from source with the same compiler (image-png with
rustc); the benchmark is `tests/png-bench/` (`png_bench.py run`, then
`report` or `compare`; `.github/workflows/bench-png.yml` runs it on the
GitHub-hosted runners, one job per platform and SIMD level).

Compilers: GCC on Linux, MSVC on Windows, Apple Clang on macOS; the CPU
gives the architecture (x86-64 or AArch64).

PNG decoding:

| Platform, CPU       | SIMD   | vs libpng | vs image-png |
| ------------------- | ------ | --------: | -----------: |
| Linux, EPYC 7763    | AVX2   |     +172% |         +23% |
| Linux, EPYC 7763    | SSE2   |     +146% |         +11% |
| Linux, EPYC 7763    | scalar |     +101% |         −19% |
| Windows, EPYC 9V45  | AVX2   |     +170% |         +17% |
| Windows, EPYC 9V45  | SSE2   |     +151% |          +7% |
| Windows, EPYC 7763  | scalar |     +109% |         −23% |
| macOS, i7-8700B     | AVX2   |     +254% |          +6% |
| macOS, i7-8700B     | SSE2   |     +228% |          −8% |
| macOS, i7-8700B     | scalar |     +160% |         −21% |
| Linux, Neoverse-N2  | NEON   |     +138% |         +11% |
| Linux, Neoverse-N2  | scalar |     +102% |         −10% |
| macOS, M1 (virtual) | NEON   |     +174% |         +22% |
| macOS, M1 (virtual) | scalar |     +188% |          −7% |

PNG encoding: vio's zlib mode vs libpng at levels 1 and 6 (L1, L6); vio's
fast mode vs fpnge and vs image-png's fast mode.

| Platform, CPU       | SIMD   | vs libpng L1 | vs libpng L6 | vs fpnge | vs image-png |
| ------------------- | ------ | -----------: | -----------: | -------: | -----------: |
| Linux, EPYC 7763    | AVX2   |        +119% |         +48% |     +29% |         +77% |
| Linux, EPYC 7763    | SSE2   |        +108% |         +44% |     +27% |         +20% |
| Linux, EPYC 7763    | scalar |         +94% |         +43% |        — |          −9% |
| Windows, EPYC 9V45  | AVX2   |        +124% |         +51% |    +115% |         +69% |
| Windows, EPYC 9V45  | SSE2   |        +118% |         +55% |     +85% |         +11% |
| Windows, EPYC 7763  | scalar |         +12% |         +14% |        — |         −80% |
| macOS, i7-8700B     | AVX2   |        +151% |         +56% |     +26% |        +115% |
| macOS, i7-8700B     | SSE2   |        +123% |         +52% |     +32% |          −6% |
| macOS, i7-8700B     | scalar |         +38% |         +51% |        — |         −51% |
| Linux, Neoverse-N2  | NEON   |        +108% |         +34% |        — |         +83% |
| Linux, Neoverse-N2  | scalar |         +88% |         +30% |        — |         +22% |
| macOS, M1 (virtual) | NEON   |        +118% |         +60% |        — |         +83% |
| macOS, M1 (virtual) | scalar |         +79% |         +46% |        — |         −15% |

In the scalar rows image-png keeps its SIMD (its checksum crates', and the
loops rustc vectorizes), so they compare vio without SIMD with image-png
with it; libpng is the like-for-like comparison there.  MSVC, unlike GCC and
Clang, does not vectorize vio's scalar loops, hence the Windows scalar row.
The fast mode's files are also about the size of libpng's at level 1 (1%
smaller), which it writes 11 to 18 times as fast with AVX2 or NEON.

By image category, number of images in parentheses (Linux x86-64, AMD EPYC
7763, GCC, AVX2); dec.: decoding, enc.: encoding.

| Category | dec. vs image-png | dec. vs libpng | enc. vs libpng¹ | enc. vs fpnge² |
| --- | ---: | ---: | ---: | ---: |
| icon_512 (213) | +29% | +289% | +93% | +14% (−16% size) |
| icon_64 (213) | +27% | +190% | +55% | +65% (−17% size) |
| photo_kodak (24) | +24% | +122% | +22% | +13% (−3% size) |
| photo_tecnick (100) | +17% | +160% | +30% | +11% (−2% size) |
| photo_wikipedia (49) | +25% | +132% | +29% | +16% (−3% size) |
| pngimg (187) | +24% | +224% | +58% | +12% (−4% size) |
| screenshot_game (618) | +29% | +198% | +70% | +12% (−6% size) |
| screenshot_web (14) | +25% | +150% | +80% | +32% (−11% size) |
| textures_photo (20) | +24% | +111% | +19% | +31% (−8% size) |
| textures_pk (1002) | +17% | +135% | +35% | +52% (−8% size) |
| textures_pk01 (113) | +23% | +178% | +29% | +12% (−3% size) |
| textures_pk02 (235) | +24% | +150% | +26% | +11% (−3% size) |
| textures_plants (60) | +21% | +201% | +45% | +16% (−3% size) |

¹ zlib mode, level 6. ² Fast mode.

Measured at [f61b56d](https://github.com/ufoym/vio/commit/f61b56d) on the
GitHub-hosted runners (one run; one job per platform and SIMD level, whose
CPUs vary from job to job).  fpnge has no Arm code; image-png's fast mode is
its fpnge-inspired encoder.

### PNG optimization log

Throughput gain of each change over the build before it (A/B on the same
machine, geometric mean over the corpus or the stated subset).

- Inflate into the image buffer with an 11-bit literal/length table whose entries hold two literals, three table lookups per bit-buffer refill and 16-byte match copies; rows reconstructed in place with NEON/SSE2 filters; CRC-32 by carry-less multiplication and SIMD Adler-32 ([4a01186](https://github.com/ufoym/vio/commit/4a01186)): decoding +170% (Apple M4, 1/8 of the corpus).
- Single-pass encoder: filters chosen by the smallest sum of absolute residuals, one Huffman code per image from sampled rows, runs of zero residuals as distance-1 matches, structured literal codes coded 32 at a time with AVX2 ([80db3a4](https://github.com/ufoym/vio/commit/80db3a4)): encoding +4100% over the zlib-level-6 encoder (Apple M4), files 5% larger.
- Literal-table lookups chained through the already shifted bit buffer (one shift, mask and load per lookup) ([ed781ce](https://github.com/ufoym/vio/commit/ed781ce)): decoding +5.5% (Apple M4, 1/8 of the corpus).
- Huffman statistics of small images from about 32 KiB of their rows; four literal codes per bit-buffer write in the scalar coder ([20ddc88](https://github.com/ufoym/vio/commit/20ddc88)): encoding +11% (Apple M4, 1/8 of the corpus), +24% on 256×256 textures.
- AVX2 filter selection and filtering, 32 bytes per step ([78b6799](https://github.com/ufoym/vio/commit/78b6799)): encoding +25% (Core Ultra 9 285K, clang-cl, 1/8 of the corpus).
- x86 Paeth unfiltering on 16-bit lanes (SSE4.1 variant at run time), SSE2 unfiltering of 16-bit images, image buffer reused across decodes ([e3125ea](https://github.com/ufoym/vio/commit/e3125ea)): decoding +12% (Core Ultra 9 285K, clang-cl and MSVC, 1/8 of the corpus).
- Unfiltering of 1-, 2-, 6- and 8-byte pixels: NEON for 16-bit images, register-carried scalar loops for gray and gray+alpha ([b7c46cb](https://github.com/ufoym/vio/commit/b7c46cb)): decoding of the corpus's gray, gray+alpha and 16-bit images +26% (Apple M4).
- One inflater (decode tables) per thread instead of one per image ([3b275b6](https://github.com/ufoym/vio/commit/3b275b6)): decoding of 64×64 icons +35% (Core Ultra 9 285K, clang-cl).
- De-interlacing with fixed-size pixel moves, no zero fill for whole-byte pixels ([65386c4](https://github.com/ufoym/vio/commit/65386c4)): decoding of interlaced images +27% (Apple M4; the corpus's 8 and PngSuite's interlaced images).
- 12-bit literal/length table, literal pairs written before the table is doubled ([8309220](https://github.com/ufoym/vio/commit/8309220)): decoding +2% (Apple M4), +2–3% (Core Ultra 9 285K, clang-cl and MSVC), 1/8 of the corpus.
- Encoder set-up for small images: canonical codes over the reduced alphabet, statistics from 8 KiB of data ([cd4f332](https://github.com/ufoym/vio/commit/cd4f332)): encoding of 64×64 icons +20% (Apple M4; sizes +0.1%).
- Match copies in inflate's tail loop with memset/memcpy ([aef2df7](https://github.com/ufoym/vio/commit/aef2df7)): decoding of 64×64 icons +7% (Apple M4).
- zlib mode: rows filtered with the fast mode's SIMD filter selection, files unchanged ([fb6801e](https://github.com/ufoym/vio/commit/fb6801e)): zlib-mode encoding +68% at level 6, +260% at level 1 (Apple M4, 1/8 of the corpus).
- zlib mode: matches compared 8 bytes at a time, codes written 32 bits at a time, output still identical to zlib's ([df87ff9](https://github.com/ufoym/vio/commit/df87ff9)): zlib-mode encoding +23% at level 6, +18% at level 1 (Apple M4, 1/8 of the corpus).
- zlib mode: block symbols in their own 16-bit buffer instead of bytes overlaid on the output buffer, which the compiler had to assume aliased the deflater's state ([f3cfc60](https://github.com/ufoym/vio/commit/f3cfc60)): zlib-mode encoding +7% at level 1, +2% at level 6 (Apple M4, 1/8 of the corpus).
- Scalar paths: 3- to 8-byte pixels unfiltered with the previous pixel in registers, Adler-32 and filter selection in loops compilers vectorize (8-bit Paeth arithmetic), braided CRC-32 ([1a7f107](https://github.com/ufoym/vio/commit/1a7f107)): without SIMD, decoding +43% and encoding +75% (Apple M4), +36% and +116% (Cortex-X925, GCC).
- Int arithmetic in MSVC's scalar filter selection, which MSVC does not vectorize ([a6fc885](https://github.com/ufoym/vio/commit/a6fc885)): encoding without SIMD +14% over the version before the scalar rewrite (Core Ultra 9 285K, MSVC).
- Generic Adler-32 kernel with SSE2 on x86-64 without SSSE3 ([bd4da24](https://github.com/ufoym/vio/commit/bd4da24)): Adler-32 46.5 GB/s instead of 5.7 (Core Ultra 9 285K, MSVC, JSIMD_FORCESSE2).
- Paeth unfiltering with the ranges of a that predict b or c precomputed 16 bytes at a time, leaving three comparisons and two selections per pixel on the dependency chain ([f582411](https://github.com/ufoym/vio/commit/f582411)): decoding +15% (Apple M4), +9% (Cortex-X925, GCC), +3% with SSE4.1 and +5% with SSE2 (Core Ultra 9 285K, MSVC), 1/8 of the corpus.
- Scalar Adler-32 in forms compilers vectorize without a horizontal sum per block, matches at distances 2 to 7 copied as a 16-byte pattern without NEON ([f35cffb](https://github.com/ufoym/vio/commit/f35cffb)): decoding without SIMD +10% (Apple M4), +5% (Cortex-X925, GCC), +8% (Core Ultra 9 285K, MSVC), 1/8 of the corpus.
- Scalar filter selection on byte-typed residuals, which Clang vectorizes on 8-bit lanes instead of 16- and 32-bit ones ([4978392](https://github.com/ufoym/vio/commit/4978392)): encoding without SIMD +65% (Apple M4), +50% (Core Ultra 9 285K, clang-cl), 1/8 of the corpus; files unchanged.
- Scalar filter selection and filtering without ?: and with operands through distinct restrict pointers, the form MSVC vectorizes ([77f90b0](https://github.com/ufoym/vio/commit/77f90b0)): encoding without SIMD +140% (Core Ultra 9 285K, MSVC, 1/8 of the corpus); files unchanged.

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

The PNG tests (`tests/libpng.cmake`) mirror libpng's test suite: pngtest,
pnggetset, pngimage, pngstest, pngunknown and pngvalid, under the same names,
on libpng's images and on generated ones.  Their reference values,
`tests/libpng-expected.txt`, are what libpng produces for the same cases;
to regenerate them after changing the cases (`tests/tools/png_cases.hpp`),
build `png-golden` against the libpng submodule (needs zlib):

```sh
cmake --preset native -DVIO_PNG_GOLDEN=ON
cmake --build build/native --target png-golden
build/native/png-golden tests/libpng > tests/libpng-expected.txt
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
├── png.hpp                           includes the png/ codec; Reader, Writer
├── png/
│   ├── decoder.hpp encoder.hpp       whole-image codec
│   ├── info.hpp common.hpp           chunk contents, shared types
│   ├── transform.hpp                 read and write transformations
│   ├── stream.hpp                    progressive (push) decoding
│   ├── simple.hpp                    pixel format conversion reader/writer
│   ├── fast_encoder.hpp              single-pass encoder (SIMD filtering
│   │                                 and Huffman coding)
│   ├── inflate.hpp                   DEFLATE decompression (whole buffer,
│   │                                 or resumable)
│   ├── deflate.hpp                   DEFLATE compression, zlib-identical
│   │                                 output
│   ├── filter.hpp                    scanline filters (scalar, NEON, SSE2,
│   │                                 generic vector kernels)
│   ├── checksum.hpp                  CRC-32, Adler-32 (PCLMUL/PMULL folding,
│   │                                 Arm CRC32, SSSE3/NEON/generic sums)
│   └── simd.hpp                      run-time kernel selection; the JPEG
│                                     vector interface `V` for AltiVec, MMI
│                                     and RVV
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
                                      jpeg-transform, jpeg-example,
                                      png-test, png-golden);
                                      png-bench/: PNG benchmark (vio, and
                                      image-png and fpnge for comparison)
```

## License

MIT; `LICENSE` also contains the third-party notices that apply.
