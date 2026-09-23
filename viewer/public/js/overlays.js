// The map's overlays: what each one reads from the world, how it draws, and what a click
// on it selects. Each definition's `load(world)` fetches its blocks and returns a renderer
// {count, items, draw(ctx, view), hit(view, sx, sy) -> the items within reach, nearest
// first}; the map draws them in this file's order and hit tests from the top down.

import { World, unwrap } from './world.js';

// --- drawing helpers --------------------------------------------------------------------

function pointAt(world, x, z) {
  return world.cellXY(x, z);
}

/** Call fn(item, sx, sy) for each item on screen, once per visible copy of the world. */
function eachOnScreen(view, items, pad, fn) {
  const turns = view.turnsFor(0, 360);
  const { k, tx, ty, width, height } = view;
  for (const m of turns) {
    const shift = 360 * m * k + tx;
    for (const item of items) {
      const sx = item.X * k + shift;
      if (sx < -pad || sx > width + pad) continue;
      const sy = item.Y * k + ty;
      if (sy < -pad || sy > height + pad) continue;
      fn(item, sx, sy);
    }
  }
}

/** Every item within `radius` pixels of (sx, sy), nearest first. */
function within(view, items, sx, sy, radius) {
  const found = [];
  const r2 = radius * radius;
  eachOnScreen(view, items, radius, (item, ix, iy) => {
    const d = (ix - sx) ** 2 + (iy - sy) ** 2;
    if (d <= r2) found.push([d, item]);
  });
  return found.sort((a, b) => a[0] - b[0]).map(([, item]) => item);
}

/** A polyline in map degrees, unwrapped across the seam, with its X extent. */
function line(points, extra = {}) {
  const pts = unwrap(points);
  let xmin = Infinity;
  let xmax = -Infinity;
  for (const [X] of pts) {
    if (X < xmin) xmin = X;
    if (X > xmax) xmax = X;
  }
  return { pts, xmin, xmax, ...extra };
}

function tracePolyline(ctx, view, ln) {
  const { k, tx, ty } = view;
  for (const m of view.turnsFor(ln.xmin, ln.xmax)) {
    const shift = 360 * m;
    ln.pts.forEach(([X, Y], i) => {
      const sx = (X + shift) * k + tx;
      const sy = Y * k + ty;
      if (i === 0) ctx.moveTo(sx, sy);
      else ctx.lineTo(sx, sy);
    });
  }
}

function strokeLines(ctx, view, lines, style) {
  ctx.beginPath();
  for (const ln of lines) tracePolyline(ctx, view, ln);
  ctx.strokeStyle = style.color;
  ctx.lineWidth = style.width;
  ctx.globalAlpha = style.alpha ?? 1;
  ctx.setLineDash(style.dash ?? []);
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  ctx.stroke();
  ctx.setLineDash([]);
  ctx.globalAlpha = 1;
}

function nodeLine(world, nodes, extra) {
  return line(nodes.map(node => world.nodeXY(node)), extra);
}

function css(color, alpha) {
  if (!color) return '#aaa';
  return alpha === undefined ? `rgb(${color.join(',')})` : `rgba(${color.join(',')},${alpha})`;
}

const CLASS_RANK = { capital: 0, medium: 1, small: 2 };

// City names, capitals first, each drawn only where it overlaps no name already drawn.
function drawLabels(ctx, labels, view) {
  const size = view.k / view.fitK > 2.5 ? 12 : 11;
  ctx.font = `600 ${size}px system-ui, sans-serif`;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'bottom';
  ctx.lineWidth = 3;
  ctx.strokeStyle = 'rgba(8,14,20,.85)';
  ctx.fillStyle = '#f4efe2';
  labels.sort((a, b) => (CLASS_RANK[a.c.record.city_class] ?? 3) - (CLASS_RANK[b.c.record.city_class] ?? 3));
  const placed = [];
  for (const { c, sx, sy } of labels) {
    const text = c.record.name;
    const w = ctx.measureText(text).width + 4;
    const box = [sx - w / 2, sy - size - 2, sx + w / 2, sy + 1];
    if (placed.some(p => box[0] < p[2] && box[2] > p[0] && box[1] < p[3] && box[3] > p[1])) continue;
    placed.push(box);
    ctx.strokeText(text, sx, sy);
    ctx.fillText(text, sx, sy);
  }
}

