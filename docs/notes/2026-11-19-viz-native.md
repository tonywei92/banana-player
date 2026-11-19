# Native visualizers: the simple styles and Peaks drawn by the player (2026-11-19)

Why: the graph-built simple styles stalled mpv on the handheld (2026-11-19-viz-simple.md, "The stall"). A style the
player draws itself has no graph to stall, no sound reopen on a switch, and costs less. Mirror bars was the pilot (the
first half of this note); Peaks, Dots, Steps, Barcode, Rings and Horizon followed the same day ("The other six", below).

## How it works

- **The sound: a second decode.** libmpv gives the app no samples. `VizTap` (src/viztap.*) opens the song itself with
  libavformat/libavcodec on a detached worker thread: the first audio stream (what the blank graph's `[aid1]` plays),
  the others discarded, resampled by libswresample to mono float at 32 kHz. It decodes only around the time it needs
  and keeps half a second behind; a jump of the clock (an mpv seek) seeks it too. A network song (bpnet://, a URL)
  keeps the graph Mirror bars: the decode reads local files only and a second read would double the traffic. A file
  the tap can't open falls back to the graph for that song (`viz-native: <file>: the graph style instead`).
- **The clock: mpv's `audio-pts`**, observed (never read on the main thread). It is mpv's position of the sound being
  heard, its output's delay already taken off (`playing_audio_pts`: written pts minus `ao_get_delay`), so no extra
  `audio-buffer` shift is needed (Lite's tap ran ahead of the speakers; this reads the file, not the chain). mpv updates
  it on its playloop ticks: **about 5 a second here**, tied to the blank graph's 5 fps frames, so `VizClock` runs it on
  at the speed between updates; play after a pause restarts it from that moment. `time-pos` would not do: under a
  graph it follows the picture's frames.
- **The spectrum: PFFFT** (third_party/pffft, NEON on aarch64, SSE on x86), 1024 points (32 ms) with a Hann window,
  centred a frame ahead of now (the draw and the swap that follow). 40 log-spaced bands from 40 Hz to 14 kHz (the
  bass left; the lowest bands one bin each), each its loudest bin. The levels are the graph's: showfreqs' log scale
  (−120..0 dB as 0..252), `vizContrast(…, "4", "1.5", "160")`, the bottom 40 steps cut, eased as an LED meter (up at
  once, the whole height down in 0.15 s). One change: **+3 dB an octave from 500 Hz**, since log bands show music's
  falling treble more than the graph's near-linear columns did (`vizMirrorTargets`; a constant to tune by eye).
- **The worker** wakes at the Visualizer frame rate setting (30/45/60), takes one spectrum, stores 40 targets and
  pushes `evViz` (once until taken). Paused or seeking it sleeps; silent for half a second it stops waking the loop.
- **mpv's picture:** the style's graph is `blankGround()` (None's: the theme's ground at 5 fps, the sound through
  `asplit`), so None ↔ Mirror bars and the reader resting it change no graph (no sound reopen) and the audio track is
  never unselected. Art + visualizer: the art is the overlay (as for every local song), the band is ours.
- **The draw** (src/topviz.cpp, `TopScreen::drawNative`): when the tap has new targets or mpv a new frame, mpv
  renders its picture (ground, art, lines, cards; the bars are cut round the cards) **without waiting for a frame's display time** (`render(…, noWait)`:
  with the wait, each 5 fps ground frame held the main thread ~60 ms), the bars are eased by the real time since the
  last draw (not while paused: the picture freezes as the graph's did), 41 quads in the theme's ACCENT and DIM (read
  each frame: a theme change shows at once; the ground follows with None's graph rebuild 500 ms later), and the top
  window is **swapped here** (KMSDRM: nothing else presents the panel while the style is up). Screenshots draw the same.
  Geometry is the graph's doubled: 16 units a bar, 12 wide, the line 4 tall; alone centred at 240, 218 each way at
  most; in the band (rows 280..480) centred at 380, 94 each way.

## The other six (same day)

The same tap, FFT and clock; each style its own bands (`vizStyleBands`: Peaks 64, Dots 32, Steps 20, Barcode 96, Rings
10, Horizon 20, all log spaced 40 Hz..14 kHz), told to the worker with `VizTap::setStyle` (a switch between native
styles keeps the tap and the blank graph: no sound reopen, one log line `viz-native: <style> now`). Peaks takes the
levels plain (its graph had no contrast push or bed cut, and its bars weren't eased); the others are spectrumRow's.
`VizStyleState` (bpcore) holds the levels and the held peaks; `vizStep` eases them, `vizShapes` makes each style's
shapes from its graph's geometry (the half-size picture doubled; `rowsUpTo` reproduces the graphs' "height of this
row" stills exactly):

- **Peaks:** 64 bars of 8 units (2 apart) from the foot, ACCENT; a 4-unit TEXT cap on each held peak, falling 102
  levels a second (the graph's lagfun 0.8679 at 30 fps); alone, the four faint white lines (10%) every 96 units.
- **Dots:** 32 x 17 dots alone (from 120 down: 75% of the height), 32 x 9 in the band; 20-unit cells, the dot 16 with
  its corners cut (three boxes); lit ACCENT from the foot, the rest BUTTON.
- **Steps:** 20 bands of 32 units: fill BUTTON, the held peak's step DIM (73.9 a second: lagfun 0.95 at 60), the
  outline ACCENT (each top 1 step alone, 2 in the band, and the rises/falls at a band's first 4 units); alone up to 75%.
- **Barcode:** 96 two-unit hairlines the region's height, from BUTTON to ACCENT by level (a mixed colour per line).
- **Rings:** ten rings (ACCENT) whose half-width steps through the graph's eight widths, a quiet band's guide
  (BUTTON), the centre dot (TEXT); real circles (a strip of quads, ~3 px a segment) where the graph's were remapped.
- **Horizon:** the 20 bands mirrored (bass in the middle) as a 40-column skyline (ACCENT, two thirds of the sky at
  most), a BUTTON sun disc behind it (its part under the line covered with BG), the 4-unit DIM line, and the striped
  reflection (DIM, odd 2-unit rows, the skyline squashed by REF/SKY, runs merged).

**The song's lines over the shapes** (Visualizer only, the gap left by the pilot): mpv draws the lines with its picture,
before the shapes, so the shapes keep off them. `Player::linesBox` (updateOverlay: the lines' measured width, 8 units
round) and mpv's cards (`cardsUp`) are cut out of the panel (`vizCutOut` on the whole 640x480), and the one vertex
buffer is drawn once per free box with a GL scissor (a few draws, no shape cut by hand; rings included). A loud bar
now stops at the box round the title. In Art + visualizer the lines are above the band, so only cards matter there.

Drawing is one buffer of coloured triangles a frame (positions and RGBA; blending on for Peaks' faint lines), no
textures. The largest is Dots alone: 1632 boxes, ~10k vertices, built on the main thread each frame.

Checked on the Mac (one run): the seven switched in turn with `set:visualizer=…`, then a seek forward and back, a pause,
Art + visualizer, and `flip:visualizer=steps|rings:6:300` (six switches 300 ms apart): no graph change, no stall, 47.4
frames a second at 45 fps. One contact sheet of the seven alone matched the graphs' looks.

## Log lines

| line | when |
|---|---|
| `viz-native: <style> for <file> (<mode>), N fps: a second decode, mono 32000 Hz, FFT 1024 (PFFFT), on mpv's audio-pts …` | the tap starts for a song |
| `viz-native: the top panel is the player's: …` | the first native frame after another picture |
| `viz-native: GL ready` (or `no glX` / `shader: …`) | the GL side, once a run |
| `viz-native: <file>: in 5.0 s 34.0 frames drawn a second, 28.6 spectra a second, audio-pts told 5.0 a second, N seeks; decoding aac 44100 Hz 2 ch` | once, 5 s in |
| `viz-native: off for <file> after N frames drawn, N spectra, N seeks (<why>)` | it stops: another style, another file, no visualizer shown, nothing plays |
| `viz-native: <file>: the second decode couldn't open it` + `… the graph style instead` | the fallback |

## Checked on the Mac

Build, core-test (`testVizNative`: bands, a sine's bar is its band's, silence is flat, the easing, the shapes, the
clock), and two scripted runs on generated tones: 45 fps → 46.9 frames drawn a second (the tap's and mpv's), 30 fps →
34; a forward and a backward seek (2 seeks, bars on), pause and play, Peaks and back (one graph change each way),
None and back (no graph change). One screenshot (Art + visualizer) matched the graph's look.

## What may differ on the handheld

The second decode's CPU (an MP3/AAC decode and a 32 kHz resample: a few percent of one A55 core expected; the worker
runs at nice 5); `audio-pts`'s update rate under the bundled mpv 0.40 (the 5 s line says it); GLES's attribute
locations and the swap under KMSDRM; the lines' box is measured with the Mac's fonts here and the bundle's Inter there (the
same Inter); Dots' ~10k vertices a frame on the A55 (the main thread's share: the log's frames a second says if it lags).

## Left out

Night city, Aurora and the scene styles still use graphs (now commented out of the list: The final ten). The tap ignores a chosen second audio track
(the blank graph plays `[aid1]` too).

## The final ten (same day)

The owner's list, all drawn by the player: **Peaks, Ribbon, Mirror bars, Pocket console, Horizon, Grid, Starfield,
Strings, Helix, Sparks** (`VIS_SHOWN`; the Display grid 5 x 2). Night city, Aurora, Dots, Steps, Barcode, Rings, Banana
dancer, Pixel sea, Jelly wobble, Washing machine and Tube man are commented out of `VIS_SHOWN` (their graphs and
native code kept, unreachable): a saved one reads as Peaks (readVisualizer's VIS_SHOWN check; Bubble tea, Long cat and
Radial, whose replacements were Dots and Steps, too). The six new ids (`ribbon`, `grid`, `starfield`, `strings`,
`helix`, `sparks`) are at the enum's end; their glyphs `G_VIS_RIBBON` … `G_VIS_SPARKS` (web/sprite.svg regenerated).

- **The worker's hint** (`VizHint`): the loudness (the 32 ms window's RMS, -42..-6 dBFS as 0..1) and a beat count
  (`VizBeat`: the 40..160 Hz energy past 1.5x its one-second mean, at most four a second, never in silence), handed
  over with the bands. Starfield and Helix ease the loudness, Sparks bursts on each beat.
- **New shapes:** besides boxes and rings, `VizRect` kind 2 is a quad with an alpha per pair of corners (a fade).
  Lines are mitred strips (`polyline`: the pieces meet on the joins, so a faint glow has no dots), curves the proposal
  page's midpoint quadratics (`smooth`), discs the rings' fan (8 sides when under 8 px). Still one buffer of coloured
  triangles a frame, no textures, no images (the owner's rule).
- **Motions** are the proposals' per-frame numbers at 30 fps, told over the real dt (`toward`: 1 - (1 - f)^(dt*30)).
  Grid keeps 17 rows in a ring, one every 0.1 s (a long gap gives one row, not a burst); Sparks a fixed pool of 400
  (`VizStyleState::sparks`, packed: a dead one swapped for the last; nothing allocated a frame); Starfield 160 stars;
  the chance (spawns, star places) is a xorshift in the state, the same run to run (tests).
- **Pocket console** natively (redesigned since: `2026-11-19-pocket-console-redesign.md`; what follows is the first
  native one): its graph's LCD as shapes (no texture, no picture): the screen box, 15 bars of 4-dot
  columns (the ink the lower two thirds, mid the top third, the caps and the floor), the 1-unit grid (80 + 60 lines)
  over everything, the four shades from `pocketInks()` read each frame. Its bands: 15, 40 Hz..8 kHz, contrast pushed
  but no bed cut (as its graph). Alone, its lines (pixel type, its ink) sit in a screen-coloured ASS box (the shapes
  keep off the lines' box, so mpv's picture shows there: this box keeps it the LCD's colour; the box 20% wider for the
  pixel font). In the band, as its graph over the art: no screen, the lit dots the bright pair, the grid cut in BG.
- **The band and the whole panel:** in Art + visualizer every style keeps below row 280 (a scissor box, so Grid's far
  ridges and Ribbon's glow stop at it) except **Sparks and Starfield** (the owner's word), which use the whole panel
  as alone, kept off the art's box and its lines' box (`groupArtBox`, `groupTextBox` from updateOverlay; the same
  scissor cut-outs as the lines alone): they pass behind them.
- **Network songs** (and a file the second decode can't read): Peaks' graph for every style (`Player::graphFor`, one log
  line `viz-native: <file>: <style> can't be drawn by the player here (…): Peaks' graph instead`); the lines then
  follow Peaks (`drawnStyle()`).

Checked on the Mac (one run, generated tones with a test-card cover): the ten switched in turn with `set:`, a seek
forward and back, Art + visualizer with Sparks, Starfield, Grid and Pocket console, and `flip:visualizer=ribbon|helix:6:300`:
"<style> now (the same tap, no graph change)" for every switch, 47.7 frames a second at 45 fps.

What may differ on the handheld: the main thread's vertex work. Heaviest: Grid (17 ridges, a BG occluder and a line
each: ~1.1k quads, ~7k vertices) and Sparks at a full pool (400 discs of 8 sides plus the swell: ~10k vertices), about
Dots' load; Ribbon's three glow strips blend over a third of the panel.

## No graph for songs (same day)

The owner: "let's not render anything with ffmpeg anymore". A song never has a `lavfi-complex` graph now (not the
blank one, not the art pass-through, not a fallback): mpv plays its sound (`aid` set by its id on load and on every
mode change, `applyAudioDisplay`, so a song is never left with no audio track: the old skip-to-next) and the player
draws the top panel:

- **Album art only:** the art as the same image overlay as Art + visualizer (decoded on a worker at the panel's
  pixels, corners rounded; `overlay-add`, no texture of ours), on mpv's `background-color` = the theme's BG, the lines as
  ASS; the placeholder as before. The file opens with `vid=no`.
- **None:** the title as ASS on BG.
- **Visualizer only / Art + visualizer:** the native styles over mpv's picture (the ground and the lines); no blank graph
  to wait for, so `syncNative` starts the tap as soon as the song plays. mpv's `audio-pts` is told more often without
  a picture (~15 a second here, was 5 under the blank graph's 5 fps).
- **Network songs:** the style drawn **idle (flat)**, no tap (one line: `viz-native: <file>: <style> drawn idle (flat): a
  network song …`); the same for a file the tap can't read. Reading bpnet:// in the worker (an AVIOContext over
  NetStreams' open/read/seek) was left out: it doubles a remote file's traffic and NetStreams' callbacks are mpv's.
  The one picture mpv still decodes: a network song's own art (`vid` = its art track, no graph, placed in the art's
  box by the video margins) in Album art only and Art + visualizer, since the overlay's decode reads local files only.
- Removed: `songGraph`, `graphFor` (and its lut warm-up and stills), the graph retry in `checkAudio`, the 500 ms wait
  before a mode's graph (`applyAudioDisplay` now runs at once), `groundColour`, `vizColours`, `blankGround`,
  `artPassGraph`. The graph builders in settings.cpp and `viz_*.cpp` stay (bpcore, tested), unreachable from the
  player. Videos never had a graph; the idle screen and photos use none.

Checked on the Mac: the four modes and a few styles on a song with art (48 kHz) and one without (44.1 kHz), a seek and
a pause: no `lavfi-complex` in either log (`graph -` in the audio lines), the art and the placeholder in both art modes.

## The five fun ones (same day)

After Sparks, in this order: **Bouncy keys, Rocket, Rain check, Sleepy moon, Dance line** (15 shown; Display 5 x 3;
ids `bouncy-keys`, `rocket`, `rain-check`, `sleepy-moon`, `dance-line`; glyphs `G_VIS_BOUNCY` … `G_VIS_DANCE`), from the
fun proposals page, shapes only (no images or textures): ellipses (`VizRect` kind 3, a fan), rounded boxes and the
rocket's body as fans, round-ended lines, arcs as polylines.

- **The beat's timing:** the state keeps the time since the last beat and its interval (eased, 0.3..1.2 s): Bouncy
  keys' hop is a parabola over one interval (a beat lands it and picks the next key within seven, the loudest plus a
  little chance; with no beat it waits on its key), Dance line sways half a turn a beat.
- **The worker's ~1 s loudness** (`VizHint::avg`, an exponential over the spectra's real gaps) wakes Sleepy moon past
  0.6 (-20 dBFS RMS) and sends it back to sleep under 0.5 (-24): the proposal's 0.31/0.24 were its made-up scale.
  Constants to tune on real music (`MOON_WAKE`, `MOON_SLEEP`).
- **Pools, fixed:** Rocket's 40 star streaks, Rain check's 260 drops, 80 splashes, 40 puddle rings (packed, swapped
  out), Bouncy keys' 9-point trail.
- **Whole panel:** Rain check and Sleepy moon (`vizFullPanel`), the art group centred as Album art only; Rain check's
  drops splash on the art's top edge (`VizStyleState::cover` = `Player::nativeArtBox`, told before each step), Sleepy
  moon is smaller at the top right (R 46 at 578,62: it may pass behind a long title's box). Bouncy keys, Rocket and
  Dance line keep the band; Rocket's stars streak over the whole panel there (`vizBehindArt`: no band cut, kept off
  the art's and lines' boxes instead), its rocket and smoke in the band, the art and lines in the band's layout.

Checked on the Mac: the five switched alone and with the art, a seek, a pause, `flip:visualizer=rocket|sleepy-moon:6:300`:
"now (the same tap, no graph change)" each time, 46.3 frames a second at 45 fps. What may differ on the handheld:
Rain check at a full pool (260 drops as quads, ~1.6k vertices, plus the rings' arcs) and Sleepy moon's 40 overlapping
cloud discs (blended fill at the foot); the moon may stay asleep on quiet masters (the line above).

## Performance, and the lines over the shapes (same day)

The owner on the handheld (Visualizer frame rate 60): Sleepy moon, Rocket and Sparks felt under 45; Grid started at 60
and slowed as its rows filled; even Mirror bars missed 60 in the band (49.7–52.4), Rocket alone 35.6–42.8 with the
worker at 47.8 spectra a second. The main thread used only 16% of a core under Sleepy moon: the frames were lost to
the GPU's fill and to the cadence, not to the CPU alone.

- **Cadence.** A frame was drawn per worker spectrum (`evViz`) or mpv frame, so the frame rate could never beat the
  worker's, and two clocks (the worker's timer, the panel's refresh) beat against each other. Now the top panel draws
  on its own grid of due times at the setting's rate (`TopScreen::nativeWaitMs` sets the loop's wait; a due time is
  the last one plus a frame, so a late wake doesn't shift the rest), taking the tap's newest levels as it draws; an
  mpv frame (the marquee's step, a card) goes into the next due frame instead of an extra one (paused: at once).
- **mpv's layer.** mpv rendered its whole picture (ground, art, lines) every frame under the shapes, and the shapes
  were kept off the lines' box: the visible "box" round the title. Now mpv's ground is clear (`background-color`
  `#00000000`, `Player::groundClear`, set by `updateOverlay` for a song with a native style shown) and mpv renders
  into a texture of ours (`layerFor`) **only when it has a new frame**; a frame is: clear to BG, the shapes, the layer
  composed over them (premultiplied: mpv's OSD blending keeps alpha). So the lines sit over the visualizer with no
  plate and no scissor hole; only mpv's cards are still kept clear. The lines (all modes over a visualizer, Pocket
  console's too) carry `\bord3\shad2` in BG (Pocket: its screen colour); the marquee's edge fades are the text's own
  alpha in four clipped strips (were BG strips over the picture). A song's picture while its style isn't up yet (or
  just after) goes through the same composition on BG (`groundFrame`), never on black. With no framebuffers the old
  way comes back (`nativeLayerFailed`: the ground opaque, the lines' and art's boxes kept clear).
- **Discs** had `r * 2.1` sides (a 230-px cloud: 256 thin triangles). Now the sides keep the edge within ~0.35 px of
  the circle (`circleSides`: 6 to 64; a spark 6, the moon ~45), small ones fanned from the rim, the cosines by a
  rotation step. Vertices are 12 bytes (colour as bytes), built into buffers kept from frame to frame, one upload.
- **Blending:** runs of opaque shapes draw with blending off (a large opaque shape starts a run; small ones ride
  along), so the GPU can drop covered fragments.
- **Sleepy moon:** 40 overlapping discs (the far 20 at 45% alpha) filled the foot several times over. Now two banks,
  each the upper outline of its discs at 96 columns, filled down, opaque (`VI_DIM_BG`: DIM laid over BG); the far
  bank only where it shows above the near one. The stars' crosses are two 3-unit bars on whole pixels (were 2-unit
  lines: lost on the handheld).
- **Rocket:** its smoke puffs opaque (`VI_DIM_BG` at the puff's faintness, the nearer over the farther) and its stars
  plain quads in the same ink (were 40 polylines blended, a vector each).
- **Grid:** 12 rows (was 17), the far half every other band, and each ridge's fill only down to the foot of the row
  behind it (everything behind lies above that line), not to its own: a quiet row fills nothing, and the fills no
  longer stack row on row as the history fills.

Measured on the Mac (Visualizer only, 60 fps, a 1280x960 drawable: four times the handheld's pixels), from the new
draw line's 5 s averages, before → after:

| style | shapes + vertices (ms) | vertices | px filled (blended) | mpv renders / 5 s |
|---|---|---|---|---|
| Sleepy moon | 4.09 → 0.31 | 28.7k → 2.0k | 4.16M (2.79M) → 0.55M (14k) | 304 → 49 |
| Sleepy moon, band | 4.22 → 0.40 | 26.2k → 2.1k | 3.53M (2.37M) → 0.43M (4k) | 321 → 89 |
| Rocket | 1.41 → 0.21 | 5.9k → 1.7k | 253k (213k) → 254k (0) | 301 → 3 |
| Sparks | 1.91 → 0.78 | 10.9k → 6.9k | 112k (12k) → 114k (13k) | 301 → 3 |
| Grid | 1.53 → 0.56 | 6.0k → 3.5k | 1.97M (85k) → 0.93M (56k) | 302 → 3 |
| Rain check | 1.99 → 1.25 | 7.3k → 5.7k | 190k → 192k | 302 → 3 |

All fifteen alone stay under 1.3 ms of our own work a frame on the Mac (the heaviest: Rain check, Sparks, Strings,
Dance line), 60.0–60.6 frames a second at 60 (a 120 Hz display here; the handheld's rate is in the GL ready line).

**The draw line** (once 5 s after a style starts or changes, then every 5 s up to four times: a style that slows as it
fills shows): `viz-native: draw <style> (alone|band) WxH: N frames a second; a frame X ms ours (shapes, GL[, GPU]), mpv
X ms (N renders), swap X ms (max), the longest gap X ms; N vertices (max), N draws, Nk px filled (Nk blended)`.
`BP_VIZ_GPU=1` adds the GPU's time (a glFinish before and after our pass: a debug run only, it stalls the pipeline).
The 5 s tap line now has the worker's own time a look, its longest, and how often it fell a frame behind.

What may differ on the handheld: the swap's wait (KMSDRM waits for the last page flip: the swap ms says how long);
whether the Mali composes the layer (one full-panel blended texture a frame, ~0.3 MP) as cheaply as it should; the
panel's refresh (the GL ready line). Not checked here: the arm64 bundle on the device.
