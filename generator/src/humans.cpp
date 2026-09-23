#include "fwg/humans.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <optional>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "fwg/civilizations.hpp"
#include "fwg/content.hpp"
#include "fwg/option_ids.hpp"
#include "fwg/random.hpp"
#include "fwg/routing.hpp"
#include "fwg/scale.hpp"
#include "fwg/settlement.hpp"
#include "fwg/settlements_stage.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

// --- authored distances, every one of them in metres ------------------------
//
// These were tuned against the 11.15 km reference world and none of them is scaled with
// world width — unlike `support_reach`, `settlement_spacing` and `culture_link_cost`,
// which the request builder multiplies by the reach factor — except the two fortress
// distances, which the fort rule of 2026-09-23 scales where it uses them. At a 200 km circumference one
// grid cell is already about 1.6 km at size 129, so each of these is smaller than a single
// step and can only ever refuse a node its own distance from itself. They are ported at
// their authored values because changing them is a design decision, not a port decision;
// docs/scale-defects.md records the consequence.

// Keeps a hamlet off the city's own doorstep. Road-cost units, not a length.
constexpr double kHamletMinAccessCost = 100.0;
// Great-circle separation between a new hamlet and everything already placed.
constexpr double kHamletSeparationM = 120.0;
// The same for a fortress -- SCALED with the world since 2026-09-23, as the support reach
// is, because the fort rule was redesigned then and this is its spacing. Unscaled it fell
// below one cell and spaced nothing: 67 forts stood a median 3 km apart on a 6 km grid.
constexpr double kFortressSeparationM = 200.0;
// How far a fort may stand from the node it guards before the distance costs it one unit
// of defence. Scaled like the spacing: unscaled, a neighbouring cell cost three units and
// the route node always won, where the reference meant a mild preference.
constexpr double kFortressNearM = 500.0;
// How far a hamlet's catchment reaches, in road-cost units.
constexpr double kCatchmentReach = 250.0;
// Annual trade: transport loss decay length and price scale, both in road-cost units.
constexpr double kTradeEfficiencyScale = 4000.0;
constexpr double kTradePriceScale = 5000.0;
constexpr double kTradePriceFloor = 0.2;

// Residents per fully productive square kilometre. The explicit provisional food
// calibration used throughout this generator.
constexpr double kResidentsPerProductiveKm2 = 100.0;
// A hamlet still works land outside its own speciality, at a quarter rate.
constexpr double kOffSpecialityRate = 0.25;
// Below this a candidate cell is not worth settling at all.
constexpr double kHamletScoreFloor = 0.025;
// Rural residents each generated hamlet is assumed to stand for, when the world is
// inferring its own counts.
constexpr double kResidentsPerHamlet = 40.0;

// --- frontier forts (2026-09-23) ---------------------------------------------
// The routes an army weighs between two lands: every node within 10% of the cheapest.
constexpr double kFrontierSlack = 0.10;
// The share of the way across in which a chokepoint is sought, cut into slices. The ends
// are left out: every route leaves from a city, where any corridor is narrow.
constexpr double kFrontierBandLow = 0.2;
constexpr double kFrontierBandHigh = 0.8;
constexpr int kFrontierSlices = 12;
// A frontier fort's standing against a road fort's, whose base runs 1 to 4 with the
// junction's degree: a chokepoint between two peoples ranks with a busy crossing.
constexpr double kFrontierBase = 2.0;
// Height over the chokepoint worth one unit of defence: the high ground beside the pass.
constexpr double kOverlookM = 50.0;

// Trade below this is not worth recording; it is a floor on a residual, not a tolerance.
constexpr double kTradeEpsilon = 1e-9;
// The same idea inside the monthly simulator, where the quantities are twelfths.
constexpr double kMonthlyShipmentFloor = 1e-12;
constexpr double kShortageEpsilon = 1e-8;

std::vector<double> nodes_of(const World& world, const SphereGrid& grid, const std::string& layer,
                             double fallback = 0.0) {
    std::vector<double> out(grid.node_count(), fallback);
    const auto it = world.layers.find(layer);
    if (it == world.layers.end()) return out;
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = it->second.at(grid.point_x[i], grid.point_z[i]);
    return out;
}

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }

std::string hex8(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%08x", value);
    return std::string(buffer);
}

const CivilizationProfile& profile_or_default(std::string_view id) {
    if (const CivilizationProfile* p = civilization_profile(id)) return *p;
    return *civilization_profile(default_profile_id());
}

// Plain Dijkstra over the CITY graph, which is a handful of nodes rather than the sphere.
// Separate from fwg::shortest_paths because that one walks a SphereGrid.
struct CityPaths {
    std::vector<double> distance;
    std::vector<int> parent;
};

