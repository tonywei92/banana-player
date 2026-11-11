# The manga Strip prototype on the handheld (2026-11-10)

The design (docs/design/manga.md §10) asked for the strip's smoothness to be measured on the RK3568 before the reader
is built on it. Result: it is smooth. The owner's words: "smooth as hell".

## What ran

- `--manga FOLDER|CBZ` (or a `manga-proto` file in the state folder, for a launch from the Ports shelf): `reader_proto.cpp`.
  Its own loop, with no player and no bottom-panel controls.
- **`Strip`** (bpcore): the layout, the drag, a fling with a 325 ms time constant, and the D-pad's eased screenful.
- **`PageRenderer`:** GLES 2 textures in the top window's context, uploaded 512 rows per frame.
- **Decoding:** two `decodePage` threads (FFmpeg + libswscale), fed by `Strip::wanted(3, 1)`.
- **Test chapter:** a CBZ of 32 generated pages, 30 at 1400 × 2000 JPEG and 2 tall pages at 800 × 6000.

## Numbers (log.txt, RG DS Plus, 1024 × 768 top panel)

| Measure | Handheld | The design's target |
|---|---|---|
| Frame rate while dragging, flinging and stepping | 58–61 fps; slowest frame 17–18 ms | 60 fps |
| Decoding a 1400 × 2000 JPEG to 1024 × 1463 | 78–105 ms | < 300 ms |
| Textures resident | 28–51 MB | 96 MB budget |

On a computer, a whole page uploaded in one frame (one 2048-row tile) held that frame for about 150 ms. Since then,
uploads go 512 rows per frame (`TILE_H`).

When measuring, PNG screenshots (`BP_SHOTS`) cost about 170 ms each. Turn them off; `BP_MANGA_DEMO` runs without
them by default.

## Found along the way

- **`build-bundle.sh` packaged the last good binary when a compile failed:** a `grep ... || true` hid the error. It now
  stops. The trigger was `pagerender.h` without `<cstddef>`: GCC needs it for `size_t`, and clang doesn't.
- **Pages swing in and out:** going back and forth over the keep window (visible −2 to +4) decodes the same pages
  again, at about 80 ms each. That's harmless at this speed. A small cache of decoded buffers would avoid the repeats.
- **Fling tuning:** the fling feel (τ 325 ms, a cap of 6000 px/s, a 100 ms velocity window) was accepted as is. The
  design's "0.998 per ms" corresponds to τ ≈ 500 ms, so it is a starting point if the feel needs tuning.
