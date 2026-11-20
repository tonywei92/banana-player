#pragma once
// The redesign kit's layout maths (docs/redesign/components.md), with no window: where each piece of a row, a
// sheet or the player screen goes on the bottom panel's 640x480, and how a cursor moves over them. The drawing
// (src/ui_kit.h) and the touches ask the same functions, so a tap hits what was drawn. Tests: tests/core_test.cpp.
#include <string>
#include <vector>

namespace bp
{
	struct Track;

	namespace lay
	{
		// a box on the panel, in its 640x480 units (Ui::Rect is this)
		struct Box
		{
			int x, y, w, h;
			constexpr bool hit(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
		};

		constexpr int PANEL_W = 640, PANEL_H = 480;
		constexpr int HINTS_H = 30;	// the hint bar along the foot (a rule above it)
		// the spec's one spacing scale (components.md "Spacing"): 2 / 4 / 6 / 8 / 12 / 16 / 24, nothing in between
		constexpr int SP2 = 2, SP4 = 4, SP6 = 6, SP8 = 8, SP12 = 12, SP16 = 16, SP24 = 24;
		constexpr int MARGIN = SP16, GAP = SP8;	// a page's side margin; between the keys of a row

		// the i-th of n cells across `area`, `gap` apart. Widths that don't divide: the spare units go one each to the
		// last cells (five across 608: 115, 115, 115, 115, 116), so the row always ends on the area's right edge
		Box cell(Box area, int n, int gap, int i);
		// n cells in `cols` columns, `gap` apart, in rows that share `area`'s height (filled left to right, top to
		// bottom): the Visualizer tab's style grid (docs/redesign, "music-viz-pick")
		std::vector<Box> gridCells(Box area, int n, int cols, int gap = GAP);
		// the transport row: five keys, the middle one (play) `mid` wide, the four others sharing the rest equally
		Box transportCell(Box area, int i, int gap = GAP, int mid = 176);

		// ---- the progress bar ----
		// the knob at `share` (0..1) of the track: `w` wide, `over` taller than the track above and below
		Box barKnob(Box track, double share, int w = 8, int over = 5);
		// where each chapter starts: the x of its notch. None at the very start, none closer than minGap to the last
		// one drawn, and none at all past `most` chapters (they would be a comb)
		std::vector<int> barMarks(Box track, double duration, const std::vector<double>& starts, int minGap = 6, size_t most = 100);

		// items `gap` apart, centred between left and right: each one's x. Too wide: the gap shrinks to minGap, then
		// the row starts at `left` (its end is cut by whoever draws it)
		std::vector<int> spread(const std::vector<int>& widths, int gap, int left, int right, int minGap = 6);

		// ---- tabs, and the List sheet (Y on the player: Up next / Chapters) ----
		constexpr int TAB_H = 44, TAB_GAP = 6, TAB_PAD = SP12, TAB_CAP = 19;
		struct Tabs
		{
			Box area;
			Box left, right;	// the key caps at the ends ("L", "R")
			std::vector<Box> tab;
		};
		// a strip 44 tall: the left cap, each tab as wide as `widths` says, 6 apart, the right cap. The caps are
		// their own boxes (`capW` wide: 19 for "L" / "R", wider for "L2" / "R2"; TAB_CAP tall, centred in the strip)
		Tabs tabs(Box area, const std::vector<int>& widths, int gap = TAB_GAP, int capW = TAB_CAP);
		// A page whose body is tabs over a list (Manga's Library / History / Browse / Downloads, screens.js manga):
		// the strip at the top of the inner body (SP12 in from the body's sides), stepped by L2 / R2 (L / R are the
		// sections in the shell); the content under it, SP8 below the strip, to the body's foot. `capW` is the caps'
		// width (Kit::capsW of "L2").
		struct TabPage
		{
			Box inner;	// the body less its side padding
			Tabs strip;
			Box content;
		};
		TabPage tabPage(Box body, const std::vector<int>& widths, int capW);
		// the tab under (x, y) in a strip (the gap between two goes to neither): -1 none
		int tabAt(const Tabs& t, int x, int y);
		// the List sheet: the grab mark, a header (the tabs and the two controls), `rows` rows 46 tall, the hint bar
		constexpr int LIST_HEAD = 54, LIST_ROW = 46, LIST_ROWS = 7;
		struct ListSheet
		{
			int rows = 0, header = LIST_HEAD;
			Box box, grab, head, hints;
			int rowsY = 0;
			Box row(int i) const;
		};
		ListSheet listSheet(int rows, int header = LIST_HEAD);
		// a sheet whose content is not rows, but a body of its own height: the song's Equalizer tab, whose bands take
		// the full height a sheet may have (docs/redesign "music-eq"). Clamped to the sheet's 226..470.
		ListSheet listSheetBody(int body, int header = LIST_HEAD);
		// the first row shown so that `cursor` is one of them, the scroll kept inside the list: what LEFT / RIGHT
		// paging and a tap leave behind
		int listScroll(int cursor, int scroll, int shown, int n);
		// A finger's drag down a list: the scroll it asks for. `first` is the row the list can scroll back to (0, or -1
		// with a ".." row), `rowH` its row pitch (46 on Settings, 52 on Files, 54 on a playlist): a row per rowH the
		// finger travels, rounded, kept inside what there is. The drag slop (a finger's drift before it is a drag) is
		// the caller's; both the shell's rebuilt pages and the old list call this, so a list scrolls the same way.
		int dragScroll(int scrollStart, int downY, int y, int rowH, int first, int n, int shown);
		// the row under a finger at `y` with the list scrolled to `scroll`: a drag's lift puts the highlight there.
		// Outside the rows it is the nearest one (above them the first shown, below them the last), and always in 0..n-1
		int rowAt(int y, int rowsY, int scroll, int shown, int n);

