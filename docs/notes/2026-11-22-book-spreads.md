# Book: wide pages span both screens (2026-11-22, c-spread)

- **Pairing** is pure, in bpcore (`manga_ui_logic.*`): `mangaBookWide` (w/h ≥ 1.2, never page 0),
  `mangaBookScreenFrom` (the screen starting at a page), `mangaBookScreens` (the walk from page 0),
  `mangaBookScreenOf` (reopen, Seek), `mangaBookScreenBefore` (a step back: the walk's screen before, cut to end at
  the current start, so a screen realigned by L/R doesn't overlap). Tests: `testMangaBookSpread*`.
- **The reader holds only `bookS`** (the screen's first page); its count is recomputed from the pages' wide flags each
  time (`bookScreen()`). A size learnt late changes what's ahead and what the current screen holds, never its start.
  `page()` is still `bookS`, so the saved place, the web's shared place and `progress.tsv` are unchanged.
- **Wide flags** (`MangaReader::wide`) come from a PDF's page tree and a card file's header at open
  (`noteShape`), and from each decode. Unknown = normal. A CBZ learns them as the preload decodes, so the pairing ahead
  can shift; the log says "reader: page N is a double-page spread (WxH)".
- **Drawing:** a spread is one Box over both panels plus the hinge (`2*viewH + gap`), fitted and centred, so the
  fold sits under the hinge; `bottomBoxes` shifts by `viewH + gap` in Book. The page is already turned upright as
  held, so its left half lands on the reader's left in either hold and either direction (RTL needs nothing).
  `hingeGap()` stays the Strip's; Book uses `bookGapNow()` (`setBookSpread`, fed each pass from main with the setting
  and Hinge gap), and View's Book rows list Hinge gap.
- **Decode width** is per page (`decodeWFor`): a spread wants `2*viewH + gap`; it's re-decoded wider once found wide.
  Spreads are big (1600x1200 here); with the decoded budget a spread can be decoded again after a step away.
- Checked: build, core-test, `--press` on a generated CBZ [tall, tall, WIDE, tall, WIDE, WIDE, tall] from Manga ›
  Library (`set:manga-reader=yes,set:manga-mode=ltr|rtl,R2*5,A,B,A,A,Start,Right` then UP/DOWN): progress went
  1→3→4→5→6→7 and back 6→5; both shots show the spread split across the panels. Not checked: the handheld's real
  hinge alignment, Book's tag with a lone page, Seek from the grid onto a spread.
- **2026-11-23:** a spread no longer skips the hinge strip (owner): it runs on across the panels; pairs keep the Hinge gap. Zoom and pan came to Book too: docs/notes/2026-11-23-book-tag-zoom.md.
