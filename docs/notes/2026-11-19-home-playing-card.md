# Home's card while something plays (no mini bar there)

The owner's decision (docs/redesign/backlog.md, "Home while something plays: one card, no mini bar";
components.md › Continue card and Mini bar; the drawn `states-browse`): on Home only, the mini bar is out of the
way, and the big card carries everything about what plays. This is what the build gained.

## What changed

- **The mini bar is hidden on Home.** `Ui::miniBarShown` (ui_home.cpp) returns false for the browser while
  `Browse::atHome()`. Every other page keeps the bar, and the old bar over a photo is untouched. `Ui::Shell`'s
  layout and the Hints bar follow `miniBarShown`, so the body is the full height it has with nothing playing: Home's
  Recent row and the three shortcuts are in the same place in both states (`lay::homePage` no longer loses the
  shortcut row on Home).
- **The Continue card's live state.** `Home::continueOf` now marks the card `live`, with `paused`, and a key of
  `Pause` / `Play` where `Resume` was. A page that plays the file (web dashboard) shows the session's wish
  (`ui.predicting` / `pagePlaying`). The note is the time only (`4:12 / 14:48`), not the folder, and the picture
  wears a yellow **NOW** pill (play mark + NOW) instead of the kind label.
- **The state pills** sit on the note line after the time, drawn as the kit's `Pill::Live` (yellow): speed when it is
  not 1x, **Shuffle** on a song, the EQ preset and the sound effect on a song, **Repeat one** on a video, and
  **Playing in …** when a page is the output. They are `Continue::pills` (plain strings) so the card's signature can
  hold them; `Home::sig` adds them, the key's word and the paused state, so a change redraws Home without a timer.
  The look pill is not there: this branch has no Picture look setting or video overlay yet (docs/notes/
  2026-11-19-redesign-settings.md lists it as missing). The pills are dropped one by one when the note line's width
  runs out before the key, so a long "Playing in …" never covers it.
- **Keys and touch.** A tap or A on the card (not its key) goes to the player: `openContinue` calls `showPlayer`
  when the card is live (B goes back to Home). The key calls `Ui::miniToggle`, the same step as the mini bar's play
  mark: a page that drives the file gets the session's Play / Pause, a short press, or a take-over held
  `PLAY_HOLD_MS`. A held key starts the same `playHoldAt` / `playHoldMini` hold the mini bar's mark uses; `Ui::tick`
  now accepts Home there (Home draws no bar), and the lift after a fired hold presses nothing. Holding any other
  part of the card opens its More at 500 ms, as a Recent card's hold does (the `HOME_PRESS_CONTINUE` sentinel in
  `homePress`; ui_draw.cpp); X and Y already opened the card's More, and still do, with Home's own column when the
  playing file is not in Recent.
- **The hint bar.** With something playing it reads A **Go to it** when the focus is on the Now playing card (or on
  the Recent card of the file that plays: A there is also its way back), A **Play** on any other card, A **Open** on a
  shortcut, then SELECT **Now playing** (Home has no bar to name it), X **More**, B **Quit**, START **Menu**. With
  nothing loaded it is as before: A **Resume** (or **Stopped for Manga**'s Resume), X More, B Quit, START Menu.
- FEATURES.md: Home's Continue bullet, the More key line and the mini bar's entry (every page but the player and
  Home; its Now playing card stands in) and the keys table.

## How it was checked

Built (`cmake --build build -j`) and `./build/core-test` (all passed). Scripted runs with generated media only
(`.scratch/homecard/`, `--press`):

- `.scratch/homecard/playing-{top,bottom}.png`: a video (`sd-video/clip.mp4`, generated) played, B, L to Home.
  Now playing, NOW pill, `0:15 / 0:40`, **Pause** (and the state dir's **Repeat one**, left from the run below);
  Recent and the shortcuts are laid out exactly as in `.scratch/homecard/continue-{top,bottom}.png` (same state
  dir, nothing loaded: Continue, **Resume**).
- `.scratch/homecard/paused-{top,bottom}.png`: a tap on the card's key, through the Session (`session: Pause`):
  the key turns to **Play**.
- `.scratch/homecard/repeat-{top,bottom}.png`: `set:loop=one`: the **Repeat one** pill after the time.
- `.scratch/homecard/song-pills-{top,bottom}.png`: a song (`media/song1.mp3`, generated), Shuffle and EQ on:
  album art, **Shuffle** and **Rock** pills.
- `.scratch/homecard/files-mini-{top,bottom}.png`: R to Files while it plays: the mini bar is still there.
- `.scratch/homecard/more-{top,bottom}.png`: a 1.1 s hold on the card (not the key): its More sheet, **Go to it**
  first, Home's own column beside it.
- `.scratch/homecard/player-bottom.png` / `.scratch/homecard/backhome-bottom.png`: a tap on the card goes to the
  player; B comes back to Home with the card.

## Not checked

- The speed pill: no speed setting reaches a scripted run, and speed changes only on the player's Speed sheet.
- The take-over hold from a browser (a web page playing the file): needs a web client driving the session; the hook
  is the mini bar's own (`pageDrives` → `playHoldAt`), but it was not exercised.
- "Stopped for Manga" after a Manga stop was not re-shot; its path (`fromResume`) is untouched.
- The mini bar's own pills were still not drawn by this branch's bar (it shows the words "Now playing"); Home's card
  built its pills from the settings directly. The shared helper now exists: `Kit::statePills` (ui_kit_variants.cpp),
  used by Home's Continue card and by the shell's mini bar (docs/notes/2026-11-19-kit-inventory.md).
