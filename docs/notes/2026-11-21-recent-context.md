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
- **Opening** (`Ui::Home::pickList` / `playInList`; the way back changed the same day, below): a playlist loaded again by name (`playingRow` with the kept index
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

## Keep playing's songs, the way back, Remove from list (same day, second pass)

- **The owner's "the list is gone"**: the song opened from Recent was one Keep playing had added (of another album), and
  `pickList` found the album by *that* song (`albumNamed(name, song)`): -1, "The album is gone", the song alone as a
  Files-kind list. That play was then kept as a Files folder ("played from the folder …"), and its next open from
  Recent went through `openFile` with B/Stop to Files: the owner's second report. Now the album is found by the list's
  own songs (those before `addedAt`).
- **What Keep playing added** is kept: `RecentContext::addedAt` (the first added song's place; they run to the end),
  written as `added=N` on the `#list` line (and in its id). Played again, `Open.added` marks them (Up next's heading).
- **A library song with no list, or one kept as a Files folder**: its Music folder's own songs (`musicFolderPick`), a
  rail list, so Keep playing applies. A real Files play of a song in a Music folder is opened this way too (accepted:
  the same folder, in Music).
- **The way back** (owner, 2026-11-21): Stop and B go to the page the player was opened from (Home, All recent, the
  album page, Search), never the list's origin; `backNext` is gone. The list's origin is used only where there is no
  such page (`showBrowser`'s cases: the List key on a playlist, Show in folder).
- **Remove from list**: Up next's X (or a row held 0.5 s) opens the row's More. The Session's `Remove` (I19);
  the handheld's own list follows by `Player::dropEntry` (the playing one as the ghost).
- Checked: core-test (named Open-with-added and Remove cases, the fuzz with Remove, history `addedAt` round trip);
  `--press` runs with generated silent tones: album to its end → 10 added → an added song → Stop → off → Recent →
  "6 of 14, 10 Keep playing had added", no new ones at its end; off at recording, on when reopened → 10 added; turned
  on during the last song → added at once; Remove of an added row, of a row before, of the playing one; Recent → Stop
  → Home; a legacy Files-folder entry → "Folder · Anime" → Stop → Home; Music › Folders → play → Stop → Music ›
  Folders. Not checked: the dashboard (its Recent list doesn't send `added` yet; no Remove on the page), the handheld.
