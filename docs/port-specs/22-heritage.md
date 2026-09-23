# Port specification: 22-heritage

Extracted from the Python `FantasyWorldGenerator` at commit `b67c99b` before that
repository was retired, then audited against the same source by a second reader.

**Source modules**

- `Sim/heritage/`

**Estimated C++ size:** roughly 2600 lines, excluding tests.

## Audit: NOT YET IMPLEMENTABLE

The spec is an excellent description of the *code* and a poor carrier of the *data*, and in this package the data is most of the subsystem. Every algorithm in `derive.py`, `phonemes.py` and `naming.py` is described accurately and in the right order, the intent comments are carried near-verbatim, the ordering hazards are catalogued better than I could have, and the four stock sizes, the dwarf inventory worked example, the threshold shape (0.35/0.52/0.64/0.73/0.79…1.0) and the `_rare_patterns` six all check out against a live run. But heritage is a table-driven package: `resolve` is 40 lines of glue over 84 KB of authored JSON, and the spec reproduces only two of the five documents. `derivation.json` is summarised as "three rule kinds" with only the three rate tables quoted — the 24 culture rules and their maps, and every genome map except rates (syllable template by speech_environment, vowel_length, hiatus, geminates, stress, tone, word_shape, compounding, honorific, call_form, script_form, direction) are absent, so a porter cannot produce a single culture key or a single phonotactic field. `lexicon.json` gives the dwarven root table but not the human or elven ones, so two of the three families have no vocabulary. `appearance.json` — 63 KB, the one layer carrying numbers, and per the spec's own wording "the deliverable, not decoration" — is present only as a shape description and one abridged dwarf example; the three race bases and nine deltas are gone. With the Python deleted, those bytes do not exist anywhere else. There is also one genuine algorithm omission that would silently produce different names: `_draw_person` passes `onset_cluster and index == 0` to `_syllable`, so only the first syllable may take an onset cluster; the spec's step 3 computes `onset_cluster` once and never mentions the index guard, and its RNG accounting inherits the same error. Beyond that the spec makes several confident statements that are false of the source — most damagingly that `name_table` appends two empty codas "so an open syllable is twice as likely", when `sorted(set(codas))` collapses them to one, and that the coda-2 cluster set is twelve entries when the shipped set is thirteen (`nd` is missing from the spec's list, contradicting the spec's own homorganic rule). A competent C++ programmer given this document alone would be blocked on the first culture key and would build a different name generator even where they were not blocked.

**5 blocking gap(s)** were found and are recorded in the addenda below.

## Purpose

Heritage answers **what a people is like**, as static, seedless, per-civilization data. The civilization registry (`civilizations.json`) already answers *where* a people can live — 37 numeric habitat fields — and heritage deliberately restates none of it. Instead it holds four layers: (1) seventeen categorical **key traits** authored per parent race and extended by a sparse per-civilization delta; (2) a **culture** (24 ethnographic fields in 6 blocks) derived as a pure function of the traits; (3) a **language genome** (phoneme inventory, phonotactics, prosody, morphology, orthography, three rates, and a branch-specific lexicon of morphemes) derived from the traits, the culture and a shared root table; (4) an authored **appearance** — a complete body per race extended per subrace, the only layer in the package that carries numbers. On top of that sits a **naming** service: morphemic, glossable settlement names, dithematic personal names, a fallback phonotactic name generator, realm names, and a per-people **name stock** that says which of the buildable names actually get used and how often.

It exists so that adding a people to the registry yields a plausible culture and a distinct tongue for free, while anything the derivation gets wrong can be hand-patched by a sparse override. Before it existed, personal and place names were keyed on the *parent race*, so four of the twelve peoples (tidekin, gnome, hill_dwarf, frosthold_dwarf) had no table at all and silently drew human names; and each fallen city's magic school lived in a hand-written twelve-row dict mirrored, unsynchronised, in `Core/legacy.cpp`. Heritage removed both defects by making the school a key trait and the tongue a per-civilization derivation.

## Where it sits in the pipeline

**Heritage is a leaf.** It reads no world layer, no config, no terrain, no RNG seed, and it never imports the simulation. It is a pure function of five packaged JSON documents plus two string arguments (`civilization_id`, `parent_race_id`). Nothing in it depends on any pipeline stage having run, and it can be built and tested before stage 1 exists. In `FWGC++` terms it is a static library with no dependency on `Grid`, `sphere_grid`, `Config` or `World`.

**What must exist before it runs:** only the civilization registry's identifier sets. Heritage owns no identifiers of its own — every key in every heritage file is either a `civilization_ids()` key or a `parent_races` key. That binding is not enforced at runtime (heritage cannot see the registry); it is enforced by a repository-level test asserting set equality in **both** directions across five separate bindings:
- `traits.peoples` keys == `civilization_ids()` (12 entries)
- `traits.races` keys == `parent_races` keys (3 entries: `human`, `elf`, `dwarf`)
- `appearance.peoples` keys == `civilization_ids()`; `appearance.races` keys == `parent_races`
- every `lexicon.families[*].parent_race` is a real parent race, and the set of claimed parents == the set of parent races
- the union of all `lexicon.families[*].branches` keys == `civilization_ids()` — i.e. **every civilization has its own language branch**

Because heritage is a leaf, the caller must pair a civilization with its parent race. In the reference the pairings live in exactly two bridges: `terrain_civilizations.heritage_of` / `civilization_report`, and `tools/export_catalogues.py`.

**What depends on it afterwards:**
- **Settlement naming** (`terrain_settlements._settlement_names`, stage 10–12): for each founded city, in **node order**, resolves the city's people and calls `settlement_name`, with a bounded 8-attempt redraw on a duplicate name (the redraw varies the seed's third parameter, so a duplicate yields another authentic name rather than a numeric suffix). Returns `{node: (name, gloss)}`.
- **Hero naming** (`hero_generator.naming.heritage_name`, stage 16): `person['race_id']` is the *parent* race, `person['civilization_id']` selects the language. A record missing `civilization_id` **raises**; there is no fallback, by design.
- **NPC roster naming** (`npc_roster.naming`, stage 16): builds one `name_stock` per `(civilization_id, parent_race_id)` pair, memoised, and draws from it per person. A size-65 world carries ~13,000 people, so the stock must be built once per people and never per name.
- **Realm naming** (`hero_generator.realms`): realm = capital name + the tongue's `realm` morpheme.
- **Ruin legacies** (`terrain_ruins.culture_schools`, stages 13–15): each civilization's `traits.magic_school` decides which leyline school a fallen city of that culture seeds. This is the one place heritage reaches into world generation numerically-consequential territory.
- **World document**: `civilization_report` writes a `civilizations` block at **version 3** containing, at top level, `heritage: heritage_identity(...)`, and per entity a `heritage` sub-object of exactly `{traits, culture, genome, provenance}` — **appearance is deliberately excluded** from the world document (see `writes`).
- **Catalogue export**: `Contracts/catalogues/native-catalogues-v1.json` carries `heritage: {civ_id: <full resolved people, including appearance>}` for all 12, sorted by id. This is the channel the art pipeline and the native core read.

## Inputs

Heritage reads **no world layer and no generator config**. Its whole input is five packaged JSON documents plus two strings.

**Call inputs**
- `civ_id: string` — a civilization id. **Unknown ids are legal**: an unlisted civilization resolves to its parent race's base traits and base appearance, with no delta and no overrides. This is the property that lets a new people arrive complete, and that keeps a synthetic test entity (`copper_folk`) driving the whole pipeline.
- `parent_race_id: string` — one of `human`, `elf`, `dwarf`. **Unknown, empty or null raises `ValueError`.** The asymmetry is deliberate and is a published contract: a parent race selects the language *family*, so guessing one produces a name in the *wrong* language rather than an approximate one. A caller that cannot supply a parent race must leave the name empty and say so, never substitute a default.
- `draw` (naming only) — a caller-supplied random source exposing exactly two operations: `random() -> double in [0,1)` and `choice(sequence) -> element`. Heritage carries **no seed logic at all**; every seeding decision stays with the subsystem that owns it.
- `terrain_slot` (optional, `settlement_name`) — a lexicon slot name such as `"water"`, letting a settlement name echo the ground it stands on.

**Packaged documents** (all under `policies/`, all UTF-8 JSON, each carrying its own positive-integer `revision`):

1. **`traits.json`** (rev 1, ~5.9 KB). `{schema, revision, notes, races: {race: {17 axes}}, peoples: {civ_id: {sparse axis delta}}}`. Every race base names exactly 17 axes; `human_heartland`, `elf` and `dwarf` carry **empty** deltas because the base is the unmarked member.
2. **`derivation.json`** (rev 1, ~8.4 KB). `{schema, revision, notes, culture: {block: {key: rule}}, genome: {block: {key: rule}}}`. Three rule kinds, described in `algorithm`.
3. **`lexicon.json`** (rev 1, ~5.9 KB). `{schema, revision, notes, shared_slots: [19 strings], families: {family: {parent_race, word_order, affixing, roots: {slot: root}, branches: {branch: {sound_changes: [[old,new],...], add_roots: {slot: root}, drop_roots?: [slot]}}}}, forbidden_forms: [14 strings], personal_templates: [{pattern:[role,role], weight:int}], settlement_templates: [same], slot_roles: {role: [slot,...]}}`.
4. **`overrides.json`** (rev 1, ~1.1 KB). `{schema, revision, notes, culture: {civ_id: {block: {key: value}}}, genome: {civ_id: {block: {key: value | {add:[],drop:[]}}}}}`. Both top-level sections are optional.
5. **`appearance.json`** (rev 1, ~63 KB, mostly English prose). `{schema, revision, notes, races: {race: {8 complete blocks}}, peoples: {civ_id: {sparse block/key delta}}}`. **Every civilization must have an entry, even an empty one** — unlike traits, an absent people is a load error, because a subrace silently wearing its archetype's body is a wrong answer nobody notices until the art comes back looking like its parent.

**Units and ranges in the one layer that carries numbers (appearance):**
- `frame.height_m.{male,female}` — metres, band `{min, mean, max, sd}`, requires `0 < min <= mean <= max`, `sd > 0`. Observed range across the twelve: 0.95–2.20 m.
- `frame.mass_kg.{male,female}` — kilogrammes, same band shape. Observed 23–~110 kg.
- `proportion.heads_tall` — head units, bounded `[3.0, 9.0]`. Observed 4.2 (gnome) to ~8 (elf).
- `proportion.{shoulder_width_ratio, leg_length_ratio, hand_length_ratio}` — fractions of standing height, bounded `[0.05, 0.95]`.
- `coloration.{skin,hair,eye}.swatches[].weight` — **population frequencies**, each `> 0`, summing to `1.0 ± 1e-6`. Minimum 3 swatches.
- `coloration.*.swatches[].hex` — lowercase `#rrggbb`, regex `^#[0-9a-f]{6}$`.
- `life_stages.{child,adolescent,adult,elder}.onset_years` — years, bounded `[0, 5000]`.
- `life_stages.*.height_fraction` — fraction of adult stature, bounded `[0.2, 1.0]`.
- `life_stages.max_years` — years, bounded `[1, 5000]`.
- Everything else categorical: bare snake_case tokens matching `^[a-z][a-z0-9_]*$`, or open English prose of at least 20 trimmed characters.

Metres and kilogrammes throughout, never feet; the Unreal adapter converts at its own boundary.

## Outputs

`resolve(civ_id, parent_race_id)` returns one **resolved people**, a deep copy (never an alias of the cache). Its top-level shape:

```
{
  "id":              civ_id,            # <-- THE KEY FIELD IS `id`, not uid/site_id/kind
  "parent_race_id":  parent_race_id,
  "traits":          {17 axes},
  "appearance":      {8 blocks},
  "culture":         {6 blocks, 24 keys},
  "genome":          {family, branch, inventory, sound_changes,
                      forbidden_forms, lexicon,
                      phonotactics, prosody, morphology, orthography, rates},
  "provenance":      {revisions: {traits,derivation,lexicon,overrides,appearance},
                      overridden: ["culture.<block>.<key>", "genome.<block>.<key>", ...]}
}
```

**`traits`** — exactly 17 keys, every value a string except `craft_focus`, which is a list of exactly 2 distinct crafts. **No numbers and no booleans anywhere**; this is enforced at load and asserted by test, and it is the structural guarantee that heritage cannot become a second copy of the numeric habitat profile.

Axes and their closed enums:
- Soma (6, drive the phoneme inventory): `body_scale` ∈ {small, compact, mid, tall, towering}; `vocal_tract` ∈ {narrow, even, broad, resonant_chest}; `breath_capacity` ∈ {short, steady, deep, sustained}; `hearing_band` ∈ {low_weighted, flat, high_weighted, wide}; `lifespan_tempo` ∈ {brief, human, long, ancient}; `dentition` ∈ {blunt, even, prominent_canine, tusked}.
- Register (3, drive prosody and script): `speech_environment` ∈ {open_plain, surf, forest, stone_hall, wind_scoured, humid_canopy, market_din, terraced_valley}; `carrying_register` ∈ {quiet, conversational, carrying, calling}; `writing_surface` ∈ {none, knot_and_cord, incised_stone, carved_wood, inked_hide, pressed_fibre_paper}.
- Social (8, drive culture): `kinship` ∈ {nuclear_household, extended_clan, lineage_house, age_cohort, crew_and_berth, guild_family}; `authority` ∈ {elder_council, hereditary_lord, guild_assembly, acclaimed_war_leader, temple_seat, consensus_moot}; `mobility` ∈ {rooted, seasonal_transhumance, voyaging, caravan, frontier_expanding}; `memory_mode` ∈ {oral_formulaic, sung_lineage, written_archive, carved_record, ledger_and_seal}; `craft_focus` ⊂ CRAFTS (exactly 2 distinct); `outsiders` ∈ {hostile, wary, transactional, hospitable, assimilating}; `magic_stance` ∈ {untrained, ritual_communal, scholarly_licensed, inherited_bloodline, taboo_avoidant, bargained_with}; `magic_school` ∈ KNOWN_SCHOOLS.

CRAFTS = (seafaring, husbandry_and_tillage, metal_and_forge, stone_and_vault, wood_and_weave, glass_and_mechanism, herb_and_dye, ice_and_preserve, clay_and_kiln).
KNOWN_SCHOOLS = (weave, umbral, infernal, radiant, fire, water, earth, air).
HIDDEN_SCHOOLS = (blood, void, rot, eldritch) — **refused as a trait value with a dedicated error**: nobody holds a cultural stance toward a school no world can raise.

**`culture`** — exactly 6 blocks, 24 keys, every value a string. No numbers, enforced.
- `household`: unit, inheritance, naming_pattern, marriage_move
- `governance`: seat, succession, law_form, assembly
- `livelihood`: primary, secondary, exchange, calendar
- `memory`: record, teaching, oath_form, taboo
- `contact`: stranger_rite, trade_stance, war_stance
- `faith`: school, practice, site, festival

