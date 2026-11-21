// banana-player: a video player for BananaOS. The picture on the top panel, the controls on the bottom one.
//
//   banana-player [--press A,X,Down,...] [--windowed] [--aspect fit|fill|stretch] [--lang eng,en] [--state-dir DIR] [--font TTF] [-v] FILE

#include "crashlog.h"
#include <csignal>
#include "format.h"
#include "framewatch.h"
#include "input.h"
#include "player.h"
#include "playlist.h"
#include "png.h"
#include "sleepwatch.h"
#include "levelwatch.h"
#include "levelcard.h"
#include "ampwatch.h"
#include "touch.h"
#include "reader_proto.h"
#include "reader.h"
#include "textreader.h"
#include "topscreen.h"
#include "theme.h"
#include "readerfetch.h"
#include "manganet.h"
#include "ui.h"
#include "ui_glyphs.h"
#include "ui_docs.h"
#include "ui_sleep.h"
#include "ui_manga.h"
#include "ui_browse.h"
#include "ui_home.h"
#include "ui_music.h"
#include "ui_videos.h"
#include "ui_books.h"
#include "ui_search.h"
#include "session_host.h"
#include "updater.h"
#include "idlesleep_host.h"
#include "lidsleep_host.h"
#include "lidtrigger.h"
#include "powerkey.h"
#include "volumekey.h"
#include "battery.h"
#include "netstore.h"
#include "networker.h"
#include "durations.h"
#include "durworker.h"
#include "coverworker.h"
#include "videoartworker.h"
#include "ui_mediaart.h"
#include "mediaworker.h"
#include "mangalibworker.h"
#include "netstream.h"
#include "netpath.h"
#ifdef BP_WEB
#include "browser.h"
#include "webstate.h"
#include "web/netif.h"
#include "web/server.h"
#include "webmedia.h"
#endif
#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sstream>
#include <sys/stat.h>
#include <vector>
#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace bp
{
	const char* buildCommit();	// buildinfo.cpp (generated): the commit, for crash.log
}

using namespace bp;

#ifdef BP_WEB
namespace bp
{
	// api_player.cpp: the playback state as the web dashboard shows it, and as its pages get it
	WebSnapshot webSnapshot(WebServer& ws, Player& p, Ui& u);
	std::string webSnapshotJson(const WebSnapshot& s, const Ui& u);
}
#endif

static Uint32 evMpv, evRender, evTouch, evLevel, evNet, evFound, evWeb, evDur, evCover, evAmp, evMangaLib, evUnplug, evPower, evVolKey, evLidIncognito, evViz, evIndex, evVideoArt;
#ifdef BP_WEB
// the dashboard's server for the sleep watch's thread (made on the main thread, never destroyed)
static std::atomic<bp::WebServer*> webForWatch{nullptr};
#endif
static std::atomic<bool> mpvPending{false}, renderPending{false};

static void onMpvWakeup(void*)
{
	if (mpvPending.exchange(true)) return;
	SDL_Event e{};
	e.type = evMpv;
	SDL_PushEvent(&e);
}

static void onMpvRender(void*)
{
	if (renderPending.exchange(true)) return;
	SDL_Event e{};
	e.type = evRender;
	SDL_PushEvent(&e);
}

static void* glProc(void*, const char* name)
{
	return SDL_GL_GetProcAddress(name);
}

// (named apart from bp::exists, which the ui headers' own helper is: both are in view here)
static bool pathThere(const std::string& p)
{
	struct stat st;
	return !p.empty() && stat(p.c_str(), &st) == 0;
}

static std::string findFont()
{
	std::string base;
	if (char* b = SDL_GetBasePath())
	{
		base = b;
		SDL_free(b);
	}
	// installed (the App Store package: bin/ and share/ side by side), then the source tree
	for (std::string p : {base + "../share/banana-player/fonts/PixelifySans-Regular.ttf",
			 base + "assets/fonts/PixelifySans-Regular.ttf",
			 std::string(BP_ASSETS_DIR) + "/fonts/PixelifySans-Regular.ttf"})
		if (pathThere(p)) return p;
	return "";
}

static std::string defaultStateDir()
{
	// a Store app's files go under /data/home (run-store-app sets XDG_CONFIG_HOME there)
	if (const char* cfg = getenv("XDG_CONFIG_HOME")) return std::string(cfg) + "/banana-player";
	if (pathThere("/data/banana")) return "/data/home/.config/banana-player";
	if (const char* home = getenv("HOME")) return std::string(home) + "/.local/state/banana-player";
	return "";
}

static void usage()
{
	fprintf(stderr,
		"banana-player " BP_VERSION "\n"
		"usage: banana-player [options] [FILE]\n"
		"  with no FILE (or a folder), a browser on the bottom panel picks the videos\n"
		"  --windowed            two windows on a computer instead of the handheld's two panels\n"
		"  --aspect MODE         fit (default), fill or stretch\n"
		"  --lang LIST           preferred audio and subtitle languages (default: eng,en)\n"
		"  --state-dir DIR       where the settings and resume positions are kept\n"
		"  --manga FOLDER        test: the manga reader's strip prototype on a folder of pages (B quits)\n"
		"  --font TTF            the bottom panel's font\n"
		"  -v                    mpv's verbose log\n"
		"  --press LIST          test run: once the file is open, do these 0.8 s apart and print the state after\n"
		"                        each, then quit: buttons (A,B,X,Y,L,R,L2,R2,Up,Down,Left,Right,Start,Select), taps\n"
		"                        (tap:X:Y, dtap:X:Y a double-tap), drags (drag:X1:Y1:X2:Y2; down:X:Y, move:X:Y, up:X:Y a finger kept down) on the bottom panel's 640x480, and info\n"
		"                        (prints the Media info page), hold:X:Y (a finger held 1.1 s), prop:NAME\n"
		"                        (prints one of mpv's properties), BUTTON*N (N presses 80 ms apart),\n"
		"                        BUTTON~MS (held MS ms, then let go)\n"
		"buttons: A play/pause, LEFT -5 s, RIGHT +10 s, L/R previous/next file, L2/R2 chapters, DOWN the chips,\n"
		"         X or SELECT More (its first row the chips, then Lock, Screen off, Info, Stop...), Y the list, START settings, B back\n"
		"keyboard: Enter, arrows, PgUp/PgDn (or Q/E), X, Y, Tab, Esc twice\n");
}

#ifdef __linux__
// BananaOS's power daemon (bananad) dims the panels after 2 idle minutes and sleeps after 10 unless a game runs: a
// live process named in /run/banana/game.pid. Its menu (banana-ui 0.5.3.4) writes that file through a stream it keeps
// open, unflushed, for the whole session, so the file stays empty while the game runs, and a film went dim and then to
// sleep (issue #4). An empty file, or one naming a process that's gone, gets this player's pid and start time (the
// form bananad reads); the menu's own write, at the session's end, replaces it and then removes it
static void holdIdleSleep()
{
	const char* path = "/run/banana/game.pid";
	FILE* f = fopen(path, "r");
	if (!f) return;	// not launched by BananaOS's menu (or not BananaOS): nothing to fill
	long pid = 0;
	int got = fscanf(f, "%ld", &pid);
	fclose(f);
	if (got == 1 && pid > 0 && kill((pid_t)pid, 0) == 0) return;	// a live one: bananad sees a game already
	char buf[1024] = {};
	FILE* st = fopen("/proc/self/stat", "r");
	if (!st) return;
	bool ok = fgets(buf, sizeof buf, st) != nullptr;
	fclose(st);
	if (!ok) return;
	const char* q = strrchr(buf, ')');
	if (!q) return;
	q += 2;
	for (int i = 0; i < 19 && *q; i++) { while (*q && *q != ' ') q++; while (*q == ' ') q++; }
	unsigned long long start = strtoull(q, nullptr, 10);
	if (FILE* w = fopen(path, "w"))
	{
		fprintf(w, "%ld %llu\n", (long)getpid(), start);
		fclose(w);
		fprintf(stderr, "banana-player: game.pid was %s: this player named in it (BananaOS's idle dim and sleep held)\n",
			got == 1 ? "a process that's gone" : "empty");
	}
}
#endif

static std::string flipKey, flipVals[2];
static int flipLeft = 0;
static Uint32 flipAt = 0, flipMs = 300;

int main(int argc, char** argv)
{
#ifdef __GLIBC__
	// glibc raises its mmap threshold to the largest block freed so far (up to 32 MB): after the manga reader's first
	// page (2 to 32 MB of RGBA) every later page and frame came out of the heaps (the two decode threads' arenas
	// too), which give memory back only from their top, so what the reader freed stayed with the process: 100 MB
	// after ordinary chapters, 240 MB after long ones, measured on Linux (docs/notes/2026-11-11-reader-memory.md).
	// Pinned, a block of 256 KB or more is a map of its own, gone when freed
	mallopt(M_MMAP_THRESHOLD, 256 << 10);
#endif
	PlayerOptions opts;
	opts.lang = "eng,en";
	opts.stateDir = defaultStateDir();
	std::string file, font;
	std::vector<std::string> steps;	// --press: button names, tap:X:Y, drag:X1:Y1:X2:Y2 (the bottom panel's 640x480)
	bool scripted = false;
	AspectMode aspect = AspectMode::Fit;
	int windowed = -1;	// -1: decide by the video driver
	std::string mangaDir;	// --manga: the strip prototype instead of the player
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto value = [&]() -> std::string {
			if (i + 1 >= argc) { usage(); exit(2); }
			return argv[++i];
		};
		if (a == "--windowed") windowed = 1;
		else if (a == "--fullscreen") windowed = 0;
		else if (a == "--aspect") { if (!parseAspect(value(), aspect)) { usage(); return 2; } }
		else if (a == "--lang") opts.lang = value();
		else if (a == "--state-dir") opts.stateDir = value();
		else if (a == "--manga") mangaDir = value();
		else if (a == "--font") font = value();
		else if (a == "-v") opts.verbose = true;
		else if (a == "--press")
		{
			std::stringstream list(value());
			std::string step;
			while (std::getline(list, step, ','))
			{
				int n = 0;
				float v[4];
				// BUTTON*N: N presses 80 ms apart (a burst, faster than the steps' 0.8 s)
				std::string::size_type star = step.find('*');
				bool burst = star != std::string::npos && buttonFromName(step.substr(0, star)) != Button::None && atoi(step.c_str() + star + 1) > 0;
				// BUTTON~MS: held MS ms, then let go
				std::string::size_type tilde = step.find('~');
				bool held = tilde != std::string::npos && buttonFromName(step.substr(0, tilde)) != Button::None && atoi(step.c_str() + tilde + 1) > 0;
				bool ok = burst || held || buttonFromName(step) != Button::None || step == "info" || step == "page" || step == "update" || step == "web" || step == "F" || step == "rows" || step == "screenshot" || step == "volup" || step == "voldown" || step.rfind("prop:", 0) == 0 || step.rfind("shot:", 0) == 0 || step.rfind("level:", 0) == 0 || step.rfind("set:", 0) == 0 || step.rfind("flip:", 0) == 0 || step.rfind("card:", 0) == 0 || step.rfind("gallery:", 0) == 0 ||
					(sscanf(step.c_str(), "tap:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "dtap:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "hold:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "down:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "move:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "up:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "drag:%f:%f:%f:%f%n", &v[0], &v[1], &v[2], &v[3], &n) == 4 && n == (int)step.size());
				if (!ok) { fprintf(stderr, "unknown step: %s\n", step.c_str()); return 2; }
				steps.push_back(step);
			}
			scripted = true;
		}
		else if (a == "-h" || a == "--help") { usage(); return 0; }
		else if (file.empty()) file = a;
		else { usage(); return 2; }
	}
	// a crash's record in <state>/crash.log, kept across runs (log.txt keeps two)
	bp::crashlog::install(opts.stateDir, BP_VERSION, bp::buildCommit());
	// BananaOS's lid sleep, if a run that kept playing with the lid shut ended without putting it back (the power
	// went): put back before anything else, so the handheld's own setting is never left changed
	LidGuard lidGuard;
	lidGuard.configure(opts.stateDir);
	lidGuard.recover();
	// a socket the other end closed: its write fails (EPIPE) instead of the signal ending the player silently.
	// libsmb2 writes with write(), not send(MSG_NOSIGNAL), so a server dropping a manga folder listing's
	// connection killed the process with no crash.log line (SIGPIPE isn't a crash signal)
	signal(SIGPIPE, SIG_IGN);
	// no file, or a folder: the browser picks them (in that folder)
	std::string startFolder;
	{
		struct stat st;
		if (!file.empty() && stat(file.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) startFolder.swap(file);
	}
	bool browserMode = file.empty();
	// a file named "verbose" in the state folder (home/.config/banana-player on the card): mpv's verbose log, for a
	// problem seen only on the handheld (it can't be given -v from the Ports shelf)
	if (!opts.stateDir.empty() && pathThere(opts.stateDir + "/verbose")) opts.verbose = true;
	// the strip prototype from the Ports shelf (which passes no options): a manga-proto file in the state folder
	// names the folder or CBZ to open (the test only; removed after it)
	if (mangaDir.empty() && !opts.stateDir.empty())
		if (FILE* f = fopen((opts.stateDir + "/manga-proto").c_str(), "r"))
		{
			char line[1024] = {0};
			if (fgets(line, sizeof line, f)) mangaDir = line;
			fclose(f);
			while (!mangaDir.empty() && (mangaDir.back() == '\n' || mangaDir.back() == '\r' || mangaDir.back() == ' ')) mangaDir.pop_back();
		}
	if (font.empty()) font = findFont();
	// the subtitle fonts are the bottom panel's font's neighbours
	if (!font.empty() && font.rfind('/') != std::string::npos) opts.fontsDir = font.substr(0, font.rfind('/'));

	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0 || TTF_Init() < 0)
	{
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		return 1;
	}
	// 18: evMpv itself and the 17 event types below it. One short once handed the manga's two registrations the
	// numbers of evVolKey and evLidIncognito, so a manga worker's wake closed everything as if the lid had shut
	evMpv = SDL_RegisterEvents(18);
	evRender = evMpv + 1;
	evTouch = evMpv + 2;
	evLevel = evMpv + 3;
	evNet = evMpv + 4;
	evFound = evMpv + 5;
	evWeb = evMpv + 6;	// the web dashboard's calls and news (WebCall)
	evDur = evMpv + 7;	// the file rows' durations: some are ready (DurationWorker::take)
	evCover = evMpv + 8;	// the manga library's covers: some are decoded (CoverWorker::take)
	evAmp = evMpv + 9;	// a speaker amplifier didn't start (AmpWatch): the sound output opened again
	evMangaLib = evMpv + 10;	// the Manga app's card library: a listing or chapter files read (MangaLibWorker::take)
	evUnplug = evMpv + 11;	// the headphones went (SleepWatch): a pause, if something plays here
	evPower = evMpv + 12;	// a short press of the power key with the lid shut (PowerKey): play/pause
	evVolKey = evMpv + 13;	// a volume key with the lid shut (VolumeKey): one step, or next / previous on a hold
	evLidIncognito = evMpv + 14;	// the lid closed in incognito with Close all or Quit (SleepWatch): main closes or quits
	evViz = evMpv + 15;	// the native visualizer's new bars (VizTap's worker): the top panel draws a frame
	evIndex = evMpv + 16;	// the cards' media index: a pass finished (Music, Videos and Search read MediaIndex)
	evVideoArt = evMpv + 17;	// the videos' thumbnails, frames and info: some are ready (VideoArtWorker::take)

	// On the handheld (SDL's KMSDRM) each panel is a display and each window fills one; elsewhere, two windows
	if (windowed < 0) windowed = strcmp(SDL_GetCurrentVideoDriver(), "KMSDRM") != 0;
	int displays = SDL_GetNumVideoDisplays();
	// the top panel is the DSI-2 connector, as banana-ui has it, if SDL names its displays by their connectors;
	// BP_TOP_DISPLAY and BP_BOTTOM_DISPLAY (indexes) settle it otherwise
	int topDisplay = 0;
	const char* topEnv = getenv("BP_TOP_CONNECTOR") ? getenv("BP_TOP_CONNECTOR") : getenv("BANANA_TOP");
	std::string topConnector = topEnv ? topEnv : "DSI-2";
	for (int i = 0; i < displays; i++)
	{
		const char* name = SDL_GetDisplayName(i);
		fprintf(stderr, "display %d: %s\n", i, name ? name : "?");
		if (name && strstr(name, topConnector.c_str())) topDisplay = i;
	}
	int bottomDisplay = displays > 1 && topDisplay == 0 ? 1 : 0;
	if (getenv("BP_TOP_DISPLAY")) topDisplay = atoi(getenv("BP_TOP_DISPLAY"));
	if (getenv("BP_BOTTOM_DISPLAY")) bottomDisplay = atoi(getenv("BP_BOTTOM_DISPLAY"));
	if (!windowed && displays < 2) fprintf(stderr, "only %d display: the controls share it\n", displays);

