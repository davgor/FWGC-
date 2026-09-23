#include "fwg/corruption.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <utility>

#include "fwg/random.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr long long kUint32Bound = 4294967296LL;

const std::string_view kOrder[] = {"gate",   "god",
                                   "host",   "corrupted cluster",
                                   "leyline update",  "biomes",
                                   "civilization",    "nests",
                                   "threat assessment", "failure mode",
                                   "pantheon reaction"};

bool lives(std::string_view status) { return status == "manifest" || status == "walking"; }

double class_intensity(CityClass city_class) {
    switch (city_class) {
        case CityClass::Capital:
            return kClassIntensityCapital;
        case CityClass::Medium:
            return kClassIntensityMedium;
        case CityClass::Small:
            break;
    }
    return kClassIntensitySmall;
}

// The network-edit contract is plant_key_point's (magic.hpp); this only makes its refusal
// a corruption refusal.
void add_ley_node(LeyNetwork& net, const LeyNode& node) {
    try {
        plant_key_point(net, node);
    } catch (const std::invalid_argument& refused) {
        throw CorruptionError("magic.networks", refused.what());
    }
}

LeyNetwork& network_for(MagicReport& magic, const std::string& school) {
    const auto it = magic.networks.find(school);
    if (it == magic.networks.end()) {
        throw CorruptionError("magic.networks", "this world has no " + school + " network to corrupt");
    }
    return it->second;
}

// The corrupted nodes themselves, which are what actually persists.
//
// ONE draw from the stream, for the ring's bearing. The seat sits on the host and the
// three ring nodes are evenly spaced around it, so the whole cluster is a rotation of one
// fixed shape -- which is why moving this draw anywhere else moves every corrupted world.
std::vector<std::string> corrupt_cluster(MagicReport& magic, const HiddenGod& god,
                                         const Villain& host, Rng& rng, int index,
                                         double spacing_m, double radius_m) {
    const Vec3 centre = host.direction;
    const double angle = kCorruptionRingSpacings * spacing_m / radius_m;
    const double base = rng.uniform(0.0, 2.0 * kPi);

    std::vector<std::pair<Vec3, double>> placements;
    placements.emplace_back(centre, kCorruptionSeatIntensity);
    for (int k = 0; k < kCorruptionRingPoints; ++k) {
        const double bearing = base + k * 2.0 * kPi / kCorruptionRingPoints;
        placements.emplace_back(offset_direction(centre, angle, bearing), kCorruptionRingIntensity);
    }

    const std::string school(god.school);
    LeyNetwork& net = network_for(magic, school);
    std::vector<std::string> ids;
    for (std::size_t k = 0; k < placements.size(); ++k) {
        const std::string node_id = "corrupt-" + std::string(god.id) + "-" + std::to_string(index) +
                                    "-" + std::to_string(k);
        add_ley_node(net, {node_id, placements[k].first, placements[k].second});
        ids.push_back(node_id);
    }
    return ids;
}

struct FailureMode {
    std::vector<VillainCity> survivors;
    std::vector<CorruptionTouch> touched;
    // The cities that end, with the falloff share each was hit at.
    std::vector<std::pair<VillainCity, double>> ruined;
};

FailureMode failure_mode(const std::vector<VillainCity>& cities, const HiddenGod& god,
                         const Villain& host, double reach_m, double radius_m) {
    FailureMode out;
    const std::string_view school = god.school;
    for (const VillainCity& original : cities) {
        VillainCity city = original;
        const double distance = great_circle_m(radius_m, city.direction, host.direction);
        // A city exactly at the reach is INSIDE, and takes a share of exactly zero.
        if (distance > reach_m) {
            out.survivors.push_back(std::move(city));
            continue;
        }
        const double share = 1.0 - distance / reach_m;
        if (school == "blood") {
            // Farmed: the city stands and its people do not.
            const double before = city.population_estimate;
            city.population_estimate = round_dp(before * (1.0 - kCorruptionFarmShare * share), 6);
            out.touched.push_back(
                {city.uid, "farmed", round_dp(before - city.population_estimate, 6)});
            out.survivors.push_back(std::move(city));
        } else if (school == "eldritch") {
            // Taken, not destroyed. Nothing visibly changes, which is the horror.
            city.taken_by = std::string(god.id);
            out.touched.push_back({city.uid, "taken", 0.0});
            out.survivors.push_back(std::move(city));
        } else {
            // Rot and Void both END the city; what they leave behind differs.
            out.ruined.emplace_back(std::move(city), share);
        }
    }
    return out;
}

