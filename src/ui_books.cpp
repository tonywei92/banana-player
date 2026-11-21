// Books (ui_books.h): the Reading now and Shelf tabs, built from the readers' places store and the cards'
// Books and Ebooks folders. Everything is drawn inside lay::shell().body (the shell draws the rail, the header
// and the hints); the touches test the same Rects the drawing asked for.
#include "ui_books.h"
#include "pagebook.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_browse.h"
#include "ui_docs.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace bp
{
	namespace
	{
		// (the file-scope helpers name what they use in full: Ui's own names are private to it, and its nested
		// types need the Ui:: prefix here)
		lay::Box booksInner()
		{
			lay::Shell s = lay::shell();
			return {s.body.x + lay::SP12, s.body.y, s.body.w - 2 * lay::SP12, s.body.h};
		}

		std::string bookName(const std::string& path)
		{
			std::string n = path.substr(path.rfind('/') + 1);
			size_t dot = n.rfind('.');
			if (dot != std::string::npos && dot > 0) n.resize(dot);
			return n;
		}

		// how far into a book its doc place says (a text's entry carries the share in its duration; a PDF's or a
		// comic's a page of so many)
		double bookProgress(const ResumeEntry& e)
		{
			if (e.aid == 1) return e.duration >= 0 ? std::clamp(e.duration, 0.0, 1.0) : -1.0;
			if (e.aid == 2 && e.position > 0 && e.duration > 0) return std::clamp(e.position / e.duration, 0.0, 1.0);
			return -1;
		}

		// the empty page's sentence
		const char* EMPTY_BOOKS_LINE = "Books, PDFs and text files you put in a Books folder, or open from Files, "
			"show up here with your place kept.";
	}

	// ---- the shelf ----

	std::string Ui::Books::placesNow() const
	{
		// the doc places' front entries as one key: a place moved (a page turned, a book finished) rebuilds the
		// strip's bars and the shelf's order around it
		ui.docs->docPlacesLoad();
		std::string key;
		int n = 0;
		for (auto& e : ui.docs->docPlaces->all())
		{
			if (++n > 5) break;
			key += e.path + "|" + std::to_string(e.position) + "|" + std::to_string(e.aid) + "|" +
				std::to_string(e.duration) + ";";
		}
		return key;
	}

	void Ui::Books::build() const
	{
		reading.clear();
		shelf.clear();
		placesKey = placesNow();
		fromWords = mediaFromWords(bookFolders());
		// the readers' own places: every book opened from anywhere, newest first, its file still there
		ui.docs->docPlacesLoad();
		std::vector<const ResumeEntry*> places;
		// (a TXT is not a book: a readme or a log read once stays in Files and Recent, not on the shelf; the owner)
		for (auto& e : ui.docs->docPlaces->all())
			if (exists(e.path) && isBookName(e.path)) places.push_back(&e);
		// the places' books: Reading now, and every one of them on the shelf too (a book opened from Files or a
		// server joins the shelf, as the mock-up's rule has it)
		std::set<std::string> placed;
		for (auto* e : places)
		{
			if (!placed.insert(e->path).second) continue;
			shelf.push_back({e->path, bookName(e->path), -1});
			reading.push_back({e->path, bookName(e->path), bookProgress(*e)});
		}
		// the cards' Books and Ebook(s) folders (mediaindex's own walk), merged with the places', by name
		for (auto& b : listBooks(bookFolders()))
		{
			if (!placed.insert(b.path).second) continue;
			shelf.push_back({b.path, bookName(b.path), -1});
		}
		std::stable_sort(shelf.begin(), shelf.end(), [](const Shelf& a, const Shelf& b) {
			return naturalLess(a.name, b.name);
		});
	}

	void Ui::Books::open(Player& p)
	{
		// built when the page opens, if the places or the cards moved since it was built
		if (placesNow() != placesKey || ui.rootsSerial() != rootsSeen) build();
		(void)p;
		ui.page = Page::Books;
		// the rail's way in opens on Reading now while a book is in progress (back from a reader keeps the tab)
		int first = lay::booksFirstTab((int)reading.size());
		if (first != tab) setTab(first);
	}

	// ---- the tabs ----

	const Ui::Books::Shelf* Ui::Books::focused() const
	{
		const auto& v = items();
		return cursor >= 0 && cursor < (int)v.size() ? &v[cursor] : nullptr;
	}

	void Ui::Books::setTab(int t)
	{
		tab = std::clamp(t, 0, 1);
		cursor = 0;
		list.top();
	}

	std::vector<std::string> Ui::Books::tabLabels() const
	{
		return {"Reading now " + std::to_string(reading.size()), "Shelf " + std::to_string(shelf.size())};
	}

	lay::BooksTabs Ui::Books::layout() const
	{
		std::vector<int> widths;
		for (auto& l : tabLabels()) widths.push_back(ui.kit->tabW(l));
		return lay::booksTabs(lay::shell().body, widths, ui.kit->tabCapW("L/R"), tab == lay::BOOKS_READING);
	}

	// ---- the shelf's grid ----

	Ui::Books::ListGeom Ui::Books::listGeom() const
	{
		// the grid from under the tabs to the body's foot, SP6 of room around each row (a focused cover's ring
		// reaches 5 past it)
		ListGeom g;
		if (items().empty()) return g;
		lay::BooksTabs b = layout();
		Rect grid = b.page.content;
		g.cols = b.cols;
		g.view = {grid.x - lay::SP6, grid.y - lay::SP6, grid.w + 2 * lay::SP6, grid.h + lay::SP6};
		g.rowH = b.pitch;
		g.count = ((int)items().size() + g.cols - 1) / g.cols;
		return g;
	}

	std::vector<lay::Box> Ui::Books::shelfRow(Rect row, int r) const
	{
		lay::BooksTabs b = layout();
		int inRow = std::clamp((int)items().size() - r * b.cols, 0, b.cols);
		return lay::coverCells({row.x + lay::SP6, row.y + lay::SP6, row.w - 2 * lay::SP6, b.cellH}, inRow, b.cols, b.cellH);
	}

	void Ui::Books::keepCursorInView(bool glide)
	{
		ListGeom g = listGeom();
		if (g.count <= 0) return;
		list.reveal(cursor / g.cols, g.view, g.rowH, g.count, glide);
	}

	// ---- the More sheet (X): what a book has ----

	void Ui::Books::openMoreSheet(const Shelf& book, Player&)
	{
		std::string path = book.path, label = book.name;
		ui.openMore([this, path, label](const Player& pl) {
			MoreSpec m;
			m.glyph = &G_BOOK;
			m.title = label;
			m.rows.push_back({&G_REFRESH, "Read from the start", "", "", [this, path](Player& p) {
				ui.docs->forgetPlace(path);
				ui.openDocument(path, p);
				return false;
			}});
			m.rows.push_back({&G_INFO, "Info", "", "", [this, path](Player& p) { ui.openFileInfo(path, ""); return false; }});
			m.rows.push_back({&G_FOLDER, "Show in folder", "", "", [this, path](Player& p) {
				ui.page = Page::Browse;
				ui.showInFolder(path, p);
				return false;
			}});
			ui.moreTwoColumns(m, ui.sourceMoreRows(LibKind::Books, pl));
			return m;
		});
	}

	// ---- the head (the shell asks) ----

	std::vector<Ui::Hint> Ui::Books::hintList(const Player&) const
	{
		return {{"A", "Open"}, {"X", "More"}, {"L/R", "Tab"}, {"B", "Home"}};
	}

	void Ui::Books::accentFirst(Kit::Empty& e, const std::vector<std::string>& lines, const char* word) const
	{
		// the word a sentence of the empty state carries in ACCENT (the folder an empty page names)
		for (size_t i = 0; i < lines.size(); i++)
		{
			size_t at = lines[i].find(word);
			if (at != std::string::npos)
			{
				e.accent.push_back({(int)i, (int)at, (int)strlen(word)});
				return;
			}
		}
	}

	Ui::ShellHead Ui::Books::head(const Player& p) const
	{
		ShellHead h;
		h.back = ui.backIconShown(p);
		h.hints = hintList(p);
		if (reading.empty() && shelf.empty())
		{
			h.title = "Books";
			h.more = false;
			h.crumb = fromWords.empty() ? "No Books or Ebooks folder on either card" : mediaEmptyWords(bookFolders());
			return h;
		}
		h.title = "Books";
		h.more = true;
		// the crumb: where from and how much ("From Books and Ebooks on both cards · 21 books")
		std::string count = std::to_string(shelf.size()) + (shelf.size() == 1 ? " book" : " books");
		h.crumb = fromWords.empty() ? count : "From " + fromWords + " \xC2\xB7 " + count;
		return h;
	}

	// ---- the keys ----

	bool Ui::Books::key(Button b, Player& p)
	{
		int n = (int)(reading.size() + shelf.size());
		if (n == 0)
		{
			// an empty page: its two keys (B stops here: the section's home)
			switch (b)
			{
			case Button::A: ui.page = Page::Browse; ui.browse->openSection(TAB_LOCAL, p); break;
			case Button::Y: ui.openWeb(); break;
			default: break;
			}
			return false;
		}
		const int cols = layout().cols, count = (int)items().size();
		switch (b)
		{
		case Button::L: case Button::R: setTab(1 - tab); break;	// (two tabs: either key is the other)
		case Button::Up: cursor = std::max(0, cursor - cols); break;
		case Button::Down: cursor = std::max(0, std::min(count - 1, cursor + cols)); break;
		case Button::Left: cursor = std::max(0, cursor - 1); break;
		case Button::Right: cursor = std::max(0, std::min(count - 1, cursor + 1)); break;
		case Button::A:
			if (const Shelf* bk = focused()) ui.openDocument(bk->path, p);
			break;
		case Button::X:
			if (const Shelf* bk = focused()) openMoreSheet(*bk, p);
			break;
		default: break;
		}
		if (ui.page == Page::Books) keepCursorInView();
		return false;
	}

	// ---- a finger ----

	bool Ui::Books::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		Rect inner = booksInner();
		lay::BooksTabs l = layout();
		int n = (int)(reading.size() + shelf.size());
		const float fy = pt.y * Ui::H;	// (by the fraction: the grid follows the panel's pixels, not whole units)
		if (pt.kind != Pointer::Down)
		{
			if (touch == T_NONE) return false;
			if (pt.kind == Pointer::Move)
			{
				// the grid follows the finger once it is past the slop; elsewhere a drag only stops being a tap
				if (onList) { if (list.move(fy)) touchMoved = true; }
				else if (!touchMoved && std::abs(y - touchY) > 6) touchMoved = true;
				return false;
			}
			if (onList) list.lift();
			int t = touch, was = pressTab;
			bool moved = touchMoved || caught;
			touch = T_NONE;
			pressTab = -1;
			touchMoved = onList = caught = false;
			if (moved || t == T_NONE) return false;
			switch (t)
			{
			case T_EMPTY1:
				if (emptyKey[0].hit(x, y)) { ui.page = Page::Browse; ui.browse->openSection(TAB_LOCAL, p); }
				return false;
			case T_EMPTY2:
				if (emptyKey[1].hit(x, y)) ui.openWeb();
				return false;
			case T_TAB:
				if (lay::tabAt(l.page.strip, x, y) == was && was != tab) setTab(was);
				return false;
			default: break;
			}
			if (t == T_GROUND) return false;		// the ground: a drag was its only meaning
			if (t >= T_CELL && t - T_CELL < (int)items().size())
			{
				// still on the same cover (the row under the finger, the cell in it)
				int i = t - T_CELL, cols = l.cols, r = list.rowAt(x, y);
				if (r == i / cols)
				{
					std::vector<lay::Box> cells = shelfRow(list.rowBox(r), r);
					if (i % cols < (int)cells.size() && cells[i % cols].hit(x, y)) ui.openDocument(items()[i].path, p);
				}
			}
			return false;
		}
		// the down: what is under the finger (the same Rects the drawing asked for)
		touch = T_NONE;
		pressTab = -1;
		touchMoved = onList = caught = false;
		touchY = y;
		if (n == 0)
		{
			std::vector<std::string> lines = ui.wrapText(EMPTY_BOOKS_LINE, F_I14, 400, 99);
			lay::EmptyState e = lay::emptyState(inner, (int)lines.size(), 2);
			emptyKey[0] = e.key[0];
			emptyKey[1] = e.key[1];
			if (e.key[0].hit(x, y)) touch = T_EMPTY1;
			else if (e.key[1].hit(x, y)) touch = T_EMPTY2;
			return false;
		}
		pressTab = lay::tabAt(l.page.strip, x, y);
		if (pressTab >= 0) { touch = T_TAB; return false; }
		// the grid: anywhere on it drives it (a touch on a gliding grid only stops it)
		ListGeom g = listGeom();
		if (g.count <= 0 || !g.view.hit(x, y)) return false;
		onList = true;
		touch = T_GROUND;
		caught = list.press(fy);
		int r = caught ? -1 : list.rowAt(x, y);
		if (r < 0) return false;
		std::vector<lay::Box> cells = shelfRow(list.rowBox(r), r);
		for (int c = 0; c < (int)cells.size(); c++)
			if (cells[c].hit(x, y))
			{
				touch = T_CELL + r * g.cols + c;
				cursor = r * g.cols + c;
			}
		return false;
	}

	// ---- the drawing ----

	void Ui::Books::draw(const Player& p)
	{
		// a place moved (a page turned) or the cards changed: the shelf is built again
		if (placesNow() != placesKey || ui.rootsSerial() != rootsSeen)
		{
			rootsSeen = ui.rootsSerial();
			build();
		}
		Rect inner = booksInner();
		Kit& k = *ui.kit;
		int n = (int)(reading.size() + shelf.size());
		if (n == 0)
		{
			// nothing opened, nothing on the cards (the mock-up's books-empty): the two keys, the folder's name
			// in the kit's accent
			std::vector<std::string> lines = ui.wrapText(EMPTY_BOOKS_LINE, F_I14, 400, 99);
			lay::EmptyState e = lay::emptyState(inner, (int)lines.size(), 2);
			emptyKey[0] = e.key[0];
			emptyKey[1] = e.key[1];
			Kit::Empty v;
			v.glyph = &G_BOOK;
			v.title = "No books yet";
			v.lines = lines;
			accentFirst(v, lines, "Books");
			v.key1 = "Browse Files";
			v.glyph1 = &G_FOLDER;
			v.key2 = "Web dashboard";
			v.glyph2 = &G_QR;
			v.pressed1 = touch == T_EMPTY1 && !touchMoved;
			v.pressed2 = touch == T_EMPTY2 && !touchMoved;
			v.hotkeys = p.settings.buttonHotkeys;
			k.emptyState(e, v);
			return;
		}
		lay::BooksTabs l = layout();
		k.tabs(l.page.strip.area, tabLabels(), tab, "L/R", BG, pressTab);
		if (items().empty())
		{
			// a tab with nothing (no book opened yet, or none on the cards): a line where its covers go
			ui.drawTextMid(tab == lay::BOOKS_READING ? "Books you open show up here with your place kept."
				: "No book files in a Books or Ebooks folder yet.", F_I14, DIM, l.page.content.x,
				l.page.content.y + lay::SP24, l.page.content.w);
			return;
		}
		// the grid: a smooth list of rows of five, each row's look its key (each cover's state in it: its arrival
		// draws the row again). The covers' boxes in the key are the slot's, as draw lays them (a cover's cached
		// size is its device size, which at the handheld's 1.6x depends on where its box starts)
		ListGeom g = listGeom();
		SmoothList::Spec spec;
		spec.view = g.view;
		spec.rowH = g.rowH;
		spec.count = g.count;
		const int cols = g.cols;
		const bool bars = tab == lay::BOOKS_READING;	// Reading now's cells carry the progress bar
		auto coverOf = [this, bars](int i) {
			const Shelf& bk = items()[i];
			Kit::Cover c;
			c.key = bk.name;
			c.title = bk.name;
			// the decoded cover over the drawn sleeve (a TXT's own cover, and until the answer)
			c.picture = [this, path = bk.path, key = bk.name](Rect r) { ui.drawBookCover(path, key, r, 5); };
			if (bars) c.progress = bk.progress;
			c.focused = i == cursor;
			c.pressed = touch == T_CELL + i && !touchMoved;
			return c;
		};
		auto tileOf = [this, bars](lay::Box cell, int i) { return lay::coverTile(cell, true, bars && items()[i].progress >= 0); };
		const std::string gen = std::string(theme::drawingClean() ? "c" : "") + std::to_string(tab) + "|";
		spec.key = [this, coverOf, tileOf, gen, cols, slotBox = Rect{0, 0, g.view.w, g.rowH}](int r) {
			std::string key = "bk" + gen + std::to_string(r);
			std::vector<lay::Box> cells = shelfRow(slotBox, r);
			for (int c = 0; c < (int)cells.size(); c++)
			{
				int i = r * cols + c;
				Kit::Cover cell = coverOf(i);
				int art = ui.bookCoverState(items()[i].path, tileOf(cells[c], i).cover, 5);
				key += "." + items()[i].path + ":" + std::to_string(art) + ":" + std::to_string((int)(cell.progress * 1000)) +
					(cell.focused ? "f" : "") + (cell.pressed ? "d" : "");
			}
			return key;
		};
		spec.draw = [this, coverOf, tileOf, cols](int r, Rect box) {
			std::vector<lay::Box> cells = shelfRow(box, r);
			for (int c = 0; c < (int)cells.size(); c++)
				ui.kit->coverTile(tileOf(cells[c], r * cols + c), coverOf(r * cols + c));
		};
		list.draw(std::move(spec));
	}

	std::string Ui::Books::signature(const Player&) const
	{
		// everything the drawing shows that Ui::signature doesn't already hold (the doc places are read at the
		// cell: the key holds their front entries, so a page turned redraws the bars), and the covers' serial: one
		// decoded, the cells draw it now
		char buf[112];
		snprintf(buf, sizeof buf, "|bk%d.%d.%d.%d.%d.%zu.%zu.%d", tab, pressTab, cursor, touchMoved, touch, reading.size(),
			shelf.size(), ui.artSerial);
		return std::string(buf) + placesKey;
	}
}
