# The dashboard's tabs (2026-11-20)

Item 5 of the status note's Next list: the web dashboard's tabs, as the spec draws them — **Home, Files, Videos,
Music (Playlists inside), Books** (`docs/redesign/src/screens.js`: the `webPage` header draws exactly those five, and
the Videos/Music notes say "Videos: a tab of its own, between Files and Music… a click plays in the browser or on the
handheld, as from Files" and "Playlists is no longer a tab of the dashboard: it is Music's third view there too").
The player itself stops being a tab — the small player and the bar are its way in — and `#/playing` stays where it
was. The spec's header also draws a Search field on every page; that is Search's own story, not this item's, so it
is not built here.

## The API (src/web/api_library.cpp, new)

One route, `/api/library`, registered beside the file routes: the index snapshot's answer for the pages, built in
one `ws.call` on the main thread (the index, the resume store and the folders' walk are the UI's own), with the
paths going out as `servedVirtual` — what the dashboard never serves is left out, never listed:

- the whole shape: the two from-lines (`mediaFromWords`), the first Music folder's virtual path (the empty Music
  page's upload button), the videos with their places and the place's own order (`at`, the store's newest-first —
  Not finished sorts by it), the album tiles (name, artist, year, count, the first song — its cover through the
  existing `/api/cover`), the artists, and the books (`listBooks(bookFolders())`).
- `?album=` / `?artist=`: that one's songs, with the sub-line built server-side (the handheld's own wording:
  "The Plantains · 2026 · 12 songs, 1 a video · 41 min").
- The `gen` number comes with it, so the pages can say "Reading the cards…" while the first walk is on.

Books open on the handheld through the route the Recent cards already use (`/api/recent/open`) — no new intent was
needed.

## The pages (web/js/videos.js, music.js, books.js, new; app.js's nav)

- **Videos**: the three views as chips with counts (`.chips`/`.chipbtn`, the app's own filter-chip look); a row
  plays with the view as the list, the tapped row first (in the browser: `playPaths` builds the list and jumps to
  `#/playing`; on the handheld: `/api/play` with the paths). A folder row opens the folder in the Files tab.
- **Music**: Albums (a grid of tiles, `.lib-grid`, the cover through `Art`), a tap opening the album's page
  (in-page state: the art, the sub-line, **Play** and **Shuffle**, the song rows); Artists, a tap opening their
  songs (the album named on each row); Playlists — the existing `Playlists` component embedded as the third view
  (its own routes, `#/playlists…`, still work; the sidebar's shortcut block still uses them). **Shuffle** is real:
  `playPaths` gained a `shuffle` (the browser sets the session's Shuffle on and starts from a random item, the
  handheld passes `shuffle: 1`), the playlist route's own behaviour moved down into it. With no Music folder the
  page says the rule and offers **Upload to Music** (a picker of the page's own, uploading into the folder the
  index named).
- **Books**: the shelf as rows, the kind's glyph, size and date; a row opens on the handheld (the Recent route's
  open — the dashboard can't read a book, and the row says so: "Opens on handheld").

The nav in app.js carries the five (plus Manga while the reader is on) in both places it draws them (the phone's
tab bar and the sidebar); `Playing`'s tab is gone from both. The old `#/playlists` route and the sidebar's playlist
block are kept. `parseRoute` knows the three new pages.

## Checked, and what isn't

Desktop build with the dashboard re-embedded; `./build/core-test`: `core: all passed` (nothing of this touched
bpcore). The server ran with `BP_WEB_FORCE=1` and listened on its port with the test card's index behind it (the
run's own line: `media index: … 5 songs, 2 albums, 2 artists; 2 videos`). What this session could not do: fetch
`/api/library` (the sandbox's `webfetch` is denied, and the headless-browser drive is the release checklist's
`tools/web-smoke.sh`) and open the pages themselves — so the API's answer and the three pages' looks wait for the
owner's phone or the screenshot audit. The API follows api_files.cpp's own handler patterns (`JsonWriter`,
`ws.call`, `servedVirtual`) throughout, and the pages are built from the Files and Home components' own row, chip
and empty-state pieces.
