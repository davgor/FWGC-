#include "fwg/fates.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <utility>

#include "fwg/corruption.hpp"
#include "fwg/entities.hpp"
#include "fwg/heritage.hpp"
#include "fwg/magic.hpp"
#include "fwg/random.hpp"

namespace fwg {
namespace {

constexpr std::string_view kSelfMagicReason =
    "The city destroyed itself in a magical experiment, leaving a new Weave key point.";

constexpr std::string_view kThreatMethod =
    "Per-city standing pressure from eligible fantasy nests (dragons and infernal/undead/aberrant "
    "families within reach) plus dominant local leyline potency, and the war outlook: wars fought "
    "this age, whether a former opponent still stands, and the strongest contention pressure a "
    "living neighbour exerts (the pair pressure the next age lottery draws against). Same nest "
    "eligibility and reach as age-fate weights; excludes magical self-destruction.";

constexpr std::string_view kThreatLimits =
    "Artistic regional threat for fortification and shape preference, not creature counts, "
    "hostility AI or guaranteed attacks. Real-animal nests do not contribute. The war outlook is a "
    "forecast, not a promise, and does not move regional_threat.";

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string school_name(int index) {
    return std::string(magic_school_table()[static_cast<std::size_t>(index)].name);
}

double potency_at(const std::vector<double>& potencies, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < potencies.size()
               ? potencies[static_cast<std::size_t>(index)]
               : 0.0;
}

// What a nest family leaves in the ley field. A code constant rather than data, because it
// is a rule about which magic a kind of creature carries, not content a writer revises.
// Empty is "no magic of its own": fantastic beasts, and any family this table never named.
constexpr std::string_view kPrimordial = "primordial";
std::string_view nest_family_school(std::string_view family) {
    if (family == "infernal") return "infernal";
    if (family == "undead") return "umbral";
    if (family == "aberrant") return "weave";
    if (family == "fey") return "weave";
    if (family == "holy") return "radiant";
    if (family == "unholy") return "infernal";
    if (family == "draconic") return "fire";
    if (family == "primordial") return kPrimordial;
    return {};
}

// The strongest element under the ruin. A dead tie keeps SCHOOL-TABLE order -- fire, water,
// earth, air -- where a tie-break by name would hand every dead tie to air.
std::string strongest_element(const std::vector<double>& potencies) {
    constexpr std::string_view kElements[] = {"fire", "water", "earth", "air"};
    int best = -1;
    double best_potency = 0.0;
    for (const std::string_view element : kElements) {
        const int index = magic_school_index(element);
        const double potency = potency_at(potencies, index);
        if (best < 0 || potency > best_potency || (potency == best_potency && index < best)) {
            best = index;
            best_potency = potency;
        }
    }
    return school_name(best);
}

double class_intensity(CityClass city_class) {
    switch (city_class) {
        case CityClass::Capital: return kClassIntensityCapital;
        case CityClass::Medium: return kClassIntensityMedium;
        case CityClass::Small: return kClassIntensitySmall;
    }
    return kClassIntensitySmall;
}

const Grid* find_layer(const std::map<std::string, Grid>* layers, const std::string& name) {
    if (layers == nullptr) return nullptr;
    const auto it = layers->find(name);
    return it != layers->end() ? &it->second : nullptr;
}

// A city off a layer it is read from is the reference's IndexError, not a zero: a
// potency invented for a cell that does not exist would decide a fate.
double read_cell(const Grid& grid, const WarCity& city, const std::string& name) {
    if (city.x < 0 || city.z < 0 || city.x >= grid.size() || city.z >= grid.size()) {
        throw std::out_of_range("city " + city.uid + " at (" + std::to_string(city.x) + ", " +
                                std::to_string(city.z) + ") is off the " + name + " grid");
    }
    return grid.at(city.x, city.z);
}

// The shared nest reader takes religion's view of a city.
ReligionCity religion_city(const WarCity& city) {
    ReligionCity out;
    out.uid = city.uid;
    out.population_profile = city.population_profile;
    out.x = city.x;
    out.z = city.z;
    out.node = city.node;
    out.direction = city.direction;
    return out;
}

VillainCity villain_city(const WarCity& city) {
    VillainCity out;
    out.uid = city.uid;
    out.id = city.id;
    out.node = city.node;
    out.direction = city.direction;
    out.population_profile = city.population_profile;
    out.city_class = city.city_class;
    out.population_estimate = static_cast<double>(city.population_estimate);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Ruins.
// ---------------------------------------------------------------------------

std::string_view culture_school(std::string_view civilization_id) {
    // Built once: resolving a heritage is not cheap and every ruin in every age asks. Held
    // as owned strings so nothing here depends on how long a resolved people lives.
    static const std::map<std::string, std::string, std::less<>> table = [] {
        std::map<std::string, std::string, std::less<>> out;
        for (const EntityRules& entity : entity_rules()) {
            const heritage::Resolved people = heritage::resolve(entity.id, entity.parent_race_id);
            out.emplace(std::string(entity.id), std::string(people.traits.axis("magic_school")));
        }
        return out;
    }();
    const auto it = table.find(civilization_id);
    return it != table.end() ? std::string_view(it->second) : std::string_view{};
}

std::string source_school(std::string_view cause, const std::vector<double>& potencies,
                          const RuinSources& sources) {
    if (magic_school_index(cause) >= 0) return std::string(cause);
    if (cause == "self_magic") return "weave";
    if (starts_with(cause, "divine")) return sources.god_school;
    // A villain scars the ground with whatever it holds. Holding nothing, it falls through
    // to the region and then the culture, exactly as an unmagical cause does.
    if (starts_with(cause, "villain")) return sources.villain_school;
    if (starts_with(cause, "war_")) return std::string(culture_school(sources.victor_culture));
    const std::string_view school = nest_family_school(sources.nest_family);
    if (school == kPrimordial) return strongest_element(potencies);
    return std::string(school);
}

RuinLegacy ruin_legacy(std::string_view cause, const std::vector<double>& potencies,
                       std::string_view population_profile, CityClass city_class,
                       const RuinSources& sources, double dominance_threshold,
                       double dominance_margin) {
    RuinLegacy out;
    out.school = source_school(cause, potencies, sources);
    out.basis = "source";
    if (out.school.empty()) {
        const int winner = dominant_school(potencies, dominance_threshold, dominance_margin);
        if (winner >= 0) out.school = school_name(winner);
        out.basis = "region";
    }
    if (out.school.empty()) {
        const std::string_view own = culture_school(population_profile);
        out.school = own.empty() ? std::string("weave") : std::string(own);
        out.basis = "culture";
    }
    out.intensity = class_intensity(city_class);
    if (cause == "self_magic") out.intensity = std::max(out.intensity, kRuinSelfMagicFloor);
    if (starts_with(cause, "divine")) out.intensity = kRuinDivineIntensity;
    return out;
}

// ---------------------------------------------------------------------------
// The moon and the potencies it sways.
// ---------------------------------------------------------------------------

std::optional<CityFateLunar> lunar_context(const Moon* moon, std::optional<long long> day,
                                           long long founding_end_year, double lunar_influence) {
    if (moon == nullptr) return std::nullopt;
    const long long when = day.has_value() ? *day : founding_end_year * kDaysPerYear;
    const DayState state = day_state(*moon, when);
    CityFateLunar out;
    out.tide = state.tide;
    out.influence = lunar_influence;
    out.day = when;
    out.phase_fraction = state.phase_fraction;
    out.elongation_degrees = state.elongation_degrees;
    out.illumination = state.illumination;
    out.leaning = state.leaning;
    return out;
}

std::vector<double> city_potencies(const std::map<std::string, Grid>* layers, const WarCity& city,
                                   const std::optional<CityFateLunar>& lunar) {
    const auto table = magic_school_table();
    std::vector<double> potencies(table.size(), 0.0);
    for (std::size_t s = 0; s < table.size(); ++s) {
        const std::string name = "ley_" + std::string(table[s].name);
        if (const Grid* grid = find_layer(layers, name)) potencies[s] = read_cell(*grid, city, name);
    }
    if (!lunar.has_value()) return potencies;

    const Grid* sensitivity = find_layer(layers, "lunar_sensitivity");
    const double sway = sensitivity != nullptr ? read_cell(*sensitivity, city, "lunar_sensitivity") : 0.0;
    for (std::size_t s = 0; s < table.size() && s < static_cast<std::size_t>(kKnownSchools); ++s) {
        if (table[s].hidden) continue;
        const double factor = 1.0 + lunar->influence * sway * (lunar->tide[s] - 1.0);
        potencies[s] = potencies[s] * factor;
    }
    return potencies;
}

// ---------------------------------------------------------------------------
// Threat.
// ---------------------------------------------------------------------------

ThreatLeyPressure ley_threat_pressure(const std::vector<double>& potencies, double dominance_threshold,
                                      double dominance_margin) {
    ThreatLeyPressure out;
    const int winner = dominant_school(potencies, dominance_threshold, dominance_margin);
    if (winner < 0) return out;
    const double potency = potency_at(potencies, winner);
    out.pressure = std::min(kThreatLeyCap, potency * kThreatLeyGain);
    out.ley = ThreatLey{school_name(winner), potency};
    return out;
}

ThreatReport assess_city_threat(const ThreatRequest& request, const WarCity& city,
                                const WarOutlook& outlook) {
    const std::vector<NestThreat> nests =
        nest_threats(religion_city(city), request.nests, request.ground.globe_radius_m);
    double nest_pressure = 0.0;
    for (const NestThreat& threat : nests) nest_pressure += threat.weight;
    // NO moon: see the declaration.
    const ThreatLeyPressure ley =
        ley_threat_pressure(city_potencies(request.layers, city, std::nullopt),
                            request.dominance_threshold, request.dominance_margin);
    // A city that has already been to war expects the next one. This is why a veteran builds
    // heavier walls: the city planner reads regional_threat as defense_priority.
    const int fought = veteran_wars(city);
    const double war_pressure = std::min(kThreatWarCap, kThreatWarPerWar * fought);

    ThreatReport report;
    report.city_uid = city.uid;
    report.city_name = city.name;
    report.site_id = city.id;
    report.regional_threat = round_dp(std::min(1.0, nest_pressure + ley.pressure + war_pressure), 6);
    report.nest_pressure = round_dp(nest_pressure, 6);
    report.ley_pressure = round_dp(ley.pressure, 6);
    report.war_pressure = round_dp(war_pressure, 6);
    report.wars_fought = fought;
    report.outlook = outlook;

    for (const NestThreat& threat : nests) {
        ThreatContributor c;
        c.kind = threat.kind;
        c.weight = round_dp(threat.weight, 6);
        c.nest_id = threat.nest_id;
        c.name = threat.name;
        c.family = threat.family;
        c.distance_m = round_dp(threat.distance_m, 4);
        c.reach_m = round_dp(threat.reach_m, 4);
        report.contributors.push_back(std::move(c));
    }
    if (ley.ley.has_value()) {
        ThreatContributor c;
        c.kind = "ley";
        c.weight = round_dp(ley.pressure, 6);
        c.school = ley.ley->school;
        c.potency = ley.ley->potency;
        report.contributors.push_back(std::move(c));
    }
    if (war_pressure != 0.0) {
        ThreatContributor c;
        c.kind = "war";
        c.weight = round_dp(war_pressure, 6);
        c.wars_fought = fought;
        report.contributors.push_back(std::move(c));
    }
    report.ley = ley.ley;
    return report;
}

ThreatAssessmentBlock assess_threats(const ThreatRequest& request) {
    // Uid order is the plan passes' order too, so the report and the plans agree. It also
    // fixes the order the outlook's pairs tie-break in.
    WarGround ground = request.ground;
    std::stable_sort(ground.cities.begin(), ground.cities.end(),
                     [](const WarCity& a, const WarCity& b) { return a.uid < b.uid; });
    std::vector<Villain> standing;
    for (const Villain& villain : ground.villains) {
        if (is_standing(villain) && villain.reach_m != 0.0) standing.push_back(villain);
    }
    ground.villains = std::move(standing);

    // A zero spacing is the reference's absent spacing, and it withholds the outlook
    // entirely -- supply pressure included. Unreachable through a resolved config, whose
    // spacing is at least 10 m.
    std::map<std::string, WarOutlook> outlook;
    if (ground.settlement_spacing_m != 0.0) outlook = war_outlook(ground, request.age);

    ThreatAssessmentBlock block;
    block.version = kThreatAssessmentVersion;
    block.evaluated_after = request.evaluated_after;
    block.age = request.age;
    for (const WarCity& city : ground.cities) {
        const auto it = outlook.find(city.uid);
        block.cities.push_back(assess_city_threat(request, city, it != outlook.end() ? it->second : WarOutlook{}));
    }
    block.method = std::string(kThreatMethod);
    block.limits = std::string(kThreatLimits);
    return block;
}

// ---------------------------------------------------------------------------
// The lottery.
// ---------------------------------------------------------------------------

std::vector<CityFateCause> city_fate_causes(const CityFateRequest& request, const WarCity& city,
                                            const std::vector<double>& potencies) {
    std::vector<CityFateCause> causes;

    const ThreatLeyPressure ley =
        ley_threat_pressure(potencies, request.dominance_threshold, request.dominance_margin);
    if (ley.ley.has_value()) {
        // Only the eight known schools have a fall reason. A hidden school's cause keeps its
        // place and weight in the walk, and chosen, it holds the city rather than ruining it
        // (ruled 2026-09-23): the reference's unguarded subscript raised here instead.
        const std::string_view reason = ley_destruction_reason(ley.ley->school);
        CityFateCause cause;
        cause.kind = ley.ley->school;
        cause.weight = ley.pressure;
        cause.reason = std::string(reason);
        cause.corrupts = reason.empty();
        cause.evidence.school = ley.ley->school;
        cause.evidence.potency = ley.ley->potency;
        causes.push_back(std::move(cause));
    }

    // String order on the nest id, from the shared reader -- and the walk is order-sensitive,
    // so this decides WHICH monster is named when several are in reach.
    for (const NestThreat& threat : nest_threats(religion_city(city), request.nests, request.globe_radius_m)) {
        CityFateCause cause;
        cause.kind = threat.kind;
        cause.weight = threat.weight;
        cause.reason = threat.reason;
        cause.evidence.nest_id = threat.nest_id;
        cause.evidence.family = threat.family;
        cause.evidence.distance_m = threat.distance_m;
        cause.evidence.reach_m = threat.reach_m;
        causes.push_back(std::move(cause));
    }

    if (request.magic_enabled) {
        CityFateCause cause;
        cause.kind = "self_magic";
        cause.weight = kFateSelfMagicBase +
                       kFateSelfMagicWeaveGain * potency_at(potencies, magic_school_index("weave"));
        cause.reason = std::string(kSelfMagicReason);
        causes.push_back(std::move(cause));
    }

    // Appended last, like the divine cause: the selector walk is order-sensitive, so a
    // villain may extend the list but must never insert into it.
    if (!request.villains.empty()) {
        for (const VillainCause& villain : villain_causes(request.villains, villain_city(city), request.globe_radius_m)) {
            CityFateCause cause;
            cause.kind = villain.kind;
            cause.weight = villain.weight;
            cause.reason = villain.reason;
            cause.evidence.villain_uid = villain.villain_uid;
            cause.evidence.growth = villain.growth;
            cause.evidence.tier = villain.tier;
            cause.evidence.distance_m = villain.distance_m;
            cause.evidence.reach_m = villain.reach_m;
            cause.evidence.school = villain.school;
            causes.push_back(std::move(cause));
        }
    }
    return causes;
}

std::optional<CityFate> city_fate(const CityFateRequest& request, const WarCity& city,
                                  std::optional<double> roll) {
    const std::vector<double> potencies = city_potencies(request.layers, city, request.lunar);
    const std::vector<CityFateCause> causes = city_fate_causes(request, city, potencies);
    double total = 0.0;
    for (const CityFateCause& cause : causes) total += cause.weight;
    const double chance = std::min(kFateChanceCap, total);

    // Drawn BEFORE survival is decided, and drawn whatever the city rolls: one word per city
    // per age, from a stream no other city shares.
    const double draw = roll.has_value()
                            ? *roll
                            : Rng::child(request.seed, "city-fate-" + city.uid, request.age).random();
    // Tested in this order so a city with no causes never reaches the division below.
    if (draw >= chance || causes.empty()) return std::nullopt;

    double selector = (draw / chance) * total;
    // The last cause is the default because a float shortfall in the walk would otherwise
    // leave nothing chosen at all.
    const CityFateCause* chosen = &causes.back();
    for (const CityFateCause& cause : causes) {
        selector -= cause.weight;
        if (selector <= 0.0) {
            chosen = &cause;
            break;
        }
    }

    RuinSources sources;
    sources.nest_family = chosen->evidence.family;
    sources.villain_school = chosen->evidence.school;

    CityFate fate;
    fate.cause = chosen->kind;
    fate.reason = chosen->reason;
    fate.evidence = chosen->evidence;
    const int school = magic_school_index(chosen->kind);
    if (request.lunar.has_value() && school >= 0 && school < kKnownSchools &&
        !magic_school_table()[static_cast<std::size_t>(school)].hidden) {
        fate.evidence.moon_tide = request.lunar->tide[static_cast<std::size_t>(school)];
    }
    fate.probability = chance;
    fate.roll = draw;
    if (chosen->corrupts) {
        // Held, not ruined: no legacy, no key point.
        fate.corrupts = true;
        return fate;
    }
    // Every ruin seeds a key point: the destroyer's school, else the region's, else the
    // culture's.
    fate.legacy = ruin_legacy(chosen->kind, potencies, city.population_profile, city.city_class, sources,
                              request.dominance_threshold, request.dominance_margin);
    fate.new_node_school = fate.legacy.school;
    return fate;
}

CityFate war_ruin_fate(const CityFateRequest& request, const WarCity& city, const WarFate& fate) {
    RuinSources sources;
    sources.victor_culture = fate.opponent_civilization_id;

    CityFate out;
    out.cause = fate.cause;
    out.reason = fate.reason;
    out.evidence.war_id = fate.war_id;
    out.evidence.kind = fate.kind;
    out.evidence.opponent_uid = fate.opponent_uid;
    out.evidence.opponent_civilization_id = fate.opponent_civilization_id;
    out.evidence.opponent_parent_race_id = fate.opponent_parent_race_id;
    out.evidence.distance_m = fate.distance_m;
    out.evidence.pressure = fate.pressure;
    out.probability = fate.probability;
    out.roll = fate.roll;
    // The SWAYED field, as the lottery sees it: the region fallback reads it when the victor's
    // culture names no school.
    out.legacy = ruin_legacy(fate.cause, city_potencies(request.layers, city, request.lunar),
                             city.population_profile, city.city_class, sources,
                             request.dominance_threshold, request.dominance_margin);
    out.new_node_school = out.legacy.school;
    return out;
}

}  // namespace fwg
