// The world map: an equirectangular canvas one block per cell, with pan, zoom and a
// seamless wrap in longitude, and the overlays drawn over it.
//
// Coordinates are map degrees (see world.js): X = longitude + 180, Y = 90 - latitude. The
// view maps them to CSS pixels with one scale for both axes, so a cell -- 360/(n-1) by
// 180/(n-1) degrees -- always shows twice as wide as it is tall.

import { throttleFrame } from './dom.js';

const MIN_ZOOM = 0.9; // relative to "whole world fits"
const MAX_CELL_PX = 160; // zoom in until one cell is this wide
const CLICK_SLOP = 4; // pixels a press may move and still count as a click

export class MapView {
  constructor(canvas, { onHover = () => {}, onClick = () => {} } = {}) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.onHover = onHover;
    this.onClick = onClick;
    this.world = null;
    this.base = null; // canvas of (n-1) x n pixels, one per unique cell
    this.overlays = []; // [{id, draw(ctx, view), hit(view, sx, sy)}], in draw order
    this.k = 1; // CSS pixels per degree
    this.tx = 0;
    this.ty = 0;
    this.width = 0;
    this.height = 0;
    this.fitK = 1;
    this.fitted = true; // still showing the whole world, so a resize refits
    this.lastDrawMs = 0;
    this.requestRender = throttleFrame(() => this.draw());
    this.bindEvents();
    new ResizeObserver(() => {
      if (this.fitted || this.width === 0) {
        this.fit();
        return;
      }
      // keep whatever was at the centre at the centre
      const [cx, cy] = this.toMap(this.width / 2, this.height / 2);
      this.measure();
      this.tx = this.width / 2 - cx * this.k;
      this.ty = this.height / 2 - cy * this.k;
      this.clampPan();
      this.requestRender();
    }).observe(canvas);
  }

  // --- state --------------------------------------------------------------------------

  setWorld(world) {
    this.world = world;
    this.base = null;
    this.overlays = [];
    this.fit();
  }

  setBase(canvas) {
    this.base = canvas;
    this.requestRender();
  }

  setOverlays(overlays) {
    this.overlays = overlays;
    this.requestRender();
  }

  // --- view ---------------------------------------------------------------------------

  measure() {
    const rect = this.canvas.getBoundingClientRect();
    this.width = rect.width;
    this.height = rect.height;
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(rect.width * dpr));
    const h = Math.max(1, Math.round(rect.height * dpr));
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
    }
    // The painted world is 360 degrees wide and half a cell taller than 180 at each pole.
    const [, extentY] = this.extent();
    this.fitK = Math.min(this.width / 360, this.height / extentY) || 1;
  }

  /** The painted world: [width, height] in degrees, and where its top-left corner sits. */
  extent() {
    const cellX = this.world?.cellX ?? 0;
    const cellY = this.world?.cellY ?? 0;
    return [360, 180 + cellY, -cellX / 2, -cellY / 2];
  }

  fit() {
    this.measure();
    const [w, h, x0, y0] = this.extent();
    this.k = this.fitK * 0.98;
    this.tx = (this.width - w * this.k) / 2 - x0 * this.k;
    this.ty = (this.height - h * this.k) / 2 - y0 * this.k;
    this.fitted = true;
    this.requestRender();
  }

  /** Centre the view on map degrees (X, Y), zoomed so a cell is about `cellPx` wide. */
  centerOn(X, Y, cellPx) {
    if (this.world && cellPx) {
      this.k = this.clampK(cellPx / this.world.cellX);
    }
    this.tx = this.width / 2 - X * this.k;
    this.ty = this.height / 2 - Y * this.k;
    this.fitted = false;
    this.clampPan();
    this.requestRender();
  }

  /** True when one copy of the world is narrower than the canvas: no wrapped copies then. */
  wholeWorldFits() {
    return 360 * this.k <= this.width;
  }

  clampK(k) {
    const maxK = this.world ? MAX_CELL_PX / this.world.cellX : this.fitK * 64;
    return Math.max(this.fitK * MIN_ZOOM, Math.min(Math.max(maxK, this.fitK), k));
  }

  zoomAt(sx, sy, factor) {
    const k = this.clampK(this.k * factor);
    const f = k / this.k;
    this.tx = sx - (sx - this.tx) * f;
    this.ty = sy - (sy - this.ty) * f;
    this.k = k;
    this.fitted = false;
    this.clampPan();
    this.requestRender();
  }

  zoomBy(factor) {
    this.zoomAt(this.width / 2, this.height / 2, factor);
  }

  // Keep some of the map on screen vertically. Horizontally, a world narrower than the
  // canvas stays wholly on it; a wider one wraps, and tx is kept within a turn so it never
  // drifts far enough to lose precision.
  clampPan() {
    const mapH = 180 * this.k;
    const margin = Math.min(this.height * 0.5, mapH * 0.5);
    this.ty = Math.min(this.height - margin, Math.max(margin - mapH, this.ty));
    const turn = 360 * this.k;
    if (this.wholeWorldFits()) {
      const [, , x0] = this.extent();
      this.tx = Math.min(this.width - (x0 + 360) * this.k, Math.max(-x0 * this.k, this.tx));
      return;
    }
    while (this.tx > turn) this.tx -= turn;
    while (this.tx < -turn) this.tx += turn;
  }

  toScreen(X, Y) {
    return [X * this.k + this.tx, Y * this.k + this.ty];
  }

  toMap(sx, sy) {
    return [(sx - this.tx) / this.k, (sy - this.ty) / this.k];
  }

  /** The visible range of map X, in degrees (unbounded: it may run past 0 or 360). */
  visibleX() {
    return [-this.tx / this.k, (this.width - this.tx) / this.k];
  }

  /**
   * Whole turns m for which something spanning map X [xmin, xmax], shifted by 360 m, is
   * on screen. Every overlay draws once per turn returned.
   */
  turnsFor(xmin, xmax) {
    const [v0, v1] = this.visibleX();
    if (this.wholeWorldFits()) {
      // one copy only; a line unwrapped past the seam is drawn again from the other side
      const turns = [0];
      if (xmin < 0) turns.push(1);
      if (xmax > 360) turns.push(-1);
      return turns.filter(m => xmax + 360 * m >= v0 && xmin + 360 * m <= v1);
    }
    const turns = [];
    const first = Math.floor((v0 - xmax) / 360);
    const last = Math.ceil((v1 - xmin) / 360);
    for (let m = first; m <= last; m++) {
      if (xmax + 360 * m >= v0 && xmin + 360 * m <= v1) turns.push(m);
    }
    return turns;
  }

  /** How many CSS pixels one cell spans across. */
  cellPx() {
    return this.world ? this.world.cellX * this.k : 0;
  }

  /** Marker size for this zoom: `base` at the whole-world view, growing gently as you zoom. */
  markerSize(base) {
    return base * Math.min(2.4, Math.max(1, Math.sqrt(this.k / this.fitK)));
  }

  // --- drawing ------------------------------------------------------------------------

  draw() {
    const started = performance.now();
    const ctx = this.ctx;
    const dpr = window.devicePixelRatio || 1;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, this.width, this.height);
    if (!this.world) return;
    const { cellX, cellY } = this.world;
    if (this.base) {
      ctx.imageSmoothingEnabled = false;
      const x0 = -cellX / 2;
      for (const m of this.wholeWorldFits() ? [0] : this.turnsFor(x0, x0 + 360)) {
        const [sx, sy] = this.toScreen(x0 + 360 * m, -cellY / 2);
        ctx.drawImage(this.base, sx, sy, 360 * this.k, (180 + cellY) * this.k);
      }
    }
    // Overlays stay on the painted world: a road crossing the seam is drawn from both sides,
    // and without the clip its far half would hang off the edge of a world that fits.
    const [w, h, x0, y0] = this.extent();
    const clipLeft = this.wholeWorldFits() ? x0 * this.k + this.tx : 0;
    const clipWidth = this.wholeWorldFits() ? w * this.k : this.width;
    for (const overlay of this.overlays) {
      ctx.save();
      ctx.beginPath();
      ctx.rect(clipLeft, y0 * this.k + this.ty - 40, clipWidth, h * this.k + 80);
      ctx.clip();
      try {
        overlay.draw(ctx, this);
      } catch (error) {
        console.error(`overlay ${overlay.id}:`, error);
      }
      ctx.restore();
    }
    this.lastDrawMs = performance.now() - started;
  }

  /**
   * Every overlay item within reach of a screen point: the topmost overlay's first, each
   * overlay's nearest first. Markers share cells (a band camps by a hamlet), so the first is
   * what a click selects and the rest are what else stands there.
   */
  hitTest(sx, sy) {
    const hits = [];
    for (let i = this.overlays.length - 1; i >= 0; i--) {
      const overlay = this.overlays[i];
      if (overlay.hit) hits.push(...overlay.hit(this, sx, sy));
    }
    return hits;
  }

  // --- input --------------------------------------------------------------------------

  bindEvents() {
    const canvas = this.canvas;
    let press = null;

    canvas.addEventListener('pointerdown', event => {
      if (event.button !== 0) return;
      press = { x: event.clientX, y: event.clientY, tx: this.tx, ty: this.ty, moved: false };
      canvas.setPointerCapture(event.pointerId);
    });

    canvas.addEventListener('pointermove', event => {
      const rect = canvas.getBoundingClientRect();
      const sx = event.clientX - rect.left;
      const sy = event.clientY - rect.top;
      if (press) {
        const dx = event.clientX - press.x;
        const dy = event.clientY - press.y;
        if (!press.moved && Math.hypot(dx, dy) > CLICK_SLOP) {
          press.moved = true;
          canvas.classList.add('dragging');
        }
        if (press.moved) {
          this.tx = press.tx + dx;
          this.ty = press.ty + dy;
          this.clampPan();
          this.requestRender();
          return;
        }
      }
      this.hover(sx, sy);
    });

    const release = event => {
      if (!press) return;
      const wasClick = !press.moved;
      press = null;
      canvas.classList.remove('dragging');
      if (wasClick && event.type === 'pointerup') {
        const rect = canvas.getBoundingClientRect();
        this.click(event.clientX - rect.left, event.clientY - rect.top);
      }
    };
    canvas.addEventListener('pointerup', release);
    canvas.addEventListener('pointercancel', release);
    canvas.addEventListener('pointerleave', () => this.onHover(null));

    canvas.addEventListener('wheel', event => {
      event.preventDefault();
      const rect = canvas.getBoundingClientRect();
      const factor = Math.exp(-event.deltaY * (event.deltaMode === 1 ? 0.05 : 0.0016));
      this.zoomAt(event.clientX - rect.left, event.clientY - rect.top, factor);
    }, { passive: false });

    canvas.addEventListener('dblclick', event => {
      const rect = canvas.getBoundingClientRect();
      this.zoomAt(event.clientX - rect.left, event.clientY - rect.top, event.shiftKey ? 0.5 : 2);
    });

    this.hover = throttleFrame((sx, sy) => {
      if (!this.world) return;
      const [X, Y] = this.toMap(sx, sy);
      const inside = Y >= -this.world.cellY / 2 && Y <= 180 + this.world.cellY / 2;
      const [x, z] = this.world.cellAt(X, Y);
      this.onHover(inside ? { x, z, hits: this.hitTest(sx, sy) } : null);
    });
  }

  click(sx, sy) {
    if (!this.world) return;
    const hits = this.hitTest(sx, sy);
    const [X, Y] = this.toMap(sx, sy);
    if (Y < -this.world.cellY / 2 || Y > 180 + this.world.cellY / 2) return;
    const [x, z] = this.world.cellAt(X, Y);
    this.onClick({ x, z, hits });
  }
}

/**
 * Paint a base image: `colorizer.rgb(value, index)` per cell, times the relief-shading
 * multiplier if one is given. Drops the seam column, so the image is (cols - 1) x rows.
 */
export function paintBase(layer, colorizer, shade) {
  const width = layer.cols - 1;
  const rows = layer.rows;
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = rows;
  const ctx = canvas.getContext('2d');
  const image = ctx.createImageData(width, rows);
  const data = image.data;
  const values = layer.values;
  for (let z = 0; z < rows; z++) {
    for (let x = 0; x < width; x++) {
      const i = z * layer.cols + x;
      const color = colorizer.rgb(values[i], i);
      const o = (z * width + x) * 4;
      if (shade) {
        const s = shade[z * width + x];
        data[o] = color[0] * s;
        data[o + 1] = color[1] * s;
        data[o + 2] = color[2] * s;
      } else {
        data[o] = color[0];
        data[o + 1] = color[1];
        data[o + 2] = color[2];
      }
      data[o + 3] = 255;
    }
  }
  ctx.putImageData(image, 0, 0);
  return canvas;
}
