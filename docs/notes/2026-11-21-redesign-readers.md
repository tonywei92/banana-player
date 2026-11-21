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
