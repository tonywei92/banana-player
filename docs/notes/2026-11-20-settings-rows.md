# Settings rows the redesign draws (status item 7)

Done: Picture look (Playback; Off, Sharpen, Soften, Scanlines, LCD grid, Retro, as an mpv `vf` read when a film opens),
Dim the controls while reading (Reading; the Screen off times, default 1 minute; a veil over the bottom panel while a
book, comic or document is open and untouched; the touch still reaches the page), Dim before turning off (Power &
screens; a veil to a third and "Screen off in N s" for the last 5 s; a press or touch only cancels), Stay awake for copies
and downloads (Power & screens; gates the idle-sleep holds for file jobs, chapter downloads, the extension engine and
updates), and the player's Adjust kept (`video-adjust`, written only when not all 0).

Top screen off (music) came after: `TopScreen::draw` has a `topBlack` branch (a glClear to black once, mpv's frames taken unseen by drainUnder) for a playing, unpaused song idle past the delay (Ui::lastInputAt, or the song's start); a press, touch, pause or track change lifts it. Not tried on the handheld (the shot only shows the row).

Not done: the looks' Strength and
"Only this video"; the Picture chip's name and the top-screen note when a look is on.

Unsure: the looks are software filters (decoding is software only) and are drawn in the film's pixels, not the panel's
(the spec's 2 + 2 / 3 + 3 lines); a heavy film may stutter on the handheld, untested. A look applies when a file opens,
so changing it mid-film waits for the next. The dim is a veil, not the backlight (brightness stays BananaOS's).
`read-dim` (Extra dim) keeps no row of its own: it is in the readers' Brightness sheet.
