#include "fwg/nests.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "fwg/random.hpp"
#include "fwg/scale.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

// Slack on the latitude bound in nearest_settled, in radians. The bound is exact in real
// arithmetic; this absorbs the rounding in the two asin calls that stand in for the
// latitudes. It is ADDED, never subtracted, so the walk goes one candidate too far
// rather than one too few: a cell whose nearest settled thing is due north or south has
// a latitude gap exactly equal to its angle, and subtracting the slack stops the walk on
// that tie and loses the argmax.
constexpr double kLatitudeSlack = 1e-9;

// Freshwater against a saline lake. A lake at or above this salinity is a salt lake,
// which no profile can declare and nothing lives in.
constexpr double kSaltLakeSalinity = 0.2;

// The minimum `coast` a shore species needs. Shore is a LAND medium: the species is
// matched against a land cell and then gated on this, so it never reaches water.
constexpr double kShoreCoastMin = 0.1;

// The temperature a cell reports when the world carries no temperature layer. Above
// every authored upper bound in the catalogue, so a world without the layer places
// nothing rather than placing everything.
constexpr double kMissingTemperatureC = 100.0;

// ...and the one the `cold` derivation uses for the same missing layer. It is NOT the
// same number, and that is the source's own asymmetry: cold reads 10 and comes out 0,
// while the habitability gate reads 100 and fails closed.
constexpr double kMissingColdTemperatureC = 10.0;

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

double great_circle(const Vec3& a, const Vec3& b, double radius) {
    return radius * std::acos(clamp_unit(dot(a, b)));
}

// The seven habitat fields the pass derives rather than reading out of a layer. They are
// written AFTER the layers, so a derived name wins over a layer of the same name -- which
// is how `coast` means maritime and `depth` means water depth however the layer set grows.
constexpr std::string_view kDerivedFields[] = {"depth",   "dryness", "coast", "mountain",
                                               "plains",  "wetland", "cold"};

enum class Derived : int { Depth = 0, Dryness, Coast, Mountain, Plains, Wetland, Cold, Count };

// A field either names a layer or is one of the seven above. A field naming a layer this
// world does not carry is neither, and reads 0.0 -- so a `requires` on it fails closed
// and a `weights` term on it contributes nothing, rather than erroring.
struct FieldBinding {
    const Grid* layer = nullptr;
    int derived = -1;
};

const Grid* find_layer(const World& world, std::string_view name) {
    const auto it = world.layers.find(std::string(name));
    return it == world.layers.end() ? nullptr : &it->second;
}

double layer_at(const Grid* layer, int x, int z) { return layer == nullptr ? 0.0 : layer->at(x, z); }

// Which cell medium a profile's declared medium demands. Shore asks for land and then
// wants a coast on top; nothing asks for a salt lake.
CellMedium demanded_medium(CreatureMedium medium) {
    switch (medium) {
        case CreatureMedium::Marine: return CellMedium::Marine;
        case CreatureMedium::Freshwater: return CellMedium::Freshwater;
        case CreatureMedium::Land:
        case CreatureMedium::Shore: break;
    }
    return CellMedium::Land;
}

// What each pass differs in. Seven things, and nothing else: the two passes never read
// each other and share every line of the placement itself.
struct PassSpec {
    std::string_view prefix;  // the site id prefix -- "animal" and "nest", not "monster"
    std::string_view domain;  // the rng stream
    CreatureClass which;
    double density = 0.0;
    double falloff = 0.0;
    // Game keeps its distance from people in proportion to how dangerous it is. Animals
    // divide the base clearance by three; a monster takes it whole.
    double clearance_divisor = 1.0;
    // The company a candidate keeps its distance from. An animal competes only with its
    // own species; a monster only with its own TIER -- an equal lair, never a greater
    // one, which is what "a dragon's range may contain a goblin camp" means.
    bool bucket_is_tier = false;
};

// One candidate species on one kind of ground, with the two gates that read nothing but
// the ground already answered.
struct Candidate {
    const CreatureProfile* profile = nullptr;
    int index = 0;
    double biome_weight = 0.0;
    double clearance_m = 0.0;
};

// A species' claim on one cell: its room before the budget, then its rate after it.
struct Share {
    const CreatureProfile* profile = nullptr;
    int index = 0;
    double rate = 0.0;
    double score = 0.0;
    bool wet = false;
};

// The species that claims this group, in proportion to its share of the cell.
const Share& pick(Rng& rng, const Share* begin, const Share* end, double total) {
    const double draw = rng.random() * total;
    double cursor = 0.0;
    for (const Share* s = begin; s != end; ++s) {
        cursor += s->rate;
        // Float error can leave the draw past the whole list; the last share takes it.
        if (draw <= cursor) return *s;
    }
    return *(end - 1);
}

