// A settlement plan in 3D -- the plan's terrain surface as a mesh, its walls and its plots
// as boxes -- and a local patch of ground, all in metres with no exaggeration. Plain WebGL 1
// and a few lines of matrix code; no library.
//
// Frame: a plan is local metres, x east and z north, heights radial metres above the
// reference sphere; a patch is its own tangent frame, x east, y up, z north. The renderer
// uses x east, y up and -z north (so it stays right-handed and nothing is mirrored), with
// heights measured from the ground at the centre.

import { plotColor, terrainPalette, wallSegments } from './city-plan.js';

// --- matrices (column-major, as WebGL wants them) ---------------------------------

function perspective(fovY, aspect, near, far) {
  const f = 1 / Math.tan(fovY / 2);
  const nf = 1 / (near - far);
  return new Float32Array([f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0]);
}

function lookAt(eye, target, up) {
  const zx = eye[0] - target[0];
  const zy = eye[1] - target[1];
  const zz = eye[2] - target[2];
  let len = Math.hypot(zx, zy, zz) || 1;
  const z = [zx / len, zy / len, zz / len];
  let x = [up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]];
  len = Math.hypot(...x) || 1;
  x = x.map(v => v / len);
  const y = [z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]];
  return new Float32Array([
    x[0], y[0], z[0], 0,
    x[1], y[1], z[1], 0,
    x[2], y[2], z[2], 0,
    -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]),
    -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]),
    -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]),
    1,
  ]);
}

function multiply(a, b) {
  const out = new Float32Array(16);
  for (let col = 0; col < 4; col++) {
    for (let row = 0; row < 4; row++) {
      let sum = 0;
      for (let k = 0; k < 4; k++) sum += a[k * 4 + row] * b[col * 4 + k];
      out[col * 4 + row] = sum;
    }
  }
  return out;
}

/** Clip-space transform of a point: [screen x, screen y, depth w] for a canvas w x h. */
function project(m, p, width, height) {
  const x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
  const y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
  const w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
  return [((x / w) * 0.5 + 0.5) * width, (1 - ((y / w) * 0.5 + 0.5)) * height, w];
}

// --- geometry ----------------------------------------------------------------------

/** Bilinear ground height (metres) at plan (x, z), from terrain.surface. */
export function heightAt(plan, x, z) {
  const surface = plan.terrain?.surface;
  if (!surface?.heights_m?.length) return 0;
  const n = surface.size ?? surface.heights_m.length;
  const lo = planBounds(plan)[0];
  const step = surface.step_m;
  const gx = Math.max(0, Math.min(n - 1, (x - lo) / step));
  const gz = Math.max(0, Math.min(n - 1, (z - lo) / step));
  const i = Math.min(n - 2, Math.floor(gx));
  const j = Math.min(n - 2, Math.floor(gz));
  const u = gx - i;
  const v = gz - j;
  const h = surface.heights_m;
  return (h[j][i] * (1 - u) + h[j][i + 1] * u) * (1 - v) + (h[j + 1][i] * (1 - u) + h[j + 1][i + 1] * u) * v;
}

/** [x0, z0, x1, z1] in metres; key-location plans keep theirs on the terrain. */
export function planBounds(plan) {
  if (Array.isArray(plan.bounds_m)) return plan.bounds_m;
  if (Array.isArray(plan.terrain?.bounds_m)) return plan.terrain.bounds_m;
  const w = plan.bounds_m?.width ?? 100;
  const d = plan.bounds_m?.depth ?? 100;
  return [-w / 2, -d / 2, w / 2, d / 2];
}

// The 8 corners of a box footprint rotated about its centre, bottom then top. Rotation is
// the plan's: x' = x cos a - z sin a, z' = x sin a + z cos a, in plan metres.
function boxCorners(b, bottom, top) {
  const a = ((b.rotation_degrees ?? 0) * Math.PI) / 180;
  const c = Math.cos(a);
  const s = Math.sin(a);
  const w = b.dimensions_m.width / 2;
  const d = b.dimensions_m.depth / 2;
  const out = [];
  for (const y of [bottom, top]) {
    for (const [x, z] of [[-w, -d], [w, -d], [w, d], [-w, d]]) out.push([b.x_m + x * c - z * s, y, b.z_m + x * s + z * c]);
  }
  return out;
}

