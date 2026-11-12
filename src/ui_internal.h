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
#include <cstring>
#include <sys/stat.h>

namespace bp
{
	inline constexpr SDL_Color sdl(theme::Rgb c, Uint8 a = 255) { return {c.r, c.g, c.b, a}; }
	inline constexpr SDL_Color BG = sdl(theme::BG), TEXT = sdl(theme::TEXT), ACCENT = sdl(theme::ACCENT),
		BUTTON = sdl(theme::BUTTON), DIM = sdl(theme::DIM), DANGER = sdl(theme::DANGER), SCREEN = sdl(theme::SCREEN),
		PRESSED = sdl(theme::PRESSED), SURFACE = sdl(theme::SURFACE), DIM_ON = sdl(theme::DIM_ON),
		ON_ACCENT = sdl(theme::ON_ACCENT), TRACK = sdl(theme::TRACK), KNOB = sdl(theme::KNOB), RULE = sdl(theme::RULE);
	using theme::R_BUTTON;
	using theme::R_TILE;
	using theme::R_DIALOG;
	using theme::R_CHIP;
	using theme::R_FIELD;
	using theme::R_BAR;
	using theme::R_KNOB;
	inline const int SIZES[3] = {18, 24, 32};
	inline const Uint32 QUIT_WINDOW_MS = 2000, SCRUB_MS = 100;
	inline const double BACK_S = 5, FORWARD_S = 10;

	constexpr Ui::Rect Ui::BAR;

	inline float sx = 1, sy = 1;	// logical 640x480 -> the panel's pixels


	// the two rows of buttons: what each does is the button it stands for
	// LEFT/RIGHT (-5 s, +10 s) have no button of their own: their keys, and the bar, seek. Their places hold Like (L3
	// on the RG DS Plus) and Add to playlist (no key: START › Playing has it), around Play, with Prev and Next under
	// the shoulders they answer to
	struct GridKey { Button button; const char* hint; };
	inline const GridKey GRID[2][5] = {
		{{Button::L, "L"}, {Button::L3, "L3"}, {Button::A, "A"}, {Button::None, nullptr}, {Button::R, "R"}},
		{{Button::Y, "Y"}, {Button::X, "X"}, {Button::Select, "SELECT"}, {Button::Up, "UP/DOWN"}, {Button::Start, "START"}},
	};

	// the settings' on/off rows (TOGGLE_ROW + their index). The manga reader's are its own page's, which no group of
	// Settings lists, and the fullscreen tip's is a switch too though nothing shows it: it has a label only because the
	// two arrays go together
	inline const char* REMEMBER_LABELS[20] = {"Remember position", "Remember audio track", "Remember subtitles",
		"Remember last folder", "Keep play history", "Button hotkeys", "Show hidden folders", "Battery percentage",
		"Keep zoom between photos", "Check automatically", "Same for both screens", "Network files in Recent",
		"Manga reader", "Swap Left/Right", "Adult sources", "Reading history", "In Recent on Home", "Fullscreen tip seen",
		"Open fullscreen", "Secure DNS"};
	inline bool Settings::*const REMEMBER[20] = {&Settings::rememberPosition, &Settings::rememberAudio, &Settings::rememberSubs,
		&Settings::rememberFolder, &Settings::keepHistory, &Settings::buttonHotkeys, &Settings::showHiddenFolders,
		&Settings::batteryPercent, &Settings::keepPhotoZoom, &Settings::checkUpdates, &Settings::sameBrightness,
		&Settings::keepNetHistory, &Settings::mangaOn, &Settings::mangaSwapLR, &Settings::mangaAdult, &Settings::mangaHistory,
		&Settings::mangaRecent, &Settings::mangaFullSeen, &Settings::mangaOpenFull, &Settings::mangaSecureDns};
	struct GroupName { const char* name; const char* about; };
	inline const GroupName GROUP_NAMES[] = {
		{"Subtitles", "Font, size, colour"},
		{"Music", "Display, visualizer, sound"},
		{"Photos", "Zoom, pan area, drag"},
		{"Remember", "Position, tracks"},
		{"Files", "Folders, play history"},
		{"Controls & screen", "Brightness, buttons"},
		{"Updates", "New versions"},
		{"Web dashboard", ""},	// (its line is its state: drawSettings)
	};
	inline const char* RECENT = "\x01recent";
	inline const char* HOMEDIR = "\x01home";	// Home: the Recent strip and the sections' tiles
	inline const char* EXTDIR = "\x01" "ext";	// the Extensions section: the extensions' drawer
	inline const char* SECTION_NAMES[] = {"Files", "Playlists", "Network", "Extensions"};
	inline const Ui::Glyph* const SECTION_GLYPHS[] = {&G_CARD, &G_PLAYLIST, &G_NETWORK, &G_BLOCKS};
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
		return d == RECENT || d == HOMEDIR || d == EXTDIR || d == NETROOT || d == FORMDIR || d == FINDNET || d == PLROOT || isPlDir(d);
	}
	// a section's first page (Ui::BrowseTab: Files, Playlists, Network, Extensions)
	inline std::string sectionTop(int tab)
	{
		return tab == 1 ? PLROOT : tab == 2 ? NETROOT : tab == 3 ? EXTDIR : "";
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
	inline const char* SHOW_SHORT[SHOWS] = {"Art", "Both", "Viz", "None"};	// MusicShow on a button
	inline const char* LOOP_SHORT[LOOPS] = {"Off", "All", "One"};		// Loop on a button (beside its mark)
	// the music key's modes (Ui::playMode): the Loops, then Shuffle (with any Loop)
	constexpr int MODE_SHUFFLE = LOOPS;
	inline const Ui::Glyph* const VIS_GLYPHS[VISUALIZERS] = {&G_VIS_BARS, &G_VIS_WAVES, &G_VIS_SPECTRUM, &G_VIS_CIRCLE, &G_VIS_PEAKS, &G_VIS_PEAKS_BANANA, &G_VIS_POCKET, &G_VIS_LED, &G_VIS_SYNTHWAVE, &G_VIS_BANANA, &G_VIS_CITY, &G_VIS_SCOPE, &G_VIS_HEART};
	// a show mode's mark: the visualizer ones take the style's
	inline const Ui::Glyph* showGlyph(int show, int vis)
	{
		return show == SHOW_ART ? &G_PICTURE : show == SHOW_NONE ? &G_TITLE_ONLY : VIS_GLYPHS[vis >= 0 && vis < VISUALIZERS ? vis : 0];
	}
	// the effects' short names (the song's line, Settings' value), and what each is (the Effects tab)
	inline const char* FX_SHORT[SOUNDFX_COUNT] = {"None", "Room", "Hall", "Church", "Cave", "Wide", "Phones"};
	inline const char* FX_ABOUT[SOUNDFX_COUNT] = {"Just the music", "A small, lively room", "A big hall, a long warm tail",
		"Stone walls, a very long tail", "Rock walls and echoes", "Wider than the speakers", "Less tiring in headphones"};
	inline const char* CUSTOM_NAME = "Custom";	// an edited curve, not saved (eqPreset "")
	// the keyboard's field for a preset's name (apart from the form's and the found servers' ids)
	inline const int EQ_NAME = 60, PL_NAME = 61;
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
