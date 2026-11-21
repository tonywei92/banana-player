# A song's Repeat mark (2026-11-21)

The owner: Repeat all / one needs an indicator. A song's five chips had no room for Repeat (its fifth is Shuffle), so
Repeat showed only in the List sheet's header.

- **Bottom panel:** `lay::playerLayout(chips, repeatMark)` gives `repeat`, 56 wide at the Next row's right end (the
  line a gap shorter); `Ui::PlayerPage::repeatMarkShown` (a song), touch id `T_REPEAT`, `stepRepeat` (shared with the
  video's Repeat chip: `Player::setSettings`, which the Session follows). Drawn by `Kit::chip` with no label and no
  value: the mark alone in its middle, DIM while Off, ACCENT and the dot on All / One. The tap puts `modeLabel` on the
  top screen, since the mark has no word.
- **Top panel:** `Player::updateOverlay` adds the glyph as an ASS drawing (`glyphPath`, a box per run of cells) in the
  top right corner (`SONG_REPEAT_*` in songlayout.h) while Repeat isn't Off; `setSettings` redraws the overlay when
  the loop changes. Visualizer only's lines stop `SONG_REPEAT_ROOM` short of it.
- **Next line:** one item under a Repeat said "Nothing after this"; it now says "This song again".
- **Not checked:** the mark over each visualizer style's own picture (only Peaks, light, and Album art only, dark,
  were looked at); the handheld's panel (libass draws 2-unit cells as 2 px there, 3 px on the 960 canvas).
