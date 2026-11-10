# Developing Banana Player

For what the player does and how to use it, see [README.md](README.md). This file covers how it's built, run,
tested and laid out.

## How it works

```
             ┌─────────────── banana-player ───────────────┐
 file ──▶ libmpv (FFmpeg demux/decode, A/V sync, libass)  │
             │   render API (OpenGL ES) ─▶ top panel        │  SDL2 KMSDRM: one window per panel
             │   ALSA "default" (bananad picks the output)  │
             │ Ui (SDL_Renderer + SDL_ttf) ─▶ bottom panel  │
             │ buttons (SDL GameController), touch (evdev)  │
             └──────────────────────────────────────────────┘
```

- **libmpv** does the demuxing, decoding, A/V sync, track switching and subtitles (libass). Decoding is software only
  for now (`hwdec=no`, a thread per core).
- **Top panel:** mpv's render API draws into an OpenGL ES context on an SDL window. For audio files, `force-window`
  keeps the window. A visualizer is an mpv `lavfi-complex` graph (`showfreqs`, `showwaves`, `showspectrum`; Cover +
  bars overlays the bars on `[vid1]`, the cover). The title, artist and album are an `osd-overlay`. Auto and Cover +
  bars choose per file at `FILE_LOADED`, once mpv knows whether the file has a cover. `lavfi-complex` is cleared
  before every load, because mpv refuses to switch tracks while a graph holds them.
- **Bottom panel:** `Ui` draws with SDL_Renderer and SDL_ttf. The layout is in 640×480 units (the RG DS panel), and
  text is rendered at the panel's real size, so Pixelify Sans stays sharp on the RG DS Plus's 1024×768 (the same 4:3
  shape, scaled ×1.6).
- **Fonts:** the bottom panel draws with SDL_ttf: Pixelify Sans, else (per text, or per run of characters when no
  one font has them all) Fusion Pixel and Noto Sans Thai/Arabic/Hebrew/Devanagari. SDL_ttf on BananaOS doesn't shape,
  so Arabic and Hebrew names read left to right there. libass (subtitles, titles) is built with fontconfig, and the
  bundled `share/banana-player/fonts.conf` (`FONTCONFIG_FILE`, set by the launcher) lists only the player's fonts:
  that's how it falls back per character with no system fonts. `BP_FONT_PROVIDER=none` shows libass without it.
- **Sound:** ALSA `default`. There's no sound server: bananad writes `/run/banana/asound.conf` (speaker, headphones or
  Bluetooth through bluez-alsa).

### Source

| File | What |
|---|---|
| `src/main.cpp` | Arguments, SDL windows and GL, the event loop, browser/playing modes, scripted runs |
| `src/player.*` | libmpv: options, properties, playlist, resume, history, audio display, photo view and thumbnail, loading spinner, media info |
| `src/ui.*` | The bottom panel: pages (main, photo, tracks, settings groups, browser, info, about), buttons and touch |
| `src/input.*` | SDL keys and pads to the handheld's buttons (Nintendo layout: A is east) |
| `src/touch.*` | The bottom touchscreen from evdev, one finger |
| `src/sleepwatch.*` | Pause on the lid's `SW_LID` and on `SIGCONT` |
| `src/panel.*` | The bottom backlight on/off, restored on quit and on a crash |
| `src/web/*`, `web/` | The web dashboard (`BP_WEB`): the server (cpp-httplib, the gate, PIN sign-in, SSE), its API for files, the player and playlists, the file probe (libavformat) and the address list. `web/` is the browser app, embedded at build time |
| `src/aspect.*`, `tracks.*`, `format.*`, `substyle.*`, `settings.*`, `resume.*`, `history.*`, `playlist.*`, `playlists.*`, `browser.*`, `photo.*`, `webauth.*`, `webpath.*`, `json.*`, `webmedia.*`, `webstate.*` | No window code; covered by `tests/core_test.cpp` |

### BananaOS integration

- **Store apps** run through BananaOS's `run-store-app`. It gives the app its own session, leaves both panels on,
  sets `HOME=/data/home` and `XDG_CONFIG_HOME=/data/home/.config`, and sets `SDL_KMSDRM_FIRST` to the top connector so
  the top panel is SDL's first display.
- **State:** `$XDG_CONFIG_HOME/banana-player/` (on the handheld `/data/home/.config/banana-player/`):
  `settings.conf`, `resume.tsv` (position and tracks per file, the last languages, the last folder) and
  `history.tsv` (30 paths). `--state-dir` overrides it.
