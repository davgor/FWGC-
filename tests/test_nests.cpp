// Where the creatures live, and why the ones that are missing are missing.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fwg/nests.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

// One settled thing every `every` land nodes. The settlement stages are not ported, so
// the caller supplies them -- which is also the only way to test the clearance and the
// ramp against a KNOWN set of people rather than whatever a founding pass happened to do.
std::vector<int> land_nodes(const World& w, const SphereGrid& g, int every) {
    std::vector<int> out;
    const auto water = w.layers.find("water_type");
    for (std::size_t i = 0; i < g.node_count(); ++i) {
        const bool land = water == w.layers.end() ||
                          water->second.at(g.point_x[i], g.point_z[i]) == 0.0;
        if (land && i % static_cast<std::size_t>(every) == 0) out.push_back(static_cast<int>(i));
    }
    return out;
}

struct Populated {
    World world;
    std::shared_ptr<const SphereGrid> grid;
    NestHabitat field;
    std::vector<int> settled;
    NestPasses passes;
};

Populated populate(std::uint32_t seed, int size, int phase,
                   const std::map<std::string, double>& options = {}, int settle_every = 97) {
    Request r;
    r.seed = seed;
    r.size = size;
    r.phase = phase;
    r.options = options;
    Populated p;
    p.world = generate(resolve(r));
    // effective, not config: config is design space and every reach in a nest pass -- the
    // clearance, the territory radius, the ramp span -- is absolute metres.
    p.grid = sphere_grid(p.world.config.size, p.world.effective.globe_radius);
    p.field = habitat_cells(p.world, *p.grid);
    p.settled = land_nodes(p.world, *p.grid, settle_every);
    p.passes = *add_nests(p.world, *p.grid, p.settled);
    return p;
}

// The world every test that does not need its own reads. Built once: the brute-force
// checks below are the expensive part of this file, not the generation.
const Populated& reference() {
    static const Populated p = populate(42, 65, 10);
    return p;
}

const CreatureProfile* by_id(std::string_view id) {
    for (const CreatureProfile& p : creature_profiles()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

int field_id(std::string_view name) {
    const auto names = nest_field_names();
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return static_cast<int>(i);
    }
    return -1;
}

double great_circle(const Vec3& a, const Vec3& b, double radius) {
    return radius * std::acos(clamp_unit(dot(a, b)));
}

// A cell that suits nothing, which a test then bends one field of at a time.
struct Probe {
    std::vector<double> values;
    HabitatCell cell;
};

Probe probe_cell(CellMedium medium, double temperature_c) {
    Probe p;
    p.values.assign(nest_field_names().size(), 0.0);
    p.cell.medium = medium;
    p.cell.temperature_c = temperature_c;
    p.cell.fields = p.values;
    return p;
}

}  // namespace

// --- the authored catalogue ------------------------------------------------

TEST(the_catalogue_is_the_authored_one_and_its_order_is_the_contract) {
    // The order is not decoration. _place builds a per-cell candidate list in catalogue
    // order, float-sums the rates over it and walks it cumulatively, so reordering the
    // same rows is a different world on the same seed.
    //
    // The authored file is strictly ascending by id, so "ascending" plus the counts pins
    // the order uniquely: no other arrangement of this id set satisfies both.
    const auto animals = creature_catalogue(CreatureClass::Animal);
    const auto monsters = creature_catalogue(CreatureClass::Monster);
    check::equal(creature_profiles().size(), std::size_t{680}, "the whole authored catalogue");
    check::equal(animals.size(), std::size_t{311}, "the animal half");
    check::equal(monsters.size(), std::size_t{369}, "the monster half");
    check::equal(creature_roles().size(), std::size_t{21}, "the feeding roles");

    for (std::size_t i = 1; i < animals.size(); ++i) {
        check::is_true(animals[i - 1].id < animals[i].id,
                       std::string("animals to keep file order at ") + std::string(animals[i].id));
    }
    for (std::size_t i = 1; i < monsters.size(); ++i) {
        check::is_true(monsters[i - 1].id < monsters[i].id,
                       std::string("monsters to keep file order at ") + std::string(monsters[i].id));
    }
    check::equal(std::string(animals.front().id), std::string("aardvark"), "the first animal");
    check::equal(std::string(monsters.front().id), std::string("absence-titans"), "the first monster");

    // Both halves are one contiguous block, animals first, so a pass takes a span and
    // never a filtered copy.
    check::is_true(animals.data() == creature_profiles().data(), "animals to lead the table");
    check::is_true(monsters.data() == creature_profiles().data() + animals.size(),
                   "monsters to follow them");

    for (const CreatureProfile& p : animals) {
        check::is_true(!p.role.empty(), std::string(p.id) + " to carry a feeding role");
        check::equal(p.creature_class == CreatureClass::Animal, true, std::string(p.id) + " to be an animal");
    }
    for (const CreatureProfile& p : monsters) {
        check::is_true(p.role.empty(), std::string(p.id) + " to have no feeding role");
    }
    for (const CreatureProfile& p : creature_profiles()) {
        check::between(p.tier, 1, 5, std::string(p.id) + " tier");
        check::is_true(!p.weights.empty(), std::string(p.id) + " to score on something");
        check::is_true(p.weight_total > 0.0, std::string(p.id) + " to have a positive weight total");
        check::equal(p.biome_weights.size(), std::size_t{kNestBiomeCount},
                     std::string(p.id) + " to carry a dense biome table");
        bool any_ground = false;
        for (const double w : p.biome_weights) any_ground = any_ground || w > 0.0;
        check::is_true(any_ground, std::string(p.id) + " to name at least one biome it uses");
    }
}

TEST(the_catalogue_carries_the_fields_a_nest_pass_never_reads) {
    // movement, sky and the art references are read by beast movement, encounters and the
    // asset list, which get them from the block's published catalogue and from nowhere
    // else. A port that kept only what it needed itself would look complete and quietly
    // delete the data the next three stages depend on -- so the floor here is a count,
    // not a spot check.
    long long moving = 0, flying = 0, referenced = 0, traited = 0, secondary = 0;
    for (const CreatureProfile& p : creature_profiles()) {
        check::is_true(!p.movement.empty(), std::string(p.id) + " to carry a movement mode");
        if (p.movement != "nester") ++moving;
        if (p.sky) ++flying;
        if (p.creature_class == CreatureClass::Monster) {
            check::is_true(!p.likeness.empty(), std::string(p.id) + " to carry an art reference");
            ++referenced;
        } else {
            // An animal is its own likeness.
            check::is_true(p.likeness.empty() && p.likeness_traits.empty(),
                           std::string(p.id) + " to need no art reference");
        }
        traited += static_cast<long long>(p.likeness_traits.size());
        if (!p.likeness_secondary.empty()) ++secondary;
    }
    check::is_true(moving > 50, "creatures that do not stay put to have survived the port, got " +
                                    std::to_string(moving));
    check::is_true(flying > 10, "creatures of the air to have survived it, got " + std::to_string(flying));
    check::equal(referenced, 369LL, "every monster to carry its art reference");
    check::is_true(traited > 200, "the trait lists to have survived it, got " + std::to_string(traited));
    check::is_true(secondary > 0, "the secondary references to have survived it");
}

