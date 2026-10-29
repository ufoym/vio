# Test suite: libjpeg-turbo's and libpng's tests, run against vio.
#
#   third_party/          git submodules, used unchanged
#     libjpeg-turbo/      libjpeg-turbo 3.2.0: test programs, TurboJPEG
#                         header and test images
#     libpng/             libpng, branch libpng18: test images
#   jpeg/
#     build.cmake         the programs: vio's tools and libjpeg-turbo's
#                         programs, built against vio and bind/c/jpeglib
#     tests.cmake         libjpeg-turbo's test definitions, adapted to vio
#     extended.cmake      libjpeg-turbo's script-based tests (optional)
#     tools/              jpeg-encode/-decode/-transform/-example: command-line
#                         compatible stand-ins for cjpeg/djpeg/jpegtran/example
#     shim/               stand-ins for the generated libjpeg-turbo headers
#                         (jconfigint.h, jversion.h) that the programs include
#     bmp_test_size.cpp   port of libjpeg-turbo's bmpsizetest.c
#   png/
#     build.cmake         png-test, and png-golden (optional)
#     tests.cmake         libpng's tests, adapted to vio
#     png_test.cpp        test runner for libpng's test cases
#     png_cases.hpp       shared test case definitions
#     png_golden.cpp      regenerates expected.txt from libpng
#     expected.txt        the images as libpng decodes them
#   bind/                 tests of bind/, in the same layout:
#     tests.cmake         c-test and jpeglib-test
#     c/vio/              c-test: the C interface (bind/c/vio/vio.h)
#     c/jpeglib/          jpeglib-test: the jpeglib.h API (bind/c/jpeglib)
#     python/             the Python package (pytest tests/bind/python)
#
# The benchmarks (bench/) are configured from here too, with the same
# settings.

set(VIO_TARGET_ARCH ${CMAKE_SYSTEM_PROCESSOR})
if(CMAKE_OSX_ARCHITECTURES)
  set(VIO_TARGET_ARCH ${CMAKE_OSX_ARCHITECTURES})
endif()

# The bind/c/jpeglib libraries that the libjpeg-turbo programs link
# (jpeg/build.cmake).
set(VIO_JPEGLIB_TARGETS jpeg jpeg-static turbojpeg turbojpeg-static)

# Executables (and DLLs) go to the top of the build tree (scripts run them
# there).
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  set_target_properties(${VIO_JPEGLIB_TARGETS} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

# On 32-bit x86, x87 floating point (excess precision) would make the scalar
# floating-point DCT results compiler-dependent; build the tests, and the
# libraries they test, with SSE2 math so that they are reproducible.  (The
# library itself still selects its SSE2/AVX2 kernels at run time when built
# without -msse2.)
option(VIO_X86_SSE2_MATH "Build the tests for 32-bit x86 with SSE2 math" ON)
if(VIO_X86_SSE2_MATH AND VIO_TARGET_ARCH MATCHES "^(i[3-6]86|x86)$" AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-msse2 -mfpmath=sse)
  foreach(t ${VIO_JPEGLIB_TARGETS})
    target_compile_options(${t} PRIVATE -msse2 -mfpmath=sse)
  endforeach()
endif()

# Sources are UTF-8; MSVC otherwise reads them in the system code page.
# The test programs use the standard C I/O functions that the MSVC runtime
# marks as deprecated.
if(MSVC)
  add_compile_options(/utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Test variants: "simd" (default) and, optionally, "scalar" (VIO_NO_SIMD).
# Each test program is built once per variant, named with VIO_SUFFIX_<variant>
# and compiled with VIO_DEFS_<variant>.
option(VIO_TEST_SCALAR "Also build and test a variant with SIMD disabled" ON)
set(VIO_VARIANTS simd)
set(VIO_SUFFIX_simd "")
set(VIO_DEFS_simd "")
if(VIO_TEST_SCALAR)
  list(APPEND VIO_VARIANTS scalar)
  set(VIO_SUFFIX_scalar "-scalar")
  set(VIO_DEFS_scalar VIO_NO_SIMD)
endif()

set(LJT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjpeg-turbo)
set(LIBPNG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpng)
set(LJT_SHIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/jpeg/shim)
if(NOT EXISTS ${LJT_SOURCE_DIR}/src/turbojpeg.h OR
   NOT EXISTS ${LIBPNG_DIR}/pngtest.png)
  message(FATAL_ERROR "tests/third_party/libjpeg-turbo or tests/third_party/libpng "
    "is missing; run\n"
    "  git submodule update --init\n"
    "or configure with -DVIO_BUILD_TESTS=OFF.")
endif()

include(jpeg/build.cmake)
include(jpeg/tests.cmake)
include(jpeg/extended.cmake)
include(png/build.cmake)
include(png/tests.cmake)
include(bind/tests.cmake)

add_subdirectory(${PROJECT_SOURCE_DIR}/bench ${CMAKE_CURRENT_BINARY_DIR}/bench)
