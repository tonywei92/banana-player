// banana-player: a video player for BananaOS. The picture on the top panel, the controls on the bottom one.
//
//   banana-player [--press A,X,Down,...] [--windowed] [--aspect fit|fill|stretch] [--lang eng,en] [--state-dir DIR] [--font TTF] [-v] FILE

#include "crashlog.h"
#include <csignal>
#include "format.h"
#include "input.h"
#include "player.h"
#include "playlist.h"
#include "png.h"
#include "sleepwatch.h"
#include "levelwatch.h"
#include "touch.h"
#include "reader_proto.h"
#include "reader.h"
#include "readerfetch.h"
#include "manganet.h"
#include "ui.h"
#include "ui_glyphs.h"
#include "session_host.h"
#include "updater.h"
#include "netstore.h"
#include "networker.h"
#include "durworker.h"
#include "coverworker.h"
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

using namespace bp;

#ifdef BP_WEB
namespace bp
{
	// api_player.cpp: the playback state as the web dashboard shows it, and as its pages get it
	WebSnapshot webSnapshot(WebServer& ws, Player& p, Ui& u);
	std::string webSnapshotJson(const WebSnapshot& s, const Ui& u);
}
#endif

static Uint32 evMpv, evRender, evTouch, evLevel, evNet, evFound, evWeb, evDur, evCover;
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

static bool exists(const std::string& p)
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
		if (exists(p)) return p;
	return "";
}

static std::string defaultStateDir()
{
	// a Store app's files go under /data/home (run-store-app sets XDG_CONFIG_HOME there)
	if (const char* cfg = getenv("XDG_CONFIG_HOME")) return std::string(cfg) + "/banana-player";
	if (exists("/data/banana")) return "/data/home/.config/banana-player";
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
		"                        (tap:X:Y, dtap:X:Y a double-tap) and drags (drag:X1:Y1:X2:Y2) on the bottom panel's 640x480, and info\n"
		"                        (prints the Media info page), hold:X:Y (a finger held 1.1 s), prop:NAME\n"
		"                        (prints one of mpv's properties), BUTTON*N (N presses 80 ms apart),\n"
		"                        BUTTON~MS (held MS ms, then let go)\n"
		"buttons: A play/pause, LEFT -5 s, RIGHT +10 s, L/R previous/next video, UP/DOWN speed, X audio,\n"
		"         Y subtitles, SELECT aspect, START settings, B twice quits\n"
		"keyboard: Enter, arrows, PgUp/PgDn (or Q/E), X, Y, Tab, Esc twice\n");
}

