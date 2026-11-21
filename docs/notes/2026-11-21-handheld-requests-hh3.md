# Handheld requests, batch hh3 (2026-11-21): Books tabs, pull to refresh, Home's tiles, the sleep sheet, Show in Files

- **Sleep timer labels cut ("End of this so…", "O…")**: not the layout (the tiles are 360 and 240 wide). `Kit::textW`
  rounded a line's pixels to units with `lround`; `choiceTile` handed that width back as the line's `maxW`, and
  `drawTextAt` cut `maxW · sx` down to an int, so at the handheld's 1.6x the limit came out a pixel under the line and
  it was elided. `textW` now rounds up, the limit to nearest. Only seen with `BP_BOTTOM_SIZE=1024x768` (the Mac at 2x
  is exact). Any `maxW = textW(...)` in the kit had the same flaw.
- **Books**: `lay::booksTabs` (a `tabPage` + the open tab's cell height) and `booksFirstTab`. Reading now is every book
  with a doc place (no longer the newest five), with its bar; L / R reach Books through `Ui::press`'s L / R routing.
  `lay::booksShelf` stays for the kit gallery's page only.
- **Home's Recent**: an album tile (`lay::albumTile`, `Kit::recentTile`); `RECENT_*` are static_asserted equal to the
  album sizes. Pictures: `Home::pictureFor` (song art, `drawBookCover`, a video's list thumbnail centre-cropped by
  `MediaArt::drawThumbCrop`); Home's sig holds `mediaArt->serial`, so a thumbnail landing redraws.
- **Pull to refresh**: `lay::Scroller` (`setPull`, `armed`, `release` returns true past `PULL_REFRESH` = 64 of band,
  then rests `REFRESH_ROW` = 44 down until `unhold`; `jump` drops the rest). `SmoothList::Spec::refresh(Player&)` is
  called from `lift()` with `ui.mainPlayer`; the row is drawn by `drawPullRow` in the room the band opened, held
  `REFRESH_HOLD_MS` (700). Library lists call `Ui::rescanLibrary()` (the files serial main already watches). Manga's
  `FollowList::refresh` passes through `mangaRows` / `drawMangaGrid`. Tests: `testScroller`'s pull cases. Not run:
  Manga's lists (no Manga library in the harness), the handheld's finger (the 64 is a guess: about 150 units of
  finger at a 300-tall view).
- **Show in Files**: `Ui::showInFiles` = `showInFolder` + `Browse::backPage/backDir`; B in that folder goes back, any
  other B or a rail tap ends it.