TEST(a_weight_row_keeps_its_file_order_rather_than_being_sorted) {
    // The score is a float sum over the terms, so a map that reorders the keys is a
    // different world. 272 of the 680 rows author their weights out of alphabetical
    // order, and the count below is the guard: if the table were ever sorted on the way
    // in, this drops to zero and says so.
    long long out_of_order = 0;
    for (const CreatureProfile& p : creature_profiles()) {
        for (std::size_t i = 1; i < p.weights.size(); ++i) {
            if (p.weights[i].field < p.weights[i - 1].field) {
                ++out_of_order;
                break;
            }
        }
    }
    check::is_true(out_of_order > 100,
                   "many rows to keep an unsorted authored order, got " + std::to_string(out_of_order));

    const CreatureProfile* acorn = by_id("acorn-folk");
    check::is_true(acorn != nullptr, "acorn-folk to be in the catalogue");
    check::equal(acorn->weights.size(), std::size_t{2}, "acorn-folk to score on two fields");
    check::equal(std::string(nest_field_names()[static_cast<std::size_t>(acorn->weights[0].field)]),
                 std::string("moisture"), "acorn-folk's first authored weight");
    check::equal(std::string(nest_field_names()[static_cast<std::size_t>(acorn->weights[1].field)]),
                 std::string("ley_weave"), "acorn-folk's second authored weight");
}

TEST(suitability_is_a_weight_normalised_mean_so_scaling_a_row_changes_nothing) {
    // sum(clamp(field) * w) / sum(w), never a raw sum. A row whose weights are all
    // doubled is the same animal, and a reader who treats the weights as a sum would put
    // it everywhere instead.
    const CreatureProfile* base = by_id("acorn-folk");
    const int moisture = field_id("moisture");
    const int weave = field_id("ley_weave");
    check::is_true(moisture >= 0 && weave >= 0, "the two fields acorn-folk scores on to exist");

    Probe probe = probe_cell(CellMedium::Land, 10.0);
    probe.values[static_cast<std::size_t>(moisture)] = 0.5;
    probe.values[static_cast<std::size_t>(weave)] = 1.0;

    std::vector<HabitatTerm> scaled;
    double total = 0.0;
    for (const HabitatTerm& t : base->weights) {
        scaled.push_back({t.field, t.value * 7.0});
        total += t.value * 7.0;
    }
    CreatureProfile loud = *base;
    loud.weights = scaled;
    loud.weight_total = total;
    loud.temperature_min_c = -100.0;
    loud.temperature_max_c = 100.0;

    CreatureProfile quiet = *base;
    quiet.temperature_min_c = -100.0;
    quiet.temperature_max_c = 100.0;

    const double a = suitability(quiet, probe.cell);
    const double b = suitability(loud, probe.cell);
    check::is_true(a > 0.0, "the unscaled row to score something on this ground");
    check::close(b, a, 1e-12, "a row whose every weight is scaled to score the same");
    check::between(a, 0.0, 1.0, "a score to stay a 0..1 mean");

    // And a field the world does not carry reads zero rather than erroring, which drops
    // the term instead of the species.
    Probe dry = probe_cell(CellMedium::Land, 10.0);
    check::equal(suitability(quiet, dry.cell), 0.0, "a row scoring only on absent fields to score zero");
}

TEST(a_requires_gate_reads_the_raw_field_and_fails_closed) {
    // `depth: 30` means thirty metres down, not a 0..1 preference -- the requirement
    // tests the unclamped value while a weight tests the clamped one. And a field the
    // world does not carry reads 0, so a positive threshold on it can never be met.
    const CreatureProfile* deep = by_id("abyssal-choir-worms");
    check::is_true(deep != nullptr, "abyssal-choir-worms to be in the catalogue");
    const int depth = field_id("depth");
    bool wants_metres = false;
    for (const HabitatTerm& t : deep->requirements) {
        if (t.field == depth) wants_metres = t.value > 1.0;
    }
    check::is_true(wants_metres, "a profile to require a depth in metres, not a 0..1 fraction");

    Probe shallow = probe_cell(CellMedium::Marine, 4.0);
    for (const HabitatTerm& t : deep->requirements) {
        shallow.values[static_cast<std::size_t>(t.field)] = t.value * 0.5;
    }
    check::equal(suitability(*deep, shallow.cell), 0.0, "half of every threshold to refuse the cell");

    Probe met = probe_cell(CellMedium::Marine, 4.0);
    for (const HabitatTerm& t : deep->requirements) {
        met.values[static_cast<std::size_t>(t.field)] = t.value;
    }
    for (const HabitatTerm& t : deep->weights) {
        met.values[static_cast<std::size_t>(t.field)] = std::max(met.values[static_cast<std::size_t>(t.field)], 1.0);
    }
    check::is_true(suitability(*deep, met.cell) > 0.0,
                   "the thresholds met exactly to admit the cell, or the gate is untestable");
}

TEST(the_temperature_band_is_a_hard_gate_and_a_missing_layer_fails_closed) {
    const CreatureProfile* p = by_id("aardvark");
    check::is_true(p != nullptr, "aardvark to be in the catalogue");
    const int plains = field_id("plains");
    const double lo = p->temperature_min_c;
    const double hi = p->temperature_max_c;
    check::is_true(lo < hi, "aardvark to have a band at all");

    const auto score_at = [&](double t) {
        Probe probe = probe_cell(CellMedium::Land, t);
        probe.values[static_cast<std::size_t>(plains)] = 1.0;
        return suitability(*p, probe.cell);
    };
    check::is_true(score_at((lo + hi) / 2.0) > 0.0, "the middle of the band to be habitable");
    check::is_true(score_at(lo) > 0.0, "the band to be inclusive at the bottom");
    check::is_true(score_at(hi) > 0.0, "the band to be inclusive at the top");
    check::equal(score_at(lo - 0.001), 0.0, "a hair below the band to be refused");
    check::equal(score_at(hi + 0.001), 0.0, "a hair above the band to be refused");

    // A world with no temperature layer reports 100 C, which is above every authored
    // upper bound in the catalogue, so it places nothing rather than everything.
    double highest = -1000.0;
    for (const CreatureProfile& q : creature_profiles()) highest = std::max(highest, q.temperature_max_c);
    check::is_true(highest < 100.0,
                   "the missing-layer temperature to sit above every authored band, highest is " +
                       std::to_string(highest));
    check::equal(score_at(100.0), 0.0, "a world with no temperature layer to place nothing");
}

// --- the ground ------------------------------------------------------------

TEST(habitat_cells_derives_the_seven_fields_the_catalogue_scores_on) {
    const Populated& p = reference();
    const int depth = field_id("depth");
    const int dryness = field_id("dryness");
    const int coast = field_id("coast");
    const int mountain = field_id("mountain");
    const int plains = field_id("plains");
    const int wetland = field_id("wetland");
    const int cold = field_id("cold");
    for (const int id : {depth, dryness, coast, mountain, plains, wetland, cold}) {
        check::is_true(id >= 0, "every derived field to be interned");
    }

    const Grid& moisture = p.world.layers.at("moisture");
    const Grid& maritime = p.world.layers.at("maritime");
    const Grid& water_depth = p.world.layers.at("water_depth");
    long long wet_ground = 0;
    long long steep_ground = 0;
    std::set<int> media;
    for (const HabitatCell& c : p.field.cells) {
        media.insert(static_cast<int>(c.medium));
        const double m = moisture.at(c.x, c.z);
        check::close(c.fields[static_cast<std::size_t>(dryness)], 1.0 - std::max(0.0, std::min(1.0, m)),
                     1e-12, "dryness to be the complement of clamped moisture");
        check::close(c.fields[static_cast<std::size_t>(coast)], maritime.at(c.x, c.z), 1e-12,
                     "coast to be the maritime layer");
        check::close(c.coast, c.fields[static_cast<std::size_t>(coast)], 1e-12,
                     "the named coast to agree with the interned one");
        // Depth is RAW metres, not a 0..1 score: the requirement thresholds read it that way.
        check::close(c.fields[static_cast<std::size_t>(depth)], water_depth.at(c.x, c.z), 1e-12,
                     "depth to be the unclamped water depth in metres");
        for (const int id : {dryness, mountain, plains, wetland, cold}) {
            check::between(c.fields[static_cast<std::size_t>(id)], 0.0, 1.0,
                           "a derived 0..1 field to stay in range");
        }
        if (c.medium != CellMedium::Land) {
            check::equal(c.fields[static_cast<std::size_t>(wetland)], 1.0,
                         "open water to be wetland outright");
        }
        if (c.fields[static_cast<std::size_t>(wetland)] > 0.5) ++wet_ground;
        if (c.fields[static_cast<std::size_t>(mountain)] > 0.2) ++steep_ground;
    }
    // Floors: a field that is constant across the whole world tests nothing.
    check::is_true(wet_ground > 0, "this world to hold wetland");
    check::is_true(steep_ground > 0, "this world to hold mountain");
    check::is_true(media.size() >= 3, "this world to hold more than one kind of ground");
    check::equal(p.field.cells.size(), p.grid->node_count(), "one cell per node");
    for (std::size_t i = 0; i < p.field.cells.size(); ++i) {
        check::equal(p.field.cells[i].node, static_cast<int>(i), "cells to be in node order");
    }
}

