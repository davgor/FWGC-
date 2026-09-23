// What each layer means and how to colour it: labels and units, the groups the layer picker
// shows, colour ramps, categorical legends from the document's own catalogues, and relief
// shading.

import { el, num, rgb, titleCase } from './dom.js';

// --- names and units --------------------------------------------------------------------

// [label, unit, kind]. Kind picks the colouring: height (hypsometric tint), continuous,
// diverging (centred on zero), log, thermal, categorical, mask. Unlisted layers are
// described from their name and coloured by what their values turn out to be.
const INFO = {
  height: ['Elevation', 'm', 'height'],
  base: ['Baseline elevation', 'm', 'height'],
  surface: ['Surface before erosion', 'm', 'height'],
  structure: ['Tectonic elevation', 'm', 'height'],
  continental: ['Continental foundation', 'm', 'height'],
  pre_tectonic_height: ['Height before the second tectonic phase', 'm', 'height'],
  water_surface: ['Water or ground surface', 'm', 'height'],
  height_above_water: ['Height above water', 'm', 'diverging'],
  archipelago_relief: ['Archipelago relief', 'm', 'continuous'],
  slope: ['Slope', '°', 'continuous'],
  tpi: ['Valley / ridge position', 'm', 'diverging'],
  landform: ['Landform', '', 'categorical'],
  land: ['Land or submerged', '', 'mask'],
  erosion: ['Material eroded', 'm', 'continuous'],
  deposition: ['Material deposited', 'm', 'continuous'],
  erosion_delta: ['Net elevation change', 'm', 'diverging'],
  noise: ['Surface-noise contribution', 'm', 'diverging'],
  relic_incision: ['Relic bed incision', 'm', 'continuous'],
  plates: ['Plate', '', 'categorical'],
  moved_plates: ['Plates moved in the second phase', '', 'categorical'],
  crust: ['Continental affinity', '0 oceanic, 1 continental', 'continuous'],
  convergence: ['Convergence', 'relative', 'continuous'],
  divergence: ['Divergence', 'relative', 'continuous'],
  shear: ['Shear', 'relative', 'continuous'],
  second_convergence: ['Second-phase convergence', 'relative', 'continuous'],
  second_divergence: ['Second-phase divergence', 'relative', 'continuous'],
  second_uplift: ['Second tectonic displacement', 'm', 'diverging'],
  boundary_distance: ['Distance to a plate boundary', 'm', 'continuous'],
  interaction: ['Boundary elevation contribution', 'm', 'diverging'],
  volcanic: ['Volcanic-arc potential', 'relative', 'continuous'],
  water_type: ['Connected water', '', 'categorical'],
  old_water_type: ['Connected water before the ages', '', 'categorical'],
  water_depth: ['Water depth', 'm', 'continuous'],
  water_distance: ['Water distance', 'as written', 'continuous'],
  river: ['Rivers', '', 'mask'],
  rain_river: ['Rain-fed rivers', '', 'mask'],
  old_river: ['Old river paths', '', 'mask'],
  gorge: ['Dry gorges', '', 'mask'],
  dry_valley: ['Dried valleys', '', 'mask'],
  relic_gorge: ['Relic gorges', '', 'mask'],
  relic_valley: ['Relic valleys', '', 'mask'],
  catchment: ['Upstream catchment', 'm²', 'log'],
  routed_catchment: ['Routed catchment', 'm²', 'log'],
  rain_runoff: ['Rain-weighted runoff', 'm² equivalent', 'log'],
  freshwater_distance: ['Freshwater route distance', 'm; −1 none', 'continuous'],
  flood_risk: ['Flood risk', 'relative', 'continuous'],
  temperature: ['Temperature', '°C', 'thermal'],
  moisture: ['Moisture', '0 dry, 1 wet', 'continuous'],
  climate_moisture: ['Climate moisture', '0 dry, 1 wet', 'continuous'],
  rainfall: ['Rainfall potential', 'relative', 'continuous'],
  air_moisture: ['Air moisture', 'relative', 'continuous'],
  wind_uplift: ['Upwind terrain rise', 'm', 'continuous'],
  boreal: ['Boreal', '', 'mask'],
  tundra: ['Tundra', '', 'mask'],
  ice_cap: ['Ice cap', '', 'mask'],
  biome: ['Biome', '', 'categorical'],
  natural_biome: ['Natural biome', '', 'categorical'],
  biome_variant: ['Magical biome', '', 'categorical'],
  dominant_magic: ['Dominant school', '', 'categorical'],
  magic_density: ['Magic density', 'relative', 'continuous'],
  magic_hazard: ['Mutation hazard', 'relative', 'continuous'],
  magic_growth: ['Growth affinity', '0 destructive, 1 growth', 'continuous'],
  magic_opposition: ['Magic opposition', 'relative', 'continuous'],
  lunar_sensitivity: ['Lunar sensitivity', 'relative', 'continuous'],
  culture_region: ['Culture region', '', 'categorical'],
  civilization_region: ['Civilization', '', 'categorical'],
  hamlet_catchment: ['Hamlet catchment', '', 'categorical'],
  fishing_ground_owner: ['Fishing ground owner', '', 'categorical'],
  suitability: ['Settlement suitability', 'relative', 'continuous'],
  food_potential: ['Adapted food potential', 'relative', 'continuous'],
  natural_food_potential: ['Natural food potential', 'relative', 'continuous'],
  irrigation_benefit: ['Irrigation benefit', 'relative', 'continuous'],
  resource_potential: ['Resource potential', 'relative', 'continuous'],
  metal_richness: ['Metal richness', 'relative', 'continuous'],
  maritime: ['Maritime access', 'relative', 'continuous'],
  open_ocean: ['Open ocean', 'relative', 'continuous'],
  salinity: ['Salinity', 'relative', 'continuous'],
  harbor_suitability: ['Harbour suitability', 'relative', 'continuous'],
  fishing_productivity: ['Fishing productivity', 'relative', 'continuous'],
  coastal_exposure: ['Coastal exposure', 'relative', 'continuous'],
  coastal_support: ['Coastal support', 'relative', 'continuous'],
  island_habitat: ['Island habitat', '', 'mask'],
};

