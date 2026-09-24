# concept art/: local rules

The concept art production map ([`README.md`](README.md)) and the art it maps, in three tiers.
**Lane: GPT.** The repository-wide rules are in [`../AGENTS.md`](../AGENTS.md).

## Rules

- **Documents and art assets only, no code.** The layer checker refuses source files here
  (`no-code`).
- **The README is the checklist.** Each item is grounded in an authored catalogue under
  `../generator/data/`. When the catalogues grow, add their IDs to the README rather than
  inventing new ones.
- **Filing art.** Name a finished piece after its catalogue ID where one exists, and file it under
  its tier's folder:
  - `tier-1-style/` for style direction;
  - `tier-2-assembled/` for whole assets;
  - `tier-3-parts/` for parts.

  A checkbox is complete only as the README says.
- **Catalogues change in the generator, not here.** Visual fields in `../generator/data/` are GPT's
  lane there, and follow `../generator/AGENTS.md` ("Content data").
