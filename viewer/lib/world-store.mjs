// Open world documents: an index of where everything is, and the few small pieces parsed.
//
// A document is read and indexed once (see json-index.mjs), then answered from its bytes on
// disk: a block, a layer or a plan is a byte range, streamed as-is. Only what the server
// itself must look inside -- the handful of small blocks a summary quotes, and a block a
// request asks to filter or project -- is ever parsed, and those parses are cached.
//
// The store keeps the indexes of the last few documents opened (two by default) and drops
// the oldest beyond that. An index is a few hundred kilobytes even at size 513; the parsed
// blocks are held in one cache shared by all open documents, bounded by source bytes.

import fs from 'node:fs';
import fsp from 'node:fs/promises';

import { countElements, indexValue, readMembers, skipWhitespace } from './json-index.mjs';

// The plan arrays. `key` is how the document keys each plan -- the stable uid a site record
// carries -- and `aliases` are the other fields a plan can be found by.
export const PLAN_KINDS = {
  city: { block: 'city_plans', array: 'cities', key: 'city_uid', aliases: ['site_id', 'name'] },
  hamlet: { block: 'hamlet_plans', array: 'hamlets', key: 'hamlet_uid', aliases: ['hamlet_id', 'node'] },
  castle: { block: 'castle_plans', array: 'castles', key: 'fortress_uid', aliases: ['fortress_id', 'node'] },
  key_location: { block: 'key_location_plans', array: 'plans', key: 'location_id', aliases: [] },
};

// Small blocks the summary quotes whole. Everything else is described by the index alone.
const SUMMARY_BLOCKS = [
  'schema', 'schema_version', 'generator_version', 'completed_stage', 'config', 'effective_config',
  'physical_scale', 'topology', 'spacing_m', 'effective_tpi_radius_m', 'resolved_octaves', 'seeds',
  'area', 'systems', 'warnings', 'timing_ms',
];

// A summary also quotes these members of `phases`, whose `plates` it leaves out.
const PHASE_MEMBERS = ['completed', 'titles', 'default_layer', 'version'];

export class NotFoundError extends Error {
  constructor(message) {
    super(message);
    this.status = 404;
    this.code = 'not_found';
  }
}

export class BadRequestError extends Error {
  constructor(message) {
    super(message);
    this.status = 400;
    this.code = 'bad_request';
  }
}

/** One indexed document on disk. */
export class WorldDocument {
  constructor(id, file) {
    this.id = id;
    this.file = file;
    this.bytes = 0;
    this.mtimeMs = 0;
    this.indexMs = 0;
    this.root = null; // the document's index node
    this.layerShapes = new Map(); // layer name -> {rows, cols}
    this.plans = {}; // kind -> {count, spans: [[start, end]], byKey: Map(string -> index)}
    this._summary = null;
  }

  /** Read the file once, index it, and note what later requests will look up. */
  async load() {
    const started = performance.now();
    const stat = await fsp.stat(this.file);
    this.bytes = stat.size;
    this.mtimeMs = stat.mtimeMs;
    const buf = await this.readSlice(0, stat.size);
    const elementPaths = new Set(Object.values(PLAN_KINDS).map(k => `${k.block}.${k.array}`));
    this.root = indexValue(buf, 0, { maxDepth: 3, elementPaths });
    if (this.root.type !== 'object') throw new Error(`${this.file} is not a JSON object`);

    // Each layer is n rows of n values; the row count is in the index, the width is not.
    const layers = this.root.members.get('layers');
    if (layers?.members) {
      for (const [name, node] of layers.members) {
        const cols = node.type === 'array' && node.count > 0 ? countElements(buf, skipWhitespace(buf, node.start + 1)) : 0;
        this.layerShapes.set(name, { rows: node.count ?? 0, cols });
      }
    }

    // Each plan's keys, read from the first few members of its object.
    for (const [kind, spec] of Object.entries(PLAN_KINDS)) {
      const array = this.root.members.get(spec.block)?.members?.get(spec.array);
      if (!array?.elements) continue;
      const wanted = new Set([spec.key, ...spec.aliases]);
      const byKey = new Map();
      array.elements.forEach(([start, end], index) => {
        const keys = readMembers(buf, start, end, wanted);
        // The primary key wins over an alias that happens to collide with it.
        for (const field of [...spec.aliases].reverse()) {
          if (keys[field] !== undefined && keys[field] !== null) byKey.set(String(keys[field]), index);
        }
        if (keys[spec.key] !== undefined && keys[spec.key] !== null) byKey.set(String(keys[spec.key]), index);
      });
      this.plans[kind] = { count: array.count, spans: array.elements, byKey };
    }
    this.indexMs = performance.now() - started;
    return this;
  }

