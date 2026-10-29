# The PNG programs: png-test, which png/tests.cmake runs, one per variant,
# and png-golden.
#
# Inputs: LIBPNG_DIR, VIO_VARIANTS.

foreach(variant ${VIO_VARIANTS})
  set(sfx ${VIO_SUFFIX_${variant}})
  add_executable(png-test${sfx} png/png_test.cpp)
  target_link_libraries(png-test${sfx} PRIVATE vio)
  target_compile_definitions(png-test${sfx} PRIVATE ${VIO_DEFS_${variant}})
endforeach()

# png-golden: regenerates png/expected.txt from the libpng submodule.
option(VIO_PNG_GOLDEN "Build png-golden (needs zlib) to regenerate png/expected.txt" OFF)
if(VIO_PNG_GOLDEN)
  find_package(ZLIB REQUIRED)
  set(PNG_SHARED OFF CACHE BOOL "" FORCE)
  set(PNG_TESTS OFF CACHE BOOL "" FORCE)
  set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
  add_subdirectory(${LIBPNG_DIR} ${CMAKE_CURRENT_BINARY_DIR}/libpng EXCLUDE_FROM_ALL)
  add_executable(png-golden png/png_golden.cpp)
  target_include_directories(png-golden PRIVATE png ${LIBPNG_DIR} ${CMAKE_CURRENT_BINARY_DIR}/libpng)
  target_link_libraries(png-golden PRIVATE vio png_static ZLIB::ZLIB)
endif()