const FACES = [[0, 3, 2, 1], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5], [2, 3, 7, 6], [3, 0, 4, 7]];

// A wall segment as a box whose depth runs from its start to its end.
function segmentBox(seg) {
  const [ax, az] = seg.from_m;
  const [bx, bz] = seg.to_m;
  const length = Math.hypot(bx - ax, bz - az) || seg.length_m || 1;
  const heading = (Math.atan2(bz - az, bx - ax) * 180) / Math.PI;
  return {
    x_m: (ax + bx) / 2,
    z_m: (az + bz) / 2,
    rotation_degrees: heading - 90,
    dimensions_m: { width: seg.thickness_m ?? 3, depth: length, height: seg.height_m ?? 7 },
    ground_elevation_m: seg.ground_elevation_m,
  };
}

// Triangles with their colours, lit once as they are added: flat shading from a light in
// the north-west and above, either side of a face lit alike.
class Mesh {
  constructor() {
    this.positions = [];
    this.colors = [];
  }

  triangle(a, b, c, color) {
    const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
    const v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    const n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
    const len = Math.hypot(...n) || 1;
    const shade = 0.55 + 0.45 * Math.abs((n[0] * -0.4 + n[1] * 0.85 + n[2] * -0.35) / len);
    for (const p of [a, b, c]) {
      this.positions.push(p[0], p[1], p[2]);
      this.colors.push(color[0] * shade, color[1] * shade, color[2] * shade);
    }
  }

  // A box footprint between two heights; `toRenderer` maps plan metres to renderer space.
  box(b, bottom, top, color, toRenderer) {
    const v = boxCorners(b, bottom, top).map(([x, y, z]) => toRenderer(x, y, z));
    for (const [a, c, d, e] of FACES) {
      this.triangle(v[a], v[c], v[d], color);
      this.triangle(v[a], v[d], v[e], color);
    }
  }

  done(extra = {}) {
    return { positions: new Float32Array(this.positions), colors: new Float32Array(this.colors), ...extra };
  }
}

export function buildGeometry(plan, plots) {
  const datum = heightAt(plan, 0, 0);
  const mesh = new Mesh();
  // renderer point from plan metres and absolute height
  const P = (x, h, z) => [x, h - datum, -z];
  const triangle = (a, b, c, color) => mesh.triangle(a, b, c, color);

  // Terrain: one quad per surface cell, coloured by the plan's terrain cell under it.
  const surface = plan.terrain?.surface;
  const [lo] = planBounds(plan);
  const palette = terrainPalette(plan);
  if (surface?.heights_m?.length) {
    const n = surface.size ?? surface.heights_m.length;
    const step = surface.step_m;
    const h = surface.heights_m;
    for (let j = 0; j < n - 1; j++) {
      for (let i = 0; i < n - 1; i++) {
        const x0 = lo + i * step;
        const z0 = lo + j * step;
        const a = P(x0, h[j][i], z0);
        const b = P(x0 + step, h[j][i + 1], z0);
        const c = P(x0 + step, h[j + 1][i + 1], z0 + step);
        const d = P(x0, h[j + 1][i], z0 + step);
        const color = palette.at(x0 + step / 2, z0 + step / 2).map(v => v / 255);
        triangle(a, c, b, color);
        triangle(a, d, c, color);
      }
    }
  }

  // Flat strips laid on the ground: road connections to the world's roads.
  function strip(a, b, width, color, lift = 0.15) {
    const length = Math.hypot(b[0] - a[0], b[1] - a[1]);
    if (!length) return;
    const dx = (-(b[1] - a[1]) / length) * (width / 2);
    const dz = ((b[0] - a[0]) / length) * (width / 2);
    const q = [[a[0] + dx, a[1] + dz], [b[0] + dx, b[1] + dz], [b[0] - dx, b[1] - dz], [a[0] - dx, a[1] - dz]]
      .map(([x, z]) => P(x, heightAt(plan, x, z) + lift, z));
    triangle(q[0], q[2], q[1], color);
    triangle(q[0], q[3], q[2], color);
  }
  for (const path of plan.street_paths_m ?? []) {
    for (let i = 1; i < path.length; i++) strip(path[i - 1], path[i], 3, [0.62, 0.62, 0.58]);
  }
  for (const c of plan.road_connections ?? []) {
    if (c.status !== 'connected' || !c.local_path_m) continue;
    for (let i = 1; i < c.local_path_m.length; i++) strip(c.local_path_m[i - 1], c.local_path_m[i], 4, [0.93, 0.66, 0.34], 0.25);
  }

  const box = (b, bottom, top, color) => mesh.box(b, bottom, top, color, P);
  for (const seg of wallSegments(plan)) {
    const b = segmentBox(seg);
    const floor = b.ground_elevation_m ?? heightAt(plan, b.x_m, b.z_m);
    box(b, floor - 1, floor + Math.max(0.5, b.dimensions_m.height), [0.45, 0.33, 0.22]);
  }
  for (const b of plots) {
    const floor = b.ground_elevation_m ?? heightAt(plan, b.x_m, b.z_m);
    const base = b.foundation_bottom_m ?? floor;
    if (floor > base + 0.01) box(b, base, floor, [0.42, 0.43, 0.42]);
    const color = plotColor(b).map(v => v / 255);
    box(b, floor + 0.03, floor + Math.max(0.05, b.dimensions_m.height || 0.05), color);
  }
  const [x0, , x1] = planBounds(plan);
  return mesh.done({ datum, span: x1 - x0 });
}

