// The settlement plan view: a dialog with the plan in 2D (SVG) or 3D (WebGL), a placement-
// pass slider, a building list and the details of whatever is selected. A plan is fetched
// only when the view opens on it.
//
// The same dialog shows a local patch of ground (`fwg patch`, through /api/patch) in 3D:
// the ground in metres around one point, with a figure for scale.

import { api } from './api.js';
import { el, latLon, num, seconds, svg, titleCase } from './dom.js';
import { buildPatchGeometry, createPlan3D, planBounds } from './city-3d.js';
import {
  PLOT_COLORS, PLOT_LEGEND, magicColorAt, placementPasses, planSummary, plotColor, plotsThrough, terrainPalette,
  wallGates, wallRings, wallSegments, wallTowers,
} from './city-plan.js';

const PASS_LABELS = {
  fortification: 'Walls and gates', high: 'High-priority buildings', high_housing: 'Their housing', low: 'Low-priority buildings',
  low_housing: 'Their housing', perimeter: 'Walls and gates', courts: 'Bailey courts', landmarks: 'Keep and landmarks', services: 'Bailey services',
};

export class CityView {
  constructor(dialog) {
    this.dialog = dialog;
    this.plan = null;
    this.shown = [];
    this.selected = null;
    this.view = '2d';
    this.renderer = null;
    this.rendererTried = false;
    this.loadToken = 0;
    this.build();
  }

  build() {
    const d = this.dialog;
    this.title = el('h2', {}, 'Settlement plan');
    this.viewSelect = el('select', {}, el('option', { value: '2d' }, '2D plan'), el('option', { value: '3d' }, '3D view'));
    this.passSelect = el('select', {});
    this.labels = el('input', { type: 'checkbox' });
    this.buildingSelect = el('select', { style: { maxWidth: '240px' } });
    const close = el('button', { type: 'button', onclick: () => d.close() }, 'Close');
    const reset = el('button', { type: 'button', onclick: () => this.resetView() }, 'Reset view');
    this.stage = el('div', { class: 'city-stage' });
    this.hint = el('div', { class: 'hint' });
    this.side = el('div', { class: 'city-side' });
    this.summaryLine = el('p', { class: 'note' });
    this.planBar = el('div', { class: 'city-toolbar' },
      el('label', { class: 'field' }, 'View ', this.viewSelect),
      el('label', { class: 'field' }, 'Placement pass ', this.passSelect),
      el('label', { class: 'check' }, this.labels, ' Labels'),
      el('label', { class: 'field' }, 'Building ', this.buildingSelect),
      reset,
      this.summaryLine);
    this.patchBar = this.buildPatchBar();
    d.append(
      el('div', { class: 'city-head' }, this.title, close),
      this.planBar,
      this.patchBar,
      el('div', { class: 'city-body' }, this.stage, this.side),
    );
    this.viewSelect.addEventListener('change', () => {
      this.view = this.viewSelect.value;
      this.renderStage();
    });
    this.passSelect.addEventListener('change', () => this.refresh());
    this.labels.addEventListener('change', () => {
      if (this.view === '3d') this.renderer?.setLabels(this.labels.checked);
      else this.draw2d();
    });
    this.buildingSelect.addEventListener('change', () => {
      const plot = this.shown.find(b => b.id === this.buildingSelect.value);
      if (plot) this.select(plot);
    });
    d.addEventListener('close', () => {
      this.loadToken++;
    });
  }

  /** Open on a plan: `load` is a promise of the plan document. */
  async open(load, heading) {
    const token = ++this.loadToken;
    this.planBar.hidden = false;
    this.patchBar.hidden = true;
    this.title.textContent = heading;
    this.plan = null;
    this.stage.replaceChildren(el('div', { class: 'city-loading' }, 'Loading the plan…'));
    this.side.replaceChildren();
    this.summaryLine.textContent = '';
    if (!this.dialog.open) this.dialog.showModal();
    let plan;
    const started = performance.now();
    try {
      plan = await load;
    } catch (error) {
      if (token === this.loadToken) this.stage.replaceChildren(el('div', { class: 'city-loading bad' }, `Could not load the plan: ${error.message}`));
      return;
    }
    if (token !== this.loadToken) return;
    this.loadMs = performance.now() - started;
    this.plan = plan;
    this.selected = null;
    const passes = placementPasses(plan);
    this.passSelect.replaceChildren(...passes.map((id, i) => el('option', { value: id }, `${i + 1}. ${PASS_LABELS[id] ?? titleCase(id)}`)));
    this.passSelect.disabled = !passes.length;
    if (passes.length) this.passSelect.value = passes[passes.length - 1];
    this.viewBox = null;
    this.refresh();
  }

