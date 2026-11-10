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

	// The bottom panel: what is playing and where, the transport (previous, -5 s, play, +10 s, next), the choices
	// (audio, subtitles, aspect, speed, settings) and their pages. Laid out on 640x480 (the RG DS's panel) and scaled
	// to the panel it is on.
	class Ui
	{
	public:
		static constexpr int W = 640, H = 480;
		// a small picture: rows of cells, '#' drawn (ui.cpp's G_*)
		struct Glyph { const char* const* rows; int h; };

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
		bool animating() const { return playHoldAt != 0 || flickAt != 0 || plDrag.on || stripT0 != 0 || likePopAt != 0 || addFlashUntil != 0 || landFlashUntil != 0 || listPulseAt != 0; }
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
		enum class Page { Main, Tracks, Chapters, Settings, Browse, Info, About, Exit, Sound, Web, Playing, UpNext };
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
		enum SetGroup { GROUP_SUBS, GROUP_MUSIC, GROUP_PHOTOS, GROUP_REMEMBER, GROUP_FILES, GROUP_CONTROLS, GROUP_UPDATES, GROUP_WEB, GROUPS };
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
		bool homeKey(Button b, Player& p);	// true: B twice, leave
		void goHome(int zone, int idx, Player& p);	// back to Home, the focus there
		void homeOpen(Player& p);	// A: the card's file, All recent, a tile's section
		// a finger on Home: on a card (a tap plays, a hold opens its menu, a drag scrolls the strip) or a tile
		bool homeTouch = false, homeMoved = false;
		int homeDownX = 0, homePress = -1;	// homePress: a card's index, or HOME_TILE + a tile's
		double homeDownOff = 0;
		Uint32 homeHoldAt = 0;
		static constexpr int HOME_TILE = 1000;
		bool homePointer(const Pointer& pt, int x, int y, Player& p);
		void drawHome(const Player& p);
		bool showHidden = false;	// the setting, kept here for drawBrowseMenu (no Player there)
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
		// a row's chips share the dialog's width: Show's 4, Style's one an offered visualizer (VIS_SHOWN)
		static int displayChips(int row) { return row ? VIS_SHOWN_COUNT : SHOWS; }
		Rect displayChip(int row, int i) const
		{
			int n = displayChips(row), w = (544 - (n - 1) * 8) / n;
			return {48 + i * (w + 8), row ? 260 : 160, w, 64};
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
		std::string drawn, fontsDir;

		Page page = Page::Main;
		int cursor = 0, scroll = 0;
		Uint32 quitArmed = 0;		// B pressed once: a second press within 2 s leaves
		bool dragging = false;
		double dragFraction = 0, scrubbed = -1;
		Uint32 scrubAt = 0;
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
		static constexpr int ROW_Y = 84, ROW_H = 48, ROWS_SHOWN = 7;
		// the settings' rows by id: the subtitle style's, Music display, Visualizer, the switches (REMEMBER), Pan area,
		// Clear play history, ..., Sound (it opens the Sound page). Shown a group at a time (settingRows), after a list
		// of the groups
		static constexpr int STYLE_ROW0 = 0, MUSIC_ROW = STYLE_KEYS, VIS_ROW = STYLE_KEYS + 1, TOGGLE_ROW = STYLE_KEYS + 2,
			TOGGLES = 12, PAN_ROW = TOGGLE_ROW + TOGGLES, DRAG_ROW = PAN_ROW + 1, CLEAR_ROW = DRAG_ROW + 1,
			CHECK_ROW = CLEAR_ROW + 1, INSTALL_ROW = CHECK_ROW + 1, TOP_LIGHT_ROW = INSTALL_ROW + 1,
			BOTTOM_LIGHT_ROW = TOP_LIGHT_ROW + 1, SOUND_ROW = BOTTOM_LIGHT_ROW + 1, CLOCK_ROW = SOUND_ROW + 1,
			SCREEN_OFF_ROW = CLOCK_ROW + 1;
		// the panels' backlights (BananaOS's own levels: changed here as its menu changes them)
		Backlight backlight;
		bool sameBrightness = true;	// the setting's, kept here for settingRows (no Player there)
		Volume volume;
		int volumePct = -1;	// as read when the quick settings opened, then as set there
		bool quickOpen = false, quickDrag = false;
		int quickCursor = 0, quickDragRow = -1;
		enum QuickRow { Q_VOLUME, Q_BRIGHT, Q_BOTTOM, Q_SAME, Q_WEB, Q_QUIT };
		int quickSub = 0;	// Web dashboard's row: 0 its switch, 1 New PIN (LEFT/RIGHT)
		std::vector<int> quickRows() const;
		bool quickKey(Button b, Player& p);
		bool quickPointer(const Pointer& pt, int x, int y, Player& p);
		void quickAdjust(int row, int dir, Player& p);
		void quickSetAt(int row, int x, Player& p);	// a finger on the row's bar
		void quickChoose(int row, Player& p);
		void drawQuick(const Player& p);
		// rows 62 apart, closer (56) when that many don't fit above the footer
		int quickStep() const { return 74 + (int)quickRows().size() * 62 <= 436 ? 62 : 56; }
		Rect quickPanel() const { int h = 74 + (int)quickRows().size() * quickStep(); return {40, (436 - h) / 2, 560, h}; }
		Rect quickRowRect(int i) const { return {56, quickPanel().y + 56 + i * quickStep(), 528, 54}; }
		Rect quickPinBtn(int i) const { Rect r = quickRowRect(i); return {r.x + r.w - 164, r.y + 3, 160, 48}; }
		Rect quickMinus(int i) const { Rect r = quickRowRect(i); return {r.x + 236, r.y + 3, 48, 48}; }
		Rect quickBar(int i) const { Rect r = quickRowRect(i); return {r.x + 296, r.y + 17, 164, 20}; }
		Rect quickPlus(int i) const { Rect r = quickRowRect(i); return {r.x + 472, r.y + 3, 48, 48}; }
		void setBrightness(Backlight::Panel panel, int permille, Player& p);
		static constexpr int SET_Y = 92, SET_H = 46, SET_SHOWN = 7, HELP_Y = 400;
		// a settings group's tile (Settings opens on them: two to a row)
		Rect groupRect(int g) const { return {16 + g % 2 * 312, 70 + g / 2 * 86, 296, 78}; }
		std::string settingHelp(int row) const;	// what a setting does, under its rows
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
		// popup's chips from CHIP_BTN (row * 8 + chip)
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
		bool backIconShown(const Player&) const { return true; }	// every page has it
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

		// ---- playing behind the browser ----
		// the player left (B, "<", Back) while a song or video plays on: the browser is the base page; showPlayer ends it
		bool bgPlay = false;
		bool stopPressed = false;	// the player's own Stop (takeStopPressed)
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
		// the mini bar (Now playing) along the bottom of Home and the browser's pages: a tap the player, its play mark
		// play / pause (a page playing the file: the page's, held: here), a hold its menu
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
