#pragma once
#include "eq.h"
#include "soundfx.h"
#include "substyle.h"
#include <cstdint>
#include <string>

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
	// Music on the top panel: its album art (embedded, or cover.jpg beside it), a visualizer drawn from the sound by
	// FFmpeg's filters, or the art with the visualizer over its lower part; a song with no art shows the visualizer
	// whatever this says. The title, artist and album over either.
	// None: neither, the title alone on black (the lightest)
	enum MusicShow { SHOW_ART, SHOW_BOTH, SHOW_VIS, SHOW_NONE, SHOWS };
	// Circle: the bars round a ring (cyan to purple to pink), drawn by looking each pixel up in two tables (remap)
	// Peaks: a spectrum analyser's bars (coral) with a cap over each (green) that holds the bar's top and falls back
	// Peaks Banana: the same in BananaOS's colours (yellow bars, cream caps)
	// Bars, Waves and Spectrum are retired, their graphs and enum values kept: nothing offers them and a setting
	// naming one reads as Peaks; VIS_SHOWN lists what's offered, in order
	// Pocket: Peaks as an old handheld's screen, four greens on a grid of chunky dots
	// and six more (src/viz_more.h, a file each): a hi-fi LED meter, Synthwave, Banana stack, Night city, a phosphor
	// oscilloscope and Heartbeat; then Campfire, Aurora, Sound garden, Turntable and Pixel sea; then Bunny hop, Bubble tea,
	// Jellyfish, Music box, Fishbowl, Disco ball and Banana dancer. Vibing cat; then Popcorn machine, Jelly wobble,
	// Tube man, Long cat and Washing machine. Peaks (coral), Oscilloscope, Turntable, Disco ball, Hi-fi LED,
	// Synthwave, Banana stack and Heartbeat are kept but not offered (the owner's pick, 0.7.12): Peaks Banana is the
	// default, and a setting naming one not offered reads as it
	enum Visualizer { VIS_BARS, VIS_WAVES, VIS_SPECTRUM, VIS_CIRCLE, VIS_PEAKS, VIS_PEAKS_BANANA, VIS_POCKET, VIS_LED,
		VIS_SYNTHWAVE, VIS_BANANA_STACK, VIS_CITY, VIS_SCOPE, VIS_HEART, VIS_FIRE, VIS_AURORA, VIS_GARDEN, VIS_VINYL, VIS_SEA, VIS_BUNNY, VIS_BOBA,
		VIS_JELLY, VIS_MUSICBOX, VIS_FISHBOWL, VIS_DISCO, VIS_DANCER, VIS_CAT, VIS_POPCORN, VIS_WOBBLE, VIS_TUBEMAN,
		VIS_LONGCAT, VIS_WASHER, VISUALIZERS };
	constexpr int VIS_SHOWN[] = {VIS_PEAKS_BANANA, VIS_CIRCLE, VIS_POCKET, VIS_CITY, VIS_DANCER, VIS_FIRE, VIS_AURORA, VIS_GARDEN,
		VIS_SEA, VIS_BUNNY, VIS_BOBA, VIS_JELLY, VIS_MUSICBOX, VIS_FISHBOWL, VIS_CAT, VIS_POPCORN, VIS_WOBBLE, VIS_TUBEMAN, VIS_LONGCAT, VIS_WASHER};
	constexpr int VIS_SHOWN_COUNT = (int)(sizeof(VIS_SHOWN) / sizeof(VIS_SHOWN[0]));
	// a visualizer's place in VIS_SHOWN (a hidden one: Peaks's, 0)
	int visualizerSlot(int v);
	// the next one offered after v (dir 1) or before it (-1), round
	int stepVisualizer(int v, int dir);
	// music only (a video never loops): all = back to the folder's first file after its last
	enum Loop { LOOP_OFF, LOOP_ALL, LOOP_ONE, LOOPS };
	// the clock in the bottom panel's header
	enum ClockMode { CLOCK_24, CLOCK_12, CLOCK_OFF, CLOCKS };
	// the bottom panel dark by itself after that long without a touch or a button, while a song or video plays (never)
	enum ScreenOffAfter { SOFF_NEVER, SOFF_15, SOFF_30, SOFF_60, SOFF_120, SOFF_300, SCREEN_OFFS };
	int screenOffSeconds(int v);	// 0: never
	const char* screenOffLabel(int v);	// "Never", "15 seconds", "1 minute"...
	const char* musicShowLabel(int v);
	const char* visualizerLabel(int v);
	const char* loopLabel(int v);
	// mpv's lavfi-complex: the first audio track to the speakers and, drawn, to the picture; overCover: over the first
	// video track (the art), on its lower part
	// mapsDir: where writeCircleMaps put Circle's tables; without them Circle is drawn as Bars
	// fps: the styles drawn at 60 a second (Pocket console, src/viz_*.cpp) drawn at 30 or 45 instead, as fast in
	// seconds (vizAtFps, src/vizcache.h); Peaks and Circle are 30 at any setting
	std::string visualizerGraph(int vis, bool overCover, const std::string& mapsDir = "", int fps = 60);
	// the same before vizAtFps, with its cacheable lut and lut2 marked (vizCacheLuts, src/vizcache.h, makes it a graph)
	std::string visualizerGraphMarked(int vis, bool overCover, const std::string& mapsDir = "");
	// Circle's tables, remap's xmap and ymap as 16-bit PGMs, in dir (made once, kept); false if they can't be written
	bool writeCircleMaps(const std::string& dir);
	// every style's stills into dir (Circle's tables, and what the six newer ones read): true when all are there
	bool writeVisualizerMaps(const std::string& dir);
	// a style that reads stills from the maps folder (drawn as Peaks while there's none)
	bool visualizerNeedsMaps(int vis);
	// a path as a filter's argument, quoted; false for one a graph can't name (':', '=', a quote, a backslash)
	bool lavfiPath(const std::string& p, std::string& out);
	// a folder a filter graph can name (FFmpeg's option parser splits at ':' and '=' even inside quotes)
	bool lavfiSafePath(const std::string& dir);
	// Circle's picture, the panel's shape: the circle a little low, under the song's title
	constexpr int CIRCLE_W = 640, CIRCLE_H = 480;
	// the tables cover only the circle's square (its bars' reach, 200 out from 320,274; black round it is pad's): the
	// lookup is most of a frame's cost, and the square is half the picture
	constexpr int CIRCLE_BOX = 400, CIRCLE_X = 120, CIRCLE_Y = 74;
	// None's picture: black, a tiny one at 5 frames a second drawn from the sound (so it ends with it): the last
	// visualizer's frame would stay otherwise
	std::string blankGraph();

	// <stateDir>/settings.conf: what the Settings page changes, for every file
	struct Settings
	{
		SubStyle style;
		// what a file opened again comes back with
		bool rememberPosition = true, rememberAudio = true, rememberSubs = true;
		// the browser opens where it was left, rather than at its first page
		bool rememberFolder = false;
		// the song's title, artist and album over the top panel's picture (off: the picture alone; None still shows the
		// title, that being all it shows)
		bool songInfo = false;
		// the videos played, for Recently watched; off: none kept (and those kept so far gone)
		bool keepHistory = true;
		// network files in Recently watched too; off: they're left out (and those kept so far go)
		bool keepNetHistory = true;
		// the handheld's buttons while a video plays; off: only touch acts there (no press by accident in a pocket
		// or a hand), the menus' and the browser's buttons still work
		bool buttonHotkeys = true;
		int musicShow = SHOW_ART, visualizer = VIS_PEAKS_BANANA, loop = LOOP_OFF;
		// the visualizers' frame rate: 30, 45 (default: smooth, lighter than 60 on the battery) or 60; Peaks and Circle
		// draw at 30 whatever it says
		int vizFps = 45;
		// the play mode Shuffle (the music key, the web's Shuffle): every list started plays shuffled from the item
		// picked, till it's turned off; loop is off meanwhile (four modes). No Settings row: those keys are its switch
		bool shuffle = false;
		// folders with a ".hidden" file in them: listed (and their videos in Recent) only when this is on
		bool showHiddenFolders = false;
		// the battery's percent beside its icon on the bottom panel (the icon is always there)
		bool batteryPercent = false;
		// the time left of the battery (Settings › Controls & screen): 24-hour, 12-hour or none
		int clock = CLOCK_24;
		// the bottom panel off by itself (Settings › Controls & screen) while a song or video plays and nothing is touched
		int screenOffAfter = SOFF_30;		// while a song plays
		int screenOffVideo = SOFF_30;		// while a video plays: a dark bottom panel keeps the eye on the film; Never for those who want its controls lit (issue #5)
		// photos: the next one opens at the zoom (and the place in it) the last was left at; off: each at its size
		bool keepPhotoZoom = true;
		// the photo's pan area on the bottom panel: the left half (the default) or the right
		bool panRight = false;
		// a drag in the pan area moves the photo (as on a phone: it follows the finger on the top panel) or the
		// box over the small photo (the top panel follows the box)
		bool dragBox = true;
		// the built-in updater (a Ports install): look for a new release at each start, in the background
		bool checkUpdates = true;
		// one brightness for both panels (quick settings and Settings › Controls & screen); off: each its own
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
		int txtFont = 0, txtSize = 2, txtSpacing = 1, txtMargins = 1, txtTheme = 0, txtFilter = 0;
		// the text viewer's view: 0 the scroll, MANGA_BOOK_LEFT / _RIGHT Book (held sideways, the top panel on that side)
		int txtBook = 0;
		// Book mode's page turn: 0 the fold (a page folding at the spine), 1 none (the pages change at once)
		int txtTurn = 0;
		int pdfFilter = 0;
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
	};

	std::string serializeSettings(const Settings& s);
	Settings parseSettings(const std::string& text);
}
