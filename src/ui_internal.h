// Names shared by the files that make up Ui (ui.cpp, ui_*.cpp): the palette, the browser's pseudo-directories and row
// ids, short labels. Not for use outside Ui's files.
#pragma once
#include "ui.h"
#include "ui_glyphs.h"
#include "durations.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include "theme.h"
#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>
#include <sys/stat.h>

namespace bp
{
	inline constexpr SDL_Color sdl(theme::Rgb c, Uint8 a = 255) { return {c.r, c.g, c.b, a}; }
	// the current theme's roles as SDL colours (theme.h's, copied by syncInk() after a swap: the names read them when drawn)
	struct Ink
	{
		SDL_Color BG, TEXT, ACCENT, BUTTON, DIM, DANGER, SCREEN, PRESSED, SURFACE, DIM_ON, ON_ACCENT, TRACK, KNOB, RULE, RAIL;
	};
	extern Ink ink;	// (ui_draw.cpp)
	void syncInk();
	inline const SDL_Color &BG = ink.BG, &TEXT = ink.TEXT, &ACCENT = ink.ACCENT, &BUTTON = ink.BUTTON, &DIM = ink.DIM, &DANGER = ink.DANGER,
		&SCREEN = ink.SCREEN, &PRESSED = ink.PRESSED, &SURFACE = ink.SURFACE, &DIM_ON = ink.DIM_ON, &ON_ACCENT = ink.ON_ACCENT, &TRACK = ink.TRACK,
		&KNOB = ink.KNOB, &RULE = ink.RULE, &RAIL = ink.RAIL;
	using theme::R_BUTTON;
	using theme::R_TILE;
	using theme::R_DIALOG;
	using theme::R_CHIP;
	using theme::R_FIELD;
	using theme::R_BAR;
	using theme::R_KNOB;
	inline const int SIZES[3] = {18, 24, 32};
	// The panel's type, by the size index every text call takes: small, normal and large are every page's (Pixelify
	// Sans at SIZES); the rest are the redesign kit's (src/ui_kit.h, docs/redesign/components.md): Pixelify Sans at
	// the mock-ups' sizes and Inter for what is read (notes, hints, small capitals). In 640x480 units, opened at the
	// panel's own pixels; the kit's are opened when first drawn (Ui::font)
	enum { F_SMALL, F_NORMAL, F_LARGE, F_P12, F_P13, F_P14, F_P16, F_P20, F_P28, F_I11B, F_I12, F_I14, F_COUNT };
	struct FontDef { const char* file; double units; bool pixel; };
	inline const FontDef FONT_DEFS[F_COUNT] = {
		{"PixelifySans-Regular.ttf", 18, true}, {"PixelifySans-Regular.ttf", 24, true}, {"PixelifySans-Regular.ttf", 32, true},
		{"PixelifySans-Regular.ttf", 12, true},	// a key cap's letters
		{"PixelifySans-Regular.ttf", 13, true},	// a chip's value
		{"PixelifySans-Regular.ttf", 14, true},	// a transport key's and a tile's label
		{"PixelifySans-Regular.ttf", 16, true},	// a 56-tall key's label, the player's state, a line key's text
		{"PixelifySans-Regular.ttf", 20, true},	// a sheet's title
		{"PixelifySans-Regular.ttf", 28, true},	// the player's title
		{"Inter-Bold.ttf", 11, false},	// small capitals (a chip's label, "NEXT"), a pill
		{"Inter-Regular.ttf", 12.5, false},	// notes, hints, the header's crumb
		{"Inter-Regular.ttf", 14, false},	// a sentence
	};
	static_assert(F_COUNT == Ui::FONT_COUNT, "Ui::FONT_COUNT is the font table's length");
	inline const Uint32 QUIT_WINDOW_MS = 2000, SCRUB_MS = 100;
	inline const int DRAG_SLOP = 8;	// the progress bar: a finger that moves this far is a drag, not a tap
	inline const double BACK_S = 5, FORWARD_S = 10;

	inline float sx = 1, sy = 1;	// logical 640x480 -> the panel's pixels

	// A decoded picture's corners in its own alpha, rounded over R pixels (4x4 samples a pixel, as Ui::corner does):
	// drawn over a rounded fill, the fill shows through the corners, so a cover looks cut to the box's shape without a
	// mask at draw time.
	inline void roundCorners(std::vector<uint8_t>& rgba, int w, int h, int R)
	{
		R = std::min({R, w / 2, h / 2});
		for (int cy = 0; cy < 2; cy++)
			for (int cx = 0; cx < 2; cx++)
				for (int y = 0; y < R; y++)
					for (int x = 0; x < R; x++)
					{
						int in = 0;
						for (int j = 0; j < 4; j++)
							for (int i = 0; i < 4; i++)
							{
								float dx = R - (x + (i + 0.5f) / 4), dy = R - (y + (j + 0.5f) / 4);
								in += dx * dx + dy * dy <= (float)R * R;
							}
						int px = cx ? w - 1 - x : x, py = cy ? h - 1 - y : y;
						uint8_t* p = rgba.data() + ((size_t)py * (size_t)w + (size_t)px) * 4;
						p[3] = (uint8_t)(p[3] * in / 16);
					}
	}

