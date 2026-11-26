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
