#pragma once
#include "battery.h"
#include "browser.h"
#include "input.h"
#include "panel.h"
#include "player.h"
#include "touch.h"
#include "updater.h"
#include "backlight.h"
#include "volume.h"
#include "slider.h"
#include "kitlayout.h"
#include "netstore.h"
#include "networker.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <algorithm>
#include <array>
#include "keyboard.h"
#include "playlists.h"
#include "discovery.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

namespace bp
{
	class WebServer;
	class DurationWorker;
	struct PaneSpec;	// previewpane.h

	// the on-screen keyboard's field for a repository's URL (Ui's Sources page; the form's are in ui_internal.h)
	inline constexpr int SRC_URL_FIELD = 62;
	// the on-screen keyboard's field for a source's search (apart from the form's, a preset's and a playlist's)
	inline constexpr int MANGA_SEARCH_FIELD = 63;	// (62: the repository URL)
	// the saved servers' form (ui_manga_servers.cpp): its four typed fields. 62 and 63 are the tabs' own.
	inline constexpr int MGSRV_NAME_FIELD = 64, MGSRV_URL_FIELD = 65, MGSRV_USER_FIELD = 66,
		MGSRV_PASS_FIELD = 67;
	// a source filter's text value (ui_manga_filters.cpp); 60-67 are the other fields
	inline constexpr int MANGA_FILTER_FIELD = 68;

	// The bottom panel: what is playing and where, the transport (previous, -5 s, play, +10 s, next), the choices
	// (audio, subtitles, aspect, speed, settings) and their pages. Laid out on 640x480 (the RG DS's panel) and scaled
	// to the panel it is on.
	class Ui
	{
	public:
		static constexpr int W = 640, H = 480;
		// the panel's type by size index (ui_internal.h's F_*: three of every page's, then the redesign kit's)
		static constexpr int FONT_COUNT = 12;
		// a small picture: rows of cells, '#' drawn (ui.cpp's G_*)
		struct Glyph { const char* const* rows; int h; };

		// fontsDir: assets/fonts, the panel's font and the subtitle preview's
		Ui();
		bool init(SDL_Window* window, const std::string& fontsDir);
		~Ui();

		// true when the player should leave
		bool press(Button b, Player& p);
		// a button let go (keys and the pad; the scripted steps right after their press): only A cares, for its hold
		// while a page plays the file
		void release(Button b, Player& p);
		// a held button that repeats here (seeking, moving in a list, photo to photo)
		bool repeats(Button b, const Player& p) const;
		// called each turn of the loop: a flicked photo glides on
		void tick(Player& p);
		// the left stick's up/down push (SDL_CONTROLLER_AXIS_LEFTY, -32768 up … 32767 down), every pass while a
		// reader is open: the text viewer or the pages scroll or pan while it is held; elsewhere it does nothing
		void stickAxis(int value, Player& p);
		// something moves by itself (a flick, a playlist's rows scrolling under a dragged one): the loop should come
		// round often
		bool animating() const;
		// the header's playing mark moves (something plays behind the browser, the bottom panel lit): the loop comes
		// round a few times a second for its frames
		bool markMoving() const { return markAnim && panel.on(); }
		// the playlist edited in the browser, written now (its edits are written a second after they stop): before
		// quitting, or stopping what plays
		void flushPlaylist(Player& p);
		// a finger (or, on a computer, the mouse) on the bottom panel; true when the player should leave
		bool pointer(const Pointer& pt, Player& p);

		// the browser: the page shown with no video, opened at the last folder (Remember last folder) or Home
		// folder: where to open (a folder given on the command line), else the remembered one or Home
		void startBrowser(Player& p, const std::string& folder = "");
		// back to the browser from a video: the folder of the video last played, that video selected (whatever
		// Remember last folder says: that's for the next launch). from: what it played from (read before stop());
		// a playlist goes back to that playlist, the cursor on the item (index: the player's, in the order played)
		void showBrowser(Player& p, const std::string& lastPlayed, const PlaySource& from = {}, int index = -1);
		// the saved playlists' folder (<stateDir>/playlists); empty: there are none to have
		void setStateDir(const std::string& stateDir);
		// the row the browser's cursor is on (Home: the card or the tile it's on), for test runs
		std::string browseSelection() const;
		// the player shown (a pick, Now playing, SELECT, a photo...): where it was opened from is kept, for the way back
		void showPlayer();
		// what plays (a song or a video) has its screen as the bottom panel's base: Main, or a page or the menu over it.
		// False while it plays behind the browser (the player left with B, "<" or Back)
		bool playerShown(const Player& p) const { return p.playing() && !bgPlay; }
		// the player's own pages: over an open reader (the mini bar tapped while reading), the top panel shows what
		// plays, not the pages; B comes back to the reader, and its pages to the top panel
		bool topShowsPlayer() const
		{
			return page == Page::Main || page == Page::Info;
		}
		// About's support QR, large on the top panel: Y on About, or a tap on the small code there, puts it up (the
		// top panel's own picture, as a reader's pages are); B, Y or a tap takes it down, and leaving About closes it
		bool aboutQrUp() const { return aboutQr && page == Page::About; }
		static lay::Box aboutQrRect() { return {24, 352, 84, 84}; }	// the small code at About's foot (drawAboutSupport)
		// kit-new (ui_kit_new.cpp): the Preview pane the top panel shows while nothing plays (previewpane.h), false
		// for none: the Theme row's (Settings › Power & screens), the test gallery's. topscreen.cpp asks every pass
		bool topPane(PaneSpec& out) const;
		// test only (--press gallery:new, gallery:new:2): kit-new's components drawn in place of the page (0: off)
		int galleryNew = 0;
		void drawGalleryNew();
		// the player's own Stop (X's More sheet, START › Playing › Stop, the mini bar's menu), once: main stops what plays
		bool takeStopPressed() { bool r = stopPressed; stopPressed = false; return r; }
		// The Stop the Manga guard asked for (ext-section-video): the page stays where it is (Manga), rather than
		// going back to where the video was opened from. Once, as the stop itself.
		bool takeStopKeepsPage() { bool r = stopKeepsPage; stopKeepsPage = false; return r; }
		// the mini bar's tap over a photo: the photo closes and the player of what plays takes the screen (once; main
		// does it: only it can close the photo viewer)
		bool takePlayerOverPhoto() { bool r = playerOverPhoto; playerOverPhoto = false; return r; }
		// the player the mini bar works on: main's (songs and videos), set by main, also while a photo is shown. The
		// photo viewer is a Player of its own and a page of its own: the bar never plays the photo, only what goes
		// on under it. Without main's (a test, a caller that has none) it is the player it is given
		Player* mainPlayer = nullptr;
		// what plays stopped (any Stop, the end of the list): a page playing the file has nothing held here any more
		// (the page plays on: not told)
		void stopped() { playHoldAt = 0; }
		// Back from the player (B, "<", Back; its Stop; a photo closed): where it was opened from, as it was (the page,
		// the folder, the cursor and the scroll; Home, a playlist, Settings...), what plays going on behind it. Never
		// opened from there (started with a file), or Show in folder asked for: the browser on lastPlayed (showBrowser)
		void backFromPlayer(Player& p, const std::string& lastPlayed, const PlaySource& from = {}, int index = -1);
		// opened behind the browser (a PlayRequest's stay): the browser stays the base page
		void playBehind() { bgPlay = true; }
		bool browsing() const { return page == Page::Browse; }
		std::string browseFolder() const;
		// a video picked in the browser, once: the caller opens it
		// the browser's rows as they are listed (label, then note; Home: the Recent cards, then the tiles), for test runs
		std::vector<std::string> browseLabels() const;
		// the Extensions section's page (Page::ExtSection): for test runs (the state line and nothing else)
		bool extPageShown() const { return page == Page::ExtSection; }
		// the Info or About page's lines as they are drawn, for test runs
		std::vector<std::string> pageLines() const
		{
			std::vector<std::string> out;
			for (auto& l : infoLines) out.push_back(l.header ? "[" + l.label + "]" : l.label + ": " + l.text);
			return out;
		}
		std::string takeOpenRequest() { std::string r; r.swap(openRequest); return r; }
		// a playlist picked in the browser (an item, Play, Shuffle play), once: the caller opens it (openPlaylist)
		// names: a network folder's listing (open's folderNames); stay: opened behind the browser, not on the player
		struct PlayRequest { std::vector<std::string> paths; int index = 0; PlaySource source; std::vector<std::string> names; bool stay = false; };
		bool takePlayRequest(PlayRequest& out)
		{
			if (playRequest.paths.empty()) return false;
			out = std::move(playRequest);
			playRequest = {};
			return true;
		}
		// ---- the top panel's page reader: its channel (the request, the commands, the status) and the views the PDF
		// viewer and the Manga reader share (the pages grid, fullscreen, the zoom), all in ui_reader.h / ui_reader.cpp ----
		// (public: main talks to it for the top panel's MangaReader)
		struct PageReader;
		std::unique_ptr<PageReader> pageReader;
		// ---- extensions and the manga reader: an area (Area), all of it in ui_manga.h, ui_manga*.cpp, ui_ext.cpp ----
		// The Extensions section's page, Manage extensions and the Manga pages. (public: main and the dashboard's routes
		// reach its store, caches, workers)
		struct Manga;
		std::unique_ptr<Manga> manga;
		// ---- Files' document viewers: an area (Area, below), all of it in ui_docs.h / ui_docs.cpp ----
		// (public: main talks to it for the top panel's text viewer and the PDF viewer's pan area)
		struct Docs;
		std::unique_ptr<Docs> docs;
		// ---- the browser (Home, Files, Playlists, Network, All recent): an area (Area), ui_browse.h; its code in
		// ui_browse.cpp, ui_fileops.cpp, ui_forms.cpp ----
		// (public: the dashboard's routes reach its folder, rows and form)
		struct Browse;
		std::unique_ptr<Browse> browse;
		// the playlists: a plain struct the browser owns (browse->playlists), ui_playlists.h / ui_playlists.cpp
		struct Playlists;
		// Home and All recent: a plain struct the browser owns (browse->homeView), ui_home.h / ui_home.cpp
		struct Home;
		// a document from Files: opened in its viewer; false when the path isn't one
		bool openDocument(const std::string& path, Player& p);
		// started with no file: the video's Quit closes it (back to the browser)
		bool browserMode = false;
		// Settings' Quit Banana Player: out of the player, from the browser or a video
		bool quitRequested = false;
		// the built-in updater (main's), and "Restart now": out with Updater::RESTART_CODE (launch.sh starts again)
		Updater* updater = nullptr;
		bool restartRequested = false;
		// the saved network connections and the thread that lists their folders (main's)
		NetStore* netStore = nullptr;
		NetWorker* netWorker = nullptr;
		// a network folder's listing, from the worker (forwarded by main); dropped if the browser has moved on
		void netListed(NetListing* listing, Player& p);
		// the local file rows' durations, probed on main's worker (null: only the ones already known are shown), and
		// its answers (main forwards its event): into the rows' notes
		DurationWorker* durWorker = nullptr;
		void durationsReady(Player& p);
		// the on-screen keyboard is up: its caret blinks (the loop wakes often enough to draw it)
		bool typing() const { return kbOpen; }
		// Find on the network: discovery runs here, its hosts sent to hostFound (on the main thread: main forwards them)
		Discovery* discovery = nullptr;
		std::function<void(const FoundHost&)> onFound;
		void hostFound(const FoundHost& h, Player& p);
		// a network folder's file names, as last listed (none if it wasn't): where a file picked there finds its
		// subtitles, its cover and the files that play after it
		std::vector<std::string> netFolderNames(const std::string& dir) const;
		// F alone: quick settings over whatever's on the bottom panel (volume, brightness, Same for both screens, Quit);
		// F again, B or a tap outside closes them
		void toggleQuick(Player& p);
		void openVariantsGallery(int page);	// test only (--press gallery:variants[:PAGE]): ui_kit_variants.cpp
		bool quickShown() const { return findOverlay("quick") != nullptr; }
		// a level from bananad (its keys: "volume" or "brightness", percent): the quick settings show it
		void levelChanged(const std::string& what, int percent) { if (what == "volume") volumePct = std::min(100, percent); }	// (the handheld's own: past 100 is the player's gain)

