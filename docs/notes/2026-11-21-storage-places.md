# Files' places: storage cards with their room (2026-11-21)

Files' first page and the dashboard's Files list each storage as a card: name, free room, a bar of what is used,
"used of total"; DANGER when nearly full.

- **Names** (`storageLabel`, browser.cpp): `/data` is "Internal storage", or "SD card 1" when the kernel says the
  device under it is a card (`/sys/block/mmcblkN/device/type` = `SD`, found through `/proc/mounts`; read once, and
  logged once as `storage: /data is on …`); `/run/sd2` is "SD card 2". Not checked on the handheld: read `log.txt`
  for that line. If `/data` isn't an mmcblk device the type is unknown and the name falls to "Internal storage".
  The Music / Videos source lines still say "This card" / "Second card" (`cardWord`, mediaindex.cpp): not changed here.
- **Room** (`capacityOf`, pure, tested): sizes in 1000s as a card's box says them; `full` under a tenth free or under
  1 GB. The web copies it by hand (`capacity`, web/js/util.js): change both.
- **Handheld:** `lay::placeRow` (a 76-tall card in an 84 pitch: `lay::filesPage(..., PLACE_ROW_H)`), drawn by
  `Ui::Browse::drawPlaceRow` inside the same smooth list, so touches and keys are the list's own. `placeRoom` reads
  every place's statvfs at most every 2 s (main thread: statvfs on a mounted card answers from memory) and
  `placesKey()` is in the signature and the rows' keys.
- **Web:** `/api/roots` adds `card` and `where`; `Place` (components.js) draws a storage in Files' first page and in
  the sidebar's Handheld card, which listed `roots[0]` only (on the handheld that is the Videos shortcut, so it
  read as "internal only"). `WebServer::setRoots` now sends an `fs` event, and the page reads `/api/roots` again on
  any `fs` event at every width (it did so on a computer only): a card put in shows without a reload.
- **Testing on a Mac:** `BP_BROWSE=a:b:c BP_BROWSE_CARDS=2` makes the first two storages. A small disk image
  (`hdiutil create -size 64m -fs ExFAT`, attached with `-mountpoint … -nobrowse`, detached after) gives a second,
  nearly full one.
- **Spec:** `files-places`, `phone-places`, and `card-out`'s rows (docs/redesign/src/screens.js: `placeRow`,
  `placeFolder`, `phonePlace`). The preview was rebuilt and its three screens render; their pictures weren't looked at.
