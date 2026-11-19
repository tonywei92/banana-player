# The spacing scale on the build (2026-11-19)

Brought the existing layouts and the kit's internals onto the scale in components.md › Spacing (2 / 4 / 6 / 8 / 12 /
16 / 24). Named constants `lay::SP2` … `SP24` live in `src/kitlayout.h`; the numbers below use them where they were
touched. No component was added: only existing numbers moved.

## What moved

- **Shell** (`lay::shell`): the body starts 6 under the header (was 0 in the box, with each page adding its own 2);
  with the mini bar the body ends 8 above it (was 4). `MINI_FOOT` 36 → 38, so the bar is 8 over the hint bar and the
  panel's foot is 30 + 8. Pages that added their own top inset dropped it: Home's column and the Extensions page start
  at `body.y`; All recent's 28-tall kind chips stay in the 6 between the header and the body because its rows must keep
  the old list's first row (`Ui::ROW_Y`, 84). Settings keeps its 64 (its own 4 under the shell's 6).
- **Filter / view chips**: `CHIP_H` 40 → 44 and the row (`CHIP_STRIP`) 56 → 44. Home's Recent filters are the chips'
  own height now; the touch area is the chip's 44.
- **Home** (`lay::homePage`): Continue card → filters 10 → 12; filters → Recent cards 4 → 8; cards → shortcuts 10 → 8.
- **Header**: the status cluster ends 8 before More (was 10); the right slot (Settings' "L2 About · Subtitles R2", the
  read-only badge) keeps 8 before the status cluster, so the pair no longer touches the clock. `Kit::header` gives the
  player's status marks the same 8 before More (was 10).
- **Player** (`lay::playerLayout`): the rows are exactly 8 apart and the Next line 4 over the hint bar, which puts the
  transport at 76 tall (was 74), keys at 272, chips at 336 and Next at 396; Next used to be 2 over the hints.
- **Kit internals**: a key's icon gap 7 → 8 (`Kit::key`, the Book bar's keys too); a row's lead/icon gap 11 → 12
  (`Kit::row`); hint items 15 → 16 apart (`Kit::hints`, the Book bar's hints); a tab's padding 13 → 12.
- **Sheets**: `SHEET_PAD` 14 → 16 (the same as the player's column); the two-column row gap follows it. Rows were
  already 46, as the List sheet's are.
- **Knock-on**: the smaller body means a playlist page shows 5 rows with its keys and 5 (was 6) with the mini bar and no
  keys; Settings' seventh row and its five tile rows end 2–4 into the body's 8 above the mini bar, over the bar's own
  8 (the page keeps its 64).

## Kept, not redesigned

- **Quick settings and Quit**: their placement stays centred over the rail, as the owner asked. Their rows are 54 tall
  and 62 apart, with a 16 gutter: no inner gap was off the scale without moving the panel, so nothing changed there.
- **All recent's chips**: still the old list's 28-tall custom chips, because its rows keep `ROW_Y` (84).
- **Sheet rows** that are not 46: none were found among the `lay::sheet` / `lay::listSheet` users; Quick settings' rows
  are the old dialog's, not a sheet.

## Tests and pictures

`tests/core_test.cpp` (testKitLayout and the kit-variant tests) asserts the new shell insets, gaps, chip rows, sheet
gutters and player rows, and still tests the same boxes the drawing uses.

Pictures (generated media, `testsrc2` + a sine tone, `ffmpeg`, `.scratch/spacing-demo.mp4`), `BP_SHOTS=.scratch/shots`:

- `.scratch/shots/home-playing-bottom.png` — Home while it plays (Now playing card, 44-tall filters, 8 gaps).
- `.scratch/shots/player-bottom.png` — the player (four rows 8 apart, Next 4 over the hints).
