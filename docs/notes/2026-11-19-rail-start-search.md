# The rail, START and Search (2026-11-19)

The owner's three decisions from `docs/redesign/backlog.md` ("Decided") and `components.md` ("Section rail",
"Header", "Arriving stops nothing"): START's menu is dropped, Search moves into the rail, and the rail's cells fit
seven sections or scroll under a finger.

## START is Settings, and remembers the door

- `Ui::openSettings(p, fromStart)` / `Ui::closeSettings(p)` replace `openMainMenu` / `closeMenu` and the whole
  `Page::Menu` tree (`TAB_*`, `MP_*`, `menuCursor`, `menuNowShown`, `drawMenu`, `menuKey`, `menuPointer`, `menuGo`,
  the Menu's sheets in `ui_home.cpp`, its `START` keys in `ui_browse` / `ui_ext` / `ui_player`).
- Opened by **START**, Settings records the page and the browser's folder/cursor/scroll; **B** or **START** goes back
  there. Opened by the **rail** or **L/R**, `settingsOver` is false and **B** goes Home (the section rule).
  Settings opened from a group or from Sound/Web keeps the same origin because only `page` and `setGroup` move.
- START is taken once, in `Ui::press` before an area sees it (`Ui::startOpensSettings`). Its exceptions are the places
  where START is a page's key: a form's Save/Done (and Find on the network), the on-screen keyboard (taken earlier),
  a document's open menu, Book mode (a page held sideways: the Book bar / Text options), and Manga's own pages.
- The player, the photo viewer, ExtSection, Manage extensions and the text/PDF readers all reach Settings now. The text
  and PDF readers lose START as their Options key; **Options is the on-screen key** (a touch), and the text reader's
  hint bar says **START Settings** as the spec asks. In Book mode START still opens Text options.
- Quick settings (**F**), About (Settings' last group), Quit (**B** on Home, Quick settings, **F + START**) and
  Manage extensions are untouched. `--press`'s info line now reports `settings` instead of `menu=…`.

## The rail

- `lay::rail(sections, h, scroll)` (kitlayout_new.cpp) now lays out: Search's key (68 x 40) pinned at 5 from the top;
  the sections in a clipped view; Settings (68 x 56) pinned at the foot; a `capStart` row over Settings. L/R caps are
  gone. Cells are 47 tall with seven or more sections, 56 with six or fewer. `maxScroll` is the content beyond the
  view, and `railAt` returns -1 outside the view (and for Search, which is the shell's own key). Settings can never be
  covered or scrolled.
- `Ui::Shell` keeps `railScroll` and drags the sections under a finger (`down` starts the drag on the cells' view, a
  move beyond 6 units scrolls and cancels the tap, a lift without a move opens the cell). The drag state is in the
  shell's signature.
- `KitNew::rail` draws the Search key (BUTTON with its edge; ACCENT with ON_ACCENT ink while Search shows), clips the
  cells to the view, and puts the **START** cap over Settings. The header's Search key and `ShellHead::search` are
  gone; the title now ends before the status cluster.
- Search itself is still the one placeholder: L3 and the rail's key raise **Search is coming in a later step**, and the
  key is lit while that note is up (its `searchUp` flag is cleared when the alert closes). L/R never visit Search.

## Checked

- Desktop build (`SDKROOT=…MacOSX26.sdk`, `cmake --build build -j`) and `./build/core-test`: **core: all passed**,
  including the rewritten `testKitNewSectionRail` (six at 56, seven at 47, nine scrolling and clamped, Search/cap
  excluded, Settings fixed at every scroll).
- Scripted runs (`--press`, screenshots in `.scratch/railshots/`; the requested scratchpad path was refused as an
  external directory):
  - `home-*`: Search pinned at the rail's top, five sections, START cap, Settings; header without a Search key;
    hint bar says **L/R Sections**, never START.
  - `settings-*`: START opens Settings, origin noted; `B` and `START` both return to Home/Files.
  - `settings-rail-*` + `rail-back-home-*`: tapping the Settings cell opens it as a section and `B` goes Home.
  - `search-alert-*` / `after-search-*`: the rail's Search tap raises the note with the key lit; `B` closes it and the
    key darkens. `l3-search-*`: L3 does the same.
- `start-back-files` run: Files → START → Settings → START lands back in Files, not Home.

## Not checked

- No generated video/audio file was available, so START on the **player** and on a **book page** was not seen in a
  screenshot; the code path is the same `openSettings`/`closeSettings` and the restore page is stored verbatim.
- The drag itself needs more sections than today's five (or a short rail) to have `maxScroll > 0`; it is covered by
  the layout test and the pointer code, but not by a live seven/eight-section screenshot. The 47-tall layout was
  tested in `core-test` only (today's rail draws five at 56).
- The handheld, KMSDRM, and the Linux build were not run.
