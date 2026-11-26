<!-- TODO list for a later release (owner, 2026-11-19): not in 0.7.34. Quick wins first; Updates is the big one. -->
# Manga reader UX audit (vs Mihon)

Private report: Mihon is named here only. Audited on main at 18763b1, 2026-11-19.

**How it was checked.** The Mac build was driven with `--press` over a generated library (3 series: a folder of 6 chapter folders, 3 CBZ chapters, one single CBZ of tall pages) from a fresh state folder. Local journeys (first open, library, series, reading, leaving and resuming, chapter end, read marks, history, mode per series, Browse, Downloads, settings) were run for real. Online sources, downloads, servers and the dashboard were audited from code, `FEATURES.md` and the redesign spec, not run. Nothing was measured on the handheld. More than 12 frames were captured; they were viewed as 5 contact sheets (`shots/v1.png` to `v5.png`).

## 1. Verdict

The reading itself is already close to Mihon and in places better: exact-page resume works (page and place inside a tall page, across a restart), the per-series mode is one press (**X**), History continues in two presses, the download sheet is three presses, and the two-screen Fullscreen and Book views have no Mihon equivalent. What is missing is everything around the reading that makes Mihon feel automatic. Nothing ever tells you a followed series has new chapters. The default view (Fullscreen) crosses a chapter boundary with no cue at all. Getting from Home back to your page costs 6 to 7 presses, one of them dismissing a 241 MB download offer that returns on every entry. The library has no sort, filter or search. Read marks are one chapter at a time. The top screen shows the idle logo on every Manga page, so truncated titles can't be read anywhere. Fix the quick wins below and the nag, the boundary and the press count go away in a day or two of work; Updates is the one large piece that decides whether it feels like Mihon.

## 2. Quick wins (S effort, high value)

| # | Change | Where |
|---|---|---|
| Q1 (done) | Offer the engine download once, not on every entry; afterwards it lives only in Sources › Repositories (the row is already there) | `src/ui_ext.cpp:14-34` |
| Q2 | End-of-chapter cue and chapter-title chip in Fullscreen (the default view has neither) | `src/ui_reader.cpp:173-176`, `src/ui_manga.cpp:2497` |
| Q3 (done) | **LEFT** at a chapter's first page opens the previous chapter at its last page | `src/ui_manga.cpp:715-735`, `:893` |
| Q4 (done) | Resume button names its target: **Start · Chapter 01** / **Resume · Ch. 02 · p. 5/8** | `src/ui_manga.cpp:2387` |
| Q5 (done) | **Mark previous as read** and **Mark all read / unread** (a small sheet on a held **X**, or a second **X** row) | `src/ui_manga.cpp:959-968`, `src/ui_manga_browse.cpp:1819` |
| Q6 (done, see note) | Dashboard: a read / unread toggle per chapter (the endpoint exists, the page never calls it) | `web/js/manga.js:110-128`, `src/web/api_manga.cpp:353` |
| Q7 | Hints: add **LEFT/RIGHT Page** on the series page; Options on one key (X in Library, Y in *On this handheld* today); **Prev** / **Next** instead of the cut *Prev chap…* / *Next chap…* | `src/ui_manga.cpp:2411`, `:2099`, `:2319`, `:2880` |

## 3. Journeys

Presses are counted from Home with the cursor on Files, the reader already on. "+1" is the **B** that dismisses the engine sheet while Extension support isn't installed.

