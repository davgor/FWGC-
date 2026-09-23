#include "fwg/magic.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>

#include "fwg/random.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr double kPi = std::numbers::pi;

Option school_option(std::string_view school, const char* suffix) {
    std::string name(school);
    name += suffix;
    const auto option = option_by_name(name);
    if (!option) throw std::runtime_error("missing world option " + name);
    return *option;
}

}  // namespace

ArcFrame arc_frame(const Vec3& a, const Vec3& b) {
    const double cosine = clamp_unit(dot(a, b));
    const double angle = std::acos(cosine);
    // Tangent at `a` pointing toward `b`. The floor stops a degenerate pair (a == b)
    // from dividing by zero; such an arc has zero length and the tangent is unused.
    const double s = std::max(1e-12, std::sin(angle));
    const Vec3 tangent{(b.x - cosine * a.x) / s, (b.y - cosine * a.y) / s, (b.z - cosine * a.z) / s};
    return {a, b, tangent, angle};
}

double distance_to_frame(const Vec3& p, const ArcFrame& frame) {
    const double pa = dot(p, frame.a);
    const double pt = dot(p, frame.tangent);
    const double along = std::atan2(pt, pa);
    double best = std::max(pa, dot(p, frame.b));
    // Only inside the arc's own span does the perpendicular foot count; outside it the
    // nearest point is an endpoint, which the max above already has.
    if (along >= 0.0 && along <= frame.angle) best = std::max(best, std::hypot(pa, pt));
    return std::acos(clamp_unit(best));
}

NetworkGeometry network_geometry(std::uint32_t seed, int count) {
    Rng rng(child_seed64(seed, "clustered-ley-v1"));

    const auto unit_point = [&rng] {
        const double y = rng.uniform(-1.0, 1.0);
        const double a = rng.uniform(-kPi, kPi);
        const double r = std::sqrt(1.0 - y * y);
        return Vec3{r * std::cos(a), y, r * std::sin(a)};
    };

    NetworkGeometry out;
    if (count <= 0) return out;

    const int clusters = static_cast<int>(rng.randint(1, std::min(4, count)));
    std::vector<Vec3> centres;
    centres.reserve(static_cast<std::size_t>(clusters));
    for (int i = 0; i < clusters; ++i) centres.push_back(unit_point());

    out.distribution.clusters = clusters;
    out.distribution.spread = rng.uniform(0.12, 0.8);
    out.distribution.outlier_fraction = rng.uniform(0.05, 0.4);
    const double spread = out.distribution.spread;
    const double outlier = out.distribution.outlier_fraction;

    std::vector<int> groups;
    for (int k = 0; k < count; ++k) {
        const int group = static_cast<int>(rng.below(static_cast<std::uint64_t>(clusters)));
        Vec3 p{};
        bool placed = false;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            if (rng.random() < outlier) {
                p = unit_point();
            } else {
                const Vec3& c = centres[static_cast<std::size_t>(group)];
                p = unit({c.x + rng.gauss(0.0, spread), c.y + rng.gauss(0.0, spread),
                          c.z + rng.gauss(0.0, spread)});
            }
            // Reject a point that coincides with an existing one OR its antipode: both
            // make a degenerate arc frame.
            const bool clear = std::all_of(out.nodes.begin(), out.nodes.end(), [&](const Vec3& q) {
                return std::abs(dot(p, q)) < 0.9999;
            });
            if (clear) { placed = true; break; }
        }
        if (!placed) throw std::runtime_error("could not separate ley key points");
        out.nodes.push_back(p);
        groups.push_back(group);
    }

    std::set<std::pair<int, int>> pairs;
    if (count >= 3) {
        // Three sacred loci share a straight spherical alignment. A fantasy reading of
        // Watkins' ley alignments, not a physical law -- node 2 is moved onto the arc
        // from 0 to 1, and the three are pulled into one cluster.
        const ArcFrame arc = arc_frame(out.nodes[0], out.nodes[1]);
        const double t = rng.uniform(0.3, 0.7) * arc.angle;
        const double c = std::cos(t);
        const double s = std::sin(t);
        out.nodes[2] = {arc.a.x * c + arc.tangent.x * s, arc.a.y * c + arc.tangent.y * s,
                        arc.a.z * c + arc.tangent.z * s};
        groups[1] = groups[2] = groups[0];
        out.distribution.aligned_nodes = {0, 2, 1};
        pairs.insert({0, 2});
        pairs.insert({1, 2});
    }

    // A sparse tree per cluster, nearest-first. Clusters are NOT joined to each other,
    // so a school can have genuinely disconnected concentrations.
    for (int group = 0; group < clusters; ++group) {
        std::vector<int> members;
        for (int i = 0; i < count; ++i) {
            if (groups[static_cast<std::size_t>(i)] == group) members.push_back(i);
        }
        if (members.empty()) continue;
        std::set<int> joined{members.front()};
        while (joined.size() < members.size()) {
            double best = 2.0;
            int best_a = -1;
            int best_b = -1;
            for (const int a : joined) {  // std::set iterates sorted, as the reference does
                for (const int b : members) {
                    if (joined.count(b)) continue;
                    const double d = 1.0 - dot(out.nodes[static_cast<std::size_t>(a)],
                                               out.nodes[static_cast<std::size_t>(b)]);
                    if (d < best || (d == best && (a < best_a || (a == best_a && b < best_b)))) {
                        best = d;
                        best_a = a;
                        best_b = b;
                    }
                }
            }
            if (best_b < 0) break;
            pairs.insert({std::min(best_a, best_b), std::max(best_a, best_b)});
            joined.insert(best_b);
        }
    }

    // Then a few long links across the whole network, at an independently seeded rate.
    out.distribution.extra_link_probability = rng.uniform(0.08, 0.45);
    for (int a = 0; a < count; ++a) {
        if (rng.random() >= out.distribution.extra_link_probability) continue;
        std::vector<int> choices;
        for (int b = 0; b < count; ++b) {
            if (b != a) choices.push_back(b);
        }
        if (choices.empty()) continue;
        const int b = rng.choice(choices);
        pairs.insert({std::min(a, b), std::max(a, b)});
    }

    out.edges.assign(pairs.begin(), pairs.end());
    return out;
}

