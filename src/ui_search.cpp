// Search (Page::Search): the field, the library's answers under their kind tabs, and Recent searches. See
// docs/notes/2026-11-20-search-network-web.md and docs/redesign/src/screens.js (search, search-results).
#include "ui_search.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_browse.h"
#include "ui_books.h"
#include "mediaindex.h"
#include "format.h"
#include "playlist.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bp
{
	namespace
	{
		const int MAX_QUERY = 40;
		const char* const RECENT_FILE = "/search-recent";

		// the matched part of a name, in its own bytes (the folded forms have the same length for the Latin names
		// the cards hold; a match found only after folding is marked where it falls)
		void markMatch(const std::string& name, const std::string& query, int& at, int& len)
		{
			at = -1;
			len = 0;
			std::string fn = foldText(name), fq = foldText(query);
			if (fn.empty() || fq.empty()) return;
			size_t p = fn.find(fq);
			if (p == std::string::npos)
			{
				// every word of the query must be in it: mark the first that is
				size_t a = 0;
				while (a <= fq.size() && p == std::string::npos)
				{
					size_t e = fq.find(' ', a);
					std::string w = fq.substr(a, e == std::string::npos ? std::string::npos : e - a);
					if (!w.empty()) p = fn.find(w);
					if (e == std::string::npos) break;
					a = e + 1;
				}
			}
			if (p == std::string::npos) return;
			at = (int)std::min<size_t>(p, name.size());
			len = (int)std::min<size_t>(fq.size(), name.size() - at);
		}

		// the name a path is shown by (Files' rows' own name, an extension kept)
		std::string nameOf(const std::string& path)
		{
			return baseName(path);
		}

		// its folder, for a result's second line ("Movies", "Books")
		std::string folderOf(const std::string& path)
		{
			std::string p = parentFolder(path);
			std::string n = baseName(p);
			return n.empty() ? p : n;
		}
	}

	Ui::Search::Search(Ui& u) : Area(u)
	{
		pad.more = false;	// a name needs no brackets: Shift, 123, Space, Clear, Del, Done
	}

	lay::Box Ui::Search::body() const
	{
		lay::Shell s = lay::shell();
		return {s.body.x + 16, s.body.y + 4, s.body.w - 32, s.body.h - 8};
	}

	void Ui::Search::loadRecent()
	{
		if (recentLoaded) return;
		recentLoaded = true;
		if (ui.stateDirPath.empty()) return;
		FILE* f = fopen((ui.stateDirPath + RECENT_FILE).c_str(), "r");
		if (!f) return;
		char line[512];
		while (recent.size() < 8 && fgets(line, sizeof line, f))
		{
			std::string q = trimmed(line);
			if (!q.empty() && std::find(recent.begin(), recent.end(), q) == recent.end()) recent.push_back(q);
		}
		fclose(f);
	}

	void Ui::Search::saveRecent() const
	{
		if (ui.stateDirPath.empty()) return;
		FILE* f = fopen((ui.stateDirPath + RECENT_FILE).c_str(), "w");
		if (!f) return;
		for (auto& q : recent) fprintf(f, "%s\n", q.c_str());
		fclose(f);
	}

	void Ui::Search::remember(const std::string& q)
	{
		// incognito keeps nothing (the spec: recent searches are not gathered there)
		if (q.empty() || ui.incognito) return;
		auto it = std::find(recent.begin(), recent.end(), q);
		if (it != recent.end()) recent.erase(it);
		recent.insert(recent.begin(), q);
		if (recent.size() > 8) recent.resize(8);
		saveRecent();
		built = false;
	}

	void Ui::Search::clearRecent()
	{
		recent.clear();
		saveRecent();
		built = false;
		cursor = 0;
	}

	void Ui::Search::open(Player& p)
	{
		(void)p;
		if (!recentLoaded) loadRecent();
		if (ui.page == Page::Search)
		{
			// the page keeps its search: a second press edits the text
			openKeyboard();
			return;
		}
		backPage = ui.page;
		backSettingsOver = ui.settingsOver;
		backSetGroup = ui.setGroup;
		ui.page = Page::Search;
		ui.ctxOpen = false;
		ui.pressedBtn = -1;
		cursor = 0;
		// with nothing searched yet it opens on the keyboard; a kept query shows its results
		if (query.empty()) openKeyboard();
	}

	void Ui::Search::close(Player& p)
	{
		(void)p;
		typing = false;
		pressRow = pressEmpty = pressTab = -1;
		finger = false;
		ui.page = backPage;
		ui.settingsOver = backSettingsOver;
		ui.setGroup = backSetGroup;
		ui.pressedBtn = -1;
	}

	void Ui::Search::back(Player& p)
	{
		// the header's < and B on an empty field: the keyboard lets go of what was typed (the results as they
		// were); with nothing searched yet, or from the results, back to the page Search was opened from
		if (typing && !query.empty()) typed(edit.text, false);
		else close(p);
	}

	void Ui::Search::openKeyboard()
	{
		typing = true;
		edit.set(query);
		kbSince = SDL_GetTicks();
		kbCursor = 0;
		kbPressed = -1;
		pad.shift = false;
		pad.layer = 0;
		ui.pressedBtn = -1;
	}

	void Ui::Search::typed(const std::string& text, bool keep)
	{
		typing = false;
		kbPressed = -1;
		pressRow = pressEmpty = -1;
		if (!keep) return;
		query = trimmed(text);
		tab = T_ALL;	// a new search starts on everything it found
		cursor = 0;
		built = false;
		if (!query.empty()) remember(query);
	}

	int Ui::Search::foundCount(int t) const
	{
		if (t != T_ALL) return (int)found[t].size();
		return (int)(found[T_MUSIC].size() + found[T_VIDEOS].size() + found[T_BOOKS].size());
	}

	void Ui::Search::searchLibrary(const Player& p) const
	{
		(void)p;
		for (auto& f : found) f.clear();
		std::shared_ptr<const MediaSnapshot> snap = ui.mediaIndex ? ui.mediaIndex->snapshot() : nullptr;
		if (!snap) return;
		// the library only: the index's songs and videos, and the Books shelf (the Books and Ebook folders, and the
		// books that were opened). A file anywhere else is Files' to find
		if (ui.books->placesNow() != ui.books->placesKey || ui.rootsSerial() != ui.books->rootsSeen || ui.books->shelf.empty())
			ui.books->build();
		std::vector<std::string> names;
		for (auto& b : ui.books->shelf) names.push_back(b.name);
		SearchHits hits = mediaSearch(*snap, names, query);
		auto add = [&](int kind, const Glyph* g, const std::string& name, const std::string& note, const std::string& pill,
			const std::string& path) {
			Row r;
			r.kind = RESULT;
			r.glyph = g;
			r.text = name;
			r.note = note;
			r.pill = pill;
			r.path = path;
			markMatch(name, query, r.matchAt, r.matchLen);
			found[kind].push_back(r);
		};
		for (uint32_t i : hits.songs)
		{
			if (i >= snap->songs.size()) continue;
			const MediaSong& s = snap->songs[i];
			std::string name = snap->at(s.title);
			if (name.empty()) name = baseName(s.path);
			std::string note = snap->at(s.artist);
			const std::string& album = snap->at(s.album);
			if (!album.empty() && album != note) note += (note.empty() ? "" : " \xC2\xB7 ") + album;
			add(T_MUSIC, &G_NOTE, name, note, s.video ? "VIDEO" : "", s.path);
		}
		for (uint32_t i : hits.videos)
		{
			if (i >= snap->videos.size()) continue;
			const MediaVideo& v = snap->videos[i];
			add(T_VIDEOS, &G_FILM, nameOf(v.path), folderOf(v.path), "", v.path);
		}
		for (uint32_t i : hits.files)
		{
			if (i >= ui.books->shelf.size()) continue;
			const std::string& path = ui.books->shelf[i].path;
			add(T_BOOKS, &G_BOOK, nameOf(path), folderOf(path), "", path);
		}
	}

	void Ui::Search::rebuild(const Player& p) const
	{
		uint32_t gen = ui.mediaIndex ? ui.mediaIndex->generation() : 0;
		bool sameSearch = built && builtGen == gen && builtQuery == query;
		if (sameSearch && builtRecent == (int)recent.size() && builtTab == tab) return;
		built = true;
		builtGen = gen;
		builtQuery = query;
		builtRecent = (int)recent.size();
		builtTab = tab;
		rows.clear();
		selectable.clear();
		if (query.empty())
		{
			for (auto& f : found) f.clear();
			// the field empty: Recent searches (A runs one again), then Clear
			if (recent.empty()) return;
			Row h;
			h.kind = HEAD;
			h.text = "Recent searches";
			h.count = (int)recent.size();
			rows.push_back(h);
			for (auto& q : recent)
			{
				Row r;
				r.kind = RECENT;
				r.glyph = &G_CLOCK;
				r.text = q;
				r.path = q;
				selectable.push_back((int)rows.size());
				rows.push_back(r);
			}
			Row c;
			c.kind = CLEAR;
			c.glyph = &G_TRASH;
			c.text = "Clear recent searches";
			selectable.push_back((int)rows.size());
			rows.push_back(c);
			return;
		}
		if (!sameSearch) searchLibrary(p);
		// the tab's rows: All is every kind under its heading (one with nothing is left out), in the tabs' order;
		// a kind's own tab is its rows alone (the count is on the tab)
		static const char* const KIND[TABS] = {"All", "Music", "Videos", "Books"};
		for (int k = T_MUSIC; k < TABS; k++)
		{
			if ((tab != T_ALL && tab != k) || found[k].empty()) continue;
			if (tab == T_ALL)
			{
				Row h;
				h.kind = HEAD;
				h.text = KIND[k];
				h.count = (int)found[k].size();
				rows.push_back(h);
			}
			for (auto& r : found[k])
			{
				selectable.push_back((int)rows.size());
				rows.push_back(r);
			}
		}
		// a kind with nothing while another has: the tabs stay, and the list says so in a line
		if (rows.empty() && foundCount(T_ALL) > 0)
		{
			Row n;
			n.kind = NONE;
			n.text = std::string("Nothing in ") + KIND[tab] + " by that name";
			rows.push_back(n);
		}
	}

	std::vector<std::string> Ui::Search::tabLabels() const
	{
		static const char* const KIND[TABS] = {"All", "Music", "Videos", "Books"};
		std::vector<std::string> out;
		for (int t = 0; t < TABS; t++) out.push_back(std::string(KIND[t]) + " " + std::to_string(foundCount(t)));
		return out;
	}

	bool Ui::Search::tabsShown(const Player& p) const
	{
		// over a query's results (not over Recent searches, the keyboard or an empty page)
		return !typing && !query.empty() && !emptyPage(p);
	}

	lay::SearchResults Ui::Search::layout(const Player& p) const
	{
		rebuild(p);
		std::vector<int> widths;
		bool tabs = tabsShown(p);
		if (tabs)
			for (auto& l : tabLabels()) widths.push_back(ui.kit->tabW(l));
		// the notice: the index is still reading the cards, over what has been found so far
		bool reading = !query.empty() && !(ui.mediaIndex && ui.mediaIndex->generation() != 0);
		return lay::searchResults(body(), widths, ui.kit->tabCapW("L/R"), tabs, reading);
	}

	std::string Ui::Search::stateCounts() const
	{
		std::string out;
		for (int t = 0; t < TABS; t++) out += (t ? "/" : "") + std::to_string(foundCount(t));
		return out;
	}

	void Ui::Search::setTab(int t, const Player& p)
	{
		t = ((t % TABS) + TABS) % TABS;
		if (t == tab) return;
		tab = t;
		cursor = 0;
		rebuild(p);
	}

	std::string Ui::Search::crumb(const Player& p) const
	{
		if (typing)
		{
			if (!ui.mediaIndex || ui.mediaIndex->generation() == 0) return "Reading the cards";
			auto snap = ui.mediaIndex->snapshot();
			size_t n = snap ? snap->songs.size() + snap->videos.size() : 0;
			return "Music, Videos and Books \xC2\xB7 " + std::to_string(n) + " names";
		}
		if (query.empty()) return recent.empty() ? std::string("Music, Videos and Books") : std::string("Recent searches");
		rebuild(p);
		int n = foundCount(T_ALL);
		return "\xE2\x80\x9C" + query + "\xE2\x80\x9D \xC2\xB7 " + std::to_string(n) + " found" +
			(n ? " \xC2\xB7 Music, Videos and Books" : "");
	}

	Ui::ShellHead Ui::Search::head(const Player& p) const
	{
		ShellHead h;
		h.back = true;	// (the header's < is B: back to the page it was opened from)
		h.more = false;
		h.title = "Search library";	// (the media library only: Music, Videos, Books; Files' folders are Files')
		h.crumb = crumb(p);
		if (typing)
			h.hints = {{"A", "Type"}, {"B", edit.text.empty() ? "Close" : "Delete"}, {"L/R", "Move"}, {"START", "Done"}};
		else if (query.empty() && recent.empty())
			h.hints = {{"A", "Type"}, {"Y", "Browse Files"}, {"B", "Back"}};
		else if (emptyPage(p))
			h.hints = {{"A", "Try another"}, {"Y", "Browse Files"}, {"B", "Back"}};
		else if (query.empty())
			h.hints = {{"A", "Run it"}, {"Y", "Edit"}, {"B", "Back"}};
		else
			h.hints = {{"A", "Open"}, {"L/R", "Kind"}, {"Y", "Edit"}, {"B", "Back"}};
		return h;
	}

	bool Ui::Search::emptyPage(const Player& p) const
	{
		if (query.empty()) return recent.empty();
		rebuild(p);
		if (ui.mediaIndex && ui.mediaIndex->generation() == 0) return false;	// still reading: the notice row
		return foundCount(T_ALL) == 0;
	}

	std::vector<std::string> Ui::Search::emptyLines(const Player& p) const
	{
		(void)p;
		if (query.empty())
		{
			std::vector<std::string> out = ui.wrapText("Find a song, a video or a book in your library by any part of "
				"its name. A file outside the Music, Videos and Books folders is in Files.", F_I14, 400, 3);
			return out;
		}
		std::vector<std::string> out = ui.wrapText("Your library was searched: songs, videos and books. "
			"Files' other folders and network folders are not.", F_I14, 400, 3);
		return out;
	}

	lay::EmptyState Ui::Search::emptyLayout(const Player& p) const
	{
		std::vector<std::string> lines = emptyLines(p);
		return lay::emptyState(body(), (int)lines.size(), 2);
	}

	lay::Keyboard Ui::Search::keyboard(const lay::Box& b) const
	{
		// the kit's keyboard laid out inside the shell's body (the keys share its width, nothing fitted or
		// scaled): the same boxes the touches test, and the kit's drawing (Kit::keyboard) draws them
		return lay::keyboard(b, {}, KeyPad::rows(), pad.toggles());
	}

	void Ui::Search::kbPressKey(int i)
	{
		kbSince = SDL_GetTicks();
		if (pad.press(i, edit, MAX_QUERY)) typed(edit.text, true);
	}

	void Ui::Search::moveCursor(int dir, int page)
	{
		if (selectable.empty()) { cursor = 0; return; }
		int n = (int)selectable.size();
		if (dir) cursor = ((cursor + dir) % n + n) % n;
		else if (page) cursor = std::clamp(cursor + page * 5, 0, n - 1);
	}

	void Ui::Search::ensureCursor(const Player& p, bool glide)
	{
		rebuild(p);
		if (selectable.empty()) { cursor = 0; return; }
		cursor = std::clamp(cursor, 0, (int)selectable.size() - 1);
		lay::Box v = listView(p);
		list.reveal(selectable[cursor], v, ROW_PITCH, (int)rows.size(), glide);
	}

	void Ui::Search::chooseRow(int i, Player& p)
	{
		if (i < 0 || i >= (int)rows.size()) return;
		Row& r = rows[i];
		if (r.kind == CLEAR) { clearRecent(); return; }
		if (r.kind == RECENT)
		{
			query = r.path;
			tab = T_ALL;
			cursor = 0;
			built = false;
			remember(query);
			return;
		}
		if (r.kind == RESULT) ui.openFile(r.path, p);
	}

	void Ui::Search::browseFiles(Player& p)
	{
		close(p);
		ui.shell->openSection(Ui::Shell::Section::Files, p);
	}

	void Ui::Search::drawRow(const Row& row, lay::Box box, bool at, bool pressed)
	{
		Kit& k = *ui.kit;
		if (row.kind == NONE)
		{
			// a kind's tab with nothing in it: one quiet line where its first row would be
			ui.drawTextMid(row.text, F_I14, DIM, box.x + lay::SP12, box.y + ROW_PITCH / 2, box.w - 2 * lay::SP12);
			return;
		}
		if (row.kind == HEAD)
		{
			lay::Box r{box.x, box.y + ROW_PITCH - lay::LINE_H - lay::SEARCH_HEAD_GAP, box.w, lay::LINE_H};
			Kit::SectionLine s;
			s.text = lay::caps(row.text) + " \xC2\xB7 " + std::to_string(row.count);
			s.caps = true;
			k.sectionLine(lay::sectionLine(r, true), s);
			return;
		}
		if (row.kind == CLEAR)
		{
			Kit::Row kr;
			kr.glyph = row.glyph;
			kr.name = row.text;
			kr.focused = at;
			kr.pressed = pressed;
			k.row({box.x, box.y + ROW_PITCH - lay::SEARCH_CLEAR_H, box.w, lay::SEARCH_CLEAR_H}, kr);
			return;
		}
		Kit::Result rr;
		rr.glyph = row.glyph;
		rr.name = row.text;
		rr.note = row.note;
		rr.pill = row.pill;
		rr.matchAt = row.matchAt;
		rr.matchLen = row.matchLen;
		rr.focused = at;
		rr.pressed = pressed;
		k.resultRow(lay::resultRow({box.x, box.y, box.w, lay::RESULT_ROW_H}), rr);
	}

	void Ui::Search::draw(const Player& p)
	{
		Kit& k = *ui.kit;
		lay::Box b = body();
		if (typing)
		{
			lay::Keyboard l = keyboard(b);
			Kit::KeyboardView v;
			v.title = "Search library";
			v.value = edit.text;
			v.caret = edit.caret;
			v.caretOn = (SDL_GetTicks() - kbSince) % 1060 < 530;
			v.special = {};
			if (!ui.mediaIndex || ui.mediaIndex->generation() == 0)
				v.note = "Reading the cards";
			else
			{
				auto snap = ui.mediaIndex->snapshot();
				size_t n = snap ? snap->songs.size() + snap->videos.size() : 0;
				v.note = "Music, Videos and Books \xC2\xB7 " + std::to_string(n) + " names";
			}
			v.keys(pad, edit.text.empty());
			v.cursor = kbCursor;
			v.pressed = kbPressed;
			k.keyboard(l, v);
			return;
		}
		// the field (not focused: A or a tap opens the keyboard; Y edits)
		lay::SearchField f = lay::searchField({b.x, b.y, b.w, lay::SEARCH_FIELD_H});
		Kit::SearchFieldView fv;
		fv.value = query;
		fv.key = "A";
		fv.focused = false;
		k.searchField(f, fv);
		if (emptyPage(p))
		{
			lay::EmptyState e = emptyLayout(p);
			Kit::Empty empty;
			empty.glyph = &G_SEARCH;
			empty.title = query.empty() ? "Search your library" : "Nothing called \xE2\x80\x9C" + query + "\xE2\x80\x9D";
			empty.lines = emptyLines(p);
			empty.key1 = query.empty() ? "Type" : "Try another";
			empty.key2 = "Browse Files";
			empty.glyph1 = query.empty() ? &G_SEARCH : &G_PENCIL;
			empty.glyph2 = &G_FOLDER;
			empty.pressed1 = pressEmpty == 1;
			empty.pressed2 = pressEmpty == 2;
			k.emptyState(e, empty);
			return;
		}
		lay::SearchResults l = layout(p);
		// the kinds, each with what it found; L / R step them (the caps at the strip's ends)
		if (!l.strip.tab.empty()) k.tabs(l.strip.area, tabLabels(), tab, "L/R", BG, pressTab);
		if (l.notice.w > 0)
		{
			Kit::NoticeRow n;
			n.tone = Kit::NoticeTone::Working;
			n.glyph = &G_REFRESH;
			n.text = "Reading the cards";
			n.second = "Results fill in as it goes";
			k.notice(lay::notice({l.notice.x, l.notice.y, l.notice.w}, true, true, 0), n);
		}
		if (rows.empty()) return;
		// a new query starts at its top; the list come back (the page left, the theme changed): the cursor in view
		bool fresh = list.takeFresh();
		// (a new query, or another kind's tab: a list of its own)
		const std::string shownFor = query + "\x02" + std::to_string(tab);
		if (shownFor != listFor)
		{
			listFor = shownFor;
			list.top();
			fresh = true;
		}
		if (fresh) ensureCursor(p, false);
		int sel = selectable.empty() ? -1 : selectable[std::clamp(cursor, 0, (int)selectable.size() - 1)];
		// the rows (a snapshot: the lambdas may outlive this draw), each one's look its key
		auto shown = std::make_shared<const std::vector<Row>>(rows);
		int pressed = fingerMoved ? -1 : pressRow;
		SmoothList::Spec spec;
		spec.view = l.list;
		spec.rowH = ROW_PITCH;
		spec.count = (int)rows.size();
		spec.key = [shown, sel, pressed](int i) {
			const Row& r = (*shown)[i];
			return std::to_string((int)r.kind) + "|" + r.text + "|" + r.note + "|" + r.pill + "|" +
				std::to_string(r.matchAt) + "," + std::to_string(r.matchLen) + "," + std::to_string(r.count) + "," +
				std::to_string(i == sel) + std::to_string(i == pressed);
		};
		spec.draw = [this, shown, sel, pressed](int i, Rect box) { drawRow((*shown)[i], box, i == sel, i == pressed); };
		list.draw(std::move(spec));
	}

	std::string Ui::Search::signature(const Player& p) const
	{
		rebuild(p);
		char b[160];
		snprintf(b, sizeof b, "|sr%d,%d,%d,%d,%d,%d,%d,%d,%d,%d|", (int)backPage, (int)typing, cursor, (int)fingerMoved, kbCursor,
			kbPressed, pad.layer * 2 + pad.shift, pressRow * 10 + pressEmpty, tab, pressTab);
		std::string s = b;
		for (int t = T_MUSIC; t < TABS; t++) s += std::to_string(found[t].size()) + ",";	// the tabs' counts
		s += query + "|" + builtQuery + "|" + std::to_string(rows.size()) + "|" + std::to_string(builtGen);
		for (auto& q : recent) s += "|r" + q;
		if (typing) s += std::string("|k") + std::to_string((SDL_GetTicks() - kbSince) % 1060 < 530) + edit.text;
		return s;
	}

	bool Ui::Search::key(Button b, Player& p)
	{
		if (typing)
		{
			switch (b)
			{
			// the keys are the field's keyboard's (ui_keyboard.cpp), button for button: each bottom key's cap says its own
			case Button::A: kbPressKey(kbCursor); break;
			// B deletes; with nothing left to delete it closes (the results as they were)
			case Button::B:
				if (edit.text.empty()) back(p);
				else kbPressKey(pad.keyOf(KeyPad::Delete));
				break;
			case Button::X: kbPressKey(pad.keyOf(KeyPad::Space)); break;
			case Button::Y: kbPressKey(pad.keyOf(KeyPad::Shift)); break;
			case Button::L2: kbPressKey(pad.keyOf(KeyPad::Clear)); break;
			case Button::L3: case Button::Select: pad.stepLayer(); break;
			case Button::Start: typed(edit.text, true); break;
			case Button::Up: kbCursor = lay::keyboardMove(keyboard(body()), kbCursor, 0, -1); break;
			case Button::Down: kbCursor = lay::keyboardMove(keyboard(body()), kbCursor, 0, 1); break;
			case Button::Left: kbCursor = lay::keyboardMove(keyboard(body()), kbCursor, -1, 0); break;
			case Button::Right: kbCursor = lay::keyboardMove(keyboard(body()), kbCursor, 1, 0); break;
			case Button::L: edit.left(); break;
			case Button::R: edit.right(); break;
			default: break;
			}
			return false;
		}
		rebuild(p);
		if (b == Button::B) { back(p); return false; }
		if (b == Button::Start) { ui.openSettings(p, true); return false; }
		bool empty = emptyPage(p);
		if (b == Button::Y)
		{
			if (empty) browseFiles(p);
			else openKeyboard();
			return false;
		}
		if (b == Button::A)
		{
			if (empty) { if (query.empty()) openKeyboard(); else openKeyboard(); return false; }
			if (selectable.empty()) { openKeyboard(); return false; }
			chooseRow(selectable[cursor], p);
			return false;
		}
		if (b == Button::Up) { moveCursor(-1, 0); ensureCursor(p); }
		else if (b == Button::Down) { moveCursor(1, 0); ensureCursor(p); }
		else if (b == Button::Left) { moveCursor(0, -1); ensureCursor(p); }
		else if (b == Button::Right) { moveCursor(0, 1); ensureCursor(p); }
		// L / R: the kind before or after, round (only over results: Recent searches have no tabs)
		else if ((b == Button::L || b == Button::R) && tabsShown(p)) setTab(tab + (b == Button::R ? 1 : -1), p);
		else if (b == Button::L3) openKeyboard();
		return false;
	}

	bool Ui::Search::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		lay::Box b = body();
		if (pt.kind == Pointer::Down)
		{
			pressRow = pressEmpty = pressTab = -1;
			if (typing)
			{
				kbSince = SDL_GetTicks();
				kbPressed = lay::keyboardHit(keyboard(b), x, y);
				if (kbPressed >= 0) kbCursor = kbPressed;
				return false;
			}
			lay::SearchField f = lay::searchField({b.x, b.y, b.w, lay::SEARCH_FIELD_H});
			if (f.box.hit(x, y)) { pressEmpty = 3; return false; }
			if (emptyPage(p))
			{
				lay::EmptyState e = emptyLayout(p);
				for (int i = 0; i < e.keys; i++)
					if (e.key[i].hit(x, y)) pressEmpty = i + 1;
				return false;
			}
			// a kind's tab (the strip the drawing asked for)
			lay::SearchResults l = layout(p);
			pressTab = lay::tabAt(l.strip, x, y);
			if (pressTab >= 0) return false;
			// the list: a touch on a gliding one only stops it; else the row under the finger (a heading: none)
			lay::Box v = l.list;
			if (rows.empty() || !v.hit(x, y)) return false;
			finger = true;
			fingerMoved = false;
			caught = list.press(pt.y * Ui::H);
			int r = caught ? -1 : list.rowAt(x, y);
			if (r >= 0 && r < (int)rows.size() && rows[r].kind != HEAD && rows[r].kind != NONE)
			{
				pressRow = r;
				for (size_t j = 0; j < selectable.size(); j++)
					if (selectable[j] == r) cursor = (int)j;
			}
			return false;
		}
		if (typing)
		{
			if (pt.kind == Pointer::Up)
			{
				int was = kbPressed;
				kbPressed = -1;
				if (was >= 0 && lay::keyboardHit(keyboard(b), x, y) == was) kbPressKey(was);
			}
			return false;
		}
		if (pressTab >= 0)
		{
			// the lift on the tab it went down on: that kind
			if (pt.kind == Pointer::Up)
			{
				int was = pressTab;
				pressTab = -1;
				if (lay::tabAt(layout(p).strip, x, y) == was) setTab(was, p);
			}
			return false;
		}
		if (finger)
		{
			// the list follows the finger past the slop (the press's look let go); the lift glides, or chooses
			if (pt.kind == Pointer::Move)
			{
				if (list.move(pt.y * Ui::H)) { fingerMoved = true; pressRow = -1; }
				return false;
			}
			if (pt.kind == Pointer::Up)
			{
				list.lift();
				finger = false;
				int was = pressRow;
				pressRow = -1;
				if (was >= 0 && !fingerMoved && !caught) chooseRow(was, p);
				fingerMoved = caught = false;
			}
			return false;
		}
		if (pt.kind == Pointer::Up && pressEmpty)
		{
			int was = pressEmpty;
			pressEmpty = -1;
			if (was == 3)
			{
				lay::SearchField f = lay::searchField({b.x, b.y, b.w, lay::SEARCH_FIELD_H});
				if (f.box.hit(x, y)) openKeyboard();
			}
			else if (was == 1)
			{
				if (emptyPage(p) && emptyLayout(p).key[0].hit(x, y)) openKeyboard();
			}
			else if (was == 2 && emptyPage(p) && emptyLayout(p).key[1].hit(x, y)) browseFiles(p);
			return false;
		}
		pressEmpty = -1;
		return false;
	}
}
