# The manga engine's wait (GitHub #19 follow-up, 2026-11-23)

**What was seen.** Resume pressed on an online series right after opening Manga: a passing note ("the manga engine
is still loading…") and nothing else for 28 s (log: starting 19:51:33, running 19:52:01, then "manga: resumes chapter
952 page 48"). It worked; it read as frozen.

**What was decided.**
- The wait is a state of the series page, not a note: `Ui::Manga::drawMangaOpenWait` (bottom) and `waitingPane` (the
  top panel's State card), from one pure view, `mangaWaitView(stage, elapsedMs)` in `manga_ui_logic.*`: the engine's
  stage counts seconds against `MANGA_ENGINE_USUAL_MS` (30 s) with a bar that stops at 95% (an estimate must not say
  done); the series check and the chapter list have no bar. B (or the header's back) drops it; no other key acts.
- A picked chapter waits the same way (it was a 2 s note and a second press): kept by its key
  (`mangaChoosePendKey`), since the check can redo the list; gone from the list, a note says so.
- A downloaded chapter never waits (`mangaOpenWay`): Resume reads the card's copy of the list, and when its chapter
  is on the card it opens its CBZ at once. The series' check stays put off, as for a picked downloaded chapter.
- The engine is warmed on Home (`mangaEngineWarm`, `mangaEngineWarmTick`): only when the newest History row is an
  extension's series read within 3 days, nothing plays, it is installed and not started, no runtime upgrade; once a
  run. It says nothing outside Manga (`engineWarmQuiet`). Entering Manga already started it (engineEntryWant): that
  alone left the whole 28 s to someone who goes straight to Resume.
- An engine not installed (removed since) fails the wait at once with a note instead of waiting for ever.

**Not done.** Opening a chapter from the page cache without the engine: the cache is keyed by the page list the
engine gives, so there is nothing to open before it answers. Browsing a source while the engine starts keeps its own
loading rows and the chip at the top ("Loading manga engine… N s").

**Not checked on the Mac** (no engine there: a java series fails with "isn't installed"): the waiting state's look on
both panels, the seconds (they count from when the Ui first saw `Starting`, `extNoticeAt`), the warm start on Home and
its memory (about 400 MB until the idle stop, 5 minutes). Log lines to read on the handheld: "manga: the engine
warmed from Home", "manga: resume waits for the series (engine state N)", "manga: resumes chapter … from the card",
"manga: the wait was cancelled".

**Follow-up: stuck on "Checking this series…" (handheld log, 2026-11-23).** A picked chapter that waited was never
looked at again: the per-pass step (`mangaNotesTick`) ran only for a waiting Resume, so after the engine came up the
check answered and nothing opened; the view said "Checking" for ever. Now both kinds of wait are stepped every pass
through one pure `mangaWaitAct` (`manga_ui_logic.*`): the check is sent when wanted and not on its way, a check or
list not answered 30 s after the engine is up (or after the last send) ends the wait with its reason on the page and
the top panel (A Retry: the list asked again, or the check sent again; B Back), as does an engine that fails. The
core test plays the handheld's sequence (starting → press → cancel → press → running → list → opens). Log lines:
"manga: series check sent / answered / failed", "manga: wait ends: …", "manga: the wait is tried again".
