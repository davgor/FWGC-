#include "fwg/villains.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

#include "fwg/religion.hpp"

namespace fwg {
namespace {

// Which pressure raised them decides how their reach grows, and therefore how they are
// fought. The sim owns this field; a hero generator reads it and picks the matching
// escalation of the archetype card a person already carries, so the two agree without
// either package importing the other.
std::string growth_for(std::string_view pressure) {
    if (pressure == "war") return "devouring";
    if (pressure == "nest") return "spreading";
    if (pressure == "ley") return "hoarding";
    return std::string(kDefaultGrowth);
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::map<std::string, const VillainThreatReport*> reports_by_uid(
    const std::vector<VillainThreatReport>& reports) {
    std::map<std::string, const VillainThreatReport*> out;
    for (const VillainThreatReport& report : reports) out[report.city_uid] = &report;
    return out;
}

// The city a villain rises in: the most troubled in its region, ties by ascending uid.
const VillainCity* seat_of(const VillainRegion& region,
                           const std::map<std::string, const VillainThreatReport*>& reports) {
    const VillainCity* best = nullptr;
    double best_turmoil = 0.0;
    for (const VillainCity* city : region.cities) {
        const auto it = reports.find(city->uid);
        // A city the assessment has nothing to say about is as untroubled as a quiet one,
        // which is what the reference's `.get(uid, {})` means.
        const double t = it != reports.end() ? turmoil(*it->second) : 0.0;
        if (best == nullptr || t > best_turmoil || (t == best_turmoil && city->uid < best->uid)) {
            best = city;
            best_turmoil = t;
        }
    }
    return best;
}

// The fallen, ordered as they fell: by fell age then uid. A malformed record with no fell
// age sorts first, which is the reference's -1 default.
void sort_fallen(std::vector<Villain>& fallen) {
    std::stable_sort(fallen.begin(), fallen.end(), [](const Villain& a, const Villain& b) {
        const int left = a.fell_age.value_or(-1);
        const int right = b.fell_age.value_or(-1);
        if (left != right) return left < right;
        return a.uid < b.uid;
    });
}

// What a villain actually did to the world, as the world itself recorded it.
struct MarksLeft {
    std::vector<std::string> ruins;
    std::vector<std::string> claims;
    std::string well;
    std::vector<std::string> held_nodes;
};

MarksLeft marks_left(const std::vector<VillainRuin>& ruins, const Villain& villain) {
    MarksLeft out;
    for (const VillainRuin& ruin : ruins) {
        if (ruin.villain_uid == villain.uid) out.ruins.push_back(ruin.id);
    }
    for (const VillainClaim& claim : villain.claims) out.claims.push_back(claim.id);
    if (villain.well.has_value()) out.well = villain.well->node;
    out.held_nodes = villain.held_nodes;
    std::sort(out.ruins.begin(), out.ruins.end());
    std::sort(out.claims.begin(), out.claims.end());
    std::sort(out.held_nodes.begin(), out.held_nodes.end());
    return out;
}

std::optional<FallenVillainMark> fallen_mark(const std::vector<VillainRuin>& ruins,
                                             const Villain& villain, int age) {
    const MarksLeft left = marks_left(ruins, villain);
    // "If they indeed left a mark" is a real condition, not a formality: held nodes alone
    // are not a mark, because a node is ground the world laid down.
    if (left.ruins.empty() && left.claims.empty() && left.well.empty()) return std::nullopt;

    FallenVillainMark mark;
    mark.id = "fallen-" + villain.uid;
    mark.uid = villain.uid;
    mark.region = villain.region;
    mark.born_age = villain.born_age;
    mark.seat_uid = villain.seat_uid;
    mark.node = villain.node;
    mark.direction = villain.direction;
    mark.school = villain.school;
    mark.growth = villain.growth;
    mark.held_nodes = villain.held_nodes;
    mark.claims = villain.claims;
    mark.well = villain.well;
    mark.god = villain.god;
    mark.log = villain.log;
    mark.fell_age = age;
    // A villain seated at age 0 and fallen at age 3 reigned 3 ages, not 0. The reference
    // spells the default out rather than writing `born or age` for exactly this reason:
    // age 0 is falsy, and the first age is when a villain is most likely to have risen.
    mark.reigned_ages = std::max(0, age - villain.born_age);
    mark.tier_at_fall = round_dp(villain.tier, 9);
    mark.left_ruins = left.ruins;
    mark.left_claims = left.claims;
    mark.left_well = left.well;
    mark.left_held_nodes = left.held_nodes;
    return mark;
}

// The record a region seats or promotes. Shared because the two paths differ only in the
// tier they grant and the word they log, and two copies of this is how they stop agreeing.
Villain seat_villain(const VillainRegion& region,
                     const std::map<std::string, const VillainThreatReport*>& reports, int age,
                     double spacing_m, double tier, double pressed, std::string_view event) {
    std::vector<const VillainThreatReport*> in_region;
    for (const VillainCity* city : region.cities) {
        const auto it = reports.find(city->uid);
        if (it != reports.end()) in_region.push_back(it->second);
    }

    Villain villain;
    villain.uid = "villain-" + std::to_string(age) + "-" + std::to_string(region.seat->node);
    villain.region = region.anchor;
    villain.born_age = age;
    villain.seat_uid = region.seat->uid;
    villain.node = region.seat->node;
    villain.direction = region.seat->direction;
    villain.school = region.school;
    if (!region.node_id.empty()) villain.held_nodes.push_back(region.node_id);
    villain.tier = tier;
    villain.reach_m = tier * kReachSpacingsPerTier * spacing_m;
    villain.growth = growth_for(dominant_pressure(in_region));
    villain.status = "living";
    VillainLogEntry entry;
    entry.age = age;
    entry.event = std::string(event);
    entry.concentration = round_dp(pressed, 6);
    villain.log.push_back(entry);
    return villain;
}

// Whether a claim is a seat or a village: a band on the holder's tier RIGHT NOW, which is
// why the advance pass restamps it and does not freeze it at the age it was staked.
std::string claim_kind(const Villain& villain) {
    return villain.tier >= kSeatTier ? "seat" : "village";
}

// A leyline well, or nothing at all. Nothing when the villain has no school, when its
// school is not in the networks, when it has no god, or when it already has a well: ONE
// WELL PER VILLAIN, EVER.
bool sink_well(Villain& villain, MagicReport& magic, int age) {
    if (villain.school.empty() || villain.god.empty()) return false;
    const auto it = magic.networks.find(villain.school);
    if (it == magic.networks.end()) return false;

    LeyNetwork& net = it->second;
    const std::string well_id = std::string(kWellNodePrefix) + villain.uid;
    for (const LeyNode& node : net.nodes) {
        if (node.id == well_id) return false;
    }

    net.nodes.push_back({well_id, villain.direction, kWellIntensity});
    const int index = static_cast<int>(net.nodes.size()) - 1;
    const std::set<std::string> held(villain.held_nodes.begin(), villain.held_nodes.end());

    std::vector<std::string> joined;
    // Everything but the new well itself, in node-list order -- which fixes the NAME of
    // each edge, because the index in the id is the count of edges made so far. Edges
    // index their endpoints by POSITION, so the node is appended and never inserted.
    for (std::size_t position = 0; position + 1 < net.nodes.size(); ++position) {
        if (held.find(net.nodes[position].id) == held.end()) continue;
        const std::string edge_id = well_id + "-line-" + std::to_string(joined.size());
        // The reference materialises a 33-point great-circle path on the edge. This port
        // does not, because LeyEdge carries no path and `evaluate_networks` rebuilds the
        // arc from the two endpoints every time it measures a field -- the same arc, and
        // one that cannot go stale when a node moves.
        net.edges.push_back({edge_id, index, static_cast<int>(position), kWellEdgeIntensity});
        joined.push_back(edge_id);
    }

    std::set<std::string> nodes(villain.held_nodes.begin(), villain.held_nodes.end());
    nodes.insert(well_id);
    villain.held_nodes.assign(nodes.begin(), nodes.end());

    VillainWell well;
    well.node = well_id;
    well.school = villain.school;
    well.god = villain.god;
    well.lines = joined;
    well.age = age;
    villain.well = well;

    VillainLogEntry entry;
    entry.age = age;
    entry.event = "sank a well";
    entry.school = villain.school;
    entry.lines = static_cast<int>(joined.size());
    villain.log.push_back(entry);
    return true;
}

// Ground a villain has taken for its own, mixed rather than of one people.
//
// Recorded as the villain's claim and NOT injected into the settlement sites: founding
// places by suitability and only inherits identity from a survivor already at that node,
// so a site pushed into the survivor list would simply not be placed.
bool claim_settlements(Villain& villain, const std::vector<VillainCity>& cities, double radius_m,
                       int age) {
    std::vector<std::pair<double, const VillainCity*>> near;
    near.reserve(cities.size());
    for (const VillainCity& city : cities) {
        near.emplace_back(great_circle_m(radius_m, villain.direction, city.direction), &city);
    }
    std::stable_sort(near.begin(), near.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second->uid < b.second->uid;
    });
    if (near.empty()) return false;

    const std::size_t take = std::min<std::size_t>(kMixedFactions, near.size());
    VillainClaim claim;
    claim.id = "claim-" + villain.uid;
    claim.villain_uid = villain.uid;
    claim.age = age;
    claim.direction = villain.direction;
    claim.node = villain.node;
    claim.kind = claim_kind(villain);
    claim.refreshed_age = age;
    // Mixed by construction: a villain takes whoever is close, not whoever shares its
    // blood, which is what makes its holdings look wrong to everyone.
    std::set<std::string> factions;
    for (std::size_t i = 0; i < take; ++i) {
        factions.insert(near[i].second->population_profile);
        claim.drawn_from.push_back(near[i].second->uid);
    }
    claim.factions.assign(factions.begin(), factions.end());
    if (villain.well.has_value()) claim.well = villain.well->node;
    claim.holder_status = "living";
    claim.influence = 1.0;

    for (VillainClaim& held : villain.claims) {
        if (held.id != claim.id) continue;
        // One constant id per villain, so from the second age onward the whole claim used
        // to be recomputed and dropped on the floor. ONLY the standing half is refreshed:
        // a tier band is not a thing any reading of this record makes correct to freeze,
        // while `factions` and `drawn_from` record what was taken and when.
        held.kind = claim.kind;
        held.refreshed_age = age;
        return true;
    }
    villain.claims.push_back(claim);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Shared arithmetic.
// ---------------------------------------------------------------------------

double round_dp(double value, int places) {
    if (!std::isfinite(value)) return value;
    const double scale = std::pow(10.0, places);
    return std::round(value * scale) / scale;
}

Vec3 offset_direction(const Vec3& up, double angle, double bearing) {
    const Vec3 reference = std::abs(up.y) > 0.999 ? Vec3{1.0, 0.0, 0.0} : Vec3{0.0, 1.0, 0.0};
    const Vec3 north = unit(reference - dot(reference, up) * up);
    // The cross in THIS order: north x up, not up x north. Reversing it mirrors every
    // bearing, which turns a ring clockwise instead of anticlockwise and moves every
    // corrupted node.
    const Vec3 east = unit(cross(north, up));
    const Vec3 tangent = std::cos(bearing) * east + std::sin(bearing) * north;
    return unit(std::cos(angle) * up + std::sin(angle) * tangent);
}

// ---------------------------------------------------------------------------
// The pure helpers.
// ---------------------------------------------------------------------------

bool is_standing(const Villain& villain) {
    return villain.status.empty() || villain.status == "living";
}

std::vector<Villain> standing_villains(const std::vector<Villain>& people) {
    std::vector<Villain> out;
    for (const Villain& villain : people) {
        if (is_standing(villain)) out.push_back(villain);
    }
    return out;
}

double turmoil(const VillainThreatReport& report) {
    const double total =
        report.regional_threat + kRiskWeight * report.war_risk + kHungerWeight * report.war_hunger;
    return std::max(0.0, std::min(1.0, total));
}

std::string dominant_pressure(const std::vector<const VillainThreatReport*>& reports) {
    if (reports.empty()) return {};
    double ley = 0.0;
    double nest = 0.0;
    double war = 0.0;
    for (const VillainThreatReport* report : reports) {
        ley += report->ley_pressure;
        nest += report->nest_pressure;
        war += report->war_pressure;
    }
    if (ley == 0.0 && nest == 0.0 && war == 0.0) return {};
    // Walked in ALPHABETICAL order with a strict `>`, so an exact tie resolves to the
    // earlier name: ley beats nest beats war. The reference gets this from
    // `max(sorted(totals), key=totals.get)` keeping the first maximum.
    const std::pair<std::string_view, double> totals[3] = {{"ley", ley}, {"nest", nest}, {"war", war}};
    std::string_view best = totals[0].first;
    double best_total = totals[0].second;
    for (const auto& entry : totals) {
        if (entry.second > best_total) {
            best = entry.first;
            best_total = entry.second;
        }
    }
    return std::string(best);
}

int villain_ceiling(int region_count, double density) {
    if (region_count <= 0) return 0;
    return std::max(1, static_cast<int>(std::ceil(region_count / std::max(density, 1.0))));
}

double claim_influence(int ages_since_fall) {
    // A guard against a fell age ahead of the current one, not a decay value.
    if (ages_since_fall < 0) return 1.0;
    const double value = kFallenClaimInfluence * std::pow(kClaimDecayPerAge, ages_since_fall);
    return value < kClaimInfluenceFloor ? 0.0 : round_dp(value, 9);
}

std::string school_god(std::string_view school) {
    if (school.empty()) return {};
    for (const PantheonGod& god : pantheon_gods()) {
        if (god.family != "school") continue;
        for (const std::string_view affinity : god.school_affinity) {
            if (affinity == school) return std::string(god.id);
        }
    }
    return {};
}

std::pair<std::string, std::string> held_node(const MagicReport* magic, const Vec3& seat,
                                              double radius_m) {
    if (magic == nullptr) return {};
    bool found = false;
    double best = 0.0;
    std::string school;
    std::string node_id;
    // std::map is already school-name ascending, which is the reference's
    // `sorted(networks.items())`. Within a network the node list order decides, and the
    // strict `<` keeps the first of two equidistant nodes.
    for (const auto& [name, net] : magic->networks) {
        for (const LeyNode& node : net.nodes) {
            if (starts_with(node.id, kWellNodePrefix)) continue;
            const double distance = great_circle_m(radius_m, seat, node.direction);
            if (!found || distance < best) {
                found = true;
                best = distance;
                school = name;
                node_id = node.id;
            }
        }
    }
    if (!found) return {};
    return {school, node_id};
}

double concentration(const VillainRegion& region,
                     const std::map<std::string, const VillainThreatReport*>& reports) {
    double total = 0.0;
    int counted = 0;
    for (const VillainCity* city : region.cities) {
        const auto it = reports.find(city->uid);
        if (it == reports.end()) continue;
        total += turmoil(*it->second);
        ++counted;
    }
    return counted > 0 ? total / counted : 0.0;
}

std::vector<VillainRegion> villain_regions(const VillainRequest& request) {
    std::map<std::string, const VillainCity*> by_uid;
    std::map<int, const VillainCity*> by_site;
    for (const VillainCity& city : request.cities) {
        if (!city.uid.empty()) by_uid[city.uid] = &city;
        by_site[city.id] = &city;
    }

    // A STRING sort of the culture id, not of the integer index. Ids look like
    // `human_heartland-672bcb6c`, so this is lexicographic over profile then hex.
    std::vector<const VillainCulture*> ordered;
    ordered.reserve(request.cultures.size());
    for (const VillainCulture& culture : request.cultures) ordered.push_back(&culture);
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const VillainCulture* a, const VillainCulture* b) { return a->id < b->id; });