- **Panels:** the top panel is the `DSI-2` connector (`BANANA_TOP`, or `BP_TOP_CONNECTOR`; `BP_TOP_DISPLAY` /
  `BP_BOTTOM_DISPLAY` take indexes). The bottom touchscreen is found by its sysfs path, `fe5e0000.i2c` (`BANANA_TOUCH`
  or `BP_TOUCH`), because SDL can't tell the two touchscreens apart. This is how banana-ui does it.
- **Sleep:** bananad sends the process group `SIGSTOP` when the handheld sleeps and `SIGCONT` when it wakes. The player
  pauses on the lid's `SW_LID` event and again on `SIGCONT`, so opening the lid never resumes playback.
- **Bottom screen off:** `bl_power` 4/0 on the backlight that bananad names in `/run/banana/backlight-bottom`, with
  `/run/banana/panel-off` naming it so bananad keeps it dark after a wake. This is the same mechanism as BananaOS's
  `one-panel` launchers. A crash handler turns it back on.

## Build and run on a computer

macOS (Homebrew): `brew install cmake pkg-config mpv sdl2 sdl2_ttf`. Debian/Ubuntu:
`apt install cmake g++ pkg-config libmpv-dev libsdl2-dev libsdl2-ttf-dev libavformat-dev libavcodec-dev libavutil-dev`.
The web dashboard (`BP_WEB`, on by default) needs FFmpeg's libavformat, libavcodec and libavutil through pkg-config (on
macOS they come with mpv); `-DBP_WEB=OFF` builds without it.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build                        # unit tests
./build/banana-player some-movie.mkv          # two windows: the top panel and the bottom panel
./build/banana-player                         # the file browser (your home folder)
BP_BROWSE=~/Videos:~/Music ./build/banana-player   # the browser's top list
./build/banana-player ~/Videos/Show           # the browser, in that folder
```

On a computer the mouse stands in for a finger. Keys: Enter (A), Esc/Backspace (B), the arrows, PgUp/PgDn or Q/E (L/R),
X, Y, S (START), Tab (SELECT).

Options: `--windowed`, `--aspect fit|fill|stretch`, `--lang eng,en`, `--state-dir DIR`, `--font TTF`, `-v` (mpv's
log), `--press LIST` (below).

To check the equalizer by numbers, run a sine at a band's centre through the `lavfi=[...]` part of the `af` string
(`ffmpeg -f lavfi -i sine=f=1000:d=2 -af "<that>,astats" -f null -`) and compare its RMS level with a flat run: the
difference is the gain at that frequency, which `eqResponseDb` gives.

### Scripted runs

`--press` runs steps 0.8 s apart, prints the state after each, then quits. Real keyboard, mouse and touch input is
ignored meanwhile. It works on the handheld over SSH too. The steps are:

- buttons: `A,B,X,Y,L,R,Up,Down,Left,Right,Start,Select,L3`;
- taps and drags on the bottom panel's 640×480: `tap:X:Y`, `dtap:X:Y` (a double-tap), `drag:X1:Y1:X2:Y2`;
- `info`, which prints the Media info rows;
- `page`, which prints the Info or About page as drawn.

```sh
./build/banana-player --press X,Down,A,tap:440:330,drag:24:162:320:162,info,B,B movie.mkv
# press 3: file=1/3 t=3.2 paused=0 speed=1x aspect=Fit audio=Japanese · AAC 5.1 · Surround sub=English · SRT screen=on
```

The state line can lag one step behind, because mpv applies changes asynchronously. A photo's line is
`photo=1/5 4000x3000 zoom=2.00 pan=-0.250,0.000 turn=90 fit thumb=560x420`. Steps wait for the file to open, 3 s at
most, so a file that never opens (a named pipe called `x.mkv`) can test the loading spinner.

### The README's pictures

`docs/make-screenshots.sh <library>` runs scripted scenes (`shot:NAME` captures both panels) on a demo library
(`Movies/`, `Music/<album>/` with `cover.jpg`) and stacks each pair into `docs/img/screen-*.png`. The screenshots use
Blender's *Sintel* trailer (CC BY 3.0). `packaging/icon/make-icon.py` draws the icon from its 32×32 grid, and
`src/logo.h` holds the same grid for the top panel's logo.

### Linux check

The evdev touch, lid and backlight code only builds on Linux. A quick arm64 check (the handheld's architecture) in
Docker:

```sh
docker run --rm -v "$PWD":/src:ro debian:trixie bash -c "apt-get update -qq && \
  apt-get install -y -qq g++ cmake make pkg-config libmpv-dev libsdl2-dev libsdl2-ttf-dev libssl-dev libcurl4-openssl-dev libavformat-dev libavcodec-dev libavutil-dev >/dev/null && \
  cmake -S /src -B /tmp/b -DCMAKE_CXX_FLAGS='-Wall -Wextra' && cmake --build /tmp/b -j && /tmp/b/core-test"
