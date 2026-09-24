cmake_minimum_required(VERSION 3.25)
project(fwgc_inference VERSION 0.1.0 LANGUAGES C CXX)

# The local model runtime: a GGUF model in memory, text in, text out, on this machine. It knows
# nothing of worlds. See README.md.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(INFERENCE_TOP_LEVEL ON)
else()
  set(INFERENCE_TOP_LEVEL OFF)
endif()

if(INFERENCE_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(INFERENCE_BUILD_TESTS "Build the inference tests" ${INFERENCE_TOP_LEVEL})

# llama.cpp, pinned to the v0.5.0 source archive by its hash. The build downloads it once,
# when it is configured (dev tooling may use the network); the runtime fetches nothing.
# Only the library is built: no tools, server, examples, tests or HTTPS support, and the CPU
# backend alone. SYSTEM keeps its headers' warnings out of this component's /W4 /WX.
set(LLAMA_BUILD_COMMON   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP      OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_UI       OFF CACHE BOOL "" FORCE)
set(LLAMA_OPENSSL        OFF CACHE BOOL "" FORCE)
# ggml's RPC backend talks over sockets. It is off by default today; saying so here keeps a
# future default from bringing a network backend into the runtime unnoticed.
set(GGML_RPC             OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(llama_cpp
  URL https://codeload.github.com/ggml-org/llama.cpp/tar.gz/refs/tags/v0.5.0
  URL_HASH SHA256=fef9ed754f4e031fb5c663c29260feda4ebc241abb68d64a81c0f1df5f1748e2
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM
)
FetchContent_MakeAvailable(llama_cpp)

# This component's C++20 is set only now, after llama.cpp's targets exist: llama.cpp is written
# for C++17, where a u8"" literal is still a char array, and does not build as C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(INFERENCE_WARNINGS /W4 /WX /permissive- /utf-8)
else()
  set(INFERENCE_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
endif()

add_library(inference STATIC src/runtime.cpp)
add_library(fwgc::inference ALIAS inference)
target_include_directories(inference PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(inference PUBLIC cxx_std_20)
# PRIVATE: a consumer sees this component's header, never llama.cpp's.
target_link_libraries(inference PRIVATE llama)
target_compile_options(inference PRIVATE ${INFERENCE_WARNINGS})

if(INFERENCE_BUILD_TESTS)
  enable_testing()
  add_executable(inference_tests tests/main.cpp tests/test_runtime.cpp)
  target_link_libraries(inference_tests PRIVATE inference)
  target_compile_options(inference_tests PRIVATE ${INFERENCE_WARNINGS})
  # The test model is downloaded, never committed; FWGC_TEST_MODEL points elsewhere.
  target_compile_definitions(inference_tests PRIVATE
    INFERENCE_TEST_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/models/qwen2.5-0.5b-instruct-q4_0.gguf")
  add_test(NAME inference_tests COMMAND inference_tests)
endif()