// Ground colours for a patch, by height within it; anything below the sea is water.
const PATCH_LOW = [0.36, 0.5, 0.31];
const PATCH_HIGH = [0.72, 0.66, 0.52];
const PATCH_WATER = [0.24, 0.45, 0.62];

/**
 * A local patch (the fwg.patch document) as a mesh: its own vertices and triangles, in its
 * tangent frame (x east, y up, z north, origin on the ground at the centre), coloured by
 * height, with a 1.8 m figure standing at the origin for scale.
 */
export function buildPatchGeometry(patch, seaLevel = 0) {
  const mesh = new Mesh();
  const { local_x: xs, local_y: ys, local_z: zs, height } = patch;
  let lo = Infinity;
  let hi = -Infinity;
  for (const y of ys) {
    if (y < lo) lo = y;
    if (y > hi) hi = y;
  }
  const span = hi - lo || 1;
  const vertex = i => [xs[i], ys[i], -zs[i]];
  const colorOf = i => {
    if (height[i] < seaLevel) return PATCH_WATER;
    const t = (ys[i] - lo) / span;
    return [0, 1, 2].map(k => PATCH_LOW[k] + (PATCH_HIGH[k] - PATCH_LOW[k]) * t);
  };
  const indices = patch.triangle_indices;
  for (let t = 0; t + 2 < indices.length; t += 3) {
    const [a, b, c] = [indices[t], indices[t + 1], indices[t + 2]];
    const color = colorOf(a).map((v, k) => (v + colorOf(b)[k] + colorOf(c)[k]) / 3);
    mesh.triangle(vertex(a), vertex(b), vertex(c), color);
  }
  const figure = { x_m: 0, z_m: 0, rotation_degrees: 0, dimensions_m: { width: 0.5, depth: 0.3, height: 1.8 } };
  mesh.box(figure, 0, 1.8, [1, 0.62, 0.2], (x, y, z) => [x, y, -z]);
  return mesh.done({ datum: 0, span: patch.patch_config?.span ?? 64 });
}

// --- the renderer ----------------------------------------------------------------------

const VERTEX = `
attribute vec3 position;
attribute vec3 color;
uniform mat4 viewProjection;
varying vec3 vColor;
void main() {
  gl_Position = viewProjection * vec4(position, 1.0);
  vColor = color;
}`;

const FRAGMENT = `
precision mediump float;
varying vec3 vColor;
void main() { gl_FragColor = vec4(vColor, 1.0); }`;

/**
 * Create a 3D view inside `container`. Returns null if WebGL is unavailable. `onPick(plot)`
 * is called when a plot is clicked.
 */
