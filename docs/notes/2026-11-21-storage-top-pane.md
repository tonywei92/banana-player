# Files' places: the storage on the top screen (2026-11-21)

With the cursor on a place of Files' first page, the top screen is the storage pane: name, where it is, filesystem,
the free room large, a bar cut into what fills it, the kinds under it.

- **Pure, tested** (browser.h, `testStorage`): `mountFactsFrom` (/proc/mounts' text: the deepest mount holding a path,
  the last line of two; `ro` as a whole option), `fsLabel`, `storageFill` (kinds with files, then Other = the used
  room none accounts for; a kind never takes more than is used). `Capacity` carries `freeBytes` / `totalBytes`.
  Layout: `lay::storagePane(kinds)` and `lay::storageSegments` (a sliver is 4 wide, 2 apart, the widest gives way).
- **Where the sums come from** (`Ui::Browse::placeFill`): the index's snapshot (`MediaVideo::size`; `MediaSong::kb`,
  new, filled by the walk from the stat it already makes, in the record's padding) and Books' shelf
  (`Shelf::size`, from `listBooks`). Nothing is walked for the pane. Kept until the snapshot, the shelf's count or the
  room's words move. **Books counts only once the shelf was built** (the Books section or Search opened): until then
  its bytes are in Other. Photos have no index: always in Other (the pane's note says so).
- **The pane** (`Ui::Browse::placePane` fills `PaneSpec::storage`, `room`, `fill`, `note`; `storagePane` in
  previewpane.cpp draws it): the kinds' colours are ACCENT (DANGER nearly full) at 100 / 66 / 40% over BG and DIM for
  Other. The Videos shortcut shows the storage holding it. Not a storage's place (a Mac's Home): the plain file pane.
- **Filesystem**: `mountFacts` is read with the room, every 2 s at most, for storages only (/proc/mounts is a file in
  memory; a Mac asks `statfs`).
- **Not seen on the handheld**: the real names (`exfat` is expected; BananaOS mounting through FUSE would say
  `fuseblk`, shown as NTFS, which would be wrong for an exFAT card: read `/proc/mounts` there once). The Card out
  pane needs a storage still listed while its folder is gone, which the places' own refresh makes brief; it wasn't
  staged. Read-only was tested only through the parser.
- **Spec**: `files-place-top` (docs/redesign/src/screens.js, `tStorage`). Note: commit 55eb15c ("the spec from
  redesign-preview") had taken `files-places` / `phone-places` out of this branch's spec again; a next copy from
  redesign-preview will take this screen out too unless it is carried there.
