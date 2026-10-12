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

Throughput measured by `tjbench` on GitHub-hosted runners: vio, libjpeg-turbo
3.2 (built from the test submodule) and IJG libjpeg 10 (driven through a thin
TurboJPEG layer, `tests/libjpeg/`), all built on the same machine with the
same compiler (GCC on Linux, Apple Clang on macOS; NASM for libjpeg-turbo's
x86 SIMD).  The image is a 3632×2384 RGB tiling of `testorig.ppm`; each number
is the best of 5 runs.  Results from commit `aeef3f9`.

Speed relative to libjpeg (1.00× = libjpeg; libjpeg has no SIMD code),
geometric mean over the configurations below except lossless, which libjpeg
does not support.  In parentheses: vio relative to libjpeg-turbo over the
same configurations (100% = same speed).

| Platform      | CPU                 | SIMD   | Compress vio | libjpeg-turbo | Decompress vio | libjpeg-turbo |
| ------------- | ------------------- | ------ | -----------: | ------------: | -------------: | ------------: |
| Linux x86-64  | AMD EPYC 7763       | AVX2   | 4.46× (94%)  |         4.73× |   2.08× (97%)  |         2.15× |
| Linux x86-64  | AMD EPYC 7763       | SSE2   | 3.96× (90%)  |         4.39× |   1.98× (99%)  |         2.00× |
| Linux x86-64  | AMD EPYC 7763       | scalar | 1.40× (99%)  |         1.41× |   1.18× (98%)  |         1.21× |
| macOS x86-64  | Intel Core i7-8700B | AVX2   | 4.31× (94%)  |         4.59× |   1.92× (95%)  |         2.02× |
| macOS x86-64  | Intel Core i7-8700B | SSE2   | 3.72× (88%)  |         4.23× |   1.90× (97%)  |         1.96× |
| macOS x86-64  | Intel Core i7-8700B | scalar | 1.83× (128%) |         1.43× |   1.18× (96%)  |         1.22× |
| Linux AArch64 | Neoverse-N2         | NEON   | 3.36× (99%)  |         3.40× |   1.57× (100%) |         1.57× |
| Linux AArch64 | Neoverse-N2         | scalar | 1.90× (93%)  |         2.05× |   1.07× (102%) |         1.06× |
| macOS AArch64 | Apple M1 (virtual)  | NEON   | 4.20× (95%)  |         4.42× |   1.71× (99%)  |         1.73× |
| macOS AArch64 | Apple M1 (virtual)  | scalar | 2.31× (100%) |         2.31× |   1.04× (93%)  |         1.11× |

Per configuration, Mpixels/s, vio / libjpeg-turbo / libjpeg:

| Configuration     | x86-64 AVX2 compress | decompress      | AArch64 NEON compress | decompress      |
| ----------------- | -------------------: | --------------: | --------------------: | --------------: |
| 4:4:4 q95         |      161 / 173 / 26  |  155 / 164 / 68 |       138 / 142 / 35  |  149 / 150 / 94 |
| 4:2:2 q95         |      217 / 236 / 33  |  193 / 201 / 76 |       186 / 188 / 44  | 179 / 179 / 108 |
| 4:2:0 q95         |      267 / 290 / 38  |  221 / 235 / 79 |       229 / 232 / 49  | 210 / 208 / 105 |
| gray q95          |      352 / 384 / 60  | 264 / 281 / 143 |       342 / 344 / 81  | 301 / 303 / 235 |
| 4:2:0 fast DCT    |      281 / 293 / 40  |  232 / 243 / 80 |       249 / 255 / 52  | 233 / 233 / 108 |
| 4:2:0 progressive |        45 / 45 / 20  |    83 / 85 / 53 |         45 / 45 / 20  |    97 / 98 / 70 |
| 4:2:0 arithmetic  |        24 / 25 / 18  |    25 / 23 / 20 |         33 / 34 / 24  |    31 / 31 / 27 |
| lossless PSV1     |        32 / 21 / —   |    50 / 52 / —  |         37 / 24 / —   |    53 / 57 / —  |

The x86-64 column is the EPYC runner, the AArch64 column the Neoverse-N2
runner.  vio matches libjpeg-turbo on AArch64 and in decompression, and
compresses lossless JPEG about 1.5× faster.  Baseline compression on x86 is
still behind: 5–10% with AVX2, 10–20% with SSE2.  Shared CI runners are noisy
(the macOS x86-64 runner most of all), so differences of a few percent are
within run-to-run variation.

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
