# Manga reader: design proposal (not built yet)

Status: **proposal for the owner's approval** (2026-11-10, branch `manga-reader`). Nothing here is implemented. The mock
of the screens is a separate `mock.html` handed over with this proposal (not in the repo): both panels of each screen,
captioned with the buttons.

An optional extension built into Banana Player: a comic and manga reader in the spirit of Mihon (library, sources,
source repositories, chapters, reading progress and history, reading modes), shaped for two 4:3 panels, the buttons and
the RK3568. **Off by default.** Off means it isn't in the Extensions drawer, no Settings rows beyond its switch, no threads, no
Lua state, no network, no files written; the extension's code is in the binary but nothing of it is constructed (as
`WebServer` while the dashboard is off).

## 1. Decisions in this proposal (each can be overruled)

1. **Where it lives (owner's decision):** Home's fourth tile is always **Extensions** (Files · Playlists · Network ·
   Extensions; L/R between sections unchanged). It opens an **app drawer**: a grid of the extensions that are on, each
   an icon tile with its name (v1: only **Manga reader**, book glyph `G_BOOK`, new); A opens one. With none on, the
   drawer says "No extensions are on" and "START: Manage extensions". The tile's second line says how many are on
   ("1 on"). START in the drawer opens **Manage extensions** (section 9). The Manga reader is an app opened from the
   drawer: its own header (no section markers), its four pages on L/R, B at a page's top back to the drawer.
2. **Names, to avoid two kinds of "extension":** the user-facing words are **Sources** (what you read from) and
   **Repositories** (lists of sources you add by URL). The scripts are "sources" everywhere in the UI; only this doc
   calls them source scripts.
3. **The reader has its own page renderer on the top panel** (owner's decision): GL textures drawn in the existing top
   window and context while the reader is open; mpv isn't used for pages. It reuses `PhotoView`'s zoom and fit
   geometry (`photo.cpp`, bpcore) and scrolls like a phone: Vertical and Webtoon are one continuous strip that follows
   the finger 1:1 and glides on with inertia; Right to left and Left to right are a horizontal swipe that snaps to a
   page. **Music keeps playing** while reading (mpv plays it; the top panel shows the reader instead of the
   visualizer; the mini bar and Now playing still control it). Section 7.
4. **No sources and no repositories ship.** Built-in sources are only the user's own: files on the card, network folders,
   Komga / Kavita / OPDS servers. Repositories are typed in by the user.
5. **Reading is local to the handheld in v1.** The web dashboard manages repositories, sources and the library; reading in
   the browser is later.

## 2. User flows

- **Turn it on:** Home › Extensions (the drawer, empty: "No extensions are on") › START: Manage extensions › Manga
  reader: A turns it On (or Settings › Extensions, or the dashboard). The first time, a sheet with the neutral note about
  repositories and **Open Manga** / **Done**. Manga reader is then a tile in the drawer; it opens on Library (empty: "Add
  series from Browse, or put CBZ files or folders of pictures in a Manga folder").
- **Read what's on the card:** Manga › Browse › *On this handheld* lists series found under the Files places' `Manga`
  folders (and any folder the user opens from Files with X › *Open as manga*). A folder of images is a chapter; a folder
  of chapters (folders or CBZ/CBR/CB7) is a series. A tapped archive from Files opens straight in the reader.
- **Read from my server:** Browse › *Add server* opens the connection form (the network one, new kinds Komga, Kavita,
  OPDS) → the server's libraries and series → a series page → Resume / a chapter → reader.
- **Add a repository:** Manga › Sources › Repositories › *Add repository* → the on-screen keyboard (or paste in the web
  dashboard) → the index is fetched and checked → its sources show under *Available* with Install. Install downloads the
  script, checks its SHA-256 against the index, and adds the source to Browse.
- **Find and keep a series:** Browse › a source › Popular / Latest / Search (Y; keyboard) → a cover grid → a series page →
  *Add to library* (the heart: X on a series, or the button) → it shows in Library with an unread count.
- **Read offline:** a series page › **Download** (this chapter, the next 5 unread, all unread, or chosen chapters) →
  a queue on the app's **Downloads** page (progress, pause / resume) → each chapter saved as a CBZ under the Manga
  downloads folder on the card → read with no network; delete per chapter or series.
- **Come back:** Library's *Continue reading* strip (and a card in Home's Recent) → the chapter at the page left.
  Finishing a chapter's last page and pressing on opens the next chapter (a top-panel card says "Chapter 13 · The Night
  Market"); the last chapter ends on a "You're caught up" card.
- **Update / remove a source:** Sources › Installed shows *Update* when the repository has a newer version; Remove
  deletes the script and its preferences; series from it stay in the library marked "Source not installed".

## 3. Screens (handheld)

Layout follows today's pages: `drawHeader` (back, title, clock), rows with `drawFocus`, `drawBox` tiles and cards,
`drawHints` footer. The Extensions section's top is the drawer (`EXTDIR`, today's placeholder page). Inside the Manga
reader the five pages are tabs under the header (Library · History · Browse · Sources · Downloads), stepped with **L/R** (the reader is an app, not a section, so L/R are
free there; X stays Options, as everywhere in the browser).

| Page | Bottom panel | Top panel |
|---|---|---|
| **Extensions** (drawer) | a grid of icon tiles, one per extension that is on (`drawBox`, 14 radius, glyph + name + a second line: "3 unread"); empty: "No extensions are on" + "START: Manage extensions" | the focused extension: name, one line on what it does |
| **Manage extensions** (START in the drawer) | every extension: icon, name, one-line description, On/Off; A toggles, X or Right: its settings | the focused extension's description |


| **Library** | *Continue reading* strip (3 cards: cover, title, "Ch. 12 · p. 14/38", progress line), then a 5-wide cover grid with unread badges | the focused series: cover, title, author, status, synopsis, "12 of 48 read" |
| **History** | rows: cover thumb, series, chapter, page, when; X: remove, clear | the focused series, as above |
| **Browse** | sources list (On this handheld, network folders, servers, installed sources; language chip, last used); then Popular / Latest / Search tabs and a cover grid that loads 2 rows ahead | the focused result's details (fetched once it rests 400 ms) |
| **Sources** | Installed / Available / Repositories, stepped with Left/Right (the list only uses Up/Down; X stays Options) | the focused source: repository, version, language, what it was checked against |
| **Series** | cover, title, status line, Resume (A), In library heart, Download, chapter list (unread dot, read dimmed with a check, "p. 14/38" on the started one, a download mark: queued, progress, on the card), X: Options (mark read/unread, from here down, refresh, sort, delete download) | cover large, synopsis, genres |
| **Download** (sheet from the series page) | This chapter · Next 5 unread · All unread (37) · Choose chapters… (the list in select mode) | how much it will take, room left |
| **Downloads** | a storage line (downloads' size, the card's free space); the queue (series, chapter, pages done, pause / resume); then what's on the card by series (chapters, size), X: delete | the focused series |
| **Reader** | a touch surface showing the pages under the finger (drag and flick there; in strip modes a map of the chapter with the window on it), Back/Forward, Prev/Next chapter, Zoom −/+, Mode, Pages; a page slider "14 / 38" (drag to jump); chapter title | the pages, drawn by the reader's renderer |
| **Pages** (Y in the reader) | the chapter's thumbnails, 6 wide | the page under the cursor |

Settings gets one tile for all extensions, **Extensions** (section 9), not one per extension.

## 4. Controls

Every page keeps the browser's grammar: D-pad/stick move, A open, B back (B at the Manga reader's top goes back to the
drawer, B in the drawer goes Home), X Options, START Menu (in the drawer: Manage extensions), L/R the reader's pages
(Library · History · Browse · Sources · Downloads; inside a source, Popular · Latest · Search), touch everywhere (tap = A, the back icon = B, drag lists). The reader:

