// Books, the rail's section (Page::Books; docs/redesign/src/screens.js: books, books-empty). An area of Ui
// (Ui::Area, docs/notes/ui-areas.md): a "Reading now" strip of the books in progress, and a shelf of every book
// file on the cards — EPUB, PDF, TXT, CBZ and CBR (pagebook.h's documents and comics).
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
#include "mediaindex.h"

namespace bp
{
	struct Ui::Books : Ui::Area
	{
		explicit Books(Ui& u) : Area(u) {}

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Books; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		bool pointerDownLate() const override { return true; }
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;

		// ---- the shelf ----
		// a book on it: its file, and the name shown (less the extension). progress: how far in (the doc places'
		// entry; -1 none) — only the reading strip's cells carry one
		struct Shelf
		{
			std::string path, name;
			double progress = -1;
		};
		// the lists are mutable: they are built at the draw (a const one), when a place moves, the cards change or
		// the page opens
		mutable std::vector<Shelf> reading;	// the doc places' books, newest first (the newest five are the strip)
		mutable std::vector<Shelf> shelf;	// every book on the cards: the places' and the folders', by name
		mutable std::string placesKey;	// the doc places as last built from (the front entries' words)
		mutable std::string fromWords;	// "Books and Ebooks on both cards", as the folders were last read
		mutable int rootsSeen = -1;		// the roots serial the shelf was built at
		void build() const;
		std::string placesNow() const;

		// ---- the focus: one cursor over the strip, then the shelf ----
		int cursor = 0, shelfScroll = 0;
		void open(Player& p);		// the rail's way in
		// the kit's own shelf layout (the strip, the grid, its scroll), with the strip's space dropped when
		// nothing is reading — one place for the drawing and the touches to ask
		lay::BooksShelf layout() const;
		// the More sheet (X): what a book has (Read from the start, Info, Show in folder)
		void openMoreSheet(const Shelf& book, Player& p);

		// ---- a finger ----
		// what it went down on: T_READ + i the strip's cell, T_SHELF + i a shelf cell, T_EMPTY1 / T_EMPTY2 the
		// empty page's keys
		enum { T_NONE = -1, T_READ = 0, T_SHELF = 10, T_GROUND = 499, T_EMPTY1 = 500, T_EMPTY2 = 501 };
		int touch = T_NONE;
		int touchY = 0, scrollStart = 0;
		bool touchMoved = false;
		Rect emptyKey[2]{};

		// ---- the shell's header (Ui::Shell::head asks this) ----
		ShellHead head(const Player& p) const;
		std::vector<Hint> hintList(const Player& p) const;
		// (the file's own small helper, defined in ui_books.cpp: it names Kit, which is Ui's)
		void accentFirst(Kit::Empty& e, const std::vector<std::string>& lines, const char* word) const;
	};
}
