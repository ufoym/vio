# Test definitions reused from libjpeg-turbo 3.2's CMakeLists.txt (the
# "enable_testing()" section): identical commands and expected MD5 sums.
# Local adaptations (marked "vio:") select vio's build variants, programs and
# floating-point expectations.  Test names are libjpeg-turbo's.  See
# tests/third_party/libjpeg-turbo/LICENSE.md.
#
# Inputs: TEST_LIBTYPES (variant names), VIO_SUFFIX_<variant> (executable
# suffix), VIO_TOOL_<program> (vio's program standing in for libjpeg-turbo's
# cjpeg/djpeg/jpegtran/example), LJT_SOURCE_DIR, WITH_TURBOJPEG,
# WITH_ARITH_ENC, WITH_ARITH_DEC.

# vio: the float DCT/IDCT kernels perform explicit fused multiply-adds at the
# points where Clang contracts libjpeg-turbo's C code, so results always match
# libjpeg-turbo built by Clang with floating point expression contraction.
set(DEFAULT_FLOATTEST8 fp-contract)
set(DEFAULT_FLOATTEST12 fp-contract)

# vio: test images come from the libjpeg-turbo submodule.
set(TESTIMAGES ${LJT_SOURCE_DIR}/testimages)
if(CMAKE_CROSSCOMPILING)
  file(RELATIVE_PATH TESTIMAGES ${CMAKE_CURRENT_BINARY_DIR} ${TESTIMAGES})
endif()

# The output of the floating point DCT/IDCT algorithms differs depending on the
# type of floating point math used, so the FLOATTEST8 and FLOATTEST12 CMake
# variables must be set in order to tell the testing system which floating
# point results it should expect:
#
# sse = validate against the expected results from the libjpeg-turbo SSE SIMD
#       extensions
# no-fp-contract = validate against the expected results from the C code when
#                  floating point expression contraction is disabled (the
#                  default with Clang 13 and earlier, when building for
#                  platforms that lack fused multiply-add [FMA] instructions,
#                  or when passing -ffp-contract=off to GCC or Clang)
# fp-contract = validate against the expected results from the C code when
#               floating point expression contraction is enabled (the default
#               with Clang 14 and later, with GCC when building for platforms
#               that have fused multiply-add [FMA] instructions, or when
#               passing -ffp-contract=fast to GCC or -ffp-contract=on to Clang)
# 387 = validate against the expected results from the C code when the 387 FPU
#       is being used for floating point math (which is generally the default
#       with x86 compilers)
# msvc = validate against the expected results from the C code when compiled
#        with a 32-bit version of Visual C++ and /arch:IA32 (which was the
#        default in Visual C++ 2010 and earlier)

# This causes FLOATTEST8 to reset to the default value if WITH_SIMD has
# changed.
if(DEFINED WITH_SIMD_INT AND NOT WITH_SIMD EQUAL WITH_SIMD_INT)
  set(FORCE_FLOATTEST8 "FORCE")
endif()
set(WITH_SIMD_INT ${WITH_SIMD} CACHE INTERNAL "")
set(FLOATTEST8 ${DEFAULT_FLOATTEST8} CACHE STRING
  "The type of floating point math used by the 8-bit-per-sample floating point DCT/IDCT algorithms.  This tells the testing system which numerical results it should expect from those tests.  [sse = ${CMAKE_PROJECT_NAME} x86/x86-64 SIMD extensions, no-fp-contract = generic FPU with floating point expression contraction disabled, fp-contract = generic FPU with floating point expression contraction enabled, 387 = 387 FPU, msvc = 32-bit Visual Studio + 387 FPU] (default = ${DEFAULT_FLOATTEST8})"
  ${FORCE_FLOATTEST8})
message(STATUS "FLOATTEST8 = ${FLOATTEST8}")

if(FLOATTEST8)
  string(TOUPPER ${FLOATTEST8} FLOATTEST8_UC)
  string(REGEX REPLACE "-" "_" FLOATTEST8_UC ${FLOATTEST8_UC})
  string(TOLOWER ${FLOATTEST8} FLOATTEST8)
  if(NOT FLOATTEST8 STREQUAL "sse" AND
    NOT FLOATTEST8 STREQUAL "no-fp-contract" AND
    NOT FLOATTEST8 STREQUAL "fp-contract" AND NOT FLOATTEST8 STREQUAL "387" AND
    NOT FLOATTEST8 STREQUAL "msvc")
    message(FATAL_ERROR "\"${FLOATTEST8}\" is not a valid value for FLOATTEST8.")
  endif()
endif()

set(FLOATTEST12 ${DEFAULT_FLOATTEST12} CACHE STRING
  "The type of floating point math used by the 12-bit-per-sample floating point DCT/IDCT algorithms.  This tells the testing system which numerical results it should expect from those tests.  [sse = ${CMAKE_PROJECT_NAME} x86/x86-64 SIMD extensions, no-fp-contract = generic FPU with floating point expression contraction disabled, fp-contract = generic FPU with floating point expression contraction enabled, 387 = 387 FPU, msvc = 32-bit Visual Studio + 387 FPU] (default = ${DEFAULT_FLOATTEST12})")
message(STATUS "FLOATTEST12 = ${FLOATTEST12}")

if(FLOATTEST12)
  string(TOUPPER ${FLOATTEST12} FLOATTEST12_UC)
  string(REGEX REPLACE "-" "_" FLOATTEST12_UC ${FLOATTEST12_UC})
  string(TOLOWER ${FLOATTEST12} FLOATTEST12)
  if(NOT FLOATTEST12 STREQUAL "sse" AND
    NOT FLOATTEST12 STREQUAL "no-fp-contract" AND
    NOT FLOATTEST12 STREQUAL "fp-contract" AND
    NOT FLOATTEST12 STREQUAL "387" AND
    NOT FLOATTEST12 STREQUAL "msvc")
    message(FATAL_ERROR "\"${FLOATTEST12}\" is not a valid value for FLOATTEST12.")
  endif()
endif()

# vio: the programs a libtype runs are vio's stand-ins (VIO_TOOL_<program>)
# unless VIO_TOOL_<program>_<libtype> names others.
foreach(t cjpeg djpeg jpegtran example tjunittest tjbench bmpsizetest)
  set(VIO_DEFAULT_TOOL_${t} ${VIO_TOOL_${t}})
endforeach()