		// ---- the bottom sheet (the More sheet: tiles, then rows in one or two columns) ----
		constexpr int SHEET_HEAD = 54, SHEET_PAD = SP16, SHEET_ROW = 46, SHEET_TILE = 60, SHEET_CHIP_H = 52, SHEET_MIN = 226, SHEET_MAX = 470;
		struct SheetSpec
		{
			int tiles = 0;	// a first row of keys, icon over label (four across)
			int rows = 0;	// list rows under them
			int cols = 1;	// the rows' columns (filled row by row)
			int body = 0;	// other content's height, between the header and the tiles
			int chips = 0;	// a first row of state chips (the player's More: its five, under the cursor)
		};
		struct Sheet
		{
			SheetSpec spec;
			Box box, grab, head, hints;
			int bodyY = 0, chipsY = 0, tilesY = 0, rowsY = 0;
			Box chip(int i) const;
			Box tile(int i) const;
			Box row(int i) const;
			int count() const { return spec.chips + spec.tiles + spec.rows; }
			// the cursor's item: the chips first, then the tiles, then the rows
			Box item(int i) const
			{
				return i < spec.chips ? chip(i) : i < spec.chips + spec.tiles ? tile(i - spec.chips) : row(i - spec.chips - spec.tiles);
			}
		};
		// full width, from the foot, as tall as its content (SHEET_MIN at least, SHEET_MAX at most)
		Sheet sheet(const SheetSpec& s);
		enum class Dir { Up, Down, Left, Right };
		// the cursor moved over the tiles and the rows (two columns: LEFT / RIGHT cross). No wrap: at an edge it stays
		int sheetMove(const SheetSpec& s, int cursor, Dir d);

		// ---- View › Display on a song (docs/redesign "music-view", "music-viz-pick"; components.md, Tile: a choice tile) ----
		// One page for both choices, 452 tall, nothing scrolls: the label "Top screen shows", the four modes in a row
		// (147 x 56, 8 apart, icon beside the name), the line naming the style, then the styles in a grid (117 x 56,
		// 6 apart, five to a row, icon over the name). The cursor is one index: the modes first, then the styles
		constexpr int DISPLAY_H = 452, DISPLAY_MODES = 4, DISPLAY_COLS = 5;
		constexpr int DISPLAY_MODE_W = 147, DISPLAY_MODE_GAP = 8, DISPLAY_STYLE_W = 117, DISPLAY_STYLE_GAP = 6, DISPLAY_TILE_H = 56;
		constexpr int DISPLAY_FOOT_H = 46, DISPLAY_FOOT_GAP = 10;	// the sheet's foot row (a switch: Match album art)
		struct DisplayPage
		{
			Sheet sheet;	// the box, grab, tab strip (head) and hint bar
			Box modesLabel, styleLine;
			Box mode[DISPLAY_MODES];
			std::vector<Box> style;
			Box foot;	// the switch row under the grid (w 0: the sheet has no room for one)
			int count() const { return DISPLAY_MODES + (int)style.size(); }
			Box item(int i) const { return i < DISPLAY_MODES ? mode[i] : style[i - DISPLAY_MODES]; }
		};
		DisplayPage displayPage(int styles);
		// the cursor moved: LEFT / RIGHT along its group (the styles run on across their rows), DOWN from a mode to the
		// style under its middle, UP from the first row of styles to the mode over it. No wrap: at an edge it stays
		int displayMove(const DisplayPage& d, int cursor, Dir dir);
		// the tile under a finger, -1 none
		int displayAt(const DisplayPage& d, int x, int y);

		// ---- the Settings page (docs/redesign: settings, settings-group) ----
		// its groups' tiles, two to a row (58 tall, 8 apart), and a group's rows (46 tall, the focused row's help at the
		// foot): the kit's header draws the title and the crumb beside its back key, so the area starts under that strip
		// (54) at 64, and seven rows fit above the help
		constexpr int SET_TILE_H = 58, SET_TILE_GAP = GAP, SET_TILE_TOP = 64;
		constexpr int SET_ROW_H = 46, SET_ROWS = 7, SET_ROW_TOP = 64, SET_HELP_Y = 400;
		// the i-th tile of n, in `cols` columns; the rows share `area`'s height evenly
		Box settingsTile(Box area, int n, int cols, int i);
		struct SettingsRows
		{
			Box area;
			Box row(int i) const;
			Box help;	// the focused row's line(s) above the hint bar
		};
		// the page without the frame (the old page's own 16-unit margins). The shell variant works the same inside
		// lay::shell().body: settingsBody is that body inset to the page's margins, settingsTiles its tile area for
		// `rows` rows of two columns, and settingsRowsIn the rows and help that fit above the body's foot
		Box settingsBody(Box body);
		Box settingsTiles(Box body, int rows);
		SettingsRows settingsRowsIn(Box body, int shown = SET_ROWS);
		SettingsRows settingsRows(int shown = SET_ROWS);

		// A row's stepper: the value centred between its two arrows, both of which are laid from the row's right
		// edge, so a row right of the rail gets the same stepper as a full-panel one. `back` is the tap that steps
		// back (the left arrow's half of the value); `fwd` is the rest of the row, the page's lift asks these.
		constexpr int STEP_VALUE_W = 238, STEP_VALUE_TEXT = 196;
		struct SettingStepper
		{
			Box row, label, value;	// the label's room; the value's box (196 wide, centred between the arrows)
			Box back, fwd;
			int prevX = 0, nextX = 0;	// the arrows' centres
		};
		SettingStepper settingStepper(Box row);
		bool stepperBack(const SettingStepper& s, int x);
		// ---- the player screen ----
		constexpr int PLAYER_CHIPS_MAX = 5;
		struct PlayerLayout
		{
			Box back, backHit, more, moreHit;	// the header's keys; their touch areas reach the panel's edges
			Box title;	// the title, and how it plays on its right
			Box line, lineHit;	// the chapter line (a key), or a song's artist and album
			Box bar, barHit, times;
			Box transport[5];	// Prev, back 5 s, Play, forward 10 s, Next
			Box keys[4];	// Like, Playlist, List, Stop
			int chips = 0;
			Box chip[PLAYER_CHIPS_MAX];
			Box next;	// the Next line
			Box hints;
			Box lock;	// the lock's key, while touch is locked
		};
		PlayerLayout playerLayout(int chips);

		// ---- About's support QR, large, on the top panel ----
		// The picture About's Y (or a tap on its small code) puts up, in the panel's own pixels (the top drawable's
		// size, so the code lands on whole pixels): a white quiet zone, the code inside it and the address's line under
		// it. As large as fits with a margin: the quiet zone takes what the height leaves once the margin, the gap and
		// the line are out.
		struct QrPanel
		{
			Box quiet, code, line;	// the white box; the code in it (a pad all round); the address under it
		};
		QrPanel qrPanel(int w, int h);

