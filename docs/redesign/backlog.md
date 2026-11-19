# Design backlog

What the redesign spec doesn't draw yet, and the decisions waiting on the owner. The gaps below were found by the
journeys audit (`journeys.py`, coverage.md › Journeys) and drawn on 2026-11-19; what is still only listed is at the end.

## Gaps (all drawn, 2026-11-19)

1. **Done. A playlist's inside, select mode and reorder:** `playlist` (rows with their place, the Now mark, Play /
   Shuffle / Add over them), `playlist-select` (ticks, a grip on every row, the bar: Remove, Move, Add to…),
   `playlist-move` (Y lifts the ticked rows, the D-pad carries them, A puts down; a finger drags a grip).
2. **Done. Liked's own list:** `liked` (a playlist whose rows wear their heart; no Add), `liked-empty`.
3. **Done. Update states:** `update-downloading` (progress, Hide on B), `update-ready` (Restart now / Later),
   `update-failed` (Try again, Details, and the reasons in a table).
4. **Done. More (X) sheets:** drawn for a Home card (`home-more`), a book (`reader-more`), a PDF (`pdf-more`), a comic
   while reading (`comic-more`), a song (`music-more`), a photo (`photo-more`). The rest are lists in `keys`, with the
   playlist ones in a table on `playlist`; all are one helper, `moreS()`.
5. **Done. Pages grid and a PDF's View:** `pdf-pages` (also a comic's), `pdf-view`.
6. **Done. The comic reader's View sheet:** `comic-view` (Layout: One screen / Fullscreen / Book first); SELECT opens
   it from all three layouts.
7. **Done. Music:** `files-music` (a folder of songs in Files), `music-list`, `music-more`, `music-artist`; and the
   folder rule's screens, `music-empty` and `music-reading`.
8. **Done. Small screens:** `net-empty`, `recent-all`, `manga-history`, `web-page-off`, `web-page-nowifi`.
9. **Done. The phone's dashboard pages:** `phone-home`, `phone-files`, `phone-playing`, each drawn at a phone's width
   beside what the handheld shows meanwhile.
10. **Done. Photos:** `photo-view`, `photo-more`.
11. **Done. Extensions as the rail's section** (the owner's request, below): `ext-section` (a row for each extension
    that is on, then Manage extensions), `ext-section-video` (the same while a video plays: “Stops the video” on
    Manga's row), `ext-section-empty` (none on). Redrawn for it: `manga` (a page inside Extensions, with ‹ and B back to
    the list), `manga-guard` (asked from Manga's row), `menu` (an Extensions tile), `extensions` (Manage extensions).

## Decided (owner, 2026-11-19)

- **L3 while browsing is Search** on every shell screen. Like is L3 on the player, the Like key there, and a row of
  More. `playlists` and `browser-mini` were the two screens that still said “Like what plays”; both are fixed.
- **Music is its own rail section**, as drawn (and since holds Playlists as its third view).
- **No Songs list in Music.** Search finds a song; Files lists them where they are.
- **Music's source:** Albums and Artists are filled by themselves from the Music folder at the top of each card;
  songs elsewhere stay in Files and Search. The UI says so: the line under Music's title (“From Music on both cards ·
  148 albums”), the Empty state with its how-to, the Notice row while a card is first read, and the dashboard's Music
  page. The rule in full: components.md › Search and Music: the index.
- **The brand mark is the app's icon:** a small flat clapperboard in the theme's roles, one helper (`brandMark()` in
  `src/kit.js`), on every pane that shows it and in the dashboard's header; the full icon stays on the welcome pane.
  components.md › Brand mark.
- **The visualizers are fifteen**, in this order (Faders in Pocket console's place and Lanterns in Rain check's, the
  owner, 2026-11-19; Lanterns is a band style, so three fill the panel now): Peaks, Ribbon, Mirror bars, Faders, Horizon, Grid, Starfield,
  Strings, Helix, Sparks, Bouncy keys, Rocket, Lanterns, Sleepy moon, Dance line. All are drawn by the player in the
  theme's colours. Sparks, Starfield and Sleepy moon fill the whole panel behind the art in Art +
  visualizer, with the art and its lines centred; Rocket's stars fill the panel while the rocket stays in the band.
  The Display grid is 5 × 3; the spec's lists, glyphs and colour rules follow.

- **Manga leaves the rail; the rail's cell is Extensions** (the owner: “I don't want people to accidentally tap Manga
  and stop the video”). Manga reader is a row of the Extensions page, with Manage extensions under it. Opening the
  page starts and stops nothing; only A on Manga's row can stop a video. This takes back “an extension that is on
  gets its own rail slot”. The rule: components.md › Arriving stops nothing.
