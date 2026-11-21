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
#include "lrucache.h"
#include "coverworker.h"
#include "netstore.h"
#include "networker.h"
#include "mediaindex.h"
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
#include <unordered_set>

namespace bp
{
	class WebServer;
	class DurationWorker;
	class MediaIndex;	// mediaindex.h (the cards' index: Music, Videos and Search)
	class MediaIndexWorker;	// mediaworker.h (the index's thread: its live counters reach the reading notice)
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
		// a button let go (keys and the pad; the scripted steps right after their press): A cares, for its hold while
		// a page plays the file, and the area (the player's volume hold)
		void release(Button b, Player& p);
		// a held button that repeats here (seeking, moving in a list, photo to photo, the player's volume)
		bool repeats(Button b, const Player& p) const;
		// how often a held button repeats, once repeats() takes it: the player's UP / DOWN eight a second (a volume
		// step), everything else 150 ms
		int repeatMs(Button b, const Player& p) const;
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
		// the player's own pages: over an open reader (the rail's Now playing key or SELECT tapped while reading), the
		// top panel shows what plays, not the pages; B comes back to the reader, and its pages to the top panel
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
		// idle: nothing plays and no card of the player's is up (the pane is the whole top screen then, ui_toppane.cpp);
		// else only the Theme row's pane (over what plays)
		bool topPane(PaneSpec& out, const Player& p, bool idle) const;
		bool topPaneGallery(PaneSpec& out) const;	// the test gallery's (ui_kit_new.cpp)
		bool idlePane(PaneSpec& out, const Player& p) const;	// ui_toppane.cpp
		// test only (--press gallery:new, gallery:new:2): kit-new's components drawn in place of the page (0: off)
		int galleryNew = 0;
		void drawGalleryNew();
		// the player's own Stop (X's More sheet, START › Playing › Stop, the Now playing block's hold menu), once: main
		// stops what plays
		bool takeStopPressed() { bool r = stopPressed; stopPressed = false; return r; }
		// The Stop the Manga guard asked for (ext-section-video): the page stays where it is (Manga), rather than
		// going back to where the video was opened from. Once, as the stop itself.
		// a photo deleted from its viewer: the viewer closes (once; main does it, as it does a Back press)
		bool takeLeaveWanted() { bool r = leaveWanted; leaveWanted = false; return r; }
		// the photo viewer's Slideshow (its More): the next photo every SLIDE_MS, until toggled off or the viewer closes
		static constexpr uint32_t SLIDE_MS = 5000;
		bool slideshow = false;
		uint32_t slideAt = 0;
		void slideshowTick(Player& p);
		bool takeStopKeepsPage() { bool r = stopKeepsPage; stopKeepsPage = false; return r; }
		// the rail's / Quick settings' tap over a photo: the photo closes and the player of what plays takes the
		// screen (once; main does it: only it can close the photo viewer)
		bool takePlayerOverPhoto() { bool r = playerOverPhoto; playerOverPhoto = false; return r; }
		// the player the Now playing keys work on: main's (songs and videos), set by main, also while a photo is shown.
		// The photo viewer is a Player of its own and a page of its own: the keys never play the photo, only what goes
		// on under it. Without main's (a test, a caller that has none) it is the player it is given
		Player* mainPlayer = nullptr;
		// what plays stopped (any Stop, the end of the list): a page playing the file has nothing held here any more
		// (the page plays on: not told)
		// Stop ran: the hold ends, and the song's backdrop starts fading out now (the top panel, drawn before the
		// next tick, would otherwise have dropped its pages first)
		void stopped();
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
		// Manga's pages for test runs ("manga tab=1 view=0", "manage-extensions", …); "" on any other page
		std::string mangaStateLine() const;
		// the Info or About page's lines as they are drawn, for test runs
		std::vector<std::string> pageLines() const
		{
			std::vector<std::string> out;
			for (auto& l : infoLines) out.push_back(l.header ? "[" + l.label + "]" : l.label + ": " + l.text);
			return out;
		}
		// the rail's library sections (the media index's pages), for test runs: "" when another page shows
		std::string libPageName() const
		{
			if (page == Page::Music) return "music";
			if (page == Page::Videos) return "videos";
			if (page == Page::Books) return "books";
			return "";
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
		// ---- the rail's library sections, filled by themselves from the cards' media index (docs/redesign:
		// music-albums, music-artists, music-album, music-artist, videos, books): areas, ui_music.h / ui_videos.h /
		// ui_books.h; main hands each the index (MediaIndexWorker's) ----
		// (public: main reaches them to hand the index in)
		struct Music;
		std::unique_ptr<Music> music;
		struct Videos;
		std::unique_ptr<Videos> videos;
		struct Books;
		std::unique_ptr<Books> books;
		// the playlists: a plain struct the browser owns (browse->playlists), ui_playlists.h / ui_playlists.cpp
		struct Playlists;
		// Home and All recent: a plain struct the browser owns (browse->homeView), ui_home.h / ui_home.cpp
		struct Home;
		// ---- Search (Page::Search): an area (Area), ui_search.h / ui_search.cpp. The rail's Search key and L3 open
		// it; the cards' index answers the query (mediaIndex, main's worker) ----
		struct Search;
		std::unique_ptr<Search> search;
		// the cards' media index, as main's worker answers it (Music, Videos and Search read it; null: not built)
		MediaIndex* mediaIndex = nullptr;
		// the index's worker (main's): the pass's live counters, for the reading notice (null: none, as in none
		// of its pages is drawn)
		MediaIndexWorker* indexWorker = nullptr;
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
		// a song's album art arrived (the cover worker's "home:" answers, forwarded by main): into the shared
		// texture cache (Home's cards, the rail's Now playing key, Quick settings' block), on this thread
		void songCoversReady(std::vector<CoverWorker::Result> results);
		// the on-screen keyboard is up: its caret blinks (the loop wakes often enough to draw it)
		bool typing() const { return kbOpen; }
		Uint32 keyboardClosedAt() const { return kbClosedAt; }
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
		Uint32 lastInputAt() const { return webInputAt; }	// the last press or touch (the top panel's Top screen off counts from it)
		bool touchIsLocked() const;	// (the player screen's lock: PlayerPage, ui_player.h)
		// Settings open (START's page): for test runs
		bool settingsPageOpen() const { return page == Page::Settings; }
		// Search open (the rail's key or L3): for test runs (the state line says what it holds)
		bool searchPageOpen() const { return page == Page::Search; }

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
		// What the last bottom frame cost, by part (ms by the performance counter): the ground (the clear and the
		// backdrop), the page's body (and inside it a smooth list's rows), the shell (rail, header, hints), the rest
		// drawn over them (overlays, the keyboard, the veil), the present; the text lines rendered afresh (a text cache
		// miss goes through FreeType) and the list rows drawn afresh. main's "timing:" lines print it.
		struct FrameParts
		{
			double ground = 0, body = 0, list = 0, shell = 0, rest = 0, present = 0;
			int textMisses = 0, rowsDrawn = 0;
			double total() const { return ground + body + shell + rest + present; }
		};
		FrameParts parts;
		std::string partsLine() const;	// "ground 3.1, body 12.0 (list 9.8, 4 rows drawn), shell 6.2, ..."
		// a smooth list gliding: the ms to wait for its next frame (-1: none moves). main's loop comes round then
		int glideWaitMs() const;
		// the dim to `f` of its way (0 the level, 1 the dim); on false (no backlight) the veil stands in
		bool dimStep(double f);
		void undim();		// the level back, as it was (unless someone changed it meanwhile)
		bool dimmed() const { return offDim.from >= 0; }
		// the dim (or the veil standing in) still easing down: the loop comes round each 16 ms
		bool dimEasing() const { return offDim.since != 0 || (veilCancels && veilAlpha > 0 && veilAlpha < 170); }
		// a finger's event time (main sets it before pointer(); 0: the harness's, taken as now)
		uint32_t pointerMs = 0;
		// the bottom panel's pixels (RGBA, ABGR8888), drawn afresh: screenshots; the caller frees it
		SDL_Surface* capture(const Player& p);

	private:
		// "at 12:03": where a video was left (a song: "")
		std::string resumeNote(const std::string& path, Player& p);
		friend struct WebPlayerGlue;
		friend class SessionHost;
		friend struct WebPlaylistGlue;
		enum class Page { Main, Settings, Browse, Search, Info, About, Web, Manga, ExtSection, Extensions, MangaSeries,
			MangaRead, MangaSettings, MangaSources, DocText, DocPages, Music, Videos, Books };
		using Rect = lay::Box;	// (kitlayout.h: the kit's layout maths give and take the same box)
		// An overlay: a dialog or a sheet over the page (ui_overlay.cpp; docs/notes/overlay-stack.md). While it is on
		// the stack it gets the keys and the touches (the topmost one only: it is modal), is drawn over the page (all
		// of them, the oldest first) and has its state in signature(): press, pointer, renderPage and signature ask
		// the stack, so a new dialog is wired in nowhere else. One is written as a struct nested in Ui (declared
		// here, defined in its own .cpp), which lets it use Ui's drawing and state through `ui`.
		// a list that follows the finger and glides (ui_smoothlist.h; docs/notes/2026-11-21-smooth-scroll.md)
		struct SmoothList;
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
			// its list that scrolls smoothly, while one shows (the top sheet's comes before the page's in Ui::draw)
			virtual SmoothList* smoothList() { return nullptr; }
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
			// a button let go: the area's own held state (the player's volume hold) ends; most have none
			virtual void release(Button, Player&) {}
			virtual bool pointer(const Pointer& pt, int x, int y, Player& p) = 0;
			virtual void tick(Player&) {}	// each turn of the loop while its page shows
			virtual void draw(const Player& p) = 0;
			// everything draw() shows that Settings doesn't hold: the panel is drawn again only when this changes
			virtual std::string signature(const Player& p) const = 0;
			virtual bool animating() const { return false; }	// something of its moves by itself (Ui::animating)
			// its touches come before the updater's pop-up (a page that always had them so); the rest after
			virtual bool pointerFirst() const { return false; }
			// its downs come late: after the header's "<" and the empty page's button (its moves and lifts
			// still come where the others' do). Manga's pages took them so (asked per page: the reader's come early)
			virtual bool pointerDownLate() const { return false; }
			// its list that scrolls smoothly, while one shows (Ui::draw steps its glide and frees its slots when it goes)
			virtual SmoothList* smoothList() { return nullptr; }
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
			GROUP_WEB, GROUP_UPDATES, GROUP_ABOUT, GROUPS };	// (no Extensions: Extensions › Manage extensions, the owner)
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
		// the redesigned section pages' shared frame (ui_shell.h): the rail, the header and the hints.
		// Not an Area of its pages (Browse and Settings keep their keys and touches): Ui asks it directly
		struct Shell;
		struct ShellHead;
		std::unique_ptr<Shell> shell;
		// the More sheet (ui_more.cpp): a bottom sheet of tiles and rows, fed a list each time it is drawn (MoreSpec,
		// ui_kit.h), as an overlay. X on the player opens the player's (PlayerPage::moreSpec)
		struct MoreItem;
		struct MoreSpec;
		struct MoreSheet;
		void openMore(std::function<MoreSpec(const Player&)> build, const char* name = "more",
			std::function<bool()> alive = {}, std::function<void()> ended = {}, int at = 0);
		// ---- where Videos, Music and Books read from (ui_sources.cpp): the More sheets' right column ("Where videos come
		// from", "Web dashboard") and the Sources sheet each opens (docs/notes/2026-11-20-library-sources.md) ----
		// m.rows are the sheet's own (left); `section` goes in a right column beside them
		void moreTwoColumns(MoreSpec& m, std::vector<MoreItem> section);
		// the section's rows: where it reads from (its note the folder count), then the Web dashboard (its note live)
		std::vector<MoreItem> sourceMoreRows(LibKind kind, const Player& p);
		MoreItem webDashboardRow(const Player& p);
		void openSources(LibKind kind);
		// test only: every ui_kit.h "KIT VARIANTS" piece on one page, for a `shot:` (--press gallery:variants[:PAGE], 1..14)
		struct VariantsGallery;
		// ---- the player screen (Page::Main: a video, a song; a photo's viewer is still here in Ui): an area,
		// ui_player.h / ui_player.cpp ----
		struct PlayerPage;
		struct SongViewSheet;	// (ui_player.cpp: a song's chip's sheet, a table of it too)
		std::unique_ptr<PlayerPage> playerPage;
		struct PlayOnSheet;
		struct SheetRows;	// (ui_player.cpp: List's, View's and a song's sheets' rows on a smooth list)
		struct ListSheet;
		struct ViewSheet;
		struct AdjustSheet;	// (ui_player.cpp: Picture's Adjust page)

