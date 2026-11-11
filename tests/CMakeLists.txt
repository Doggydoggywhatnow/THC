add_executable(test_smoke test_smoke.c)
add_test(NAME smoke COMMAND test_smoke)

add_executable(test_history test_history.c)
target_link_libraries(test_history PRIVATE thc_core)
add_test(NAME history COMMAND test_history WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
