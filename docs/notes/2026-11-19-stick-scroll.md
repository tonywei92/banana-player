# 2026-11-19 The left stick scrolls the readers (issue #16)

## The problem as seen

The RG DS Plus has one stick (the left, also L3 when clicked). In the text viewer and the manga reader's Vertical
scroll the D-pad moves in steps; pushing the stick did nothing in either, although the hand expects a phone-like
smooth scroll: a small push crawls, a full push is fast.

## Cause

`main.cpp` read only the two trigger axes. `SDL_CONTROLLER_AXIS_LEFTY` was left to `idleSleep.input` and to the
button mapping, and no reader had any analog motion. `Strip` (manga) only knew a finger's drag, its fling and the
D-pad's animated screenfuls; `TextReader` only knew a finger's scroll/fling and the D-pad's glides.

The D-pad is not made from the stick anywhere in the app: `input.cpp` maps `SDL_CONTROLLER_BUTTON_DPAD_*` only, and no
code turns `LEFTY` into `Button::Up`/`Down`. So there was nothing to suppress in the readers; the stick is a new input
beside the D-pad.

## The fix

- **One pure curve.** `src/stickscroll.h/.cpp` (bpcore): `stickScrollSpeed(value, maxSpeed, deadZone)` reads SDL's
  LEFTY range, ignores the first 15 % (`STICK_DEAD_ZONE`) so a resting stick never drifts, and squares the push past it
  so the start is gentle and a full push is `maxSpeed`. The sign follows the axis: up (negative) scrolls up.
  `testStickScroll` in `tests/core_test.cpp` pins the dead zone, the sign, the curve and the full push.
- **Read once, per frame.** `main.cpp` keeps the last LEFTY from `SDL_CONTROLLERAXISMOTION` and hands it to
  `Ui::stickAxis()` each pass; `Ui` gives it only to the reader being read (the text viewer, the PDF/comic viewer, or
  the Manga reader). A reader turns the value into pixels in its own `frame()` with the time since the last frame
  (clamped to 50 ms), so the speed is the same at any frame rate, not per axis event. Releasing (0, or back inside
  the dead zone) stops it in the next frame. Main's loop already waits 1 ms while `reader.busy()`/`textReader.busy()`,
  and both now include the stick while it can still move.
- **The text viewer** (`TextReader`) gets a `Stick` command: it clears a fling/glide and scrolls by
  `speed * dt`, up to one panel height a second. **Book mode** has no scroll: up/down turn a spread, and the push is
  mapped by `mangaBookStep()` exactly as its D-pad is (one push, one spread).
- **The manga/comic reader** (`MangaReader`) gets a `Stick` command. In the Strip it calls `Strip::scrollBy`; the
  pages' owner (`frame`) does the timing. In a paged mode it calls the new `Paged::panBy()` only when the page shown
  overflows the view (`Paged::canPanV()`); a page that fits does not move, and the stick stays there for nothing else.
  In Book it does nothing (Book has no pan and no scroll; its spreads turn from `Ui::Manga::stickAxis()` with
  `mangaBookStep()`, one push a spread). Fullscreen's tall view pans/scrolls the same way.
- **Gates.** The stick is not sent while a reader's sheet/menu is up (Text options, the PDF options, the Manga Reading
  menu), while the pages grid shows, while a chapter is still opening, or over any non-reader page. Under the lid
  `stickY` is dropped, so a stick held as the lid closed cannot keep a reader moving with both panels dark. Nothing
  here lights a panel or touches `Ui::press`.

## Logging

The reader that owns the stick writes one line each way, so a handheld report shows the stick's real range and
where the dead zone ends:

```
text: stick scroll on (value -16000, -232 px/s)
text: stick scroll off (value 0)
reader: stick scroll on (value 21000, 305 px/s) in strip
reader: stick scroll off (value 0)
```

## Not changed

The D-pad's steps, triggers, L3 and every button mapping are as they were. The manga paged modes without a page
overflow (fit Page, a short page) are left alone: there is no panning to drive. The stick's horizontal axis
(LEFTX) is not read; the readers scroll up/down only, as asked.

## What the handheld must confirm

The real `LEFTY` range and rest value of the RG DS Plus's stick (whether it reaches ±32767, and how much drift it
has at rest). The 15 % dead zone is the usual console figure; `log.txt`'s `stick scroll on (value …)` lines from the
device will say if this unit needs more or less. The panel pixels per second are the drawable height at the moment
(`viewH`), so a 480-line panel and a fullscreen view get the same feel at the same physical push.
