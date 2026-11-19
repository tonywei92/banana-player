# Pocket console, redesigned (2026-11-19)

The owner asked for a redesign of Pocket console, then, seeing the first pass: "it doesn't feel clean, make it
clean". This is what it is now, how it got there, and what it costs. Code: `pocket()` and the `VIS_POCKET` block of
`vizStep` in `src/viznative.cpp`, `pocketInks()` in `src/settings.cpp`, the lines' place in `Player::updateOverlay`.

## What it is

A little LCD in a thin bezel: an original pocket game screen, no real product's look, name or marks. Everything on
the screen is a dot of 8 units under a 1-unit grid line, all boxes, nothing blended, no texture.

- **The frame.** 20 units of bezel left and right, 16 above and below (alone); a 3-unit line for the screen's recess;
  the screen's corners cut in three steps of bezel, the line going round them. The screen is 75 x 56 dots alone.
- **The bars.** 15, as before (40 Hz..8 kHz, contrast pushed, no bed cut: `vizStyleBands`), 3 dots wide with a gap
  of 2, one ink, 32 rows at most; a cap in mid rides each bar's held peak (Peaks' fall, 102 levels a second) and is
  hidden while the bar reaches it. They stand on a floor line (mid) their own width.
- **The road.** Under the floor, dashes in the ghost shade (2 dots every 5, one a bar) pass by at 2 to 24 dots a
  second with the loudness (`loudShown`, eased), a dot at a time.
- **The sun** (alone only), top right: a core in mid inside a ghost halo. The halo is a dot wider while the bass
  (bands 0..3) is over 0.6; on a beat the core is a dot wider and four rays show while the beat rings (`pkPulse`:
  1 on a beat, gone in 0.4 s), the straight four on one beat, the slanted four on the next.
- **The foot** (alone only): a heart under the bars' left edge (full while a beat rings, small otherwise), a note
  under their right (a dot's hop on a beat).
- **The lines** (Visualizer only) are on the screen: from (28, 30), over the bars' left edge, in the pixel type and
  the screen's ink as before, at most 350 units as measured (the pixel type is a fifth wider: to 448), which keeps
  them short of the sun. A longer title is the marquee, a longer artist or album ends in an ellipsis.
- **Art + visualizer:** the same screen as a strip in the band (rows 280..480: 8 units of bezel above and below,
  75 x 23 dots), the bars 17 rows with their caps, the floor and the road; no sun, heart or note. The art and its
  lines are where every band style has them. (Before, the band had no screen: lit dots on BG in the bright pair.
  `PocketInks::light` and `bright` are now only the graph's, the network fallback.)

## The shades

`PocketInks` got three: `ghost` (15% ink on the screen: the halo, the dashes), `bezel` (BG 9% towards TEXT in a dark
theme, 16% in a light one) and `edge` (the screen 70% towards BG in a dark theme, 45% towards the ink in a light
one). The first four are unchanged (Yellow's greens, another theme's from its accent: 2026-11-19-viz-theme.md), so
the graph and its test hashes are as they were. New `VizInk`s: `VI_P_GHOST`, `VI_P_BEZEL`, `VI_P_EDGE`.

## How it got there

Three directions were mocked as canvas frames (`.scratch/mock/pocket-directions.html`, not in the repository):
**A** a framed meter with a status line, **B** A as the stage of a small scene (sun, clouds, road), **C** a runner
over a ground made of the last seconds of bass. C was the most a game and the least a visualizer; B was picked: the
bars stay the picture, the scene gives the beat and the loudness somewhere to show.

The first build of B had too much: a ghost column above every falling bar (a slow LCD), two-tone bars, three clouds,
eight rays always on, a status line of low / mid / high letters and pips, a glint across the glass. In the shots the
ghost columns and the pips were the noise, and the owner said so. Cut, in this order of how much each helped: the
ghost columns, the status meters, the clouds, the glint, the bars' second tone; then the bars went from 4 + 1 dots
to 3 + 2 (air between them), the rays show only while a beat rings, the floor and dashes stop at the bars' edges,
and the band lost the heart and note for taller bars. Rule of thumb that came out of it: one moving thing per
signal (bars: spectrum; caps: peaks; road: loudness; sun, heart, note: the beat), and nothing drawn that isn't one.

## Cost

Desktop, a 2x window (1280 x 960), `viz-native: draw` lines: alone 0.15 ms a frame of ours (shapes 0.06, GL 0.09),
about 1420 vertices, 2 draws, 1.8 M px filled (about 460 k on the 640 x 480 panel: the screen once, the bezel as
four strips, the grid's 131 lines, the bars), **0 blended**; the band about 1400 vertices and less fill. The first
native Pocket console filled about the same (the screen box, 140 grid lines). Not timed on the handheld: nothing in
it is heavier than what that one drew.

## Checking it

`.scratch/run.sh THEME` (not in the repository) plays a generated song with `visualizer = pocket` and takes
`shot:`s alone and after `set:music-show=both`. Two things it had to learn: played from `.scratch/media` (a path
with dot folders in it) the file was not shown as a song (no visualizer, the art as a picture; why was not looked
into), so it is played from a plain folder; and `BP_AO_PCM` is untimed, so the song ends at once: the Mac is muted
for the run instead. Not checked: a title long enough for the marquee, and the handheld itself.
Tests: `core_test.cpp`, "Pocket console: the bezel round a 75-dot screen".