**`genome`** —
- `family: string` (one of `human`, `elven`, `dwarven`), `branch: string` (== civ_id for the twelve; for an unlisted civ, the branch simply does not exist in the family and an empty spec is used).
- `inventory`: `{consonants: [token], vowels: [token], forbidden: [token], onsets: [token]}`. `consonants ∪ vowels` is disjoint from `forbidden`. `onsets` is `consonants` minus `ng`. All ordered by the fixed alphabet order, **never by the order gates fired** — this ordering is hashed into the heritage identity.
- `sound_changes: [[old, new], ...]` — the branch's ordered rules, carried so coined names undergo the same shifts the inherited roots did.
- `forbidden_forms: [string]` — the global 14-entry banned-substring list, copied per people.
- `lexicon: {slot: root}` — the branch's own morpheme table (family roots rewritten by the branch's sound changes, minus `drop_roots`, plus `add_roots` unshifted). 29–34 entries per branch.
- `phonotactics`: `syllable` (template string), `vowel_length` ∈ {none, contrastive}, `hiatus` ∈ {forbidden, allowed, preferred}, `geminates` (**bool**), plus the computed `max_onset_cluster` (int 1 or 2), `max_coda_cluster` (int 1 or 2), `onset_clusters` (sorted list), `coda_clusters` (sorted list), `allowed_clusters` (sorted union).
- `prosody`: `stress` ∈ {penultimate, initial}, `tone` ∈ {none, register}, `word_shape` ∈ {monosyllabic_heavy, disyllabic, trisyllabic_flowing}.
- `morphology`: `compounding` ∈ {light, moderate, heavy}, `honorific` ∈ {age_suffix, title_prefix, rank_suffix, deed_epithet, sacred_prefix, none}, `call_form` (**bool**), plus `word_order` and `affixing` copied from the family, and `patronymic` mapped from the culture's naming pattern.
- `orthography`: `script_form`, `direction`, plus `digraphs` (the multi-letter consonants actually in the inventory, in alphabet order).
- `rates`: `third_syllable`, `cluster`, `loanword` — **doubles in [0,1]**, the only numbers in the genome.

**`appearance`** — 8 blocks, described fully under `constants`/`algorithm`. Numbers permitted here and nowhere else in the package.

**`provenance.overridden`** — a flat list of dotted paths, culture paths first (in override-document iteration order) then genome paths. Empty for a people with no overrides.

**Other writes**
- `heritage_identity(pairs) -> {revision: {5 file revisions}, sha256: 64 hex chars}`. Hashes the **resolved peoples**, not the source bytes: a derivation edit that changes no output invalidates nothing, while a one-character override that does change an output is caught. Nothing gates on this hash — only the registry identity gates age advance — so it reports the change without rejecting anything.
- `revisions() -> {traits, derivation, lexicon, overrides, appearance}` (ints).
- `lexicon() -> the whole lexicon document`, shared, loaded once, not copied into each resolution.
- Naming: `settlement_name -> (Name, "slot-slot")`; `person_name -> Name`; `person_name_with_gloss -> (Name, "slot-slot")`; `realm_name -> Name or ""`; `name_stock -> {common: [(name,gloss)], rare: [(name,gloss)], thresholds: (double,...)}`; `stock_name -> (name,gloss) or None`; `name_table -> {onsets, nuclei, codas, third_syllable_chance}` (a compatibility adapter; see `open_questions`).

**Appearance ships through the catalogue and through `resolve`, and NOT through the world document.** `heritage_of` publishes traits, culture, genome and provenance only, so the `civilizations` block stays at version 3 and no saved world is rejected on age advance. A body is identical in every world from every seed, no generation step reads one, and paying a world-document version bump to copy static data into every save is the wrong trade. A consumer holding a world joins to the catalogue on `civilization_id`.

## Algorithm

## A. Loading (`load` / `load_all`)

1. For each of the five names in fixed order `(traits, derivation, lexicon, overrides, appearance)`, read the packaged file as UTF-8 and parse JSON with two hardenings: **duplicate object keys are a load error** (`duplicate policy key 'x'`), and the JSON constants `NaN`/`Infinity`/`-Infinity` are a load error (`non-finite policy number`).
2. Each document must carry `revision` as an integer `>= 1`, else error.
3. Run the linters, in this order, all of which raise on the first fault with a sentence naming the fault: `lint_traits`, `lint_derivation`, `lint_lexicon(lexicon, traits)`, `lint_overrides(overrides, traits)`, `lint_appearance(appearance, traits)`.
4. Memoise the whole bundle. This does **not** hot-reload (packaged resources carry no stat to key a cache on and the tables are immutable at runtime); a cache-reset entry point exists only for tests.

### A.1 `lint_traits`
For each race base: its key set must equal the 17-axis set exactly (error names the symmetric difference). For each `(axis, value)` in every race base and every people delta, run `check_axis`:
- axis must be a known axis;
- if the axis is list-valued (`craft_focus` is the only one): value must be a list of exactly `CRAFT_COUNT = 2` entries, all distinct, each in CRAFTS;
- otherwise: a bool, int or float is refused with `heritage traits carry no numbers`;
- otherwise the value must be in the axis enum; if the axis is `magic_school` and the value is one of the four hidden schools, the error says so specifically (`claims the hidden school 'rot'; generation can never raise one`).

Note a people delta is **not** required to be complete — that is the whole point of a delta.

### A.2 `lint_derivation`
- `derivation.culture` block set must equal `CULTURE_BLOCKS` exactly, and each block's key set must equal its declared keys exactly.
- Every rule in `culture` and in `genome` goes through `lint_rule`:
  - a rule is a dict of length 1 or 2;
  - `{"copy": axis}` — axis must be a known axis; done;
  - `{"craft": index}` — index must be 0 or 1 (i.e. `< CRAFT_COUNT`); done;
  - `{"from": axis, "map": {...}}` — axis must be known and must **not** be list-valued; the map must contain **every** value of the axis enum (error: `derivation has no rule for 'x'/'y'; every enum value must map`) and **no** value outside it; no mapped value may itself be a dict or list (`derivation nests ... more than two levels deep`); and in the `culture` section a mapped bool/int/float is refused (`heritage culture carries no numbers`).
- Then for each of `RATE_KEYS = (third_syllable, cluster, loanword)`, every value in `genome.rates.<key>.map` must be a non-bool number in `[0, 1]`.

The totality rule is the whole point: adding an enum value to an axis cannot silently produce an empty culture — it fails at load.

### A.3 `lint_lexicon`
- Every family's `parent_race` must be a real race in traits.
- Every family's `roots` must contain all 19 `shared_slots` (error names the missing ones).
- **Spellability**: compute the parent race's phoneme inventory (section B), take the set of *letters* appearing in its consonants and vowels, and require every root to be non-empty, pure ASCII, already stripped, and built only from those letters. A root spelled with a letter its family's soma cannot make is an authoring error, not a quirk.
- Every branch name must be globally unique across all families.
- Every sound change must be a two-element list whose first element is non-empty.
- Every `slot_roles` role must be non-empty.

### A.4 `lint_overrides`
Every civ id named under `culture` or `genome` must be a real people in traits. Every culture override must touch a known block and a known key, must not nest deeper than two levels, and must not be a number or bool.

### A.5 `lint_appearance`
- `appearance.races` key set == `traits.races` key set; `appearance.peoples` key set == `traits.peoples` key set. Both directions.
- Each race base must name exactly the 8 blocks and, per block, exactly that block's keys. Every value goes through `check_appearance_key` (section E.2).
- Each race base is then run through `check_resolved_appearance` (section E.3) against its own traits — a base is complete, so it can be judged now.
- Each people delta may name any subset of blocks and, within a block, any subset of keys, but nothing unknown. Each delta value goes through `check_appearance_key` too (a delta **replaces a key whole**, so a delta value must be a complete valid value for that key). A delta is *not* run through `check_resolved_appearance` here — it cannot be judged until merged.

---

## B. Phoneme inventory (`phonemes.inventory(traits)`)

This is **code, not data**, because it is a physical constraint rather than a taste: a people with blunt dentition cannot make an interdental fricative and no amount of authoring should let one.

Closed alphabet, ASCII digraphs rather than IPA (the repository hashes bytes and disables text normalisation for every provenance destination; a table of multi-byte glyphs would strain that contract for no expressive gain):

```
CONSONANTS = p b t d k g q  m n ng  f v th dh s z sh zh kh gh h  ts ch  l lh r w y   (28)
VOWELS     = a e i o u  aa ee ii oo uu  ae ao ia io ua                               (15)
ALPHABET   = CONSONANTS ∪ VOWELS
CORE_CONSONANTS = p b t d k g m n ng s h l r      (13)
CORE_VOWELS     = a e i o u                       (5)
```

Algorithm:
1. `kept = CORE_CONSONANTS ∪ CORE_VOWELS`; `banned = {}`.
2. For each of the five gated axes — **`dentition`, `vocal_tract`, `hearing_band`, `body_scale`, `breath_capacity`** — look up this people's value in the `GATES` table (section `constants`) and union its `add` into `kept` and its `drop` into `banned`. A value with no rule contributes nothing.
3. `kept -= banned`. **Additions accumulate and removals win**: the forbidden set is a union, so one impossibility is not argued away by another axis that would have permitted it. (Because `kept` and `banned` are sets and the final subtraction is applied once, the result is **order-independent** across axes — but keep the declared axis order anyway for error reproducibility.)
4. If `(kept ∪ banned)` contains anything outside ALPHABET, raise — this can only happen if the GATES table is edited wrong.
5. Emit three lists **in ALPHABET order, not gate-firing order**, because this table is hashed into the heritage identity:
   - `consonants` = CONSONANTS filtered by membership in `kept`
   - `vowels` = VOWELS filtered by membership in `kept`
   - `forbidden` = (CONSONANTS ++ VOWELS) filtered by membership in `banned`

Worked example (dwarf: dentition=blunt, vocal_tract=resonant_chest, hearing_band=low_weighted, body_scale=compact, breath_capacity=deep):
consonants `p b t d k g q m n ng s kh gh h l r`; vowels `a e i o u aa`; forbidden `th dh z sh zh ts ch y`.

---

## C. Culture derivation (`derive_culture(traits, rules)`)

For each culture block, for each key, apply the rule:
- `{"copy": axis}` → `traits[axis]` verbatim.
- `{"craft": i}` → `traits["craft_focus"][i]`.
- `{"from": axis, "map": m}` → `m[traits[axis]]`.

That is all. 24 lookups, no interaction between keys, no feedback. The output preserves the block/key order of the derivation document (irrelevant to correctness; the identity hash sorts keys).

---

## D. Genome derivation (`derive_genome(culture, traits, rules, lexicon, parent_race_id, branch)`)

1. **Find the family** by linear scan of `lexicon.families` for the entry whose `parent_race == parent_race_id`; raise if none. (One family per race; the lint guarantees the mapping is onto.)
2. `(consonants, vowels, forbidden) = inventory(traits)` (section B).
3. Apply the derivation's `genome` rules exactly as in section C, producing `phonotactics`, `prosody`, `morphology`, `orthography`, `rates`.
4. **Syllable template shape.** Split `phonotactics.syllable` at the **first** `V`. `max_onset_cluster` = count of the literal substring `"(C)"` in the head; `max_coda_cluster` = count of `"(C)"` plus count of `"(N)"` in the tail. So `(C)(C)V(C)` is 2 onsets and 1 coda, `(C)V(C)(C)` is the reverse, and **`N` counts as a coda**. Across the shipped eight environments: only `open_plain` and `market_din` give onset 2; only `wind_scoured` gives coda 2; every template yields coda ≥ 1, so `closes` is always true today.
5. **Cluster construction** (`_clusters(consonants, onset_max, coda_max)`). Build plausible clusters from what the inventory actually holds. Partition the inventory:
   - `stops` = `[p b t d k g q]` ∩ inventory, in that order
   - `voiceless` = `[p t k]` ∩ inventory
   - `liquids` = `[l lh r]` ∩ inventory
   - `nasals` = `[m n ng]` ∩ inventory
   
   Onsets (only if `onset_max > 1`):
   - every `stop + liquid`, **excluding** `t`/`d` before `l`/`lh` — `tl` and `dl` are the two stop-liquid pairs most tongues refuse;
   - if `s` is in the inventory, every `s + voiceless`.
   
   Codas (only if `coda_max > 1`):
   - every `liquid + voiceless`;
   - if `s` is present, every `liquid + s`;
   - **homorganic nasal-stop only**: `m` before `p`/`b`, `n` before `t`/`d`, `ng` before `k` — each conditional on both members being in the inventory.
   
   Return each as `sorted(set(...))`. **Why these shapes and not others** (carried verbatim from the source): onsets and codas are built separately because they are not interchangeable — a sequence that opens a syllable rarely closes one. Letting any fricative lead produced onsets like `kht`; pairing any nasal with any stop produced codas like `nb` and names like `Pimd` and `Wongd`. Both are the kind of form a real tongue would have levelled long ago. `ng` takes only the voiceless member because `ng + g` renders as the unreadable `ngg`.
   
   For the shipped data this yields, for onset-2 tongues: `bl br dr gl gr kl kr pl pr sk sp st tr`; for coda-2 tongues: `lk lp ls lt mb mp ngk nt rk rp rs rt`.