		// the bottom panel's backlight: off from its button, on again at the next touch
		bool screenOn() const { return panel.on(); }
		bool touchIsLocked() const;	// (the player screen's lock: PlayerPage, ui_player.h)
		// Settings open (START's page): for test runs
		bool settingsPageOpen() const { return page == Page::Settings; }

		// The web dashboard (BP_WEB builds): main's server, null till it's first turned on, and its switch (start or
		// stop it, on the main thread). The routes reach the rest through WebPlayerGlue and WebPlaylistGlue
		// (src/web/api_*.cpp), on the main thread too
		WebServer* web = nullptr;
		std::function<void(bool)> webPower;
		// Page::Web open: a browser signing in needs no toast there (the page counts it)
		bool webPageShown() const { return page == Page::Web; }
		// a folder changed from the web (an upload, a delete): listed again if the browser shows it
		void folderChanged(const std::string& dir, Player& p);
		// the theme swapped (theme::setTheme): the SDL copies of its colours taken again, and what was drawn once with a
		// colour baked in (the PIN's digits, the support QR's address) dropped to be drawn again; the theme is in signature()
		void themeChanged();
		// a volume change made here (the web's, the quick settings'): how long main shows the level card (as
		// takeCardShown), once, with its label and percent; main puts it on whatever the top panel shows
		int takeLevelShown(std::string& label, int& percent) { int ms = levelMs; levelMs = 0; label = levelLabel; percent = levelPct; return ms; }
		// the web's Stop: main closes what plays (back to the browser), once
		bool takeStopRequest() { bool r = stopRequest; stopRequest = false; return r; }
		// the handheld's volume, percent (-1 unknown): read once, then followed (bananad's keys, the quick settings, the web)
		int volumeLevel();
		// the volume as heard, 0 to Max volume: the handheld's (0-100), or past 100 the player's own gain over its 100%
		int volumeTotal(Player& p);
		// the volume set to v as heard (the web's bar, the volume keys past 100%): the handheld's to min(v, 100), the
		// gain to the rest; force: the handheld's set even if it seems at that already (bananad's own key press lowered
		// it). The first time past 120% in a run, a warning
		void setVolumeTotal(Player& p, int v, bool force = false);
		bool loudWarned = false;
		// the browser tab playing the sound: its own volume as it last said (POST /api/browser-volume), -1 not known.
		// The handheld's volume keys move it while the tab is the output; the handheld's own is left as it was
		int browserVol = -1;
		void keepHandheldVolume(int v) { if (v >= 0 && volume.present()) volume.set(v); if (v >= 0) volumePct = v; }
		// the playlists' store serial (-1 none): the web's lists follow it
		int playlistSerial() const;
		// Like (the built-in Liked playlist). likeable: something plays that can be liked (a song or video, not a photo)
		// and there's a state folder. likedNow: it is liked (cheap: a cached set). setLikedNow(want) / toggleLike set it
		// and return what it is now; false-able only by a failed write (osd says so). The pages that show Liked follow.
		bool likeable(const Player& p) const;
		bool likedNow(const Player& p) const;
		bool setLikedNow(Player& p, bool on);
		bool toggleLike(Player& p) { return setLikedNow(p, !likedNow(p)); }
		// the same for any path (the web's Like, a browser row): title/duration "" / -1 when not known
		// at, dropped: PlaylistStore::setLiked's (the web's Undo, and the one a full Liked lost)
		bool setLikedPath(Player& p, const std::string& path, const std::string& title, double duration, bool on, int at = 0,
			std::string* dropped = nullptr);
		bool plIsLiked(const std::string& file) const;
		// a page in a browser plays what plays here (the Session's output is that tab): the handheld holds it paused
		// a file job (copy, move, delete, a network copy of a book) is running: the idle sleep waits for it
		bool fileJobActive() const { return (bool)fileJob; }
		bool pageIsOut() const;
		// how this page can take a page's playback back, for the top panel's card: 1 hold A (the player), 2 elsewhere
		// (A does something else there: the card says the player can)
		int takeOverHint(const Player& p) const;
		std::string outputName() const;	// where it plays now: "Handheld", or the page's name ("Chrome on Mac")
		std::string pageName() const;	// the output page's name, as it gave it ("Browser" unknown)
		// while a page plays the file, the paused copy here stays where it was (no seek per report: that decodes a frame):
		// the bar, times and chapter show the session's place (posNow); a seek made here is the session's Seek
		bool predicting(const Player& p) const;
		double shownPosition(const Player& p) const;
		int shownChapter(const Player& p) const;
		void noteSeek(const Player& p, double t);	// a seek made here meanwhile: the session's Seek (the page follows)
		// a page plays, and the handheld's A is its (Play / Pause of the session)
		bool pageDrives(const Player& p) const;
		// A (or the Play button) while a page drives: a short press plays or pauses the page (the session's Play /
		// Pause, the state wanted: a press seen twice can't undo itself); held PLAY_HOLD_MS it plays here (takeOver)
		static constexpr Uint32 PLAY_HOLD_MS = 600;
		Uint32 playHoldAt = 0;
		bool playHoldTouch = false;	// the hold is a finger's on the Play button (else A's)
		bool pagePlaying() const;	// the page plays (the session's wish)
		void toggleBrowser(Player& p);
		void takeOver(Player& p);
		// an audio track or subtitles (id 0: off) chosen, as the Audio & Subtitles page chooses one (and the web): the
		// language remembered as the settings say, and "Audio: <its label>" over the top panel. false: no such track
		bool chooseTrack(Player& p, TrackType type, int64_t id);

		// redraws when what it would show changed since the last frame
		void draw(const Player& p, bool force = false);
		// the bottom panel's pixels (RGBA, ABGR8888), drawn afresh: screenshots; the caller frees it
		SDL_Surface* capture(const Player& p);

