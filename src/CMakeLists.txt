add_library(fwg STATIC
  astrology.cpp
  biomes.cpp
  civilizations.cpp
  climate.cpp
  config.cpp
  generated/content_biomes.cpp
  generated/content_civilizations.cpp
  generated/content_registry.cpp
  generated/content_schools.cpp
  generated/content_variants.cpp
  generated/world_options.cpp
  detail.cpp
  ecology.cpp
  erosion.cpp
  founding.cpp
  grid.cpp
  history.cpp
  json.cpp
  magic.cpp
  measure.cpp
  noise.cpp
  options.cpp
  random.cpp
  routing.cpp
  scale.cpp
  serialize.cpp
  settlement.cpp
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