  refresh() {
    if (!this.plan) return;
    this.shown = plotsThrough(this.plan, this.passSelect.value || null);
    this.buildingSelect.replaceChildren(el('option', { value: '' }, `${this.shown.length} plots…`),
      ...[...this.shown].sort((a, b) => a.name.localeCompare(b.name)).map(b => el('option', { value: b.id }, `${b.name} (${b.id})`)));
    this.summaryLine.textContent = planSummary(this.plan, this.shown);
    this.renderSide();
    this.renderStage();
  }

  select(plot) {
    this.selected = plot?.id ?? null;
    this.buildingSelect.value = this.selected ?? '';
    if (this.view === '3d') this.renderer?.select(this.selected);
    else this.draw2d();
    this.renderSide();
  }

  resetView() {
    this.viewBox = null;
    if (this.view === '3d') this.renderer?.reset();
    else this.draw2d();
  }

  // The WebGL view, made on first use; null (with the reason on the stage) if unavailable.
  ensureRenderer(fallback) {
    if (!this.rendererTried) {
      this.rendererTried = true;
      try {
        this.renderer = createPlan3D(el('div'), plot => this.select(plot));
      } catch (error) {
        console.warn('3D view unavailable', error);
        this.renderer = null;
      }
    }
    if (!this.renderer) {
      this.stage.replaceChildren(el('div', { class: 'city-loading' }, `WebGL is not available in this browser, so there is no 3D view.${fallback ? ` ${fallback}` : ''}`));
    }
    return this.renderer;
  }

  renderStage() {
    if (!this.plan) return;
    if (this.view === '3d') {
      if (!this.ensureRenderer('The 2D plan still works.')) return;
      this.stage.replaceChildren(this.renderer.element, this.hint);
      this.hint.textContent = 'Drag to orbit · shift-drag or right-drag to pan · scroll to zoom · click a building';
      const started = performance.now();
      const { triangles } = this.renderer.setScene(this.plan, this.shown);
      this.renderer.setLabels(this.labels.checked);
      this.renderer.select(this.selected);
      this.hint.textContent += ` · ${num(triangles)} triangles, built in ${Math.round(performance.now() - started)} ms`;
    } else {
      this.draw2d();
    }
  }

  // --- 2D ------------------------------------------------------------------------------

