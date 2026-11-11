# Add unit tests here (history database, folder parser, ...).
add_executable(test_smoke test_smoke.c)
add_test(NAME smoke COMMAND test_smoke)
