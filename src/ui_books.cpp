// Books (ui_books.h): the Reading now strip and the shelf, built from the readers' places store and the cards'
// Books and Ebooks folders. Everything is drawn inside lay::shell().body (the shell draws the rail, the header
// and the hints); the touches test the same Rects the drawing asked for.
#include "ui_books.h"
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
		for (auto& e : ui.docs->docPlaces->all())
			if (exists(e.path)) places.push_back(&e);
		// the places' books: the strip's newest five, and every one of them on the shelf too (a book opened from
		// Files or a server joins the shelf, as the mock-up's rule has it)
		std::set<std::string> placed;
		for (auto* e : places)
		{
			if (!placed.insert(e->path).second) continue;
			shelf.push_back({e->path, bookName(e->path), -1});
			if (reading.size() < 5) reading.push_back({e->path, bookName(e->path), bookProgress(*e)});
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
	}

	lay::BooksShelf Ui::Books::layout() const
	{
		Rect inner = booksInner();
		// with nothing reading, the strip's space is dropped and the shelf starts at the page's own top
		int drop = reading.empty() ? lay::LINE_H + lay::SHELF_HEAD_GAP + lay::COVER_CELL_H + lay::SHELF_GROUP_GAP : 0;
		return lay::booksShelf({inner.x, inner.y - drop, inner.w, inner.h + drop}, (int)reading.size(),
			(int)shelf.size(), shelfScroll);
	}

	// ---- the More sheet (X): what a book has ----

	void Ui::Books::openMoreSheet(const Shelf& book, Player&)
	{
		std::string path = book.path, label = book.name;
		ui.openMore([this, path, label](const Player&) {
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
			return m;
		});
	}

	// ---- the head (the shell asks) ----

	std::vector<Ui::Hint> Ui::Books::hintList(const Player&) const
	{
		return {{"A", "Open"}, {"X", "More"}, {"\xE2\x97\x80/\xE2\x96\xB6", "Page"}, {"B", "Home"}};
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
			return h;
		}
		h.title = "Books";
		h.more = true;
		// the crumb: how much and where from ("21 books · Books and Ebooks on both cards")
		h.crumb = std::to_string(shelf.size()) + (shelf.size() == 1 ? " book" : " books") +
			(fromWords.empty() ? "" : " \xC2\xB7 " + fromWords);
		return h;
	}

	// ---- the keys ----

	bool Ui::Books::key(Button b, Player& p)
	{
		int n = (int)(reading.size() + shelf.size());
		if (n == 0)
		{
			// an empty page: its two keys, and B Home
			switch (b)
			{
			case Button::A: ui.page = Page::Browse; ui.browse->openSection(TAB_LOCAL, p); break;
			case Button::Y: ui.openWeb(); break;
			case Button::B: ui.page = Page::Browse; ui.browse->enterFolder(HOMEDIR, p); break;
			default: break;
			}
			return false;
		}
		lay::BooksShelf sh = layout();
		int cols = sh.cols;
		switch (b)
		{
		case Button::Up: cursor = std::max(0, cursor - cols); break;
		case Button::Down: cursor = std::min(n - 1, cursor + cols); break;
		case Button::Left: cursor = std::max(0, cursor - 1); break;
		case Button::Right: cursor = std::min(n - 1, cursor + 1); break;
		case Button::A:
			if (cursor < (int)reading.size()) ui.openDocument(reading[cursor].path, p);
			else if (cursor - (int)reading.size() < (int)shelf.size())
				ui.openDocument(shelf[cursor - reading.size()].path, p);
			break;
		case Button::X:
			if (cursor < (int)reading.size()) openMoreSheet(reading[cursor], p);
			else if (cursor - (int)reading.size() < (int)shelf.size())
				openMoreSheet(shelf[cursor - reading.size()], p);
			break;
		case Button::B: ui.page = Page::Browse; ui.browse->enterFolder(HOMEDIR, p); break;
		default: break;
		}
		// the shelf's row in view (the strip never scrolls)
		int row = std::max(0, cursor - (int)reading.size()) / cols;
		shelfScroll = std::clamp(shelfScroll, std::max(0, row - (sh.shown - 1)), row);
		return false;
	}

	// ---- a finger ----

	bool Ui::Books::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		Rect inner = booksInner();
		lay::BooksShelf sh = layout();
		int cols = sh.cols;
		int n = (int)(reading.size() + shelf.size());
		if (pt.kind != Pointer::Down)
		{
			if (touch == T_NONE) return false;
			if (pt.kind == Pointer::Move)
			{
				if (!touchMoved && std::abs(y - touchY) > 6) touchMoved = true;
				if (touchMoved)
					shelfScroll = lay::dragScroll(scrollStart, touchY, y, lay::SHELF_CELL_H + lay::COVER_GAP,
						0, (int)((shelf.size() + cols - 1) / cols), sh.shown);
				return false;
			}
			int t = touch;
			bool moved = touchMoved;
			touch = T_NONE;
			touchMoved = false;
			if (moved || t == T_NONE) return false;
			switch (t)
			{
			case T_EMPTY1:
				if (emptyKey[0].hit(x, y)) { ui.page = Page::Browse; ui.browse->openSection(TAB_LOCAL, p); }
				return false;
			case T_EMPTY2:
				if (emptyKey[1].hit(x, y)) ui.openWeb();
				return false;
			default: break;
			}
			if (t == T_GROUND) return false;		// the ground: a drag was its only meaning
			if (t >= T_READ && t < T_SHELF && t - T_READ < (int)sh.reading.size())
			{
				if (sh.reading[t - T_READ].hit(x, y)) ui.openDocument(reading[t - T_READ].path, p);
				return false;
			}
			if (t >= T_SHELF && t - T_SHELF < (int)shelf.size())
			{
				int i = t - T_SHELF;
				if (sh.shelf[i].hit(x, y)) ui.openDocument(shelf[i].path, p);
				return false;
			}
			return false;
		}
		// the down: what is under the finger (the same Rects the drawing asked for)
		touch = T_NONE;
		touchMoved = false;
		touchY = y;
		scrollStart = shelfScroll;
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
		for (int i = 0; i < (int)sh.reading.size(); i++)
			if (sh.reading[i].hit(x, y)) { touch = T_READ + i; cursor = i; return false; }
		for (int i = sh.first * cols; i < (int)shelf.size() && i < (sh.first + sh.shown) * cols; i++)
			if (sh.shelf[i].hit(x, y)) { touch = T_SHELF + i; cursor = (int)reading.size() + i; return false; }
		touch = T_GROUND;		// the shelf's own ground: a drag still scrolls it
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
		auto caps = [&](const std::string& text, Rect box) {
			Kit::SectionLine line;
			line.caps = true;
			line.text = text;
			k.sectionLine(lay::sectionLine(box, false), line);
		};
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
			v.glyph2 = &G_WEB;
			v.pressed1 = touch == T_EMPTY1 && !touchMoved;
			v.pressed2 = touch == T_EMPTY2 && !touchMoved;
			v.hotkeys = p.settings.buttonHotkeys;
			k.emptyState(e, v);
			return;
		}
		lay::BooksShelf b = layout();
		if (!reading.empty())
		{
			caps("Reading now \xC2\xB7 " + std::to_string(reading.size()), b.readingHead);
			for (int i = 0; i < (int)b.reading.size(); i++)
			{
				Kit::Cover c;
				c.key = reading[i].name;
				c.title = reading[i].name;
				// the decoded cover over the drawn sleeve (a TXT's own cover, and until the answer)
				c.picture = [this, path = reading[i].path, key = reading[i].name](Rect r) {
					ui.drawBookCover(path, key, r, 5);
				};
				c.progress = reading[i].progress;
				c.focused = i == cursor;
				c.pressed = touch == T_READ + i && !touchMoved;
				k.coverTile(lay::coverTile(b.reading[i], true, reading[i].progress >= 0), c);
			}
		}
		caps("Shelf \xC2\xB7 " + std::to_string(shelf.size()), b.shelfHead);
		for (int i = b.first * b.cols; i < (int)shelf.size() && i < (b.first + b.shown) * b.cols; i++)
		{
			Kit::Cover c;
			c.key = shelf[i].name;
			c.title = shelf[i].name;
			// the shelf's cells carry no bar: the strip's do (a book in progress is in both)
			c.picture = [this, path = shelf[i].path, key = shelf[i].name](Rect r) {
				ui.drawBookCover(path, key, r, 5);
			};
			c.focused = (int)reading.size() + i == cursor;
			c.pressed = touch == T_SHELF + i && !touchMoved;
			k.coverTile(lay::coverTile(b.shelf[i], true, false), c);
		}
	}

	std::string Ui::Books::signature(const Player&) const
	{
		// everything the drawing shows that Ui::signature doesn't already hold (the doc places are read at the
		// cell: the key holds their front entries, so a page turned redraws the strip's bars), and the covers'
		// serial: one decoded, the cells draw it now
		char buf[96];
		snprintf(buf, sizeof buf, "|bk%d.%d.%d.%zu.%zu.%d", cursor, shelfScroll, touch, reading.size(), shelf.size(),
			ui.artSerial);
		return std::string(buf) + placesKey;
	}
}