#ifdef BP_GLES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_Window *top, *bottom;
	if (windowed)
	{
		top = SDL_CreateWindow("banana-player: top", 80, 60, Ui::W, Ui::H, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
		// tests: BP_BOTTOM_SIZE=1024x768 draws the bottom panel at that many pixels (the RG DS Plus's panel), not the
		// computer's own scale
		int bw = 0, bh = 0;
		if (const char* sz = getenv("BP_BOTTOM_SIZE")) sscanf(sz, "%dx%d", &bw, &bh);
		bottom = bw > 0 && bh > 0 ? SDL_CreateWindow("banana-player: bottom", 80, 60 + Ui::H + 40, bw, bh, 0)
			: SDL_CreateWindow("banana-player: bottom", 80, 60 + Ui::H + 40, Ui::W, Ui::H, SDL_WINDOW_ALLOW_HIGHDPI);
	}
	else
	{
		top = SDL_CreateWindow("banana-player", SDL_WINDOWPOS_UNDEFINED_DISPLAY(topDisplay), SDL_WINDOWPOS_UNDEFINED_DISPLAY(topDisplay),
			Ui::W, Ui::H, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
		bottom = displays < 2 ? nullptr : SDL_CreateWindow("banana-player controls", SDL_WINDOWPOS_UNDEFINED_DISPLAY(bottomDisplay),
			SDL_WINDOWPOS_UNDEFINED_DISPLAY(bottomDisplay), Ui::W, Ui::H, SDL_WINDOW_FULLSCREEN_DESKTOP);
	}
	if (!top)
	{
		fprintf(stderr, "window: %s\n", SDL_GetError());
		return 1;
	}
	// where the top window went, in the log (a black top panel: which display, what size)
	{
		int tw = 0, th = 0;
		SDL_GetWindowSize(top, &tw, &th);
		fprintf(stderr, "top: window on display %d, %dx%d; bottom on display %d\n", SDL_GetWindowDisplayIndex(top), tw, th,
			bottom ? SDL_GetWindowDisplayIndex(bottom) : -1);
	}
	SDL_GLContext gl = SDL_GL_CreateContext(top);
	if (!gl) fprintf(stderr, "top: GL context: %s\n", SDL_GetError());
	if (!gl)
	{
		fprintf(stderr, "GL context: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_SetSwapInterval(1);
	// --manga: the manga reader's strip prototype, alone (no player, no bottom panel's controls): its own loop
	if (!mangaDir.empty())
	{
#ifdef BP_GLES
		const bool gles = true;
#else
		const bool gles = false;
#endif
		BottomTouch touch;
		touch.start(getenv("BP_TOUCH") ? getenv("BP_TOUCH") : "fe5e0000.i2c", [](Pointer pt) {
			SDL_Event ev{};
			ev.type = evTouch;
			ev.user.data1 = new Pointer(pt);
			SDL_PushEvent(&ev);
		});
		int r = runStripProto(top, gl, gles, mangaDir, evTouch);
		touch.stop();
		SDL_GL_DeleteContext(gl);
		if (bottom) SDL_DestroyWindow(bottom);
		SDL_DestroyWindow(top);
		SDL_Quit();
		return r;
	}

	// the saved network connections (before the player: mpv opens their files) and mpv's way to their files, which
	// outlives the player (mpv calls it till it's gone)
	NetStore netStore;
	netStore.load(opts.stateDir);
	NetStreams netStreams, photoStreams;	// (the photo viewer's own: a photo's open never cancels a video's)
	netStreams.update(netStore);
	photoStreams.update(netStore);
	opts.net = &netStreams;
	int netVersion = netStore.version();

	int rc = 0;
	{
		Player player;
		player.aspect = aspect;
		if (!player.init(opts, onMpvWakeup, nullptr) || !player.initRender(glProc, nullptr, onMpvRender, nullptr))
		{
			fprintf(stderr, "mpv could not start\n");
			return 1;
		}
		// the native visualizer's worker: new bars ready (once until the top panel takes them: VizTap coalesces)
		player.setNativeWake([] {
			SDL_Event e{};
			e.type = evViz;
			SDL_PushEvent(&e);
		});
		// the file lists' lengths kept from the last run: loaded before the prober or the dashboard starts (both probe
		// through the same cache), so a folder opened before is known without opening its files again
		if (!opts.stateDir.empty())
		{
			Uint32 t0 = SDL_GetTicks();
			DurationCache& durations = DurationCache::shared();
			durations.load(opts.stateDir + "/durations.txt");
			fprintf(stderr, "durations: %zu kept from the last run (%u ms)\n", durations.size(), SDL_GetTicks() - t0);
		}
		// The photo viewer: a second mpv, photos only, with no sound output, drawn on the top panel in place of the
		// first while a photo is up. What plays goes on in the first (a video's picture off meanwhile: hideVideo), so a
		// photo never stops or reloads a song or a video. Photos always open here, with something playing or not (one
		// way for them). Its settings are the player's, copied in as a photo opens and kept in step (syncPhotoSettings)
		Player photos;
		{
			PlayerOptions po = opts;
			po.stateDir.clear();	// (photos keep nothing: no resume, no history; settings.conf is the player's)
			po.photosOnly = true;
			po.net = &photoStreams;
			if (!photos.init(po, onMpvWakeup, nullptr) || !photos.initRender(glProc, nullptr, onMpvRender, nullptr))
			{
				fprintf(stderr, "mpv could not start (the photo viewer)\n");
				return 1;
			}
		}
		photos.settings = player.settings;
		std::string photoSettingsSeen = serializeSettings(player.settings);
		// the one the bottom panel works on: the photo viewer while a photo is up, else the player
		auto shown = [&]() -> Player& { return photos.playing() ? photos : player; };

		Ui ui;
		// the Now playing keys' player: main's (songs and videos), also while a photo is shown (the photo viewer is its
		// own Player, and a page of its own)
		ui.mainPlayer = &player;
		bool haveUi = bottom && ui.init(bottom, opts.fontsDir);
		// the theme settings.conf names, before anything is drawn (a change later: the loop's check, below)
		if (theme::setTheme(player.settings.theme))
		{
			fprintf(stderr, "theme: %s -> %s (start)\n", theme::themeId(0), theme::themeId(theme::currentTheme()));
			ui.themeChanged();
		}
		// what closing the lid does, asked once at a start whose settings.conf names none (not in a scripted run:
		// its steps and pictures expect the panel as it is; BP_ASK_LID=1 asks there too)
		if (haveUi && player.settings.lidAsk && (steps.empty() || getenv("BP_ASK_LID"))) ui.askLid();
		// the built-in updater: for the installed package (bin/ beside VERSION), not a build on a computer
		// (BP_PACKAGE_DIR stands in for one in tests)
		// never destroyed: a download under way doesn't hold up quitting (the process ends it; the next start clears
		// what it left)
		Updater& updater = *new Updater;
		{
			std::string pkg;
			if (const char* d = getenv("BP_PACKAGE_DIR")) pkg = d;
			else if (char* b = SDL_GetBasePath())
			{
				std::string base = b;
				SDL_free(b);
				while (!base.empty() && base.back() == '/') base.pop_back();
				if (base.size() > 4 && base.compare(base.size() - 4, 4, "/bin") == 0) pkg = base.substr(0, base.size() - 4);
			}
			if (!pkg.empty() && !pathThere(pkg + "/VERSION")) pkg.clear();
			updater.init(pkg, BP_VERSION);
			ui.updater = &updater;
		}
		// the manga reader's network side: configured now, started the first time the reader is used with it on; never
		// destroyed (its threads leave on stop() without waiting for servers)
		static MangaNet* mangaNet = new MangaNet();
		const Uint32 evMangaFetch = SDL_RegisterEvents(1), evMangaWorker = SDL_RegisterEvents(1);
		{
			std::vector<std::string> places;
			for (auto& r : browseRoots()) places.push_back(r.path);
			mangaNet->configure(evMangaFetch, evMangaWorker, opts.stateDir, places);
		}
		// the reader's Secure DNS setting reaches the threads as a flag, never by their reading the Ui's state
		mangaNet->setSecureDns(player.settings.mangaSecureDns);
		ui.manga->mangaNet = mangaNet;
		// the thread that lists the network folders (its answers come as evNet)
		NetWorker netWorker([](NetListing* l) {
			SDL_Event ev{};
			ev.type = evNet;
			ev.user.data1 = l;
			SDL_PushEvent(&ev);
		});
		ui.netStore = &netStore;
		ui.netWorker = &netWorker;
		// the file rows' durations (evDur when some are ready); never destroyed: its thread may be mid-probe at exit
		DurationWorker* durWorker = new DurationWorker([] {
			SDL_Event ev{};
			ev.type = evDur;
			SDL_PushEvent(&ev);
		});
		ui.durWorker = durWorker;
		// the manga library's covers (evCover when some are decoded); never destroyed, its thread may be inside
		// libavcodec at exit
		CoverWorker* coverWorker = new CoverWorker([] {
			SDL_Event ev{};
			ev.type = evCover;
			SDL_PushEvent(&ev);
		});
		coverWorker->setStateDir(opts.stateDir);	// (Match album art's cache: <state dir>/artbg, the worker's own)
		ui.manga->coverWorker = coverWorker;
		// the videos' thumbnails, frames and info (evVideoArt when some are ready), kept in <state dir>/cache/videos;
		// never destroyed: its thread may be inside libavcodec at exit
		VideoArtWorker* videoArt = new VideoArtWorker([] {
			SDL_Event ev{};
			ev.type = evVideoArt;
			SDL_PushEvent(&ev);
		});
		videoArt->setDir(opts.stateDir + "/cache/videos");
		ui.mediaArt->worker = videoArt;
		// the first start: an empty Videos, Music and Books on the main card where a section has no folder (once)
		bp::makeLibraryFolders(opts.stateDir);
		// the cards' media index (Music, Videos and Search read it; docs/redesign/components.md › "Search and
		// Music: the index"): a pass on this thread at start and one when the cards change, never destroyed (its
		// thread may be mid-probe at exit). Search reads the answer through ui.mediaIndex (Music and Videos later)
		MediaIndexWorker* indexWorker = new MediaIndexWorker(*new MediaIndex, [] {
			SDL_Event ev{};
			ev.type = evIndex;
			SDL_PushEvent(&ev);
		});
		indexWorker->configure(opts.stateDir);
		ui.mediaIndex = &indexWorker->index();	// Search (and Music, Videos) read its answers on this thread
		ui.indexWorker = indexWorker;		// (their reading notice reads the pass's live counters)
		indexWorker->start();
		// the library sections read the worker's index: the generation in their signature() redraws them when a
		// pass lands (nothing is read off the worker's thread)
		ui.music->source = &indexWorker->index();
		ui.videos->source = &indexWorker->index();
		// (Books' shelf is the doc places and the cards' Books/Documents folders: src/mediaindex.h's own walk)
		// the manga store's durable writes (a progress save's fsync on the card); never destroyed, its thread may be
		// inside an fsync at exit
		ui.manga->writer = new DurableWriter();
		// a document's place, written the same way: its tick saves every two seconds while a document is read
		ui.docs->writer = new DurableWriter();
		// the Manga app's card library, listed and its chapter files read off this thread (evMangaLib); never destroyed,
		// its thread may be inside pdfium at exit
		MangaLibWorker* mangaLibWorker = new MangaLibWorker([] {
			SDL_Event ev{};
			ev.type = evMangaLib;
			SDL_PushEvent(&ev);
		});
		ui.manga->libWorker = mangaLibWorker;
		ui.setStateDir(opts.stateDir);
		// Find on the network: the servers that answer, on the main thread as evFound
		Discovery discovery;
		ui.discovery = &discovery;
		ui.onFound = [](const FoundHost& h) {
			SDL_Event ev{};
			ev.type = evFound;
			ev.user.data1 = new FoundHost(h);
			SDL_PushEvent(&ev);
		};
		// looked for once, a little after the start, in the background (Settings › Updates › Check automatically)
		Uint32 updateCheckAt = player.settings.checkUpdates ? SDL_GetTicks() + 4000 : 0;
		if (bottom && !haveUi) fprintf(stderr, "the controls could not start: %s\n", SDL_GetError());

#ifdef BP_WEB
		// The web dashboard: made the first time it's turned on (no thread, socket or object before), never
		// destroyed (stop() ends what it can in a second; a slow client's write ends with the process)
		WebServer* webServer = nullptr;
		auto webPower = [&](bool on) {
			if (!on)
			{
				// the pages say why it went (`event: bye` "off"), not that the handheld can't be reached
				if (webServer) webServer->stop("off");
				return;
			}
			if (!webServer)
			{
				webServer = new WebServer;
				ui.web = webServer;
				webForWatch = webServer;
			}
			if (webServer->running()) return;
			WebConfig wc;
			wc.stateDir = opts.stateDir;
			// the browser's places, the whole card ("/", a computer's) left out
			for (auto& r : browseRoots())
				if (r.path != "/") wc.roots.push_back({r.label, r.path});
			wc.eventType = evWeb;
			WebServer* ws = webServer;
			wc.snapshot = [ws](Player& p, Ui& u) { return webSnapshotJson(webSnapshot(*ws, p, u), u); };
			// tests: BP_WEB_FAIL=1 stands in for a server that couldn't start
			if (!getenv("BP_WEB_FAIL")) webServer->start(wc);
		};
		ui.webPower = webPower;
		// the Session (docs/design/session.md): step (b), the output and the tracks are its own (run here); the rest is
		// today's code, which it follows, logging where they part. A browser tab can have a file the web serves in a
		// format a browser plays, while that tab is there (toPage's rule)
		SessionHost& shadow = SessionHost::get();
		shadow.attach(&player, &ui);
		ui.sleep->player = &player;	// the sleep timer pauses what plays (never the photo viewer)
#ifdef BP_WEB
		shadow.browserCan = [&webServer](const std::string& path, const std::string& tab) {
			return webServer && webServer->running() && !webServer->servedVirtual(path).empty() && playableByName(path) != Playable::No &&
				(tab.empty() || webServer->tabConnected(tab));
		};
#endif
		// tests: BP_WEB_FORCE=1 starts it whatever the settings say (as if it were on, not written)
		if (getenv("BP_WEB_FORCE") && atoi(getenv("BP_WEB_FORCE"))) player.settings.webDashboard = true;
		if (player.settings.webDashboard) webPower(true);
		WebSnapshot webLast;
		bool webForce = true;	// publish at the next turn whatever changed (a browser came, the handheld woke)
		Uint32 webPublishedAt = 0;
		int webRecentSent = -1;	// the Recent serial the pages were last told
		std::string elsewhereShown;	// the top panel's "Playing in Chrome on Mac" as drawn ("" none)
#endif
		Uint32 bottomId = bottom ? SDL_GetWindowID(bottom) : 0;

		// the lid closed, the handheld slept: paused at once (from the watch's thread), and the place kept
		static std::atomic<bool> slept{false};
		// When the lid closes: Keep playing (set by the main loop: the watch's thread reads it)
		static std::atomic<bool> keepOnLid{false};
		keepOnLid = player.settings.lidKeepPlaying;
		// Incognito and its Lid or sleep choice, for the watch's thread: Close all and Quit are decided on the main
		// thread (it owns the Player and the Ui), so the thread only sends the event; if the handheld sleeps before
		// that, main finishes on waking, before the first picture is drawn
		static std::atomic<bool> incognitoOn{false};
		static std::atomic<int> lidIncognitoMode{0};
		IdleSleep idleSleep;
		// what keeps the handheld awake now (idlesleep.h): the idle sleep's holds, and the sleep timer's (only the jobs)
		auto idleHoldsNow = [&](bool charging) {
			IdleHolds h;
			h.charging = charging;
			h.playing = player.playing() && !player.paused;
			h.fileJob = ui.fileJobActive();
			{
				auto& q = mangaNet->queue();
				bool running = false;
				for (const auto& it : q.items()) if (it.state == DlItem::Running) running = true;
				h.mangaDownloads = running || q.next() != nullptr;
				if (mangaNet->started())
				{
					auto js = mangaNet->javaExt().state();
					h.mangaEngine = js == JavaExtHost::State::Starting || js == JavaExtHost::State::Downloading;
				}
			}
			Updater::State us = updater.state();
			h.updating = us == Updater::State::Downloading;
			// (Settings: Stay awake for copies and downloads: off, they no longer hold the sleep)
			if (!player.settings.stayAwakeJobs) h.fileJob = h.mangaDownloads = h.mangaEngine = h.updating = false;
#ifdef BP_WEB
			h.browserOut = webServer && webServer->running() && ui.pageIsOut() && ui.pagePlaying();
			h.webActive = webServer && webServer->running() && webServer->activeWithin(120000);
#endif
			return h;
		};
		SleepWatch sleepWatch;
		// Every incognito trigger asks again here, on the main thread, with the state of this moment: the watch's
		// thread and the power key's hold a copy that is a loop old, and a stale on must never close everything.
		// An ignored one says so once (a flood would hide the rest of the log); the next trigger that counts
		// clears it again
		bool incognitoOffSaid = false;
		auto incognitoNow = [&](const char* source) {
			if (ui.incognitoOn())
			{
				incognitoOffSaid = false;
				return true;
			}
			if (!incognitoOffSaid) fprintf(stderr, "incognito: %s ignored: incognito is off\n", source);
			incognitoOffSaid = true;
			return false;
		};
		// what a lid close does when incognito isn't in charge: Keep playing, or paused here (the watch's thread does
		// it as the lid shuts, before bananad stops the process; main does it if incognito turned off meanwhile)
		auto lidPause = [&](bool lid) {
			// Keep playing: the lid shut is only the panels going dark (BananaOS's lid sleep is off while it plays,
			// LidGuard). If the handheld sleeps after all, the SIGCONT after it pauses as always
			if (lid && keepOnLid.load())
			{
				fprintf(stderr, "lid: closed, keeps playing\n");
				return;
			}
			player.pauseNow();
			photos.pauseNow();
			slept = true;
#ifdef BP_WEB
			// the pages told now, from this thread (notify is thread-safe): bananad stops the process in a moment, and
			// the main loop may not come round before it. The streams stay open for the wake
			if (WebServer* ws = webForWatch.load(); lid && ws && ws->running()) ws->notify("bye", "{\"reason\":\"sleep\"}");
#else
			(void)lid;
#endif
			onMpvWakeup(nullptr);
		};
		sleepWatch.start([&](bool lid) {
			// Close all / Quit win over Keep playing: main does it (it owns the Player and the Ui), and it checks
			// incognito again before anything closes. Only a real open -> closed transition (the switch, read by
			// SleepWatch) is a close; a SIGCONT is a wake and pauses as it always did
			IncognitoLidAction action = incognitoLidAction(lid, incognitoOn.load(), lidIncognitoMode.load());
			if (action != IncognitoLidAction::NotInCharge)
			{
				SDL_Event ev{};
				ev.type = evLidIncognito;
				ev.user.code = lidIncognitoMode.load();
				SDL_PushEvent(&ev);
				fprintf(stderr, "incognito: lid switch: closed: %s (decided on the main thread)\n",
					action == IncognitoLidAction::Quit ? "quit" : "close all");
				return;
			}
			lidPause(lid);
		}, [] {
#ifdef BP_WEB
			// back (or never gone: lid sleep off): the pages' "asleep" undone; the state follows (woke: webForce)
			if (WebServer* ws = webForWatch.load(); ws && ws->running()) ws->notify("awake", "{}");
#endif
		}, [](const std::string& what) {
			// the main thread decides: it pauses only if something plays here
			SDL_Event ev{};
			ev.type = evUnplug;
			ev.user.data1 = new std::string(what);
			SDL_PushEvent(&ev);
		});
		// the power key with the lid shut (powerkey.h): its thread takes the key while main's tick says so, and a short
		// press comes back as evPower; a 2 s hold is a sleep, asked for on that thread
		PowerKey powerKey;
		powerKey.start(evPower);
		// the volume keys with the lid shut (volumekey.h): taken by the same three conditions; a press is one volume
		// step on the main thread through Volume, a 0.6 s hold is next / previous, and BananaOS never moves the volume
		VolumeKey volumeKey;
		volumeKey.start(evVolKey);

		// the bottom touchscreen, read on its own thread; where there's none, SDL's mouse and touch stand in
		BottomTouch bottomTouch;
		bool ownTouch = haveUi && bottomTouch.start(getenv("BP_TOUCH") ? getenv("BP_TOUCH") : getenv("BANANA_TOUCH") ? getenv("BANANA_TOUCH") : "fe5e0000.i2c", [](Pointer pt) {
			SDL_Event ev{};
			ev.type = evTouch;
			ev.user.data1 = new Pointer(pt);
			SDL_PushEvent(&ev);
		});
		SDL_FingerID finger = -1;	// SDL's touches: the one finger that drives

		// the videos beside it are the playlist: L and R, and the next one plays when this one ends
		bool running = true;
		// the videos beside it are the playlist; with no file, the browser first
		ui.browserMode = browserMode;
		{
			// the top panel's pixels: the idle icon and the level card are drawn at that size, whole pixels
			int tw = 0, th = 0;
			SDL_GL_GetDrawableSize(top, &tw, &th);
			player.setTopSize(tw, th);
			photos.setTopSize(tw, th);
		}
		if (browserMode)
		{
			ui.startBrowser(player, startFolder);
			player.showIdle(true);
			player.wakeOutput();
		}
		else
		{
			int index;
			std::vector<std::string> list = siblingVideos(file, index);
			if (isPhotoFile(file)) photos.open(list, index);
			else
			{
				player.open(list, index);
				shadow.open(list, index, 0, "", "hh", nullptr, 0);
			}
		}
		// the top panel's own state (topscreen.h): made before the lambdas below, which read topDark
		TopScreen ts(top, gl, opts.fontsDir);
		bool& topDark = ts.topDark;	// the top panel cleared while there's no video
		Uint32 levelUntil = 0;
		// started with a file: the browser from now on (B, Stop: its folder), as if started with none
		auto toBrowserMode = [&] {
			if (browserMode) return;
			browserMode = true;
			ui.browserMode = true;
		};
		// Stopping what plays (the player's Stop, the menus', the web's, the end of the list): the place kept; the
		// browser on the file only if the player was what the bottom panel showed (behind the browser it stays where
		// it is). The session's Stop comes first (one Stop, everywhere: a page that plays it stops too, staying the
		// selected output); this is the handheld's side of it
		enum class StopWhy { Handheld, Web, End };
		auto stopPlaying = [&](StopWhy why) {
			if (!player.playing()) return;
			// started with a file: its end still quits (as it always did)
			if (!browserMode && why == StopWhy::End) { running = false; return; }
			// (a photo up over it: the photo stays, and closing it goes where it would have; the Manga guard's Stop
			// keeps the page it stopped from)
			bool keepPage = haveUi && ui.takeStopKeepsPage();
			bool shown = !keepPage && (!haveUi || (ui.playerShown(player) && !photos.playing()));
			toBrowserMode();
			// what it played from, read before stop() forgets it: a playlist goes back to that playlist
			std::string last = player.currentPath();
			PlaySource from = player.source();
			int index = player.index();
			if (haveUi)
			{
				ui.flushPlaylist(player);
				ui.stopped();
			}
			player.stop();
			topDark = false;
			if (haveUi && why != StopWhy::End) ui.sleep->stopped();	// (Stop, anywhere: the sleep timer with it)
			fprintf(stderr, "stop: %s (%s)\n", why == StopWhy::End ? "the end of the list" : why == StopWhy::Web ? "the web" : "Stop",
				shown ? "back to the browser" : "behind the browser");
			if (shown && haveUi) ui.backFromPlayer(player, last, from, index);
			else if (why == StopWhy::End)
			{
				// browsing meanwhile (or a photo up): the bar folds away, the list stays where it is; the top panel says why
				(photos.playing() ? photos : player).osd("Finished", 2000);
				levelUntil = SDL_GetTicks() + 2000;
			}
		};
		// The photo viewer closed (B, Close, a pick that isn't a photo): what plays under it shows again where it has got
		// to (it never stopped). screens: back where the photo was opened from (the player of what plays, or the
		// browser's page as it was), else the caller changes them
		bool photoOverPlayer = false;	// the photo was opened over the player of what plays (the web's pick)
		auto closePhoto = [&](bool screens) {
			if (!photos.playing()) return;
			std::string was = photos.currentPath();
			photos.stop();
			player.hideVideo(false);
			renderPending = true;
			fprintf(stderr, "photo: closed%s\n", player.playing() ? (", " + player.currentPath().substr(player.currentPath().rfind('/') + 1) + " shown again").c_str() : "");
			if (!screens || !haveUi) return;
			if (photoOverPlayer && player.playing()) ui.showPlayer();
			else ui.backFromPlayer(player, was);
		};
		// just before a sleep the player asks for (the idle sleep, the sleep timer's): incognito decides first, and only
		// if it is on at this moment
		auto incognitoBeforeSleep = [&](const char* source) {
			if (!incognitoNow(source)) return;
			int mode = player.settings.lidIncognito;
			if (mode == LID_INCOGNITO_CLOSE_ALL)
			{
				fprintf(stderr, "incognito: %s: close all, then sleeping\n", source);
				closePhoto(false);
				ui.incognitoCloseAllNow(player);
			}
			else if (mode == LID_INCOGNITO_QUIT) { fprintf(stderr, "incognito: %s: quitting\n", source); running = false; }
		};
		// B, "<" or Back on the player (a press() or pointer() that says leave): the screen only, what plays goes on
		// behind the browser, back where the player was opened from (Ui::backFromPlayer). A photo (and a file that
		// didn't open) closes instead. With nothing playing (Quit in B's dialog at Home), out
		auto leave = [&] {
			if (photos.playing())
			{
				toBrowserMode();
				closePhoto(true);
				return;
			}
			if (!player.playing()) { running = false; return; }
			if (!player.error.empty())
			{
				toBrowserMode();
				stopPlaying(StopWhy::Handheld);
				return;
			}
			if (!haveUi) { running = false; return; }
			toBrowserMode();
			ui.flushPlaylist(player);
			fprintf(stderr, "leave: the player left, %s plays on\n", player.currentPath().substr(player.currentPath().rfind('/') + 1).c_str());
			ui.backFromPlayer(player, player.currentPath(), player.source(), player.index());
		};
		// the panel's own state lives in TopScreen (topscreen.h): these name what the rest of main reads
		MangaReader& reader = ts.reader;
		TextReader& textReader = ts.textReader;
		ReaderFetch& readerFetch = ts.readerFetch;	// a URL chapter's pages, kept coming while reader is open
		// MangaNet routes a page's answer to the reader before the Ui can take it (the Ui's own answers are untouched)
		mangaNet->setFetchOwner([&readerFetch](MangaFetch::Result& r) { return readerFetch.own(r); });
		// a reader open, its pages on the top panel; not while the player's own pages show (they show what plays)
		auto readerOnTop = [&] { return ts.onTop(ui); };
		// The level card (levelcard.h: one picture, the same on every screen) on whatever the top panel shows: mpv's
		// (what plays, a photo, the idle screen), and over a reader's pages when a reader draws the panel (mpv's
		// would be under them). Every volume, brightness and zoom card comes through here. "Zoom": the reader's, on the reader
		// only (the line alone)
		auto showLevelCard = [&](const std::string& label, int percent, int ms) {
			bool bar = label != "Zoom";
			if (bar) shown().showLevel(label, percent);
			if (haveUi && (readerOnTop() || ts.paneUp))
				ts.cardOnReader(label, percent, bar, bar ? levelTop(label, player.settings.volumeMax) : 0, ms);
			if (!bar) return;
			levelUntil = SDL_GetTicks() + ms;
			renderPending = true;
		};
		// What plays under a photo: a song's graph makes frames still (it carries the sound), and mpv waits for each to
		// be taken (else its picture queue stalls, 200 ms a frame). They're taken by a tiny target of their own with
		// rendering skipped (nothing drawn, no GL time), so the photo or the comic isn't drawn again 30 times a second
		// and the bottom panel keeps the GPU. A video decodes on meanwhile (hideVideo), its frames taken the same way
		auto drainUnder = [&] { ts.drainUnder(player, photos, readerOnTop()); };

		// the top panel's pixels (RGBA, top row first), the frame drawn again for it: screenshots
		auto captureTop = [&]() -> SDL_Surface* { return ts.capture(shown(), readerOnTop(), ui.aboutQrUp()); };
		// F + L2 (as in BananaOS's emulators) or, on a computer, F12: both panels, the top over the bottom, as a PNG
		// in the card's screenshots folder
		auto screenshot = [&] {
			SDL_Surface* t = captureTop();
			SDL_Surface* bt = haveUi ? ui.capture(shown()) : nullptr;
			std::string dir = getenv("BP_SCREENSHOTS") ? getenv("BP_SCREENSHOTS")
							: pathThere("/data/screenshots") ? "/data/screenshots" : getenv("HOME") ? getenv("HOME") : ".";
			char name[64];
			time_t now = time(nullptr);
			strftime(name, sizeof name, "/Banana Player %Y-%m-%d %H-%M-%S.png", localtime(&now));
			bool ok = false;
			if (t)
			{
				int w = std::max(t->w, bt ? bt->w : 0), h = t->h + (bt ? bt->h : 0);
				std::vector<unsigned char> rgba((size_t)w * h * 4, 0);
				auto put = [&](SDL_Surface* s, int y0) {
					for (int y = 0; y < s->h; y++)
						memcpy(&rgba[((size_t)(y0 + y) * w + (w - s->w) / 2) * 4], (unsigned char*)s->pixels + (size_t)y * s->pitch, (size_t)s->w * 4);
				};
				put(t, 0);
				if (bt) put(bt, t->h);
				ok = savePng(dir + name, w, h, rgba.data());
			}
			if (t) SDL_FreeSurface(t);
			if (bt) SDL_FreeSurface(bt);
			shown().osd(ok ? "Screenshot saved" : "Screenshot couldn't be saved");
			fprintf(stderr, "screenshot %s%s: %s\n", dir.c_str(), name, ok ? "saved" : "failed");
		};
		bool fHeld = false, fCombo = false, l2Down = false, r2Down = false;

		// the volume and brightness keys (bananad's): their level on a card over the top panel for 1.5 s
		LevelWatch levelWatch;
		levelWatch.start([](const std::string& what, int pct) {
			SDL_Event ev{};
			ev.type = evLevel;
			ev.user.code = pct;
			ev.user.data1 = new std::string(what);
			SDL_PushEvent(&ev);
		});
		// a speaker amplifier that didn't start (the RG DS Plus's aw88166s): the output opened again a moment later (both
		// chips' failures come together: one reopen for both), 3 times a minute at most
		AmpWatch ampWatch;
		bool ampWatching = ampWatch.start([](const std::string& chip) {
			SDL_Event ev{};
			ev.type = evAmp;
			ev.user.data1 = new std::string(chip);
			SDL_PushEvent(&ev);
		});
		if (ampWatching) fprintf(stderr, "audio: watching the speaker amplifiers (kernel log)\n");
		Uint32 ampReloadAt = 0;
		int volumeOverride = 0;
		Uint32 browserVolUntil = 0;	// the volume keys moving a browser tab's volume: bananad's report of them ignored
		int handheldVolKept = -1;	// (and the handheld's own put back to this)	// the volume keys past 100%: what bananad's own report of them shows instead, a moment
		Uint32 volumeOverrideUntil = 0;
		std::vector<Uint32> ampReloads;
		bool& logNextTop = ts.logNextTop;

		Button held = Button::None;	// a pad's button held down: repeated as a key is
		int stickY = 0, stickX = 0;	// the left stick's LEFTY (-32768 up … 32767 down): the readers scroll while it is held
		Uint32 heldNext = 0, savedAt = SDL_GetTicks();
		// Both panels drawn again and again for a while after the start and a wake, and every few seconds after: on
		// the handheld (SDL's KMSDRM) the first frames presented don't reach the panels, and a page that doesn't
		// change would otherwise stay black till something did
		// (8 s at the start, not 2: the first launch after the handheld starts is slow, every shader compiled afresh, and
		// mpv's first picture of the top panel can come after 2 s, then nothing drew it again for a while: black)
		Uint32 freshUntil = SDL_GetTicks() + 8000, refreshedAt = 0;
		std::string crashContext;
		const char* crashTest = getenv("BP_CRASH_TEST");
		Uint32 crashAt = SDL_GetTicks() + 1500;
		while (running)
		{
			// what plays, for a crash's record
			if (player.currentPath() != crashContext)
			{
				crashContext = player.currentPath();
				bp::crashlog::setContext(crashContext.c_str());
			}
			if (crashTest && SDL_GetTicks() >= crashAt) bp::crashlog::testCrash(crashTest);
			// the power and volume keys with the lid shut: each taken only while its own setting says so (Play / pause;
			// Volume and skip), Keep playing is on, the lid is shut and something is loaded here (playing or paused, not
			// stopped). The threads let go the moment any of them ends; the beat says this loop still runs (3 s without
			// one and it lets go by itself)
			{
				bool keep = player.settings.lidKeepPlaying;
				bool shut = sleepWatch.lidClosed();
				const char* why = !keep ? "Keep playing is off" : !shut ? "the lid opened" : "nothing is loaded";
				bool powerMine = player.settings.lidPower == LID_POWER_PLAY, volumeMine = player.settings.lidVolume == LID_VOLUME_SKIP;
				bool loaded = keep && shut && player.playing();
				powerKey.tick(loaded && powerMine, powerMine ? why : "the setting says Sleep",
					ui.incognitoOn() ? player.settings.lidIncognito : LID_INCOGNITO_NOTHING);
				volumeKey.tick(loaded && volumeMine, volumeMine ? why : "the setting says Volume only");
				incognitoOn = ui.incognitoOn();
				lidIncognitoMode = player.settings.lidIncognito;
			}
			// Under the lid nothing but the volume keys, Power and the shoulder keys (by their setting) does anything:
			// the face keys, the D-pad, START, SELECT, F, the stick and the touch screen are ignored while the lid is
			// shut, Keep playing is on and something is loaded. A shoulder held 0.6 s (Hold) acts once; As usual acts
			// as it goes down; Ignored does nothing. They act on what plays, never on the page left open.
			// (asked per event: a lid that opened while the loop waited for it must let that press act, as the first
			// press after opening does)
			auto lidKeysNow = [&] {
				return player.settings.lidKeepPlaying && sleepWatch.lidClosed() && player.playing() && !player.playingPhoto();
			};
			// under the lid the stick is one of the keys ignored (and its held value must not keep a reader moving
			// with both panels dark: no event comes to stop it)
			if (lidKeysNow()) stickY = stickX = 0;
			Uint32 lidShoulderAt[4] = {0, 0, 0, 0};
			bool lidShoulderDone[4] = {false, false, false, false}, lidShoulderStuck[4] = {false, false, false, false};
			auto lidStep = [&](int dir) {
				// the same intents L / R send (the Session's Step, so a browser that has the sound follows)
				const char* name = dir < 0 ? "L" : "R";
				if (!player.settings.buttonHotkeys) { fprintf(stderr, "lid keys: %s ignored (Button hotkeys off)\n", name); return; }
				if (SessionHost::get().stepsHere(player)) SessionHost::get().step(dir);
				else
				{
					bool ok = dir < 0 ? player.back() : player.next();
					if (!ok) fprintf(stderr, "lid keys: %s: nothing to %s to\n", name, dir < 0 ? "go back" : "go");
				}
				fprintf(stderr, "lid keys: %s: %s\n", name, dir < 0 ? "previous" : "next");
			};
			auto lidShoulderAct = [&](int i) {
				if (i < 2) { lidStep(i == 0 ? -1 : 1); return; }
				const char* name = i == 2 ? "L2" : "R2";
				if (!player.settings.buttonHotkeys) { fprintf(stderr, "lid keys: %s ignored (Button hotkeys off)\n", name); return; }
				ui.chapterStepForLid(i == 2 ? -1 : 1, player);
				fprintf(stderr, "lid keys: %s: %s chapter\n", name, i == 2 ? "previous" : "next");
			};
			auto lidShoulderDown = [&](int i) {
				if (!lidKeysNow()) return;
				lidShoulderAt[i] = SDL_GetTicks() ? SDL_GetTicks() : 1;
				lidShoulderDone[i] = lidShoulderStuck[i] = false;
				const char* name = i == 0 ? "L" : i == 1 ? "R" : i == 2 ? "L2" : "R2";
				fprintf(stderr, "lid keys: shoulder %s down (setting %d)\n", name, player.settings.lidShoulder);
				if (player.settings.lidShoulder == LID_SHOULDER_AS_USUAL) { lidShoulderAct(i); lidShoulderDone[i] = true; }
			};
			auto lidShoulderUp = [&](int i) { lidShoulderAt[i] = 0; lidShoulderDone[i] = lidShoulderStuck[i] = false; };
			SDL_Event e;
			int waitMs = (reader.isOpen() && reader.busy()) || (textReader.isOpen() && textReader.busy()) ? 1 : reader.isOpen() && reader.seekPending() ? 16 : reader.spinning() ? MangaReader::SPIN_MS / 2 : ui.animating() || ui.dimEasing() || player.marqueeMoving() ? 16 : ui.typing() ? 50 : held != Button::None || player.loading() ? 20 : SDL_GetTicks() < freshUntil || levelUntil ? 50 : ui.markMoving() ? 80 : 250;
			// the native visualizer's next frame (its own rate: the tap's levels are taken as it's drawn)
			{
				const int nw = ts.nativeWaitMs(shown());
				if (nw >= 0) waitMs = std::min(waitMs, nw);
			}
			// the sleep timer's End of this chapter: its last moments looked at closely
			if (const int sw = haveUi ? ui.sleep->waitMs() : -1; sw >= 0) waitMs = std::min(waitMs, sw);
			// a list gliding on the bottom panel: its next frame 16 ms after the last began
			{
				const int gw = haveUi ? ui.glideWaitMs() : -1;
				if (gw >= 0) waitMs = std::min(waitMs, gw);
			}
			bool got = SDL_WaitEventTimeout(&e, waitMs);
			// the pass starts after the wait (the wait isn't a frame's work) and ends after both panels are drawn
			FrameWatch::begin(SDL_GetTicks());
			Uint32 eventsAt = SDL_GetTicks();
			while (got)
			{
				// a button, a touch or the stick moved (past drift): the idle sleep's clock starts again
				if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_KEYDOWN || e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYHATMOTION ||
					e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_FINGERDOWN || e.type == SDL_FINGERMOTION || e.type == SDL_MOUSEWHEEL ||
					(e.type == SDL_CONTROLLERAXISMOTION && std::abs((int)e.caxis.value) > 12000))
					idleSleep.input(SDL_GetTicks());
				// every button in the log (home/.config/banana-player/log.txt on the card), mapped or not: what the
				// handheld's keys send, found by pressing them
				if (e.type == SDL_CONTROLLERBUTTONDOWN)
				{
					const char* name = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button);
					fprintf(stderr, "input: pad button %d (%s)\n", e.cbutton.button, name ? name : "?");
				}
				else if (e.type == SDL_JOYBUTTONDOWN) fprintf(stderr, "input: joystick %d button %d\n", e.jbutton.which, e.jbutton.button);
				else if (e.type == SDL_JOYHATMOTION && e.jhat.value) fprintf(stderr, "input: joystick %d hat %d value %d\n", e.jhat.which, e.jhat.hat, e.jhat.value);
				else if (e.type == SDL_KEYDOWN && !e.key.repeat)
					fprintf(stderr, "input: key %s (scancode %d)\n", SDL_GetKeyName(e.key.keysym.sym), e.key.keysym.scancode);
				// the lid shut: only the shoulders (by the setting) and the volume keys' and Power's own devices answer.
				// Everything else is under the lid: the face keys, the D-pad, START, SELECT, F, the stick and the touch
				bool lidInput = e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP || e.type == SDL_CONTROLLERAXISMOTION ||
					e.type == SDL_KEYDOWN || e.type == SDL_KEYUP || e.type == evTouch ||
					(e.type >= SDL_JOYAXISMOTION && e.type <= SDL_JOYHATMOTION) || (e.type >= SDL_MOUSEMOTION && e.type <= SDL_MOUSEWHEEL) ||
					e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP || e.type == SDL_FINGERMOTION;
				if (lidKeysNow() && lidInput)
				{
					Button b = e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_KEYDOWN ? buttonFromEvent(e)
						: e.type == SDL_CONTROLLERBUTTONUP || e.type == SDL_KEYUP ? buttonReleased(e) : Button::None;
					int si = b == Button::L ? 0 : b == Button::R ? 1 : b == Button::L2 ? 2 : b == Button::R2 ? 3 : -1;
					if (si >= 0) { if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_KEYDOWN) lidShoulderDown(si); else lidShoulderUp(si); }
					else if (e.type == SDL_CONTROLLERAXISMOTION && (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
					{
						// the triggers are axes: the same half-way/quarter-way line as with the lid open
						bool left = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
						bool& was = left ? l2Down : r2Down;
						bool down = was ? e.caxis.value > 8000 : e.caxis.value > 16000;
						if (down != was)
						{
							was = down;
							if (down) lidShoulderDown(left ? 2 : 3);
							else lidShoulderUp(left ? 2 : 3);
						}
					}
					// (the face keys, D-pad, START, SELECT, F, the stick and the touch: swallowed)
					got = SDL_PollEvent(&e);
					continue;
				}
				// the volume keys past 100% (Max volume 120 or 150): bananad sets the handheld's level and stops at 100; the
				// player's own gain goes on from there, 5 at a press. Down takes the gain off first (and puts back the 100
				// bananad's press just took away). F + volume is bananad's brightness: left alone
				bool volKey = e.type == SDL_KEYDOWN && !fHeld && haveUi &&
					(e.key.keysym.scancode == SDL_SCANCODE_VOLUMEUP || e.key.keysym.scancode == SDL_SCANCODE_VOLUMEDOWN);
#ifdef BP_WEB
				// a browser tab plays the sound: the keys are its volume (5 a press, told to it), and the handheld's own,
				// which bananad moves at the same press, is put back as it was (two volumes, each its own)
				if (volKey && webServer)
				{
					const Session& ss = SessionHost::get().session();
					if (ss.out.kind == OutKind::Browser && ss.index >= 0 && !ss.out.tab.empty())
					{
						bool up = e.key.keysym.scancode == SDL_SCANCODE_VOLUMEUP;
						int bv = std::clamp((ui.browserVol >= 0 ? ui.browserVol : 100) + (up ? 5 : -5), 0, 100);
						ui.browserVol = bv;
						webServer->notify("volume", "{\"tab\":\"" + ss.out.tab + "\",\"v\":" + std::to_string(bv) + "}");
						if (!(SDL_GetTicks() < browserVolUntil)) handheldVolKept = ui.volumeLevel();
						browserVolUntil = SDL_GetTicks() + 1500;
						showLevelCard("Browser volume", bv, 1500);
						fprintf(stderr, "level: the browser's volume %d (its tab told; the handheld's kept at %d)\n", bv, handheldVolKept);
						volKey = false;
					}
				}
#endif
				if (volKey && !player.playingPhoto())
				{
					bool up = e.key.keysym.scancode == SDL_SCANCODE_VOLUMEUP;
					int top = std::max(100, player.settings.volumeMax), now = player.boost;
					int to = -1;
					if (up && top > 100 && ui.volumeLevel() >= 100 && now < top) to = std::min(top, now + 5);
					else if (!up && now > 100) to = now - 5;
					if (to >= 0)
					{
						ui.setVolumeTotal(player, to, !up);
						volumeOverride = to;
						volumeOverrideUntil = SDL_GetTicks() + 1200;
						levelUntil = SDL_GetTicks() + 1500;
						renderPending = true;
						fprintf(stderr, "level: volume %d (the player's gain over the handheld's 100%%)\n", to);
					}
					// a press the player doesn't take (Down at 100: the handheld's own volume goes to 95) ends the
					// override at once: still up from the press that brought 110 to 100, it showed bananad's 95 as 100
					else volumeOverrideUntil = 0;
				}
				// F + any other button: no quick settings as F lets go (the button does what it does)
				if (e.type == SDL_CONTROLLERBUTTONDOWN && fHeld && e.cbutton.button != SDL_CONTROLLER_BUTTON_GUIDE) fCombo = true;
				if (e.type == SDL_QUIT) running = false;
				else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
				else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_EXPOSED || e.window.event == SDL_WINDOWEVENT_SHOWN))
				{
					renderPending = true;
					freshUntil = std::max(freshUntil, SDL_GetTicks() + 500);
				}
				else if (e.type == evMpv) mpvPending = false;
				else if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED)
				{
					handlePadHotplug(e);
					if (e.type == SDL_CONTROLLERDEVICEREMOVED) stickY = stickX = 0;	// a stick held as the pad goes must not keep scrolling
				}
				else if (e.type == SDL_CONTROLLERAXISMOTION && (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY || e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX))
				{
					// the readers' analog scroll (issue 16): the value is read here and turned into pixels by the
					// readers each frame, so their speed doesn't depend on how often the pad sends events
					(e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY ? stickY : stickX) = e.caxis.value;
				}
				else if (scripted && (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP || e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP || e.type == evTouch ||
							 (e.type >= SDL_MOUSEMOTION && e.type <= SDL_MOUSEWHEEL) || (e.type >= SDL_FINGERDOWN && e.type <= SDL_FINGERMOTION) || e.type == SDL_CONTROLLERAXISMOTION))
				{
					// a test run takes only its own steps: the windows can take the keyboard's focus while someone
					// types elsewhere
					if (e.type == evTouch) delete (Pointer*)e.user.data1;
				}
				else if ((e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) && e.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE)
				{
					// F: with L2 a screenshot, with START out; alone (pressed and let go with nothing between, not
					// even bananad's F + volume for brightness) the quick settings
					bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
					if (down) fCombo = false;
					else if (fHeld && !fCombo && haveUi) ui.toggleQuick(shown());
					fHeld = down;
				}
				else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F1 && !e.key.repeat && haveUi) ui.toggleQuick(shown());
				else if (e.type == SDL_CONTROLLERAXISMOTION && (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT))
				{
					// L2 and R2 are triggers to SDL: down past half way, up again only below a quarter, so a trigger
					// hovering at the line doesn't press over and over
					bool left = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
					bool& was = left ? l2Down : r2Down;
					bool down = was ? e.caxis.value > 8000 : e.caxis.value > 16000;
					if (down != was)
					{
						was = down;
						fprintf(stderr, "input: trigger %s %s (value %d)\n", left ? "L2" : "R2", down ? "down" : "up", e.caxis.value);
						if (down && fHeld)
						{
							fCombo = true;
							if (left) screenshot();	// F + L2; F + R2 does nothing
						}
						else if (down && haveUi && ui.press(left ? Button::L2 : Button::R2, shown())) leave();
					}
				}
				else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F12 && !e.key.repeat) screenshot();
				else if (e.type == SDL_CONTROLLERBUTTONUP || e.type == SDL_KEYUP)
				{
					if (e.type == SDL_CONTROLLERBUTTONUP) held = Button::None;
					if (Button b = buttonReleased(e); b != Button::None && haveUi) ui.release(b, shown());
				}
				// F + START: out at once, from anywhere (the place is kept as the loop ends)
				else if (e.type == SDL_CONTROLLERBUTTONDOWN && fHeld && buttonFromEvent(e) == Button::Start) running = false;
				else if (Button b = buttonFromEvent(e); b != Button::None)
				{
					if (e.type == SDL_CONTROLLERBUTTONDOWN && ui.repeats(b, shown()))
					{
						held = b;
						heldNext = SDL_GetTicks() + 400;
					}
					if (ui.press(b, shown())) leave();
				}
				else if (e.type == evUnplug)
				{
					std::unique_ptr<std::string> what((std::string*)e.user.data1);
					// the sound would go on out of the speakers: paused as A pauses (the session's Pause, then mpv).
					// Plugging back in never plays
					if (player.playing() && !player.paused && !SessionHost::get().pageIsOutput())
					{
						fprintf(stderr, "audio: %s, paused\n", what ? what->c_str() : "?");
						SessionHost::get().play(false);
						player.pauseNow();
					}
					else fprintf(stderr, "audio: %s (nothing playing here)\n", what ? what->c_str() : "?");
				}
				else if (e.type == evAmp)
				{
					std::unique_ptr<std::string> chip((std::string*)e.user.data1);
					fprintf(stderr, "audio: the speaker amplifier %s didn't start\n", chip ? chip->c_str() : "?");
					if (!ampReloadAt) ampReloadAt = SDL_GetTicks() + 600;
				}
				else if (e.type == evLidIncognito)
				{
					// a real lid-close transition from the switch (SleepWatch). Incognito decides here, and only if it
					// is on at this moment and the switch still reads shut; otherwise this is an ordinary lid close
					IncognitoLidAction action = incognitoLidAction(sleepWatch.lidClosed(), ui.incognitoOn(), player.settings.lidIncognito);
					if (!sleepWatch.lidClosed())
						fprintf(stderr, "incognito: lid switch: ignored (the lid is open)\n");
					else if (!incognitoNow("lid switch")) lidPause(true);
					else if (action == IncognitoLidAction::Quit)
					{
						fprintf(stderr, "incognito: lid switch: quitting\n");
						running = false;
					}
					else if (action == IncognitoLidAction::CloseAll)
					{
						fprintf(stderr, "incognito: lid switch: close all\n");
						closePhoto(false);
						if (haveUi) ui.incognitoCloseAllNow(player);
					}
					else lidPause(true);	// Nothing: the lid does what it does with incognito off
				}
				else if (e.type == evPower)
				{
					int holdMode = e.user.code;
					if (holdMode == PowerKey::SleepAfterCloseAll || holdMode == PowerKey::SleepAfterQuit)
					{
						// a 2 s hold is a sleep request whatever incognito says: the close all / quit is the extra,
						// decided here from the incognito and setting of this moment (the key's thread read a copy a
						// loop old). The sleep is asked for in either case
						int mode = player.settings.lidIncognito;
						if (!incognitoNow("power hold") || mode == LID_INCOGNITO_NOTHING) askBananadSleep();
						else if (mode == LID_INCOGNITO_QUIT)
						{
							fprintf(stderr, "incognito: power hold: quitting, then sleeping\n");
							running = false;
							askBananadSleep();
						}
						else
						{
							fprintf(stderr, "incognito: power hold: close all, then sleeping\n");
							closePhoto(false);
							if (haveUi) ui.incognitoCloseAllNow(player);
							askBananadSleep();
						}
					}
					// the sleep timer's last minute with the lid shut (Keep playing): no card is seen, the fade is the
					// warning, and Power is 10 more minutes (for that minute only)
					else if (haveUi && ui.sleep->warning() && sleepWatch.lidClosed())
					{
						fprintf(stderr, "power: press: the sleep timer's snooze\n");
						ui.sleep->snooze();
					}
					// the power key with the lid shut, let go before 2 s: play/pause, the same Session path A takes (its
					// Play/Pause, then mpv). A page that plays it is taken over here, as A does. The conditions are
					// checked again: a press queued as they ended (the lid opening) does nothing
					else if (player.playing() && player.settings.lidKeepPlaying && sleepWatch.lidClosed())
					{
						if (SessionHost::get().pageIsOutput())
						{
							fprintf(stderr, "power: press: playing on the handheld\n");
							SessionHost::get().takeOver();
							player.osd("Playing on the handheld");
						}
						else
						{
							fprintf(stderr, "power: press: play/pause\n");
							SessionHost::get().play(player.paused);
							player.togglePause();
						}
					}
				}
				else if (e.type == evVolKey)
				{
					// a volume key with the lid shut: a press (under 0.6 s, on letting go) is one volume step through
					// BananaOS's own command; a 0.6 s hold is next / previous. Nothing else, and no sound effects
					bool ok = player.settings.lidKeepPlaying && sleepWatch.lidClosed() && player.playing() && !player.playingPhoto();
					if (ok && (e.user.code == VolumeKey::Next || e.user.code == VolumeKey::Previous))
						lidStep(e.user.code == VolumeKey::Next ? 1 : -1);
					else if (ok)
					{
						bool up = e.user.code == VolumeKey::VolumeUp;
						bool browserOut = false;
#ifdef BP_WEB
						const Session& ss = SessionHost::get().session();
						if (ss.out.kind == OutKind::Browser && ss.index >= 0 && !ss.out.tab.empty() && webServer)
						{
							// the same keys step that browser's volume, as with the lid open
							browserOut = true;
							int bv = std::clamp((ui.browserVol >= 0 ? ui.browserVol : 100) + (up ? 5 : -5), 0, 100);
							ui.browserVol = bv;
							webServer->notify("volume", "{\"tab\":\"" + ss.out.tab + "\",\"v\":" + std::to_string(bv) + "}");
							if (!(SDL_GetTicks() < browserVolUntil)) handheldVolKept = ui.volumeLevel();
							browserVolUntil = SDL_GetTicks() + 1500;
							fprintf(stderr, "volume keys: %s press: the browser's volume %d (the handheld's kept)\n", up ? "Volume +" : "Volume -", bv);
							showLevelCard("Browser volume", bv, 1500);
						}
#endif
						if (!browserOut)
						{
							int v = haveUi ? ui.volumeLevel() : -1;
							if (v < 0) fprintf(stderr, "volume keys: %s press: no level to step\n", up ? "Volume +" : "Volume -");
							else
							{
								// stops at 100% with the lid shut: a boost set before stays, the keys do not add to it
								int to = std::clamp(v + (up ? 5 : -5), 0, 100);
								if (haveUi) ui.setVolumeTotal(player, to, true);
								fprintf(stderr, "volume keys: %s press: volume %d%%\n", up ? "Volume +" : "Volume -", to);
							}
						}
					}
				}
				else if (e.type == evLevel)
				{
					std::string* what = (std::string*)e.user.data1;
					// bananad's word on a volume key that moved the browser's volume: the handheld's put back, and the
					// card stays the browser's
					if (*what == "volume" && SDL_GetTicks() < browserVolUntil)
					{
						if (haveUi) ui.keepHandheldVolume(handheldVolKept);
						delete what;
						got = SDL_PollEvent(&e);	// (the loop's next event: a bare continue would take this one again)
						continue;
					}
					// bananad's word on a volume key the player took past 100% (its 100, or the 95 its Down set before the
					// player put 100 back): the level as heard instead
					if (*what == "volume" && (SDL_GetTicks() < volumeOverrideUntil || player.boost > 100))
					{
						int heard = SDL_GetTicks() < volumeOverrideUntil ? volumeOverride : player.boost;
						if (heard >= 100) e.user.code = heard;
					}
					showLevelCard(*what == "brightness" ? "Brightness" : "Volume", e.user.code, 1500);
					if (haveUi) ui.levelChanged(*what, e.user.code);	// the quick settings' bar follows the keys
					fprintf(stderr, "level: %s %d (at %u ms)\n", what->c_str(), e.user.code, SDL_GetTicks());
					logNextTop = true;
					if (fHeld) fCombo = true;	// F + volume: bananad's brightness
					delete what;
				}
				else if (e.type == evNet) ui.netListed((NetListing*)e.user.data1, player);
				else if (e.type == evDur) ui.durationsReady(player);
				else if (e.type == evVideoArt)
				{
					// thumbnails, frames and facts landed: into textures and the top pane's cache on this thread (the
					// serial in the pages' signatures, the pane's key, draw them)
					if (haveUi) ui.mediaArt->videosReady(videoArt->take());
					else videoArt->take();
				}
				else if (e.type == evIndex)
				{
					// the media index finished a pass: a new snapshot with a new generation is in indexWorker->index().
					// Music, Videos and Search hold the generation in their signature(), so this pass's landing is
					// drawn on the next loop pass, here on the main thread, never from the worker
				}
				else if (e.type == evMangaFetch) mangaNet->onFetchEvent();
				else if (e.type == evMangaWorker) mangaNet->onWorkerEvent();
				else if (e.type == evCover)
				{
					// a cover arrived: into a texture on this thread, and the page redraws (its serial is in signature).
					// Main's one worker decodes Manga's covers and the songs' art: the "home:" keys are the song
					// art cache's (Ui's: Home's cards, the rail's Now playing key, Quick settings' block), the
					// "bg:" ones the Match-album-art cache's (the player's ground and the top screen's)
					std::vector<CoverWorker::Result> results = coverWorker->take();
					std::vector<CoverWorker::Result> home, manga, backdrops;
					const size_t prefix = std::strlen(HOME_COVER_KEY), bprefix = std::strlen(BACKDROP_COVER_KEY);
					for (auto& r : results)
					{
						if (r.key.compare(0, bprefix, BACKDROP_COVER_KEY) == 0) backdrops.push_back(std::move(r));
						else (r.key.compare(0, prefix, HOME_COVER_KEY) == 0 ? home : manga).push_back(std::move(r));
					}
					if (haveUi)
					{
						if (!manga.empty()) ui.manga->coversReady(std::move(manga));
						if (!home.empty()) ui.songCoversReady(std::move(home));
						for (auto& r : backdrops) ui.backdropReady(r);
					}
					// (nothing draws them when !haveUi: dropped)
				}
				else if (e.type == evMangaLib)
				{
					// the library listed, or chapter files read: into the lists on this thread (its serial is in signature)
					if (haveUi) ui.manga->mangaLibReady(mangaLibWorker->take());
					else mangaLibWorker->take();
				}
				else if (e.type == evFound)
				{
					std::unique_ptr<FoundHost> h((FoundHost*)e.user.data1);
					ui.hostFound(*h, player);
				}
#ifdef BP_WEB
				else if (e.type == evWeb)
				{
					auto c = WebCall::take(e.user.data1);
					// a toast over the top panel for 2 s, unless a video plays (it'd be over the picture) or the page that
					// says it is open
					auto toast = [&](const char* text) {
						bool video = player.playing() && !player.playingAudio() && !player.playingPhoto();
						if (video || ui.webPageShown()) return;
						shown().osd(text, 2000);
						levelUntil = SDL_GetTicks() + 2000;	// (with nothing playing the top panel is drawn while a card is up)
						renderPending = true;
					};
					if (!c) {}
					else if (c->kind == WebCall::Kind::Call) c->run(player, ui);
					else if (c->kind == WebCall::Kind::FsChanged) ui.folderChanged(c->arg, player);
					else if (c->kind == WebCall::Kind::ClientConnected) toast("Web dashboard: a browser signed in");
					else if (c->kind == WebCall::Kind::PinRotated) toast("PIN changed after too many wrong tries");
					else if (c->kind == WebCall::Kind::ClientsChanged) webForce = true;
				}
#endif
				else if (e.type == evTouch)
				{
					Pointer* pt = (Pointer*)e.user.data1;
					ui.pointerMs = e.common.timestamp;	// (pushed by the touch thread as it read it)
					if (ui.pointer(*pt, shown())) leave();
					delete pt;
				}
				else if (haveUi && !ownTouch)
				{
					// a computer: the mouse in the bottom window, or SDL's touches on it (a touch's copy as a mouse is
					// left out: it came as a finger)
					Pointer pt{Pointer::Move, 0, 0};
					bool use = false;
					int ww, wh;
					SDL_GetWindowSize(bottom, &ww, &wh);
					if ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) && e.button.which != SDL_TOUCH_MOUSEID &&
						e.button.windowID == bottomId && e.button.button == SDL_BUTTON_LEFT)
					{
						pt = {e.type == SDL_MOUSEBUTTONDOWN ? Pointer::Down : Pointer::Up, e.button.x / (float)ww, e.button.y / (float)wh};
						use = true;
					}
					else if (e.type == SDL_MOUSEMOTION && e.motion.which != SDL_TOUCH_MOUSEID && e.motion.windowID == bottomId &&
						(e.motion.state & SDL_BUTTON_LMASK))
					{
						pt = {Pointer::Move, e.motion.x / (float)ww, e.motion.y / (float)wh};
						use = true;
					}
					else if ((e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP || e.type == SDL_FINGERMOTION) &&
						(e.tfinger.windowID == bottomId || e.tfinger.windowID == 0))
					{
						// one finger at a time: the first down, till it lifts
						if (e.type == SDL_FINGERDOWN && finger < 0) finger = e.tfinger.fingerId;
						if (e.tfinger.fingerId == finger)
						{
							pt = {e.type == SDL_FINGERDOWN ? Pointer::Down : e.type == SDL_FINGERUP ? Pointer::Up : Pointer::Move,
								e.tfinger.x, e.tfinger.y};
							use = true;
							if (e.type == SDL_FINGERUP) finger = -1;
						}
					}
					if (use)
					{
						pt.x = std::clamp(pt.x, 0.f, 1.f);
						pt.y = std::clamp(pt.y, 0.f, 1.f);
						ui.pointerMs = e.common.timestamp;
						if (ui.pointer(pt, shown())) leave();
					}
				}
				got = SDL_PollEvent(&e);
			}
			FrameWatch::add("events", SDL_GetTicks() - eventsAt);
			// the shoulder keys' hold: one step at 0.6 s; a key down 5 s is stuck and ignored until let go; not lit
			// anymore (the lid opened, Stop) releases them all
			if (!lidKeysNow())
			{
				for (int i = 0; i < 4; i++) lidShoulderUp(i);
			}
			else if (player.settings.lidShoulder == LID_SHOULDER_HOLD)
			{
				Uint32 at = SDL_GetTicks();
				for (int i = 0; i < 4; i++)
				{
					if (!lidShoulderAt[i] || lidShoulderDone[i]) continue;
					Uint32 held = at - lidShoulderAt[i];
					if (held >= 5000)
					{
						lidShoulderDone[i] = lidShoulderStuck[i] = true;
						fprintf(stderr, "lid keys: shoulder %s down %u ms: stuck, ignored until let go\n",
							i == 0 ? "L" : i == 1 ? "R" : i == 2 ? "L2" : "R2", held);
					}
					else if (held >= 600)
					{
						lidShoulderDone[i] = true;
						lidShoulderAct(i);
					}
				}
			}
			if (held != Button::None && SDL_GetTicks() >= heldNext)
			{
				heldNext = SDL_GetTicks() + ui.repeatMs(held, shown());
				if (ui.repeats(held, shown()) && ui.press(held, shown())) leave();
			}

			// flip: (a test step) a setting alternated on a timer, each change applied at once (no debounce)
			if (flipLeft > 0 && SDL_GetTicks() >= flipAt)
			{
				std::string v = flipVals[flipLeft % 2];
				Settings s = parseSettings(serializeSettings(player.settings) + flipKey + " = " + v + "\n");
				player.setSettings(s);
				player.displayNow();
				fprintf(stderr, "press: flip %s = %s (%d left)\n", flipKey.c_str(), v.c_str(), flipLeft - 1);
				flipLeft--;
				flipAt = SDL_GetTicks() + flipMs;
			}
			player.pollEvents();
			player.checkAudio();
			player.checkSongArt();
			player.syncNative();
			player.tickMarquee();
			player.checkSongEnd();
			player.checkDisplay();
			player.flushWrites();
			player.checkCpu();
			player.checkNet();
			player.checkLoading();
			photos.pollEvents();
			photos.checkNet();
			photos.checkPhoto();
			photos.checkLoading();
			photos.finished = false;	// (a photo stays: loop-file)
			// the settings changed over a photo (Settings over it): the player's, written; or the other way (the web)
			if (std::string ps = serializeSettings(player.settings), fs = serializeSettings(photos.settings); fs != photoSettingsSeen)
			{
				player.setSettings(photos.settings);
				photoSettingsSeen = fs;
			}
			else if (ps != photoSettingsSeen)
			{
				photos.settings = player.settings;
				photoSettingsSeen = ps;
			}
			if (haveUi)
			{
				FrameWatchScope fw("ui tick");
				ui.tick(shown());
			}
			if (player.quit || ui.quitRequested || ui.restartRequested) running = false;
			// the player's own Stop: the session's (everywhere: a page playing it too); what the session doesn't have (a
			// photo) stops here alone. The web's Stop of a photo, or of a file deleted under it, likewise
			if (ui.takeLeaveWanted() && photos.playing()) leave();
			if (ui.takeStopPressed() && !shadow.stop("hh").reject.empty()) stopPlaying(StopWhy::Handheld);
			if (ui.takeStopRequest()) stopPlaying(StopWhy::Web);
			// the mini bar tapped over a photo: the photo closes and the player of what plays takes the screen (the top
			// panel shows it again; the bottom panel is its player page)
			if (ui.takePlayerOverPhoto())
			{
				photoOverPlayer = false;
				closePhoto(false);
				ui.showPlayer();
				renderPending = true;
			}
			{
				std::string label;
				int pct = 0;
				if (int ms = ui.takeLevelShown(label, pct)) showLevelCard(label, pct, ms);
			}
			if (updateCheckAt && SDL_GetTicks() >= updateCheckAt)
			{
				updateCheckAt = 0;
				if (updater.state() == Updater::State::Idle) updater.check();
			}
			if (player.finished)
			{
				// played to its end: next time from the start; on to the next video, or out after the last
				player.saveResume();
				player.finished = false;
				// the session's Ended: the next one (the executor moves the handheld along its list), a wrap, or the end.
				// A photo's list, or a file the session doesn't have (it disagrees: logged), as before
				const Session& ss = shadow.session();
				bool mine = ss.out.kind == OutKind::Handheld && ss.index >= 0 && !player.playingPhoto() && player.currentPath() == shadow.file();
				// (the sleep timer's End of this song or video: paused at once, as the next one opens)
				bool sleepEnds = haveUi && ui.sleep->endsWithItem();
				if (mine && shadow.ended().reject.empty()) { if (shadow.session().index < 0) stopPlaying(StopWhy::End); }
				else if (!player.advance()) stopPlaying(StopWhy::End);
				if (sleepEnds) ui.sleep->itemEnded();
			}
			// the place is kept now and then too: a battery that runs out, a power-off after sleeping
			bool woke = slept.exchange(false);
			if (woke)
			{
				freshUntil = SDL_GetTicks() + 2000;	// the panels back from a sleep: drawn again
				stickY = stickX = 0;	// a stick held as the handheld slept must not start a reader at the wake
				ui.undim();	// (slept in the dim before Screen off: the bottom panel's level back)
			}
