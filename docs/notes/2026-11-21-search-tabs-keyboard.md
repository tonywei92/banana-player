# Search library's tabs, the key pad, Music's Folders (2026-11-21)

Four owner requests on `c-search`; what was built and what the next reader needs.

## The keyboard is one model now: `KeyPad` (keyboard.h, bpcore)

Both keyboards (a field's over the panel, `ui_keyboard.cpp`; Search's in the shell's body, `ui_search.cpp`) ask one
struct for their keys: `label(i)`, `button(i)` (the cap), `lit(i)`, `role(i)`, `press(i, TextEdit&, maxLen,
digitsOnly)` (true: Done). `Kit::KeyboardView::keys(pad, empty)` fills the drawing from it, `lay::keyboard(area, {},
KeyPad::rows(), pad.toggles())` lays it out, `lay::keyboardHit` / `keyboardMove` serve both. Search's is the same pad
with `more = false` (no `#+=`). A change to the keys is made once, there; `testKeyboard` pins it.

- Keys are numbered: the number row (0–9), the layer's 26 characters, then the bottom row. `KeyPad::CHARS` is 36.
- The **number row** shows on every layer (the owner), so 123 holds marks only and `#+=` the six left; a test checks
  every printable ASCII mark is on one of the two (a password's).
- Four rows fit only with `KB_KEY_H` 44 (was 52): the field's keyboard's note line ends at 426, the hint bar starts
  at 450. With a special row (the gallery's page only, no screen uses one yet) the gap under it is 10.
- Shift, 123 and `#+=` are toggles drawn lit (tint, ACCENT word, a lamp bar); the focus stays the ACCENT edge.
- Bottom keys are 78 wide: "Clear" + the L2 cap needs 66 in Pixelify 14. A longer word would be cut, not its cap.
- Buttons, both keyboards: A type, B Del (empty field: close), X Space, Y Shift, L3 / SELECT step the layers, L2
  Clear, L / R the caret, START Done. Search's used to be X delete, Y space, B close: changed to match, and its
  header's `<` calls `Search::back` (ui.cpp's BACK_ICON) because B no longer always closes.
- `Ui::press` swallowed L2 / R2 outside the readers; Search is now let through (L2 is Clear).

## Search library

- `searchLibrary()` fills `found[T_MUSIC|T_VIDEOS|T_BOOKS]` once per query and index generation; `rebuild()` makes
  the tab's rows from them (a tab change searches nothing again). Books are `Ui::Books::shelf` (built there if the
  places or the roots moved): the Files part of the old search (history, the folder Files shows) is gone.
- Layout: `lay::searchResults(body, tabWidths, capW, tabs, notice)`; `Search::layout()` is the one place the
  drawing and the touches ask. The tabs are `Kit::tabs` on that strip's area, hit by `lay::tabAt`.
- The scripted state line prints `tab=` and `found=all/music/videos/books`.

## Music's Folders view

View 3 (after Playlists, so the Playlists page's chip indices did not move). Rows come from the snapshot's songs'
paths under `musicFolders` (`folderRows()`, cached per folder and generation, shared by the list's lambdas). One Music
folder: the view opens inside it (`folderShown`), `folderAt` stays "" at that top.

The artist page bug: the strip is drawn while `keyFocus >= 0 || artistZone == 0`, the touch asked for `keyFocus < 0`.
Both now ask `artistStripShown()` and `artistStripCell(j)`.

## Not verified

- On the handheld: 44-tall keys under a thumb at 1.6x; the lit toggle's tint in Clean and High contrast; Clear's
  word at the real font hinting. The number-row keyboard was checked by its layout tests and scripted typing, not
  looked at in a screenshot (the shot budget went to the earlier bottom row, the results and Folders).
- An artist with songs and no albums, and the songs list under the strip, by touch (there is still no touch way
  from the strip down to an artist's songs: only DOWN).
- Search with thousands of books on the shelf (`Books::build` walks the Books folders on the main thread, once per
  new query when the shelf is stale).
- `docs/artifacts/handheld-redesign/index.html` (the published copy) was not rebuilt; `docs/redesign/preview.html` was.
- The spec's Music screens still draw three view chips (Folders was not asked into the spec).
