# Match album art: the backdrop's cost, the top panel's modes, its strength (2026-11-20)

## 1. Marquee and slider frames were slow with the backdrop on

**Why.** The player page redraws whole on every `signature()` change: a Title marquee step (about 27 a second) or a
held slider's frame. Each redraw drew the backdrop as `Kit::backdrop` did: the 160 x 120 picture stretched, then the
veil **row by row**, 480 `SDL_RenderFillRect`s with blending (and the same again under a cross-fade). On the handheld's
GLES2 renderer that is hundreds of commands a frame on the main thread, which the top panel's visualizer waits behind.

**Fix.** `Kit::backdropPage` composes the picture with the theme's veil once (`artbg::composePage`, 160 x 480 RGBA,
now interpolating between the picture's rows so the stretch has no 4-row steps) into one texture; `Kit::backdrop`
draws it as one copy (no blending at full). Ui keeps one page per slot (`bgTex`, `bgFadeTex`), made on first draw and
again only for a new theme (`bgTexTheme`). The marquee's fade strips over the backdrop now copy the backdrop's own
pixels (`Ui::fadeStrip`, `Kit::backdropStrip`) instead of the plain ground's colour, which showed as a dark bar.

**Measured** (Mac, 1280 x 960 panel, Peaks + marquee, `timing: player page …` line, 5 s windows, ~28 redraws/s):

| renderer | before: ground / whole redraw | after: ground / whole redraw |
|---|---|---|
| OpenGL (the handheld's kind) | 0.80 ms / 2.93 ms | 0.04 ms / 1.91 ms |
| software (a slow GPU's stand-in) | 4.29 ms / 6.10 ms | 0.87 ms / 2.69 ms |

Draw calls for the ground: 482 → 1. The handheld's own number: the `timing: player page N redraws in 5 s with the
backdrop` line (three per song at most) in log.txt; the visualizer's own fps: the `viz-native: draw` line.

## 2. The top panel's music modes showed no backdrop

**Why.** The plumbing was all there (Ui's answer → `TopScreen::composeBackdrop` → its own `PageRenderer` pages →
`drawBackdropPages` under mpv's layer in `nativeFrame`), but `drawBackdropPages` returned `PageRenderer::draw`'s
answer, which means "pages still uploading", not "drawn". With nothing pending it said false, and `nativeFrame` cleared
the panel to the theme's BG over the backdrop it had just drawn: every mode (Album art only, Art + visualizer,
Visualizer only) showed the plain ground. Second, a still panel (Album art only, or paused) was never drawn again when
the pages arrived: nothing asked for a frame.

**Fix.** `drawBackdropPages` returns true once it has drawn (its pages go up whole in the frame, `setUploadAll`);
`composeBackdrop` / `dropBackdropPages` say when the pages changed and the top panel's pass sets `renderPending`.
Cost: the pages' one quad (two during the 0.5 s cross-fade) under mpv's layer, which the native styles composite anyway.

**Not covered:** a graph visualizer (an mpv `lavfi-complex` style, or the network fallback) draws its own opaque
ground over the whole panel; the backdrop can only show under the native styles and the art modes.
