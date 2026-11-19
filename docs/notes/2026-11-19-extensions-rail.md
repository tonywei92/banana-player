# Extensions in the rail (2026-11-19)

What the owner decided ("Extensions, as drawn", backlog.md › Decided) is built: the rail's cell is **Extensions**, it
is always there, and the Manga reader is a row of its page. Arriving at the section starts and stops nothing; only
**A** on the Manga row can stop a video, and it asks first.

## What changed, and where

- **The rail** (`Ui::Shell::cells`, `section`, `openSection`, `Page::ExtSection` in `ui.h`): Home, Files, Playlists,
  **Extensions** (G_BLOCKS, in Manga's old place), Network, Settings. Extensions is one cell for every extension that is
  on, so it no longer comes and goes with the reader's setting, and **no cell carries a warning**: the red 11-unit
  mark is gone (`Shell::draw`/`signature`), as the spec's rail says. A Manga page's section is Extensions, so the cell is
  the lit one whenever the shell draws a page of that section (the Extensions list and Manage extensions).
- **The page** (`Page::ExtSection`, `Ui::Manga::drawExtSection`): a 64-tall row per extension that is on — the Manga
  reader's 44 mark (`drawMangaIcon`), its name, a second line (series and downloads once the store is read, else what
  the extension is), a red **Stops the video** pill while a video plays and a chevron; then a 1-unit rule and
  **Manage extensions** (52). Under them the spec's line: "Coming here starts nothing…", or which video keeps playing.
  The page is drawn on the kit (`Kit::row`, whose `mark` and `second` are the row's new fields, `Kit::row`'s pill and
  chevron) in `lay::extPage`'s boxes; touches test those same boxes (`Manga::extPointer`). Keys: A opens the row (a tap
  too), B Home, L/R the sections (the shell's), L3 Search (the shell's), START the menu. **X More is skipped**: Open /
  Its settings / Turn off is not trivial, and off is Manage extensions' own switch. Hints are the shell's hint bar.
- **The guard** (`Ui::MangaGuard` over `lay::confirmSheet` + `Kit::confirmSheet`): while a video plays or is paused,
  A on the Manga row raises "Stop the video to open Manga?" with the reason and where the video waits; **Stay** is
  focused, is **B**, and the action is **Stop video & open Manga** in red ink. **Y** (or a tap on the line) ticks
  **Don't ask again**, which is the new setting `manga-ask-video` (`Settings::mangaAskVideo`, default ask; Settings ›
  Manga, "Manga and a video", Stop / Ask). A song or a photo is never asked about: the mini bar carries on into Manga.
  The stop is the existing Stop (`ui.stopPressed`); `Ui::takeStopKeepsPage` keeps the page on Manga instead of going
  back to where the film was opened from, and `videoBlockedAt` (which the five-second note and Home's **Stopped for
  Manga** card read) is set by the guard, so the note is left out (`guardStoppedVideo`) and Home still says why.
- **The empty state** (`lay::extEmpty`): with none on, the Extensions mark, **No extensions are on**, two wrapped lines
  and one 240-wide key, **Manage extensions** (A). Its one Rect is what the touch tests.
- **Manga is a page of Extensions**: the reader's B (and the header's ‹, which is B) goes to the Extensions list, not
  Home (`Manga::mangaKey` → `openExtList`). The list's **A on the Manga row** resumes the page where it was left (its
  tab, its cursor: `mangaReady` skips `mangaOpen`'s reset) unless a video plays. `Manga::sectionLeft` records where the
  section was left (`Manga::extReturn`); the rail's Extensions cell returns there when the section is re-entered and a
  video does not play.
- **Manage extensions has two doors**: the Extensions page's row, and a new Settings group tile **Extensions** (same
  mark as the rail, "1 on · Manga reader" / "None on"; `GROUP_EXTENSIONS`, and `SG_EXTENSIONS` in `settings.h` so the
  two enums still line up). **B** returns to the door it came through (`extFromSettings`). `ui.cpp`'s menu block no
  longer closes START's menu on B over `Page::Extensions`, so that B reaches the page.
- **START's menu**: one **Extensions** tile in Manga's place (`MP_EXTENSIONS`, G_BLOCKS, "1 on · Manga reader" / "None
  on"); it opens the Extensions page.
- **Home's drawer is gone**: `EXTDIR`, the `TAB_EXT` browse tab, `extDrawerKey`, `drawExtensionsDrawer`, the drawer's
  tiles and their touches and the `Ui::signature` term for them are removed. Home had already lost its tile; nothing
  of the drawer is left.

## Not done, and why

- **Manga's own pages still draw their own frame** (their header, their four tabs, their own hint bar), so the rail is
  not drawn while reading or on Manga's page: the previous step's note says they keep their frame until they are
  rebuilt. That means the "coming back returns to Manga where you were" rule has nothing to exercise it from the rail
  today — `sectionLeft`/`extReturn` implement it, and the visible half is the Manga row resuming where you left it.
  Making Manga a shell page would also cut a fullscreen comic's page by the bridged body (54..450 of the panel), so it
  is a step of its own.
- **L/R inside Manga** still step its tabs (the spec moves the tabs to L2 / R2 and L/R to the sections, as part of that
  same rebuild).
- **Comics opened from Files or Home's Recent** while a video plays still stop it without the question (the old
  `mangaVideoGuard` path); the brief put the guard on the Extensions row. The same check could sit in `openManga` once
  those paths are rebuilt.

## Checked

Desktop build (macOS) and `./build/core-test` ("core: all passed"), including the new `lay::extPage` / `lay::extEmpty`
checks in `testKitLayout`. Scripted runs with generated media only (a testsrc MP4, 2:00; a mono sine MP3), `BP_SHOTS`
into `.scratch/shots` of this worktree:

- `ext-section`: the rail's Extensions cell lit, the Manga row with its mark and line, the rule and Manage extensions.
- `ext-video`: a video playing — the red **Stops the video** pill, the mini bar, "…keeps playing while you are here".
- `guard`: A on the row — the sheet, Stay focused and on B, the action in red, the Don't ask again line with Y.
- `stayed`, `guard2`: Stay leaves the film playing; A asks again.
- `tap-opened`, `manga-opened`, `m3`: the action (by keys and by taps) stops the film and opens Manga on its page; B
  there goes to the Extensions list; A on the row returns to Manga's History tab, where it was left.
- `home-stopped`: Home's Continue card reads **Stopped for Manga**, at 0:08 of 2:00.
- `ext-empty`: the Empty state, its key 240 wide.
- `manage`, `manage2`, `settings-groups`, `settings-back`: Manage extensions from the list and from Settings ›
  Extensions, and B back to each door.
- `menu`, `menu-ext`: START's menu with one Extensions tile, "None on", opening the page.
- `song-ext`, `song-manga`: a song plays through — no pill, no question, the mini bar on the Extensions page, the song
  still playing in Manga.
- `rail-tap`, `touch-manga`: a tap on the rail cell and on the row does what L/R and A do.
- `search`: L3 on the page is the shell's Search (a note until the page is built).
- `manga-settings`: the Manga settings page (START in Manga) lists the new **Manga and a video** row, Stop / Ask.
- The "Don't ask again" tick was saved (`manga-ask-video = stop` in `settings.conf`) and the next press stopped the
  video without the sheet.

Not checked: the handheld (pixels, touch, the 1 GB memory), the Linux build and the Docker check, and the rendered
result on a real display (SDL's macOS window was all the runs used). The rail's label "Extensions" at 11.5 units was
not measured against the 68-unit cell on the device.
