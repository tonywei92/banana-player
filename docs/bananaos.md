# BananaOS integration

Constraints from BananaOS (github.com/JorreFog/BananaLinux) that this repo can't show; moved out of CLAUDE.md word for word.


- Runs as a Port (`run-port` runs `Banana Player.sh`) or Store app (`run-store-app` runs `launch.sh`), in its own
  session, `HOME=/data/home`, `SDL_KMSDRM_FIRST=DSI-2` (the top panel is SDL display 0). `run-port` darkens the bottom
  backlight; `launch.sh` turns it on. State: `$XDG_CONFIG_HOME/banana-player`; log: `home/.config/banana-player/log.txt`
  on the card (`log.prev.txt` the run before).
- **Lid / sleep:** bananad SIGSTOPs the process group and SIGCONTs on wake; pause on `SW_LID` and `SIGCONT`.
  **Volume/brightness keys:** bananad writes the level to `/run/banana/osd`.
  **Bottom backlight:** `bl_power` at the path in `/run/banana/backlight-bottom`, plus `/run/banana/panel-off`.
  **Bottom touchscreen:** found by sysfs path `fe5e0000.i2c` (SDL can't tell them apart).
  **Brightness:** `backlight1` top, `backlight0` bottom, level = thousandths of `max_brightness` into sysfs, both levels to
  `/data/banana/backlight` ("<top> <bottom>"); no other interface.
  **Volume:** only `/usr/libexec/banana/volume get | set N`, then SIGHUP to bananad; never amixer. `volume.cpp` sets it
  on a thread, latest value only.
  **F** is shared with bananad (F + volume = brightness): quick settings open as F lets go only if nothing came between.
  **Battery:** `/sys/class/power_supply`; an online charger counts as charging (the rk817 gauge says "Discharging").
  **Sound:** no sound server; ALSA's plug maps channels, hence `audio-channels=stereo`.
- SDL's KMSDRM drops the first presented frames: both panels are redrawn repeatedly after start and wake.
- Bundling: libmpv, FFmpeg, libplacebo, libass, fontconfig, expat, harfbuzz, fribidi are bundled. glibc, libstdc++, SDL2,
  SDL2_ttf, Mesa, libdrm, ALSA, FreeType, zlib come from BananaOS and must not be (patched SDL; Mesa needs its libstdc++).
  No fonts on BananaOS: `FONTCONFIG_FILE` points at the bundle's `fonts.conf` (only `assets/fonts`, also
  `sub-fonts-dir`/`osd-fonts-dir`, `osd-font=Inter`).
- Decoding is software only (kernel has no `CONFIG_MEDIA_SUPPORT`; JorreFog/BananaLinux#208, text in
  `docs/bananalinux-hw-decoding-request.md`). The SoC is an RK3568, not RK3566.
- Nintendo layout: A is the east button, `SDL_CONTROLLER_BUTTON_B` (B is SDL's A); X and Y arrive under their printed
  names (`SDL_CONTROLLER_BUTTON_X` is X), not swapped by position. The RG DS Plus has one stick, the left, which clicks
  (L3, `SDL_CONTROLLER_BUTTON_LEFTSTICK`). F is SDL's `GUIDE`; `main.cpp` turns both trigger axes into `L2`/`R2` unless F
  is held (F + L2 screenshot, F + START quits). Back key = B as `SDLK_AC_BACK` (unconfirmed); each press logs `input: ...`.
- The RK3568's Mali-G52 may take no texture over 8192 (unverified), hence photos ≤ 4096 px.
