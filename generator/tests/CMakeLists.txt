add_executable(fwg_tests
  main.cpp
  test_ages.cpp
  test_astrology.cpp
  test_beasts.cpp
  test_biomes.cpp
  test_castle_plan.cpp
  test_city_plan.cpp
  test_civilizations.cpp
  test_climate.cpp
  test_colleges.cpp
  test_corruption.cpp
  test_document.cpp
  test_ecology.cpp
  test_entities.cpp
  test_fates.cpp
  test_habitation.cpp
  test_founding.cpp
  test_history.cpp
  test_heroes.cpp
  test_humans.cpp
  test_key_locations.cpp
  test_liveness.cpp
  test_magic.cpp
  test_quests.cpp
  test_religion.cpp
  test_roads.cpp
  test_routing.cpp
  test_settlement.cpp
  test_settlement_api.cpp
  test_heritage.cpp
  test_nests.cpp
  test_nomad_api.cpp
  test_nomads.cpp
  test_npcs.cpp
  test_options.cpp
  test_patch.cpp
  test_person_state.cpp
  test_read.cpp
  test_serialize.cpp
  test_settlements_stage.cpp
  test_society.cpp
  test_stages.cpp
  test_story_web.cpp
  test_substrate.cpp
  test_time.cpp
  test_villains.cpp
  test_visitation.cpp
  test_wars.cpp
  test_water.cpp
  test_world.cpp
  test_world_api.cpp
  test_world_scene.cpp
  test_world_time.cpp
)

target_link_libraries(fwg_tests PRIVATE fwg fwg_flags)
target_include_directories(fwg_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The product package's tests, kept as their own block so they merge cleanly with the
# generator's list above. They read the reference's conformance fixtures, carried verbatim
# in tests/fixtures/.
target_sources(fwg_tests PRIVATE
  test_asset_list.cpp
  test_capabilities.cpp
  test_coordinates.cpp
  test_counter_kernel.cpp
  test_hero_guild.cpp
  test_json_value.cpp
  test_request_json.cpp
  test_taxonomy.cpp
  test_world_handle.cpp
)
target_compile_definitions(fwg_tests PRIVATE FWG_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME fwg_tests COMMAND fwg_tests)

# The suite reports "no tests ran" as a failure, so an empty or mis-filtered run
# cannot be mistaken for a green one.
set_tests_properties(fwg_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL|no tests ran")
