#pragma once
#include "battery.h"
#include "browser.h"
#include "input.h"
#include "javaext.h"
#include "panel.h"
#include "player.h"
#include "touch.h"
#include "updater.h"
#include "backlight.h"
#include "volume.h"
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
#include "mangafilters.h"
#include "mangajava.h"
#include "manga_ui_logic.h"
#include "mangalocal.h"
#include "manganetfolders.h"
#include "mangaremote.h"
#include "mangarepos.h"
#include "mangasource.h"
#include "mangastore.h"
#include "mangaworker.h"
#include "mangafetch.h"
#include "pagecache.h"
#include "coverworker.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

namespace bp
{
	class WebServer;
	class DurationWorker;

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
		// a small picture: rows of cells, '#' drawn (ui.cpp's G_*)
		struct Glyph { const char* const* rows; int h; };

		// A source's filter sheet (ui_manga_filters.cpp): the definitions filters() gave, the values Apply sends,
		// and where the sheet is. Kept per source while the app runs (5.2.1), not written to the state dir.
		struct MangaFilterSession
		{
			bool known = false, loading = false;
			std::string error;
			uint64_t serial = 0;		// the Filters job in flight (another serial's answer is stale)
			uint64_t javaSerial = 0;	// the Java host's filter request (a separate serial space from the worker's)
			std::vector<MangaFilter> defs;
			std::vector<MangaFilterValue> values;
			bool open = false;			// the sheet is up
			std::vector<int> path;		// group indices from the top, for the level shown
			int cursor = 0, scroll = 0;
			int checkAt = -1;			// the filter whose multi/tri option list is open (-1: the rows)
			int checkCursor = 0, checkScroll = 0;
			int textAt = -1;			// the text filter the keyboard is editing
		};

		// fontsDir: assets/fonts, the panel's font and the subtitle preview's
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
		// something moves by itself (a flick, a playlist's rows scrolling under a dragged one): the loop should come
		// round often
		bool animating() const { return loaderShown || playHoldAt != 0 || flickAt != 0 || plDrag.on || stripT0 != 0 || likePopAt != 0 || addFlashUntil != 0 || landFlashUntil != 0 || listPulseAt != 0; }
		// the header's playing mark moves (something plays behind the browser, the bottom panel lit): the loop comes
		// round a few times a second for its frames
		bool markMoving() const { return markAnim && panel.on(); }
		// the playlist edited in the browser, written now (its edits are written a second after they stop): before
		// quitting, or stopping what plays
		void flushPlaylist(Player& p) { plFlush(p); }
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
		void showPlayer()
		{
			if (!onPlayer) playerBack = {true, page, inMenu, menuTab, setGroup, cursor, scroll, menuReturnDir, menuReturnCursor, menuReturnScroll, browseDir, browseTab};
			if (!onPlayer) listPulseArm = true;	// (a playlist's song: the List key glows twice, drawMain)
			onPlayer = true;
			page = Page::Main;
			inMenu = false;
			bgPlay = false;
			ctxOpen = false;
			dropTouch();
		}
		// what plays (a song or a video) has its screen as the bottom panel's base: Main, or a page or the menu over it.
		// False while it plays behind the browser (the player left with B, "<" or Back)
		bool playerShown(const Player& p) const { return p.playing() && !bgPlay; }
		// the player's own Stop (Main's Stop, START › Playing › Stop, the mini bar's menu), once: main stops what plays
		bool takeStopPressed() { bool r = stopPressed; stopPressed = false; return r; }
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
		// the folder, the cursor and the scroll; Home, a playlist, START's menu...), what plays going on behind it. Never
		// opened from there (started with a file), or Show in folder asked for: the browser on lastPlayed (showBrowser)
		void backFromPlayer(Player& p, const std::string& lastPlayed, const PlaySource& from = {}, int index = -1);
		// opened behind the browser (a PlayRequest's stay): the browser stays the base page
		void playBehind() { bgPlay = true; }
		bool browsing() const { return page == Page::Browse; }
		std::string browseFolder() const { return browseDir; }
		// a video picked in the browser, once: the caller opens it
		// the browser's rows as they are listed (label, then note; Home: the Recent cards, then the tiles), for test runs
		std::vector<std::string> browseLabels() const;
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
		// ---- the manga reader: Ui (bottom panel, the library) and main's reader (top panel) talk only through these ----
		// a chapter to open on the top panel (Ui sets it and shows Page::MangaRead; main takes it once and opens it).
		// path: a folder of pictures or a CBZ; page: where to start (0-based); fit: MANGA_FIT_*
		struct MangaReadRequest
		{
			std::string seriesId, seriesTitle, chapterId, chapterTitle, path;
			bool archive = false;
			int page = 0, mode = 0, fit = 0;
			int book = 0;	// MANGA_BOOK_*: the series' own (takeMangaRead fills it)
			// a chapter from a source or a server: its pages' URLs, in order, and the headers they need (the source's
			// Referer...); path is then the chapter's id (the cache's key, never opened). Empty: path is a folder or a CBZ
			std::vector<std::string> pageUrls;
			std::vector<std::pair<std::string, std::string>> pageHeaders;
		};
		bool takeMangaRead(MangaReadRequest& out)
		{
			if (mangaRead.path.empty()) return false;
			out = std::move(mangaRead);
			mangaRead = {};
			out.book = mangaBookFor(out.seriesId);
			return true;
		}
		// the dashboard's series view (design 8): a chapter with what the reader knows of it. download is "", or the
		// mark the handheld's series page gives it ("queued", "running", "paused", "failed", "onCard")
		struct MangaWebChapter
		{
			std::string id, title, download;
			double number = -1;
			bool read = false;
			int page = 0, pages = 0, done = 0, total = 0;
		};
		struct MangaWebSeries
		{
			std::string id, title, source, error;
			bool local = false, pending = false;
			std::vector<MangaWebChapter> items;
		};
		// GET /api/manga/chapters: true with the list; false with pending (its source is being asked; try again) or
		// error. A local series comes from the card's scan, a source series from its worker as the handheld asks it.
		bool mangaWebChapters(const std::string& seriesId, MangaWebSeries& out);
		// POST /api/manga/download: the chapters queued as the Download sheet queues them (their page lists asked of
		// the source first); added counts the new asks. False (with error) when they can't be queued.
		bool mangaWebDownload(const std::string& seriesId, const std::vector<std::string>& chapterIds, int& added,
			std::string& error);
		// the Download sheet's queueing, shared with the dashboard: each chapter waits for its source's page list
		// before it becomes a queue item. added: how many new asks (already queued or on the card: none).
		int mangaDlQueueChapters(const std::string& sourceId, const std::string& scriptPath,
			const std::string& seriesId, const std::string& seriesTitle,
			const std::vector<MangaChapter>& chapters);
		// what the bottom panel asks of the reader, in order (main applies them each pass): Close; Step (v: screens, + down);
		// Page (v: +1 / -1, a whole page); Seek (v: the page index); TouchDown / Drag (v: the finger's dy as a fraction of
		// the panel's height, + down: the panels are the same size, so main scales it to the top panel) / Release, each
		// with the time in ms (Drag's v2: the finger's dx, as a fraction of the panel's width, + right); Mode (v: MANGA_RTL,
		// MANGA_LTR, MANGA_STRIP: the page under the view kept); Fit (v: MANGA_FIT_*: the page under the view kept)
		// Fullscreen (v: 1 on, 0 off; v2: the hinge gap in 480-line units: both panels are one tall view, the gap skipped)
		// Zoom (v: the factor, 1 = the fit, 1.5 … 8: the content under the view's middle stays there, and the pages in
		// view are decoded again at that width once the presses rest)
		// Rotate (paged modes only: each press turns the page 90° clockwise, 0 → 90 → 180 → 270 → 0; Strip ignores it;
		// kept per chapter, back to 0 on another chapter; the fit and zoom work on the turned page)
		// ZoomPoint (v: the factor; v2, v3: the tap point as fractions of the view width and height: zoom to that point
		// / back to the fit, as a double-tap on the page area does)
		// Book (v: MANGA_BOOK_*: held sideways, a page upright on each panel; Step turns a spread, Page moves one)
		struct ReaderCmd { enum Kind { Close, Step, Page, Seek, TouchDown, Drag, Release, Mode, Fit, Fullscreen, Zoom, Rotate, ZoomPoint, Retry, Book } kind = Close; double v = 0; int64_t ms = 0; double v2 = 0; double v3 = 0; };
		std::vector<ReaderCmd> readerCmds;
		// what the reader shows, as main last set it (the bottom panel draws from it; it goes into signature())
		struct ReaderStatus
		{
			bool open = false, loading = false, atEnd = false;	// atEnd: the strip's bottom reached (the next chapter's card)
			int page = 0, pages = 0;	// the page under the view's middle (0-based), the chapter's page count
			int mode = 0;	// the mode it reads in (MANGA_*)
			int zoomPct = 100;	// the zoom (1 = the fit, 100 % … 800 %)
			bool fullscreen = false;	// both panels show the pages
			int rotation = 0;	// the page turn in degrees (0, 90, 180, 270; paged modes only, 0 in Strip)
			int book = 0;	// MANGA_BOOK_*: held sideways, a page on each panel (page: the spread's first)
			std::string error;	// the chapter couldn't be opened ("" while fine)
			bool failedHere = false;	// a page in view failed (fetch, decode or no GPU memory): Try again shows
		};
		ReaderStatus readerStatus;
		// fullscreen: what the bottom panel shows of the view (main sets it each pass): each page's box as fractions
		// of the panel (x, y, w, h; y may run past 0..1), and the pages' pixels (RGBA, straight alpha) for those boxes;
		// gen changes with the chapter (the bottom panel's textures of the last one go)
		struct ReaderBox { int page; float x, y, w, h; };
		struct ReaderPix { int page = -1, w = 0, h = 0; std::shared_ptr<const std::vector<uint8_t>> rgba; };
		struct ReaderBottom { bool on = false; uint64_t gen = 0; std::vector<ReaderBox> boxes; std::vector<ReaderPix> pages; };
		ReaderBottom readerBottom;
		// the pages grid's thumbnails (docs/design/manga.md section 3): one per decoded page, ~96 px wide RGBA,
		// made by the reader as each page decodes; pages not decoded yet are absent (the Ui draws a numbered
		// placeholder). gen changes with the chapter: the Ui's textures of the last one go
		struct ReaderThumb { int page = -1, w = 0, h = 0; std::shared_ptr<const std::vector<uint8_t>> rgba; };
		struct ReaderThumbs { bool on = false; uint64_t gen = 0; std::vector<ReaderThumb> pages; };
		ReaderThumbs readerThumbs;
		// the reader closed itself (an error, or main stopped it): the bottom panel leaves its page
		bool readerOpen() const { return readerStatus.open; }
		// started with no file: the video's Quit closes it (back to the browser)
		bool browserMode = false;
		// Settings' Quit Banana Player: out of the player, from the browser or a video
		bool quitRequested = false;
		// the built-in updater (main's), and "Restart now": out with Updater::RESTART_CODE (launch.sh starts again)
		Updater* updater = nullptr;
		// the manga reader's network side (main's; started when the reader is first used with it on)
		class MangaNet* mangaNet = nullptr;
		// the manga reader's library and progress store (public for the web dashboard's manga routes)
		class MangaStore* mangaStorePublic() const { return mangaStore.get(); }
		// main's URL chapters put their pages in this same cache (the Manga settings' Clear cache empties it), not in
		// a second PageCache over the same folder
		PageCache& mangaPageCacheRef() { return mangaPageCache; }
		// the cover cache (the web dashboard's GET /api/manga/cover serves from it; the reader fills it)
		PageCache& mangaCoverCacheRef() { return mangaCoverCache; }
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
		// the manga library's covers, decoded on main's own worker (null: the generated ones only); main forwards its
		// event, and the answers become textures here, on the main thread
		CoverWorker* coverWorker = nullptr;
		void coversReady(std::vector<CoverWorker::Result> results);
		// the on-screen keyboard is up: its caret blinks (the loop wakes often enough to draw it)
		bool typing() const { return kbOpen; }
		// Find on the network: discovery runs here, its hosts sent to hostFound (on the main thread: main forwards them)
		Discovery* discovery = nullptr;
		std::function<void(const FoundHost&)> onFound;
		void hostFound(const FoundHost& h, Player& p);
		// a network folder's file names, as last listed (none if it wasn't): where a file picked there finds its
		// subtitles, its cover and the files that play after it
		std::vector<std::string> netFolderNames(const std::string& dir) const
		{
			auto it = netNames.find(dir);
			return it == netNames.end() ? std::vector<std::string>() : it->second;
		}
		// F alone: quick settings over whatever's on the bottom panel (volume, brightness, Same for both screens, Quit);
		// F again, B or a tap outside closes them
		void toggleQuick(Player& p);
		bool quickShown() const { return quickOpen; }
		// a level from bananad (its keys: "volume" or "brightness", percent): the quick settings show it
		void levelChanged(const std::string& what, int percent) { if (what == "volume") volumePct = percent; }

		// the bottom panel's backlight: off from its button, on again at the next touch
		bool screenOn() const { return panel.on(); }
		bool touchIsLocked() const { return touchLocked; }
		// START's menu's open tab ("playing", "settings", "about", "exit"), "" while it's closed: for test runs
		const char* menuTabName() const
		{
			static const char* names[] = {"settings", "about", "exit", "playing"};
			return inMenu ? names[menuTab] : "";
		}

		// The web dashboard (BP_WEB builds): main's server, null till it's first turned on, and its switch (start or
		// stop it, on the main thread). The routes reach the rest through WebPlayerGlue and WebPlaylistGlue
		// (src/web/api_*.cpp), on the main thread too
		WebServer* web = nullptr;
		std::function<void(bool)> webPower;
		// Page::Web open: a browser signing in needs no toast there (the page counts it)
		bool webPageShown() const { return page == Page::Web; }
		// a folder changed from the web (an upload, a delete): listed again if the browser shows it
		void folderChanged(const std::string& dir, Player& p);
		// the volume card shown by a change from the web: how long main keeps it up (as takeCardShown), once
		int takeLevelShown() { int ms = levelMs; levelMs = 0; return ms; }
		// the web's Stop: main closes what plays (back to the browser), once
		bool takeStopRequest() { bool r = stopRequest; stopRequest = false; return r; }
		// the handheld's volume, percent (-1 unknown): read once, then followed (bananad's keys, the quick settings, the web)
		int volumeLevel();
		// the playlists' store serial (-1 none): the web's lists follow it
		int playlistSerial() const { return plStore ? plStore->serial() : -1; }
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
		bool plIsLiked(const std::string& file) const { return plStore && plStore->isBuiltinFile(file); }
		// a page in a browser plays what plays here (the Session's output is that tab): the handheld holds it paused
		bool pageIsOut() const;
		// how this page can take a page's playback back, for the top panel's card: 1 hold A (the player), 2 elsewhere
		// (A does something else there: the card says the player can)
		int takeOverHint(const Player& p) const;
		// the top panel's level card (Volume / Brightness, as Player::showLevel has mpv draw it) as RGBA pixels, at the
		// panels' scale: for a top panel mpv doesn't draw (the manga reader's pages)
		std::shared_ptr<const std::vector<uint8_t>> levelCard(const std::string& label, int percent, int& w, int& h);
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
		enum class Page { Main, Tracks, Chapters, Settings, Browse, Info, About, Exit, Sound, Web, Playing, UpNext, Manga,
			Extensions, MangaSeries, MangaRead, MangaSettings, MangaSources };
		struct Rect { int x, y, w, h; bool hit(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; } };
		struct Row { int64_t id; std::string label; bool selected; std::string note = ""; };

