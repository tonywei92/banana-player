# Match album art, wired (2026-11-20)

Item 3 of the status note's Next list: the built pieces (`artbg::artBackdrop`'s maths, `Kit::backdrop`) wired to the
song player and the top screen, on the cover worker, with the cache, the 0.5 s cross-fade and the switch. The spec is
`docs/redesign/components.md` › "Match album art" and the `dyn-art` screen; the pieces' own note is
`docs/notes/2026-11-19-kit-pages.md`.

## The scope, as the newest spec draws it

The song player and the top screen only. `backlog.md` › Decided once named Quick settings' block too (the older line,
"as drawn"), but components.md's "Where" — the newer text, which carries the light-theme decision that takes back part
of that same line — says "Quick settings' block, the rail's key and every other page are not touched", and the status
note's item names only the player and the top screen. The switch's default comes from the spec's own drawing
(screens.js `set-music`): "Match album art — On".

## The setting

`Settings::matchAlbumArt` (default on), a flag row in Settings › Music & sound between Music display and Visualizer
style (`SG_MUSIC` order 15), and the same switch at the foot of the song's Display sheet — `lay::displayPage` gained a
`foot` Box (46 tall, 10 under the styles grid; w 0 when the sheet is too full, which twenty styles make it), the
sheet's cursor reaches it from the grid's last row (A, LEFT/RIGHT or a tap toggle it), and the row draws as the
sheet's rows do (a radio dot for on).

## The worker and the cache

The cover worker (one thread, Manga's covers and the song art already) gained a backdrop job: `wantBackdrop(key,
path)` with the `"bg:"` key prefix, which is how main routes the answer to `Ui::backdropReady`. The job hashes the
art's **encoded** bytes (`artbg::fnv64`), reads `<state dir>/artbg/<hash>.bg` (a hit bumps the file's date; the
eviction, `artbg::evict`, is walked oldest-first past the 16 MB cap), and on a miss decodes the art at 320 wide and
runs the pure maths once. No art in the file or beside it: `ok` false — the plain ground is the answer.

## The answer, once, both panels

Ui holds one answer (`bgNow`: the hash, the `artbg::Art`) and the one it fades from (`bgFade`); a new answer starts
the 500 ms cross-fade, and a first answer fades in from the fallback ground (the slot is filled with it, so both
panels agree on what "before" was). `Ui::tick` asks once per playing song; the switch off, a video, a photo or High
contrast drops the answer and the textures. `backdropOn()` is the one rule both panels ask.

- **The player page** (bottom): `Ui::drawBackdrop` at the page's start — the old backdrop under, the new one's
  picture and veil at the fade's alpha (`Kit::backdrop` gained the `fade` parameter; the veil's fills scale with it).
  `signature()` takes the backdrop's hash and the fade's step, and `Ui::animating()` covers the fade so the loop
  passes at 16 ms.
- **The top screen** (GL): its own `PageRenderer` pages (as the QR's and the pane's), composed from the same answer —
  `artbg::composePage`, new in bpcore and tested: the 160 x 120 picture with the theme's veil baked per row (the
  fallback a gradient page), composed again only when the answer, the theme or the fade's state moves. A cross-fade
  is two pages: what it fades from, drawn plain, and the new one at the fade's alpha (`Placed.blend`).
- **mpv's ground**: `Player::updateOverlay` clears it for any song while the switch is on (not High contrast, not
  `noLayer`), so mpv's picture — the art and the song's lines, every display mode — composites over the backdrop
  through the same layer the native visualizer uses: the ground step in `TopScreen::nativeFrame` draws the backdrop
  pages instead of clearing the theme's BG, and the shapes of a native style draw over them. A panel that cannot make
  the layer keeps the visualizer's fallback (`noLayer`: the theme's ground again, no backdrop). The pass conditions
  in `TopScreen::draw` take the fade's steps (mpv needs no new frame for them).

## Checked

Desktop build; `./build/core-test`: `core: all passed`, with the new `testArtBackdropPage` (the composed page: the
veil's exact mix at the top row and the foot, the fallback's gradient, a light theme's one colour) and the
`displayPage` foot row's layout checks (10 under the grid, clear of the hints, declined when the sheet is full).
Not checked from this session (the harness refuses the player binary): the four looks on a screen — the player page
over a backdrop, the top screen's composite, the cross-fade's half second, the light version — all waiting on the
handheld or the screenshot audit. The log lines to read there: `backdrop: … for <file>` on each answer, and
`backdrop: the top panel composes the song's ground` once.
