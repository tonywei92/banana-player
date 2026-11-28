# A left stick resting past its dead zone stopped every list's glide (2026-11-28)

Owner's report on the RG DS Plus: no flick glide on any list, and in Manga › a source's catalog D-pad Down moved and
then went back up. A restart of the app fixed it.

## Cause

- The handheld's left stick rests off centre: `EVIOCGABS` on `retrogame_joypad` (event6) reads ABS_Y **-277 of ±1800**,
  steady (flat 32), i.e. SDL LEFTY ≈ -5043, **15.4%** up. `STICK_DEAD_ZONE` was 0.15.
- `Ui::draw` scrolls the shown smooth list by the stick: while `stickScrollSpeed` is non-zero it calls
  `Scroller::jump(offset + speed·dt)` every frame. Just past the dead zone the speed is ~0, but `jump()` sets the mode
  to Still: every glide after a lift and every cursor glide (`reveal`) was stopped on its first frame.
- The bounce: Down moves the cursor below the view, `reveal` starts a glide, the next frame's `jump` kills it, and
  `FollowList::follow`'s "motion over" rule then puts the cursor back among the rows shown: one row up.
- Why a restart fixes it: SDL does not send an axis event for a stick that sits still at start (no event, `stickY`
  stays 0). Once the stick is pushed and let go, its last event is the resting -5043, and the lists crawl from then
  on, until the next start.

The touch path was not at fault: the Manga lists and most areas just don't log their drags ("touch: list drag" is
only Ui::pointer's own lists).

## Fix

- `STICK_DEAD_ZONE` 0.25 (`src/stickscroll.h`; test pins the measured rest).
- `Ui::draw`: a stick push moving the list under half a unit a frame doesn't `jump` a list that is gliding.
- Log `input: left stick past its dead zone / back at rest (LEFTY n)` as it changes (`Ui::stickAxis`).
- Self-healing for the other suspect: a touch-down with the shown list still `held()` (a lost lift) lets it go and
  logs `touch: a list's finger had no lift; let go`.

To check a handheld's stick: `python3` with `fcntl.ioctl(f, EVIOCGABS(1), buf)` on /dev/input/event6.
