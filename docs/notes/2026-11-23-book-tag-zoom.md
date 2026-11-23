# Book: the tag setting, spreads without the gap, zoom and pan (2026-11-23, c-booktag)

- **Tag** (`manga-book-tag`, `Settings::mangaBookTag`, `MANGA_TAG_*`): Hide / Show at load (default) / Always; also a
  View row in Book (`ROW_TAG`). `PageReader::mangaBookTagAlpha` is the one place that decides it: Show at load is
  `BOOK_TAG_MS` (2.5 s) from a screen change, a tap or the chapter's open, fading over the last `BOOK_TAG_FADE_MS`
  (400; the Ui signature carries the fade in 16-alpha steps); the chapter's end keeps it up except under Hide (owner:
  Hide is never). Log: `manga: book tag shown|gone (setting N)`.
- **Spread, no gap:** `bookGapNow()` is the current screen's gap: 0 on a spread, the Hinge gap on a pair (both
  `bookBoxesAt` and `bottomBoxes` read it, so a spread lies over `2*viewH` and the bottom panel is shifted `viewH`).
  A pair's gap now applies whether or not spreads are on; it only shows in the slide and the zoom. During a slide
  between a pair and a spread both screens use the new one's gap (a few px off for 200 ms, not seen).
- **Zoom** is Book's own (`MangaReader::bz`, `MangaBookZoom`), not `zscale`: the whole column (top panel, gap, bottom
  panel) scaled and shifted in `bookBoxes()`, so both panels draw one picture and pan in lockstep. Pure maths in
  `manga_ui_logic.*`: `mangaBookZoomStep` (1, 1.5, 2, 3), `mangaBookZoomClamp` (no empty edge where the pages are
  bigger than the column; centred where smaller), `mangaBookZoomTo` (about a point), `mangaBookPan`,
  `mangaBookZoomAtEnd`, `mangaBookZoomKey` (the key up as held zooms in). Tests `testMangaBookZoom*`.
- Keys: LEFT/RIGHT zoom (Zoom cmd, about the column's middle); a double-tap is `ZoomPoint` at the bottom panel's
  point (it closes the bar the first tap raised); a drag while zoomed is `Drag` (fractions of the panel), a lift
  `Release` (logs `reader: book pan offset …` with both panels' first box: in step). The stick, zoomed, sends `Stick`
  and `frame()` pans `bz`. A turn key zoomed pans to the end that way first; at the end it turns (`bookGo` resets the
  zoom), at the chapter's first/last screen it gives back the fit so the next press goes on (next chapter).
- Decode length follows the zoom (`decodeW`/`decodeWFor` × `bz.z`, cap 2048) after `ZOOM_SETTLE_MS`.
- Text Book (EPUB/TXT) untouched (owner).
- Checked: build, core-test, `--press` on a generated six-page CBZ (page 3 wide): tag shown/gone for each setting,
  the spread continuous across the panels, 150 → 200 %, a drag (offset -440,-1240; top page 3 at y -1027, bottom at
  -1987: one `viewH` apart), the stick, Down to the end then the turn. Not checked: the handheld's touch on a drag at
  300 % (decode at 2048 px of a spread: memory), the Book bar's four hints on the real panel.
