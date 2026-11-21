// Search (Page::Search), an area of Ui: the field, the cards' index's answers grouped by kind, and Recent searches.
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
		Uint32 kbSince = 0;
		int kbCursor = 0, kbPressed = -1, kbLayer = 0;
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
		// one line of the page: a group's heading, a result, a recent one, or Clear
		enum RowKind { HEAD, RESULT, RECENT, CLEAR };
		struct Row
		{
			RowKind kind = RESULT;
			const Glyph* glyph = nullptr;
			std::string text, note, pill, path;
			int matchAt = -1, matchLen = 0;
			int count = 0;
		};
		mutable std::vector<Row> rows;		// built from the index (mutable: signature() may be the first to ask)
		mutable std::vector<int> selectable;	// indices into rows the cursor may rest on
		mutable uint32_t builtGen = 0xFFFFFFFFu;
		mutable std::string builtQuery;
		mutable bool built = false;
		mutable int builtRecent = -1;
		void rebuild(const Player& p) const;
		std::string crumb(const Player& p) const;
		// the rows as a smooth list: every row (a group's heading too) one pitch tall, under the field and the notice
		// row while the index reads. listFor: the query the list was last shown for (a new one starts at the top)
		static constexpr int ROW_PITCH = lay::RESULT_ROW_H;
		SmoothList list{ui};
		std::string listFor = "\x01";
		lay::Box listView(const Player& p) const;
		lay::Box noticeBox(const Player& p) const;	// (w 0: none)
		void drawRow(const Row& row, lay::Box box, bool at, bool pressed);
		// the field empty with no recent, or a query nothing matched: the empty state's page
		bool emptyPage(const Player& p) const;
		lay::EmptyState emptyLayout(const Player& p) const;
		std::vector<std::string> emptyLines(const Player& p) const;
		lay::Box body() const;
		lay::Keyboard keyboard(const lay::Box& body) const;
		std::string keyboardNote(const Player& p) const;
		void openKeyboard();
		void kbPressKey(int i);
		int kbAt(int x, int y) const;
		void kbMove(int dx, int dy);
		int keyCount() const;
		void moveCursor(int dir, int page);
		void ensureCursor(const Player& p, bool glide = true);
		void chooseRow(int i, Player& p);
		void clearRecent();
		void browseFiles(Player& p);
	};
}