TEST(nothing_lives_in_a_salt_lake) {
    // habitat_cells labels a saline lake `salt_lake`, which is not one of the four media a
    // profile may declare, so no species can ever match one. The area is nevertheless
    // counted into the water budget and thins every marine and freshwater species
    // slightly -- inherited deliberately, and recorded here rather than quietly fixed.
    const Populated& p = reference();
    std::vector<std::size_t> salt;
    for (std::size_t i = 0; i < p.field.cells.size(); ++i) {
        if (p.field.cells[i].medium == CellMedium::SaltLake) salt.push_back(i);
    }
    check::is_true(!salt.empty(), "this world to hold a salt lake, or the rule is untested");

    for (const std::size_t at : salt) {
        for (const CreatureProfile& q : creature_profiles()) {
            check::equal(suitability(q, p.field.cells[at]), 0.0,
                         std::string(q.id) + " to find no ground in a salt lake");
        }
    }
    for (const NestBlock* b : {&p.passes.wildlife, &p.passes.beast_nests}) {
        for (const NestSite& s : b->sites) {
            check::is_true(p.field.cells[static_cast<std::size_t>(s.node)].medium != CellMedium::SaltLake,
                           s.id + " not to stand in a salt lake");
        }
    }
}

TEST(the_medium_gate_keeps_land_and_water_apart_and_shore_is_a_land_medium) {
    const Populated& p = reference();
    long long shore_cells = 0;
    long long dry_land = 0;
    for (const HabitatCell& c : p.field.cells) {
        if (c.medium != CellMedium::Land) continue;
        if (c.coast >= 0.1) ++shore_cells;
        else ++dry_land;
    }
    check::is_true(shore_cells > 0, "this world to hold coastal refuge somewhere");
    check::is_true(dry_land > 0, "this world to hold land without one");

    const CreatureProfile* shore = nullptr;
    for (const CreatureProfile& q : creature_profiles()) {
        if (q.medium == CreatureMedium::Shore) shore = &q;
    }
    check::is_true(shore != nullptr, "the catalogue to author a shore species");

    bool refused_inland = false;
    for (const HabitatCell& c : p.field.cells) {
        if (c.medium == CellMedium::Land && c.coast < 0.1) {
            check::equal(suitability(*shore, c), 0.0,
                         std::string(shore->id) + " to refuse land with no coastal refuge");
            refused_inland = true;
        }
        if (c.medium == CellMedium::Marine || c.medium == CellMedium::Freshwater) {
            check::equal(suitability(*shore, c), 0.0,
                         std::string(shore->id) + " never to match open water");
        }
    }
    check::is_true(refused_inland, "the coastal gate to have been exercised");

    // And every site that was placed stands on ground its profile declares.
    long long by_medium[4] = {};
    for (const NestBlock* b : {&p.passes.wildlife, &p.passes.beast_nests}) {
        for (const NestSite& s : b->sites) {
            const CreatureProfile* q = by_id(s.species_id);
            const CellMedium at = p.field.cells[static_cast<std::size_t>(s.node)].medium;
            const CellMedium want = q->medium == CreatureMedium::Marine       ? CellMedium::Marine
                                    : q->medium == CreatureMedium::Freshwater ? CellMedium::Freshwater
                                                                              : CellMedium::Land;
            check::equal(static_cast<int>(at), static_cast<int>(want),
                         s.id + " to stand on the medium it declares");
            ++by_medium[static_cast<int>(q->medium)];
        }
    }
    check::is_true(by_medium[static_cast<int>(CreatureMedium::Land)] > 0, "land creatures to be placed");
    check::is_true(by_medium[static_cast<int>(CreatureMedium::Marine)] > 0, "sea creatures to be placed");
}

TEST(an_absent_biome_key_is_exclusion_rather_than_neutrality) {
    // A species' table is sparse and an absent biome is ground it does not use, so a
    // five-key table forbids a quarter of the map rather than being indifferent to it.
    const Populated& p = reference();
    std::set<int> live;
    for (const HabitatCell& c : p.field.cells) live.insert(c.biome);
    check::is_true(live.size() >= 5, "this world to carry several biomes");

    long long excluded = 0;
    for (const CreatureProfile& q : creature_catalogue(CreatureClass::Animal)) {
        for (const int b : live) {
            if (biome_weight(q, b) <= 0.0) ++excluded;
        }
    }
    // The floor. If every table named every live biome the rule would be unobservable --
    // which is in fact true of the monster half, whose tables all name all eleven live
    // ids, so the animal half is where this can be tested at all.
    check::is_true(excluded > 0,
                   "some species to be excluded from some live biome, got " + std::to_string(excluded));

    for (const NestBlock* b : {&p.passes.wildlife, &p.passes.beast_nests}) {
        for (const NestSite& s : b->sites) {
            check::is_true(biome_weight(*by_id(s.species_id), s.biome) > 0.0,
                           s.id + " to stand on a biome its own table names");
        }
    }
    // A biome id the roster does not contain is ground nothing uses, not ground everything
    // uses: the fallback is exclusion at both ends of the range.
    check::equal(biome_weight(*by_id("aardvark"), -1), 0.0, "a negative biome id to be excluded");
    check::equal(biome_weight(*by_id("aardvark"), kNestBiomeCount), 0.0,
                 "a biome id past the roster to be excluded");
}

// --- the arithmetic --------------------------------------------------------

TEST(the_danger_ramp_slides_danger_outward_from_people_and_makes_nothing_impossible) {
    const double span = 5000.0;
    // Tier one is drawn towards the doorstep, tier five away from it, and they cross once.
    check::is_true(danger_ramp(1, 0.0, span) > danger_ramp(1, span, span), "tier one to prefer settled ground");
    check::is_true(danger_ramp(1, span, span) > danger_ramp(1, 10.0 * span, span), "and to keep falling");
    check::is_true(danger_ramp(5, 0.0, span) < danger_ramp(5, span, span), "tier five to prefer the wilderness");
    check::is_true(danger_ramp(5, span, span) < danger_ramp(5, 10.0 * span, span), "and to keep rising");

    // Tier three is the pivot: it is (1 + floor)/2 at every distance, so a five-step
    // danger ladder is not something this mechanism can make at any tuning.
    const double pivot = (1.0 + kNestDangerFloor) / 2.0;
    for (const double d : {0.0, 1.0, span, 1000.0 * span}) {
        check::close(danger_ramp(3, d, span), pivot, 1e-12, "tier three to sit at the pivot");
    }

    // Nothing may become impossible. A floor of zero would make a tier five species
    // unplaceable on a town's doorstep and a tier one species unplaceable in the
    // wilderness, and neither is true.
    for (int tier = 1; tier <= 5; ++tier) {
        for (const double d : {0.0, 1.0, span, 1000.0 * span}) {
            check::between(danger_ramp(tier, d, span), kNestDangerFloor, 1.0, "the ramp to stay in band");
        }
        // With nothing settled the distance is infinite, which must be a number rather
        // than a NaN: the block is published and a NaN in it is a broken document.
        const double empty = danger_ramp(tier, std::numeric_limits<double>::infinity(), span);
        check::is_true(std::isfinite(empty), "an unsettled world to give the ramp a finite value");
        check::close(empty, kNestDangerFloor + (1.0 - kNestDangerFloor) * (tier - 1) / 4.0, 1e-12,
                     "an unsettled world to leave only the tier term");
    }
}