    std::vector<VillainRegion> groups;
    for (const VillainCulture* culture : ordered) {
        VillainRegion group;
        group.id = culture->id;
        for (const int id : culture->city_ids) {
            const auto it = by_site.find(id);
            if (it != by_site.end()) group.cities.push_back(it->second);
        }
        if (!group.cities.empty()) groups.push_back(std::move(group));
    }
    // No cultures at all is still a world with cities in it, and one region is a truer
    // reading of that than none.
    if (groups.empty() && !by_uid.empty()) {
        VillainRegion world;
        world.id = "world";
        for (const auto& [uid, city] : by_uid) world.cities.push_back(city);
        groups.push_back(std::move(world));
    }

    const auto reports = reports_by_uid(request.reports);
    std::vector<VillainRegion> out;
    for (VillainRegion& group : groups) {
        const VillainCity* seat = seat_of(group, reports);
        if (seat == nullptr) continue;
        const auto [school, node_id] = held_node(request.magic, seat->direction, request.globe_radius_m);
        group.seat = seat;
        group.school = school;
        group.node_id = node_id;
        // Where there is no magic at all the anchor falls back to the seat city's uid,
        // which survivors also carry forward.
        group.anchor = !node_id.empty() ? node_id : "seat-" + seat->uid;
        out.push_back(group);
    }

