# Architecture (the long form)

Moved out of `CLAUDE.md`, which keeps the one-line rules and the map; the reasoning and detail are here. Nothing was dropped.

- **`main.cpp`** owns SDL: the top window (GL/GLES context; mpv draws into it through its render API), the bottom
  window (an SDL_Renderer that `Ui` draws on), and the single event loop. The loop also handles browser vs playing
  mode (`leave()` leaves the player, what plays going on behind the browser; `stopPlaying()` stops), scripted steps, screenshots, periodic redraws, and the hand-off of events from background threads.
  Background threads (`SleepWatch`, `BottomTouch`, `LevelWatch`, mpv's callbacks) never touch UI state: they push SDL
  user events (`evMpv`, `evRender`, `evTouch`, `evLevel`), or for pause call libmpv directly, which is thread-safe.
- **`Player`** wraps libmpv and owns everything about playing: the playlist (the file's folder, `playlist.*`),
  per-file resume (`resume.tsv`, which also keeps the last audio/subtitle languages and the browser's last folder),
  play history (`history.tsv`, 30 entries), `settings.conf`, music display (`lavfi-complex` graphs: visualizer, art +
  visualizer, a tiny black picture for None; the art choices are decided at `FILE_LOADED`, from whether there's an
  album-art track) and Loop (one: `loop-file`; all: `advance()`), and the top-panel overlays drawn as
  ASS through `osd-overlay` (1: song title, 2: volume/brightness card, 3: idle logo). A file's own choices (`start`, `aid`,
  `sid`) go as `loadfile`'s per-file options (`loadfile <path> replace -1 start=…,aid=…,sid=…`), never as properties
  set before it: a property reaches the file still playing, and an `aid` it doesn't have turns its sound off for good,
  so the next file opened silent (a remembered 3rd audio track, Next from a file with 2). `alang`/`slang` stay
  global. `checkAudio()` picks the track again when a file has audio and none is chosen (mpv drops it when the sound
  device won't open). `lavfi-complex` must
  be cleared before setting `aid`/`sid`; taking a graph off leaves no audio track chosen (restore `aid`), and `vid`
  has to be set by id. A graph's picture never ends, so mpv never ends a song that has one (every song without art):
  `checkSongEnd()` ends it, or restarts it for Loop one, once the clock passes the duration. Circle's graph reads
  two `remap` tables with `movie=` from the state folder (`graphFor()` writes them first; without them it's Bars).
  In art + visualizer (Bars, Waves, Spectrum, Peaks) the art is fitted once and only the 640×200 strip is drawn per frame; time any
  new graph against Art+viz Bars (~0.85 ms/frame on the Mac, ~4 on the handheld), not only Visualizer Bars.
  Offered: Peaks (default), Peaks Banana (same graph, `PeaksColours`), Circle (`VIS_SHOWN`; `stepVisualizer` cycles them). Bars, Waves and Spectrum are retired, code kept:
  a setting naming one reads as Peaks. Peaks draws from 64 numbers (showfreqs measures, `lagfun` on exp() values makes
  the caps fall evenly), at half size with `lut2` against a still height picture, only the luma scaled up.
  Keep visualizers cheap: Circle was shaped by timing `ffmpeg -filter_complex_threads 1` against Bars per frame. (Numbers and the handheld benchmark: docs/notes/2026-11-09-visualizer-performance.md, tools/viz-bench/.)
  Every visual branch starts with `aformat=sample_rates=44100|48000` (mono where channels share a colour; a 192 kHz
  file otherwise costs up to 2× and draws ultrasound), makes at most 30 frames per second of the song (the filter's own
  `rate=`/`fps=`; the graph runs before `speed`, so 2× shows 60/s), and converts no frame: no per-frame `format=`
  but `rgb0` (RGBA's alpha shows as mpv's checkerboard), no `colorkey`. A swscale conversion of 640×480 costs the
  A55s ~7 ms a frame, which the Mac hides: time on the handheld when you can (graphs run single-threaded there).
  A graph's still inputs must be single frames, never `loop=-1`: an endless source floods mpv past the song's end
  (its clock stops, so the song never ends, and memory grows by gigabytes; on the handheld, a freeze). Test such a
  graph in mpv with a memory watchdog: the ffmpeg CLI keeps running endless sources even after the output ends.
  Music's equalizer and effect are one `af`, after the lavfi-complex graph (so the visualizer sees the original sound):
  `@bpin` first, always (over 48 kHz to 48 kHz, the rate ALSA's plug plays at, so the EQ, effect and scaletempo2 never
  run at 96/192 kHz; and the af's output rate, which mpv reopens the output for, never changes with the EQ), then `@bpeq` (10 equalizer biquads plus a pre-gain from bpcore's `eqPeakDb`), then `@bpfx` (the effect), built by `syncEq`
  and `fxPart`. Band edits go live by `af-command` (`syncEq`), which `prop:af` doesn't show. Anything that rebuilds
  the chain (loadfile, lavfi-complex) needs `syncEq(true)` first. Setting the same af string again is a no-op. Reverb
  impulse responses are synthesized by bpcore (`soundfx`) and written to the state dir as `ir-<key>.wav`. Display
  changes are debounced 500 ms (`checkDisplay`).
- **Playlists** play as a `PlaySource` (`Folder`, `NetFolder` or `Playlist`, in `player.h`): `openPlaylist` takes the paths in
  play order (photos dropped as a guard), `updatePlaylist` swaps them in after an edit without reloading, `renameSource`
  follows a rename. For a playlist only, `step()` skips local entries that fail `access(F_OK)`. A network item whose folder
  isn't listed yet waits at most 1.5 s before its `loadfile` (`netWait`), so its subtitles and covers are found:
  `netFolderWanted()` names the folder (the next network item's too, to prefetch), `netFolderListed()` takes the listing,
  and `Ui::tick` drives both.
- **Photos** play through mpv too (`isPhoto`): `PhotoView` (zoom over Fit or Fill, pan in mpv's `video-pan` units,
  a user turn) is applied as `video-zoom`/`video-pan-*`/`video-rotate`; the geometry is in `photo.cpp`. Fill is done
  by zoom, not `panscan`. Everything set for a photo at `load()` (vf, dscale, zoom/pan/rotate) is reset for a video.
  `video-params` is before the `vf` scale and before rotation. The thumbnail is `screenshot-raw video`, unrotated.
  Photos have their own playlist (`siblingVideos` splits photos from videos/songs), no history and no resume.
  **Photos play in a second `Player`** (`main.cpp`'s `photos`: `PlayerOptions::photosOnly`, `ao=null`, no state dir,
  its own `NetStreams`), always, so a photo never stops or reloads what plays: a song goes on (its next one too), a
  video's sound goes on with its picture off (`Player::hideVideo`: `vid=no` mid-file, and `vid=no` as each video's
  `loadfile` option while hidden; back with `vid=auto`, mpv's track switch seeks the new decoder to the clock), a page's
  item stays held. The top panel draws `photos` while a photo is up (`shown()`), and the bottom panel's `Ui` gets
  `photos` as its `Player&` then (main's `shown()`: press, pointer, tick, draw). The mini bar is the exception: `Ui`'s
  `mainPlayer` (set by main) is the player it shows and works on, so over a photo the bar still offers what plays under
  it (a tap on it closes the photo and opens the player). A song's graph still makes frames under
  the photo and mpv waits for each to be taken (else 200 ms stalls): `drainUnder()` renders them into a 16×16 FBO of
  its own, so the photo isn't drawn again per frame. The settings are the player's, copied into `photos` as a photo opens
  and kept in step each pass (a change over the photo, from START's menu, is `setSettings` on the player). Closing it
  (`closePhoto`) goes back to the player when the photo was opened over it (a web pick), else `Ui::backFromPlayer`.
  Measured on the Mac (14 s each): a 640×360 video alone 23% CPU, the same under a photo 14%, a photo alone 8%; max RSS
  152 / 178 / 160 MB.
- **Never wait for mpv on the main thread while a file opens.** As it switches files mpv waits for the main thread
  to draw the top panel (`mpv_render_context_render`); a blocking call meanwhile (`mpv_set_property`,
  `mpv_command`, `mpv_get_property`) holds both until mpv gives up after 200 ms ("not being called or stuck" in
  `-v`), several times a file: Next took 2-3 s on the handheld. So `prop()` and commands are async, the track list
  and tags come by `observe_property` (`MPV_FORMAT_NODE`), `fileReady()` waits for the track list rather than read
  it, and `graphSet` remembers `lavfi-complex`. Reads (`get`) only once a file has started (`checkPhoto`), or on
  the Info page. mpv's own Lua scripts are off: their per-file hooks stalled every open too. A song keeps the last
  one's graph when it's the same (taking it off and on reopens the sound output).
- **Loading:** `loading()` is true from `load()` till mpv's `PLAYBACK_RESTART`; the stale `video-params` and
  `video-out-params` of the last file (or the idle window) can't tell.
- **Chapters** come by `observe_property` (`chapter-list`, `chapter`), as the track list does; `chapterTarget` in bpcore
  computes each L2/R2 jump, a pending chapter covers quick repeated presses, and the L2/R2 card shares `osd-overlay` id 2 with the volume card.
- **`Ui`** is the whole bottom panel (its definitions are split over `src/ui*.cpp` by area: see "Where things live" below): pages (Main, Tracks (SELECT on a video; tabs Chapters, Subtitles, Audio, Picture (Fit/Fill/Stretch): `tracksTabs()`; the Chapters tab is `Page::Chapters`), Up next (`Page::UpNext`: the Session's queue, `upNextList`; a row is a `PlayRequest` of that list), Settings, Browse, Info, About, Exit, Sound (Equalizer & Effects tabs)), START's tabbed
  menu (`inMenu`: Playing (`Page::Playing`, while a song or video plays: Like, Add to playlist…; `menuTabs`), Settings, About; no Quit: quick settings and F + START quit), the Options menu (X), Lock, Screen off. The browser's first page is **Home**
  (`browseDir` `HOMEDIR`, `drawHome`, `homeKey`, `homePointer`): the Recent strip (`recent`, built by `listRecent` from the
  history and resume.tsv's positions and `#duration` lines; cards `CARD_W` wide, `STRIDE` apart, the focused one kept in
  the second slot by `stripTarget`; `setStrip` eases `stripOff` over 120 ms, stepped in `tick()` while `stripT0` is set,
  which is what `animating()` reports) over the four sections' tiles (`homeZone`/`homeStrip`/`homeGrid`, `homeCol` for
  coming back down). The sections are `browseTab` (Files `""`, Playlists `PLROOT`, Network `NETROOT`, Extensions `EXTDIR`,
  a placeholder); `drawSectionHeader` draws their markers (`pipRect`) where tabs were; `openSection` (L/R, a tile, a
  marker) opens one where it was left (`tabDir`; its top if that folder or playlist is gone), `setSection` moves to one without entering (Show in folder, back from a video);
  `atFirstPage()` (a section's top, or All recent, `RECENT`) is where B goes Home. Home's menus are `ctxItems` from
  `CTX_R_PLAY` on (`ctxWide()`). The header's right end is `drawBattery`: the battery, the web's globe, then the clock
  (`clockText`, not over a photo; the minute is in `signature()`); `statusLeft()` is where they start, and titles and
  the section markers end before it. Main over a photo is
  `drawPhoto` (the pan area and `photoBtn`s, ids from `PHOTO_BTN`; `photoPointer`, a flick's glide in `tick()`; a
  drag moves the photo or, `dragBox`, the box). `BACK_ICON` ("<", top left of every page) is B by touch, except
  over what plays, where one tap closes it; no page has another Back. `dropTouch()` (on `showPlayer`/`showBrowser`) swallows only a finger still down:
  swallowing the next touch regardless ate the first tap after opening a file. Settings opens on groups (`setGroup`
  -1, tiles at `groupRect`); `settingRows()` lists a group's row ids, `settingHelp()` their help lines. Pages are
  built from shared pieces (`drawHeader`, `drawTab`, `drawHints` for the footer, `drawFocus` for the cursor's row,
  `drawEmpty`, `drawDialog`, `drawButton`); icons are `G_*` glyphs (rows of `#` cells, `src/ui_glyphs.h`) drawn by `drawGlyph`, snapped
  to the panel's pixels. The look (Midnight, `docs/design/midnight.md`) is in one place: colours by role and radii
  in `src/theme.h` (`src/ui_internal.h` aliases them as `BG`, `TEXT`, `ACCENT`, `BUTTON`…; `player.cpp`'s ASS cards use
  `theme::ass()` and `roundBox()`; `web/style.css` copies the values by hand). Boxes are rounded: `fillRound` (a
  middle and sides as rects, corners one cached anti-aliased quarter-circle texture per pixel radius, tinted and
  flipped), `drawBox` for buttons, tabs, tiles, cards and dialogs (an edge 30% towards TEXT, or ACCENT when focused,
  and a darker lip), `drawLevel` for bars, `fillRoundBand` for a line along a rounded edge. A new box goes
  through these, not `fill`. `BP_BOTTOM_SIZE=1024x768` draws the bottom window at the RG DS Plus's size for screenshots. Layout is in fixed 640×480 logical units, scaled by `sx`/`sy`; text
  is rendered at the real panel size. It redraws only when
  `signature()` changes (or when forced), so any new state that affects drawing must be added to `signature()`.
  `press(Button)` and `pointer(Pointer)` return true for "leave", which `main` turns into leaving the player (it plays
  on behind the browser, `bgPlay`; a photo closes) or, with nothing playing, quitting. The way back is
  `backFromPlayer`: `showPlayer` keeps where the player was opened from (`playerBack`: page, menu tab, settings group,
  cursor, scroll, the menu's own way back; `onPlayer` says the player is up, so pages over it don't overwrite it), and
  leaving, Stop and a list's end with the player up restore it as it was; never opened from a page (started with a
  file) or Show in folder: `showBrowser` on the file. Stop is its own request
  (`takeStopPressed`). See docs/notes/2026-11-10-background-playback.md.
- **Playlists** (`PlaylistStore`, `playlists.*`) are browser pages: `PLROOT` (the Playlists tab) and `PLDIR` (`"\x01pl:"` plus
  the m3u's path) are pseudo-dirs that `pseudoDir()` recognises, so a new path check must go through it. Select, add and
  move are `selMode` states of the browser page, not pages of their own. Edits are debounced 1 s (a dirty flag checked in
  `tick()`) and flushed on leaving and on quit (`flushPlaylist`).
  **Liked** is built in: `<state>/playlists/.liked.m3u` (a name `playlistFileName` can't make; `list()` skips dot
  files and adds it first, `Info::builtin`), newest first, the oldest dropped at `PL_MAX_ENTRIES`. Only the heart
  (`Ui::toggleLike`/`setLikedPath`, `/api/like`) adds to it; Remove there is Unlike; every other edit path refuses it
  (`isBuiltinFile`), and Add to playlist lists `addTargets()` (all but Liked).
- **Web dashboard** (`BP_WEB`, default ON at build time, off at run time; `src/web/`, `web/`): cpp-httplib (split into
  `third_party/httplib/`), nayuki qrcodegen (`third_party/qrcodegen/`), Preact+htm and SortableJS vendored in `web/vendor/`
  (`third_party/README.md` has versions, hashes, licences). `web/` is embedded at build time by `cmake/EmbedWeb.cmake`
  into `web_assets.cpp`; env `BP_WEB_DIR` serves it from disk instead (a reload shows an edit). `WebServer` exists only
  while `Settings::webDashboard` is on: made on first turn-on, never destroyed (like the Updater), so off means no
  object, thread or socket. Server threads never touch `Player` or `Ui`: `WebServer::call` posts an `evWeb` SDL event
  carrying a `WebCall`, which `main` runs on the main thread and the server thread waits for (503 on timeout); only
  async Player calls there, as with mpv. Fire-and-forget news goes by `post` (FsChanged, ClientConnected, PinRotated,
  ClientsChanged). Live updates are SSE on `/api/events`: `webSnapshot` (`main.cpp`, `api_player.cpp`) is built in the
  loop only while clients exist, and published at most 4/s when anything but the clock changed; `notify` carries fs,
  playlists and recent events; a new Session version (`session` in the state) goes out at once. Pre-routing gate in
  `server.cpp`: private remote address, Host an IP literal or localhost (DNS rebinding), `X-BP: 1` and a matching Origin
  on non-GET, and the `bp_s` session cookie on every `/api/*` but login. Sessions and the PIN (`webauth`: PinGate,
  SessionStore) live in `<state>/web-sessions.tsv` and `web.conf`. Paths are virtual `<rootIndex>/<rel>` through
  `webpath`'s `resolveVirtual`; the deny list holds the state dir (it has `servers.key`), `/data/home`, `/data/banana`
  and `/data/store`. httplib's keep-alive poll interval is overridden to 1 s (`CMakeLists.txt`) for battery, and httplib
  ignores SIGPIPE process-wide. bpcore gains `webauth`, `webpath`, `json`, `webmedia`, `webstate`; the app links
  libavformat/libavcodec/libavutil for the file probe (`src/web/probe.cpp`) when BP_WEB, and always for the lists'
  durations (`bpprobe`: `src/durprobe.cpp`; cache and the skipped formats in `src/durations.cpp`, the handheld's
  thread in `src/durworker.cpp`; docs/notes/2026-11-10-file-durations.md). Test hooks: `BP_WEB_FORCE=1`
  (on at start), `BP_WEB_PIN`, `BP_WEB_PORT`, `BP_WEB_DIR`, `BP_WEB_IP` (`none` = no Wi-Fi), `BP_WEB_FAIL` (the server
  won't start), `BP_WEB_KEEPALIVE` (idle seconds), and the scripted `web` step. Icons in `web/sprite.svg` come from the
  same `G_*` glyphs as the bottom panel: regenerate with `tools/make-sprite.py` after changing one.
  **Playing in the browser** (`docs/design/session.md`): one Session holds what plays, where (`out`), where picks go
  (`pref`), the play state, the place and the track picks. A page sends intents to `POST /api/intent` (Open, Step,
  Play, Pause, Pos, Loaded, Ended, CantPlay, SetOutput, SetAudio/SetSub, Stop) and renders from `state.session`
  (`onSession`: a new `load` while it is the output loads `file` at `pos` with `tracksFor()`'s picks; `playV`/`seekN`
  changes play, pause or seek it; `out` leaving stops its element). `SessionHost` runs the effects on the handheld once
  per loop pass: the item held paused for a page (`Player::holdPaused()`, its screens show it; `jumpTo` within its own
  list), seek, play with `ao-reload` for a copy that was only held (`wakeHeld`: opened paused, the handheld's ALSA
  output stayed silent once played while the picture ran on), the picks via `matchTrack`. The bottom panel shows the
  session's place while a page plays (`Ui::shownPosition()` → `posNow`); A (or the Play button) plays/pauses the page
  as it lets go, held 600 ms (`PLAY_HOLD_MS`, `Ui::tick`) it plays here (`takeOver`).
  **Play on** (START › Playing, `ui_menus.cpp`: `outChoices`, `drawPlayOn`): the pages come from
  `WebServer::playTabs()`, each tab's SSE stream registering `?tab=&name=&play=` (`DEVICE_NAME` in `store.js`; the
  server keeps the name per tab, numbers duplicates, never shows it anywhere but the handheld). A pick is
  `SessionHost::sendTo()` (SetOutput from `"hh"`, after `reportHere()`). A page whose `play()` is refused without a tap
  reports `Blocked` (the session's `blocked`: the wish stays playing, `posNow` stops running); it shows Tap to play,
  the handheld's top card and status line say **Waiting for <page>**, and B there is `takeOver`
  (docs/notes/2026-11-10-play-on.md).
  **The page's layout** (`web/style.css`, `docs/design/midnight.md`'s palette): phone (tab bar, floating mini player,
  the full player with its panel as a sheet), tablet from 700 px (icon rail, player bar), computer from 1100 px
  (sidebar with playlists and the handheld's card); `Q_WIDE`/`Q_DESK` in `web/js/store.js` match the CSS's media queries (a phone
  on its side, short and touch, stays a phone). One edged, lipped `.box` (`.btn`, `.surface`, `.yellow`, `.red`);
  covers are the file's art over a generated gradient (`Art`, `GenCover`, keyed on the path). Uploads are the round key
  (`UploadFab`, `UploadSheet`); the page's one `<video>` (`el`) docks in the newest slot on the page through
  `useVideoSlot` (`playing.js`: the player's media box with the ASS canvas, else the phone's small player's or the bar's
  art tile, else `#holder`); `:root.vid-mini` is on while it sits in a tile and hides the overlays drawn over the
  picture (`.assbox`, `psub*`); web-only glyphs (`g-folder-move`) are inline symbols in `web/index.html`.
  **History in `web/js/router.js`:** each entry carries the load's id (`bpDoc`), how many of ours lie behind (`bpIdx`) and
  whether it came from a tap (`bpOk`); the player's chevron goes back only when `canGoBack()` (Safari skips, going
  back, an entry left by a step without a tap, and an entry from an earlier load reloads the page), else it replaces
  (`replaceHash`). iPhone/iPad play in the page too since 0.5.5 (`NO_HERE` false). Overscroll is blocked on the y axis only: a
  sideways block kills the two-finger swipe Back in Chrome.
- **Updater** (`updater.*`, Ports installs; a Store install, under `/data/store`, is the Store's): a thread runs
  BananaOS's `curl` against GitHub's `/releases/latest` (`update.cpp` parses it), downloads the zip and its
  `.sha256`, checks the hash (SHA-256 in `update.cpp`), unpacks it (`unzip`, else `python3 -m zipfile`) and renames it
  to `BananaPlayer.update` beside the package once whole. `launch.sh` swaps it in before the player starts (the old
  folder kept as `BananaPlayer.old`) and starts again when the player exits with 75 ("Restart now"). The pop-up is
  `Ui::popupShown` over the browser; "Not until tomorrow" is `updateRemindAfter` in settings.conf. Tests stand in
  with `BP_PACKAGE_DIR` (the installed folder) and `BP_UPDATE_URL` (a `file://` answer whose asset URLs are
  `file://` too); the `update` step prints its state. The Updater is never destroyed, so a download doesn't hold up
  quitting.
- **Network folders** (SMB, FTP; `BP_NET`, on when libsmb2 is found): a place is `bpnet://<server-id>/<path>`
  (`netpath.*`), never with credentials. `NetStore` keeps `servers.conf` (passwords sealed by `secret.*`: AES-GCM, a
  key file plus the SoC serial). `net.h` is the backend interface (`net_smb.cpp` libsmb2, async calls only, never the
  sync wrappers; `net_ftp.cpp` libcurl multi; `netwait.h` their cancel), one backend per thread. The browser lists
  through `NetWorker` (its thread, answers as `evNet`, a serial per request; Test connection too); mpv reads files
  through `NetStreams` (`mpv_stream_cb`, a backend per file; a failed read is EOF to mpv, so the failure is kept and
  `Player` turns it into an error). Per file: `cache=yes` and `sub-files`/`cover-art-files` from the folder's
  listing (mpv doesn't look beside a URL). Names found by `Discovery` (NetBIOS, mDNS, WS-Discovery) are stored as
  names and resolved at each connect (`netresolve.*`): BananaOS has no DNS server or mDNS daemon. Never block on a
  server in a destructor (a polite SMB disconnect held quitting). Test servers: Docker Samba and FTP (DEVELOPMENT.md);
  `bp-net` and `bp-servers` (tools/) drive them from a shell.
- **The connection form and keyboard** are browser pages (`browseDir` `FORMDIR`, `FINDNET`), so they reuse the list's
  drawing and touch; the on-screen keyboard (`keyboard.*` layout and text, `Ui::kb*`) is an overlay that takes input
  first.
- **Library code with no window** (`aspect`, `tracks`, `format`, `substyle`, `settings`, `resume`, `history`,
  `playlist`, `browser`, `png`, `battery`, `photo`, `update`, `backlight`, `netpath`, `servers`, `secret`, `netstore`,
  `sidecar`, `keyboard`, `ftplist`, `eq`, `soundfx`, `playlists`, `webauth`, `webpath`, `json`, `webmedia`, `webstate`) builds as `bpcore` and is covered by `tests/core_test.cpp` (a hand-rolled `CHECK_EQ`).
  Keep window/mpv-handle code out of these.
- **Settings** are an INI-ish `key = value` file. New switches go in `Settings` plus the `REMEMBER` table in
  `settings.cpp`. In `src/ui_internal.h`, the `REMEMBER`/`REMEMBER_LABELS` arrays, `TOGGLES` and the row ids (`TOGGLE_ROW`,
  `PAN_ROW`, `DRAG_ROW`, `CLEAR_ROW`, `CHECK_ROW`, `INSTALL_ROW`…) in `ui.h` must be kept in step with them, and the row placed in a group in `settingRows()`.

## Where things live

Ui (class declared in `src/ui.h`; file-static names shared by the files are in `src/ui_internal.h`, the `G_*` glyphs in
`src/ui_glyphs.h`, which `tools/make-sprite.py` reads):

| file | holds |
|---|---|
| `ui.cpp` | init/destructor, `press`, `pointer`, `act`, `tick`, `signature` (dispatch and the redraw rule) |
| `ui_draw.cpp` | text (`runs`, `drawText`), `fill`/`fillRound`/`drawBox`/`drawLevel`, `draw`/`renderPage`, header, battery, clock, hints, glyph drawing, `drawButton`, `wrapText`, `capture` |
| `ui_menus.cpp` | rows/`choose`, START menu tabs, Tracks/Chapters pages (`openTracks`, `drawChapters`), Up next (`openUpNext`, `drawUpNext`), quick settings (F), update popup, the browse Options menu's drawing, Exit/About/Info, Like, Playing tab, `volumeLevel` |
| `ui_browse.cpp` | the file list: `enterFolder`, `netListed`, sections/tabs, Options menu items (`openBrowseMenu`, `chooseBrowseMenu`), `drawTracks` (the list), `folderChanged` |
| `ui_forms.cpp` | connection form, found servers, the on-screen keyboard |
| `ui_playlists.cpp` | the Playlists pages, select/move/drag, picker, add, their drawing, edits from the web |
| `ui_settings.cpp` | `settingRows`, `settingHelp`, groups, `changeSetting`, `drawSettings`, `drawPreview` |
| `ui_main.cpp` | the playing screen (`drawMain`, `drawLeave`), photo view, predicted position, `musicKey`, `buttonRect` |
| `ui_sound.cpp` | the Display popup, the Sound page (Equalizer, Effects) |
| `ui_web.cpp` | the Web dashboard page and the header's globe |
| `ui_home.cpp` | Home: the Recent strip, tiles, section header; what plays behind the browser: the mini bar, the playing row's chip |

Web app (`web/app.js` is the entry: shell, App, debug hook; `web/js/` the modules; `index.html` loads `/app.js` as a module):
`store.js` (store, `useStore`, `parseRoute`, layout queries, `NO_HERE`), `router.js` (history bookkeeping), `util.js`, `api.js`,
`connection.js`, `events.js` (SSE, `applySnap`), `session.js` (`boot`, `signOut`), `toasts.js` (toasts, modals),
`browser-player.js` (the in-browser player, media session), `uploads.js`, `icons.js`, `components.js` (hooks, `Modal`, `Seek`, art, sheets),
`login.js`, `playing.js`, `plshared.js` (playlist edits, Like, Add to playlist), `files.js` (+ the upload key), `playlists.js`, `down.js`.
No build step: ES modules served as they are. A name another file needs is `export`ed where it is declared; a `let` is only
assigned in its own module (others read it). `store.js` must not import from other app modules (cycles that touch it
at load time hit the temporal dead zone).


## The rules in full (as CLAUDE.md had them on 2026-11-10)

CLAUDE.md now keeps one line per rule; this is the full text it had, kept here word for word.


- **Never wait for mpv on the main thread while a file opens** (no `mpv_set_property`/`mpv_command`/`mpv_get_property`
  there: mpv waits for the top panel's draw and gives up after 200 ms, several times a file). `prop()` and commands are
  async; the track list, tags and chapters come by `observe_property`; `fileReady()` waits for the track list; read
  (`get`) only once a file has started or on the Info page. mpv's own Lua scripts stay off.
- **A file's own choices (`start`, `aid`, `sid`) go as `loadfile`'s per-file options**, never as properties set before it
  (an `aid` the next file lacks silences it for good). `alang`/`slang` stay global. `checkAudio()` re-picks a lost track.
  `loading()` is true from `load()` till `PLAYBACK_RESTART`; stale `video-params` can't tell.
- **Music graphs** (`lavfi-complex`): clear before setting `aid`/`sid`; taking a graph off leaves no audio (restore
  `aid`); set `vid` by id. A graph's picture never ends, so `checkSongEnd()` ends songs. A graph's still inputs are single
  frames, never `loop=-1` (floods mpv; memory grows, the handheld freezes); test with a memory watchdog. Every visual
  branch starts with `aformat=sample_rates=44100|48000`, makes ≤30 frames/s, and converts no frame (no per-frame
  `format=` but `rgb0`, no `colorkey`). Keep them cheap: time a new graph on the handheld if you can, and against
  Art+viz Bars (~0.85 ms/frame on the Mac, ~4 on the handheld). Circle reads `remap` tables written by `graphFor()`.
- **Equalizer and effect** are one `af` after the graph: `@bpin` (to 48 kHz, always), `@bpeq`, `@bpfx`, built by
  `syncEq`/`fxPart`. Band edits go by `af-command`; anything that rebuilds the chain needs `syncEq(true)` first.
- **Ui redraws only when `signature()` changes:** any new state that affects drawing goes into it. Layout is fixed
  640×480 units scaled by `sx`/`sy`. Colours and radii come from `src/theme.h`; a new box goes through `fillRound`/
  `drawBox`/`drawLevel`/`fillRoundBand`, not `fill`. A new path check goes through `pseudoDir()` (PLROOT, PLDIR, FORMDIR,
  FINDNET are pseudo-dirs). `dropTouch()` swallows only a finger still down.
- **Settings:** a new switch goes in `Settings` plus the `REMEMBER` table in `settings.cpp`; in `src/ui_internal.h` keep
  `REMEMBER`/`REMEMBER_LABELS`, `TOGGLES` and the row ids (`TOGGLE_ROW`, `PAN_ROW`, … in `ui.h`) in step, and place the row
  in a group in `settingRows()` (+ `settingHelp()`).
- **Threads:** background threads (`SleepWatch`, `BottomTouch`, `LevelWatch`, mpv callbacks, web server, `NetWorker`)
  never touch `Player`/`Ui`: they push SDL user events (`evMpv`, `evRender`, `evTouch`, `evLevel`, `evNet`, `evWeb`).
  Never block on a server in a destructor. The Updater and `WebServer` are never destroyed.
- **Memory** (added 2026-11-11, `docs/notes/2026-11-11-reader-memory.md`): `main` pins glibc's mmap threshold at
  256 KB (`mallopt(M_MMAP_THRESHOLD)`). Left dynamic it rises to the largest block freed (a decoded manga page, up
  to 32 MB), after which pages and frames come out of the heaps and stay with the process when freed: 100 MB after
  ordinary chapters, 240 MB after long ones, on Linux. A leak hunt is run on glibc (`tools/leak-loop`: the arm64
  container under Xvfb), since the Mac's allocator behaves differently; the Mac gives the blocks in use.
- **Playlists:** Liked (`.liked.m3u`) is built in: only the heart adds to it; every other edit path refuses it
  (`isBuiltinFile`). Edits are debounced 1 s and flushed on leaving and quit. Photos have their own playlist, no
  history, no resume; they play through mpv (scaled to 4096 px at most).
- **Network:** a place is `bpnet://<server-id>/<path>`, never with credentials; passwords sealed by `secret.*`; libsmb2
  async calls only; names resolved at each connect (no DNS/mDNS on BananaOS).
- **Web security gate** (`server.cpp`, before routing): private remote address, Host an IP literal or localhost, `X-BP: 1`
  and matching Origin on non-GET, `bp_s` session cookie on every `/api/*` but login. Paths are virtual
  `<rootIndex>/<rel>` via `resolveVirtual`; the deny list holds the state dir (`servers.key`), `/data/home`,
  `/data/banana`, `/data/store`. Server threads reach the player only through `WebServer::call` (`evWeb`; async calls).
  The web app is plain ES modules: export a name where it is declared, assign a `let` only in its own module, and keep
  `store.js` free of app imports. `?debug` exposes `window.__bp` (tests use it). The glyph sprite is generated
  (`tools/make-sprite.py`); web-only glyphs are inline in `index.html`. `web/style.css` copies `theme.h` by hand.
- **Playing in the browser:** the handheld holds a paused copy of the page's item (the Session's `HhLoad` with
  `hold`), never moved per report (only the session's Seek/SetOutput seek it); its first play there does `ao-reload`;
  iPhone/iPad play in the page too (`NO_HERE` false since 0.5.5). `store.target` is derived from the Session's `pref`.
- **Tracks in the browser** (docs/notes/2026-11-11-browser-tracks.md): `/api/subs` (WebVTT), `/api/ass` (the whole ASS
  script for JASSUB, vendored in `web/vendor/jassub/`, loaded only when a styled track is chosen) and `/api/fonts` +
  `/api/font` (Matroska font attachments) in `subs.*`; a non-default audio track is a Direct Stream (`remux.*`:
  `/api/remux-start` → `{start}`, `/api/remux` fMP4 from that keyframe, one per tab). The page's clock is then
  `B.remux.base + currentTime`: use `bPos()`/`seekTo()`, never `el.currentTime`. The bundle's FFmpeg needs the mp4 muxer.
- **Leaving is not stopping** (docs/notes/2026-11-10-background-playback.md): `press`/`pointer` returning true is
  `main`'s `leave()`, a screen change (what plays goes on behind the browser, `Ui::bgPlay`); stopping is
  `stopPlaying()` (`takeStopPressed`, `takeStopRequest`, the end of the list), which changes screens only when
  `Ui::playerShown()`. "Back to Main" goes through `home()`/`showPlayer()`, never `p.playing()` alone. Any Stop clears
  the browser hand-off (`Ui::stopped`) without telling the page; the page announces only to a snapshot with a path.
  A photo opened over a song parks it (`Ui::park`); the mini bar (`drawMiniBar`, `miniBarShown`) offers it back.
  (Superseded 2026-11-10: photos play in their own mpv, nothing is parked; back from the player is
  `Ui::backFromPlayer`. See "Photos" above and docs/notes/2026-11-10-photos-over-media.md.)
- **Fonts:** SDL_ttf has no fallback (`Ui::runs()` picks per run); libass uses the bundle's `fonts.conf` (player's fonts
  only). The icon's 32×32 grid lives in both `packaging/icon/make-icon.py` and `src/logo.h`: change both.
- **Code style:** tabs; comments say why, not what. Screenshots/README use Blender's *Sintel* (CC BY 3.0); no
  copyrighted material.


## The map as CLAUDE.md had it (2026-11-10)

- `src/main.cpp`: SDL, both windows, the one event loop (browser vs playing, scripted steps, web calls). `src/player.cpp`:
  libmpv, playlist, resume/history/settings, graphs, ASS overlays. Library code with no window is `bpcore`
  (tests in `tests/core_test.cpp`): keep window/mpv-handle code out of it.
- **Ui** (class in `src/ui.h`; shared names in `src/ui_internal.h`, glyphs in `src/ui_glyphs.h`): `ui.cpp` core (init,
  `press`, `pointer`, `act`, `tick`, `signature`), `ui_draw.cpp` primitives and page pieces, `ui_main.cpp` playing screen
  and photos, `ui_home.cpp` Home, `ui_browse.cpp` the file list and sections, `ui_forms.cpp` connection form and keyboard,
  `ui_playlists.cpp` playlists/select/picker, `ui_settings.cpp`, `ui_menus.cpp` (START, quick settings, popups, Info,
  Like), `ui_sound.cpp` (Display, Sound), `ui_web.cpp`. A table of what is in each: `docs/architecture.md`.
- Web: `src/web/` (server, API), `web/app.js` (entry) + `web/js/*.js` (ES modules, no build step), `web/style.css`,
  `web/vendor/`; `cmake/EmbedWeb.cmake` embeds every file under `web/` into the binary.
- Network: `net*.cpp`, `netstore`, `networker`, `netstream`, `discovery`. Updater: `updater.*`, `update.cpp`.
- Session (`docs/design/session.md`): `session.*` (bpcore) is the one playback state and its pure reducer `apply()`
  (invariants and a fuzz test in core-test; `matchTrack` also in `web/js/match.js`, both run `tests/session_vectors.txt`).
  `session_host.*` (`SessionHost::get()`) owns it and runs the effects on mpv once per loop pass (`run()`,
  coalesced). Opens (main's `routed()`), Step, Ended, Stop, the output (`out`), the selected output (`pref`: only
  SetOutput, an Open that names where, or a tab gone 10 s move it), the track picks, photos and Woke all go through
  it; a page sends intents to `/api/intent` and renders from `state.session` (`onSession`/`renderLoad`). While the
  handheld itself is the output mpv is the clock and reports back (`pass()`). Any new way to play on the handheld
  while a page is the output goes through `takeOver()` (else the I2 repair pauses it).

