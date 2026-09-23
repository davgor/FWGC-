// Pantheon invariants.
//
// These run against a hand-built world rather than a generated one, for the same reason
// the reference's own tests do: the properties under test are about existence rules,
// draw order and carry-forward, and a generated world would make every one of them depend
// on eleven stages upstream.
//
// The world is deliberately small and explicit -- two peoples, four nests, three ruins,
// an eight-cell ley grid -- so that when a test fails the thing that moved is in the test.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fwg/civilizations.hpp"
#include "fwg/religion.hpp"

using namespace fwg;

namespace {

constexpr int kGrid = 8;
constexpr double kRadius = 1000.0;

// A world religion can read, owned by the test so that the request's pointers stay valid.
struct Bench {
    MagicReport magic;
    std::map<std::string, Grid> layers;
    Almanac almanac;
    std::vector<ReligionCity> cities;
    std::vector<ReligionNest> nests;
    std::vector<ReligionRuin> ruins;
    std::vector<ReligionAge> ages;
    std::vector<ReligionFoundingEvent> events;
    std::vector<std::string> used;
    double ocean_km2 = 300.0;
    double lake_km2 = 100.0;
    double dry_km2 = 600.0;
    int sky = 0;
    double lunar = 0.0;
    double radius = kRadius;
    std::uint32_t seed = 42;

    ReligionRequest request(const Religion* previous = nullptr) const {
        ReligionRequest r;
        r.seed = seed;
        r.magic = &magic;
        r.layers = &layers;
        r.almanac = &almanac;
        r.cities = cities;
        r.nests = nests;
        r.ruins = ruins;
        r.ages = ages;
        r.founding_events = events;
        r.used_civilizations = used;
        r.sky_settlements = sky;
        r.ocean_km2 = ocean_km2;
        r.lake_km2 = lake_km2;
        r.dry_km2 = dry_km2;
        r.lunar_influence = lunar;
        r.globe_radius_m = radius;
        r.previous = previous;
        return r;
    }

    void network(const std::string& school, const std::vector<double>& intensities,
                 double instability = 0.0) {
        LeyNetwork net;
        net.name = school;
        net.instability = instability;
        int n = 0;
        for (const double intensity : intensities) {
            net.nodes.push_back({school + "-" + std::to_string(n++), Vec3{0.0, 1.0, 0.0}, intensity});
        }
        magic.networks[school] = net;
        magic.enabled = true;
    }

    void potency(const std::string& school, double value) {
        layers["ley_" + school] = Grid(kGrid, value);
    }

    void city(const std::string& uid, const std::string& profile, const Vec3& where) {
        ReligionCity c;
        c.uid = uid;
        c.population_profile = profile;
        c.x = 3;
        c.z = 4;
        c.node = static_cast<int>(cities.size()) + 1;
        c.direction = where;
        cities.push_back(c);
    }

    void nest(const std::string& id, const std::string& name, const std::string& family,
              const Vec3& where, double range_m = 5000.0) {
        ReligionNest n;
        n.id = id;
        n.name = name;
        n.family = family;
        n.real = false;
        n.tier = 4;
        n.range_m = range_m;
        n.direction = where;
        nests.push_back(n);
    }
};

// A unit direction `degrees` away from the pole, in the x-y plane.
Vec3 lean(double degrees) {
    const double a = degrees * 3.14159265358979323846 / 180.0;
    return {std::sin(a), std::cos(a), 0.0};
}

// Two peoples with cities, magic in four schools, and ground that has been fought over.
Bench populated_world() {
    Bench b;
    b.network("weave", {1.4, 1.5}, 0.2);
    b.network("water", {0.5, 0.6}, 0.2);
    b.network("earth", {0.9, 1.0}, 0.1);
    b.network("radiant", {0.8}, 0.0);
    b.potency("weave", 1.2);
    b.potency("water", 0.4);
    b.potency("earth", 0.9);
    b.potency("radiant", 0.6);
    b.city("city-a", "dwarf", lean(0.0));
    b.city("city-b", "human_heartland", lean(30.0));
    b.used = {"dwarf", "human_heartland"};
    b.ruins.push_back({"ruin-1", 17, "self_magic", 1, "dwarf"});
    b.ruins.push_back({"ruin-2", 23, "war_border", 1, "human_heartland"});
    b.ages.push_back({1, 2, 1.5});
    return b;
}

const GodRecord* record_of(const Religion& religion, std::string_view id) {
    for (const GodRecord& god : religion.gods) {
        if (god.id == id) return &god;
    }
    return nullptr;
}

std::string status_of(const Religion& religion, std::string_view id) {
    const GodRecord* god = record_of(religion, id);
    return god != nullptr ? god->status : std::string("<missing>");
}

const Faith* faith_of(const Religion& religion, std::string_view civilization) {
    for (const Faith& faith : religion.faiths) {
        if (faith.civilization == civilization) return &faith;
    }
    return nullptr;
}

double weight_of(const Faith& faith, std::string_view god_id) {
    for (const auto& entry : faith.weights) {
        if (entry.first == god_id) return entry.second;
    }
    return 0.0;
}

bool has_weight(const Faith& faith, std::string_view god_id) {
    for (const auto& entry : faith.weights) {
        if (entry.first == god_id) return true;
    }
    return false;
}

long long manifest_count(const Religion& religion) {
    long long n = 0;
    for (const GodRecord& god : religion.gods) {
        if (god.status == "manifest" || god.status == "walking") ++n;
    }
    return n;
}

std::size_t catalogue_index(std::string_view id) {
    const auto gods = pantheon_gods();
    for (std::size_t i = 0; i < gods.size(); ++i) {
        if (gods[i].id == id) return i;
    }
    return gods.size();
}

Almanac almanac_with(const std::vector<double>& nods, const std::vector<AlmanacEvent>& events) {
    Almanac a;
    for (const double nod : nods) {
        AlmanacMonth month;
        month.state.nod_degrees = nod;
        a.months.push_back(month);
    }
    a.events = events;
    return a;
}

}  // namespace

// --- the catalogue --------------------------------------------------------

TEST(the_catalogue_lints_and_is_the_size_it_claims) {
    lint_pantheon();
    check::equal(pantheon_gods().size(), std::size_t{25}, "the known pantheon");
    check::equal(hidden_pantheon_gods().size(), std::size_t{4}, "the gods nobody speaks of");
    check::equal(pantheon_cosmologies().size(), std::size_t{11}, "cosmology templates");
    check::equal(pantheon_affinities().size(), civilization_ids().size(),
                 "one affinity table per civilization");

    // Every school god, exactly once, or a whole network raises nobody.
    std::set<std::string> schools;
    for (const PantheonGod& god : pantheon_gods()) {
        if (god.family == "school") schools.insert(std::string(god.exists[0].school));
    }
    check::equal(schools.size(), std::size_t{kKnownSchools}, "school gods covering every school");
}