#ifdef BP_WEB
			// the server was frozen with the rest: the pages watching get the state afresh
			if (woke) webForce = true;
#endif
			// a photo has no Play: an animated one paused by the lid goes on at the wake (a video waits for A)
			if (woke && photos.playing()) photos.unpause();
			if (woke) shadow.woke();
			if (woke && haveUi) ui.sleep->woke();	// (a timer doesn't outlive a sleep; its own sleep says so on waking)
			if (woke || (player.loaded && !player.paused && SDL_GetTicks() - savedAt > 60000))
			{
				player.saveResume();
				savedAt = SDL_GetTicks();
			}
			// a pick (here, or on the web): it opens here as today, and the session's Open says where it plays: the
			// selected output (pref), a browser tab that can play it, else here with "This one plays on the handheld".
			// Held paused here for a page at once (it never sounds); the page loads it from the session
			auto routed = [&](const std::vector<std::string>& list, int index, int kind, const std::string& title, const std::string& pick,
				const std::string& id = "") {
				if (isPhotoFile(pick)) return;
				shadow.open(list, index, kind, title, "hh", nullptr, 0, true, id);
				if (shadow.pageIsOutput() && shadow.file() == player.currentPath()) player.holdPaused();
			};
			// a video picked in the browser: it and the videos beside it
			if (std::string pick = ui.takeOpenRequest(); !pick.empty())
			{
				int index = 0;
				std::vector<std::string> list, names;
				if (isNetPath(pick))
				{
					// the folder's listing, as the browser had it: the files after this one, its subtitles, its cover
					std::string dir = netParent(pick);
					names = ui.netFolderNames(dir);
					for (auto& n : siblingNames(netBaseName(pick), names, index)) list.push_back(netJoin(dir, n));
					if (list.empty()) list = {pick};
				}
				else list = siblingVideos(pick, index);
				if (isPhotoFile(pick))
				{
					// a photo: the photo viewer's, over what plays, which goes on (a song, the next one after it, a video's
					// sound; a page's: untouched). The settings the player's
					if (!photos.playing()) photoOverPlayer = haveUi && ui.playerShown(player);
					photos.settings = player.settings;
					photoSettingsSeen = serializeSettings(player.settings);
					player.hideVideo(true);
					photos.open(list, index, names);
					if (player.playing()) fprintf(stderr, "photo: over %s, which plays on\n", player.currentPath().substr(player.currentPath().rfind('/') + 1).c_str());
				}
				else
				{
					closePhoto(false);	// (a song or video picked over a photo: it takes the screen)
					player.open(list, index, names);
					routed(list, index, isNetPath(pick) ? 1 : 0, "", pick);
				}
				ui.showPlayer();
			}
			// a playlist picked in the browser: its items, in its order (or shuffled); Up next's row
			if (Ui::PlayRequest pr; ui.takePlayRequest(pr))
			{
				std::string pick = pr.paths[std::clamp(pr.index, 0, (int)pr.paths.size() - 1)];
				if (!pr.stay) closePhoto(false);
				if (pr.source.kind == SourceKind::Playlist) player.openPlaylist(pr.paths, pr.index, pr.source);
				else player.open(pr.paths, pr.index, pr.names);
				routed(pr.paths, pr.index, pr.source.kind == SourceKind::Playlist ? 2 : pr.source.kind == SourceKind::NetFolder ? 1 : 0,
					pr.source.title, pick, pr.source.kind == SourceKind::Playlist ? pr.source.id : std::string());
				if (pr.stay) ui.playBehind();
				else ui.showPlayer();
			}
			// a connection added, changed or deleted: mpv's opens see it
			if (netStore.version() != netVersion)
			{
				netVersion = netStore.version();
				netStreams.update(netStore);
				photoStreams.update(netStore);
			}
			// a test run's steps wait for the file to open, 3 s at most (a file that never does: a loading test)
			static Uint32 waitFrom = 0;
			if (player.loaded || !player.playing() || !player.error.empty()) waitFrom = 0;
			else if (!waitFrom) waitFrom = SDL_GetTicks();
			// the rest of a BUTTON*N step, between passes of the loop (so main sends what a press set, as for a hand), and
			// not held up by a file opening (a burst of Next opens one after another)
			static Button burstButton = Button::None;
			static int burstLeft = 0;
			static Uint32 burstAt = 0;
			if (scripted && burstLeft > 0 && running && SDL_GetTicks() >= burstAt)
			{
				burstLeft--;
				burstAt = SDL_GetTicks() + 80;
				if (ui.press(burstButton, shown())) leave();
				ui.release(burstButton, shown());
			}
			if (scripted && (!waitFrom || SDL_GetTicks() - waitFrom > 3000) && running)
			{
				static Uint32 next = SDL_GetTicks() + 800;
				static size_t pressed = 0;
				if (burstLeft > 0) next = SDL_GetTicks() + 800;
				else if (SDL_GetTicks() >= next)
				{
					next = SDL_GetTicks() + 800;
					if (pressed == steps.size()) running = false;
					else
					{
						const std::string& step = steps[pressed++];
						float v[4];
						auto at = [&](Pointer::Kind k, float x, float y) {
							if (ui.pointer({k, x / Ui::W, y / Ui::H}, shown())) leave();
						};
						if (sscanf(step.c_str(), "dtap:%f:%f", &v[0], &v[1]) == 2)
						{
							// a double-tap: two taps 120 ms apart
							for (int i = 0; i < 2; i++)
							{
								if (i) SDL_Delay(120);
								at(Pointer::Down, v[0], v[1]);
								at(Pointer::Up, v[0], v[1]);
							}
						}
						// a finger that stays down from one step to the next (a held slider's look): down, moves, up
						else if (sscanf(step.c_str(), "down:%f:%f", &v[0], &v[1]) == 2) at(Pointer::Down, v[0], v[1]);
						else if (sscanf(step.c_str(), "move:%f:%f", &v[0], &v[1]) == 2) at(Pointer::Move, v[0], v[1]);
						else if (sscanf(step.c_str(), "up:%f:%f", &v[0], &v[1]) == 2) at(Pointer::Up, v[0], v[1]);
						else if (sscanf(step.c_str(), "tap:%f:%f", &v[0], &v[1]) == 2)
						{
							at(Pointer::Down, v[0], v[1]);
							at(Pointer::Up, v[0], v[1]);
						}
						else if (sscanf(step.c_str(), "hold:%f:%f", &v[0], &v[1]) == 2)
						{
							// a finger held 1.1 s, the panel drawn meanwhile (a hold is timed there)
							at(Pointer::Down, v[0], v[1]);
							for (int i = 0; i < 11; i++)
							{
								SDL_Delay(100);
								ui.tick(shown());	// (the Play button's hold is timed there)
								ui.draw(shown());
							}
							at(Pointer::Up, v[0], v[1]);
						}
						else if (sscanf(step.c_str(), "drag:%f:%f:%f:%f", &v[0], &v[1], &v[2], &v[3]) == 4)
						{
							// a finger's way, in steps 30 ms apart: the scrub sees it move
							at(Pointer::Down, v[0], v[1]);
							for (int i = 1; i <= 10; i++)
							{
								SDL_Delay(30);
								at(Pointer::Move, v[0] + (v[2] - v[0]) * i / 10, v[1] + (v[3] - v[1]) * i / 10);
								player.pollEvents();
							}
							at(Pointer::Up, v[2], v[3]);
						}
						else if (step == "info")
						{
							// what the Media info page shows
							for (auto& r : player.mediaInfo())
								printf(r.header ? "  [%s]\n" : "    %-16s %s\n", r.label.c_str(), r.value.c_str());
						}
						else if (step == "screenshot") screenshot();	// as F + L2 does
						else if (step == "volup" || step == "voldown")
						{
							// the handheld's volume keys (bananad's on the handheld: here only what the player does with them)
							SDL_Event ev{};
							ev.type = SDL_KEYDOWN;
							ev.key.keysym.scancode = step == "volup" ? SDL_SCANCODE_VOLUMEUP : SDL_SCANCODE_VOLUMEDOWN;
							ev.key.keysym.sym = step == "volup" ? SDLK_VOLUMEUP : SDLK_VOLUMEDOWN;
							SDL_PushEvent(&ev);
						}
						else if (step.rfind("flip:", 0) == 0)
						{
							// flip:KEY=A|B:COUNT:MS: KEY set to A and B in turn COUNT times, MS apart, each applied at once
							std::string r = step.substr(5);
							size_t eq = r.find('='), bar = r.find('|'), c1 = r.find(':', bar), c2 = r.find(':', c1 + 1);
							if (eq != std::string::npos && bar != std::string::npos && c1 != std::string::npos && c2 != std::string::npos)
							{
								flipKey = r.substr(0, eq);
								flipVals[1] = r.substr(eq + 1, bar - eq - 1);
								flipVals[0] = r.substr(bar + 1, c1 - bar - 1);
								flipLeft = atoi(r.c_str() + c1 + 1);
								flipMs = (Uint32)atoi(r.c_str() + c2 + 1);
								flipAt = SDL_GetTicks();
							}
						}
						else if (step.rfind("set:", 0) == 0)
						{
							// set:KEY=VALUE: one setting changed as Settings would (a test's way to flip, e.g. the
							// visualizer, without driving the pages)
							std::string kv = step.substr(4);
							size_t eq = kv.find('=');
							if (eq != std::string::npos)
							{
								Settings s = parseSettings(serializeSettings(player.settings) + kv.substr(0, eq) + " = " + kv.substr(eq + 1) + "\n");
								player.setSettings(s);
								fprintf(stderr, "press: set %s\n", kv.c_str());
							}
							renderPending = true;
						}
						else if (step.rfind("level:", 0) == 0)
						{
							// level:volume:40 (or brightness): as the hardware keys' change comes, so the level card shows
							std::string rest = step.substr(6), name = rest.substr(0, rest.find(':'));
							SDL_Event ev{};
							ev.type = evLevel;
							ev.user.code = rest.find(':') == std::string::npos ? 50 : atoi(rest.c_str() + rest.find(':') + 1);
							ev.user.data1 = new std::string(name);
							SDL_PushEvent(&ev);
						}
						else if (step.rfind("card:", 0) == 0)
						{
							// card:state|eq|note|like|fail: a top-panel card as its feature would show it (for a shot:)
							std::string what = step.substr(5);
							if (what == "state") player.showElsewhere(true, true, "Chrome on Mac", false, 1, G_QR_ROWS, G_QR.h);
							else if (what == "off") player.showElsewhere(false);
							else if (what == "eq") player.showEqCard();
							else if (what == "note") player.osd("Speed 1.5\xc3\x97");
							else if (what == "like") player.showNoteCard(G_HEART_ROWS, G_HEART.h, "Added to Liked", "Big Buck Bunny");
							else if (what == "fail") player.osd("Couldn't save Liked");
							renderPending = true;
						}
						else if (step.rfind("gallery:variants", 0) == 0)
						{
							// gallery:variants[:PAGE] (1..20): the kit's variant components on one page, for a shot:
							std::string::size_type colon = step.rfind(':');
							int pg = colon > 7 ? atoi(step.c_str() + colon + 1) : 1;
							ui.openVariantsGallery(pg > 0 ? pg : 1);
							renderPending = true;
						}
						else if (step.rfind("gallery:", 0) == 0)
						{
							// gallery:new (gallery:new:2 with the Screen-off warning over it, gallery:off): kit-new's
							// components drawn in place of the page, and a Preview pane on the top panel (for a shot:)
							ui.galleryNew = step == "gallery:new" ? 1 : step == "gallery:new:2" ? 2 : step == "gallery:new:3" ? 3 : 0;
							renderPending = true;
						}
						else if (step.rfind("shot:", 0) == 0)
						{
							// both panels: <BP_SHOTS or .>/<name>-top.png and -bottom.png
							std::string base = std::string(getenv("BP_SHOTS") ? getenv("BP_SHOTS") : ".") + "/" + (step.c_str() + 5);
							SDL_Surface* t = captureTop();
							SDL_Surface* bt = haveUi ? ui.capture(shown()) : nullptr;
							bool ok = t && bt && savePng(base + "-top.png", t->w, t->h, (unsigned char*)t->pixels) &&
									  savePng(base + "-bottom.png", bt->w, bt->h, (unsigned char*)bt->pixels);
							if (t) SDL_FreeSurface(t);
							if (bt) SDL_FreeSurface(bt);
							printf("    shot %s: %s\n", step.c_str() + 5, ok ? "saved" : "failed");
						}
						else if (step.rfind("prop:", 0) == 0)
							printf("    %s = %s\n", step.c_str() + 5, player.property(step.c_str() + 5).c_str());
						else if (step == "rows")
						{
							for (auto& l : ui.browseLabels()) printf("    %s\n", l.c_str());
						}
						else if (step == "F") ui.toggleQuick(player);	// F alone
						else if (step == "update")
						{
							static const char* names[] = {"off", "idle", "checking", "up-to-date", "available", "downloading", "ready", "failed"};
							printf("    update: %s latest=%s progress=%d %s\n", names[(int)updater.state()], updater.available().c_str(),
								updater.progress(), updater.message().c_str());
						}
#ifdef BP_WEB
						else if (step == "web")
						{
							bool up = webServer && webServer->running();
							printf("    web: %s url=http://%s:%d pin=%s clients=%d pageOut=%d screen=%s\n", up ? "running" : webServer ? "stopped" : "off",
								deviceAddress().c_str(), up ? webServer->port() : 0, up ? webServer->pin().c_str() : "", up ? webServer->clients() : 0,
								ui.pageIsOut(), ui.screenOn() ? "on" : "off");
						}
#endif
						else if (step == "page")
						{
							for (auto& l : ui.pageLines()) printf("    %s\n", l.c_str());
						}
						else if (step.find('*') != std::string::npos)
						{
							burstButton = buttonFromName(step.substr(0, step.find('*')));
							burstLeft = atoi(step.c_str() + step.find('*') + 1) - 1;
							burstAt = SDL_GetTicks() + 80;
							if (ui.press(burstButton, shown())) leave();
							ui.release(burstButton, shown());
						}
						else if (step.find('~') != std::string::npos)
						{
							// BUTTON~MS: held that long (the loop's tick() and drawing go on meanwhile, as they time a
							// hold), then let go
							Button b = buttonFromName(step.substr(0, step.find('~')));
							Uint32 until = SDL_GetTicks() + atoi(step.c_str() + step.find('~') + 1);
							if (ui.press(b, shown())) leave();
							while (SDL_GetTicks() < until)
							{
								SDL_Delay(30);
								player.pollEvents();
								ui.tick(shown());
								ui.draw(shown());
							}
							ui.release(b, shown());
						}
						else
						{
							Button b = buttonFromName(step);
							if (ui.press(b, shown())) leave();
							ui.release(b, shown());
						}
						// (no wait for mpv's answer here: a loop that doesn't draw holds mpv up as it switches files, and
						// the timings would be the harness's; the state line can lag a step)
						player.pollEvents();
						const Track* a = player.selected(TrackType::Audio);
						const Track* s = player.selected(TrackType::Sub);
						if (ui.settingsPageOpen()) printf("press %zu: settings\n", pressed);
						else if (ui.searchPageOpen())
							// Search is its own page (once it fell through to the line below, printed as
							// "settings"): its query, the cursor and whether the keyboard is up
							printf("press %zu: search q=%s cursor=%d%s\n", pressed, ui.search->query.c_str(),
								ui.search->cursor, ui.search->typing ? " typing" : "");
						else if (ui.browsing())
						{
							std::string at = ui.browseSelection();
							printf("press %zu: browse=%s at=%s\n", pressed, ui.browseFolder().c_str(), at.substr(at.rfind('/') + 1).c_str());
						}
						else if (photos.playing())
						{
							// (and what plays under it: its file, place and pause)
							const Player::PhotoView& v = photos.photo();
							printf("press %zu: photo=%d/%d %dx%d zoom=%.2f pan=%.3f,%.3f turn=%d %s thumb=%dx%d screen=%s", pressed,
								photos.index() + 1, photos.count(), photos.photoWidth(), photos.photoHeight(), v.zoom, v.panX, v.panY,
								v.rotate, v.fill ? "fill" : "fit", photos.thumbW, photos.thumbH, ui.screenOn() ? "on" : "off");
							if (player.playing())
								printf(" under=%s t=%.1f paused=%d", player.currentPath().substr(player.currentPath().rfind('/') + 1).c_str(), player.position, player.paused);
							printf("\n");
						}
						else if (std::string lib = ui.libPageName(); !lib.empty())
						{
							// the library sections' own state: the view (Music's three, Videos' three, a
							// folder), the page below the views, the focus
							if (lib == "music") printf("press %zu: music view=%d%s%s cursor=%d row=%d keyFocus=%d\n", pressed,
								ui.music->view, ui.music->albumAt >= 0 ? " album" : "",
								ui.music->artistAt >= 0 ? " artist" : "", ui.music->cursor, ui.music->rowCursor, ui.music->keyFocus);
							else if (lib == "videos") printf("press %zu: videos view=%d folder=%s cursor=%d rows=%zu\n", pressed,
								ui.videos->view, ui.videos->folder.c_str(), ui.videos->cursor,
								ui.videos->rowsFor(player).size());
							else printf("press %zu: books reading=%zu shelf=%zu cursor=%d\n", pressed,
								ui.books->reading.size(), ui.books->shelf.size(), ui.books->cursor);
						}
						else if (ui.extPageShown()) printf("press %zu: extensions\n", pressed);
						else if (std::string mg = ui.mangaStateLine(); !mg.empty()) printf("press %zu: %s\n", pressed, mg.c_str());
						else if (!player.playing()) printf("press %zu: settings (from the browser)\n", pressed);
						else
						{
							// chapter=i/N: the current one (1-based, - before the first) of N, when the file has any
							std::string chapterState;
							if (!player.chapters.empty())
							{
								int c = player.currentChapter();
								chapterState = " chapter=" + (c >= 0 ? std::to_string(c + 1) : std::string("-")) + "/" + std::to_string(player.chapters.size());
							}
							printf("press %zu: file=%d/%d t=%.1f paused=%d speed=%s aspect=%s audio=%s sub=%s screen=%s%s%s\n", pressed,
							player.index() + 1, player.count(), player.position, player.paused, speedLabel(player.speed).c_str(),
							aspectName(player.aspect), a ? trackLabel(*a, (int)a->id).c_str() : "off",
							s ? trackLabel(*s, (int)s->id).c_str() : "off", ui.screenOn() ? "on" : "off", ui.touchIsLocked() ? " LOCKED" : "", chapterState.c_str());
						}
						fflush(stdout);
					}
				}
			}