		// a chip's value for a track: its language ("English"), or "Track 2" when the file names none. Two tracks of
		// the list in that language: what tells this one apart comes after it ("English · Commentary")
		std::string chipTrack(const Track& t, int n, const std::vector<Track>& all);
		// ASCII capitals, for a chip's and a line's small label ("SUBTITLES")
		std::string caps(std::string s);

		// ==== the kit's variants (docs/notes/2026-11-18-kit-variants.md): status cluster, mini bar, filter chips, form row,
		// alert / confirm / busy sheets, keyboard, volume bar, tiles, cards, empty state, notice row, select and clip bars ====

		// ---- the status cluster and header badges: items right to left, `gap` apart ----
		constexpr int BADGE_H = 20, BADGE_PAD = 5;
		struct StatusRow
		{
			std::vector<int> x;	// each item's left edge (in the order given: the rightmost first)
			int left = 0;	// the cluster's left edge
		};
		StatusRow statusRow(int right, const std::vector<int>& widths, int gap = 8);
		int badgeW(int textW, bool glyph);	// a badge: padding both sides, its mark (16) and a gap before the text

		// ---- the mini bar's measures: no page draws it any more (the owner, 2026-11-19); Quick settings'
		// Now playing block takes its box and its progress line ----
		constexpr int MINI_H = 52, MINI_SIDE = SP12, MINI_FOOT = HINTS_H + SP8, MINI_MARK_W = 56, MINI_MARK_H = 42;
		struct MiniBar
		{
			Box box, mark, line;	// the bar; the play mark (its own touch); the 3-tall progress line along its foot
			int kindX = 0, textX = 0, rightX = 0;	// the kind icon's centre; where the title starts; the right edge of what it holds
			int cy = 0;
		};
		MiniBar miniBar();
		// the same pieces inside `box` (a Quick settings Now playing block: its progress line along the foot)
		MiniBar miniBar(Box box);

		// ---- filter chips used as filters: 44 tall (a finger's size) in a 44-tall row ----
		constexpr int CHIP_H = 44, CHIP_STRIP = 44, CHIP_GAP = 6;
		struct FilterChips
		{
			std::vector<Box> chip, hit;	// the chip as drawn; the touch area (44 tall, up to the next one)
		};
		FilterChips filterChips(Box area, const std::vector<int>& widths);
		int filterAt(const FilterChips& c, int x, int y);	// the chip a touch is on (-1 none)

		// ---- a form row: 46 tall, the label 150 wide, then the value; two keys at the foot ----
		constexpr int FORM_ROW = 46, FORM_LABEL = 150, FORM_KEY_H = 56;
		struct FormRow
		{
			Box row, label, value;
		};
		FormRow formRow(Box row);
		void formKeys(Box out[2]);	// Test (left) and Save (right)

		// ---- the alert, confirm and busy sheets (each a lay::Sheet with a body) ----
		struct AlertSheet
		{
			Sheet sheet;
			Box text, key;	// the sentence's lines; the one primary key, full width
		};
		AlertSheet alertSheet(int textLines);
		struct ConfirmSheet
		{
			Sheet sheet;
			Box text, safe, action, third, foot;	// third.w 0: two keys; foot: the "don't ask again" line (empty without one)
		};
		ConfirmSheet confirmSheet(int textLines, bool foot, bool third = false);
		struct BusySheet
		{
			Sheet sheet;
			Box file, bar, amounts, key, key2;	// the file name line, the 12-tall bar, "x of y" and the time left, Cancel
			// key2.w 0: one key across the sheet (Hide or Cancel); else the two share the row (Hide, Stop the download)
		};
		BusySheet busySheet(bool twoKeys = false);

		// ---- the keyboard: a field, a special row, three rows of letters, the bottom row ----
		// Every key of a letter row is the same width, set by the widest row so it fits the area: the keys share
		// the width the caller gives (the mock-up's 1fr grid), so a row never runs past the area's edge — the
		// panel-wide 58 of a fixed key width did, on Search's narrower shell body.
		constexpr int KB_FIELD_H = 48, KB_KEY_H = 52, KB_GAP = 5, KB_BOTTOM_H = 56, KB_SPECIAL_H = 36;
		struct Keyboard
		{
			Box field, note;
			std::vector<Box> special;	// the field's own pieces (https://, .json), as wide as their labels say
			std::vector<std::vector<Box>> rows;	// the letters, each row centred
			std::vector<Box> bottom;	// layer keys, Space, backspace, Done
		};
		// area: the keyboard's own box (the field and the bottom row span it, the letters centred in it);
		// specialW: each piece's width (empty: no special row); counts: the keys in each row of letters;
		// layers: how many layer keys open the bottom row
		Keyboard keyboard(Box area, const std::vector<int>& specialW, const std::vector<int>& counts, int layers);

		// ---- the volume bar: the 100 mark, and where each colour ends ----
		struct VolumeBar
		{
			Box bar, number;
			int tickX = -1;	// the 100 mark (-1: the range ends at 100: nothing to mark)
		};
		VolumeBar volumeBar(Box area, int max);
		int volumeX(Box bar, int value, int max);	// the x of `value` on the bar (0..max)
		// the colour's part: 0 to 100, 1 from 100 to 120, 2 past 120
		int volumeZone(int value);

		// ---- tiles with a summary: n in `cols` columns, `h` tall, 8 apart, in `area` ----
		Box tileCell(Box area, int n, int cols, int h, int i);

		// ---- the Recent card (112 x 100) and the Continue card (108 tall) ----
		constexpr int RECENT_W = 112, RECENT_H = 100, CONTINUE_H = 108;
		struct RecentCard
		{
			Box box, pic, title, note, line;
		};
		RecentCard recentCard(Box box);
		struct ContinueCard
		{
			Box box, pic, label, title, note, bar, key;
		};
		ContinueCard continueCard(Box box);

