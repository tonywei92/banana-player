# Search, Network and the Web dashboard on the kit (2026-11-19; branch `net-search`)

The three areas the brief names, built on `Ui::Kit` with the layouts of `src/kitlayout.h`. The spec:
`docs/redesign/components.md` and `docs/redesign/src/screens.js` (`search`, `search-results`, `net-list`,
`net-find`, `net-form`, `web-page` and the off / no Wi-Fi / incognito states).

## Search (`src/ui_search.h`, `src/ui_search.cpp`, `Page::Search`)

An area of `Ui` (docs/notes/ui-areas.md), found by `Ui::area()`; its constructor adopts the page so it draws in
`lay::shell().body`, and `Shell::page`/`head`/`draw`/`signature` give it the rail, header and hint bar. It is a tool,
not a section: no rail cell is lit on it and `L / R` do not step the sections under it (they move the caret, or the
page in the results).

- **In:** the rail's Search key and `L3` from Home, Files, Playlists, Extensions, Network and Settings (the one
  `Shell::search` hook now calls `search->open`). A second press edits the query; `B` returns to the page it was opened
  from. In a document or the Manga reader L3 remains their Find / Like.
- **Typing:** the kit's field and `Kit::keyboard` (`lay::keyboard` fitted into the shell's body, so the drawn boxes are
  the touch boxes): three letter rows, a digits layer on `L3` / `SELECT`, Space on `Y`, delete on `X`, Done on
  `START`, the D-pad over the keys. Up to 40 letters. The query is case- and accent-insensitive (the index's
  `foldText`).
- **Results:** `mediaSearch` over the cards' index (`Ui::mediaIndex`, main's `MediaIndexWorker`), grouped **Videos**,
  **Music**, **Files** with `Kit::sectionLine` and `Kit::resultRow`; a group with nothing is left out, the matched part
  is marked in ACCENT. Files are the non-media paths the index does not hold: the history and the folder Files is
  showing. `A` opens a result as Files would (`Ui::openFile`: a video or song plays with its folder as the list, a
  document opens in its viewer). `UP/DOWN` move, `LEFT/RIGHT` page, and the page scrolls in the body.
- **States:** the empty field rests on **Recent searches** (the last eight in `<state>/search-recent`, a `Clear recent
  searches` row; nothing is kept in incognito); no recent shows the empty page with **Type** (A) and **Browse Files**
  (Y); a query nothing matches shows "Nothing called …" with **Try another** and **Browse Files**, and says network
  folders are not searched. While the index's first pass runs, the page shows the `Kit::notice` row ("Reading the
  cards") and fills in; its `signature` carries the index's generation, so the redraw follows the worker's event.
- **Kept:** the page keeps its last query and cursor; `Y` edits it. The top panel is not changed in this pass.

## Network (`src/ui_browse.cpp`, `src/ui_forms.cpp`)

The Network tab's three pages now draw in `lay::shell().body` (adopted): the saved connections as `Kit::mediaRow` rows
(60 tall, name, then kind and address), with **Find on the network** and **Add connection…** as `Kit::row` foot rows
(44 each, a finger's size; the D-pad reaches them after the last connection) as well as in `X · More`; the empty state
is `Kit::emptyState` (**Find nearby** on A, **Add by address** on Y). A connection's form is `Kit::formRow` per field
with its help line and `Test connection` / `Save` as the two keys at the foot (the `lay::cell` split `formKeys` uses;
`Y` tests, `START` saves, `LEFT/RIGHT` steps a choice, the value's keyboard as before). **Find on the network** is
`Kit::row` rows, and while discovery runs a `Kit::busySheet` says **Searching…**; `B` still backs out. Every touch
tests the boxes its draw used; the keys, the X menu (Edit / Test / Delete / Add / Find / Refresh / Web dashboard), Y's
Add and B's way back are unchanged. `Shell::head` names the pages' hints (Open / More / Add; Edit / Change / Test /
Save / Cancel; Add this one / Back).

## Web dashboard (`src/ui_web.cpp`)

The page keeps its own full panel (it is not a shell page): `Kit::header` (title, Settings crumb, status cluster and
the globe badge), a **Web dashboard** row whose trail is `Kit::toggle`, and the state's body. On: the QR code at the
left (196 square, quiet zone, whole-pixel modules), **Address** and **PIN** as `Kit::sectionLine` headings with the
address in F_P28 and the four PIN boxes large, the browser count, and **New PIN** / **Turn off** as `Kit::key`s.
`A` (or a tap on the switch row) is the switch on and off. Off: the switch off and one sentence, nothing else. No
Wi-Fi and a failed start: the switch stays on, a `Kit::notice` row says why, and **Try again** is the key. Incognito:
the switch dimmed with its reason, the sentence, and **Leave incognito**. All touches test the same boxes.

## Wiring outside the areas (disclosed)

`Shell::search` is the brief's one hook, but the frame had to learn the pages too: `Shell::page` includes
`Page::Search`, `head` asks `Search::head` and names the Network pages' hints, `adopted` treats NETROOT/FORMDIR/
FINDNET as rebuilt, and the rail lits Search from the page (not from an alert). `Page::Search` and
`Ui::search`/`Ui::mediaIndex` are in `ui.h`; `ui.cpp` lists the area and keeps START as Done in Search;
`main.cpp` hands the worker's index to `Ui`; `CMakeLists.txt` adds `ui_search.cpp`. These are the lines that make the
rail's Search key and L3 open the page instead of the old note.

## Checked

Desktop build and `./build/core-test`: `core: all passed`. Scripted runs (a fresh `.scratch/state`, `BP_BROWSE`/
`BP_INDEX` a generated `.scratch/lib` with a Music album and two Videos folders, `BP_WEB_FORCE=1 BP_WEB_PIN=ACDE
BP_WEB_PORT=8093`):

- Search: `L3`, typed `bunn` by tapping the kit keyboard, Done: three groups (Videos 2, Music 1), the query in the
  crumb; `B`, `B` back to Home. The query was written to `<state>/search-recent`.
- Network: `R*4` to the section (empty state), `Y` the form, `A` from the empty state to Find and `B` back.
- Web: Settings › Web dashboard (three Downs and a Right, `A`), on with the QR, address and PIN; a run with
  `BP_WEB_IP=none` and `X` (off) left the page without a crash.

Shots (`.scratch/shots`, four pairs): `search-results-*`, `net-empty-*` (the empty state), `net-form-*`, `web-*`.
The connection list itself was run with two saved connections in `.scratch/state/servers.conf` (rows, the two foot
rows, the cursor) and looked at; the four-shot budget went to the empty state, the form and the other two areas.

## Not done / not verified

- The top screen's echo of the search (the text large and the per-kind counts) was not added: it needs the top
  panel's drawing, outside this brief.
- Find on the network was run to its searching state and backed out of; no host was picked or saved. A connection's
  Edit (a saved store) and the offline Network state were not driven. The Web page's incognito view and its 90-second
  screen-off were not driven (the body is the old behaviour, redrawn).
- The handheld and the Linux build were not run (no Linux-only file changed).
