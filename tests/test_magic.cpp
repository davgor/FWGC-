// Leyline invariants.
#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>

#include "fwg/magic.hpp"
#include "fwg/world.hpp"

using namespace fwg;

namespace {

World build(int size = 65, int magic_enabled = 1) {
    Request r;
    r.seed = 42;
    r.size = size;
    r.phase = 6;
    r.magic_enabled = magic_enabled;
    return generate(resolve(r));
}

}  // namespace

// --- the taxonomy ---------------------------------------------------------

TEST(the_school_table_keeps_the_known_eight_first) {
    const auto table = magic_school_table();
    check::equal(table.size(), std::size_t{12}, "twelve schools");

    // Position is a contract: dominant_magic exports an index into this table, so a
    // hidden school appearing before a known one would renumber every saved world.
    bool seen_hidden = false;
    int known = 0;
    for (const MagicSchool& s : table) {
        if (s.hidden) seen_hidden = true;
        else {
            check::is_true(!seen_hidden, "no known school after a hidden one");
            ++known;
        }
        check::is_true(!s.name.empty() && !s.group.empty() && !s.descriptors.empty(), "a named school");
    }
    check::equal(known, 8, "eight known schools");
    check::equal(std::string(table[0].name), std::string("weave"), "position 0");
    check::equal(magic_school_index("weave"), 0, "index by name");
    check::equal(magic_school_index("not_a_school"), -1, "an unknown school");
}

// --- geometry -------------------------------------------------------------

TEST(arc_frame_and_distance_agree_at_the_ends_and_the_middle) {
    const Vec3 a{1.0, 0.0, 0.0};
    const Vec3 b{0.0, 0.0, 1.0};
    const ArcFrame frame = arc_frame(a, b);
    check::close(frame.angle, std::acos(-1.0) / 2.0, 1e-12, "a quarter turn");
    check::close(dot(frame.tangent, a), 0.0, 1e-12, "the tangent is perpendicular at a");
    check::close(std::sqrt(dot(frame.tangent, frame.tangent)), 1.0, 1e-12, "a unit tangent");

    // Angular distance is computed as acos of a dot product, and acos LOSES HALF THE
    // MANTISSA near 1: acos(1 - e) is about sqrt(2e), so a dot product one ulp below 1
    // reads roughly 1.5e-8 rather than 0. That is a property of the formula, not a
    // defect, and it is why nothing downstream may compare an arc distance against a
    // tolerance tighter than about 1e-7. An earlier version of this test used 1e-12
    // and failed for exactly that reason.
    constexpr double kArcTolerance = 1e-7;
    check::close(distance_to_frame(a, frame), 0.0, kArcTolerance, "zero at one end");
    check::close(distance_to_frame(b, frame), 0.0, kArcTolerance, "zero at the other");
    // A point on the arc's interior is also at zero.
    const Vec3 middle = unit({1.0, 0.0, 1.0});
    check::close(distance_to_frame(middle, frame), 0.0, kArcTolerance, "zero on the arc");
    // A point off the arc's plane is at its angular offset.
    check::close(distance_to_frame({0.0, 1.0, 0.0}, frame), std::acos(-1.0) / 2.0, 1e-12,
                 "a right angle off the plane");
    // Beyond an endpoint the nearest point is the endpoint, never the great circle.
    const Vec3 past = unit({1.0, 0.0, -0.2});
    check::is_true(distance_to_frame(past, frame) > 0.0, "past the end is not on the arc");
}

TEST(network_geometry_is_seeded_separated_and_connected) {
    for (const int count : {3, 8, 16}) {
        const NetworkGeometry a = network_geometry(12345, count);
        const NetworkGeometry b = network_geometry(12345, count);
        check::equal(a.nodes.size(), static_cast<std::size_t>(count), "the requested key points");
        check::equal(a.edges.size(), b.edges.size(), "the geometry to replay from its seed");

        for (const Vec3& p : a.nodes) {
            check::close(std::sqrt(dot(p, p)), 1.0, 1e-9, "a key point on the sphere");
        }
        // No two key points may coincide or be antipodal: either makes a degenerate arc.
        for (std::size_t i = 0; i < a.nodes.size(); ++i) {
            for (std::size_t j = i + 1; j < a.nodes.size(); ++j) {
                check::is_true(std::abs(dot(a.nodes[i], a.nodes[j])) < 0.9999,
                               "key points to be separated");
            }
        }
        for (const auto& [from, to] : a.edges) {
            check::is_true(from != to, "no self link");
            check::is_true(from >= 0 && from < count && to >= 0 && to < count, "an edge in range");
            check::is_true(from < to, "edges to be stored low-to-high");
        }
        check::between(a.distribution.clusters, 1, std::min(4, count), "cluster count");
    }
}

