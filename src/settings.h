#pragma once
#include "eq.h"
#include "soundfx.h"
#include "substyle.h"
#include <cstdint>
#include "theme.h"
#include <string>
#include <vector>

namespace bp
{
	constexpr int MANGA_RTL = 0, MANGA_LTR = 1, MANGA_STRIP = 2, MANGA_MODES = 3;
	// Book (issue #7): the handheld held sideways, one page turned upright on each panel. Which way it is held: the
	// top panel on the reader's left (the buttons under the right hand) or on the right; off reads as before
	constexpr int MANGA_BOOK_OFF = 0, MANGA_BOOK_LEFT = 1, MANGA_BOOK_RIGHT = 2, MANGA_BOOKS = 3;
	// how the paged modes fit a page to the screen: Page keeps it whole, Width makes it as wide as the screen (read
	// down a screenful at a time), Fill covers the screen and crops what overflows; the Strip always fits width
	constexpr int MANGA_FIT_PAGE = 0, MANGA_FIT_WIDTH = 1, MANGA_FIT_FILL = 2, MANGA_FITS = 3;
	// the pages decoded ahead of the one read (Preload), smallest first, and the one a new setting starts on
	constexpr int MANGA_PRELOADS[] = {2, 3, 6};
	constexpr int MANGA_PRELOAD_COUNT = 3, MANGA_PRELOAD_DEFAULT = 3;
	// the next one up (dir 1) or down (-1), round: a value that isn't one of them starts from the first
	int stepMangaPreload(int v, int dir);
	// the page cache's cap as its row offers it (100 MB, 300 MB, 1 GB), the default, and the bytes of one
	constexpr int MANGA_CACHES = 3, MANGA_CACHE_DEFAULT = 2;	// 1 GB: the least recently read pages go past it
	uint64_t mangaCacheBytes(int v);
	// Music on the top panel: its album art (embedded, or cover.jpg beside it), a visualizer drawn by the player
	// (src/viznative.h; no filter graph since 2026-11-19), or the art with the visualizer along its foot; a song with no art shows the visualizer
	// whatever this says. The title, artist and album over either.
	// None: neither, the title alone on black (the lightest)
	enum MusicShow { SHOW_ART, SHOW_BOTH, SHOW_VIS, SHOW_NONE, SHOWS };
	// Peaks: a spectrum analyser's bars (coral) with a cap over each (green) that holds the bar's top and falls back
	// Peaks Banana: the same in the theme's colours (ACCENT bars, TEXT caps), offered as "Peaks" (the coral one retired)
	// Bars, Waves and Spectrum are retired, their graphs and enum values kept: nothing offers them and a setting
	// naming one reads as Peaks; VIS_SHOWN lists what's offered, in order
	// Pocket: a little LCD in a bezel, a clean scene of chunky dots: 15 dot bars with caps on a road that passes by, a
	// sun that beats, a heart and a note
	// and the styles a file each (src/viz_*.cpp, declared in src/viz_more.h): a hi-fi LED meter, Synthwave, Banana
	// stack, Night city, a phosphor oscilloscope, Heartbeat, Aurora, Turntable, Pixel sea, Disco ball, Banana dancer,
	// Jelly wobble, Tube man, Washing machine. Peaks (coral), Oscilloscope, Turntable, Disco ball, Hi-fi LED,
	// Synthwave, Banana stack and Heartbeat are kept but not offered (the owner's pick, 0.7.12)
	// The simple ones (src/viz_simple.cpp, 2026-11-19), in the theme's colours: Mirror bars, Dots, Horizon, Steps,
	// Barcode, Rings. Circle, Bubble tea, Sound garden and Long cat made way for the first ones (as did Area and
	// Radial, briefly offered), and a setting naming one reads as its replacement (visualizerReplacement); Music
	// box, Jellyfish, Wave, Levels, Campfire, Bunny hop, Fishbowl, Vibing cat and Popcorn were dropped (the owner's
	// final list): a setting naming one reads as Peaks. Their code is gone; their ids (enum values, words) stay
	// Lite (the player-drawn one, no graph) was removed for now, after the owner saw its bars sit mostly at the top
	// on the handheld: branch vizlite keeps it, docs/notes/2026-11-17-viz-lite.md has its story, and a setting naming
	// it reads as Peaks
	enum Visualizer { VIS_BARS, VIS_WAVES, VIS_SPECTRUM, VIS_CIRCLE, VIS_PEAKS, VIS_PEAKS_BANANA, VIS_FADERS, VIS_LED,
		VIS_SYNTHWAVE, VIS_BANANA_STACK, VIS_CITY, VIS_SCOPE, VIS_HEART, VIS_FIRE, VIS_AURORA, VIS_GARDEN, VIS_VINYL, VIS_SEA, VIS_BUNNY, VIS_BOBA,
		VIS_JELLY, VIS_MUSICBOX, VIS_FISHBOWL, VIS_DISCO, VIS_DANCER, VIS_CAT, VIS_POPCORN, VIS_WOBBLE, VIS_TUBEMAN,
		VIS_LONGCAT, VIS_WASHER, VIS_MIRROR, VIS_WAVE, VIS_DOTS, VIS_LEVELS, VIS_AREA, VIS_RADIAL, VIS_HORIZON,
		VIS_STEPS, VIS_BARCODE, VIS_RINGS, VIS_RIBBON, VIS_GRID, VIS_STARFIELD, VIS_STRINGS, VIS_HELIX, VIS_SPARKS,
		VIS_BOUNCY, VIS_ROCKET, VIS_LANTERNS, VIS_MOON, VIS_DANCE, VISUALIZERS };
	// the owner's final ten (2026-11-19) and the five fun ones after them, in its order, all drawn by the player
	// (src/viznative.h, no mpv graph). The others are commented out of the list, their code kept so they can come
	// back: a setting naming one reads as Peaks
	constexpr int VIS_SHOWN[] = {VIS_PEAKS_BANANA, VIS_RIBBON, VIS_MIRROR, VIS_FADERS, VIS_HORIZON, VIS_GRID, VIS_STARFIELD, VIS_STRINGS,
		VIS_HELIX, VIS_SPARKS, VIS_BOUNCY, VIS_ROCKET, VIS_LANTERNS, VIS_MOON, VIS_DANCE /* , VIS_CITY, VIS_AURORA, VIS_DOTS, VIS_STEPS, VIS_BARCODE, VIS_RINGS, VIS_DANCER, VIS_SEA, VIS_WOBBLE,
		VIS_WASHER, VIS_TUBEMAN */};
	constexpr int VIS_SHOWN_COUNT = (int)(sizeof(VIS_SHOWN) / sizeof(VIS_SHOWN[0]));
	// a visualizer's place in VIS_SHOWN (a hidden one: the first offered, Peaks, at 0)
	int visualizerSlot(int v);
	// the next one offered after v (dir 1) or before it (-1), round
	int stepVisualizer(int v, int dir);
	// a retired style's replacement (Circle: Mirror bars, Bubble tea: Dots, Sound garden and Area: Horizon, Long cat and
	// Radial: Steps; the dropped ones, whose code is gone: Peaks); any other as it is
	int visualizerReplacement(int v);
	// drawn on the theme's BG in the theme's colours when alone (Peaks, the simple ones): the song's lines over it
	// are the theme's too
	bool visualizerOnGround(int v);
	// music only (a video never loops): all = back to the folder's first file after its last
	enum Loop { LOOP_OFF, LOOP_ALL, LOOP_ONE, LOOPS };
	// the clock in the bottom panel's header
	enum ClockMode { CLOCK_24, CLOCK_12, CLOCK_OFF, CLOCKS };
	// the bottom panel dark by itself after that long without a touch or a button, while a song or video plays (never)
	// the default picture look of every video (Settings > Playback > Picture look): a mpv filter on the film, none on a
	// song or a photo
	enum PictureLook { LOOK_OFF, LOOK_SHARPEN, LOOK_SOFTEN, LOOK_SCANLINES, LOOK_LCD, LOOK_RETRO, PICTURE_LOOKS };
	// the mpv `vf` text of a look ("" for Off)
	const char* pictureLookFilter(int look);
	enum ScreenOffAfter { SOFF_NEVER, SOFF_15, SOFF_30, SOFF_60, SOFF_120, SOFF_300, SCREEN_OFFS };
	int screenOffSeconds(int v);	// 0: never
	const char* screenOffLabel(int v);	// "Never", "15 seconds", "1 minute"...
	// L, R, L2 and R2 with the lid shut (docs/redesign/lid-and-incognito.md, "Shoulder keys, lid shut")
	enum LidShoulder { LID_SHOULDER_IGNORED, LID_SHOULDER_HOLD, LID_SHOULDER_AS_USUAL, LID_SHOULDERS };
	// the power key with the lid shut: a press plays or pauses (a 2 s hold sleeps), or the key is BananaOS's (a press
	// sleeps, as with the lid open: the player never takes it)
	enum LidPower { LID_POWER_PLAY, LID_POWER_SLEEP, LID_POWERS };
	// the volume keys with the lid shut: a press is a step and a 0.6 s hold skips (the player takes the device), or
	// only the volume, as with the lid open (the player never takes them)
	enum LidVolume { LID_VOLUME_SKIP, LID_VOLUME_ONLY, LID_VOLUMES };
	// what incognito does when the lid closes or the handheld sleeps: nothing, close all, quit
	enum LidIncognito { LID_INCOGNITO_NOTHING, LID_INCOGNITO_CLOSE_ALL, LID_INCOGNITO_QUIT, LID_INCOGNITOS };
	const char* musicShowLabel(int v);
	const char* visualizerLabel(int v);
	const char* loopLabel(int v);
	// mpv's lavfi-complex: the first audio track to the speakers and, drawn, to the picture; overCover: over the first
	// video track (the art), on its lower part
	// mapsDir: where writeVisualizerMaps put the styles' stills; a style that reads some is drawn as Peaks without them
	// fps: the styles drawn at 60 a second (Pocket console, src/viz_*.cpp) drawn at 30 or 45 instead, as fast in
	// seconds (vizAtFps, src/vizcache.h); Peaks is 30 at any setting
	std::string visualizerGraph(int vis, bool overCover, const std::string& mapsDir = "", int fps = 60);
	// the same before vizAtFps, with its cacheable lut and lut2 marked (vizCacheLuts, src/vizcache.h, makes it a graph)
	std::string visualizerGraphMarked(int vis, bool overCover, const std::string& mapsDir = "");
	// a theme colour helper the styles share: WCAG contrast, and `c` moved towards `to` till it reaches `ratio` on `on`
	double colourContrast(theme::Rgb a, theme::Rgb b);
	theme::Rgb inkOn(theme::Rgb c, theme::Rgb on, theme::Rgb to, double ratio);
	// the native styles' two derived inks (docs/notes/2026-11-19-faders-lanterns.md): INK is ACCENT moved towards TEXT
	// until 3:1 on BG; LINE is RULE 25% (light theme) or 35% (dark) of the way to DIM, a quiet rule that still shows
	theme::Rgb visualizerInk();
	theme::Rgb visualizerLine();
	// every style's stills into dir (src/viz_*.cpp: the scenes' pictures): true when all are there.
	// The retired styles' stills, if any are left there, removed
	bool writeVisualizerMaps(const std::string& dir);
	// a style that reads stills from the maps folder (drawn as Peaks while there's none)
	bool visualizerNeedsMaps(int vis);
	// a path as a filter's argument, quoted; false for one a graph can't name (':', '=', a quote, a backslash)
	bool lavfiPath(const std::string& p, std::string& out);
	// a folder a filter graph can name (FFmpeg's option parser splits at ':' and '=' even inside quotes)
	bool lavfiSafePath(const std::string& dir);
	// None's picture: a tiny one at 5 frames a second drawn from the sound (so it ends with it), filled with `ground`
	// (lavfi's colour: the theme's BG under a song's lines, "0xRRGGBB"): the last visualizer's frame would stay otherwise
	std::string blankGraph(const std::string& ground = "black");

