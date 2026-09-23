// The viewer's wiring: generate or pick a world, load its summary, paint a layer, load the
// overlays that are switched on, and route clicks to the inspector and the plan view.

import { api } from './api.js';
import { renderCast } from './cast-panel.js';
import { CityView } from './city-view.js';
import { ControlsPanel } from './controls.js';
import { renderDebug } from './debug-panel.js';
import { renderNpcs } from './npcs-panel.js';
import { renderRaces } from './races-panel.js';
import { $, bytes, el, latLon, num, seconds } from './dom.js';
import { Inspector, searchIndex } from './inspector.js';
import { cataloguesFor, companionsFor, groupLayers, layerInfo, makeColorizer, reliefShading } from './layers.js';
import { MapView, paintBase } from './map.js';
import { OVERLAYS } from './overlays.js';
import { World } from './world.js';

// --- state ---------------------------------------------------------------------------

const state = {
  world: null,
  layerName: null,
  colorizer: null,
  shade: null, // hillshade factors for the current world, once computed
  paintToken: 0,
  loadToken: 0,
  overlays: new Map(), // id -> {def, checkbox, status, enabled, renderer, pending}
  timings: [],
  reports: new Map(), // report tab -> {world it was rendered for, render token}
  search: null,
  patchSupported: undefined,
};
// For measurement and debugging from the console.
window.viewer = state;

const statusLine = $('status');
const progress = $('progress');
const progressBar = progress.querySelector('.progress-bar');

function setStatus(text, { error = false } = {}) {
  statusLine.textContent = text;
  statusLine.classList.toggle('error', error);
  statusLine.title = text;
}

const controls = new ControlsPanel({
  groupsContainer: $('control-groups'),
  note: $('controls-note'),
  search: $('control-search'),
  resetButton: $('controls-reset'),
  seed: $('seed'),
  size: $('size'),
  phase: $('phase'),
});

const map = new MapView($('map'), {
  onHover: hover => showReadout(hover),
  onClick: ({ x, z, hits }) => {
    inspector.show(hits.length ? { ...hits[0], alsoHere: hits.slice(1, 16) } : { kind: 'cell', x, z });
    revealInspector();
  },
});

const cityView = new CityView($('city-dialog'));
state.cityView = cityView;
state.map = map;

const inspector = new Inspector($('inspector-body'), $('inspector-back'), {
  openPlan: (kind, key, title) => cityView.open(state.world.plan(kind, key), title),
  focus: (X, Y) => {
    selectTab('map');
    map.centerOn(X, Y, Math.max(18, map.cellPx()));
  },
  cellItems: (x, z) => itemsAtCell(x, z),
  describeCell: (x, z) => describeCell(x, z),
  openPatch: (latitude, longitude, title) => cityView.openPatch({ world: state.world, latitude, longitude, title }),
  canPatch: () => state.patchSupported !== false,
});

// --- generating ----------------------------------------------------------------------

// Rough generation time, from measured runs (seed 42, this machine): 2 s at 33, 5.5 s at
// 129, 11 s at 257, 33 s at 513. Only drives the progress bar.
function estimateMs(size) {
  return 1500 + size * size * 0.12;
}

async function generate() {
  if (!controls.validate()) return;
  const { seed, overrides } = controls.request();
  const button = $('generate');
  button.disabled = true;
  progress.hidden = false;
  const started = performance.now();
  const expected = estimateMs(overrides.size);
  const tick = setInterval(() => {
    const elapsed = performance.now() - started;
    // eases toward 95% at the estimate and never quite arrives
    progressBar.style.width = `${Math.min(97, 95 * (1 - Math.exp(-elapsed / expected * 1.6)) / (1 - Math.exp(-1.6)))}%`;
    setStatus(`Generating seed ${seed} at size ${overrides.size}${overrides.phase ? `, phase ${overrides.phase}` : ''}… ${(elapsed / 1000).toFixed(1)} s`);
  }, 100);
  try {
    const result = await api.generate(seed, overrides);
    clearInterval(tick);
    progressBar.style.width = '100%';
    const how = result.cached ? `from the cache (generated in ${seconds(result.ms)} earlier)` : `generated in ${seconds(result.ms)}`;
    setStatus(`${bytes(result.bytes)} ${how}; loading…`);
    await loadWorld(result.id, { generation: result, requestedAt: started });
    refreshRecent();
  } catch (error) {
    clearInterval(tick);
    setStatus(error.message, { error: true });
  } finally {
    button.disabled = false;
    setTimeout(() => {
      progress.hidden = true;
      progressBar.style.width = '0';
    }, 400);
  }
}

