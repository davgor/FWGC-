// Stage 16, "Simulation complete": the planners, the bands, and the packages that read a
// finished world -- heroes, the story web, the rosters, the key locations.
#pragma once

#include <cstdint>
#include <vector>

#include "fwg/nomads.hpp"

namespace fwg {

struct World;
struct SphereGrid;
struct HeroesRequest;
struct KeyLocationRequest;

// Runs stage 16 over whatever world exists. Every pass here reads the world rather than
// changing its ground, so it runs the same whether or not the age transitions ran first.
void add_simulation_complete(World& world);

// The city, hamlet and castle plans and the world scene alone, redrawn over the world as it
// stands. What a summons or a departure redraws; the bands, the cast and the rosters are
// left as they were, as the reference leaves them.
void redraw_plans(World& world);

// The nomad pass's view of the world, for the band request API (nomad_api.hpp): the same
// adapter placement uses, so a requested band and a placed one read identical ground.
// `grid` must be built at world.effective.globe_radius and `cells` from it; the view points
// into both, and into the world's seasonal months.
NomadRequest nomad_view(const World& world, const SphereGrid& grid, const std::vector<NomadHabitatCell>& cells);

// The cast's and the key locations' views of the world: the adapters stage 16 hands those
// two passes, exported so a test can hold them to what the reference's passes read. Both
// point into `world` (and the second into `grid`), so neither may outlive them.
HeroesRequest heroes_view(const World& world);
KeyLocationRequest key_location_view(const World& world, const SphereGrid& grid);

// ---------------------------------------------------------------------------
// The moving world's five passes, one at a time.
//
// Stage 16 runs them in this order with the genesis seed. The time API runs each on its own
// cadence (world_time.cpp), with a seed stepped from the genesis seed on the absolute step
// index, exactly as the reference hands each pass a copy of the config with only the seed
// replaced. Each reads the world AS IT STANDS -- the route pass reads the block the nomad
// pass wrote, the encounter index reads both the bands and the herds -- and writes its own
// block, so calling them out of this order indexes bands that no longer exist.
//
// These are the same write-backs stage 16 performs, not a second copy of them: stage 16 is
// built from the same pieces.
// ---------------------------------------------------------------------------

// Place and classify the bands, replacing `world.nomads` WHOLESALE -- its routes included,
// as the reference's add_nomads replaces its block. Does nothing below phase 16 or without
// a recipe.
void run_nomads(World& world, std::uint32_t seed);

// Route the placed bands. The reference's guard: nothing to do on a world with no nomad
// block or a block with no bands, and the block is left exactly as found.
void run_nomad_routes(World& world, std::uint32_t seed);

// The herds' circuits. `seed` feeds the thinning stream only; the year's forage factor is
// keyed on the GENESIS seed (world.config.seed) so a monthly cadence cannot re-roll a per-
// year decision. `year` is the absolute year the block is built for -- see
// beast_movement_year.
void run_beast_movements(World& world, std::uint32_t seed, long long year);

// The encounter index over the bands and the herds as they stand. Draws nothing.
void run_encounters(World& world);

// What the bands write back: a cult deepens its circuit node (the fields are NOT
// re-evaluated here, as the reference does not), raiders widen a town's threat, and the
// survivor candidates are carried unless this pass has new ones. Nothing to do on a world
// with no nomad block.
void run_nomad_effects(World& world, std::uint32_t seed);

// The absolute year the beast movement pass means by `year`: the world clock's, when the
// world carries one, else the year founding ended -- the reference's world_year.
//
// **Read as it stands when the pass runs.** The time API writes the advanced clock only
// after its cadence loop, as the reference does, so a span crossing years builds the block
// for the year the span STARTED in, and an age advance builds the new age's herds for the
// year the world stood in before the ages turned. The reference documents that as a known
// imprecision; it is reproduced, not fixed.
long long beast_movement_year(const World& world);

}  // namespace fwg