  hasBlock(name) {
    return this.root.members.has(name);
  }

  blockNode(name) {
    const node = this.root.members.get(name);
    if (!node) throw new NotFoundError(`This world has no block '${name}'.`);
    return node;
  }

  layerNode(name) {
    const node = this.root.members.get('layers')?.members?.get(name);
    if (!node) throw new NotFoundError(`This world has no layer '${name}'.`);
    return node;
  }

  planSpan(kind, key) {
    if (!Object.hasOwn(PLAN_KINDS, kind)) throw new NotFoundError(`Unknown plan kind '${kind}'; use ${Object.keys(PLAN_KINDS).join(', ')}.`);
    const spec = PLAN_KINDS[kind];
    const plans = Object.hasOwn(this.plans, kind) ? this.plans[kind] : null;
    if (!plans) throw new NotFoundError(`This world has no ${spec.block} block.`);
    const index = plans.byKey.get(String(key));
    if (index === undefined) {
      throw new NotFoundError(`No ${kind} plan keyed '${key}' in ${spec.block}.${spec.array} (keyed by ${[spec.key, ...spec.aliases].join(' or ')}).`);
    }
    return plans.spans[index];
  }

  /**
   * Read [start, end) of the file into a Buffer, in reads of at most 1 GiB: one read, like
   * fs.readFile, stops at 2 GiB, and a size-1025 document is larger than that.
   */
  async readSlice(start, end) {
    const handle = await fsp.open(this.file, 'r');
    try {
      const out = Buffer.allocUnsafe(end - start);
      let offset = 0;
      while (offset < out.length) {
        const length = Math.min(out.length - offset, 1 << 30);
        const { bytesRead } = await handle.read(out, offset, length, start + offset);
        if (bytesRead === 0) throw new Error(`${this.file} ended early; was it replaced while open?`);
        offset += bytesRead;
      }
      return out;
    } finally {
      await handle.close();
    }
  }

  /** A read stream over [start, end) of the file. */
  streamSlice(start, end) {
    return fs.createReadStream(this.file, { start, end: end - 1 });
  }

  async parseSpan(start, end) {
    return JSON.parse((await this.readSlice(start, end)).toString('utf8'));
  }

  /** The summary: the small blocks whole, and the rest described by the index. */
  async summary() {
    if (this._summary) return this._summary;
    const out = { id: this.id, bytes: this.bytes, index_ms: Math.round(this.indexMs) };
    for (const name of SUMMARY_BLOCKS) {
      const node = this.root.members.get(name);
      out[name] = node ? await this.parseSpan(node.start, node.end) : null;
    }
    const phases = this.root.members.get('phases');
    if (phases?.members) {
      out.phases = {};
      for (const member of PHASE_MEMBERS) {
        const node = phases.members.get(member);
        if (node) out.phases[member] = await this.parseSpan(node.start, node.end);
      }
    } else {
      out.phases = null;
    }
    out.size = out.config?.size ?? null;

    // Counts per block: an array's length, an object's member count, and the length of every
    // array directly inside an object block (settlements.sites, humans.hamlets, ...).
    out.blocks = [];
    for (const [name, node] of this.root.members) {
      const row = { name, type: node.type, bytes: node.end - node.start };
      if (node.type === 'array') row.count = node.count;
      if (node.type === 'object' && node.members) {
        row.count = node.members.size;
        const arrays = {};
        for (const [member, child] of node.members) {
          if (child.type === 'array' && name !== 'layers') arrays[member] = child.count;
        }
        if (Object.keys(arrays).length) row.arrays = arrays;
      }
      out.blocks.push(row);
    }
    out.layers = [...this.layerShapes].map(([name, shape]) => ({ name, ...shape }));
    out.plans = Object.fromEntries(Object.entries(this.plans).map(([kind, p]) => [kind, p.count]));
    this._summary = out;
    return out;
  }
}

/**
 * The open documents and the parsed-block cache.
 *
 * `resolveFile(id)` names the file behind an id, or returns null for an id the cache does
 * not hold. Two requests for the same unopened document share one load.
 */
export class WorldStore {
  constructor({ resolveFile, maxOpen = 2, parsedBudgetBytes = 160 * 1024 * 1024 }) {
    this.resolveFile = resolveFile;
    this.maxOpen = maxOpen;
    this.parsedBudgetBytes = parsedBudgetBytes;
    this.open = new Map(); // id -> Promise<WorldDocument>, oldest first
    this.parsed = new Map(); // `${id}\0${block}` -> {value, bytes}, oldest first
    this.parsedBytes = 0;
  }