TEST(the_tier_pyramid_falls_geometrically_rather_than_stepping) {
    check::close(tier_density(4.5, 4.0, 1), 4.5, 1e-12, "tier one to be the base");
    check::close(tier_density(4.5, 4.0, 2), 1.125, 1e-12, "tier two to be a quarter of it");
    check::close(tier_density(4.5, 4.0, 5), 4.5 / 256.0, 1e-12, "tier five to be four steps down");
    for (int tier = 2; tier <= 5; ++tier) {
        check::close(tier_density(0.53, 2.0, tier) * 2.0, tier_density(0.53, 2.0, tier - 1), 1e-15,
                     "each tier to be exactly one falloff below the one under it");
    }
}

TEST(nearest_settled_is_the_minimum_over_every_settled_thing) {
    // The published form is an argmax with a latitude stopping rule, so that the scan can
    // stop early. It has to agree with the honest minimum at every cell, and the slack on
    // the stopping bound is ADDED so the walk goes one candidate too far rather than one
    // too few -- subtracting it loses the argmax on a tie.
    const Populated& p = reference();
    const double radius = p.world.effective.globe_radius;
    std::vector<Vec3> settled;
    for (const int node : p.settled) {
        settled.push_back(p.field.cells[static_cast<std::size_t>(node)].direction);
    }
    check::is_true(settled.size() > 1, "more than one settled thing, or the walk is trivial");

    const std::vector<double> fast = nearest_settled(p.field.cells, settled, radius);
    check::equal(fast.size(), p.field.cells.size(), "one distance per cell");
    double widest = 0.0;
    long long zeroes = 0;
    for (std::size_t i = 0; i < p.field.cells.size(); ++i) {
        double slow = std::numeric_limits<double>::infinity();
        for (const Vec3& s : settled) slow = std::min(slow, great_circle(p.field.cells[i].direction, s, radius));
        check::close(fast[i], slow, 1e-6, "the latitude walk to agree with the honest minimum");
        widest = std::max(widest, fast[i]);
        // Not exactly zero: a unit vector's dot with itself lands a few ulp under one and
        // acos turns that into a fraction of a millimetre. The raster's finest step is
        // over a hundred metres, so no other cell can be this close.
        if (fast[i] < 1e-3) ++zeroes;
    }
    check::equal(zeroes, static_cast<long long>(settled.size()),
                 "a settled cell to be zero from itself");
    check::is_true(widest > 1000.0, "some ground to be genuinely far from people");

    // Nothing settled is a legal world, and every cell is then infinitely far from people.
    const std::vector<double> empty = nearest_settled(p.field.cells, {}, radius);
    for (const double d : empty) check::is_true(std::isinf(d), "an unsettled world to be all infinity");
}

// --- placement -------------------------------------------------------------

TEST(a_bucket_never_holds_two_sites_inside_one_anothers_territory) {
    // The refusal rule, re-derived by brute force. The pass answers it with a same-cell
    // lookup and then a bound on the rows a reach can cross; this compares every pair in
    // every bucket, which is the predicate the optimised scan claims to compute. A row
    // band that was one row too narrow would show up here and nowhere else.
    const Populated& p = reference();
    const double radius = p.world.effective.globe_radius;

    std::map<std::string, std::vector<const NestSite*>> by_species;
    for (const NestSite& s : p.passes.wildlife.sites) by_species[std::string(s.species_id)].push_back(&s);
    std::map<int, std::vector<const NestSite*>> by_tier;
    for (const NestSite& s : p.passes.beast_nests.sites) by_tier[s.tier].push_back(&s);

    long long compared = 0;
    const auto no_overlap = [&](const std::vector<const NestSite*>& bucket, const std::string& what) {
        for (std::size_t i = 0; i < bucket.size(); ++i) {
            for (std::size_t j = i + 1; j < bucket.size(); ++j) {
                const double d = great_circle(bucket[i]->direction, bucket[j]->direction, radius);
                ++compared;
                check::is_true(d >= std::max(bucket[i]->range_m, bucket[j]->range_m) - 1e-9,
                               what + ": " + bucket[i]->id + " and " + bucket[j]->id +
                                   " to hold their distance");
            }
        }
    };
    for (const auto& [id, bucket] : by_species) no_overlap(bucket, "a species over itself");
    for (const auto& [tier, bucket] : by_tier) no_overlap(bucket, "a monster tier over itself");
    check::is_true(compared > 10000, "many pairs to have been compared, got " + std::to_string(compared));

    // ...and the other half of the rule: nothing holds ground against another bucket. A
    // dragon's range may contain a goblin camp, and a wolf range crosses a deer range.
    long long crossing_tiers = 0;
    for (const NestSite& a : p.passes.beast_nests.sites) {
        for (const NestSite& b : p.passes.beast_nests.sites) {
            if (a.tier == b.tier) continue;
            if (great_circle(a.direction, b.direction, radius) < std::max(a.range_m, b.range_m)) {
                ++crossing_tiers;
            }
        }
    }
    check::is_true(crossing_tiers > 0,
                   "a lesser lair to sit inside a greater monster's territory somewhere");
}

TEST(a_site_is_a_centre_a_radius_and_a_recomputed_identity) {
    const Populated& p = reference();
    const double spacing = p.world.effective.options[Option::nest_spacing];
    long long dens = 0, ranges = 0, animals = 0, monsters = 0;
    for (const NestBlock* b : {&p.passes.wildlife, &p.passes.beast_nests}) {
        check::is_true(!b->sites.empty(), "a pass to have placed something");
        for (const NestSite& s : b->sites) {
            const CreatureProfile* q = by_id(s.species_id);
            check::is_true(q != nullptr, s.id + " to name a catalogue species");
            // spacing_m is a DIAMETER; the placed territory is half of it.
            check::close(s.range_m, q->spacing_m * spacing / 2.0, 1e-12,
                         s.id + " range to be half the authored diameter");
            check::is_true(s.range_m > 0.0, s.id + " to hold some ground");
            ++ranges;
            // `real` is recomputed here, NOT copied from the row: it means "is a
            // real-world animal", not "is valid".
            check::equal(s.real, q->creature_class == CreatureClass::Animal, s.id + " real flag");
            check::equal(s.den, s.kind == "den" || s.kind == "lair", s.id + " den flag");
            if (s.den) ++dens;
            check::equal(s.suitability >= p.world.effective.options[Option::nest_min_suitability], true,
                         s.id + " to have cleared the suitability floor");
            check::between(s.suitability, 0.0, 1.0, s.id + " suitability");
            const HabitatCell& cell = p.field.cells[static_cast<std::size_t>(s.node)];
            check::equal(s.x, cell.x, s.id + " to carry its cell's column");
            check::equal(s.z, cell.z, s.id + " to carry its cell's row");
            check::equal(s.biome, cell.biome, s.id + " to carry its cell's natural biome");
            check::close(dot(s.direction, cell.direction), 1.0, 1e-12,
                         s.id + " to sit at its cell's centre");
            check::equal(s.id, std::string(b == &p.passes.wildlife ? "animal" : "nest") + "-" +
                                   std::string(s.species_id) + "-" + std::to_string(s.node),
                         "the published id form");
            if (s.creature_class == CreatureClass::Animal) ++animals; else ++monsters;
            check::equal(s.role.empty(), s.creature_class == CreatureClass::Monster,
                         s.id + " to carry a role only if it is an animal");
        }
    }
    check::is_true(dens > 0 && ranges > dens, "both dens and open ranges to have been placed");
    check::is_true(animals > 0 && monsters > 0, "both passes to have produced sites");
}