#ifdef BP_WEB
			// the handheld paused for a page: said on the top panel (a paused visualizer looks frozen); gone with the
			// dashboard too
			{
				bool elsewhere = webServer && webServer->running() && ui.pageIsOut() && player.playing() && player.paused;
				// (+4: a song with the equalizer or an effect on, which the page doesn't play with: the card says so)
				bool sound = player.playingAudio() && (player.settings.eqOn || player.settings.soundFx != SoundFx::None);
				// (the page by name; waiting: it was refused the sound without a tap there, B plays here)
				bool waiting = elsewhere && shadow.pageWaits();
				std::string name = elsewhere ? ui.pageName() : "";
				// (sent again once the held copy has loaded, and for each file: the first one after a start could go to an
				// mpv with no picture yet, and the card never showed till something else changed)
				int hint = elsewhere ? ui.takeOverHint(player) : 0;	// (sent again as the bottom panel's page changes)
				std::string shown = elsewhere ? std::to_string(theme::currentTheme()) + std::to_string(ui.pagePlaying()) + std::to_string(sound) + std::to_string(waiting) + std::to_string(hint) + name +
					(player.loaded ? "|L|" : "|-|") + player.currentPath() : "";
				if (shown != elsewhereShown)
				{
					player.showElsewhere(elsewhere, ui.pagePlaying(), name, waiting, hint, G_QR_ROWS, G_QR.h);
					elsewhereShown = shown;
				}
			}
			// the web dashboard's live state: made only while a browser watches, sent when something but the clock
			// changed (or the clock drifted), at most 4 times a second
			if (webServer && webServer->running())
			{
				// the history or a kept place changed: the pages' Recent loads again
				if (player.recentSerial() != webRecentSent)
				{
					if (webRecentSent >= 0) webServer->notify("recent", "{\"serial\":" + std::to_string(player.recentSerial()) + "}");
					webRecentSent = player.recentSerial();
				}
				// (a new session version goes out at once: the pages render from it)
				if (webServer->clients() > 0 && (SDL_GetTicks() - webPublishedAt >= 250 || shadow.version() != shadow.publishedV))
				{
					WebSnapshot snap = webSnapshot(*webServer, player, ui);
					if (webForce || snapshotChanged(webLast, snap))
					{
						webServer->publish(webSnapshotJson(snap, ui));
						shadow.publishedV = snap.sessionV;
						webLast = snap;
						webForce = false;
						webPublishedAt = SDL_GetTicks();
					}
				}
			}