		// the readers' Brightness sheet (ui_light.cpp): the screens' backlight, Extra dim, the reader's filter
		// (an overlay: LightSheet, pushed by openLight)
		struct LightSheet;
		void openLight();
		// the same kind (ui_overlay.cpp): the browser-check message ("mangaerr", from Ui::Manga's mangaErrCheck)
		// and the quick settings ("quick", from toggleQuick). Each is a thin
		// overlay over the code it had (drawMangaErr, quickKey/quickPointer/drawQuick)
		struct MangaErrDialog;
		struct QuickSheet;
		void openMangaErr();
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
		// Info (Files' Y menu, the manga reader's series menu): a page of what a file or folder is, in groups (File,
		// then its Video / Audio / Subtitles tracks), scrolled; any key closes it (the FileInfoSheet overlay)
		struct InfoGroup
		{
			std::string title;
			std::vector<std::pair<std::string, std::string>> rows;	// a dim label, its value
		};
		std::string infoTitle;
		std::vector<InfoGroup> infoGroups;
		// p (when the caller has one): the tracks of the file shown, if it is the one loaded
		void openFileInfo(const std::string& path, const std::string& title, std::vector<std::pair<std::string, std::string>> extra = {},
			const Player* p = nullptr);
		// the same page for any groups (the manga series' details)
		void openInfoGroups(const std::string& title, std::vector<InfoGroup> groups);
		int infoGroupLines() const;	// the headings and rows, as the sheet's lines
		void infoGroupLine(int i, std::string& head, std::string& label, std::string& value) const;
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
			// the Now playing menu (a hold on the rail's key or Home's card): Now playing, Play / Pause, Like, Add to
			// playlist..., Show in folder, Stop
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
		// the browse menu (Files, Home, Playlists, Network, the mini bar's hold menu) is a More sheet named "ctxmenu": its
		// state stays ctxOpen/ctxItems/ctxCursor/ctxArmed, its list is built from them each time (ui_menus.cpp)
		MoreSpec browseMenuSpec(const Player& p);
		bool pickBrowseMenu(int i, Player& p);
		void openBrowseSheet();
		// the updater's question over the file list: a release found (Update now, Later, Not until tomorrow), then
		// the download (Hide, Stop the download), then Restart now / Later, or what went wrong (Try again, Details)
		bool popupClosed = false, popupFollow = false;
		int popupCursor = 0;
		bool popupDetails = false;	// the failed state's Details (Y) unfolded
		bool popupShown(const Player& p) const;
		bool popupKey(Button b, Player& p);	// its keys (Browse's: the question is over the file list)
		std::vector<const char*> popupItems() const;
		int popupSafe() const;	// the key B answers with (Later, Hide, Close): its row's index
		void choosePopup(int item, Player& p);
		void drawUpdatePopup();
		Rect popupRow(int item) const;	// a key's box, as the sheet draws it (the touches test this)
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

