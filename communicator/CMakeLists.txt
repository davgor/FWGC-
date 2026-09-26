cmake_minimum_required(VERSION 3.25)
project(fwgc_communicator VERSION 0.1.0 LANGUAGES C CXX)

# NPC dialogue: the interpreter (a player's words to one dialogue act) and the voice (a fact
# packet in the speaker's words). It sees the orchestrator's contract and nothing of a world.
# See README.md.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(COMMUNICATOR_TOP_LEVEL ON)
else()
  set(COMMUNICATOR_TOP_LEVEL OFF)
endif()

if(COMMUNICATOR_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(COMMUNICATOR_BUILD_TESTS "Build the communicator's tests" ${COMMUNICATOR_TOP_LEVEL})

# The two components below this one, as a sibling adds them. inference comes first, before this
# file sets its C++20: llama.cpp builds as C++17 (inference/CMakeLists.txt says why).
# EXCLUDE_FROM_ALL builds only what is linked: the orchestrator's contract, never its director or
# the generator behind it.
if(NOT TARGET fwgc::inference)
  add_subdirectory(../inference inference EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET fwgc::orchestrator_contract)
  add_subdirectory(../orchestrator orchestrator EXCLUDE_FROM_ALL)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(COMMUNICATOR_WARNINGS /W4 /WX /permissive- /utf-8)
else()
  set(COMMUNICATOR_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
endif()

add_library(communicator STATIC
  src/authored.cpp
  src/gbnf.cpp
  src/interpreter.cpp
  src/known.cpp
  src/meaning.cpp
  src/memory.cpp
  src/mention.cpp
  src/reply_shape.cpp
  src/text.cpp
  src/voice.cpp
)
add_library(fwgc::communicator ALIAS communicator)
target_include_directories(communicator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(communicator PUBLIC cxx_std_20)
# The contract alone: the director, and the generator behind it, are out of reach.
target_link_libraries(communicator PUBLIC fwgc::orchestrator_contract fwgc::inference)
target_compile_options(communicator PRIVATE ${COMMUNICATOR_WARNINGS})

if(COMMUNICATOR_BUILD_TESTS)
  enable_testing()
  add_executable(communicator_tests
    tests/main.cpp
    tests/test_authored.cpp
    tests/test_known.cpp
    tests/test_interpreter.cpp
    tests/test_meaning.cpp
    tests/test_voice.cpp
  )
  target_link_libraries(communicator_tests PRIVATE communicator)
  target_include_directories(communicator_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_options(communicator_tests PRIVATE ${COMMUNICATOR_WARNINGS})
  # The test model and the embedding model inference/README.md names, downloaded and never
  # committed; FWGC_TEST_MODEL and FWGC_EMBEDDING_MODEL point elsewhere.
  target_compile_definitions(communicator_tests PRIVATE
    COMMUNICATOR_TEST_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/../inference/models/qwen2.5-0.5b-instruct-q4_0.gguf"
    COMMUNICATOR_EMBEDDING_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/../inference/models/Qwen3-Embedding-0.6B-Q8_0.gguf"
    COMMUNICATOR_LINES="${CMAKE_CURRENT_SOURCE_DIR}/data/lines"
    COMMUNICATOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  add_test(NAME communicator_tests COMMAND communicator_tests)

  # A measuring tool over eval/, not a test: eval/README.md.
  add_executable(communicator_eval eval/main.cpp)
  target_link_libraries(communicator_eval PRIVATE communicator)
  target_include_directories(communicator_eval PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_options(communicator_eval PRIVATE ${COMMUNICATOR_WARNINGS})
  target_compile_definitions(communicator_eval PRIVATE
    COMMUNICATOR_TEST_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/../inference/models/qwen2.5-0.5b-instruct-q4_0.gguf"
    COMMUNICATOR_EVAL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/eval"
    COMMUNICATOR_LINES="${CMAKE_CURRENT_SOURCE_DIR}/data/lines")
endif()