const PATTERNS = [
  [/^ley_(.+)$/, m => [`${titleCase(m[1])} ley potency`, 'relative', 'continuous']],
  [/^instability_(.+)$/, m => [`${titleCase(m[1])} instability`, 'relative', 'continuous']],
  [/^zone_(.+)$/, m => [`${titleCase(m[1])} region`, 'intensity', 'continuous']],
  [/^suitability_(.+)$/, m => [`Suitability: ${titleCase(m[1])}`, 'relative', 'continuous']],
  [/^life_capacity_(.+)$/, m => [`Life capacity: ${titleCase(m[1])}`, 'relative', 'continuous']],
  [/^magic_risk_(.+)$/, m => [`Magic risk: ${titleCase(m[1])}`, 'relative', 'continuous']],
  [/^(.+)_support_reach$/, m => [`Support reach: ${titleCase(m[1])}`, '', 'mask']],
];

export function layerInfo(name) {
  if (INFO[name]) {
    const [label, unit, kind] = INFO[name];
    return { name, label, unit, kind };
  }
  for (const [pattern, make] of PATTERNS) {
    const match = pattern.exec(name);
    if (match) {
      const [label, unit, kind] = make(match);
      return { name, label, unit, kind };
    }
  }
  return { name, label: titleCase(name), unit: '', kind: null };
}

// The layer picker's groups, in order; a layer joins the first group whose test it passes.
const GROUPS = [
  ['Terrain', n => ['height', 'base', 'surface', 'structure', 'continental', 'pre_tectonic_height', 'height_above_water',
    'archipelago_relief', 'slope', 'tpi', 'landform', 'land', 'erosion', 'deposition', 'erosion_delta', 'noise', 'relic_incision'].includes(n)],
  ['Tectonics', n => ['plates', 'moved_plates', 'crust', 'convergence', 'divergence', 'shear', 'boundary_distance', 'interaction',
    'volcanic', 'second_uplift', 'second_convergence', 'second_divergence'].includes(n)],
  ['Water', n => /^(water_|river$|rain_river|old_river|old_water_type|catchment|routed_catchment|rain_runoff|gorge|dry_valley|relic_gorge|relic_valley|freshwater_distance|flood_risk)/.test(n)],
  ['Climate', n => ['temperature', 'moisture', 'climate_moisture', 'rainfall', 'air_moisture', 'wind_uplift', 'boreal', 'tundra', 'ice_cap'].includes(n)],
  ['Biomes', n => ['biome', 'natural_biome', 'biome_variant'].includes(n)],
  ['Coasts and sea', n => ['coastal_exposure', 'coastal_support', 'estuary', 'fjord', 'kelp', 'lagoon', 'reef', 'salinity', 'sheltered_bay',
    'rocky_coast', 'open_ocean', 'maritime', 'harbor_suitability', 'fishing_productivity', 'fishing_ground_owner', 'island_habitat'].includes(n)],
  ['Magic', n => /^(ley_|instability_|magic_(density|hazard|growth|opposition)|dominant_magic|lunar_sensitivity)/.test(n)],
  ['Magical regions', n => n.startsWith('zone_')],
  ['People', n => /^(suitability|life_capacity_|magic_risk_|culture_region|civilization_region|hamlet_catchment|food_potential|natural_food_potential|irrigation_benefit|resource_potential|metal_richness)/.test(n) || n.endsWith('_support_reach')],
  ['Other', () => true],
];

