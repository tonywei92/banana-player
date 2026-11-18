# Handheld redesign (preview)

The proposed redesign of the handheld UI, kept for when it gets built.

- `preview.html`: every screen as a clickable mockup (open it in a browser). It is built from `src/`
  (`style.css`, `kit.js`, `screens.js`, `comps.js`, `app.js`, `body.html`) and `img/` by
  `python3 build.py docs/redesign`; edit `src/`, then rebuild.
- `journeys.py`: the screens as a graph, from `src/` (needs node): what leads where, screens nothing reaches, screens with no
  way back, hint bars that name a key the `keys` list lacks, and the journeys of `coverage.md` walked click by click
  (`python3 journeys.py --journeys`; `--all` lists every link and every key that leads nowhere). Links are a key's target in
  `keys`, a `data-go` in the drawing, and the `TAPS` / `ALSO` tables at the end of `src/screens.js`. Run it after adding a screen.
- `components.md`: the reusable components, so each is written once in code. Its Themes section has every token's value per theme and
  the table of each component with the tokens it uses.
- `themes.py`: the themes' values and the contrast of each pair (`python3 themes.py`; `css` prints the blocks of
  `src/style.css`, `md` the tables of `components.md`). Change a value there, then paste both.
- `backlog.md`: what the spec doesn't draw yet (the journeys audit's gaps) and the decisions waiting on the owner.
- `coverage.md`: the audit of what the preview covers against the app's features.
- `lid-and-incognito.md`: incognito and the lid, incognito and the Web dashboard, the keys with the lid shut
  (two options, one recommended).
- `today/`: screenshots of the current app (top and bottom panels) for comparison.

Mock content (names, addresses) is made up; pictures are *Big Buck Bunny* (CC BY 3.0) or generated.
