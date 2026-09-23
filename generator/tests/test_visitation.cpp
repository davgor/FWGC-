// Visitation invariants.
//
// The world is hand-built on the real 200 km preset -- its radius and its settlement spacing
// in metres -- so every test here also runs at a width the generator ships. The pantheon is
// resolved for real, because which gods exist, what they are called and whom they rival is
// the catalogue's to say and not a mock's.
//
// Two modules this port does not own are stood in for: the ruins module's legacy ladder and
// the LEY_DESTRUCTION sentences. The stand-ins below are deliberately plain and visibly not
// the real ones; they exist so a test can see exactly what a visitation asked and what it did
// with the answer.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "fwg/corruption.hpp"
#include "fwg/religion.hpp"
#include "fwg/villains.hpp"
#include "fwg/visitation.hpp"

using namespace fwg;

namespace {

constexpr int kSize = 33;
constexpr int kEquator = 16;
// The 200 km preset, in metres: a circumference of exactly 200 km, and settlement spacing
// resolved as 450 m * (200 km / the 11 148.96 m reference circumference).
constexpr double kRadius = 31830.988618379066;
constexpr double kSpacing = 8072.500651872125;
// One longitude step along the equator of a size-33 grid is exactly 6 250 m here, so a city
// k cells east of the centre is 6 250 k metres away and the 24 217.5 m reach takes k <= 3.

using Reasons = std::map<std::string, std::string, std::less<>>;

Reasons ley_reasons() {
    Reasons out;
    const auto table = magic_school_table();
    for (int s = 0; s < kKnownSchools; ++s) {
        const std::string name(table[static_cast<std::size_t>(s)].name);
        out[name] = "The " + name + " line took the city.";
    }
    return out;
}

// Everything the legacy resolver was asked, in the order it was asked.
struct LegacyProbe {
    std::vector<std::string> cities;
    std::vector<std::string> causes;
    std::vector<std::string> families;
    std::vector<std::string> god_schools;
    std::vector<std::vector<double>> potencies;
};

// A stand-in for the ruins module: the destroyer's own school where it has one, and earth
// otherwise. Plain on purpose, so what a test sees is what the visitation did with it.
VisitationLegacyResolver probing(const std::shared_ptr<LegacyProbe>& probe) {
    return [probe](const VisitationLegacyQuery& query) {
        probe->cities.push_back(query.city->uid);
        probe->causes.emplace_back(query.cause);
        probe->families.emplace_back(query.nest_family);
        probe->god_schools.emplace_back(query.god_school);
        probe->potencies.emplace_back(query.potencies.begin(), query.potencies.end());
        VisitationLegacy legacy;
        if (magic_school_index(query.cause) >= 0) {
            legacy = {std::string(query.cause), 1.5, "source"};
        } else if (query.cause.starts_with("divine_")) {
            legacy = {std::string(query.god_school), 4.0, "source"};
        } else {
            legacy = {"earth", 2.5, "region"};
        }
        return legacy;
    };
}

struct Bench {
    double radius = kRadius;
    double spacing = kSpacing;
    std::shared_ptr<const SphereGrid> grid;
    std::map<std::string, Grid> layers;
    MagicReport magic;
    Almanac almanac;
    Moon moon;
    Religion religion;
    std::vector<SettlementSite> cities;
    std::vector<NestSite> nests;
    std::vector<ReligionRuin> ruins;
    std::vector<VisitationRecord> visitations;
    VillainBlock villains;
    std::vector<CorruptionRecord> corruptions;
    std::uint32_t seed = 42;
    int age = 2;
    std::shared_ptr<LegacyProbe> probe = std::make_shared<LegacyProbe>();

    explicit Bench(double r = kRadius, double s = kSpacing) : radius(r), spacing(s) {
        grid = sphere_grid(kSize, radius);
        moon = seed_moon(seed);
        layers["water_type"] = Grid(kSize, 0.0);
        // Every school the table names. Each known one holds a key point at the north pole,
        // a quarter of the world from the equator where every god here is summoned: enough
        // for its god to be manifest, and far outside any reach. The hidden four are EMPTY,
        // which is the world's normal state.
        for (const MagicSchool& school : magic_school_table()) {
            LeyNetwork net;
            net.name = std::string(school.name);
            if (!school.hidden) net.nodes.push_back({net.name + "-node-0", Vec3{0.0, 1.0, 0.0}, 1.0});
            magic.networks[net.name] = net;
            layers["ley_" + net.name] = Grid(kSize, 0.0);
        }
        magic.enabled = true;
    }

    Vec3 at(int x, int z) const { return direction(x, z, kSize); }

    void city(const std::string& uid, int x, int z, const std::string& profile = "human_heartland") {
        SettlementSite site;
        site.id = static_cast<int>(cities.size());
        site.uid = uid;
        site.node = grid->node_index(x, z);
        site.x = x;
        site.z = z;
        site.direction = at(x, z);
        site.name = "City " + uid;
        site.population_profile = profile;
        cities.push_back(site);
    }

    const SettlementSite& site(const std::string& uid) const {
        for (const SettlementSite& c : cities) {
            if (c.uid == uid) return c;
        }
        check::fail("no city " + uid);
    }

    void nest(const std::string& id, std::string_view name, std::string_view family, const Vec3& where,
              bool real = false, double range_m = 2000.0) {
        NestSite n;
        n.id = id;
        n.name = name;
        n.family = family;
        n.direction = where;
        n.tier = 5;
        n.range_m = range_m;
        n.real = real;
        nests.push_back(n);
    }

    ReligionRequest religion_request(const Religion* previous = nullptr) const {
        ReligionRequest r;
        r.seed = seed;
        r.magic = &magic;
        r.layers = &layers;
        r.almanac = &almanac;
        for (const SettlementSite& site : cities) {
            ReligionCity c;
            c.uid = site.uid;
            c.population_profile = site.population_profile;
            c.x = site.x;
            c.z = site.z;
            c.node = site.node;
            c.direction = site.direction;
            r.cities.push_back(c);
        }
        r.ruins = ruins;
        r.used_civilizations = {"human_heartland", "dwarf"};
        r.ocean_km2 = 300.0;
        r.lake_km2 = 100.0;
        r.dry_km2 = 600.0;
        r.globe_radius_m = radius;
        r.previous = previous;
        return r;
    }

    void resolve() { religion = resolve_religion(religion_request()); }

    VisitationRequest request(const std::string& god_id) const {
        VisitationRequest r;
        r.seed = seed;
        r.age = age;
        r.globe_radius_m = radius;
        r.settlement_spacing_m = spacing;
        r.grid = grid.get();
        r.layers = &layers;
        r.moon = moon;
        r.founding_end_year = 3;
        r.god_id = god_id;
        r.target_city_uid = "city-centre";
        r.magic = magic;
        r.religion = religion;
        r.visitations = visitations;
        r.cities = cities;
        r.nests = nests;
        r.ley_destruction = ley_reasons();
        r.ruin_legacy = probing(probe);
        return r;
    }

