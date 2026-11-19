# Home on the redesign (2026-11-19)

Home and All recent (`docs/redesign/src/screens.js`: `home`, `home-first`, `home-stopped`, `home-more`, `recent-all`,
and Home's side of `card-out`) are rebuilt on `Ui::Kit` and the `lay::` boxes. The player screen's pattern was followed:
the area keeps the state, `lay::` gives every Rect, `Kit` draws it, and the touches test the same Rects.

## What changed

- `src/ui_home.h` / `src/ui_home.cpp`: one Continue card (what plays now, the newest unfinished thing, or the video
  Manga stopped), the Recent row (four cards, a kind filter, the chevron to All recent), three shortcuts
  (Shuffle Liked / Browse Files, Send from phone, All recent), the empty state, All recent's own page, and the More
  sheet on a card (X, Y or a hold) built with `Ui::openMore`. The old section tiles and the sideways strip are gone.
- `src/kitlayout.h` / `.cpp`: `lay::homePage(body, hasContinue, hasRecent, cards, more, chipWidths)` and
  `lay::allRecentPage(body)`, pure functions of the shell's body. The new cases are appended to `testKitLayout`.
- `src/ui_browse.cpp`: the small integration points: draw All recent through `Home::drawAllRecent`, send its Down
  touches to `Home::recentPointer`, append `Home::sig` to `Browse::signature`, start the focus on Continue, and route
  X in All recent to the card's More sheet. `Ui::Home` is still Browse's view, not an `Area`.
- `FEATURES.md`: Home's section, the All recent X row and the liked-heart line.

## Kept

Every existing Home key still works: A plays / resumes / opens, X and Y open a card's actions, a hold opens the same
sheet, B asks to quit, START opens the places, D-pad moves. The old card actions (Resume, Play from the start, Show in
folder, Add to playlist, Like / Unlike, Remove from Recent) are all in the More sheet, and Home's X options are its
right column (Clear recent still asks twice; Show hidden folders and Settings are still there). A liked card's heart
and a network card's mark are drawn on the picture again.

Sections: the old tiles went, so until the shell's rail lands **START → Menu** (`TAB_MENU`, the kept places menu) is
the way to Files, Playlists, Manga, Network, Settings and About; the empty state and the shortcuts also reach Files and
the Web dashboard.

## Changed or skipped, and why

- **Rail, header, hint bar and mini bar:** the shell step owns them. `Home::frame()` is the one place Home still draws
  the old header and hint bar; `title()`, `crumb()` and `hintList()` are what the shell will ask. Nothing draws a rail.
- **L2/R2 for the kind filters:** `homeKey` handles them, but `Ui::press` still swallows L2/R2 for the chapter step
  before any browser page sees them. Tapping a chip works today; the shell/key routing can pass L2/R2 to Home.
- **All recent's chips** are 28 units tall, not the kit's 40: the list's own touch maths starts at `ROW_Y` 84, and the
  28-unit strip is what fits under the header without moving the rows.
- **Pictures:** Recent and Continue show the kind mark, and a song's plain `Kit::artPlaceholder`; there are no decoded
  thumbnails or album art here (the redesign's picture cells are a later step).
- **All recent by day:** the mock-up groups rows under Today / Yesterday; `history.h` stores only paths in order, with
  no timestamps. The page lists every card in that order, with the same kind filters.
- **A card taken out:** `listRecent` still drops files that are gone. The Continue card falls back to `resume.tsv`, so
  a video whose card was pulled keeps its place there (its Resume says the file is gone, as opening any missing file
  does). The notice row over Files is the `card-out` Files step, not Home's.
- **home-first's top screen** (the welcome with the app icon and the three ways to start) and **home-stopped's note on
  the top screen** are `topscreen.*`, outside this area. The bottom halves are built.
- **The More sheet's two columns:** a card has seven actions, so Home's column has seven rows too (All recent, Browse
  Files, Show hidden folders, Clear recent…, Settings, Menu, Quick settings). Search is not built: there is no search
  feature in the app yet, and this step did not add one.

## Checked, and not

`cmake` build and `./build/core-test` pass, including the new `testKitLayout` cases (Home's boxes inside the shell's
body, none over another, the row on the right edge, the shortcut row dropped over the mini bar; All recent's chips
above rows 84…). The screenshot run was refused by the harness's command permission (`./build/banana-player` is not
in this worktree's allow list), so neither Home nor All recent was seen drawn. Not checked on the handheld: the
Pixelify/Inter sizes at 640×480, the 28-unit chips, and the touch targets.
