cmake_minimum_required(VERSION 3.20)
project(fwg VERSION 0.1.0 LANGUAGES CXX)

# Built on its own, this is the top-level project. A sibling component in the monorepo that
# adds this folder -- add_subdirectory(../generator fwg) -- gets the library alone: no test
# suite, no CLI, and none of this project's build-type or warning settings imposed on it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(FWG_TOP_LEVEL ON)
else()
  set(FWG_TOP_LEVEL OFF)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(FWG_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(FWG_BUILD_TESTS "Build the test suite" ${FWG_TOP_LEVEL})
option(FWG_BUILD_CLI   "Build the fwg command line driver" ${FWG_TOP_LEVEL})
option(FWG_WERROR      "Treat warnings as errors" ON)

# ---------------------------------------------------------------------------
# Determinism flags.
#
# A world is one long chain of floating point operations. A compiler that is free
# to contract a*b+c into an FMA, or to reassociate a sum, produces a DIFFERENT
# WORLD on a different machine or a different optimisation level. Both MSVC (since
# 19.30) and GCC/Clang contract by default.
#
# These flags are not a performance preference we can revisit casually. They are
# the reason two machines agree on a seed. See docs/determinism.md.
#
# fwg_determinism is the contract, and the library passes it to every consumer.
# fwg_flags adds this project's own warning policy on top, for the library, the CLI
# and the tests alone: a consumer's code keeps its own warning level.
# ---------------------------------------------------------------------------
add_library(fwg_determinism INTERFACE)
add_library(fwg_flags INTERFACE)
target_link_libraries(fwg_flags INTERFACE fwg_determinism)

if(MSVC)
  # MSVC has no /fp:contract- . Contraction is OPT-IN through /fp:contract, and
  # /fp:precise leaves it off, so /fp:precise is the whole of what is needed here.
  # (/fp:fast would turn it on along with reassociation -- never set it.)
  target_compile_options(fwg_determinism INTERFACE /fp:precise)
  target_compile_options(fwg_flags INTERFACE /permissive- /utf-8 /W4)
  if(FWG_WERROR)
    target_compile_options(fwg_flags INTERFACE /WX)
  endif()
  # CPython links the shared UCRT and the static CRT's libm disagrees with it.
  # Irrelevant to correctness now that nothing is compared against Python, but the
  # shared runtime is also what an Unreal module expects, so keep it.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
  target_compile_options(fwg_determinism INTERFACE -ffp-contract=off -fno-fast-math)
  target_compile_options(fwg_flags INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wdouble-promotion)
  if(FWG_WERROR)
    target_compile_options(fwg_flags INTERFACE -Werror)
  endif()
endif()

add_subdirectory(src)

if(FWG_BUILD_CLI)
  add_subdirectory(apps)
endif()

if(FWG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
