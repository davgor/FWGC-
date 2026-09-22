# Port specification: 07-civ-registry-profiles

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/icarus_sim/civilization_registry.py`
- `Sim/icarus_sim/terrain_profiles.py`

**Estimated C++ size:** roughly 2000 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

The algorithm sections are excellent — I walked every one of `_validate_staffing`, `staffing_totals`, `_validate_settlement_blocks`, `_resolve`, `validate_registry`, `_expand_settlement_plan`, `registry_identity`, `building_pack_data`, `layout_profile_data`, `validate_habitat` and `validate_profile` against the source and the control flow, check order, error-wrapping boundaries, set-equality predicates, bool-rejection semantics and key-order consequences are all faithfully described. I confirmed the resolved key order and reproduced the sha256 `038cf89f…709e` exactly as the spec states. The 32-row trait table, all four biome maps, the placement-condition usage counts, the geometry_type/priority/access/housing_location histograms, the 106-structure count, the library block listing, the programme-size samples, the "29 distinct name overrides", the hamlet 12/1/18 figure and the guildhall claim are all correct. Where it fails is data. The spec says under DATA FILES that "the entire reference content is given under `reads` and `constants`", and that is not true: the per-entity `settlement.surface_habitat`/`world_habitat` rules (the only real users of the habitat DSL the spec spends a page defining), the building-pack `criteria` vocabulary and values, the layout-profile feature counts, the pack weights and layout bindings, and all presentation/economy/sky/binding data are absent — and the JSON is being deleted with the repo. Worse, three of the omissions are paired with confident false statements: `criteria` is described as "`{}` in every reference pack" when six of seven carry real selection criteria; the `NATURAL_BIOMES` id→core map is guessed with question marks and is wrong (6 is `snow`, not `boreal_forest`; 15 is `boreal_forest`; 0 and 8 are `ocean` and `lake` and go unmentioned) even though habitat rules address biomes by raw integer id; and the magic-school order is given as "weave, infernal, umbral, water, earth, air" when it is actually `weave, umbral, infernal, radiant, fire, water, earth, air` plus the four hidden — two schools missing entirely, in a list the spec itself calls a frozen positional contract. An implementer following this would silently give human_cold the wrong biomes, mis-resolve every `"core.school"` key, and build a pack selector with no criteria at all. Add the missing content and fix those three, plus the two inverted bool claims in EDGE CASES, and the document becomes implementable.

**6 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

These two modules are the authoring registry for every civilization in the world and the validated "population profile" view of it. `civilization_registry.py` loads, links, validates and caches two JSON documents — `civilizations.json` (who the peoples are, where they will live, what colours they draw in, and a measured building programme for each settlement size) and its sibling `buildings.json` (the neutral building/structure catalogue, building packs, layout profiles and housing profiles) — and exposes them as read-only deep copies plus two derived products: resolved settlement plans (`city_plan` / `hamlet_plan`) and a cryptographic `registry_identity()`. `terrain_profiles.py` is the narrow, heavily validated numeric view of the same data that the terrain pipeline actually scores ground with: 38 traits per civilization plus a `civilization` identity block, a small habitat rule DSL, and the two biome-lookup helpers (`biome_preference`, `biome_food_multiplier`) that decide how attractive and how productive a patch of ground is to a given people. Nothing in either module generates anything; they are the schema, the validator and the single source of truth that thirteen downstream generator stages read. The registry is deliberately fail-closed: a malformed registry is an exception at import-of-data time, never a silently smaller world.

## Where it sits in the pipeline

Nothing must exist before these run. Both are pure data loaders with no dependency on a `World`; the only import-time dependency is `terrain_profiles` -> `terrain_biome_catalogue` (which reads `biomes.json`) and `terrain_leyline_history` (for `HIDDEN_SCHOOLS`). `civilization_registry.validate_registry` imports `terrain_profiles.validate_profile`/`validate_habitat` lazily inside the function to break the import cycle (`terrain_profiles.profiles()` imports `civilization_registry.population_profiles` lazily for the same reason). In C++ both belong in the same translation unit or two units with no cycle.

Downstream consumers, all of which break if this subsystem is absent:
- `terrain_recipes.seed_config` (stage 0): reads the *default* profile to set `human_magic_limit` (= `mutation_limit`), `road_max_grade` (= `road_grade_limit`), `human_adaptation` (= `irrigation`), `urban_food_demand` (= `food_demand`), `stubbornness` (= `difficult_fraction`). `derive_population` uses `land_per_city_km2` and `support_multiplier` (dead on world recipe 3).
- `terrain_settlements.add_settlements` (stage 9): the big one. Uses every scoring weight, both habitat rules, `biome_preference`, `biome_food_multiplier`, `entity_rules(...)['settlement'|'economy']`, `section('defaults')`, `section('founding_rules')` (splatted as keyword arguments into `found_cities`), `building_pack_data()` and `layout_profile_data()`.
- `terrain_civilizations` (stage 9): `eligible_civilizations`, `classify_cities` (uses `city_classification.medium_suitability_min`), `civilization_report` (embeds `registry_identity()`, `parent_races`, presentation colours, and a `region_index` per entity).
- `terrain_magic` (colleges): `college_slope_limit`, `college_temperature_min/max`, `college_water_reach`, `college_flood_limit`, `college_suitability_min`, `mutation_limit`.
- `terrain_humans`, `terrain_seasons`, `terrain_society`, `terrain_wars`: `food_demand`, `water_reach`, `irrigation`.
- `terrain_religion`, `terrain_ruins`, `terrain_leyline_history`, `terrain_world`: `civilization_ids()`, `entity_rules`.
- `city_planner` (stage 16), `hamlet_planner`, `castle_planner`: `city_plan`/`hamlet_plan`/`section('structure_blocks')`; each embeds `registry_identity()` in its `planner_identity()` replay record.
- `heritage`: paired with `entity_rules(key)['parent_race_id']`.
- `npc_roster`: reads `city_plans`/`hamlet_plans` staffing rows; keeps a *snapshot copy* of `buildings.json` rosters in `npc_roster/policies/posts.json` and reports the world's registry revision/sha beside it so drift is visible.
- `fantasy_world_generator.asset_list`: `building_pack_data()`, `section('structure_blocks')`, `section('housing_profiles')`.
- `terrain_history.advance_age_request` **refuses to advance any saved world whose `world['civilizations']['registry'] != registry_identity()`**. This is the hard gate: any byte change in either JSON file invalidates every saved world.

## Inputs

**Files (the only inputs; no layers, no config, no world).**

1. `civilizations.json`, resolved as `Path(__file__).with_name('civilizations.json')` — a sibling of the module. Size in the reference tree: 495 KB, 12 civilizations.
2. `buildings.json`, resolved as `REGISTRY_PATH.with_name('buildings.json')`. 261 KB, 106 structures.

`terrain_profiles.py` additionally reads two compile-time tables from sibling modules:
- `NATURAL_BIOMES` from `terrain_biome_catalogue` (from `biomes.json`): a dict of `int id -> str core`. In the reference tree there are **13**, with non-contiguous ids `{0,1,2,3,4,5,6,7,8,13,15,16,17}` and cores `tundra, ?, desert, grassland, forest, exposed_rock, boreal_forest, rainforest, ?, marsh(13?), ..., cold_tundra, snow, land_ice` (id 17 is "Persistent land ice"). The exact core names that appear as variant-id prefixes in the data are: `tundra, desert, grassland, forest, exposed_rock, snow, rainforest, marsh, boreal_forest, cold_tundra, land_ice` (plus two more unused in magic maps).
- `biome_catalogue()` from the same module: every natural core crossed with every magic school, id `"<core>.<school>"`. 13 cores x 12 schools = **156 variant ids**. Schools: 8 known (`... weave, infernal, umbral, water, earth, air, ...`) then 4 hidden (`blood, void, rot, eldritch`), in that order, appended never inserted.
- `HIDDEN_SCHOOLS` from `terrain_leyline_history`: `{'blood','void','rot','eldritch'}`. Therefore `HIDDEN_VARIANT_IDS` has 13 x 4 = **52** members.

**`civilizations.json` top-level schema** (exact key order in the file, which matters — see ordering hazards):
```
schema                "fantasy-world-generator.civilization-registry"   (exact string)
schema_version        int, must equal 6
revision              int >= 1
description           free text, unvalidated
defaults              object
city_classification   object
configurations        object: id -> population profile (aggregates)
building_catalogue    object, must equal EXACTLY {"path":"buildings.json","schema_version":2}
civilization_order    list[str], a permutation of every nested civilization id
parent_races          object: race key -> {name, founding_participation_percent, civilizations{...}}
founding_rules        object
```
Note there is **no `entities` key in the file**; `entities` is synthesised by the loader by flattening `parent_races[*].civilizations`. A file that already contains `entities` is rejected.

`defaults` = `{profile_id, aggregate_profile_id, resource_score_factor_label, normal_site_reason, outpost_site_reason, outpost_wall_color_rgb, outpost_roof_color_rgb}`. Reference values: `profile_id="human_heartland"`, `aggregate_profile_id="mixed"`, `resource_score_factor_label="gnome_metal_affinity"`, `normal_site_reason="Combined freshwater, slope, climate and flood-risk score"`, `outpost_site_reason="Resource-driven difficult site"`, `outpost_wall_color_rgb=[232,199,125]`, `outpost_roof_color_rgb=[133,112,72]`. Only `profile_id` and `aggregate_profile_id` are validated (membership); the rest are free-form and consumed verbatim by `terrain_settlements` and `civilization_report`.

`city_classification` = `{medium_suitability_min: 0.65, capital_policy: "retain_founding_capital_else_highest_suitability_then_lowest_node", capital_scope: "Active surface cities, one per civilization; sky settlements are separate communities."}`. `capital_policy` must equal that exact string. `capital_scope` is unvalidated free text carried into the report.

`founding_rules` = `{years_per_round: 250, origin_min_separation_degrees: 90, diaspora_interval_years: 1000, diaspora_bonus_per_parent: 1}`. **These four key names are the parameter names of `found_cities(...)`** — `terrain_settlements` calls `found_cities(..., **section('founding_rules'))`. Adding a fifth key is a TypeError at call time, not a validation error.

`parent_races`: exactly 3 in the reference tree. `human` (name "Human", `founding_participation_percent` 95, 6 civilizations), `elf` ("Elf", 25, 2), `dwarf` ("Dwarf", 60, 4). Each race block also holds `civilizations: {id -> entity}`; the loader strips that and keeps only `{name, founding_participation_percent}`.

**A civilization entity** (post-flattening) has EXACTLY these 11 keys — the validator uses set equality, so any extra or missing key is fatal:
`parent_race_id` (injected by the loader), `population`, `settlement`, `economy`, `sky`, `presentation`, `buildings`, `small_city`, `medium_city`, `capital_city`, `hamlet`.

- `settlement` = `{surface_habitat, world_habitat, freshwater_reach_multiplier (float 0..100 or null), support_outside_habitat (bool), coastal_score_weight (0..2), resource_score_weight (0..2), reason (str or null)}`. `surface_habitat` and `world_habitat` are habitat rules (DSL below); `world_habitat` is used on world recipe 3, `surface_habitat` on the geometry lab recipe.
- `economy` = `{fishing_reach_multiplier (0.01..10), industrial_resource_min (0..1 or null), air_terminal_resource_min (0..1 or null), winter_fishing_fraction (0..1)}`.
- `sky` = `{score_bias (-100..100), moisture_score_weight (-100..100)}`.
- `presentation` = `{map_color_rgb, wall_color_rgb, roof_color_rgb (each a list of exactly 3 ints 0..255), outpost_colors (bool)}`.
- `buildings` = `{packs: {pack_id -> [option_id,...]}, structure_block_ids: [library key,...], housing_status: "deferred", layout_features: {layout_id -> [feature name,...]}}`. `housing_status` is **present in the data but never validated**.
- `small_city` / `medium_city` / `capital_city` / `hamlet` are settlement programme blocks (below).
- `population` is a population profile (38 traits + `civilization`), validated by `terrain_profiles.validate_profile`.

**A settlement programme block** = `{status: "preconfigured_design", housing_status: "deferred", selection_notes: str, buildings: [row,...], infrastructure: [row,...]}`. `status` and `housing_status` must be exactly those strings. `selection_notes` is unvalidated free text and is carried into the resolved plan.
- A `buildings` row = `{structure_id, name, count, placement_conditions[, staffing]}`.
- An `infrastructure` row = `{structure_id, quantity_mode: "fit_route_or_perimeter", placement_conditions[, staffing]}`. No `name`, no `count`.

**`buildings.json` top-level schema:**
```
schema            "fantasy-world-generator.building-registry"
schema_version    int, must equal 2
revision          int >= 1
description       free text
building_packs    {schema_version, description, fallback_pack_id, packs:[...]}
layout_profiles   {schema_version, fallback_profile_id, profiles:[...]}
structure_blocks  {library key -> library}
housing_profiles  {worker_house, worker_apartment, hamlet_house}
```
The four keys `building_packs, layout_profiles, structure_blocks, housing_profiles` are `BUILDING_SECTIONS` and **must all be present**. Every other top-level key of `buildings.json` (schema, schema_version, revision, description) is collected into the resolved document as `building_catalogue_metadata`.

A **structure library** (three of them: `common` 11 blocks / 80 structures, `rural` 1 block / 13 structures, `castle` 2 blocks / 13 structures; 106 in total) = `{schema, schema_version, catalogue_revision, status: "design_only", runtime_placement_enabled: false, scope, deferred_blocks: ["housing"], unit: "metres", priority_definitions{core,conditional,specialist}, scale_policy{counts,sharing,geometry,world_budget,housing,asset_links,state}, blocks:[{id,name,structures:[...]}], measurement_status: "provisional_design_defaults", measurement_convention{axes,footprint,plot,linear,access,terrain,variations,conversion}, library_id}`. Only `blocks[*].structures[*]` is validated; everything else is carried verbatim. `common` block ids in order: `water_sanitation(7), food(9), trade(8), crafts(9), civic(5), health(4), faith(5), defense(10), streets_transport(8), knowledge_magic(7), hinterland(8)`. `rural`: `hamlet_services(13)`, every id prefixed `building.hamlet_`. `castle`: `castle_fortification(7), castle_services(6)`.

A **structure** = `{id, name, status:"planned", priority, module_family, purpose, placement, requirements:[str], reuse_candidates:[str], geometry_type, dimensions_m{width,depth,height}, clearance_m{front,rear,left,right}, plot_m{width,depth}, access{mode, minimum_clear_width_m}, staffing}`. Its key field is `id`, matching `building\.[a-z][a-z0-9_]*`. Enumerations observed: `geometry_type` in `{building(48), compound(33), linear_segment(10), simple_building(8), open_area(7)}`; `priority` in `{core(45), conditional(45), specialist(16)}`; `access.mode` in `{cart(64), pedestrian(18), foot(13), service(11)}`; `module_family` is one of 21 values (`civic_hall, extraction_site, fortification, funerary_structure, hall, landmark, linear_fortification, linear_infrastructure, magical_structure, market, military_hall, mill, monument, open_space, public_hall, service_outbuilding, service_yard, storehouse, water_point, waterfront, workshop`) — **`housing` is deliberately absent**, and the validator rejects it if it appears.

`staffing` = `{basis: "distinct_people_all_shifts", housing_location: "city"|"hinterland", roles: [{role, minimum, target, maximum}], notes: non-empty str}`. 85 of 106 structures are `city`-housed, 21 `hinterland`.

A **building pack** = `{id, name, layout_profile_id (may be null), criteria (object; `{}` in every reference pack), weight (float), building_options: [...]}`. 7 packs: `city_pack_generic_surface(15 options), city_pack_human_rivergate(15), city_pack_human_highland(11), city_pack_dwarven_metalwork(11), city_pack_elf_woodland(13), city_pack_tideport(13), city_pack_gnome_foundry(12)`. `fallback_pack_id = "city_pack_generic_surface"`.
A **building option** = `{id, name, count (number or {base, per_100_residents?, min?, max?}), required? (bool), placement ("anchored"|"structural"|"river_gate"|"wall"), tags:[str], asset_choices:[{asset_id, weight}]}`.

A **layout profile** = `{id, name, features: {name -> number or {base, per_100_residents?, min?, max?, profiles?}}, placement: {river_buffer_m, river_fork_bridge_threshold, bridge_if_river_fork, max_anchor_slope_degrees}}`. Two profiles: `standard_city_layout` and `riverfront_city_layout`, both with the same 7 features `leader_homes, barracks, noble_homes, worker_housing, apartments, market_district, religious_building`. Reference `standard_city_layout` placement: `river_buffer_m 90, river_fork_bridge_threshold 2, bridge_if_river_fork true, max_anchor_slope_degrees 16`. `fallback_profile_id = "standard_city_layout"`.

`housing_profiles`: three entries keyed `worker_house`, `worker_apartment`, `hamlet_house`, each `{id: "building.<key>", name, dimensions_m{width,depth,height}, plot_m{width,depth}, worker_beds int, notes}`. Reference: worker_house 8x10x6 on a 12x16 plot, 4 beds; worker_apartment 10x12x12 on the **same** 12x16 plot, 16 beds; hamlet_house 5x6x4 on a 9x12 plot, 2 beds.

## Outputs

No layers, no world blocks. Everything is a return value. All of them are deep copies; callers may mutate freely (tested: `test_resolved_plan_is_independent_copy`).

- `load_registry() -> dict` — the whole resolved document.
- `section(name) -> Any` — one top-level key of the resolved document. KeyError propagates as-is (not wrapped).
- `entity_rules(entity_id) -> dict` — one entity's 11-key block.
- `default_profile_id() -> str` — `defaults.profile_id`.
- `population_profiles() -> dict[str, profile]` — `{entity_id: entity.population}` in `civilization_order`, then `configurations` merged on top. A configuration id equal to an entity id would shadow it, but that collision is rejected by validation.
- `registry_identity() -> {schema_version: int, revision: int, sha256: str(64 hex)}` — the saved-world gate. Reference value at revision 12: `038cf89fe4a5248daf47e6ea002880257d0cdaac4824ed30fa3ee49bb9c8709e`.
- `staffing_totals(staff, count) -> {workers, city_worker_beds, hinterland_worker_beds}`, each a `{minimum, target, maximum}` of non-negative ints.
- `city_plan(entity_id, city_block)` / `hamlet_plan(entity_id)` -> resolved settlement plan (below).
- `building_pack_data()` / `layout_profile_data()` -> the two libraries with per-entity binding lists inverted into them.

**Resolved settlement plan** (key order as produced): `status, housing_status, selection_notes, buildings, infrastructure, staffing_summary, civilization_id, unit ("metres"), runtime_placement_enabled (false), city_block` — where the last key is `city_block` for `city_plan` and `settlement_block` for `hamlet_plan`, and its value is the source block name (`small_city`/`medium_city`/`capital_city`/`hamlet`).

A resolved **buildings row** is the structure record with the programme row merged over it, so it carries every structure key plus `structure_id`, `count`, `placement_conditions`, `staffing_totals`. It has **both `id` and `structure_id`, holding the same `building.*` string**. `name` is the programme row's name where authored (29 culture-flavoured overrides exist: `building.civic_hall` becomes "Hold council hall", "Grove council hall", "Bazaar court", etc.), otherwise the structure's neutral name. `staffing` is the programme row's override if present, otherwise the structure's.

A resolved **infrastructure row** is the same merge but with `quantity_mode` instead of `count`, and **no `staffing_totals`** — infrastructure is never costed, because linear infrastructure uses shared service crews.

`staffing_summary` = `{unconditional, conditional, all_configured, total_residents: null, houses_required: null, basis: "One distinct resident worker per filled roster post; household members and housing occupancy not yet modeled."}`. Each of the first three is `{workers, city_worker_beds, hinterland_worker_beds}` x `{minimum, target, maximum}`. `total_residents` and `houses_required` are **deliberately null**: the model does not yet know household size or housing occupancy, and a number there would be a fabricated claim.

`building_pack_data()` mutates its copy: each `pack['criteria']['population_profiles']` becomes the sorted-by-`civilization_order` list of entity ids that bind that pack; each `option['profiles']` becomes the list of entity ids that bind that option; **options whose `profiles` came out empty are dropped from the pack entirely**.

`layout_profile_data()` mutates its copy: a scalar feature value is boxed into `{'base': value}` in place, and every feature gains `feature['profiles']`, the list of entity ids binding that feature in that layout.

Validated profile (`validate_profile` / `get_profile`) returns a new dict: the 38 trait fields, plus `id` (the profile key), plus `civilization` (the identity block, aliased not copied), plus `schema_version: 4`, plus `definition_hash` (sha256 hex of the dict as it stands just before the hash is inserted).

## Algorithm

## A. Loading and linking — `_document()`

1. `stat` the registry path. Read it through `_read(path, mtime_ns, size)`, an LRU-cached (maxsize 8) `json.loads(..., object_pairs_hook=_unique_object)`. `_unique_object` builds the dict pair by pair and raises `ValueError('Duplicate registry key: ' + key)` the first time a key repeats — JSON's last-wins is not acceptable here.
2. Assert `raw['building_catalogue'] == {'path': 'buildings.json', 'schema_version': 2}` **by exact dict equality**. This is the entire path-safety mechanism: `'../buildings.json'` and `'https://…/buildings.json'` both fail this equality (tested). The declared path is never used to resolve anything — the sibling is always `REGISTRY_PATH.with_name('buildings.json')`.
3. `stat` and read the sibling the same way, then call `_resolve(civ_path, civ_mtime, civ_size, bld_path, bld_mtime, bld_size)`, also LRU-cached at maxsize 8.
4. `_document` wraps `OSError, KeyError, TypeError, AttributeError` into `ValueError('Cannot load linked civilization/building registries: ' + str(exc))`. It does **not** wrap `ValueError`, so validation messages surface verbatim. `json.JSONDecodeError` is a `ValueError` subclass and also surfaces verbatim.

## B. `_resolve` — merge the two documents into one

1. Re-read the civ document (cache hit).
2. If any of `building_packs, layout_profiles, structure_blocks, housing_profiles, building_catalogue_metadata` appears in the civ document, raise `'Building definitions must live in the linked buildings.json'`.
3. Read the buildings document. Assert `schema == 'fantasy-world-generator.building-registry'`, `schema_version == 2` (`type(...) is int` checked separately so `True` is rejected), `revision` is an int `>= 1`, and all four `BUILDING_SECTIONS` present.
4. Collect every structure id across every library and block into a list. Reject if the list has duplicates, or if any id fails `building\.[a-z][a-z0-9_]*`. **This global uniqueness is the invariant that makes the flat `structure_id -> structure` map in the plan expander safe.**
5. Assert `raw['schema_version'] == 6` and `'entities' not in raw`. *(The error message says "Expected schema 5 with civilizations nested in parent race blocks" — stale, the check is 6. Carry the check, fix the message.)*
6. Deep-copy the civ document into `data`. Then flatten:
   - For each `(race, block)` in `data['parent_races'].items()` **in file order**: record `parents[race] = {'name': block['name'], 'founding_participation_percent': block['founding_participation_percent']}`, dropping `civilizations`.
   - For each `(key, entity)` in `block['civilizations'].items()`: if `key` is already in `entities`, or the entity already carries a `parent_race_id`, raise `'Duplicate civilization or redundant parent race'`. Otherwise `entities[key] = {**entity, 'parent_race_id': race}` — note the injected key lands **last** in the entity's key order.
   - Read `data['civilization_order']`. It must be a list, have no duplicates, and be set-equal to the flattened entity ids; otherwise `'Civilization order must name every nested entity exactly once'`.
   - `data['entities'] = {key: entities[key] for key in order}` — this is a **new** key, so it is appended after `founding_rules` in the document's key order.
   - `data['parent_races'] = parents` — an **existing** key, so it keeps its position.
   - Any `KeyError/TypeError/AttributeError` in this block becomes `ValueError('Malformed parent race blocks')`.
7. `data.update({key: deepcopy(buildings[key]) for key in BUILDING_SECTIONS})` — appends the four keys in the tuple order `building_packs, layout_profiles, structure_blocks, housing_profiles`.
8. `data['building_catalogue_metadata'] = {k: deepcopy(v) for k, v in buildings.items() if k not in BUILDING_SECTIONS}` — appended last, preserving the buildings document's own key order for those keys.
9. Return `validate_registry(data)` (which returns `data` unchanged on success).

Resulting key order of the resolved document, which `registry_identity()` hashes:
`schema, schema_version, revision, description, defaults, city_classification, configurations, building_catalogue, civilization_order, parent_races, founding_rules, entities, building_packs, layout_profiles, structure_blocks, housing_profiles, building_catalogue_metadata`.

## C. `validate_registry(data)` — ordered checks

Wrapped so that `KeyError, TypeError, AttributeError` become `ValueError('Malformed civilization registry: ' + str(exc))`. `IndexError` is **not** caught.

1. `isinstance(data, dict)`, `data.get('schema') == 'fantasy-world-generator.civilization-registry'`, `type(data.get('schema_version')) is int`, `schema_version == 6`. Else `'Unsupported civilization registry schema'`.
2. `type(revision) is int and revision >= 1`.
3. `_finite(data)`: recurse dicts (values) and lists; for a `float` leaf, `math.isfinite` or raise `'Registry numbers must be finite'`. Ints and strings pass; `Decimal` is not a concern. This is a whole-document pass before anything else.
4. `founding_rules`: `years_per_round` in `[1, 10000]`; `diaspora_interval_years` in `[1, 100000]`; `diaspora_interval_years % years_per_round == 0` else `'Diaspora interval must align with founding rounds'`; `type(diaspora_bonus_per_parent) is int and value in (0,1)`; `origin_min_separation_degrees` in `[0, 180]`.
5. `parent_races`: non-empty dict. For each: `founding_participation_percent` in `[0, 100]`; key matches `[a-z][a-z0-9_]*`; `name` a non-empty string.
6. `housing_profiles`: for each of `worker_house, worker_apartment, hamlet_house`: `id == 'building.' + key`; `type(worker_beds) is int and 1 <= worker_beds <= 32`; for `width` and `depth`: `dimensions_m[k]` in `[1, 100]` and `plot_m[k]` in `[dimensions_m[k], 200]`; `dimensions_m['height']` in `[1, 100]`. Then the densification invariant: `worker_apartment.plot_m == worker_house.plot_m` (dict equality) **and** `worker_apartment.worker_beds > worker_house.worker_beds`, else `'Apartment densification requires the same plot and higher capacity'`. *(Intent: the planner densifies by swapping a house for an apartment on the identical reserved plot; different plots would make the swap non-deterministic.)*
7. Every structure in every library: `_validate_staffing(structure['staffing'])`; and if `geometry_type == 'linear_segment'` then `staffing['roles']` must be empty, else `'Linear infrastructure uses shared service crews'`.
8. `entities` is a non-empty dict with at most **128** members. `set(entities) & set(configurations)` must be empty. Every key in `entities` and `configurations` matches `[a-z][a-z0-9_]*`.
9. `defaults['profile_id'] in entities` and `defaults['aggregate_profile_id'] in configurations`.
10. `city_classification`: `medium_suitability_min` in `[0, 1]`; `capital_policy == 'retain_founding_capital_else_highest_suitability_then_lowest_node'`.
11. Build `pack_index = {pack.id: {option.id for option in pack.building_options}}` and `layout_index = {profile.id: set(profile.features)}`. If `len(pack_index) != len(packs)` or `len(layout_index) != len(profiles)`, `'Duplicate library ID'`. `fallback_pack_id in pack_index`, `fallback_profile_id in layout_index`. For each pack: option ids unique within the pack; `layout_profile_id`, when not None, must be in `layout_index`.
12. For each `(key, entity)` in `entities.items()` **in insertion (civilization_order) order**:
    a. `set(entity)` equals exactly `{parent_race_id, population, settlement, economy, sky, presentation, buildings, small_city, medium_city, capital_city, hamlet}`.
    b. `entity['parent_race_id'] in parent_races`.
    c. `population = validate_profile(entity['population'], key)`; then `population['civilization']['kind'] == 'entity'`.
    d. `validate_habitat(settlement['surface_habitat'])` and `validate_habitat(settlement['world_habitat'])`.
    e. `freshwater_reach_multiplier` in `[0, 100]` or null; `support_outside_habitat` is a real `bool`; `coastal_score_weight` and `resource_score_weight` in `[0, 2]`; `reason` is null or a string (empty string allowed).
    f. `winter_fishing_fraction` in `[0, 1]`; `fishing_reach_multiplier` in `[0.01, 10]`; `industrial_resource_min` and `air_terminal_resource_min` in `[0, 1]` or null.
    g. `sky.score_bias` and `sky.moisture_score_weight` in `[-100, 100]`.
    h. Each of `map_color_rgb, wall_color_rgb, roof_color_rgb` is a list of exactly 3 ints in `[0, 255]` (bools rejected by `type(v) is not int`); `outpost_colors` is a real bool.
    i. For each `(pack, options)` in `buildings['packs']`: pack in `pack_index`; `options` a list with no duplicates and a subset of that pack's option ids.
    j. `fallback_pack_id in buildings['packs']` — `'Entity needs an explicit fallback pack binding'`. *(Intent: the layout engine falls back to the generic pack, so an entity that never bound it would fall back to something it does not own.)*
    k. For each `(layout, features)` in `buildings['layout_features']`: layout in `layout_index`; features a subset of that profile's feature names.
    l. `set(buildings['structure_block_ids']) <= set(structure_blocks)`, and `'rural' in structure_block_ids` — every entity must own the rural hamlet library.
    m. `population['civilization']['structure_blocks']`, if not null, must be in `structure_block_ids` — `'Structure block identity disagrees with bindings'`.
    n. `_validate_city_blocks(entity, structure_blocks)` then `_validate_hamlet_block(entity, structure_blocks)`.
13. For each `(key, profile)` in `configurations.items()`: `validate_profile(profile, key)['civilization']['kind'] == 'aggregate'`.
14. Return `data` (the same object, not a copy).

## D. `_validate_staffing(staff)`

1. `set(staff) == {'basis','housing_location','roles','notes'}` and `basis == 'distinct_people_all_shifts'`.
2. `housing_location in ('city','hinterland')`.
3. `notes` a non-empty string.
4. `roles` a list. For each role, with a `seen` set of role names:
   - `set(role) == {'role','minimum','target','maximum'}`, `role['role']` a non-empty string not already in `seen`.
   - `values = [minimum, target, maximum]`; each must satisfy `type(v) is int` (so `True`/`False` are rejected) and `0 <= v <= 10000`; `values == sorted(values)` (non-decreasing); `values[-1] != 0` (a role whose maximum is zero is not a role).

## E. `staffing_totals(staff, count)`

1. `_validate_staffing(staff)`; `type(count) is int and 0 <= count <= 10000`.
2. `workers[level] = count * sum(role[level] for role in staff['roles'])` for each of `minimum, target, maximum`.
3. `city_worker_beds[level] = workers[level] if housing_location == 'city' else 0`; `hinterland_worker_beds` is the mirror. A structure's workers are therefore counted once, into exactly one bed pool.

## F. `_validate_settlement_blocks(entity, libraries, tiers, label)`

1. Build `structures = {s.id: s}` over **every** library named in `entity['buildings']['structure_block_ids']`, every block, every structure. (Safe because ids are globally unique — see B.4.)
2. For each tier in `tiers` (`('small_city','medium_city','capital_city')` for cities, `('hamlet',)` for hamlets):
   - `plan['status'] == 'preconfigured_design'` and `plan['housing_status'] == 'deferred'`, else `'Unsupported <label> plan status'`.
   - For `group` in `('buildings', 'infrastructure')` **in that order**: `entries` must be a non-empty list. With a per-group `seen` set:
     * `sid = entry['structure_id']` must be in `structures` and not in `seen`. Note `seen` resets per group, so the **same structure may appear once in buildings and once in infrastructure** of the same tier.
     * If the entry carries `staffing`, validate it.
     * If `group == 'infrastructure'` and `entry.get('staffing', structure['staffing'])['roles']` is non-empty: `'Linear infrastructure uses shared service crews'`.
     * `structure['module_family'] != 'housing'` — `'Housing is deferred'`.
     * `entry['placement_conditions']` must be a list and a subset of `PLACEMENT_CONDITIONS`.
     * If `group == 'buildings'`: `structure['geometry_type'] != 'linear_segment'` (`'Linear structure needs route sizing'`); `type(count) is int and 1 <= count <= 10000`; `name` a string whose `.strip()` is non-empty.
     * Else (infrastructure): `structure['geometry_type'] == 'linear_segment'` **and** `entry['quantity_mode'] == 'fit_route_or_perimeter'`, else `'Invalid <label> infrastructure sizing'`.
     * Geometry: `size = structure['dimensions_m']`, `clear = structure['clearance_m']`, `plot = structure['plot_m']`. `size.width, size.depth, plot.width, plot.depth` each in `[0.01, 100000]`; `size.height` in `[0, 100000]`; each of `clear.left, clear.right, clear.front, clear.rear` in `[0, 100000]`. Then the plot identity: `plot.width == size.width + clear.left + clear.right` and `plot.depth == size.depth + clear.front + clear.rear`, else `'<Label> structure plot does not include its clearances'`. **This is an exact equality on floating-point values.**

Note that these geometry checks are only reached for structures a programme actually references. A structure nobody places is never geometry-checked.

## G. `_expand_settlement_plan(entity_id, block_name, block_key)`

1. `entity_id in doc['entities']` else `'Unknown civilization'`.
2. Build the same flat `structures` map from the entity's bound libraries.
3. `plan = deepcopy(entity[block_name])`.
4. For `group` in `('buildings','infrastructure')`: `plan[group] = [{**deepcopy(structures[row['structure_id']]), **row} for row in plan[group]]`. **The programme row wins on every shared key** — `name` and `staffing` in particular. Order of rows is the authored order and is preserved.
5. Initialise `summary` as three groups (`unconditional`, `conditional`, `all_configured`) x three metrics (`workers`, `city_worker_beds`, `hinterland_worker_beds`) x three levels, all zero.
6. For each row in `plan['buildings']` in order: `row['staffing_totals'] = staffing_totals(row['staffing'], row['count'])`; the group is `'conditional'` if `row['placement_conditions']` is non-empty else `'unconditional'`; add each metric/level into that group **and** into `all_configured`. Infrastructure is skipped entirely.
7. `summary.update(total_residents=None, houses_required=None, basis='One distinct resident worker per filled roster post; household members and housing occupancy not yet modeled.')`.
8. `plan['staffing_summary'] = summary`; `plan.update(civilization_id=entity_id, unit='metres', runtime_placement_enabled=False)`; `plan[block_key] = block_name`.

`city_plan(entity_id, city_block)` rejects a `city_block` outside `CITY_BLOCKS` with `'Unknown city size block'` and calls this with `block_key='city_block'`. `hamlet_plan(entity_id)` calls it with `block_name='hamlet'`, `block_key='settlement_block'`.

## H. `registry_identity()`

`canonical = json.dumps(doc, ensure_ascii=False, separators=(',', ':')).encode('utf-8')`; return `{'schema_version': doc['schema_version'], 'revision': doc['revision'], 'sha256': sha256(canonical).hexdigest()}`. No `sort_keys` — **the hash depends on the resolved document's key insertion order**, hence on the file's key order, the parent-race iteration order, `civilization_order`, and the order of `BUILDING_SECTIONS`.

## I. `building_pack_data()`

```
data = deepcopy(doc['building_packs'])
for pack in data['packs']:
    pack['criteria']['population_profiles'] = [k for k,e in doc['entities'].items() if pack['id'] in e['buildings']['packs']]
    for option in pack['building_options']:
        option['profiles'] = [k for k,e in doc['entities'].items()
                              if option['id'] in e['buildings']['packs'].get(pack['id'], [])]
    pack['building_options'] = [o for o in pack['building_options'] if o['profiles']]
