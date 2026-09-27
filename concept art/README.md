# Concept art production map

This is the master checklist for visual development of the FWGC++ game world. The generator defines a **procedural setting**, so each checked item is a reusable visual language or asset family, not one image for every generated seed, settlement, person, or site. The inventories below include every currently authored visual identity in `generator/data/` and the additional visual systems a game client will need. Add new catalogue IDs here when authored content grows.

No art is marked complete. A checkbox is complete only when the source file is saved in the mapped folder, named with the catalogue ID where one exists, and reviewed against the approved Tier 1 style guide. A concept image alone does not imply a game-ready mesh, texture, icon, or animation.

Retain only owner-approved artwork in this folder. Keep new previews outside the repository until reviewed, and remove rejected or superseded versions. Supporting notes retain the approved direction, review limitations and generation provenance.

Character variations should provide distinct useful references: vary hairstyle, garment silhouette, palette, equipment and pose while retaining the approved anatomy, rendering and cultural material language. This applies to male/female examples as well as other individuals.

Biome concept workflow (owner direction, 2026-09-25): generate three distinct whole-scene images for each future biome: a basic natural landscape, a separate setting with ruins integrated into that biome, and a storm scene appropriate to its climate. The set must broaden references for later modeling: vary viewpoint, composition, terrain forms, vegetation arrangements, rock formations and visible features across all three. Do not insert ruins or apply storm effects to the base image and reuse its layout. Share the approved rendering and biome identity, while adapting weathering, reclamation and storm behavior to each new setting. Present all three for review; approval of one does not approve the others. Keep unapproved previews outside the repository. Ruins and storm studies use existing biome identities, not new catalogue IDs. Earlier approved pairs remain approved; no retrospective storm coverage is implied.

## The three tiers

Season coverage (owner direction, 2026-09-25): existing approved concepts serve as summer references. Add one separate winter landscape per biome, alongside the summer base, ruins and storm scenes. The initial winter pass covers grassland, forest and exposed rock (three images); it does not request winter copies of every ruin, storm, village or building. Keep the established variety of viewpoints and settings, and adapt winter vegetation, snow, ice and ground exposure to the biome rather than covering every surface uniformly. Future biome sets therefore include three summer scenes and one winter landscape, each reviewed separately.

| Tier | Deliverable | Review question |
|---|---|---|
| **1 — High-level concept art** | Finished key art, mood boards, color and shape scripts, scale and material language, and style sheets showing whole scenes. | Does this lock a consistent art style for the world and its cultures? |
| **2 — Full components, effectively assembled** | Complete creatures, characters, buildings, landmarks, environments, map/UI compositions, and readable state variants. | Can a production artist see how the whole asset fits together in context? |
| **3 — Individual parts for asset generation** | Separated silhouettes, anatomy, costume, architectural modules, terrain pieces, props, decals, symbols, material callouts, and effects layers. | Could these parts be modeled, illustrated, assembled, and varied without guessing? |

Work normally flows **Tier 1 → Tier 2 → Tier 3**. A Tier 2 entry may reuse an approved Tier 1 style sheet; Tier 3 parts may be shared by many Tier 2 entries. Record variations as needed for civilization, biome, season, era, scale, damage, occupation, corruption/cleansing, and the twelve magic schools. Do not produce every Cartesian combination unless it is visible in the game.

## Folder map

```text
concept art/
├── README.md                         ← checklist and source map
├── tier-1-style/                     ← approved full-scene style direction
│   ├── world-and-environments/
│   ├── peoples-and-creatures/
│   ├── settlements-and-locations/
│   ├── magic-and-story/
│   └── maps-and-interface/
├── tier-2-assembled/                 ← whole assets and compositions
│   ├── world-and-environments/
│   ├── peoples-and-characters/
│   ├── creatures/
│   ├── settlements-and-buildings/
│   ├── key-locations/
│   ├── magic-and-faith/
│   ├── props-and-transport/
│   └── maps-and-interface/
└── tier-3-parts/                     ← reusable production breakdowns
    ├── terrain-and-vegetation/
    ├── anatomy-and-costume/
    ├── architecture/
    ├── location-parts/
    ├── props-and-vehicles/
    ├── effects-and-materials/
    └── symbols-and-interface/
```

The folder map is a **navigation map**, not a predesigned world map. A generated world has no single canonical geography. The cartography checklist below covers map styles and procedural map elements.

## Tier 1 — Style and direction

Original approved style exploration: [Heartland human — restrained anime style study](tier-1-style/peoples-and-creatures/people.human_heartland.style-study.md). This established the rendering direction; use the individual character sheets below for ongoing character refinement. The full art bible remains in development and all checkboxes remain open.

Approved core character sheets: [Human — female](tier-1-style/peoples-and-creatures/people.human_heartland_female.style-study.md) and [male](tier-1-style/peoples-and-creatures/people.human_heartland_male.style-study.md), [Dwarf — male](tier-1-style/peoples-and-creatures/people.dwarf_male.style-study.md) and [female smith](tier-1-style/peoples-and-creatures/people.dwarf_female.style-study.md), and [Elf — male](tier-1-style/peoples-and-creatures/people.elf_male.style-study.md) and [female](tier-1-style/peoples-and-creatures/people.elf_female.style-study.md). Each contains one standing figure and one matching headshot. These replace the combined sheet, whose artwork has been removed; refine each people using its individual sheet. The heartland male adds auburn hair, a short beard, red wrap jerkin, ochre sash, slate trousers and compact travel gear, broadening the shared linen/wool/leather clothing language. Preserve their shared restrained anime rendering, including the simplified dwarf. Separate canvases are independently framed, not a physical height comparison. The full twelve-peoples comparison and art bible remain outstanding.

Approved tidekin variants: [male](tier-1-style/peoples-and-creatures/people.tidekin_male.style-study.md) and [female](tier-1-style/peoples-and-creatures/people.tidekin_female.style-study.md), sharing oceanic pirate materials and sharkskin sashes with distinct outfits and hairstyles. Approved gnome variants: [female](tier-1-style/peoples-and-creatures/people.gnome_female.style-study.md) and [male](tier-1-style/peoples-and-creatures/people.gnome_male.style-study.md), sharing colorful steampunk-adventurer design with different palettes, silhouettes and gear. Preserve normal tidekin eyes and human-like gnome noses. The owner requested `_male`/`_female` filenames; these suffixes identify character variants, not new catalogue peoples. Only current approved images are retained.

Approved regional human sheets: [Maritime female](tier-1-style/peoples-and-creatures/people.human_maritime_female.style-study.md), [Maritime male](tier-1-style/peoples-and-creatures/people.human_maritime_male.style-study.md), [Desert male](tier-1-style/peoples-and-creatures/people.human_desert_male.style-study.md), [Desert female](tier-1-style/peoples-and-creatures/people.human_desert_female.style-study.md), [Cold-region male](tier-1-style/peoples-and-creatures/people.human_cold_male.style-study.md), and [Cold-region female](tier-1-style/peoples-and-creatures/people.human_cold_female.style-study.md). Carry forward Greek/Roman-inspired pirate maritime clothing with leather boots, Persian-inspired desert with deep-brown complexions, and Viking-inspired winter clothing with rich dyed textiles, fur, decorated brooches and runic-style embroidery. The cold-region male has a blue tunic and red full-length cloak, replacing the previous grey outfit; the female has an auburn crown braid, plum strap-overdress, red underdress, short blue cape, paired oval brooches and runic hem panels. The approved desert variants have distinct outfits: the male's indigo robe, saffron sash and wrapped headcloth; the female's copper-red wrap tunic, teal waistcoat, cream shawl, long braid and travel boots. Both desert variants share floral/geometric textile borders and refined metal fittings. Maritime, desert and cold-region filenames follow the `_male`/`_female` convention. These are individual styling examples, not gender-specific uniforms. Only the final approved images are retained.

Approved maritime variants share Greek/Roman-inspired draped linen, meander and wave borders, bronze fittings and pirate equipment. The female has a prominent red shoulder wrap, colored single-row leather skirt bands with a rising diagonal hem, and fitted boots. The male has a short sea-green vest, ivory draped tunic, terracotta borders, wine-red sash, bronze shoulder guard, chart case, trousers and cuffed boots. Preserve these distinct individual outfits; future architectural applications of their motifs remain exploratory.

Approved large-island human variants: [female](tier-1-style/peoples-and-creatures/people.human_large_island_female.style-study.md) and [male](tier-1-style/peoples-and-creatures/people.human_large_island_male.style-study.md). Shared language includes wrapped-and-sashed clothing, red and gold, partial lacquered armor, bamboo fittings and wooden clogs. The female has pinned hair and a long red wrap with separate shoulder, forearm and hip armor; the male has cropped hair, a short gold jacket, charcoal trousers, black lacquer chest and shin armor, asymmetric upper-arm protection and a document case. These are distinct individual styling examples, not gender-specific uniforms. Only approved images are retained.

Approved hill dwarf variants: [female](tier-1-style/peoples-and-creatures/people.hill_dwarf_female.style-study.md) and [male](tier-1-style/peoples-and-creatures/people.hill_dwarf_male.style-study.md). Shared direction includes lighter dwarven builds, softer faces, freckles, practical plant-dyed clothing and herb satchels. The female has a copper braid, green tunic, ochre sash and apron; the male has short sun-lightened brown hair, a cropped beard, ochre short-sleeved tunic, indigo neckerchief, green trousers and a canvas/hide collecting bag. These are distinct individual styling examples, not gender-specific uniforms. Both are stored with `_female`/`_male` suffixes; only approved images are retained.

Approved frosthold dwarf variants: [male](tier-1-style/peoples-and-creatures/people.frosthold_dwarf_male.style-study.md) and [female](tier-1-style/peoples-and-creatures/people.frosthold_dwarf_female.style-study.md). Both share broad winter silhouettes, pale eyes, carved bone snow goggles, heavy hide/fur layers and practical armor, combining Scottish-inspired textiles with Inuit-inspired winter construction. The male has white-blond braids, a crimson-and-teal tartan sash and matching scarf with teal face markings. The female has a crown braid with a white streak, ivory wrap-parka, plum-and-ochre tartan shoulder wrap, overlapping skirt armor and plum face markings. Preserve their distinct garments and palettes. Use fashion choices to inform later cultural and architectural studies without treating those unmade studies as approved designs. Only the approved `_male`/`_female` images are retained.

Approved rainforest human variants: [male](tier-1-style/peoples-and-creatures/people.human_rainforest_male.style-study.md) and [female](tier-1-style/peoples-and-creatures/people.human_rainforest_female.style-study.md). Shared language includes long black hair, Aztec-inspired stepped textile borders, plain woven cloth, turquoise inlays and light gold armor. The male has loose straight hair, a headband, turquoise draping secured with a flat clasp, a red waist wrap and gold shoulder/forearm armor. The female has a long braid, square-neck ivory top, teal wrapped skirt, ochre sash, satchel and small gold chest, forearm and shin guards. These are distinct individual styling examples, not gender-specific uniforms. Only approved images are retained; the padded-cotton preview is superseded.

Character style checkpoint: all twelve authored peoples now have owner-approved first-pass character references across these sheets. This does not complete the broader civilization comparison (including architecture and materials at one scale), the overall art bible or the production asset checklists.

- [ ] Overall art bible: visual pillars, level of realism, line/brush language, edge treatment, value range, palette, texture density, lighting, scale, and accessibility/readability rules.
- [ ] World overview key art: spherical planet, continents/islands, sea and atmosphere, tectonic relief, and a recognizable silhouette at atlas scale.
- [ ] Environment key art for ocean/coast, drylands, temperate lowlands, deep forest, rainforest, wetlands, mountains, boreal forest, tundra, ice, rivers, and lakes.
- [ ] Climate/season script: spring, summer, autumn, winter, drought, flood, storm, and day/night visual rules.
- [ ] Magic direction sheet showing all twelve schools together, including ordinary, radiant, corrupt, hidden, and blended manifestations.
- [ ] Civilization comparison sheet: the twelve authored peoples at one scale, with silhouette, materials, architecture, clothing, and color languages.
- [ ] Character/creature direction sheet: human and nonhuman anatomy, heroic/villainous shapes, mundane wildlife, giant threats, and supernatural beings.
- [ ] Settlement key art: hamlet, small city, medium city, capital, harbour, mountain hold, woodland settlement, and castle in their landscapes. Approved first Heartland village direction: [whole-village concept](tier-1-style/settlements-and-locations/people.human_heartland.village.style-study.md), with timber-and-plaster cottages, a village green, riverside mill, stone bridge, inn, forge and surrounding farmland. Individual building breakdowns are the next stage; this reference does not complete the wider settlement set.
- [ ] Landmark key art: ruins, sacred place, wonder, lair, subterranean site, and drowned site; show intact and age-worn states. [Approved Heartland ruins direction](tier-1-style/settlements-and-locations/biome.grassland.heartland-ruins.style-study.md) establishes reclaimed stone ruins within the grassland; other landmark families and intact comparisons remain open.
- [ ] Society/story key art: market life, travel, war, refuge, religious practice, expedition, quest, and corruption/cleansing.
- [ ] Cartography/UI direction sheet: globe, atlas, regional map, local plan, symbols, labels, information hierarchy, and color-blind-safe status coding.
- [ ] Production scale guide: person, creature, door, building, street, settlement, landmark, and terrain reference silhouettes.