export function createPlan3D(container, onPick) {
  const holder = document.createElement('div');
  holder.className = 'gl';
  const canvas = document.createElement('canvas');
  const labels = document.createElement('canvas');
  labels.className = 'labels';
  holder.append(canvas, labels);
  container.append(holder);
  const gl = canvas.getContext('webgl', { antialias: true });
  if (!gl) {
    holder.remove();
    return null;
  }

  const compile = (type, source) => {
    const shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(shader));
    return shader;
  };
  const program = gl.createProgram();
  gl.attachShader(program, compile(gl.VERTEX_SHADER, VERTEX));
  gl.attachShader(program, compile(gl.FRAGMENT_SHADER, FRAGMENT));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
  const positionBuffer = gl.createBuffer();
  const colorBuffer = gl.createBuffer();
  const positionAttr = gl.getAttribLocation(program, 'position');
  const colorAttr = gl.getAttribLocation(program, 'color');
  const viewProjectionUniform = gl.getUniformLocation(program, 'viewProjection');

  let scene = null; // what is shown: a plan, or a mesh key; the camera resets when it changes
  let plan = null; // the plan whose plots can be picked; null for a bare mesh
  let plots = [];
  let datum = 0;
  let count = 0;
  let span = 100;
  let yaw = -0.6;
  let pitch = 0.75;
  let distanceScale = 1;
  let target = [0, 0, 0];
  let showLabels = false;
  let selected = null;
  let matrix = null;

  function resetCamera() {
    yaw = -0.6;
    pitch = 0.75;
    distanceScale = 1;
    target = [0, 0, 0];
    selected = null;
  }

  function camera(width, height) {
    const distance = span * 1.25 * distanceScale;
    const eye = [
      target[0] + distance * Math.cos(pitch) * Math.sin(yaw),
      target[1] + distance * Math.sin(pitch),
      target[2] + distance * Math.cos(pitch) * Math.cos(yaw),
    ];
    const proj = perspective(0.8, width / Math.max(1, height), Math.max(0.5, distance / 200), distance * 8 + span * 4);
    return multiply(proj, lookAt(eye, target, [0, 1, 0]));
  }

  function draw() {
    if (!count) return;
    const rect = holder.getBoundingClientRect();
    if (!rect.width || !rect.height) return;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = Math.round(rect.width * dpr);
    const h = Math.round(rect.height * dpr);
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = labels.width = w;
      canvas.height = labels.height = h;
    }
    gl.viewport(0, 0, w, h);
    gl.clearColor(0.05, 0.09, 0.12, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);
    gl.useProgram(program);
    matrix = camera(w, h);
    gl.uniformMatrix4fv(viewProjectionUniform, false, matrix);
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.enableVertexAttribArray(positionAttr);
    gl.vertexAttribPointer(positionAttr, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, colorBuffer);
    gl.enableVertexAttribArray(colorAttr);
    gl.vertexAttribPointer(colorAttr, 3, gl.FLOAT, false, 0, 0);
    gl.drawArrays(gl.TRIANGLES, 0, count);
    drawLabels(w, h, dpr);
  }

  // Names above plots (all, or just the selected one), and the selected plot's outline.
  function drawLabels(w, h, dpr) {
    const ctx = labels.getContext('2d');
    ctx.clearRect(0, 0, w, h);
    ctx.font = `${11 * dpr}px system-ui, sans-serif`;
    ctx.textAlign = 'center';
    for (const b of plots) {
      if (!showLabels && b.id !== selected) continue;
      const floor = b.ground_elevation_m ?? heightAt(plan, b.x_m, b.z_m);
      const top = floor + (b.dimensions_m.height || 0);
      const corners = boxCorners(b, top, top).slice(4).map(([x, y, z]) => project(matrix, [x, y - datum, -z], w, h));
      if (corners.some(p => p[2] <= 0)) continue;
      if (b.id === selected) {
        ctx.strokeStyle = '#ffffff';
        ctx.lineWidth = 2 * dpr;
        ctx.beginPath();
        corners.forEach((p, i) => (i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1])));
        ctx.closePath();
        ctx.stroke();
      }
      const cx = corners.reduce((s, p) => s + p[0], 0) / 4;
      const cy = Math.min(...corners.map(p => p[1])) - 4 * dpr;
      ctx.lineWidth = 3 * dpr;
      ctx.strokeStyle = 'rgba(10,16,20,.9)';
      ctx.strokeText(b.name, cx, cy);
      ctx.fillStyle = '#ffffff';
      ctx.fillText(b.name, cx, cy);
    }
  }

  // The nearest plot whose projected faces contain the point.
  function pick(clientX, clientY) {
    if (!plan || !matrix) return;
    const rect = canvas.getBoundingClientRect();
    const x = ((clientX - rect.left) * canvas.width) / rect.width;
    const y = ((clientY - rect.top) * canvas.height) / rect.height;
    const inside = pts => {
      let yes = false;
      for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
        const a = pts[i];
        const b = pts[j];
        if ((a[1] > y) !== (b[1] > y) && x < ((b[0] - a[0]) * (y - a[1])) / (b[1] - a[1]) + a[0]) yes = !yes;
      }
      return yes;
    };
    let hit = null;
    let depth = Infinity;
    for (const b of plots) {
      const floor = b.ground_elevation_m ?? heightAt(plan, b.x_m, b.z_m);
      const v = boxCorners(b, floor, floor + Math.max(0.05, b.dimensions_m.height || 0.05))
        .map(([px, py, pz]) => project(matrix, [px, py - datum, -pz], canvas.width, canvas.height));
      for (const face of FACES) {
        const pts = face.map(i => v[i]);
        if (pts.some(p => p[2] <= 0)) continue;
        const d = pts.reduce((s, p) => s + p[2], 0) / 4;
        if (d < depth && inside(pts)) {
          depth = d;
          hit = b;
        }
      }
    }
    if (hit) {
      selected = hit.id;
      onPick(hit);
      draw();
    }
  }

  let press = null;
  canvas.addEventListener('pointerdown', event => {
    press = { x: event.clientX, y: event.clientY, last: [event.clientX, event.clientY], moved: false, pan: event.button === 2 || event.shiftKey };
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener('pointermove', event => {
    if (!press) return;
    const dx = event.clientX - press.last[0];
    const dy = event.clientY - press.last[1];
    press.last = [event.clientX, event.clientY];
    if (Math.hypot(event.clientX - press.x, event.clientY - press.y) > 4) press.moved = true;
    if (press.pan) {
      // move the target in the ground plane, along the view's right and forward
      const scale = (span * distanceScale) / 400;
      target = [
        target[0] - (dx * Math.cos(yaw) + dy * Math.sin(yaw)) * scale,
        target[1],
        target[2] - (-dx * Math.sin(yaw) + dy * Math.cos(yaw)) * scale,
      ];
    } else {
      yaw -= dx * 0.008;
      pitch = Math.max(0.08, Math.min(1.52, pitch + dy * 0.006));
    }
    draw();
  });
  canvas.addEventListener('pointerup', event => {
    if (press && !press.moved) pick(event.clientX, event.clientY);
    press = null;
  });
  canvas.addEventListener('pointercancel', () => {
    press = null;
  });
  canvas.addEventListener('contextmenu', event => event.preventDefault());
  canvas.addEventListener('wheel', event => {
    event.preventDefault();
    distanceScale = Math.max(0.08, Math.min(4, distanceScale * Math.exp(event.deltaY * 0.0012)));
    draw();
  }, { passive: false });
  new ResizeObserver(() => draw()).observe(holder);

  // Fill the buffers before anything draws from them: a draw from an empty buffer left this
  // context drawing nothing even once the data arrived.
  function show(geometry, key, nextPlan, nextPlots) {
    gl.bindBuffer(gl.ARRAY_BUFFER, positionBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, geometry.positions, gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, colorBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, geometry.colors, gl.STATIC_DRAW);
    const changed = key !== scene;
    scene = key;
    plan = nextPlan;
    plots = nextPlots;
    datum = geometry.datum;
    span = geometry.span;
    count = geometry.positions.length / 3;
    if (changed) resetCamera();
    draw();
    return { triangles: count / 3 };
  }

  return {
    element: holder,
    /** Show a plan and the plots standing in it; its plots can be picked. */
    setScene(nextPlan, nextPlots) {
      return show(buildGeometry(nextPlan, nextPlots), nextPlan, nextPlan, nextPlots);
    },
    /** Show a prebuilt mesh with nothing to pick; `key` names it, so the camera stays put for the same one. */
    setMesh(geometry, key) {
      return show(geometry, key, null, []);
    },
    setLabels(on) {
      showLabels = on;
      draw();
    },
    select(id) {
      selected = id;
      draw();
    },
    reset() {
      resetCamera();
      draw();
    },
    redraw: draw,
    // For inspection from the console: the camera and where the plan centre lands.
    debug: () => ({ count, span, yaw, pitch, distanceScale, target, matrix: matrix && [...matrix], centre: matrix && project(matrix, [0, 0, 0], canvas.width, canvas.height) }),
  };
}