#endif
			if (haveUi)
			{
#ifdef BP_WEB
				shadow.pass(player, ui, [&webServer](const std::string& tab) { return webServer && webServer->running() && webServer->tabConnected(tab); },
					[&webServer](const std::string& tab) { return webServer && webServer->tabSaidBye(tab); });
#else
				shadow.pass(player, ui, nullptr);
#endif
				// the session stopped its item (a Stop, the end of the list, its file deleted): the handheld lets go too
				if (int u = shadow.takeUnload()) stopPlaying(u == 2 ? StopWhy::End : StopWhy::Web);
			}
			Uint32 now = SDL_GetTicks();
			// the sound output opened again after an amplifier didn't start (3 times a minute at most: a chip that never
			// starts isn't helped by more)
			if (ampReloadAt && (int)(now - ampReloadAt) >= 0)
			{
				ampReloadAt = 0;
				ampReloads.erase(std::remove_if(ampReloads.begin(), ampReloads.end(), [now](Uint32 t) { return now - t > 60000; }), ampReloads.end());
				if (ampReloads.size() < 3)
				{
					ampReloads.push_back(now);
					player.reloadAudio();
				}
				else fprintf(stderr, "audio: an amplifier still won't start: not opened again (3 tries this minute)\n");
			}
#ifdef __linux__
			// (every 5 s: the file is BananaOS's, written at a launch; a few bytes read)
			static Uint32 idleHoldAt = 0;
			if (now - idleHoldAt >= 5000)
			{
				idleHoldAt = now;
				FrameWatchScope fw("idle hold");
				holdIdleSleep();
			}