```
The comment that justifies the filter, verbatim: *"Empty profiles mean unrestricted in the legacy layout engine. Remove truly unreachable options instead of accidentally enabling them."* Both list comprehensions iterate `doc['entities']`, i.e. `civilization_order`, so the profile lists are in `civilization_order`, not alphabetical.

## J. `layout_profile_data()`

```
data = deepcopy(doc['layout_profiles'])
for layout in data['profiles']:
    for name, feature in layout['features'].items():
        if not isinstance(feature, dict):
            feature = {'base': feature}; layout['features'][name] = feature
        feature['profiles'] = [k for k,e in doc['entities'].items()
                               if name in e['buildings']['layout_features'].get(layout['id'], [])]
```
**The consumer treats an empty `profiles` list here as "no profile fits", the exact opposite of the pack-option case** (`terrain_settlements._coerce_feature_count` does `set(profiles) if profiles is not None else None`, and `_profile_fits_feature` returns `feature['profiles'] is None or profile in feature['profiles']`). That asymmetry is pinned by a test named `test_empty_feature_bindings_disable_feature`. Preserve it or fix it deliberately; do not "harmonise" it by accident.

## K. `terrain_profiles.validate_habitat(rule, depth=0)` — the habitat DSL

```
if not isinstance(rule, dict) or depth > 8: error
if not rule: return                      # {} is the universal rule, always true
for operator in ('all','any'):           # 'all' checked before 'any'
    if operator in rule:
        set(rule) must == {operator}; rule[operator] a non-empty list
        recurse each child at depth+1; return