		// the on-screen keyboard, over the bottom panel: a form's field typed in
		bool kbOpen = false;
		int kbLayer = 0, kbCursor = 0, kbField = 0, kbPressed = -1;	// kbPressed: a key's number, -2 the back key
		// when the keyboard last closed: main redraws the panels for a second after it (the handheld can drop the
		// frame that takes the keyboard away), and a touch in the next 400 ms is let go (a finger still typing on the
		// keys it sees would land on the page under them: the rail's Music and Extensions, the owner found)
		Uint32 kbClosedAt = 0;
		Uint32 kbActiveAt = 0;	// the last key: the caret shows steadily for a moment after it, then blinks
		bool kbCaretShown() const;
		TextEdit kbEdit;
		lay::Keyboard kbLayout() const;	// the kit's keyboard over the panel (ui_keyboard.cpp)
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
		// Sound (music's): the equalizer's and effects' actions, ui_sound.h / ui_sound.cpp. The song's
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
		// the pieces of a page: the header ("<", the title, the battery), a tab, the footer's hints (a button's name in
		// yellow, what it does), the cursor's row, a list's scroll bar, a page with nothing to list
		struct Hint { std::string key, verb; };
		void drawHeader(const std::string& title, const Player& p, int titleRight = 0);	// titleRight: the title ends before it
		void drawTab(Rect r, const char* name, bool active, int size = 1);
		void drawHints(const std::vector<Hint>& hints);
		// while an old body is bridged into the shell (renderPage): the title its drawHeader gave and the hints its
		// drawHints (or footer line) gave, for the shell's header and hint bar (Manga::head)
		bool hdrCapture = false;
		std::string capTitle;
		std::vector<Hint> capHints;
		void drawFooterText(const std::string& text, SDL_Color c);
		void drawFocus(Rect r);
		void drawScrollBar(int top, int height, int first, int shown, int total);
		// btn: the button's glyph (G_PLUS when none); sub: a line, or two apart by '\n'
		void drawEmpty(const Glyph& g, const std::string& title, const std::string& sub, const char* action, const Glyph* btn = nullptr);
		std::string elideLeft(const std::string& s, int size, int maxW) const;
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
		// `fade` (the Title marquee's): the text's own alpha ramped to nothing over `w` units inside [x0, x1] at
		// the sides it names, drawn as narrow slices of the same texture (no new texture, no colour laid over it)
		struct EdgeFade
		{
			int x0 = 0, x1 = 0, w = 0;	// the box's edges and the ramp's width, in 640 x 480 units
			bool left = false, right = false;
		};
		void drawTextAt(const std::string& s, int size, SDL_Color c, int x, int y, int maxW, int align, bool middle,
			const EdgeFade* fade = nullptr);
		void drawFaded(SDL_Texture* t, SDL_Rect dst, const EdgeFade& fade);	// (drawTextAt's slices)
		// The Title marquee (docs/redesign/components.md; lay::marquee): a title too long for its box, drawn from its
		// start with its last 16 units fading out (the text's own alpha: any ground shows through), moving along twice (passes: 1 in a focused row), then
		// resting at its start. One moves per panel at a time (the first drawn in a frame: a focused row before the mini
		// bar); a sheet or menu over the page holds it at its start, and it starts again when it's gone, when the page
		// changes or the panel wakes. Scroll long titles off: cut with "…" as before
		void drawMarquee(const std::string& s, int size, SDL_Color c, int x, int cy, int boxW, int passes = 2);
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
	public:
		// a rounded outline e units thick (the focus ring): straight sides as rects, corners anti-aliased quarter rings,
		// the inside left as it was
		void strokeRound(Rect r, SDL_Color c, int rad, int e);
	private:
		SDL_Texture* ringCorner(int R, int E);
		std::map<int, SDL_Texture*> ringCorners;	// by R * 256 + E, in pixels
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
		// the rendered text lines, by size, colour and text: when full the line drawn longest ago goes (lrucache.h)
		LruCache<SDL_Texture*> cache{TEXT_CACHE, [](SDL_Texture* t) { if (t) SDL_DestroyTexture(t); }};
		static constexpr size_t TEXT_CACHE = 384;
		// measured widths and shortened ("...") strings, by size and text: a long line is shortened once, not
		// re-measured a character at a time on every redraw (the handheld's cores made lists lag)
		mutable std::unordered_map<std::string, int> widthCache;
		std::unordered_map<std::string, std::string> elideCache;
		std::string drawn, fontsDir;
		// ---- the smooth list (ui_smoothlist.h) ----
		SmoothList* listShown = nullptr;	// the shown area's (null: none)
		std::string drawnList;			// its offset when last drawn, in device pixels
		uint32_t frameAtMs = 0;	// when the last bottom frame began (the glide's frame pacing)
		uint32_t touchMs = 0;	// the finger's event time now (Ui::pointer, from pointerMs)

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
		// the dark veil over the bottom panel (Dim the controls while reading; before Screen off where there's no
		// backlight to lower: a computer), and whether a press or touch only lifts it
		int veilAlpha = 0;
		bool veilCancels = false;
		// The dim before Screen off (Settings: Dim before turning off), as a phone about to turn its screen off: the
		// bottom panel's backlight eases down to a third of its level over its last 5 s, set in sysfs only (never the
		// stored level bananad restores at boot). Any press or touch, a wake, Screen off and the quit put the level back.
		struct OffDim
		{
			int from = -1;		// the level before (thousandths; -1: not dimmed)
			int wrote = -1;		// the last level written: another found there means someone else set it (keys)
			uint32_t since = 0;	// when the dim began
		} offDim;
		static constexpr int DIM_RAMP_MS = 1200;
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
		int filesChanged = 0;		// a file op moved the cards' content: main re-walks the index
		void rootsCheck(Player& p);
	public:
		int rootsSerial() const { return rootsChanged; }
		// a file op changed what the cards hold (a delete, a rename or a move finished): main walks the index
		// again, as it does when a card is swapped, so Music, Videos and Search keep true
		int filesSerial() const { return filesChanged; }
		const std::vector<BrowseRoot>& places() const { return roots; }
		// main's lid-shut keys (docs/redesign/lid-and-incognito.md): the chapter before / after, on what plays
		void chapterStepForLid(int dir, Player& p) { chapterStep(dir, p); }
		// incognito, for main's lid, power-hold and idle-sleep rules (the quick settings and the page use the methods)
		bool incognitoOn() const { return incognito; }
		void incognitoSet(bool on, Player& p, bool note = true) { setIncognito(on, p, note); }
		void incognitoCloseAllNow(Player& p) { incognitoCloseAll(p); }
	private:

