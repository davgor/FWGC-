// What a settlement plan means, shared by the 2D and 3D views.
//
// City, hamlet and castle plans share one local frame: metres, x east and z north, around
// the settlement, with `bounds_m` = [x0, z0, x1, z1]. The terrain is a raster of cells
// `terrain.cell_m` wide -- row j, column i covering x0 + i*cell, z0 + j*cell -- whose `codes`
// say 0 buildable, 1 water, 2 steep or channel; `roads` lists the cells streets occupy as
// [i, j]; `terrain.surface` holds heights on the cell corners. Walls are city
// `fortifications` or castle `wall_networks`. Key-location plans are smaller and simpler:
// bounds on the terrain, no biome raster, plots only.

// Plot fill colours, by what a plot is.
export const PLOT_COLORS = {
  housing: [118, 180, 208],
  fortification: [138, 106, 62],
  court: [90, 112, 82],
  core: [217, 179, 95],
  service: [189, 152, 204],
  structure: [176, 166, 146],
  terrain: [128, 118, 98],
};

export function plotCategory(b) {
  if (b.kind === 'housing') return 'housing';
  if (b.kind === 'gate' || b.kind === 'tower' || b.kind === 'stair' || b.phase === 'fortification' || b.phase === 'perimeter') return 'fortification';
  if (b.kind === 'court') return 'court';
  if (b.kind === 'landmark' || b.phase === 'landmarks' || b.phase === 'high') return 'core';
  if (b.kind === 'structure') return 'structure';
  if (b.kind === 'terrain') return 'terrain';
  return 'service';
}

export function plotColor(b) {
  return PLOT_COLORS[plotCategory(b)];
}

export const PLOT_LEGEND = [
  ['core', 'core buildings and landmarks'],
  ['service', 'other services'],
  ['housing', 'housing'],
  ['fortification', 'gates, towers, stairs'],
  ['court', 'courts'],
  ['structure', 'structures'],
];

export function wallSegments(plan) {
  return [
    ...(plan.fortifications?.segments ?? []),
    ...(plan.wall_networks ?? []).flatMap(network => network.segments ?? []),
  ];
}

export function wallRings(plan) {
  return [...(plan.fortifications?.rings ?? []), ...(plan.wall_networks ?? [])];
}

export function wallGates(plan) {
  return [...(plan.fortifications?.gates ?? []), ...(plan.wall_networks ?? []).flatMap(network => network.gates ?? [])];
}

export function wallTowers(plan) {
  return [...(plan.fortifications?.towers ?? []), ...(plan.wall_networks ?? []).flatMap(network => network.towers ?? [])];
}

/** The placement passes that place plots, in order, from the plan's own `passes`. */
export function placementPasses(plan) {
  return (plan.passes ?? []).map(p => p.id).filter(id => id !== 'map' && id !== 'shape');
}

/**
 * The plots standing after pass `phase`: later passes' plots are hidden, and a house a later
 * pass upgraded to apartments shows as it was before the upgrade.
 */
export function plotsThrough(plan, phase) {
  const order = placementPasses(plan);
  const limit = phase ? order.indexOf(phase) : order.length - 1;
  if (limit < 0 || !order.length) return plan.plots ?? [];
  return (plan.plots ?? [])
    .filter(b => {
      const at = order.indexOf(b.phase);
      return at < 0 || at <= limit;
    })
    .map(b => (b.housing_upgrade && order.indexOf(b.housing_upgrade.phase) > limit ? { ...b, ...b.housing_upgrade.previous } : b));
}

const CODE_COLORS = [[74, 104, 72], [52, 96, 128], [118, 84, 66]];
const ROAD_COLOR = [128, 128, 118];
const PLAIN_GROUND = [96, 108, 84];

/**
 * Colours of the plan's terrain: `cell(i, j)` for a raster cell and `at(x, z)` for a point in
 * metres. Natural biome colours where the plan carries them, water and steep ground from the
 * codes, streets from `roads`.
 */
export function terrainPalette(plan) {
  const t = plan.terrain ?? {};
  const size = t.size ?? 0;
  const cell = t.cell_m ?? 4;
  const [x0, z0] = Array.isArray(plan.bounds_m) ? plan.bounds_m : Array.isArray(t.bounds_m) ? t.bounds_m : [0, 0];
  const biomes = new Map((t.biome_catalogue ?? []).map(b => [b.id, b.color]));
  const roads = new Set((plan.roads ?? []).map(([i, j]) => j * size + i));
  const hasGrid = Boolean(t.codes?.length);
  const cellColor = (i, j) => {
    if (!hasGrid || i < 0 || j < 0 || i >= size || j >= size) return PLAIN_GROUND;
    if (roads.has(j * size + i)) return ROAD_COLOR;
    const code = t.codes[j][i];
    if (code === 1) return CODE_COLORS[1];
    const biome = biomes.get(t.natural_biome?.[j]?.[i]);
    if (!biome) return CODE_COLORS[code] ?? CODE_COLORS[0];
    // steep ground and channels, darkened
    return code === 2 ? biome.map(v => Math.round(v * 0.7)) : biome;
  };
  return {
    hasGrid,
    size,
    cell,
    cellColor,
    at: (x, z) => cellColor(Math.floor((x - x0) / cell), Math.floor((z - z0) / cell)),
  };
}

/** The ley school colour of raster cell (i, j), or null where the ground is not magical. */
export function magicColorAt(plan, i, j) {
  const t = plan.terrain ?? {};
  const v = t.biome_variant?.[j]?.[i];
  if (!(v >= 0)) return null;
  const school = t.magical_catalogue?.[v]?.magic_school;
  return t.magic_colors?.[school] ?? null;
}

/** One line describing the plan, as its kind reads best. */
export function planSummary(plan, shown) {
  const s = plan.stats ?? {};
  const workers = shown.reduce((a, b) => a + (b.workers ?? 0), 0);
  const beds = shown.reduce((a, b) => a + (b.beds ?? 0), 0);
  if (plan.kind === 'castle' || plan.fortress_id) {
    return `Castle · ${plan.kit_name ?? plan.kit_id ?? ''} · ${plan.status} · ${wallSegments(plan).length} wall segments, rings closed: ${s.wall_rings_closed ?? '—'} · ${shown.length} plots, ${workers} workers`;
  }
  if (plan.hamlet_id) {
    return `Hamlet · ${plan.role ?? ''} · ${plan.shape?.shape_id?.replace(/_/g, ' ') ?? 'compact'} · ${plan.status} · ${shown.filter(b => b.kind === 'service').length} services, ${shown.filter(b => b.kind === 'housing').length} cottages · ${workers} workers, ${beds} beds · shortfall ${s.housing_shortfall ?? '—'}`;
  }
  if (plan.location_id) {
    return `${plan.kind?.replace(/_/g, ' ')} · ${plan.arrangement ?? ''} · ${plan.status} · ${shown.length} plots · kit ${plan.kit_id}`;
  }
  return `${plan.city_class ?? ''} city · ${plan.shape?.shape_id?.replace(/_/g, ' ') ?? 'no suitable shape'} · ${plan.status} · ${shown.filter(b => b.kind === 'service').length} services, ${shown.filter(b => b.kind === 'housing').length} houses · ${workers} workers, ${beds} beds · shortfall ${s.housing_shortfall ?? '—'} · ${wallSegments(plan).length} wall segments`;
}
