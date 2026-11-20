// The redesign's component kit (docs/redesign/components.md): the pieces the redesigned screens are drawn from, each
// written once. A component is a function of Ui::Kit that takes a Rect and a small struct of inputs and draws; it
// keeps nothing, so the screen's state stays the screen's (its area's struct), and the Rect it is given is the one
// the screen's touches test (a row or a sheet's Rects come from src/kitlayout.h, which has no window and is tested
// in tests/core_test.cpp). Built on Ui's primitives (fillRound, drawBox, drawTextMid, the glyphs of ui_glyphs.h) and
// theme.h's tokens; type sizes are ui_internal.h's F_* (the mock-ups' Pixelify Sans and Inter sizes).
// How to build a screen with it: docs/notes/2026-11-16-redesign-kit.md.
//
//   caps / capsW   key caps: a button's name in a yellow outline ("A", "L2/R2", "LEFT/RIGHT" as arrows)
//   hints          the hint bar along the foot: caps, then what they do
//   key            a key (button): icon, label and cap side by side (56 tall), the cap under them (60 and taller),
//                  or icon over label (a sheet's tile); normal, primary (yellow), danger (red ink), focused, off
//   transport      a transport key (74 tall): icon over label-with-cap; the play key's hold fill and third line
//   iconKey        a key that is only a mark (the header's back and More)
//   chip           a state chip: icon, a small label in capitals, the value under it; yellow with a dot when the
//                  value is away from normal
//   line           a line key: a full-width key that reads as a line (the chapter line, the Next line)
//   progress       the 14-tall bar with a knob and chapter marks (a slider: its knob grows and shows a tag when held)
//   bar            a progress bar that isn't touched (3, 6 or 12 tall)
//   sheet          a bottom sheet's body: the veil, the box, the grab mark, the header (lay::sheet gives its Rects)
//   sheetBack      the same body without a header (the List sheet draws tabs and controls in its place)
//   tabs           a strip of tabs 44 tall, a key cap at each end (a sheet's List / View)
//   segment        a row of two or three small chips, the chosen one filled yellow (Repeat Off / All / One)
//   row            a list row: lead icon, number or radio, name, pill, note, and a cap, a chevron or a switch at its end
//   pill, toggle   a mark on a row; a switch (never the touch target: its row is)
//   lock           the lock over a screen whose touch is locked
//   flat           a box with an edge and no lip (SURFACE pieces: chips, lines)
// Unavailable ("off") is one rule everywhere: the piece at theme::OFF percent, nothing else changed.
#pragma once
#include "ui_internal.h"
#include "artbackdrop.h"
#include <functional>

namespace bp
{
	struct Ui::Kit
	{
		Ui& ui;
		explicit Kit(Ui& u) : ui(u) {}

		// a piece that stays Classic in the Clean style (the photo viewer's on-screen keys, the keyboard, Home's
		// shortcuts): alive for the drawing only. A named key needs no scope: Kit::key and Kit::transport are always
		// Classic (components.md "Clean style", rule 7)
		struct ClassicHere
		{
			ClassicHere() { ++theme::classicDepth; }
			~ClassicHere() { --theme::classicDepth; }
			ClassicHere(const ClassicHere&) = delete;
			ClassicHere& operator=(const ClassicHere&) = delete;
		};

		int textW(const std::string& s, int font) const;	// in 640x480 units
		// s cut with "..." at a character boundary until it fits `room` (the cut Ui::drawTextAt makes), for
		// placing something right after the name: a pill that follows it
		std::string elided(const std::string& s, int font, int room) const;
		static SDL_Color off(SDL_Color c, bool isOff = true);	// c as an unavailable piece shows it
		static SDL_Color edgeOf(SDL_Color fill);
		static SDL_Color cardFill(SDL_Color c);	// Clean: SURFACE becomes BUTTON (a fill carries a card or tile's shape)
		void ring(Rect r, int rad, SDL_Color c);	// a 2-unit outline (the focus ring), the inside left alone
		void flat(Rect r, SDL_Color fill, int rad, SDL_Color edge);
		// the art placeholder: a record in its sleeve in the theme's roles (artph.h), r a square; key: the album's name,
		// else the title, else the file's name. rad: the art box's radius; edge: a 1-unit edgeOf(BUTTON) edge inside the
		// square (where the art has no edge of its own; a Cover cell has none)
		void artPlaceholder(Rect r, const std::string& key, int rad = 5, bool edge = true);