$('run-form').addEventListener('submit', event => {
  event.preventDefault();
  generate();
});
$('seed-random').addEventListener('click', () => {
  $('seed').value = String(crypto.getRandomValues(new Uint32Array(1))[0]);
});

// --- loading a world -------------------------------------------------------------------

async function loadWorld(id, { generation = null, requestedAt = null } = {}) {
  const token = ++state.loadToken;
  const t0 = performance.now();
  const timings = [];
  const mark = (label, since = t0) => timings.push([label, performance.now() - since]);
  let summary;
  try {
    summary = await api.summary(id);
  } catch (error) {
    setStatus(`Could not open world ${id}: ${error.message}`, { error: true });
    return;
  }
  if (token !== state.loadToken) return;
  mark('Summary (server indexes the document on first open)');

  const world = new World(id, summary);
  state.world = world;
  state.shade = null;
  state.search = null;
  state.reports.clear();
  location.hash = `world=${id}`;
  controls.applyRequest(summary.generation?.request);
  if (summary.phases?.titles?.length) controls.setPhaseTitles(summary.phases.titles);
  map.setWorld(world);
  inspector.setWorld(world);
  $('map-empty').hidden = true;
  buildLayerSelect(world);
  buildOverlayToggles(world);
  $('find').value = '';

  const firstLayer = world.hasLayer('height') ? 'height' : summary.phases?.default_layer ?? world.layerNames[0];
  const layerStart = performance.now();
  await showLayer(firstLayer);
  if (token !== state.loadToken) return;
  mark('First layer fetched and painted', layerStart);

  const overlayStart = performance.now();
  await Promise.all([...state.overlays.values()].filter(o => o.enabled).map(o => loadOverlay(o)));
  if (token !== state.loadToken) return;
  mark('Overlays switched on by default', overlayStart);
  mark('World on screen, from its summary');
  if (requestedAt) timings.push(['From Generate to world on screen', performance.now() - requestedAt]);
  state.timings = timings;

  const s = summary;
  const genText = generation ? `${generation.cached ? 'cached' : `generated in ${seconds(generation.ms)}`}, ` : '';
  setStatus(`Seed ${s.config?.seed}, size ${s.size}, stage ${s.completed_stage}/16 · ${bytes(s.bytes)} · ${genText}on screen in ${seconds(performance.now() - t0)} · ${s.warnings?.length ?? 0} warnings`);
  if (openTab() !== 'map') showReport(openTab());
  // Readout helpers, fetched quietly once the map is up.
  for (const name of ['natural_biome']) world.layer(name).catch(() => {});
  world.block('terrain').catch(() => {});
}

// --- layers ----------------------------------------------------------------------------

function buildLayerSelect(world) {
  const select = $('layer-select');
  select.replaceChildren(...groupLayers(world.layerNames).map(([group, names]) =>
    el('optgroup', { label: group }, names.map(name => el('option', { value: name }, layerInfo(name).label)))));
  select.disabled = !world.layerNames.length;
}

$('layer-select').addEventListener('change', event => showLayer(event.target.value));
$('hillshade').addEventListener('change', () => showLayer(state.layerName));

async function showLayer(name) {
  const world = state.world;
  if (!world || !name) return;
  const token = ++state.paintToken;
  state.layerName = name;
  $('layer-select').value = name;
  $('legend').replaceChildren(el('span', { class: 'note' }, `Loading ${layerInfo(name).label}…`));
  const wantShade = $('hillshade').checked && world.hasLayer('height');
  try {
    const [layer, catalogues, companions, height] = await Promise.all([
      world.layer(name),
      Promise.all(cataloguesFor(name).map(async block => [block, await world.block(block)])).then(Object.fromEntries),
      Promise.all(companionsFor(name).filter(n => world.hasLayer(n)).map(async n => [n, await world.layer(n)])).then(Object.fromEntries),
      wantShade ? world.layer('height') : null,
    ]);
    if (token !== state.paintToken || world !== state.world) return;
    if (wantShade && !state.shade) state.shade = reliefShading(height, world.radius, world.config.sea_level ?? 0);
    const colorizer = makeColorizer(layer, { catalogues, companions, seaLevel: world.config.sea_level ?? 0 });
    state.colorizer = colorizer;
    state.layer = layer;
    map.setBase(paintBase(layer, colorizer, wantShade ? state.shade : null));
    const legend = colorizer.legend();
    $('legend').replaceChildren(...[].concat(legend).filter(Boolean));
  } catch (error) {
    if (token === state.paintToken) $('legend').replaceChildren(el('span', { class: 'bad' }, `Could not show ${name}: ${error.message}`));
  }
}

