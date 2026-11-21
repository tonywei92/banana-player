# Recent: a song's list, and files that are missing (2026-11-21)

- **What is kept** (`RecentContext`, history.h, bpcore, tested in `testRecentContext`): for a song only, the list it
  played from. A playlist by its name (the Session's `source.id`, the file's stem: never the path); a Files folder by
  its path (only when every item of the queue is in the song's folder: a selection keeps nothing); the Music rail's
  lists (album, artist, Music folder, Play random, Keep playing's songs) by the queue itself, since an album's order or
  a random draw can't be read again. The kind of a rail list comes from the title Music gave it (`Album · `,
  `Artist · `, `Folder · `, else Random).
- **When**: main's loop, once per Session version or history change, sets the context of the newest history entry
  when it is the Session's item (`recentContextOf`). The Session is not touched (no new state, no invariant): the
  history entry is added at `Player::load`, before the Session's Open, so the context is attached a pass later.
- **history.tsv**: `path \t time \t kind \t name \t listId \t index`; the lists as `#list \t id \t paths…` lines at the
  end, once per list (FNV-1a id). An older build reads the path and the time's digits (strtoll stops at the tab) and
  skips `#` lines; if it rewrites the file the lists are gone and the songs fall back to no context. A list Keep
  playing grew replaces the shorter one it began as for every song (`setContext`), and one longer than 400 is kept
  as the 400 around the song: the file stays small.
- **Opening** (`Ui::Home::pickList` / `playInList`): a playlist loaded again by name (`playingRow` with the kept index
  for a song listed twice); a rail list less its files gone, with Music's album/artist/folder set under the player so
  B lands there (`Ui::backNext`, used once by `showPlayer`); a Files folder through `openFile` (siblings), B to it
  (page Main makes `backFromPlayer` take `showBrowser`'s source path). An album or artist no longer in the library, a
  folder or playlist gone: the song alone and an OSD saying why. The dashboard: `/api/play recent=1` (handheld),
  `/api/recent/list` (this browser: the virtual list, then the page's own `Open` intent with `rail`).
- **Missing** (`recentMissing`, `removableCards`): `Card 2 is out` when the path is under a removable card that isn't
  mounted (`/run/sd2` on BananaOS; `BP_CARD2=DIR` stands in for it in a test run, "in" while the folder exists), else
  `Not on this handheld any more`. Home and All recent list them dimmed (`Kit::Card::off`, no picture); opening one
  opens its More sheet (the reason in its header, Remove first). `Ui::rootsCheck` lists Recent again when the cards
  change. Clear missing takes only the deleted ones. Network files are unchanged (only their server knows).
- Checked: core-test; `--press` runs (playlist Road → Home's card → "plays in the playlist Road (item 3)", B → the
  playlist's page on it; album → card → "plays in Album · Tones", B → the album page on the song; a deleted file and
  a `BP_CARD2` card out dimmed with their reasons, the sheet); one headless Chrome run (`/api/recent`'s why/list, the
  gone row's sheet, Play in this browser as the album's list less the deleted song). Not checked: the handheld's real
  `/run/sd2` unmount, Safari.