// Whether this bucket already holds ground against a candidate in this cell.
//
// Exactly `any(distance(a, cell) < max(reach, a.range_m))` over everything the bucket has
// placed, and every comparison below IS that predicate on a real pair. What changes is
// which pairs are worth forming.
//
// SAME CELL FIRST. A site sits at its cell's centre, so a second site in the same cell is
// at distance zero and refused by any positive reach -- and same-cell is where the
// refusals are, about seven in ten of them. The test is still the real predicate rather
// than an assertion that reach is positive.
//
// THEN THE ROWS `reach` CAN CROSS. `direction` puts grid row z at latitude
// pi/2 - pi*z/(n-1), and the angle between two unit vectors is at least the difference of
// their latitudes, so two sites r rows apart are at least r*pitch apart on the sphere
// whatever their longitudes do. A blocker has to be nearer than max(reach, range_m), so it
// has to be inside that many rows. One row is added to the bound because it is exact in
// real arithmetic and a raster row is cheaper than an argument about the last bit of a
// divide. The band is taken over the bucket's WIDEST territory, not the candidate's own,
// or a narrow-ranged species walks straight through a wide incumbent's ground.
//
// This is NOT the claim that only the same cell can ever block, which the source used to
// make and which is false: the grid is lat/lon, the rings beside the poles are packed a
// hundred times tighter than the equator, and a cross-cell refusal is ordinary ground
// there. The band does not wrap across a pole and does not need to -- the latitude bound
// is a true lower bound in every case, and two cells in the same polar ring share a row.
bool blocked(const std::vector<NestSite>& placed,
             const std::unordered_map<int, std::vector<int>>& at_node,
             const std::unordered_map<int, std::vector<int>>& on_row, double widest,
             const HabitatCell& cell, double reach, double radius, double pitch, int n) {
    const Vec3 facing = cell.direction;
    const auto here = at_node.find(cell.node);
    if (here != at_node.end()) {
        for (const int i : here->second) {
            const NestSite& a = placed[static_cast<std::size_t>(i)];
            if (great_circle(a.direction, facing, radius) < std::max(reach, a.range_m)) return true;
        }
    }
    if (on_row.empty()) return false;
    const double span = std::max(reach, widest) / pitch;
    // The band is a row count, so it is bounded by the raster whatever the reach is; a
    // territory wider than the world would otherwise spin over an arbitrary integer range
    // to find nothing, because a row outside the grid holds no sites either way.
    const int band = span >= static_cast<double>(n) ? n : static_cast<int>(span) + 1;
    const int low = std::max(0, cell.z - band);
    const int high = std::min(n - 1, cell.z + band);
    for (int row = low; row <= high; ++row) {
        const auto it = on_row.find(row);
        if (it == on_row.end()) continue;
        for (const int i : it->second) {
            const NestSite& a = placed[static_cast<std::size_t>(i)];
            if (great_circle(a.direction, facing, radius) < std::max(reach, a.range_m)) return true;
        }
    }
    return false;
}

NestReason reach_reason(const CreatureProfile& profile, long long cells_reached,
                        long long placed_count, bool cleared_out) {
    if (placed_count != 0) return NestReason::Placed;
    if (profile.never_satisfiable) return NestReason::NeverSatisfiable;
    if (cells_reached != 0) return NestReason::LostTheDraw;
    return cleared_out ? NestReason::ClearedOut : NestReason::AbsentFromThisWorld;
}

constexpr std::string_view kWildlifeMethod =
    "Thinned point process over habitable ground: density falls geometrically with danger "
    "tier, so prey are common and apex animals rare. Hunting grounds are a centre and a "
    "radius; only a species competes with itself. `saturation` reports, per tier, the "
    "raster cells the tier can reach, the groups the rate drew, what was placed, what "
    "territory refused, and the mean distance from settled ground of the rate and of the "
    "placed set. `diagnostics` reports, per species, the cells it could live in and why it "
    "is not here; `reachability` counts the catalogue against what this world holds.";

constexpr std::string_view kWildlifeLimits =
    "Static habitat proposals. No prey carrying capacity, herd sizes, migrations or runtime "
    "spawning; a range is where an animal may be met, not a simulated population. Placement "
    "resolves at the raster cell: every site in a cell carries the cell centre, so the "
    "spacing rule can keep only one site per species per cell and a species whose rate "
    "exceeds the cells it reaches is pinned there rather than growing with the ground. The "
    "pyramid is exact in `saturation.drawn` and clipped in `sites`, and the danger gradient "
    "is exact in `saturation.rate_distance_m` and clipped the same way. "
    "`reachability.eligible` and `.placed` are this world only and move with the seed; only "
    "`never_satisfiable` holds for every world.";

constexpr std::string_view kBeastNestsMethod =
    "Thinned point process keyed to magic and terrain, independent of the animals. Density "
    "falls geometrically with danger tier and territory grows with it; a lair is refused "
    "only by an equal lair, so a greater monster tolerates a lesser inside its territory and "
    "hunting grounds overlap. `saturation` reports, per tier, the raster cells the tier can "
    "reach, the lairs the rate drew, what was placed, what territory refused, and the mean "
    "distance from settled ground of the rate and of the placed set. `diagnostics` reports, "
    "per species, the cells it could live in and why it is not here; `reachability` counts "
    "the catalogue against what this world holds.";

// The clause "no species' territory is as wide as the raster pitch" is FALSE and is the
// exact claim the source's own board card exists to retract; see `blocked`. It survives
// here because this string is published data that consumers key on, not a rule anyone
// implements. Port it verbatim; do not implement it.
constexpr std::string_view kBeastNestsLimits =
    "Static habitat proposals. No hostility, population, patrol or runtime spawning; "
    "territory is a claim on ground, not a simulated creature. Placement resolves at the "
    "raster cell: every lair in a cell carries the cell centre and no species' territory is "
    "as wide as the raster pitch, so a tier holds at most one lair per cell and a tier whose "
    "rate exceeds the cells it reaches is pinned at that ceiling however much ground the "
    "world gains. The pyramid is exact in `saturation.drawn` and clipped in `sites`, and the "
    "danger gradient is exact in `saturation.rate_distance_m` and clipped the same way: "
    "danger ramps outward from people in the rate and the placed medians barely move. "
    "`reachability.eligible` and `.placed` are this world only and move with the seed; only "
    "`never_satisfiable` holds for every world. `saturation` reports what this pass "
    "produced: a divine visitation purges lairs from `sites` afterwards, so the two stop "
    "agreeing once one has run.";

