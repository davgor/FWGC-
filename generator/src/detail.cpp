#include "fwg/detail.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <stdexcept>

#include "fwg/json.hpp"
#include "fwg/noise.hpp"
#include "fwg/random.hpp"
#include "fwg/sha256.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

// Coarse to fine. The coarsest band is what a player walks over; the finest is what
// they see under their feet at a metre.
constexpr double kBandScale[3] = {120.0, 28.0, 6.0};
constexpr double kBandAmplitude[3] = {12.0, 2.5, 0.3};

// How freely each surface takes detail. Bare rock is roughest; the default is for
// anything not listed, which is deliberately conservative.
double biome_roughness(int biome) {
    switch (biome) {
        case 5: return 1.4;   // exposed rock
        case 3: return 0.8;   // grassland
        case 4: return 1.0;   // forest
        case 7: return 1.0;   // rainforest
        case 15: return 0.8;  // boreal forest
        default: return 0.65;
    }
}

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

}  // namespace

TerrainDetail make_terrain_detail(std::uint32_t world_seed) {
    TerrainDetail detail;
    detail.seed = child_seed(world_seed, "continuous-terrain-v1");
    for (int i = 0; i < 3; ++i) {
        detail.bands.push_back({kBandScale[i], kBandAmplitude[i],
                                child_seed(detail.seed, std::to_string(i))});
    }
    return detail;
}

HeightField::HeightField(const World& world) {
    radius_ = world.effective.globe_radius;
    sea_level_ = world.effective.sea_level;
    base_ = &world.layers.at("height");
    n_ = base_->size();
    detail_ = make_terrain_detail(world.config.seed);

    // Absent layers are not an error: the detail field is defined from phase 2, long
    // before water, rivers or biomes exist, and each missing input simply stops
    // protecting anything.
    const auto layer = [&](const char* name) -> const Grid* {
        const auto it = world.layers.find(name);
        return it != world.layers.end() ? &it->second : nullptr;
    };

    const Grid* water = layer("water_type");
    const Grid* river = layer("river");
    const Grid* flood = layer("flood_risk");
    const Grid* biome = layer("natural_biome");

    if (water) {
        water_ = water;
    } else {
        water_owned_ = Grid(n_, 0.0);
        water_ = &water_owned_;
    }

    // The protection mask, built once. Every factor can independently zero it, and a
    // zero plateau is what keeps lakes flat and river beds smooth.
    strength_ = Grid(n_, 0.0);
    for (int z = 0; z < n_; ++z) {
        for (int x = 0; x < n_; ++x) {
            if (water_->at(x, z) != 0.0) continue;  // water takes no detail at all
            const double river_guard = std::max(0.0, 1.0 - std::min(1.0, river ? river->at(x, z) * 2.0 : 0.0));
            const double flood_guard = std::max(0.0, 1.0 - std::min(1.0, flood ? flood->at(x, z) : 0.0));
            const double rough = biome_roughness(biome ? static_cast<int>(biome->at(x, z)) : 3);
            strength_.at(x, z) = river_guard * flood_guard * rough;
        }
    }
}

HeightField::Cell HeightField::locate(const Vec3& p) const {
    // The index arithmetic `sample` performs, computed ONCE: height() interpolates
    // three grids of identical shape at the same direction, and the two inverse
    // trigonometric calls were otherwise repeated for each of them.
    double x = (std::atan2(p.z, p.x) + kPi) / (2.0 * kPi) * (n_ - 1);
    x = std::fmod(x, static_cast<double>(n_ - 1));
    if (x < 0.0) x += n_ - 1;
    double z = (kPi / 2.0 - std::asin(clamp_unit(p.y))) / kPi * (n_ - 1);
    z = std::max(0.0, std::min(static_cast<double>(n_ - 1), z));
    const int ix = static_cast<int>(x);
    const int iz = static_cast<int>(z);
    return {ix, iz, std::min(n_ - 1, iz + 1), x - ix, z - iz};
}

double HeightField::bilinear(const Grid& g, const Cell& c) {
    return ((1.0 - c.fx) * g.at(c.ix, c.iz) + c.fx * g.at(c.ix + 1, c.iz)) * (1.0 - c.fz) +
           ((1.0 - c.fx) * g.at(c.ix, c.jz) + c.fx * g.at(c.ix + 1, c.jz)) * c.fz;
}

