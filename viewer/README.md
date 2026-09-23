# FWGC++ world viewer

A browser viewer for the worlds `fwg` generates: pick a seed, a size and any of the
generator's published controls, generate, and look at the result -- the world map with any
of its layers and overlays, the record behind anything on it, the settlement plans in 2D and
3D, and what the generator reported about its own run.

It is a small Node server (`server.mjs`, no npm dependencies) that runs `fwg.exe` and serves
a static page (`public/`, no build step, no libraries). The browser never receives a whole
world document -- they run from 23 MB at size 17 to 720 MB at size 513 -- only the summary
and the one block, layer or plan it is showing.

## Start it

Needs Node 18 or later and a built `fwg`.

```bash
cd viewer
node server.mjs
```

Then open <http://127.0.0.1:8787/>. Options:

| | |
|---|---|
| `--port N` | listen on port N (default 8787, or `$PORT`; `0` picks a free one) |
| `--host ADDRESS` | listen on ADDRESS (default `127.0.0.1`: this machine only) |
| `--cache DIR` | where generated worlds are kept (default `viewer/.cache`) |

**Where fwg is looked for.** `../generator/build/apps/Release/fwg.exe`, relative to this
folder -- what the generator's Visual Studio build writes -- then `fwg` without the extension
and the same two directly under `build/apps/` (a single-configuration build). Set `FWG_EXE`
to use any other. If none exists the server still starts, says so, and answers every
generation request with the same instructions:

```bash
cd generator
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target fwg_cli
```

`FWG_VIEWER_CACHE_MB` sets the cache budget (default 4096); `FWG_VIEWER_LOG=1` logs every
API request with its time.

## What it shows

- **Controls.** Seed, size and phase in the top bar; every other open control from
  `fwg controls` in collapsible groups, each with its default, range, units and
  description, and a search box. Only controls you change are sent: several defaults
  (`globe_radius`, `amplitude`, `settlement_spacing`, ...) are *derived* from the world's
  width unless set, and are marked so. Generate shows elapsed time against an estimate for
  the size; a refused request shows fwg's own message.
- **World map.** Equirectangular, one block per cell, wrapping in longitude when zoomed in;
  drag to pan, scroll or double-click to zoom. Any of the document's layers (about 160, in
  groups); elevation gets a hypsometric tint, and every layer can take relief shading from
  the height field. Categorical layers (biomes, magical biomes, landforms, water, plates,
  dominant school, civilization, culture, catchments) get a legend with the document's own
  names and colours. Overlays, each switchable, with the number drawn beside it: rivers;
  sea and air routes; roads (river crossings picked out); ley networks by school; bands
  and their routes; nests (off by default -- there are tens of thousands); key locations
  by family, their chains faint behind; ruins, with cleansed ones bleached and ringed;
  hamlets by role and fortresses; cities, sized by class, capitals ringed, corrupted cities
  ringed in the colour of the school that holds them. The footer reads out the cell under
  the pointer.
- **Inspector.** Click anything for its record: the fields that matter, laid out to read,
  then all fields as a tree and the raw JSON on demand. Where the document joins records
  the inspector links them -- a city to its plan, realm, sovereign, the people whose home
  it is, the people present, its hamlets and fortresses and its wars; a hamlet or fortress
  to its plan and city; a band to its quest hooks; a person to their home and where they
  are, their bonds, the hooks they offer and their story web; a ruin to whoever holds it,
  the camp and shrine in it and what destroyed it; a key location to its plan, chain and
  the ruin it grew from. When markers share a cell the others are listed as "Also here".
  The find box searches cities, ruins, hamlets, fortresses, people, key locations and
  bands by name. Back walks the history.
- **Plans.** City, hamlet, castle and key-location plans in a 2D plan (terrain shaded
  from the plan's own surface, streets, the road out to the world, walls, gates, towers,
  plots and their footprints; north up) and a 3D view (WebGL, metres, no exaggeration;
  orbit, pan, zoom, click a building). A placement-pass selector replays the plan's passes,
  showing houses a later pass upgraded as they were. A plan is fetched only when opened.
- **Local patch.** From a cell, city, ruin or key location, the ground in metres around
  that point: `fwg patch` regenerates the world from its request and cuts up to 512 m at
  down to 0.1 m spacing, shown in 3D with a 1.8 m figure at the centre, with its elevation
  range, relief, detail and warnings. On a world with published terrain detail (stage 9
  on) the patch *is* the height field the plans are built on; the micro-relief inputs are
  offered only for a world without it.
