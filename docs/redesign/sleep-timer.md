# Sleep timer

New (there is none today). Drawn in preview.html (Player): **Sleep timer: setting it** (`sleep-set`), **while it runs**
(`sleep-on`), **the last minute** (`sleep-warn`), **it ran out** (`sleep-done`); the key on **The player: a song**
(`music`) and **a video** (`video`), the tile in **More** (`music-more`, `video-more`); the Web dashboard's side
(`phone-sleep`, `phone-sleep-warn`, with a desktop's width under the first). Components: Bottom sheet, choice tiles (Display's),
Away-from-normal mark, header badge, Note, and one new one, **Sleep warning** (components.md, Rules).

## The flow

1. **Open.** The **Sleep** key on the player's face. A song: where List was (Like, Playlist, Sleep, Stop). A video:
   a fifth key between List and Stop (Like, Playlist, List, Sleep, Stop; 115 wide each, as the five chips under
   them). By the keys on both: **X** (or SELECT) › the **Sleep timer** tile, third of five. While one runs: also a tap
   on the header's badge.
2. **Set** (`sleep-set`). A bottom sheet, 282 tall. Title "Sleep timer", right note "Off" / "24 min left" / "This
   song". Label "Pause after". Five tiles **15 min, 30 min, 45 min, 60 min, 90 min**; under them **End of this song**
   and **Off**. Foot line: "The last minute fades the volume. Then it pauses, and the handheld sleeps." A sets and
   closes; the top screen notes "Sleep timer · 30 min" over "pauses at 22:17" (3 s).
3. **Runs** (`sleep-on`). The Sleep key reads what is left, in ACCENT with the dot; the header of every page has a
   badge (clock, "24 min"); a song's top screen has the same words, dim, top right. Nothing else changes.
4. **The last minute** (`sleep-warn`). The volume fades to silence over 60 s. A card on the top screen: "Sleeping in
   0:48", **A** 10 more min, **B** Timer off. The same card with two touch keys on the bottom screen, if it is lit.
5. **It ran out** (`sleep-done`). Pause; both screens off; 10 s later the handheld sleeps (the setting: Sleep, the
   default, or Pause). On waking: "Paused by the
   sleep timer" over "A plays", kept until the first key.

## States

| State | Sleep key | Header badge | Top screen | Sheet's note |
|---|---|---|---|---|
| Off | "Sleep" | none | nothing | Off |
| Running, minutes | "24 min", ACCENT, dot | clock "24 min" | song: clock "24 min", dim, top right; video: nothing | 24 min left |
| Running, End of this song | "Song" | clock "Song" | song: clock "This song" | This song |
| Last minute | "0:48" | clock "0:48" | the card "Sleeping in 0:48" | (sheet closed by the warning) |
| Last minute, End of this song | "0:48" | clock "0:48" | "Sleeping after this song · 0:48" | |
| Ran out | "Sleep" | none | dark; on waking the note | Off |

"End of this song" reads "End of this video" on a video and "End of this chapter" where what plays has chapters (an
audiobook, a film with chapters).

## Keys and touches

| Where | Key / touch | Does |
|---|---|---|
| Player, a song or a video | tap Sleep | the sheet |
| Player, either | X or SELECT, then the Sleep timer tile | the sheet |
| Any page, timer running | tap the badge | the sheet, cursor on the choice in use |
| Sheet | D-pad | move over the seven tiles (opens on 30 min, or the choice in use) |
| Sheet | A, or a tap on a tile | set it (replaces a running one, counts from now) and close |
| Sheet | X (only while one runs) | 10 more minutes (the Snooze setting) |
| Sheet | B, or a tap on the veil | close, nothing changes |
| Warning | A, or tap "10 more min" | 10 more minutes; volume back in 1 s; the card goes |
| Warning | B, or tap "Timer off" | timer off; it plays on; volume back in 1 s |
| Warning | UP / DOWN, volume keys | volume, as always |
| Warning | any other key; a touch off the card | nothing (a dark screen wakes, as always; that press does nothing else) |
| Warning, lid shut (Keep playing) | Power press | 10 more minutes (for that minute only; otherwise play / pause) |
| The 10 s after it paused | any key or touch | the screens wake; the sleep is called off; it stays paused |

