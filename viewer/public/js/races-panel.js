// Races: every civilization and every parent race, counted across the world's blocks.
//
// The Python lab showed urban population by civilization and a parent-race table (cities,
// urban population, placed workers, worker beds), both from debug_stats. Those are here as
// they were, and beside them what each people holds elsewhere in the document: its whole
// population, capitals, hamlets, forts by what they watch, the cities it has lost, its
// wars, its NPCs and its cast. A civilization is the settling people (`population_profile`,
// e.g. hill_dwarf); its parent race is the race it descends from (dwarf). Last, each race's
// and each people's life span and the young, middle and old bands NPC ages are drawn in.

import { el, num, titleCase } from './dom.js';
import { bars, card, cell, sortableTable } from './panels.js';

function blank() {
  return {
    cities: 0, capitals: 0, population: 0, urban: 0, rural: 0, workers: 0, beds: 0, hamlets: 0,
    roadForts: 0, frontierForts: 0, ruins: 0, wars: 0, npcs: 0, npcsDead: 0, important: 0, living: 0, legends: 0,
  };
}

/** One tally per civilization, then summed per parent race. */
function tally({ debugStats, civilizations, settlements, humans, ruins, npcs, heroes }) {
  const civ = new Map();
  const parentOf = new Map();
  const nameOf = new Map();
  for (const e of civilizations?.entities ?? []) {
    parentOf.set(e.id, e.parent_race_id);
    nameOf.set(e.id, e.name);
  }
  const of = id => {
    if (!civ.has(id)) civ.set(id, blank());
    return civ.get(id);
  };

  const sites = settlements?.sites ?? [];
  for (const s of sites) {
    const t = of(s.population_profile);
    if (!parentOf.has(s.population_profile) && s.parent_race_id) parentOf.set(s.population_profile, s.parent_race_id);
    t.cities += 1;
    t.capitals += s.city_class === 'capital' ? 1 : 0;
    t.population += s.population_estimate ?? 0;
    t.urban += s.urban_population_estimate ?? 0;
    t.rural += s.rural_population_estimate ?? 0;
    t.wars += s.war_history?.length ?? 0;
  }
  for (const c of debugStats?.civilizations ?? []) {
    const t = of(c.id);
    t.workers = c.workers ?? 0;
    t.beds = c.beds ?? 0;
  }
  for (const h of humans?.hamlets ?? []) if (h.population_profile) of(h.population_profile).hamlets += 1;
  for (const f of humans?.fortresses ?? []) {
    if (!f.population_profile) continue;
    const t = of(f.population_profile);
    if (f.watch === 'frontier') t.frontierForts += 1;
    else t.roadForts += 1;
  }
  for (const r of ruins ?? []) of(r.civilization_id ?? r.population_profile).ruins += 1;

  const siteCiv = new Map((npcs?.sites ?? []).map(s => [s.uid, s.civilization_id]));
  for (const p of npcs?.people ?? []) {
    const id = siteCiv.get(p.site_uid);
    if (!id) continue;
    const t = of(id);
    t.npcs += 1;
    t.npcsDead += p.status === 'dead' ? 1 : 0;
    t.important += p.important ? 1 : 0;
  }
  for (const p of heroes?.people ?? []) {
    if (!p.civilization_id) continue;
    const t = of(p.civilization_id);
    t.living += p.status === 'living' ? 1 : 0;
    t.legends += p.status === 'legend' ? 1 : 0;
    if (!parentOf.has(p.civilization_id) && p.race_id) parentOf.set(p.civilization_id, p.race_id);
  }

  const parents = new Map();
  for (const [id, t] of civ) {
    const parent = parentOf.get(id) ?? 'unknown';
    if (!parents.has(parent)) parents.set(parent, { ...blank(), civilizations: 0 });
    const p = parents.get(parent);
    p.civilizations += 1;
    for (const key of Object.keys(t)) p[key] += t[key];
  }
  return { civ, parents, parentOf, nameOf };
}

const COLUMNS = ['Cities', 'Capitals', 'Population', 'Urban', 'Rural', 'Workers', 'Beds', 'Hamlets', 'Road forts',
  'Frontier forts', 'Cities lost', 'Wars fought', 'NPCs', 'NPCs dead', 'Important NPCs', 'Cast living', 'Legends'];

function counts(t) {
  return [t.cities, t.capitals, t.population, t.urban, t.rural, t.workers, t.beds, t.hamlets, t.roadForts, t.frontierForts,
    t.ruins, t.wars, t.npcs, t.npcsDead, t.important, t.living, t.legends];
}

