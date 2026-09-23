// Terrain labels: artistic climate proxies, not a weather simulation.
//
// This is where a physical world becomes a legible one. Every later stage reads these
// labels — settlement siting, ecology, creature placement and art all key off the
// biome and the landform rather than off raw height and slope.
#pragma once

#include <string>
#include <vector>

#include "fwg/grid.hpp"
#include "fwg/vec3.hpp"

namespace fwg {

// Landform classes, by index. Order is the contract; the strings are display names.
enum Landform : int { kSubmerged = 0, kPlain = 1, kMountain = 2, kValley = 3 };
const char* landform_name(int form);

// The biome decision tree. `above_sea` is metres above the datum, `slope` degrees,
// `temperature` Celsius, `moisture` a 0..1 wetness. Returns a natural biome id.
//
// Order matters and is not arbitrary: ice before rock before cold before dry, so a
// frozen cliff is ice rather than exposed rock.
int classify(double above_sea, double slope, double temperature, double moisture);

// Height-and-relief class for a cell.
int classify_landform(double above_sea, double tpi);

// Marsh needs wet, unfrozen, gently sloping ground that is close to mapped water and
// barely above it. Not a soil saturation model.
bool marsh_suitable(double wet, double temperature, double slope, double water_distance,
                    double height_above_water);

// Distance to the nearest mapped water within the wetland margin, and the height of
// each cell above THAT water's surface. Distance is -1 where no water is in range.
struct WetlandAccess {
    Grid distance_m;
    Grid height_above_water_m;
};
struct World;
WetlandAccess wetland_access(const World& world);

// A sparse, representative region marker — not a unique geological object.
struct TerrainFeature {
    std::string kind;
    std::string label;
    int x = 0;
    int z = 0;
    Vec3 direction;
    double height_m = 0.0;
};

struct TerrainReport {
    int version = 6;
    std::vector<TerrainFeature> features;
    std::uint32_t climate_seed = 0;
    std::string method;
};

// The wetland margin, in metres. A cell further than this from water is never marsh.
inline constexpr double kWetlandMarginM = 180.0;

// Angular separation between selected features, in radians: about 13.7 degrees of arc.
// Wraps over the seam because it is a dot product on the sphere, not a grid distance.
inline constexpr double kFeatureSeparationRad = 0.24;

// Features selected per kind.
inline constexpr int kFeaturesPerKind = 4;

}  // namespace fwg