		// ======== Home and All recent (docs/redesign: home, home-first, home-stopped, recent-all) ========
		// The content column inside lay::shell().body: 12 from its sides, 6 under the header. The screens draw
		// inside these boxes only; their touches test the same ones.
		constexpr int HOME_PAD = SP12, HOME_CONTINUE_H = 108, HOME_CARD_ROW_H = 100, HOME_SHORTCUT_H = 56;
		constexpr int HOME_CARDS_MAX = 4;
		struct HomePage
		{
			Box inner;
			bool hasContinue = false;
			Box cont;	// the Continue card (w 0 without one)
			Box chipStrip;	// the filter chips' 56-tall strip (w 0 with no Recent)
			FilterChips chips;
			Box card[HOME_CARDS_MAX];	// the Recent cards shown, left to right
			int cards = 0;
			Box more;	// the chevron card after them (w 0 when all of them fit)
			Box shortcut[3];
			int shortcuts = 0;
		};
		// hasContinue: a Continue card over the Recent row; hasRecent: Recent has anything (else no chip strip, cards
		// or shortcuts: the empty state); cards: how many Recent cards (at most HOME_CARDS_MAX); more: the chevron
		// card after them (there are more than fit); chipWidths: the filter chips' measured widths. The shortcut row is
		// left out when the body leaves no room for it; Home's Now playing card is the one place for what plays.
		HomePage homePage(Box body, bool hasContinue, bool hasRecent, int cards, bool more, const std::vector<int>& chipWidths);
		// All recent: the filter chips' 28-tall strip under the header, then the rows (48 tall, as the list's pointers
		// expect: their first is at Ui::ROW_Y, 84). `shown` is what fits above the hint bar.
		struct AllRecentPage
		{
			Box inner, chipStrip;
			int rowY = 0, rowH = 48, shown = 0;
			Box row(int i) const { return {inner.x, rowY + i * rowH, inner.w, rowH}; }
		};
		AllRecentPage allRecentPage(Box body);

		// ---- the empty state: centred in the page; keys 200 x 56 ----
		struct EmptyState
		{
			Box icon, title, lines, key[2];
			int keys = 0;
		};
		EmptyState emptyState(Box page, int lineCount, int keys);

		// ---- the notice row: 52 tall (60 with a second line and a key), an optional 5-tall bar and key ----
		struct Notice
		{
			Box row, icon, text, second, bar, key;
			bool hasSecond = false, hasBar = false, hasKey = false;
		};
		Notice notice(Box area, bool second, bool bar, int keyW);	// area: x, y and width; its height is the row's

		// ---- the select bar and the clip bar, at the foot of the list above the hints ----
		constexpr int SELBAR_H = 56, CLIPBAR_H = 60, CLIP_KEY_H = 46;
		void selectBar(Box out[4]);	// Playlist, Copy, Move, Delete
		struct ClipBar
		{
			Box box, text, key[2];	// "... here" (SELECT), Cancel (X)
		};
		ClipBar clipBar();
		// ======== kit-new: Section rail, Preview pane, Book bar, Away-from-normal, Screen-off warning ========
		// (docs/redesign/components.md, each under its own name; their drawing: ui_kit.h's kit-new block and
		// previewpane.h; tests: testKitNew in tests/core_test.cpp)

		// ---- Section rail: 78 wide, full height, at the panel's left ----
		// From the top: Search's key (68 x 40, the magnifier alone), the sections (68 x 52, or 68 x 45 with seven so
		// all show at once, 2 apart), the Now playing key (68 x 44, above Settings, its slot kept while nothing plays)
		// and Settings pinned at the foot (68 x 50). When the sections don't fit their view, `scroll` moves them under
		// a finger; Settings and Search stay put (components.md, Section rail).
		constexpr int RAIL_W = 78, RAIL_CELL_W = 68, RAIL_CELL_H = 52, RAIL_CELL_H7 = 45, RAIL_GAP = 2, RAIL_PAD = 5,
			RAIL_SEARCH_H = 40, RAIL_SETTINGS_H = 50, RAIL_NP_H = 44;
		struct Rail
		{
			Box area;	// the whole strip (its rule is its last unit)
			Box search;	// Search's key, pinned at the top
			Box view;	// the sections' viewport, between Search and the Now playing key
			std::vector<Box> cell;	// every section, top down (clipped to `view` when drawn)
			Box np;	// Now playing, pinned above Settings (its slot is always there)
			Box settings;	// pinned at the foot, never scrolled
			int cellH = RAIL_CELL_H;	// 45 when seven or more show
			int scroll = 0, maxScroll = 0;	// the sections' offset, in units from their top
		};
		Rail rail(int sections, int h = PANEL_H, int scroll = 0);
		// the cell under (x, y): 0..cells-1, `cells` for Settings, -1 none (Search and Now playing are the shell's keys)
		int railAt(const Rail& r, int x, int y);
		// L / R: the section before / after, round (n: the sections L / R step through; Search is passed by)
		int railStep(int at, int dir, int n);

		// ---- The shell: a section's page (Home, Files, Music, Playlists, Books, Manga, Network, Settings) ----
		// The rail at the left; right of it the header (54), the body (from 6 under the header) and the hint bar
		// (HINTS_H) at the foot. No page has a bar at its foot any more (the owner, 2026-11-19): the body runs to the
		// hint bar, as it does on Home. The shared contract the redesigned screens draw in: a screen lays out its own
		// content in `body` and never draws over the rail, the header or the hints
		// (docs/redesign/components.md, Frame).
		constexpr int SHELL_HEAD = 54;
		struct Shell
		{
			Box rail{}, main{};	// the rail's strip; everything right of it
			Box header{}, body{}, hints{};
		};
		Shell shell();

		// ---- A playlist's page (docs/redesign: playlists, playlist, playlist-select, playlist-move, liked) ----
		// Inside `body`: an optional row of keys (Play, Shuffle and Add; Liked has no Add), then the rows, and at the
		// foot an optional select bar (3 keys) or move bar (2). Rows are 52 tall, 2 apart; the bar 56, 6 above the
		// rows' end. The rows that fit are `rows`; the caller's scroll picks which.
		constexpr int PL_KEYS_H = 56, PL_ROW_H = 52, PL_ROW_PITCH = 54, PL_BAR_H = 56, PL_FOOT_GAP = 6, PL_PAD = 8, PL_ADD_W = 64;
		struct PlaylistPage
		{
			Box area;	// the body given
			int keys = 0;	// 0, 2 (Play, Shuffle) or 3 (and Add)
			Box key[3];
			Box list;	// the rows' strip
			Box bar;	// w 0 without one
			int barKeys = 0;	// 0, 3 (Remove, Move, Add to) or 2 (Put here, Cancel)
			Box barKey[3];
			int rowsY = 0, rows = 0;
			Box row(int i) const { return {list.x, rowsY + i * PL_ROW_PITCH, list.w, PL_ROW_H}; }
		};
		PlaylistPage playlistPage(Box body, int keys, int barKeys);
		// Kit::header's boxes on the shell's header strip: the back key at the main strip's left, the More key at its
		// right, and the title's room between the margins (a page with no More has it to the right margin)
		PlayerLayout shellHeader(Box main);

