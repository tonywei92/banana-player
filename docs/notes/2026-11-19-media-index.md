# 2026-11-19 The cards' media index

The index Music, Videos and Search will read (docs/redesign/components.md › "Search and Music: the index", and the
backlog's Decided entries on Music's folders, Videos' folders and a video in a Music folder). This task is the index
itself — the worker, the snapshot, the tag cache, the query — with no UI: nothing of it is on screen yet, and the
redesigned Music, Videos and Search sections will read it through the small API at the end.

## The shape of it

- **The folders** (mediaindex.h): a card's Music folders are the ones named "Music" or "Musics", any capitals, every
  such folder at the top of the card, all as one list; its Videos folders "Videos" and "Movies" the same way; plus
  BananaOS's `/data/roms/music` and `/data/roms/videos` when they are there. The cards themselves are `BP_INDEX`,
  else the browser's own `BP_BROWSE` test roots, else the handheld's `/data` and a mounted `/run/sd2` — a computer has
  no cards, so a desktop run without a `BP_*` root indexes nothing. Everything under the folders counts, however
  deep; hidden folders (a `.hidden` marker) and dotted names don't, as long as they are hidden.
- **The walk** (`walkMedia`, bpcore, driven by the worker's thread): under Music every audio file is a song and every
  video file a music video — its tags are read as a song's, and with none it goes by its file's name, in its folder's
  album, exactly as an untagged song does (the owner's rule); it is marked `video`, counts in the album's songs, and
  is never in the Videos list. Under Videos only video files are kept: a name, a folder, a size and a date, nothing
  read — the row's length comes from the durations cache (`cachedDuration`) and its place from the resume store
  (`resume.tsv`), both looked up by path when the row is drawn.
- **Tags** (`probeMediaTags`, bpprobe): libavformat opens the file with a 512 KB probesize, no `find_stream_info`, a
  2 s deadline — the way the dashboard's probe (web/probe.cpp) reads the same words. Title, artist, album artist
  (both spellings), album, track ("3/12" is 3), disc, date/year, and the header's duration. A known duration also
  goes into the shared `DurationCache`, so a song the index read never needs a duration probe again. A read cut short
  by the deadline is not kept (the card was busy, not the file's fault) and is asked again next pass. Tags are read
  only inside the Music folders — the drawn decision; a song elsewhere is Search's by its file's name alone.
- **The snapshot** (`groupMediaSnapshot`): the records' words are pooled (`MediaWords`: an album's songs share their
  artist's and album's words, folded twins too, so 50 000 songs are indices and one table, not four strings each —
  the spec's compact records). Albums are grouped by album artist + album (an empty album tag falls back to the
  folder's own name, a rip's habit); artists are the album artist when there is one, else the artist, the unknown
  artist last; an album's songs in disc, track and name order, an untracked one (a music video) after the tracks.
  Sealed when built: the dedupe map is let go, and readers hold a `shared_ptr<const MediaSnapshot>`.
- **The read API** (`MediaIndex`): `snapshot()` and `generation()` for a page's `signature()`, both one small mutex;
  `main` keeps the worker's `MediaIndex&` for the UI to be handed later. On `evIndex` (reserved as `evMpv + 16`,
  `SDL_RegisterEvents(17)`) the new snapshot is in; nothing reads it yet.
- **Search** (`mediaSearch`, bpcore, pure): one pass over the folded words — a song matches by title, artist or
  album, a video or file by its name less the extension; every query word must appear; a hit that starts with the
  query comes before one that only holds it. Case and accents never matter: `foldText` takes the Latin accents off
  ("Café" → "cafe"), drops a combining mark, and lowers Cyrillic and Greek in their own case. Books' and files'
  names are passed in by the caller (their lists live a moment; the index's own records are pre-folded).

## The tag cache: `<state>/media-index.tsv`

One line a file, appended as the walk learns it (a quit mid-pass keeps what was read), the last line for a path
wins, compacted at load when the file has grown well past its entries — durations.txt's shape:

```
.scratch/cards/Music/Album One/01 Sunrise.mp3	240688	1795101502	s	1	0	2026	30	Sunrise	The Bananas	The Bananas	Peel
.scratch/cards/Music/Peel Show.mkv	476460	1795101511	v	0	0	0	8	Peel Show	The Bananas	The Bananas	Peel
```

path, size, mtime, kind (`s` a song, `v` a music video), track, disc, year, duration (seconds), title, artist, album
artist, album. Keyed by path + size + mtime: a second start walks the card (readdir and stat — unavoidable) but
opens nothing, so the log's second line says `tags 0 read, 4 kept; 0 ms`. A path with a tab or a newline is not kept;
a word with one has it turned into a space; a torn last line loses only itself. A card pulled mid-pass keeps its
lines for when it's back (a card that's out is "set aside, not deleted"). The words inside the cache are pooled once
more; it is a pass-local object, made on the worker's thread and dropped when the pass ends, so it never doubles the
snapshot's memory between passes.

## The worker

`MediaIndexWorker` (mediaworker.h, main makes it on the heap and never destroys it, as `DurationWorker` and
`CoverWorker`): one pass at start, one when main sees the cards changed (`ui.rootsSerial()` — a card put in or taken
out; a Files › Refresh will call `refresh()` the same way). It never touches Ui or Player. It yields:

- `setBusy` from main's loop pass: busy while a file opens (`player.loading()`), a video's picture plays, or the
  Manga reader is on; music and photos never stop it. The walk's gate sleeps it out between files.
- small batches: a 2 ms rest every 16 files, so the walk never owns the card for long even with nothing playing;
- Linux: nice 15 and the best-effort I/O class's lowest level, as the durations prober's thread.
- a pass stopped by the gate (a quit) is not committed and not logged: a half snapshot never shows.

Log lines, one summary a pass and one a failure:

```
media index: 1 Music, 1 Videos folders; 4 songs (1 a video), 2 albums, 2 artists; 2 videos; tags 4 read, 0 kept; 3 ms
media index: couldn't read /run/sd2/Music
```

## What was checked, and how

- `tests/core_test.cpp`, `testMediaIndex`: the folder names with any capitals and several a card; the fold (Latin
  accents, ß → ss, combining marks, Cyrillic and Greek); `mediaFolders()` through `BP_INDEX` and `BP_BROWSE`; the
  walk on a real folder tree with a fake tag reader (hidden and dotted folders out, the kinds the player doesn't
  open out, an untagged song by its file's name in its folder's album, the music video marked); the incremental
  pass (a second walk reads nothing, a changed file is the only one read) both in memory and across runs through
  the cache file; the gate saying no; a walk from a root that starts with a dot (the player's test roots are
  relative); the cache file on its own (round trip, a rewritten file a miss, a torn line tolerated, a tab in a tag);
  the grouping into albums and artists with the unknown artist last, a tagged music video in its album after the
  tracks, `MediaIndex`'s generation; the query (starts-with first, accents, every word, videos, the callers' file
  names, the empty query).
- End to end (`.scratch/cards` as the card, `BP_BROWSE` pointed at it, `--state-dir .scratch/st`): tagged mp3s, an
  untagged mp3, a tagged mkv in Music and two videos in Videos made with ffmpeg's own test sources. First start:
  `tags 4 read`; second start: `tags 0 read, 4 kept`; with a video playing: no summary at all (the walk holds, quit
  aborts the pass); with a song playing: the pass finishes during the song (`tags 4 read, 173 ms`).

## Not checked, and next

- Nothing on the real cards: the walk's timing on the RK3568 against a full card (tens of thousands of files over
  exFAT), and the yielding under real playback — the numbers above are a Mac's five files.
