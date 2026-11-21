// Search (Page::Search): the field, the cards' index's answers grouped by kind, and Recent searches. See
// docs/notes/2026-11-20-search-network-web.md and docs/redesign/src/screens.js (search, search-results).
#include "ui_search.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_browse.h"
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
		const char* const LETTERS = "qwertyuiopasdfghjklzxcvbnm";
		const char* const DIGITS = "1234567890!@#$%^&*(-_=+[]{}";
		const int COUNTS[3] = {10, 9, 7};
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
		pressRow = pressEmpty = -1;
		finger = false;
		ui.page = backPage;
		ui.settingsOver = backSettingsOver;
		ui.setGroup = backSetGroup;
		ui.pressedBtn = -1;
	}

	void Ui::Search::openKeyboard()
	{
		typing = true;
		edit.set(query);
		kbSince = SDL_GetTicks();
		kbCursor = 0;
		kbPressed = -1;
		kbLayer = 0;
		ui.pressedBtn = -1;
	}

	void Ui::Search::typed(const std::string& text, bool keep)
	{
		typing = false;
		kbPressed = -1;
		pressRow = pressEmpty = -1;
		if (!keep) return;
		query = trimmed(text);
		cursor = 0;
		built = false;
		if (!query.empty()) remember(query);
	}

	void Ui::Search::rebuild(const Player& p) const
	{
		uint32_t gen = ui.mediaIndex ? ui.mediaIndex->generation() : 0;
		if (built && builtGen == gen && builtQuery == query && builtRecent == (int)recent.size()) return;
		built = true;
		builtGen = gen;
		builtQuery = query;
		builtRecent = (int)recent.size();
		rows.clear();
		selectable.clear();
		if (query.empty())
		{
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
		std::shared_ptr<const MediaSnapshot> snap = ui.mediaIndex ? ui.mediaIndex->snapshot() : nullptr;
		// the files part of the query: documents and pictures in the history and in the folder the browser shows
		// (the index holds songs and videos; a file elsewhere is found as any card file is)
		std::vector<std::string> names, paths;
		auto addFile = [&](const std::string& path) {
			if (path.empty() || isNetPath(path) || isMediaFile(path)) return;
			for (auto& x : paths) if (x == path) return;
			paths.push_back(path);
			names.push_back(baseName(path));
		};
		for (auto& h : p.history().items()) addFile(h);
		for (auto& path : ui.browse->browsePaths) addFile(path);
		if (!snap) return;
		SearchHits hits = mediaSearch(*snap, names, query);
		auto head = [&](const char* name, int n) {
			if (n <= 0) return;
			Row h;
			h.kind = HEAD;
			h.text = name;
			h.count = n;
			rows.push_back(h);
		};
		auto add = [&](RowKind kind, const Glyph* g, const std::string& name, const std::string& note, const std::string& pill,
			const std::string& path) {
			Row r;
			r.kind = kind;
			r.glyph = g;
			r.text = name;
			r.note = note;
			r.pill = pill;
			r.path = path;
			markMatch(name, query, r.matchAt, r.matchLen);
			selectable.push_back((int)rows.size());
			rows.push_back(r);
		};
		head("Videos", (int)hits.videos.size());
		for (uint32_t i : hits.videos)
		{
			if (i >= snap->videos.size()) continue;
			const MediaVideo& v = snap->videos[i];
			add(RESULT, &G_FILM, nameOf(v.path), folderOf(v.path), "", v.path);
		}
		head("Music", (int)hits.songs.size());
		for (uint32_t i : hits.songs)
		{
			if (i >= snap->songs.size()) continue;
			const MediaSong& s = snap->songs[i];
			std::string name = snap->at(s.title);
			if (name.empty()) name = baseName(s.path);
			std::string note = snap->at(s.artist);
			const std::string& album = snap->at(s.album);
			if (!album.empty() && album != note) note += (note.empty() ? "" : " \xC2\xB7 ") + album;
			add(RESULT, &G_NOTE, name, note, s.video ? "VIDEO" : "", s.path);
		}
		head("Files", (int)hits.files.size());
		for (uint32_t i : hits.files)
		{
			if (i >= paths.size()) continue;
			add(RESULT, &G_FILE, nameOf(paths[i]), folderOf(paths[i]), "", paths[i]);
		}
	}

	std::string Ui::Search::crumb(const Player& p) const
	{
		if (typing)
		{
			if (!ui.mediaIndex || ui.mediaIndex->generation() == 0) return "Reading the cards";
			auto snap = ui.mediaIndex->snapshot();
			size_t n = snap ? snap->songs.size() + snap->videos.size() : 0;
			return "Both cards \xC2\xB7 " + std::to_string(n) + " names";
		}
		if (query.empty()) return recent.empty() ? std::string("Both cards") : std::string("Recent searches");
		rebuild(p);
		int n = 0;
		for (auto& r : rows) if (r.kind == RESULT) n++;
		return "\xE2\x80\x9C" + query + "\xE2\x80\x9D \xC2\xB7 " + std::to_string(n) + (n == 1 ? " found" : " found") +
			(n ? " \xC2\xB7 both cards" : "");
	}

	Ui::ShellHead Ui::Search::head(const Player& p) const
	{
		ShellHead h;
		h.back = true;	// (the header's < is B: back to the page it was opened from)
		h.more = false;
		h.title = "Search";
		h.crumb = crumb(p);
		if (typing)
			h.hints = {{"A", "Type"}, {"X", "Delete"}, {"Y", "Space"}, {"START", "Done"}, {"B", "Close"}};
		else if (query.empty() && recent.empty())
			h.hints = {{"A", "Type"}, {"Y", "Browse Files"}, {"B", "Back"}};
		else if (emptyPage(p))
			h.hints = {{"A", "Try another"}, {"Y", "Browse Files"}, {"B", "Back"}};
		else
			h.hints = {{"A", query.empty() ? "Run it" : "Open"}, {"Y", "Edit"}, {"B", "Back"}};
		return h;
	}

	bool Ui::Search::emptyPage(const Player& p) const
	{
		if (query.empty()) return recent.empty();
		rebuild(p);
		if (ui.mediaIndex && ui.mediaIndex->generation() == 0) return false;	// still reading: the notice row
		for (auto& r : rows) if (r.kind == RESULT) return false;
		return true;
	}

	std::vector<std::string> Ui::Search::emptyLines(const Player& p) const
	{
		(void)p;
		if (query.empty())
		{
			std::vector<std::string> out = ui.wrapText("Find a song, a video, a book or a file on the cards by any part of "
				"its name. The index reads the cards in the background.", F_I14, 400, 3);
			return out;
		}
		std::vector<std::string> out = ui.wrapText("Names on both cards were searched: files, songs and videos. "
			"Network folders are not searched.", F_I14, 400, 3);
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
		return lay::keyboard(b, {}, {COUNTS[0], COUNTS[1], COUNTS[2]}, 2);
	}

	std::string Ui::Search::keyboardNote(const Player& p) const
	{
		(void)p;
		if (!ui.mediaIndex || ui.mediaIndex->generation() == 0) return "Reading the cards";
		auto snap = ui.mediaIndex->snapshot();
		size_t n = snap ? snap->songs.size() + snap->videos.size() : 0;
		return "Both cards \xC2\xB7 " + std::to_string(n) + " names";
	}

	int Ui::Search::keyCount() const
	{
		return COUNTS[0] + COUNTS[1] + COUNTS[2] + 5;
	}

	int Ui::Search::kbAt(int x, int y) const
	{
		lay::Keyboard k = keyboard(body());
		int i = 0;
		for (auto& row : k.rows)
			for (auto& r : row)
			{
				if (r.hit(x, y)) return i;
				i++;
			}
		for (auto& r : k.bottom)
		{
			if (r.hit(x, y)) return i;
			i++;
		}
		return -1;
	}

	void Ui::Search::kbMove(int dx, int dy)
	{
		int starts[4] = {0, COUNTS[0], COUNTS[0] + COUNTS[1], COUNTS[0] + COUNTS[1] + COUNTS[2]};
		int sizes[4] = {COUNTS[0], COUNTS[1], COUNTS[2], 5};
		lay::Keyboard k = keyboard(body());
		auto centre = [&](int idx) {
			int i = 0;
			for (auto& row : k.rows)
				for (auto& r : row) { if (i == idx) return r.x + r.w / 2; i++; }
			for (auto& r : k.bottom) { if (i == idx) return r.x + r.w / 2; i++; }
			return 0;
		};
		int g = 0;
		while (g < 3 && kbCursor >= starts[g + 1]) g++;
		int at = std::clamp(kbCursor - starts[g], 0, sizes[g] - 1);
		if (dx)
		{
			at = ((at + dx) % sizes[g] + sizes[g]) % sizes[g];
			kbCursor = starts[g] + at;
			return;
		}
		if (!dy) return;
		int ng = std::clamp(g + dy, 0, 3), cx = centre(kbCursor), best = 0, bestD = 1 << 30;
		for (int i = 0; i < sizes[ng]; i++)
		{
			int d = std::abs(centre(starts[ng] + i) - cx);
			if (d < bestD) { bestD = d; best = i; }
		}
		kbCursor = starts[ng] + best;
	}

	void Ui::Search::kbPressKey(int i)
	{
		if (i < 0 || i >= keyCount()) return;
		kbSince = SDL_GetTicks();
		int letters = COUNTS[0] + COUNTS[1] + COUNTS[2];
		if (i < letters)
		{
			if (edit.text.size() < (size_t)MAX_QUERY)
			{
				char c = (kbLayer == 0 ? LETTERS : DIGITS)[i];
				edit.insert(std::string(1, c));
			}
			return;
		}
		switch (i - letters)
		{
		case 0: case 1: kbLayer = i - letters; break;
		case 2: if (edit.text.size() < (size_t)MAX_QUERY) edit.insert(" "); break;
		case 3: edit.backspace(); break;
		case 4: typed(edit.text, true); break;
		default: break;
		}
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

	lay::Box Ui::Search::noticeBox(const Player& p) const
	{
		if (query.empty() || (ui.mediaIndex && ui.mediaIndex->generation() != 0)) return {0, 0, 0, 0};
		lay::Box b = body();
		// the index is still reading the cards: the notice row over what has been found so far
		return lay::notice({b.x, b.y + lay::SEARCH_FIELD_H + lay::SEARCH_GROUP_GAP, b.w}, true, true, 0).row;
	}

	lay::Box Ui::Search::listView(const Player& p) const
	{
		// from just under the field (or the notice) to the body's foot; a group's heading sits at the foot of its
		// row's pitch, so the first one lands a gap under the field
		lay::Box b = body(), n = noticeBox(p);
		int top = n.w > 0 ? n.y + n.h + lay::SP4 : b.y + lay::SEARCH_FIELD_H + lay::SP4;
		return {b.x, top, b.w, std::max(0, b.y + b.h - top)};
	}

	void Ui::Search::drawRow(const Row& row, lay::Box box, bool at, bool pressed)
	{
		Kit& k = *ui.kit;
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
			v.title = "Search";
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
				v.note = "Both cards \xC2\xB7 " + std::to_string(n) + " names";
			}
			const char* rows[3] = {LETTERS, LETTERS + 10, LETTERS + 19};
			const char* drows[3] = {DIGITS, DIGITS + 10, DIGITS + 19};
			for (int r = 0; r < 3; r++)
			{
				std::vector<std::string> line;
				for (int i = 0; i < COUNTS[r]; i++) line.emplace_back(1, (kbLayer == 0 ? rows[r] : drows[r])[i]);
				v.rows.push_back(line);
			}
			v.layers = {"abc", "123"};
			v.bottomCaps = {"L3", "SELECT", "Y", "X", "START"};
			v.cursor = kbCursor;
			v.pressed = kbPressed;
			v.layerOn = kbLayer;
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
			empty.title = query.empty() ? "Search your cards" : "Nothing called \xE2\x80\x9C" + query + "\xE2\x80\x9D";
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
		rebuild(p);
		lay::Box nb = noticeBox(p);
		if (nb.w > 0)
		{
			Kit::NoticeRow n;
			n.tone = Kit::NoticeTone::Working;
			n.glyph = &G_REFRESH;
			n.text = "Reading the cards";
			n.second = "Results fill in as it goes";
			k.notice(lay::notice({b.x, nb.y, b.w}, true, true, 0), n);
		}
		if (rows.empty()) return;
		// a new query starts at its top; the list come back (the page left, the theme changed): the cursor in view
		bool fresh = list.takeFresh();
		if (query != listFor)
		{
			listFor = query;
			list.top();
			fresh = true;
		}
		if (fresh) ensureCursor(p, false);
		int sel = selectable.empty() ? -1 : selectable[std::clamp(cursor, 0, (int)selectable.size() - 1)];
		// the rows (a snapshot: the lambdas may outlive this draw), each one's look its key
		auto shown = std::make_shared<const std::vector<Row>>(rows);
		int pressed = fingerMoved ? -1 : pressRow;
		SmoothList::Spec spec;
		spec.view = listView(p);
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
		snprintf(b, sizeof b, "|sr%d,%d,%d,%d,%d,%d,%d,%d|", (int)backPage, (int)typing, cursor, (int)fingerMoved, kbCursor,
			kbPressed, kbLayer, pressRow * 10 + pressEmpty);
		std::string s = b;
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
			case Button::A: kbPressKey(kbCursor); break;
			// close: the results as they were; with nothing searched yet, back to the page it was opened from
			case Button::B: if (query.empty() && edit.text.empty()) close(p); else typed(edit.text, false); break;
			case Button::X: edit.backspace(); kbSince = SDL_GetTicks(); break;
			case Button::Y: if (edit.text.size() < (size_t)MAX_QUERY) edit.insert(" "); break;
			case Button::Start: typed(edit.text, true); break;
			case Button::L3: case Button::Select: kbLayer = kbLayer ? 0 : 1; break;
			case Button::Up: kbMove(0, -1); break;
			case Button::Down: kbMove(0, 1); break;
			case Button::Left: kbMove(-1, 0); break;
			case Button::Right: kbMove(1, 0); break;
			case Button::L: edit.left(); break;
			case Button::R: edit.right(); break;
			default: break;
			}
			return false;
		}
		rebuild(p);
		if (b == Button::B) { close(p); return false; }
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
		else if (b == Button::L3) openKeyboard();
		return false;
	}

	bool Ui::Search::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		lay::Box b = body();
		if (pt.kind == Pointer::Down)
		{
			pressRow = pressEmpty = -1;
			if (typing)
			{
				kbSince = SDL_GetTicks();
				kbPressed = kbAt(x, y);
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
			// the list: a touch on a gliding one only stops it; else the row under the finger (a heading: none)
			lay::Box v = listView(p);
			if (rows.empty() || !v.hit(x, y)) return false;
			finger = true;
			fingerMoved = false;
			caught = list.press(pt.y * Ui::H);
			int r = caught ? -1 : list.rowAt(x, y);
			if (r >= 0 && r < (int)rows.size() && rows[r].kind != HEAD)
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
				if (was >= 0 && kbAt(x, y) == was) kbPressKey(was);
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
