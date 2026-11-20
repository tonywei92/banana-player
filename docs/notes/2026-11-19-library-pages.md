# The rail's Music, Videos and Books sections (2026-11-19)

Branch `pages-music`. The three library sections as real shell pages (docs/redesign/src/screens.js: music-albums,
music-artists, music-album, music-artist, music-empty, music-reading, videos, videos-empty, videos-reading, books,
books-empty; docs/redesign/components.md "Search and Music: the index" and "Section rail"), wired to the cards' media
index (docs/notes/2026-11-19-media-index.md) and the kit's pieces (2026-11-19-kit-pages.md, 2026-11-19-kit-more.md).
The rail becomes **Home, Files, Videos, Music, Books, Extensions, Network**, then Settings (seven cells, 45 tall as
`lay::rail` does); **Playlists leaves the rail and is Music's third view**.

## The shape

- **Three new areas** (`Ui::Area`, docs/notes/ui-areas.md): `src/ui_music.h/.cpp`, `src/ui_videos.h/.cpp`,
  `src/ui_books.h/.cpp`, each `shell->adopt(Page::…)` so its body is drawn in `lay::shell().body` (no bridge), its
  downs late as Files' are. `Page` gains `Music`, `Videos`, `Books`; `Ui::Shell::Section` gains them and drops
  `Playlists`. The areas say their own header (title, crumb, hints, whether More shows) through
  `ShellHead head(const Player&)`, as `Shell::head` asks Home for its pieces.
- **The index in**: main hands each area `&indexWorker->index()` (`MediaIndex`); nothing is read off the worker's
  thread. The areas' `signature()` holds `generation()` (a pass landing redraws them) and `ui.artSerial` (an album's
  art arriving). `evIndex` itself stays empty: the generation is pulled each frame.
- **The third view**: `Music::show()` opens `browse->openSection(TAB_PLAYLISTS, p)` — the existing playlists pages,
  untouched, drawn under Music (`Shell::section` maps `PLROOT`/`isPlDir` there). `L2/R2` steps the views from either
  side (`Ui::press` routes the shoulders to the area, and from the playlists page back into it).
- **Playing** is the play request as Files' Play all (`ui.playRequest`, `SourceKind::Folder`, the title `Album · …` /
  `Artist · …` / `Videos`), so the Session owns it as ever; a shuffle is `SessionHost::setShuffle(true)` plus a random
  first song, exactly `filesPlayAll`'s. A video's place comes from resume.tsv in `Player::load` (`start=`), so "plays
  from the place" is free.
- **The art** is the shared song-art cache: `ui.drawSongArt(albumFirstSongPath, albumName, box, 5)` — the placeholder
  keyed by the album, the decoded art over it. The album grid, the strip and the album page's head all ask it.
- **The More sheets** reuse `Ui::openMore`: Music's is Add to playlist… / Like / Show in folder; a video's adds Play
  from the start and Info; a book's is Read from the start / Info / Show in folder. "Add to playlist" for an album or
  an artist needed one new plumbing piece in `ui_playlists.*`: `openPickerEntries(entries, name, p)` — the select
  bar's own "Add to…" flow with the entries held (`pickerEdit`), and `pickerNewName` so New playlist… starts from the
  album's or artist's name ("" elsewhere: the folder's or the file's rule as before).
- **mediaindex.* (bpcore) gained** the Books shelves' own walk, the index's rules on a much smaller list:
  `isBookFolderName` ("Books", "Documents", any capitals), `bookFolders()` (the top of each card as `mediaFolders()`
  sees them), `listBooks()` (every document and comic file however deep, hidden and dotted folders out, natural by
  name), plus `mediaCards()` and `mediaFromWords(folders)` — the "From Music on both cards" / "Videos and Movies on
  this card" phrase the pages' lines and crumbs are made of. `mediaFolders()` now shares `indexCards()` with them.

## What was chosen (the brief said: keep it simple and say so)

- **Books' shelf**: the doc places (`<state>/doc-places.conf`, newest five as the Reading now strip with their
  progress) merged with the cards' Books and Documents folders, by name. A book opened from Files joins the shelf;
  a book being read is in both lists. **Kind chips** (All / Books / Comics / PDF / Text, the mock-up's L2/R2) are
  **not built**: the shelf is small and the brief's shelf is one list; the hint bar says so by not offering them.
  **Covers are the kit's drawn one** (`Kit::bookPlaceholder`, keyed by the name) — the readers' cover decoding is a
  later step. The shelf is built at the draw, when the places' front entries, the roots serial or the page's opening
  say it moved.
- **The first-read notice** (`music-reading`, `videos-reading`) shows while `generation() == 0`. The worker exposes
  no progress, so the row says "the list fills in as it goes" without a bar or a card name; it leaves when the pass
  lands (a half snapshot never shows, by the worker's own rule). The crumb says "Reading the cards".
- **The from-line** lives in the header's crumb, as the spec draws it (`musicShell`'s crumb), the 2026-11-20 fix:
  it was drawn twice (the crumb filled in later, the body line left in), and the body's copy went — the view chips
  start at the body's top (`musicHeader` no longer reserves a line), the album, artist and folder pages keep their own
  crumbs as the mock-ups have them.
- **An artist's page** doesn't fit head + albums + songs in the 390-unit body (measured: 190 + 174 + 30 + rows), so
  it is two stacked zones: the keys and their albums as one strip (▼ from the keys), then their songs as the list
  (▼ again; ▲ returns). One content scroll follows the cursor.
- **Videos' "the list is what this view shows"**: All plays all, Not finished plays those, a folder's list plays the
  folder's; the Folders view's loose files play the loose files (a mixed view would be a strange list). The kit's
  video row has no playing pill, so nothing marks the row that plays (the resume bar and the rail's Now playing key
  carry it).
- **The kit's rows cut with "…"** and take no Title marquee — the kit is the law for its own rows (Files' hand-drawn
  row marquees; noted, not fixed here).