void validate_corruption(const CorruptionRequest& request) {
    if (request.api_version != kCorruptionApiVersion) {
        throw CorruptionError("api_version", "this build serves corruption API version 1");
    }
    if (request.religion.version != kReligionVersion || request.religion.gods.empty()) {
        throw CorruptionError("religion",
                              "this world lacks the religion contract at version 1. It is written "
                              "during generation, so a world exported before it has to be "
                              "regenerated.");
    }
    if (request.religion.catalogue.sha256 != pantheon_identity().sha256) {
        throw CorruptionError("religion.catalogue",
                              "this world was built against a different pantheon catalogue, so the "
                              "gods it names may not exist here. Regenerate or explicitly migrate "
                              "it.");
    }
    if (request.encounters < 0 || request.encounters >= kUint32Bound) {
        throw CorruptionError("encounters", "encounters across playthroughs, 0 .. 4294967295");
    }
    if (request.variation < 0 || request.variation >= kUint32Bound) {
        throw CorruptionError("variation", "seed, 0 .. 4294967295");
    }
    // Standing only: a god reaches for someone who HOLDS ground, and `people` keeps the
    // fallen too. A world whose only villain has fallen has none to corrupt.
    const std::vector<Villain> people = standing_villains(request.villains.people);
    if (people.empty()) {
        throw CorruptionError("world.villains",
                              "corruption needs a standing super villain to seat and this world "
                              "has none. Villains rise from recorded turmoil, so advance the world "
                              "or raise villain_rise above zero.");
    }
    if (request.villain_uid.has_value()) {
        bool known = false;
        for (const Villain& villain : people) {
            if (villain.uid == *request.villain_uid) known = true;
        }
        if (!known) {
            throw CorruptionError("villain_uid",
                                  *request.villain_uid + " is not a standing villain of this world");
        }
    }
    long long held = 0;
    for (const CorruptionRecord& record : request.corruptions) {
        if (!record.cleansed_age.has_value()) held += static_cast<long long>(record.nodes.size());
    }
    if (held >= kCorruptionNodeBudget) {
        throw CorruptionError("religion.corruptions",
                              "this world already carries " + std::to_string(held) +
                                  " corrupted nodes, and the per-world budget is 96");
    }
}

}  // namespace

std::span<const std::string_view> corruption_order() { return {kOrder, std::size(kOrder)}; }

// ---------------------------------------------------------------------------
// Reading the world.
// ---------------------------------------------------------------------------

double hidden_interest(std::string_view kind, const CorruptionRequest& request) {
    if (kind == "living") {
        double people = 0.0;
        for (const VillainCity& city : request.cities) people += city.population_estimate;
        return people != 0.0 ? std::min(1.0, people / 200000.0) : 0.0;
    }
    if (kind == "decay") {
        const double ruins = request.existing_ruins;
        const double total = std::max(1.0, static_cast<double>(request.cities.size()) + ruins);
        return std::min(1.0, ruins / total);
    }
    if (kind == "aberrant") {
        double wrong = 0.0;
        for (const std::string& family : request.nest_families) {
            if (family == "aberrant" || family == "undead") wrong += 1.0;
        }
        return wrong != 0.0 ? std::min(1.0, wrong / 24.0) : 0.0;
    }
    if (kind == "dead_magic") {
        double live = 0.0;
        for (const auto& [name, net] : request.magic.networks) {
            const int index = magic_school_index(name);
            if (index < 0 || magic_school_table()[static_cast<std::size_t>(index)].hidden) continue;
            if (!net.nodes.empty()) live += 1.0;
        }
        return std::max(0.0, 1.0 - live / 8.0);
    }
    throw CorruptionError("interest", "unknown interest kind " + std::string(kind));
}