int main(int argc, char** argv)
{
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
				bool ok = burst || held || buttonFromName(step) != Button::None || step == "info" || step == "page" || step == "update" || step == "web" || step == "F" || step == "rows" || step == "screenshot" || step.rfind("prop:", 0) == 0 || step.rfind("shot:", 0) == 0 ||
					(sscanf(step.c_str(), "tap:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "dtap:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "hold:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
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
	bp::crashlog::install(opts.stateDir, BP_VERSION, BP_COMMIT);
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
	if (!opts.stateDir.empty() && exists(opts.stateDir + "/verbose")) opts.verbose = true;
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
	evMpv = SDL_RegisterEvents(9);
	evRender = evMpv + 1;
	evTouch = evMpv + 2;
	evLevel = evMpv + 3;
	evNet = evMpv + 4;
	evFound = evMpv + 5;
	evWeb = evMpv + 6;	// the web dashboard's calls and news (WebCall)
	evDur = evMpv + 7;	// the file rows' durations: some are ready (DurationWorker::take)
	evCover = evMpv + 8;	// the manga library's covers: some are decoded (CoverWorker::take)

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
		// the mini bar's player: main's (songs and videos), also while a photo is shown (the photo viewer is its own
		// Player, and a page of its own)
		ui.mainPlayer = &player;
		bool haveUi = bottom && ui.init(bottom, opts.fontsDir);
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
			if (!pkg.empty() && !exists(pkg + "/VERSION")) pkg.clear();
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
		ui.mangaNet = mangaNet;
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
		ui.coverWorker = coverWorker;
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
		SleepWatch sleepWatch;
		sleepWatch.start([&player, &photos](bool lid) {
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
		}, [] {
#ifdef BP_WEB
			// back (or never gone: lid sleep off): the pages' "asleep" undone; the state follows (woke: webForce)
			if (WebServer* ws = webForWatch.load(); ws && ws->running()) ws->notify("awake", "{}");
#endif
		});

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
		if (browserMode)
		{
			ui.startBrowser(player, startFolder);
			{
				int tw = 0, th = 0;
				SDL_GL_GetDrawableSize(top, &tw, &th);
				player.setTopSize(tw, th);	// (the idle icon in whole panel pixels)
			}
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
		bool topDark = false;	// the top panel cleared while there's no video
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
			// (a photo up over it: the photo stays, and closing it goes where it would have)
			bool shown = !haveUi || (ui.playerShown(player) && !photos.playing());
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
		// the top panel's frame: the photo while one is up, else what plays, or the icon with nothing playing
		auto renderTop = [&](int w, int h) { return shown().render(w, h); };
		// What plays under a photo: a song's graph makes frames still (it carries the sound), and mpv waits for each to
		// be taken (else its picture queue stalls, 200 ms a frame). They're taken by a tiny target of their own with
		// rendering skipped (nothing drawn, no GL time), so the photo or the comic isn't drawn again 30 times a second
		// and the bottom panel keeps the GPU. A video decodes on meanwhile (hideVideo), its frames taken the same way
		unsigned sinkFbo = 0;
		// the manga reader: its pages on the top panel while a chapter is open (Ui asks for it: takeMangaRead)
		MangaReader reader;
		ReaderFetch readerFetch;	// a URL chapter's pages, kept coming while reader is open
		// MangaNet routes a page's answer to the reader before the Ui can take it (the Ui's own answers are untouched)
		mangaNet->setFetchOwner([&readerFetch](MangaFetch::Result& r) { return readerFetch.own(r); });
		bool readerReady = false;
		const Uint32 evReader = SDL_RegisterEvents(1);	// a page decoded: the loop wakes and takes it
		auto drainUnder = [&] {
			if ((!photos.playing() && !reader.isOpen()) || !player.playing() || !player.frameDue()) return;
			SDL_GL_MakeCurrent(top, gl);
			if (!sinkFbo)
			{
				using Gen = void (*)(int, unsigned*);
				using Bind = void (*)(unsigned, unsigned);
				using TexImage = void (*)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
				using FbTex = void (*)(unsigned, unsigned, unsigned, unsigned, int);
				auto genFb = (Gen)SDL_GL_GetProcAddress("glGenFramebuffers");
				auto bindFb = (Bind)SDL_GL_GetProcAddress("glBindFramebuffer");
				auto genTex = (Gen)SDL_GL_GetProcAddress("glGenTextures");
				auto bindTex = (Bind)SDL_GL_GetProcAddress("glBindTexture");
				auto texImage = (TexImage)SDL_GL_GetProcAddress("glTexImage2D");
				auto fbTex = (FbTex)SDL_GL_GetProcAddress("glFramebufferTexture2D");
				if (!genFb || !bindFb || !genTex || !bindTex || !texImage || !fbTex) return;
				unsigned tex = 0;
				genTex(1, &tex);
				bindTex(0x0DE1 /* GL_TEXTURE_2D */, tex);
				texImage(0x0DE1, 0, 0x1908 /* GL_RGBA */, 16, 16, 0, 0x1908, 0x1401 /* GL_UNSIGNED_BYTE */, nullptr);
				bindTex(0x0DE1, 0);
				genFb(1, &sinkFbo);
				bindFb(0x8D40 /* GL_FRAMEBUFFER */, sinkFbo);
				fbTex(0x8D40, 0x8CE0 /* GL_COLOR_ATTACHMENT0 */, 0x0DE1, tex, 0);
				bindFb(0x8D40, 0);
			}
			player.render(16, 16, sinkFbo, true);	// unseen: taken, not drawn (MPV_RENDER_PARAM_SKIP_RENDERING)
			player.reportSwap();
		};

		// the top panel's pixels (RGBA, top row first), the frame drawn again for it: screenshots
		auto captureTop = [&]() -> SDL_Surface* {
			SDL_GL_MakeCurrent(top, gl);
			int w, h;
			SDL_GL_GetDrawableSize(top, &w, &h);
			using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void*);
			auto readPixels = (ReadPixels)SDL_GL_GetProcAddress("glReadPixels");
			if (!readPixels) return nullptr;
			if (reader.isOpen()) reader.draw(w, h);	// a manga chapter's pages
			else renderTop(w, h);	// a video, a photo, or the icon with nothing playing
			std::vector<unsigned char> rgba((size_t)w * h * 4);
			readPixels(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, rgba.data());
			SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
			if (s)
				for (int y = 0; y < h; y++)	// GL's rows go up
					memcpy((unsigned char*)s->pixels + (size_t)y * s->pitch, &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
			SDL_GL_SwapWindow(top);
			shown().reportSwap();
			return s;
		};
		// F + L2 (as in BananaOS's emulators) or, on a computer, F12: both panels, the top over the bottom, as a PNG
		// in the card's screenshots folder
		auto screenshot = [&] {
			SDL_Surface* t = captureTop();
			SDL_Surface* bt = haveUi ? ui.capture(shown()) : nullptr;
			std::string dir = getenv("BP_SCREENSHOTS") ? getenv("BP_SCREENSHOTS")
							: exists("/data/screenshots") ? "/data/screenshots" : getenv("HOME") ? getenv("HOME") : ".";
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
		int topDraws = 0;
		bool logNextTop = false;

		Button held = Button::None;	// a pad's button held down: repeated as a key is
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
			SDL_Event e;
			bool got = SDL_WaitEventTimeout(&e, reader.isOpen() && reader.busy() ? 1 : reader.isOpen() && reader.seekPending() ? 16 : reader.spinning() ? MangaReader::SPIN_MS / 2 : ui.animating() ? 16 : ui.typing() ? 50 : held != Button::None || player.loading() ? 20 : SDL_GetTicks() < freshUntil || levelUntil ? 50 : ui.markMoving() ? 80 : 250);
			while (got)
			{
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
				else if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) handlePadHotplug(e);
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
				else if (e.type == evLevel)
				{
					std::string* what = (std::string*)e.user.data1;
					shown().showLevel(*what == "brightness" ? "Brightness" : "Volume", e.user.code);
					if (haveUi) ui.levelChanged(*what, e.user.code);	// the quick settings' bar follows the keys
					fprintf(stderr, "level: %s %d (at %u ms)\n", what->c_str(), e.user.code, SDL_GetTicks());
					logNextTop = true;
					if (fHeld) fCombo = true;	// F + volume: bananad's brightness
					levelUntil = SDL_GetTicks() + 1500;
					renderPending = true;
					delete what;
				}
				else if (e.type == evNet) ui.netListed((NetListing*)e.user.data1, player);
				else if (e.type == evDur) ui.durationsReady(player);
				else if (e.type == evMangaFetch) mangaNet->onFetchEvent();
				else if (e.type == evMangaWorker) mangaNet->onWorkerEvent();
				else if (e.type == evCover)
				{
					// a cover arrived: into a texture on this thread, and the grid redraws (its serial is in signature)
					if (haveUi) ui.coversReady(coverWorker->take());
					else coverWorker->take();	// (nothing draws them: dropped)
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
						if (ui.pointer(pt, shown())) leave();
					}
				}
				got = SDL_PollEvent(&e);
			}
			if (held != Button::None && SDL_GetTicks() >= heldNext)
			{
				heldNext = SDL_GetTicks() + 150;
				if (ui.repeats(held, shown()) && ui.press(held, shown())) leave();
			}

			player.pollEvents();
			player.checkAudio();
			player.checkSongEnd();
			player.checkDisplay();
			player.checkCpu();
			player.checkNet();
			player.checkLoading();
			photos.pollEvents();
			photos.checkNet();
			photos.checkPhoto();
			photos.checkLoading();
			photos.finished = false;	// (a photo stays: loop-file)
			// the settings changed over a photo (START's menu over it): the player's, written; or the other way (the web)
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
			if (haveUi) ui.tick(shown());
			if (player.quit || ui.quitRequested || ui.restartRequested) running = false;
			// the player's own Stop: the session's (everywhere: a page playing it too); what the session doesn't have (a
			// photo) stops here alone. The web's Stop of a photo, or of a file deleted under it, likewise
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
			if (int ms = ui.takeLevelShown())
			{
				levelUntil = SDL_GetTicks() + ms;
				renderPending = true;
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
				if (mine && shadow.ended().reject.empty()) { if (shadow.session().index < 0) stopPlaying(StopWhy::End); }
				else if (!player.advance()) stopPlaying(StopWhy::End);
			}
			// the place is kept now and then too: a battery that runs out, a power-off after sleeping
			bool woke = slept.exchange(false);
			if (woke) freshUntil = SDL_GetTicks() + 2000;	// the panels back from a sleep: drawn again
#ifdef BP_WEB
			// the server was frozen with the rest: the pages watching get the state afresh
			if (woke) webForce = true;
#endif
			// a photo has no Play: an animated one paused by the lid goes on at the wake (a video waits for A)
			if (woke && photos.playing()) photos.unpause();
			if (woke) shadow.woke();
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
						if (*ui.menuTabName()) printf("press %zu: menu=%s\n", pressed, ui.menuTabName());
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
				std::string shown = elsewhere ? std::to_string(ui.pagePlaying()) + std::to_string(sound) + std::to_string(waiting) + name +
					(player.loaded ? "|L|" : "|-|") + player.currentPath() : "";
				if (shown != elsewhereShown)
				{
					player.showElsewhere(elsewhere, ui.pagePlaying(), name, waiting, G_LAPTOP_ROWS, G_LAPTOP.h);
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
				shadow.pass(player, ui, [&webServer](const std::string& tab) { return webServer && webServer->running() && webServer->tabConnected(tab); });
#else
				shadow.pass(player, ui, nullptr);
#endif
				// the session stopped its item (a Stop, the end of the list, its file deleted): the handheld lets go too
				if (int u = shadow.takeUnload()) stopPlaying(u == 2 ? StopWhy::End : StopWhy::Web);
			}
			Uint32 now = SDL_GetTicks();
			// a chapter or equalizer card shown: it goes the way the volume card does, after its time
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
			bool refresh = now - refreshedAt >= (now < freshUntil ? 100u : 2000u);
			if (refresh)
			{
				refreshedAt = now;
				topDark = false;	// the black of no video drawn again too
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
			// the manga reader: a chapter asked for, the bottom panel's commands, what it shows told back
			if (haveUi)
			{
				Ui::MangaReadRequest rq;
				if (ui.takeMangaRead(rq))
				{
					readerFetch.end();
					SDL_GL_MakeCurrent(top, gl);
#ifdef BP_GLES
					if (!readerReady) readerReady = reader.init(true, evReader);
#else
					if (!readerReady) readerReady = reader.init(false, evReader);
#endif
					int w, h;
					SDL_GL_GetDrawableSize(top, &w, &h);
					ui.readerStatus = {};
					if (!readerReady) ui.readerStatus.error = "The reader couldn't start";
					else if (!rq.pageUrls.empty())
					{
						// a source's or a server's chapter: the pages come through MangaNet into the Ui's own page
						// cache (its Clear cache empties the same folder)
						if (opts.stateDir.empty()) ui.readerStatus.error = "No state folder for the chapter's pages";
						else if (!mangaNet || !player.settings.mangaOn)
							ui.readerStatus.error = "The manga reader's network side is off";
						else
						{
							mangaNet->start();
							PageCache& cache = ui.mangaPageCacheRef();
							uint64_t limit = mangaCacheBytes(player.settings.mangaCacheSize);
							if (!cache.open(opts.stateDir + "/manga/cache", limit))
								ui.readerStatus.error = "The page cache couldn't be opened";
							else if (!reader.open(rq.pageUrls, rq.pageHeaders, cache, rq.path, rq.page, w, h, rq.mode,
										 rq.fit))
								ui.readerStatus.error = reader.error();
							else
								readerFetch.begin(mangaNet, &cache, &reader, rq.pageUrls, rq.pageHeaders, limit);
						}
					}
					else if (!reader.open(rq.path, rq.archive, rq.page, w, h, rq.mode, rq.fit))
						ui.readerStatus.error = reader.error();
				}
				for (auto& c : ui.readerCmds)
				{
					if (c.kind == Ui::ReaderCmd::Close)
					{
						readerFetch.end();
						reader.close();
					}
					else reader.command((int)c.kind, c.v, c.ms, c.v2, c.v3);
				}
				ui.readerCmds.clear();
				// a URL chapter's pages: requests reordered and the free slots filled (the answers themselves come
				// through MangaNet's fetch owner above)
				if (reader.isOpen()) reader.tick(now);
				if (reader.isOpen()) readerFetch.pass();
				if (reader.isOpen())
				{
					// the row set while a chapter is open reaches the reader too (the fit comes as its own command)
					reader.setPreload(player.settings.mangaPreload);
					ui.readerStatus.open = true;
					ui.readerStatus.loading = reader.loading();
					ui.readerStatus.error = reader.error();
					ui.readerStatus.failedHere = reader.failedInView();
					ui.readerStatus.atEnd = reader.atEnd();
					ui.readerStatus.page = reader.page();
					ui.readerStatus.pages = reader.pages();
					ui.readerStatus.mode = reader.mode();
					ui.readerStatus.zoomPct = reader.zoomPct();
					ui.readerStatus.fullscreen = reader.fullscreen();
					ui.readerStatus.rotation = reader.rotation();
					// fullscreen: the bottom panel's share of the view, as fractions of the panel, and the pixels it needs
					Ui::ReaderBottom rb;
					rb.on = reader.fullscreen();
					rb.gen = reader.chapterGen();
					if (rb.on)
					{
						int w, h;
						SDL_GL_GetDrawableSize(top, &w, &h);
						for (auto& b : reader.bottomBoxes())
						{
							rb.boxes.push_back({b.page, b.x / w, b.y / h, b.w / w, b.h / h});
							int pw = 0, ph = 0;
							if (auto px = reader.pixels(b.page, pw, ph)) rb.pages.push_back({b.page, pw, ph, px});
						}
					}
					ui.readerBottom = std::move(rb);
					// the pages grid's thumbnails: every page the reader has decoded, ~96 px wide RGBA.
					// always filled while the reader is open (the Ui only uses them when the grid shows);
					// shared_ptr copies are cheap, and absent pages are placeholders in the grid
					Ui::ReaderThumbs rt;
					rt.on = true;
					rt.gen = reader.chapterGen();
					for (int pi = 0; pi < reader.pages(); pi++)
					{
						int tw = 0, th = 0;
						if (auto tp = reader.thumbPixels(pi, tw, th))
							rt.pages.push_back({pi, tw, th, tp});
					}
					ui.readerThumbs = std::move(rt);
				}
				else
				{
					readerFetch.end();
					ui.readerStatus.open = false;
					ui.readerBottom = {};
					ui.readerThumbs = {};
				}
				if (woke) reader.redrawSoon();
			}
			// with nothing playing the top panel is drawn on a change only, and mpv may not have the level card yet
			// when it's asked to show: drawn again and again while the card is up
			drainUnder();
			if (reader.isOpen())
			{
				// the pages, not mpv's picture (what plays goes on under them: drainUnder)
				renderPending = false;
				if (reader.frame(now) || refresh)
				{
					SDL_GL_MakeCurrent(top, gl);
					int w, h;
					SDL_GL_GetDrawableSize(top, &w, &h);
					reader.draw(w, h);
					SDL_GL_SwapWindow(top);
				}
			}
			else if (renderPending.exchange(false) || refresh || (!shown().playing() && (!topDark || levelUntil)))
			{
				SDL_GL_MakeCurrent(top, gl);
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				if (!shown().playing())
				{
					// nothing playing: mpv draws its black with the icon (and a level card if one is up)
					int r = player.render(w, h);
					if (r < 0 && topDraws < 5) fprintf(stderr, "top: mpv couldn't draw (%d) at %u ms\n", r, SDL_GetTicks());
					SDL_GL_SwapWindow(top);
					player.reportSwap();
					topDark = true;
					// in the log: the top panel drawn with nothing playing (the first time, and after a level card),
					// to find where a black top panel comes from
					if (topDraws++ == 0 || logNextTop)
						fprintf(stderr, "top: drawn idle %dx%d at %u ms (draw %d)%s\n", w, h, SDL_GetTicks(), topDraws,
							logNextTop ? " with the level card" : "");
					logNextTop = false;
				}
				// a new frame, or the same one again (paused, or a refresh); a photo's over what plays
				else if (shown().frameDue() || refresh)
				{
					shown().render(w, h);
					SDL_GL_SwapWindow(top);
					shown().reportSwap();
				}
			}
			if (haveUi)
			{
				// a slow bottom panel, in the log: what a press waits for on the handheld (the folder it was in)
				Uint32 t0 = SDL_GetTicks();
				ui.draw(shown(), refresh);
				Uint32 took = SDL_GetTicks() - t0;
				if (took >= 30)
					fprintf(stderr, "timing: bottom panel drawn in %u ms (%s)\n", (unsigned)took,
						isNetPath(ui.browseFolder()) ? "network folder" : ui.browseFolder().empty() ? "player" : "browser");
			}
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
		sleepWatch.stop();
		levelWatch.stop();
		bottomTouch.stop();
		// a playlist's last edits (they wait a second to be written)
		if (haveUi) ui.flushPlaylist(player);
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
