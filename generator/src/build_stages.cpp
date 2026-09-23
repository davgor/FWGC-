#include "fwg/build_stages.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "fwg/json_scan.hpp"
#include "fwg/serialize.hpp"
#include "fwg/world.hpp"

namespace fwg {
namespace {

constexpr std::string_view kStageTitles[] = {
    "Plate layout",       "Tectonic relief",   "Surface detail",     "Erosion and sediment",
    "Connected water",    "Second tectonic relief", "Valleys and gorges", "Wind and rain",
    "Leylines",           "Founding",          "Roads",              "Populated regions",
    "Beasties and animals", "Age transition 1", "Age transition 2",  "Simulation complete"};

// The reference's STATE_KEYS, in its order. `world_clock` and `quests` are written by the
// time API rather than by generation; they are here because the list is the reference's.
constexpr std::string_view kStateKeys[] = {
    "world_scene", "terrain_detail", "wildlife", "city_plans", "hamlet_plans", "castle_plans",
    "civilizations", "history", "ocean_archipelagos", "sediment_budget", "terrain", "water", "climate",
    "magic", "settlements", "roads", "humans", "sky", "beast_nests", "threat_assessments", "ruins",
    "villains", "heroes", "story_web", "npcs", "key_locations", "key_location_plans", "nomads",
    "beast_movements", "encounters", "settlement_candidates", "world_clock", "quests", "habitats",
    "regions", "seasonal_environment", "population_budget", "peoples", "population",
    "population_profiles", "seasonal_food", "fisheries", "transport", "world_economy",
    "geological_history", "area", "astrology", "lunar_almanac", "religion"};

const json_scan::Member* find(const std::vector<json_scan::Member>& members, std::string_view key) {
    for (const json_scan::Member& m : members) {
        if (m.key == key) return &m;
    }
    return nullptr;
}

// A Python dict, as far as materialize_stage uses one: assignment keeps an existing key's
// place, a new key goes to the end, and a removed key that comes back goes to the end.
class Ordered {
public:
    void set(std::string_view key, std::string value) {
        for (auto& [k, v] : items_) {
            if (k == key) {
                v = std::move(value);
                return;
            }
        }
        items_.emplace_back(std::string(key), std::move(value));
    }
    void erase(std::string_view key) {
        items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const auto& item) { return item.first == key; }),
                     items_.end());
    }
    std::string object() const {
        std::string out = "{";
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (i != 0) out += ',';
            out += json_scan::quoted(items_[i].first);
            out += ':';
            out += items_[i].second;
        }
        out += '}';
        return out;
    }

private:
    std::vector<std::pair<std::string, std::string>> items_;
};

}  // namespace

std::span<const std::string_view> stage_titles() { return kStageTitles; }

std::span<const std::string_view> state_keys() { return kStateKeys; }

bool is_state_key(std::string_view key) {
    return std::find(std::begin(kStateKeys), std::end(kStateKeys), key) != std::end(kStateKeys);
}

void BuildStageRecorder::capture(int stage, const World& world) {
    std::ostringstream out;
    write_world_json(out, world, /*pretty=*/false, /*include_timings=*/false);
    const std::string document = out.str();
    const std::vector<json_scan::Member> members = json_scan::object_members(document);

    BuildStage entry;
    entry.stage = stage;
    entry.title = stage >= 1 && stage <= static_cast<int>(std::size(kStageTitles))
                      ? std::string(kStageTitles[stage - 1])
                      : "Stage " + std::to_string(stage);

    // Layers: every one whose text moved, then every one that is gone. layers_ is ordered by
    // name, so the removed list comes out sorted, as the reference sorts it.
    std::vector<std::string> live;
    if (const json_scan::Member* layers = find(members, "layers")) {
        for (json_scan::Member& layer : json_scan::object_members(layers->value)) {
            live.push_back(layer.key);
            const auto previous = layers_.find(layer.key);
            if (previous != layers_.end() && *previous->second == layer.value) continue;
            auto text = std::make_shared<const std::string>(layer.value);
            entry.layers.emplace_back(layer.key, text);
            layers_.insert_or_assign(std::move(layer.key), std::move(text));
        }
    }
    std::sort(live.begin(), live.end());
    for (auto it = layers_.begin(); it != layers_.end();) {
        if (!std::binary_search(live.begin(), live.end(), it->first)) {
            entry.removed_layers.push_back(it->first);
            it = layers_.erase(it);
        } else {
            ++it;
        }
    }

    // State keys, in the reference's order. Absent and null are the same thing to it:
    // `result.get(k)` is None either way.
    for (const std::string_view key : kStateKeys) {
        const json_scan::Member* current = find(members, key);
        if (current != nullptr && json_scan::is_null(current->value)) current = nullptr;
        const auto previous = state_.find(key);
        if (current == nullptr) {
            if (previous != state_.end()) {
                entry.state.emplace_back(std::string(key), nullptr);
                state_.erase(previous);
            }
            continue;
        }
        if (previous != state_.end() && *previous->second == current->value) continue;
        auto text = std::make_shared<const std::string>(current->value);
        entry.state.emplace_back(std::string(key), text);
        state_.insert_or_assign(std::string(key), std::move(text));
    }
    stages_.push_back(std::move(entry));
}

std::string materialize_stage(std::string_view document, int stage) {
    const std::vector<json_scan::Member> members = json_scan::object_members(document);
    const json_scan::Member* snapshots = find(members, "build_stages");
    if (snapshots == nullptr) {
        throw std::invalid_argument("materialize_stage: the document carries no build_stages");
    }
    const std::vector<std::string_view> entries = json_scan::array_items(snapshots->value);
    if (stage < 1 || stage > static_cast<int>(entries.size())) throw std::invalid_argument("Stage was not generated");

    Ordered out;
    for (const json_scan::Member& m : members) {
        if (is_state_key(m.key) || m.key == "layers" || m.key == "build_stages") continue;
        out.set(m.key, std::string(m.value));
    }
    out.set("layers", {});  // placed now, filled once the layers are known

    Ordered layers;
    for (int i = 0; i < stage; ++i) {
        const std::vector<json_scan::Member> fields = json_scan::object_members(entries[static_cast<std::size_t>(i)]);
        // The reference's order within one entry: update the layers, THEN drop the removed
        // ones -- a layer both written and removed at one stage ends up removed -- then the
        // state.
        if (const json_scan::Member* written = find(fields, "layers")) {
            for (const json_scan::Member& layer : json_scan::object_members(written->value)) {
                layers.set(layer.key, std::string(layer.value));
            }
        }
        if (const json_scan::Member* removed = find(fields, "removed_layers")) {
            for (const std::string_view name : json_scan::array_items(removed->value)) {
                layers.erase(json_scan::string_value(name));
            }
        }
        if (const json_scan::Member* state = find(fields, "state")) {
            for (const json_scan::Member& block : json_scan::object_members(state->value)) {
                if (json_scan::is_null(block.value)) {
                    out.erase(block.key);
                } else {
                    out.set(block.key, std::string(block.value));
                }
            }
        }
    }
    out.set("layers", layers.object());
    return out.object();
}

}  // namespace fwg
