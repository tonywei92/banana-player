# Books on the Web dashboard

How book reading works in a browser signed in to the handheld: the Books page, a reader in the page, one reading place
shared with the handheld, and "Open on handheld" beside it. Drawn in `preview.html` under **Web dashboard: Books**
(16 screens, ids below); the journey "Web dashboard: Books" in `journeys.py` walks them.

Today (`web/js/books.js`): a list of rows; a tap opens the book on the handheld; the page cannot read anything.

## 1. The answer in short

- **A tap reads the book here**, at the place it was last read on either device. "Open on handheld" is the second key
  of the book's sheet (⋯) and of the reader.
- **One place per book**, kept by the handheld (its own store). Whoever turned a page last set it. Nothing jumps under a
  reader: the other device's move is a note with a key.
- **The handheld does the hard part of each format** (unpacks, parses, draws); the page lays out text and shows
  pictures. No EPUB or PDF library is shipped to the browser.
- **Reading settings are the browser's own** (theme, size, font); only the place and bookmarks travel.

## 2. Screens

| id | what |
|---|---|
| `phone-books` | The shelf at 390: Upload + card, Reading now (covers with progress), kind chips, Shelf grid, ⋯ per book |
| `phone-book` | A book's sheet: who read last, **Continue here** / **Open on handheld**, the file's rows |
| `phone-books-upload` | Upload: This card (SD1) / Second card (SD2), Choose files |
| `phone-read`, `phone-read-bar` | Text reader: the page alone (with the "Picked up from the handheld" note); the bars |
| `phone-read-text` | Text sheet: Light / Sepia / Dark, size, font, spacing, margins, Pages / Scroll |
| `phone-read-contents`, `phone-read-marks` | Contents and Bookmarks, one sheet, two lists |
| `phone-read-two` | The conflict: two places, pick one |
| `phone-read-there` | After Open on handheld: where the book went, Read here instead |
| `phone-read-comic`, `phone-read-pdf` | Page reader: a comic with bars; a PDF zoomed, with "The handheld turned to page 5" |
| `phone-read-lost` | The handheld stopped answering |
| `desk-books`, `desk-read`, `desk-read-comic` | 1280 wide: shelf with cards and drop-to-upload; two-column text with Contents pinned; a two-page spread with View |

`desk` is a new drawing kind in `src/app.js` (a 1280 × 800 panel), beside `phone`.

## 3. Flows

**Read here.** Books → tap a cover → the reader opens full-page (the dashboard's header and tabs go; ‹ returns to the
shelf, and the browser's Back does the same). It asks the handheld for the book and its place and opens there. If the
place was set by another device and differs from this browser's last one: a note for 8 s, "Picked up from the handheld:
Chapter 1, 7%" with **Back to 4%**.

**Open on handheld.** ⋯ → Open on handheld (or the key in the reader's ⋯ / the desktop bar). The handheld opens the
book at the browser's current place; the page becomes `phone-read-there`, follows the handheld's place, and offers
**Read here instead**. What plays on the handheld keeps playing. Home's Continue card for a book asks the same two
(Read here / Open on handheld), as it does for a song.

**Upload.** Upload books → (two cards in: which card, once, remembered) → the system file picker. On a computer, also
drop files anywhere on the page. A book in flight is a dashed cover first on the shelf with its share sent; it becomes
the book when shelved. Non-books are refused before sending.

**Both at once.** The book is open on the handheld and in a browser. Each turns its own pages. Each is told of the
other's turn (note, 8 s, **Go there**); neither jumps. The stored place follows the last turn.

## 4. The place: rules and conflict cases

The place is `{ id | page, frac, rev, when, by }`: a text's block id (the handheld's own, `TextReader::place()`) or a
page index; the share of the book; a counter bumped on every write; the handheld's clock; who wrote it ("the handheld",
"Safari on iPhone").

