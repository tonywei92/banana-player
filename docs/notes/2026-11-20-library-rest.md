# Library pages, the rest (2026-11-20)

Item 4 of the status note's Next list, one commit. The spec: `docs/redesign/src/screens.js` (books, videos,
music-reading, videos-reading) and components.md's Cover cell and Notice row; the built state's own note is
`docs/notes/2026-11-19-library-pages.md`.

## Real covers on album tiles — already wired, nothing to build

The Music views already draw decoded art (`cell.art` → `Ui::drawSongArt`, the cover worker's `wantSong`, the
signature following `ui.artSerial`); the status note's parenthetical is the honest one — it has only been *seen* with
art-less test songs. The check belongs to a run with real art (the handheld, or a screenshot audit); the code path
is the same one Home's cards use.

## Books' covers, decoded

- `Epub::coverImage()` (bpcore, with `epubCoverFromPackage` pure beside it — tested with inline packages for both
  declarations, EPUB 3's `properties="cover-image"` and EPUB 2's `<meta name="cover">`): the package's own cover
  image, else "" and the caller falls back to the first picture.
- The cover worker gained `wantBook(key, path, w, h)` (the key carries the `"home:"` prefix, so the answer lands in
  Ui's one art cache beside the songs'): a comic's or a PDF's first page through `openPageBook`, an EPUB's own cover
  image, nothing for a TXT — the drawn sleeve stands in until the answer and for good where there is none, exactly
  as the spec words it ("until one is decoded, the drawn one stands in, as Manga does"). The crop keeps a cover's
  head, as Manga's does.
- Books' cells draw through `Ui::drawBookCover`; the page's signature follows `ui.artSerial`.

## Videos' More: Copy / Move / Rename / Delete; Sort

The four file rows call `Browse::fileOpsAct` / `filesDeleteAsk` — Files' own operations, nothing new to invent
(the clipboard's flow is still Files': open the folder it goes in, then SELECT; Delete asks on its Confirm sheet).
**Sort** is a value row stepped in place (A cycles), as the sheet's rows step chips: by **Name** (the snapshot's own
order, the spec's default), **Newest first**, **Biggest first**. The spec names only "by name" and the row itself, so
the two other orders are a choice of mine — veto if you'd rather have others (the row's value words are two string
constants). "Not finished" keeps the store's own newest-first, and a folder's own list sorts with the view.

A delete, a rename or a move done now bumps `Ui::filesSerial()`, which main watches beside the roots serial and
answers with an index re-walk — so the library pages (and Search) are true again a moment after the op, from any
page, Files' own included.

## The reading notice's bar (and the crumb's so-far)

The walk now keeps live counters (`WalkCounters` in mediaindex.h, atomics, handed to `walkMedia` by the worker):
which card (the roots' index — `walkCardName` words it as the spec does: "the card", "the second card"), folders
done of the ones seen, songs and videos read. `MediaIndexWorker::progress()` copies them for the main thread.
Music's and Videos' reading notices draw them: "Reading Music on the second card · 3 of 9 folders · 1,180 songs
read", a bar over the folders; the crumb while it runs: "From Music on both cards · 1,180 songs so far" (Videos:
videos so far). The signatures hold a quantized step (a folder, or 25 files) so the bar moves about once a second
without a redraw a file.

Two honest wording changes: the old row claimed "the list fills in as it goes", which a sealed snapshot never did
(the library-pages note said so); the row now says the list lands when it's done. Filling in row by row would need
partial snapshots — a bigger mechanism than this item, and the sealed answer is the worker's own rule.

## A playing mark on video rows

`Kit::Video` gained `playing` / `paused`: a **Playing** pill (Live tone) or **Paused** (neutral) after the name,
the same mark the Playlists rows and Music's album cells carry. The spec's own rows draw no such mark — this one is
the status note's ask.

## Checked

Desktop build; `./build/core-test`: `core: all passed` — the EPUB cover declarations (both ways, resolved against
the package's place), and the walk's counters (every folder done, the files counted, no card once it's over). The
visual checks (a book's decoded cover on the shelf, the More sheet's new rows, the notice's bar moving, the pill on
the row that plays) wait for the handheld or the screenshot audit; the log line to read on a book cover decode is
Ui's "song art: WxH for <file>".
