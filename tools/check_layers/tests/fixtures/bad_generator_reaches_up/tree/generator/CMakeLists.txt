# add_subdirectory(../orchestrator) in a comment is fine
add_library(fwg STATIC src/world.cpp)
add_subdirectory(../orchestrator orch)
target_link_libraries(fwg PRIVATE orchestrator_core)
