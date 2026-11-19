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
#include <functional>

namespace bp
{
	struct Ui::Kit
	{
		Ui& ui;
		explicit Kit(Ui& u) : ui(u) {}

		int textW(const std::string& s, int font) const;	// in 640x480 units
		static SDL_Color off(SDL_Color c, bool isOff = true);	// c as an unavailable piece shows it
		static SDL_Color edgeOf(SDL_Color fill);
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
		// the same bar in a box of its own: a screen inside the shell passes lay::shell(mini).hints, so the rail's
		// strip is not painted over
		void hintsIn(Rect area, const std::vector<Hint>& list);

		// ---- the header (every redesigned page has one) ----
		// The strip a player layout gives the back key, the More key and the crumb, then the title under it (and the
		// state on its right). `right` draws the badge and status cluster from `right` (its x) leftwards and returns
		// the left edge it used, so the crumb can end before it. `inlineTitle` puts the title beside the back key with
		// the crumb under it, as the redesign's own header does (.hd .tt): the Settings pages and About, which have no
		// state and so no title row of their own. See docs/redesign/components.md "Header".
		struct Header
		{
			std::string title, crumb;	// title in F_P28 (the title's own line); crumb in F_I12 (the strip)
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
		void iconKey(Rect r, const Glyph& g, bool pressed, int cell = 2);

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
		// the veil, the box and the grab mark: what a sheet draws before its own content (the header too, if it has
		// one; the List sheet has tabs and controls instead)
		void sheetBack(Rect box, Rect grab);

		// ---- tabs and a segmented choice ----
		// each tab's box: as wide as its label in F_P14 plus 26, 44 tall, 6 apart, the caps' room at both ends
		// (caps false: no key caps are drawn, so the strip starts at the area's left edge)
		std::vector<Rect> tabRects(Rect area, const std::vector<std::string>& labels, bool caps = true) const;
		// keys: the caps at its ends ("L/R" in a sheet); on: the open tab (-1 none)
		void tabs(Rect area, const std::vector<std::string>& labels, int on, const std::string& keys = "L/R");
		// options shared across `area` 6 apart, 40 tall (the caller's area): the chosen one yellow with black ink
		void segment(Rect area, const std::vector<std::string>& options, int on);
		enum class Pill { None, Live, Fact, Bad };	// yellow: live or wants attention; neutral: a fact; red: failed
		enum class Trail { None, Chevron, SwitchOff, SwitchOn };
		struct Row
		{
			const Glyph* glyph = nullptr;
			std::string lead;	// a number in the icon's place (a list's place), when there is no icon
			bool radio = false;	// a choice's mark in the icon's place (View's tracks, sizes and speeds)
			bool radioOn = false;	// the one in use: G_RADIO_ON in yellow
			std::string name, note, key, pill;
			Pill tone = Pill::Live;
			Trail trail = Trail::None;
			bool focused = false, pressed = false, off = false, danger = false, lit = false;
			int font = F_SMALL;	// the name's (a sheet's two-column rows: F_P16)
			SDL_Color under = SURFACE;	// what the row lies on: a sheet (a page: BG)
		};
		void row(Rect r, const Row& row);
		int pill(int right, int cy, const std::string& text, Pill tone = Pill::Live);	// its width
		void toggle(int x, int cy, bool on);	// 40 x 22, its left edge at x

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
		// the chips in `area` (56 tall: lay::filterChips); on: the chosen one; focused: the D-pad's
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
			bool danger = false;	// the action's ink red: it destroys
			int focus = 0;	// 0 the safe key (yellow, focused, B), 1 the action
			int pressed = -1;
			bool footOn = false;	// the "don't ask again" line ticked
		};
		void confirmSheet(const lay::ConfirmSheet& l, const ConfirmSpec& c, const std::vector<std::string>& lines);
		struct BusySpec
		{
			std::string title, file, left, right;	// left: "3 of 12 \xC2\xB7 25%", right: the time left (Inter 13)
			double share = -1;	// -1: the amount isn't known (no bar)
			const Glyph* glyph = nullptr;	// the job's icon; none: the spinner
			int tick = 0;	// the spinner's step
			std::string key = "Cancel", cap = "B";
			bool pressed = false;
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
			std::vector<std::string> lines;	// one or two (Inter 14 dim, at most 400 wide)
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
		void selectBar(const lay::Box keys[4], const BarKey k[4]);
		struct Clip
		{
			std::string what, size;	// "3 files held" and how big
			BarKey put, cancel;	// put: "... here" (SELECT), cancel: (X)
		};
		void clipBar(const lay::ClipBar& l, const Clip& c);
	};

	// ---- the More sheet's list (Ui::openMore; ui_more.cpp) ----
	struct Ui::MoreItem
	{
		const Glyph* glyph = nullptr;
		std::string label, note;
		std::string key;	// a row's cap ("L3": that button chooses it while the sheet is open)
		std::function<bool(Player&)> act;	// what choosing it does; true: leave the player (as Ui::press says)
		bool danger = false, off = false, lit = false;
		bool stay = false;	// the sheet stays open (the row says what it is now: Like)
	};
	struct Ui::MoreSpec
	{
		const Glyph* glyph = nullptr;
		std::string title, note;
		std::vector<MoreItem> tiles, rows;	// a first row of keys (four across), then the list
		int cols = 1;	// the list's columns
	};

	// ======== kit-new: Section rail, Book bar, Away-from-normal, Screen-off warning (ui_kit_new.cpp) ========
	// Written beside Ui::Kit as a struct of its own (another branch adds members to Kit at the same time); each draws on the
	// Rects of kitlayout.h's kit-new block, which the touches test too. The Preview pane is the top panel's
	// (previewpane.h). Not used by a screen yet: --press gallery:new draws them (docs/workflow.md).
	struct Ui::KitNew
	{
		// ---- Section rail: RAIL ground, a rule on its right, the sections, the L / R caps, Settings at the foot ----
		struct RailItem
		{
			const Glyph* glyph = nullptr;
			std::string label;
			bool warn = false;	// a red mark: opening it would stop what plays
		};
		// active: the section lit (sections.size(): Settings; -1 none); pressed: the cell under a finger
		static void rail(Kit& k, const lay::Rail& r, const std::vector<RailItem>& sections, const RailItem& settings, int active,
			int pressed = -1);

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
