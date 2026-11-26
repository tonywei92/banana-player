# Handheld redesign (spec and preview)

The redesign of the handheld UI. It shipped in 0.9.0; the spec and the preview were brought in line with the app at 0.9.6
(2026-11-26), so they describe what is built. Where they and the code differ, the code is right and this is to be fixed.
The one designed part not built is Lyrics (`lyrics.md`): its screens are kept and marked “Designed · not built”.

- `preview.html`: every screen as a clickable mockup (open it in a browser). It is built from `src/`
  (`style.css`, `kit.js`, `screens.js`, `comps.js`, `app.js`, `body.html`) and `img/` by
  `python3 build.py docs/redesign`; edit `src/`, then rebuild.
- `journeys.py`: the screens as a graph, from `src/` (needs node): what leads where, screens nothing reaches, screens with no
  way back, hint bars that name a key the `keys` list lacks, and the journeys of `coverage.md` walked click by click
  (`python3 journeys.py --journeys`; `--all` lists every link and every key that leads nowhere). Links are a key's target in
  `keys`, a `data-go` in the drawing, and the `TAPS` / `ALSO` tables at the end of `src/screens.js`. Run it after adding a screen.
  A screen with a `phone` drawing is a page of the Web dashboard at a phone's width, shown beside the handheld's two panels; its taps
  are entries of `keys`.
- `components.md`: the reusable components, so each is written once in code. Its Themes section has every token's value per theme and
  the table of each component with the tokens it uses.
- `themes.py`: the themes' values and the contrast of each pair (`python3 themes.py`; `css` prints the blocks of
  `src/style.css`, `md` the tables of `components.md`). Change a value there, then paste both.
- `backlog.md`: the journeys audit's gaps (drawn), what the owner decided, the decisions still waiting, what is only listed.
- `coverage.md`: the audit of what the preview covers against the app's features.
- `lid-and-incognito.md`: incognito and the lid, incognito and the Web dashboard, the keys with the lid shut
  (two options, one recommended).
- `sleep-timer.md`: the sleep timer (the Sleep key on a song, the last minute's warning with 15 more min / Timer off,
  what happens at its end, its two settings, the Web dashboard's side); drawn as `sleep-set`, `sleep-on`, `sleep-warn`,
  `sleep-done`, `phone-sleep`, `phone-sleep-warn`.
- `lyrics.md`: lyrics (later work): where they come from, the top screen, the sheet and its states, the timing nudge,
  saving, the dashboard's page; drawn as `lyrics`, `lyrics-*`, `phone-lyrics`.
- `web-books.md`: Books on the Web dashboard: the shelf, reading in the browser (text, PDF, comics), the place shared with the
  handheld and its conflict cases, keys, copy, and what the server adds vs the page. Its screens are the group
  “Web dashboard: Books” (a `desk` drawing is a page at a computer's width, 1280).
- `today/`: screenshots of the current app (top and bottom panels) for comparison.

Mock content (names, addresses) is made up; pictures are *Big Buck Bunny* (CC BY 3.0) or generated.

## The readers' keys (owner, 2026-11-21: "SELECT should be Fullscreen across all readers")

One rule for every reader: **SELECT is the full view, in and out in one press; START is "how this is shown"; L3 is the
reader's one shortcut.** It replaces "SELECT · View" inside the readers only (lists, the player and photos keep theirs).

| Key | Files' comic / PDF | Manga reader | Text (EPUB / TXT) | Why |
|---|---|---|---|---|
| SELECT | Fullscreen on / off | Fullscreen on / off | Book mode on / off | The view over both screens is the one thing changed many times a sitting: one press, the same button, both ways. Book mode is the text's full view (both screens, a page each), so it keeps SELECT rather than the Text sheet. |
| START | View (the sheet) | View (the sheet) | Text (the options) | The sheet is opened rarely; START was already the Manga reader's. START or B closes it. Settings moves to X · More › Reading settings. |
| L3 | Turn a quarter (Single page) | Turn a quarter (paged) | Find | Fullscreen left L3 for SELECT, so Files' reader takes the Manga reader's Turn; a text has nothing to turn, and Find is its shortcut. |
| B | Fullscreen: one screen; else close | Close (fullscreen is kept for the next chapter) | Book: back to scrolling; else close | The one difference left: a chapter that opens in Fullscreen must close in one press. |
| Held sideways (Book) | n/a | START: View, X: More (upright sheets over the pages); a tap in the middle: the Book bar; SELECT: Fullscreen, upright | START: Text options (upright); a tap in the middle: the Book bar; SELECT: back to scrolling | Owner, 2026-11-22: menus stay upright in Book, only the pages turn; START is the same sheet in every layout. The bar's View / Text keys wear START. |

On the way into Fullscreen a line says "SELECT or B: one screen" (Files) / "SELECT: one screen" (Manga) and "tap the
middle: controls": that tap shows the navigation (`comic-full`: the slider, the steps, Pages, View, One screen), the
same sheet in both readers. In the View
sheet SELECT still goes Fullscreen (the sheet closes); a Layout change keeps the sheet open.
