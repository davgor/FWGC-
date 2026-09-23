// The viewer server's JSON API. Every call resolves to parsed JSON or throws an ApiError
// whose message is the server's readable one.

export class ApiError extends Error {
  constructor(status, message, body) {
    super(message);
    this.status = status;
    this.body = body; // the parsed error document, when there was one
  }
}

// The server's own errors carry `error`; a refusal from fwg is its failure document, which
// carries `message`, the field it refused and sometimes a suggestion.
function messageOf(body, fallback) {
  if (!body || typeof body !== 'object') return fallback;
  if (typeof body.error === 'string') return body.error;
  if (typeof body.message === 'string') {
    let text = body.message;
    if (body.suggestion?.kind === 'clamp' && body.suggestion.value !== undefined) text += ` Try ${body.suggestion.value}.`;
    return text;
  }
  return fallback;
}

async function call(method, url, body) {
  let response;
  try {
    response = await fetch(url, {
      method,
      headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
  } catch (error) {
    throw new ApiError(0, `The viewer server did not answer (${error.message}). Is node server.mjs still running?`, null);
  }
  const text = await response.text();
  let parsed = null;
  try {
    parsed = text ? JSON.parse(text) : null;
  } catch {
    // leave parsed null; the status says what happened
  }
  if (!response.ok) {
    throw new ApiError(response.status, messageOf(parsed, `${method} ${url} failed with ${response.status}`), parsed);
  }
  return parsed;
}

const enc = encodeURIComponent;

function query(params) {
  if (!params) return '';
  const search = new URLSearchParams();
  for (const [key, value] of Object.entries(params)) {
    if (value === undefined || value === null) continue;
    if (Array.isArray(value)) value.forEach(v => search.append(key, v));
    else search.set(key, Array.isArray(value) ? value.join(',') : String(value));
  }
  const text = search.toString();
  return text ? `?${text}` : '';
}

export const api = {
  health: () => call('GET', '/api/health'),
  controls: () => call('GET', '/api/controls'),
  worlds: () => call('GET', '/api/worlds'),
  generate: (seed, overrides) => call('POST', '/api/generate', { seed, overrides }),
  summary: id => call('GET', `/api/world/${enc(id)}/summary`),
  /** params: {path, where: [..], fields: 'a,b', offset, limit} */
  block: (id, name, params) => call('GET', `/api/world/${enc(id)}/block/${enc(name)}${query(params)}`),
  layer: (id, name) => call('GET', `/api/world/${enc(id)}/layer/${enc(name)}`),
  plan: (id, kind, key) => call('GET', `/api/world/${enc(id)}/plan/${enc(kind)}/${enc(key)}`),
  /** body: {world, latitude, longitude, span, spacing, detail_height, detail_scale} */
  patch: body => call('POST', '/api/patch', body),
};