rule['field'] must be one of the 14 allowed fields
set(rule) - {'field','in','min','max','gt','equals'} must be empty, and len(rule) >= 2
for k in ('min','max','gt'): if present, type in (int,float) and finite
if 'min' and 'max' both present: min <= max
if 'in' present: a non-empty list whose members are strings, or finite int/float/bool
if 'equals' present: finite int/float/bool (NOT a string)
```
Allowed fields, in source order: `biome, variant, temperature, moisture, maritime, landmass_area_m2, landmass_fraction, largest_landmass, resource, slope, height, tpi, coastal_support, abs_latitude`.

The **evaluator** lives in `terrain_civilizations.matches(rule, environment)` and is the other half of the contract:
```
if not rule: return True
if 'all' in rule: return all(matches(r, env) for r in rule['all'])
if 'any' in rule: return any(matches(r, env) for r in rule['any'])
value = env[rule['field']]              # KeyError if the caller did not supply it
return (('in' not in rule or value in rule['in'])
    and ('min' not in rule or value >= rule['min'])
    and ('max' not in rule or value <= rule['max'])
    and ('gt'  not in rule or value >  rule['gt'])
    and ('equals' not in rule or value == rule['equals']))
```
All comparisons in one rule are ANDed. `largest_landmass` is a bool compared with `equals: false`; in Python `False == 0` is true, which is how `{"field":"largest_landmass","equals":false}` works against a bool. Units: `temperature` degrees C, `moisture` 0..1, `maritime` 0..1, `resource` 0..1, `slope` degrees, `height` metres above sea level, `tpi` metres, `coastal_support` 0..1, `abs_latitude` degrees 0..90, `landmass_area_m2` square metres, `landmass_fraction` 0..1, `biome` an int natural-biome id, `variant` a `"core.school"` string.

## L. `terrain_profiles.validate_profile(raw, profile_id)`

1. `set(raw) == FIELDS | {'civilization'}` — exactly 39 keys, set equality. The 38 `FIELDS` are listed under constants. Any extra or missing trait is fatal: `'Civilization requires a complete independent trait record'`. **There is no inheritance: every civilization restates every trait.**
2. `identity = raw['civilization']`: a dict whose `kind` is `'entity'` or `'aggregate'`; `set(identity) == {'kind','inspiration','environment','habitat','allocation_group','priority','structure_blocks'}`; `type(priority) is int and priority >= 0`; each of `inspiration, allocation_group, structure_blocks` is either null or a non-empty string; `environment` is a non-empty string; `validate_habitat(identity['habitat'])`.
3. `result = {k: v for k, v in raw.items() if k != 'civilization'}`; `result['id'] = profile_id`.
4. Per-key type pass over `result` (which at this point holds the 38 traits plus `id`):
   - `name`, `description`, `id`: non-empty strings.
   - The four biome maps: each a dict. For each `(biome, value)`:
     * If `'magic_biome' in k` (true for `magic_biome_preferences` and `food_magic_biome_multipliers`): the key must be a string in `VARIANT_IDS`.
     * Otherwise (`biome_preferences`, `food_biome_multipliers`): the key must be a string that `.isdigit()` and whose `int()` is in `NATURAL_BIOMES`. **Keys are decimal strings, not ints** — `"13"`, not `13`. `.isdigit()` means no sign and no whitespace; note it is true for some Unicode digit characters, which `int()` also accepts.
     * The value must be a finite int or float.
     * Range: if `k.endswith('preferences')` then `-1 <= value <= 1`; else (`*multipliers`) `0 <= value <= 2`.
   - Every other key: a finite int or float.
5. Positivity: `temperature_tolerance, slope_comfort, site_slope_limit, work_slope_limit, water_reach, food_temperature_tolerance, food_slope_comfort, land_per_city_km2, support_multiplier, college_water_reach` must each be `> 0`.
6. Unit interval: `mutation_limit, difficult_fraction, irrigation, moisture_ideal, food_moisture_ideal, college_flood_limit, college_suitability_min` must each be in `[0, 1]`.
7. `food_moisture_ideal != 0` and `0.01 <= road_grade_limit <= 1`, else `'Invalid profile reach or moisture'`.
8. `college_temperature_min < college_temperature_max`.
9. Unit interval again: `water_weight, slope_weight, climate_weight, moisture_weight, resource_weight, flood_penalty, magic_penalty` each in `[0, 1]`. **They are not required to sum to 1**; in the reference data the five positive weights sum to exactly 1.0 for every civilization, but nothing enforces it and `tidekin` relies on the slack (its five sum to 0.8, leaving room for a 0.35 coastal weight).
10. Angles: `site_slope_limit, work_slope_limit, college_slope_limit, slope_comfort, food_slope_comfort` each strictly inside `(0, 90)` degrees.
11. `0 <= food_demand <= 10000`. `0.01 <= land_per_city_km2 <= 1000`. `0 < support_multiplier <= 10`.
12. `result['civilization'] = identity` (aliased, not copied).
13. `type(minimum_founding_residents) is int and 1 <= it <= 10000`.
14. `-100 <= food_temperature_ideal <= 100`.
15. `result['schema_version'] = 4`.
16. `result['definition_hash'] = sha256(json.dumps(result, sort_keys=True).encode()).hexdigest()` — covers the 38 traits, `id`, `civilization` and `schema_version`, and uses **sorted keys with Python's default separators `', '` / `': '` and `ensure_ascii=True`**, which is a *different* canonicalisation from `registry_identity()`'s compact, unsorted, `ensure_ascii=False` one. Two canonical forms in one subsystem; both matter, neither is the other.
17. Return `result`.

`get_profile(profile_id)` looks the id up in `profiles()` (raising `'Unknown population profile'` if absent or not a string) and re-runs the whole validation on every call. There is no caching; a stage that scores a grid must hoist the call out of its loop.

## M. `biome_preference(profile, core_id, variant_id=None)`

```
if variant_id in HIDDEN_VARIANT_IDS:
    return profile['magic_biome_preferences'].get(variant_id, HIDDEN_PREFERENCE)   # -1.0
