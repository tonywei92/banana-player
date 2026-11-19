#pragma once
// The player's look in one place: its colours by role and its corners (docs/redesign/components.md, Themes). The bottom
// panel (ui_*.cpp through ui_internal.h's SDL copies), the top panel's cards (levelcard.cpp, player.cpp's ASS) and the
// web dashboard (/theme.css, written from this table by themeCss) follow it.
//
// A theme is only values: the table in theme.cpp, one line of base roles per theme (docs/redesign/themes.py has the
// same numbers and checks their contrast). The names below read the current theme, so a swap (Settings › Power &
// screens › Theme) is setTheme() and a redraw: nothing is drawn with a colour of its own. Yellow, the first, is the
// default and today's look, value for value.
#include <cstdio>
#include <string>

namespace bp
{
	namespace theme
	{
		struct Rgb
		{
			unsigned char r, g, b;
			bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
			bool operator!=(const Rgb& o) const { return !(*this == o); }
		};

		// one theme's values (docs/redesign/components.md, "The tokens, and each theme's values")
		struct Palette
		{
			const char* id;	// settings.conf's word (`theme`), the page's data-theme
			const char* name;	// the Settings row's
			Rgb BG, SURFACE, BUTTON, PRESSED, TRACK, RULE;
			Rgb RAIL;	// the section rail's ground
			Rgb TEXT, DIM, DIM_ON, ACCENT, ON_ACCENT, DANGER;
			int EDGE_MIX;	// how far a box's edge goes from its fill towards TEXT, in percent
			// what is drawn over a picture or in its place (levelcard.cpp): dark in every theme
			Rgb TOP_CARD, TOP_EDGE, TOP_TITLE, TOP_TEXT, TOP_SUB, TOP_TRACK, TOP_SQUARE, TOP_DANGER;
			int page;	// the reader's page for text-theme "Match theme": TXT_THEME_DARK, _LIGHT or _BLACK (textreader.h)
			Rgb OK;	// the dashboard's green (something is on, or worked); the handheld has none
			// a theme whose keys are dark with light lettering (Gundam's navy keys): Kit::key's fill and ink, its edge when
			// focused, and the hint bar's ground and ink. Off (strongKeys false): BUTTON, TEXT, ACCENT, BG and DIM_ON, as before
			bool strongKeys;
			Rgb KEY, ON_KEY, KEY_FOCUS, HINT_BAR, ON_HINT;
			Rgb PRIMARY, ON_PRIMARY;	// a primary key's fill and ink (Play, Pause, a dialog's first key): ACCENT, ON_ACCENT unless set
			Rgb ON_RAIL;	// the rail's icons and labels on a dark RAIL in a light theme (Gundam): DIM_ON unless set
		};

		// the themes, in theme.cpp's order (LEFT / RIGHT steps through them): Yellow first. Adding one is a line there
		int themeCount();
		const Palette& palette(int i);	// (clamped: an unknown index is Yellow)
		const char* themeId(int i);
		const char* const* themeIds();	// every theme's id, then its name: the setting's words and labels
		const char* const* themeNames();
		// the theme drawn now, and a swap to another (false when it already was)
		extern Palette current;
		int currentTheme();
		bool setTheme(int i);
		// text-theme's own value for "Match theme" under theme i (Light: the Light page, High contrast: Black, the rest Dark)
		int matchedPage(int i);
		// every theme but Yellow as `:root[data-theme=ID]{...}` blocks of web/style.css's variables (the dashboard's
		// /theme.css): the same table and the same edge and lip rules, never copied by hand
		std::string themeCss();

		// the current theme's values, by the names everything draws with (read when drawn: a swap reaches them all)
		inline const Rgb& BG = current.BG;
		inline const Rgb& SURFACE = current.SURFACE;	// tiles, Recent cards, dialogs' bodies
		inline const Rgb& BUTTON = current.BUTTON;	// buttons, unselected tabs, the focused row's fill
		inline const Rgb& PRESSED = current.PRESSED;	// a button while held
		inline const Rgb& ACCENT = current.ACCENT;	// key hints, Playing, the bar's fill, the selected tab, the cursor
		inline const Rgb& ON_ACCENT = current.ON_ACCENT;	// ink on an accent (or danger) fill
		inline const Rgb& TEXT = current.TEXT;
		inline const Rgb& DIM = current.DIM;	// secondary text on BG / SURFACE, disabled ink
		inline const Rgb& DIM_ON = current.DIM_ON;	// secondary text on a button or the focused row
		inline const Rgb& DANGER = current.DANGER;
		inline const Rgb& SCREEN = current.BUTTON;	// the subtitle preview's box, a photo's pan area (= BUTTON)
		inline const Rgb& TRACK = current.TRACK;	// a bar's empty part, keyboard special keys, dimmed chips
		inline const Rgb& KNOB = current.TEXT;	// (= TEXT)
		inline const Rgb& RULE = current.RULE;	// thin lines between rows
		inline const Rgb& RAIL = current.RAIL;
		inline const Rgb& ON_RAIL = current.ON_RAIL;	// the rail's ink (DIM_ON unless a theme gives it)
		inline const Rgb& KEY_FOCUS = current.KEY_FOCUS;	// a focused strong key, the rail's active cell in a strong theme
		inline const int& EDGE_MIX = current.EDGE_MIX;

