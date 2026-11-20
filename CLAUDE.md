# CLAUDE.md

Banana Player: a video and music player for BananaOS (github.com/JorreFog/BananaLinux) on the Anbernic RG DS / RG DS
Plus (RK3568, aarch64, two 4:3 panels, the bottom one touch). C++17, libmpv, SDL2, SDL2_ttf, zlib; a web dashboard
served by the handheld (`src/web/` + `web/`).

**Read only what your part needs** (this file is the map and the one-line rules; the long form is elsewhere). Don't
read these whole: grep them for the part you touch and read that section. `docs/architecture.md` (how each part works,
the rules in full), `docs/design/session.md` (playback state: read it before touching Session), `docs/bananaos.md` (OS
constraints this repo can't show), `docs/workflow.md` (harness, tests, bundle, releases), `FEATURES.md` (every
feature, control and setting: grep for yours; a new one gets its entry), `DEVELOPMENT.md` (detail, roadmap),
`docs/notes/` (one note per investigation: read the one your brief names; an agent finishing one adds a note).
`src/ui.h` is large: grep it, and work in the area's own header where there is one (`ui_docs.h`, `ui_sound.h`,
`ui_web.h`, `ui_manga.h`, `ui_settings.h`, `ui_browse.h`, `ui_player.h`; the browser's playlists and Home:
`ui_playlists.h`, `ui_home.h`; the page reader Docs and Manga share: `ui_reader.h`; the redesign's components:
`ui_kit.h`).

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
- **Linux-only code** (`touch.*`, `sleepwatch.*`, `volumekey.*`, `levelwatch.*`, `panel.*`): run the arm64 Docker check (DEVELOPMENT.md).
- **Releasing:** `docs/workflow.md` (bump `CMakeLists.txt` + README links, bundle, `.sha256`, `gh release`, mark the
  previous superseded). Versions compare numerically (0.5.10 > 0.5.9).

## Where things live

