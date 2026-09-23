// Building blocks shared by the report panels (World debug, Races, NPCs, Cast): cards,
// bar charts and tables, one of which sorts by a clicked column.

import { el, num } from './dom.js';

/**
 * A cell that shows one thing and sorts by another: a link that sorts by its label, a
 * formatted number that sorts by the number.
 */
export function cell(node, key) {
  return { node, key };
}

function shown(value) {
  if (value && typeof value === 'object' && 'node' in value) return value.node;
  if (value instanceof Node) return value;
  if (typeof value === 'number') return num(value);
  return value ?? '—';
}

function sortKey(value) {
  if (value && typeof value === 'object' && 'node' in value) return value.key;
  if (value instanceof Node) return value.textContent;
  return value;
}

/** A plain table. `numeric` lists the column indexes aligned right. */
export function table(headers, rows, numeric = []) {
  return el('div', { class: 'scroll' },
    el('table', { class: 'data' },
      el('thead', {}, el('tr', {}, headers.map((h, i) => el('th', { class: numeric.includes(i) ? 'num' : null }, h)))),
      el('tbody', {}, rows.map(row => el('tr', {}, row.map((value, i) =>
        el('td', { class: numeric.includes(i) ? 'num' : null }, shown(value))))))));
}

/**
 * A table sorted by clicking a column heading (again to reverse it). Missing values sort
 * last either way. `sorted` is the column it starts sorted by, `descending` its direction.
 */
export function sortableTable(headers, rows, { numeric = [], sorted = null, descending = false, tall = false } = {}) {
  let column = sorted;
  let down = descending;
  const body = el('tbody');
  const heads = headers.map((h, i) => el('th', {
    class: `${numeric.includes(i) ? 'num ' : ''}sortable`,
    title: 'Sort by this column',
    onclick: () => {
      down = column === i ? !down : numeric.includes(i);
      column = i;
      draw();
    },
  }, h));
  function draw() {
    const ordered = [...rows];
    if (column !== null) {
      ordered.sort((a, b) => {
        const x = sortKey(a[column]);
        const y = sortKey(b[column]);
        const missingX = x === null || x === undefined || x === '';
        const missingY = y === null || y === undefined || y === '';
        if (missingX || missingY) return missingX === missingY ? 0 : missingX ? 1 : -1;
        const order = typeof x === 'number' && typeof y === 'number' ? x - y : String(x).localeCompare(String(y));
        return down ? -order : order;
      });
    }
    heads.forEach((th, i) => th.setAttribute('aria-sort', i === column ? (down ? 'descending' : 'ascending') : 'none'));
    body.replaceChildren(...ordered.map(row => el('tr', {}, row.map((value, i) =>
      el('td', { class: numeric.includes(i) ? 'num' : null }, shown(value))))));
  }
  draw();
  return el('div', { class: tall ? 'scroll tall' : 'scroll' },
    el('table', { class: 'data' }, el('thead', {}, el('tr', {}, heads)), body));
}

/** A horizontal bar chart of [label, value] rows. */
export function bars(rows, { format = num, alt = false } = {}) {
  const max = Math.max(1e-9, ...rows.map(([, v]) => v ?? 0));
  return el('div', { class: 'bars' }, rows.flatMap(([label, value]) => [
    el('span', {}, label),
    el('span', {}, el('div', { class: `bar${alt ? ' alt' : ''}`, style: { width: `${(100 * (value ?? 0)) / max}%` } })),
    el('span', { class: 'value' }, format(value)),
  ]));
}

export function card(title, ...children) {
  return el('div', { class: 'card' }, el('h3', {}, title), children);
}

/** A card holding a collapsed section: the rest of it opens on demand. */
export function folded(summary, ...children) {
  return el('details', { class: 'folded' }, el('summary', {}, summary), children);
}
