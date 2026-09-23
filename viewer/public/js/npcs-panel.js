// NPCs: the roster, as the Python lab showed it -- its summary, what it reported, the posts
// by site, and the important people of each site with the quest verbs their posts offer --
// and then every individual NPC, filtered, sorted and paged. Since 2026-09-23 each person
// also carries a gender and an age in their people's young, middle or old band, the building
// they work in, and a storekeeper the store they keep; a horse keeper sells mounts and runs
// carriages from the city's stable.

import { el, num, titleCase } from './dom.js';
import { bars, card, cell, folded, sortableTable } from './panels.js';

const PAGE = 100;

/** Inspector records for the sites NPCs live at, by uid. */
function siteRecords(settlements, humans) {
  const out = new Map();
  for (const s of settlements?.sites ?? []) out.set(s.uid, { kind: 'city', record: s });
  for (const h of humans?.hamlets ?? []) if (h.uid) out.set(h.uid, { kind: 'hamlet', record: h });
  for (const f of humans?.fortresses ?? []) if (f.uid) out.set(f.uid, { kind: 'fortress', record: f });
  return out;
}

export async function renderNpcs(container, world, actions, isCurrent = () => true) {
  container.replaceChildren(el('p', { class: 'note' }, 'Loading the NPC roster…'));
  const [npcs, settlements, humans, heroBlock] = await Promise.all([
    world.block('npcs'), world.block('settlements'), world.block('humans'), world.block('heroes'),
  ]);
  const heroes = [...(heroBlock?.people ?? []), ...(heroBlock?.dreads ?? [])];
  if (!isCurrent()) return;
  const root = el('div');
  root.append(el('h2', {}, 'NPCs'));
  if (!npcs) {
    root.append(el('p', { class: 'note' }, 'No NPC roster in this world (it is written at stage 16).'));
    container.replaceChildren(root);
    return;
  }
  if (npcs.status !== 'ok') {
    root.append(el('p', { class: 'bad' }, `NPC roster failed: ${npcs.error ?? npcs.status}`));
    container.replaceChildren(root);
    return;
  }

  const s = npcs.summary ?? {};
  const people = npcs.people ?? [];
  const sites = npcs.sites ?? [];
  const records = siteRecords(settlements, humans);
  const heroByUid = new Map((heroes ?? []).map(h => [h.uid, h]));
  const siteByUid = new Map(sites.map(site => [site.uid, site]));
  const verbsOf = post => npcs.post_verbs?.[post] ?? [];
  const servicesOf = post => npcs.post_services?.[post] ?? [];
  const postLabel = p => (p.store ? `${titleCase(p.post)} · ${titleCase(p.store).toLowerCase()}` : titleCase(p.post));
  const BANDS = ['young', 'middle', 'old'];

  // The Python lab's summary line, and what the roster reported about itself.
  root.append(el('p', {},
    `${num(s.total)} people across ${num(s.sites)} sites · ${num(s.alive)} alive · ${num(s.dead)} dead · `
    + `${num(s.important)} earmarked important (${num(100 * (s.important_fraction ?? 0), 1)}%) · ${num(s.heroes_linked)} linked to the cast · `
    + `policy revisions ${Object.values(npcs.policy_revision ?? {}).join('/')}`));
  const reported = ['unresolved_parent_race', 'unanchored_sites', 'roster_mismatches', 'duplicate_site_keys', 'important_capped_out',
    'cast_outside_a_planned_site', 'cast_presence_unresolved', 'appearance_unresolved'].filter(k => s[k]);
  if (reported.length) {
    root.append(el('p', { class: 'warn' }, `Reported: ${reported.map(k => `${titleCase(k).toLowerCase()} ${num(s[k])}`).join(' · ')}`));
  }

  const siteLink = (uid, label) => {
    const item = records.get(uid);
    return item ? cell(el('button', { type: 'button', class: 'link', onclick: () => actions.inspect(item) }, label), label) : label;
  };
  const heroLink = uid => {
    const hero = heroByUid.get(uid);
    if (!hero) return uid ?? null;
    const label = hero.display_name ?? hero.name ?? uid;
    return cell(el('button', { type: 'button', class: 'link', onclick: () => actions.inspect({ kind: 'hero', record: hero }) }, label), label);
  };

  // --- aggregate stats -------------------------------------------------------------------
  const byPost = new Map();
  const bySite = new Map();
  for (const p of people) {
    byPost.set(p.post, (byPost.get(p.post) ?? 0) + 1);
    const key = p.site_uid ?? null;
    if (!bySite.has(key)) bySite.set(key, []);
    bySite.get(key).push(p);
  }
  const grid = el('div', { class: 'debug-grid' });
  const posts = [...byPost.entries()].sort((a, b) => b[1] - a[1]);
  grid.append(card(`People by post (${posts.length} posts)`,
    el('div', { class: 'scroll' }, bars(posts.map(([post, n]) => [titleCase(post), n])))));
  const kinds = new Map();
  for (const p of people) kinds.set(p.site_kind ?? 'no site', (kinds.get(p.site_kind ?? 'no site') ?? 0) + 1);
  grid.append(card('People by kind of site', bars([...kinds.entries()].sort((a, b) => b[1] - a[1]).map(([k, n]) => [titleCase(k), n]), { alt: true }),
    el('p', { class: 'note' }, 'Verbs a post offers a hero, from the roster’s own table: ',
      Object.keys(npcs.post_verbs ?? {}).length ? `${Object.keys(npcs.post_verbs).length} posts offer at least one.` : 'none.')));

  // Gender and age: every person drew both from their own stream, the age inside their
  // people's band.
  const aged = people.filter(p => p.appearance);
  if (aged.length) {
    const tallyOf = read => {
      const out = new Map();
      for (const p of aged) out.set(read(p), (out.get(read(p)) ?? 0) + 1);
      return out;
    };
    const bands = tallyOf(p => p.appearance.age_band);
    const genders = tallyOf(p => p.appearance.gender);
    grid.append(card('People by age', bars(BANDS.map(b => [titleCase(b), bands.get(b) ?? 0])),
      bars([...genders.entries()].sort().map(([g, n]) => [titleCase(g), n]), { alt: true }),
      el('p', { class: 'note' }, `${num(aged.length)} people aged; each band is their own people's (the Races tab lists them).`)));
  }

  // Storekeepers by the store they keep, and each city's horse keeper.
  const stores = new Map();
  for (const p of people) if (p.store) stores.set(p.store, (stores.get(p.store) ?? 0) + 1);
  const citySites = sites.filter(site => site.kind === 'city');
  const keeping = new Set(people.filter(p => p.post === 'horse_keeper').map(p => p.site_uid));
  const without = citySites.filter(site => !keeping.has(site.uid));
  grid.append(card('Stores and stables',
    stores.size ? bars([...stores.entries()].sort((a, b) => b[1] - a[1]).map(([store, n]) => [`${titleCase(store)} keepers`, n]))
      : el('p', { class: 'note' }, 'No storekeeper in this world.'),
    el('p', {}, `Horse keepers (${servicesOf('horse_keeper').join(', ') || 'no services listed'}): `
      + `${num(citySites.length - without.length)} of ${num(citySites.length)} cities.`),
    without.length
      ? el('p', { class: 'warn' }, 'No stable in: ', ...without.flatMap((site, i) => [i ? ', ' : '',
        siteLink(site.uid, site.name).node ?? site.name, site.plan_status === 'unbuildable' ? ' (not planned)' : '']),
        '. Every city queues one; a planned city without one ran out of room, and its plan lists the stable unplaced.')
      : el('p', { class: 'note' }, 'Every city placed its stable.')));
  root.append(grid);

  // --- the sites -------------------------------------------------------------------------
  root.append(card(`Posts by site (${sites.length})`, sortableTable(
    ['Site', 'Kind', 'Class', 'Civilization', 'Parent race', 'Posts', 'People', 'Alive', 'Dead', 'Important', 'Cast', 'Plan'],
    sites.map(site => {
      const here = bySite.get(site.uid) ?? [];
      return [siteLink(site.uid, site.name), titleCase(site.kind), site.city_class, site.civilization_id ? titleCase(site.civilization_id) : 'unknown people',
        site.parent_race_id, site.posts, here.length, here.filter(p => p.status !== 'dead').length, here.filter(p => p.status === 'dead').length,
        here.filter(p => p.important).length, here.filter(p => p.hero_uid).length, site.plan_status];
    }), { numeric: [5, 6, 7, 8, 9, 10], sorted: 6, descending: true })));

  // --- every NPC -------------------------------------------------------------------------
  const columns = [
    ['Name', p => p.name],
    ['Post', p => postLabel(p)],
    ['Gender', p => (p.appearance ? titleCase(p.appearance.gender) : null)],
    ['Age', p => (p.appearance ? cell(`${p.appearance.age_years} · ${p.appearance.age_band}`, p.appearance.age_years) : null)],
    ['Site', p => (p.site_uid ? siteLink(p.site_uid, siteByUid.get(p.site_uid)?.name ?? p.site_uid) : null)],
    ['Kind', p => (p.site_kind ? titleCase(p.site_kind) : null)],
    ['Civilization', p => { const c = p.appearance?.civilization_id ?? siteByUid.get(p.site_uid)?.civilization_id; return c ? titleCase(c) : null; }],
    ['Status', p => p.status],
    ['Important', p => (p.important ? 'yes' : '')],
    ['Offers', p => verbsOf(p.post).join(', ') || null],
    ['Sells', p => servicesOf(p.post).join(', ') || null],
    ['Cast', p => (p.hero_uid ? heroLink(p.hero_uid) : null)],
    ['Workplace', p => p.workplace ?? p.building_id ?? null],
  ];
  const peopleOf = [...new Set(people.map(p => p.appearance?.civilization_id ?? siteByUid.get(p.site_uid)?.civilization_id).filter(Boolean))].sort();
  const filters = {
    text: el('input', { type: 'search', placeholder: 'Name, post or workplace', 'aria-label': 'Filter NPCs by name, post or workplace' }),
    site: el('select', { 'aria-label': 'Site' }, el('option', { value: '' }, 'Every site'),
      [...sites].sort((a, b) => a.name.localeCompare(b.name)).map(site => el('option', { value: site.uid }, site.name))),
    civ: el('select', { 'aria-label': 'Civilization' }, el('option', { value: '' }, 'Every civilization'),
      peopleOf.map(c => el('option', { value: c }, titleCase(c)))),
    post: el('select', { 'aria-label': 'Post' }, el('option', { value: '' }, 'Every post'),
      [...byPost.keys()].sort().map(post => el('option', { value: post }, titleCase(post)))),
    store: el('select', { 'aria-label': 'Store kept' }, el('option', { value: '' }, 'Any store or none'),
      [...stores.keys()].sort().map(store => el('option', { value: store }, `Keeps a ${titleCase(store).toLowerCase()}`))),
    gender: el('select', { 'aria-label': 'Gender' }, el('option', { value: '' }, 'Any gender'),
      [...new Set(aged.map(p => p.appearance.gender))].sort().map(g => el('option', { value: g }, titleCase(g)))),
    band: el('select', { 'aria-label': 'Age band' }, el('option', { value: '' }, 'Any age'),
      BANDS.map(b => el('option', { value: b }, titleCase(b)))),
    status: el('select', { 'aria-label': 'Status' }, el('option', { value: '' }, 'Alive or dead'),
      el('option', { value: 'alive' }, 'Alive'), el('option', { value: 'dead' }, 'Dead')),
    important: el('input', { type: 'checkbox' }),
    cast: el('input', { type: 'checkbox' }),
  };
  let sortColumn = 0;
  let descending = false;
  let page = 0;
  const count = el('span', { class: 'note' });
  const body = el('tbody');
  const heads = columns.map(([label], i) => el('th', {
    class: 'sortable', title: 'Sort by this column',
    onclick: () => { descending = sortColumn === i ? !descending : false; sortColumn = i; page = 0; draw(); },
  }, label));
  const prev = el('button', { type: 'button', onclick: () => { page -= 1; draw(); } }, '‹ Previous');
  const next = el('button', { type: 'button', onclick: () => { page += 1; draw(); } }, 'Next ›');
  const text = value => (value && typeof value === 'object' && 'node' in value ? value.key : value) ?? '';

  function matches(p) {
    const q = filters.text.value.trim().toLowerCase();
    if (q && !`${p.name} ${p.post} ${postLabel(p)} ${p.workplace ?? ''}`.toLowerCase().includes(q)) return false;
    if (filters.site.value && p.site_uid !== filters.site.value) return false;
    if (filters.civ.value && (p.appearance?.civilization_id ?? siteByUid.get(p.site_uid)?.civilization_id) !== filters.civ.value) return false;
    if (filters.post.value && p.post !== filters.post.value) return false;
    if (filters.store.value && p.store !== filters.store.value) return false;
    if (filters.gender.value && p.appearance?.gender !== filters.gender.value) return false;
    if (filters.band.value && p.appearance?.age_band !== filters.band.value) return false;
    if (filters.status.value && p.status !== filters.status.value) return false;
    if (filters.important.checked && !p.important) return false;
    if (filters.cast.checked && !p.hero_uid) return false;
    return true;
  }
  // A number sorts as a number (an age of 100 after one of 15); text as text; blanks last.
  const keyOf = value => {
    const key = text(value);
    return typeof key === 'number' ? key : String(key ?? '').toLowerCase();
  };
  function draw() {
    const [, read] = columns[sortColumn];
    const kept = people.filter(matches).map(p => [p, keyOf(read(p))]);
    kept.sort((a, b) => {
      const [x, y] = [a[1], b[1]];
      if (x === y) return 0;
      if (x === '') return 1;
      if (y === '') return -1;
      const order = typeof x === 'number' && typeof y === 'number' ? x - y : String(x).localeCompare(String(y));
      return (descending ? -1 : 1) * order;
    });
    const pages = Math.max(1, Math.ceil(kept.length / PAGE));
    page = Math.max(0, Math.min(page, pages - 1));
    const shown = kept.slice(page * PAGE, page * PAGE + PAGE);
    heads.forEach((th, i) => th.setAttribute('aria-sort', i === sortColumn ? (descending ? 'descending' : 'ascending') : 'none'));
    body.replaceChildren(...shown.map(([p]) => el('tr', { class: p.status === 'dead' ? 'dead' : null },
      columns.map(([, read]) => {
        const value = read(p);
        return el('td', {}, value && typeof value === 'object' && 'node' in value ? value.node : value ?? '—');
      }))));
    count.textContent = kept.length
      ? `${num(page * PAGE + 1)}–${num(page * PAGE + shown.length)} of ${num(kept.length)} NPCs (page ${page + 1} of ${pages})`
      : 'No NPC matches.';
    prev.disabled = page === 0;
    next.disabled = page >= pages - 1;
  }
  for (const control of [filters.site, filters.civ, filters.post, filters.store, filters.gender, filters.band, filters.status,
    filters.important, filters.cast]) {
    control.addEventListener('change', () => { page = 0; draw(); });
  }
  filters.text.addEventListener('input', () => { page = 0; draw(); });

  root.append(card(`Every NPC (${num(people.length)})`,
    el('div', { class: 'filters' }, filters.text, filters.site, filters.civ, filters.post, filters.store, filters.gender, filters.band,
      filters.status, el('label', { class: 'check' }, filters.important, 'Important only'), el('label', { class: 'check' }, filters.cast, 'In the cast')),
    el('div', { class: 'pager' }, prev, next, count),
    el('div', { class: 'scroll tall' }, el('table', { class: 'data' }, el('thead', {}, el('tr', {}, heads)), body))));
  draw();

  // --- the important people of each site, as the Python lab listed them ------------------
  const important = sites.map(site => [site, (bySite.get(site.uid) ?? []).filter(p => p.important)]).filter(([, list]) => list.length);
  root.append(folded(`Important people by site (${important.length} sites)`,
    el('div', { class: 'debug-grid' }, important.map(([site, list]) => card(site.name,
      el('p', { class: 'note' }, `${titleCase(site.kind)}${site.city_class ? ` · ${site.city_class}` : ''} · ${num(site.posts)} posts · ${site.civilization_id ? titleCase(site.civilization_id) : 'unknown people'}`),
      el('ul', { class: 'links' }, list.map(p => el('li', { class: p.status === 'dead' ? 'dead' : null },
        `${p.name} — ${postLabel(p).toLowerCase()}`, p.hero_uid ? ' (cast)' : '', p.status === 'dead' ? ' · dead' : '',
        p.appearance ? ` · ${p.appearance.gender}, ${p.appearance.age_years}` : '',
        verbsOf(p.post).length ? ` · offers ${verbsOf(p.post).join(', ')}` : '',
        servicesOf(p.post).length ? ` · sells ${servicesOf(p.post).join(', ')}` : ''))))))));
  const loose = (bySite.get(null) ?? []).filter(p => p.important);
  if (loose.length) {
    root.append(el('p', { class: 'note' }, `Cast standing outside a planned site: ${loose.map(p =>
      `${p.name} (${p.presence_kind ?? 'no presence'}${p.status === 'dead' ? ', dead' : ''})`).join(' · ')}`));
  }
  container.replaceChildren(root);
}