  /** The document for `id`, loading and indexing it on first use. */
  async get(id) {
    let pending = this.open.get(id);
    if (pending) {
      // Most recently used goes to the back of the line.
      this.open.delete(id);
      this.open.set(id, pending);
    } else {
      const file = await this.resolveFile(id);
      if (!file) throw new NotFoundError(`No world '${id}' in the cache. Generate it again.`);
      pending = new WorldDocument(id, file).load();
      this.open.set(id, pending);
      pending.catch(() => this.open.delete(id));
      while (this.open.size > this.maxOpen) this.forget(this.open.keys().next().value);
    }
    const doc = await pending;
    // The file was regenerated or removed underneath us: index it afresh.
    const stat = await fsp.stat(doc.file).catch(() => null);
    if (!stat) {
      this.forget(id);
      throw new NotFoundError(`World '${id}' was removed from the cache. Generate it again.`);
    }
    if (stat.mtimeMs !== doc.mtimeMs || stat.size !== doc.bytes) {
      this.forget(id);
      return this.get(id);
    }
    return doc;
  }

  isOpen(id) {
    return this.open.has(id);
  }

  forget(id) {
    this.open.delete(id);
    for (const key of [...this.parsed.keys()]) {
      if (key.startsWith(`${id}\0`)) this.dropParsed(key);
    }
  }

  dropParsed(key) {
    const entry = this.parsed.get(key);
    if (!entry) return;
    this.parsedBytes -= entry.bytes;
    this.parsed.delete(key);
  }

  /** A top-level block, parsed. Cached, least recently used first out. */
  async parsedBlock(doc, name) {
    const key = `${doc.id}\0${name}`;
    const hit = this.parsed.get(key);
    if (hit) {
      this.parsed.delete(key);
      this.parsed.set(key, hit);
      return hit.value;
    }
    const node = doc.blockNode(name);
    const bytes = node.end - node.start;
    const value = await doc.parseSpan(node.start, node.end);
    if (bytes <= this.parsedBudgetBytes) {
      this.parsed.set(key, { value, bytes });
      this.parsedBytes += bytes;
      while (this.parsedBytes > this.parsedBudgetBytes && this.parsed.size > 1) {
        this.dropParsed(this.parsed.keys().next().value);
      }
    }
    return value;
  }
}

/**
 * Select within a parsed block: `path` walks members and array indexes ("sites.12"),
 * `where` keeps array elements whose field equals a value ("site_uid:abc", repeatable, all
 * must hold), `offset`/`limit` page an array, and `fields` keeps only the named fields of
 * each object. Returns the selected value.
 */
export function selectWithin(value, { path, where = [], fields, offset, limit }) {
  let current = value;
  if (path) {
    for (const part of path.split('.').filter(Boolean)) {
      if (Array.isArray(current)) {
        if (!/^\d+$/.test(part)) throw new BadRequestError(`'${part}' is not an index into an array.`);
        current = current[Number(part)];
      } else if (current && typeof current === 'object') {
        current = Object.hasOwn(current, part) ? current[part] : undefined;
      } else {
        current = undefined;
      }
      if (current === undefined) throw new NotFoundError(`Nothing at path '${path}'.`);
    }
  }
  if (where.length) {
    if (!Array.isArray(current)) throw new BadRequestError('where= applies to an array; add a path= that selects one.');
    const tests = where.map(clause => {
      const colon = clause.indexOf(':');
      if (colon < 1) throw new BadRequestError(`where= needs field:value, got '${clause}'.`);
      return [clause.slice(0, colon), clause.slice(colon + 1)];
    });
    current = current.filter(item => item && tests.every(([field, expected]) => String(item[field]) === expected));
  }
  if (offset !== undefined || limit !== undefined) {
    if (!Array.isArray(current)) throw new BadRequestError('offset= and limit= apply to an array.');
    const from = offset ?? 0;
    current = current.slice(from, limit === undefined ? undefined : from + limit);
  }
  if (fields?.length) {
    const pick = item => {
      if (!item || typeof item !== 'object' || Array.isArray(item)) return item;
      const out = {};
      for (const field of fields) if (Object.hasOwn(item, field)) out[field] = item[field];
      return out;
    };
    current = Array.isArray(current) ? current.map(pick) : pick(current);
  }
  return current;
}