  draw2d() {
    const plan = this.plan;
    if (!plan) return;
    const [x0, z0, x1, z1] = planBounds(plan);
    const span = Math.max(x1 - x0, z1 - z0);
    // The drawing is in plan metres inside a group flipped vertically, so north is up.
    this.viewBox ??= [x0, -z1, span, span];
    const root = svg('svg', { viewBox: this.viewBox.join(' '), role: 'img', 'aria-label': 'Settlement plan' });
    const world = svg('g', { transform: 'scale(1,-1)' });
    root.append(world);

    world.append(this.terrainImage(plan, x0, z0, x1, z1));
    this.drawMagicEdges(world, plan, x0, z0);

    const pathOf = points => points.map(([x, z], i) => `${i ? 'L' : 'M'}${x} ${z}`).join('');
    for (const street of plan.street_paths_m ?? []) {
      world.append(svg('path', { d: pathOf(street), fill: 'none', stroke: '#c9ccc0', 'stroke-width': 2.2, 'stroke-linecap': 'round', 'stroke-linejoin': 'round', opacity: 0.85 }));
    }
    for (const c of plan.road_connections ?? []) {
      if (c.status !== 'connected' || !c.local_path_m) continue;
      world.append(svg('path', { d: pathOf(c.local_path_m), fill: 'none', stroke: '#edb877', 'stroke-width': 4, 'stroke-linecap': 'round', opacity: 0.9 }));
    }

    // Walls: measured segments where the plan has them, ring polylines otherwise.
    const segments = wallSegments(plan);
    if (segments.length) {
      for (const seg of segments) {
        const [ax, az] = seg.from_m;
        const [bx, bz] = seg.to_m;
        const length = Math.hypot(bx - ax, bz - az) || 1;
        const t = (seg.thickness_m ?? 3) / 2;
        const nx = (-(bz - az) / length) * t;
        const nz = ((bx - ax) / length) * t;
        world.append(svg('polygon', {
          points: [[ax + nx, az + nz], [bx + nx, bz + nz], [bx - nx, bz - nz], [ax - nx, az - nz]].map(p => p.join(',')).join(' '),
          fill: '#6d4c30', stroke: '#2a1a0c', 'stroke-width': 0.4,
        }));
      }
    } else {
      for (const ring of wallRings(plan)) {
        const pts = ring.polyline_m ?? [];
        if (pts.length > 1) world.append(svg('path', { d: `${pathOf(pts)}Z`, fill: 'none', stroke: '#6d4c30', 'stroke-width': 3 }));
      }
    }
    for (const tower of wallTowers(plan)) {
      const [x, z] = tower.position_m;
      world.append(svg('rect', { x: x - 2.5, y: z - 2.5, width: 5, height: 5, fill: '#8a6a3e', stroke: '#2a1a0c', 'stroke-width': 0.5 }));
    }
    for (const gate of wallGates(plan)) {
      const [x, z] = gate.position_m;
      world.append(svg('circle', { cx: x, cy: z, r: 3.5, fill: '#d4b262', stroke: '#2a1a0c', 'stroke-width': 0.6 }));
    }

    // Plots: the plot (setbacks included) filled, the building footprint outlined.
    for (const b of this.shown) {
      const color = plotColor(b);
      const selected = b.id === this.selected;
      const group = svg('g', {
        transform: `rotate(${b.rotation_degrees ?? 0} ${b.x_m} ${b.z_m})`, tabindex: 0, role: 'button', 'aria-label': `${b.name} ${b.id}`,
        style: 'cursor:pointer',
        onclick: event => {
          event.stopPropagation();
          this.select(b);
        },
        onkeydown: event => {
          if (event.key === 'Enter' || event.key === ' ') {
            event.preventDefault();
            this.select(b);
          }
        },
      });
      group.append(
        svg('rect', {
          x: b.x_m - b.plot_m.width / 2, y: b.z_m - b.plot_m.depth / 2, width: b.plot_m.width, height: b.plot_m.depth,
          fill: `rgb(${color.join(',')})`, 'fill-opacity': 0.8, stroke: selected ? '#ffffff' : '#101b20', 'stroke-width': selected ? 1.6 : 0.5,
        }),
        svg('rect', {
          x: b.x_m - b.dimensions_m.width / 2, y: b.z_m - b.dimensions_m.depth / 2, width: b.dimensions_m.width, height: b.dimensions_m.depth,
          fill: 'none', stroke: '#fbfbf6', 'stroke-width': 0.4,
        }),
      );
      world.append(group);
    }

    // Labels, upright (counter-flipped), unrotated so they stay readable.
    const scale = this.viewBox[2] / (this.stage.clientWidth || 800);
    if (this.labels.checked || this.selected) {
      for (const b of this.shown) {
        if (!this.labels.checked && b.id !== this.selected) continue;
        if (this.labels.checked && b.kind === 'housing' && b.id !== this.selected) continue;
        const size = Math.max(2.2, 11 * scale);
        world.append(svg('text', {
          transform: `translate(${b.x_m} ${b.z_m}) scale(1,-1)`, 'font-size': size, 'text-anchor': 'middle', 'dominant-baseline': 'middle',
          fill: '#101b20', stroke: '#f8f4e8', 'stroke-width': size * 0.25, 'paint-order': 'stroke', style: 'pointer-events:none',
        }, b.name));
      }
    }
    root.addEventListener('click', () => this.select(null));
    this.enablePanZoom(root);
    this.stage.replaceChildren(root, this.hint);
    this.hint.textContent = `North is up · drag to pan · scroll to zoom · ${num(span)} m across · terrain cells ${plan.terrain?.cell_m ?? '—'} m · loaded in ${Math.round(this.loadMs ?? 0)} ms`;
  }

  // The terrain raster as an image, shaded by the corner-sampled surface. Encoded once per
  // plan: selecting a building redraws the plan, not the ground.
  terrainImage(plan, x0, z0, x1, z1) {
    if (this.terrainCache?.plan === plan) return this.terrainCache.node.cloneNode();
    const node = this.paintTerrain(plan, x0, z0, x1, z1);
    this.terrainCache = { plan, node };
    return node.cloneNode();
  }

