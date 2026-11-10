# CLAUDE.md

Banana Player: a video and music player for BananaOS (github.com/JorreFog/BananaLinux) on the Anbernic RG DS / RG DS
Plus (RK3568, aarch64, two 4:3 panels, the bottom one touch). C++17, libmpv, SDL2, SDL2_ttf, zlib; a web dashboard
served by the handheld (`src/web/` + `web/`).

**Read before you touch a part** (this file is the map and the one-line rules; the long form is elsewhere):
`docs/architecture.md` (how each part works, and the rules in full), `docs/design/session.md` (playback state),
`docs/bananaos.md` (OS constraints this repo can't show), `docs/workflow.md` (harness, tests, bundle, releases),
`FEATURES.md` (every feature, control and setting: a new one goes there), `DEVELOPMENT.md` (detail, roadmap),
`docs/notes/` (investigations: an agent finishing one adds a note).

## Commands

```sh
cmake -S . -B build && cmake --build build -j       # desktop build (macOS: brew mpv sdl2 sdl2_ttf cmake pkg-config)
./build/core-test                                    # all unit tests incl. the Session fuzz (no filter)
./build/banana-player [FILE|FOLDER] [--state-dir D] [--press STEPS]   # two windows = the two panels
BP_WEB_FORCE=1 BP_WEB_PIN=ACDE BP_WEB_PORT=8093 BP_BROWSE=lib ./build/banana-player --state-dir D   # with the dashboard
packaging/build-bundle.sh [--quick|--clean]          # aarch64 bundle (Docker); --quick: no zip
tools/push-handheld.sh root@<ip>                     # changed files onto the handheld, clean quit, folder swap
tools/web-smoke.sh                                   # the dashboard's API end to end (needs a player as above)
tools/make-sprite.py                                 # web/sprite.svg from the G_* glyphs in src/ui_glyphs.h
```

- `--press` steps (buttons, `tap:X:Y`, `info`, `rows`, `prop:NAME`, `shot:NAME` with `BP_SHOTS`…) and the `BP_*` test
  variables: `docs/workflow.md`. Headless Chrome drives the dashboard (`?debug` exposes `window.__bp`); launch it muted.
- **Test budget:** every change: build, `core-test`, one check aimed at it. Before a release: `tools/web-smoke.sh`.
  The owner finds the real bugs on the handheld and in Safari; no long suites unless asked. A bug not pinned by
  `log.txt`: add logging that would.
- **Linux-only code** (`touch.*`, `sleepwatch.*`, `levelwatch.*`, `panel.*`): run the arm64 Docker check (DEVELOPMENT.md).
- **Releasing:** `docs/workflow.md` (bump `CMakeLists.txt` + README links, bundle, `.sha256`, `gh release`, mark the
  previous superseded). Versions compare numerically (0.5.10 > 0.5.9).

## Where things live

- `src/main.cpp`: SDL, both windows, the event loop. `src/player.cpp`: libmpv, playlist, settings, graphs, overlays.
  `bpcore` = library code with no window (tests in `tests/core_test.cpp`): keep window/mpv-handle code out of it.
- **Session** (`session.*`, bpcore): the one playback state and its pure reducer `apply()` (invariants I1–I13 + fuzz).
  `session_host.*` runs its effects on mpv once per loop pass. Every play/step/stop/output/track/shuffle/loop change,
  from the handheld or a page (`/api/intent`), goes through it.
- **Ui** (`src/ui.h`, split by area over `ui_*.cpp`; shared names `ui_internal.h`, glyphs `ui_glyphs.h`): the bottom panel.
- **Web:** `src/web/` (server, API, probe, subs, picsubs, remux), `web/app.js` + `web/js/*.js` (ES modules, no build),
  `web/style.css` (copies `src/theme.h` by hand), embedded by `cmake/EmbedWeb.cmake`.
- Network: `net*.cpp`, `netstore`, `networker`, `netstream`, `discovery`. Updater: `updater.*`, `update.cpp`.
  Durations: `durations.*`, `durprobe.*`, `durworker.*`.

## Hard rules (the full text and the reasons: docs/architecture.md)

- **mpv:** never wait for mpv on the main thread while a file opens (`prop()`/commands async; track list, tags,
  chapters by `observe_property`). A file's own `start`/`aid`/`sid` go as `loadfile` options. Choose a track again by
  its id (`songAid()`), never by setting "auto" again. mpv's Lua scripts stay off. Photos play in their own mpv
  (main's `photos`, no sound): what plays goes on under a photo (a video with `vid=no`).
- **Music graphs** (`lavfi-complex`): clear before `aid`/`sid`; still inputs are single frames, never `loop=-1`; each
  branch starts with `aformat=sample_rates=44100|48000`, ≤30 fps, no per-frame conversions; time new graphs.
  EQ/effect is one `af` after the graph (`syncEq`).
- **Ui:** redraws only when `signature()` changes (new drawing state goes in it); 640×480 units; colours/radii from
  `theme.h` via `fillRound`/`drawBox`; path checks via `pseudoDir()`.
- **Settings:** a new one goes in `Settings` + the `REMEMBER` table, kept in step with `ui_internal.h` and the row ids.
- **Threads:** background threads never touch `Player`/`Ui`; they push SDL user events. Never block on a server in a
  destructor; the Updater and `WebServer` are never destroyed.
- **Session:** keep `apply()` pure (randomness and time come in on the intent); add an invariant + a named test for
  new state. One output (`pref`), moved only by an explicit choice. Shuffle and Loop are lasting settings; a list's
  order is never shown shuffled. Leaving the player is not stopping; Stop stops everywhere. An edit to the playing
  playlist reaches the session's list through `plTellPlayer` → `SessionHost::playlistEdited` (`Requeue`).
- **Browser output:** a file the browser can't play is asked about when picked (Play on handheld / Cancel) and
  skipped when reached by Next/Previous/an end. The page's clock under a Direct Stream is `bPos()`, never `currentTime`.
- **Web security:** private addresses only, IP-literal Host, `X-BP` + Origin on non-GET, session cookie on `/api/*`,
  paths through `resolveVirtual`, the deny list (state dir, `/data/home`, `/data/banana`, `/data/store`). Server
  threads reach the player only through `WebServer::call`. The session names a playlist by its file's name less
  `.m3u`, never its path (the state dir).
- **Playlists:** Liked is built in (only the heart adds to it). **Network:** `bpnet://<server-id>/<path>`, never
  credentials; passwords sealed by `secret.*`.
- **Fonts:** SDL_ttf has no fallback (`Ui::runs()`); libass uses only the bundle's fonts.
- **Style:** tabs; comments say why. Screenshots and promo use Blender's *Sintel* (CC BY 3.0) and generated media
  only; no copyrighted material and none of the owner's own files. Public text never compares to other apps by name.

## BananaOS (full list: docs/bananaos.md)

Volume only through `/usr/libexec/banana/volume`, brightness only through sysfs + `/data/banana/backlight`; pause on
`SW_LID` and `SIGCONT`; the bottom touchscreen is found by its sysfs path; decoding is software only (no kernel media
drivers); KMSDRM drops the first frames, so both panels are redrawn after start and wake; bundled vs system libraries
as listed there.
