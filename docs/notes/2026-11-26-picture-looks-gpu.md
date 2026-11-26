# Picture looks on the GPU (2026-11-26)

**Why.** The looks were FFmpeg filters (`vf=@look:lavfi=[scale to Fit, then unsharp / drawgrid / lutyuv]`). On the
handheld's four A55s that was 3–13 ms of CPU a frame, taken from the software decoder: the owner saw video lag. mpv
already draws on the GPU (`vo=libmpv`, an OpenGL render context), so each look is now an mpv user shader
(`glsl-shaders`) and the CPU does nothing per frame. The per-frame scale went with the filters (nothing else used it;
a photo's own `vf` scale stays).

**How** (`src/picturelook.*`, `Player::lookShaderNow/applyLook/lookRejected`):
- One pass each, hooked at `OUTPUT`: the picture as drawn on the panel, so the cost is bounded by the panel's pixels
  (≤ 1024×768) whatever the film, and Scanlines / LCD grid count `gl_FragCoord` rows: lines land on panel pixels
  (unit = `lookLineUnit(topH)`: 2 on 640×480, 3 on 1024×768; 4 on a Retina Mac's 960-pixel window, the same 2 points).
- Taps: Sharpen 5 (centre + four between texels = a 3×3 binomial blur, luma unsharp), Soften 5 (centre + a pinwheel of
  four 2×2 means, ~5×5), Scanlines / LCD grid / Retro 1. Hooked textures are filtered linearly (mpv's `ra_tex_resize`).
- **No `//!PARAM`**: libmpv's renderer is vo_gpu's `gl_video` (bundled mpv v0.40.0); only vo_gpu_next reads PARAM and
  `glsl-shader-opts`, gl_video logs "Unrecognized command". So strength (and unit) are written into the text: one file
  per (look, strength, unit) in `<state>/cache/looks/`, written the first time a run asks for it (≤ 50 tiny files).
- Sent as the whole `glsl-shaders` list (our one file or ""), async through `prop()`: the same as change-list
  remove/append, and never two after a quick toggle. Hold Y sends "" and the file back.
- `log.txt`: `look: shader applied in N ms (file)` per change (the property's answer; the compile itself happens at the
  next draw). A parse error or a GLSL compile log at error level while a look is on: `look: mpv refused the shader …:
  Off instead`, once per file; the look is Off for the run, with a short panel note.

**Cost seen** (Mac, 720p testsrc): 0 dropped frames with or without a look; the first shader change 165 ms to answer
(gl_video re-inits its shaders), later ones 2–45 ms. `vo-delayed-frame-count` reads empty under libmpv.

**May differ on Panfrost (Mali-G52, GLES 3):** mpv's header asks `precision highp float` when the fragment stage has
it (GLES 3 does), so no `mediump` rounding of `gl_FragCoord` rows or texel offsets; were it mediump, a row near 768
could floor to its neighbour. `textureGather`/`textureGatherOffset` (GLES 3.1) and compute are not used. A first
compile on Mesa may be tens of ms in `mpv_render_context_render`, on the main thread: one hitch per new (look, strength).

## Scanlines and LCD grid made realistic (same day)

Written for this app (no community shader copied; most are GPL, this project is MIT). Both: one pass, in linear light
(`pow(c, 2.2)` in, `1/2.2` out), the cell or line's colour sampled at its centre (an offset from `gl_FragCoord` to the
centre, times `HOOKED_pt`), a gain worked out on the CPU so a mid grey keeps its brightness (written into the text).
- **Scanlines**: replaced the same week by crt-lottes (below).
- **LCD grid.** Cells of `max(3, round(unit·(1.5 + 1.5·k)))` px (3–6 on the RG DS, 5–9 on the Plus). One tap at the
  cell's centre (the picture snapped to the grid); three vertical stripes over the lit width, each passing its own
  channel whole and the others at 0.30 − 0.15·k; a 1-px gap row under each cell, and a 1-px gap column from 4 px up, at
  0.25 − 0.15·k. Gain ≤ 2.6. Cost: 1 tap, 2 `pow` a pixel.
- If a row lands one off on Panfrost (an FBO flipped against `HOOKED_pos`), the centre sample comes from a row within
  the same few pixels: a blur of a pixel, not a misplaced pattern. Worth a look on the handheld.

## Scanlines invisible on the Plus (1024x768), and crt-lottes (2026-11-26)

**Root cause, by numbers** (`BP_TOP_SIZE=1024x768`, a black-to-white ramp, pixel rows of the top shot): the pitch is
3 (`lookLineUnit(768)` = round(3.2)) and the beam's centre sat on a row's middle, so the other two rows were a third
of a line off on either side and **no row fell in the gap**: two dim rows, one bright, a ripple of 13% (highlights,
after the gain clipped them) to 36% (shadows), which at 366 ppi reads as no lines. On 640x480 (pitch 2) the same code
gave 28–47%. Not the cause: `target_size`/the video rect (the pattern counts panel rows), a flip (rows were regular).

**Fix:** the line's centre at `pitch/2 − 0.5` px, so each line's last row is half a line away and is the gap (pitch 3:
bright, bright, dark). Same pass now ported from **crt-lottes** ("PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER by
Timothy Lottes", libretro glsl-shaders `crt/shaders/crt-lottes.glsl`; the header says public domain, checked before
use; the notice is kept in the shader text and the README credits it). What was kept: emulated pixels `pitch` px
square (~240 lines, integer pitch), sampled at their centres in linear light; the three nearest lines, each a
horizontal Gaussian (`hardPix` −3) of three emulated pixels, weighted by the scan Gaussian `exp2(hardScan·d²)`; the
aperture-grille mask (his type 2) on output columns. Changes: warp/curvature off, no bloom, `Horz3` for the centre
line too (9 taps, not 11), `pow 2.2` gamma, our gain. Strength k: `hardScan` −(6 + 10k) (his −8 at k≈0.2), mask
dark/light 1 ∓ 0.5k (his 0.5/1.5 at k = 1), gain ≤ 2 for a mid grey.

**Dips measured** (ramp, sRGB row min vs max): 640x480 at 10/60/100% ≈ 22/46/–%; 1024x768 ≈ 18/41/55%
(was 13–36% at 60%). LCD grid at 1024x768: gap rows 24–34% every 7 px: visible, left as it was.
Cost: 9 taps, 2 `pow`, 6 `exp2` a pixel, ~zfast's order; Panfrost compile time unmeasured. `log.txt` now has
`top: WxH pixels`, so the panel's size behind a look is pinned.

## LCD grid replaced by S-Video (2026-11-26)

The owner found the LCD grid ugly on the handheld and asked for RetroArch's "crt ntsc svideo". Licence first: libretro's
`ntsc/shaders/ntsc-pass1-svideo-3phase.glsl` / `ntsc-pass2-3phase.glsl` (Themaister, from RetroArch; also the slang
`maister/` set) carry **no licence header at all**: no public-domain or permissive grant, and they come from GPLv3
RetroArch. So nothing of them was copied or translated; only the well-known technique is used, written for this app.

**S-Video** (`svideoBody`), one OUTPUT pass, 7 taps, in the picture's gamma (as the signal is): YIQ (FCC matrices);
luma a 3-tap soften 0.6 px apart, mixed in by 0.25 + 0.5k; I/Q from a 1-4-6-4-1 blur over 5 taps one virtual pixel
apart, the virtual width 640 (10%) to 320 (100%) across the picture: the colour bleed; I/Q turned 3 degrees and 5%
richer (a set's warm tint); back to RGB; then in linear light a light crt-lottes scan profile laid over the pixel
(`scanProfile`: the three nearest lines' Gaussians at this row, hardScan −(2 + 4k), no line resampling, credited in
the shader). No carrier and no frame counter: no dot crawl, no rainbow (composite's, not S-Video's).
Measured at 1024x768 on testsrc's cyan|magenta edge, red channel 10–90% width: Off 1 px, 40% 4 px, 100% 7 px.
A saved `lcd-grid` (settings.conf or resume.tsv) reads as S-Video (`pictureLookFromId`, `parseSettings`).
