cmake_minimum_required(VERSION 3.20)
project(THC VERSION 0.1.3 LANGUAGES C)

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

# ---------------------------------------------------------------------------
# lib-Peep (public domain): WAV and FLAC decoding in assembly, x86-64 (NASM)
# and ARM64 (GNU as syntax). Its codec interface also carries THC's MP3
# (dr_mp3) and Ogg Vorbis (stb_vorbis) decoders. Where it cannot be built
# (other CPUs, Intel Macs), THC plays those formats through libmpv instead.
# ---------------------------------------------------------------------------
option(THC_USE_PEEP "Use lib-Peep for music decoding" ON)
set(PEEP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lib-Peep)
set(THC_HAVE_PEEP OFF)
if(THC_BUILD_APP AND THC_USE_PEEP)
  include(CheckLanguage)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$" AND NOT APPLE)
    check_language(ASM_NASM)
    if(CMAKE_ASM_NASM_COMPILER)
      enable_language(ASM_NASM)
      set(PEEP_SRC ${PEEP_DIR}/peep.asm ${PEEP_DIR}/flac.asm ${PEEP_DIR}/wav.asm ${PEEP_DIR}/wavc.asm)
      add_library(peep STATIC ${PEEP_SRC})
      target_include_directories(peep PRIVATE ${PEEP_DIR}/)
      if(WIN32)
        set_source_files_properties(${PEEP_SRC} PROPERTIES COMPILE_DEFINITIONS WIN64)
      endif()
      set(THC_HAVE_PEEP ON)
      set(THC_PEEP_WAVC ON)
    else()
      message(WARNING "NASM not found: lib-Peep disabled, FLAC/WAV/MP3/OGG will play through libmpv")
    endif()
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$" AND NOT MSVC)
    enable_language(ASM)
    if(APPLE)
      # Mach-O: the sources' APPLE mode (underscore symbols). The compressed
      # WAV codec (wavc) does not assemble for Mach-O yet, so it is left out.
      set(PEEP_SRC)
      foreach(f peep flac wav)
        set(w ${CMAKE_CURRENT_BINARY_DIR}/peep_apple_${f}.s)
        file(WRITE ${w} ".set APPLE, 1\n.include \"${PEEP_DIR}/arm64/${f}.s\"\n")
        list(APPEND PEEP_SRC ${w})
      endforeach()
      set(THC_PEEP_WAVC OFF)
    else()
      set(PEEP_SRC ${PEEP_DIR}/arm64/peep.s ${PEEP_DIR}/arm64/flac.s ${PEEP_DIR}/arm64/wav.s
                   ${PEEP_DIR}/arm64/wavc.s)
      set(THC_PEEP_WAVC ON)
    endif()
    add_library(peep STATIC ${PEEP_SRC})
    target_include_directories(peep PRIVATE ${PEEP_DIR}/arm64)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
      target_compile_options(peep PRIVATE "-Wa,-I${PEEP_DIR}/arm64")
    endif()
    set(THC_HAVE_PEEP ON)
  else()
    message(STATUS "lib-Peep has no build for ${CMAKE_SYSTEM_PROCESSOR} here: using libmpv for music")
  endif()
endif()
message(STATUS "lib-Peep: ${THC_HAVE_PEEP}")

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
  src/mpvconf.c
  src/settings.c
  src/tags.c
  src/thumbkey.c)
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
    src/audio.c
    src/chrome.c
    src/hotkeys.c
    src/mpv_dyn.c
    src/music.c
    src/portal_books.c
    src/portal_music.c
    src/portal_video.c
    src/scan.c
    src/tagcache.c
    src/thumbgen.c
    src/thumbs.c
    src/screen_settings.c
    src/screen_setup.c
    src/third_party.c
    src/ui.c
    src/video.c
    src/player_stub.c
    ${CMAKE_CURRENT_BINARY_DIR}/font_regular.c
    ${CMAKE_CURRENT_BINARY_DIR}/font_bold.c)
  target_include_directories(thc PRIVATE include src third_party/stb)
  set_source_files_properties(src/third_party.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>")
  if(THC_HAVE_PEEP)
    target_sources(thc PRIVATE src/peep_codecs.c)
    set_source_files_properties(src/peep_codecs.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>")
    target_include_directories(thc PRIVATE third_party/lib-Peep third_party/dr_libs)
    target_link_libraries(thc PRIVATE peep)
    target_compile_definitions(thc PRIVATE THC_HAVE_PEEP=1)
    if(THC_PEEP_WAVC)
      target_compile_definitions(thc PRIVATE THC_PEEP_WAVC=1)
    endif()
  endif()
  target_compile_definitions(thc PRIVATE THC_VERSION="${PROJECT_VERSION}")
  target_link_libraries(thc PRIVATE thc_core SDL3::SDL3)
  thc_warnings(thc)
endif()

enable_testing()
add_subdirectory(tests)

# ---------------------------------------------------------------------------
# Install and Linux packages (cpack -G DEB / TGZ)
# ---------------------------------------------------------------------------
if(THC_BUILD_APP AND UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  install(TARGETS thc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES packaging/linux/thc.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES packaging/linux/thc.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
  install(FILES packaging/linux/io.github.doggydoggywhatnow.thc.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

  set(CPACK_PACKAGE_NAME thc)
  set(CPACK_PACKAGE_VENDOR "THC")
  set(CPACK_PACKAGE_CONTACT "THC <https://github.com/Doggydoggywhatnow/THC>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight player for the music and videos you already have")
  set(CPACK_PACKAGE_FILE_NAME "thc-${PROJECT_VERSION}-linux-x86_64")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_SECTION video)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libsqlite3-0")
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libmpv2 | libmpv1")
  set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Doggydoggywhatnow/THC")
  set(CPACK_GENERATOR "DEB;TGZ")
  include(CPack)
endif()
