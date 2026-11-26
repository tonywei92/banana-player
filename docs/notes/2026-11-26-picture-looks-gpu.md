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
- **Scanlines (CRT beam).** Lines `unit` = panel height / 240 pixels tall (about 240 lines on either panel; 4 px on a
  Retina Mac). Each pixel takes its line's colour, softened across by two side taps at ±0.7 px (3 taps). Its weight is a
  Gaussian of its distance from the line's centre row, `sigma` from 0.17 line (dark) to 0.30 (bright, by the square root
  of the brightest channel): highlights bloom into the gap, dark parts show deep lines. Strength sets the depth (45–100%
  of the way from flat to the Gaussian) and an aperture grille on output columns (R, G, B; the other two channels at
  1 − 0.3·k). Gain ≤ 1.8. Cost: 3 taps, 2 `pow`, 1 `exp` a pixel.
- **LCD grid.** Cells of `max(3, round(unit·(1.5 + 1.5·k)))` px (3–6 on the RG DS, 5–9 on the Plus). One tap at the
  cell's centre (the picture snapped to the grid); three vertical stripes over the lit width, each passing its own
  channel whole and the others at 0.30 − 0.15·k; a 1-px gap row under each cell, and a 1-px gap column from 4 px up, at
  0.25 − 0.15·k. Gain ≤ 2.6. Cost: 1 tap, 2 `pow` a pixel.
- If a row lands one off on Panfrost (an FBO flipped against `HOOKED_pos`), the centre sample comes from a row within
  the same few pixels: a blur of a pixel, not a misplaced pattern. Worth a look on the handheld.
