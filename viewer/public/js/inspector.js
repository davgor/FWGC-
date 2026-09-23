// The inspector: one record at a time, readable, with its raw JSON on demand and links to
// the records the document joins it to. Following a link pushes onto a history the back
// button unwinds.
//
// Joins used here, each by the field the writer wrote:
//   city     settlements.sites[].uid  = city_plans.cities[].city_uid, heroes.people[].home.uid,
//            heroes.people[].presence.uid, heroes.realms[].city_uids[], npcs.sites[].uid,
//            threat_assessments.cities[].city_uid; humans.hamlets/fortresses[].core_id = sites[].id
//   hamlet   humans.hamlets[].uid     = hamlet_plans.hamlets[].hamlet_uid; a port is keyed by
//            its ordinal: fisheries.ports[].id = humans.hamlets[].id
//   fortress humans.fortresses[].uid  = castle_plans.castles[].fortress_uid
//   ruin     ruins[].uid (the fallen city's uid), ruins[].id = religion.sites[].ruin_id;
//            heroes.camps[].ruin_uid, heroes.relics[].resting_at
//   band     nomads.groups[].uid      = heroes.quest_hooks[].giver_uid / .target,
//            settlement_candidates[].from_band, encounters.entries[].group_uid
//   hero     heroes.people[].home {kind, uid}, .presence {site_kind, uid}, .realm_uid, .org_uid
//   key loc  key_locations.sites[].id = key_location_plans.plans[].location_id

import { el, latLon, num, titleCase } from './dom.js';
import { World } from './world.js';

export class Inspector {
  /**
   * `actions` supplies what the inspector cannot do alone:
   *   openPlan(kind, key, title), focus(X, Y), cellItems(x, z) -> [items at that cell],
   *   describeCell(x, z) -> [[label, text], ...] for the loaded layers,
   *   openPatch(latitude, longitude, title) and canPatch() -> whether fwg can cut one.
   */
  constructor(body, backButton, actions) {
    this.body = body;
    this.backButton = backButton;
    this.actions = actions;
    this.world = null;
    this.stack = [];
    this.token = 0;
    backButton.addEventListener('click', () => this.back());
  }

  setWorld(world) {
    this.world = world;
    this.stack = [];
    this.backButton.disabled = true;
    this.body.replaceChildren(el('p', { class: 'note' }, 'Click anything on the map to see its record.'));
  }

  /** Show an item {kind, record, ...}; `replace` shows it without adding to the history. */
  show(item, { replace = false } = {}) {
    if (!this.world || !item) return;
    if (!replace) this.stack.push(item);
    else this.stack[this.stack.length - 1] = item;
    this.backButton.disabled = this.stack.length < 2;
    this.render(item);
  }

  back() {
    if (this.stack.length < 2) return;
    this.stack.pop();
    this.backButton.disabled = this.stack.length < 2;
    this.render(this.stack[this.stack.length - 1]);
  }

  render(item) {
    const token = ++this.token;
    // Each record renders into its own element. A renderer still awaiting a block when the
    // next record replaces it goes on writing, but into an element no longer on the page.
    const page = el('div');
    this.body.replaceChildren(page);
    this.body.scrollTop = 0;
    const renderer = RENDERERS[item.kind] ?? renderGeneric;
    const ctx = new Context(this, item, page, () => token === this.token);
    Promise.resolve(renderer(ctx)).catch(error => {
      page.append(el('p', { class: 'bad' }, `Could not show this record: ${error.message}`));
      console.error(error);
    });
  }
}

// What a renderer works with: the world, the item, and helpers that append to the body.
class Context {
  constructor(inspector, item, body, current) {
    this.inspector = inspector;
    this.world = inspector.world;
    this.item = item;
    this.record = item.record;
    this.body = body;
    this.current = current; // false once another record replaced this one
    this.actions = inspector.actions;
  }

  header(kind, title, subtitle) {
    this.body.append(
      el('div', { class: 'kind' }, kind),
      el('h2', {}, title ?? '(unnamed)'),
      subtitle ? el('p', { class: 'subtitle' }, subtitle) : null,
    );
    // Whatever else the click landed on: markers share cells.
    const also = this.item.alsoHere ?? [];
    if (also.length) {
      this.body.append(el('p', { class: 'note' }, 'Also here: ',
        also.map((other, i) => [i ? ' · ' : '', this.link(siteLabel(other), other)])));
    }
  }

  buttons(...buttons) {
    const row = el('div', { class: 'actions' }, buttons.filter(Boolean));
    this.body.append(row);
    return row;
  }

  button(label, onclick, title) {
    return el('button', { type: 'button', onclick, title }, label);
  }

  focusButton(x, z) {
    if (x === undefined || z === undefined || x === null) return null;
    return this.button('Show on map', () => this.actions.focus(...this.world.cellXY(x, z)));
  }

  // The ground in metres around cell (x, z), cut by `fwg patch`.
  patchButton(x, z, what) {
    if (x === undefined || z === undefined || x === null || !this.actions.openPatch) return null;
    const available = this.actions.canPatch?.() ?? true;
    const button = this.button('Local patch', () => this.actions.openPatch(this.world.lat(z), this.world.lon(x), `Local patch · ${what}`),
      available ? 'The ground in metres around this point, cut by fwg patch' : 'This fwg has no patch command; rebuild the generator');
    button.disabled = !available;
    return button;
  }

  section(title) {
    const node = el('div', {}, el('h3', {}, title));
    this.body.append(node);
    return node;
  }

  /** A table of [label, value] rows; rows with an undefined/null value are skipped. */
  facts(rows, parent = this.body) {
    const table = el('table', { class: 'facts' });
    for (const [label, value] of rows) {
      if (value === undefined || value === null || value === '') continue;
      table.append(el('tr', {}, el('th', {}, label), el('td', {}, value instanceof Node ? value : format(value))));
    }
    if (table.children.length) parent.append(table);
    return table;
  }

  /** A link that opens another record in the inspector. */
  link(label, item) {
    return el('button', { type: 'button', class: 'link', onclick: () => this.inspector.show(item) }, label);
  }

