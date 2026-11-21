// Home, the browser's first page (browseDir HOMEDIR), and All recent (RECENT), redesigned on the kit (docs/redesign:
// home, home-first, home-stopped, home-more, recent-all, card-out). One screen to carry on from: a Continue card, the
// Recent row with its kind filters, and three shortcuts; the section tiles that only opened sections are gone (the
// rail has them). All recent is Home's own page: the same filters, every card in the history.
// A plain struct owned by the browser's area (Ui::Browse::homeView, ui_browse.h; docs/notes/ui-areas.md): Home is a
// folder of Page::Browse, not a page, so Browse's key and pointer hand it Home's keys and touches (homeKey, homePointer)
// and Browse::draw draws it (drawHome; recent-all: drawAllRecent). Stayed Ui's: what plays a recent file or shows it in
// its folder (openFile, showInFolder), the playing file's chip (every page's), the settings mirrored
// for drawing (showHidden, mangaOn) and the Home touches Ui::pointer takes before the area's point (the quit dialog
// over every finger, a card's hold in progress), which call in here where they always were.
// Home draws only its body in lay::shell().body; Ui::Shell draws the header and hint bar from title(), crumb(),
// hintList() and draws the rail, the header's back and More keys.
#pragma once
#include "ui.h"

namespace bp
{
	struct Ui::Home
	{
		Ui& ui;
		explicit Home(Ui& u) : ui(u) {}

		// Home's Recent (and All recent) listed again where it's being looked at (the next song started, the web removed
		// one): in the order it was shown, so nothing moves under the cursor; a newly played one comes when it's
		// entered again (each entry lists it newest first)
		struct RecentItem
		{
			std::string path, name, sub, where, at;	// at: where it was left ("0:27"), "" from the start
			const Glyph* kind;
			double frac;	// the bar: how far into it (played through: 1; -1 none)
			bool net;
			std::string missing;	// why it can't open ("Card 2 is out"; recentMissing): drawn dimmed, its sheet offers Remove
			bool gone = false;	// missing and not on a card that's out: what Clear missing takes
		};
		std::vector<RecentItem> recent;	// as listed with Home (or All recent)
		void listRecent(Player& p);
		bool recentKeepOrder = false;

		// All recent's filters: 0 everything, 1 videos, 2 music, 3 books and documents (L2/R2, or a tap on them); Home's
		// Recent row uses the same four, so a kind chosen on one page is the kind on the other. The counts as last listed.
		int recentTab = 0, recentCounts[4] = {0, 0, 0, 0};
		Rect recentTabRect[4];
		bool drawRecentTabs(int x, int mid, int right, bool counts = true);	// (Home's past header helper; All recent draws its own)
		bool recentTabsUp = false;
		void stepRecentTab(int dir, Player& p);
		void setRecentFilter(int tab);	// Home's chips: the same four, no page change
		void recentInOrder(const std::vector<std::string>& before);
		// the history changed from elsewhere (the web): Home or All recent listed again, the focus kept
		void recentEdited(Player& p);

