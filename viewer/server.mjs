#!/usr/bin/env node
// The FWGC++ world viewer's server: static files from public/, and a small JSON API over
// fwg.exe and the world documents it writes. Node 18 or later, no dependencies.
//
//   node server.mjs [--port 8787] [--host 127.0.0.1] [--cache DIR]
//
// See README.md for the API. The browser never receives a whole world document: it asks for
// a summary, then one block, one layer or one plan at a time.

import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { BUILD_HINT, Generator, GeneratorError } from './lib/generator.mjs';
import { BadRequestError, NotFoundError, PLAN_KINDS, WorldStore, selectWithin } from './lib/world-store.mjs';

const VIEWER_DIR = path.dirname(fileURLToPath(import.meta.url));
const PUBLIC_DIR = path.join(VIEWER_DIR, 'public');
const DEFAULT_PORT = 8787;
const MAX_BODY_BYTES = 64 * 1024;

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.txt': 'text/plain; charset=utf-8',
};

const USAGE = `FWGC++ world viewer

usage: node server.mjs [--port N] [--host ADDRESS] [--cache DIR]

  --port N        listen on port N (default ${DEFAULT_PORT}, or $PORT; 0 picks a free one)
  --host ADDRESS  listen on ADDRESS (default 127.0.0.1, this machine only; bound
                  there, the server answers only requests addressed to this machine)
  --cache DIR     where generated worlds are kept (default viewer/.cache)

environment:
  FWG_EXE               the fwg executable (default ../generator/build/apps/Release/fwg.exe)
  FWG_VIEWER_CACHE_MB   cache budget in MB before old worlds are deleted (default 4096)
`;

function parseArgs(argv) {
  const options = {};
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    const value = () => {
      if (i + 1 >= argv.length) throw new Error(`${arg} needs a value`);
      return argv[++i];
    };
    if (arg === '--port') options.port = Number(value());
    else if (arg === '--host') options.host = value();
    else if (arg === '--cache') options.cacheDir = path.resolve(value());
    else if (arg === '-h' || arg === '--help') options.help = true;
    else throw new Error(`unknown argument '${arg}'`);
  }
  if (options.port !== undefined && (!Number.isInteger(options.port) || options.port < 0 || options.port > 65535)) {
    throw new Error('--port needs a number 0..65535');
  }
  return options;
}

// --- responses ------------------------------------------------------------------------

function sendJson(res, status, value, headers = {}) {
  const body = Buffer.from(JSON.stringify(value));
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': body.length,
    'Cache-Control': 'no-store',
    ...headers,
  });
  res.end(body);
}

function sendError(res, status, code, message) {
  sendJson(res, status, { error: message, code, status });
}

// A byte range of a world document, optionally wrapped, as a JSON response. The range is
// already JSON, so it is streamed from disk without being parsed.
function streamJsonSlice(req, res, doc, start, end, prefix = '', suffix = '') {
  const etag = `"${doc.id}-${Math.round(doc.mtimeMs)}-${start}-${end}"`;
  if (req.headers['if-none-match'] === etag) {
    res.writeHead(304, { ETag: etag, 'Cache-Control': 'no-cache' });
    res.end();
    return;
  }
  const head = Buffer.from(prefix);
  const tail = Buffer.from(suffix);
  res.writeHead(200, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': head.length + (end - start) + tail.length,
    'Cache-Control': 'no-cache',
    ETag: etag,
  });
  if (req.method === 'HEAD') {
    res.end();
    return;
  }
  res.write(head);
  const stream = doc.streamSlice(start, end);
  stream.on('error', error => {
    console.error(`reading ${doc.file}: ${error.message}`);
    res.destroy(error);
  });
  stream.on('end', () => res.end(tail));
  stream.pipe(res, { end: false });
}