		// ---- the readers' header (a book, a PDF, a comic) ----
		// The back key at the left, the title beside it with the document's line under it (Kit::header's inline title:
		// the file's name at the readers' 18 units), and at the right the bookmark key (a book's: one tap marks the
		// place) and More. Their touch areas reach the panel's edges, as the shell header's do; the title and crumb
		// stop 8 before the leftmost of them
		struct ReaderHeader
		{
			Box back, backHit, bookmark, bookmarkHit, more, moreHit;
			Box title, crumb;	// the title's line and the document's line, between the keys
			PlayerLayout strip;	// the same boxes for Kit::header (back, more, title)
		};
		ReaderHeader readerHeader(Box main, bool bookmark = true);

		// ---- Preview pane: the top screen with nothing playing (its own 640x480 units) ----
		// The brand mark top left, then a text column (from 74, 44 from the sides) or, with a cover, the cover at
		// 36, 66 (196 x 278) and the text column from 262
		struct Pane
		{
			Box brandTile, brandName;	// the 22-unit tile and the name's line beside it
			Box column;	// the text: label, title, sentence, rows, keys, top down
			Box cover;	// w 0: the text layout
		};
		Pane previewPane(bool cover);
		// the Theme row's swatches: n of 84 x 30 in rows of `perRow`, 9 apart, each with its name's line (14) under it
		// and 9 between the rows, from (left, top): eighteen as three rows of six. The row is 6 * 84 + 5 * 9 = 549, inside the
		// text column's 552
		constexpr int SWATCH_W = 84, SWATCH_H = 30, SWATCH_GAP = 9, SWATCH_NAME = 14, SWATCH_ROW_GAP = 9, SWATCH_PER_ROW = 6;
		std::vector<Box> swatches(int n, int left, int top, int perRow = SWATCH_PER_ROW);

		// ---- Book bar: the bottom sheet of Book mode, drawn upright as the handheld is held (sideways) ----
		// Upright the touch panel is 480 wide and 640 tall. The sheet's boxes are its own (0, 0 at its top left);
		// `box` is where it sits upright, on the foot
		constexpr int BOOK_W = 480, BOOK_H = 640;
		struct BookBar
		{
			Box box;	// upright, in 480 x 640
			Box grab, title, progress, value, slider, note;	// the slider row: the value, the track, the note on its right
			Box keys[4];	// two rows of two
			Box leave;	// the full-width key that leaves Book mode
			Box hints;
		};
		BookBar bookBar();
		// the panel's box (640 x 480) of an upright box: topLeft, the handheld held with the top screen on the left (its
		// pages turned a quarter clockwise: the panel's right edge is up), else on the right
		Box sideways(Box upright, bool topLeft);
		// a touch at the panel's (x, y), upright
		void uprightPoint(int x, int y, bool topLeft, int& ux, int& uy);
		// the Book bar's piece under an upright point: 0..3 a key, 4 the leave key, 5 the slider, 6 elsewhere on the
		// sheet, -1 off it
		int bookBarAt(const BookBar& b, int ux, int uy);

		// ---- Away-from-normal: one rule for a control whose value is not the normal one ----
		// Normal: the setting's name; away: the value in its place, ACCENT and a 6-unit dot; danger (past a danger line:
		// volume over 120%, offline, failed): DANGER
		enum class AwayTone { Normal, Away, Danger };
		struct Away
		{
			AwayTone tone = AwayTone::Normal;
			std::string words;	// what the control says
			bool dot = false;	// the 6-unit dot in its corner
		};
		Away away(const std::string& name, const std::string& value, const std::string& normal, bool danger = false);
		// a level (volume, brightness...): away when it isn't `normal`, danger above `dangerAbove` (<= 0: never)
		Away awayLevel(const std::string& name, int value, int normal, int dangerAbove = 0, const std::string& unit = "%");
		constexpr int AWAY_DOT = 6, AWAY_INSET = 5;	// (where the state chip has always put its dot)
		Box awayDot(Box control);	// the dot's box in a control's top right corner

		// ---- Screen-off warning: the five seconds before an automatic screen off ----
		// A 62% black veil over the panel and one notice row 52 tall, 40 above the foot, 16 from the sides
		constexpr int WARN_VEIL = 62, WARN_ROW_H = 52, WARN_FOOT = 40, WARN_SECONDS = 5;
		struct ScreenOffWarn
		{
			Box row, icon, text;	// the notice row; the moon (20); the line's room after it
		};
		ScreenOffWarn screenOffWarn(Box panel = {0, 0, PANEL_W, PANEL_H});
		// "Screen off in 4 s": the seconds left, rounded up (ms: what is left), at least 1
		std::string screenOffWords(int msLeft);

		// ---- Title marquee: a title that doesn't fit its box moves along, twice, then rests at its start ----
		// (docs/redesign/components.md, Title marquee) rest 1.5 s; move left 24 steps a second (2 units a step for type of
		// 24 and more, 1 under) till its end shows; rest 1.5 s; fade out 150 ms and in 150 ms at its start; rest 3 s;
		// again; then still at its start. One pass in a focused row
		constexpr int MARQ_REST = 1500, MARQ_END_REST = 1500, MARQ_FADE = 150, MARQ_GAP = 3000, MARQ_STEPS = 24, MARQ_FADE_W = 16;
		struct MarqueeAt
		{
			int offset = 0;	// units the text is moved left
			float offsetF = 0;	// the same, continuous (the top panel moves it smoothly; the bottom one in whole steps)
			float alpha = 1;	// the text's (the fade back to its start)
			bool fadeLeft = false, fadeRight = false;	// the ground's fades at the box's edges (the text goes on there)
			bool done = false;	// its passes over: still at its start from now on
			int step = 0;	// changes whenever what is drawn changes (a redraw's key)
		};
		// ms since it started; the text's and the box's widths (units); the type's size; passes (2, or 1 in a row).
		// A text that fits: nothing moves (done, no fades)
		MarqueeAt marquee(int ms, int textW, int boxW, int fontSize, int passes = 2);

