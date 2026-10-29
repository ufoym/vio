# The JPEG programs that jpeg/tests.cmake runs:
#   jpeg-encode/-decode/-transform/-example, bmp-test-size
#                         vio's stand-ins (tools/, bmp_test_size.cpp), one
#                         per variant
#   jpeg-test, jpeg-bench libjpeg-turbo's tjunittest and tjbench on vio's
#                         TurboJPEG layer, one per variant
#   cjpeg-shared, ...     libjpeg-turbo's programs, unchanged, against the
#                         shared and static bind/c/jpeglib libraries
#
# Inputs: LJT_SOURCE_DIR, LJT_SHIM_DIR, VIO_VARIANTS.

# libjpeg-turbo's programs are compiled from a copy so that their quoted
# includes of the library headers (jpeglib.h, turbojpeg.h, ...) resolve to
# bind/c/jpeglib/, and of jconfigint.h/jversion.h to jpeg/shim/, rather than
# to the real headers next to them in the submodule.
set(LJT_DIR ${CMAKE_CURRENT_BINARY_DIR}/libjpeg-turbo)
set(LJT_PROGRAM_FILES cjpeg.c djpeg.c jpegtran.c example.c bmpsizetest.c
  strtest.c tjcomp.c tjdecomp.c tjtran.c cdjpeg.c cdjpeg.h cderror.h cmyk.h
  jinclude.h jsamplecomp.h jpegapicomp.h rdswitch.c rdbmp.c rdgif.c rdpng.c
  rdppm.c rdtarga.c rdcolmap.c wrbmp.c wrgif.c wrpng.c wrppm.c wrtarga.c
  transupp.c transupp.h tjunittest.c tjbench.c tjutil.c tjutil.h
  md5/md5.c md5/md5.h md5/md5hl.c md5/md5cmp.c md5/md5sum.c wrjpgcom.c
  spng/spng.c spng/spng.h)
