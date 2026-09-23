// A byte-span index over a JSON document, built without parsing it.
//
// World documents run from about 20 MB (size 17) to about 700 MB (size 513). Parsing one
// whole costs a second and roughly four times its size in heap at size 257, and at size
// 513 it cannot be done at all: the text is longer than the longest string V8 will make.
// The viewer never needs a whole document at once -- it asks for one block, one layer or
// one plan -- so instead of parsing, this module walks the raw bytes once and records where
// each top-level block (and, below it, each member) starts and ends. A request is then
// answered by slicing those bytes off disk, and only the small pieces anything actually
// reads are ever parsed.
//
// The walk looks only at the ASCII structural characters: quotes, backslashes, braces,
// brackets, commas and colons. Every byte of a multi-byte UTF-8 sequence is 0x80 or above,
// so none can be mistaken for one of them.

const QUOTE = 0x22;
const BACKSLASH = 0x5c;
const LBRACE = 0x7b;
const RBRACE = 0x7d;
const LBRACK = 0x5b;
const RBRACK = 0x5d;
const COMMA = 0x2c;
const COLON = 0x3a;

function isWhitespace(byte) {
  return byte === 0x20 || byte === 0x0a || byte === 0x0d || byte === 0x09;
}

export function skipWhitespace(buf, i, end = buf.length) {
  while (i < end && isWhitespace(buf[i])) i++;
  return i;
}

// `i` is at an opening quote; returns the index just past the closing one. Jumps from quote
// to quote with the native indexOf, and treats a quote as escaped only when an odd number
// of backslashes precedes it.
function skipString(buf, i) {
  let from = i + 1;
  for (;;) {
    const quote = buf.indexOf(QUOTE, from);
    if (quote < 0) throw new SyntaxError(`unterminated string starting at byte ${i}`);
    let backslashes = 0;
    for (let k = quote - 1; buf[k] === BACKSLASH; k--) backslashes++;
    if ((backslashes & 1) === 0) return quote + 1;
    from = quote + 1;
  }
}

// `i` is at `{` or `[`; returns the index just past its matching close.
function skipContainer(buf, i) {
  let depth = 0;
  const length = buf.length;
  for (let j = i; j < length; j++) {
    const byte = buf[j];
    if (byte === QUOTE) {
      j = skipString(buf, j) - 1;
    } else if (byte === LBRACE || byte === LBRACK) {
      depth++;
    } else if (byte === RBRACE || byte === RBRACK) {
      depth--;
      if (depth === 0) return j + 1;
    }
  }
  throw new SyntaxError(`unterminated container starting at byte ${i}`);
}

// A number, true, false or null: runs to the next delimiter.
function skipScalar(buf, i) {
  const length = buf.length;
  let j = i;
  while (j < length) {
    const byte = buf[j];
    if (byte === COMMA || byte === RBRACE || byte === RBRACK || isWhitespace(byte)) break;
    j++;
  }
  if (j === i) throw new SyntaxError(`expected a value at byte ${i}`);
  return j;
}

// Returns the index just past the value that starts at `i`.
export function skipValue(buf, i) {
  const byte = buf[i];
  if (byte === QUOTE) return skipString(buf, i);
  if (byte === LBRACE || byte === LBRACK) return skipContainer(buf, i);
  return skipScalar(buf, i);
}

function typeOf(byte) {
  if (byte === LBRACE) return 'object';
  if (byte === LBRACK) return 'array';
  if (byte === QUOTE) return 'string';
  if (byte === 0x74 || byte === 0x66) return 'boolean';
  if (byte === 0x6e) return 'null';
  return 'number';
}

// Object keys in these documents never carry escapes, but decode them properly if one does.
function decodeKey(buf, start, end) {
  const raw = buf.toString('utf8', start + 1, end - 1);
  return raw.includes('\\') ? JSON.parse(buf.toString('utf8', start, end)) : raw;
}

/**
 * Walk the value at `start` and describe its shape down to `maxDepth`.
 *
 * Returns a node `{start, end, type}`. Above `maxDepth`, an object node also carries
 * `members` (a Map of key to child node, in document order) and an array node carries
 * `count`; an array whose dotted path is listed in `elementPaths` also keeps `elements`,
 * the [start, end) span of each element. At `maxDepth` a value is skipped whole.
 */