  paintTerrain(plan, x0, z0, x1, z1) {
    const palette = terrainPalette(plan);
    const surface = plan.terrain?.surface;
    const n = palette.hasGrid ? palette.size : Math.max(1, (surface?.size ?? 2) - 1);
    const cell = palette.hasGrid ? palette.cell : (x1 - x0) / n;
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = n;
    const ctx = canvas.getContext('2d');
    const image = ctx.createImageData(n, n);
    const h = surface?.heights_m;
    for (let j = 0; j < n; j++) {
      for (let i = 0; i < n; i++) {
        const color = palette.hasGrid ? palette.cellColor(i, j) : palette.at(x0 + (i + 0.5) * cell, z0 + (j + 0.5) * cell);
        let shade = 1;
        if (h?.[j + 1]?.[i + 1] !== undefined) {
          // slope toward the light (north-west) from the cell's corners
          const dzdx = (h[j][i + 1] + h[j + 1][i + 1] - h[j][i] - h[j + 1][i]) / (2 * cell);
          const dzdn = (h[j + 1][i] + h[j + 1][i + 1] - h[j][i] - h[j][i + 1]) / (2 * cell);
          shade = Math.max(0.55, Math.min(1.35, 1 + 1.4 * (-dzdx * -0.5 - dzdn * 0.5)));
        }
        // image rows run top-down; row j is drawn at z0 + j*cell, flipped by the group
        const o = (j * n + i) * 4;
        image.data[o] = color[0] * shade;
        image.data[o + 1] = color[1] * shade;
        image.data[o + 2] = color[2] * shade;
        image.data[o + 3] = 255;
      }
    }
    ctx.putImageData(image, 0, 0);
    return svg('image', {
      href: canvas.toDataURL(), x: x0, y: z0, width: n * cell, height: n * cell, preserveAspectRatio: 'none', style: 'image-rendering:pixelated',
    });
  }

  // Outline the edges between magical and other ground in the school's colour.
  drawMagicEdges(group, plan, x0, z0) {
    const t = plan.terrain;
    if (!t?.biome_variant?.length) return;
    const cell = t.cell_m;
    const paths = new Map();
    for (let j = 0; j < t.size; j++) {
      for (let i = 0; i < t.size; i++) {
        const color = magicColorAt(plan, i, j);
        if (!color) continue;
        const v = t.biome_variant[j][i];
        const a = x0 + i * cell;
        const b = z0 + j * cell;
        const edges = [[0, -1, a, b, a + cell, b], [1, 0, a + cell, b, a + cell, b + cell], [0, 1, a + cell, b + cell, a, b + cell], [-1, 0, a, b + cell, a, b]];
        const key = color.join(',');
        for (const [di, dj, ax, az, bx, bz] of edges) {
          if (t.biome_variant[j + dj]?.[i + di] === v) continue;
          paths.set(key, (paths.get(key) ?? '') + `M${ax} ${az}L${bx} ${bz}`);
        }
      }
    }
    for (const [key, d] of paths) group.append(svg('path', { d, fill: 'none', stroke: `rgb(${key})`, 'stroke-width': 1.2, opacity: 0.9 }));
  }

