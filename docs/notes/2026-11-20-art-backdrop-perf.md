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

## 3. The art a step more visible

`artbg::VEIL_BASE` (one constant, both versions): 58 / 52 / 74 / 86 / 92 in place of 66 / 60 / 80 / 90 / 94. The
contrast protection is unchanged, so a stop only goes lower where the theme's text still holds 4.5:1. With the
generated orange test art: dark theme 66 66 90 90 94 → 60 60 90 90 92 (the head and title area show the art most;
under the bar DIM_ON's protection keeps 90), Light 74 74 80 90 94 → 74 74 74 86 92. The `backdrop: the player's page
composed` log line now prints the five veils.

## 4. The marquee's edges: the text's own alpha, not a coloured band

The Title marquee faded its ends by laying four strips of the ground's colour (BG, or BUTTON in a row) over the text:
on anything but that plain colour (the backdrop, a card, a light theme's paper) it showed as a dark band. Now
`drawTextAt` takes an `EdgeFade` and `Ui::drawFaded` draws the same cached text texture in vertical slices — whole
between the ramps, 8 slices a 16-unit ramp at a falling alpha mod (times the marquee's own fade-in alpha). No texture
is made per frame: 17 copies of one texture at most. `drawMarquee` lost its `ground` argument (five callers).

## 5. One picture down both panels

The owner: both panels showed the same backdrop. Now they are one tall canvas (640 x 960, 2:3): `composePage` takes a
`Half` — Upper for the top panel's pages (TopScreen::composeBackdrop), Lower for the bottom's (Ui::drawBackdrop) —
and samples the picture's middle 80 columns over twice the panel's height, so the top shows the art's upper half and
the bottom its lower half, meeting at the seam (core_test: a row ramp meets itself within 1). Still one blur and one
cache file per art; each panel composes its half once per art and theme. The veil: the bottom panel keeps its five
stops; the top panel's is the first stop, flat. The protection's luminance bands moved to the canvas (top band:
picture rows 0..80, `BAND_ROW`; only the middle columns), so `CACHE_VERSION` is 3 and old `.bg` files are made again.
The gallery's page 20 still draws a Whole (4:3) page.

## 6. The browser's pages on the backdrop too

While a song plays with the switch on, `Ui::renderPage` draws the same Lower page (`drawBackdrop`: one copy, the
same cache, the same 0.5 s cross-fade) right after its clear, on every page but the player's own (which draws it in
PlayerPage::draw) and the readers (DocText, DocPages, MangaRead) — `Ui::browserBackdrop`. `groundIsBackdrop` tells
the shell's header to leave its BG fill out; the rail and the hint bar keep their own surfaces, as rows and cards
do. `Ui::signature` takes the answer's hash and the fade's step on those pages. No extra veil step was needed: below
the bottom panel's y 168 the Lower veil is already 84%+ (the list's DIM text there holds), and the header area is the
protected TEXT band. A bridged old body (select / add modes) still clears its box to BG.

## 7. Grid and Horizon over the backdrop (the native styles' VI_BG ink)

Grid hides the rows behind each ridge with quads in VI_BG, and Horizon covers the sun under its line with a
full-width VI_BG box to the foot: over the backdrop these were solid theme-BG patches and a black band at the bottom.
VI_BG means "the ground", so when the backdrop is the ground (`groundArt` in `nativeFrame`) those shapes are now
painted with the backdrop itself: the top panel keeps the newer page's bytes (`bgGroundPage`, a serial), topviz
uploads them once into its own texture (`groundTexture`), and a second tiny program (`groundProg`: the page sampled
at the vertex's panel position, the shape's alpha) draws the VI_BG runs (`Run::ground`, `useGroundProgram`). No extra
pass: the same vertices, a program switch per ground run. Every other VI_BG use (Rocket's window, the faces' eyes)
now cuts to the art as well, which is what "the ground" meant. No other style paints a fixed band (checked
`box(out, 0, …, 640, 480, …)`). During the 0.5 s cross-fade the ground shapes show the newer page.

## 8. The browser's pages: a lighter veil, a glass hint bar

The browser pages drew the player's page (veil 84%+ below y 168: the art barely showed). They now have their own
page per slot (`Ui::bgTex[BG_LIST]`, composed once like the player's): `artbg::backdropForList` starts from
`LIST_VEIL_BASE` 44/44/50/54/58 and raises a stop only until TEXT holds 4.5:1 and DIM_ON 3:1 over the picture's
95th (dark) / 5th (light) percentile — rows, cards and the rail bring their own surfaces. The test art: 44 44 69 69
69 (the player's 58 52 84 86 92). The hint bar over the backdrop is HINT_BAR at 55% (`Kit::HINT_GLASS`); its keys keep
their own caps. Still one copy a redraw.

## 9. Stop fades the backdrop out

Stop (or the switch off, a video, High contrast) used to drop the backdrop at once. `Ui::leaveBackdrop` now moves the
drawn slot and its pages into `bgFade` and stamps `bgLeaveAt`; `drawBackdrop` draws that page over the plain ground at
an alpha falling to nothing over `BG_FADE_MS` (0.5 s), on the player page and the browser's (`browserBackdrop` holds
while it runs); `backdropTick` drops it at the end; a new song's answer meanwhile cancels the leave and fades in.
The signatures take the fade's step (`|bgout`), `animating()` keeps the loop drawing. The top panel still drops its
pages at once: on stop it goes back to mpv's idle picture, whose ground is opaque.
