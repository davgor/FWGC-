#!/usr/bin/env node
// Smoke test for the viewer: start the server on a free port with a throwaway cache,
// generate a size-17 world through the API, and check every endpoint answers as the README
// says. Exits non-zero on any failure.
//
//   node viewer/test/smoke.mjs
//
// Needs fwg built (see viewer/README.md) or FWG_EXE set.

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const viewerDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const cacheDir = fs.mkdtempSync(path.join(os.tmpdir(), 'fwg-viewer-smoke-'));
const started = performance.now();
let failures = 0;
let checks = 0;
let server = null;
let base = null;

function check(name, ok, detail = '') {
  checks++;
  if (ok) {
    console.log(`  ok    ${name}`);
  } else {
    failures++;
    console.log(`  FAIL  ${name}${detail ? `\n        ${detail}` : ''}`);
  }
  return ok;
}

async function request(method, route, body) {
  const t0 = performance.now();
  const response = await fetch(new URL(route, base), {
    method,
    headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : typeof body === 'string' ? body : JSON.stringify(body),
  });
  const text = await response.text();
  let json = null;
  try {
    json = JSON.parse(text);
  } catch {
    // not JSON; the caller checks
  }
  return { status: response.status, type: response.headers.get('content-type') ?? '', text, json, ms: performance.now() - t0 };
}

// The status of a request sent exactly as written: path, headers and body untouched.
function rawStatus(rawPath, { method = 'GET', headers = {}, body } = {}) {
  return new Promise((resolve, reject) => {
    const url = new URL(base);
    const req = http.request({ host: url.hostname, port: url.port, path: rawPath, method, headers }, res => {
      res.resume();
      resolve(res.statusCode);
    });
    req.on('error', reject);
    req.end(body);
  });
}

