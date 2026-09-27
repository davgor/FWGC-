# ui-mockups

Mockups of the games' UI screens. Each screen is designed on a Claude Design canvas. When the
owner approves a version, its export is filed here in a folder named for the screen, and the
game's UI is built from it. An export is the approved artboard as a static HTML page, which
holds the exact sizes, colours and fonts, and a PNG of that page at 1920×1080. A screen with
several states has one page that shows each state at its own address (`settings.html#audio`),
and a PNG of each state. New Character's 24 states differ only in which people and look they
show, so it has PNGs of two. The working rules are in [AGENTS.md](AGENTS.md).

## Screens

The screens are designed one at a time, on one canvas with a page per screen:
[UI Journey](https://claude.ai/artifact/3hP4mXacmLnjaQU5X791xb). The canvas is private until the
owner shares it.

| Screen | Canvas page | Export | State |
|---|---|---|---|
| Opening screen | 1 · Opening screen | [opening-screen/](opening-screen/) | Approved: direction A, Horizon |
| Quit | 2 · Quit | [quit/](quit/) | Approved: the inline confirm |
| Settings | 3 · Settings | [settings/](settings/) | Approved |
| Load Character | 4 · Load Character | [load-character/](load-character/) | Approved |
| New Character | 5 · New Character | [new-character/](new-character/) | Approved |