file(GLOB LJT_GLOBBED RELATIVE ${LJT_SOURCE_DIR}/src
  ${LJT_SOURCE_DIR}/src/wrapper/rd*.c ${LJT_SOURCE_DIR}/src/wrapper/wr*.c
  ${LJT_SOURCE_DIR}/src/spng/zlib/*.c ${LJT_SOURCE_DIR}/src/spng/zlib/*.h)
foreach(f ${LJT_PROGRAM_FILES} ${LJT_GLOBBED})
  configure_file(${LJT_SOURCE_DIR}/src/${f} ${LJT_DIR}/${f} COPYONLY)
endforeach()

# Settings for the libjpeg-turbo C programs.
macro(vio_ljt_program target)
  target_include_directories(${target} PRIVATE
    ${LJT_SHIM_DIR} ${PROJECT_SOURCE_DIR}/bind/c/jpeglib
    ${LJT_DIR})
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

  add_executable(bmp-test-size${sfx} jpeg/bmp_test_size.cpp)
  target_link_libraries(bmp-test-size${sfx} PRIVATE vio)
  target_compile_definitions(bmp-test-size${sfx} PRIVATE ${defs})
  add_custom_target(bmpsizetest${sfx} DEPENDS bmp-test-size${sfx})

  # The TurboJPEG library (bind/c/jpeglib), built for this variant.
  add_library(turbojpeg-vio${sfx} STATIC
    ${PROJECT_SOURCE_DIR}/bind/c/jpeglib/turbojpeg.cpp)
  target_include_directories(turbojpeg-vio${sfx} PUBLIC
    ${PROJECT_SOURCE_DIR}/bind/c/jpeglib)
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

# libjpeg-turbo's programs (cjpeg, djpeg, jpegtran, example, tjunittest, ...),
# unchanged, built against the jpeglib.h and turbojpeg.h libraries of
# bind/c/jpeglib: test "libtypes" shared and static, as in libjpeg-turbo.
add_library(ljt-zlib OBJECT ${LJT_DIR}/spng/zlib/adler32.c
  ${LJT_DIR}/spng/zlib/compress.c ${LJT_DIR}/spng/zlib/crc32.c
  ${LJT_DIR}/spng/zlib/deflate.c ${LJT_DIR}/spng/zlib/inffast.c
  ${LJT_DIR}/spng/zlib/inflate.c ${LJT_DIR}/spng/zlib/inftrees.c
  ${LJT_DIR}/spng/zlib/trees.c ${LJT_DIR}/spng/zlib/zutil.c)
add_library(ljt-spng OBJECT ${LJT_DIR}/spng/spng.c)
target_include_directories(ljt-spng PRIVATE ${LJT_DIR}/spng/zlib)
target_compile_definitions(ljt-spng PRIVATE SPNG_STATIC)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(ljt-zlib PRIVATE -w)
  target_compile_options(ljt-spng PRIVATE -w)
endif()
set(LJT_PNG_OBJECTS $<TARGET_OBJECTS:ljt-spng> $<TARGET_OBJECTS:ljt-zlib>)
set(CDJPEG_DEFS BMP_SUPPORTED GIF_SUPPORTED PNG_SUPPORTED SPNG_STATIC
  PPM_SUPPORTED TARGA_SUPPORTED)
if(WIN32)
  list(APPEND CDJPEG_DEFS USE_SETMODE)
endif()
include(CheckSymbolExists)
check_symbol_exists(fmemopen "stdio.h" HAVE_FMEMOPEN)

# Static cross builds cannot link the shared-library test programs.
option(VIO_TEST_SHARED "Also test the shared jpeglib.h/turbojpeg.h libraries" ON)
set(VIO_COMPAT_LIBTYPES static)
if(VIO_TEST_SHARED)
  list(PREPEND VIO_COMPAT_LIBTYPES shared)
endif()
set(VIO_LIB_shared "")
set(VIO_LIB_static -static)
foreach(kind ${VIO_COMPAT_LIBTYPES})
  set(sfx -${kind})
  set(jpeglib jpeg${VIO_LIB_${kind}})
  set(tjlib turbojpeg${VIO_LIB_${kind}})
  set(VIO_SUFFIX_${kind} ${sfx})
  macro(ljt_program name lib)
    add_executable(${name}${sfx} ${ARGN})
    target_link_libraries(${name}${sfx} PRIVATE ${lib})
    set_target_properties(${name}${sfx} PROPERTIES LINKER_LANGUAGE CXX)
    vio_ljt_program(${name}${sfx})
  endmacro()
  ljt_program(cjpeg ${jpeglib} ${LJT_DIR}/cjpeg.c ${LJT_DIR}/cdjpeg.c
    ${LJT_DIR}/rdbmp.c ${LJT_DIR}/rdgif.c ${LJT_DIR}/wrapper/rdpng-8.c
    ${LJT_DIR}/wrapper/rdpng-12.c ${LJT_DIR}/wrapper/rdpng-16.c
    ${LJT_DIR}/wrapper/rdppm-8.c ${LJT_DIR}/wrapper/rdppm-12.c
    ${LJT_DIR}/wrapper/rdppm-16.c ${LJT_DIR}/rdswitch.c ${LJT_DIR}/rdtarga.c
    ${LJT_PNG_OBJECTS})
  ljt_program(djpeg ${jpeglib} ${LJT_DIR}/djpeg.c ${LJT_DIR}/cdjpeg.c
    ${LJT_DIR}/wrapper/rdcolmap-8.c ${LJT_DIR}/wrapper/rdcolmap-12.c
    ${LJT_DIR}/rdswitch.c ${LJT_DIR}/wrbmp.c ${LJT_DIR}/wrapper/wrgif-8.c
    ${LJT_DIR}/wrapper/wrgif-12.c ${LJT_DIR}/wrapper/wrpng-8.c
    ${LJT_DIR}/wrapper/wrpng-12.c ${LJT_DIR}/wrapper/wrpng-16.c
    ${LJT_DIR}/wrapper/wrppm-8.c ${LJT_DIR}/wrapper/wrppm-12.c
    ${LJT_DIR}/wrapper/wrppm-16.c ${LJT_DIR}/wrtarga.c ${LJT_PNG_OBJECTS})
  ljt_program(jpegtran ${jpeglib} ${LJT_DIR}/jpegtran.c ${LJT_DIR}/cdjpeg.c
    ${LJT_DIR}/rdswitch.c ${LJT_DIR}/transupp.c)
  ljt_program(example ${jpeglib} ${LJT_DIR}/example.c)
  foreach(t cjpeg djpeg)
    target_compile_definitions(${t}${sfx} PRIVATE ${CDJPEG_DEFS})
  endforeach()
  if(WIN32)
    target_compile_definitions(jpegtran${sfx} PRIVATE USE_SETMODE)
  endif()
  if(HAVE_FMEMOPEN)
    ljt_program(bmpsizetest ${jpeglib} ${LJT_DIR}/bmpsizetest.c
      ${LJT_DIR}/rdbmp.c ${LJT_DIR}/wrbmp.c ${LJT_DIR}/rdgif.c
      ${LJT_DIR}/wrapper/wrgif-8.c ${LJT_DIR}/wrapper/wrgif-12.c
      ${LJT_DIR}/wrapper/rdpng-8.c ${LJT_DIR}/wrapper/rdpng-12.c
      ${LJT_DIR}/wrapper/rdpng-16.c ${LJT_DIR}/wrapper/wrpng-8.c
      ${LJT_DIR}/wrapper/wrpng-12.c ${LJT_DIR}/wrapper/wrpng-16.c
      ${LJT_DIR}/wrapper/rdppm-8.c ${LJT_DIR}/wrapper/rdppm-12.c
      ${LJT_DIR}/wrapper/rdppm-16.c ${LJT_DIR}/wrapper/wrppm-8.c
      ${LJT_DIR}/wrapper/wrppm-12.c ${LJT_DIR}/wrapper/wrppm-16.c
      ${LJT_DIR}/rdtarga.c ${LJT_DIR}/wrtarga.c ${LJT_PNG_OBJECTS})
    target_compile_definitions(bmpsizetest${sfx} PRIVATE ${CDJPEG_DEFS})
  endif()
  ljt_program(tjunittest ${tjlib} ${LJT_DIR}/tjunittest.c ${LJT_DIR}/tjutil.c
    ${LJT_DIR}/md5/md5.c ${LJT_DIR}/md5/md5hl.c)
  ljt_program(tjbench ${tjlib} ${LJT_DIR}/tjbench.c ${LJT_DIR}/tjutil.c)
  foreach(t tjcomp tjdecomp tjtran)
    ljt_program(${t} ${tjlib} ${LJT_DIR}/${t}.c)
  endforeach()
  ljt_program(strtest ${jpeglib} ${LJT_DIR}/strtest.c)
  if(UNIX)
    target_link_libraries(tjbench${sfx} PRIVATE m)
  endif()
  foreach(t cjpeg djpeg jpegtran example tjunittest tjbench bmpsizetest)
    set(VIO_TOOL_${t}_${kind} ${t})
  endforeach()
endforeach()

# Inputs of jpeg/tests.cmake.
set(TEST_LIBTYPES ${VIO_VARIANTS} ${VIO_COMPAT_LIBTYPES})
# The x86 SIMD builds use libjpeg-turbo's SSE floating-point DCT kernels
# for 8-bit samples, whose results differ from the C algorithms'.
if(VIO_TARGET_ARCH MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
  foreach(t simd ${VIO_COMPAT_LIBTYPES})
    set(VIO_FLOATTEST8_${t} sse)
  endforeach()
endif()
set(WITH_TURBOJPEG 1)
set(WITH_ARITH_ENC 1)
set(WITH_ARITH_DEC 1)
