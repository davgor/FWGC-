// Wizard colleges: where a people founds an institution of magic.
//
// A college wants strong magic that its own people can survive, on ground a city can
// reach, and otherwise the same things a town wants -- gentle slope, a bearable climate,
// fresh water nearby, no floods. Eight limits decide it, tested in a fixed order so that
// "the limit that refused this cell" means the same thing every time it is reported.
//
// Part of stage 12. The reference runs it between the hinterland and the seasonal food,
// because it reads the hamlets and fortresses the hinterland placed and nothing it writes
// is read by the seasons.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fwg/civilizations.hpp"

namespace fwg {

struct World;
struct HumanHinterland;

// The eight limits, in the order they are tested. The ORDER IS THE MEANING: a cell that
// fails several reports the first, so reordering this changes every diagnostic without
// changing a single placement.
enum class CollegeLimit : int {
    Density = 0,
    Hazard,
    Water,
    Slope,
    Temperature,
    Freshwater,
    Flood,
    Suitability,
};
inline constexpr std::size_t kCollegeLimitCount = 8;
std::string_view to_string(CollegeLimit limit);

// Which limit refuses this cell a college, or nullopt if none does.
//
// This is the definition of eligibility, not a report written alongside it: a second copy
// of the gate kept for the diagnostic is how a report starts disagreeing with the thing it
// reports on.
std::optional<CollegeLimit> college_refusal(double density, double hazard, double hazard_limit,
                                            double slope, double temperature, double freshwater_m,
                                            double flood, double suitability, double water,
                                            const CivilizationTraits& traits);

struct College {
    std::string id;  // "college-<n>", 1-based, in placement order
    int node = 0;
    int x = 0;
    int z = 0;
    int core_id = -1;  // the owning city's index
    std::string population_profile;
    std::string culture_id;
    double density = 0.0;
    double hazard = 0.0;
    double suitability = 0.0;
    // [college node, ..., owning city's node]. Every consecutive pair is a traversable
    // edge under the owning people's own road tolerances.
    std::vector<int> access_nodes;
    double access_cost = 0.0;  // road-cost units, NOT a length
};

// One row per institution, the shape key locations and heroes also publish: whether it
// placed, what was evaluated, and why none did.
struct CollegeDiagnostic {
    int placed = 0;
    int wanted = 0;
    // Every cell that cleared all eight limits, INCLUDING ones the budget never reached,
    // so `placed == candidates` means the world ran out of ground and `placed <
    // candidates` means the budget or the spacing stopped it. Those are different facts
    // and used to be the same zero.
    int candidates = 0;
    int sources = 0;  // cells within some city's support reach -- all the pass ever reads
    std::string reason;
};

struct CollegeReport {
    std::vector<College> colleges;
    CollegeDiagnostic diagnostic;
    double spacing_m = 0.0;  // the minimum distance between two colleges
    std::array<int, kCollegeLimitCount> refusals{};
    int refused_clearance = 0;
    int refused_spacing = 0;
};

// The minimum distance between two colleges: a fraction of the world's radius, with a
// floor. Unlike most authored distances this one scales with the planet.
double college_spacing_m(double globe_radius_m);

// Runs the college pass. Needs the settlement stage's generic layers and the hinterland,
// whose hamlets and fortresses a college must stand clear of.
CollegeReport add_colleges(const World& world, const HumanHinterland& humans);

}  // namespace fwg