	private:
		// "at 12:03": where a video was left (a song: "")
		std::string resumeNote(const std::string& path, Player& p);
		friend struct WebPlayerGlue;
		friend class SessionHost;
		friend struct WebPlaylistGlue;
		enum class Page { Main, Settings, Browse, Info, About, Sound, Web, Manga, ExtSection, Extensions, MangaSeries,
			MangaRead, MangaSettings, MangaSources, DocText, DocPages };
		using Rect = lay::Box;	// (kitlayout.h: the kit's layout maths give and take the same box)
		// An overlay: a dialog or a sheet over the page (ui_overlay.cpp; docs/notes/overlay-stack.md). While it is on
		// the stack it gets the keys and the touches (the topmost one only: it is modal), is drawn over the page (all
		// of them, the oldest first) and has its state in signature(): press, pointer, renderPage and signature ask
		// the stack, so a new dialog is wired in nowhere else. One is written as a struct nested in Ui (declared
		// here, defined in its own .cpp), which lets it use Ui's drawing and state through `ui`.
		struct Overlay
		{
			Ui& ui;
			bool done = false;	// closed: taken off the stack once the call that closed it is over
			explicit Overlay(Ui& u) : ui(u) {}
			virtual ~Overlay() = default;
			// what it is ("light", "alert"): one of a kind is open at a time, and signature() says which
			virtual const char* name() const = 0;
			// as Ui::press and Ui::pointer (x, y in the panel's 640x480 units): true leaves the player
			virtual bool key(Button b, Player& p) = 0;
			virtual bool pointer(const Pointer& pt, int x, int y, Player& p) = 0;
			virtual void draw(const Player& p) = 0;
			// everything draw() shows that Settings doesn't hold (the cursor, a level read from elsewhere): the
			// panel is drawn again only when this changes
			virtual std::string signature(const Player& p) const = 0;
			void close() { done = true; }
		};
		std::vector<std::unique_ptr<Overlay>> overlays;	// the stack: the last is on top
		void pushOverlay(std::unique_ptr<Overlay> o);
		Overlay* topOverlay() const;	// (null: none open)
		Overlay* findOverlay(const char* name) const;
		void dropClosedOverlays();
		bool overlayKey(Button b, Player& p);
		bool overlayPointer(const Pointer& pt, int x, int y, Player& p);
		void drawOverlays(const Player& p);
		std::string overlaySignature(const Player& p) const;
		// An area: a part of the app with pages of its own (the document viewers: Docs, ui_docs.h), its state and
		// its handlers in one struct instead of in Ui (docs/notes/ui-areas.md). While one of its pages is
		// shown, press, pointer, tick, renderPage, signature and animating ask it, so a feature inside an area is
		// written in the area's own header and .cpp, and a new area is wired into those six nowhere. One is written
		// as a struct nested in Ui (declared in ui.h by name only, defined in its own header), which lets it use
		// Ui's drawing and state through `ui`; the Player comes with each call, as for every Ui method.
		struct Area
		{
			Ui& ui;
			explicit Area(Ui& u) : ui(u) {}
			virtual ~Area() = default;
			virtual bool has(Page pg) const = 0;	// the page is one of its own
			// as Ui::press and Ui::pointer (x, y in the panel's 640x480 units): true leaves the player
			virtual bool key(Button b, Player& p) = 0;
			virtual bool pointer(const Pointer& pt, int x, int y, Player& p) = 0;
			virtual void tick(Player&) {}	// each turn of the loop while its page shows
			virtual void draw(const Player& p) = 0;
			// everything draw() shows that Settings doesn't hold: the panel is drawn again only when this changes
			virtual std::string signature(const Player& p) const = 0;
			virtual bool animating() const { return false; }	// something of its moves by itself (Ui::animating)
			// its touches come before the updater's pop-up and the mini bar (a page that always had them so); the rest after
			virtual bool pointerFirst() const { return false; }
			// its downs come late: after the header's "<" and the empty page's button (its moves and lifts
			// still come where the others' do). Manga's pages took them so (asked per page: the reader's come early)
			virtual bool pointerDownLate() const { return false; }
		};
		std::vector<Area*> areas;	// (each owned by its own member, `docs`, `manga`...; listed in Ui's constructor, ui.cpp)
		Area* area() const;	// the one whose page is shown (null: a page that is still Ui's own)
		struct Row { int64_t id; std::string label; bool selected; std::string note = ""; };

		std::vector<Row> rows(const Player& p) const;
		// a list page's own keys (the tracks, Up next, the browser): UP/DOWN, a page with LEFT/RIGHT, A chooses, B home
		bool listKey(Button b, Player& p);
		// a finger down on a list page's rows: the row under it the cursor, the touch begun (the drag or the tap is
		// Ui::pointer's)
		bool listDown(int x, int y, Player& p);
		// The file's tracks and picture size, as the old Tracks page listed them: View's tabs choose them (Ui::ViewSheet
		// and SongViewSheet ask rows() with tracksTab pointed at what they show). That page is gone: these are the
		// lists' terms, not a page's.
		enum TracksTab { TRACKS_SUBS, TRACKS_AUDIO, TRACKS_PICTURE };
		int tracksTab = TRACKS_SUBS;
		TrackType trackType() const { return tracksTab == TRACKS_AUDIO ? TrackType::Audio : TrackType::Sub; }
		// L2/R2: the chapter before or after, over what plays (a page playing it takes the Session's Seek; the List
		// sheet's Chapters tab follows). Ui::press and the List sheet's open state both call it
		void chapterStep(int dir, Player& p);
		// Up next (the List sheet's first tab): the list that plays, in its own order (the Session's queue: Shuffle
		// reorders nothing shown), the one playing marked; A plays a row as a pick from that list does (the Session's
		// Open: Shuffle and Loop stay as set)
		std::vector<std::string> upNextList(const Player& p, int& current) const;
		void playUpNext(int i, Player& p);
		// the same chapter step, for main's lid-shut keys: they act on what plays, never on the page left open
		// (public: main calls it; see the public block by places())
		void choose(int i, Player& p);
		// Settings opens on its groups (setGroup -1); A or a tap opens one, B goes back to them. The order is the spec's
		// (docs/redesign/src/screens.js, "settings"): Playback, Subtitles, Music & sound, Photos, Reading, Files &
		// history, Power & screens, Web dashboard, Extensions, Updates, About (Web dashboard, Extensions and About are
		// tiles like the groups but pages of their own: openGroup and stepGroup open Page::Web, Page::Extensions and
		// Page::About)
		enum SetGroup { GROUP_PLAYBACK, GROUP_SUBS, GROUP_MUSIC, GROUP_PHOTOS, GROUP_READING, GROUP_FILES, GROUP_SCREEN,
			GROUP_WEB, GROUP_EXTENSIONS, GROUP_UPDATES, GROUP_ABOUT, GROUPS };
		// the page is an area (SettingsPage, ui_settings.h): the rows, changeSetting, openGroup, its drawing and keys.
		// Here stay: the groups' ids, the rows' ids and the list's measures (Manga's settings page and the tables use
		// them) and setGroup, the open group, which showPlayer's, the dashboard's and Browse's way-back records keep
		int setGroup = -1;
		struct SettingsPage;
		std::unique_ptr<SettingsPage> settingsPage;
		Page home(const Player& p) const { return playerShown(p) ? Page::Main : Page::Browse; }
		// the browser's three sections, opened from Home's shortcuts and the section markers: Files (the cards),
		// Playlists (the saved ones, then their items) and Network (the saved connections, then their folders).
		// Extensions is the rail's section now (Page::ExtSection), not a folder of the browser's. L/R step to the one
		// before or after (no wrap), at its top; the section shown and where each was left are the browser's
		// (Browse::browseTab, tabDir)
		enum BrowseTab { TAB_LOCAL, TAB_PLAYLISTS, TAB_NETWORK, BROWSE_TABS };

		// ---- the redesign's component kit (ui_kit.h / ui_kit.cpp; docs/notes/2026-11-16-redesign-kit.md) ----
		// the pieces the redesigned screens are drawn from (key caps, keys, chips, line keys, the bar, sheets, rows):
		// each takes its inputs and draws, and holds no screen's state
		struct Kit;
		struct KitNew;	// kit-new's components, beside Kit (ui_kit.h's kit-new block)
		std::unique_ptr<Kit> kit;
		// the redesigned section pages' shared frame (ui_shell.h): the rail, the header, the hints and the mini bar.
		// Not an Area of its pages (Browse and Settings keep their keys and touches): Ui asks it directly
		struct Shell;
		struct ShellHead;
		std::unique_ptr<Shell> shell;
		// the More sheet (ui_more.cpp): a bottom sheet of tiles and rows, fed a list each time it is drawn (MoreSpec,
		// ui_kit.h), as an overlay. X on the player opens the player's (PlayerPage::moreSpec)
		struct MoreItem;
		struct MoreSpec;
		struct MoreSheet;
		void openMore(std::function<MoreSpec(const Player&)> build);
		// test only: every ui_kit.h "KIT VARIANTS" piece on one page, for a `shot:` (--press gallery:variants[:PAGE], 1..10)
		struct VariantsGallery;
		// ---- the player screen (Page::Main: a video, a song; a photo's viewer is still here in Ui): an area,
		// ui_player.h / ui_player.cpp ----
		struct PlayerPage;
		struct SongViewSheet;	// (ui_player.cpp: a song's chip's sheet, a table of it too)
		std::unique_ptr<PlayerPage> playerPage;
		struct PlayOnSheet;
		struct ListSheet;
		struct ViewSheet;
		struct AdjustSheet;	// (ui_player.cpp: Picture's Adjust page)

