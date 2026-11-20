# The four fixes the last shots asked for (2026-11-20)

The handoff's "Start here" (item 1 of the status note's Next list), one commit on `redesign-impl`. Where the spec
draws each thing is named; the kit pieces changed first, then the screens that use them.

## 1. The "From … on this card" line, drawn twice (Music and Videos)

The line was in the header's crumb *and* in the body, over the view chips (`musicHeader`'s line box). The spec draws
it once, in the crumb (`musicShell(tab, crumb, body)` — the body starts with the chips), so the body's copy went:

- `lay::musicHeader` lost its `line` box: the chips' 44 strip now sits at the body's top, and the grid under the
  chips gains the 30 units the line took (`ui_music.cpp`, `ui_videos.cpp`: the draw, the keys and the touches all
  read the same layout, so they moved together; Videos' `rowsShown` subtraction dropped the line's height too).
- The gallery's pages 11 and 17 drew the same line from `mh.line`: page 11 is the Music page's pieces (the line was
  its own demo on page 10, which stays), page 17 the Playlists view, whose "5 playlists · Liked first" is the crumb
  there too. Both now show the chips at the top, the grid 8 under them.
- The stale paragraphs in the 2026-11-19 notes (library-pages, kit-pages, kit-inventory) say where the line lives now.

## 2. Books' shelf heading with no books under it

With the Reading now strip shown, the body's 390 units leave the shelf grid 142 — a shelf cell is 156, so
`booksShelf` laid **zero** rows and the "Shelf · N" heading sat over nothing. The album grid's own rule fixes it: at
least one row is laid, hanging over the grid's foot (the hints bar, painted after the page, covers the hangover; the
row's covers are whole, its title line is the part that hangs — scrolling brings the rows fully into view). A test
now lays the shell's real body (`{90, 60, 538, 390}`, five reading, twelve on the shelf) and checks `shown == 1` with
the hangover, which the old test's taller 440-unit body never caught.

## 3. Search's keyboard past the right margin

`lay::keyboard` laid its letter rows on the whole 640 panel (a 10-key row is 625 wide, centred to 7..632 — past the
16-unit margins everything else in the keyboard respects), and Search then *fitted* that panel into the shell's body
by scaling a source box (16..624) the keys never respected: the first row landed ~7 units past the body's right
margin. The keyboard is now laid out inside a box the caller gives — `lay::keyboard(Box area, specialW, counts,
layers)`:

- every letter key the width the widest row fits in the area (48 on the 530-wide body, 56 panel-wide), each row
  centred, the field and the bottom row spanning the area — no scaling anywhere, and the mock-up's 10-column grid is
  what this is;
- no special pieces: no special row (Search's field went straight to the letters once, with a phantom gap where the
  panel's URL pieces sat);
- Search's `keyboard(body)` is the one call; the gallery's page 5 passes a panel box and looks as before.

`KB_KEY_W` (58) is gone: the width is derived, which is the fix (a fixed 58 cannot fit a 530-wide body).

## 4. The state line printed the Search page as "settings"

`Page::Search` fell through main.cpp's state-line chain to the "settings (from the browser)" branch. It now has its
own branch (`Ui::searchPageOpen()`, beside `settingsPageOpen()`): `press N: search q=… cursor=… [typing]`.

## Checked

Desktop build; `./build/core-test`: `core: all passed`, with the three layout tests rewritten where the pieces moved
(`testKitVariantKeyboard` — keys inside the area, a touch target still on the 530-wide body; the `musicHeader` chips
at the body's top; the Books shelf's one row in the real body). Screenshots in `.scratch/shots` and the push to the
handheld could not be run from this session: the harness allows `cmake` and `./build/core-test` only, refusing
`./build/banana-player`, `ffmpeg` and the bundle/push scripts — so the four pages still need eyes on them (Music and
Videos' views, Books with a book in progress, Search typing), easiest on the handheld at the next push.