// --- glyphs (screen pixels, centred on the point) -----------------------------------------

function drawHouse(ctx, sx, sy, s, wall, roof) {
  ctx.fillStyle = wall;
  ctx.strokeStyle = '#2b241d';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.rect(sx - s * 0.55, sy - s * 0.15, s * 1.1, s * 0.75);
  ctx.fill();
  ctx.stroke();
  ctx.fillStyle = roof;
  ctx.beginPath();
  ctx.moveTo(sx - s * 0.78, sy - s * 0.1);
  ctx.lineTo(sx, sy - s * 0.78);
  ctx.lineTo(sx + s * 0.78, sy - s * 0.1);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
}

function drawKeep(ctx, sx, sy, s) {
  const h = s / 2;
  ctx.fillStyle = '#c9c8d3';
  ctx.strokeStyle = '#1f2a33';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(sx - h, sy + h);
  ctx.lineTo(sx - h, sy - h);
  ctx.lineTo(sx - h * 0.35, sy - h);
  ctx.lineTo(sx - h * 0.35, sy - h * 0.45);
  ctx.lineTo(sx + h * 0.35, sy - h * 0.45);
  ctx.lineTo(sx + h * 0.35, sy - h);
  ctx.lineTo(sx + h, sy - h);
  ctx.lineTo(sx + h, sy + h);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
}

// A ruin is three wall stumps of falling height: low and broken, so it never reads as the
// keep a fortress is drawn with.
const RUIN_OUTLINE = [[-10, 8], [-10, -3], [-6, -5], [-6, 3], [1, 4], [1, -1], [6, -3], [6, 5], [10, 6], [10, 8]];

function drawRuin(ctx, sx, sy, s, fill) {
  const f = s / 20;
  ctx.fillStyle = fill;
  ctx.strokeStyle = '#2f2922';
  ctx.lineWidth = 1;
  ctx.beginPath();
  RUIN_OUTLINE.forEach(([x, y], i) => (i ? ctx.lineTo(sx + x * f, sy + y * f) : ctx.moveTo(sx + x * f, sy + y * f)));
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
}

// A dread: a horned diamond, crimson while its lair stands, a ghost of one as a legend.
function drawDread(ctx, sx, sy, s, living) {
  const r = s * 0.55;
  const fill = living ? '#e2483d' : 'rgba(226,72,61,.35)';
  ctx.beginPath();
  ctx.moveTo(sx - r * 0.45, sy - r * 0.35);
  ctx.lineTo(sx - r * 0.8, sy - r * 1.1);
  ctx.lineTo(sx - r * 0.05, sy - r * 0.55);
  ctx.moveTo(sx + r * 0.45, sy - r * 0.35);
  ctx.lineTo(sx + r * 0.8, sy - r * 1.1);
  ctx.lineTo(sx + r * 0.05, sy - r * 0.55);
  ctx.lineWidth = 1.5;
  ctx.strokeStyle = 'rgba(10,14,18,.85)';
  ctx.fillStyle = fill;
  ctx.stroke();
  ctx.fill();
  ctx.beginPath();
  ctx.moveTo(sx, sy - r);
  ctx.lineTo(sx + r * 0.8, sy);
  ctx.lineTo(sx, sy + r);
  ctx.lineTo(sx - r * 0.8, sy);
  ctx.closePath();
  ctx.lineWidth = 2;
  ctx.stroke();
  ctx.fill();
}

function drawTent(ctx, sx, sy, s, fill) {
  const h = s / 2;
  ctx.fillStyle = fill;
  ctx.strokeStyle = '#1d1a14';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(sx, sy - h);
  ctx.lineTo(sx + h, sy + h * 0.8);
  ctx.lineTo(sx - h, sy + h * 0.8);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
}

// --- the overlays ------------------------------------------------------------------------

const HAMLET_COLORS = { farming: '#a6da90', resource: '#e9c17c' };
const COASTAL_COLOR = '#8ff0ea';
const BAND_COLORS = {
  survivors: '#f2d479', deserters: '#c7a3a3', cultists: '#d68cf0', merchants: '#f5b35a', bandits: '#ef6a5a', wanderers: '#d3dde2',
};

