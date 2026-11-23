# Engineering notes

One dated note per investigation, named `YYYY-MM-DD-<slug>.md`: the problem as seen, the root cause, the numbers, what
was ruled out, and how to reproduce or verify it. `CLAUDE.md` keeps only the one-line rules that came out of them; the
reasoning lives here. An agent finishing an investigation adds a note and, if it made a rule, one line to `CLAUDE.md`.
Write the way the rest of the repo does: plain and precise, with the why. Say what was not verified.

## Index

- [2026-11-23 the manga engine's wait](2026-11-23-manga-engine-wait.md): Resume or a chapter before the engine answers is a waiting state of the series page with the seconds (`mangaWaitView`), a downloaded chapter never waits (`mangaOpenWay`), and Home warms the engine when an online series was read last (`mangaEngineWarm`).
- [2026-11-21 Files' places: storage cards](2026-11-21-storage-places.md): each storage a card with its room and a bar (`lay::placeRow`, `capacityOf`, `storageLabel`), the same on the dashboard (`/api/roots` `card` / `where`, `Place`), and the sidebar lists every storage.
- [2026-11-21 smooth scrolling](2026-11-21-smooth-scroll.md): Music's lists follow the finger by the pixel, glide and band at the ends (`lay::Scroller`, `Ui::SmoothList`: row slots, scroll frames composed over a kept frame, an LRU text cache); the frame's cost by part in the timing lines; how to move another list onto it.
- [2026-11-19 the Now playing key and Quick settings' block](2026-11-19-np-rail-quick.md): the mini bar leaves every page (`lay::shell()` loses its box); the rail's 68 × 44 Now playing key (art at 30, bars, a pause mark, its slot kept) with tap / hold / SELECT; Quick settings' 592-wide panel heads with an always-present Now playing block; the header's status cluster opens the panel; the tests and what was not done.
- [2026-11-19 the player's chips open their own sheets](2026-11-19-player-sheets.md): no View sheet or tabs (each chip opens a titled sheet), Repeat steps and Shuffle toggles in place, More's first row is the chips, Picture's Adjust page, and a photo's SELECT More; `G_LOOP_ONE`, the sheet chip row, `--press` shots.
- [2026-11-19 the shell's touch: Files rows and drags](2026-11-19-shell-touch.md): Files' rows set the cursor but never opened (its Move/Up never left `Browse::pointer`) and no rebuilt list shared one drag's maths; one `lay::dragScroll`, the Files lift, and `touch:` log lines that name the page and the missed step.
- [2026-11-19 the left stick scrolls the readers](2026-11-19-stick-scroll.md): LEFTY read past a 15 % dead zone, squared, one panel height a second at full push; the text viewer and the Strip scroll, a paged page with room pans, Book turns; the speed curve is bpcore's `stickScrollSpeed` (issue #16).
- [2026-11-18 Previous on a video](2026-11-18-previous-video.md): Previous restarts a song 5 s in, never a video; a video always goes to the one before, and on the first one does nothing unless Repeat All; the kind is the session's `audioOnly` (`Loaded`), I17.
- [2026-11-17 a song's View: the equalizer's bands](2026-11-17-song-view-eq-drag.md): the sheet's ten bands did not move under a finger (the slider was grabbed with no overlay owner, so it was dropped as stale); `this` makes them the Sound page's slider, through `eqSetBand` / `syncEq`.
- [2026-11-17 settings groups](2026-11-17-settings-groups.md): the Settings page as the spec's ten groups, drawn with the kit (tiles with a live summary, rows with the stepper), L2 / R2 between groups; the readers' defaults get their own rows.
- [2026-11-17 lid keys and incognito](2026-11-17-lid-keys-incognito.md): the keys with the lid shut (the player takes `gpio-keys-volume`: a press one volume step, a 0.6 s hold next / previous) and incognito's lid or sleep choice (Close all / Quit) with the Web dashboard held off.
- [2026-11-17 end of a song list](2026-11-17-end-of-list.md): the last song of a list stays loaded, back at 0:00, paused (A or the power key plays it again); a song with one after it goes on to it; videos end as before; `keep-open=always` for songs with `eof-reached` as their end, I16.
- [2026-11-17 power key](2026-11-17-power-key.md): with the lid shut and Keep playing, the power key is play/pause and a 2 s hold sleeps (an `EVIOCGRAB` on `rk805 pwrkey`, released the moment the conditions end, a 3 s main-loop safety).
- [2026-11-16 manga library scan](2026-11-16-manga-library-scan.md): the Manga app froze for minutes on a big card (every chapter file opened on the main thread at each open); now listed in the background, files opened lazily, what each held kept in `<state>/manga/library-index.txt`.
- [2026-11-15 Ui areas](ui-areas.md): a part of the app in its own struct and header (`Ui::Area`); the document viewers moved out of `ui.h` first, the plan for the rest.
- [2026-11-15 sliders](sliders.md): one slider for every bar a finger moves (`Ui::slideGrab`, `slider.h`): grabbed anywhere in its row, absolute, kept until the lift, paced for costly targets; how a held one looks; how to add one.
- [2026-11-14 overlay stack](overlay-stack.md): dialogs and sheets as `Ui::Overlay` (keys, touches, drawing and the signature from one stack); the Brightness sheet and the message dialog moved; how the others would.
- [2026-11-13 visualizer lut cache](2026-11-13-viz-lut-cache.md): style changes slow on the handheld (lut2/lut configure); expressions cached as exact lookups; the remap form ruled out for its per-frame cost; the frame-rate setting.
- [2026-11-11 reader memory](2026-11-11-reader-memory.md): the player and the manga reader, back and forth: no leak; glibc kept the freed pages (mmap threshold pinned); the engine restarted every five minutes inside Manga.
- [2026-11-10 picture subtitles](2026-11-10-picture-subs.md): PGS/VobSub/DVB in the browser player; `/api/picsubs` windows, costs, test media.
- [2026-11-10 play on](2026-11-10-play-on.md): the handheld picks where it plays; page names, the refused `play()` (`Blocked`) and how to test it headless.
- [2026-11-11 session migration](2026-11-11-session-migration.md): steps b–f of the one playback Session; the Stop-then-pick bug and what the owner will notice.
- [2026-11-10 file durations](2026-11-10-file-durations.md): lengths in the file rows and Up next; header-only probes, the formats skipped, MP3 without Xing.
- [2026-11-10 background playback](2026-11-10-background-playback.md): leaving the player keeps it playing; the leave/stop split and the browser-play bug it found.
- [2026-11-10 fast skip sync](2026-11-10-fast-skip-sync.md): the handheld's Next pressed fast while a browser plays left the two on different files.
- [2026-11-09 visualizer performance](2026-11-09-visualizer-performance.md): lag on hi-res FLAC and above 1x speed; measured on the handheld.
- [2026-11-09 silent handoff](2026-11-09-silent-handoff.md): browser to handheld play left the handheld's sound silent.
- [2026-11-09 art graph, stale tracks](2026-11-09-art-graph-stale-tracks.md): songs going silent and freezing in art + visualizer.
- [2026-11-09 iOS Safari history](2026-11-09-ios-safari-history.md): the web player's chevron leaving the page on iPhone.
- [2026-11-09 crash log](2026-11-09-crash-log.md): why `crash.log` exists and how it is bounded.
- [2026-11-10 Photos over media](2026-11-10-photos-over-media.md): photos in their own mpv, what plays goes on under them (a video decoding on, unseen).
- [2026-11-16 lid and unplug](2026-11-16-lid-and-unplug.md): Keep playing with the lid shut (BananaOS's `lid_sleep` switched and put back, the launcher's undo), a pause when the headphones go.
- [2026-11-16 redesign kit](2026-11-16-redesign-kit.md): how to build a screen with the redesign's component kit (kitlayout.h, ui_kit.h); the player screen as the first one.
- [2026-11-14 button rows](button-rows.md): one helper for rows of buttons on the bottom panel (rowCell, gridCell, drawKeyedLabel).
- [settings table](settings-table.md): a setting is one `SettingDef` entry (key, member, range, page row); what keeps custom code, how to add one.
- [2026-11-18 themes](2026-11-18-themes.md): the theme table (theme.cpp), the names that read the current theme, the swap in main's loop, the dashboard's /theme.css.
