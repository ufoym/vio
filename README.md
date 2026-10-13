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
| Windows, MSVC                  |  ✓  |       ✓       |    ✓    |
| Windows, clang-cl              |  ✓  |       ✓       |    ✓    |
| Compilers without C++20        |  ✗  |       ✓       |    ✓    |

¹ GCC only. ² Little-endian only. ³ GCC ≥ 14 or Clang ≥ 17.

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
same compiler (GCC on Linux, Apple Clang on macOS, MSVC on Windows; NASM for
libjpeg-turbo's x86 SIMD).  The image is a 3632×2384 RGB tiling of `testorig.ppm`; each number
is the best of 5 runs.  Results from commit `bb9833a`.

Speed relative to libjpeg (1.00× = libjpeg; libjpeg has no SIMD code),
geometric mean over the configurations below except lossless, which libjpeg
does not support.  In parentheses: vio relative to libjpeg-turbo over the
same configurations (100% = same speed).

| Platform       | CPU                      | SIMD   | Compress vio | libjpeg-turbo | Decompress vio | libjpeg-turbo |
| -------------- | ------------------------ | ------ | -----------: | ------------: | -------------: | ------------: |
| Linux x86-64   | AMD EPYC 9V45            | AVX2   | 5.78× (109%) |         5.31× |   2.49× (122%) |         2.04× |
| Linux x86-64   | AMD EPYC 9V45            | SSE2   | 5.36× (114%) |         4.70× |   2.34× (123%) |         1.90× |
| Linux x86-64   | AMD EPYC 9V45            | scalar | 2.46× (162%) |         1.52× |   1.41× (111%) |         1.27× |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   | 4.96× (107%) |         4.62× |   2.15× (105%) |         2.04× |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |  4.39× (99%) |         4.41× |   2.14× (109%) |         1.96× |
| macOS x86-64   | Intel Core i7-8700B      | scalar | 2.44× (157%) |         1.55× |   1.36× (101%) |         1.34× |
| Linux AArch64  | Neoverse-N2              | NEON   | 3.77× (111%) |         3.39× |   1.91× (122%) |         1.57× |
| Linux AArch64  | Neoverse-N2              | scalar | 2.22× (109%) |         2.05× |   1.22× (115%) |         1.06× |
| macOS AArch64  | Apple M4¹                | NEON   | 5.11× (116%) |         4.41× |   2.02× (124%) |         1.63× |
| macOS AArch64  | Apple M4¹                | scalar | 3.00× (122%) |         2.45× |   1.25× (112%) |         1.11× |
| Windows x86-64 | Intel Core Ultra 9 285K² | AVX2   | 5.18× (109%) |         4.74× |   1.82× (113%) |         1.61× |
| Windows x86-64 | Intel Core Ultra 9 285K² | SSE2   | 4.69× (111%) |         4.23× |   1.68× (113%) |         1.48× |
| Windows x86-64 | Intel Core Ultra 9 285K² | scalar | 2.26× (159%) |         1.42× |   1.09× (105%) |         1.04× |

¹ Measured on a local Mac (native, same build and benchmark script)
instead of the GitHub-hosted runner, which is a virtualized Apple M1.
² Measured on a local Windows 11 machine with MSVC 19.44 (Visual Studio
2022), pinned to the performance cores.  Built with clang-cl 19 instead
(vio and libjpeg-turbo), vio is at 116% (AVX2), 111% (SSE2) and 181%
(scalar) of libjpeg-turbo's compression speed and at 122%, 123% and 113%
of its decompression speed.

Per configuration, Mpixels/s, vio / libjpeg-turbo / libjpeg:

| Configuration     | x86-64 AVX2 compress | decompress      | AArch64 NEON compress | decompress      |
| ----------------- | -------------------: | --------------: | --------------------: | --------------: |
| 4:4:4 q95         |      305 / 284 / 36  |  311 / 236 / 99  |       150 / 141 / 35  |  189 / 149 / 94 |
| 4:2:2 q95         |      382 / 375 / 44  | 358 / 283 / 116  |       201 / 188 / 44  | 227 / 179 / 108 |
| 4:2:0 q95         |      460 / 456 / 52  | 422 / 336 / 123  |       247 / 232 / 49  | 268 / 208 / 105 |
| gray q95          |      595 / 603 / 80  | 574 / 440 / 210  |       379 / 342 / 81  | 405 / 303 / 234 |
| 4:2:0 fast DCT    |      493 / 466 / 54  | 444 / 344 / 127  |       269 / 254 / 52  | 308 / 233 / 108 |
| 4:2:0 progressive |        83 / 66 / 29  |   130 / 119 / 81 |         53 / 45 / 20  |   106 / 98 / 70 |
| 4:2:0 arithmetic  |        41 / 33 / 24  |     32 / 31 / 28 |         42 / 34 / 24  |    32 / 31 / 27 |
| lossless PSV1     |        61 / 37 / —   |     89 / 79 / —  |         40 / 24 / —   |    61 / 57 / —  |

The x86-64 column is the EPYC runner, the AArch64 column the Neoverse-N2
runner.  Sequential decompression is 20–40% faster than libjpeg-turbo (from
Huffman decoding), progressive and arithmetic-coded decompression 3–10%,
baseline compression up to 10%, arithmetic-coded compression 20–30% and
lossless compression about 1.6×.  Shared CI runners are noisy (the macOS
x86-64 runner most of all), so differences of a few percent, such as the
SSE2 compression on that runner, are within run-to-run variation.

On the Windows machine (MSVC, AVX2), same format:

| Configuration     | Compress        | Decompress      |
| ----------------- | --------------: | --------------: |
| 4:4:4 q95         | 357 / 340 / 48  | 376 / 325 / 185 |
| 4:2:2 q95         | 471 / 445 / 61  | 464 / 405 / 211 |
| 4:2:0 q95         | 569 / 533 / 72  | 532 / 467 / 223 |
| gray q95          | 676 / 649 / 107 | 623 / 548 / 379 |
| 4:2:0 fast DCT    | 579 / 529 / 73  | 550 / 470 / 228 |
| 4:2:0 progressive |   83 / 78 / 32  | 139 / 125 / 100 |
| 4:2:0 arithmetic  |   52 / 40 / 30  |    41 / 40 / 37 |
| lossless PSV1     |   57 / 34 / —   |   104 / 95 / —  |

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