// ---------------------------------------------------------------------------
// One thinned point process over the cells, shared by both passes.
// ---------------------------------------------------------------------------
NestBlock place(const World& world, const NestHabitat& ground_field,
                const std::vector<Vec3>& settled, const PassSpec& spec,
                const std::vector<NestSite>& held) {
    const Config& cfg = world.effective;
    const WorldOptions& o = cfg.options;
    const double radius = cfg.globe_radius;
    const int n = cfg.size;
    const std::vector<HabitatCell>& cells = ground_field.cells;
    const std::size_t count = cells.size();

    const std::span<const CreatureProfile> catalogue = creature_catalogue(spec.which);
    const std::size_t species_count = catalogue.size();

    Rng rng = Rng::child(cfg.seed, spec.domain, static_cast<std::int64_t>(o[Option::nest_variation]));

    const double floor_score = o[Option::nest_min_suitability];
    const double nest_spacing = o[Option::nest_spacing];
    const double base_clearance = o[Option::nest_settlement_clearance];
    const long long per_species = static_cast<long long>(o.as_int(Option::nest_per_species));
    const long long limit = static_cast<long long>(o.as_int(Option::nest_limit));

    const auto clearance_of = [&](int tier) {
        return base_clearance * tier / spec.clearance_divisor;
    };

    // Distance to the nearest settled thing, once per cell. Keeping clear of people is a
    // property of the ground rather than of luck, so it belongs in the rate: reject a draw
    // that already happened and the world quietly holds far fewer creatures than the
    // density asked for.
    const std::vector<double> nearest = nearest_settled(cells, settled, radius);

    // ...and how far out a cell is decides how dangerous the ground is, continuously. The
    // clearance still refuses the doorstep outright; this is the ramp between the doorstep
    // and the wilderness. Authored against the reference world, so it means the same thing
    // at every width -- an absolute metre constant is sub-cell on a 200 km world.
    const double span = kNestDangerSpanM * scale::reach_scale(2.0 * kPi * radius);

    // Two of the four gates read nothing about a cell but its medium and its biome, and a
    // world holds about a dozen distinct pairs of those against tens of thousands of cells.
    // So the pair decides the candidate list once and every cell holding it reuses that
    // list, with the biome weight already resolved.
    //
    // Order inside a list is CATALOGUE ORDER, unchanged. It has to be: the cell total is a
    // float sum over the list and the pick walks it cumulatively, so a reordering is a
    // different world even when it is the same set.
    //
    // Nothing is hoisted out of `suitability`, which still re-tests the medium it was
    // selected on. Keeping that call whole is worth more than the duplicated comparison: it
    // stays the single published answer to "can this species live here".
    constexpr std::size_t kGroundKinds = 4;
    std::vector<std::vector<Candidate>> by_ground(kGroundKinds * kNestBiomeCount);
    std::vector<char> ground_built(kGroundKinds * kNestBiomeCount, 0);
    // Ground nothing can use, for a biome id outside the roster. Every table excludes such
    // an id already, so the row would come out empty anyway -- this only keeps a world that
    // grew an eighteenth biome from indexing past the memo instead of quietly holding
    // nothing, which is what it should hold.
    static const std::vector<Candidate> kNoCandidates;
    const auto ground_candidates = [&](CellMedium medium, int biome) -> const std::vector<Candidate>& {
        if (biome < 0 || biome >= kNestBiomeCount) return kNoCandidates;
        const std::size_t key =
            static_cast<std::size_t>(medium) * kNestBiomeCount + static_cast<std::size_t>(biome);
        if (!ground_built[key]) {
            std::vector<Candidate>& row = by_ground[key];
            for (std::size_t i = 0; i < species_count; ++i) {
                const CreatureProfile& p = catalogue[i];
                if (medium != demanded_medium(p.medium)) continue;
                const double weight = biome_weight(p, biome);
                if (weight > 0.0) {
                    row.push_back({&p, static_cast<int>(i), weight, clearance_of(p.tier)});
                }
            }
            ground_built[key] = 1;
        }
        return by_ground[key];
    };

    // Where each species can live and how much room it has there. `room` is what the
    // species would claim if nothing else existed: how common it is, how well the ground
    // suits it, how much it likes the biome, how the gradient treats it there, and how much
    // ground there is.
    std::vector<Share> shares;
    std::vector<std::size_t> share_begin(count + 1, 0);
    double tier_room[6][2] = {};
    double ground_km2[2] = {0.0, 0.0};
    long long tier_cells[6] = {};
    std::vector<long long> species_cells(species_count, 0);

    for (std::size_t i = 0; i < count; ++i) {
        const HabitatCell& cell = cells[i];
        const double clear_of = nearest[i];
        // Land and open water are two ecosystems sharing a planet, and a species belongs to
        // exactly one of them. Budget them apart or the sea decides how empty the land is.
        const bool wet = cell.medium != CellMedium::Land;
        ground_km2[wet ? 1 : 0] += cell.area_km2;

        bool tier_seen[6] = {};
        for (const Candidate& c : ground_candidates(cell.medium, cell.biome)) {
            if (clear_of < c.clearance_m) continue;
            const double score = suitability(*c.profile, cell);
            if (score < floor_score) continue;
            const double room = c.profile->occurrence * score * c.biome_weight *
                                danger_ramp(c.profile->tier, clear_of, span) * cell.area_km2;
            if (room <= 0.0) continue;
            shares.push_back({c.profile, c.index, room, score, wet});
            ++species_cells[static_cast<std::size_t>(c.index)];
            tier_room[c.profile->tier][wet ? 1 : 0] += room;
            // How much of the raster a tier can reach at all. This is the ceiling on what it
            // can place, not a curiosity: see the saturation block below.
            if (!tier_seen[c.profile->tier]) {
                tier_seen[c.profile->tier] = true;
                ++tier_cells[c.profile->tier];
            }
        }
        share_begin[i + 1] = shares.size();
    }

    // The pyramid is a claim about the world, so it is a budget FOR THE WORLD: tier t gets
    // density * falloff^(1-t) groups per square kilometre of its own medium, and that budget
    // is then spread over whatever ground the tier can actually use.
    //
    // Sharing per cell instead loses a tier its share wherever no member qualifies, and the
    // low monster tiers are the specialised ones -- grave mites want a barrow, shipbreaker
    // crabs want any sea at all. Measured, that inverted the pyramid outright: 114 tier four
    // lairs against 9 tier one. A budget cannot be forfeited by being fussy about where it
    // is spent.
    double budget[6][2] = {};
    for (int t = 1; t <= 5; ++t) {
        for (int w = 0; w < 2; ++w) {
            if (tier_room[t][w] > 0.0) {
                budget[t][w] = tier_density(spec.density, spec.falloff, t) *
                               ground_km2[w] / tier_room[t][w];
            }
        }
    }
    for (Share& s : shares) s.rate *= budget[s.profile->tier][s.wet ? 1 : 0];

    // Where the rate puts each tier, weighted by the rate itself. This is the danger ramp
    // integrated over the world and it is the only place the gradient is exact: what is
    // placed cannot show it, because a saturated tier fills the cells it reaches whatever
    // the rate said about which of them it preferred.
    //
    // Guarded on `settled`: with nothing settled anywhere `clear_of` is infinite, and an
    // infinite times a zero rate is a NaN in the published block.
    double rate_reach[6] = {};
    double rate_weight[6] = {};
    if (!settled.empty()) {
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t k = share_begin[i]; k < share_begin[i + 1]; ++k) {
                const Share& s = shares[k];
                rate_reach[s.profile->tier] += s.rate * nearest[i];
                rate_weight[s.profile->tier] += s.rate;
            }
        }
    }

    // --- placement ---------------------------------------------------------
    std::vector<NestSite> placed;
    std::vector<long long> counts(species_count, 0);
    long long drawn[6] = {};
    const std::size_t bucket_count = spec.bucket_is_tier ? 6 : species_count;
    std::vector<std::unordered_map<int, std::vector<int>>> at_node(bucket_count);
    std::vector<std::unordered_map<int, std::vector<int>>> on_row(bucket_count);
    std::vector<double> widest(bucket_count, 0.0);
    // Metres between two grid rows. `direction` puts row z at latitude pi/2 - pi*z/(n-1),
    // so this is the MERIDIONAL pitch and it does not vary with latitude, which is the whole
    // reason the refusal scan bounds by row rather than by cell.
    const double pitch = radius * kPi / (n - 1);

    // A species this cell has already REFUSED needs no second answer: the company only ever
    // grows and the test is a disjunction over it, so the answer cannot come back the other
    // way, and a cell asks the same question many times. A species this cell has already
    // PLACED is deliberately NOT memoised -- that would assert that a second site in the
    // same cell is always refused, which needs `reach` to be positive: true of the shipped
    // catalogue and of the option bounds, but an assumption, and the scan answers it from
    // the cell's own list in one lookup regardless.
    std::vector<char> refused_here(species_count, 0);
    std::vector<int> refused_list;

    // The lairs that stand (add_nests), first and in id order, as incumbents: the draw below
    // is untouched, and a lair it would have placed where a standing one holds the ground is
    // refused as any territory refusal is.
    if (!held.empty()) {
        std::unordered_map<std::string_view, std::size_t> species_at;
        for (std::size_t i = 0; i < species_count; ++i) species_at.emplace(catalogue[i].id, i);
        std::vector<const NestSite*> ordered;
        ordered.reserve(held.size());
        for (const NestSite& lair : held) ordered.push_back(&lair);
        std::sort(ordered.begin(), ordered.end(),
                  [](const NestSite* a, const NestSite* b) { return a->id < b->id; });
        for (const NestSite* lair : ordered) {
            const auto found = species_at.find(lair->species_id);
            if (found == species_at.end()) continue;  // a species this catalogue no longer holds
            const std::size_t species = found->second;
            const std::size_t b = spec.bucket_is_tier ? static_cast<std::size_t>(lair->tier) : species;
            ++counts[species];
            const int where = static_cast<int>(placed.size());
            placed.push_back(*lair);
            at_node[b][lair->node].push_back(where);
            on_row[b][lair->z].push_back(where);
            if (lair->range_m > widest[b]) widest[b] = lair->range_m;
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t begin = share_begin[i];
        const std::size_t end = share_begin[i + 1];
        if (begin == end) continue;
        const HabitatCell& cell = cells[i];

        double total = 0.0;
        for (std::size_t k = begin; k < end; ++k) total += shares[k].rate;
        // floor plus one Bernoulli draw: an exact discretisation while the cell rate is
        // below one, which it is almost everywhere, and no new distribution is needed. The
        // total is bounded by the tier budget over one cell's area, so the cast cannot
        // overflow however large the world is.
        long long groups = static_cast<long long>(total);
        if (rng.random() < total - static_cast<double>(groups)) ++groups;

        for (const int idx : refused_list) refused_here[static_cast<std::size_t>(idx)] = 0;
        refused_list.clear();

        for (long long g = 0; g < groups; ++g) {
            const Share& s = pick(rng, shares.data() + begin, shares.data() + end, total);
            const CreatureProfile& p = *s.profile;
            const auto species = static_cast<std::size_t>(s.index);
            // Incremented BEFORE the cap and the memo, so `drawn` counts every group the
            // rate asked for, including ones no rule could ever have placed.
            ++drawn[p.tier];
            if (per_species != 0 && counts[species] >= per_species) continue;
            if (refused_here[species]) continue;

            const double reach = p.spacing_m * nest_spacing / 2.0;
            const std::size_t b = spec.bucket_is_tier ? static_cast<std::size_t>(p.tier) : species;
            if (blocked(placed, at_node[b], on_row[b], widest[b], cell, reach, radius, pitch, n)) {
                refused_here[species] = 1;
                refused_list.push_back(s.index);
                continue;
            }

            ++counts[species];
            NestSite site;
            site.id = std::string(spec.prefix) + "-" + std::string(p.id) + "-" +
                      std::to_string(cell.node);
            site.species_id = p.id;
            site.name = p.name;
            site.node = cell.node;
            site.x = cell.x;
            site.z = cell.z;
            site.direction = cell.direction;
            site.biome = cell.biome;
            site.tier = p.tier;
            site.family = p.family;
            site.kind = p.kind;
            site.size = p.size;
            site.role = p.role;
            site.creature_class = p.creature_class;
            site.suitability = s.score;
            site.range_m = reach;
            // A den or lair is a place you can find and raid; a herd, roost or shoal is
            // only a range where you meet them.
            site.den = p.kind == "den" || p.kind == "lair";
            // Recomputed, never read from the row.
            site.real = p.creature_class == CreatureClass::Animal;

            const int where = static_cast<int>(placed.size());
            placed.push_back(std::move(site));
            at_node[b][cell.node].push_back(where);
            on_row[b][cell.z].push_back(where);
            if (reach > widest[b]) widest[b] = reach;
        }
    }

    // A safety valve, not the thing that decides how full a world is. When it has to bite it
    // keeps the notable creatures: highest tier first, then by id, LEXICOGRAPHICALLY.
    //
    // It rewrites `counts` but not `species_cells`, so a species the cap empties reports
    // "lost the draw to incumbents", indistinguishable from one that genuinely lost; and it
    // runs before `saturation`, so `refused` silently includes cap removals that no
    // territory rule refused. Both are inherited defects, recorded rather than fixed -- the
    // cap is zero by default and neither bites in a shipped world.
    if (limit != 0 && static_cast<long long>(placed.size()) > limit) {
        std::sort(placed.begin(), placed.end(), [](const NestSite& a, const NestSite& b) {
            if (a.tier != b.tier) return a.tier > b.tier;
            return a.id < b.id;
        });
        placed.resize(static_cast<std::size_t>(limit));
        std::unordered_map<std::string_view, std::size_t> by_species;
        for (std::size_t i = 0; i < species_count; ++i) by_species.emplace(catalogue[i].id, i);
        counts.assign(species_count, 0);
        for (const NestSite& a : placed) ++counts[by_species.at(a.species_id)];
    }

    // Which of the species that found nothing were refused by the CLEARANCE rather than by
    // the world. A species whose habitat is the ground people stand on can have all its
    // eligible cells inside a settlement's clearance, and calling that "conditions absent
    // from this world" sends a reader to look at the terrain, where there is nothing to
    // find. Scanned only over the cells close enough to be refused by anything in the
    // catalogue, and only for species with no cells at all.
    double ceiling = 0.0;
    for (const CreatureProfile& p : catalogue) ceiling = std::max(ceiling, clearance_of(p.tier));
    std::vector<std::size_t> close;
    for (std::size_t i = 0; i < count; ++i) {
        if (nearest[i] < ceiling) close.push_back(i);
    }
    std::vector<char> cleared_out(species_count, 0);
    for (std::size_t i = 0; i < species_count; ++i) {
        if (species_cells[i] != 0) continue;
        const CreatureProfile& p = catalogue[i];
        const double needed = clearance_of(p.tier);
        for (const std::size_t at : close) {
            const HabitatCell& cell = cells[at];
            if (nearest[at] < needed && biome_weight(p, cell.biome) > 0.0 &&
                suitability(p, cell) >= floor_score) {
                cleared_out[i] = 1;
                break;
            }
        }
    }

    // --- the block ---------------------------------------------------------
    NestBlock block;
    block.version = spec.which == CreatureClass::Animal ? kWildlifeVersion : kBeastNestsVersion;
    block.profiles = catalogue;
    block.catalogue_count = static_cast<long long>(species_count);
    block.method = spec.which == CreatureClass::Animal ? kWildlifeMethod : kBeastNestsMethod;
    block.limits = spec.which == CreatureClass::Animal ? kWildlifeLimits : kBeastNestsLimits;
    if (spec.which == CreatureClass::Animal) block.roles = creature_roles();

    // The published order of the block, and a LEXICOGRAPHIC string sort: node 10 sorts
    // before node 2. Do not "fix" it to a numeric sort.
    std::sort(placed.begin(), placed.end(),
              [](const NestSite& a, const NestSite& b) { return a.id < b.id; });
    block.sites = std::move(placed);

    long long kept[6] = {};
    double kept_reach[6] = {};
    for (const NestSite& a : block.sites) {
        ++kept[a.tier];
        kept_reach[a.tier] += nearest[static_cast<std::size_t>(a.node)];
    }
    // What the rate asked for, what the raster could hold, and what survived. Read a flat
    // placed histogram against `drawn` and `cells` before reading it as the ecology: the
    // pyramid is exact in the rate and clipped in what is placed, and the two differ by an
    // order of magnitude.
    for (int t = 1; t <= 5; ++t) {
        if (tier_cells[t] == 0 && drawn[t] == 0 && kept[t] == 0) continue;
        NestSaturation row;
        row.tier = t;
        row.cells = tier_cells[t];
        row.drawn = drawn[t];
        row.placed = kept[t];
        row.refused = drawn[t] - kept[t];
        if (!settled.empty() && rate_weight[t] != 0.0) {
            row.rate_distance_m = rate_reach[t] / rate_weight[t];
        }
        if (!settled.empty() && kept[t] != 0) {
            row.placed_distance_m = kept_reach[t] / static_cast<double>(kept[t]);
        }
        block.saturation.push_back(row);
    }

    block.diagnostics.reserve(species_count);
    for (std::size_t i = 0; i < species_count; ++i) {
        const CreatureProfile& p = catalogue[i];
        NestDiagnostic row;
        row.species_id = p.id;
        row.name = p.name;
        row.tier = p.tier;
        row.role = p.role;
        row.placed = counts[i];
        row.cells = species_cells[i];
        row.reason = reach_reason(p, species_cells[i], counts[i], cleared_out[i] != 0);
        block.diagnostics.push_back(row);
        if (species_cells[i] != 0) ++block.reachability.eligible;
        if (counts[i] != 0) ++block.reachability.placed;
        if (p.never_satisfiable) ++block.reachability.never_satisfiable;
    }
    block.reachability.authored = static_cast<long long>(species_count);
    return block;
}

