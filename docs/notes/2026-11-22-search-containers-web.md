# Search library: albums, artists, folders; the dashboard's Search (2026-11-22)

Branch `d-search2`. What was built and what the next reader needs.

## The query (`mediaSearch`, mediaindex.*)

`SearchHits` gained `albums`, `artists` (indices into the snapshot) and `musicFolders`, `videoFolders`
(`SearchFolder`: a path and how many songs / videos are under it, however deep).

- An artist: every word in their name (the unknown artist never). An album: rank 0 its name starts with the query,
  1 its name holds it, 2 found only with its artist's words. A song got the same three ranks (title starts, title
  holds, artist / album only): "road" no longer lists an album's other songs before a song called Night Road.
- Folders (`searchFolders`): every folder between a Music (Videos) folder and a file, matched by its own name; the
  roots themselves are never results. The records come album by album, so a file in the folder the last one was in
  only counts again (no map lookup). Sorted starts-first, then natural by name, then by path.
- Tests: `testMediaIndex`'s "the query's containers" block.

## The sections (`lay::searchSections`, kitlayout.*)

One rule for the handheld and the page (search.js copies it by hand: `searchSections`, `ALL_CAP`): the sections in
order Artists, Albums, Music folders, Songs, Video folders, Videos, Books; under All three of each and a See all row
when another section found too (four are simply shown, a lone section is whole); a kind's tab whole, headed except
Books. `found[]` in `Ui::Search` is now per section; a tab's count is its sections' sum.

## A container's page, and B back (`Search::openResult`, `comeBack`, `visiting`)

Search borrows Music's and Videos' own pages (`openAlbum`, `openArtist`, `folderAt`, `Videos::openFolder`) and
remembers the visit. Music's and Videos' B handlers ask `ui.search->comeBack(p)` first: true only while the page
shown is still that visit (so B inside an album opened from the visited artist goes to the artist, and B from a
deeper Music folder climbs first). `Shell::openSection` and `Search::open` forget the visit. The hint bar's B says
"Results" while `visiting()`.

An album's art in a result is `Kit::Result::art` (a 28 square, `lay::resultArt`); rows with art key on
`ui.artSerial`.

A fix on the way: `loadRecent` kept each line's newline, so a recent search run after a restart found nothing.

## The dashboard

`GET /api/search?q=` (api_library.cpp): session-gated like every `/api/*`, read only, the same `mediaSearch` over the
snapshot and `listBooks`; paths through `servedVirtual` (what is never served is left out and not counted), albums
and artists by index (`/api/library?album=` / `?artist=` open them); 200 a section, `totals` the real counts.

`web/js/search.js`: the page (`#/search?q=`), `SearchField` (the sidebar's on a computer, the page's own elsewhere;
CSS decides which shows), `openLib` hands a container to Music / Videos through `store.libGoto` (they take it in an
effect and clear it), `backToResults` goes one entry back when it is ours.

## Not verified

- On the handheld: the 28 art square beside Pixelify 16 at 1.6x; a cover arriving while the list glides.
- Thousands of folders: `searchFolders` runs on the main thread once per query (a pass over every path).
- Safari / WebKit: only headless Chrome (desktop and phone size) drove the page; iOS will not open the keyboard from
  the magnifier's page load without a tap on the field.
- `/api/search` walks the Books folders (`listBooks`) on every query, as `/api/library` does on every load.
- The dashboard's artist page by index: `/api/library`'s artists list skips unserved artists, so Music's own Artists
  view can be one off when a whole artist is under a folder the dashboard never serves (older; search passes the
  snapshot's index).
- `docs/artifacts/handheld-redesign/index.html` was not rebuilt; `docs/redesign/preview.html` was.