TEST(the_moon_is_an_affinity_and_not_a_school) {
    // `moon` is on god_turning_moon's affinity list on purpose. It names no potency grid,
    // so it scores exactly zero -- see the faith test below, which proves the zero rather
    // than merely asserting the string.
    const PantheonGod* moon = pantheon_god(kMoonGodId);
    check::is_true(moon != nullptr, "the moon god to be in the catalogue");
    check::equal(moon->school_affinity.size(), std::size_t{1}, "one affinity");
    check::equal(std::string(moon->school_affinity[0]), std::string(kMoonPseudoSchool),
                 "the moon's affinity");
    check::is_true(magic_school_index(kMoonPseudoSchool) < 0,
                   "the moon to be no school at all in the school table");
}

TEST(the_catalogue_digest_covers_the_known_half_alone) {
    const PantheonIdentity known = pantheon_identity();
    const PantheonIdentity hidden = hidden_pantheon_identity();

    check::equal(known.sha256.size(), std::size_t{64}, "a sha256 over the known catalogue");
    check::equal(hidden.sha256.size(), std::size_t{64}, "a sha256 over the hidden catalogue");
    check::equal(known.source_sha256.size(), std::size_t{64}, "the reference catalogue's digest");
    check::is_true(known.sha256 != known.source_sha256,
                   "this data to hash differently from the file it was extracted from");
    // TWO DIGESTS, not one. The four Outside gods hash separately so that touching one
    // does not move the identity every saved world was stamped with.
    check::is_true(known.sha256 != hidden.sha256, "the two halves to have separate identities");
    check::is_true(known.source_sha256 != hidden.source_sha256, "separate provenance too");

    // The structural fact the split rests on: a hidden god is not in the known table, so
    // the known digest cannot see it.
    for (const HiddenGod& god : hidden_pantheon_gods()) {
        check::is_true(pantheon_god(god.id) == nullptr,
                       std::string(god.id) + " to be absent from the known catalogue");
    }
}

// --- existence ------------------------------------------------------------

TEST(every_catalogue_god_is_resolved_once_in_catalogue_order) {
    const Bench bench = populated_world();
    const Religion religion = resolve_religion(bench.request());

    check::equal(religion.gods.size(), std::size_t{29}, "25 known gods then 4 hidden ones");
    const auto known = pantheon_gods();
    for (std::size_t i = 0; i < known.size(); ++i) {
        check::equal(religion.gods[i].id, std::string(known[i].id), "god at position " + std::to_string(i));
    }
    const auto hidden = hidden_pantheon_gods();
    for (std::size_t i = 0; i < hidden.size(); ++i) {
        check::equal(religion.gods[known.size() + i].id, std::string(hidden[i].id),
                     "hidden god at position " + std::to_string(i));
    }
    check::is_true(manifest_count(religion) >= 10,
                   "a populated world to raise a pantheon rather than an empty list");
}

TEST(an_empty_world_raises_the_gods_that_need_nothing_and_no_others) {
    // The degenerate case: no magic, no cities, no ruins, no nests. The seven civic gods
    // and the moon exist on `always` and nothing else can.
    const ReligionRequest request;
    const Religion religion = resolve_religion(request);

    check::equal(manifest_count(religion), 8LL, "the eight gods whose rule is `always`");
    check::equal(status_of(religion, kMoonGodId), std::string("manifest"), "the moon");
    for (const PantheonGod& god : pantheon_gods()) {
        if (god.family != "school") continue;
        check::equal(status_of(religion, god.id), std::string("absent"),
                     std::string(god.id) + " with no network");
    }
    check::equal(religion.faiths.size(), std::size_t{0}, "no people, no faith");
    check::equal(religion.sites.size(), std::size_t{0}, "no ruins, no sites");
    check::is_true(!religion.cosmology.id.empty(), "a cosmology even in an empty world");

    // `magic_disabled` is reachable from exactly one place in the whole catalogue: the One
    // Sun's second disjunct, which is how a world with no magic at all still gets an
    // over-god. Nothing else in the suite would notice if the kind stopped working.
    check::is_true(!religion.facts.magic_enabled, "a world with no networks to have no magic");
    bool one_sun = false;
    for (const auto& entry : religion.cosmology.eligible) {
        if (entry.first == "cos_one_sun") one_sun = true;
    }
    check::is_true(one_sun, "the One Sun to fit a world with no magic and no Radiant");
}

TEST(a_god_that_stops_qualifying_sleeps_with_its_last_aspect) {
    Bench bench = populated_world();
    const Religion before = resolve_religion(bench.request());
    check::equal(status_of(before, "god_weave"), std::string("manifest"), "the Weave with a network");
    const std::string worn = record_of(before, "god_weave")->aspect;
    check::is_true(!worn.empty(), "a manifest god to wear an aspect");
    check::is_true(record_of(before, "god_weave")->evidence.mean_intensity > 0.0,
                   "the aspect to have been measured from the network");

    bench.magic.networks.erase("weave");
    const Religion after = resolve_religion(bench.request(&before));

    check::equal(status_of(after, "god_weave"), std::string("sleeping"), "the Weave once its network is gone");
    check::equal(record_of(after, "god_weave")->aspect, worn, "the aspect it was last seen wearing");
    check::is_true(record_of(after, "god_weave")->evidence.last_seen,
                   "the evidence to say it is historic");
    check::equal(status_of(after, "god_earth"), std::string("manifest"),
                 "a god whose network survived to be untouched");
}

TEST(sleeping_repeatedly_does_not_deepen_the_evidence) {
    // The reference wraps the whole of the previous evidence in a fresh `last_seen` on
    // every resolution, so a god asleep through many age turns grows the export without
    // bound. This port keeps ONE level; the test is what stops that being re-introduced.
    Bench bench = populated_world();
    const Religion first = resolve_religion(bench.request());
    const double measured = record_of(first, "god_weave")->evidence.mean_intensity;
    check::is_true(measured > 0.0, "the Weave to have been measured while it was awake");

    bench.magic.networks.erase("weave");
    Religion current = resolve_religion(bench.request(&first));
    for (int i = 0; i < 5; ++i) {
        const Religion next = resolve_religion(bench.request(&current));
        current = next;
    }
    const GodRecord* weave = record_of(current, "god_weave");
    check::equal(weave->status, std::string("sleeping"), "still asleep after six resolutions");
    check::is_true(weave->evidence.last_seen, "still flagged historic");
    check::close(weave->evidence.mean_intensity, measured, 1e-12,
                 "the same measurement, not a nest of copies of it");
}