PassSpec animal_pass(const WorldOptions& o) {
    PassSpec spec;
    spec.prefix = "animal";
    spec.domain = "animals-v1";
    spec.which = CreatureClass::Animal;
    spec.density = o[Option::animal_density] * o[Option::nest_density];
    spec.falloff = o[Option::animal_tier_falloff];
    spec.clearance_divisor = 3.0;
    // Territorial within a species only: a wolf range over a deer range is the point of the
    // map, not a collision.
    spec.bucket_is_tier = false;
    return spec;
}

PassSpec monster_pass(const WorldOptions& o) {
    PassSpec spec;
    spec.prefix = "nest";
    spec.domain = "monsters-v1";
    spec.which = CreatureClass::Monster;
    spec.density = o[Option::monster_density] * o[Option::nest_density] * o[Option::nest_fantasy];
    spec.falloff = o[Option::monster_tier_falloff];
    spec.clearance_divisor = 1.0;
    // Rank, not spacing, and an EQUAL only. Refusing by rank would also refuse the pyramid:
    // a tier one candidate would be held off by every lair on the world while a tier five
    // was held off only by other tier fives.
    spec.bucket_is_tier = true;
    return spec;
}

// The gate both passes carry: no recipe, or too early a phase, and no block is written.
// That is the reference's own behaviour and is not an error.
bool passes_run(const Config& cfg) { return cfg.world_recipe != 0 && cfg.phase >= 7; }