A and B are the warning's with both screens dark: the usual case in bed is the bottom screen off (Screen off (music),
30 s), the top one on.

## Copy

- Key: "Sleep" · Tile: "Sleep timer" · Sheet title: "Sleep timer" · Label: "Pause after"
- Tiles: "15 min", "30 min", "45 min", "60 min", "90 min", "End of this song" / "video" / "chapter", "Off"
- Foot: "The last minute fades the volume. Then it pauses, and the handheld sleeps."
- Notes (top screen): "Sleep timer · 30 min" / "pauses at 22:17"; "Sleep timer · this song"; "Sleep timer off";
  "10 more minutes"; on waking "Paused by the sleep timer" / "A plays"
- Warning: "Sleeping in 0:48" · "Sleeping after this song · 0:48" · bottom card's right note "the volume is fading"
  (End of this song: none; list ended: "the list has ended") · keys "10 more min", "Timer off"
- The snooze's words follow its setting: "10 more min", "+10 min", "10 more minutes".

## Settings (Power & screens, after When the lid closes; one `settingDefs()` entry each)

| Row | Values | Help (top screen) |
|---|---|---|
| When the sleep timer ends | **Sleep** (default) / Pause | "What the sleep timer does at its end. Sleep: it pauses, both screens turn off and the handheld sleeps. Pause: it pauses and the screens turn off; the handheld stays awake." |
| Sleep timer snooze | 5 / **10** (default) / 15 / 30 min | "How much longer it plays when you press A in the sleep timer's last minute." |

## Decisions

- **The Sleep key takes List's place on a song; on a video it is a fifth key.** A video keeps List: its chapters
  live there. Five keys at 115 line up with the five chips, and each is still a thumb's target (56 tall).
- **No key of its own.** Every key on the player is taken; Y stays List. X, then the tile, is two presses.
- **Five lengths, End of this song, Off; no custom length.** One press each; X adds the snooze to a running one.
- **Counts by the clock, playing or paused.** "30 min" means the handheld is quiet at that time, as on a phone.
- **A 60 s fade of the player's own volume (mpv's).** Long enough to notice awake, gentle asleep; the handheld's
  volume and its bar never move, so nothing has to be put back.
