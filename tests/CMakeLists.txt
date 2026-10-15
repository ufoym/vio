# Test suite: libjpeg-turbo's tests, run against vio.
#
#   libjpeg-turbo/        libjpeg-turbo 3.2.0 (git submodule): test programs,
#                         TurboJPEG header and test images, used unchanged
#   libjpeg-turbo.cmake   libjpeg-turbo's test definitions, adapted to vio
#   tools/                jpeg-encode/-decode/-transform/-example: command-line
#                         compatible stand-ins for cjpeg/djpeg/jpegtran/example
#   turbojpeg/            the TurboJPEG API implemented on vio, for tjunittest
#                         and tjbench
#   shim/                 stand-ins for the libjpeg-turbo internal headers that
#                         the test programs include
#   bmpsizetest.cpp       port of libjpeg-turbo's bmpsizetest.c

set(LJT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libjpeg-turbo)
if(NOT EXISTS ${LJT_SOURCE_DIR}/src/turbojpeg.h)
  message(FATAL_ERROR "tests/libjpeg-turbo is missing; run\n"
    "  git submodule update --init\n"
    "or configure with -DVIO_BUILD_TESTS=OFF.")
endif()

# The libjpeg-turbo sources are compiled from a copy so that their quoted
# includes of cdjpeg.h and jconfigint.h resolve to shim/ rather than to the
# real headers next to them in the submodule.
set(LJT_DIR ${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo)
foreach(f tjunittest.c tjbench.c tjutil.c tjutil.h turbojpeg.h
          md5/md5.c md5/md5.h md5/md5hl.c md5/md5cmp.c)
  configure_file(${LJT_SOURCE_DIR}/src/${f} ${LJT_DIR}/${f} COPYONLY)
endforeach()

# Settings for the libjpeg-turbo C programs.
macro(vio_ljt_program target)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim ${LJT_DIR})
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE -w)
  endif()
endmacro()

set(VIO_TOOL_cjpeg jpeg-encode)
set(VIO_TOOL_djpeg jpeg-decode)
set(VIO_TOOL_jpegtran jpeg-transform)
set(VIO_TOOL_example jpeg-example)

foreach(variant ${VIO_VARIANTS})
  set(sfx ${VIO_SUFFIX_${variant}})
  set(defs ${VIO_DEFS_${variant}})

  foreach(tool encode decode transform example)
    add_executable(jpeg-${tool}${sfx} tools/jpeg_${tool}.cpp)
    target_link_libraries(jpeg-${tool}${sfx} PRIVATE vio)
    target_compile_definitions(jpeg-${tool}${sfx} PRIVATE ${defs})
  endforeach()

  add_executable(bmpsizetest${sfx} bmpsizetest.cpp)
  target_link_libraries(bmpsizetest${sfx} PRIVATE vio)
  target_compile_definitions(bmpsizetest${sfx} PRIVATE ${defs})

  add_library(turbojpeg-vio${sfx} STATIC turbojpeg/turbojpeg.cpp)
  target_include_directories(turbojpeg-vio${sfx} PUBLIC ${LJT_DIR})
  target_link_libraries(turbojpeg-vio${sfx} PUBLIC vio)
  target_compile_definitions(turbojpeg-vio${sfx} PRIVATE ${defs})

  add_executable(tjunittest${sfx} ${LJT_DIR}/tjunittest.c ${LJT_DIR}/tjutil.c
    ${LJT_DIR}/md5/md5.c ${LJT_DIR}/md5/md5hl.c)
  add_executable(tjbench${sfx} ${LJT_DIR}/tjbench.c ${LJT_DIR}/tjutil.c)
  foreach(t tjunittest tjbench)
    target_link_libraries(${t}${sfx} PRIVATE turbojpeg-vio${sfx})
    set_target_properties(${t}${sfx} PROPERTIES LINKER_LANGUAGE CXX)
    vio_ljt_program(${t}${sfx})
  endforeach()
endforeach()

add_executable(md5cmp ${LJT_DIR}/md5/md5cmp.c ${LJT_DIR}/md5/md5.c
  ${LJT_DIR}/md5/md5hl.c)
vio_ljt_program(md5cmp)