double HeightField::height(const Vec3& p) const {
    const Cell c = locate(p);
    const double base = bilinear(*base_, c);

    // Each factor can independently zero the detail, and the answer is `base` whenever
    // one does. Testing them as they are computed skips the remaining grid samples for
    // water, shoreline and protected cells instead of multiplying by zero after paying
    // for all of them.
    const double coast = clamp01((base - sea_level_) / 20.0);
    if (coast == 0.0) return base;

    double strength = bilinear(strength_, c);
    // A zero plateau: everything under 0.15 is fully protected rather than faintly
    // rough, which is what makes shores conservative instead of crumbly.
    strength = std::max(0.0, (strength - 0.15) / 0.85);
    if (strength == 0.0) return base;

    const double wet = std::max(0.0, 1.0 - 2.0 * bilinear(*water_, c));
    strength *= coast * coast * (3.0 - 2.0 * coast) * wet * wet;
    if (strength == 0.0) return base;

    double detail = 0.0;
    for (const DetailBand& band : detail_.bands) {
        detail += band.amplitude_m * perlin3(p.x * radius_ / band.scale_m, p.y * radius_ / band.scale_m,
                                             p.z * radius_ / band.scale_m, band.seed);
    }
    return base + strength * detail;
}

HeightTile export_height_tile(const World& world, int level, int x, int z, int cells) {
    if (level < 1 || level > 24) throw std::invalid_argument("tile level must be 1..24");
    if (cells < 1 || cells > 256) throw std::invalid_argument("tile cells must be 1..256");

    const int rows = 1 << level;
    const int cols = 2 * rows;
    if (x < 0 || x >= cols || z < 0 || z >= rows || x + cells > cols || z + cells > rows) {
        throw std::invalid_argument("tile lies outside the global grid");
    }

    const HeightField field(world);

    HeightTile tile;
    tile.level = level;
    tile.origin_x = x;
    tile.origin_z = z;
    tile.size = cells + 1;  // tiles SHARE their far edge, which is why this is +1
    tile.global_cols = cols;
    tile.global_rows = rows;
    tile.radius_m = field.radius();
    tile.angular_step_degrees = 180.0 / rows;
    tile.equatorial_spacing_m = kPi * field.radius() / rows;

    // Canonicalise the seam and the poles before evaluating noise, or two tiles that
    // share an edge sample fractionally different directions and the seam reopens.
    const auto point = [&](int i, int j) -> Vec3 {
        if (j == 0) return {0.0, 1.0, 0.0};
        if (j == rows) return {0.0, -1.0, 0.0};
        const double lat = kPi * (0.5 - static_cast<double>(j) / rows);
        const int wrapped = ((i % cols) + cols) % cols;
        const double lon = kPi * (2.0 * wrapped / cols - 1.0);
        return {std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
    };

    tile.heights_m.resize(static_cast<std::size_t>(cells + 1));
    for (int j = 0; j <= cells; ++j) {
        auto& row = tile.heights_m[static_cast<std::size_t>(j)];
        row.resize(static_cast<std::size_t>(cells + 1));
        for (int i = 0; i <= cells; ++i) {
            row[static_cast<std::size_t>(i)] = field.height(point(x + i, z + j));
        }
    }

    // A digest over the INPUTS, not the samples: two tiles of one world carry the same
    // source hash, so a consumer can tell "same terrain, different window" from "the
    // world changed under me". Built from the shortest round-trip text of each value,
    // so it is a property of the numbers rather than of any memory layout.
    std::ostringstream material;
    material << "fwg.height-tile/1\n" << format_double(field.radius()) << '\n'
             << format_double(world.effective.sea_level) << '\n' << field.detail().seed << '\n';
    for (const DetailBand& band : field.detail().bands) {
        material << format_double(band.scale_m) << ',' << format_double(band.amplitude_m) << ','
                 << band.seed << '\n';
    }
    const Grid& base = world.layers.at("height");
    const Grid& strength = field.strength();
    for (int zz = 0; zz < base.size(); ++zz) {
        for (int xx = 0; xx < base.size(); ++xx) {
            material << format_double(base.at(xx, zz)) << ',' << format_double(strength.at(xx, zz)) << ';';
        }
    }
    tile.source_sha256 = sha256_hex(material.str());
    return tile;
}

}  // namespace fwg
