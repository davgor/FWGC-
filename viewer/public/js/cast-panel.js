// The cast: the world's heroes, legends and dreads, as the Python lab's cards showed them --
// who each is, where, doing what, their log and bonds, the persona a player meets and, on
// request, the truth behind it -- after one sortable table of every member at a glance.

import { el, num, titleCase } from './dom.js';
import { card, cell, sortableTable } from './panels.js';

export async function renderCast(container, world, actions, isCurrent = () => true) {
  container.replaceChildren(el('p', { class: 'note' }, 'Loading the cast…'));
  const [heroes, civilizations, settlements, ruins, humans] = await Promise.all([
    world.block('heroes'), world.block('civilizations'), world.block('settlements'), world.block('ruins'), world.block('humans'),
  ]);
  if (!isCurrent()) return;
  const root = el('div');
  root.append(el('h2', {}, 'The cast'));
  if (!heroes) {
    root.append(el('p', { class: 'note' }, 'No cast in this world (heroes are written at stage 16).'));
    container.replaceChildren(root);
    return;
  }
  if (heroes.status !== 'ok') {
    root.append(el('p', { class: 'bad' }, `Hero generator failed: ${heroes.error ?? heroes.status}`));
    container.replaceChildren(root);
    return;
  }

  const people = heroes.people ?? [];
  const dreads = heroes.dreads ?? [];
  const everyone = [...people, ...dreads];
  const byUid = new Map(everyone.map(p => [p.uid, p]));
  const realms = new Map((heroes.realms ?? []).map(r => [r.uid, r]));
  const civName = id => civilizations?.entities?.find(e => e.id === id)?.name ?? titleCase(id ?? '');
  const places = new Map();
  for (const s of settlements?.sites ?? []) places.set(s.uid, s.name);
  for (const r of ruins ?? []) places.set(r.uid, `ruins of ${r.name}`);
  for (const h of humans?.hamlets ?? []) if (h.uid) places.set(h.uid, h.name || h.id);
  for (const f of humans?.fortresses ?? []) if (f.uid) places.set(f.uid, f.name || f.id);
  const nameOf = uid => byUid.get(uid)?.display_name ?? uid;
  const hooksOf = uid => (heroes.quest_hooks ?? []).filter(q => q.giver_uid === uid);
  const inspect = p => actions.inspect({ kind: 'hero', record: p });
  const link = p => cell(el('button', { type: 'button', class: 'link', onclick: () => inspect(p) }, p.display_name ?? p.uid), p.display_name ?? p.uid);

  const s = heroes.summary ?? {};
  root.append(el('p', {},
    `${num(s.precipitated)} of ${num(s.candidates)} candidates precipitated · ${num(s.living)} living · ${num(s.legends)} legends · `
    + `${num(s.dreads)} dreads · ${num(s.orgs ?? 0)} orgs · ${num(s.camps ?? 0)} camps · ${num(s.hooks ?? 0)} hooks · ${num(s.realms)} realms · `
    + `policy revisions ${Object.values(heroes.policy_revision ?? {}).join('/')}`));

  // Every member at a glance.
  root.append(card(`Everyone (${everyone.length})`, sortableTable(
    ['Name', 'Status', 'Role', 'Archetype', 'Alignment', 'Law', 'Good', 'Tier', 'Fame', 'Civilization', 'Race', 'School', 'Seat', 'Situation', 'Hooks'],
    everyone.map(p => [
      link(p), p.role === 'dread' ? 'dread' : p.status, titleCase(p.role), p.archetype_name ?? null, p.alignment?.label ?? null,
      p.alignment?.law ?? null, p.alignment?.good ?? null, titleCase(p.tier), p.fame ?? null,
      p.role === 'dread' ? titleCase(p.family ?? 'beast') : civName(p.civilization_id), p.race_id ? titleCase(p.race_id) : null,
      p.school ? titleCase(p.school) : null, p.seat ? titleCase(p.seat) : null, p.situation ?? null, hooksOf(p.uid).length,
    ]), { numeric: [5, 6, 8, 14], sorted: 8, descending: true, tall: true })));

  // The cards, as the lab drew them.
  const hideLegends = el('input', { type: 'checkbox' });
  const hideDreads = el('input', { type: 'checkbox' });
  const showTruth = el('input', { type: 'checkbox' });
  const cards = el('div', { class: 'cast-cards' });
  root.append(el('div', { class: 'filters' },
    el('label', { class: 'check' }, hideLegends, 'Hide legends'), el('label', { class: 'check' }, hideDreads, 'Hide dreads'),
    el('label', { class: 'check' }, showTruth, 'Show the truth behind each face')), cards);

  function heroCard(p) {
    const body = el('article', { class: `cast-card${p.status === 'legend' ? ' legend' : ''}` });
    body.append(el('strong', {}, el('button', { type: 'button', class: 'link', onclick: () => inspect(p) }, p.display_name ?? p.uid)));
    const faces = p.faces ?? [];
    if (faces.length > 1) {
      body.append(el('p', { class: 'mask' }, `Appears as: ${faces[0].label} face, a ${titleCase(faces[0].apparent_role).toLowerCase()} (${faces[0].alignment?.label ?? '—'})`));
    }
    const who = p.role === 'dread'
      ? `${titleCase(p.family ?? 'beast')} · nest tier ${p.nest_tier ?? '—'}`
      : `${civName(p.civilization_id)} (${titleCase(p.race_id ?? '')})${p.school ? ` · ${titleCase(p.school)}` : ''}${p.seat ? ` · ${titleCase(p.seat)}` : ''}`;
    body.append(el('p', {}, `${who} · ${titleCase(p.role)} · ${p.archetype_name ?? 'No archetype'} · ${p.alignment?.label ?? '—'} · ${titleCase(p.tier)}, fame ${num(p.fame)}`));
    const realm = realms.get(p.realm_uid);
    body.append(el('p', {}, p.presence
      ? `Position: ${titleCase(p.presence.situation)} at ${places.get(p.presence.uid) ?? p.site_name ?? p.presence.uid}${realm ? ` · ${realm.name}` : ''}`
      : `Legend of Age ${p.born_age ?? p.deeds?.[0]?.age ?? '—'} · no presence in the world`));
    if (p.situation) body.append(el('p', {}, `Now: ${p.situation}`));

    const log = el('details', {}, el('summary', {}, 'Event log and bonds'));
    for (const e of p.log ?? []) log.append(el('p', {}, `Age ${e.age} · ${e.event_id}: ${e.text}`));
    if (p.claim) log.append(el('p', {}, `Claim: ${p.claim.verb} ${p.claim.target_name}`));
    for (const [label, list] of [['Rivals', p.rivals], ['Kin', p.kin], ['Allies', p.allies]]) {
      if (list?.length) log.append(el('p', {}, `${label}: ${list.map(nameOf).join(', ')}`));
    }
    if (p.predecessor_uid) log.append(el('p', {}, `Inherits from ${nameOf(p.predecessor_uid)}`));
    for (const q of hooksOf(p.uid)) log.append(el('p', {}, `Asks: “${q.stated_purpose}”`));
    if (p.companion?.eligible) log.append(el('p', {}, `Would join the player once ${p.companion.join_condition}.`));
    body.append(log);

    if (p.persona) {
      const persona = el('details', {}, el('summary', {}, 'Persona'));
      for (const [k, v] of [['Belief', p.persona.belief], ['Wants', p.persona.wants], ['Fears', p.persona.fears], ['Manner', p.persona.manner],
        ['Voice', p.persona.voice], ['Tells', p.persona.tells], ['Toward a stranger', p.persona.toward_player], ['Lies', p.persona.lies],
        ['Cornered', p.persona.cornered], ['Never', (p.persona.never ?? []).join('; ')]]) {
        if (v) persona.append(el('p', {}, `${k}: ${v}`));
      }
      if (p.persona.line) persona.append(el('p', { class: 'line' }, `“${p.persona.line}”`));
      body.append(persona);
    }

    if (showTruth.checked) {
      const truth = el('details', { open: true }, el('summary', {}, 'Truth'));
      truth.append(el('p', {}, `Faces: ${faces.map(f => `${f.label} (${f.alignment?.label ?? '—'}, ${f.claim?.verb ?? '—'} ${f.claim?.target_name ?? ''})`).join(', ') || 'one face'}`));
      const a = p.alignment ?? {};
      truth.append(el('p', {}, `Alignment ${num(a.law, 2)} law / ${num(a.good, 2)} good${a.drift ? ` · drift ${a.drift.law}/${a.drift.good} per age` : ''} · selection features: ${(p.selectable ?? []).join(', ') || 'none'}`));
      for (const q of hooksOf(p.uid)) {
        const e = q.actual_effect ?? {};
        const change = e.action ?? (e.intensity_delta !== undefined ? `${e.intensity_delta > 0 ? '+' : ''}${e.intensity_delta}` : '');
        truth.append(el('p', {}, `${q.unwitting ? '⚠ unwitting' : 'honest'} · “${q.stated_purpose}” → ${e.kind ?? '—'} ${e.node_id ?? e.uid ?? ''} ${change} ${e.school ?? ''}`));
      }
      body.append(truth);
    }
    return body;
  }

  function draw() {
    const sections = [['Living cast', people.filter(p => p.status === 'living')], ['Legends', people.filter(p => p.status === 'legend')], ['Dreads', dreads]];
    cards.replaceChildren();
    for (const [label, list] of sections) {
      if (!list.length || (label === 'Legends' && hideLegends.checked) || (label === 'Dreads' && hideDreads.checked)) continue;
      cards.append(el('h3', { class: 'wide' }, `${label} (${list.length})`), ...list.map(heroCard));
    }
  }
  for (const box of [hideLegends, hideDreads, showTruth]) box.addEventListener('change', draw);
  draw();
  container.replaceChildren(root);
}