TEST(three_key_points_share_a_straight_alignment) {
    // The one deliberate piece of structure in an otherwise scattered layout: nodes 0,
    // 2 and 1 lie on one great circle, and 2 sits strictly between the other two.
    const NetworkGeometry g = network_geometry(999, 8);
    check::equal(g.distribution.aligned_nodes.size(), std::size_t{3}, "three aligned nodes");

    const ArcFrame frame = arc_frame(g.nodes[0], g.nodes[1]);
    check::close(distance_to_frame(g.nodes[2], frame), 0.0, 1e-7, "node 2 to lie on the 0-1 arc");

    // And with fewer than three there is no alignment to make.
    check::is_true(network_geometry(999, 2).distribution.aligned_nodes.empty(), "no alignment below three");
}

// --- dominance ------------------------------------------------------------

TEST(dominance_needs_both_a_floor_and_a_lead) {
    // Strong enough AND clearly ahead. Either alone is not enough, which is what keeps
    // contested ground unowned rather than arbitrarily assigned.
    check::equal(dominant_school({0.9, 0.1, 0.0}, 0.35, 0.08), 0, "a clear winner");
    check::equal(dominant_school({0.2, 0.1, 0.0}, 0.35, 0.08), -1, "ahead but below the floor");
    check::equal(dominant_school({0.9, 0.88, 0.0}, 0.35, 0.08), -1, "strong but not clearly ahead");
    check::equal(dominant_school({0.43, 0.35, 0.0}, 0.35, 0.08), 0, "exactly on both limits");
    check::equal(dominant_school({}, 0.35, 0.08), -1, "nothing at all");
    check::equal(dominant_school({0.0, 0.0}, 0.35, 0.08), -1, "an empty world is unowned");
}

// --- the fields -----------------------------------------------------------

TEST(every_school_publishes_a_field_and_generation_raises_no_hidden_one) {
    const World world = build();
    check::is_true(world.has_magic, "a phase-6 world to have magic");
    for (const MagicSchool& s : magic_school_table()) {
        const std::string name(s.name);
        check::equal(world.layers.count("ley_" + name), std::size_t{1}, "a field for " + name);
        check::equal(world.layers.count("instability_" + name), std::size_t{1}, "instability for " + name);
        const LeyNetwork& net = world.magic.networks.at(name);
        if (s.hidden) {
            // Locked at zero occurrence. Only the corruption API can put a node in one,
            // and that runs long after generation.
            check::is_true(!net.manifested, "generation never raises " + name);
            check::is_true(net.nodes.empty() && net.edges.empty(), name + " to stay empty");
        }
    }
    const int manifested = static_cast<int>(std::count_if(
        magic_school_table().begin(), magic_school_table().end(),
        [&](const MagicSchool& s) { return world.magic.networks.at(std::string(s.name)).manifested; }));
    check::is_true(manifested > 0, "a magical world to raise some school");
}

TEST(the_derived_magic_fields_stay_in_their_ranges) {
    const World world = build();
    for (const char* name : {"magic_density", "magic_hazard", "magic_growth", "magic_opposition"}) {
        for (const double v : world.layers.at(name).cells()) {
            check::between(v, 0.0, 1.0, std::string(name) + " is a 0..1 field");
        }
    }
    // dominant_magic is -1 or a valid index into the school table.
    const auto schools = static_cast<int>(magic_school_table().size());
    for (const double v : world.layers.at("dominant_magic").cells()) {
        const int index = static_cast<int>(v);
        check::is_true(index == -1 || (index >= 0 && index < schools), "a school index or none");
    }
    for (const MagicSchool& s : magic_school_table()) {
        const LeyNetwork& net = world.magic.networks.at(std::string(s.name));
        for (const double v : world.layers.at("ley_" + std::string(s.name)).cells()) {
            check::is_true(v <= net.strength + 1e-12, "potency saturates at the school's strength");
        }
    }
}