		// the readers' Brightness sheet (ui_light.cpp): the screens' backlight, Extra dim, the reader's filter
		// (an overlay: LightSheet, pushed by openLight)
		struct LightSheet;
		void openLight();
		// the same kind (ui_overlay.cpp): the browser-check message ("mangaerr", from Ui::Manga's mangaErrCheck),
		// Files' Info card ("info", from openFileInfo) and the quick settings ("quick", from toggleQuick). Each is a thin
		// overlay over the code it had (drawMangaErr, drawFileInfo, quickKey/quickPointer/drawQuick)
		struct MangaErrDialog;
		struct InfoSheet;
		struct QuickSheet;
		void openMangaErr();
		void openInfo();
		void openQuick();
		int& lightFilter(Settings& s) const;	// the reader's own filter: the text viewer's, the PDF/comic viewer's, Manga's
		// what the reader's Brightness button says while Extra dim or a filter is on ("Warm · 30%"); empty: neither
		std::string lightNote(const Player& p) const;
		bool showHidden = false;	// the setting, kept here for drawBrowseMenu (no Player there)
		bool mangaOn = false;		// the manga reader's switch, kept here for tileSub (no Player there)
		// a recent file played (as All recent's rows play one), from its start, shown in its folder (Files or Network)
		void openFile(const std::string& path, Player& p);
		void showInFolder(const std::string& path, Player& p);
		std::string stateDirPath;	// (setStateDir's) the manga store's, the readers' places and the playlists' folders are under it
		// ---- Files' own file operations (ui_fileops.cpp) ----
		// Copy / Move: the item is held (clip) while you go to the folder it goes in, and pasted there (SELECT, or the
		// bar's button); a copy, or a move to the other card, runs on a thread of its own with this panel showing
		// its progress until it ends (nothing else answers meanwhile but Cancel)
		enum { CLIP_NONE, CLIP_COPY, CLIP_MOVE };	// what the clip holds (Browse::clipOp), what a job does
		struct FileJob;
		std::shared_ptr<FileJob> fileJob;
		static void runFileJob(FileJob* j);	// on the job's thread
		std::string fileJobSignature() const;
		int fileJobPressed = -1;
		// Info (Files' Y menu, the manga reader's series menu): a sheet of what a file or folder is, any key closes it
		// (the InfoSheet overlay: openFileInfo pushes it)
		std::string infoTitle;
		std::vector<std::pair<std::string, std::string>> infoRows;
		void openFileInfo(const std::string& path, const std::string& title, std::vector<std::pair<std::string, std::string>> extra = {});
		void drawFileInfo();
	public:
		// a file or folder moved (Files' Rename or Move here, the dashboard's): everything kept of it follows it, it and
		// what's inside it — Recent and places to resume, playlists and Liked, a document's place and bookmarks, the
		// manga reader's series and their read marks (main thread: the dashboard's come through WebServer::call)
		void pathMoved(const std::string& from, const std::string& to, Player& p);
	private:
		void fileJobTick(Player& p);
		bool fileJobKey(Button b);
		bool fileJobPointer(const Pointer& pt, int x, int y);
		void drawFileJob();
		Rect fileJobCancelRect() const { return {200, 300, 240, 52}; }
		PlayRequest playRequest;
		// the select mode's uses (Playlists::selMode): Edit (a playlist's items), Add (files), Pick (a folder)
		enum SelMode { SEL_NONE, SEL_EDIT, SEL_ADD, SEL_PICK };
		std::string whereOf(const std::string& dir) const;	// "Videos / Shows", "MY-PC / Media / Movies"
		std::string ctxTitle, ctxNote;	// a playlist's menu: its title, and the line under it ("12 items", why it's missing)
		// a playlist's network items: their folders listed for their subtitles and covers (Player::netFolderWanted),
		// each once a play; the answers by serial
		std::set<std::string> sideAsked;
		std::map<int, std::string> sidePending;
		int sideSerial = 1 << 30;
		void dropTouch();
		// the browser's menu: X anywhere (or a finger held on a folder). On a card's folder Hide folder / Unhide
		// folder (a ".hidden" file in it); everywhere Refresh (the list read again: a file copied in meanwhile); Cancel
		enum CtxItem { CTX_HIDE, CTX_REFRESH, CTX_CANCEL, CTX_ADD, CTX_FIND, CTX_EDIT, CTX_TEST, CTX_DELETE,
			// a playlist's (Y on one, X in one) and its items'
			CTX_PL_NEW, CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_RENAME, CTX_PL_DELETE, CTX_PL_DEDUP, CTX_PL_MISSING, CTX_PL_HERE, CTX_PL_REMOVE,
			CTX_PL_SHOW, CTX_PL_ADD_FILES, CTX_PL_ADD_FOLDER, CTX_PL_SELECT, CTX_PL_MOVE,
			// Local and Network: X's Select files, a folder's Add to playlist...
			CTX_SELECT_FILES, CTX_ADD_TO,
			// a file's or folder's own on the cards (Files, ui_fileops.cpp): Copy..., Move..., Rename..., Delete
			CTX_F_COPY, CTX_F_MOVE, CTX_F_RENAME, CTX_F_DELETE, CTX_F_INFO,
			// the Network tab's X: the web dashboard's page
			CTX_WEB,
			// Liked's item's Add to playlist... (the one file)
			CTX_PL_ADD_ONE,
			// a recent file's (Home's Y): Resume, Play from the start, Show in folder, Like / Unlike, Add to playlist...,
			// Remove from Recent
			CTX_R_PLAY, CTX_R_START, CTX_R_SHOW, CTX_R_LIKE, CTX_R_ADD, CTX_R_REMOVE,
			// Home's X
			CTX_CLEAR_RECENT, CTX_HIDDEN, CTX_SETTINGS,
			// the mini bar's (a hold on it): Now playing, Play / Pause, Like, Add to playlist..., Show in folder, Stop
			CTX_NP_NOW, CTX_NP_PLAY, CTX_NP_LIKE, CTX_NP_ADD, CTX_NP_SHOW, CTX_NP_STOP };
		// how it was opened: X (the place's menu), Y (the row's), a finger held on a row (the row's, folders and
		// connections only)
		enum MenuHow { MENU_X, MENU_Y, MENU_HOLD };
		std::string ctxServer;		// a connection's menu: its id
		std::string ctxStatus;		// a connection's menu: Test's answer
		bool ctxArmed = false;		// Delete pressed once
		int ctxMissing = 0;		// a playlist's Remove missing, pressed once: how many it would take out
		bool ctxOpen = false;
		int ctxCursor = 0;
		std::vector<int> ctxItems;	// CtxItem
		std::string ctxPath;		// the folder Hide is for ("" none)
		std::string ctxFolder;		// the folder "Add to playlist..." is for (a card's or a server's)
		Uint32 holdAt = 0;	// a finger down on a row, for the hold that opens it
		void openBrowseMenu(int row, MenuHow how);
		void chooseBrowseMenu(int item, Player& p);
		void drawBrowseMenu(const Player& p);
		// the updater's pop-up over the file list: a release found (Update now, Later, Not until tomorrow), then the
		// download's progress (Hide), then Restart now / Later, or what went wrong (OK)
		bool popupClosed = false, popupFollow = false;
		int popupCursor = 0;
		bool popupShown(const Player& p) const;
		bool popupKey(Button b, Player& p);	// its keys (Browse's: the pop-up is over the file list)
		std::vector<const char*> popupItems() const;
		void choosePopup(int item, Player& p);
		void drawUpdatePopup();
		Rect popupRow(int item) const { return {136, 252 + item * 50, 368, 44}; }
		std::string updateStatus() const;	// Check now's value
		// the battery's icon (and percent, a bolt while charging), its right edge at right; read every 10 s
		BatteryState battery;
		Uint32 batteryAt = 0;
		void readPower();
		// minX: the clock only if it fits right of it (a page's title or a settings crumb)
		void drawBattery(int right, int y, const Player& p, int minX = 0);
		// the clock, left of the battery and the web's globe ("14:32", "2:32 PM"; "" off, or over a photo); where the
		// header's marks start (the battery's right edge at 616): a title or the section markers end before it
		std::string clockText(const Player& p) const;
		int statusLeft(const Player& p) const;
		static constexpr int BATTERY_W = 110;	// what a title leaves for it
		// the menu's box, in the middle of the panel, as tall as its items
		// (a connection's has a line under its title for Test's answer)
		// A long menu (a playlist's eight) steps its rows closer, 52, 48 or 44 apart, to fit above the footer; one that
		// still doesn't leaves its line out (a playlist's count: the page shows it)
		int ctxStep() const
		{
			int n = (int)ctxItems.size(), extra = ctxServer.empty() && ctxNote.empty() ? 0 : 34;
			for (int step : {52, 48})
				if (80 + extra + n * step <= 436) return step;
			return 44;
		}
		int ctxExtra() const
		{
			if (ctxServer.empty() && ctxNote.empty()) return 0;
			return 80 + 34 + (int)ctxItems.size() * ctxStep() <= 436 ? 34 : 0;
		}
		// Home's (a recent file's, Options) wider: their rows have a value on the right
		bool ctxWide() const { return !ctxItems.empty() && ctxItems[0] >= CTX_R_PLAY; }
		Rect ctxBox() const
		{
			int h = 80 + ctxExtra() + (int)ctxItems.size() * ctxStep();
			return ctxWide() ? Rect{90, (436 - h) / 2, 460, h} : Rect{112, (436 - h) / 2, 416, h};
		}
		Rect ctxRow(int item) const
		{
			Rect b = ctxBox();
			int inset = ctxWide() ? 8 : 16;
			return {b.x + inset, b.y + 62 + ctxExtra() + item * ctxStep(), b.w - 2 * inset, ctxStep() - 6};
		}