# Optional, for performance comparisons: tjbench on IJG libjpeg (9 or later),
# built from an unpacked jpegsrc.v*.tar.gz with the same compiler and flags.
set(VIO_LIBJPEG_SOURCE_DIR "" CACHE PATH
  "IJG libjpeg source tree; builds tjbench-libjpeg when set")
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
  # libjpeg/jconfig.h stands in for the header configure would generate.
  target_include_directories(ijg-jpeg PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libjpeg ${VIO_LIBJPEG_SOURCE_DIR})
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ijg-jpeg PRIVATE -w)
  endif()

  add_library(turbojpeg-libjpeg STATIC libjpeg/turbojpeg.c)
  target_include_directories(turbojpeg-libjpeg PUBLIC ${LJT_DIR})
  target_link_libraries(turbojpeg-libjpeg PUBLIC ijg-jpeg)

  add_executable(tjbench-libjpeg ${LJT_DIR}/tjbench.c ${LJT_DIR}/tjutil.c)
  target_link_libraries(tjbench-libjpeg PRIVATE turbojpeg-libjpeg)
  if(UNIX AND NOT APPLE)
    target_link_libraries(tjbench-libjpeg PRIVATE m)
  endif()
  vio_ljt_program(tjbench-libjpeg)
endif()

# Inputs of libjpeg-turbo.cmake.
set(TEST_LIBTYPES ${VIO_VARIANTS})
# The x86 SIMD variant uses libjpeg-turbo's SSE floating-point DCT kernels
# for 8-bit samples, whose results differ from the C algorithms'.
if(VIO_TARGET_ARCH MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
  set(VIO_FLOATTEST8_simd sse)
endif()
set(WITH_TURBOJPEG 1)
set(WITH_ARITH_ENC 1)
set(WITH_ARITH_DEC 1)
include(libjpeg-turbo.cmake)

# PNG benchmark (png-bench/): vio, and optionally fpnge (x86 only; it needs
# SSE4.1, PCLMUL and AVX2) and libpng with zlib, built from source trees with
# the same compiler.
add_executable(png-bench png-bench/png_bench.cpp)
target_link_libraries(png-bench PRIVATE vio)
set(VIO_FPNGE_SOURCE_DIR "" CACHE PATH
  "fpnge source tree; adds fpnge to png-bench when set")
if(VIO_FPNGE_SOURCE_DIR)
  target_sources(png-bench PRIVATE ${VIO_FPNGE_SOURCE_DIR}/fpnge.cc)
  target_include_directories(png-bench PRIVATE ${VIO_FPNGE_SOURCE_DIR})
  target_compile_definitions(png-bench PRIVATE VIO_BENCH_FPNGE)
  if(MSVC)
    set_source_files_properties(${VIO_FPNGE_SOURCE_DIR}/fpnge.cc PROPERTIES
      COMPILE_OPTIONS "/arch:AVX2")
  else()
    set_source_files_properties(${VIO_FPNGE_SOURCE_DIR}/fpnge.cc PROPERTIES
      COMPILE_OPTIONS "-march=haswell;-w")
  endif()
endif()
set(VIO_LIBPNG_SOURCE_DIR "" CACHE PATH
  "libpng 1.6 source tree; adds libpng to png-bench (with VIO_ZLIB_SOURCE_DIR)")
set(VIO_ZLIB_SOURCE_DIR "" CACHE PATH "zlib source tree for libpng in png-bench")
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
  if(VIO_TARGET_ARCH MATCHES "^(arm64|aarch64|ARM64)$")
    list(APPEND LIBPNG_SOURCES ${VIO_LIBPNG_SOURCE_DIR}/arm/arm_init.c
      ${VIO_LIBPNG_SOURCE_DIR}/arm/filter_neon_intrinsics.c
      ${VIO_LIBPNG_SOURCE_DIR}/arm/palette_neon_intrinsics.c)
    set(LIBPNG_DEFS PNG_ARM_NEON_OPT=2)
  elseif(VIO_TARGET_ARCH MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
    list(APPEND LIBPNG_SOURCES ${VIO_LIBPNG_SOURCE_DIR}/intel/intel_init.c
      ${VIO_LIBPNG_SOURCE_DIR}/intel/filter_sse2_intrinsics.c)
    set(LIBPNG_DEFS PNG_INTEL_SSE_OPT=1)
  else()
    set(LIBPNG_DEFS PNG_ARM_NEON_OPT=0)
  endif()
  configure_file(${VIO_LIBPNG_SOURCE_DIR}/scripts/pnglibconf.h.prebuilt
    ${CMAKE_CURRENT_BINARY_DIR}/libpng/pnglibconf.h COPYONLY)
  add_library(bench-libpng STATIC ${LIBPNG_SOURCES})
  target_include_directories(bench-libpng PUBLIC ${VIO_LIBPNG_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/libpng)
  target_compile_definitions(bench-libpng PRIVATE ${LIBPNG_DEFS})
  target_link_libraries(bench-libpng PUBLIC bench-zlib)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(bench-zlib PRIVATE -w)
    target_compile_options(bench-libpng PRIVATE -w)
  endif()
  target_link_libraries(png-bench PRIVATE bench-libpng)
  target_compile_definitions(png-bench PRIVATE VIO_BENCH_LIBPNG)
endif()
