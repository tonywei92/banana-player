<p align="center">
  <img src="docs/img/banner.png" width="100%" alt="Banana Player: video and music for BananaOS, on the Anbernic RG DS and RG DS Plus">
</p>

<p align="center">
  <b>A dual-screen media player for BananaOS on the RG DS and RG DS Plus: movies, music, photos, manga and books, from your card or your network, with a companion web dashboard.</b>
</p>

<p align="center">
  <a href="#install">Install</a> ·
  <a href="#the-buttons-you-need">Buttons</a> ·
  <a href="FEATURES.md">All features</a> ·
  <a href="#need-help">Help</a>
</p>

<table>
  <tr>
    <td align="center" width="33%"><img src="docs/img/screen-home.png?v=0.9.8" alt="Home in the Dark theme: Recent cards and the sections"></td>
    <td align="center" width="33%"><img src="docs/img/screen-playing.png?v=0.9.8" alt="Big Buck Bunny playing in the Light theme, with its chapters and a subtitle on the top screen"></td>
    <td align="center" width="33%"><img src="docs/img/screen-music.png?v=0.9.8" alt="A song in the Dark theme: matched album art with the Ribbon visualizer"></td>
  </tr>
  <tr>
    <td align="center" width="33%"><img src="docs/img/screen-manga.png?v=0.9.8" alt="A manga page across both screens in the Light theme"></td>
    <td align="center" width="33%"><img src="docs/img/screen-files.png?v=0.9.8" alt="Files in the Dark theme, with the storage cards"></td>
    <td align="center" width="33%"><img src="docs/img/screen-library.png?v=0.9.8" alt="The Music library's Albums grid in the Light theme"></td>
  </tr>
</table>

## What it does

- 🎬 **Movies:** MP4, MKV and more. Switch subtitles and audio tracks mid-film, jump between chapters, pick up where you stopped.
- 🎵 **Music:** a folder plays as an album, with its cover, a visualizer and a 10-band equalizer.
- 🖼️ **Photos:** zoom in and look around with a finger.
- 📖 **Manga:** CBZ, CBR and PDF chapters across both screens, or hold the handheld sideways like a book. An optional extension. Banana Player includes no manga sources: you're responsible for what you add.
- 📚 **Books and documents:** TXT and EPUB in a reader of their own (fonts, sizes, warm filter, bookmarks, find, a book mode across both screens); PDFs page by page or as one long scroll. Even very large text files open at once.
- 🗂️ **Files:** copy, move, rename and delete on the handheld, between both cards, with a progress bar.
- 📡 **Your network:** play straight from your computer or NAS over Wi-Fi, nothing copied.
- 🌐 **Web dashboard:** upload files, make playlists and control playback from your phone or computer.
- 🕶️ **Incognito mode:** while it's on, nothing you play is remembered.
- 🔄 **Updates in one tap**, with your settings kept.

## Visualizers

Fifteen looks for your music, drawn in your theme's colours, picked on **View › Display** (**SELECT**) while a song plays. Each one also works with the album art.

<table>
  <tr>
    <td align="center" width="20%"><img src="docs/img/viz/peaks-yellow.jpg?v=0.9.8" alt="The Peaks visualizer in the Yellow theme"><br><sub>Peaks</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/ribbon.jpg?v=0.9.8" alt="The Ribbon visualizer in the Sakura theme"><br><sub>Ribbon</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/mirror-bars.jpg?v=0.9.8" alt="The Mirror bars visualizer in the Midnight blue theme"><br><sub>Mirror bars</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/faders.jpg?v=0.9.8" alt="The Faders visualizer in the Dark theme"><br><sub>Faders</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/horizon.jpg?v=0.9.8" alt="The Horizon visualizer in the Sunset theme"><br><sub>Horizon</sub></td>
  </tr>
  <tr>
    <td align="center" width="20%"><img src="docs/img/viz/grid.jpg?v=0.9.8" alt="The Grid visualizer in the Dark purple theme"><br><sub>Grid</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/starfield.jpg?v=0.9.8" alt="The Starfield visualizer in the Dark theme"><br><sub>Starfield</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/strings.jpg?v=0.9.8" alt="The Strings visualizer in the Light theme"><br><sub>Strings</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/helix.jpg?v=0.9.8" alt="The Helix visualizer in the Mint theme"><br><sub>Helix</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/sparks.jpg?v=0.9.8" alt="The Sparks visualizer in the Coffee theme"><br><sub>Sparks</sub></td>
  </tr>
  <tr>
    <td align="center" width="20%"><img src="docs/img/viz/bouncy-keys.jpg?v=0.9.8" alt="The Bouncy keys visualizer in the 16-bit theme"><br><sub>Bouncy keys</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/rocket.jpg?v=0.9.8" alt="The Rocket visualizer in the Midnight blue theme"><br><sub>Rocket</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/lanterns.jpg?v=0.9.8" alt="The Lanterns visualizer in the 32-bit theme"><br><sub>Lanterns</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/sleepy-moon.jpg?v=0.9.8" alt="The Sleepy moon visualizer in the Dark red theme"><br><sub>Sleepy moon</sub></td>
    <td align="center" width="20%"><img src="docs/img/viz/dance-line.jpg?v=0.9.8" alt="The Dance line visualizer in the Pink theme"><br><sub>Dance line</sub></td>
  </tr>