**D-pad Left/Right do all the moving** (owner's decision). One press is one step; held, it repeats.

- **Right = forward.** When the page is bigger than the screen at the current fit or zoom (Fit width, zoomed in, a tall
  page), Right first moves one screenful on through the page (down, then across for a wide one, in reading order); at the
  page's end it turns to the next page, and past a chapter's last page it shows the end card, then the next chapter.
  In Vertical and Webtoon it is one screenful down. Every step is animated (a 180 ms ease-out slide, not a jump).
- **Left = back:** a screenful up (back through the page), and at the page's top the previous page (opened at its end, so
  Left reads backwards smoothly).
- **Right is forward whatever the reading direction** by default. **Swap Left/Right** (a Manga setting, Off) makes Left
  forward, for those who want it to match a right-to-left book. Either way the page-turn slide follows the reading
  direction (right to left: the new page comes in from the left).

| Input | Paged (Right to left, Left to right) | Strip (Vertical, Webtoon) |
|---|---|---|
| **D-pad Right / Left** | forward / back by a screenful, then by a page (above) | a screenful down / up |
| **R / L** | next / previous page, straight to it (skips the rest of a tall or zoomed page) | a screenful down / up |
| **D-pad Up / Down** | pan within a zoomed or taller-than-screen page (a small step; held, smooth) | scroll smoothly |
| **Stick** | pan, any direction | scroll smoothly |
| **A** | zoom to 2× at the centre / back to the fit | fit width / 2× |
| **B** | close the reader (progress saved) to the series page | same |
| **X** | reading mode, cycled with a card on top (RTL → LTR → Vertical → Webtoon), kept for this series | same |
| **Y** | Pages (thumbnail grid) | same |
| **L2 / R2** | previous / next chapter (top card with its name) | same |
| **SELECT** | fit: Page / Width / Fill | — |
| **L3** | turn 90° (as photos; for double-page spreads printed sideways) | — |
| **START** | Menu (Playing tab becomes Reading: mode, fit, zoom steps, brightness, mark chapter read) | same |
| **Drag (bottom)** | the page follows the finger sideways, its neighbour sliding in; let go: it snaps to a page (a quick flick turns even a short drag; a slow one past half). A zoomed or oversize page pans first, then the drag carries over to the next | the strip follows the finger 1:1; a flick glides on and slows by friction; no seam between pages or into the next chapter |
| **Other touch** | slider to jump; buttons (Zoom −/+ among them); double-tap: 2× / the fit; **pinch** to zoom only if the touchscreen reports two fingers (to verify on the hardware: `BottomTouch` reads one slot today) | the same; tap the map to jump |

