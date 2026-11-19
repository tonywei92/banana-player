# Components of the handheld redesign

Every piece that appears on more than one screen of the redesign preview (redesign/preview.html, 123 screens), so each is coded once. Sizes are in the bottom panel's 640 x 480 units (top-panel components: the same units on the top panel). "Code" names the existing building block it maps to; status is one of: exists, reuse / exists, needs a variant / new.

Counts: 48 components: 10 reuse, 28 variant, 10 new. (Lid-shut keys is a rule with nothing to draw, and QR code is a picture of an address: neither has an example in the preview’s Components section.)

In the preview every screen draws these through shared functions and style rules (src/kit.js, src/screens.js, src/style.css beside preview.html), the same ones the Components section renders its examples from.

Colours are roles: every colour named below is a token of src/theme.h, and a theme gives each its value (see Themes). “Yellow”, “cream” and “red” are ACCENT, TEXT and DANGER in the Yellow theme.

Existing blocks referred to: `Ui::Overlay` (src/ui_overlay.cpp, docs/notes/overlay-stack.md), `Ui::Area` (docs/notes/ui-areas.md), `slideGrab` / src/slider.h (docs/notes/sliders.md), `rowCell` / `gridCell` / `drawKeyedLabel` (docs/notes/button-rows.md), `SettingDef` (docs/notes/settings-table.md), `drawDialog`, `drawFocus`, `drawHints`, `drawSlider`, `fillRound` / `drawBox`, and the tokens in src/theme.h.

## Frame

### Header (exists, needs a variant)

- **Used:** every bottom screen (96)
- **Anatomy and states:** 54 tall, at the top. Back key 56 × 48 (radius 10) when there is somewhere to go back to; title Pixelify 25 (19 in the viewers, where it is a file name), one line, cut with “…”; crumb under it, Inter 12, dim; status cluster; More key 56 × 48 at the right edge. Touch area of both keys runs to the panel’s edges (60 × 56). On a shell screen the right slot may hold a Search key (48 × 48, a magnifier) left of the status cluster: the touch way into Search, which L3 opens by the keys.
- **Inputs:** title, crumb, back target, more (yes / no), right slot (tabs’ keys, a badge)
- **Keys and touch:** ‹ = B. ⋯ = X. Nothing else in it is pressed.
- **Code:** drawHeader, drawSectionHeader, drawBackIcon (ui.h). Needs: the crumb line, the More key, room for badges.

### Status cluster and header badges (exists, needs a variant)

- **Used:** every header; badges on 9 screens
- **Anatomy and states:** Right of the title: badges, playing bars (3 × 13), Wi-Fi 16, battery 22 × 12 (2-unit outline; red when low, a bolt when charging), clock Pixelify 15. Badge: 20 tall, padding 0 5, radius 5, Inter 700 11, yellow on 17% yellow; red variant on 20% red. The globe: absent with the dashboard off, a plain 16 icon with nobody signed in, a badge with the count when someone is, a badge with the browser’s name while it plays. Icon in a badge 14–16.
- **Inputs:** which states are on; battery level / charging; clock format
- **Keys and touch:** None, except the globe (opens the Web dashboard page).
- **Code:** drawBattery, drawWebIndicator, the clock and incognito mark in drawHeader. New: the badge shape for states (boost, browser, incognito, Wi-Fi off, read only).

### Section rail (new)

- **Used:** every shell screen (about 50)
- **Anatomy and states:** 78 wide, full height, background RAIL, a 1-unit rule on its right. Cell 68 × 56, radius 10: icon 22 over a label Inter 600 11.5. Active cell: yellow fill, black ink, 3-unit lip. L and R key caps above the first and under the last section; Settings pinned at the foot. A red 11-unit mark on a cell that would stop what plays.
- **Inputs:** sections (icon, label, shown or not), the active one, a warning mark per cell. The sections: Home, Files, Music, Playlists, Books, Manga (while the reader is on), Network. Six cells fit; with seven the rail scrolls so the active one is in view, L and R still stepping all of them
- **Keys and touch:** L / R step the sections (wrapping); a tap opens one. The D-pad never enters it.
- **Code:** New. Replaces the four markers drawn by drawSectionHeader and Home’s tiles (drawHome). Belongs to Ui’s shell, drawn for every page of an area that is a section.

### Hint bar (exists, needs a variant)

- **Used:** every bottom screen and every sheet
- **Anatomy and states:** 30 tall at the foot, a 1-unit rule above. Items centred, 15 apart: key cap(s) then the word, Inter 12.5. Key cap: 19 tall, min 19 wide, 2-unit yellow outline, radius 5, Pixelify 600 12. At most six items; always A, B, X, Y first, then shoulders.
- **Inputs:** list of (keys, word)
- **Keys and touch:** A legend only: not pressed.
- **Code:** drawHints (ui.h). Needs: the key in a boxed cap instead of plain yellow text; Inter for the words.

### Mini bar (exists, needs a variant)

- **Used:** every shell screen while something plays (drawn on 5)
- **Anatomy and states:** 52 tall, 12 from the sides, 36 above the foot; SURFACE with a 2-unit yellow outline, radius 10. Play mark 56 × 42; kind icon 17; title Pixelify 15, a Title marquee on SURFACE, in what the other parts leave; time Inter 12; state pills; SELECT cap; a 3-unit progress line along its foot. With pills it drops the words “Now playing”. Where the bar shows a song’s art (30 square, radius 5, in the kind icon’s place), a song with none gets the Art placeholder at 30 with its 1-unit edge: its BUTTON sleeve shows on the bar’s SURFACE.
- **Inputs:** title, time, paused, pills (speed, look, repeat, EQ, “Playing in …”), progress
- **Keys and touch:** Tap: the player (SELECT). Tap the mark: play / pause. Hold the mark: take over from a browser. Hold the bar: its More sheet.
- **Code:** drawMiniBar (ui.h). Needs: the wider play mark, state pills.

## Keys

### Key (button) (exists, needs a variant)

- **Used:** everywhere
- **Anatomy and states:** Radius 10, 2-unit edge, 3-unit lip. Two layouts by height: 56 tall = icon 19, label Pixelify 16, cap inline, padding 0 8; 60 and taller = icon + label on the first line, the cap under it (drawKeyedLabel). States: normal BUTTON; focused = yellow edge; pressed PRESSED; primary = yellow fill, black ink; danger = red ink (red fill only for Quit); disabled 45%. Minimum short side 56.
- **Inputs:** icon, label, key, kind (normal / primary / danger / flat), enabled
- **Keys and touch:** Acts on lift (slide off to cancel). A on the focused one.
- **Code:** drawBox / fillRound with theme.h’s BUTTON, PRESSED, R_BUTTON, EDGE, LIP; drawButton, drawBarButton; drawKeyedLabel for label + key. Needs: the inline-cap layout for 56-tall keys.

### Button row and grid (exists, reuse)

- **Used:** reader, PDF, comic, photo, player rows, sheets’ key pairs, select bar
- **Anatomy and states:** Rows: 16 from the sides, 8 between keys; widths divide exactly (four of 146, five of 115 + 116, three of 197). The player’s row is Like · Playlist · List · Stop: no View key, since the chips under it open View on each tab and SELECT opens it by the keys. Grids beside a pan area: two columns, 8 apart.
- **Inputs:** area, count, gap
- **Keys and touch:** D-pad moves along it where the page gives the keys focus; each key also has its own button.
- **Code:** rowCell, gridCell (docs/notes/button-rows.md). One function returns each cell’s rect; drawing and touch both ask it.

### Transport key (exists, needs a variant)

- **Used:** every player screen (14)
- **Anatomy and states:** 74 tall. Four of 100 and the play key 176, yellow. Icon 22 (28 on play), label 14 with its cap. Play key variants: Pause / Play / Next (when a file can’t play) / hold-to-take-over (a darker fill growing left to right, a third line Inter 12).
- **Inputs:** icon, label, key, hold progress, third line
- **Keys and touch:** A = the play key; hold A 0.6 s = take over from a browser.
- **Code:** The player grid’s drawButton (BTN_STEP columns). Needs: unequal widths (the play key 176), the hold-fill, the third line.

### State chip (new)

- **Used:** player screens (14), five in a row on a video (Subtitles, Audio, Picture, Speed, Repeat) and on a song (Sound, Sound FX, Display, Speed, Shuffle)
- **Anatomy and states:** 52 tall, SURFACE, radius 10. Icon 16–18; a label in capitals Inter 600 11; the value Pixelify 13 under it. Normal: cream value, no mark. Away from normal: the value yellow and a 6-unit dot top right. Unavailable: 45% (one rule), the reason as its value, nothing else changes. Width from the row: 115, the last 116 (146 where a row has four). On a song’s Display chip the value is a short form, on the chip only: Art, Art + viz, Visualizer, None. The value is one line: the shortest form of the state (a language without its region), cut with “…” at its end; never text under 16 units in a non-Latin script (the interface’s name for the language is used).
- **Inputs:** icon, label, value, away-from-normal, enabled, target sheet tab
- **Keys and touch:** Tap: opens the View sheet on the tab that sets it, with its tab strip: the same sheet SELECT opens, never a second form of the page. Touch only: no D-pad focus. By buttons the same tabs are reached with SELECT (View), then L / R.
- **Code:** New (today: two text lines under the keys, drawn in drawMain). Lay out with rowCell.

### Line key (exists, needs a variant)

- **Used:** player (chapter line, Next line), notice rows
- **Anatomy and states:** A full-width key that reads as a line of text: SURFACE, 2-unit edge, radius 10, 40 tall (chapter line; touch area 56 with the title above) or 50 (Next line). Small label in capitals, icon, text, right note, chevron.
- **Inputs:** label, icon, text, note, target
- **Keys and touch:** Tap: opens what it names.
- **Code:** drawBox with SURFACE; today’s chapter line and track lines in drawMain.

## Lists

### List row (exists, needs a variant)

- **Used:** about 60 screens
- **Anatomy and states:** 52 tall in a page, 46 in a sheet, 60 with two lines (a track: language on the first, name · flags · format on the second). Every line is one line, cut with “…” at its end; a file name is cut in its middle, keeping its ending. The focused row’s name is a Title marquee (one pass). A right-to-left name is drawn right to left inside its run; the row never flips. Radius 10, padding 0 12, gap 11. Slots left to right: lead (icon 20, or a tick 22, a radio 14, a number, a dot 8), name Pixelify 17 (cut with “…”), pills, note Inter 12.5 dim, trail (chevron 14, switch, a small key 44 tall, a grip). Focused: BUTTON fill, 2-unit yellow edge, the name yellow. Dimmed (gone, unavailable, offline): 45%, the reason in the note. Danger: red ink. In Search’s results the part of the name that matched is ACCENT.
- **Inputs:** lead, name, second line, pills, note, trail, focused, dimmed, danger
- **Keys and touch:** UP / DOWN move, LEFT / RIGHT page, A acts, a tap acts on lift, a drag scrolls, a hold starts select mode (file lists).
- **Code:** drawPlRow, drawUpRow, drawRowIcon, drawDialogRow, drawFocus (ui.h). Needs: one row function with the slots below instead of one per list.

### Pill (mark on a row or card) (exists, needs a variant)

- **Used:** about 30 screens
- **Anatomy and states:** Inter 700 11, padding 3 6, radius 5, an optional 12-unit icon. Three tones: ACCENT fill, ON_ACCENT ink = live or wants attention (Playing, Now, Here, Update, 12 / 40); neutral = TRACK fill, TEXT ink = a fact (On card, Added, Yours, Queued, Paused); red = DANGER at 20% (TINT) as the fill, DANGER ink (Gone, Failed, 18+).
- **Inputs:** text, icon, tone
- **Keys and touch:** None.
- **Code:** drawChip, drawMark, drawMangaDlMark, drawAdultChip (ui.h). Needs: the three tones as one function.

### Filter chips and segmented choice (exists, needs a variant)

- **Used:** Home, Books, All recent; Use for, View, Repeat, Show
- **Anatomy and states:** 40 tall, radius 9, padding 0 12, Pixelify 14 (13 when three share a sheet header); 6 apart. Off: TRACK fill, dim ink. On: yellow fill, black ink. As a filter row its touch area is 56 tall.
- **Inputs:** options (label, count), the chosen one
- **Keys and touch:** Filters: L2 / R2 step, or tap. In a row: LEFT / RIGHT change, or tap.
- **Code:** drawRecentTabs, drawTab (ui.h). Needs: use as a two- or three-way choice inside a row.

### Tabs (exists, needs a variant)