		// the on-screen keyboard, over the bottom panel: a form's field typed in
		bool kbOpen = false, kbSymbols = false;
		ShiftState kbShift = ShiftState::Off;
		int kbCursor = 0, kbField = 0, kbPressed = -1;
		Uint32 kbActiveAt = 0;	// the last key: the caret shows steadily for a moment after it, then blinks
		bool kbCaretShown() const;
		TextEdit kbEdit;
		std::vector<KeyDef> kbKeys() const;
		void openKeyboard(int field, const std::string& text);
		void kbPress(Button b);
		void kbPointer(const Pointer& pt, int x, int y);
		void kbKey(int i);
		void kbClose(bool keep);
		void drawKeyboard();
		// Settings: START's page on every shell screen, the player and the text and PDF readers (there is no START
		// menu any more: its places are the rail's cells, F is Quick settings, L3 is Search, and About is a Settings
		// group). Opened by START it remembers where it came from, and B or START goes back there (the player, a book
		// page, a folder); opened by the rail or L/R it is a section, and B goes Home.
		bool settingsOver = false;
		Page settingsBackPage = Page::Browse;
		std::string settingsBackDir;
		int settingsBackCursor = 0, settingsBackScroll = 0, settingsBackTab = 0;
		void openSettings(Player& p, bool fromStart);
		void closeSettings(Player& p);
		bool startOpensSettings(const Player& p) const;	// START's one exception: forms' Save/Done, the Book bar
		// Play on, where it plays: the handheld or one of the dashboard pages (PlayerPage's PlayOnSheet, ui_player.cpp).
		// Picking one is the Session's SetOutput (SessionHost::sendTo)
		struct OutChoice { std::string tab, name; };	// tab "": the handheld
		std::vector<OutChoice> outChoices() const;	// the handheld first, then each page that can play (WebServer::playTabs)
		bool playOnShown() const;	// the row: a page that can play is open, or one is the output
		// the output moved to that choice (the Session's SetOutput), unless it plays there already: the Play on sheet's
		void playOnSend(const OutChoice& c, Player& p);
		// Show in folder from More: what plays closed, then its folder shown (showBrowser)
		std::string showAfterLeave;
		bool showSourceAfterLeave = false;	// List (Y) on a playlist's song: the player left for that playlist's page
		// Like: toggled (the heart, L3), the heart's pop and the top panel's card
		void likeNow(Player& p);
		void likeCard(Player& p, bool liked, const std::string& title);
		Uint32 likePopAt = 0, addFlashUntil = 0;
		// what plays can be liked and added to a playlist (a song or a video, a state folder to keep them in)
		bool headerActions(const Player& p) const;
		bool clearHistory(Player& p);	// true when it cleared (the second time)
		bool act(int id, Player& p);	// a touch button let go of on it
		bool pressAnyway(Button b, Player& p);
		// what the top screen says as the play mode changes (the dashboard's labels too)
		std::string modeLabel(const Player& p) const;
		// Sound (music's): two tabs, Equalizer and Effects: an area (Area), ui_sound.h / ui_sound.cpp. The song's
		// Equalizer and Effects sheets (PlayerPage's SongViewSheet) call these same actions
		struct Sound;
		std::unique_ptr<Sound> sound;
		// a photo: the pan area (its thumbnail, the part on the top panel boxed: a finger drags the photo as on a
		// phone, a flick glides, a double-tap zooms; no swipe to the next: Prev/Next do that) beside its buttons
		bool photoKey(Button b, Player& p);	// true: it was a photo's
		bool photoPointer(const Pointer& pt, int x, int y, Player& p);
		bool photoDown(int x, int y, Player& p);	// a finger down on its page: the pan area, or a button
		void photoZoomTo(double zoom, double panX, double panY, Player& p);
		void drawPhoto(const Player& p);
		Rect panRect(const Player& p) const { return {p.settings.panRight ? 288 : 16, 80, PAN_W, PAN_H}; }
		Rect photoBtn(int i, const Player& p) const
		{
			return gridCell({p.settings.panRight ? 16 : 368, 80, 120, 56}, 2, 8, 8, i);
		}
		Rect thumbRect(const Player& p) const;	// the photo, as shown, fitted in the pan area
		bool panTouch = false, panMoved = false;
		int panDownX = 0, panDownY = 0, lastX = 0, lastY = 0, tapX = 0, tapY = 0;
		double panStartX = 0, panStartY = 0, flickVX = 0, flickVY = 0;	// the flick's speed: units a millisecond
		Uint32 lastAt = 0, flickAt = 0, tapAt = 0;
		SDL_Texture* photoTex = nullptr;
		int photoTexId = -1;
		static constexpr int PAN_W = 336, PAN_H = 252, PHOTO_BTN = 20, PHOTO_BTNS = 8;
		Rect buttonRect(int id) const;
		std::string signature(const Player& p) const;

		void renderPage(const Player& p);	// the page into the renderer, not yet shown
		void drawLeave(const Player& p);	// a photo's Close
		void drawTracks(const Player& p);
		// the pieces of a page: the header ("<", the title, the battery), a tab, the footer's hints (a button's name in
		// yellow, what it does), the cursor's row, a list's scroll bar, a page with nothing to list
		struct Hint { std::string key, verb; };
		void drawHeader(const std::string& title, const Player& p, int titleRight = 0);	// titleRight: the title ends before it
		void drawTab(Rect r, const char* name, bool active, int size = 1);
		void drawHints(const std::vector<Hint>& hints);
		void drawFooterText(const std::string& text, SDL_Color c);
		void drawFocus(Rect r);
		void drawScrollBar(int top, int height, int first, int shown, int total);
		// btn: the button's glyph (G_PLUS when none); sub: a line, or two apart by '\n'
		void drawEmpty(const Glyph& g, const std::string& title, const std::string& sub, const char* action, const Glyph* btn = nullptr);
		std::string elideLeft(const std::string& s, int size, int maxW) const;
		// a dialog over the page (the Options menu, the update, quick settings), and one of its rows
		void drawDialog(Rect box, const std::string& title);
		// note: a value on the row's right ("On", "Press twice")
		void drawDialogRow(Rect r, const Glyph* g, const std::string& label, bool at, bool pressed, SDL_Color ink,
			const std::string& note = "", SDL_Color noteInk = {0, 0, 0, 0});
		std::vector<std::string> wrapText(const std::string& text, int size, int maxW, int maxLines) const;
		// a button's face: a glyph and a label side by side, the hardware button's name under them (key: none)
		// Rows and grids of buttons: one function per row gives the rect, and drawing and touch both ask it.
		// rowCell: the i-th of n equal cells across `area`, `gap` apart (the sizes divide exactly in every row that
		// uses it: a remainder would be dropped at the right).
		static Rect rowCell(Rect area, int n, int gap, int i)
		{
			int w = (area.w - (n - 1) * gap) / n;
			return {area.x + i * (w + gap), area.y, w, area.h};
		}
		// gridCell: the i-th of a grid of `cols` columns, row by row, every cell the size of `first`
		static Rect gridCell(Rect first, int cols, int gapX, int gapY, int i)
		{
			return {first.x + i % cols * (first.w + gapX), first.y + i / cols * (first.h + gapY), first.w, first.h};
		}
		// a button's label, with its key under it when there is one (the label moves up to make room); the label
		// and key fit labelW/keyW units wide (0: the button less 6)
		void drawKeyedLabel(Rect b, const std::string& label, const std::string& key, bool off = false, int labelW = 0, int keyW = 0);
		void drawButton(Rect b, const Glyph* g, const std::string& label, const char* key, bool off, int size, bool lit = false);	// lit: the glyph and label in yellow (on, as the heart once liked)
		static constexpr int FOOT_Y = 458, SUB_Y = 70;	// the footer's line, the browser's "where" line (their middles)
		void drawInfo(const Player& p);
		// the Media info page's lines: a row's value wrapped to the width, read again each second while it's open
		struct InfoLine { std::string label, text; bool header; };
		std::vector<InfoLine> infoLines;
		Uint32 infoAt = 0;
		void readInfo(const Player& p);
		void setInfoLines(const std::vector<Player::InfoRow>& rows);	// wrapped to the page
		void openAbout(const Player& p);
		static constexpr int INFO_Y = 70, INFO_H = 26, INFO_SHOWN = 14, INFO_VALUE_X = 190;
		// About is a settings group, under the kit's header: its rows start under the header's strip (the title is in
		// it, beside the back key), the foot kept clear for the support block
		static constexpr int ABOUT_Y = 64, ABOUT_SHOWN = 10;
		void drawAboutSupport();	// the line, the QR (packaging/support/kofi-qr.png) and the address
		SDL_Texture* supportTex = nullptr;	// (decoded once at the size drawn)
		int supportPx = 0;
		// the support QR, large: the flag (see aboutQrUp); the small code's box is aboutQrRect
		bool aboutQr = false;