int dominant_school(const std::vector<double>& potencies, double threshold, double margin) {
    // Only the top two of twelve are ever read, so this is a two-pass scan rather than
    // a sort. Ties go to the lower index, matching the reference's (-potency, name)
    // ordering over a table whose names are in contract order.
    int winner = -1;
    double best = -1.0;
    double runner = 0.0;
    for (std::size_t i = 0; i < potencies.size(); ++i) {
        if (potencies[i] > best) {
            runner = best < 0.0 ? 0.0 : best;
            best = potencies[i];
            winner = static_cast<int>(i);
        } else if (potencies[i] > runner) {
            runner = potencies[i];
        }
    }
    if (winner < 0) return -1;
    return (best >= threshold && best - runner >= margin) ? winner : -1;
}

void generate_networks(World& world) {
    const Config& cfg = world.effective;
    const WorldOptions& o = cfg.options;

    // Authored widths are a reach on the reference world. Left absolute, a 110 m
    // Gaussian on a 200 km planet falls entirely between raster cells and every ley
    // field, magic density, biome variant and magic-gated habitat collapses to zero.
    const double width_scale = cfg.globe_radius / kReferenceRadiusM;

    MagicReport report;
    report.enabled = cfg.magic_enabled != 0;

    for (const MagicSchool& school : magic_school_table()) {
        const std::string name(school.name);
        const auto variation = static_cast<std::int64_t>(o[school_option(school.name, "_variation")]);
        const std::uint32_t seed = child_seed(cfg.seed, "history-ley-" + name, variation);

        LeyNetwork net;
        net.name = name;
        net.group = std::string(school.group);
        net.descriptors = std::string(school.descriptors);
        for (int i = 0; i < 3; ++i) net.color[i] = school.color[i];
        net.seed = seed;
        net.strength = o[school_option(school.name, "_strength")];
        net.width_m = o[school_option(school.name, "_width")] * width_scale;
        net.instability = o[school_option(school.name, "_instability")];

        // One roll decides whether the school manifests at all. A hidden school's
        // occurrence is locked to zero, so this is always false for those four.
        Rng roll(seed);
        net.manifested = report.enabled && roll.random() < o[school_option(school.name, "_occurrence")];

        if (net.manifested) {
            const int node_count = o.as_int(school_option(school.name, "_nodes"));
            const NetworkGeometry geometry = network_geometry(seed, node_count);
            net.distribution = geometry.distribution;

            Rng intensities(child_seed64(seed, "intensities"));
            for (std::size_t i = 0; i < geometry.nodes.size(); ++i) {
                net.nodes.push_back({name + "-node-" + std::to_string(i), geometry.nodes[i],
                                     intensities.uniform(0.35, 1.65)});
            }
            for (std::size_t i = 0; i < geometry.edges.size(); ++i) {
                net.edges.push_back({name + "-line-" + std::to_string(i), geometry.edges[i].first,
                                     geometry.edges[i].second, intensities.uniform(0.35, 1.65)});
            }
        }
        report.networks.emplace(name, std::move(net));
    }

    world.magic = std::move(report);
    world.has_magic = true;
    evaluate_networks(world);
    world.systems.push_back("leylines");
}