- **Used:** sheets (View: Subtitles, Audio, Picture, Speed on a video; Display, Equalizer, Effects, Speed on a song; List; Contents) and sections (Manga, Sources, a source)
- **Anatomy and states:** 44 tall, padding 0 13, radius 10, Pixelify 15 everywhere; off = BUTTON with edge, on = yellow with lip; 6 apart. Key caps at both ends say what steps it: L R in a sheet, L2 R2 in a section. A count may follow the label.
- **Inputs:** labels, counts, the open one, which keys step it
- **Keys and touch:** The named keys step (no wrap); tap a tab.
- **Code:** drawTabs, drawTab, drawTracksHeader, drawMangaTabs (ui.h). Needs: one strip for both uses, with its stepping keys drawn at its ends.

### Switch (new)

- **Used:** Quick settings, sheets, Extensions, the dashboard page
- **Anatomy and states:** 40 × 22, radius 11, 2-unit edge; knob 14. Off: TRACK, dim knob. On: yellow, black knob. It is never the touch target: its row or tile is.
- **Inputs:** on
- **Keys and touch:** A or a tap on its row toggles.
- **Code:** New (today: tick boxes in drawQuick, On / Off words in drawSettingRow).

### Tick and radio (exists, reuse)

- **Used:** select mode, View’s lists
- **Anatomy and states:** Tick: 22 square, radius 6, 2-unit edge; ticked = yellow fill with a black check. Radio: 14 round, 2-unit edge; chosen = yellow ring with a yellow centre. Both are a row’s lead; the row is the target.
- **Inputs:** on
- **Keys and touch:** A ticks; SELECT ticks all (select mode).
- **Code:** The select-mode tick and the Tracks list’s radio (drawPlRow, drawTracks).

### Settings row (stepper) (exists, reuse)

- **Used:** 12 settings groups, Text sheet, forms’ choice rows
- **Anatomy and states:** 46 tall. Label left; value in a stepper right: ‹ value ›, the value centred in at least 118 units. Variants of the right side: a switch, an action note with a chevron, swatches, a five-step bar. The focused row’s help is on the top screen.
- **Inputs:** label, value, kind, help
- **Keys and touch:** UP / DOWN choose, LEFT / RIGHT change in place, A opens an action row; tap the left or right half to step.
- **Code:** SettingDef in settingDefs() gives label, value text, help and Left / Right (docs/notes/settings-table.md); drawSettingRow draws it. New settings (Picture look, Reading’s rows) are entries, not code.

### Form row (exists, needs a variant)

- **Used:** Add connection, Add a server
- **Anatomy and states:** 46 tall: label 150 wide (yellow when focused), then the value. A opens the keyboard for it; choice rows are settings rows. Two keys at the foot, 56 tall: Test (Y), Save (START).
- **Inputs:** label, value, secret, kind
- **Keys and touch:** UP / DOWN choose, A edits, LEFT / RIGHT on a choice row.
- **Code:** ui_forms.cpp (the connection form), drawMangaServerForm. Needs: one form for both.

## Sheets

### Bottom sheet (exists, needs a variant)

- **Used:** about 45 screens
- **Anatomy and states:** Full width from the foot; SURFACE, 2-unit edge, top corners 18; the page behind under a 68% black veil. Grab mark 44 × 4. Header 54: icon 20, title Pixelify 20 yellow (red for danger), a right note Inter 12.5. Body padding 0 14. Its own hint bar. Height by content, 226 to 470; in a tabbed sheet each tab has its own height (the Equalizer takes 470, a song’s Display 452). One of a kind open at a time; a sheet may open over a sheet. A sheet that needs an answer (the first-start lid question) is not closed by a tap on the veil: B gives its safe answer.
- **Inputs:** icon, title, note, body, hints, the key that opened it
- **Keys and touch:** It owns every key while open. B or the opening key closes; a tap on the veil closes. L / R step its tabs.
- **Code:** Ui::Overlay (the stack: keys, touches, draw, signature) with drawDialog for the body (docs/notes/overlay-stack.md). Needs: a sheet look for drawDialog (full width, top corners 18, a grab mark, its own hint bar) beside the centred box.

### Alert sheet (exists, needs a variant)

- **Used:** about forty messages (1 drawn large, 6 small)
- **Anatomy and states:** A bottom sheet 226 tall: info icon, title, one sentence Inter 14, one primary key 56 tall, full width. Red title only when something failed or is dangerous. The key may name the next step.
- **Inputs:** title, text, key label, danger
- **Keys and touch:** A or B closes.
- **Code:** Ui::AlertBox (ui_overlay.cpp:72), opened by alert(title, text). Keep the call; change its draw to the sheet.

### Confirm sheet (exists, needs a variant)

- **Used:** Quit, Delete, Manga + video, 120%, engine offer, updater, first-time notices, incognito with a browser signed in, the first-start lid question (13)
- **Anatomy and states:** A bottom sheet 240–320 tall: title, one or two sentences, two keys 56 tall side by side. The safe key is on the left, yellow, focused, and is B; the action is on the right, red ink when it destroys. An optional “don’t ask again” line.
- **Inputs:** icon, title, text, safe label, action label, danger, foot
- **Keys and touch:** LEFT / RIGHT choose, A does the focused one, B = the safe key.
- **Code:** drawQuitDialog, drawUpdatePopup, the press-twice rows; as an Overlay.

### Busy and job sheet (exists, needs a variant)

- **Used:** Reading folder, copies, moves, downloads, searches (8)
- **Anatomy and states:** A bottom sheet 214–270 tall: a spinner 22 (or the job’s icon), what it is doing, a file name line, a progress bar 12 tall when the amount is known, “x of y · %” left and time left right (Inter 13), one Cancel key 56 tall.
- **Inputs:** title, text, file, share, amounts, can cancel, can hide
- **Keys and touch:** B cancels (or hides a download that carries on).
- **Code:** drawBusy, drawFileJob (ui_fileops.cpp), drawMangaEngineSheet; the Find-in-book job. One overlay for all.

### More sheet (menu) (exists, needs a variant)

- **Used:** X on every screen (drawn on 8)
- **Anatomy and states:** A bottom sheet of 46-tall rows. One column up to six rows. Two columns when it holds both the item (left, titled “This file”) and the place (right, titled “This folder”). On the player only: a first row of four tiles 60 tall (Lock, Screen off, Info, Stop). Unavailable rows: 45% with the reason as the note.
- **Inputs:** title, note, left rows, right rows, tiles
- **Keys and touch:** D-pad moves (two columns: LEFT / RIGHT cross), A chooses, X or B closes.
- **Code:** drawBrowseMenu, drawDialogRow, drawMangaLibMenu, drawMangaHisMenu, drawMangaLocalSheet, drawDocMenu: today one per page. One overlay fed a list.

### Book bar (and sheets held sideways) (new)

- **Used:** Book mode for a book and for a comic (4 screens)
- **Anatomy and states:** A bottom sheet on the touch page, upright as held, 480 wide, about 340 tall: title Pixelify 18 (one line), a progress slider row 36 tall, four keys in two rows (60 tall, about 222 wide: name on the first line, key under it), one full-width key 56 tall to leave Book mode, an upright hint bar. A lit Light key carries its state. Other sheets opened from it (Text, Contents, View, Light) are the same sheet component at 480 wide with 46-tall rows.
- **Inputs:** title, progress, four keys, the leave key, which side the top screen is on
- **Keys and touch:** A tap in the middle of the page, or START, shows and hides it. Directions in its hints are as you see them while holding the handheld.
- **Code:** New. Today a tap in the middle opens Text options (drawDocMenu) or the Reading menu (drawMangaReadingMenu), drawn in the panel’s usual orientation. This is a bottom sheet drawn rotated a quarter turn on the touch panel, 480 wide by up to 640 tall; every sheet needs that rotated draw in Book mode.

### Keyboard (exists, needs a variant)