Approved elf variants: [male](tier-1-style/peoples-and-creatures/people.elf_male.style-study.md) and [female](tier-1-style/peoples-and-creatures/people.elf_female.style-study.md). Both establish high elf culture through tailored robes, geometric metalwork, crystal fittings and arcane scholarship. The male has silver hair, indigo robes and a clean metal circlet; the female adds a dark braided updo, teal-and-ivory layers, asymmetric mantle and arcane measuring instrument. This approved arcane direction replaces woodland styling. Future variants should broaden hairstyles, clothing silhouettes, palettes and magical equipment within this direction.

Approved core dwarf variants: the [male](tier-1-style/peoples-and-creatures/people.dwarf_male.style-study.md) is a younger chestnut-haired adult in substantial angular plate-and-mail armor; the [female smith](tier-1-style/peoples-and-creatures/people.dwarf_female.style-study.md) is clean-shaven with black braided hair, burgundy rolled sleeves, a heavy leather apron and practical forging tools. Both retain compact powerful anatomy and substantial crafted materials, while offering distinct occupations and silhouettes. Preserve the male's layered wearable plate construction and the female's workshop clothing. Avoid oversized power-armor blocks and noisy hammered textures. The female's no-beard direction is owner-approved; neither outfit defines an entire sex or people.

## Tier 2 — Assembled concepts

### World, terrain, water, and ecology

- [ ] Planet/globe presentation with sea level, landmasses, clouds, polar caps, and atmospheric views.
- [ ] Terrain assemblies: continental shelf, beach, cliff, dune, grassland, forest, jungle, marsh, bog, riverbank, canyon, foothill, ridge, mountain, glacier, volcanic field, cave entrance, and island chains.
- [ ] Water assemblies: open ocean, coastal shallows, reef, lake, river source/course/mouth, delta, waterfall, wetland, flooded ground, and ice-covered water.
- [ ] Ecology assemblies: tree line, canopy/understory, meadow, crop/forage land, animal habitat, herd route, nest habitat, and resource site.
- [ ] Geological and history overlays: fault/plate boundary, uplift, eroded face, crater, ancient relic terrain, ruins, scars, and reclaimed ground.
- [ ] Weather and time-of-day scene variants: clear, rain, snow, fog, heat, wind, storm, dawn, noon, dusk, and night.

**Base biome identities** — one complete establishing scene per reachable authored biome in [biomes.json](../generator/data/biomes.json). `tundra` (1) and `snow` (6) are tombstoned in that catalogue and are intentionally not commissioned.

<!-- BEGIN:BASE_BIOMES -->
- [ ] `biome.ocean` — base biome ID 0; approved summer [coastal base](tier-1-style/world-and-environments/biome.ocean.style-study.md), [harbor ruins](tier-1-style/settlements-and-locations/biome.ocean.ruins.style-study.md), [basalt-coast storm](tier-1-style/world-and-environments/biome.ocean.storm.style-study.md), and independent [winter shingle coast](tier-1-style/world-and-environments/biome.ocean.winter.style-study.md). Winter depicts a cold-temperate coast with mostly unfrozen sea. Production breakdown remains open.
- [ ] `biome.desert` — base biome ID 2; approved summer [basin base](tier-1-style/world-and-environments/biome.desert.style-study.md), [caravan courtyard ruins](tier-1-style/settlements-and-locations/biome.desert.ruins.style-study.md), [playa dust storm](tier-1-style/world-and-environments/biome.desert.storm.style-study.md), and independent [cold-desert winter](tier-1-style/world-and-environments/biome.desert.winter.style-study.md). Winter depicts a local cold high-desert example with patchy snow. Production breakdown remains open.
- [ ] `biome.grassland` — base biome ID 3; approved summer [base landscape](tier-1-style/world-and-environments/biome.grassland.style-study.md), [Heartland ruins](tier-1-style/settlements-and-locations/biome.grassland.heartland-ruins.style-study.md), [storm scene](tier-1-style/world-and-environments/biome.grassland.storm.style-study.md), and separate [winter landscape](tier-1-style/world-and-environments/biome.grassland.winter.style-study.md). Production breakdown remains open.
- [ ] `biome.forest` — base biome ID 4; approved summer [base forest](tier-1-style/world-and-environments/biome.forest.style-study.md), [independent ravine ruins](tier-1-style/settlements-and-locations/biome.forest.ruins.style-study.md), [storm scene](tier-1-style/world-and-environments/biome.forest.storm.style-study.md), and separate [winter landscape](tier-1-style/world-and-environments/biome.forest.winter.style-study.md). Production breakdown remains open.
- [ ] `biome.exposed_rock` — base biome ID 5; approved summer [natural plateau](tier-1-style/world-and-environments/biome.exposed_rock.style-study.md), [cliffside ruins](tier-1-style/settlements-and-locations/biome.exposed_rock.ruins.style-study.md), [uphill storm scene](tier-1-style/world-and-environments/biome.exposed_rock.storm.style-study.md), and separate [winter landscape](tier-1-style/world-and-environments/biome.exposed_rock.winter.style-study.md), each with distinct terrain and viewpoints. Production breakdown remains open.
- [ ] `biome.rainforest` — base biome ID 7; approved summer [understory base](tier-1-style/world-and-environments/biome.rainforest.style-study.md), [ruined courtyard](tier-1-style/settlements-and-locations/biome.rainforest.ruins.style-study.md), [canopy storm](tier-1-style/world-and-environments/biome.rainforest.storm.style-study.md), and independent [winter streambank](tier-1-style/world-and-environments/biome.rainforest.winter.style-study.md). Winter remains tropical and evergreen in this approved local seasonal example. Production breakdown remains open.
- [ ] `biome.lake` — base biome ID 8; approved summer [shoreline base](tier-1-style/world-and-environments/biome.lake.style-study.md), [island ruins](tier-1-style/settlements-and-locations/biome.lake.ruins.style-study.md), [headland storm](tier-1-style/world-and-environments/biome.lake.storm.style-study.md), and independent [winter inlet](tier-1-style/world-and-environments/biome.lake.winter.style-study.md). Winter depicts a cold-temperate lake example. Production breakdown remains open.
- [ ] `biome.marsh` — base biome ID 13; approved summer [wetland base](tier-1-style/world-and-environments/biome.marsh.style-study.md), [sluice-house ruins](tier-1-style/settlements-and-locations/biome.marsh.ruins.style-study.md), [water-level storm](tier-1-style/world-and-environments/biome.marsh.storm.style-study.md), and independent [winter channels](tier-1-style/world-and-environments/biome.marsh.winter.style-study.md). Winter depicts a cold-temperate marsh example. Production breakdown remains open.
- [ ] `biome.boreal_forest` — base biome ID 15; approved summer [mossy forest base](tier-1-style/world-and-environments/biome.boreal_forest.style-study.md), [ridge waystation ruins](tier-1-style/settlements-and-locations/biome.boreal_forest.ruins.style-study.md), [ravine storm](tier-1-style/world-and-environments/biome.boreal_forest.storm.style-study.md), and independent [winter creek](tier-1-style/world-and-environments/biome.boreal_forest.winter.style-study.md). Production breakdown remains open.
- [ ] `biome.cold_tundra` — base biome ID 16; approved summer [plateau base](tier-1-style/world-and-environments/biome.cold_tundra.style-study.md), [turf-and-stone ruins](tier-1-style/settlements-and-locations/biome.cold_tundra.ruins.style-study.md), [sleet storm](tier-1-style/world-and-environments/biome.cold_tundra.storm.style-study.md), and independent [winter ravine](tier-1-style/world-and-environments/biome.cold_tundra.winter.style-study.md). Production breakdown remains open.
- [ ] `biome.land_ice` — base biome ID 17; approved summer [glacier base](tier-1-style/world-and-environments/biome.land_ice.style-study.md), [refuge ruins](tier-1-style/settlements-and-locations/biome.land_ice.ruins.style-study.md), [plateau blizzard](tier-1-style/world-and-environments/biome.land_ice.storm.style-study.md), and independent [winter icefall](tier-1-style/world-and-environments/biome.land_ice.winter.style-study.md). Summer remains glaciated; winter emphasizes snow accumulation. Production breakdown remains open.
<!-- END:BASE_BIOMES -->

**Magic-biome variants** — one complete scene for each reachable biome and school combination. The authored names below come from [biomes.json](../generator/data/biomes.json); names for the two tombstoned biome slots are excluded.

<!-- BEGIN:BIOME_VARIANTS -->
#### Weave