TEST(sites_are_published_in_lexicographic_id_order) {
    // A LEXICOGRAPHIC string sort, so "animal-wolf-1000" sorts before "animal-wolf-99".
    // The second check is the floor: it finds a neighbouring pair that a numeric sort
    // would have ordered the other way, so a "fix" to numeric ordering fails here.
    const Populated& p = reference();
    for (const NestBlock* b : {&p.passes.wildlife, &p.passes.beast_nests}) {
        long long disagrees_with_numeric = 0;
        for (std::size_t i = 1; i < b->sites.size(); ++i) {
            check::is_true(b->sites[i - 1].id < b->sites[i].id, "sites to be ordered by id");
            if (b->sites[i - 1].species_id == b->sites[i].species_id &&
                b->sites[i - 1].node > b->sites[i].node) {
                ++disagrees_with_numeric;
            }
        }
        check::is_true(disagrees_with_numeric > 0,
                       "the string sort to disagree with a numeric one somewhere, or it is untested");
    }
}

TEST(the_settlement_clearance_is_a_hard_floor_in_proportion_to_danger) {
    const Populated& p = reference();
    const double radius = p.world.effective.globe_radius;
    const double base = p.world.effective.options[Option::nest_settlement_clearance];
    check::is_true(base > 0.0 && !p.settled.empty(), "a clearance and some people to keep clear of");

    std::vector<Vec3> settled;
    for (const int node : p.settled) settled.push_back(p.field.cells[static_cast<std::size_t>(node)].direction);
    const std::vector<double> nearest = nearest_settled(p.field.cells, settled, radius);

    // The floor: the clearance has to have refused something, or the check below passes
    // on a world where no cell was ever close enough to be refused.
    long long refusable = 0;
    for (const double d : nearest) {
        if (d < base * 5.0) ++refusable;
    }
    check::is_true(refusable > 0, "some ground to be inside the widest clearance");

    for (const NestSite& s : p.passes.wildlife.sites) {
        check::is_true(nearest[static_cast<std::size_t>(s.node)] >= base * s.tier / 3.0,
                       s.id + " to keep an animal's distance from people");
    }
    for (const NestSite& s : p.passes.beast_nests.sites) {
        check::is_true(nearest[static_cast<std::size_t>(s.node)] >= base * s.tier,
                       s.id + " to keep a monster's distance from people");
    }
}

TEST(the_rate_is_a_budget_for_the_world_and_the_pyramid_is_exact_in_it) {
    // The expected number of groups of tier t is tier_density(t) times the ground of its
    // own medium, and nothing else: suitability, biome weight, occurrence and the ramp all
    // decide only WHERE the groups land and WHICH species claims each one. That makes the
    // drawn histogram a prediction rather than an observation, which is what this checks.
    const Populated& p = reference();
    double total_km2 = 0.0;
    for (const double a : p.grid->areas) total_km2 += a / 1e6;
    check::is_true(total_km2 > 1000.0, "a world with some ground on it");

    const WorldOptions& o = p.world.effective.options;
    struct Pass {
        const NestBlock* block;
        double density;
        double falloff;
        const char* what;
    };
    const Pass passes[] = {
        {&p.passes.wildlife, o[Option::animal_density] * o[Option::nest_density],
         o[Option::animal_tier_falloff], "animals"},
        {&p.passes.beast_nests,
         o[Option::monster_density] * o[Option::nest_density] * o[Option::nest_fantasy],
         o[Option::monster_tier_falloff], "monsters"}};

    long long checked = 0;
    for (const Pass& pass : passes) {
        for (const NestSaturation& s : pass.block->saturation) {
            const double predicted = tier_density(pass.density, pass.falloff, s.tier) * total_km2;
            if (predicted < 100.0) continue;  // too few to be a measurement
            ++checked;
            const double ratio = static_cast<double>(s.drawn) / predicted;
            check::between(ratio, 0.8, 1.2,
                           std::string(pass.what) + " tier " + std::to_string(s.tier) +
                               " to draw its budget: predicted " + std::to_string(predicted) +
                               ", drew " + std::to_string(s.drawn));
        }
    }
    check::is_true(checked >= 6, "several tiers to have been predicted, got " + std::to_string(checked));
}

TEST(the_creature_count_follows_the_ground_and_not_the_raster) {
    // This is a thinned point process over the ground, not a fixed anchor budget: the
    // expected count is an integral over habitable land, so doubling the land doubles the
    // creatures and refining the raster changes nothing. A per-cell budget -- the obvious
    // wrong shape -- would do the opposite of both.
    //
    // What is PLACED cannot show this, because the raster clips it; `drawn` is the rate
    // and the rate is what the claim is about.
    const auto drawn_total = [](const NestBlock& b) {
        long long total = 0;
        for (const NestSaturation& s : b.saturation) total += s.drawn;
        return total;
    };

    const Populated& coarse = reference();
    const long long here = drawn_total(coarse.passes.beast_nests);
    check::is_true(here > 1000, "enough lairs drawn to measure, got " + std::to_string(here));

    // Same world, four times the cells. This also stands in for cost: a naive refusal
    // scan is quadratic in the pass and would not finish here.
    Request fine;
    fine.seed = 42;
    fine.size = 129;
    fine.phase = 10;
    const World finer = generate(resolve(fine));
    const auto fine_grid = sphere_grid(finer.config.size, finer.effective.globe_radius);
    const long long refined = drawn_total(add_nests(finer, *fine_grid, {})->beast_nests);
    check::is_true(fine_grid->node_count() > 3 * coarse.grid->node_count(),
                   "the finer raster to actually be finer");
    check::between(static_cast<double>(refined) / static_cast<double>(here), 0.85, 1.15,
                   "four times the cells to hold the same creatures: " + std::to_string(here) +
                       " against " + std::to_string(refined));

    // Twice as wide is four times the ground, so four times the creatures.
    Request wide;
    wide.seed = 42;
    wide.size = 65;
    wide.phase = 10;
    wide.circumference_km = 400.0;
    const World broad = generate(resolve(wide));
    const auto broad_grid = sphere_grid(broad.config.size, broad.effective.globe_radius);
    const long long bigger = drawn_total(add_nests(broad, *broad_grid, {})->beast_nests);
    check::between(static_cast<double>(bigger) / static_cast<double>(here), 3.4, 4.6,
                   "four times the ground to hold four times the creatures: " +
                       std::to_string(here) + " against " + std::to_string(bigger));
}

