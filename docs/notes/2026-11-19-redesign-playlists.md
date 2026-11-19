# Playlists and Liked, redesigned (2026-11-19)

Step 4 of the redesign: the Playlists section (`playlists`, `playlist`, `playlist-select`, `playlist-move`, `liked`,
`liked-empty`) and the Add to playlist picker (`picker`), drawn with `Ui::Kit` on `lay::` Rectangles. The shared shell
(rail, header, hint bar, mini bar) is another agent's; until it lands `Ui::Playlists::frame()` draws this page's title,
crumb and hints, and the page's content stays inside `lay::shell(mini).body` exactly as the merge will need.

## Code

- `src/kitlayout.h` / `.cpp`: `lay::PlaylistPage` and `lay::playlistPage(body, keys, barKeys)`. Inside the body: an
  optional row of keys (Play + Shuffle, and Add on a playlist of your own), 52-tall rows 2 apart, and an optional
  select bar (3 keys) or move bar (2), 6 above the rows' end. `PL_*` are its measures. Tested at the end of
  `testKitLayout` (six rows without the mini bar, four with it; select and move bars; the list of playlists' seven and
  six).
- `src/ui_kit.h` / `ui_kit.cpp`: the List row variant the spec asks for. `Row` gains `tick`/`tickOn` (a select box
  before everything), `numberThenIcon` (a playlist's place, then the kind's icon), `leadGlyph` (a play mark in the
  place's stead on the row that plays), `heart` (a small heart before the note), and `Trail::Grip` (30 x 44 at the
  right, yellow on a lifted row) and `Trail::Heart` (a Liked row's ending heart, which a tap on the row unlikes).
  Existing rows are unchanged: every new field defaults off.
- `src/ui_playlists.*`: `draw`, `frame`, `plTitle`, `plCrumb`, `plHints`, `key`, `pointer`, `actKey`, `barAct`,
  `addSheet`, the focus state (`keyFocus`, `keyPress`, `barPress`, `plPressRow`) and `syncFocus` (a playlist opens on
  its playing row, else on Play). The picker is now a `lay::sheet` with `Kit::sheet` and `Kit::row`, the page's
  title its header, and its scroll is computed from the rows that fit.
- `src/ui_browse.cpp`: three hooks only (the area's `draw`, `key` and `pointer` on a playlist page) and the new focus
  state in `Browse::signature`; the rest of Browse is untouched.

## What the pages do

- **Playlists**: Liked first, then your playlists, then New playlist; a Playing pill on the one that plays; empty state
  when the store has nothing.
- **A playlist**: Play / Shuffle / Add over the rows; each row its place, kind icon, name, heart if liked, length; the
  playing row has the play mark, a Now (or Paused / In browser) pill and `position / length`; gone items say Gone.
  A plays from the row; UP from the first row reaches the keys; SELECT goes back to the player.
- **Select mode** (Y, or X on the keys row): ticks, a grip on every row, and the bar Remove / Move / Add to… . SELECT
  ticks all, X opens More, B is done. Remove asks a second press. Add to… opens the existing picker with the ticked
  items, not files ticked in Files.
- **Move**: Y lifts the ticked block; UP/DOWN a place, LEFT/RIGHT a page, L2/R2 the ends, A puts it down (ticks kept),
  B puts it back. A finger drags a grip (the existing drag, now on the new row pitch), and a dragged row's landing line
  is drawn.
- **Liked**: the same page with Play and Shuffle only, each row ending in its heart; a tap on the heart unlikes. It is
  reordered like any playlist: its store is an ordinary `.m3u`, so the order is saved and `plSaved` still tells the
  session through `plTellPlayer` -> `SessionHost::playlistEdited`. Empty: the Empty state with Browse Files.
- **Picker**: a bottom sheet, New playlist… first, then the playlists last changed first, an Added pill on those that
  have the one file (choosing one asks Add again?), and the count.

## Kept, changed, or left out

- Kept: every old key and menu (X and Y on a playlist, the Options menu's Play / Shuffle / Rename / Delete / Add files /
  Add folder / Select / Remove duplicates / Remove missing, the item menu, the drag, the picker's questions), the
  session edit path, the one-second save, and Liked's rule that only the heart adds to it.
- The playlist list's Y still opens a playlist's menu; the spec's "Y select playlists" is not built (there is no
  selection action for whole playlists yet).
- X is the item's menu when the cursor is on an item, and the playlist's menu when the keys row has the focus (or
  there is no row). The spec's one two-column More sheet is not built; the existing menu is kept.
- The Liked empty state has Browse Files only: there is no Music section in this branch to send Y to.
- The Art placeholder is not used: these rows have no art in the spec either.
- The old select header and bar functions remain in `ui_playlists.cpp` unused; the picker is the only sheet moved to
  `lay::sheet` here (the busy card and the Options menu are unchanged).

## Checked

Desktop build (macOS, `SDKROOT` as in `docs/conducting-agents.md` §8) and `./build/core-test` ends `core: all passed`,
including the new `lay::playlistPage` checks.

## Not checked

- Screenshots: the harness shell refused `./build/banana-player` (only `cmake`, `core-test` and the read-only commands
  are allowed here), so no `--press` run or shot was made. The scratch state and media under `.cache/` were prepared
  but are not committed.
- The handheld's panel and a real finger (the kit's 12–18 unit type and the grip's 34-unit touch zone are the first
  things to look at), the mini bar's new shell position (the old bar still draws), and the header until `Ui::Shell`
  lands.
- Reordering Liked while it plays was reasoned from the store's code and the existing `plTellPlayer` remap, not driven.