#endif
			// the player's own idle sleep, once a second (the pure rule and its logging: idlesleep.h)
			{
				static Uint32 idleTickAt = 0, powerAt = 0;
				static bool charging = false;
				if (now - idleTickAt >= 1000)
				{
					idleTickAt = now;
					FrameWatchScope fw("idle tick");
					if (!powerAt || now - powerAt >= 10000)
					{
						powerAt = now;
						const char* base = getenv("BP_POWER_SUPPLY");
						charging = readBattery(base ? base : "/sys/class/power_supply").charging;
					}
					IdleHolds h = idleHoldsNow(charging);
					idleSleep.tick(now, h, [&] { incognitoBeforeSleep("idle sleep"); });
				}
			}
			// the sleep timer (ui_sleep.h): its fade, its cards, its end, each pass; then, 10 s paused and dark, the sleep
			// asked of BananaOS as its menu's Sleep does. Only a copy, a move or a download holds it (Stay awake for
			// copies and downloads): a browser signed in, or playing, doesn't (the timer was someone's own choice)
			if (haveUi && ui.sleep->tick())
			{
				IdleHolds h = idleHoldsNow(false);
				h.playing = h.browserOut = h.webActive = false;
				std::string held = idleHeldBy(h);
				if (player.settings.sleepTimerPause) fprintf(stderr, "sleep timer: When the sleep timer ends is Pause: it stays awake, dark\n");
				else if (!held.empty()) fprintf(stderr, "sleep timer: no sleep: %s keeps the handheld awake (paused, dark)\n", held.c_str());
				else
				{
					incognitoBeforeSleep("sleep timer");
#ifdef BP_WEB
					if (webServer && webServer->running()) webServer->notify("bye", "{\"reason\":\"sleeptimer\"}");
#endif
					bool asked = askBananadSleep();
					fprintf(stderr, "sleep timer: the sleep %s\n", asked ? "asked of BananaOS" : "not asked (no /run/banana: not BananaOS)");
				}
			}
			// When the lid closes: Keep playing. BananaOS's lid sleep off while something plays here, back after; and the
			// whole time the web dashboard runs, since a browser playing from it (the output moved there, the handheld
			// paused) needs the handheld awake: lid sleep put back as the handheld paused let a shut lid sleep it, and
			// the page's sound with it
			{
				static Uint32 lidTickAt = 0;
				if (now - lidTickAt >= 250)
				{
					lidTickAt = now;
					FrameWatchScope fw("lid guard");
					keepOnLid = player.settings.lidKeepPlaying;
					// loaded counts, paused too: with the lid shut a pause by the power key must not let it sleep,
					// or Power could never play again; only nothing loaded (Stop) gives the lid back to BananaOS
					bool plays = player.playing() && !player.playingPhoto();
#ifdef BP_WEB
					plays = plays || (webServer && webServer->running());
#endif
					lidGuard.tick(now, player.settings.lidKeepPlaying && plays, sleepWatch.lidClosed());
				}
			}
			// a chapter or equalizer card shown: it goes the way the volume card does, after its time
			// BananaOS moves the sound (headphones in or out, Bluetooth headphones on or off) by writing ALSA's default
			// route to /run/banana/asound.conf; a device already open stays where it was, and each player is to watch the
			// file and open its sound again (bananad.c, audio_route). A new file each time (written and renamed): its
			// inode tells. Looked at twice a second; absent off the handheld
			// The file is written again (renamed over) more often than the sound moves: with Bluetooth headphones every
			// few seconds, the route the same. Opening the output again each time cut the sound, and in the middle of a
			// file opening left it with none (a video then waited for ever): only a route that says something else counts,
			// and not while a file opens (taken once it's open)
			{
				static Uint32 routeAt = 0;
				static ino_t routeIno = 0;
				static std::string routeText;
				static bool routeMoved = false;
				// only worth a look while something is loaded: a new file opens its sound on the route of the moment
				if (player.loaded && now - routeAt >= 500)
				{
					routeAt = now;
					FrameWatchScope fw("route check");
					struct stat rs;
					ino_t ino = stat("/run/banana/asound.conf", &rs) == 0 ? rs.st_ino : 0;
					if (ino != routeIno)
					{
						std::string text;
						if (ino)
							if (FILE* f = fopen("/run/banana/asound.conf", "r"))
							{
								char buf[1024];
								size_t n = fread(buf, 1, sizeof buf, f);
								fclose(f);
								text.assign(buf, n);
							}
						if (routeIno && ino && text != routeText) routeMoved = true;
						routeIno = ino;
						if (ino) routeText = text;
					}
					if (routeMoved && !player.loading())
					{
						routeMoved = false;
						player.reloadAudio("BananaOS moved the sound: headphones or Bluetooth");
					}
				}
			}
			if (int ms = std::max(player.takeCardShown(), photos.takeCardShown()))
			{
				levelUntil = now + ms;
				renderPending = true;
			}
			if (levelUntil && now >= levelUntil)
			{
				player.hideLevel();
				photos.hideLevel();
				levelUntil = 0;
				topDark = false;	// with no video: black again, without the card
			}
			// the top panel goes between a reader's pages and what plays (the player's pages over a reader): drawn at once
			{
				static bool readerWasOnTop = false;
				if (readerOnTop() != readerWasOnTop)
				{
					readerWasOnTop = !readerWasOnTop;
					refreshedAt = 0;
					renderPending = true;
					topDark = false;
					if (readerWasOnTop && reader.isOpen()) reader.redrawSoon();
				}
			}
			{
				static Uint32 kbClosedSeen = 0;
				if (haveUi && ui.keyboardClosedAt() != kbClosedSeen)
				{
					kbClosedSeen = ui.keyboardClosedAt();
					freshUntil = std::max(freshUntil, now + 1000);
				}
			}
			bool refresh = now - refreshedAt >= (now < freshUntil ? 100u : 2000u);
			if (refresh)
			{
				refreshedAt = now;
				topDark = false;	// the black of no video drawn again too
			}
			// the theme changed (Settings › Power & screens › Theme stepped, or the dashboard): both panels drawn again in
			// it at once, what was sent to mpv with its colours sent again, and the panels redrawn for a while after (as
			// after a wake: KMSDRM can drop a frame)
			if (shown().settings.theme != theme::currentTheme())
			{
				const char* from = theme::themeId(theme::currentTheme());
				theme::setTheme(shown().settings.theme);
				fprintf(stderr, "theme: %s -> %s\n", from, theme::themeId(theme::currentTheme()));
				ui.themeChanged();
				player.themeChanged();
				freshUntil = std::max(freshUntil, now + 2000);
				refreshedAt = 0;
				refresh = true;
				renderPending = true;
				topDark = false;
				if (reader.isOpen()) reader.redrawSoon();
			}
			// the manga reader's downloads move on (nothing while its network side hasn't started)
			mangaNet->pump(now);
			// Secure DNS changed (Manga settings): the next request and lookup take it
			{
				static bool mangaDnsOn = false;
				if (mangaDnsOn != player.settings.mangaSecureDns) mangaNet->setSecureDns(player.settings.mangaSecureDns);
				mangaDnsOn = player.settings.mangaSecureDns;
			}
			// the reader turned off (Manage extensions, or the dashboard): everything asked of its network side goes
			{
				static bool mangaWasOn = false;
				if (mangaWasOn && !player.settings.mangaOn) mangaNet->off();
				mangaWasOn = player.settings.mangaOn;
			}
			// the cards changed (ui.rootsCheck every few seconds: one put in or taken out): the media index walks again.
			// A file op (a delete, a rename, a move from any page) says so too, the same way
			{
				static int indexRootsSeen = 0, indexFilesSeen = 0;
				if (haveUi && (ui.rootsSerial() != indexRootsSeen || ui.filesSerial() != indexFilesSeen))
				{
					indexRootsSeen = ui.rootsSerial();
					indexFilesSeen = ui.filesSerial();
					indexWorker->refresh();
				}
			}
			// the media index's walk waits out whatever has the card: a file opening (its seeks and its decode) and a
			// video's picture. Music never stops it, and neither does a photo. (Not the Manga switch: it is a lasting
			// setting, so with Manga on the walk waited for ever and Videos / Music said "Reading …" with no end)
			{
				static bool indexBusy = false;
				bool busy = player.loading() || (player.playing() && !player.playingAudio() && !player.playingPhoto());
				if (busy != indexBusy)
				{
					indexWorker->setBusy(indexBusy = busy);
					videoArt->setBusy(busy);	// (its frames wait too; what is cached still answers)
					fprintf(stderr, "media index: walk %s\n", busy ? "waits (a file opening or a video)" : "goes on");
				}
			}
			// a video that plays: what mpv knows of it (every track, a subtitle file beside it, the exact length) written
			// over the cached facts, so the top screen shows what changed. Once per file and track list
			{
				static std::string recorded;
				std::string path = player.currentPath();
				if (player.playing() && !player.playingAudio() && !player.playingPhoto() && !player.loading() &&
					player.duration > 0 && !player.tracks.empty() && !isNetPath(path) && path.find("://") == std::string::npos)
				{
					std::string mark = path + "|" + std::to_string(player.tracks.size()) + "|" + std::to_string(player.chapters.size());
					if (mark != recorded)
					{
						recorded = mark;
						videoArt->record(path, videoMetaFromTracks(player.tracks, player.duration, (int)player.chapters.size()));
						fprintf(stderr, "video art: facts of %s from the player (%zu tracks)\n", path.c_str(), player.tracks.size());
					}
				}
			}
