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
- **The visualizers are fifteen**, in this order: Peaks, Ribbon, Mirror bars, Pocket console, Horizon, Grid, Starfield,
  Strings, Helix, Sparks, Bouncy keys, Rocket, Rain check, Sleepy moon, Dance line. All are drawn by the player in the
  theme's colours. Sparks, Starfield, Rain check and Sleepy moon fill the whole panel behind the art in Art +
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
  in forms and the Book bar in the comic reader. A START cap sits over the rail's Settings cell, so no shell hint bar
  names it. The `menu` screen is removed. With the menu gone a finger needs another way to the seventh section: a drag
  scrolls the rail's cells.
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
  `videos-reading`. The choices this needed are below.

## Decisions waiting on the owner

Each was needed to draw a gap; the drawn one is the recommendation.

From the owner's Videos, Music and Playlists decisions (2026-11-19):

- **Videos is a rail section** (drawn: Home, Files, Videos, Music, Books, Extensions, Network), with three views, All
  / Folders / Not finished. A folder that is read by itself needs a place that shows what was read, and Music has
  one. Or: no section, and the Videos folders only feed Home's Recent and a shortcut row at the top of Files (less to
  build, but then the rule changes nothing you can see).
- **Seven sections, all in sight** (drawn): with seven the rail's cells are 47 tall instead of 56, so the rail never
  scrolls. Or: keep 56 and scroll the seventh out of view, as before Playlists left (a section you cannot see).
- **“SD1:/videos and SD1/videos”** is read as both cards: the main card and the second (drawn). The build today
  lists `/data/roms/videos` as its Videos root; recommended: that folder counts too, as BananaOS's own videos folder,
  or today's users lose their list on updating.
- **Which names count as Videos:** “Videos” alone, in any capitals (drawn, as the owner wrote it). Or “Video” and
  “Movies” too. Music's twin choice (“Music” or “Musics”) is still waiting below; whichever way it goes, both should
  go the same way.
- **The dashboard's tabs** still read Home, Files, Music, Playlists, Books (drawn, unchanged): recommended to follow
  the handheld later (Playlists into Music, a Videos tab), in its own pass.

From the gaps:

- **The brand mark's stripes:** ACCENT on BG (drawn: the strongest pair in every theme, crisp at 22), or ACCENT and
  TEXT as in the icon (purple and white there; in most themes two colours of the same lightness, so the bar goes flat).
- **Which folder names count as Music:** “Music” or “Musics”, any capitals, all of them if a card has several (drawn),
  or “Music” alone in any capitals (stricter, one thing to explain).
- **Tags outside Music:** read only for songs in the Music folders, so a song elsewhere is found by its file's name
  (drawn: less reading, one rule), or read for every song so Search finds any song by its title tag.
- **Y in a playlist's select mode is Move** (drawn: today's key, and the playlist's own job), or Y does nothing there
  as in Files' select mode and Move is only on the bar and in More.
- **Liked's order:** newest first, and it can be reordered like a playlist (drawn), or fixed newest first.
- **An update that is ready while something plays:** the Restart question waits for a pause or the file's end, with a
  Note meanwhile (drawn), or asks at once.

## Still only listed

More (X) on a playlist and its items, Liked, a song in Files, an album, an artist, a search result, a connection,
select mode and a book on the shelf (lists in `keys`); Manga's chapter selection and a source's filters; the phone's
Music, Playlists and Books tabs as phone pages.