export async function renderRaces(container, world, isCurrent = () => true) {
  container.replaceChildren(el('p', { class: 'note' }, 'Loading the peoples of this world…'));
  const [debugStats, civilizations, settlements, humans, ruins, npcs, heroes] = await Promise.all([
    world.block('debug_stats'), world.block('civilizations'), world.block('settlements'), world.block('humans'),
    world.block('ruins'), world.block('npcs'), world.block('heroes'),
  ]);
  if (!isCurrent()) return;
  const root = el('div');
  if (!settlements?.sites) {
    root.append(el('p', { class: 'note' }, 'No cities in this world yet (they are founded at stage 10).'));
    container.replaceChildren(root);
    return;
  }
  const { civ, parents, parentOf, nameOf } = tally({ debugStats, civilizations, settlements, humans, ruins, npcs, heroes });
  const parentName = id => civilizations?.parent_races?.[id]?.name ?? titleCase(id);
  const civName = id => nameOf.get(id) ?? titleCase(id);
  const numeric = COLUMNS.map((_, i) => i + 2);

  root.append(el('h2', {}, 'Races'), el('p', { class: 'note' },
    `${civ.size} civilizations of ${parents.size} parent races. Population is what the settlements block estimates per `
    + 'city (urban plus rural); workers and beds are placed in city plans (debug_stats); wars fought are the wars '
    + 'its standing cities have fought, each war once per city; NPCs are counted by the civilization of the site they '
    + 'live at, the cast by its own civilization. Click a heading to sort.'));

  const grid = el('div', { class: 'debug-grid' });
  const byUrban = [...civ.entries()].sort((a, b) => b[1].urban - a[1].urban);
  grid.append(card('Urban population by civilization', bars(byUrban.map(([id, t]) => [civName(id), t.urban]))));
  grid.append(card('Cities by civilization', bars([...civ.entries()].sort((a, b) => b[1].cities - a[1].cities)
    .map(([id, t]) => [civName(id), t.cities]), { alt: true })));
  if (npcs?.people?.length) {
    grid.append(card('NPCs by civilization', bars([...civ.entries()].filter(([, t]) => t.npcs).sort((a, b) => b[1].npcs - a[1].npcs)
      .map(([id, t]) => [civName(id), t.npcs]))));
  }
  const participation = civilizations?.parent_races ?? {};
  if (Object.keys(participation).length) {
    grid.append(card('Parent races in the founding', sortableTable(['Parent race', 'Founding participation %'],
      Object.entries(participation).map(([id, p]) => [p.name ?? titleCase(id), p.founding_participation_percent]),
      { numeric: [1], sorted: 1, descending: true })));
  }
  root.append(grid);

  root.append(card(`Parent races (${parents.size})`, sortableTable(['Parent race', 'Civilizations', ...COLUMNS],
    [...parents.entries()].map(([id, t]) => [parentName(id), t.civilizations, ...counts(t)]),
    { numeric: [1, ...numeric], sorted: 4, descending: true })));

  root.append(card(`Civilizations (${civ.size})`, sortableTable(['Civilization', 'Parent race', ...COLUMNS],
    [...civ.entries()].map(([id, t]) => [civName(id), parentName(parentOf.get(id) ?? 'unknown'), ...counts(t)]),
    { numeric, sorted: 4, descending: true })));

  // Each parent race's life and each people's, in the three adult bands the roster cut from
  // them and drew every NPC's age in (2026-09-23).
  const spans = npcs?.life_spans;
  if (spans?.peoples?.length) {
    const ages = new Map();
    const raceAges = new Map();
    for (const p of npcs.people ?? []) {
      const a = p.appearance;
      if (!a) continue;
      for (const [map, key] of [[ages, a.civilization_id], [raceAges, a.parent_race_id]]) {
        if (!map.has(key)) map.set(key, []);
        map.get(key).push(a.age_years);
      }
    }
    const mean = list => (list?.length ? list.reduce((sum, x) => sum + x, 0) / list.length : null);
    const band = (span, id) => {
      const b = span.bands?.[id];
      return b ? cell(`${num(b.from)}–${num(b.to)}`, b.from) : null;
    };
    const life = span => [span.max_years, span.adult_years, span.elder_years, band(span, 'young'), band(span, 'middle'), band(span, 'old')];
    const LIFE = ['Longest life', 'Adult from', 'Elder from', 'Young', 'Middle', 'Old', 'NPCs aged', 'Mean age'];
    root.append(card('Life spans (years)',
      el('p', { class: 'note' }, 'Young runs from the adult onset to halfway to the elder onset, middle from there to the elder '
        + 'onset, old from the elder onset to the longest life. A people that states no life of its own lives as its race '
        + 'does. Every NPC and cast member drew a band (young 2 : middle 2 : old 1) and then a year inside it.'),
      sortableTable(['Parent race', ...LIFE], (spans.parent_races ?? []).map(span => [parentName(span.id), ...life(span),
        raceAges.get(span.id)?.length ?? 0, mean(raceAges.get(span.id))]), { numeric: [1, 2, 3, 4, 5, 6, 7, 8], sorted: 1, descending: true }),
      sortableTable(['People', 'Parent race', ...LIFE], spans.peoples.map(span => [civName(span.id), parentName(span.parent_race_id),
        ...life(span), ages.get(span.id)?.length ?? 0, mean(ages.get(span.id))]), { numeric: [2, 3, 4, 5, 6, 7, 8, 9], sorted: 2, descending: true })));
  }

  const npcTotal = npcs?.summary?.total ?? npcs?.people?.length ?? 0;
  const attributed = [...civ.values()].reduce((sum, t) => sum + t.npcs, 0);
  root.append(el('p', { class: 'note' },
    `Checked against the document's own totals: ${num(settlements.sites.length)} cities, `
    + `${num(humans?.fortresses?.length ?? 0)} forts, ${num(npcTotal)} NPCs, `
    + `${num((heroes?.people ?? []).filter(p => p.status === 'living').length)} living cast. `
    + (npcTotal > attributed ? `${num(npcTotal - attributed)} NPCs stand at no planned site, or at a site of no civilization, and are in no row above.` : '')));
  container.replaceChildren(root);
}
