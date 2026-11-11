cmake_minimum_required(VERSION 3.20)
project(THC VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(THC_BUILD_APP "Build the THC application (needs SDL3)" ON)
option(THC_FETCH_DEPS "Download SDL3 if it is not installed" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# SDL3: use an installed copy, or build it from source.
# ---------------------------------------------------------------------------
if(THC_BUILD_APP)
  find_package(SDL3 CONFIG QUIET)
  if(NOT SDL3_FOUND)
    if(NOT THC_FETCH_DEPS)
      message(FATAL_ERROR "SDL3 not found and THC_FETCH_DEPS is OFF")
    endif()
    message(STATUS "SDL3 not found: building it from source")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG release-3.4.20
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
  endif()
endif()

function(thc_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /wd4100 /wd4996)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# The THC application.
# ---------------------------------------------------------------------------
if(THC_BUILD_APP)
  add_executable(thc WIN32 MACOSX_BUNDLE
    src/main.c
    src/mpv_dyn.c
    src/video.c)
  target_include_directories(thc PRIVATE include)
  target_compile_definitions(thc PRIVATE THC_VERSION="${PROJECT_VERSION}")
  target_link_libraries(thc PRIVATE SDL3::SDL3)
  thc_warnings(thc)
endif()

enable_testing()
add_subdirectory(tests)
