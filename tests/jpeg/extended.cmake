# libjpeg-turbo's script-based tests -- its tjtest, tjtest12, tjtest16,
# tjexampletest, croptest and indexedcolortest targets -- as ctest tests
# (label "extended"), run on libjpeg-turbo's programs built against vio's
# shared libraries.  The scripts need bash; croptest and indexedcolortest
# also need ImageMagick ("magick").
#
# Inputs: LJT_SOURCE_DIR, LJT_DIR (the copied program sources).

option(VIO_TEST_EXTENDED
  "Add libjpeg-turbo's script-based tests (label extended; about 20 minutes)" OFF)
find_program(BASH bash)
find_program(MAGICK magick)
if(NOT VIO_TEST_EXTENDED OR NOT UNIX OR NOT BASH)
  return()
endif()

# The scripts' EXEDIR: the programs under their libjpeg-turbo names, plus
# test/ with md5cmp, md5sum and the configured scripts.
set(LJT_EXEDIR ${CMAKE_CURRENT_BINARY_DIR}/ljt-shared)
file(MAKE_DIRECTORY ${LJT_EXEDIR}/test)

add_executable(wrjpgcom ${LJT_DIR}/wrjpgcom.c)
add_executable(md5-sum ${LJT_DIR}/md5/md5sum.c ${LJT_DIR}/md5/md5.c
  ${LJT_DIR}/md5/md5hl.c)
foreach(t wrjpgcom md5-sum)
  vio_ljt_program(${t})
endforeach()

macro(ljt_exe_link target name)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink $<TARGET_FILE:${target}>
      ${LJT_EXEDIR}/${name})
endmacro()
foreach(p cjpeg djpeg jpegtran tjbench tjcomp tjdecomp tjtran)
  ljt_exe_link(${p}-shared ${p})
endforeach()
ljt_exe_link(wrjpgcom wrjpgcom)
ljt_exe_link(md5-cmp test/md5cmp)
ljt_exe_link(md5-sum test/md5sum)

set(CMAKE_SOURCE_DIR_SAVED ${CMAKE_SOURCE_DIR})
set(CMAKE_BINARY_DIR_SAVED ${CMAKE_BINARY_DIR})
set(CMAKE_SOURCE_DIR ${LJT_SOURCE_DIR})
set(CMAKE_BINARY_DIR ${LJT_EXEDIR})
set(Java_JAVA_EXECUTABLE "")
set(WITH_JNA "")
foreach(script croptest indexedcolortest tjbenchtest tjcomptest tjdecomptest
               tjtrantest)
  configure_file(${LJT_SOURCE_DIR}/test/${script}.in ${LJT_EXEDIR}/test/${script}
    @ONLY FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
    GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
set(CMAKE_SOURCE_DIR ${CMAKE_SOURCE_DIR_SAVED})
set(CMAKE_BINARY_DIR ${CMAKE_BINARY_DIR_SAVED})

set(LJT_SCRIPTS ${LJT_SOURCE_DIR}/cmakescripts)
macro(ljt_script_test name)
  add_test(NAME ${name} COMMAND ${ARGN} WORKING_DIRECTORY ${LJT_EXEDIR})
  set_tests_properties(${name} PROPERTIES LABELS extended TIMEOUT 7200)
endmacro()
ljt_script_test(tjtest ${CMAKE_COMMAND} -DPRECISION=8
  -P ${LJT_SCRIPTS}/tjbenchtest.cmake)
ljt_script_test(tjtest12 ${CMAKE_COMMAND} -DPRECISION=12
  -P ${LJT_SCRIPTS}/tjbenchtest.cmake)
foreach(p 2 3 4 5 6 7 9 10 11 13 14 15 16)
  ljt_script_test(tjtest16-${p} ${CMAKE_COMMAND} -DPRECISION=${p}
    -P ${LJT_SCRIPTS}/tjbenchtest.cmake)
endforeach()
# tjexampletest: its scripts, run with bash (one lacks a valid "#!" line)
foreach(script tjcomptest tjdecomptest tjtrantest)
  ljt_script_test(tjexampletest-${script} ${BASH} ${LJT_EXEDIR}/test/${script})
endforeach()
if(MAGICK)
  ljt_script_test(croptest ${BASH} ${LJT_EXEDIR}/test/croptest)
  ljt_script_test(indexedcolortest ${BASH} ${LJT_EXEDIR}/test/indexedcolortest)
endif()