		// ---- key caps and the hint bar ----
		// keys: one name or several apart by '/' ("L2/R2"); LEFT, RIGHT, UP, DOWN are drawn as arrows. ground: what
		// the cap is drawn on (its inside is that colour). Returns the width drawn
		int capsW(const std::string& keys) const;
		int caps(const std::string& keys, int x, int cy, SDL_Color ink = ACCENT, SDL_Color ground = BG);
		void hints(const std::vector<Hint>& list);	// (Ui::Hint: key, verb)
		// the same bar inside `bar` (the shell's hint bar sits right of the rail: lay::shell().hints): it fills that
		// box, rules its top edge and centres the items between its sides
		void hints(Rect bar, const std::vector<Hint>& list);
		// the app's mark (components.md "Brand mark"): the clapperboard on a 22 grid, `size` units square, from the
		// theme's roles (BUTTON body, BG clapper bar with ACCENT stripes, SURFACE slate, ACCENT play, DIM/ACCENT/TEXT
		// progress). One drawing, one state
		void brandMark(int x, int y, int size = 22);

		// ---- the header (every redesigned page has one) ----
		// The strip a player layout gives the back key, the More key and the crumb, then the title under it (and the
		// state on its right). `right` draws the badge and status cluster from `right` (its x) leftwards and returns
		// the left edge it used, so the crumb can end before it. `inlineTitle` puts the title beside the back key with
		// the crumb under it, as the redesign's own header does (.hd .tt): the Settings pages and About, which have no
		// state and so no title row of their own. See docs/redesign/components.md "Header".
		struct Header
		{
			std::string title, crumb;	// title in F_P28 (the title's own line, or titleFont); crumb in F_I12 (the strip)
			int titleFont = F_P28;	// the readers' file name is 18 units (their own line, not the player's 28)
			const Glyph* mark = nullptr;	// a small mark before the crumb (a heart, a folder)
			bool back = true, backPressed = false, more = true, morePressed = false;
			bool crumbEnd = false;	// keep the crumb's end (a folder's path), else cut its start (a playlist's name)
			bool inlineTitle = false;	// the title beside the back key, the crumb under it (no title row)
			// the player's title: the Title marquee, and the title as shown in the room it gets (Repeated start)
			bool marquee = false;
			std::function<std::string(int room)> titleFor;
			// the title's right: "Playing", "Paused in Chrome" (a mark before it too)
			std::string state;
			const Glyph* stateMark = nullptr;
			SDL_Color stateInk = ACCENT;
			std::function<int(int right, int cy)> right;	// the strip's right slot; returns its left edge
		};
		void header(const lay::PlayerLayout& l, const Header& h);

		// ---- keys ----
		enum class Tone { Normal, Primary, Danger };
		struct Key
		{
			const Glyph* glyph = nullptr;
			std::string label, key;	// key: the button that does the same ("" none shown)
			Tone tone = Tone::Normal;
			bool pressed = false, focused = false, off = false;
			bool lit = false;	// the icon and label in yellow (on: the heart once liked)
			bool stack = false;	// icon over label (a sheet's tile)
			bool quiet = false;	// the page asks for a quiet look (a row's steppers): in Clean, no ground at all
			double glow = 0;	// 0..1: a soft yellow swell over the key (it asks to be noticed)
		};
		void key(Rect r, const Key& k);
		struct Transport
		{
			const Glyph* glyph = nullptr;
			std::string label, key;
			bool primary = false, pressed = false, off = false;
			double hold = -1;	// 0..1: held this far (a darker fill growing left to right); -1: not held
			std::string third;	// a third line ("Hold: play it here")
		};
		void transport(Rect r, const Transport& t);
		// a key that is only a mark. quiet: the header's Back and More, a glyph alone in Clean (rule 5)
		void iconKey(Rect r, const Glyph& g, bool pressed, int cell = 2, bool quiet = false);

		// ---- state chips and line keys ----
		struct Chip
		{
			const Glyph* glyph = nullptr;
			std::string label, value;
			bool away = false;	// the value isn't the normal one: yellow, with a dot
			bool off = false, focused = false, pressed = false;
		};
		void chip(Rect r, const Chip& c);
		struct Line
		{
			std::string label;	// a small label in capitals before the text ("NEXT")
			const Glyph* glyph = nullptr;
			std::string text, note, keys;	// note: dim, on the right; keys: caps before the chevron
			bool chevron = true, pressed = false, focused = false;
			bool dim = false;	// the text dim (nothing to name)
			int font = F_P16;
		};
		void line(Rect r, const Line& l);

		// ---- bars ----
		// marks: lay::barMarks; held: under a finger (Ui::slideHeld), tag: the value over the knob then
		void progress(Rect track, double share, const std::vector<int>& marks, bool held = false, const std::string& tag = "");
		void bar(Rect r, double share);

