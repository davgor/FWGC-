// The world-options control surface: cross-field rules and prerequisite biasing.
#include "check.hpp"

#include <algorithm>
#include <string>

#include "fwg/config.hpp"

using namespace fwg;

namespace {

double option_default(Option o) { return option_spec(o).default_value; }

Config with(const std::map<std::string, double>& options) {
    Request r;
    r.seed = 42;
    r.size = 33;
    r.phase = 2;
    r.options = options;
    return resolve(r);
}

// How far a knob can be pushed past its default without leaving its own range. Tests
// derive their bump from this rather than naming a number, so a retuned option does not
// turn a real assertion into a refusal the test reads as a failure.
double headroom(Option o, double wanted) {
    const OptionSpec& spec = option_spec(o);
    return std::min(wanted, spec.max_value - spec.default_value);
}

bool biased(const Config& cfg, const char* target) {
    for (const auto& b : cfg.option_biases) {
        if (b.target == target) return true;
    }
    return false;
}

}  // namespace

// --- cross-field rules ----------------------------------------------------

TEST(a_world_where_no_island_could_have_a_character_is_refused) {
    // Every cluster draws its character from these four weights. All four at zero
    // leaves nothing to draw from, and both honest outcomes downstream are bad: pick a
    // fixed kind and every archipelago silently becomes volcanic, or skip the draw and
    // shift the stream every later cluster reads. The reference refuses the combination
    // instead, and so does this.
    check::throws([] {
        with({{"volcanic_island_weight", 0.0},
              {"atoll_weight", 0.0},
              {"continental_island_weight", 0.0},
              {"cold_island_weight", 0.0}});
    }, "a world with no possible island character");

    // One positive weight is enough.
    const Config cfg = with({{"volcanic_island_weight", 0.0},
                             {"atoll_weight", 0.0},
                             {"continental_island_weight", 0.0},
                             {"cold_island_weight", 0.25}});
    check::close(cfg.options[Option::cold_island_weight], 0.25, 1e-12, "the one live weight to survive");
}

// --- prerequisite biasing -------------------------------------------------

TEST(asking_for_dragons_raises_the_mountains_they_need) {
    // A caller who raises draconic_occurrence is asking for dragons, and dragons need
    // mountains. Without this the request raises the region's own weight over ground
    // that still cannot host it -- a stronger multiplier on zero.
    const double bump = headroom(Option::draconic_occurrence, 0.3);
    check::is_true(bump > 0.0, "draconic_occurrence to have room to be asked for");
    const Config cfg = with({{"draconic_occurrence", option_default(Option::draconic_occurrence) + bump}});

    check::is_true(cfg.options[Option::mountain_abundance] > option_default(Option::mountain_abundance),
                   "the mountains to grow for the dragons");
    check::close(cfg.options[Option::mountain_abundance],
                 option_default(Option::mountain_abundance) + bump * 0.6, 1e-12,
                 "the nudge to be proportional to how far past the default the caller went");
    check::is_true(biased(cfg, "mountain_abundance"), "the bias to be recorded, not silent");
}

TEST(a_caller_who_sets_the_condition_themselves_is_not_overruled) {
    // An explicit setting is the caller's word. A side effect of another knob does not
    // get to quietly replace it.
    const double bump = headroom(Option::draconic_occurrence, 0.3);
    const Config cfg = with({{"draconic_occurrence", option_default(Option::draconic_occurrence) + bump},
                             {"mountain_abundance", 0.5}});
    check::close(cfg.options[Option::mountain_abundance], 0.5, 1e-12,
                 "the caller's own mountain setting to stand");
    check::is_true(!biased(cfg, "mountain_abundance"), "no bias to be recorded");
}

TEST(a_world_nobody_asked_anything_of_keeps_its_defaults) {
    const Config cfg = with({});
    check::close(cfg.options[Option::mountain_abundance], option_default(Option::mountain_abundance),
                 1e-12, "an unasked world to keep the default");
    check::equal(cfg.option_biases.size(), std::size_t{0}, "and to record no biases");
}

TEST(the_bias_is_clamped_to_what_the_option_allows) {
    // A large request must not push a knob past its own published bounds.
    const double spec_max = option_spec(Option::draconic_occurrence).max_value;
    const Config cfg = with({{"draconic_occurrence", spec_max}});
    const OptionSpec& mountains = option_spec(Option::mountain_abundance);
    check::between(cfg.options[Option::mountain_abundance], mountains.min_value, mountains.max_value,
                   "the biased value to stay within the option's own range");
}

TEST(red_sands_want_dry_ground_so_their_bias_pulls_downward) {
    // The only rule with a negative gain. A bias table that took magnitudes rather than
    // signed gains would make the desert wetter.
    const double bump = headroom(Option::red_sands_occurrence, 0.3);
    check::is_true(bump > 0.0, "red_sands_occurrence to have room to be asked for");
    const Config cfg = with({{"red_sands_occurrence", option_default(Option::red_sands_occurrence) + bump}});
    check::is_true(cfg.moisture_bias < 0.0,
                   "the world to dry out for the red sands, got " + std::to_string(cfg.moisture_bias));
    check::is_true(biased(cfg, "moisture_bias"), "the bias to be recorded");
}

TEST(each_region_biases_its_own_condition_and_no_others) {
    // Eight rules, eight different targets. A table that pointed two regions at one
    // knob, or every region at the first knob, would still pass a test that only ever
    // asked about dragons.
    struct Case {
        const char* zone;
        Option target;
    };
    const Case cases[] = {
        {"demonic_occurrence", Option::infernal_strength},
        {"haunted_sands_occurrence", Option::umbral_strength},
        {"steampunk_occurrence", Option::metal_abundance},
        {"dead_sea_occurrence", Option::salinity},
        {"starlight_lakes_occurrence", Option::weave_strength},
        {"pirate_occurrence", Option::archipelago_occurrence},
    };
    for (const Case& c : cases) {
        const auto zone = option_by_name(c.zone);
        check::is_true(zone.has_value(), std::string(c.zone) + " to be a published option");
        if (!zone) continue;
        const double bump = headroom(*zone, 0.3);
        check::is_true(bump > 0.0, std::string(c.zone) + " to have room to be asked for");
        const Config cfg = with({{c.zone, option_spec(*zone).default_value + bump}});
        check::is_true(cfg.options[c.target] != option_default(c.target),
                       std::string(c.zone) + " to move " + std::string(option_spec(c.target).name));
        check::equal(cfg.option_biases.size(), std::size_t{1},
                     std::string(c.zone) + " to bias exactly one condition");
    }
}