		// the top panel's cards (src/levelcard.cpp: level, chapter/Equalizer, note, state card; mpv image overlays) and the
		// idle logo: dark in every theme (a light card over a film glares); a light-bodied theme takes a dark one's
		inline const Rgb& TOP_TITLE = current.TOP_TITLE;	// the idle title, a note's line, a state card's title, a boosted level
		inline const Rgb& TOP_TEXT = current.TOP_TEXT;	// a card's level, a state card's sentence
		inline const Rgb& TOP_SUB = current.TOP_SUB;
		inline const Rgb& TOP_CARD = current.TOP_CARD;	// every card's body
		inline const Rgb& TOP_EDGE = current.TOP_EDGE;
		inline const Rgb& TOP_TRACK = current.TOP_TRACK;	// a card's bar; the ground of a page not yet decoded
		inline const Rgb& TOP_SQUARE = current.TOP_SQUARE;	// the logo's dark square, a state card's tile
		inline const Rgb& TOP_DANGER = current.TOP_DANGER;

		// corners, in the bottom panel's 640x480 units (the top panel's ASS cards in its 960x720): the same in every theme
		constexpr int R_BUTTON = 10;	// buttons, tabs, the focused row
		constexpr int R_TILE = 14;	// Home's cards and tiles, Settings' group tiles
		constexpr int R_DIALOG = 18;	// dialogs, pop-ups
		constexpr int R_CHIP = 6;
		constexpr int R_FIELD = 4;	// text fields, keyboard keys
		constexpr int R_BAR = 6;	// the progress bar's track
		constexpr int R_KNOB = 2;
		constexpr int R_TOP_CARD = 18;
		constexpr int R_TOP_BAR = 4;
		// the redesign kit's (docs/redesign/components.md, src/ui_kit.h)
		constexpr int R_CAP = 5;	// a key cap (the button's name in its outline), a pill
		constexpr int R_TRACK = 7;	// the 14-tall slider and progress track
		constexpr int R_GRIP = 3;	// its knob
		constexpr int R_SHEET = 18;	// a bottom sheet's top corners
		constexpr int R_LOCK = 30;	// the 120-square key of the lock
		constexpr int EDGE = 2;	// a box's edge, inside its outline
		constexpr int LIP = 3;	// the darker band above its bottom edge

		// The Clean style is the look (components.md "Clean style"): every theme but High contrast, which is Classic.
		// The kit draws a fill and the focus ring, no edge and no lip, with one radius for the page's pressed pieces and
		// one for small tints. `clean` follows the theme (setTheme); Kit::ClassicHere raises classicDepth while a page
		// draws pieces that stay Classic whatever the style (the photo viewer's keys, all keys by rule 7).
		extern bool clean;
		extern int classicDepth;
		inline bool drawingClean() { return clean && classicDepth == 0; }
		// one radius in Clean: 12 for the page's pressed pieces (cards, tiles, chips, tabs, rows, rail cells), 6 for
		// caps and pills. A key keeps R_BUTTON: a key is a key (rule 7)
		constexpr int cleanRadius(int rad)
		{
			return rad == R_BUTTON || rad == R_TILE ? 12 : rad == R_CAP ? 6 : rad;
		}
		inline int kitRadius(int rad) { return drawingClean() ? cleanRadius(rad) : rad; }

		// a box's edge: its fill EDGE_MIX percent towards the text (a light theme's edges come out darker than the fill,
		// a dark theme's lighter: one rule); its lip: the fill 30% darker (both rounded, as the mock's)
		constexpr unsigned char towards(int a, int b, int percent)
		{
			return (unsigned char)(a + ((b - a) * percent + ((b - a) >= 0 ? 50 : -50)) / 100);
		}
		constexpr Rgb edgeOf(Rgb c, Rgb text, int percent)
		{
			return {towards(c.r, text.r, percent), towards(c.g, text.g, percent), towards(c.b, text.b, percent)};
		}
		inline Rgb edgeOf(Rgb c)
		{
			return edgeOf(c, current.TEXT, current.EDGE_MIX);
		}
		constexpr Rgb lipOf(Rgb c)
		{
			return {(unsigned char)((c.r * 7 + 5) / 10), (unsigned char)((c.g * 7 + 5) / 10), (unsigned char)((c.b * 7 + 5) / 10)};
		}

		// the kit's strengths, in percent: the veil (BG) under a sheet and under the lock, what is left of a control
		// that can't be used, the darker part of a key filling as it is held, the red behind a red pill. The same in
		// every theme
		constexpr int VEIL_SHEET = 68, VEIL_LOCK = 80, OFF = 45, HOLD_FILL = 28, TINT = 20;
		// c at `percent` strength over `under` (an unavailable control over the ground; a tinted pill's ground)
		constexpr Rgb mix(Rgb under, Rgb c, int percent)
		{
			return {(unsigned char)(under.r + (c.r - under.r) * percent / 100), (unsigned char)(under.g + (c.g - under.g) * percent / 100),
				(unsigned char)(under.b + (c.b - under.b) * percent / 100)};
		}
		// the accent's tint over a ground: what is selected in Clean (a chip, a tab, the rail's cell)
		inline Rgb tintOf(Rgb ground) { return mix(ground, current.ACCENT, 17); }

		// "&HBBGGRR&", as ASS writes a colour
		inline std::string ass(Rgb c)
		{
			char s[12];
			snprintf(s, sizeof s, "&H%02X%02X%02X&", c.b, c.g, c.r);
			return s;
		}
	}
}