CityPaths city_shortest_paths(const std::vector<std::vector<std::pair<int, double>>>& graph, int start) {
    CityPaths out;
    out.distance.assign(graph.size(), kUnreachable);
    out.parent.assign(graph.size(), -1);
    if (start < 0 || static_cast<std::size_t>(start) >= graph.size()) return out;
    out.distance[static_cast<std::size_t>(start)] = 0.0;

    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    queue.emplace(0.0, start);
    while (!queue.empty()) {
        const auto [value, i] = queue.top();
        queue.pop();
        const auto ui = static_cast<std::size_t>(i);
        if (value != out.distance[ui]) continue;
        for (const auto& [j, weight] : graph[ui]) {
            const double candidate = value + weight;
            const auto uj = static_cast<std::size_t>(j);
            if (candidate >= out.distance[uj]) continue;
            out.distance[uj] = candidate;
            out.parent[uj] = i;
            queue.emplace(candidate, j);
        }
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------

std::vector<int> culture_groups(int count, const std::vector<CultureLink>& links, double threshold) {
    if (count <= 0) return {};
    std::vector<int> groups(static_cast<std::size_t>(count));
    for (std::size_t i = 0; i < groups.size(); ++i) groups[i] = static_cast<int>(i);

    const auto root = [&groups](int i) {
        while (groups[static_cast<std::size_t>(i)] != i) i = groups[static_cast<std::size_t>(i)];
        return i;
    };

    for (const CultureLink& link : links) {
        if (link.cost > threshold) continue;
        if (link.from < 0 || link.from >= count || link.to < 0 || link.to >= count) continue;
        const int a = root(link.from);
        const int b = root(link.to);
        // Always point the LARGER root at the smaller. That is what makes the canonical
        // id the lowest member index and makes the result independent of link order.
        groups[static_cast<std::size_t>(std::max(a, b))] = std::min(a, b);
    }

    std::vector<int> out(groups.size());
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = root(static_cast<int>(i));
    return out;
}

// ---------------------------------------------------------------------------

std::vector<Crossing> narrowest_crossings(const std::vector<double>& from_a, const std::vector<double>& from_b,
                                          const std::vector<double>& areas_m2, double slack, double band_low,
                                          double band_high, int slices) {
    const std::size_t count = std::min({from_a.size(), from_b.size(), areas_m2.size()});
    if (slices < 1 || !(band_low < band_high) || band_low < 0.0 || band_high > 1.0) return {};
    double cheapest = kUnreachable;
    for (std::size_t i = 0; i < count; ++i) cheapest = std::min(cheapest, from_a[i] + from_b[i]);
    // Unjoined lands, or two lands that share a node: no route to narrow.
    if (!std::isfinite(cheapest) || cheapest <= 0.0) return {};
    const double within = cheapest * (1.0 + slack);

    struct Slice {
        double area = 0.0;
        int node = -1;
        double node_total = kUnreachable;
        double node_t = 0.0;
    };
    // `open` excludes the band's ends where the fallback reads the whole route: a node with
    // t of exactly 0 or 1 IS a city.
    const auto cut = [&](double low, double high, int parts, bool open) {
        std::vector<Slice> out(static_cast<std::size_t>(parts));
        const double step = (high - low) / parts;
        for (std::size_t i = 0; i < count; ++i) {
            const double total = from_a[i] + from_b[i];
            if (!(total <= within)) continue;  // off the corridor, or unreachable from one side
            const double t = from_a[i] / total;
            if (t < low || t > high || (open && (t <= low || t >= high))) continue;
            const auto k = std::min(static_cast<std::size_t>((t - low) / step), out.size() - 1);
            Slice& s = out[k];
            s.area += areas_m2[i];
            const int node = static_cast<int>(i);
            if (total < s.node_total || (total == s.node_total && node < s.node)) {
                s.node_total = total;
                s.node = node;
                s.node_t = t;
            }
        }
        return out;
    };

    double low = band_low;
    double high = band_high;
    std::vector<Slice> sliced = cut(low, high, slices, false);
    const bool empty = std::none_of(sliced.begin(), sliced.end(), [](const Slice& s) { return s.node >= 0; });
    if (empty) {
        low = 0.0;
        high = 1.0;
        sliced = cut(low, high, 1, true);
    }

    struct Ranked {
        double width;
        double off_middle;
        std::size_t slice;
    };
    std::vector<Ranked> ranked;
    const double step = (high - low) / static_cast<double>(sliced.size());
    for (std::size_t k = 0; k < sliced.size(); ++k) {
        if (sliced[k].node < 0) continue;
        const double centre = low + (static_cast<double>(k) + 0.5) * step;
        // An open plain's corridor between two points is widest midway and narrows as
        // sqrt(t (1 - t)) toward either end; dividing it out leaves what the terrain did.
        ranked.push_back({sliced[k].area / std::sqrt(centre * (1.0 - centre)), std::abs(centre - 0.5), k});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        return std::tie(a.width, a.off_middle, a.slice) < std::tie(b.width, b.off_middle, b.slice);
    });

    std::vector<Crossing> out;
    out.reserve(ranked.size());
    for (const Ranked& r : ranked) {
        const Slice& s = sliced[r.slice];
        out.push_back({s.node, cheapest, s.area / 1e6, s.node_t});
    }
    return out;
}

// ---------------------------------------------------------------------------

std::vector<FoodShipment> exchange_food(std::vector<CityCore>& cores, const std::vector<Road>& roads) {
    std::vector<FoodShipment> shipments;
    if (cores.empty()) return shipments;

    std::vector<std::vector<std::pair<int, double>>> graph(cores.size());
    for (const Road& road : roads) {
        if (road.from < 0 || static_cast<std::size_t>(road.from) >= cores.size()) continue;
        if (road.to < 0 || static_cast<std::size_t>(road.to) >= cores.size()) continue;
        graph[static_cast<std::size_t>(road.from)].emplace_back(road.to, road.cost);
        graph[static_cast<std::size_t>(road.to)].emplace_back(road.from, road.cost);
    }

    for (CityCore& c : cores) {
        c.food_imports = 0.0;
        c.food_exports = 0.0;
        c.trade_material_cost = 0.0;
    }

    // Neediest city buys first, so the worst shortage has first call on the world's
    // surplus rather than whichever city happened to be founded earliest.
    std::vector<int> buyers(cores.size());
    for (std::size_t i = 0; i < buyers.size(); ++i) buyers[i] = static_cast<int>(i);
    std::sort(buyers.begin(), buyers.end(), [&cores](int a, int b) {
        const CityCore& x = cores[static_cast<std::size_t>(a)];
        const CityCore& y = cores[static_cast<std::size_t>(b)];
        const double gap_x = x.food_supply - x.food_demand;
        const double gap_y = y.food_supply - y.food_demand;
        return gap_x != gap_y ? gap_x < gap_y : x.site_id < y.site_id;
    });

    for (const int bi : buyers) {
        CityCore& buyer = cores[static_cast<std::size_t>(bi)];
        double needed = std::max(0.0, buyer.food_demand - buyer.food_supply);
        const int b = buyer.site_id;
        const CityPaths paths = city_shortest_paths(graph, b);

        // Nearest seller first: trade loses to distance, so the cheapest food is the
        // closest food even when a distant city has more of it.
        std::vector<int> sellers(cores.size());
        for (std::size_t i = 0; i < sellers.size(); ++i) sellers[i] = static_cast<int>(i);
        std::sort(sellers.begin(), sellers.end(), [&paths](int x, int y) {
            return std::tie(paths.distance[static_cast<std::size_t>(x)], x) <
                   std::tie(paths.distance[static_cast<std::size_t>(y)], y);
        });

        for (const int a : sellers) {
            if (needed <= kTradeEpsilon) break;
            const auto ua = static_cast<std::size_t>(a);
            if (a == b || !std::isfinite(paths.distance[ua])) continue;

            CityCore& seller = cores[ua];
            // What the seller has already promised to earlier buyers is gone: nobody
            // sells the same grain twice.
            const double surplus =
                std::max(0.0, seller.food_supply - seller.food_demand - seller.food_exports);
            const double reach = paths.distance[ua];
            const double efficiency = std::exp(-reach / kTradeEfficiencyScale);
            const double price = kTradePriceFloor + reach / kTradePriceScale;
            const double budget = std::max(0.0, buyer.material_supply - buyer.trade_material_cost);
            const double received = std::min({needed, surplus * efficiency, budget / price});
            if (received <= kTradeEpsilon) continue;

            const double sent = received / efficiency;
            seller.food_exports += sent;
            buyer.food_imports += received;
            buyer.trade_material_cost += received * price;
            needed -= received;

            FoodShipment shipment;
            shipment.from = a;
            shipment.to = b;
            shipment.sent = sent;
            shipment.delivered = received;
            shipment.lost = sent - received;
            shipment.material_cost = received * price;
            shipment.city_path.push_back(a);
            while (shipment.city_path.back() != b) {
                const int next = paths.parent[static_cast<std::size_t>(shipment.city_path.back())];
                if (next < 0) break;  // a broken chain is a bug upstream, not a silent loop
                shipment.city_path.push_back(next);
            }
            shipments.push_back(std::move(shipment));
        }
    }

    // What the trade could not close stays visible rather than being papered over.
    for (CityCore& c : cores) {
        c.food_deficit = std::max(0.0, c.food_demand - c.food_supply - c.food_imports + c.food_exports);
    }
    return shipments;
}

// ---------------------------------------------------------------------------

std::array<double, 12> seasonal_harvest(double annual_food, double wet, double latitude_deg,
                                        double mean_temp, double tolerance, double ideal,
                                        double seasonality) {
    // Seasonality reverses across the equator because sin(latitude) changes sign, and a
    // wet climate swings less than a dry one.
    const double amplitude =
        20.0 * std::sin(latitude_deg * kPi / 180.0) * (1.0 - 0.4 * wet) * seasonality;
    const double mean_offset = (mean_temp - ideal) / tolerance;
    const double baseline = std::max(0.05, std::exp(-mean_offset * mean_offset));

    std::array<double, 12> growth{};
    for (int month = 0; month < 12; ++month) {
        const double temp = mean_temp + amplitude * std::cos(2.0 * kPi * (month - 6) / 12.0);
        const double offset = (temp - ideal) / tolerance;
        const double thermal = std::exp(-offset * offset) * clamp01(temp / 8.0);
        growth[static_cast<std::size_t>(month)] = std::min(1.5, thermal / baseline);
    }

    // ONE-MONTH HARVEST LAG: what grows in a month is delivered in the next.
    std::array<double, 12> out{};
    for (int month = 0; month < 12; ++month) {
        out[static_cast<std::size_t>(month)] =
            annual_food * growth[static_cast<std::size_t>((month + 11) % 12)] / 12.0;
    }
    return out;
}

FoodRun simulate_food(const std::vector<FoodModel>& cities, const std::vector<FoodRoute>& roads,
                      int years) {
    FoodRun run;
    const std::size_t count = cities.size();
    std::vector<double> stocks(count, 0.0);  // stores start EMPTY

    for (int step = 0; step < years * 12; ++step) {
        const auto month = static_cast<std::size_t>(step % 12);
        const std::vector<double> opening = stocks;

        // Spoilage is charged on the OPENING stock, before the harvest lands: this
        // month's crop has not been sitting in the store long enough to rot.
        std::vector<double> spoiled(count, 0.0);
        std::vector<double> harvest(count, 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            spoiled[i] = stocks[i] * cities[i].spoilage;
            harvest[i] = cities[i].harvest[month];
            stocks[i] = stocks[i] - spoiled[i] + harvest[i];
        }

        // A city protects one month of consumption and one month of store before it will
        // sell anything, which is what stops a granary exporting itself empty.
        std::vector<double> exportable(count, 0.0);
        std::vector<double> budgets(count, 0.0);
        for (std::size_t i = 0; i < count; ++i) {
            exportable[i] = std::max(
                0.0, stocks[i] - cities[i].demand - std::min(cities[i].demand, cities[i].storage));
            budgets[i] = cities[i].transport_budget;
        }

        std::vector<double> imports(count, 0.0);
        std::vector<double> exports(count, 0.0);
        std::vector<double> spent(count, 0.0);
        std::vector<FoodMonthShipment> shipments;
        double loss = 0.0;

        for (std::size_t route_index = 0; route_index < roads.size(); ++route_index) {
            const FoodRoute& edge = roads[route_index];
            if (edge.from < 0 || static_cast<std::size_t>(edge.from) >= count) continue;
            if (edge.to < 0 || static_cast<std::size_t>(edge.to) >= count) continue;
            // ONE capacity, shared and decremented across BOTH directions: a road is a
            // road, not two independent lanes.
            double remaining = edge.capacity[month];

            const std::pair<int, int> directions[2] = {{edge.from, edge.to}, {edge.to, edge.from}};
            for (const auto& [seller_id, buyer_id] : directions) {
                const double efficiency = edge.efficiency;
                const double price = edge.price;
                if (efficiency <= 0.0) continue;
                const auto seller = static_cast<std::size_t>(seller_id);
                const auto buyer = static_cast<std::size_t>(buyer_id);

                const double need = std::max(0.0, cities[buyer].demand - stocks[buyer]);
                const double sent = std::min({exportable[seller], remaining, need / efficiency,
                                              budgets[buyer] / price / efficiency});
                if (sent <= kMonthlyShipmentFloor) continue;

                const double received = sent * efficiency;
                const double charge = received * price;
                stocks[seller] -= sent;
                stocks[buyer] += received;
                exportable[seller] -= sent;
                remaining -= sent;
                budgets[buyer] -= charge;
                spent[buyer] += charge;
                exports[seller] += sent;
                imports[buyer] += received;
                loss += sent - received;

                FoodMonthShipment record;
                record.from = seller_id;
                record.to = buyer_id;
                record.route_index = static_cast<int>(route_index);
                record.sent = sent;
                record.delivered = received;
                record.lost = sent - received;
                record.material_cost = charge;
                shipments.push_back(record);
            }
        }

        FoodMonth out;
        out.year = step / 12 + 1;
        out.month = static_cast<int>(month) + 1;
        out.shipments = std::move(shipments);
        out.transit_loss = loss;
        for (std::size_t i = 0; i < count; ++i) {
            // Consumption precedes overflow: nobody starves beside a full store.
            const double consumed = std::min(cities[i].demand, stocks[i]);
            stocks[i] -= consumed;
            const double overflow = std::max(0.0, stocks[i] - cities[i].storage);
            stocks[i] -= overflow;

            FoodMonthCity city;
            city.opening = opening[i];
            city.harvest = harvest[i];
            city.spoiled = spoiled[i];
            city.imports = imports[i];
            city.exports = exports[i];
            city.material_spent = spent[i];
            city.demand = cities[i].demand;
            city.consumed = consumed;
            city.shortage = std::max(0.0, cities[i].demand - consumed);
            city.overflow = overflow;
            city.closing = stocks[i];
            out.cities.push_back(city);

            out.opening += opening[i];
            out.harvest += harvest[i];
            out.closing += stocks[i];
            out.consumed += consumed;
            out.spoiled += spoiled[i];
            out.overflow += overflow;
        }
        run.months.push_back(std::move(out));
    }
    return run;
}

// ---------------------------------------------------------------------------

HumanHinterland add_humans(const HumansRequest& request) {
    HumanHinterland out;
    if (request.world == nullptr) return out;
    const World& world = *request.world;
    // Without roads there is no culture link, no route to defend and nothing to trade
    // over. A world with zero cities still runs the pass and emits empty lists.
    if (world.effective.phase < 9 || !world.has_roads) return out;

    const Config& cfg = world.effective;
    out.population_profile = cfg.population_profile;
    const int n = cfg.size;
    const auto held = sphere_grid(n, cfg.globe_radius);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();
    const std::vector<SettlementSite>& sites = world.settlements.sites;
    const bool recipe = cfg.world_recipe != 0;

    const std::vector<double> height = nodes_of(world, grid, "height");
    const std::vector<double> water = nodes_of(world, grid, "water_type");
    const std::vector<double> slope = nodes_of(world, grid, "slope");
    const std::vector<double> wet = nodes_of(world, grid, "moisture");
    const std::vector<double> temperature = nodes_of(world, grid, "temperature");
    const std::vector<double> resource = nodes_of(world, grid, "metal_richness");
    const std::vector<double> tpi = nodes_of(world, grid, "tpi");
    const std::vector<double> river = nodes_of(world, grid, "rain_river");
    const std::vector<double> biome = nodes_of(world, grid, "biome");
    std::vector<double> hazard = nodes_of(world, grid, "magic_hazard");

    const FreshwaterAccess water_access = freshwater_access(world, grid);
    const std::vector<double>& fresh = water_access.distance_m;
    const std::vector<double> flood = flood_risk(world, grid, water_access);

    const RoadCost cost(grid, cfg, water, height, flood, river, &hazard);

    // Each people walks by its own rules. The risk fields have to live somewhere stable,
    // because every cost function below holds a reference into this map.
    std::map<std::string, std::vector<double>> risk_by_people;
    const auto risk_for = [&](const std::string& people) -> const std::vector<double>& {
        const auto cached = risk_by_people.find(people);
        if (cached != risk_by_people.end()) return cached->second;
        const std::string layer = "magic_risk_" + people;
        std::vector<double> field = world.layers.count(layer) != 0 ? nodes_of(world, grid, layer) : hazard;
        return risk_by_people.emplace(people, std::move(field)).first->second;
    };

    std::vector<double> distance(count, kUnreachable);
    std::vector<int> owner(count, -1);
    std::vector<int> parent(count, -1);
    std::vector<RoadCost> individual_costs;

    if (recipe) {
        individual_costs.reserve(sites.size());
        for (const SettlementSite& site : sites) {
            const CivilizationProfile& p = profile_or_default(site.population_profile);
            Config theirs = cfg;
            theirs.human_magic_limit = p.traits.mutation_limit;
            theirs.road_max_grade = p.traits.road_grade_limit;
            individual_costs.emplace_back(grid, theirs, water, height, flood, river,
                                          &risk_for(site.population_profile));
        }

        // A frontier RETAINS ITS OWNER'S traversal rules, so a parent path can never jump
        // ownership: the edge cost depends on the label being carried, not on the node.
        //
        // The reference runs a second, unbounded multi-source search before this one and
        // throws every array away. It is skipped here rather than reproduced.
        using Entry = std::tuple<double, int, int>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
        for (const SettlementSite& site : sites) {
            const auto i = static_cast<std::size_t>(site.node);
            owner[i] = site.id;
            distance[i] = 0.0;
            queue.emplace(0.0, site.id, site.node);
        }
        while (!queue.empty()) {
            const auto [value, label, i] = queue.top();
            queue.pop();
            const auto ui = static_cast<std::size_t>(i);
            if (value != distance[ui] || label != owner[ui]) continue;
            for (const SphereNeighbour& nb : grid.neighbours[ui]) {
                const std::optional<double> edge =
                    individual_costs[static_cast<std::size_t>(label)](i, nb.index, nb.distance);
                if (!edge) continue;
                const double candidate = value + *edge;
                const auto j = static_cast<std::size_t>(nb.index);
                // STRICT less-than and no owner tie-break, and bounded at the support
                // reach so everything outside a catchment stays at infinity. Both differ
                // from the shared allocate_access, and both are load-bearing.
                if (candidate < distance[j] && candidate <= cfg.support_reach) {
                    distance[j] = candidate;
                    owner[j] = label;
                    parent[j] = i;
                    queue.emplace(candidate, label, nb.index);
                }
            }
        }
    } else {
        std::vector<std::pair<int, int>> seeds;
        seeds.reserve(sites.size());
        for (const SettlementSite& site : sites) seeds.emplace_back(site.node, site.id);
        const AccessResult access = allocate_access(grid, seeds, cost);
        distance = access.distance;
        owner = access.owner;
        parent = access.parent;
    }

    std::vector<const CivilizationProfile*> site_profiles;
    site_profiles.reserve(sites.size());
    for (const SettlementSite& site : sites) site_profiles.push_back(&profile_or_default(site.population_profile));

    const CivilizationProfile& default_profile = profile_or_default(cfg.population_profile);
    std::vector<const CivilizationProfile*> node_profiles(count, &default_profile);
    for (std::size_t i = 0; i < count; ++i) {
        if (owner[i] >= 0) node_profiles[i] = site_profiles[static_cast<std::size_t>(owner[i])];
    }

    if (recipe) {
        // Re-read the hazard through each cell's OWNER's eyes. This happens after the cost
        // functions were built, so it does not change traversal — it only changes the crop
        // loss below.
        for (std::size_t i = 0; i < count; ++i) {
            hazard[i] = risk_for(std::string(node_profiles[i]->id))[i];
        }
    }

    // --- cultures ----------------------------------------------------------
    // Only a road between two cities of the SAME people can carry a culture, so two
    // peoples sharing a road never share a culture.
    std::vector<CultureLink> links;
    for (const Road& road : world.roads.routes) {
        const auto a = static_cast<std::size_t>(road.from);
        const auto b = static_cast<std::size_t>(road.to);
        if (a >= sites.size() || b >= sites.size()) continue;
        if (sites[a].population_profile != sites[b].population_profile) continue;
        links.push_back({road.from, road.to, road.cost});
    }
    const std::vector<int> groups = culture_groups(static_cast<int>(sites.size()), links, cfg.culture_link_cost);

    std::map<int, std::string> culture_ids;
    for (const int g : groups) {
        if (culture_ids.count(g) != 0) continue;
        const std::string domain = "culture-" + std::to_string(g);
        culture_ids[g] = std::string(sites[static_cast<std::size_t>(g)].population_profile) + "-" +
                         hex8(child_seed(cfg.seed, domain));
    }
    for (const auto& [g, id] : culture_ids) {
        CultureGroup culture;
        culture.index = g;
        culture.id = id;
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if (groups[i] == g) culture.city_ids.push_back(static_cast<int>(i));
        }
        culture.population_profile = sites[static_cast<std::size_t>(g)].population_profile;
        culture.civilization_id = culture.population_profile;
        culture.architecture_style_id = culture.population_profile;
        out.cultures.push_back(std::move(culture));
    }

    // --- region layers -----------------------------------------------------
    // Territory stops at the support reach; wilderness stays unassigned.
    std::map<std::string, int, std::less<>> entity_index;
    {
        int position = 0;
        for (const std::string_view id : civilization_ids()) entity_index.emplace(std::string(id), position++);
    }
    std::vector<double> region(count, -1.0);
    std::vector<double> civilization_region(count, -1.0);
    for (std::size_t i = 0; i < count; ++i) {
        if (owner[i] < 0 || distance[i] > cfg.support_reach) continue;
        region[i] = static_cast<double>(groups[static_cast<std::size_t>(owner[i])]);
        const auto found = entity_index.find(sites[static_cast<std::size_t>(owner[i])].population_profile);
        if (found != entity_index.end()) civilization_region[i] = static_cast<double>(found->second);
    }

    // --- per-node food and material ----------------------------------------
    // Relative exportable potential after rural subsistence, not people or tonnes a year.
    const auto variant_layer = world.layers.find("biome_variant");
    const std::span<const BiomeVariant> catalogue = biome_variant_catalogue();
    std::vector<double> natural(count, 0.0);
    std::vector<double> food(count, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const CivilizationProfile& p = *node_profiles[i];
        if (water[i] == 0.0) {
            const double adaptation =
                cfg.population_profile == "mixed" ? p.traits.irrigation : cfg.human_adaptation;
            const FarmingPotential yields = farming_potential(slope[i], temperature[i], wet[i], flood[i],
                                                             fresh[i], adaptation, p.traits);
            natural[i] = yields.natural;
            food[i] = yields.irrigated;
        }

        // The exact magical state where there is one. Storing the natural biome as an
        // integer is deliberate: the reference keys its table by a decimal STRING, and a
        // biome layer held as a double silently misses every lookup.
        std::string_view variant;
        if (variant_layer != world.layers.end()) {
            const int index = static_cast<int>(variant_layer->second.at(grid.point_x[i], grid.point_z[i]));
            if (index >= 0 && static_cast<std::size_t>(index) < catalogue.size()) {
                variant = catalogue[static_cast<std::size_t>(index)].id;
            }
        }
        const double multiplier = biome_food_multiplier(p, static_cast<int>(biome[i]), variant);
        // Mutation and the people's own opinion of the ground both reduce crop surplus.
        natural[i] *= multiplier * (1.0 - hazard[i]);
        food[i] *= multiplier * (1.0 - hazard[i]);
    }

    // --- player-founded rural rows -----------------------------------------
    std::vector<RuralSite> player_hamlets;
    std::vector<RuralSite> player_fortresses;
    for (const RuralSite& row : request.previous_hamlets) {
        if (row.founded_by == "player") player_hamlets.push_back(row);
    }
    for (const RuralSite& row : request.previous_fortresses) {
        if (row.founded_by == "player") player_fortresses.push_back(row);
    }
    const auto by_node = [](const RuralSite& a, const RuralSite& b) { return a.node < b.node; };
    std::sort(player_hamlets.begin(), player_hamlets.end(), by_node);
    std::sort(player_fortresses.begin(), player_fortresses.end(), by_node);

    std::set<int> city_nodes;
    for (const SettlementSite& site : sites) city_nodes.insert(site.node);
    std::set<int> player_nodes;
    for (const RuralSite& row : player_hamlets) player_nodes.insert(row.node);
    for (const RuralSite& row : player_fortresses) player_nodes.insert(row.node);

    std::vector<int> occupied;
    occupied.reserve(sites.size() + player_nodes.size() + request.previous_fortresses.size());
    for (const SettlementSite& site : sites) occupied.push_back(site.node);
    for (const int node : player_nodes) occupied.push_back(node);

    // --- forts that stand ---------------------------------------------------
    // A fort keeps its ground while the city that garrisons it stands (ruled 2026-09-23): it
    // is carried rather than re-proposed, and falls with its city. Its ground can still be
    // lost -- drowned, or built over by a city or a player's site. The nodes join the
    // occupied set before the hamlets are placed, so nothing new is sited on a fort.
    std::map<std::string, int, std::less<>> city_by_uid;
    for (const SettlementSite& site : sites) city_by_uid.emplace(site.uid, site.id);
    std::vector<RuralSite> standing;
    std::set<int> standing_nodes;
    for (const RuralSite& row : request.previous_fortresses) {
        if (row.founded_by == "player" || row.watch.empty()) continue;
        const auto city = city_by_uid.find(row.core_uid);
        if (city == city_by_uid.end()) continue;  // its city fell, and the fort with it
        if (row.node < 0 || static_cast<std::size_t>(row.node) >= count) continue;
        if (water[static_cast<std::size_t>(row.node)] != 0.0) continue;
        if (city_nodes.count(row.node) != 0 || player_nodes.count(row.node) != 0) continue;
        if (!standing_nodes.insert(row.node).second) continue;
        RuralSite fort = row;
        const auto garrison = static_cast<std::size_t>(city->second);
        fort.core_id = city->second;
        fort.population_profile = sites[garrison].population_profile;
        fort.culture_id = culture_ids[groups[garrison]];
        standing.push_back(std::move(fort));
    }
    for (const int node : standing_nodes) occupied.push_back(node);

    std::vector<Vec3> vectors(count);
    for (std::size_t i = 0; i < count; ++i) vectors[i] = direction(grid.point_x[i], grid.point_z[i], n);

    const double radius = cfg.globe_radius;
    const auto separated = [&](int i, double minimum) {
        const Vec3& a = vectors[static_cast<std::size_t>(i)];
        for (const int j : occupied) {
            const Vec3& b = vectors[static_cast<std::size_t>(j)];
            const double dot = std::max(-1.0, std::min(1.0, a.x * b.x + a.y * b.y + a.z * b.z));
            if (radius * std::acos(dot) < minimum) return false;
        }
        return true;
    };

    const auto record = [&](int i, const char* kind, std::size_t number, std::string reason) {
        RuralSite site;
        const int core = owner[static_cast<std::size_t>(i)];
        site.id = std::string(kind) + "-" + std::to_string(number);
        site.uid = std::string(kind) + "-node-" + std::to_string(i);
        site.kind = kind;
        site.node = i;
        site.x = grid.point_x[static_cast<std::size_t>(i)];
        site.z = grid.point_z[static_cast<std::size_t>(i)];
        site.core_id = core;
        if (core >= 0) {
            site.core_uid = sites[static_cast<std::size_t>(core)].uid;
            site.population_profile = sites[static_cast<std::size_t>(core)].population_profile;
            site.culture_id = culture_ids[groups[static_cast<std::size_t>(core)]];
        }
        site.raised_age = world.age;
        site.height_m = height[static_cast<std::size_t>(i)];
        site.access_cost = distance[static_cast<std::size_t>(i)];
        site.access_nodes.push_back(i);
        while (parent[static_cast<std::size_t>(site.access_nodes.back())] >= 0) {
            site.access_nodes.push_back(parent[static_cast<std::size_t>(site.access_nodes.back())]);
        }
        site.reason = std::move(reason);
        return site;
    };

    // A fort garrisoned from a city found by a search of that city's whole land: a frontier
    // fort, which may stand past any one city's support reach, so the territory's owner and
    // access path are not its garrison's.
    const auto garrisoned_record = [&](int i, int city, const AccessResult& land, std::string reason) {
        RuralSite site;
        const auto c = static_cast<std::size_t>(city);
        const auto ui = static_cast<std::size_t>(i);
        site.uid = "fortress-node-" + std::to_string(i);
        site.kind = "fortress";
        site.node = i;
        site.x = grid.point_x[ui];
        site.z = grid.point_z[ui];
        site.core_id = city;
        site.core_uid = sites[c].uid;
        site.population_profile = sites[c].population_profile;
        site.culture_id = culture_ids[groups[c]];
        site.height_m = height[ui];
        site.access_cost = land.distance[ui];
        site.access_nodes.push_back(i);
        while (land.parent[static_cast<std::size_t>(site.access_nodes.back())] >= 0) {
            site.access_nodes.push_back(land.parent[static_cast<std::size_t>(site.access_nodes.back())]);
        }
        site.reason = std::move(reason);
        site.raised_age = world.age;
        return site;
    };

    // Re-seat a player row in this pass's numbering and catchments.
    //
    // The ordinal id is reissued because it is a position in a list this pass rebuilt, and
    // the access path, owning city and culture are recomputed because they describe the
    // world as it is now. The uid, the node and the player's own choices are not:
    // re-deriving those would move a settlement under its owner. A node outside every
    // catchment keeps the core it was founded against rather than being dropped.
    const auto adopt = [&](const RuralSite& row, const char* kind, std::size_t number) {
        RuralSite reseated;
        if (owner[static_cast<std::size_t>(row.node)] >= 0) {
            reseated = record(row.node, kind, number,
                              row.reason.empty() ? std::string("Founded by the player.") : row.reason);
        } else {
            reseated = row;
            reseated.id = std::string(kind) + "-" + std::to_string(number);
        }
        reseated.uid = row.uid;
        reseated.founded_by = row.founded_by;
        reseated.founded_age = row.founded_age;
        reseated.raised_age = row.founded_age;
        reseated.watch = row.watch.empty() && std::string_view(kind) == "fortress" ? std::string("player") : row.watch;
        reseated.name = row.name;
        if (!row.population_profile.empty()) reseated.population_profile = row.population_profile;
        if (!row.role.empty()) reseated.role = row.role;
        reseated.irrigation_benefit = row.irrigation_benefit;
        reseated.worked_area_km2 = row.worked_area_km2;
        reseated.delivered_food = row.delivered_food;
        reseated.delivered_materials = row.delivered_materials;
        reseated.defence_score = row.defence_score;
        reseated.protected_route_node = row.protected_route_node;
        return reseated;
    };

    // --- fortresses --------------------------------------------------------
    // Placed BEFORE the hamlets (2026-09-23): a fort claims the strategic ground and the farms
    // settle around it. Placed after them, the hamlets had already taken the few road nodes
    // in a city's territory, and most cities that asked for a fort got none.
    // Two watches (ruled 2026-09-23). A capital, and a city that has fought, each hold one
    // fort on the most defensible junction or crossing of their own roads. Two civilizations
    // joined by a road hold one fort on the high ground beside the narrowest point of the
    // natural overland route between their lands. A fort that stands answers its ask, so a
    // rebuild raises only what is newly asked, and fortress_count caps what is raised and
    // never raises it. No two forts stand closer than the reference's spacing, scaled with
    // the world as the support reach is.
    const double reach_factor = scale::reach_scale(2.0 * kPi * cfg.globe_radius);
    const double fort_spacing = kFortressSeparationM * reach_factor;
    const double fort_near = kFortressNearM * reach_factor;

    // Route junctions, river crossings and elevated surroundings suggest defensive sites.
    // Insertion order matters: a tie on the score below is kept by the FIRST writer.
    std::vector<int> route_order;
    std::unordered_map<int, std::size_t> route_slot;
    std::vector<std::set<int>> route_links;
    const auto touch = [&](int node) -> std::set<int>& {
        const auto found = route_slot.find(node);
        if (found != route_slot.end()) return route_links[found->second];
        route_slot.emplace(node, route_links.size());
        route_order.push_back(node);
        route_links.emplace_back();
        return route_links.back();
    };

    std::set<int> crossings;
    std::set<std::pair<int, int>> crossing_edges;
    double road_length = 0.0;
    std::vector<char> on_road(sites.size(), 0);
    using Frontier = std::pair<std::string, std::string>;
    std::set<Frontier> frontiers;
    for (const Road& road : world.roads.routes) {
        for (std::size_t k = 0; k + 1 < road.nodes.size(); ++k) {
            touch(road.nodes[k]).insert(road.nodes[k + 1]);
            touch(road.nodes[k + 1]).insert(road.nodes[k]);
        }
        for (const auto& [a, b] : road.river_crossings) {
            crossings.insert(a);
            crossings.insert(b);
            crossing_edges.emplace(std::min(a, b), std::max(a, b));
        }
        road_length += road.length_m;

        // Two peoples a road joins are neighbours: an army goes where the traders go.
        const auto a = static_cast<std::size_t>(road.from);
        const auto b = static_cast<std::size_t>(road.to);
        if (a >= sites.size() || b >= sites.size()) continue;
        on_road[a] = 1;
        on_road[b] = 1;
        const std::string& pa = sites[a].population_profile;
        const std::string& pb = sites[b].population_profile;
        if (pa != pb) frontiers.emplace(std::min(pa, pb), std::max(pa, pb));
    }

    // Who asks for a road fort: a capital, and a city that has fought -- once, however many
    // wars. A city no road reaches has nothing for one to watch.
    std::vector<char> asks(sites.size(), 0);
    int capitals = 0;
    int veteran_cities = 0;
    int veteran_asks = 0;
    int road_asks = 0;
    for (std::size_t i = 0; i < sites.size(); ++i) {
        const bool capital = sites[i].city_class == CityClass::Capital;
        const bool fought = i < request.war_history.size() && request.war_history[i] > 0;
        capitals += capital ? 1 : 0;
        veteran_cities += fought ? 1 : 0;
        if (!on_road[i] || !(capital || fought)) continue;
        asks[i] = 1;
        ++road_asks;
        veteran_asks += capital ? 0 : 1;
    }

    // What the forts that stand already answer.
    std::set<int> garrisoned;
    std::set<Frontier> watched;
    for (const RuralSite& fort : standing) {
        if (fort.watch == "road") garrisoned.insert(fort.core_id);
        if (fort.watch == "frontier") watched.emplace(fort.frontier_a, fort.frontier_b);
    }
    int unmet = 0;
    for (std::size_t i = 0; i < sites.size(); ++i) {
        if (asks[i] != 0 && garrisoned.count(static_cast<int>(i)) == 0) ++unmet;
    }
    for (const Frontier& frontier : frontiers) {
        if (watched.count(frontier) == 0) ++unmet;
    }
    const int carried = static_cast<int>(standing.size());
    const int fortress_limit = std::max(0, std::min(cfg.fortress_count - carried, unmet));
    std::vector<RuralSite> raised;
    const auto room = [&] { return static_cast<int>(raised.size()) < fortress_limit; };

    // Road forts. A candidate is a road node or a neighbour of one, on dry ground its owner
    // works, in the territory of a city that asks and has no road fort standing. Each such
    // city takes the best candidate it owns that keeps the spacing, the best sites first.
    std::map<int, std::pair<double, int>> strategic;
    for (std::size_t slot = 0; slot < route_order.size(); ++slot) {
        const int node = route_order[slot];
        const std::set<int>& neighbours = route_links[slot];
        const double base = 1.0 + std::min(2.0, std::max(0.0, static_cast<double>(neighbours.size()) - 2.0)) +
                            (crossings.count(node) != 0 ? 1.0 : 0.0);

        std::vector<std::pair<int, double>> around;
        around.emplace_back(node, 0.0);
        for (const SphereNeighbour& nb : grid.neighbours[static_cast<std::size_t>(node)]) {
            around.emplace_back(nb.index, nb.distance);
        }

        for (const auto& [i, d] : around) {
            const auto ui = static_cast<std::size_t>(i);
            if (water[ui] != 0.0) continue;
            if (slope[ui] >= node_profiles[ui]->traits.work_slope_limit) continue;
            if (owner[ui] < 0 || distance[ui] > cfg.support_reach) continue;
            if (asks[static_cast<std::size_t>(owner[ui])] == 0 || garrisoned.count(owner[ui]) != 0) continue;
            // The GLOBAL cost function, not the owner's: a fort has to stand where any
            // road could reach it.
            if (i != node && !cost(node, i, d)) continue;

            const double value = base + std::max(-0.5, std::min(1.0, tpi[ui] / 30.0)) - 0.5 * flood[ui] -
                                 slope[ui] / 40.0 - d / fort_near;
            const auto found = strategic.find(i);
            if (found == strategic.end() || value > found->second.first) strategic[i] = {value, node};
        }
    }

    std::vector<std::pair<int, std::pair<double, int>>> road_sites(strategic.begin(), strategic.end());
    std::sort(road_sites.begin(), road_sites.end(), [](const auto& a, const auto& b) {
        return a.second.first != b.second.first ? a.second.first > b.second.first : a.first < b.first;
    });
    std::set<int> served;
    for (const auto& [i, scored] : road_sites) {
        if (!room()) break;
        const int city = owner[static_cast<std::size_t>(i)];
        if (served.count(city) != 0) continue;
        if (!separated(i, fort_spacing)) continue;
        RuralSite fort = record(i, "fortress", 0, "Its city's road; junction/crossing importance and elevated surroundings");
        fort.watch = "road";
        fort.defence_score = scored.first;
        fort.protected_route_node = scored.second;
        raised.push_back(std::move(fort));
        occupied.push_back(i);
        served.insert(city);
    }
    // Frontier forts. Each civilization's lands are searched once, from all its cities, over
    // the ground an army crosses -- the shared road cost, which refuses water, cliffs and
    // unsafe magic whoever marches -- and the fort takes the best ground beside the
    // narrowest crossing it can still be built at, garrisoned from the nearer land's
    // nearest city. Unbounded: roads are laid by each people's own rules, so a road's cost
    // does not bound the shared route.
    std::map<std::string, AccessResult, std::less<>> lands;
    const auto land_of = [&](const std::string& people) -> const AccessResult& {
        if (const auto found = lands.find(people); found != lands.end()) return found->second;
        std::vector<std::pair<int, int>> seeds;
        for (const SettlementSite& site : sites) {
            if (site.population_profile == people) seeds.emplace_back(site.node, site.id);
        }
        return lands.emplace(people, allocate_access(grid, seeds, cost)).first->second;
    };
    for (const Frontier& frontier : frontiers) {
        if (!room()) break;
        if (watched.count(frontier) != 0) continue;
        const AccessResult& a = land_of(frontier.first);
        const AccessResult& b = land_of(frontier.second);
        const std::vector<Crossing> crossings_between = narrowest_crossings(
            a.distance, b.distance, grid.areas, kFrontierSlack, kFrontierBandLow, kFrontierBandHigh, kFrontierSlices);
        bool placed = false;
        for (const Crossing& crossing : crossings_between) {
            const int choke = crossing.node;
            const auto uc = static_cast<std::size_t>(choke);
            std::vector<std::pair<int, double>> around;
            around.emplace_back(choke, 0.0);
            for (const SphereNeighbour& nb : grid.neighbours[uc]) around.emplace_back(nb.index, nb.distance);

            std::vector<std::pair<double, int>> ground;
            for (const auto& [i, d] : around) {
                const auto ui = static_cast<std::size_t>(i);
                if (water[ui] != 0.0) continue;
                if (i != choke && !cost(choke, i, d)) continue;
                const AccessResult& side = a.distance[ui] <= b.distance[ui] ? a : b;
                const int city = side.owner[ui];
                if (city < 0 || !std::isfinite(side.distance[ui])) continue;
                if (slope[ui] >= site_profiles[static_cast<std::size_t>(city)]->traits.work_slope_limit) continue;
                const double value = kFrontierBase + std::max(-0.5, std::min(1.0, tpi[ui] / 30.0)) +
                                     std::max(0.0, std::min(1.0, (height[ui] - height[uc]) / kOverlookM)) -
                                     0.5 * flood[ui] - slope[ui] / 40.0 - d / fort_near;
                ground.emplace_back(value, i);
            }
            std::sort(ground.begin(), ground.end(), [](const auto& x, const auto& y) {
                return x.first != y.first ? x.first > y.first : x.second < y.second;
            });

            for (const auto& [value, i] : ground) {
                if (!separated(i, fort_spacing)) continue;
                const auto ui = static_cast<std::size_t>(i);
                const AccessResult& side = a.distance[ui] <= b.distance[ui] ? a : b;
                RuralSite fort = garrisoned_record(i, side.owner[ui], side,
                                                   "High ground beside the narrowest point of the natural route "
                                                   "between the " + frontier.first + " and " + frontier.second +
                                                       " lands");
                fort.watch = "frontier";
                fort.frontier_a = frontier.first;
                fort.frontier_b = frontier.second;
                fort.corridor_km2 = crossing.width_km2;
                fort.defence_score = value;
                fort.protected_route_node = choke;
                raised.push_back(std::move(fort));
                occupied.push_back(i);
                placed = true;
                break;
            }
            if (placed) break;
        }
    }

    // The list: what stands, in the order it was raised, then this pass's forts, then the
    // player's -- not proposals, so appended after the ceiling rather than under it. The
    // ordinals are this list's positions, reissued on every pass.
    out.fortresses = std::move(standing);
    for (RuralSite& fort : raised) out.fortresses.push_back(std::move(fort));
    for (const RuralSite& row : player_fortresses) out.fortresses.push_back(adopt(row, "fortress", 0));
    for (std::size_t k = 0; k < out.fortresses.size(); ++k) out.fortresses[k].id = "fortress-" + std::to_string(k);

    // --- hamlets -----------------------------------------------------------
    for (const SettlementSite& site : sites) {
        const CivilizationProfile& p = *site_profiles[static_cast<std::size_t>(site.id)];
        std::vector<int> eligible;
        for (std::size_t i = 0; i < count; ++i) {
            if (owner[i] != site.id) continue;
            if (distance[i] < kHamletMinAccessCost || distance[i] > cfg.support_reach) continue;
            if (water[i] != 0.0) continue;
            if (slope[i] >= p.traits.work_slope_limit) continue;
            eligible.push_back(static_cast<int>(i));
        }

        int hamlet_limit = cfg.hamlets_per_core;
        if (cfg.auto_parameters != 0) {
            const double wanted =
                std::ceil(static_cast<double>(site.rural_population_estimate) / kResidentsPerHamlet);
            hamlet_limit = std::min(hamlet_limit, static_cast<int>(wanted));
        }

        for (int k = 0; k < hamlet_limit; ++k) {
            // Two farms then a quarry, per city: a hinterland is not all one thing.
            const bool farming = k % 3 != 2;
            const auto score = [&](int i) {
                const auto ui = static_cast<std::size_t>(i);
                const double base = farming ? food[ui] : resource[ui] * (1.0 - flood[ui]);
                return base * std::exp(-distance[ui] / cfg.support_reach);
            };

            // The eligible list is deliberately NOT re-filtered between rounds; the
            // separation test against the growing occupied list is what stops a node from
            // being taken twice.
            std::vector<int> ranked = eligible;
            std::sort(ranked.begin(), ranked.end(), [&score](int a, int b) {
                const double sa = score(a);
                const double sb = score(b);
                return sa != sb ? sa > sb : a < b;
            });

            for (const int i : ranked) {
                if (score(i) < kHamletScoreFloor) break;  // nothing left worth settling
                if (!separated(i, kHamletSeparationM)) continue;
                RuralSite hamlet = record(i, "hamlet", out.hamlets.size(),
                                          std::string("Reachable ") + (farming ? "farming" : "resource") +
                                              " potential with low transport cost");
                hamlet.role = farming ? "farming" : "resource";
                hamlet.irrigation_benefit =
                    food[static_cast<std::size_t>(i)] - natural[static_cast<std::size_t>(i)];
                out.hamlets.push_back(std::move(hamlet));
                occupied.push_back(i);
                break;
            }
        }
    }

    // Adopted BEFORE the catchment allocation below, so a player hamlet works land exactly
    // as a generated one does. The accumulators start at zero for the same reason a
    // generated hamlet's do — the allocation fills them — and the irrigation benefit is a
    // read of THIS pass's ground, because it describes the cell and not the settlement.
    for (const RuralSite& row : player_hamlets) {
        RuralSite adopted = adopt(row, "hamlet", out.hamlets.size());
        const auto i = static_cast<std::size_t>(row.node);
        adopted.irrigation_benefit = food[i] - natural[i];
        adopted.worked_area_km2 = 0.0;
        adopted.delivered_food = 0.0;
        adopted.delivered_materials = 0.0;
        out.hamlets.push_back(std::move(adopted));
    }

    // --- catchments and delivery -------------------------------------------
    // Each productive cell belongs to at most one hamlet and cannot cross city catchments.
    const auto farm_cost = [&](int i, int j, double d) -> std::optional<double> {
        const auto ui = static_cast<std::size_t>(i);
        const auto uj = static_cast<std::size_t>(j);
        if (owner[ui] != owner[uj] || distance[uj] > cfg.support_reach) return std::nullopt;
        if (recipe && owner[ui] >= 0) return individual_costs[static_cast<std::size_t>(owner[ui])](i, j, d);
        return cost(i, j, d);
    };

    std::vector<std::pair<int, int>> hamlet_seeds;
    hamlet_seeds.reserve(out.hamlets.size());
    for (std::size_t k = 0; k < out.hamlets.size(); ++k) {
        hamlet_seeds.emplace_back(out.hamlets[k].node, static_cast<int>(k));
    }
    const AccessResult farm = allocate_access(grid, hamlet_seeds, farm_cost, kCatchmentReach);

    for (std::size_t i = 0; i < count; ++i) {
        const int k = farm.owner[i];
        if (k < 0) continue;
        // A city does not farm the ground it stands on, and nobody works a slope their
        // people will not work.
        if (city_nodes.count(static_cast<int>(i)) != 0) continue;
        if (slope[i] >= node_profiles[i]->traits.work_slope_limit) continue;

        RuralSite& hamlet = out.hamlets[static_cast<std::size_t>(k)];
        const double area = grid.areas[i] / 1e6;
        const double delivery = std::exp(-hamlet.access_cost / cfg.support_reach);
        const bool farming = hamlet.role == "farming";
        hamlet.worked_area_km2 += area;
        hamlet.delivered_food += kResidentsPerProductiveKm2 * area * food[i] * delivery *
                                 (farming ? 1.0 : kOffSpecialityRate);
        hamlet.delivered_materials += kResidentsPerProductiveKm2 * area * resource[i] * delivery *
                                      (farming ? kOffSpecialityRate : 1.0);
    }


    // --- cores and trade ---------------------------------------------------
    for (const SettlementSite& site : sites) {
        CityCore core;
        core.site_id = site.id;
        core.population_profile = site.population_profile;
        core.culture_id = culture_ids[groups[static_cast<std::size_t>(site.id)]];
        for (const RuralSite& hamlet : out.hamlets) {
            if (hamlet.core_id != site.id) continue;
            core.hamlet_ids.push_back(hamlet.id);
            core.food_supply += hamlet.delivered_food;
            core.material_supply += hamlet.delivered_materials;
        }
        for (const RuralSite& fort : out.fortresses) {
            if (fort.core_id == site.id) core.fortress_ids.push_back(fort.id);
        }
        core.food_demand = site_profiles[static_cast<std::size_t>(site.id)]->traits.food_demand *
                           static_cast<double>(site.urban_population_estimate) / 100.0;
        out.cores.push_back(std::move(core));
    }
    out.shipments = exchange_food(out.cores, world.roads.routes);

    // --- layers and the block ----------------------------------------------
    std::vector<double> benefit(count, 0.0);
    std::vector<double> catchment(count, -1.0);
    for (std::size_t i = 0; i < count; ++i) {
        benefit[i] = food[i] - natural[i];
        catchment[i] = static_cast<double>(farm.owner[i]);
    }
    out.food_potential = grid.to_grid(food);
    out.natural_food_potential = grid.to_grid(natural);
    out.irrigation_benefit = grid.to_grid(benefit);
    out.culture_region = grid.to_grid(region);
    out.civilization_region = grid.to_grid(civilization_region);
    out.hamlet_catchment = grid.to_grid(catchment);

    FortressDemand& demand = out.fortress_demand;
    demand.proposed = unmet;
    demand.city_ceiling = road_asks;
    demand.requested_ceiling = cfg.fortress_count;
    demand.limit = fortress_limit;
    demand.player_founded = static_cast<int>(player_fortresses.size());
    demand.veteran_demand = veteran_asks;
    demand.veteran_cities = veteran_cities;
    demand.crossings = static_cast<int>(crossing_edges.size());
    demand.road_length_m = road_length;
    demand.support_reach = cfg.support_reach;
    demand.capitals = capitals;
    demand.frontiers = static_cast<int>(frontiers.size());
    demand.carried = carried;
    for (const RuralSite& fort : out.fortresses) {
        demand.road_forts += fort.watch == "road" ? 1 : 0;
        demand.frontier_forts += fort.watch == "frontier" ? 1 : 0;
    }
    demand.spacing_m = fort_spacing;
    // What no fort answers: an asking city with no road fort, a frontier with none. Usable
    // ground and the spacing decide this; the reader should not have to infer it.
    std::set<int> holding;
    std::set<Frontier> kept_watch;
    for (const RuralSite& fort : out.fortresses) {
        if (fort.watch == "road") holding.insert(fort.core_id);
        if (fort.watch == "frontier") kept_watch.emplace(fort.frontier_a, fort.frontier_b);
    }
    for (std::size_t i = 0; i < sites.size(); ++i) {
        if (asks[i] != 0 && holding.count(static_cast<int>(i)) == 0) ++demand.unanswered;
    }
    for (const Frontier& frontier : frontiers) {
        if (kept_watch.count(frontier) == 0) ++demand.unanswered;
    }

    out.method =
        "All primary pins are cities. Rural sites share exclusive reachable catchments. Food/material "
        "values are relative exportable potential units, not historical yields or population capacity. "
        "Irrigation requires nearby mapped freshwater; water extraction capacity and groundwater are not "
        "simulated. Cities can buy finite surplus over roads using material potential, with transport "
        "loss; remaining shortages stay visible.";
    if (world.has_magic) {
        out.method +=
            " Mutation and the selected population biome food multipliers reduce crop surplus. Rural "
            "access cannot cross unsafe magic.";
    }
    out.culture_method =
        "Existing road links below a cost threshold form single-link interaction groups. Culture IDs are "
        "seed-local, not inferred ethnicities or political borders; architecture style keys identify "
        "standalone civilizations. Territory stops at the support reach; wilderness remains unassigned.";
    out.defence_method =
        "Fortresses are spaced strategic proposals, not a siege or visibility simulation. Every capital and "
        "every city that has fought a war holds one road fort on the most defensible junction or crossing "
        "of its own roads. Every two civilizations joined by a road hold one frontier fort on the high "
        "ground beside the narrowest point of the natural overland route between their lands -- a pass, a "
        "land bridge, a ford -- found where the corridor of routes within 10% of the cheapest covers the "
        "least ground. A fort stands through age turns while the city that garrisons it stands; a new age "
        "raises only what its cities and frontiers newly ask. fortress_count caps what is raised and never "
        "raises it. No two forts stand closer than the reference's 200 m scaled with the world, as the "
        "support reach is. Garrison demand is not yet budgeted.";
    return out;
}