double hidden_chance(const HiddenGod& god, long long encounters) {
    if (encounters < god.noticing) return 0.0;
    // Degenerate authored data, guarded rather than divided by.
    if (god.climb <= god.noticing) return 1.0;
    const double span = static_cast<double>(god.climb - god.noticing);
    return std::max(0.0, std::min(1.0, static_cast<double>(encounters - god.noticing) / span));
}

std::vector<const HiddenGod*> watching_gods(const CorruptionRequest& request) {
    GodIdSet manifest;
    for (const GodRecord& god : request.religion.gods) {
        if (lives(god.status)) manifest.insert(god.id);
    }

    std::vector<std::pair<double, const HiddenGod*>> rows;
    // FILE ORDER, which is also the order the ranking below is stable against.
    for (const HiddenGod& god : hidden_pantheon_gods()) {
        if (hidden_chance(god, request.encounters) <= 0.0) continue;
        if (!rule_holds(god.exists, request.religion.facts, manifest)) continue;
        rows.emplace_back(hidden_interest(god.interest, request), &god);
    }
    // Most interested first, ties by id: a draw between two gods is decided by the world
    // and then by name, never by a roll.
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first > b.first;
        return a.second->id < b.second->id;
    });

    std::vector<const HiddenGod*> out;
    out.reserve(rows.size());
    for (const auto& row : rows) out.push_back(row.second);
    return out;
}

const Villain* corruption_host(const VillainBlock& villains,
                               const std::optional<std::string>& villain_uid) {
    std::vector<const Villain*> candidates;
    for (const Villain& villain : villains.people) {
        if (is_standing(villain)) candidates.push_back(&villain);
    }
    // An explicit uid searches the CANDIDATES rather than the filtered set, so it accepts a
    // villain a hidden god already holds: two records can point at one host and the second
    // god overwrites its school and its patron.
    if (villain_uid.has_value()) {
        for (const Villain* villain : candidates) {
            if (villain->uid == *villain_uid) return villain;
        }
        return nullptr;
    }
    const Villain* best = nullptr;
    for (const Villain* villain : candidates) {
        if (villain->corrupted) continue;
        // The key is (tier, uid), so the uid is part of the ordering rather than a
        // tie-break on an equal key: the result is the lexicographically GREATEST uid among
        // those at the highest tier.
        if (best == nullptr || villain->tier > best->tier ||
            (villain->tier == best->tier && villain->uid > best->uid)) {
            best = villain;
        }
    }
    return best;
}

std::vector<std::string> reveal_hidden_god(Religion& religion, std::string_view god_id,
                                           std::string_view host_uid, int age) {
    (void)host_uid;
    (void)age;
    const HiddenGod* hidden = hidden_pantheon_god(god_id);
    if (hidden == nullptr) throw CorruptionError("god_id", "no hidden god called " + std::string(god_id));
    std::set<std::string_view> rivals(hidden->rivals.begin(), hidden->rivals.end());

    std::vector<std::string> roused;
    for (GodRecord& record : religion.gods) {
        if (record.id == god_id) {
            record.status = "walking";
            record.aspect = "wild";
            record.aspect_name = std::string(hidden->aspects.wild);
            continue;
        }
        if (rivals.find(record.id) == rivals.end()) continue;
        if (record.status != "manifest" && record.status != "sleeping" && record.status != "walking") {
            continue;
        }
        const PantheonGod* known = pantheon_god(record.id);
        // Sixteen of the known gods have only one face and cannot be pushed anywhere. They
        // are still roused -- the pantheon still names the intruder a rival -- and that is
        // what the list records.
        if (known != nullptr && !known->aspects.wild.empty()) {
            record.aspect = "wild";
            record.aspect_name = std::string(known->aspects.wild);
        }
        roused.push_back(record.id);
    }
    return roused;
}

