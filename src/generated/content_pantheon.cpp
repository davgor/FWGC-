// GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_content.py from data/pantheon.json.
// Edit the JSON and regenerate; `python tools/embed_content.py --check` fails if this
// file is stale.

#include "fwg/religion.hpp"

namespace fwg {
namespace {

constexpr std::string_view kWords[] = {"creation", "creativity", "chance", "magic itself", "god_air", "god_fortune_fate", "god_umbral", "weave", "death", "endings", "memory", "order", "god_grave_memory", "god_weave", "god_radiant", "umbral", "holy", "fey", "ambition", "pacts", "ruin", "temptation", "god_radiant", "god_crown_law", "infernal", "holy", "healing", "light", "law", "judgement", "god_crown_law", "god_infernal", "god_umbral", "radiant", "undead", "infernal", "unholy", "fire", "forge", "renewal", "passion", "god_forge_craft", "god_water", "fire", "undead", "sea", "rain", "ice", "patience", "god_drowned_mother", "god_fire", "water", "infernal", "harvest", "stone", "growth", "endurance", "god_hearth_harvest", "god_deep_forge", "god_air", "earth", "aberrant", "wind", "storms", "travel", "freedom", "god_road_market", "god_wanderer", "god_earth", "air", "founding", "kinship", "the walled town", "god_hearth_harvest", "radiant", "living wood", "long memory", "starlight", "god_weave", "god_infernal", "weave", "infernal", "stone", "craft", "oaths", "hoard", "god_earth", "god_fire", "god_air", "earth", "aberrant", "the sea", "fishing", "storms at sea", "drowning", "god_water", "god_fire", "water", "infernal", "floating islands", "altitude", "the far view", "god_air", "god_earth", "air", "dragons", "hoards", "fear", "fire from above", "god_fire", "god_crown_law", "fire", "fey", "holy", "madness", "the wrong shape of things", "forbidden knowledge", "god_radiant", "god_earth", "weave", "umbral", "holy", "fey", "primordial", "ruins", "lost knowledge", "warnings", "god_grave_memory", "umbral", "exile", "roads between peoples", "second chances", "god_air", "god_road_market", "air", "the moon", "tides of magic", "calendars", "the great year", "god_fortune_fate", "moon", "home", "bread", "children", "god_earth", "god_red_field", "earth", "undead", "tools", "walls", "guilds", "god_fire", "god_deep_forge", "fire", "trade", "oaths between strangers", "travel", "god_air", "god_wanderer", "air", "kingship", "law", "treaties", "god_radiant", "god_infernal", "god_great_wyrm", "radiant", "infernal", "natural death", "ancestors", "remembrance", "god_umbral", "god_fallen_cities", "umbral", "undead", "luck", "weather", "fate", "god_weave", "god_turning_moon", "weave", "war", "courage", "the honoured dead", "god_hearth_harvest", "fire", "infernal", "god_fire", "god_water", "god_earth", "god_air", "god_radiant", "god_infernal", "god_first_hearth", "god_elder_star", "god_deep_forge", "god_radiant", "god_crown_law", "god_fallen_cities", "god_wanderer", "god_drowned_mother", "god_air", "god_sky_court", "god_great_wyrm", "god_weave", "god_turning_moon", "god_drowned_mother", "god_water", "god_road_market", "god_air", "god_earth", "god_fire", "god_water", "god_crown_law", "god_wanderer", "god_drowned_mother", "god_water", "god_hearth_harvest", "god_red_field", "god_umbral", "god_fire", "god_drowned_mother", "god_air", "god_elder_star", "god_fortune_fate", "god_infernal", "god_earth", "god_great_wyrm", "god_fire", "god_umbral", "god_water", "god_crown_law", "god_hearth_harvest", "god_radiant", "god_red_field", "god_outer_unnamed", "god_deep_forge", "god_earth", "god_fire", "god_red_field", "god_air", "god_weave", "god_elder_star", "god_weave", "god_earth", "god_radiant", "god_infernal", "god_fire", "god_deep_forge", "god_weave", "god_forge_craft", "god_air", "god_fortune_fate", "god_umbral", "god_elder_star", "god_drowned_mother", "god_water", "god_air", "god_earth", "god_hearth_harvest", "god_earth", "god_fortune_fate", "god_road_market", "god_great_wyrm", "god_deep_forge", "god_water", "god_fire", "god_red_field", "god_sky_court", "sacrifice", "lineage", "vitality taken and spent", "hunger that does not end", "god_radiant", "god_hearth_harvest", "god_first_hearth", "holy", "fey", "disease", "undeath", "decay that is fertile", "rebirth nobody asked for", "god_radiant", "god_grave_memory", "holy", "madness", "wrong geometry", "dominion of minds", "knowledge that costs", "god_radiant", "god_earth", "god_crown_law", "holy", "fey", "primordial", "absence", "entropy", "unmaking", "the silence after", "god_weave", "god_fire", "god_great_wyrm", "holy", "fey", "primordial", "draconic", "cos_mosaic", "cos_ancestor_halls", "infernal", "undead"};
constexpr std::string_view kRuleTexts[] = {"human_maritime", "human_large_island", "tidekin", "aberrant", "god_radiant", "god_infernal", "god_drowned_mother", "aberrant"};
constexpr std::uint32_t kRuleChildren[] = {1u, 2u, 1u, 2u, 1u, 2u, 1u, 2u, 1u, 2u, 1u, 2u};

// Existence rules, flattened. Each god and cosmology owns a contiguous slice
// whose root is its first node; a combinator's children are INDICES into
// kRuleChildren, relative to that slice.
constexpr RuleNode kRules[] = {
    {RuleKind::school, "weave", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::school, "umbral", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::school, "infernal", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::school, "radiant", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::school, "fire", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::school, "water", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::school, "earth", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::school, "air", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::race, "", "human", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::race, "", "elf", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::race, "", "dwarf", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::any_of, "", "", 0.0, true, 0u, 0u, 0u, 2u},
    {RuleKind::ocean_share, "", "", 0.5, true, 0u, 0u, 0u, 0u},
    {RuleKind::civilization_city, "", "", 0.0, true, 0u, 3u, 0u, 0u},
    {RuleKind::sky, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::dragon, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::nest_family, "", "", 0.0, true, 3u, 4u, 0u, 0u},
    {RuleKind::ruins, "", "", 1.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::diaspora, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::always, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::always, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::always, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::always, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::always, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::always, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::always, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::always, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::primordial_count, "", "", 3.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::gods, "", "", 0.0, true, 4u, 6u, 0u, 0u},
    {RuleKind::races, "", "", 2.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::any_of, "", "", 0.0, true, 0u, 0u, 2u, 4u},
    {RuleKind::school, "weave", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::school, "earth", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::any_of, "", "", 0.0, true, 0u, 0u, 4u, 6u},
    {RuleKind::school, "radiant", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::magic_disabled, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::any_of, "", "", 0.0, true, 0u, 0u, 6u, 8u},
    {RuleKind::ruins, "", "", 2.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::school, "umbral", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::any_of, "", "", 0.0, true, 0u, 0u, 8u, 10u},
    {RuleKind::gods, "", "", 0.0, true, 6u, 7u, 0u, 0u},
    {RuleKind::sky, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::dragon, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::school, "weave", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::magic_enabled, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::always, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::magic_enabled, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::magic_enabled, "", "", 0.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::any_of, "", "", 0.0, true, 0u, 0u, 10u, 12u},
    {RuleKind::nest_family, "", "", 0.0, true, 7u, 8u, 0u, 0u},
    {RuleKind::ruins, "", "", 1.0, true, 0u, 0u, 0u, 0u},
    {RuleKind::always, "", "", 0.0, true, 0u, 0u, 0u, 0u},
};

constexpr Epithet kEpithets[] = {{"human_maritime", "Harbour-Mother"}, {"human_desert", "Well-Father"}, {"human_cold", "Long-Night Keeper"}, {"human_large_island", "Shore-Ancestor"}, {"human_rainforest", "Feathered Founder"}, {"human_heartland", "The Old King"}, {"elf", "The Grove-Singer"}, {"tidekin", "The Foam-Born"}, {"dwarf", "The Anvil-Father"}, {"gnome", "The Tinkering Spark"}, {"hill_dwarf", "The Full Table"}, {"frosthold_dwarf", "The Ember Under Ice"}};

// ORDER IS A CONTRACT. resolve_gods emits one record per row in this order,
// and choose_faiths builds its weighted draw by walking the resolved records
// in the same order -- so this table decides ties in every faith's draw.
// Reordering it is a different world on the same seed.
constexpr PantheonGod kGods[] = {
    {"god_weave", "The Weave", "school", {"The Loom-Mother", "The Laughing Unmaker"},
     {kWords + 0, 4}, {kRules + 0, 1}, {kWords + 4, 2}, {kWords + 6, 1}, {kWords + 7, 1}, {kWords + 8, 0},
     "reality unpicked itself around the city; nothing of it holds a shape", "", {kEpithets + 0, 0}, "", ""},
    {"god_umbral", "The Umbral", "school", {"The Quiet Judge", "The Hollow Hunger"},
     {kWords + 8, 4}, {kRules + 1, 1}, {kWords + 12, 1}, {kWords + 13, 2}, {kWords + 15, 1}, {kWords + 16, 2},
     "every hearth went cold in one night and the dead walked out of the gate", "", {kEpithets + 0, 0}, "", ""},
    {"god_infernal", "The Infernal", "school", {"The Chained Tyrant", "The Devourer Below"},
     {kWords + 18, 4}, {kRules + 2, 1}, {kWords + 22, 0}, {kWords + 22, 2}, {kWords + 24, 1}, {kWords + 25, 1},
     "the ground opened under the market and the city paid the price of its pacts", "", {kEpithets + 0, 0}, "", ""},
    {"god_radiant", "The Radiant", "school", {"The Dawn Healer", "The Burning Verdict"},
     {kWords + 26, 4}, {kRules + 3, 1}, {kWords + 30, 1}, {kWords + 31, 2}, {kWords + 33, 1}, {kWords + 34, 3},
     "a verdict of light burned the city to white ash", "", {kEpithets + 0, 0}, "", ""},
    {"god_fire", "The Fire", "school", {"The Hearth-Forger", "The Wildfire"},
     {kWords + 37, 4}, {kRules + 4, 1}, {kWords + 41, 1}, {kWords + 42, 1}, {kWords + 43, 1}, {kWords + 44, 1},
     "the city burned for nine days and the stones ran", "", {kEpithets + 0, 0}, "", ""},
    {"god_water", "The Water", "school", {"The Tide-Keeper", "The Winter Drowned"},
     {kWords + 45, 4}, {kRules + 5, 1}, {kWords + 49, 1}, {kWords + 50, 1}, {kWords + 51, 1}, {kWords + 52, 1},
     "the river turned in its bed and took the city with it", "", {kEpithets + 0, 0}, "", ""},
    {"god_earth", "The Earth", "school", {"The Green Grandmother", "The Titan Under the Hill"},
     {kWords + 53, 4}, {kRules + 6, 1}, {kWords + 57, 2}, {kWords + 59, 1}, {kWords + 60, 1}, {kWords + 61, 1},
     "the hill stood up and the city was under it", "", {kEpithets + 0, 0}, "", ""},
    {"god_air", "The Air", "school", {"The Wayfarer of Winds", "The Storm-Crown"},
     {kWords + 62, 4}, {kRules + 7, 1}, {kWords + 66, 2}, {kWords + 68, 1}, {kWords + 69, 1}, {kWords + 70, 0},
     "a storm with a crown of lightning scattered the city across the plain", "", {kEpithets + 0, 0}, "", ""},
    {"god_first_hearth", "The First Hearth", "ancestor", {"The First Hearth", ""},
     {kWords + 70, 3}, {kRules + 8, 1}, {kWords + 73, 1}, {kWords + 74, 0}, {kWords + 74, 1}, {kWords + 75, 0},
     "the founders' own walls closed on the city that forgot them", "human", {kEpithets + 0, 6}, "", ""},
    {"god_elder_star", "The Elder Star", "ancestor", {"The Elder Star", ""},
     {kWords + 75, 3}, {kRules + 9, 1}, {kWords + 78, 1}, {kWords + 79, 1}, {kWords + 80, 1}, {kWords + 81, 1},
     "the woods remembered the city and grew through it in a season", "elf", {kEpithets + 6, 2}, "", ""},
    {"god_deep_forge", "The Deep Forge", "ancestor", {"The Deep Forge", ""},
     {kWords + 82, 4}, {kRules + 10, 1}, {kWords + 86, 2}, {kWords + 88, 1}, {kWords + 89, 1}, {kWords + 90, 1},
     "the oath-breakers' halls were sealed in stone with everyone inside", "dwarf", {kEpithets + 8, 4}, "", ""},
    {"god_drowned_mother", "The Drowned Mother", "feature", {"The Drowned Mother", ""},
     {kWords + 91, 4}, {kRules + 11, 3}, {kWords + 95, 1}, {kWords + 96, 1}, {kWords + 97, 1}, {kWords + 98, 1},
     "the sea came up the streets and did not go back", "", {kEpithets + 12, 0}, "", ""},
    {"god_sky_court", "The Sky Court", "feature", {"The Sky Court", ""},
     {kWords + 99, 3}, {kRules + 14, 1}, {kWords + 102, 1}, {kWords + 103, 1}, {kWords + 104, 1}, {kWords + 105, 0},
     "the court sat in judgement above the city and let it fall from its favour", "", {kEpithets + 12, 0}, "", ""},
    {"god_great_wyrm", "The Great Wyrm", "feature", {"The Great Wyrm", ""},
     {kWords + 105, 4}, {kRules + 15, 1}, {kWords + 109, 1}, {kWords + 110, 1}, {kWords + 111, 1}, {kWords + 112, 2},
     "the Wyrm took the city as its hoard and left the walls as its teeth", "", {kEpithets + 12, 0}, "", ""},
    {"god_outer_unnamed", "The Unnamed Outside", "feature", {"The Unnamed Outside", ""},
     {kWords + 114, 3}, {kRules + 16, 1}, {kWords + 117, 0}, {kWords + 117, 2}, {kWords + 119, 2}, {kWords + 121, 3},
     "the city is still there, but its angles are wrong and nobody who enters comes back the same", "", {kEpithets + 12, 0}, "", ""},
    {"god_fallen_cities", "The Keeper of Fallen Cities", "feature", {"The Keeper of Fallen Cities", ""},
     {kWords + 124, 3}, {kRules + 17, 1}, {kWords + 127, 1}, {kWords + 128, 0}, {kWords + 128, 1}, {kWords + 129, 0},
     "the Keeper added one more name to the list", "", {kEpithets + 12, 0}, "", ""},
    {"god_wanderer", "The Wanderer", "feature", {"The Wanderer", ""},
     {kWords + 129, 3}, {kRules + 18, 1}, {kWords + 132, 2}, {kWords + 134, 0}, {kWords + 134, 1}, {kWords + 135, 0},
     "the roads left the city and its people followed them", "", {kEpithets + 12, 0}, "", ""},
    {"god_turning_moon", "The Turning Moon", "feature", {"The Pale Warden", "The Turning Face"},
     {kWords + 135, 4}, {kRules + 19, 1}, {kWords + 139, 1}, {kWords + 140, 0}, {kWords + 140, 1}, {kWords + 141, 0},
     "every school surged at once under a moon that filled half the sky, and the city could not stand it", "", {kEpithets + 12, 0}, "", "full moons; hollow nights are dread for every faith but the Wild Umbral and Infernal cults, who keep them as vigils"},
    {"god_hearth_harvest", "The Hearth and the Harvest", "civic", {"The Hearth and the Harvest", ""},
     {kWords + 141, 3}, {kRules + 20, 1}, {kWords + 144, 1}, {kWords + 145, 1}, {kWords + 146, 1}, {kWords + 147, 1},
     "the fields refused the city and the hearths would not light", "", {kEpithets + 12, 0}, "god_earth", ""},
    {"god_forge_craft", "The Maker", "civic", {"The Maker", ""},
     {kWords + 148, 3}, {kRules + 21, 1}, {kWords + 151, 2}, {kWords + 153, 0}, {kWords + 153, 1}, {kWords + 154, 0},
     "every wall the city ever raised came down in the same hour", "", {kEpithets + 12, 0}, "god_fire", ""},
    {"god_road_market", "The Coin and the Road", "civic", {"The Coin and the Road", ""},
     {kWords + 154, 3}, {kRules + 22, 1}, {kWords + 157, 2}, {kWords + 159, 0}, {kWords + 159, 1}, {kWords + 160, 0},
     "no road led to the city any more, and it starved in sight of its neighbours", "", {kEpithets + 12, 0}, "god_air", ""},
    {"god_crown_law", "The Crowned Scale", "civic", {"The Crowned Scale", ""},
     {kWords + 160, 3}, {kRules + 23, 1}, {kWords + 163, 1}, {kWords + 164, 2}, {kWords + 166, 1}, {kWords + 167, 1},
     "the Scale found the city wanting and the sentence was carried out at once", "", {kEpithets + 12, 0}, "god_radiant", ""},
    {"god_grave_memory", "The Grave Keeper", "civic", {"The Grave Keeper", ""},
     {kWords + 168, 3}, {kRules + 24, 1}, {kWords + 171, 2}, {kWords + 173, 0}, {kWords + 173, 1}, {kWords + 174, 1},
     "the ancestors rose to reclaim what the living had squandered", "", {kEpithets + 12, 0}, "god_umbral", ""},
    {"god_fortune_fate", "The Turning Wheel", "civic", {"The Turning Wheel", ""},
     {kWords + 175, 3}, {kRules + 25, 1}, {kWords + 178, 2}, {kWords + 180, 0}, {kWords + 180, 1}, {kWords + 181, 0},
     "the wheel turned once and the city's luck ran out all together", "", {kEpithets + 12, 0}, "god_weave", ""},
    {"god_red_field", "The Red Field", "civic", {"The Red Field", ""},
     {kWords + 181, 3}, {kRules + 26, 1}, {kWords + 184, 0}, {kWords + 184, 1}, {kWords + 185, 2}, {kWords + 187, 0},
     "the city fought a war with no enemy and lost it", "", {kEpithets + 12, 0}, "", ""},
};

// ORDER IS A CONTRACT. choose_cosmology walks this table in order, building
// `eligible`, and subtracts from ONE uniform draw along it.
constexpr Cosmology kCosmologies[] = {
    {"cos_elemental_court", "The Elemental Court", {kRules + 27, 1}, {"primordial_count", 0.0, 1.0}, {kWords + 187, 4}, "Fire, Water, Earth and Air are high; Weave and Umbral are titans before the court; Radiant and Infernal are outsiders."},
    {"cos_dual_dawn", "Dawn and Pit", {kRules + 28, 1}, {"", 2.0, 0.0}, {kWords + 191, 2}, "Two high gods; every other god is a saint of one or a devil of the other."},
    {"cos_ancestor_halls", "The Ancestor Halls", {kRules + 29, 1}, {"races", 0.0, 1.0}, {kWords + 193, 3}, "Parent-race gods are high; school gods are impersonal powers with no temples."},
    {"cos_spirits_of_place", "Spirits of Place", {kRules + 30, 3}, {"", 1.5, 0.0}, {kWords + 196, 0}, "No high gods; every leyline key point is a named local spirit; civic gods are household spirits."},
    {"cos_one_sun", "The One Sun", {kRules + 33, 3}, {"", 1.5, 0.0}, {kWords + 196, 2}, "One over-god (Radiant Sovereign, else the Crowned Scale); all others become saints or angels."},
    {"cos_silent_heavens", "The Silent Heavens", {kRules + 36, 3}, {"ruins", 0.5, 0.5}, {kWords + 198, 2}, "The gods are dead or withdrawn; only the Keeper of Fallen Cities, the Wanderer and cults of the Wild aspects are active."},
    {"cos_sea_and_sky", "Mother Sea, Father Sky", {kRules + 39, 3}, {"", 1.5, 0.0}, {kWords + 200, 3}, "The Drowned Mother and Air (or the Sky Court) are the high pair; others are their children."},
    {"cos_wyrm_thrones", "The Wyrm Thrones", {kRules + 42, 1}, {"dragons", 0.0, 1.0}, {kWords + 203, 1}, "The Great Wyrm is high; each dragon nest is a demigod with a territory."},
    {"cos_the_woven", "The Woven", {kRules + 43, 1}, {"", 1.5, 0.0}, {kWords + 204, 1}, "Magic itself is divine; the Loom-Mother is high and colleges are its churches."},
    {"cos_turning_moon", "The Turning Moon", {kRules + 44, 1}, {"lunar_influence", 0.0, 8.0}, {kWords + 205, 1}, "The moon is high; each school god is one of its eight faces and the civic gods are its months."},
    {"cos_mosaic", "The Mosaic", {kRules + 45, 1}, {"", 0.5, 0.0}, {kWords + 206, 0}, "No world-level high gods; each civilization ranks its own faith."},
};

constexpr AffinityTable kAffinities[] = {
    {"human_maritime", {kWords + 206, 2}, {kWords + 208, 2}, {kWords + 210, 1}},
    {"human_desert", {kWords + 211, 2}, {kWords + 213, 2}, {kWords + 215, 1}},
    {"human_cold", {kWords + 216, 2}, {kWords + 218, 2}, {kWords + 220, 1}},
    {"human_large_island", {kWords + 221, 2}, {kWords + 223, 2}, {kWords + 225, 1}},
    {"human_rainforest", {kWords + 226, 2}, {kWords + 228, 2}, {kWords + 230, 1}},
    {"human_heartland", {kWords + 231, 2}, {kWords + 233, 2}, {kWords + 235, 1}},
    {"dwarf", {kWords + 236, 3}, {kWords + 239, 1}, {kWords + 240, 2}},
    {"elf", {kWords + 242, 3}, {kWords + 245, 1}, {kWords + 246, 2}},
    {"gnome", {kWords + 248, 3}, {kWords + 251, 2}, {kWords + 253, 1}},
    {"tidekin", {kWords + 254, 2}, {kWords + 256, 2}, {kWords + 258, 1}},
    {"hill_dwarf", {kWords + 259, 2}, {kWords + 261, 2}, {kWords + 263, 1}},
    {"frosthold_dwarf", {kWords + 264, 3}, {kWords + 267, 1}, {kWords + 268, 1}},
};

// ORDER IS A CONTRACT: the last four entries of religion.gods, in this order.
constexpr HiddenGod kHiddenGods[] = {
    {"god_long_thirst", "The Long Thirst", "blood", {"The Patient Vein", "The Red Harvest"},
     {kWords + 269, 4}, {kRules + 46, 1}, "living", 3, 12, {kWords + 273, 3}, {kWords + 276, 2}, "the city stood and its people did not; what walked the walls afterward wore their faces"},
    {"god_glad_mother", "The Glad Mother", "rot", {"The Kind Decay", "The Teeming Bloom"},
     {kWords + 278, 4}, {kRules + 47, 1}, "decay", 6, 16, {kWords + 282, 2}, {kWords + 284, 1}, "the dead did not stay where they were put, and there were more of them each morning"},
    {"god_shape_beneath", "The Shape Beneath", "eldritch", {"The Quiet Geometry", "The Waking Angle"},
     {kWords + 285, 4}, {kRules + 48, 3}, "aberrant", 10, 22, {kWords + 289, 3}, {kWords + 292, 3}, "nothing was destroyed; the gates opened on time and the wrong people gave the orders"},
    {"god_unwriting", "The Unwriting", "void", {"The Long Silence", "The Open Absence"},
     {kWords + 295, 4}, {kRules + 51, 1}, "dead_magic", 15, 30, {kWords + 299, 3}, {kWords + 302, 4}, "the ground where it stood is on no map now, and the ley runs around the place it was"},
};

constexpr std::string_view kFoldFree[] = {"cos_mosaic", "cos_ancestor_halls"};
constexpr FearGod kFearGods[] = {{"aberrant", "god_outer_unnamed"}, {"draconic", "god_great_wyrm"}, {"infernal", "god_infernal"}, {"undead", "god_umbral"}};
constexpr WildPush kWildPush[] = {{"god_infernal", {kWords + 308, 1}}, {"god_umbral", {kWords + 309, 1}}};

}  // namespace

std::span<const PantheonGod> pantheon_gods() { return kGods; }
std::span<const HiddenGod> hidden_pantheon_gods() { return kHiddenGods; }
std::span<const Cosmology> pantheon_cosmologies() { return kCosmologies; }
std::span<const AffinityTable> pantheon_affinities() { return kAffinities; }
std::span<const std::string_view> fold_free_cosmologies() { return kFoldFree; }
std::span<const FearGod> fear_gods() { return kFearGods; }
std::span<const WildPush> wild_push_families() { return kWildPush; }
RulePools pantheon_rule_pools() { return {kRuleChildren, kRuleTexts}; }

PantheonTuning pantheon_tuning() {
    PantheonTuning t;
    t.aspect_midpoint = 1.2;
    t.nest_aspect_push = 0.15;
    t.affinity.strong = 2.0;
    t.affinity.mild = 1.0;
    t.affinity.aversion = -2.0;
    t.affinity.high = 1.5;
    t.affinity.civic_base = 0.5;
    return t;
}

// THE SCOPE OF THIS DIGEST IS THE POINT. It is taken over the known catalogue
// alone, so editing one of the four Outside gods does not retire every saved
// world -- which is exactly why the reference kept them in a second file.
PantheonIdentity pantheon_identity() {
    return {1, 1, "54f493246c7845196fe6116a3494f551209496a85f17853a85a580e8229ee30b", "34e5575f072f59b96d9f52941a329ae4958ec49136d2d5a3da3037df88b3c04e"};
}

PantheonIdentity hidden_pantheon_identity() {
    return {1, 1, "e1f22f118c8a4cc74a62c08a9dd1bc002cfa208da6c8a181a4206f02287abb84", "5e75330ad0cdd9ebf27dcfc52db7bd89acc880f0998a5e41459989dd820f6d5a"};
}

}  // namespace fwg