  /** A list of links, or a note when there are none. */
  links(parent, items, empty) {
    if (!items.length) {
      parent.append(el('p', { class: 'note' }, empty));
      return;
    }
    parent.append(el('ul', { class: 'links' }, items.map(node => el('li', {}, node))));
  }

  /** Append a section filled in once `fill(section)` resolves, if still showing. */
  async later(title, fill) {
    const node = this.section(title);
    const waiting = el('p', { class: 'note' }, 'Loading…');
    node.append(waiting);
    try {
      await fill(node);
    } catch (error) {
      if (this.current()) node.append(el('p', { class: 'bad' }, error.message));
    } finally {
      waiting.remove();
    }
  }

  /** "All fields" and "Raw JSON", both built only when opened. */
  rawSections(record = this.record) {
    const all = el('details', {}, el('summary', {}, 'All fields'));
    all.addEventListener('toggle', () => {
      if (all.open && all.children.length === 1) all.append(el('div', { class: 'tree' }, tree(record)));
    });
    const raw = el('details', {}, el('summary', {}, 'Raw JSON'));
    raw.addEventListener('toggle', () => {
      if (raw.open && raw.children.length === 1) raw.append(el('pre', { class: 'raw' }, JSON.stringify(record, null, 2)));
    });
    this.body.append(el('h3', {}, 'Record'), all, raw);
  }
}

// --- value formatting ------------------------------------------------------------------

function format(value) {
  if (value === null || value === undefined) return '—';
  if (typeof value === 'number') return num(value);
  if (typeof value === 'boolean') return value ? 'yes' : 'no';
  if (Array.isArray(value)) {
    if (!value.length) return 'none';
    if (value.every(v => typeof v !== 'object' || v === null)) return value.map(v => format(v)).join(', ');
    return `${value.length} entries`;
  }
  if (typeof value === 'object') return JSON.stringify(value);
  return String(value).replace(/_/g, ' ');
}

// A collapsible tree of a JSON value; children are built when their branch opens.
function tree(value, key) {
  const label = key === undefined ? null : el('span', { class: 'key' }, `${key}: `);
  if (value === null || typeof value !== 'object') {
    return el('div', { class: 'leaf' }, label, typeof value === 'string' ? value : JSON.stringify(value));
  }
  const entries = Array.isArray(value) ? value.map((v, i) => [i, v]) : Object.entries(value);
  if (!entries.length) return el('div', { class: 'leaf' }, label, Array.isArray(value) ? '[]' : '{}');
  const summaryText = Array.isArray(value) ? `[${value.length}]` : `{${entries.length}}`;
  const node = el('details', {}, el('summary', {}, label, summaryText));
  node.addEventListener('toggle', () => {
    if (!node.open || node.children.length > 1) return;
    const shown = entries.slice(0, 500);
    for (const [k, v] of shown) node.append(tree(v, k));
    if (entries.length > shown.length) node.append(el('div', { class: 'leaf note' }, `… ${entries.length - shown.length} more (see Raw JSON)`));
  });
  return node;
}

// --- lookups over loaded blocks, memoised per world ---------------------------------

function byField(list, field) {
  const map = new Map();
  for (const item of list ?? []) if (item?.[field] !== undefined && item[field] !== null) map.set(String(item[field]), item);
  return map;
}

function groupBy(list, keyOf) {
  const map = new Map();
  for (const item of list ?? []) {
    const keys = [].concat(keyOf(item) ?? []);
    for (const key of keys) {
      if (key === undefined || key === null) continue;
      if (!map.has(String(key))) map.set(String(key), []);
      map.get(String(key)).push(item);
    }
  }
  return map;
}

const lookups = {
  sites: w => w.index('sites', async () => {
    const s = await w.block('settlements');
    return { byUid: byField(s?.sites, 'uid'), byId: byField(s?.sites, 'id') };
  }),
  humans: w => w.index('humans', async () => {
    const h = await w.block('humans');
    return {
      hamletByUid: byField(h?.hamlets, 'uid'), hamletById: byField(h?.hamlets, 'id'),
      fortByUid: byField(h?.fortresses, 'uid'), fortById: byField(h?.fortresses, 'id'),
      hamletsByCore: groupBy(h?.hamlets, x => x.core_id), fortsByCore: groupBy(h?.fortresses, x => x.core_id),
    };
  }),
  ruins: w => w.index('ruins', async () => {
    const r = await w.block('ruins');
    return { byUid: byField(r, 'uid'), byId: byField(r, 'id') };
  }),
  heroes: w => w.index('heroes', async () => {
    const h = await w.block('heroes');
    const people = [...(h?.people ?? []), ...(h?.dreads ?? [])];
    return {
      block: h,
      byUid: byField(people, 'uid'),
      byHome: groupBy(people, p => p.home?.uid),
      byPresence: groupBy(people, p => p.presence?.uid),
      hooksByGiver: groupBy(h?.quest_hooks, q => q.giver_uid),
      hooksByTarget: groupBy(h?.quest_hooks, q => [q.target, q.actual_effect?.uid]),
      realmByCity: groupBy(h?.realms, r => r.city_uids),
      realmByUid: byField(h?.realms, 'uid'),
      orgByUid: byField(h?.orgs, 'uid'),
      campByUid: byField(h?.camps, 'uid'),
      campByRuin: groupBy(h?.camps, c => c.ruin_uid),
      relicsAt: groupBy(h?.relics, r => r.resting_at),
    };
  }),
  keylocs: w => w.index('keylocs', async () => {
    const k = await w.block('key_locations');
    return { block: k, byId: byField(k?.sites, 'id'), chainById: byField(k?.chains, 'id'), clusterById: byField(k?.clusters, 'id') };
  }),
  civs: w => w.index('civs', async () => {
    const c = await w.block('civilizations');
    return { byId: byField(c?.entities, 'id') };
  }),
};