- **Extensions, as drawn (owner, 2026-11-19, all six):** opening Manga while a video plays asks (`manga-guard`, Stay
  focused and on B; a song or a photo is never asked about), with “Don't ask again” offered; the cell is always in the
  rail, its page the Empty state when nothing is on (`ext-section-empty`); coming back returns to Manga where you left
  it, except while a video plays (then the list); the cell sits in Manga's old place, between Books and Network; Home
  keeps nothing of the drawer.

- **START's menu is dropped; START opens Settings** (the owner: “why do we need START Menu on the Home page?”). The
  menu held nothing of its own: its places are the rail's cells, About is Settings' last group, Now playing is SELECT
  and the mini bar, Quick settings is F, Search is L3 and the header's key. START opens Settings from every shell
  screen, the player and the text and PDF readers; START or B there goes back to where you were. It stays Save / Done
  in forms and the Book bar in the comic reader. The rail carries no START cap (taken off by the owner, below). The `menu` screen is removed.
  (The rail has since stopped scrolling: all seven sections show.)
- **B on Home asks to quit**, Cancel focused, as today; Home's hint bar says “B Quit” (A Resume · X More · L/R
  Section · F Quick · B Quit). Quit is also in Quick settings and on F + START.
- **Search is a key at the top of the rail** (owner, 2026-11-19; it takes back “the header's magnifier, no rail
  cell”). 68 × 40, the magnifier alone, pinned as Settings is at the foot, on every shell screen, Settings included.
  Search is a page that keeps its results until you search again; its key is lit while it shows and B goes back to
  where you were. L3 stays its key while browsing; L / R pass it by. The header has no magnifier, and the rail's L
  and R caps are gone to make the room (every shell hint bar says L/R Section).
- **On the player SELECT opens More, as X does** (owner; the build does it). Inside a chip's sheet SELECT or B
  closes, and in a song's Display sheet SELECT steps the Show mode. The hint bar says “X / SELECT More”. In
  a book, a PDF and a comic SELECT is still View (Text in a book).
- **Home while something plays: one card, no mini bar** (owner). Home's big card is Continue (Resume) or Now playing
  (Pause / Play in the same spot, the state pills after the time; a tap or A goes to the player, a hold or X is its
  More). The mini bar is hidden on Home, and only there; Home's layout is the same either way. `states-browse`.
- **The Repeat and Shuffle chips change in place** (owner): Repeat steps Off → All → One → Off, Shuffle on / off. No
  sheet and no note on the top screen; the chip's word and colour say it. Both stay at the top of List (Y).
- **No View sheet and no tabs on the player** (owner). Each chip opens its own sheet at once: Subtitles, Audio,
  Picture, Speed on a video; Sound (the Equalizer), Sound FX (Effects), Display, Speed on a song. Adjust is a page of
  Picture, behind its last row (‹ or B back to Picture). By the keys: SELECT or X opens More, whose first row is the
  same five chips under the cursor; A opens the sheet and closes More, and A on Repeat or Shuffle changes it in
  place. L and R do nothing in a sheet. `video-more`, `music-more`, `video-view` (Subtitles), `video-picture`,
  `video-adjust`, `video-speed`, `music-view` (Display), `music-eq`.
- **On a photo SELECT opens More too** (owner). `photo-view` is removed: the Fit key steps Fit / Fill in place, and
  More's first row is four chips changed in place (Shown, Keep zoom, Pan area, A drag moves). Zoom is UP / DOWN and
  Turn is L3.

- **Playlists are inside Music** (owner): Music's third view, beside Albums and Artists (L2 / R2, or its chip). The
  Playlists cell leaves the rail. A playlist that holds videos is still there, and says so on its row. Music is always
  in the rail, with or without a Music folder. `playlists`, and every playlist page keeps Music lit.
- **A video in a Music folder is part of Music** (owner): with tags it sits in its album and under its artist like a
  song, with none it goes by its file's name; a VIDEO pill on its row, counted in the album's line, played by the
  video player as one item of the album's list; not listed in Videos. `music-album`.
- **Videos get Music's folder rule** (owner): the folder named Videos at the top of each card, the main one and the
  second, any capitals, all of them if a card has several. Drawn as a rail section: `videos`, `videos-empty`,
  `videos-reading`.
