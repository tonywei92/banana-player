# The shell's overlays and settings steppers (2026-11-19)

Three fixes the owner found on the handheld after the shell landed: the Quick settings dialog and Home's
quit question hanging over the rail, the Settings steppers' values sitting off-centre, and "Second card"
crowding the rebuilt pages.

## Quick settings: a dialog inside the shell's main strip

`Ui::quickPanel()` used to be the full panel's centred dialog (`{40, y, 560, h}`), so on a shell page it
covered the left 38 units of the rail. It is now laid inside `lay::shell(false).main`: 8 units in from the
rail and from the right edge (`{86, y, 546, h}` on the 640 x 480 panel), still vertically centred in the
dialog band (0..436). The row, its two stepper buttons, its bar and the Web dashboard's Settings key are laid
from the row's right edge, so they follow the narrower panel without a second set of constants. The outside
tap test is the same `quickPanel()`, and every row's rect is the same `quickRowRect()` the drawing uses.

`drawDialog()` already draws its veil over the whole panel before its box. On a shell page that means the
rail is dimmed uniformly as part of the modal, while the box itself stays right of the rail: nothing is drawn
half over it. I kept the floating dialog rather than turning Quick settings into a bottom sheet, so its
appearance and its rows are unchanged on the player and Menu pages (where `inShell()` is false and the old
40..600 panel is kept).

Home's quit question had the same full-panel coordinates (`QUIT_BOX{40, 119, 560, 198}`). It now asks
`quitBox()` / `quitRow(i)` for a box in `lay::shell(false).main` (8 in from both main edges), and the touches
and the drawing ask the same functions. Its veil comes from `drawDialog`. Because the rail is no longer under
the panel, `Ui::pointer` gives the question every finger before the shell's rail and header (as it did when
its box covered them): a tap outside it still cancels rather than opening a section. The shell draws its
frame after the page body, so the old `drawHints` inside the question was painted over (by a bar that also ran
across the rail); `Home::hintList()` now returns the question's own keys while `quitOpen`, and the shell's
hint bar shows Choose / OK / Cancel.

## Settings rows: the value centred between its arrows

`Kit::settingRow` drew the value at `arrowX + 118` with a 196-unit cut, an offset that was tuned for a
full-panel row at x = 16; in the shell the row starts at x = 94 and the value sat 38 units right of the
stepper's middle (and could run past the panel). The row's boxes now come from `lay::settingStepper(row)`:

- the right arrow 14 units in from the row's right edge (as before),
- the left arrow `STEP_VALUE_W` (238) before it,
- the value's 196-wide box centred between the two arrows, and
- the label's room ending before the left arrow.

The old full-panel tap rule (`SET_ARROW_X`, 360..490) is replaced for rows laid by `settingsRowsIn` /
`settingsRows`: the lift asks `lay::stepperBack`, the left arrow's half of the value. `SET_ARROW_X` stays for
the old full-panel pages that still draw their own arrows (the Manga reader's settings page, the document
settings menu, the Sound page's dialogs). Vertical centring was already `drawTextMid` on the row's middle and
is unchanged for value rows and action rows alike.

## "Second card" → "Card 2"

The root label is now **Card 2** (`browseRoots()`, `src/browser.cpp`), the short half of the This card /
Card 2 pair. The internal path `/run/sd2` is unchanged. It propagates where the root label is shown: the
header crumb (`Files › Card 2 › …`), the Recent rows' `where` note, the Manga library's help line (changed
from the hard-coded "Second card › Manga"), and the web dashboard's places, which share the same label
string. `FEATURES.md` now names Card 2.

Files' top row also no longer prints the place's internal path on the right (`/run/sd2`, `/data`): a root
row's note is empty, so the name has the whole row. `BrowseRoot` grew a `card` flag, and the two summaries
that used to guess a card from a label ending in "card" (`Home::tileSub`, the Files first-page line) ask it
instead — "Card 2" no longer ends in "card", and a folder that happens to be named "… card" is not counted.

## Checked

- macOS build (`cmake --build build -j`) and `./build/core-test`: `core: all passed`. `testKitLayout` gained
  the stepper checks (arrows 238 apart from the row's right edge minus 14, the value's 196 box centred
  between them, the label ending before the left arrow, the back tap's half).
- Four shots, both panels, from `--press` (generated media only, state dirs under the scratchpad):
  - `quick-bottom.png`: F over Home. The panel starts at x = 86, right of the 78-unit rail; the rail is
    dimmed by the dialog's veil and no edge of the panel crosses it. Hints full width at the foot (the veil
    covers the rail there too).
  - `settings-bottom.png`: L, A on Home → Playback. The values ("On", "120%") sit on the middle between the
    left and right arrows.
  - `files-bottom.png`: R on Home with a lab `BP_BROWSE` of `This card` and `Card 2` (a generated clip in
    `Card 2/Videos`). The two place rows read "This card" / "Card 2" and show no paths.
  - `quit-bottom.png`: B on Home. The question's panel is right of the rail, and the hint bar now shows
    Choose / OK / Cancel.
- The screenshots are from the Mac's 1280 x 960 rendering (2 units a pixel), copied out of
  `…/scratchpad/ovshots/` only to read them; they were not added to the repo.

## Not checked

- The handheld at 640 x 480 and KMSDRM, the rail's touch targets on the glass, and whether the dimmed rail
  reads as inactive there.
- The bar rows (volume, brightness) in Quick settings: this Mac has no volume or backlight, so the shot has
  only the dashboard, incognito and Quit rows. Their right-anchored arithmetic is in the code and the tests
  do not render it.
- A real second card (`/run/sd2`); the Files shot uses `BP_BROWSE` basenames, so it checks the row layout and
  the missing path, not `browseRoots()` mounting.
- The Manga library help line's new string on the handheld.
