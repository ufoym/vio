# Test suite: libjpeg-turbo's and libpng's tests, run against vio.
#
#   libjpeg-turbo/        libjpeg-turbo 3.2.0 (git submodule): test programs,
#                         TurboJPEG header and test images, used unchanged
#   libpng/               libpng, branch libpng18 (git submodule): test images
#   jpeg/
#     tests.cmake         libjpeg-turbo's test definitions, adapted to vio
#     tools/              jpeg-encode/-decode/-transform/-example: command-line
#                         compatible stand-ins for cjpeg/djpeg/jpegtran/example
#     turbojpeg.cpp       the TurboJPEG API implemented on vio, for jpeg-test
#                         and jpeg-bench
#     shim/               stand-ins for the libjpeg-turbo internal headers that
#                         the test programs include
#     libjpeg/            TurboJPEG layer and config for IJG libjpeg benchmarks
#     bmp_test_size.cpp   port of libjpeg-turbo's bmpsizetest.c
#   c/                    tests of the C interface (bind/c/vio.h)
#   python/               tests of the Python package (pytest tests/python)
#   png/
#     tests.cmake         libpng's tests, adapted to vio (png/png_test.cpp)
#     expected.txt        the images as libpng decodes them
#     png_test.cpp        test runner for libpng's test cases
#     png_golden.cpp      regenerates expected.txt from libpng
#     png_cases.hpp       shared test case definitions
#     bench/              PNG throughput benchmark suite

set(LJT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libjpeg-turbo)
if(NOT EXISTS ${LJT_SOURCE_DIR}/src/turbojpeg.h OR
   NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libpng/pngtest.png)
  message(FATAL_ERROR "tests/libjpeg-turbo or tests/libpng is missing; run\n"
    "  git submodule update --init\n"
    "or configure with -DVIO_BUILD_TESTS=OFF.")
endif()

# The libjpeg-turbo sources are compiled from a copy so that their quoted
# includes of cdjpeg.h and jconfigint.h resolve to jpeg/shim/ rather than to the
# real headers next to them in the submodule.
set(LJT_DIR ${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo)
foreach(f tjunittest.c tjbench.c tjutil.c tjutil.h turbojpeg.h
          md5/md5.c md5/md5.h md5/md5hl.c md5/md5cmp.c)
  configure_file(${LJT_SOURCE_DIR}/src/${f} ${LJT_DIR}/${f} COPYONLY)
endforeach()

# Settings for the libjpeg-turbo C programs.
macro(vio_ljt_program target)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/jpeg/shim ${LJT_DIR})
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE -w)
  endif()
endmacro()

set(VIO_TOOL_cjpeg jpeg-encode)
set(VIO_TOOL_djpeg jpeg-decode)
set(VIO_TOOL_jpegtran jpeg-transform)
set(VIO_TOOL_example jpeg-example)
set(VIO_TOOL_tjunittest jpeg-test)
set(VIO_TOOL_tjbench jpeg-bench)
set(VIO_TOOL_bmpsizetest bmp-test-size)
set(VIO_TOOL_md5cmp md5-cmp)