- The walk's per-file stat is still paid on a second start (readdir + stat is how a change is seen at all); the
  spec's "a folder whose date has not changed is skipped" would cut the card's stat load further, if the real card
  needs it.
- The UI: Music's sections, Videos and Search read `indexWorker->index()` on `evIndex`, with the generation in their
  `signature()`; the dashboard's `/api/search?q=`, `/api/music/*` routes come with them. No `FEATURES.md` entry yet
  — nothing user-visible is on screen.
- One pre-existing quirk found while testing: `Player` classifies a *file argument* by `isAudioFile(path)`
  (player.cpp:534) and `hasExtension` rejects anything whose string starts with '.', so a relative path from a
  dotted folder (`.scratch/…01 Sunrise.mp3`) plays as "not a song" (the walk held as if a video played). On the
  handheld every path is absolute, so it can't show there; kept in mind, not fixed here.

## Files

- `src/mediaindex.h/.cpp` (bpcore): the folders, the fold, the words, the records and snapshot, the grouping, the
  query, `MediaIndex`.
- `src/mediacache.h/.cpp` (bpcore): `MediaTagCache`, the state folder's `media-index.tsv`.
- `src/mediatags.h/.cpp` (bpprobe): `probeMediaTags`, the libavformat tag reader.
- `src/mediaworker.h/.cpp` (banana-player): the thread, the pass, the summary line.
- `src/main.cpp`: `evIndex` (`SDL_RegisterEvents(17)`), the worker made and started, the busy gate, the
  cards-changed refresh, `stop()`.