		SDL_Texture* text(const std::string& s, int size, SDL_Color c);
		void drawText(const std::string& s, int size, SDL_Color c, int x, int y, int maxW = 0, int align = 0);
		// the same, the line's middle (its capitals') on cy: labels centred in their boxes at any panel size
		void drawTextMid(const std::string& s, int size, SDL_Color c, int x, int cy, int maxW = 0, int align = 0);
		void drawTextAt(const std::string& s, int size, SDL_Color c, int x, int y, int maxW, int align, bool middle);
		// The Title marquee (docs/redesign/components.md; lay::marquee): a title too long for its box, drawn from its
		// start with its last 16 units fading into `ground`, moving along twice (passes: 1 in a focused row), then
		// resting at its start. One moves per panel at a time (the first drawn in a frame: a focused row before the mini
		// bar); a sheet or menu over the page holds it at its start, and it starts again when it's gone, when the page
		// changes or the panel wakes. Scroll long titles off: cut with "…" as before
		void drawMarquee(const std::string& s, int size, SDL_Color c, int x, int cy, int boxW, SDL_Color ground, int passes = 2);
		// the title of what plays as shown in a box boxW wide (Repeated start: shownTitle)
		std::string playingTitle(const Player& p, const std::string& title, int size, int boxW) const;
		bool scrollTitles = true;
		struct MarqMover
		{
			std::string key;
			int textW = 0, boxW = 0, size = 0, passes = 2;
			uint32_t start = 0;
		};
		std::map<std::string, uint32_t> marqStarts;	// each title's start, this page (cleared: they start again)
		MarqMover marqMover;	// the one moving, as last drawn
		bool marqUp = false;	// one claimed the panel in the last drawing
		uint32_t marqFrame = 0, marqClaim = 0;
		int marqPage = -1;
		lay::MarqueeAt marqNow() const;	// where the moving one is now
		bool marqueeMoving() const;
		void marqueeRestart() { marqStarts.clear(); }	// the panel woke
		mutable int capHeight[FONT_COUNT] = {};	// in pixels
		void fill(Rect r, SDL_Color c);
		// rounded corners (theme.h's radii, in 640x480 units): corners a mask of ROUND_TL, _TR, _BL, _BR. SDL_Renderer
		// has none: a box is its middle and sides as rects, its corners copies of one anti-aliased quarter circle
		// (white, made once per radius in the panel's pixels, tinted and flipped)
		enum { ROUND_TL = 1, ROUND_TR = 2, ROUND_BL = 4, ROUND_BR = 8, ROUND_ALL = 15 };
		void fillRound(Rect r, SDL_Color c, int rad, int corners = ROUND_ALL);
		// the part of a rounded box between y0 and y1 (left of x1, when given): a line along its edge, its corners kept
		void fillRoundBand(Rect box, int rad, int y0, int y1, SDL_Color c, int x1 = -1);
		// a level bar: the track, the first w units filled (the accent when none given)
		void drawLevel(Rect track, int w, SDL_Color trackInk, SDL_Color fillInk = {0, 0, 0, 0});
		// a button, tab, tile, card or dialog: rounded, an edge inside its outline (the fill 30% towards the text, the
		// accent when focused) and a darker lip along its bottom
		void drawBox(Rect r, SDL_Color c, int rad, bool focus = false);
		void drawBox(Rect r, SDL_Color c, int rad, SDL_Color edge);
		void roundDev(SDL_Rect d, SDL_Color c, int R, int corners);	// in the panel's pixels
		SDL_Rect devRect(Rect r) const;
		int devRadius(int rad) const;
		SDL_Texture* corner(int R);
		std::vector<SDL_Texture*> corners;	// by radius in pixels
		// a glyph at (x, y), its cells cell units square, snapped to the panel's pixels; Mid: centred on (cx, cy)
		void drawGlyph(const Glyph& g, int x, int y, SDL_Color c, int cell = 2);
		void drawGlyphMid(const Glyph& g, int cx, int cy, SDL_Color c, int cell = 2);
		int glyphW(const Glyph& g, int cell = 2) const;

		SDL_Renderer* ren = nullptr;
		// small, normal, large (opened at the start), then the kit's sizes, each opened when first drawn (font())
		mutable TTF_Font* fonts[FONT_COUNT] = {};
		TTF_Font* font(int size) const;
		// for text Pixelify Sans has no glyphs for, at the same sizes: Fusion Pixel (Chinese, Japanese, Korean,
		// Vietnamese, Greek), then Noto Sans Thai, Arabic, Hebrew, Devanagari (the kit's sizes: as font())
		mutable std::vector<std::array<TTF_Font*, FONT_COUNT>> fallbacks;
		std::vector<std::string> fallbackFiles;	// each one's file, in fallbacks' order
		TTF_Font* fallbackFont(size_t k, int size) const;
		// the text in pieces, each with a font that has its characters: one piece in one font when one font has
		// them all (no mix of looks in a name), else a font a run of characters (an Arabic name's ".mkv")
		std::vector<std::pair<TTF_Font*, std::string>> runs(const std::string& s, int size) const;
		std::vector<std::pair<TTF_Font*, std::string>> shapeRuns(const std::string& s, int size) const;
		int textWidth(const std::string& s, int size) const;
		std::unordered_map<std::string, SDL_Texture*> cache;
		// measured widths and shortened ("...") strings, by size and text: a long line is shortened once, not
		// re-measured a character at a time on every redraw (the handheld's cores made lists lag)
		mutable std::unordered_map<std::string, int> widthCache;
		std::unordered_map<std::string, std::string> elideCache;
		std::string drawn, fontsDir;

		Page page = Page::Main;
		int cursor = 0, scroll = 0;
		// ---- sliders (ui_slider.cpp, slider.h; docs/notes/sliders.md) ----
		// Every bar a finger moves is one of these: a touch in its hit area grabs it (slideGrab) and the value jumps
		// to the finger; pointer() then gives every move and the lift to it, wherever the finger goes, until it
		// lifts. `set` hears the value while held (each change; one per everyMs at most when that's given: a seek, a
		// page) and once more with last = true as the finger lifts. A key pressed meanwhile ends it as a lift does.
		enum { SL_SEEK = 1, SL_QUICK, SL_DOC, SL_MANGA, SL_EQ, SL_LIGHT };
		struct SlideSpec
		{
			Rect track;	// the finger's place is read along this (its width; its height when upright)
			SliderRange range;
			bool upright = false;
			Uint32 everyMs = 0;	// a costly target: told this often at most while held (0: every change)
			double now = 0;	// its value as the finger lands: a slide moves it on from here, by the finger's travel
		};
		using SlideSet = std::function<void(double v, bool last, Player& p)>;
		struct Slide
		{
			bool on = false, moved = false;	// held; the finger has travelled along the track (not a tap)
			int id = 0, sub = 0;	// which slider (SL_*), and which of its kind (a row, a band)
			SlideSpec spec;
			SlideSet set;
			double value = 0;	// where the finger has it
			int at0 = 0, at = 0;	// along the track: where the finger landed; where the slider is (sliderFollow)
			Page page = Page::Main;	// the page and the overlay it was grabbed on: gone, the slider is let go (slideStale)
			const Overlay* owner = nullptr;
			SliderPace pace;
		} slide;
		void slideGrab(int id, int sub, const SlideSpec& spec, int x, int y, Player& p, SlideSet set, const Overlay* owner = nullptr);
		// held: a finger on it that has slid along it (a touch alone changes nothing and shows nothing yet)
		bool slideHeld(int id) const { return slide.on && slide.moved && slide.id == id; }
		bool slideHeld(int id, int sub) const { return slide.on && slide.moved && slide.id == id && slide.sub == sub; }
		bool slideFollow(const Pointer& pt, int x, int y, Player& p);	// pointer(): a move or the lift while one is held
		void slideLive(Player& p);	// the value to the target, if it's due (tick() too: a finger at rest)
		bool slideWaiting() const;
		bool slideStale() const;
		void slideEnd(Player& p);	// let go, the last value given
		void slideDrop();	// let go, nothing given
		// How a slider looks, the same for all (docs/notes/sliders.md): at rest the track, the accent to `share`
		// (0..1) of it and the knob there; held, the knob grown (drawSlideKnob) so the finger knows it has it, and the
		// value said where the finger doesn't hide it: a tag over the knob (drawSlideTag), unless the row says the
		// value itself above or beside the bar (then that is lit instead). Lifted, the resting look again.
		void drawSlider(Rect track, double share, bool held = false, const std::string& tag = "");
		void drawSlideKnob(Rect knob);	// a resting knob's box, drawn as held: 3 bigger all round, a dark rim
		void drawSlideTag(Rect track, double share, const std::string& text);
		// the share a slider is drawn at: where the finger has it while held, else `rest` (what its target says)
		double slideShare(int id, int sub, double rest) const
		{
			return slideHeld(id, sub) ? sliderShare(slide.value, slide.spec.range) : rest;
		}
		// the finger may shift this far along the track without moving the slider (landing, lifting)
		static constexpr int SLIDE_SLACK = 6;
		BottomPanel panel;
		bool swallowTouch = false;	// the touch that woke the panel: it presses nothing
		int pressedBtn = -1;		// a touch button under the finger: it acts when the finger lifts on it
		bool listTouch = false, listScrolled = false;	// a finger on a track list: a tap chooses, a drag scrolls
		int touchStartY = 0, touchX = 0, scrollStart = 0;
		Uint32 clearArmed = 0;		// Clear play history chosen once: again within 2 s clears it

