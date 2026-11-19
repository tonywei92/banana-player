# Settings on the shared shell (2026-11-19)

Step 5 of the redesign: the Settings pages (`settings`, `settings-group`, the ten group pages, `set-theme`, the
`power-*` states the screen-off settings describe) as content inside the shell, not a full-panel page. The settings
themselves, their groups, the theme preview and every key were already built (`2026-11-17-settings-groups.md`):
this step moves the drawing and the touches into `lay::shell(mini)`.

## The frame contract

- `src/ui_settings.h` / `.cpp`: the page draws only in `lay::shell(ui.miniBarShown(p)).body`.
  `frame(p)` is the one small function that draws the page's header (back, title, crumb, the group neighbours or
  the status marks) and its hint bar inside the shell's `header` / `hints` strips. When `Ui::Shell` lands, the merge
  replaces `frame()` with the shell's own drawing; `title()`, `crumb()` and `hints(p)` are the values it will need.
- The back key is drawn at the shell's `main.x + 10` rather than the old `x = 10`. Ui's own `buttonRect(BACK_ICON)`
  is still the old full-panel box (it cannot move until the shell owns the header), so `SettingsPage::pointer` takes
  a tap on `lay::shellHeader(...).backHit` and, on the lift, calls the same `pressAnyway(Button::B)`. `backDown` is
  in `signature()`. `B` itself is unchanged.
- `Kit::hintsIn(Rect, list)` is an additive variant: `Kit::hints` now calls it with the full foot, so its drawing is
  unchanged. The Settings page passes the shell's hint strip so the rail's bottom is not painted over.
- `lay::shellHeader(Box main)`: the `Kit::header` boxes on the shell's strip (back 56 x 48 at `main.x + 10`, More at
  the right, the title's room between the margins, the hints on `main`). `lay::settingsBody`, `settingsTiles` and
  `settingsRowsIn` inset the body by the page's margins; the help waits for the mini bar to go (with it the body ends
  just under the seventh row), so the rows keep their seven and their 46-unit pitch.
- Tests: appended to `testKitLayout` (header inside the main strip, tiles and rows inside the body with and without
  the mini bar, the help present only without it, no two tiles overlapping).

## Rows, groups, names

Rows still come from `settingDefs()` (`ui_settings.cpp` hand-lists only the action rows that have no setting: Sound,
Clear play history, Check for updates and the update itself). No group or row was renamed in this step: the prior
settings pass already had the spec's ten names and labels. The differences from the spec's list, kept because they
are existing features or settings that do not exist yet:

- Ten groups, not twelve: **Extensions** and **Manga** are not Settings groups (the extensions drawer and the Manga
  app's own settings page stay), as `FEATURES.md` says.
- No **Picture look**, **Top screen off (music)**, **Dim before turning off**, **Stay awake for copies and
  downloads** or **Dim the controls while reading** rows: there is no setting or behaviour behind them yet.
- The focused row's explanation stays above the hint bar (`settingHelp`), as before. The spec puts it on the top
  screen; the top screen's pane is still only the Theme row's preview (`Ui::topPane`). With the mini bar up the line
  has no room and is not drawn (the rows keep the space).
- The tile page keeps the spec's `F Quick settings` hint (the spec drops that key, but Quick settings is an existing
  feature) and does not advertise `L` / `R` / `START`: the rail's section keys are the shell's, not this page's.

The `power-dim`, `power-wake` and `power-awake` screens describe behaviour outside the Settings area (the five-second
warning, the wake press, the Awake badge). Their setting rows are the ones above; none of the three behaviours was
added here.

## Checked

- macOS build (`SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk`, `cmake -S . -B build`, `cmake --build
  build -j`), `./build/core-test` ends `core: all passed` (the new layout checks included).
- Read back: the tile grid is two columns of 58-tall tiles ending at 386; seven 46-tall rows end at 384; the help is
  400..444 without the mini bar; every rect is inside `lay::shell(mini).body`.

## Not verified

- Screenshots: `./build/banana-player --press ... shot:NAME` is refused by this session's shell (the permission list
  in the brief does not include the player), so the two panels were not looked at. The layout numbers above are from
  the code and its tests.
- The handheld at one pixel a unit: the 12-unit tile summaries in a 197-unit line, and the stepper that still uses
  the old absolute `SET_ARROW_X` (372) so the shared list lift's left/right halves keep working before the shell moves
  the header and the rows' touch code together.
- The merged shell: `frame()` still draws the header and hints, and the rail strip is left empty until `Ui::Shell`
  draws it.