#ifdef BP_WEB
			// the cards changed (a second card put in or taken out): the dashboard's places follow
			{
				static int rootsSeen = 0;
				if (ui.rootsSerial() != rootsSeen && webServer)
				{
					rootsSeen = ui.rootsSerial();
					std::vector<WebRoot> wr;
					for (auto& r : ui.places())
						if (r.path != "/") wr.push_back({r.label, r.path});
					webServer->setRoots(wr);
				}
			}
#endif
			// the manga reader: a chapter asked for, the bottom panel's commands, what it shows told back
			if (haveUi)
			{
				// the left stick's push reaches whichever reader is being read (nothing elsewhere), then its
				// commands are applied below
				ui.stickAxis(stickY, player, stickX);
				FrameWatchScope fw("reader sync");
				ts.syncReaders(ui, player, mangaNet, opts.stateDir, now, woke, haveUi, showLevelCard, renderPending);
			}
			// with nothing playing the top panel is drawn on a change only, and mpv may not have the level card yet
			// when it's asked to show: drawn again and again while the card is up
			drainUnder();
			{
				FrameWatchScope fw("top draw");
				ts.draw(player, photos, shown(), ui, now, refresh, levelUntil, renderPending);
			}
			if (haveUi)
			{
				FrameWatchScope fw("bottom draw");
				// a slow bottom panel, in the log: what a press waits for on the handheld (the folder it was in)
				Uint32 t0 = SDL_GetTicks();
				ui.draw(shown(), refresh);
				Uint32 took = SDL_GetTicks() - t0;
				// (BP_DRAW_TIMING: every frame's line, a computer's frames being far under 30 ms)
				static const bool everyFrame = getenv("BP_DRAW_TIMING") != nullptr;
				if (took >= 30 || (everyFrame && ui.parts.total() > 0))
					fprintf(stderr, "timing: bottom panel drawn in %u ms (%s): %s\n", (unsigned)took,
						isNetPath(ui.browseFolder()) ? "network folder" : ui.browseFolder().empty() ? "player" : "browser",
						ui.partsLine().c_str());
				ui.parts = Ui::FrameParts{};
			}
			FrameWatch::end(SDL_GetTicks());
		}
		if (!player.error.empty()) rc = 1;
		// "Restart now": launch.sh starts again, the update put in place
		if (ui.restartRequested) rc = Updater::RESTART_CODE;