## What was checked, and how

Desktop build (macOS) and `./build/core-test`: `core: all passed`, including the new checks in `testMediaIndex` (the
Books folders' names, the walk with a deep folder and natural order, `mediaFromWords`'s phrases on one card and both,
`mediaCards`). End to end (`.scratch/cards` as the card, `BP_BROWSE` pointed at it, a fresh `--state-dir`, `--press`):

- The index pass: `4 songs (1 a video), 2 albums, 2 artists; 2 videos; tags 4 read, 0 kept` on the second run.
- Music by keys: R,R,R from Home → the Albums grid; A → the album page (Play focused); A → plays the album
  (`file=1/3`, the player's crumb from the Session's `Album · Yellow Album` title); R,R from the player → the
  **music video as item 3 of 3**; B → back on the album's cell; R2 → Artists; A → the artist page; Down,Down,Down →
  the keys, the strip, the songs; A on song 2 → `file=2/3` from there; R2 → the browser's Playlists page
  (`browse=pls`, the rail keeping Music lit); L2 → back to Artists; L from the playlists page → Videos.
- Music by touch: a tap on the Artists chip (`tap:200:112`) steps the view; a tap on an artist row opens the page; a
  tap below the list does nothing (the ground — a first draft acted on row 0; fixed with a distinct `T_GROUND`).
- Music's More: X on the album's focused song → Like → the song in `.liked.m3u`; X on the album → Add to playlist… →
  with no playlists yet, New playlist… named "Yellow Album" from the picker's own keyboard → Done →
  `playlists/Yellow Album.m3u` holding the album's three items, the music video included.
- Videos: A → `file=1/2` (the view's list) from the place; B → back on its row; X → the More sheet (B closes); R2 →
  Folders (1 folder + 1 loose); A → the folder's own list; B, B → the views, Home. Not finished: after playing one
  video ~7 s and switching to the next, `rows=1` (the first run's 15 s test video could never be "not finished" —
  `worthResuming` keeps the last 15 s; a 60 s one shows it).
- Books: opens with the shelf (2 books); A → the text reader (`docs: open Field Notes.txt (text)`); B → back on the
  shelf, which now shows **Reading now · 1** (the place, read from the doc places); X → Read from the start → the
  reader opened from its place forgotten.
- Shots (`.scratch/shots/`, the bottom panels are the pages; the tops are the idle card): `lib-music-bottom.png`,
  `lib-album-bottom.png`, `lib-videos-bottom.png` (a row with its resume bar), `lib-books-bottom.png` (the Reading
  now strip over the shelf). The model cannot view images in this session, so the owner should look at the four
  bottoms first.

## Not checked, and what is stubbed

- The handheld and the Linux build (no Linux-only file touched). The index's timing on a real card is the media
  index note's own open question; the pages only read the snapshot.
- **Search** is the net-search agent's (L3's note stays theirs); the index's query (`mediaSearch`) is not asked by
  these pages. The dashboard's `/api/music/*` routes are not built (the index note's "next").
- **Books' covers** are all drawn ones (EPUB/PDF cover decoding is a later step); comics on the shelf open only with
  the Manga reader on, as Files' comics do. Books' kind chips and the artist page's Title marquee are left out
  (above).
- Videos' More has no Copy/Move/Rename/Delete/Sort rows: those live on the browser's clip and file-job plumbing, which
  belongs to a browser page; a later step could route them. Music's More has no Play-next (no such plumbing exists
  for a list).
- The first-read notice says nothing about a card being out or a video holding the walk (the worker's gate states
  are not exposed); the crumb says "Reading the cards" however the pass is held up.

## Files

- `src/ui_music.h/.cpp`, `src/ui_videos.h/.cpp`, `src/ui_books.h/.cpp` (banana-player): the three areas.
- `src/ui_shell.h/.cpp`: the sections, the cells, `page()`/`section()`/`head()`/`openSection()`, L3 on the library
  pages.
- `src/ui.h`: `Page::Music/Videos/Books`, the three `unique_ptr`s, `libPageName()` for test runs; `src/ui.cpp`: the
  areas made and listed, the L2/R2 routing, START opens Settings there; `src/main.cpp`: the index handed in, the
  state line for the pages (`press N: music view=…`).
- `src/ui_playlists.h/.cpp`: `openPickerEntries`, `pickerNewName`.
- `src/mediaindex.h/.cpp` (bpcore): `mediaCards`, `mediaFromWords`, `isBookFolderName`, `bookFolders`,
  `MediaBook`/`listBooks`; `tests/core_test.cpp`: their checks.
- `CMakeLists.txt`: the three files on their own `target_sources` line.