TEST(the_pyramid_is_exact_in_the_rate_and_clipped_in_what_is_placed) {
    // A flat placed histogram is the raster, not the ecology, and the block publishes both
    // halves so a reader cannot mistake one for the other.
    const Populated& p = reference();
    // How many sites a tier can hold per cell, which is how many BUCKETS it spans: one for
    // a monster tier, because the bucket IS the tier, and one per species for an animal
    // tier, because a wolf range over a deer range is the point of the map.
    std::map<int, long long> animal_species_per_tier;
    for (const CreatureProfile& q : creature_catalogue(CreatureClass::Animal)) {
        ++animal_species_per_tier[q.tier];
    }

    for (const NestBlock* b : {&p.passes.wildlife, &p.passes.beast_nests}) {
        const bool monsters = b == &p.passes.beast_nests;
        check::is_true(b->saturation.size() >= 4, "several live tiers");
        for (std::size_t i = 1; i < b->saturation.size(); ++i) {
            check::is_true(b->saturation[i - 1].tier < b->saturation[i].tier,
                           "saturation rows to be ascending by tier");
        }
        const NestSaturation& lowest = b->saturation.front();
        const NestSaturation& highest = b->saturation.back();
        check::is_true(lowest.drawn > 0 && highest.drawn > 0, "both ends of the pyramid to be drawn");
        const double drawn_span = static_cast<double>(lowest.drawn) / static_cast<double>(highest.drawn);
        const double placed_span = static_cast<double>(lowest.placed) / static_cast<double>(std::max(1LL, highest.placed));
        check::is_true(drawn_span > 4.0, "the rate to be a steep pyramid, got " + std::to_string(drawn_span));
        check::is_true(placed_span < drawn_span / 2.0,
                       "the placed set to be far flatter than the rate: rate " +
                           std::to_string(drawn_span) + ", placed " + std::to_string(placed_span));

        long long placed_total = 0;
        for (const NestSaturation& s : b->saturation) {
            check::equal(s.refused, s.drawn - s.placed, "refused to be what the rate asked for and lost");
            // The raster ceiling. A bucket holds at most one site per cell, because every
            // site in a cell carries the cell's own centre and the narrowest territory the
            // options admit is still positive. So a monster tier is pinned at its cell
            // count outright, and an animal tier at its cell count times its membership.
            const long long ceiling =
                monsters ? s.cells : s.cells * animal_species_per_tier[s.tier];
            check::is_true(s.placed <= ceiling,
                           "tier " + std::to_string(s.tier) + " to be pinned by the cells it reaches: " +
                               std::to_string(s.placed) + " placed against a ceiling of " +
                               std::to_string(ceiling));
            placed_total += s.placed;
            check::is_true(s.rate_distance_m.has_value(), "a settled world to report a rate distance");
            check::is_true(s.placed_distance_m.has_value() || s.placed == 0,
                           "a placed tier to report where it ended up");
        }
        check::equal(placed_total, static_cast<long long>(b->sites.size()),
                     "saturation to account for every published site");
    }

    // The gradient is exact in the rate and swallowed by the raster in what is placed.
    const auto& sat = p.passes.beast_nests.saturation;
    check::is_true(*sat.back().rate_distance_m > *sat.front().rate_distance_m,
                   "the rate to put the greater monsters further from people");
}

TEST(an_unsettled_world_places_creatures_and_publishes_no_distances) {
    // inf * 0.0 is a NaN, and a NaN in a published block is a broken document, so the
    // distance columns are guarded rather than computed and clamped.
    const Populated& p = reference();
    const auto empty = add_nests(p.world, *p.grid, {});
    check::is_true(empty.has_value(), "a world with nobody in it still to hold creatures");
    for (const NestBlock* b : {&empty->wildlife, &empty->beast_nests}) {
        check::is_true(!b->sites.empty(), "creatures to be placed with nothing settled");
        for (const NestSaturation& s : b->saturation) {
            check::is_true(!s.rate_distance_m.has_value(), "no rate distance without people");
            check::is_true(!s.placed_distance_m.has_value(), "no placed distance without people");
        }
        for (const NestSite& s : b->sites) {
            check::is_true(std::isfinite(s.suitability) && std::isfinite(s.range_m),
                           s.id + " to carry finite numbers");
        }
    }
}

// --- diagnostics -----------------------------------------------------------

TEST(diagnostics_are_one_row_per_profile_in_catalogue_order) {
    const Populated& p = reference();
    const struct {
        const NestBlock* block;
        CreatureClass which;
        bool roles;
        int version;
    } blocks[] = {{&p.passes.wildlife, CreatureClass::Animal, true, kWildlifeVersion},
                  {&p.passes.beast_nests, CreatureClass::Monster, false, kBeastNestsVersion}};

    for (const auto& b : blocks) {
        const auto catalogue = creature_catalogue(b.which);
        check::equal(b.block->diagnostics.size(), catalogue.size(), "one diagnostic per profile");
        check::equal(b.block->catalogue_count, static_cast<long long>(catalogue.size()), "catalogue_count");
        check::equal(b.block->version, b.version, "the block version");
        check::is_true(b.block->profiles.data() == catalogue.data(), "the block to publish its own half");
        check::equal(b.block->roles.empty(), !b.roles, "only wildlife to publish the feeding roles");
        check::is_true(!b.block->method.empty() && !b.block->limits.empty(), "the prose to travel with it");
        for (std::size_t i = 0; i < catalogue.size(); ++i) {
            check::equal(std::string(b.block->diagnostics[i].species_id), std::string(catalogue[i].id),
                         "diagnostics to follow catalogue order");
            check::equal(std::string(b.block->diagnostics[i].name), std::string(catalogue[i].name), "the name");
            check::equal(b.block->diagnostics[i].tier, catalogue[i].tier, "the tier");
            check::equal(b.block->diagnostics[i].role.empty(), !b.roles, "the role column");
        }
    }
}

TEST(every_reason_a_species_is_missing_is_earned_and_distinguished) {
    // Five reasons, not four, because "absent" would otherwise bundle one catalogue defect
    // and one placement rule with two ordinary facts about a particular world.
    const Populated& p = reference();
    long long seen[5] = {};
    for (const NestBlock* b : {&p.passes.wildlife, &p.passes.beast_nests}) {
        std::map<std::string, long long> placed_by_species;
        for (const NestSite& s : b->sites) ++placed_by_species[std::string(s.species_id)];

        long long eligible = 0, placed_rows = 0, never = 0;
        for (const NestDiagnostic& d : b->diagnostics) {
            ++seen[static_cast<int>(d.reason)];
            const CreatureProfile* q = by_id(d.species_id);
            const long long actually = placed_by_species[std::string(d.species_id)];
            check::equal(d.placed, actually, std::string(d.species_id) + " placed count");
            if (d.cells != 0) ++eligible;
            if (d.placed != 0) ++placed_rows;
            if (q->never_satisfiable) ++never;

            switch (d.reason) {
                case NestReason::Placed:
                    check::is_true(d.placed > 0, "placed to mean placed");
                    break;
                case NestReason::NeverSatisfiable:
                    check::is_true(q->never_satisfiable && d.placed == 0 && d.cells == 0,
                                   std::string(d.species_id) + " to be provably unreachable");
                    break;
                case NestReason::LostTheDraw:
                    check::is_true(d.cells > 0 && d.placed == 0,
                                   std::string(d.species_id) + " to have had ground and no luck");
                    break;
                case NestReason::ClearedOut:
                case NestReason::AbsentFromThisWorld:
                    check::is_true(d.cells == 0 && d.placed == 0,
                                   std::string(d.species_id) + " to have had no ground at all");
                    break;
            }
        }
        check::equal(b->reachability.authored, static_cast<long long>(b->diagnostics.size()), "authored");
        check::equal(b->reachability.eligible, eligible, "eligible to count this world's ground");
        check::equal(b->reachability.placed, placed_rows, "placed to count this world's sites");
        check::equal(b->reachability.never_satisfiable, never, "the provable count");
        check::is_true(b->reachability.placed <= b->reachability.eligible,
                       "nothing to be placed where it cannot live");
    }
    // Floors: the reasons that should be exercised by this world are.
    check::is_true(seen[static_cast<int>(NestReason::Placed)] > 0, "some species to be placed");
    check::is_true(seen[static_cast<int>(NestReason::LostTheDraw)] > 0, "some species to lose the draw");
    check::is_true(seen[static_cast<int>(NestReason::AbsentFromThisWorld)] > 0,
                   "some species to want ground this world did not roll");
    check::is_true(seen[static_cast<int>(NestReason::NeverSatisfiable)] > 0,
                   "the provable verdict to be reported");

    check::equal(std::string(to_string(NestReason::Placed)), std::string("placed"), "the published wording");
    check::equal(std::string(to_string(NestReason::LostTheDraw)),
                 std::string("lost the draw to incumbents"), "the published wording");
    check::equal(std::string(to_string(NestReason::AbsentFromThisWorld)),
                 std::string("conditions absent from this world"), "the published wording");
    check::equal(std::string(to_string(NestReason::NeverSatisfiable)), std::string("never satisfiable"),
                 "the published wording");
    check::equal(std::string(to_string(NestReason::ClearedOut)),
                 std::string("its only ground is inside a settlement clearance"), "the published wording");
}

