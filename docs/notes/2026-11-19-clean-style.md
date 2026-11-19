# The Clean style (2026-11-19)

A second way to draw the kit, beside Classic (the spec: docs/redesign/components.md, "Clean style"; decided in
backlog.md). Classic stays the default: Settings › Power & screens › Style, under Theme, values `classic` / `clean`
(`Settings::cleanStyle`, one table entry in `settings.cpp`).

**What changes.** One flag read only while the kit draws: `theme::clean` (set in `Ui::renderPage` from the setting;
High contrast is forced Classic). The kit stops drawing edges and lips, gives every pressed thing one radius (12; caps
and pills 6), and uses one fill for what is pressed (BUTTON; a Clean card or tile is BUTTON too, not SURFACE). The
accent is solid once (the primary key); selected chips, tabs and the rail's cell take its 17% tint with ACCENT ink.
An unselected chip, the player's state chips and lines, the header's Back and More and the rail's Search have no
ground. Caps and pills are tints, the focus ring stays (2 units), a text field keeps its edge and a sheet its 1-unit
RULE hairline. The rail and the hint bar lose their rules.

**Where it is read.** `drawBox` / `fillRound` (`ui_draw.cpp`: no edge, no lip, the radii), and the kit's own pieces
(`ui_kit.cpp`, `ui_kit_variants.cpp`, `ui_kit_new.cpp`): `Kit::flat`, `Kit::caps`, `Kit::pill`, `Kit::chip`,
`Kit::line`, `Kit::key`, `Kit::transport`, `Kit::iconKey`, the tabs, segment, filters, tiles, cards, notice, switches,
search field and the rail. No screen has a rule for it and no colour is a screen's own.

**The exception (owner).** The media player's transport keys and its Like / Playlist / List / Stop row, and the photo
viewer's on-screen keys, stay Classic while a designer refines exactly which player pieces change. They are drawn
inside `Kit::ClassicHere`, which raises `theme::classicDepth` for the block; `drawingClean()` is false there, so
`drawBox`, `fillRound` and every kit piece take their Classic drawing. `signature()` carries the setting so a swap
redraws every page.

Checked: the desktop build, `core-test` (the setting round-trips, its row's texts, the radii and the ClassicHere
scope; every other theme's ON_RAIL falls back to DIM_ON), and scripted shots of Home and the player in Clean and of
Home in Gundam Classic.
