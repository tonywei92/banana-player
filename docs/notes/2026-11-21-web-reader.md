# Reading books in the web dashboard (2026-11-21, branch c-webread)

Built from docs/redesign/web-books.md with the owner's decisions: PDF in the browser by pdf.js (vendored), an open
reader does not keep the handheld awake, a browser's turn shows on the handheld only in Contents (Y), no publisher
styling for EPUBs, no web manga.

## What is where

- **Place store** (`src/bookplace.*`, bpcore; `testBookPlace`): the document's entry in `doc-places.conf`
  (ResumeEntry: position = block id or page index, aid = 1 text / 2 pages, duration = share or page count) gains
  `rev / when / by`, written as a `#place<TAB>rev<TAB>when<TAB>by<TAB>path` line after the entry (an older build skips
  it as a comment). `mergePlace`: written when nothing is stored, when forced, or when the base is the stored rev;
  `Same` when it moved to that very place; else `Conflict` (409 with the stored place). `stampPlace`: the handheld's
  own write, always taken.
- **Handheld** (`Ui::Docs`): `docRemember` now writes only the reader's own moves (`docOwnLast`), so a browser's write
  is not overwritten on the next tick; each write raises the `book-place` event. `webPlace` (via `WebServer::call`)
  merges a browser's write and, when that book is open, sets `foreignPlace`: Contents' first row in a text
  ("Where Chrome on Mac is · 62%"), the first options row (`DP_THERE`) for a PDF/comic. Selecting it jumps there.
- **Server** (`src/web/api_books.cpp`): `GET /api/book` (outline + place + bookmarks), `/api/book/part` (a text's part
  as blocks: `{id, k, t | r:[[style,text]], img}`), `/api/book/img`, `/api/book/page` (a comic's entry as is, ETag,
  `Content-Security-Policy: sandbox`, images only), `POST /api/book/place`, `POST /api/book/mark[/remove]`.
  `/api/recent/open` takes an optional place (written, forced, before opening). Books open on server threads: a cache
  of two, closed when idle 2 min at the next look. Place and bookmarks only through `WebServer::call`. POST, not the
  spec's PUT/PATCH/DELETE: the page's api.js and the gate are form-POST throughout.
- **Page**: `web/js/bookplace.js` (debounce, base rev, offline retry, conflict hook; the view's first report is never
  written, so opening a book doesn't make the browser its last reader), `reader.js` (bars, notes, sheets, Two places,
  Open on handheld), `reader-text.js` (CSS columns, the top block is the place), `reader-pages.js` (comic `<img>`,
  PDF canvas via `import('../vendor/pdfjs/pdf.min.mjs')`). `events.js` re-dispatches `book-place` as a window event.

## Text ids are stable across opens (checked)

The spec assumes the block ids survive a reopen. They do: an EPUB's id is `chapter << 32 | block index`, the blocks
being `epubBlocks()` of that spine document, deterministic for the same file; a TXT's id is the byte offset of a
TextDoc piece (a line, or 16 KB of a long one), fixed by the file's bytes, not by the encoding shown. So the place
format is the handheld's own, unchanged. A TXT is cut into parts every 96 KB (`snap()` to the piece there); an EPUB's
part is its spine document. Ids fit a JS number exactly (< 2^53) and `JsonWriter::num` writes them exactly.

## pdf.js

pdfjs-dist 4.10.38, the **legacy** build (`pdf.min.mjs` 398 KB, `pdf.worker.min.mjs` 1.4 MB, Apache-2.0, LICENSE in
the folder), for older Safari. `.mjs` was added to the embed's and BP_WEB_DIR's types (a module worker needs a JS
type). It reads the PDF from `/api/file` (ranges). No cMaps / standard font data are bundled: a PDF that relies on
non-embedded CJK fonts may draw with the browser's fallback. The binary grows by about 1.8 MB.

## Checked (macOS, headless Chrome muted, generated EPUB / CBZ / PDF)

EPUB, CBZ and PDF open, turn and save (rev, by "Chrome on Mac"); the place written in the browser opened the book on
the handheld there (Open on handheld); the browser's later turns showed as Contents' first row on the handheld
(scripted `Y`, shot); the handheld's turns (`R`) reached the page as "The handheld turned to … · Go there". 409 /
Same / force by curl. `tools/web-smoke.sh` 215 passed.

## Not done / unsure

- Not built: upload ghosts and the card choice beside Upload, the bookmark's maker (doc-bookmarks.conf has no such
  field), "finished" leaving Reading now. (Scroll, the View sheet, zoom, spreads, the strip, the grid, the desktop
  layouts and Home's Read here came later the same day: below.)
- A PDF's page count comes from pdf.js; the handheld's own count is the same file's, but a damaged PDF may differ.
- Safari (and WebKit's CSS columns with `getBoundingClientRect` on fragmented blocks) is not checked: the place is
  the first block whose box reaches past the page's left edge.
- Handheld-only differences: none expected (no new Linux code); the server's book cache holds an EPUB's few chapters
  and a comic's archive index in RAM while a browser reads.

## The reader's extras (same day, branch c-webq)

- **Scroll** (`ScrollText` in reader-text.js): the parts in a row, at most three laid out; the next asked when the end
  is a screen away, one further off dropped with the block at the top kept where it was on the screen (an anchor
  measured before, restored in the layout effect), so nothing jumps. A switch of Pages / Scroll remounts the view at
  the place shown (`book.start`). TXT parts (96 KB) are not chapters: "Previous part", not "Previous chapter".
- **Pages** (reader-pages.js): the page sizes are learnt as pictures arrive (a comic: the next pages fetched ahead as
  `Image`s, so pairing rarely changes under the reader; a PDF: `getPage().getViewport`, cheap). Unknown: upright 7:10.
  No server change: `/api/book` has no page sizes (reading each entry's header for a 200-page CBR on the handheld would
  cost more than it saves). Spreads: the cover alone, then pairs; a wide page alone and the pairing goes on after it.
  Zoom is the drawn size times z inside an `overflow:auto` box (`margin:auto` centres a small spread and still
  scrolls a large one); pinch and Ctrl+wheel need non-passive listeners; `touch-action: pan-x pan-y` keeps the
  browser's own pinch off the page. A PDF canvas is capped at 16 Mpx (a 4× zoom of a big page), and the strip's and
  grid's canvases draw only when near the view (IntersectionObserver). The grid's comic thumbnails are the pages
  themselves, scaled (the browser's cache keeps them; no thumbnail route).
- **Desktop** (Q_DESK, 1100 px): the bars are static (the book between them), hidden only in full screen, until the
  pointer moves. Contents pinned from 1200 px. Home lists books again (it left them out when only the handheld could
  read them), with a Books filter.
- Checked (headless Chrome, muted, generated EPUB / CBZ with one wide page / PDF): the two columns and the pinned
  Contents following the chapter, `C`; Scroll to the third part and back to Pages at the same block; the comic as 1,
  2–3, 4–5, 6 (the wide one alone); zoom; the grid and a pick; View → Strip; a PDF's cover alone; the phone's one
  page and keys; Home's Continue card → Read here at the place. Not checked: Safari, a real touch pinch, the handheld.