```

### Crash log

A crash (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, an uncaught exception) appends a record to `crash.log` in the state
folder (`home/.config/banana-player/crash.log` on the card) and a `crash: … see …/crash.log` line to log.txt, then the
process dies as it would have (exit status 139 or 134). `src/crashlog.cpp`'s handler runs on its own stack (the main
thread's overflow too) and makes only async-signal-safe calls; what can be is formatted at the start. A record:

```
=== crash 2026-11-09 00:32:13 +0000 ===
version 0.5.1 (a87be23), binary base 0xaaaaaefa0000
signal: SIGSEGV (11), code 1 SEGV_MAPERR, address 0x0, pc 0xaaaaaefa47f4
uptime: 312 s
thread: vo (tid 10330)
playing: /roms/videos/Sintel.mkv
backtrace:
  …/BananaPlayer/bin/banana-player(+0x3df8) [0xaaaaaefa3df8]
  linux-vdso.so.1(__kernel_rt_sigreturn+0x0) [0xffff977ff8c0]
  …/BananaPlayer/bin/banana-player(+0x47f4) [0xaaaaaefa47f4]
  ...
```

The frames above `__kernel_rt_sigreturn` are the handler's own; the one below it is where it crashed. Its size stays
bounded: a record is at most 16 KB (64 frames, 200 characters a line), the handler never writes past 64 KB, and each
start keeps only the newest 32 KB (whole records) once the file is over 48 KB, so the next record always fits. It's the
only file it makes (a trim writes `crash.log.tmp`, renamed over it). A free-space check isn't needed: 64 KB at most.

**Symbolizing.** The bundle's player is stripped; `build-bundle.sh` builds it with `-g` and keeps the unstripped copy in
`dist/symbols/banana-player-<version>` (outside the zip: keep it with each release you hand out). A frame in the player
reads `banana-player(+0xOFFSET)`, the offset in the file, which addr2line takes as it is (in the arm64 Docker image, or
any binutils that reads aarch64 ELF):

```sh
docker run --rm -v "$PWD/dist/symbols":/s:ro debian:bookworm bash -c "apt-get update -qq && apt-get install -y -qq binutils >/dev/null && \
  addr2line -Cfipe /s/banana-player-0.5.1 0x3df8 0x47f4"
