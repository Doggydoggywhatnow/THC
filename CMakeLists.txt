cmake_minimum_required(VERSION 3.20)
project(THC VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies (enable as each milestone lands):
#   find_package(SDL3 REQUIRED)
#   find_package(SQLite3 REQUIRED)
#   libmpv via pkg-config (mpv)
#   lib-Peep: https://github.com/Doggydoggywhatnow/lib-Peep

add_executable(thc src/main.c)
target_include_directories(thc PRIVATE include)
target_compile_definitions(thc PRIVATE THC_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(thc PRIVATE /W4)
else()
  target_compile_options(thc PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_subdirectory(tests)