TEST(a_walking_god_stays_walking_and_keeps_its_avatar) {
    Bench bench = populated_world();
    Religion previous = resolve_religion(bench.request());
    // What a visitation would have written. Nothing in this stage can end a walk.
    for (GodRecord& god : previous.gods) {
        if (god.id == "god_air") {
            god.status = "walking";
            god.avatar = OpaqueRecord("{\"form\":\"a wind with a crown\"}");
        }
    }
    check::equal(status_of(previous, "god_air"), std::string("walking"), "the fixture to be set up");

    const Religion after = resolve_religion(bench.request(&previous));
    check::equal(status_of(after, "god_air"), std::string("walking"),
                 "the Air to still walk although it has no network");
    const GodRecord* air = record_of(after, "god_air");
    check::is_true(air->avatar.has_value(), "the avatar to be carried forward");
    check::equal(*air->avatar, OpaqueRecord("{\"form\":\"a wind with a crown\"}"), "the same avatar");
    check::is_true(!air->aspect.empty(), "a walking god to be measured like a manifest one");
}

TEST(removing_the_dragon_removes_the_wyrm_and_nothing_else) {
    Bench with = populated_world();
    with.nest("nest-dragon", "Ash Dragon", "draconic", lean(1.0));
    Bench without = populated_world();

    const Religion a = resolve_religion(with.request());
    const Religion b = resolve_religion(without.request());

    check::equal(status_of(a, "god_great_wyrm"), std::string("manifest"), "the Wyrm with a dragon");
    check::equal(status_of(b, "god_great_wyrm"), std::string("absent"), "the Wyrm without one");

    long long differences = 0;
    long long agreed_live = 0;
    for (std::size_t i = 0; i < a.gods.size(); ++i) {
        check::equal(a.gods[i].id, b.gods[i].id, "the two lists to line up");
        if (a.gods[i].status != b.gods[i].status) ++differences;
        else if (a.gods[i].status == "manifest") ++agreed_live;
    }
    check::equal(differences, 1LL, "exactly one god to change");
    check::is_true(agreed_live >= 8, "the rest of the pantheon to be non-trivially the same");
}

TEST(a_gods_rule_can_never_hold_while_resolving_gods) {
    // `resolve_gods` passes no manifest set, which is what makes a `gods` rule
    // unsatisfiable there. Only a cosmology is evaluated with one.
    Bench bench = populated_world();
    bench.network("infernal", {1.0});
    const WorldFacts facts = world_facts(bench.request());

    const Cosmology* dual = pantheon_cosmology("cos_dual_dawn");
    check::is_true(dual != nullptr, "the two-god cosmology to exist");
    check::is_true(!rule_holds(dual->requirement, facts),
                   "a gods rule to fail with no manifest set, however the world looks");

    GodIdSet manifest;
    manifest.insert("god_radiant");
    manifest.insert("god_infernal");
    check::is_true(rule_holds(dual->requirement, facts, manifest),
                   "the same rule to hold once the gods are named -- or the test proves nothing");

    manifest.erase("god_infernal");
    check::is_true(!rule_holds(dual->requirement, facts, manifest),
                   "an `all` rule to need every god, not one");
}

// --- the cosmology --------------------------------------------------------

TEST(the_eligible_list_is_in_catalogue_order_and_not_in_weight_order) {
    // The order is the order the single uniform draw walks, so it decides which template
    // a seed picks even when two weigh the same.
    //
    // The ruins are here to make the weights RISE along the catalogue: the Silent Heavens
    // weigh half a ruin each and sit below the Ancestor Halls, so a list that had been
    // sorted by weight would be a different list. Without them every eligible weight
    // happens to descend already and the assertion below is satisfied by an accident.
    Bench bench = populated_world();
    for (int i = 0; i < 8; ++i) {
        bench.ruins.push_back({"ruin-x" + std::to_string(i), 60 + i, "dragon", 1, "dwarf"});
    }
    const Religion religion = resolve_religion(bench.request());

    check::is_true(religion.cosmology.eligible.size() >= 3,
                   "several templates to fit, or the ordering is untested");
    std::size_t previous = 0;
    bool seen = false;
    bool descends = true;
    double last_weight = 0.0;
    for (const auto& entry : religion.cosmology.eligible) {
        std::size_t index = pantheon_cosmologies().size();
        for (std::size_t i = 0; i < pantheon_cosmologies().size(); ++i) {
            if (pantheon_cosmologies()[i].id == entry.first) index = i;
        }
        check::is_true(index < pantheon_cosmologies().size(), entry.first + " to be in the catalogue");
        if (seen) check::is_true(index > previous, "eligible templates to ascend the catalogue");
        if (seen && entry.second > last_weight) descends = false;
        previous = index;
        last_weight = entry.second;
        seen = true;
        check::is_true(entry.second > 0.0, entry.first + " to carry a positive weight");
    }
    check::is_true(!descends,
                   "the fixture's weights to rise somewhere, or catalogue order and weight "
                   "order agree and the assertion above tests nothing");
}

TEST(the_cosmology_is_drawn_and_a_different_seed_can_draw_another) {
    Bench bench = populated_world();
    std::set<std::string> drawn;
    for (std::uint32_t seed = 0; seed < 40; ++seed) {
        bench.seed = seed;
        const Religion religion = resolve_religion(bench.request());
        drawn.insert(religion.cosmology.id);
        // Whatever is drawn must have been eligible.
        bool eligible = false;
        for (const auto& entry : religion.cosmology.eligible) {
            if (entry.first == religion.cosmology.id) eligible = true;
        }
        check::is_true(eligible, "the drawn cosmology to be one of the eligible ones");
    }
    check::is_true(drawn.size() >= 2,
                   "the seed to actually move the draw, not merely be accepted");
}

TEST(the_mosaic_is_always_eligible_so_the_fallback_is_dead_ground) {
    const ReligionRequest empty;
    const Religion religion = resolve_religion(empty);
    bool mosaic = false;
    for (const auto& entry : religion.cosmology.eligible) {
        if (entry.first == kFallbackCosmologyId) mosaic = true;
    }
    check::is_true(mosaic, "the Mosaic to fit even the emptiest world");
    check::is_true(!religion.cosmology.eligible.empty(), "something to draw from");
}

TEST(high_gods_are_filtered_to_the_ones_that_exist) {
    Bench bench = populated_world();
    for (std::uint32_t seed = 0; seed < 60; ++seed) {
        bench.seed = seed;
        const Religion religion = resolve_religion(bench.request());
        for (const std::string& high : religion.cosmology.high) {
            check::equal(status_of(religion, high) == "absent", false,
                         high + " to be a high god only while it exists");
        }
    }
    // Non-vacuity: at least one seed must produce a cosmology that names anybody.
    bool any_high = false;
    for (std::uint32_t seed = 0; seed < 60 && !any_high; ++seed) {
        bench.seed = seed;
        any_high = !resolve_religion(bench.request()).cosmology.high.empty();
    }
    check::is_true(any_high, "some cosmology in the sweep to name a high god");
}

// --- aspects --------------------------------------------------------------

