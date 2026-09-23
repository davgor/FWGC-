// Running fwg.exe: the control catalogue, and generation into a content-addressed cache.
//
// A world is named by a hash of its request and of the executable that answers it.
// Generation is deterministic for one build of the generator, so a repeated request is
// served from the cache; a rebuilt fwg.exe is a different generator, and its worlds get new
// names rather than being confused with the old one's.

import { spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import fsp from 'node:fs/promises';
import path from 'node:path';

export const RECIPE_VERSION = 3;

// Where a CMake build puts the CLI: the Visual Studio generator adds a configuration folder,
// single-configuration generators (Ninja, Makefiles) do not, and only Windows adds .exe.
const BUILD_OUTPUTS = [
  'build/apps/Release/fwg.exe',
  'build/apps/Release/fwg',
  'build/apps/fwg.exe',
  'build/apps/fwg',
];

export const BUILD_HINT = [
  'Build it from the generator folder:',
  '  cd generator',
  '  cmake -S . -B build -G "Visual Studio 17 2022" -A x64',
  '  cmake --build build --config Release --target fwg_cli',
  'or point FWG_EXE at an fwg executable.',
].join('\n');

/** Where fwg is: FWG_EXE if set, else the first build output that exists, else the default. */
export function locateExecutable(viewerDir, env = process.env) {
  if (env.FWG_EXE) return { path: path.resolve(env.FWG_EXE), source: 'FWG_EXE' };
  const generatorDir = path.resolve(viewerDir, '..', 'generator');
  for (const relative of BUILD_OUTPUTS) {
    const candidate = path.join(generatorDir, relative);
    if (fs.existsSync(candidate)) return { path: candidate, source: 'build' };
  }
  return { path: path.join(generatorDir, BUILD_OUTPUTS[0]), source: 'default' };
}

/** An error with an HTTP status, and optionally a JSON body to send verbatim. */
export class GeneratorError extends Error {
  constructor(status, code, message, rawBody = null) {
    super(message);
    this.status = status;
    this.code = code;
    this.rawBody = rawBody; // a CLI failure document, passed through as-is
  }
}

// JSON with object keys sorted at every level, so {a, b} and {b, a} hash alike.
function canonicalJson(value) {
  if (Array.isArray(value)) return `[${value.map(canonicalJson).join(',')}]`;
  if (value && typeof value === 'object') {
    return `{${Object.keys(value).sort().map(k => `${JSON.stringify(k)}:${canonicalJson(value[k])}`).join(',')}}`;
  }
  return JSON.stringify(value);
}

/**
 * The error for an fwg run that did not exit 0. A refusal -- exit 2 with fwg's failure
 * document on stdout -- keeps that document to be passed through untouched. Other failures
 * print one line on stderr, "fwg: <reason>"; a request the library rejects outright (an
 * even size, say) exits 2 that way too, so exit 2 is the caller's fault unless the reason
 * is the file system's.
 */
function failureOf(result) {
  const stdout = result.stdout.trim();
  if (result.code === 2 && stdout.startsWith('{')) {
    let failure = null;
    try {
      failure = JSON.parse(stdout);
    } catch {
      // not a document after all; fall through to the plain error
    }
    if (failure) return new GeneratorError(400, 'refused', failure.message ?? 'Request refused', stdout);
  }
  const reason = result.stderr.trim().replace(/^fwg:\s*/, '') || `fwg exited with code ${result.code}`;
  const callers = result.code === 2 && !/^cannot (write|read)/.test(reason);
  return new GeneratorError(callers ? 400 : 500, callers ? 'invalid_request' : 'fwg_failed', reason);
}

function run(exe, args, { stdoutLimit = 64 * 1024 * 1024 } = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(exe, args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    const out = [];
    const err = [];
    let outBytes = 0;
    child.stdout.on('data', chunk => {
      outBytes += chunk.length;
      if (outBytes <= stdoutLimit) out.push(chunk);
    });
    child.stderr.on('data', chunk => err.push(chunk));
    child.on('error', reject);
    child.on('close', code => resolve({
      code,
      stdout: Buffer.concat(out).toString('utf8'),
      stderr: Buffer.concat(err).toString('utf8'),
    }));
  });
}

export class Generator {
  constructor({ viewerDir, cacheDir, cacheLimitBytes, env = process.env, log = console.log }) {
    this.viewerDir = viewerDir;
    this.cacheDir = cacheDir;
    this.cacheLimitBytes = cacheLimitBytes;
    this.env = env;
    this.log = log;
    this.exe = locateExecutable(viewerDir, env);
    this._fingerprint = null; // {key, hash}
    this._controls = null; // {fingerprint, value}
    this.queue = Promise.resolve(); // generations run one at a time
    this.inflight = new Map(); // id -> Promise of a generation already queued
    this.protect = () => false; // ids the cache must not evict (open in the viewer)
  }

