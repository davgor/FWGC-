// Raise a band on a world that already exists. See nomad_api.hpp for the contract; this file
// is the World adapter around placement's own pieces (nomads.hpp) and stage 16's own band
// adapters (complete_stage.hpp), and implements none of either.
#include "fwg/nomad_api.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fwg/complete_stage.hpp"
#include "fwg/grid.hpp"
#include "fwg/random.hpp"
#include "fwg/request_error.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr long long kVariationMax = 4294967295LL;

std::vector<std::string> classification_names() {
    std::vector<std::string> out;
    for (int i = 0; i < kNomadClassCount; ++i) out.emplace_back(to_string(static_cast<NomadClass>(i)));
    return out;
}

std::vector<std::string> origin_kinds() { return {"world", "god", "villain"}; }

NomadOriginKind origin_kind_of(const std::string& kind) {
    if (kind == "god") return NomadOriginKind::God;
    if (kind == "villain") return NomadOriginKind::Villain;
    return NomadOriginKind::World;
}

std::string_view medium_name(NomadMedium medium) {
    switch (medium) {
        case NomadMedium::Land: return "land";
        case NomadMedium::Marine: return "marine";
        case NomadMedium::Freshwater: return "freshwater";
        case NomadMedium::SaltLake: return "salt_lake";
    }
    return "not land";
}

bool listed(const std::vector<std::string>& list, const std::string& item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

// Each pass a call re-runs records itself again; the list says what ran, once.
void dedupe_systems(std::vector<std::string>& systems) {
    std::set<std::string> seen;
    std::vector<std::string> out;
    for (std::string& name : systems) {
        if (seen.insert(name).second) out.push_back(std::move(name));
    }
    systems = std::move(out);
}

struct Checked {
    int node = 0;
    NomadBandOrigin origin;
    int age = 0;
    std::optional<NomadClass> wanted;
    std::int64_t variation = 0;
};

// Everything that can be judged before the world is copied, in the reference's order.
Checked validate(const World& world, const RaiseBandRequest& request) {
    if (request.api_version != kNomadApiVersion) {
        constexpr std::array<long long, 1> supported{kNomadApiVersion};
        throw RequestError(unsupported_api("api_version", request.api_version, supported));
    }
    if (!world.has_nomads) {
        throw RequestError(missing_block("nomads",
                                         "This world carries no nomads block. Bands are placed at stage 16, so "
                                         "generate through phase 16 before raising one."));
    }
    if (!request.node) throw RequestError(wrong_type("node", std::monostate{}, "integer"));
    if (*request.node < 0) throw RequestError(out_of_range("node", *request.node, 0, std::nullopt, "grid index"));
    if (!request.origin) throw RequestError(wrong_type("origin", std::monostate{}, "object"));
    const std::vector<std::string> kinds = origin_kinds();
    if (!listed(kinds, request.origin->kind)) {
        throw RequestError(invalid_choice("origin.kind", request.origin->kind, kinds));
    }
    if (!request.origin->age) throw RequestError(wrong_type("origin.age", std::monostate{}, "integer"));
    if (*request.origin->age < 0) {
        throw RequestError(out_of_range("origin.age", *request.origin->age, 0, std::nullopt, ""));
    }
    Checked out;
    if (request.classification) {
        const int index = nomad_class_index(*request.classification);
        if (index < 0) {
            throw RequestError(unknown_field(*request.classification, classification_names(), "classification"));
        }
        out.wanted = static_cast<NomadClass>(index);
    }
    if (request.variation < 0 || request.variation > kVariationMax) {
        throw RequestError(out_of_range("variation", request.variation, 0, kVariationMax, "seed"));
    }
    out.node = *request.node;
    out.origin = *request.origin;
    out.age = *request.origin->age;
    out.variation = request.variation;
    return out;
}

}  // namespace