foreach(libtype ${TEST_LIBTYPES})
  # vio: each "libtype" is a build variant of the vio executables, or of
  # libjpeg-turbo's programs built against vio's libraries.
  set(suffix ${VIO_SUFFIX_${libtype}})
  foreach(t cjpeg djpeg jpegtran example tjunittest tjbench bmpsizetest)
    if(DEFINED VIO_TOOL_${t}_${libtype})
      set(VIO_TOOL_${t} ${VIO_TOOL_${t}_${libtype}})
    else()
      set(VIO_TOOL_${t} ${VIO_DEFAULT_TOOL_${t}})
    endif()
  endforeach()
  if(WITH_TURBOJPEG)
    add_test(NAME tjunittest-${libtype}
      COMMAND ${VIO_TOOL_tjunittest}${suffix})
    add_test(NAME tjunittest-${libtype}-alloc
      COMMAND ${VIO_TOOL_tjunittest}${suffix} -alloc)
    add_test(NAME tjunittest-${libtype}-yuv
      COMMAND ${VIO_TOOL_tjunittest}${suffix} -yuv)
    add_test(NAME tjunittest-${libtype}-yuv-alloc
      COMMAND ${VIO_TOOL_tjunittest}${suffix} -yuv -alloc)
    add_test(NAME tjunittest-${libtype}-yuv-nopad
      COMMAND ${VIO_TOOL_tjunittest}${suffix} -yuv -noyuvpad)
    add_test(NAME tjunittest-${libtype}-lossless
      COMMAND ${VIO_TOOL_tjunittest}${suffix} -lossless)
    add_test(NAME tjunittest-${libtype}-lossless-alloc
      COMMAND ${VIO_TOOL_tjunittest}${suffix} -lossless -alloc)
    add_test(NAME tjunittest-${libtype}-bmp
      COMMAND ${VIO_TOOL_tjunittest}${suffix} -bmp)
    add_test(NAME tjunittest12-${libtype}
      COMMAND ${VIO_TOOL_tjunittest}${suffix} -precision 12)
    add_test(NAME tjunittest12-${libtype}-alloc
      COMMAND ${VIO_TOOL_tjunittest}${suffix} -precision 12 -alloc)
    foreach(sample_bits 2 3 4 5 6 7 9 10 11 12 13 14 15 16)
      add_test(NAME tjunittest${sample_bits}-${libtype}-lossless
        COMMAND ${VIO_TOOL_tjunittest}${suffix} -precision ${sample_bits} -lossless)
      add_test(NAME tjunittest${sample_bits}-${libtype}-lossless-alloc
        COMMAND ${VIO_TOOL_tjunittest}${suffix} -precision ${sample_bits} -lossless -alloc)
      add_test(NAME tjunittest${sample_bits}-${libtype}-bmp
        COMMAND ${VIO_TOOL_tjunittest}${suffix} -precision ${sample_bits} -bmp)
    endforeach()

    foreach(sample_bits 8 12)

      if(sample_bits EQUAL 12)
        set(tjbench tjbench12)
        set(testout testout12${suffix})
        set(SRCIMG ${TESTIMAGES}/monkey16.png)
        set(ext png)
        set(EXT PNG)

        set(MD5_PNG_GRAY_TILE 999a5fd03e8bb1e0b32c9ccb29cec846)
        set(MD5_PNG_420_8x8_TILE 0c921be3a229b3376f5e671928c741ff)
        set(MD5_PNG_420_16x16_TILE 5e0d2e2e69ca0d1ad97797791ff1a30b)
        set(MD5_PNG_420_32x32_TILE 4949016568cf8aecc08b1af5be822bd1)
        set(MD5_PNG_420_64x64_TILE 6df139058c430b34e44b346bc5899706)
        set(MD5_PNG_420_128x128_TILE 0b42e6aca0bd21b2c5846d34e652b2b6)
        set(MD5_PNG_420M_8x8_TILE 540bea1d82d6964dab66003ca12823e0)
        set(MD5_PNG_420M_TILE 825368d2d974eed4111f25b0854643d8)
        set(MD5_PNG_422_8x8_TILE f39d59bae49dd10a8532f4ab943f3e5a)
        set(MD5_PNG_422_16x16_TILE f4cc229c2ceefa33052d0ca98ff79855)
        set(MD5_PNG_422_32x32_TILE 2d25e57eff9e316409523740def79876)
        set(MD5_PNG_422_64x64_TILE a6b531c0899653baff5e20a2484a8bfa)
        set(MD5_PNG_422_128x128_TILE 579ead8169a4e7144d5c3b624f4d1cc3)
        set(MD5_PNG_422M_8x8_TILE 70920ea8f550524fb684cefb23dfeb4d)
        set(MD5_PNG_422M_TILE ba85405622ea0449f33ddb5d1892853f)
        set(MD5_PNG_444_TILE 0dffd24a3a64c945139960cc0f553272)
      else()
        set(tjbench tjbench)
        set(testout testout${suffix})
        set(SRCIMG ${TESTIMAGES}/testorig.ppm)
        set(ext ppm)
        set(EXT PPM)

        set(MD5_PPM_GRAY_TILE 2c3b567086e6ca0c5e6d34ad8d6f6fe8)
        set(MD5_PPM_420_8x8_TILE efca1bdf0226df01777137778cf986ec)
        set(MD5_PPM_420_16x16_TILE 8c92c7453870d9e11c6d1dec3a8c9101)
        set(MD5_PPM_420_32x32_TILE 3f7651872a95e469d1c7115f1b11ecef)
        set(MD5_PPM_420_64x64_TILE f64c71af03fdea12363b62f1a3096aab)
        set(MD5_PPM_420_128x128_TILE 5a5ef57517558c671bf5e75793588d69)
        set(MD5_PPM_420M_8x8_TILE 66bd869b315a32a00fef1a025661ce72)
        set(MD5_PPM_420M_TILE bf9ec2ab4875abb2efcce8f876fe2c2a)
        set(MD5_PPM_422_8x8_TILE c300553ce1b3b90fd414ec96b62fe988)
        set(MD5_PPM_422_16x16_TILE 6559ddb1191f5b2d3eb41081b254c4e0)
        set(MD5_PPM_422_32x32_TILE 58691797f4584c4c5ed5965a6bb9aec0)
        set(MD5_PPM_422_64x64_TILE 7f9e34942ae46af7b784f459ec133f5e)
        set(MD5_PPM_422_128x128_TILE 6afcb77580d85dd3eacb04b3c2bc7710)
        set(MD5_PPM_422M_8x8_TILE 55df1f96bcfb631aedeb940cf3f011f5)
        set(MD5_PPM_422M_TILE 6502031018c2d2f69bc6353347f8df4d)
        set(MD5_PPM_444_TILE 87bd58005eec73f0f313c8e38d0d793c)
      endif()

      # Test compressing from/decompressing to an arbitrary subregion of a
      # larger image buffer
      add_test(NAME ${tjbench}-${libtype}-tile-cp
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${SRCIMG} ${testout}_tile.${ext})
      add_test(NAME ${tjbench}-${libtype}-tile
        COMMAND ${VIO_TOOL_tjbench}${suffix} ${testout}_tile.${ext} 95
          -precision ${sample_bits} -rgb -quiet -tile -benchtime 0.01
          -warmup 0)
      set_tests_properties(${tjbench}-${libtype}-tile
        PROPERTIES DEPENDS ${tjbench}-${libtype}-tile-cp)

      foreach(tile 8 16 32 64 128)
        add_test(NAME ${tjbench}-${libtype}-tile-gray-${tile}x${tile}-cmp
          COMMAND ${VIO_TOOL_md5cmp} ${MD5_${EXT}_GRAY_TILE}
            ${testout}_tile_GRAY_Q95_${tile}x${tile}.${ext})
        foreach(subsamp 420 422)
          add_test(NAME ${tjbench}-${libtype}-tile-${subsamp}-${tile}x${tile}-cmp
            COMMAND ${VIO_TOOL_md5cmp} ${MD5_${EXT}_${subsamp}_${tile}x${tile}_TILE}
              ${testout}_tile_${subsamp}_Q95_${tile}x${tile}.${ext})
        endforeach()
        add_test(NAME ${tjbench}-${libtype}-tile-444-${tile}x${tile}-cmp
          COMMAND ${VIO_TOOL_md5cmp} ${MD5_${EXT}_444_TILE}
            ${testout}_tile_444_Q95_${tile}x${tile}.${ext})
        foreach(subsamp gray 420 422 444)
          set_tests_properties(
            ${tjbench}-${libtype}-tile-${subsamp}-${tile}x${tile}-cmp
            PROPERTIES DEPENDS ${tjbench}-${libtype}-tile)
        endforeach()
      endforeach()

      add_test(NAME ${tjbench}-${libtype}-tilem-cp
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${SRCIMG} ${testout}_tilem.${ext})
      add_test(NAME ${tjbench}-${libtype}-tilem
        COMMAND ${VIO_TOOL_tjbench}${suffix} ${testout}_tilem.${ext} 95
          -precision ${sample_bits} -rgb -fastupsample -quiet -tile
          -benchtime 0.01 -warmup 0)
      set_tests_properties(${tjbench}-${libtype}-tilem
        PROPERTIES DEPENDS ${tjbench}-${libtype}-tilem-cp)

      add_test(NAME ${tjbench}-${libtype}-tile-420m-8x8-cmp
        COMMAND ${VIO_TOOL_md5cmp} ${MD5_${EXT}_420M_8x8_TILE}
          ${testout}_tilem_420_Q95_8x8.${ext})
      add_test(NAME ${tjbench}-${libtype}-tile-422m-8x8-cmp
        COMMAND ${VIO_TOOL_md5cmp} ${MD5_${EXT}_422M_8x8_TILE}
          ${testout}_tilem_422_Q95_8x8.${ext})
      foreach(tile 16 32 64 128)
        foreach(subsamp 420 422)
          add_test(NAME ${tjbench}-${libtype}-tile-${subsamp}m-${tile}x${tile}-cmp
            COMMAND ${VIO_TOOL_md5cmp} ${MD5_${EXT}_${subsamp}M_TILE}
              ${testout}_tilem_${subsamp}_Q95_${tile}x${tile}.${ext})
        endforeach()
      endforeach()
      foreach(tile 8 16 32 64 128)
        foreach(subsamp 420 422)
          set_tests_properties(
            ${tjbench}-${libtype}-tile-${subsamp}m-${tile}x${tile}-cmp
            PROPERTIES DEPENDS ${tjbench}-${libtype}-tilem)
        endforeach()
      endforeach()

    endforeach()

  endif()

  # vio: when the program was built (it needs fmemopen()).
  if(UNIX AND TARGET ${VIO_TOOL_bmpsizetest}${suffix})
    add_test(NAME bmpsizetest-${libtype} COMMAND ${VIO_TOOL_bmpsizetest}${suffix})
  endif()

  # These tests are carefully crafted to provide full coverage of as many of
  # the underlying algorithms as possible (including all of the
  # SIMD-accelerated ones.)

  macro(add_bittest PROG NAME ARGS OUTFILE INFILE MD5SUM)
    if(${PROG} STREQUAL "cjpeg16")
      set(ACTUAL_ARGS "${ARGS};-precision;16")
    elseif(${PROG} STREQUAL "cjpeg12")
      set(ACTUAL_ARGS "${ARGS};-precision;12")
    else()
      set(ACTUAL_ARGS ${ARGS})
    endif()
    string(REGEX REPLACE "16" "" ACTUAL_PROG ${PROG})
    string(REGEX REPLACE "12" "" ACTUAL_PROG ${ACTUAL_PROG})
    # vio: run vio's program standing in for libjpeg-turbo's.
    set(ACTUAL_PROG ${VIO_TOOL_${ACTUAL_PROG}})
    add_test(NAME ${PROG}-${libtype}-${NAME}
      COMMAND ${ACTUAL_PROG}${suffix} ${ACTUAL_ARGS} -outfile ${OUTFILE}
        ${INFILE})
    add_test(NAME ${PROG}-${libtype}-${NAME}-cmp
      COMMAND ${VIO_TOOL_md5cmp} ${MD5SUM} ${OUTFILE})
    set_tests_properties(${PROG}-${libtype}-${NAME}-cmp PROPERTIES
      DEPENDS ${PROG}-${libtype}-${NAME})
    if(${ARGC} GREATER 6)
      set(DEPENDS ${ARGN})
      set_tests_properties(${PROG}-${libtype}-${NAME} PROPERTIES
        DEPENDS ${DEPENDS})
    endif()
  endmacro()

  set(MD5_JPEG_LOSSLESS fdf975c44ddcbc86f144eb58f2417c97)
  set(MD5_PNG_LOSSLESS ae6de70fa4537456317631df3dab37c8)

  # Lossless (all arguments other than -lossless and -restart should have no
  # effect)
  add_bittest(cjpeg16 lossless
    "-lossless;4;-restart;1;-quality;1;-grayscale;-optimize;-dct;float;-smooth;100;-baseline;-qslots;1,0,0;-sample;1x2,3x4,2x1"
    testout16_lossless.jpg ${TESTIMAGES}/monkey16.png ${MD5_JPEG_LOSSLESS})
  add_bittest(djpeg16 lossless
    "-fast;-scale;1/8;-dct;float;-dither;none;-nosmooth;-onepass;-png"
    testout16_lossless.png testout16_lossless.jpg
    ${MD5_PNG_LOSSLESS} cjpeg16-${libtype}-lossless)

  foreach(sample_bits 8 12)

    if(sample_bits EQUAL 12)
      set(cjpeg cjpeg12)
      set(djpeg djpeg12)
      set(jpegtran jpegtran12)
      set(testout testout12${suffix})
      set(SRCIMG ${TESTIMAGES}/monkey16.png)
      set(ext png)
      set(EXT PNG)
      set(JPEGIMG ${TESTIMAGES}/monkey12.jpg)

      set(MD5_JPEG_RGB_ISLOW 412da084cd06bece1822dc7b92f01e1a)
      set(MD5_JPEG_RGB_ISLOW2 47d05e783674661f1c2c749e230d98b7)
      set(MD5_PNG_RGB_ISLOW ae3a3285a63b15f905951f1c6a8de6c2)
      set(MD5_JPEG_422_IFAST_OPT fd9d6d81531e33661de10495a0069001)
      set(MD5_PNG_422_IFAST 310d2ac448193a3d13b31832cee87b11)
      set(MD5_JPEG_440_ISLOW 3f8c31d8fef17c4ad92e5603116211c9)
      set(MD5_PNG_440_ISLOW 3e849499bbe903c0352ea4316ef9e136)
      set(MD5_PNG_422M_IFAST e41b8710266ef758d1b8c30ba322608d)
      set(MD5_JPEG_420_IFAST_Q100_PROG dafebba3616229772fcb157cc90efbc1)
      set(MD5_PNG_420_Q100_IFAST 4c4502a15a96bb3b8275b6d628475e66)
      set(MD5_PNG_420M_Q100_IFAST f63b8b627bc5e3df7fdee6c44824affe)
      set(MD5_JPEG_GRAY_ISLOW 4bd46c0943f1ba911437700b0a98bd56)
      set(MD5_PNG_GRAY_ISLOW 3d5c96b48535dcc5123bae2ec7ece092)
      set(MD5_PNG_GRAY_ISLOW_RGB 512ffa30491bdf5a58ae0204caee55c4)
      set(MD5_JPEG_420S_ISLOW_OPT 2b7de3331421c2e41cf6cd9b5ddcf8e0)

      set(MD5_JPEG_3x2_FLOAT_PROG_SSE 2fe2ef00578fd4d5f7c105e37090780c)
      set(MD5_PNG_3x2_FLOAT_SSE 68a9b2522b8d30bed42c2104028957cd)
      set(MD5_JPEG_3x2_FLOAT_PROG_NO_FP_CONTRACT
        ${MD5_JPEG_3x2_FLOAT_PROG_SSE})
      set(MD5_PNG_3x2_FLOAT_NO_FP_CONTRACT ${MD5_PNG_3x2_FLOAT_SSE})
      set(MD5_JPEG_3x2_FLOAT_PROG_FP_CONTRACT ${MD5_JPEG_3x2_FLOAT_PROG_SSE})
      set(MD5_PNG_3x2_FLOAT_FP_CONTRACT ${MD5_PNG_3x2_FLOAT_SSE})
      set(MD5_JPEG_3x2_FLOAT_PROG_387 5d203a595f448cf19ce86c9bb2c52dfe)
      set(MD5_PNG_3x2_FLOAT_387 05e50fa2fb929d335a144813cd97cd82)
      set(MD5_JPEG_3x2_FLOAT_PROG_MSVC af7f873ea14136271cc21ddbc4bee46c)
      set(MD5_PNG_3x2_FLOAT_MSVC 1790c395e3fbc8c60dbe503a8db3ffe7)

      set(MD5_JPEG_3x2_IFAST_PROG eb1ba1cb5656025dc2ba13d81bcd5a2d)
      set(MD5_PNG_3x2_IFAST def6c47bbfa0e6b797f8f326f6ef82c5)
      set(MD5_PNG_420M_ISLOW_2_1 9812e72849d2b18a25c3d0ee4a718cd6)
      set(MD5_PNG_420M_ISLOW_15_8 7b1bcb4ff1b3e123d98bde20d2bac816)
      set(MD5_PNG_420M_ISLOW_13_8 583f6d5a240ae7abdaae07a03c0de506)
      set(MD5_PNG_420M_ISLOW_11_8 b812cd1f78757b062582c7ddb5d4f5e2)
      set(MD5_PNG_420M_ISLOW_9_8 050747dff00afd2779f595ddd883ccbc)
      set(MD5_PNG_420M_ISLOW_7_8 66dce85a76a034f472e9e33c8cbad349)
      set(MD5_PNG_420M_ISLOW_3_4 a3c324540ef157fc43937516975af91c)
      set(MD5_PNG_420M_ISLOW_5_8 5edcf6ec475809766a639c02ace4323e)
      set(MD5_PNG_420M_ISLOW_1_2 131d3fe37905472aed58a2a73a146e3b)
      set(MD5_PNG_420M_ISLOW_3_8 75080741e2d9c33ebdaa73f1554b024b)
      set(MD5_PNG_420M_ISLOW_1_4 d76d67d6663df0aad9b9c839491ea64d)
      set(MD5_PNG_420M_ISLOW_1_8 37f958113630dce326d6bc26d7cd00fe)
      set(MD5_JPEG_LOSSLESS dacb841a16cc2e1140042d989bf6457e)
      set(MD5_PNG_LOSSLESS 60811cac46a04912c30ff1cad74c9f23)
      set(MD5_PNG_420_ISLOW_SKIP15_31 1c1737561a26e0104248654931b88307)
      set(MD5_PNG_420_ISLOW_PROG_CROP62x62_71_71
        5d46774035a03fc00d90fd7b107eca6f)
      set(MD5_PNG_444_ISLOW_SKIP1_6 f52ca3ef309bdcbab981e13f2b11cb0a)
      set(MD5_PNG_444_ISLOW_PROG_CROP98x98_13_13
        24c7a6cf12f7bfa57e12e7b0238add66)
      set(MD5_JPEG_CROP b816363e572435067ec23bfa390761b4)

      set(MD5_JPEG_EXAMPLE_COMPRESS 5e502da0c3c0f957a58c536f31e973dc)
      set(MD5_PPM_EXAMPLE_DECOMPRESS 49aac0119b87c4deb70e9a7daf151253)
    else()
      set(cjpeg cjpeg)
      set(djpeg djpeg)
      set(jpegtran jpegtran)
      set(testout testout${suffix})
      set(SRCIMG ${TESTIMAGES}/testorig.ppm)
      set(ext ppm)
      set(EXT PPM)
      set(JPEGIMG ${TESTIMAGES}/testorig.jpg)

      set(MD5_JPEG_RGB_ISLOW 1d44a406f61da743b5fd31c0a9abdca3)
      set(MD5_JPEG_RGB_ISLOW2 b811e5ad807beb9384e51ef6a47f715d)
      set(MD5_PPM_RGB_ISLOW 00a257f5393fef8821f2b88ac7421291)
      set(MD5_BMP_RGB_ISLOW_565 f07d2e75073e4bb10f6c6f4d36e2e3be)
      set(MD5_BMP_RGB_ISLOW_565D 4cfa0928ef3e6bb626d7728c924cfda4)
      set(MD5_JPEG_422_IFAST_OPT 2540287b79d913f91665e660303ab2c8)
      set(MD5_PPM_422_IFAST 35bd6b3f833bad23de82acea847129fa)
      set(MD5_JPEG_440_ISLOW 538bc02bd4b4658fd85de6ece6cbeda6)
      set(MD5_PPM_440_ISLOW 11e7eab7ef7ef3276934bb7e7b6bb377)
      set(MD5_PPM_422M_IFAST 8dbc65323d62cca7c91ba02dd1cfa81d)
      set(MD5_BMP_422M_IFAST_565 3294bd4d9a1f2b3d08ea6020d0db7065)
      set(MD5_BMP_422M_IFAST_565D da98c9c7b6039511be4a79a878a9abc1)
      set(MD5_JPEG_420_IFAST_Q100_PROG 0ba15f9dab81a703505f835f9dbbac6d)
      set(MD5_PPM_420_Q100_IFAST 5a732542015c278ff43635e473a8a294)
      set(MD5_PPM_420M_Q100_IFAST ff692ee9323a3b424894862557c092f1)
      set(MD5_JPEG_GRAY_ISLOW 72b51f894b8f4a10b3ee3066770aa38d)
      set(MD5_PPM_GRAY_ISLOW 8d3596c56eace32f205deccc229aa5ed)
      set(MD5_PPM_GRAY_ISLOW_RGB 116424ac07b79e5e801f00508eab48ec)
      set(MD5_BMP_GRAY_ISLOW_565 12f78118e56a2f48b966f792fedf23cc)
      set(MD5_BMP_GRAY_ISLOW_565D bdbbd616441a24354c98553df5dc82db)
      set(MD5_JPEG_420S_ISLOW_OPT 388708217ac46273ca33086b22827ed8)

      set(MD5_JPEG_3x2_FLOAT_PROG_SSE 343e3f8caf8af5986ebaf0bdc13b5c71)
      set(MD5_PPM_3x2_FLOAT_SSE 1a75f36e5904d6fc3a85a43da9ad89bb)
      set(MD5_JPEG_3x2_FLOAT_PROG_NO_FP_CONTRACT
        9bca803d2042bd1eb03819e2bf92b3e5)
      set(MD5_PPM_3x2_FLOAT_NO_FP_CONTRACT f6bfab038438ed8f5522fbd33595dcdc)
      set(MD5_JPEG_3x2_FLOAT_PROG_FP_CONTRACT
        ${MD5_JPEG_3x2_FLOAT_PROG_NO_FP_CONTRACT})
      set(MD5_PPM_3x2_FLOAT_FP_CONTRACT ${MD5_PPM_3x2_FLOAT_NO_FP_CONTRACT})
      set(MD5_JPEG_3x2_FLOAT_PROG_387 1657664a410e0822c924b54f6f65e6e9)
      set(MD5_PPM_3x2_FLOAT_387 cb0a1f027f3d2917c902b5640214e025)
      set(MD5_JPEG_3x2_FLOAT_PROG_MSVC 7999ce9cd0ee9b6c7043b7351ab7639d)
      set(MD5_PPM_3x2_FLOAT_MSVC 28cdc448a6b75e97892f0e0f8d4b21f3)

      set(MD5_JPEG_3x2_IFAST_PROG 1ee5d2c1a77f2da495f993c8c7cceca5)
      set(MD5_PPM_3x2_IFAST fd283664b3b49127984af0a7f118fccd)
      set(MD5_JPEG_420_ISLOW_ARI e986fb0a637a8d833d96e8a6d6d84ea1)
      set(MD5_JPEG_444_ISLOW_PROGARI 0a8f1c8f66e113c3cf635df0a475a617)
      set(MD5_PPM_420M_IFAST_ARI 57251da28a35b46eecb7177d82d10e0e)
      set(MD5_JPEG_420_ISLOW 9a68f56bc76e466aa7e52f415d0f4a5f)
      set(MD5_PPM_420M_ISLOW_2_1 9f9de8c0612f8d06869b960b05abf9c9)
      set(MD5_PPM_420M_ISLOW_15_8 b6875bc070720b899566cc06459b63b7)
      set(MD5_PPM_420M_ISLOW_13_8 bc3452573c8152f6ae552939ee19f82f)
      set(MD5_PPM_420M_ISLOW_11_8 d8cc73c0aaacd4556569b59437ba00a5)
      set(MD5_PPM_420M_ISLOW_9_8 d25e61bc7eac0002f5b393aa223747b6)
      set(MD5_PPM_420M_ISLOW_7_8 ddb564b7c74a09494016d6cd7502a946)
      set(MD5_PPM_420M_ISLOW_3_4 8ed8e68808c3fbc4ea764fc9d2968646)
      set(MD5_PPM_420M_ISLOW_5_8 a3363274999da2366a024efae6d16c9b)
      set(MD5_PPM_420M_ISLOW_1_2 e692a315cea26b988c8e8b29a5dbcd81)
      set(MD5_PPM_420M_ISLOW_3_8 79eca9175652ced755155c90e785a996)
      set(MD5_PPM_420M_ISLOW_1_4 79cd778f8bf1a117690052cacdd54eca)
      set(MD5_PPM_420M_ISLOW_1_8 391b3d4aca640c8567d6f8745eb2142f)
      set(MD5_BMP_420_ISLOW_256 4980185e3776e89bd931736e1cddeee6)
      set(MD5_BMP_420_ISLOW_565 bf9d13e16c4923b92e1faa604d7922cb)
      set(MD5_BMP_420_ISLOW_565D 6bde71526acc44bcff76f696df8638d2)
      set(MD5_BMP_420M_ISLOW_565 8dc0185245353cfa32ad97027342216f)
      set(MD5_BMP_420M_ISLOW_565D ce034037d212bc403330df6f915c161b)
      set(MD5_JPEG_LOSSLESS fc777b82d42d835ae1282ba1ee87c209)
      set(MD5_PPM_LOSSLESS 64072f1dbdc5b3a187777788604971a5)
      set(MD5_PPM_420_ISLOW_SKIP15_31 c4c65c1e43d7275cd50328a61e6534f0)
      set(MD5_PPM_420_ISLOW_ARI_SKIP16_139 087c6b123db16ac00cb88c5b590bb74a)
      set(MD5_PPM_420_ISLOW_PROG_CROP62x62_71_71
        26eb36ccc7d1f0cb80cdabb0ac8b5d99)
      set(MD5_PPM_420_ISLOW_ARI_CROP53x53_4_4 886c6775af22370257122f8b16207e6d)
      set(MD5_PPM_444_ISLOW_SKIP1_6 5606f86874cf26b8fcee1117a0a436a6)
      set(MD5_PPM_444_ISLOW_PROG_CROP98x98_13_13
        db87dc7ce26bcdc7a6b56239ce2b9d6c)
      set(MD5_PPM_444_ISLOW_ARI_CROP37x37_0_0 cb57b32bd6d03e35432362f7bf184b6d)
      set(MD5_JPEG_CROP b4197f377e621c4e9b1d20471432610d)

      set(MD5_JPEG_EXAMPLE_COMPRESS 95d4d72e2ef127332654c2599afb47bf)
      set(MD5_PPM_EXAMPLE_DECOMPRESS dea1d7bbc37e39adf628342c86096641)
    endif()

    # CC: null  SAMP: fullsize  FDCT: islow  ENT: huff
    add_bittest(${cjpeg} rgb-islow "-rgb;-dct;int;-icc;${TESTIMAGES}/test1.icc"
      ${testout}_rgb_islow.jpg ${SRCIMG}
      ${MD5_JPEG_RGB_ISLOW})

    # CC: null  SAMP: fullsize  IDCT: islow  ENT: huff
    add_bittest(${djpeg} rgb-islow
      "-dct;int;-${ext};-icc;${testout}_rgb_islow.icc"
      ${testout}_rgb_islow.${ext} ${testout}_rgb_islow.jpg
      ${MD5_${EXT}_RGB_ISLOW} ${cjpeg}-${libtype}-rgb-islow)

    add_test(NAME ${djpeg}-${libtype}-rgb-islow-icc-cmp
      COMMAND ${VIO_TOOL_md5cmp} b06a39d730129122e85c1363ed1bbc9e ${testout}_rgb_islow.icc)
    set_tests_properties(${djpeg}-${libtype}-rgb-islow-icc-cmp PROPERTIES
      DEPENDS ${djpeg}-${libtype}-rgb-islow)

    add_bittest(${jpegtran} icc "-copy;all;-icc;${TESTIMAGES}/test3.icc"
      ${testout}_rgb_islow2.jpg ${testout}_rgb_islow.jpg
      ${MD5_JPEG_RGB_ISLOW2} ${cjpeg}-${libtype}-rgb-islow)

    if(sample_bits EQUAL 8)
      # CC: RGB->RGB565  SAMP: fullsize  IDCT: islow  ENT: huff
      add_bittest(${djpeg} rgb-islow-565 "-dct;int;-rgb565;-dither;none;-bmp"
        ${testout}_rgb_islow_565.bmp ${testout}_rgb_islow.jpg
        ${MD5_BMP_RGB_ISLOW_565} ${cjpeg}-${libtype}-rgb-islow)

      # CC: RGB->RGB565 (dithered)  SAMP: fullsize  IDCT: islow  ENT: huff
      add_bittest(${djpeg} rgb-islow-565D "-dct;int;-rgb565;-bmp"
        ${testout}_rgb_islow_565D.bmp ${testout}_rgb_islow.jpg
        ${MD5_BMP_RGB_ISLOW_565D} ${cjpeg}-${libtype}-rgb-islow)
    endif()

    # CC: RGB->YCC  SAMP: fullsize/h2v1  FDCT: ifast  ENT: 2-pass huff
    add_bittest(${cjpeg} 422-ifast-opt "-sample;2x1;-dct;fast;-opt"
      ${testout}_422_ifast_opt.jpg ${SRCIMG}
      ${MD5_JPEG_422_IFAST_OPT})

    # CC: YCC->RGB  SAMP: fullsize/h2v1 fancy  IDCT: ifast  ENT: huff
    add_bittest(${djpeg} 422-ifast "-dct;fast;-${ext}"
      ${testout}_422_ifast.${ext} ${testout}_422_ifast_opt.jpg
      ${MD5_${EXT}_422_IFAST} ${cjpeg}-${libtype}-422-ifast-opt)

    # CC: RGB->YCC  SAMP: fullsize/h1v2  FDCT: islow  ENT: huff
    add_bittest(${cjpeg} 440-islow "-sample;1x2;-dct;int"
      ${testout}_440_islow.jpg ${SRCIMG}
      ${MD5_JPEG_440_ISLOW})

    # CC: YCC->RGB  SAMP: fullsize/h1v2 fancy  IDCT: islow  ENT: huff
    add_bittest(${djpeg} 440-islow "-dct;int;-${ext}"
      ${testout}_440_islow.${ext} ${testout}_440_islow.jpg
      ${MD5_${EXT}_440_ISLOW} ${cjpeg}-${libtype}-440-islow)

    # CC: YCC->RGB  SAMP: h2v1 merged  IDCT: ifast  ENT: huff
    add_bittest(${djpeg} 422m-ifast "-dct;fast;-nosmooth;-${ext}"
      ${testout}_422m_ifast.${ext} ${testout}_422_ifast_opt.jpg
      ${MD5_${EXT}_422M_IFAST} ${cjpeg}-${libtype}-422-ifast-opt)

    if(sample_bits EQUAL 8)
      # CC: YCC->RGB565  SAMP: h2v1 merged  IDCT: ifast  ENT: huff
      add_bittest(${djpeg} 422m-ifast-565
        "-dct;int;-nosmooth;-rgb565;-dither;none;-bmp"
        ${testout}_422m_ifast_565.bmp ${testout}_422_ifast_opt.jpg
        ${MD5_BMP_422M_IFAST_565} ${cjpeg}-${libtype}-422-ifast-opt)

      # CC: YCC->RGB565 (dithered)  SAMP: h2v1 merged  IDCT: ifast  ENT: huff
      add_bittest(${djpeg} 422m-ifast-565D "-dct;int;-nosmooth;-rgb565;-bmp"
        ${testout}_422m_ifast_565D.bmp ${testout}_422_ifast_opt.jpg
        ${MD5_BMP_422M_IFAST_565D} ${cjpeg}-${libtype}-422-ifast-opt)
    endif()

    # CC: RGB->YCC  SAMP: fullsize/h2v2  FDCT: ifast  ENT: prog huff
    add_bittest(${cjpeg} 420-q100-ifast-prog
      "-sample;2x2;-quality;100;-dct;fast;-scans;${TESTIMAGES}/test.scan"
      ${testout}_420_q100_ifast_prog.jpg ${SRCIMG}
      ${MD5_JPEG_420_IFAST_Q100_PROG})

    # CC: YCC->RGB  SAMP: fullsize/h2v2 fancy  IDCT: ifast  ENT: prog huff
    add_bittest(${djpeg} 420-q100-ifast-prog "-dct;fast;-${ext}"
      ${testout}_420_q100_ifast.${ext} ${testout}_420_q100_ifast_prog.jpg
      ${MD5_${EXT}_420_Q100_IFAST} ${cjpeg}-${libtype}-420-q100-ifast-prog)

    # CC: YCC->RGB  SAMP: h2v2 merged  IDCT: ifast  ENT: prog huff
    add_bittest(${djpeg} 420m-q100-ifast-prog "-dct;fast;-nosmooth;-${ext}"
      ${testout}_420m_q100_ifast.${ext} ${testout}_420_q100_ifast_prog.jpg
      ${MD5_${EXT}_420M_Q100_IFAST} ${cjpeg}-${libtype}-420-q100-ifast-prog)

    # CC: RGB->Gray  SAMP: fullsize  FDCT: islow  ENT: huff
    add_bittest(${cjpeg} gray-islow "-gray;-dct;int;-noicc"
      ${testout}_gray_islow.jpg ${SRCIMG}
      ${MD5_JPEG_GRAY_ISLOW})

    # CC: Gray->Gray  SAMP: fullsize  IDCT: islow  ENT: huff
    add_bittest(${djpeg} gray-islow "-dct;int;-${ext}"
      ${testout}_gray_islow.${ext} ${testout}_gray_islow.jpg
      ${MD5_${EXT}_GRAY_ISLOW} ${cjpeg}-${libtype}-gray-islow)

    # CC: Gray->RGB  SAMP: fullsize  IDCT: islow  ENT: huff
    add_bittest(${djpeg} gray-islow-rgb "-dct;int;-rgb;-${ext}"
      ${testout}_gray_islow_rgb.${ext} ${testout}_gray_islow.jpg
      ${MD5_${EXT}_GRAY_ISLOW_RGB} ${cjpeg}-${libtype}-gray-islow)

    if(sample_bits EQUAL 8)
      # CC: Gray->RGB565  SAMP: fullsize  IDCT: islow  ENT: huff
      add_bittest(${djpeg} gray-islow-565 "-dct;int;-rgb565;-dither;none;-bmp"
        ${testout}_gray_islow_565.bmp ${testout}_gray_islow.jpg
        ${MD5_BMP_GRAY_ISLOW_565} ${cjpeg}-${libtype}-gray-islow)

      # CC: Gray->RGB565 (dithered)  SAMP: fullsize  IDCT: islow  ENT: huff
      add_bittest(${djpeg} gray-islow-565D "-dct;int;-rgb565;-bmp"
        ${testout}_gray_islow_565D.bmp ${testout}_gray_islow.jpg
        ${MD5_BMP_GRAY_ISLOW_565D} ${cjpeg}-${libtype}-gray-islow)
    endif()

    # CC: RGB->YCC  SAMP: fullsize smooth/h2v2 smooth  FDCT: islow
    # ENT: 2-pass huff
    add_bittest(${cjpeg} 420s-islow-opt "-sample;2x2;-smooth;1;-dct;int;-opt"
      ${testout}_420s_islow_opt.jpg ${SRCIMG}
      ${MD5_JPEG_420S_ISLOW_OPT})

    # vio: a build variant may use different floating-point kernels (e.g.
    # the x86 SSE ones), hence different expected results.
    set(FLOATTEST_UC ${FLOATTEST${sample_bits}_UC})
    if(DEFINED VIO_FLOATTEST${sample_bits}_${libtype})
      string(TOUPPER ${VIO_FLOATTEST${sample_bits}_${libtype}} FLOATTEST_UC)
      string(REGEX REPLACE "-" "_" FLOATTEST_UC ${FLOATTEST_UC})
    endif()
    if(FLOATTEST${sample_bits})
      # CC: RGB->YCC  SAMP: fullsize/int  FDCT: float  ENT: prog huff
      add_bittest(${cjpeg} 3x2-float-prog "-sample;3x2;-dct;float;-prog"
        ${testout}_3x2_float_prog.jpg ${SRCIMG}
        ${MD5_JPEG_3x2_FLOAT_PROG_${FLOATTEST_UC}})

      # CC: YCC->RGB  SAMP: fullsize/int  IDCT: float  ENT: prog huff
      add_bittest(${djpeg} 3x2-float-prog "-dct;float;-${ext}"
        ${testout}_3x2_float.${ext} ${testout}_3x2_float_prog.jpg
        ${MD5_${EXT}_3x2_FLOAT_${FLOATTEST_UC}}
        ${cjpeg}-${libtype}-3x2-float-prog)
    endif()

    # CC: RGB->YCC  SAMP: fullsize/int  FDCT: ifast  ENT: prog huff
    add_bittest(${cjpeg} 3x2-ifast-prog "-sample;3x2;-dct;fast;-prog"
      ${testout}_3x2_ifast_prog.jpg ${SRCIMG}
      ${MD5_JPEG_3x2_IFAST_PROG})

    # CC: YCC->RGB  SAMP: fullsize/int  IDCT: ifast  ENT: prog huff
    add_bittest(${djpeg} 3x2-ifast-prog "-dct;fast;-${ext}"
      ${testout}_3x2_ifast.${ext} ${testout}_3x2_ifast_prog.jpg
      ${MD5_${EXT}_3x2_IFAST} ${cjpeg}-${libtype}-3x2-ifast-prog)

    if(WITH_ARITH_ENC AND sample_bits EQUAL 8)
      # CC: YCC->RGB  SAMP: fullsize/h2v2  FDCT: islow  ENT: arith
      add_bittest(${cjpeg} 420-islow-ari "-dct;int;-arithmetic"
        ${testout}_420_islow_ari.jpg ${SRCIMG}
        ${MD5_JPEG_420_ISLOW_ARI})

      add_bittest(${jpegtran} 420-islow-ari "-arithmetic"
        ${testout}_420_islow_ari2.jpg ${TESTIMAGES}/testimgint.jpg
        ${MD5_JPEG_420_ISLOW_ARI})

      # CC: YCC->RGB  SAMP: fullsize  FDCT: islow  ENT: prog arith
      add_bittest(${cjpeg} 444-islow-progari
        "-sample;1x1;-dct;int;-prog;-arithmetic"
        ${testout}_444_islow_progari.jpg ${SRCIMG}
        ${MD5_JPEG_444_ISLOW_PROGARI})
    endif()

    if(WITH_ARITH_DEC AND sample_bits EQUAL 8)
      # CC: RGB->YCC  SAMP: h2v2 merged  IDCT: ifast  ENT: arith
      add_bittest(${djpeg} 420m-ifast-ari "-fast;-skip;1,20;-${ext}"
        ${testout}_420m_ifast_ari.${ext} ${TESTIMAGES}/testimgari.jpg
        ${MD5_${EXT}_420M_IFAST_ARI})

      add_bittest(${jpegtran} 420-islow ""
        ${testout}_420_islow.jpg ${TESTIMAGES}/testimgari.jpg
        ${MD5_JPEG_420_ISLOW})
    endif()

    # 2/1--   CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 16x16 islow  ENT: huff
    # 15/8--  CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 15x15 islow  ENT: huff
    # 13/8--  CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 13x13 islow  ENT: huff
    # 11/8--  CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 11x11 islow  ENT: huff
    # 9/8--   CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 9x9 islow  ENT: huff
    # 7/8--   CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 7x7 islow/14x14 islow
    #         ENT: huff
    # 3/4--   CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 6x6 islow/12x12 islow
    #         ENT: huff
    # 5/8--   CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 5x5 islow/10x10 islow
    #         ENT: huff
    # 1/2--   CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 4x4 islow/8x8 islow
    #         ENT: huff
    # 3/8--   CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 3x3 islow/6x6 islow
    #         ENT: huff
    # 1/4--   CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 2x2 islow/4x4 islow
    #         ENT: huff
    # 1/8--   CC: YCC->RGB  SAMP: h2v2 merged  IDCT: 1x1 islow/2x2 islow
    #         ENT: huff
    foreach(scale 2_1 15_8 13_8 11_8 9_8 7_8 3_4 5_8 1_2 3_8 1_4 1_8)
      string(REGEX REPLACE "_" "/" scalearg ${scale})
      add_bittest(${djpeg} 420m-islow-${scale}
        "-dct;int;-scale;${scalearg};-nosmooth;-${ext}"
        ${testout}_420m_islow_${scale}.${ext} ${JPEGIMG}
        ${MD5_${EXT}_420M_ISLOW_${scale}})
    endforeach()

    if(sample_bits EQUAL 8)
      # CC: YCC->RGB (dithered)  SAMP: h2v2 fancy  IDCT: islow  ENT: huff
      add_bittest(${djpeg} 420-islow-256 "-dct;int;-colors;256;-bmp"
        ${testout}_420_islow_256.bmp ${JPEGIMG}
        ${MD5_BMP_420_ISLOW_256})

      # CC: YCC->RGB565  SAMP: h2v2 fancy  IDCT: islow  ENT: huff
      add_bittest(${djpeg} 420-islow-565 "-dct;int;-rgb565;-dither;none;-bmp"
        ${testout}_420_islow_565.bmp ${JPEGIMG}
        ${MD5_BMP_420_ISLOW_565})

      # CC: YCC->RGB565 (dithered)  SAMP: h2v2 fancy  IDCT: islow  ENT: huff
      add_bittest(${djpeg} 420-islow-565D "-dct;int;-rgb565;-bmp"
        ${testout}_420_islow_565D.bmp ${JPEGIMG}
        ${MD5_BMP_420_ISLOW_565D})

      # CC: YCC->RGB565  SAMP: h2v2 merged  IDCT: islow  ENT: huff
      add_bittest(${djpeg} 420m-islow-565
        "-dct;int;-nosmooth;-rgb565;-dither;none;-bmp"
        ${testout}_420m_islow_565.bmp ${JPEGIMG}
        ${MD5_BMP_420M_ISLOW_565})

      # CC: YCC->RGB565 (dithered)  SAMP: h2v2 merged  IDCT: islow  ENT: huff
      add_bittest(${djpeg} 420m-islow-565D "-dct;int;-nosmooth;-rgb565;-bmp"
        ${testout}_420m_islow_565D.bmp ${JPEGIMG}
        ${MD5_BMP_420M_ISLOW_565D})
    endif()

    # Lossless (all arguments other than -lossless and -restart should have no
    # effect)
    add_bittest(${cjpeg} lossless
      "-lossless;4;-restart;1;-quality;1;-grayscale;-optimize;-dct;float;-smooth;100;-baseline;-qslots;1,0,0;-sample;1x2,3x4,2x1"
      ${testout}_lossless.jpg ${SRCIMG}
      ${MD5_JPEG_LOSSLESS})
    add_bittest(${djpeg} lossless
      "-fast;-scale;1/8;-dct;float;-dither;none;-nosmooth;-onepass;-${ext}"
      ${testout}_lossless.${ext} ${testout}_lossless.jpg
      ${MD5_${EXT}_LOSSLESS} ${cjpeg}-${libtype}-lossless)

    # Partial decode tests.  These tests are designed to cover all of the
    # possible code paths in jpeg_skip_scanlines().

    # Context rows: Yes  Intra-iMCU row: Yes  iMCU row prefetch: No
    # ENT: huff
    add_bittest(${djpeg} 420-islow-skip15_31 "-dct;int;-skip;15,31;-${ext}"
      ${testout}_420_islow_skip15,31.${ext} ${JPEGIMG}
      ${MD5_${EXT}_420_ISLOW_SKIP15_31})

    # Context rows: Yes  Intra-iMCU row: No   iMCU row prefetch: Yes
    # ENT: arith
    if(WITH_ARITH_DEC AND sample_bits EQUAL 8)
      add_bittest(${djpeg} 420-islow-ari-skip16_139
        "-dct;int;-skip;16,139;-${ext}"
        ${testout}_420_islow_ari_skip16,139.${ext} ${TESTIMAGES}/testimgari.jpg
        ${MD5_${EXT}_420_ISLOW_ARI_SKIP16_139})
    endif()

    # Context rows: Yes  Intra-iMCU row: No   iMCU row prefetch: No
    # ENT: prog huff
    add_test(NAME ${cjpeg}-${libtype}-420-islow-prog
      COMMAND ${VIO_TOOL_cjpeg}${suffix} -dct int -prog -precision ${sample_bits}
        -outfile ${testout}_420_islow_prog.jpg ${SRCIMG})
    add_bittest(${djpeg} 420-islow-prog-crop62x62_71_71
      "-dct;int;-crop;62x62+71+71;-${ext}"
      ${testout}_420_islow_prog_crop62x62,71,71.${ext}
      ${testout}_420_islow_prog.jpg
      ${MD5_${EXT}_420_ISLOW_PROG_CROP62x62_71_71}
      ${cjpeg}-${libtype}-420-islow-prog)

    # Context rows: Yes  Intra-iMCU row: No   iMCU row prefetch: No
    # ENT: arith
    if(WITH_ARITH_DEC AND sample_bits EQUAL 8)
      add_bittest(${djpeg} 420-islow-ari-crop53x53_4_4
        "-dct;int;-crop;53x53+4+4;-${ext}"
        ${testout}_420_islow_ari_crop53x53,4,4.${ext}
        ${TESTIMAGES}/testimgari.jpg
        ${MD5_${EXT}_420_ISLOW_ARI_CROP53x53_4_4})
    endif()

    # Context rows: No   Intra-iMCU row: Yes  ENT: huff
    add_test(NAME ${cjpeg}-${libtype}-444-islow
      COMMAND ${VIO_TOOL_cjpeg}${suffix} -dct int -sample 1x1 -precision ${sample_bits}
        -outfile ${testout}_444_islow.jpg ${SRCIMG})
    add_bittest(${djpeg} 444-islow-skip1_6 "-dct;int;-skip;1,6;-${ext}"
      ${testout}_444_islow_skip1,6.${ext} ${testout}_444_islow.jpg
      ${MD5_${EXT}_444_ISLOW_SKIP1_6} ${cjpeg}-${libtype}-444-islow)

    # Context rows: No   Intra-iMCU row: No   ENT: prog huff
    add_test(NAME ${cjpeg}-${libtype}-444-islow-prog
      COMMAND ${VIO_TOOL_cjpeg}${suffix} -dct int -prog -precision ${sample_bits}
        -sample 1x1 -outfile ${testout}_444_islow_prog.jpg ${SRCIMG})
    add_bittest(${djpeg} 444-islow-prog-crop98x98_13_13
      "-dct;int;-crop;98x98+13+13;-${ext}"
      ${testout}_444_islow_prog_crop98x98,13,13.${ext}
      ${testout}_444_islow_prog.jpg
      ${MD5_${EXT}_444_ISLOW_PROG_CROP98x98_13_13}
      ${cjpeg}-${libtype}-444-islow-prog)

    # Context rows: No   Intra-iMCU row: No   ENT: arith
    if(WITH_ARITH_ENC AND sample_bits EQUAL 8)
      add_test(NAME ${cjpeg}-${libtype}-444-islow-ari
        COMMAND ${VIO_TOOL_cjpeg}${suffix} -dct int -arithmetic -sample 1x1
          -precision ${sample_bits} -outfile ${testout}_444_islow_ari.jpg
          ${SRCIMG})
      if(WITH_ARITH_DEC)
        add_bittest(${djpeg} 444-islow-ari-crop37x37_0_0
          "-dct;int;-crop;37x37+0+0;-${ext}"
          ${testout}_444_islow_ari_crop37x37,0,0.${ext}
          ${testout}_444_islow_ari.jpg
          ${MD5_${EXT}_444_ISLOW_ARI_CROP37x37_0_0}
          ${cjpeg}-${libtype}-444-islow-ari)
      endif()
    endif()

    add_bittest(${jpegtran} crop "-crop;120x90+20+50;-transpose;-perfect"
      ${testout}_crop.jpg ${JPEGIMG} ${MD5_JPEG_CROP})

    unset(EXAMPLE_12BIT_ARG)
    if(sample_bits EQUAL 12)
      set(EXAMPLE_12BIT_ARG "-precision;12")
    endif()

    add_test(NAME example-${sample_bits}bit-${libtype}-compress
      COMMAND ${VIO_TOOL_example}${suffix} compress -q 95 ${EXAMPLE_12BIT_ARG}
        ${testout}-example.jpg)
    add_test(NAME example-${sample_bits}bit-${libtype}-compress-cmp
      COMMAND ${VIO_TOOL_md5cmp} ${MD5_JPEG_EXAMPLE_COMPRESS} ${testout}-example.jpg)
    set_tests_properties(example-${sample_bits}bit-${libtype}-compress-cmp
      PROPERTIES DEPENDS example-${sample_bits}bit-${libtype}-compress)

    add_test(NAME example-${sample_bits}bit-${libtype}-decompress
      COMMAND ${VIO_TOOL_example}${suffix} decompress ${EXAMPLE_12BIT_ARG}
        ${JPEGIMG} ${testout}-example.ppm)
    add_test(NAME example-${sample_bits}bit-${libtype}-decompress-cmp
      COMMAND ${VIO_TOOL_md5cmp} ${MD5_PPM_EXAMPLE_DECOMPRESS} ${testout}-example.ppm)
    set_tests_properties(example-${sample_bits}bit-${libtype}-decompress-cmp
      PROPERTIES DEPENDS example-${sample_bits}bit-${libtype}-decompress)

  endforeach()

endforeach()
