// build_stages: the stage inspector's per-stage snapshots, opt-in.
//
// The browser lab scrubs a world back through its sixteen stages. The reference makes that
// possible by capturing, after every stage, what changed since the stage before -- every
// layer whose values moved, every layer that disappeared, and every STATE KEY (a fixed list
// of top-level blocks) whose content moved -- and publishing the list as `build_stages`.
// Replaying the first N entries over the finished document's non-state keys rebuilds the
// world as stage N left it (materialize_stage below).
//
// COMPARED AND KEPT AS DOCUMENT TEXT. A snapshot is taken by serialising the stage's World
// with the ordinary document writer and splitting the result at its top-level keys (and the
// `layers` object at its layers); "changed" means the bytes differ. That is the reference's
// content equality, for the document as written, and it keeps each changed block exactly as
// it will appear: nothing is re-serialised when the snapshots are written out. A block that
// did not change is held once, shared between the stage that last wrote it and the
// comparison that follows.
//
// OPT-IN, as in the reference (`--include-build-stages`): the snapshots are roughly the size
// of the world again several times over, and no consumer of an exported world reads them.
// Nothing here runs unless a caller attaches a recorder to generate().
#pragma once

#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fwg {

struct World;

// The sixteen stage titles, in ladder order: every snapshot's title and `phases.titles`.
std::span<const std::string_view> stage_titles();

// The top-level blocks a snapshot tracks -- the reference's STATE_KEYS, in its order. A
// block not in this list is invisible to the stage inspector: materialize_stage takes it
// from the finished document whatever stage is asked for.
std::span<const std::string_view> state_keys();
bool is_state_key(std::string_view key);

// One stage's delta. Every value is compact JSON text, exactly as the document writer
// produced it; a state value of nullptr means the block was removed at this stage (it is
// written as null).
struct BuildStage {
    int stage = 0;
    std::string title;
    std::vector<std::pair<std::string, std::shared_ptr<const std::string>>> layers;  // changed, document order
    std::vector<std::string> removed_layers;                                           // sorted
    std::vector<std::pair<std::string, std::shared_ptr<const std::string>>> state;    // changed, state_keys() order
};

class BuildStageRecorder {
public:
    // Serialise `world` as a compact document without timings and record what changed since
    // the previous capture. Captures must arrive in stage order, one per stage.
    void capture(int stage, const World& world);
    const std::vector<BuildStage>& stages() const { return stages_; }

private:
    std::vector<BuildStage> stages_;
    // The last text seen for every live layer and every present state key, shared with the
    // snapshot that recorded it.
    std::map<std::string, std::shared_ptr<const std::string>, std::less<>> layers_;
    std::map<std::string, std::shared_ptr<const std::string>, std::less<>> state_;
};

// The reference's materialize_stage, over a document that carries `build_stages`: the
// finished document's non-state keys (everything but the state keys, `layers` and
// `build_stages`), then `layers`, then every state key as the first `stage` snapshots left
// it. Returns compact JSON. Key order is the reference's dict order: the non-state keys in
// document order, `layers` next, and state keys in the order they were first set, a key
// that was removed and set again going to the end.
//
// Throws std::invalid_argument("Stage was not generated") for a stage outside
// 1..len(build_stages), and std::invalid_argument when the document is not an object or
// carries no build_stages.
std::string materialize_stage(std::string_view document, int stage);

}  // namespace fwg