		// ---- sheets and rows ----
		struct SheetHead
		{
			const Glyph* glyph = nullptr;
			std::string title, note;
			bool danger = false;
		};
		void sheet(const lay::Sheet& s, const SheetHead& h);
		// the same header over a list sheet's box and head (a sheet of rows, no tiles)
		void sheet(const lay::ListSheet& s, const SheetHead& h);
		// the veil, the box and the grab mark: what a sheet draws before its own content (the header too, if it has
		// one; the List sheet has tabs and controls instead)
		void sheetBack(Rect box, Rect grab);

		// ---- tabs and a segmented choice ----
		// each tab's box: as wide as its label in F_P14 plus 26, 44 tall, 6 apart, the caps' room at both ends
		// (caps false: no key caps are drawn, so the strip starts at the area's left edge)
		std::vector<Rect> tabRects(Rect area, const std::vector<std::string>& labels, bool caps = true) const;
		// keys: the caps at its ends ("L/R" in a sheet); on: the open tab (-1 none)
		void tabs(Rect area, const std::vector<std::string>& labels, int on, const std::string& keys = "L/R");
		// the same on a page's ground (BG, not a sheet's SURFACE): the caps' ground, the pressed tab (-1 none)
		void tabs(Rect area, const std::vector<std::string>& labels, int on, const std::string& keys, SDL_Color ground,
			int pressed = -1);
		// a tab's width for its label (F_P14 plus its padding), and the caps' width for `keys` ("L2/R2": the wider)
		int tabW(const std::string& label) const;
		// a key's width for its label and its cap (a row's Continue · A): F_P14 and the cap, SP12 each side
		int keyW(const std::string& label, const std::string& keys) const;
		int tabCapW(const std::string& keys) const;
		// options shared across `area` 6 apart, 40 tall (the caller's area): the chosen one yellow with black ink
		void segment(Rect area, const std::vector<std::string>& options, int on);
		enum class Pill { None, Live, Fact, Bad };	// yellow: live or wants attention; neutral: a fact; red: failed
		enum class Trail { None, Chevron, SwitchOff, SwitchOn, Grip, Heart };
		struct Row
		{
			const Glyph* glyph = nullptr;
			std::string lead;	// a number in the icon's place (a list's place), when there is no icon
			const Glyph* leadGlyph = nullptr;	// a mark in the lead's place (a playing row's play mark)
			bool numberThenIcon = false;	// a playlist's row: its place, then its kind's icon, then the name
			bool tick = false, tickOn = false;	// a select box in the first slot (a playlist's select mode)
			bool radio = false;	// a choice's mark in the icon's place (View's tracks, sizes and speeds)
			bool radioOn = false;	// the one in use: G_RADIO_ON in yellow
			bool heart = false;	// a small heart before the note (an item of a playlist that is liked)
			std::string name, note, key, pill;
			Pill tone = Pill::Live;
			Trail trail = Trail::None;
			bool focused = false, pressed = false, off = false, danger = false, lit = false;
			int font = F_SMALL;	// the name's (a sheet's two-column rows: F_P16)
			SDL_Color under = SURFACE;	// what the row lies on: a sheet (a page: BG)
			// An extension's row: a 44 x 44 mark at the left (its picture), and a second line under the name (its
			// counts), as the mock-up's ext-section row has them. The name moves up to leave the line its room.
			std::function<void(Rect)> mark;
			std::string second;
		};
		void row(Rect r, const Row& row);
		int pill(int right, int cy, const std::string& text, Pill tone = Pill::Live);	// its width
		int pillW(const std::string& text) const;	// the width `pill` draws (text + its 12 of padding), for laying a run out
		void toggle(int x, int cy, bool on);	// 40 x 22, its left edge at x
		// ---- a sheet of settings controls (lay::controlSheet) ----
		struct SliderRow
		{
			std::string label, value;
			double share = 0;
			bool focused = false, held = false, away = false;	// away: the value in yellow (not at its normal)
		};
		void sliderRow(Rect row, Rect track, const SliderRow& s);	// the label and value on a line, the bar under
		// a label over a segmented choice (Kit::segment): the row lit when focused
		void choiceRow(Rect row, Rect seg, const std::string& label, const std::vector<std::string>& options, int on, bool focused);

