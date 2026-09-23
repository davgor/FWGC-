add_library(fwg STATIC
  ages.cpp
  astrology.cpp
  beasties_stage.cpp
  beasts.cpp
  biomes.cpp
  build_stages.cpp
  castle_plan.cpp
  city_layout.cpp
  city_plan.cpp
  civilization_report.cpp
  civilizations.cpp
  climate.cpp
  colleges.cpp
  complete_stage.cpp
  config.cpp
  generated/content_biomes.cpp
  generated/content_buildings.cpp
  generated/content_castles.cpp
  generated/content_city_shapes.cpp
  generated/content_settlement_plans.cpp
  generated/content_civilizations.cpp
  generated/content_civilization_report.cpp
  generated/content_controls.cpp
  generated/content_entities.cpp
  generated/content_registry.cpp
  generated/content_heritage.cpp
  generated/content_heroes.cpp
  generated/content_story_web.cpp
  generated/content_key_locations.cpp
  generated/content_ley_destruction.cpp
  generated/content_npcs.cpp
  generated/content_nests.cpp
  generated/content_nest_catalogue.cpp
  generated/content_nomads.cpp
  generated/content_pantheon.cpp
  generated/content_schools.cpp
  generated/content_variants.cpp
  generated/world_options.cpp
  corruption.cpp
  debug_stats.cpp
  detail.cpp
  ecology.cpp
  erosion.cpp
  fates.cpp
  entities.cpp
  founding.cpp
  grid.cpp
  habitation.cpp
  heritage.cpp
  heroes.cpp
  history.cpp
  humans.cpp
  json.cpp
  json_scan.cpp
  key_locations.cpp
  liveness.cpp
  local_sampler.cpp
  magic.cpp
  measure.cpp
  nests.cpp
  nomad_api.cpp
  nomads.cpp
  npcs.cpp
  noise.cpp
  options.cpp
  patch.cpp
  person_state.cpp
  random.cpp
  read.cpp
  read_select.cpp
  roads.cpp
  routing.cpp
  populated_stage.cpp
  quests.cpp
  religion.cpp
  request_error.cpp
  scale.cpp
  serialize.cpp
  serialize_blocks.cpp
  serialize_document.cpp
  settlement.cpp
  settlement_api.cpp
  settlements_stage.cpp
  sha256.cpp
  society.cpp
  story_web.cpp
  tectonics.cpp
  time.cpp
  villains.cpp
  visitation.cpp
  wars.cpp
  water.cpp
  world.cpp
  world_api.cpp
  world_scene.cpp
  world_time.cpp
)

# The product package: the JSON document layer, world handles, the counter kernel, the
# coordinate oracle, the taxonomy, capabilities and controls, hero guild planning, the
# asset list and the JSON request boundary. Kept as its own block so it merges cleanly
# with the generator's list above.
target_sources(fwg PRIVATE
  asset_list.cpp
  capabilities.cpp
  coordinates.cpp
  counter_kernel.cpp
  hero_guild.cpp
  json_value.cpp
  request_json.cpp
  taxonomy.cpp
  world_handle.cpp
  generated/content_asset_sources.cpp
  generated/content_capabilities.cpp
  generated/content_control_catalogue.cpp
  generated/content_hero_guild.cpp
  generated/content_taxonomy.cpp
)

add_library(fwg::fwg ALIAS fwg)

target_include_directories(fwg PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(fwg PRIVATE fwg_flags)

# The determinism flags are part of the contract, not an implementation detail: a
# consumer that compiles against this header set with contraction on gets different
# worlds. Propagate them -- them alone: the warning policy is this project's own. The
# headers are C++20, so a consumer is compiled as C++20 at least.
target_link_libraries(fwg INTERFACE fwg_determinism)
target_compile_features(fwg PUBLIC cxx_std_20)

set_target_properties(fwg PROPERTIES POSITION_INDEPENDENT_CODE ON)