TEST(a_school_god_turns_wild_above_the_midpoint) {
    Bench bench = populated_world();
    const Religion religion = resolve_religion(bench.request());

    // weave 1.45 mean x 1.2 instability = 1.74, past the 1.2 midpoint.
    const GodRecord* weave = record_of(religion, "god_weave");
    check::equal(weave->aspect, std::string("wild"), "the Weave above the midpoint");
    check::equal(weave->aspect_name, std::string("The Laughing Unmaker"), "its wild name");
    check::close(weave->evidence.mean_intensity, 1.45, 1e-12, "the measured mean");
    check::close(weave->evidence.midpoint, 1.2, 1e-12, "the midpoint it was judged against");

    // water 0.55 x 1.2 = 0.66, comfortably below.
    const GodRecord* water = record_of(religion, "god_water");
    check::equal(water->aspect, std::string("sovereign"), "the Water below it");
    check::equal(water->aspect_name, std::string("The Tide-Keeper"), "its sovereign name");
}

TEST(an_undead_nest_tips_a_network_that_was_sitting_just_under_the_midpoint) {
    // 1.1 is below 1.2 and 1.1 + 0.15 is above it: the push is sized to tip a network
    // already close, and not a quiet one.
    Bench quiet = populated_world();
    quiet.network("umbral", {1.1}, 0.0);
    const Religion without = resolve_religion(quiet.request());
    check::equal(record_of(without, "god_umbral")->aspect, std::string("sovereign"),
                 "the Umbral with no undead near it");
    check::close(record_of(without, "god_umbral")->evidence.nest_push, 0.0, 1e-12, "no push");

    Bench haunted = quiet;
    haunted.nest("nest-barrow", "Barrow Host", "undead", lean(2.0));
    const Religion with = resolve_religion(haunted.request());
    check::equal(record_of(with, "god_umbral")->aspect, std::string("wild"),
                 "the Umbral once the dead are walking");
    check::close(record_of(with, "god_umbral")->evidence.nest_push, 0.15, 1e-12, "the nest push");

    // The push is targeted: an undead nest says nothing about the Weave.
    check::equal(record_of(with, "god_weave")->aspect, record_of(without, "god_weave")->aspect,
                 "the Weave to be unmoved by undead");
}

TEST(the_moon_follows_its_lean) {
    Bench still = populated_world();
    still.almanac = almanac_with({1.0, 2.0, 3.0}, {});
    const Religion a = resolve_religion(still.request());
    check::equal(record_of(a, kMoonGodId)->aspect, std::string("sovereign"), "a still moon");
    check::equal(record_of(a, kMoonGodId)->evidence.leaning, std::string("still"), "and it says so");
    check::close(record_of(a, kMoonGodId)->evidence.mean_nod_degrees, 2.0, 1e-12, "the mean lean");

    Bench restless = populated_world();
    restless.almanac = almanac_with({-1.0, -2.0, -3.0}, {});
    const Religion b = resolve_religion(restless.request());
    check::equal(record_of(b, kMoonGodId)->aspect, std::string("wild"), "a restless moon");
    check::equal(record_of(b, kMoonGodId)->aspect_name, std::string("The Turning Face"), "its wild name");
}

TEST(a_god_with_one_face_is_named_for_itself) {
    const Bench bench = populated_world();
    const Religion religion = resolve_religion(bench.request());
    long long checked = 0;
    for (const PantheonGod& god : pantheon_gods()) {
        if (!god.aspects.wild.empty()) continue;
        const GodRecord* record = record_of(religion, god.id);
        if (record->status == "absent") continue;
        check::equal(record->aspect, std::string("sovereign"), std::string(god.id) + " never wavers");
        check::equal(record->aspect_name, std::string(god.name),
                     std::string(god.id) + "'s sovereign name is its own name");
        ++checked;
    }
    check::is_true(checked >= 8, "a real number of single-faced gods to have been checked");
}

// --- faiths ---------------------------------------------------------------

TEST(faiths_follow_the_sorted_civilization_order) {
    Bench bench = populated_world();
    bench.city("city-c", "elf", lean(60.0));
    bench.used.push_back("elf");
    const Religion religion = resolve_religion(bench.request());

    check::equal(religion.faiths.size(), std::size_t{3}, "one faith per people with a city");
    check::equal(religion.faiths[0].civilization, std::string("dwarf"), "first");
    check::equal(religion.faiths[1].civilization, std::string("elf"), "second");
    check::equal(religion.faiths[2].civilization, std::string("human_heartland"), "third");
    for (const Faith& faith : religion.faiths) {
        check::is_true(!faith.parent_race_id.empty(), faith.civilization + " to have a parent race");
    }
}

TEST(a_people_takes_its_own_ancestor_as_patron) {
    const Bench bench = populated_world();
    const Religion religion = resolve_religion(bench.request());

    const Faith* dwarves = faith_of(religion, "dwarf");
    check::is_true(dwarves != nullptr, "the dwarves to keep a faith");
    check::equal(dwarves->patron, std::string("god_deep_forge"), "the dwarven ancestor");
    check::equal(dwarves->gods.front(), dwarves->patron, "the patron to lead the list");
    check::equal(dwarves->parent_race_id, std::string("dwarf"), "its parent race");
    // The ancestor is taken, not drawn, so it never appears in the weighted pool.
    check::is_true(!has_weight(*dwarves, "god_deep_forge"),
                   "the ancestor to be out of the draw entirely");

    const Faith* humans = faith_of(religion, "human_heartland");
    check::equal(humans->patron, std::string("god_first_hearth"), "the human ancestor");
    check::equal(humans->names.front().second, std::string("The Old King"),
                 "the heartland's own epithet for it");
}

TEST(a_patron_is_never_drawn_because_an_ancestor_is_always_there) {
    // A people with a standing city puts its parent race into facts.races, which is
    // exactly what its ancestor god's existence rule tests -- so the ancestor is always
    // live and is always taken directly. The weighted draw for a patron is therefore
    // unreachable while every parent race has an ancestor god, and a patron is a property
    // of the world rather than of the seed.
    //
    // This test is here because the day that stops holding, the patron becomes random and
    // nothing else in the suite would say so.
    std::map<std::string, long long> ancestors;
    for (const std::string_view race : {"human", "elf", "dwarf"}) {
        ancestors[std::string(race)] = 0;
    }
    for (const PantheonGod& god : pantheon_gods()) {
        if (god.family == "ancestor") ++ancestors[std::string(god.race)];
    }
    for (const auto& entry : ancestors) {
        check::equal(entry.second, 1LL, "exactly one ancestor god for " + entry.first);
    }

    Bench bench = populated_world();
    bench.city("city-c", "elf", lean(70.0));
    for (std::uint32_t seed = 0; seed < 25; ++seed) {
        bench.seed = seed;
        const Religion religion = resolve_religion(bench.request());
        check::equal(religion.faiths.size(), std::size_t{3}, "three peoples");
        for (const Faith& faith : religion.faiths) {
            const PantheonGod* patron = pantheon_god(faith.patron);
            check::is_true(patron != nullptr, faith.civilization + " to have a patron at all");
            check::equal(std::string(patron->family), std::string("ancestor"),
                         faith.civilization + "'s patron to be an ancestor god");
            check::equal(std::string(patron->race), faith.parent_race_id, "and its own one");
        }
    }
}

