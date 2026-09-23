// The world debug panel: what was generated and how -- the summary, the generator's own
// timings, its warnings, what each block of the document weighs, debug_stats, and the
// history (ages and, once an API has touched the world, its operations). Its "Graphs and
// data" are the Python lab's World debug tables and charts, one for one.

import { bytes, el, num, seconds, titleCase } from './dom.js';
import { bars, card, cell, folded, sortableTable, table } from './panels.js';

/**
 * Render into `container`. `viewerTimings` are the browser-side measurements main.js keeps;
 * `actions.inspect(item)` and `actions.openPlan(kind, key, title)` wire the city table.
 */
export async function renderDebug(container, world, viewerTimings, actions, isCurrent = () => true) {
  const s = world.summary;
  container.replaceChildren(el('p', { class: 'note' }, 'Loading debug statistics…'));
  // A path a world at an early phase lacks is a refused selection, not a missing panel.
  const part = (name, path) => world.select(name, { path }).catch(() => null);
  const [debugStats, history, settlements, economy, colleges, ruins, features, landmarks] = await Promise.all([
    world.block('debug_stats'), world.block('history'), world.block('settlements'), world.block('world_economy'),
    part('magic', 'colleges'), world.block('ruins'), part('terrain', 'features'), part('regions', 'landmarks'),
  ]);
  // Another world, or another render of this one, may have taken the panel meanwhile. The
  // panel is built off the page and put in place in one step, so it is never half of each.
  if (!isCurrent()) return;
  const root = el('div');
  const cfg = s.config ?? {};

  const head = el('div', { class: 'debug-grid' },
    card('World',
      table(['', ''], [
        ['Document', `${s.schema} version ${s.schema_version}, generator ${s.generator_version}`],
        ['Stage reached', `${s.completed_stage} of 16`],
        ['Seed · size · phase', `${cfg.seed} · ${cfg.size} · ${cfg.phase}`],
        ['Planet', `radius ${num(s.topology?.radius_m)} m, ${num((s.config?.circumference_m ?? 0) / 1000)} km around (${cfg.world_size})`],
        ['Cell spacing', `${num(s.spacing_m)} m at the equator`],
        ['Octaves resolved', `${s.resolved_octaves} of ${cfg.octaves}`],
        ['Land', s.area ? `${num(s.area.land_km2)} km² of ${num(s.area.total_km2)} km² (${num(100 * s.area.land_fraction, 1)}%)` : null],
        ['Document size', bytes(s.bytes)],
        ['Generated in', s.generation?.ms ? seconds(s.generation.ms) : 'not recorded'],
        ['Indexed by the server in', seconds(s.index_ms)],
        ['id', world.id],
      ])),
    card('Viewer load times',
      viewerTimings?.length ? table(['Step', 'Time'], viewerTimings.map(([step, ms]) => [step, seconds(ms)]), [1])
        : el('p', { class: 'note' }, 'Measured when a world loads.')),
    card(`Systems that ran (${s.systems?.length ?? 0})`,
      el('div', { class: 'chips' }, (s.systems ?? []).map(name => el('span', { class: 'chip-text' }, name)))),
  );

  const timings = s.timing_ms ? Object.entries(s.timing_ms).filter(([k]) => k !== 'total').sort((a, b) => b[1] - a[1]) : [];
  const timingCard = card('Generator timings (timing_ms)',
    s.timing_ms
      ? [el('p', { class: 'note' }, `total ${seconds(s.timing_ms.total)}; each stage's own wall time, as the generator measured it`),
        bars(timings, { format: v => seconds(v) })]
      : el('p', { class: 'note' }, 'This document was written without timings (fwg generate --include-timings adds them).'));

  const warningCard = card(`Warnings (${s.warnings?.length ?? 0})`,
    s.warnings?.length ? el('ul', { class: 'warnings' }, s.warnings.map(w => el('li', {}, w))) : el('p', { class: 'note' }, 'None.'));

  const blocks = [...s.blocks].sort((a, b) => b.bytes - a.bytes);
  const blockCard = card(`Document blocks (${blocks.length}); ${s.layers.length} layers, plans: ${Object.entries(s.plans ?? {}).map(([k, v]) => `${v} ${k}`).join(', ')}`,
    el('p', { class: 'note' }, 'Bytes of JSON per top-level block. Every city, hamlet and castle plan carries its own copy of the biome and magic catalogues.'),
    table(['Block', 'Type', 'Bytes', 'Share', 'Count', 'Arrays inside'], blocks.map(b => [
      b.name, b.type, bytes(b.bytes), `${num((100 * b.bytes) / s.bytes, 1)}%`, b.count ?? '',
      b.arrays ? Object.entries(b.arrays).map(([k, v]) => `${k} ${num(v)}`).join(', ') : '',
    ]), [2, 3, 4]));

  root.append(
    el('h2', {}, 'World debug'), head,
    el('div', { class: 'debug-grid' }, timingCard, warningCard),
    el('h2', {}, 'Document'), blockCard,
  );

  // debug_stats
  root.append(el('h2', {}, 'debug_stats'));
  if (!debugStats) {
    root.append(el('p', { class: 'note' }, 'This world has no debug_stats block (it is written from stage 10).'));
  } else {
    const d = debugStats;
    const grid = el('div', { class: 'debug-grid' });
    if (d.scope) root.append(el('p', { class: 'note' }, d.scope));
    if (d.civilizations?.length) grid.append(card('Urban population by civilization', bars(d.civilizations.map(c => [titleCase(c.id), c.urban_population]))));
    if (d.parent_races?.length) {
      grid.append(card('Parent races', table(['Race', 'Cities', 'Urban population', 'Workers', 'Beds'],
        d.parent_races.map(r => [r.id, r.cities, r.urban_population, r.workers, r.beds]), [1, 2, 3, 4])));
    }
    if (d.timeline?.length) grid.append(card('Cities through founding and the ages', bars(d.timeline.map(t => [`${t.stage}. ${t.title}`, t.cities]), { alt: true })));
    if (d.founding_outcomes) {
      grid.append(card('Founding outcomes', table(['Outcome', 'Count'], Object.entries(d.founding_outcomes).map(([k, v]) => [titleCase(k), v]), [1]),
        d.diaspora ? el('p', { class: 'note' }, `Diaspora: ${d.diaspora.founded} founded, ${d.diaspora.surviving} surviving, ${d.diaspora.destroyed} destroyed.`) : null));
    }
    if (d.leylines?.length) {
      grid.append(card('Leylines', table(['School', 'Nodes', 'Lines', 'Clusters', 'Spread', 'Model'],
        d.leylines.map(l => [l.id, l.nodes, l.lines, l.distribution?.clusters, l.distribution?.spread, l.distribution?.model]), [1, 2, 3, 4])));
    }
    if (d.biome_coverage?.length) grid.append(card('Natural biome coverage (% of the globe)', bars(d.biome_coverage.map(b => [b.name, b.percent_world]), { format: v => `${num(v, 1)}%` })));
    const spaced = (d.cities ?? []).filter(c => c.nearest_city_m !== null && c.nearest_city_m !== undefined);
    if (spaced.length) grid.append(card('Nearest city distance (metres)', bars(spaced.map(c => [c.name, c.nearest_city_m]), { alt: true })));
    root.append(grid);
    if (d.cities?.length) {
      const sites = new Map((settlements?.sites ?? []).map(site => [site.uid, site]));
      root.append(card(`Cities — housing and constraints (${d.cities.length})`,
        sortableTable(['City', 'Parent race', 'Civilization', 'Class', 'Urban pop.', 'Nearest city (m)', 'Workers', 'Beds', 'Houses',
          'Apartments', 'Shortfall', 'Vacant safe cells', 'Frontage candidates', 'Wall segments', 'Unplaced', 'Plan'],
        d.cities.map(c => {
          const site = sites.get(c.uid);
          return [
            site ? cell(el('button', { type: 'button', class: 'link', onclick: () => actions.inspect({ kind: 'city', record: site }) }, c.name), c.name) : c.name,
            c.parent_race ?? site?.parent_race_id, c.civilization, site?.city_class, c.urban_population_estimate, c.nearest_city_m,
            c.stats?.workers, c.stats?.worker_beds, c.stats?.houses, c.stats?.apartments, c.stats?.housing_shortfall,
            c.placement?.vacant_shape_cells, c.placement?.housing_frontage_candidates, c.stats?.wall_segments, c.stats?.unplaced_total,
            cell(el('button', { type: 'button', class: 'link', onclick: () => actions.openPlan('city', c.uid, `${c.name} — city plan`) }, 'open'), ''),
          ];
        }), { numeric: [4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14], sorted: 4, descending: true }),
        el('p', { class: 'note' }, 'Vacant cells are not necessarily usable plots: a building needs contiguous safe ground and a clear connection to a street. Houses are tried first; apartments upgrade them only once legal plots run out. Beds are for facility workers, not every resident.')));
    }
    if (d.founding_events?.length) {
      root.append(card(`Founding rounds (${d.founding_events.length} events)`,
        sortableTable(['Year', 'Turn', 'Parent race', 'Outcome', 'Civilization', 'Came from', 'Notes', 'Reach (m)'],
          d.founding_events.map(e => [
            e.founding_year ?? null, e.turn, e.parent_race_id, titleCase(e.status), e.population_profile ? titleCase(e.population_profile) : null,
            e.source_civilization_id ? `${titleCase(e.source_civilization_id)} (node ${e.migration_source_node})` : null,
            [e.founding_capital ? 'separate parent origin' : null, e.cultural_branch ? 'cultural branch' : null,
              e.diaspora_reason ? titleCase(e.diaspora_reason) : null, e.diaspora_bonus ? 'parent bonus city' : null].filter(Boolean).join(', ') || null,
            e.founding_capital ? null : e.radius_m,
          ]), { numeric: [0, 1, 7], sorted: 1 })));
    }
    root.append(folded('Raw diagnostic JSON (debug_stats)', el('pre', { class: 'raw' }, JSON.stringify(d, null, 2))));
  }

  // The Python lab's other measured tables, over the finished world.
  root.append(el('h2', {}, 'Food, colleges, ruins and landmarks'));
  const more = el('div', { class: 'debug-grid' });
  more.append(card(`Food supply (${economy?.sites?.length ?? 0} communities)`,
    economy?.sites?.length
      ? sortableTable(['Community', 'Layer', 'Food coverage %', 'Annual shortage', 'Lean months'],
        economy.sites.map(s => [s.name, s.layer, 100 * s.food_coverage, s.annual_shortage, (s.lean_months ?? []).join(', ') || 'none']),
        { numeric: [2, 3], sorted: 2 })
      : el('p', { class: 'note' }, 'No world economy in this world (it is written from stage 12).')));
  more.append(card(`Colleges (${colleges?.length ?? 0})`,
    colleges?.length
      ? sortableTable(['College', 'Civilization', 'Magic density', 'Hazard', 'Access cost (m)'],
        colleges.map(c => [c.id, c.population_profile, c.density, c.hazard, c.access_cost]), { numeric: [2, 3, 4] })
      : el('p', { class: 'note' }, 'No college stands in this world.')));
  more.append(card(`Ruins (${ruins?.length ?? 0})`,
    ruins?.length
      ? sortableTable(['Name', 'Civilization', 'Destroyed age', 'Cause', 'Reason'],
        ruins.map(r => [r.name, r.civilization_id ?? r.population_profile, r.destroyed_age, titleCase(r.cause), r.reason]),
        { numeric: [2], sorted: 2 })
      : el('p', { class: 'note' }, 'No city has fallen in this world.')));
  const marks = [...(features ?? []), ...(landmarks ?? [])];
  more.append(card(`Landmarks (${marks.length})`,
    marks.length
      ? sortableTable(['Kind', 'Label', 'Grid x', 'Grid z'], marks.map(f => [titleCase(f.kind), f.label ?? f.id, f.x, f.z]), { numeric: [2, 3] })
      : el('p', { class: 'note' }, 'No terrain feature or regional landmark in this world.')));
  root.append(more);

  // history
  root.append(el('h2', {}, 'History'));
  const ages = history?.ages ?? [];
  root.append(card(`Ages (${ages.length})`,
    ages.length ? table(['Age', 'Events', 'Wars', 'Surviving cities', 'New cities', 'Moon', 'Order of the turn'], ages.map(a => [
      a.age, a.events?.length, a.wars?.length, a.surviving_city_ids?.length, a.new_city_ids?.length,
      a.moon ? `${num(100 * (a.moon.illumination ?? 0), 0)}% lit, ${a.moon.leaning}` : '—',
      (a.order ?? []).join(' → '),
    ]), [1, 2, 3, 4]) : el('p', { class: 'note' }, 'No age has turned in this world.')));
  const operations = history?.operations;
  root.append(card(`Operations (${operations?.length ?? 0})`,
    operations?.length
      ? table(['#', 'Operation', 'Detail'], operations.map((op, i) => [i + 1, op.operation ?? op.kind ?? op.api ?? Object.keys(op)[0], JSON.stringify(op)]))
      : el('p', { class: 'note' }, 'history.operations is written once an API (advance_ages, corrupt, cleanse, summon, found, raise a band) has changed a world after generation. fwg generate runs none, so a generated document has no operations.')));
  container.replaceChildren(root);
}