export function indexValue(buf, start, { maxDepth = 3, elementPaths = new Set() } = {}) {
  return walk(buf, skipWhitespace(buf, start), 0, '', maxDepth, elementPaths);
}

function walk(buf, i, depth, path, maxDepth, elementPaths) {
  const first = buf[i];
  const node = { start: i, end: 0, type: typeOf(first) };
  if (first !== LBRACE && first !== LBRACK) {
    node.end = skipValue(buf, i);
    return node;
  }
  if (depth >= maxDepth) {
    node.end = skipContainer(buf, i);
    return node;
  }

  if (first === LBRACE) {
    node.members = new Map();
    let j = skipWhitespace(buf, i + 1);
    if (buf[j] === RBRACE) {
      node.end = j + 1;
      return node;
    }
    for (;;) {
      if (buf[j] !== QUOTE) throw new SyntaxError(`expected a key at byte ${j}`);
      const keyEnd = skipString(buf, j);
      const key = decodeKey(buf, j, keyEnd);
      j = skipWhitespace(buf, keyEnd);
      if (buf[j] !== COLON) throw new SyntaxError(`expected ':' at byte ${j}`);
      j = skipWhitespace(buf, j + 1);
      const child = walk(buf, j, depth + 1, path ? `${path}.${key}` : key, maxDepth, elementPaths);
      node.members.set(key, child);
      j = skipWhitespace(buf, child.end);
      if (buf[j] === COMMA) {
        j = skipWhitespace(buf, j + 1);
        continue;
      }
      if (buf[j] === RBRACE) {
        node.end = j + 1;
        return node;
      }
      throw new SyntaxError(`expected ',' or '}' at byte ${j}`);
    }
  }

  // An array: count its elements, keeping their spans only where asked to.
  node.count = 0;
  const keep = elementPaths.has(path);
  if (keep) node.elements = [];
  let j = skipWhitespace(buf, i + 1);
  if (buf[j] === RBRACK) {
    node.end = j + 1;
    return node;
  }
  for (;;) {
    const end = skipValue(buf, j);
    if (keep) node.elements.push([j, end]);
    node.count++;
    j = skipWhitespace(buf, end);
    if (buf[j] === COMMA) {
      j = skipWhitespace(buf, j + 1);
      continue;
    }
    if (buf[j] === RBRACK) {
      node.end = j + 1;
      return node;
    }
    throw new SyntaxError(`expected ',' or ']' at byte ${j}`);
  }
}

/**
 * Read chosen members of the object spanning [start, end) without walking all of it.
 *
 * Scans members in order and stops as soon as every wanted key has been seen, so finding
 * `city_uid` near the top of a half-megabyte plan costs a few dozen bytes, not the plan.
 * Returns an object of key -> parsed value for the keys found.
 */
export function readMembers(buf, start, end, wanted) {
  const found = {};
  let remaining = wanted.size;
  let j = skipWhitespace(buf, start, end);
  if (buf[j] !== LBRACE) return found;
  j = skipWhitespace(buf, j + 1, end);
  while (remaining > 0 && j < end && buf[j] === QUOTE) {
    const keyEnd = skipString(buf, j);
    const key = decodeKey(buf, j, keyEnd);
    j = skipWhitespace(buf, keyEnd, end);
    if (buf[j] !== COLON) break;
    j = skipWhitespace(buf, j + 1, end);
    const valueEnd = skipValue(buf, j);
    if (wanted.has(key) && !(key in found)) {
      found[key] = JSON.parse(buf.toString('utf8', j, valueEnd));
      remaining--;
    }
    j = skipWhitespace(buf, valueEnd, end);
    if (buf[j] !== COMMA) break;
    j = skipWhitespace(buf, j + 1, end);
  }
  return found;
}

/** Count the elements of the array spanning [start, end) -- used for a layer's row width. */
export function countElements(buf, start) {
  let j = skipWhitespace(buf, start);
  if (buf[j] !== LBRACK) return 0;
  j = skipWhitespace(buf, j + 1);
  if (buf[j] === RBRACK) return 0;
  let count = 0;
  for (;;) {
    const end = skipValue(buf, j);
    count++;
    j = skipWhitespace(buf, end);
    if (buf[j] !== COMMA) return count;
    j = skipWhitespace(buf, j + 1);
  }
}
