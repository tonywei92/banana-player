# Handheld redesign (preview)

The proposed redesign of the handheld UI, kept for when it gets built.

- `preview.html`: every screen as a clickable mockup (open it in a browser). It is built from `src/`
  (`style.css`, `kit.js`, `screens.js`, `comps.js`, `app.js`, `body.html`) and `img/` by
  `python3 build.py docs/redesign`; edit `src/`, then rebuild.
- `components.md`: the reusable components, so each is written once in code. Its Themes section has every token's value per theme and
  the table of each component with the tokens it uses.
- `themes.py`: the themes' values and the contrast of each pair (`python3 themes.py`; `css` prints the blocks of
  `src/style.css`, `md` the tables of `components.md`). Change a value there, then paste both.
- `coverage.md`: the audit of what the preview covers against the app's features.
- `lid-and-incognito.md`: incognito and the lid, incognito and the Web dashboard, the keys with the lid shut
  (two options, one recommended).
- `today/`: screenshots of the current app (top and bottom panels) for comparison.

Mock content (names, addresses) is made up; pictures are *Big Buck Bunny* (CC BY 3.0) or generated.
