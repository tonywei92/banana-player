# The readers' chrome, Info's groups, the update sheet and All recent's days (2026-11-19)

Branch `readers-info`. The spec: `docs/redesign/components.md` and `docs/redesign/src/screens.js` (`reader`, `pdf`,
`manga-read`, `book-text`, `book-text-bar`, `book-comic`, `book-comic-end`, `info`, `update`, `update-downloading`,
`update-ready`, `update-failed`, `recent-all`). The kit pieces the brief named came first; the pages that ask for them
follow. Nothing here changes what a key or a touch does beyond the one place the spec asks for it (the Book bar's tap
and START), and every old key is still there.

## 1. The kit gaps

| Gap | Where | Test |
|---|---|---|
| `Kit::Header::titleFont` (default `F_P28`, unchanged) | `src/ui_kit.h`, used for the title, the inline title and the marquee | `testKitLayoutReaders` (the readers' header) |
| `lay::confirmSheet(textLines, foot, third)` and `Kit::ConfirmSpec::third` / `thirdKey` | `src/kitlayout.*`, `src/ui_kit.*`: three keys across (197, 197, 198), safe on the left, action on B, the third by touch or its own cap | `testKitVariantConfirm` |
| `Ui::TopPane`: `PaneSpec::coverArt` | `src/previewpane.h/.cpp`: a decoded RGBA cover fitted inside the cover's box (aspect kept, box filter); none: today's SURFACE place and the picture's mark | `testKitLayoutReaders` (the image's own check) + `testKitNewPreviewPane` |
| `lay::busySheet(twoKeys)` and `Kit::BusySpec::key2` | `src/kitlayout.*`, `src/ui_kit.*`: Hide and Stop share the sheet's row, each 56 tall; one key stays full width | `testKitLayoutReaders` |
| `lay::readerHeader` | `src/kitlayout.*`: the readers' 54-tall header: back, the inline title's room, the bookmark (a book's) and More; their touch areas reach the panel's edges | `testKitLayoutReaders` |

## 2. The readers' chrome

`src/ui_docs.cpp` (TXT, EPUB, PDF) and `src/ui_manga.cpp` (the comic):

- The header is `Kit::header` with `inlineTitle` and `titleFont = F_SMALL` (18 units): the file's or chapter's name
  beside the back key, its own line under it (a text's `7% · UTF-8`, a PDF's `Page 3 of 48 · 150%`, a comic's series),
  the status/More slot at the right. The text reader also has the spec's bookmark key (`G_BOOKMARK`, ACCENT), one tap
  adding the bookmark at the place (the same code the Bookmarks sheet's first row runs); the PDF and the comic have
  More, which opens the options / Reading menu as the old touch keys did.
- The key rows are `Kit::key` (and `Kit::iconKey` for the header): the text's Contents, Bookmarks, Options, Book mode
  and the small Light key; the PDF's nine; the comic's Prev chapter, Fullscreen, Light, Next chapter. Their Rects, caps,
  pressed and off states and touches are the old ones; only the drawing moved.
- The sideways Book bar (`KitNew::bookBar` on `lay::bookBar`, its first caller): a tap in the middle of a held-sideways
  text page, or START, shows it upright along the panel's edge; its keys are Contents, Text, Light, Find and **Back to
  scrolling**, with the title, the percent and the chapter line. A comic's is Pages, View, Light, Next chapter and
  **One screen**, with the spread's numbers on the slider (touch seeks). Taps off the sheet and START hide it; any key
  acts on the spread and hides it. The bar is turned onto the panel with a target texture, as the kit-new gallery draws
  it. The old keys stay: SELECT, X, Y and L3 still do what they did in Book mode.

## 3. Info

`src/ui_fileops.cpp`: `infoRows` became `infoGroups` (`Ui::InfoGroup`), and the `FileInfoSheet` line walk draws a group
with `Kit::sectionLine` and its rows with `lay::fileInfoRow` (26 tall, dim 120-unit label, value after it). **File** is
the old list (name, kind, size, length, where, access, changed, path); a file that is the one loaded adds **Video**,
**Audio** and **Subtitles**, one row per track with `trackLabel`'s language and format. `openFileInfo` takes the
`Player` (the callers pass it) so the tracks are the player's own; the older `drawFileInfo` dialog walks the same groups.

## 4. The update question

`Ui::drawUpdatePopup` (`src/ui_menus.cpp`) is now the kit's sheets, one per state:

- **Available**: a `Kit::confirmSheet` with three keys, Update now (primary, A), Later (B) and Not until tomorrow; the
  same reminder time as before.
- **Downloading**: the `Kit::busySheet` with a spinner, the percent big, the bar, Hide (B, closes the sheet; it goes on)
  and Stop the download (Y). `Updater::cancel()` kills the curl (its part file names it in the command line), removes
  the part files and goes back to Idle: nothing was changed.
- **Ready**: the two-key confirm, Later safe and focused (B) or Restart now (A).
- **Failed**: a red confirm sheet, the reason, Try again (A: downloads afresh), Close (B) and Details (Y: unfolds the
  whole reason in place). The version in place is untouched.

`popupRow` returns the sheet's own Rects so a tap hits what is drawn; B answers with `popupSafe()`.

## 5. All recent by day

- `PlayHistory` keeps when each entry played: `history.tsv` is now `path<TAB>epoch` a line. An old file (a path alone)
  still loads, with `NO_TIME`; it is under **Earlier**. `times()` and `timeOf()` give the times, `dayGroup()` (pure,
  civil days, so DST doesn't matter) gives Today / Yesterday / Earlier, and `dayName()` the heading.
- `Ui::player` records `time(nullptr)` on every add (`Player::open` and `addRecent`).
- All recent's listing (`src/ui_browse.cpp`) inserts a heading row (id `-3`) before each day's items; `Ui::Home::drawAllRecent`
  draws it with `Kit::sectionLine`. The list's keys pass headings over: `Ui::listKey`'s Up / Down / Left / Right and
  `Ui::Browse::fitList` (so a rebuilt list never leaves the cursor on one), and taps on a heading do nothing.

## Behaviour that changed on purpose

- A tap in the middle of a held-sideways text page, or START, now shows the Book bar rather than Text options; the
  bar's **Text** key opens the options, so nothing is lost. A comic's START shows its bar; **View** opens the Reading
  menu it used to open.
- A failed update's A is **Try again** (the spec's), where it used to close.

## Skipped

- No gallery pages: the kit's new pieces are tested and drawn by the screens that use them, and `--press gallery` still
  lists 1..20.
- No shots: this session's harness allows the build and `./build/core-test`, not running the player (the same limit the
  kit notes recorded). The drawing was checked by reading it against the mock-up: the owner should look at a text
  reader's header and Book bar, Info on a video, an update question, and All recent first.

## Checked

Desktop build and `./build/core-test`: `core: all passed` (the new `testKitLayoutReaders`; `testKitVariantConfirm`'s
third key and `testHistory`'s times included).