		// ---- where the focus is ----
		// The three places the D-pad moves between (the old strip and grid names are kept for the browser's state
		// line and the hold: HOME_STRIP is the Recent row, HOME_GRID the shortcuts); HOME_EMPTY is "nothing played yet".
		enum HomeZone { HOME_CONTINUE, HOME_CARDS, HOME_SHORTCUTS, HOME_EMPTY, HOME_STRIP = HOME_CARDS, HOME_GRID = HOME_SHORTCUTS };
		int homeZone = HOME_CARDS;
		int homeStrip = 0;	// the focused card's index in `recent` (recent.size(): the chevron card)
		int homeShortcut = 0, homeGrid = 0, homeCol = 0;
		bool homeFresh = true;	// not yet shown this run: the focus starts on Continue (or the newest card)
		// the old strip's measures (the browser's state line names them; Home no longer scrolls sideways)
		static constexpr int CARD_W = 240, STRIDE = 250, CARD_Y = 84, CARD_H = 104, STRIP_TOP = 80, STRIP_H = 116;
		static constexpr int HOME_RECENT_MAX = lay::HOME_CARDS_MAX;
		int stripCards() const { return (int)recent.size(); }	// every card; the row shows the first four
		int stripCount() const { return stripCards() + 1; }	// (+ the chevron card)
		int stripMax() const { return 0; }
		int stripTarget(int) const { return 0; }
		double stripOff = 0, stripFrom = 0, stripTo = 0;
		Uint32 stripT0 = 0;
		void setStrip(int i, bool animate = true);
		void stripStep();	// (the old eased scroll; now only the card under a finger's hold is timed elsewhere)
		int visibleCount() const;	// the cards the chosen filter leaves
		int firstVisible() const;
		int nextVisible(int from, int dir) const;
		int nthVisible(int n) const;	// the n-th card the filter leaves (the row's left-to-right order)
		bool matchesFilter(int i) const;
		bool moreShown() const { return visibleCount() > HOME_RECENT_MAX; }
		// the Continue card: what plays now, else the newest unfinished thing (a card's even when its file has gone:
		// the place is kept). label: Now playing / Continue / Stopped for Manga. Live: the note is the time and the
		// the Now playing block's state pills (speed, repeat or Shuffle, the EQ, "Playing in ..."), and key is Pause / Play
		struct Continue
		{
			bool shown = false, live = false, stopped = false, paused = false;
			std::string path, title, note, label, key, artKey;
			std::string missing;	// why it can't open (the Recent item's reason): drawn dimmed, no key, opening it offers Remove
			std::vector<std::string> pills;	// all Live (yellow), the kit's statePills: away from normal, as Quick settings' block has them
			const Glyph* kind = nullptr;
			double frac = 0;
		};
		Continue continueOf(const Player& p) const;
		// the picture a Recent tile or the Continue card draws for a file: a song's album art, a book's cover, a video's
		// list thumbnail once made (asked here); null: the kind's mark (a photo, a network file, a video not yet read)
		std::function<void(Rect)> pictureFor(const std::string& path, const Glyph* kind, const std::string& artKey, int rad) const;
		mutable std::string mangaStopped;	// a video Manga stopped (the Continue card says why); cleared when it plays again
		mutable Uint32 mangaStopSeen = 0;
		// the last song or video that played: Recent keeps a card where it was when it is played from there, so after Stop
		// the card is this one, not whatever heads Recent
		mutable std::string lastLive;

		// ---- a song's real album art (the Continue card and the Recent cards) ----
		// The cache is Ui's now (ui.h, "a song's album art"): one set of textures shared with the rail's
		// Now playing key and Quick settings' Now playing block. Home draws through Ui::drawSongArt, and its
		// sig() follows ui.artSerial, so an arriving cover redraws the page.

		void openContinue(const Continue& c, Player& p);
		void openCard(int i, Player& p);	// A / a tap: the card's file, or the chevron's All recent
		void openShortcut(int i, Player& p);
		void goHome(int zone, int idx, Player& p);	// back to Home, the focus there
		void homeOpen(Player& p);	// A: what the focus is on (kept for the browser's callers)
		bool homeKey(Button b, Player& p);	// true: Quit chosen in B's dialog, leave
		bool homePointer(const Pointer& pt, int x, int y, Player& p);
		// All recent's rows, on the browser's smooth list (its chips are Ui::pointer's): a drag scrolls, a tap on a
		// row opens it as the finger lifts there
		bool recentPointer(const Pointer& pt, int x, int y, Player& p);
		int recentTouch = -1;	// the row the finger went down on (-1: none, or the list's ground)
		bool recentMoved = false;
		void homeFocusFromPointer();	// a touch never shows the focus; it moves with what was touched