// A site named by {kind, uid}, as heroes' home and presence name them, as an inspector item.
async function resolveSite(world, kind, uid) {
  if (!uid) return null;
  if (kind === 'city') {
    const site = (await lookups.sites(world)).byUid.get(uid);
    if (site) return { kind: 'city', record: site };
    const ruin = (await lookups.ruins(world)).byUid.get(uid);
    return ruin ? { kind: 'ruin', record: ruin } : null;
  }
  if (kind === 'ruin') {
    const ruin = (await lookups.ruins(world)).byUid.get(uid) ?? (await lookups.ruins(world)).byId.get(uid);
    return ruin ? { kind: 'ruin', record: ruin } : null;
  }
  if (kind === 'hamlet') {
    const h = (await lookups.humans(world)).hamletByUid.get(uid);
    return h ? { kind: 'hamlet', record: h } : null;
  }
  if (kind === 'port') {
    // A port is keyed by its ordinal id, which it shares with its hamlet record.
    const h = (await lookups.humans(world)).hamletById.get(uid);
    return h ? { kind: 'hamlet', record: h } : null;
  }
  if (kind === 'fortress') {
    const f = (await lookups.humans(world)).fortByUid.get(uid);
    return f ? { kind: 'fortress', record: f } : null;
  }
  if (kind === 'camp') {
    const c = (await lookups.heroes(world)).campByUid.get(uid);
    return c ? { kind: 'camp', record: c } : null;
  }
  if (kind === 'key_location') {
    const k = (await lookups.keylocs(world)).byId.get(uid);
    return k ? { kind: 'key_location', record: k } : null;
  }
  if (kind === 'shrine') {
    const religion = await world.block('religion');
    const site = (religion?.sites ?? []).find(s => s.id === uid);
    return site ? { kind: 'shrine', record: site } : null;
  }
  if (kind === 'nest') {
    const found = await world.select('beast_nests', { path: 'sites', where: [`id:${uid}`] });
    return found?.[0] ? { kind: 'nest', record: found[0], block: 'beast_nests' } : null;
  }
  return null;
}

function siteLabel(item) {
  const r = item.record;
  if (item.kind === 'city') return `${r.name} (city)`;
  if (item.kind === 'ruin') return `ruins of ${r.name}`;
  if (item.kind === 'hamlet') return `${r.id} (${r.role} hamlet)`;
  if (item.kind === 'fortress') return `${r.id} (fortress)`;
  if (item.kind === 'camp') return r.name;
  if (item.kind === 'key_location') return `${r.name} (${titleCase(r.kind)})`;
  if (item.kind === 'shrine') return `${titleCase(r.kind)} ${r.id}`;
  if (item.kind === 'band') return `${r.classification} band ${r.uid}`;
  if (item.kind === 'nest') return `${r.name} (${r.class} nest)`;
  if (item.kind === 'ley_node') return `${r.id} (ley node)`;
  return r.name ?? r.id ?? r.uid ?? item.kind;
}

function heroLabel(p) {
  return `${p.display_name ?? p.name ?? p.uid}${p.role ? ` — ${titleCase(p.role)}` : ''}${p.status && p.status !== 'living' ? ` (${p.status})` : ''}`;
}

function hookNode(ctx, hook, { showGiver = false } = {}) {
  const node = el('div', { class: 'hook' },
    el('div', {}, el('span', { class: 'verb' }, hook.verb ?? '?'), ` · difficulty ${hook.difficulty ?? '?'}${hook.unwitting ? ' · unwitting' : ''}`),
    el('div', {}, hook.stated_purpose ?? ''),
    el('div', { class: 'note' }, `target ${hook.target ?? '—'}; effect ${hook.actual_effect?.kind ?? '?'}${hook.actual_effect?.action ? ` (${hook.actual_effect.action})` : ''}`));
  node.append(el('div', {}, ctx.link('Open this hook', { kind: 'hook', record: hook })));
  if (showGiver) node.dataset.giver = hook.giver_uid;
  return node;
}

// --- renderers by kind ----------------------------------------------------------------

