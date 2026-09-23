// The read API: the reference's terrain_read request functions over a World.
//
// Glue. Every answer is a selector's (read_select.cpp); this file owns only the request
// envelope -- the refusals, in the reference's order -- and the document each read returns.
#include "fwg/read.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

#include "fwg/world.hpp"

namespace fwg {
namespace {

// The shared refusal builders (world_time.hpp), under a name that fits on a line.
namespace refuse = ::fwg::request_errors;

constexpr std::string_view kReadNames[] = {"blocks", "near", "place", "person", "quests"};

// Python's %g, for the two sentences that print a radius.
std::string general(double value) {
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%g", value);
    return std::string(buffer.data());
}

void check_api(int api_version) {
    if (api_version != kReadApiVersion) throw refuse::unsupported_api(api_version, kReadApiVersion);
}

ReadEnvelope envelope(std::string_view read, const World& world) {
    ReadEnvelope out;
    out.schema = std::string(kReadSchemaPrefix) + std::string(read);
    out.read = read;
    out.source.generator_version = world.generator_version;
    out.source.seed = world.config.seed;
    out.source.size = world.config.size;
    out.source.phase = world.config.phase;
    if (world.world_clock) {
        out.source.day = world.world_clock->day;
        out.source.age = world.world_clock->age;
    }
    return out;
}

// A world can only be asked what is near a node if it has nodes. Deliberately NOT the time
// or age gates: those answer whether a world can be CHANGED, and a read that refused to
// describe a world it could describe would be a worse answer than the description.
void require_grid(const World& world) {
    const int size = grid_size(world);
    const double radius = globe_radius(world);
    if (size < 2) {
        throw refuse::missing_block("config", "A near read needs the grid this world was built on; config.size is " +
                                                  std::to_string(size) + ".");
    }
    // NaN is refused here as well as zero and below: the reference's `radius <= 0` let a NaN
    // radius through to answer with NaN distances.
    if (!(radius > 0.0)) {
        throw refuse::missing_block("effective_config",
                                    "A near read measures in metres on this world's globe; "
                                    "effective_config.globe_radius is " +
                                        refuse::render(radius) + ".");
    }
}

constexpr ReadJoin kNearJoins[] = {
    {"encounters", "encounters.by_node[\"<node>\"]", "encounters.occupancy[].entry -> encounters.entries[]",
     "src/beasts.cpp add_encounters (index.by_node)"},
    {"distance", "the node direction", "great-circle metres on effective_config.globe_radius",
     "src/grid.cpp sphere_grid (the neighbour arc), include/fwg/vec3.hpp great_circle_m"},
};

constexpr ReadJoin kQuestJoins[] = {
    {"giver", "quests.quests[].giver_uid", "heroes.people[].uid / heroes.dreads[].uid / npcs.people[].uid",
     "src/quests.cpp QuestGiverIndex, fed by src/world_time.cpp quest_inputs_of"},
    {"anchor", "quests.quests[].anchor", "carried verbatim from heroes.quest_hooks[].target",
     "src/quests.cpp quest_step (fresh.anchor = hook->target)"},
};

}  // namespace

std::span<const std::string_view> read_names() { return std::span<const std::string_view>(kReadNames); }

// ---------------------------------------------------------------------------

BlocksRead read_blocks(const World& world, const BlocksReadRequest& request) {
    check_api(request.api_version);
    BlocksRead out;
    out.envelope = envelope("blocks", world);
    out.summary = block_summary(world);
    out.encoding =
        "json; the writer's key order (which moves no byte count); no whitespace; utf-8. NaN, and infinity in every "
        "block written from stage 12 on, are written as null and count as encodable; an infinity spelled 1e999 makes "
        "its block unencodable. The world document as write_world_json writes it by default, without timings.";
    return out;
}

// ---------------------------------------------------------------------------

NearRead read_near(const World& world, const NearReadRequest& request) {
    check_api(request.api_version);
    if (!request.node) {
        throw refuse::wrong_type("node", "None", "an integer");
    }
    const int node = *request.node;
    const double radius_m = request.radius_m;
    if (!std::isfinite(radius_m)) throw refuse::wrong_type("radius_m", refuse::render(radius_m), "a finite number");
    if (radius_m < 0.0) throw refuse::out_of_range("radius_m", radius_m, 0.0, std::nullopt, "m");

    require_grid(world);
    const std::size_t count = node_count(world);
    if (node < 0 || static_cast<std::size_t>(node) >= count) {
        throw refuse::refused_by_world("node", std::to_string(node),
                                       "This world is grid size " + std::to_string(grid_size(world)) + " and carries " +
                                           std::to_string(count) + " terrain nodes, numbered 0.." +
                                           std::to_string(count - 1) + ".",
                                       {});
    }
    const double radius = globe_radius(world);
    const double ceiling = radius * kNearRadiusFraction;
    if (radius_m > ceiling) {
        throw refuse::refused_by_world("radius_m", refuse::render(radius_m),
                                       "A near read reaches at most half this globe's radius (" + general(ceiling) +
                                           " m). This world has radius " + general(radius) + " m.",
                                       {});
    }

    NearRows rows = near_rows(world, node, radius_m);
    const std::size_t total = rows.total();
    if (total > kMaxNearRows) {
        // Refused rather than truncated. A truncated answer to "what is near here" is a wrong
        // answer that looks complete, and the caller has a remedy it can apply.
        throw refuse::over_capacity("near.rows", total, kMaxNearRows, "rows per read; ask a smaller radius_m");
    }

    NearRead out;
    out.envelope = envelope("near", world);
    out.node = node;
    out.radius_m = radius_m;
    out.radius_ceiling_m = ceiling;
    out.row_ceiling = kMaxNearRows;
    out.rows = total;
    out.cell.cell = node_cell(world, node);
    out.cell.layers = layer_sample(world, out.cell.cell.x, out.cell.cell.z);
    out.near = std::move(rows);
    out.joins.assign(std::begin(kNearJoins), std::end(kNearJoins));
    return out;
}

// ---------------------------------------------------------------------------

PlaceRead read_place(const World& world, const PlaceReadRequest& request) {
    check_api(request.api_version);
    if (request.place_id.empty()) throw refuse::wrong_type("place_id", "''", "text");
    if (!world.has_settlements) {
        // "(phase 9)" is the reference's sentence and it is wrong: founding is stage 10.
        // Reproduced, and pinned by a test, rather than silently corrected.
        throw refuse::missing_block("settlements",
                                    "This world carries no settlements, so it has no places to look in. "
                                    "Generate it through founding (phase 9).");
    }
    std::optional<PlaceDocument> found = place_document(world, request.place_id, kMaxPlacePosts);
    if (!found) {
        std::vector<std::string> known;
        for (PlaceId& id : place_ids(world)) known.push_back(std::move(id.id));
        throw refuse::unknown_name(request.place_id, known, "place id");
    }
    PlaceRead out;
    out.envelope = envelope("place", world);
    out.place = std::move(*found);
    out.post_ceiling = kMaxPlacePosts;
    return out;
}

// ---------------------------------------------------------------------------

PersonRead read_person(const World& world, const PersonReadRequest& request) {
    check_api(request.api_version);
    if (request.uid.empty()) throw refuse::wrong_type("uid", "''", "text");
    if (!world.has_heroes && !world.has_npcs && !world.has_villains) {
        throw refuse::missing_block("heroes",
                                    "This world carries no heroes, npcs or villains, so it names no people. "
                                    "Generate it through the cast (phase 16).");
    }
    std::optional<PersonDocument> found = person_document(world, request.uid);
    if (!found) {
        std::vector<std::string> known;
        for (const auto& [uid, entry] : person_index(world)) known.push_back(uid);
        throw refuse::unknown_name(request.uid, known, "person uid");
    }
    PersonRead out;
    out.envelope = envelope("person", world);
    out.person = std::move(*found);
    return out;
}

// ---------------------------------------------------------------------------

QuestsRead read_quests(const World& world, const QuestsReadRequest& request) {
    check_api(request.api_version);
    if (request.states) {
        const std::span<const QuestState> vocabulary = quest_states();
        for (const QuestState state : *request.states) {
            if (std::find(vocabulary.begin(), vocabulary.end(), state) != vocabulary.end()) continue;
            std::vector<std::string> choices;
            for (const QuestState known : vocabulary) choices.emplace_back(to_string(known));
            throw refuse::invalid_choice("states", std::to_string(static_cast<int>(state)), choices);
        }
    }
    if (!world.quests) {
        // The reference names its HTTP route, POST /world/advance-time; this names the call.
        throw refuse::missing_block("quests",
                                    "This world carries no quest board. It is written by the time capability, so "
                                    "advance the world by at least a day first (advance_world_time).");
    }
    std::vector<QuestRow> rows = quest_rows(world, request.states, person_index(world));
    if (rows.size() > kMaxQuestRows) {
        throw refuse::over_capacity("quests.rows", rows.size(), kMaxQuestRows,
                                    "rows per read; narrow the states filter");
    }
    QuestsRead out;
    out.envelope = envelope("quests", world);
    out.quests = std::move(rows);
    out.counts = quest_counts(world);
    out.states = request.states;
    out.row_ceiling = kMaxQuestRows;
    out.joins.assign(std::begin(kQuestJoins), std::end(kQuestJoins));
    return out;
}

// ---------------------------------------------------------------------------

PatchRead read_patch(const World& world, const PatchReadRequest& request) {
    check_api(request.api_version);
    PatchRead out;
    out.patch = generate_patch(world, request.patch);
    out.envelope = envelope("patch", world);
    return out;
}

}  // namespace fwg
