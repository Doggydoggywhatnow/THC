add_executable(test_smoke test_smoke.c)
add_test(NAME smoke COMMAND test_smoke)

add_executable(test_history test_history.c)
target_link_libraries(test_history PRIVATE thc_core)
add_test(NAME history COMMAND test_history WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_settings test_settings.c)
target_link_libraries(test_settings PRIVATE thc_core)
add_test(NAME settings COMMAND test_settings WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_history_compat test_history_compat.c)
target_link_libraries(test_history_compat PRIVATE thc_core)
add_test(NAME history_compat COMMAND test_history_compat WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_queue test_queue.c)
target_link_libraries(test_queue PRIVATE thc_core)
add_test(NAME queue COMMAND test_queue WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_search test_search.c)
target_link_libraries(test_search PRIVATE thc_core)
add_test(NAME search COMMAND test_search)

add_executable(test_mpvconf test_mpvconf.c)
target_link_libraries(test_mpvconf PRIVATE thc_core)
add_test(NAME mpvconf COMMAND test_mpvconf)

add_executable(test_parser test_parser.c)
target_link_libraries(test_parser PRIVATE thc_core)
add_test(NAME parser COMMAND test_parser)

if(THC_HAVE_PEEP)
  add_executable(test_audio_decode test_audio_decode.c ../src/peep_codecs.c)
  target_include_directories(test_audio_decode PRIVATE ../third_party/lib-Peep ../third_party/dr_libs
                             ../third_party/stb)
  target_compile_definitions(test_audio_decode PRIVATE THC_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
  set_source_files_properties(../src/peep_codecs.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>")
  target_link_libraries(test_audio_decode PRIVATE peep)
  if(UNIX)
    target_link_libraries(test_audio_decode PRIVATE m)
  endif()
  add_test(NAME audio_decode COMMAND test_audio_decode)
endif()

add_executable(test_tags test_tags.c)
target_link_libraries(test_tags PRIVATE thc_core)
add_test(NAME tags COMMAND test_tags WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

if(THC_BUILD_APP)
  add_executable(test_mpv_resume test_mpv_resume.c ../src/video.c ../src/mpv_dyn.c)
  target_include_directories(test_mpv_resume PRIVATE ../include)
  target_compile_definitions(test_mpv_resume PRIVATE THC_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
  target_link_libraries(test_mpv_resume PRIVATE SDL3::SDL3)
  add_test(NAME mpv_resume COMMAND test_mpv_resume WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

  add_executable(test_mpv_config test_mpv_config.c ../src/video.c ../src/mpv_dyn.c ../src/mpvconf.c)
  target_include_directories(test_mpv_config PRIVATE ../include)
  target_compile_definitions(test_mpv_config PRIVATE THC_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
  target_link_libraries(test_mpv_config PRIVATE SDL3::SDL3)
  add_test(NAME mpv_config COMMAND test_mpv_config WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()

add_executable(test_thumbs_core test_thumbs.c)
target_link_libraries(test_thumbs_core PRIVATE thc_core)
if(THC_BUILD_APP)
  # the generator and the stb decoder come from the app's sources
  target_sources(test_thumbs_core PRIVATE ../src/thumbgen.c ../src/mpv_dyn.c ../src/third_party.c)
  target_include_directories(test_thumbs_core PRIVATE ../src ../third_party/stb)
  target_compile_definitions(test_thumbs_core PRIVATE THC_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
  set_source_files_properties(../src/third_party.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>")
  target_link_libraries(test_thumbs_core PRIVATE SDL3::SDL3)
  add_test(NAME thumbs COMMAND test_thumbs_core WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endif()