async function renderCity(ctx) {
  const { world, record: site } = ctx;
  const civ = (await lookups.civs(world)).byId.get(site.civilization_id ?? site.population_profile);
  ctx.header(`City · ${site.city_class ?? 'unclassified'}`, site.name,
    [civ?.name ?? titleCase(site.population_profile), site.parent_race_id ? `${site.parent_race_id} race` : null, site.name_gloss ? `“${site.name_gloss}”` : null].filter(Boolean).join(' · '));
  const hasPlan = world.summary.plans?.city > 0;
  ctx.buttons(
    hasPlan ? ctx.button('Open city plan', () => ctx.actions.openPlan('city', site.uid, `${site.name} — city plan`)) : null,
    ctx.focusButton(site.x, site.z),
    ctx.patchButton(site.x, site.z, site.name),
  );
  if (site.corruption) {
    const c = site.corruption;
    ctx.body.append(el('p', { class: 'warn' }, `Held by ${titleCase(c.school)} (${c.cause}) since age ${c.age}; corruption ${c.corruption}. Only a cleanse ends the hold, and a cleanse destroys the city.`));
  }
  ctx.facts([
    ['Population', site.population_estimate !== undefined ? `${num(site.population_estimate)} (${num(site.urban_population_estimate)} urban, ${num(site.rural_population_estimate)} rural)` : undefined],
    ['Founded', site.founding_year !== undefined ? `year ${num(site.founding_year)}, round ${site.founding_turn}${site.founding_capital ? ', founding capital' : ''}` : undefined],
    ['Founded in age', site.founded_age],
    ['Suitability', site.suitability],
    ['Why here', site.reason],
    ['Classification', site.classification_reason],
    ['Height', site.height_m !== undefined ? `${num(site.height_m)} m` : undefined],
    ['Slope', site.slope_degrees !== undefined ? `${num(site.slope_degrees)}°` : undefined],
    ['Temperature', site.temperature_c !== undefined ? `${num(site.temperature_c)} °C` : undefined],
    ['Moisture', site.moisture],
    ['Flood risk', site.flood_risk],
    ['Freshwater', site.freshwater_distance_m === null ? 'none in reach' : site.freshwater_distance_m !== undefined ? `${num(site.freshwater_distance_m)} m` : undefined],
    ['Community', site.community_traits],
    ['Where', `${latLon(world.lat(site.z), world.lon(site.x))} · cell ${site.x}, ${site.z} · node ${site.node}`],
    ['uid', site.uid],
  ]);

  await ctx.later('Links', async section => {
    const [heroes, humans, npcs, threats] = await Promise.all([
      lookups.heroes(world), lookups.humans(world),
      world.has('npcs') ? world.select('npcs', { path: 'sites', where: [`uid:${site.uid}`] }) : null,
      world.has('threat_assessments') ? world.select('threat_assessments', { path: 'cities', where: [`city_uid:${site.uid}`] }) : null,
    ]);
    if (!ctx.current()) return;
    const realm = heroes.realmByCity.get(site.uid)?.[0];
    if (realm) {
      const sovereign = heroes.byUid.get(realm.sovereign_uid);
      ctx.facts([
        ['Realm', `${realm.name} (${realm.english_name ?? ''})`],
        ['Sovereign', sovereign ? ctx.link(heroLabel(sovereign), { kind: 'hero', record: sovereign }) : realm.sovereign_uid],
        ['Realm cities', realm.city_uids?.length],
      ], section);
    }
    const npcSite = npcs?.[0];
    if (npcSite) ctx.facts([['Staffed posts', `${num(npcSite.posts)} (plan ${npcSite.plan_status})`]], section);
    const threat = threats?.[0];
    if (threat) {
      ctx.facts([
        ['Regional threat', threat.regional_threat],
        ['War risk', threat.war_risk !== undefined ? `${num(threat.war_risk)}${threat.war_risk_kind ? ` (${threat.war_risk_kind})` : ''}` : undefined],
      ], section);
    }
    const home = heroes.byHome.get(site.uid) ?? [];
    const here = (heroes.byPresence.get(site.uid) ?? []).filter(p => !home.includes(p));
    section.append(el('h3', {}, `People whose home it is (${home.length})`));
    ctx.links(section, home.map(p => ctx.link(heroLabel(p), { kind: 'hero', record: p })), 'Nobody in the cast calls this city home.');
    section.append(el('h3', {}, `People present (${here.length})`));
    ctx.links(section, here.map(p => ctx.link(heroLabel(p), { kind: 'hero', record: p })), 'Nobody else in the cast is here.');
    const hamlets = humans.hamletsByCore.get(String(site.id)) ?? [];
    const forts = humans.fortsByCore.get(String(site.id)) ?? [];
    section.append(el('h3', {}, `Hinterland (${hamlets.length} hamlets, ${forts.length} fortresses)`));
    ctx.links(section, [
      ...hamlets.map(h => ctx.link(`${h.id} — ${h.role}`, { kind: 'hamlet', record: h })),
      ...forts.map(f => ctx.link(`${f.id} — fortress`, { kind: 'fortress', record: f })),
    ], 'No hamlets or fortresses support it.');
    const wars = site.war_history ?? [];
    if (wars.length) {
      section.append(el('h3', {}, `Wars (${wars.length})`));
      const items = [];
      for (const war of wars) {
        const opponent = await resolveSite(world, 'city', war.opponent_uid);
        items.push(el('span', {}, `Age ${war.age}, ${war.kind}: ${war.outcome} against `,
          opponent ? ctx.link(siteLabel(opponent), opponent) : war.opponent_uid ?? 'unknown'));
      }
      ctx.links(section, items, '');
    }
  });
  ctx.rawSections();
}

async function renderHamlet(ctx) {
  const { world, record: h } = ctx;
  const sites = await lookups.sites(world);
  const parent = sites.byId.get(String(h.core_id));
  ctx.header(`Hamlet · ${h.role ?? ''}`, h.id, parent ? `Supports ${parent.name}` : null);
  ctx.buttons(
    world.summary.plans?.hamlet > 0 ? ctx.button('Open hamlet plan', () => ctx.actions.openPlan('hamlet', h.uid, `${h.id} — hamlet plan`)) : null,
    ctx.focusButton(h.x, h.z),
  );
  ctx.facts([
    ['Parent city', parent ? ctx.link(parent.name, { kind: 'city', record: parent }) : h.core_id],
    ['Role', h.role],
    ['People', h.population_profile],
    ['Culture', h.culture_id],
    ['Height', h.height_m !== undefined ? `${num(h.height_m)} m` : undefined],
    ['Access cost', h.access_cost !== undefined ? `${num(h.access_cost)} m` : undefined],
    ['Worked area', h.worked_area_km2 !== undefined ? `${num(h.worked_area_km2)} km²` : undefined],
    ['Food delivered', h.delivered_food],
    ['Materials delivered', h.delivered_materials],
    ['Irrigation benefit', h.irrigation_benefit],
    ['Why', h.reason],
    ['Where', `${latLon(world.lat(h.z), world.lon(h.x))} · cell ${h.x}, ${h.z} · node ${h.node}`],
    ['uid', h.uid],
  ]);
  await ctx.later('Links', async section => {
    const [heroes, fisheries] = await Promise.all([lookups.heroes(world), world.block('fisheries')]);
    if (!ctx.current()) return;
    const port = (fisheries?.ports ?? []).find(p => p.id === h.id);
    if (port) {
      ctx.facts([
        ['Port', `${port.role}; harbour quality ${num(port.harbor_quality)}${port.trade_terminal ? '; trade terminal' : ''}`],
        ['Fishing grounds', port.worked_area_km2 !== undefined ? `${num(port.worked_area_km2)} km²` : undefined],
      ], section);
    }
    const people = [...(heroes.byPresence.get(h.uid) ?? []), ...(heroes.byPresence.get(h.id) ?? [])];
    section.append(el('h3', {}, `People present (${people.length})`));
    ctx.links(section, people.map(p => ctx.link(heroLabel(p), { kind: 'hero', record: p })), 'Nobody in the cast is here.');
  });
  ctx.rawSections();
}

