add_executable(test_smoke test_smoke.c)
add_test(NAME smoke COMMAND test_smoke)

add_executable(test_history test_history.c)
target_link_libraries(test_history PRIVATE thc_core)
add_test(NAME history COMMAND test_history WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(test_settings test_settings.c)
target_link_libraries(test_settings PRIVATE thc_core)
add_test(NAME settings COMMAND test_settings WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

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