  enablePanZoom(root) {
    let press = null;
    const toUnits = (dx, dy) => {
      const rect = root.getBoundingClientRect();
      const scale = this.viewBox[2] / Math.min(rect.width, rect.height);
      return [dx * scale, dy * scale];
    };
    root.addEventListener('pointerdown', event => {
      press = { x: event.clientX, y: event.clientY, box: [...this.viewBox], moved: false };
    });
    root.addEventListener('pointermove', event => {
      if (!press) return;
      const dx = event.clientX - press.x;
      const dy = event.clientY - press.y;
      if (!press.moved && Math.hypot(dx, dy) < 4) return;
      if (!press.moved) {
        press.moved = true;
        root.setPointerCapture(event.pointerId);
        root.classList.add('dragging');
      }
      const [ux, uy] = toUnits(dx, dy);
      this.viewBox = [press.box[0] - ux, press.box[1] - uy, press.box[2], press.box[3]];
      root.setAttribute('viewBox', this.viewBox.join(' '));
    });
    const release = event => {
      if (press?.moved) {
        event.stopPropagation();
        root.classList.remove('dragging');
        // swallow the click that ends a drag
        root.addEventListener('click', e => e.stopPropagation(), { capture: true, once: true });
      }
      press = null;
    };
    root.addEventListener('pointerup', release);
    root.addEventListener('pointercancel', release);
    root.addEventListener('wheel', event => {
      event.preventDefault();
      const rect = root.getBoundingClientRect();
      const size = Math.min(rect.width, rect.height);
      // the point under the cursor, in viewBox units
      const offsetX = (rect.width - size) / 2;
      const offsetY = (rect.height - size) / 2;
      const [bx, by, bw, bh] = this.viewBox;
      const fx = (event.clientX - rect.left - offsetX) / size;
      const fy = (event.clientY - rect.top - offsetY) / size;
      const factor = Math.exp(event.deltaY * 0.0015);
      const [x0, , x1] = planBounds(this.plan);
      const w = Math.max((x1 - x0) / 40, Math.min((x1 - x0) * 3, bw * factor));
      const h = (bh * w) / bw;
      const px = bx + fx * bw;
      const py = by + fy * bh;
      this.viewBox = [px - fx * w, py - fy * h, w, h];
      root.setAttribute('viewBox', this.viewBox.join(' '));
    }, { passive: false });
    root.addEventListener('wheel', () => {
      clearTimeout(this.relabel);
      if (this.labels.checked || this.selected) this.relabel = setTimeout(() => this.draw2d(), 250);
    });
  }

  // --- side panel ------------------------------------------------------------------------

  renderSide() {
    const plan = this.plan;
    const side = this.side;
    side.replaceChildren();
    const plot = this.shown.find(b => b.id === this.selected);
    side.append(el('h3', {}, 'Selected'));
    if (plot) {
      side.append(el('table', { class: 'facts' },
        [
          ['Name', plot.name], ['Building', plot.building_id], ['Kind', plot.kind], ['Pass', plot.phase],
          ['Workers', plot.workers], ['Beds', plot.beds],
          ['Size (w × d × h)', `${num(plot.dimensions_m.width)} × ${num(plot.dimensions_m.depth)} × ${num(plot.dimensions_m.height)} m`],
          ['Plot', `${num(plot.plot_m.width)} × ${num(plot.plot_m.depth)} m`],
          ['Rotation', `${num(plot.rotation_degrees ?? 0)}°`],
          ['Floor', plot.ground_elevation_m !== undefined ? `${num(plot.ground_elevation_m, 1)} m` : undefined],
          ['Foundation', plot.foundation_bottom_m !== undefined ? `${num(plot.foundation_bottom_m, 1)} m` : undefined],
          ['Position', `${num(plot.x_m, 1)}, ${num(plot.z_m, 1)} m`],
          ['Upgraded', plot.housing_upgrade ? `in ${plot.housing_upgrade.phase}` : undefined],
          ['id', plot.id],
        ].filter(([, v]) => v !== undefined && v !== null).map(([k, v]) => el('tr', {}, el('th', {}, k), el('td', {}, String(v))))));
    } else {
      side.append(el('p', { class: 'note' }, 'Click a building, or pick one from the list.'));
    }

    side.append(el('h3', {}, 'Key'), el('div', { class: 'city-key' },
      PLOT_LEGEND.filter(([cat]) => this.shown.some(b => PLOT_COLORS[cat] === plotColor(b)))
        .map(([cat, label]) => el('span', {}, el('span', { class: 'chip', style: { background: `rgb(${PLOT_COLORS[cat].join(',')})` } }), label)),
      el('span', {}, el('span', { class: 'chip', style: { background: '#6d4c30' } }), 'walls'),
      el('span', {}, el('span', { class: 'chip', style: { background: '#d4b262' } }), 'gates'),
      el('span', {}, el('span', { class: 'chip', style: { background: '#c9ccc0' } }), 'streets'),
      el('span', {}, el('span', { class: 'chip', style: { background: '#edb877' } }), 'road to the world')));

    const s = plan.stats ?? {};
    side.append(el('h3', {}, 'Plan'), el('table', { class: 'facts' },
      [
        ['Status', plan.status], ['Version', plan.version], ['Shape', plan.shape?.shape_id], ['Kit', plan.kit_name ?? plan.kit_id],
        ['Workers', s.workers], ['Worker beds', s.worker_beds], ['Houses', s.houses], ['Apartments', s.apartments],
        ['Housing shortfall', s.housing_shortfall], ['Services', s.service_buildings], ['Unplaced', s.unplaced_total],
        ['Urban population', s.urban_population_estimate], ['Wall segments', wallSegments(plan).length || undefined],
        ['Regional sampling', plan.source_resolution_m !== undefined ? `${num(plan.source_resolution_m)} m` : undefined],
        ['Terrain cells', plan.terrain?.cell_m !== undefined ? `${plan.terrain.cell_m} m, ${plan.terrain.size} × ${plan.terrain.size}` : undefined],
      ].filter(([, v]) => v !== undefined && v !== null).map(([k, v]) => el('tr', {}, el('th', {}, k), el('td', {}, typeof v === 'number' ? num(v) : String(v))))));

    const unplaced = plan.unplaced ?? [];
    const warnings = plan.warnings ?? [];
    if (unplaced.length || warnings.length) {
      side.append(el('h3', {}, `Unplaced and warnings (${unplaced.length + warnings.length})`),
        el('ul', {}, unplaced.map(u => el('li', {}, `${u.count ?? 1} × ${u.building_id}: ${u.reason}`)), warnings.map(w => el('li', { class: 'warn' }, w))));
    }
    const fortUnplaced = plan.fortifications?.unplaced ?? [];
    if (fortUnplaced.length) side.append(el('ul', {}, fortUnplaced.map(u => el('li', {}, `${u.building_id}: ${u.reason}`))));
  }