| Journey | Here | Mihon | Friction |
|---|---|---|---|
| First open | Home → RIGHT, DOWN, A (drawer) → START (Manage) → A (on) → sheet A (Open Manga) → engine sheet B: **7**, two sheets back to back | Open the app: 0 | The second sheet focuses **Download 241 MB**; an extra **A** starts it (this happened in the audit run) |
| Open the library | RIGHT, DOWN, A, A (+1): **4-5** | 0-1 | Manga is two levels under Home; the engine sheet every time |
| Continue the series I was reading | Library (4-5) → A (series; the cursor is on the last read) → A (Resume): **6-7**. Through History: 4-5 → R → A: **6-7**, lands on the page | 2 (History › resume, or series › Resume) | No Continue reading on Home or in the drawer; a single-file series saves one press |
| Leave mid-chapter, come back | B, later A on the chapter or Resume: page and place kept (verified across a restart: p. 4/8 came back as p. 4/8) | Same | None. The row shows *p. 4/8* and a dot on the chapter Resume opens |
| Next chapter | RIGHT at the end, then RIGHT again: 1 extra press. One screen: *End of chapter · R2 or RIGHT for the next*. **Fullscreen (default): no message, no title on arrival** | Transition page naming both chapters; continuous in webtoon mode | The default view is the one without the cue |
| Previous chapter | L2 only, lands on its **first** page; LEFT at page 1 does nothing | Back past page 1 lands on the previous chapter's last page | Re-reading the last pages of the chapter before costs L2 plus a walk to the end |
| Mark one chapter read / unread | X: **1** | Swipe or long-press + tap: 1-2 | None |
| Mark 40 earlier chapters read | 40 × (DOWN, X): **80** | Long-press, *Mark previous as read*: 3 | No bulk marks |
| New chapters of followed series | Per series: A, L2, wait, B: **3 presses + a wait each**; nothing is ever checked by itself | 0 (scheduled) or 1 pull; an Updates tab | No Updates at all; the unread badge is stale until each series is opened and refreshed |
| Add a series from an installed source | Library → R, R → DOWN to the source → A → pick a tile → A → Y (heart): **about 7** | Browse › source › long-press: 3-4 | Fine once the source is there |
| Install the first source | Browse → X → Repositories → *Add repository…* → a 50+ character address on the on-screen keyboard → Available → A | Same idea, but one tap from a link | The dashboard's paste box solves it and nothing on the handheld points there (the redesign's **From my phone** does) |
| Search a source | Browse → source → Y → type: 3 + typing | Same; plus global search over every source | No global search; every word is typed on the handheld |
| Download chapters | R2 → choice → A: **3** (This chapter / Next 5 unread / All unread / Choose) with sizes and free space | 3 | None; this is as good |
| Read offline | The same series page (*On card*), or Downloads → series → chapter | Same page | Issue #15: the two doors keep separate places |
| Delete downloads | Downloads → series → X, X per chapter or series | Auto-delete after reading; *Delete read chapters* | All by hand |
| History | R → A: **2** to continue; X: Clear (item / hour / today / 24 h / all) | 2 | None |
| Reading mode per series | X while reading: 1-2, kept for the series | 2-3 | Better than Mihon; in Fullscreen no label was seen 0.8 s after the press |
| Library housekeeping | X: *Remove missing* only | Sort, filter, search, categories | Order is last-read only |
| Phone + handheld | Dashboard: library covers and unread counts, a series' chapters and download picks, Sources (paste a repository), the queue; changes show live on both sides | n/a | The phone can't mark read, search or add a series, open a chapter on the handheld, or show History |

## 4. Findings

### P1: blocks or confuses

**F1. The engine download is offered on every entry, focused on Download.** `openManga()` raises *Get the manga engine?* each time Manga opens while Extension support isn't installed; the cursor starts on **Download · 241 MB**. Someone who reads only from the card dismisses it for ever, and a habitual **A** (the same key that opened Manga) starts a 241 MB download. In the audit run the second entry's sheet took an **A** meant for History and started the download. *Fix:* ask once (a kept flag beside `manga-intro-shown`), merged into the first-time sheet as a third button (**Open Manga** / **Get online sources · 241 MB** / **Done**). Afterwards it is only the row in Sources › Repositories (`mangaEngineOffer()` already serves it) and the Manga settings row. Keep the per-entry offer for the lighter-runtime upgrade only if it must stay, with **Not now** focused. *Effort S.* `src/ui_ext.cpp:14-34`, `src/ui_manga.cpp:1885-1950`.

**F2. Nothing says a followed series has new chapters.** A source's chapter list is asked only by **L2** on its own page (by design: "nothing is ever asked by itself"); the Library badge counts unread in the kept list. Following 20 series means 20 × (open, L2, wait, back). This is the feature Mihon users open the app for. *Fix:* (a) **Y** on Library: **Check for new chapters**, a background pass over the library's source series with a line under the tabs (*Checking 7 of 20…*, then *3 series have new chapters*); (b) the cover badge turns accent with the count of new ones and those series sort first; (c) a daily automatic pass on entering Manga (setting **Check for new chapters**: Off / On entering, daily), which the design already lists under Later; (d) later, an **Updates** list (new chapters by day, **A** reads, **R2** downloads). Engine sources are checked only while the engine is up; say so (*5 wait for online sources*). *Effort L (a+b M).* `src/ui_manga_browse.cpp` (the L2 refresh path, about `:2060-2110`), `src/ui_manga.cpp:2050-2100`, `src/mangastore.*`, `docs/design/manga.md` section 10.