	// <stateDir>/settings.conf: what the Settings page changes, for every file
	// text-theme's values: Match theme (the page the app's theme picks: settings.h's txtPage), then textreader.h's
	// pages one up (Dark, Light, Sepia, Black)
	constexpr int TXT_PAGE_MATCH = 0, TXT_PAGES = 5;
	struct Settings
	{
		SubStyle style;
		// what a file opened again comes back with
		bool rememberPosition = true, rememberAudio = true, rememberSubs = true;
		// the browser opens where it was left, rather than at its first page
		bool rememberFolder = false;
		// the videos played, for Recently watched; off: none kept (and those kept so far gone)
		bool keepHistory = true;
		// network files in Recently watched too; off: they're left out (and those kept so far go)
		bool keepNetHistory = true;
		// the handheld's buttons while a video plays; off: only touch acts there (no press by accident in a pocket
		// or a hand), the menus' and the browser's buttons still work
		bool buttonHotkeys = true;
		int musicShow = SHOW_ART, visualizer = VIS_PEAKS_BANANA, loop = LOOP_OFF;
		// Match album art (Settings › Music & sound, and the foot of the song's Display sheet; docs/redesign
		// "Match album art"): a song's art, blurred and muted, is the ground behind the player and the top
		// screen — one hue from the art tinting the theme's own charcoal, never a colour of the UI. The theme's
		// keys, chips, accent and text stay the theme's; High contrast keeps its plain ground whatever this says
		bool matchAlbumArt = true;
		// the visualizers' frame rate: 30, 45 (default: smooth, lighter than 60 on the battery) or 60; Peaks and Circle
		// draw at 30 whatever it says
		int vizFps = 45;
		int volumeMax = 120;	// the volume's top: 100, 120 or 150 (above 100 the player's own gain, over the handheld's 100%); 120 by default, 150 only asked for
		// the play mode Shuffle (the music key, the web's Shuffle): every list started plays shuffled from the item
		// picked, till it's turned off; loop is off meanwhile (four modes). No Settings row: those keys are its switch
		bool shuffle = false;
		// folders with a ".hidden" file in them: listed (and their videos in Recent) only when this is on
		bool showHiddenFolders = false;
		// the battery's percent beside its icon on the bottom panel (the icon is always there)
		bool batteryPercent = false;
		bool scrollTitles = true;	// a title too long for its place moves along (the Title marquee); off: cut with "…"
		// closing the lid while something plays: pause (false), or keep playing (BananaOS's lid sleep switched off while
		// it plays: lidsleep_host.h)
		bool lidKeepPlaying = false;
		// with the lid shut and Keep playing: the power key's play/pause or BananaOS's sleep
		int lidPower = LID_POWER_PLAY;
		// with the lid shut and Keep playing: the volume keys' press and hold, or only the volume
		int lidVolume = LID_VOLUME_SKIP;
		// with the lid shut and Keep playing: L / R / L2 / R2 ignored, held 0.6 s, or as with the lid open
		int lidShoulder = LID_SHOULDER_IGNORED;
		// incognito while the lid closes or the handheld sleeps: nothing, close everything, or quit
		int lidIncognito = LID_INCOGNITO_CLOSE_ALL;
		// (not saved) the file named no lid-close: the choice is asked once at start (Ui::askLid), then kept
		bool lidAsk = false;
		// the app's colours, both panels and the dashboard (Settings › Power & screens › Theme): theme.h's table, by index
		int theme = 1;	// Dark (theme.cpp's order: Yellow, the look before themes, is 0)
		// the time left of the battery (Settings › Power & screens): 24-hour, 12-hour or none
		int clock = CLOCK_24;
		// the bottom panel off by itself (Settings › Power & screens) while a song or video plays and nothing is touched
		int screenOffAfter = SOFF_30;		// while a song plays
		int screenOffVideo = SOFF_30;		// while a video plays: a dark bottom panel keeps the eye on the film; Never for those who want its controls lit (issue #5)
		// the bottom panel dimmed (not off) for the last 5 s before Screen off: a warning that a touch only cancels
		bool dimBeforeOff = true;
		// reading (a book, a comic, a document): the bottom panel veiled after that long untouched (a touch lifts it)
		int readDimAfter = SOFF_60;
		// a copy, a download or an update holds the handheld awake (the player's idle sleep: idlesleep.h)
		bool stayAwakeJobs = true;
		// the film's look, kept (Settings > Playback > Picture look)
		int pictureLook = LOOK_OFF;
		// the Picture sheet's Adjust: brightness, contrast, saturation, gamma (-100..100), kept between runs
		int videoAdjust[4] = {0, 0, 0, 0};
		// photos: the next one opens at the zoom (and the place in it) the last was left at; off: each at its size
		bool keepPhotoZoom = true;
		// the photo's pan area on the bottom panel: the left half (the default) or the right
		bool panRight = false;
		// a drag in the pan area moves the photo (as on a phone: it follows the finger on the top panel) or the
		// box over the small photo (the top panel follows the box)
		bool dragBox = true;
		// the built-in updater (a Ports install): look for a new release at each start, in the background
		bool checkUpdates = true;
		// one brightness for both panels (quick settings and Settings › Power & screens); off: each its own
		bool sameBrightness = true;
		// its pop-up's "Not until tomorrow": not shown before this time (seconds since 1970)
		long long updateRemindAfter = 0;
		// the equalizer (music only). eqPreset: the preset the curve is ("" = Custom, an edited curve not saved);
		// eqOrigin: the preset Custom was made from; eqGains: the curve that plays, whichever
		bool eqOn = false;
		std::string eqPreset = "Flat", eqOrigin;
		EqGains eqGains{};
		// music's sound effect (reverb, wide stereo...), whether or not the equalizer is on
		SoundFx soundFx = SoundFx::None;
		// the web dashboard (Settings › Web dashboard, quick settings): its server runs from the start while this is
		// on, across restarts. Not a row of the switches: its page turns it on and off
		bool webDashboard = false;
		// the manga reader (Manage extensions): off by default; off, nothing of it shows or runs
		bool mangaOn = false;
		// the manga reader's mode for a series that has none of its own: right to left (printed manga), left to right, strip
		int mangaMode = MANGA_STRIP;	// a new reader reads down a strip (webtoons and most sources' chapters)
		// the paged modes' fit (MANGA_FIT_*): Page, Width, Fill; the Strip always fits width
		int mangaFit = MANGA_FIT_PAGE;
		// Files' document viewers. The text viewer (TXT, EPUB): its font, size, line spacing, margins, colours and
		// filter (textreader.h's TXT_* and DOC_FILTER_*); the PDF viewer: its filter, and Single page or Strip
		int txtFont = 0, txtSize = 2, txtSpacing = 1, txtMargins = 1, txtTheme = TXT_PAGE_MATCH, txtFilter = 0;
		// the text viewer's view: 0 the scroll, MANGA_BOOK_LEFT / _RIGHT Book (held sideways, the top panel on that side)
		int txtBook = 0;
		// Book mode's page turn: 0 the fold (a page folding at the spine), 1 none (the pages change at once)
		int txtTurn = 0;
		int pdfFilter = 0;
		// the Manga reader's own reading filter (DOC_FILTER_*), and every reader's extra dimming past the screen's
		// lowest backlight (0-80 percent darker: the pages' colours scaled down)
		int mangaFilter = 0, readDim = 0;
		bool pdfStrip = false;
		// Off: RIGHT is forward on the reader's page. On: LEFT is (to match a right-to-left book)
		bool mangaSwapLR = false;
		// pages decoded ahead of the one read: 2, 3 or 6
		int mangaPreload = MANGA_PRELOAD_DEFAULT;
		// fullscreen: the strip skipped under the hinge between the panels, in 480-line units (measured on the handheld)
		int mangaHinge = 36;
		// fullscreen's first open has shown its toast (a hint to leave it): shown once, ever
		bool mangaFullSeen = false;
		// a chapter opens on both panels (fullscreen) rather than on this one's page alone
		bool mangaOpenFull = true;	// a chapter opens over both panels
		// the page cache's cap, as the row offers it (MANGA_CACHES, 0 is 100 MB)
		int mangaCacheSize = MANGA_CACHE_DEFAULT;
		// adult sources a repository marks as adult: shown in the lists (hidden by default)
		bool mangaAdult = false;
		// Sources › Available shows these languages only ("en,id"; "" is every language). "all" (a source for every
		// language) always shows
		std::string mangaLangs;
		// the History list (the reading progress, read marks and places, is kept either way); off clears it
		bool mangaHistory = true;
		// manga in Home's Recent (the card comes later)
		bool mangaRecent = true;
		// sites are looked up through Cloudflare over HTTPS (Secure DNS): past a network whose own DNS blocks them
		bool mangaSecureDns = false;
		// its first-time note about repositories has been shown: it isn't again when the reader is turned on
		bool mangaIntroShown = false;
		// Opening Manga while a video plays asks first: the manga-guard sheet (Stop video & open Manga / Stay). Off
		// ("Don't ask again" in that sheet, or Settings › Manga) stops the video and opens Manga on the press
		bool mangaAskVideo = true;
	};