TEST(a_faith_draws_at_most_three_gods_beside_its_patron) {
    const Bench bench = populated_world();
    const Religion religion = resolve_religion(bench.request());
    long long drew_companions = 0;
    for (const Faith& faith : religion.faiths) {
        check::is_true(faith.gods.size() <= 4, faith.civilization + " to keep at most four gods");
        const std::set<std::string> distinct(faith.gods.begin(), faith.gods.end());
        check::equal(distinct.size(), faith.gods.size(), "no god drawn twice");
        check::equal(faith.names.size(), faith.gods.size(), "a display name for each");
        if (faith.gods.size() > 1) ++drew_companions;
    }
    check::is_true(drew_companions > 0, "at least one people to have drawn beyond its patron");
}

TEST(the_weighted_pool_is_built_in_catalogue_order) {
    // The pool order is the order the draw's linear walk visits, so the catalogue breaks
    // every tie in every faith.
    const Bench bench = populated_world();
    const Religion religion = resolve_religion(bench.request());
    long long checked = 0;
    bool rises_somewhere = false;
    for (const Faith& faith : religion.faiths) {
        std::size_t previous = 0;
        double last_weight = 0.0;
        bool seen = false;
        for (const auto& entry : faith.weights) {
            const std::size_t index = catalogue_index(entry.first);
            check::is_true(index < pantheon_gods().size(), entry.first + " to be a known god");
            if (seen) check::is_true(index > previous, "weights to ascend the catalogue");
            if (seen && entry.second > last_weight) rises_somewhere = true;
            previous = index;
            last_weight = entry.second;
            seen = true;
            ++checked;
        }
    }
    check::is_true(checked >= 8, "a real number of weighted gods to have been checked");
    // Otherwise catalogue order and descending weight order would be the same list, and
    // a pool that had been sorted by weight would satisfy the assertion above by accident.
    check::is_true(rises_somewhere, "some faith's weights to rise along the catalogue");
}

TEST(leyline_potency_under_a_peoples_cities_raises_its_gods_weight) {
    Bench poor = populated_world();
    poor.potency("earth", 0.0);
    Bench rich = populated_world();
    rich.potency("earth", 3.0);

    const Religion a = resolve_religion(poor.request());
    const Religion b = resolve_religion(rich.request());
    check::equal(status_of(a, "god_earth"), std::string("manifest"), "the Earth to exist in both");

    const double before = weight_of(*faith_of(a, "dwarf"), "god_earth");
    const double after = weight_of(*faith_of(b, "dwarf"), "god_earth");
    check::is_true(before > 0.0, "the Earth to already be worth something to dwarves");
    check::close(after - before, 3.0, 1e-9, "the mean potency to be added outright");
}

TEST(the_moon_god_scores_nothing_from_a_world_saturated_with_magic) {
    // Its only affinity is the pseudo-school, which names no grid. Saturating every real
    // school therefore moves every other god and leaves this one exactly where it was.
    Bench bench = populated_world();
    for (const std::string school : {"weave", "umbral", "infernal", "radiant", "fire", "water",
                                     "earth", "air"}) {
        bench.network(school, {1.0});
        bench.potency(school, 4.0);
    }
    bench.lunar = 0.0;  // keeps cos_turning_moon out, so no high-god bonus reaches it
    const Religion religion = resolve_religion(bench.request());

    check::equal(status_of(religion, kMoonGodId), std::string("manifest"), "the moon to exist");
    const Faith* dwarves = faith_of(religion, "dwarf");
    check::is_true(has_weight(*dwarves, "god_earth"), "a god with a real school to be in the pool");
    check::is_true(weight_of(*dwarves, "god_earth") >= 4.0,
                   "and to have taken the saturated potency, or the control proves nothing");
    check::is_true(!has_weight(*dwarves, kMoonGodId),
                   "the moon to score zero and leave the pool despite the flood");
}

TEST(a_conversion_overrides_the_ancestor_and_the_ancestor_rejoins_the_pool) {
    Bench bench = populated_world();
    bench.network("fire", {1.0});
    bench.potency("fire", 0.5);
    Religion previous = resolve_religion(bench.request());
    check::equal(faith_of(previous, "dwarf")->patron, std::string("god_deep_forge"),
                 "the dwarves to start with their ancestor");

    for (Faith& faith : previous.faiths) {
        if (faith.civilization == "dwarf") faith.conversion = Conversion{"god_fire", 3};
    }
    const Religion after = resolve_religion(bench.request(&previous));

    const Faith* dwarves = faith_of(after, "dwarf");
    check::equal(dwarves->patron, std::string("god_fire"), "the god that walked among them");
    check::equal(dwarves->gods.front(), std::string("god_fire"), "and it leads the list");
    check::is_true(dwarves->conversion.has_value(), "the conversion to be exported");
    // The displaced ancestor is not discarded: it rejoins the pool at `strong`.
    check::is_true(has_weight(*dwarves, "god_deep_forge"), "the ancestor to rejoin the draw");
    check::close(weight_of(*dwarves, "god_deep_forge"), 2.0, 1e-12, "at the strong weight");
}

TEST(a_conversion_to_a_god_that_stopped_existing_is_exported_but_not_obeyed) {
    Bench bench = populated_world();
    Religion previous = resolve_religion(bench.request());
    check::equal(status_of(previous, "god_sky_court"), std::string("absent"),
                 "the Sky Court to have no sky to sit in");
    for (Faith& faith : previous.faiths) {
        if (faith.civilization == "dwarf") faith.conversion = Conversion{"god_sky_court", 1};
    }

    const Religion after = resolve_religion(bench.request(&previous));
    const Faith* dwarves = faith_of(after, "dwarf");
    check::is_true(dwarves->conversion.has_value(), "the record to survive");
    check::equal(dwarves->conversion->god_id, std::string("god_sky_court"), "naming the same god");
    check::equal(dwarves->patron, std::string("god_deep_forge"),
                 "but the patron to fall back to the ancestor");
}

TEST(a_people_whose_cities_name_no_known_civilization_is_refused) {
    Bench bench = populated_world();
    bench.city("city-x", "nobody", lean(10.0));
    const ReligionRequest request = bench.request();
    // Refused loudly rather than skipped: a people with cities and no faith is a bug in
    // whatever sited them, and a silent skip hides it.
    check::throws([&] { resolve_religion(request); }, "an unknown civilization");

    // The fact itself is still published unfiltered, exactly as the reference leaves it.
    const WorldFacts facts = world_facts(request);
    check::is_true(std::find(facts.civilizations_with_cities.begin(),
                             facts.civilizations_with_cities.end(),
                             "nobody") != facts.civilizations_with_cities.end(),
                   "the unknown profile to reach the exported facts");
}