/** [[group, [layer names...]], ...] in picker order. */
export function groupLayers(names) {
  const groups = new Map(GROUPS.map(([group]) => [group, []]));
  for (const name of names) {
    const [group] = GROUPS.find(([, test]) => test(name));
    groups.get(group).push(name);
  }
  for (const list of groups.values()) list.sort((a, b) => layerInfo(a).label.localeCompare(layerInfo(b).label));
  return [...groups].filter(([, list]) => list.length);
}

// --- colour ramps ---------------------------------------------------------------------

// Sequential: viridis, sampled.
const SEQUENTIAL = [[68, 1, 84], [72, 40, 120], [62, 74, 137], [49, 104, 142], [38, 130, 142], [31, 158, 137],
  [53, 183, 121], [109, 205, 89], [180, 222, 44], [253, 231, 37]];
// Diverging, centred on zero: blue, near-white, red.
const DIVERGING = [[43, 76, 148], [103, 156, 205], [225, 228, 230], [226, 128, 88], [163, 32, 42]];
// Temperature: cold blue through temperate green to hot red.
const THERMAL = [[58, 76, 192], [99, 150, 226], [170, 212, 232], [150, 200, 130], [243, 205, 104], [228, 110, 60], [170, 30, 40]];
// Hypsometric tints, by fraction of the world's highest land and deepest water.
const LAND = [[0, [96, 148, 82]], [0.08, [132, 170, 96]], [0.2, [186, 192, 126]], [0.38, [196, 164, 112]],
  [0.6, [160, 120, 92]], [0.8, [176, 166, 156]], [1, [246, 246, 244]]];
const SEA = [[0, [118, 178, 216]], [0.1, [76, 142, 196]], [0.35, [48, 102, 162]], [0.7, [31, 66, 122]], [1, [18, 36, 80]]];

function lerp(a, b, t) {
  return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
}

function sampleRamp(ramp, t) {
  const clamped = Math.max(0, Math.min(1, Number.isFinite(t) ? t : 0));
  const scaled = clamped * (ramp.length - 1);
  const i = Math.min(ramp.length - 2, Math.floor(scaled));
  return lerp(ramp[i], ramp[i + 1], scaled - i);
}

function sampleStops(stops, t) {
  const clamped = Math.max(0, Math.min(1, t));
  for (let i = 1; i < stops.length; i++) {
    if (clamped <= stops[i][0]) {
      const [t0, c0] = stops[i - 1];
      const [t1, c1] = stops[i];
      return lerp(c0, c1, (clamped - t0) / (t1 - t0 || 1));
    }
  }
  return stops[stops.length - 1][1];
}

function cssGradient(sample, steps = 12) {
  const parts = [];
  for (let i = 0; i <= steps; i++) parts.push(`${rgb(sample(i / steps).map(Math.round))} ${((100 * i) / steps).toFixed(1)}%`);
  return `linear-gradient(to right, ${parts.join(', ')})`;
}

/** A distinct colour for category `i`, stepping hue by the golden angle. */
export function categoryColor(i) {
  const hue = (((i * 137.508) % 360) + 360) % 360;
  return hslToRgb(hue, 0.55, i % 2 ? 0.5 : 0.6);
}

function hslToRgb(h, s, l) {
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const hp = h / 60;
  const x = c * (1 - Math.abs((hp % 2) - 1));
  const [r, g, b] = hp < 1 ? [c, x, 0] : hp < 2 ? [x, c, 0] : hp < 3 ? [0, c, x] : hp < 4 ? [0, x, c] : hp < 5 ? [x, 0, c] : [c, 0, x];
  const m = l - c / 2;
  return [Math.round((r + m) * 255), Math.round((g + m) * 255), Math.round((b + m) * 255)];
}

const UNASSIGNED = [42, 52, 60];
const MASK_OFF = [34, 43, 51];

// --- colorizers -------------------------------------------------------------------------