```

A frame shown as `banana-player(symbol+0x…) [0xADDR]` instead: give addr2line `0xADDR` minus the record's `binary
base`. The addresses are return addresses, so a line can be the one after the call. Frames in the bundled libraries
(`libmpv.so.2(+0x…)`) need those libraries unstripped, from `.cache/bundle`'s build. Test with `BP_CRASH_TEST=segv`
(also `abort`, `throw`, `overflow`), which crashes 1.5 s after the start. On a Mac the frames come as `atos` reads them:
`atos -o build/banana-player -l <binary base> <address>`.

### FTP backend and bp-net

`net_ftp.cpp` is FTP/FTPS on libcurl's multi interface, polled in our own loop so a wait can be cancelled (`netwait.h`'s
Canceller, shared with SMB). Paths are from the folder the login lands in. A listing is MLSD, or LIST once a server
refuses it (`ftplist.cpp` parses MLSD, Unix `ls -l` and IIS lines); a file's size is SIZE. A file is read by one RETR
from the offset being read, into a 1 MiB buffer (paused when full); a read elsewhere ends it and starts another with
REST, a step forward under 1 MiB is read through. A failed transfer is started again from where it stopped (at once,
then after 1, 2, 4 s) for 20 s, then `Gone`. Sockets are shut down before curl's QUIT so ending a session never waits
on a dead server. FTPS (`ftps` set) is explicit TLS and doesn't verify the certificate (NAS certificates are their
own). Not covered: FTPS against a real server, MLSD against a real server (the test one, vsftpd, has none).

### DLNA backend

`net_dlna.cpp` makes a DLNA/UPnP media server (Plex, Jellyfin, a NAS...) a network folder. The server is found by SSDP
(an M-SEARCH sent to the host's port 1900 and to the multicast group; a description URL in the Server field skips
that) and its ContentDirectory service is browsed with SOAP `Browse` (`BrowseDirectChildren`, DIDL-Lite answers,
read by a small XML reader in the file). DLNA names things by object id and the player's paths are names, so a path
is walked from container "0" by title: two entries of one title get " (2)", and a title with no extension gets one from
the file's MIME type (the browser tells media by extension). A file is an item's HTTP `<res>`, read with Range requests
in 1 MiB blocks, with one retry on a dropped connection. A folder is asked for in one request (Plex takes seconds to
build a list whatever its length), and the listings are shared by every backend of the process for ten minutes, so
mpv's backend finds the file picked in the browser without listing its folder again. When a kept listing leads to a missing object (Plex renumbers its ids when the library is refreshed) the kept listings are dropped and the walk is made again, once. There is no sign-in. Tested on
Plex; `tools/mock-dlna.py` is a stand-in (it can renumber a container, drop HEAD, leave out `res@size`). Not covered: Jellyfin, minidlna, Windows
Media Player, transcoded `<res>`, UPnP servers on IPv6 only.

`bp-net` (built with `BP_TOOLS` and `BP_NET`) tries a backend from a shell, with `ftp://host:port` for FTP and
`dlna://host[:port]` (or `dlna://http://host:port/description.xml`) for DLNA:

```sh
build/bp-net ls ftp://127.0.0.1:2121 tony banana /Media/Movies
BP_OUT=/tmp/o build/bp-net cat ftp://127.0.0.1:2121 tony banana "/Media/Movies/a.mkv" 5000000 700000   # cmp with dd
BP_OUT=/tmp/o build/bp-net reads ftp://... tony banana /f.mkv 14000000:65536,0:65536,7000000:300000       # seeks
build/bp-net loop ftp://... tony banana /f.mkv 65536 0     # sequential; `docker restart` the server meanwhile
build/bp-net cancel-ls|cancel-cat ...                      # with the server `docker pause`d: Cancelled in ~0.4 s
```

## Notes

Dated write-ups of investigations (cause, numbers, what was ruled out, how to verify) are in `docs/notes/`.

## Roadmap

- [x] Video on the top panel, controls on the bottom, by buttons and touch
- [x] Audio and subtitle tracks, subtitle style, Fit / Fill / Stretch, speed, live scrubbing
- [x] Playlist per folder, resume, remember options, file browser, play history, media info, about
- [x] Music: audio files, album art, visualizers
- [x] Pause on lid/sleep, bottom screen off, button hotkeys off
- [x] **Settings in groups:** Subtitles, Music, Photos, Remember, Files, Controls & screen.
- [x] **Photos:** JPG, PNG, WebP, GIF, BMP, TIFF, AVIF, shown by mpv (`image-display-duration=inf`, `video-zoom`,
      `video-pan-*`, `video-rotate`, a `scale` filter to at most 4096 pixels across), upright by their rotation
      metadata or, where FFmpeg leaves it (7.1), their EXIF orientation (`photo.cpp`). The pan area's thumbnail comes
      from `screenshot-raw video`, asked asynchronously. Photos step through photos only, stay out of Recent and keep
      no resume position.
  - [ ] Pinch to zoom: `touch.cpp` reads the first finger only. Does the RG DS's touchscreen report two?
  - [ ] Animated WebP: FFmpeg 7.1 may not decode its animation.
  - [ ] On the handheld: L3 as SDL's `LEFTSTICK`; decode time of a 12 MP JPEG; `dscale=hermite` with
        `correct-downscaling` on Panfrost.
- [x] **Circle visualizer:** `showfreqs` bars, coloured by a `blend` with a gradient, beside a palette for the
      ring, bent round by `remap` with two tables (`circle2-{x,y}.pgm` in the state folder, written once by
      `writeCircleMaps`). Every pixel is one lookup, so it costs what Bars does (about 0.76 ms a frame on an M1).
