// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/nomads.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/nomads.hpp"

namespace fwg {
namespace {

// What each kind of band is walking toward and what it has with it. Nothing in a
// nomad pass reads either; every band record publishes both, and a fission child
// copies the arrays of the classification it inherits.
constexpr std::string_view kSeeks[] = {
    "shelter",
    "kin",
    "food",
    "cover",
    "plunder",
    "a way out",
    "the god",
    "sacred ground",
    "converts",
    "markets",
    "escort",
    "a safe night",
    "plunder",
    "ransom",
    "weak escorts",
    "pasture",
    "water",
    "a sheltered winter",
};

constexpr std::string_view kCarries[] = {
    "what they could lift",
    "army issue",
    "stolen supply",
    "relics",
    "offerings",
    "goods",
    "coin",
    "news",
    "stolen goods",
    "herds",
    "tents",
    "craft",
};

// Only herders split today. Ordered by the enum rather than by the file, for the
// same reason the table below is.
constexpr NomadClass kFissionClasses[] = {
    NomadClass::Wanderers,
};

// IN NomadClass ORDER, NOT FILE ORDER. The gates are evaluated in this order, the
// share list is built in it, `total` accumulates in it and the cumulative pick
// walks it -- so reordering the same rows is a different world on the same seed.
// The table is indexed by the enum, which is what makes that structural.
constexpr NomadClassPolicy kClasses[] = {
    {"survivors", 1.6, NomadDisposition::Fleeing, 11000.0, 30, 260, 1.4,
     {kSeeks + 0, 3}, {kCarries + 0, 1},
     {.ruin_reach_spacings = 0.9, .refuge_reach_spacings = 2.5}},
    {"deserters", 1.0, NomadDisposition::Wary, 17000.0, 12, 90, 2.2,
     {kSeeks + 3, 3}, {kCarries + 1, 2},
     {.war_reach_spacings = 0.8, .min_ruggedness = 0.32}},
    {"cultists", 0.8, NomadDisposition::Devout, 13000.0, 15, 140, 1.8,
     {kSeeks + 6, 3}, {kCarries + 3, 2},
     {.min_ley_intensity = 2.0, .ley_reach_spacings = 0.35, .shrine_reach_spacings = 0.8, .claim_reach_spacings = 1.2}},
    {"merchants", 1.3, NomadDisposition::Tradeable, 24000.0, 8, 70, 6.0,
     {kSeeks + 9, 3}, {kCarries + 5, 3},
     {.road_reach_spacings = 0.35, .market_reach_spacings = 3.0, .min_markets = 2, .min_market_population = 2000.0}},
    {"bandits", 1.2, NomadDisposition::Hostile, 19000.0, 10, 80, 2.0,
     {kSeeks + 12, 3}, {kCarries + 8, 1},
     {.min_ruggedness = 0.34, .strike_reach_spacings = 1.3, .control_clear_spacings = 0.9}},
    {"wanderers", 2.0, NomadDisposition::Wary, 15000.0, 40, 420, 3.2,
     {kSeeks + 15, 3}, {kCarries + 9, 3},
     {.min_forage = 0.07, .min_seasonal_swing = 0.3, .summer_reach_spacings = 3.5, .winter_reach_spacings = 3.5}},
};

// `min_suitability` is carried for fidelity of the data file and is NEVER READ: no
// nomad code in the reference consults it, and inventing a suitability floor would
// change every world's band count against no reference behaviour at all.
constexpr NomadPolicy kPolicy = {
    2,
    {18.0, 0.3, 0.12},
    {{kFissionClasses, 1}, 0.55, 0.3, 0.45},
    {kClasses, 6},
};

}  // namespace

const NomadPolicy& nomad_policy() { return kPolicy; }

}  // namespace fwg