		std::vector<Row> rows(const Player& p) const;
		void openMenu(Page pg, const Player& p);
		// Tracks: one page, four tabs in this order, those that apply (tracksTabs): Chapters (a file with chapters; the
		// Chapters page, Page::Chapters), Subtitles, Audio and Picture (Fit, Fill, Stretch) (a video; Page::Tracks).
		// L/R (and Y/X) step between them. SELECT on a video opens the last one shown (openTracksPage); none shown yet:
		// Chapters where there are some, else Subtitles. A line of the player's opens its own tab
		enum TracksTab { TRACKS_CHAPTERS, TRACKS_SUBS, TRACKS_AUDIO, TRACKS_PICTURE };
		int tracksTab = TRACKS_SUBS;
		bool tracksTabUsed = false;	// a tab was shown this run (X opens it again)
		TrackType trackType() const { return tracksTab == TRACKS_AUDIO ? TrackType::Audio : TrackType::Sub; }
		std::vector<int> tracksTabs(const Player& p) const;
		void openTracks(int tab, const Player& p);
		void openTracksPage(const Player& p);
		void stepTracksTab(int dir, const Player& p);	// the tab before (-1) or after (+1), no wrap
		// the i-th of n tabs shown: three or fewer 152 wide, four share the room before the clock
		// the tabs share the room between the back key and the header's status (clock, Wi-Fi, battery): tabsRight, set
		// as the header is drawn (statusLeft), so a tap hits what was drawn
		int tabsRight = 630;
		Rect tracksTabRect(int i, int n) const
		{
			int room = tabsRight - 70, step = std::min(n > 3 ? 122 : 156, n > 0 ? room / n : room);
			return {70 + i * step, 10, step - 4, 44};
		}
		void drawTracksHeader(const Player& p);
		// the file's chapters (the Tracks page's Chapters tab): the cursor on the one playing
		void openChapters(const Player& p);
		void drawChapters(const Player& p);
		// Up next (Y on the player): the list that plays, in its own order (the Session's queue: Shuffle reorders
		// nothing shown), the one playing marked, scrolled to it; A plays a row as a pick from that list does (the
		// Session's Open: Shuffle and Loop stay as set)
		std::vector<std::string> upNextList(const Player& p, int& current) const;
		void openUpNext(const Player& p);
		void playUpNext(int i, Player& p);
		void drawUpNext(const Player& p);
		// the chapter line over the bar (the one playing), and the two track lines under the buttons: each opens its
		// page. Their ids are apart from buttonRect's grid and the photo's
		static constexpr int LINE1_BTN = 500, LINE2_BTN = 501, CHAPTER_BTN = 502;
		static constexpr int CHAPTER_Y = 126;
		bool chapterLineShown(const Player& p) const;
		void choose(int i, Player& p);
		void changeSetting(int row, int dir, Player& p);
		// Settings opens on its groups (setGroup -1); A or a tap opens one, B goes back to them
		// (Web dashboard is a tile like the groups, but its own page: openGroup and stepGroup open Page::Web)
		enum SetGroup { GROUP_SUBS, GROUP_MUSIC, GROUP_PHOTOS, GROUP_REMEMBER, GROUP_FILES, GROUP_CONTROLS, GROUP_UPDATES, GROUP_WEB,
			GROUPS };
		int setGroup = -1;
		std::vector<int> settingRows() const;	// the open group's rows, by id
		void openGroup(int g);
		Page home(const Player& p) const { return playerShown(p) ? Page::Main : Page::Browse; }
		// HOMEDIR Home, "" the cards (Files' top), RECENT the recent videos (All recent)
		void enterFolder(std::string dir, Player& p);
		// the browser's four sections, opened from Home's tiles: Files (the cards), Playlists (the saved ones, then their
		// items), Network (the saved connections, then their folders) and Extensions (a placeholder). L/R step to the
		// one before or after (no wrap), at its top; the header's markers by touch. tabDir: where each was left (the
		// select mode goes between Files and Network keeping its place)
		enum BrowseTab { TAB_LOCAL, TAB_PLAYLISTS, TAB_NETWORK, TAB_EXT, BROWSE_TABS };
		int browseTab = TAB_LOCAL;
		std::string tabDir[BROWSE_TABS];
		void switchTab(int tab, Player& p);	// to where the section was left
		void openSection(int tab, Player& p);	// to its top
		void setSection(int tab);	// the section a page about to be entered is in (where this one was kept)
		bool atFirstPage() const;	// a section's top (or All recent): B goes Home from it
		bool atHome() const;
		// the section markers in a section's header (L, four marks, R), left of the battery (and the web's globe)
		Rect pipRect(int k, const Player& p) const;
		void drawSectionHeader(const Player& p);

		// Home (browseDir HOMEDIR), the browser's first page: the Recent strip (a card a recent file, then All recent)
		// over the four sections' tiles, two by two. The focus is on the strip or the tiles; UP/DOWN go between them
		// (back to the card or the column left), LEFT/RIGHT along. The focused card is kept in the strip's second
		// slot, the strip easing there over 120 ms (stripT0: tick moves it)
		struct RecentItem
		{
			std::string path, name, sub, where, at;	// at: where it was left ("0:27"), "" from the start
			const Glyph* kind;
			double frac;	// the bar: how far into it (played through: 1)
			bool net;
		};
		std::vector<RecentItem> recent;	// as listed with Home (or All recent)
		void listRecent(Player& p);
		// the history changed from elsewhere (the web): Home or All recent listed again, the focus kept
		void recentEdited(Player& p);
		enum HomeZone { HOME_STRIP, HOME_GRID };
		int homeZone = HOME_STRIP, homeStrip = 0, homeGrid = 0, homeCol = 0;
		bool homeFresh = true;	// not yet shown this run: the focus starts on the newest card (Files with none)
		static constexpr int CARD_W = 240, STRIDE = 250, CARD_Y = 84, CARD_H = 104, STRIP_TOP = 80, STRIP_H = 116;
		int stripCount() const { return (int)recent.size() + 1; }	// (+ All recent)
		int stripMax() const { return std::max(0, 16 + stripCount() * STRIDE - 10 + 16 - W); }
		int stripTarget(int i) const { return std::clamp((i - 1) * STRIDE, 0, stripMax()); }
		double stripOff = 0, stripFrom = 0, stripTo = 0;
		Uint32 stripT0 = 0;
		void setStrip(int i, bool animate = true);
		void stripStep();	// the eased scroll, a frame
		Rect tileRect(int g) const;
		std::string tileSub(int g) const;
		// the tiles' lines, made again when the playlists or the connections change
		mutable std::string tileSubs[BROWSE_TABS];
		mutable int tileSubsKey = -1;
		bool homeKey(Button b, Player& p);	// true: Quit chosen in B's dialog, leave
		void goHome(int zone, int idx, Player& p);	// back to Home, the focus there
		void homeOpen(Player& p);	// A: the card's file, All recent, a tile's section
		// a finger on Home: on a card (a tap plays, a hold opens its menu, a drag scrolls the strip) or a tile
		bool homeTouch = false, homeMoved = false;
		int homeDownX = 0, homePress = -1;	// homePress: a card's index, or HOME_TILE + a tile's
		double homeDownOff = 0;
		Uint32 homeHoldAt = 0;
		static constexpr int HOME_TILE = 1000;
		bool homePointer(const Pointer& pt, int x, int y, Player& p);
		// B on Home with nothing playing asks first; the dialog is Home's, over the page (drawHome)
		bool quitOpen = false;
		int quitCursor = 1;	// Cancel first
		// laid out as Quick settings (F) is: its panel, its rows, its red Quit button
		static constexpr Rect QUIT_BOX{40, 119, 560, 198};
		Rect quitRow(int i) const { return {56, QUIT_BOX.y + 56 + i * 62, 528, 54}; }
		void drawQuitDialog(bool quitPlaying);	// (playing: it says quitting stops it)
		void drawHome(const Player& p);
		bool showHidden = false;	// the setting, kept here for drawBrowseMenu (no Player there)
		bool mangaOn = false;		// the manga reader's switch, kept here for tileSub (no Player there)
		// a recent file played (as All recent's rows play one), from its start, shown in its folder (Files or Network)
		void openFile(const std::string& path, Player& p);
		void showInFolder(const std::string& path, Player& p);
		// Y on a card (or on All recent's row, a hold on a card): Resume from / Play, Play from the start, Show in
		// folder, Add to playlist..., Remove from Recent; X on Home (and All recent): Clear Recent, Show hidden folders,
		// Settings
		std::string ctxRecent;	// the file the menu is for
		void openRecentMenu(const std::string& path);
		void openHomeOptions();
		// Playlists (browseDir PLROOT, the list; PLDIR + its file, a playlist's items). The store is null with no state
		// folder. plCur: the playlist open (its items are browseRows, in its order; PL_ITEM rows)
		std::unique_ptr<PlaylistStore> plStore;
		MangaReadRequest mangaRead;	// (takeMangaRead)
		Playlist plCur;
		bool plLoaded = false;	// plCur read (false: the file couldn't be)
		PlayRequest playRequest;
		// the order the last playlist played in (shuffled or not): the player's index -> the item's, for coming back
		std::vector<int> plOrder;
		std::string plOrderId;
		// a playlist item that isn't there: checked as its row first shows (access(), cached till the list is read
		// again). 0 there, 1 not found (a local file), 2 its connection deleted; -1 not checked yet
		mutable std::vector<int8_t> plMissing;
		mutable int plMissingSerial = 0;
		int plMissingAt(int row) const;
		void plCheckShown();	// the rows in view checked
		void plRows(Player& p);	// browseRows from plCur
		std::vector<std::string> plNames() const;	// the playlists' names (for a new one's checks)
		void plPlay(const Playlist& pl, int index, bool shuffle, Player& p);
		void plSaved(Player& p, const std::vector<int>& remap);	// plCur edited: saved (soon), the list again, the player told
		// plCur's edits wait a second for the next (a run of moves is one write): written by tick, or now by plFlush
		// (leaving the page, playing, stopping, quitting)
		bool plDirty = false;
		Uint32 plDirtyAt = 0;
		void plFlush(Player& p);
		// a playlist that plays was edited (remap: its old items' new indexes; any index none maps to is new, played
		// last): the player gets its new items in the order they play
		void plTellPlayer(const Playlist& pl, const std::vector<int>& remap, Player& p);
		void plRefresh(Player& p);	// browseRows from plCur again, the cursor and the scroll kept

		// Selecting, one mode with three uses: Edit (a playlist's items: remove, move), Add (files on the Local and
		// Network tabs, kept across folders and both tabs, added in the order ticked) and Pick (a folder for Add
		// folder...). The tab bar is a header then, and the footer a bar of buttons
		enum SelMode { SEL_NONE, SEL_EDIT, SEL_ADD, SEL_PICK };
		int selMode = SEL_NONE;
		std::set<int> plSel;	// Edit's: plCur's indexes
		std::vector<std::string> pickOrder;	// Add's: full paths, in the order ticked
		std::set<std::string> picked;
		std::string selDest, selDestName;	// the playlist adding goes to ("": Add to... asks)
		int selSerial = 0;	// a tick changed
		Uint32 dropArmed = 0;	// Add's B at a first page with files ticked: again within 2 s drops them
		bool removeArmed = false;	// Edit's Remove pressed once
		void selStart(int mode, const std::string& dest);
		void selEnd();
		void selLeave(Player& p);	// out of it; a playlist's Add files... and Add folder... go back to it
		bool selKey(Button b, Player& p);	// true: it was the mode's
		bool selChoose(int row, Player& p);	// A or a tap on a row: true when it was the mode's (a tick)
		void selBack(Player& p, bool now);	// now: no second press asked (the header's ✕)
		void selAct(Player& p);	// X: Remove, Add, Add this folder
		void selAll(Player& p);	// SELECT
		bool selTickable(int row) const;
		bool selTicked(int row) const;
		int selCount() const { return selMode == SEL_EDIT ? (int)plSel.size() : (int)picked.size(); }
		int selFolderFiles(int& ticked) const;	// this folder's files that can be ticked (and how many are)
		bool pickable() const;	// Pick: a folder of a card's or a server's (Add this folder)
		std::string folderName(const std::string& dir) const;
		// Move (Edit's Y, an item's menu): the ticked items gathered into a block, moved a row (UP/DOWN) or a page
		// (LEFT/RIGHT), A drops it (saved), B puts everything back. moveRemap: the order before -> now
		bool moving = false, moveFromMenu = false;
		std::vector<PlaylistEntry> moveOrig;
		std::set<int> moveOrigSel;
		std::vector<int> moveRemap;
		void moveStart(Player& p);
		void moveStep(int dir, int times, Player& p);
		void moveEnd(bool keep, Player& p);
		void moveFollow();	// the block in view
		// a finger on a row's grip (Edit): the row (or the ticked items, gathered) dragged; a line where it lands, the
		// list scrolling near its edges; lifted, dropped and saved
		struct PlDrag { bool on = false; int y = 0; Uint32 scrollAt = 0; } plDrag;
		std::set<int> dragSel;
		void dragBegin(int row, int y, Player& p);
		int dragSlot() const;	// the gap the finger is at (0..n, before that row)
		int dragTarget() const;	// where the block's first row goes
		void dragEnd(Player& p);
		// adding: "Add to..." (the playlists, New playlist... first), then the files or a folder's media added
		bool pickerOpen = false;
		int pickerCursor = 0, pickerScroll = 0, pickerTouchY = 0, pickerScrollStart = 0;
		bool pickerScrolled = false;
		std::vector<PlaylistStore::Info> pickerList;
		std::string pickerFolder;	// a folder's media ("Add to playlist..." on a folder); "" the ticked files
		// one file (Main's Add square, the Playing tab, a Recent card, a Liked item): "Add “title” to…", the playlists
		// last added to first, ✓ Added on those that have it (pickerHas), whose A asks again first (pickerArmed)
		std::string pickerOne, pickerOneTitle;
		double pickerOneDuration = -1;
		std::vector<char> pickerHas;
		int pickerArmed = -1;
		void openPicker(const std::string& folder, Player& p, bool one = false);
		void openAddOne(const std::string& path, const std::string& title, double duration, Player& p);
		void pickerChoose(int i, Player& p);
		void pickerNew(Player& p);	// New playlist...: named on the keyboard, then added to
		void pickerAdd(const std::string& dest, Player& p);
		bool pickerKey(Button b, Player& p);
		bool pickerPointer(const Pointer& pt, int x, int y, Player& p);
		void drawPicker();
		// as tall as its rows, 7 at most (then 392, from y 22)
		static constexpr int PICKER_SHOWN = 7;
		Rect pickerBox() const
		{
			int h = 66 + std::min((int)pickerList.size() + 1, PICKER_SHOWN) * 46;
			return {96, (436 - h) / 2, 448, h};
		}
		Rect pickerRow(int i) const { return {112, pickerBox().y + 52 + (i - pickerScroll) * 46, 416, 42}; }
		bool plNameThenAdd = false;	// the name typed is a new playlist to add to (pickerFolder's, the one file, or the ticked files)
		bool pickerNewOne = false;	// the playlist added to was just made (the card says so)
		std::string plNameAddFolder;
		void addFolderTo(const std::string& dir, const std::string& dest, bool back, Player& p);
		// added to dest (the end; up to the cap): what was added said, and back: to the playlist, on the first added.
		// False: nothing was (none to add, the playlist full, a file that couldn't be read or written)
		bool addTo(const std::string& destRef, const std::vector<PlaylistEntry>& add, int photos, const std::string& from,
			bool back, Player& p);
		std::vector<PlaylistEntry> pickedEntries() const;
		void returnToPlaylist(const std::string& file, int item, Player& p);
		// a network folder added before it was ever listed: listed first, a busy dialog meanwhile (B cancels)
		struct AddPending { std::string dir, dest; int serial = 0; bool back = false; } addPending;
		int addSerial = 1 << 29;
		void cancelAdd(Player& p);
		void drawBusy();
		// the select mode's header (over the tabs), where line and bar (over the footer)
		void drawSelHeader(const Player& p);
		void drawSelBar();
		void drawBarButton(int id, const Glyph* g, const std::string& label, const char* key, SDL_Color ink, bool filled, bool off);
		void drawWhere(int x, int maxW);	// the browser's where line: its end kept, the last folder brighter
		std::string whereOf(const std::string& dir) const;	// "Videos / Shows", "MY-PC / Media / Movies"
		// the Main page's line under the title: what plays from (a folder, a network folder, a playlist)
		void drawSource(const Player& p);
		static constexpr int SOURCE_Y = 70;
		void plDelete(const std::string& file, Player& p);
		// naming a playlist on the keyboard (PL_NAME): New, or Rename (plRenameFile); a name refused keeps it up
		bool plRenaming = false, plNameSubmit = false;
		std::string plRenameFile, plNameNote;
		// name: Rename's, or a new one's to start from ("" New playlist, the first number free)
		void plNameOpen(bool rename, const std::string& file, const std::string& name);
		void plNameDone(Player& p);
		// the item a menu is for (a playlist's file and the item's index; -1 the playlist itself)
		std::string ctxPlFile;
		int ctxPlItem = -1;
		std::string ctxTitle, ctxNote;	// a playlist's menu: its title, and the line under it ("12 items", why it's missing)
		bool holdRow(int row) const;	// a finger held on it opens its menu
		// a playlist's network items: their folders listed for their subtitles and covers (Player::netFolderWanted),
		// each once a play; the answers by serial
		std::set<std::string> sideAsked;
		std::map<int, std::string> sidePending;
		int sideSerial = 1 << 30;
		// a network folder listing: what's asked of the worker now, and what went wrong last
		int netSerial = 0;
		int durSerial = 0, durLo = -1;	// the worker's list (one a listing) and the rows on screen it was last told
		bool netLoading = false;
		std::string netSelect;	// a file to put the cursor on once its folder is listed (back from playing it)
		std::map<std::string, std::vector<std::string>> netNames;	// netFolderNames'
		std::string netOpenPending;	// a network file to open once its folder is listed
		void browseUp(Player& p);
		// a folder below a section's top (a card's folder, a server's, a playlist): a ".." row (up a folder) above the
		// list. It isn't one of browseRows (a playlist's rows are its items' indices): it's row -1, where the cursor and
		// the scroll can be (listFirst()), so nothing that counts, selects or plays rows ever sees it
		bool upRow() const;
		int listFirst() const { return upRow() ? -1 : 0; }
		void drawUpRow(int y, bool at);
		// the browser's cursor on a row and its window as full as the list allows (a list that came back shorter:
		// a scroll from before would show its last rows only)
		void fitList();
		void dropTouch();
		// the browser's menu: X anywhere (or a finger held on a folder). On a card's folder Hide folder / Unhide
		// folder (a ".hidden" file in it); everywhere Refresh (the list read again: a file copied in meanwhile); Cancel
		enum CtxItem { CTX_HIDE, CTX_REFRESH, CTX_CANCEL, CTX_ADD, CTX_FIND, CTX_EDIT, CTX_TEST, CTX_DELETE,
			// a playlist's (Y on one, X in one) and its items'
			CTX_PL_NEW, CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_RENAME, CTX_PL_DELETE, CTX_PL_DEDUP, CTX_PL_HERE, CTX_PL_REMOVE,
			CTX_PL_SHOW, CTX_PL_ADD_FILES, CTX_PL_ADD_FOLDER, CTX_PL_SELECT, CTX_PL_MOVE,
			// Local and Network: X's Select files, a folder's Add to playlist...
			CTX_SELECT_FILES, CTX_ADD_TO,
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
		std::vector<const char*> popupItems() const;
		void choosePopup(int item, Player& p);
		void drawUpdatePopup();
		Rect popupRow(int item) const { return {136, 252 + item * 50, 368, 44}; }
		std::string updateStatus() const;	// Check now's value
		// the battery's icon (and percent, a bolt while charging), its right edge at right; read every 10 s
		BatteryState battery;
		Uint32 batteryAt = 0;
		void readPower();
		// minX: the clock only if it fits right of it (START's tabs)
		void drawBattery(int right, int y, const Player& p, int minX = 0);
		// the clock, left of the battery and the web's globe ("14:32", "2:32 PM"; "" off, or over a photo); where the
		// header's marks start (the battery's right edge at 616): a title or the section markers end before it
		std::string clockText(const Player& p) const;
		int statusLeft(const Player& p) const;
		static constexpr int BATTERY_W = 110;	// what a title leaves for it
		bool folderRow(int row) const;
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

