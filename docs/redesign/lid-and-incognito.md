# The lid shut: incognito, the dashboard, and the keys

Two additions to the redesign. Both are drawn in preview.html (System): **Incognito: when the lid closes**
(`incog-lid`), **Incognito: the Web dashboard is off** (`incog-web`), **Incognito while a browser is signed in**
(`incog-web-ask`), **Lid shut: the keys** (`lid-keys`). The settings rows are in **Settings › Power & screens**
(`set-screen`).

What exists today (0.7.27): Incognito is a switch in Quick settings, never saved. **When the lid closes: Pause / Keep
playing** is a setting, asked at the first start. The Web dashboard keeps the handheld awake while it is on. Being
built: with the lid shut and Keep playing, a press of the power key is play / pause and a hold of 2 s is sleep.

## 1. Incognito and the lid

### The setting

- **Where:** Settings › Power & screens, the third lid row: When the lid closes, Shoulder keys, lid shut, **Lid closes
  in incognito**. A settings row (stepper), one entry in `settingDefs()`. It is saved. Incognito itself is still never
  saved.
- **Values:** **Nothing** / **Close all** (default) / **Quit**.
- **Help (top screen):** “What closing the lid does while incognito is on. Close all: what plays stops, books and
  photos close, Home shows, and incognito is off.”
- **While incognito is off:** the row can be set; it does nothing until incognito is on.
- **Said where it matters:** Quick settings’ Incognito tile carries a second line while it is on: “Lid: closes all”,
  “Lid: quits”, nothing for Nothing. Turning incognito on shows one note on the top screen (3 s): “Incognito on” over
  “Closing the lid closes all” (or “Closing the lid quits”).

### What each value does

| Value | When the lid closes in incognito |
|---|---|
| Nothing | Incognito changes nothing. The lid does what *When the lid closes* says. |
| Close all | What plays stops (Stop, everywhere). A book, PDF, comic, photo, the Manga reader and any sheet close. The bottom screen goes to Home. Incognito turns off. |
| Quit | Banana Player quits at once, as F + START does. Incognito is off at the next start, as always. |

- Close all is Stop: where you stopped, reading places and read marks are kept exactly as Stop and Back keep them in
  incognito today. Nothing was going into Recent or History, so Home shows nothing of the session.
- Copies, moves and downloads go on. They are not media.
- The Web dashboard comes back if its switch was on (section 2).

### With Pause / Keep playing

Incognito’s choice is decided first, at the moment the lid closes.

| When the lid closes | Nothing | Close all | Quit |
|---|---|---|---|
| Pause | Pauses; the handheld sleeps. Still in incognito on waking. | Stops, closes, Home, incognito off; the handheld sleeps. | Quits; the handheld sleeps. |
| Keep playing | Keeps playing, still in incognito. The lid-shut keys work. | Stops, closes, Home, incognito off. Nothing plays, so the handheld sleeps. | Quits; the handheld sleeps. |

- **Close all and Quit win over Keep playing.** In incognito the lid is the way to put everything away; that matters
  more than the music going on. Someone who wants music in a pocket while in incognito sets Nothing.
- After Close all nothing plays, so the player stops holding off BananaOS’s lid sleep and the handheld sleeps as
  BananaOS says.

### Opening the lid again

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
| On, nobody signed in | No question. The dashboard goes off. Note: “Incognito on” over “Web dashboard off · Closing the lid closes all”. |
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
- By the lid (Close all): the dashboard is on again, no note.
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

### What the keys do on the player today (lid open)

| Key | Today |
|---|---|
| L / R | Previous / next file in the list. Previous, 5 s or more in: back to the start of this one. |
| L2 / R2 | Previous / next chapter. L2, more than 5 s into a chapter: back to its start. R2 on the last chapter stops there. A file with no chapters: nothing, and the note “No chapters in this file”. |
| Volume + / − | The handheld’s volume, one step a press. BananaOS moves it at the press; past 100% the player adds its own 5% a press up to Max volume. While a browser plays: that browser’s volume. |
| Power | BananaOS’s: sleep. |