		// ---- the Settings page's pieces (docs/redesign/components.md: "Settings row (stepper)", "Settings tile") ----
		// a row: the label left, the value in a stepper at the right (< value >), or an action's note and a chevron.
		// 46 tall; the focused row lit, its label and value yellow; off: the 45% look with the note in the value's place
		struct SettingRow
		{
			std::string label, value;
			bool focused = false, pressed = false, off = false, action = false;
		};
		void settingRow(Rect r, const SettingRow& s);
		// a group's tile: the mark, its name and a live summary (58 tall, the mock-up's "Settings tile")
		struct SettingTile
		{
			const Glyph* glyph = nullptr;
			std::string name, summary;
			bool focused = false, pressed = false;
		};
		void settingTile(Rect r, const SettingTile& t);

		// ---- the lock over a screen ----
		void lock(Rect key, double held);	// held 0..1: the hold that unlocks, filling

		// ==== KIT VARIANTS (ui_kit_variants.cpp; docs/notes/2026-11-18-kit-variants.md): the redesign's second set ====
		// The sheets build on `sheet` and are drawn from a Rect set (lay::alertSheet ...) that the overlay's touches
		// test too; none replaces a dialog yet. Every piece takes `off` (the 45% look) and `focused` (ACCENT edge).

		// ---- status cluster and header badges ----
		// a badge: 20 tall, a mark (16) and a small label in capitals, ACCENT on ACCENT at 17% (red: DANGER). Returns its width
		struct Badge
		{
			const Glyph* glyph = nullptr;
			std::string text;
			bool danger = false;
		};
		int badge(int right, int cy, const Badge& b);
		struct Status
		{
			std::vector<Badge> badges;	// boost, browser, incognito, Wi-Fi off, read only: left of the marks
			bool playing = false;	// the playing bars (3 x 13)
			int wifi = -1;	// 0..4 bars, -1: none shown
			int battery = -1;	// percent, -1: none shown
			bool charging = false;
			std::string clock;
			int tick = 0;	// the playing bars' step
		};
		// drawn from `right` leftwards; returns the left edge it used (so the crumb can end before it)
		int status(int right, int cy, const Status& s);

		// ---- the mini bar (the variant: a wider play mark, state pills, a progress line) ----
		struct Mini
		{
			const Glyph* kind = nullptr;	// the kind icon (a note, a film)
			std::string title, time;
			bool paused = false, err = false, pressed = false, markPressed = false;
			double hold = -1;	// 0..1: the mark held this far (taking playback over from a browser)
			double progress = 0;
			std::vector<std::pair<std::string, Pill>> pills;	// speed, look, repeat, EQ, "Playing in ..."
			std::string key = "SELECT";	// the cap ("" none: Button hotkeys off)
		};
		void miniBar(const lay::MiniBar& l, const Mini& m);

		// ---- filter chips used as filters (beside `segment`) ----
		struct FilterItem
		{
			std::string label;
			int count = -1;	// -1: none
		};
		std::vector<int> filterWidths(const std::vector<FilterItem>& items) const;
		// the chips in `area` (44 tall: lay::filterChips); on: the chosen one; focused: the D-pad's
		void filters(const lay::FilterChips& c, const std::vector<FilterItem>& items, int on, int focused = -1, int pressed = -1);

		// ---- a form row (Add connection, Add a server) ----
		struct FormField
		{
			std::string label, value, placeholder;
			bool secret = false, focused = false, pressed = false, off = false;
		};
		void formRow(const lay::FormRow& l, const FormField& f);

		// ---- the alert, confirm and busy sheets ----
		struct AlertSpec
		{
			std::string title, text, key = "OK";
			bool danger = false, pressed = false;
		};
		std::vector<std::string> sheetText(const std::string& text) const;	// the sentence wrapped to the body's width (3 lines)
		void alertSheet(const lay::AlertSheet& l, const AlertSpec& a, const std::vector<std::string>& lines);
		struct ConfirmSpec
		{
			std::string title, text, safe = "Cancel", action = "OK", foot;
			// a third key (the update question's "Not until tomorrow", a failed update's "Details"): the three go
			// across the sheet in this order (safe, action, third). B stays the action, as with two
			std::string third, thirdKey;
			bool danger = false;	// the action's ink red: it destroys
			bool titleDanger = false;	// the title in red, the keys as they are (a failure, not a destroying action)
			int focus = 0;	// 0 the safe key (yellow, focused, B), 1 the action, 2 the third
			int pressed = -1;
			bool footOn = false;	// the "don't ask again" line ticked
			std::string footKey;	// the cap that ticks the line ("Y"); "" none
		};
		void confirmSheet(const lay::ConfirmSheet& l, const ConfirmSpec& c, const std::vector<std::string>& lines);
		struct BusySpec
		{
			std::string title, file, left, right;	// left: "3 of 12 \xC2\xB7 25%", right: the time left (Inter 13)
			double share = -1;	// -1: the amount isn't known (no bar)
			const Glyph* glyph = nullptr;	// the job's icon; none: the spinner
			int tick = 0;	// the spinner's step
			std::string key = "Cancel", cap = "B";
			std::string key2, cap2;	// a second key beside it (the update's Stop), when the job has one ("" none)
			int pressed2 = -1;
			bool pressed = false;
			bool primary = false;	// the one key is the sheet's action (a download's Hide), not a Cancel
		};
		void busySheet(const lay::BusySheet& l, const BusySpec& b);