return profile['magic_biome_preferences'].get(variant_id,
       profile['biome_preferences'].get(str(core_id), 0))
```
The docstring is the reasoning and must be carried: *"An exact magical state overrides its natural-core preference. Ground held by a hidden school is uninhabitable to everyone, and that is a rule rather than 52 authored opinions per people: nobody has a cultural preference about a school they have never heard of. Without it the fallback below would read a rotted grassland as plain grassland, and settlement placement would find corrupted ground exactly as attractive as clean ground."*

When `variant_id` is `None` (the geometry-lab path, where no variant layer exists), `None` is not in `HIDDEN_VARIANT_IDS` and `magic_biome_preferences.get(None, ...)` always misses (keys are strings), so the natural-core lookup is what runs. Default 0 for an unmentioned core.

## N. `biome_food_multiplier(profile, core_id, variant_id=None)`

Same shape, different defaults:
```
if variant_id in HIDDEN_VARIANT_IDS:
    return profile['food_magic_biome_multipliers'].get(variant_id, HIDDEN_FOOD)    # 0.0
return profile['food_magic_biome_multipliers'].get(variant_id,
       profile['food_biome_multipliers'].get(str(core_id), 1))
```
Docstring, to carry: *"Exact state food factor, before independent school-specific hazard losses. Corrupted ground feeds nobody; which of the four took it is the corruption record's business, not the terrain table's."* Default 1 (neutral) for an unmentioned core.

## O. `profile_options`, `civilization_ids`

`profile_options() -> [{'id': key, 'name': value['name']} for key, value in profiles().items()]` — in `profiles()` order (entities in `civilization_order`, then configurations).
`civilization_ids() -> tuple(key for key, value in profiles().items() if value['civilization']['kind'] == 'entity')` — the ordered entity ids, and the source of `region_index` in the civilization report and of the `suitability_<id>` / `magic_risk_<id>` layer names.

## Constants

**Module-level constants, `civilization_registry.py`:**
- `REGISTRY_PATH` = sibling `civilizations.json`. Changing it changes which world you generate; tests monkeypatch it.
- `BUILDING_SECTIONS = ('building_packs','layout_profiles','structure_blocks','housing_profiles')`. The tuple **order** feeds `dict.update`, hence the hashed key order. Reordering it changes `registry_identity().sha256` and invalidates every saved world.
- `CITY_BLOCKS = ('small_city','medium_city','capital_city')`. Validation order and the accepted values of `city_plan`'s second argument.
- `HAMLET_BLOCK = 'hamlet'`.
- `PLACEMENT_CONDITIONS` — 18 strings: `usable_groundwater, water_collection_or_delivery, navigable_shore, flowing_water, reliable_wind, ore_and_fuel_supply, food_surplus, working_animals, clay_and_fuel_supply, medicinal_supply, clear_sky_view, isolation_access, route_crossing, grade_change, retention_needed, defended_perimeter, farming_support, resource_support`. Reference usage counts across all 12 civs x 4 blocks: defended_perimeter 60, navigable_shore 41, usable_groundwater 36, route_crossing 36, medicinal_supply 26, working_animals 25, farming_support 24, food_surplus 15, grade_change 15, retention_needed 15, isolation_access 12, resource_support 12, clay_and_fuel_supply 11, water_collection_or_delivery 9, clear_sky_view 7, ore_and_fuel_supply 6, flowing_water 4, **reliable_wind 0** (declared, never used — keep it; a wind-driven mill programme is clearly intended).
- `STAFF_LEVELS = ('minimum','target','maximum')`. Order defines the sorted-ness check `values == sorted(values)`; reordering it silently changes what "non-decreasing" means.

**Numeric bounds (all inclusive unless stated).** Changing any of these only widens or narrows what data is accepted; none is a physical constant.
| Check | Range | Effect of widening |
|---|---|---|
| entity count | 1..128 | 128 is a soft sanity ceiling; nothing downstream depends on it |
| `revision` | >= 1 | 0 means "unversioned", rejected so an identity can never be ambiguous |
| `years_per_round` | 1..10000 | founding round length in years; 250 in data |
| `diaspora_interval_years` | 1..100000, multiple of `years_per_round` | 1000 in data = one diaspora every 4 rounds |
| `diaspora_bonus_per_parent` | exactly 0 or 1 | 1 = at most one bonus city per parent race, ever |
| `origin_min_separation_degrees` | 0..180 | 90 in data; converted to `world_reach * deg/180` metres in `found_cities` |
| `founding_participation_percent` | 0..100 | human 95, dwarf 60, elf 25 |
| `worker_beds` | int 1..32 | 4 / 16 / 2 in data |
| housing `dimensions_m` w,d | 1..100 m | |
| housing `plot_m` w,d | dimension..200 m | plot can never be smaller than the building |
| housing height | 1..100 m | |
| `medium_suitability_min` | 0..1 | 0.65; raising it makes more cities "small" |
| `coastal_score_weight`, `resource_score_weight` | 0..2 | 0.15 typical, 0.35 tidekin, 0.25 frosthold |
| `freshwater_reach_multiplier` | 0..100 or null | null = no hard freshwater gate; 2 = must be within 2x `water_reach` |
| `winter_fishing_fraction` | 0..1 | 0 for most, 0.25 human_cold, 0.15 frosthold |
| `fishing_reach_multiplier` | 0.01..10 | 1.0 default, 1.25 cold/frosthold, 1.5 tidekin |
| `industrial_resource_min`, `air_terminal_resource_min` | 0..1 or null | 0.5/0.5 for gnome only |
| `sky.score_bias`, `sky.moisture_score_weight` | -100..100 | gnome +4 bias, tidekin -10 bias, elf +3 moisture weight |
| RGB components | int 0..255 | |
| building `count` | int 1..10000 | |
| `staffing_totals` count | int 0..10000 | 0 allowed here but not in a programme row |
| role level | int 0..10000, non-decreasing, max > 0 | |
| structure w,d and plot w,d | 0.01..100000 m | |
| structure height | 0..100000 m | 0 means "no enclosing building height", explicitly *not* "nothing there" |
| clearances | 0..100000 m | |

**`terrain_profiles.py` constants:**
- `FIELDS` — the 38 trait names, exactly: `name, description, food_temperature_ideal, minimum_founding_residents, temperature_ideal, temperature_tolerance, slope_comfort, site_slope_limit, work_slope_limit, road_grade_limit, water_reach, moisture_ideal, water_weight, slope_weight, climate_weight, moisture_weight, resource_weight, flood_penalty, magic_penalty, mutation_limit, difficult_fraction, food_temperature_tolerance, food_slope_comfort, food_moisture_ideal, irrigation, food_demand, land_per_city_km2, support_multiplier, college_slope_limit, college_temperature_min, college_temperature_max, college_water_reach, college_flood_limit, college_suitability_min, biome_preferences, food_biome_multipliers, magic_biome_preferences, food_magic_biome_multipliers`. It is a `set`, used only for set equality — its own iteration order never reaches output.
- `HIDDEN_PREFERENCE = -1.0` — the preference score of ground held by a hidden magic school. -1 is the bottom of the legal preference range, so it drags a site score to zero after clamping.
- `HIDDEN_FOOD = 0.0` — the food multiplier of that ground. Zero, not small: corrupted ground feeds nobody.
- `VARIANT_IDS` — 156 `"core.school"` strings.
- `HIDDEN_VARIANT_IDS` — the 52 of those whose school is hidden.
- Profile `schema_version = 4`, hard-coded into every validated profile.

**Trait meanings and units** (all 38, since nothing else will say):
- `name`, `description`: display strings.
- `temperature_ideal` (C), `temperature_tolerance` (C, the sigma of a Gaussian): site climate score is `exp(-((T - ideal)/tolerance)^2)`.
- `slope_comfort` (degrees, Gaussian sigma): slope score is `exp(-(slope/comfort)^2)`.
- `site_slope_limit` (degrees, 0..90 exclusive): hard cut — no city on steeper ground.
- `work_slope_limit` (degrees): hard cut on farmable/productive ground.
- `road_grade_limit` (0.01..1, rise/run): becomes `cfg.road_max_grade`.
- `water_reach` (**metres, absolute**): freshwater decay length, `exp(-distance/reach)`. Also the base of the hard freshwater gate when `freshwater_reach_multiplier` is set.
- `moisture_ideal` (0..1): moisture score is `max(0, 1 - |moisture - ideal|)`.
- `water_weight, slope_weight, climate_weight, moisture_weight, resource_weight` (0..1): the five additive score terms.
- `flood_penalty, magic_penalty` (0..1): subtractive terms, multiplied by flood risk and magic hazard.
- `mutation_limit` (0..1): maximum magic hazard a site or worker tolerates; becomes `cfg.human_magic_limit` for the default profile.
- `difficult_fraction` (0..1): becomes `cfg.stubbornness` — the share of sites allowed to be "difficult" outposts.
- `food_temperature_ideal` (-100..100 C), `food_temperature_tolerance`, `food_slope_comfort` (degrees), `food_moisture_ideal` (0..1, and must not be exactly 0): the farming-potential analogues.
- `irrigation` (0..1): becomes `cfg.human_adaptation`; how much the people can compensate for dry ground.
- `food_demand` (0..10000): food units per 100 residents per year; becomes `cfg.urban_food_demand`.
- `land_per_city_km2` (0.01..1000): habitable km^2 required per city. Divides weighted habitat area to get a city quota.
- `support_multiplier` (0..10 exclusive of 0): multiplies derived support reach. 1.0 for every civilization in the data — the knob exists and is unused.
- `minimum_founding_residents` (int 1..10000): 30 for human_cold and frosthold_dwarf, 40 for everyone else. Divides the population allowance into a city count.
- `college_slope_limit` (degrees), `college_temperature_min` < `college_temperature_max` (C), `college_water_reach` (metres), `college_flood_limit` (0..1), `college_suitability_min` (0..1): the magic-college siting gate.
- The four biome maps, described above.

**The full reference trait table** (12 entities, in `civilization_order`; `mixed` aggregate matches `human_heartland` except `food_moisture_ideal` 0.65 and `food_temperature_ideal` 18):
```
field                    maritime desert cold  island rainfor heart  dwarf elf   gnome tidekin hill_d frost_d
temperature_ideal          20      28     4     18     25     18      9    18    18    18     18     -8
temperature_tolerance      24      24    24     26     20     26     28    26    26    30     26     20
slope_comfort              16      18    20     20     18     16     22    16    22    16      8     22
site_slope_limit           35      38    40     40     38     35     45    35    40    35     18     45
work_slope_limit           20      22    25     25     22     20     30    20    20    20     16     30
road_grade_limit          0.35    0.40  0.45   0.45   0.40   0.35   0.55  0.35  0.35  0.35   0.35   0.55
water_reach               500     650   500    500    450    400    600   400   400   600    400    600
moisture_ideal            0.65    0.30  0.55   0.70   0.85   0.60   0.60  0.75  0.60  0.75   0.60   0.60
water_weight              0.30    0.30  0.30   0.30   0.30   0.30   0.25  0.30  0.30  0.20   0.30   0.25
slope_weight              0.25    0.25  0.25   0.25   0.25   0.25   0.15  0.25  0.25  0.15   0.25   0.15
climate_weight            0.20    0.20  0.20   0.20   0.20   0.20   0.20  0.20  0.20  0.20   0.20   0.20
moisture_weight           0.15    0.15  0.15   0.15   0.15   0.15   0.15  0.15  0.15  0.15   0.15   0.15
resource_weight           0.10    0.10  0.10   0.10   0.10   0.10   0.25  0.10  0.30  0.10   0.05   0.25
flood_penalty             0.25 (all)
magic_penalty             0.20 (all except elf 0.10)
mutation_limit            0.45    0.45  0.45   0.45   0.45   0.45   0.45  0.60  0.55  0.60   0.45   0.45
difficult_fraction        0.20 (all)
food_temperature_ideal     20      28     4     18     25     18      9    18    18    18     18      9
food_temperature_tolerance 26      26    26     28     22     28     30    28    28    28     28     30
food_slope_comfort         18      20    22     22     20     18     24    18    18    18     10     24
food_moisture_ideal       0.65    0.40  0.55   0.70   0.85   0.60   0.55  0.75  0.65  0.65   0.60   0.55
irrigation                0.70    0.90  0.55   0.75   0.50   0.70   0.70  0.40  0.70  0.70   0.70   0.70
food_demand                10      10    10     10     10     10     10     8     8     8      8     10
land_per_city_km2         1.60    1.80  0.90   1.60   1.60   1.60   1.00  1.20  0.35  0.25   0.70   0.55
support_multiplier        1 (all)
minimum_founding_residents 40      40    30     40     40     40     40    40    40    40     40     30
college_slope_limit        15      15    15     15     15     15     22    15    15    15     15     22
college_temperature_min    -3       5   -19     -5      2     -5    -15    -5    -5    -5     -5    -15
college_temperature_max    40      48    24     38     45     38     38    38    38    38     38     38
college_water_reach       700     850   700    700    650    600    600   600   600   600    600    600
college_flood_limit       0.50 (all)
college_suitability_min   0.55 (all)
```
Habitats (`civilization.habitat`, the eligibility gate; `{}` = always eligible): maritime `{field:maritime, min:0.3}`; desert `{field:biome, in:[2]}`; cold `{any:[{biome in [1,6,15,16]},{temperature max 5}]}`; large_island `{all:[{landmass_area_m2 min 1e6},{landmass_fraction max 0.15},{largest_landmass equals false}]}`; rainforest `{all:[{biome in [7]},{moisture min 0.7}]}`; heartland `{}`; dwarf/elf/gnome/tidekin/frosthold `{}`; hill_dwarf `{field:biome, in:[3]}`; frosthold's own `civilization.habitat` is `{field:temperature, max:5}`.
`allocation_group`: `"human_geography"` for all six human civilizations, `null` for the other six. `priority`: heartland 5, maritime 4, large_island 3, rainforest 2, desert 1, cold 0, everyone else 0.
`civilization.structure_blocks`: `"common"` for the six humans and hill_dwarf, `null` for dwarf, elf, gnome, tidekin, frosthold_dwarf.
`inspiration`: Mediterranean, Persian, Nordic, Eastern, Aztec, European, null, null, null, null, "Pastoral smallfolk", null.

Non-trivial biome maps: `dwarf`/`frosthold_dwarf` `biome_preferences {"5":0.12,"6":0.05}`, `magic_biome_preferences {"desert.weave":0.08,"desert.infernal":0.08,"desert.water":0.08}`; `elf` `{"4":0.12,"7":0.12,"2":-0.12}` and six forest magic preferences at 0.16/0.18; `gnome` `{"15":0.05,"17":-0.5}`; `tidekin` `{"13":0.1,"15":0.05,"17":-0.5}`; `hill_dwarf` `{"3":0.2,"4":0.02,"5":-0.15}`; the six humans all `{}`. `food_biome_multipliers` is `{"13":0.35}` for everyone except elf `{"13":0.6}` and hill_dwarf `{"13":0.35,"3":1.15}`. `food_magic_biome_multipliers` is the same 17-entry table for everyone (11 `*.infernal` at 0.05 except `desert.infernal` 0.15; three `*.earth` forest entries at 0.5, which elf raises to 1; `desert.weave` and `desert.water` 0.15; `marsh.umbral` 0.35, which elf raises to 0.5). **No profile authors an opinion about any of the 52 hidden-school variants** — the `-1.0`/`0.0` rule is what covers them, which is exactly what the docstring says.

**Reference settlement-programme sizes** (building rows / summed counts / infrastructure rows / target workers), so a port can sanity-check its own data: human_maritime small 30/31/5/92, medium 45/62/5/205, capital 50/87/5/305; human_heartland small 28/29/5/86, medium 41/58/5/188, capital 45/82/5/285; dwarf small 28/29/7/84, medium 40/57/7/185, capital 46/83/7/287; frosthold_dwarf small 29/29/7/85, medium 41/42/7/136, capital 47/48/7/169. Every hamlet is 12 buildings / 1 infrastructure / 18 target workers. Every city of every entity has exactly one `building.guildhall` with `count` 1, `priority` "core", no placement conditions, roles `{cook, bartender}`, and `city_worker_beds` target 2. Totals are strictly increasing small < medium < capital for every entity.

## Data files

**`Sim/icarus_sim/civilizations.json`** — 495 KB, schema `fantasy-world-generator.civilization-registry`, `schema_version` 6, `revision` 12. Full schema and the entire reference content are given under `reads` and `constants`. Entry selection: entities are selected by id, and the id namespace is flat across parent races; a caller never names a race.

**`Sim/icarus_sim/buildings.json`** — 261 KB, schema `fantasy-world-generator.building-registry`, `schema_version` 2, `revision` 5. Linked from the first by the exact literal `{"path":"buildings.json","schema_version":2}`; the path string is validated but never dereferenced, so the link cannot escape the sibling.

**`Sim/icarus_sim/biomes.json`** — read indirectly through `terrain_biome_catalogue`; supplies the 13 natural biome ids/cores and the per-school variant names. Its list **position is a contract**: `biome_variant` stores a flat index into `biome_catalogue()`, so the 8 known schools keep positions 0..7 and the 4 hidden ones take the tail. Reordering it renumbers every variant in every saved world.

**`Sim/npc_roster/policies/posts.json`** — not read by these modules, but it is a *snapshot copy* of `buildings.json`'s staffing rosters, kept because `npc_roster` must not import the generator. A test re-reads the registry and fails on any difference. A C++ port that changes the building rosters must regenerate that snapshot too.

A representative structure entry, complete:
```json
{"id":"building.well","name":"Public well","status":"planned","priority":"core",
 "module_family":"water_point","purpose":"Supply drinking and household water.",
 "placement":"Accessible neighborhood space, clear of waste.",
 "requirements":["Verified usable groundwater; sanitary separation."],
 "reuse_candidates":["production.prop-019"],
 "geometry_type":"building",
 "dimensions_m":{"width":3,"depth":3,"height":3},
 "clearance_m":{"front":3,"rear":3,"left":2,"right":2},
 "plot_m":{"width":7,"depth":9},
 "access":{"mode":"pedestrian","minimum_clear_width_m":1.5},
 "staffing":{"basis":"distinct_people_all_shifts","housing_location":"city",
   "roles":[{"role":"water_keeper","minimum":1,"target":1,"maximum":2}],
   "notes":"Provisional operating roster per instance, including working owners and apprentices where listed. Distinct people across all shifts; excludes visitors, customers, patients and dependents. Each post assumes a different worker unless a future assignment model shares jobs."}}
