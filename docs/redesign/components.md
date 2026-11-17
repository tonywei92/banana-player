# Components of the handheld redesign

Every piece that appears on more than one screen of the redesign preview (redesign/preview.html, 113 screens), so each is coded once. Sizes are in the bottom panel's 640 x 480 units (top-panel components: the same units on the top panel). "Code" names the existing building block it maps to; status is one of: exists, reuse / exists, needs a variant / new.

Counts: 46 components: 10 reuse, 28 variant, 8 new. (Lid-shut keys is a rule with nothing to draw, and QR code is a picture of an address: neither has an example in the preview’s Components section.)

In the preview every screen draws these through shared functions and style rules (src/kit.js, src/screens.js, src/style.css beside preview.html), the same ones the Components section renders its examples from.

Existing blocks referred to: `Ui::Overlay` (src/ui_overlay.cpp, docs/notes/overlay-stack.md), `Ui::Area` (docs/notes/ui-areas.md), `slideGrab` / src/slider.h (docs/notes/sliders.md), `rowCell` / `gridCell` / `drawKeyedLabel` (docs/notes/button-rows.md), `SettingDef` (docs/notes/settings-table.md), `drawDialog`, `drawFocus`, `drawHints`, `drawSlider`, `fillRound` / `drawBox`, and the tokens in src/theme.h.

## Frame

### Header (exists, needs a variant)

- **Used:** every bottom screen (96)
- **Anatomy and states:** 54 tall, at the top. Back key 56 × 48 (radius 10) when there is somewhere to go back to; title Pixelify 25 (19 in the viewers, where it is a file name), one line, cut with “…”; crumb under it, Inter 12, dim; status cluster; More key 56 × 48 at the right edge. Touch area of both keys runs to the panel’s edges (60 × 56).
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
- **Anatomy and states:** 78 wide, full height, background #0B0A0D, a 1-unit rule on its right. Cell 68 × 56, radius 10: icon 22 over a label Inter 600 11.5. Active cell: yellow fill, black ink, 3-unit lip. L and R key caps above the first and under the last section; Settings pinned at the foot. A red 11-unit mark on a cell that would stop what plays.
- **Inputs:** sections (icon, label, shown or not), the active one, a warning mark per cell
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
- **Anatomy and states:** 52 tall, 12 from the sides, 36 above the foot; SURFACE with a 2-unit yellow outline, radius 10. Play mark 56 × 42; kind icon 17; title Pixelify 15; time Inter 12; state pills; SELECT cap; a 3-unit progress line along its foot. With pills it drops the words “Now playing”.
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
- **Anatomy and states:** 52 tall in a page, 46 in a sheet, 60 with two lines (a track: language on the first, name · flags · format on the second). Every line is one line, cut with “…” at its end; a file name is cut in its middle, keeping its ending. A right-to-left name is drawn right to left inside its run; the row never flips. Radius 10, padding 0 12, gap 11. Slots left to right: lead (icon 20, or a tick 22, a radio 14, a number, a dot 8), name Pixelify 17 (cut with “…”), pills, note Inter 12.5 dim, trail (chevron 14, switch, a small key 44 tall, a grip). Focused: BUTTON fill, 2-unit yellow edge, the name yellow. Dimmed (gone, unavailable, offline): 45%, the reason in the note. Danger: red ink.
- **Inputs:** lead, name, second line, pills, note, trail, focused, dimmed, danger
- **Keys and touch:** UP / DOWN move, LEFT / RIGHT page, A acts, a tap acts on lift, a drag scrolls, a hold starts select mode (file lists).
- **Code:** drawPlRow, drawUpRow, drawRowIcon, drawDialogRow, drawFocus (ui.h). Needs: one row function with the slots below instead of one per list.

### Pill (mark on a row or card) (exists, needs a variant)

- **Used:** about 30 screens
- **Anatomy and states:** Inter 700 11, padding 3 6, radius 5, an optional 12-unit icon. Three tones: yellow fill, black ink = live or wants attention (Playing, Now, Here, Update, 12 / 40); neutral = TRACK fill, cream ink = a fact (On card, Added, Yours, Queued, Paused); red = 20% red fill, red ink (Gone, Failed, 18+).
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

- **Used:** sheets (View, List, Contents) and sections (Manga, Sources, a source)
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
- **Anatomy and states:** Full width from the foot; SURFACE, 2-unit edge, top corners 18; the page behind under a 68% black veil. Grab mark 44 × 4. Header 54: icon 20, title Pixelify 20 yellow (red for danger), a right note Inter 12.5. Body padding 0 14. Its own hint bar. Height by content, 226 to 470; in a tabbed sheet each tab has its own height (the Equalizer takes 470). One of a kind open at a time; a sheet may open over a sheet.
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

- **Used:** rename, names, search, addresses, passwords
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
- **Anatomy and states:** A level bar whose range runs to Max volume: cream to 100%, yellow from 100 to 120%, red past 120%; a tick at 100%. The number takes the colour of the part it is in.
- **Inputs:** value, max
- **Keys and touch:** As a slider in Quick settings; the volume keys elsewhere.
- **Code:** drawLevel (top card) and drawQuick’s bar. Needs: the three colours and the 100 mark as one function.

## Cells

### Tile (exists, needs a variant)