- **Videos is a rail section** (owner), between Files and Music, with the views All / Folders / Not finished.
- **Seven rail cells, all in sight** (owner): 45 tall with seven sections, 52 with six or fewer (since the Now playing key took a place); the rail never
  scrolls.
- **`/data/roms/videos` counts too** (owner): BananaOS's own videos folder on the main card is read with the rest.
- **Videos' folder names are “Videos” and “Movies”** (owner), any capitals, every such folder on a card.
- **The Web dashboard's tabs follow the rail** (owner): Home, Files, Videos, Music, Books. Playlists is Music's
  third view there too; Videos is the new tab. Drawn under `videos` (the Videos tab, Music › Playlists) and on the
  phone pages' tab row. Extensions, Network and Settings stay on the handheld.

- **Music's folders** (owner, 2026-11-19): “Music” and “Musics”, any capitals, every such folder at the top of each
  card, plus `/data/roms/music` when BananaOS has one; all merged, as Videos' are.

- **No START cap on the rail** (owner). START still opens Settings from every shell screen, the player and the text
  and PDF readers. It is taught where it is used: Settings' hint bar reads “B / START Back”, and the first-start
  pane's key line names it. The rail gets the room back: with seven sections the cells are 50 tall.

- **On the player, UP / DOWN are the volume** (owner: the volume keys are hard to reach with the handheld lying
  down). A video and a song alike. They do what the volume keys do: one step a press, the Level card on the top
  screen, the boost past 100% up to Max volume with its warning; the browser's volume while a browser plays. The hint
  bar says “▲/▼ Volume”. UP and DOWN did nothing on the player before (the chips take no focus; the keys reach them
  through More), so nothing moved.

- **Gundam takes the proposed values** (owner): BG #D9DFEC, BUTTON #ECEFF6, PRESSED #CBD3E4, TRACK #C6CEDF, RULE
  #B4BDD0, RAIL #172E6B, DIM #53546A, DIM_ON #43445A, ACCENT and KEY #2C52B3, a new optional ON_RAIL #C9D3EE; the
  rail's active cell is KEY_FOCUS (#FFF867) with TEXT ink and the hint bar's caps are KEY_FOCUS; the rest as first
  built. `theme-gundam` keeps the first values beside them.
- **Match album art, as drawn** (owner): the song's player, the top screen and Quick settings' block; always a dark
  result; a switch in Settings › Music & sound and at the foot of the song's Display sheet. `dyn-art`.
- **Clean is the look; Classic is not offered** (owner; it takes back “Classic is the default, Clean the option”).
  No Style row in Settings. Clean as balanced since: keys stay keys. High contrast alone is drawn Classic.
  `clean-style`.
- **Held, UP stops at 100%** (owner): the boost takes a fresh press. **UP / DOWN are the volume only on the player.**
- **No mini bar; Quick settings' Now playing block replaces it everywhere** (owner). No shell page has a bar at its
  foot; the body has the room. The block heads Quick settings on every screen, the player included, and is always
  there: idle (“Nothing playing”, keys dimmed, not pressed) when nothing is loaded. Home's Now playing card stays.
  `quick`, `quick-playing`.

