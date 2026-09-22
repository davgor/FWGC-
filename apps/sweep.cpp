// A smoke sweep: generate many worlds and complain about any that look wrong.
//
// Unit tests pin properties on one or two worlds. This asks a cruder question across
// many: does every seed produce a world at all, and is anything about it degenerate?
// A generator that throws on one seed in fifty, or quietly makes a world with no land,
// is not something a test on seed 42 will ever notice.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <set>
#include <string>
#include <vector>

#include "fwg/world.hpp"

using namespace fwg;

namespace {

struct Complaint {
    std::uint32_t seed;
    int size;
    std::string what;
};

void check_world(const World& w, std::uint32_t seed, int size, std::vector<Complaint>& out) {
    const auto say = [&](const std::string& what) { out.push_back({seed, size, what}); };

    if (w.area.land_fraction <= 0.02) say("almost no land: " + std::to_string(w.area.land_fraction));
    if (w.area.land_fraction >= 0.98) say("almost no sea: " + std::to_string(w.area.land_fraction));

    if (w.completed_stage != 11) say("stopped at stage " + std::to_string(w.completed_stage));
    if (!w.has_settlements) say("no settlement stage");
    if (!w.has_roads) say("no road stage");

    if (w.settlements.sites.empty()) {
        say("no cities at all");
        return;
    }

    std::set<int> nodes;
    std::set<std::string> peoples;
    long long unnamed = 0;
    for (const SettlementSite& s : w.settlements.sites) {
        if (!nodes.insert(s.node).second) say("two cities on node " + std::to_string(s.node));
        peoples.insert(s.population_profile);
        if (s.name.empty()) ++unnamed;
        if (s.population_estimate < 0) say("negative population in " + s.name);
    }
    if (unnamed > 0) say(std::to_string(unnamed) + " cities with no name");
    if (peoples.size() < 2) say("only " + std::to_string(peoples.size()) + " people settled");

    // A forest over N cities in C components has exactly N - C edges.
    const auto cities = static_cast<int>(w.settlements.sites.size());
    if (static_cast<int>(w.roads.routes.size()) != cities - w.roads.components) {
        say("road count " + std::to_string(w.roads.routes.size()) + " is not " +
            std::to_string(cities) + " - " + std::to_string(w.roads.components));
    }
}

}  // namespace

// Configurations that are legal but unusual. These are where the bugs are: the common
// path is what every test exercises, and a world with magic switched off or one people
// or no cities at all is a shape nothing else asks for.
struct Variant {
    const char* name;
    void (*apply)(Request&);
    bool expects_cities;
};

const Variant kVariants[] = {
    {"default", [](Request&) {}, true},
    {"no magic", [](Request& r) { r.magic_enabled = 0; }, true},
    {"one people", [](Request& r) { r.population_profile = std::string(civilization_ids().front()); }, false},
    {"two cities", [](Request& r) { r.settlement_count = 2; }, true},
    {"no cities", [](Request& r) { r.settlement_count = 0; }, false},
    {"large world", [](Request& r) { r.circumference_km = 600.0; }, true},
};

int main(int argc, char** argv) {
    const int seeds = argc > 1 ? std::atoi(argv[1]) : 20;
    const std::vector<int> sizes = {33, 65, 129};

    std::vector<Complaint> complaints;
    long long built = 0;
    long long threw = 0;

    for (int size : sizes) {
        for (int s = 0; s < seeds; ++s) {
            Request r;
            r.seed = static_cast<std::uint32_t>(1000 + s);
            r.size = size;
            r.phase = 11;
            try {
                const World w = generate(resolve(r));
                ++built;
                check_world(w, r.seed, size, complaints);
            } catch (const std::exception& e) {
                ++threw;
                complaints.push_back({r.seed, size, std::string("threw: ") + e.what()});
            }
        }
        std::printf("size %-4d done\n", size);
    }

    // Every variant on a handful of seeds, since the point is the shape and not the
    // sample. A variant that expects no cities is checked only for not throwing --
    // the coherence checks above all assume a settled world.
    for (const Variant& v : kVariants) {
        for (int s = 0; s < 4; ++s) {
            Request r;
            r.seed = static_cast<std::uint32_t>(500 + s);
            r.size = 65;
            r.phase = 11;
            v.apply(r);
            try {
                const World w = generate(resolve(r));
                ++built;
                if (v.expects_cities) {
                    check_world(w, r.seed, 65, complaints);
                } else if (!w.has_settlements) {
                    complaints.push_back({r.seed, 65, std::string(v.name) + ": no settlement stage"});
                }
            } catch (const std::exception& e) {
                ++threw;
                complaints.push_back({r.seed, 65, std::string(v.name) + " threw: " + e.what()});
            }
        }
        std::printf("variant %-14s done\n", v.name);
    }

    std::printf("\n%lld worlds built, %lld threw, %zu complaints\n", built, threw, complaints.size());
    for (const Complaint& c : complaints) {
        std::printf("  seed %-6u size %-4d %s\n", c.seed, c.size, c.what.c_str());
    }
    return complaints.empty() ? 0 : 1;
}