		// ---- the keyboard (the variant: a special row per field, Done yellow) ----
		struct KeyboardView
		{
			std::string title, value, note;
			size_t caret = 0;
			bool secret = false, caretOn = true;
			std::vector<std::string> special;	// the field's pieces ("https://", ".json")
			std::vector<std::vector<std::string>> rows;	// the letters, by row
			std::vector<std::string> layers;	// the layer keys' labels ("abc", "123")
			std::vector<std::string> bottomCaps;	// the buttons of: each layer key, Space, backspace, Done ("L3", "Y", "X", "START")
			int cursor = -1;	// the focused key: the specials, then the letters row by row, then the bottom row
			int pressed = -1;
			int layerOn = -1;	// the layer key that is the open one
		};
		void keyboard(const lay::Keyboard& l, const KeyboardView& v);

		// ---- the volume bar: TEXT to 100%, ACCENT to 120%, DANGER past it; a mark at 100 ----
		static SDL_Color volumeInk(int value);
		void volumeBar(const lay::VolumeBar& l, int value, int max, bool off = false);

		// ---- a tile with a summary (Menu tile ~146 x 84 icon over name over note; a 56-tall shortcut or toggle tile) ----
		struct Tile
		{
			const Glyph* glyph = nullptr;
			std::string name, note, mark;	// note: the live summary, or (off) the reason; mark: a small pill at a corner
			bool focused = false, pressed = false, off = false, lit = false;
			bool classic = false;	// Home's shortcuts: the Classic flat piece (SURFACE and its edge, radius 10)
		};
		void tile(Rect r, const Tile& t);
		// a choice tile, one of a set (View › Display: the modes, icon beside the name; the styles, icon over it): the one
		// in use filled ACCENT with ON_ACCENT ink, the cursor the ACCENT edge (and, on the one in use, an ACCENT ring 2
		// clear of it: its edge would not show on its own fill)
		struct Choice
		{
			const Glyph* glyph = nullptr;
			std::string name;
			bool on = false, focused = false, pressed = false;
			bool over = false;	// the icon over the name (else beside it)
			int font = F_P13;
		};
		void choiceTile(Rect r, const Choice& c);

		// ---- the Recent card and the Continue card ----
		struct Card
		{
			const Glyph* kind = nullptr;	// the kind mark on the picture
			std::string label, title, note, key;	// label: "Continue", "Now playing" (the Continue card)
			const Glyph* keyGlyph = nullptr;
			double progress = -1;	// -1: none
			bool focused = false, pressed = false, keyPressed = false, off = false;
			std::function<void(Rect)> picture;	// draws the picture's own (a cover); none: a TRACK square with the kind mark
		};
		void pictureOf(Rect r, const Card& c, int rad);
		void recentCard(const lay::RecentCard& l, const Card& c);
		void continueCard(const lay::ContinueCard& l, const Card& c);

		// ---- the empty state ----
		struct Empty
		{
			const Glyph* glyph = nullptr;
			std::string title;
			std::vector<std::string> lines;	// each a line (Inter 14 dim, at most 400 wide); a page wraps its sentences first
			// a word drawn in ACCENT inside a line, as the preview's `hl()` marks a folder an empty page names
			// ("Music", "Videos or Movies"): the byte range in that line. Any number, in line order
			struct AccentWord
			{
				int line = 0, at = 0, len = 0;
			};
			std::vector<AccentWord> accent;
			std::string key1, key2;	// A, Y ("" none)
			const Glyph *glyph1 = nullptr, *glyph2 = nullptr;
			bool pressed1 = false, pressed2 = false, hotkeys = true;
		};
		void emptyState(const lay::EmptyState& l, const Empty& e);

		// ---- the notice row ----
		enum class NoticeTone { Working, Ready, Failed };
		struct NoticeRow
		{
			NoticeTone tone = NoticeTone::Working;
			const Glyph* glyph = nullptr;
			std::string text, second, key, cap;
			double share = -1;	// -1: no bar
			bool pressed = false;
		};
		void notice(const lay::Notice& l, const NoticeRow& n);

