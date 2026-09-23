// The control panel, built from the published catalogue (`fwg controls`). Seed, size and
// phase sit in the top bar; every other open control is here, in collapsible groups, with
// its default, range, units and description.
//
// Only controls someone has changed are sent as overrides. That matters beyond tidiness:
// several defaults (globe_radius, amplitude, settlement_spacing, ...) are *derived* from the
// world's width unless set, and sending the catalogue default would pin them instead.

import { el, num, titleCase } from './dom.js';

const TOP_BAR = new Set(['seed', 'size', 'phase']);

// Stage titles for the phase picker, from the CLI's own help; a loaded world's
// phases.titles replaces them.
export const STAGE_TITLES = [
  'Plate layout', 'Hypsometric surface', 'Surface detail', 'Erosion', 'Land mask and water', 'Tectonic transition',
  'Relic carving', 'Climate and biomes', 'Leylines, astrology, ecology', 'Founding: cities', 'Roads', 'Populated regions',
  'Nests, beasts, religion', 'An age turns', 'The second age turns', 'Plans, bands, heroes, key locations',
];

export class ControlsPanel {
  constructor({ groupsContainer, note, search, resetButton, seed, size, phase }) {
    this.groupsContainer = groupsContainer;
    this.note = note;
    this.search = search;
    this.seed = seed;
    this.size = size;
    this.phase = phase;
    this.catalogue = null;
    this.rows = new Map(); // control name -> {spec, input, row, group}
    this.setPhaseTitles(STAGE_TITLES);
    search.addEventListener('input', () => this.filter(search.value));
    resetButton.addEventListener('click', () => this.resetAll());
  }

  setPhaseTitles(titles) {
    const current = this.phase.value || '16';
    this.phase.replaceChildren(...titles.map((title, i) => el('option', { value: i + 1 }, `${i + 1} · ${title}`)));
    this.phase.value = current;
  }

  setCatalogue(catalogue) {
    this.catalogue = catalogue;
    const controls = catalogue.controls ?? {};
    for (const [name, input] of [['seed', this.seed], ['size', this.size]]) {
      const spec = controls[name];
      if (!spec) continue;
      input.min = spec.min;
      input.max = spec.max;
      input.title = spec.description;
    }
    const phaseSpec = controls.phase;
    if (phaseSpec) this.phase.title = phaseSpec.description;

    const groups = [...(catalogue.groups ?? [])].sort((a, b) => (a === 'World' ? -1 : b === 'World' ? 1 : a.localeCompare(b)));
    this.groupsContainer.replaceChildren();
    this.rows.clear();
    for (const group of groups) {
      const names = Object.keys(controls).filter(n => controls[n].group === group && !TOP_BAR.has(n)).sort();
      if (!names.length) continue;
      const edited = el('span', { class: 'edited' });
      const details = el('details', { class: 'control-group' },
        el('summary', {}, el('span', {}, group), el('span', {}, edited, ' ', el('span', { class: 'count' }, String(names.length)))));
      details._edited = edited;
      for (const name of names) details.append(this.buildControl(name, controls[name], details));
      this.groupsContainer.append(details);
    }
    const open = Object.keys(controls).length - TOP_BAR.size;
    this.note.textContent = `${open} open controls of ${catalogue.total ?? open} (recipe ${catalogue.recipe_version}). Only the ones you change are sent.`;
  }