    // Everything the caller owes between and after the two halves of a summons, with a
    // rebuild that changes nothing but what the summons itself changed.
    VisitationOutcome summon(const VisitationRequest& r) {
        const VisitationSummons summons = summon_god(r);
        VisitationRebuilt rebuilt;
        rebuilt.cities = summons.survivors;
        rebuilt.nests = nests;
        const VisitationOutcome outcome = complete_summons(summons, rebuilt);
        for (const VisitationRuin& ruin : summons.new_ruins) {
            ruins.push_back({ruin.id, ruin.node, ruin.cause, ruin.destroyed_age, ruin.population_profile});
        }
        magic = summons.magic;
        cities = summons.survivors;
        nests = outcome.nests;
        visitations = outcome.visitations;
        religion = resolve_religion(religion_request(&outcome.religion));
        return outcome;
    }
};

// Seven cities along the equator. The centre is where every god here is summoned.
Bench populated(double radius = kRadius, double spacing = kSpacing) {
    Bench b(radius, spacing);
    b.city("city-centre", 8, kEquator);
    b.city("city-east-1", 9, kEquator);   //  6 250 m
    b.city("city-east-2", 10, kEquator);  // 12 500 m
    b.city("city-east-3", 11, kEquator);  // 18 750 m, inside the 24 217.5 m reach
    b.city("city-east-4", 12, kEquator);  // 25 000 m, just outside it
    b.city("city-west-1", 7, kEquator, "dwarf");
    b.city("city-far", 24, kEquator, "dwarf");  // the other side of the world
    b.resolve();
    return b;
}

std::string status_of(const Religion& religion, std::string_view id) {
    for (const GodRecord& god : religion.gods) {
        if (god.id == id) return god.status;
    }
    return "<missing>";
}

GodRecord* god_of(Religion& religion, std::string_view id) {
    for (GodRecord& god : religion.gods) {
        if (god.id == id) return &god;
    }
    check::fail("no god " + std::string(id));
}

Faith* faith_of(Religion& religion, std::string_view civilization) {
    for (Faith& faith : religion.faiths) {
        if (faith.civilization == civilization) return &faith;
    }
    check::fail("no faith for " + std::string(civilization));
}

// A people that does not worship this god, whatever the faith draw gave it. A civic god is
// drawable by everyone, so without this a test about "the god's own faithful" would depend on
// which companions a seed happened to draw.
void forsake(Religion& religion, std::string_view civilization, std::string_view god_id) {
    Faith* faith = faith_of(religion, civilization);
    std::erase(faith->gods, std::string(god_id));
    if (faith->patron == god_id) faith->patron = faith->gods.empty() ? std::string() : faith->gods.front();
}

const LeyNode* node_of(const LeyNetwork& net, const std::string& id) {
    for (const LeyNode& node : net.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

const LeyEdge* edge_of(const LeyNetwork& net, const std::string& id) {
    for (const LeyEdge& edge : net.edges) {
        if (edge.id == id) return &edge;
    }
    return nullptr;
}

// The refusal a call raised, so a test can say WHICH one. `check::throws` accepts any
// exception at all, and a request broken in one field can be refused by an earlier check for a
// reason that has nothing to do with the property under test.
template <typename Fn>
VisitationError refusal(Fn&& fn, const std::string& what) {
    try {
        fn();
    } catch (const VisitationError& error) {
        return error;
    } catch (const std::exception& other) {
        check::fail(what + ": expected a VisitationError, got " + other.what());
    }
    check::fail(what + ": expected a refusal, none raised");
}

bool says(const VisitationError& error, const std::string& text) {
    return std::string(error.what()).find(text) != std::string::npos;
}

bool contains_text(const std::vector<std::string>& haystack, const std::string& needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

bool in_any_network(const MagicReport& magic, const std::string& id) {
    for (const auto& [name, net] : magic.networks) {
        if (node_of(net, id) != nullptr) return true;
    }
    return false;
}

const VisitationRuin* ruin_of(const VisitationSummons& summons, const std::string& city_uid) {
    for (const VisitationRuin& ruin : summons.new_ruins) {
        if (ruin.city_uid == city_uid) return &ruin;
    }
    return nullptr;
}

// Runs the real corruption API on the bench, then does what corruption's caller owes: the
// rebuild re-resolves religion and the revelation is replayed on the result.
void corrupt(Bench& b) {
    CorruptionRequest r;
    r.seed = b.seed;
    r.age = b.age;
    r.globe_radius_m = b.radius;
    r.settlement_spacing_m = b.spacing;
    r.encounters = 40;  // past every hidden god's climb
    for (const SettlementSite& site : b.cities) {
        VillainCity c;
        c.uid = site.uid;
        c.id = site.id;
        c.node = site.node;
        c.direction = site.direction;
        c.population_profile = site.population_profile;
        c.population_estimate = 100000.0;  // a rich world, which is what the Long Thirst wants
        r.cities.push_back(c);
    }
    const SettlementSite& seat = b.site("city-west-1");
    Villain v;
    v.uid = "villain-1-" + std::to_string(seat.node);
    v.region = "earth-node-0";
    v.born_age = 1;
    v.seat_uid = seat.uid;
    v.node = seat.node;
    v.direction = seat.direction;
    v.school = "earth";
    v.held_nodes = {"earth-node-0"};
    v.tier = 1.2;
    v.reach_m = kReachSpacingsPerTier * v.tier * b.spacing;
    v.growth = "hoarding";
    v.status = "living";
    r.villains.people.push_back(v);
    r.villains.tiers[v.region] = v.tier;
    r.magic = b.magic;
    r.religion = b.religion;

    const CorruptionOutcome outcome = corrupt_world(r);
    if (!outcome.acted) check::fail("the fixture to have corrupted the world");
    b.magic = outcome.magic;
    b.villains = outcome.villains;
    b.corruptions = outcome.corruptions;
    b.religion = resolve_religion(b.religion_request(&outcome.religion));
    reveal_hidden_god(b.religion, outcome.god_id, outcome.villain_uid, b.age);
}

}  // namespace

// --- the age boundary ------------------------------------------------------

TEST(an_age_can_turn_over_a_corrupted_world) {
    // The reference could not do this. A hidden god's return marks it `walking` and writes no
    // visitation record, the age transition sent every walking god home, and sending home a
    // god with no record raises: the first age turn after any corruption failed. Here the age
    // turn runs exactly as the transition runs it -- `walking_gods`, then `depart_god` for each
    // -- over a world that holds one god of each kind.
    Bench bench = populated();
    corrupt(bench);
    check::equal(status_of(bench.religion, "god_long_thirst"), std::string("walking"),
                 "the intruder to walk after the corruption's own rebuild");
    check::equal(bench.visitations.size(), std::size_t{0}, "without a visitation record");
    const LeyNetwork& blood_before = bench.magic.networks.at("blood");
    check::equal(blood_before.nodes.size(), std::size_t{4}, "and its footprint to be its cluster");

    bench.summon(bench.request("god_red_field"));
    check::equal(status_of(bench.religion, "god_red_field"), std::string("walking"),
                 "a summoned god to walk after the pantheon re-resolves");
    check::equal(bench.visitations.size(), std::size_t{1}, "with a record");

    // What the reference iterated: every god whose status reads walking. It names both, and
    // one of them has nowhere to go back to.
    std::vector<std::string> by_status;
    for (const GodRecord& god : bench.religion.gods) {
        if (god.status == "walking") by_status.push_back(god.id);
    }
    check::equal(by_status.size(), std::size_t{2}, "two gods on the plane");
    const VisitationError sent = refusal(
        [&] { depart_god({"god_long_thirst", bench.age + 1, bench.magic, bench.religion, bench.visitations}); },
        "sending the intruder home, which is where the reference raised");
    check::is_true(says(sent, "does not walk by visitation"), "refused because it has no record, by name");

    // The age turn.
    const int age = bench.age + 1;
    const std::vector<std::string> sent_home = walking_gods(bench.religion, bench.visitations);
    check::equal(sent_home.size(), std::size_t{1}, "one god for the age to send home");
    check::equal(sent_home[0], std::string("god_red_field"), "and it is the one that was summoned");
    for (const std::string& god_id : sent_home) {
        VisitationDeparture gone = depart_god({god_id, age, bench.magic, bench.religion, bench.visitations});
        bench.magic = std::move(gone.magic);
        bench.religion = std::move(gone.religion);
        bench.visitations = std::move(gone.visitations);
    }

    check::equal(status_of(bench.religion, "god_red_field"), std::string("manifest"), "the visitor went home");
    check::is_true(!god_of(bench.religion, "god_red_field")->avatar.has_value(), "and took its avatar with it");
    check::is_true(bench.visitations[0].departed_age.has_value(), "its record closed");
    check::equal(*bench.visitations[0].departed_age, age, "in the age that turned");
    for (const std::string& id : bench.visitations[0].cluster) {
        check::is_true(!in_any_network(bench.magic, id), id + " to have collapsed");
    }
    check::is_true(node_of(bench.magic.networks.at("infernal"), "footprint-god_red_field-0") != nullptr,
                   "a footprint left in the god's first school");

    check::equal(status_of(bench.religion, "god_long_thirst"), std::string("walking"),
                 "the intruder still walks: an age does not send a corruption home");
    const LeyNetwork& blood = bench.magic.networks.at("blood");
    check::equal(blood.nodes.size(), std::size_t{4}, "and its corrupted nodes stand");
    for (std::size_t k = 0; k < 4; ++k) {
        check::equal(blood.nodes[k].id, "corrupt-god_long_thirst-0-" + std::to_string(k), "corrupted node");
    }

    // The tail of the age re-resolves the pantheon.
    const Religion next = resolve_religion(bench.religion_request(&bench.religion));
    check::equal(status_of(next, "god_long_thirst"), std::string("walking"), "still walking after the resolve");
    check::equal(status_of(next, "god_red_field"), std::string("manifest"), "the visitor still home");
    bool pilgrimage = false;
    for (const ReligionSite& site : next.sites) {
        if (site.id == "theophany-0-god_red_field") pilgrimage = site.kind == "pilgrimage";
    }
    check::is_true(pilgrimage, "the theophany carried forward as a pilgrimage");

    // And the next age turns too: nobody is left for it to send home.
    check::equal(walking_gods(next, bench.visitations).size(), std::size_t{0}, "nothing left to send home");

    // The intruder is still opposed the way it always was, through its corruption.
    CleanseRequest cleanse;
    cleanse.age = age;
    cleanse.power = 1.0;
    cleanse.corruption = 0;
    cleanse.magic = bench.magic;
    cleanse.religion = next;
    cleanse.villains = bench.villains;
    cleanse.corruptions = bench.corruptions;
    const CleanseOutcome cleansed = cleanse_world(cleanse);
    check::is_true(cleansed.cleansed, "a full-power cleanse to end the cluster");
    check::equal(status_of(cleansed.religion, "god_long_thirst"), std::string("sleeping"),
                 "and to put the intruder back to sleep");
}

TEST(walking_gods_names_only_gods_that_walk_by_visitation) {
    Bench bench = populated();
    Religion religion = bench.religion;
    god_of(religion, "god_fire")->status = "walking";         // summoned, record open
    god_of(religion, "god_water")->status = "walking";        // revealed-style: no record at all
    god_of(religion, "god_earth")->status = "walking";        // its only record is closed
    god_of(religion, "god_crown_law")->status = "manifest";   // an open record, but not walking
    god_of(religion, "god_forge_craft")->status = "walking";  // summoned, record open

    std::vector<VisitationRecord> records(4);
    records[0].index = 0;
    records[0].god_id = "god_earth";
    records[0].departed_age = 1;
    records[1].index = 1;
    records[1].god_id = "god_forge_craft";
    records[2].index = 2;
    records[2].god_id = "god_crown_law";
    records[3].index = 3;
    records[3].god_id = "god_fire";

    const std::vector<std::string> gods = walking_gods(religion, records);
    check::equal(gods.size(), std::size_t{2}, "two gods walk by visitation");
    // Pantheon order, not record order: god_fire is fifth in the catalogue and the forge
    // twentieth, while their records run the other way.
    check::equal(gods[0], std::string("god_fire"), "the first, in pantheon order");
    check::equal(gods[1], std::string("god_forge_craft"), "the second");
    check::equal(walking_gods(religion, {}).size(), std::size_t{0}, "and none without records");
}

TEST(departure_refuses_by_name_what_it_cannot_send_home) {
    Bench bench = populated();
    Religion religion = bench.religion;
    god_of(religion, "god_water")->status = "walking";
    const auto depart = [&](const std::string& id, const std::vector<VisitationRecord>& records) {
        return depart_god({id, 3, bench.magic, religion, records});
    };
    check::is_true(says(refusal([&] { depart("god_nobody", {}); }, "a stranger"), "is no god of this world"),
                   "a god this world has never heard of");
    check::is_true(says(refusal([&] { depart("god_fire", {}); }, "a god at home"), "only a walking god can depart"),
                   "a god that does not walk");
    check::is_true(says(refusal([&] { depart("god_water", {}); }, "a revealed god"), "does not walk by visitation"),
                   "a walking god with no record, as corruption leaves one");

    // Two records for one god, the older closed: the most recent open one is the one to close.
    std::vector<VisitationRecord> records(2);
    for (int i = 0; i < 2; ++i) {
        VisitationRecord& r = records[static_cast<std::size_t>(i)];
        r.index = i;
        r.god_id = "god_water";
        r.schools = {"water"};
        r.direction = bench.at(8, kEquator);
    }
    records[0].departed_age = 1;
    const VisitationDeparture gone = depart("god_water", records);
    check::equal(gone.record.index, 1, "the open record");
    check::equal(gone.footprint, std::string("footprint-god_water-1"), "and its footprint");
    check::is_true(says(refusal([&] { depart("god_water", {records[0]}); }, "closed records"),
                        "does not walk by visitation"),
                   "a god whose every record is closed");
}

// --- the avatar ------------------------------------------------------------

TEST(a_gods_schools_follow_the_known_order_and_the_moon_follows_its_lean) {
    const Moon moon = seed_moon(42);
    // The Red Field's affinity lists fire first; known order puts infernal first, and the
    // first school is where its divine ruins and its footprint go.
    const std::vector<std::string> red = god_schools("god_red_field", moon, 0);
    check::equal(red.size(), std::size_t{2}, "two schools");
    check::equal(red[0], std::string("infernal"), "infernal first");
    check::equal(red[1], std::string("fire"), "then fire");
    const std::vector<std::string> hearth = god_schools("god_hearth_harvest", moon, 0);
    check::equal(hearth.size(), std::size_t{1}, "one school");
    check::equal(hearth[0], std::string("earth"), "earth");

    long long still_day = -1;
    long long restless_day = -1;
    for (long long day = 0; day < 2LL * moon.nod_days && (still_day < 0 || restless_day < 0); ++day) {
        const bool still = moon_state(moon, day).nod_degrees >= 0.0;
        if (still && still_day < 0) still_day = day;
        if (!still && restless_day < 0) restless_day = day;
    }
    check::is_true(still_day >= 0 && restless_day >= 0, "a day of each leaning within two nods");
    const std::vector<std::string> still = god_schools(kMoonGodId, moon, still_day);
    const std::vector<std::string> restless = god_schools(kMoonGodId, moon, restless_day);
    const std::vector<std::string> expect_still{"umbral", "radiant", "water", "earth"};
    const std::vector<std::string> expect_restless{"weave", "infernal", "fire", "air"};
    check::equal(still.size(), std::size_t{4}, "the Still hemisphere");
    check::equal(restless.size(), std::size_t{4}, "the Restless one");
    for (std::size_t i = 0; i < 4; ++i) {
        check::equal(still[i], expect_still[i], "still school " + std::to_string(i));
        check::equal(restless[i], expect_restless[i], "restless school " + std::to_string(i));
    }

    check::throws([] { god_schools("god_long_thirst", seed_moon(42), 0); }, "a hidden god, which walks only by corruption");
    check::throws([] { god_schools("god_nobody", seed_moon(42), 0); }, "a god nobody wrote");
    check::throws([] { god_schools(kMoonGodId, Moon{}, 0); }, "the moon god under an unseeded moon");
}

TEST(a_summons_plants_five_nodes_per_school_on_one_shared_bearing) {
    Bench bench = populated();
    const VisitationRequest request = bench.request("god_red_field");
    const VisitationSummons summons = summon_god(request);
    check::equal(summons.cluster.size(), std::size_t{10}, "five nodes in each of two schools");
    check::equal(summons.cluster[0], std::string("avatar-god_red_field-0-infernal-0"), "school-major ids");
    check::equal(summons.cluster[9], std::string("avatar-god_red_field-0-fire-4"), "ring-point-minor");
    check::is_true(summons.rebuild_required, "and the caller owes the rebuild");
    check::equal(visitation_order().size(), std::size_t{11}, "eleven ordered steps are published");

    const LeyNetwork& infernal = summons.magic.networks.at("infernal");
    const LeyNetwork& fire = summons.magic.networks.at("fire");
    const LeyNode* centre = node_of(infernal, "avatar-god_red_field-0-infernal-0");
    check::is_true(centre != nullptr, "the centre node");
    check::close(centre->intensity, kAvatarIntensity, 0.0, "at the network ceiling");
    check::close(dot(centre->direction, bench.site("city-centre").direction), 1.0, 1e-12,
                 "standing on the city it was summoned to");

    std::vector<Vec3> ring;
    for (int k = 1; k <= kAvatarRingPoints; ++k) {
        const std::string suffix = "-" + std::to_string(k);
        const LeyNode* a = node_of(infernal, "avatar-god_red_field-0-infernal" + suffix);
        const LeyNode* b = node_of(fire, "avatar-god_red_field-0-fire" + suffix);
        check::is_true(a != nullptr && b != nullptr, "ring point " + std::to_string(k) + " in both schools");
        check::close(a->intensity, kAvatarRingIntensity, 0.0, "a ring intensity");
        // The ring sits half a settlement spacing out, which scales with the planet.
        check::close(great_circle_m(kRadius, centre->direction, a->direction), kAvatarRingSpacings * kSpacing,
                     1e-6, "the ring radius in metres");
        // ONE bearing, drawn once before the school loop.
        check::close(a->direction.x, b->direction.x, 0.0, "the same bearing in every school");
        check::close(a->direction.y, b->direction.y, 0.0, "in every component");
        check::close(a->direction.z, b->direction.z, 0.0, "all three");
        ring.push_back(a->direction);
    }
    for (std::size_t k = 0; k < ring.size(); ++k) {
        const double side = great_circle_m(kRadius, ring[k], ring[(k + 1) % ring.size()]);
        check::close(side, great_circle_m(kRadius, ring[0], ring[1]), 1e-6, "four evenly spaced points");
    }

    // Variation turns the ring and nothing else.
    VisitationRequest varied = request;
    varied.variation = 7;
    const VisitationSummons turned = summon_god(varied);
    const LeyNetwork& turned_infernal = turned.magic.networks.at("infernal");
    check::close(dot(node_of(turned_infernal, "avatar-god_red_field-0-infernal-0")->direction, centre->direction),
                 1.0, 1e-15, "the centre does not move");
    check::is_true(
        std::abs(node_of(turned_infernal, "avatar-god_red_field-0-infernal-1")->direction.x - ring[0].x) > 1e-9,
        "but the ring turns");
}

TEST(opposed_schools_are_suppressed_inside_the_reach_and_restored_exactly) {
    // The Red Field saturates infernal and fire, so it suppresses radiant and water: TWO
    // networks. Departure must look each entry up in its own; restoring into whichever network
    // it last touched misses every entry of the other and leaves that rival halved for good.
    Bench bench = populated();
    LeyNetwork& radiant = bench.magic.networks["radiant"];
    radiant.nodes.push_back({"radiant-near", bench.at(9, kEquator), 1.2});   // 1: inside
    radiant.nodes.push_back({"radiant-out", bench.at(20, kEquator), 0.9});   // 2: outside
    radiant.nodes.push_back({"radiant-far", bench.at(24, kEquator), 1.1});   // 3: outside
    radiant.edges.push_back({"radiant-line-a", 1, 2, 1.3});  // touches the near node
    radiant.edges.push_back({"radiant-line-b", 2, 3, 0.8});  // touches nothing near
    LeyNetwork& water = bench.magic.networks["water"];
    water.nodes.push_back({"water-near", bench.at(7, kEquator), 1.4});
    water.nodes.push_back({"water-out", bench.at(20, kEquator), 1.0});
    water.edges.push_back({"water-line", 1, 2, 1.1});
    bench.resolve();

    VisitationRequest request = bench.request("god_red_field");
    request.wrath = 1.0;  // the scale is 1 - 0.5 * wrath: halved
    const VisitationSummons summons = summon_god(request);
    const LeyNetwork& r = summons.magic.networks.at("radiant");
    const LeyNetwork& w = summons.magic.networks.at("water");
    check::close(node_of(r, "radiant-near")->intensity, 0.6, 1e-12, "a rival key point in reach, halved");
    check::close(edge_of(r, "radiant-line-a")->intensity, 0.65, 1e-12, "a line touching it, halved");
    check::close(node_of(r, "radiant-out")->intensity, 0.9, 0.0, "a key point out of reach, untouched");
    check::close(edge_of(r, "radiant-line-b")->intensity, 0.8, 0.0, "a line out of reach, untouched");
    check::close(node_of(r, "radiant-node-0")->intensity, 1.0, 0.0, "the pole, untouched");
    check::close(node_of(w, "water-near")->intensity, 0.7, 1e-12, "the second rival school, halved");
    check::close(edge_of(w, "water-line")->intensity, 0.55, 1e-12, "and its line");
    check::close(node_of(summons.magic.networks.at("fire"), "avatar-god_red_field-0-fire-0")->intensity,
                 kAvatarIntensity, 0.0, "the god's own schools are not each other's rivals");

    check::equal(summons.restore.size(), std::size_t{4}, "four entries, two per rival school");
    check::equal(summons.restore[0].school, std::string("radiant"), "infernal's rival first");
    check::equal(summons.restore[0].node_id, std::string("radiant-near"), "its key point");
    check::close(summons.restore[0].intensity, 1.2, 0.0, "recorded BEFORE scaling");
    check::equal(summons.restore[1].line_id, std::string("radiant-line-a"), "then its line");
    check::equal(summons.restore[2].school, std::string("water"), "fire's rival second");
    check::equal(summons.restore[3].line_id, std::string("water-line"), "and its line");

    VisitationRebuilt rebuilt;
    rebuilt.cities = summons.survivors;
    const VisitationOutcome arrived = complete_summons(summons, rebuilt);
    const VisitationDeparture gone =
        depart_god({"god_red_field", 3, summons.magic, arrived.religion, arrived.visitations});
    const LeyNetwork& r2 = gone.magic.networks.at("radiant");
    const LeyNetwork& w2 = gone.magic.networks.at("water");
    // EXACT, not re-scaled: the stored value is written back.
    check::close(node_of(r2, "radiant-near")->intensity, 1.2, 0.0, "the radiant key point restored exactly");
    check::close(edge_of(r2, "radiant-line-a")->intensity, 1.3, 0.0, "and its line");
    check::close(node_of(w2, "water-near")->intensity, 1.4, 0.0, "the water key point restored exactly");
    check::close(edge_of(w2, "water-line")->intensity, 1.1, 0.0, "and its line");
    check::equal(gone.unrestored.size(), std::size_t{0}, "every entry found its target");
}

TEST(a_god_whose_schools_oppose_each_other_suppresses_its_own_avatar) {
    // The Outer Unnamed saturates weave and umbral, which oppose each other, and suppression
    // runs after the cluster is planted -- so at full wrath it halves its own avatar. That is
    // the reference's behaviour, kept; a port that excluded the god's own cluster would build
    // a different world for this god.
    Bench bench = populated();
    GodRecord* outer = god_of(bench.religion, "god_outer_unnamed");
    outer->status = "manifest";
    outer->aspect = "sovereign";
    VisitationRequest request = bench.request("god_outer_unnamed");
    request.wrath = 1.0;
    const VisitationSummons summons = summon_god(request);
    check::equal(summons.schools.size(), std::size_t{2}, "weave and umbral");
    check::equal(summons.cluster.size(), std::size_t{10}, "ten avatar nodes");
    for (const std::string& id : summons.cluster) {
        const bool centre = id.back() == '0';
        const LeyNetwork& net = summons.magic.networks.at(id.find("-weave-") != std::string::npos ? "weave" : "umbral");
        check::close(node_of(net, id)->intensity, (centre ? kAvatarIntensity : kAvatarRingIntensity) * 0.5, 1e-12,
                     id + " halved by its own other school");
    }
    int own = 0;
    for (const VisitationRestore& item : summons.restore) {
        if (item.node_id.rfind("avatar-god_outer_unnamed-", 0) == 0) ++own;
    }
    check::equal(own, 10, "and every one of them is in the restore list");

    VisitationRebuilt rebuilt;
    rebuilt.cities = summons.survivors;
    const VisitationOutcome arrived = complete_summons(summons, rebuilt);
    const VisitationDeparture gone =
        depart_god({"god_outer_unnamed", 3, summons.magic, arrived.religion, arrived.visitations});
    // Those entries miss at departure, because the cluster is removed first. Skipped, as the
    // reference skips them, and reported rather than silent.
    check::equal(gone.unrestored.size(), std::size_t{10}, "its own entries find nothing to restore");
}

TEST(earth_and_air_have_no_opposite_so_their_gods_suppress_nothing) {
    for (const std::string god : {"god_hearth_harvest", "god_road_market"}) {
        Bench bench = populated();
        bench.magic.networks["water"].nodes.push_back({"water-near", bench.at(9, kEquator), 1.4});
        VisitationRequest request = bench.request(god);
        request.wrath = 1.0;
        const VisitationSummons summons = summon_god(request);
        check::equal(summons.schools.size(), std::size_t{1}, god + " to saturate one school");
        check::equal(summons.restore.size(), std::size_t{0}, god + " to suppress nothing");
        for (const auto& [name, net] : request.magic.networks) {
            const LeyNetwork& after = summons.magic.networks.at(name);
            for (const LeyNode& node : net.nodes) {
                check::close(node_of(after, node.id)->intensity, node.intensity, 0.0, name + " untouched by " + god);
            }
        }
    }
}

// --- the fate lottery ------------------------------------------------------

TEST(each_city_rolls_on_its_own_stream) {
    // One stream per city, keyed on its uid: a city's roll depends on nothing but its own
    // identity. Adding cities elsewhere, or visiting these in another order, must not move it.
    Bench bench = populated();
    god_of(bench.religion, "god_red_field")->aspect = "wild";  // four times a sovereign's weight
    std::uint32_t seed = 0;
    VisitationSummons base;
    for (std::uint32_t s = 1; s <= 200 && seed == 0; ++s) {
        VisitationRequest probe = bench.request("god_red_field");
        probe.seed = s;
        probe.wrath = 1.0;
        const VisitationSummons summons = summon_god(probe);
        const int in_reach = 5;  // centre, three east and one west
        const int fallen = static_cast<int>(summons.new_ruins.size());
        if (fallen > 0 && fallen < in_reach) {
            seed = s;
            base = summons;
        }
    }
    check::is_true(seed != 0, "a seed on which some cities in reach fall and some stand");

    // The same world visited in the opposite order, with two more cities in it -- and every
    // site's integer id renumbered to its new position, as any change to the set renumbers
    // them. Only the uid survives that, so a roll keyed on anything else moves here.
    Bench shuffled = populated();
    god_of(shuffled.religion, "god_red_field")->aspect = "wild";
    std::reverse(shuffled.cities.begin(), shuffled.cities.end());
    shuffled.city("city-newcomer", 9, kEquator - 1);  // inside the reach
    shuffled.city("city-stranger", 20, kEquator);     // far outside it
    for (std::size_t i = 0; i < shuffled.cities.size(); ++i) shuffled.cities[i].id = static_cast<int>(i);
    check::is_true(shuffled.site("city-centre").id != bench.site("city-centre").id, "the centre renumbered");
    VisitationRequest again = shuffled.request("god_red_field");
    again.seed = seed;
    again.wrath = 1.0;
    const VisitationSummons other = summon_god(again);

    int compared = 0;
    for (const char* uid : {"city-centre", "city-east-1", "city-east-2", "city-east-3", "city-west-1"}) {
        const VisitationRuin* a = ruin_of(base, uid);
        const VisitationRuin* b = ruin_of(other, uid);
        check::equal(a != nullptr, b != nullptr, std::string(uid) + " to meet the same fate");
        if (a == nullptr || b == nullptr) continue;
        check::close(a->roll, b->roll, 0.0, std::string(uid) + " to roll the same draw");
        check::equal(a->cause, b->cause, std::string(uid) + " to fall to the same cause");
        ++compared;
    }
    check::is_true(compared > 0, "at least one ruin compared draw for draw");
}

TEST(the_causes_are_walked_ley_first_and_the_god_last) {
    // The selector walk subtracts in list order and defaults to the last cause, so the order is
    // attribution: ley, then nests, then the god. A city on fire ground at the very centre has
    // exactly two causes and a known split between them.
    Bench bench;
    bench.city("city-centre", 8, kEquator);
    bench.resolve();
    bench.layers["ley_fire"].at(8, kEquator) = 0.9;
    god_of(bench.religion, "god_crown_law")->aspect = "wild";
    const double ley_weight = std::min(0.6, 0.9 * 0.45);
    const double faith = visitation_faith_factor(bench.religion, "human_heartland", "god_crown_law");

    int ley = 0;
    int divine = 0;
    for (std::uint32_t s = 1; s <= 300; ++s) {
        VisitationRequest request = bench.request("god_crown_law");
        request.seed = s;
        request.wrath = 1.0;
        const VisitationSummons summons = summon_god(request);
        const VisitationRuin* ruin = ruin_of(summons, "city-centre");
        if (ruin == nullptr) continue;
        const double distance = great_circle_m(kRadius, summons.centre, bench.site("city-centre").direction);
        const double divine_weight =
            kDivineWeight * request.wrath * (1.0 - distance / summons.reach_m) * kWildAspectFactor * faith;
        const double selector = (ruin->roll / ruin->probability) * (ley_weight + divine_weight) - ley_weight;
        if (std::abs(selector) < 1e-9) continue;  // too near the boundary to call
        if (selector <= 0.0) {
            check::equal(ruin->cause, std::string("fire"), "a draw in the first segment is the ley's");
            ++ley;
        } else {
            check::equal(ruin->cause, std::string("divine_god_crown_law"), "a draw past it is the god's");
            ++divine;
        }
    }
    check::is_true(ley > 0, "some falls on the ley's share");
    check::is_true(divine > 0, "and some on the god's");
}

TEST(the_gods_share_is_its_weight_times_wrath_aspect_and_faith) {
    // A city with no ley ground and no nests has one cause, the god, so the chance it fell at
    // IS the god's share -- read off the ruin's recorded probability and taken back to distance
    // zero. The share is 0.6 x wrath x aspect x faith, capped with every other cause at 0.9.
    const auto share = [](const std::string& aspect, const std::string& faith, double wrath) {
        Bench bench;
        bench.city("city-centre", 8, kEquator);
        bench.resolve();
        god_of(bench.religion, "god_red_field")->aspect = aspect;
        Faith* humans = faith_of(bench.religion, "human_heartland");
        std::erase(humans->gods, std::string("god_red_field"));
        // The Hearth and Harvest names the Red Field its rival; the First Hearth names nobody.
        humans->patron = faith == "rival" ? "god_hearth_harvest" : "god_first_hearth";
        if (faith == "own") humans->gods.push_back("god_red_field");
        for (std::uint32_t s = 1; s <= 2000; ++s) {
            VisitationRequest request = bench.request("god_red_field");
            request.seed = s;
            request.wrath = wrath;
            const VisitationSummons summons = summon_god(request);
            const VisitationRuin* ruin = ruin_of(summons, "city-centre");
            if (ruin == nullptr) continue;
            const double distance = great_circle_m(kRadius, summons.centre, bench.site("city-centre").direction);
            return ruin->probability / (1.0 - distance / summons.reach_m);
        }
        check::fail("the centre to fall to a " + aspect + " god and a " + faith + " people on some seed");
    };

    // The authored numbers: a base share of 0.6, Sovereign halving it.
    const double sovereign = share("sovereign", "neutral", 0.5);
    check::close(sovereign, 0.6 * 0.5 * 0.5, 1e-9, "a Sovereign god at half wrath");
    check::close(share("wild", "neutral", 0.5) / sovereign, 4.0, 1e-9,
                 "a Wild god destroys four times as much as a Sovereign one");
    check::close(share("", "neutral", 0.5) / sovereign, 2.0, 1e-9,
                 "a god with no aspect strikes at the plain weight, between the two");
    check::close(share("sovereign", "rival", 0.5) / sovereign, 2.0, 1e-9, "a rival's flock twice as hard");
    check::close(share("sovereign", "own", 1.0) / share("sovereign", "neutral", 1.0), 0.25, 1e-9,
                 "its own faithful at a quarter");
    // Wild, a rival's flock and full wrath is 2.4: the cap holds it at 0.9, so a city can never
    // be certain to fall.
    check::close(share("wild", "rival", 1.0), 0.9, 1e-6, "capped at 0.9");
}

TEST(each_cause_names_itself_and_asks_for_its_own_legacy) {
    // --- the ley: its reason is the table's, keyed by the school that owns the ground.
    {
        Bench bench = populated();
        bench.layers["ley_water"].at(8, kEquator) = 0.8;
        const VisitationRuin* found = nullptr;
        VisitationSummons summons;
        for (std::uint32_t s = 1; s <= 100 && found == nullptr; ++s) {
            VisitationRequest request = bench.request("god_crown_law");
            request.seed = s;
            request.wrath = 0.0;  // the god's own weight is zero: only the ground can take it
            bench.probe->causes.clear();
            summons = summon_god(request);
            found = ruin_of(summons, "city-centre");
        }
        check::is_true(found != nullptr, "the water ground to take the centre on some seed");
        check::equal(found->cause, std::string("water"), "the cause is the school");
        check::equal(found->reason, ley_reasons()["water"], "and the reason the table's");
        check::is_true(found->evidence.kind == VisitationCauseKind::Ley, "ley evidence");
        check::close(found->evidence.potency, 0.8, 0.0, "the potency that owned it");
        check::close(found->probability, 0.8 * 0.45, 1e-12, "the ley's pressure alone");
        check::equal(bench.probe->causes.back(), std::string("water"), "the legacy was asked about water");
        check::equal(bench.probe->potencies.back().size(), magic_school_table().size(),
                     "with all twelve schools, hidden ones included");
        check::equal(found->new_node_school, std::string("water"), "and its answer is where the key point goes");
        const LeyNode* key = node_of(summons.magic.networks.at("water"), "ruin-city-centre-key");
        check::is_true(key != nullptr, "the key point is planted");
        check::close(key->intensity, 1.5, 0.0, "at the legacy intensity");
    }
    // --- a nest: dragon or monster by name, and its family goes to the legacy.
    {
        Bench bench = populated();
        // Both lairs reach both cities (the reach is scaled to the world, 2026-09-23), and only
        // the strongest is drawn: the one on a city's own cell outweighs its neighbour's.
        bench.nest("nest-barrow", "Barrow Host", "undead", bench.at(9, kEquator));
        bench.nest("nest-ash", "Ash Dragon", "draconic", bench.at(10, kEquator));
        bool monster = false;
        bool dragon = false;
        for (std::uint32_t s = 1; s <= 200 && (!monster || !dragon); ++s) {
            VisitationRequest request = bench.request("god_crown_law");
            request.seed = s;
            request.wrath = 0.0;
            const VisitationSummons summons = summon_god(request);
            if (const VisitationRuin* r = ruin_of(summons, "city-east-1"); r != nullptr && !monster) {
                check::equal(r->cause, std::string("monster"), "an undead host is a monster");
                check::equal(r->reason, std::string("Barrow Host drove the inhabitants away."), "its reason");
                check::is_true(r->evidence.kind == VisitationCauseKind::Nest, "nest evidence");
                check::equal(r->evidence.nest_id, std::string("nest-barrow"), "the nest it names");
                check::equal(r->evidence.family, std::string("undead"), "and its family");
                monster = true;
            }
            if (const VisitationRuin* r = ruin_of(summons, "city-east-2"); r != nullptr && !dragon) {
                check::equal(r->cause, std::string("dragon"), "a nest named for a dragon is a dragon");
                check::equal(r->reason, std::string("Ash Dragon drove the inhabitants away."), "its reason");
                dragon = true;
            }
        }
        check::is_true(monster && dragon, "both nests to take their city on some seed");
        const auto asked = std::find(bench.probe->families.begin(), bench.probe->families.end(), "undead");
        check::is_true(asked != bench.probe->families.end(), "the nest's family to reach the legacy");
    }
    // --- the god: its own reason, its first school, and the full divine evidence.
    {
        Bench bench = populated();
        god_of(bench.religion, "god_red_field")->aspect = "wild";
        const VisitationRuin* found = nullptr;
        VisitationSummons summons;
        for (std::uint32_t s = 1; s <= 100 && found == nullptr; ++s) {
            VisitationRequest request = bench.request("god_red_field");
            request.seed = s;
            request.wrath = 1.0;
            summons = summon_god(request);
            found = ruin_of(summons, "city-centre");
        }
        check::is_true(found != nullptr, "the god to take the centre on some seed");
        const GodRecord* god = god_of(bench.religion, "god_red_field");
        const std::string name = god->aspect_name.empty() ? god->name : god->aspect_name;
        check::equal(found->cause, std::string("divine_god_red_field"), "the god is the cause");
        check::equal(found->reason, name + ": " + std::string(pantheon_god("god_red_field")->smite) + ".",
                     "in its own words");
        check::equal(found->evidence.god_id, std::string("god_red_field"), "the evidence names it");
        check::equal(found->evidence.aspect, std::string("wild"), "and the face it wore");
        check::close(found->evidence.reach_m, kVisitationReachSpacings * kSpacing, 1e-9, "and its reach");
        check::equal(bench.probe->god_schools.back(), std::string("infernal"), "its FIRST school was offered");
        check::equal(found->new_node_school, std::string("infernal"), "and the key point went there");
        check::equal(found->visitation, 0, "the ruin names the visitation");
    }
}

TEST(a_god_spares_its_faithful_and_punishes_a_rivals_flock) {
    Bench bench = populated();
    Religion religion = bench.religion;
    Faith devout;
    devout.civilization = "people_devout";
    devout.patron = "god_hearth_harvest";  // a rival of the Red Field -- and still spared
    devout.gods = {"god_hearth_harvest", "god_red_field"};
    Faith rival;
    rival.civilization = "people_rival";
    rival.patron = "god_hearth_harvest";
    rival.gods = {"god_hearth_harvest"};
    Faith neutral;
    neutral.civilization = "people_neutral";
    neutral.patron = "god_forge_craft";
    neutral.gods = {"god_forge_craft"};
    Faith elves;
    elves.civilization = "people_elves";
    elves.patron = "god_elder_star";  // names the Infernal a rival; the Infernal does not name it
    elves.gods = {"god_elder_star"};
    religion.faiths = {devout, rival, neutral, elves};

    check::close(visitation_faith_factor(religion, "people_devout", "god_red_field"), kOwnFaithfulFactor, 0.0,
                 "its own faithful, rivalry notwithstanding");
    check::close(visitation_faith_factor(religion, "people_rival", "god_red_field"), kRivalFlockFactor, 0.0,
                 "a rival's flock");
    check::close(visitation_faith_factor(religion, "people_neutral", "god_red_field"), 1.0, 0.0, "anyone else");
    check::close(visitation_faith_factor(religion, "people_nobody", "god_red_field"), 1.0, 0.0, "a people with no faith");
    // A rivalry counts from either side.
    check::close(visitation_faith_factor(religion, "people_elves", "god_infernal"), kRivalFlockFactor, 0.0,
                 "the patron names the god");
    Faith infernal;
    infernal.civilization = "people_infernal";
    infernal.patron = "god_infernal";
    infernal.gods = {"god_infernal"};
    religion.faiths = {infernal};
    check::close(visitation_faith_factor(religion, "people_infernal", "god_elder_star"), kRivalFlockFactor, 0.0,
                 "the god names the patron");
}

TEST(the_gods_own_faithful_that_stand_are_favoured_and_their_threat_relieved) {
    Bench bench = populated();
    faith_of(bench.religion, "human_heartland")->gods.push_back("god_red_field");
    forsake(bench.religion, "dwarf", "god_red_field");
    VisitationSummons summons;
    bool centre_stood = false;
    for (std::uint32_t s = 1; s <= 50 && !centre_stood; ++s) {
        VisitationRequest request = bench.request("god_red_field");
        request.seed = s;
        request.wrath = 1.0;
        summons = summon_god(request);
        centre_stood = ruin_of(summons, "city-centre") == nullptr && ruin_of(summons, "city-east-1") == nullptr;
    }
    check::is_true(centre_stood, "the centre and its neighbour to stand on some seed");

    const auto favour_of = [&summons](const std::string& uid) -> const VisitationFavor* {
        for (const VisitationFavor& f : summons.favored) {
            if (f.city_uid == uid) return &f;
        }
        return nullptr;
    };
    const VisitationFavor* centre = favour_of("city-centre");
    const VisitationFavor* east = favour_of("city-east-1");
    check::is_true(centre != nullptr && east != nullptr, "the god's own faithful that stood are favoured");
    check::is_true(favour_of("city-west-1") == nullptr, "a people that does not worship it is not");
    check::is_true(favour_of("city-east-4") == nullptr, "nor is a city out of reach");
    const double east_distance = great_circle_m(kRadius, summons.centre, bench.site("city-east-1").direction);
    check::close(east->strength, round_dp(1.0 - east_distance / summons.reach_m, 6), 0.0, "falling off with distance");

    VisitationRebuilt rebuilt;
    rebuilt.cities = summons.survivors;
    rebuilt.reports = {{"city-centre", 0.5}, {"city-east-1", 0.1}, {"city-west-1", 0.4}, {"city-gone", 0.9}};
    const VisitationOutcome outcome = complete_summons(summons, rebuilt);
    check::equal(outcome.relief.size(), std::size_t{2}, "relief for the two favoured reports only");
    check::equal(outcome.relief[0].city_uid, std::string("city-centre"), "the centre");
    check::close(outcome.relief[0].divine_favor, centre->strength, 0.0, "favoured by its strength");
    check::close(outcome.relief[0].regional_threat, round_dp(0.5 - kFavorRelief * centre->strength, 6), 0.0,
                 "its threat relieved");
    check::close(outcome.relief[1].regional_threat, 0.0, 0.0, "a threat is relieved to zero, never below it");
    check::equal(outcome.record.favored.size(), summons.favored.size(), "and the record keeps the favour");
}

TEST(rival_nests_inside_the_reach_are_purged_after_the_rebuild) {
    // The Crown and Law drives out infernal things. The purge runs on the REBUILT nests: the
    // request had none at all, so anything purged here came from the rebuild's.
    Bench bench = populated();
    VisitationRequest request = bench.request("god_crown_law");
    request.wrath = 0.0;
    const VisitationSummons summons = summon_god(request);

    Bench rebuilt_world = populated();
    rebuilt_world.nest("nest-a", "Pit Fiend", "infernal", bench.at(9, kEquator));
    rebuilt_world.nest("nest-b", "Hellhound", "infernal", bench.at(9, kEquator), true);  // a real animal
    rebuilt_world.nest("nest-c", "Far Fiend", "infernal", bench.at(20, kEquator));       // out of reach
    rebuilt_world.nest("nest-d", "Barrow Host", "undead", bench.at(10, kEquator));       // not its enemy
    rebuilt_world.nest("nest-e", "Edge Fiend", "infernal", bench.at(11, kEquator));      // 18.75 km: inside
    VisitationRebuilt rebuilt;
    rebuilt.cities = summons.survivors;
    rebuilt.nests = rebuilt_world.nests;
    const VisitationOutcome outcome = complete_summons(summons, rebuilt);

    check::equal(outcome.record.purged_nests.size(), std::size_t{2}, "two rival nests purged");
    check::equal(outcome.record.purged_nests[0], std::string("nest-a"), "the one beside the city");
    check::equal(outcome.record.purged_nests[1], std::string("nest-e"), "and the one at the edge");
    check::equal(outcome.nests.size(), std::size_t{3}, "three kept");
    check::equal(outcome.nests[0].id, std::string("nest-b"), "a real animal is never purged");
    check::equal(outcome.nests[1].id, std::string("nest-c"), "nor a rival out of reach");
    check::equal(outcome.nests[2].id, std::string("nest-d"), "nor a family this god does not hunt");
}

TEST(the_peoples_that_stand_within_reach_convert_and_the_god_walks) {
    Bench bench = populated();
    faith_of(bench.religion, "dwarf")->patron = "god_forge_craft";
    faith_of(bench.religion, "human_heartland")->patron = "god_first_hearth";
    VisitationRequest request = bench.request("god_red_field");
    request.wrath = 0.0;  // nobody falls, so every city in reach is a candidate
    const VisitationSummons summons = summon_god(request);
    check::equal(summons.new_ruins.size(), std::size_t{0}, "nothing to fall at zero wrath");

    VisitationRebuilt rebuilt;
    rebuilt.cities = summons.survivors;
    Bench extra;
    extra.city("city-founded", 9, kEquator - 1, "gnome_tinker");  // a people with no faith here
    rebuilt.cities.push_back(extra.cities[0]);
    const VisitationOutcome outcome = complete_summons(summons, rebuilt);

    // FIRST-SEEN over the rebuilt sites: the centre is human and comes first, although "dwarf"
    // sorts before it.
    check::equal(outcome.record.converted.size(), std::size_t{2}, "two peoples convert");
    check::equal(outcome.record.converted[0], std::string("human_heartland"), "the first seen");
    check::equal(outcome.record.converted[1], std::string("dwarf"), "then the next");
    const Faith* human = nullptr;
    for (const Faith& f : outcome.religion.faiths) {
        if (f.civilization == "human_heartland") human = &f;
    }
    check::is_true(human != nullptr && human->conversion.has_value(), "the conversion is written onto the faith");
    check::equal(human->conversion->god_id, std::string("god_red_field"), "naming the god");
    check::equal(human->conversion->visitation, 0, "and the visitation");

    const GodRecord* red = nullptr;
    for (const GodRecord& g : outcome.religion.gods) {
        if (g.id == "god_red_field") red = &g;
    }
    check::equal(red->status, std::string("walking"), "the god walks");
    const std::string centre_node = std::to_string(bench.site("city-centre").node);
    std::string cluster;
    for (const std::string& id : summons.cluster) cluster += (cluster.empty() ? "\"" : ",\"") + id + "\"";
    check::equal(*red->avatar,
                 "{\"node\":" + centre_node + ",\"since_age\":2,\"day\":" + std::to_string(summons.day) +
                     ",\"visitation\":0,\"cluster\":[" + cluster + "]}",
                 "with its avatar");
    check::equal(summons.day, 3LL * kDaysPerYear, "arriving on the first day of the year founding ended");
    check::equal(outcome.record.moon.day, summons.day, "under that day's sky");

    bool theophany = false;
    for (const ReligionSite& site : outcome.religion.sites) {
        if (site.id == "theophany-0-god_red_field") theophany = site.kind == "theophany" && site.visitation == 0;
    }
    check::is_true(theophany, "a theophany where it stood");
    check::is_true(std::is_sorted(outcome.religion.sites.begin(), outcome.religion.sites.end(),
                                  [](const ReligionSite& a, const ReligionSite& b) { return a.id < b.id; }),
                   "the sites still sorted by id");
    check::equal(outcome.visitations.size(), std::size_t{1}, "the record appended");
    check::is_true(!outcome.record.departed_age.has_value(), "open");
    check::equal(outcome.operation.kind, std::string("visitation"), "the operation");
    check::equal(*outcome.operation.node, bench.site("city-centre").node, "naming where");

    // The caller's resolve carries all of it forward.
    bench.cities = summons.survivors;
    const Religion resolved = resolve_religion(bench.religion_request(&outcome.religion));
    check::equal(status_of(resolved, "god_red_field"), std::string("walking"), "still walking after the resolve");
    for (const GodRecord& g : resolved.gods) {
        if (g.id == "god_red_field") check::equal(*g.avatar, *red->avatar, "with the same avatar");
    }
    for (const Faith& f : resolved.faiths) {
        if (f.civilization == "human_heartland") {
            check::equal(f.patron, std::string("god_red_field"), "a converted people takes the god as patron");
        }
    }
}

TEST(a_people_already_sworn_to_the_god_and_the_fallen_do_not_convert) {
    Bench bench = populated();
    faith_of(bench.religion, "dwarf")->patron = "god_red_field";
    faith_of(bench.religion, "human_heartland")->patron = "god_first_hearth";
    VisitationRequest request = bench.request("god_red_field");
    request.wrath = 0.0;
    const VisitationSummons summons = summon_god(request);
    VisitationRebuilt rebuilt;
    rebuilt.cities = summons.survivors;
    const VisitationOutcome outcome = complete_summons(summons, rebuilt);
    check::equal(outcome.record.converted.size(), std::size_t{1}, "only the humans convert");
    check::equal(outcome.record.converted[0], std::string("human_heartland"), "the dwarves already were");

    // A ruined city never converts, even if a rebuild were to hand it back. city-west-1 is the
    // only dwarven city in reach, so if it falls the dwarves have nobody left there to convert.
    Bench wrathful = populated();
    faith_of(wrathful.religion, "dwarf")->patron = "god_forge_craft";
    god_of(wrathful.religion, "god_red_field")->aspect = "wild";
    VisitationSummons fell;
    bool found = false;
    for (std::uint32_t s = 1; s <= 100 && !found; ++s) {
        VisitationRequest r = wrathful.request("god_red_field");
        r.seed = s;
        r.wrath = 1.0;
        fell = summon_god(r);
        found = ruin_of(fell, "city-west-1") != nullptr;
    }
    check::is_true(found, "a seed on which the dwarven city falls");
    VisitationRebuilt handed_back;
    handed_back.cities = wrathful.cities;  // every city, the fallen included
    const VisitationOutcome ruined = complete_summons(fell, handed_back);
    check::is_true(!contains_text(ruined.record.converted, "dwarf"), "the fallen dwarven city converts nobody");
    // The control: the same summons with no ruins recorded converts them, so it is the ruin
    // and nothing else that kept them out.
    VisitationSummons unruined = fell;
    unruined.new_ruins.clear();
    check::is_true(contains_text(complete_summons(unruined, handed_back).record.converted, "dwarf"),
                   "a standing dwarven city in reach converts its people");
}

// --- departure -------------------------------------------------------------

TEST(a_departure_collapses_the_cluster_to_a_footprint_and_remaps_the_lines) {
    Bench bench = populated();
    VisitationRequest request = bench.request("god_red_field");
    request.wrath = 0.0;
    const VisitationSummons summons = summon_god(request);
    VisitationRebuilt rebuilt;
    rebuilt.cities = summons.survivors;
    const VisitationOutcome arrived = complete_summons(summons, rebuilt);

    // Key points that landed AFTER the cluster, with lines -- as a ruin's key point and a
    // villain's well can. Lines index their endpoints by position, so the cluster's removal
    // renumbers them.
    MagicReport magic = summons.magic;
    LeyNetwork& infernal = magic.networks["infernal"];
    const int first_late = static_cast<int>(infernal.nodes.size());
    infernal.nodes.push_back({"late-a", bench.at(9, kEquator), 1.0});
    infernal.nodes.push_back({"late-b", bench.at(10, kEquator), 1.0});
    infernal.edges.push_back({"line-to-pole", first_late, 0, 1.0});
    infernal.edges.push_back({"line-between", first_late, first_late + 1, 1.0});
    infernal.edges.push_back({"line-to-avatar", first_late, 1, 1.0});  // node 1 is the avatar centre

    const VisitationDeparture gone = depart_god({"god_red_field", 5, magic, arrived.religion, arrived.visitations});
    const LeyNetwork& after = gone.magic.networks.at("infernal");
    check::equal(after.nodes.size(), std::size_t{4}, "the pole, the two late points and the footprint");
    check::equal(after.edges.size(), std::size_t{2}, "a line to a collapsed node goes with it");
    for (const LeyEdge& edge : after.edges) {
        check::is_true(edge.from >= 0 && edge.to >= 0 && edge.from < 4 && edge.to < 4, edge.id + " inside the list");
    }
    const LeyEdge* to_pole = edge_of(after, "line-to-pole");
    const LeyEdge* between = edge_of(after, "line-between");
    check::equal(after.nodes[static_cast<std::size_t>(to_pole->from)].id, std::string("late-a"), "still from late-a");
    check::equal(after.nodes[static_cast<std::size_t>(to_pole->to)].id, std::string("infernal-node-0"), "to the pole");
    check::equal(after.nodes[static_cast<std::size_t>(between->to)].id, std::string("late-b"), "and on to late-b");

    const LeyNode* footprint = node_of(after, "footprint-god_red_field-0");
    check::is_true(footprint != nullptr, "a footprint in the god's first school");
    check::close(footprint->intensity, kFootprintIntensity, 0.0, "at its own intensity");
    check::close(dot(footprint->direction, summons.centre), 1.0, 1e-12, "where the god stood");
    check::is_true(node_of(gone.magic.networks.at("fire"), "avatar-god_red_field-0-fire-0") == nullptr,
                   "the cluster gone from its other school too");

    check::equal(*gone.record.departed_age, 5, "the record closed in the age given");
    check::equal(status_of(gone.religion, "god_red_field"), std::string("manifest"), "the god home");
    bool pilgrimage = false;
    for (const ReligionSite& site : gone.religion.sites) {
        if (site.id == "theophany-0-god_red_field") pilgrimage = site.kind == "pilgrimage";
    }
    check::is_true(pilgrimage, "the theophany now a pilgrimage");
    check::equal(gone.operation.kind, std::string("departure"), "a departure operation");
    check::is_true(!gone.operation.node.has_value(), "which names no place");
    check::is_true(gone.rebuild_required, "and the ley field moved");
}

// --- refusals --------------------------------------------------------------

TEST(a_summons_is_refused_rather_than_guessed) {
    Bench bench = populated();
    // Each refusal must come from the check it is about, named by field and, where one field
    // has several refusals, by its wording.
    const auto refused = [&bench](const std::string& field, const std::string& wording,
                                  const std::function<void(VisitationRequest&)>& edit, const std::string& what) {
        VisitationRequest request = bench.request("god_red_field");
        edit(request);
        const VisitationError error = refusal([&] { summon_god(request); }, what);
        check::equal(error.field(), field, what);
        check::is_true(says(error, wording), what + " refused as \"" + wording + "\", not as: " + error.what());
    };
    // The floor: the unedited request is accepted, so each refusal below is the edit's doing.
    summon_god(bench.request("god_red_field"));

    refused("api_version", "version 1", [](VisitationRequest& r) { r.api_version = 2; }, "another API version");
    refused("world", "node graph", [](VisitationRequest& r) { r.grid = nullptr; }, "no node graph");
    refused("religion", "version 1", [](VisitationRequest& r) { r.religion.version = 0; }, "no religion contract");
    refused("religion.catalogue", "different pantheon",
            [](VisitationRequest& r) { r.religion.catalogue.sha256 = "another"; }, "another pantheon");
    refused("astrology", "no seeded moon", [](VisitationRequest& r) { r.moon = Moon{}; }, "an unseeded moon");
    refused("world", "positive metres", [](VisitationRequest& r) { r.settlement_spacing_m = 0.0; },
            "no spacing, so no reach");
    refused("god_id", "is no god of this world", [](VisitationRequest& r) { r.god_id = "god_nobody"; },
            "a god nobody wrote");
    refused("god_id", "only by corruption", [](VisitationRequest& r) { r.god_id = "god_long_thirst"; }, "a hidden god");
    refused("god_id", "already walks",
            [](VisitationRequest& r) { god_of(r.religion, "god_red_field")->status = "walking"; },
            "a god already walking");
    check::equal(status_of(bench.religion, "god_sky_court"), std::string("absent"), "a world without sky cities");
    refused("god_id", "an absent god", [](VisitationRequest& r) { r.god_id = "god_sky_court"; }, "an absent god");
    // The limit counts EVERY record, the departed included, so a world can be out of summons
    // for good. Seven gone home leave room for an eighth; eight gone home leave none.
    const auto departed = [](VisitationRequest& r, int count) {
        r.visitations.resize(static_cast<std::size_t>(count));
        for (VisitationRecord& v : r.visitations) v.departed_age = 1;
    };
    VisitationRequest seventh = bench.request("god_red_field");
    departed(seventh, kMaxVisitations - 1);
    check::equal(summon_god(seventh).index, kMaxVisitations - 1, "the eighth summons, after seven have gone home");
    refused("visitations", "limit is 8", [&departed](VisitationRequest& r) { departed(r, kMaxVisitations); },
            "a world whose eight visitors have all gone home");
    // A god that withdrew can still be called back.
    VisitationRequest sleeping = bench.request("god_red_field");
    god_of(sleeping.religion, "god_red_field")->status = "sleeping";
    check::equal(summon_god(sleeping).god_id, std::string("god_red_field"), "a sleeping god summoned");
    refused("target", "exactly one place", [](VisitationRequest& r) { r.target_node = 3; }, "two targets");
    refused("target", "exactly one place", [](VisitationRequest& r) { r.target_city_uid.reset(); }, "no target");
    refused("target.city_uid", "is no city", [](VisitationRequest& r) { r.target_city_uid = "city-nowhere"; },
            "an unknown city");
    refused("target.node", "outside the grid",
            [](VisitationRequest& r) {
                r.target_city_uid.reset();
                r.target_node = -1;
            },
            "a node below the grid");
    refused("target.node", "outside the grid",
            [](VisitationRequest& r) {
                r.target_city_uid.reset();
                r.target_node = static_cast<int>(r.grid->node_count());
            },
            "a node past the grid");
    refused("wrath", "0..1", [](VisitationRequest& r) { r.wrath = -0.1; }, "wrath below zero");
    refused("wrath", "0..1", [](VisitationRequest& r) { r.wrath = 1.1; }, "wrath above one");
    refused("wrath", "0..1", [](VisitationRequest& r) { r.wrath = std::nan(""); }, "wrath that is not a number");
    refused("variation", "uint32", [](VisitationRequest& r) { r.variation = -1; }, "a negative variation");
    refused("variation", "uint32", [](VisitationRequest& r) { r.variation = 4294967296LL; }, "a variation past a uint32");
    refused("day", ">= 0", [](VisitationRequest& r) { r.day = -1; }, "a day before the epoch");
    refused("ley_destruction", "no ley destruction reason for air",
            [](VisitationRequest& r) { r.ley_destruction.erase("air"); }, "a reason table short of a school");
    refused("ruin_legacy", "legacy resolution", [](VisitationRequest& r) { r.ruin_legacy = nullptr; },
            "no legacy resolution");

    // A god walks on land.
    Bench wet = populated();
    wet.layers["water_type"].at(8, kEquator) = 1.0;
    const VisitationError water = refusal([&] { summon_god(wet.request("god_red_field")); }, "a target on water");
    check::is_true(says(water, "walks on land"), "refused because it is water");
}

TEST(a_hidden_school_owning_the_ground_is_refused_by_name) {
    // Reachable on a corrupted world: a corrupted seat node sits on its host's own seat city at
    // full intensity. The reference raises KeyError on the lookup. No sentence exists for it and
    // none is invented; a ruling, if one is made, is an entry in the table.
    Bench bench = populated();
    bench.layers["ley_blood"].at(9, kEquator) = 0.9;
    const VisitationError error =
        refusal([&] { summon_god(bench.request("god_red_field")); }, "a god smiting ground the Long Thirst owns");
    check::equal(error.field(), std::string("ley_destruction"), "refused for want of a sentence");
    check::is_true(says(error, "city-east-1") && says(error, "blood"), "naming the city and the school");

    Bench outside = populated();
    outside.layers["ley_blood"].at(12, kEquator) = 0.9;  // city-east-4, just out of reach
    summon_god(outside.request("god_red_field"));  // the ground out of reach is never read

    VisitationRequest ruled = bench.request("god_red_field");
    ruled.ley_destruction["blood"] = "The Long Thirst drank the ground dry.";
    summon_god(ruled);  // with a ruling in the table, it proceeds
}

// --- scale -----------------------------------------------------------------

TEST(the_reach_is_a_multiple_of_settlement_spacing_and_fires_at_200_km) {
    // The same world at the 11.15 km reference width and at the 200 km preset. Settlement
    // spacing is 450 m * reach_scale(circumference), so it grows with the planet and every
    // reach here is a multiple of it: the two widths must reach the same cities and plant the
    // same shape.
    const double reference_radius = 11148.961626795008 / (2.0 * 3.14159265358979323846);
    std::vector<std::vector<std::string>> favoured;
    std::vector<double> ring_angle;
    for (const auto& [radius, spacing] : {std::pair{reference_radius, 450.0}, std::pair{kRadius, kSpacing}}) {
        Bench bench = populated(radius, spacing);
        faith_of(bench.religion, "human_heartland")->gods.push_back("god_red_field");
        forsake(bench.religion, "dwarf", "god_red_field");
        VisitationRequest request = bench.request("god_red_field");
        request.wrath = 0.0;  // nobody falls, so every human city in reach is favoured
        const VisitationSummons summons = summon_god(request);
        check::close(summons.reach_m, kVisitationReachSpacings * spacing, 1e-9, "three spacings");
        std::vector<std::string> uids;
        for (const VisitationFavor& f : summons.favored) uids.push_back(f.city_uid);
        favoured.push_back(uids);
        const LeyNetwork& infernal = summons.magic.networks.at("infernal");
        ring_angle.push_back(great_circle_m(1.0, node_of(infernal, "avatar-god_red_field-0-infernal-0")->direction,
                                            node_of(infernal, "avatar-god_red_field-0-infernal-1")->direction));
    }
    check::equal(favoured[0].size(), favoured[1].size(), "the same number of cities in reach at both widths");
    for (std::size_t i = 0; i < favoured[0].size(); ++i) {
        check::equal(favoured[0][i], favoured[1][i], "the same city " + std::to_string(i));
    }
    check::close(ring_angle[0], ring_angle[1], 1e-12, "the same ring, as an angle");
    // At 200 km the reach is 24 217.5 m and takes a city three cells and 18.75 km away.
    check::equal(favoured[1].size(), std::size_t{4}, "the centre and three cities east of it");
    check::equal(favoured[1].back(), std::string("city-east-3"), "the furthest 18.75 km out");
}
