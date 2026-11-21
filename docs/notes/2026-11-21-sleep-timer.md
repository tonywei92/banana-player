# The sleep timer (2026-11-21)

Built from `docs/redesign/sleep-timer.md` with the owner's overrides: the **Sleep** key replaces **List** on the
player's face on songs and videos alike (four keys; the list is **Y** or a tap on the Next line, and the List key's
playlist glow went with it), the timer wins over a signed-in browser, and the web page shows and changes it.

## Where it lives, and why not in the Session

- `src/sleeptimer.*` (bpcore): the pure rule. A `SleepTimer` and `sleepTick(t, in)` with the time and what plays coming
  in (`SleepIn`: loaded, seconds left of the song / video / chapter, its end seen) and what to do going out
  (`SleepOut`: pause, sleep, the fade factor). Tested in `testSleepTimer` (count, label, last minute, fade, snooze,
  End of this song, a skip, the list ending first, the dark's 10 s).
- `src/ui_sleep.*` (`Ui::Sleep`, owned by Ui, not an area: it has no page): holds one, is ticked by main once a pass,
  and does what it says: `Player::setFade` (mpv's `volume` = boost × fade; the handheld's volume never moves), the
  Pause through `SessionHost` (`play(false)` + `pause()` here, `playLive(false)` when a page plays it), both screens
  dark (`panel.set(false)`; the top panel black through `TopScreen`'s `sleepBlack`), and its sheet and warning
  (`Ui::SleepSheet`, `Ui::SleepWarning`: overlays). The layouts are `lay::sleepSheet`, `lay::sleepMove`,
  `lay::sleepWarn` (kitlayout_new.cpp; `testSleepLayout`).
- **Not the Session:** the Session is what plays and where; the timer plays nothing and moves nothing in the list. At
  its end it asks for a Pause, as a person would, so the Session's invariants and its reducer stay as they are. It
  needs the wall clock, the screens, the bananad request and the settings, none of which belong in `apply()`. And it is
  never saved or published as Session state (the web page gets it as its own field, `sleep`).
- **main** (`main.cpp`): `ui.sleep->tick()` each pass; when it says the sleep is due, the holds are
  `idleHoldsNow()` (now one lambda shared with the idle sleep) less playing, the browser output and a signed-in
  browser, so only a copy, a move, a download or an update holds it (with Stay awake for copies and downloads on).
  Then incognito's own step (`incognitoBeforeSleep`, shared with the idle sleep), a `bye` with reason `sleeptimer` to
  the pages, and `askBananadSleep()` (`/run/banana/sleep-now` + SIGHUP), as BananaOS allows. End of this song / video
  hooks `player.finished` (paused right after the Session moves on, so the next one opens paused); a chapter's end and
  a page's song are watched by the host (`seenEnd`). Stop (not the list's end) turns it off; a wake (`woke`) turns it
  off and, after its own sleep, puts up "Paused by the sleep timer" until the first key. Power with the lid shut in its
  last minute snoozes.
- **Web:** `WebSnapshot::sleep*` → `"sleep": {kind, label, warn, at, minutes, snooze, word}`; `POST /api/sleep`
  (`do=set&min=15|30|45|60|90`, `end`, `snooze`, `off`) through `WebServer::call`, behind the usual guards.
  `web/js/sleep.js`: the Now playing row, the sheet, the desktop bar's key, the last-minute card (mounted in the shell).
  The down screen has a `sleeptimer` reason.

## Test variable

`BP_SLEEP_SECONDS=N`: every length is N seconds (with N under 60 the last minute starts at once). The scripted check:

```sh
BP_SLEEP_SECONDS=5 BP_SHOTS=<scratch> ./build/banana-player tone.mp3 --state-dir <scratch> \
  --press "info,tap:397:300,shot:sheet,A,info,info,info,info,shot:warn,A,info,tap:397:300,Down,Right,A,info,shot:off"
```

(`tap:397:300` is the Sleep key.) A second run with `BP_SLEEP_SECONDS=2` and twenty `info` steps goes through the end:
paused, back where the fade began, both screens dark, the sleep due after 10 s (not asked: no `/run/banana` on a Mac),
then a key wakes the screens, it stays paused and the note shows.

## Not verified

- The handheld: the sleep itself (bananad's SIGHUP), the panels after it, KMSDRM's first frames, the fade's steps on
  the real speaker, the 640×480 panel's look of the badge and the warning card.
- The lid shut with Keep playing (Power's snooze), a page as the output at End of this song (its end is read from the
  Session's item changing in its last 3 s), End of this chapter (read from mpv's chapter and position).
- The top screen's last-minute card now also goes over a reader's pages and the Preview pane (Player::sleepCardOnTop ->
  TopScreen::cardImageOnReader, put again each second); the quiet mark still is an mpv overlay only. A dimmed bottom
  screen stays dimmed through the last minute (Ui: no undim while "sleepwarn" shows). Not looked at on the handheld.
- The web page only by `curl` (set, a refused length, snooze, end, off, the state's field) and a headless load of the
  app (the modules load); the row, the sheet and the card were not looked at in a browser.

## The end that didn't sleep (2026-11-21, c-sleepfix)

On the handheld the log stopped at "ran out: paused, both screens off" / "top: black (the sleep timer)": no "the sleep
is due", no request to bananad, and the top panel stayed lit (drawn black, backlight on). The 10 s step did run on the
Mac, idle or scripted (`BP_RUN_DIR=<tmp> BP_SLEEP_SECONDS=2`, the timer set through `POST /api/sleep`: `sleep-now`
written), so the handheld's cause is not pinned. Now: the grace is 3 s (`SLEEP_DARK_MS`), the timer's next moment
wakes the loop (`sleepNextMs` → `Ui::Sleep::waitMs`; `testSleepNextMs`), the top panel's backlight goes off too
(`TopPanel`, backlight1's `bl_power`, on again at a key, a wake, a quit, a crash, or whenever the dark ends), and a pass
that comes round over 1 s late in the dark is logged ("the loop came round after N ms in the dark"). The log says
"sleep timer: asked BananaOS to sleep" or why not. If the next handheld log still lacks "the sleep is due", the loop
itself stalled (look at the late-pass line) or something cleared the dark silently.
