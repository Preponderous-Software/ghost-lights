cmake_minimum_required(VERSION 3.20)
project(ghost_lights VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GHOST_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(GHOST_BUILD_TESTS "Build the test suite" ON)
option(GHOST_WEB "Build the browser game (Emscripten + Strata's WebGL 2 backend)" OFF)

# Emscripten: the rules and tests build as they are and the tests run under
# Node; GHOST_WEB adds the browser game. Strata compiles C++ exceptions as
# wasm exceptions, and everything in one .wasm must match.
if(EMSCRIPTEN)
  cmake_minimum_required(VERSION 3.24)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>")
  add_link_options(-fwasm-exceptions -sALLOW_MEMORY_GROWTH=1)
endif()
if(GHOST_WEB AND NOT EMSCRIPTEN)
  message(FATAL_ERROR "GHOST_WEB needs Emscripten (emcmake cmake ...)")
endif()

function(ghost_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(GHOST_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(GHOST_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# The rules: the marsh, the ghosts, the wisps, the levels. No engine, no GPU.
# Never fuse multiply-adds: the rules give the same results on every compiler.
add_library(ghost_sim STATIC src/sim/world.cpp src/sim/levels.cpp src/sim/autopilot.cpp)
target_include_directories(ghost_sim PUBLIC src)
ghost_warnings(ghost_sim)
if(MSVC)
  target_compile_options(ghost_sim PRIVATE /fp:precise)
else()
  target_compile_options(ghost_sim PRIVATE -ffp-contract=off)
endif()

if(GHOST_BUILD_TESTS)
  enable_testing()
  add_executable(test_rules tests/test_rules.cpp)
  target_link_libraries(test_rules PRIVATE ghost_sim)
  ghost_warnings(test_rules)
  add_test(NAME rules COMMAND test_rules)
endif()

# ---------------------------------------------------------------- the game on Strata
# Strata is fetched at GHOST_STRATA_REF (the v0.7.0 tag, ABI 1.12), or taken
# from a local checkout with -DFETCHCONTENT_SOURCE_DIR_STRATA=<path>.
# emcmake cmake -S . -B build-web -DGHOST_WEB=ON: the page is
# ghost-lights.html + .js + .wasm.
if(GHOST_WEB)
  include(FetchContent)
  set(GHOST_STRATA_REF "20827c3d67e71c1f942c29e7cf8adfa4801188a8" CACHE STRING "Strata git ref (a full commit SHA)")
  set(STRATA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(STRATA_WITH_SIM OFF CACHE BOOL "" FORCE)
  set(STRATA_WITH_USD OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(strata
    GIT_REPOSITORY https://github.com/Preponderous-Software/strata.git
    GIT_TAG ${GHOST_STRATA_REF})
  FetchContent_MakeAvailable(strata)

  add_library(ghost_play STATIC src/play/scene.cpp)
  target_include_directories(ghost_play PUBLIC src)
  target_link_libraries(ghost_play PUBLIC ghost_sim strata::static)
  ghost_warnings(ghost_play)

  add_executable(ghost-web app/web_main.cpp)
  target_link_libraries(ghost-web PRIVATE ghost_play)
  ghost_warnings(ghost-web)
  target_compile_definitions(ghost-web PRIVATE GHOST_VERSION="${PROJECT_VERSION}")
  set_target_properties(ghost-web PROPERTIES OUTPUT_NAME ghost-lights SUFFIX ".html")
  target_link_options(ghost-web PRIVATE
    -sSTACK_SIZE=1MB
    "-sEXPORTED_FUNCTIONS=_main,_gl_move,_gl_pointer,_gl_flare,_gl_next,_gl_restart,_gl_level,_gl_solve,_gl_resize,_gl_status,_gl_autopilot"
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
  set_property(TARGET ghost-web APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/strata-kit/strata-loader.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/strata-boot.js")
  # The loading shell (web/strata-kit, vendored from Strata) and its bridge go
  # inline into the page, with the download's size (web/strata_page.cmake).
  add_custom_command(TARGET ghost-web POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DHTML=$<TARGET_FILE:ghost-web>"
            "-DWASM=$<TARGET_FILE_DIR:ghost-web>/$<TARGET_FILE_BASE_NAME:ghost-web>.wasm"
            "-DWEB=${CMAKE_CURRENT_SOURCE_DIR}/web" -P "${CMAKE_CURRENT_SOURCE_DIR}/web/strata_page.cmake"
    VERBATIM)
endif()
