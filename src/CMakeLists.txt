add_library(fwg STATIC
  climate.cpp
  config.cpp
  erosion.cpp
  grid.cpp
  json.cpp
  measure.cpp
  noise.cpp
  random.cpp
  scale.cpp
  serialize.cpp
  sha256.cpp
  tectonics.cpp
  water.cpp
  world.cpp
)

add_library(fwg::fwg ALIAS fwg)

target_include_directories(fwg PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(fwg PRIVATE fwg_flags)

# The determinism flags are part of the contract, not an implementation detail: a
# consumer that compiles against this header set with contraction on gets different
# worlds. Propagate them.
target_link_libraries(fwg INTERFACE fwg_flags)

set_target_properties(fwg PROPERTIES POSITION_INDEPENDENT_CODE ON)
