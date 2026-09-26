cmake_minimum_required(VERSION 3.20)
project(fwgc_orchestrator VERSION 0.1.0 LANGUAGES CXX)

# The director: deterministic C++ that holds one world, changes it only through the
# generator's logged verbs, and answers a player's dialogue acts as knowledge-scoped fact
# packets. See README.md.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ORCHESTRATOR_TOP_LEVEL ON)
else()
  set(ORCHESTRATOR_TOP_LEVEL OFF)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(ORCHESTRATOR_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(ORCHESTRATOR_BUILD_TESTS "Build the orchestrator's tests" ${ORCHESTRATOR_TOP_LEVEL})

# The generator, as a sibling adds it: the library alone, with its determinism flags.
if(NOT TARGET fwg::fwg)
  add_subdirectory(../generator fwg)  # the binary-directory argument is required
endif()

if(MSVC)
  set(ORCHESTRATOR_WARNINGS /W4 /WX /permissive- /utf-8)
else()
  set(ORCHESTRATOR_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
endif()

# The contract: the types a communicator shares with the director. It links nothing of the
# generator, so a component that depends on this alone cannot reach a world.
add_library(orchestrator_contract STATIC src/contract.cpp)
add_library(fwgc::orchestrator_contract ALIAS orchestrator_contract)
target_include_directories(orchestrator_contract PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(orchestrator_contract PUBLIC cxx_std_20)
target_compile_options(orchestrator_contract PRIVATE ${ORCHESTRATOR_WARNINGS})

# The director itself.
add_library(orchestrator STATIC src/director.cpp src/knowledge.cpp)
add_library(fwgc::orchestrator ALIAS orchestrator)
target_include_directories(orchestrator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(orchestrator PUBLIC orchestrator_contract fwg::fwg)
target_compile_options(orchestrator PRIVATE ${ORCHESTRATOR_WARNINGS})

if(ORCHESTRATOR_BUILD_TESTS)
  enable_testing()
  add_executable(orchestrator_tests
    tests/main.cpp
    tests/test_contract.cpp
    tests/test_director.cpp
    tests/test_conversation.cpp
  )
  target_link_libraries(orchestrator_tests PRIVATE orchestrator)
  target_compile_options(orchestrator_tests PRIVATE ${ORCHESTRATOR_WARNINGS})
  add_test(NAME orchestrator_tests COMMAND orchestrator_tests)
endif()
