# Lyrics

Later work; the design is kept here so it can be built in one go. Drawn in preview.html (Player): **Lyrics: the
sheet** (`lyrics`), **on the top screen** (`lyrics-on`), **the timing nudge** (`lyrics-timing`), **asking before
going online** (`lyrics-ask`), **looking** (`lyrics-searching`), **plain text only** (`lyrics-plain`), **none found**
(`lyrics-none`), **no Wi-Fi** (`lyrics-offline`), **online lookup is off** (`lyrics-off`), **choosing among results**
(`lyrics-results`), **editing the search** (`lyrics-query`), **More (X)** (`lyrics-more`); and the dashboard's page
(`phone-lyrics`). Every piece is an existing component: Bottom sheet, List row, Switch, Confirm sheet, Busy sheet,
Empty state, More sheet, Keyboard, Preview pane, Note.

## Where it is opened (not final)

Drawn: a **Lyrics** row in a song's More (X or SELECT), which works by keys and touch today. Options for a key:

| Option | For | Against |
|---|---|---|
| A. More row only (drawn) | costs nothing; no key moves | two presses; not discoverable |
| B. Takes the Sleep key's place later (Sleep back to a More tile) | lyrics are used far more often than a timer | the timer's key moves a second time |
| C. Takes the **Speed** chip's place on a song (Speed stays in More; a sixth chip does not fit) | chips are "what you set"; lit while lyrics show | Speed on a song loses its face |

Recommended when it ships: **B** if lyrics prove popular, else **A**. Whichever holds it shows the state: ACCENT ink
and the dot while the top screen shows lyrics.

## Sources, in order

1. A file beside the song with the song's name: `.lrc` (timed), then `.txt` (plain).
2. The song's own tags (timed, then plain).
3. What was found or chosen for it before (the state folder, by the song's path).
4. **lrclib.net**: the exact match (title, artist, album, length within 2 s), then a search by title and artist; the
   best result within 5 s of the song's length.
5. **Your Lyrica server**, if Settings names one (empty by default).

The first that has any wins; within a source timed beats plain. 4 and 5 only with **Look for lyrics online** on.
Each online source gets **5 s**, then is skipped. No other sites, nothing scraped. "None found" is remembered for a
week. In incognito nothing found is kept.

## Where lyrics show

- **Top screen** (`lyrics-on`): a strip (art 56, title 19, artist · album 13.5), then the lines, left-aligned, 32
  in: the line before (20, DIM), the line sung now (31, semibold, ACCENT, two lines at most), the next four (20,
  DIM_ON), the last fading into the ground. Lines step up together in 200 ms. It replaces the art and the visualizer
  while the song has lyrics; the visualizer is not drawn under it. A gap of 8 s or more shows "· · ·".
- **Plain** (`lyrics-plain`): the same strip, the text at 20 as a page, "Not timed" top right; it moves so its end
  arrives with the song's; UP / DOWN in the sheet scroll it by hand (which stops the even pace for that song).
- **Bottom screen** (`lyrics`): the sheet, 410 tall: header (source, Timing chip, Top screen switch), then a row per
  line, 46 tall, with its time; the line sung now in ACCENT with the play mark. It follows the song; after a move by
  hand it follows again 5 s later.
- **Top screen switch:** a lasting setting, turned on the first time the sheet opens. On: every song with lyrics
  shows them, and a song without is looked up while it plays (online lookup on). A song with none shows what
  Display says; the top screen never shows an empty lyrics page.

## States

| State | Screen | Sheet shows | Keys |
|---|---|---|---|
| First time, nothing local, setting never answered | `lyrics-ask` | Confirm: "Look for lyrics online?" | B Not now · A Look online |
| Looking | `lyrics-searching` | Busy: "Looking for lyrics" | B Cancel |
| Found, timed | `lyrics` | the lines with times | below |
| Found, plain only | `lyrics-plain` | the text; Timing "none" at 45% | UP / DOWN scroll, Y, X, B |
| None found / instrumental / no answer in 5 s | `lyrics-none` | Empty state | A Edit search… · Y Search again |
| No Wi-Fi | `lyrics-offline` | Empty state | A Try again · Y Edit search… |
| Online lookup off | `lyrics-off` | Empty state | A Look online… · Y How to add |
| Wrong match | `lyrics-more` › `lyrics-results` | every result; the top screen previews | A Use these · Y Edit search |
| Manual query | `lyrics-query` | the Keyboard, one field | START searches |

## Keys and touches (the sheet, timed)

| Key / touch | Does |
|---|---|
| UP / DOWN | move over the lines |
| A, or a tap on a line | play from that line |
| LEFT / RIGHT, or a tap on the Timing chip's left / right half | lyrics 0.1 s earlier / later (held: eight a second), −9.9 to +9.9 s |
| Y, or the switch | lyrics on the top screen on / off |
| X | More: Search again · Edit search… · Save beside the song · Reset timing · Remove these lyrics · Look for lyrics online (switch) |
| B, the veil | close (the top screen keeps the lyrics) |

## Timing, saving, removing

- **Timing** (`lyrics-timing`): kept per song with what was found. Not 0.0 s = away from normal: the chip in ACCENT,
  "Timing +0.3 s" top right on the top screen while the sheet is open.
- **Save beside the song:** writes `<song's name>.lrc` (timed, the timing as `[offset:]` in ms) or `.txt` (plain),
  UTF-8, in the song's folder. Only on that press. Dimmed with its reason: "A network folder", "The card is
  read-only". For a `.lrc` already beside the song a changed timing is written to its `[offset:]` when the sheet
  closes.
- **Remove these lyrics:** found ones are forgotten (that result is not picked again); a file beside the song is
  deleted after a second press.

## Privacy and settings

- Settings › Music & sound › **Look for lyrics online**: Off / On. Unset until the question (`lyrics-ask`) is
  answered; either answer sets it. Help: "A song with no lyrics of its own is looked up at lrclib.net. Its title,
  artist, album and length are sent, nothing else. Off: only a .lrc beside the song and the song's own tags are
  used."
- Under it, **Lyrics server (Lyrica)**: an address, empty by default. Help: "The address of a Lyrica server of your
  own, asked when lrclib.net has nothing. Empty: not used."
- A lookup happens only for a song whose lyrics are being shown (the sheet open, or Top screen on). Never for a
  whole library, never in the background for songs not playing.

## The Web dashboard (`phone-lyrics`)

Now playing gets a **Lyrics** key in its row (Up next, Playlist, Lyrics, Shuffle). The page: what the handheld has
now and from where; a box to **paste** lyrics; **Choose a .lrc or .txt file** (its text fills the box); **Save beside
the song**; Download .lrc; Remove. `[mm:ss.xx]` times make it timed, none plain. Saved as `<song's name>.lrc` /
`.txt` in the song's folder (asks before replacing; a network folder or read-only card: kept on the handheld
instead, and it says so). The handheld re-reads at once: the top screen shows them with the note "Lyrics from your
phone". One POST with the usual guards (cookie, X-BP, Origin, `resolveVirtual`, the deny list), text only, 256 KB.