// --- overlays --------------------------------------------------------------------------

function buildOverlayToggles(world) {
  const container = $('overlay-toggles');
  container.replaceChildren();
  state.overlays.clear();
  for (const def of OVERLAYS) {
    if (!def.available(world)) continue;
    const checkbox = el('input', { type: 'checkbox' });
    checkbox.checked = def.defaultOn;
    const status = el('span', { class: 'loading' });
    const entry = { def, checkbox, status, enabled: def.defaultOn, renderer: null, pending: null };
    state.overlays.set(def.id, entry);
    checkbox.addEventListener('change', async () => {
      entry.enabled = checkbox.checked;
      if (entry.enabled) await loadOverlay(entry);
      applyOverlays();
    });
    container.append(el('label', { class: 'check', title: def.note ?? '' },
      checkbox, el('span', { class: 'swatch', style: { background: def.swatch } }), def.label, status));
  }
}

async function loadOverlay(entry) {
  const world = state.world;
  if (entry.renderer) return entry.renderer;
  if (!entry.pending) {
    entry.status.textContent = ' …';
    const started = performance.now();
    entry.pending = entry.def.load(world).then(renderer => {
      if (world !== state.world) return null;
      entry.renderer = renderer;
      entry.status.textContent = renderer.count !== undefined ? ` ${num(renderer.count)}` : '';
      entry.loadMs = performance.now() - started;
      applyOverlays();
      return renderer;
    }).catch(error => {
      entry.status.textContent = ' failed';
      entry.status.title = error.message;
      entry.pending = null;
      console.error(error);
      return null;
    });
  }
  return entry.pending;
}

function applyOverlays() {
  const active = OVERLAYS.map(def => state.overlays.get(def.id))
    .filter(entry => entry?.enabled && entry.renderer)
    .map(entry => ({ id: entry.def.id, ...entry.renderer }));
  map.setOverlays(active);
}

/** Every item the loaded overlays place on cell (x, z). */
function itemsAtCell(x, z) {
  const out = [];
  for (const entry of state.overlays.values()) {
    if (!entry.renderer || !entry.enabled) continue;
    for (const item of entry.renderer.items ?? []) {
      if (item.record?.x === x && item.record?.z === z) out.push(item);
    }
  }
  return out;
}

// --- readout and cell description ----------------------------------------------------

function describeCell(x, z) {
  const world = state.world;
  const rows = [];
  const i = z * world.n + x;
  if (state.layer && state.colorizer) rows.push([layerInfo(state.layer.name).label, state.colorizer.describe(state.layer.values[i])]);
  const height = syncLayer('height');
  if (height && state.layer?.name !== 'height') rows.push(['Elevation', `${num(height.values[i])} m`]);
  const biome = syncLayer('natural_biome');
  const terrain = syncBlock('terrain');
  if (biome && terrain) rows.push(['Natural biome', terrain.biomes?.find(b => b.id === biome.values[i])?.name ?? biome.values[i]]);
  return rows;
}

// Layers and blocks already fetched, read without waiting (the readout runs per mouse move).
const settled = new WeakMap();
function trackSettled(promise) {
  if (promise && !settled.has(promise)) {
    settled.set(promise, undefined);
    promise.then(value => settled.set(promise, value)).catch(() => {});
  }
  return promise ? settled.get(promise) : undefined;
}
function syncLayer(name) {
  return trackSettled(state.world?.loadedLayer(name));
}
function syncBlock(name) {
  return trackSettled(state.world?.loadedBlock(name));
}