		static constexpr int ROW_Y = 84, ROW_H = 48, ROWS_SHOWN = 7;	// 7 rows end at 420, above the hints
		// the settings' rows by id: the subtitle style's, and the actions and pages that have their own code (the rest are
		// the settings table's: SET_ROW0 + the index in settingDefs(), ui_internal.h). Shown a group at a time
		// (settingRows), after a list of the groups. The manga reader's rows are its own page's (Page::MangaSettings):
		// EXT_NOTE is its note, no value
		static constexpr int STYLE_ROW0 = 0, CLEAR_ROW = STYLE_KEYS, CHECK_ROW = CLEAR_ROW + 1, INSTALL_ROW = CHECK_ROW + 1,
			TOP_LIGHT_ROW = INSTALL_ROW + 1, BOTTOM_LIGHT_ROW = TOP_LIGHT_ROW + 1, SOUND_ROW = BOTTOM_LIGHT_ROW + 1,
			EXT_NOTE = SOUND_ROW + 1, MANGA_CLEAR_ROW = EXT_NOTE + 1, MANGA_JAVA_ROW = MANGA_CLEAR_ROW + 1, SOUND_FX_ROW = MANGA_JAVA_ROW + 1;
		// the panels' backlights (BananaOS's own levels: changed here as its menu changes them)
		Backlight backlight;
		bool sameBrightness = true;	// the setting's, kept here for settingRows (no Player there)
		Volume volume;
		int volumePct = -1;	// as read when the quick settings opened, then as set there
		// ---- a song's album art, decoded by the one cover worker ----
		// Shared by Home's cards, the rail's Now playing key and Quick settings' Now playing block (it was
		// Home's own, 2026-11-19-home-art.md; one cache, not a copy each). A texture is one picture decoded
		// at one box's device pixels and rounded at upload, so its key holds the box too: the Continue
		// card's 128 x 84 crop is not the block's 48 square. ART_KEEP at most, the least recently drawn
		// freed. A path with no art (or an unreadable one) stays in artFailed: its placeholder is the
		// answer, and it is never asked again.
		struct ArtTex { SDL_Texture* tex = nullptr; uint64_t drawnAt = 0; };
		std::unordered_map<std::string, ArtTex> artTex;
		std::unordered_map<std::string, std::string> artPath;	// key -> path (a failure is the path's)
		std::unordered_map<std::string, int> artRad;		// key -> the radius the crop was cut for
		std::unordered_set<std::string> artAsked, artFailed;
		uint64_t artDrawn = 0;	// the LRU's order
		int artSerial = 0;	// what signature()s follow: a cover arrived, the page draws it now
		static constexpr size_t ART_KEEP = 48;	// (Music's grid shows up to 20 covers, its slots keep 20 more)
		// ---- Match album art's backdrop (docs/redesign "Match album art") ----
		// The same worker's "bg:" answers, asked once per playing song: the maths arrive on the main thread, one
		// SDL texture here (the top screen uploads its own copy into its GL), and the player's page and the top
		// panel both draw from the one answer. A song with no art answers with the fallback ground. A new answer
		// cross-fades in over half a second; until it lands the previous one stays.
		struct BackdropSlot
		{
			bool answered = false;	// an answer landed (else the slot is empty and never drawn)
			uint64_t hash = 0;	// the art it was made from (0: none hashed — no art, or nothing asked)
			bool hasArt = false;	// the song has an art (else the fallback ground is the answer)
			artbg::Art art;		// the maths (its rgb empty when there is no picture)
		};
		BackdropSlot bgNow, bgFade;	// what is drawn, and what it fades from while the cross-fade runs
		Uint32 bgFadeAt = 0;		// when the cross-fade began (0: none is running)
		Uint32 bgLeaveAt = 0;		// when the song stopped and bgFade began to fade out over the plain ground (0: not)
		std::string bgAsked;		// the song the worker was last asked for ("" none: nothing plays)
		// the two slots' pages (Kit::backdropPage: the picture and the theme's veil composed once, one texture),
		// the bottom panel's own (the fade's goes first); made when first drawn, again only for a new theme
		// Two kinds of each: the player page's veil ([BG_PLAYER]) and the browser's lighter one ([BG_LIST])
		static constexpr int BG_PLAYER = 0, BG_LIST = 1;
		SDL_Texture* bgTex[2] = {}, *bgFadeTex[2] = {};
		int bgTexTheme = -1;	// the theme the pages were composed for
		static void freePages(SDL_Texture* (&pages)[2]);
		void backdropTick(Player& p);		// the ask (Ui::tick): once per song, off when the switch is off
		// the ground (its caller cleared the BG): `kind` BG_PLAYER for the player page, BG_LIST for the browser's
		void drawBackdrop(const Player& shown, int kind = BG_PLAYER);
		void dropBackdrop();
		void leaveBackdrop();	// (the song stopped: the fade out, then dropBackdrop)
		// what the player page's redraws cost while the backdrop is on (the 5 s timing line: noteDrawCost)
		struct DrawCost
		{
			uint64_t since = 0;	// (performance counter) the window's start
			int draws = 0;
			double pageMs = 0, presentMs = 0, groundMs = 0, maxMs = 0;
		} drawCost;
		int drawCostLogs = 0;	// lines written for this song's backdrop (three at most: the log stays short)
		uint64_t drawCostOf = 0;
		void noteDrawCost(const Player& p, uint64_t t0, uint64_t t1, uint64_t t2);
	public:
		// the top panel's half reads these (it composes its own GL pages from the same answer); the player
		// page's signature takes the first two; main hands the worker's answers in here
		void backdropReady(const CoverWorker::Result& r);
		bool backdropOn(const Player& shown) const;		// the switch, the song, the theme: it is drawn at all
		// the browser's pages on it too (Home, Files, the sections, Settings, Search…): a song plays with the switch
		// on, and the page is no reader (a book's, a comic's, Manga's pages) and not the player's own (which draws it)
		bool browserBackdrop(const Player& p) const;
		bool groundIsBackdrop = false;	// this redraw's ground is the backdrop (the shell's header leaves it showing)
		bool backdropFading() const { return bgFadeAt != 0 && SDL_GetTicks() - bgFadeAt < BG_FADE_MS + 50; }
		// the song stopped (or the switch went off): the last backdrop fades out on the bottom panel's pages
		bool backdropLeaving() const { return bgLeaveAt != 0 && SDL_GetTicks() - bgLeaveAt < BG_FADE_MS + 50; }
		Uint32 backdropLeaveAt() const { return bgLeaveAt; }
		static constexpr Uint32 BG_FADE_MS = 500;	// the cross-fade, as the spec draws it
		uint64_t backdropHash() const { return bgNow.hash; }
		const artbg::Art& backdropArt() const { return bgNow.art; }
		bool backdropFadeAnswered() const { return bgFade.answered; }
		uint64_t backdropFadeHash() const { return bgFade.hash; }
		const artbg::Art& backdropFadeArt() const { return bgFade.art; }
		Uint32 backdropFadeAt() const { return bgFadeAt; }
	private:
		// the placeholder, then the decoded art over it when it has arrived (the edge and the rounded corners
		// stay the placeholder's); asks the worker once per key, never decodes here
		void drawSongArt(const std::string& path, const std::string& key, Rect r, int rad);
		// what drawSongArt would show in `r` now: 0 the placeholder (asked, or not yet), 1 the picture, 2 none for
		// good. A smooth list's row keys hold it, so a cover's arrival draws its row again
		int songArtState(const std::string& path, Rect r, int rad);
		// what plays in an art's box, as Home's cards have it: a song's art (its placeholder while it decodes,
		// or for good when it has none), a video's kind mark, a music mark when nothing plays
		void drawNowArt(const Player& bp, Rect r, int rad);
		// a book's cover (Books' shelf cells): the drawn sleeve stands in until the worker's answer, and for
		// good where there is none (a TXT, an unreadable one)
		void drawBookCover(const std::string& path, const std::string& key, Rect r, int rad);
		// what drawBookCover would show in `r` now (songArtState's for a book: a smooth list's row key holds it)
		int bookCoverState(const std::string& path, Rect r, int rad);
		// ---- Quick settings (F): a panel centred on the whole bottom screen, over the rail ----
		int quickCursor = 0;	// a row's index; -1: the Now playing block above Volume (the cursor opens on Volume)
		enum QuickRow { Q_VOLUME, Q_BRIGHT, Q_BOTTOM, Q_SAME, Q_WEB, Q_INCOGNITO, Q_QUIT };
		int quickSub = 0;	// Web dashboard's tile: 0 its switch, 1 its own key to the page (LEFT/RIGHT)
		// the Now playing block's pieces under a finger (pressedBtn): the card (opens the player), its three keys
		static constexpr int QUICK_NOW_CARD = 290, QUICK_NOW_PREV = 291, QUICK_NOW_PLAY = 292, QUICK_NOW_NEXT = 293;
		Uint32 quickHoldAt = 0;	// a finger on the block's Play: held, it takes playback over from a browser
		void closeQuick();	// (its overlay, if open: B, START, the Web button, a tap outside)
		std::vector<int> quickRows() const;
		bool quickKey(Button b, Player& p);
		bool quickPointer(const Pointer& pt, int x, int y, Player& p);
		void quickAdjust(int row, int dir, Player& p);
		void quickSlide(int row, int x, int y, Player& p);	// a finger on the row's bar: grabbed
		void quickChoose(int row, Player& p);
		void quickNow(Button b, Player& p);	// on the block: A play / pause, LEFT / RIGHT seek
		void quickNowKey(int what, Player& p);	// its three touch keys
		void quickStep(int dir, Player& p);	// L / R: previous / next, from any row
		void drawQuick(const Player& p);
		void drawQuickNow(const Player& p, const lay::QuickPanel& q);
		// the panel's boxes (kitlayout's QuickPanel: one place for the drawing and the touches, tested in
		// core_test.cpp): the level rows, the switch tiles (the Web tile's own key while the dashboard is
		// on) and Quit, from what quickRows() holds
		lay::QuickPanel quickLayout(const Player& p) const;
		void setBrightness(Backlight::Panel panel, int permille, Player& p);
		// (7 rows end at 386: the help's two lines at 400 stay clear of them; a longer group scrolls)
		static constexpr int SET_Y = 92, SET_H = 46, SET_SHOWN = lay::SET_ROWS, HELP_Y = 400;
		static constexpr int64_t STYLE_ROW = -2;	// the subtitle menu's last row: the settings page
		static constexpr int QUIT_BTN = 10, INFO_BTN = 13,	// (a photo's Close and Info)
			BACK_ICON = 16,	// "<" at the top left of every page: B
			EMPTY_BTN = 17;	// an empty page's button (no connections: Find on the network)
		// the web dashboard's page: New PIN and Turn off (its Turn on / Try again is EMPTY_BTN)
		static constexpr int WEB_NEWPIN = 540, WEB_OFF = 541, WEB_SWITCH = 542;
		// the select mode's bar (and Move's), the busy dialog's Cancel, the Add to... picker's rows (PICK_ROW + i)
		static constexpr int SEL_ALL = 530, SEL_MOVE = 531, SEL_ACT = 532, SEL_CANCEL = 533, MOVE_CANCEL = 534, MOVE_DONE = 535,
			BUSY_CANCEL = 536, PICK_ROW = 900;
		// the kept Menu of places (unreached): the Now playing line, the places' tiles (MENU_TILE + i), Quick settings and Quit
		static constexpr int MENU_NOW = 560, MENU_TILE = 561, MENU_QUICK = 570, MENU_QUIT = 571;
		// the dialogs' rows from DLG_ROW
		static constexpr int DLG_ROW = 700;
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
		void askQuit();	// Home's B: Quit Banana Player? (the kit's confirm sheet)
		struct QuitAsk;
		struct LidAskBox;
		// Opening Manga while a video plays (docs/redesign: manga-guard, ext-section-video): the confirm sheet that
		// says the video stops, Stay focused and B, with "Don't ask again" (the manga-ask-video setting). Pushed by
		// the Extensions page's Manga row (MangaGuard in ui_ext.cpp); a song or a photo is never asked about.
		void askMangaGuard(const Player& p, std::string what = "Manga", std::function<void(Player&)> go = nullptr);
		struct MangaGuard;
	private:

		// ---- playing behind the browser ----
		// the player left (B, "<", Back) while a song or video plays on: the browser is the base page; showPlayer ends it
		bool bgPlay = false;
		bool stopPressed = false;	// the player's own Stop (takeStopPressed)
		bool leaveWanted = false;
		bool stopKeepsPage = false;	// the Manga guard's Stop: the page shown stays (takeStopKeepsPage)
		bool playerOverPhoto = false;	// the Now playing key's tap over a photo (takePlayerOverPhoto)
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
		// back to what plays (SELECT on a browse page, the rail's Now playing key, Now playing, B at Home): the player.
		// False: nothing plays
		bool goNowPlaying(Player& p);
		bool nowPlayable(const Player& p) const { return p.playing() && !p.playingPhoto(); }
		// the player the Now playing block and the header's marks work on: main's (see mainPlayer above); without it, p
		Player& barPlayer(Player& p) const { return mainPlayer ? *mainPlayer : p; }
		const Player& barPlayer(const Player& p) const { return mainPlayer ? *mainPlayer : p; }
		void miniToggle(Player& p);	// play / pause what plays (Quick settings' block, Home's card)
		void openNowMenu(const Player& p);	// a hold on the rail's Now playing key (and Home's card): what plays' More
		bool playHoldMini = false;	// the Play hold (a page plays the file) is on Home's Now playing card
		int footY() const { return FOOT_Y; }
		// the header's status cluster (the playing bars, Wi-Fi, battery, clock), as last drawn: one touch target that
		// opens Quick settings, on every header
		Rect statusHit{0, 0, 0, 0};
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