  /** The executable's state, re-checked on every call: it may be built while we run. */
  status() {
    if (this.exe.source !== 'FWG_EXE') this.exe = locateExecutable(this.viewerDir, this.env);
    const stat = fs.statSync(this.exe.path, { throwIfNoEntry: false });
    return { path: this.exe.path, source: this.exe.source, found: Boolean(stat?.isFile()), size: stat?.size ?? 0, mtimeMs: stat?.mtimeMs ?? 0 };
  }

  requireExecutable() {
    const status = this.status();
    if (!status.found) {
      const how = status.source === 'FWG_EXE' ? `FWG_EXE points at ${status.path}, which does not exist.` : `fwg was not found at ${status.path}.`;
      throw new GeneratorError(503, 'fwg_missing', `${how}\n${BUILD_HINT}`);
    }
    return status;
  }

  /** SHA-256 of the executable, recomputed only when its size or time changes. */
  async fingerprint() {
    const status = this.requireExecutable();
    const key = `${status.path}|${status.size}|${status.mtimeMs}`;
    if (this._fingerprint?.key !== key) {
      const hash = crypto.createHash('sha256').update(await fsp.readFile(status.path)).digest('hex');
      this._fingerprint = { key, hash };
    }
    return this._fingerprint.hash;
  }

  /** `fwg controls`, parsed; cached for as long as the executable is unchanged. */
  async controls() {
    const fingerprint = await this.fingerprint();
    if (this._controls?.fingerprint === fingerprint) return this._controls.value;
    const result = await run(this.exe.path, ['controls']);
    if (result.code !== 0) {
      throw new GeneratorError(500, 'fwg_failed', `fwg controls exited ${result.code}: ${result.stderr.trim() || result.stdout.trim()}`);
    }
    const value = JSON.parse(result.stdout);
    this._controls = { fingerprint, value };
    return value;
  }

  worldFile(id) {
    return path.join(this.cacheDir, `${id}.world.json`);
  }

  metaFile(id) {
    return path.join(this.cacheDir, `${id}.meta.json`);
  }

  /** The file behind an id, or null when the cache does not hold it. */
  async resolveWorld(id) {
    if (!/^[0-9a-f]{8,64}$/.test(id)) return null;
    const file = this.worldFile(id);
    return fs.existsSync(file) ? file : null;
  }

  async readMeta(id) {
    try {
      return JSON.parse(await fsp.readFile(this.metaFile(id), 'utf8'));
    } catch {
      return null;
    }
  }

  // Recency for eviction lives on the small meta file. Touching the world file itself would
  // make every open index of it look stale.
  async touch(id) {
    const now = new Date();
    await fsp.utimes(this.metaFile(id), now, now).catch(() => {});
  }

  /** Every cached world, newest first. */
  async listWorlds() {
    let names = [];
    try {
      names = await fsp.readdir(this.cacheDir);
    } catch {
      return [];
    }
    const worlds = [];
    for (const name of names) {
      const match = /^([0-9a-f]+)\.world\.json$/.exec(name);
      if (!match) continue;
      const stat = await fsp.stat(path.join(this.cacheDir, name)).catch(() => null);
      if (!stat) continue;
      const meta = await this.readMeta(match[1]);
      const metaStat = await fsp.stat(this.metaFile(match[1])).catch(() => null);
      worlds.push({
        id: match[1],
        bytes: stat.size,
        ms: meta?.ms ?? null,
        created: meta?.created ?? new Date(stat.mtimeMs).toISOString(),
        used: new Date(Math.max(stat.mtimeMs, metaStat?.mtimeMs ?? 0)).toISOString(),
        request: meta?.request ?? null,
      });
    }
    return worlds.sort((a, b) => b.used.localeCompare(a.used));
  }

  /**
   * Build the CLI request from a POST body {seed, overrides}. Only the shape is checked
   * here; the values are the CLI's to judge, and its refusal is what the caller sees.
   */
  static requestFrom(body) {
    if (!body || typeof body !== 'object' || Array.isArray(body)) {
      throw new GeneratorError(400, 'bad_request', 'The body must be a JSON object: {"seed": N, "overrides": {control: value}}.');
    }
    const unknown = Object.keys(body).filter(k => !['seed', 'overrides', 'recipe_version'].includes(k));
    if (unknown.length) {
      throw new GeneratorError(400, 'bad_request', `Unknown field${unknown.length > 1 ? 's' : ''} ${unknown.map(k => `'${k}'`).join(', ')}; the body takes seed and overrides.`);
    }
    if (body.seed === undefined) throw new GeneratorError(400, 'bad_request', 'seed is required.');
    const overrides = body.overrides ?? {};
    if (!overrides || typeof overrides !== 'object' || Array.isArray(overrides)) {
      throw new GeneratorError(400, 'bad_request', 'overrides must be an object of control name to value.');
    }
    return { seed: body.seed, recipe_version: body.recipe_version ?? RECIPE_VERSION, overrides };
  }