function showReadout(hover) {
  const readout = $('readout');
  const world = state.world;
  if (!hover || !world) {
    readout.textContent = ' ';
    return;
  }
  const { x, z, hits } = hover;
  const hit = hits[0];
  const i = z * world.n + x;
  const parts = [latLon(world.lat(z), world.lon(x)), `cell ${x},${z}`];
  if (state.layer && state.colorizer) parts.push(`${layerInfo(state.layer.name).label}: ${state.colorizer.describe(state.layer.values[i])}`);
  const height = syncLayer('height');
  if (height && state.layer?.name !== 'height') parts.push(`elevation ${num(height.values[i])} m`);
  const biome = syncLayer('natural_biome');
  const terrain = syncBlock('terrain');
  if (biome && terrain && state.layer?.name !== 'natural_biome') {
    const name = terrain.biomes?.find(b => b.id === biome.values[i])?.name;
    if (name) parts.push(name);
  }
  if (hit) parts.push(`▶ ${hitLabel(hit)}${hits.length > 1 ? ` (+${hits.length - 1} more here)` : ''}`);
  readout.textContent = parts.join(' · ');
}

function hitLabel(hit) {
  const r = hit.record;
  switch (hit.kind) {
    case 'city': return `${r.name} (${r.city_class} city${r.corruption ? `, held by ${r.corruption.school}` : ''})`;
    case 'hamlet': return `${r.id} (${r.role} hamlet)`;
    case 'fortress': return `${r.id} (fortress)`;
    case 'ruin': return `ruins of ${r.name} (${r.cause})`;
    case 'band': return `${r.classification} band of ${r.size}`;
    case 'key_location': return `${r.name} (${r.kind.replace(/_/g, ' ')}, tier ${r.tier})`;
    case 'nest': return `${r.name} (${r.class}, tier ${r.tier})`;
    case 'ley_node': return `${r.id} (intensity ${num(r.intensity)})`;
    default: return hit.kind;
  }
}

// --- the find box ------------------------------------------------------------------------

$('find').addEventListener('focus', async () => {
  const world = state.world;
  if (!world || state.search) return;
  state.search = searchIndex(world);
  const entries = await state.search;
  if (world !== state.world) return;
  $('find-list').replaceChildren(...entries.map(e => el('option', { value: e.label })));
});
$('find').addEventListener('change', async event => {
  if (!state.search) return;
  const entries = await state.search;
  const hit = entries.find(e => e.label === event.target.value);
  if (hit) {
    inspector.show(hit.item);
    revealInspector();
  }
});

// --- tabs, panels, recent worlds -------------------------------------------------------

// The report tabs. Each is built once per world, the first time it is shown, off the page
// and put in place in one step; a world loaded meanwhile discards the build.
const REPORTS = {
  debug: {
    view: 'debug-view', label: 'debug panel',
    render: (container, world, actions, isCurrent) => {
      const overlayTimes = [...state.overlays.values()].filter(o => o.loadMs !== undefined).map(o => [`Overlay: ${o.def.label}`, o.loadMs]);
      return renderDebug(container, world, [...state.timings, ...overlayTimes, ['Last map redraw', map.lastDrawMs]], actions, isCurrent);
    },
  },
  races: { view: 'races-view', label: 'race statistics', render: (container, world, actions, isCurrent) => renderRaces(container, world, isCurrent) },
  npcs: { view: 'npcs-view', label: 'NPC roster', render: (container, world, actions, isCurrent) => renderNpcs(container, world, actions, isCurrent) },
  cast: { view: 'cast-view', label: 'cast', render: (container, world, actions, isCurrent) => renderCast(container, world, actions, isCurrent) },
};
const TABS = ['map', ...Object.keys(REPORTS)];

function openTab() {
  return TABS.find(tab => $(`tab-${tab}`).getAttribute('aria-selected') === 'true') ?? 'map';
}

function selectTab(which) {
  for (const tab of TABS) {
    $(`tab-${tab}`).setAttribute('aria-selected', String(tab === which));
    $(tab === 'map' ? 'map-view' : REPORTS[tab].view).hidden = tab !== which;
  }
  if (which === 'map') map.requestRender();
  else showReport(which);
}