		// ---- Files (docs/redesign/src/screens.js: files, files-playing, select) ----
		// The page's rows are 52 tall, the footer's line 24, the select bar 56 at the body's foot. The body comes
		// from lay::shell, so the rows never run under the rail, the header or the hints.
		constexpr int FILE_ROW_H = 52, FILE_FOOT_H = 24, FILE_SELBAR_H = 56, FILE_SELBAR_GAP = 8;
		struct FilesPage
		{
			Box body, rows, footer;	// footer.w 0: none
			Box bar[4];	// the select bar's keys (all w 0 when not selecting)
			int shown = 0;	// the rows that fit
			Box row(int i) const { return {rows.x, rows.y + i * FILE_ROW_H, rows.w, FILE_ROW_H}; }
		};
		// rows: how many the list has (the ones that fit are shown); select: the bar takes the foot; footer: a
		// line under the rows (not with the bar: the caller leaves it out)
		FilesPage filesPage(Box body, int rows, bool select, bool footer);
		// the header it draws before the shell lands: the back key, the More key, the crumb and title beside them
		// (the mock-up's .hd .tt), and the room the status and the read-only badge leave at its right
		struct FilesHeader
		{
			Box back, backHit, more, moreHit;
			Box title, crumb;
			int right = 0;	// where the title and crumb must end (the status cluster's left)
		};
		FilesHeader filesHeader(Box header);
		// the clip bar (a held file or several) at the body's foot: what is held, and Put here (SELECT) and Cancel (X)
		struct FilesClip
		{
			Box box, text, key[2];
		};
		FilesClip filesClipBar(Box body);
		// Info's page: the rows of one line each, scrolled; `body` is a shell's body
		constexpr int FILE_INFO_ROW = 26;
		Box fileInfoRow(Box body, int i);
		int fileInfoShown(Box body);

		// ---- Extensions, the rail's section (docs/redesign/src/screens.js: ext-section, ext-section-video,
		// ext-section-empty) ----
		// One row per extension that is on (64 tall: its 44 mark, its name, a line under it, a pill and a chevron),
		// then a 1-unit rule and Manage extensions (52). `body` is a shell's body; the page's side margin is 8, as the
		// mock-up's rows run the full width of the page. With none on the page is an empty state instead
		// (lay::emptyState(body, 2, 1): "No extensions are on" and its one key).
		constexpr int EXT_ROW_H = 64, EXT_ROW_GAP = 8, EXT_MANAGE_H = 52, EXT_PAGE_PAD = 8, EXT_RULE_GAP = 10;
		struct ExtPage
		{
			Box area;				// the page's content column
			std::vector<Box> row;	// the extensions on, top down
			Box rule, manage;		// the rule under the last row, and Manage extensions under it
		};
		ExtPage extPage(Box body, int extensions);
		// the page with none on (docs/redesign: ext-section-empty): the Extensions mark, "No extensions are on", two
		// lines, and one key 240 wide ("Manage extensions", its cap); centred in the body
		constexpr int EXT_EMPTY_KEY_W = 240;
		struct ExtEmpty
		{
			Box icon, title, lines, key;
		};
		ExtEmpty extEmpty(Box body);

		// ==== list and page pieces (2026-11-19: the Music/Videos and Books pages, Search, and the line under a
		// page's title; docs/notes/2026-11-19-kit-inventory.md) ====

		// ---- a page's own line under its title, or a group's heading over a grid ----
		// 22 tall at the area's top: the line on the left (cut with "..."), an optional note right-aligned. `caps`
		// in Kit::SectionLine sets the small capitals a group heading uses ("READING NOW · 3"); the page's line
		// ("From Music on both cards · 148 albums") is the same box in Inter 12.
		constexpr int LINE_H = 22;
		struct SectionLine
		{
			Box line, note;	// note.w 0: none
		};
		SectionLine sectionLine(Box area, bool note);

		// ---- Search's field: 48 tall, the magnifier, the text (or its placeholder), a key cap at the right ----
		constexpr int SEARCH_FIELD_H = 48, SEARCH_FIELD_PAD = 12, SEARCH_ICON = 18;
		struct SearchField
		{
			Box box, icon, text, cap;	// cap 32 wide, its right edge 12 inside the field
		};
		SearchField searchField(Box row);

		// ---- a Search result row: 50 tall, the kind's icon, the name, the note at the right ----
		// A result's kind pill (none in the mock-up's search rows, but a row says VIDEO like the album's) is drawn
		// from note.x leftwards, so the name ends before it.
		constexpr int RESULT_ROW_H = 50, RESULT_ICON = 19, RESULT_NOTE_W = 140;
		struct ResultRow
		{
			Box row, icon, name, note;
		};
		ResultRow resultRow(Box row);

		// ---- a Music/Videos row: 60 tall, its art 30 square at the left, the title and a second line, a kind
		// pill and the note (the length, the counts) at the right ----
		constexpr int MEDIA_ROW_H = 60, MEDIA_ART = 30, MEDIA_ROW_PAD = 12, MEDIA_ART_GAP = 10;
		struct MediaRow
		{
			Box row, art, name, second;
			int right = 0;	// where the note and the pill end (12 inside the row)
		};
		MediaRow mediaRow(Box row);

		// ---- a Books cover tile: a 96 x 136 cover, the title under it, and (in progress) a bar and its percent ----
		// The cover has no box of its own (the cover is the picture); the title is one line, Pixelify 13. A shelf is
		// 5 covers across 96 + 14 apart, so `coverCells` lays them at those sizes rather than dividing the width.
		constexpr int COVER_W = 96, COVER_H = 136, COVER_GAP = 14, COVER_TITLE_GAP = 6, COVER_TITLE_H = 14,
			COVER_BAR_H = 6, COVER_BAR_GAP = 4, COVER_PERCENT_W = 34;
		// the tile's own height with its title and its progress line (5 over the 6-tall bar for the percent's type)
		constexpr int COVER_CELL_H = COVER_H + COVER_TITLE_GAP + COVER_TITLE_H + COVER_BAR_GAP + 16;
		struct CoverTile
		{
			Box box, cover, title, bar, percent;	// title/bar/percent h 0: not shown
		};
		CoverTile coverTile(Box box, bool title, bool progress);
		// n tiles in `cols` columns, COVER_W wide, 14 apart, `cellH` tall (the cell's own height), top down
		std::vector<Box> coverCells(Box area, int n, int cols, int cellH);