		// the Network tab's other pages, as the browser's (browseDir FORMDIR, FINDNET). A connection's form: its
		// rows (form*), each opening the keyboard or stepping through its choices
		NetServer form;
		std::string formId;			// the connection edited, "" a new one
		std::string formPassword;	// typed this time
		bool formPasswordTyped = false;
		std::string formNote;		// Save's complaint (no server yet)
		void openForm(const NetServer* existing, const FoundHost* found, Player& p);
		void formRows();			// browseRows from form
		void formChoose(int64_t id, Player& p);
		void formChange(int64_t id, int dir);
		void formSave(Player& p);
		NetServer formServer() const;	// the form as a server (the port filled in)
		// Test connection (the form's row, a connection's menu): the answer comes as a NetListing (test)
		int testSerial = 0;
		std::string testText;
		void startTest(const NetServer& s, const std::string& password);
		// the servers found on the network (Find on the network)
		std::vector<FoundHost> found;
		bool findWasRunning = false;
		void findRows();

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
		// START's menu, over a video or the browser: tabs Settings, About, Exit (Recent is Home's); L/R or a tap
		// change tab, B or START close it
		// Playing (Like, Add to playlist..., Show in folder, Info) is first, while a song or video plays (menuTabs)
		enum Tab { TAB_SETTINGS, TAB_ABOUT, TAB_EXIT, TAB_PLAYING, TABS };
		std::vector<int> menuTabs(const Player& p) const;	// the tabs shown, in their order
		bool playingTabShown(const Player& p) const;
		Rect menuTabRect(int i, int n) const;	// the i-th of n tabs
		bool inMenu = false;
		int menuTab = TAB_SETTINGS;
		// the browser's folder before the menu, and its cursor: back to them after
		Page menuReturnPage = Page::Browse;	// the menu opened over Manage extensions: back to it
		std::string menuReturnDir;
		int menuReturnCursor = 0, menuReturnScroll = 0;
		void openMainMenu(Player& p, int tab = TAB_SETTINGS);
		void showTab(int tab, Player& p);
		void closeMenu(Player& p);
		Rect tabRect(int tab) const { return {72 + tab * 136, 10, 128, 44}; }
		void drawTabs(const Player& p);
		// the Playing tab: the file's card, then its rows (PLAY_ROW + i)
		// Now playing (while it plays behind the browser) first, Stop last
		enum PlayingRow { PR_LIKE, PR_ADD, PR_SHOW, PR_INFO, PR_NOW, PR_STOP, PR_PLAYON };
		std::vector<int> playingRows() const;
		void drawPlaying(const Player& p);
		bool playingKey(Button b, Player& p);
		bool playingChoose(int row, Player& p);	// true: leave (Show in folder closes what plays)
		Rect playingRowRect(int i) const
		{
			int n = (int)playingRows().size(), step = n > 6 ? 42 : n > 5 ? 46 : 54;	// (six or seven: closer, above the footer)
			return {16, 142 + i * step, 608, step - 4};
		}
		// Play on (the Playing tab's row, while a dashboard page that can play is open): where it plays, the handheld or
		// one of those pages, as a dialog over the tab. Picking one is the Session's SetOutput (SessionHost::sendTo)
		struct OutChoice { std::string tab, name; };	// tab "": the handheld
		std::vector<OutChoice> outChoices() const;	// the handheld first, then each page that can play (WebServer::playTabs)
		bool playOnShown() const;	// the row: a page that can play is open, or one is the output
		bool playOnOpen = false;
		int playOnCursor = 0;
		void openPlayOn();
		bool playOnKey(Button b, Player& p);
		bool playOnPointer(const Pointer& pt, int x, int y, Player& p);
		void playOnChoose(int i, Player& p);
		void drawPlayOn(const Player& p);
		Rect playOnBox() const { int n = (int)outChoices().size(); int h = 122 + std::min(n, PLAYON_SHOWN) * 60; return {70, std::max(16, (436 - h) / 2), 500, h}; }
		Rect playOnRow(int i) const { Rect b = playOnBox(); return {b.x + 20, b.y + 106 + (i - playOnScroll) * 60, b.w - 40, 54}; }
		static constexpr int PLAYON_SHOWN = 4, PLAYON_ROW = 2000;	// (above the picker's PICK_ROW + its rows)
		int playOnScroll = 0;
		// Show in folder from the Playing tab: what plays closed, then its folder shown (showBrowser)
		std::string showAfterLeave;
		bool showSourceAfterLeave = false;	// List (Y) on a playlist's song: the player left for that playlist's page
		// Like: toggled (the heart, START's Playing tab, L3), the heart's pop and the top panel's card
		void likeNow(Player& p);
		void likeCard(Player& p, bool liked, const std::string& title);
		Uint32 likePopAt = 0, addFlashUntil = 0;
		// the Playing tab's "In Road trip": the playlists the file is in, read again when they or the file change
		mutable std::string playingIn, playingInPath;
		mutable int playingInSerial = -1;
		// Main's header squares (a song or a video): Add to playlist, Like
		bool headerActions(const Player& p) const;
		void drawExit(const Player& p);
		bool clearHistory(Player& p);	// true when it cleared (the second time)
		bool act(int id, Player& p);	// a touch button let go of on it
		bool pressAnyway(Button b, Player& p);
		// music playing: X loop, Y chapters (none: nothing), SELECT the Display popup (a video: audio, subtitles,
		// picture size)
		bool musicKey(Button b, Player& p);
		// the music key's mode: MODE_SHUFFLE when Shuffle is on (with whichever Loop), else the Loop (LOOP_*)
		int playMode(const Player& p) const;
		std::string modeLabel(const Player& p) const;	// what the screen says as X changes it
		// Display, over a song's page (SELECT): what the top panel shows, and the visualizer's style; each change
		// applied at once (the player rebuilds the graph once the changes stop)
		bool displayOpen = false;
		int displayRow = 0;	// 0 Show, 1 Style
		bool displayShown(const Player& p) const { return displayOpen && page == Page::Main && !inMenu && p.playingAudio(); }
		bool displayKey(Button b, Player& p);
		bool displayPointer(const Pointer& pt, int x, int y, Player& p);
		void drawDisplay(const Player& p);
		// Show's 4 chips share a row; Style's (one an offered visualizer, VIS_SHOWN) go 5 to a row, as many rows as
		// it takes (three for fifteen), the chips a little lower than Show's
		static int displayChips(int row) { return row ? VIS_SHOWN_COUNT : SHOWS; }
		static constexpr int DISPLAY_BOX_Y = 24, DISPLAY_BOX_H = 412, STYLE_COLS = 5, STYLE_ROWS = (VIS_SHOWN_COUNT + 4) / 5,
			STYLE_Y = 166, STYLE_STEP = 56, CHIP_H = 50;
		Rect displayChip(int row, int i) const
		{
			if (!row) return {48 + i * (130 + 8), 84, 130, CHIP_H};
			int w = (544 - (STYLE_COLS - 1) * 8) / STYLE_COLS;
			return {48 + (i % STYLE_COLS) * (w + 8), STYLE_Y + (i / STYLE_COLS) * STYLE_STEP, w, CHIP_H};
		}
		void displaySet(int row, int value, Player& p);
		// Sound (music's): two tabs, Equalizer and Effects (L/R), opened from the song's Sound button, its line, or
		// Settings › Music (B goes back there, on its row); the tab last open kept for the run
		enum SoundTab { SOUND_EQ, SOUND_FX };
		int soundTab = SOUND_EQ;
		bool soundFromSettings = false, soundFromMenu = false;
		void openSound(Player& p, bool fromSettings);
		void closeSound(Player& p);
		bool soundKey(Button b, Player& p);
		bool soundPointer(const Pointer& pt, int x, int y, Player& p);
		void drawSound(const Player& p);
		void drawSoundHeader(const Player& p);
		void drawEqualizer(const Player& p);
		void drawEffects(const Player& p);
		Rect soundTabRect(int t) const { return {t == SOUND_EQ ? 72 : 248, 10, 168, 44}; }
		// the equalizer: the band the cursor (or a finger) is on; a finger on a column sets its gain, a double-tap 0 dB
		int eqBand = 0;
		bool eqTouch = false, eqMoved = false;
		int eqDownY = 0, eqTapBand = -1;
		Uint32 eqTapAt = 0;
		void eqSetBand(int band, double gain, Player& p);	// an edit: Custom from the first one
		void eqPick(const std::string& name, Player& p);	// a preset (CUSTOM_NAME: Custom back)
		void eqStep(int dir, Player& p);					// the preset before or after, round (Y/X)
		std::vector<std::string> eqCycle(const Player& p) const;	// the presets, then Custom while there is one
		bool eqCustom(const Player& p) const { return p.settings.eqPreset.empty(); }
		bool eqCanReset(const Player& p) const;
		bool eqOriginIsUser(const Player& p) const;
		void eqReset(Player& p);
		void eqSave(Player& p);	// A: over the user preset it came from, or a new one named on the keyboard
		void eqToggle(Player& p);
		// Custom put aside when another preset is picked (Y/X, the list): still in the cycle for the run
		bool stashOn = false;
		std::string stashOrigin;
		EqGains stashGains{};
		// its dialogs: Options (START), the preset list, Delete's question
		enum EqDialog { EQD_NONE, EQD_OPTIONS, EQD_LIST, EQD_DELETE };
		int eqDialog = EQD_NONE, eqDlgCursor = 0, eqListScroll = 0;
		bool eqListTouch = false;
		enum EqOption { EQO_LIST, EQO_SAVE_TO, EQO_SAVE_AS, EQO_RENAME, EQO_DELETE, EQO_RESET, EQO_CANCEL };
		std::vector<int> eqOptions(const Player& p) const;
		Rect eqOptBox(const Player& p) const { int h = 80 + (int)eqOptions(p).size() * 52; return {112, (436 - h) / 2, 416, h}; }
		Rect eqOptRow(int i, const Player& p) const { return {128, eqOptBox(p).y + 62 + i * 52, 384, 46}; }
		void eqChooseOption(int item, Player& p);
		// the preset list's entries: section headers (not chosen) and presets, each its height
		struct EqEntry { int kind; std::string name; };	// kind 0 header, 1 preset, 2 Custom
		std::vector<EqEntry> eqEntries(const Player& p) const;
		void eqListFit(const Player& p);	// the cursor's entry in view
		int eqListBottom(const std::vector<EqEntry>& e, int first) const;	// the last entry shown from first
		void eqListMove(int dir, const Player& p);
		void openEqList(const Player& p);
		// naming a preset on the keyboard: Save as (a new one) or Rename (eqRenameFrom); a name refused keeps it up
		bool eqRenaming = false, eqNameSubmit = false;
		std::string eqRenameFrom, eqNameNote;
		void eqNameDone(Player& p);
		void eqNameOpen(bool rename, Player& p);
		bool eqIsUser(const Player& p, const std::string& name) const;	// one of yours (not a built-in, not Custom)
		int fxCursor = 0;
		void fxChoose(int i, Player& p);
		// a photo: the pan area (its thumbnail, the part on the top panel boxed: a finger drags the photo as on a
		// phone, a flick glides, a double-tap zooms; no swipe to the next: Prev/Next do that) beside its buttons
		bool photoKey(Button b, Player& p);	// true: it was a photo's
		bool photoPointer(const Pointer& pt, int x, int y, Player& p);
		void photoZoomTo(double zoom, double panX, double panY, Player& p);
		void drawPhoto(const Player& p);
		Rect panRect(const Player& p) const { return {p.settings.panRight ? 288 : 16, 80, PAN_W, PAN_H}; }
		Rect photoBtn(int i, const Player& p) const
		{
			return {(p.settings.panRight ? 16 : 368) + i % 2 * 128, 80 + i / 2 * 64, 120, 56};
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
		void drawMain(const Player& p);
		void drawLeave(const Player& p);	// Close (Quit): B twice, or a tap twice
		void drawTracks(const Player& p);
		void drawPlRow(int i, int mid, bool at);	// a playlist's item: its number, mark, name and where it is
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
		std::string browseWhere() const;
		// a dialog over the page (the Options menu, the update, quick settings), and one of its rows
		void drawDialog(Rect box, const std::string& title);
		// note: a value on the row's right ("On", "Press twice")
		void drawDialogRow(Rect r, const Glyph* g, const std::string& label, bool at, bool pressed, SDL_Color ink,
			const std::string& note = "", SDL_Color noteInk = {0, 0, 0, 0});
		std::vector<std::string> wrapText(const std::string& text, int size, int maxW, int maxLines) const;
		// a button's face: a glyph and a label side by side, the hardware button's name under them (key: none)
		void drawButton(Rect b, const Glyph* g, const std::string& label, const char* key, bool off, int size, bool lit = false);	// lit: the glyph and label in yellow (on, as the heart once liked)
		static constexpr int STATUS_Y = 100, LINE1_Y = 378, LINE2_Y = 403;	// the player's status line, its two track lines
		// the Network tab with no connections: its page offers Add connection; Playlists with none, New playlist
		bool emptyAction() const;
		int firstCursor() const;	// where a folder opened afresh puts the cursor
		void doEmptyAction(Player& p);
		static constexpr int FOOT_Y = 458, SUB_Y = 70;	// the footer's line, the browser's "where" line (their middles)
		void drawSettings(const Player& p);
		void drawInfo(const Player& p);
		// the Media info page's lines: a row's value wrapped to the width, read again each second while it's open
		struct InfoLine { std::string label, text; bool header; };
		std::vector<InfoLine> infoLines;
		Uint32 infoAt = 0;
		void readInfo(const Player& p);
		void setInfoLines(const std::vector<Player::InfoRow>& rows);	// wrapped to the page
		void openAbout(const Player& p);
		static constexpr int INFO_Y = 70, INFO_H = 26, INFO_SHOWN = 14, INFO_VALUE_X = 190;
		void drawPreview(const SubStyle& st, Rect box);

		SDL_Texture* text(const std::string& s, int size, SDL_Color c);
		void drawText(const std::string& s, int size, SDL_Color c, int x, int y, int maxW = 0, int align = 0);
		// the same, the line's middle (its capitals') on cy: labels centred in their boxes at any panel size
		void drawTextMid(const std::string& s, int size, SDL_Color c, int x, int cy, int maxW = 0, int align = 0);
		void drawTextAt(const std::string& s, int size, SDL_Color c, int x, int y, int maxW, int align, bool middle);
		int capHeight[3] = {};	// in pixels
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
		TTF_Font* fonts[3] = {};	// small, normal, large
		// for text Pixelify Sans has no glyphs for, at the same sizes: Fusion Pixel (Chinese, Japanese, Korean,
		// Vietnamese, Greek), then Noto Sans Thai, Arabic, Hebrew, Devanagari
		std::vector<std::array<TTF_Font*, 3>> fallbacks;
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
		bool dragging = false;
		double dragFraction = 0, scrubbed = -1;
		Uint32 scrubAt = 0;
		// the bar's drag is relative (as a touch bar's slider): it moves the place from where it was by how far the
		// finger goes, wherever it landed; a tap that doesn't move goes to the tapped place
		int dragX0 = 0;
		double dragFrom = 0;
		bool dragMoved = false;
		// let go, the bar stays where the finger left it until playback gets there (the exact seek takes a moment, a buffering one longer;
		// and until then the position is still the old one: the bar flicked back and forth); -1 none
		double seekHeld = -1;
		Uint32 seekHeldAt = 0;
		BottomPanel panel;
		// Lock: over a video, touch does nothing but Screen off and a 1 s hold on the lock (its release); the
		// buttons go on as Button hotkeys says
		bool touchLocked = false;
		Uint32 lockHoldAt = 0, lockNoticeAt = 0;
		bool swallowTouch = false;	// the touch that woke the panel: it presses nothing
		int pressedBtn = -1;		// a touch button under the finger: it acts when the finger lifts on it
		bool listTouch = false, listScrolled = false;	// a finger on a track list: a tap chooses, a drag scrolls
		int touchStartY = 0, touchX = 0, scrollStart = 0;
		Uint32 clearArmed = 0;		// Clear play history chosen once: again within 2 s clears it

		std::string browseDir, openRequest;
		std::vector<Row> browseRows;
		std::vector<std::string> browsePaths;	// beside browseRows: what each row opens
		std::vector<BrowseRoot> roots;
		std::map<std::string, std::pair<int, int>> placeIn;	// each folder's cursor and scroll, for coming back
		int browseVersion = 0;

		static constexpr Rect BAR{24, 150, 592, 24};
		static constexpr int BTN_W = 108, BTN_H = 60, BTN_STEP = 121, ROW1_Y = 228, ROW2_Y = 300;
		static constexpr int ROW_Y = 84, ROW_H = 48, ROWS_SHOWN = 7;	// 7 rows end at 420, above the hints (and the mini bar)
		// the settings' rows by id: the subtitle style's, Music display, Visualizer, the switches (REMEMBER), Pan area,
		// Clear play history, ..., Sound (it opens the Sound page). Shown a group at a time (settingRows), after a list of
		// the groups. The manga reader's rows are its own page's (Page::MangaSettings): EXT_NOTE is its note, no value
		static constexpr int STYLE_ROW0 = 0, MUSIC_ROW = STYLE_KEYS, VIS_ROW = STYLE_KEYS + 1, TOGGLE_ROW = STYLE_KEYS + 2,
			TOGGLES = 20, PAN_ROW = TOGGLE_ROW + TOGGLES, DRAG_ROW = PAN_ROW + 1, CLEAR_ROW = DRAG_ROW + 1,
			CHECK_ROW = CLEAR_ROW + 1, INSTALL_ROW = CHECK_ROW + 1, TOP_LIGHT_ROW = INSTALL_ROW + 1,
			BOTTOM_LIGHT_ROW = TOP_LIGHT_ROW + 1, SOUND_ROW = BOTTOM_LIGHT_ROW + 1, CLOCK_ROW = SOUND_ROW + 1,
			SCREEN_OFF_ROW = CLOCK_ROW + 1, EXT_NOTE = SCREEN_OFF_ROW + 1, MANGA_MODE_ROW = EXT_NOTE + 1,
			MANGA_FIT_ROW = MANGA_MODE_ROW + 1, MANGA_HINGE_ROW = MANGA_FIT_ROW + 1, MANGA_PRELOAD_ROW = MANGA_HINGE_ROW + 1,
			MANGA_CACHE_ROW = MANGA_PRELOAD_ROW + 1, MANGA_CLEAR_ROW = MANGA_CACHE_ROW + 1,
			MANGA_JAVA_ROW = MANGA_CLEAR_ROW + 1, SCREEN_OFF_VIDEO_ROW = MANGA_JAVA_ROW + 1,
			// Secure DNS is one of the REMEMBER switches (the last), so its id is that switch's: changeSetting's
			// generic branch toggles it by that index
			MANGA_DNS_ROW = TOGGLE_ROW + 19;
		// the panels' backlights (BananaOS's own levels: changed here as its menu changes them)
		Backlight backlight;
		bool sameBrightness = true;	// the setting's, kept here for settingRows (no Player there)
		Volume volume;
		int volumePct = -1;	// as read when the quick settings opened, then as set there
		bool quickOpen = false, quickDrag = false;
		int quickCursor = 0, quickDragRow = -1;
		enum QuickRow { Q_VOLUME, Q_BRIGHT, Q_BOTTOM, Q_SAME, Q_WEB, Q_INCOGNITO, Q_QUIT };
		int quickSub = 0;	// Web dashboard's row: 0 its switch, 1 New PIN (LEFT/RIGHT)
		std::vector<int> quickRows() const;
		bool quickKey(Button b, Player& p);
		bool quickPointer(const Pointer& pt, int x, int y, Player& p);
		void quickAdjust(int row, int dir, Player& p);
		void quickSetAt(int row, int x, Player& p);	// a finger on the row's bar
		void quickChoose(int row, Player& p);
		void drawQuick(const Player& p);
		// rows 62 apart, closer (56) when that many don't fit above the footer
		// the rows' spacing: roomy while they fit above the hints, closer as rows come (seven at most: both panels'
		// brightness apart, the dashboard, incognito mode and quit)
		int quickStep() const
		{
			int n = (int)quickRows().size();
			return 74 + n * 62 <= 436 ? 62 : 74 + n * 56 <= 436 ? 56 : (436 - 74) / n;
		}
		Rect quickPanel() const { int h = 74 + (int)quickRows().size() * quickStep(); return {40, (436 - h) / 2, 560, h}; }
		Rect quickRowRect(int i) const { return {56, quickPanel().y + 56 + i * quickStep(), 528, std::min(54, quickStep() - 2)}; }
		Rect quickPinBtn(int i) const { Rect r = quickRowRect(i); return {r.x + r.w - 164, r.y + (r.h - 48) / 2, 160, 48}; }
		Rect quickMinus(int i) const { Rect r = quickRowRect(i); return {r.x + 236, r.y + (r.h - 48) / 2, 48, 48}; }
		Rect quickBar(int i) const { Rect r = quickRowRect(i); return {r.x + 296, r.y + r.h / 2 - 10, 164, 20}; }
		Rect quickPlus(int i) const { Rect r = quickRowRect(i); return {r.x + 472, r.y + (r.h - 48) / 2, 48, 48}; }
		void setBrightness(Backlight::Panel panel, int permille, Player& p);
		static constexpr int SET_Y = 92, SET_H = 46, SET_SHOWN = 7, HELP_Y = 400;
		// a settings group's tile (Settings opens on them: two to a row)
		Rect groupRect(int g) const { return {16 + g % 2 * 312, 70 + g / 2 * 86, 296, 78}; }
		std::string settingHelp(int row) const;	// what a setting does, under its rows
		// one settings row at y (at: the cursor's): its label, arrows and value, an action's or the note's own look.
		// Shared by Settings (drawSettings) and the manga reader's own settings page (drawMangaSettings)
		void drawSettingRow(int row, int y, bool at, const Player& p);
		static constexpr int64_t STYLE_ROW = -2;	// the subtitle menu's last row: the settings page
		static constexpr int QUIT_BTN = 10, SCREEN_BTN = 12, INFO_BTN = 13, LOCK_BTN = 14, EXIT_BTN = 15,
			BACK_ICON = 16,	// "<" at the top left of every page: B
			EMPTY_BTN = 17,	// an empty page's button (no connections: Add connection)
			GROUP_PREV = 18, GROUP_NEXT = 19;	// a settings group's neighbours, either side of its title (Y, X)
		// the web dashboard's page: New PIN and Turn off (its Turn on / Try again is EMPTY_BTN)
		static constexpr int WEB_NEWPIN = 540, WEB_OFF = 541;
		// the select mode's bar (and Move's), the busy dialog's Cancel, the Add to... picker's rows (PICK_ROW + i)
		static constexpr int SEL_ALL = 530, SEL_MOVE = 531, SEL_ACT = 532, SEL_CANCEL = 533, MOVE_CANCEL = 534, MOVE_DONE = 535,
			BUSY_CANCEL = 536, PICK_ROW = 900;
		// Main's header squares, the Playing tab's rows (PLAY_ROW + i)
		static constexpr int ADD_BTN = 503, LIKE_BTN = 504, PLAY_ROW = 550;
		static constexpr int PLAY_GRID = 2;	// the grid's Play/Pause (A's place)
		// Sound's touch buttons, apart from the grid's and the photo's; its dialogs' rows from DLG_ROW, the Display
		// popup's chips from CHIP_BTN (row * 16 + chip)
		static constexpr int SOUND_TAB0 = 510, SOUND_TAB1 = 511, EQ_SWITCH = 520, EQ_PREV = 521, EQ_NEXT = 522, EQ_NAME_ZONE = 523,
			EQ_RESET = 524, EQ_SAVE = 525, EQ_OPTIONS = 526, DLG_ROW = 700, CHIP_BTN = 600, FX_ROW = 800;
		void stepGroup(int dir);	// to the settings group before (-1) or after (1), round
		bool styleFromSubs = false;	// the subtitle style opened from the Subtitles list: B goes back to it
		void drawBackIcon();
		// the browser's rows' marks: what each row is
		enum RowIcon { ICON_NONE, ICON_RECENT, ICON_PLACE, ICON_FOLDER, ICON_VIDEO, ICON_SONG, ICON_PHOTO, ICON_NETWORK, ICON_REFRESH,
			ICON_TYPE, ICON_TEST, ICON_SAVE, ICON_PLAYLIST, ICON_LIKED, ICON_NEW };
		RowIcon rowIcon(int row) const;
		void drawRowIcon(RowIcon icon, int x, int y, SDL_Color c);
		// every page has it but Home (nowhere to go back to; B there asks to quit, START has Quit)
		bool backIconShown(const Player&) const { return !(page == Page::Browse && !inMenu && atHome()); }
		static constexpr Uint32 UNLOCK_HOLD_MS = 1000;	// buttonRect's ids beside the grid's 0..9
		static constexpr int SET_ARROW_X = 372;	// a settings row's left arrow: a tap left of the value's middle steps back

		// ---- the web dashboard ----
		// Page::Web: where it was opened from (B goes back there)
		struct WebBack { Page page = Page::Browse; bool inMenu = false; int menuTab = 0, setGroup = -1, cursor = 0, scroll = 0; } webBack;
		void openWeb();
		void closeWeb();
		bool webKey(Button b, Player& p);
		bool webPointer(const Pointer& pt, int x, int y, Player& p);
		void drawWeb(const Player& p);
		// its states as the page shows them
		enum WebView { WEB_OFF_VIEW, WEB_ON_VIEW, WEB_NOWIFI_VIEW, WEB_FAILED_VIEW };
		int webView(const Player& p) const;
		void webTurn(bool on, Player& p);	// the switch: kept in the settings, the server started or stopped
		void webNewPin(Player& p);			// New PIN: pressed twice (2 s apart at most)
		void webPrimary(Player& p);			// the empty page's button: Turn on, Try again
		int webClients() const;
		std::string webPin() const;
		std::string webAddress() const;	// "192.168.1.23:8080" ("" no Wi-Fi, or not running)
		std::string webIp;				// the device's address, read when the page opens and every 10 s while it shows
		Uint32 webIpAt = 0, webInputAt = 0, pinArmed = 0;
		void readWebIp();
		// the QR code of the page's URL, made again when the URL changes: its modules, a row at a time
		std::string qrText;
		std::vector<unsigned char> qrMods;
		int qrSize = 0;
		TTF_Font* pinFont = nullptr;	// Pixelify Sans at 56: the PIN's characters
		// the header's indicator (left of the battery), as last drawn: a tap opens the page
		Rect webInd{0, 0, 0, 0};
		bool webIndDown = false;
		void drawWebIndicator(int right, int cy, const Player& p);
		bool webIndicatorShown(const Player& p) const;
		std::string webSig(const Player& p) const;	// for signature()
		int levelMs = 0;
		bool stopRequest = false;

		// ---- extensions (ui_ext.cpp) ----
		// Home's Extensions drawer (browseDir EXTDIR), Manage extensions (Page::Extensions) and the manga reader's
		// page (Page::Manga): the extension that is on, its switch, and the first-time note about repositories over
		// Manage extensions
		bool extSheet = false;	// the note, over Manage extensions
		int extSheetCursor = 0, extCursor = 0, extPressed = -1;	// the sheet's two buttons, the drawer's tile, the touch's button
		void openExtensions();		// START in the drawer (and its button, with none on)
		void openManga(Player& p);	// the app: its store loaded and the card scanned (ui_manga.cpp)
		void openExtensionsDrawer(Player& p);	// B in either page: back to the drawer (the Browse page on it)
		void extToggleManga(Player& p);	// A on the row or the chip: on / off (the first time, the sheet)
		void extSheetChoose(int i, Player& p);	// Open Manga, Done
		bool extDrawerKey(Button b, Player& p);
		bool extKey(Button b, Player& p);
		bool extPointer(const Pointer& pt, int x, int y, Player& p);
		bool extAct(int id, Player& p);
		void openExtSettings();	// X or the gear: the manga reader's own settings page (Page::MangaSettings)
		void drawExtensionsDrawer(const Player& p);	// (the section header is already drawn)
		void drawExtensions(const Player& p);
		void drawManga(const Player& p);
		void drawExtSheet(const Player& p);
		// the touch ids: apart from the grid's, the buttons' and the pages'
		static constexpr int EXT_TILE = 560, EXT_MANAGE = 561, EXT_ROW = 562, EXT_CHIP = 563, EXT_GEAR = 564, EXT_OPEN = 565,
			EXT_DONE = 566;
		Rect extTileRect(int i) const { return {16 + i * 200, 88, 188, 150}; }	// (one to a row: several fit)
		Rect extManageRect() const { return {190, 304, 260, 48}; }
		Rect extRowRect() const { return {16, 88, 608, 70}; }
		Rect extChipRect() const { return {548, 104, 64, 36}; }
		Rect extGearRect() const { return {536, 104, 76, 36}; }
		Rect extSheetBox() const { return {60, 90, 520, 300}; }
		Rect extSheetBtn(int i) const { return {i ? 328 : 92, 310, 220, 50}; }
		static constexpr int EXT_DRAWER_TILES = 1;	// (what's on: the manga reader, so far)

		// ---- the manga reader (ui_manga.cpp) ----
		// Page::Manga's four tabs (Library, History, Browse, Downloads), each its own cursor and scroll;
		// Page::MangaSeries; and Page::MangaRead, the bottom panel while main's reader shows pages on top. Sources is a
		// page of its own (Page::MangaSources), opened with X on Browse. The store is <state>/manga (setStateDir keeps
		// the path), loaded and scanned the first time the app opens.
		enum MangaTab { MT_LIBRARY, MT_HISTORY, MT_BROWSE, MT_DOWNLOADS, MANGA_TABS };
		int mangaTab = MT_LIBRARY;
		std::unique_ptr<MangaStore> mangaStore;
		std::vector<LocalSeries> mangaLocal;	// On this handheld: each Manga folder's series, in reading order
		std::unordered_map<std::string, size_t> mangaLocalById;
		bool mangaScanned = false;	// the card was read at least once (the dashboard's local series can ask for it first)
		std::string stateDirPath;	// (setStateDir's) the manga store's own folder is under it
		bool mangaDirty = false;	// the store changed and wasn't written yet (mangaTick writes it once a second)
		Uint32 mangaSavedAt = 0;
		std::vector<std::string> mangaNotes;	// toasts made where no Player was at hand (mangaNotesTick shows them)
		// the Library and History lists made again when the store changes: the library, its unread counts, the
		// History's series rows
		mutable uint64_t mangaCacheSerial = 0;
		mutable int mangaCacheCount = -1;
		mutable bool mangaCacheValid = false;
		mutable std::vector<MangaSeries> mangaLib;
		// a cover's cache key: the URL, but the manga engine's as "javahost/<install token>/<path>" (its port changes at
		// every start, and its numbers start over at every fresh install: the token keeps covers of two installs apart)
		std::string mangaCoverKey(const std::string& url) const;
		// a cover as the store keeps it: the engine's by its key (its install known), any other by its URL
		std::string mangaCoverStored(const std::string& url) const;
		std::string mangaSeriesCoverUrl(const std::string& seriesId) const;	// a saved series' cover by URL ("" for a local one)
		mutable std::vector<int> mangaLibUnread;
		mutable std::vector<ChapterProgress> mangaHistory;	// one row per series (historyBySeries)
		int mlibGrid = 0, mlibScroll = 0;
		int mhisCursor = 0, mhisScroll = 0;
		int mbrowseCursor = 0, mListCursor = 0, mListScroll = 0;
		bool mBrowseList = false;	// Browse's "On this handheld": the series list open
		std::string mangaSeriesId;	// the series shown (Page::MangaSeries)
		int mangaSeriesFrom = 0;	// 0 Library, 1 Browse's list, 2 History (B goes back there)
		int mserCursor = -1, mserScroll = 0;	// -1 the Resume button, else a chapter
		Uint32 mangaClearAt = 0;	// History's Clear pressed once: again within 2 s clears it
	// the page cache (the setting's cap), opened when the app does: the network sources put their pages in it
		PageCache mangaPageCache;
		// the cover cache (50 MB fixed; docs/design/manga.md section 6): the cover images the reader fetched, keyed
		// by their URL, so a cover already downloaded isn't asked for again and the web dashboard can serve one
		PageCache mangaCoverCache;
		static constexpr uint64_t MANGA_COVER_CACHE_LIMIT = 50ull * 1024 * 1024;
		Uint32 mangaCacheAt = 0;	// Clear cache pressed once: again within 2 s empties it
		Uint32 mangaJavaAt = 0;		// Java extensions' Remove pressed once: again within 2 s removes it
		bool mangaHistOn = true;	// the setting, kept here for the reader's records (mangaRecordProgress has no Player)
		int mangaPressed = -1;		// the card, cover, row or button under the finger
		// Page::MangaRead: the chapter open, what was last recorded, and the finger on it
		std::string mangaReadSeriesId, mangaReadChapterId, mangaReadChapterTitle;
		bool mangaReadLive = false;	// main has had this request open (a close before that isn't main's)
		// a source's chapter picked from its series page: the reader page shows at once, "Loading chapter…" until its
		// page list comes (then the request goes to main), or why it didn't (A tries again, B goes back)
		bool mangaReadWaiting = false;
		// the one loading mark: a ring of dots turning, a line under it, in the middle of what's loading (a page's
		// list, a chapter). Drawn this frame: the loop comes round for its next step (animating, signature)
		mutable bool loaderShown = false;
		void drawLoader(int cx, int cy, const std::string& text);
		// the source series page's unread count and Resume point, and what they were made from (drawMangaSourceSeries)
		std::string mserSumKey;
		std::string mserFocusChapter;
		int mserBtn = 0;
		int mserDragY = -1, mserDragScroll = 0, mserDragTap = -1;	// a finger on the series page's chapter list
		bool mserDragMoved = false;	// the series page's button row: 0 Resume, 1 Download, 2 Refresh, 3 Library (LEFT/RIGHT)	// a chapter the series page puts its cursor on once its list shows (from History)
		int mserSumUnread = 0;
		MangaResumePoint mserSumResume;
		// the extensions starting (about 25 s on the handheld), on every page: a pill at the top, its seconds counting,
		// then "Extensions ready" a moment. Its text is in signature(), so it redraws as the seconds change
		mutable int extNoticeState = -1;
		Uint32 videoBlockedAt = 0;
		Uint32 engineStopAt = 0;
		// the engine is asked for once per entry into the Manga pages (mangaVideoGuard): in them now, and the wish
		// still to be made (it waits while a stop is ending or the engine is still downloading)
		bool engineInManga = false, engineEntryWant = false;
		Uint32 engineUseAt = 0;	// the last time a Manga page counted as the engine's use (every 30 s while one shows)
		// the manga extension's picture (assets/icons/manga.png), decoded at the size drawn
		SDL_Texture* mangaIconTex = nullptr;
		int mangaIconPx = 0;	// the size it was decoded at
		void drawMangaIcon(Rect r);
		// a short line in the notice at the top of the panel, gone by itself (the manga engine's other notices' place)
		std::string noticeText;
		Uint32 noticeUntil = 0;
		void mangaNotice(const std::string& text, Uint32 ms) { noticeText = text; noticeUntil = SDL_GetTicks() + ms; }
		// a manga source that stands behind a browser check (Cloudflare, DDoS-Guard): a dialog says so, OK closes it.
		// Opened once per error (mangaErrFor), over any page
		bool mangaErrOpen = false;
		// the error each asker last had a dialog for (0: a source's lists and series, 1: the reader's pages); forgotten
		// once that asker's error clears (the source asked again), so the same error after a reopen is said again
		std::string mangaErrFor[2], mangaErrText;
		Rect mangaErrBox() const { return {96, 120, 448, 220}; }
		Rect mangaErrOk() const { return {240, 280, 160, 44}; }
		void mangaErrCheck(const std::string& error, bool anyError = false, int asker = 0);	// anyError: not only a browser check
		void drawMangaErr();	// a video outside the reader asked the manga engine to stop (once)
		void mangaVideoGuard(Player& p);	// (ui_manga_java.cpp)	// a video stopped because the extensions run: the notice says so a moment
		mutable Uint32 extNoticeAt = 0;
		std::string extNoticeText() const;
		void drawExtNotice();
		void drawMangaReadWaiting();
		void mangaReadWaitCancel();
		int mangaProgPage = -1, mangaProgPages = -1;
		bool mangaTouch = false, mangaSlider = false;
		double mangaTouchY = 0, mangaTouchX = 0;
		// fullscreen reading: the pages' textures on this panel (a page in slices of at most 2048 rows), the chapter
		// they belong to, how many still wait to be made, when the page tag fades
		struct FullTex { int w = 0, h = 0; std::vector<SDL_Texture*> tiles; Uint32 used = 0; const void* src = nullptr; };	// src: the pixels it was made from
		std::map<int, FullTex> mfTex;
		uint64_t mfGen = 0;
		int mfWaiting = 0;
		Uint32 mfTagUntil = 0;
		int mfLastPage = -1;
		void drawMangaFull();
		void drawMangaControls();
		void drawMangaToast();
		void mangaFullscreen(bool on, Player& p);	// SELECT, and B while fullscreen
		void mfFree();
		int mangaModeFor(const std::string& seriesId, const Player& p) const;	// the series' own, else the setting's
		int mangaBookFor(const std::string& seriesId) const;	// the series' own Book (MANGA_BOOK_*; off by default)
		void mangaSetBook(int book, Player& p);	// on (held as book) or off, kept for the series
		bool mangaBookKey(Button b, Player& p);	// the reader's keys while in Book
		void mangaCycleMode(Player& p);	// X while reading
		int mangaSliderPage = -1;
		// the page slider's drag is relative, as the progress bar's: from the page it was on by the finger's travel
		// (the slider's width is the chapter); a tap that doesn't move goes to the tapped page
		int mgSlideX0 = 0;
		double mgSlideFrom = 0;
		bool mgSlideMoved = false;
		// fullscreen's controls over the pages: a tap shows them 4 s (a drag or the 4 s hides them), and the finger that
		// may still be a tap; the sheet's button under it (and its slider, which is the reading page's)
		Uint32 mangaCtrlUntil = 0;	// when they hide (0: hidden)
		bool mangaTap = false;
		int mangaTapX = 0, mangaTapY = 0;
		Uint32 mangaTapAt = 0;
		// the finger came down outside the bar while it showed: lifted as a tap, it puts the bar away and does
		// nothing else; moved, it drags the page and hides the bar as any drag does
		bool mangaTapDismiss = false;
		int mangaCtrlPressed = -1;
		// double-tap on the page area: zoom to 200 % at the tapped point / back to the fit, as A does (but at the
		// finger). the first tap is recorded; a second tap within 300 ms and 20 units sends a ZoomPoint command
		Uint32 mangaLastTapAt = 0;
		int mangaLastTapX = 0, mangaLastTapY = 0;
		// START while reading: the Reading menu, a compact sheet over the page (over the lower part of the fullscreen
		// view): its cursor, and its keys (UP/DOWN move, LEFT/RIGHT change, A toggles / opens the last row, B or
		// START close). Reading goes on behind it
		bool mangaMenu = false;
		int mangaMenuCursor = 0;
		// the zoom the keys last asked for: key repeats polled in one pass must walk the ladder, not resend a step
		int mangaZoomAsked = 100;
		static constexpr int MGM_ROWS = 8, MGM_ROW_H = 34, MGM_TOP = 164;	// at most 8 rows, above the help line (452)
		// the rows a menu may have, in order; mangaMenuRows() lists the ones that mean something now (the cursor is
		// one of these ids, so it stays on its row as others come and go)
		enum { MGM_VIEW, MGM_HOLD, MGM_MODE, MGM_FIT, MGM_ZOOM, MGM_HINGE, MGM_SWAP, MGM_PAGES, MGM_SETTINGS, MGM_COUNT };
		// the View row: the pages on one screen, on both as one tall view (fullscreen), or Book (held sideways)
		enum { MV_ONE, MV_FULL, MV_BOOK, MV_COUNT };
		int mangaViewAsked = -1, mangaHoldAsked = 0;	// what the menu just asked, till the reader's status says it
		int mangaHoldLast = MANGA_BOOK_LEFT;	// how Book is held when the View row turns it on
		int mangaViewNow() const;
		int mangaHoldNow() const;
		std::vector<int> mangaMenuRows() const;
		void mangaSetView(int v, Player& p);
		Rect mangaMenuRect() const { return {0, 132, W, H - 132}; }
		Rect mangaMenuRowRect(int i) const { return {16, MGM_TOP + i * MGM_ROW_H, 608, MGM_ROW_H - 4}; }
		void mangaOpenReadingMenu();
		bool mangaMenuKey(Button b, Player& p);
		void mangaMenuStep(int dir, Player& p);
		void mangaMenuAct(Player& p);
		void drawMangaReadingMenu(const Player& p);
		// the pages grid (Y in the reader): the chapter's thumbnails 6 wide, the D-pad moves the cursor, A jumps
		// and closes, B or Y closes; the top panel keeps showing the reader, moved to the page under the cursor
		// (a Seek as the slider sends). the thumbnails are made by the reader as each page decodes
		bool mangaPages = false;	// the grid is open
		int mangaPagesCursor = 0;	// the page under the cursor (0-based)
		int mangaPagesScroll = 0;	// the first row shown
		bool mangaPagesTouch = false;
		double mangaPagesTouchY = 0, mangaPagesTouchStartY = 0;	// the finger's last place the scroll took, and where it went down
		bool mangaPagesMoved = false;	// the finger moved far enough to be a drag: lifting it then doesn't open a page
		int mangaPagesPressed = -1;	// the thumbnail under the finger (-1: none)
		// the grid's own textures: one SDL_Texture per decoded page, made a few per frame so a turn never stalls
		struct ThumbTex { SDL_Texture* tex = nullptr; int w = 0, h = 0; Uint32 used = 0; };
		std::map<int, ThumbTex> mpTex;
		uint64_t mpGen = 0;
		int mpMade = 0;	// how many were made this frame (at most a few)
		void drawMangaPages(const Player& p);
		void mangaPagesOpen(Player& p);
		void mangaPagesClose(Player& p);
		bool mangaPagesKey(Button b, Player& p);
		bool mangaPagesPointer(const Pointer& pt, int x, int y, Player& p);
		void mpFree();
		void mangaZoomStep(int dir);	// UP/DOWN, and the Zoom row: one of the photo viewer's steps
		void mangaZoomToggle();			// A: 200 % or back to the fit
		void mangaSetMode(int m, Player& p);	// this series' reading mode (X and the menu)
		void mangaZoomTo(double z);
		// the first fullscreen ever: a toast in the panel's middle for 2 s (mangaFullSeen remembers it)
		Uint32 mangaToastUntil = 0;
		int mangaBookToast = 0;	// MANGA_BOOK_*: the toast says how to hold the handheld for Book (0: how to leave fullscreen)
		void mangaBookTurn(int step, Player& p);	// a spread on (+1, the next chapter at the end) or back (-1)
		void drawMangaBookTag();
	// the page numbers, turned as the pages are, at the foot of the spread
		static constexpr Uint32 MG_TAP_MS = 250, MG_CTRL_MS = 4000, MG_TOAST_MS = 2000;
		static constexpr int MG_TAP_UNITS = 12;	// a finger that moves less (640x480 units) may still be a tap
		static constexpr int MG_LIST_SLOP = 14;	// the app's lists: a finger that moves more is a drag (mangaListPointer)
		Rect mangaSheetRect() const { return {0, 300, W, H - 300}; }
		Rect mangaCtrlBtnRect(int i) const
		{
			// four to a row across the panel's 608: the long one ("Leave fullscreen") takes a little more; above the
			// mini bar while it shows in the sheet
			static const int X[4] = {16, 164, 312, 488}, WIDTH[4] = {140, 140, 168, 136};
			return {X[i], miniUp ? 374 : 392, WIDTH[i], 44};
		}
		void mangaOpen(Player& p);		// openManga: the store loaded, the card scanned, the tabs reset
		void mangaScan();				// each Manga folder read and its series put in the store
		void mangaCache() const;
		void mangaSave();				// the store written now (the reader's: once a second, and on closing)
		void mangaTick();				// the reader's page recorded, the store written when a second has passed
		void mangaRecordProgress();		// the reader's place into the store
		void mangaCloseReader();		// B: Close pushed, progress saved, the series page
		void openMangaSeries(const std::string& id, int from, Player& p);
		void mangaOpenChapter(const LocalSeries& s, int i, int page, Player& p);
		void mangaChapterStep(int dir, Player& p);	// the chapter before (-1) or after (+1), at page 0
		void mangaSeek(int page);		// the slider: a Seek command
		const LocalSeries* mangaLocalSeries(const std::string& id) const;
		std::string mangaTitle(const std::string& id) const;
		int mangaReadChapterIndex() const;
		void mangaChoose(int tab, int i, Player& p);	// A on what a tab's cursor is on
		void mangaSeriesChoose(int i, Player& p);		// A on a chapter
		void mangaSeriesResume(Player& p);				// A on Resume
		bool mangaKey(Button b, Player& p);
		bool mangaPointer(const Pointer& pt, int x, int y, Player& p);
		// a finger on one of the app's lists or grids (Library, History, Browse, Downloads, Sources, a server's, a
		// network folder's, a series' chapters): it goes down on a row (mangaListBegin); past a small slop it's a
		// drag that scrolls the list a row per row's height (scrollTo asks the list for a first row and answers the
		// one it took, the list keeping its cursor among its rows shown); lifted without one, on the spot it went
		// down, it's a tap on that row (tap). Ui::pointer sends it the moves and the lift while it's on
		struct MangaListDrag
		{
			bool on = false, moved = false;
			int downX = 0, downY = 0;	// where the finger went down (640x480)
			int fromY = 0, fromScroll = 0;	// the finger's place and the list's first row the scroll counts from
			int rowH = 1, item = -1;	// the list's row height; the row under the finger (-1: none, only a drag)
			std::function<int(int)> scrollTo;
			std::function<void(int)> tap;
		};
		MangaListDrag mlist;
		void mangaListBegin(int x, int y, int scroll, int rowH, int item, std::function<int(int)> scrollTo,
			std::function<void(int)> tap);
		bool mangaListPointer(const Pointer& pt, int x, int y);
		void drawMangaTabs();
		void drawMangaLibrary(const Player& p);
		void drawMangaHistory(const Player& p);
		void drawMangaBrowse(const Player& p);
		void drawMangaSeries(const Player& p);
		void drawMangaRead(const Player& p);
		void drawMangaCover(Rect r, const std::string& title, bool focus, int bookCell, const std::string& seriesId,
			const std::string& coverUrl = "",
			const std::vector<std::pair<std::string, std::string>>* coverHeaders = nullptr);
		// the series' real cover once it arrived, null until then (and for one that couldn't be read): the first draw
		// asks the worker once. Keyed by the series id
		SDL_Texture* coverOf(const std::string& seriesId);
		// a cover by its URL (a source's or a server's): the bytes in the cover cache, decoded by the worker, turned
		// into a texture on the main thread. Fetched only when the card is on screen; cancelled when it scrolls away.
		// A cover already in the cache is used with no fetch. headers: the source's image headers, or the server's
		// auth header via remoteAuth. coverKey is the texture-cache key (the URL; unique per image)
		// while a server's grid draws: its covers' addresses as a request needs them (Kavita's {key} filled in); the
		// cover's own address, its cache key, keeps the placeholder. Empty the rest of the time
		std::function<std::string(const std::string&)> coverFetchUrl;
		SDL_Texture* coverOfUrl(const std::string& coverUrl, int w, int h,
			const std::vector<std::pair<std::string, std::string>>& headers);
		struct CoverTex { SDL_Texture* tex = nullptr; uint64_t drawnAt = 0; };
		std::unordered_map<std::string, CoverTex> coverTex;	// at most COVER_KEEP, least recently drawn freed
		std::set<std::string> coverAsked, coverFailed;		// (a failed one is left alone until the app reopens)
		uint64_t coverDrawn = 0, coverSerial = 0;	// the LRU's order and what signature() follows
		static constexpr size_t COVER_KEEP = 80;
		// the cover fetches in flight: URL → fetch serial. A card scrolling away cancels its fetch; an answer that
		// arrives is put in the cover cache and handed to the worker for decode
		std::unordered_map<std::string, uint64_t> mangaCoverFetches;
		// the fetches the worker is decoding (asked but no result yet): so a draw doesn't ask twice
		std::set<std::string> coverUrlAsked;
		// cover URLs on screen this frame: anything in mangaCoverFetches but not here is cancelled at the end of the
		// draw (a card scrolled away)
		std::set<std::string> mangaCoverOnScreen;
		void mangaCoverCancelOffScreen();	// cancel fetches for URLs that scrolled away
		void mangaCoverCancelAll();			// the page left: every in-flight cover fetch goes
		bool mangaCoverOnFetched(MangaFetch::Result& r);	// true if the result was one of ours (a cover fetch)
		// four tabs evenly across the room under the header's back icon: 24 each side, 8 between
		Rect mangaTabRect(int i) const { return {24 + i * 150, 60, 142, 30}; }
		static constexpr int MG_COLS = 5, MG_COVER_W = 112, MG_COVER_H = 150;	// the grid's covers 124 apart
		// the grid starts right under the tabs (their row ends at 90), its label on the line above it: with the
		// Continue reading strip gone, a row more of covers shows
		static constexpr int MG_LIB_TOP = 118, MG_LIB_BOTTOM = 416;
		Rect mangaCoverRect(int i) const
		{
			int row = i / MG_COLS, col = i % MG_COLS;
			return {16 + col * 124, MG_LIB_TOP + (row - mlibScroll) * 186, MG_COVER_W, MG_COVER_H};
		}
		Rect mangaResumeRect() const { return {16, 84, 220, 52}; }
		Rect mangaHeartRect() const { return {556, 84, 68, 52}; }
		// the reading page's own pieces sit above the mini bar when it shows (it takes the panel's bottom): the page
		// area, the slider and the chapter row all move up, the hints keep to the line under the bar
		// Try again, in the middle of the page area (or of the panel in fullscreen), while a page in view failed
		Rect mangaRetryRect(bool full) const { return full ? Rect{200, 216, 240, 48} : Rect{200, 176, 240, 48}; }
		void mangaRetry() { readerCmds.push_back({ReaderCmd::Retry, 0, (int64_t)SDL_GetTicks()}); }
		void drawMangaRetry(bool full);
		Rect mangaAreaRect() const { return {16, 70, 608, miniUp ? 222 : 260}; }	// (222: the page line below keeps 8 clear of it)
		Rect mangaSliderRect() const { return {40, miniUp ? 330 : 352, 560, 22}; }
		// the chapter row: three buttons of one size, equal margins and gaps across the panel's 608 (16, 208 wide
		// pieces, 16 apart: the last one ends at 624, 16 from the right edge)
		Rect mangaPrevRect() const { return {16, miniUp ? 372 : 404, 192, 40}; }
		Rect mangaFullRect() const { return {224, miniUp ? 372 : 404, 192, 40}; }	// the Fullscreen button: what SELECT does
		Rect mangaNextRect() const { return {432, miniUp ? 372 : 404, 192, 40}; }
		static constexpr int MG_SER_ROWS = 5, MG_SER_TOP = 140;	// Page::MangaSeries' chapter rows
		static constexpr int MG_ROW_TOP = 102, MG_ROWS = 6;
		// History's rows hold two lines (series, then chapter and page): taller, so fewer
		static constexpr int MG_HIST_ROW_H = 58, MG_HIST_ROWS = 5;	// the tabs end at 86: History's and Browse's rows below them
		// the touch ids: their own space (mangaPointer acts on them; act() never sees them)
		static constexpr int MG_RESUME = 3080, MG_HEART = 3081, MG_PREV = 3100, MG_NEXT = 3101, MG_FULL = 3102;
		// the pages grid: 6 columns of 96 px wide thumbnails, 6 px gap, centred in the 640-wide panel;
		// each row is 130 px (thumb + page number), the grid starts below the header and ends above the hints
		static constexpr int MPG_COLS = 6, MPG_THUMB_W = 96, MPG_GAP = 6, MPG_ROW_H = 130;
		static constexpr int MPG_LEFT = (W - (MPG_COLS * MPG_THUMB_W + (MPG_COLS - 1) * MPG_GAP)) / 2;
		static constexpr int MPG_TOP = 72;
		Rect mangaPageRect(int i) const	// i: 0-based page index; the rect in the 640x480 panel
		{
			int row = i / MPG_COLS, col = i % MPG_COLS;
			return {MPG_LEFT + col * (MPG_THUMB_W + MPG_GAP), MPG_TOP + (row - mangaPagesScroll) * MPG_ROW_H,
				MPG_THUMB_W, MPG_ROW_H - MPG_GAP};
		}
		static constexpr int MPG_THUMB = 4000;	// touch id base for the thumbnails (mangaPointer)

		// ---- the manga reader's Sources page (ui_manga_sources.cpp) ----
		// Installed / Available / Repositories, three sub-lists of a page of its own (X on Browse): LEFT/RIGHT step
		// them, UP/DOWN the list. The repositories are the user's own URL indexes (design 5.3); MangaNet owns them and
		// the events their fetches leave (mangaSourcesTick turns them into toasts). None of it runs while the page is
		// closed.
		enum MangaSourcesTab { MST_INSTALLED, MST_AVAILABLE, MST_REPOSITORIES, MANGA_SOURCES_TABS };
		// (Installed is no tab of its own now: Available lists the installed on top; the segments are Available and
		// Repositories, L1/R1 between them)
		int mstTab = MST_AVAILABLE;
		void drawMangaLuaInstalledRow(const MangaRepos::InstalledSource& s, Rect r, bool at);
		int mstCursor[MANGA_SOURCES_TABS] = {}, mstScroll[MANGA_SOURCES_TABS] = {};
		std::set<std::string> mstInstalling;		// full ids on their way (Installing… on the row), and Updating…
		std::map<std::string, std::string> mstInstallName;	// full id -> its name, for the answer's toast
		std::map<std::string, std::string> mstRepoError;		// repository URL -> its last failure
		std::set<std::string> mstAdding;			// typed in: its first index loaded says Added
		std::string mstPendingUrl;					// typed on the keyboard; mangaSourcesTick adds it
		Uint32 mstInstallAt = 0;					// when the first download started (a lost answer doesn't stick)
		// a row's sheet (A: details, X: options), a repository's (A: Refresh / Remove) and Remove's question
		enum MangaSourceModal { MSM_NONE, MSM_DETAILS, MSM_OPTIONS, MSM_REPO, MSM_REMOVE };
		int mstModal = MSM_NONE, mstModalCursor = 0;
		std::string mstModalId;						// a source's full id, or a repository's URL
		bool mstUninstallArmed = false;				// Uninstall pressed once: again within 2 s removes it
		Uint32 mstUninstallAt = 0;
		uint64_t mstLocal = 0;						// the page's own changes (an event taken, a row acted on)
		// ---- Java extensions in Sources and Browse (ui_manga_java.cpp) ----
		// A Java repository's extensions are listed after the Lua ones with a Java badge; a Java source is a Browse
		// row "java:<source id>" and its series "src:java:<source id>:<manga id>". Its requests go to the host's
		// GraphQL channel, never to the Lua worker.
		struct MangaSrcRow;	// (defined below, with the other source rows)
		struct MangaDlAsk;	// (defined below, with the Download sheet)
		uint64_t mstJavaLocal = 0;					// the Java rows' own changes, for signature()
		std::map<std::string, std::string> mstJavaBusy;	// package -> Installing… / Updating… / Removing…
		std::map<std::string, Uint32> mstJavaBusyAt;	// when each started (a lost answer doesn't stick)
		// a Java extension's sheet (A/X on an installed row) or a Java repository's (A on its row)
		enum MangaJavaModal { MJM_NONE, MJM_DETAILS, MJM_REPO };
		int mstJavaModal = MJM_NONE, mstJavaModalCursor = 0;
		std::string mstJavaModalId;					// a package name, or a repository URL
		bool mstJavaUninstallArmed = false;
		Uint32 mstJavaUninstallAt = 0;
		uint64_t mangaJavaListSerial = 0, mangaJavaSeriesSerial = 0, mangaJavaPagesSerial = 0;
		std::string mangaJavaBase;					// the host URL a request was built on (its port can change)
		Uint32 mangaJavaExtAt = 0;					// the last automatic fetchExtensions (not every tick)
		Uint32 mangaJavaDlAt = 0;					// the last scan of the download queue for Java work
		Uint32 mangaJavaKeepAt = 0;					// the last want() that kept an open Java page alive
		uint64_t mangaJavaManagerSerial() const;	// javaSources().serial(): the rows follow it (a source installed)
		bool mangaJavaInstalled() const;				// the add-on is on the card (Installed / Starting / Running)
		void mangaJavaNeedHost();					// want(), when the add-on is there
		std::string mangaJavaStatusLine() const;		// "Starting extensions… about 25 s", or a failure
		void mangaJavaAddRepo(const std::string& url, Player& p);
		void mangaJavaRefresh();						// fetchExtensions (Y, and the first Contacts with a store)
		void mangaJavaPoll(const Player& p);				// the manager's events and the host's answers
		void mangaNotesTick(Player& p);				// the queued toasts, one at a time
		int mangaJavaInstalledRows(const Player& p) const;
		int mangaJavaAvailableRows(const Player& p) const;
		int mangaJavaRepoRows() const;
		const MangaJava::Source* mangaJavaSourceFor(const std::string& fullId) const;
		void mangaJavaInstalledAct(int local, Player& p);	// A/X on an installed extension
		void mangaJavaAvailableAct(int local, Player& p);	// A: install
		void mangaJavaRepoAct(int local, Player& p);		// A: the repository's sheet
		void drawMangaJavaRow(int tab, int local, const Rect& r, bool at, bool focus, const Player& p);
		bool mangaJavaModalKey(Button b, Player& p);
		void mangaJavaModalAct(Player& p);
		void mangaJavaModalClose();
		bool mangaJavaModalPointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaJavaModal(const Player& p);
		int mangaJavaModalRows() const;
		Rect mangaJavaModalBox() const;
		Rect mangaJavaModalRowRect(int i) const;
		// a Java source's page (ui_manga_browse.cpp's hooks)
		void mangaJavaSourceOpened(const MangaSrcRow& row, Player& p);
		void mangaJavaListLoad(int page);
		void mangaJavaSeriesLoad(Player& p, bool refresh = false);
		void mangaJavaPagesAsk(Player& p);
		void mangaJavaFiltersAsk();
		std::string mangaJavaFiltersJson() const;
		bool mangaJavaFilterAnswer(const JavaExtHost::GraphQlResult& r);
		bool mangaJavaListAnswer(const JavaExtHost::GraphQlResult& r);
		bool mangaJavaSeriesAnswer(const JavaExtHost::GraphQlResult& r);
		// A saved extension series whose id the engine doesn't know, or now gives to another series (the engine was
		// installed afresh: its ids start over): found again by its title at its source, once per opening, and its
		// heart, History and chapters' places moved to the new ids
		std::string mangaJavaRelinkFrom, mangaJavaRelinkTitle, mangaJavaLoadedFor;
		uint64_t mangaJavaRelinkSerial = 0;
		bool mangaJavaRelinkTried = false;
		bool mangaJavaRelinkStart(const std::string& why);
		bool mangaJavaRelinkAnswer(const JavaExtHost::GraphQlResult& r);
		void mangaJavaRelinkChapters();
		// the check of a series opened from the card's copy (title, source, chapter ids, from the engine's database)
		bool mangaJavaChecking = false;
		bool mangaJavaCheckPending = false;	// the open's check put off until a chapter is wanted (mangaSourceEngineWant)
		void mangaSourceEngineWant();
		uint64_t mangaJavaCheckSerial = 0;
		void mangaJavaCheckStart();
		bool mangaJavaCheckAnswer(const JavaExtHost::GraphQlResult& r);
		std::string mangaSourceWaitText() const;	// why an extension's chapter waits: the engine, or the check
		bool mangaJavaPagesAnswer(const JavaExtHost::GraphQlResult& r, const Player& p);
		uint64_t mangaJavaDlSubmit(const MangaDlAsk& a);	// a chapter's page list for the Download sheet
		bool mangaJavaDlAnswer(const JavaExtHost::GraphQlResult& r);
		void mangaJavaCancel();						// the source's Java requests dropped (its page or series left)
		void mangaJavaCancelList();					// just the list in flight
		uint64_t mangaReposSerial() const;			// repos().serial(): signature() follows it (ui.cpp has no MangaNet)
		uint64_t mangaJavaSerial() const;			// javaExt().serial(): the Java row's state and progress
		std::string mangaJavaValue() const;			// the Java row's right side (the host's state)
		void mangaJavaAct();						// A: Install, Cancel, Remove (twice), Try again
		void mangaSourcesOpen(Player& p);			// the page opened: the network side started, the indexes refreshed
		void openMangaSources(Player& p);			// X on Browse: the Sources page
		bool mangaSourcesPageKey(Button b, Player& p);	// its keys, B back to Browse
		void mangaSourcesTick(Player& p);			// the manager's events into toasts, a typed URL added
		bool mangaSourcesKey(Button b, Player& p);
		bool mangaSourcesPointer(const Pointer& pt, int x, int y, Player& p);
		bool mangaSourcesModal() const { return mstModal != MSM_NONE || mstJavaModal != MJM_NONE || mstAvMenu != 0; }
		// Available's X: a menu (Refresh, Languages) and the language list (a tick on each kept; Settings::mangaLangs)
		int mstAvMenu = 0, mstAvCursor = 0, mstAvScroll = 0;
		bool mstInstalledOnly = false;
		// the extensions' order Available was opened with (an install doesn't move its row until Available is opened
		// again: entering Sources, L1/R1 onto it, the Filter menu)
		std::unordered_map<std::string, int> mstAvOrder;
		void mangaAvFreeze(const Player& p);	// Filter › Installed only: Available lists only what is installed
		void drawAdultChip(int x, int cy);	// "18+" after a source's version	// 0 none, 1 the menu, 2 the languages
		std::vector<std::string> mstAvLangs;	// the languages the repositories offer, when the list was opened
		static constexpr int MST_AV_SHOWN = 6;
		bool mangaLangShown(const Player& p, const std::string& lang) const;
		std::string mangaLangsSummary(const Player& p) const;
		void mangaAvMenuOpen(Player& p);
		bool mangaAvMenuKey(Button b, Player& p);
		void mangaAvMenuAct(Player& p);
		bool mangaAvMenuPointer(int x, int y, Player& p);
		void drawMangaAvMenu(const Player& p);
		Rect mangaAvBox() const;
		Rect mangaAvRow(int i) const;
		bool mangaSourcesModalKey(Button b, Player& p);
		void mangaSourcesModalAct(Player& p);
		void mangaSourcesCloseModal();
		void mangaSourcesAct(Player& p);				// A (or a tap) on the tab's cursor's row
		void mangaSourcesInstall(const std::string& fullId, const std::string& repo, const std::string& id,
			const std::string& name, Player& p);
		void mangaSourcesUpdate(const std::string& fullId, const std::string& repo, const std::string& id,
			const std::string& name, Player& p);
		void mangaSourcesUninstall(const std::string& fullId, const std::string& name, Player& p);
		void mangaSourcesAddOpen(Player& p);			// Add repository…: the keyboard on https://
		void mangaSourcesUrlTyped(const std::string& url);	// the keyboard's answer (ui_forms.cpp's kbClose)
		std::vector<MangaRepos::InstalledSource> mangaSourcesInstalled() const;
		std::vector<MangaRepos::AvailableSource> mangaSourcesAvailable(const Player& p) const;
		std::vector<MangaRepos::RepoInfo> mangaSourcesRepos() const;
		int mangaSourcesRows(const Player& p) const;	// the tab's rows (Repositories: the Add row last)
		int mangaSourcesModalRows() const;
		std::string elideMiddle(const std::string& s, int size, int maxW) const;	// a URL: "...repository/index.json"
		void drawMangaSources(const Player& p);
		void drawMangaSourcesPage(const Player& p);	// its header ("Sources"), then the page
		void drawMangaSourceModal(const Player& p);
		static constexpr int MS_ROW_TOP = 102, MS_ROWS = 4, MS_ROW_H = 58;
		Rect mangaSourcesSegRect(int i) const { return {16 + i * 306, 68, 302, 26}; }	// i: 0 Available, 1 Repositories	// under the header (a page of its own)
		Rect mangaSourcesRowRect(int i) const { return {16, MS_ROW_TOP + i * MS_ROW_H, 608, MS_ROW_H - 4}; }
		Rect mangaSourcesModalBox() const
		{
			int rows = mangaSourcesModalRows();
			int h = mstModal == MSM_DETAILS ? 150 + rows * 50 : mstModal == MSM_REMOVE ? 130 + rows * 52 : 110 + rows * 52;
			return {112, (436 - h) / 2, 416, h};
		}
		Rect mangaSourcesModalRowRect(int i) const
		{
			Rect b = mangaSourcesModalBox();
			int top = mstModal == MSM_DETAILS ? b.y + 132 : mstModal == MSM_REMOVE ? b.y + 110 : b.y + 90;
			int step = mstModal == MSM_DETAILS ? 50 : 52;
			return {b.x + 16, top + i * step, b.w - 32, step - 6};
		}
		// ---- browsing a source (ui_manga_browse.cpp) ----
		// Browse lists the saved servers and the installed sources after On this handheld; A opens one, with
		// Popular · Latest · Search (L/R) and a cover grid. A source's page is a view of Page::Manga and its series
		// page is Page::MangaSeries (the id says whose: "src:<full source id>:<key>"), because a Page of their own
		// would need its keys, taps and drawing dispatched in ui.cpp, ui_draw.cpp and ui_ext.cpp.
		struct MangaSrcRow { std::string id, name, lang, file; bool adult = false, java = false; };
		std::vector<MangaSrcRow> mangaSrcRows;	// every installed source, as repos() last listed them
		uint64_t mangaSrcRowsSerial = 0;		// repos()'s serial when they were read
		uint64_t mangaJavaRowsSerial = 0;		// javaSources()'s serial when the Java rows were read
		bool mangaSrcRowsOn = false;			// they were read at least once (a source list can be empty)
		int mangaView = 0;						// 0 the app's tabs, 1 a source's page, 2+ a saved server's pages
		std::string mangaSrcId, mangaSrcName, mangaSrcFile;	// the source open: full id, name, its script
		std::string mangaSrcQuery;
		// the source's own headers (its Referer and the like) for cover image fetches: filled from the worker's
		// answer for the first list, the same for every cover this source page asks for
		std::vector<std::pair<std::string, std::string>> mangaSrcCoverHeaders;
		// a source's grid's tabs, in the order they show (Latest only while the source has latest())
		static constexpr int SRC_POPULAR = 0, SRC_LATEST = 1, SRC_SEARCH = 2;
		int mangaSrcTab = 0;					// the source's tabs, in their order: Popular, Latest, Search
		bool mangaSrcHasLatest = true;			// a source without latest() loses its tab once the first ask says so
		std::vector<MangaItem> mangaSrcItems;
		// each tab's grid as it was left (source and tab; Search with its query): switching back shows it again, no
		// ask. A few sources' worth, then forgotten
		struct SrcTabState { std::vector<MangaItem> items; int page = 0, cursor = 0, scroll = 0; bool more = false; std::string query, filters; };
		void mangaSrcTabSave();
		bool mangaSrcTabRestore(int tab);
		std::string mangaSrcFiltersNow() const;
		std::map<std::string, SrcTabState> mangaSrcTabCache;
		int mangaSrcCursor = 0, mangaSrcScroll = 0, mangaSrcPage = 0;
		// a finger on the grid: where it went down, the scroll then, whether it has moved (a drag, not a tap)
		int mangaSrcDragY = -1, mangaSrcDragScroll = 0, mangaSrcDragTap = -1;
		bool mangaSrcDragMoved = false;
		void mangaSourceMaybeMore();
		// a series page's chapter rows shown: five down to the hint bar, four while the mini bar takes the foot
		bool mangaSourceEngineReady() const;	// an extension's series: the manga engine answering (Resume waits for it)
		// History's X: Clear last hour / today / last 24 hours / all (the places to resume stay)
		bool mhisMenu = false;
		int mhisMenuCursor = 0;
		static constexpr int MHIS_MENU_ROWS = 5;	// Selected item, Last hour, Today, Last 24 hours, All
		Rect mhisMenuBox() const { return {112, 80, 416, 70 + MHIS_MENU_ROWS * 44}; }
		Rect mhisMenuRow(int i) const { return {128, 136 + i * 44, 384, 40}; }
		bool mangaHisMenuKey(Button b, Player& p);
		void mangaHisMenuAct(Player& p);
		void drawMangaHisMenu();
		std::string mangaSourceDownloadsId(const std::string& sourceSeriesId) const;
		std::string mangaSourceLabel(const std::string& seriesId) const;	// its source by name ("" unknown)	// "local:<folder>" or ""
		bool mangaSourceSeriesInLib() const;	// the series or its downloads folder in the Library
		int mserRows() const { return miniUp ? 4 : 5; }
		// the grid's foot: down to the hint bar, or above the mini bar while it shows (a touch there is the bar's)
		int mangaSrcClipBottom() const { return miniUp ? 418 : 448; }	// near the grid's end (cursor or scroll): its next page asked for
		Rect mangaSrcStatusRect() const { return {436, 64, 188, 30}; }	// right of the tabs: Loading…, an error (tap: Try again)
		bool mangaSrcMore = false, mangaSrcLoading = false;
		std::string mangaSrcError;				// the source's one-line message, while it shows
		int mangaSrcFailed = 0;					// what A asks again after an error: 0 none, 1 a list, 2 details, 3 a chapter
		// the jobs in flight: each answer is dropped unless its serial is still the one here (the page or tab changed)
		uint64_t mangaSrcListSerial = 0, mangaSrcDetailsSerial = 0, mangaSrcChaptersSerial = 0, mangaSrcPagesSerial = 0,
			mangaSrcImageSerial = 0;
		std::vector<MangaPage> mangaSrcPages;	// a chapter's pages while its {key} ones are resolved by image()
		int mangaSrcImageAt = 0;
		std::vector<std::string> mangaSrcUrls;
		std::vector<std::pair<std::string, std::string>> mangaSrcHeaders;
		// the chapter a Pages job is for; the read request is filled from these once its pages are known
		int mangaSrcPendAt = -1;
		std::vector<MangaChapter> mangaSrcPendChapters;
		std::string mangaSrcPendSeriesId, mangaSrcPendSeriesTitle;
		int mangaSrcPendPage = 0, mangaSrcPendMode = 0, mangaSrcPendFit = 0, mangaSrcPendHinge = 36;
		bool mangaSrcPendFull = false;
		// the source series shown (Page::MangaSeries): the item as the source gave it, its details and its chapters
		std::string mangaSrcKey, mangaSrcItemJson, mangaSrcCover, mangaSrcTitle;
		MangaDetails mangaSrcInfo;
		bool mangaSrcInfoOn = false;
		std::vector<MangaChapter> mangaSrcChapters;		// oldest first, as the card's series page lists them
		bool mangaSrcChaptersOn = false;
		// the page's cached list (design 6): opened from <state>/manga/series/<hash>.json, so its details and chapters
		// show at once and only Refresh asks the source. fetchedAt: when the list on screen was fetched (0: no cache).
		// hadList: the fetch began with a list on screen, so a failure keeps it and says so in a toast instead.
		int64_t mangaSrcFetchedAt = 0;
		bool mangaSrcRefreshing = false, mangaSrcHadList = false;
		bool mangaSrcFetchDetailsOk = false, mangaSrcFetchChaptersOk = false;	// this fetch's two halves
		std::vector<MangaChapter> mangaReadSrcChapters;	// the reading chapter's series, for the chapter buttons
		// a source's ids: the series' and a chapter's are "src:<full source id>:<key>" (docs/design/manga.md 6)
		static bool isSourceId(const std::string& id) { return id.rfind("src:", 0) == 0; }
		static std::string sourceItemId(const std::string& sourceId, const std::string& key)
		{
			return "src:" + sourceId + ":" + key;
		}
		// saved servers' ids are "srv:<server id>:<key>" as sources' are "src:" (a server's key is a Komga id, an
		// OPDS feed url or a chapter's url): they never collide with a local series or an installed source's
		static bool isServerId(const std::string& id) { return id.rfind("srv:", 0) == 0; }
		static std::string serverItemId(const std::string& serverId, const std::string& key)
		{
			return "srv:" + serverId + ":" + key;
		}
		static void splitServerId(const std::string& id, std::string& server, std::string& key)
		{
			server.clear();
			key.clear();
			if (!isServerId(id)) return;
			size_t colon = id.find(':', 4);
			if (colon == std::string::npos) { server = id.substr(4); return; }
			server = id.substr(4, colon - 4);
			key = id.substr(colon + 1);
		}
		// the two parts of "src:<source>:<key>"; full is "" when the id is malformed. A Java source's full id has
		// a colon of its own ("java:<source id>"), so its series split after it, not at the first colon.
		static void splitSourceId(const std::string& id, std::string& full, std::string& key)
		{
			full.clear();
			key.clear();
			if (!isSourceId(id)) return;
			size_t colon = id.compare(4, 5, "java:") == 0 ? id.find(':', 9) : id.find(':', 4);
			if (colon == std::string::npos) { full = id.substr(4); return; }
			full = id.substr(4, colon - 4);
			key = id.substr(colon + 1);
		}
		int mbrowseScroll = 0;							// Browse's rows (On this handheld and the sources)
		bool mangaFullSeenPending = false;				// the first fullscreen's flag waits for a key (no Player in a draw)
		void mangaBrowseReset(Player& p);				// the app opened or left: the source's jobs dropped
		void mangaBrowsePoll(const Player& p);			// the worker's answers (the pages' draws and keys call it)
		void mangaBrowseRepos(const Player& p);			// the installed sources, read again when repos() changed
		int mangaBrowseCount(const Player& p) const;	// On this handheld, the servers, the sources
		const MangaSrcRow* mangaBrowseSourceAt(int i, const Player& p) const;	// i: the sources the setting shows
		int mangaBrowseServerCount() const;				// the saved servers, as Browse lists them
		int mangaBrowseSourceStart() const { return mangaNetFoldersCount() + mangaBrowseServerCount() + 2; }	// past the network folders, servers and Manage sources
		const MangaSrcRow* mangaSrcRowFor(const std::string& fullId) const;
		bool mangaBrowseKey(Button b, Player& p);
		bool mangaBrowsePointer(const Pointer& pt, int x, int y, Player& p);
		void mangaBrowseChoose(int i, Player& p);
		void drawMangaBrowseRows(const Player& p);
		void mangaOpenSource(const MangaSrcRow& row, Player& p);
		void mangaSourceBack(Player& p);
		bool mangaSourceKey(Button b, Player& p);
		bool mangaSourcePointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaSource(const Player& p);
		void mangaSourceTabStep(int dir, Player& p);
		void mangaSourceTabSet(int tab, Player& p);		// (the Search tab asks for the query)
		void mangaSourceLoad(int page);					// the tab's list: Popular / Latest / Search page 1, 2, …
		void mangaSourceSearch(const std::string& query);	// the keyboard's Done
		void mangaSourceRetry(Player& p);				// A after the source's error
		void mangaSourceOpenSeriesAt(int i, Player& p);
		void mangaOpenSourceSeries(const std::string& id, int from, const std::string& itemJson,
			const std::string& cover, const std::string& title, Player& p);
		bool mangaSourceSeriesOn() const;
		bool mangaSourceSeriesKey(Button b, Player& p);
		bool mangaSourceSeriesPointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaSourceSeries(const Player& p);
		void mangaSourceSeriesLoad(Player& p, bool refresh = false);
		void mangaSourceSeriesRefresh(Player& p);		// L2: ask the source again; a failure keeps what shows
		void mangaSourceSeriesFetched();				// both halves are in: save, or keep the list and toast
		void mangaSourceSeriesFail(const std::string& why);	// before the jobs: what shows stays
		bool mangaSourceSeriesCacheLoad();				// the card's copy into the page; false: none to show
		void mangaSourceSeriesCacheSave();
		void mangaSourceSeriesChoose(int i, Player& p);
		void mangaSourceSeriesResume(Player& p);
		void mangaSourceSeriesInLibrary(Player& p);
		void mangaSourceChapterOpen(const std::vector<MangaChapter>& chapters, int i, int page,
			const std::string& seriesId, const std::string& seriesTitle, Player& p);
		void mangaSourcePagesAsk(Player& p);			// the chapter's Pages job
		void mangaSourceImageAsk();					// the next page's Image job, while one has no url
		void mangaSourceCancel();						// the source's jobs dropped (its page or series left)
		void mangaSourceChapterReady(const Player& p);	// its pages are known: the read request, the reader page
		void mangaSourceChapterStep(int dir, Player& p);
		int mangaReadChapterTotal() const;
		std::string mangaNetSignature() const;			// this part of the page for signature()
		// ---- the network folders (ui_manga_netfolders.cpp) ----
		// Browse lists the saved connections; A on one lists its folders through the worker. A folder laid out as a
		// series (chapter folders of pictures, or CBZ/ZIP archives) opens as its series page; reading a chapter copies
		// it into the page cache's own folder first, then opens that with MangaReadRequest. The series and the
		// progress are keyed by their bpnet:// paths, never by a credential.
		enum MangaNetView { MNF_OFF, MNF_DIRS, MNF_SERIES };
		struct MangaNetRow
		{
			std::string name, path;
			bool dir = false;
			bool archive = false;
		};
		int mangaNetView = MNF_OFF;
		int mangaNetCursor = 0, mangaNetScroll = 0;
		std::string mangaNetServerId, mangaNetStart, mangaNetDir;
		std::string mangaNetBackDir;	// the folder a series was opened from (B returns there; "" the connection's own)
		std::vector<MangaNetRow> mangaNetRows;
		std::string mangaNetError;
		bool mangaNetLoading = false;
		uint64_t mangaNetSerial = 0;			// the listing or scan in flight
		std::vector<NetMangaChapter> mangaNetChapters;
		std::string mangaNetSeriesPath, mangaNetSeriesTitle;
		uint64_t mangaNetFetchSerial = 0;		// a chapter's copy in flight
		int mangaNetPendAt = -1, mangaNetPendPage = 0, mangaNetPendMode = 0, mangaNetPendFit = 0, mangaNetPendHinge = 36;
		bool mangaNetPendFull = false;
		std::vector<NetServer> mangaNetFoldersServers() const;	// the saved connections, most recently used first
		int mangaNetFoldersCount() const;
		void mangaNetFoldersReset(Player& p);
		void mangaNetFoldersOpen(const std::string& serverId, Player& p);
		void mangaNetFoldersLeave();
		void mangaNetList(const std::string& dir);
		void mangaNetScan(const std::string& path);
		void mangaNetSeriesOpen(const NetMangaSeries& s);
		void mangaNetChapterOpen(int i, int page0, Player& p);
		void mangaNetFetchStart(const NetMangaChapter& c);
		void mangaNetChapterReady(const std::string& localPath, const Player& p);
		std::string mangaNetCachePath(const NetMangaChapter& c) const;
		bool mangaNetCached(const std::string& localPath, bool archive) const;
		void mangaNetCancel();
		bool mangaNetFoldersKey(Button b, Player& p);
		bool mangaNetFoldersPointer(const Pointer& pt, int x, int y, Player& p);
		void mangaNetFoldersTap(int i, Player& p);	// a tap on row i: what A does there
		void drawMangaNetFolders(const Player& p);
		bool mangaNetFoldersResult(MangaWorker::Result& r, const Player& p);
		// ---- a source's filters (ui_manga_filters.cpp) ----
		// filters() is asked once a run per source; SELECT opens its sheet, Apply runs search("", 1, values) on the
		// Search tab. A source with none (or one whose list failed) shows no hint and no sheet.
		std::map<std::string, MangaFilterSession> mangaFilters;	// full source id -> its values and sheet
		uint64_t mangaFilterLocal = 0;							// any filter change (signature() follows it)
		void mangaFilterAsk();									// its filters(), once a run
		bool mangaFilterAvailable() const;						// it declared some: SELECT opens the sheet
		bool mangaFilterShown() const;							// the sheet takes the keys and taps
		MangaFilterSession* mangaFilterSession();
		const MangaFilterSession* mangaFilterSession() const;
		void mangaFilterShow(Player& p);
		void mangaFilterClose();
		bool mangaFilterKey(Button b, Player& p);
		bool mangaFilterPointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaFilters(const Player& p);
		std::string mangaSourceFiltersJson() const;				// what a Search job sends ("{}" without filters)
		void mangaFilterTextTyped(const std::string& text);		// the keyboard's Done
		static constexpr int MF_ROW_TOP = 118, MF_ROWS = 8, MF_ROW_H = 36;
		Rect mangaFilterBox() const { return {48, 64, 544, 372}; }
		Rect mangaFilterRowRect(int i) const { return {64, MF_ROW_TOP + i * MF_ROW_H, 512, MF_ROW_H - 4}; }
		Rect mangaSrcTabRect(int i) const { return {60 + i * 120, 64, 116, 30}; }
		// two whole rows of covers with their titles, down to the hint bar (186 apart left a black band under them)
		static constexpr int MG_SRC_TOP = 104, MG_SRC_STEP = 172, MG_SRC_SER_TOP = 200,
			MG_SRC_STATUS_Y = 410;
		// a source's series page: its buttons right under the details line, its chapters right under them
		static constexpr int MG_SRC_BTN_Y = 104, MG_SRC_SERIES_TOP = 160;
		Rect mangaSrcCoverRect(int i) const
		{
			int row = i / MG_COLS, col = i % MG_COLS;
			return {16 + col * 124, MG_SRC_TOP + (row - mangaSrcScroll) * MG_SRC_STEP, MG_COVER_W, MG_COVER_H};
		}
		// a source series page's one buttons row, 48 high and single-line glyph + label each (the keys are in the
		// hints): Resume (A), Download (R2), Refresh (L2), In library (Y); 608 wide with the page's margins
		// a source series' row of five: Resume, Download, Refresh, Info, Library (mserBtn 0..4)
		// (Info, its label short, narrower: the others get the room; "Download" at the small size needs 104 of them)
		static Rect mangaSrcBtn(int i)
		{
			static const int X[5] = {16, 148, 280, 412, 500}, Wd[5] = {124, 124, 124, 80, 124};
			return {X[i], MG_SRC_BTN_Y, Wd[i], 48};
		}
		Rect mangaSrcResumeRect() const { return mangaSrcBtn(0); }
		Rect mangaSrcRefreshRect() const { return mangaSrcBtn(2); }
		Rect mangaSrcInfoRect() const { return mangaSrcBtn(3); }
		Rect mangaSrcHeartRect() const { return mangaSrcBtn(4); }
		// Info: the series' details (its description) in a dialog, off the page (it took two lines above the row)
		bool mangaSrcInfoShown = false;
		void drawMangaSrcInfo();

		// ---- the saved servers (ui_manga_servers.cpp) ----
		// Browse › Y opens the Add server form: kind, name, address, user, password; Test, Save. A server's
		// libraries or catalogue, then its series, are a cover grid here, and a series is Page::MangaSeries with an
		// id "srv:<server id>:<key>" so the reader and the History find their way back. The catalogue and the series
		// page are views of Page::Manga (mangaView 2 and up), as a source's page is.
		static constexpr int MGSRV_NONE = 0, MGSRV_CATALOGUE = 1, MGSRV_SERIES = 2, MGSRV_FORM = 3;
		static constexpr int MGSRV_LOAD_LIBRARIES = 1, MGSRV_LOAD_SERIES = 2, MGSRV_LOAD_FEED = 3;
		static constexpr int MGSRV_FROM_LIST = 0, MGSRV_FROM_FOLDER = 1, MGSRV_FROM_HISTORY = 2;
		int mangaSrvView = MGSRV_NONE;				// what the server's own page shows (mangaView is 2 while it does)
		std::string mangaSrvId, mangaSrvName, mangaSrvKind;	// the server open
		std::string mangaSrvRootUrl;				// an OPDS server's start feed (Kavita's key is in it)
		std::string mangaSrvHere;					// the heading: the server, or the folder open
		std::vector<std::string> mangaSrvPath;		// the folders opened: Komga library ids, OPDS feed urls
		std::vector<RemoteItem> mangaSrvItems;		// the libraries, series or feed entries in view
		int mangaSrvCursor = 0, mangaSrvScroll = 0, mangaSrvPage = 0;
		bool mangaSrvMore = false, mangaSrvLoading = false, mangaSrvAppend = false;
		std::string mangaSrvNext;					// a feed's own next link, when it names one
		std::string mangaSrvError;
		int mangaSrvLoad = 0;						// what the request in flight is: MGSRV_LOAD_*
		std::string mangaSrvLoadUrl;				// the url asked for (a feed's relative links resolve on it)
		uint64_t mangaSrvFetchSerial = 0;
		// a server's series: its chapters as the server gave them (oldest first for Komga, the feed's order for OPDS)
		std::string mangaSrvSeriesId, mangaSrvSeriesKey, mangaSrvSeriesTitle;
		std::vector<RemoteChapter> mangaSrvChapters;
		bool mangaSrvChaptersOn = false;
		int mangaSrvSerCursor = 0, mangaSrvSerScroll = 0, mangaSrvSerPage = 0, mangaSrvSeriesFrom = MGSRV_FROM_LIST;
		bool mangaSrvSerMore = false, mangaSrvSerLoading = false, mangaSrvSerAppend = false;
		std::string mangaSrvSerError;
		uint64_t mangaSrvSerSerial = 0;
		// the form (Add server, Edit): the four typed fields, the kind and what Test answered
		std::string mangaSrvFormId;					// "" adding; else the server edited
		std::string mangaSrvFormKind = "komga", mangaSrvFormName, mangaSrvFormUrl = "https://", mangaSrvFormUser,
			mangaSrvFormPassword;
		bool mangaSrvFormPasswordTyped = false;		// a new password typed: it is sealed on Save, never shown again
		int mangaSrvFormCursor = 0;
		std::string mangaSrvFormNote, mangaSrvTestText;
		uint64_t mangaSrvTestSerial = 0;
		// X on a server row: Edit / Delete, the delete asked twice (as the other clears)
		bool mangaSrvMenu = false;
		std::string mangaSrvMenuId;
		// On this handheld's list: Y, where to put manga (MLS_HELP); X, a series' Open / Delete, the delete asked twice
		// entering Manga without the manga engine: Download it / Not now (asked again at the next entry)
		bool mengSheet = false;
		int mengCursor = 0;
		bool mengBusy = false;
		bool mengUpgrade = false;	// the sheet offers the lighter runtime (an install on HotSpot), not the engine itself	// Download chosen: the sheet stays with its progress until it's done; Cancel only
		Rect mangaEngineBox() const { return {64, 88, 512, 300}; }
		Rect mangaEngineRow(int i) const { return {80, 268 + i * 48, 480, 40}; }
		bool mangaEngineKey(Button b, Player& p);
		void mangaEngineChoose(int i, Player& p);
		void mangaEngineWatch(Player& p);
		bool mangaEngineMissing() const;	// not on the card and not downloading: the offer and its row show
		void mangaEngineOffer();			// the offer, again (the Repositories row)
		std::string mangaEngineSignature() const;	// while downloading: its state and MB so far	// the download done (the sheet closes) or failed (back to its offer)
		void drawMangaEngineSheet();
		enum { MLS_NONE, MLS_HELP, MLS_MENU };
		int mlocSheet = MLS_NONE, mlocCursor = 0;
		bool mlocDeleteArmed = false;
		Uint32 mlocDeleteAt = 0;
		Rect mangaLocalHelpBox() const { return {48, 64, 544, 316}; }
		bool mangaLocalSheetKey(Button b, Player& p);
		bool mangaLocalSheetPointer(int x, int y, Player& p);
		void drawMangaLocalSheet(const Player& p);
		std::string mangaLocalFolder() const;	// the Manga folder to name: the first on the card, else the downloads'
		void mangaLocalDeleteNow(Player& p);
		int mangaSrvMenuCursor = 0;
		bool mangaSrvDeleteArmed = false;
		Uint32 mangaSrvDeleteAt = 0;
		uint64_t mangaSrvLocal = 0;					// the pages' own changes (a field typed, a row acted on)
		void mangaServersReset();					// the app opened or left: every job dropped and the pages closed
		void mangaServersPoll();					// the fetcher's answers (the draws and keys call it)
		bool mangaServersKey(Button b, Player& p);
		bool mangaServersPointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaServers(const Player& p);
		void mangaServerOpen(const std::string& id, Player& p);
		void mangaServerBack(Player& p);
		void mangaServerListLoad(int load);			// the libraries, a library's series or a feed
		void mangaServerFeedLoad(const std::string& url);	// a feed's next page, or a folder just opened
		void mangaServerMore();						// the next page of what shows
		void mangaServerReload();					// A after the server's error: the same list again
		void mangaServerItemOpen(int i, Player& p);
		void mangaServerSeriesOpen(const std::string& key, const std::string& title, int from);
		void mangaServerSeriesOpenFromId(const std::string& id, Player& p);	// the History's way in
		void mangaServerSeriesLoad();
		void mangaServerSeriesReload();				// A after the chapters' error
		void mangaServerSeriesMore();
		void mangaServerSeriesBack(Player& p);
		bool mangaServerSeriesKey(Button b, Player& p);
		bool mangaServerSeriesPointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaServerSeries(const Player& p);
		void mangaServerSeriesChoose(int i, Player& p);
		void mangaServerSeriesResume(Player& p);
		void mangaServerChapterOpen(int i, Player& p);
		void mangaServerChapterStep(int dir, Player& p);
		int mangaServerChapterIndex() const;
		void mangaServerAddOpen(Player& p);
		void mangaServerEditOpen(const std::string& id, Player& p);
		bool mangaServerFormKey(Button b, Player& p);
		void drawMangaServerForm(const Player& p);
		void mangaServerFormChoose(Player& p);
		void mangaServerFormSave(Player& p);
		void mangaServerTest();
		void mangaServerFieldTyped(int field, const std::string& text);	// ui_forms.cpp's kbClose
		void mangaServerCancel();
		void mangaServerModalOpen(int row, Player& p);
		bool mangaServerModalKey(Button b, Player& p);
		void drawMangaServerModal(const Player& p);
		std::string mangaServersSignature() const;
		static constexpr int MGSRV_TOP = 104, MGSRV_STEP = 186, MGSRV_STATUS_Y = 410;
		// a server's series page keeps its own Resume button: the cards' pages and a source's row differ (the source's
		// shares its row with Download, Refresh and the heart)
		Rect mangaServerResumeRect() const { return {16, 144, 220, 48}; }
		static constexpr int MGSRV_FORM_ROWS = 7;	// Kind, Name, Address, User, Password, Test, Save
		static constexpr int MGSRV_GRID_ROWS_SHOWN = 2, MGSRV_CLIP_TOP = 96, MGSRV_CLIP_BOTTOM = 398;
		Rect mangaSrvCoverRect(int i) const
		{
			int row = i / MG_COLS, col = i % MG_COLS;
			return {16 + col * 124, MGSRV_TOP + (row - mangaSrvScroll) * MGSRV_STEP, MG_COVER_W, MG_COVER_H};
		}
		Rect mangaSrvFormRowRect(int i) const { return {16, 74 + i * 40, 608, 36}; }
		Rect mangaServerModalBox() const { return {112, 150, 416, 170}; }
		Rect mangaServerModalRowRect(int i) const { return {128, 214 + i * 48, 384, 40}; }
		// ---- the dashboard's chapters for a source series (api_manga.cpp, ui_manga_web.cpp) ----
		// The worker's answer is kept by series: the page polls while it comes (202), then reads it at once. The
		// read marks and download states are filled in on every read, since the queue moves underneath the list.
		struct MangaWebChaptersState
		{
			std::vector<MangaChapter> chapters;
			std::string full, key, itemJson, title, source, error;
			uint64_t serial = 0;
			Uint32 askedAt = 0;
			bool have = false;
		};
		std::map<std::string, MangaWebChaptersState> mangaWebCache;
		bool mangaWebWorker(MangaWorker::Result& r);	// true: the answer was one of the dashboard's lists
		// ---- the manga reader's Downloads (ui_manga_downloads.cpp) ----
		// Page::Manga's Downloads tab: the queue, then what the downloads put on the card; and the Download sheet a
		// source's series page opens (R2, or its button): This chapter / Next 5 unread / All unread / Choose chapters.
		// A selected chapter's page list is asked of the source's worker first (Pages, then Image while a page has
		// only a key), so the downloader never takes an item without pages. Only archives carrying our ComicInfo
		// (BPSource / BPSeriesKey) count as downloads: the same Manga folder also holds the card's own series.
		struct MangaDlCardChapter
		{
			std::string title, path;	// the .cbz, its title as ComicInfo has it (else its name)
			std::string sourceId, seriesKey;	// what the download wrote (ComicInfo BPSource / BPSeriesKey)
			int pages = 0;
			uint64_t bytes = 0;
			double number = -1;	// ComicInfo's Number, when it is one
		};
		struct MangaDlCardSeries
		{
			std::string name, path;	// the folder's name and path
			std::vector<MangaDlCardChapter> chapters;
			uint64_t bytes = 0;
		};
		// a chapter's download state, as a series page's rows and the Choose list mark it
		struct MangaDlMark
		{
			enum Kind { None, Queued, Running, Paused, Failed, OnCard } kind = None;
			int done = 0, total = 0;
		};
		// one chapter waiting for its page list: the worker asks the source, then the queue gets the item
		struct MangaDlAsk
		{
			std::string sourceId, scriptPath, seriesId, seriesTitle, chapterId, chapterTitle, chapterJson;
		};
		enum MangaDlSheet { MDLS_NONE, MDLS_MENU, MDLS_CHOOSE };
		mutable std::vector<MangaDlCardSeries> mdlCard;	// the card's downloads, scanned when the page or tab needs them
		uint64_t mdlCardStamp = 0;	// what the last scan found, so an unchanged card doesn't redraw every frame
		Uint32 mdlScanAt = 0;		// the scan is slow enough to be worth throttling
		bool mdlCardOn = false;
		uint64_t mdlLocal = 0;		// the tab's own changes (a scan, a delete), for signature()
		int mdlCursor = 0, mdlScroll = 0;	// the queue, then Pause all, then the card's series
		bool mdlCardOpen = false;	// A on a card series: its chapters
		int mdlCardAt = -1;			// the card series open (its index; the queue's length may change under it)
		int mdlCardCursor = 0, mdlCardScroll = 0;
		std::string mdlDeletePath;	// X once: the series folder or chapter armed; again within 2 s deletes
		Uint32 mdlDeleteAt = 0;
		std::string mdlNote;		// a page list that didn't come, or what the card couldn't do
		Uint32 mdlNoteUntil = 0;
		// the Download sheet over a source's series page
		int mdlSheet = MDLS_NONE, mdlSheetCursor = 0;
		int mdlChooseCursor = 0, mdlChooseScroll = 0;
		std::set<int> mdlChosen;	// chapter indices ticked in Choose chapters
		std::vector<MangaDlAsk> mdlAsks;	// chapters waiting for their page list, in the order they were picked
		MangaDlAsk mdlAsk;			// the one in the worker now
		bool mdlAskOn = false;
		uint64_t mdlPagesSerial = 0, mdlImageSerial = 0;
		std::vector<MangaPage> mdlAskPages;
		std::vector<std::string> mdlAskUrls;
		int mdlAskImageAt = 0;
		void mangaDownloadsOpen(Player& p);		// the tab opened: its lists read, finished queue items cleared
		void mangaDownloadsReset();				// the app opened: an ask the last session left waits for the worker again
		void mangaDownloadsTick(Player& p);		// the armed delete and the note time out; a lost worker answer resumes
		bool mangaDownloadsKey(Button b, Player& p);
		bool mangaDownloadsPointer(const Pointer& pt, int x, int y, Player& p);
		bool mangaDownloadsBack();	// B: out of a card series' chapters (true when it went somewhere)
		void drawMangaDownloads(const Player& p);
		void drawMangaDlCardSeries(const Player& p);
		void mangaDlEnsureCard(bool force = false);	// the downloads folder read (at most every 1.5 s)
		uint64_t mangaDlCardBytes() const;
		uint64_t mangaDlFreeBytes() const;			// statvfs of the downloads folder, or its nearest parent
		int mangaDlQueueCount() const;				// items still waiting, running, paused or failed
		const MangaDlCardChapter* mangaDlCardFor(const std::string& sourceId, const std::string& seriesId,
			const MangaChapter& c) const;
		MangaDlMark mangaDlMarkFor(const std::string& sourceId, const std::string& seriesId, const MangaChapter& c) const;
		void drawMangaDlMark(const MangaDlMark& m, int right, int cy, bool at);
		void mangaDlOpenCbz(const std::string& seriesId, const std::string& seriesTitle, const std::string& chapterId,
			const std::string& chapterTitle, const std::string& path, int page0, Player& p);
		bool mangaDlOpenSourceChapter(const std::vector<MangaChapter>& chapters, int i, int page0,
			const std::string& seriesId, const std::string& seriesTitle, Player& p);	// true: it was on the card
		void mangaDlOpenCardAt(int seriesIndex, int chapterIndex, Player& p);
		bool mangaDlBlocked(const std::string& sourceId, const std::string& seriesId, const MangaChapter& c) const;
		void mangaDlEnqueueChapter(const std::string& sourceId, const std::string& scriptPath, const std::string& seriesId,
			const std::string& seriesTitle, const MangaChapter& c);
		void mangaDlAskPump();		// the next chapter's Pages job
		bool mangaDlImageAsk();		// the next page's Image job, while one has only a key
		void mangaDlYieldToSource();	// a chapter being read needs the worker first: the ask waits its turn
		void mangaDlAskFinish(bool queued, const std::string& note);	// the chapter's page list is done (or didn't come)
		void mangaDlWorker(MangaWorker::Result& r);	// its answers (the rest of mangaBrowsePoll's are the browse page's)
		std::vector<int> mangaDlUnreadFrom(int from, int limit) const;	// unread source chapters, in list order
		// the Download sheet
		Rect mangaDlButtonRect() const { return mangaSrcBtn(1); }
		Rect mangaDlSheetBox() const { return mdlSheet == MDLS_CHOOSE ? Rect{16, 62, 608, 366} : Rect{100, 96, 440, 260}; }
		Rect mangaDlSheetRowRect(int i) const;
		void mangaDlSheetOpen(Player& p);
		void mangaDlSheetClose();
		bool mangaDlSheetKey(Button b, Player& p);
		bool mangaDlSheetPointer(const Pointer& pt, int x, int y, Player& p);
		void mangaDlSheetAct(Player& p);
		void drawMangaDlSheet(const Player& p);
		std::string mangaDlSignature() const;		// this part of the page for signature()
		// the Downloads tab's rows: the queue (up to 3), Pause all, then the card's series (up to 2). With nothing
		// queued the queue's room goes to the card: one dim line, then the series from MDL_CARD_TOP_C down to the
		// hint line, as many as fit
		static constexpr int MDL_TOP = 138, MDL_QROW = 58, MDL_QSHOWN = 3, MDL_ALL_Y = 316, MDL_CARD_TOP = 368,
			MDL_CROW = 38, MDL_CSHOWN = 2;
		static constexpr int MDL_NOTE_Y = 122, MDL_CARD_LABEL_C = 142, MDL_CARD_TOP_C = 156;
		int mdlCardRows() const
		{
			if (mangaDlQueueCount() > 0) return MDL_CSHOWN;
			return std::max(1, (footY() - 16 - MDL_CARD_TOP_C) / MDL_CROW);
		}
		int mdlCardTop() const { return mangaDlQueueCount() > 0 ? MDL_CARD_TOP : MDL_CARD_TOP_C; }

		// ---- the manga reader's settings (ui_manga_settings.cpp) ----
		// A page of the app's own, not a group of Banana Player's Settings: START on the app's pages (any tab) or over
		// the reader opens it, and so does Manage extensions' X / gear. Its rows are the same ids as Settings' (drawn
		// and stepped as its rows are: drawSettingRow, changeSetting); they scroll as its lists do.
		// mangaSettingsFrom: the page B goes back to (the reader stays open behind it)
		Page mangaSettingsFrom = Page::Manga;
		int mangaSettingsTab = MT_LIBRARY;	// the app's tab it came from (B goes back to it)
		bool mangaSettingsList = false;		// Browse's series list was open (B goes back to it too)
		void openMangaSettings();	// from where it is now (mangaSettingsFrom)
		void closeMangaSettings();
		bool mangaSettingsKey(Button b, Player& p);
		void drawMangaSettings(const Player& p);
		std::vector<int> mangaSettingRows() const;	// the page's rows, by id (the note last)

		// ---- playing behind the browser ----
		// the player left (B, "<", Back) while a song or video plays on: the browser is the base page; showPlayer ends it
		bool bgPlay = false;
		bool stopPressed = false;	// the player's own Stop (takeStopPressed)
		bool playerOverPhoto = false;	// the mini bar's tap over a photo (takePlayerOverPhoto)
		// the player shown (showPlayer) and not left since; back: where it was opened from (backFromPlayer)
		bool onPlayer = true;
		struct PlayerBack
		{
			bool set = false;
			Page page = Page::Browse;
			bool inMenu = false;
			int menuTab = 0, setGroup = -1, cursor = 0, scroll = 0;
			std::string menuReturnDir;
			int menuReturnCursor = 0, menuReturnScroll = 0;
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
		// the player opened on a playlist's first song: its List key (the way to the playlist) glows a subtle yellow
		// twice
		bool listPulseArm = false;
		std::string listPulsedFor;	// the playlist it glowed for (cleared when something else plays)
		Uint32 listPulseAt = 0;
		static constexpr Uint32 LIST_PULSE_MS = 1400;
		bool markAnim = false;	// drawBattery drew the playing mark moving
		int markW(const Player& p) const;	// its room in the header (0 none)
		// Incognito mode (Quick settings' Incognito mode): nothing played goes in Recent (Player::incognito) and
		// reading adds nothing to the manga History (places, read marks, resume points, the library and downloads go
		// on as ever). Never saved: off at every start. Its mark sits left of the clock while it's on
		bool incognito = false;
		int incognitoW() const { return incognito ? 18 + 10 : 0; }
		bool markLive(const Player& p) const;	// it moves: what plays plays (a page's: as the page does)
		void drawMark(int right, int cy, const Player& p);
		std::string homeListedFor;	// what played as Home's Recent was listed (it's listed again when that changes)
		// the equalizer changed from the web (a slider's run): written (commitEq) a second after the last change
		Uint32 eqWebDirtyAt = 0;
		// a seek waiting for its file to open (the hand-off from a browser: at that position): path, seconds
		std::string seekPath;
		double seekTo = -1;
		Uint32 seekUntil = 0;
		// the paths a web selection plays (Up next lists them)
		std::vector<std::string> webSelection;
		// playlists edited from the web (WebPlaylistGlue), on the main thread. plWebFile: a playlist's file from its
		// id (its file's name less .m3u), "" none. plWebEdited: the file was saved with remap (old -> new): the page
		// showing it reads it again, the player told. Renamed, deleted, made: the pages that show them follow
		std::string plWebFile(const std::string& id) const;
		void plWebEdited(const std::string& file, const std::vector<int>& remap, Player& p);
		void plWebRenamed(const std::string& old, const std::string& file, const std::string& name, Player& p);
		void plWebDeleted(const std::string& file, Player& p);
		void plWebListChanged(Player& p);
	};
}