/** Blocks a layer's colours or names come from. */
export function cataloguesFor(name) {
  if (['biome', 'natural_biome', 'biome_variant', 'landform'].includes(name)) return ['terrain'];
  if (name === 'dominant_magic') return ['magic'];
  if (name === 'civilization_region') return ['civilizations'];
  return [];
}

/** Other layers a layer's colouring reads. */
export function companionsFor(name) {
  if (name === 'biome_variant') return ['natural_biome'];
  if (INFO[name]?.[2] === 'height') return ['water_type'];
  return [];
}

/**
 * How to colour `layer`: returns {kind, rgb(value, cellIndex), describe(value), legend()}.
 * `catalogues` holds whatever blocks cataloguesFor() named, `companions` the layers
 * companionsFor() named (either may be missing), `seaLevel` the world's datum.
 */
export function makeColorizer(layer, { catalogues = {}, companions = {}, seaLevel = 0 } = {}) {
  const info = layerInfo(layer.name);
  let kind = info.kind;
  if (!kind) {
    const values = layer.distinct ? [...layer.distinct.keys()] : null;
    if (values && layer.integer && values.every(v => v === 0 || v === 1)) kind = 'mask';
    else if (layer.integer && layer.distinct) kind = 'categorical';
    else kind = 'continuous';
  }
  if (kind === 'mask' && !(layer.integer && layer.distinct)) kind = 'continuous';
  if (kind === 'categorical' && !layer.integer) kind = 'continuous';

  if (layer.constant) return constantColorizer(layer, info);
  if (kind === 'height') return heightColorizer(layer, info, companions.water_type, seaLevel);
  if (kind === 'categorical') return categoricalColorizer(layer, info, catalogues, companions);
  if (kind === 'mask') return maskColorizer(layer, info);
  return rampColorizer(layer, info, kind);
}

function unitSuffix(info) {
  return info.unit ? ` ${info.unit}` : '';
}

function constantColorizer(layer, info) {
  const color = [70, 84, 96];
  return {
    kind: 'constant',
    rgb: () => color,
    describe: v => `${num(v)}${unitSuffix(info)}`,
    legend: () => el('span', { class: 'item' }, el('span', { class: 'chip', style: { background: rgb(color) } }),
      `${info.label}: ${num(layer.min)} everywhere (this layer is constant in this world)`),
  };
}

function rampColorizer(layer, info, kind) {
  let lo = layer.min;
  let hi = layer.max;
  let transform = v => v;
  let ramp = SEQUENTIAL;
  if (kind === 'diverging') {
    const m = Math.max(Math.abs(lo), Math.abs(hi)) || 1;
    lo = -m;
    hi = m;
    ramp = DIVERGING;
  } else if (kind === 'thermal') {
    ramp = THERMAL;
  } else if (kind === 'log') {
    transform = v => Math.log10(Math.max(v, 0) + 1);
  }
  const tlo = transform(lo);
  const thi = transform(hi);
  const span = thi - tlo || 1;
  const cache = new Array(256);
  const colorAt = t => {
    const bucket = Math.max(0, Math.min(255, Math.round(t * 255)));
    return (cache[bucket] ??= sampleRamp(ramp, bucket / 255).map(Math.round));
  };
  return {
    kind,
    rgb: v => colorAt((transform(v) - tlo) / span),
    describe: v => `${num(v)}${unitSuffix(info)}`,
    legend: () => el('span', { class: 'item' },
      el('span', { class: 'title' }, info.label),
      el('span', {},
        el('div', { class: 'ramp', style: { background: cssGradient(t => sampleRamp(ramp, t)) } }),
        el('div', { class: 'ends' }, el('span', {}, num(lo)), kind === 'log' ? el('span', {}, 'log scale') : null, el('span', {}, num(hi)))),
      info.unit ? el('span', { class: 'more' }, info.unit) : null),
  };
}