foreach(variant ${VIO_VARIANTS})
  set(sfx ${VIO_SUFFIX_${variant}})
  set(defs ${VIO_DEFS_${variant}})

  foreach(tool encode decode transform example)
    add_executable(jpeg-${tool}${sfx} jpeg/tools/jpeg_${tool}.cpp)
    target_link_libraries(jpeg-${tool}${sfx} PRIVATE vio)
    target_compile_definitions(jpeg-${tool}${sfx} PRIVATE ${defs})
  endforeach()

  add_executable(png-test${sfx} png/png_test.cpp)
  target_link_libraries(png-test${sfx} PRIVATE vio)
  target_compile_definitions(png-test${sfx} PRIVATE ${defs})

  add_executable(bmp-test-size${sfx} jpeg/bmp_test_size.cpp)
  target_link_libraries(bmp-test-size${sfx} PRIVATE vio)
  target_compile_definitions(bmp-test-size${sfx} PRIVATE ${defs})
  add_custom_target(bmpsizetest${sfx} DEPENDS bmp-test-size${sfx})

  add_library(turbojpeg-vio${sfx} STATIC jpeg/turbojpeg.cpp)
  target_include_directories(turbojpeg-vio${sfx} PUBLIC ${LJT_DIR})
  target_link_libraries(turbojpeg-vio${sfx} PUBLIC vio)
  target_compile_definitions(turbojpeg-vio${sfx} PRIVATE ${defs})

  add_executable(jpeg-test${sfx} ${LJT_DIR}/tjunittest.c ${LJT_DIR}/tjutil.c
    ${LJT_DIR}/md5/md5.c ${LJT_DIR}/md5/md5hl.c)
  add_executable(jpeg-bench${sfx} ${LJT_DIR}/tjbench.c ${LJT_DIR}/tjutil.c)
  foreach(t jpeg-test jpeg-bench)
    target_link_libraries(${t}${sfx} PRIVATE turbojpeg-vio${sfx})
    set_target_properties(${t}${sfx} PROPERTIES LINKER_LANGUAGE CXX)
    vio_ljt_program(${t}${sfx})
  endforeach()
  add_custom_target(tjunittest${sfx} DEPENDS jpeg-test${sfx})
  add_custom_target(tjbench${sfx} DEPENDS jpeg-bench${sfx})
endforeach()

add_executable(md5-cmp ${LJT_DIR}/md5/md5cmp.c ${LJT_DIR}/md5/md5.c
  ${LJT_DIR}/md5/md5hl.c)
vio_ljt_program(md5-cmp)
add_custom_target(md5cmp DEPENDS md5-cmp)

# Optional, for performance comparisons: jpeg-bench on IJG libjpeg (9 or later),
# built from an unpacked jpegsrc.v*.tar.gz with the same compiler and flags.
set(VIO_LIBJPEG_SOURCE_DIR "" CACHE PATH
  "IJG libjpeg source tree; builds jpeg-bench-libjpeg when set")
if(VIO_LIBJPEG_SOURCE_DIR)
  set(IJG_SOURCES jaricom jcapimin jcapistd jcarith jccoefct jccolor jcdctmgr
    jchuff jcinit jcmainct jcmarker jcmaster jcomapi jcparam jcprepct jcsample
    jctrans jdapimin jdapistd jdarith jdatadst jdatasrc jdcoefct jdcolor
    jddctmgr jdhuff jdinput jdmainct jdmarker jdmaster jdmerge jdpostct
    jdsample jdtrans jerror jfdctflt jfdctfst jfdctint jidctflt jidctfst
    jidctint jquant1 jquant2 jutils jmemmgr jmemnobs)
  list(TRANSFORM IJG_SOURCES PREPEND ${VIO_LIBJPEG_SOURCE_DIR}/)
  list(TRANSFORM IJG_SOURCES APPEND .c)
  add_library(ijg-jpeg STATIC ${IJG_SOURCES})
  # jpeg/libjpeg/jconfig.h stands in for the header configure would generate.
  target_include_directories(ijg-jpeg PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/jpeg/libjpeg ${VIO_LIBJPEG_SOURCE_DIR})
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ijg-jpeg PRIVATE -w)
  endif()

  add_library(turbojpeg-libjpeg STATIC jpeg/libjpeg/turbojpeg.c)
  target_include_directories(turbojpeg-libjpeg PUBLIC ${LJT_DIR})
  target_link_libraries(turbojpeg-libjpeg PUBLIC ijg-jpeg)

  add_executable(jpeg-bench-libjpeg ${LJT_DIR}/tjbench.c ${LJT_DIR}/tjutil.c)
  target_link_libraries(jpeg-bench-libjpeg PRIVATE turbojpeg-libjpeg)
  if(UNIX AND NOT APPLE)
    target_link_libraries(jpeg-bench-libjpeg PRIVATE m)
  endif()
  vio_ljt_program(jpeg-bench-libjpeg)
  add_custom_target(tjbench-libjpeg DEPENDS jpeg-bench-libjpeg)
endif()

