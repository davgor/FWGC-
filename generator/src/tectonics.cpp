#include "fwg/tectonics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "fwg/ecology.hpp"
#include "fwg/erosion.hpp"
#include "fwg/measure.hpp"
#include "fwg/noise.hpp"
#include "fwg/random.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Relative surface velocity of two plates at a boundary point, split into the
// components the relief model cares about.
struct Motion {
    double convergence = 0.0;
    double divergence = 0.0;
    double shear = 0.0;
};

Motion relative_motion(const Vec3& p, const Vec3& normal, const Vec3& omega_i, const Vec3& omega_j) {
    const Vec3 vi = cross(omega_i, p);
    const Vec3 vj = cross(omega_j, p);
    const Vec3 relative = vj - vi;
    const double opening = dot(relative, normal);
    const double shear = std::sqrt(std::max(0.0, dot(relative, relative) - opening * opening));
    return {clamp01(-opening / 2.0), clamp01(opening / 2.0), std::min(1.0, shear / 2.0)};
}

// Elongated cross-belt ridges with peaks and passes varying along their length.
// Without this a collision belt is a smooth wall; with it, it is a range.
double mountain_modulation(double along, double across, double strength) {
    const double ridge = 0.5 + 0.5 * std::cos(9.0 * across + 0.6 * std::sin(3.0 * along));
    const double peaks = 0.7 + 0.3 * (0.5 + 0.5 * std::sin(5.0 * along + 0.4));
    const double shaped = 0.35 + 0.65 * ridge * peaks;
    return 1.0 - strength + strength * shaped;
}

struct Relief3 {
    double structure = 0.0;    // scaled by tectonic_relief, includes the hypsometric base
    double interaction = 0.0;  // scaled by tectonic_relief, the tectonic term alone
    double volcanic = 0.0;     // unscaled 0..1 activity
};

Relief3 relief_at(double c, double distance, double conv, double div, double ci, double cj,
                  const Config& cfg, double along, double across) {
    const double width = cfg.belt_width * cfg.globe_radius;
    const double d = distance / width;
    const double belt = std::exp(-d * d);

    const double collision = conv * ci * cj;
    const double subduction = conv * (1.0 - ci * cj);
    // The more continental side overrides. Equal crust is a simplified tie.
    const double overriding = 0.5 + 0.5 * std::tanh(8.0 * (ci - cj));

    const double spread =
        cfg.options[Option::mountain_abundance] * (1.0 + 0.25 * cfg.mountain_detail * std::tanh(across));
    const double falloff = d / spread;
    const double uplift = cfg.orogeny * 2.0 * collision * std::exp(-falloff * falloff) *
                          mountain_modulation(along, across, cfg.mountain_detail);

    const double volcanic_shape = (d - 0.8) / 0.5;
    const double volcanic = subduction * std::exp(-volcanic_shape * volcanic_shape) * overriding;

    const double trench_shape = (d - 0.25) / 0.25;
    const double trench = -1.2 * subduction * std::exp(-trench_shape * trench_shape) * (1.0 - overriding);

    const double rift = -0.7 * div * c * belt;
    const double ridge = 0.6 * div * (1.0 - c) * belt;

    const double interaction = uplift + trench + rift + ridge + 0.55 * volcanic;
    return {cfg.tectonic_relief * (continental_profile(c) + interaction),
            cfg.tectonic_relief * interaction,
            volcanic};
}

}  // namespace