## Copy

"Lyrics" · "Choose lyrics" · "Timing" · "Top screen" · "Not timed" · "Look for lyrics online?" / "This song has no
lyrics of its own. Banana Player can look at lrclib.net: the song's title, artist, album and length are sent, nothing
else." / "Not now" / "Look online" · "Looking for lyrics" · "No lyrics found" · "No lyrics with this song" · "There
is no .lrc beside it and none in its tags. Wi-Fi is off, so nothing was looked up." / "… Looking online is off." ·
"No words in this one" / "lrclib.net lists it as instrumental." · "lrclib.net did not answer." · "Search again" ·
"Edit search…" · "Use these" · "In use" / "Timed" / "Plain" · "Save beside the song" / "Saved" · "Reset timing" ·
"Remove these lyrics" · notes: "Lyrics saved", "Lyrics from your phone".

## Decisions

- **Top screen for reading, bottom for doing.** The top panel has no touch and is the one you look at; the list
  below is the one you tap.
- **Lyrics replace the visualizer, not sit over it.** One thing moves on a panel; words over bars are unreadable.
- **Ask once before the first lookup; default off until answered.** It sends what you listen to; that is yours to
  allow.
- **Look up only what is being shown.** Fewer requests, and nothing leaves for songs you never opened lyrics on.
- **Local always wins.** A `.lrc` you placed is never overridden by a server's.
- **Saving is a press, never automatic.** The player does not write into a music folder unasked.
- **Timing by two keys, in tenths, while listening.** The ear does the measuring; no page, no number entry.
- **Plain lyrics are shown, paced evenly.** Better than nothing, and honest: "Not timed", no line lit.
- **Results list on demand only.** The best fit shows at once; the list is behind Search again for a wrong match.
- **The phone is the way to bring your own.** Pasting on a handheld keyboard is not a thing anyone should do.

## Not verified

- lrclib.net's and Lyrica's request and answer shapes, and their terms for a client like this: check before building.
- Fonts: lyrics in scripts the bundle's fonts lack. The top screen is libass (bundle fonts only), the sheet is
  SDL_ttf through `Ui::runs()`.
- The cost of redrawing the top screen's lines against the visualizer it replaces.

## Open for the owner

1. Where the Lyrics key goes (the table above).
2. Should Top screen: on look up every new song by itself, or only when the sheet is opened?
3. Save beside the song: keep it a press, or offer "always" as a setting?