Zoom steps other than A's 2× are the bottom panel's Zoom −/+ buttons (Up/Down now pan). At a chapter's end, forward
shows the end card first ("End of chapter 12 · RIGHT: chapter 13"), so a held Right never skips a chapter unseen.

## 5. Sources

### 5.1 Built in (no script)

| Source | How | v1 |
|---|---|---|
| **On this handheld** | folders of images; `.cbz` (zip), `.cb7` (7z), `.cbr` (rar) through **libarchive** (BSD-2, bundled); `ComicInfo.xml` for title/number; natural sort | yes |
| **Network folders** | the same, over the existing SMB/FTP backends (`net.h`): libarchive's read/seek callbacks on a backend, on the manga thread | yes |
| **Komga** | its REST API (`/api/v1/series`, `/books`, `/books/{id}/pages/{n}`), API key or user/password | yes |
| **OPDS** (Kavita, Komga, others) | OPDS 1.2 catalogue + Page Streaming Extension (PSE) for pages; Kavita through its OPDS URL | yes |
| **Kavita native API** | progress sync, better metadata | later |
| **PDF** | no cheap renderer under a compatible licence: MuPDF is AGPL, Poppler GPL, PDFium BSD but large to build for aarch64 | later / never |

Server credentials go in `servers.conf` through `NetStore` (new kinds `komga`, `kavita`, `opds`), passwords and API keys
sealed by `secret.*`, edited with the existing connection form (`FORMDIR`). A server address is resolved at each
connect, as network folders are.

### 5.2 Source scripts (Lua, user-added)

A source is one Lua 5.4 file that returns a table. Lua 5.4 (MIT) is vendored in `third_party/lua/` and linked
statically (~250 KB). Functions run on the manga thread, one at a time per source, each with limits (5.4).

```lua
return {
  api = 1,                         -- the contract's version; a script with a newer one is refused ("needs a newer Banana Player")
  id = "example-reader",           -- unique within its repository
  name = "Example Reader", lang = "en", version = "1.2.0", nsfw = false,
  baseUrl = "https://example.org",
  headers = { ["Referer"] = "https://example.org/" },   -- optional, sent with every request and image

  popular  = function(page) return { items = { {key="/m/123", title="…", cover="https://…"} }, more = true } end,
  latest   = function(page) … end,                     -- optional
  search   = function(query, page, filters) … end,     -- filters: the values of filters() below
  filters  = function() … end,                         -- optional: the source's own categories (5.2.1)
  details  = function(manga) return { title=…, author=…, artist=…, description=…, genres={…},
                                      status="ongoing"|"completed"|"hiatus"|"cancelled"|"unknown", cover=… } end,
  chapters = function(manga) return { {key="/c/9", name="Chapter 9", number=9, date=1731200000, group="…"} } end, -- newest first
  pages    = function(chapter) return { {url="https://…/1.jpg"}, {key="p2"} } end,
  image    = function(page) return { url=…, headers={…} } end,  -- optional: resolves a page given as {key=…}
  prefs    = { {id="quality", type="select", name="Image quality", options={"High","Data saver"}, default="High"} },
}
```