export const OVERLAYS = [
  {
    id: 'rivers',
    label: 'Rivers',
    swatch: '#5ec3e8',
    defaultOn: true,
    available: w => w.has('climate') || w.has('water'),
    async load(world) {
      const climate = await world.block('climate');
      const segments = climate?.river_segments ?? (await world.block('water'))?.river_segments ?? [];
      const lines = segments.map(([a, b]) => nodeLine(world, [a, b]));
      return {
        count: lines.length,
        draw(ctx, view) {
          strokeLines(ctx, view, lines, { color: '#5ec3e8', width: Math.max(1.2, view.cellPx() * 0.12), alpha: 0.9 });
        },
      };
    },
  },
  {
    id: 'trade',
    label: 'Sea and air routes',
    swatch: '#7ff2e5',
    defaultOn: false,
    available: w => w.has('transport'),
    async load(world) {
      const transport = await world.block('transport');
      const routes = (transport?.routes ?? []).filter(r => r.mode !== 'ground');
      const lines = routes.map(route => {
        const points = route.path ? route.path.map(p => (p.length === 3 ? World.directionXY(p) : pointAt(world, p[0], p[1])))
          : (route.nodes ?? []).map(node => world.nodeXY(node));
        return line(points, { route });
      });
      return {
        count: lines.length,
        draw(ctx, view) {
          strokeLines(ctx, view, lines.filter(l => l.route.mode === 'sea'), { color: '#7ff2e5', width: 1.3, alpha: 0.75, dash: [5, 4] });
          strokeLines(ctx, view, lines.filter(l => l.route.mode === 'air'), { color: '#e7caff', width: 1.3, alpha: 0.75, dash: [2, 4] });
        },
      };
    },
  },
  {
    id: 'roads',
    label: 'Roads',
    swatch: '#e2b77c',
    defaultOn: true,
    available: w => w.has('roads'),
    async load(world) {
      const roads = await world.block('roads');
      const lines = (roads?.routes ?? []).map((route, index) => nodeLine(world, route.nodes ?? [], { route, index }));
      const crossings = (roads?.routes ?? []).flatMap(route => (route.river_crossings ?? []).map(pair => nodeLine(world, pair)));
      return {
        count: lines.length,
        draw(ctx, view) {
          const width = Math.max(1.4, Math.min(4, view.cellPx() * 0.16));
          strokeLines(ctx, view, lines, { color: '#3a2c1c', width: width + 1.6, alpha: 0.6 });
          strokeLines(ctx, view, lines, { color: '#e2b77c', width });
          strokeLines(ctx, view, crossings, { color: '#fbf3e4', width: width + 1 });
        },
      };
    },
  },
  {
    id: 'ley',
    label: 'Ley networks',
    swatch: '#b48bf0',
    defaultOn: false,
    available: w => w.has('magic'),
    async load(world) {
      const magic = await world.block('magic');
      const lines = [];
      const nodes = [];
      for (const [school, network] of Object.entries(magic?.networks ?? {})) {
        const color = network.color ?? [200, 170, 240];
        for (const edge of network.edges ?? []) {
          lines.push(line((edge.path ?? []).map(World.directionXY), { school, color, edge }));
        }
        for (const node of network.nodes ?? []) {
          const [X, Y] = World.directionXY(node.direction);
          nodes.push({ X, Y, kind: 'ley_node', record: { ...node, school, network: network.name }, color });
        }
      }
      const bySchool = new Map();
      for (const ln of lines) {
        if (!bySchool.has(ln.school)) bySchool.set(ln.school, []);
        bySchool.get(ln.school).push(ln);
      }
      return {
        count: nodes.length,
        items: nodes,
        draw(ctx, view) {
          for (const [, group] of bySchool) {
            strokeLines(ctx, view, group, { color: css(group[0].color), width: Math.max(1.2, view.cellPx() * 0.1), alpha: 0.75 });
          }
          const r = view.markerSize(2.6);
          eachOnScreen(view, nodes, r * 2, (node, sx, sy) => {
            const radius = r * Math.min(2, 0.6 + 0.35 * (node.record.intensity ?? 1));
            ctx.beginPath();
            ctx.arc(sx, sy, radius, 0, Math.PI * 2);
            ctx.fillStyle = css(node.color);
            ctx.fill();
            ctx.lineWidth = 1;
            ctx.strokeStyle = '#fff5dc';
            ctx.stroke();
          });
        },
        hit: (view, sx, sy) => within(view,nodes, sx, sy, 7),
      };
    },
  },
  {
    id: 'bands',
    label: 'Bands and routes',
    swatch: BAND_COLORS.survivors,
    defaultOn: true,
    available: w => w.has('nomads'),
    async load(world) {
      const nomads = await world.block('nomads');
      const bands = [];
      const routes = [];
      for (const band of nomads?.groups ?? []) {
        const [X, Y] = pointAt(world, band.x, band.z);
        const color = BAND_COLORS[band.classification] ?? '#dddddd';
        bands.push({ X, Y, kind: 'band', record: band, color });
        for (const leg of band.legs ?? []) routes.push(nodeLine(world, leg.nodes ?? [], { color }));
      }
      return {
        count: bands.length,
        items: bands,
        draw(ctx, view) {
          const byColor = new Map();
          for (const r of routes) {
            if (!byColor.has(r.color)) byColor.set(r.color, []);
            byColor.get(r.color).push(r);
          }
          for (const [color, group] of byColor) strokeLines(ctx, view, group, { color, width: 1.4, alpha: 0.85, dash: [4, 3] });
          const s = view.markerSize(9);
          eachOnScreen(view, bands, s, (band, sx, sy) => drawTent(ctx, sx, sy, s, band.color));
        },
        hit: (view, sx, sy) => within(view,bands, sx, sy, view.markerSize(9) * 0.7 + 3),
      };
    },
  },
  {
    id: 'nests',
    label: 'Nests',
    swatch: '#ff8f76',
    defaultOn: false,
    note: 'thousands; monsters coral, animals teal',
    available: w => w.has('beast_nests') || w.has('wildlife'),
    async load(world) {
      const fields = 'id,species_id,name,x,z,tier,kind,class,layer,family,size';
      const [monsters, animals] = await Promise.all([
        world.select('beast_nests', { path: 'sites', fields }),
        world.select('wildlife', { path: 'sites', fields }),
      ]);
      const items = [];
      for (const [list, block] of [[animals ?? [], 'wildlife'], [monsters ?? [], 'beast_nests']]) {
        for (const nest of list) {
          const [X, Y] = pointAt(world, nest.x, nest.z);
          items.push({ X, Y, kind: 'nest', record: nest, block, monster: nest.class === 'monster' });
        }
      }
      return {
        count: items.length,
        items,
        draw(ctx, view) {
          const s = Math.max(2, Math.min(5, view.cellPx() * 0.12));
          eachOnScreen(view, items, s, (item, sx, sy) => {
            const tier = item.record.tier ?? 1;
            ctx.fillStyle = item.monster ? `hsl(${14 - tier * 3}, 90%, ${72 - tier * 6}%)` : `hsl(170, 60%, ${68 - tier * 5}%)`;
            ctx.fillRect(sx - s / 2, sy - s / 2, s, s);
          });
        },
        hit: (view, sx, sy) => within(view,items, sx, sy, 5),
      };
    },
  },
  {
    id: 'keylocs',
    label: 'Key locations',
    swatch: '#d6c484',
    defaultOn: true,
    available: w => w.has('key_locations'),
    async load(world) {
      const block = await world.block('key_locations');
      const families = block?.families ?? {};
      const sites = (block?.sites ?? []).map(site => {
        const [X, Y] = pointAt(world, site.x, site.z);
        const family = families[site.family] ?? {};
        return { X, Y, kind: 'key_location', record: site, glyph: family.glyph ?? '?', color: css(family.color ?? [216, 210, 196]) };
      });
      const byId = new Map(sites.map(s => [s.record.id, s]));
      const chains = (block?.chains ?? []).map(chain => {
        const members = (chain.members ?? []).map(id => byId.get(id)).filter(Boolean);
        return members.length > 1 ? line(members.map(m => [m.X, m.Y]), { color: members[0].color }) : null;
      }).filter(Boolean);
      return {
        count: sites.length,
        items: sites,
        draw(ctx, view) {
          for (const chain of chains) strokeLines(ctx, view, [chain], { color: chain.color, width: 1, alpha: 0.35 });
          const base = view.markerSize(10);
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          eachOnScreen(view, sites, base * 1.5, (site, sx, sy) => {
            const tier = site.record.tier ?? 1;
            const size = Math.round(base * (tier >= 3 ? 1.45 : tier === 2 ? 1.15 : 0.9));
            ctx.font = `700 ${size}px system-ui, sans-serif`;
            ctx.lineWidth = 3;
            ctx.strokeStyle = 'rgba(10,14,18,.8)';
            ctx.strokeText(site.glyph, sx, sy);
            ctx.fillStyle = site.color;
            ctx.fillText(site.glyph, sx, sy);
          });
        },
        hit: (view, sx, sy) => within(view,sites, sx, sy, view.markerSize(10) * 0.6 + 3),
      };
    },
  },
  {
    id: 'ruins',
    label: 'Ruins',
    swatch: '#8a8177',
    defaultOn: true,
    note: 'cleansed ruins bleached',
    available: w => w.has('ruins'),
    async load(world) {
      const ruins = (await world.block('ruins')) ?? [];
      const items = ruins.map(ruin => {
        const [X, Y] = pointAt(world, ruin.x, ruin.z);
        return { X, Y, kind: 'ruin', record: ruin, cleansed: ruin.cause === 'cleansed' };
      });
      return {
        count: items.length,
        items,
        draw(ctx, view) {
          const s = view.markerSize(13);
          eachOnScreen(view, items, s, (item, sx, sy) => {
            if (item.cleansed) {
              ctx.beginPath();
              ctx.arc(sx, sy, s * 0.62, 0, Math.PI * 2);
              ctx.strokeStyle = 'rgba(255,250,220,.9)';
              ctx.lineWidth = 1.5;
              ctx.stroke();
            }
            drawRuin(ctx, sx, sy, s, item.cleansed ? '#e9e2cf' : '#8a8177');
          });
        },
        hit: (view, sx, sy) => within(view,items, sx, sy, view.markerSize(13) * 0.55 + 2),
      };
    },
  },
  {
    // The monsters the cast names for the cities they razed (2026-09-23): a living one at the
    // lair it still holds, a legend at the last city it razed.
    id: 'dreads',
    label: 'Dreads',
    swatch: '#e2483d',
    defaultOn: true,
    note: 'living at their lairs; legends faded, at the last city they razed',
    available: w => w.has('heroes'),
    async load(world) {
      const [heroes, lairs, ruins] = await Promise.all([
        world.block('heroes'),
        world.has('beast_nests') ? world.select('beast_nests', { path: 'sites', fields: 'id,x,z' }) : [],
        world.has('ruins') ? world.block('ruins') : [],
      ]);
      const lairAt = new Map((lairs ?? []).map(n => [n.id, n]));
      const ruinAt = new Map((ruins ?? []).map(r => [r.uid, r]));
      const items = [];
      for (const dread of heroes?.dreads ?? []) {
        const living = dread.status === 'living';
        const razed = dread.destroyed ?? [];
        const where = living ? lairAt.get(dread.nest_id) : ruinAt.get(razed[razed.length - 1]?.uid);
        if (!where) continue;
        const [X, Y] = pointAt(world, where.x, where.z);
        items.push({ X, Y, kind: 'hero', record: dread, living });
      }
      return {
        count: items.length,
        items,
        draw(ctx, view) {
          const s = view.markerSize(12);
          eachOnScreen(view, items, s, (item, sx, sy) => drawDread(ctx, sx, sy, s, item.living));
        },
        hit: (view, sx, sy) => within(view, items, sx, sy, view.markerSize(12) * 0.6 + 3),
      };
    },
  },
  {
    id: 'rural',
    label: 'Hamlets and fortresses',
    swatch: HAMLET_COLORS.farming,
    defaultOn: true,
    available: w => w.has('humans'),
    async load(world) {
      const humans = await world.block('humans');
      const hamlets = (humans?.hamlets ?? []).map(h => {
        const [X, Y] = pointAt(world, h.x, h.z);
        const color = HAMLET_COLORS[h.role] ?? COASTAL_COLOR;
        return { X, Y, kind: 'hamlet', record: h, color };
      });
      const forts = (humans?.fortresses ?? []).map(f => {
        const [X, Y] = pointAt(world, f.x, f.z);
        return { X, Y, kind: 'fortress', record: f };
      });
      return {
        count: hamlets.length + forts.length,
        items: [...forts, ...hamlets],
        draw(ctx, view) {
          const r = view.markerSize(2.8);
          eachOnScreen(view, hamlets, r * 2, (h, sx, sy) => {
            ctx.beginPath();
            ctx.arc(sx, sy, r, 0, Math.PI * 2);
            ctx.fillStyle = h.color;
            ctx.fill();
            ctx.lineWidth = 0.8;
            ctx.strokeStyle = '#1b2620';
            ctx.stroke();
          });
          const s = view.markerSize(9);
          eachOnScreen(view, forts, s, (f, sx, sy) => drawKeep(ctx, sx, sy, s));
        },
        hit(view, sx, sy) {
          return [...within(view, forts, sx, sy, view.markerSize(9) * 0.6 + 2), ...within(view, hamlets, sx, sy, view.markerSize(2.8) + 4)];
        },
      };
    },
  },
  {
    id: 'cities',
    label: 'Cities',
    swatch: '#f5e6b8',
    defaultOn: true,
    note: 'size by class; corrupted cities ringed',
    available: w => w.has('settlements'),
    async load(world) {
      const [settlements, civilizations, magic] = await Promise.all([world.block('settlements'), world.block('civilizations'), world.block('magic')]);
      const civ = new Map((civilizations?.entities ?? []).map(e => [e.id, e]));
      const defaults = civilizations?.presentation_defaults ?? {};
      const cities = (settlements?.sites ?? []).map(site => {
        const [X, Y] = pointAt(world, site.x, site.z);
        const look = civ.get(site.civilization_id ?? site.population_profile)?.presentation ?? {};
        const outpost = site.outpost && look.outpost_colors;
        const wall = outpost ? defaults.outpost_wall_color_rgb : look.wall_color_rgb;
        const roof = outpost ? defaults.outpost_roof_color_rgb : look.roof_color_rgb;
        const school = site.corruption?.school;
        return {
          X, Y, kind: 'city', record: site,
          scale: site.city_class === 'capital' ? 1.35 : site.city_class === 'medium' ? 1.1 : 0.9,
          wall: css(wall ?? [236, 226, 200]),
          roof: css(roof ?? [170, 96, 78]),
          corruption: school ? css(magic?.networks?.[school]?.color ?? [150, 60, 200]) : null,
        };
      });
      return {
        count: cities.length,
        items: cities,
        draw(ctx, view) {
          const s0 = view.markerSize(12);
          const labels = [];
          eachOnScreen(view, cities, s0 * 2, (c, sx, sy) => {
            const s = s0 * c.scale;
            if (c.record.city_class === 'capital') {
              ctx.beginPath();
              ctx.arc(sx, sy - s * 0.1, s * 0.95, 0, Math.PI * 2);
              ctx.strokeStyle = 'rgba(255,240,190,.85)';
              ctx.lineWidth = 1.4;
              ctx.stroke();
            }
            if (c.corruption) {
              ctx.beginPath();
              ctx.arc(sx, sy - s * 0.1, s * 1.2, 0, Math.PI * 2);
              ctx.strokeStyle = c.corruption;
              ctx.lineWidth = 3;
              ctx.setLineDash([3, 2]);
              ctx.stroke();
              ctx.setLineDash([]);
            }
            drawHouse(ctx, sx, sy, s, c.wall, c.roof);
            if (c.corruption) {
              ctx.fillStyle = c.corruption;
              ctx.beginPath();
              ctx.arc(sx, sy + s * 0.22, s * 0.2, 0, Math.PI * 2);
              ctx.fill();
            }
            labels.push({ c, sx, sy: sy - s * 0.9 });
          });
          drawLabels(ctx, labels, view);
        },
        hit: (view, sx, sy) => within(view,cities, sx, sy, view.markerSize(12) * 0.8 + 3),
      };
    },
  },
];
