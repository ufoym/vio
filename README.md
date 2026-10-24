# vio

Fast, complete, header-only C++20 image I/O: JPEG, PNG, BMP, GIF, PPM and
Targa, with SIMD on every major architecture.

- **Fast JPEG** (see [Performance](#performance)):
  - encoding 4.6–7.6× libjpeg, and 1.2–1.7× libjpeg-turbo;
  - decoding 2.2–3.1× libjpeg, and 1.2–1.5× libjpeg-turbo.
- **Fast PNG:**
  - decoding 2.2–3.6× libpng, and faster than image-png;
  - encoding 1.4–3.0× libpng with identical files, and with SIMD faster
    than fpnge and image-png.
- **Complete JPEG:** sequential, progressive, arithmetic-coded and lossless,
  8- to 16-bit samples, lossless transforms.
- **Portable SIMD:** x86 (MMX through AVX2), Arm NEON, PowerPC AltiVec,
  Loongson MMI and RISC-V Vector, chosen at run time.
- **Header-only:** add `include/` and include only the formats you need;
  PNG brings its own DEFLATE, with no zlib dependency.
- **C and Python:** a header-only C interface (`bind/c/vio.h`) and a
  NumPy-based Python package (`pip install .`) for whole images.
- **Drop-in C libraries:** `libjpeg` and `libturbojpeg` (`bind/jpeglib/`)
  implement the `jpeglib.h` and `turbojpeg.h` APIs on vio, source- and
  binary-compatible.

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

### C

`bind/c/vio.h` decodes and encodes whole images in one layout: rows of 1
(gray), 2 (gray + alpha), 3 (RGB) or 4 (RGBA) channels of 8- or 16-bit
samples.  It is header-only too; its implementation is C++20, so one C++
file of the program compiles it (with `include/` and `bind/c/` on the include
path, both of which the `vio::vio` CMake target provides):

```cpp
// vio.cpp
#define VIO_IMPLEMENTATION
#include "vio.h"
```

and the rest of the program, in C or C++, includes `vio.h` as usual:

```c
#include "vio.h"

vio_image img;
if (vio_load("in.png", 0, 0, &img) != VIO_OK) {  /* 0, 0: the file's layout */
  fprintf(stderr, "%s\n", vio_last_error());
  return 1;
}
vio_options opt = {0};
opt.quality = 85;                                /* JPEG; 0 = defaults */
vio_save("out.jpg", &img, &opt);                 /* format from the extension */
vio_image_free(&img);
```

| Function                                      | Does                                         |
| --------------------------------------------- | -------------------------------------------- |
| `vio_decode(data, size, channels, depth, &img)` | decodes a file in memory; channels 1..4 and depth 8/16 convert, 0 keeps the file's |
| `vio_load(path, channels, depth, &img)`       | the same from a file                         |
| `vio_encode(&img, format, &opt, &out, &size)` | encodes into a buffer freed with `vio_free()` |
| `vio_save(path, &img, &opt)`                  | writes a file, format from the extension     |
| `vio_probe(data, size, &info)`                | format, size and layout from the header      |
| `vio_detect(data, size)`, `vio_format_from_path(path)` | the format of a file or file name    |
| `vio_last_error()`                            | the last error message on this thread        |

JPEG, PNG, BMP, GIF (first image), PPM/PGM and Targa are decoded; all but
GIF are encoded.  Options: JPEG `quality` (default 90), `subsampling` (444,
422, 420), `progressive`; PNG `compression` (0: the fast single-pass
encoder, 1..9: zlib levels).  Formats that cannot hold alpha drop it;
JPEG, BMP and Targa store 8-bit samples.

### jpeglib.h and turbojpeg.h

`bind/jpeglib/` implements two established C interfaces on vio's codec, as
shared and static libraries:

- `libjpeg`: the `jpeglib.h` API, with its extensions for extended RGB pixel
  formats, RGB565 output, 12- and 16-bit samples (`jpeg12_*`, `jpeg16_*`),
  lossless JPEG, partial decoding (`jpeg_skip_scanlines`,
  `jpeg_crop_scanline`), ICC profiles and in-memory sources and
  destinations;
- `libturbojpeg`: the TurboJPEG API (`turbojpeg.h`, version 3.2, with the
  1.x and 2.x functions).

The headers have the standard structure layouts and message codes, and the
shared libraries the usual names, versions and versioned symbols
(`libjpeg.so.62`, `libturbojpeg.so.0`; on Windows `jpeg62.dll`), so existing
programs work unchanged: rebuilt against these headers, or as they are with
these libraries in place of others.  `VIO_JPEG_LIB_VERSION` selects the
`jpeglib.h` ABI level: 62 (the default), 70 or 80 (`libjpeg.so.7`,
`libjpeg.so.8`).  CMake targets: `vio::jpeg`, `vio::jpeg-static`,
`vio::turbojpeg`, `vio::turbojpeg-static`; `cmake --install` installs the
libraries and headers.

```c
#include <stdio.h>
#include "jpeglib.h"

struct jpeg_decompress_struct cinfo;
struct jpeg_error_mgr jerr;
cinfo.err = jpeg_std_error(&jerr);
jpeg_create_decompress(&cinfo);
jpeg_stdio_src(&cinfo, infile);
jpeg_read_header(&cinfo, TRUE);
cinfo.out_color_space = JCS_EXT_RGBA;
jpeg_start_decompress(&cinfo);
while (cinfo.output_scanline < cinfo.output_height)
  jpeg_read_scanlines(&cinfo, &row, 1);
jpeg_finish_decompress(&cinfo);
jpeg_destroy_decompress(&cinfo);
```

All of `jpeglib.h` is implemented, including buffered-image mode, suspending
data sources and destinations, marker processors, abbreviated datastreams,
raw (downsampled) data, coefficient access for transcoding, color
quantization, the memory manager's virtual arrays and the standard error
manager's message tables.  Differences from other implementations:

- The private processing modules (`cinfo->cconvert`, `cinfo->upsample`,
  ...) are opaque: applications cannot install modules of their own.
- Before decoding an image, its compressed data are read from the data source
  up to the end of the image (in place when the source's buffer already holds
  them, as with `jpeg_mem_src()`), so `jpeg_start_decompress()` consumes
  the whole image; `jpeg_read_header()` reads only the header.
- Virtual arrays are always kept in memory; `max_memory_to_use` (and the
  `JPEGMEM` environment variable) limits their total size.
- Not implemented, as in the 6.2 ABI: compression scaling and SmartScale
  (`scale_num`/`scale_denom` and `block_size` of the 7 and 8 ABIs are
  ignored), CCIR 601 sampling, and arithmetic-coded lossless JPEG.

### Python

```sh
pip install .        # builds the extension (needs a C++20 compiler) and installs numpy
```

```python
import vio

img = vio.read("photo.jpg")                  # numpy array, (h, w, 3) uint8
vio.write("photo.png", img)
data = vio.encode(img, "jpeg", quality=85, subsampling=444, progressive=True)
rgba = vio.decode(data, channels=4)          # (h, w, 4)
gray = vio.read("photo.png", channels=1, depth=16)   # (h, w) uint16
vio.info("photo.jpg")                        # Info(format='jpeg', width=227, ...)
```

Arrays are `(h, w)` for gray, else `(h, w, c)`, of `uint8` or `uint16`;
decoded arrays share the decoder's buffer (no copy), and decoding and
encoding release the GIL.  Errors raise `vio.Error`.  The package lives in
`bind/python/` (its build files, `pyproject.toml` and `setup.py`, at the top
so that `pip install` works on the repository).

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
| Integration: libjpeg C API                                 |  ✓  |       ✓       |    ✓    |     ✗     |
| Integration: TurboJPEG C API                               |  ✓  |       ✓       |    ✗    |     ✗     |
| Integration: Java binding                                  |  ✗  |       ✓       |    ✗    |     ✗     |
| Integration: simple whole-image C API                      |  ✓  |       ✓       |    ✗    |     ✓     |
| Integration: Python binding                                |  ✓  |       ✗       |    ✗    |     ✗     |

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
| Integration: C API                                 |     ✓      |    ✓    |    ✓    |     ✗     |     ✓     |
| Integration: Python binding                        |     ✓      |    ✗    |    ✗    |     ✗     |     ✗     |

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

Throughput measured by `jpeg-bench`: vio against libjpeg-turbo 3.2 (built from
the test submodule) and IJG libjpeg 10 (no SIMD; driven through a thin
TurboJPEG layer, `tests/jpeg/libjpeg/`), all built on the same machine with the
same compiler (GCC on Linux, Apple Clang on macOS, MSVC on Windows; NASM for
libjpeg-turbo's x86 SIMD).  The image is a 3632×2384 RGB tiling of
`testorig.ppm`, each number the best of 5 runs.  +20% means 1.2 times the
throughput; the summaries are geometric means over the configurations except
lossless, which libjpeg does not support.

The EPYC 7763 (Linux and Windows), i7-8700B, Neoverse-N2 and virtualized M1
rows are the GitHub-hosted runners in CI at
[a3184d7](https://github.com/ufoym/vio/commit/a3184d7).  The other x86-64
processors that hosted Linux runners come with were measured by the
on-demand `bench-jpeg.yml` at `57212e5` (median when a processor came up
more than once; the JPEG code has not changed since).

JPEG compression:

| Platform       | CPU                      | SIMD   | vs libjpeg-turbo | vs libjpeg |
| -------------- | ------------------------ | ------ | ---------------: | ---------: |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | AVX2   |             +39% |      +555% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | SSE2   |             +31% |      +478% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | scalar |             +50% |      +111% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | AVX2   |             +44% |      +614% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | SSE2   |             +38% |      +499% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | scalar |             +55% |      +128% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | AVX2   |             +45% |      +663% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | SSE2   |             +39% |      +547% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | scalar |             +71% |      +157% |
| Linux x86-64   | Intel Xeon 8573C         | AVX2   |             +44% |      +570% |
| Linux x86-64   | Intel Xeon 8573C         | SSE2   |             +34% |      +463% |
| Linux x86-64   | Intel Xeon 8573C         | scalar |             +64% |      +128% |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   |             +31% |      +501% |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |             +23% |      +459% |
| macOS x86-64   | Intel Core i7-8700B      | scalar |             +72% |      +187% |
| Linux AArch64  | Neoverse-N2              | NEON   |             +34% |      +358% |
| Linux AArch64  | Neoverse-N2              | scalar |             +36% |      +179% |
| macOS AArch64  | Apple M4¹                | NEON   |             +61% |      +612% |
| macOS AArch64  | Apple M4¹                | scalar |             +61% |      +296% |
| macOS AArch64  | Apple M1 (virtual)       | NEON   |             +53% |      +589% |
| macOS AArch64  | Apple M1 (virtual)       | scalar |             +71% |      +333% |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | AVX2   |             +37% |      +552% |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | SSE2   |             +32% |      +483% |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | scalar |             +48% |      +104% |

JPEG decompression:

| Platform       | CPU                      | SIMD   | vs libjpeg-turbo | vs libjpeg |
| -------------- | ------------------------ | ------ | ---------------: | ---------: |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | AVX2   |             +34% |      +188% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | SSE2   |             +36% |      +174% |
| Linux x86-64   | AMD EPYC 7763 (Zen 3)    | scalar |             +21% |       +47% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | AVX2   |             +45% |      +210% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | SSE2   |             +42% |      +178% |
| Linux x86-64   | AMD EPYC 9V74 (Zen 4)    | scalar |             +22% |       +48% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | AVX2   |             +44% |      +196% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | SSE2   |             +45% |      +173% |
| Linux x86-64   | AMD EPYC 9V45 (Zen 5)    | scalar |             +27% |       +60% |
| Linux x86-64   | Intel Xeon 8573C         | AVX2   |             +47% |      +163% |
| Linux x86-64   | Intel Xeon 8573C         | SSE2   |             +42% |      +135% |
| Linux x86-64   | Intel Xeon 8573C         | scalar |             +27% |       +55% |
| macOS x86-64   | Intel Core i7-8700B      | AVX2   |             +28% |      +163% |
| macOS x86-64   | Intel Core i7-8700B      | SSE2   |             +22% |      +169% |
| macOS x86-64   | Intel Core i7-8700B      | scalar |             +23% |       +85% |
| Linux AArch64  | Neoverse-N2              | NEON   |             +37% |      +115% |
| Linux AArch64  | Neoverse-N2              | scalar |             +27% |       +35% |
| macOS AArch64  | Apple M4¹                | NEON   |             +49% |      +142% |
| macOS AArch64  | Apple M4¹                | scalar |             +47% |       +63% |
| macOS AArch64  | Apple M1 (virtual)       | NEON   |             +49% |      +157% |
| macOS AArch64  | Apple M1 (virtual)       | scalar |             +47% |       +77% |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | AVX2   |             +31% |      +150% |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | SSE2   |             +28% |      +129% |
| Windows x86-64 | AMD EPYC 7763 (Zen 3)²   | scalar |             +23% |       +31% |

¹ A local Mac (native, same build and benchmark script).
² The GitHub-hosted Windows runner, with MSVC.

Per configuration; each summary figure above is the geometric mean of the
first seven columns.  4:4:4, 4:2:2, 4:2:0 and gray at quality 95; fastDCT,
prog. and arith.: 4:2:0 with the fast integer DCT, progressive and
arithmetic-coded.

JPEG compression, vio vs libjpeg-turbo:

| CPU, SIMD            | 4:4:4 | 4:2:2 | 4:2:0 |  gray | fastDCT | prog. | arith. | lossless |
| -------------------- | ----: | ----: | ----: | ----: | ------: | ----: | -----: | -------: |
| EPYC 7763 AVX2       |  +35% |  +31% |  +29% |  +40% |    +38% |  +68% |   +32% |     +33% |
| EPYC 7763 SSE2       |  +29% |  +25% |  +22% |  +29% |    +23% |  +65% |   +31% |     +33% |
| EPYC 7763 scalar     |  +67% |  +56% |  +49% |  +65% |    +44% |  +30% |   +40% |     +33% |
| EPYC 9V74 AVX2       |  +36% |  +33% |  +32% |  +47% |    +42% |  +75% |   +50% |     +30% |
| EPYC 9V74 SSE2       |  +36% |  +29% |  +25% |  +31% |    +27% |  +72% |   +49% |     +30% |
| EPYC 9V74 scalar     |  +74% |  +61% |  +53% |  +66% |    +49% |  +30% |   +54% |     +30% |
| EPYC 9V45 AVX2       |  +34% |  +31% |  +29% |  +48% |    +30% |  +96% |   +57% |     +33% |
| EPYC 9V45 SSE2       |  +31% |  +27% |  +25% |  +33% |    +24% |  +91% |   +55% |     +31% |
| EPYC 9V45 scalar     |  +99% |  +78% |  +69% |  +94% |    +67% |  +34% |   +63% |     +34% |
| Xeon 8573C AVX2      |  +36% |  +35% |  +34% |  +50% |    +31% |  +86% |   +46% |     +86% |
| Xeon 8573C SSE2      |  +24% |  +24% |  +23% |  +29% |    +25% |  +81% |   +44% |     +88% |
| Xeon 8573C scalar    |  +86% |  +72% |  +64% |  +81% |    +61% |  +35% |   +53% |     +86% |
| i7-8700B AVX2        |  +32% |  +16% |   +7% |  +33% |    +27% |  +92% |   +25% |     +41% |
| i7-8700B SSE2        |  +18% |  +14% |  +12% |  +18% |    +12% |  +79% |   +22% |     +42% |
| i7-8700B scalar      | +100% |  +92% |  +84% |  +89% |    +68% |  +42% |   +38% |     +41% |
| Neoverse-N2 NEON     |  +23% |  +22% |  +20% |  +30% |    +22% |  +85% |   +44% |     +75% |
| Neoverse-N2 scalar   |  +28% |  +30% |  +32% |  +31% |    +34% |  +54% |   +48% |     +75% |
| M4¹ NEON             |  +47% |  +43% |  +40% |  +65% |    +34% | +148% |   +76% |     +54% |
| M4¹ scalar           |  +55% |  +58% |  +60% |  +71% |    +54% |  +50% |   +79% |     +54% |
| M1 (virtual) NEON    |  +44% |  +26% |  +39% |  +55% |    +19% | +131% |   +81% |     +48% |
| M1 (virtual) scalar  |  +74% |  +73% |  +74% |  +81% |    +61% |  +53% |   +83% |     +56% |
| EPYC 7763² AVX2      |  +27% |  +23% |  +21% |  +29% |    +26% |  +71% |   +68% |     +52% |
| EPYC 7763² SSE2      |  +22% |  +19% |  +18% |  +22% |    +19% |  +68% |   +68% |     +51% |
| EPYC 7763² scalar    |  +55% |  +47% |  +41% |  +48% |    +45% |  +30% |   +70% |     +53% |

JPEG decompression, vio vs libjpeg-turbo:

| CPU, SIMD            | 4:4:4 | 4:2:2 | 4:2:0 |  gray | fastDCT | prog. | arith. | lossless |
| -------------------- | ----: | ----: | ----: | ----: | ------: | ----: | -----: | -------: |
| EPYC 7763 AVX2       |  +26% |  +28% |  +29% |  +39% |    +35% |  +50% |   +30% |     +33% |
| EPYC 7763 SSE2       |  +33% |  +33% |  +30% |  +47% |    +30% |  +50% |   +30% |     +33% |
| EPYC 7763 scalar     |  +13% |  +12% |  +17% |  +27% |    +22% |  +30% |   +28% |     +33% |
| EPYC 9V74 AVX2       |  +42% |  +39% |  +39% |  +52% |    +42% |  +61% |   +44% |     +23% |
| EPYC 9V74 SSE2       |  +43% |  +36% |  +33% |  +50% |    +34% |  +53% |   +44% |     +23% |
| EPYC 9V74 scalar     |  +14% |  +11% |  +16% |  +29% |    +21% |  +30% |   +39% |     +22% |
| EPYC 9V45 AVX2       |  +40% |  +37% |  +36% |  +39% |    +38% |  +76% |   +47% |     +38% |
| EPYC 9V45 SSE2       |  +49% |  +42% |  +36% |  +44% |    +34% |  +72% |   +43% |     +37% |
| EPYC 9V45 scalar     |  +18% |  +18% |  +19% |  +28% |    +23% |  +43% |   +42% |     +39% |
| Xeon 8573C AVX2      |  +39% |  +42% |  +40% |  +49% |    +43% |  +75% |   +43% |     +37% |
| Xeon 8573C SSE2      |  +34% |  +37% |  +34% |  +45% |    +34% |  +68% |   +42% |     +38% |
| Xeon 8573C scalar    |  +17% |  +17% |  +18% |  +33% |    +24% |  +43% |   +39% |     +37% |
| i7-8700B AVX2        |  +15% |  +18% |  +14% |  +47% |    +18% |  +43% |   +44% |     +16% |
| i7-8700B SSE2        |  +12% |  +12% |  +11% |  +37% |    +10% |  +32% |   +42% |     +20% |
| i7-8700B scalar      |  +13% |  +17% |  +19% |  +21% |    +24% |  +26% |   +41% |     +20% |
| Neoverse-N2 NEON     |  +31% |  +31% |  +33% |  +40% |    +37% |  +48% |   +39% |     +48% |
| Neoverse-N2 scalar   |  +29% |  +18% |  +21% |  +33% |    +30% |  +28% |   +35% |     +48% |
| M4¹ NEON             |  +45% |  +39% |  +40% |  +52% |    +45% |  +73% |   +47% |     +78% |
| M4¹ scalar           |  +40% |  +40% |  +50% |  +36% |    +58% |  +58% |   +48% |     +78% |
| M1 (virtual) NEON    |  +47% |  +49% |  +41% |  +58% |    +48% |  +49% |   +51% |     +82% |
| M1 (virtual) scalar  |  +41% |  +40% |  +49% |  +40% |    +63% |  +48% |   +47% |     +81% |
| EPYC 7763² AVX2      |  +25% |  +26% |  +26% |  +29% |    +27% |  +40% |   +44% |     +39% |
| EPYC 7763² SSE2      |  +26% |  +26% |  +21% |  +28% |    +20% |  +37% |   +43% |     +40% |
| EPYC 7763² scalar    |  +11% |  +16% |  +19% |  +19% |    +25% |  +29% |   +40% |     +39% |

With SIMD, baseline compression is 18–65% faster than libjpeg-turbo and
sequential decompression 20–58% (7–33% and 10–47% on the macOS x86-64
runner, the noisiest); without SIMD, 41–100% and 11–33% on x86, 28–81% and
18–63% on AArch64.  Progressive, arithmetic-coded and lossless JPEG compress
30–148%, 22–83% and 30–88% faster, and decompress 26–76%, 28–51% and 16–82%
faster.  Differences of a few percent are within run-to-run variation.

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
textures, game and web screenshots; image-png's own benchmark corpus): the
geometric mean over the images of the speed-up in megapixels per second,
each image the best of 3 runs.  The same comparators at every SIMD level:
libpng 1.6.59 with zlib 1.3.2, the most widely used combination, and the
fastest decoder and encoders measured.  All are built from source with the
same compiler (GCC on Linux, MSVC on Windows, Apple Clang on macOS; rustc for
image-png).

- Decoding produces the image's own sample layout and verifies both
  checksums (chunk CRC-32, zlib Adler-32) in every library.  image-png 0.18.1
  (`Transformations::IDENTITY`, `ignore_checksums(false)`) is the fastest
  decoder measured, ahead of Wuffs 0.4, zune-png 0.5, libspng and libpng
  with zlib-ng 2.3.
- Encoding starts from 8-bit RGB or RGBA.  vio's zlib mode
  (`Encoder::Mode::kZlib`) writes libpng's files byte for byte at the same
  zlib level, so the libpng columns (levels 1 and 6, the default) compare
  speed alone.  vio's default fast mode, a single-pass encoder, is compared
  with fpnge (x86 only; its AVX2 build in the AVX2 rows, SSE4.1 in the SSE2
  rows) and image-png's fast mode; its files are 4–5% smaller than fpnge's
  and 4% smaller than image-png's.  Both modes are lossless.
- vio's SIMD level is set with `JSIMD_*`.  In the scalar rows libpng (whose
  only SIMD code unfilters rows) is built without SIMD and image-png's
  checksum crates use their portable code
  (`tests/png/bench/image-png/build_portable.py`); loops the compiler
  vectorizes by itself stay vectorized in every library.

The benchmark is `tests/png/bench/` (`png_bench.py run`, then `report` or
`compare`); `.github/workflows/bench-png.yml` runs it on the GitHub-hosted
runners, one job per platform and SIMD level, so the CPU can differ between
rows.  Results from one run at
[d72c74b](https://github.com/ufoym/vio/commit/d72c74b).

PNG decoding:

| Platform, CPU       | SIMD   | vs libpng | vs image-png |
| ------------------- | ------ | --------: | -----------: |
| Linux, EPYC 7763    | AVX2   |     +183% |         +40% |
| Linux, EPYC 7763    | SSE2   |     +151% |         +14% |
| Linux, EPYC 7763    | scalar |     +129% |         +13% |
| Windows, EPYC 7763  | AVX2   |     +222% |         +26% |
| Windows, EPYC 9V45  | SSE2   |     +162% |         +14% |
| Windows, Xeon 8573C | scalar |     +172% |         +31% |
| macOS, i7-8700B     | AVX2   |     +260% |         +19% |
| macOS, i7-8700B     | SSE2   |     +224% |          −8% |
| macOS, i7-8700B     | scalar |     +211% |         +24% |
| Linux, Neoverse-N2  | NEON   |     +152% |         +21% |
| Linux, Neoverse-N2  | scalar |     +120% |         +40% |
| macOS, M1 (virtual) | NEON   |     +182% |         +26% |
| macOS, M1 (virtual) | scalar |     +231% |         +47% |

PNG encoding: vio's zlib mode vs libpng at levels 1 and 6 (L1, L6); vio's
fast mode vs fpnge and vs image-png's fast mode.

| Platform, CPU       | SIMD   | vs libpng L1 | vs libpng L6 | vs fpnge | vs image-png |
| ------------------- | ------ | -----------: | -----------: | -------: | -----------: |
| Linux, EPYC 7763    | AVX2   |        +124% |         +52% |     +33% |         +85% |
| Linux, EPYC 7763    | SSE2   |        +112% |         +48% |     +34% |         +29% |
| Linux, EPYC 7763    | scalar |        +100% |         +48% |        — |         +21% |
| Windows, EPYC 7763  | AVX2   |        +120% |         +43% |    +145% |         +90% |
| Windows, EPYC 9V45  | SSE2   |        +116% |         +51% |    +107% |         +20% |
| Windows, Xeon 8573C | scalar |         +67% |         +43% |        — |         −19% |
| macOS, i7-8700B     | AVX2   |        +195% |         +75% |     +31% |        +101% |
| macOS, i7-8700B     | SSE2   |        +132% |         +59% |     +34% |         +32% |
| macOS, i7-8700B     | scalar |         +94% |         +57% |        — |          −2% |
| Linux, Neoverse-N2  | NEON   |        +111% |         +45% |        — |         +92% |
| Linux, Neoverse-N2  | scalar |         +94% |         +42% |        — |         +67% |
| macOS, M1 (virtual) | NEON   |        +128% |         +57% |        — |         +82% |
| macOS, M1 (virtual) | scalar |        +110% |         +74% |        — |         +87% |

The fast mode's files are also about the size of libpng's at level 1 (1%
smaller), which it writes 12 to 20 times as fast with AVX2 or NEON.

By image category, number of images in parentheses (Linux x86-64, AMD EPYC
7763, GCC, AVX2); dec.: decoding, enc.: encoding.

| Category | dec. vs image-png | dec. vs libpng | enc. vs libpng¹ | enc. vs fpnge² |
| --- | ---: | ---: | ---: | ---: |
| icon_512 (213) | +42% | +314% | +101% | +20% (−16% size) |
| icon_64 (213) | +40% | +189% | +59% | +81% (−17% size) |
| photo_kodak (24) | +38% | +130% | +25% | +17% (−3% size) |
| photo_tecnick (100) | +38% | +165% | +32% | +11% (−2% size) |
| photo_wikipedia (49) | +43% | +142% | +31% | +19% (−3% size) |
| pngimg (187) | +38% | +239% | +63% | +20% (−4% size) |
| screenshot_game (618) | +47% | +221% | +75% | +17% (−6% size) |
| screenshot_web (14) | +90% | +258% | +86% | +49% (−11% size) |
| textures_photo (20) | +43% | +121% | +22% | +32% (−8% size) |
| textures_pk (1002) | +37% | +140% | +39% | +53% (−8% size) |
| textures_pk01 (113) | +39% | +190% | +31% | +16% (−3% size) |
| textures_pk02 (235) | +38% | +158% | +29% | +12% (−3% size) |
| textures_plants (60) | +39% | +215% | +49% | +22% (−3% size) |

¹ zlib mode, level 6. ² Fast mode.

The macOS x86-64 runner varies the most: a rerun of its SSE2 job measured
decoding +3% and encoding +14% against image-png (−8% and +32% above).
fpnge has no Arm code; image-png's fast mode is its fpnge-inspired encoder.

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
- Huffman coder's bit writer copied field by field around zero runs, where MSVC's struct copy (a 16-byte load right after 8-byte stores) stalled store forwarding ([d4abf9f](https://github.com/ufoym/vio/commit/d4abf9f)): encoding +13% (AVX2), +14% (SSE2), +11% (without SIMD) on the Core Ultra 9 285K with MSVC, 1/8 of the corpus.
- Scalar Adler-32 on 64-bit words (byte sums by a multiply, per-position sums in 16-bit lanes) for MSVC and Clang on x86, which vectorized the per-position form poorly ([d0f39d8](https://github.com/ufoym/vio/commit/d0f39d8)): Adler-32 17 instead of 11 GB/s, decoding without SIMD +3% (Core Ultra 9 285K, MSVC, 1/8 of the corpus).
- Braided CRC-32 in named 64-bit words instead of arrays, which MSVC kept in memory ([b3747bf](https://github.com/ufoym/vio/commit/b3747bf)): CRC-32 without PCLMULQDQ 10 instead of 5 GB/s; decoding +3% without SIMD and with SSE2, SSE2 encoding +4% (Core Ultra 9 285K, MSVC, 1/8 of the corpus).
- Scalar Paeth of 3- and 4-byte pixels for Clang on x86 on 16-bit lanes with t = 3c - a - b, the prediction max(a, b), min(a, b) or c by two comparisons with t, which Clang vectorizes over the pixel's bytes ([47697f5](https://github.com/ufoym/vio/commit/47697f5)): decoding without SIMD +8.5% (Core Ultra 9 285K, clang-cl, 1/8 of the corpus).
- Encoder output buffer kept per thread for the next image, where Windows returned each large buffer to the OS and faulted every page of the next one in ([b10524b](https://github.com/ufoym/vio/commit/b10524b)): encoding +16% without SIMD, +11% with SSE2 (Core Ultra 9 285K, MSVC, 1/8 of the corpus).
- Scalar filter selection on x86 on 16-bit values (Paeth by comparisons with 3c - a - b), which SSE2 compares and bounds natively ([175a138](https://github.com/ufoym/vio/commit/175a138)): encoding without SIMD +5% (Core Ultra 9 285K, clang-cl, 1/8 of the corpus); files unchanged.
- NEON reconstruction of consecutive Sub, Average and Paeth rows in pairs, the second a 192-byte block behind the first, two independent dependency chains instead of one; data inflated a row past the image's start so the second row never overwrites input still to be read ([87ff9cd](https://github.com/ufoym/vio/commit/87ff9cd)): decoding +4% (Apple M4), +8% (Cortex-X925, GCC), 1/8 of the corpus.
- SSE2, AVX2 and generic Paeth predictors in fewer operations: the sign test from the saturated differences already computed, both selections from one minimum ([8f0da1f](https://github.com/ufoym/vio/commit/8f0da1f)): encoding +3% with AVX2, +3–6% with SSE2 (Core Ultra 9 285K, MSVC and clang-cl, 1/8 of the corpus); files unchanged.
- Inflate fast loop compiled a second time with BMI2 shifts (GCC, Clang; chosen at run time, not at the SSE2 level), one micro-op each where shifts by cl take up to three on the lookup chain ([1b7636d](https://github.com/ufoym/vio/commit/1b7636d)): decoding +1% (Core Ultra 9 285K, clang-cl, 1/8 of the corpus).
- MSVC scalar filter selection storing the Paeth residuals as it costs them, so rows taking Paeth are not filtered again ([801d2b2](https://github.com/ufoym/vio/commit/801d2b2)): encoding without SIMD +6–8% (Core Ultra 9 285K, MSVC, 1/8 of the corpus); files unchanged.
- NEON, SSE2 and AVX2 filter selection storing the Paeth residuals as it costs them, so rows taking Paeth are not filtered again ([75e6e3f](https://github.com/ufoym/vio/commit/75e6e3f)): encoding +2–4% (Apple M4), +3% (Cortex-X925, GCC), +4% with AVX2 and +6% with SSE2 (Core Ultra 9 285K, MSVC), 1/8 of the corpus; files unchanged.
- Consecutive Paeth rows in pairs, the second one pixel behind, in GCC's scalar code on AArch64 ([ce86b25](https://github.com/ufoym/vio/commit/ce86b25)): decoding without SIMD +2% (Cortex-X925, GCC, 1/8 of the corpus).
- Scalar Paeth of 3- and 4-byte pixels by minimum, maximum and two comparisons with 3c - a - b for MSVC and Clang ([faf21b4](https://github.com/ufoym/vio/commit/faf21b4)): decoding without SIMD +12% (Core Ultra 9 285K, MSVC), +3% (Apple M4), 1/8 of the corpus.
- GCC's scalar Paeth by the same minimum/maximum comparisons in branchless form ([ff9e269](https://github.com/ufoym/vio/commit/ff9e269)): decoding without SIMD +0.8% where rows are not paired (Cortex-X925, GCC, 1/8 of the corpus).
- The same in GCC's paired Paeth rows ([3b7ca71](https://github.com/ufoym/vio/commit/3b7ca71)): decoding without SIMD +1% (Cortex-X925, GCC, 1/8 of the corpus).

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
`-scalar` variant), `VIO_BUILD_JPEGLIB` (the `bind/jpeglib/` libraries; on
when vio is the top-level project), `VIO_JPEG_LIB_VERSION` (their ABI
level), `VIO_TEST_EXTENDED` (the script-based tests below).

The JPEG tests run twice over: on vio's own command-line tools and
TurboJPEG layer (variants `simd` and `scalar`), and on libjpeg-turbo's
programs -- cjpeg, djpeg, jpegtran, example, tjunittest, tjbench and
bmpsizetest, compiled unchanged against `bind/jpeglib/` -- linked with its
shared and static libraries (the `shared` and `static` test names, as in
libjpeg-turbo's own build).  `jpeglib-test` checks the rest of the
`jpeglib.h` API (suspending sources and destinations, buffered-image mode,
marker processors, abbreviated datastreams, error recovery, raw data).  With
`-DVIO_TEST_EXTENDED=ON`, `ctest -L extended` also runs libjpeg-turbo's
script-based tests (its tjtest, tjtest12, tjtest16 and tjexampletest targets,
and with ImageMagick croptest and indexedcolortest), which compare the
TurboJPEG programs' output with cjpeg's, djpeg's and jpegtran's (bash needed;
about 20 minutes).

CI (`.github/workflows/ci.yml`) runs the whole suite on every push: natively
with GCC and Clang on Linux x86-64, GCC on Linux i686 and AArch64, Apple
Clang on macOS x86-64 and AArch64, MSVC (x64, x86) and clang-cl on Windows;
under QEMU 10.2 on AArch32 (NEON at build time, and detected at run time),
PowerPC 64 LE (AltiVec, VSX) and 32 BE (AltiVec), MIPS64 Loongson MMI and
RISC-V 64 RVV (VLEN 128 and 256).  On x86 the suite is repeated at the lower
SIMD levels (`JSIMD_FORCESSE2`, and `JSIMD_FORCESSE` on i686).  All 23 runs
pass at `d72c74b`.  The native build on an Apple M4 runs 1409 tests (plus
18 extended ones), all passing.

On Windows, configure from a Visual Studio developer command prompt with
Ninja (add `-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl` for
clang-cl):

```sh
cmake -S . -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/msvc
ctest --test-dir build/msvc -j 8
```

The PNG tests (`tests/png/tests.cmake`) mirror libpng's test suite: pngtest,
pnggetset, pngimage, pngstest, pngunknown and pngvalid, under the same names,
on libpng's images and on generated ones.  Their reference values,
`tests/png/expected.txt`, are what libpng produces for the same cases;
to regenerate them after changing the cases (`tests/png/png_cases.hpp`),
build `png-golden` against the libpng submodule (needs zlib):

```sh
cmake --preset native -DVIO_PNG_GOLDEN=ON
cmake --build build/native --target png-golden
build/native/png-golden tests/libpng > tests/png/expected.txt
```

To benchmark against libjpeg-turbo, build it from the submodule into a tree
of its own; to add IJG libjpeg, unpack its source (`jpegsrc.v10.tar.gz` from
ijg.org) and point `VIO_LIBJPEG_SOURCE_DIR` at it, which builds
`jpeg-bench-libjpeg`:

```sh
cmake -S tests/libjpeg-turbo -B build/libjpeg-turbo -DCMAKE_BUILD_TYPE=Release
cmake --build build/libjpeg-turbo -j 8   # tjbench
cmake --preset native -DVIO_LIBJPEG_SOURCE_DIR=$PWD/build/jpeg-10
cmake --build build/native --target jpeg-bench-libjpeg
```

## Repository layout

```
include/vio/
├── core.hpp                          shared by all codecs: color spaces,
│                                     vio::Error, row reader/writer interfaces
├── bmp.hpp gif.hpp ppm.hpp targa.hpp one file per format; needs only core.hpp
├── png.hpp                           includes the png/ codec; Reader, Writer
├── png/
│   ├── decoder.hpp encoder.hpp       public API (whole-image codec, read/write
│   │   transform.hpp stream.hpp      transforms, progressive decoding, pixel
│   │   simple.hpp types.hpp          format conversion, chunk/header types)
│   ├── config.hpp                    compiler macros, SIMD back-end selection
│   │                                 (VIO_NO_SIMD, ...)
│   ├── detail/                       codec internals
│   │   ├── common.hpp                byte I/O, Adam7, chunk/sRGB tables
│   │   ├── fast_encoder.hpp          single-pass encoder (SIMD filtering
│   │   │                             and Huffman coding)
│   │   ├── inflate.hpp               DEFLATE decompression (whole buffer,
│   │   │                             or resumable)
│   │   ├── inflate_fast.inc          its fast loop (compiled per ISA)
│   │   ├── deflate.hpp               DEFLATE compression, zlib-identical
│   │   │                             output
│   │   ├── filter.hpp                scanline filters (scalar, NEON, SSE2,
│   │   │                             generic vector kernels)
│   │   └── checksum.hpp              CRC-32, Adler-32 (PCLMUL/PMULL folding,
│   │                                 Arm CRC32, SSSE3/NEON/generic sums)
│   └── simd/
│       ├── cpu.hpp                   run-time ISA detection, JSIMD_* env vars
│       └── vec.hpp                   128-bit vector interface `V` (SSE2,
│                                     AltiVec, MMI, RVV)
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
bind/
├── c/vio.h                           header-only C interface
├── jpeglib/                          libjpeg and libturbojpeg: jpeglib.h,
│                                     turbojpeg.h and their implementations
│                                     (jpeglib.cpp, turbojpeg.cpp)
└── python/vio/                       Python package: __init__.py over the
                                      _vio extension (_vio.cpp, on vio.h)
tests/                                conformance and benchmark suite
├── c/ python/                        tests of the C and Python interfaces
├── libjpeg-turbo/ libpng/            test programs and images (git submodules)
├── jpeg/                             JPEG tests (jpeg-test, jpeg-bench,
│   ├── tests.cmake                   bmp-test-size), tools (jpeg-encode,
│   └── tools/ shim/ libjpeg/         jpeg-decode, jpeg-transform, jpeg-example),
│                                     TurboJPEG and shims
└── png/                              PNG tests (png-test, png-golden) and
    ├── tests.cmake expected.txt      benchmark suite (bench/: vio vs
    └── bench/                        image-png, fpnge, libpng, Wuffs)
```

## License

MIT; `LICENSE` also contains the third-party notices that apply.
