# Faders and Lanterns replace Pocket console and Rain check (2026-11-19)

The owner's decision: of the fifteen visualizers, two are replaced by styles designed in
`.scratch/viz-ideas.html` (a 640 × 480 canvas preview). **Faders** takes Pocket console's place (third in the list,
after Mirror bars); **Lanterns** takes Rain check's (after Rocket). Both are native styles: the player measures the
sound and draws flat shapes in GL, with no mpv graph (docs/notes/2026-11-19-viz-native.md). This note is what they are,
the ids, the theme inks, what the old code took with it, and what they cost.

## Ids and places

- `VIS_POCKET` became `VIS_FADERS`, `VIS_RAIN` became `VIS_LANTERNS` (same enum values, same places in `VIS_SHOWN`).
  Their words are `faders` and `lanterns`; their labels "Faders" and "Lanterns".
- A saved `settings.conf` with the old ids still loads them: `visualizer = pocket` reads as Faders and
  `visualizer = rain-check` reads as Lanterns (`readVisualizer` in settings.cpp). They are written back under the new
  ids.
- Both have no graph of their own. Where a graph is needed (`visualizerGraph`, the network fallback) they are drawn as
  Peaks: Faders is mapped with the styles after Ribbon because it sits before `VIS_LED` in the enum, and Lanterns by the
  `>= VIS_RIBBON` rule.
- The Display grid glyphs are `G_VIS_FADERS` (five rails with a knob at a different height each) and
  `G_VIS_LANTERNS` (two paper lanterns on a string). FEATURES.md's Music list and the whole-panel exception list are
  updated; Rain check no longer belongs to the latter.

## What they draw

### Faders

A mixing desk seen from above: sixteen thin rails across the panel, a knob riding each at its band's level, bass at
the left. A small tick holds each band's recent peak for 0.35 s, then falls at 0.55 of the height a second (the
preview's numbers, on the 0..252 level scale).

- Alone: rails from y 150 to 424, x 76 to 564. In the band: 310 to 456, the same width.
- Three inks on BG: the rail in LINE, the lit part of the rail and the peak tick in DIM, the knob in INK.
- Silence: every knob sits on the floor in a row; no tick shows.
- Flat shapes and no blending: 16 rails, 16 lit parts, 16 ticks, 16 rounded knobs.

### Lanterns

A string of paper lanterns across the panel, each lit by its band. The beat nudges the string (alternating sides, a
small per-lantern hash), the lanterns swing and settle; paper lights quickly and cools slowly (30 and 5 a second, as
the preview).

- Alone: seven lanterns, string at y 128 with a 40-unit sag, bodies 58 × 76. In the band: nine smaller ones, y 290,
  sag 18, bodies 40 × 52.
- Unlit paper BUTTON; lit paper INK, mixed opaquely (the body is a fan in VI_INK_RAMP, its mix the lantern's level).
  A bright one has a halo of INK 10% on BG (opaque, not blended). Caps and tassels DIM, the string LINE.
- Silence: unlit lanterns hanging straight and still.

## The shared derived inks

Both styles use the preview's shared helpers, and no colour of their own:

- `visualizerInk()`: ACCENT moved towards TEXT in 10% steps until it reaches 3:1 on BG (`inkOn`, the helper
  pocketInks used). In Dark the accent is already bright enough, so it stays the accent; on Light it moves towards the
  text.
- `visualizerLine()`: RULE 25% (light theme) or 35% (dark) of the way to DIM, because RULE is very close to BG in the
  dark themes.

They are read from the theme each frame in topviz.cpp (`VI_INK`, `VI_LINE`), and mixed there for the lantern body and
halo (`VI_INK_RAMP`, `VI_INK_BG`). A theme change recolours them at once.

## Removed

Nothing uses Pocket console's old scene any more, so `pocket()`, its sprites, `pocketGraph()` and `pocketInks()` /
`PocketInks` are gone, along with the `VI_P_*` ink enum values and the Pocket console line layout in
`Player::updateOverlay` (the lines are back to the ordinary top-left theme layout over a visualizer). Rain check's
drops, splashes, puddle rings, its `cover` box and the umbrella are gone too (`rainCheck()`, the state's `Drop`,
`Splash`, `Puddle`, `dropN`/`splashN`/`ringN` and `cover`). `vizFullPanel` is now Sparks, Starfield and Sleepy moon
only.

## Tests

`tests/core_test.cpp`:

- settings: `faders` and `lanterns` parse and serialize; `pocket` and `rain-check` read as them and are written as the
  new ids; labels; the graph fallback is Peaks alone and over the art; the fifteen-style order and cycle include them.
- shapes: Faders' 16 rails, knobs and no lit part in silence; a full band, the knob at the top, the peak tick after the
  levels fall; nothing above row 280 in the band. Lanterns' unlit paper and no halo at rest, a beat moving them and
  lighting their paper (halo present), the still/unlit rest after the sound stops, all shapes in the band.
- themes: `visualizerInk()` is 3:1 on BG in every theme, `visualizerLine()` is not BG; the Night city graph hashes
  still pass.
- The old Pocket console graph hashes and its `pocketInks` contrast checks were removed with the style (nothing can
  build that graph any more). The Night city hashes remain the graph fallback's regression check.

## Check (Mac, small build)

Generated with ffmpeg (three sine tones, 55/220/880 Hz, amplitude-modulated; a 600 × 600 cover), played in Dark at
60 fps:

- `settings.conf` `visualizer = faders`, alone and `music-show = both`:
  `.scratch/shots/faders-alone-top.png`, `.scratch/shots/faders-art-top.png`
- `settings.conf` `visualizer = lanterns`, alone and `music-show = both`:
  `.scratch/shots/lanterns-alone-top.png`, `.scratch/shots/lanterns-art-top.png`
- Frame cost in the `viz-native: draw` log (1280 × 960, alone): Faders 0.23 ms ours (shapes 0.09, GL 0.13), 2061
  vertices, 0k blended; Lanterns 0.14 ms ours (shapes 0.06, GL 0.08), 1750 vertices (max 1938), 0k blended. Both are
  flat geometry only.
