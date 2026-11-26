# OPDS catalogs in Books: the design (GitHub #22, 2026-11-26)

Designed, then built the same day (`src/ui_catalogs.*`, `src/catalogs.*`, `src/opds.*`). The screens are in the redesign preview (`docs/redesign/src/screens.js`, the block
“Catalogs (OPDS) in Books”, fifteen screens); the spec is `docs/redesign/backlog.md` › Catalogs
(OPDS) in Books; the walk is the journey “Books: catalogs (OPDS)” in `docs/redesign/journeys.py`.

**What was decided, and why.**
- **A third tab of Books, “Catalogs”.** A catalog is where books come from, so it sits beside the Shelf they land
  on. Files › Network is for places whose files are managed and played in place; X · More hides a thing used every
  time a book is wanted.
- **Download, then read.** A book is always saved to the card first and opened from there (the readers map a file;
  a book is small). No reading from the server.
- **Saves to `<card>/Books/<catalog's name>/<Title> - <Author>.<ext>`.** `listBooks` (src/mediaindex.cpp) already
  walks sub-folders of the Books folders, so the Shelf, covers, progress, Search and the Web dashboard need nothing new.
- **Format:** chosen on the book's page only when there are several; EPUB first, then PDF, CBZ, CBR, TXT. Formats
  no reader opens are not offered; a book with only those is listed with “Can't open” and its page says why.
- **Download progress is on the page and on the cell, not a sheet**; B leaves it running; Cancel is Y.
- **“On card”** comes from a per-catalog list (entry id → path), checked against the file still being there.
- **Comics in a catalog open here** (the comic reader, as a CBZ on the Shelf does). No hand-off to Manga.
- **Manga's OPDS servers stay a separate list.** Different jobs (streaming chapters by series vs. whole files to
  the Shelf), and Books must not need the Manga extension. The form copies a server from Manga at one press, so
  an address and password are typed once. `src/opds.h` already carries `next`, `search` and the links; it needs a
  book-first `acquisition()` choice beside the comic-first one.
- **Errors** reuse what exists: the Empty state on the catalog's page (no answer, No Wi-Fi, not a catalog), an Alert
  sheet for a refused sign-in (A opens the form), the network copy's failure sheet for a download.

**For whoever builds it.**
- Calibre's content server answers with Digest authentication by default, Calibre-Web and COPS with Basic: both are
  needed. A Kavita address holds its key (`/api/opds/<key>`): seal it as Manga does.
- Never send the login to a host other than the catalog's (acquisition links can point elsewhere).
- A new area (`Ui::Catalogs`, its own header), its sheets as `Ui::Overlay`s, fetches on a worker that pushes SDL
  user events; the web deny list is untouched (downloads land in Books, not the state dir).

**The owner's answers (2026-11-26).** 1. A third tab. 2. Try Gutenberg on the Empty state's Y. 3. Two lists, with
the Copy from Manga's servers row. 4. One sub-folder per catalog, `<card>/Books/<catalog>/<Title> - <Author>.<ext>`,
on the card with more room when both have a Books folder. 5. OPDS 1.2 only. 6. No dashboard form yet. 7. One
download at a time, in the background (B leaves it running, Y cancels).

**What building it taught (2026-11-26).**
- **The store is Manga's.** `MangaServerStore::load(stateDir, "catalogs")`: the same TSV, the same sealing, its own
  key file; a Kavita address's key is sealed by the same code. Copy from Manga's servers opens Manga's sealed
  password and seals it again with the catalogs' key on Save. (The file's first line still says "manga servers".)
- **The fetcher is Manga's class, a second instance** (main, its own SDL event, started at the first request,
  `stop()` at quit). It follows redirects; curl (7.58+) drops a custom `Authorization` header when a redirect leaves
  the host, and the login is only ever attached when the URL's origin is the catalog's (`sameOrigin`). It gives no
  progress, so the book's page reads the part file's size four times a second (`tick`) against the link's
  `length` (Calibre's is the size before its metadata update: 55,844 said, 55,919 came, so the share is capped at 99%).
- **Sign-in is asked for, not sent ahead.** The first request goes bare; a 401 names Basic or Digest
  (`parseAuthChallenge` prefers Digest), the request goes again with it, and every later one to that catalog carries it
  (Digest's nc counts up; a `stale=true` gets one more try). So a Basic password never crosses plain HTTP to a server
  that wanted Digest. Digest is computed here (MD5 by OpenSSL's EVP), RFC 2617's own example is the unit test.
- **Calibre's content server (9.19)**: its search link is a feed template (`/opds/search/{searchTerms}`, type
  atom+xml), not an OpenSearch description, so both are read (`fillSearchTerms`, or `remoteOpdsSearch` for a
  description). Its entries have no `dc:issued`; Atom's `published` is the day the book was added, so no year is
  shown rather than a wrong one. Its navigation links carry no rel; the summary is xhtml content. Its fixtures are in
  `tests/fixtures/opds/`.
- **The download** goes to `<dir>/.<name>.part` (MangaFetch's part), renamed by it to `<dir>/.<name>` when whole,
  checked (a `text/html` type or an HTML start is "the catalog sent a web page"), then renamed to `<name>`. Dotted
  names are not on the Shelf (`listBooks` skips them), so nothing half-made is ever listed; part files left by a
  quit are removed when the catalogs load. "On card" is `<state>/catalogs/<id>.got` (entry id, path), checked against
  the file each time.
- **The form is a page, not an overlay**: `Ui::press` gives the overlays the keys before the keyboard, so a form
  that opens the keyboard must be a page view (as Network's and Manga's server forms are). The sheets are overlays.
- **Wiring in Books**: L / R step three tabs; the shell's B stops at a section home only on the tab itself
  (`atSectionHome`); `startOpensSettings` gives START to the form's Save.
- **Tested** on a Mac against a live Calibre 9.19 (no login): add by the form (`--press type:` sets the keyboard's
  text), Test, Save, open, a folder, a book, download, On card, Read, the Shelf; search. **Not tested**: Digest and
  Basic against a real server (only the unit vectors), Kavita, COPS, Gutenberg (it was down for maintenance),
  the handheld's exFAT card (names are made safe by rule; rename and `statvfs` on exFAT may differ), a card
  pulled during a download, the KMSDRM panels.
- **Not built**: the catalog's cover pictures (the drawn cover stands in), the summary on the top screen, More's
  Download every format and Delete from the card, a "No Wi-Fi" first line.