// Start `node server.mjs --port 0` and read the address it prints.
function startServer() {
  return new Promise((resolve, reject) => {
    server = spawn(process.execPath, [path.join(viewerDir, 'server.mjs'), '--port', '0', '--cache', cacheDir], {
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let output = '';
    const timer = setTimeout(() => reject(new Error(`the server did not start within 15 s:\n${output}`)), 15000);
    const onData = chunk => {
      output += chunk;
      const match = /listening on (http:\/\/\S+)/.exec(output);
      if (match) {
        clearTimeout(timer);
        resolve(match[1]);
      }
    };
    server.stdout.on('data', onData);
    server.stderr.on('data', onData);
    server.on('exit', code => {
      clearTimeout(timer);
      reject(new Error(`the server exited with code ${code} before listening:\n${output}`));
    });
  });
}

async function main() {
  console.log(`viewer smoke test (cache ${cacheDir})`);
  base = await startServer();
  console.log(`server at ${base}`);

  const health = await request('GET', '/api/health');
  check('GET /api/health answers', health.status === 200 && health.json?.ok === true, health.text);
  if (!check('fwg is found', health.json?.fwg?.found === true, `looked at ${health.json?.fwg?.path}; build it (see viewer/README.md) or set FWG_EXE`)) return;

  const page = await request('GET', '/');
  check('GET / serves the page', page.status === 200 && page.type.startsWith('text/html') && page.text.includes('id="map"'), `${page.status} ${page.type}`);
  const script = await request('GET', '/js/main.js');
  check('GET /js/main.js serves a module', script.status === 200 && script.type.startsWith('text/javascript'), `${script.status} ${script.type}`);
  // fetch() normalises dot segments away, so send the escaping path raw.
  const outside = await rawStatus('/..%2fserver.mjs');
  check('static files stay inside public/', outside === 403, `${outside}`);

  const controls = await request('GET', '/api/controls');
  const catalogue = controls.json;
  check('GET /api/controls is the control catalogue', controls.status === 200 && catalogue?.schema === 'fantasy-world-generator.world-controls', controls.text.slice(0, 200));
  check('the catalogue has seed, size and phase', ['seed', 'size', 'phase'].every(name => catalogue?.controls?.[name]), JSON.stringify(Object.keys(catalogue?.controls ?? {}).slice(0, 5)));
  check('every control has a group, type and default', Object.values(catalogue?.controls ?? {}).every(c => c.group && c.type && c.default !== undefined));

  const generated = await request('POST', '/api/generate', { seed: 42, overrides: { size: 17 } });
  const id = generated.json?.id;
  check('POST /api/generate makes a size-17 world', generated.status === 200 && /^[0-9a-f]{24}$/.test(id ?? '') && generated.json.bytes > 0 && generated.json.cached === false, generated.text);
  if (!id) return;
  console.log(`        ${id}: ${(generated.json.bytes / 1e6).toFixed(1)} MB in ${generated.json.ms} ms`);
  const again = await request('POST', '/api/generate', { overrides: { size: 17 }, seed: 42 });
  check('the same request is served from the cache', again.json?.id === id && again.json?.cached === true, again.text);

  const summary = await request('GET', `/api/world/${id}/summary`);
  const s = summary.json;
  check('GET summary answers', summary.status === 200 && s?.id === id, summary.text.slice(0, 300));
  check('summary: stage 16 of a size-17 world', s?.completed_stage === 16 && s?.size === 17 && s?.config?.seed === 42, JSON.stringify({ stage: s?.completed_stage, size: s?.size }));
  check('summary: systems and warnings', Array.isArray(s?.systems) && s.systems.length > 10 && Array.isArray(s?.warnings));
  check('summary: counts per block', Array.isArray(s?.blocks) && s.blocks.some(b => b.name === 'settlements' && b.arrays?.sites > 0), JSON.stringify(s?.blocks?.find(b => b.name === 'settlements')));
  check('summary: layer names and shapes', s?.layers?.some(l => l.name === 'height' && l.rows === 17 && l.cols === 17), JSON.stringify(s?.layers?.slice(0, 2)));
  check('summary does not carry the document', summary.text.length < 200000, `${summary.text.length} bytes`);

  const block = await request('GET', `/api/world/${id}/block/settlements`);
  const sites = block.json?.sites ?? [];
  check('GET block/settlements', block.status === 200 && sites.length > 0 && typeof sites[0].uid === 'string', block.text.slice(0, 200));
  const picked = await request('GET', `/api/world/${id}/block/settlements?path=sites&fields=uid,name&limit=2`);
  check('block selection: path, fields, limit', picked.status === 200 && picked.json?.length === 2 && Object.keys(picked.json[0]).join() === 'uid,name', picked.text.slice(0, 200));
  const filtered = await request('GET', `/api/world/${id}/block/settlements?path=sites&where=${encodeURIComponent(`uid:${sites[0]?.uid}`)}`);
  check('block selection: where', filtered.status === 200 && filtered.json?.length === 1 && filtered.json[0].uid === sites[0]?.uid, filtered.text.slice(0, 200));
  const ruins = await request('GET', `/api/world/${id}/block/ruins`);
  check('GET block/ruins (an array block)', ruins.status === 200 && Array.isArray(ruins.json), ruins.text.slice(0, 120));

  const layer = await request('GET', `/api/world/${id}/layer/height`);
  const values = layer.json?.values;
  check('GET layer/height', layer.status === 200 && layer.json?.name === 'height' && layer.json.rows === 17 && layer.json.cols === 17, layer.text.slice(0, 120));
  check('the layer is 17 rows of 17 numbers', Array.isArray(values) && values.length === 17 && values.every(row => row.length === 17 && row.every(Number.isFinite)));

  const city = sites[0];
  const plan = await request('GET', `/api/world/${id}/plan/city/${encodeURIComponent(city.uid)}`);
  check(`GET plan/city/${city.uid}`, plan.status === 200 && plan.json?.city_uid === city.uid && Array.isArray(plan.json.plots) && plan.json.terrain?.surface, plan.text.slice(0, 200));
  const byOrdinal = await request('GET', `/api/world/${id}/plan/city/${city.id}`);
  check('a city plan is also found by site_id', byOrdinal.status === 200 && byOrdinal.json?.city_uid === city.uid, byOrdinal.text.slice(0, 120));
  const humans = (await request('GET', `/api/world/${id}/block/humans?path=fortresses&fields=uid&limit=1`)).json ?? [];
  if (humans[0]) {
    const castle = await request('GET', `/api/world/${id}/plan/castle/${encodeURIComponent(humans[0].uid)}`);
    check('GET plan/castle by fortress uid', castle.status === 200 && castle.json?.fortress_uid === humans[0].uid, castle.text.slice(0, 120));
  }

  // A local patch, where this fwg can cut one.
  if (health.json.fwg.patch) {
    const patch = await request('POST', '/api/patch', { world: id, latitude: 10, longitude: 20, span: 32, spacing: 2 });
    check('POST /api/patch cuts a patch', patch.status === 200 && patch.json?.schema === 'fwg.patch' && patch.json.size === 17
      && patch.json.height?.length === 17 * 17 && patch.json.world?.size === 17, patch.text.slice(0, 200));
    const tooWide = await request('POST', '/api/patch', { world: id, span: 100000 });
    check('a refused patch passes fwg\'s failure document through', tooWide.status === 400 && tooWide.json?.field === 'patch.span', tooWide.text.slice(0, 200));
  } else {
    console.log('  skip  POST /api/patch: this fwg has no patch command');
  }

  // Errors are JSON with a readable message; a refusal is fwg's own failure document.
  const errors = [
    ['unknown world', 'GET', '/api/world/0123456789abcdef01234567/summary', undefined, 404],
    ['unknown block', 'GET', `/api/world/${id}/block/no_such_block`, undefined, 404],
    ['unknown layer', 'GET', `/api/world/${id}/layer/no_such_layer`, undefined, 404],
    ['unknown plan', 'GET', `/api/world/${id}/plan/city/no-such-city`, undefined, 404],
    ['unknown plan kind', 'GET', `/api/world/${id}/plan/palace/x`, undefined, 404],
    ['a bad path', 'GET', `/api/world/${id}/block/settlements?path=nothing.here`, undefined, 404],
    ['a body that is not JSON', 'POST', '/api/generate', 'not json', 400],
    ['a body without a seed', 'POST', '/api/generate', { overrides: { size: 17 } }, 400],
    ['an even size', 'POST', '/api/generate', { seed: 1, overrides: { size: 16 } }, 400],
  ];
  for (const [name, method, route, body, status] of errors) {
    const r = await request(method, route, body);
    check(`${name}: ${status} with a message`, r.status === status && typeof r.json?.error === 'string' && r.json.error.length > 0, `${r.status} ${r.text.slice(0, 200)}`);
  }
  const refused = await request('POST', '/api/generate', { seed: 42, overrides: { size: 17, no_such_control: 1 } });
  check('a refused request passes fwg\'s failure document through', refused.status === 400 && refused.json?.schema === 'fantasy-world-generator.failure' && refused.json?.field === 'no_such_control', refused.text.slice(0, 300));
  const inherited = await request('GET', `/api/world/${id}/plan/constructor/x`);
  check('an inherited name is no plan kind', inherited.status === 404, `${inherited.status} ${inherited.text.slice(0, 120)}`);

  // Other web pages cannot drive the server: a body a browser could send cross-site without
  // asking, a foreign Origin, and a Host that is not this machine are each refused.
  const json = JSON.stringify({ seed: 5, overrides: { size: 9 } });
  check('a POST that is not JSON is refused (415)', await rawStatus('/api/generate', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: json }) === 415);
  check('a POST from another site is refused (403)', await rawStatus('/api/generate', { method: 'POST', headers: { 'Content-Type': 'application/json', Origin: 'http://elsewhere.example' }, body: json }) === 403);
  check('a request for another host name is refused (403)', await rawStatus('/api/health', { headers: { Host: `elsewhere.example:${new URL(base).port}` } }) === 403);
}

try {
  await main();
} catch (error) {
  failures++;
  console.log(`  FAIL  ${error.stack ?? error.message}`);
} finally {
  server?.kill();
  fs.rmSync(cacheDir, { recursive: true, force: true });
}
console.log(`${checks - failures} of ${checks} checks passed in ${((performance.now() - started) / 1000).toFixed(1)} s`);
process.exit(failures ? 1 : 0);