  // --- local patch --------------------------------------------------------------------

  // Its bounds are fwg's (patch.hpp); the inputs only stop the obvious typo.
  buildPatchBar() {
    const number = (name, value, min, max, step) => el('input', { type: 'number', name, value, min, max, step, required: true, style: { width: '90px' } });
    this.patchInputs = {
      latitude: number('latitude', 0, -90, 90, 'any'),
      longitude: number('longitude', 0, -180, 180, 'any'),
      span: number('span', 64, 4, 512, 'any'),
      spacing: number('spacing', 0.5, 0.1, 8, 'any'),
      detail_height: number('detail_height', 0.4, 0, 5, 'any'),
      detail_scale: number('detail_scale', 8, 0.5, 100, 'any'),
    };
    this.legacyFields = el('span', { style: { display: 'contents' } },
      el('label', { class: 'field', title: 'Legacy worlds only: micro relief the patch adds' }, 'Detail height m ', this.patchInputs.detail_height),
      el('label', { class: 'field', title: 'Legacy worlds only' }, 'Detail wavelength m ', this.patchInputs.detail_scale));
    this.cutButton = el('button', { type: 'submit', class: 'primary' }, 'Cut patch');
    this.patchStatus = el('span', { class: 'note' });
    const form = el('form', { class: 'city-toolbar', onsubmit: event => {
      event.preventDefault();
      this.cutPatch();
    } },
    el('label', { class: 'field' }, 'Latitude ', this.patchInputs.latitude),
    el('label', { class: 'field' }, 'Longitude ', this.patchInputs.longitude),
    el('label', { class: 'field', title: '4 to 512 m' }, 'Span m ', this.patchInputs.span),
    el('label', { class: 'field', title: '0.1 to 8 m; at most 256 steps across' }, 'Spacing m ', this.patchInputs.spacing),
    this.legacyFields,
    this.cutButton,
    this.patchStatus);
    form.hidden = true;
    return form;
  }

  /**
   * Open on a local patch of `world` around a point, and cut it. `legacy` worlds (no
   * published terrain detail) take the micro-relief inputs; canonical ones ignore them.
   */
  openPatch({ world, latitude, longitude, title }) {
    this.patchWorld = world;
    this.planBar.hidden = true;
    this.patchBar.hidden = false;
    this.legacyFields.hidden = world.has('terrain_detail');
    this.patchInputs.latitude.value = latitude.toFixed(4);
    this.patchInputs.longitude.value = longitude.toFixed(4);
    this.title.textContent = title ?? 'Local patch';
    this.plan = null;
    if (!this.dialog.open) this.dialog.showModal();
    this.cutPatch();
  }

