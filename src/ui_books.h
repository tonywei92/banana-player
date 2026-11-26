// Books, the rail's section (Page::Books; docs/redesign/src/screens.js: books, books-empty). An area of Ui
// (Ui::Area, docs/notes/ui-areas.md): three tabs, "Reading now" (the books in progress), "Catalogs" (OPDS: ui_catalogs.h) and "Shelf" (every book
// file on the cards — EPUB, PDF, TXT, CBZ and CBR: pagebook.h's documents and comics). L / R step the tabs; Books opens on
// Reading now while a book is in progress, else on the Shelf.
//
// Where the shelf comes from (the index holds songs and videos only): the readers' own places store
// (<state>/doc-places.conf, the doc places — every book opened from anywhere, newest first, with its place) and the
// folders named Books, Ebook and Ebooks at the top of each card (src/mediaindex.h's bookFolders/listBooks, the same
// walk as the index's). The two are merged, by name; the places' ones carry their progress. Every cover is the
// kit's drawn one (bookPlaceholder, keyed by the name) — the readers' own cover decoding is a later step.
//
// A opens the existing reader (Ui::openDocument): a text in the text viewer, a PDF, a comic or a pictures EPUB in
// the page reader, at the place kept; B from the reader comes back here (Docs::docFrom).
#pragma once
#include "ui.h"
#include "ui_shell.h"
#include "ui_kit.h"
#include "ui_smoothlist.h"
#include "mediaindex.h"

namespace bp
{
	struct Ui::Books : Ui::Area
	{
		explicit Books(Ui& u);
		~Books() override;

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Books; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		bool pointerDownLate() const override { return true; }
		void draw(const Player& p) override;
		void tick(Player& p) override;
		std::string signature(const Player& p) const override;

		// ---- the third tab, Catalogs (OPDS; ui_catalogs.h): its own state and handlers, handed everything while open ----
		struct Catalogs;
		std::unique_ptr<Catalogs> catalogs;
		bool onCatalogs() const { return tab == lay::BOOKS_CATALOGS; }

		// ---- the shelf ----
		// a book on it: its file, and the name shown (less the extension). progress: how far in (the doc places'
		// entry; -1 none) — only Reading now's cells carry one
		struct Shelf
		{
			std::string path, name;
			double progress = -1;
			long long size = 0;	// the file's, when the folders' walk listed it (0: opened from elsewhere)
		};
		// the lists are mutable: they are built at the draw (a const one), when a place moves, the cards change or
		// the page opens
		mutable std::vector<Shelf> reading;	// the doc places' books, newest first (Reading now)
		mutable std::vector<Shelf> shelf;	// every book on the cards: the places' and the folders', by name
		mutable std::string placesKey;	// the doc places as last built from (the front entries' words)
		mutable std::string fromWords;	// "Books and Ebooks on both cards", as the folders were last read
		mutable int rootsSeen = -1;		// the roots serial the shelf was built at
		void build() const;
		std::string placesNow() const;

		// ---- the tabs (lay::BooksTab) and the focus: one cursor over the open tab's books ----
		int tab = lay::BOOKS_SHELF;
		int pressTab = -1;	// the tab a finger went down on
		int cursor = 0;
		const std::vector<Shelf>& items() const { return tab == lay::BOOKS_READING ? reading : shelf; }
		const Shelf* focused() const;	// the book under the cursor (null: none)
		void setTab(int t);	// (a new tab starts at its first book)
		std::vector<std::string> tabLabels() const;	// "Reading now 3", "Shelf 21"
		void open(Player& p);		// the rail's way in: the first tab (lay::booksFirstTab)
		// the page's layout (the tabs' strip, the grid's area, the open tab's cell) — one place for the drawing and
		// the touches to ask
		lay::BooksTabs layout() const;

		// ---- the open tab's grid: a smooth list of rows of five (ui_smoothlist.h), as Music's Albums grid ----
		SmoothList list{ui};
		SmoothList* smoothList() override;
		struct ListGeom
		{
			Rect view{};
			int rowH = 0, count = 0, cols = 5;
		};
		ListGeom listGeom() const;
		void keepCursorInView(bool glide = true);	// the cursor's row: a short glide to it
		// the grid's row r: its cells inside the row's box (the focus ring's room around them)
		std::vector<lay::Box> shelfRow(Rect row, int r) const;
		// the More sheet (X): what a book has (Read from the start, Info, Show in folder)
		void openMoreSheet(const Shelf& book, Player& p);

		// ---- a finger ----
		// what it went down on: T_TAB a tab (pressTab), T_GROUND the grid's ground (a drag scrolls), T_EMPTY1 /
		// T_EMPTY2 the empty page's keys, T_CELL + i a cover (its index in the open tab's books)
		enum { T_NONE = -1, T_TAB = 6, T_GROUND = 7, T_EMPTY1 = 8, T_EMPTY2 = 9, T_CELL = 10 };
		int touch = T_NONE;
		int touchY = 0;
		bool touchMoved = false;
		bool onList = false;	// the finger drives the grid (it went down on it)
		bool caught = false;	// it went down on a gliding grid: it stopped it, and is no tap
		Rect emptyKey[2]{};

		// ---- the shell's header (Ui::Shell::head asks this) ----
		ShellHead head(const Player& p) const;
		std::vector<Hint> hintList(const Player& p) const;
		// (the file's own small helper, defined in ui_books.cpp: it names Kit, which is Ui's)
		void accentFirst(Kit::Empty& e, const std::vector<std::string>& lines, const char* word) const;
	};
}
