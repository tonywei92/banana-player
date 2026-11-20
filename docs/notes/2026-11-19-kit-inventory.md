# Kit inventory, and the components this pass added (2026-11-19)

Branch `kit-components`. `docs/redesign/components.md` defines the redesign's pieces; this note maps each one to
where it is drawn today (`src/ui_kit.h` / `src/kitlayout.h`, or the older code where it has not moved into the kit),
and lists the pieces this pass added. Nothing new is wired into a page: the gallery (`--press gallery:variants:9`
and `:10`) draws them.

## Inventory

`components.md` has 50 `###` components; its own count of 48 leaves out Lid-shut keys (a rule with nothing to
draw) and QR code (a picture of an address, with no example in the preview). "Kit" below is `Ui::Kit`; "lay" is a
pure `lay::` function in `src/kitlayout.*`.

| # | Component (`components.md`) | Where it is drawn | Status |
|---|---|---|---|
| 1 | Header | `Kit::header`; `lay::playerLayout`, `lay::shellHeader` | present |
| 2 | Status cluster and header badges | `Kit::status`, `Kit::badge`; `lay::statusRow`, `lay::badgeW` | present |
| 3 | Section rail | `KitNew::rail`; `lay::rail`, `railAt`, `railStep` | present (kit-new) |
| 4 | Hint bar | `Kit::hints` (panel or a Rect) | present |
| 5 | Mini bar | `Kit::miniBar`; `lay::miniBar`; state pills now `Kit::statePills` | present |
| 6 | Key (button) | `Kit::key`, `Kit::iconKey` | present |
| 7 | Button row and grid | `lay::cell`, `lay::gridCells`, `lay::transportCell`; drawn by the callers' keys | reuse (lay) |
| 8 | Transport key | `Kit::transport` | present |
| 9 | State chip | `Kit::chip` | present |
| 10 | Line key | `Kit::line` | present |
| 11 | List row | `Kit::row` | present |
| 12 | Pill (mark on a row or card) | `Kit::pill`, `Kit::pillW` | present |
| 13 | Filter chips and segmented choice | `Kit::filters`, `Kit::segment`; `lay::filterChips`, `filterAt` | present |
| 14 | Tabs | `Kit::tabs` (a page's ground: the `ground`/`pressed` overload; caps as wide as "L2"/"R2" need), `Kit::tabRects`, `Kit::tabW`, `Kit::tabCapW`; `lay::tabs` (`capW`), `lay::tabPage` (tabs over a body: Manga), `lay::tabAt`; gallery page 21. Also for Manga: `lay::coverGrid` (cover tiles, scrolled), `lay::rowList` + `lay::rowKey` (two-line rows, a key on the focused one), `Kit::keyW` | present |
| 15 | Switch | `Kit::toggle`, `Kit::Row::Trail::SwitchOn/Off` | present |
| 16 | Tick and radio | `Kit::Row::tick`, `Kit::Row::radio` (glyphs) | present |
| 17 | Settings row (stepper) | `Kit::settingRow`; `lay::settingStepper`, `stepperBack` | present |
| 18 | Form row | `Kit::formRow`; `lay::formRow`, `formKeys` | present |
| 19 | Bottom sheet | `Kit::sheet`, `Kit::sheetBack`; `lay::sheet`, `lay::listSheet` | present |
| 20 | Alert sheet | `Kit::alertSheet`; `lay::alertSheet` | present |
| 21 | Confirm sheet | `Kit::confirmSheet`; `lay::confirmSheet` | present |
| 22 | Busy and job sheet | `Kit::busySheet`; `lay::busySheet` | present |
| 23 | More sheet (menu) | `Ui::openMore` / `MoreSpec` (`src/ui_more.cpp`) on `Kit::sheet` | present |
| 24 | Book bar (and sheets held sideways) | `KitNew::bookBar`; `lay::bookBar`, `sideways`, `uprightPoint`, `bookBarAt` | present (kit-new) |
| 25 | Keyboard | `Kit::keyboard`; `lay::keyboard` | present |
| 26 | Slider | `Ui::slideGrab` / `slideHeld` / `slideShare` (`src/slider.h`) with `Kit::progress` | reuse (Ui); in an overlay pass it as `owner` (2026-11-20-slider.md) |
| 27 | Progress bar | `Kit::bar` (3, 6, 12) and `Kit::progress` (14, a slider) | present |
| 28 | Volume bar | `Kit::volumeBar`, `volumeInk`; `lay::volumeBar`, `volumeX`, `volumeZone` | present |
| 29 | Tile | `Kit::tile`, `Kit::choiceTile`, `Kit::settingTile`; `lay::tileCell` | present |
| 30 | Cover cell | **was missing** | **built: `Kit::coverTile`; `lay::coverTile`, `coverCells`** |
| 31 | Art placeholder | `Kit::artPlaceholder`; `artph.h` (`artph::shape`) | present |
| 32 | Brand mark | `Kit::brandMark` | present |
| 33 | QR code | `src/ui_web.cpp`'s code; `lay::qrPanel` | reuse (Ui) |
| 34 | Recent card and Continue card | `Kit::recentCard`, `Kit::continueCard`; `lay::recentCard`, `continueCard` | present |
| 35 | Empty state | `Kit::emptyState`; `lay::emptyState` (older pages still `Ui::drawEmpty`) | present |
| 36 | Notice row | `Kit::notice`; `lay::notice` | present |
| 37 | Select bar and clip bar | `Kit::selectBar`, `Kit::clipBar`; `lay::selectBar`, `clipBar` | present |
| 38 | Pan area and drag area | the photo and document viewers (`ui_docs.cpp`, `drawPhoto`) | reuse (Ui) |
| 39 | Level card | `src/levelcard.*` (`drawLevel`, chapter / EQ / note cards) | reuse (Ui) |
| 40 | Note | `player.osd`, `showNoteCard`, `main`'s top-panel note | reuse (Ui) |
| 41 | State card | `player.cpp` / `ui_main.cpp` top-panel cards | reuse (Ui) |
| 42 | Preview pane | `Ui::TopPane` (`previewpane.h`); `lay::previewPane` | present (elsewhere) |
| 43 | Song layout | `src/songlayout.*`; `player.cpp` | reuse (Ui) |
| 44 | Focus | every `Kit` component's `focused` input (`drawFocus` on older pages) | present |
| 45 | Away-from-normal mark | `KitNew::awayInk`, `KitNew::awayDot`; `lay::away`, `awayLevel` | present (kit-new) |
| 46 | Screen-off warning | `KitNew::screenOffWarn`; `lay::screenOffWarn`, `screenOffWords` | present (kit-new) |
| 47 | Lid-shut keys | a rule with nothing to draw (`lidsleep`, `volumekey`, `powerkey`) | rule |
| 48 | Arriving stops nothing | a rule: `Kit::pill` (Danger) + `Kit::confirmSheet`; no guard overlay yet | rule |
| 49 | Title marquee | `lay::marquee`; `Ui::drawMarquee`, `Kit::header`'s `marquee`, `Kit::row`'s one pass | present |
| 50 | Lock overlay | `Kit::lock` | present |

Counts: 38 in the kit, 1 in pure layout (button rows), 8 reused from older Ui code, 2 rules, 1 missing (the Cover
cell, built here). The "section page header line" ("From Music on both cards · 148 albums") is the header's
`crumb` (`Kit::header` / the shell's `drawHeader`); the in-page line and a group heading share the new
`Kit::sectionLine`.

## Added in this pass (layout + draw, no page)

| Piece | Layout | Draw | Taken from |
|---|---|---|---|
| Shared state pills | — (reads `Settings`) | `Kit::statePills` | `ui_home.cpp`'s `homeStatePills` (moved; Home and the shell's mini bar both call it) |
| Pill width | — | `Kit::pillW` | the `textW + 12` rule Home repeated by hand |
| Search field | `lay::searchField` | `Kit::searchField` | `screens.js` `searchField` (48 tall, magnifier, text, A cap) |
| Search result row | `lay::resultRow` | `Kit::resultRow` | `screens.js` `srow2` (kind icon, name with the match in ACCENT, right note, optional pill) |
| Music/Videos row | `lay::mediaRow` | `Kit::mediaRow` | `screens.js` `row2` (30 art, title, second line, kind pill, note) |
| Books cover tile | `lay::coverTile`, `lay::coverCells` | `Kit::coverTile` | `screens.js` `bookCell` (96 × 136 cover, title, progress bar and percent, count pill) |
| Page line / group heading | `lay::sectionLine` | `Kit::sectionLine` | `screens.js` `.gl`, an Info group's line, All recent's day headings (the line under Music's title became the header's crumb, 2026-11-20) |

Sizes come from the spacing scale (2 / 4 / 6 / 8 / 12 / 16 / 24): rows 60 tall (52 + the second line's room),
the search field 48, art 30, covers 96 × 136 with 14 between, and every touch target is a whole row or field
(≥ 44). Colours are `theme.h`'s roles only.

## Duplicates merged

- `homeStatePills` (Home's local copy) → `Kit::statePills`, now also the shell mini bar's pills.
- Home's hand-written pill width (`textW(pill) + 12`) → `Kit::pillW`, which `Kit::pill` itself uses.

## Gallery

`--press gallery:variants:9` draws the Search field and result rows; `:10` the Music/Videos rows, the Books cover
tiles and the page line. No screenshots were taken in this session: the harness allows build and `core-test`
only, not running the player.

## Checked

Desktop build and `./build/core-test` (the new `testKitLayoutLists` checks the layouts).