// ---------------------------------------------------------------------------

SeasonalFood add_seasonal_food(const SeasonalFoodRequest& request) {
    SeasonalFood out;
    if (request.world == nullptr || request.humans == nullptr) return out;
    const World& world = *request.world;
    const HumanHinterland& humans = *request.humans;
    if (humans.cores.empty()) return out;

    const Config& cfg = world.effective;
    const int n = cfg.size;
    const std::vector<SettlementSite>& sites = world.settlements.sites;
    const double seasonality = cfg.world_recipe != 0 ? cfg.options[Option::seasonality] : 1.0;

    const auto layer_at = [&world](const char* name, int x, int z) {
        const auto it = world.layers.find(name);
        return it != world.layers.end() ? it->second.at(x, z) : 0.0;
    };

    for (std::size_t k = 0; k < humans.cores.size() && k < sites.size(); ++k) {
        const CityCore& core = humans.cores[k];
        const SettlementSite& site = sites[k];
        const CivilizationProfile& p = profile_or_default(site.population_profile);

        FoodModel model;
        model.site_id = site.id;
        for (const RuralSite& hamlet : humans.hamlets) {
            if (hamlet.core_id != site.id) continue;
            const double latitude = 90.0 - 180.0 * hamlet.z / (n - 1);
            const std::array<double, 12> annual = seasonal_harvest(
                hamlet.delivered_food, layer_at("moisture", hamlet.x, hamlet.z), latitude,
                layer_at("temperature", hamlet.x, hamlet.z), p.traits.food_temperature_tolerance,
                p.traits.food_temperature_ideal, seasonality);
            for (std::size_t m = 0; m < 12; ++m) model.harvest[m] += annual[m];
        }
        model.demand = core.food_demand / 12.0;
        // Materials imply storage construction and maintenance; humidity and heat drive
        // spoilage.
        const double storage_months = std::min(6.0, core.material_supply / std::max(core.food_demand, 0.01) * 3.0);
        model.storage = model.demand * storage_months;
        model.spoilage = std::min(0.08, 0.005 + 0.035 * site.moisture * clamp01((site.temperature_c + 5.0) / 35.0));
        model.transport_budget = core.material_supply / 12.0;
        out.models.push_back(std::move(model));
    }

    for (const Road& road : world.roads.routes) {
        const auto a = static_cast<std::size_t>(road.from);
        const auto b = static_cast<std::size_t>(road.to);
        if (a >= out.models.size() || b >= out.models.size()) continue;
        const double mean = (sites[a].temperature_c + sites[b].temperature_c) / 2.0;
        const double latitude = 90.0 - 90.0 * (sites[a].z + sites[b].z) / (n - 1);
        const double base =
            (out.models[a].demand + out.models[b].demand) / (1.0 + road.cost / cfg.support_reach);

        FoodRoute route;
        route.from = road.from;
        route.to = road.to;
        for (int month = 0; month < 12; ++month) {
            // Route winter conditions use endpoint climate rather than every pass.
            const double temp = mean + 15.0 * std::sin(latitude * kPi / 180.0) *
                                           std::cos(2.0 * kPi * (month - 6) / 12.0);
            const double winter = clamp01((temp + 15.0) / 20.0);
            route.capacity[static_cast<std::size_t>(month)] =
                base * winter / (1.0 + 0.1 * static_cast<double>(road.river_crossings.size()));
        }
        route.efficiency = std::exp(-road.cost / kTradeEfficiencyScale);
        route.price = kTradePriceFloor + road.cost / kTradePriceScale;
        out.routes.push_back(route);
    }

    const FoodRun run = simulate_food(out.models, out.routes, out.years);
    out.months = run.months;

    const std::size_t final_start = run.months.size() >= 12 ? run.months.size() - 12 : 0;
    for (std::size_t i = 0; i < out.models.size(); ++i) {
        SeasonalCity summary;
        summary.site_id = static_cast<int>(i);
        double shortage = 0.0;
        double demand = 0.0;
        double harvest = 0.0;
        for (std::size_t m = final_start; m < run.months.size(); ++m) {
            const FoodMonthCity& v = run.months[m].cities[i];
            shortage += v.shortage;
            demand += v.demand;
            harvest += v.harvest;
            if (v.shortage > kShortageEpsilon) {
                summary.lean_months.push_back(static_cast<int>(m - final_start) + 1);
            }
        }
        const double delta = run.months.back().cities[i].closing - run.months[final_start].cities[i].opening;
        summary.annual_demand = demand;
        summary.annual_harvest = harvest;
        summary.annual_shortage = shortage;
        summary.food_coverage = demand != 0.0 ? 1.0 - shortage / demand : 1.0;
        summary.reserve_change = delta;
        summary.ending_reserve = run.months.back().cities[i].closing;
        summary.passes_final_year = shortage < kShortageEpsilon && delta >= -kShortageEpsilon;
        summary.urban_food_equivalent = static_cast<long long>(std::floor(
            static_cast<double>(sites[i].urban_population_estimate) * summary.food_coverage));
        out.cities.push_back(std::move(summary));
    }

    out.method =
        "Four repeated climatic years from empty stores; year 4 is a stress-test observation, not proof "
        "of equilibrium. Monthly mean thermal growth and a one-month harvest lag use existing rural "
        "exportable surplus after rural subsistence. Storage and monthly transport budgets derive from "
        "material potential; humidity/heat cause spoilage. Direct road neighbours trade finite surplus, "
        "protecting one month of donor reserve; shared edge throughput, winter penalties, transport loss "
        "and material cost apply. No same-month re-export. Legacy annual trade is a separate diagnostic, "
        "not extra food. Proposed population is not mutated.";
    out.limits =
        "Relative food units retain provisional calibration. Rural subsistence is assumed upstream, not "
        "seasonally validated here; coverage and food-equivalent residents concern urban demand only. No "
        "crop calendar diversity, stochastic droughts, soil depletion, groundwater extraction, shipping, "
        "migration or mortality. Route winter conditions use endpoint climate rather than every pass. "
        "Greedy direct-neighbour trade is not an optimal multi-hop logistics solver.";
    return out;
}

}  // namespace fwg