function heightColorizer(layer, info, water, seaLevel) {
  const top = Math.max(layer.max - seaLevel, 1);
  const deep = Math.max(seaLevel - layer.min, 1);
  const lake = [104, 170, 210];
  const cacheLand = new Array(256);
  const cacheSea = new Array(256);
  const land = h => cacheLand[Math.round(Math.max(0, Math.min(1, (h - seaLevel) / top)) * 255)]
    ??= sampleStops(LAND, Math.max(0, Math.min(1, (h - seaLevel) / top))).map(Math.round);
  const sea = h => cacheSea[Math.round(Math.max(0, Math.min(1, (seaLevel - h) / deep)) * 255)]
    ??= sampleStops(SEA, Math.max(0, Math.min(1, (seaLevel - h) / deep))).map(Math.round);
  return {
    kind: 'height',
    rgb: (h, i) => {
      if (water && water.values[i] === 2) return lake;
      return h > seaLevel ? land(h) : sea(h);
    },
    describe: h => `${num(h)} m${h > seaLevel ? '' : ' (below sea level)'}`,
    legend: () => {
      const sample = t => {
        // left third water, right two thirds land, sea level at the join
        return t < 1 / 3 ? sampleStops(SEA, 1 - t * 3) : sampleStops(LAND, (t - 1 / 3) * 1.5);
      };
      return el('span', { class: 'item' },
        el('span', { class: 'title' }, info.label),
        el('span', {},
          el('div', { class: 'ramp', style: { background: cssGradient(sample, 24) } }),
          el('div', { class: 'ends' }, el('span', {}, `${num(layer.min)} m`), el('span', {}, `sea level ${num(seaLevel)}`), el('span', {}, `${num(layer.max)} m`))),
        water ? el('span', { class: 'item' }, el('span', { class: 'chip', style: { background: rgb(lake) } }), 'lake') : null);
    },
  };
}

function maskColorizer(layer, info) {
  const on = /river|water|gorge|valley|ice|lagoon|reef|kelp|estuary|fjord/.test(layer.name) ? [104, 190, 230]
    : /tundra|boreal/.test(layer.name) ? [196, 214, 190] : [150, 206, 128];
  return {
    kind: 'mask',
    rgb: v => (v >= 0.5 ? on : MASK_OFF),
    describe: v => (v >= 0.5 ? 'yes' : 'no'),
    legend: () => [
      el('span', { class: 'title' }, info.label),
      el('span', { class: 'item' }, el('span', { class: 'chip', style: { background: rgb(on) } }), `yes (${num(layer.distinct?.get(1) ?? 0)} cells)`),
      el('span', { class: 'item' }, el('span', { class: 'chip', style: { background: rgb(MASK_OFF) } }), 'no'),
    ],
  };
}

// Categories: each value gets a colour and a name, from the document where it has them.
function categoricalColorizer(layer, info, catalogues, companions) {
  const classes = new Map(); // value -> {color, name}
  const terrain = catalogues.terrain;
  const name = layer.name;
  const fallback = v => (v < 0 ? { color: UNASSIGNED, name: 'unassigned' } : { color: categoryColor(v), name: `${info.label} ${v}` });

  if ((name === 'biome' || name === 'natural_biome') && terrain?.biomes) {
    for (const b of terrain.biomes) classes.set(b.id, { color: b.color, name: b.name });
  } else if (name === 'biome_variant' && terrain?.magical_biomes) {
    terrain.magical_biomes.forEach((b, index) => classes.set(index, { color: b.color, name: `${b.name} (${b.magic_school})` }));
  } else if (name === 'landform') {
    const names = terrain?.landforms ?? ['Submerged', 'Plain / hillside', 'Mountain / ridge', 'Valley'];
    const colors = [[48, 102, 150], [139, 176, 99], [151, 128, 108], [94, 158, 151]];
    names.forEach((label, i) => classes.set(i, { color: colors[i] ?? categoryColor(i), name: label }));
  } else if (name === 'water_type' || name === 'old_water_type') {
    classes.set(0, { color: [150, 170, 110], name: 'dry ground' });
    classes.set(1, { color: [42, 96, 150], name: 'ocean' });
    classes.set(2, { color: [96, 170, 206], name: 'lake' });
  } else if (name === 'dominant_magic' && catalogues.magic) {
    const order = catalogues.magic.school_order ?? Object.keys(catalogues.magic.networks ?? {});
    order.forEach((school, i) => classes.set(i, { color: catalogues.magic.networks?.[school]?.color ?? categoryColor(i), name: titleCase(school) }));
    classes.set(-1, { color: UNASSIGNED, name: 'contested or weak' });
  } else if (name === 'civilization_region' && catalogues.civilizations?.entities) {
    for (const e of catalogues.civilizations.entities) {
      classes.set(e.region_index, { color: e.presentation?.map_color_rgb ?? categoryColor(e.region_index), name: e.name });
    }
    classes.set(-1, { color: UNASSIGNED, name: 'unclaimed wilderness' });
  }

  // biome_variant's -1 is "no magical variant": show the natural biome beneath, darkened.
  const natural = name === 'biome_variant' ? companions.natural_biome : null;
  const naturalColors = new Map((terrain?.biomes ?? []).map(b => [b.id, b.color.map(c => Math.round(c * 0.55))]));
  const lookup = v => classes.get(v) ?? fallback(v);
  const colorCache = new Map();
  return {
    kind: 'categorical',
    rgb: (v, i) => {
      if (natural && v < 0) return naturalColors.get(natural.values[i]) ?? UNASSIGNED;
      let c = colorCache.get(v);
      if (!c) colorCache.set(v, (c = lookup(v).color));
      return c;
    },
    describe: v => (natural && v < 0 ? 'no magical variant' : lookup(v).name),
    legend: () => {
      const present = layer.distinct ? [...layer.distinct].sort((a, b) => b[1] - a[1]) : [];
      const shown = present.slice(0, 28);
      const items = shown.map(([v, count]) => {
        const entry = natural && v < 0 ? { color: [60, 70, 60], name: 'no magical variant (natural biome, darkened)' } : lookup(v);
        return el('span', { class: 'item', title: `${num(count)} cells` }, el('span', { class: 'chip', style: { background: rgb(entry.color) } }), entry.name);
      });
      return [el('span', { class: 'title' }, info.label), ...items,
        present.length > shown.length ? el('span', { class: 'more' }, `and ${present.length - shown.length} more`) : null];
    },
  };
}