		// ---- the select bar and the clip bar ----
		struct BarKey
		{
			const Glyph* glyph = nullptr;
			std::string label, key;
			Tone tone = Tone::Normal;
			bool pressed = false, off = false, primary = false;
		};
		static Key barKey(const BarKey& b);
		void selectBar(const lay::Box keys[4], const BarKey k[4], int n = 4);	// n keys (the others' boxes are empty)
		struct Clip
		{
			std::string what, size;	// "3 files held" and how big
			BarKey put, cancel;	// put: "... here" (SELECT), cancel: (X)
		};
		void clipBar(const lay::ClipBar& l, const Clip& c);

		// ==== list and page pieces (ui_kit_variants.cpp; docs/notes/2026-11-19-kit-inventory.md): the Search
		// field and its result row, a Music/Videos row, a Books cover tile, a page's own line, and the state pills
		// the mini bar's state pills and Home's Now playing card share. Layouts: kitlayout.h's same block. ====
		// The state pills are the kit's one helper that reads the Player (a pill's words are the same everywhere):
		// only what is away from normal, in yellow: the speed, Shuffle or Repeat one, the EQ preset and the sound
		// effect, and where a browser plays it. Home's Continue card, Quick settings' Now playing block and the
		// shell's Now playing key (its bars) show these
		// (components.md, "Pill", "Mini bar", "Continue card"); all are Live
		std::vector<std::string> statePills(const Player& p) const;
		// Search's field: 48 tall, focused (ACCENT edge) while the keyboard has it; value or placeholder; a key cap
		struct SearchFieldView
		{
			std::string value, placeholder = "A name, or part of one", key = "A";
			bool focused = true, secret = false;
		};
		void searchField(const lay::SearchField& l, const SearchFieldView& f);
		// A result row: the kind's mark, the name with the part that matched in ACCENT, an optional kind pill and
		// the note at the right (where it is: "Videos / Movies · 9:56")
		struct Result
		{
			const Glyph* glyph = nullptr;
			std::string name, note, pill;
			int matchAt = -1, matchLen = 0;	// the matched part, in `name`'s own bytes (-1: nothing to mark)
			bool focused = false, pressed = false, off = false;
		};
		void resultRow(const lay::ResultRow& l, const Result& r);
		// A Music/Videos row: its art in a 30 square (none: the Art placeholder keyed by `artKey`, else the kind's
		// icon there), the title, a second line, a kind pill at the right and the note (the length, the counts)
		struct Media
		{
			std::function<void(Rect)> art;	// the picture, decoded by the caller; none: the placeholder or the icon
			const Glyph* glyph = nullptr;	// the kind's icon (a video, an artist) when there is no picture
			std::string artKey;	// the Art placeholder's key (the album, else the title)
			std::string name, second, note, pill;
			Pill tone = Pill::Fact;
			bool focused = false, pressed = false, off = false, heart = false;
		};
		void mediaRow(const lay::MediaRow& l, const Media& m);
		// A Books cover: the picture (none: the Art placeholder keyed by the title), a count pill at its top
		// right, the title under it, and a 6-tall progress bar with its percent
		struct Cover
		{
			std::function<void(Rect)> picture;
			std::string key, title, badge;
			double progress = -1;	// -1: no bar
			bool focused = false, pressed = false, off = false;
		};
		void coverTile(const lay::CoverTile& l, const Cover& c);
		// A page's own line under its title, or a group's heading: `caps` sets the small capitals and a wider
		// tracking a heading has ("READING NOW · 3"); the note is right-aligned
		struct SectionLine
		{
			std::string text, note;
			bool caps = false;
		};
		void sectionLine(const lay::SectionLine& l, const SectionLine& s);

		// ==== Music, Videos, Books and Search (ui_kit_variants.cpp; docs/notes/2026-11-19-kit-pages.md):
		// the album tile and its grid, the artist row, the video and folder rows, the Books shelf's book cover,
		// and Search's page. Layouts: kitlayout.h's same block. ====

		// An album cell: the 96-square art (the caller's picture, else the Art placeholder keyed by the album), the
		// album's name, its artist, and a "now playing" mark (a yellow square with a play or pause mark) in the art's
		// top right. Focused: the 3-unit ACCENT outline 2 clear of the art, as a Cover cell has it.
		struct Album
		{
			std::function<void(Rect)> art;
			std::string artKey, title, artist;
			bool playing = false, paused = false, focused = false, off = false;
		};
		void albumTile(const lay::AlbumTile& l, const Album& a);

