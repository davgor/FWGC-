# ui-mockups/: local rules

Mockups of the games' UI screens. **Lane: shared.** Claude designs the mockups and GPT builds them
in the game. Neither one writing here is a lane crossing. The folder is a drop spot until the
games' own projects exist; the mockups move there then. The repository-wide rules are in
[`../AGENTS.md`](../AGENTS.md).

## Rules

- **Documents and assets only, no code.** A mockup is an HTML page, a PDF or an image. The layer
  checker refuses source files here (`no-code`). It reads extensions, so it cannot see a script
  inside an HTML page: a mockup's script only makes the mockup clickable, and the game's UI logic
  is written in the game, never copied from a mockup.
- **The canvas is the draft; the export is what gets built.** Each screen is designed on a Claude
  Design canvas, which keeps changing. When the owner approves a version, its export is filed
  here, and the game is built from that export, not from the live canvas.
- **A folder per screen,** named for the screen in lower case with hyphens (`conversation/`,
  `quest-log/`), holding that screen's exports.
- **The README is the index.** Each screen has a row: its canvas link, its approved export, and
  whether it is in design, approved or built.
