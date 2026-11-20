# The top screen with nothing playing: Preview pane and State card (2026-11-20)

The old idle card (mpv overlay in `Player::showIdle`: a purple clapperboard picture, "Banana Player", "Pick a video or a
song on the bottom screen") is gone, with `src/logo.h`. The top panel is drawn by the app, in the theme's tokens and
Pixelify, through the Preview pane's path (`previewpane.*` composed in software at the panel's pixels, shown 1:1 by
`TopScreen::drawPane`; composed again only when its key, the theme or the size changes).

- **Brand mark:** `lay::brandMark(x, y, size)` (tested: the 22 grid, scaled), drawn by `brand()` (polygons through
  `card::Canvas::poly`). Top left of every pane, 22, with the name beside it.
- **Welcome:** `PaneSpec::welcome`: the app icon (84, radius 20, `assets/icons/app.png`, decoded once per panel size in
  `TopScreen`) beside label/title/sentence, the three ways to start as the two-column rows under it (`lay::previewPane(cover, welcome)`).
  Shown on Home while Recent is empty (a fresh install). `--press gallery:new:3` draws it.
- **Wiring:** `Ui::topPane(out, player, idle)`; `idle` is "nothing plays, no photo, no note card up". `ui_toppane.cpp`
  (`Ui::idlePane`) reads state the pages already hold: Home (the focused Recent card), Files (`browseSelection()`:
  kind, size, folder), Videos (focused row, size, "From ..."), Music (album/artist, cover layout for an album),
  Books (cover layout), Settings (the focused row's label and `settingHelp`), Search, Extensions, and a plain
  "Nothing playing" pane for pages with no pane of their own. The Theme row's pane still shows over what plays.
- **Covers:** the cover layout shows the cover's place with the picture mark: the shared art cache holds SDL textures
  only, not pixels, so a decoded cover is not drawn yet (`PaneSpec::coverArt` is ready for it).
- **State card:** `lay::stateCard` + `PaneSpec::state` (tile with icon or spinner, title, line, bar, hint with its key,
  foot). `Player::showElsewhere` and `checkLoading` no longer put mpv images (`OV_LOAD`, `OV_STATE`, `drawStateCard`
  deleted); they set `Player::topState()`, which `TopScreen::draw` shows as the pane (it wins over the cursor's pane;
  what plays goes on under it, its frames taken unseen). Finished and error have no top-screen card in the app today, so
  none was drawn.
- **Unchanged:** the level card and the note card stay mpv overlays (the pane carries the level card as before);
  a song's title/artist lines stay in mpv's font (owner, 2026-11-20); KMSDRM's redraw after start is the loop's
  `refresh`, which the pane honours; with nothing changing, nothing is drawn.
