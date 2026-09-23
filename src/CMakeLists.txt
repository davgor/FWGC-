add_library(fwg STATIC
  astrology.cpp
  beasties_stage.cpp
  beasts.cpp
  biomes.cpp
  city_plan.cpp
  civilizations.cpp
  climate.cpp
  colleges.cpp
  complete_stage.cpp
  config.cpp
  generated/content_biomes.cpp
  generated/content_buildings.cpp
  generated/content_city_shapes.cpp
  generated/content_settlement_plans.cpp
  generated/content_civilizations.cpp
  generated/content_entities.cpp
  generated/content_registry.cpp
  generated/content_heritage.cpp
  generated/content_heroes.cpp
  generated/content_story_web.cpp
  generated/content_key_locations.cpp
  generated/content_npcs.cpp
  generated/content_nests.cpp
  generated/content_nomads.cpp
  generated/content_pantheon.cpp
  generated/content_schools.cpp
  generated/content_variants.cpp
  generated/world_options.cpp
  corruption.cpp
  detail.cpp
  ecology.cpp
  erosion.cpp
  entities.cpp
  founding.cpp
  grid.cpp
  habitation.cpp
  heritage.cpp
  heroes.cpp
  history.cpp
  humans.cpp
  json.cpp
  key_locations.cpp
  magic.cpp
  measure.cpp
  nests.cpp
  nomads.cpp
  npcs.cpp
  noise.cpp
  options.cpp
  random.cpp
  roads.cpp
  routing.cpp
  populated_stage.cpp
  quests.cpp
  religion.cpp
  scale.cpp
  serialize.cpp
  settlement.cpp
  settlements_stage.cpp
  sha256.cpp
  society.cpp
  story_web.cpp
  tectonics.cpp
  time.cpp
  villains.cpp
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