void require_metre_grid(const World& world, const SphereGrid& grid) {
    const Config& cfg = world.effective;
    if (std::abs(grid.radius - cfg.globe_radius) > 1e-6 * std::max(1.0, cfg.globe_radius)) {
        throw std::invalid_argument(
            "a nest pass was given a grid of radius " + std::to_string(grid.radius) +
            " m but this world is " + std::to_string(cfg.globe_radius) +
            " m across; a design-space grid would compare the clearance, the territory "
            "radius and the ramp span -- all absolute metres -- against design units");
    }
}

std::vector<Vec3> settled_directions(const SphereGrid& grid, const std::vector<int>& nodes) {
    std::vector<Vec3> out;
    out.reserve(nodes.size());
    for (const int node : nodes) {
        if (node < 0 || static_cast<std::size_t>(node) >= grid.node_count()) {
            throw std::invalid_argument("a settled thing names node " + std::to_string(node) +
                                        ", which this grid does not have");
        }
        const auto i = static_cast<std::size_t>(node);
        out.push_back(direction(grid.point_x[i], grid.point_z[i], grid.n));
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------

std::string_view to_string(NestReason reason) {
    switch (reason) {
        case NestReason::Placed: return "placed";
        case NestReason::LostTheDraw: return "lost the draw to incumbents";
        case NestReason::AbsentFromThisWorld: return "conditions absent from this world";
        case NestReason::NeverSatisfiable: return "never satisfiable";
        case NestReason::ClearedOut: return "its only ground is inside a settlement clearance";
    }
    return "";
}

std::string_view nest_reachability_scope() {
    return "`eligible` and `placed` count this world only and move with the seed; they are "
           "a sample, not a capability. `never_satisfiable` is decided from the profile "
           "alone and holds for every world.";
}

double biome_weight(const CreatureProfile& profile, int biome_id) {
    if (biome_id < 0 || biome_id >= kNestBiomeCount) return 0.0;
    return profile.biome_weights[static_cast<std::size_t>(biome_id)];
}

double suitability(const CreatureProfile& profile, const HabitatCell& cell) {
    if (cell.medium != demanded_medium(profile.medium)) return 0.0;
    if (profile.medium == CreatureMedium::Shore && cell.coast < kShoreCoastMin) return 0.0;
    if (!(profile.temperature_min_c <= cell.temperature_c &&
          cell.temperature_c <= profile.temperature_max_c)) {
        return 0.0;
    }
    // A requirement tests the RAW field, so `depth: 30` means thirty metres down, while a
    // weight below tests the CLAMPED one. One field, two readings; inherited.
    for (const HabitatTerm& t : profile.requirements) {
        if (cell.fields[static_cast<std::size_t>(t.field)] < t.value) return 0.0;
    }
    // A weight-NORMALISED mean, not a sum: every term is divided by the row's own weight
    // total, so scaling every weight of a row changes nothing at all.
    double score = 0.0;
    for (const HabitatTerm& t : profile.weights) {
        score += clamp01(cell.fields[static_cast<std::size_t>(t.field)]) * t.value /
                 profile.weight_total;
    }
    return score;
}

double danger_ramp(int tier, double clear_of_m, double span_m) {
    // A ratio of squares rather than an exponential: no pow, no exp, nothing whose last bit
    // depends on the libm the build links against. Squared because the plain ratio is too
    // gentle to be worth having -- across the usable band it swings by only a factor of two
    // however low the floor goes.
    //
    // With nothing settled, clear_of is infinite: out is infinite, `near` is exactly zero
    // rather than a NaN, and the ramp degenerates to floor + (1-floor)*danger.
    const double reach = span_m * span_m;
    const double out = clear_of_m * clear_of_m;
    const double near = reach / (reach + out);
    const double danger = (tier - 1) / 4.0;
    // Tier three is the pivot and sits at (1+floor)/2 at every distance, so a five-step
    // danger ladder is not something this mechanism can make at any tuning.
    return kNestDangerFloor + (1.0 - kNestDangerFloor) * (danger * (1.0 - near) + (1.0 - danger) * near);
}

double tier_density(double base, double falloff, int tier) {
    // falloff^(1-tier) by repeated division rather than pow, for the same reason
    // danger_ramp avoids it: the exponent is a small integer and a libm call would make
    // the last bit depend on which library the build linked.
    double density = base;
    for (int k = 1; k < tier; ++k) density /= falloff;
    return density;
}

std::vector<double> nearest_settled(const std::vector<HabitatCell>& cells,
                                    const std::vector<Vec3>& settled, double radius) {
    const std::size_t total = settled.size();
    if (total == 0) {
        return std::vector<double>(cells.size(), std::numeric_limits<double>::infinity());
    }

    // Float for float the same list as a min of the distance over every settled site, and
    // for a reason rather than by measurement: the distance is radius*acos(clamp(dot)),
    // acos is decreasing and the clamp is monotone, so the smallest distance is the one at
    // the LARGEST dot product. Written as a min the scan cannot stop early; written as an
    // argmax it can, and that is the only reason to write it the second way.
    std::vector<double> latitude(total);
    for (std::size_t i = 0; i < total; ++i) latitude[i] = std::asin(clamp_unit(settled[i].y));
    std::vector<std::size_t> order(total);
    for (std::size_t i = 0; i < total; ++i) order[i] = i;
    // std::sort is NOT stable, so the original-index tie-break is written out: two settled
    // things at exactly the same latitude have to be visited in the order the caller listed
    // them or the walk stops one candidate earlier or later.
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (latitude[a] != latitude[b]) return latitude[a] < latitude[b];
        return a < b;
    });
    std::vector<double> lats(total);
    std::vector<Vec3> dirs(total);
    for (std::size_t i = 0; i < total; ++i) {
        lats[i] = latitude[order[i]];
        dirs[i] = settled[order[i]];
    }

    // The stopping rule is the one piece of new mathematics. The angle between two unit
    // vectors is at least the difference of their latitudes, whatever their longitudes do.
    // So with the sites sorted by latitude and walked outwards from the cell's own
    // latitude, nearest latitude first, the walk can stop the moment that difference
    // reaches the best angle already found: everything still unvisited is at least that far
    // away on both sides, because the side with the smaller remaining gap is the one being
    // taken.
    std::vector<double> out;
    out.reserve(cells.size());
    for (const HabitatCell& cell : cells) {
        const Vec3 c = cell.direction;
        const double phi = std::asin(clamp_unit(c.y));
        auto left = static_cast<std::size_t>(std::lower_bound(lats.begin(), lats.end(), phi) -
                                             lats.begin());
        std::size_t right = left;
        double best = -2.0;
        double angle = kPi;
        for (;;) {
            const bool has_left = left > 0;
            const bool has_right = right < total;
            if (!has_left && !has_right) break;
            const double gap_left = has_left ? phi - lats[left - 1] : 0.0;
            const double gap_right = has_right ? lats[right] - phi : 0.0;
            double gap;
            std::size_t at;
            if (!has_right || (has_left && gap_left <= gap_right)) {
                gap = gap_left;
                --left;
                at = left;
            } else {
                gap = gap_right;
                at = right;
                ++right;
            }
            // The candidate taken is the nearest in latitude on either side, so if it is
            // already further out than the best angle, so is everything behind it.
            if (gap >= angle + kLatitudeSlack) break;
            const double d = dot(c, dirs[at]);
            if (d > best) {
                best = d;
                angle = std::acos(clamp_unit(best));
            }
        }
        out.push_back(radius * std::acos(clamp_unit(best)));
    }
    return out;
}

