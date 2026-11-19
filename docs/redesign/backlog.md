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
- **Music is its own rail section**, as drawn.
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

## Decisions waiting on the owner

Each was needed to draw a gap; the drawn one is the recommendation.

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