  /** A world's id: its request, and the build of fwg that answers it. */
  static idOf(fingerprint, request) {
    return crypto.createHash('sha256').update(`${fingerprint}\n${canonicalJson(request)}`).digest('hex').slice(0, 24);
  }

  /** Generate, or find in the cache, the world for a POST body. Returns {id, bytes, ms, cached}. */
  async generate(body) {
    const request = Generator.requestFrom(body);
    // The build that names the world is the build recorded as having made it.
    const fingerprint = await this.fingerprint();
    const id = Generator.idOf(fingerprint, request);
    const file = this.worldFile(id);
    const stat = await fsp.stat(file).catch(() => null);
    if (stat) {
      let meta = await this.readMeta(id);
      if (!meta) {
        // A world whose meta was lost: the id says which request and build made it.
        meta = { id, request, bytes: stat.size, ms: null, created: new Date(stat.mtimeMs).toISOString(), fwg: { path: this.exe.path, sha256: fingerprint } };
        await this.writeMeta(meta);
      }
      await this.touch(id);
      return { id, bytes: stat.size, ms: meta.ms ?? null, cached: true };
    }
    let job = this.inflight.get(id);
    if (!job) {
      job = this.queue.then(() => this.runGeneration(id, request, fingerprint));
      this.queue = job.catch(() => {});
      this.inflight.set(id, job);
      job.finally(() => this.inflight.delete(id)).catch(() => {});
    }
    return job;
  }

  async writeMeta(meta) {
    const target = this.metaFile(meta.id);
    await fsp.writeFile(`${target}.partial`, `${JSON.stringify(meta, null, 2)}\n`);
    await fsp.rename(`${target}.partial`, target);
  }

  async runGeneration(id, request, fingerprint) {
    const status = this.requireExecutable();
    await fsp.mkdir(this.cacheDir, { recursive: true });
    const requestFile = path.join(this.cacheDir, `${id}.request.json`);
    const partial = path.join(this.cacheDir, `${id}.world.json.partial`);
    await fsp.writeFile(requestFile, `${JSON.stringify(request, null, 2)}\n`);
    const started = performance.now();
    let result;
    try {
      result = await run(status.path, ['generate', '--request', requestFile, '-o', partial, '--quiet', '--include-timings']);
    } catch (error) {
      throw new GeneratorError(503, 'fwg_failed', `Could not run ${status.path}: ${error.message}\n${BUILD_HINT}`);
    }
    const ms = Math.round(performance.now() - started);

    if (result.code !== 0) {
      await fsp.rm(partial, { force: true });
      await fsp.rm(requestFile, { force: true });
      throw failureOf(result);
    }

    // Meta first, then the world into place: a world file in the cache always has its meta.
    const { size } = await fsp.stat(partial);
    const meta = { id, request, bytes: size, ms, created: new Date().toISOString(), fwg: { path: status.path, sha256: fingerprint } };
    await this.writeMeta(meta);
    const file = this.worldFile(id);
    await fsp.rename(partial, file);
    this.log(`generated ${id}: seed ${request.seed}, ${JSON.stringify(request.overrides)} -> ${(size / 1e6).toFixed(1)} MB in ${(ms / 1000).toFixed(1)} s`);
    await this.evict(id).catch(error => this.log(`cache eviction failed: ${error.message}`));
    return { id, bytes: size, ms, cached: false };
  }

  // --- local patches ----------------------------------------------------------------

  /** Whether this fwg has the `patch` command; builds from before it do not. */
  async supportsPatch() {
    const fingerprint = await this.fingerprint();
    if (this._patchSupport?.fingerprint !== fingerprint) {
      const result = await run(this.exe.path, ['--help']);
      this._patchSupport = { fingerprint, value: /^\s*fwg patch\b/m.test(result.stdout) };
    }
    return this._patchSupport.value;
  }

