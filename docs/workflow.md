# Working on Banana Player: the harness, tests, bundle and releases

Moved out of CLAUDE.md word for word (CLAUDE.md keeps the commands and a line on each).

```

- **Scripted runs** are the end-to-end harness: `--press STEPS` runs comma-separated steps 0.8 s apart, prints the state
  after each, then quits; real input is ignored meanwhile. Steps: button names (`A,B,X,Y,L,R,L2,R2,Up,Down,Left,Right,
  Start,Select,L3`; `R*15`: that button N times, 80 ms apart; `A~700`: held 700 ms, then let go), `tap:X:Y`, `dtap:X:Y`, `stick:X:Y` (the left stick held at LEFTX:LEFTY, −32768…32767, until `stick:0:0`), `drag:X1:Y1:X2:Y2`, `hold:X:Y`, `down:X:Y` / `move:X:Y` / `up:X:Y` (a finger kept down from step to step: a held slider's look in a `shot:`) (bottom panel's 640×480 units), `info`, `page`,
  `rows` (the browser's rows; on Home the Recent cards, `card …`, then tiles, `tile …`, `>` on the focus), `prop:NAME`
  (any mpv property), `shot:NAME` (both panels to `$BP_SHOTS/NAME-{top,bottom}.png`), `level:volume:40` (or `brightness`: the level card, as the hardware keys bring it), `set:KEY=VALUE` (one setting, as Settings changes it), `flip:KEY=A|B:COUNT:MS` (a setting alternated COUNT times, MS apart, each applied at once: graph switches without the pages), `card:state|off|eq|note|like|fail` (a top-panel card as its feature shows it, for a `shot:`), `gallery:variants[:PAGE]` (the kit's variant components, `Ui::Kit`'s "KIT VARIANTS" block, on one page for a `shot:`; pages 1 to 21: widgets, cards and bars, mini bar / select bar, empty state, keyboard, alert, confirm, busy, Search field and result rows, Music/Videos rows and Books covers, the album grid and artist row, video and folder rows, the Books shelf, Search's page, an album's page (head and track rows), an artist's page, Playlists' rows, the library's empty states, the first-read notice rows, Match album art's backdrop, the tab page (L2 / R2 tabs over a body: Manga's); `B` closes it), `gallery:new` (the redesign's Section rail, Book bar, Away-from-normal marks and, on the top panel, a Preview pane, in place of the page; `gallery:new:2` the Screen-off warning over them and the pane's cover layout; `gallery:off`: back to the page), `screenshot`, `web` (URL, PIN,
  clients). Use `--state-dir` with a scratch dir. The state line can lag a step behind (mpv is asynchronous; the harness
  doesn't wait for it). Steps wait for the file to open, 3 s at most. `timing: <file> started in N ms` log lines give
  start times, on the handheld too.
- **How much to test** (the owner finds the real bugs on the handheld and in Safari, which headless runs miss): every
  change: build, `core-test`, and one check aimed at the change; before a card copy: `tools/web-smoke.sh`, one
  phone-size browser run (WebKit for the browser player) and one scripted handheld run, then copy the card first and let
  the owner test while anything longer runs; the full multi-size browser suite only before a release, in the background.
  Subagents don't run long suites unless asked. When a bug can't be pinned from `log.txt`, add logging that would.
- **Test variables:** `BP_POWER_SUPPLY` (fake `/sys/class/power_supply`), `BP_BACKLIGHT_DIR` (fake `/sys/class/backlight`: `backlight0`/`backlight1` with `max_brightness` and `brightness`), `BP_OSD_FILE` (bananad's level file),
  `BP_FONT_PROVIDER=none` (libass with no system fonts, as on BananaOS), `BP_MANGA_SLOW=MS` (the Manga library read MS ms a series slower, as a slow card: its "Reading your library" line in a `shot:`), `BP_SCREENSHOTS`, `BP_BOTTOM_SIZE=1024x768`,
  `BP_AO_PCM=FILE` (sound out as raw 44.1 kHz s16 stereo, untimed), `BP_CRASH_TEST=segv|abort|throw|overflow` (crashes
  1.5 s after start), `BP_BACKLIGHT_DIR`/`BP_BACKLIGHT_STORE`, `BP_VOLUME_SCRIPT`, `BP_PACKAGE_DIR`/`BP_UPDATE_URL`;
  BananaOS's power side: `BP_RUN_DIR` (fake `/run/banana`; also `bt/audio` for Bluetooth headphones), `BP_BANANAD_CONF`
  (a folder with `etc.conf` and `data.conf` for the two bananad.conf), `BP_IDLE_SLEEP_S`, `BP_SLEEP_SECONDS=N` (every sleep timer length N seconds: its last minute at once), `BP_ASOUND_CARDS` (fake
  `/proc/asound/cards`), and `BP_LID_RESTORE_ONLY=STATEDIR packaging/launch.sh` (only the launcher's lid undo);
  web: `BP_WEB_FORCE=1`, `BP_WEB_PIN`, `BP_WEB_PORT`, `BP_WEB_DIR` (serve `web/` from disk, not the embedded copy),
  `BP_WEB_IP` (`none` = no Wi-Fi), `BP_WEB_FAIL`, `BP_WEB_KEEPALIVE`. Crash log: `<state>/crash.log` (DEVELOPMENT.md).
- **Linux-only code** (evdev touch, lid, inotify, backlight, the volume keys' grab) compiles only on Linux: after touching `touch.*`,
  `sleepwatch.*`, `volumekey.*`, `levelwatch.*`, `panel.*` (or Ui's includes) run the arm64 Docker check in DEVELOPMENT.md ("Linux check").
- **Bundle:** `build-bundle.sh` caches the libraries and ccache's store in the main checkout's `.cache/bundle` (shared by
  every worktree), builds the player in each checkout's `.cache/bp-build` (a commit alone rebuilds one file: ~4 s), and outputs `dist/ports/` and
  `dist/BananaPlayer-<version>-aarch64.zip` (`BananaPlayer/`, `Banana Player.sh`, `media/box/Banana Player.png`). The card
  is `/Volumes/BANANA`; copy into `roms/ports/` with `COPYFILE_DISABLE=1 cp -X` and `dot_clean` (a stray
  `._Banana Player.sh` shows up as a broken port).
- **Releasing:** (1) bump `project(... VERSION x.y.z)` in `CMakeLists.txt`, the README's install links
  (`releases/tag/vx.y.z`, the zip's name) and the `?v=x.y.z` on its screenshot links (images are cached by URL);
  regenerate the pictures first if a pictured screen changed (the Big Buck Bunny trailer, with chapters and subtitles added; the Network shot needs the Docker Samba and
  FTP servers). (2) Commit, push, `build-bundle.sh`, `shasum -a 256` the zip into `<zip>.sha256`. (3)
  `gh release create vx.y.z --target main --latest <zip> <zip>.sha256`, then prepend "Superseded by…" to the previous
  release's notes. (4) The BananaOS Store follows the releases (asset `BananaPlayer-{version}-aarch64.zip`, tag prefix
  `v`; entry and PR text in `packaging/store/`; the PR to JorreFog/BananaLinux isn't opened).

