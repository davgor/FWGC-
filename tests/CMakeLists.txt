add_executable(fwg_tests
  main.cpp
  test_astrology.cpp
  test_beasts.cpp
  test_biomes.cpp
  test_city_plan.cpp
  test_civilizations.cpp
  test_climate.cpp
  test_ecology.cpp
  test_entities.cpp
  test_habitation.cpp
  test_founding.cpp
  test_history.cpp
  test_magic.cpp
  test_religion.cpp
  test_roads.cpp
  test_routing.cpp
  test_settlement.cpp
  test_heritage.cpp
  test_nests.cpp
  test_options.cpp
  test_settlements_stage.cpp
  test_stages.cpp
  test_substrate.cpp
  test_water.cpp
  test_world.cpp
)

target_link_libraries(fwg_tests PRIVATE fwg fwg_flags)
target_include_directories(fwg_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME fwg_tests COMMAND fwg_tests)

# The suite reports "no tests ran" as a failure, so an empty or mis-filtered run
# cannot be mistaken for a green one.
set_tests_properties(fwg_tests PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL|no tests ran")