TEST(a_requirement_on_a_dormant_school_is_the_one_world_independent_verdict) {
    // Four ley schools exist in the taxonomy and generation can never raise one, so a
    // positive requirement on one is a hard gate against a layer that is identically zero
    // in every world the generator can build. Everything else about eligibility is a
    // statement about one world.
    const Populated& p = reference();
    for (const char* school : {"ley_blood", "ley_void", "ley_rot", "ley_eldritch"}) {
        const auto layer = p.world.layers.find(school);
        check::is_true(layer != p.world.layers.end(),
                       std::string(school) + " to exist as a layer, or the proof is about nothing");
        double highest = 0.0;
        for (const double v : layer->second.cells()) highest = std::max(highest, v);
        check::equal(highest, 0.0, std::string(school) + " to be identically zero in a generated world");
    }
    // ...and a live school is not, or the check above would hold for every layer.
    double live = 0.0;
    for (const double v : p.world.layers.at("ley_umbral").cells()) live = std::max(live, v);
    check::is_true(live > 0.0, "a live school to actually manifest");

    long long proved = 0;
    for (const CreatureProfile& q : creature_profiles()) {
        bool wants_dormant = false;
        for (const HabitatTerm& t : q.requirements) {
            const std::string_view name = nest_field_names()[static_cast<std::size_t>(t.field)];
            if (t.value > 0.0 && (name == "ley_blood" || name == "ley_void" || name == "ley_rot" ||
                                  name == "ley_eldritch")) {
                wants_dormant = true;
            }
        }
        check::equal(q.never_satisfiable, wants_dormant,
                     std::string(q.id) + " to be provably unreachable exactly when it needs a dormant school");
        if (wants_dormant) ++proved;
    }
    check::equal(proved, 25LL, "the provably unreachable count");
    check::equal(p.passes.wildlife.reachability.never_satisfiable, 0LL, "no animal to be one of them");
    check::equal(p.passes.beast_nests.reachability.never_satisfiable, 25LL, "every one to be a monster");

    // A profile that merely SCORES on a dormant school is handicapped, not proved
    // unreachable -- the distinction the fourth reason exists to keep.
    long long scorers = 0;
    for (const CreatureProfile& q : creature_profiles()) {
        for (const HabitatTerm& t : q.weights) {
            const std::string_view name = nest_field_names()[static_cast<std::size_t>(t.field)];
            if (name == "ley_blood" || name == "ley_void" || name == "ley_rot" || name == "ley_eldritch") {
                ++scorers;
                break;
            }
        }
    }
    check::is_true(scorers > 0, "some profile to score on a dormant school");
}

TEST(the_clearance_alone_can_be_why_a_species_is_missing) {
    // A species whose habitat is the ground people stand on can have all its eligible
    // cells inside a settlement's clearance. Calling that "conditions absent from this
    // world" sends a reader to look at the terrain, where there is nothing to find.
    //
    // The default clearance is well under the raster pitch at this width, so it only ever
    // refuses a settlement's own cell; a wide clearance and a densely settled world are
    // what make the reason reachable at all.
    const Populated crowded = populate(42, 65, 10, {{"nest_settlement_clearance", 3000.0}}, 3);
    check::is_true(crowded.settled.size() > 100, "a densely settled world");

    long long cleared = 0;
    for (const NestBlock* b : {&crowded.passes.wildlife, &crowded.passes.beast_nests}) {
        for (const NestDiagnostic& d : b->diagnostics) {
            if (d.reason != NestReason::ClearedOut) continue;
            ++cleared;
            check::equal(d.cells, 0LL, std::string(d.species_id) + " to have found no ground");
            check::equal(d.placed, 0LL, std::string(d.species_id) + " to have placed nothing");
            check::is_true(!by_id(d.species_id)->never_satisfiable,
                           std::string(d.species_id) + " to be refused by the clearance, not by the catalogue");
        }
    }
    check::is_true(cleared > 0, "the clearance alone to have emptied some species, got " +
                                    std::to_string(cleared));

    // And it is a reason this world invents, not one the profile carries: the same species
    // are not cleared out on the sparsely settled reference world.
    long long sparse = 0;
    for (const NestBlock* b : {&reference().passes.wildlife, &reference().passes.beast_nests}) {
        for (const NestDiagnostic& d : b->diagnostics) {
            if (d.reason == NestReason::ClearedOut) ++sparse;
        }
    }
    check::is_true(sparse < cleared, "the count to move with how much of the world is settled");
}

TEST(eligibility_is_a_sample_of_one_world_and_the_provable_count_is_not) {
    const Populated other = populate(7, 65, 10);
    const Populated& here = reference();
    bool moved = false;
    for (int i = 0; i < 2; ++i) {
        const NestBlock& a = i == 0 ? here.passes.wildlife : here.passes.beast_nests;
        const NestBlock& b = i == 0 ? other.passes.wildlife : other.passes.beast_nests;
        check::equal(a.reachability.authored, b.reachability.authored, "the authored count to be fixed");
        check::equal(a.reachability.never_satisfiable, b.reachability.never_satisfiable,
                     "the provable count to hold for every world");
        if (a.reachability.eligible != b.reachability.eligible) moved = true;
        if (a.reachability.placed != b.reachability.placed) moved = true;
    }
    check::is_true(moved, "eligibility to move with the seed, or it is not a sample");
    check::is_true(!nest_reachability_scope().empty(), "the scope line to travel with the four integers");
}

// --- the controls ----------------------------------------------------------

TEST(the_phase_gate_writes_no_block_and_a_design_space_grid_is_refused) {
    Request r;
    r.seed = 42;
    r.size = 33;
    r.phase = 6;
    const World early = generate(resolve(r));
    const auto grid = sphere_grid(early.config.size, early.effective.globe_radius);
    check::is_true(!add_nests(early, *grid, {}).has_value(), "no block below phase 7");
    check::is_true(!add_animals(early, *grid, {}).has_value(), "no wildlife below phase 7");
    check::is_true(!add_monsters(early, *grid, {}).has_value(), "no beast nests below phase 7");

    r.phase = 7;
    const World late = generate(resolve(r));
    const auto ok = sphere_grid(late.config.size, late.effective.globe_radius);
    check::is_true(add_nests(late, *ok, {}).has_value(), "the passes to run from phase 7");

    // A design-space grid would compare metre reaches against design units, silently.
    check::is_true(std::abs(late.config.globe_radius - late.effective.globe_radius) > 1.0,
                   "the two radii to actually differ, or the guard is untestable");
    const auto wrong = sphere_grid(late.config.size, late.config.globe_radius);
    check::throws([&] { add_nests(late, *wrong, {}); }, "a design-space grid");
    check::throws([&] { add_nests(late, *ok, {static_cast<int>(ok->node_count())}); },
                  "a settled thing off the end of the grid");
}

