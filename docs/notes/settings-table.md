# The settings table

A setting is one entry in `settingDefs()` (`src/settings.cpp`, type `SettingDef` in `src/settings.h`), plus its member in
`Settings`. Parsing, writing, clamping, the Settings page's row (label, value text, help, Left/Right) all come from the
entry. No behaviour changed in the move: `settings.conf` is byte-for-byte what it was, and old files read the same.

## What a setting needed before

Four places, by hand, kept in step by convention:

1. `Settings` (`settings.h`): the member and its default.
2. `settings.cpp`: `serializeSettings` and `parseSettings` (two hand-written `if` lines per int, a loop per choice), or an
   entry in `REMEMBER` / `DOC_INTS`; clamping lived in those lines.
3. `ui.h` / `ui_internal.h`: a row id (`TOGGLE_ROW + n`, `VOLUME_MAX_ROW`, `MANGA_FIT_ROW`...; a switch also in the
   parallel `REMEMBER` and `REMEMBER_LABELS` arrays, whose index was its id), and the group's list in `settingRows()`.
4. `ui_settings.cpp`: `settingHelp()`, `changeSetting()` (its stepping), `drawSettingRow()` (label and value text).

`volumeMax` was `Settings` + a parse line + a serialize line + `VOLUME_MAX_ROW` + `settingRows()` + `settingHelp()` +
`changeSetting()` (the 100/120/150 table) + `drawSettingRow()`. `readDim` and `mangaFilter` have no row on this page (the readers draw them), so they needed only the first
two.

## What a setting needs now

The member in `Settings` (its initializer is the default), and one entry in `settingDefs()`, in the place the line
should have in the file (the order of `settings.conf` is the table's order):

```cpp
D::ints("volume-max", &Settings::volumeMax, VOLUME_TOPS, 3)        // key, member, the values the file may name
	.page(SG_PLAYBACK, 40, "Max volume", "How far the volume keys go...")   // group, place in the group, label, help
	.shownAs(nullptr, "%"),                                        // value text: the number, then "%"
```

The helpers: `flag(key, member, noWord, yesWord)` (default `no`/`yes`; `on`/`off`, `left`/`right`... where the file
always had them), `choice(key, member, ids, count)` (stored as an index, read and written by its names in `ids`),
`ints(key, member, values, count)` (only the listed values are read), `range(key, member, min, max, snap)`, `text`,
`custom(key, parse, write)`. Then, for a page row: `.page(group, place, label, help)`, `.shownAs(texts, unit)` (a choice's
names; a flag's two words, off then on, default Off/On), `.stepped(wrap, fn, by)` (Left/Right go round, or by its own
rule, or by `by`; the default stops at the ends), `.afterRead(fn)`. A row's id on the page is `SET_ROW0 + its index`;
`pageRows(group, own)` (`ui_internal.h`) lists a group's rows by place, with the page's own rows among them.

Adding a switch "Foo" to Playback: a `bool foo = false;` in `Settings`, and

```cpp
D::flag("foo", &Settings::foo).page(SG_PLAYBACK, 65, "Foo", "What Foo does."),
```

That is the whole change; the group's list, the help line, Left/Right and the saved file follow. If the new setting does
something besides keep its value (the way Max volume lowers the volume that is over it), the one hook is the
`if (d->... == &Settings::x)` chain after `stepSetting` in `Ui::changeSetting` (`ui_settings.cpp`).

The Clean style is the same shape with its own words: `D::flag("style", &Settings::cleanStyle, "classic", "clean")`
with `.shownAs({ "Classic", "Clean" })`, its row under Theme. Its value is read by the kit's drawing only.

`places` are only an order inside the group (gaps are fine); the page's own rows have theirs in `settingRows()` and
`mangaSettingRows()`. Defaults are not in the table: the member's initializer is the one place.

## What keeps custom code, and why

- **Subtitle style** (six rows, `STYLE_ROW0 + k`): its keys are `substyle.cpp`'s (`serializeSubStyle` comes first in the
  file), its stepping and value text `stepStyle` / `styleValueLabel`. The Subtitles group's preview also hangs on them.
- **Actions** (`CLEAR_ROW` Clear play history, `MANGA_CLEAR_ROW`, `MANGA_JAVA_ROW`, `CHECK_ROW`, `INSTALL_ROW`,
  `EXT_NOTE`) and **Sound FX** (`SOUND_ROW`, a page of its own): not values.
- **Brightness** (`TOP_LIGHT_ROW`, `BOTTOM_LIGHT_ROW`): the backlight's, not in `settings.conf`; and whether *Same for
  both screens* is listed depends on the panels found (`settingRows()`).
- **Hooks in `changeSetting`**: Page fit and Hinge gap push a command to an open reader, Page cache resizes the cache,
  Reading history clears the list when turned off, Max volume warns at 150% and lowers the boost (the file's value is
  the table's; what else happens is the Ui's, which `bpcore` can't hold).
- **In the table but custom**: `visualizer` (`afterRead`: a retired or unoffered style reads as Peaks Banana, `timeline`
  too; Left/Right by `stepVisualizer`, the styles offered), `eq-gains`, `sound-effect` and `update-remind-after`
  (`custom`: their own text; the last is written only once there is a time). `audio-display`, the key from before 0.1.1, is
  read in `parseSettings` beside the table (it sets two settings, and is never written).
- **Quirks kept on purpose** so old files read alike: `manga-preload` reads only its own digits (`digitsOnly`, "03" is
  none), `manga-hinge` ignores a blank value where the others' blank reads as 0 (`ignoreBlank`), the int lists read with
  `atoi` (so "045" is 45).
- **Listed on no page**: the document viewers' choices (`text-*`, `pdf-*`: Files' viewers draw their own rows), `loop`,
  `shuffle`, `manga-reader`, `manga-recent`... They are table entries with no `.page(...)`.

A step from a value outside an int list's values (not reachable from a file, which reads only listed ones) starts from the
first value; before, Max volume and the frame rate each had a nearest-value rule of their own.

## How it was checked

Defaults, a Settings with every member changed, and fourteen hand-made old files (edge values: blanks, "03", "045", out
of range, retired visualizers, `audio-display`, bad words) were read and written back by the build before the table and
by the one after: the output is identical, byte for byte. Settings page screenshots for every group, each row at a value
change in both directions, before and after: identical. `core-test` has `testSettingsTable` (every entry round-trips
at each value Left/Right reaches; the old files' values; places and texts).
