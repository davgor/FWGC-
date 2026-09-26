// The public API headers (include/fwg/api/), included first and on their own, the way a
// sibling component includes them. A forwarder that stops compiling without help from an
// earlier include fails here rather than in the first consumer.
#include "fwg/api/ages.hpp"
#include "fwg/api/capabilities.hpp"
#include "fwg/api/json_value.hpp"
#include "fwg/api/nomad_api.hpp"
#include "fwg/api/option_ids.hpp"
#include "fwg/api/options.hpp"
#include "fwg/api/person_state.hpp"
#include "fwg/api/quests.hpp"
#include "fwg/api/read.hpp"
#include "fwg/api/request_json.hpp"
#include "fwg/api/serialize.hpp"
#include "fwg/api/settlement_api.hpp"
#include "fwg/api/weather.hpp"
#include "fwg/api/workers.hpp"
#include "fwg/api/world.hpp"
#include "fwg/api/world_api.hpp"
#include "fwg/api/world_time.hpp"

#include "check.hpp"

#include <optional>
#include <string>

// The bodies below call only functions generator/api.txt lists, so they also show that the
// list is enough to reach what they test.

TEST(every_published_option_resolves_by_its_own_name_through_the_api) {
    const auto specs = fwg::option_specs();
    check::is_true(!specs.empty(), "the option table to publish at least one option");
    for (const fwg::OptionSpec& spec : specs) {
        const auto option = fwg::option_by_name(spec.name);
        check::is_true(option.has_value(), "option_by_name to find " + std::string(spec.name));
        check::equal(std::string(fwg::option_spec(*option).name), std::string(spec.name),
                     "the spec found by name");
    }
}

TEST(the_weather_at_a_moment_reads_through_the_api) {
    fwg::Request request;
    request.seed = 42;
    request.size = 17;
    request.phase = 9;
    const fwg::World world = fwg::generate(fwg::resolve(request));
    const fwg::WeatherReport report = fwg::weather_report(world, 400000.25);
    const fwg::WeatherSample here = fwg::weather_sample(world, report, 10.0, 20.0);
    const fwg::WeatherSample again = fwg::weather_at(world, 400000.25, 10.0, 20.0);
    check::equal(here.intensity_pct, again.intensity_pct, "a sample from a report and a read at the same moment");
    check::equal(here.summer_pct, again.summer_pct, "the same season both ways");
    const fwg::WeatherField field = fwg::weather_field(world, report);
    check::equal(field.intensity_pct.size(), world.config.size, "the field on the world's own grid");
}

TEST(the_weather_at_a_node_and_the_moon_read_through_the_api) {
    fwg::Request request;
    request.seed = 42;
    request.size = 17;
    request.phase = 9;
    const fwg::World world = fwg::generate(fwg::resolve(request));
    const fwg::WeatherReport report = fwg::weather_report(world, 400000.25);
    const fwg::WeatherSample at_node = fwg::weather_sample_at_node(world, report, 100);
    const fwg::WeatherSample there = fwg::weather_sample(world, report, at_node.latitude_deg, at_node.longitude_deg);
    check::equal(at_node.intensity_pct, there.intensity_pct, "a node's weather is the weather where the node stands");
    check::equal(at_node.summer_pct, there.summer_pct, "and its season");
    const std::optional<fwg::MoonPhase> moon = fwg::moon_phase_on(world, 400000.25);
    check::is_true(moon.has_value(), "a world with an almanac has a moon");
    if (!moon) return;
    check::is_true(moon->synodic_days > 1 && moon->fraction >= 0.0 && moon->fraction < 1.0, "a phase inside its period");
    int full = 0;
    for (int day = 400000; day < 400000 + moon->synodic_days; ++day) full += fwg::moon_phase_on(world, day)->full ? 1 : 0;
    check::equal(full, 1, "one full moon in a period");
}

TEST(every_published_control_name_has_a_definition_through_the_api) {
    const auto& names = fwg::control_names();
    check::is_true(!names.empty(), "the control catalogue to name at least one control");
    for (const std::string& name : names)
        check::is_true(fwg::control_definition(name) != nullptr, "a definition for control " + name);
    check::is_true(fwg::control_definition("no_such_control_anywhere") == nullptr,
                   "no definition for a name the catalogue does not publish");
}
