// Small DOM and formatting helpers shared by every view.

export const $ = id => document.getElementById(id);

/**
 * Create an element. `attrs` may hold `class`, `text`, `html` (trusted strings only),
 * `dataset`, `style` (an object), `on<event>` handlers, and plain attributes; `false`,
 * `null` and `undefined` attributes are skipped. Children may be nodes, strings, numbers or
 * nested arrays of them.
 */
export function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  applyAttrs(node, attrs);
  append(node, children);
  return node;
}

const SVG_NS = 'http://www.w3.org/2000/svg';

/** Create an SVG element; attributes as in `el`. */
export function svg(tag, attrs = {}, ...children) {
  const node = document.createElementNS(SVG_NS, tag);
  applyAttrs(node, attrs);
  append(node, children);
  return node;
}

function applyAttrs(node, attrs) {
  for (const [key, value] of Object.entries(attrs ?? {})) {
    if (value === false || value === null || value === undefined) continue;
    if (key === 'class') node.setAttribute('class', value);
    else if (key === 'text') node.textContent = value;
    else if (key === 'html') node.innerHTML = value;
    else if (key === 'dataset') Object.assign(node.dataset, value);
    else if (key === 'style' && typeof value === 'object') Object.assign(node.style, value);
    else if (key.startsWith('on') && typeof value === 'function') node.addEventListener(key.slice(2), value);
    else node.setAttribute(key, value === true ? '' : String(value));
  }
}

function append(node, children) {
  for (const child of children.flat(Infinity)) {
    if (child === null || child === undefined || child === false) continue;
    node.append(child instanceof Node ? child : String(child));
  }
}

/** snake_case or kebab-case to "Title case words". */
export function titleCase(text) {
  if (text === null || text === undefined) return '';
  const words = String(text).replace(/[_-]+/g, ' ').trim();
  return words.charAt(0).toUpperCase() + words.slice(1);
}

/** A number for people: grouped, with sensible precision for its size. */
export function num(value, digits) {
  if (value === null || value === undefined || Number.isNaN(value)) return '—';
  if (typeof value !== 'number') return String(value);
  if (!Number.isFinite(value)) return String(value);
  if (digits !== undefined) return value.toLocaleString('en', { maximumFractionDigits: digits, minimumFractionDigits: 0 });
  const abs = Math.abs(value);
  if (Number.isInteger(value)) return value.toLocaleString('en');
  if (abs !== 0 && (abs < 1e-3 || abs >= 1e9)) return value.toExponential(3);
  if (abs < 1) return value.toLocaleString('en', { maximumSignificantDigits: 3 });
  if (abs < 100) return value.toLocaleString('en', { maximumFractionDigits: 2 });
  return value.toLocaleString('en', { maximumFractionDigits: 0 });
}

/** A size in decimal units, as the generator's own documentation quotes them. */
export function bytes(n) {
  if (n === null || n === undefined) return '—';
  if (n < 1e3) return `${n} B`;
  if (n < 1e6) return `${(n / 1e3).toFixed(1)} kB`;
  if (n < 1e9) return `${(n / 1e6).toFixed(1)} MB`;
  return `${(n / 1e9).toFixed(2)} GB`;
}

export function seconds(ms) {
  if (ms === null || ms === undefined) return '—';
  return ms < 1000 ? `${Math.round(ms)} ms` : `${(ms / 1000).toFixed(1)} s`;
}

export function rgb(color, alpha) {
  if (!color) return 'transparent';
  return alpha === undefined ? `rgb(${color[0]},${color[1]},${color[2]})` : `rgba(${color[0]},${color[1]},${color[2]},${alpha})`;
}

/** Latitude and longitude as "12.3°N 45.6°W". */
export function latLon(lat, lon) {
  const ns = lat >= 0 ? 'N' : 'S';
  const ew = lon >= 0 ? 'E' : 'W';
  return `${Math.abs(lat).toFixed(1)}°${ns} ${Math.abs(lon).toFixed(1)}°${ew}`;
}

/** Run `fn` at most once per animation frame, with the latest arguments. */
export function throttleFrame(fn) {
  let pending = null;
  return (...args) => {
    const first = pending === null;
    pending = args;
    if (first) {
      requestAnimationFrame(() => {
        const latest = pending;
        pending = null;
        fn(...latest);
      });
    }
  };
}