  async cutPatch() {
    const inputs = Object.values(this.patchInputs).filter(input => !this.legacyFields.contains(input) || !this.legacyFields.hidden);
    for (const input of inputs) if (!input.reportValidity()) return;
    const token = ++this.loadToken;
    const body = { world: this.patchWorld.id };
    for (const [name, input] of Object.entries(this.patchInputs)) {
      if (this.legacyFields.hidden && (name === 'detail_height' || name === 'detail_scale')) continue;
      body[name] = Number(input.value);
    }
    const started = performance.now();
    const waiting = el('div', { class: 'city-loading' });
    this.stage.replaceChildren(waiting);
    this.side.replaceChildren(el('p', { class: 'note' }, 'fwg patch regenerates the world from the request it was made with, then cuts the ground from it, so this takes about as long as generating the world did.'));
    this.cutButton.disabled = true;
    const tick = setInterval(() => {
      waiting.textContent = `Regenerating the world and cutting the patch… ${((performance.now() - started) / 1000).toFixed(1)} s`;
    }, 100);
    let patch;
    try {
      patch = await api.patch(body);
    } catch (error) {
      if (token === this.loadToken) this.stage.replaceChildren(el('div', { class: 'city-loading bad' }, error.message));
      return;
    } finally {
      clearInterval(tick);
      this.cutButton.disabled = false;
    }
    if (token !== this.loadToken) return;
    const elapsed = performance.now() - started;
    this.patchStatus.textContent = `${patch.size} × ${patch.size} vertices in ${seconds(elapsed)}`;
    if (!this.ensureRenderer()) return;
    this.stage.replaceChildren(this.renderer.element, this.hint);
    const seaLevel = this.patchWorld.config.sea_level ?? 0;
    const { triangles } = this.renderer.setMesh(buildPatchGeometry(patch, seaLevel), JSON.stringify(body));
    this.hint.textContent = `Drag to orbit · shift-drag to pan · scroll to zoom · ${num(triangles)} triangles · no vertical exaggeration · the orange post is 1.8 m tall`;
    this.renderPatchSide(patch, elapsed);
  }

  renderPatchSide(patch, elapsed) {
    const range = values => {
      let lo = Infinity;
      let hi = -Infinity;
      for (const v of values) {
        if (v < lo) lo = v;
        if (v > hi) hi = v;
      }
      return [lo, hi];
    };
    const [hLo, hHi] = range(patch.height);
    const [dLo, dHi] = range(patch.detail);
    const [yLo, yHi] = range(patch.local_y);
    const canonical = patch.patch_version === 2;
    const rows = [
      ['Where', latLon(patch.patch_config.latitude, patch.patch_config.longitude)],
      ['Vertices', `${patch.size} × ${patch.size} = ${num(patch.size * patch.size)}`],
      ['Spacing', `${num(patch.spacing_m, 3)} m (asked ${patch.patch_config.spacing})`],
      ['Span', `${num(patch.patch_config.span)} m`],
      ['Surface', canonical ? 'canonical: the height field the plans are built on' : 'legacy: the coarse layer plus this patch\'s own micro relief'],
      ['Elevation', `${num(hLo, 2)} to ${num(hHi, 2)} m`],
      ['At the centre', `${num(patch.origin_height_m, 2)} m${patch.origin_height_m < (this.patchWorld.config.sea_level ?? 0) ? ', below sea level' : ''}`],
      ['Relief in the patch', `${num(yHi - yLo, 2)} m, curvature included`],
      ['Detail above the coarse base', `${num(dLo, 3)} to ${num(dHi, 3)} m`],
      ['Detail resolved', patch.detail_resolved ? 'yes' : 'no: its wavelength is under two samples'],
      ['World', `seed ${patch.world.seed}, size ${patch.world.size}, phase ${patch.world.phase}`],
      ['Cut in', seconds(elapsed)],
    ];
    this.side.replaceChildren(
      el('h3', {}, 'Patch'),
      el('table', { class: 'facts' }, rows.map(([k, v]) => el('tr', {}, el('th', {}, k), el('td', {}, v)))),
      el('h3', {}, `Warnings (${patch.warnings?.length ?? 0})`),
      el('ul', {}, (patch.warnings ?? []).map(w => el('li', { class: 'warn' }, w))),
      el('p', { class: 'note' }, patch.coordinates),
    );
  }
}
