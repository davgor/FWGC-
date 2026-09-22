// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/biomes.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/content.hpp"

namespace fwg {
namespace {

// ORDER IS A CONTRACT. `biome_variant` stores a flat index into the cross
// product of these rows with the magic schools, so inserting a row renumbers
// every variant in every world ever saved. Append, never insert.
constexpr NaturalBiome kNaturalBiomes[] = {
    {0, "ocean", "Ocean", "terrain.biome.000", {42, 102, 147}, true},
    {1, "tundra", "Tundra", "terrain.biome.001", {156, 164, 126}, false},
    {2, "desert", "Desert", "terrain.biome.002", {218, 184, 120}, true},
    {3, "grassland", "Grassland", "terrain.biome.003", {139, 176, 99}, true},
    {4, "forest", "Forest", "terrain.biome.004", {65, 135, 80}, true},
    {5, "exposed_rock", "Exposed rock", "terrain.biome.005", {145, 143, 139}, true},
    {6, "snow", "Snow", "terrain.biome.006", {230, 240, 241}, false},
    {7, "rainforest", "Rainforest", "terrain.biome.007", {22, 83, 58}, true},
    {8, "lake", "Lake", "terrain.biome.008", {68, 160, 185}, true},
    {13, "marsh", "Marsh", "terrain.biome.013", {105, 135, 101}, true},
    {15, "boreal_forest", "Boreal forest", "terrain.biome.015", {56, 104, 95}, true},
    {16, "cold_tundra", "Cold tundra", "terrain.biome.016", {152, 164, 136}, true},
    {17, "land_ice", "Persistent land ice", "terrain.biome.017", {223, 240, 245}, true},
};

// One variant name per natural biome, in catalogue order, per school.
constexpr std::string_view kSchoolNames[] = {
    "air",
    "blood",
    "earth",
    "eldritch",
    "fire",
    "infernal",
    "radiant",
    "rot",
    "umbral",
    "void",
    "water",
    "weave",
};

constexpr std::string_view kVariantNames[][13] = {
    {"Storm seas", "Gale tundra", "Whistling dunes", "Thundergrass plains", "Skyreach woods", "Windcarved spires", "Dancing snow", "Stormcrown jungle", "Suspended mist lakes", "Cloudveil marsh", "Singing pines", "Force-swept tundra", "Howling glaciers"},
    {"Arterial tides", "Weeping permafrost", "Scab flats", "Redgrass steppe", "Vein-root woods", "Marrow spires", "Crimson drifts", "Pulsing canopy", "Clotted meres", "Ichor fen", "Hollow-bone taiga", "Frozen carrion plains", "Bloodglass ice"},
    {"Rootreef seas", "Mossbound tundra", "Blooming dunes", "Titan meadows", "Ancient rootwoods", "Living monoliths", "Mosswarm snow", "Colossal jungle", "Rootcradle lakes", "Deep-root marsh", "Ironbark taiga", "Lichenstone plains", "Rootsplit glaciers"},
    {"Watching seas", "Whispering barrens", "Impossible dunes", "Spiral meadows", "Unmeeting woods", "Wrong-angle crags", "Susurrus snow", "Writhing canopy", "Fathomless eyes", "Murmuring mire", "Listening pines", "Dreaming frostlands", "Sleeper ice"},
    {"Steam seas", "Ember tundra", "Furnace dunes", "Flamegrass plains", "Phoenix woods", "Molten crags", "Smoldering snow", "Emberbloom jungle", "Boiling lakes", "Cinder mire", "Firecone woods", "Ashfrost plains", "Steamcut glaciers"},
    {"Brimstone seas", "Blighted tundra", "Hellglass wastes", "Cinder blight", "Thornhell woods", "Demon spires", "Sootsnow fields", "Devouring jungle", "Bloodglass lakes", "Corruption mire", "Charred blackwoods", "Torment barrens", "Infernal glaciers"},
    {"Dawn seas", "Blessed tundra", "Golden sanctuaries", "Healing meadows", "Sanctuary woods", "Hallowed heights", "Luminous snow", "Mercy gardens", "Lustral lakes", "Purifying marsh", "Dawnlit pines", "Peaceful frostlands", "Cathedral ice"},
    {"Septic seas", "Thawing sickbeds", "Spore dunes", "Blightgrass meadows", "Fruiting deadwoods", "Mouldered stone", "Grey slush fields", "Putrid canopy", "Stagnant pools", "Plague fen", "Softwood taiga", "Preserved dead plains", "Weeping glaciers"},
    {"Silent seas", "Grave tundra", "Haunted tombs", "Ashen meadows", "Mourning woods", "Ossuary crags", "Funeral snow", "Withering canopy", "Stillwater tombs", "Haunted marsh", "Ghost pines", "Deathfrost plains", "Sepulchral ice"},
    {"Unlit deeps", "Erased barrens", "Ashless waste", "Muted plains", "Soundless woods", "Unmade crags", "Colourless drifts", "Swallowed canopy", "Starless waters", "Forgetting mire", "Null pines", "Emptied frostlands", "Absence glaciers"},
    {"Crystal currents", "Rime tundra", "Frostglass dunes", "Dew meadows", "Rainveil woods", "Springstone cliffs", "Sapphire snow", "Deluge jungle", "Winterglass lakes", "Tidal gardens", "Mistbound pines", "Bluefrost tundra", "Everflow glaciers"},
    {"Prismatic seas", "Dream tundra", "Glass mirages", "Possibility meadows", "Living storywoods", "Floating stonefields", "Chromatic snow", "Everchanging canopy", "Starlight lakes", "Spellmist marsh", "Aurora woods", "Shifting frostlands", "Singing glaciers"},
};

}  // namespace

std::span<const NaturalBiome> natural_biomes() { return kNaturalBiomes; }

const NaturalBiome* natural_biome(int id) {
    for (const NaturalBiome& row : kNaturalBiomes) {
        if (row.id == id) return &row;
    }
    return nullptr;
}

std::span<const std::string_view> magic_schools() { return kSchoolNames; }

std::string_view variant_name(std::size_t school_index, std::size_t biome_index) {
    return kVariantNames[school_index][biome_index];
}

}  // namespace fwg