PointSample layout_point(const Vec3& p, const std::vector<Plate>& plates, std::uint32_t crust_seed,
                         const Config& cfg) {
    const std::size_t count = plates.size();

    std::vector<double> scores(count);
    double best = -2.0;
    std::size_t owner_index = 0;
    for (std::size_t i = 0; i < count; ++i) {
        scores[i] = dot(p, plates[i].center);
        if (scores[i] > best) {
            best = scores[i];
            owner_index = i;
        }
    }

    // Compact, continuous memberships: a plate whose centre is far behind the nearest
    // one contributes nothing, which keeps distant great-circle extensions of a
    // boundary out of a cell that is nowhere near it.
    const double support = 2.0 * cfg.belt_width;
    std::vector<double> weights(count);
    for (std::size_t i = 0; i < count; ++i) weights[i] = plate_membership(best - scores[i], support);

    const double c = crust_at(p, crust_seed, cfg.crust_bias);

    double total_weight = 0.0;
    double sum_convergence = 0.0;
    double sum_divergence = 0.0;
    double sum_shear = 0.0;
    double sum_interaction = 0.0;
    double sum_volcanic = 0.0;

    for (std::size_t i = 0; i < count; ++i) {
        if (weights[i] == 0.0) continue;
        for (std::size_t j = i + 1; j < count; ++j) {
            if (weights[j] == 0.0) continue;

            const Vec3 normal = unit(plates[j].center - plates[i].center);
            const double projection = clamp_unit(dot(p, normal));
            const Vec3 raw = p - projection * normal;
            if (dot(raw, raw) < 1e-20) continue;  // p sits on the pair's axis
            const Vec3 boundary = unit(raw);

            const Motion motion = relative_motion(boundary, normal, plates[i].omega, plates[j].omega);

            const double angle = cfg.belt_width;
            const double ca = std::cos(angle);
            const double sa = std::sin(angle);
            const double ci = crust_at(boundary * ca - normal * sa, crust_seed, cfg.crust_bias);
            const double cj = crust_at(boundary * ca + normal * sa, crust_seed, cfg.crust_bias);

            const Vec3 reference = unit(plates[i].center + plates[j].center);
            const double along = std::atan2(dot(boundary, cross(normal, reference)), dot(boundary, reference));
            const double across = std::asin(projection) / angle;
            const double distance = std::abs(across) * angle * cfg.globe_radius;

            // Smoothly choose crust on this side, including across the boundary.
            const double side = 0.5 + 0.5 * std::tanh(across * 3.0);
            const double here = ci * (1.0 - side) + cj * side;
            const double there = cj * (1.0 - side) + ci * side;

            const Relief3 relief = relief_at(c, distance, motion.convergence, motion.divergence, here, there,
                                             cfg, along, across);

            const double w = weights[i] * weights[j];
            total_weight += w;
            sum_convergence += w * motion.convergence;
            sum_divergence += w * motion.divergence;
            sum_shear += w * motion.shear;
            sum_interaction += w * relief.interaction;
            sum_volcanic += w * relief.volcanic;
        }
    }

    // A baseline weight of one fades isolated pair tails to zero without a hard
    // cutoff: where the pair weights are small the interaction is scaled down rather
    // than normalised back up to full strength.
    const double denominator = std::max(1.0, total_weight);

    PointSample out;
    out.owner = static_cast<double>(plates[owner_index].id);
    out.crust = c;
    out.convergence = sum_convergence / denominator;
    out.divergence = sum_divergence / denominator;
    out.shear = sum_shear / denominator;
    out.interaction = sum_interaction / denominator;
    out.volcanic = sum_volcanic / denominator;
    out.structure = cfg.tectonic_relief * continental_profile(c) + out.interaction;

    // Distance to the nearest half-space edge of the owning Voronoi cell. Computed
    // independently of the blend support, so a candidate fading out does not make it
    // jump.
    const Vec3 center = plates[owner_index].center;
    double nearest = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < count; ++i) {
        if (i == owner_index) continue;
        const Vec3 normal = unit(plates[i].center - center);
        nearest = std::min(nearest, std::abs(std::asin(clamp_unit(dot(p, normal)))));
    }
    out.boundary_distance = (count > 1 ? nearest : 0.0) * cfg.globe_radius;

    return out;
}

double crust_at(const Vec3& p, std::uint32_t seed, double bias) {
    double v = 0.5 + 1.65 * perlin3(p.x * 1.4 + 0.17, p.y * 1.4 + 0.39, p.z * 1.4 + 0.71, seed) + bias * 0.5;
    v = clamp01(v);
    return v * v * (3.0 - 2.0 * v);
}

double continental_profile(double c) {
    // Abyssal plain, shelf break, coastal platform, interior.
    static constexpr double kAnchorC[5] = {0.0, 0.3, 0.45, 0.6, 1.0};
    static constexpr double kAnchorH[5] = {-1.8, -1.65, -0.12, 0.08, 0.3};
    for (int i = 0; i < 4; ++i) {
        if (c <= kAnchorC[i + 1]) {
            double t = (c - kAnchorC[i]) / (kAnchorC[i + 1] - kAnchorC[i]);
            t = clamp01(t);
            return kAnchorH[i] + (kAnchorH[i + 1] - kAnchorH[i]) * t * t * (3.0 - 2.0 * t);
        }
    }
    return kAnchorH[4];
}

