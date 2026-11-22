// Search (Page::Search, titled "Search library"), an area of Ui: the field, the library's answers under kind tabs
// (All / Music / Videos / Books, each with its count; L / R step them), and Recent searches. It searches the media
// library only — the index's artists, albums, songs and videos, the folders inside the Music and the Videos
// folders, and the Books shelf — never Files' folders. A result that is a container opens its own page of Music's
// or Videos' (the album, the artist, Music › Folders or Videos at that folder), and B there comes back to the
// results (comeBack).
// Built on the redesign's kit (ui_kit.h: searchField, sectionLine, resultRow, emptyState, notice, keyboard) with the
// layouts of kitlayout.h, as docs/redesign/src/screens.js draws it (search, search-results). It is a tool, not a
// section: the rail's Search key and L3 open it, no rail cell is lit on it, and B goes back where it was opened from.
// The cards' index is main's MediaIndexWorker answer (Ui::mediaIndex); the query is mediaSearch's (mediaindex.h).
#pragma once
#include "ui.h"
#include "ui_shell.h"
#include "ui_smoothlist.h"
#include "keyboard.h"

namespace bp
{
	struct Ui::Search : Ui::Area
	{
		explicit Search(Ui& u);

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Search; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;
		// the results (or Recent searches) while they show: a smooth list (ui_smoothlist.h)
		SmoothList* smoothList() override { return !typing && !rows.empty() ? &list : nullptr; }

		// ---- the rest of Ui ----
		// from the rail's key, L3 or Shell::search: the page, on its last query; a second press edits it
		void open(Player& p);
		void close(Player& p);
		void back(Player& p);	// B and the header's <: out of the keyboard, else out of Search
		// the shell's header for this page (Shell::head asks it): title, the query's line, the keys
		ShellHead head(const Player& p) const;
		// the keyboard's Done (keep): the query shown; B: back to the results as they were
		void typed(const std::string& text, bool keep);

		// where it was opened from (B goes back there)
		Page backPage = Page::Browse;
		bool backSettingsOver = false;
		int backSetGroup = -1;
		// the page's own: the query it keeps, the keyboard's text while it types, the cursor (in `selectable`)
		std::string query;
		bool typing = false;
		TextEdit edit;
		int cursor = 0;
		// the kind tab: All (the kinds under their headings), then one kind each
		enum Tab { T_ALL, T_MUSIC, T_VIDEOS, T_BOOKS, TABS };
		int tab = T_ALL;
		void setTab(int t, const Player& p);
		std::string stateCounts() const;	// "all/music/videos/books" found, for a scripted run's state line
		int pressTab = -1;	// a finger on a tab
		// a container result opened (an album's page, an artist's, a folder of Music's or of Videos'): Music's and
		// Videos' B ask comeBack first — true: that page is left as its view's top and the results show again, the
		// cursor on the row that was opened. The rail (Shell::openSection) and a new Search forget the visit
		enum Visit { VISIT_NONE, VISIT_ALBUM, VISIT_ARTIST, VISIT_MUSIC_FOLDER, VISIT_VIDEO_FOLDER };
		int visit = VISIT_NONE;
		int visitIndex = -1, visitView = 0;	// the album or artist; the view Music (Videos) was on before
		std::string visitPath;				// the folder
		bool comeBack(Player& p);
		bool visiting() const;	// the page shown is the one a result opened: its B says "Results"
		Uint32 kbSince = 0;
		int kbCursor = 0, kbPressed = -1;
		KeyPad pad;	// the keyboard's keys (keyboard.h): the field's keyboard's own, less #+=
		int pressRow = -1, pressEmpty = -1;	// a finger on a row, an empty state's key
		bool finger = false;	// a finger down on the list: its moves scroll it, its lift may choose
		bool fingerMoved = false, caught = false;	// (caught: it stopped a glide, and chooses nothing)
		// Recent searches: the last eight, kept in the state dir (not while incognito); Clear empties it
		std::vector<std::string> recent;
		bool recentLoaded = false;
		void loadRecent();
		void saveRecent() const;
		void remember(const std::string& q);

	private:
		// one line of the page: a section's heading, a result, a section's "See all" (under All), a recent one,
		// Clear, or a tab's "nothing here" line
		enum RowKind { HEAD, RESULT, MORE, RECENT, CLEAR, NONE };
		struct Row
		{
			RowKind kind = RESULT;
			int sec = 0;			// a result's, a heading's, a See all's section (lay::SearchSec)
			const Glyph* glyph = nullptr;
			// a file's path (a song, a video, a book: A opens it), a folder's; an album's first song (its art)
			std::string text, note, pill, path;
			int index = -1;			// an album's or an artist's place in the snapshot
			bool art = false;		// an album or an artist: a small art square where the others have their glyph
			int matchAt = -1, matchLen = 0;
			int count = 0;
		};
		mutable std::vector<Row> rows;		// built from the index (mutable: signature() may be the first to ask)
		mutable std::vector<int> selectable;	// indices into rows the cursor may rest on
		mutable uint32_t builtGen = 0xFFFFFFFFu;
		mutable std::string builtQuery;
		mutable bool built = false;
		mutable int builtRecent = -1, builtTab = -1;
		// what the query found, by section (lay::SearchSec; a tab's count is its sections' sum, All's all of them)
		mutable std::vector<Row> found[lay::SEARCH_SECS];
		void openResult(const Row& r, Player& p);	// A on a result: the file, or the container's page
		void seeAll(int sec, const Player& p);		// a See all row: the section's kind tab, the cursor on its first
		int foundCount(int t) const;
		void rebuild(const Player& p) const;
		void searchLibrary(const Player& p) const;	// fills `found` for the query
		// the page under the field: the tabs' strip (while a query shows results), the notice, the list — one
		// layout for the drawing and the touches
		std::vector<std::string> tabLabels() const;
		bool tabsShown(const Player& p) const;
		lay::SearchResults layout(const Player& p) const;
		std::string crumb(const Player& p) const;
		// the rows as a smooth list: every row (a group's heading too) one pitch tall, under the field and the notice
		// row while the index reads. listFor: the query the list was last shown for (a new one starts at the top)
		static constexpr int ROW_PITCH = lay::RESULT_ROW_H;
		SmoothList list{ui};
		std::string listFor = "\x01";
		lay::Box listView(const Player& p) const { return layout(p).list; }
		void drawRow(const Row& row, lay::Box box, bool at, bool pressed);
		// the field empty with no recent, or a query nothing matched: the empty state's page
		bool emptyPage(const Player& p) const;
		lay::EmptyState emptyLayout(const Player& p) const;
		std::vector<std::string> emptyLines(const Player& p) const;
		lay::Box body() const;
		lay::Keyboard keyboard(const lay::Box& body) const;
		void openKeyboard();
		void kbPressKey(int i);
		void moveCursor(int dir, int page);
		void ensureCursor(const Player& p, bool glide = true);
		void chooseRow(int i, Player& p);
		void clearRecent();
		void browseFiles(Player& p);
	};
}