// --- fear -----------------------------------------------------------------

TEST(family_decides_which_god_a_city_fears_before_the_name_does) {
    Bench bench = populated_world();
    // A nest that reads as a dragon but is a barrow-host. The family wins.
    bench.nest("nest-a", "Dragon-Barrow Host", "undead", lean(0.5));
    bench.nest("nest-b", "Pit Fiend", "infernal", lean(0.5));
    bench.network("umbral", {2.0});
    bench.network("infernal", {2.0});

    const Religion religion = resolve_religion(bench.request());
    const Faith* dwarves = faith_of(religion, "dwarf");
    std::set<std::string> feared;
    for (const FearRecord& fear : dwarves->fear) {
        feared.insert(fear.god_id);
        check::equal(fear.mode, std::string("fear"), "the mode");
        check::equal(fear.city_uid, std::string("city-a"), "the city that is afraid");
    }
    check::is_true(feared.count("god_umbral") == 1, "the undead nest to raise the Umbral");
    check::is_true(feared.count("god_infernal") == 1, "the infernal nest to raise the Infernal");
    check::is_true(feared.count("god_great_wyrm") == 0,
                   "and the Wyrm to stay out of it, whatever the nest is called");
}

TEST(one_fear_per_god_and_city_keeps_the_first_nest_by_id) {
    Bench bench = populated_world();
    bench.network("umbral", {2.0});
    bench.nest("nest-zulu", "Wight Hall", "undead", lean(0.5));
    bench.nest("nest-alpha", "Bone Field", "undead", lean(0.6));

    const Religion religion = resolve_religion(bench.request());
    const Faith* dwarves = faith_of(religion, "dwarf");
    long long umbral = 0;
    std::string kept;
    for (const FearRecord& fear : dwarves->fear) {
        if (fear.god_id == "god_umbral" && fear.city_uid == "city-a") {
            ++umbral;
            kept = fear.nest_id;
        }
    }
    check::equal(umbral, 1LL, "two undead nests to make one fear");
    // String order on the id, not the order the nests were listed in.
    check::equal(kept, std::string("nest-alpha"), "the lexicographically first nest");
}

TEST(a_nest_beyond_its_reach_and_a_real_animal_both_frighten_nobody) {
    Bench near = populated_world();
    near.network("umbral", {2.0});
    near.nest("nest-1", "Barrow Host", "undead", lean(0.5));
    const Religion control = resolve_religion(near.request());
    check::is_true(!faith_of(control, "dwarf")->fear.empty(),
                   "a nest at the gates to frighten somebody, or the rest proves nothing");

    Bench far = populated_world();
    far.network("umbral", {2.0});
    far.nest("nest-1", "Barrow Host", "undead", lean(170.0));
    check::equal(faith_of(resolve_religion(far.request()), "dwarf")->fear.size(), std::size_t{0},
                 "a nest half a world away");

    Bench tame = populated_world();
    tame.network("umbral", {2.0});
    tame.nest("nest-1", "Barrow Host", "undead", lean(0.5));
    tame.nests[0].real = true;
    const Religion tamed = resolve_religion(tame.request());
    check::equal(faith_of(tamed, "dwarf")->fear.size(), std::size_t{0}, "a real animal");
    check::equal(tamed.facts.families.size(), std::size_t{0}, "and it is not even a family");
}

TEST(the_fear_reach_is_metres_and_not_a_fraction_of_the_world) {
    // The 350 m floor and the doubled range are ABSOLUTE metres authored against the
    // reference world. Handing this design units instead of metres would make the same
    // nest near on one world and far on another, silently.
    Bench close = populated_world();
    close.network("umbral", {2.0});
    close.radius = 1000.0;
    close.nest("nest-1", "Barrow Host", "undead", lean(20.0), 500.0);  // ~349 m away
    check::is_true(!faith_of(resolve_religion(close.request()), "dwarf")->fear.empty(),
                   "a nest inside a thousand-metre reach");

    Bench wide = close;
    wide.radius = 100000.0;  // the same angle is now 35 km, far past a 1 km reach
    check::equal(faith_of(resolve_religion(wide.request()), "dwarf")->fear.size(), std::size_t{0},
                 "the same nest on a world a hundred times bigger");
}

// --- sites ----------------------------------------------------------------

TEST(sites_follow_the_ruins_and_the_moon_that_stood_over_them) {
    Bench bench = populated_world();
    bench.ruins.push_back({"ruin-3", 31, "self_magic", 2, "dwarf"});
    bench.ruins.push_back({"ruin-4", 37, "dragon", 1, "dwarf"});
    bench.ages.push_back({2, 0, 1.0});

    const Religion religion = resolve_religion(bench.request());
    check::equal(religion.sites.size(), std::size_t{3}, "two cults and one shrine");

    // Sorted by id, and the ids interleave lexicographically.
    for (std::size_t i = 1; i < religion.sites.size(); ++i) {
        check::is_true(religion.sites[i - 1].id < religion.sites[i].id, "sites to be sorted by id");
    }

    const ReligionSite& first = religion.sites[0];
    check::equal(first.id, std::string("cult-ruin-1"), "the first cult");
    check::equal(first.god_id, std::string(kSelfMagicCultGodId), "raised to the Weave");
    check::is_true(first.born_under_surge, "born under a weave tide of 1.5, past the 1.3 threshold");
    check::is_true(first.weave_tide.has_value(), "and the tide is recorded");

    const ReligionSite& second = religion.sites[1];
    check::equal(second.id, std::string("cult-ruin-3"), "the second cult");
    check::is_true(!second.born_under_surge, "a tide of 1.0 is not a surge");

    const ReligionSite& shrine = religion.sites[2];
    check::equal(shrine.id, std::string("shrine-ruin-2"), "the war shrine");
    check::equal(shrine.god_id, std::string(kWarShrineGodId), "raised to the Red Field");
    check::equal(shrine.node, 23, "on the ruin's node, which survives an age boundary");
}

TEST(a_ruin_in_no_recorded_age_records_no_tide) {
    Bench bench = populated_world();
    bench.ages.clear();
    const Religion religion = resolve_religion(bench.request());
    const ReligionSite& cult = religion.sites[0];
    check::equal(cult.kind, std::string("cult"), "still a cult");
    check::is_true(!cult.weave_tide.has_value(), "with no tide to record");
    check::is_true(!cult.born_under_surge, "and no surge to claim");
}