		// An artist's row: the caller's picture or a disc with the name's first letter, the name and its line. The
		// disc's fill is a tint of ACCENT over BUTTON, the letter in ACCENT (the same in every theme's roles).
		struct Artist
		{
			std::function<void(Rect)> art;
			std::string name, second;
			bool focused = false, pressed = false, off = false;
		};
		void artistRow(const lay::ArtistRow& l, const Artist& a);

		// A Videos row: a 64 x 36 thumbnail (the caller's frame, else the kind's mark on TRACK), the title and the
		// length (right), a size/date line under it, a tick when finished, a Playing / Paused pill on the row that
		// plays, and a 3-unit resume bar at the foot when resume is 0..1. A folder row is the 44 tile, the folder's
		// name and counts, and a chevron.
		struct Video
		{
			std::function<void(Rect)> art;
			const Glyph* glyph = nullptr;	// the kind's mark (G_FILM when none is given)
			std::string name, note, second;
			double resume = -1;	// -1: no bar (not started); 1: finished
			bool done = false, focused = false, pressed = false, off = false;
			bool playing = false, paused = false;	// the row that plays (the rail's key carries the bars)
		};
		void videoRow(const lay::VideoRow& l, const Video& v);
		struct Folder
		{
			std::string name, second, note;
			bool focused = false, pressed = false, off = false;
		};
		void folderRow(const lay::FolderRow& l, const Folder& f);

		// A book's drawn cover (a Cover cell with no picture): a sleeve in BUTTON, the spine in PRESSED with the
		// ACCENT mark, and the title's bars in TEXT and DIM, their widths keyed by the book's name so a shelf is not
		// one drawing repeated. The song's record-in-its-sleeve would read as an album on the Books page.
		void bookPlaceholder(Rect r, const std::string& key, int rad = 5);

		// ==== the library's pages (ui_kit_variants.cpp; docs/notes/2026-11-19-kit-more.md): an album's and an
		// artist's page head, the album's track row, the Playlists page's row, and Match album art's backdrop.
		// Layouts: kitlayout.h's same block. ====

		// An album's or an artist's page head (screens.js music-album, music-artist): the art at the left (the
		// caller's picture; none: the Art placeholder keyed by the album, or the artist's disc), the title, its
		// lines under it, and the page's keys: Play (primary, A), Shuffle (Y) and Add (touch only; Liked has no Add)
		struct PageHead
		{
			std::function<void(Rect)> art;
			std::string artKey;	// the Art placeholder's key (an album's or an artist's name)
			bool disc = false;	// an artist with no picture: the initial's disc (the ACCENT's tint over BUTTON)
			std::string title;
			std::vector<std::string> lines;	// an album: its artist, then its counts; an artist: its counts
			int focused = -1;	// the key the cursor is on (-1 none)
			int pressed = -1;	// the key under a finger
			bool hotkeys = true;	// the keys' caps ("" while Button hotkeys are off)
		};
		void pageHead(const lay::PageHead& l, const PageHead& h);

		// A track of an album's page: its number or the playing mark in its place, the title, a pill right after
		// the title when the track is a music video ("VIDEO"), a heart when it is liked, its length at the right
		struct Track
		{
			std::string number;	// its place on the album ("" when the playing mark is shown)
			bool playing = false, paused = false;	// the mark in the number's place
			std::string name, pill;
			Pill tone = Pill::Fact;	// the music video's VIDEO pill is a fact, not a state
			bool heart = false;
			std::string note;	// the length ("" until the length cache has it)
			bool focused = false, pressed = false, off = false;
		};
		void trackRow(const lay::TrackRow& l, const Track& t);

		// The Playlists page's row: the list's icon or Liked's heart, the name, a Now pill (a play mark and the
		// word) when it is the one playing, its counts at the right. New playlist is the dimmed plus row
		struct Playlist
		{
			const Glyph* glyph = nullptr;	// the row's mark (G_PLAYLIST, G_PLUS for New playlist; none: Liked's heart)
			bool liked = false;	// Liked: the filled heart, in ACCENT
			std::string name, note;
			bool playing = false;
			bool focused = false, pressed = false, off = false, lit = false;
		};
		void playlistRow(const lay::PlaylistRow& l, const Playlist& p);