		std::string openRequest;
		std::vector<BrowseRoot> roots;
		// the cards looked at again every few seconds: one put in or taken out shows at once (rootsSerial: main hands
		// the dashboard its places again)
		Uint32 rootsCheckedAt = 0;
		int rootsChanged = 0;
		void rootsCheck(Player& p);
	public:
		int rootsSerial() const { return rootsChanged; }
		const std::vector<BrowseRoot>& places() const { return roots; }
		// main's lid-shut keys (docs/redesign/lid-and-incognito.md): the chapter before / after, on what plays
		void chapterStepForLid(int dir, Player& p) { chapterStep(dir, p); }
		// incognito, for main's lid, power-hold and idle-sleep rules (the quick settings and the page use the methods)
		bool incognitoOn() const { return incognito; }
		void incognitoSet(bool on, Player& p, bool note = true) { setIncognito(on, p, note); }
		void incognitoCloseAllNow(Player& p) { incognitoCloseAll(p); }
	private:

		static constexpr int ROW_Y = 84, ROW_H = 48, ROWS_SHOWN = 7;	// 7 rows end at 420, above the hints (and the mini bar)
		// the settings' rows by id: the subtitle style's, and the actions and pages that have their own code (the rest are
		// the settings table's: SET_ROW0 + the index in settingDefs(), ui_internal.h). Shown a group at a time
		// (settingRows), after a list of the groups. The manga reader's rows are its own page's (Page::MangaSettings):
		// EXT_NOTE is its note, no value
		static constexpr int STYLE_ROW0 = 0, CLEAR_ROW = STYLE_KEYS, CHECK_ROW = CLEAR_ROW + 1, INSTALL_ROW = CHECK_ROW + 1,
			TOP_LIGHT_ROW = INSTALL_ROW + 1, BOTTOM_LIGHT_ROW = TOP_LIGHT_ROW + 1, SOUND_ROW = BOTTOM_LIGHT_ROW + 1,
			EXT_NOTE = SOUND_ROW + 1, MANGA_CLEAR_ROW = EXT_NOTE + 1, MANGA_JAVA_ROW = MANGA_CLEAR_ROW + 1;
		// the panels' backlights (BananaOS's own levels: changed here as its menu changes them)
		Backlight backlight;
		bool sameBrightness = true;	// the setting's, kept here for settingRows (no Player there)
		Volume volume;
		int volumePct = -1;	// as read when the quick settings opened, then as set there
		int quickCursor = 0;
		enum QuickRow { Q_VOLUME, Q_BRIGHT, Q_BOTTOM, Q_SAME, Q_WEB, Q_INCOGNITO, Q_QUIT };
		int quickSub = 0;	// Web dashboard's row: 0 its switch, 1 New PIN (LEFT/RIGHT)
		void closeQuick();	// (its overlay, if open: B, START, the Web button, a tap outside)
		std::vector<int> quickRows() const;
		bool quickKey(Button b, Player& p);
		bool quickPointer(const Pointer& pt, int x, int y, Player& p);
		void quickAdjust(int row, int dir, Player& p);
		void quickSlide(int row, int x, int y, Player& p);	// a finger on the row's bar: grabbed
		void quickChoose(int row, Player& p);
		void drawQuick(const Player& p);
		// the quick settings' panel: rows 62 apart, closer (56) when that many don't fit above the footer
		// the rows' spacing: roomy while they fit above the hints, closer as rows come (seven at most: both panels'
		// brightness apart, the dashboard, incognito mode and quit)
		int quickStep() const
		{
			int n = (int)quickRows().size();
			return 74 + n * 62 <= 436 ? 62 : 74 + n * 56 <= 436 ? 56 : (436 - 74) / n;
		}
		// centred on the whole panel, over the shell's rail too (the owner, 2026-11-19): a modal under its veil
		Rect quickPanel() const
		{
			int h = 74 + (int)quickRows().size() * quickStep();
			return {40, (436 - h) / 2, 560, h};
		}
		Rect quickRowRect(int i) const
		{
			Rect p = quickPanel();
			return {p.x + 16, p.y + 56 + i * quickStep(), p.w - 32, std::min(54, quickStep() - 2)};
		}
		// the stepper's pieces are laid from the row's right edge, so the panel's width changes nothing they need
		Rect quickPinBtn(int i) const { Rect r = quickRowRect(i); return {r.x + r.w - 164, r.y + (r.h - 48) / 2, 160, 48}; }
		Rect quickMinus(int i) const { Rect r = quickRowRect(i); return {r.x + r.w - 292, r.y + (r.h - 48) / 2, 48, 48}; }
		Rect quickBar(int i) const { Rect r = quickRowRect(i); return {r.x + r.w - 232, r.y + r.h / 2 - 10, 164, 20}; }
		Rect quickPlus(int i) const { Rect r = quickRowRect(i); return {r.x + r.w - 56, r.y + (r.h - 48) / 2, 48, 48}; }
		void setBrightness(Backlight::Panel panel, int permille, Player& p);
		// (7 rows end at 386: the help's two lines at 400 stay clear of them; a longer group scrolls)
		static constexpr int SET_Y = 92, SET_H = 46, SET_SHOWN = lay::SET_ROWS, HELP_Y = 400;
		static constexpr int64_t STYLE_ROW = -2;	// the subtitle menu's last row: the settings page
		static constexpr int QUIT_BTN = 10, INFO_BTN = 13,	// (a photo's Close and Info)
			BACK_ICON = 16,	// "<" at the top left of every page: B
			EMPTY_BTN = 17;	// an empty page's button (no connections: Find on the network)
		// the web dashboard's page: New PIN and Turn off (its Turn on / Try again is EMPTY_BTN)
		static constexpr int WEB_NEWPIN = 540, WEB_OFF = 541;
		// the select mode's bar (and Move's), the busy dialog's Cancel, the Add to... picker's rows (PICK_ROW + i)
		static constexpr int SEL_ALL = 530, SEL_MOVE = 531, SEL_ACT = 532, SEL_CANCEL = 533, MOVE_CANCEL = 534, MOVE_DONE = 535,
			BUSY_CANCEL = 536, PICK_ROW = 900;
		// the kept Menu of places (unreached): the Now playing line, the places' tiles (MENU_TILE + i), Quick settings and Quit
		static constexpr int MENU_NOW = 560, MENU_TILE = 561, MENU_QUICK = 570, MENU_QUIT = 571;
		// Sound's touch buttons, apart from the grid's and the photo's; its dialogs' rows from DLG_ROW
		static constexpr int SOUND_TAB0 = 510, SOUND_TAB1 = 511, EQ_SWITCH = 520, EQ_PREV = 521, EQ_NEXT = 522, EQ_NAME_ZONE = 523,
			EQ_RESET = 524, EQ_SAVE = 525, EQ_OPTIONS = 526, DLG_ROW = 700, FX_ROW = 800;
		void drawBackIcon();
		// the browser's rows' marks: what each row is
		enum RowIcon { ICON_NONE, ICON_RECENT, ICON_PLACE, ICON_FOLDER, ICON_VIDEO, ICON_SONG, ICON_PHOTO, ICON_DOC, ICON_BOOK, ICON_FILE, ICON_PDF, ICON_EPUB, ICON_COMIC, ICON_NETWORK, ICON_REFRESH,
			ICON_TYPE, ICON_TEST, ICON_SAVE, ICON_PLAYLIST, ICON_LIKED, ICON_NEW };
		RowIcon rowIcon(int row) const;
		void drawRowIcon(RowIcon icon, int x, int y, SDL_Color c);
		// every page has it but Home (nowhere to go back to; B there asks to quit, START has Quit)
		bool backIconShown(const Player&) const;
		static constexpr Uint32 UNLOCK_HOLD_MS = 1000;	// buttonRect's ids beside the grid's 0..9
		static constexpr int SET_ARROW_X = 372;	// an old full-panel settings row's left arrow (the shell's rows use lay::settingStepper)