TEST(a_sleeping_weave_keeps_its_cults_and_an_absent_one_never_had_any) {
    Bench absent = populated_world();
    absent.magic.networks.erase("weave");
    const Religion never = resolve_religion(absent.request());
    check::equal(status_of(never, "god_weave"), std::string("absent"), "a Weave that never was");
    for (const ReligionSite& site : never.sites) {
        check::is_true(site.kind != "cult", "no cult to a god that never existed");
    }
    check::is_true(!never.sites.empty(), "the war shrine to still stand, or this proves nothing");

    Bench bench = populated_world();
    const Religion awake = resolve_religion(bench.request());
    bench.magic.networks.erase("weave");
    const Religion asleep = resolve_religion(bench.request(&awake));
    check::equal(status_of(asleep, "god_weave"), std::string("sleeping"), "a withdrawn Weave");
    long long cults = 0;
    for (const ReligionSite& site : asleep.sites) {
        if (site.kind == "cult") ++cults;
    }
    check::equal(cults, 1LL, "the god withdrew; the cult did not");
}

TEST(only_what_a_visitation_wrote_survives_a_resolve) {
    Bench bench = populated_world();
    Religion previous = resolve_religion(bench.request());
    check::is_true(previous.sites.size() >= 2, "the fixture to have ruin-born sites");
    ReligionSite theophany;
    theophany.id = "theophany-0-god_earth";
    theophany.kind = "theophany";
    theophany.god_id = "god_earth";
    theophany.node = 41;
    theophany.visitation = 0;
    previous.sites.push_back(theophany);

    bench.ruins.clear();
    const Religion after = resolve_religion(bench.request(&previous));
    check::equal(after.sites.size(), std::size_t{1}, "the ruin-born sites to be rebuilt away");
    check::equal(after.sites[0].id, std::string("theophany-0-god_earth"), "and the theophany to remain");
    check::equal(after.sites[0].visitation, 0, "carrying its visitation index");
}

// --- saints, sects and feasts ---------------------------------------------

TEST(saints_fold_only_under_a_cosmology_that_allows_folding) {
    Bench bench = populated_world();
    bool saw_folding = false;
    bool saw_free = false;
    for (std::uint32_t seed = 0; seed < 80 && !(saw_folding && saw_free); ++seed) {
        bench.seed = seed;
        const Religion religion = resolve_religion(bench.request());
        bool fold_free = false;
        for (const std::string_view id : fold_free_cosmologies()) {
            if (religion.cosmology.id == id) fold_free = true;
        }
        long long saints = 0;
        for (const GodRecord& god : religion.gods) {
            if (!god.saint_of.empty()) {
                ++saints;
                check::is_true(god.status != "absent", "an absent god to be nobody's saint");
                check::is_true(status_of(religion, god.saint_of) != "absent",
                               "a saint's god to exist");
            }
        }
        if (fold_free) {
            check::equal(saints, 0LL, "no saints under " + religion.cosmology.id);
            saw_free = true;
        } else if (saints > 0) {
            saw_folding = true;
        }
    }
    check::is_true(saw_free, "the sweep to reach a fold-free cosmology");
    check::is_true(saw_folding, "and one that folds, or half the rule is untested");
}

TEST(a_schism_founds_one_sect_whose_patron_is_the_orthodoxys_rival) {
    Bench bench = populated_world();
    bench.events.push_back({"diaspora_founded", "religious_schism", "human_heartland", 55, 1500});
    bench.events.push_back({"diaspora_founded", "exile", "human_heartland", 57, 1600});
    bench.events.push_back({"founded", "", "human_heartland", 59, 1700});

    const Religion religion = resolve_religion(bench.request());
    const Faith* humans = faith_of(religion, "human_heartland");
    check::is_true(humans->gods.size() > 1,
                   "the heartland to worship more than its ancestor, or the fallback is untested");
    check::equal(humans->sects.size(), std::size_t{1}, "one schism, one sect");

    const SectRecord& sect = humans->sects[0];
    check::equal(sect.node, 55, "on the node the schism founded");
    check::equal(sect.founding_year, 1500LL, "in the year it founded");
    // god_first_hearth has no rivals at all, so the sect follows the people's SECOND god.
    check::equal(sect.patron, humans->gods[1], "the second god, the patron having no live rival");
    check::equal(sect.rival, humans->patron, "and the orthodoxy it broke from");
    check::is_true(sect.epithet.size() > std::string(" of the Schism").size(),
                   "a name before the suffix");
    check::equal(sect.epithet.substr(sect.epithet.size() - 14), std::string(" of the Schism"),
                 "the suffix");

    check::equal(faith_of(religion, "dwarf")->sects.size(), std::size_t{0},
                 "a schism among one people not to split another");
}

TEST(feasts_follow_the_almanac) {
    Bench bench = populated_world();
    bench.almanac = almanac_with({1.0}, {{3, "surge", "earth", 1.7},
                                         {9, "full_moon", "", 0.0},
                                         {14, "surge", "weave", 1.9},
                                         {21, "hollow_night", "", 0.4},
                                         {40, "surge", "earth", 1.65}});
    bench.potency("earth", 5.0);  // makes the Earth a near-certain draw for the dwarves

    const Religion religion = resolve_religion(bench.request());
    const Faith* dwarves = faith_of(religion, "dwarf");
    check::is_true(std::find(dwarves->gods.begin(), dwarves->gods.end(), "god_earth") !=
                       dwarves->gods.end(),
                   "the dwarves to worship the Earth, or the surge list is untested");

    bool found = false;
    for (const auto& entry : dwarves->feasts.surges) {
        if (entry.first != "god_earth") continue;
        found = true;
        check::equal(entry.second.size(), std::size_t{2}, "two earth surges");
        check::equal(entry.second[0], 3, "the first");
        check::equal(entry.second[1], 40, "the second");
    }
    check::is_true(found, "a feast for the Earth's surges");

    check::equal(dwarves->feasts.hollow_nights.size(), std::size_t{1},
                 "hollow nights to be listed whoever keeps them");
    check::equal(dwarves->feasts.full_moons.size(), std::size_t{0},
                 "no full moons for a people that does not worship the moon");
    check::equal(dwarves->feasts.hollow_kept_as, std::string("dread"), "and kept as dread");
}

TEST(hollow_nights_are_vigils_only_for_a_wild_patron) {
    Bench bench = populated_world();
    bench.almanac = almanac_with({1.0}, {{21, "hollow_night", "", 0.4}});
    bench.network("umbral", {2.0});  // 2.0 > 1.2: the Hollow Hunger

    Religion previous = resolve_religion(bench.request());
    check::equal(record_of(previous, "god_umbral")->aspect, std::string("wild"), "a Wild Umbral");
    for (Faith& faith : previous.faiths) {
        if (faith.civilization == "dwarf") faith.conversion = Conversion{"god_umbral", 1};
    }
    const Religion vigil = resolve_religion(bench.request(&previous));
    check::equal(faith_of(vigil, "dwarf")->patron, std::string("god_umbral"), "as the patron");
    check::equal(faith_of(vigil, "dwarf")->feasts.hollow_kept_as, std::string("vigil"),
                 "hollow nights kept as vigils");

    // The same cult under a Sovereign Umbral keeps dread: it is the ASPECT that decides.
    Bench quiet = populated_world();
    quiet.almanac = bench.almanac;
    quiet.network("umbral", {0.5});
    Religion before = resolve_religion(quiet.request());
    check::equal(record_of(before, "god_umbral")->aspect, std::string("sovereign"), "a Quiet Judge");
    for (Faith& faith : before.faiths) {
        if (faith.civilization == "dwarf") faith.conversion = Conversion{"god_umbral", 1};
    }
    const Religion dread = resolve_religion(quiet.request(&before));
    check::equal(faith_of(dread, "dwarf")->patron, std::string("god_umbral"), "the same patron");
    check::equal(faith_of(dread, "dwarf")->feasts.hollow_kept_as, std::string("dread"),
                 "and dread instead");
}