async function readJsonBody(req) {
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > MAX_BODY_BYTES) {
      const error = new Error(`The request body is over ${MAX_BODY_BYTES} bytes.`);
      error.status = 413;
      error.code = 'too_large';
      throw error;
    }
    chunks.push(chunk);
  }
  const text = Buffer.concat(chunks).toString('utf8');
  if (!text.trim()) throw new BadRequestError('The request body is empty; send JSON.');
  try {
    return JSON.parse(text);
  } catch (error) {
    throw new BadRequestError(`The request body is not JSON: ${error.message}`);
  }
}

// Query parameters for a block selection.
function selectionFrom(url) {
  const params = url.searchParams;
  const integer = name => {
    if (!params.has(name)) return undefined;
    const value = Number(params.get(name));
    if (!Number.isInteger(value) || value < 0) throw new BadRequestError(`${name}= needs a whole number.`);
    return value;
  };
  const fields = params.get('fields');
  return {
    path: params.get('path') ?? undefined,
    where: params.getAll('where'),
    fields: fields ? fields.split(',').map(f => f.trim()).filter(Boolean) : undefined,
    offset: integer('offset'),
    limit: integer('limit'),
  };
}

// --- static files ---------------------------------------------------------------------

function serveStatic(req, res, url) {
  let relative = decodeURIComponent(url.pathname);
  if (relative.endsWith('/')) relative += 'index.html';
  const file = path.resolve(PUBLIC_DIR, `.${relative}`);
  if (file !== PUBLIC_DIR && !file.startsWith(PUBLIC_DIR + path.sep)) {
    sendError(res, 403, 'forbidden', 'That path is outside the viewer.');
    return;
  }
  fs.stat(file, (error, stat) => {
    if (error || !stat.isFile()) {
      sendError(res, 404, 'not_found', `No file ${url.pathname}`);
      return;
    }
    res.writeHead(200, {
      'Content-Type': MIME_TYPES[path.extname(file).toLowerCase()] ?? 'application/octet-stream',
      'Content-Length': stat.size,
      'Cache-Control': 'no-cache',
    });
    if (req.method === 'HEAD') {
      res.end();
      return;
    }
    const stream = fs.createReadStream(file);
    stream.on('error', streamError => res.destroy(streamError));
    stream.pipe(res);
  });
}

// --- the API ---------------------------------------------------------------------------

// Other web pages must not be able to drive this server: it runs a program that can spend
// minutes of CPU and gigabytes of disk. A browser sends a cross-site POST without asking
// only when its body is a "simple" type, so the API takes JSON bodies alone, and a request
// whose Origin is some other site is refused outright. Bound to this machine, the server
// also answers only Host names of this machine, which stops a DNS-rebound name reaching it.
function refusalOf(req, url, allowedHosts) {
  const host = req.headers.host ?? '';
  if (allowedHosts && !allowedHosts.has(host.toLowerCase())) {
    return [403, 'forbidden_host', `This server answers requests addressed to this machine, not '${host}'.`];
  }
  if (!url.pathname.startsWith('/api/')) return null;
  const origin = req.headers.origin;
  if (origin && origin !== 'null' && origin.toLowerCase() !== `http://${host.toLowerCase()}`) {
    return [403, 'forbidden_origin', `Requests from ${origin} are not accepted.`];
  }
  if (req.method === 'POST' && !/^application\/json\b/i.test(req.headers['content-type'] ?? '')) {
    return [415, 'unsupported_media_type', 'Send the body as JSON, with Content-Type: application/json.'];
  }
  return null;
}

