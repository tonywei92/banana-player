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