- [ ] `biome.ocean.weave` — Prismatic seas; approved [summer surf](tier-1-style/world-and-environments/biome.ocean.weave.style-study.md), [drowned wizard lighthouse](tier-1-style/settlements-and-locations/biome.ocean.weave.ruins.style-study.md), [storm](tier-1-style/world-and-environments/biome.ocean.weave.storm.style-study.md), [winter coast](tier-1-style/world-and-environments/biome.ocean.weave.winter.style-study.md), and supplementary [living-magic reef](tier-1-style/world-and-environments/biome.ocean.weave.reef.style-study.md). Direction: intense spectral water, opalescent stone and interwoven prismatic skies; reef colonies mix saturated solid coral with translucent raw living magic. Production breakdown remains open.
- [ ] `biome.desert.weave` — Glass mirages; approved [summer glass dunes](tier-1-style/world-and-environments/biome.desert.weave.style-study.md), [caravanserai and lens-observatory ruins](tier-1-style/settlements-and-locations/biome.desert.weave.ruins.style-study.md), [opalescent sandstorm](tier-1-style/world-and-environments/biome.desert.weave.storm.style-study.md), and [winter ravine](tier-1-style/world-and-environments/biome.desert.weave.winter.style-study.md). Direction: pervasive opalescent glass sand, translucent dune crests, tangible mirages and interwoven prismatic skies. Production breakdown remains open.
- [ ] `biome.grassland.weave` — Possibility meadows; approved [summer meadow](tier-1-style/world-and-environments/biome.grassland.weave.style-study.md), [wizard's hall and seed-garden ruins](tier-1-style/settlements-and-locations/biome.grassland.weave.ruins.style-study.md), [windswept storm](tier-1-style/world-and-environments/biome.grassland.weave.storm.style-study.md), and [winter meadow](tier-1-style/world-and-environments/biome.grassland.weave.winter.style-study.md). Direction: physical meadow plants entwined with living-magic growth, overlapping possible forms and interwoven prismatic skies. Production breakdown remains open.
- [ ] `biome.forest.weave` — Living storywoods; approved [summer grove](tier-1-style/world-and-environments/biome.forest.weave.style-study.md), [woodland amphitheatre ruins](tier-1-style/settlements-and-locations/biome.forest.weave.ruins.style-study.md), [storm ravine](tier-1-style/world-and-environments/biome.forest.weave.storm.style-study.md), and [winter hollow](tier-1-style/world-and-environments/biome.forest.weave.winter.style-study.md). Direction: luminous growth rings and saplight, living-magic wood and foliage, alternate branching forms and spectral summer canopies beneath woven prismatic skies. Production breakdown remains open.
- [ ] `biome.exposed_rock.weave` — Floating stonefields; approved [summer uplands](tier-1-style/world-and-environments/biome.exposed_rock.weave.style-study.md), [broken mountain-crossing ruins](tier-1-style/settlements-and-locations/biome.exposed_rock.weave.ruins.style-study.md), [storm escarpment](tier-1-style/world-and-environments/biome.exposed_rock.weave.storm.style-study.md), and [winter shelves](tier-1-style/world-and-environments/biome.exposed_rock.weave.winter.style-study.md). Direction: heavy suspended stone masses, opalescent fracture interiors, refractive magic across open gaps and woven prismatic skies. Production breakdown remains open.
- [ ] `biome.rainforest.weave` — Everchanging canopy; approved [summer treetops](tier-1-style/world-and-environments/biome.rainforest.weave.style-study.md), [root-swallowed sanctuary ruins](tier-1-style/settlements-and-locations/biome.rainforest.weave.ruins.style-study.md), [storm above the river](tier-1-style/world-and-environments/biome.rainforest.weave.storm.style-study.md), and [tropical winter hollow](tier-1-style/world-and-environments/biome.rainforest.weave.winter.style-study.md). Direction: changing spectral leaf forms, living-magic vines and epiphytes, layered tropical growth and woven prismatic skies; winter stays evergreen without snow or ice. Production breakdown remains open.
- [ ] `biome.lake.weave` — Starlight lakes; approved [summer shallows](tier-1-style/world-and-environments/biome.lake.weave.style-study.md), [drowned stargazer's pavilion](tier-1-style/settlements-and-locations/biome.lake.weave.ruins.style-study.md), [storm-churned starlight](tier-1-style/world-and-environments/biome.lake.weave.storm.style-study.md), and [winter ice](tier-1-style/world-and-environments/biome.lake.weave.winter.style-study.md). Direction: dense starlight within freshwater, luminous currents and aquatic plants, opalescent shore stones and woven prismatic skies. Production breakdown remains open.
- [ ] `biome.marsh.weave` — Spellmist marsh; approved [summer reed beds](tier-1-style/world-and-environments/biome.marsh.weave.style-study.md), [sunken fen workshop](tier-1-style/settlements-and-locations/biome.marsh.weave.ruins.style-study.md), [wind-torn spellmist](tier-1-style/world-and-environments/biome.marsh.weave.storm.style-study.md), and [winter pools](tier-1-style/world-and-environments/biome.marsh.weave.winter.style-study.md). Direction: luminous low mist condensing into fleeting plant forms among physical reeds, shallow pools and peat hummocks beneath woven prismatic skies. Production breakdown remains open.
- [ ] `biome.boreal_forest.weave` — Aurora woods; approved fresh [summer moss terraces](tier-1-style/world-and-environments/biome.boreal_forest.weave.style-study.md), [rooted watchkeep ruins](tier-1-style/settlements-and-locations/biome.boreal_forest.weave.ruins.style-study.md), [storm through the pines](tier-1-style/world-and-environments/biome.boreal_forest.weave.storm.style-study.md), and [infused winter snow](tier-1-style/world-and-environments/biome.boreal_forest.weave.winter.style-study.md). Direction: aurora woven through conifers, living mutant moss with plush folded growth and luminous tips, and snow lit from within; clean painted forms from fresh generations. Production breakdown remains open.
- [ ] `biome.cold_tundra.weave` — Shifting frostlands — approved Tier 1 [summer](tier-1-style/world-and-environments/biome.cold_tundra.weave.style-study.md), [ruins](tier-1-style/settlements-and-locations/biome.cold_tundra.weave.ruins.style-study.md), [storm](tier-1-style/world-and-environments/biome.cold_tundra.weave.storm.style-study.md), [winter](tier-1-style/world-and-environments/biome.cold_tundra.weave.winter.style-study.md); production assets pending
- [ ] `biome.land_ice.weave` — Singing glaciers — approved Tier 1 [summer](tier-1-style/world-and-environments/biome.land_ice.weave.style-study.md), [ruins](tier-1-style/settlements-and-locations/biome.land_ice.weave.ruins.style-study.md), [storm](tier-1-style/world-and-environments/biome.land_ice.weave.storm.style-study.md), [winter](tier-1-style/world-and-environments/biome.land_ice.weave.winter.style-study.md); production assets pending

#### Umbral

- [ ] `biome.ocean.umbral` — Silent seas
- [ ] `biome.desert.umbral` — Haunted tombs
- [ ] `biome.grassland.umbral` — Ashen meadows
- [ ] `biome.forest.umbral` — Mourning woods
- [ ] `biome.exposed_rock.umbral` — Ossuary crags
- [ ] `biome.rainforest.umbral` — Withering canopy
- [ ] `biome.lake.umbral` — Stillwater tombs
- [ ] `biome.marsh.umbral` — Haunted marsh
- [ ] `biome.boreal_forest.umbral` — Ghost pines
- [ ] `biome.cold_tundra.umbral` — Deathfrost plains
- [ ] `biome.land_ice.umbral` — Sepulchral ice

#### Infernal

- [ ] `biome.ocean.infernal` — Brimstone seas
- [ ] `biome.desert.infernal` — Hellglass wastes
- [ ] `biome.grassland.infernal` — Cinder blight
- [ ] `biome.forest.infernal` — Thornhell woods
- [ ] `biome.exposed_rock.infernal` — Demon spires
- [ ] `biome.rainforest.infernal` — Devouring jungle
- [ ] `biome.lake.infernal` — Bloodglass lakes
- [ ] `biome.marsh.infernal` — Corruption mire
- [ ] `biome.boreal_forest.infernal` — Charred blackwoods
- [ ] `biome.cold_tundra.infernal` — Torment barrens
- [ ] `biome.land_ice.infernal` — Infernal glaciers

#### Radiant

- [ ] `biome.ocean.radiant` — Dawn seas
- [ ] `biome.desert.radiant` — Golden sanctuaries
- [ ] `biome.grassland.radiant` — Healing meadows
- [ ] `biome.forest.radiant` — Sanctuary woods
- [ ] `biome.exposed_rock.radiant` — Hallowed heights
- [ ] `biome.rainforest.radiant` — Mercy gardens
- [ ] `biome.lake.radiant` — Lustral lakes
- [ ] `biome.marsh.radiant` — Purifying marsh
- [ ] `biome.boreal_forest.radiant` — Dawnlit pines
- [ ] `biome.cold_tundra.radiant` — Peaceful frostlands
- [ ] `biome.land_ice.radiant` — Cathedral ice

#### Fire

- [ ] `biome.ocean.fire` — Steam seas
- [ ] `biome.desert.fire` — Furnace dunes
- [ ] `biome.grassland.fire` — Flamegrass plains
- [ ] `biome.forest.fire` — Phoenix woods
- [ ] `biome.exposed_rock.fire` — Molten crags
- [ ] `biome.rainforest.fire` — Emberbloom jungle
- [ ] `biome.lake.fire` — Boiling lakes
- [ ] `biome.marsh.fire` — Cinder mire
- [ ] `biome.boreal_forest.fire` — Firecone woods
- [ ] `biome.cold_tundra.fire` — Ashfrost plains
- [ ] `biome.land_ice.fire` — Steamcut glaciers

#### Water

- [ ] `biome.ocean.water` — Crystal currents
- [ ] `biome.desert.water` — Frostglass dunes
- [ ] `biome.grassland.water` — Dew meadows
- [ ] `biome.forest.water` — Rainveil woods
- [ ] `biome.exposed_rock.water` — Springstone cliffs
- [ ] `biome.rainforest.water` — Deluge jungle
- [ ] `biome.lake.water` — Winterglass lakes
- [ ] `biome.marsh.water` — Tidal gardens
- [ ] `biome.boreal_forest.water` — Mistbound pines
- [ ] `biome.cold_tundra.water` — Bluefrost tundra
- [ ] `biome.land_ice.water` — Everflow glaciers

#### Earth

- [ ] `biome.ocean.earth` — Rootreef seas
- [ ] `biome.desert.earth` — Blooming dunes
- [ ] `biome.grassland.earth` — Titan meadows
- [ ] `biome.forest.earth` — Ancient rootwoods
- [ ] `biome.exposed_rock.earth` — Living monoliths
- [ ] `biome.rainforest.earth` — Colossal jungle
- [ ] `biome.lake.earth` — Rootcradle lakes
- [ ] `biome.marsh.earth` — Deep-root marsh
- [ ] `biome.boreal_forest.earth` — Ironbark taiga
- [ ] `biome.cold_tundra.earth` — Lichenstone plains
- [ ] `biome.land_ice.earth` — Rootsplit glaciers

#### Air

- [ ] `biome.ocean.air` — Storm seas
- [ ] `biome.desert.air` — Whistling dunes
- [ ] `biome.grassland.air` — Thundergrass plains
- [ ] `biome.forest.air` — Skyreach woods
- [ ] `biome.exposed_rock.air` — Windcarved spires
- [ ] `biome.rainforest.air` — Stormcrown jungle
- [ ] `biome.lake.air` — Suspended mist lakes
- [ ] `biome.marsh.air` — Cloudveil marsh
- [ ] `biome.boreal_forest.air` — Singing pines
- [ ] `biome.cold_tundra.air` — Force-swept tundra
- [ ] `biome.land_ice.air` — Howling glaciers

#### Blood

- [ ] `biome.ocean.blood` — Arterial tides
- [ ] `biome.desert.blood` — Scab flats
- [ ] `biome.grassland.blood` — Redgrass steppe
- [ ] `biome.forest.blood` — Vein-root woods
- [ ] `biome.exposed_rock.blood` — Marrow spires
- [ ] `biome.rainforest.blood` — Pulsing canopy
- [ ] `biome.lake.blood` — Clotted meres
- [ ] `biome.marsh.blood` — Ichor fen
- [ ] `biome.boreal_forest.blood` — Hollow-bone taiga
- [ ] `biome.cold_tundra.blood` — Frozen carrion plains
- [ ] `biome.land_ice.blood` — Bloodglass ice

#### Void

- [ ] `biome.ocean.void` — Unlit deeps
- [ ] `biome.desert.void` — Ashless waste
- [ ] `biome.grassland.void` — Muted plains
- [ ] `biome.forest.void` — Soundless woods
- [ ] `biome.exposed_rock.void` — Unmade crags
- [ ] `biome.rainforest.void` — Swallowed canopy
- [ ] `biome.lake.void` — Starless waters
- [ ] `biome.marsh.void` — Forgetting mire
- [ ] `biome.boreal_forest.void` — Null pines
- [ ] `biome.cold_tundra.void` — Emptied frostlands
- [ ] `biome.land_ice.void` — Absence glaciers

#### Rot

- [ ] `biome.ocean.rot` — Septic seas
- [ ] `biome.desert.rot` — Spore dunes
- [ ] `biome.grassland.rot` — Blightgrass meadows
- [ ] `biome.forest.rot` — Fruiting deadwoods
- [ ] `biome.exposed_rock.rot` — Mouldered stone
- [ ] `biome.rainforest.rot` — Putrid canopy
- [ ] `biome.lake.rot` — Stagnant pools
- [ ] `biome.marsh.rot` — Plague fen
- [ ] `biome.boreal_forest.rot` — Softwood taiga
- [ ] `biome.cold_tundra.rot` — Preserved dead plains
- [ ] `biome.land_ice.rot` — Weeping glaciers

#### Eldritch

- [ ] `biome.ocean.eldritch` — Watching seas
- [ ] `biome.desert.eldritch` — Impossible dunes
- [ ] `biome.grassland.eldritch` — Spiral meadows
- [ ] `biome.forest.eldritch` — Unmeeting woods
- [ ] `biome.exposed_rock.eldritch` — Wrong-angle crags
- [ ] `biome.rainforest.eldritch` — Writhing canopy
- [ ] `biome.lake.eldritch` — Fathomless eyes
- [ ] `biome.marsh.eldritch` — Murmuring mire
- [ ] `biome.boreal_forest.eldritch` — Listening pines
- [ ] `biome.cold_tundra.eldritch` — Dreaming frostlands
- [ ] `biome.land_ice.eldritch` — Sleeper ice
<!-- END:BIOME_VARIANTS -->

### Peoples, characters, and society

- [ ] Character turnaround template: front/side/back, scale, expression, palette, equipment layers, and age/body variation.
- [ ] Generic roles: resident, farmer, craftsperson, trader, sailor, guard, soldier, ruler, priest, mage, scholar, healer, refugee, and traveller.
- [ ] Life and state variants: child/adult/elder, work/formal/travel/combat dress, healthy/injured, living/dead, heroic/fallen/corrupted/redeemed.
- [ ] NPC post visual language for the authored building roles in [npc-posts.json](../generator/data/npc-posts.json).
- [ ] Six nomad band assemblies: survivors, deserters, cultists, merchants, bandits, and wanderers; each includes members, transport, camp, and carried goods.
- [ ] Hero party and villain faction lineup, with relationships readable through clothing, insignia, and pose.

**Authored peoples** — complete representative character and culture sheets from [heritage-appearance.json](../generator/data/heritage-appearance.json) and [civilizations.json](../generator/data/civilizations.json). `mixed` is a population configuration, not a separate people.

<!-- BEGIN:PEOPLES -->
- [ ] `people.human_heartland`
- [ ] `people.elf`
- [ ] `people.dwarf`
- [ ] `people.human_maritime`
- [ ] `people.human_desert`
- [ ] `people.human_cold`
- [ ] `people.human_large_island`
- [ ] `people.human_rainforest`
- [ ] `people.tidekin`
- [ ] `people.gnome`
- [ ] `people.hill_dwarf`
- [ ] `people.frosthold_dwarf`
<!-- END:PEOPLES -->

**Hero/villain archetype cards** — from [hero-archetypes.json](../generator/data/hero-archetypes.json). These are visual storytelling templates; a generated individual may combine them.

<!-- BEGIN:HERO_ARCHETYPES -->
- [ ] `archetype.conqueror` — The Conqueror
- [ ] `archetype.exiled_hero` — The Exiled Hero
- [ ] `archetype.oathbound` — The Oathbound
- [ ] `archetype.inherited_hero` — The Inherited Hero
- [ ] `archetype.false_hero` — The False Hero
- [ ] `archetype.redeemed` — The Redeemed
- [ ] `archetype.tainted_one` — The Tainted One
- [ ] `archetype.forgiving` — The Forgiving
- [ ] `archetype.tyrant` — The Tyrant
- [ ] `archetype.mastermind` — The Mastermind
- [ ] `archetype.corrupt` — The Corrupt
- [ ] `archetype.beast` — The Beast
- [ ] `archetype.incarnate` — The Incarnate
- [ ] `archetype.broken_soul` — The Broken Soul
- [ ] `archetype.fractured_heart` — The Fractured Heart
- [ ] `archetype.malevolent_benevolent` — The Malevolent / Benevolent
- [ ] `archetype.deceiver` — The Deceiver
- [ ] `archetype.mentor` — The Mentor
- [ ] `archetype.trickster` — The Trickster
- [ ] `archetype.zealot` — The Zealot
- [ ] `archetype.reluctant_hero` — The Reluctant Hero
- [ ] `archetype.fallen_hero` — The Fallen Hero
- [ ] `archetype.kingmaker` — The Kingmaker
- [ ] `archetype.puppet_sovereign` — The Puppet Sovereign
- [ ] `archetype.rebel` — The Rebel
- [ ] `archetype.knight_errant` — The Knight Errant
- [ ] `archetype.magnate` — The Magnate
- [ ] `archetype.hermit_sage` — The Hermit Sage
- [ ] `archetype.obsessed_hunter` — The Obsessed Hunter
- [ ] `archetype.turncoat` — The Turncoat
- [ ] `archetype.warden` — The Warden
- [ ] `archetype.ancient` — The Ancient
<!-- END:HERO_ARCHETYPES -->

### Creatures and encounters

- [ ] Creature presentation template: silhouette, scale against a person, ecology, behavior, movement, color, and threat/temperament cues.
- [ ] Encounter compositions: solitary threat, pack, swarm, herd, predator/prey, nest defense, wandering band, ambush, and settlement visitation.
- [ ] Nest/lair state sheets: occupied, abandoned, destroyed, expanding, and magic-affected.

**Authored creature and fauna profiles** — all 680 profile IDs from [nest-profiles.json](../generator/data/nest-profiles.json). Each checkbox calls for one assembled design; multiple profiles may intentionally share Tier 3 anatomy or materials.

<!-- BEGIN:CREATURES -->
#### A

- [ ] `creature.aardvark`
- [ ] `creature.absence-titans`
- [ ] `creature.abyssal-choir-worms`
- [ ] `creature.abyssal-krakens`
- [ ] `creature.acorn-folk`
- [ ] `creature.addax`
- [ ] `creature.aegis-rams`
- [ ] `creature.african-buffalo`
- [ ] `creature.african-elephant`
- [ ] `creature.african-wild-dog`
- [ ] `creature.alms-hounds`
- [ ] `creature.alpine-ibex`
- [ ] `creature.alpine-marmot`
- [ ] `creature.altar-beasts`
- [ ] `creature.american-alligator`
- [ ] `creature.american-bison`
- [ ] `creature.american-black-bear`
- [ ] `creature.american-mink`
- [ ] `creature.american-pika`
- [ ] `creature.amphipteres`
- [ ] `creature.anchor-eels`
- [ ] `creature.anglerfish`
- [ ] `creature.antlered-hares`
- [ ] `creature.arabian-oryx`
- [ ] `creature.arctic-fox`
- [ ] `creature.arctic-hare`
- [ ] `creature.armored-river-crabs`
- [ ] `creature.ash-crows`
- [ ] `creature.ash-drakes`
- [ ] `creature.ash-scavengers`
- [ ] `creature.asian-elephant`
- [ ] `creature.atlantic-bluefin-tuna`
- [ ] `creature.atlantic-cod`
- [ ] `creature.atlantic-herring`
- [ ] `creature.atlantic-mackerel`
- [ ] `creature.atlantic-puffin`
- [ ] `creature.atlantic-salmon`
- [ ] `creature.avalanche-giants`

#### B

- [ ] `creature.baird-s-tapir`
- [ ] `creature.banded-mongoose`
- [ ] `creature.banded-sea-krait`
- [ ] `creature.bank-vole`
- [ ] `creature.banshees`
- [ ] `creature.barghests`
- [ ] `creature.barnacle-dead`
- [ ] `creature.barrow-rats`
- [ ] `creature.barrow-wight`
- [ ] `creature.basalt-guardians`
- [ ] `creature.basilisk-drakes`
- [ ] `creature.basilisks`
- [ ] `creature.basking-shark`
- [ ] `creature.bat-eared-fox`
- [ ] `creature.beacon-jellies`
- [ ] `creature.bearded-vulture`
- [ ] `creature.behemoths`
- [ ] `creature.bell-hares`
- [ ] `creature.bell-mouthed-mourners`
- [ ] `creature.bighorn-sheep`
- [ ] `creature.bile-hounds`
- [ ] `creature.bilge-worms`
- [ ] `creature.biting-pixies`
- [ ] `creature.black-antlered-rams`
- [ ] `creature.black-mamba`
- [ ] `creature.black-rat`
- [ ] `creature.black-rhinoceros`
- [ ] `creature.black-tailed-jackrabbit`
- [ ] `creature.black-tailed-prairie-dog`
- [ ] `creature.bladefish`
- [ ] `creature.blight-flies`
- [ ] `creature.blink-hares`
- [ ] `creature.blood-barons`
- [ ] `creature.blood-ticks`
- [ ] `creature.blue-mussel`
- [ ] `creature.blue-ringed-octopus`
- [ ] `creature.blue-shark`
- [ ] `creature.blue-whale`
- [ ] `creature.blue-wildebeest`
- [ ] `creature.boa-constrictor`
- [ ] `creature.bobcat`
- [ ] `creature.bog-wyrmlings`
- [ ] `creature.boggarts`
- [ ] `creature.bone-amalgams`
- [ ] `creature.bone-crows`
- [ ] `creature.bone-gulls`
- [ ] `creature.bottlenose-dolphin`
- [ ] `creature.bramble-hounds`
- [ ] `creature.briar-duelists`
- [ ] `creature.briar-goblins`
- [ ] `creature.briar-trolls`
- [ ] `creature.brimstone-bats`
- [ ] `creature.brimstone-raiders`
- [ ] `creature.brine-revenants`
- [ ] `creature.brine-sprites`
- [ ] `creature.brood-mothers`
- [ ] `creature.brood-surgeons`
- [ ] `creature.brown-bear`
- [ ] `creature.brown-rat`
- [ ] `creature.brown-throated-sloth`
- [ ] `creature.brown-trout`
- [ ] `creature.brownies`
- [ ] `creature.bull-shark`
- [ ] `creature.bullfrog`
- [ ] `creature.burrow-drakes`
- [ ] `creature.burrow-hulks`
- [ ] `creature.burrowing-owl`

#### C

- [ ] `creature.cairn-wolves`
- [ ] `creature.candle-worms`
- [ ] `creature.capercaillie`
- [ ] `creature.capybara`
- [ ] `creature.caracal`
- [ ] `creature.caravan-jackals`
- [ ] `creature.carrion-flies`
- [ ] `creature.carrion-followers`
- [ ] `creature.cathedral-spiders`
- [ ] `creature.cave-bears`
- [ ] `creature.cellar-mouths`
- [ ] `creature.censer-scarabs`
- [ ] `creature.central-american-agouti`
- [ ] `creature.chain-hydras`
- [ ] `creature.chain-jailers`
- [ ] `creature.chamois`
- [ ] `creature.chancel-toads`
- [ ] `creature.chapel-mice`
- [ ] `creature.cheetah`
- [ ] `creature.chimeras`
- [ ] `creature.chimpanzee`
- [ ] `creature.choir-brains`
- [ ] `creature.choir-constructs`
- [ ] `creature.cinder-centaurs`
- [ ] `creature.cinder-dragons`
- [ ] `creature.cinder-hoppers`
- [ ] `creature.cinder-jackals`
- [ ] `creature.cinder-lice`
- [ ] `creature.clay-elementals`
- [ ] `creature.clay-hounds`
- [ ] `creature.cliff-drakes`
- [ ] `creature.cloister-spiders`
- [ ] `creature.clouded-leopard`
- [ ] `creature.cockatrices`
- [ ] `creature.coffin-beetles`
- [ ] `creature.collared-lemming`
- [ ] `creature.collared-peccary`
- [ ] `creature.colossal-squid`
- [ ] `creature.common-brushtail-possum`
- [ ] `creature.common-carp`
- [ ] `creature.common-crane`
- [ ] `creature.common-cuttlefish`
- [ ] `creature.common-frog`
- [ ] `creature.common-genet`
- [ ] `creature.common-kingfisher`
- [ ] `creature.common-octopus`
- [ ] `creature.common-pheasant`
- [ ] `creature.common-pipistrelle`
- [ ] `creature.common-raven`
- [ ] `creature.common-roach`
- [ ] `creature.common-shrew`
- [ ] `creature.common-starfish`
- [ ] `creature.common-toad`
- [ ] `creature.common-warthog`
- [ ] `creature.common-wombat`
- [ ] `creature.conger-eel`
- [ ] `creature.contract-devils`
- [ ] `creature.corpse-bears`
- [ ] `creature.corpse-lights`
- [ ] `creature.corpse-moths`
- [ ] `creature.cougar`
- [ ] `creature.court-spiders`
- [ ] `creature.coyote`
- [ ] `creature.crested-porcupine`
- [ ] `creature.crimson-courts`
- [ ] `creature.crystal-dragons`
- [ ] `creature.curse-ticks`

#### D

- [ ] `creature.darkling-beetle`
- [ ] `creature.dawn-heralds`
- [ ] `creature.deep-sea-dragons`
- [ ] `creature.desecrated-bulls`
- [ ] `creature.desert-hairy-scorpion`
- [ ] `creature.desert-hedgehog`
- [ ] `creature.desert-locust`
- [ ] `creature.desert-tortoise`
- [ ] `creature.devouring-tide`
- [ ] `creature.dew-sprites`
- [ ] `creature.dhole`
- [ ] `creature.dingo`
- [ ] `creature.dire-wolves`
- [ ] `creature.dragon-turtles`
- [ ] `creature.dream-medusae`
- [ ] `creature.drift-polyps`
- [ ] `creature.dromedary-camel`
- [ ] `creature.drowned-dead`
- [ ] `creature.drowned-lanterns`
- [ ] `creature.drowned-titans`
- [ ] `creature.dryads`
- [ ] `creature.dune-hounds`
- [ ] `creature.dune-wyrms`
- [ ] `creature.dust-hoppers`
- [ ] `creature.dust-imps`
- [ ] `creature.dust-wisps`

#### E

- [ ] `creature.eastern-chipmunk`
- [ ] `creature.eastern-cottontail`
- [ ] `creature.eastern-gray-squirrel`
- [ ] `creature.echo-hunters`
- [ ] `creature.edible-crab`
- [ ] `creature.effigy-stalkers`
- [ ] `creature.elder-sky-serpents`
- [ ] `creature.elder-tree-wardens`
- [ ] `creature.electric-eel`
- [ ] `creature.elk`
- [ ] `creature.ember-geckos`
- [ ] `creature.ember-hounds`
- [ ] `creature.ember-imps`
- [ ] `creature.ember-newts`
- [ ] `creature.emberlings`
- [ ] `creature.emperor-dragonfly`
- [ ] `creature.emperor-penguin`
- [ ] `creature.emu`
- [ ] `creature.entropy-motes`
- [ ] `creature.eurasian-beaver`
- [ ] `creature.eurasian-bittern`
- [ ] `creature.eurasian-lynx`
- [ ] `creature.eurasian-otter`
- [ ] `creature.eurasian-oystercatcher`
- [ ] `creature.european-adder`
- [ ] `creature.european-badger`
- [ ] `creature.european-eel`
- [ ] `creature.european-flounder`
- [ ] `creature.european-hare`
- [ ] `creature.european-hedgehog`
- [ ] `creature.european-lobster`
- [ ] `creature.european-perch`
- [ ] `creature.european-polecat`
- [ ] `creature.european-pond-turtle`
- [ ] `creature.european-rabbit`
- [ ] `creature.european-sea-bass`
- [ ] `creature.european-sea-urchin`
- [ ] `creature.european-shag`
- [ ] `creature.european-water-vole`
- [ ] `creature.european-wildcat`
- [ ] `creature.exsanguinators`
- [ ] `creature.eyebud-clusters`
- [ ] `creature.eyeless-owls`
- [ ] `creature.eyestalk-shoals`

#### F

- [ ] `creature.fallow-deer`
- [ ] `creature.false-angels`
- [ ] `creature.fauns`
- [ ] `creature.fennec-fox`
- [ ] `creature.feral-cat`
- [ ] `creature.feral-dog`
- [ ] `creature.fire-salamander`
- [ ] `creature.flagellant-swarms`
- [ ] `creature.flame-elementals`
- [ ] `creature.flesh-amalgams`
- [ ] `creature.flesh-reefs`
- [ ] `creature.foam-sprites`
- [ ] `creature.folded-leviathans`
- [ ] `creature.font-salamanders`
- [ ] `creature.forest-dragons`
- [ ] `creature.foundry-mothers`
- [ ] `creature.frost-hulks`
- [ ] `creature.frost-owls`
- [ ] `creature.frost-sprites`
- [ ] `creature.funeral-knights`
- [ ] `creature.fungus-beetle`
- [ ] `creature.fungus-gnat`
- [ ] `creature.furnace-bulls`
- [ ] `creature.furnace-smiths`
- [ ] `creature.furnace-toads`

#### G

- [ ] `creature.gale-elementals`
- [ ] `creature.gate-colossi`
- [ ] `creature.gaur`
- [ ] `creature.geoffroy-s-spider-monkey`
- [ ] `creature.geometry-wraiths`
- [ ] `creature.geyser-serpents`
- [ ] `creature.ghost-galleons`
- [ ] `creature.ghouls`
- [ ] `creature.giant-anteater`
- [ ] `creature.giant-bats`
- [ ] `creature.giant-beetles`
- [ ] `creature.giant-centipedes`
- [ ] `creature.giant-eagles`
- [ ] `creature.giant-grouper`
- [ ] `creature.giant-leeches`
- [ ] `creature.giant-moray`
- [ ] `creature.giant-oceanic-manta-ray`
- [ ] `creature.giant-otter`
- [ ] `creature.giant-panda`
- [ ] `creature.giant-scorpions`
- [ ] `creature.giant-spiders`
- [ ] `creature.giant-squid`
- [ ] `creature.gibbon`
- [ ] `creature.gila-monster`
- [ ] `creature.giraffe`
- [ ] `creature.glacier-dragons`
- [ ] `creature.glacier-walkers`
- [ ] `creature.glass-larvae`
- [ ] `creature.glass-shrimp`
- [ ] `creature.glass-wing-dragonets`
- [ ] `creature.glasswing-moths`
- [ ] `creature.glimmer-thieves`
- [ ] `creature.glow-snails`
- [ ] `creature.golden-eagle`
- [ ] `creature.golden-jackal`
- [ ] `creature.grass-snake`
- [ ] `creature.grass-tide-rats`
- [ ] `creature.grave-candles`
- [ ] `creature.grave-leviathans`
- [ ] `creature.grave-mites`
- [ ] `creature.grave-worms`
- [ ] `creature.gray-heron`
- [ ] `creature.gray-seal`
- [ ] `creature.gray-wolf`
- [ ] `creature.great-crested-grebe`
- [ ] `creature.great-crested-newt`
- [ ] `creature.great-diving-beetle`
- [ ] `creature.great-hammerhead-shark`
- [ ] `creature.great-pond-snail`
- [ ] `creature.great-spotted-woodpecker`
- [ ] `creature.great-white-shark`
- [ ] `creature.green-iguana`
- [ ] `creature.green-knights`
- [ ] `creature.green-sea-turtle`
- [ ] `creature.greenland-shark`
- [ ] `creature.griffon-vulture`
- [ ] `creature.griffons`
- [ ] `creature.ground-pangolin`
- [ ] `creature.ground-squirrel`
- [ ] `creature.groundhog`
- [ ] `creature.guanaco`
- [ ] `creature.gulper-leviathans`
- [ ] `creature.gust-foxes`
- [ ] `creature.gutter-tongues`

#### H

- [ ] `creature.hags`
- [ ] `creature.hail-wolves`
- [ ] `creature.halo-motes`
- [ ] `creature.hanged-sentinels`
- [ ] `creature.hanuman-langur`
- [ ] `creature.harbor-porpoise`
- [ ] `creature.harbor-seal`
- [ ] `creature.harpy-eagle`
- [ ] `creature.hearth-sprites`
- [ ] `creature.hedge-sprites`
- [ ] `creature.hellhounds`
- [ ] `creature.herd-stalkers`
- [ ] `creature.hexbound-penitents`
- [ ] `creature.himalayan-tahr`
- [ ] `creature.hippogryphs`
- [ ] `creature.hippopotamus`
- [ ] `creature.hob-cats`
- [ ] `creature.hollow-acolytes`
- [ ] `creature.hollow-deer`
- [ ] `creature.hollow-echoes`
- [ ] `creature.hollow-pilgrims`
- [ ] `creature.honey-badger`
- [ ] `creature.hooded-crow`
- [ ] `creature.hook-wing-harriers`
- [ ] `creature.horizon-eyes`
- [ ] `creature.house-mouse`
- [ ] `creature.humpback-whale`
- [ ] `creature.hunger-saints`
- [ ] `creature.hunt-wraiths`
- [ ] `creature.hydras`
- [ ] `creature.hymn-owls`

#### I

- [ ] `creature.impala`
- [ ] `creature.indian-peafowl`
- [ ] `creature.indian-rhinoceros`
- [ ] `creature.infernal-dreadworms`
- [ ] `creature.inverted-seraphs`
- [ ] `creature.inverted-walkers`
- [ ] `creature.iron-drakes`
- [ ] `creature.ironhide-fiends`
- [ ] `creature.islandback-grazers`

#### J

- [ ] `creature.jaguar`
- [ ] `creature.jointless-stalkers`
- [ ] `creature.judgment-titan`

#### K

- [ ] `creature.keel-billed-toucan`
- [ ] `creature.kelp-wardens`
- [ ] `creature.kelpies`
- [ ] `creature.kiln-dancers`
- [ ] `creature.king-cobra`
- [ ] `creature.knuckle-crabs`
- [ ] `creature.koala`
- [ ] `creature.komodo-dragon`
- [ ] `creature.kraken`

#### L

- [ ] `creature.land-snail`
- [ ] `creature.lantern-anglers`
- [ ] `creature.lantern-attendants`
- [ ] `creature.lantern-dragonets`
- [ ] `creature.lantern-ghosts`
- [ ] `creature.lantern-jellies`
- [ ] `creature.leafcutter-ant`
- [ ] `creature.least-weasel`
- [ ] `creature.leatherback-sea-turtle`
- [ ] `creature.leopard`
- [ ] `creature.leopard-seal`
- [ ] `creature.leshy`
- [ ] `creature.lesser-egyptian-jerboa`
- [ ] `creature.leviathan`
- [ ] `creature.liches`
- [ ] `creature.lindworms`
- [ ] `creature.lion`
- [ ] `creature.litany-worms`
- [ ] `creature.living-dunes`
- [ ] `creature.long-tailed-jaeger`
- [ ] `creature.lowland-paca`

#### M

- [ ] `creature.magma-salamanders`
- [ ] `creature.mallard`
- [ ] `creature.mandrill`
- [ ] `creature.maned-wolf`
- [ ] `creature.manticores`
- [ ] `creature.mantled-howler-monkey`
- [ ] `creature.many-faced-guardians`
- [ ] `creature.many-handed-burrowers`
- [ ] `creature.marabou-stork`
- [ ] `creature.marine-iguana`
- [ ] `creature.markhor`
- [ ] `creature.marsh-corpses`
- [ ] `creature.marsh-dragons`
- [ ] `creature.marsh-harrier`
- [ ] `creature.maw-mimics`
- [ ] `creature.medicinal-leech`
- [ ] `creature.meerkat`
- [ ] `creature.memory-grazers`
- [ ] `creature.mercy-keepers`
- [ ] `creature.millipede`
- [ ] `creature.mind-flayers`
- [ ] `creature.mirror-confessors`
- [ ] `creature.mirror-finches`
- [ ] `creature.mirror-snails`
- [ ] `creature.moon-carp`
- [ ] `creature.moose`
- [ ] `creature.morpho-butterfly`
- [ ] `creature.moss-bears`
- [ ] `creature.moss-golems`
- [ ] `creature.moss-hares`
- [ ] `creature.mouflon`
- [ ] `creature.mountain-goat`
- [ ] `creature.mountain-hare`
- [ ] `creature.mountain-wyrms`
- [ ] `creature.mourning-shades`
- [ ] `creature.muskox`
- [ ] `creature.muskrat`
- [ ] `creature.mute-swan`

#### N

- [ ] `creature.narwhal`
- [ ] `creature.needle-drakes`
- [ ] `creature.nerve-leeches`
- [ ] `creature.nile-crocodile`
- [ ] `creature.nile-monitor`
- [ ] `creature.nine-banded-armadillo`
- [ ] `creature.nixies`
- [ ] `creature.noble-crayfish`
- [ ] `creature.northern-elephant-seal`
- [ ] `creature.northern-gannet`
- [ ] `creature.northern-pike`
- [ ] `creature.null-wardens`
- [ ] `creature.nurse-shark`
- [ ] `creature.nutria`

#### O

- [ ] `creature.oath-judges`
- [ ] `creature.oceanic-whitetip-shark`
- [ ] `creature.ocelot`
- [ ] `creature.okapi`
- [ ] `creature.olive-baboon`
- [ ] `creature.onager`
- [ ] `creature.orangutan`
- [ ] `creature.orca`
- [ ] `creature.osprey`
- [ ] `creature.ossuary-spiders`
- [ ] `creature.ostrich`

#### P

- [ ] `creature.p-ca`
- [ ] `creature.pack-drakes`
- [ ] `creature.pebble-swarms`
- [ ] `creature.pegasi`
- [ ] `creature.peregrine-falcon`
- [ ] `creature.phase-eels`
- [ ] `creature.phoenixes`
- [ ] `creature.pilgrim-elk`
- [ ] `creature.pilgrim-tortoises`
- [ ] `creature.pine-marten`
- [ ] `creature.pitch-crawlers`
- [ ] `creature.plague-cortege`
- [ ] `creature.plague-shepherds`
- [ ] `creature.plains-zebra`
- [ ] `creature.polar-bear`
- [ ] `creature.pond-dragonets`
- [ ] `creature.pond-sprites`
- [ ] `creature.processional-horrors`
- [ ] `creature.profaned-idols`
- [ ] `creature.pronghorn`
- [ ] `creature.przewalski-s-horse`
- [ ] `creature.psalm-crickets`

#### R

- [ ] `creature.raccoon`
- [ ] `creature.raccoon-dog`
- [ ] `creature.rainbow-trout`
- [ ] `creature.ravening-swarm`
- [ ] `creature.red-bellied-piranha`
- [ ] `creature.red-deer`
- [ ] `creature.red-fox`
- [ ] `creature.red-kangaroo`
- [ ] `creature.red-lionfish`
- [ ] `creature.red-necked-wallaby`
- [ ] `creature.red-panda`
- [ ] `creature.red-river-hog`
- [ ] `creature.red-squirrel`
- [ ] `creature.redcaps`
- [ ] `creature.reed-drakes`
- [ ] `creature.reef-stalkers`
- [ ] `creature.reefback-leviathans`
- [ ] `creature.reindeer-caribou`
- [ ] `creature.relic-gnawers`
- [ ] `creature.relic-knights`
- [ ] `creature.reliquary-bears`
- [ ] `creature.reliquary-doves`
- [ ] `creature.reliquary-wombs`
- [ ] `creature.revenants`
- [ ] `creature.rhesus-macaque`
- [ ] `creature.rhinoceros-beetle`
- [ ] `creature.ridge-drakes`
- [ ] `creature.ridge-goats`
- [ ] `creature.rill-spirits`
- [ ] `creature.ring-tailed-lemur`
- [ ] `creature.river-serpents`
- [ ] `creature.road-shades`
- [ ] `creature.rock-hyrax`
- [ ] `creature.rock-ptarmigan`
- [ ] `creature.rocs`
- [ ] `creature.roe-deer`
- [ ] `creature.root-titans`
- [ ] `creature.roseate-spoonbill`

#### S

- [ ] `creature.sackcloth-rats`
- [ ] `creature.saiga-antelope`
- [ ] `creature.sailback-drakes`
- [ ] `creature.salt-colossi`
- [ ] `creature.salt-drakes`
- [ ] `creature.saltwater-crocodile`
- [ ] `creature.sanctuary-lions`
- [ ] `creature.sand-cat`
- [ ] `creature.sand-lizard`
- [ ] `creature.sandstone-stalkers`
- [ ] `creature.scale-mites`
- [ ] `creature.scarlet-macaw`
- [ ] `creature.scorch-lizards`
- [ ] `creature.scripture-serpents`
- [ ] `creature.sea-otter`
- [ ] `creature.sea-serpents`
- [ ] `creature.secretary-bird`
- [ ] `creature.selkies`
- [ ] `creature.seraphic-executioners`
- [ ] `creature.serval`
- [ ] `creature.severed-crawlers`
- [ ] `creature.shipbreaker-crabs`
- [ ] `creature.shortfin-mako-shark`
- [ ] `creature.shrine-foxes`
- [ ] `creature.sidewinder`
- [ ] `creature.siege-devils`
- [ ] `creature.siege-tortoises`
- [ ] `creature.sika-deer`
- [ ] `creature.silence-eaters`
- [ ] `creature.silt-wisps`
- [ ] `creature.silver-salmon`
- [ ] `creature.singing-shoals`
- [ ] `creature.skeletal-hounds`
- [ ] `creature.skeletons`
- [ ] `creature.skin-borrowers`
- [ ] `creature.skin-takers`
- [ ] `creature.sky-rays`
- [ ] `creature.skywhales`
- [ ] `creature.slag-beetles`
- [ ] `creature.sloth-bear`
- [ ] `creature.slug`
- [ ] `creature.snapping-pike`
- [ ] `creature.snow-drakes`
- [ ] `creature.snow-goose`
- [ ] `creature.snow-leopard`
- [ ] `creature.snowy-owl`
- [ ] `creature.solar-rocs`
- [ ] `creature.soot-vermin`
- [ ] `creature.southern-cassowary`
- [ ] `creature.spark-beetles`
- [ ] `creature.spark-motes`
- [ ] `creature.spectacled-bear`
- [ ] `creature.sperm-whale`
- [ ] `creature.spire-drakes`
- [ ] `creature.spotted-eagle-ray`
- [ ] `creature.spotted-hyena`
- [ ] `creature.spriggans`
- [ ] `creature.stag-beetle`
- [ ] `creature.star-cold-drifters`
- [ ] `creature.star-spawn`
- [ ] `creature.static-fleas`
- [ ] `creature.steppe-eagle`
- [ ] `creature.stitch-wolves`
- [ ] `creature.stoat`
- [ ] `creature.storm-dragons`
- [ ] `creature.strawberry-poison-frog`
- [ ] `creature.striped-skunk`
- [ ] `creature.sulphur-eels`
- [ ] `creature.sulphur-gnats`
- [ ] `creature.sun-bear`
- [ ] `creature.swordfish`

#### T

- [ ] `creature.tallow-imps`
- [ ] `creature.tally-wasps`
- [ ] `creature.tawny-owl`
- [ ] `creature.tempting-shades`
- [ ] `creature.tench`
- [ ] `creature.tentacled-dreamers`
- [ ] `creature.the-long-thirst`
- [ ] `creature.the-unwriting`
- [ ] `creature.the-watching-deep`
- [ ] `creature.the-wild-hunt`
- [ ] `creature.thicket-drakes`
- [ ] `creature.thorn-boars`
- [ ] `creature.thorn-giants`
- [ ] `creature.thorn-sprites`
- [ ] `creature.thorn-stags`
- [ ] `creature.thought-lice`
- [ ] `creature.thunder-boars`
- [ ] `creature.thunder-herd`
- [ ] `creature.thunderhead-beasts`
- [ ] `creature.tidal-leviathans`
- [ ] `creature.tide-drakes`
- [ ] `creature.tiger`
- [ ] `creature.tiger-shark`
- [ ] `creature.tinder-mice`
- [ ] `creature.tithe-mites`
- [ ] `creature.toadstool-imps`
- [ ] `creature.toll-fiends`
- [ ] `creature.tomb-wardens`
- [ ] `creature.tree-frog`
- [ ] `creature.tundra-vole`

#### U

- [ ] `creature.unicorns`
- [ ] `creature.unlit-shapes`
- [ ] `creature.unquiet-column`

#### V

- [ ] `creature.vampires`
- [ ] `creature.vein-hounds`
- [ ] `creature.vestment-mimics`
- [ ] `creature.vigil-moths`
- [ ] `creature.virginia-opossum`
- [ ] `creature.void-ghosts`
- [ ] `creature.volcano-hearts`
- [ ] `creature.votive-stags`
- [ ] `creature.vow-hounds`

#### W

- [ ] `creature.walking-desecration`
- [ ] `creature.walking-hellgate`
- [ ] `creature.walking-necropolis`
- [ ] `creature.walrus`
- [ ] `creature.wandering-geometries`
- [ ] `creature.wandering-hollow-hills`
- [ ] `creature.war-boars`
- [ ] `creature.ward-cranes`
- [ ] `creature.water-buffalo`
- [ ] `creature.water-elementals`
- [ ] `creature.water-rail`
- [ ] `creature.wax-crawlers`
- [ ] `creature.weepers`
- [ ] `creature.wels-catfish`
- [ ] `creature.western-gorilla`
- [ ] `creature.whale-shark`
- [ ] `creature.wheel-sentinels`
- [ ] `creature.whirlpool-elementals`
- [ ] `creature.white-faced-capuchin`
- [ ] `creature.white-nosed-coati`
- [ ] `creature.white-rhinoceros`
- [ ] `creature.white-tailed-deer`
- [ ] `creature.wights`
- [ ] `creature.wild-bactrian-camel`
- [ ] `creature.wild-boar`
- [ ] `creature.wild-hunt-riders`
- [ ] `creature.wild-turkey`
- [ ] `creature.wild-yak`
- [ ] `creature.will-o-wisps`
- [ ] `creature.winged-wardens`
- [ ] `creature.wolverine`
- [ ] `creature.womb-mountains`
- [ ] `creature.wood-mouse`
- [ ] `creature.woodlouse`
- [ ] `creature.woolly-mammoths`
- [ ] `creature.wraiths`
- [ ] `creature.wreck-crabs`
- [ ] `creature.wreck-merrows`
- [ ] `creature.wyverns`

#### Z

- [ ] `creature.zombies`
<!-- END:CREATURES -->

### Settlements, architecture, and infrastructure

Approved Heartland assembled set: thatched cottage, inn, watermill, smithy with distinct house and forge roof levels, barn, stone bridge, well with corrected windlass, market shelter, meeting shelter, and stable yard. Individual exterior references are linked below; rear views, interiors and production parts remain open.

- [ ] `building.hamlet_house` — [approved Heartland thatched cottage exterior](tier-2-assembled/settlements-and-buildings/building.hamlet_house.heartland.md), grounded in the housing profiles in [buildings.json](../generator/data/buildings.json); production breakdown remains open.

- [ ] Settlement scale assemblies: hamlet, small city, medium city, capital city, castle, and surrounding farmland/hinterland.
- [ ] District assemblies: housing, civic core, market, industry, faith, harbour, defensive perimeter, and abandoned/ruined district.
- [ ] Infrastructure assemblies: street, bridge, ford, ferry, road, trail, pass, harbour, dock, aqueduct, irrigation, walls, gate, and tower.
- [ ] Settlement condition variants: founding, thriving, besieged, ruined, rebuilding, and reclaimed.

**City layout shapes** — complete plan/oblique concepts from [city-shapes.json](../generator/data/city-shapes.json).

<!-- BEGIN:CITY_SHAPES -->
- [ ] `city_shape.organic_market`
- [ ] `city_shape.market_grid`
- [ ] `city_shape.courtyard_lanes`
- [ ] `city_shape.ridge_fishbone`
- [ ] `city_shape.hill_rings`
- [ ] `city_shape.oasis_cluster`
- [ ] `city_shape.river_meander`
- [ ] `city_shape.bridge_twin`
- [ ] `city_shape.island_spines`
- [ ] `city_shape.waterfront_comb`
- [ ] `city_shape.peninsula_core`
- [ ] `city_shape.lagoon_network`
- [ ] `city_shape.citadel_lower`
- [ ] `city_shape.ceremonial_clusters`
- [ ] `city_shape.walled_compounds`
- [ ] `city_shape.concentric_enceintes`
- [ ] `city_shape.planned_extension`
<!-- END:CITY_SHAPES -->

**Building pack styles** — assembled streetscape and material/roof/ornament language for each authored pack in [buildings.json](../generator/data/buildings.json).

<!-- BEGIN:BUILDING_PACKS -->
- [ ] `city_pack_generic_surface` — Generic Settlements
- [ ] `city_pack_human_rivergate` — Rivergate Town
- [ ] `city_pack_human_highland` — Highland Hold
- [ ] `city_pack_dwarven_metalwork` — Dwarven Forgehold
- [ ] `city_pack_elf_woodland` — Sylvan Borough
- [ ] `city_pack_tideport` — Tideport Harbor
- [ ] `city_pack_gnome_foundry` — Foundry Quarter
<!-- END:BUILDING_PACKS -->

**Building asset choices** — each distinct authored assembled building ID in [buildings.json](../generator/data/buildings.json). The catalogue also has functional structure blocks; those are covered by the shared parts and assemblies above.

<!-- BEGIN:BUILDINGS -->
- [ ] `building_apartment_block_generic`
- [ ] `building_apartment_block_human`
- [ ] `building_apartment_tree_elf`
- [ ] `building_barracks_dwarf`
- [ ] `building_barracks_elf`
- [ ] `building_barracks_generic`
- [ ] `building_barracks_gnome`
- [ ] `building_barracks_human`
- [ ] `building_barracks_tidekin`
- [ ] `building_cathedral_human`
- [ ] `building_city_gate_generic`
- [ ] `building_city_wall_dwarf`
- [ ] `building_city_wall_elf`
- [ ] `building_city_wall_gnome`
- [ ] `building_city_wall_human`
- [ ] `building_city_wall_stone`
- [ ] `building_city_wall_tidekin`
- [ ] `building_city_wall_wood`
- [ ] `building_clanhall_dwarf`
- [ ] `building_courthouse_dwarf`
- [ ] `building_courthouse_elf`
- [ ] `building_courthouse_generic`
- [ ] `building_courthouse_gnome`
- [ ] `building_courthouse_human`
- [ ] `building_courthouse_tidekin`
- [ ] `building_forge_dwarf`
- [ ] `building_granary_dwarf`
- [ ] `building_granary_elf`
- [ ] `building_granary_generic`
- [ ] `building_granary_gnome`
- [ ] `building_granary_human`
- [ ] `building_granary_tidekin`
- [ ] `building_harbor_gate_tidekin`
- [ ] `building_leader_house_elf`
- [ ] `building_leader_house_generic`
- [ ] `building_leader_house_gnome`
- [ ] `building_leader_house_human`
- [ ] `building_leader_house_tidekin`
- [ ] `building_leader_keep_human`
- [ ] `building_market_hall_dwarf`
- [ ] `building_market_hall_gnome`
- [ ] `building_market_house_human`
- [ ] `building_market_house_tidekin`
- [ ] `building_market_stall_elf`
- [ ] `building_market_stall_generic`
- [ ] `building_market_stall_human`
- [ ] `building_noble_estate_generic`
- [ ] `building_noble_estate_human`
- [ ] `building_noble_house_dwarf`
- [ ] `building_noble_house_elf`
- [ ] `building_noble_house_generic`
- [ ] `building_noble_house_gnome`
- [ ] `building_noble_house_human`
- [ ] `building_noble_house_tidekin`
- [ ] `building_noble_treehouse_elf`
- [ ] `building_river_dock_generic`
- [ ] `building_river_dock_human`
- [ ] `building_river_gate_human`
- [ ] `building_shrine_elf`
- [ ] `building_shrine_gnome`
- [ ] `building_shrine_tidekin`
- [ ] `building_temple_dwarf`
- [ ] `building_temple_elf`
- [ ] `building_temple_generic`
- [ ] `building_temple_gnome`
- [ ] `building_temple_human`
- [ ] `building_temple_tidekin`
- [ ] `building_watchtower_dwarf`
- [ ] `building_watchtower_elf`
- [ ] `building_watchtower_generic`
- [ ] `building_watchtower_gnome`
- [ ] `building_watchtower_human`
- [ ] `building_watchtower_tidekin`
- [ ] `building_worker_housing_dwarf`
- [ ] `building_worker_housing_elf`
- [ ] `building_worker_housing_generic`
- [ ] `building_worker_housing_gnome`
- [ ] `building_worker_housing_human`
- [ ] `building_worker_housing_tidekin`
- [ ] `building_workshop_elf`
- [ ] `building_workshop_generic`
- [ ] `building_workshop_gnome`
- [ ] `building_workshop_human`
- [ ] `building_workshop_tidekin`
<!-- END:BUILDINGS -->

**Functional building and infrastructure types** — the 106 distinct structure IDs in the common, rural, and castle blocks of [buildings.json](../generator/data/buildings.json). These include all 88 building types that have authored NPC posts in [npc-posts.json](../generator/data/npc-posts.json). Some blocks are currently marked design-only; they remain concept-art candidates. These are functional assemblies, while the building choices above are visual pack variants.

- [ ] `building.apothecary`
- [ ] `building.armory`
- [ ] `building.bailey_court`
- [ ] `building.bakery`
- [ ] `building.barbican`
- [ ] `building.barracks`
- [ ] `building.bathhouse`
- [ ] `building.boatyard`
- [ ] `building.boundary_wall`
- [ ] `building.brewery`
- [ ] `building.bridge` — [approved Heartland exterior concept](tier-2-assembled/settlements-and-buildings/building.bridge.heartland.md); production breakdown remains open.
- [ ] `building.butchery`
- [ ] `building.carpenter`
- [ ] `building.cart_yard`
- [ ] `building.castle_armory`
- [ ] `building.castle_barracks`
- [ ] `building.castle_chapel`
- [ ] `building.castle_well`
- [ ] `building.cemetery`
- [ ] `building.charcoal_yard`
- [ ] `building.cistern`
- [ ] `building.civic_hall`
- [ ] `building.clay_pit`
- [ ] `building.corner_tower`
- [ ] `building.court`
- [ ] `building.crypt`
- [ ] `building.curtain_segment`
- [ ] `building.ditch_palisade`
- [ ] `building.dock`
- [ ] `building.drainage`
- [ ] `building.drawbridge`
- [ ] `building.dyeworks`
- [ ] `building.farmstead`
- [ ] `building.fire_shed`
- [ ] `building.fuel_depot`
- [ ] `building.garden_orchard`
- [ ] `building.gatehouse`
- [ ] `building.granary`
- [ ] `building.great_hall`
- [ ] `building.great_temple`
- [ ] `building.guardhouse`
- [ ] `building.guildhall`
- [ ] `building.hamlet_barn` — [approved Heartland exterior concept](tier-2-assembled/settlements-and-buildings/building.hamlet_barn.heartland.md); production breakdown remains open.
- [ ] `building.hamlet_cart_shed`
- [ ] `building.hamlet_farmyard`
- [ ] `building.hamlet_food_store`
- [ ] `building.hamlet_landing`
- [ ] `building.hamlet_latrine`
- [ ] `building.hamlet_meeting_shed` — [approved Heartland exterior concept](tier-2-assembled/settlements-and-buildings/building.hamlet_meeting_shed.heartland.md); production breakdown remains open.
- [ ] `building.hamlet_net_shed`
- [ ] `building.hamlet_shrine`
- [ ] `building.hamlet_smith_shed`
- [ ] `building.hamlet_staging_yard`
- [ ] `building.hamlet_track`
- [ ] `building.hamlet_well` — [approved Heartland exterior concept](tier-2-assembled/settlements-and-buildings/building.hamlet_well.heartland.md); production breakdown remains open.
- [ ] `building.hand_mill`
- [ ] `building.holding_cells`
- [ ] `building.infirmary`
- [ ] `building.inn` — [approved Heartland exterior concept](tier-2-assembled/settlements-and-buildings/building.inn.heartland.md); production breakdown remains open.
- [ ] `building.isolation_house`
- [ ] `building.keep`
- [ ] `building.keep_tower`
- [ ] `building.laboratory`
- [ ] `building.latrine`
- [ ] `building.library`
- [ ] `building.livestock_yard`
- [ ] `building.magic_well`
- [ ] `building.magical_worksite`
- [ ] `building.market_shelter` — [approved Heartland exterior concept](tier-2-assembled/settlements-and-buildings/building.market_shelter.heartland.md); production breakdown remains open.
- [ ] `building.market_square`
- [ ] `building.mason`
- [ ] `building.mine`
- [ ] `building.notice_space`
- [ ] `building.observatory`
- [ ] `building.postern`
- [ ] `building.pottery`
- [ ] `building.preservation_house`
- [ ] `building.quarry`
- [ ] `building.retaining_wall`
- [ ] `building.root_cellar`
- [ ] `building.school`
- [ ] `building.shrine`
- [ ] `building.smelter`
- [ ] `building.smithy` — [approved Heartland exterior concept](tier-2-assembled/settlements-and-buildings/building.smithy.heartland.md), with clearly separated house and forge roofs; production breakdown remains open.
- [ ] `building.square`
- [ ] `building.stable` — [approved Heartland exterior concept](tier-2-assembled/settlements-and-buildings/building.stable.heartland.md); production breakdown remains open.
- [ ] `building.stairs_ramps`
- [ ] `building.street`
- [ ] `building.tannery`
- [ ] `building.tavern`
- [ ] `building.temple`
- [ ] `building.timber_yard`
- [ ] `building.tower`
- [ ] `building.training_yard`
- [ ] `building.treasury`
- [ ] `building.wall`
- [ ] `building.wall_stair`
- [ ] `building.ward_anchor`
- [ ] `building.warehouse`
- [ ] `building.waste_yard`
- [ ] `building.water_point`
- [ ] `building.watermill` — [approved Heartland exterior concept](tier-2-assembled/settlements-and-buildings/building.watermill.heartland.md); production breakdown remains open.
- [ ] `building.weaver`
- [ ] `building.weigh_house`
- [ ] `building.well`
- [ ] `building.windmill`

**Castle kits** — complete castle concepts from [castles.json](../generator/data/castles.json).

<!-- BEGIN:CASTLE_KITS -->
- [ ] `kit.simple_enclosure` — Simple enclosure
- [ ] `kit.motte_bailey` — Motte and bailey
- [ ] `kit.concentric` — Concentric wards
<!-- END:CASTLE_KITS -->

### Key locations, exploration, and narrative

- [ ] Key-location state language: intact, occupied, deserted, ruined, flooded, buried, revealed, and reclaimed.
- [ ] Interior language for tier-2 catalogue sites: entrance, circulation, chamber, hazard, reward, and exit; use authored interior rules where present.
- [ ] Location composition sets: road waystones, march stones, beacon line, pilgrim road, siege works, mine village, barrow ring, and hermit cells.
- [ ] Story scene sets: founding, siege, war, exile, refuge, pilgrimage, hunt, betrayal, revelation, corruption, cleansing, and age transition.

**Key-location archetypes** — all 97 authored site IDs, grouped by family from [key-locations-catalogue.json](../generator/data/key-locations-catalogue.json). A complete exterior is required for every item; tier-2 interiors only where the catalogue specifies one.

<!-- BEGIN:KEY_LOCATIONS -->
#### Subterranean (`subterranean`)

- [ ] `key_location.karst_cave` — tier 2; interior specified
- [ ] `key_location.sea_cave` — tier 1
- [ ] `key_location.lava_tube` — tier 2; interior specified
- [ ] `key_location.crystal_cavern` — tier 2; interior specified
- [ ] `key_location.sinkhole` — tier 1
- [ ] `key_location.rift_chasm` — tier 1
- [ ] `key_location.natural_arch` — tier 0
- [ ] `key_location.tar_pit` — tier 1
- [ ] `key_location.geyser_basin` — tier 1
- [ ] `key_location.glacier_cave` — tier 2; interior specified
- [ ] `key_location.meteor_crater` — tier 1

#### Extractive (`extractive`)

- [ ] `key_location.iron_mine` — tier 2; interior specified
- [ ] `key_location.silver_mine` — tier 2; interior specified
- [ ] `key_location.gem_mine` — tier 2; interior specified
- [ ] `key_location.salt_mine` — tier 2; interior specified
- [ ] `key_location.quarry` — tier 1
- [ ] `key_location.salt_pans` — tier 1
- [ ] `key_location.charcoal_camp` — tier 0
- [ ] `key_location.lime_kiln` — tier 0
- [ ] `key_location.peat_cuttings` — tier 1
- [ ] `key_location.whaling_station` — tier 1
- [ ] `key_location.logging_camp` — tier 1
- [ ] `key_location.smelter_ruin` — tier 1
- [ ] `key_location.miners_cottages` — tier 1

#### Fortification (`fortification`)

- [ ] `key_location.border_fort` — tier 2; interior specified
- [ ] `key_location.watchtower` — tier 1
- [ ] `key_location.hillfort` — tier 1
- [ ] `key_location.toll_station` — tier 1
- [ ] `key_location.linear_dyke` — tier 1
- [ ] `key_location.refuge_broch` — tier 1
- [ ] `key_location.beacon_tower` — tier 1
- [ ] `key_location.siege_camp` — tier 1

#### Funerary (`funerary`)

- [ ] `key_location.barrow` — tier 1
- [ ] `key_location.barrow_field` — tier 2; interior specified
- [ ] `key_location.mausoleum` — tier 2; interior specified
- [ ] `key_location.cairn_field` — tier 0
- [ ] `key_location.excarnation_tower` — tier 1
- [ ] `key_location.ship_burial` — tier 1
- [ ] `key_location.plague_pit` — tier 1
- [ ] `key_location.necropolis` — tier 2; interior specified
- [ ] `key_location.cliff_coffins` — tier 1

#### Sacred (`sacred`)

- [ ] `key_location.wayside_shrine` — tier 0
- [ ] `key_location.monastery` — tier 2; interior specified
- [ ] `key_location.hermitage` — tier 1
- [ ] `key_location.sacred_grove` — tier 1
- [ ] `key_location.standing_stones` — tier 1
- [ ] `key_location.oracle_cave` — tier 2; interior specified
- [ ] `key_location.holy_well` — tier 1
- [ ] `key_location.mountain_sanctuary` — tier 2; interior specified
- [ ] `key_location.drowned_temple` — tier 2; interior specified
- [ ] `key_location.colossus` — tier 1
- [ ] `key_location.reliquary_chapel` — tier 1
- [ ] `key_location.pilgrim_station` — tier 1
- [ ] `key_location.hermit_cell` — tier 1

#### Wayside (`wayside`)

- [ ] `key_location.crossroads` — tier 0
- [ ] `key_location.bridge` — tier 1
- [ ] `key_location.ford` — tier 0
- [ ] `key_location.pass_hospice` — tier 1
- [ ] `key_location.ferry_crossing` — tier 1
- [ ] `key_location.hedge_inn` — tier 1
- [ ] `key_location.gibbet` — tier 0
- [ ] `key_location.caravanserai` — tier 2; interior specified
- [ ] `key_location.waystone` — tier 0
- [ ] `key_location.boundary_stone` — tier 0

#### Arcane (`arcane`)

- [ ] `key_location.ley_nexus` — tier 1
- [ ] `key_location.wizard_tower` — tier 2; interior specified
- [ ] `key_location.wild_magic_scar` — tier 1
- [ ] `key_location.binding_site` — tier 2; interior specified
- [ ] `key_location.portal_arch` — tier 1
- [ ] `key_location.academy_ruin` — tier 2; interior specified
- [ ] `key_location.summoning_circle` — tier 0
- [ ] `key_location.petrified_army` — tier 1

#### Lair (`lair`)

- [ ] `key_location.dragon_lair` — tier 2; interior specified
- [ ] `key_location.giants_hall` — tier 2; interior specified
- [ ] `key_location.spider_wood` — tier 1
- [ ] `key_location.beast_den` — tier 1
- [ ] `key_location.wyvern_eyrie` — tier 1

#### Drowned (`drowned`)

- [ ] `key_location.drowned_village` — tier 1
- [ ] `key_location.wreck_field` — tier 1
- [ ] `key_location.surf_ruin` — tier 1
- [ ] `key_location.crannog` — tier 1

#### Wonder (`wonder`)

- [ ] `key_location.fallen_star` — tier 3
- [ ] `key_location.titan_skeleton` — tier 3; interior specified
- [ ] `key_location.great_stump` — tier 3
- [ ] `key_location.the_scar` — tier 3
- [ ] `key_location.maelstrom` — tier 3

#### Curiosity (`curiosity`)

- [ ] `key_location.observatory` — tier 2; interior specified
- [ ] `key_location.library_ruin` — tier 2; interior specified
- [ ] `key_location.lighthouse` — tier 1
- [ ] `key_location.quarantine_isle` — tier 1
- [ ] `key_location.arena_ruin` — tier 1
- [ ] `key_location.aqueduct_span` — tier 1
- [ ] `key_location.fairy_ring` — tier 0
- [ ] `key_location.witch_cottage` — tier 1
- [ ] `key_location.bandit_camp` — tier 1
- [ ] `key_location.hunting_lodge` — tier 1
- [ ] `key_location.battlefield` — tier 1
<!-- END:KEY_LOCATIONS -->

### Magic, faith, effects, and resources

- [ ] Ley network assembly: line, node, nexus, key point, drift, fracture, and suppression/activation states.
- [ ] Spellcasting assemblies: source, casting gesture/focus, projectile/field, impact, residue, and environmental response.
- [ ] Corruption and cleansing transformations across land, water, vegetation, architecture, creature, and character.
- [ ] Religious assemblies: shrine, temple, icon, ritual, procession, avatar/visitation, and sacred landscape.
- [ ] Resource/economy assemblies: farm, mine, quarry, logging, forge, market, trade caravan, port, and storage.

**Magic schools** — one assembled effect and environment signature per authored school in [magic-schools.json](../generator/data/magic-schools.json).

<!-- BEGIN:MAGIC_SCHOOLS -->
- [ ] `school.weave` — chaos, creation, creativity
- [ ] `school.umbral` — necrotic, entropy, death, order
- [ ] `school.infernal` — demons, hellscapes, corruption, evil
- [ ] `school.radiant` — holy, healing, order, peace, righteousness
- [ ] `school.fire` — fire, heat, renewal, chaos
- [ ] `school.water` — water, ice, currents, order
- [ ] `school.earth` — nature, ground, stone, roots, order
- [ ] `school.air` — wind, storms, chaos, force
- [ ] `school.blood` — sacrifice, lineage, vitality, hunger
- [ ] `school.void` — absence, entropy, unmaking, silence
- [ ] `school.rot` — disease, undeath, decay, rebirth
- [ ] `school.eldritch` — madness, wrong geometry, dominion of minds
<!-- END:MAGIC_SCHOOLS -->

**Gods** — distinct iconography and representative manifestation for the public and hidden authored gods in [pantheon.json](../generator/data/pantheon.json).

<!-- BEGIN:GODS -->
#### Public gods

- [ ] `god_weave` — The Weave
- [ ] `god_umbral` — The Umbral
- [ ] `god_infernal` — The Infernal
- [ ] `god_radiant` — The Radiant
- [ ] `god_fire` — The Fire
- [ ] `god_water` — The Water
- [ ] `god_earth` — The Earth
- [ ] `god_air` — The Air
- [ ] `god_first_hearth` — The First Hearth
- [ ] `god_elder_star` — The Elder Star
- [ ] `god_deep_forge` — The Deep Forge
- [ ] `god_drowned_mother` — The Drowned Mother
- [ ] `god_sky_court` — The Sky Court
- [ ] `god_great_wyrm` — The Great Wyrm
- [ ] `god_outer_unnamed` — The Unnamed Outside
- [ ] `god_fallen_cities` — The Keeper of Fallen Cities
- [ ] `god_wanderer` — The Wanderer
- [ ] `god_turning_moon` — The Turning Moon
- [ ] `god_hearth_harvest` — The Hearth and the Harvest
- [ ] `god_forge_craft` — The Maker
- [ ] `god_road_market` — The Coin and the Road
- [ ] `god_crown_law` — The Crowned Scale
- [ ] `god_grave_memory` — The Grave Keeper
- [ ] `god_fortune_fate` — The Turning Wheel
- [ ] `god_red_field` — The Red Field

#### Hidden gods

- [ ] `god_long_thirst` — The Long Thirst
- [ ] `god_glad_mother` — The Glad Mother
- [ ] `god_shape_beneath` — The Shape Beneath
- [ ] `god_unwriting` — The Unwriting
<!-- END:GODS -->

### Maps and interface

- [ ] Globe and atlas map composition, including land/water, relief, climate, biome, and magic overlays.
- [ ] Regional map composition with rivers, roads, settlements, borders, key locations, nests, and routes.
- [ ] Local map composition for city, hamlet, castle, key-location exterior, and interior plans.
- [ ] Procedural map key and legend with scale, compass, latitude, terrain, ownership, danger, and uncertainty.
- [ ] Marker sets for every biome, settlement class, road/route, castle, ruin, nest, key-location family/archetype, quest, hero, NPC, god, and school.
- [ ] UI screen concepts: world generation controls, world overview, settlement view, character/creature detail, quest/story web, timeline/history, and map layers.
- [ ] UI states: selected/hovered/disabled, fog/undiscovered, warning, corruption/cleansing, seasonal change, and accessibility alternatives.

## Tier 3 — Parts for production

### Terrain, ecology, and materials

- [ ] Terrain kit: height/erosion shapes, cliffs, boulders, scree, dunes, soil, sand, snow, ice, lava, wet ground, and shore transitions.
- [ ] Water kit: waves, foam, river banks, rapids, spray, waterfall, shorelines, frozen edges, and underwater silhouettes.
- [ ] Vegetation kit: trunks, roots, branches, canopies, leaves, flowers, crops, reeds, moss, fungi, deadwood, and seasonal variants.
- [ ] Decal/material kit: mud, dust, moss, soot, salt, rust, blood, rot, cracks, ruins, water stains, and magical contamination.
- [ ] Sky/weather kit: cloud types, precipitation, fog, wind streaks, lightning, sunlight, moon/stars, and atmospheric perspective.

### People and creature parts

- [ ] Anatomy breakdowns for each body plan used by authored profiles: humanoid, quadruped, bird, fish, reptile, amphibian, invertebrate, cephalopod, marine mammal, giant, and supernatural/abstract.
- [ ] Character parts: heads/faces, hair, skin/markings, hands, feet, body proportions, expressions, poses, and age/condition variants.
- [ ] Costume and equipment kit by people/role: underlayers, outerwear, armor, footwear, headwear, jewelry, insignia, weapons, tools, packs, and travel gear.
- [ ] Creature parts: skulls, jaws, eyes, horns, antlers, wings, limbs, claws, tails, shells, scales, fur, feathers, fins, teeth, and sensory organs.
- [ ] Animation reference sheets: walk/run/fly/swim/crawl, attack/defense, idle, interaction, death, and carrying/handling props.

### Architecture and transport parts

- [ ] Construction materials and joints for timber, stone, brick, earth, metal, glass, bone, living wood, and magical materials.
- [ ] Building modules: foundations, walls, corners, roofs, doors, windows, floors, stairs, chimneys, porches, balconies, supports, and ornament.
- [ ] Civic/service modules: wells, cisterns, markets, workshops, storage, sanitary works, shrines, docks, and street furniture.
- [ ] Road/route modules: roadbed, paving, trail, bridge spans, ferries, signs, milestones, waystones, and boundary markers.
- [ ] Vehicle/transport modules: carts, wagons, boats, ships, pack saddles, harnesses, caravan tents, and cargo loads.

**Castle geometry modules** — individual production breakdowns from [castles.json](../generator/data/castles.json).

<!-- BEGIN:CASTLE_MODULES -->
- [ ] `castle_module.curtain_segment`
- [ ] `castle_module.corner_join`
- [ ] `castle_module.gatehouse`
- [ ] `castle_module.postern`
- [ ] `castle_module.wall_stair`
- [ ] `castle_module.interval_tower`
- [ ] `castle_module.drawbridge`
- [ ] `castle_module.bailey_court`
- [ ] `castle_module.keep_tower`
<!-- END:CASTLE_MODULES -->

### Key-location exterior parts

**Authored exterior pieces** — all 75 individual part IDs from [key-location-exteriors.json](../generator/data/key-location-exteriors.json). These assemble into the Tier 2 key-location concepts; the catalogue also has family and archetype kit recipes.

<!-- BEGIN:LOCATION_PARTS -->
- [ ] `keyloc.part.cave_mouth` — Cave mouth
- [ ] `keyloc.part.rock_outcrop` — Rock outcrop
- [ ] `keyloc.part.boulder` — Boulder
- [ ] `keyloc.part.scree_fan` — Scree fan
- [ ] `keyloc.part.sinkhole_rim` — Sinkhole rim
- [ ] `keyloc.part.arch_span` — Stone arch
- [ ] `keyloc.part.geyser_vent` — Geyser vent
- [ ] `keyloc.part.tar_seep` — Tar seep
- [ ] `keyloc.part.ice_mouth` — Ice mouth
- [ ] `keyloc.part.crater_rim` — Crater rim
- [ ] `keyloc.part.crystal_spur` — Crystal spur
- [ ] `keyloc.part.chasm_edge` — Chasm edge
- [ ] `keyloc.part.headframe` — Winding headframe
- [ ] `keyloc.part.adit_mouth` — Adit mouth
- [ ] `keyloc.part.spoil_heap` — Spoil heap
- [ ] `keyloc.part.winding_shed` — Winding shed
- [ ] `keyloc.part.ore_track` — Ore track
- [ ] `keyloc.part.kiln` — Kiln
- [ ] `keyloc.part.charcoal_hearth` — Charcoal hearth
- [ ] `keyloc.part.salt_pan` — Salt pan
- [ ] `keyloc.part.peat_stack` — Peat stack
- [ ] `keyloc.part.drying_rack` — Drying rack
- [ ] `keyloc.part.slag_mound` — Slag mound
- [ ] `keyloc.part.quarry_face` — Quarry face
- [ ] `keyloc.part.tower` — Tower
- [ ] `keyloc.part.wall_segment` — Wall segment
- [ ] `keyloc.part.gatehouse` — Gatehouse
- [ ] `keyloc.part.rampart` — Rampart
- [ ] `keyloc.part.palisade` — Palisade
- [ ] `keyloc.part.watch_platform` — Watch platform
- [ ] `keyloc.part.broch_shell` — Broch shell
- [ ] `keyloc.part.dyke_bank` — Dyke bank
- [ ] `keyloc.part.barrow_mound` — Barrow mound
- [ ] `keyloc.part.standing_stone` — Standing stone
- [ ] `keyloc.part.cairn` — Cairn
- [ ] `keyloc.part.tomb_facade` — Tomb facade
- [ ] `keyloc.part.grave_marker` — Grave marker
- [ ] `keyloc.part.excarnation_platform` — Excarnation platform
- [ ] `keyloc.part.coffin_ledge` — Coffin ledge
- [ ] `keyloc.part.ossuary_shed` — Ossuary shed
- [ ] `keyloc.part.shrine_niche` — Shrine niche
- [ ] `keyloc.part.chapel` — Chapel
- [ ] `keyloc.part.cloister_range` — Cloister range
- [ ] `keyloc.part.altar_stone` — Altar stone
- [ ] `keyloc.part.well_head` — Well head
- [ ] `keyloc.part.colossus_figure` — Colossal figure
- [ ] `keyloc.part.pilgrim_shelter` — Pilgrim shelter
- [ ] `keyloc.part.waystone` — Waystone
- [ ] `keyloc.part.bridge_span` — Bridge span
- [ ] `keyloc.part.ford_marker` — Ford marker
- [ ] `keyloc.part.gibbet_post` — Gibbet
- [ ] `keyloc.part.inn_house` — Roadhouse
- [ ] `keyloc.part.stable` — Stable
- [ ] `keyloc.part.yard_wall` — Yard wall
- [ ] `keyloc.part.caravan_range` — Caravan range
- [ ] `keyloc.part.ley_pillar` — Ley pillar
- [ ] `keyloc.part.tower_spire` — Spire
- [ ] `keyloc.part.ward_stone` — Ward stone
- [ ] `keyloc.part.portal_arch` — Portal arch
- [ ] `keyloc.part.circle_cut` — Cut circle
- [ ] `keyloc.part.petrified_figure` — Petrified figure
- [ ] `keyloc.part.bone_midden` — Bone midden
- [ ] `keyloc.part.nest_mound` — Nest mound
- [ ] `keyloc.part.wreck_hull` — Wreck hull
- [ ] `keyloc.part.drowned_roof` — Drowned roof
- [ ] `keyloc.part.piling` — Piling
- [ ] `keyloc.part.crannog_platform` — Crannog platform
- [ ] `keyloc.part.rubble_pile` — Rubble pile
- [ ] `keyloc.part.ruined_wall` — Ruined wall
- [ ] `keyloc.part.fallen_column` — Fallen column
- [ ] `keyloc.part.foundation_line` — Foundation line
- [ ] `keyloc.part.overgrown_mound` — Overgrown mound
- [ ] `keyloc.part.tent` — Tent
- [ ] `keyloc.part.campfire` — Campfire
- [ ] `keyloc.part.lean_to` — Lean-to
<!-- END:LOCATION_PARTS -->

### Magic, props, and UI parts

- [ ] Twelve school effect kits: shape glyphs, particles, light, surface change, cast origin, impact, and lingering field for each school listed above.
- [ ] Faith props: altars, icons, offerings, reliquaries, vestments, ritual tools, banners, and pilgrimage markers.
- [ ] Quest/story props: letters, maps, seals, relics, weapons, keys, containers, clues, and faction emblems.
- [ ] Economy/resource props: crops, ore, timber, stone, textiles, coin, trade goods, tools, and storage vessels.
- [ ] Cartography parts: terrain fills, contours, hachures, coastlines, rivers, borders, roads, labels, frames, scale bars, compass, and markers.
- [ ] UI parts: icons, panels, typography, controls, status badges, portrait frames, inventory slots, tooltips, timeline nodes, and connection lines.

## Source and upkeep rules

- [ ] When `generator/data/` adds, removes, or renames an authored visual ID, update the corresponding checklist section and count here.
- [ ] For each approved concept, record its catalogue ID, tier, version, owner, date, and any linked parent/child concepts in the source file or adjacent metadata.
- [ ] Review assembled concepts at game camera distance and map symbols at final UI size, including color-blind-safe differentiation.
- [ ] Confirm that shared Tier 3 parts cover every approved Tier 2 design before treating the asset family as production-ready.

Catalogue coverage at creation: 11 reachable base biomes, 132 reachable school/biome variants, 12 peoples, 32 hero archetypes, 680 creature profiles, 17 city shapes, 7 building packs, 84 building choices, 106 functional structures, 3 castle kits, 97 key locations, 12 magic schools, 29 gods, 9 castle modules, and 75 key-location exterior parts. These counts refer to **authored IDs**, not the unbounded number of generated instances.