async function showReport(which) {
  const report = REPORTS[which];
  const world = state.world;
  const container = $(report.view);
  if (!world) {
    container.replaceChildren(el('p', { class: 'note' }, 'Load a world first.'));
    return;
  }
  const done = state.reports.get(which);
  if (done?.world === world) return;
  const token = (done?.token ?? 0) + 1;
  state.reports.set(which, { world, token });
  const isCurrent = () => state.reports.get(which)?.token === token && state.world === world;
  const actions = {
    inspect: item => {
      inspector.show(item);
      revealInspector();
    },
    openPlan: (kind, key, title) => cityView.open(world.plan(kind, key), title),
  };
  try {
    await report.render(container, world, actions, isCurrent);
  } catch (error) {
    if (!isCurrent()) return;
    container.replaceChildren(el('p', { class: 'bad' }, `Could not build the ${report.label}: ${error.message}`));
    state.reports.delete(which);
  }
}

for (const tab of TABS) $(`tab-${tab}`).addEventListener('click', () => selectTab(tab));

function setPanel(buttonId, className, shown) {
  $('app').classList.toggle(className, !shown);
  $(buttonId).setAttribute('aria-pressed', String(shown));
  requestAnimationFrame(() => map.requestRender());
}
function panelShown(className) {
  return !$('app').classList.contains(className);
}
$('toggle-controls').addEventListener('click', () => setPanel('toggle-controls', 'no-controls', !panelShown('no-controls')));
$('toggle-inspector').addEventListener('click', () => setPanel('toggle-inspector', 'no-inspector', !panelShown('no-inspector')));

// In a narrow window the panels are drawers over the map: start with both closed, and open
// the inspector when something is picked.
const narrow = window.matchMedia('(max-width: 1000px)');
if (narrow.matches) {
  setPanel('toggle-controls', 'no-controls', false);
  setPanel('toggle-inspector', 'no-inspector', false);
}
function revealInspector() {
  if (!panelShown('no-inspector')) setPanel('toggle-inspector', 'no-inspector', true);
}

$('zoom-in').addEventListener('click', () => map.zoomBy(1.6));
$('zoom-out').addEventListener('click', () => map.zoomBy(1 / 1.6));
$('zoom-fit').addEventListener('click', () => map.fit());

async function refreshRecent() {
  const select = $('recent-worlds');
  let worlds = [];
  try {
    worlds = (await api.worlds()).worlds;
  } catch {
    return;
  }
  const describe = w => {
    const o = w.request?.overrides ?? {};
    const extra = Object.keys(o).filter(k => k !== 'size' && k !== 'phase').length;
    return `seed ${w.request?.seed ?? '?'} · size ${o.size ?? '?'}${o.phase ? ` · phase ${o.phase}` : ''}${extra ? ` · +${extra}` : ''} · ${bytes(w.bytes)}`;
  };
  select.replaceChildren(el('option', { value: '' }, worlds.length ? `${worlds.length} in the cache…` : 'none cached yet'),
    ...worlds.map(w => el('option', { value: w.id }, describe(w))));
  select.value = worlds.some(w => w.id === state.world?.id) ? state.world.id : '';
}

$('recent-worlds').addEventListener('change', event => {
  if (event.target.value) {
    setStatus('Opening a cached world…');
    loadWorld(event.target.value);
  }
});

// --- start ----------------------------------------------------------------------------

async function start() {
  try {
    const health = await api.health();
    state.patchSupported = health.fwg.patch;
    if (!health.fwg.found) {
      setStatus(`fwg was not found at ${health.fwg.path}. Build it (see viewer/README.md) or set FWG_EXE, then reload.`, { error: true });
      $('generate').disabled = true;
    }
  } catch (error) {
    setStatus(error.message, { error: true });
    return;
  }
  api.controls().then(catalogue => {
    controls.setCatalogue(catalogue);
    // A world may have opened first; show its request in the controls that just arrived.
    controls.applyRequest(state.world?.summary.generation?.request);
  }).catch(error => {
    $('controls-note').textContent = `Could not load the control catalogue: ${error.message}`;
  });
  await refreshRecent();
  const id = new URLSearchParams(location.hash.slice(1)).get('world');
  if (id) {
    setStatus('Opening the world in the address…');
    await loadWorld(id);
  } else if (!statusLine.classList.contains('error')) {
    setStatus('Ready. Generate a world, or open one from the cache.');
  }
}

window.addEventListener('hashchange', () => {
  const id = new URLSearchParams(location.hash.slice(1)).get('world');
  if (id && id !== state.world?.id) loadWorld(id);
});

start();
