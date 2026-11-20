# Files' add modes on the kit, the bridge gone (2026-11-20)

- Adding files to a playlist (SEL_ADD with a destination) and picking a folder (SEL_PICK) now draw on the Files page:
  `Browse::addMode()/pickMode()/selecting()`, `drawAddBar` (3 / 2 keys, `lay::filesPage(..., barKeys)`,
  `Kit::selectBar(..., n)`), keys through `addKey` (movement and A here, the rest `Playlists::selKey`), touches
  `addBarAct`. The ticks stay the playlists' (`picked`, across folders). Files' own select mode keeps SEL_PICK as a
  sentinel, so `fileSel` tells the two apart. The header's key is a cross (`ShellHead::cancel`).
- Deleted: Shell's beginBody/endBody/mapPoint (the 0.88 texture bridge), adopt()/adoptBrowse(), Ui::drawTracks and the
  old select header / bar / playlist row / section header / where line that only it called.
- Not covered: add mode on the connections' list (NETROOT) still draws the kit's list without a bar (keys work; B / X leave).