	std::string serializeSettings(const Settings& s);
	// the reader's page now (textreader.h's TXT_THEME_*): text-theme's own, or Match theme's by the app's theme
	int txtPage(const Settings& s);
	Settings parseSettings(const std::string& text);

	// The Settings page's groups (Ui::SetGroup's order: ui_settings.cpp checks it), and the manga reader's own page
	enum SettingsGroup { SG_NONE = -1, SG_PLAYBACK, SG_SUBS, SG_MUSIC, SG_PHOTOS, SG_READING, SG_FILES, SG_SCREEN, SG_WEB, SG_UPDATES, SG_MANGA = 100 };

	// One setting of settings.conf, declared once in settings.cpp's table: its key, its member, how its value is read and
	// written, and (when a page shows it) the group, place, label, value text and help line of its row. Parsing,
	// writing, clamping, the page's value text and its Left/Right all read this. The default is the member's own
	// initializer in Settings. Built by the helpers below: flag(), choice(), ints(), range(), text(), custom(), then
	// .page(...) for a row.
	struct SettingDef
	{
		enum Kind { FLAG, CHOICE, INT_LIST, INT_RANGE, TEXT, CUSTOM };
		Kind kind = FLAG;
		const char* key = "";
		bool Settings::*flagField = nullptr;
		int Settings::*intField = nullptr;
		std::string Settings::*textField = nullptr;
		// the file's words: a FLAG's (no, yes) or a CHOICE's, by value
		const char* const* ids = nullptr;
		int count = 0;
		// INT_LIST: the values the file may name. INT_RANGE: min..max, rounded down to a multiple of snap (1: as is)
		const int* values = nullptr;
		int min = 0, max = 0, snap = 1;
		bool blankIgnored = false;	// INT_RANGE: an empty value is left alone (the rest read it as 0, as atoi does)
		bool canonical = false;	// INT_LIST: only the value written as its own digits ("03" isn't 3)
		// run after a line of this key is read (Visualizer: a retired style reads as the default)
		void (*after)(Settings&, const std::string& value) = nullptr;
		// CUSTOM: its own reading (the value of a line) and writing (the whole line's value, or false to leave it out)
		void (*parse)(Settings&, const std::string& value) = nullptr;
		bool (*write)(const Settings&, std::string& value) = nullptr;
		// the page: group (SG_NONE: no row), place in the group (ascending; the page's own rows have theirs), text
		int group = SG_NONE, place = 0;
		const char* label = "";
		const char* help = "";
		const char* const* shown = nullptr;	// the value text by value (a FLAG's: off, on; default Off, On)
		const char* unit = "";	// an int's value text: the number, then this
		bool wrap = false;	// Left/Right go round (otherwise stop at the ends)
		int (*stepFn)(int v, int dir) = nullptr;	// Left/Right by its own rule (Visualizer's: the styles offered)
		int stepBy = 1;	// INT_RANGE: the step