</table>

## Install

**From the BananaOS Store (easiest):** open the **Store** on your handheld, find **Banana Player** and install it. The
Store keeps it up to date with each release.

**Or by hand, on the Ports shelf:**

1. **Download** `BananaPlayer-0.9.8-aarch64.zip` from the [latest release](https://github.com/tonywei92/banana-player/releases/tag/v0.9.8).
2. **Unzip it into `roms/ports`** on your SD card. You should see `Banana Player.sh`, a `BananaPlayer` folder and a
   `media` folder there.
3. **Open the Ports shelf** on the handheld and choose **Banana Player**.

Put your videos, music and photos anywhere on the card. Most 1080p videos play smoothly; very high bitrates and HEVC
(x265) may stutter.

## The buttons you need

| Button | What it does |
|---|---|
| **A** | Open, play, pause |
| **B** | Back (on Home: quit) |
| **L / R** | Previous / next |
| **START** | Menu and Settings |
| **Menu/Function** | Quick settings: volume, brightness, web dashboard |
| **F + START** | Quit straight away |

Everything also works by touch, and the bottom line of each screen shows its buttons. The full list is in
**[FEATURES.md](FEATURES.md)**.

## Need help?

Put the SD card in a computer and attach `home/.config/banana-player/log.txt` (and `crash.log` if there is one) to an
[issue](https://github.com/tonywei92/banana-player/issues).

To uninstall, delete `Banana Player.sh`, the `BananaPlayer` folder and `media/box/Banana Player.png` from `roms/ports`.

Want to build it or help out? See [DEVELOPMENT.md](DEVELOPMENT.md).

## Support

<p align="center">
  <a href="https://ko-fi.com/tonysong"><img src="docs/img/kofi-banner.png" width="100%" alt="Support my work on Ko-fi: help me keep building Banana Player and other fun projects for BananaOS. ko-fi.com/tonysong"></a>
</p>

Banana Player is free and always will be. If it makes your handheld better and you'd like to say thanks:
☕ [buy me a coffee on Ko-fi](https://ko-fi.com/tonysong). Entirely optional: every feature is for everyone. ❤️

## Credits

- Made by [tonywei92](https://github.com/tonywei92) for [BananaOS](https://github.com/JorreFog/BananaLinux) by
  JorreFog and the Banana contributors.
- Built on [mpv](https://mpv.io), [FFmpeg](https://ffmpeg.org) and [SDL](https://libsdl.org); network folders with
  [libsmb2](https://github.com/sahlberg/libsmb2) (LGPL 2.1) and [libcurl](https://curl.se).
- The manga reader uses [Lua](https://www.lua.org) (MIT), [lexbor](https://github.com/lexbor/lexbor) (Apache 2.0) and
  [libjpeg-turbo](https://libjpeg-turbo.org) (BSD-style). Its optional manga engine,
  [Suwayomi-Server](https://github.com/Suwayomi/Suwayomi-Server) (MPL 2.0) on a Java runtime, is downloaded only if
  you ask for it.
- The web dashboard uses [cpp-httplib](https://github.com/yhirose/cpp-httplib) (MIT),
  [QR-Code-generator](https://github.com/nayuki/QR-Code-generator) by Project Nayuki (MIT),
  [htm](https://github.com/developit/htm) with [Preact](https://preactjs.com) (Apache 2.0 and MIT) and
  [SortableJS](https://sortablejs.github.io/Sortable/) (MIT).
- Fonts: [Pixelify Sans](https://github.com/eifetx/Pixelify-Sans), [Inter](https://github.com/rsms/inter),
  [Fusion Pixel](https://github.com/TakWolf/fusion-pixel-font) and [Noto Sans](https://notofonts.github.io) Thai,
  Arabic, Hebrew and Devanagari (SIL Open Font License 1.1).
- The film in the screenshots is [Big Buck Bunny](https://peach.blender.org), © copyright Blender Foundation |
  www.bigbuckbunny.org, under [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) (subtitles and chapters added
  for the pictures). The manga page was made for Banana Player; the music and its cover are generated.

## No warranty

Banana Player is free and comes **as is, with no warranty**: use it at your own risk. The authors aren't responsible
for any damage to your handheld, lost files, or anything else that happens from using it. Manga sources are run by
third parties, and Banana Player doesn't host or vouch for what they serve. BananaOS is a separate project that
doesn't endorse Banana Player.

## Licence

[MIT](LICENSE) © tonywei92. You're free to use, change and share Banana Player; copies and forks must keep the
copyright notice and the licence.