Today nothing is different with the lid shut: every key acts as with it open, on whatever page was left showing.

### Option A (the owner’s proposal)

- A setting: the shoulder keys with the lid shut are **Ignored** or **As usual** (the table above).
- Volume + / −, a press: volume, as today.
- Volume + / −, a long press: next / previous.

What it leaves open: the keys under the lid, how long a long press is, what the volume does when a long press was
meant, a key held down by a pocket, and how you know it worked.

### Option B (recommended)

Option A’s volume keys and setting, with the open points settled and one more value.

| Key, lid shut | Press | Hold 0.6 s |
|---|---|---|
| Volume + | Volume up one step. Stops at 100%. | Next. |
| Volume − | Volume down one step. | Previous (5 s or more in: back to the start). |
| Power | Play / pause. | 2 s: sleep. |
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
   they must not.) They are the same intents the player’s keys send, so a browser that has the sound follows them too.
3. **Under the lid is ignored.** The face keys, the D-pad, START, SELECT, F, the stick and the touch screen cannot be
   meant with the lid shut. No setting.
4. **The threshold is 0.6 s**, the redesign’s other hold (hold A to take over). A graze is shorter; a second feels
   slow when counted without looking. Sleep stays at 2 s because it costs more to undo.
5. **The volume step is taken back.** BananaOS moves the volume when the key goes down, before anyone can know a hold
   is meant. At 0.6 s the player puts the level back to what it was at the press (as it already does while a browser
   plays), then steps the track. A hold never changes the volume.
6. **One hold, one step.** No repeat. A key still down after the step does nothing more, and its volume is kept where
   it was. A key down for more than 5 s is taken as stuck and ignored until it is let go. So a pocket pressing on a
   volume key can skip one song; it cannot run the volume up or run through the list.
7. **The volume keys stop at 100% with the lid shut.** A boost set before stays; the keys do not add to it unseen.
8. **Feedback.** None for what can be heard: the song changes, the sound stops or starts. One short double tick in the
   sound output when a hold has nothing to go to (the last one, the first one), in place of the note nobody sees. No
   setting for it.
9. **Opening the lid:** the screens come back on the page left open; the first press acts. No summary of what was
   pressed.
10. **Taught at the choice.** The first-start lid question says it: “Keep playing: the sound goes on with the lid
    shut. Then Power pauses, and holding a volume key skips.” The setting’s help lists the keys on the top screen.

### Trade-offs

| | Option A | Option B |
|---|---|---|
| Presses by accident | As usual: one graze of R skips a song. Under-lid keys and a held key are open. | A graze can only move the volume one step or, on Power, pause. A held key gives one skip at most. |
| Learning it without looking | Two things to learn: volume hold, and shoulders if on. | The same two, one rule behind both: hold to skip. |
| Shoulder keys for those who want them | On or off. | Off, safe (hold), or as usual. |
| Volume when a hold is meant | Open: it would go up or down a step each time. | Taken back. |
| Loudness in a pocket | A held Volume + climbs to the top. | One step, taken back; never past 100%. |
| Cost | One setting, two values. | One setting, three values; the stuck-key rule; one sound. |

**Recommendation: B.** It is A with its gaps closed, and its default is safe: only the volume keys and Power do
anything. The extra value (Hold) is for audiobooks, where chapters matter and a graze should not lose the place.

### Open questions

1. Does BananaOS repeat the volume key while it is held, and how fast? If it steps several times before 0.6 s, the
   sound jumps for a moment before the level is put back.
2. Is the double tick worth a second sound path beside the player’s? If not: no sound at all.
3. Volume + is next and Volume − is previous. Is that the right way round for these keys as they sit on the handheld?
4. After a pause by the power key with the lid shut, how long does the handheld stay awake so that Power can play
   again? Suggested: 5 minutes, then it sleeps.
5. Should *Lid closes in incognito* also apply to sleep by the power key and to idle sleep? Drawn here: the lid only.
6. Is a video with the lid shut treated as a song (the same keys)? Drawn here: yes.