- `src/main.cpp`: SDL, both windows, the event loop. `src/topscreen.*`: the top panel (the manga reader, the text
  viewer, the frame taken from mpv under their pages, what's drawn each pass, its screenshot). `src/player.cpp`: libmpv, playlist, settings, graphs, overlays.
  `bpcore` = library code with no window (tests in `tests/core_test.cpp`): keep window/mpv-handle code out of it.
- **Session** (`session.*`, bpcore): the one playback state and its pure reducer `apply()` (invariants I1–I13 + fuzz).
  `session_host.*` runs its effects on mpv once per loop pass. Every play/step/stop/output/track/shuffle/loop change,
  from the handheld or a page (`/api/intent`), goes through it.
- **Ui** (`src/ui.h`, split by area over `ui_*.cpp`; shared names `ui_internal.h`, glyphs `ui_glyphs.h`): the bottom panel.
  The browser (Home, Files, Playlists, Network, All recent): `ui_browse.h` (the area `Ui::Browse`; Files' operations
  `ui_fileops.cpp`, the Network form `ui_forms.cpp`), `ui_playlists.h` (`Ui::Playlists`, owned by it: the store, edits
  to the session, select mode, picker), `ui_home.h` (`Ui::Home`, owned by it: Home's strip and tiles, Recent).
- **The redesign** (`docs/redesign/`; how to build a screen: docs/notes/2026-11-16-redesign-kit.md): layout maths
  `kitlayout.*` (bpcore, `lay::`), components `ui_kit.h` + `ui_kit.cpp` (`Ui::Kit`), the More sheet `ui_more.cpp`; the
  player screen `ui_player.h` + `ui_player.cpp` (`Ui::PlayerPage`) is built on them, the other screens not yet.
- **Web:** `src/web/` (server, API, probe, subs, picsubs, remux), `web/app.js` + `web/js/*.js` (ES modules, no build),
  `web/style.css` (copies `src/theme.h` by hand), embedded by `cmake/EmbedWeb.cmake`.
- Books and documents: `pagebook.*` (a chapter in one file: CBZ, CBR via libarchive, PDF via pdfium, a comic EPUB),
  `epub.*`, `textdoc.*` (a TXT mapped, read only around the place shown), `textsource.*`, `textreader.*` (the text
  viewer on the top panel), `ui_docs.h` + `ui_docs.cpp` (Files' TXT/EPUB/PDF viewers: the area `Ui::Docs`).
- Manga and extensions: `ui_manga.h` + `ui_manga*.cpp`, `ui_ext.cpp` (Manage extensions, Home's drawer, the Manga
  pages: the area `Ui::Manga`); `ui_reader.h` + `ui_reader.cpp` (`Ui::PageReader`: the top panel's page reader's channel
  and the views Docs and Manga share); `manga*.*` (store, sources, workers), `reader.*` (the top panel's MangaReader).
- Network: `net*.cpp`, `netstore`, `networker`, `netstream`, `discovery`. Updater: `updater.*`, `update.cpp`.
  Durations: `durations.*`, `durprobe.*`, `durworker.*`.
- Smooth scrolling (a list that follows the finger and glides; docs/notes/2026-11-21-smooth-scroll.md): the motion
  `scroller.*` (bpcore, `lay::Scroller`), the drawing `ui_smoothlist.*` (`Ui::SmoothList`: row slots, scroll frames).
  Music's lists use it; the other lists still scroll by whole rows.
- Native visualizers (drawn by the player, no mpv graph: Peaks, Mirror bars, Dots, Steps, Barcode, Rings, Horizon; their
  graphs are the network fallback): `viznative.*` (bpcore: PFFFT spectrum, each style's bands, easing, shapes), `viztap.*` (the worker's second decode, on mpv's `audio-pts`), `topviz.cpp` (the top panel's GL).

## Hard rules (the full text and the reasons: docs/architecture.md)

- **mpv:** never wait for mpv on the main thread while a file opens (`prop()`/commands async; track list, tags,
  chapters by `observe_property`). A file's own `start`/`aid`/`sid` go as `loadfile` options. Choose a track again by
  its id (`songAid()`), never by setting "auto" again. mpv's Lua scripts stay off. Photos play in their own mpv
  (main's `photos`, no sound): what plays goes on under a photo (a video with `vid=no`).
- **Music graphs** (`lavfi-complex`): clear before `aid`/`sid`; still inputs are single frames, never `loop=-1`; each
  branch starts with `aformat=sample_rates=44100|48000`, ≤30 fps (the styles in `viz_*.cpp`: written
  at 60, drawn at the `visualizer-fps` setting by `vizAtFps`: count time in `n`, `rate=60`, `lagfun`, `tmix` only), no
  per-frame conversions; time new graphs. A `lut`/`lut2` on 8-bit inputs is written `bplut=`/`bplut2=` (its
  expression cached as a quick lookup: `src/vizcache.h`); 16-bit ones stay plain.
  EQ/effect is one `af` after the graph (`syncEq`).
- **Ui:** redraws only when `signature()` changes (new drawing state goes in it); a new dialog or sheet is a
  `Ui::Overlay` (`ui_overlay.cpp`, docs/notes/overlay-stack.md), not a flag checked in `press`/`pointer`/`draw`; an area's state and handlers go in its own struct and header
  (`Ui::Docs`, `ui_docs.h`; docs/notes/ui-areas.md), not `ui.h`; 640×480 units; colours/radii from
  `theme.h` via `fillRound`/`drawBox`; path checks via `pseudoDir()`. A redesigned screen draws with `Ui::Kit`
  on Rects from `kitlayout.h`, which its touches test too.
- **Settings:** a new one is a member in `Settings` + one entry in `settingDefs()` (`settings.cpp`: key, default via the member, range, and the page row's group/label/help); `docs/notes/settings-table.md`.
- **Threads:** background threads never touch `Player`/`Ui`; they push SDL user events. Never block on a server in a
  destructor; the Updater and `WebServer` are never destroyed.
- **Memory:** glibc's mmap threshold stays pinned (`mallopt` in `main`): a big buffer freed goes back to the system.
  Memory is judged on glibc (`tools/leak-loop`, Linux), not by the Mac's allocator.
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
- **Style:** tabs; comments say why. Screenshots and promo use Blender's *Big Buck Bunny* (CC BY 3.0; older pictures *Sintel*) and generated media
  only; no copyrighted material and none of the owner's own files. Public text never compares to other apps by name.

## BananaOS (full list: docs/bananaos.md)

Volume only through `/usr/libexec/banana/volume`, brightness only through sysfs + `/data/banana/backlight`; pause on
`SW_LID` and `SIGCONT`; the bottom touchscreen is found by its sysfs path; decoding is software only (no kernel media
drivers); KMSDRM drops the first frames, so both panels are redrawn after start and wake; bundled vs system libraries
as listed there.