TEST(the_compatibility_projections_are_projections) {
    // ley_holy and ley_primordial are VIEWS, never networks: nothing seeds or edits
    // them, and a consumer that writes to one is writing to a derived field.
    const World world = build(33);
    const Grid& holy = world.layers.at("ley_holy");
    const Grid& radiant = world.layers.at("ley_radiant");
    for (int z = 0; z < holy.size(); ++z) {
        for (int x = 0; x < holy.size(); ++x) {
            check::equal(holy.at(x, z), radiant.at(x, z), "ley_holy to be ley_radiant");
        }
    }
    const Grid& primordial = world.layers.at("ley_primordial");
    for (int z = 0; z < primordial.size(); ++z) {
        for (int x = 0; x < primordial.size(); ++x) {
            const double best = std::max({world.layers.at("ley_fire").at(x, z),
                                          world.layers.at("ley_water").at(x, z),
                                          world.layers.at("ley_earth").at(x, z),
                                          world.layers.at("ley_air").at(x, z)});
            check::equal(primordial.at(x, z), best, "ley_primordial to be the primordial maximum");
        }
    }
}

TEST(magic_can_be_switched_off_entirely) {
    const World world = build(33, 0);
    check::is_true(!world.magic.enabled, "magic disabled");
    for (const MagicSchool& s : magic_school_table()) {
        check::is_true(!world.magic.networks.at(std::string(s.name)).manifested,
                       std::string(s.name) + " not to manifest");
    }
    for (const double v : world.layers.at("magic_density").cells()) {
        check::close(v, 0.0, 1e-12, "no density without magic");
    }
    for (const double v : world.layers.at("dominant_magic").cells()) {
        check::equal(v, -1.0, "nowhere belongs to a school");
    }
}

TEST(school_width_scales_with_the_world) {
    // Widths are authored as a reach on an 11.15 km reference world. Left absolute, a
    // 110 m Gaussian on a 200 km planet falls between raster cells and every magic
    // field collapses to zero -- which is a published layer becoming a constant.
    Request small;
    small.seed = 42;
    small.size = 33;
    small.phase = 6;
    small.world_size = WorldSize::Small;
    Request large = small;
    large.world_size = WorldSize::Large;

    const World a = generate(resolve(small));
    const World b = generate(resolve(large));
    const double wa = a.magic.networks.at("weave").width_m;
    const double wb = b.magic.networks.at("weave").width_m;
    check::close(wb / wa, 3.0, 1e-9, "a triple-width world to triple the ley reach");
    check::is_true(wa > 0.0, "a positive width");
}

// --- and what the leylines unlock ----------------------------------------

TEST(leylines_unblock_the_magical_regions) {
    // Ten of the thirteen regions read the ley layers for eligibility. Before leylines
    // existed only the non-magical ones could manifest, and the block reported
    // "insufficient suitable habitat" for the rest -- correctly, but emptily.
    const World with_magic = build(65, 1);
    const World without = build(65, 0);

    const auto manifested = [](const World& w) {
        return std::count_if(w.ecology.regions.begin(), w.ecology.regions.end(),
                             [](const RegionInfluence& r) { return r.reason == "manifested"; });
    };
    check::is_true(manifested(with_magic) > manifested(without),
                   "magic to bring more regions into being (" + std::to_string(manifested(without)) +
                       " -> " + std::to_string(manifested(with_magic)) + ")");

    // And the magic-dependent ones specifically cannot appear without it.
    for (const RegionInfluence& r : without.ecology.regions) {
        if (r.id == "demonic" || r.id == "enchanted" || r.id == "fungal" || r.id == "haunted_sands") {
            check::is_true(r.centers.empty(), r.id + " to need magic");
        }
    }
}
