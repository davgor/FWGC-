// NPCs: the roster, as the Python lab showed it -- its summary, what it reported, the posts
// by site, and the important people of each site with the quest verbs their posts offer --
// and then every individual NPC, filtered, sorted and paged.

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

  // The Python lab's summary line, and what the roster reported about itself.
  root.append(el('p', {},
    `${num(s.total)} people across ${num(s.sites)} sites · ${num(s.alive)} alive · ${num(s.dead)} dead · `
    + `${num(s.important)} earmarked important (${num(100 * (s.important_fraction ?? 0), 1)}%) · ${num(s.heroes_linked)} linked to the cast · `
    + `policy revisions ${Object.values(npcs.policy_revision ?? {}).join('/')}`));
  const reported = ['unresolved_parent_race', 'unanchored_sites', 'roster_mismatches', 'duplicate_site_keys', 'important_capped_out',
    'cast_outside_a_planned_site', 'cast_presence_unresolved'].filter(k => s[k]);
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
    ['Post', p => titleCase(p.post)],
    ['Site', p => (p.site_uid ? siteLink(p.site_uid, siteByUid.get(p.site_uid)?.name ?? p.site_uid) : null)],
    ['Kind', p => (p.site_kind ? titleCase(p.site_kind) : null)],
    ['Civilization', p => { const c = siteByUid.get(p.site_uid)?.civilization_id; return c ? titleCase(c) : null; }],
    ['Status', p => p.status],
    ['Important', p => (p.important ? 'yes' : '')],
    ['Offers', p => verbsOf(p.post).join(', ') || null],
    ['Cast', p => (p.hero_uid ? heroLink(p.hero_uid) : null)],
    ['Building', p => p.building_id ?? null],
  ];
  const filters = {
    text: el('input', { type: 'search', placeholder: 'Name or post', 'aria-label': 'Filter NPCs by name or post' }),
    site: el('select', { 'aria-label': 'Site' }, el('option', { value: '' }, 'Every site'),
      [...sites].sort((a, b) => a.name.localeCompare(b.name)).map(site => el('option', { value: site.uid }, site.name))),
    civ: el('select', { 'aria-label': 'Civilization' }, el('option', { value: '' }, 'Every civilization'),
      [...new Set(sites.map(site => site.civilization_id).filter(Boolean))].sort().map(c => el('option', { value: c }, titleCase(c)))),
    post: el('select', { 'aria-label': 'Post' }, el('option', { value: '' }, 'Every post'),
      [...byPost.keys()].sort().map(post => el('option', { value: post }, titleCase(post)))),
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
    if (q && !`${p.name} ${p.post}`.toLowerCase().includes(q) && !titleCase(p.post).toLowerCase().includes(q)) return false;
    if (filters.site.value && p.site_uid !== filters.site.value) return false;
    if (filters.civ.value && siteByUid.get(p.site_uid)?.civilization_id !== filters.civ.value) return false;
    if (filters.post.value && p.post !== filters.post.value) return false;
    if (filters.status.value && p.status !== filters.status.value) return false;
    if (filters.important.checked && !p.important) return false;
    if (filters.cast.checked && !p.hero_uid) return false;
    return true;
  }
  function draw() {
    const [, read] = columns[sortColumn];
    const kept = people.filter(matches).map(p => [p, String(text(read(p))).toLowerCase()]);
    kept.sort((a, b) => (a[1] === b[1] ? 0 : a[1] === '' ? 1 : b[1] === '' ? -1 : (descending ? -1 : 1) * a[1].localeCompare(b[1])));
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
  for (const control of [filters.site, filters.civ, filters.post, filters.status, filters.important, filters.cast]) {
    control.addEventListener('change', () => { page = 0; draw(); });
  }
  filters.text.addEventListener('input', () => { page = 0; draw(); });

  root.append(card(`Every NPC (${num(people.length)})`,
    el('div', { class: 'filters' }, filters.text, filters.site, filters.civ, filters.post, filters.status,
      el('label', { class: 'check' }, filters.important, 'Important only'), el('label', { class: 'check' }, filters.cast, 'In the cast')),
    el('div', { class: 'pager' }, prev, next, count),
    el('div', { class: 'scroll tall' }, el('table', { class: 'data' }, el('thead', {}, el('tr', {}, heads)), body))));
  draw();

  // --- the important people of each site, as the Python lab listed them ------------------
  const important = sites.map(site => [site, (bySite.get(site.uid) ?? []).filter(p => p.important)]).filter(([, list]) => list.length);
  root.append(folded(`Important people by site (${important.length} sites)`,
    el('div', { class: 'debug-grid' }, important.map(([site, list]) => card(site.name,
      el('p', { class: 'note' }, `${titleCase(site.kind)}${site.city_class ? ` · ${site.city_class}` : ''} · ${num(site.posts)} posts · ${site.civilization_id ? titleCase(site.civilization_id) : 'unknown people'}`),
      el('ul', { class: 'links' }, list.map(p => el('li', { class: p.status === 'dead' ? 'dead' : null },
        `${p.name} — ${titleCase(p.post).toLowerCase()}`, p.hero_uid ? ' (cast)' : '', p.status === 'dead' ? ' · dead' : '',
        verbsOf(p.post).length ? ` · offers ${verbsOf(p.post).join(', ')}` : ''))))))));
  const loose = (bySite.get(null) ?? []).filter(p => p.important);
  if (loose.length) {
    root.append(el('p', { class: 'note' }, `Cast standing outside a planned site: ${loose.map(p =>
      `${p.name} (${p.presence_kind ?? 'no presence'}${p.status === 'dead' ? ', dead' : ''})`).join(' · ')}`));
  }
  container.replaceChildren(root);
}