// ---------------------------------------------------------------------------
// The act.
// ---------------------------------------------------------------------------

CorruptionOutcome corrupt_world(const CorruptionRequest& request) {
    validate_corruption(request);

    CorruptionOutcome outcome;
    outcome.encounters = request.encounters;
    outcome.survivors = request.cities;
    outcome.magic = request.magic;
    outcome.religion = request.religion;
    outcome.villains = request.villains;
    outcome.corruptions = request.corruptions;

    const int index = static_cast<int>(request.corruptions.size());
    const std::vector<const HiddenGod*> candidates = watching_gods(request);
    for (const HiddenGod* god : candidates) outcome.watching.emplace_back(god->id);
    const Villain* host = corruption_host(request.villains, request.villain_uid);

    if (candidates.empty() || host == nullptr) {
        // The reference says "a watching god did not act this time" whenever anybody is
        // watching, including the case where nothing was rolled because every standing
        // villain is already taken. Reproduced, and it is misleading; see the report.
        outcome.reason = candidates.empty() ? "no hidden god is watching yet"
                                            : "a watching god did not act this time";
        return outcome;
    }

    const HiddenGod& god = *candidates.front();
    const std::string stream = "corruption-" + std::to_string(index) + "-" + std::string(god.id);
    // TWO DISTINCT DERIVATIONS, and the difference is the point: the escalation roll varies
    // on `encounters`, so retrying the same request with a higher ledger gives a different
    // roll AND a higher threshold, while the cluster's bearing varies on `variation`, so a
    // caller can reroll the shape without moving the gate.
    Rng escalation = Rng::child(request.seed, stream + "-escalation", request.encounters);
    const double roll = escalation.random();
    if (!(roll < hidden_chance(god, request.encounters))) {
        outcome.reason = "a watching god did not act this time";
        return outcome;
    }

    Rng rng = Rng::child(request.seed, stream, request.variation);
    const std::vector<std::string> nodes =
        corrupt_cluster(outcome.magic, god, *host, rng, index, request.settlement_spacing_m,
                        request.globe_radius_m);

    // A corrupted villain reaches as far as it already did, with a floor under it. The
    // floor matters because a villain whose own seat city has since fallen still sits where
    // its node is, and the nearest survivor can be further away than a fixed constant
    // would ever carry.
    const double reach =
        std::max(host->reach_m, kCorruptionReachSpacings * request.settlement_spacing_m);
    FailureMode failed = failure_mode(request.cities, god, *host, reach, request.globe_radius_m);
    outcome.survivors = std::move(failed.survivors);

    CorruptionRecord record;
    record.index = index;
    record.god_id = std::string(god.id);
    record.school = std::string(god.school);
    record.villain_uid = host->uid;
    record.age = request.age;
    record.encounters = request.encounters;
    record.variation = request.variation;
    record.nodes = nodes;
    record.reach_m = reach;
    record.touched = std::move(failed.touched);

    const std::string cause = god.school == "rot" ? "rot_plague" : "void_unmade";
    for (const auto& [city, share] : failed.ruined) {
        CorruptionRuin ruin;
        ruin.id = "ruin-" + city.uid;
        ruin.city_uid = city.uid;
        ruin.node = city.node;
        ruin.direction = city.direction;
        ruin.population_profile = city.population_profile;
        ruin.city_class = city.city_class;
        ruin.destroyed_age = request.age;
        ruin.cause = cause;
        ruin.reason = std::string(god.name) + ": " + std::string(god.smite) + ".";
        ruin.god_id = std::string(god.id);
        ruin.school = std::string(god.school);
        ruin.share = round_dp(share, 6);
        ruin.corruption = index;
        ruin.new_node_school = std::string(god.school);
        // The god IS the source, so only the class intensity is resolved from the city --
        // the school and the basis are stated. The reference resolves all three and
        // overwrites two, which left the ruin claiming the region's dominant magic had
        // chosen a hidden school, and a region can never hold one.
        ruin.legacy.school = std::string(god.school);
        ruin.legacy.basis = "source";
        ruin.legacy.intensity = class_intensity(city.city_class);
        record.ruins.push_back(ruin.id);
        outcome.new_ruins.push_back(std::move(ruin));
    }

    for (Villain& person : outcome.villains.people) {
        if (person.uid != host->uid) continue;
        person.god = std::string(god.id);
        // The host's school BECOMES a hidden school, which is what makes its next well sink
        // into a network nothing else can reach.
        person.school = std::string(god.school);
        person.corrupted = true;
        std::set<std::string> held(person.held_nodes.begin(), person.held_nodes.end());
        held.insert(nodes.begin(), nodes.end());
        person.held_nodes.assign(held.begin(), held.end());
        VillainLogEntry entry;
        entry.age = request.age;
        entry.event = "corrupted";
        entry.god = std::string(god.id);
        entry.encounters = request.encounters;
        person.log.push_back(entry);
    }

    record.roused = reveal_hidden_god(outcome.religion, god.id, host->uid, request.age);
    outcome.corruptions.push_back(record);

    outcome.acted = true;
    outcome.god_id = record.god_id;
    outcome.villain_uid = record.villain_uid;
    outcome.corruption = index;
    outcome.record = std::move(record);
    outcome.rebuild_required = true;
    return outcome;
}