async function renderFortress(ctx) {
  const { world, record: f } = ctx;
  const parent = (await lookups.sites(world)).byId.get(String(f.core_id));
  ctx.header('Fortress', f.id, parent ? `Guards the roads of ${parent.name}` : null);
  ctx.buttons(
    world.summary.plans?.castle > 0 ? ctx.button('Open castle plan', () => ctx.actions.openPlan('castle', f.uid, `${f.id} — castle plan`)) : null,
    ctx.focusButton(f.x, f.z),
  );
  ctx.facts([
    ['Parent city', parent ? ctx.link(parent.name, { kind: 'city', record: parent }) : f.core_id],
    ['People', f.population_profile],
    ['Defence score', f.defence_score],
    ['Protects route node', f.protected_route_node],
    ['Access cost', f.access_cost !== undefined ? `${num(f.access_cost)} m` : undefined],
    ['Why', f.reason],
    ['Where', `${latLon(world.lat(f.z), world.lon(f.x))} · cell ${f.x}, ${f.z} · node ${f.node}`],
    ['uid', f.uid],
  ]);
  await ctx.later('Links', async section => {
    const heroes = await lookups.heroes(world);
    if (!ctx.current()) return;
    const people = heroes.byPresence.get(f.uid) ?? [];
    section.append(el('h3', {}, `People present (${people.length})`));
    ctx.links(section, people.map(p => ctx.link(heroLabel(p), { kind: 'hero', record: p })), 'Nobody in the cast is here.');
  });
  ctx.rawSections();
}

async function renderRuin(ctx) {
  const { world, record: r } = ctx;
  const cleansed = r.cause === 'cleansed';
  ctx.header(cleansed ? 'Ruin · cleansed' : 'Ruin', `Ruins of ${r.name}`, `${titleCase(r.cause)} in age ${r.destroyed_age}`);
  ctx.buttons(ctx.focusButton(r.x, r.z), ctx.patchButton(r.x, r.z, `ruins of ${r.name}`));
  ctx.facts([
    ['What happened', r.reason],
    ['Was', `${r.city_class ?? ''} city of the ${r.civilization_id ?? r.population_profile}, founded in age ${r.founded_age}`],
    ['Probability / roll', r.probability !== undefined ? `${num(r.probability)} / ${num(r.roll)}` : undefined],
    ['Key point', r.new_node_school ? `${titleCase(r.new_node_school)}${r.legacy ? `, intensity ${r.legacy.intensity} (${r.legacy.basis})` : ''}` : 'none'],
    ['Corruption', r.corruption],
    ['Where', `${latLon(world.lat(r.z), world.lon(r.x))} · cell ${r.x}, ${r.z} · node ${r.node}`],
    ['id', r.id],
    ['City uid', r.uid],
  ]);
  await ctx.later('Links', async section => {
    const [heroes, religion] = await Promise.all([lookups.heroes(world), world.block('religion')]);
    if (!ctx.current()) return;
    const opponent = r.evidence?.opponent_uid ? await resolveSite(world, 'city', r.evidence.opponent_uid) : null;
    if (opponent) ctx.facts([['Fell to', ctx.link(siteLabel(opponent), opponent)]], section);
    const camps = heroes.campByRuin.get(r.uid) ?? [];
    const shrines = (religion?.sites ?? []).filter(s => s.ruin_id === r.id);
    const relics = heroes.relicsAt.get(r.uid) ?? heroes.relicsAt.get(r.id) ?? [];
    const people = heroes.byPresence.get(r.uid) ?? [];
    section.append(el('h3', {}, `People present (${people.length})`));
    ctx.links(section, people.map(p => ctx.link(heroLabel(p), { kind: 'hero', record: p })), 'Nobody in the cast is here.');
    if (camps.length || shrines.length || relics.length) {
      section.append(el('h3', {}, 'In the ruins'));
      ctx.links(section, [
        ...camps.map(c => ctx.link(c.name, { kind: 'camp', record: c })),
        ...shrines.map(s => ctx.link(`${titleCase(s.kind)} of ${s.god_id}`, { kind: 'shrine', record: s })),
        ...relics.map(x => el('span', {}, `${x.name} (${x.kind})`)),
      ], '');
    }
  });
  ctx.rawSections();
}

async function renderBand(ctx) {
  const { world, record: band } = ctx;
  ctx.header(`Band · ${band.classification}`, band.uid, `${num(band.size)} people, ${band.disposition}; ${band.route_status}`);
  ctx.buttons(ctx.focusButton(band.x, band.z));
  ctx.facts([
    ['Seeks', band.seeks],
    ['Carries', band.carries],
    ['Speed', band.speed_m_per_day !== undefined ? `${num(band.speed_m_per_day)} m a day` : undefined],
    ['Column', band.column_length_m !== undefined ? `${num(band.column_length_m)} m` : undefined],
    ['Round', band.round_length_m !== undefined ? `${num(band.round_length_m)} m` : undefined],
    ['Camps', band.camps?.length],
    ['Legs', band.legs?.length],
    ['God / school', band.god_id ? `${band.god_id} / ${band.school}` : undefined],
    ['Origin', band.origin ? `${band.origin.kind}${band.origin.id ? ` ${band.origin.id}` : ''}, age ${band.origin.age}` : undefined],
    ['Parent band', band.parent_uid],
    ['Where', `${latLon(world.lat(band.z), world.lon(band.x))} · cell ${band.x}, ${band.z} · node ${band.node}`],
  ]);
  await ctx.later('Quest hooks', async section => {
    const heroes = await lookups.heroes(world);
    if (!ctx.current()) return;
    const hooks = [...new Set([...(heroes.hooksByGiver.get(band.uid) ?? []), ...(heroes.hooksByTarget.get(band.uid) ?? [])])];
    if (!hooks.length) {
      section.append(el('p', { class: 'note' }, 'No hook in heroes.quest_hooks names this band as giver or target. Bands give hooks when a cleanse raises survivors from a destroyed city, and generation runs no cleanse.'));
    }
    for (const hook of hooks) section.append(hookNode(ctx, hook));
    const candidates = await world.block('settlement_candidates');
    const mine = (candidates ?? []).filter(c => c.from_band === band.uid);
    if (mine.length) {
      section.append(el('h3', {}, 'Settlement candidates'));
      ctx.links(section, mine.map(c => el('span', {}, `${c.id}: ${c.reason}`)), '');
    }
    if (band.parent_uid) {
      const nomads = await world.block('nomads');
      const parent = (nomads?.groups ?? []).find(g => g.uid === band.parent_uid);
      if (parent) section.append(el('p', {}, 'Split from ', ctx.link(parent.uid, { kind: 'band', record: parent })));
    }
  });
  ctx.rawSections();
}

