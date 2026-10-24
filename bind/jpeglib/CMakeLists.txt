# The jpeglib.h and turbojpeg.h C interfaces, built on vio as shared and
# static libraries: vio::jpeg / vio::jpeg-static (named libjpeg) and
# vio::turbojpeg / vio::turbojpeg-static (named libturbojpeg), with the
# library names, versions and versioned symbols of their usual
# implementations, so that they can replace those.

set(VIO_JPEG_LIB_VERSION 62 CACHE STRING
  "jpeglib.h API/ABI level of libjpeg: 62, 70 or 80")
set_property(CACHE VIO_JPEG_LIB_VERSION PROPERTY STRINGS 62 70 80)
if(NOT VIO_JPEG_LIB_VERSION MATCHES "^(62|70|80)$")
  message(FATAL_ERROR "VIO_JPEG_LIB_VERSION must be 62, 70 or 80")
endif()

# Shared library versions: libjpeg.so.62.4.0, .7.4.0 or .8.3.2;
# libturbojpeg.so.0.5.0.
math(EXPR major "${VIO_JPEG_LIB_VERSION} / 10")
math(EXPR minor "${VIO_JPEG_LIB_VERSION} % 10")
set(VIO_JPEG_LIB_VERSION_DECIMAL ${major}.${minor})
if(VIO_JPEG_LIB_VERSION EQUAL 62)
  set(jpeg_so 62)
  set(jpeg_version 62.4.0)
elseif(VIO_JPEG_LIB_VERSION EQUAL 70)
  set(jpeg_so 7)
  set(jpeg_version 7.4.0)
else()
  set(jpeg_so 8)
  set(jpeg_version 8.3.2)
endif()
# In-memory sources/destinations are an extension before the 8.0 ABI.
if(VIO_JPEG_LIB_VERSION LESS 80)
  set(VIO_MEM_SRCDST_FUNCTIONS "global: jpeg_mem_dest; jpeg_mem_src;")
endif()

# The installed jconfig.h records the ABI level; in the source tree it is a
# definition on the targets.
set(VIO_JPEGLIB_GEN ${CMAKE_CURRENT_BINARY_DIR}/include)
file(READ jconfig.h jconfig)
string(REPLACE "#ifndef JPEG_LIB_VERSION\n#define JPEG_LIB_VERSION  62\n#endif"
  "#define JPEG_LIB_VERSION  ${VIO_JPEG_LIB_VERSION}" jconfig "${jconfig}")
file(CONFIGURE OUTPUT ${VIO_JPEGLIB_GEN}/jconfig.h CONTENT "${jconfig}")
configure_file(libjpeg.map.in ${CMAKE_CURRENT_BINARY_DIR}/libjpeg.map @ONLY)

include(CheckCSourceCompiles)
if(UNIX AND NOT APPLE)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/conftest.map "VERS_1 { global: *; };")
  set(CMAKE_REQUIRED_FLAGS
    "-Wl,--version-script,${CMAKE_CURRENT_BINARY_DIR}/conftest.map")
  check_c_source_compiles("int main(void) { return 0; }" VIO_HAVE_VERSION_SCRIPT)
  unset(CMAKE_REQUIRED_FLAGS)
endif()

macro(vio_c_library target type output source)
  add_library(${target} ${type} ${source})
  add_library(vio::${target} ALIAS ${target})
  target_include_directories(${target} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
  if(NOT VIO_JPEG_LIB_VERSION EQUAL 62)
    target_compile_definitions(${target} PUBLIC
      $<BUILD_INTERFACE:JPEG_LIB_VERSION=${VIO_JPEG_LIB_VERSION}>)
  endif()
  target_link_libraries(${target} PRIVATE vio)
  set_target_properties(${target} PROPERTIES
    OUTPUT_NAME ${output}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  if(${type} STREQUAL SHARED AND WIN32)
    set_target_properties(${target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  endif()
  if(${type} STREQUAL STATIC)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
endmacro()

# Windows keeps import and static libraries apart by name.
if(WIN32)
  set(static_sfx -static)
else()
  set(static_sfx "")
endif()

vio_c_library(jpeg SHARED jpeg jpeglib.cpp)
set_target_properties(jpeg PROPERTIES VERSION ${jpeg_version} SOVERSION ${jpeg_so})
if(WIN32)
  set_target_properties(jpeg PROPERTIES RUNTIME_OUTPUT_NAME jpeg${jpeg_so})
endif()
vio_c_library(jpeg-static STATIC jpeg${static_sfx} jpeglib.cpp)

vio_c_library(turbojpeg SHARED turbojpeg turbojpeg.cpp)
set_target_properties(turbojpeg PROPERTIES VERSION 0.5.0 SOVERSION 0)
vio_c_library(turbojpeg-static STATIC turbojpeg${static_sfx} turbojpeg.cpp)

if(VIO_HAVE_VERSION_SCRIPT)
  target_link_options(jpeg PRIVATE
    -Wl,--version-script,${CMAKE_CURRENT_BINARY_DIR}/libjpeg.map)
  target_link_options(turbojpeg PRIVATE
    -Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/turbojpeg.map)
  set_property(TARGET jpeg APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/libjpeg.map)
  set_property(TARGET turbojpeg APPEND PROPERTY LINK_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/turbojpeg.map)
endif()

install(TARGETS jpeg jpeg-static turbojpeg turbojpeg-static EXPORT vioTargets
  ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(FILES ${VIO_JPEGLIB_GEN}/jconfig.h jmorecfg.h jpeglib.h jerror.h
  turbojpeg.h DESTINATION include)
