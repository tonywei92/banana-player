# Quick settings' Now playing block as the panel's lead (2026-11-21)

The owner: "make the mini player on Quick settings stand out more." Branch `c-qsmini`.

## What it is now

- **Height from the title line, not from the rows.** The usual panel (two level rows, three tiles) already fills the
  450 above the hint bar, and the handheld's seven squeeze the level rows to about 30. So the block grew 64 → 72 by
  taking 8 from the title line (48 → 40): the panel's height and every row are as they were (`QUICK_TITLE_H`,
  `QUICK_NOW_H` in `kitlayout.h`).
- **Its own ground while something plays:** SURFACE under the accent's tint (`theme::tintOf`, the 17% Clean uses for
  what is selected), the one coloured surface in the panel; the cursor on it adds the ACCENT ring. Idle it is a plain
  SURFACE well, so "something plays" is itself the difference. High contrast (Classic) keeps SURFACE and its edge.
- **Pieces:** art 56 (radius 8); a state line in small capitals (NOW PLAYING / PAUSED, CAN'T PLAY in DANGER), the
  title in Pixelify 16, artist · time in DIM_ON; a 4-unit progress line (TRACK, ACCENT played) along the words'
  column, level with the art's foot (it was a 3-unit line along the card's foot, which met the rounded corners);
  Previous / Play / Next 56 tall, Play / Pause the panel's only ACCENT fill.
- **Boxes:** `lay::QuickPanel` gained `nowText` and `nowLine`; `drawQuickNow` draws on them and no longer borrows
  `lay::miniBar(box)` (only the kit's gallery uses the mini bar's measures now). Touches and keys are unchanged: the
  three keys' Rects, and the rest of `q.now` opens the player.

## Not done, and why

- **Match album art does not tint the block.** components.md says the backdrop leaves Quick settings alone, the
  backdrop is a whole-panel page (`drawBackdrop`), and the theme's tint reads in every theme, light ones included,
  without a second colour source. If wanted later: `artbg::Backdrop::ground` is the tinted colour to start from.

## Checked

`core-test` (testQuickPanel: the new boxes inside the block, apart, the keys 44 or more); shots in Yellow and Gundam
on the Mac (no level rows there). Not seen on the handheld: the seven-row panel, High contrast.
