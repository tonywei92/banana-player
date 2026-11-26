# OPDS catalogs in Books: the design (GitHub #22, 2026-11-26)

Design only: nothing is built. The screens are in the redesign preview (`docs/redesign/src/screens.js`, the block
“Catalogs (OPDS) in Books”, fifteen screens marked NOT BUILT); the spec is `docs/redesign/backlog.md` › Catalogs
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

**Open questions for the owner.**
1. A third tab, or a row in Books' X · More? (Drawn: the tab.)
2. “Try Gutenberg” on the Empty state: is offering a named public catalog by default wanted? (Drawn: yes, there only.)
3. One list shared with Manga's servers instead of Copy from Manga? (Drawn: two lists and the copy row.)
4. The sub-folder per catalog, or straight into Books? And which card when both have a Books folder (drawn: more room)?
5. OPDS 2.0 (JSON) as well as 1.2? Kavita, Komga, Calibre and Gutenberg all give 1.2; proposed: 1.2 only at first.
6. Adding a catalog from the Web dashboard's Books page (an address is easier typed on a phone): now or later?
7. Background downloads with a queue of three, or one at a time behind a sheet (simpler to build)?
