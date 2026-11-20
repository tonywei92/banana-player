# Manga inside the shell (2026-11-20)

Status note item 6. Manga's pages are shell pages now: the rail (Extensions lit), the shell's header and hint bar.
The reader (`Page::MangaRead`) is not: a comic's page keeps the whole panel, unchanged.

## What is where

- **Kit first.** `lay::tabs` takes a cap width (`capW`, "L2"/"R2" are wider than 19); `lay::tabPage(body, widths,
  capW)` is tabs over a body (strip on the inner body's top, content SP8 under it); `lay::tabAt` is the touch test.
  `Kit::tabs(area, labels, on, keys, ground, pressed)` draws them on a page's ground; `Kit::tabW` / `Kit::tabCapW`
  give the widths. Tests in `tests/core_test.cpp` (after the tabs' own); gallery page 21.
- **Which pages:** `Manga::shellPage(pg)` = the area's pages but MangaRead. `Shell::page()` asks it; `Shell::head()`
  hands Manga's pages to `Manga::head()`.
- **Manage extensions** (`Page::Extensions`) is rebuilt on the kit (`Kit::row` with the mark and a switch trail, in
  `lay::extPage`'s first row; the touch tests that row). X opens Manga settings (spec: extensions).
- **The four tabs** are the kit's (`Manga::drawShellBody`, `mangaTabPage`), stepped by L2 / R2 (round), tapped on
  the same Rects (`Manga::shellPointer`, called in `Ui::pointer` before the bridge maps the touch). Downloads reads
  "Downloads 2" while some are queued. The header: "Manga", crumb "Library · 3 series" (the line that said it in the
  body is gone).
- **The bodies** are still the old 640-wide drawings, bridged (`Shell::beginBody/endBody`) into the body box. The
  bridge now takes a source band: `Manga::shellBand` gives `srcTop` 94 under the kit's tabs on the tab view (the old
  tabs ended at 90), 54 elsewhere; the band is top-aligned for Manga. `mapPoint(…, any)` maps a finger already down
  wherever it moves, so a drag leaving the box goes on (all bridged pages).
- **Header and hints of a bridged page** come from its old drawing: `Ui::hdrCapture` makes `drawHeader` keep the
  first title (`capTitle`) and `drawHints` / `drawFooterText` the last hints (`capHints`) while the body is bridged;
  `Manga::head` uses them (plus "L2/R2 Tab" on the tab view). So every old Manga page got a right header without
  touching each one.
- **Sheets** (the engine offer, the Library and History menus, the card help, the server menu, the filters, the
  Download sheet, the source info, Sources' modals, the first-time note) are handed to `Manga::later()` while
  `sheetsLater` is set and run after the shell is drawn: full size, over the frame, in their old coordinates. While
  one is up (`Manga::shellModal()`, the keyboard included) the shell takes no keys or touches and nothing is mapped:
  the sheet works exactly as before.
- **L / R** are the sections on every Manga page (the shell's); the source page's tabs and the Sources page's lists
  moved to L2 / R2 (`Ui::press` lets L2 / R2 reach `Page::MangaSources`). L3 is Search on Manga's pages.
- **Coming back:** `Manga::sectionLeft` only records when an Extensions page is left (leaving Books used to reset it,
  so Books → Extensions forgot Manga); Sources and Manga settings come back to the app.
- The state line (`--press`) prints Manga's pages: `manga tab=N view=V`, `manga-series ID`, `manga-sources tab=N`,
  `manga-settings`, `manage-extensions` (they printed "settings (from the browser)").

## Follow-ups

- **START** on Manga's shell pages is Settings (`Ui::startOpensSettings` asks `Manga::shellPage`); the reader keeps its
  Reading menu. **Manga settings** is the last row of Library's X menu (Remove missing, Manga settings) and of
  History's (the five clears, Manga settings); with the history off, X on History opens it straight away. Browse's,
  Downloads' and a series' X are not menus (Sources, Edit, read / unread) and stay as they were.

- **Library and History on the kit** (no bridge, no scaling: `Manga::kitBody()` makes `Shell::adopted` true for
  them): Library is `lay::coverGrid` (new: 96 x 136 covers with their titles, five across, rows SP12 apart, scrolled by
  whole rows) drawn with `Kit::coverTile` (unread count as its badge pill, the focused cover ringed); History is
  `lay::rowList` (new: rows 60 tall, 2 apart, clamped to end at the last) drawn with `Kit::row` (the cover in its mark,
  the title, "Ch. · page · when"), the focused row carrying a Continue key at `lay::rowKey` (`Kit::keyW` for its width).
  Empty states are `Kit::emptyState` in the content box. Touches test those same boxes (`mangaListBegin` with the
  grid's or the rows' pitch); their hints are set in `Manga::head` (nothing to catch). Tests next to the tab page's.

- **History's Y** opens the series' page (`openMangaSeries(id, 2)`, a server's by `mangaServerSeriesOpenFromId`);
  `Manga::mangaHisGone` dims a row (`Kit::Row::off`, no Continue key) whose source is no longer installed (asked only
  once the engine is up) or whose card series the last scan didn't find, and says which in its second line.

## Not done

- The other bodies (Browse, Downloads, a series, a source's grid, servers, Sources, Manga settings) are still bridged,
  scaled to 0.88. 
- A sheet over the bridge keeps its old full-page place; any sheet placed by what is under it would be off by the
  scale (none seen, not every one checked).
- Not checked on the handheld (KMSDRM, the real panel); the scaled body's text is small at 0.88.
