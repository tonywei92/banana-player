# The player's chips open their own sheets (2026-11-19)

The owner's decisions of 2026-11-19 (`docs/redesign/backlog.md` › Decided): no View sheet and no tabs on the player;
Repeat and Shuffle change in place; the player's More starts with the same chips; a photo's SELECT opens More, and its
View sheet is gone. The screens are `docs/redesign/src/screens.js` "video", "video-more", "video-view" (now
Subtitles), "video-picture", "video-adjust", "video-speed", "music", "music-more", "music-view" (now Display),
"music-eq" and "photo-more"; components.md › State chip and More sheet.

## What it is now

- **A chip, a sheet.** `PlayerPage::chipAct` opens one titled sheet per chip. A video: Subtitles, Audio, Picture,
  Speed. A song: Equalizer, Sound FX's Effects, Display, Speed. The bodies are today's `Ui::ViewSheet` /
  `Ui::SongViewSheet` tab bodies with their `L / R` tab stepping and their tab strip removed: `Kit::sheet` draws the
  chip's glyph and name in the header instead. `L` and `R` fall through and do nothing in a sheet. `SELECT` or `B`
  closes; a song's Display keeps `SELECT` as **Next mode**.
- **Repeat and Shuffle in place.** The video's Repeat chip steps `Off → All → One → Off` with the List sheet's own
  setting (`p.setSettings`, so the Session follows), and One wears the new `G_LOOP_ONE` (a loop with a 1 inside). The
  song's Shuffle chip toggles through `SessionHost::setShuffle`. Neither puts a note on the top screen; the chip's
  word, colour and dot say it. Both are still at the top of List (Y).
- **More's first row.** `MoreSpec` now has `chips`, and `lay::sheet` / `lay::Sheet` grew a chip row above the tiles
  (`SHEET_CHIP_H` 52, the same width rules as the player's face: 5 across 608). The player's More is its five chips
  (cursor on the first), then Lock / Screen off / Info / Stop, then Like, Add to playlist…, Show in folder, Play on….
  `A` on a chip opens that sheet and More closes; `A` on Repeat or Shuffle changes it and More stays. The old View row
  is gone. `SELECT` closes More on the player and a photo (where it opened it), like X and B.
- **Picture › Adjust…** is a last row of the Picture sheet. It opens `Ui::AdjustSheet`, a page over Picture: four
  two-way rows (Brightness, Contrast, Saturation, Gamma), `LEFT / RIGHT` ±5, a slide anywhere on a row, `A` to 0,
  `X` all four to 0; `B` or its ‹ goes back to Picture, `SELECT` closes both. `Player::setVideoAdjust` sends mpv's
  `brightness` / `contrast` / `saturation` / `gamma` at once and applies them to the next video; a photo and a song
  are set back to 0. They last while the app runs (not in settings.conf).
- **A photo.** `SELECT` or `X` opens the photo's More: four chips changed in place (**Shown** Fit / Fill, **Keep
  zoom**, **Pan area**, **A drag moves**; `away` yellow where they differ from the normal), then Info, Show in
  folder and Photo settings (Settings' Photos group). There was no photo View sheet in the code to remove; its Fit
  key now steps Fit / Fill itself on the touch page (the key's cap is gone), and `SELECT` is More. Zoom (UP / DOWN)
  and Turn (L3) keep their keys.

## Files

`src/ui_player.{h,cpp}` (the sheets, More, Repeat/Shuffle, the photo More, Adjust), `src/ui_more.cpp` (chips and
SELECT closing), `src/kitlayout.{h,cpp}` (a chip row in the sheet), `src/ui_kit.{h,cpp}` (MoreItem's chip fields, a
`Kit::sheet` over a `ListSheet`), `src/ui_glyphs.h` (`G_LOOP_ONE`), `src/player.{h,cpp}` (the four mpv properties),
`src/ui.cpp` / `src/ui_main.cpp` (the photo's Fit key toggles; SELECT opens More), `src/ui.h`, `src/settings.cpp`
(the Music display help names the Display chip), `src/main.cpp` (the `--help` line), `tests/core_test.cpp` (the sheet's
chip row and cursor), `FEATURES.md`, `docs/architecture.md`.

## Checked

Desktop build (`cmake --build build -j`) clean; `./build/core-test` ends `core: all passed`, including new checks that
the player's More is `52` tall for its chips, the photo's are `147` wide, and `sheetMove` crosses chips → tiles → rows.

`--press` with generated media (`.scratch/playershots`):

- video: `more`, `repeat-off`, `repeat-all`, `repeat-one`, `repeat-off2`, `subs`, `picture`, `picture-adjustrow`,
  `adjust`, `adjust2`, `picture-back`, `more-select`, `more-header`, `more-x-closed`, `face-r0`…`face-r3` (a tap on the
  face's Repeat chip stepped it in place), `touch-chip-sheet`, `touch-chip-inplace`, `adjust-sel`, `adjust-sel-closed`
- song: `song-more`, `song-display`, `song-display-next` (SELECT stepped the mode), `song-shuffle` (More stayed open,
  no note), `song-eq`, `song-speed`
- photo: `photo-more`, `photo-keepoff`, `photo-rows`, `photo-closed2` (SELECT closed), `photo-fill`, `photo-fit2`

The first photo run showed SELECT not closing More (`MoreSheet` had no SELECT case); fixed and re-shot
(`photo-closed2`, `more-x-closed`).

## Not checked / not built

- The handheld's panel, other themes, and a non-Latin track name on a chip; the screenshots are the desktop's
  `dark` theme at 640×480.
- The video **looks** (Sharpen, Scanlines, LCD grid…) and the spec's **Use for: all videos / only this video** are
  not built. Adjust is the four mpv properties above, runtime only; it is not in `settings.conf` and has no Settings
  row, so it starts at 0 on every launch.
- The photo's More has no Slideshow, Copy, Move, Rename or Delete rows (none of those exist in the photo viewer yet).
- Tapping a More chip was not driven (`--press` drove it with the D-pad); the pointer path shares `MoreSheet::choose`
  with the keys.
