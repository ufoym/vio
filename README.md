# vio

Header-only C++20 image I/O.  The JPEG codec supports sequential,
progressive, arithmetic-coded and lossless JPEG with 8- to 16-bit samples,
lossless transforms, and SIMD acceleration for x86 (MMX through AVX2), Arm
NEON, PowerPC AltiVec, Loongson MMI and RISC-V Vector.  Its speed is on par
with libjpeg-turbo (see [Performance](#performance)).

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

## Platform support

Compared with libjpeg-turbo 3.2 and IJG libjpeg 10.  All three run on any
architecture through portable code; libjpeg, written in plain ANSI C, builds
with the widest range of compilers and systems, while vio needs a C++20
compiler.

SIMD acceleration:

| Architecture  | SIMD                      | vio | libjpeg-turbo | libjpeg |
| ------------- | ------------------------- | :-: | :-----------: | :-----: |
| x86-64        | SSE2, AVX2                |  ✓  |       ✓       |    ✗    |
| i386          | MMX, SSE, SSE2, AVX2      |  ✓  |       ✓       |    ✗    |
| AArch64       | NEON                      |  ✓  |       ✓       |    ✗    |
| AArch32       | NEON                      |  ✓  |       ✓       |    ✗    |
| AArch32       | NEON detected at run time |  ✓¹ |       ✓       |    ✗    |
| PowerPC       | AltiVec                   |  ✓  |       ✓       |    ✗    |
| MIPS64        | Loongson MMI              |  ✓² |       ✓       |    ✗    |
| RISC-V 64     | RVV 1.0                   |  ✓³ |       ✓       |    ✗    |

Toolchains:

| OS / compiler                  | vio | libjpeg-turbo | libjpeg |
| ------------------------------ | :-: | :-----------: | :-----: |
| Linux, GCC                     |  ✓  |       ✓       |    ✓    |
| Linux, Clang                   |  ✓  |       ✓       |    ✓    |
| macOS, Apple Clang             |  ✓  |       ✓       |    ✓    |
| Windows, MSVC                  |  ✗⁴ |       ✓       |    ✓    |
| Compilers without C++20        |  ✗  |       ✓       |    ✓    |

¹ GCC only. ² Little-endian only. ³ GCC ≥ 14 or Clang ≥ 17.
⁴ Code paths exist but are not yet tested.

## Features

| JPEG                                                 | vio | libjpeg-turbo | libjpeg |
| ---------------------------------------------------- | :-: | :-----------: | :-----: |
| Baseline and extended sequential                     |  ✓  |       ✓       |    ✓    |
| Progressive                                          |  ✓  |       ✓       |    ✓    |
| Arithmetic coding                                    |  ✓  |       ✓       |    ✓    |
| Lossless (2- to 16-bit)                              |  ✓  |       ✓       |    ✗    |
| 12-bit lossy                                         |  ✓  |       ✓       |    ✓⁵   |
| Lossless transforms (rotate, flip, crop, wipe, drop) |  ✓  |       ✓       |    ✓    |
| Lossless roll transform                              |  ✓  |       ✓       |    ✗    |
| Lossless negate transform                            |  ✗  |       ✗       |    ✓    |
| SmartScale (non-standard DCT block sizes)            |  ✗  |       ✗       |    ✓    |
| Scaled decoding                                      |  ✓  |       ✓       |    ✓    |
| Partial decoding (crop, skip rows)                   |  ✓  |       ✓       |    ✗    |
| Raw (planar YCbCr) input and output                  |  ✓  |       ✓       |    ✓    |
| Saved markers                                        |  ✓  |       ✓       |    ✓    |
| ICC profile read/write API                           |  ✓  |       ✓       |    ✗    |
| Color quantization and dithering                     |  ✓  |       ✓       |    ✓    |
| Run-time SIMD selection via `JSIMD_*` variables      |  ✓  |       ✓       |    ✗    |

| Integration                    | vio | libjpeg-turbo | libjpeg |
| ------------------------------ | :-: | :-----------: | :-----: |
| Header-only, no build step     |  ✓  |       ✗       |    ✗    |
| No assembler needed            |  ✓  |      ✗⁶       |    ✓    |
| C++ API                        |  ✓  |       ✗       |    ✗    |
| libjpeg C API                  |  ✗  |       ✓       |    ✓    |
| TurboJPEG C API                |  ✗  |       ✓       |    ✗    |
| Java binding                   |  ✗  |       ✓       |    ✗    |

⁵ A separate build of the library (8- or 12-bit, not both).
⁶ NASM or Yasm for the x86 SIMD code.

## Performance

Throughput in Mpixels/s measured by `tjbench` on GitHub-hosted runners, vio
against libjpeg-turbo 3.2 built from the test submodule on the same machine
(GCC on Linux, Apple Clang on macOS; NASM for libjpeg-turbo's x86 SIMD).  The
image is a 3632×2384 RGB tiling of `testorig.ppm`; each number is the best of
5 runs.  Results from commit `dd10d15`.

Relative speed, vio / libjpeg-turbo, geometric mean over the 8 configurations
below (100% = same speed):

| Platform      | CPU                 | SIMD   | Compress | Decompress |
| ------------- | ------------------- | ------ | -------: | ---------: |
| Linux x86-64  | AMD EPYC 7763       | AVX2   |      99% |        97% |
| Linux x86-64  | AMD EPYC 7763       | SSE2   |      96% |        99% |
| Linux x86-64  | AMD EPYC 7763       | scalar |     105% |        98% |
| macOS x86-64  | Intel Core i7-8700B | AVX2   |      97% |        93% |
| macOS x86-64  | Intel Core i7-8700B | SSE2   |      88% |        97% |
| macOS x86-64  | Intel Core i7-8700B | scalar |     127% |        96% |
| Linux AArch64 | Neoverse-N2         | NEON   |     105% |        99% |
| Linux AArch64 | Neoverse-N2         | scalar |     100% |       100% |
| macOS AArch64 | Apple M1 (virtual)  | NEON   |     101% |       100% |
| macOS AArch64 | Apple M1 (virtual)  | scalar |     104% |        96% |

Per configuration, Mpixels/s, vio / libjpeg-turbo:

| Configuration     | x86-64 AVX2 compress | decompress | AArch64 NEON compress | decompress |
| ----------------- | -------------------: | ---------: | --------------------: | ---------: |
| 4:4:4 q95         |            161 / 173 |  155 / 164 |             138 / 142 |  149 / 150 |
| 4:2:2 q95         |            216 / 236 |  192 / 202 |             185 / 188 |  179 / 179 |
| 4:2:0 q95         |            266 / 290 |  221 / 235 |             228 / 232 |  210 / 208 |
| gray q95          |            351 / 384 |  263 / 281 |             345 / 342 |  303 / 304 |
| 4:2:0 fast DCT    |            279 / 293 |  231 / 242 |             249 / 255 |  233 / 235 |
| 4:2:0 progressive |              44 / 45 |    81 / 84 |               46 / 46 |    98 / 99 |
| 4:2:0 arithmetic  |              24 / 25 |    25 / 23 |               33 / 34 |    31 / 31 |
| lossless PSV1     |              32 / 21 |    50 / 51 |               38 / 24 |    53 / 57 |

Lossless compression is about 1.5× faster on every platform, which lifts the
compression means; baseline Huffman compression on x86 is still 5–10% behind.
Shared CI runners are noisy (the macOS x86-64 runner most of all), so
differences of a few percent are within run-to-run variation.

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

To benchmark against libjpeg-turbo, build it from the submodule into a tree
of its own:

```sh
cmake -S tests/libjpeg-turbo -B build/libjpeg-turbo -DCMAKE_BUILD_TYPE=Release
cmake --build build/libjpeg-turbo -j 8   # tjbench
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