- **End of this song has no fade.** The song ends as it was made; the card still shows for its last 60 s.
- **It pauses, never stops.** The list, the place and Resume are kept; Stop would lose the place in the list.
- **After pausing it seeks back to where the fade began.** Nothing heard half-asleep at a whisper is missed.
- **Then the handheld sleeps** (10 s later, `/run/banana/sleep-now` + SIGHUP). Darkness and the battery are what a
  sleep timer is for; a paused player with lit screens all night is the failure. "Pause" is the setting for
  those who disagree (the owner's choice: a setting, default Sleep).
- **Not slept while a copy, move or download runs, or a browser is signed in.** The existing stay-awake rule wins;
  it is paused and dark, and BananaOS's idle sleep follows.
- **Snooze is one key with no choice at that moment; its length is a setting, 10 min by default** (the owner's
  choice). A choice in the dark is reading; a second A later adds it again, and the sheet has every length.
- **A = snooze, B = timer off; nothing else acts.** Two keys findable by feel; B as "leave this" matches every sheet.
- **The warning is on the top screen first.** The bottom one is usually off by then; the top one is the one facing
  the pillow.
- **The warning never lights a dark screen.** Not alarming: no sound, no red, no light. The fade itself is the cue
  when both are dark.
- **Screen off's own 5 s warning waits** while the sleep warning shows: two countdowns in one place is one too many.
- **The timer is the app's, not the Session's.** Session stays as it is; at the end the timer sends its Pause intent.
- **Not saved.** A timer does not outlive a quit or a sleep.

## Edge cases

- **Leaving the player:** it runs on; the badge is in every header; the warning is an Overlay over any page (a book,
  Manga, Settings) and a card over whatever the top screen shows.
- **Stop (the key, anywhere):** the timer is off, note "Sleep timer off".
- **The list ends first** (Repeat off): its last minute starts then: the card with nothing to fade, "the list has
  ended", then dark and sleep. A snoozes (nothing plays; the timer just runs 10 more), B turns it off.
- **Paused by hand when it runs out:** the card for 60 s, no fade, then dark and sleep.
- **Next / Previous during the minute:** they work; the fade and the count go on.
- **End of this song with Repeat One, or after a skip:** it is the end of whatever song is playing when it ends.
- **Lid closes, "Pause":** the handheld sleeps as always; the timer is off (it would have nothing to do).
- **Lid closes, "Keep playing":** it runs on. No card is seen; the fade is the warning; Power = 10 more min during
  it. At the end: pause, then the sleep asked of BananaOS, as for the 2 s Power hold.
- **Sleep by the Power key, or BananaOS's idle sleep:** the timer is off.
- **Bottom screen dimming (the 5 s Screen-off warning) when the last minute begins:** the sleep warning replaces
  it; the screen stays dimmed and on until the end.
- **Top screen off (music) set, both dark:** nothing is drawn or lit; A and B still work; any key wakes the screens
  to the card.
- **Lock touch:** the card's touch keys are locked too; A and B work.
- **Playing in a browser:** the browser is paused through the Session at the end; no fade (its volume is its own);
  the card still shows on the handheld. A signed-in browser keeps the handheld awake, so it is paused and dark only.
- **Incognito:** the sleep is a sleep: "Lid or sleep in incognito" applies.
- **A photo slideshow, a book, Manga with nothing playing:** the Sleep key and tile exist only on the player; a
  timer set while music played still ends as above, whatever page shows.
- **Volume changed during the fade:** the handheld's volume moves as always; the fade is a factor on top.

## The Web dashboard (`phone-sleep`, `phone-sleep-warn`)

One timer, the handheld's; the page shows and changes it, and both sides follow at once.

- **Now playing (a phone):** a **Sleep timer** row under the keys: "Off", or what is left in ACCENT with an ACCENT
  edge ("24 min left", "This song", "0:48"). A tap opens a sheet from the foot: label "Pause the handheld after",
  six chips (15 / 30 / 45 / 60 / 90 min, This song; the one in use filled); while one runs, two keys **10 more min**
  and **Timer off**, and "Pauses at 22:41." A tap outside closes.
- **A desktop's width:** a **Sleep** key at the right end of the player bar; it reads what is left while one runs. A
  click opens the same choices as a card over the bar.
- **The last minute:** a card with an ACCENT edge, pinned to the foot of every page (a phone) or over the bar (a
  desktop): "Sleeping in 0:48", **10 more min** (primary), **Timer off**. No sound, no red, no browser notification.
  Whichever side answers, both cards go.
- **At the end:** with Sleep the handheld sleeps and the page shows its lost-connection state with the reason: "The
  handheld is asleep (sleep timer)". A signed-in browser keeps the handheld awake today (the stay-awake rule), so
  as drawn the page sees it paused, not asleep (see Open).
- **Playing in the browser:** the same row and card; the browser is paused through the Session at the end, without
  a fade.
- **Build:** one field in the state the page already follows (the deadline, its kind, whether the last minute is
  on), one POST (set N / song / snooze / off) with the usual guards (session cookie, X-BP, Origin), run on the main
  thread through `WebServer::call`. `web/js/playing.js` for the row, sheet and card; colours from `web/style.css`'
  copies of the theme's tokens.

## Build notes

- `Ui::Overlay` for the sheet and for the warning; the badge through `status`' cluster; the key and tile through the
  Away-from-normal helper. `signature()` gets the timer's shown text (minutes, then seconds in the last minute).
- The timer: one deadline in main's loop (no thread). Redraws once a minute, once a second in the last one.
- Fade: `volume` on mpv (async `set_property`), 100 → 0 linearly; restored to 100 after the pause lands, or over 1 s
  on snooze / off. Do not touch `/usr/libexec/banana/volume`.
- New glyph: a clock (the kit's `clock`), `tools/make-sprite.py` after adding it.

## Open for the owner

1. A browser signed in to the dashboard keeps the handheld awake, so a timer set from the phone ends paused and
   dark, not asleep. Should the sleep timer win over a signed-in browser (copies and downloads still holding it)?
2. On a song, should List come back as a fifth key too, as on a video (Like, Playlist, List, Sleep, Stop)?
