# The lid shut: incognito, the dashboard, and the keys

Two additions to the redesign. Both are drawn in preview.html (System): **Incognito: the lid and sleep**
(`incog-lid`), **Incognito: the Web dashboard is off** (`incog-web`), **Incognito while a browser is signed in**
(`incog-web-ask`), **Lid shut: the keys** (`lid-keys`). The settings rows are in **Settings › Power & screens**
(`set-screen`).

What exists today (0.7.27): Incognito is a switch in Quick settings, never saved. **When the lid closes: Pause / Keep
playing** is a setting, asked at the first start. The Web dashboard keeps the handheld awake while it is on. Being
built: with the lid shut and Keep playing, a press of the power key is play / pause and a hold of 2 s is sleep.

## 1. Incognito, the lid and sleep

### The setting

- **Where:** Settings › Power & screens, the third lid row: When the lid closes, Shoulder keys, lid shut, **Lid or sleep
  in incognito**. A settings row (stepper), one entry in `settingDefs()`. It is saved. Incognito itself is still never
  saved.
- **Values:** **Nothing** / **Close all** (default) / **Quit**.
- **When it applies:** the lid closes (with Pause or with Keep playing); the handheld is put to sleep by the power
  key (BananaOS's, or the 2 s hold with the lid shut); the handheld sleeps by itself after its idle minutes.
- **Help (top screen):** “What happens while incognito is on when the lid closes or the handheld goes to sleep. Close all: what plays stops, books and
  photos close, Home shows, and incognito is off.”
- **While incognito is off:** the row can be set; it does nothing until incognito is on.
- **Said where it matters:** Quick settings’ Incognito tile carries a second line while it is on: “Lid or sleep: closes all”,
  “Lid or sleep: quits”, nothing for Nothing. Turning incognito on shows one note on the top screen (3 s): “Incognito on” over
  “Lid or sleep closes all” (or “Lid or sleep quits”).

### What each value does

| Value | When the lid closes or the handheld is about to sleep, in incognito |
|---|---|
| Nothing | Incognito changes nothing. The lid does what *When the lid closes* says; sleep is sleep. |
| Close all | What plays stops (Stop, everywhere). A book, PDF, comic, photo, the Manga reader and any sheet close. The bottom screen goes to Home. Incognito turns off. |
| Quit | Banana Player quits at once, as F + START does. Incognito is off at the next start, as always. |

- Close all is Stop: where you stopped, reading places and read marks are kept exactly as Stop and Back keep them in
  incognito today. Nothing was going into Recent or History, so Home shows nothing of the session.
- Copies, moves and downloads go on. They are not media.
- The Web dashboard comes back if its switch was on (section 2).

### With Pause / Keep playing

Incognito’s choice is decided first, at the moment the lid closes or just before the sleep.

| When the lid closes | Nothing | Close all | Quit |
|---|---|---|---|
| Pause | Pauses; the handheld sleeps. Still in incognito on waking. | Stops, closes, Home, incognito off; the handheld sleeps. | Quits; the handheld sleeps. |
| Keep playing | Keeps playing, still in incognito. The lid-shut keys work. | Stops, closes, Home, incognito off. Nothing is loaded, so the handheld sleeps. | Quits; the handheld sleeps. |
| Sleep by the power key or by itself (lid open or shut) | Sleeps, still in incognito on waking. | Stops, closes, Home, incognito off; then it sleeps. | Quits; then it sleeps. |

- **Close all and Quit win over Keep playing.** In incognito the lid is the way to put everything away; that matters
  more than the music going on. Someone who wants music in a pocket while in incognito sets Nothing.
- After Close all nothing of the session can be started again from outside: Power’s play / pause has nothing loaded.
- Idle sleep is asked for by the player, so it closes first and asks after. A sleep by the power key may stop the
  player before it has closed: the rule under *Opening the lid again, or waking* covers it.

### Opening the lid again, or waking

- Home on the bottom screen, the usual preview on the top. No hat-and-glasses badge.
- **No note.** Nothing on either screen says incognito was on or has ended. “Paused: the handheld slept” is not shown
  either: nothing is paused.
- If the handheld slept before the player finished closing, it finishes on waking, before the first picture is drawn.
  Both screens are redrawn after a wake already; nothing of the session may be in that first frame.

## 2. Incognito and the Web dashboard

**Rule:** while incognito is on, the dashboard’s server does not listen. The dashboard shows what plays and what is on
the card to every browser signed in; incognito and that do not go together.

The switch keeps what you set. Incognito holds the dashboard off; leaving incognito puts it back, with the same PIN and
the same browsers signed in. (After a quit in incognito it is on at the next start, as its switch says.)

### Turning incognito on while the dashboard is on

| Dashboard | What happens |
|---|---|
| On, nobody signed in | No question. The dashboard goes off. Note: “Incognito on” over “Web dashboard off · Lid or sleep closes all”. |
| A browser signed in | A confirm sheet (drawn: `incog-web-ask`). |
| A browser playing | The same sheet; its sentence ends “What it plays stops; its place is kept.” Playing does not move to the handheld by itself. |
| A browser uploading | The same sheet; “An upload stops and must be sent again.” |

The confirm sheet: title **Turn on incognito?** Text: “The Web dashboard turns off while incognito is on. 1 browser is
signed in: Chrome on Mac. It can’t reach the handheld until you leave incognito.” Keys: **Cancel** (left, yellow, B) /
**Turn on**. A dim line under the text names the lid choice.

### The dashboard switch while incognito is on

Unavailable, by the one rule: 45%, the reason in the note.

| Where | What it shows |
|---|---|
| Quick settings › Web dashboard | The tile at 45%, second line “Off in incognito”. A changes nothing; the top screen notes “Leave incognito to use the Web dashboard”. |
| Settings › the Web dashboard tile | Summary “Off in incognito”. |
| The Web dashboard page | The switch at 45% with “Off in incognito”. One sentence: “The Web dashboard is off while incognito is on. It comes back by itself when you leave incognito.” One key: **Leave incognito** (Y). No QR, address or PIN. |
| The header | No globe. The hat-and-glasses badge. |
| Home’s “Send from phone”, Books’ “Web dashboard” key, Network › X › Web dashboard… | Open the page above. |
| X › More › Play on… | Not offered: no browser can be there. |

### Leaving incognito

- By its switch (Quick settings, or Leave incognito on the page): the dashboard is on again. Note: “Incognito off” over
  “Web dashboard on again”.
- By the lid or a sleep (Close all): the dashboard is on again, no note.
- The dashboard was off before incognito: it stays off.

### What a connected browser sees

- A page that was open: “**The dashboard is off**. It was turned off on the handheld. This page comes back by itself
  when it is on again.” and **Try now**. The server tells its pages before it stops, so this shows in place of a
  connection error. The page tries again quietly; when the dashboard is back it carries on, signed in as before.
- It never says why. The word incognito is not sent to a browser.
- A page that was playing stops; its place is kept on the handheld.
- A browser opening the address while it is off: nothing answers.

### Other rules this touches

- Awake: a dashboard that is off keeps nothing awake.
- The server object stays (it is never destroyed); it closes its listening socket and its open connections.

## 3. The keys with the lid shut

For listening with the handheld shut, in a pocket or a bag. It applies only while the lid is shut and the handheld is
awake, so only with **When the lid closes: Keep playing**. With Pause the handheld sleeps and there are no keys.
With Keep playing the handheld does not sleep while the lid is shut and something is loaded, playing or paused.

### What the keys do on the player today (lid open)

| Key | Today |
|---|---|
| L / R | Previous / next file in the list. Previous, 5 s or more in: back to the start of this one. |
| L2 / R2 | Previous / next chapter. L2, more than 5 s into a chapter: back to its start. R2 on the last chapter stops there. A file with no chapters: nothing, and the note “No chapters in this file”. |
| Volume + / − | The handheld’s volume, one step a press. BananaOS moves it at the press and repeats while the key is held; past 100% the player adds its own 5% a press up to Max volume. While a browser plays: that browser’s volume. |
| Power | BananaOS’s: sleep. |

Today nothing is different with the lid shut: every key acts as with it open, on whatever page was left showing.

### Option A (the owner’s proposal)

- A setting: the shoulder keys with the lid shut are **Ignored** or **As usual** (the table above).
- Volume + / −, a press: volume, as today.
- Volume + / −, a long press: next / previous.

What it leaves open: the keys under the lid, how long a long press is, what the volume does when a long press was
meant, a key held down by a pocket, and how you know it worked. And because BananaOS repeats a held volume key, a long
press as proposed also runs the volume up or down while it is held.

### Option B (recommended)

Option A’s volume keys and setting, with the open points settled and one more value.

| Key, lid shut | Press | Hold 0.6 s |
|---|---|---|
| Volume + | Volume up one step, when let go. Stops at 100%. | Next. The volume does not move. |
| Volume − | Volume down one step, when let go. | Previous (5 s or more in: back to the start). The volume does not move. |
| Power | Play / pause. It stays awake after a pause. | 2 s: sleep. |
| L / R | By the setting | By the setting |
| L2 / R2 | By the setting | By the setting |
| A, B, X, Y, D-pad, START, SELECT, F, the stick, touch | Ignored | Ignored |

**Shoulder keys, lid shut** (Settings › Power & screens, under When the lid closes):

| Value | L / R | L2 / R2 |
|---|---|---|
| Ignored (default) | Nothing | Nothing |
| Hold | Held 0.6 s: previous / next | Held 0.6 s: previous / next chapter |
| As usual | A press: previous / next | A press: previous / next chapter |

Help: “What L, R, L2 and R2 do while the lid is shut and something plays. Ignored: they do nothing. Hold: they act when
held for 0.6 s. As usual: they act at once.” With When the lid closes: Pause the row is at 45%, its note “The lid
pauses”.

The rules:

1. **One grammar.** With the lid shut a press changes only what a second press undoes (volume, play / pause). Changing
   the track takes a hold.
2. **The keys act on what plays**, never on the page left open. (L and R on a list step the sections; with the lid shut
   they must not.) They are the same intents the player's keys send, so a browser that has the sound follows them too.
   A video gets the same keys as a song.
3. **Under the lid is ignored.** The face keys, the D-pad, START, SELECT, F, the stick and the touch screen cannot be
   meant with the lid shut. No setting.
4. **The threshold is 0.6 s**, the redesign's other hold (hold A to take over). A graze is shorter; a second feels
   slow when counted without looking. Sleep stays at 2 s because it costs more to undo.
5. **The player owns the volume keys while the lid is shut** (see below). A press steps the volume once, when the key
   is let go. A hold steps the track at 0.6 s and never touches the volume. Nothing is moved and put back.
6. **One hold, one step.** No repeat. A key still down after the step does nothing more. A key down for more than 5 s
   is taken as stuck and ignored until it is let go. So a pocket pressing on a volume key can skip one song; it cannot
   run the volume up or run through the list.
7. **The volume keys stop at 100% with the lid shut.** A boost set before stays; the keys do not add to it unseen.
8. **No sound effects.** What happened is heard in what plays: the song changes, the sound stops or starts. A hold
   with nothing to go to (the last one, the first one) does nothing.
9. **No sleep with the lid shut while something is loaded.** With Keep playing the handheld stays awake for as long
   as the lid is shut and a file is loaded: while it plays, after a pause, after the list ends. With nothing loaded
   (after Stop, or after Close all in incognito) it sleeps as BananaOS normally would. Power can always play again. Only a 2 s hold of Power sleeps (and the
   battery at 5%, as the power table says).
10. **Opening the lid:** the screens come back on the page left open; the first press acts. No summary of what was
    pressed.
11. **Taught at the choice.** The first-start lid question says it: "Keep playing: the sound goes on with the lid
    shut. Then Power pauses, and holding a volume key skips." The setting's help lists the keys on the top screen.

### The volume keys: no jump and back

BananaOS moves the volume itself when a volume key goes down, and repeats while the key is held. A hold of 0.6 s would
be heard as the volume climbing and then, if the player put it back, dropping again. Three ways were weighed:

| Way | Result |
|---|---|
| Let BananaOS step, put the level back at 0.6 s | Heard: several steps up, then back. Rejected. |
| Let BananaOS step, keep what it did | A hold to skip also turns the volume up or down, more each time. Rejected. |
| **The player takes the volume keys while the lid is shut** | BananaOS never sees them, so nothing moves by itself. **Chosen.** |

How it works:

- When the lid shuts (Keep playing), the player takes the volume keys' input device for itself; when the lid opens, or
  the player quits or crashes, it lets go and the keys are BananaOS's again. With the lid open nothing changes.
- Key down: nothing yet. Let go before 0.6 s: one volume step, set by the player through BananaOS's own volume
  command (`/usr/libexec/banana/volume`), so the level BananaOS knows stays true. Still down at 0.6 s: the track
  steps; the volume is not touched.
- The cost: a press's step comes when the key is let go, not when it goes down (a tap is about a tenth of a second).
  Holding a key no longer runs the volume up or down with the lid shut: one press, one step. In a pocket that is wanted.
- While a browser has the sound the same keys step that browser's volume, as today.
- Code: Linux-only (beside `sleepwatch.*`, which already reads the lid switch): `EVIOCGRAB` on the device, released on
  lid open and on exit; the arm64 Docker check applies.

The volume keys are their own input device on the RG DS (`gpio-keys-volume`), apart from the power key (`rk805
pwrkey`) and the lid switch (`gpio-keys-hall`). So the player takes that one device and nothing else; BananaOS keeps
Power and the lid.

### Trade-offs

| | Option A | Option B |
|---|---|---|
| Presses by accident | As usual: one graze of R skips a song. Under-lid keys and a held key are open. | A graze can only move the volume one step or, on Power, pause. A held key gives one skip at most. |
| Learning it without looking | Two things to learn: volume hold, and shoulders if on. | The same two, one rule behind both: hold to skip. |
| Shoulder keys for those who want them | On or off. | Off, safe (hold), or as usual. |
| Volume when a hold is meant | Open: BananaOS repeats, so it climbs while held. | Never moves: the player owns the keys. |
| Loudness in a pocket | A held Volume + climbs to the top. | One step a press; never past 100%. |
| Cost | One setting, two values. | One setting, three values; the stuck-key rule; taking the keys from BananaOS while shut. |

**Recommendation: B.** It is A with its gaps closed, and its default is safe: only the volume keys and Power do
anything. The extra value (Hold) is for audiobooks, where chapters matter and a graze should not lose the place.

### Decided by the owner

- BananaOS repeats a held volume key: hence the player owns the keys while shut (above).
- No sound effects.
- Volume + is next, Volume − is previous.
- Keep playing: no sleep at all with the lid shut, also after a pause; a 2 s hold of Power still sleeps.
- *Lid or sleep in incognito* covers the lid, the power key's sleep and idle sleep.
- A video with the lid shut gets the same keys as a song.
- Leaving incognito puts the dashboard back as its switch says.
- Keep playing, lid shut, nothing loaded (after Stop, or after Close all in incognito): the handheld sleeps as
  BananaOS normally would.
- The volume keys are their own input device, so the player takes them alone.

No open questions.