TEST(emptying_the_density_empties_the_world_without_emptying_the_diagnostics) {
    // Zero density is a legal world with nothing in it, and a species then reports that it
    // lost the draw rather than that its ground is missing -- the rate is what went to
    // zero, not the habitat.
    const Populated quiet = populate(42, 65, 10, {{"nest_density", 0.0}});
    check::is_true(quiet.passes.wildlife.sites.empty(), "no animals at zero density");
    check::is_true(quiet.passes.beast_nests.sites.empty(), "no monsters at zero density");
    check::is_true(quiet.passes.wildlife.reachability.eligible > 0,
                   "the ground to still be there");
    long long lost = 0;
    for (const NestDiagnostic& d : quiet.passes.wildlife.diagnostics) {
        if (d.reason == NestReason::LostTheDraw) ++lost;
    }
    check::is_true(lost > 0, "an eligible species to report that it lost the draw");

    // nest_fantasy is the monster-only knob: zero leaves the animals alone.
    const Populated mundane = populate(42, 65, 10, {{"nest_fantasy", 0.0}});
    check::is_true(mundane.passes.beast_nests.sites.empty(), "no monsters with fantasy off");
    check::is_true(!mundane.passes.wildlife.sites.empty(), "the animals to be untouched");
    check::equal(mundane.passes.wildlife.sites.size(), reference().passes.wildlife.sites.size(),
                 "the animal pass not to notice the monster knob at all");
}

TEST(the_caps_bind_and_the_post_hoc_limit_keeps_the_notable_creatures) {
    const Populated& full = reference();
    std::map<std::string, long long> worst;
    long long busiest = 0;
    for (const NestSite& s : full.passes.wildlife.sites) {
        busiest = std::max(busiest, ++worst[std::string(s.species_id)]);
    }
    check::is_true(busiest > 2, "some species to exceed the cap before it is applied");

    const Populated capped = populate(42, 65, 10, {{"nest_per_species", 2.0}});
    std::map<std::string, long long> per;
    for (const NestSite& s : capped.passes.wildlife.sites) {
        check::is_true(++per[std::string(s.species_id)] <= 2, "the per-species cap to bind");
    }
    check::is_true(!capped.passes.wildlife.sites.empty(), "the cap to leave a world behind");
    check::is_true(capped.passes.wildlife.sites.size() < full.passes.wildlife.sites.size(),
                   "the cap to have removed something");

    // A safety valve, not the thing that decides how full a world is. When it bites it
    // keeps the notable creatures: highest tier first, then by id.
    const Populated limited = populate(42, 65, 10, {{"nest_limit", 100.0}});
    for (const NestBlock* b : {&limited.passes.wildlife, &limited.passes.beast_nests}) {
        check::equal(b->sites.size(), std::size_t{100}, "the limit to bind exactly");
        int top = 0;
        for (const NestSite& s : b->sites) top = std::max(top, s.tier);
        for (const NestSite& s : b->sites) {
            check::equal(s.tier, top, "the cap to keep only the most dangerous tier it can fill");
        }
        long long counted = 0;
        for (const NestDiagnostic& d : b->diagnostics) counted += d.placed;
        check::equal(counted, 100LL, "the diagnostics to be rewritten by the cap");
    }
}

TEST(a_pass_is_repeatable_and_the_two_passes_never_read_each_other) {
    const Populated& p = reference();
    const auto again = add_nests(p.world, *p.grid, p.settled);
    check::equal(again->wildlife.sites.size(), p.passes.wildlife.sites.size(), "the same animals");
    check::equal(again->beast_nests.sites.size(), p.passes.beast_nests.sites.size(), "the same monsters");
    for (std::size_t i = 0; i < again->wildlife.sites.size(); ++i) {
        check::equal(again->wildlife.sites[i].id, p.passes.wildlife.sites[i].id, "site by site");
    }

    // Each pass on its own is the same pass: the animal half does not read a monster and
    // vice versa, so running one alone cannot move it.
    const auto alone = add_animals(p.world, *p.grid, p.settled);
    const auto lairs = add_monsters(p.world, *p.grid, p.settled);
    check::equal(alone->sites.size(), p.passes.wildlife.sites.size(), "the animal pass run alone");
    check::equal(lairs->sites.size(), p.passes.beast_nests.sites.size(), "the monster pass run alone");
    for (std::size_t i = 0; i < alone->sites.size(); ++i) {
        check::equal(alone->sites[i].id, p.passes.wildlife.sites[i].id, "animal site by site");
    }
    for (std::size_t i = 0; i < lairs->sites.size(); ++i) {
        check::equal(lairs->sites[i].id, p.passes.beast_nests.sites[i].id, "monster site by site");
    }

    // The two streams are named apart, so a variation moves both and neither moves the
    // other's ground.
    const Populated varied = populate(42, 65, 10, {{"nest_variation", 17.0}});
    check::is_true(varied.passes.wildlife.sites.size() != p.passes.wildlife.sites.size() ||
                       varied.passes.wildlife.sites.front().id != p.passes.wildlife.sites.front().id,
                   "a variation to move the animals");
    check::is_true(varied.passes.beast_nests.sites.size() != p.passes.beast_nests.sites.size() ||
                       varied.passes.beast_nests.sites.front().id != p.passes.beast_nests.sites.front().id,
                   "a variation to move the monsters");
    check::equal(varied.passes.wildlife.reachability.eligible, p.passes.wildlife.reachability.eligible,
                 "a variation to move the draw and not the ground");
}

// --- an inherited defect, pinned so a fix cannot land silently --------------

TEST(a_shore_species_can_reach_almost_no_ground_at_this_world_width) {
    // `coast` is the maritime layer, which is exp(-distance_to_ocean / 300 m) -- authored
    // for the 11.15 km reference world, whose grid step is about 87 m. On the 200 km
    // default world the finest temperate step is over a kilometre, so no temperate land
    // cell can read the 0.1 a shore species needs: the threshold asks for a resolution the
    // raster does not have, and the handful of cells that do qualify are polar, where
    // longitude convergence packs the rings arbitrarily tight.
    //
    // The consequence is that twenty-one authored shore species -- puffins, penguins,
    // oystercatchers, sea kraits -- are eligible on a couple of dozen cells between them
    // and are placed essentially nowhere.
    //
    // This is the same disease test_habitation pins on human_maritime's 0.3 threshold, and
    // it is inherited from the source rather than introduced by the port. PINNED rather
    // than fixed because rescaling the decay changes every world. When that decision is
    // taken, this test is one of the two to update.
    const Populated& p = reference();
    long long coastal = 0;
    long long polar = 0;
    const int n = p.world.config.size;
    for (const HabitatCell& c : p.field.cells) {
        if (c.medium != CellMedium::Land || c.coast < 0.1) continue;
        ++coastal;
        if (std::abs(90.0 - 180.0 * c.z / (n - 1)) > 60.0) ++polar;
    }
    check::is_true(coastal > 0, "some cell to reach the coastal threshold, or nothing is being measured");
    check::equal(polar, coastal, "every qualifying cell to be polar, which is a raster artifact");

    long long shore_profiles = 0, shore_cells = 0, shore_sites = 0;
    for (const CreatureProfile& q : creature_profiles()) {
        if (q.medium == CreatureMedium::Shore) ++shore_profiles;
    }
    for (const NestDiagnostic& d : p.passes.wildlife.diagnostics) {
        if (by_id(d.species_id)->medium == CreatureMedium::Shore) shore_cells += d.cells;
    }
    for (const NestBlock* b : {&p.passes.wildlife, &p.passes.beast_nests}) {
        for (const NestSite& s : b->sites) {
            if (by_id(s.species_id)->medium == CreatureMedium::Shore) ++shore_sites;
        }
    }
    check::is_true(shore_profiles >= 20, "the catalogue to author a shore half worth placing");
    check::is_true(shore_cells < 100,
                   "shore species to reach almost no ground, got " + std::to_string(shore_cells) + " cells");
    check::is_true(shore_sites * 1000 < static_cast<long long>(p.passes.wildlife.sites.size()),
                   "and to be placed essentially nowhere, got " + std::to_string(shore_sites));
}