- **World debug.** The summary (stage reached, seed, size, planet, octaves, land, document
  size, generation and indexing time, the viewer's own load times), the systems that ran,
  the generator's `timing_ms`, its warnings, every block's size and counts, `debug_stats`
  and the history: ages, and `history.operations` -- which a generated world never has,
  since only the post-generation APIs write it. The `debug_stats` tables and charts are the
  Python lab's "Graphs and data", one for one: urban population by civilization, parent
  races, the city count through founding and the ages, founding outcomes and diaspora,
  leylines, biome coverage, nearest-city distances, every city's housing and constraints
  (parent race, class, workers, beds, houses, apartments, shortfall, vacant safe cells,
  frontage candidates), the founding rounds, and the raw JSON; then food supply, colleges,
  ruins and landmarks.
- **Races.** Every civilization and every parent race, counted across the document: cities,
  capitals, population (urban and rural), placed workers and beds, hamlets, road and
  frontier forts, cities lost, wars fought, NPCs (dead, important) and the cast (living,
  legends), with charts of urban population, cities and NPCs by civilization and each parent
  race's founding participation. The tables sort by any column.
- **NPCs.** The roster as the Python lab showed it -- its summary, what it reported about
  itself, the posts by site and each site's important people with the quest verbs their
  posts offer -- and every individual NPC in one table: filter by name or post, site,
  civilization, post, alive or dead, important, in the cast; sort by any column; 100 a page.
  A site or a cast member opens in the inspector.
- **Cast.** Every hero, legend and dread in a sortable table (role, archetype, alignment
  and its law and good, tier, fame, civilization, race, school, seat, situation, quest
  hooks), then the Python lab's cards: who and where each is, what they are doing, their
  log and bonds, their persona and -- on request -- the truth behind their faces and hooks.

The address bar keeps the world (`#world=<id>`), so a reload or a shared link reopens it;
the World menu lists every world in the cache.

## The API

All JSON. Errors are `{"error": "<readable message>", "code": "...", "status": N}` --
except a request `fwg` itself refuses, which comes back (400) as fwg's own failure document,
exactly as fwg printed it.

Other web pages cannot drive the server, which runs a program that can spend minutes of CPU
and gigabytes of disk: a POST body must be sent as `application/json` (415 otherwise, since
that is a type a browser will not send across sites unasked), a request whose `Origin` is
another site is refused (403), and when bound to this machine the server answers only
`Host` names of this machine (403), which stops a DNS-rebound name reaching it.

| Route | Answer |
|---|---|
| `GET /api/health` | `{ok, fwg: {path, source, found, patch, ...}, cache: {dir, limit_bytes}}`; `patch` says whether this fwg has the `patch` command |
| `GET /api/controls` | the `fwg controls` catalogue, cached for as long as the executable is unchanged |
| `POST /api/generate` `{seed, overrides}` | `{id, bytes, ms, cached}`. Runs `fwg generate --request` with `recipe_version` 3 into the cache, or finds the world already there. Generations run one at a time. |
| `GET /api/worlds` | `{worlds: [{id, bytes, ms, created, used, request}]}`, most recently used first |
| `GET /api/world/:id/summary` | config, effective_config, completed_stage, systems, warnings, timing_ms, topology, area, phases (titles), per-block `{name, type, bytes, count, arrays}`, layer names with rows and cols, plan counts, and how the world was generated. Never the whole document. |
| `GET /api/world/:id/block/:name` | one top-level block, as written |
| `GET /api/world/:id/block/:name?path=&where=&fields=&offset=&limit=` | part of one: `path` walks into it (`sites`, `sites.3`), `where=field:value` keeps matching array elements (repeatable), `offset`/`limit` page an array, `fields=a,b` keeps only those fields |
| `GET /api/world/:id/layer/:name` | `{name, rows, cols, values}`, values as the document writes them: rows of numbers, row 0 at the north pole |
| `GET /api/world/:id/plan/:kind/:key` | one plan, as written. `city` by `city_uid` (or `site_id`, `name`), `hamlet` by `hamlet_uid` (or `hamlet_id`, `node`), `castle` by `fortress_uid` (or `fortress_id`, `node`), `key_location` by `location_id` |
| `POST /api/patch` `{world, latitude, longitude, span, spacing, detail_height, detail_scale}` | the `fwg.patch` document fwg writes (all but `world` optional, fwg's defaults otherwise). Regenerates the world, so it takes as long as generating it did; cached per world and parameters. 409 for a world an earlier build of fwg generated, 501 for an fwg without the command. |

```bash
curl -H 'Content-Type: application/json' -d '{"seed": 42, "overrides": {"size": 65}}' \
     http://127.0.0.1:8787/api/generate
curl 'http://127.0.0.1:8787/api/world/<id>/block/settlements?path=sites&fields=uid,name,x,z'
```

## How documents are served

A document is not parsed whole. On first use the server reads it once and records where
each top-level block, each block's members, each layer and each plan begin and end
(`lib/json-index.mjs`) -- about 0.4 s for the 277 MB of a size-257 world, against 1.2 s and
about 1 GB of heap to `JSON.parse` it, and a size-513 document is longer than the longest
string V8 can hold, so it cannot be parsed whole at all. A block, layer or plan request is
then a byte range streamed from disk; only the small blocks a summary quotes, and a block
someone asks to select within, are parsed, and those are cached. The indexes of the last
two documents opened are kept in memory.

## The cache

`viewer/.cache/` (ignored by git) holds, per world, `<id>.world.json`, the
`<id>.request.json` it was generated from, `<id>.meta.json` (the request, generation
time, size and the executable's SHA-256) and any `<id>.patch-<hash>.json` cut from it.
The id is a hash of the request and of that
executable: generation is deterministic for one build of fwg, so a repeated request is
served from the cache, and a rebuilt fwg makes new ids rather than serving an old build's
worlds. Worlds are generated with `--include-timings`, so the debug panel has them. When the
cache passes its budget the least recently used worlds are deleted, never one open in the
viewer.

## Sizes

Measured 2026-09-23 in the browser on the development machine, seed 42, phase 16, each
world generated fresh, while other work was running on the machine:

| size | document | fwg generate | server indexes it | then on screen | Generate to on screen | a city plan (2D, then 3D) | nests: count, load, redraw |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 33 | 60 MB | 3.0 s | 0.10 s | 0.14 s | 3.1 s | 44 ms, 32 ms | 13,268, 46 ms, 3 ms |
| 129 | 157 MB | 8.2 s | 0.24 s | 0.28 s | 8.5 s | 22 ms, 37 ms | 44,766, 129 ms, 13 ms |
| 257 | 277 MB | 14.6 s | 0.41 s | 0.47 s | 15.1 s | 18 ms, 25 ms | 65,241, 194 ms, 21 ms |
| 513 | 720 MB | 42.8 s | 1.0 s | 1.1 s | 43.9 s | 27 ms (fetch) | 79,961, 239 ms, 31 ms |

"Then on screen" runs from the generate response to the summary, the first layer and the
default overlays drawn. Switching layers takes 10 to 16 ms. Every size from 3 to 513 opens
without stalling the page; at 513 the server holds about 150 MB after indexing the
document and serving its nests. Size 1025 has not been tried: its document would run to
gigabytes, and the server reads a document whole, once, to index it.

## Test

```bash
node viewer/test/smoke.mjs
```

Starts the server on a free port with a throwaway cache, generates a size-17 world through
the API, and checks the page, the controls, generation and the cache, the summary, a block
and a selection within one, a layer, a city and a castle plan, a local patch (skipped for an
fwg without the command), the errors and refusals, and that other sites and host names are
turned away. Exits non-zero on any failure.

## Files

| | |
|---|---|
| `server.mjs` | the HTTP server and its routes |
| `lib/generator.mjs` | finding and running fwg, request ids, the cache, patches |
| `lib/world-store.mjs` | open documents, summaries, selections within a block |
| `lib/json-index.mjs` | the byte-span index |
| `public/index.html`, `public/css/viewer.css` | the page |
| `public/js/main.js` | wiring: generate, load, layers, overlays, readout, tabs |
| `public/js/world.js` | a loaded world: its blocks and layers, grid and node geometry |
| `public/js/layers.js` | layer names, groups, colour ramps, legends, relief shading |
| `public/js/map.js` | the map canvas: view, pan, zoom, wrap, hit testing |
| `public/js/overlays.js` | what each overlay reads and draws |
| `public/js/inspector.js` | records and the joins between them |
| `public/js/city-view.js`, `city-plan.js`, `city-3d.js` | the plan dialog, plan semantics, the WebGL view |
| `public/js/controls.js`, `debug-panel.js`, `api.js`, `dom.js` | the control panel, the debug panel, the API client, helpers |
| `public/js/races-panel.js`, `npcs-panel.js`, `cast-panel.js` | the Races, NPCs and Cast tabs |
| `public/js/panels.js` | what the report tabs share: cards, bar charts, tables that sort |
