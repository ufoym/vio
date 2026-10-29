# Tests of bind/ beyond what the libjpeg-turbo and libpng programs check.

# c-test: the C interface (bind/c/vio/vio.h).
add_executable(c-test bind/c/vio/c_test.c bind/c/vio/vio_impl.cpp)
target_link_libraries(c-test PRIVATE vio)
add_test(NAME c-test COMMAND c-test ${LJT_SOURCE_DIR}/testimages
  ${CMAKE_CURRENT_BINARY_DIR})

# jpeglib-test-shared/-static: the jpeglib.h API (bind/c/jpeglib), with the
# settings of libjpeg-turbo's programs (jpeg/build.cmake).
foreach(kind ${VIO_COMPAT_LIBTYPES})
  set(sfx -${kind})
  ljt_program(jpeglib-test jpeg${VIO_LIB_${kind}} bind/c/jpeglib/jpeglib_test.c)
  target_link_libraries(jpeglib-test${sfx} PRIVATE turbojpeg${VIO_LIB_${kind}})
  add_test(NAME jpeglib-test-${kind}
    COMMAND jpeglib-test${sfx} ${LJT_SOURCE_DIR}/testimages)
endforeach()
