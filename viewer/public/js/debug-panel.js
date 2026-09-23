// The world debug panel: what was generated and how -- the summary, the generator's own
// timings, its warnings, what each block of the document weighs, debug_stats, and the
// history (ages and, once an API has touched the world, its operations).

import { bytes, el, num, seconds, titleCase } from './dom.js';

function table(headers, rows, numeric = []) {
  return el('div', { class: 'scroll' },
    el('table', { class: 'data' },
      el('thead', {}, el('tr', {}, headers.map((h, i) => el('th', { class: numeric.includes(i) ? 'num' : null }, h)))),
      el('tbody', {}, rows.map(row => el('tr', {}, row.map((cell, i) => el('td', { class: numeric.includes(i) ? 'num' : null },
        cell instanceof Node ? cell : typeof cell === 'number' ? num(cell) : cell ?? '—')))))));
}

function bars(rows, { format = num, alt = false } = {}) {
  const max = Math.max(1e-9, ...rows.map(([, v]) => v ?? 0));
  return el('div', { class: 'bars' }, rows.flatMap(([label, value]) => [
    el('span', {}, label),
    el('span', {}, el('div', { class: `bar${alt ? ' alt' : ''}`, style: { width: `${(100 * (value ?? 0)) / max}%` } })),
    el('span', { class: 'value' }, format(value)),
  ]));
}

function card(title, ...children) {
  return el('div', { class: 'card' }, el('h3', {}, title), children);
}

/**
 * Render into `container`. `viewerTimings` are the browser-side measurements main.js keeps;
 * `actions.inspect(item)` and `actions.openPlan(kind, key, title)` wire the city table.
 */
export async function renderDebug(container, world, viewerTimings, actions, isCurrent = () => true) {
  const s = world.summary;
  container.replaceChildren(el('p', { class: 'note' }, 'Loading debug statistics…'));
  const [debugStats, history, settlements] = await Promise.all([world.block('debug_stats'), world.block('history'), world.block('settlements')]);
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
    root.append(grid);
    if (d.cities?.length) {
      const sites = new Map((settlements?.sites ?? []).map(site => [site.uid, site]));
      root.append(card(`Cities (${d.cities.length})`,
        table(['City', 'Civilization', 'Urban pop.', 'Nearest city', 'Workers', 'Beds', 'Houses', 'Apartments', 'Shortfall', 'Wall segments', 'Unplaced', 'Plan'],
          d.cities.map(c => {
            const site = sites.get(c.uid);
            return [
              site ? el('button', { type: 'button', class: 'link', onclick: () => actions.inspect({ kind: 'city', record: site }) }, c.name) : c.name,
              c.civilization, c.urban_population_estimate, c.nearest_city_m === null ? '—' : `${num(c.nearest_city_m)} m`,
              c.stats?.workers, c.stats?.worker_beds, c.stats?.houses, c.stats?.apartments, c.stats?.housing_shortfall,
              c.stats?.wall_segments, c.stats?.unplaced_total,
              el('button', { type: 'button', class: 'link', onclick: () => actions.openPlan('city', c.uid, `${c.name} — city plan`) }, 'open'),
            ];
          }), [2, 3, 4, 5, 6, 7, 8, 9, 10])));
    }
  }

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