// --- the hidden four ------------------------------------------------------

TEST(hidden_gods_are_exported_absent_and_carry_one_hidden_school) {
    const Bench bench = populated_world();
    const Religion religion = resolve_religion(bench.request());

    long long outsiders = 0;
    for (const GodRecord& god : religion.gods) {
        if (god.family != kOutsideFamily) continue;
        ++outsiders;
        check::equal(god.status, std::string("absent"), std::string(god.id) + " starts absent");
        check::equal(god.saint_of, std::string(), "and is nobody's saint, ever");
        check::equal(god.schools.size(), std::size_t{1}, "one school, singular");
        check::is_true(magic_school_index(god.schools[0]) >= kKnownSchools,
                       god.schools[0] + " to be a hidden school");
        check::is_true(!god.rule.empty(), "its rule to be exported for the corruption API");
    }
    check::equal(outsiders, 4LL, "the four the pantheon does not speak of");
}

TEST(a_hidden_god_that_woke_stays_awake_and_still_reaches_nobody) {
    Bench bench = populated_world();
    Religion previous = resolve_religion(bench.request());
    for (GodRecord& god : previous.gods) {
        if (god.id == "god_long_thirst") {
            god.status = "walking";
            god.aspect = "wild";
            god.aspect_name = "The Red Harvest";
        }
    }

    const Religion after = resolve_religion(bench.request(&previous));
    const GodRecord* thirst = record_of(after, "god_long_thirst");
    check::equal(thirst->status, std::string("walking"), "the status to carry forward");
    check::equal(thirst->aspect_name, std::string("The Red Harvest"), "and the face it wore");

    // The manifest set is closed before the hidden four are appended, so nothing can
    // reach them: no cosmology ranks one, no faith worships one, no fear names one.
    for (const std::string& high : after.cosmology.high) {
        check::is_true(hidden_pantheon_god(high) == nullptr, "no hidden high god");
    }
    for (const Faith& faith : after.faiths) {
        for (const std::string& god_id : faith.gods) {
            check::is_true(hidden_pantheon_god(god_id) == nullptr, "no hidden god worshipped");
        }
        for (const auto& entry : faith.weights) {
            check::is_true(hidden_pantheon_god(entry.first) == nullptr, "nor drawable");
        }
    }
    check::is_true(!after.faiths.empty(), "faiths to exist at all, or this proves nothing");
}

// --- the block itself -----------------------------------------------------

TEST(the_block_is_stamped_and_reproducible) {
    const Bench bench = populated_world();
    const Religion a = resolve_religion(bench.request());
    const Religion b = resolve_religion(bench.request());

    check::equal(a.version, kReligionVersion, "the version three downstream gates read");
    check::equal(std::string(a.catalogue.sha256), std::string(pantheon_identity().sha256),
                 "the catalogue identity a saved world is measured against");
    check::is_true(!a.method.empty() && !a.limits.empty(), "the provenance paragraphs");

    check::equal(a.gods.size(), b.gods.size(), "the same pantheon twice");
    for (std::size_t i = 0; i < a.gods.size(); ++i) {
        check::equal(a.gods[i].status, b.gods[i].status, a.gods[i].id + "'s status");
        check::equal(a.gods[i].aspect, b.gods[i].aspect, a.gods[i].id + "'s aspect");
    }
    check::equal(a.cosmology.id, b.cosmology.id, "the same cosmology");
    check::equal(a.faiths.size(), b.faiths.size(), "the same faiths");
    for (std::size_t i = 0; i < a.faiths.size(); ++i) {
        check::equal(a.faiths[i].gods.size(), b.faiths[i].gods.size(), "the same god count");
        for (std::size_t g = 0; g < a.faiths[i].gods.size(); ++g) {
            check::equal(a.faiths[i].gods[g], b.faiths[i].gods[g], "the same god in the same place");
        }
    }
    check::is_true(manifest_count(a) >= 10, "a pantheon worth comparing");
}

TEST(visitations_and_corruptions_outlive_the_block_that_carried_them) {
    // Both are opaque records written by APIs this stage does not implement. A world that
    // round-trips through here and loses its corruptions has lost game state permanently.
    Bench bench = populated_world();
    Religion previous = resolve_religion(bench.request());
    previous.visitations.push_back("{\"god_id\":\"god_earth\",\"age\":2}");
    previous.corruptions.push_back("{\"god_id\":\"god_long_thirst\",\"nodes\":3}");

    const Religion after = resolve_religion(bench.request(&previous));
    check::equal(after.visitations.size(), std::size_t{1}, "the visitation to survive");
    check::equal(after.corruptions.size(), std::size_t{1}, "and the corruption");
    check::equal(after.corruptions[0], OpaqueRecord("{\"god_id\":\"god_long_thirst\",\"nodes\":3}"),
                 "verbatim");
}

TEST(the_facts_report_the_world_that_raised_the_gods) {
    Bench bench = populated_world();
    bench.nest("nest-1", "Ash Dragon", "draconic", lean(5.0));
    bench.nest("nest-2", "Barrow Host", "undead", lean(6.0));
    bench.nest("nest-3", "Elk", "animal", lean(7.0));
    bench.nests[2].real = true;
    bench.cities[0].diaspora = true;
    bench.events.push_back({"diaspora_founded", "exile", "dwarf", 61, 900});

    const WorldFacts facts = world_facts(bench.request());
    check::equal(facts.dragons, 1, "one dragon");
    check::equal(facts.families.size(), std::size_t{2}, "two fantasy families; the elk is not one");
    check::equal(facts.families[0], std::string("draconic"), "sorted, first");
    check::equal(facts.families[1], std::string("undead"), "sorted, second");
    check::equal(facts.ruins, 2, "two ruins");
    check::equal(facts.diaspora, 2, "one diaspora city and one diaspora founding");
    check::equal(facts.wars, 2, "the wars of the age that turned");
    check::equal(facts.races.size(), std::size_t{2}, "dwarves and humans");
    check::equal(facts.civilizations_with_cities.size(), std::size_t{2}, "two peoples standing");
    check::close(facts.ocean_share, 0.3, 1e-12, "three tenths ocean");
    check::is_true(facts.magic_enabled, "magic to be on");
    check::equal(facts.primordial_count, 2, "water and earth of the four primordials");
}