- **Dark's accent is neutral** (the owner: it looked bluish, purplish). ACCENT #A9C4E6 → #E8E4DA, a warm off-white;
  nothing else of Dark changes (ON_ACCENT stays #0B0B0D). For the builders: the `dark` line of `BASES` in
  `src/theme.cpp`, its tenth colour; the top cards' title and the dashboard follow through `build()` and `themeCss`.
  The two candidates not taken (Chalk #ECEEF1, Mist #D3DAE4) are beside it on `theme-dark-accent`.

- **A playing mark on the rail** (owner), instead of only the header's bars: while something is loaded the rail has
  a Now playing key, and one tap on it opens the player. Details chosen for it are in the waiting list.

- **In Clean, the player's and the photo viewer's keys stay Classic** (owner). Covered: the transport row and the
  Like · Playlist · List · Stop row on a video and a song; Zoom −, Zoom +, Rotate, Fit, Previous, Next on a photo.
  Edges, lips, solid fills, radius 10, outlined caps, as in Classic. Also covered (owner, the same day): the
  player's setting chips (Subtitles, Audio, Picture, Speed, Repeat; Sound, Sound FX, Display, Speed, Shuffle), on its
  face and in its More. Not covered (they stay Clean): the header's Back and More, the chapter and Next lines, the
  hint bar, and the rest of the sheets opened from those screens.
  components.md › Clean style, rule 7; drawn on `clean-style`.

- **Match album art's backdrop, the owner's pipeline:** resize the art (to 160 × 120), a real Gaussian blur (40
  units at panel scale, σ 10 at the working size), the dominant colours taken from the art, a dark vertical gradient
  over the blur (the palette's BG at 66 / 60 / 80 / 90 / 94% at y 0 / 120 / 168 / 330 / 480, deepened for a bright
  art), and the result cached by the artwork's hash (`<state dir>/artbg/`, 16 MB, oldest dropped). The flat scrim is
  gone. components.md › Match album art; `dyn-art`.

- **In Clean, keys stay keys** (owner: “the border also applies to big buttons”). Every labelled key keeps its
  Classic look (edge, lip, solid fill): the player's and the photo viewer's (as decided), Home's Resume / Pause and
  its three shortcuts, an empty page's keys, every sheet's and panel's action keys (More's four tiles and Quick
  settings' Previous / Play / Next and Quit among them), the readers' key rows and the Book bar, an album's Play /
  Shuffle / Add. Clean still cleans the page: rows, cards and tiles that go somewhere or hold a value, chips, tabs,
  the rail, the header's icons, the two lines of the player, steppers, caps, pills, the hint bar. components.md ›
  Clean style, rule 7.

- **The keyboard is all keys** (as drawn): every key of it Classic, in Clean too.

- **Match album art is constrained** (owner; it takes back the palette worked out from the art): “artwork
  influences the atmosphere, not the UI identity.” The art gives one hue (median cut, then near-black, near-white,
  extreme and rare colours dropped), blended at 15% saturation into the theme's fixed charcoal; the blurred art
  (Gaussian, 40 units) lies behind the player under the dark vertical gradient; accent, text, keys, chips and
  controls stay the theme's; a neutral dark gradient when the art gives nothing, cannot be protected, or its hue is
  opposite the accent's. Cached by the art's hash as before. components.md › Match album art; `dyn-art`.

- **Settings has no Extensions group and no Manga group** (owner; as built). An extension's settings live with the
  extension: Manage extensions has one door, its row on the Extensions page; Manga settings is a page of Manga (X ·
  More there, or X on its row in Manage extensions). Settings is ten groups: Playback, Subtitles, Music & sound,
  Photos, Reading, Files & history, Power & screens, Web dashboard, Updates, About.

- **The status cluster's taps** (as built): the globe opens the Web dashboard page; the rest of the cluster opens
  Quick settings.
- **No playing bars in the header on shell pages** (owner; built): the rail's Now playing key carries them. The
  headers of the player, the readers and the photo viewer, which have no rail, keep theirs.

- **Match album art in a light theme: both screens** (owner; it takes back “the top screen only”). Both
  screens get a light version (a song's top screen is on the theme's ground too): the blurred art lifted onto the theme's light
  ground (the hue at 22% saturation and 86% lightness in the theme's paper, a light vertical gradient, whitened for
  a dark art until TEXT, DIM_ON and ACCENT hold 4.5); its fallback is the theme's plain ground. The theme's keys,
  accent and text are untouched. themes.py checks the limit in every theme.

- **Tags outside Music** (owner): tags are read only for songs in the Music folders, as drawn; a song elsewhere is
  found by its file's name.

- **Y in a playlist's select mode is Move** (owner, as drawn).
- **Liked is newest first and can be reordered like a playlist** (owner, as drawn).
- **An update ready while something plays waits** for a pause or the file's end, with a Note meanwhile (owner, as
  drawn).
- **Visualizers: Faders and Lanterns only** from the ten ideas, for now (owner); the fifteen stay as listed above.

- **The rail's active section has a bar at the panel's edge** (owner; built): 3 units wide at x 0, the cell's
  height less 8 a side, right corners rounded 1, in the accent (KEY_FOCUS in a strong theme), in Classic and Clean,
  alongside the cell's tint.

- **One face: IBM Plex Sans for everything** (owner; it takes back Pixelify Sans + Inter). No pixel face in the
  interface. Weights by role (600 titles, key labels, tile names, what is selected; 500 names, notes, hints; 700
  pills and badges), smallest size 11, digits of one width as drawn. Sizes stay, except a page's title 25 → 23, a
  card's 22 → 20, a sheet's 20 → 19. components.md › Type; `type-options` keeps the eight pairs that were compared.

## Decisions waiting on the owner

Each was needed to draw a gap; the drawn one is the recommendation.

- **The rail's Now playing key: what had to be chosen.** Drawn: it sits above Settings (near the thumb, where the
  mini bar was), 68 × 44, the art at 30 with the three playing bars beside it (a pause mark when paused), no label;
  its place is kept empty when nothing plays, so the rail's cells never change height (45 tall with seven sections,
  52 with fewer; Settings 50). A tap opens the player; a hold opens the More of what plays (Stop, Like, Add…), as the
  mini bar's hold did. L / R pass it by; SELECT is its key. The header's status cluster still opens Quick settings,
  and the header keeps its playing bars only where there is no rail (decided since: none on shell pages). Or: under Search at the top; or a hold
  that opens Quick settings; or no kept place (the cells would jump when playback starts).

