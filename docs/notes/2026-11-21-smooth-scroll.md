# Smooth scrolling: Music's lists, and how another list moves onto it (2026-11-21)

The owner's goal: lists scroll as on a phone — the content follows the finger by the pixel, glides on after the lift
and slows by friction, gives a little past the ends, and does it at 60 frames a second on the RG DS (RK3568, SDL2's
GLES2 renderer on KMSDRM), where a bottom-panel frame took 30–55 ms. This step: one shared component, used by Music's
four lists only (the Albums grid, Artists, an album's tracks, an artist's songs). Every other list still scrolls by
whole rows (`lay::listScroll` / `lay::dragScroll` / `lay::rowAt`).

## Where a frame went

The "timing:" line now splits a bottom frame (`Ui::FrameParts`, `Ui::partsLine`): the ground (clear, backdrop), the
page's body and inside it a smooth list's rows, the shell (rail, header, hints), the rest over them (sheets, keyboard,
veil), the present, the text lines rendered afresh (a text cache miss is FreeType) and the rows drawn afresh. It is
printed for frames of 30 ms or more, as before, and for every frame with `BP_DRAW_TIMING=1`. Each glide adds one line
when the list comes to rest:

```
timing: list moved: 64 frames in 1043 ms (60 a second), 2.64 ms each (list 0.21), max 9.99, 0 late (over 25 ms apart
while fast); 12 rows drawn, 32 text misses
```

Mac (M-series, 2x window), Artists with 71 rows, a 280-unit flick: before, every redraw was a full frame of 1–3 ms
(body 0.2–0.5, shell 0.2–0.5, present 0.3–1.4) and the list jumped a row at a time with no glide. After, a glide is
~70 frames at 57–60 a second (with the composed frames, since dropped: 0.13–0.30 ms each; every frame whole: 2–3 ms at
1024x768); 6–10 rows drawn per fling (each row entering the view once), none at all when it is flicked back over
rows already drawn. The handheld's first log: 59 a second, ~13 ms a frame, composed and full alike.

## The pieces

| Piece | File | What |
|---|---|---|
| Motion | `src/scroller.h/.cpp` (bpcore, `lay::Scroller`) | Pixel offset (fractions kept). `press` / `move` / `release` (8-unit tap slop; the lift's speed from the last 100 ms of samples, capped at 6 units/ms; a press on a moving list catches it and is no tap). Glide: speed × exp(−t/325 ms), integrated, so a 5 ms and a 50 ms stepper land on the same pixel. Rubber band past an end (iOS's `(1 − 1/(x·0.55/d + 1))·d`), and a critically damped spring back (0.03/ms); a glide that crosses an end hands its speed to that spring at the crossing (the bounce). `reveal(top, h)`: the D-pad's cursor, a spring (0.025/ms) whose target adds up while a key is held. `rowAt` at a pixel offset. Tests: `testScroller`. |
| Text cache | `src/lrucache.h` | `Ui::cache` is an `LruCache` of 384 lines: full, it drops the line drawn longest ago (it used to destroy all 256 and the next frame rendered every line again). Tests: `testLruCache`. |
| Drawing | `src/ui_smoothlist.h/.cpp` (`Ui::SmoothList`) | Rows drawn into textures of their own ("slots": the rows touching the view plus 4) only when their key changes; placed at `view.y + i·rowH − offset` in device pixels, clipped to the view, with the premultiplied blend (a row is drawn onto clear pixels, so its edges carry multiplied colour). Without render targets every row is drawn where it is, by the unit. |
| Frames | `Ui::draw` (`ui_draw.cpp`) | Every frame is drawn whole: the page, the list's slots placed in it. The list's offset is not in the signature: `drawnList` follows it in device pixels, and a change in it draws a frame. (A kept-frame path — the last frame in a texture, the ground under the list copied out of it, scroll frames only the rows over that ground — was dropped: see "Fixed".) |
| Loop | `main.cpp`, `Ui::glideWaitMs` | While a list glides the loop comes round 16 ms after the last bottom frame began. The finger's time is the event's (`ui.pointerMs` → `ui.touchMs`), so the lift's speed doesn't depend on when the loop read the events. |

## Moving another list onto it (Files, Videos, Settings, a sheet...)

