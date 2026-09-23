// World -> JSON document.
//
// The document is the product: an engine, a tool or a language model reads this and
// never links against the library. It therefore states what it is -- which stage it
// reached, which octaves resolved, what the numbers mean and what they do not -- as
// part of the payload rather than in documentation the consumer will not have.
#pragma once

#include <iosfwd>
#include <vector>

#include "fwg/build_stages.hpp"
#include "fwg/detail.hpp"
#include "fwg/world.hpp"

namespace fwg {

// The `fwg.world` document's version. 2 (2026-09-23): the reference's published key tree,
// pinned by tests/test_document_paths.cpp; keys were added, renamed and removed against 1,
// so a reader of version 1 must not take a version-2 document for one.
inline constexpr int kWorldSchemaVersion = 2;

// Timings are OFF by default and that is deliberate. A wall-clock measurement in
// the document makes two runs of the same seed differ, which costs the one property
// worth most downstream: a world document can be hashed, cached and compared. The
// numbers are still on the World struct for anyone who wants them, and `--timings`
// puts them back in when the document is being read by a human rather than a tool.
void write_world_json(std::ostream& out, const World& world, bool pretty = true, bool include_timings = false);

struct DocumentOptions {
    bool pretty = true;
    bool include_timings = false;
    // The stage snapshots a BuildStageRecorder captured while this world generated, written
    // as `build_stages`. Null writes none, which is the default because no consumer of an
    // exported world reads them and they are several times the world's own size.
    const std::vector<BuildStage>* build_stages = nullptr;
};
void write_world_json(std::ostream& out, const World& world, const DocumentOptions& options);

// A height tile (export_height_tile, detail.hpp) as the reference's height-tile document:
// schema `fantasy-world-generator.height-tile` version 1, key for key -- the tile's frame, the
// terrain-detail definition it was sampled with, and its heights, row-major. `world` is the
// world the tile was exported from; it supplies the definition and the generator version.
// Compact by default, as interchange output is.
inline constexpr int kHeightTileVersion = 1;
void write_height_tile_json(std::ostream& out, const World& world, const HeightTile& tile, bool pretty = false);

}  // namespace fwg
