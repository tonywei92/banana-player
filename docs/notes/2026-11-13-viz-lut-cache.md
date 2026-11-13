# Visualizer setup: lut and lut2 lookups, and the frame-rate setting (2026-11-13)

## The problem

A style's graph took long to come up on the handheld: Circle and Peaks ~0.05-0.1 s, most of the newer styles
0.3-0.75 s, Banana dancer ~1.6 s, Long cat ~2.9 s. Every change of style, and every song start, paid it.

Nearly all of it is configure time in `lut2` and `lut`. lut2 works out its expression for all 256 x 256 (x, y)
pairs when the graph is built; **lut does 65536 values too**, even on an 8-bit picture (vf_lut.c loops over its
whole `uint16_t lut[256 * 256]`), so a 700-character palette on a `lut` costs as much as a lut2. A style has
10-28 of them, some long: palettes as `if(eq(x,k),...)` chains, sprites, Long cat's clouds and tile. On the Mac the
expressions alone were ~350 ms of Long cat's 0.42 s to its first frame (ffmpeg's own start is ~0.04 s).

## What was done

`src/vizcache.{h,cpp}` (bpcore, libavutil's `av_expr`):

- A style marks the filters whose inputs are 8 bits: `bplut2=` for `lut2=`, `bplut=` for `lut=`. Not the exp/log
  `lut` pair around each `lagfun` (16-bit). Every marker becomes the plain filter again (`vizPlainLuts`).
- For each marked expression, its table is worked out once exactly as the filter does (lut2: y outer, x inner, one
  evaluator so `st()`/`ld()` carry over, `(uint16_t)` then the low byte; lut: `av_clip((int)r, 0, 255)`), then
  turned into a **lookup**: an expression that finds the same value by halves. A line of values (a lut's, or a
  lut2 column or row) is cut into pieces, each a number or the variable plus a constant (`y`, `y+12`); lines with
  the same pieces side by side are one; the value is found with a few `if(lt(v,k),...)`. A line too smooth for
  pieces (more than 24) keeps the original expression there. Both orientations are tried; the lookup is kept only
  if its table is **identical** to the expression's and it evaluates at least 1.5x quicker. It is cached as
  `<maps>/lut/<FNV-1a of version + key>.txt` (the lookup, or `=` for "keep the expression"), so a changed style
  makes new files by itself; `vizPruneLuts` removes the ones no shown style reads.
- `Player::graphFor` reads the lookups for the graph it builds; one not made yet leaves that filter's expression as
  it was this time, and a detached thread (files only, nice 10 on Linux) makes it, with every other shown style's
  the first time a run (~5 s on the Mac, all styles). The next build uses them.

The filter, its pictures and their sizes don't change, only the expression text: **a frame costs what it did and is
the same frame**.

### Verified

- Every shown style, alone and over the art, 120 frames rendered to yuv420p by the ffmpeg CLI with the plain graph
  and the cached one: byte-identical md5s for all 40, at 60 and at 45 fps, on FFmpeg 9.0.2 (Mac) **and on FFmpeg
  7.1.1 aarch64 Linux** (the bundle's version, built in the bp-builder image): 160/160.
- `core-test` `testVizCache`: lookups give the same table as their expressions, the hash naming, a second call
  reusing the files, `=` for what can't be a table (w/h, NaN, out of range), pruning.

### Numbers (Mac, ffmpeg CLI to the first frame, best of 3; cold = no lookups yet = before)

| style | cold | warm |  | style | cold | warm |
|---|---|---|---|---|---|---|
| Long cat | 0.423 / 0.232 | 0.146 / 0.127 | | Fishbowl | 0.174 / 0.153 | 0.139 / 0.126 |
| Banana dancer | 0.283 / 0.276 | 0.117 / 0.120 | | Tube man | 0.156 / 0.158 | 0.127 / 0.129 |
| Vibing cat | 0.163 / 0.161 | 0.108 / 0.113 | | Sound garden | 0.133 / 0.148 | 0.101 / 0.110 |
| Washing machine | 0.138 / 0.137 | 0.100 / 0.100 | | Campfire | 0.113 / 0.114 | 0.101 / 0.102 |
| Popcorn machine | 0.133 / 0.122 | 0.108 / 0.103 | | Jelly wobble | 0.105 / 0.106 | 0.086 / 0.088 |

(alone / over the art; ffmpeg's own start ~0.04 s is in every number). The expressions' own cost, all keys: Long
cat 349 -> 72 ms, Banana dancer 215 -> 51 ms, all styles together 2.19 -> 1.14 s. Aurora, Jellyfish, Music box,
Pixel sea gain little: their costly expressions are smooth (sines, blends toward a colour), which no lookup by
halves beats in av_expr. In the player (Mac): Long cat's song start 0.80 s cold -> 0.26 s warm.

## Ruled out: a remap of a cached table picture

Built and verified first (identical md5s on both FFmpeg versions): each marked filter became `remap` reading a
256 x 256 PGM of the table, its coordinates the inputs as 16-bit maps (`lut2=c0=x:d=16`), the table copied each frame
over the first input's pixel (`pad` + `blend` normal at opacity 0, so the frames, rate and properties are the first
input's). Setup fell to the floor (Long cat 0.42 -> 0.09 s), but per frame, single-threaded as the handheld runs
graphs, it roughly **doubled** a graph's cost (Fishbowl 0.29 -> 0.65 ms a frame, Peaks Banana over the art 0.37 ->
0.84), mostly the 64 KB table copy and four more filters per site, and far more at 640x200. Lessons worth keeping:

- remap's source drives it and stops it (sync 2, EXT_STOP): a one-frame table can't be the source; Circle's
  ended inputs are its maps.
- A `movie=` source pins its colour range in FFmpeg 9's negotiation (unknown); added to a graph it turned the art's
  range conversion on (pc -> unknown) elsewhere in the graph. A plain `scale` after the movie decouples it (no
  value changes for gray).
- `vstack` copies no frame properties and makes the rate VFR when its inputs' rates differ.
- A lut2 whose first input is a still (Popcorn's pile, lid) runs on its second input's frames after the still ends.
- Slice threading: with mpv's default threads, many tiny filters cost more in thread wake-ups than in work.

## The frame-rate setting (Settings > Music, `visualizer-fps`, 30/45/60, default 45)

`vizAtFps` rewrites a 60-a-second style's finished graph (one with `rate=60`; Peaks and Circle stay at 30):
`rate=60`/`r=60` to the rate, each `lagfun=decay=d` to `d^(60/fps)` (it multiplies once a frame), each
`tmix=frames=k` to `k*fps/60`, and `n` in crop's and overlay's expressions to `floor(n*60/fps)` (the frame the
style would be on at 60 at that moment), so every step, loop and sway takes the same time. Checked: all 40 graphs
run at 30 and 45; rendered 4 s at 30, 45 and 60 and compared frame k at 30/45 with the 60's frame at the same
moment: mean differences 0.1-1.8 (at the same moment) against 1.6-15 (same frame number) for Vibing cat, Washing
machine, Jellyfish, Long cat, Tube man, Banana dancer, Pixel sea, Pocket console. Popcorn machine doesn't line up
either way: its kernels and lid are gated by thresholds on held levels, which sample differently at 30 than at
60. Not verified on the handheld.

## Left

- The handheld's own numbers (ratio to the Mac ~6-7x on the expressions): expected Long cat ~2.9 -> ~1 s, Banana
  dancer ~1.6 -> ~0.7 s, the rest ~0.1-0.2 s quicker.
- What remains per style: smooth expressions (vizContrast's `clip(150+1.8*(x-150)+6*(x-y),0,252)` alone is ~5 ms
  on the Mac in every style), the 16-bit exp/log luts (~8 ms), and filter setup. Merging vizContrast and the exp lut
  into one `lut2 ... d=16` would save one 65536-value lut and a conversion per frame (frames to be checked equal).