- [x] **Network: SMB and FTP streaming, a connection manager** (0.3.0; tested on the desktop against Docker Samba and
      FTP and a Linux PC's shares, not yet on the handheld):
  - File list tabs **Local | Network** (L/R switch them; LEFT/RIGHT page). Network lists saved connections; **X** there:
    Add connection, Find on the network; **Y** on a connection: Edit, Test connection, Delete (confirmed).
  - Add: SMB (Find on the network, or an address) or FTP; name, host, port, guest/account, user, password (always
    remembered, sealed with AES-256-GCM, key from a random key file + the SoC serial, as BananaOS's banana-fetch).
    An on-screen keyboard (touch and D-pad; `keyboard.*`). A server found by its name is saved by name and resolved at
    each connection (`netresolve.*`), so a new DHCP address doesn't lose it.
  - Discovery: NetBIOS (node-status sweep of the /24 and broadcast), mDNS (`_smb._tcp`, A for `.local`), WS-Discovery
    (Windows 10/11); the same code resolves a typed name. BananaOS has no mDNS/NetBIOS daemon nor `.local` resolution.
  - Transport: libsmb2 6.2 bundled (LGPL-2.1; async calls only: 6.2's sync wrappers use freed memory when a connection
    fails, as banana-fetch notes); FTP/FTPS through BananaOS's libcurl (multi API). mpv reads them through
    `mpv_stream_cb_add_ro("bpnet")`, URLs `bpnet://<server-id>/<share>/<path>` (no credentials), per-file
    `cache=yes` and a 64 MiB buffer; a failed read is EOF to mpv, so the stream reconnects itself (about 20 s) and the
    player tells a lost connection from a song's end. External subtitles and covers listed and passed as
    `sub-files`/`cover-art-files` (mpv's own auto-loading skips URLs).
  - Robustness tested: a server paused (Buffering, then on), stopped for good (an error after 20 s, never a frozen
    picture), a stalled read while quitting (immediate). FTP paths are relative to the login folder.
  - [ ] On the handheld: Wi-Fi drop and recovery, discovery on a home LAN with a Windows PC (WS-Discovery untested),
        FTPS against a real NAS, MLSD (the test server has none).
- [x] **Web dashboard, phase 1** (`src/web/`, `web/`, `BP_WEB`; off until turned on in Settings, quick settings or the
      Network tab's X menu): a phone or computer on the same Wi-Fi signs in by QR code or PIN (lockout, 7-day sessions,
      private addresses only). Playing: control the handheld, or play in the browser; Files on the card (browse, upload
      files and folders, rename, move, download, delete, multi-select); Playlists (edit, reorder, add from Files); edits
      show live both ways over SSE. Design and layout: `docs/architecture.md`.
  - [ ] Phase 2: network folders in the browser, a photo thumbnail grid, saving EQ presets from the web, subtitles when
        playing in the browser.
  - [ ] On the handheld: the QR code scanned by a phone, sleep and wake with a browser open, a Wi-Fi drop, the globe
        indicator, and the battery cost of a browser watching.
- [x] **Redesign** (0.3.0): one header (`<`, title, battery) and one footer (button names) on every page
      (`drawHeader`, `drawHints`), bitmap icons (`drawGlyph`), Settings as tiles with help lines (Y/X between groups),
      one dialog style, error and locked states, Pixelify's broken "fi" ligature split. `BP_BOTTOM_SIZE=1024x768`
      draws the bottom panel at the Plus's size in tests.
- [x] **Loading spinner** on the top panel (from 0.3 s, till mpv's `playback-restart`), for any file.
- [x] **Chapters:** the file's own chapter markers (`chapter-list`, `chapter`): L2/R2 jump (`chapterTarget` in bpcore),
      a Chapters page, the chapter line on Main, bar marks, and the top-panel card (`osd-overlay` id 2). Audiobooks
      (M4B) play as music, with the chapter under the title.
- [x] **Equalizer and sound effects** (music only): a ten-band equalizer (`eq.*`) with built-in and your own presets,
      live band edits by `af-command`, and the Sound page's Effects tab: Room, Concert hall, Church and Cave (afir
      reverbs with impulse responses synthesized in `soundfx.*`, no recordings), Wide stereo and Headphones (crossfeed).
      The Display popup merges the art and visualizer choices under SELECT.
- [x] **Playlists** (`playlists.*`, `Player`'s `PlaySource`): a Playlists tab between Local and Network; extended M3U files
      in the state folder's `playlists/`, local and network items mixed, Shuffle play, select, add, move and remove, missing
      items skipped while playing. A network item waits up to 1.5 s for its folder's listing (subtitles and covers).
      The file format and edits are unit-tested in `tests/core_test.cpp`.
- [ ] **On the handheld (needs the hardware):**
  - [ ] Does SDL2's KMSDRM give both panels as two displays, with a GLES window on one and a renderer on the other?
        If not, the bottom panel falls back to raw KMS, as banana-ui uses.
  - [ ] Touch: is the bottom touchscreen found by `fe5e0000.i2c`, and do its axes match the panel?
  - [ ] Backlight off/on through `/run/banana/backlight-bottom`, and dark again after a wake.
  - [ ] Lid: does the `SW_LID` pause land before bananad's `SIGSTOP`? If not, does a little audio play out at the wake?
  - [ ] Buttons: does SDL map the ADC joypad as a GameController, with A east?
  - [ ] CPU and dropped frames with software decoding: 480p/720p/1080p H.264, HEVC 8/10-bit, AV1.
  - [ ] Visualizer cost: on an M1 the player used 15–25% of a core with a visualizer and ~5% with album art. If it's
        too much on the RK3568, draw at 320×240 or 20 fps.
  - [ ] Sound on the speaker, headphones and Bluetooth.
- [ ] **App Store package:** bundle libmpv, FFmpeg and libass with their dependencies (SDL2, SDL2_ttf, ALSA and
      FreeType come with BananaOS), cross-built with BananaOS's Buildroot toolchain to match its glibc, plus a Store
      entry script for `run-store-app`. The catalog format (`store/catalog.json`) still needs reading.
- [x] **Software decoding, fast:** mpv's `fast` profile, `vd-lavc-fast`, `vd-lavc-skiploopfilter=nonref`,
      `framedrop=decoder+vo`.
- [ ] **Hardware decoding: blocked on BananaOS.** Its kernel fragment disables `CONFIG_MEDIA_SUPPORT`, so the
      RK3568's Hantro and rkvdec decoders aren't built and there's no `/dev/video*`. The request to build them as
      modules is [BananaLinux#208](https://github.com/JorreFog/BananaLinux/issues/208) (its text:
      [docs/bananalinux-hw-decoding-request.md](docs/bananalinux-hw-decoding-request.md)). Once they
      exist: FFmpeg with the v4l2-request hwaccel (Kwiboo's patches), mpv `hwdec=drm` (zero-copy through EGL
      dma-buf import) with `drm-copy` and software as fallbacks, and `modprobe hantro-vpu rockchip-vdec` in the
      launcher.
- [ ] **Sound that sometimes doesn't open:** when the sound device isn't free as a file starts, mpv plays silently;
      the player now reopens it (`ao-reload`, three tries, logged). Find what holds the device then.
- [ ] A `banana-perf` clock profile while playing.
- [ ] **Later:** an audio delay setting for Bluetooth, holding off idle sleep while playing, music with the lid closed
      (bananad would need to hold the lid's sleep), translations, CJK file names on the bottom panel.

- [ ] **On-device debug mode** (backlog): a Settings switch, off by default, that adds to the web dashboard's API
      (behind its PIN and private-network gate, read-only) a live tail of `log.txt` and reads of mpv properties
      (`current-ao`, `af`, `lavfi-complex`, `audio-out-params`, pause, position), so a bug on the handheld can be
      watched with `curl` from a computer while it's reproduced, with no card swaps.
- [x] **iPhone and iPad:** playing in the page is on there since 0.5.5 (tested by the owner; `NO_HERE` in
      `web/js/store.js` turns it off again).

## Licences

The code is [MIT](LICENSE) © tonywei92. The fonts are under the SIL OFL 1.1 (`assets/fonts/licenses/`): Pixelify Sans
and Fusion Pixel come from BananaLinux's `ui/assets/rnds/fonts`, and Inter from its `ui/assets/banana/fonts`. libmpv
is GPL-2.0+ by default and LGPL-2.1+ when built with `-Dgpl=false`. MIT is compatible with either, but the Store
package must ship the licence of each bundled library and the source offer the GPL requires.

Vendored third-party code keeps its own licences: cpp-httplib (MIT) and nayuki's QR-Code-generator (MIT) in
`third_party/`, and htm with Preact (Apache-2.0 and MIT) and SortableJS (MIT) in `web/vendor/`. `third_party/README.md`
lists each one's version, source, file hashes and licence file. The bundle copies these licences into
`share/banana-player/licenses/`, with the fonts the web dashboard serves (SIL OFL 1.1).
