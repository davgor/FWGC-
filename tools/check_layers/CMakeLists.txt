cmake_minimum_required(VERSION 3.20)
project(fwgc_check_layers VERSION 0.1.0 LANGUAGES CXX)

# The layer checker and the commit gate: dev tools with no dependency outside the standard
# library, built on their own (they do not add the generator or anything else in the repo).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Werror)
endif()

add_library(layers_core STATIC
  src/lex.cpp
  src/rules.cpp
  src/check.cpp
  src/json.cpp
)
target_include_directories(layers_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(check_layers src/main_check.cpp)
target_link_libraries(check_layers PRIVATE layers_core)

# The gate runs git and the claude CLI as child processes; that layer is Windows-only for now.
if(WIN32)
  add_executable(commit_gate src/main_gate.cpp src/process.cpp)
  target_link_libraries(commit_gate PRIVATE layers_core)
else()
  message(STATUS "commit_gate is not built: its process layer (src/process.cpp) is Windows-only")
endif()

enable_testing()
add_executable(layers_tests tests/test_main.cpp)
target_link_libraries(layers_tests PRIVATE layers_core)
add_test(NAME layers_tests
  COMMAND layers_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures ${CMAKE_CURRENT_SOURCE_DIR}/layers.txt)
