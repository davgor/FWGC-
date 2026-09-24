# viewer/: local rules

A browser viewer for generated worlds: a small Node server over `fwg.exe` and a static page.
**Lane: GPT.** The repository-wide rules are in [`../AGENTS.md`](../AGENTS.md).

## Run and test

```bash
cd viewer
node server.mjs                 # then open http://127.0.0.1:8787/
node test/smoke.mjs             # from the repository root: node viewer/test/smoke.mjs
```

It needs Node 18 or later and a built `fwg.exe`. [`README.md`](README.md) says where the
executable is looked for, the options, the API and the cache.

## Rules

- **The generator is reached through `fwg.exe` and its documents only.** Run its commands
  (`generate`, `controls`, `patch`, ...) and read what they write. Never read `generator/src`,
  `generator/include`, `generator/data` or `generator/tools`. The layer checker refuses those
  paths in the viewer's code (`path-text`).
- **Nothing depends on the viewer.** It is a dev tool. No runtime component may use it or
  anything in it.
- **JavaScript is a declared exception** (ruled 2026-09-24). It stays a dev tool.
- **No npm dependencies and no build step.** Keep them out.
- **Keep the server's protections.** It listens on this machine by default, refuses other sites'
  pages and rebinding hostnames, and takes only `application/json` bodies (README, "The API").
- **Documents are never parsed whole.** A size-513 world is longer than the longest string V8 can
  hold. Keep reading by byte range, as `lib/json-index.mjs` does.