```
(`plot_m.width` 7 = 3 + 2 + 2; `plot_m.depth` 9 = 3 + 3 + 3 — the identity the validator enforces.)

A representative programme row pair:
```json
{"structure_id":"building.dock","name":"Dock","count":1,"placement_conditions":["navigable_shore"]}
{"structure_id":"building.street","quantity_mode":"fit_route_or_perimeter","placement_conditions":[]}
```
The nine structure ids that ever appear as infrastructure: `building.street, building.bridge, building.drainage, building.stairs_ramps, building.retaining_wall, building.wall, building.boundary_wall, building.ditch_palisade, building.hamlet_track`.

The `measurement_convention` block in each structure library is not machine-read but is the only place the geometry is explained, so carry it verbatim into the C++ data or its header comment. Its load-bearing statements: local X is width across the frontage, local Z is depth away from the front access edge, Y is height above local grade, and the footprint, clearances and access rotate together. `plot_m` is the *unrotated* reservation. Clearances lie outside the footprint and road carriageways are separate. For `linear_segment` rows, `width` is the corridor cross-section and `depth` is a repeat length or an illustrative bridge span — fit a path with segments, do not tile the ends as independent rectangular plots. A height of zero means no enclosing building height, not empty ground. `minimum_clear_width_m` is route clearance, not a door opening. No terrain suitability, foundation depth, usable floor area, occupancy or structural-span guarantee is implied by these bounding boxes. And, for the engine: *"Future Unreal adapter converts metres to centimetres by multiplying by 100."*

## Random streams

**None.** Neither module draws a random number, seeds a stream, or imports `random`. Both are fully deterministic functions of the two JSON files.

The only randomness anywhere near them is downstream and belongs to other subsystems: `terrain_recipes.seed_config` seeds `random.Random(child_seed(seed,'recipe-v1-geology'))` and feeds it profile-derived constants; `found_cities` seeds `random.Random(child_seed(seed, f'diaspora-{year}'))` and takes `founding_rules` as plain parameters; `terrain_settlements._weighted_pick` draws once per building option against `asset_choices[*].weight`. A C++ port of *this* subsystem needs no `fwg::Rng` at all, and must not introduce one — a fourth seed derivation is explicitly forbidden and none is needed here.

## Ordering hazards

This subsystem is unusually order-sensitive, because its output is hashed.

1. **`registry_identity()` hashes an unsorted serialisation.** `json.dumps(doc, ensure_ascii=False, separators=(',',':'))` walks dicts in insertion order. Therefore the sha256 depends on: the key order of `civilizations.json` as authored; the key order of each nested object; the `parent_races` iteration order (which sets the order entities are discovered, though `civilization_order` then re-orders `entities`); the fact that `entities` is appended after `founding_rules`; the tuple order of `BUILDING_SECTIONS`; and the key order of `buildings.json`'s non-section keys inside `building_catalogue_metadata`. Any of these moving invalidates every saved world via `terrain_history`. A C++ port must pick a canonical form and pin it; sorting keys would be *cleaner*, but it is a different hash and therefore a one-time break — do it deliberately, in the same change as the world format version.

2. **`civilization_order` is the entity iteration order everywhere.** `data['entities']` is built as `{key: entities[key] for key in order}`, so `load_registry()['entities']`, `population_profiles()`, `profiles()`, `civilization_ids()`, `profile_options()`, `building_pack_data()`'s `population_profiles` and `profiles` lists, `layout_profile_data()`'s `profiles` lists, and `civilization_report`'s `region_index` all follow it. `region_index` in particular is an **ordinal that the map viewer uses to pick a colour**, and the suitability/magic-risk layer names are `suitability_<id>` — so reordering `civilization_order` renumbers regions.

3. **`eligible_civilizations` sorts by `(priority, key)`** — an ascending numeric priority then a lexicographic id tie-break — and claims `allocation_group`s first-come. All six human civilizations share `allocation_group: "human_geography"`, so **exactly one human civilization can be eligible at any given cell**, and it is the lowest-priority-number one whose habitat matches: cold(0), desert(1), rainforest(2), large_island(3), maritime(4), heartland(5). Heartland has an empty habitat, so it is the fallback that always matches but is checked last. Change a priority and you change which human people owns a region.

4. **`classify_cities` uses `min(members, key=lambda s: (not s['founding_capital'], -s['suitability'], s['node']))`** — a founding capital first, then highest suitability, then **lowest node index**. That is literally what `capital_policy` spells out, and node index is latitude-ordered on the sphere grid, so the tie-break is not neutral.

5. **Programme row order is the authored JSON array order** and is preserved through `_expand_settlement_plan` into `city_plans`/`hamlet_plans`, which the city planner then places in order. Reordering the arrays moves buildings on the ground.

6. **`_validate_settlement_blocks` iterates `('buildings','infrastructure')` in that order** and resets `seen` between them, so the first error you hit depends on group order. More importantly, the same `structure_id` may legally appear once per group.

7. **`STAFF_LEVELS` order defines the monotonicity check.** `values == sorted(values)` with `('minimum','target','maximum')` means min <= target <= max; any other tuple order means something else.

8. **`_unique_object` makes duplicate JSON keys fatal**, so parse order cannot silently pick a winner. A C++ JSON parser that last-wins or first-wins must be changed to reject.

9. **`json.dumps(result, sort_keys=True)` for `definition_hash`** sorts, so that one is order-independent — but it is a *different* canonicalisation from the identity hash (default separators, `ensure_ascii=True`). Two canonical forms, deliberately unaligned; do not unify them by accident.

10. **Set iteration never reaches output.** `FIELDS`, `PLACEMENT_CONDITIONS`, `pack_index` value sets, `layout_index` value sets, `seen` and `claimed` are all used only for membership and equality. `HIDDEN_VARIANT_IDS` and `VARIANT_IDS` likewise. The only place a set becomes a list is `sorted(...)` in `asset_list` and in `found_cities`' report, both explicitly sorted.

11. **`data.update({...})` and `summary.update(...)` append new keys at the end**, which is why `total_residents`, `houses_required` and `basis` sit after the three groups in `staffing_summary`, and why the resolved plan's last four keys are `civilization_id, unit, runtime_placement_enabled, <block_key>`.

## Edge cases

- **No world edges, no poles.** Neither module touches a grid.
- **Empty habitat rule `{}`** is the universal rule and returns true. `validate_habitat({})` returns immediately without checking anything; `matches({}, env)` returns True. Six of twelve civilizations rely on it.
- **`matches` raises `KeyError` on a field the caller did not supply.** `environment_at` supplies only `abs_latitude, biome, temperature, moisture, maritime` plus the three landmass fields; `terrain_settlements` adds `resource, slope, height, tpi, variant, coastal_support` before evaluating `surface_habitat`/`world_habitat`. So the *eligibility* habitat (`civilization.habitat`) may only use the first eight fields, and the *settlement* habitats may use all fourteen. Nothing enforces that split — it is a latent crash if an author uses `tpi` in `civilization.habitat`.
- **`coastal_support` is supplied as `0.0` when not on world recipe 3**, so a rule requiring `coastal_support > 0.1` (tidekin) silently matches nothing on the geometry-lab recipe. That is intended: tidekin only exists on the full recipe.
- **Bools are rejected wherever an int is required.** `type(v) is not int` is false for `True`. But `_number` uses `type(value) not in (int, float)`, which also rejects bools. Conversely `validate_habitat` deliberately *allows* bools in `in` and `equals`, for `largest_landmass`.
- **`math.isfinite(True)` is True** — bools pass the finiteness check in habitat bounds.
- **`_finite` never sees a non-finite int** (impossible) and ignores strings; a NaN reaches it only as a float, and `json.loads` produces `NaN`/`Infinity` from the non-standard literals, which is exactly why the check exists.
- **Empty `roles` list is legal** and is required for `linear_segment` structures. `staffing_totals` then returns all zeros.
- **`staffing_totals` accepts `count == 0`**, but a programme buildings row must have `count >= 1`. The zero path exists only for a direct caller.
- **A structure that no programme references is never geometry-checked.** Its `plot_m`/`clearance_m` identity is unverified. All 106 are staffing-checked, though.
- **Floating-point exact equality on the plot identity.** `plot.width == size.width + clear.left + clear.right` is an exact `==`. Every reference value is a small integer or a 1-decimal value that happens to sum exactly, so it passes; authored values like `2.1 + 0.7 + 0.2` would not. A C++ port must decide whether to keep exact equality (bit-for-bit reproducing the current acceptance set) or add an epsilon (accepting strictly more data). Keeping exact equality is the conservative choice; if you add an epsilon, say so.
- **An entity binding zero packs** would fail at step 12j (the fallback pack must be bound).
- **A pack all of whose options end up with empty `profiles`** becomes a pack with an empty `building_options` list, which `building_pack_data` does not remove. The consumer must tolerate it.
- **`section(name)` on a missing key raises a bare `KeyError`**, not a `ValueError` — it is outside the `_document()` try. Same for `entity_rules(unknown_id)`.
- **`city_plan` with an unknown `city_block` raises before touching the document**; with an unknown entity id it raises `'Unknown civilization'`.
- **LRU cache staleness.** `_read` and `_resolve` are keyed on `(path, st_mtime_ns, st_size)`. Two different contents written to the same path in the same nanosecond with the same byte length would return the stale parse. On a filesystem with coarse mtime granularity this is reachable; on NTFS with `st_mtime_ns` it is very unlikely. `maxsize=8` means more than 8 distinct registry versions in one process evicts the oldest, which only costs a re-parse.
- **`_document()` does not cache the `stat` calls**, so every accessor stats two files. `city_plan` is called once per settlement per world; `get_profile` is called inside per-cell loops in some stages after being hoisted. A C++ port should load once and hold, but must keep the "an edit is picked up" behaviour if the lab's hot-reload is being preserved.
- **`validate_registry` returns the same object it was given**, not a copy, and mutates nothing. `load_registry`/`section`/`entity_rules` deep-copy on the way out, which is the only thing protecting the cached document from a caller.
- **`validate_profile` aliases `identity` into its result** rather than copying it. Harmless because the caller already holds a deep copy, but a port that hands out references must be careful.
- **An empty `reason` string is legal** (`rules['reason'] is not None and not isinstance(rules['reason'], str)`), and then `reason or defaults[...]` in `terrain_settlements` falls through to the default — so `""` behaves as `null`.
- **`configurations` may be empty**; only `defaults.aggregate_profile_id` must resolve, so at least one is required in practice.
- **`entities` may not be empty** (1..128).

## Reasoning worth preserving

Quoted verbatim; these are the expensive parts.

From `terrain_profiles.biome_preference`:
> "An exact magical state overrides its natural-core preference.
> Ground held by a hidden school is uninhabitable to everyone, and that is a rule rather than 52 authored opinions per people: nobody has a cultural preference about a school they have never heard of. Without it the fallback below would read a rotted grassland as plain grassland, and settlement placement would find corrupted ground exactly as attractive as clean ground."

From `terrain_profiles.biome_food_multiplier`:
> "Exact state food factor, before independent school-specific hazard losses.
> Corrupted ground feeds nobody; which of the four took it is the corruption record's business, not the terrain table's."

From `terrain_profiles`, above `HIDDEN_VARIANT_IDS`:
> "Ground a hidden school holds. A profile may still author an explicit opinion about one, but none does and none needs to: these are the defaults below."

From `civilization_registry.staffing_totals`:
> "Count unique roster posts, assuming each post has its own resident worker."

From `civilization_registry.building_pack_data`:
> "Empty profiles mean unrestricted in the legacy layout engine. Remove truly unreachable options instead of accidentally enabling them."

From `civilization_registry.city_plan` / `hamlet_plan`:
> "Resolve an independent size preset into measured, unplaced requirements." / "Resolve the rural hamlet preset into measured, unplaced requirements."

The `staffing_summary.basis` string, which is the model's own disclaimer:
> "One distinct resident worker per filled roster post; household members and housing occupancy not yet modeled."

The staffing `notes` carried on every structure:
> "Provisional operating roster per instance, including working owners and apprentices where listed. Distinct people across all shifts; excludes visitors, customers, patients and dependents. Each post assumes a different worker unless a future assignment model shares jobs."

Module docstrings:
> `civilization_registry`: "Validated access to linked civilization and building authoring registries."
> `terrain_profiles`: "Validated population views of the master civilization registry."

The registry's own `description` field:
> "Single authoring source for independent civilizations, generic rule parameters, presentation, building/layout libraries and measured structure blocks. **No population inheritance.**"

The buildings registry's `description`:
> "Shared building definitions, measurements, staffing, runtime packs and layout libraries. Civilization presets reference these records. Castle fortification IDs live under structure_blocks.castle for the independent castle planner."

Library `scale_policy` entries, which explain why the counts are what they are:
> counts: "Use civilization city-size preset quantities as provisional budgets; reconcile with population, throughput, terrain and area during placement."
> sharing: "Several functions may occupy one structure or courtyard. Shared occupancy must not duplicate service capacity."
> geometry: "Use explicit dimensions_m and clearance_m to reserve plot_m; do not scale randomly."
> world_budget: "Do not give every city every specialist structure. Bound active detail and distant simulation separately."
> housing: "Dwellings, apartments, noble residences, guest rooms, dormitories and barracks sleeping accommodation are deferred."
> asset_links: "Reuse candidates are existing semantic references only, not complete coverage or approved assets. Empty lists identify gaps for the later catalogue rebuild."
> state: "Future construction, damage and ruins need supported state assets; these design rows do not enable new generator states."

`priority_definitions`, the meaning of the structure-level `priority` field:
> core: "Provide this service in a city, allowing explicitly planned shared buildings or supplied alternatives."
> conditional: "Use only when the stated terrain, resource, economic or defensive conditions apply."
> specialist: "Reserve for cities whose role, wealth, institutions or future gameplay justify it."

Housing profile notes, which explain the densification invariant the validator enforces:
> worker_apartment: "Four-storey shared worker housing; **same reserved plot as a worker house allows deterministic densification.** Provisional capacity, no dependents."
> hamlet_house: "Compact rural dwelling for hamlet staffing; culturally distinct from city worker houses. No apartment densification in planner v1."

From `terrain_history` (the consumer that makes the identity hash load-bearing):
> "The civilization registry changed since this world was generated, so the entities it names may no longer resolve. Regenerate or explicitly migrate it."

From `npc_roster.__init__._registry`, on why it snapshots rather than imports:
> "The labels come from a snapshot of `buildings.json` carried in the policy, because this package must not import the generator. A per-civilization size-block `staffing` override replaces the library roster for that entry alone, and no override exists today -- so the snapshot is correct now and could silently mislabel one civilization's posts later. Naming the world's own registry identity beside the snapshot's is what makes that visible... The block reports it rather than failing, because the registry revision moves for reasons that have nothing to do with staffing."

From `terrain_civilizations.heritage_of`, on the import placement:
> "Imported here rather than at module scope because heritage is a leaf package that knows nothing of the registry: something that imports both has to pair a civilization with its parent race, and the report is the natural place for that."

From `terrain_biome_catalogue.biome_catalogue` (why the variant list order is frozen, which is why `VARIANT_IDS` can be trusted as a contract):
> "A position in this list is a contract. `biome_variant` stores a flat index into it, so the known cores-by-schools block keeps the positions it has always had and hidden schools take the positions after it. That is why this is two passes and not more columns in one: another column would renumber every variant in every saved world."

From `terrain_leyline_history`, on the hidden schools:
> "Appended, never inserted... Generation can never raise one. Their occurrence is locked to zero in the option registry, so generate_networks always leaves them empty, and only the corruption API puts a node in one. They are declared here, and nowhere else, because a school that does not exist in the taxonomy cannot be edited into a world by a caller who guesses its name."

**And one undocumented intent worth stating explicitly, because it is the design of the whole file:** there is no inheritance and no defaulting anywhere in a population profile. `FIELDS` is checked by set equality, so every civilization restates all 38 traits. A previous design had `extends`; a test still asserts `assertNotIn('extends', entity['population'])`. Do not reintroduce trait inheritance in the C++ port to save typing — the flat record is the feature.

## Notes for the C++ implementation

**The single biggest gap: FWGC++ has no JSON reader.** `include/fwg/json.hpp` is a writer only ("Only writing is supported"). This subsystem is 90% JSON reading and validating. You must either (a) write a small reader alongside it — it must preserve object key insertion order, reject duplicate keys, distinguish integer from floating-point literals, and reject the non-standard `NaN`/`Infinity` literals or surface them so `_finite` can reject them — or (b) compile the registry into C++ data structures at build time and keep the JSON only as the authoring format. Option (b) removes the LRU-cache-on-mtime hot-reload that the terrain lab relies on; if the lab is being ported, you need (a). Given "no dependencies at all", (a) means about 400 lines of hand-rolled parser with an order-preserving object type.

**Python idioms with no direct equivalent:**
- `dict` preserving insertion order *and* being hashed in that order. C++ `std::map` sorts; `std::unordered_map` has no order. You need an insertion-ordered map (a `std::vector<std::pair<std::string, Value>>` with an index side-table) for the JSON document type, or you must accept a different canonical form for `registry_identity()`.
- `set(a) == {…}` for exact key-set equality: build a sorted vector of keys and compare against a sorted literal, or count-and-contains.
- `set(a) <= set(b)`: `std::includes` on sorted ranges.
- `type(v) is not int` rejecting `bool`: in C++ a JSON value should carry an explicit `Int | Double | Bool | String | Null | Array | Object` tag, and the int check must reject the Bool tag. Do not collapse bool into int.
- `{**structure, **row}` (dict merge, right wins): copy the structure's ordered map, then assign each of the row's keys, which must *replace in place* if the key already exists and *append* otherwise — that is exactly Python's semantics and it is what makes the resolved row's key order `…structure keys…, structure_id, count, placement_conditions, staffing_totals`.
- `copy.deepcopy` on every accessor: in C++, return by value from an owned document, or hand out `const&` and make the callers' mutation impossible. Handing out `const&` is better and is a deliberate behaviour change worth noting (the Python contract explicitly allows callers to mutate).
- `functools.lru_cache` keyed on `(path, mtime_ns, size)`: a small `std::map<key, shared_ptr<Document>>` with an LRU list, or just a single cached document plus a stat check.
- `re.fullmatch('[a-z][a-z0-9_]*', key)`: hand-roll it; do not pull in `<regex>` (slow, and a dependency-free build should not pay for it). Same for `building\.[a-z][a-z0-9_]*` — check the literal prefix `building.` then the same character class on the tail.
- `str.isdigit()` for biome map keys: Python's accepts some Unicode digits. A C++ `all(isdigit)` over ASCII accepts strictly less. This is one of the very few places the port can legitimately differ; say so in a comment.
- `math.isfinite` on a float: `std::isfinite`.
- `values == sorted(values)` on a 3-element list: just `a <= b && b <= c`.

**Mapping onto existing FWGC++ pieces:**
- `fwg::sha256_hex(std::string_view)` in `sha256.hpp` is exactly what `registry_identity()` needs. `sha256_seed` is not relevant here.
- `fwg::JsonWriter::format_double` produces shortest round-trip text, which matches Python's `repr`/`json.dumps` for doubles. But Python writes `1` for an int and `1.0` for a float, and the registry data mixes both freely (`"support_multiplier": 1`, `"weight": 1.0`). If you want the canonical bytes to be stable across reloads you must preserve the int/double tag from the parse and re-emit it the same way. Since the port is a clean rewrite and numbers need not match, the simplest correct choice is: define your own canonical form, document it, and accept that the sha differs from Python's.
- `fwg::Config` should **not** absorb the registry. The registry is a separate owned document (`fwg::CivRegistry`), loaded once and referenced by `fwg::World`. What *does* belong in `Config` is the five recipe-derived values `terrain_recipes` copies out of the default profile: `human_magic_limit`, `road_max_grade`, `human_adaptation`, `urban_food_demand`, `stubbornness`.
- `fwg::World` should carry the `{schema_version, revision, sha256}` identity triple in a typed block (the Python puts it at `world['civilizations']['registry']`), because the age-advance gate compares it.
- `fwg::Rng` is not used here at all.
- `fwg::Grid` and `fwg::sphere_grid` are not used here at all.

**Suggested C++ shape:**
```
struct StaffRole { std::string role; int minimum, target, maximum; };
struct Staffing { std::string basis, notes; enum { City, Hinterland } housing_location;
                  std::vector<StaffRole> roles; };