#ifdef BP_WEB
		// waiting calls answered 503, the streams ended, the listener stopped: a second at most
		if (webServer)
		{
			Uint32 t0 = SDL_GetTicks();
			// (a restart for an update too: to a page, the player closed; it comes back as a new boot id)
			webServer->stop("quit");
			fprintf(stderr, "web: stopped in %u ms\n", SDL_GetTicks() - t0);
		}
		ui.web = nullptr;
#endif
		durWorker->stop();
		readerFetch.end();
		mangaNet->setFetchOwner({});
		mangaNet->stop();
		coverWorker->stop();
		videoArt->stop();
		indexWorker->stop();
		mangaLibWorker->stop();
		// (before the watch stops: it knows the lid)
		lidGuard.release(sleepWatch.lidClosed());
		powerKey.stop();
		volumeKey.stop();
		sleepWatch.stop();
		levelWatch.stop();
		bottomTouch.stop();
		// a playlist's last edits (they wait a second to be written)
		if (haveUi) ui.flushPlaylist(player);
		// the manga reader's place now (its saves are debounced while a chapter is read); the writer is waited for, at
		// most a second, so the last place isn't lost with the process
		if (haveUi) ui.manga->mangaFlush();
		if (haveUi && ui.manga->writer) ui.manga->writer->stop();
		// the document reader's place, the same way
		if (haveUi) ui.docs->docFlush();
		if (haveUi && ui.docs->writer) ui.docs->writer->stop();
		player.saveResume();
		SDL_GL_MakeCurrent(top, gl);
		photos.shutdown();
		player.shutdown();
	}
	SDL_GL_DeleteContext(gl);
	if (bottom) SDL_DestroyWindow(bottom);
	SDL_DestroyWindow(top);
	TTF_Quit();
	SDL_Quit();
	return rc;
}