std::vector<Plate> make_plates(std::uint64_t seed, int count) {
    Rng rng(seed);
    std::vector<Plate> plates;
    plates.reserve(static_cast<std::size_t>(count));

    // Refuse a centre that crowds an existing one. The coefficient is tuned so that
    // `count` plates fit without the search running long.
    const double cutoff = std::cos(0.55 * std::sqrt(4.0 * kPi / count));

    const long long attempts = static_cast<long long>(count) * 1000;
    for (long long attempt = 0; attempt < attempts; ++attempt) {
        const Vec3 center = unit({rng.gauss(0.0, 1.0), rng.gauss(0.0, 1.0), rng.gauss(0.0, 1.0)});
        bool crowded = false;
        for (const Plate& plate : plates) {
            if (dot(center, plate.center) > cutoff) { crowded = true; break; }
        }
        if (crowded) continue;

        const Vec3 axis = unit({rng.gauss(0.0, 1.0), rng.gauss(0.0, 1.0), rng.gauss(0.0, 1.0)});
        const double speed = rng.uniform(0.25, 1.0);
        plates.push_back({static_cast<int>(plates.size()), center, axis * speed});
        if (static_cast<int>(plates.size()) == count) return plates;
    }
    throw std::runtime_error("could not distribute plate centres; lower plate_count");
}

