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

# ---------------------------------------------------------------------------
# SQLite (public domain): system copy, or the amalgamation from sqlite.org.
# ---------------------------------------------------------------------------
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
  if(NOT THC_FETCH_DEPS)
    message(FATAL_ERROR "SQLite3 not found and THC_FETCH_DEPS is OFF")
  endif()
  message(STATUS "SQLite3 not found: building the amalgamation")
  FetchContent_Declare(sqlite_amalgamation
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip)
  FetchContent_MakeAvailable(sqlite_amalgamation)
  add_library(thc_sqlite3 STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
  target_include_directories(thc_sqlite3 PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
  target_compile_definitions(thc_sqlite3 PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=1)
  if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(thc_sqlite3 PUBLIC Threads::Threads)
  endif()
  add_library(SQLite::SQLite3 ALIAS thc_sqlite3)
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
# thc_core: everything that does not need SDL (tested on its own).
# ---------------------------------------------------------------------------
add_library(thc_core STATIC
  src/fstree.c
  src/history.c
  src/library.c
  src/settings.c)
target_include_directories(thc_core PUBLIC include)
target_link_libraries(thc_core PUBLIC SQLite::SQLite3)
if(UNIX)
  target_link_libraries(thc_core PUBLIC m)
endif()
thc_warnings(thc_core)

# ---------------------------------------------------------------------------
# The THC application.
# ---------------------------------------------------------------------------
if(THC_BUILD_APP)
  # The bundled font (Atkinson Hyperlegible, SIL OFL 1.1) is compiled in.
  include(cmake/embed.cmake)
  set(FONT_C ${CMAKE_CURRENT_BINARY_DIR}/thc_fonts.c)
  embed_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/AtkinsonHyperlegible-Regular.ttf
             ${CMAKE_CURRENT_BINARY_DIR}/font_regular.c thc_font_regular)
  embed_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/AtkinsonHyperlegible-Bold.ttf
             ${CMAKE_CURRENT_BINARY_DIR}/font_bold.c thc_font_bold)

  add_executable(thc WIN32 MACOSX_BUNDLE
    src/main.c
    src/art.c
    src/chrome.c
    src/hotkeys.c
    src/mpv_dyn.c
    src/music.c
    src/portal_books.c
    src/portal_music.c
    src/portal_video.c
    src/scan.c
    src/screen_settings.c
    src/screen_setup.c
    src/third_party.c
    src/ui.c
    src/video.c
    ${CMAKE_CURRENT_BINARY_DIR}/font_regular.c
    ${CMAKE_CURRENT_BINARY_DIR}/font_bold.c)
  target_include_directories(thc PRIVATE include src third_party/stb)
  set_source_files_properties(src/third_party.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>")
  target_compile_definitions(thc PRIVATE THC_VERSION="${PROJECT_VERSION}")
  target_link_libraries(thc PRIVATE thc_core SDL3::SDL3)
  thc_warnings(thc)
endif()

enable_testing()
add_subdirectory(tests)