`manga` and `chapter` are the tables the script returned (only `key` and `title`/`name` are required; anything else the
script put there comes back to it). Images are downloaded by C++, not Lua, with the source's `headers` plus the page's.

The API the script sees (everything else is absent):

| Name | |
|---|---|
| `bp.http.get(url, {headers=…, timeout=…})`, `bp.http.post(url, body, {headers=…, form=…})` | → `{status, body, headers, url}` (the final URL after redirects); blocking for the script, async underneath (curl multi), cancellable |
| `bp.html.parse(s)` → doc; `doc:select(css)` / `:selectFirst(css)` → elements; `el:text()`, `el:attr(n)`, `el:html()`, `el:select(css)` | **lexbor** (Apache-2.0, C, has CSS selectors) |
| `bp.json.decode(s)`, `bp.json.encode(t)` | bpcore's `json` |
| `bp.url.encode/decode/join(base, rel)/query(t)` | |
| `bp.prefs.get(id)`, `bp.prefs.set(id, v)` | strings, ≤ 64 KB per source, in `manga/prefs/<source>.json` |
| `bp.log(…)` | to `log.txt`, prefixed with the source, rate-limited |
| `bp.time()`, `bp.date(s, fmt)` | instead of `os.time`/`os.date` |
| `string`, `table`, `math`, `utf8`, `coroutine` (all), base functions but those removed below | |

### 5.2.1 Categories and filters (each source its own)

