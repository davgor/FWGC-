// One loaded world, as the browser sees it: the summary, and blocks, layers and plans
// fetched on first use and kept.
//
// GRID. A world of size n samples the sphere on an n x n grid: column x is longitude
// -180 + 360 x / (n - 1) and row z is latitude 90 - 180 z / (n - 1). Column n - 1 repeats
// column 0 (the seam), and rows 0 and n - 1 are the poles. The map works in degrees from
// the top-left corner -- X = longitude + 180, Y = 90 - latitude -- so a cell is 360/(n-1)
// wide and 180/(n-1) tall, and the map is twice as wide as it is tall.
//
// NODES. Routes (roads, rivers, band legs) are lists of node indexes into the unique points
// of that grid: node 0 is the north pole, then n - 1 nodes for each row from 1 to n - 2
// (the seam column left out), then the south pole. `nodeXZ` turns one back into a cell.

import { api } from './api.js';

export class Layer {
  constructor(name, rows, cols, values) {
    this.name = name;
    this.rows = rows;
    this.cols = cols;
    this.values = values; // Float64Array, row-major: [z * cols + x]
    let min = Infinity;
    let max = -Infinity;
    let integer = true;
    let distinct = new Map();
    for (let i = 0; i < values.length; i++) {
      const v = values[i];
      if (v < min) min = v;
      if (v > max) max = v;
      if (integer && !Number.isInteger(v)) integer = false;
      if (distinct) {
        distinct.set(v, (distinct.get(v) ?? 0) + 1);
        if (distinct.size > 256) distinct = null;
      }
    }
    this.min = min;
    this.max = max;
    this.integer = integer;
    this.distinct = distinct; // value -> cell count, or null when there are more than 256 values
    this.constant = min === max;
  }

  at(x, z) {
    return this.values[z * this.cols + x];
  }

  static fromResponse(response) {
    const rows = response.values.length;
    const cols = rows ? response.values[0].length : 0;
    const values = new Float64Array(rows * cols);
    for (let z = 0; z < rows; z++) values.set(response.values[z], z * cols);
    return new Layer(response.name, rows, cols, values);
  }
}

export class World {
  constructor(id, summary) {
    this.id = id;
    this.summary = summary;
    this.n = summary.size ?? summary.config?.size ?? summary.layers?.[0]?.rows ?? 0;
    this.blockInfo = new Map(summary.blocks.map(b => [b.name, b]));
    this.layerNames = summary.layers.map(l => l.name);
    this._blocks = new Map();
    this._selections = new Map();
    this._layers = new Map();
    this._plans = new Map();
    this._indexes = new Map();
  }

  get config() {
    return this.summary.effective_config ?? this.summary.config ?? {};
  }

  get radius() {
    return this.summary.topology?.radius_m ?? this.config.globe_radius ?? 1;
  }

  has(name) {
    return this.blockInfo.has(name);
  }

  hasLayer(name) {
    return this.layerNames.includes(name);
  }

  /** A whole top-level block, or null when the world has none. */
  block(name) {
    if (!this.has(name)) return Promise.resolve(null);
    if (!this._blocks.has(name)) {
      const pending = api.block(this.id, name);
      pending.catch(() => this._blocks.delete(name));
      this._blocks.set(name, pending);
    }
    return this._blocks.get(name);
  }

  /** Part of a block, selected on the server (see the API's path/where/fields). */
  select(name, params) {
    if (!this.has(name)) return Promise.resolve(null);
    const key = `${name}?${JSON.stringify(params)}`;
    if (!this._selections.has(key)) {
      const pending = api.block(this.id, name, params);
      pending.catch(() => this._selections.delete(key));
      this._selections.set(key, pending);
    }
    return this._selections.get(key);
  }

  layer(name) {
    if (!this.hasLayer(name)) return Promise.resolve(null);
    if (!this._layers.has(name)) {
      const pending = api.layer(this.id, name).then(Layer.fromResponse);
      pending.catch(() => this._layers.delete(name));
      this._layers.set(name, pending);
    }
    return this._layers.get(name);
  }

  /** The pending or loaded layer, or undefined if it was never asked for (no request is made). */
  loadedLayer(name) {
    return this._layers.get(name);
  }

  /** The pending or loaded block, or undefined if it was never asked for (no request is made). */
  loadedBlock(name) {
    return this._blocks.get(name);
  }

  /** One plan; the last few are kept, since they run to a megabyte each. */
  plan(kind, key) {
    const cacheKey = `${kind}/${key}`;
    if (!this._plans.has(cacheKey)) {
      const pending = api.plan(this.id, kind, key);
      pending.catch(() => this._plans.delete(cacheKey));
      this._plans.set(cacheKey, pending);
      while (this._plans.size > 4) this._plans.delete(this._plans.keys().next().value);
    }
    return this._plans.get(cacheKey);
  }

  /** Memoise a derived lookup (a Map by uid, say) built from loaded blocks. */
  async index(key, build) {
    if (!this._indexes.has(key)) {
      const pending = Promise.resolve().then(build);
      pending.catch(() => this._indexes.delete(key));
      this._indexes.set(key, pending);
    }
    return this._indexes.get(key);
  }

  // --- geometry ---------------------------------------------------------------------

  get cellX() {
    return 360 / (this.n - 1);
  }

  get cellY() {
    return 180 / (this.n - 1);
  }

  lon(x) {
    return -180 + (360 * x) / (this.n - 1);
  }

  lat(z) {
    return 90 - (180 * z) / (this.n - 1);
  }

  /** Map degrees of a grid cell. */
  cellXY(x, z) {
    return [(360 * x) / (this.n - 1), (180 * z) / (this.n - 1)];
  }

  nodeXZ(node) {
    const n = this.n;
    if (node <= 0) return [0, 0];
    const last = (n - 2) * (n - 1) + 1;
    if (node >= last) return [0, n - 1];
    const k = node - 1;
    return [k % (n - 1), 1 + Math.floor(k / (n - 1))];
  }

  nodeXY(node) {
    const [x, z] = this.nodeXZ(node);
    return this.cellXY(x, z);
  }

  /** Map degrees of a unit direction (cos lat cos lon, sin lat, cos lat sin lon). */
  static directionXY(d) {
    const length = Math.hypot(d[0], d[1], d[2]) || 1;
    const lon = (Math.atan2(d[2], d[0]) * 180) / Math.PI;
    const lat = (Math.asin(Math.max(-1, Math.min(1, d[1] / length))) * 180) / Math.PI;
    return [lon + 180, 90 - lat];
  }

  /** The cell under map degrees (X, Y), wrapping X. */
  cellAt(X, Y) {
    const n = this.n;
    const x = ((Math.round(X / this.cellX) % (n - 1)) + (n - 1)) % (n - 1);
    const z = Math.max(0, Math.min(n - 1, Math.round(Y / this.cellY)));
    return [x, z];
  }
}

/**
 * Turn a polyline of map degrees into one that never jumps across the seam: each point is
 * shifted by a whole turn to sit within half a turn of the one before. Drawn at offsets of
 * -360, 0 and +360 it then wraps correctly.
 */
export function unwrap(points) {
  const out = [];
  let previous = null;
  for (const [X, Y] of points) {
    let x = X;
    if (previous !== null) {
      while (x - previous > 180) x -= 360;
      while (previous - x > 180) x += 360;
    }
    out.push([x, Y]);
    previous = x;
  }
  return out;
}
