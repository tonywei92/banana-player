# Book: upright menus, the turn's slide, the chapter in the page tag (2026-11-22, c-bookmenu)

- **Keys in Book** (Manga): START opens View and X More, as in every layout; both are the kit's upright sheets, drawn
  over the pages by the overlay stack (the reader's Book drawing is only the pages, so nothing turns them). The sideways
  Book bar is a tap in the middle only (owner: it may stay turned with the pages); any key puts it away. Reading mode
  (View) replaced X's direction switch. Text Book: START is Text options (upright), as in the scroll.
- **View stays open on a Layout change** (Manga `ComicView::setLayout`, Files' `PagesView`): the rows are ids, so the
  cursor keeps its row as Book's set and the others swap.
- **The slide** lives in `MangaReader::bookBoxes()`, the one place both panels' boxes come from (`bottomBoxes` shifts
  them), so the panels move as one column (`2*viewH + gap`). `bookTurn` (Step only; L/R realign stays instant) records
  the old screen's first page, the direction (forward = the side the later pages lie: `mangaBookEarlierOnTop`, so RTL
  reverses by itself) and the start; `frame()` ends it after `TURN_MS` (200, ease-out cubic). A turn within
  `TURN_FAST_MS` (280) of the last is instant and cancels a running slide. Old pages not on the GPU are left out (never
  a spinner). The bottom panel keeps 4 page textures in Book (2 before) so both screens of a slide are on hand; one
  texture is made a frame, so a new page can be a dark box for a frame. Setting `manga-turn-anim`.
- **Chapter in the tag:** `mangaChapterTag` (bpcore, `testMangaChapterTag`): the chapter's number, else "Chapter N" read
  from the title, else the title cut to 14 characters; none when the series id has no chapter list (Files' comic).
  Shown in fullscreen's pill, Book's tag and the fullscreen navigation's page line; one screen's header already names it.
- Checked: build, core-test, `--press` on a generated six-colour CBZ (Manga › library): View upright in Book, Layout
  changed twice with the sheet open, More upright, the middle tap's bar, the slide mid-way (with TURN_MS raised for the
  shot only), four Downs 80 ms apart (one slide, then instant). Desktop frames during a slide: 2-21 ms. Not checked: the
  handheld's GL/KMSDRM rate during a slide, Mali texture upload of the second new page.
- **2026-11-23:** the tag has a setting (Hide / On turn / Always) and fades: docs/notes/2026-11-23-book-tag-zoom.md.
