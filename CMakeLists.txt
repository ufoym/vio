cmake_minimum_required(VERSION 3.16)
project(vio VERSION 0.1.0 LANGUAGES C CXX)

# vio is header-only: the library itself is the `vio` INTERFACE target.
# tests/ builds the conformance test suite and the programs it runs.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(vio INTERFACE)
add_library(vio::vio ALIAS vio)
# bind/c/vio.h: the header-only C interface.
target_include_directories(vio INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bind/c>
  $<INSTALL_INTERFACE:include>)
target_compile_features(vio INTERFACE cxx_std_20)

set(VIO_TARGET_ARCH ${CMAKE_SYSTEM_PROCESSOR})
if(CMAKE_OSX_ARCHITECTURES)
  set(VIO_TARGET_ARCH ${CMAKE_OSX_ARCHITECTURES})
endif()

# On 32-bit x86, x87 floating point (excess precision) would make the scalar
# floating-point DCT results compiler-dependent; build the tests with SSE2
# math so that they are reproducible.  (The library itself still selects its
# SSE2/AVX2 kernels at run time when built without -msse2.)
option(VIO_X86_SSE2_MATH "Build the tests for 32-bit x86 with SSE2 math" ON)
if(VIO_X86_SSE2_MATH AND VIO_TARGET_ARCH MATCHES "^(i[3-6]86|x86)$" AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-msse2 -mfpmath=sse)
endif()

# Sources are UTF-8; MSVC otherwise reads them in the system code page.
# The test programs use the standard C I/O functions that the MSVC runtime
# marks as deprecated.
if(MSVC)
  add_compile_options(/utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

set(VIO_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(VIO_IS_TOP_LEVEL ON)
endif()
option(VIO_BUILD_TESTS "Build the test suite (needs the test submodule)"
  ${VIO_IS_TOP_LEVEL})
option(VIO_TEST_SCALAR "Also build and test a variant with SIMD disabled" ON)

# Test variants: "simd" (default) and, optionally, "scalar" (VIO_NO_SIMD).
# Each test program is built once per variant, named with VIO_SUFFIX_<variant>
# and compiled with VIO_DEFS_<variant>.
set(VIO_VARIANTS simd)
set(VIO_SUFFIX_simd "")
set(VIO_DEFS_simd "")
if(VIO_TEST_SCALAR)
  list(APPEND VIO_VARIANTS scalar)
  set(VIO_SUFFIX_scalar "-scalar")
  set(VIO_DEFS_scalar VIO_NO_SIMD)
endif()

if(VIO_BUILD_TESTS)
  # Executables go to the top of the build tree (scripts run them there).
  if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  endif()
  enable_testing()
  add_subdirectory(tests)
endif()

install(DIRECTORY include/ DESTINATION include)
install(FILES bind/c/vio.h DESTINATION include)
install(TARGETS vio EXPORT vioTargets)
install(EXPORT vioTargets NAMESPACE vio:: DESTINATION lib/cmake/vio)