void evaluate_networks(World& world) {
    const Config& cfg = world.effective;
    const int n = cfg.size;
    const double radius = cfg.globe_radius;

    const auto held = sphere_grid(n, radius);
    const SphereGrid& grid = *held;
    const std::size_t count = grid.node_count();

    std::vector<Vec3> vectors(count);
    for (std::size_t i = 0; i < count; ++i) vectors[i] = direction(grid.point_x[i], grid.point_z[i], n);

    const auto table = magic_school_table();
    std::vector<std::vector<double>> powers(table.size(), std::vector<double>(count, 0.0));

    for (std::size_t s = 0; s < table.size(); ++s) {
        const LeyNetwork& net = world.magic.networks.at(std::string(table[s].name));
        std::vector<double>& power = powers[s];

        if (net.nodes.empty() && net.edges.empty()) {
            // An empty school sums to integer zero everywhere, and -expm1(-0) is
            // NEGATIVE zero. Written as the same expression rather than a literal so
            // it cannot drift from the loop below; the sign reaches the document.
            const double value = net.strength * -std::expm1(-0.0);
            std::fill(power.begin(), power.end(), value);
            continue;
        }

        // Per-point invariants hoisted out of a loop that runs once per cell per
        // school: at size 513 that is 263k iterations times twelve schools.
        std::vector<std::pair<double, Vec3>> node_terms;
        node_terms.reserve(net.nodes.size());
        for (const LeyNode& node : net.nodes) node_terms.emplace_back(node.intensity, node.direction);

        std::vector<std::pair<double, ArcFrame>> edge_terms;
        edge_terms.reserve(net.edges.size());
        for (const LeyEdge& e : net.edges) {
            const LeyNode& a = net.nodes[static_cast<std::size_t>(e.from)];
            const LeyNode& b = net.nodes[static_cast<std::size_t>(e.to)];
            // A line carries the mean of the two points it joins, scaled by its own.
            edge_terms.emplace_back(e.intensity * (a.intensity + b.intensity) / 2.0,
                                    arc_frame(a.direction, b.direction));
        }

        const double width = net.width_m;
        for (std::size_t i = 0; i < count; ++i) {
            const Vec3& p = vectors[i];
            double total = 0.0;
            for (const auto& [intensity, point] : node_terms) {
                const double a = radius * std::acos(clamp_unit(dot(p, point))) / width;
                total += intensity * std::exp(-a * a);
            }
            for (const auto& [k, frame] : edge_terms) {
                const double a = radius * distance_to_frame(p, frame) / width;
                total += k * std::exp(-a * a);
            }
            // Saturating: overlapping sources approach `strength` and never exceed it.
            power[i] = net.strength * -std::expm1(-total);
        }
    }

    // Publish the per-school fields.
    for (std::size_t s = 0; s < table.size(); ++s) {
        const std::string name(table[s].name);
        const LeyNetwork& net = world.magic.networks.at(name);
        world.layers["ley_" + name] = grid.to_grid(powers[s]);
        std::vector<double> unstable(count);
        for (std::size_t i = 0; i < count; ++i) unstable[i] = std::min(1.0, powers[s][i] * net.instability);
        world.layers["instability_" + name] = grid.to_grid(unstable);
    }

    const int weave = magic_school_index("weave");
    const int umbral = magic_school_index("umbral");
    const int infernal = magic_school_index("infernal");
    const int radiant = magic_school_index("radiant");
    const int fire = magic_school_index("fire");
    const int water = magic_school_index("water");
    const int earth = magic_school_index("earth");
    const int air = magic_school_index("air");

    std::vector<double> density(count), hazard(count), growth(count), opposition(count), winners(count);
    std::vector<double> cell(table.size());
    for (std::size_t i = 0; i < count; ++i) {
        double total = 0.0;
        double instability_total = 0.0;
        for (std::size_t s = 0; s < table.size(); ++s) {
            cell[s] = powers[s][i];
            total += cell[s];
            instability_total +=
                std::min(1.0, cell[s] * world.magic.networks.at(std::string(table[s].name)).instability);
        }
        // Opposed pairs: light against dark, creation against entropy, fire against
        // water. Where both are strong the ground is contested, not doubly magical.
        const double conflict = std::min(cell[static_cast<std::size_t>(radiant)], cell[static_cast<std::size_t>(infernal)]) +
                                std::min(cell[static_cast<std::size_t>(weave)], cell[static_cast<std::size_t>(umbral)]) +
                                std::min(cell[static_cast<std::size_t>(fire)], cell[static_cast<std::size_t>(water)]);

        density[i] = -std::expm1(-total);
        opposition[i] = std::min(1.0, conflict);
        hazard[i] = std::min(1.0, instability_total * 0.3 + conflict * 0.2);
        growth[i] = (cell[static_cast<std::size_t>(earth)] + 0.6 * cell[static_cast<std::size_t>(radiant)] +
                     0.5 * cell[static_cast<std::size_t>(weave)] + 0.4 * cell[static_cast<std::size_t>(water)]) /
                    std::max(total, 1e-12);
        winners[i] = dominant_school(cell, world.magic.mutation_threshold, world.magic.dominance_margin);
    }

    world.layers["magic_density"] = grid.to_grid(density);
    world.layers["magic_hazard"] = grid.to_grid(hazard);
    world.layers["magic_growth"] = grid.to_grid(growth);
    world.layers["magic_opposition"] = grid.to_grid(opposition);
    world.layers["dominant_magic"] = grid.to_grid(winners);

    // Compatibility projections for consumers that predate the eight-school taxonomy.
    // They are PROJECTIONS, never extra networks: nothing seeds or edits them.
    world.layers["ley_holy"] = world.layers.at("ley_radiant");
    std::vector<double> primordial(count);
    for (std::size_t i = 0; i < count; ++i) {
        primordial[i] = std::max({powers[static_cast<std::size_t>(fire)][i], powers[static_cast<std::size_t>(water)][i],
                                  powers[static_cast<std::size_t>(earth)][i], powers[static_cast<std::size_t>(air)][i]});
    }
    world.layers["ley_primordial"] = grid.to_grid(primordial);
}

