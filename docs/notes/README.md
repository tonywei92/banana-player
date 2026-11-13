# Engineering notes

One dated note per investigation, named `YYYY-MM-DD-<slug>.md`: the problem as seen, the root cause, the numbers, what
was ruled out, and how to reproduce or verify it. `CLAUDE.md` keeps only the one-line rules that came out of them; the
reasoning lives here. An agent finishing an investigation adds a note and, if it made a rule, one line to `CLAUDE.md`.
Write the way the rest of the repo does: plain and precise, with the why. Say what was not verified.

## Index

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
