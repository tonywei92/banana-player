# Quick settings on the kit, and the real art in the rail's key and the panel's block (2026-11-19)

Two changes, one panel: the rail's Now playing key and Quick settings' Now playing block drew
`Kit::artPlaceholder` for a song that had art; and Quick settings was still the old drawing (tick boxes, the
old hint footer) under its Overlay. The panel is now built on the kit (docs/redesign/components.md "Quick
settings panel"; the mock-up's `quick` and `quick-playing` in docs/redesign/src/screens.js), and both places
show the song's real album art through the cover worker Home already uses
(docs/notes/2026-11-19-home-art.md).

## The one art cache (was Home's)

- `src/ui.h` / `ui_menus.cpp`: the texture LRU, the per-path failure set, `drawSongArt` and the ready-handler
  moved from `Ui::Home` into Ui (`songCoversReady`, called from main's `evCover`). Home's cards, the rail's
  Now playing key and the panel's block share it; the key still carries the `HOME_COVER_KEY` prefix, so the
  routing in `src/main.cpp` only changed its target. A path is asked once per box (the key holds the box's
  device pixels and radius: the rail's 30 square is not the block's 48), the placeholder is drawn until the
  answer, and a file with no art keeps it for good.
- `Ui::drawNowArt(bp, r, rad)`: what plays in an art's box, as Home's cards have it — a song asks for its art,
  a video shows its film mark on TRACK (no picture decoded for one), and the callers keep their own idle
  drawing (the block's music mark). The rail's mark is drawn at cell 2 (a 30 box), the block's at 3.
- `src/ui_kit.h` / `ui_kit_new.cpp`: `KitNew::rail` takes `std::function<void(Rect)> npPicture` instead of the
  placeholder's key string; `src/ui_shell.cpp` builds it and its `signature()` follows `ui.artSerial` (and
  says audio or video, so the mark change redraws). The QuickSheet overlay's signature carries the serial too.
- `src/ui_home.h` / `ui_home.cpp`: Home keeps only its calls; its `sig()` follows `ui.artSerial`. Nothing of
  the drawing changed on Home.

## The panel

- `src/kitlayout.h` / `kitlayout.cpp`: `lay::QuickPanel` — one place for the panel's boxes, tested in
  `tests/core_test.cpp` (`testQuickPanel`). 592 wide, 24 from the sides, radius 18, padding 16, centred in the
  height above the hint bar: the title line (48), the Now playing block (64), then 8 apart the level rows (52)
  and the switch tiles two to a row (54), Quit (46) at the foot. When they don't all fit (the handheld's seven:
  Volume, Top, Bottom, the three tiles and Quit) the level rows share what is left, never under 24, so the
  block, the tiles and Quit keep their size — the six-row case fills the 450 above the hint bar exactly.
- `src/ui_menus.cpp`: `drawQuick` rebuilt on the kit. The veil and the panel (`Kit::flat`, SURFACE with its
  edge); the title in Pixelify 20 with the battery at its right; the level rows drawn as Kit rows (nothing
  until the cursor, then a lit button) with `Kit::key` steppers (quiet in a row in Clean, 56 × 44), the
  kit's `volumeBar` (its number now right of **+**, as the mock-up draws it) and `Kit::progress` for
  brightness; the switches as tiles drawn with the kit's tile look and `Kit::toggle` at the right; **Quit** as
  the spec's one red-filled key (Classic always — `Kit::key`'s danger is red ink on cream, and the spec for
  this panel says DANGER fill, which is also what Home's quit dialog draws); the kit's hint bar instead of the
  old `drawHints` footer (with Button hotkeys off it centres "Tap outside to close").
- `src/ui.h`: the old Rect helpers (`quickPanel`, `quickNowRect`, `quickRowRect`, the steppers' boxes) are one
  `quickLayout(const Player&)` now; `quickKey` (the cursor and every key), `quickChoose`, `quickAdjust`,
  `quickSlide` and the touch rules are unchanged — `quickPointer` tests the same pieces from `lay::`.

### The Web tile's own key

The mock-up's Web dashboard tile has only a switch, but the row's behaviour (RIGHT then A, or a tap, opens the
dashboard's page) had to stay. A labelled "Settings" key (98 wide) would cut the tile's name to 73 of the 121
"Web dashboard" needs, so the key is a chevron alone in 40 × 40 — the row's "this leads to a page" mark —
between the name and the switch; the name keeps its 122. The cursor's half (quickSub) is the key's edge, as
the row's Settings button had it.

## Checked, and not

- `cmake --build build -j` and `./build/core-test` pass (`testQuickPanel` covers the six-row, seven-row and
  smaller panels: the boxes, the pieces' spacing, the web key, the tiles two to a row).
- Three shots in `.scratch/shots` (`qs-song`, `qs-video`, `rail-song`; generated media only: a testsrc2 cover
  attached to a sine, a testsrc2 video, run with `BP_BACKLIGHT_DIR` pointing at a fake backlight so the
  Brightness row shows). The decode logs confirm the pipeline end to end: the rail asked at 52 × 52 and the
  block at 88 × 88, both arrived before the shot; the video run decodes nothing (its film marks); the Home run
  shows the cache serving the Continue card and a Recent card beside the rail. The pictures themselves were not
  looked at by this run (the session's reader has no image input): the row, tile and key geometry comes from
  the test and the measured name room, not from the eye. The owner finds the real bugs on the handheld; the
  panel's Clean look and the incognito note line are unverified.
- A test-run quirk found on the way: a file passed to `banana-player` as a path that *starts with a dot*
  (`.scratch/media/song.mp3`) is classed as neither audio nor photo — `hasExtension` rejects names whose first
  character is '.', and a relative path like that starts with one. Use an absolute path when driving a run
  with `--press`.