		// ---- the web dashboard ----
		// Page::Web is an area (Area), ui_web.h / ui_web.cpp; what every page shows of it (the header's globe, the quick
		// settings' row, the switch, the address and the PIN) stays Ui's, below
		struct Web;
		std::unique_ptr<Web> webPage;
		void openWeb();	// the page, from Home's menu, a settings group, the globe, the quick settings
		// its states as the page shows them (WEB_INCOGNITO_VIEW: the dashboard held off by incognito: the switch at 45%
		// with its reason, one sentence, Leave incognito; no QR, address or PIN)
		enum WebView { WEB_OFF_VIEW, WEB_ON_VIEW, WEB_NOWIFI_VIEW, WEB_FAILED_VIEW, WEB_INCOGNITO_VIEW };
		int webView(const Player& p) const;
		void webTurn(bool on, Player& p);	// the switch: kept in the settings, the server started or stopped
		int webClients() const;
		std::string webPin() const;
		std::string webAddress() const;	// "192.168.1.23:8080" ("" no Wi-Fi, or not running)
		std::string webIp;				// the device's address, read when the page opens and every 10 s while it shows
		Uint32 webIpAt = 0, webInputAt = 0, pinArmed = 0;
		void readWebIp();
		TTF_Font* pinFont = nullptr;	// Pixelify Sans at 56: the PIN's characters
		// the header's indicator (left of the battery), as last drawn: a tap opens the page
		Rect webInd{0, 0, 0, 0};
		bool webIndDown = false;
		void drawWebIndicator(int right, int cy, const Player& p);
		bool webIndicatorShown(const Player& p) const;
		std::string webSig(const Player& p) const;	// for signature()
		int levelMs = 0;
		std::string levelLabel;
		int levelPct = 0;
		bool stopRequest = false;

		// ---- every page's loading mark and message dialog (the Manga pages use them most; the area is ui_manga.h) ----
		// the one loading mark: a ring of dots turning, a line under it, in the middle of what's loading (a page's
		// list, a chapter). Drawn this frame: the loop comes round for its next step (animating, signature)
		mutable bool loaderShown = false;
		void drawLoader(int cx, int cy, const std::string& text);
		// A message that needs reading (what can't be done, what went wrong): a dialog on this panel with OK, A or B
		// or a tap closes it. Short confirmations ("Copied", "Speed 1.5x") stay the top screen's notice (Player::osd)
		// (an overlay: AlertBox in ui_overlay.cpp; a second message takes the first one's place)
		struct AlertBox;
	public:
		void alert(const std::string& title, const std::string& text = "");
		// the first start with no lid-close in settings.conf: what closing the lid does, asked once (Pause first);
		// the answer is saved, so it isn't asked again (LidAskBox in ui_overlay.cpp)
		void askLid();
		struct LidAskBox;
		// Opening Manga while a video plays (docs/redesign: manga-guard, ext-section-video): the confirm sheet that
		// says the video stops, Stay focused and B, with "Don't ask again" (the manga-ask-video setting). Pushed by
		// the Extensions page's Manga row (MangaGuard in ui_ext.cpp); a song or a photo is never asked about.
		void askMangaGuard(const Player& p);
		struct MangaGuard;
	private:

		// ---- playing behind the browser ----
		// the player left (B, "<", Back) while a song or video plays on: the browser is the base page; showPlayer ends it
		bool bgPlay = false;
		bool stopPressed = false;	// the player's own Stop (takeStopPressed)
		bool stopKeepsPage = false;	// the Manga guard's Stop: the page shown stays (takeStopKeepsPage)
		bool playerOverPhoto = false;	// the mini bar's tap over a photo (takePlayerOverPhoto)
		// the player shown (showPlayer) and not left since; back: where it was opened from (backFromPlayer)
		bool onPlayer = true;
		struct PlayerBack
		{
			bool set = false;
			Page page = Page::Browse;
			int setGroup = -1, cursor = 0, scroll = 0;
			std::string browseDir;	// the folder or playlist page it was opened from (a playlist's play moves the list's)
			int browseTab = 0;
		} playerBack;
		// back to what plays (SELECT on a browse page, the bar, Now playing, B at Home): the player. False: nothing plays
		bool goNowPlaying(Player& p);
		bool nowPlayable(const Player& p) const { return p.playing() && !p.playingPhoto(); }
		// the player the mini bar works on: main's (see mainPlayer above); without it, p
		Player& barPlayer(Player& p) const { return mainPlayer ? *mainPlayer : p; }
		const Player& barPlayer(const Player& p) const { return mainPlayer ? *mainPlayer : p; }
		// the mini bar (Now playing) along the bottom of every page but the player's own (the song or video player
		// page, its tracks, its queue, its Info): a tap the player, its play mark play / pause (a page playing the
		// file: the page's, held: here), a hold its menu. The comic reader and the photo viewer are pages of their
		// own, not the player: it shows there too
		// SELECT here goes back to what plays (the bar's right end names it), but where SELECT is the page's own key
		// (a photo's Fit/Fill, Sound's On/Off, the reader's fullscreen) the bar doesn't name it
		bool selectToPlayer() const { return page != Page::Main && page != Page::Sound && page != Page::MangaRead; }
		static constexpr Rect MINI_BOX{16, 422, 608, 34};
		static constexpr int MINI_PP = 560, MINI_BAR = 561;
		bool miniBarShown(const Player& p) const;
		// where the bar is now: the shell's own box on a section page (right of the rail), the old one elsewhere;
		// and where its play mark ends (a tap left of it is the mark, the rest the bar)
		Rect miniBox(const Player& p) const;
		int miniMarkRight(const Player& p) const;
		bool miniUp = false;	// shown in the last frame drawn (tileRect, the footer's line)
		void drawMiniBar(const Player& p);
		void miniToggle(Player& p);	// its play mark: play / pause what plays (a page's too)
		void openNowMenu(const Player& p);
		Uint32 miniHoldAt = 0;
		bool playHoldMini = false;	// the Play hold (a page plays the file) is on the mini bar's play mark
		int footY() const { return miniUp ? 468 : FOOT_Y; }
		// the playing file's row (and Recent card): a chip ("Playing", "Paused", "In browser", "Stopped"), set each frame
		std::string chipPath, chipPlDir;	// chipPlDir: a playlist's page (only there, of the playlists: the one playing)
		int chipKind = 0;	// 0 none, 1 playing, 2 paused, 3 in a browser
		void setChip(const Player& p);
		int drawChip(int right, int mid, int kind, bool card = false);	// its width
		int rowChip(int i) const;	// the chip of the browser's row i (0 none)
		// back from the player: the playing file's row (and the bar's edge) lit a moment
		Uint32 landFlashUntil = 0;
		bool markAnim = false;	// drawBattery drew the playing mark moving
		int markW(const Player& p) const;	// its room in the header (0 none)
		// Incognito mode (Quick settings' Incognito mode): nothing played goes in Recent (Player::incognito) and
		// reading adds nothing to the manga History (places, read marks, resume points, the library and downloads go
		// on as ever). Never saved: off at every start. Its mark sits left of the clock while it's on.
		// While it's on the web dashboard's server does not listen (the switch keeps its saved value, the server comes
		// back with the same PIN and sign-ins when incognito ends); the lid or a sleep can close everything (the
		// Lid or sleep in incognito setting): docs/redesign/lid-and-incognito.md
		bool incognito = false;
		int incognitoW() const { return incognito ? 18 + 10 : 0; }
		// Close all asked for Home: Stop's own way back (where the file came from) must not undo it
		bool homeAfterCloseAll = false;
		// Quick settings' tile, and a browser's confirm sheet: turning it on stops the dashboard (after asking when a
		// browser is signed in), turning it off starts it again when its switch says on. note: the "Incognito on/off"
		// note (false: none, as closing everything and a sleep say nothing)
		void setIncognito(bool on, Player& p, bool note = true);
		void askIncognito();	// the confirm sheet (a browser is signed in) then applyIncognito on A
		// the change itself, after the ask (public for the ask sheet, which is nested)
		void applyIncognito(bool on, Player& p, bool note);
		// Close all, at the lid or a sleep: Stop, a book/photo/comic/the Manga reader and sheets close, Home shows,
		// incognito ends. main closes its photo viewer before calling this (the photo is another player)
		void incognitoCloseAll(Player& p);
		struct IncognitoAsk;
		bool markLive(const Player& p) const;	// it moves: what plays plays (a page's: as the page does)
		void drawMark(int right, int cy, const Player& p);
		// the equalizer changed from the web (a slider's run): written (commitEq) a second after the last change
		Uint32 eqWebDirtyAt = 0;
		// a seek waiting for its file to open (the hand-off from a browser: at that position): path, seconds
		std::string seekPath;
		double seekTo = -1;
		Uint32 seekUntil = 0;
		// the paths a web selection plays (Up next lists them)
		std::vector<std::string> webSelection;
	};
}