**F3. The default view crosses chapters silently.** **Open fullscreen** is On by default. In Fullscreen the only overlay is the page chip for 1.5 s; the end-of-chapter line is drawn only on the one-screen controls page and in Book. So at the last page the view simply stops moving, the next **RIGHT** opens the next chapter with no title, and the last chapter gives no "caught up". Observed: Chapter 01 p. 8/8 → RIGHT → RIGHT → *opened Chapter 02* with nothing on either panel. *Fix:* at `atEnd` in Fullscreen keep a chip up: **End of Chapter 01 · RIGHT: Chapter 02** (last one: **You're caught up · B: chapters**); when a chapter opens by a step, show **Chapter 02 · 1 / 8** for 2 s. Better, in Vertical scroll: a card in the strip itself after the last page, as Mihon's transition page. *Effort S (chips), M (the card in the strip).* `src/ui_reader.cpp:91`, `:173-176`, `src/ui_manga.cpp:2497-2499`, `:2856-2870`, `src/reader.cpp:1115`.

**Context, not a new finding: issue #15.** A source's chapter read online and the same chapter read from its download keep separate places (the Downloads door falls back to `dl:` / `local:` ids, `src/ui_manga_downloads.cpp:350-352`). A fix is being built. It matters to F2 and F9 too: marks have to be on one id before badges and "delete read chapters" can be trusted.

### P2: friction

**F4. Six to seven presses from Home to your page.** RIGHT, DOWN, A, A, (B), A, A. Manga is absent from Home's Recent (owner's call, 2026-11-11; `manga-recent` is kept but not shown). *Fix without reopening that decision:* the drawer's tile becomes two targets: **A** opens the Library, **Y** *Continue · Paper Lanterns Ch. 02 p. 5* opens the page (Home → RIGHT, DOWN, A, Y: 4). In Library, **Y** does the same until F2 takes Y, then it moves to the first cover slot as a Continue card. The redesign's rail (Manga one L/R away) with a Continue card on Home is the real answer; worth asking the owner again for the Continue card alone. *Effort S-M.* `src/ui_ext.cpp`, `src/ui_manga.cpp:773-800`, `src/ui_home.h`.

**F5. No bulk read marks.** **X** toggles one chapter. Importing a series already read elsewhere, or marking a batch, is two presses a chapter. *Fix:* **X** stays the toggle; holding **X** (or **X** on an already-toggled row within a second) opens **Mark previous as read**, **Mark all read**, **Mark all unread**. `markRead()` already takes a list. *Effort S.* `src/ui_manga.cpp:959-968`, `src/ui_manga_browse.cpp:1819`, `src/ui_manga_netfolders.cpp:524`, `src/mangastore.cpp:522`.

