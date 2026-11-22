# Book and Reading mode are independent (2026-11-22, d-readmode)

- **The bug:** Book wrote the mode it read in over the mode chosen. `MangaReader::setBook` called `setMode(MANGA_RTL)`
  on a Strip and `Ui::Manga::mangaSetBook` stored "rtl/book-left"; `parseMangaReadingMode` refused Book beside
  "strip". So Vertical scroll → Book → One screen / Fullscreen came back Right to left.
- **The model:** two choices, neither changing the other. The mode *chosen* (`MangaReader::setModeV`,
  `ReaderStatus::modeSet`, the series' `readingMode`) is only moved by View's Reading mode row. The mode *read in*
  (`readMode`, `ReaderStatus::mode`: what every strip/rtl test in the reader and the Ui already reads) is
  `mangaModeIn(set, book, bookDir)` (bpcore, `manga_ui_logic.*`): the mode chosen, except a Strip in Book, which
  reads in pages. `setMode` keeps the choice and applies the result; `setBook` applies it going in and gives the
  choice back going out (`applyMode` is the old `setMode`: the page under the view kept).
- **Book's direction for a Vertical scroll** (`mangaBookDirection`): the Reading mode setting's when it is a
  direction, else right to left (as before). No source or series says which way its pages run: when one does, it goes
  in here. Fed each pass (`setBookDirection`), so the setting changed under Book takes at once.
- **The store** keeps "strip/book-left" now (the mode chosen + the hold). An older build reads that as a strip with
  Book off, which is safe. A series kept as "rtl/book-left" by the bug stays Right to left: what it was before is gone.
- **View:** the row lists all three modes in every layout; in Book a Strip reads "Vertical scroll (paged)"
  (`mangaModeLabel`: "(pages in Book)" was cut by the row's value box) and the sheet's line is "Scrolls again outside
  Book" (`mangaModeNote`) in place of "Kept for this series".
- Files' comic reader (`Docs::PagesView`) has no Book and no Reading mode row: its Layout *is* the mode. Not touched.
- Log: `reader: reads rtl (reading mode set: strip)` at each change of the mode read in.
- Tests: `testMangaModeInLayout`, `testMangaModeKeptThroughBook`. Checked: build, core-test, `--press` on the
  generated six-colour CBZ (Manga › Library, `set:manga-mode=strip`, View): Book → Fullscreen and Book → One screen
  both logged "reads strip". Not checked: a series' place inside a tall page after Book (Book keeps the page only),
  a chapter opened straight into Book from a Vertical scroll series on the handheld.