NestHabitat habitat_cells(const World& world, const SphereGrid& grid) {
    const int n = world.effective.size;
    const std::size_t count = grid.node_count();
    const std::span<const std::string_view> names = nest_field_names();
    const std::size_t field_count = names.size();

    std::vector<FieldBinding> binding(field_count);
    for (std::size_t f = 0; f < field_count; ++f) {
        const auto derived = std::find(std::begin(kDerivedFields), std::end(kDerivedFields), names[f]);
        if (derived != std::end(kDerivedFields)) {
            binding[f].derived = static_cast<int>(derived - std::begin(kDerivedFields));
        } else {
            binding[f].layer = find_layer(world, names[f]);
        }
    }

    // The layers the derivations themselves read. A missing one reads zero, except
    // temperature, which reads 100 for the habitability gate and 10 for `cold` -- the two
    // defaults are different and that difference is the source's.
    const Grid* water_type = find_layer(world, "water_type");
    const Grid* salinity = find_layer(world, "salinity");
    const Grid* moisture = find_layer(world, "moisture");
    const Grid* slope = find_layer(world, "slope");
    const Grid* river = find_layer(world, "river");
    const Grid* water_depth = find_layer(world, "water_depth");
    const Grid* maritime = find_layer(world, "maritime");
    const Grid* temperature = find_layer(world, "temperature");
    const Grid* natural_biome = find_layer(world, "natural_biome");
    const Grid* biome = find_layer(world, "biome");

    NestHabitat out;
    out.cells.resize(count);
    out.storage.assign(count * field_count, 0.0);

    std::array<double, static_cast<std::size_t>(Derived::Count)> derived{};
    for (std::size_t i = 0; i < count; ++i) {
        const int x = grid.point_x[i];
        const int z = grid.point_z[i];
        const double water = layer_at(water_type, x, z);
        const double wet = layer_at(moisture, x, z);
        const double steep = layer_at(slope, x, z);  // DEGREES

        derived[static_cast<std::size_t>(Derived::Depth)] = layer_at(water_depth, x, z);
        derived[static_cast<std::size_t>(Derived::Dryness)] = 1.0 - clamp01(wet);
        derived[static_cast<std::size_t>(Derived::Coast)] = layer_at(maritime, x, z);
        derived[static_cast<std::size_t>(Derived::Mountain)] = clamp01(steep / 25.0);
        derived[static_cast<std::size_t>(Derived::Plains)] =
            clamp01(1.0 - steep / 15.0) * (1.0 - std::abs(wet - 0.45));
        derived[static_cast<std::size_t>(Derived::Wetland)] =
            std::max({water != 0.0 ? 1.0 : 0.0, layer_at(river, x, z), wet * std::exp(-steep / 8.0)});
        const double cold_temperature =
            temperature != nullptr ? temperature->at(x, z) : kMissingColdTemperatureC;
        derived[static_cast<std::size_t>(Derived::Cold)] = clamp01((10.0 - cold_temperature) / 25.0);

        double* row = out.storage.data() + i * field_count;
        for (std::size_t f = 0; f < field_count; ++f) {
            // Derived last, so a derived name wins over a layer that happens to share it.
            row[f] = binding[f].derived >= 0 ? derived[static_cast<std::size_t>(binding[f].derived)]
                                             : layer_at(binding[f].layer, x, z);
        }

        HabitatCell& cell = out.cells[i];
        cell.node = static_cast<int>(i);
        cell.x = x;
        cell.z = z;
        cell.area_km2 = grid.areas[i] / 1e6;
        cell.direction = direction(x, z, n);
        // The NATURAL biome, never the magic variant: the tables key the ground a creature
        // evolved for, and a mutated cell is still that ground.
        cell.biome = static_cast<int>(natural_biome != nullptr ? natural_biome->at(x, z)
                                                               : layer_at(biome, x, z));
        cell.medium = water == 1.0                                       ? CellMedium::Marine
                      : water == 0.0                                     ? CellMedium::Land
                      : layer_at(salinity, x, z) < kSaltLakeSalinity     ? CellMedium::Freshwater
                                                                         : CellMedium::SaltLake;
        cell.temperature_c = temperature != nullptr ? temperature->at(x, z) : kMissingTemperatureC;
        cell.coast = derived[static_cast<std::size_t>(Derived::Coast)];
        cell.fields = std::span<const double>(row, field_count);
    }
    return out;
}