		// a finger on Home: on a card (a tap plays, a hold opens its More sheet) or a shortcut; the old drag fields
		// stay for Ui's hold and state line
		bool homeTouch = false, homeMoved = false;
		int homeDownX = 0, homePress = -1;	// homePress: a card's index, HOME_PRESS_CONTINUE, or HOME_TILE + an old tile's
		double homeDownOff = 0;
		Uint32 homeHoldAt = 0;
		Uint32 resumeHoldAt = 0;	// a finger on the live card's key: held, it takes over from a browser (Quick settings' block's)
		static constexpr int HOME_TILE = 1000;
		static constexpr int HOME_PRESS_CONTINUE = -2;	// a finger held on the Continue card (its More, at 500 ms)
		// what a finger is on (one of them: the lift acts only if it is still on it)
		bool pressedContinue = false, pressedResume = false;
		int pressedCard = -1, pressedShortcut = -1, pressedChip = -1, pressedEmptyKey = -1;

		void drawHome(const Player& p);
		void drawAllRecent(const Player& p);

		// ---- the More sheet (X or a hold): the card on the left, Home's own things on the right ----
		std::string ctxRecent;	// the file the sheet is for ("" Home's own)
		std::string morePath;	// the same, for the open More sheet ("" Home's)
		Uint32 clearArmedAt = 0;	// Clear recent asks twice, as it did
		void openRecentMenu(const std::string& path);
		void openHomeOptions();
		// the browser's state line still names the old tiles' lines (browseLabels): the sections' summaries
		std::string tileSub(int g) const;
		mutable std::string tileSubs[BROWSE_TABS];
		mutable int tileSubsKey = -1;
		MoreSpec moreSpec(const Player& p);
		const RecentItem* findRecent(const std::string& path) const;
		bool listedThere(const std::string& path) const;	// in Recent, its file there
		int missingCount() const;	// Recent's files that can't open
		int goneCount() const;	// those deleted or moved away (Clear missing's)

		// ---- opening one of Recent (a card, All recent's row, the Continue card, the dashboard's handheld play) ----
		// a file that's gone: its More sheet says why and offers Remove; a song: in the list it played from
		// (RecentContext); else as Files opens it
		void openRecent(const std::string& path, Player& p);
		// the song in the list it played from (Stop and B go back to the page it was opened from); false: no list kept (the
		// caller opens the file alone). note: why the list couldn't be had ("" none), said on the handheld too
		bool playInList(const std::string& path, Player& p, std::string* note = nullptr);
		// that list found again (no play): false, no list kept. why: it's gone (the song plays alone). The dashboard's
		// Play in this browser asks this too (/api/recent/list)
		struct ListPick
		{
			RecentContext::Kind kind = RecentContext::NONE;
			std::string why, title, plName, folder;	// plName: a playlist's (its file's name less .m3u)
			std::vector<std::string> paths;	// the list (FOLDER: the song; openFile lists the folder)
			std::vector<std::string> added;	// a rail list's songs Keep playing had added (its section in Up next)
			int index = 0;
			Playlist pl;
			int album = -2, artist = -2;	// the Music page under the player: an album's, an artist's
		};
		bool pickList(const std::string& path, const Player& p, ListPick& out) const;
		// a song of the library with no list kept, or one a Files folder kept: its Music folder's songs, as Music ›
		// Folders plays them (a Music rail list: Keep playing applies, Stop and B go back to Music › Folders)
		bool musicFolderPick(const std::string& path, ListPick& out) const;
		void clearMissing(Player& p);

		// ---- the shell's header (Ui::Shell::head asks these) ----
		std::string title() const;
		std::string crumb(const Player& p) const;
		std::vector<Hint> hintList(const Player& p) const;
		// the empty state's sentences, wrapped to the kit's 400-unit line width: as many lines as they need
		std::vector<std::string> emptyText(const Player& p) const;
		std::vector<std::string> recentEmptyText(const Player& p) const;
		std::vector<std::string> wrapEmpty(const std::vector<std::string>& text) const;
		// Home's drawing state (Ui::Browse::signature adds it): what draw() shows that Ui::signature doesn't hold
		std::string sig(const Player& p) const;

		std::string homeListedFor;	// what played as Home's Recent was listed (it's listed again when that changes)
	};
}