World raise_band(const World& world, const RaiseBandRequest& request) {
    const Checked checked = validate(world, request);
    const auto started = std::chrono::steady_clock::now();
    const int node = checked.node;

    // Everything up to the band itself only READS the world, so it reads the caller's: the
    // reference copies first, but a copy has no effect a refusal could show, and judging the
    // ground before copying keeps a refused call as cheap as a refusal should be.
    const std::uint32_t seed = world.config.seed;
    const auto held = sphere_grid(world.effective.size, world.effective.globe_radius);
    const SphereGrid& grid = *held;
    if (static_cast<std::size_t>(node) >= grid.node_count()) {
        throw RequestError(refused_by_world("node", node,
                                            "Node " + std::to_string(node) + " is outside this world; it has " +
                                                std::to_string(grid.node_count()) + " grid points."));
    }
    const std::vector<NomadHabitatCell> cells = nomad_habitat_cells(world.layers, grid);
    const NomadHabitatCell& cell = cells[static_cast<std::size_t>(node)];
    if (cell.medium != NomadMedium::Land) {
        throw RequestError(refused_by_world("node", node,
                                            "A band cannot be raised on water; node " + std::to_string(node) + " is " +
                                                std::string(medium_name(cell.medium)) + ". Pick a land node."));
    }

    // Everything the gates read, from the world as it stands -- the same adapter placement
    // uses, so a requested band and a placed one read identical ground. The routes and the
    // write-backs below read it too: the copy they run on is this world until the band
    // joins its block, and the band set is the block's, not the view's.
    const NomadRequest view = nomad_view(world, grid, cells);
    const double swing = nomad_seasonal_swing(view.seasonal_temperature, cell.x, cell.z);
    const NomadGround ground = nomad_ground(view);

    // Every classification whose gate holds, in the contract's order. NOT filtered on a
    // zero share as placement filters it: the reference's request keeps a gate that holds at
    // a zero score, so that classification is eligible here and can be asked for by name.
    std::vector<std::pair<NomadClass, double>> shares;
    std::vector<NomadBasis> evidence;
    for (int i = 0; i < kNomadClassCount; ++i) {
        const auto value = static_cast<NomadClass>(i);
        const std::optional<NomadVerdict> verdict = nomad_gate(value, cell, swing, ground, view);
        if (!verdict) continue;
        shares.emplace_back(value, verdict->score * nomad_class_policy(value).weight);
        evidence.push_back(verdict->basis);
    }
    if (shares.empty()) {
        throw RequestError(refused_by_world("node", node,
                                            "This ground raises nobody: no classification gate holds at node " +
                                                std::to_string(node) + ". Pick another node."));
    }

    NomadClass chosen = shares.back().first;
    if (checked.wanted) {
        const bool eligible = std::any_of(shares.begin(), shares.end(),
                                          [&](const auto& share) { return share.first == *checked.wanted; });
        if (!eligible) {
            std::vector<std::string> supported;
            for (const auto& share : shares) supported.emplace_back(to_string(share.first));
            const std::string wanted(to_string(*checked.wanted));
            throw RequestError(refused_by_world("classification", wanted,
                                                "The ground at node " + std::to_string(node) + " does not support " +
                                                    wanted + ".",
                                                supported));
        }
        chosen = *checked.wanted;
    } else {
        double total = 0.0;
        for (const auto& share : shares) total += share.second;  // plain accumulation, in order
        // Keyed on the node and the variation, never on the call: the same request draws the
        // same band.
        Rng rng = Rng::child(seed, "nomad-request-" + std::to_string(node), checked.variation);
        chosen = nomad_pick(rng, shares, total).first;
    }

    const NomadClassPolicy& rule = nomad_class_policy(chosen);
    Rng sizer = Rng::child(seed, "nomad-request-size-" + std::to_string(node), checked.variation);
    const int size = static_cast<int>(sizer.randint(rule.size_low, rule.size_high));

    World out = world;

    // Keyed on the asker's age and the terrain node, suffixed past any band already there.
    std::set<std::string> existing;
    for (const NomadBand& band : out.nomads.groups) existing.insert(band.uid);
    const std::string stem = "nomad-" + std::to_string(checked.age) + "-" + std::to_string(node);
    std::string uid = stem;
    for (int suffix = 1; existing.count(uid) != 0; ++suffix) uid = stem + "-" + std::to_string(suffix);

    std::size_t which = 0;
    while (shares[which].first != chosen) ++which;
    NomadBand band = nomad_make_band(uid, chosen, cell, size, checked.age, evidence[which]);
    band.origin.kind = origin_kind_of(checked.origin.kind);
    band.origin.id = checked.origin.id.value_or(std::string());
    band.origin.age = checked.age;
    const NomadOrigin origin = band.origin;

    out.nomads.groups.push_back(std::move(band));
    std::sort(out.nomads.groups.begin(), out.nomads.groups.end(),
              [](const NomadBand& a, const NomadBand& b) { return a.uid < b.uid; });
    ++out.nomads.counts[static_cast<std::size_t>(chosen)];  // the route pass recounts anyway

    // The circuits, the encounter index and the write-backs are all functions of the band
    // set, so they are rebuilt rather than patched: rebuilding is the only way the index
    // cannot drift from the bands.
    run_nomad_routes(out, seed);
    run_encounters(out);
    run_nomad_effects(out, seed);

    NomadOperation operation;
    operation.uid = uid;
    operation.classification = chosen;
    operation.node = node;
    operation.origin = origin;
    operation.requested_classification = checked.wanted;
    operation.variation = checked.variation;
    out.history.operations.push_back(std::move(operation));

    dedupe_systems(out.systems);
    const double elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    out.timing_ms["nomad_request"] = elapsed;
    out.timing_ms["total"] += elapsed;
    return out;
}

}  // namespace fwg