Every site sorts and narrows its catalogue differently (genres, tags, status, demographic, content rating, type,
year, sort orders), so a source declares its own filters and Banana Player draws them generically; nothing about
genres is built in. `filters()` returns a list of filter definitions; it may fetch them (a site's genre list) and is
called once per Browse visit, its result kept for the session (and the last good one on disk, for offline).

| `type` | what it is | value sent back to `search()` |
|---|---|---|
| `select` | one of a list (Status: Any / Ongoing / Completed) | the option's `id` (or its index) |
| `sort` | one of a list + ascending/descending (Popular, Latest, A–Z, Rating) | `{by = id, asc = bool}` |
| `check` | one switch (Only with chapters) | `true` / `false` |
| `multi` | several of a list (Type: Manga, Manhwa, Manhua) | list of option ids |
| `tri` | a list where each is off, **include** or **exclude** (Genres/Tags: ✓ Action, ✗ Romance) | `{include = {…}, exclude = {…}}` |
| `text` | free text (Author, Year) | string |
| `group` | a heading holding other filters (Tags › Themes, Tags › Formats) | (its children's values) |

```lua
filters = function()
  local g = json.decode(http.get(base .. "/api/genres").body)   -- the site's own list, fetched
  local opts = {}
  for _, x in ipairs(g) do opts[#opts + 1] = { id = x.slug, name = x.name } end
  return {
    { id = "sort",   type = "sort",   name = "Sort", options = { {id="popular", name="Popular"}, {id="latest", name="Latest"} }, default = { by = "popular", asc = false } },
    { id = "status", type = "select", name = "Status", options = { {id="any", name="Any"}, {id="ongoing", name="Ongoing"}, {id="completed", name="Completed"} } },
    { id = "genres", type = "tri",    name = "Genres", options = opts },
  }
end,
search = function(query, page, f)   -- f.sort, f.status, f.genres.include / .exclude …
```

- **Browse without a query** is a search with filters: `popular()` and `latest()` stay as the two quick lists; the
  **Filter** sheet (Y in Browse, a button on the dashboard) then runs `search("", page, values)`.
- **Handheld:** the sheet lists the source's filters as rows (a select or sort opens its list; `multi`/`tri` open a
  checklist where A cycles off → ✓ include → ✗ exclude; `text` opens the keyboard), with **Reset** and **Apply**; the row
  shows its current value ("Genres · ✓2 ✗1"). A long genre list is filtered by typing on the keyboard (X), as the file
  list's filter.
- **Remembered per source:** the last filter values (`<state>/manga/sources/<id>/filters.json`) and restored next time;
  a filter or option the source no longer lists is dropped quietly.
- **Built-in sources** use the same model: Komga gives its genres/tags/publishers/status from its API; OPDS gives the
  feed's facets; Local offers Sort (name, date added, last read) and Status (unread / in progress / finished).
- **Limits:** at most 50 filters and 2,000 options per source, labels clipped at 60 characters (a script can't flood the UI).
- **A series' own categories** (`details().genres`) show on its page as chips; tapping one runs that source's search with
  the matching filter set, when the source maps it (`genreFilter = "genres"` in the script).

### 5.3 Repositories

A repository is an HTTPS URL to a JSON index:

```json
{
  "format": 1,
  "name": "Example repository",
  "sources": [
    { "id": "example-reader", "name": "Example Reader", "lang": "en", "version": "1.2.0", "api": 1, "nsfw": false,
      "file": "sources/example-reader.lua", "sha256": "9f2c…", "size": 18342,
      "icon": "icons/example-reader.png", "description": "One line." }
  ]
}
```

- `file` and `icon` are relative to the index URL. Sizes: index ≤ 1 MB, script ≤ 256 KB, icon ≤ 64 KB PNG.
- **Install:** download the script, check size and SHA-256 against the index, `load(text, id, "t", env)` it in a fresh
  sandbox, check it returns a table with `api`, `id` matching the index, and the required functions; only then is it
  written to `manga/sources/<repo-hash>/<id>.lua` and listed. A source's full id is `<repo-hash>/<id>`, so two
  repositories can't collide.
- **Update:** repositories are fetched when Sources opens (at most once an hour, `If-None-Match`), and with Refresh.
  A higher `version` shows *Update*; it installs the same way, keeping prefs. Never automatic.
- **Uninstall:** script and prefs deleted, its cache entries dropped; library entries stay ("Source not installed").
  Removing a repository asks whether to remove its sources too.
- The checksum guards against a damaged or swapped file relative to its index, not against the index's author. Signing
  (a key fixed when the repository is added) is open question 4.

### 5.4 Sandbox and limits

- A fresh `lua_State` per source, created on first use, closed after 5 minutes idle. Opened libs: base, string, table,
  math, utf8, coroutine. Removed: `io`, `os`, `package`/`require`, `debug`, `load`, `loadfile`, `dofile`,
  `loadstring`, `string.dump`, `collectgarbage` (but `"count"`), `print` (→ `bp.log`). The host loads the script itself
  as text only (`luaL_loadbufferx(…, "t")`): bytecode is never loaded (it can break out of Lua's checks).
- **Memory:** a custom `lua_Alloc` caps each state at 32 MB; over it, the call fails with "Out of memory".
- **CPU:** a count hook every 10 000 instructions checks an instruction budget (≈ 200 M per call) and a wall-clock
  deadline (30 s per call, network included) and a cancel flag (the user left the page). Set on every coroutine too.
- **Network:** `http`/`https` only (`CURLOPT_PROTOCOLS_STR`, redirects too, at most 5). Only public addresses: the name
  is resolved first and the connection pinned to that address (`CURLOPT_RESOLVE`), refusing loopback, private, link-local
  and the handheld's own addresses, so a script can't reach the web dashboard or the LAN (servers are built-in sources,
  not scripts). Response caps: 8 MB for a script's request, 30 MB for an image. At most 40 requests per call, 4 at once
  per source. A cookie jar per source, in memory only.
- **Results** are copied out of Lua into plain C++ structs (strings capped, lists ≤ 2 000 entries), so nothing of a
  state outlives a call. A script error shows as one line on the page ("Example Reader: couldn't load chapters") with the
  message in `log.txt`.
- **No web view:** sites that need a browser challenge (Cloudflare and the like) won't work. Said in the Sources help.

## 6. Data and storage

All under `<state>/manga/` (the state dir is on the card), created only when the reader is first switched on.

| File | Holds |
|---|---|
| `library.tsv` | the library: source id, series key, title, cover URL, added, last opened, unread count, the series' own reading mode (or empty) |
| `series/<hash>.json` | the last details and chapter list fetched for a series (shown at once, refreshed in the background on open) |
| `progress.tsv` | per chapter: source, series key, chapter key, page, pages, read (0/1), time. Append and compact, as `resume.tsv` |
| `history.tsv` | the last 100 chapters opened (series, chapter, page, time); Keep reading history: Off clears it |
| `repos.json` | repositories: URL, name, ETag, last fetched, the last index |
| `sources.json` + `sources/<repo-hash>/<id>.lua` | installed sources: full id, repository, version, SHA-256, enabled |
| `prefs/<source>.json` | `bp.prefs` |
| `downloads.tsv`, `downloads-queue.tsv` | what is downloaded (source, series, chapter → file, size) and what is waiting |
| `cache/pages/`, `cache/covers/`, `cache/index.tsv` | downloaded images, named by a hash of the URL; least recently used dropped over the cap (pages default 300 MB, covers 50 MB, fixed) |

Library code with no window goes to bpcore and core-test: `mangastore` (library, progress, history), `mangarepo` (index
parsing, version compare, checksum), `mangacache` (LRU accounting), `comicinfo`, `opds`, `komga` (JSON → structs),
`mangasort` (natural sort, chapter numbers). The Lua host and lexbor binding are a separate library (`bpmanga`) with
its own tests (sandbox escapes, limits, a fixture source against a local HTTP server).

## 7. Threads, network, images

- **`MangaWorker`** (like `NetWorker`): one thread runs Lua calls and built-in source requests; a second thread runs a
  curl multi for images and covers (4 at once, 2 per host). Requests carry a serial; answers come back as an
  **`evManga`** SDL user event; the Ui drops answers whose serial is stale. Neither thread touches `Player`/`Ui`.
  Created when the reader is first turned on, never destroyed (as the Updater): turning it off cancels everything and
  leaves the threads asleep; a slow server never holds up quitting.
- **The page renderer (`PageRenderer`, main thread, top window's GL context).** While the reader is open the top
  panel draws the reader, not mpv's picture: textured quads (GLES 2 shaders), one per tile, placed by `PageLayout`
  (bpcore, tested: the strip's offsets, page sizes at the fit and zoom from `photo.cpp`'s math, snapping, the inertia
  curve). It draws only when something changed or an animation runs (the top panel's version of `signature()`), at the
  display's 60 fps while moving; after open and wake it redraws a few times (KMSDRM drops the first frames).
- **Music while reading:** mpv keeps playing the song. While the reader covers the top panel the song's display is
  switched to None (the tiny black picture: no visualizer cost) and mpv's frames are rendered into a small offscreen
  FBO whenever mpv reports an update, so mpv never waits on the main thread (the 200 ms stall rule). The volume,
  brightness and L2/R2 cards are drawn by the reader in the same look, since mpv's OSD isn't on screen. Leaving the
  reader restores the display. To verify on the handheld: switching to None and back without a sound gap.
- **Decoding (`PageDecoder`, 2 threads):** the bundled FFmpeg (libavcodec + libswscale) decodes JPEG, PNG, WebP (and
  AVIF if the bundle's FFmpeg has a decoder for it) to RGBA, scaled once to what the panel needs (the fit at the
  panel's real pixels; the current page again at 2× when zoomed), a few pages ahead in reading order (Preload) and one
  behind. Results are handed to the main thread as `evManga` with the buffer.
- **Textures:** uploads happen on the main thread at most one tile per frame (so a turn never stutters), tiles at most
  2048 px tall (and never over `GL_MAX_TEXTURE_SIZE`): a 20 000 px webtoon page is ten tiles. Only nearby pages stay
  resident (current −1 … +3, the strip's window ± 2 screens), under a budget (96 MB of textures, 64 MB of decoded
  buffers waiting); the farthest go first.
- **Gestures:** the bottom touchscreen's drag is fed to the reader in the top panel's pixels (the panels are the same
  size, so 1:1 with the finger); velocity from the last 80 ms of samples; a flick decays by friction (≈ 0.998 per ms,
  as phones); the strip's ends rubber-band; a paged release animates to the snapped page in 220 ms.
- **Preload:** the next pages (setting, default 3) are downloaded or extracted into the cache and decoded; on the last
  3 pages of a chapter the next chapter's first pages too, so the strip runs on without a seam. Target: a preloaded page
  shown in the next frame; a cold page under 300 ms for a 1400 × 2000 JPEG (`timing:` lines).
- **Downloads (`MangaDownloads`, on the image thread):** one chapter at a time, 2 pages at once; pages go into a
  temporary folder and are zipped (stored, not deflated: images are compressed already) into
  `<downloads folder>/<Series>/<Chapter>.cbz` with a `ComicInfo.xml` (series, number, title, the source and its
  series key), then renamed in place, so a half chapter never looks whole. The queue is kept in
  `manga/downloads-queue.tsv` and resumes after a restart. Pause/resume per item and for all; an "Only while charging"
  option. The reader prefers a downloaded chapter over the network. Downloaded files also show under On this handheld,
  linked to their series (by ComicInfo) rather than listed twice.
- **Covers on the bottom panel:** decoded and scaled to the tile size on the worker, turned into SDL textures on the main
  thread (≤ 80 kept, least recently drawn freed). Generated gradient covers (as the dashboard's `GenCover`) until the
  real one arrives, so a grid never waits.
- **Ui redraws:** the manga page state, loaded covers' generation and request states go into `signature()`.

## 8. Web dashboard (v1: manage, don't read)

- **Manage extensions** card (in the dashboard's settings): the same list and switches as the handheld; it sends the
  setting, the handheld applies it.
- A **Manga** item in the sidebar / tab bar, shown only while the reader is on, with three pages:
  **Library** (cover grid, unread counts, remove, open the series' chapter list, mark read) and **Sources**: a
  Repositories card on top (a field to paste a URL, Add; each repository's name, source count, last fetched, Refresh,
  Remove, and the neutral note) and the sources below (installed first, Install / Update / Uninstall, adult sources
  hidden unless the setting says Shown).
- API, behind the existing gate: `GET /api/manga/state`, `POST /api/manga/repo` (`{url}`), `DELETE /api/manga/repo`,
  `POST /api/manga/install|update|uninstall` (`{id}`), `GET /api/manga/library`, `DELETE /api/manga/library`,
  `GET /api/manga/cover?s=&k=` (from the cover cache only: the dashboard never makes the handheld fetch on its behalf
  beyond covers already wanted). All go through `WebServer::call`; changes are announced on SSE as a `manga` notify.
- **Downloads** (a third page): the queue with progress, pause/resume, and what's on the card by series with Delete;
  `GET /api/manga/downloads`, `POST /api/manga/download` (`{series, chapters}`), `DELETE /api/manga/download`.
- Later: reading in the browser (`/api/manga/page` serving cached pages to a page-turning view).

## 9. Turning it on and off, and its settings

**Where:** **Manage extensions** (START in Home's Extensions drawer), and the same switches and options under a new
Settings tile, **Extensions** (one tile for every extension to come, not one per extension). It lists each extension with a line on what it does and an **On/Off** switch
(**Manga reader · Off** to start). Turning one on shows its settings right under it (the Manga group below) and its
places appear at once (its tile in the drawer, the dashboard's Manga item); turning it off hides them again and stops its
threads. Nothing is deleted by turning it off: the library, history and installed sources wait for it to come back.

- **Manage extensions** rows: icon, name, one line on what it does, **On/Off**. A toggles; X ("Settings") or Right opens
  that extension's own group (the Manga group below). Off: it leaves the drawer and stops; nothing is deleted.
- **Home:** the 4th tile is always **Extensions** and opens the drawer (section 1); its second line counts what is on
  ("1 on", or "None on · START to manage").
- **Web dashboard:** a **Manage extensions** card with the same switches (it sends the setting; the handheld applies
  it). Off, the dashboard shows no Manga item at all.
- **First time on:** a one-time sheet with the neutral note about repositories and two buttons, **Open Manga** and **Done**.

### The Manga group (shown under Extensions while Manga is on)

| Row | Values | Help line |
|---|---|---|
| Manga reader | **Off** / On | Read comics and manga from your files, your servers and sources you add. Off: none of it shows or runs. |
| Reading mode | **Right to left** / Left to right / Vertical / Webtoon | How a new series opens; X in the reader changes it for that series. |
| Page fit | **Page** / Width / Fill | Width: a page as wide as the screen, read down with RIGHT a screenful at a time. |
| Swap Left/Right | **Off** / On | Off: RIGHT is always forward. On: LEFT is forward (to match a right-to-left book). |
| Preload | 2 / **3** / 6 pages | More: smoother on a slow server, more data. |
| Page cache | 100 MB / **300 MB** / 1 GB | Pages kept on the card to read again without downloading. Covers: 50 MB more. |
| Clear cache | (button: "Clear · 214 MB") | |
| Downloads folder | the Files place's `Manga` folder (or another, picked in the browser) | Where downloaded chapters go, as CBZ files you can also copy. |
| Download | **Always** / Only while charging | The handheld's only network is Wi-Fi, so there is no Wi-Fi-only switch. |
| Downloads | (line: "1.2 GB · 18.4 GB free on the card") | Separate from the page cache; delete on the Downloads page. |
| Adult sources | **Hidden** / Shown | Sources a repository marks as adult. |
| Reading history | **On** / Off | Off clears it. |
| In Recent on Home | **On** / Off | |
| Repositories (note, no value) | | Repositories are added by you. Banana Player doesn't provide or check their content. |

Each switch goes into `Settings` + `REMEMBER` in `settings.cpp`, the row ids in `ui.h`, and `settingRows()` /
`settingHelp()`, per the rules.

## 10. v1 and later

**v1:** Home's Extensions drawer, Manage extensions (and Settings › Extensions, the dashboard's card), the first-time
sheet; Library, History, Browse, Sources, Series, Reader, Pages; On this handheld
(folders, CBZ/CB7/CBR), network folders, Komga, OPDS (+ PSE; covers Kavita); Lua sources and repositories (install,
update, remove); the reader's own page renderer (paged Right to left / Left to right with swipe and snap; Vertical and Webtoon as one
continuous strip with drag and inertia), music playing on while reading; downloads (queue, CBZ on the card, offline
reading, Downloads page); progress, history, Continue reading, a Recent card on Home; page and cover cache; dashboard
pages for library, sources and repositories, downloads; FEATURES.md, a note in `docs/notes/`.

Prototype first, on the handheld: the strip renderer's smoothness (drag, flick, decode keeping up) before the rest is
built on it.

**Later:** double-page spread (two portrait pages side by side fit the 4:3 panel at the same scale as one page);
library update checks on start (once a day) with a "new chapters" badge; read-progress sync with Komga / Kavita;
repository signing; pinch zoom if the touchscreen can't report two fingers today; reading in the browser; categories;
PDF. Not planned: Book mode (owner's decision).

## 11. Risks

- **The renderer is new code on the top panel:** it shares the GL context with mpv's render API, so GL state must be
  saved and restored around mpv's draw (or mpv rendered only into its own FBO while reading); a bug there shows as a
  black or frozen top panel. The KMSDRM first-frames quirk applies to it too.
- **Decode speed on the A55s:** a big PNG or WebP page (2000 × 3000) can take 200–400 ms in software; a fast flick
  through a webtoon can outrun two decoder threads. Mitigation: decode ahead in the scroll direction, scaled straight to
  the panel's width, draw a placeholder of the page's size until it arrives (the strip's length never jumps), measure
  on the handheld before building further.
- **Texture memory and upload time:** RGBA at the Plus's 1024 px width is 4 MB per 1000 px of height; the budget
  (96 MB) and the one-tile-per-frame upload keep it bounded. The Mali-G52's texture limit (≤ 8192, unverified) is
  respected by 2048 px tiles.
- **Music under the reader:** switching the song's display to None and back must not gap the sound (the
  `lavfi-complex` rules apply); mpv must never be left waiting for a draw.
- **Gesture feel:** the touchscreen's sample rate and noise decide how good the inertia feels; tune on the hardware.
- **Downloads and the card:** a full series can be gigabytes; the Downloads page shows the size and the card's free
  space, and the queue stops (with a notice) under 500 MB free.
- **Card wear and space (cache):** the cache is written once per page and capped; it lives in the state dir. Low space: stop
  caching, read straight from the network.
- **Malicious or broken scripts:** the sandbox (5.4), public-address-only network, the checksum, results copied out and
  capped. A script can still waste its own budget or show the user anything a website can; that is the user's choice
  of repository.
- **Sites that change** break their sources; that's the repository's job to update. Banana Player's part is the
  contract (`api`) staying stable.
- **Build weight:** Lua (~250 KB), lexbor (~1.5 MB), libarchive (~800 KB without its optional codecs we don't need).

## 12. Open questions for the owner

Answered: Home's 4th tile is the Extensions drawer (section 1); Settings gets one Extensions tile (section 9); Home's
Recent shows manga cards that open at the page; the reader has its own renderer and music plays on; downloads and
Webtoon are in v1 (Webtoon after the smoothness prototype); adult sources hidden by default; SHA-256 only for v1
(signing later); Komga progress sync later; no Book mode; L/R step the app's pages and A zooms.

1. The handheld has only Wi-Fi, so the download option is "Only while charging" instead of Wi-Fi only: fine?
2. Vertical vs Webtoon, now both strips: Vertical keeps a thin gap between pages at the chosen fit; Webtoon is full width
   with no gap. Keep both, or one strip mode with a "gap" setting?
3. While reading, should the bottom panel show the mini bar for what plays (as in the browser), or only START › Playing?
