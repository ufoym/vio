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