// ---------------------------------------------------------------------------
// Cleansing.
// ---------------------------------------------------------------------------

CleanseOutcome cleanse_world(const CleanseRequest& request) {
    if (request.api_version != kCorruptionApiVersion) {
        throw CorruptionError("api_version", "this build serves corruption API version 1");
    }
    if (request.corruption.has_value() == request.god_id.has_value()) {
        throw CorruptionError("target",
                              "target names exactly one thing, as a corruption index or a god id");
    }
    if (!std::isfinite(request.power) || !(request.power > 0.0) || !(request.power <= 1.0)) {
        throw CorruptionError("power", "share of the structure drained, greater than 0 and at most 1");
    }

    if (request.god_id.has_value()) {
        const GodRecord* god = nullptr;
        for (const GodRecord& record : request.religion.gods) {
            if (record.id == *request.god_id) god = &record;
        }
        if (god == nullptr) {
            throw CorruptionError("target.god_id", *request.god_id + " is no god of this world");
        }
        if (god->status != "walking") {
            throw CorruptionError("target.god_id", "only a god that walks the world can be opposed; " +
                                                       *request.god_id + " is " + god->status);
        }
        // Two different things set a god walking, and only one of them writes a visitation
        // record: a visitation does, a revelation does not, because a corrupted god's
        // footprint is its ley cluster. Departure requires the record. Corruption is the
        // only thing in this library that can set a god walking at all, so this branch
        // refuses by name rather than reaching an unguarded lookup -- exactly as the
        // reference does, and for every god it can currently reach.
        throw CorruptionError("target.god_id",
                              "a god revealed by corruption is opposed through its corruption "
                              "rather than sent home; target its corruption index instead");
    }

    const int index = *request.corruption;
    if (index < 0 || index >= static_cast<int>(request.corruptions.size())) {
        throw CorruptionError("target.corruption",
                              "this world carries " + std::to_string(request.corruptions.size()) +
                                  " corruption record(s); " + std::to_string(index) +
                                  " names none of them");
    }
    if (request.corruptions[static_cast<std::size_t>(index)].cleansed_age.has_value()) {
        throw CorruptionError("target.corruption",
                              "corruption " + std::to_string(index) + " was already cleansed");
    }

    CleanseOutcome outcome;
    outcome.corruption = index;
    outcome.magic = request.magic;
    outcome.religion = request.religion;
    outcome.villains = request.villains;
    outcome.corruptions = request.corruptions;

    CorruptionRecord& record = outcome.corruptions[static_cast<std::size_t>(index)];
    outcome.god_id = record.god_id;
    LeyNetwork& net = network_for(outcome.magic, record.school);
    const double drain = request.power * kCleanseMaxDrain;
    const std::set<std::string> targets(record.nodes.begin(), record.nodes.end());

    std::vector<LeyNode> remaining;
    std::vector<int> keep;
    for (std::size_t i = 0; i < net.nodes.size(); ++i) {
        const LeyNode& node = net.nodes[i];
        if (targets.find(node.id) == targets.end()) {
            remaining.push_back(node);
            keep.push_back(static_cast<int>(i));
            continue;
        }
        const double left = round_dp(std::max(0.0, node.intensity - drain), 9);
        if (left <= 0.0) {
            outcome.removed.push_back(node.id);
            continue;
        }
        LeyNode drained = node;
        drained.intensity = left;
        remaining.push_back(std::move(drained));
        keep.push_back(static_cast<int>(i));
    }

    // DEPARTURE FROM THE REFERENCE, deliberately. Its guard here tests whether an edge
    // endpoint is a STRING, and endpoints are integer positions everywhere in this
    // codebase, so the guard never fires -- while a corrupted host's next well sinks into
    // its now-hidden school and joins edges to exactly these nodes. Removing them by filter
    // then leaves every later edge endpoint pointing at the wrong node. The endpoints are
    // remapped instead, which is what the reference's own departure path already does.
    std::map<int, int> moved;
    for (std::size_t i = 0; i < keep.size(); ++i) moved[keep[i]] = static_cast<int>(i);
    std::vector<LeyEdge> edges;
    for (const LeyEdge& edge : net.edges) {
        const auto from = moved.find(edge.from);
        const auto to = moved.find(edge.to);
        if (from == moved.end() || to == moved.end()) continue;
        edges.push_back({edge.id, from->second, to->second, edge.intensity});
    }
    net.nodes = std::move(remaining);
    net.edges = std::move(edges);

    std::vector<std::string> left;
    for (const std::string& node : record.nodes) {
        if (std::find(outcome.removed.begin(), outcome.removed.end(), node) == outcome.removed.end()) {
            left.push_back(node);
        }
    }
    record.nodes = std::move(left);
    outcome.remaining = static_cast<int>(record.nodes.size());
    outcome.cleansed = record.nodes.empty();
    outcome.rebuild_required = true;

    if (!outcome.cleansed) return outcome;

    record.cleansed_age = request.age;
    // The holder is unseated with its LAST node. Killing the villain would have left the
    // nodes standing and a successor free to take them; this is the other order.
    for (Villain& person : outcome.villains.people) {
        if (person.uid != record.villain_uid) continue;
        person.god.clear();
        person.corrupted = false;
        // FAITHFUL TO THE REFERENCE and wrong, recorded rather than fixed: `removed` is this
        // tick's removals alone, so a node driven to zero on an earlier partial tick stays
        // in the holder's `held_nodes` forever. A later well then tries to join an edge to
        // a node that no longer exists and silently skips it.
        std::vector<std::string> held;
        for (const std::string& node : person.held_nodes) {
            if (std::find(outcome.removed.begin(), outcome.removed.end(), node) ==
                outcome.removed.end()) {
                held.push_back(node);
            }
        }
        person.held_nodes = std::move(held);
        VillainLogEntry entry;
        entry.age = request.age;
        entry.event = "cleansed";
        entry.god = record.god_id;
        person.log.push_back(entry);
    }
    for (GodRecord& god : outcome.religion.gods) {
        if (god.id != record.god_id || god.status != "walking") continue;
        god.status = "sleeping";
        god.evidence.last_seen = true;
    }
    return outcome;
}

}  // namespace fwg