// The slay hooks sent against a lair: who hunts the thing that lives there.
function huntsOf(heroes, nestId) {
  return nestId ? (heroes.block?.quest_hooks ?? []).filter(q => q.verb === 'slay' && q.actual_effect?.nest_id === nestId) : [];
}

// Each hunt under the name of whoever asks for it.
function appendHunts(ctx, heroes, section, hunts) {
  for (const hook of hunts) {
    const giver = heroes.byUid.get(hook.giver_uid);
    section.append(el('p', {}, giver ? ctx.link(heroLabel(giver), { kind: 'hero', record: giver }) : hook.giver_uid, ' asks:'),
      hookNode(ctx, hook));
  }
}

async function renderHero(ctx) {
  const { world, record: p } = ctx;
  if (p.role === 'dread') return renderDread(ctx);
  ctx.header(`Person · ${titleCase(p.role ?? '')}`, p.display_name ?? p.name ?? p.uid,
    [p.archetype_name, p.alignment?.label, p.tier ? titleCase(p.tier) : null, p.status].filter(Boolean).join(' · '));
  if (p.situation) ctx.body.append(el('p', {}, p.situation));
  const heroes = await lookups.heroes(world);
  const home = p.home ? await resolveSite(world, p.home.kind, p.home.uid) : null;
  const presence = p.presence ? await resolveSite(world, p.presence.site_kind, p.presence.uid) : null;
  if (!ctx.current()) return;
  ctx.buttons(presence?.record?.x !== undefined ? ctx.focusButton(presence.record.x, presence.record.z) : null);
  const realm = p.realm_uid ? heroes.realmByUid.get(p.realm_uid) : null;
  const org = p.org_uid ? heroes.orgByUid.get(p.org_uid) : null;
  ctx.facts([
    ['Home', p.home ? (home ? ctx.link(siteLabel(home), home) : `${p.home.kind} ${p.home.uid}`) : 'none recorded'],
    ['Present at', p.presence ? el('span', {}, presence ? ctx.link(siteLabel(presence), presence) : `${p.presence.site_kind} ${p.presence.uid}`, ` (${p.presence.situation})`) : 'nowhere; a legend'],
    ['Civilization', p.civilization_id],
    ['Race', p.race_id],
    ['Born in age', p.born_age],
    ['Fame', p.fame],
    ['Claim', p.claim ? `${p.claim.verb} ${p.claim.target_name}` : undefined],
    ['Realm', realm ? `${realm.name}` : p.realm_uid],
    ['Order', org ? `${org.name} (${org.kind})` : p.org_uid],
    ['School', p.school],
    ['Lost', p.lost ? `${p.lost.name} (${p.lost.cause}, age ${p.lost.destroyed_age})` : undefined],
    ['uid', p.uid],
  ]);
  const people = uids => (uids ?? []).map(uid => heroes.byUid.get(uid)).filter(Boolean).map(q => ctx.link(heroLabel(q), { kind: 'hero', record: q }));
  const bonds = [['Rivals', p.rivals], ['Kin', p.kin], ['Allies', p.allies]].filter(([, list]) => list?.length);
  if (bonds.length) {
    const section = ctx.section('Bonds');
    for (const [label, list] of bonds) {
      section.append(el('div', {}, `${label}: `));
      ctx.links(section, people(list), '');
    }
  }
  const hooks = heroes.hooksByGiver.get(p.uid) ?? [];
  const hookSection = ctx.section(`Quest hooks offered (${hooks.length})`);
  if (!hooks.length) hookSection.append(el('p', { class: 'note' }, 'None.'));
  for (const hook of hooks) hookSection.append(hookNode(ctx, hook));
  if (p.persona) {
    const persona = ctx.section('Persona');
    ctx.facts(Object.entries(p.persona).map(([k, v]) => [titleCase(k), Array.isArray(v) ? v.join('; ') : v]), persona);
  }
  if (world.has('story_web')) {
    await ctx.later('Story web', async section => {
      const webs = await world.select('story_web', { path: 'webs', where: [`uid:${p.uid}`], fields: 'offered,spokes,rest,acts' });
      if (!ctx.current()) return;
      const web = webs?.[0];
      if (!web) {
        section.append(el('p', { class: 'note' }, 'No web for this person.'));
        return;
      }
      ctx.facts([
        ['Offered', web.offered],
        ['Spokes', (web.spokes ?? []).map(s => `${s.name} (${num(s.weight)})`).join(', ')],
        ['Resting hook', web.rest?.hook?.prompt ?? web.rest?.prompt],
        ['Acts', (web.acts ?? []).map(a => a.title).join(' → ')],
      ], section);
    });
  }
  ctx.rawSections();
}

// A dread: a monster the cast names for the cities it razed. Living while its lair stands, and
// hunted by the heirs of those cities, the reeves in its reach and the orders that failed
// against it.
async function renderDread(ctx) {
  const { world, record: d } = ctx;
  ctx.header(`Dread · ${d.species ?? 'beast'}`, d.display_name ?? d.name ?? d.uid,
    [d.archetype_name, d.alignment?.label, d.tier ? titleCase(d.tier) : null, d.status].filter(Boolean).join(' · '));
  if (d.situation) ctx.body.append(el('p', {}, d.situation));
  const heroes = await lookups.heroes(world);
  const lair = d.presence ? await resolveSite(world, d.presence.site_kind, d.presence.uid) : null;
  const razed = await Promise.all((d.destroyed ?? []).map(async kill => [kill, await resolveSite(world, 'ruin', kill.uid)]));
  if (!ctx.current()) return;
  ctx.buttons(lair?.record?.x !== undefined ? ctx.focusButton(lair.record.x, lair.record.z) : null);
  ctx.facts([
    ['Lair', lair ? ctx.link(`${lair.record.name} (tier ${d.nest_tier})`, lair) : `gone; ${d.nest_id}`],
    ['Family', d.family || 'unknown'],
    ['Fame', d.fame],
    ['uid', d.uid],
  ]);
  const razedSection = ctx.section(`Razed (${razed.length})`);
  ctx.links(razedSection, razed.map(([kill, ruin]) => (ruin
    ? ctx.link(`${kill.name}, age ${kill.age}`, ruin)
    : `${kill.name}, age ${kill.age}`)), 'No city recorded.');
  const hunts = huntsOf(heroes, d.status === 'living' ? d.nest_id : null);
  const huntSection = ctx.section(`Hunted by (${hunts.length})`);
  if (!hunts.length) {
    huntSection.append(el('p', { class: 'note' }, d.status === 'living' ? 'Nobody has asked yet.' : 'Its lair is gone: there is nothing left to hunt.'));
  }
  appendHunts(ctx, heroes, huntSection, hunts);
  // A dread record carries no bond list of its own: its rivals are the bonds naming it.
  const rivals = (heroes.block?.bonds ?? [])
    .filter(b => b.kind === 'rival' && (b.a === d.uid || b.b === d.uid))
    .map(b => heroes.byUid.get(b.a === d.uid ? b.b : b.a)).filter(Boolean);
  if (rivals.length) {
    const section = ctx.section('Rivals');
    ctx.links(section, rivals.map(q => ctx.link(heroLabel(q), { kind: 'hero', record: q })), '');
  }
  if (d.persona) {
    const persona = ctx.section('Persona');
    ctx.facts(Object.entries(d.persona).map(([k, v]) => [titleCase(k), Array.isArray(v) ? v.join('; ') : v]), persona);
  }
  ctx.rawSections();
}

