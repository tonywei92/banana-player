# Files' add modes on the kit, the bridge gone (2026-11-20)

- Adding files to a playlist (SEL_ADD with a destination) and picking a folder (SEL_PICK) now draw on the Files page:
  `Browse::addMode()/pickMode()/selecting()`, `drawAddBar` (3 / 2 keys, `lay::filesPage(..., barKeys)`,
  `Kit::selectBar(..., n)`), keys through `addKey` (movement and A here, the rest `Playlists::selKey`), touches
  `addBarAct`. The ticks stay the playlists' (`picked`, across folders). Files' own select mode keeps SEL_PICK as a
  sentinel, so `fileSel` tells the two apart. The header's key is a cross (`ShellHead::cancel`).
- Deleted: Shell's beginBody/endBody/mapPoint (the 0.88 texture bridge), adopt()/adoptBrowse(), Ui::drawTracks and the
  old select header / bar / playlist row / section header / where line that only it called.
- Not covered: add mode on the connections' list (NETROOT) still draws the kit's list without a bar (keys work; B / X leave).

# Group Copy / Move to or from a server

- `filePaste` no longer refuses a group when a server is at one end: `Browse::startNetGroupJob` builds a `FileJob`
  with `net`, `srcs`, `srcEnds` (`Browse::netEnd`: a card's path, or the server with its sealed password, never in the
  path: the job's `bpnet://<id>/<path>` only names it) and the names asked for.
- The loop is `xferGroup` (src/xfergroup.*, bpcore): one `xferCopy` per source, `XferProgress::doneBase/totalBase`
  carry the bytes across them (xfer.cpp writes `totalBase + its own total`). A stop (failure or cancel) leaves the
  earlier items done: each `xferCopy` is whole or nothing; the message adds "(k of n were done)". Card-to-card groups
  keep their all-or-nothing rule (treecopy).
- The progress sheet, errors and the listing refresh are the card-to-card group's (`fileJobTick`, `drawFileJob`): the
  job's thread only writes its own fields, the Ui reads them each pass (no new SDL event: the pass already polls).
- The share is cumulative and the total grows as each source is measured, so the bar can step back a little when a
  big file follows small ones.
- Fixed on the way: a group Copy on the cards called `pathMoved` for the copies (a playlist would have followed the
  copy); only a Move does now.
- A move out of a read-only server folder says so (`netReadOnlySaid`); a server to a server group goes through the same
  job (one `xferCopy` each, the same-server move is a rename).
- Tested: `testXferGroup` in core-test (cumulative bases, failure at item k, cancel between and inside items, names).
  No FTP / SMB stand-in exists in tools/, so the real transfer is not run here: try it on the handheld with a share.
