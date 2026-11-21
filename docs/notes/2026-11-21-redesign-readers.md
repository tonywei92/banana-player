# The page readers on the kit (2026-11-21)

Spec: `docs/redesign/src/screens.js` › `pdf`, `pdf-view`, `pdf-more`, `pdf-pages`, `manga-end` (backlog items 5–6,
coverage item 6).

## Built

- **Files' PDF / CBZ / CBR / comic EPUB** (`Ui::Docs`, ui_docs.cpp "the page reader"): `lay::readerPage(strip)` lays out
  both views (one page: the pan area, Page / Zoom keys two by two, the Page fit stepper, the help lines, the page
  slider; Vertical scroll: slider, then the drag surface), ending on Pages · View · Light. Draw and touch both ask
  `pagesKeyRect(id)`. The old nine-key grid and four-key row are gone.
- **Keys:** X is More (`openPagesMore`, a `MoreSpec`: a browser's page, Info, Show in folder, Light & filter, Reading
  settings); SELECT is View (`Docs::PagesView`, an overlay: Layout Single page / Vertical scroll / Fullscreen, Page
  fit, Zoom, Turn, Light & filter). In Fullscreen B goes back to one screen and SELECT opens View. START stays
  Settings. The Docs "Options" menu for pages (the DP_* rows) is removed; the text viewer's options are unchanged.
- **Pages grid** (`PageReader::drawMangaPages`, shared with the Manga reader): a sheet (`lay::pagesSheet`,
  `lay::pagesCell`) over the reader, still on the SmoothList; Here pill, accent ring, L2/R2 a screenful, a tap above
  the sheet closes. The Manga reader now draws its page under it (on plain ground in Fullscreen/Book).
- **Manga ends:** the end line names the next chapter (`mangaReadChapterName`), the crumb says "the end", caught up
  says "that was the last chapter"; Book's tag names it too. Waiting for a source's chapter: the bottom page is on the
  kit header and surface, and the top panel shows a State card (`Manga::waitingPane` via `Ui::topPane`).

## Not done (still old or not built)

- The Manga reader's own bottom screen (`manga-read`: slider above the surface, the five-key row) and its START
  Reading menu are the old drawing; `comic-view` / `comic-more` as sheets are not built there.
- New features the spec draws: page bookmarks (More's "Bookmark this page", the grid's Bookmarks tab), View's "Pan
  area" row (the setting `panRight` is the photo viewer's), the top panel's end-of-chapter card (the reader owns that
  panel; the bottom says it).
- "Book first" is the comic View's Layout row (One screen / Fullscreen / Book), not a layout of its own.

## Checked

Build, `core-test` (`testKitLayoutReaders`: both reader layouts and the Pages sheet). `--press` runs on a generated
9-page PDF (ImageMagick from ffmpeg `testsrc` frames) and CBZ: the single page, View, Pages, the comic in Vertical
scroll, Fullscreen on and off from View, More opened and closed. Not driven: the waiting card (needs a source), the
end lines, the grid inside the Manga reader, the handheld's panel.

## Fullscreen in one step, the stick across, Hinge gap in View (2026-11-21, later)

Owner: "switching to fullscreen should be easy".

- **Files' reader:** a fourth key in the row, **Fullscreen**, and **L3** (the one button the page reader had free);
  `Docs::togglePagesFull` goes in from either layout and back to the one it came from (`docFullFrom`), **B** the same.
  On the way in `lay::readerFullBar()` shows 3 s (what leaves, a One screen key: `pagesKeyRect(DP_KEY_FULL)` is that
  key in Fullscreen and the row's otherwise, so draw and touch agree).
- **Taps in Fullscreen** (`lay::readerFullTap`): the middle raises the bar, the edges turn (a strip's top and bottom
  28%; a paged comic's sides as LEFT / RIGHT, through `mangaKey`, so Swap Left/Right and the chapter's end hold). The
  Manga reader's bar still goes away on a tap outside it. An edge tap is never a double-tap's first.
- **The Manga reader** already had SELECT and its Fullscreen key; its "SELECT leaves" line now shows each time it is
  turned on by hand (it was once ever). L3 stays Turn there, so the two readers differ on the button (SELECT / L3)
  until the Manga reader moves to the kit and View takes SELECT: the spec's `comic-full` note says what to keep.
- **The stick sideways:** `Ui::stickAxis` dropped LEFTX before the readers (it reached only the photo viewer), and
  the Strip had no pan for it. Now `Strip::panBy` / `canPan` and `MangaReader::stickSpeedXNow` cover a zoomed strip
  (one screen and Fullscreen) as well as a paged page. `--press` has `stick:X:Y`; the log line "reader: stick scroll
  off (…), across N px" pins it on the handheld.
- **Hinge gap** is a row of Files' View sheet (the Manga setting `manga-hinge`, live through
  `SettingsPage::changeSetting`), active in Fullscreen. In the Manga reader it is in the Reading menu (START).