		// ==== Music, Videos, Books and Search (2026-11-19: the pages the next screens need; docs/redesign/
		// src/screens.js: music-albums, music-artists, videos, books, search-results) ====

		// ---- an album: a 96-square cover, its name and its artist under it, a now-playing mark in the art's corner ----
		// The grid is the mock-up's `.alb`: five to a row, 14 between the columns, 12 between the rows. The album's
		// name is one line (Pixelify 13), the artist one (Inter 12); the mark is 22 square, 8 inside the art's top right.
		constexpr int ALBUM_ART = 96, ALBUM_COL_GAP = COVER_GAP, ALBUM_ROW_GAP = SP12;
		constexpr int ALBUM_TITLE_GAP = SP6, ALBUM_TITLE_H = 16, ALBUM_ARTIST_GAP = SP2, ALBUM_ARTIST_H = 14;
		constexpr int ALBUM_CELL_H = ALBUM_ART + ALBUM_TITLE_GAP + ALBUM_TITLE_H + ALBUM_ARTIST_GAP + ALBUM_ARTIST_H;
		constexpr int ALBUM_MARK = 22, ALBUM_MARK_INSET = SP8;
		struct AlbumTile
		{
			Box box, cover, title, artist, mark;
		};
		AlbumTile albumTile(Box box);
		// The albums in `cols` columns: `first` is the row at the top (a scroll, clamped), so a cell of a row
		// scrolled out lies above `area`; `shown` is the whole rows that fit. The caller draws `cell[i]`.
		struct AlbumGrid
		{
			Box area;
			int cols = 5, cellW = ALBUM_ART, cellH = ALBUM_CELL_H, colGap = ALBUM_COL_GAP, rowGap = ALBUM_ROW_GAP;
			int albums = 0, rows = 0, shown = 0, first = 0;
			std::vector<Box> cell;
			Box rowBox(int r) const;
		};
		AlbumGrid albumGrid(Box area, int albums, int first = 0, int cols = 5);
		// Manga's Library (screens.js manga: mcell): the same grid of COVER_W x COVER_H covers with their title under
		// (no bar), five across, 14 apart, rows SP12 apart; `first` is the top row (clamped)
		constexpr int COVER_GRID_CELL_H = COVER_H + COVER_TITLE_GAP + COVER_TITLE_H;
		AlbumGrid coverGrid(Box area, int n, int first = 0, int cols = 5);

		// ---- a list of rows (Manga's History: screens.js manga-history's row2, 60 tall, 2 apart) ----
		// `first` the top row (clamped so the list ends at its last row); row[k] is row first + k, those that fit
		constexpr int ROW2_H = 60, ROW2_GAP = SP2;
		struct RowList
		{
			Box area;
			int n = 0, first = 0, shown = 0, rowH = ROW2_H, gap = ROW2_GAP;
			std::vector<Box> row;
			int at(int x, int y) const;	// the row (its index in the list) under (x, y), -1 none
		};
		RowList rowList(Box area, int n, int first, int rowH = ROW2_H, int gap = ROW2_GAP);
		// a key at a row's right end (History's Continue on the focused row): keyW wide, 44 tall, SP8 in, centred
		Box rowKey(Box row, int keyW);

		// ---- an artist's row: a 40 disc (the first letter) or its art, the name, "3 albums · 34 songs" ----
		// Rows are 60 tall, as a Music/Videos row is: the name and its line on the row's middle, a chevron at the right
		constexpr int ARTIST_ROW_H = 60, ARTIST_ART = 40, ARTIST_ART_GAP = SP12;
		struct ArtistRow
		{
			Box row, art, name, second;
			int right = 0, chevronX = 0;
		};
		ArtistRow artistRow(Box row);

		// ---- Music's page head: the view chips (Albums / Artists / Playlists) at the top of the body ----
		// `chipWidths` is Kit::filterWidths' answer (the labels carry their counts); the chips are a 44-tall strip,
		// and `chips` is the same lay::FilterChips the touches test. No line over them: the "From …" line is the
		// header's crumb (screens.js music-albums), which once sat in the body as well, drawn twice.
		struct MusicHeader
		{
			Box strip;
			FilterChips chips;
		};
		MusicHeader musicHeader(Box body, const std::vector<int>& chipWidths);

		// ---- a Videos row: a 64 x 36 (16:9) thumbnail or the kind's mark, the title, the length at the right, a
		// size/date line under it, and a thin resume bar along the row's foot ----
		constexpr int VIDEO_ROW_H = 60, VIDEO_THUMB_W = 64, VIDEO_THUMB_H = 36, VIDEO_BAR_H = 3;
		struct VideoRow
		{
			Box row, thumb, name, note, second, bar;
			int right = 0;
		};
		VideoRow videoRow(Box row);
		// A folder inside Videos: a 44 tile, the folder's name and its counts, a chevron; the same 60 tall
		constexpr int FOLDER_ROW_H = 60, FOLDER_TILE = 44;
		struct FolderRow
		{
			Box row, tile, name, second, chevron;
			int right = 0;
		};
		FolderRow folderRow(Box row);

		// ---- the Books shelf: a "Reading now" strip of cover cells, then the shelf's grid ----
		// The strip is one row of lay::coverCells at COVER_CELL_H (title and progress bar); the shelf's cells are
		// COVER_H + the title (no bar, as the mock-up's shelf), five to a row, 14 apart, `first` rows scrolled.
		// With the strip shown the grid has less than a cell's height left, but at least one row is laid (as the
		// album grid lays one): the heading always has books under it, the row hanging over the grid's foot.
		constexpr int SHELF_HEAD_GAP = SP8, SHELF_GROUP_GAP = SP12;
		constexpr int SHELF_CELL_H = COVER_H + COVER_TITLE_GAP + COVER_TITLE_H;
		struct BooksShelf
		{
			Box area;
			int cols = 5;
			Box readingHead, readingStrip, shelfHead, shelfGrid;
			std::vector<Box> reading;	// the strip's cells (title and progress: lay::coverTile(cell, true, true))
			std::vector<Box> shelf;		// the shelf's cells (title only: lay::coverTile(cell, true, false))
			int rows = 0, shown = 0, first = 0;
		};
		BooksShelf booksShelf(Box body, int readingNow, int shelfCount, int first = 0);

