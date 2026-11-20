# Library sources (2026-11-20)

What it is: where Videos, Music and Books read from, shown and filled. Spec: docs/redesign/backlog.md › Decided
(videos-more, videos-from, music-from, books-from, the *-empty pages, home-first).

- Rules: the folder names are the whole rule (`isMusicFolderName`, `isVideosFolderName`, `isBookFolderName`: Books /
  Ebook / Ebooks, any case, not Documents). `mediaFromWords` counts three or more ("3 folders on both cards");
  `mediaEmptyWords` is the empty page's crumb ("Videos is empty on this card").
- First start: `makeLibraryFolders(stateDir)` (mediaindex.cpp, called from main before the index worker): on the main
  card (first root) only, an empty Videos / Music / Books when no folder by any of that section's names is there.
  Once: marker `<state>/library-folders-made`; logged as `library: made ...` in log.txt. Test in core_test.
- More: `Ui::moreTwoColumns` weaves the sheet's own rows (left) with the section's (right) into a 2-column More;
  `sourceMoreRows` gives "Where ... come from" (no count note: the half-width column cut the name) and "Web
  dashboard" (its note: the address, Off, or No Wi-Fi). Code: src/ui_sources.cpp. `MoreItem::dim` = drawn dim, still chosen.
- Sources sheet (`Ui::openSources`): a More sheet of rows: folders (card / folder, count or "empty" dim; A opens in
  Files; at most 5), the dashboard row, the rule as two dim lines. Counts are taken when it opens.
- Not done: the top screen's how-to steps for the empty pages and Home's welcome (the top panel has no such page yet);
  Home's shortcut tile is now "Web dashboard" too. Unsure on the handheld: the 2-column More at real size.

## The dashboard side
- `GET /api/library` now carries `cards`: each card the dashboard serves, named `SD1`/`SD2` in the index's order, with
  the virtual path of its Videos / Music / Books folder (`""` when none is there). `POST /api/library/folder?kind=
  videos|music|books&card=N` makes the folder if missing (Videos / Music / Books at the card's top, checked with
  `resolve(..., forCreate)`; logged `library: made ...`) and answers `{p, made}`; the files then go through the
  existing `PUT /api/upload` (resolveVirtual, deny list, X-BP + Origin, session). Only the card's top level counts.
- `web/js/libupload.js`: `SectionUpload` (the tabs' header button), `uploadToSection` (card asked first when there are
  two, then the picker opened from that tap, the folder made once files are chosen). The empty pages' buttons use it.
- tools/web-smoke.sh: the "library" block (cards, folder route refusals, an upload into Music and its delete).
- Not checked in a browser: the picker flow on Safari (the card sheet comes before the picker so the tap stays a tap).