struct Dimensions { double width, depth, height; };
struct Clearance  { double front, rear, left, right; };
struct PlotSize   { double width, depth; };
struct Structure { std::string id, name, status, priority, module_family, purpose, placement;
                   std::vector<std::string> requirements, reuse_candidates;
                   GeometryType geometry_type; Dimensions dimensions_m; Clearance clearance_m;
                   PlotSize plot_m; Access access; Staffing staffing; };
struct ProgrammeRow { std::string structure_id, name;      // name empty => use the structure's
                      int count = 0;                       // 0 for infrastructure
                      std::string quantity_mode;           // empty for buildings
                      std::vector<PlacementCondition> placement_conditions;
                      std::optional<Staffing> staffing; };
struct HabitatRule { enum { Empty, All, Any, Compare } kind;
                     std::vector<HabitatRule> children;
                     HabitatField field;
                     std::optional<double> min, max, gt, equals;
                     std::vector<Term> in_;                // Term = string | double | bool
                     };
struct Profile { /* 38 named fields, NOT a map<string,double> */ ... };
```
**Do not model a profile as `map<string,double>`.** A previous C++ attempt did exactly that and a string-valued trait reached a `number()` conversion and threw at runtime; the identity strings (`kind`, `environment`, `allocation_group`, `structure_blocks`) survived only by being read as explicitly named fields. Name all 38 fields, and name the four biome maps as their own typed members (`std::map<int,double>` for the two natural ones — the string keys are decimal ids and should be converted on load — and `std::map<std::string,double>` for the two variant ones).

**Habitat evaluation** should be a small recursive `bool matches(const HabitatRule&, const Environment&)` over a `struct Environment` with all fourteen fields and a per-field `has_value` flag, so that the Python `KeyError` becomes an explicit "habitat rule uses a field this evaluator was not given" error rather than a default-zero silent wrong answer. That is a genuine improvement and should be called out.

**Distance units.** `water_reach` and `college_water_reach` are **absolute metres, authored against the reference world and used unscaled**. `terrain_settlements` computes `exp(-distances[i] / p['water_reach'])` where `distances` are real world metres, and there is no multiplication by any reach-scale factor anywhere in the path. `fwg::scale::reach_scale(circumference_m)` exists precisely because leaving such reaches absolute makes them meaningless on a 200 km world — but the Python does **not** apply it to these. Decide explicitly: either reproduce the Python (absolute, meaning a 400 m freshwater decay length on a 200 km planet is a rounding error) or route them through `reach_scale` and document it as a deliberate divergence. `land_per_city_km2` is an **area**, so if you scale it, it takes `runoff_scale` (the square), not `reach_scale`. All structure dimensions, clearances and plots are absolute local metres and are correctly *not* world-scaled — a doorway is a doorway on any planet.

**Threading.** Nothing here forks a process pool, but `ICARUS_CITY_WORKERS` (read by `city_planner`/`hamlet_planner`, the consumers of `city_plan`/`hamlet_plan`) parallelises the planners that call into this module. `city_plan` must therefore be thread-safe. The Python is thread-safe only by accident (deep copies plus an LRU cache that is not); a C++ port should load the document once at startup and hand out `const&`, making concurrency trivially safe. Nothing ever proved pooled planner output equals serial output; if you parallelise, write that test.

## Open questions

1. **Stale error message.** `_resolve` checks `raw.get('schema_version') != 6` but raises `'Expected schema 5 with civilizations nested in parent race blocks'`. The check is right, the message is a version behind. Port the check; fix the message.

2. **Two canonicalisations, deliberately unaligned.** `registry_identity()` uses compact separators, `ensure_ascii=False`, insertion order. `definition_hash` uses default separators, `ensure_ascii=True`, `sort_keys=True`. Nothing says whether that is intentional. It probably is (the identity must be insertion-ordered because it is also a change detector on the *file*, whereas a profile hash should be content-addressed), but a porter should record the choice rather than assume it.

3. **The empty-list asymmetry.** For a layout feature, `profiles: []` disables the feature; for a building pack option, `profiles: []` means unrestricted, which is why `building_pack_data` deletes such options outright. Two opposite readings of the same empty list in the same document, one of them pinned by a test. Is that intended, or is the pack-option branch a legacy bug the delete-filter papers over? The comment suggests the latter ("Empty profiles mean unrestricted **in the legacy layout engine**"). A port could unify them on the "empty means nothing fits" reading and delete the filter — but it would change which building options reach the world, so it is a behaviour change, not a cleanup.

4. **Exact float equality on the plot identity.** `plot.width == size.width + clear.left + clear.right`. Every reference value passes, but the check will reject perfectly reasonable future data with one-decimal clearances. Epsilon or not? Say which you chose.

5. **`housing_status` inside `entity['buildings']` is never validated.** It is `"deferred"` in every entity and nothing reads it. The tier blocks' own `housing_status` *is* validated. Dead field, or an oversight that should be validated too?

6. **`support_multiplier` is 1.0 for all twelve civilizations** and only reaches `derive_population`, which is dead on world recipe 3. The knob exists, is validated, and does nothing on the recipe that actually generates worlds. Port it as data, but do not build anything on the assumption that it has ever been exercised.

7. **`reliable_wind` is a declared placement condition that no programme uses.** Harmless, but it means the condition vocabulary has never been fully round-tripped through the planner.

8. **`civilization.habitat` vs `settlement.surface_habitat`/`world_habitat` use the same DSL and the same validator, but different callers supply different environment fields.** A rule in `civilization.habitat` naming `tpi`, `variant`, `resource`, `slope`, `height` or `coastal_support` would pass validation and then raise `KeyError` at scoring time. No check exists. A port should either split the two field vocabularies or supply all fourteen fields to both evaluators.

9. **The LRU cache keyed on `(path, mtime_ns, size)`** can in principle return a stale parse for a same-size, same-nanosecond rewrite. Whether that matters depends on whether the hot-reload behaviour is being ported at all.

10. **No conformance record exists for either module.** `docs/conformance/coverage.json` lists both `Sim/icarus_sim/civilization_registry.py` and `Sim/icarus_sim/terrain_profiles.py` under `uncovered`, ticketed to `board/in-progress/CONFORMANCE-DOCS.md`. Two other records mention them in passing — `settlement-founding.md` ("the civilization registry through `terrain_profiles`") and `settlement-records.md` ("the civilization registry through `civilization_registry` and `terrain_profiles`, the shape catalogue…") — but neither describes their behaviour and neither contradicts the code. This specification is the only description.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** A building pack = {id, name, layout_profile_id (may be null), criteria (object; `{}` in every reference pack), weight (float), building_options: [...]}
  **Actually:** Only city_pack_generic_surface has criteria {}. The other six carry substantive selection criteria (biomes, biome_variants, freshwater_distance_m_min/max, height_min_m, slope_max_degrees, resource_min, resource_max). building_pack_data() writes pack['criteria']['population_profiles'] into that dict, so it is read by the consumer. Treating criteria as always-empty removes pack selection entirely.

- **Claimed:** NATURAL_BIOMES ... 13, with non-contiguous ids {0,1,2,3,4,5,6,7,8,13,15,16,17} and cores tundra, ?, desert, grassland, forest, exposed_rock, boreal_forest, rainforest, ?, marsh(13?), ..., cold_tundra, snow, land_ice
  **Actually:** The mapping is {0:'ocean', 1:'tundra', 2:'desert', 3:'grassland', 4:'forest', 5:'exposed_rock', 6:'snow', 7:'rainforest', 8:'lake', 13:'marsh', 15:'boreal_forest', 16:'cold_tundra', 17:'land_ice'}. The spec assigns boreal_forest to 6 (it is snow), leaves 1 and 8 as '?', and never names ocean or lake. Because habitat rules address biomes by raw id, this misassigns civilizations: human_cold's [1,6,15,16] is tundra/snow/boreal_forest/cold_tundra, not what the spec's table implies.

- **Claimed:** Schools: 8 known (... weave, infernal, umbral, water, earth, air, ...) then 4 hidden (blood, void, rot, eldritch), in that order
  **Actually:** The actual order is weave, umbral, infernal, radiant, fire, water, earth, air, then blood, void, rot, eldritch. `radiant` and `fire` are omitted from the spec entirely, and umbral/infernal are transposed. The spec itself states that a position in this list is a contract for the flat biome_variant index, so this error renumbers every variant.

- **Claimed:** HIDDEN_SCHOOLS from terrain_leyline_history: {'blood','void','rot','eldritch'}
  **Actually:** HIDDEN_SCHOOLS is a dict, not a set: {'blood': ('Outside', 'sacrifice, lineage, vitality, hunger', [140,26,38]), 'void': ('Outside', 'absence, entropy, unmaking, silence', [26,22,40]), 'rot': ('Outside', 'disease, undeath, decay, rebirth', [124,130,62]), 'eldritch': ('Outside', 'madness, wrong geometry, dominion of minds', [62,96,92])}. Membership testing behaves identically so the derived HIDDEN_VARIANT_IDS (52) is right, but the type and its payload are not what the spec says.

- **Claimed:** coastal_score_weight, resource_score_weight | 0..2 | 0.15 typical, 0.35 tidekin, 0.25 frosthold
  **Actually:** Those three values belong to coastal_score_weight alone (0.15 for all twelve except tidekin 0.35 and frosthold_dwarf 0.25). resource_score_weight is 0.0 for eleven civilizations and 0.15 for gnome only — the one value the spec never gives.

- **Claimed:** `mixed` aggregate matches `human_heartland` except food_moisture_ideal 0.65 and food_temperature_ideal 18
  **Actually:** food_temperature_ideal is 18 in both, so it is not a difference. The actual differences are food_moisture_ideal (0.6 -> 0.65), name ("Heartland humans" -> "Humans, dwarves and elves"), description, and the whole civilization identity block (kind aggregate, inspiration null, allocation_group null, structure_blocks null, priority 0, habitat {}, environment "Shared productive-land capacity, habitat-derived city counts, exclusive sites and conservative common surface roads.").

- **Claimed:** Bools are rejected wherever an int is required. `type(v) is not int` is false for `True`.
  **Actually:** `type(True) is not int` evaluates to True, so the guard fires and bools ARE rejected. The sentence as written says the opposite of both the source and the spec's own heading, and would lead a porter to collapse bool into int in the JSON value tag — which is the exact mistake the spec's C++ NOTES warn against.

- **Claimed:** math.isfinite(True) is True — bools pass the finiteness check in habitat bounds.
  **Actually:** Bools never reach math.isfinite in habitat bounds. validate_habitat checks `type(rule[key]) not in (int,float)` for 'min','max','gt', and type(True) is bool, so the bound is rejected first. Bools are accepted only in `in` and `equals`, where the type tuple is explicitly (int,float,bool) — which is how {"field":"largest_landmass","equals":false} works.

- **Claimed:** heartland {}; dwarf/elf/gnome/tidekin/frosthold {}; ... frosthold's own civilization.habitat is {field:temperature, max:5}
  **Actually:** Self-contradictory, and the first half is wrong for frosthold_dwarf. The civilization.habitat values are: dwarf {}, elf {}, gnome {}, tidekin {}, frosthold_dwarf {"field":"temperature","max":5}, hill_dwarf {"field":"biome","in":[3]}.

- **Claimed:** Two profiles: standard_city_layout and riverfront_city_layout, both with the same 7 features ... Reference standard_city_layout placement: river_buffer_m 90, river_fork_bridge_threshold 2, bridge_if_river_fork true, max_anchor_slope_degrees 16.
  **Actually:** The feature NAMES are the same but the feature VALUES differ between the two profiles, and riverfront's placement block is {river_buffer_m: 70, river_fork_bridge_threshold: 2, bridge_if_river_fork: true, max_anchor_slope_degrees: 14}, not 90/16. Giving only standard's reads as if both were identical.

- **Claimed:** Library scale_policy, quoted verbatim — geometry: "Use explicit dimensions_m and clearance_m to reserve plot_m; do not scale randomly."
  **Actually:** The quote is truncated. The full value is "Use explicit dimensions_m and clearance_m to reserve plot_m; do not scale randomly. Revise defaults after map and gameplay clearance prototypes." The dropped sentence is the one recording that the defaults are provisional pending prototypes — exactly the kind of intent the spec sets out to preserve.

- **Claimed:** Sim/icarus_sim/civilizations.json — Full schema and the entire reference content are given under `reads` and `constants`.
  **Actually:** Not true. The settlement habitat rules, pack criteria, layout feature counts, pack weights, per-entity economy/sky/presentation/bindings, the ~1556 programme building rows, the 222 infrastructure rows, the 106 structure records and the 90 pack building_options are all absent. Since the repo is being deleted, this sentence would lead a porter to discard the JSON believing the spec superseded it.

### Missing — blocking

**NATURAL_BIOMES: the exact id → core mapping**

{0: 'ocean', 1: 'tundra', 2: 'desert', 3: 'grassland', 4: 'forest', 5: 'exposed_rock', 6: 'snow', 7: 'rainforest', 8: 'lake', 13: 'marsh', 15: 'boreal_forest', 16: 'cold_tundra', 17: 'land_ice'}. Ids are non-contiguous (9-12 and 14 do not exist). These integers are what habitat rules and biome_preferences keys name directly: human_cold is `biome in [1,6,15,16]` = tundra/snow/boreal_forest/cold_tundra; dwarf is `[5]` = exposed_rock; elf is `[4,7,15]` = forest/rainforest/boreal_forest; hill_dwarf `[3]` = grassland; human_desert `[2]`; human_rainforest `[7]`. biome_preferences keys are the decimal strings of these ids.

**The twelve magic schools in their frozen catalogue order**

`weave, umbral, infernal, radiant, fire, water, earth, air` (the 8 known, positions 0-7) then `blood, void, rot, eldritch` (the 4 hidden, positions 8-11). biome_catalogue() emits 13 cores x 12 schools = 156 entries, core-major, school-minor, so `ocean.weave` is flat index 0, `ocean.umbral` 1, `ocean.infernal` 2. Each entry is {id, core, core_biome_id, magic_school, group, descriptors, name, color, asset_id}; `id` is "<core>.<school>". The spec omitted `radiant` and `fire` and mis-ordered umbral/infernal.

**Building-pack `criteria`: the vocabulary and the per-pack values**

criteria keys observed: `biomes` (list[int] of natural biome ids), `biome_variants` (list["core.school"]), `freshwater_distance_m_min`, `freshwater_distance_m_max` (metres), `height_min_m`, `slope_max_degrees`, `resource_min`, `resource_max` (0..1). Values: city_pack_generic_surface {}; city_pack_human_rivergate {biomes:[13,15], freshwater_distance_m_max:2500, biome_variants:[]}; city_pack_human_highland {biomes:[5,6], height_min_m:20, slope_max_degrees:40, biome_variants:["desert.infernal","desert.water","desert.weave"]}; city_pack_dwarven_metalwork {biomes:[5,6], resource_min:0.4, biome_variants:[same three]}; city_pack_elf_woodland {biomes:[4,7], resource_max:0.9, biome_variants:["boreal_forest.earth","boreal_forest.weave","forest.earth","forest.weave","rainforest.earth","rainforest.weave"]}; city_pack_tideport {biomes:[13,15], freshwater_distance_m_min:100, freshwater_distance_m_max:4500, biome_variants:[]}; city_pack_gnome_foundry {resource_min:0.5}. building_pack_data() writes `population_profiles` into this same dict, so it is not a spare field.

**Layout-profile feature counts — the numbers that decide how many buildings a city gets**

standard_city_layout: leader_homes 1, barracks 1 (both bare integers, not objects), noble_homes {base:1, per_100_residents:0.04, max:3}, worker_housing {base:2, per_100_residents:0.25, max:24}, apartments {base:0, per_100_residents:0.15, max:8}, market_district {base:1, max:3}, religious_building {base:1, max:2}. riverfront_city_layout: leader_homes 1, barracks 1, noble_homes {base:1, per_100_residents:0.05, max:3}, worker_housing {base:3, per_100_residents:0.32, max:26}, apartments {base:1, per_100_residents:0.24, max:9}, market_district {base:2, max:4}, religious_building {base:1, max:2}. leader_homes and barracks being scalars is the only place layout_profile_data()'s boxing into {'base': v} actually fires.

**The per-entity settlement habitat rules — the actual DSL trees**

All six humans, gnome and tidekin have surface_habitat {}. Actual rules: dwarf surface=world={all:[{field:resource,min:0.5},{any:[{field:biome,in:[5]},{field:variant,in:["desert.weave","desert.infernal","desert.water"]},{field:tpi,gt:6},{all:[{field:slope,gt:10},{field:height,gt:15}]}]}]}. elf surface={all:[{field:biome,in:[4,7,15]},{field:moisture,min:0.55}]}, world= same but moisture min 0.4 (the only civ whose two habitats differ). gnome surface={}, world={field:resource,gt:0.45}. tidekin surface={}, world={all:[{field:maritime,gt:0.3},{field:coastal_support,gt:0.1}]}. hill_dwarf surface=world={all:[{field:biome,in:[3]},{field:slope,max:18}]}. frosthold_dwarf surface=world={all:[{field:abs_latitude,min:50},{field:temperature,max:5},{field:resource,min:0.45},{any:[{field:biome,in:[5]},{field:tpi,gt:6},{all:[{field:slope,gt:10},{field:height,gt:15}]},{all:[{field:maritime,gt:0.3},{field:slope,min:8}]}]}]}.

**An explicit instruction to copy the two JSON files out before the repo is deleted**

civilizations.json (495 KB) and buildings.json (261 KB) contain roughly 1556 programme building rows, 222 infrastructure rows, 106 structure records with purpose/placement/requirements/reuse_candidates prose, and 90 pack building_options with asset_choices and weights. None of that is reproducible from this specification, and the spec's DATA FILES section instead asserts "Full schema and the entire reference content are given under `reads` and `constants`". The port must archive both files (and biomes.json, and npc_roster/policies/posts.json) as authoring data before deletion.

### Missing — important

**Per-entity settlement/economy/sky/presentation values**

settlement.freshwater_reach_multiplier: 2 for dwarf, elf, frosthold_dwarf; null for the other nine. support_outside_habitat: true for dwarf and frosthold_dwarf only. coastal_score_weight: 0.15 for all except tidekin 0.35 and frosthold_dwarf 0.25. resource_score_weight: 0.0 for all except gnome 0.15. reason: null for the six humans, gnome and tidekin; dwarf "Mineral-rich upland with freshwater access", elf "Moist forest habitat with freshwater access", hill_dwarf "Fertile grassland and gentle slopes suited to a pastoral hill-dwarf community", frosthold_dwarf "Cold mineral-rich upland or coastal cliff with reachable freshwater and supply hinterland". economy: fishing_reach_multiplier 1.0 except human_cold 1.25, tidekin 1.5, frosthold_dwarf 1.25; winter_fishing_fraction 0 except human_cold 0.25, frosthold_dwarf 0.15; industrial/air_terminal_resource_min null except gnome 0.5/0.5. sky: {0,0} for all except gnome score_bias 4, tidekin score_bias -10, elf moisture_score_weight 3. presentation map_color_rgb: human_maritime [83,161,190], human_desert [210,171,103], human_cold [149,172,202], human_large_island [138,165,115], human_rainforest [70,136,105], human_heartland [184,131,113], dwarf [142,142,154], elf [123,185,143], gnome [193,166,98], tidekin [102,189,184], hill_dwarf [185,174,95], frosthold_dwarf [133,182,207]. wall/roof: the six humans, gnome and tidekin share [255,232,194]/[186,102,83]; dwarf [185,194,207]/[104,122,150]; elf [179,232,199]/[54,131,101]; hill_dwarf [197,183,135]/[99,139,71]; frosthold_dwarf [111,129,145]/[60,92,114]. outpost_colors true for all except dwarf, elf and frosthold_dwarf.

**Which parent race each civilization belongs to, and the pack/layout/library bindings**

parent_races.human holds human_maritime, human_desert, human_cold, human_large_island, human_rainforest, human_heartland; parent_races.elf holds elf and tidekin; parent_races.dwarf holds dwarf, gnome, hill_dwarf, frosthold_dwarf (gnome's parent race is dwarf, which `heritage` depends on). Pack bindings: the six humans and hill_dwarf bind {generic_surface, human_rivergate, human_highland}; dwarf and frosthold_dwarf bind {generic_surface, human_highland, dwarven_metalwork}; elf binds {generic_surface, elf_woodland, tideport}; gnome binds {generic_surface, gnome_foundry}; tidekin binds {generic_surface, tideport}. Every entity binds EVERY option of every pack it binds, and binds both layouts with all 7 features. Consequence: building_pack_data()'s empty-profiles drop filter and layout_profile_data()'s empty-profiles disable path never fire on reference data, so Open Question 3's asymmetry is entirely untested by the shipped data. structure_block_ids is ['common','rural'] for all twelve — nobody binds 'castle', so its 13 structures are staffing-validated but never geometry-validated and are unreachable through city_plan/hamlet_plan; castle_planner reads section('structure_blocks') directly.

**Pack weights and layout bindings**

city_pack_generic_surface weight 1.0 / standard_city_layout; city_pack_human_rivergate 1.2 / riverfront_city_layout; city_pack_human_highland 0.8 / standard; city_pack_dwarven_metalwork 1.5 / standard; city_pack_elf_woodland 1.2 / standard; city_pack_tideport 1.3 / riverfront; city_pack_gnome_foundry 1.1 / standard. No reference pack has a null layout_profile_id, and validation uses pack.get('layout_profile_id') is not None, so an absent key is legal too.

**riverfront_city_layout placement block**

{river_buffer_m: 70, river_fork_bridge_threshold: 2, bridge_if_river_fork: true, max_anchor_slope_degrees: 14} — it differs from standard_city_layout (90 / 2 / true / 16). The spec gives only standard's, implying the two match.

**The `_number` helper, which the whole validator is built on**

_number(value, low, high, label, nullable=False): if nullable and value is None, return immediately; otherwise raise ValueError('Invalid registry ' + label) if `type(value) not in (int, float)` (which rejects bool, since type(True) is bool), or not math.isfinite(value), or not low <= value <= high. Every numeric bound in the table is this one function; the spec names it only in passing in EDGE CASES and never defines it.

### Missing — minor

**Concrete error-message strings not quoted in the spec**

_document's link check: 'Expected a sibling buildings.json registry link with schema version 2'. _resolve: 'Unsupported building registry schema', 'Invalid building registry revision', 'Incomplete building registry', 'Invalid or duplicate neutral building ID'. _validate_staffing: 'Invalid staffing basis', 'Invalid worker housing location', 'Missing staffing notes', 'Invalid staffing roles', 'Invalid or duplicate staffing role', 'Invalid staffing range'. staffing_totals: 'Invalid staffing instance count'. _validate_settlement_blocks: 'Empty <label> plan group', 'Unknown or duplicate <label> structure', 'Invalid <label> placement condition', 'Invalid <label> building count', 'Missing <label> building name'. validate_registry: 'Invalid registry revision', 'Missing parent race blocks', 'Invalid parent race', 'Invalid housing capacity', 'Expected 1..128 civilization entities', 'Entity/configuration IDs collide', 'Invalid civilization ID', 'Invalid registry default', 'Unsupported capital policy', 'Duplicate library ID', 'Invalid library fallback', 'Duplicate building option', 'Unknown pack layout', 'Incomplete civilization entity', 'Unknown parent race', 'Entity must declare entity kind', 'Invalid support policy', 'Invalid site reason', 'Invalid display color', 'Invalid outpost display flag', 'Unknown or duplicate building reference', 'Unknown layout feature reference', 'Unknown structure block library', 'Entity needs the rural hamlet structure library', 'Invalid aggregate configuration'.

**The cache key uses the resolved path**

_document() computes `path = str(REGISTRY_PATH.resolve())` and `str(building_path.resolve())` before passing them to the lru_cache'd _read/_resolve, so symlinked or relatively-addressed paths collapse to one cache entry. The spec says the key is (path, mtime_ns, size) without mentioning resolution.

**structure library `scope` and the guildhall roster note**

Each library carries scope, e.g. common: "Shared city layout requirements for all civilizations, subsequent planner work and catalogue rebuilding. Structures are functional requirements, not one mesh or one mandatory instance each." And building.guildhall's staffing notes, which record deferred work: "Minimal service roster: one cook and one bartender, two distinct city residents. No additional shifts, officers, clerks or heroes counted. Hero membership, activity and accommodation calculations are deferred to HERO-GUILD backlog work."