Checked: build, `core-test`, `--press` on a generated 6-page CBZ: L3 in and out (strip and Single page), the stick
across at 110% in the strip (0 → 128 px), in Fullscreen, and in Single page at 500%; taps top / bottom / middle and
the One screen key; the Hinge row moving the view (72 → 80 → 88 px of the Mac's 960). Not driven: the Manga reader's
edge taps and toast (needs a library), a real stick, the handheld's panel and touch.

## The Pages grid's missing thumbnails (2026-11-21, later)

Not the smooth list (its key holds each thumb's presence, `mpThumb` counts as a use, 60 kept for 18 in view): the
reader only ever made a thumbnail of a page it had decoded for the view, so a 40-page comic opened at page 1 had three.
Now the grid says which pages it shows (`ReaderCmd::Thumbs`, the rows in view and one each side, `PageReader::mpAsk`)
and `MangaReader::want()` queues those with no thumbnail as jobs of their own (`Job::thumb`, 192 wide, after the
view's pages; `thumbBusy` / `thumbFailed` apart from the view's sets, so they never count in the decoded budget or
become textures). Checked on a generated 40-page CBZ: 18 of 18 in view, and after a drag the next rows filled.
Left: a source's page not yet on the card is not fetched for its thumbnail; a turned page's grid-only thumb is unturned.

## One key map for the readers (2026-11-21, later still)

Owner: "SELECT should be Fullscreen across all readers". The table and its reasons: `docs/redesign/README.md`, "The
readers' keys". This replaces what the sections above say about SELECT · View and L3 · Fullscreen.

- **Files' comic / PDF:** SELECT `togglePagesFull`, START `openPagesView` (START or B closes it; SELECT in the sheet
  closes it and goes Fullscreen), L3 `stepPagesTurn(1)` in Single page. The row's captions and `pagesKeyAct` follow;
  the entry bar reads "SELECT or B: one screen".
- **START is no longer Settings in a reader** (`Ui::startOpensSettings` drops DocText / DocPages): the page reader
  has X · More › Reading settings; the text reader's START opens its options (`DOC_MENU_OPTIONS`), whose on-screen
  Options key now wears the START cap (it sends `key(Start)`, which `Docs::key` had no case for outside a menu).
- **Manga reader:** keys unchanged (SELECT, START, L3 were already these); its line reads "SELECT: one screen". B
  still closes the reader from Fullscreen (Files' B leaves Fullscreen first): kept, a chapter may open in Fullscreen.
- **Book bars:** the View / Text keys had a SELECT cap that was wrong (SELECT there leaves Book); no cap now.

Checked: build, `core-test`, `--press` on a generated 6-page CBZ (`A,Select,shot,Select,Start,shot,Start,L3,B`): the
log's "docs: fullscreen on / off", the bar's wording and the View sheet's "START B Close" in the shots. Not driven:
the text reader's START, the Manga reader's toast (needs a library), the handheld.


## Fullscreen's navigation, History's row (2026-11-21, last)

Owner: a middle tap in fullscreen showed "only a small bar" with a One screen key; History's row cut "Contin…".

- **One sheet for both readers:** `lay::readerFullNav(chapters)` (the name, "Page N of M", the slider, the steps,
  Pages / View / One screen) drawn by `PageReader::drawFullNav` on a see-through ground (BG at 224 of 255; the keys
  solid). Files: `Docs::pagesKeyRect` hands the same `DP_KEY_*` ids the navigation's boxes while in Fullscreen, so
  press, lift and act are the row's code; the slider is `grabPagesSlider` on the sheet's track. Manga:
  `mangaCtrlPressed` is a `PageReader::FN_*` id, acted as its button (`mangaKey`). `lay::readerFullBar` is gone; the
  way in says its line as a toast in both (`PageReader::drawFullToast`).
- **Taps with it up:** inside the sheet, the sheet's (and 4 s more); outside, an edge still turns (Manga's used to
  only close it) and the middle closes. A strip's bottom edge lies under the sheet while it shows: only the top
  edge turns then. The slider held keeps it up (`pagesFullNavShown` / the slide's callback).
- **Left off it:** Light and the Manga sheet's Close key (B closes; View has Light & filter).
- **History's row:** no key on the row (`lay::rowProgress`: a 3-unit bar along its foot; when at the right as the
  row's note). Downloads' Pause / Retry and Sources' Edit keep `lay::rowKey`: they are a second action, not the row's.

Checked: build, `core-test`, `--press` on a generated 6-page CBZ in Files (`set:manga-reader=yes,A,Select,tap:320:240,
drag:30:329:420:329` → "reader: jump 2 -> 4", the Page key, a second middle tap "the navigation away", gone by
itself after 4 s) and as a Manga chapter (`BP_BROWSE` on a folder with `Manga/<series>/`: the slider, a top-edge tap
"tap (edge: turned)" with it up, a middle tap "closed the navigation"), and History's row in a shot. Not driven: a
paged comic's side taps with the sheet up, Button hotkeys off, the handheld's panel and touch.