From the gaps:

- **The brand mark's stripes:** ACCENT on BG (drawn: the strongest pair in every theme, crisp at 22), or ACCENT and
  TEXT as in the icon (purple and white there; in most themes two colours of the same lightness, so the bar goes flat).

## Spacing audit (2026-11-19)

The scale and the rules are in components.md › Spacing. Found, worst first, and what was done in the spec:

1. **No scale.** Fourteen different gaps (3 to 18) across the kit and the screens. Now 2 / 4 / 6 / 8 / 12 / 16 / 24; the
   kit's own values are fixed (a key's icon gap 7 → 8, a row's 11 → 12, hints 15 → 16 apart, a tab's padding 13 → 12).
2. **Chips under 44.** Every view and filter chip was 40 tall (Home's Recent, Music, Videos, Books, Search, Manga).
   Now 44, and their row 44.
3. **Three gutters.** The header's More key ended 10 from the edge, the body 12, sheets 14, the player 16. Now the
   shell is 12 everywhere and sheets match the player's 16.
4. **The header sat on the body** (2 apart: Home's card touched the header's keys). Now 6.
5. **The mini bar** had 4 above it and 6 under it. Now 8 and 8.
6. **Home's blocks** were 10 / 8 / 10 apart. Now 12 before the Recent heading, 8 after it, and the shortcuts 8
   under the strip.
7. **The player's rows** were 8, 10 and 8 apart, with 2 over the hint bar. Now 8, 8, 8 and 4.
8. **Search's kind chips** ran 32 past the column with six kinds. Their padding is 8 there; the rule: a chip row that
   does not fit at 12 uses 8, and past that scrolls with the cursor.
9. **Playlists' rows** were drawn 42 tall to fit over the mini bar. They are 46.
10. **Drawn under 44, pressed larger** (kept, and now written down): the rail's Search key (68 × 40), the mini bar's
    mark (56 × 42), the player's line (40).

Not changed, only noted: inline one-off gaps inside individual drawings (a 5 or a 9 between a label and its value)
are still in `src/screens.js`; they follow the scale as each screen is next touched. `preview.html?audit` lists what
overflows its box and what is under 44 (read it with headless Chrome's `--dump-dom`).

For the builders (the build's screenshots of 2026-11-19 against this):

- `lay::shell`: the body starts 6 under the header (it is 2: “12 from its sides, 2 under the header”); with the mini
  bar the body ends 8 above it. `MINI_FOOT` 36 → 38.
- Filter and view chips 44 tall (Home's Recent filters draw about 38).
- Home (`ui_home`): the card to the Recent filters is about 22 and the filters to the strip about 14; they are 12 and
  8. The strip is followed by the shortcuts row 8 under it; with the mini bar gone from Home there is room.
- The header: the status cluster's items and the Search / More keys are 8 apart; in Settings the “L2 About ·
  Subtitles R2” pair runs into the clock (no gap): 8 before the clock, or drop the neighbours' names when they don't
  fit. `lay::statusRow` already takes a gap of 8.
- The player (`lay::playerLayout`): `keys`, `chip` and `next` 8 apart and `next` 4 over the hints (the Next line
  sits about 2 from the hint bar); the title to the bar has about 45 of air where the line (40) belongs.
- Quick settings stays a centred panel (the owner; components.md › Quick settings panel) but takes the kit's rows,
  tiles and hint bar in place of its own hint style (“UP/DOWN Choose”); Quit is a Confirm sheet.
- The rail: Search's key at the top (touch 78 × 48), cells `RAIL_CELL_H` 56, or 50 with seven sections; no caps
  at all (`RAIL_CAP_H` and the capStart row go).

## Still only listed

More (X) on a playlist and its items, Liked, a song in Files, an album, an artist, a search result, a connection,
select mode and a book on the shelf (lists in `keys`); Manga's chapter selection and a source's filters; the phone's
Videos, Music (with Playlists) and Books tabs as phone pages.