**F6. The top screen is the idle logo on every Manga page.** Library, series, History, Browse: the top panel shows "Banana Player · Pick a video or a song". Covers are 5 to a row with names cut at about 11 characters (*Paper Lant…*, *Long Scroll …*), and no place shows the full title, the cover at size, the author or the description (a source's Info is a dialog). *Fix:* pull the redesign's Preview pane forward for Manga only: the cover large, full title, author, *Ch. 02 · p. 5/8*, unread and downloaded counts, and for a source series the description. It follows the cursor. *Effort M.* `src/topscreen.*`, `src/ui_manga.cpp:2050-2100`, `:2327-2411`; spec: `screens.js` id `manga`.

**F7. The library can't be sorted, filtered or searched.** Order is last read; **X · Options** holds only **Remove missing**. Past 30 series this is a hunt, and a series from the card can't be told from a source's. *Fix:* the Options sheet gains **Sort** (Last read / Title / Unread / Added), **Show** (All / Unread / Downloaded / On card / a source), and **Search** (the keyboard, filters as typed). Categories can wait. *Effort M.* `src/ui_manga.cpp:1023`, `:2160-2172`, `src/mangastore.*`.

**F8. Going back a chapter lands on its first page.** `mangaChapterStep()` and the source's step always open at page 0, and the back key at a chapter's first page is ignored. *Fix:* back at the start opens the previous chapter at its last page (with F3's chip); **L2** keeps opening at the start. *Effort S.* `src/ui_manga.cpp:715-735`, `:893`, `src/ui_manga_browse.cpp:1423`.

**F9. Downloads are all manual.** No delete after reading, no "delete read chapters", no download-ahead while reading, no download of new chapters for a followed series. On a card that also holds videos this is the housekeeping Mihon removes. *Fix:* Manga settings: **After reading** (Keep / Delete the chapter); Downloads **X** on a series adds **Delete read chapters · N · 214 MB**; Reading menu row **Download next 5**; with F2, **Download new chapters** per series. *Effort S each, M together.* `src/ui_manga_downloads.cpp:499`, `:629`, `:1440-1484`, `src/mangadownloader.*`, `src/ui_manga.cpp:579`.

**F10. Resume doesn't say what it opens.** The button reads **Resume** on a series never opened, and never names the chapter; the only clue is a small dot in the list, which can be off screen. *Fix:* **Start · Chapter 01**, **Resume · Ch. 02 · p. 5/8**, **Read again · Chapter 01** when all are read; opening the page scrolls the list to the dotted chapter. *Effort S.* `src/ui_manga.cpp:2368-2403`.

**F11. The phone can watch but not help.** The dashboard lists the library, chapters and the queue, and adds repositories. It can't mark read (`POST /api/manga/read` exists and is unused by `manga.js`), can't search a source or add a series (the one job where a phone keyboard beats the handheld's), can't send a chapter to the handheld, and has no History. *Fix, in order:* the read toggle (S); **Read on handheld** on a chapter row (S-M, through `WebServer::call`); source search and **Add to library** from the page (M-L); History (S). Point at it from the handheld: the redesign's **From my phone** on *Add repository…* and on Search. *Effort S to L.* `web/js/manga.js`, `src/web/api_manga.cpp`, `src/ui_manga_web.cpp`.

**F12. Long chapter lists.** Five rows a screen; **LEFT/RIGHT** page by five but no hint says so; rows show the title and a state only (no number, date or group for a source's chapters); the order can't be flipped. A 300-chapter series is 60 screens. *Fix:* the hint; 7 rows at a tighter height; a dim date on the right; Options: **Newest first**; **L2/R2** jump to the first unread on a card series (free there). *Effort S-M.* `src/ui_manga.h:431`, `src/ui_manga.cpp:970-990`, `:2393-2411`, `src/ui_manga_browse.cpp:2104-2108`.

### P3: polish

**F13 (ComicInfo half done 2026-11-26; shape-guess not). Vertical scroll is the default for everything.** Paged, right-to-left manga is the common case, and `ComicInfo.xml`'s `Manga=YesAndRightToLeft` is parsed but never sets the mode. *Fix:* honour ComicInfo; otherwise pick by shape on first open (pages taller than about 2.5 × their width: Vertical scroll; else Right to left) until the reader chooses. Judge on the handheld. *Effort S-M.* `src/comicinfo.cpp:272`, `src/reader.cpp:233`, `src/settings.cpp:659`.

**F14. A mode change in Fullscreen has no label.** After **X** the page re-lays and nothing names the new mode (only Book sets the tag). *Fix:* a 1.5 s chip: **Right to left**. *Effort S.* `src/ui_manga.cpp:904`, `:2604-2625`.

**F15. Reading page labels.** *Prev chap…* and *Next chap…* are cut; the hints line omits **X** (mode) and **Y** (Pages). *Fix:* **Prev** / **Next** with the glyphs; hints *X Mode · Y Pages · START Menu · B Close* (zoom is learnt once). *Effort S.* `src/ui_manga.cpp:2516`, `:2880-2900`.

**F16. Pages grid shows bare numbers for pages not yet decoded,** even for a chapter on the card (pages 1, 2 and 8 of 8 were blank at p. 4). *Fix:* decode thumbnails in the background for chapters on the card. *Effort M.* `src/ui_reader.cpp` (`drawMangaPages`).

**F17. A card series' marks are tied to its absolute path** (`local:<path>` in `progress.tsv`). Renaming the folder, or the card mounting elsewhere, drops every mark and place. *Fix:* key by the path under the Manga folder (and migrate on load). Check against the #15 fix so both land on one scheme. *Effort M.* `src/ui_manga.cpp:211-237`, `src/mangastore.cpp`.

**F18. Options is X in Library and Y in *On this handheld*;** Sources' lists step with L/R while every other Manga page uses L/R for the app's tabs. The redesign settles both (L2/R2 for tabs inside a section); until then make Options **X** in both. *Effort S.* `src/ui_manga.cpp:2099`, `:2319`.

### Absent next to Mihon (decide, don't drift)

| Feature | Worth it here? |
|---|---|
| Updates feed, scheduled checks | Yes: F2 |
| Global search over all sources | Yes, after F11's search from the phone (M) |
| Categories | Later; F7's Show filter covers most of it |
| Migration to another source | Yes eventually (L): a source dying strands the series and its marks. Minimum: series › Options › **Find on another source**, carrying read marks by chapter number |
| Tracking (external lists) | No for now: accounts and sign-in on a handheld |
| Backup / restore | Cheap: the dashboard downloads and restores `library.tsv` + `progress.tsv` (S-M) |
| Server progress sync (Komga / Kavita) | Already listed under Later in the design; do with #15's id work |

## 5. Screens checked

`shots/v1.png` first-time sheets · `v2.png` library, series, reading, resume · `v3.png` History, controls, Reading menu, Pages, read marks, Clear history · `v4.png` mode change, series after a chapter step, top panel on Manga pages · `v5.png` Library options, Browse, *On this handheld*, Downloads, Manga settings. The test library and its generator are in `lib/` and `gen/make.py` beside this file.


**Q6 note (2026-11-26):** the chapter rows now have a Mark read / Mark unread button (POST /api/manga/read, the page's `post`: X-BP; 403 without it, checked from headless Chrome). The dashboard's series page is still unmounted code (`Manga()` shows Repositories only), so the button appears when that page comes back.
