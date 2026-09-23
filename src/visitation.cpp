#include "fwg/visitation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <sstream>
#include <utility>

#include "fwg/json.hpp"
#include "fwg/random.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr long long kUint32Bound = 4294967296LL;

// The ley cause's weight, which in the reference is terrain_history's ley_threat_pressure --
// shared there by the age fate and the threat assessment, neither of which is ported yet. When
// the age fate lands, this is the second copy of one rule and the two should become one.
constexpr double kLeyPressureGain = 0.45;
constexpr double kLeyPressureCap = 0.6;

const std::string_view kOrder[] = {"avatar cluster",
                                   "opposed suppression",
                                   "divine fate lottery",
                                   "ruins and legacies",
                                   "leyline update",
                                   "biomes",
                                   "civilization",
                                   "nests and purge",
                                   "threat assessment and favour",
                                   "astrology",
                                   "conversion"};

// Light against dark, creation against entropy, fire against water. Earth and air have NO
// opposite, so an earth or air god suppresses nothing at all.
std::string_view opposed_school(std::string_view school) {
    if (school == "radiant") return "infernal";
    if (school == "infernal") return "radiant";
    if (school == "weave") return "umbral";
    if (school == "umbral") return "weave";
    if (school == "fire") return "water";
    if (school == "water") return "fire";
    return {};
}