TectonicResult generate_tectonics(const Config& cfg) {
    const int n = cfg.size;
    const double r = cfg.globe_radius;

    TectonicResult out;
    out.layout_seed = child_seed64(cfg.seed, "plates", cfg.layout_variation);
    out.crust_seed = child_seed(cfg.seed, "crust", cfg.layout_variation);
    out.detail_seed = child_seed(cfg.seed, "surface", cfg.detail_variation);
    out.plates = make_plates(out.layout_seed, cfg.plate_count);

    // --- plate layout ------------------------------------------------------
    std::vector<PointSample> records(static_cast<std::size_t>(n) * static_cast<std::size_t>(n));
    Grid plates_layer(n), crust_layer(n), boundary(n), convergence(n), divergence(n), shear(n);
    Grid structure(n), interaction(n), volcanic(n);

    std::vector<double> row(static_cast<std::size_t>(n));
    const auto emit = [&](Grid& layer, int z, int count, auto&& pick) {
        row.assign(static_cast<std::size_t>(count), 0.0);
        for (int x = 0; x < count; ++x) row[static_cast<std::size_t>(x)] = pick(x);
        layer.fill_row(z, row);
    };

    for (int z = 0; z < n; ++z) {
        const int count = distinct_count(z, n);
        std::vector<PointSample> samples(static_cast<std::size_t>(count));
        for (int x = 0; x < count; ++x) {
            samples[static_cast<std::size_t>(x)] = layout_point(direction(x, z, n), out.plates, out.crust_seed, cfg);
        }
        emit(plates_layer, z, count, [&](int x) { return samples[static_cast<std::size_t>(x)].owner; });
        emit(crust_layer, z, count, [&](int x) { return samples[static_cast<std::size_t>(x)].crust; });
        emit(boundary, z, count, [&](int x) { return samples[static_cast<std::size_t>(x)].boundary_distance; });
        emit(convergence, z, count, [&](int x) { return samples[static_cast<std::size_t>(x)].convergence; });
        emit(divergence, z, count, [&](int x) { return samples[static_cast<std::size_t>(x)].divergence; });
        emit(shear, z, count, [&](int x) { return samples[static_cast<std::size_t>(x)].shear; });
        emit(structure, z, count, [&](int x) { return samples[static_cast<std::size_t>(x)].structure; });
        emit(interaction, z, count, [&](int x) { return samples[static_cast<std::size_t>(x)].interaction; });
        emit(volcanic, z, count, [&](int x) { return samples[static_cast<std::size_t>(x)].volcanic; });
    }

    out.layers["plates"] = plates_layer;
    out.layers["crust"] = crust_layer;
    out.layers["boundary_distance"] = boundary;
    out.layers["convergence"] = convergence;
    out.layers["divergence"] = divergence;
    out.layers["shear"] = shear;
    out.layers["base"] = Grid(n);
    out.layers["height"] = Grid(n);

    // --- phase 2: the hypsometric surface ----------------------------------
    if (cfg.phase >= 2) {
        Grid continental(n);
        for (int z = 0; z < n; ++z) {
            for (int x = 0; x < n; ++x) {
                continental.at(x, z) = cfg.tectonic_relief * continental_profile(crust_layer.at(x, z));
            }
        }
        out.layers["structure"] = structure;
        out.layers["interaction"] = interaction;
        out.layers["volcanic"] = volcanic;
        out.layers["continental"] = continental;
        out.layers["base"] = (cfg.phase == 2) ? continental : structure;
        out.layers["height"] = structure;
    }

    // --- ocean archipelagos ------------------------------------------------
    // Raised into the tectonic surface BEFORE the surface noise, erosion and drainage,
    // so an island is eroded and drained like any other land rather than pasted on top
    // of a finished world.
    if (cfg.phase >= 2) {
        out.archipelagos = shape_islands(out.layers, cfg);
    }

    // --- phase 3: surface detail -------------------------------------------
    const double step = cfg.step();  // one definition, in Config
    out.spacing_m = step;
    out.effective_tpi_radius_m = std::min(kPi * r / 2.0, std::max(kPi * r / (n - 1), cfg.radius));
    const std::vector<double> frequencies = resolved_frequencies(cfg.wavelength, cfg.octaves, step);

    if (cfg.phase >= 3) {
        out.resolved_octaves = static_cast<int>(frequencies.size());
        Grid noise(n);
        Grid height(n);
        // The PUBLISHED structure, not the local copy taken before the islands were
        // raised. shape_islands adds the archipelago uplift to out.layers, and reading
        // the stale local here discarded every island at phase 3 and above -- which is
        // every world built at the default phase -- while the published structure, base
        // and archipelago_relief layers all went on describing them.
        const Grid& raised = out.layers.at("structure");
        for (int z = 0; z < n; ++z) {
            const int count = distinct_count(z, n);
            row.assign(static_cast<std::size_t>(count), 0.0);
            for (int x = 0; x < count; ++x) {
                const Vec3 p = direction(x, z, n);
                double value = 0.0;
                double gain = 1.0;
                for (std::size_t k = 0; k < frequencies.size(); ++k) {
                    const double f = frequencies[k];
                    const double raw = perlin3(p.x * r * f + 0.173, p.y * r * f + 0.391, p.z * r * f + 0.719,
                                               octave_seed(out.detail_seed, k));
                    const double sampled = clamp_unit(raw);
                    const double ridged = 1.0 - std::abs(sampled);
                    value += cfg.amplitude * gain *
                             ((1.0 - cfg.ridge) * sampled + cfg.ridge * (ridged * ridged * ridged - 0.5));
                    gain *= 0.5;
                }
                // Roughness follows boundary influence, with a small interior floor:
                // plate interiors are quiet, boundaries are broken ground.
                const double influence = std::min(1.0, convergence.at(x, z) + divergence.at(x, z) + shear.at(x, z));
                row[static_cast<std::size_t>(x)] = value * (0.2 + 0.8 * influence);
            }
            noise.fill_row(z, row);
            for (int x = 0; x < n; ++x) height.at(x, z) = raised.at(x, z) + noise.at(x, z);
        }
        out.layers["noise"] = noise;
        out.layers["height"] = height;
        out.layers["surface"] = height;
    }

    // --- phase 4: erosion ---------------------------------------------------
    if (cfg.phase >= 4) {
        const ErosionResult eroded = erode(out.layers["height"], r, cfg.sea_level, cfg.erosion_passes,
                                           cfg.erosion_strength);
        out.layers["height"] = eroded.height;
        out.layers["erosion"] = eroded.erosion;
        out.layers["deposition"] = eroded.deposition;
        out.layers["erosion_delta"] = eroded.erosion_delta;
        out.layers["catchment"] = eroded.catchment;
        out.layers["base"] = Grid(out.layers.at("surface"));
        out.has_sediment_budget = true;
        out.eroded_m3 = eroded.eroded_m3;
        out.deposited_m3 = eroded.deposited_m3;
        out.stored_sediment_m3 = eroded.stored_sediment_m3;
        out.balance_error_m3 = eroded.balance_error_m3;
    }

    if (cfg.phase >= 2) {
        const Relief relief = measure_globe(out.layers["height"], r, cfg.radius);
        out.layers["slope"] = relief.slope;
        out.layers["tpi"] = relief.tpi;
    }

    out.warnings.push_back(
        "Static tectonic approximation: relative motion is dimensionless, not a time-evolving simulation.");
    out.warnings.push_back("Nearby plate pairs blend continuously; plate identity itself remains categorical.");
    out.warnings.push_back(
        "Erosion uses downhill grid neighbours and closed-depression sediment sinks. Later water routing does "
        "not rerun sediment transport.");
    out.warnings.push_back("Latitude grid is not equal-area. TPI uses an 8-point geodesic ring.");
    if (cfg.phase >= 3 && static_cast<int>(frequencies.size()) < cfg.octaves) {
        out.warnings.push_back("Only " + std::to_string(frequencies.size()) + "/" + std::to_string(cfg.octaves) +
                               " surface-noise octaves resolved; fine layers omitted.");
    }
    return out;
}

}  // namespace fwg