namespace {

bool known_school(std::string_view name) {
    for (const MagicSchool& school : magic_school_table()) {
        if (!school.hidden && school.name == name) return true;
    }
    return false;
}

std::string known_school_list() {
    std::vector<std::string_view> names;
    for (const MagicSchool& school : magic_school_table()) {
        if (!school.hidden) names.push_back(school.name);
    }
    std::sort(names.begin(), names.end());
    std::string out;
    for (const std::string_view name : names) out += (out.empty() ? "" : ", ") + std::string(name);
    return out;
}

}  // namespace

void apply_leyline_edit(MagicReport& magic, const LeylineEdit& edit) {
    if (!known_school(edit.school)) {
        throw LeylineEditError("leyline_edits[].school",
                               "'" + edit.school + "' is not one of the known schools: " + known_school_list());
    }
    if (edit.new_node && edit.intensity) {
        throw LeylineEditError("leyline_edits[].new_node",
                               "Put a new node intensity inside new_node; the outer intensity edits an existing node.");
    }
    const int targets = (edit.node_id ? 1 : 0) + (edit.line_id ? 1 : 0) + (edit.new_node ? 1 : 0);
    if (targets != 1) throw LeylineEditError("leyline_edits[]", "Select one node, line, or new key point");
    LeyNetwork& net = magic.networks.at(edit.school);
    if (edit.new_node) {
        try {
            plant_key_point(net, *edit.new_node);
        } catch (const std::invalid_argument& refused) {
            throw LeylineEditError("leyline_edits[].new_node", refused.what());
        }
        return;
    }
    const double intensity = edit.intensity.value_or(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(intensity) || intensity < 0.0 || intensity > kLeyIntensityMax) {
        throw LeylineEditError("leyline_edits[].intensity", "Intensity must be finite and between 0 and 4");
    }
    if (edit.node_id) {
        for (LeyNode& node : net.nodes) {
            if (node.id != *edit.node_id) continue;
            node.intensity = intensity;
            return;
        }
    } else {
        for (LeyEdge& line : net.edges) {
            if (line.id != *edit.line_id) continue;
            line.intensity = intensity;
            return;
        }
    }
    throw LeylineEditError("leyline_edits[]", "Unknown key point or line");
}

void plant_key_point(LeyNetwork& net, const LeyNode& node) {
    if (node.id.empty()) throw std::invalid_argument("a key point needs a non-empty id");
    for (const LeyNode& existing : net.nodes) {
        if (existing.id == node.id) throw std::invalid_argument("duplicate key point id " + node.id);
    }
    const double length = dot(node.direction, node.direction);
    if (!std::isfinite(length) || std::abs(length - 1.0) > 1e-6) {
        throw std::invalid_argument(node.id + " is not a unit direction");
    }
    if (!std::isfinite(node.intensity) || node.intensity < 0.0 || node.intensity > kLeyIntensityMax) {
        throw std::invalid_argument(node.id + " intensity must be finite and between 0 and 4");
    }
    net.nodes.push_back(node);
}

}  // namespace fwg