function renderHook(ctx) {
  const { world, record: hook } = ctx;
  ctx.header(`Quest hook · ${hook.verb}`, hook.stated_purpose, `difficulty ${hook.difficulty}${hook.unwitting ? ' · the giver is unwitting' : ''}`);
  return (async () => {
    const heroes = await lookups.heroes(world);
    let giver = heroes.byUid.get(hook.giver_uid);
    let giverItem = giver ? { kind: 'hero', record: giver } : null;
    if (!giverItem && world.has('nomads')) {
      const nomads = await world.block('nomads');
      const band = (nomads?.groups ?? []).find(g => g.uid === hook.giver_uid);
      if (band) giverItem = { kind: 'band', record: band };
    }
    if (!ctx.current()) return;
    ctx.facts([
      ['Giver', giverItem ? ctx.link(giverItem.kind === 'hero' ? heroLabel(giverItem.record) : `band ${hook.giver_uid}`, giverItem) : hook.giver_uid],
      ['Offered from face', hook.offered_from_face],
      ['Target', hook.target],
      ['Target node', hook.target_node],
      ['Actual effect', hook.actual_effect],
      ['Unsited because', hook.unsited_reason],
      ['id', hook.hook_id],
    ]);
    if (hook.target_node !== undefined && hook.target_node !== null) {
      const [x, z] = world.nodeXZ(hook.target_node);
      ctx.buttons(ctx.focusButton(x, z));
    }
    ctx.rawSections();
  })();
}

async function renderKeyLocation(ctx) {
  const { world, record: k } = ctx;
  const keylocs = await lookups.keylocs(world);
  const family = keylocs.block?.families?.[k.family];
  ctx.header(`Key location · ${family?.name ?? k.family} · tier ${k.tier}`, k.name, `${titleCase(k.kind)}; ${k.state}, ${k.occupant === 'none' ? 'unoccupied' : `held by ${k.occupant}`}; threat ${k.threat}`);
  ctx.buttons(
    world.summary.plans?.key_location > 0 ? ctx.button('Open location plan', () => ctx.actions.openPlan('key_location', k.id, `${k.name} — ${titleCase(k.kind)}`)) : null,
    ctx.focusButton(k.x, k.z),
    ctx.patchButton(k.x, k.z, k.name),
  );
  if (k.reason) ctx.body.append(el('p', {}, k.reason));
  const chain = k.links?.chain ? keylocs.chainById.get(k.links.chain) : null;
  const cluster = k.links?.cluster ? keylocs.clusterById.get(k.links.cluster) : null;
  ctx.facts([
    ['Placement', k.placement],
    ['Domain', k.domain],
    ['Layer', k.layer],
    ['Succession', k.succession ? `${k.succession.from_occupant} → ${k.succession.to_occupant}: ${k.succession.note}` : undefined],
    ['Interior', k.interior ? `${k.interior.levels} levels, ${k.interior.chambers?.length ?? 0} chambers (${k.interior.plan})` : undefined],
    ['Chain', chain ? `${chain.name}, ${chain.members.length} along the ${chain.along}` : k.links?.chain],
    ['Cluster', cluster ? `${cluster.name} around ${cluster.anchor_name ?? cluster.anchor}` : k.links?.cluster],
    ['Environment', k.environment ? `${k.environment.variant_name ?? k.environment.natural_biome_name}; ${num(k.environment.temperature_c)} °C` : undefined],
    ['Suitability', k.suitability],
    ['Height', k.height_m !== undefined ? `${num(k.height_m)} m` : undefined],
    ['Where', `${latLon(world.lat(k.z), world.lon(k.x))} · cell ${k.x}, ${k.z} · node ${k.node}`],
    ['id', k.id],
  ]);
  if (k.origin?.ruin_id) {
    const ruin = (await lookups.ruins(world)).byId.get(k.origin.ruin_id);
    if (ruin && ctx.current()) ctx.facts([['Grew from', ctx.link(`ruins of ${ruin.name}`, { kind: 'ruin', record: ruin })]]);
  }
  ctx.rawSections();
}