6. Write into `phonotactics`: `max_onset_cluster`, `max_coda_cluster`, `onset_clusters`, `coda_clusters`, `allowed_clusters = sorted(onset ∪ coda)`.
7. Write into `morphology`: `word_order` and `affixing` copied from the family entry, and `patronymic` from a fixed 6-entry table keyed on `culture.household.naming_pattern`: `given_plus_father→patronymic`, `given_plus_clan→clan_name`, `given_plus_house→house_name`, `given_plus_cohort→cohort_name`, `given_plus_vessel→vessel_name`, `given_plus_workshop→workshop_name`. **This is the only place the genome reads the culture** — everything else in the genome reads the traits directly.
8. Write `orthography.digraphs` = the multi-letter consonants in the inventory, in inventory order.
9. Assemble the result: `family`, `branch`, `inventory` (with `onsets` = consonants minus `ng` — *`ng` is a fine coda and a poor word onset in most tongues; without this, dwarven produced names like `Ngukhuls`*), `sound_changes` (a copy of the branch's rules — *carried so coined words undergo the same shifts the inherited roots did; without it, two siblings with the same soma generate identical personal names, which is exactly what `human_large_island` and `human_rainforest` did before this line*), `forbidden_forms` (the global list), `lexicon` (section D.1), then the five derived blocks.

### D.1 Branch lexicon (`branch_lexicon(lexicon, family, branch)`)
1. Take the family's `roots`. If the branch is unknown, use an empty spec (`sound_changes: []`, `add_roots: {}`).
2. For each `(slot, root)` **in family-root insertion order**, skip it if the slot is in the branch's `drop_roots`; otherwise apply the branch's ordered sound changes to the root and keep the result.
3. Then `update` with the branch's `add_roots` **verbatim, unshifted**. *Innovations are added after the changes rather than before: a word the branch invented was not present when the sound shift ran, which is why gnomish keeps `spril` intact.*

Worked example — dwarven `stone = khas`: `dwarf` (no rules) → `khas`; `frosthold_dwarf` (kh→q, nd→nt, a→o) → `qos`; `gnome` (kh→k, q→k, gh→g, rn→rl, aa→a) → `kas`; `hill_dwarf` (kh→h, q→k, gh→g, u→o) → `has`.

### D.2 Sound changes (`apply_sound_changes(word, changes)`)
For each `(old, new)` **in order** — rules may feed each other, and `[k→h, h→g]` on `kal` gives `gal` while `[h→g, k→h]` gives `hal` — run one non-overlapping left-to-right pass:

```
index = 0; out = ""
while index < len(word):
    pair = word[index : index+2]
    if pair is a DIGRAPH and old != pair:
        out += pair; index += 2; continue      # consume the digraph whole
    if word starts with `old` at index:
        out += new; index += len(old); continue
    out += word[index]; index += 1
word = out
```

`DIGRAPHS` = every token in CONSONANTS ++ VOWELS of length > 1 (20 of them: aa ae ao ch dh ee gh ia ii io kh lh ng oo sh th ts ua uu zh).

*A naive string replace reads the `s` inside `sh` as an `s`, so the rainforest rule s→sh turned `mush` into `mushh`. Scanning left to right and consuming a digraph whole makes a rule apply to the phoneme it names and to nothing else.* Note the `old != pair` escape: a rule whose `old` **is** the digraph (gnomish `aa→a`) still fires on it.

---

## E. Resolution (`resolve(civ_id, parent_race_id)`)

Memoised internally on `(civ_id, parent_race_id)`; the public entry point returns a **deep copy** so a caller mutating the result cannot poison the cache.

1. Raise if `parent_race_id` is not a race in traits (message must contain `parent race`).
2. `base = traits.races[parent_race_id]`; `delta = traits.peoples.get(civ_id, {})`.
3. `traits = merge_traits(base, delta)` — **shallow, key by key**. An axis has no internal structure, so there is nothing to merge. Two errors: an axis not present in the base is unknown; an axis whose delta value **equals** the inherited value is an authoring error (`overrides axis 'x' with its inherited value`) — the delta is meant to read as the difference from its archetype.
4. `appearance = merge_appearance(appearance.races[parent_race_id], appearance.peoples.get(civ_id, {}))` — **depth two**: a block, then a key; a key's value replaces its inherited value whole (deep-copied). *Deeper than `merge_traits` because a body has internal structure — a height band is four numbers and a palette is a list of swatches — and no deeper than two, which is the same bound the override merge keeps so the merge rule stays statable in one sentence.* Same two errors: unknown block/key, and restating an inherited value.
5. `check_resolved_appearance(appearance, traits, label)` — section E.3. Run **here**, on the merged body, because a sparse delta alone cannot be measured against an envelope. One implementation, two call sites (load for race bases, resolve for peoples); *the alternative is a second merge in the policy module, which is exactly the drift a sparse layer invites.*
6. `culture = derive_culture(traits, derivation)`, then `culture, culture_touched = apply_overrides(culture, overrides.culture.get(civ_id, {}), civ_id)`.
7. `genome = derive_genome(culture, traits, derivation, lexicon, parent_race_id, civ_id)` — **the branch is the civ id**, then `genome, genome_touched = apply_overrides(genome, overrides.genome.get(civ_id, {}), civ_id)`.
8. Assemble the resolved people (shape in `writes`), with `provenance.overridden = ["culture."+p for p in culture_touched] + ["genome."+p for p in genome_touched]`.

### E.1 `apply_overrides(derived, patch, label)`
Deep-copy the derived object. For each block, then each key (both must already exist, else error):
- if the patch value is a **dict**, it is an inventory patch — hand it to `patch_inventory` (below);
- otherwise, if it equals the current value, raise `restates the derived value at block.key`;
- otherwise assign it.
Record `"block.key"` in `touched`, in patch iteration order.

`patch_inventory(derived_list, {add: [...], drop: [...]})`:
- `add ∩ drop` non-empty → error naming the first overlapping token (sorted);
- any token outside ALPHABET → error;
- a `drop` token not present in `derived_list` → error (`drops phoneme 'zh' the derivation never produced`);
- an `add` token already present → error;
- result = `(derived ∪ add) − drop`, re-emitted **in CONSONANTS++VOWELS order**, not in patch order.

The only shipped genome override is `tidekin.inventory.consonants: {add: [lh], drop: [p, g]}`.

### E.2 `check_appearance_key(block, key, value, label)` — structural validation, dispatched on block, then key
- **frame**: `height_m` / `mass_kg` must be a dict of exactly `{male, female}`, each a **band**; `sexual_dimorphism` must be in the closed set `{minimal, low, moderate, pronounced}`; anything else (`build`) is a bare token.
- **proportion**: `heads_tall` bounded `[3.0, 9.0]`; every other key bounded `[0.05, 0.95]`. *Head-units and body ratios: what a rig and a sprite sheet are actually built on.*
- **coloration**: exactly `{note, swatches}`; `note` is prose (≥ 20 trimmed chars); `swatches` is a **palette**.
- **grooming**: `common_styles` is a non-empty duplicate-free token list; `facial_hair` must be in the closed set `{none, sparse, male_only, male_full, male_full_female_sparse, full_universal}`; anything else is a bare token.
- **life_stages**: `max_years` bounded `[1, 5000]`; every other key is a dict of exactly `{onset_years, height_fraction, read}` with `onset_years ∈ [0,5000]`, `height_fraction ∈ [0.2,1.0]`, and `read` a non-empty non-blank string saying what visibly changes at that stage.
- **attire**: `materials` is a token list; everything else a bare token.
- **features**: exactly `{form, note}` — `form` a bare token, `note` prose. *They are one value, not two: a parallel prose block beside a token block is two registries of the same fact, and they drift.* `broad_high_bridge` tells a concept artist nothing about a dwarf's nose.
- otherwise (**art_direction**): `avoid` is a token list; `silhouette` and `reads_as` are prose.

`band(v)`: exactly `{min, mean, max, sd}`, all finite; requires `0 < min <= mean <= max`; requires `sd > 0` (*a per-NPC draw needs a spread*).
`palette(v)`: a list of at least 3 entries, each exactly `{name, hex, weight}` with `name` a bare token, `hex` matching `^#[0-9a-f]{6}$`, `weight` finite and `> 0` (*drop the swatch instead*); names must be unique; weights must sum to `1.0` within `1e-6`.

Two closed vocabularies only — `facial_hair` and `sexual_dimorphism` — *because a wrong value in either produces the wrong art silently rather than a missing field. Every other descriptive key stays open: appearance vocabulary is art direction and grows with the world, so it is held to a bare snake_case token — usable as a prompt fragment, enumerable later — and to nothing further. Closing them all would mean a code edit for every new nose.*

### E.3 `check_resolved_appearance(appearance, traits, label)` — cross-layer agreement
1. **Height envelope.** `floor, ceiling = BODY_SCALE_HEIGHT_M[traits.body_scale]`. For each sex, require `band.min >= floor` and `band.max <= ceiling`, else raise naming both the scale and the offending band.
2. **Stage order.** `life_stages.child.onset_years` must be exactly 0; the four onsets must be **strictly increasing**.
3. **Growth.** `child < adolescent < adult` on `height_fraction`, strictly.
4. **Adult is the reference.** `life_stages.adult.height_fraction` must be exactly `1` — a child sprite scales off this, and every band is stated at adult stature.
5. **Death after elderhood.** `max_years > elder.onset_years`.
6. **Lifespan tempo envelope.** `low, high = LIFESPAN_TEMPO_YEARS[traits.lifespan_tempo]`; require `low <= max_years <= high`. *An ancient people that dies at ninety is an authoring slip, not a design decision.*

---

## F. Naming

All naming takes a caller-supplied `draw` exposing `random()` and `choice(seq)`. Heritage carries no seed logic.

### F.1 The seam (`_join(first, second, genome)`)
Smooth a compound seam under the tongue's own rules. If either side is empty, return the other. Otherwise, **in this order**:
1. `first[-1] == second[0]` → a doubled letter across a seam is a geminate; if `phonotactics.geminates` is true keep both, else drop one (`first + second[1:]`).
2. Both sides vowels at the seam (first char in `aeiou`) **and** `hiatus == "forbidden"` → drop the first's final vowel (`first[:-1] + second`).
3. `first[-1] + second[0]` spells a DIGRAPH **and** `len(first) > 2` → drop the first's final consonant. *A seam can spell a digraph its two halves never contained: `ulk` + `hakn` reads as `ul-khakn`, inventing a `kh` neither root has and mis-teaching the reader where the compound breaks. Drop the first root's final consonant rather than let the seam lie.* (`ulk`+`hakn` → `ulhakn`; `fros`+`hal` → `frohal`.)
4. Otherwise concatenate.

The `len(first) > 2` guard stops a two-letter first element being eaten to one.

### F.2 Weighted template pick (`_weighted(templates, draw)`)
`total = Σ weight`; `cut = draw.random() * total`; walk the templates **in document order** accumulating; return the first whose running total exceeds `cut`; fall through to the last. Exactly **one** `random()` call.

### F.3 Banned-form filter (`_acceptable(word, forms)`)
Lowercase the word; reject if **any** banned form is a substring. *Not fastidiousness: a prototype of this design generated `Person` for a cold human from `per` + `son`, and `Mordor` for a hill dwarf from `copper` + `gate`. Innocent roots collide into loaded words, and a generated world produces far more names than anyone reviews. The check is a substring match because the compound, not the root, is where the accident happens.*

### F.4 Settlement names (`settlement_name(resolved, lexicon, draw, terrain_slot=None)`)
Loop at most `REDRAW_LIMIT = 8` times:
1. Pick a settlement template by weight.
2. For each role in the template pattern, in order, with its index:
   - `options` = the slots `slot_roles[role]` **that this branch's lexicon actually holds**, in `slot_roles` document order; if that is empty, fall back to `sorted(all roots)`;
   - if this is index 0, the role is `terrain`, and the caller's `terrain_slot` is among the options, use it (the terrain bias); otherwise `draw.choice(sorted(options))`.
3. Fold the chosen roots left to right through `_join`.
4. If the result passes `_acceptable`, return `(word.capitalize(), "-".join(chosen_slots))`.

If all 8 attempts fail, **return the last attempt anyway** — bounded, so naming always terminates. Reaching there means the lexicon is small enough that the filter cannot be satisfied, which is an authoring problem.

Examples: dwarf → `Bargdorn` (wood-hold), `Ulhakn` (old-shore); frosthold_dwarf under the same draws → `Borgdorn`, `Ulhokn`; elf → `Lainduin` (green-river); human_maritime → `Halmar` (wood-harbour).

### F.5 Dithematic personal names
`_pick_elements(resolved, lexicon, draw)`: pick a **personal** template by weight; for each of its two roles, build `options` as above but **return empty on an empty option list** (no fallback); `draw.choice(sorted(options))`. If the two chosen slots are identical, return empty — a personal name never repeats one element.

`_compose(resolved, slots)`: fold through `_join`, capitalize.

`person_name_with_gloss(resolved, lexicon, draw)`: pick elements; if empty, fall back to the phonotactic path and an empty gloss; else return `(composed, "slot-slot")`.

`person_name(resolved, lexicon=None, draw=None)`: if `draw` is None the two trailing arguments are **swapped** so the legacy two-argument form `person_name(resolved, draw)` still reaches the phonotactic path. Then loop up to `REDRAW_LIMIT = 8`: draw a candidate (dithematic if a lexicon was supplied, phonotactic otherwise); return it if non-empty and acceptable. On exhaustion return the last candidate, or a fresh phonotactic coinage if that was empty.

*Real names are built from two meaning-bearing elements — Thor-bjorn, Gal-adriel, Ead-weard — and the difference shows: drawing syllables under the phonotactics alone produced pronounceable noise like `Gregaa` and `Penaatang`. Composing a quality with a craft or a terrain gives `Ulkbor` and `Narrik`, which read as names and gloss like the place names do.* The four personal patterns deliberately avoid `terrain + site`, which is what a settlement is, *so a person and a town do not come out looking like the same kind of word* — and no personal pattern contains `site` at all.

### F.6 Phonotactic personal names (`_draw_person`)
1. `count = {monosyllabic_heavy: 1, disyllabic: 2, trisyllabic_flowing: 2}[prosody.word_shape]`. (Note: trisyllabic starts at **2**, not 3 — the third syllable comes from the rate.)
2. One `random()`; if it is below `rates.third_syllable`, `count += 1`.
3. `onset_cluster = max_onset_cluster > 1`; `closes = max_coda_cluster > 0`.
4. For each syllable index:
   - `mode = "none"` if it is not the last syllable, or if the tongue does not close;
   - else `"required"` if `count == 1` or the shape is `monosyllabic_heavy`;
   - else `"optional"`.
   
   *Only the final syllable closes, so a medial coda never meets the next onset and builds a cluster the tongue does not permit. A one-syllable name must close, or a heavy monosyllabic tongue produces bare `Na`.*
   - Build the syllable (below), then `_join` it onto the word so far.
5. Run the branch's own sound changes over the whole coined word, then capitalize. *A sound change is a property of the tongue, not of the inherited vocabulary: a speaker who cannot say `th` cannot say it in a new name either. Applying the chain here is what keeps two siblings that share a soma from generating the same names.*

`_syllable(genome, draw, allow_onset_cluster, coda_mode)`:
- **Onset**: if `allow_onset_cluster` and the onset-cluster list is non-empty and `random() < rates.cluster`, `choice(onset_clusters)`; else `choice(inventory.onsets or inventory.consonants)`.
- **Nucleus**: `_nucleus` (below).
- **Coda**: if `coda_mode == "none"`, stop. If `coda_mode == "optional"` and `random() >= 0.55`, stop (i.e. an optional coda appears 55% of the time). Then: if the coda-cluster list is non-empty and `random() < rates.cluster`, `choice(coda_clusters)`; else `choice([c for c in consonants if len(c) <= 2])` — a filter that is a no-op on the current alphabet, since no consonant token exceeds two letters.

`_nucleus(genome, draw)`: `simple` = one-letter vowels (or all vowels if there are none); `complex` = multi-letter vowels. `share = 0.15`, raised to `0.3` if `vowel_length == "contrastive"`, then to `0.4` if `hiatus == "preferred"` (the second test overwrites the first). If `complex` is non-empty and `random() < share`, `choice(complex)`, else `choice(simple)`. *Simple vowels carry most syllables; complex nuclei are a seasoning, not a staple. Drawing uniformly from the whole vowel set gave elvish `Paemuayuan` — every nucleus a diphthong, which no tongue does. The complex share is raised only where the genome says the speakers hold vowels: contrastive length, or a habit of hiatus.*

### F.7 Realm names (`realm_name(resolved, capital_name)`)
Look up the `realm` root in the branch lexicon. If absent, or `capital_name` is empty, **return the empty string** — which lets a caller keep its English form rather than invent one. Otherwise `_join(capital_name.lower(), root, genome).capitalize()`. *`Bargdorn` + dwarven `rik` gives `Bargdornrik`. Whether a doubled letter survives is the tongue's decision and not this function's: `Bor` + `rik` is `Borik` in gnomish and hill-dwarvish, which have no geminates, and `Borrik` in dwarvish and frostholder, whose stone-hall and wind-scoured acoustics do.*

### F.8 `name_table(resolved)` — legacy adapter
`onsets` = every `consonant + vowel` pair from the **first 12** consonants and the **first 3** vowels, deduplicated and sorted; `nuclei` = the vowel list; `codas` = consonants of length ≤ 2, deduplicated and sorted, **plus two empty strings** if `hiatus == "preferred"` (so an open syllable is twice as likely for those tongues); `third_syllable_chance` = `rates.third_syllable`. *A compatibility bridge, not the destination.*

---

## G. The name stock — which names a people actually uses

`person_name` says what a tongue *can* build; it does not say what a people calls its children, and drawing uniformly from the first answers the second badly. A heartland genome composes 88 names; drawn uniformly each lands on about one person in ninety, so nobody is common and — the part that costs more — **nobody is unusual either**. A rare name cannot mark a foreigner, an old family or an affectation when no name is rare. At roster scale (≈13,000 people in a size-65 world) that reads wrong in both directions.

The whole stock is **pure and seedless**: one people yields one stock in every world. Rank must be a property of the culture, not of a seed — `Eldsel` is a common heartland name in every world the way John was common in every English county — and this package carries no seed logic by design, which rules out permuting the order with one.

### G.1 Rare patterns (`_rare_patterns(lexicon)`)
`roles = sorted(slot_roles)` → `[craft, quality, site, terrain]`. `spent` = the pattern tuples of **both** the personal templates and the settlement templates. Return every ordered pair `(a, b)` with `a != b` and `(a,b) ∉ spent`, in nested `for a in roles: for b in roles` order. For the shipped lexicon that is exactly six: `(craft,terrain) (quality,site) (site,craft) (site,quality) (site,terrain) (terrain,quality)`.

*A rare name is still two meaning-bearing roots joined. It has to be: the phonotactic path coins `Wopagup` and `Yoroochaat`, which is the reason the dithematic path exists, and a tail built from it would read as generator noise exactly where a reader is meant to notice the name. What makes these rare is the pairing. Both authored sets are removed: pairs already spent on personal names are what common means, so they cannot also be rare; pairs spent on settlements would make a person read as a town.*

### G.2 Enumeration (`_enumerate(resolved, lexicon, [(pattern, weight), ...])`)
For each `(pattern, weight)`: `heads = sorted(slots of pattern[0] present in this branch's lexicon)`, `tails = sorted(slots of pattern[1] present)`. For every `first` in heads × every `second` in tails, skipping `first == second`: compose the name; skip it if it fails `_acceptable`. Key the result table **on the finished name**, because composition collides — two different root pairs can join to one string once the seam rules have had their say. Collision rule: **the first reading wins and the highest weight wins** — on a collision, keep the previously-stored gloss and slots but raise the stored weight to the new one if it is larger. *A name a common template can reach is a common name however else it can also be reached.*

Value is `(gloss_string, weight, (first_slot, second_slot))`.

### G.3 Rank order (`_ordered`)
Sort the enumerated entries by `(-weight, len(name), name)`. **Authored template weight first, then length, then spelling.** *Template weight is authored and already says which shapes a people reaches for. Length breaks its ties, because the names that wear into everyday use are the short ones, and the spelling breaks what is left, so the order is total and the stock is reproducible.* The key is a total order, so no dictionary or set iteration order can reach the output here.

### G.4 Head spreading (`_ranked`)
`HEAD_SPREAD = 5`. Walk the rank order once, maintaining `spent` (the set of slots already used by the head):
- if the head holds fewer than 5 entries **and** this name's two slots are both unused, append to the head and mark both slots spent;
- otherwise append to the body.
Result = head ++ body. **Fallback:** if the head ended up shorter than `min(HEAD_SPREAD, len(ordered))` — i.e. the space was too inbred to fill five root-disjoint names — discard the spread and return the plain rank order whole.

*Ranking on length alone put the whole heartland head on one root: `Eldsel`, `Frosel`, `Nersel`, `Versel`, and the elves on `-naur` four times over. That is worse than the uniform draw it replaces. Four fifths of a people would share not just a few names but a few sounds, and a reader meeting them in succession hears a stutter rather than a culture. Real stocks do not behave that way: John, William, Thomas, Richard and Robert have nothing in common, because a name that cannot be told from the last one is a name that stops being used.* What is skipped is not discarded — it falls to the body of the stock, in its own rank order.

### G.5 Thresholds (`_thresholds(count)`)
Cumulative draw thresholds over `count` ranks.
- `count <= 0` → empty.
- `shares = HEAD_SHARES[:count]` (the tabulated head).
- If `rest = count - len(shares) > 0`: `remainder = 1 - Σshares` (= 0.21 for a full head); `harmonic = Σ_{r=1..rest} 1/r^ZIPF_EXPONENT`; append `remainder / r^ZIPF_EXPONENT / harmonic` for `r = 1..rest`. A plain 1/r tail.
- Normalise by the total and emit the running cumulative sums.
*Normalised at the end so a stock smaller than the head table still sums to one rather than silently dropping its last ranks below the last threshold.* The last threshold is always exactly (to within float error) 1.0. Shipped shape for a large stock: `0.35, 0.52, 0.64, 0.73, 0.79, …, 1.0`.

### G.6 `name_stock(resolved, lexicon)`
```
common = _ranked(_enumerate(resolved, lexicon, [(t.pattern, t.weight) for t in personal_templates]))
rare   = _ranked(_enumerate(resolved, lexicon, [(p, 1) for p in _rare_patterns(lexicon)]))
rare   = [e for e in rare if e.name not in {n for n,_ in common}]   # a rare pairing that
                                                                   # composes to a common name is not rare
return {common, rare, thresholds: _thresholds(len(common))}
```
Shipped sizes: heartland 88 common / 216 rare; elf 112/210; dwarf 130/241; gnome 184/285. **Build once per civilization** and hand it to `stock_name` per person — enumerating the whole reachable space is cheap but not free.

### G.7 `stock_name(stock, draw)`
```
roll = draw.random(); cut = draw.random()          # ALWAYS TWO DRAWS, whichever branch is taken
if common is empty: return None
if rare is non-empty and roll < RARE_SHARE:
    return rare[ min(int(cut * len(rare)), len(rare)-1) ]     # uniform over the tail
for index, threshold in enumerate(thresholds):
    if cut < threshold: return common[index]
return common[-1]
```
*Two draws whatever the outcome, so the stream a caller sees does not depend on which branch was taken and adding the rare tail cannot desynchronise a replay from itself.* This is asserted by a dedicated test and is **load-bearing**: a branch that spends a different number of draws desynchronises a replay.

*An empty common stock means a family whose roots reach none of the personal templates, which is an authoring failure rather than a naming one. The rare pairings may still compose, but promoting them would make the tail the whole tongue and hide the authoring fault behind names that look fine. Refuse the whole stock* and let the caller fall back visibly.

Measured result of the whole stock layer (heartland, 4000 draws): commonest name 34.3% vs 3.3% drawn flat; top five 76.6% vs 31%; effective number of names 5.8 vs 49. The historical target is 35% / 79% / ~6.

---

## H. Identity (`heritage_identity(pairs)`)
`pairs` is an iterable of `(civilization_id, parent_race_id)` — passed in rather than looked up, so this package stays a leaf. Sort the pairs, resolve each into a map keyed by civ id, serialise with **sorted keys, no whitespace separators, non-ASCII left literal**, and return `{revision: <the five file revisions>, sha256: <hex digest of the UTF-8 bytes>}`.

*Hash the resolved peoples, not the source bytes. A derivation edit that changes no output then invalidates nothing, while a one-character override that does change an output is caught.*

## Constants

### Cardinalities and limits
- `CRAFT_COUNT = 2` — `craft_focus` holds exactly two distinct crafts. Changing it changes the lint, the `{"craft": i}` rule range, and the authored data.
- `POLICY_FILES = (traits, derivation, lexicon, overrides, appearance)` — order matters only for the `revisions` map's key order.
- `REDRAW_LIMIT = 8` (naming.py) — bounded redraw for both settlement and personal names against the forbidden-form filter. Raise it and pathological lexicons spin longer; lower it and banned forms leak. Reaching the limit is an authoring signal, not a runtime failure.
- The settlement-name **collision** redraw in the caller is also 8 attempts, varying the seed's third parameter — a separate, unrelated 8.
- `PROSE_MIN = 20` — minimum trimmed length of any appearance `note`, `silhouette` or `reads_as`. *Long enough that a token cannot pass as a description.* Lower it and `"big nose"` ships.
- `PALETTE_MIN = 3` — minimum swatches per coloration channel.
- `WEIGHT_TOLERANCE = 1e-6` — palette weights must sum to 1 within this. *Population frequencies are authored to two decimals; the tolerance is for float addition, not for a palette that nearly sums to one.*
- `TOKEN = ^[a-z][a-z0-9_]*$`; `HEX_COLOUR = ^#[0-9a-f]{6}$`.

### Physical envelopes (policy.py, deliberately beside the phoneme gates)
`BODY_SCALE_HEIGHT_M` — (floor, ceiling) in **metres** that an authored height band must lie inside:
```
small     (0.92, 1.26)
compact   (1.10, 1.34)
mid       (1.38, 2.05)
tall      (1.55, 2.20)
towering  (2.00, 3.20)
```
*These are physical constraints rather than authoring taste, so they live here beside the phoneme gates instead of in the table they police: a subrace cannot be handed a stature its own key trait contradicts.* The `compact` ceiling 1.34 and the `small` ceiling 1.26 are together what make **"no dwarf stands over 4 ft 5 in" (1.3462 m)** a property of the loader rather than a promise in a comment — all four peoples under the dwarf parent race are `compact` or `small`. Raise either ceiling and that guarantee silently dies; a dedicated test restates it in metres so an envelope edit fails loudly.

`LIFESPAN_TEMPO_YEARS` — (low, high) in **years** that `life_stages.max_years` must lie inside:
```
brief   ( 55,  120)
human   ( 70,  160)
long    (180,  520)
ancient (500, 1600)
```
*An ancient people that dies at ninety is an authoring slip, not a design decision, and the tempo axis is what a name, a culture and an elder sprite are all already keyed on.*

### Closed appearance vocabularies (only two)
`FACIAL_HAIR = (none, sparse, male_only, male_full, male_full_female_sparse, full_universal)`
`SEXUAL_DIMORPHISM = (minimal, low, moderate, pronounced)`
Everything else descriptive is an open bare token. *Closing them all would mean a code edit for every new nose.*

### Block layouts
`APPEARANCE_BLOCKS` (ordered from what a modeller needs first to what a concept artist needs last):
`frame` (height_m, mass_kg, build, sexual_dimorphism) · `proportion` (heads_tall, shoulder_width_ratio, leg_length_ratio, hand_length_ratio) · `coloration` (skin, hair, eye) · `features` (face_shape, ear, eye_shape, nose, brow, jaw, lips, teeth, skin_texture, markings) · `grooming` (scalp_texture, facial_hair, body_hair, common_styles) · `life_stages` (child, adolescent, adult, elder, max_years) · `attire` (layering, materials, dye_character, signature_item, footwear) · `art_direction` (silhouette, reads_as, avoid).
`BAND_KEYS = (min, mean, max, sd)` · `FEATURE_KEYS = (form, note)` · `PALETTE_KEYS = (note, swatches)` · `SEXES = (male, female)` · `LIFE_STAGES = (child, adolescent, adult, elder)` · `STAGE_KEYS = (onset_years, height_fraction, read)`.
`CULTURE_BLOCKS` and `GENOME_DERIVED_BLOCKS = (phonotactics, prosody, morphology, orthography, rates)` as listed in `writes`.

### Phoneme gates (`phonemes.GATES`) — the physical table, verbatim
Five gated axes in this declared order: `dentition`, `vocal_tract`, `hearing_band`, `body_scale`, `breath_capacity`.
```
dentition
  blunt             drop th dh
  even              add  th f v
  prominent_canine  add  f v          drop dh
  tusked                              drop f v th dh
vocal_tract
  narrow            add  ts ch sh y   drop q kh gh
  even              add  f v w y
  broad             add  kh gh w
  resonant_chest    add  q kh gh r    drop y
hearing_band                       # a tongue shaped for low frequencies sheds the contrasts
  low_weighted                        drop sh zh ts ch z   # its listeners cannot separate;
  flat              add  z                                 # one sibilant survives to carry
  high_weighted     add  s z sh ts ch                      # the whole series
  wide              add  s z sh ts ch zh
body_scale
  small             add  ii
  compact           (nothing)
  mid               (nothing)
  tall              add  ae ee
  towering          add  ao uu
breath_capacity
  short             (nothing)
  steady            (nothing)
  deep              add  aa
  sustained         add  aa ee oo ia io ua
```
Removals win over additions, always. Adding a gate that drops a core consonant can empty an inventory — nothing guards against that except the lexicon spellability lint, which will fail first.

### Cluster shapes (hard-coded in `_clusters`)
`stops = p b t d k g q` · `voiceless = p t k` · `liquids = l lh r` · `nasals = m n ng`.
Onsets: stop+liquid **minus** `t/d` + `l/lh`; plus `s` + voiceless.
Codas: liquid+voiceless; liquid+`s`; homorganic nasal+stop only — `m`+{p,b}, `n`+{t,d}, `ng`+{k}.

### Naming numbers
- `_syllable` optional-coda gate: `random() >= 0.55` skips the coda — **an optional coda appears 55% of the time**. Raising it makes names more open; it is the only bare probability in the syllable builder not sourced from the genome.
- `_nucleus` complex-vowel share: base `0.15`; `0.3` if `vowel_length == contrastive`; `0.4` if `hiatus == preferred` (the hiatus test runs second and wins). Raise the base and every tongue turns into `Paemuayuan`.
- `_draw_person` syllable counts: `monosyllabic_heavy → 1`, `disyllabic → 2`, `trisyllabic_flowing → 2` (+1 with probability `rates.third_syllable`).
- `name_table` slicing: first **12** consonants × first **3** vowels.

### Stock numbers
- `HEAD_SHARES = (0.35, 0.17, 0.12, 0.09, 0.06)` — the five commonest names' population shares, summing to **0.79**. *In the English poll tax returns of the late fourteenth century one man in three was a John and the five commonest names covered about four fifths of all men, with a long tail of forms borne by one person each. These shares are those figures rounded. They are **tabulated rather than fitted** because the measured curve is steeper at the head than any single Zipf exponent reproduces: an exponent steep enough to put one name at 35% still leaves the top five short of 79%.* Flatten these and the stock reverts to the failure it replaced.
- `ZIPF_EXPONENT = 1.0` — what the ranks below the head divide the remaining `0.21` by. A plain 1/r.
- `RARE_SHARE = 0.025` — how often a name comes from outside the common stock. *Small on purpose: this is the tail that makes rarity mean something, not a second stock.* Above roughly 0.05 the tail stops reading as a tail.
- `HEAD_SPREAD = 5` — how many of the commonest names are held root-disjoint from one another. *The head carries about four fifths of a people, so these are the names a reader meets over and over.*

### Derivation rate tables (the only numbers the derivation emits)
`third_syllable` by `breath_capacity`: short 0.1, steady 0.35, deep 0.3, sustained 0.7.
`cluster` by `speech_environment`: open_plain 0.3, surf 0.05, forest 0.15, stone_hall 0.2, wind_scoured 0.55, humid_canopy 0.05, market_din 0.4, terraced_valley 0.2.
`loanword` by `outsiders`: hostile 0.05, wary 0.15, transactional 0.3, hospitable 0.3, assimilating 0.45. (`loanword` is emitted and **nothing currently reads it**.)

### Caches
`_document` and `lexicon` are single-entry memoised; `_resolve` is memoised with **capacity 64** (twelve peoples plus synthetic test entities fit comfortably). Eviction changes nothing but speed.

## Data files

Five JSON documents under `Sim/heritage/policies/`, loaded as package resources (`importlib.resources`), not from a path — so they are compiled into the distribution. **For the C++ port they should be embedded as resources or parsed by a JSON reader the library does not yet have** (see `cpp_notes`).

---
### 1. `traits.json` — 5,862 bytes, revision 1
`{schema: "fantasy-world-generator.heritage-traits", revision, notes, races: {3}, peoples: {12}}`

Selection: `races[parent_race_id]` by exact key (missing → raise); `peoples[civ_id]` by exact key (missing → empty delta).

Race bases, verbatim:
```
human:  body_scale mid, vocal_tract even, breath_capacity steady, hearing_band flat,
        lifespan_tempo brief, dentition even, speech_environment open_plain,
        carrying_register conversational, writing_surface inked_hide,
        kinship nuclear_household, authority hereditary_lord, mobility frontier_expanding,
        memory_mode written_archive, craft_focus [husbandry_and_tillage, wood_and_weave],
        outsiders transactional, magic_stance scholarly_licensed, magic_school radiant
elf:    body_scale tall, vocal_tract narrow, breath_capacity sustained,
        hearing_band high_weighted, lifespan_tempo ancient, dentition even,
        speech_environment forest, carrying_register quiet, writing_surface carved_wood,
        kinship lineage_house, authority elder_council, mobility rooted,
        memory_mode sung_lineage, craft_focus [wood_and_weave, herb_and_dye],
        outsiders wary, magic_stance inherited_bloodline, magic_school weave
dwarf:  body_scale compact, vocal_tract resonant_chest, breath_capacity deep,
        hearing_band low_weighted, lifespan_tempo long, dentition blunt,
        speech_environment stone_hall, carrying_register carrying,
        writing_surface incised_stone, kinship extended_clan, authority guild_assembly,
        mobility rooted, memory_mode carved_record,
        craft_focus [metal_and_forge, stone_and_vault], outsiders wary,
        magic_stance ritual_communal, magic_school earth
```
Peoples deltas: `human_heartland`, `elf`, `dwarf` are **empty `{}`** — the base is the unmarked member. The other nine each name 6–13 axes. The resolved `magic_school` per civilization (load-bearing: it seeds ruin leylines) is exactly:
`human_maritime water · human_desert fire · human_cold water · human_large_island air · human_rainforest earth · human_heartland radiant · dwarf earth · elf weave · gnome weave · tidekin water · hill_dwarf earth · frosthold_dwarf water`.
These twelve values were a hand-written dict in `terrain_ruins` and a second, unsynchronised copy in `Core/legacy.cpp`; the migration is guarded by a test holding the twelve verbatim, *because a derived mapping compared against itself proves nothing.* **A change here is a change to the world.**

---
### 2. `derivation.json` — 8,364 bytes, revision 1
`{schema, revision, notes, culture: {6 blocks × 4 keys}, genome: {5 blocks}}`. Every rule is one of `{"copy": axis}`, `{"craft": 0|1}`, `{"from": axis, "map": {every enum value → value}}`. The maps are total by lint. Culture values are always strings; genome values may be strings, booleans (`geminates`, `call_form`) or numbers (`rates` only).

Selection: direct dictionary lookup on the trait value. There is no default branch anywhere, by design.

---
### 3. `lexicon.json` — 5,878 bytes, revision 1
`shared_slots` (19): stone, water, high, deep, wood, field, river, gate, hall, old, north, cold, fire, green, shore, wind, road, house, realm. Every family must carry all nineteen.

Three families:
- **human** (parent `human`, verb_medial, suffixing) — 31 roots incl. `realm: mark`. Branches: `human_heartland` (no changes), `human_maritime` (v→b, th→t, w→v; adds sail=vela, bay=gol), `human_desert` (p→f, th→s, e→i; adds well=kanat, dune=rig, glass=sira), `human_cold` (v→f, ar→or, nd→nn; adds ice=isk, hearth=arn), `human_large_island` (th→t, l→r; adds terrace=dan, kiln=yaki), `human_rainforest` (s→sh, th→t; adds feather=kitza, canopy=napan).
- **elven** (parent `elf`, verb_medial, **prefixing**) — 29 roots incl. `realm: doren`. Branches: `elf` (none), `tidekin` (th→s, sh→s, ts→s, ch→s, z→s, nd→n; adds reef=riel, salt=sael, keel=loa).
- **dwarven** (parent `dwarf`, **verb_final**, suffixing) — 31 roots incl. `realm: rik`. Branches: `dwarf` (none), `frosthold_dwarf` (kh→q, nd→nt, a→o; adds ice=merk, pact=gorn), `gnome` (kh→k, q→k, gh→g, rn→rl, aa→a; adds gear=spril, glass=glis, spark=pik), `hill_dwarf` (kh→h, q→k, gh→g, u→o; adds table=born, orchard=mel, barrow=dolm).

Representative family entry (dwarven roots): stone khas, water sirn, high tor, deep gund, wood barg, field mek, river durn, gate kadr, hall dum, old ulk, north nord, cold krum, fire nar, green leg, shore hakn, wind guld, road rak, house bod, forge bral, hold dorn, iron ang, gold bor, peak garn, king rik, hammer mund, ash grim, salt sulk, vault gok, ember mokh, realm rik.

`forbidden_forms` (14, global, not per-family): person, mordor, hell, arse, damn, shit, piss, crap, slut, rape, nazi, cock, tit, fart.

`personal_templates`: `[quality,craft] w4` · `[craft,quality] w3` · `[terrain,craft] w2` · `[quality,terrain] w2` — **no `site`**.
`settlement_templates`: `[terrain,site] w5` · `[quality,terrain] w3` · `[craft,site] w2`.
`slot_roles`: `terrain` = stone water wood field river shore north wind (8); `site` = hall gate house hold road bridge harbour vault grove forge well terrace keel barrow table (15); `quality` = high deep old cold green long dark light new (9); `craft` = fire gold iron salt glass gear silver sail kiln ember ice spark orchard (13). A role's list is a superset across families — a branch only uses the slots its own root table actually holds, which is why role option lists are filtered on membership in the branch lexicon.

`drop_roots` is read by the code but is used by no branch today and is **not validated by the lint**.

---
### 4. `overrides.json` — 1,106 bytes, revision 1
`culture` patches for five peoples only:
```
frosthold_dwarf: memory.taboo = letting_the_forge_die ; faith.festival = ember_vigil
tidekin:         memory.taboo = naming_the_drowned    ; faith.festival = the_long_ebb
gnome:           memory.taboo = unlicensed_copy       ; faith.festival = patent_day
hill_dwarf:      faith.festival = the_full_table
human_cold:      faith.festival = the_long_night_watch
```
`genome` patches, one only: `tidekin.inventory.consonants = {add: [lh], drop: [p, g]}`.
*Every entry here is a deliberate departure from the derivation; an entry that restates the derived value is a load error, so this file stays a list of real decisions rather than a slowly fossilising copy of the output.*

---
### 5. `appearance.json` — 63,011 bytes, revision 1
`{schema, revision, notes, races: {human, elf, dwarf}, peoples: {all 12}}`. Three complete bodies (8 blocks each) and twelve deltas, of which three (`human_heartland`, `elf`, `dwarf`) are **empty `{}`**. The nine real deltas touch between 14 and 31 keys each; every one of them replaces all three coloration palettes and all three art-direction keys, and most replace the whole attire block. The bulk of the file is English prose.

Representative complete entry — `races.dwarf`, abridged to its shape with a few values kept because the numbers are load-bearing:
```
frame:      height_m {male {min 1.19, mean 1.27, max 1.34, sd 0.045},
                      female {min 1.13, mean 1.21, max 1.29, sd 0.043}}
            mass_kg  {male {57, 68, 84, 7.5}, female {49, 59, 73, 6.5}}
            build "dense_broad"; sexual_dimorphism "low"
proportion: heads_tall 4.6, shoulder_width_ratio 0.31, leg_length_ratio 0.42,
            hand_length_ratio 0.125
features:   ten {form, note} pairs — e.g. nose {form "broad_high_bridge", note "Big, and
            meant to be. ... It is the largest feature a dwarf has, it survives every helm,
            and it is what the face is built around. A dwarf drawn with a small neat nose
            is a short human."}
coloration: skin/hair/eye, each {note, swatches:[{name, hex, weight} ...]} —
            skin = pale_stone #e0c3a8 0.22, ruddy_ochre #c98f68 0.34,
                   deep_umber #8a5a3b 0.28, slate_cast #a3907f 0.16   (sums to 1.00)
grooming:   scalp_texture "coarse_thick", facial_hair "full_universal",
            body_hair "heavy", common_styles [forked_beard, clan_braids, shaved_temples]
life_stages: child {0, 0.42, "..."}, adolescent {14, 0.78, "..."},
            adult {30, 1.0, "..."}, elder {180, 0.97, "..."}, max_years 350
attire:     layering "heavy_layered", materials [boiled_leather, wool_felt, riveted_plate,
            gold_wire], dye_character "earth_dyes_and_bare_metal",
            signature_item "clan_beard_rings", footwear "hobnailed_boots"
art_direction: silhouette "...", reads_as "A walking anvil. ...",
            avoid [tall_slender_limbs, bare_chin, pointed_ears, small_neat_nose,
                   long_leg_line]
```
Note `elder.height_fraction 0.97` — the growth monotonicity check covers only child→adolescent→adult, so an elder may shrink.

Representative delta — `peoples.gnome` replaces all three frame keys, all four proportion keys, all ten features, all three palettes, all four grooming keys, all five life-stage keys and all attire and art-direction keys: a gnome is a near-total repaint of the dwarven base. `peoples.frosthold_dwarf`, at the other end, replaces only `frame.height_m`, `frame.mass_kg`, three features, three palettes, `grooming.common_styles`, four attire keys and the three art-direction keys.

Content invariants the art depends on, asserted by test: elves have pointed ears and a narrow nose; dwarves have rounded ears and a broad nose; **gnomes carry both a dwarf's nose and an elf's ears at once** — that is the entire likeness of a gnome, so the gnome nose `note` must mention the ears, and the gnome `avoid` list must contain `small_ears` and `small_nose`.

## Random streams

**Heritage owns no RNG and derives no seed.** It deliberately carries no seed logic, *which keeps every seeding decision with the subsystem that owns it.* Resolution, derivation, linting, identity hashing and `name_stock` are all completely deterministic and seedless.

The only randomness is in naming, and it comes from a caller-supplied `draw` object with exactly two operations: `random() -> [0,1)` and `choice(sequence)`. The reference callers pass a fresh `random.Random(child_seed(world_seed, domain, variation))` per name or per person, so the stream is per-call and never shared. In `FWGC++` this is `fwg::Rng` (xoshiro256++), constructed via `Rng::child(master, domain, variation)` — **no fourth seed derivation is needed and none must be invented**.

Draw counts, which matter because a caller replaying a world must spend the same stream:

- **`_weighted`** — exactly 1 `random()`.
- **`settlement_name`** — per attempt: 1 `random()` for the template, then for each of the template's 2 roles either 0 draws (the terrain bias fired) or 1 `choice`. So 2–3 draws per attempt, 1–8 attempts. **Data-dependent** on the forbidden-form filter and on whether `terrain_slot` matched.
- **`_pick_elements`** — 1 `random()` + up to 2 `choice`. May terminate early (0 further draws) when an option list is empty.
- **`person_name`** (dithematic) — 1–8 iterations of the above, plus the slot-repeat rejection. **Data-dependent.**
- **`_nucleus`** — 1 `random()` if the complex set is non-empty, then 1 `choice`; or just 1 `choice` if the tongue has no complex vowels. **Note this is itself data-dependent on the inventory**, not only on the draws.
- **`_syllable`** — onset: 1 `random()` (only if onset clusters are permitted and non-empty) + 1 `choice`; nucleus as above; coda: 0 draws if `mode == none`; else 1 `random()` for the optional gate, then 1 `random()` for the cluster gate (only if coda clusters exist) and 1 `choice`.
- **`_draw_person`** — 1 `random()` for the third syllable, then 1–3 syllables. Roughly 3–12 draws. **Data-dependent.**
- **`stock_name`** — **exactly two `random()` calls, always, whichever branch is taken.** This is a hard invariant with its own test (`test_the_draw_is_fixed_width_whichever_branch_it_takes`): *a branch that spends a different number of draws desynchronises a replay*, and the rare tail was added after the common path without moving the stream precisely because both draws happen up front.

`Rng::choice` in `FWGC++` costs one word plus rejections (`below`), while CPython's `choice` costs one or more words through `_randbelow`; and CPython's `random()` costs two 32-bit MT words. None of this needs to match — the port is a clean rewrite and numbers are free — but **the fixed-width property of `stock_name` must be preserved**, and so must "only the final syllable closes", because both are about stream structure rather than stream values.

For a roster of ~13,000 people the per-person cost is 2 draws (stock path) plus, rarely, a full `person_name_with_gloss` fallback. For a world's cities it is 2–3 draws per name with up to 8 collision redraws each on a fresh sub-stream.

## Ordering hazards

Container ordering reaches output in a lot of places here. Every one found:

**Load-bearing, must be reproduced exactly**
1. **`inventory()` output order is the fixed ALPHABET order**, not gate-firing order — the source comment says why: *"Order follows ALPHABET rather than the order gates happened to fire, because this table is hashed into the heritage identity."* `consonants`, `vowels` and `forbidden` are each a filter over the declared `CONSONANTS` / `VOWELS` tuples.
2. **`_patch_inventory` re-emits in CONSONANTS++VOWELS order**, discarding both the derived list's order and the patch's order. A tidekin inventory with `lh` added lands between `l` and `r`, not at the end.
3. **`orthography.digraphs`** is a filter over the inventory's consonant list, so it inherits ALPHABET order.
4. **`inventory.onsets`** is the consonant list minus `ng`, preserving order.
5. **`_clusters` returns `sorted(set(...))`** for both onsets and codas, and `allowed_clusters` is `sorted(onset ∪ coda)`. The build order (stop×liquid, then s+voiceless; liquid×voiceless, then liquid+s, then homorganic nasal+stop) is therefore invisible in the output — but the *set* it builds is order-independent anyway.
6. **`draw.choice(sorted(options))`** — every naming choice sorts its option list first, in `settlement_name` and `_pick_elements`. Without this, the name depends on JSON key order. The unsorted `options` list is built by filtering `slot_roles[role]` in document order, then sorted; the terrain-bias membership test (`terrain_slot in options`) runs against the **unsorted** list, which is harmless since it is a membership test.
7. **`sorted(roots)` fallback** in `settlement_name` when a role has no reachable slot.
8. **`_enumerate` iterates `sorted(heads) × sorted(tails)`**, and the outer loop runs the pattern list in the caller's order. The collision rule (first-seen gloss wins, highest weight wins) therefore depends on pattern order — but `_ordered` re-sorts totally afterwards, so only the *gloss text* of a colliding name can be affected by it, never the rank.
9. **`_ordered` sorts by `(-weight, len(name), name)`** — a **total** order over distinct names, so no dictionary or set iteration order survives into the stock's rank. This is the single most important ordering property in the package: the stock must be identical in every world and every process.
10. **`_rare_patterns` uses `roles = sorted(slot_roles)`** and a nested `for a: for b:` loop, giving a deterministic pattern order independent of JSON key order.
11. **`_ranked` walks the rank order once**, greedily; its head depends on that order and on `spent`, a set used only for membership.
12. **`heritage_identity` uses `sorted(pairs)`** and serialises with sorted JSON keys — so dict insertion order in the resolved people does not reach the hash. (It does reach the *catalogue* text, which is separately serialised with sorted keys.)
13. **`_thresholds`** emits in rank order; the normalisation is a running sum, so float accumulation order matters to the last digits but not to behaviour.

**Order-independent despite looking otherwise**
14. `phonemes.GATED_AXES = tuple(GATES)` iterates the gate table's declared order, but adds accumulate into a set and drops union into another, with one subtraction at the end — the result does not depend on axis order. Keep the declared order anyway so error messages are reproducible.
15. `merge_traits` / `merge_appearance` / `apply_overrides` iterate the *delta's* key order, but each key is independent, so only the **error message** (which fault is reported first) depends on it — and `provenance.overridden` **does** record `touched` in patch iteration order, so that list's order is JSON-key-order-dependent. It is informational only, but if you want it stable, sort it or preserve document order deterministically.
16. `derive_culture` / `derive_genome` build result dicts in derivation-document order; consumers sort keys before hashing.

**Caller-side ordering that heritage depends on**
17. `terrain_settlements._settlement_names` iterates `sorted(profile_by_node)` — **node order, not founding order** — *so that collision resolution is canonical: whichever city is processed second re-draws, and which one that is must not depend on how the founding loop happened to be sequenced.* If the C++ port iterates founded cities in founding order, every name after the first collision moves.
18. `export_catalogues` emits `heritage` for `sorted(civilization_ids())`; `civilization_report` emits entities in **registry order** (`civilization_ids()`), because founding rotates initiative through the parent races.

**Identifiers**
- Heritage's key field on a resolved people is **`id`** (the civilization id), plus `parent_race_id`. It uses neither `uid`, `site_id`, `kind` nor `name`.
- Heritage ids are **authored registry keys and do not renumber across an age boundary** — unlike hamlet, fortress, plot, nest and culture ids. Anything joining to heritage should join on `civilization_id`, which is stable for the life of the world.
- The word `tier` does not appear anywhere in this package, and neither does `camp`. The word **`culture` does collide**: heritage's `culture` is per-civilization ethnography, while `terrain_humans.culture_groups` is a road-connectivity grouping of cities with seed-local ids that are explicitly *not* inferred ethnicities. They are different objects sharing a word. Likewise `lexicon` names both the whole shared document and a per-branch `{slot: root}` map inside a genome.
- No distance, reach or width appears in this package at all, so the pre-/post-scaling question does not arise. The only physical units are metres and kilogrammes in `appearance`, and they are **absolute, unscaled, world-independent** — a body is identical in every world from every seed.

## Edge cases

**Resolution**
- Unknown `civ_id` → resolves to the parent race base for both traits and appearance, with no overrides applied. Legal and load-bearing: it is what lets a synthetic registry entity drive the whole pipeline, and what makes a newly-added people arrive complete.
- Unknown, empty or null `parent_race_id` → **raise**, with a message containing "parent race". Not a fallback, ever.
- An unknown `branch` (i.e. an unlisted civ) → `branch_lexicon` and `derive_genome` both fall back to an empty spec `{sound_changes: [], add_roots: {}}`; the resolved genome still carries `branch: <civ_id>` even though no such branch exists in the family. The unlisted people therefore speaks the family's unshifted root set — identical to whichever branch has no sound changes (`human_heartland`, `elf`, `dwarf`).
- A delta naming an axis absent from the base → raise. A delta restating the inherited value → raise. Same for appearance deltas and for overrides restating a derived value. Together these keep the sparse layers readable as differences.
- `resolve` returns a **deep copy**; mutating it must not poison the cache. There is a test for exactly this.

**Merges**
- `merge_appearance` is depth exactly two. A delta supplying `frame.height_m` must supply the complete `{male: band, female: band}` structure — there is no partial band merge. A delta supplying `coloration.skin` must supply the complete `{note, swatches}`.
- An empty delta `{}` is legal and is what the three archetypes carry.

**Phonology**
- A gate that drops a phoneme no gate added, or adds one already present, is harmless (set semantics). A gate producing a token outside ALPHABET raises — only reachable by editing the gate table.
- The final inventory can in principle be very small; nothing bounds it except the lexicon spellability lint, which fires first if a root becomes unspellable.
- `apply_sound_changes` with an empty change list returns the word unchanged (the common case: three of twelve branches have no rules).
- A sound change whose `old` is a digraph still fires on that digraph (the `old != pair` escape). A change whose `old` is a single letter **never** fires inside a digraph.
- A rule producing the empty string (`[x, ""]`) would delete the phoneme; the lint only requires `change[0]` non-empty, so a deletion rule is legal. None ships.

**Naming**
- `_join` with an empty first or second argument returns the other unchanged.
- `_join`'s digraph-seam repair is guarded by `len(first) > 2`, so a two-letter first element is never eaten down to one.
- `settlement_name` returning after the 8-attempt loop returns the **last** attempt even though it failed the filter — bounded so naming always terminates; reaching there is an authoring problem, not a runtime one. `word` and `chosen` are always bound because `REDRAW_LIMIT >= 1`.
- `_pick_elements` returns empty when a role has no reachable slot **or** when the two chosen slots are the same. `person_name_with_gloss` then falls back to the phonotactic path with an **empty gloss string** — a caller splitting the gloss on `-` gets `['']`, not two slots.
- `person_name(resolved, draw)` — the two-argument legacy form — is detected by `draw is None` and the arguments are swapped. A caller passing `person_name(resolved, lexicon)` with no draw will crash on `draw.random()`; there is no validation.
- `_nucleus` with no simple (one-letter) vowels falls back to the whole vowel list. Not reachable today: every inventory keeps the five core vowels.
- `_syllable`'s final coda filter `len(c) <= 2` is a no-op on the current alphabet (no consonant token exceeds two letters). Same for `name_table`'s codas.
- `inventory.onsets` could in principle be empty (only if `ng` were the sole consonant); `_syllable` falls back to the full consonant list via `onsets or consonants`.
- `realm_name` returns `""` when the branch has no `realm` root or the capital name is empty — *which lets a caller keep its English form rather than invent one*. Both `hero_generator.realms` and any other caller must handle the empty string.
- `.capitalize()` lowercases everything after the first character. All composed words are already lowercase, but `realm_name` explicitly lowercases the capital name first for exactly this reason.

**The stock**
- Empty `common` → `stock_name` returns **None**, even if `rare` is non-empty. *Serving the rare pairings would make the tail the whole tongue and would hide an unfillable template behind names that look perfectly fine.* A 200-seed test asserts a tail-only stock never yields a name.
- `_thresholds(0)` → empty tuple. `_thresholds(n)` for `n < 5` truncates the head table **and renormalises**, so the last threshold is still 1.0 — *truncating the head without renormalising would drop its last ranks unreachable.*
- `_ranked`'s fallback fires when the head cannot be filled with five root-disjoint names: the plain rank order comes back **whole**, not short. No shipped people reaches this branch, which is why it has a synthetic test — *a single-people version of this test would pass on the data rather than on the property.*
- A rare pairing composing to a name already in the common stock is dropped from the tail.
- A name reachable by two different root pairs is stored once, under the first gloss seen, at the highest weight seen.
- The float index into `rare` is clamped: `min(int(cut * len(rare)), len(rare) - 1)`, so `cut` at the very top of `[0,1)` cannot overrun.

**Loading**
- Duplicate JSON keys → load error. `NaN` / `Infinity` → load error. Missing or non-positive `revision` → load error.
- The lints are fail-fast: the first fault raises, so a file with several faults reports one.
- There is **no hot reload**: the documents are packaged resources with no stat to key a cache on, and the tables are immutable at runtime. A cache-reset entry point exists only for tests.

**World edges and poles** — not applicable. This subsystem never touches a grid, a node, a latitude or a seam.

## Reasoning worth preserving

The comments in this package record measured failures. Quoted, because they exist nowhere else.

**Why the phoneme gates are code and not data** — *"This is code rather than data because it is a physical constraint, not a taste: a people with blunt dentition cannot make an interdental fricative, and no amount of authoring should let one."* And on the alphabet: *"Tokens are ASCII digraphs, not IPA. The repository hashes bytes and disables text normalisation for every provenance destination; a table full of multi-byte glyphs would put that contract under avoidable strain for no expressive gain."*

**Removals win** — *"A phoneme removed by any gate stays removed: the forbidden set is a union, so one impossibility is not argued away by another axis that would have permitted it."*

**The low-frequency sibilant collapse** — *"A tongue shaped for low frequencies sheds the contrasts its listeners cannot separate; one sibilant survives to carry the whole series."*

**Inventory ordering is hashed** — *"Order follows ALPHABET rather than the order gates happened to fire, because this table is hashed into the heritage identity."*

**The cluster shapes, with the forms they replaced** — *"Onsets and codas are built separately because they are not interchangeable: a sequence that opens a syllable rarely closes one. The shapes here are the cross-linguistically common ones — stop plus liquid, and s plus voiceless stop, in front; sonorant plus voiceless stop, and homorganic nasal plus stop, behind. Letting any fricative lead produced onsets like `kht`, and pairing any nasal with any stop produced codas like `nb`; both are the kind of form a real tongue would have levelled long ago."* And: *"Nasal plus stop only where the two share a place of articulation. Pairing any nasal with any stop produced `Pimd` and `Wongd`; homorganic pairing is the rule real tongues keep, and it is why `mp`, `nt` and `ngk` are the surviving shapes. `ng` takes only the voiceless member: ng + g renders as the unreadable `ngg`."* And: *"`tl` and `dl` are the two stop-liquid pairs most tongues refuse."*

**The digraph-atomic sound change** — *"A naive `str.replace` reads the `s` inside `sh` as an `s`, so the rainforest rule s -> sh turned `mush` into `mushh`. Scanning left to right and consuming a digraph whole makes a rule apply to the phoneme it names and to nothing else."*

**Innovations are added after the shift** — *"Innovations are added after the changes rather than before: a word the branch invented was not present when the sound shift ran, which is why gnomish keeps `spril` intact."*

**Why `ng` is barred from word onset** — *"`ng` is a fine coda and a poor word onset in most tongues; without this, dwarven produced names like `Ngukhuls`."*

**Why the genome carries its own sound changes** — *"Carried so coined words undergo the same shifts the inherited roots did. Without it two siblings with the same soma generate identical personal names — which is exactly what human_large_island and human_rainforest did before this line."*

**The compound seam** — *"A seam can spell a digraph its two halves never contained: `ulk` + `hakn` reads as `ul-khakn`, inventing a `kh` neither root has and mis-teaching the reader where the compound breaks. Drop the first root's final consonant rather than let the seam lie."*

**The forbidden-forms filter** — *"Not fastidiousness: a prototype of this design generated `Person` for a cold human from `per` + `son`, and `Mordor` for a hill dwarf from `copper` + `gate`. Innocent roots collide into loaded words, and a generated world produces far more names than anyone reviews. The check is a substring match because the compound, not the root, is where the accident happens."*

**Dithematic over phonotactic** — *"Real names are built from two meaning-bearing elements — Thor-bjorn, Gal-adriel, Ead-weard — and the difference shows: drawing syllables under the phonotactics alone produced pronounceable noise like `Gregaa` and `Penaatang`. Composing a quality with a craft or a terrain gives `Ulkbor` and `Narrik`, which read as names and gloss like the place names do. The element pairs deliberately avoid `terrain + site`, which is what a settlement is, so a person and a town do not come out looking like the same kind of word."*

**Complex nuclei are a seasoning** — *"Drawing uniformly from the whole vowel set gave elvish `Paemuayuan` — every nucleus a diphthong, which no tongue does."*

**Only the final syllable closes** — *"Only the final syllable closes, so a medial coda never meets the next onset and builds a cluster the tongue does not permit. A one-syllable name must close, or a heavy monosyllabic tongue produces bare `Na`."*

**Why the stock exists** — *"A heartland genome composes 88 names; drawn uniformly each lands on about one person in ninety, so nobody is common and — the part that costs more — nobody is unusual either. A rare name cannot mark a foreigner, an old family or an affectation when no name is rare."* And the source of the shares: *"In the English poll tax returns of the late fourteenth century one man in three was a John and the five commonest names covered about four fifths of all men, with a long tail of forms borne by one person each. These shares are those figures rounded. They are tabulated rather than fitted because the measured curve is steeper at the head than any single Zipf exponent reproduces: an exponent steep enough to put one name at 35% still leaves the top five short of 79%."*

**Why the head is spread** — *"Ranking on length alone put the whole heartland head on one root: `Eldsel`, `Frosel`, `Nersel`, `Versel`, and the elves on `-naur` four times over. That is worse than the uniform draw it replaces. Four fifths of a people would share not just a few names but a few sounds, and a reader meeting them in succession hears a stutter rather than a culture. Real stocks do not behave that way: John, William, Thomas, Richard and Robert have nothing in common, because a name that cannot be told from the last one is a name that stops being used."* And the fallback: *"if the space is too small to fill the head with distinct roots, the rank order stands unaltered rather than the head running short."*

**Why the stock is seedless** — *"Rank has to be a property of the culture rather than of a seed — `Eldsel` is a common heartland name in every world the way John was common in every English county — and this package carries no seed logic by design, which rules out permuting the order with one."*

**Why the tail is morphemic** — *"the phonotactic path coins `Wopagup` and `Yoroochaat`, which is the reason the dithematic path exists, and a tail built from it would read as generator noise exactly where a reader is meant to notice the name."*

**Fixed-width draws** — *"Two draws whatever the outcome, so the stream a caller sees does not depend on which branch was taken and adding the rare tail cannot desynchronise a replay from itself."*

**Refusing a tail-only stock** — *"No common stock means no personal template this family's roots can fill. The rare pairings may still compose, but promoting them would make the tail the whole tongue and hide the authoring fault behind names that look fine. Refuse the whole stock."*

**The no-numbers rule and its one exception** — *"This is the one heritage layer that carries numbers, and it may because a body has measurements and nothing else in the repository states them. `civilizations.json` measures habitat and never anatomy, so a number here cannot become a second copy of one the placement code reads — which is the whole reason the trait and culture layers are barred from carrying any."*

**Why appearance is authored, not derived** — *"a skin range or an ear form is new information rather than a consequence of the seventeen axes."* And from the docs: *"a generator that invented them from `body_scale` would produce three bodies wearing twelve names."*

**Why the height envelope lives beside the gates** — *"These are physical constraints rather than authoring taste, so they live here beside `phonemes.GATES` instead of in the table they police: a subrace cannot be handed a stature its own key trait contradicts. The `compact` and `small` ceilings are what make 'no dwarf stands over 4 ft 5 in' — 1.3462 m — a property of the loader rather than a promise in a comment."*

**Why the cross-layer check is a separate function** — *"Called once per race base at load, and once per people after `merge_appearance`, because a delta cannot be judged until it has been merged onto what it inherits. One implementation and two call sites: the alternative is a second merge in this module, which is exactly the drift a sparse layer invites."*

**Why form and note travel together** — *"a `features` entry is a form token and the sentence that explains it... Keeping the pair in one place is the point — a parallel prose block beside a token block is two registries of the same fact, and they drift."* And: *"`broad_high_bridge` tells a concept artist nothing about a dwarf's nose."*

**Why only two closed vocabularies** — *"a wrong value in either produces the wrong art silently rather than a missing field. Every other descriptive key stays open: appearance vocabulary is art direction and grows with the world... Closing them all would mean a code edit for every new nose."*

**Why the identity hashes outputs, not bytes** — *"A derivation edit that changes no output then invalidates nothing, while a one-character override that does change an output is caught."*

**The `resolve` asymmetry** — *"An unknown `civ_id` resolves to its parent base, so a people that heritage has never heard of still arrives with a complete culture and tongue. An unknown or missing `parent_race_id` raises: it selects the language *family*, so guessing one yields a name in the wrong language rather than an approximate one, and that silent guess is the defect this package exists to remove. A caller that cannot supply a parent race should leave the name empty and say so, rather than substituting a default."*

**Why the whole package is a leaf** — *"It never imports `icarus_sim`, so `resolve` is handed a parent race rather than looking one up, and the two enums it has to restate — the civilization ids and the eight known magic schools — are locked against the registry from the repository test suite rather than by an import. It also carries no seed logic: callers pass their own deterministic draw, which keeps every seeding decision with the subsystem that owns it."*

**Why hidden schools are refused** — *"nobody has a cultural stance toward a school they have never heard of, and no world can produce one."*

**Why the totality rule exists** — *"Every enum value maps, so adding an axis value cannot silently produce an empty culture."*

**Why overrides may not restate** — *"an entry that restates the derived value is a load error, so this file stays a list of real decisions rather than a slowly fossilising copy of the output."*

**Why settlement naming runs in node order** (from the caller, but it constrains heritage's contract) — *"Naming runs in node order rather than founding order so that collision resolution is canonical: whichever city is processed second re-draws, and which one that is must not depend on how the founding loop happened to be sequenced. The redraw is bounded and uses child_seed's third parameter, so a duplicate yields another authentic name rather than a numeric suffix."*

**Known limits, recorded by the author** (docs/heritage.md, "Known limits"): name-space per tongue is a few hundred settlement names and the elven lexicon is the thinnest; sibling tongues can still coin the same personal name, and *"The stock makes this louder rather than quieter"* — `dwarf` and `gnome` share three of their five commonest names, as do `human_heartland` and `human_rainforest`; `hill_dwarf` inherits uvulars its pastoral description does not suggest, and its rules shift them away but the inventory still lists them; a coloration palette of four or five swatches *"will read as banding if a consumer picks swatches without interpolating between them"*; `attire` is one costume per subrace with no class, rank, season or trade variation, so *"a city of them drawn straight from this table is a city in uniform; the intended fix is a per-NPC layer on the engine side, not more rows here"*; and nothing yet generates a byname, so at this concentration the commonest heartland name lands on hundreds of people with no way to tell them apart.

## Notes for the C++ implementation

**This subsystem touches none of the `fwg` numeric machinery.** It reads no `Grid`, never calls `sphere_grid`, never reads `Config`, and does not write layers. Treat it as a self-contained static data library — `include/fwg/heritage.hpp` plus `src/heritage/*.cpp` — that the settlement, hero, NPC and ruin stages call into. It can be built and fully tested before any of those stages exist, which makes it an unusually good early port.

**The biggest structural problem: there is no JSON reader.** The library has its own JSON *writer* and no parser, and heritage's entire input is 84 KB of JSON across five files. Three options, in order of preference:
1. **Generate C++ source from the JSON at build time** (a small tool, or a one-off commit) into `constexpr`/`inline const` tables. This keeps the no-dependency rule absolutely, makes the tables immutable and zero-cost at startup, and turns every lint from a runtime check into a static-assert-or-unit-test. The 63 KB appearance file becomes long string literals; MSVC's 65,535-byte limit per *string literal* applies, so split the prose, not the file.
2. Write a small read-only JSON parser (~300–400 lines) and embed the files as raw string literals. Keeps the data files editable as data, which is what the authoring workflow expects, and keeps the linters meaningful.
3. Load from disk at runtime. Rejected: the reference uses package resources precisely so the tables ship with the code, and a missing file at stage 16 is a terrible failure mode.

Recommend (2) if the authoring loop matters, (1) otherwise. **Decide this before writing any code**, because the linters only earn their keep under (2) or (3).

**Python idioms with no direct C++ equivalent**

- **Insertion-ordered dicts.** Python's `dict` preserves insertion order and the reference leans on it in places that reach output (`provenance.overridden`; `orthography.digraphs` via the inventory list; `branch_lexicon` building roots in family-root order). `std::map` sorts, `std::unordered_map` does neither. Use `std::vector<std::pair<K,V>>` with a lookup helper, or a small insertion-ordered map type, wherever iteration order is observable; use `std::map` where only lookup matters. **Do not reach for `unordered_map` anywhere whose iteration reaches output.**
- **`sorted(set(...))`** in `_clusters` → `std::vector`, `std::sort`, `std::unique`, `erase`. Straightforward.
- **`sorted(options)` before `choice`** → sort the option vector, then `Rng::choice`. This is not an optimisation to skip.
- **`_ordered`'s sort key `(-weight, len(name), name)`** → `std::sort` with an explicit comparator. The key is total over distinct names, so `std::sort`'s instability is harmless — but state that in a comment so nobody "fixes" it to `stable_sort` and nobody removes the tie-breakers.
- **`str.capitalize()`** uppercases the first character **and lowercases the rest**. Everything composed here is already lowercase, except `realm_name`, which explicitly lowercases the capital name first. Reproduce both halves; ASCII only, so no locale involvement.
- **String slicing and `startswith(old, index)`** in `apply_sound_changes` → `std::string_view` with `compare`/`substr`. Watch the two-byte lookahead at the end of the string: Python's `word[i:i+2]` silently yields a one-character string at the last position, whereas `substr(i, 2)` also clamps — but a naive `word[i] , word[i+1]` read does not. Guard it.
- **`lru_cache`** → a `std::map` keyed on `(civ_id, parent_race_id)` behind the resolve entry point, plus a single lazily-initialised document bundle. The reference caps resolution at 64 entries; no cap is needed in C++. **The public entry point must return a copy**, not a reference into the cache — a test exists for exactly that, and the reference's `copy.deepcopy` is doing real work.
- **Exceptions.** Every lint failure and every resolution failure is a `ValueError` with a message that names the fault. If the library avoids exceptions, return `std::expected`-style results or a `std::string` error out-parameter, but **keep the messages** — they are half the value of the linters, and several are quoted verbatim in tests.
- **Dynamic typing in the policy documents.** Culture values are always strings; genome values are string, bool **or** double; appearance values are string, double, band, palette, token list, or `{form,note}` pairs. Under option (1) each becomes a typed field and the type-checking lints disappear into the type system. Under option (2) you need a small variant. The `lint_rule` check "a culture mapped value must not be a number or bool" is exactly the kind of thing a typed table makes unrepresentable — say so in a comment rather than silently dropping the check.

**Mapping onto the existing conventions**

- **`fwg::Rng`**: naming takes `Rng&`. `Rng::random()` and `Rng::choice(vector)` are already the two operations needed. The caller derives streams with `Rng::child(master, domain, variation)` — the settlement namer's domain is `"city-name-<node>-<x>-<z>-<profile>"` with `variation` 0..7 for the collision redraw. **No fourth seed derivation.** `Rng::choice` takes a `const std::vector<T>&`, so option lists must be materialised as vectors, which they already are.
- **`Rng::draws()`**: use it to assert `stock_name` spends **exactly two words' worth of `random()` calls** on both branches. This is the one stream-discipline invariant in the package and the existing harness already has the tool for it.
- **`fwg::World`**: heritage writes nothing into a `World` directly. The consumer (`civilization_report`, stage 10+) embeds `{traits, culture, genome, provenance}` per entity into the `civilizations` block at **version 3**, plus a top-level `heritage` identity. Appearance stays out of the world document and goes to the catalogue.
- **`fwg::JsonWriter`**: enough to emit the resolved people. Serialising for the identity hash needs **sorted keys and compact separators**, which the writer does not currently guarantee — either add a sorted-object mode or build the canonical string separately. Doubles use `std::to_chars` shortest round-trip, which differs from Python's `repr`; that is fine, the hash need only be stable within this implementation.
- **`fwg::sha256`**: already present, used directly by `heritage_identity`.
- **No `fwg::Grid`, no `sphere_grid`, no `Config`.** Resist any urge to key heritage on a node.

**Things that will bite**

- The **appearance prose is the deliverable**, not decoration. Do not truncate, reflow or re-encode it. It is all ASCII today; keep it that way and keep the bytes stable, because they feed a content hash.
- The **`(C)V(C)` template parser** counts literal `"(C)"` and `"(N)"` substrings after partitioning at the first `V`. Do not "improve" it into a grammar; the eight shipped templates are the whole input domain and the counting rule is the specification.
- **`_join`'s three rules are ordered and the order is load-bearing** — geminate, then hiatus, then digraph seam. Reordering them changes names.
- **The optional-coda gate is `random() >= 0.55` (skip)**, not `< 0.55` (emit). Getting the sense backwards flips every optional coda in the world.
- **`trisyllabic_flowing` maps to a base count of 2, not 3.** It looks like a bug and is not: the third syllable arrives through `rates.third_syllable`, which is 0.7 for `sustained` breath, the only trait that produces that shape.
- **`_enumerate`'s collision rule keeps the first gloss and the highest weight** — i.e. it deliberately produces an entry whose weight came from one pairing and whose gloss came from another. Reproduce it exactly; "fixing" it reorders stocks.
- `name_stock` enumerates a full cross product (up to ~190 names × up to 6 patterns for the rare set). Cheap once per people, ruinous per person. **Cache on `(civilization_id, parent_race_id)`** as the reference's roster does.
- The `loanword` rate is computed and written and **nothing reads it**. Port it anyway; it is part of the published genome shape.

## Open questions

1. **How the data files should exist in C++.** There is no JSON reader in the library and heritage is 84 KB of JSON. Generate C++ tables at build time, write a small parser, or load from disk? This changes what the five linters are for: under generated tables most of them become compile-time or test-time properties and several become unrepresentable. A porter must decide this before writing a line, and should record which lints survive in which form. My recommendation is a small read-only parser plus embedded literals, because the authoring workflow (add a people, bump a revision, re-export the catalogue) assumes the files stay data.

2. **`name_table` is a compatibility adapter with no remaining consumer in the Python tree.** Its own docstring calls it *"A compatibility bridge, not the destination"* and says the old call site *"still appends a bare nucleus for its third syllable, so the tables below keep onsets consonant-initial and codas closed to limit the damage until that call site moves to `person_name`."* `hero_generator.naming.given_name` — the function it adapts to — has been deleted. Grep confirms no caller outside the package and one test asserting its shape. **Port it or drop it?** Dropping it is defensible; if it is dropped, say so in the porting notes rather than letting it vanish silently.

3. **`drop_roots` is implemented and unused.** `branch_lexicon` honours `spec['drop_roots']`, no branch uses it, and `lint_lexicon` does not validate it (an unknown slot in `drop_roots` is silently ignored). Port the feature and add the missing lint, or drop it? If ported, note that a branch dropping a *shared* slot would escape the shared-slot completeness lint, which only checks the family's `roots`.

4. **`provenance.overridden` order is JSON-key-order-dependent.** It records patch keys in iteration order, so its order is an accident of the override document. It is informational only, and it is serialised into the world document and into the identity hash's input — but since the hash sorts *keys* and not *array elements*, a reordering of the override file would move the hash. Should the ported list be sorted? The reference does not sort it; sorting it is a behaviour change, and the hash is not gated on, so either answer is defensible. State the choice.

5. **`_lint_rule` accepts a malformed rule.** It checks `len(rule) in (1, 2)` and then tests `'copy' in rule` first, so `{"copy": "kinship", "map": {...}}` passes as a copy rule with the map silently ignored. Not reachable with the shipped data. Tighten it in the port (a rule should be exactly one of the three shapes), or reproduce the laxity? Tightening is almost certainly right; flag it as a deliberate departure.

6. **`rates.loanword` is dead output.** Computed for every people, published in the genome, read by nothing anywhere in the tree. It is presumably reserved for a borrowing mechanic that was never built. Port it as data (it is part of the published shape) but do not invent a consumer.

7. **The tidekin inventory keeps `th` while the tidekin sound changes delete every `th`.** Same for `hill_dwarf`, whose inventory lists uvulars its rules shift away — the docs acknowledge this one: *"`hill_dwarf` inherits uvulars its pastoral description does not suggest; its rules shift them away, but the inventory still lists them."* So a coined phonotactic name can be built with a phoneme the branch's own rules then rewrite, which is arguably correct (the shift applies after coinage) and arguably means the published inventory misdescribes the tongue. Reproduce as-is; it is documented, not accidental. But a consumer reading `genome.inventory` as "what this tongue contains" will be wrong, and that is worth a comment in the header.

8. **Nothing in the reference proves the appearance `elder.height_fraction` may exceed the adult's.** The growth check covers only child < adolescent < adult, so an elder at 1.05 would load. All shipped values are below 1. Add an upper bound in the port, or leave the gap? Leaving it is faithful; closing it is one line and cannot reject any shipped data.

There is no `ICARUS_CITY_WORKERS` process pool anywhere in this package, no `tier`, no `camp`, and no scaled distance — the three traps listed for this codebase do not apply here. The `culture` word collision does apply and is disambiguated in `ordering_hazards`.

## Addenda from the audit

The reader above wrote the specification; a second reader went back to the source
to find what it had left out or got wrong. Both are kept: the omissions are
content the implementer needs, and the corrections say where the specification
above is not to be trusted.

### Corrections

- **Claimed:** `name_table`: `codas` = consonants of length <= 2, deduplicated and sorted, plus two empty strings if `hiatus == "preferred"` (so an open syllable is twice as likely for those tongues)
  **Actually:** The source appends two empty strings and then returns `sorted(set(codas))`, which collapses them to ONE. For an elf (`hiatus == preferred`) the returned coda list is `['', 'b', 'ch', 'd', 'f', 'g', 'h', 'k', 'l', 'm', 'n', 'ng', 'p', 'r', 's', 'sh', 't', 'th', 'ts', 'v', 'y', 'z']` — a single empty entry out of 22, about 4.5%, not double weight. The stated rationale is false of the shipped code; a porter implementing 'twice as likely' would build something the reference does not do.

- **Claimed:** For coda-2 tongues the cluster set is `lk lp ls lt mb mp ngk nt rk rp rs rt`
  **Actually:** Thirteen entries, not twelve: `nd` is missing from the spec's list. `human_cold`, `human_desert` and `frosthold_dwarf` all resolve to `['lk','lp','ls','lt','mb','mp','nd','ngk','nt','rk','rp','rs','rt']`. The spec's own homorganic rule ('`n` before `t`/`d`') produces `nd`, so the list contradicts the algorithm beside it and would make a porter 'fix' a correct implementation.

- **Claimed:** For onset-2 tongues the cluster set is `bl br dr gl gr kl kr pl pr sk sp st tr`
  **Actually:** True for `human_heartland` and `gnome`, but `hill_dwarf` is also an onset-2 tongue (speech_environment open_plain) and keeps `q` in its inventory, so it yields fifteen: the same thirteen plus `ql` and `qr`. The cluster set is a function of the inventory, not a constant per template.

- **Claimed:** Open question 8: 'Nothing in the reference proves the appearance `elder.height_fraction` may exceed the adult's ... All shipped values are below 1.'
  **Actually:** The elf race base has `life_stages.elder.height_fraction = 1.0`, exactly equal to the adult reference, not below it. (human 0.98, dwarf 0.97, tidekin 0.99, gnome 0.96, hill_dwarf 0.96 are below.) A port that 'closes the gap' with a strict `elder < adult` bound would reject the shipped elf, which the spec asserts cannot happen.

- **Claimed:** dwarven family — 31 roots incl. `realm: rik`; branch lexicons are 29–34 entries per branch
  **Actually:** The dwarven family has 30 roots (18 shared-slot roots + forge, hold, iron, gold, peak, king, hammer, ash, salt, vault, ember + realm). Measured branch lexicon sizes across the twelve are 29 (elf) to 33 (gnome, hill_dwarf, and each human branch bar heartland), i.e. 29–33, never 34. The spec's own dwarven root listing is complete and correct — only the count is wrong.

- **Claimed:** The nine real appearance deltas touch between 14 and 31 keys each
  **Actually:** 14 to 37. Measured: human_maritime 14, frosthold_dwarf 16, human_desert 20, human_cold 21, human_large_island 21, human_rainforest 27, tidekin 27, hill_dwarf 27, gnome 37. The gnome delta — which the spec itself enumerates as 3+4+10+3+4+5+5+3 — is 37, above the stated ceiling.

- **Claimed:** `frame.height_m` observed range across the twelve: 0.95–2.20 m; `frame.mass_kg` observed 23–~110 kg
  **Actually:** The observed maxima are 1.99 m (elf male `max`) and 104 kg (human_cold male `max`). 2.20 m is the `tall` envelope CEILING from `BODY_SCALE_HEIGHT_M`, not an authored value; quoting it as observed invites a porter to treat the envelope and the data as the same thing.

- **Claimed:** `lint_traits`: a race base's key set must equal the 17-axis set exactly (error names the symmetric difference)
  **Actually:** The error names the missing axes if any are missing, and only otherwise names the extra ones: `missing = sorted(set(AXES) - set(base)) or sorted(set(base) - set(AXES))`. A base that is both missing an axis and carrying an unknown one reports only the missing half. Several linter messages are quoted verbatim in tests, so the construction matters.

- **Claimed:** `person_name(resolved, lexicon=None, draw=None)`: if `draw` is None the two trailing arguments are swapped
  **Actually:** Accurate in effect but worth stating precisely, since the port will not have Python's defaulting: the body is `if draw is None: lexicon, draw = None, lexicon`. In C++ this is two overloads — `person_name(resolved, lexicon, rng)` (dithematic) and `person_name(resolved, rng)` (phonotactic) — not a runtime argument swap, and a literal transcription of 'swap' into a typed language will not compile or will mis-dispatch.

### Missing — blocking

**derivation.json — the entire culture rule set (24 rules with their maps)**

household.unit={copy:kinship}; household.inheritance={from:kinship,map:{nuclear_household:divided_among_children, extended_clan:hold_undivided, lineage_house:house_entail, age_cohort:cohort_commons, crew_and_berth:hull_and_share, guild_family:shop_and_tools}}; household.naming_pattern={from:kinship,map:{nuclear_household:given_plus_father, extended_clan:given_plus_clan, lineage_house:given_plus_house, age_cohort:given_plus_cohort, crew_and_berth:given_plus_vessel, guild_family:given_plus_workshop}}; household.marriage_move={from:kinship,map:{nuclear_household:new_household, extended_clan:to_the_hold, lineage_house:to_the_elder_house, age_cohort:within_the_cohort, crew_and_berth:to_either_deck, guild_family:to_the_bench}}. governance.seat={copy:authority}; governance.succession={from:authority,map:{elder_council:seniority, hereditary_lord:eldest_of_the_line, guild_assembly:elected_term, acclaimed_war_leader:acclaimed_in_the_field, temple_seat:chosen_by_omen, consensus_moot:acclaimed_each_moot}}; governance.law_form={from:memory_mode,map:{oral_formulaic:spoken_precedent, sung_lineage:sung_precedent, written_archive:written_code, carved_record:carved_statute, ledger_and_seal:written_charter}}; governance.assembly={from:mobility,map:{rooted:winter_court, seasonal_transhumance:gathering_at_the_move, voyaging:landing_moot, caravan:caravanserai_council, frontier_expanding:county_assize}}. livelihood.primary={craft:0}; livelihood.secondary={craft:1}; livelihood.exchange={from:outsiders,map:{hostile:weighed_barter, wary:weighed_barter, transactional:coin_and_tally, hospitable:share_and_gift, assimilating:coin_and_credit}}; livelihood.calendar={from:mobility,map:{rooted:two_season_dark_and_thaw, seasonal_transhumance:drive_and_return, voyaging:tide_and_run, caravan:market_quarters, frontier_expanding:sowing_and_harvest}}. memory.record={copy:memory_mode}; memory.teaching={from:lifespan_tempo,map:{brief:short_apprenticeship, human:indentured_apprenticeship, long:hold_apprenticeship, ancient:lifelong_fostering}}; memory.oath_form={from:memory_mode,map:{oral_formulaic:spoken_before_witnesses, sung_lineage:sung_before_kin, written_archive:signed_and_witnessed, carved_record:cut_into_the_lintel, ledger_and_seal:signed_and_sealed}}; memory.taboo={from:magic_stance,map:{untrained:speaking_to_the_unseen, ritual_communal:rite_out_of_season, scholarly_licensed:unlicensed_practice, inherited_bloodline:teaching_the_bloodless, taboo_avoidant:the_worked_word, bargained_with:breaking_the_terms}}. contact.stranger_rite={from:outsiders,map:{hostile:gate_challenge, wary:threshold_question, transactional:open_counter, hospitable:bread_and_seat, assimilating:named_as_kin}}; contact.trade_stance={copy:outsiders}; contact.war_stance={from:authority,map:{elder_council:defend_the_grove, hereditary_lord:hold_and_deny, guild_assembly:paid_levy, acclaimed_war_leader:raid_and_withdraw, temple_seat:sanctioned_war, consensus_moot:muster_and_disperse}}. faith.school={copy:magic_school}; faith.practice={copy:magic_stance}; faith.site={from:magic_school,map:{weave:chartered_college, umbral:unlit_vault, infernal:warded_pit, radiant:sun_terrace, fire:ember_hearth, water:tideline_stone, earth:deep_cairn, air:wind_gallery}}; faith.festival={from:magic_stance,map:{untrained:harvest_thanks, ritual_communal:the_long_rite, scholarly_licensed:charter_day, inherited_bloodline:the_naming_of_lines, taboo_avoidant:the_quiet_week, bargained_with:the_terms_renewed}}.

**derivation.json — every genome map except the three rate tables**

phonotactics.syllable={from:speech_environment,map:{open_plain:"(C)(C)V(C)", surf:"(C)V(V)(N)", forest:"(C)V(C)", stone_hall:"(C)V(C)", wind_scoured:"(C)V(C)(C)", humid_canopy:"(C)V(N)", market_din:"(C)(C)V(C)", terraced_valley:"(C)V(C)"}} — note surf's "(V)" in the tail is counted by neither `(C)` nor `(N)` and is silently ignored. phonotactics.vowel_length={from:breath_capacity,map:{short:none, steady:none, deep:none, sustained:contrastive}}. phonotactics.hiatus={from:breath_capacity,map:{short:forbidden, steady:allowed, deep:forbidden, sustained:preferred}}. phonotactics.geminates={from:speech_environment,map:{open_plain:false, surf:false, forest:false, stone_hall:true, wind_scoured:true, humid_canopy:false, market_din:false, terraced_valley:true}}. prosody.stress={from:carrying_register,map:{quiet:penultimate, conversational:initial, carrying:initial, calling:penultimate}}. prosody.tone={from:speech_environment,map:{open_plain:none, surf:none, forest:none, stone_hall:none, wind_scoured:none, humid_canopy:register, market_din:none, terraced_valley:register}}. prosody.word_shape={from:breath_capacity,map:{short:monosyllabic_heavy, steady:disyllabic, deep:disyllabic, sustained:trisyllabic_flowing}}. morphology.compounding={from:memory_mode,map:{oral_formulaic:light, sung_lineage:light, written_archive:moderate, carved_record:heavy, ledger_and_seal:heavy}}. morphology.honorific={from:authority,map:{elder_council:age_suffix, hereditary_lord:title_prefix, guild_assembly:rank_suffix, acclaimed_war_leader:deed_epithet, temple_seat:sacred_prefix, consensus_moot:none}}. morphology.call_form={from:carrying_register,map:{quiet:false, conversational:false, carrying:true, calling:true}}. orthography.script_form={from:writing_surface,map:{none:unwritten, knot_and_cord:knotted_cord, incised_stone:angular_rune, carved_wood:notched_stave, inked_hide:round_hand, pressed_fibre_paper:cursive_nib}}. orthography.direction={from:writing_surface,map:{none:unwritten, knot_and_cord:strand_order, incised_stone:vertical_column, carved_wood:boustrophedon, inked_hide:left_to_right, pressed_fibre_paper:left_to_right}}.

**lexicon.json — the human and elven family root tables (60 of the 90 shipped roots)**

human family (parent_race human, word_order verb_medial, affixing suffixing), 31 roots in this insertion order: stone kor, water dun, high bren, deep graf, wood hal, field fen, river lin, gate por, hall sal, old eld, north nord, cold fros, fire brand, green ver, shore strand, wind wen, road weg, house ham, harbour mar, ford vad, hill tor, new ner, king ros, salt sel, bridge brig, mill mel, well pat, sun sol, sand tam, market sev, realm mark. elven family (parent_race elf, verb_medial, PREFIXING), 29 roots: stone gond, water nael, high tael, deep thal, wood taur, field laen, river duin, gate annon, hall mereth, old ando, north fori, cold hela, fire naur, green lain, shore aeril, wind siria, road pada, house mar, grove sila, song aeth, leaf lith, star eiri, long anda, tide mael, foam vaen, light galad, dark mori, silver kelev, realm doren. (Insertion order is load-bearing: `branch_lexicon` iterates the family roots in document order.)

**appearance.json — the whole 63 KB of authored bodies: 3 complete race bases and 9 subrace deltas**

The spec carries only an abridged dwarf sketch. The numeric spine that must survive, as (min, mean, max, sd): human height_m male (1.62,1.74,1.88,0.068) female (1.51,1.62,1.75,0.062); mass_kg male (60,74,92,9) female (50,62,78,8); build balanced, sexual_dimorphism moderate; proportion heads_tall 7.4, shoulder 0.245, leg 0.47, hand 0.11; life_stages child(0,0.38) adolescent(12,0.80) adult(18,1.0) elder(55,0.98), max_years 95; grooming scalp varied_straight_to_coiled, facial_hair male_only, body_hair moderate, styles [cropped, shoulder_length, braided_crown]. elf height_m male (1.72,1.86,1.99,0.055) female (1.66,1.79,1.92,0.052); mass_kg male (56,66,78,5.5) female (48,57,68,5.0); build lithe_long, dimorphism minimal; proportion 8.1/0.22/0.52/0.115; stages child(0,0.35) adolescent(25,0.70) adult(100,1.0) elder(600,1.0), max_years 900; grooming fine_straight, none, absent, [waist_length_loose, temple_braids, single_crown_plait]. dwarf height_m male (1.19,1.27,1.34,0.045) female (1.13,1.21,1.29,0.043); mass_kg male (57,68,84,7.5) female (49,59,73,6.5); build dense_broad, dimorphism low; proportion 4.6/0.31/0.42/0.125; stages child(0,0.42) adolescent(14,0.78) adult(30,1.0) elder(180,0.97), max_years 350; grooming coarse_thick, full_universal, heavy, [forked_beard, clan_braids, shaved_temples]. Subrace numeric deltas: human_maritime mass male(64,78,95,9) female(53,65,81,8), build rope_muscled. human_desert height male(1.66,1.79,1.93,0.066) female(1.55,1.67,1.80,0.061), mass male(55,66,80,8) female(45,56,69,7), build lean_long, heads_tall 7.7, shoulder 0.235, leg 0.49. human_cold mass male(70,86,104,10) female(58,72,88,9), build heavy_set, shoulder 0.27, elder(50,0.97), max_years 85. human_large_island height male(1.58,1.69,1.82,0.062) female(1.47,1.58,1.70,0.058), heads_tall 7.3, shoulder 0.24. human_rainforest height male(1.52,1.63,1.75,0.06) female(1.42,1.53,1.64,0.056), mass male(52,62,75,7) female(43,52,64,6), build compact_wiry, heads_tall 7.1, leg 0.45, adolescent(11,0.79) adult(16,1.0) elder(45,0.97), max_years 78. tidekin height male(1.64,1.76,1.88,0.05) female(1.60,1.71,1.83,0.048), mass male(62,72,84,6) female(55,64,75,5.5), build streamlined_muscular, 7.6/0.25/0.49, elder(450,0.99), max_years 700. gnome height male(0.97,1.05,1.14,0.038) female(0.95,1.01,1.09,0.036), mass male(26,33,41,4) female(23,29,36,3.6), build slight_wiry, 4.2/0.26/0.44/0.14, child(0,0.40) adolescent(12,0.76) adult(22,1.0) elder(70,0.96), max_years 120. hill_dwarf height male(1.08,1.16,1.23,0.04) female(1.02,1.10,1.18,0.038), mass male(44,54,66,6) female(38,46,57,5.5), build lean_broad, 4.9/0.29/0.45, elder(150,0.96), max_years 280. frosthold_dwarf height male(1.21,1.29,1.34,0.043) female(1.15,1.23,1.30,0.041), mass male(62,75,90,8) female(53,64,79,7). Beyond the numbers, the 10 `features` {form,note} pairs, 3 `coloration` {note,swatches} palettes, 5 `attire` keys and 3 `art_direction` keys per body are English prose the spec itself calls the deliverable and must be copied byte-for-byte; they are not reconstructible from the shape description.

**`_draw_person` restricts onset clusters to the FIRST syllable only**

The call is `word = _join(word, _syllable(genome, draw, onset_cluster and index == 0, mode), genome)`. The spec's F.6 computes `onset_cluster = max_onset_cluster > 1` once and never says it is ANDed with `index == 0`, so an implementer will permit a cluster onset on every syllable. That changes both the names (medial `br-`, `st-` onsets that the reference never produces) and the draw count per name, which the spec elsewhere declares stream-structural. Correct rule: a syllable may take an onset cluster only when the tongue permits clusters AND it is syllable 0.

### Missing — important

**`terrain_slot` has no production caller**

The spec presents the terrain bias as a live feature of settlement naming. `terrain_settlements._settlement_names` calls `heritage.settlement_name(resolved[profile], lexicon, draw)` with three arguments — no `terrain_slot`. The only caller that passes it anywhere in the tree is `Sim/tests/test_heritage.py`. It is dead in the pipeline exactly as `rates.loanword` is dead output, and the spec flags the latter but not this.

**`add_roots` can shadow an inherited slot, not only add new ones**

`branch_lexicon` ends with `roots.update(spec.get('add_roots', {}))`. A branch whose `add_roots` names a slot the family already has replaces the shifted inherited root with the unshifted coinage. This happens in the shipped data: the human family has `well: pat`, and `human_desert` (rule p→f, which would give `fat`) declares `add_roots: {well: kanat}`, so desert `well` is `kanat`. The spec calls add_roots "innovations" and never states the overwrite, so a porter who implements add-if-absent gets a different lexicon for human_desert.

**`hero_generator.naming.heritage_name` defaults the parent race to 'human'**

The spec states the no-default contract for `parent_race_id` and says a caller that cannot supply one must leave the name empty. The actual hero caller does `heritage.resolve(civilization_id, person.get('race_id') or 'human')` — it raises only on a missing `civilization_id`, and substitutes `'human'` for a missing `race_id`. If the port preserves the caller, preserve this asymmetry knowingly; if it does not, that is a deliberate behaviour change and should be recorded.

### Missing — minor

**`lint_overrides` performs no structural validation of the `genome` section**

It checks only that each civ id under `genome` is a real people. Unlike the culture section, there is no block check, no key check, no nesting check and no number check — a genome override naming a nonexistent block is caught only at `apply_overrides` time during `resolve`, not at load. The spec's A.4 implies this by omission but never states it, and a porter tightening the lint would make a currently-loadable document fail.

**`lint_appearance` rejects a non-dict people block**

`if not isinstance(keys, dict): raise ValueError(f'{label} block {block!r} must map keys to extensions')` — a people delta whose block value is a scalar or list fails here before any key is examined. Absent from the spec's A.5.

**`lint_lexicon` accesses required sub-keys directly, so a malformed document raises KeyError rather than ValueError**

`entry['parent_race']`, `entry['roots']`, `entry['branches']`, `spec['sound_changes']` and `document['slot_roles']` are all indexed, not `.get`. A branch spec missing `sound_changes` crashes with a KeyError, not one of the linter's sentences. Since the spec says every lint 'raises on the first fault with a sentence naming the fault', a port that returns typed errors needs to know which faults have no sentence.

**The package exposes `heritage.VERSION = 1` and re-exports `load`, `load_all`, `AXES`, `APPEARANCE_BLOCKS`, `BODY_SCALE_HEIGHT_M`, `CULTURE_BLOCKS`, `KNOWN_SCHOOLS`, `reset_cache`**

`__init__.py` defines `VERSION = 1` and an explicit 20-entry `__all__`. The spec's `writes` section lists the functions but never mentions the package version constant or the exported tables, which consumers may read.

**`_lint_rule` requires `map` to be a dict before the totality check**

`table = rule.get('map'); if not isinstance(table, dict): raise ValueError(f'derivation rule at {path} needs a map')`. The spec's A.2 jumps straight to 'the map must contain every value of the axis enum' without the type gate, and without it a non-dict `map` gives a TypeError rather than the authored message.

**`heritage_identity` hashes the resolved people INCLUDING `appearance`**

`heritage_identity` serialises the output of `_resolve`, which carries the full 8-block appearance. The spec is careful to say appearance is excluded from the world document, but never says it is included in the identity hash — so an appearance prose edit does move the sha256, which is the property that makes the hash useful for the art pipeline. Stating it matters because the C++ note recommends a sorted-key canonical serialiser and a porter must know how much of the record feeds it.