1. **The area owns one**: `SmoothList list{ui};` (include `ui_smoothlist.h`). Remove its int `scroll` for that list.
2. **One geometry function** used by draw, keys and touch (Music's `listGeom()`): the view Rect, the row pitch, the
   count, the cursor's row. Return it empty when the list doesn't show.
3. **`SmoothList* smoothList() override`**: `&list` while it shows, else null. Ui steps the glide, composes the scroll
   frames and frees the textures when the page leaves.
4. **draw()**: fill a `SmoothList::Spec` — `view`, `rowH`, `count`, `key(i)` and `draw(i, box)` — and call
   `list.draw(spec)` where the rows used to be drawn. Both lambdas outlive this draw (a frame that only moved calls them for a
   row coming into view), so **capture by value** (the snapshot's shared_ptr, the playing path, the focus and pressed
   index), never `p` or locals by reference. `key(i)` must hold everything the row shows that can change while the
   page's signature doesn't (a cover arriving: `ui.songArtState`, asked with the boxes the row's draw uses in its slot — at 1.6x a cover's device
   size depends on where its box starts, and the art cache keys by it; a like is in the signature and in the key);
   return `""` for a row that animates by itself (a focused title's marquee): it is drawn again on every full frame.
   The row draws at `box` (its slot's top left): anything it draws outside its box is cut, so give it room in the
   pitch (Music's grid rows: SP6 around the covers for the focus ring, the view widened by the same).
5. **pointer()**: on Down `caught = list.press(pt.y * Ui::H)` (by the fraction) and, if not caught, `list.rowAt(x, y)`
   for the row (`list.rowBox(r)` for cells inside it); on Move `if (list.move(fy)) touchMoved = true;`; on Up
   `list.lift()` and no tap when it moved or was caught. Put `touchMoved` in the signature (a drag clears the pressed
   look once).
6. **keys**: after the cursor moves, `list.reveal(cursor, view, rowH, count)` (a glide); a new list or a view
   changed, `list.top()`; coming back to a list at its cursor, `reveal(..., false)` (at once).
7. **signature()**: drop the scroll from it; keep the cursor, the pressed row, `touchMoved`.

A list inside a sheet (an overlay) gets the same: the slots, the glide and the pixel offset.

## Traps

- **Touch indices**: Music's `T_ARTIST + i` ranges were 100 wide, and the grid's lift opened its cell's index counted
  from the first row shown (the wrong album once scrolled). They are now 2^20 wide and index the whole list.
- **SDL's render targets**: switching targets resets the clip rect (texture to texture) and restores the screen's on
  the way back to null. The slots are filled before any is placed, so a frame switches targets once per changed row,
  and the clip is set after. The only targets are the slots, drawn into and then copied onto the window: never a
  target copied into another target.
- **Textures and the renderer**: `~Ui` releases the shown list's slots before the renderer goes; a theme change
  releases them (the rows were drawn in the old colours).
- **The art cache and slots**: a slot draws a cover once, so the cover is not "drawn" again while the row shows;
  `songArtState` (asked by the row's key every frame) counts as a draw, or the covers on screen are the cache's oldest.
- The **artist album strip** (one horizontal row of five) is not a smooth list; it still steps.

## Not verified

- **The handheld**: KMSDRM + GLES2 with render targets and the custom blend (the log says `smooth list: rows WxH px,
  premultiplied blend` or `plain blend` once), the 1.6x panel (row slots are drawn at x 0, so a row's inner x can round
  a pixel differently from a row drawn in place), and the memory:
  up to ~10 slots (a grid row 880x234: 0.8 MB) while Music shows, and up to 48 covers (147x147: 86 KB each).
- A finger on the real panel: the slop, the band's give, the 325 ms friction and the spring constants are the
  manga strip's and iOS's numbers, not tuned on the RG DS.
- With the backdrop (Match album art) behind Music: the ground keeps it, but no run drew it.
- The Linux (arm64 Docker) build: no Linux-only file changed.
- D-pad bursts: each cursor step is a full frame (the focus moved) and the top panel's preview pane is redrawn; on the
  Mac 20 of 126 frames of a `Down*30` came more than 25 ms apart while fast. The top panel's share is the likely cost.

## Fixed (2026-11-21): the list upside down, covers blinking (handheld only)

The owner on the RG DS: "sometimes it becomes upside down, and music thumbnails go visible and invisible"; the Mac
(Metal, 2x) never showed it.

- **Covers**: the art cache held 12 (`ART_KEEP`), Music's grid shows up to 15 covers with more in the spare slots, and a
  slot never draws its cover again, so the covers on screen were the least recently drawn and freed first. The row's
  key (cover there or not) flipped, the row was drawn with the placeholder, the cover was asked again, arrived, freed
  another: a loop. Reproduced on the Mac with covers in the media and `BP_BOTTOM_SIZE=1024x768` (the handheld's 1.6x):
  12 781 cover decodes in one run, every frame a full one at ~458 a second. Fixed: `songArtState` counts as a draw,
  `ART_KEEP` 48, the grid's key lays its covers in the slot's box (the draw's). After: 32 decodes (one per cover).
  (The Mac runs before had no covers in their media.)
- **Upside down**: not reproduced on the Mac (`SDL_RENDER_DRIVER=opengl`; no GLES2 there). The suspect was the only
  target-to-target copy (the frame texture's pixels under the list copied into `ground`, while the frame texture was
  the target) and the composed frames built on it. That whole path is gone: every frame is drawn whole, the slots
  copied onto the window. The handheld's log said full frames take ~13 ms, inside the 16. If it still flips, the
  slots are the remaining render targets (and the Book bar's turned texture): the next log to add is around them.

## Reproduce

Generated media (tiny silent FLACs with tags, and a `testsrc` frame as attached cover for the art cache): 70 artists of one 3-song album, one 40-song album, in `<lib>/Music`.
`BP_DRAW_TIMING=1 BP_BROWSE=<lib> banana-player --state-dir D --press "tap:39:230,R,info,...,drag:350:420:350:140,
info,info,info,drag:350:140:350:420,info,info,info"` with eight `info` steps where the dots are (the rail's Music
cell, Artists, two flicks; the first 8 s pass first — the panels are redrawn whole every 100 ms after start). Then
`grep "list moved" log`; `grep -c 'song art: [0-9]' log` counts the covers decoded (one each, not thousands). Run
with `BP_BOTTOM_SIZE=1024x768 SDL_RENDER_DRIVER=opengl` for the handheld's scale and a GL renderer.
