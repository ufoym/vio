# libpng's tests (tests/libpng/tests, libpng18 branch), adapted to vio: the
# same test names and images, run by png-test (tools/png_test.cpp) instead
# of libpng's programs.  Results are compared with libpng-expected.txt, what
# libpng produces for the same cases (written by tools/png_golden.cpp; build
# it with -DVIO_PNG_GOLDEN=ON).
#
#   pngtest-all              decode and re-encode pngtest.png, the png-3 and
#                            corrupt images (pngtest)
#   pnggetset                every chunk value written and read back
#   pngimage-quick/-full     read transformations on PngSuite (and
#                            contrib/testpngs), reversible ones written back
#   pngstest-*               the format-conversion API on contrib/testpngs
#                            by gamma and alpha, with large/negative strides
#   pngunknown-*             unknown chunk rules on pngtest.png
#   pngvalid-*               generated images: every format, size and
#                            interlacing, progressive reading, read
#                            transformations and gamma processing

set(LIBPNG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libpng)
set(LIBPNG_EXPECTED ${CMAKE_CURRENT_SOURCE_DIR}/libpng-expected.txt)

file(GLOB PNGSUITE_PNGS RELATIVE ${LIBPNG_DIR} ${LIBPNG_DIR}/contrib/pngsuite/*.png)
file(GLOB PNGSUITE_INTERLACED_PNGS RELATIVE ${LIBPNG_DIR}
  ${LIBPNG_DIR}/contrib/pngsuite/interlaced/*.png)
file(GLOB TEST_PNGS RELATIVE ${LIBPNG_DIR} ${LIBPNG_DIR}/contrib/testpngs/*.png)
file(GLOB TEST_PNG3_PNGS RELATIVE ${LIBPNG_DIR} ${LIBPNG_DIR}/contrib/testpngs/png-3/*.png)
file(GLOB CRASHER_PNGS RELATIVE ${LIBPNG_DIR}
  ${LIBPNG_DIR}/contrib/testpngs/crashers/*.png ${LIBPNG_DIR}/contrib/testpngs/badpal/*.png)
foreach(list PNGSUITE_PNGS PNGSUITE_INTERLACED_PNGS TEST_PNGS TEST_PNG3_PNGS CRASHER_PNGS)
  list(SORT ${list})
endforeach()

set(STRIDE_PNGS
  contrib/testpngs/gray-1.png contrib/testpngs/gray-16-linear.png
  contrib/testpngs/rgb-8-linear.png contrib/testpngs/rgb-16-linear.png
  contrib/testpngs/palette-8-tRNS.png contrib/testpngs/gray-alpha-8-linear.png
  contrib/testpngs/gray-alpha-16-linear.png contrib/testpngs/rgb-alpha-8-1.8.png
  contrib/testpngs/rgb-alpha-8-sRGB.png contrib/testpngs/rgb-alpha-16-linear.png)

foreach(variant ${VIO_VARIANTS})
  set(sfx ${VIO_SUFFIX_${variant}})
  set(t png-test${sfx})  # (a target name: CMake adds the cross-compiling emulator)
  set(e ${LIBPNG_EXPECTED})

  add_test(NAME pngtest-all${sfx}
    COMMAND ${t} check --roundtrip ${e} ${LIBPNG_DIR} pngtest.png ${TEST_PNG3_PNGS} ${CRASHER_PNGS})
  add_test(NAME pnggetset${sfx} COMMAND ${t} getset)
  add_test(NAME pngimage-quick${sfx} COMMAND ${t} xform ${e} ${LIBPNG_DIR} ${PNGSUITE_PNGS})
  add_test(NAME pngimage-full${sfx}
    COMMAND ${t} xform ${e} ${LIBPNG_DIR} ${PNGSUITE_PNGS} ${TEST_PNGS})
  add_test(NAME pngimage-interlaced${sfx}
    COMMAND ${t} check --roundtrip ${e} ${LIBPNG_DIR} ${PNGSUITE_PNGS} ${PNGSUITE_INTERLACED_PNGS})

  foreach(gamma_type 1.8 linear none sRGB)
    foreach(alpha_type none alpha)
      set(files)
      foreach(f ${TEST_PNGS})
        string(REGEX MATCH "-linear[-.]" is_linear "${f}")
        string(REGEX MATCH "-sRGB[-.]" is_srgb "${f}")
        string(REGEX MATCH "-1.8[-.]" is_g18 "${f}")
        string(REGEX MATCH "-alpha-" is_alpha "${f}")
        if(is_linear)
          set(g linear)
        elseif(is_srgb)
          set(g sRGB)
        elseif(is_g18)
          set(g 1.8)
        else()
          set(g none)
        endif()
        if(is_alpha)
          set(a alpha)
        else()
          set(a none)
        endif()
        if(g STREQUAL gamma_type AND a STREQUAL alpha_type)
          list(APPEND files ${f})
        endif()
      endforeach()
      if(alpha_type STREQUAL "alpha")
        set(name pngstest-${gamma_type}-alpha)
      else()
        set(name pngstest-${gamma_type})
      endif()
      add_test(NAME ${name}${sfx} COMMAND ${t} format ${e} ${LIBPNG_DIR} ${files})
    endforeach()
  endforeach()
  add_test(NAME pngstest-large-stride${sfx}
    COMMAND ${t} format ${e} ${LIBPNG_DIR} --stride-extra 33000 contrib/testpngs/rgb-alpha-16-linear.png)
  add_test(NAME pngstest-negative-stride${sfx}
    COMMAND ${t} format ${e} ${LIBPNG_DIR} --negative-stride ${STRIDE_PNGS})
  add_test(NAME pngstest-negative-stride-extra${sfx}
    COMMAND ${t} format ${e} ${LIBPNG_DIR} --negative-stride --stride-extra 7 ${STRIDE_PNGS})

  foreach(rule IDAT discard if-safe sAPI sTER save vpAg)
    add_test(NAME pngunknown-${rule}${sfx} COMMAND ${t} unknown ${e} ${LIBPNG_DIR}/pngtest.png ${rule})
  endforeach()

  add_test(NAME pngvalid-standard${sfx} COMMAND ${t} valid ${e} standard)
  add_test(NAME pngvalid-interlace-standard${sfx} COMMAND ${t} valid ${e} interlace)
  add_test(NAME pngvalid-size${sfx} COMMAND ${t} valid ${e} size)
  add_test(NAME pngvalid-progressive-standard${sfx} COMMAND ${t} valid ${e} standard --progressive)
  add_test(NAME pngvalid-progressive-interlace-standard${sfx}
    COMMAND ${t} valid ${e} interlace --progressive)
  add_test(NAME pngvalid-progressive-size${sfx} COMMAND ${t} valid ${e} size --progressive)
  add_test(NAME pngvalid-transform${sfx} COMMAND ${t} vxform ${e} transform)
  foreach(kind threshold transform sbit 16-to-8 background alpha-mode)
    add_test(NAME pngvalid-gamma-${kind}${sfx} COMMAND ${t} vxform ${e} gamma-${kind})
  endforeach()
  foreach(kind alpha-mode background transform)
    add_test(NAME pngvalid-gamma-expand16-${kind}${sfx} COMMAND ${t} vxform ${e} gamma-${kind}-expand16)
  endforeach()
endforeach()

# png-golden: regenerates libpng-expected.txt from the libpng submodule.
option(VIO_PNG_GOLDEN "Build png-golden (needs zlib) to regenerate libpng-expected.txt" OFF)
if(VIO_PNG_GOLDEN)
  find_package(ZLIB REQUIRED)
  set(PNG_SHARED OFF CACHE BOOL "" FORCE)
  set(PNG_TESTS OFF CACHE BOOL "" FORCE)
  set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
  add_subdirectory(${LIBPNG_DIR} ${CMAKE_CURRENT_BINARY_DIR}/libpng EXCLUDE_FROM_ALL)
  add_executable(png-golden tools/png_golden.cpp)
  target_include_directories(png-golden PRIVATE tools ${LIBPNG_DIR} ${CMAKE_CURRENT_BINARY_DIR}/libpng)
  target_link_libraries(png-golden PRIVATE vio png_static ZLIB::ZLIB)
endif()