  /**
   * The patch parameters from a POST body {world, latitude, longitude, span, spacing,
   * detail_height, detail_scale}. Numbers only; their ranges are fwg's to judge.
   */
  static patchFrom(body) {
    if (!body || typeof body !== 'object' || Array.isArray(body)) {
      throw new GeneratorError(400, 'bad_request', 'The body must be a JSON object: {"world": id, "latitude": deg, "longitude": deg, ...}.');
    }
    const numeric = ['latitude', 'longitude', 'span', 'spacing', 'detail_height', 'detail_scale'];
    const unknown = Object.keys(body).filter(k => k !== 'world' && !numeric.includes(k));
    if (unknown.length) throw new GeneratorError(400, 'bad_request', `Unknown field ${unknown.map(k => `'${k}'`).join(', ')}; the body takes world and ${numeric.join(', ')}.`);
    if (typeof body.world !== 'string') throw new GeneratorError(400, 'bad_request', 'world is required: the id of a world in the cache.');
    const params = {};
    for (const name of numeric) {
      if (body[name] === undefined || body[name] === null) continue;
      if (typeof body[name] !== 'number' || !Number.isFinite(body[name])) {
        throw new GeneratorError(400, 'bad_request', `${name} must be a number.`);
      }
      params[name] = body[name];
    }
    return { world: body.world, params };
  }

  /**
   * Cut a local patch from a cached world: `fwg patch` regenerates the world from the
   * request it was made with and samples the ground around one point. Patches are cached
   * beside their world, keyed by their parameters. Resolves to {file, cached, ms}.
   */
  async patch(body) {
    const { world, params } = Generator.patchFrom(body);
    const meta = /^[0-9a-f]{8,64}$/.test(world) ? await this.readMeta(world) : null;
    if (!meta?.request || !(await this.resolveWorld(world))) {
      throw new GeneratorError(404, 'not_found', `No world '${world}' with a recorded request in the cache. Generate it again.`);
    }
    if (!(await this.supportsPatch())) {
      throw new GeneratorError(501, 'fwg_without_patch', `This fwg has no patch command: ${this.exe.path} predates it.\n${BUILD_HINT}`);
    }
    // The patch regenerates the world, so it must be the build that generated it.
    if (meta.fwg?.sha256 && meta.fwg.sha256 !== (await this.fingerprint())) {
      throw new GeneratorError(409, 'other_build', 'This world was generated by a different build of fwg, which may make a different world from the same request. Generate it again to cut patches from it.');
    }
    const key = crypto.createHash('sha256').update(`${world}\n${canonicalJson(params)}`).digest('hex').slice(0, 16);
    const file = path.join(this.cacheDir, `${world}.patch-${key}.json`);
    if (fs.existsSync(file)) return { file, cached: true, ms: 0 };
    const jobKey = `patch:${world}:${key}`;
    let job = this.inflight.get(jobKey);
    if (!job) {
      job = this.queue.then(() => this.runPatch(world, meta.request, params, file));
      this.queue = job.catch(() => {});
      this.inflight.set(jobKey, job);
      job.finally(() => this.inflight.delete(jobKey)).catch(() => {});
    }
    return job;
  }

  async runPatch(world, request, params, file) {
    const status = this.requireExecutable();
    const requestFile = path.join(this.cacheDir, `${world}.request.json`);
    await fsp.writeFile(requestFile, `${JSON.stringify(request, null, 2)}\n`);
    const partial = `${file}.partial`;
    const args = ['patch', '--request', requestFile, '--output', partial];
    const flags = { latitude: '--latitude', longitude: '--longitude', span: '--span', spacing: '--spacing', detail_height: '--detail-height', detail_scale: '--detail-scale' };
    for (const [name, flag] of Object.entries(flags)) {
      if (params[name] !== undefined) args.push(flag, String(params[name]));
    }
    const started = performance.now();
    const result = await run(status.path, args);
    const ms = Math.round(performance.now() - started);
    if (result.code !== 0) {
      await fsp.rm(partial, { force: true });
      throw failureOf(result);
    }
    await fsp.rename(partial, file);
    this.log(`cut a patch from ${world} at ${params.latitude ?? 0}, ${params.longitude ?? 0} in ${(ms / 1000).toFixed(1)} s`);
    return { file, cached: false, ms };
  }

  /** Delete least recently used worlds until the cache fits its budget. */
  async evict(keepId) {
    if (!this.cacheLimitBytes) return;
    const worlds = await this.listWorlds();
    let total = worlds.reduce((sum, w) => sum + w.bytes, 0);
    for (const world of [...worlds].reverse()) {
      if (total <= this.cacheLimitBytes) break;
      if (world.id === keepId || this.protect(world.id)) continue;
      // The world, its meta and request, and every patch cut from it.
      for (const name of await fsp.readdir(this.cacheDir)) {
        if (name.startsWith(`${world.id}.`)) await fsp.rm(path.join(this.cacheDir, name), { force: true });
      }
      total -= world.bytes;
      this.log(`evicted ${world.id} (${(world.bytes / 1e6).toFixed(0)} MB) to keep the cache under ${(this.cacheLimitBytes / 1e9).toFixed(1)} GB`);
    }
  }
}