bool contains(std::span<const std::string_view> haystack, std::string_view needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

bool contains(const std::vector<std::string>& haystack, std::string_view needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

bool moon_is_seeded(const Moon& moon) {
    return moon.synodic_days > 0 && moon.spin_days > 0 && moon.nod_days > 0;
}

const GodRecord* find_god(const Religion& religion, std::string_view god_id) {
    for (const GodRecord& god : religion.gods) {
        if (god.id == god_id) return &god;
    }
    return nullptr;
}

const Faith* find_faith(const Religion& religion, std::string_view civilization) {
    for (const Faith& faith : religion.faiths) {
        if (faith.civilization == civilization) return &faith;
    }
    return nullptr;
}

LeyNetwork& network_for(MagicReport& magic, std::string_view school) {
    const auto it = magic.networks.find(std::string(school));
    if (it == magic.networks.end()) {
        throw VisitationError("magic.networks", "this world has no " + std::string(school) + " network");
    }
    return it->second;
}

// The network-edit contract is plant_key_point's (magic.hpp); this only makes its refusal
// a visitation refusal.
void plant_node(LeyNetwork& net, const LeyNode& node) {
    try {
        plant_key_point(net, node);
    } catch (const std::invalid_argument& refused) {
        throw VisitationError("magic.networks", refused.what());
    }
}

double aspect_factor(std::string_view aspect) {
    if (aspect == "wild") return kWildAspectFactor;
    if (aspect == "sovereign") return kSovereignAspectFactor;
    return 1.0;
}

// A beast nest as the shared threat rule reads one.
//
// Converted here rather than asked of the caller, because `ReligionNest::real` defaults TRUE:
// a caller that forgot the field would hand over every monster as an ordinary animal and the
// lottery would quietly lose its nest causes.
std::vector<ReligionNest> threat_view(const std::vector<NestSite>& nests) {
    std::vector<ReligionNest> out;
    out.reserve(nests.size());
    for (const NestSite& site : nests) {
        ReligionNest nest;
        nest.id = site.id;
        nest.name = std::string(site.name);
        nest.family = std::string(site.family);
        nest.layer = std::string(kNestSiteLayer);
        nest.real = site.real;
        nest.tier = site.tier;
        // Present, so it is used even when it is zero; the spacing fallback is for a record
        // that has no range at all.
        nest.range_m = site.range_m;
        nest.direction = site.direction;
        out.push_back(std::move(nest));
    }
    return out;
}

std::string avatar_record(int node, int since_age, long long day, int visitation,
                          const std::vector<std::string>& cluster) {
    std::ostringstream text;
    JsonWriter json(text, false);
    json.begin_object();
    json.field("node", node);
    json.field("since_age", since_age);
    json.field("day", day);
    json.field("visitation", visitation);
    json.key("cluster");
    json.value(cluster);
    json.end_object();
    return text.str();
}

// ---------------------------------------------------------------------------
// Validation.
// ---------------------------------------------------------------------------

struct SummonsPlan {
    const GodRecord* god = nullptr;
    const PantheonGod* entry = nullptr;
    int node = 0;
    int x = 0;
    int z = 0;
    long long day = 0;
    std::vector<std::string> schools;
};

SummonsPlan validate_summons(const VisitationRequest& request) {
    if (request.api_version != kVisitationApiVersion) {
        throw VisitationError("api_version", "this build serves visitation API version 1");
    }
    if (request.grid == nullptr || request.layers == nullptr) {
        throw VisitationError("world", "a summons needs the world's node graph and its layers");
    }
    if (request.religion.version != kReligionVersion || request.religion.gods.empty()) {
        throw VisitationError("religion",
                              "this world lacks the religion contract at version 1. It is written during "
                              "generation, so a world exported before it has to be regenerated rather "
                              "than migrated.");
    }
    if (request.religion.catalogue.sha256 != pantheon_identity().sha256) {
        throw VisitationError("religion.catalogue",
                              "this world was built against a different pantheon catalogue. The gods it "
                              "names may not exist here, so it must be regenerated or explicitly migrated "
                              "rather than summoned into.");
    }
    if (!moon_is_seeded(request.moon)) {
        throw VisitationError("astrology",
                              "this world has no seeded moon, and a summons reads the sky on the day the "
                              "god arrives. Regenerate it with astrology.");
    }
    if (!std::isfinite(request.globe_radius_m) || !(request.globe_radius_m > 0.0) ||
        !std::isfinite(request.settlement_spacing_m) || !(request.settlement_spacing_m > 0.0)) {
        throw VisitationError("world", "globe radius and settlement spacing must be positive metres");
    }

    SummonsPlan plan;
    plan.god = find_god(request.religion, request.god_id);
    if (plan.god == nullptr) {
        std::string known;
        for (const GodRecord& god : request.religion.gods) known += (known.empty() ? "" : ", ") + god.id;
        throw VisitationError("god_id", request.god_id + " is no god of this world; it knows " + known);
    }
    // DEPARTURE FROM THE REFERENCE. It reaches a cleansed -- and therefore sleeping -- hidden
    // god with no refusal at all, and then raises KeyError looking it up in the known
    // catalogue. Refused here by name instead.
    if (plan.god->family == kOutsideFamily || hidden_pantheon_god(request.god_id) != nullptr) {
        throw VisitationError("god_id", request.god_id +
                                            " is one of the four the pantheon does not speak of. It walks "
                                            "only by corruption and cannot be summoned.");
    }
    if (plan.god->status == "walking") {
        throw VisitationError("god_id", request.god_id +
                                            " already walks the world; summon another god or send this "
                                            "one home first.");
    }
    if (plan.god->status != "manifest" && plan.god->status != "sleeping") {
        std::string alternatives;
        for (const GodRecord& god : request.religion.gods) {
            const bool summonable = (god.status == "manifest" || god.status == "sleeping") &&
                                    god.family != kOutsideFamily;
            if (summonable) alternatives += (alternatives.empty() ? "" : ", ") + god.id;
        }
        throw VisitationError("god_id", "an absent god cannot be summoned; " + request.god_id +
                                            " does not exist in this world. Summonable: " + alternatives);
    }
    plan.entry = pantheon_god(request.god_id);
    if (plan.entry == nullptr) {
        throw VisitationError("god_id", request.god_id + " is not in the known catalogue");
    }
    if (request.visitations.size() >= static_cast<std::size_t>(kMaxVisitations)) {
        throw VisitationError("visitations", "this world already carries " +
                                                 std::to_string(request.visitations.size()) +
                                                 " visitations, and the per-world limit is 8");
    }

    if (request.target_city_uid.has_value() == request.target_node.has_value()) {
        throw VisitationError("target", "a target names exactly one place, as a city uid or a node");
    }
    if (request.target_city_uid.has_value()) {
        const SettlementSite* city = nullptr;
        for (const SettlementSite& site : request.cities) {
            if (site.uid == *request.target_city_uid) city = &site;
        }
        if (city == nullptr) throw VisitationError("target.city_uid", *request.target_city_uid + " is no city");
        plan.node = city->node;
    } else {
        plan.node = *request.target_node;
    }
    const SphereGrid& graph = *request.grid;
    if (plan.node < 0 || static_cast<std::size_t>(plan.node) >= graph.node_count()) {
        throw VisitationError("target.node", "target node outside the grid");
    }
    plan.x = graph.point_x[static_cast<std::size_t>(plan.node)];
    plan.z = graph.point_z[static_cast<std::size_t>(plan.node)];
    const auto water = request.layers->find("water_type");
    if (water == request.layers->end() || water->second.size() != graph.n) {
        throw VisitationError("layers.water_type", "this world has no water_type layer at its grid size");
    }
    if (water->second.at(plan.x, plan.z) != 0.0) {
        throw VisitationError("target", "a god walks on land; the target is water");
    }

    if (!std::isfinite(request.wrath) || request.wrath < 0.0 || request.wrath > 1.0) {
        throw VisitationError("wrath", "wrath must be 0..1");
    }
    if (request.variation < 0 || request.variation >= kUint32Bound) {
        throw VisitationError("variation", "variation must be a uint32");
    }
    if (request.day.has_value() && *request.day < 0) {
        throw VisitationError("day", "day must be an integer day >= 0");
    }

    // The two modules this port does not own, checked up front rather than when the dice
    // happen to reach them: a missing join would otherwise pass every summons whose cities
    // all stood and fail the first one that did not.
    const auto table = magic_school_table();
    for (int s = 0; s < kKnownSchools; ++s) {
        const std::string_view school = table[static_cast<std::size_t>(s)].name;
        const auto it = request.ley_destruction.find(school);
        if (it == request.ley_destruction.end() || it->second.empty()) {
            throw VisitationError("ley_destruction", "no ley destruction reason for " + std::string(school) +
                                                         "; the table must name all eight known schools");
        }
    }
    if (!request.ruin_legacy) {
        throw VisitationError("ruin_legacy", "a summons needs the ruins module's legacy resolution");
    }

    // The arrival day is the first day of the year founding ended, unless the orchestrator
    // named one.
    plan.day = request.day.has_value() ? *request.day : request.founding_end_year * kDaysPerYear;
    plan.schools = god_schools(request.god_id, request.moon, plan.day);
    if (plan.schools.empty()) {
        // No shipped god has this shape, and the reference would raise IndexError on the first
        // ruin or at departure.
        throw VisitationError("god_id", request.god_id + " saturates no known school");
    }
    for (const std::string& school : plan.schools) {
        if (request.magic.networks.find(school) == request.magic.networks.end()) {
            throw VisitationError("magic.networks", "this world has no " + school + " network");
        }
        const std::string_view opposed = opposed_school(school);
        if (!opposed.empty() && request.magic.networks.find(std::string(opposed)) == request.magic.networks.end()) {
            throw VisitationError("magic.networks", "this world has no " + std::string(opposed) + " network");
        }
    }
    return plan;
}

// ---------------------------------------------------------------------------
// The summons, step by step.
// ---------------------------------------------------------------------------

// Five nodes per school on ONE bearing drawn once, before the school loop, so every school's
// ring sits at the same four bearings. Moving the draw inside the loop moves every
// multi-school summons.
std::vector<std::string> avatar_cluster(MagicReport& magic, const std::string& god_id,
                                        const std::vector<std::string>& schools, const Vec3& centre,
                                        Rng& rng, int index, double spacing_m, double radius_m) {
    const double angle = kAvatarRingSpacings * spacing_m / radius_m;
    const double base = rng.uniform(0.0, 2.0 * kPi);

    std::vector<std::pair<Vec3, double>> placements;
    placements.emplace_back(centre, kAvatarIntensity);
    for (int k = 0; k < kAvatarRingPoints; ++k) {
        const double bearing = base + k * 2.0 * kPi / kAvatarRingPoints;
        placements.emplace_back(offset_direction(centre, angle, bearing), kAvatarRingIntensity);
    }

    std::vector<std::string> ids;
    for (const std::string& school : schools) {
        LeyNetwork& net = network_for(magic, school);
        for (std::size_t k = 0; k < placements.size(); ++k) {
            const std::string id = "avatar-" + god_id + "-" + std::to_string(index) + "-" + school + "-" +
                                   std::to_string(k);
            plant_node(net, {id, placements[k].first, placements[k].second});
            ids.push_back(id);
        }
    }
    return ids;
}

// Weaken rival schools within reach; the record lets departure restore them exactly.
//
// Runs AFTER the cluster is planted and reads the networks as they now stand, so a god whose
// own schools oppose each other -- the Outer Unnamed, weave and umbral -- halves its own
// avatar at full wrath. That is the reference's behaviour and it is kept; its restore entries
// for its own nodes then miss at departure, because the cluster is removed first.
std::vector<VisitationRestore> suppress_opposed(MagicReport& magic, const std::vector<std::string>& schools,
                                                const Vec3& centre, double reach_m, double wrath,
                                                double radius_m) {
    const double scale = 1.0 - kSuppression * wrath;
    std::vector<VisitationRestore> restore;
    for (const std::string& school : schools) {
        const std::string_view opposed = opposed_school(school);
        if (opposed.empty()) continue;
        LeyNetwork& net = network_for(magic, opposed);
        std::vector<bool> near(net.nodes.size(), false);
        for (std::size_t i = 0; i < net.nodes.size(); ++i) {
            near[i] = great_circle_m(radius_m, centre, net.nodes[i].direction) <= reach_m;
        }
        // Nodes in index order, then lines in list order: the order is only the record's
        // bytes, because departure restores by id and writes an absolute value.
        for (std::size_t i = 0; i < net.nodes.size(); ++i) {
            if (!near[i]) continue;
            restore.push_back({std::string(opposed), net.nodes[i].id, "", net.nodes[i].intensity});
            net.nodes[i].intensity *= scale;
        }
        const auto touches = [&near](int endpoint) {
            return endpoint >= 0 && static_cast<std::size_t>(endpoint) < near.size() &&
                   near[static_cast<std::size_t>(endpoint)];
        };
        for (LeyEdge& edge : net.edges) {
            if (!touches(edge.from) && !touches(edge.to)) continue;
            restore.push_back({std::string(opposed), "", edge.id, edge.intensity});
            edge.intensity *= scale;
        }
    }
    return restore;
}

struct Cause {
    std::string kind;
    double weight = 0.0;
    std::string reason;
    VisitationEvidence evidence;
};

struct Lottery {
    std::vector<SettlementSite> survivors;
    std::vector<VisitationRuin> ruins;
    std::vector<VisitationFavor> favored;
};

// Every city within reach rolls the age lottery with one more cause: the god itself.
//
// A second, parallel implementation of the age fate rather than a call into it: no self-magic
// cause, no villain causes, no lunar sway, and the divine cause appended LAST.
Lottery divine_lottery(const VisitationRequest& request, const GodRecord& god, const PantheonGod& entry,
                       const VisitationSummons& summons, const std::string& stream) {
    const double radius = request.globe_radius_m;
    const double reach = summons.reach_m;
    const double aspect = aspect_factor(god.aspect);
    const std::vector<ReligionNest> nests = threat_view(request.nests);
    const std::string reason_divine =
        (god.aspect_name.empty() ? god.name : god.aspect_name) + ": " + std::string(entry.smite) + ".";

    // Hoisted: one lookup per school rather than one string build per school per city.
    const auto table = magic_school_table();
    std::vector<const Grid*> ley(table.size(), nullptr);
    for (std::size_t s = 0; s < table.size(); ++s) {
        const auto it = request.layers->find("ley_" + std::string(table[s].name));
        if (it != request.layers->end()) ley[s] = &it->second;
    }

    Lottery out;
    for (const SettlementSite& city : request.cities) {
        const double distance = great_circle_m(radius, summons.centre, city.direction);
        // A city exactly at the reach is INSIDE, and the god's own weight there is zero.
        if (distance > reach) {
            out.survivors.push_back(city);
            continue;
        }

        // Every school, known or not: a god smiting a city sees the ground as it is.
        std::vector<double> potencies(table.size(), 0.0);
        for (std::size_t s = 0; s < table.size(); ++s) {
            if (ley[s] == nullptr) continue;
            const Grid& grid = *ley[s];
            if (city.x < 0 || city.x >= grid.size() || city.z < 0 || city.z >= grid.size()) {
                throw VisitationError("cities", "city " + city.uid + " stands outside the ley grid");
            }
            potencies[s] = grid.at(city.x, city.z);
        }

        // ORDER IS ATTRIBUTION: ley, then nests in id order, then the god. The selector walk
        // below subtracts in this order and defaults to the last, so reordering these changes
        // which cause a given draw lands on, which changes the ruin's school and its key point.
        std::vector<Cause> causes;
        const int winner = dominant_school(potencies, request.magic.mutation_threshold,
                                           request.magic.dominance_margin);
        if (winner >= 0) {
            const std::string school(table[static_cast<std::size_t>(winner)].name);
            const auto reason = request.ley_destruction.find(school);
            if (reason == request.ley_destruction.end()) {
                // Reachable: a corrupted seat node sits on the host's own seat city at full
                // intensity, so a hidden school can own the ground there. The reference raises
                // KeyError. No sentence exists for it and none is invented here.
                throw VisitationError("ley_destruction",
                                      "the ground under " + city.uid + " belongs to the " + school +
                                          " school, which has no ley destruction reason; a hidden "
                                          "school needs a ruling before a god can smite its ground");
            }
            Cause ley_cause;
            ley_cause.kind = school;
            const double potency = potencies[static_cast<std::size_t>(winner)];
            ley_cause.weight = std::min(kLeyPressureCap, potency * kLeyPressureGain);
            ley_cause.reason = reason->second;
            ley_cause.evidence.kind = VisitationCauseKind::Ley;
            ley_cause.evidence.school = school;
            ley_cause.evidence.potency = potency;
            causes.push_back(std::move(ley_cause));
        }

        ReligionCity view;
        view.uid = city.uid;
        view.population_profile = city.population_profile;
        view.x = city.x;
        view.z = city.z;
        view.node = city.node;
        view.direction = city.direction;
        for (const NestThreat& threat : nest_threats(view, nests, radius)) {
            Cause nest_cause;
            nest_cause.kind = threat.kind;
            nest_cause.weight = threat.weight;
            nest_cause.reason = threat.reason;
            nest_cause.evidence.kind = VisitationCauseKind::Nest;
            nest_cause.evidence.nest_id = threat.nest_id;
            nest_cause.evidence.family = threat.family;
            nest_cause.evidence.distance_m = threat.distance_m;
            nest_cause.evidence.reach_m = threat.reach_m;
            causes.push_back(std::move(nest_cause));
        }

        const double faith = visitation_faith_factor(request.religion, city.population_profile, god.id);
        Cause divine;
        divine.kind = "divine_" + god.id;
        divine.weight = kDivineWeight * request.wrath * (1.0 - distance / reach) * aspect * faith;
        divine.reason = reason_divine;
        divine.evidence.kind = VisitationCauseKind::Divine;
        divine.evidence.god_id = god.id;
        divine.evidence.distance_m = distance;
        divine.evidence.reach_m = reach;
        divine.evidence.aspect = god.aspect;
        divine.evidence.faith_factor = faith;
        causes.push_back(std::move(divine));

        double total = 0.0;
        for (const Cause& cause : causes) total += cause.weight;
        const double chance = std::min(kVisitationChanceCap, total);

        // ONE STREAM PER CITY, keyed on its uid, one draw each. A city's roll therefore depends
        // on nothing but its own identity: adding a city elsewhere, or visiting these in another
        // order, cannot move it.
        Rng rng = Rng::child(request.seed, stream + "-" + city.uid, request.variation);
        const double draw = rng.random();

        if (draw >= chance) {
            if (faith < 1.0) {
                out.favored.push_back({city.uid, round_dp(request.wrath * (1.0 - distance / reach), 6)});
            }
            out.survivors.push_back(city);
            continue;
        }

        double selector = (draw / chance) * total;
        // Float drift can leave the walk a hair above zero after the last subtraction; the god
        // is the cause it was about to reach.
        const Cause* chosen = &causes.back();
        for (const Cause& cause : causes) {
            selector -= cause.weight;
            if (selector <= 0.0) {
                chosen = &cause;
                break;
            }
        }

        const std::string_view family =
            chosen->evidence.kind == VisitationCauseKind::Nest ? std::string_view(chosen->evidence.family)
                                                               : std::string_view();
        const VisitationLegacy legacy =
            request.ruin_legacy({&city, chosen->kind, potencies, family, summons.schools.front()});
        if (legacy.school.empty() || request.magic.networks.find(legacy.school) == request.magic.networks.end()) {
            throw VisitationError("ruin_legacy", "the legacy of " + city.uid + " names no school with a network");
        }

        VisitationRuin ruin;
        ruin.id = "ruin-" + city.uid;
        ruin.city_uid = city.uid;
        ruin.name = city.name;
        ruin.node = city.node;
        ruin.x = city.x;
        ruin.z = city.z;
        ruin.direction = city.direction;
        ruin.height_m = city.height_m;
        ruin.population_profile = city.population_profile;
        ruin.city_class = city.city_class;
        ruin.destroyed_age = request.age;
        ruin.cause = chosen->kind;
        ruin.reason = chosen->reason;
        ruin.evidence = chosen->evidence;
        ruin.probability = chance;
        ruin.roll = draw;
        ruin.new_node_school = legacy.school;
        ruin.legacy = legacy;
        ruin.visitation = summons.index;
        out.ruins.push_back(std::move(ruin));
    }
    return out;
}

}  // namespace

std::span<const std::string_view> visitation_order() { return {kOrder, std::size(kOrder)}; }

double visitation_reach_m(double settlement_spacing_m) {
    return kVisitationReachSpacings * settlement_spacing_m;
}

std::vector<std::string> god_schools(std::string_view god_id, const Moon& moon, long long day) {
    const PantheonGod* god = pantheon_god(god_id);
    if (god == nullptr) {
        throw VisitationError("god_id", std::string(god_id) + " is not a known god; only the known pantheon walks by visitation");
    }
    const auto table = magic_school_table();
    std::vector<std::string> out;

    // EXACTLY ['moon'], as the reference tests it. A god with the moon among other schools
    // takes only the others.
    const bool moon_god = god->school_affinity.size() == 1 && god->school_affinity[0] == kMoonPseudoSchool;
    if (moon_god) {
        if (!moon_is_seeded(moon)) {
            throw VisitationError("astrology", "the moon god leans with a moon this world has not seeded");
        }
        const bool still = moon_state(moon, day).nod_degrees >= 0.0;
        for (int s = 0; s < kKnownSchools; ++s) {
            // The Still hemisphere is the one whose quarters point up the moon's polar axis:
            // the astrology module's own definition, read rather than restated.
            const bool quarter_is_still = school_region(s).y > 0.0;
            if (quarter_is_still == still) out.emplace_back(table[static_cast<std::size_t>(s)].name);
        }
        return out;
    }
    // Known-school order, not the affinity list's: the first is the divine legacy's school and
    // the footprint's.
    for (int s = 0; s < kKnownSchools; ++s) {
        const std::string_view school = table[static_cast<std::size_t>(s)].name;
        if (contains(god->school_affinity, school)) out.emplace_back(school);
    }
    return out;
}

double visitation_faith_factor(const Religion& religion, std::string_view population_profile,
                               std::string_view god_id) {
    const Faith* faith = find_faith(religion, population_profile);
    if (faith == nullptr) return 1.0;
    if (contains(faith->gods, god_id)) return kOwnFaithfulFactor;
    if (!faith->patron.empty()) {
        const PantheonGod* patron = pantheon_god(faith->patron);
        const PantheonGod* god = pantheon_god(god_id);
        const bool rival = (patron != nullptr && contains(patron->rivals, god_id)) ||
                           (god != nullptr && contains(god->rivals, faith->patron));
        if (rival) return kRivalFlockFactor;
    }
    return 1.0;
}

VisitationSummons summon_god(const VisitationRequest& request) {
    const SummonsPlan plan = validate_summons(request);

    VisitationSummons summons;
    summons.index = static_cast<int>(request.visitations.size());
    summons.god_id = request.god_id;
    summons.aspect = plan.god->aspect;
    summons.node = plan.node;
    summons.centre = direction(plan.x, plan.z, request.grid->n);
    summons.age = request.age;
    summons.day = plan.day;
    summons.wrath = request.wrath;
    summons.variation = request.variation;
    summons.reach_m = visitation_reach_m(request.settlement_spacing_m);
    summons.globe_radius_m = request.globe_radius_m;
    summons.schools = plan.schools;
    summons.moon = day_state(request.moon, plan.day);
    summons.magic = request.magic;
    summons.religion = request.religion;
    summons.visitations = request.visitations;

    const std::string stream = "visitation-" + std::to_string(summons.index) + "-" + summons.god_id;
    Rng rng = Rng::child(request.seed, stream, request.variation);
    summons.cluster = avatar_cluster(summons.magic, summons.god_id, summons.schools, summons.centre, rng,
                                     summons.index, request.settlement_spacing_m, request.globe_radius_m);
    summons.restore = suppress_opposed(summons.magic, summons.schools, summons.centre, summons.reach_m,
                                       summons.wrath, request.globe_radius_m);

    Lottery lottery = divine_lottery(request, *plan.god, *plan.entry, summons, stream);
    // Every fallen city leaves a key point at its legacy school -- after the lottery, which
    // read the ground as it stood before any of them.
    for (const VisitationRuin& ruin : lottery.ruins) {
        plant_node(network_for(summons.magic, ruin.legacy.school),
                   {ruin.id + "-key", ruin.direction, ruin.legacy.intensity});
    }
    summons.new_ruins = std::move(lottery.ruins);
    summons.survivors = std::move(lottery.survivors);
    summons.favored = std::move(lottery.favored);
    summons.rebuild_required = true;
    return summons;
}

VisitationOutcome complete_summons(const VisitationSummons& summons, const VisitationRebuilt& rebuilt) {
    const PantheonGod* entry = pantheon_god(summons.god_id);
    if (summons.index < 0 || entry == nullptr || find_god(summons.religion, summons.god_id) == nullptr) {
        throw VisitationError("summons", "complete only a summons that summon_god produced");
    }
    const double radius = summons.globe_radius_m;

    VisitationOutcome out;
    out.religion = summons.religion;
    out.visitations = summons.visitations;

    // Rival nests are driven out AFTER the rebuild regenerated them; purging first would be
    // undone by it. Real animals are never purged.
    std::vector<std::string> purged;
    for (const NestSite& nest : rebuilt.nests) {
        const bool rival = contains(entry->purge_families, nest.family) && !nest.real &&
                           great_circle_m(radius, summons.centre, nest.direction) <= summons.reach_m;
        if (rival) {
            purged.push_back(nest.id);
        } else {
            out.nests.push_back(nest);
        }
    }

    // Favour is relief on the RE-ASSESSED threat, which is why it waits for the rebuild.
    for (const VillainThreatReport& report : rebuilt.reports) {
        const auto favor = std::find_if(summons.favored.begin(), summons.favored.end(),
                                        [&report](const VisitationFavor& f) { return f.city_uid == report.city_uid; });
        if (favor == summons.favored.end()) continue;
        out.relief.push_back({report.city_uid, favor->strength,
                              round_dp(std::max(0.0, report.regional_threat - kFavorRelief * favor->strength), 6)});
    }

    // The peoples that stand within reach convert, in first-seen order over the REBUILT sites.
    // A people whose patron is already this god is not converted to it.
    std::set<std::string> ruined;
    for (const VisitationRuin& ruin : summons.new_ruins) ruined.insert(ruin.city_uid);
    std::vector<std::string> converted;
    for (const SettlementSite& city : rebuilt.cities) {
        if (great_circle_m(radius, summons.centre, city.direction) > summons.reach_m) continue;
        if (ruined.count(city.uid) != 0) continue;
        const Faith* faith = find_faith(out.religion, city.population_profile);
        if (faith == nullptr || faith->patron == summons.god_id) continue;
        if (!contains(converted, city.population_profile)) converted.push_back(city.population_profile);
    }

    VisitationRecord record;
    record.index = summons.index;
    record.god_id = summons.god_id;
    record.aspect = summons.aspect;
    record.node = summons.node;
    record.direction = summons.centre;
    record.age = summons.age;
    record.day = summons.day;
    record.wrath = summons.wrath;
    record.variation = summons.variation;
    record.schools = summons.schools;
    record.reach_m = summons.reach_m;
    record.cluster = summons.cluster;
    record.restore = summons.restore;
    for (const VisitationRuin& ruin : summons.new_ruins) record.ruins.push_back(ruin.id);
    record.purged_nests = purged;
    record.favored = summons.favored;
    record.converted = converted;
    record.moon = summons.moon;

    for (GodRecord& god : out.religion.gods) {
        if (god.id != summons.god_id) continue;
        god.status = "walking";
        god.avatar = avatar_record(summons.node, summons.age, summons.day, summons.index, summons.cluster);
    }
    for (Faith& faith : out.religion.faiths) {
        if (contains(converted, faith.civilization)) faith.conversion = Conversion{summons.god_id, summons.index};
    }
    ReligionSite site;
    site.id = "theophany-" + std::to_string(summons.index) + "-" + summons.god_id;
    site.kind = "theophany";
    site.god_id = summons.god_id;
    site.node = summons.node;
    site.visitation = summons.index;
    // Inserted in place, because the block's sites are sorted by id; the resolve the caller
    // owes next re-sorts them anyway.
    const auto at = std::lower_bound(out.religion.sites.begin(), out.religion.sites.end(), site.id,
                                     [](const ReligionSite& a, const std::string& id) { return a.id < id; });
    out.religion.sites.insert(at, site);
    out.visitations.push_back(record);

    out.operation.kind = "visitation";
    out.operation.visitation = summons.index;
    out.operation.god_id = summons.god_id;
    out.operation.node = summons.node;
    out.operation.wrath = summons.wrath;
    out.operation.variation = summons.variation;
    out.record = std::move(record);
    return out;
}

std::vector<std::string> walking_gods(const Religion& religion,
                                      const std::vector<VisitationRecord>& visitations) {
    std::vector<std::string> out;
    for (const GodRecord& god : religion.gods) {
        if (god.status != "walking") continue;
        const bool by_visitation =
            std::any_of(visitations.begin(), visitations.end(), [&god](const VisitationRecord& v) {
                return v.god_id == god.id && !v.departed_age.has_value();
            });
        // A god revealed by corruption walks with no record and is not sent home: its footprint
        // is its corrupted cluster, which outlives the age and is drained by a cleanse.
        if (by_visitation) out.push_back(god.id);
    }
    return out;
}

VisitationDeparture depart_god(const VisitationDepartureRequest& request) {
    const GodRecord* god = find_god(request.religion, request.god_id);
    if (god == nullptr) {
        throw VisitationError("god_id", request.god_id + " is no god of this world");
    }
    if (god->status != "walking") {
        throw VisitationError("god_id", "only a walking god can depart; " + request.god_id + " is " + god->status);
    }
    // The MOST RECENT undeparted record.
    std::size_t found = request.visitations.size();
    for (std::size_t i = request.visitations.size(); i-- > 0;) {
        const VisitationRecord& v = request.visitations[i];
        if (v.god_id == request.god_id && !v.departed_age.has_value()) {
            found = i;
            break;
        }
    }
    if (found == request.visitations.size()) {
        throw VisitationError("god_id", "no undeparted visitation for " + request.god_id +
                                            "; that god does not walk by visitation. A god revealed by "
                                            "corruption is opposed through its corruption, not sent home.");
    }

    VisitationDeparture out;
    out.magic = request.magic;
    out.religion = request.religion;
    out.visitations = request.visitations;
    VisitationRecord& record = out.visitations[found];
    if (record.schools.empty()) {
        throw VisitationError("visitations", "visitation " + std::to_string(record.index) + " names no school");
    }

    const std::set<std::string> cluster(record.cluster.begin(), record.cluster.end());
    for (auto& [school, net] : out.magic.networks) {
        const bool holds = std::any_of(net.nodes.begin(), net.nodes.end(),
                                       [&cluster](const LeyNode& n) { return cluster.count(n.id) != 0; });
        if (!holds) continue;
        // Edges index their endpoints by POSITION, so dropping a node renumbers every edge
        // after it. Avatar nodes carry no edges and are appended last, which is why a plain
        // filter used to be safe; but a ruin's key point can land after them in the same
        // network, and a villain's well joins edges to whatever it holds, so the endpoints are
        // remapped rather than left pointing at whatever slid down.
        std::vector<LeyNode> kept;
        std::vector<int> moved(net.nodes.size(), -1);
        for (std::size_t i = 0; i < net.nodes.size(); ++i) {
            if (cluster.count(net.nodes[i].id) != 0) continue;
            moved[i] = static_cast<int>(kept.size());
            kept.push_back(net.nodes[i]);
        }
        const auto remap = [&moved](int endpoint) {
            return endpoint >= 0 && static_cast<std::size_t>(endpoint) < moved.size()
                       ? moved[static_cast<std::size_t>(endpoint)]
                       : -1;
        };
        std::vector<LeyEdge> edges;
        for (const LeyEdge& edge : net.edges) {
            const int from = remap(edge.from);
            const int to = remap(edge.to);
            if (from < 0 || to < 0) continue;
            edges.push_back({edge.id, from, to, edge.intensity});
        }
        net.nodes = std::move(kept);
        net.edges = std::move(edges);
    }

    out.footprint = "footprint-" + request.god_id + "-" + std::to_string(record.index);
    plant_node(network_for(out.magic, record.schools.front()),
               {out.footprint, record.direction, kFootprintIntensity});

    // The network is looked up PER ENTRY. A summons suppresses up to three opposed schools, and
    // an entry restored into any network but its own misses silently.
    for (const VisitationRestore& item : record.restore) {
        LeyNetwork& net = network_for(out.magic, item.school);
        bool restored = false;
        if (!item.node_id.empty()) {
            for (LeyNode& node : net.nodes) {
                if (node.id != item.node_id) continue;
                node.intensity = item.intensity;  // EXACT, not a re-scale
                restored = true;
                break;
            }
        } else {
            for (LeyEdge& edge : net.edges) {
                if (edge.id != item.line_id) continue;
                edge.intensity = item.intensity;
                restored = true;
                break;
            }
        }
        if (!restored) out.unrestored.push_back(item.node_id.empty() ? item.line_id : item.node_id);
    }

    record.departed_age = request.age;
    for (GodRecord& record_of_god : out.religion.gods) {
        if (record_of_god.id != request.god_id) continue;
        record_of_god.status = "manifest";
        record_of_god.avatar.reset();
    }
    for (ReligionSite& site : out.religion.sites) {
        if (site.kind == "theophany" && site.visitation == record.index) site.kind = "pilgrimage";
    }

    out.operation.kind = "departure";
    out.operation.visitation = record.index;
    out.operation.god_id = request.god_id;
    out.record = record;
    out.rebuild_required = true;
    return out;
}

}  // namespace fwg