		// ---- Search's page: the field, then a group's heading and its result rows, then Recent searches and
		// their Clear row ----
		// Groups are skipped when they hold nothing; 12 between a group and the next, 8 under a heading. The rows
		// are lay::RESULT_ROW_H (50); a recent one 50; Clear 46, 6 under the last recent. Everything is positioned;
		// the caller draws what the page's body holds.
		constexpr int SEARCH_GROUP_GAP = SP12, SEARCH_HEAD_GAP = SP8, SEARCH_CLEAR_GAP = SP6;
		constexpr int SEARCH_RECENT_H = RESULT_ROW_H, SEARCH_CLEAR_H = 46;
		struct SearchGroup
		{
			Box head;
			int firstRow = 0, rows = 0;	// its rows' place in SearchPage::row
		};
		struct SearchPage
		{
			Box area, field;
			std::vector<SearchGroup> group;
			std::vector<Box> row;		// every result row, in group order
			Box recentHead{}, clear{};	// w 0: no recent list (clear: none)
			std::vector<Box> recent;
		};
		SearchPage searchPage(Box body, const std::vector<int>& groupRows, int recentCount = 0, bool clear = true);

		// ==== Quick settings (F): the panel centred on the whole screen, over the rail (docs/redesign:
		// quick, quick-playing; components.md "Quick settings panel") ====
		// 592 wide (24 from the sides), radius 18, padding 16: the title line 48, the Now playing block 64,
		// then 8 apart the level rows (52) and the switch tiles two to a row (54), Quit 46 at the foot — as
		// tall as its rows, centred in the height above the hint bar. When they don't all fit there (the
		// handheld's seven: Volume, Top, Bottom and the three tiles), the level rows share what is left,
		// never under 24, so the block, the tiles and Quit keep their size whatever is plugged in.
		constexpr int QUICK_W = 592, QUICK_SIDE = SP24, QUICK_PAD = SP16, QUICK_GAP = SP8, QUICK_RAD = 18,
			QUICK_TITLE_H = 48, QUICK_NOW_H = 64, QUICK_ROW_H = 52, QUICK_MIN_ROW = 24,
			QUICK_TILE_H = 54, QUICK_TILE_GAP = SP8, QUICK_QUIT_H = 46,
			QUICK_STEP_W = 56, QUICK_STEP_H = 44, QUICK_LABEL_W = 96, QUICK_NUM_W = 48;
		// a level row's own pieces. `bar` is the whole strip between its − and its +: the track is drawn
		// centred in it, and a touch anywhere in it grabs the slider (the caller adds the touch's slack)
		struct QuickLevel
		{
			Box row, icon, label, minus, bar, plus, number;
		};
		struct QuickPanel
		{
			Box box;	// the panel
			Box title;	// the title line: the name at its left, the battery at its right
			Box now;	// the Now playing block, always there (idle while nothing plays)
			Box nowArt, nowPrev, nowPlay, nowNext;	// its 48 art, and Previous 52 / Play 68 / Next 52
			std::vector<QuickLevel> level;	// the volume and brightness rows, top down
			std::vector<Box> tile;		// the switch tiles, two to a row
			Box webKey{};		// the Web tile's own key, while the dashboard is on (w 0 without one)
			Box quit;
			int rowH = QUICK_ROW_H;	// the level rows' height: 52, or less when they don't fit
		};
		// levels: the volume and brightness rows; tiles: the switch tiles; webTile: which tile is the Web
		// dashboard's (-1 none); webKey: that tile's own key is drawn (the dashboard is on)
		QuickPanel quickPanel(int levels, int tiles, int webTile = -1, bool webKey = false);

		// ==== the library's pages (2026-11-19: an album's and an artist's own page, and Music's Playlists view;
		// docs/redesign/src/screens.js: music-album, music-artist, playlists) ====

		// ---- an album's or an artist's page head: the art (or the artist's disc) at the left, the title and its
		// lines beside it, the page's keys (Play, Shuffle, Add — the playlist keys' own measures) under them, then
		// the page's own content (an album's track rows, an artist's albums) ----
		constexpr int PAGEHEAD_ART = 120;	// the art's side: the artist pane's covers are 120 (components.md, Cover cell)
		constexpr int PAGEHEAD_TITLE_H = 30, PAGEHEAD_LINE1_H = 18, PAGEHEAD_LINE2_H = 16;
		struct PageHead
		{
			Box art;		// 120 square: the caller's picture, the Art placeholder, or the artist's initial disc
			Box title;		// the album's or the artist's name (F_P28)
			Box line[2];	// an album: its artist, then its counts; an artist: its counts alone (line[1].h 0)
			int lines = 0;
			int keys = 0;	// 3: Play, Shuffle, Add; 2: Play and Shuffle (Liked has no Add)
			Box key[3];
			Box content;	// where the page's rows or grid begin, and what is left of the body
		};
		PageHead pageHead(Box body, int lines, int keys);

		// ---- a track of an album's page (screens.js music-album): its number or the playing mark, the title, a
		// VIDEO pill when the track is a music video, a heart when liked, its length at the right ----
		constexpr int TRACK_ROW_H = 52;
		struct TrackRow
		{
			Box row, number, name;
			int right = 0;	// where the length (and the heart before it) end, 12 inside the row
		};
		TrackRow trackRow(Box row);

		// ---- the Playlists page's row (screens.js playlists): the list's icon or Liked's heart, the name, a Now
		// pill when it is the one playing, its counts at the right; 46 tall, as a sheet's row ----
		constexpr int PLAYLIST_ROW_H = 46;
		struct PlaylistRow
		{
			Box row, icon, name;
			int right = 0;	// where the counts end, 12 inside the row
		};
		PlaylistRow playlistRow(Box row);
	}
}