		static SettingDef flag(const char* key, bool Settings::*f, const char* no = "no", const char* yes = "yes");
		static SettingDef choice(const char* key, int Settings::*f, const char* const* ids, int count);
		static SettingDef ints(const char* key, int Settings::*f, const int* values, int count);
		static SettingDef range(const char* key, int Settings::*f, int min, int max, int snap = 1);
		static SettingDef text(const char* key, std::string Settings::*f);
		static SettingDef custom(const char* key, void (*parse)(Settings&, const std::string&), bool (*write)(const Settings&, std::string&));
		SettingDef page(int group, int place, const char* label, const char* help = "") const
		{
			SettingDef d = *this;
			d.group = group; d.place = place; d.label = label; d.help = help;
			return d;
		}
		SettingDef shownAs(const char* const* shown_, const char* unit_ = "") const { SettingDef d = *this; d.shown = shown_; d.unit = unit_; return d; }
		SettingDef stepped(bool wrap_, int (*fn)(int, int) = nullptr, int by = 1) const { SettingDef d = *this; d.wrap = wrap_; d.stepFn = fn; d.stepBy = by; return d; }
		SettingDef digitsOnly() const { SettingDef d = *this; d.canonical = true; return d; }
		SettingDef ignoreBlank() const { SettingDef d = *this; d.blankIgnored = true; return d; }
		SettingDef afterRead(void (*fn)(Settings&, const std::string&)) const { SettingDef d = *this; d.after = fn; return d; }
	};

	// every setting, in the order serializeSettings writes them (the style's keys come first, apart)
	const std::vector<SettingDef>& settingDefs();
	// the table's index of a key (-1: none), and the rows of one group of the page: their indexes, by place
	int settingIndex(const char* key);
	std::vector<int> settingsOfGroup(int group);
	// the text a row shows for the setting's value now ("On", "120%", "Cloudflare (1.1.1.1)")
	std::string settingValueText(const SettingDef& d, const Settings& s);
	// Left (-1) / Right (1) on the row: flips a FLAG, steps the others
	void stepSetting(const SettingDef& d, Settings& s, int dir);
}