		// Match album art's backdrop (components.md "Match album art"; the maths: artbackdrop.h). backdropPage
		// composes it once — the art's picture with the theme's five-stop veil baked in per row (artbg::composePage),
		// or the fallback's gradient — into one opaque 160 x 480 texture the caller keeps and destroys (null: none
		// could be made); backdrop then draws that texture over `r`, stretched with linear filtering: one copy a
		// frame, whatever else redraws (a marquee step, a slider frame). `fade` under 255 draws it at that alpha
		// (a cross-fade: the previous backdrop under it at full)
		// (`half`: the bottom panel's is Lower, the tall canvas's lower half; the gallery's Whole)
		SDL_Texture* backdropPage(const artbg::Art& art, const artbg::Backdrop& b, artbg::Half half = artbg::Half::Whole);
		void backdrop(Rect r, SDL_Texture* page, Uint8 fade = 255);
	};

	// ---- the More sheet's list (Ui::openMore; ui_more.cpp) ----
	struct Ui::MoreItem
	{
		const Glyph* glyph = nullptr;
		std::string label, note;
		std::string key;	// a row's cap ("L3": that button chooses it while the sheet is open)
		std::function<bool(Player&)> act;	// what choosing it does; true: leave the player (as Ui::press says)
		bool danger = false, off = false, lit = false;
		bool stay = false;	// the sheet stays open (the row says what it is now: Like; a chip changed in place)
		std::string value;	// a chip's state ("All", "Fit")
		bool away = false;	// a chip away from its normal state (yellow, with a dot)
	};
	struct Ui::MoreSpec
	{
		const Glyph* glyph = nullptr;
		std::string title, note;
		std::vector<MoreItem> chips, tiles, rows;	// a first row of state chips, a row of keys (four across), then the list
		int cols = 1;	// the list's columns
	};

	// ======== kit-new: Section rail, Book bar, Away-from-normal, Screen-off warning (ui_kit_new.cpp) ========
	// Written beside Ui::Kit as a struct of its own (another branch adds members to Kit at the same time); each draws on the
	// Rects of kitlayout.h's kit-new block, which the touches test too. The Preview pane is the top panel's
	// (previewpane.h). Not used by a screen yet: --press gallery:new draws them (docs/workflow.md).
	struct Ui::KitNew
	{
		// ---- Section rail: RAIL ground, a rule on its right, Search at the top, Now playing above Settings ----
		struct RailItem
		{
			const Glyph* glyph = nullptr;
			std::string label;
			bool warn = false;	// a red mark: opening it would stop what plays
		};
		// active: the section lit (sections.size(): Settings; -1 none); pressed: the cell under a finger (sections.size():
		// Settings; -2: Now playing); searchLit: Search shows (its key filled); searchPressed: the key under a finger.
		// npPicture: the Now playing key's art box, drawn by the caller (a song's real art or its placeholder, a
		// video's kind mark — Ui::drawNowArt); empty when nothing is loaded (the key's slot is drawn empty then);
		// npPlaying / npPaused: its bars, moving or a pause mark. Its place is always kept, so the cells never change
		// height when playback starts.
		static constexpr int NP_PRESSED = -2;
		static void rail(Kit& k, const lay::Rail& r, const std::vector<RailItem>& sections, const RailItem& settings, int active,
			int pressed = -1, bool searchLit = false, bool searchPressed = false, bool npPlaying = false, bool npPaused = false,
			const std::function<void(Rect)>& npPicture = {});

		// ---- Book bar: Book mode's bottom sheet, drawn upright as the handheld is held ----
		struct BookKey
		{
			const Glyph* glyph = nullptr;
			std::string label, key;
			std::string lit;	// a lit key's state ("Warm · -30%"): ACCENT, the state in place of the cap, the dot
		};
		struct BookSpec
		{
			std::string title, value, note;	// the slider row: "7%" on its left, "Chapter 1 of 12" on its right
			double share = 0;
			BookKey keys[4];
			std::string leave, leaveKey;	// "Back to scrolling", "B"
			std::vector<Hint> hints;	// directions as seen while held
			bool topLeft = true;	// the top screen on the left as held (lay::sideways)
			int pressed = -1;	// lay::bookBarAt's piece under a finger
			bool veil = true;	// the page dimmed under it (the test gallery leaves its other pieces clear)
		};
		// the veil over the page and the sheet, turned a quarter onto the panel; false: the renderer has no target
		// textures, and the sheet was drawn unturned at the panel's foot
		static bool bookBar(Kit& k, const lay::BookBar& b, const BookSpec& s);

		// ---- Away-from-normal: the tone's ink and the dot in a control's corner ----
		static SDL_Color awayInk(lay::AwayTone t);	// TEXT, ACCENT or DANGER
		static void awayDot(Kit& k, Rect control, lay::AwayTone t);	// nothing when Normal

		// ---- Screen-off warning: the veil and the notice row (the panel's, or `over`) ----
		static void screenOffWarn(Kit& k, int msLeft, Rect over = {0, 0, W, H});
	};
}