// --- relief shading -----------------------------------------------------------------

/**
 * Brightness multipliers for relief shading, one per cell of the n-1 unique columns: the
 * hillshade, applied fully on land and faintly under water, so coasts and mountains read
 * without the sea floor turning to noise.
 */
export function reliefShading(height, radius, seaLevel = 0) {
  const factors = hillshade(height, radius, 2, seaLevel);
  const width = height.cols - 1;
  const out = new Float32Array(factors.length);
  for (let z = 0; z < height.rows; z++) {
    for (let x = 0; x < width; x++) {
      const i = z * width + x;
      const weight = height.values[z * height.cols + x] > seaLevel ? 0.7 : 0.3;
      out[i] = Math.max(0.5, Math.min(1.3, 1 + (factors[i] - 1) * weight));
    }
  }
  return out;
}

/**
 * Lambertian hillshade from a height layer, one factor per cell of the n-1 unique columns
 * (the seam column is dropped): about 1 on flat ground, brighter facing the light (from
 * the north-west, 45° up), darker facing away. Slopes are measured in metres on the
 * sphere, with the east-west spacing narrowing toward the poles, and exaggerated so that
 * relief a few kilometres high reads on cells kilometres wide. A land cell sees the water
 * beside it at sea level, so a coast is not shaded as a cliff down to the sea floor.
 */
export function hillshade(height, radius, exaggeration = 2.5, seaLevel = -Infinity) {
  const rows = height.rows;
  const cols = height.cols;
  const width = cols - 1;
  const raw = height.values;
  const landFloor = new Float64Array(raw.length);
  for (let i = 0; i < raw.length; i++) landFloor[i] = Math.max(raw[i], seaLevel);
  const out = new Float32Array(rows * width);
  const dLon = (2 * Math.PI) / width;
  const dLat = Math.PI / (rows - 1);
  const dy = radius * dLat;
  // light from azimuth 315° (north-west), altitude 45°; x east, y north, z up
  const light = [-Math.SQRT1_2 * Math.SQRT1_2, Math.SQRT1_2 * Math.SQRT1_2, Math.SQRT1_2];
  const flat = light[2];
  for (let z = 0; z < rows; z++) {
    const lat = Math.PI / 2 - z * dLat;
    const dx = radius * Math.max(Math.cos(lat), 0.12) * dLon;
    const up = Math.max(z - 1, 0);
    const down = Math.min(z + 1, rows - 1);
    for (let x = 0; x < width; x++) {
      const h = raw[z * cols + x] > seaLevel ? landFloor : raw;
      const west = (x - 1 + width) % width;
      const east = (x + 1) % width;
      const dzdx = ((h[z * cols + east] - h[z * cols + west]) / (2 * dx)) * exaggeration;
      // rows run southward, so north is up-row
      const dzdn = ((h[up * cols + x] - h[down * cols + x]) / ((down - up) * dy || 1)) * exaggeration;
      const length = Math.hypot(dzdx, dzdn, 1);
      const shade = (-dzdx * light[0] - dzdn * light[1] + light[2]) / length;
      out[z * width + x] = Math.max(0, shade) / flat;
    }
  }
  return out;
}