- **Used:** Settings (12), Menu (8), Home shortcuts, Quick settings
- **Anatomy and states:** SURFACE, 2-unit edge, radius 14. Settings tile 265 × 58: icon 22, name Pixelify 17, a live summary Inter 12 dim. Menu tile about 146 × 84: icon 26 over name 17 over a note. Shortcut and toggle tiles 56 tall. A toggle tile may carry a second line Inter 12 dim: its state in words (“Lid or sleep: closes all”) or, at 45%, the reason it can’t be changed (“Off in incognito”). Focused: yellow edge, icon and name yellow.
- **Inputs:** icon, name, summary, focused, mark
- **Keys and touch:** D-pad moves in two dimensions, A opens.
- **Code:** Home’s tiles (drawHome), Settings’ group tiles (drawSettings). Needs: the live summary line.

### Cover cell (exists, reuse)

- **Used:** Books, Manga library, a source’s grid
- **Anatomy and states:** 96 × 136, radius 5, five to a row, 14 apart. A count pill top right (unread). Under it, when in progress: a 4-unit bar and a percent, Inter 12. Focused: a 3-unit yellow outline 2 away. Dimmed 45% while it can’t open.
- **Inputs:** image or drawn cover, title, author, badge, progress, dimmed
- **Keys and touch:** D-pad moves, A opens, drag scrolls.
- **Code:** drawMangaCover (real cover decoded in the background, a drawn one standing in). Books uses the same.

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

- **Used:** Books, an empty folder, Repositories, Network, Playlists, Recent
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
- **Anatomy and states:** Top right, 20 from the edges, 290 wide (200 with no bar), #0B0A0D, radius 18, padding 14 18. A small label Inter 12 dim over the value Pixelify 19, or icon + name + value on one line; a bar 8 tall under it; an optional third line Inter 12. Fades after a moment.
- **Inputs:** icon, label, value, share, tone, third line
- **Keys and touch:** Shown by the key that changed the level.
- **Code:** drawLevel and the chapter / EQ cards in main.cpp (ASS on the top panel, theme.h’s TOP_CARD, R_TOP_CARD). Needs: cream for normal levels.

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

- **Used:** every shell screen with nothing playing (about 40)
- **Anatomy and states:** Brand mark top left (22 tile + name 15). Then one of two layouts: text (a label in capitals Inter 600 11 yellow, a title Pixelify 28, a sentence Inter 14.5, a two-column list Inter 13.5, a key line) from 74 down, 44 from the sides; or cover (a 196 × 278 cover at 36, 66 with the same text column from 262). A picture only when one already exists.
- **Inputs:** label, title, text, rows, hints, cover, picture
- **Keys and touch:** None: it follows the cursor.
- **Code:** New. The top panel is mpv’s overlay today (the idle logo card in player.cpp); this needs a laid-out card with an optional picture while nothing plays.

### Song layout (exists, needs a variant)

- **Used:** every song screen
- **Anatomy and states:** Album art alone: 260 square, centred. With Song info: art 240 left, three lines right (28 / 17 / 14), the group centred on its real width with 40 margins, the text centred on the art. Art + visualizer: the same in the space above a band of bars 148 tall along the foot. No art: a placeholder in the art’s place.
- **Inputs:** Show (Album art only / Art + visualizer / Visualizer only / None: four radio rows on View › Display), the visualizer’s style, song info, art or none, title, artist, album. Show and the style are two inputs, set in two places (View › Display, View › Visualizer; Settings › Music & sound › Music display, Visualizer) and never written as one value. With Album art only or None the style is kept, can be changed, and is marked “not shown”, not dimmed.
- **Keys and touch:** SELECT steps Show (on View › Display).
- **Code:** The music graph and the title lines in player.cpp (Player::shownMusic). A change to today’s layout.

## Rules

### Focus (exists, reuse)

- **Used:** everywhere
- **Anatomy and states:** One meaning, three drawings by what is focused: a key or tile = its edge turns yellow; a row = BUTTON fill, yellow edge, yellow name; a cover = a 3-unit yellow outline 2 away. Never a second colour, never a glow.
- **Inputs:** kind
- **Keys and touch:** The D-pad moves it; touch does not show it.
- **Code:** drawFocus (ui.h) and ACCENT.

### Away-from-normal mark (new)

- **Used:** chips, the Light key, header badges, mini bar pills, slider rows
- **Anatomy and states:** Yellow; the value written in place of the setting’s name; a 6-unit dot in the control’s corner. Where the control isn’t on screen: the badge (header) or pill (mini bar). Red only past a danger line (volume over 120%, offline, failed).
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

### Lock overlay (exists, needs a variant)

- **Used:** the player, locked
- **Anatomy and states:** An 80% black veil over the controls; a 120-square key (radius 30, yellow edge) with a lock 54; “Touch is locked” Pixelify 22 yellow; one line Inter 14.
- **Inputs:** held share
- **Keys and touch:** Hold the lock 1 s. Buttons still work.
- **Code:** The lock state in ui_main.cpp:391 (“Hold the lock 1 s to unlock”).

## What was drawn two ways: now one

All seventeen are resolved in the preview; the exceptions are deliberate.

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

## Not verified

- Sizes are the preview's CSS, in units; none was tried on the handheld.
- The "Code" lines come from reading docs/notes and the function names in src/ui.h; the bodies of most draw functions were not read, so "needs a variant" is a judgement of how far today's drawing is from the preview, not a measured diff.
- The top-panel components assume the top panel can lay out cards with pictures while nothing plays; today it is mpv's overlay.
