# Section navigation: the rail as a tab bar, B stopping at a section's home, B from the player

The owner's three rules (2026-11-21) and where each lives.

- **A section's home** is `Ui::Shell::atSectionHome` (ui_shell.cpp): Home, Files' places and Network, Music's views
  (Albums, Artists, and the browser's Playlists page), Videos' views, Books, the Extensions page, Settings' tiles when
  the rail or L2/R2 opened it (from START it still goes back where START was pressed). A select mode, the picker or a
  move is never a home (its B and its header's cross stay). There `Shell::key` swallows B, and `Shell::head` (over
  the page's own `pageHead`) drops the back key and the B hint ("B/START" keeps START). The areas' old "B goes Home"
  branches went, and so did Home's Quit dialog (`QuitAsk`): Quit is Quick settings or F + START.
- **The rail's tap on the section shown** (`Shell::openSection` → `toSectionHome`): inside it, its home opened at the
  top (a browser page's place forgotten: `placeIn.erase`); on its home, a glide to its first row (`listReveal` for
  the browser, `keepCursorInView` for the areas). L2/R2 never reach it (they always change section).
- **B from the player** (`Ui::backFromPlayer`): the page it was opened from, as before, but a playlist's page stays
  that playlist; the cursor goes on what played through `playingRow` (playlist.h, bpcore, tested): the player's index
  when that row is it (a file listed twice), else its first row. `Music::showPlaying` and `Videos::showPlaying` do the
  same for an album's tracks, an artist's songs and Videos' list.
- Checked with `--press` runs (the harness's music line now prints `row=`, the album's or artist's row cursor).
  Not checked: Manga's pages (they are inside Extensions, their own B unchanged), the handheld's touch.
