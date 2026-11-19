# The Clean style (2026-11-19)

Clean is how the app is drawn (the spec: docs/redesign/components.md, "Clean style"; decided in
docs/redesign/backlog.md). It is not a setting: there is no Style row. `theme::clean` follows the theme — every theme but
High contrast — so changing the Theme redraws the pages in the look (`signature()` already carries the theme).

**What changes.** One flag read only while the kit draws: `theme::clean` (set by `theme::setTheme`; High contrast is
Classic). The kit stops drawing edges and lips on the page, gives every page piece one radius (12; caps, pills and
badges 6) and uses one fill for what is pressed (BUTTON; a Clean card or tile is BUTTON too, not SURFACE). The accent is
solid once (the primary key); selected chips, tabs and the rail's cell take its 17% tint with ACCENT ink. An unselected
chip, the player's chapter and Next lines, the header's Back and More and the rail's Search have no ground. Caps and
pills are tints, the focus ring stays (2 units), a text field keeps its edge and a sheet its 1-unit RULE hairline. The
rail and the hint bar lose their rules.

**Keys stay keys** (rule 7). `Kit::key`, `Kit::iconKey` and `Kit::transport` never read the flag: a key is BUTTON (KEY
in a strong theme) with its 2-unit edge, 3-unit lip, radius 10 and outlined caps; the primary key is solid PRIMARY and
danger is Classic. The same goes for the photo viewer's keys (drawn with `drawBox` inside `Kit::ClassicHere`), the
keyboard (all of it), the reader's key rows (`Ui::Docs` and the Book bar keep their `drawBox` keys in `ClassicHere`),
the player's state chips (the Classic SURFACE piece, on its face and in More) and Home's three shortcuts (SURFACE and
its edge, radius 10). The scopes that used to hold the player's transport and named keys are gone: the kit's key
functions do it themselves. Two callers ask for a quiet, no-ground look: the header's Back and More
(`iconKey(..., quiet)`), and a settings row's − / + steppers (drawn as glyphs, with no ground in either style; Quick
settings' own steppers are quiet in Clean and keys in Classic).

**Where it is read.** `drawBox` / `fillRound` (`ui_draw.cpp`: no edge, no lip, the radii), and the kit's own pieces
(`ui_kit.cpp`, `ui_kit_variants.cpp`, `ui_kit_new.cpp`): `Kit::flat`, `Kit::caps`, `Kit::pill`, `Kit::tabs`,
`Kit::segment`, `Kit::filters`, `Kit::tile`, `Kit::cardFill`, the cards, notice, switches, search field and the rail. No
screen has a rule for it and no colour is a screen's own.

Checked: the desktop build, `core-test` (Clean follows the theme with no Style entry left, the radii, every theme's
ON_RAIL falls back to DIM_ON), and scripted shots of Home and the player in Dark.
