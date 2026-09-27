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
add_library(orchestrator STATIC src/director.cpp src/knowledge.cpp src/schedule.cpp)
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
    tests/test_schedule.cpp
  )
  target_link_libraries(orchestrator_tests PRIVATE orchestrator)
  # The schedule is tested on its own too, without a world: its header is the library's own.
  target_include_directories(orchestrator_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_options(orchestrator_tests PRIVATE ${ORCHESTRATOR_WARNINGS})
  add_test(NAME orchestrator_tests COMMAND orchestrator_tests)

  # A measuring tool, not a test: a scripted session written out as packets, for
  # communicator_eval --session to speak (tools/session.cpp says how to script one).
  add_executable(orchestrator_session tools/session.cpp)
  target_link_libraries(orchestrator_session PRIVATE orchestrator)
  target_compile_options(orchestrator_session PRIVATE ${ORCHESTRATOR_WARNINGS})
endif()

# The director in C: fwgc_director.dll, the whole director behind the plain C interface in
# include/orchestrator/fwgc_director.h, for a game engine to load. Only a build of this folder on
# its own makes it; a component that adds this folder as a sibling (the communicator) never sees
# it. The DLL, its import library and its test land together, so the test finds the DLL.
option(ORCHESTRATOR_BUILD_C_API "Build fwgc_director, the C interface a game binds to" ${ORCHESTRATOR_TOP_LEVEL})
if(ORCHESTRATOR_BUILD_C_API)
  set(FWGC_DIRECTOR_RUNTIME "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")  # the shared runtime, as the generator's
  add_library(fwgc_director SHARED src/c_api.cpp src/c_json.cpp src/c_save.cpp)
  target_include_directories(fwgc_director PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(fwgc_director PRIVATE orchestrator)
  target_compile_options(fwgc_director PRIVATE ${ORCHESTRATOR_WARNINGS})
  set_target_properties(fwgc_director PROPERTIES
    MSVC_RUNTIME_LIBRARY ${FWGC_DIRECTOR_RUNTIME}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
  # What a game's third-party folder takes: the DLL, its import library, the header.
  install(TARGETS fwgc_director RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
  install(FILES include/orchestrator/fwgc_director.h DESTINATION include)

  if(ORCHESTRATOR_BUILD_TESTS)
    # Through the DLL, held to the C++ director it wraps: the test links both, two separate images.
    add_executable(orchestrator_c_tests tests/main.cpp tests/test_c_api.cpp)
    target_link_libraries(orchestrator_c_tests PRIVATE fwgc_director orchestrator)
    target_include_directories(orchestrator_c_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(orchestrator_c_tests PRIVATE ${ORCHESTRATOR_WARNINGS})
    set_target_properties(orchestrator_c_tests PROPERTIES
      MSVC_RUNTIME_LIBRARY ${FWGC_DIRECTOR_RUNTIME}
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
    add_test(NAME orchestrator_c_tests COMMAND orchestrator_c_tests)
  endif()
endif()