std::optional<NestBlock> add_animals(const World& world, const SphereGrid& grid,
                                     const std::vector<int>& settled_nodes) {
    // Hunting grounds. Prey are many and small-ranged, predators few and wide.
    const Config& cfg = world.effective;
    if (!passes_run(cfg)) return std::nullopt;
    require_metre_grid(world, grid);
    return place(world, habitat_cells(world, grid), settled_directions(grid, settled_nodes),
                 animal_pass(cfg.options), {});
}

std::optional<NestBlock> add_monsters(const World& world, const SphereGrid& grid,
                                      const std::vector<int>& settled_nodes) {
    // Territory. A greater monster excludes an equal, and tolerates a lesser.
    const Config& cfg = world.effective;
    if (!passes_run(cfg)) return std::nullopt;
    require_metre_grid(world, grid);
    return place(world, habitat_cells(world, grid), settled_directions(grid, settled_nodes),
                 monster_pass(cfg.options), {});
}

std::optional<NestPasses> add_nests(const World& world, const SphereGrid& grid,
                                    const std::vector<int>& settled_nodes,
                                    const std::vector<NestSite>& held_lairs) {
    // Both passes, animals first. They never read each other: each draws from its own
    // named rng stream and neither reads the other's sites, so the ground is the only
    // thing they share -- which is why it is built once here rather than twice.
    const Config& cfg = world.effective;
    if (!passes_run(cfg)) return std::nullopt;
    require_metre_grid(world, grid);

    // A lair whose node has since been settled is gone: people stand where it stood.
    const std::set<int> settled_set(settled_nodes.begin(), settled_nodes.end());
    std::vector<NestSite> standing;
    for (const NestSite& lair : held_lairs) {
        if (lair.real || settled_set.count(lair.node) != 0) continue;
        standing.push_back(lair);
    }

    const NestHabitat field = habitat_cells(world, grid);
    const std::vector<Vec3> settled = settled_directions(grid, settled_nodes);
    NestPasses passes;
    passes.wildlife = place(world, field, settled, animal_pass(cfg.options), {});
    passes.beast_nests = place(world, field, settled, monster_pass(cfg.options), standing);
    return passes;
}

}  // namespace fwg