  buildControl(name, spec, groupNode) {
    let input;
    if (spec.choices) {
      input = el('select', { name },
        spec.choices.map(choice => el('option', { value: choice }, spec.choice_labels?.[choice] ?? titleCase(choice))));
      input.value = String(spec.default);
      if (spec.choices.length === 1) input.disabled = true;
    } else {
      input = el('input', {
        name, type: 'number',
        step: spec.type === 'integer' ? '1' : 'any',
        min: spec.min, max: spec.max,
      });
      if (spec.derived_from) input.placeholder = `derived (${num(spec.default)})`;
      else input.value = String(spec.default);
    }
    input.setAttribute('aria-label', name);
    const reset = el('button', { type: 'button', class: 'reset', title: 'Back to the default' }, '↺');
    const range = spec.min !== undefined ? `${num(spec.min)} to ${num(spec.max)}` : spec.choices ? `${spec.choices.length} choices` : '';
    const meta = [
      `default ${typeof spec.default === 'number' ? num(spec.default) : spec.default}`,
      range,
      spec.units ? spec.units : null,
    ].filter(Boolean).join(' · ');
    const row = el('div', { class: 'control', title: spec.description },
      el('div', { class: 'control-row' },
        el('span', { class: 'control-name' }, name,
          spec.derived_from ? el('span', { class: 'badge', title: spec.note ?? '' }, 'derived') : null,
          spec.overrides ? el('span', { class: 'badge', title: `Overrides ${spec.overrides.join(', ')}` }, 'sets others') : null),
        input, reset),
      el('div', { class: 'control-meta' }, meta),
      el('p', { class: 'control-help' }, spec.description, spec.note ? ` ${spec.note}` : ''));
    const entry = { spec, input, row, group: groupNode };
    this.rows.set(name, entry);
    const update = () => this.markEdited(name);
    input.addEventListener('input', update);
    input.addEventListener('change', update);
    reset.addEventListener('click', () => {
      this.resetControl(name);
      this.markEdited(name);
    });
    return row;
  }

  isEdited(name) {
    const { spec, input } = this.rows.get(name);
    if (input.value === '') return false;
    if (spec.derived_from) return true;
    if (spec.choices) return input.value !== String(spec.default);
    return Number(input.value) !== Number(spec.default);
  }

  markEdited(name) {
    const entry = this.rows.get(name);
    entry.row.classList.toggle('edited', this.isEdited(name));
    const edited = [...this.rows.values()].filter(r => r.group === entry.group && r.row.classList.contains('edited')).length;
    entry.group._edited.textContent = edited ? `${edited} changed` : '';
  }

  resetControl(name) {
    const { spec, input } = this.rows.get(name);
    input.value = spec.derived_from && !spec.choices ? '' : String(spec.default);
  }

  resetAll() {
    for (const name of this.rows.keys()) {
      this.resetControl(name);
      this.markEdited(name);
    }
  }

  filter(text) {
    const needle = text.trim().toLowerCase();
    for (const group of this.groupsContainer.children) {
      let any = false;
      for (const [name, entry] of this.rows) {
        if (entry.group !== group) continue;
        const hit = !needle || name.includes(needle) || entry.spec.description?.toLowerCase().includes(needle);
        entry.row.hidden = !hit;
        any ||= hit;
      }
      group.hidden = !any;
      if (needle && any) group.open = true;
    }
  }

  /** Check every changed control; returns false (and shows the browser's message) if one is out of range. */
  validate() {
    for (const input of [this.seed, this.size]) if (!input.reportValidity()) return false;
    for (const [name, entry] of this.rows) {
      if (this.isEdited(name) && !entry.input.reportValidity()) {
        entry.group.open = true;
        return false;
      }
    }
    return true;
  }

  /** {seed, overrides} for the request: size always, phase when not 16, and whatever changed. */
  request() {
    const overrides = { size: Number(this.size.value) };
    if (Number(this.phase.value) !== 16) overrides.phase = Number(this.phase.value);
    for (const [name, { spec, input }] of this.rows) {
      if (!this.isEdited(name)) continue;
      overrides[name] = spec.type === 'string' ? input.value : Number(input.value);
    }
    return { seed: Number(this.seed.value), overrides };
  }

  /** Show a request (a cached world's) in the controls. */
  applyRequest(request) {
    if (!request) return;
    this.seed.value = request.seed;
    const overrides = request.overrides ?? {};
    if (overrides.size !== undefined) this.size.value = overrides.size;
    this.phase.value = String(overrides.phase ?? 16);
    for (const [name, entry] of this.rows) {
      if (Object.hasOwn(overrides, name)) entry.input.value = String(overrides[name]);
      else this.resetControl(name);
      this.markEdited(name);
    }
  }
}