	// a Settings row made from the settings table (settings.h's SettingDef): SET_ROW0 + its index in settingDefs()
	inline constexpr int SET_ROW0 = 1000;
	inline int settingRow(const char* key) { return SET_ROW0 + settingIndex(key); }
	inline const SettingDef* rowDef(int row)
	{
		return row >= SET_ROW0 && row - SET_ROW0 < (int)settingDefs().size() ? &settingDefs()[row - SET_ROW0] : nullptr;
	}
	// a page's rows in order: the table's settings of the group, and the page's own rows (their ids and places: `own`,
	// each {place, row id}) among them, by place
	inline std::vector<int> pageRows(int group, std::vector<std::pair<int, int>> own = {})
	{
		std::vector<std::pair<int, int>> all = own;
		for (int i : settingsOfGroup(group)) all.push_back({settingDefs()[i].place, SET_ROW0 + i});
		std::stable_sort(all.begin(), all.end(), [](auto& a, auto& b) { return a.first < b.first; });
		std::vector<int> out;
		for (auto& r : all) out.push_back(r.second);
		return out;
	}
	// the Settings page's groups in the spec's order (Ui::SetGroup)
	inline const char* const GROUP_NAMES[] = {
		"Playback",
		"Subtitles",
		"Music & sound",
		"Photos",
		"Reading",
		"Files & history",
		"Power & screens",
		"Web dashboard",
		"Updates",
		"About",
	};
	inline const char* RECENT = "\x01recent";
	inline const char* HOMEDIR = "\x01home";	// Home: the Recent strip and the sections' tiles
	inline const char* SECTION_NAMES[] = {"Files", "Playlists", "Network"};
	inline const Ui::Glyph* const SECTION_GLYPHS[] = {&G_CARD, &G_PLAYLIST, &G_NETWORK};
	inline const char* NETROOT = "\x01net";	// the Network tab's first page: the saved connections
	inline const char* FORMDIR = "\x01form";	// a connection's form (Add connection, Edit)
	inline const char* FINDNET = "\x01find";	// the servers found on the network
	inline const char* PLROOT = "\x01pls";	// the Playlists tab's first page: the saved playlists
	inline const char* PLDIR = "\x01pl:";	// a playlist's items: PLDIR + the playlist's file
	inline bool isPlDir(const std::string& d) { return d.rfind(PLDIR, 0) == 0; }
	inline std::string plDirFor(const std::string& file) { return PLDIR + file; }
	inline std::string plFileOf(const std::string& d) { return isPlDir(d) ? d.substr(strlen(PLDIR)) : ""; }
	// a page of the browser's own, not a folder on a card (never listed or stat'ed). Compared whole: FORMDIR and
	// FINDNET don't start with \x01 (the escape takes their "f" as a hex digit)
	inline bool pseudoDir(const std::string& d)
	{
		return d == RECENT || d == HOMEDIR || d == NETROOT || d == FORMDIR || d == FINDNET || d == PLROOT || isPlDir(d);
	}
	// a section's first page (Ui::BrowseTab: Files, Playlists, Network)
	inline std::string sectionTop(int tab)
	{
		return tab == 1 ? PLROOT : tab == 2 ? NETROOT : "";
	}
	// the browser's rows' ids: 1 a folder, 2 a file, 3 Clear play history, 4 a connection, 6 Try again, -1 a message;
	// a playlist (Playlists' first page), a playlist's item
	enum { PL_ROW = 7, PL_ITEM = 8, PL_NEW = 9 };	// (PL_NEW: Playlists' last row, New playlist)
	// the form's rows
	enum { FORM_TYPE = 20, FORM_NAME, FORM_HOST, FORM_PORT, FORM_SHARE, FORM_FOLDER, FORM_SIGNIN, FORM_USER, FORM_PASS, FORM_FTPS, FORM_TEST, FORM_SAVE };
	// the found servers' rows
	enum { R_FOUND = 50, R_SEARCH, R_TYPE_ADDRESS };
	// a note drawn dim (a placeholder: what an empty field comes out as)
	inline const char DIM_NOTE = '\x02';
	// a file's note: where it was left and its size ("at 0:42 · 13.9 MB")
	inline std::string fileNote(const std::string& at, long long size) { return fileNote(at, -1, size); }
	inline long long fileSize(const std::string& path)
	{
		struct stat st;
		return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? (long long)st.st_size : -1;
	}
	inline const char* LOOP_SHORT[LOOPS] = {"Off", "All", "One"};		// Loop as a chip's value
	// (the retired and dropped styles' ids: their replacements' glyphs, Peaks' for the dropped; nothing shows them)
	inline const Ui::Glyph* const VIS_GLYPHS[VISUALIZERS] = {&G_VIS_BARS, &G_VIS_WAVES, &G_VIS_SPECTRUM, &G_VIS_MIRROR, &G_VIS_PEAKS,
		&G_VIS_PEAKS_BANANA, &G_VIS_FADERS, &G_VIS_LED, &G_VIS_SYNTHWAVE, &G_VIS_BANANA, &G_VIS_CITY, &G_VIS_SCOPE, &G_VIS_HEART,
		&G_VIS_PEAKS_BANANA, &G_VIS_AURORA, &G_VIS_HORIZON, &G_VIS_VINYL, &G_VIS_SEA, &G_VIS_PEAKS_BANANA, &G_VIS_DOTS, &G_VIS_PEAKS_BANANA,
		&G_VIS_PEAKS_BANANA, &G_VIS_PEAKS_BANANA, &G_VIS_DISCO, &G_VIS_DANCER, &G_VIS_PEAKS_BANANA, &G_VIS_PEAKS_BANANA, &G_VIS_WOBBLE,
		&G_VIS_TUBEMAN, &G_VIS_STEPS, &G_VIS_WASHER, &G_VIS_MIRROR, &G_VIS_PEAKS_BANANA, &G_VIS_DOTS, &G_VIS_PEAKS_BANANA, &G_VIS_HORIZON,
		&G_VIS_STEPS, &G_VIS_HORIZON, &G_VIS_STEPS, &G_VIS_BARCODE, &G_VIS_RINGS, &G_VIS_RIBBON, &G_VIS_GRID, &G_VIS_STARFIELD,
		&G_VIS_STRINGS, &G_VIS_HELIX, &G_VIS_SPARKS, &G_VIS_BOUNCY, &G_VIS_ROCKET, &G_VIS_LANTERNS, &G_VIS_MOON, &G_VIS_DANCE};
	// a show mode's mark: the visualizer ones take the style's
	inline const Ui::Glyph* showGlyph(int show, int vis)
	{
		return show == SHOW_ART ? &G_PICTURE : show == SHOW_NONE ? &G_TITLE_ONLY : VIS_GLYPHS[vis >= 0 && vis < VISUALIZERS ? vis : 0];
	}
	// the player's Display chip: a show mode's short form, on the chip only (the full names are musicShowLabel's; the
	// chip is 115 wide and "Album art only" does not fit one line)
	inline const char* SHOW_SHORT[] = {"Art", "Art + viz", "Visualizer", "None"};
	// the effects' short names (the song's line, Settings' value), and what each is (the Effects tab)
	inline const char* FX_SHORT[SOUNDFX_COUNT] = {"None", "Room", "Hall", "Church", "Cave", "Wide", "Phones"};
	inline const char* FX_ABOUT[SOUNDFX_COUNT] = {"Just the music", "A small, lively room", "A big hall, a long warm tail",
		"Stone walls, a very long tail", "Rock walls and echoes", "Wider than the speakers", "Less tiring in headphones"};
	inline const char* CUSTOM_NAME = "Custom";	// an edited curve, not saved (eqPreset "")
	// the keyboard's field for a preset's name (apart from the form's and the found servers' ids)
	inline const int EQ_NAME = 60, PL_NAME = 61, FILE_NAME = 69, FIND_FIELD = 70;	// (62-68: the manga reader's)
	inline const char* const LQ = "\xE2\x80\x9C", *const RQ = "\xE2\x80\x9D";	// “ ”
	inline std::string quoted(const std::string& s) { return LQ + s + RQ; }
	// "Rock", "Custom", "Rock · Hall", "EQ off · Hall", "Off": the sound as one line (Settings' value)
	inline std::string soundSummary(const Settings& s)
	{
		std::string preset = s.eqPreset.empty() ? CUSTOM_NAME : s.eqPreset;
		bool fx = s.soundFx != SoundFx::None;
		if (!s.eqOn) return fx ? std::string("EQ off \xC2\xB7 ") + FX_SHORT[(int)s.soundFx] : "Off";
		return fx ? preset + " \xC2\xB7 " + FX_SHORT[(int)s.soundFx] : preset;
	}

	inline std::string baseName(const std::string& p)
	{
		size_t slash = p.rfind('/');
		return slash == std::string::npos ? p : p.substr(slash + 1);
	}

	inline bool exists(const std::string& p)
	{
		struct stat st;
		return stat(p.c_str(), &st) == 0;
	}

	inline std::string trimmed(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
		return a == std::string::npos ? "" : s.substr(a, b - a + 1);
	}

	inline int rowsIndexOf(const std::vector<int>& rows, int row)
	{
		for (size_t i = 0; i < rows.size(); i++)
			if (rows[i] == row) return (int)i;
		return -1;
	}
}
