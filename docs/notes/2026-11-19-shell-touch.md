# The shell's touch: Files rows and drags (2026-11-19)

The owner, finger on the handheld: a tap on a row of the rebuilt Files page did nothing, and the rebuilt lists
(Files, Playlists, a playlist, Settings' rows, Home's All recent) would not scroll under a drag. Both were found
after the shell landed (`docs/notes/2026-11-19-redesign-shell.md`), with the pages drawn in `lay::shell(mini).body`.

## What a Files touch did

`Ui::pointer` sends a touch down to `Browse::pointer` late (`pointerDownLate`), then `filesPointer` takes it. The
Files page keeps its own finger in `filesTouch` / `filesPressed` / `filesScrolled`, and its lift opens or ticks the
row. But `Browse::pointer` had this at its top:

```cpp
if (pt.kind != Pointer::Down)
{
    if (pt.kind == Pointer::Up && !ui.inMenu && browseDir == EXTDIR) return ui.manga->extPointer(...);
    return false;                       // <- every Move and Up stopped here
}
```

The Files hook (`if (!ui.inMenu && filesPage()) return filesPointer(...)`) sits below that return, so it only ever
ran for `Down`. A finger on a row therefore set the cursor (the yellow row) and nothing else: the `Up` never reached
`filesPointer`, so `choose()` was never called; the `Move` never reached it either, so `filesScrolled` stayed false
and the list never scrolled. The shared list lift in `Ui::pointer` (`listTouch`, `touchStartY`) could not cover it:
Files does not use that state.

The same loss hit the Files page's other fingers: its header's back and More, the select bar and the empty page's
"Go up" / "Send files" keys all act on the lift. (The shell's own header keys are taken by `Ui::Shell` first, so the
header's back and More worked regardless.)

## The fix

- `src/ui_browse.cpp`, `Browse::pointer`: Move and Up go to `filesPointer` when `filesPage()`, after the Extensions
  drawer's own branch. Nothing else was needed there: `filesPointer` already handled Down, Move (a drag) and Up (a
  tap), including "a drag does not also tap" (`filesScrolled` on the lift).
- `src/kitlayout.*`: the drag's maths is now `lay::dragScroll(scrollStart, downY, y, rowH, first, n, shown)` (one row
  per row pitch, rounded, kept inside the list). Files (52), a playlist (54) and the shell's lists through
  `Ui::pointer` (Settings 46, All recent 48, a track list 48) all call it, so they scroll alike. The 12-unit drag
  slop stays at the call sites (it is a finger's, not a layout's, number). Tests: `testKitLayout`'s `dragScroll`
  checks (no travel, rounding, both clamped ends, a `..` row's `first`, no pitch).
- Logging, for the handheld's `log.txt` (grep `touch:`):
  - `touch: down X,Y on page N, section S` for every touch down on a shell page, before anything can swallow it;
  - `touch: shell rail <section>` when the rail takes one;
  - `touch: files tap row R of N at X,Y`, `touch: files drag y A -> B, scroll C -> D of N` (once per gesture);
  - `touch: playlist drag ...`;
  - `touch: list drag page N ...` for Settings' rows and All recent (the shared lift).

  A finger that does nothing now leaves the down line and no tap/drag line: that says the event reached `Ui`, and
  which of the two layers stopped it. A missing down line says the touch never got to `Ui`.

## The other lists

Their routes were checked line by line and are unchanged by this fix:

- **Playlists list and a playlist** (`Ui::Playlists::pointer`, `src/ui_playlists.cpp`): its own rows, `plPressRow`,
  `plTouchScrolled`, `plScrollStart`; `Browse::pointer` calls it for every kind *before* the return above, so the
  drag and the lift were already wired. `lay::dragScroll` replaces its inline rounding.
- **Settings' rows and tiles** (`SettingsPage::settingsDown`): the down begins the shared list touch (`ui.listTouch`),
  and its Move/Up are `Ui::pointer`'s one list block (`Ui::pointer`): `lay::dragScroll` now does its scroll.
- **Home's All recent** (`Home::recentPointer`): the down begins the same shared list touch on Home's own row rects;
  the drag and lift are the same block.

So Files was the page with no drag at all; the others scroll through the shared path (or their own) and now share
the same maths and the same log line. If the owner still sees one not following a finger, `touch:` says whether the
down or the drag is missing, and on which page.

## Checked

- macOS build (`SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk`, `cmake --build build -j`) and
  `./build/core-test`: `core: all passed`, the new `dragScroll` checks included.
- Read back: `Browse::pointer`'s Up path now reaches `filesPointer` for a Files page; `filesPointer`'s lift refuses
  the tap when `filesScrolled`, and its drag/`cursor` clamp is unchanged.

## Not checked

- `--press` on this build: the session's shell refuses `./build/banana-player` (and the session scratchpad outside
  the worktree), so `tap:300:130`, `drag:300:360:300:150` and `rows` on a generated folder were **not run**. The
  generated folder (`.scratch/touch-folder`, ten `.mkv`/`.mp4` names) exists in the worktree for the owner's run;
  it is not a media folder (the files are placeholders), only a listing.
- The handheld itself: the real finger's drag slop and the log's on-device lines.
- The Linux/arm64 build (no Linux-only file changed).