async function renderNest(ctx) {
  const { world, item } = ctx;
  const block = item.block ?? (item.record.class === 'animal' ? 'wildlife' : 'beast_nests');
  ctx.header(`Nest · ${item.record.class ?? ''} · tier ${item.record.tier ?? '?'}`, item.record.name, `${titleCase(item.record.kind)}; ${item.record.size ?? ''} ${item.record.family ?? ''}`);
  ctx.buttons(ctx.focusButton(item.record.x, item.record.z));
  const [full, profiles] = await Promise.all([
    world.select(block, { path: 'sites', where: [`id:${item.record.id}`] }),
    world.select(block, { path: 'profiles', where: [`id:${item.record.species_id}`] }),
  ]);
  if (!ctx.current()) return;
  const nest = full?.[0] ?? item.record;
  const profile = profiles?.[0];
  // A lair a dread holds: name it, and who is sent against it.
  if (block === 'beast_nests' && world.has('heroes')) {
    const heroes = await lookups.heroes(world);
    if (!ctx.current()) return;
    const dread = (heroes.block?.dreads ?? []).find(x => x.nest_id === nest.id);
    if (dread) {
      ctx.body.append(el('p', {}, 'The lair of ', ctx.link(dread.display_name, { kind: 'hero', record: dread }),
        `, who razed ${(dread.destroyed ?? []).map(k => k.name).join(', ') || 'a city'}.`));
      const hunts = huntsOf(heroes, nest.id);
      if (hunts.length) {
        const section = ctx.section(`Hunted by (${hunts.length})`);
        appendHunts(ctx, heroes, section, hunts);
      }
    }
  }
  ctx.facts([
    ['Role', nest.role],
    ['Suitability', nest.suitability],
    ['Range', nest.range_m !== undefined ? `${num(nest.range_m)} m` : undefined],
    ['Den', nest.den],
    ['Real animal', nest.real],
    ['Layer', nest.layer],
    ['Biome', nest.biome],
    ['Where', `${latLon(world.lat(nest.z), world.lon(nest.x))} · cell ${nest.x}, ${nest.z} · node ${nest.node}`],
    ['id', nest.id],
  ]);
  if (profile) {
    const section = ctx.section('Species');
    ctx.facts([
      ['Medium', profile.medium],
      ['Temperature', profile.temperature ? `${profile.temperature.join(' to ')} °C` : undefined],
      ['Requires', profile.requires ? Object.entries(profile.requires).map(([k, v]) => `${k} ≥ ${v}`).join(', ') || 'nothing' : undefined],
      ['Movement', profile.movement],
      ['Spacing', profile.spacing_m !== undefined ? `${num(profile.spacing_m)} m` : undefined],
      ['Likeness', profile.likeness],
    ], section);
  }
  ctx.rawSections(nest);
}

function renderLeyNode(ctx) {
  const { world, record: node } = ctx;
  const [X, Y] = World.directionXY(node.direction);
  ctx.header(`Ley node · ${titleCase(node.school)}`, node.id, `intensity ${num(node.intensity)}`);
  ctx.buttons(ctx.button('Show on map', () => ctx.actions.focus(X, Y)));
  ctx.facts([
    ['Network', node.network],
    ['Where', latLon(90 - Y, X - 180)],
  ]);
  ctx.rawSections();
}

function renderCamp(ctx) {
  const { record: c } = ctx;
  ctx.header('Camp', c.name, `${titleCase(c.origin)} since age ${c.since_age}`);
  ctx.buttons(ctx.focusButton(c.x, c.z));
  ctx.facts([['Civilization', c.civilization_id], ['Ruin', c.ruin_uid], ['uid', c.uid]]);
  ctx.rawSections();
}

function renderShrine(ctx) {
  const { world, record: s } = ctx;
  ctx.header(`Shrine · ${s.kind}`, s.id, s.god_id);
  if (s.node !== undefined) ctx.buttons(ctx.focusButton(...world.nodeXZ(s.node)));
  ctx.facts([['God', s.god_id], ['Ruin', s.ruin_id], ['Born under a surge', s.born_under_surge]]);
  ctx.rawSections();
}

function renderCell(ctx) {
  const { world, item } = ctx;
  const { x, z } = item;
  ctx.header('Cell', latLon(world.lat(z), world.lon(x)), `column ${x}, row ${z}`);
  ctx.buttons(ctx.patchButton(x, z, latLon(world.lat(z), world.lon(x))));
  const node = z === 0 ? 0 : z === world.n - 1 ? (world.n - 2) * (world.n - 1) + 1 : 1 + (z - 1) * (world.n - 1) + x;
  ctx.facts([['Node', node], ...ctx.actions.describeCell(x, z)]);
  const things = ctx.actions.cellItems(x, z);
  const section = ctx.section(`On this cell (${things.length})`);
  ctx.links(section, things.slice(0, 60).map(t => ctx.link(`${siteLabel(t)} · ${t.kind.replace('_', ' ')}`, t)), 'Nothing from the shown overlays stands on this cell.');
}

function renderGeneric(ctx) {
  const r = ctx.record ?? {};
  ctx.header(titleCase(ctx.item.kind), r.name ?? r.id ?? r.uid ?? '');
  ctx.rawSections();
}

const RENDERERS = {
  city: renderCity,
  hamlet: renderHamlet,
  fortress: renderFortress,
  ruin: renderRuin,
  band: renderBand,
  hero: renderHero,
  hook: renderHook,
  key_location: renderKeyLocation,
  nest: renderNest,
  ley_node: renderLeyNode,
  camp: renderCamp,
  shrine: renderShrine,
  cell: renderCell,
};

/** Names to search in the "Find" box: [{label, item}] built from the loaded world. */
export async function searchIndex(world) {
  const out = [];
  const [settlements, humans, ruins, heroes, keylocs, nomads] = await Promise.all([
    world.block('settlements'), world.block('humans'), world.block('ruins'), world.block('heroes'), world.block('key_locations'), world.block('nomads'),
  ]);
  for (const s of settlements?.sites ?? []) out.push({ label: `${s.name} — city`, item: { kind: 'city', record: s } });
  for (const r of ruins ?? []) out.push({ label: `${r.name} — ruins`, item: { kind: 'ruin', record: r } });
  for (const h of humans?.hamlets ?? []) out.push({ label: `${h.id} — ${h.role} hamlet`, item: { kind: 'hamlet', record: h } });
  for (const f of humans?.fortresses ?? []) out.push({ label: `${f.id} — fortress`, item: { kind: 'fortress', record: f } });
  for (const p of [...(heroes?.people ?? []), ...(heroes?.dreads ?? [])]) out.push({ label: `${p.display_name ?? p.uid} — ${p.role ?? 'person'}`, item: { kind: 'hero', record: p } });
  for (const k of keylocs?.sites ?? []) out.push({ label: `${k.name} — ${k.kind.replace(/_/g, ' ')}`, item: { kind: 'key_location', record: k } });
  for (const b of nomads?.groups ?? []) out.push({ label: `${b.uid} — ${b.classification} band`, item: { kind: 'band', record: b } });
  return out;
}
