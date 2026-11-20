# Old-style drawing sweep on the bottom panel (2026-11-20, branch c-sweep)

Goal: nothing on the bottom panel drawn by hand with drawBox / fillRound / drawDialog; everything on `Ui::Kit` and `lay::`
Rects, Clean look, keys stay keys. One commit per group.

1. **Dialogs.** Quit is now an overlay (`Ui::QuitAsk`, so it covers the rail) on `Kit::confirmSheet`; Incognito, the lid
   question, `Ui::alert` and the manga browser-check alert are `confirmSheet` / `alertSheet`; "Finding..." (Docs) and
   "Reading folder" (Playlists) are `busySheet`s. `Home::quitOpen` and its box maths are gone. A on a two-key sheet
   chooses the focused key (LEFT/RIGHT moves, B is the safe key): Incognito's and the lid question's A no longer
   always means the action.
2. **Sound.** The old Sound page (own tabs, drawn by hand, own dialogs) is deleted. Settings' Music group has two rows,
   Equalizer and Sound effect (`SOUND_FX_ROW`); each opens the player's `SongViewSheet` (an overlay, `fromSettings`) on
   that tab, with nothing playing. The sheet's EQ bands use `slideGrab` with the sheet as `owner`; it now commits the
   curve (`commitEq`) when it closes. `Ui::Sound` keeps the preset actions only.
3. **Brightness.** `lay::controlSheet` (tested: `testControlSheet`) + `Kit::sliderRow`, `Kit::choiceRow`; the sheet is
   `Kit::sheet` + `Kit::Row` (switch) over them. Drag checked: 128 -> 230 in the fake backlight.
4. **Manga.** Filters sheet: `lay::listSheet` rows (7, was 8). Java repository / extension sheet: `lay::sheet` with
   body lines and rows. Engine offer / download: `confirmSheet` / `busySheet` (the spinner shows while it unpacks).
   Not seen on screen: the engine sheet needs a started engine, the filters a source (no scripted way here).
5. **Leftovers.** Page reader: Try again is a `Kit::key`, the page tag a `Kit::pill`, the Pages thumbnails `Kit::flat`.
   The photo viewer's buttons and Close are `Kit::key`s (Classic). The text/PDF options menu (`drawDocMenu`) is
   `Kit::settingRow` / `Kit::Row` rows (row pitch 50, touches via `lay::settingStepper`). Dead code deleted:
   `drawSettingRow`, `drawKeyedLabel`.

## Left, and why

- `Ui::drawDialog` / `drawDialogRow` still draw: the Files context menu (`ui_menus.cpp`), Manga's Server, Source, Library,
  History, series and download menus, the "Adding manga" help, the Files info box, `ui_docs.cpp`'s pads. They are
  menus of rows, a next group of work (a `Kit::sheet` with `Kit::Row`s each, as the filters sheet now does).
- Pan / scroll pads in `ui_docs.cpp` (`drawBox(pad, SURFACE...)`): the inventory's "Pan area" (reuse, not a control).
- `ui_player.cpp`'s EQ band columns and the Match album art track, `ui_settings.cpp`'s subtitle preview box: content,
  not controls.
- `drawDocMenu`, the manga engine sheet and the filters sheet were not looked at in a screenshot (budget); built and
  `core-test` only.