1. **Open** anywhere: go to the stored place.
2. **Turn** in a browser: save 1.5 s after the last turn, and at once on `visibilitychange` / `pagehide`
   (`fetch` with `keepalive`; `sendBeacon` can't carry `X-BP`). The save names the `rev` it read (`base`).
3. **The handheld turns** while a browser has the book: the events stream says so with the new `rev`; the page shows
   the note and takes the new `rev` as its base. Its own next turn then writes cleanly: last turn wins.
4. **A browser turns** while the handheld has the book: a Note on the top screen for 6 s ("Safari on iPhone is at
   Chapter 2 · 11%"), and Contents (Y) gains a first row "Where Safari on iPhone is · 11%" while the two differ. No new key.
5. **The browser read on while the handheld was unreachable** (a text chapter is whole in the page; a comic has pages
   ahead): the place waits in the browser ("Not sent yet"). When the handheld answers:
   - its `rev` is still the base → the place is written, silently;
   - its `rev` moved but to the same page / block → nothing to do;
   - its `rev` moved elsewhere → **Two places in this book** (`phone-read-two`): the furthest first and primary. The
     one picked is written; the other becomes the bookmark "Other place" (a text) or the 8 s note "Back to page 24"
     (a PDF or comic, which have no bookmarks).
6. **Two browsers**: the same rules; a browser is just another `by`.
7. **Read from the start / Read again**: writes the start; the old place becomes "Other place".
8. **The end**: the last page marks the book finished; it leaves Reading now on both.
9. **The file moved, renamed or deleted** on the handheld while open in a browser: the place follows a move (the
   handheld already does this); a deleted book says "This book is no longer on the card." with Back to Books.
10. **Incognito**: the dashboard is off while it is on (lid-and-incognito.md), so there is no web reader to decide for.
11. **A comic with the Manga reader off**: the browser reads it all the same; the switch is about the handheld's screens.

Bookmarks (texts and EPUBs only, as on the handheld) are the handheld's `doc-bookmarks.conf`: the page lists, adds,
renames and removes them; each shows which device made it.

## 5. Keys and gestures

| | Phone | Computer |
|---|---|---|
| Next / previous | tap the right / left third, swipe | → ← , Space / Shift+Space, PageDown / PageUp, click the edges, wheel in Scroll |
| Bars | tap the middle | always shown; hidden in full screen until the pointer moves |
| Chapter | Contents | `[` `]`, Home / End (chapter's start / end) |
| Text size, theme | Text sheet | `+` `−`, `T` |
| Contents, bookmark | the list key, the bookmark key | `C`, `D` |
| Zoom (pages) | pinch to 4×, double tap (panel width and back), drag | `+` `−`, Ctrl+wheel, `W` width, `H` height, `0` 100% |
| Layout (pages) | Fit width / Fit page / Long strip | `1` one, `2` two, `S` strip |
| Pages grid | the grid key | `G` |
| Full screen | (the phone's own) | `F` |
| Leave | ‹, browser Back | Esc (a sheet first), browser Back |

In a right-to-left comic the sides swap (left third and ← are Next) and the slider runs right to left; Space is always
Next. A swipe turns the page only at 1× zoom or at the page's edge.

Two pages: the default when the window is landscape and ≥ 1000 px; the cover stands alone; a page wider than tall is
shown alone; the place kept is the first of the pair. Two text columns from 900 px of page width; the measure stops at 68
characters.

## 6. States and copy

| State | Copy |
|---|---|
| Shelf, reading the cards | "Reading the cards…" (today's) |
| Shelf, empty | today's empty state; its key is "Upload books" |
| Opening | the cover, centred, over the page's colour; no spinner under 300 ms |
| Picked up elsewhere | "Picked up from the handheld: Chapter 1, 7%" · **Back to 4%** |
| Other device turned | "The handheld turned to page 5" · **Go there** |
| Sync line in the bars | "Shared with the handheld" / "Saving…" / "Not sent yet" |
| Conflict | "Two places in this book" · "You read on here while the handheld couldn’t be reached, and the book was read on the handheld too." · **Continue** / **Go there** |
| Page not here, handheld silent | "Page 13 is still on the handheld" · "It isn’t answering: asleep, or off Wi-Fi. Wake it and this page carries on by itself. Pages already here still turn, and your place is sent when it is back." · **Try again** / **Back to page 12** |
| A page that can't be drawn | "This page couldn’t be read." · **Skip it** (the handheld's own error under it, small) |
| A book that can't be opened | "This book couldn’t be opened here." + the reason · **Open on handheld** / **Download** |
| A protected EPUB (DRM) | "This book is locked by its seller and can’t be read here." |
| Open on handheld, sent | "Open on the handheld" · "Chapter 1 · 7%, where you were here." · **Read here instead** |
| Open on handheld, no answer | "The handheld isn’t answering. Wake it and try again." (the book stays open here) |
| Upload refused | "Not a book: holiday.mov. Send it from Files." |
| Upload, card full | "Not enough room on This card (41 MB free)." · **Use the Second card** |
| First open of a reader | "Sides turn the page · middle shows the controls" (one tap, once per browser) |
| Text sheet's foot | "Kept in this browser. The handheld keeps its own text settings; only your place and bookmarks are shared." |

## 7. Decisions

| Decision | Why |
|---|---|
| A tap reads here; the handheld is in ⋯ | The browser is the device in hand; it matches "tap a song, it plays in this browser". |
| One place, last turn wins, never a jump under the reader | A page that moves by itself loses the reader's eye; a note costs one tap when wanted. |
| Ask only when both moved unseen (case 5) | It is the one case where a rule would throw reading away. |
| The furthest place is the primary choice | Reading on is the common cause; a wrong "back" costs more to undo. |
| EPUB/TXT: the handheld sends a chapter as its own blocks (JSON), the page lays them out | The block ids are the place and the bookmarks, so sync is exact; no zip/EPUB code in a no-build page; a book's own scripts and CSS never reach the origin that holds the session cookie. Cost: the publisher's styling is not kept, as on the handheld. |
| Comics: the archive's picture as it is, unpacked by the handheld | No re-encode, no CPU beyond the unpack; CBR needs libarchive, which only the handheld has. |
| PDF: the handheld draws each page (pdfium) at the width asked, as a JPEG | One renderer, so both devices show the same page; pdf.js would add about 1.5 MB to the embedded page and still fail on older Safari. Cost: handheld CPU per page; see the open question. |
| Paged text by default, Scroll offered | Pages match the handheld and make "where I am" stable; long-scroll readers get their mode. |
| Reader themes Light / Sepia / Dark, separate from the dashboard's theme | A page of text wants its own contrast; the dashboard keeps the handheld's theme. |
| Text settings per browser, not synced | A phone and the handheld's 4-inch panels need different type. |
| Bookmarks shared, with their maker named | They belong to the book; the handheld already keeps them by block id. |
| No bookmarks for PDF/comics | The handheld has none there; its list is the Pages grid. Same here. |
| Reading never takes the output or stops what plays | A book is silent; music on the handheld under a book in the browser is a real use. |
| The card choice beside Upload, asked once | Seen before the files are picked; a wrong card is the costly mistake. |
| The reader is the whole page | Tabs and the mini player's bar would take a fifth of a phone; the bar of what plays returns with the bars. |

## 8. Implementation split

### The server adds (`src/web/`, a new `api_books.cpp`)

All paths through `resolveVirtual` and the deny list; GETs under the session cookie; non-GETs with `X-BP` + Origin.

| Route | Does | Uses |
|---|---|---|
| `GET /api/library` (extend) | per book: kind, author, `place` (frac, page/pages, when, by), finished, has-cover | `Ui::Docs::placeOf`, as `docPlace` in `api_playlists.cpp` |
| `GET /api/book?p=` | title, author, kind (`text` / `pages`), chapters (title, block id, depth, share) or page count + each page's size, direction, the place with `rev`, bookmarks | `epub.*` / `textsource.*`; `PageBook::pages`, `pageSize`, `comicInfo` |
| `GET /api/book/chapter?p=&i=` | one chapter as blocks: `{id, kind, text runs (bold/italic), img}` | `textsource.*` (the same blocks the handheld wraps) |
| `GET /api/book/img?p=&id=` | an EPUB's inline picture | `epub.*` |
| `GET /api/book/page?p=&n=&w=` | a comic: the entry's bytes with its own type; a PDF: `render(n, w, …)` → JPEG. `w` snaps to 800 / 1200 / 1600 / 2048 | `PageBook::read` / `render`, `encodeJpeg` (`videoframe.h`) |
| `GET /api/book/cover?p=&w=` | the shelf's cover (or 404: the page draws one) | what the handheld's shelf decodes; cached in the state dir |
| `PUT /api/book/place` | `{p, id | page, frac, base, force?}` → `{rev}` or `409 {place}` | `WebServer::call` → the Docs place store; adds `rev`, `when`, `by` |
| `POST` / `PATCH` / `DELETE /api/book/mark` | bookmarks | `WebServer::call` → `docMarks` |
| open-on-handheld (extend the Recent open) | accepts a place; opens the book there | `Ui::openFile` + a jump |
| events (extend `/api/events`) | `book-place {p, place}` on any write, from the handheld or a browser | the existing stream |

Rules for it:
- Books are opened and drawn **on server threads**, each with its own `PageBook` / text source (a small cache: the last
  two books, closed after 2 minutes idle); only the place and bookmark stores are reached through `WebServer::call`.
- PDF drawing: one at a time (a mutex), `maxPixels` capped as the handheld's; buffers are freed after the response
  (the mmap threshold rule). Time it on the handheld with music playing before settling the widths; write the note.
- Responses carry `ETag` from the file's size + mtime + page + width and `Cache-Control: private, max-age` so a
  back-turn costs nothing.
- The handheld's place store gains `rev`, `when`, `by` per document; its readers write through the same function and
  raise the event. A reader that is open ignores foreign writes for its own position (rule 3–4) and shows the Note.
- A page request counts as dashboard use for the idle sleep (it already does: anything but `/api/events` and `/api/ping`).

### The page does (`web/js/`, no build)

- `books.js`: the shelf (Reading now, chips, sort, covers with a drawn stand-in, ⋯ sheet / right-click menu), upload
  with the card choice and drop target, ghost covers from the upload queue.
- `reader-text.js`: lays the blocks out with CSS multi-column pagination (one column on a phone, two from 900 px) or
  as a scroll; finds the block at the top of the page for the place; themes and type from `localStorage`; Contents and
  Bookmarks; the slider.
- `reader-pages.js`: one / two / strip layouts from the page sizes (no jump as pictures arrive), fit modes, pinch and
  wheel zoom, direction, the grid; fetches the next two pages ahead and keeps the last few; asks a sharper PDF page when zoomed.
- `bookplace.js`: the place: read on open, debounced save with `base`, the offline queue, the events, the notes, the
  Two places sheet. One module for both readers.
- Routing: `#/books/read?p=…` so the browser's Back leaves the reader and a reload returns to the page.

### Order to build

1. Server: `/api/book`, `/page`, `/place` + events; page: comics and PDFs with the place (the most asked, the least layout).
2. Text: `/chapter`, `reader-text.js`, the Text sheet, bookmarks.
3. The shelf's covers and Reading now, upload ghosts, the desktop spread and columns.
4. The handheld's side of "both at once" (the Note and the Contents row), Open on handheld with a place.

## 9. Open questions

1. **PDF: drawn by the handheld, or pdf.js in the browser?** Proposed: the handheld (above). If a page takes over about
   a second on the RK3568 while music plays, pdf.js becomes the better trade despite its size. Needs one timing run.
2. **Should a browser reading keep the handheld awake?** Proposed: only as today (each request counts as use), so a slow
   reader of one long chapter can find the handheld asleep at the next (`phone-read-lost`). The other way is a ping
   while a reader is open, at the battery's cost.
3. **The handheld's Note when a browser turns a page of the book it has open** (rule 4): wanted, or is the Contents row enough?
4. **Publisher styling for EPUBs** is dropped (as on the handheld). Fine for novels; a heavily laid-out EPUB reads
   plainly. Accept, or later offer "Download" as the answer for those?
5. **Manga sources' downloaded chapters** (not files on the shelf) are out of this design. Add them to the page reader later?