# Inputs of jpeg/tests.cmake.
set(TEST_LIBTYPES ${VIO_VARIANTS})
# The x86 SIMD variant uses libjpeg-turbo's SSE floating-point DCT kernels
# for 8-bit samples, whose results differ from the C algorithms'.
if(VIO_TARGET_ARCH MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
  set(VIO_FLOATTEST8_simd sse)
endif()
set(WITH_TURBOJPEG 1)
set(WITH_ARITH_ENC 1)
set(WITH_ARITH_DEC 1)
include(jpeg/tests.cmake)
include(png/tests.cmake)

add_executable(c-test c/c_test.c c/vio_impl.cpp)
target_link_libraries(c-test PRIVATE vio)
add_test(NAME c-test COMMAND c-test ${LJT_SOURCE_DIR}/testimages
  ${CMAKE_CURRENT_BINARY_DIR})

# PNG benchmark (png/bench/): vio, and optionally fpnge (x86 only; it needs
# SSE4.1 and PCLMUL, AVX2 in its fastest build), libpng with zlib and Wuffs,
# built from source trees with the same compiler.  One executable per SIMD
# level of the other libraries, run with the matching JSIMD_* variable for
# vio:
#   png-bench          their default builds (fpnge for AVX2)
#   png-bench-sse      fpnge for SSE4.1 (x86, with fpnge)
#   png-bench-scalar   libpng and Wuffs without their SIMD code
set(VIO_FPNGE_SOURCE_DIR "" CACHE PATH
  "fpnge source tree; adds fpnge to png-bench when set")
set(VIO_LIBPNG_SOURCE_DIR "" CACHE PATH
  "libpng 1.6 source tree; adds libpng to png-bench (with VIO_ZLIB_SOURCE_DIR)")
set(VIO_ZLIB_SOURCE_DIR "" CACHE PATH "zlib source tree for libpng in png-bench")
set(VIO_WUFFS_SOURCE_DIR "" CACHE PATH
  "directory of Wuffs' release/c/wuffs-v0.4.c; adds Wuffs to png-bench")

if(VIO_LIBPNG_SOURCE_DIR AND VIO_ZLIB_SOURCE_DIR)
  set(ZLIB_SOURCES adler32 compress crc32 deflate infback inffast inflate
    inftrees trees uncompr zutil)
  list(TRANSFORM ZLIB_SOURCES PREPEND ${VIO_ZLIB_SOURCE_DIR}/)
  list(TRANSFORM ZLIB_SOURCES APPEND .c)
  add_library(bench-zlib STATIC ${ZLIB_SOURCES})
  target_include_directories(bench-zlib PUBLIC ${VIO_ZLIB_SOURCE_DIR})
  set(LIBPNG_SOURCES png pngerror pngget pngmem pngpread pngread pngrio
    pngrtran pngrutil pngset pngtrans pngwio pngwrite pngwtran pngwutil)
  list(TRANSFORM LIBPNG_SOURCES PREPEND ${VIO_LIBPNG_SOURCE_DIR}/)
  list(TRANSFORM LIBPNG_SOURCES APPEND .c)
  # libpng's default build: its SIMD filters for the target.
  set(LIBPNG_SIMD_SOURCES)
  if(VIO_TARGET_ARCH MATCHES "^(arm64|aarch64|ARM64)$")
    set(LIBPNG_SIMD_SOURCES arm/arm_init.c arm/filter_neon_intrinsics.c
      arm/palette_neon_intrinsics.c)
    set(LIBPNG_DEFS PNG_ARM_NEON_OPT=2)
  elseif(VIO_TARGET_ARCH MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
    set(LIBPNG_SIMD_SOURCES intel/intel_init.c intel/filter_sse2_intrinsics.c)
    set(LIBPNG_DEFS PNG_INTEL_SSE_OPT=1)
  else()
    set(LIBPNG_DEFS PNG_ARM_NEON_OPT=0)
  endif()
  list(TRANSFORM LIBPNG_SIMD_SOURCES PREPEND ${VIO_LIBPNG_SOURCE_DIR}/)
  configure_file(${VIO_LIBPNG_SOURCE_DIR}/scripts/pnglibconf.h.prebuilt
    ${CMAKE_CURRENT_BINARY_DIR}/libpng/pnglibconf.h COPYONLY)
  add_library(bench-libpng STATIC ${LIBPNG_SOURCES} ${LIBPNG_SIMD_SOURCES})
  target_compile_definitions(bench-libpng PRIVATE ${LIBPNG_DEFS})
  # without SIMD (libpng's configuration option for each architecture)
  add_library(bench-libpng-scalar STATIC ${LIBPNG_SOURCES})
  target_compile_definitions(bench-libpng-scalar PRIVATE PNG_ARM_NEON_OPT=0
    PNG_INTEL_SSE_OPT=0 PNG_POWERPC_VSX_OPT=0 PNG_MIPS_MSA_OPT=0
    PNG_MIPS_MMI_OPT=0 PNG_LOONGARCH_LSX_OPT=0 PNG_RISCV_RVV_OPT=0)
  foreach(t bench-libpng bench-libpng-scalar)
    target_include_directories(${t} PUBLIC ${VIO_LIBPNG_SOURCE_DIR}
      ${CMAKE_CURRENT_BINARY_DIR}/libpng)
    target_link_libraries(${t} PUBLIC bench-zlib)
  endforeach()
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(bench-zlib PRIVATE -w)
    target_compile_options(bench-libpng PRIVATE -w)
    target_compile_options(bench-libpng-scalar PRIVATE -w)
  endif()
endif()

if(VIO_WUFFS_SOURCE_DIR)
  foreach(t bench-wuffs bench-wuffs-scalar)
    add_library(${t} STATIC ${VIO_WUFFS_SOURCE_DIR}/wuffs-v0.4.c)
    target_include_directories(${t} PUBLIC ${VIO_WUFFS_SOURCE_DIR})
    target_compile_definitions(${t} PRIVATE WUFFS_IMPLEMENTATION
      WUFFS_CONFIG__MODULES WUFFS_CONFIG__MODULE__BASE
      WUFFS_CONFIG__MODULE__ADLER32 WUFFS_CONFIG__MODULE__CRC32
      WUFFS_CONFIG__MODULE__DEFLATE WUFFS_CONFIG__MODULE__ZLIB
      WUFFS_CONFIG__MODULE__PNG)
  endforeach()
  target_compile_definitions(bench-wuffs-scalar PRIVATE
    WUFFS_CONFIG__AVOID_CPU_ARCH)
endif()

# png_bench(<target> <level>), level: default, sse or scalar (see above).
function(png_bench target level)
  add_executable(${target} png/bench/png_bench.cpp)
  target_link_libraries(${target} PRIVATE vio)
  if(VIO_FPNGE_SOURCE_DIR AND NOT level STREQUAL "scalar")
    # (a copy of the source per build: the ISA flags apply per file)
    set(src ${CMAKE_CURRENT_BINARY_DIR}/fpnge-${level}.cc)
    configure_file(${VIO_FPNGE_SOURCE_DIR}/fpnge.cc ${src} COPYONLY)
    target_sources(${target} PRIVATE ${src})
    target_include_directories(${target} PRIVATE ${VIO_FPNGE_SOURCE_DIR})
    target_compile_definitions(${target} PRIVATE VIO_BENCH_FPNGE)
    if(MSVC)
      if(level STREQUAL "default")
        set_source_files_properties(${src} PROPERTIES COMPILE_OPTIONS "/arch:AVX2")
      endif()
    elseif(level STREQUAL "default")
      set_source_files_properties(${src} PROPERTIES COMPILE_OPTIONS "-march=haswell;-w")
    else()
      set_source_files_properties(${src} PROPERTIES COMPILE_OPTIONS "-msse4.1;-mpclmul;-w")
    endif()
  endif()
  set(suffix "")
  if(level STREQUAL "scalar")
    set(suffix "-scalar")
  endif()
  if(TARGET bench-libpng)
    target_link_libraries(${target} PRIVATE bench-libpng${suffix})
    target_compile_definitions(${target} PRIVATE VIO_BENCH_LIBPNG)
  endif()
  if(TARGET bench-wuffs)
    target_link_libraries(${target} PRIVATE bench-wuffs${suffix})
    target_compile_definitions(${target} PRIVATE VIO_BENCH_WUFFS)
  endif()
endfunction()
png_bench(png-bench default)
if(VIO_FPNGE_SOURCE_DIR)
  png_bench(png-bench-sse sse)
endif()
if(TARGET bench-libpng OR TARGET bench-wuffs)
  png_bench(png-bench-scalar scalar)
endif()
