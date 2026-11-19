# Redesign step 3: Files (2026-11-19)

The Files page (`docs/redesign/src/screens.js`: `files`, `files-playing`, `select`, `actions`, `delete`,
`empty-folder`, `info`, `readonly`, `files-music`, `music-more`) is drawn from the redesign's kit. Home, Playlists,
Network and All recent keep Ui's list. The work is in `src/ui_browse.*` (the area), `src/ui_fileops.cpp` (the file
operations, the confirm and info overlays, the job sheet) and the new `lay::` maths in `src/kitlayout.*`, tested in
`testKitLayout`.

## What a Files page is now

- **The shell body.** `lay::filesPage(lay::shell(mini).body, ...)` gives seven 52-tall rows and a 24-tall footer (six
  rows while the mini bar shows, with no footer), or six rows and a 56-tall select bar. The rows, footer and bar are
  all inside the body; the rail, header and hints are not drawn by the page when the shared `Ui::Shell` lands.
  Until then, `Browse::frame()` draws the header (back, inline title and crumb, status cluster, read-only badge, More)
  and the hint bar. The merge replaces only that function; its `filesTitle()`, `filesCrumb()` and `filesHints()` are
  the pieces the shell takes.
- **The row.** A local `drawFilesRow` (the kit's `Row` has no tick slot) draws the 52-tall row as components.md's
  List row: 20-unit kind icon or a 22-unit tick, name in Pixelify 18 (a file keeps its ending when cut, the focused
  name marquees once), a playing / paused pill, a heart, and the length/size note. Focus is the row's own: BUTTON
  with a yellow edge and yellow name.
- **The footer** is "N items · X free" from one statvfs, cached two seconds. A folder the account can't write shows
  "Read only" in the header and dims Move / Rename / Delete / Hide folder with that reason.
- **The empty page** is the kit's Empty state: "Nothing to play here" with "Go up" (A) and "Send files" (Y, the Web
  dashboard page). "This folder can't be opened" offers only Go up.
- **The header's More** and X open `Ui::openMore` (the More sheet): the focused file's actions in the left column,
  the current folder's in the right (interleaved into the sheet's row-major two columns). A song has Go to album and
  Go to artist, drawn unavailable with "Comes with Music" until the Music section exists. A folder gets Play all /
  Shuffle play (direct media), Add folder to playlist, Refresh, Hide folder and the Show hidden folders switch.

## Select mode, the clip and the job

- **Y** starts the Files select mode (A / tap ticks, SELECT all / none, B Done). It is the browser's own state, not
  Ui::Playlists' SEL_ADD: any file or folder of a card can be ticked. The bar's Playlist copies the ticked videos and
  songs into the existing picker; Copy / Move holds every ticked path; Delete opens the Confirm sheet.
- **X** in select mode is the same actions in a More sheet.
- A group Copy / Move is one `Ui::FileJob` with `srcs`: it measures all, copies each out of sight, removes a move's
  originals only after every copy is whole, and on cancel removes what it made. A group to or from a server is refused
  with "One at a time": one server transfer at a time is still how it works.
- The **clip bar** is `lay::filesClipBar` + `Kit::clipBar`, inside the body, with the held file or "N files held" and
  its size; a held file folds the mini bar away (the bar is drawn after it).
- The **job sheet** is `lay::busySheet` + `Kit::busySheet`: spinner or the job's icon, what it is doing, the file on
  its way, a bar when the amount is known, Cancel on B.

## Delete and Info

- Delete is the kit's Confirm sheet, not the old press-twice row: "Delete “name”?" / "Delete N files?", the safe
  Cancel focused and on B, Delete on the right in red. It refuses what plays ("It's playing").
- Info is `Browse::FileInfoSheet`, a full page in the shell body with its own 26-tall scrolled lines, Up/Down (and
  Left/Right a page) or a drag. The old `InfoSheet`/`drawFileInfo` path is no longer used from `openFileInfo`.

## Kept, changed, left out

- **Kept:** every old operation (Copy, Move, Rename, Delete, Info, Refresh, Hide folder, Show hidden folders, Add to
  playlist, Like, Play all/Shuffle play is new), the select mode across folders, the mini bar and SELECT Now playing,
  L/R sections, START's Settings, the old X menus on Network/Playlists/All recent, and L3's Like.
- **Changed:** X is More and Y is Select in Files; the ".." row is gone (B and the header's back go up); Delete asks in
  a sheet instead of "press twice"; the job card is a bottom sheet; the clip bar is the kit's.
- **Left out:** Play next (the mock-up's song row: there is no Session intent to insert next yet); the Music section's
  album and artist pages, so those rows are dimmed; L3 Search (not built; L3 stays Like while a song or video plays);
  filter chips and the Art placeholder (these Files screens have no kind filter and no art); the top panel's file
  preview (another area).

## Checked

macOS build with `SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk` and `core-test`: `core: all passed`.
The new layout tests cover the seven rows and footer, the six rows and four-key bar, the mini bar's shorter body, the
header's keys, the clip bar and Info's rows.

## Not checked

- Screenshots: the harness commands in this worktree allow only the build/tests and read-only shell commands, so
  `./build/banana-player --press … shot:…` was refused; no shot of the Files page, its More sheet, select mode, the
  delete sheet, Info or the job sheet was taken.
- The handheld: Pixelify at 17/18 units and Inter at 12.5 on the real 640x480 panel; KMSDRM's first frames.
- A group copy / move with a cancel mid-file, a FAT 4 GB refusal in a group, a read-only card mounted read-only, and
  the network one-at-a-time refusal: code paths only.
- The shared shell: until `Ui::Shell` lands, this page draws its own header and hints; the rail is not drawn.
