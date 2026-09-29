add_executable(fwg_tests
  main.cpp
  test_ages.cpp
  test_api.cpp
  test_astrology.cpp
  test_beasts.cpp
  test_biomes.cpp
  test_castle_plan.cpp
  test_city_layout.cpp
  test_city_plan.cpp
  test_civilizations.cpp
  test_climate.cpp
  test_colleges.cpp
  test_corruption.cpp
  test_document.cpp
  test_document_paths.cpp
  test_ecology.cpp
  test_entities.cpp
  test_fates.cpp
  test_fortresses.cpp
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
  test_hero_action.cpp
  test_read.cpp
  test_serialize.cpp
  test_settlements_stage.cpp
  test_society.cpp
  test_split.cpp
  test_stages.cpp
  test_story_web.cpp
  test_substrate.cpp
  test_time.cpp
  test_villains.cpp
  test_visitation.cpp
  test_wars.cpp
  test_water.cpp
  test_weather.cpp
  test_workers.cpp
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

# `fwg patch` end to end, where the CLI is built: a request file in and the fwg.patch
# document out, and a patch config the library refuses coming back as fwg's failure
# document. The document itself is pinned by test_patch.cpp; these pin the command.
if(TARGET fwg_cli)
  set(FWG_PATCH_REQUEST "${CMAKE_CURRENT_BINARY_DIR}/patch_request.json")
  file(WRITE "${FWG_PATCH_REQUEST}" "{\"seed\": 42, \"recipe_version\": 3, \"overrides\": {\"size\": 9}}\n")
  add_test(NAME fwg_cli_patch
           COMMAND fwg_cli patch --request "${FWG_PATCH_REQUEST}" --latitude 10 --longitude 20 --span 32 --spacing 2)
  set_tests_properties(fwg_cli_patch PROPERTIES
                       PASS_REGULAR_EXPRESSION "^\\{\"schema\":\"fwg\\.patch\",\"version\":1,.*\"size\":17,")
  add_test(NAME fwg_cli_patch_refused
           COMMAND fwg_cli patch --request "${FWG_PATCH_REQUEST}" --span 100000)
  set_tests_properties(fwg_cli_patch_refused PROPERTIES
                       PASS_REGULAR_EXPRESSION "\"schema\": \"fantasy-world-generator\\.failure\".*\"field\": \"patch\\.span\"")

  # `fwg weather` end to end: three hours at a place are three moments of one sample each, and a
  # place off the globe is refused as a failure document naming the field. The model is pinned by
  # test_weather.cpp.
  add_test(NAME fwg_cli_weather
           COMMAND fwg_cli weather --request "${FWG_PATCH_REQUEST}" --day 400000.5 --latitude 10 --longitude 20 --hours 3)
  set_tests_properties(fwg_cli_weather PROPERTIES
                       PASS_REGULAR_EXPRESSION "^\\{\"schema\":\"fwg\\.weather\",\"version\":2,\"moments\":\\[.*\"samples\":\\[\\{[^{}]*\\}\\].*\"samples\":\\[\\{[^{}]*\\}\\].*\"samples\":\\[\\{[^{}]*\\}\\]")
  add_test(NAME fwg_cli_weather_refused
           COMMAND fwg_cli weather --request "${FWG_PATCH_REQUEST}" --latitude 95 --longitude 0)
  set_tests_properties(fwg_cli_weather_refused PROPERTIES
                       PASS_REGULAR_EXPRESSION "\"schema\": \"fantasy-world-generator\\.failure\".*\"field\": \"latitude\"")
endif()
