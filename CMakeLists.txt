cmake_minimum_required(VERSION 3.16)
project(vio VERSION 0.1.0 LANGUAGES C CXX)

# vio is header-only: the library itself is the `vio` INTERFACE target.
# bind/c/jpeglib/ builds the jpeglib.h/turbojpeg.h compatible libraries;
# tests/ builds the conformance test suite and the programs it runs.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(vio INTERFACE)
add_library(vio::vio ALIAS vio)
# bind/c/vio/vio.h: the header-only C interface.
target_include_directories(vio INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bind/c/vio>
  $<INSTALL_INTERFACE:include>)
target_compile_features(vio INTERFACE cxx_std_20)

set(VIO_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(VIO_IS_TOP_LEVEL ON)
endif()
option(VIO_BUILD_TESTS "Build the test suite (needs the test submodule)"
  ${VIO_IS_TOP_LEVEL})
option(VIO_BUILD_JPEGLIB "Build the jpeglib.h/turbojpeg.h compatible libraries"
  ${VIO_IS_TOP_LEVEL})
if(VIO_BUILD_TESTS AND NOT VIO_BUILD_JPEGLIB)
  message(FATAL_ERROR "VIO_BUILD_TESTS requires VIO_BUILD_JPEGLIB")
endif()

if(VIO_BUILD_JPEGLIB)
  add_subdirectory(bind/c/jpeglib)
endif()

if(VIO_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(DIRECTORY include/ DESTINATION include)
install(FILES bind/c/vio/vio.h DESTINATION include)
install(TARGETS vio EXPORT vioTargets)
install(EXPORT vioTargets NAMESPACE vio:: DESTINATION lib/cmake/vio)