    // Two regions can share a nearest node; the troubled one keeps it, so an anchor names
    // one region and a tier cannot be double-counted into it. STRICT `>`, so on an exact
    // concentration tie the earlier group in culture-id order keeps the anchor.
    std::map<std::string, VillainRegion> best;
    for (VillainRegion& group : out) {
        const auto it = best.find(group.anchor);
        if (it == best.end() || concentration(group, reports) > concentration(it->second, reports)) {
            best[group.anchor] = group;
        }
    }
    std::vector<VillainRegion> ranked;
    ranked.reserve(best.size());
    for (auto& [anchor, group] : best) ranked.push_back(std::move(group));
    return ranked;
}

// ---------------------------------------------------------------------------
// The passes.
// ---------------------------------------------------------------------------

VillainAdvance advance_villains(const VillainRequest& request) {
    VillainAdvance result;
    // Zero raises none, and a world that has never had one carries no block at all: the
    // option is invisible in the output, not merely inert in it. A world that WAS built
    // with villains keeps its record even if the rate is later turned down.
    if (request.rise <= 0.0 && !request.villains.has_value()) return result;

    VillainBlock block = request.villains.value_or(VillainBlock{});
    std::map<std::string, double> tiers = block.tiers;

    // Only the standing are candidates to hold, fall or be counted against the ceiling.
    // Without this filter a fallen villain is reloaded as living and rises from the dead.
    std::map<std::string, Villain> living;
    std::vector<Villain> fallen;
    for (const Villain& villain : block.people) {
        if (is_standing(villain)) {
            living[villain.region] = villain;
        } else {
            fallen.push_back(villain);
        }
    }

    // The dead fade. Restamped here rather than computed by each consumer, because the
    // claim is what the leaf packages read and they cannot import this module to ask.
    for (Villain& old : fallen) {
        if (!old.fell_age.has_value()) continue;
        for (VillainClaim& claim : old.claims) {
            claim.influence = claim_influence(request.age - *old.fell_age);
        }
    }

    const auto reports = reports_by_uid(request.reports);
    const std::vector<VillainRegion> region_list = villain_regions(request);
    std::map<std::string, const VillainRegion*> by_anchor;
    for (const VillainRegion& region : region_list) by_anchor[region.anchor] = &region;
    const int limit = villain_ceiling(static_cast<int>(region_list.size()), request.density);

    int seated_count = 0;
    for (const auto& [anchor, villain] : living) {
        if (villain.tier >= kSuperTier) ++seated_count;
    }

    // A villain whose anchor is not produced this age was never the subject of this loop:
    // its tier was never touched, the fall check never ran, and it went straight back into
    // `people` as standing, holding a frozen reach over a region the world no longer has.
    // Two ordinary routes get there -- a seat city taken by a fate, and a ley node appended
    // nearer the seat, which moves the anchor while the region carries on. It is visited
    // with the concentration a region nobody produced presses, which is none.
    //
    // Deliberately NOT an immediate fall: unseating a villain the moment a node is appended
    // would be a rule about ley geometry masquerading as a rule about power.
    //
    // Visited AFTER the produced regions, so which regions seat a villain this age is
    // unchanged: a stranded reign ending must not free a seat in the age it ends.
    std::vector<std::string> order;
    order.reserve(region_list.size() + living.size());
    for (const VillainRegion& region : region_list) order.push_back(region.anchor);
    for (const auto& [anchor, villain] : living) {
        if (by_anchor.find(anchor) == by_anchor.end()) order.push_back(anchor);
    }

    std::vector<FallenVillainMark> marks;
    for (const std::string& rid : order) {
        const auto region_it = by_anchor.find(rid);
        const VillainRegion* region = region_it != by_anchor.end() ? region_it->second : nullptr;
        const double pressed = region != nullptr ? concentration(*region, reports) : 0.0;
        const auto tier_it = tiers.find(rid);
        double tier = (tier_it != tiers.end() ? tier_it->second : 0.0) * kTierKeptPerAge +
                      request.rise * pressed;
        const auto villain_it = living.find(rid);
        const bool held = villain_it != living.end();

        if (tier >= kSuperTier && !held && seated_count >= limit) {
            // Pretenders stall at the line: the ceiling stops a rise, not an existence.
            // Clamping the LEDGER and not merely the local is the point -- a blocked region
            // that banked tier would burst through the moment a slot opened.
            tier = std::min(tier, kStalledTier);
        }
        tiers[rid] = tier;

        if (!held) {
            // A null region implies a stranded anchor, which by construction already holds
            // a villain, so this never seats one into a region that was not produced.
            if (tier >= kSuperTier && region != nullptr) {
                living[rid] = seat_villain(*region, reports, request.age,
                                           request.settlement_spacing_m, tier, pressed, "rose");
                ++seated_count;
            }
            continue;
        }

        Villain& villain = villain_it->second;
        villain.tier = tier;
        // Refreshed BEFORE the fall test, so a fallen villain carries a live-looking reach
        // and every consumer that means "who stands now" has to say so.
        villain.reach_m = tier * kReachSpacingsPerTier * request.settlement_spacing_m;
        if (tier >= request.hold) continue;

        villain.status = "fallen";
        villain.fell_age = request.age;
        VillainLogEntry entry;
        entry.age = request.age;
        entry.event = "fell";
        entry.concentration = round_dp(pressed, 6);
        villain.log.push_back(entry);
        // Overrides the ledger line above: what a villain that left no mark leaves behind
        // is the decayed tier its region keeps, which is the successor squabble.
        tiers[rid] = tier * kFragmentShare;
        for (VillainClaim& claim : villain.claims) {
            claim.holder_status = "fallen";
            claim.holder_fell_age = request.age;
            claim.influence = claim_influence(0);
        }
        // Taken BEFORE the roster entry leaves the standing set, so it reads the villain as
        // it was at the end of its reign rather than after.
        std::optional<FallenVillainMark> mark = fallen_mark(request.ruins, villain, request.age);
        if (mark.has_value()) marks.push_back(*std::move(mark));
        fallen.push_back(std::move(villain));
        living.erase(villain_it);
        --seated_count;
    }

    block.version = kVillainsVersion;
    block.tiers.clear();
    for (const auto& [anchor, tier] : tiers) block.tiers[anchor] = round_dp(tier, 9);
    std::stable_sort(marks.begin(), marks.end(),
                     [](const FallenVillainMark& a, const FallenVillainMark& b) { return a.id < b.id; });
    for (FallenVillainMark& mark : marks) block.fallen.push_back(std::move(mark));

    sort_fallen(fallen);
    block.people.clear();
    for (auto& [anchor, villain] : living) block.people.push_back(std::move(villain));
    for (Villain& villain : fallen) block.people.push_back(std::move(villain));

    result.standing = standing_villains(block.people);
    result.block = std::move(block);
    return result;
}

std::vector<VillainCause> villain_causes(const std::vector<Villain>& cast, const VillainCity& city,
                                         double radius_m) {
    std::vector<VillainCause> out;
    for (const Villain& villain : cast) {
        if (!is_standing(villain)) continue;
        // The tier test also excludes the fallen, because a villain falls under `hold` and
        // `hold` is under kSuperTier -- but that is a coupling between two constants, not a
        // rule. A fallen villain reaching this lottery would move every world, so the rule
        // is stated rather than relied upon.
        if (villain.tier < kSuperTier || villain.reach_m == 0.0) continue;
        const double distance = great_circle_m(radius_m, city.direction, villain.direction);
        const double spent = kVillainFalloff * villain.reach_m;
        // A city exactly at the spent distance is excluded, and THE SEAT ITSELF IS IMMUNE.
        if (distance >= spent || city.uid == villain.seat_uid) continue;
        const double weight = kVillainWeight * villain.tier * (1.0 - distance / spent);
        if (weight <= 0.0) continue;

        VillainCause cause;
        cause.kind = "villain_" + villain.growth;
        cause.weight = weight;
        cause.reason = "A power seated at " + villain.seat_uid +
                       " reached this far and the city did not stand.";
        cause.villain_uid = villain.uid;
        cause.growth = villain.growth;
        cause.tier = round_dp(villain.tier, 6);
        cause.distance_m = round_dp(distance, 3);
        cause.reach_m = round_dp(villain.reach_m, 3);
        cause.school = villain.school;
        out.push_back(std::move(cause));
    }
    return out;
}

VillainOutlook villain_outlook(const VillainRequest& request) {
    const auto reports = reports_by_uid(request.reports);
    std::map<std::string, double> tiers;
    std::set<std::string> seated;
    if (request.villains.has_value()) {
        tiers = request.villains->tiers;
        for (const Villain& villain : request.villains->people) {
            if (is_standing(villain)) seated.insert(villain.region);
        }
    }

    const std::vector<VillainRegion> region_list = villain_regions(request);
    VillainOutlook out;
    out.rise = request.rise;
    out.ceiling = villain_ceiling(static_cast<int>(region_list.size()), request.density);
    out.standing = static_cast<int>(seated.size());

    std::set<std::string> produced;
    for (const VillainRegion& region : region_list) {
        produced.insert(region.anchor);
        const auto it = tiers.find(region.anchor);
        const double tier = it != tiers.end() ? it->second : 0.0;
        VillainOutlookRow row;
        row.region = region.anchor;
        row.culture = region.id;
        row.has_culture = true;
        row.concentration = round_dp(concentration(region, reports), 6);
        row.tier = round_dp(tier, 6);
        row.to_threshold = round_dp(std::max(0.0, kSuperTier - tier), 6);
        row.seated = seated.count(region.anchor) > 0;
        row.stalled = row.seated ? false : tier >= kStalledTier;
        row.cities = static_cast<int>(region.cities.size());
        out.regions.push_back(std::move(row));
    }

    // A villain standing on an anchor no region was produced for made one document give two
    // answers -- `standing: 1` beside zero regions seated -- with no way for a reader to
    // tell which was right. The holding gets a row instead of being counted and hidden.
    for (const std::string& rid : seated) {
        if (produced.count(rid) > 0) continue;
        const auto it = tiers.find(rid);
        const double tier = it != tiers.end() ? it->second : 0.0;
        VillainOutlookRow row;
        row.region = rid;
        row.has_culture = false;
        row.concentration = 0.0;
        row.tier = round_dp(tier, 6);
        row.to_threshold = round_dp(std::max(0.0, kSuperTier - tier), 6);
        row.seated = true;
        row.stalled = false;
        row.cities = 0;
        out.regions.push_back(std::move(row));
    }
    return out;
}

VillainPromotion promote_villains(const VillainRequest& request) {
    VillainPromotion result;
    // A world that never raised anybody carries no block, and the end of the simulation
    // must not invent one.
    if (!request.villains.has_value()) return result;

    VillainBlock block = *request.villains;
    std::map<std::string, double> tiers = block.tiers;
    std::map<std::string, Villain> living;
    for (const Villain& villain : block.people) {
        if (is_standing(villain)) living[villain.region] = villain;
    }

    const auto reports = reports_by_uid(request.reports);
    const std::vector<VillainRegion> region_list = villain_regions(request);
    const int limit = villain_ceiling(static_cast<int>(region_list.size()), request.density);
    int seated = 0;
    for (const auto& [anchor, villain] : living) {
        if (villain.tier >= kSuperTier) ++seated;
    }
    const int room = limit - seated;
    if (room <= 0) return result;

    // Most concentrated first, ties by anchor so the order is a property of the world and
    // not of container iteration.
    std::vector<const VillainRegion*> ranked;
    for (const VillainRegion& region : region_list) {
        if (living.find(region.anchor) == living.end()) ranked.push_back(&region);
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [&reports](const VillainRegion* a, const VillainRegion* b) {
                         const double left = concentration(*a, reports);
                         const double right = concentration(*b, reports);
                         if (left != right) return left > right;
                         return a->anchor < b->anchor;
                     });

    const std::size_t take = std::min<std::size_t>(static_cast<std::size_t>(room), ranked.size());
    for (std::size_t i = 0; i < take; ++i) {
        const VillainRegion& region = *ranked[i];
        Villain villain = seat_villain(region, reports, request.age, request.settlement_spacing_m,
                                       kSuperTier, concentration(region, reports), "promoted");
        tiers[region.anchor] = kSuperTier;
        living[region.anchor] = villain;
        result.promoted.push_back(std::move(villain));
    }
    // Nothing written at all when nobody was promoted: no version bump, no tiers rewrite.
    // Idempotency depends on it.
    if (result.promoted.empty()) return result;

    block.version = kVillainsVersion;
    block.tiers.clear();
    for (const auto& [anchor, tier] : tiers) block.tiers[anchor] = round_dp(tier, 9);

    std::vector<Villain> fallen;
    for (const Villain& villain : block.people) {
        if (!is_standing(villain)) fallen.push_back(villain);
    }
    sort_fallen(fallen);
    block.people.clear();
    for (auto& [anchor, villain] : living) block.people.push_back(std::move(villain));
    for (Villain& villain : fallen) block.people.push_back(std::move(villain));
    // NOTE the two things promotion deliberately does not do: it does not decay the dead's
    // claims and it does not append to `fallen`. Nobody fell here.
    result.block = std::move(block);
    return result;
}

VillainWorks build_villain_works(const VillainRequest& request) {
    VillainWorks result;
    if (request.villains.has_value()) result.block = *request.villains;
    if (request.magic != nullptr) result.magic = *request.magic;

    for (Villain& villain : result.block.people) {
        if (!is_standing(villain)) continue;
        if (villain.tier < kSuperTier) continue;
        // Bound to an old god by the ground it holds, if no new one has taken it. Sinking a
        // well is what binds a villain to its school's god, which is also why being sworn
        // to one is no disqualification for corruption later.
        if (villain.god.empty() && !villain.school.empty()) villain.god = school_god(villain.school);
        const bool well = sink_well(villain, result.magic, request.age);
        const bool claim =
            claim_settlements(villain, request.cities, request.globe_radius_m, request.age);
        if (well || claim) result.works.push_back({villain.uid, well, claim});
    }
    // Only written when there IS work. An age in which nobody built anything leaves the
    // previous age's record alone rather than replacing it with an empty list.
    if (!result.works.empty()) result.block.works = result.works;
    return result;
}

}  // namespace fwg
