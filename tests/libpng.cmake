# libpng's tests (tests/libpng/CMakeLists.txt, tests/pngtest-all), adapted to
# vio: the same test names and images, run by png-test (tools/png_test.cpp)
# instead of libpng's programs.  Decoded images are compared with
# libpng-expected.txt, the samples libpng 1.6.59 decodes.
#
#   pngtest, pngtest-png-3   decode and re-encode pngtest.png / png-3 images
#   pngtest-crashers         the corrupt and odd files of pngtest-all
#   pngvalid-*               generated images of every format, written with
#                            random filters, interlaced or not
#   pngstest-*               contrib/testpngs by gamma and alpha type
#   pngunknown-discard       unknown chunks
#   pngimage-quick/-full     PngSuite (-full: also the interlaced set and
#                            re-encoding)

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

foreach(variant ${VIO_VARIANTS})
  set(sfx ${VIO_SUFFIX_${variant}})
  set(png_test $<TARGET_FILE:png-test${sfx}>)
  set(check ${png_test} check --roundtrip ${LIBPNG_EXPECTED} ${LIBPNG_DIR})

  add_test(NAME pngtest${sfx} COMMAND ${check} pngtest.png)
  add_test(NAME pngtest-png-3${sfx} COMMAND ${check} ${TEST_PNG3_PNGS})
  add_test(NAME pngtest-crashers${sfx} COMMAND ${check} ${CRASHER_PNGS})

  add_test(NAME pngvalid-standard${sfx} COMMAND ${png_test} valid standard)
  add_test(NAME pngvalid-interlace-standard${sfx} COMMAND ${png_test} valid interlace)
  add_test(NAME pngvalid-size${sfx} COMMAND ${png_test} valid size)

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
      add_test(NAME pngstest-${gamma_type}-${alpha_type}${sfx} COMMAND ${check} ${files})
    endforeach()
  endforeach()

  add_test(NAME pngunknown-discard${sfx}
    COMMAND ${png_test} unknown ${LIBPNG_DIR}/pngtest.png)

  add_test(NAME pngimage-quick${sfx}
    COMMAND ${png_test} check ${LIBPNG_EXPECTED} ${LIBPNG_DIR} ${PNGSUITE_PNGS})
  add_test(NAME pngimage-full${sfx}
    COMMAND ${check} ${PNGSUITE_PNGS} ${PNGSUITE_INTERLACED_PNGS})
endforeach()