- **Used:** rename, names, Search (L3: the top screen lists what is found, by kind, as each letter is typed), addresses, passwords
- **Anatomy and states:** A page, not a sheet. Field 48 tall under the header, yellow outline, Pixelify 17. Letter keys 58 × 52, radius 6, ten across, 5 apart, three rows. Bottom row 56 tall: layer keys, Space, ⌫, Done (yellow), each with its button; a field may add whole pieces (https://, .json).
- **Inputs:** title, value, secret, max length, special keys
- **Keys and touch:** D-pad moves, A types, X deletes, Y space, L3 capitals, START done, B cancels.
- **Code:** drawKeyboard (ui.h). Needs: a special row per field, the text echoed on the top screen.

## Values

### Slider (exists, reuse)

- **Used:** progress, reader, pages, Quick settings, Light, Strength, Adjust, EQ (12 screens)
- **Anatomy and states:** Track 14 tall, radius 7, TRACK; fill yellow; knob 8 × 24, radius 3, cream. Hit area: the whole row (56 or more). Held: the knob grows 3 all round and the value shows (lit beside the bar, or a tag 28 above the knob). Marks: chapter notches, the “100” mark. Variants: two-way (a tick at 0, fill from the centre: new drawing, same grab), upright (EQ), with − and + keys 56 × 44.
- **Inputs:** range and step, value, marks, two-way, upright, paced, tag text
- **Keys and touch:** Trackpad-style: a touch alone changes nothing; slide anywhere in the row and it moves from where it was; a tap jumps. LEFT / RIGHT step the focused one. Two-way and EQ: double-tap = 0.
- **Code:** slideGrab, slideHeld, slideShare, drawSlider, drawSlideKnob, drawSlideTag (src/slider.h, ui_slider.cpp; docs/notes/sliders.md). New sliders are an id, a track rect, a hit rect, a range and a setter.

### Progress bar (exists, reuse)

- **Used:** about 40 screens
- **Anatomy and states:** Not touched. Three sizes: a 3-unit line along the foot of a card or the mini bar; 6 tall (radius 3) in rows, cards and top-screen previews; 12 tall (radius 6) in job sheets. TRACK with a yellow fill. On top-screen level cards: 8 tall.
- **Inputs:** share, size
- **Keys and touch:** None.
- **Code:** The download, Find-job, file-job and Recent-card bars (plain fillRound).

### Volume bar (exists, needs a variant)

- **Used:** top card, Quick settings
- **Anatomy and states:** A level bar whose range runs to Max volume: TEXT (cream) to 100%, ACCENT from 100 to 120%, DANGER past 120%; a tick at 100%. The number takes the colour of the part it is in.
- **Inputs:** value, max
- **Keys and touch:** As a slider in Quick settings; the volume keys elsewhere.
- **Code:** drawLevel (top card) and drawQuick’s bar. Needs: the three colours and the 100 mark as one function.

## Cells

### Tile (exists, needs a variant)

- **Used:** Settings (12), Menu (8), Home shortcuts, Quick settings
- **Anatomy and states:** SURFACE, 2-unit edge, radius 14. Settings tile 265 × 58: icon 22, name Pixelify 17, a live summary Inter 12 dim. Menu tile about 146 × 84: icon 26 over name 17 over a note. Shortcut and toggle tiles 56 tall. A choice tile (View › Display on a song: the four modes 147 × 56 with the mark beside the name, Pixelify 13.5; the twenty styles 117 × 56 in a 5 × 4 grid, 6 apart, the mark 22 wide over the name, Pixelify 12) is one of a set: the one in use is filled ACCENT with ON_ACCENT ink, as a segmented choice is; the cursor is the ACCENT edge. A toggle tile may carry a second line Inter 12 dim: its state in words (“Lid or sleep: closes all”) or, at 45%, the reason it can’t be changed (“Off in incognito”). Focused: yellow edge, icon and name yellow.
- **Inputs:** icon, name, summary, focused, mark
- **Keys and touch:** D-pad moves in two dimensions, A opens.
- **Code:** Home’s tiles (drawHome), Settings’ group tiles (drawSettings). Needs: the live summary line.

### Cover cell (exists, reuse)

- **Used:** Books, Manga library, a source’s grid
- **Anatomy and states:** 96 × 136, radius 5, five to a row, 14 apart. A count pill top right (unread). Under it, when in progress: a 4-unit bar and a percent, Inter 12. Focused: a 3-unit yellow outline 2 away. Dimmed 45% while it can’t open. An album is the same cell, square: 96 × 96 with the album’s name (Pixelify 13) and its artist (Inter 11.5 dim) under it, one line each; 120 on the top screen’s artist pane. An album with no cover in its songs: the Art placeholder at the cell’s size (96, 120), radius 5, keyed by the album’s name, so a grid of them is not one drawing repeated; the focus outline as on any cover. A song’s row that shows art takes the placeholder at the row’s art size, with its 1-unit edge. Books and manga keep their drawn covers.
- **Inputs:** image or drawn cover (an album: its art or the Art placeholder), title, author, badge, progress, dimmed
- **Keys and touch:** D-pad moves, A opens, drag scrolls.
- **Code:** drawMangaCover (real cover decoded in the background, a drawn one standing in). Books uses the same.

### Art placeholder (new)

- **Used:** wherever a song’s or an album’s art would be and there is none: the top screen’s Song layout (240 in Album art only, 224 in Art + visualizer), the player’s thumbnail by the artist line (30), the mini bar, list rows, an album’s Cover cell (96; 120 on the artist pane), the Web dashboard’s album covers
- **Anatomy and states:** It follows the theme: flat shapes in theme roles only, no picture, no glyph, no gradient, nothing of its own in any theme. A record in its sleeve, five shapes, back to front, on a square of side s:

  | Shape | Role | Size |
  |---|---|---|
  | The square (the sleeve) | BUTTON | s, with the radius of the art’s box (8 on the top screen, 5 elsewhere) |
  | The record: a disc | BG | D = 72% of s when whole, 88% when half out |
  | Its inner band: a disc | SURFACE | 62% of D (s of 64 or more) |
  | The label: a disc, the one mark | ACCENT | 34% of D |
  | The hole: a dot | BG | 7% of D, at least 2 |

  Diameters are rounded to an even number of units, so every centre is on a whole unit. The discs share one centre and are clipped to the square.
  - **Placement (the variation):** four, so a grid of albums without covers is not one drawing repeated. 0: whole, centred. 1: half out of the bottom edge (centre at the edge’s middle). 2: half out of the right edge. 3: half out of the top edge. Never the left: a right one beside it in a grid would read as one record cut in two. At 88% a half record stays clear of the box’s corners, so a plain clip rect is enough.
  - **Which one:** the top two bits of FNV-1a (32 bits) over the UTF-8 bytes of the key. The key is the album’s name; with none, the title; with none, the file’s name. So an album’s songs, its cell in Music › Albums, the top screen and the dashboard all draw the same one, and it does not change between starts. The preview’s names: Yellow Album 0, Red Room 1, Wheel 2, Field Recordings 3.
  - **Small (s under 64: the thumbnail, the mini bar, a row):** always placement 0, without the SURFACE disc: three shapes on the square. At 30: record 22, label 8, hole 2.
  - **Edge:** the art’s own box and edge where it has one (the top screen: 2 units, edgeOf(SURFACE)). Where the art has none (the thumbnail, the mini bar, a row), a 1-unit edgeOf(BUTTON) edge inside the square, so it still has a shape on a focused row’s BUTTON. A Cover cell has none; focused, the Cover cell’s ACCENT outline.
  - **Per theme:** nothing but the roles’ values. The record is the theme’s ground, so it is the darkest part in a dark theme (a black record, the label lit) and the lightest in a light-bodied one (Light, Sakura, 16-bit, 16-bit Lavender, 32-bit: a paper record on a BUTTON sleeve, the label in the theme’s deep ACCENT). The label on the record and on the sleeve are ACCENT on BG and ACCENT on BUTTON, both checked at 4.5 : 1 or more in every theme (themes.py); the sleeve stands off the ground as much as a key does, and the record off the sleeve by the same step.
  - **Not used here:** TEXT, DIM, DIM_ON, PRESSED (no text, no second state: it is not a key and has no pressed or off drawing of its own; a dimmed cell dims it with the cell).
- **Inputs:** side, key (album, else title, else file name), the box’s radius and edge
- **Keys and touch:** None of its own: it is the art, and takes what the art would.
- **Code:** New: `Kit::artPlaceholder(Rect, key)` on the bottom panel (a `fillRound` and three or four discs, a disc being a `fillRound` with radius half its side, under `SDL_RenderSetClipRect`); the same shapes in the top panel’s GL drawing (a scissor rect; no texture, no image to load or scale); on the Web dashboard an inline SVG of the same five shapes in `--btn`, `--bg`, `--surface`, `--yellow`, with the same sum in JS. Replaces the colour-wheel picture with the note, which was the same in every theme. The placement sum goes in bpcore with a test against the four names above.
### QR code (exists, reuse)

- **Used:** the Web dashboard page (196 square), Settings › About’s support line (84 square)
- **Anatomy and states:** Black on a white square with a quiet edge of white, radius 6 to 10, whole modules only. It holds one address and nothing else. The address is always written out beside it, for typing. On About it is the last block of the page, under a rule: “Banana Player is free. If you’d like to support it:” Inter 12.5 dim, then ko-fi.com/tonysong Inter 15. Only there: no pop-up, reminder, badge or note elsewhere.
- **Inputs:** the address, the size
- **Keys and touch:** None. The handheld opens nothing; the phone that scans it does. Nothing is fetched.
- **Code:** The dashboard page’s QR drawing (ui_web.cpp), given the support address, a constant. On the Web dashboard the same address is a plain link at the foot of Home (web/js/home.js), opened in a new tab with no referrer; no QR there.

### Recent card and Continue card (exists, needs a variant)

- **Used:** Home and its states
- **Anatomy and states:** Recent card 112 × 100, radius 12: a 48-tall picture with a kind mark, title 14, note 12, a 3-unit progress line. Continue card 108 tall, radius 14: picture 128 × 84, a label in capitals (Continue / Now playing / Stopped for Manga), title 22, note 12.5, a bar, one primary key 118 × 52.
- **Inputs:** picture, kind, title, note, progress, label, key
- **Keys and touch:** A plays or resumes; X (or hold) its More sheet.
- **Code:** Home’s Recent cards in drawHome. Needs: the Continue card.

### Empty state (exists, needs a variant)

- **Used:** Books, an empty folder, Repositories, Network, Playlists, Recent, Home on a fresh install (“Nothing played yet”: Browse Files on A, Web dashboard on Y)
- **Anatomy and states:** Centred in the page: icon 54 (dim), title Pixelify 24, one or two lines Inter 13.5 dim, at most 400 wide; two keys 200 × 56, the first primary on A, the second on Y. The how-to (three numbered steps) is on the top screen.
- **Inputs:** icon, title, text, key 1, key 2
- **Keys and touch:** A = the first key, Y = the second.
- **Code:** drawEmpty (ui.h). Needs: the two keys and the top-screen how-to.

### Notice row (exists, needs a variant)

- **Used:** Manga loading / ready / failed, No Wi-Fi, a card taken out
- **Anatomy and states:** A line key that is not pressed: 52 tall, radius 12, SURFACE; edge yellow (ready), red (failed, offline) or plain (working). Icon 20, text Pixelify 15, a second line Inter 12, an optional 5-unit bar, an optional key 44 tall at its right. 60 tall when it has a second line and a key.
- **Inputs:** tone, icon, text, second line, share, key
- **Keys and touch:** Its key only (Y, or a tap).
- **Code:** drawExtNotice, drawMangaToast (the chip that counts seconds). Needs: a row in the page that stays, with an optional bar and key.

### Select bar and clip bar (exists, needs a variant)

- **Used:** select mode, a held file
- **Anatomy and states:** At the foot of the list, above the hints. Select bar: four keys 56 tall (Playlist, Copy, Move, Delete in red). Clip bar: 60 tall, yellow outline, what is held and its size, two keys 46 tall (… here on SELECT, Cancel on X).
- **Inputs:** mode, count, held item
- **Keys and touch:** SELECT = all / put here; X = More / Cancel while a file is held.
- **Code:** drawSelBar, drawSelHeader, drawClipBar (ui.h).

### Pan area and drag area (exists, reuse)

- **Used:** photo, PDF, book, comic
- **Anatomy and states:** Pan area 388 wide, radius 12, BUTTON fill: the page or photo small, a 3-unit yellow box on the part shown. Drag area: full width, 258 tall, radius 14, SURFACE, a dashed middle line and three captions.
- **Inputs:** thumbnail, the shown rect; captions
- **Keys and touch:** Drag the box (or the photo), tap to look there, double-tap 200%. Drag area: drag scrolls and glides, tap halves page.
- **Code:** The photo and document viewers’ pan box and drag area (ui_docs.cpp, drawPhoto), with gridCell for the keys beside.

## Top screen

### Level card (exists, needs a variant)

- **Used:** volume, brightness, zoom, chapter, equalizer, look, adjust
- **Anatomy and states:** Top right, 20 from the edges, 290 wide (200 with no bar), TOP_CARD with a TOP_EDGE edge, radius 18, padding 14 18. A small label Inter 12 dim over the value Pixelify 19, or icon + name + value on one line; a bar 8 tall under it; an optional third line Inter 12. Fades after a moment.
- **Inputs:** icon, label, value, share, tone, third line
- **Keys and touch:** Shown by the key that changed the level.
- **Code:** drawLevel and the chapter / EQ cards in main.cpp (ASS on the top panel, theme.h’s TOP_ tokens, R_TOP_CARD). Needs: TOP_TEXT (cream) for normal levels.

### Note (exists, reuse)

- **Used:** Like, speed, skipped, finished, copied, a look that is on when a video starts, a browser signing in or out, wrong PINs
- **Anatomy and states:** Bottom centre, 26 up (above a subtitle): the card’s body, one line Pixelify 19 yellow (red for a security or failure note) with an optional icon, an optional second line Inter 13. Stays 3 s; 6 s for a browser signing in. Each line is cut with “…” (a file name in its middle). A wide variant (20 from both sides, Inter 13) for a sentence that explains, kept until the next key.
- **Inputs:** text, icon, second line, wide, sticky
- **Keys and touch:** None.
- **Code:** The short OSD lines in player.cpp / main.cpp (“This is the first one”, “Added to Liked”).

### State card (exists, needs a variant)

- **Used:** browser output, buffering, opening, can’t play, lost connection, end of chapter
- **Anatomy and states:** The whole top screen, centred column from 104–150 down: a 72 tile with an icon (or a spinner 54, or a red icon 48), a title Pixelify 26–29 (yellow; red on failure), one line Inter 15–16, an optional bar, a hint line Inter 14 with its key.
- **Inputs:** icon or spinner, tone, title, line, share, hint
- **Keys and touch:** The key it names (A retries, hold A takes over, B goes back).
- **Code:** The top panel’s “Playing in your browser”, Buffering, Finished and error cards (player.cpp, ui_main.cpp:395). One layout for all.

### Preview pane (new)

- **Used:** every shell screen with nothing playing (about 40); and over whatever plays while Settings › Power & screens › Theme is focused (the picture is back when the cursor leaves the row)
- **Anatomy and states:** Brand mark top left (22 tile + name 15). Then one of two layouts: text (a label in capitals Inter 600 11 yellow, a title Pixelify 28, a sentence Inter 14.5, a two-column list Inter 13.5, a key line) from 74 down, 44 from the sides; or cover (a 196 × 278 cover at 36, 66 with the same text column from 262). A picture only when one already exists. The welcome on a fresh install is the text layout with the app’s icon (84 square, radius 20) beside the name in place of the brand mark; the first-start lid question’s pane is the plain text layout.
- **Inputs:** label, title, text, rows, hints, cover, picture
- **Keys and touch:** None: it follows the cursor.
- **Code:** New. The top panel is mpv’s overlay today (the idle logo card in player.cpp); this needs a laid-out card with an optional picture while nothing plays.

### Song layout (exists, needs a variant)

- **Used:** every song screen
- **Anatomy and states:** Title, artist and album are always shown (28 / 17 / 14): there is no setting. The title is a Title marquee in its line’s box (the text column beside the art; the panel’s width less its margins in Visualizer only and None, where it is centred while it fits); artist and album are cut with “…”. Album art only: art 240 square left, the three lines right, the group centred on its real width with 40 margins, the text centred on the art. Art + visualizer: the same with art 224, in the space above the visualizer, which is drawn along the foot (about 150 tall) straight on the ground: no strip, no frame. Visualizer only: the style over the whole screen, the three lines top left over it. None: the title alone, centred, artist and album under it. No art: the Art placeholder in the art’s place, at the art’s size (240, 224), in the same radius-8 box with the same 2-unit edgeOf(SURFACE) edge, keyed by the song’s album; the layout and the chosen mode stay. The player’s 30-unit thumbnail by the artist line is the same placeholder at 30. Ground: BG, all the way down, in Album art only, Art + visualizer and None, with the lines in TEXT, DIM_ON and DIM and a 2-unit edgeOf(SURFACE) edge round the art. The visualizer over it is each style’s over-the-art drawing (only its marks, the ground showing between them), with BG where the art was. Peaks and the six simple styles (Mirror bars, Wave, Dots, Levels, Area, Radial) draw their marks in ACCENT and TEXT; the pixel-art styles and Pocket console keep their own colours. Visualizer only: Peaks and the simple styles on BG in the same colours, with the lines in TEXT, DIM_ON and DIM; a style that paints the whole screen is its own picture, the lines over it white with their shadow.
- **Inputs:** Show (Album art only / Art + visualizer / Visualizer only / None: four icon tiles on View › Display), the visualizer’s style (the twenty icon tiles under them), art or none, title, artist, album. Show and the style are two inputs, set on one page (View › Display) and in two rows of Settings › Music & sound (Music display, Visualizer), and never written as one value. With Album art only or None the style is kept, can be changed, and is marked “not shown now”, not dimmed.
- **Keys and touch:** SELECT steps Show (on View › Display).
- **Code:** The music graph and the title lines in player.cpp (Player::shownMusic). A change to today’s layout: the lines always drawn (the Song info on top setting and its `SettingDef` go), and a ground in BG under the art.

## Rules

### Focus (exists, reuse)

- **Used:** everywhere
- **Anatomy and states:** One meaning, three drawings by what is focused: a key or tile = its edge turns ACCENT (yellow in the Yellow theme); a row = BUTTON fill, ACCENT edge, ACCENT name; a cover = a 3-unit ACCENT outline 2 away. Never a second colour, never a glow, in any theme.
- **Inputs:** kind
- **Keys and touch:** The D-pad moves it; touch does not show it.
- **Code:** drawFocus (ui.h) and ACCENT: the theme’s, so focus needs no change per theme.

### Away-from-normal mark (new)

- **Used:** chips, the Light key, header badges, mini bar pills, slider rows
- **Anatomy and states:** ACCENT; the value written in place of the setting’s name; a 6-unit dot in the control’s corner. Where the control isn’t on screen: the badge (header) or pill (mini bar). DANGER only past a danger line (volume over 120%, offline, failed).
- **Inputs:** state, its normal value, danger
- **Keys and touch:** A tap on a badge or pill goes to where the control is.
- **Code:** New as a rule (0.7.18 has the yellow speed key and the lit Brightness key). One helper: given a state and its normal, return the tone and the words.

### Screen-off warning (new)

- **Used:** before any automatic screen off (bottom or top)
- **Anatomy and states:** A 62% black veil over the panel (the backlight also drops to a third), and one notice row 52 tall, 40 above the foot, yellow edge: a moon icon 20, “Screen off in N s” Pixelify 15 yellow, and “touch, or any key, keeps it on” Inter 12.5. On the top screen it is a note at the foot instead.
- **Inputs:** seconds left, which screen
- **Keys and touch:** Any key or touch cancels and does nothing else.
- **Code:** New. The Screen off timers exist (Settings: Screen off (music) / (video)); the five seconds before them are not drawn today. A veil and a notice row over whatever page shows.

### Lid-shut keys (new)

- **Used:** while the lid is shut and the handheld is awake (When the lid closes: Keep playing)
- **Anatomy and states:** Nothing is drawn: both screens are off. One key map, the same on every page: Volume + / − press = volume (to 100%), hold 0.6 s = next / previous; the volume step is made when the key is let go, so a hold never moves it; Power press = play / pause, hold 2 s = sleep (the setting Power key, lid shut: Play / pause, the default / Sleep: the key left to BananaOS), and nothing else sleeps the handheld while shut and something is loaded (nothing loaded: it sleeps as BananaOS says); the hold is the setting Volume keys, lid shut (Volume and skip, the default / Volume only: the keys left to BananaOS); L, R, L2, R2 by the setting Shoulder keys, lid shut (Ignored / Hold / As usual); everything under the lid ignored. One hold, one step; a key down over 5 s is ignored until let go. No sound effects. The full rules and the two options: lid-and-incognito.md.
- **Inputs:** lid shut, the setting, what plays
- **Keys and touch:** As above. Touch is ignored.
- **Code:** New. One filter in front of the key handling in main.cpp, fed by `SleepWatch::lidClosed()`; it sends the Session’s own intents (step, play / pause), so the page left open never sees a key. BananaOS repeats a held volume key, so while the lid is shut the player takes the volume keys’ input device (`gpio-keys-volume`, its own device, apart from Power and the lid switch; `EVIOCGRAB`, Linux-only, beside sleepwatch.*) and steps the volume itself through `/usr/libexec/banana/volume`; it lets go when the lid opens and on exit.

### Title marquee (new)

- **Used:** the player’s title (a song and a video), the mini bar’s title, a song’s title on the top screen (all four Show modes), the focused row’s name in a list. Not the header’s title, not artist and album, not a chip’s value, not a Note: those stay one line cut with “…”.
- **Anatomy and states:** One line in a box.
  - **Fits:** drawn as before; nothing moves, nothing fades.
  - **Doesn’t fit, at rest:** the text from its start, no “…”; its last 16 units fade into the ground, which says it goes on.
  - **Moving:** after 1.5 s at its start it moves left until its end shows, rests 1.5 s there, fades back to its start (150 ms out, 150 ms in), rests 3 s, and does it once more. After two passes it stays at its start. A focused row makes one pass, and only after the cursor has rested 1.5 s on it, so stepping down a list starts nothing. While it moves both edges fade; at its end the right one doesn’t.
  - **Speed:** whole units, 24 steps a second: 2 units a step (48 a second) for type of 24 and more, 1 unit a step (24 a second) under it. About three letters a second at both sizes; whole units keep the pixel type sharp.
  - **Starts again:** the title changes; the screen is opened; the panel wakes. **Stops, back at its start:** the panel dims (the Screen-off warning) or turns off, the lid shuts, a sheet opens over it, Scroll long titles is turned off. Pausing changes nothing: two passes bound it, so a screen where nothing else changes is still again within seconds.
  - **One at a time on a panel:** the focused row first; the mini bar’s title waits at its start until the row is done.
  - **Right to left:** a title that starts in a right-to-left script rests at its right end and moves right.
  - **Colours:** the text keeps its own (TEXT on the player, the mini bar and a song’s top screen; ACCENT in a focused row; white with its shadow over a picture). The fade is the ground’s token, from clear to full over 16 units: BG on the player and a song’s top screen, SURFACE in the mini bar, BUTTON in a focused row. Over a picture (a pixel-art visualizer alone) there is no fade: a plain cut at the box. Nothing is new per theme.
- **Repeated start (the text it is given):** the shown title of what plays (the three places above, and the player’s Next line) leaves out its start when all of these hold. It is a rule, not a setting.
  1. The whole title doesn’t fit the box it is drawn in. A title that fits is never changed, so a wide box may show more than a narrow one.
  2. The start is, without regard to case, the album’s name, the artist’s, the name of the folder the file is in, or a start that every item of the playing list shares (three items or more, four letters or more).
  3. That start ends at a separator: “ - ”, “ – ”, “ — ”, “: ”, “ | ” or “_”. The separator goes with it.
  4. Three letters or more are left, not counting digits and marks (“Yellow Album - 03” stays whole).

  At most two starts go (artist, then album). A track number stays: it is short and says where in the album. Nothing is renamed: list rows, Search and More › Info show the name whole, and a focused row scrolls the whole name. With Scroll long titles off the rule still applies, and helps most there.
- **Inputs:** text, font and size, the box, the ground’s token (or none), passes (2; 1 in a row), Scroll long titles (Settings › Power & screens: On, the default / Off; help: “A title too long for its place moves along after a moment, twice, then rests at its start. Off: it is cut with “…”.”). Off: cut with “…” as today, a file name in its middle.
- **Keys and touch:** None. A tap goes to what is under it (the mini bar: the player; the player’s title is not a key); a drag does not move it.
- **Code:** New. `lay::marquee` in kitlayout (bpcore, tested): the offset from the time since it started, the text’s and the box’s widths and the type’s size; pure, so the rests, the speed and the two passes are one table. `Kit::marquee(Rect, text, font, ground)` in ui_kit.cpp. `shownTitle()` (bpcore, tested with the table on the screen A long title) for the Repeated start. One `SettingDef`, `scroll-titles`.
  - **Bottom screen:** the text is drawn once to a texture (per text and font, through `Ui::runs()`), and a step copies a part of it into the box, then the two fades. The step is not in `signature()`: while a marquee moves, the pass redraws only its Rect over the panel as last drawn, 24 times a second, and the loop goes back to waiting when it rests. A full redraw (a `signature()` change) leaves its clock alone.
  - **Top screen:** the song’s lines are an mpv ASS overlay. The title becomes its own overlay (so artist and album are not sent again): one event, `\an7\pos(x,y)\clip(box)`, sent again with a new x each step, in whole panel pixels (the overlay’s canvas is 960 × 720, so a 2-pixel step is 3 of its units). libass keeps the glyphs it has drawn, so a step is a blend, not a new drawing of the text. The fades are two fixed events (four 4-unit strips of BG at 20, 40, 60, 80%), sent once. Not an image overlay (`overlay-add`): that would draw the title with SDL_ttf, which has no font fallback, where libass has the bundle’s fonts for other scripts.
  - **Cost:** a step makes mpv draw a frame. With a visualizer on, mpv draws 30 a second anyway and the steps ride on them. In Album art only and None the picture is still, so the marquee is the only thing drawing: at most two passes a song, a few seconds each, then nothing. Time it on the handheld with the other graphs.

### Lock overlay (exists, needs a variant)

- **Used:** the player, locked
- **Anatomy and states:** An 80% black veil over the controls; a 120-square key (radius 30, yellow edge) with a lock 54; “Touch is locked” Pixelify 22 yellow; one line Inter 14.
- **Inputs:** held share
- **Keys and touch:** Hold the lock 1 s. Buttons still work.
- **Code:** The lock state in ui_main.cpp:391 (“Hold the lock 1 s to unlock”).

## Search and Music: the index

Both features read one index of the cards. It is the constraint that shapes them: a card holds tens of thousands of files, the handheld has 1 GB of memory, and a card is slow to walk.

- **What it holds**, one record a file the app plays or opens, and one a folder: the name (folded: no capitals, no accents), its folder, its kind, its size and date. For a song, from its tags: title, artist, album artist, album, disc and track number, year, and whether it carries a picture. About 150 bytes a record: 50,000 files are 8 MB on the card, in the state folder, one file a card (named by the card's id, so a card that comes back is known).
- **How it is read.** A background worker walks the folders (never the main thread; it posts an event when a folder is done). Tags are read by a small reader of ID3, Vorbis comments and MP4 atoms: the first kilobytes of a file, never a decoder, never mpv. Covers are not in the index: a cover is made the first time it is shown, small (96 and 196 units), and kept in the state folder, 48 MB at most, oldest dropped first.
- **When it refreshes.** At start, and when a card goes in: a folder whose date has not changed is skipped, so a second start reads almost nothing. At once, without a walk, after anything the app itself does: a copy, a move, a rename, a delete, an upload from the dashboard. Files › More › Refresh walks that folder. A card taken out: its records are set aside, not deleted.
- **It yields.** The walk stops while a video plays or the Manga engine is up (the card and the memory are theirs), and goes on with music. It never keeps the handheld awake by itself.
- **Searching it.** The index is mapped, not loaded: a search is one pass over the names (50,000 in well under a tenth of a second), run off the main thread, the newest text replacing the one before. No word index, no ranking beyond “starts with it” first.
- **While it is not done.** Search answers from what has been read, with a notice row and a bar; Music lists what it has and says “Reading the card · 62%” in its crumb.
- **Not in it.** Network servers (no index to ask; a walk over Wi-Fi takes minutes and the server may be off), hidden folders while they are hidden, and anything played in incognito is not added to Recent searches.
- **The Web dashboard** asks the handheld: `/api/search?q=` and `/api/music/albums`, `/artists`, `/album`, `/artist`, through `WebServer::call`, paths through `resolveVirtual` and the deny list as every route. Its header has the search box on every page (results as you type, 200 ms after the last letter, the same groups and counts); Music is a tab beside Files and Playlists, with the Albums grid and the Artists list in its own boxes and type, in the handheld's theme.

## Themes

A theme is a set of values for the tokens of `src/theme.h`, and nothing else. Every component in this document is drawn with those tokens, so a theme is a swap of the table: no component and no screen has a colour of its own, and none has a rule for one theme. The setting is Settings › Power & screens › Theme (first row); the preview draws it as `set-theme`, and the Theme switch at the foot of the preview page changes every screen at once.

Where this document says yellow, cream or red, it means ACCENT, TEXT and DANGER: those are the Yellow theme's values.

### What a theme does not change

Sizes, radii (`R_*`), edge and lip widths (`EDGE` 2, `LIP` 3), type (Pixelify Sans, Inter and their sizes), layouts (`kitlayout.h`), states, keys and touch. The strengths `VEIL_SHEET` 68, `VEIL_LOCK` 80, `OFF` 45, `HOLD_FILL` 28, `TINT` 20 and the top card's 223 of 255. The Focus rule: one meaning, three drawings, the edge in ACCENT, never a second colour, never a glow. The Away-from-normal rule: the value in ACCENT with a dot; DANGER only past a danger line.

### The themes

Seventeen, stepped with LEFT / RIGHT in this order (it wraps): Yellow first, then the calm ones, then the playful ones, then the light ones (Sakura, Light) and High contrast. Dark is the default; Yellow is today's look, value for value, and stays first in the order. The playful ones are for a small screen at night: tinted near-black grounds, soft text, one bright accent. 16-bit and 32-bit are inspired by classic consoles: their greys as the body, one key colour as the accent, red as DANGER. They carry no mark or name of any product, and their other key colours are not used: a theme has one accent.

| Theme | What it is |
|---|---|
| Yellow | Today's look |
| Dark | Near-black greys, a soft steel-blue accent; nothing pure white |
| Midnight blue | Deep navy, a sky-blue accent |
| Dark purple | Deep violet, a soft lilac accent |
| Dark red | A red cast, a red accent; DANGER is amber |
| Pink | A plum cast, a pink accent; DANGER is amber |
| Coffee | Roast browns, cream text, a latte accent |
| Mint | Dark teal, a cool mint accent |
| Retro green | An old handheld's greens, a lime accent |
| Sunset | Warm dusk, an orange accent; DANGER is hot pink |
| Candy | Grape night, pink-white text, an aqua accent |
| 16-bit | A classic console's light grey and lavender, purple keys; light-bodied |
| 16-bit Lavender | From a five-colour palette: light grey body, lavender keys, dark grey text, a deepened purple accent; light-bodied |
| 32-bit | A classic console's cool greys, a deep blue accent; light-bodied |
| Sakura | From a five-colour palette: peach and blush paper, pink keys, plum text, a deepened mauve accent; light-bodied; DANGER is burnt orange |
| Light | Warm paper, dark ink, dark amber: for sunlight |
| High contrast | Black, white, bright yellow; text at 7 : 1 or more |

Five are light-bodied: 16-bit, 16-bit Lavender, 32-bit, Sakura and Light. 16-bit Lavender is built from five given colours: light grey #C2C2C2 is its BG, lavender #B5AFE1 its BUTTON, dark grey #3B3B3B its TEXT, mid grey #96939F its RULE, all as given; purple #8866B2 is its accent's hue, deepened to #4E3375, because as given it is 2.5 : 1 on the grey and cannot be text. Its ground is the darkest of the light ones, so its dim text sits close to its text and its edges go 80% of the way to TEXT. Sakura is built from five given colours too: pale peach #FFE6E0 is its BG, blush #FACFD8 its RAIL and TRACK, pink #E8B3CF its BUTTON and RULE, mauve #C1839F its PRESSED, deep plum #561F33 its TEXT, all as given. Three are derived: SURFACE is the peach lightened (#FFF3EF); the accent is the mauve deepened to #852A59, because as given it is 2.1 : 1 on the pink and plum itself would not tell focus from text; DIM and DIM_ON are the mauve darkened (#8A5069, #6F3C55). They share Light's rules, which are the tokens' rules: a dark accent with light ink on it, EDGE_MIX 55, a veil of their own BG, the Light page under Match theme, and TOP_ tokens taken from a dark theme (below).

### The tokens, and each theme's values

Sixteen colour roles, one strength, and eight for the top screen. Existing names are kept. New ones are marked: RAIL (today a literal, #0B0A0D), EDGE_MIX (today the fixed 30 inside `edgeOf`), TOP_TEXT, TOP_EDGE and TOP_DANGER (today TEXT, no edge, and DANGER used on the card). "Preview" is the CSS variable of `src/style.css` that carries the token: one to one, so the switch in the preview swaps what the code will swap.

| Token | Preview | Yellow | Dark | Midnight blue | Dark purple | Dark red | Pink | Coffee | Mint | Retro green | Sunset | Candy | 16-bit | 16-bit Lavender | 32-bit | Sakura | Light | High contrast |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| BG | `--bg` | #000000 | #0B0B0D | #040816 | #0A0612 | #0A0405 | #0C060A | #0D0907 | #050D0C | #0A0F08 | #0E0806 | #100A14 | #E4E3EA | #C2C2C2 | #DEDEE1 | #FFE6E0 | #F7F4EC | #000000 |
| SURFACE | `--sf` | #121116 | #17171A | #0C142E | #150E22 | #190C0E | #1B0F17 | #1A1410 | #0D1917 | #131B10 | #1C110D | #1C1323 | #F4F3F8 | #D4D4D6 | #EFEFF2 | #FFF3EF | #FFFDF8 | #0A0A0A |
| BUTTON | `--bt` | #26242E | #2A2A2F | #1A2649 | #2C1F45 | #361D21 | #301C29 | #30261F | #1A2E2B | #22301D | #33211A | #31223D | #CFCDDC | #B5AFE1 | #C8C8CE | #E8B3CF | #E6E0D2 | #1E1E1E |
| PRESSED | `--pr` | #3E3B4A | #424249 | #2C3C6C | #3D2C5E | #4D2A2F | #4B2E41 | #4A3B30 | #2A4642 | #35492D | #4E3328 | #4C365D | #B8B5CC | #9E97D0 | #B0B0B8 | #C1839F | #D0C9B8 | #3A3A3A |
| TRACK | `--tr` | #1E1D24 | #222226 | #131C3A | #1D1430 | #241315 | #25151F | #241C17 | #132321 | #1A2516 | #261812 | #26192F | #D6D4E0 | #B4B3BB | #D0D0D6 | #FACFD8 | #DED8C9 | #2B2B2B |
| RULE | `--ru` | #2A2833 | #2E2E34 | #202C52 | #2E2147 | #382224 | #37222F | #392D25 | #213734 | #2A3A24 | #3A261E | #3A2947 | #C6C4D3 | #96939F | #C0C0C7 | #E8B3CF | #D5CEBE | #5E5E5E |
| RAIL | `--ra` | #0B0A0D | #121215 | #090F24 | #110B1C | #13090A | #150B12 | #15100C | #0A1413 | #0F160D | #160D0A | #170F1D | #DAD9E2 | #B8B8BA | #D3D3D8 | #FACFD8 | #ECE7DB | #060606 |
| SCREEN | `--sc` | #26242E | #2A2A2F | #1A2649 | #2C1F45 | #361D21 | #301C29 | #30261F | #1A2E2B | #22301D | #33211A | #31223D | #CFCDDC | #B5AFE1 | #C8C8CE | #E8B3CF | #E6E0D2 | #1E1E1E |
| TEXT | `--tx` | #E8E2D3 | #C2C4CA | #E1E7F5 | #E6DFF2 | #EFE1DD | #F1E2E9 | #EADFCF | #DDEBE7 | #D9E4C8 | #F1E1D2 | #F3E4EE | #26242E | #3B3B3B | #1E1E22 | #561F33 | #1B1A20 | #FFFFFF |
| DIM | `--dm` | #8F8A80 | #8A8C94 | #8893B0 | #938AA8 | #A08B87 | #A18B97 | #9C8E7E | #859A95 | #8A9A7C | #A38D7E | #A08C9C | #5E5B6B | #4C4A52 | #595960 | #8A5069 | #625E56 | #C6C6C6 |
| DIM_ON | `--do` | #ABA597 | #A6A8B0 | #A5AFCA | #AEA5C3 | #BCA8A4 | #BAA5B1 | #B7A999 | #A1B5B0 | #A6B598 | #BDA797 | #BBA7B7 | #4F4C5C | #434149 | #4B4B52 | #6F3C55 | #55514A | #D8D8D8 |
| KNOB | `--kn` | #E8E2D3 | #C2C4CA | #E1E7F5 | #E6DFF2 | #EFE1DD | #F1E2E9 | #EADFCF | #DDEBE7 | #D9E4C8 | #F1E1D2 | #F3E4EE | #26242E | #3B3B3B | #1E1E22 | #561F33 | #1B1A20 | #FFFFFF |
| ACCENT | `--ac` | #FFD23F | #A9C4E6 | #8CC8FF | #B79CFF | #FF6A6E | #FF8FC7 | #E0B070 | #6FE0C2 | #A6D44A | #FF9A4D | #7FE0E8 | #55429C | #4E3375 | #1F4E9E | #852A59 | #7A5200 | #FFE14D |
| ON_ACCENT | `--oa` | #000000 | #0B0B0D | #000000 | #0A0612 | #000000 | #000000 | #0D0907 | #050D0C | #0A0F08 | #0E0806 | #100A14 | #F4F3F8 | #E6E4F0 | #EFEFF2 | #FFE6E0 | #FFFDF8 | #000000 |
| DANGER | `--dg` | #F0645A | #F47067 | #FF7A70 | #FF7A70 | #FFB224 | #FFB224 | #FF7468 | #FF7A70 | #FF7A6B | #FF6B9D | #FF7A70 | #A81E2A | #8A1420 | #961C24 | #7A3800 | #A3261D | #FF8A80 |
| EDGE_MIX | (in the edges) | 30% | 40% | 30% | 30% | 30% | 30% | 30% | 30% | 30% | 30% | 30% | 55% | 80% | 55% | 55% | 55% | 50% |
| TOP_CARD | `--tc` in the top scope | #0B0A0D | #121215 | #090F24 | #110B1C | #13090A | #150B12 | #15100C | #0A1413 | #0F160D | #160D0A | #170F1D | #110B1C | #110B1C | #121215 | #150B12 | #0B0A0D | #060606 |
| TOP_EDGE | `--te` in the top scope | #2A2833 | #2E2E34 | #202C52 | #2E2147 | #382224 | #37222F | #392D25 | #213734 | #2A3A24 | #3A261E | #3A2947 | #2E2147 | #2E2147 | #2E2E34 | #37222F | #2A2833 | #5E5E5E |
| TOP_TITLE | `--ac` in the top scope | #FFD23F | #A9C4E6 | #8CC8FF | #B79CFF | #FF6A6E | #FF8FC7 | #E0B070 | #6FE0C2 | #A6D44A | #FF9A4D | #7FE0E8 | #B79CFF | #B79CFF | #A9C4E6 | #FF8FC7 | #FFD23F | #FFE14D |
| TOP_TEXT | `--tx` in the top scope | #E8E2D3 | #C2C4CA | #E1E7F5 | #E6DFF2 | #EFE1DD | #F1E2E9 | #EADFCF | #DDEBE7 | #D9E4C8 | #F1E1D2 | #F3E4EE | #E6DFF2 | #E6DFF2 | #C2C4CA | #F1E2E9 | #E8E2D3 | #FFFFFF |
| TOP_SUB | `--dm, --do` in the top scope | #8F8A80 | #8A8C94 | #8893B0 | #938AA8 | #A08B87 | #A18B97 | #9C8E7E | #859A95 | #8A9A7C | #A38D7E | #A08C9C | #938AA8 | #938AA8 | #8A8C94 | #A18B97 | #8F8A80 | #C6C6C6 |
| TOP_TRACK | `--bt` in the top scope | #26242E | #2A2A2F | #1A2649 | #2C1F45 | #361D21 | #301C29 | #30261F | #1A2E2B | #22301D | #33211A | #31223D | #2C1F45 | #2C1F45 | #2A2A2F | #301C29 | #26242E | #1E1E1E |
| TOP_SQUARE | `--sf` in the top scope | #121116 | #17171A | #0C142E | #150E22 | #190C0E | #1B0F17 | #1A1410 | #0D1917 | #131B10 | #1C110D | #1C1323 | #150E22 | #150E22 | #17171A | #1B0F17 | #121116 | #0A0A0A |
| TOP_DANGER | `--dg` in the top scope | #F0645A | #F47067 | #FF7A70 | #FF7A70 | #FFB224 | #FFB224 | #FF7468 | #FF7A70 | #FF7A6B | #FF6B9D | #FF7A70 | #FF7A70 | #FF7A70 | #F47067 | #FFB224 | #F0645A | #FF8A80 |

| Worked out | Preview | Yellow | Dark | Midnight blue | Dark purple | Dark red | Pink | Coffee | Mint | Retro green | Sunset | Candy | 16-bit | 16-bit Lavender | 32-bit | Sakura | Light | High contrast |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| edgeOf(BUTTON) | `--ed` | #605D60 | #67686D | #56607D | #645979 | #6E5859 | #6A5763 | #685E54 | #556763 | #596650 | #6C5B51 | #6B5C72 | #72707C | #53525C | #6A6A6F | #986279 | #767370 | #8F8F8F |
| edgeOf(SURFACE) | `--eds` | #52504F | #5B5C60 | #4C536A | #544D60 | #594C4C | #5B4E56 | #585149 | #4B5855 | #4E5747 | #5C4F48 | #5D5260 | #838189 | #5A5A5A | #7C7C80 | #A27E88 | #828081 | #858585 |
| lipOf(BUTTON) | `--lp` | #1B1920 | #1D1D21 | #121B33 | #1F1630 | #261417 | #22141D | #221B16 | #12201E | #182214 | #241712 | #22182B | #91909A | #7F7B9E | #8C8C90 | #A27D91 | #A19D93 | #151515 |
| edgeOf(ACCENT) | `--ae` | #F8D76B | #B3C4DB | #A6D1FC | #C5B0FB | #FA8E8F | #FBA8D1 | #E3BE8D | #90E3CD | #B5D970 | #FBAF75 | #A2E1EA | #3B315F | #3F3947 | #1E345A | #6B2444 | #463312 | #FFF0A6 |
| lipOf(ACCENT) | `--al` | #B3932C | #7689A1 | #628CB3 | #806DB3 | #B34A4D | #B3648B | #9D7B4E | #4E9D88 | #749434 | #B36C36 | #599DA2 | #3C2E6D | #372452 | #16376F | #5D1D3E | #553900 | #B39E36 |
| edgeOf(DANGER) | `--de` | #EE8A7E | #E0928F | #F69B98 | #F79897 | #FAC05C | #FBC05F | #F99487 | #F59C94 | #F49A87 | #FB8EAD | #FB9A96 | #60212C | #4B3336 | #541D23 | #662A1C | #581F1F | #FFC5C0 |
| lipOf(DANGER) | `--dl` | #A8463F | #AB4E48 | #B3554E | #B3554E | #B37D19 | #B37D19 | #B35149 | #B3554E | #B3554B | #B34B6E | #B3554E | #76151D | #610E16 | #691419 | #552700 | #721B14 | #B3615A |

Aliases stay aliases in every theme: KNOB = TEXT, SCREEN = BUTTON. In the twelve dark themes the TOP_ tokens are aliases too, as today: TOP_TITLE = ACCENT, TOP_TEXT = TEXT, TOP_SUB = DIM, TOP_TRACK = BUTTON, TOP_SQUARE = SURFACE, TOP_DANGER = DANGER, TOP_EDGE = RULE, and TOP_CARD and RAIL are SURFACE at 60% over BG. The light-bodied themes give RAIL its own value and take their TOP_ tokens from a dark theme: Light from Yellow, Sakura from Pink, 16-bit and 16-bit Lavender from Dark purple, 32-bit from Dark. EDGE_MIX is 30 except where a theme's TEXT is too close to its fills for the edge to show: Dark 40, High contrast 50, the light-bodied ones 55, 16-bit Lavender 80.

The worked-out colours keep their rules in every theme, so they are not stored:

- `edgeOf(fill)`: the fill EDGE_MIX percent of the way to TEXT. A light theme's edges come out darker than the fill, a dark theme's lighter: the same rule.
- `lipOf(fill)`: the fill × 0.7.
- Unavailable: the piece at `OFF` percent over BG (`Kit::off`).
- The veil under a sheet and under the lock: BG at `VEIL_SHEET` / `VEIL_LOCK`. In a light-bodied theme it is a pale veil, so what is written on it (the lock's two lines) stays TEXT and ACCENT on BG.
- The play key's hold: BG at `HOLD_FILL` over the key.
- A red pill's ground: DANGER at `TINT` over its ground. A header badge's ground: ACCENT at 17% over its ground.

No theme but High contrast uses pure white (#FFFFFF) for a fill, a text or an accent (the light-bodied grounds are off-white and light greys), and only Yellow, as today, and High contrast have a pure black BG. Dark keeps its accent (11.0 on BG) and its text (11.3) well under the 14 to 21 the others reach, on purpose: it is the theme for a dark room. Its focus is told from plain text by the accent's blue against grey, as well as by the edge and the row's fill.

DANGER is never near the accent's hue: amber in Dark red and Pink, hot pink in Sunset (its accent is orange), burnt orange in Sakura (its accent is rose), a red in the rest (coral on dark, deep on a light body).

### Contrast

WCAG ratios, worked out by `themes.py` (it fails if a pair is under its floor). Text pairs must reach 4.5 (AA); High contrast holds its main pairs to 7 (AAA). Edges and bars must reach 3. Unavailable pieces (45%) are outside the floors, as the standard allows.

| Pair (floor) | Yellow | Dark | Midnight blue | Dark purple | Dark red | Pink | Coffee | Mint | Retro green | Sunset | Candy | 16-bit | 16-bit Lavender | 32-bit | Sakura | Light | High contrast |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| TEXT on BG (4.5; High contrast 7) | 16.3 | 11.3 | 16.1 | 15.5 | 16.0 | 16.1 | 15.1 | 16.0 | 14.6 | 15.6 | 15.9 | 12.0 | 6.3 | 12.4 | 10.8 | 15.7 | 21.0 |
| TEXT on SURFACE (4.5; High contrast 7) | 14.5 | 10.3 | 14.7 | 14.5 | 15.0 | 14.9 | 13.9 | 14.6 | 13.3 | 14.5 | 14.7 | 13.8 | 7.6 | 14.5 | 11.8 | 17.0 | 19.8 |
| TEXT on BUTTON (4.5; High contrast 7) | 11.8 | 8.2 | 12.0 | 11.7 | 12.2 | 12.7 | 11.2 | 11.6 | 10.5 | 12.0 | 12.0 | 9.8 | 5.4 | 10.0 | 7.2 | 13.1 | 16.7 |
| TEXT on TRACK (a neutral pill) (4.5; High contrast 7) | 12.9 | 9.1 | 13.5 | 13.6 | 14.0 | 13.9 | 12.7 | 13.3 | 12.0 | 13.5 | 13.6 | 10.4 | 5.4 | 10.8 | 9.1 | 12.2 | 14.2 |
| DIM on BG (4.5; High contrast 7) | 6.1 | 5.9 | 6.5 | 6.1 | 6.3 | 6.4 | 6.2 | 6.6 | 6.4 | 6.3 | 6.3 | 5.2 | 4.9 | 5.2 | 5.1 | 5.9 | 12.3 |
| DIM_ON on BG (a song's artist line, the hint bar) (4.5; High contrast 7) | 8.6 | 8.3 | 9.1 | 8.6 | 9.0 | 8.7 | 8.6 | 9.1 | 8.9 | 8.7 | 8.7 | 6.5 | 5.6 | 6.4 | 7.2 | 7.2 | 14.7 |
| DIM on SURFACE (4.5) | 5.5 | 5.3 | 5.9 | 5.8 | 5.9 | 5.9 | 5.7 | 6.0 | 5.9 | 5.9 | 5.8 | 6.0 | 5.9 | 6.1 | 5.6 | 6.3 | 11.6 |
| DIM_ON on BUTTON (4.5) | 6.2 | 6.0 | 6.8 | 6.5 | 6.8 | 6.9 | 6.4 | 6.6 | 6.4 | 6.7 | 6.5 | 5.3 | 4.9 | 5.2 | 4.8 | 6.0 | 11.7 |
| DIM_ON on TRACK (a chip, off) (4.5) | 6.8 | 6.7 | 7.6 | 7.5 | 7.9 | 7.6 | 7.3 | 7.6 | 7.3 | 7.5 | 7.4 | 5.7 | 4.8 | 5.6 | 6.1 | 5.5 | 9.9 |
| DIM_ON on RAIL (4.5) | 8.0 | 7.9 | 8.7 | 8.2 | 8.7 | 8.4 | 8.2 | 8.7 | 8.5 | 8.3 | 8.3 | 6.0 | 5.1 | 5.8 | 6.1 | 6.4 | 14.2 |
| ACCENT on BG (4.5; High contrast 7) | 14.5 | 11.0 | 11.2 | 8.8 | 7.3 | 9.6 | 10.0 | 12.3 | 11.2 | 9.5 | 12.8 | 6.2 | 5.7 | 5.9 | 7.1 | 6.3 | 16.1 |
| ACCENT on SURFACE (4.5; High contrast 7) | 13.0 | 10.0 | 10.2 | 8.2 | 6.9 | 8.9 | 9.2 | 11.2 | 10.2 | 8.8 | 11.8 | 7.2 | 6.9 | 6.9 | 7.8 | 6.8 | 15.2 |
| ACCENT on BUTTON (the focused row) (4.5) | 10.6 | 8.0 | 8.3 | 6.6 | 5.6 | 7.6 | 7.5 | 8.9 | 8.0 | 7.3 | 9.6 | 5.1 | 5.0 | 4.8 | 4.8 | 5.3 | 12.8 |
| ACCENT on its 17% tint (a badge) (4.5) | 8.8 | 7.0 | 7.1 | 6.1 | 5.4 | 6.5 | 6.6 | 7.6 | 7.0 | 6.4 | 8.0 | 5.5 | 5.3 | 5.3 | 5.9 | 5.3 | 10.3 |
| ON_ACCENT on ACCENT (4.5; High contrast 7) | 14.5 | 11.0 | 11.8 | 8.8 | 7.5 | 10.0 | 10.0 | 12.3 | 11.2 | 9.5 | 12.8 | 7.2 | 8.1 | 6.9 | 7.1 | 6.8 | 16.1 |
| ON_ACCENT on DANGER (Quit) (4.5) | 6.7 | 6.9 | 8.3 | 7.9 | 11.6 | 11.6 | 7.5 | 7.7 | 7.6 | 7.4 | 7.7 | 6.6 | 7.6 | 7.4 | 7.4 | 7.2 | 9.2 |
| DANGER on BG (4.5) | 6.7 | 6.9 | 7.9 | 7.9 | 11.3 | 11.1 | 7.5 | 7.7 | 7.6 | 7.4 | 7.7 | 5.7 | 5.4 | 6.3 | 7.4 | 6.7 | 9.2 |
| DANGER on SURFACE (4.5) | 6.0 | 6.3 | 7.2 | 7.4 | 10.6 | 10.3 | 6.9 | 7.1 | 6.9 | 6.9 | 7.1 | 6.6 | 6.4 | 7.4 | 8.1 | 7.2 | 8.7 |
| DANGER on BUTTON (4.5) | 4.9 | 5.0 | 5.8 | 6.0 | 8.6 | 8.8 | 5.6 | 5.6 | 5.5 | 5.7 | 5.8 | 4.7 | 4.6 | 5.1 | 4.9 | 5.6 | 7.3 |
| DANGER on its 20% tint (a red pill) (4.5) | 4.6 | 4.6 | 5.3 | 5.4 | 7.0 | 6.8 | 5.0 | 5.2 | 5.0 | 5.0 | 5.1 | 4.7 | 4.6 | 5.2 | 5.8 | 5.2 | 6.3 |
| Focus edge (ACCENT) against BG (3) | 14.5 | 11.0 | 11.2 | 8.8 | 7.3 | 9.6 | 10.0 | 12.3 | 11.2 | 9.5 | 12.8 | 6.2 | 5.7 | 5.9 | 7.1 | 6.3 | 16.1 |
| Focus edge against SURFACE (3) | 13.0 | 10.0 | 10.2 | 8.2 | 6.9 | 8.9 | 9.2 | 11.2 | 10.2 | 8.8 | 11.8 | 7.2 | 6.9 | 6.9 | 7.8 | 6.8 | 15.2 |
| edgeOf(BUTTON) against BG (3) | 3.2 | 3.5 | 3.2 | 3.1 | 3.1 | 3.0 | 3.1 | 3.3 | 3.2 | 3.1 | 3.2 | 3.8 | 4.3 | 4.0 | 4.0 | 4.3 | 6.5 |
| edgeOf(SURFACE) against BG (as today: a flat piece, the album art's edge) (2.4) | 2.6 | 2.9 | 2.6 | 2.5 | 2.5 | 2.6 | 2.5 | 2.6 | 2.6 | 2.5 | 2.6 | 3.0 | 3.9 | 3.1 | 3.0 | 3.6 | 5.7 |
| Peaks and the six simple styles: marks (ACCENT) against BG (3) | 14.5 | 11.0 | 11.2 | 8.8 | 7.3 | 9.6 | 10.0 | 12.3 | 11.2 | 9.5 | 12.8 | 6.2 | 5.7 | 5.9 | 7.1 | 6.3 | 16.1 |
| Bar fill (ACCENT) against TRACK (3) | 11.6 | 8.9 | 9.4 | 7.7 | 6.4 | 8.3 | 8.5 | 10.2 | 9.2 | 8.2 | 10.9 | 5.4 | 4.9 | 5.2 | 6.1 | 4.9 | 10.9 |
| KNOB against TRACK (3) | 12.9 | 9.1 | 13.5 | 13.6 | 14.0 | 13.9 | 12.7 | 13.3 | 12.0 | 13.5 | 13.6 | 10.4 | 5.4 | 10.8 | 9.1 | 12.2 | 14.2 |
| TOP_TEXT on TOP_CARD (4.5; High contrast 7) | 15.3 | 10.7 | 15.3 | 14.9 | 15.4 | 15.4 | 14.4 | 15.2 | 13.9 | 15.0 | 15.3 | 14.9 | 14.9 | 10.7 | 15.4 | 15.3 | 20.3 |
| TOP_SUB on TOP_CARD (4.5) | 5.8 | 5.6 | 6.2 | 5.9 | 6.1 | 6.1 | 5.9 | 6.3 | 6.1 | 6.1 | 6.0 | 5.9 | 5.9 | 5.6 | 6.1 | 5.8 | 11.9 |
| TOP_TITLE on TOP_CARD (4.5; High contrast 7) | 13.7 | 10.4 | 10.7 | 8.5 | 7.0 | 9.2 | 9.6 | 11.7 | 10.6 | 9.1 | 12.2 | 8.5 | 8.5 | 10.4 | 9.2 | 13.7 | 15.6 |
| TOP_DANGER on TOP_CARD (4.5) | 6.3 | 6.6 | 7.5 | 7.6 | 10.9 | 10.7 | 7.2 | 7.4 | 7.2 | 7.2 | 7.4 | 7.6 | 7.6 | 6.6 | 10.7 | 6.3 | 8.9 |
| Reader page (Match theme): text on page (4.5; High contrast 7) | 13.4 | 13.4 | 13.4 | 13.4 | 13.4 | 13.4 | 13.4 | 13.4 | 13.4 | 13.4 | 13.4 | 14.1 | 14.1 | 14.1 | 14.1 | 14.1 | 10.9 |

One pair is below 3 in the dark themes, as it is today: a flat piece's edge against BG (2.5 to 2.9). A chip or a tile is told by its fill and its edge together; The light-bodied themes and High contrast lift it with EDGE_MIX.

### Fixed in every theme

- The picture: video, photos, album art, covers, the manga cover placeholders' gradients, a comic's or a PDF's page, and the black around them on the top screen. Not the Art placeholder: what stands in for missing album art is app UI, drawn in BUTTON, BG, SURFACE and ACCENT, and changes with the theme.
- The pixel-art visualizers' own colours, and Pocket console's LCD greens: the things they draw, and the whole picture when one is alone on the screen. Not their ground in Art + visualizer, which is BG, and not the marks of Peaks and the six simple styles (Mirror bars, Wave, Dots, Levels, Area, Radial), which take ACCENT and TEXT (their own yellow on Light's paper would be 1.4 : 1).
- Subtitles and their preview: the subtitle settings decide them.
- Text laid straight on a picture with no card under it: the song's title, artist and album over a visualizer that fills the screen (#FFFFFF, #B4B4B4, with their shadow), drawn as subtitles are. On the theme's ground the same lines are TEXT, DIM_ON and DIM.
- A book's page: Reading › Colours (`text-theme`: Dark, Light, Sepia, Black) and the Light sheet's filters. See below.
- The QR code: black on white with its white quiet edge.
- The screen-off warning's veil and a screen that is off: black, because they stand for the backlight going down.
- The picture looks' overlays (scanlines, LCD grid).

### The top screen

Two cases, by what is under the drawing.

- **Nothing plays** (the Preview pane, a setting's help, the idle logo), **or a song in Album art only, Art + visualizer or None**: app UI. It takes the main tokens, so in a light-bodied theme the top screen is light. A song's ground is BG, flat: the same ground as the bottom screen, so the two panels read as one. Its title is TEXT, the artist DIM_ON, the album DIM; the art keeps a 2-unit `edgeOf(SURFACE)` edge, which shows on a light ground and a dark one. The art is a picture on that ground; a song with none shows the Art placeholder there, which is app UI in the same tokens. In Art + visualizer the ground runs to the foot of the screen and the visualizer is drawn over it, as today's app draws it over the art (every style has that drawing: only its marks, nothing behind them); there is no black strip on any theme.

| Style | Over BG in Art + visualizer | Alone (Visualizer only) |
|---|---|---|
| Peaks, Mirror bars, Wave, Dots, Levels, Area, Radial | Recoloured, always: bars ACCENT, caps TEXT (two constants in the graph, set when it is built; in Yellow they are today's yellow and cream) | The same, on BG; the song's lines TEXT, DIM_ON, DIM |
| Pocket console | As it is: its LCD greens, the ground showing between its dots | As it is: its own screen |
| The twelve pixel-art styles (Night city, Banana dancer, Campfire, Aurora, Pixel sea, Bunny hop, Fishbowl, Vibing cat, Popcorn, Jelly wobble, Tube man, Washing machine) | As they are: their own colours, drawn as over the art, BG where the art was | As they are: the whole picture; the song's lines white with their shadow |

The graph changes in one place for a theme: the colour of its ground source is BG, and the two mark colours of Peaks and the six simple styles are ACCENT and TEXT. No frame is converted or keyed. One more case is app UI whatever plays: the theme's preview, while Settings › Power & screens › Theme is focused (Where it is changed).
- **A picture, or its place** (a video, a photo, a pixel-art visualizer alone, a page, and the State card that stands where the picture would be: Buffering, Can't play, Playing in your browser): the TOP_ tokens only. Level cards and Notes are always drawn with the TOP_ tokens, over a pane too, so there is one drawing of each.

The TOP_ tokens are dark in every theme. A light card over a film glares, hides more of the picture than it covers, and makes the film look darker. So a light-bodied theme takes a dark theme's cards (Light Yellow's, Sakura Pink's, 16-bit's and 16-bit Lavender's Dark purple's, 32-bit Dark's); the dark themes tint theirs with their own accent.

| Piece | Body | Edge | Main line | Second line | Bar | Past a danger line |
|---|---|---|---|---|---|---|
| Level card | TOP_CARD at 223 / 255 | TOP_EDGE | TOP_TEXT for a level, TOP_TITLE for a name (a chapter, a preset) and for a boosted volume | TOP_SUB | TOP_TRACK, filled TOP_TEXT to 100%, TOP_TITLE to 120%; the 100 mark TOP_SUB | TOP_DANGER (bar and number) |
| Note | TOP_CARD at 223 / 255 | TOP_EDGE | TOP_TITLE | TOP_SUB | none | TOP_DANGER (a security or failure note) |
| State card | TOP_CARD, the whole panel | none | TOP_TITLE; its tile TOP_SQUARE with `edgeOf(TOP_SQUARE)`, the icon TOP_TITLE | TOP_TEXT, then TOP_SUB; key caps TOP_TITLE | TOP_TRACK, filled TOP_TITLE | TOP_DANGER (title and icon) |

### Where it is changed

- **Settings › Power & screens › Theme**, the group's first row: a stepper, ‹ Dark ›. Setting key `theme`, values `yellow`, `dark`, `midnight-blue`, `dark-purple`, `dark-red`, `pink`, `coffee`, `mint`, `retro-green`, `sunset`, `candy`, `16-bit`, `16-bit-lavender`, `32-bit`, `sakura`, `light`, `high-contrast`; default `dark`. One entry in `settingDefs()`.
- **Help line** (top screen, while the row is focused): “The app's colours, on both screens and on the Web dashboard. Videos, pictures, album art, subtitles and a book's page keep their own.” Under it: a swatch of each theme in two rows, in the stepper's order (three rows of six: six, six, five; 84 × 30 each, 9 apart, its name under it in Inter 10.5), with the chosen one outlined, and a key, a focused row, pills and a bar as they now look.
- **The preview always shows.** While the Theme row is focused the top screen is the theme's preview pane, whatever plays: a video or a song goes on underneath and its sound goes on, and its picture is back the moment the cursor leaves the row or Settings closes. It is the one row whose help takes the top screen from a picture, because the pane is what the setting changes.
- **It previews live.** LEFT / RIGHT apply the theme at once: nothing to open, nothing to confirm, B leaves it as it is. Both panels redraw on the step (the theme is part of `signature()`), a card showing on the top screen is drawn again, and dashboard pages follow on their next state update.
- **The group's tile** names it first: “Yellow · screen off 30 s · the lid pauses”.
- **Not in Quick settings (F).** Quick settings holds what is changed while something plays; a theme is chosen once.
- **The Web dashboard follows the handheld.** `web/style.css` holds the same tokens per theme (below), the page takes the handheld's theme with its state, and its Settings page has the same Theme row, which sets the handheld's. One setting, seen in both places. The page's `theme-color` and the Settings page's small picture of the two panels use the same tokens.

### A book's page colours

Reading › Colours stays its own setting: a page is read for hours and its colours are a reading choice, not the app's. It gains a first value, **Match theme**, which is the default: The light-bodied themes give the Light page, High contrast the Black page, every other theme the Dark page. For anyone on Yellow or Dark that is the Dark page, as today. Choosing Dark, Light, Sepia or Black by hand holds, whatever the theme. The reader's sheets and keys are app UI and follow the theme; the page under them does not.

### Every component, by the tokens it uses

This table is the contract for the build. “Box” is `drawBox`: a fill, `edgeOf(fill)`, `lipOf(fill)`. “Flat” is `Kit::flat`: a fill and `edgeOf(fill)`, no lip. “Off” is `Kit::off`. Focus always adds an ACCENT edge.

| Component (this document) | Code | Tokens |
|---|---|---|
| Header | `Kit::header`, `Kit::iconKey` | BG ground; TEXT title; DIM crumb; keys: Box BUTTON / PRESSED, TEXT mark; ACCENT state (DANGER when it failed) |
| Status cluster and badges | `drawBattery`, `drawWebIndicator` | DIM_ON marks and clock; ACCENT playing bars; badge: ACCENT on ACCENT at 17%; red badge and low battery: DANGER on DANGER at TINT |
| Section rail | new | RAIL ground, RULE line; DIM_ON cells; active cell ACCENT with `lipOf(ACCENT)`, ON_ACCENT ink; caps ACCENT; warning mark DANGER |
| Hint bar | `Kit::hints`, `Kit::caps`, `drawHints` | BG ground, RULE line, DIM_ON words; caps: ACCENT outline and letter on their ground (ON_ACCENT on an accent key) |
| Mini bar | `drawMiniBar` | SURFACE, ACCENT edge; play mark BUTTON with ACCENT glyph; TEXT title; DIM time; DIM_ON kind icon; pills as Pill; progress line ACCENT |
| Key (button) | `Kit::key`, `drawBox`, `drawButton`, `drawKeyedLabel` | Box BUTTON, pressed PRESSED, TEXT; primary: Box ACCENT (pressed `lipOf(ACCENT)`), ON_ACCENT; danger: DANGER ink, or Box DANGER with ON_ACCENT (Quit); lit: ACCENT ink; Off |
| Button row and grid | `rowCell`, `gridCell`, `lay::cell` | none: Rects only |
| Transport key | `Kit::transport` | as Key; the hold: BG at HOLD_FILL over the key; third line ON_ACCENT |
| State chip | `Kit::chip` | Flat SURFACE; DIM_ON icon; DIM label; TEXT value; away from normal: ACCENT icon, value and dot; Off |
| Line key | `Kit::line` | Flat SURFACE; DIM label; TEXT text (DIM when there is nothing to name); DIM note; ACCENT caps and chevron |
| List row | `Kit::row`, `drawDialogRow`, `drawFocus` | its ground (BG on a page, SURFACE in a sheet); TEXT name; DIM note and lead; focused: BUTTON fill, ACCENT edge, ACCENT name, DIM_ON note; danger: DANGER; radio and tick: ACCENT; Off |
| Pill | `Kit::pill` | live: ACCENT, ON_ACCENT; fact: TRACK, TEXT; bad: DANGER at TINT over its ground, DANGER ink |
| Filter chips and segmented choice | `Kit::segment`, `drawTab` | off: TRACK, DIM_ON; on: ACCENT, ON_ACCENT |
| Tabs | `Kit::tabs` | off: Box BUTTON, TEXT; on: ACCENT with `edgeOf(ACCENT)` and `lipOf(ACCENT)`, ON_ACCENT; caps ACCENT |
| Switch | `Kit::toggle` | off: TRACK, `edgeOf(BUTTON)`, knob DIM; on: ACCENT, `edgeOf(ACCENT)`, knob ON_ACCENT |
| Tick and radio | `Kit::row` (radio), the select tick | `edgeOf(BUTTON)` outline; on: ACCENT fill, ON_ACCENT check; radio: ACCENT ring and centre |
| Settings row (stepper) | `Kit::settingRow` | as List row; value TEXT; arrows DIM; action note DIM (DANGER when it destroys) |
| Form row | `ui_forms.cpp` | as List row; label DIM, ACCENT when focused; value TEXT |
| Bottom sheet | `Kit::sheet`, `Kit::sheetBack`, `Ui::Overlay`, `drawDialog` | veil: BG at VEIL_SHEET; Flat SURFACE; grab mark PRESSED; title ACCENT (DANGER for danger); note DIM; its Hint bar |
| Alert sheet, Confirm sheet | `Ui::AlertBox`, `drawQuitDialog`, `drawUpdatePopup` | Bottom sheet; sentence TEXT; safe key primary; the action key normal, DANGER ink when it destroys |
| Busy and job sheet | `drawBusy`, `drawFileJob` | Bottom sheet; spinner ACCENT on TRACK; Progress bar; amounts DIM |
| More sheet | `Ui::openMore`, `ui_more.cpp` | Bottom sheet; tiles as Key; rows as List row; column titles DIM |
| Book bar | new | Bottom sheet, turned; a lit Light key: ACCENT ink |
| Keyboard | `drawKeyboard` | field: SURFACE with an ACCENT outline, TEXT, cursor ACCENT; letter keys Box BUTTON, the focused one ACCENT edge; special keys TRACK; Done primary |
| Slider | `drawSlider`, `drawSlideKnob`, `drawSlideTag`, `slideGrab`, `Kit::progress` | TRACK; fill ACCENT; KNOB; chapter notches BG; the held tag ACCENT with ON_ACCENT; the two-way tick DIM |
| Progress bar | `Kit::bar` | TRACK; fill ACCENT |
| Volume bar | `drawLevel`, `drawQuick` | TRACK; TEXT to 100%, ACCENT to 120%, DANGER past it; the 100 mark DIM_ON; on the top card the TOP_ tokens |
| Tile | `Kit::settingTile`, `Kit::key` (stack) | Flat SURFACE; TEXT name; DIM summary; focused: ACCENT edge, icon and name; a choice tile in use: ACCENT fill with `edgeOf(ACCENT)`, ON_ACCENT mark and name |
| Cover cell | `drawMangaCover` | the cover is a picture (an album with none: the Art placeholder); count pill as Pill; progress as Progress bar; percent DIM; focused: ACCENT outline |
| Art placeholder | new (`Kit::artPlaceholder`) | the square BUTTON; the record BG; its inner band SURFACE; the label ACCENT; the hole BG; with no box of its own a 1-unit `edgeOf(BUTTON)` edge |
| QR code | `ui_web.cpp` | fixed black on white; the address TEXT, the sentence DIM |
| Recent card, Continue card | `drawHome` | Flat SURFACE; label DIM; TEXT title; DIM note; Progress bar; kind mark fixed (on a picture); the key primary |
| Empty state | `drawEmpty` | DIM icon; TEXT title; DIM lines; keys: primary and normal |
| Notice row | `drawExtNotice` | Flat SURFACE with its edge ACCENT (ready), DANGER (failed) or `edgeOf(SURFACE)`; TEXT; DIM second line; Progress bar; a small Key |
| Select bar, clip bar | `drawSelBar`, `drawClipBar` | keys as Key (Delete: DANGER ink); clip bar: SURFACE, ACCENT edge, TEXT, DIM size |
| Pan area, drag area | `ui_docs.cpp`, `drawPhoto` | pan area SCREEN, the box ACCENT; drag area Flat SURFACE, RULE dashes, DIM captions |
| Level card, Note, State card | `levelcard.cpp`, the ASS cards in `player.cpp` | the TOP_ tokens (the table above) |
| Preview pane | new | BG; brand tile ACCENT with ON_ACCENT, name ACCENT; label ACCENT; TEXT title; DIM_ON sentence; list DIM and TEXT; caps ACCENT |
| Song layout | `Player::shownMusic` | ground BG and the three lines TEXT, DIM_ON, DIM (Album art only, Art + visualizer, None, and Peaks or a simple style alone); the art's edge `edgeOf(SURFACE)`; no art: the Art placeholder in that box; the marks of Peaks and the six simple styles ACCENT and TEXT; fixed: the art, a missing cover's placeholder, the pixel-art styles' and Pocket console's colours, and white text with its shadow over a style that fills the screen |
| Focus | `drawFocus` | ACCENT |
| Away-from-normal mark | new | ACCENT; DANGER past a danger line |
| Screen-off warning | new | veil fixed black 62%; a Notice row with an ACCENT edge |
| Lid-shut keys | new | nothing drawn |
| Title marquee | new (`Kit::marquee`) | the text’s own colour (TEXT; ACCENT in a focused row; fixed white over a picture); the fades: the ground’s token (BG, SURFACE in the mini bar, BUTTON in a focused row), none over a picture |
| Lock overlay | `Kit::lock` | veil: BG at VEIL_LOCK; Box BUTTON (PRESSED while held) with an ACCENT edge, ACCENT lock and title; DIM_ON line |
| The reader views | `ui_reader.cpp`, `reader.*`, `textreader.*` | the page: fixed (its own colours); around a page not yet decoded: TOP_TRACK; the page-number note: a Note |

### What breaks the swap today

Found reading `src/theme.h`, `src/ui_internal.h`, `src/ui_kit.cpp`, `src/levelcard.cpp`, `web/style.css` and this spec. Each is a one-time change; after it a theme is values only.

- The tokens are `constexpr` (`theme.h`, and `ui_internal.h`'s `SDL_Color` copies), fixed when the app is built. A theme is chosen while it runs: they become one table of themes, read through the same names.
- `edgeOf` has 30 written in it: it reads EDGE_MIX.
- `levelcard.cpp` draws the boosted part of the bar with ACCENT and DANGER and the 100 mark with TEXT: TOP_TITLE, TOP_DANGER, TOP_SUB. The card's text takes TOP_TEXT.
- Scrims written as black: `{0,0,0,170}` (drawDialog, `ui_ext.cpp`) and `{0,0,0,200}` (`ui_fileops.cpp`). Both become BG at VEIL_SHEET, as `Kit::sheetBack` already does.
- The reader's ground for a page not yet decoded, `40,40,44`: TOP_TRACK.
- In this spec: the rail's #0B0A0D is RAIL; the level card's #0B0A0D is TOP_CARD; a badge's “17% yellow” is ACCENT at 17%.
- `web/style.css`: `color: #000` on a yellow or red box is ON_ACCENT; the edges and lips copied by hand (`--e-*`, `--l-*`) are worked out; `--scrim` and `--veil-sheet` are black, and become BG at their strength.

### The Web dashboard

The same table. Each theme is one block of the variables below (`:root[data-theme=…]`), written from `theme.h`'s table, not copied by hand.

| `web/style.css` | Token |
|---|---|
| `--bg`, `--surface`, `--btn`, `--pressed`, `--track`, `--rule` | BG, SURFACE, BUTTON, PRESSED, TRACK, RULE (today's `--rule` is a little darker than RULE: it takes RULE) |
| `--cream`, `--dim`, `--dim-btn` | TEXT, DIM, DIM_ON |
| `--yellow`, `--red` | ACCENT, DANGER. The names say a colour: `--accent`, `--danger`, and `--text` for `--cream`, are the names to move to |
| ink on `.box.yellow`, `.box.red` | ON_ACCENT |
| `--e-btn`, `--e-pressed`, `--e-surface`, `--e-yellow`, `--e-red`, `--e-track` | `edgeOf` of each, with EDGE_MIX |
| `--l-btn`, `--l-pressed`, `--l-surface`, `--l-yellow`, `--l-red` | `lipOf` of each |
| `--raised` (dashboard only) | SURFACE 35% of the way to BUTTON |
| `--yellow-hover`, `--yellow-down` (dashboard only) | ACCENT 40% of the way to TEXT; ACCENT × 0.88 |
| `--picked`, `--e-picked` (dashboard only) | ACCENT at 17% over BG; its `edgeOf` |
| `--dot`, `--scrim`, `--veil-sheet` | TEXT at 7%; BG at 72%; BG at VEIL_SHEET |
| `--green` (dashboard only: something is on, or worked) | a token of its own, OK: #7BD88F in the dark themes, #1F7A4D in the light-bodied ones, #8DF0A4 in High contrast |
| an album or a song with no cover | the Art placeholder, an inline SVG: `--btn`, `--bg`, `--surface`, `--yellow` (ACCENT); the same placement sum as the handheld |
| `theme-color` in `index.html` | BG |
| the Settings page's small picture of the two panels | the same tokens: the bottom panel the main ones, a card on the top one the TOP_ ones |

### Not verified

- No theme was tried on the handheld's panels or in sunlight. The ratios are worked out, not measured. The lowest accent pairs are the ones to look at first: 32-bit's and 16-bit's accent on their BUTTON (4.8 and 5.1), and Dark red's (5.6).
- The pixel-art styles over a light ground were not looked at: each was drawn to sit over album art, so most carry their own dark outlines, but the ones made of light (Aurora's curtains, Night city's lit windows, Campfire) are the ones to try first on Light, Sakura and the 16-bit themes.
- 16-bit and 32-bit were checked by their ratios and by the swatches; neither was looked at on a whole screen.
- `web/style.css` was read at its `:root` only.
- OK's three values for the dashboard were not checked for contrast.

## What was drawn two ways: now one

All seventeen are resolved in the preview; the exceptions are deliberate. The last row was added with the themes.

| What | Was drawn as | Every screen now uses | Status |
|---|---|---|---|
| Rows inside sheets | 44, 46 and 48 | 46, from one rule. The More sheet on a file is 480 tall to hold its eight rows | Unified |
| Keys at the foot of a form | 52 and 56 | 56 | Unified |
| Empty state keys | 54 and 56 | 56 | Unified |
| Header height | 50 on the player, 54 elsewhere | 54; the player’s blocks sit 4 lower | Unified |
| State chip | 146 wide with a 14 value, 115 wide with a 13 value | One function: width from the row, value 13; unavailable = the same chip at 45% | Unified |
| Key with its button | Beside the label on photo and PDF keys, under it on reader rows | Under the label on every key 60 or taller, beside it on 56-tall keys: one function | Unified, one exception: transport keys (74 tall) keep icon over label-with-cap; they are their own component |
| Slider knob | 8 × 24, 9 × 26, a taller track when held | Track 14, knob 8 × 24, held = 14 × 30 | Unified |
| Progress bars | Seven heights | 3 (card foot), 6 (rows, cards, previews), 12 (job sheets, the hold), 8 (top-screen level cards) | Unified |
| “On card” pill | Yellow and neutral | Neutral. Pills take their tone from one rule: plain = live, n = a fact, r = failed | Unified |
| Unavailable look | Dashed edge, 42%, 45%, 50% | 45%, the reason in the note | Unified, one exception: a Gone row dims its icon and name only, so its red pill stays readable |
| Notice row | 44, 52, 54, 60 | 52; 60 with a second line or a key: one rule | Unified |
| Tabs’ type | 16 and 15 | 15 | Unified |
| Closing a sheet in the hints | Sometimes only “B Close” | “(opening key)/B Close” | Unified, one exception: a song’s Display tab, where SELECT steps the Show mode and only B closes (kept from today) |
| Volume card | Yellow 230-wide on one screen | The cream 290-wide one | Unified |
| Keyboard | Two drawings | One function; the field supplies its bottom row (https://, .json, .pb, / for an address) | Unified |
| Confirm keys’ widths | Three ratios | Equal halves, the safe key left | Unified, one exception: the update sheet has three keys (Update now, Later, Not until tomorrow), in thirds |
| A sentence on the top screen | Top and foot | Help for the focused row: top. Something that happened: foot | Unified |
| Colours | Literals in places (the rail’s and the card’s #0B0A0D, black scrims, a badge’s yellow) | Tokens of theme.h only; a theme is their values (Themes) | Unified |

## Not verified

- Sizes are the preview's CSS, in units; none was tried on the handheld.
- The "Code" lines come from reading docs/notes and the function names in src/ui.h; the bodies of most draw functions were not read, so "needs a variant" is a judgement of how far today's drawing is from the preview, not a measured diff.
- The top-panel components assume the top panel can lay out cards with pictures while nothing plays; today it is mpv's overlay.
- Title marquee: the times and speeds are the preview’s, read on a desktop; none was tried on the handheld’s panels. Whether the bottom screen can redraw one Rect over the panel as last drawn was not checked in the code (if it can’t, the step goes in `signature()` for the seconds it moves: time both). That libass moves a clipped line by whole pixels without drawing its glyphs again, and what a step costs mpv in Album art only, were not measured.
