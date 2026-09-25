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
# GPU offload through Vulkan, which NVIDIA, AMD and Intel drivers carry on Windows, so a player
# installs nothing. Building it needs the Vulkan SDK (its shader compiler and headers); off by
# default, so a machine without the SDK still builds, for the CPU alone.
option(INFERENCE_VULKAN "Build llama.cpp's Vulkan backend, for GPU offload" OFF)

# llama.cpp, pinned to the v0.5.0 source archive by its hash. The build downloads it once,
# when it is configured (dev tooling may use the network); the runtime fetches nothing.
# Only the library is built: no tools, server, examples, tests or HTTPS support; the CPU
# backend, and with INFERENCE_VULKAN the Vulkan one. SYSTEM keeps its headers' warnings out of
# this component's /W4 /WX.
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
set(GGML_VULKAN          ${INFERENCE_VULKAN} CACHE BOOL "" FORCE)
if(INFERENCE_VULKAN)
  # A GPU build loads its backends at run time, as DLLs beside the program. Linked in, the
  # Vulkan backend would make vulkan-1.dll a dependency of the program itself, and a machine
  # without Vulkan could not start it at all; loaded, a backend that will not load is skipped
  # and the CPU runs instead. The CPU backend comes in one DLL per processor generation, the
  # best one chosen on each machine, rather than built for the machine that compiles it.
  set(BUILD_SHARED_LIBS     ON  CACHE BOOL "" FORCE)
  set(GGML_BACKEND_DL       ON  CACHE BOOL "" FORCE)
  set(GGML_CPU_ALL_VARIANTS ON  CACHE BOOL "" FORCE)
  set(GGML_NATIVE           OFF CACHE BOOL "" FORCE)
  # Every DLL and program in one folder, where the backends are looked for.
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
else()
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
endif()

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
if(INFERENCE_VULKAN)
  target_compile_definitions(inference PRIVATE INFERENCE_LOAD_BACKENDS=1)
endif()

if(INFERENCE_BUILD_TESTS)
  enable_testing()
  add_executable(inference_tests tests/main.cpp tests/test_runtime.cpp)
  target_link_libraries(inference_tests PRIVATE inference)
  target_compile_options(inference_tests PRIVATE ${INFERENCE_WARNINGS})
  # The test model is downloaded, never committed; FWGC_TEST_MODEL points elsewhere.
  # The tests know which build they run in: with a GPU backend, offloading must reach a GPU.
  if(INFERENCE_VULKAN)
    set(INFERENCE_TESTS_GPU 1)
  else()
    set(INFERENCE_TESTS_GPU 0)
  endif()
  target_compile_definitions(inference_tests PRIVATE
    INFERENCE_TEST_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/models/qwen2.5-0.5b-instruct-q4_0.gguf"
    INFERENCE_TESTS_GPU=${INFERENCE_TESTS_GPU})
  if(INFERENCE_VULKAN)
    # A backend the runtime does not ship, offered to it in a test through GGML_BACKEND_PATH:
    # the runtime must unload it.
    add_library(inference_foreign_backend MODULE tests/foreign_backend.cpp)
    # Kept out of bin/, where the DLLs a game ships are gathered.
    set_target_properties(inference_foreign_backend PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test-backends)
    target_include_directories(inference_foreign_backend PRIVATE
      ${llama_cpp_SOURCE_DIR}/ggml/include ${llama_cpp_SOURCE_DIR}/ggml/src)
    target_compile_definitions(inference_foreign_backend PRIVATE GGML_BACKEND_DL GGML_BACKEND_SHARED GGML_BACKEND_BUILD)
    add_dependencies(inference_tests inference_foreign_backend)
    target_compile_definitions(inference_tests PRIVATE
      INFERENCE_TEST_FOREIGN_BACKEND="$<TARGET_FILE:inference_foreign_backend>")
  endif()
  add_test(NAME inference_tests COMMAND inference_tests)
endif()