function createApp({ generator, store, allowedHosts }) {
  const routes = [
    ['GET', /^\/api\/health$/, async (req, res) => {
      const fwg = generator.status();
      if (fwg.found) fwg.patch = await generator.supportsPatch().catch(() => false);
      sendJson(res, 200, { ok: true, fwg, cache: { dir: generator.cacheDir, limit_bytes: generator.cacheLimitBytes } });
    }],

    ['GET', /^\/api\/controls$/, async (req, res) => {
      sendJson(res, 200, await generator.controls());
    }],

    ['POST', /^\/api\/generate$/, async (req, res) => {
      const body = await readJsonBody(req);
      sendJson(res, 200, await generator.generate(body));
    }],

    ['GET', /^\/api\/worlds$/, async (req, res) => {
      sendJson(res, 200, { worlds: await generator.listWorlds() });
    }],

    ['POST', /^\/api\/patch$/, async (req, res) => {
      const body = await readJsonBody(req);
      const { file, cached, ms } = await generator.patch(body);
      // Read whole (a patch is a few megabytes at most), so a file the cache evicts in the
      // meantime is a clean error rather than a stream that dies half sent.
      const text = await fs.promises.readFile(file);
      res.writeHead(200, {
        'Content-Type': 'application/json; charset=utf-8',
        'Content-Length': text.length,
        'Cache-Control': 'no-store',
        'X-Patch-Cached': String(cached),
        'X-Patch-Ms': String(ms),
      });
      res.end(text);
    }],

    ['GET', /^\/api\/world\/([^/]+)\/summary$/, async (req, res, [id]) => {
      const doc = await store.get(id);
      const summary = await doc.summary();
      const meta = await generator.readMeta(id);
      sendJson(res, 200, { ...summary, generation: meta ? { ms: meta.ms, created: meta.created, request: meta.request } : null });
    }],

    ['GET', /^\/api\/world\/([^/]+)\/block\/([^/]+)$/, async (req, res, [id, name], url) => {
      const doc = await store.get(id);
      const node = doc.blockNode(name);
      const selection = selectionFrom(url);
      const selecting = selection.path || selection.where.length || selection.fields || selection.offset !== undefined || selection.limit !== undefined;
      if (!selecting) {
        streamJsonSlice(req, res, doc, node.start, node.end);
        return;
      }
      const block = await store.parsedBlock(doc, name);
      sendJson(res, 200, selectWithin(block, selection));
    }],

    ['GET', /^\/api\/world\/([^/]+)\/layer\/([^/]+)$/, async (req, res, [id, name]) => {
      const doc = await store.get(id);
      const node = doc.layerNode(name);
      const shape = doc.layerShapes.get(name) ?? { rows: 0, cols: 0 };
      const prefix = `{"name":${JSON.stringify(name)},"rows":${shape.rows},"cols":${shape.cols},"values":`;
      streamJsonSlice(req, res, doc, node.start, node.end, prefix, '}');
    }],

    ['GET', /^\/api\/world\/([^/]+)\/plan\/([^/]+)\/([^/]+)$/, async (req, res, [id, kind, key]) => {
      if (!Object.hasOwn(PLAN_KINDS, kind)) throw new NotFoundError(`Unknown plan kind '${kind}'; use ${Object.keys(PLAN_KINDS).join(', ')}.`);
      const doc = await store.get(id);
      const [start, end] = doc.planSpan(kind, key);
      streamJsonSlice(req, res, doc, start, end);
    }],
  ];

  return async function handle(req, res) {
    const url = new URL(req.url, 'http://localhost');
    const started = performance.now();
    try {
      const refused = refusalOf(req, url, allowedHosts());
      if (refused) {
        sendError(res, ...refused);
        return;
      }
      if (!url.pathname.startsWith('/api/')) {
        if (req.method !== 'GET' && req.method !== 'HEAD') {
          sendError(res, 405, 'method_not_allowed', `${req.method} is not supported here.`);
          return;
        }
        serveStatic(req, res, url);
        return;
      }
      let pathMatched = false;
      for (const [method, pattern, handler] of routes) {
        const match = pattern.exec(url.pathname);
        if (!match) continue;
        pathMatched = true;
        if (method !== req.method && !(method === 'GET' && req.method === 'HEAD')) continue;
        const params = match.slice(1).map(decodeURIComponent);
        await handler(req, res, params, url);
        if (process.env.FWG_VIEWER_LOG) console.log(`${req.method} ${url.pathname}${url.search} ${res.statusCode} ${(performance.now() - started).toFixed(0)} ms`);
        return;
      }
      if (pathMatched) sendError(res, 405, 'method_not_allowed', `${req.method} is not supported on ${url.pathname}.`);
      else sendError(res, 404, 'not_found', `No API route ${url.pathname}. See viewer/README.md.`);
    } catch (error) {
      if (res.headersSent) {
        res.destroy(error);
        return;
      }
      if (error instanceof GeneratorError && error.rawBody) {
        // The CLI's own failure document, exactly as it printed it.
        const body = Buffer.from(error.rawBody);
        res.writeHead(error.status, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': body.length, 'Cache-Control': 'no-store' });
        res.end(body);
        return;
      }
      if (error instanceof URIError) {
        sendError(res, 400, 'bad_request', `The address is not validly escaped: ${error.message}`);
        return;
      }
      const status = error.status ?? 500;
      if (status >= 500) console.error(`${req.method} ${url.pathname}: ${error.stack ?? error.message}`);
      sendError(res, status, error.code ?? 'internal_error', error.message);
    }
  };
}

/** Start the server; resolves to {server, port, url, generator, store}. */
export async function startServer({ port = DEFAULT_PORT, host = '127.0.0.1', cacheDir, env = process.env, log = console.log } = {}) {
  const cacheLimitMb = Number(env.FWG_VIEWER_CACHE_MB ?? 4096);
  const generator = new Generator({
    viewerDir: VIEWER_DIR,
    cacheDir: cacheDir ?? path.join(VIEWER_DIR, '.cache'),
    cacheLimitBytes: Number.isFinite(cacheLimitMb) && cacheLimitMb > 0 ? cacheLimitMb * 1024 * 1024 : 0,
    env,
    log,
  });
  const store = new WorldStore({
    resolveFile: async id => {
      const file = await generator.resolveWorld(id);
      if (file) await generator.touch(id);
      return file;
    },
  });
  generator.protect = id => store.isOpen(id);
  // Filled in once the port is known; null (any Host) when bound beyond this machine.
  let allowedHosts = null;
  const server = http.createServer(createApp({ generator, store, allowedHosts: () => allowedHosts }));
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, host, resolve);
  });
  const actualPort = server.address().port;
  if (['127.0.0.1', 'localhost', '::1'].includes(host)) {
    allowedHosts = new Set(['127.0.0.1', 'localhost', '[::1]'].map(name => `${name}:${actualPort}`));
    if (actualPort === 80) for (const name of ['127.0.0.1', 'localhost', '[::1]']) allowedHosts.add(name);
  }
  const shownHost = host === '0.0.0.0' || host === '::' ? 'localhost' : host === '::1' ? '[::1]' : host;
  return { server, port: actualPort, url: `http://${shownHost}:${actualPort}/`, generator, store };
}

async function main() {
  let options;
  try {
    options = parseArgs(process.argv.slice(2));
  } catch (error) {
    console.error(`${error.message}\n\n${USAGE}`);
    process.exit(2);
  }
  if (options.help) {
    console.log(USAGE);
    return;
  }
  const port = options.port ?? (process.env.PORT ? Number(process.env.PORT) : DEFAULT_PORT);
  let started;
  try {
    started = await startServer({ port, host: options.host, cacheDir: options.cacheDir });
  } catch (error) {
    console.error(error.code === 'EADDRINUSE' ? `Port ${port} is in use; pass --port N (0 picks a free one).` : error.message);
    process.exit(1);
  }
  const status = started.generator.status();
  console.log(`FWGC++ world viewer listening on ${started.url}`);
  console.log(`  fwg: ${status.path}${status.source === 'FWG_EXE' ? ' (from FWG_EXE)' : ''}`);
  console.log(`  cache: ${started.generator.cacheDir}`);
  if (!status.found) {
    console.log(`\n  fwg was not found there, so nothing can be generated yet.\n${BUILD_HINT.replace(/^/gm, '  ')}\n`);
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  main();
}
