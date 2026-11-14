// Files' document viewers, the bottom panel's side: a TXT or a book of text (EPUB) on main's TextReader
// (Page::DocText), and a PDF or a comic EPUB on its MangaReader (Page::DocPages), read a page at a time with the photo
// viewer's pan area, or down a strip (fullscreen across both panels). Each document's place is kept in
// doc-places.conf (the state folder), so it opens where it was left.
#include "ui_internal.h"
#include "epub.h"
#include "pagebook.h"
#include "substyle.h"
#include "textreader.h"
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <fstream>
#include <sstream>

namespace bp
{
	namespace
	{
		std::string extLower(const std::string& path)
		{
			size_t dot = path.rfind('.'), slash = path.rfind('/');
			if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
			std::string e = path.substr(dot + 1);
			for (char& c : e) c = (char)std::tolower((unsigned char)c);
			return e;
		}

		std::string fileName(const std::string& path)
		{
			size_t s = path.rfind('/');
			return s == std::string::npos ? path : path.substr(s + 1);
		}

		const char* FONT_NAMES[TXT_FONTS] = {"Serif", "Sans", "Mono", "Pixel", "Pixel CJK"};
		const char* SIZE_NAMES[TXT_SIZES] = {"Small", "Medium", "Large", "Larger", "Largest"};
		const char* SPACING_NAMES[TXT_SPACINGS] = {"Tight", "Normal", "Loose"};
		const char* MARGIN_NAMES[TXT_MARGINS] = {"Narrow", "Normal", "Wide"};
		const char* THEME_NAMES[TXT_THEMES] = {"Dark", "Light", "Sepia", "Black"};
		const char* FILTER_NAMES[DOC_FILTERS] = {"Off", "Warm", "Warmer", "Dim"};

		// the menus' rows
		enum { DT_FONT, DT_SIZE, DT_SPACING, DT_MARGINS, DT_THEME, DT_FILTER, DT_ENCODING, DT_GOTO, DP_VIEW, DP_FULL, DP_FILTER,
			DP_FIT, DP_ZOOM, DP_PAGES, DT_VIEW, DT_HOLD, DT_MARKS, DT_TURN, DT_FIND };
		constexpr int MENU_TOP = 72, MENU_ROW_H = 44;
		constexpr Uint32 TAP_MS = 350;

		// the reading filter as SDL's colour mod (the bottom panel's copies of the pages)
		void tintTexture(SDL_Texture* t, int filter)
		{
			float r, g, b;
			docFilterTint(filter, r, g, b);
			SDL_SetTextureColorMod(t, (Uint8)(r * 255), (Uint8)(g * 255), (Uint8)(b * 255));
		}
	}

	// a Find under way (below: Find)
	struct Ui::FindJob
	{
		std::string path, charset, query;
		bool epub = false, back = false;
		uint64_t from = 0, found = 0;
		std::atomic<int> permille{0};
		std::atomic<bool> cancel{false}, done{false};
		bool hit = false;	// (read once done)
		std::thread t;
		~FindJob()
		{
			if (t.joinable()) t.join();
		}
	};

	// ---- opening, places, closing ----

	bool Ui::openDocument(const std::string& path, Player& p)
	{
		bool comic = isComicName(path);
		if (isNetPath(path) || (!isDocumentName(path) && !comic)) return false;
		// a comic (CBZ, CBR) reads with the Manga reader's pages: only with it turned on
		if (comic && !p.settings.mangaOn)
		{
			alert("Comics need the Manga reader", "Turn it on in Extensions to read CBZ and CBR files.");
			return true;
		}
		std::string e = extLower(path);
		docPlacesLoad();
		docMarksLoad();
		const ResumeEntry* kept = docPlaces->find(path);
		bool pictures = isPdfName(path) || comic;
		if (e == "epub")
		{
			// a comic (pictures a page) reads as a PDF does; a novel reflows in the text viewer
			Epub book;
			pictures = book.open(path) && book.pictureBook();
		}
		if (page != Page::DocText && page != Page::DocPages) docFrom = page;
		docPath = path;
		docLive = false;
		docMenu = DOC_MENU_NONE;
		docPressed = -1;
		docTouch = docBar = docPanTouch = false;
		std::string name = fileName(path);
		fprintf(stderr, "docs: open %s (%s)\n", name.c_str(), pictures ? "pages" : "text");
		p.addRecent(path);
		if (!pictures)
		{
			textRead = {path, kept && kept->position > 0 ? (uint64_t)kept->position : 0};
			textStatus = {};
			textStatus.title = name;
			page = Page::DocText;
			return true;
		}
		int page0 = kept && kept->position > 0 ? (int)kept->position : 0;
		mangaRead = {"", name, "", name, path, true, page0, p.settings.pdfStrip ? MANGA_STRIP : MANGA_LTR, MANGA_FIT_PAGE};
		mangaReadSeriesId.clear();
		mangaReadChapterId.clear();
		mangaReadChapterTitle = name;
		mangaZoomAsked = 100;
		mangaPages = false;
		readerStatus = {};
		docPan = {};
		page = Page::DocPages;
		return true;
	}

	int Ui::docPagesFilter(const Player& p) const
	{
		return page == Page::DocPages ? p.settings.pdfFilter : -1;
	}

	void Ui::docPlacesLoad()
	{
		if (docPlaces) return;
		docPlaces = std::make_unique<ResumeStore>();
		if (stateDirPath.empty()) return;
		std::ifstream f(stateDirPath + "/doc-places.conf");
		std::stringstream ss;
		ss << f.rdbuf();
		docPlaces->parse(ss.str());
	}

	void Ui::docPlacesSave(bool now)
	{
		if (!docPlaces || !docPlacesDirty || stateDirPath.empty()) return;
		if (!now && SDL_GetTicks() - docSavedAt < 2000) return;
		std::string tmp = stateDirPath + "/doc-places.conf.tmp";
		{
			std::ofstream f(tmp);
			f << docPlaces->serialize();
		}
		std::rename(tmp.c_str(), (stateDirPath + "/doc-places.conf").c_str());
		docPlacesDirty = false;
		docSavedAt = SDL_GetTicks();
	}

	void Ui::docRemember()
	{
		if (docPath.empty() || !docLive) return;
		docPlacesLoad();
		ResumeEntry e;
		e.path = docPath;
		if (page == Page::DocText)
		{
			if (!textStatus.open) return;
			e.position = (double)textStatus.place;
			e.duration = textStatus.fraction;
			e.aid = 1;	// (the kind: a text's place and share, not a page of so many)
		}
		else
		{
			if (!readerStatus.open || readerStatus.pages <= 0) return;
			e.position = readerStatus.page;
			e.duration = readerStatus.pages;
			e.aid = 2;	// (the kind: a page of so many)
		}
		const ResumeEntry* was = docPlaces->find(docPath);
		if (was && was->position == e.position && was->aid == e.aid && was->duration == e.duration) return;
		docPlaces->put(e);
		docPlacesDirty = true;
	}

	void Ui::docClose(Player& p)
	{
		(void)p;
		docRemember();
		docPlacesSave(true);
		if (page == Page::DocText) textCloseAsked = true;
		else
		{
			readerCmds.push_back({ReaderCmd::Close, 0, (int64_t)SDL_GetTicks()});
			mangaPages = false;
			mpFree();
			mfFree();
			mangaZoomAsked = 100;
		}
		if (docPanTex) SDL_DestroyTexture(docPanTex);
		docPanTex = nullptr;
		if (docBookTex) SDL_DestroyTexture(docBookTex);
		docBookTex = nullptr;
		if (docBookOldTex) SDL_DestroyTexture(docBookOldTex);
		docBookOldTex = nullptr;
		docBookOldSrc = nullptr;
		docPanTexPage = -1;
		docPanTexSrc = nullptr;
		docMenu = DOC_MENU_NONE;
		docLive = false;
		if (findJob) findJob->cancel = true;
		findOn = false;
		docNote.clear();
		fprintf(stderr, "docs: closed %s\n", fileName(docPath).c_str());
		docPath.clear();
		page = docFrom == Page::DocText || docFrom == Page::DocPages ? Page::Browse : docFrom;
	}

	void Ui::docTick(Player& p)
	{
		if (p.settings.txtBook) docBookHoldLast = p.settings.txtBook;
		findTick();
		bool open = page == Page::DocText ? textStatus.open : readerStatus.open;
		if (open) docLive = true;
		docRemember();
		docPlacesSave(false);
	}

	// ---- Find ----

	void Ui::runFind(FindJob* j)
	{
		j->hit = findInFile(j->path, j->epub, j->charset, j->query, j->from, j->back, j->found,
			[j] { return j->cancel.load(); }, [j](double f) { j->permille = (int)(f * 1000); });
		j->done = true;
	}

	void Ui::findStart(bool back)
	{
		if (findQuery.empty() || findJob || docPath.empty()) return;
		auto j = std::make_shared<FindJob>();
		j->path = docPath;
		j->epub = extLower(docPath) == "epub";
		j->charset = textEncoding;
		j->query = findQuery;
		j->back = back;
		j->from = textStatus.place;
		findJob = j;
		FindJob* raw = j.get();
		fprintf(stderr, "docs: find \"%s\" %s from %llu\n", findQuery.c_str(), back ? "back" : "on", (unsigned long long)j->from);
		j->t = std::thread([raw] { runFind(raw); });
	}

	std::string Ui::docHighlight() const
	{
		return findOn && page == Page::DocText ? findQuery : std::string();
	}

	void Ui::findTick()
	{
		// a step within the block shown came back: none there, the search goes on that way
		if (findStepWay && textStatus.matchStep)
		{
			int way = findStepWay;
			findStepWay = 0;
			if (textStatus.matchStep < 0) findStart(way < 0);
			textStatus.matchStep = 0;
		}
		if (!findJob || !findJob->done) return;
		auto j = findJob;
		findJob.reset();
		if (j->t.joinable()) j->t.join();
		docNoteAt = SDL_GetTicks();
		if (j->cancel) { docNote = "Find stopped"; return; }
		if (!j->hit)
		{
			docNote = "No \u201C" + j->query + "\u201D in this " + (j->epub ? "book" : "text");
			return;
		}
		textCmds.push_back({j->back ? TextReader::JumpToLast : TextReader::JumpTo, (double)j->found, (int64_t)SDL_GetTicks()});
		findOn = true;
		docNote = "Found \u201C" + j->query + "\u201D";
		fprintf(stderr, "docs: found at %llu\n", (unsigned long long)j->found);
	}

	std::string Ui::findSignature() const
	{
		std::string s = "|find" + std::to_string(findOn) + findQuery;
		if (findJob) s += "~" + std::to_string(findJob->permille / 20);
		Uint32 now = SDL_GetTicks();
		if (!docNote.empty() && now - docNoteAt < 2000) s += "|note" + docNote + std::to_string((now - docNoteAt) / 100);
		return s;
	}

	void Ui::drawFind()
	{
		// a search running: over everything, how far, B stops it
		if (findJob)
		{
			Rect box{100, 170, 440, 140};
			drawDialog(box, "Finding\u2026");
			drawTextMid("\u201C" + findQuery + "\u201D", 1, TEXT, W / 2, box.y + 70, box.w - 40, 2);
			Rect track{box.x + 30, box.y + 96, box.w - 60, 10};
			drawLevel(track, track.w * findJob->permille / 1000, TRACK, ACCENT);
			drawHints({{"B", "Stop"}});
		}
	}

	// ---- bookmarks ----

	void Ui::docMarksLoad()
	{
		if (docMarksLoaded) return;
		docMarksLoaded = true;
		if (stateDirPath.empty()) return;
		std::ifstream f(stateDirPath + "/doc-bookmarks.conf");
		std::string line;
		// a line a bookmark: the place, its share, the file, its label (tabs between; the label last, as typed)
		while (std::getline(f, line))
		{
			if (line.empty() || line[0] == '#') continue;
			size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1), c = b == std::string::npos ? b : line.find('\t', b + 1);
			if (c == std::string::npos) continue;
			DocMark m;
			m.id = strtoull(line.substr(0, a).c_str(), nullptr, 10);
			m.frac = atof(line.substr(a + 1, b - a - 1).c_str());
			m.path = line.substr(b + 1, c - b - 1);
			m.label = line.substr(c + 1);
			docMarks.push_back(m);
		}
	}

	void Ui::docMarksSave()
	{
		if (stateDirPath.empty()) return;
		std::string out = "# banana-player: bookmarks in documents (place, share, file, label)\n";
		for (auto& m : docMarks)
		{
			std::string label = m.label;
			for (char& ch : label) if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
			char head[64];
			snprintf(head, sizeof head, "%llu\t%.4f\t", (unsigned long long)m.id, m.frac);
			out += head + m.path + "\t" + label + "\n";
		}
		std::string tmp = stateDirPath + "/doc-bookmarks.conf.tmp";
		{
			std::ofstream f(tmp);
			f << out;
		}
		std::rename(tmp.c_str(), (stateDirPath + "/doc-bookmarks.conf").c_str());
	}

	std::vector<int> Ui::docMarksHere() const
	{
		std::vector<int> v;
		for (int i = 0; i < (int)docMarks.size(); i++)
			if (docMarks[i].path == docPath) v.push_back(i);
		std::sort(v.begin(), v.end(), [&](int a, int b) { return docMarks[a].id < docMarks[b].id; });
		return v;
	}

	void Ui::docMarksOpen()
	{
		docMarksLoad();
		docMenu = DOC_MENU_MARKS;
		docMenuCursor = docMenuScroll = 0;
		docMarkArmed = false;
	}

	// ---- the menus ----

	std::vector<int> Ui::docMenuRows() const
	{
		if (page == Page::DocText)
		{
			std::vector<int> r = {DT_FIND, DT_MARKS, DT_VIEW};
			if (textStatus.book) { r.push_back(DT_HOLD); r.push_back(DT_TURN); }
			for (int id : {DT_FONT, DT_SIZE, DT_SPACING, DT_MARGINS, DT_THEME, DT_FILTER}) r.push_back(id);
			if (!textStatus.encoding.empty()) r.push_back(DT_ENCODING);
			return r;
		}
		bool strip = readerStatus.mode == MANGA_STRIP;
		std::vector<int> r = {DP_VIEW};
		if (strip) r.push_back(DP_FULL);
		r.push_back(DP_FILTER);
		if (!strip) { r.push_back(DP_FIT); r.push_back(DP_ZOOM); }
		r.push_back(DP_PAGES);
		return r;
	}

	void Ui::docMenuStep(int dir, Player& p)
	{
		auto rows = docMenuRows();
		if (docMenuCursor < 0 || docMenuCursor >= (int)rows.size()) return;
		int id = rows[docMenuCursor];
		Settings s = p.settings;
		auto cyc = [&](int& v, int n) { v = ((v + dir) % n + n) % n; };
		switch (id)
		{
		case DT_FONT: cyc(s.txtFont, TXT_FONTS); break;
		case DT_SIZE: s.txtSize = std::clamp(s.txtSize + dir, 0, TXT_SIZES - 1); break;
		case DT_SPACING: s.txtSpacing = std::clamp(s.txtSpacing + dir, 0, TXT_SPACINGS - 1); break;
		case DT_MARGINS: s.txtMargins = std::clamp(s.txtMargins + dir, 0, TXT_MARGINS - 1); break;
		case DT_THEME: cyc(s.txtTheme, TXT_THEMES); break;
		case DT_FILTER: cyc(s.txtFilter, DOC_FILTERS); break;
		case DT_VIEW:
			// Scroll or Book, held as Book was last (the top panel on the left the first time)
			s.txtBook = s.txtBook ? 0 : (docBookHoldLast ? docBookHoldLast : MANGA_BOOK_LEFT);
			if (s.txtBook) docBookToastUntil = SDL_GetTicks() + 4000;
			break;
		case DT_TURN: cyc(s.txtTurn, 2); break;
		case DT_HOLD:
			s.txtBook = s.txtBook == MANGA_BOOK_LEFT ? MANGA_BOOK_RIGHT : MANGA_BOOK_LEFT;
			docBookHoldLast = s.txtBook;
			docBookToastUntil = SDL_GetTicks() + 4000;
			break;
		case DP_FILTER: cyc(s.pdfFilter, DOC_FILTERS); break;
		case DT_ENCODING:
		{
			// the subtitles' own list of code pages (Auto first)
			const auto& ch = subStyleSettings()[ENCODING].choices;
			int at = 0;
			for (int i = 0; i < (int)ch.size(); i++)
			{
				std::string id = ch[i].id;
				for (char& c : id) c = (char)std::toupper((unsigned char)c);
				if ((i == 0 && textEncoding.empty()) || (i > 0 && id == textEncoding)) at = i;
			}
			at = ((at + dir) % (int)ch.size() + (int)ch.size()) % (int)ch.size();
			std::string id = at == 0 ? "" : ch[at].id;
			for (char& c : id) c = (char)std::toupper((unsigned char)c);
			textEncoding = id;
			textEncodingAsked = true;
			return;
		}
		case DT_MARKS: case DT_FIND: return;
		case DT_GOTO:
			textCmds.push_back({TextReader::Jump, std::clamp(textStatus.fraction + dir * 0.05, 0.0, 1.0), (int64_t)SDL_GetTicks()});
			return;
		case DP_VIEW:
		{
			bool strip = readerStatus.mode != MANGA_STRIP;
			// (each view opens at its fit: a page's zoom would cut a strip's lines at both edges)
			if (readerStatus.zoomPct != 100) mangaZoomTo(1.0);
			if (!strip && readerStatus.fullscreen) readerCmds.push_back({ReaderCmd::Fullscreen, 0, (int64_t)SDL_GetTicks()});
			readerCmds.push_back({ReaderCmd::Mode, (double)(strip ? MANGA_STRIP : MANGA_LTR), (int64_t)SDL_GetTicks()});
			s.pdfStrip = strip;
			break;
		}
		case DP_FULL:
			readerCmds.push_back({ReaderCmd::Fullscreen, readerStatus.fullscreen ? 0.0 : 1.0, (int64_t)SDL_GetTicks(), (double)p.settings.mangaHinge});
			if (!readerStatus.fullscreen) docFullTagUntil = SDL_GetTicks() + 2500;
			return;
		case DP_FIT:
			cyc(s.mangaFit, 2);	// Page and Width (Fill crops a document's margins away)
			readerCmds.push_back({ReaderCmd::Fit, (double)s.mangaFit, (int64_t)SDL_GetTicks()});
			break;
		case DP_ZOOM: mangaZoomStep(dir); return;
		default: return;
		}
		p.setSettings(s);
	}

	void Ui::docMenuAct(Player& p)
	{
		if (docMenu == DOC_MENU_MARKS)
		{
			// the first row adds one at the place; the rest go to theirs
			auto here = docMarksHere();
			if (docMenuCursor == 0)
			{
				for (int i : here)
					if (docMarks[i].id == textStatus.place) { p.osd("There's a bookmark here already", 1500); return; }
				DocMark m;
				m.path = docPath;
				m.id = textStatus.place;
				m.frac = textStatus.fraction;
				m.label = textStatus.snippet.empty() ? "Bookmark" : textStatus.snippet;
				docMarks.push_back(m);
				docMarksSave();
				p.osd("Bookmarked", 1200);
				return;
			}
			int k = docMenuCursor - 1;
			if (k >= 0 && k < (int)here.size())
				textCmds.push_back({TextReader::JumpTo, (double)docMarks[here[k]].id, (int64_t)SDL_GetTicks()});
			docMenu = DOC_MENU_NONE;
			return;
		}
		if (docMenu == DOC_MENU_CONTENTS)
		{
			if (docMenuCursor >= 0 && docMenuCursor < (int)textStatus.contents.size())
				textCmds.push_back({TextReader::JumpTo, (double)textStatus.contents[docMenuCursor].second.first, (int64_t)SDL_GetTicks()});
			docMenu = DOC_MENU_NONE;
			return;
		}
		auto rows = docMenuRows();
		int id = docMenuCursor >= 0 && docMenuCursor < (int)rows.size() ? rows[docMenuCursor] : -1;
		if (id == DP_PAGES)
		{
			docMenu = DOC_MENU_NONE;
			mangaPagesOpen(p);
			return;
		}
		if (id == DT_MARKS)
		{
			docMarksOpen();
			return;
		}
		if (id == DT_FIND)
		{
			docMenu = DOC_MENU_NONE;
			openKeyboard(FIND_FIELD, findQuery);
			return;
		}
		docMenuStep(1, p);
	}

	void Ui::drawDocMenu(const Player& p)
	{
		bool contents = docMenu == DOC_MENU_CONTENTS, marks = docMenu == DOC_MENU_MARKS;
		// over the whole panel, its title where the page's was
		fill({0, 0, W, H}, BG);
		fill({0, 60, W, 2}, RULE);
		drawTextMid(marks ? "Bookmarks" : contents ? "Contents" : page == Page::DocText ? "Text options" : "Options", 1, TEXT, 24, 32, 400);
		auto here = docMarksHere();
		int shown = (H - MENU_TOP - 40) / MENU_ROW_H;
		int n = marks ? (int)here.size() + 1 : contents ? (int)textStatus.contents.size() : (int)docMenuRows().size();
		if (docMenuCursor < docMenuScroll) docMenuScroll = docMenuCursor;
		if (docMenuCursor >= docMenuScroll + shown) docMenuScroll = docMenuCursor - shown + 1;
		docMenuScroll = std::clamp(docMenuScroll, 0, std::max(0, n - shown));
		auto rows = docMenuRows();
		for (int i = docMenuScroll; i < n && i < docMenuScroll + shown; i++)
		{
			Rect r{16, MENU_TOP + (i - docMenuScroll) * MENU_ROW_H, W - 32, MENU_ROW_H - 4};
			bool at = i == docMenuCursor;
			if (at) drawFocus(r);
			int mid = r.y + r.h / 2;
			SDL_Color ink = at ? ACCENT : TEXT;
			if (marks)
			{
				if (i == 0)
				{
					drawGlyphMid(G_PLUS, 40, mid, ink);
					drawTextMid("Add a bookmark here", 1, ink, 64, mid, 520);
					continue;
				}
				const DocMark& m = docMarks[here[i - 1]];
				drawGlyphMid(G_BOOKMARK, 40, mid, at ? ACCENT : DIM);
				drawTextMid(std::to_string((int)std::lround(m.frac * 100)) + "%", 1, ink, 64, mid, 70);
				bool armedHere = at && docMarkArmed;
				drawTextMid(armedHere ? "X again to delete it" : m.label, 0, armedHere ? ACCENT : at ? TEXT : DIM, 140, mid, 470);
				continue;
			}
			if (contents)
			{
				const auto& c = textStatus.contents[i];
				drawTextMid(c.first, 1, ink, 32 + std::min(c.second.second, 4) * 20, mid, 560 - std::min(c.second.second, 4) * 20);
				continue;
			}
			int id = rows[i];
			static const char* LABELS[] = {"Font", "Text size", "Line spacing", "Margins", "Colours", "Filter", "Encoding",
				"Go to", "View", "Fullscreen", "Filter", "Page fit", "Zoom", "Pages", "View", "Hold", "Bookmarks", "Page turn", "Find\u2026"};
			drawTextMid(LABELS[id], 1, ink, 32, mid, 300);
			if (id == DT_FIND)
			{
				if (!findQuery.empty()) drawTextMid("\u201C" + findQuery + "\u201D", 0, at ? ACCENT : DIM, 580, mid, 220, 1);
				drawGlyphMid(G_RIGHT, 604, mid, at ? ACCENT : DIM);
				continue;
			}
			if (id == DP_PAGES || id == DT_MARKS)
			{
				if (id == DT_MARKS)
				{
					int nm = (int)docMarksHere().size();
					drawTextMid(nm ? std::to_string(nm) : "None yet", 0, at ? ACCENT : DIM, 580, mid, 160, 1);
				}
				drawGlyphMid(G_RIGHT, 604, mid, at ? ACCENT : DIM);
				continue;
			}
			std::string v;
			switch (id)
			{
			case DT_FONT: v = FONT_NAMES[std::clamp(p.settings.txtFont, 0, TXT_FONTS - 1)]; break;
			case DT_SIZE: v = SIZE_NAMES[std::clamp(p.settings.txtSize, 0, TXT_SIZES - 1)]; break;
			case DT_SPACING: v = SPACING_NAMES[std::clamp(p.settings.txtSpacing, 0, TXT_SPACINGS - 1)]; break;
			case DT_MARGINS: v = MARGIN_NAMES[std::clamp(p.settings.txtMargins, 0, TXT_MARGINS - 1)]; break;
			case DT_THEME: v = THEME_NAMES[std::clamp(p.settings.txtTheme, 0, TXT_THEMES - 1)]; break;
			case DT_FILTER: v = FILTER_NAMES[std::clamp(p.settings.txtFilter, 0, DOC_FILTERS - 1)]; break;
			case DP_FILTER: v = FILTER_NAMES[std::clamp(p.settings.pdfFilter, 0, DOC_FILTERS - 1)]; break;
			case DT_ENCODING: v = textEncoding.empty() ? "Auto (" + textStatus.encoding + ")" : textStatus.encoding; break;
			case DT_GOTO: v = std::to_string((int)std::lround(textStatus.fraction * 100)) + "%"; break;
			case DT_VIEW: v = p.settings.txtBook ? "Book mode" : "Scroll"; break;
			case DT_TURN: v = p.settings.txtTurn ? "Off" : "Fold"; break;
			case DT_HOLD: v = p.settings.txtBook == MANGA_BOOK_RIGHT ? "Top screen right" : "Top screen left"; break;
			case DP_VIEW: v = readerStatus.mode == MANGA_STRIP ? "Vertical scroll" : "Single page"; break;
			case DP_FULL: v = readerStatus.fullscreen ? "On" : "Off"; break;
			case DP_FIT: v = p.settings.mangaFit == MANGA_FIT_WIDTH ? "Width" : "Page"; break;
			case DP_ZOOM: v = std::to_string(readerStatus.zoomPct) + "%"; break;
			default: break;
			}
			drawTextMid("<", 1, at ? ACCENT : DIM, SET_ARROW_X, mid);
			drawTextMid(v, 1, ink, 490, mid, 196, 2);
			drawTextMid(">", 1, at ? ACCENT : DIM, 608, mid, 0, 1);
		}
		if (n > shown) drawScrollBar(MENU_TOP, shown * MENU_ROW_H, docMenuScroll, shown, n);
		if (marks) drawHints({{"A", docMenuCursor == 0 ? "Add" : "Go"}, {"X", "Delete"}, {"B", "Close"}});
		else if (contents) drawHints({{"A", "Go"}, {"Y", "Close"}, {"B", "Close"}});
		else drawHints({{"LEFT/RIGHT", "Change"}, {"START", "Close"}, {"B", "Close"}});
	}

	// ---- the keys ----

	bool Ui::docKey(Button b, Player& p)
	{
		int64_t now = SDL_GetTicks();
		// a search running: B stops it, nothing else answers meanwhile
		if (findJob)
		{
			if (b == Button::B) findJob->cancel = true;
			return false;
		}
		// a match shown: R / L the next / previous, B leaves Find (the text stays where it is)
		if (page == Page::DocText && findOn && docMenu == DOC_MENU_NONE)
		{
			// (the block shown first: a long paragraph's other matches, then on to the next block that has one)
			if (b == Button::R || b == Button::L)
			{
				findStepWay = b == Button::R ? 1 : -1;
				textStatus.matchStep = 0;
				textCmds.push_back({TextReader::MatchStep, (double)findStepWay, now});
				return false;
			}
			if (b == Button::B) { findOn = false; return false; }
		}
		// L3: Find (the query typed again, the last one ready)
		if (page == Page::DocText && b == Button::L3 && docMenu == DOC_MENU_NONE)
		{
			openKeyboard(FIND_FIELD, findQuery);
			return false;
		}
		if (docMenu != DOC_MENU_NONE)
		{
			int n = docMenu == DOC_MENU_MARKS ? (int)docMarksHere().size() + 1
				: docMenu == DOC_MENU_CONTENTS ? (int)textStatus.contents.size() : (int)docMenuRows().size();
			if (docMenu == DOC_MENU_MARKS && b == Button::X && docMenuCursor > 0)
			{
				// twice: the bookmark under the cursor goes
				if (!docMarkArmed) { docMarkArmed = true; return false; }
				auto here = docMarksHere();
				if (docMenuCursor - 1 < (int)here.size())
				{
					docMarks.erase(docMarks.begin() + here[docMenuCursor - 1]);
					docMarksSave();
				}
				docMarkArmed = false;
				docMenuCursor = std::min(docMenuCursor, (int)docMarksHere().size());
				return false;
			}
			if (b != Button::X) docMarkArmed = false;
			switch (b)
			{
			case Button::Up: if (docMenuCursor > 0) docMenuCursor--; break;
			case Button::Down: if (docMenuCursor + 1 < n) docMenuCursor++; break;
			case Button::L: docMenuCursor = std::max(0, docMenuCursor - 8); break;
			case Button::R: docMenuCursor = std::clamp(docMenuCursor + 8, 0, std::max(0, n - 1)); break;
			case Button::Left: if (docMenu == DOC_MENU_OPTIONS) docMenuStep(-1, p); break;
			case Button::Right: if (docMenu == DOC_MENU_OPTIONS) docMenuStep(1, p); break;
			case Button::A: docMenuAct(p); break;
			case Button::B: case Button::Start: case Button::Y: docMenu = DOC_MENU_NONE; break;
			case Button::X: if (docMenu != DOC_MENU_MARKS) docMenu = DOC_MENU_NONE; break;
			default: break;
			}
			return false;
		}
		if (page == Page::DocText && textStatus.book)
		{
			// held sideways: UP and DOWN point along the spread (as the manga reader's Book); the shoulders turn too
			switch (b)
			{
			case Button::Up: docBookTurn(-1, p); break;
			case Button::Down: docBookTurn(1, p); break;
			case Button::L: textCmds.push_back({TextReader::BookTurn, -1, now}); break;
			case Button::R: case Button::A: textCmds.push_back({TextReader::BookTurn, 1, now}); break;
			case Button::X: docMarksOpen(); break;
			case Button::Start: docMenu = DOC_MENU_OPTIONS; docMenuCursor = docMenuScroll = 0; break;
			// SELECT or B: back to the scroll (B there closes the document)
			case Button::Select: case Button::B: { Settings s = p.settings; s.txtBook = 0; p.setSettings(s); break; }
			default: break;
			}
			if (b == Button::Y && !textStatus.contents.empty())
			{
				docMenu = DOC_MENU_CONTENTS;
				docMenuCursor = 0;
				for (int i = 0; i < (int)textStatus.contents.size(); i++)
					if (textStatus.contents[i].second.first <= textStatus.place &&
						(i == 0 || textStatus.contents[i].second.first > textStatus.contents[docMenuCursor].second.first))
						docMenuCursor = i;
				docMenuScroll = std::max(0, docMenuCursor - 3);
			}
			return false;
		}
		if (page == Page::DocText)
		{
			switch (b)
			{
			case Button::Up: textCmds.push_back({TextReader::Lines, -3, now}); break;
			case Button::Down: textCmds.push_back({TextReader::Lines, 3, now}); break;
			case Button::Left: case Button::L: textCmds.push_back({TextReader::Page, -1, now}); break;
			case Button::Right: case Button::R: case Button::A: textCmds.push_back({TextReader::Page, 1, now}); break;
			case Button::X: docMarksOpen(); break;
			case Button::Start: docMenu = DOC_MENU_OPTIONS; docMenuCursor = docMenuScroll = 0; break;
			case Button::Y:
				if (!textStatus.contents.empty())
				{
					// the cursor on the chapter being read
					docMenu = DOC_MENU_CONTENTS;
					docMenuCursor = 0;
					// (the first of entries that open the same place: a chapter before its own sections)
					for (int i = 0; i < (int)textStatus.contents.size(); i++)
						if (textStatus.contents[i].second.first <= textStatus.place &&
							(i == 0 || textStatus.contents[i].second.first > textStatus.contents[docMenuCursor].second.first))
							docMenuCursor = i;
					docMenuScroll = std::max(0, docMenuCursor - 3);
				}
				break;
			case Button::Select:
			{
				// Book, held as it was last
				Settings s = p.settings;
				s.txtBook = docBookHoldLast ? docBookHoldLast : MANGA_BOOK_LEFT;
				p.setSettings(s);
				docBookToastUntil = SDL_GetTicks() + 4000;
				break;
			}
			case Button::B: docClose(p); break;
			default: break;
			}
			return false;
		}
		// the PDF viewer
		if (mangaPages) return mangaPagesKey(b, p);
		bool strip = readerStatus.mode == MANGA_STRIP;
		if (readerStatus.fullscreen && (b == Button::B || b == Button::Select))
		{
			readerCmds.push_back({ReaderCmd::Fullscreen, 0, now});
			return false;
		}
		switch (b)
		{
		case Button::Left: readerCmds.push_back({ReaderCmd::Step, -1, now}); break;
		case Button::Right: readerCmds.push_back({ReaderCmd::Step, 1, now}); break;
		case Button::L: readerCmds.push_back({ReaderCmd::Page, -1, now}); break;
		case Button::R: readerCmds.push_back({ReaderCmd::Page, 1, now}); break;
		// zoom in either view, as the manga reader and the photo viewer do (the finger, LEFT/RIGHT and A move on)
		case Button::Up: mangaZoomStep(1); break;
		case Button::Down: mangaZoomStep(-1); break;
		case Button::A:
			if (readerStatus.failedHere) mangaRetry();
			else if (strip) readerCmds.push_back({ReaderCmd::Step, 1, now});
			else mangaZoomToggle();
			break;
		case Button::X:
		{
			// Single page and Strip, as the menu's View row (the first)
			docMenuCursor = 0;
			docMenuStep(1, p);
			break;
		}
		case Button::Select:
			if (strip)
			{
				readerCmds.push_back({ReaderCmd::Fullscreen, 1, now, (double)p.settings.mangaHinge});
				docFullTagUntil = SDL_GetTicks() + 2500;
			}
			break;
		case Button::Y: mangaPagesOpen(p); break;
		case Button::Start: docMenu = DOC_MENU_OPTIONS; docMenuCursor = docMenuScroll = 0; break;
		case Button::B: docClose(p); break;
		default: break;
		}
		return false;
	}

	void Ui::docBookTurn(int dpadDy, Player& p)
	{
		int step = mangaBookStep(textStatus.book, false, false, dpadDy);
		if (step) textCmds.push_back({TextReader::BookTurn, (double)step, (int64_t)SDL_GetTicks()});
		(void)p;
	}

	// ---- the finger ----

	Ui::Rect Ui::docBarRect() const { return {24, 66, W - 48, 30}; }
	Ui::Rect Ui::docPadRect() const { return {16, 104, W - 32, (miniUp ? 344 : 396) - 104 - 64}; }
	Ui::Rect Ui::docBtn(int i) const
	{
		if (page == Page::DocPages && readerStatus.mode != MANGA_STRIP)
		{
			// beside the pan area, as the photo viewer's; the ninth, wide under them: the other view
			if (i == 8) return {368, 336, 248, 52};
			return {368 + i % 2 * 128, 80 + i / 2 * 64, 120, 56};
		}
		int y = (miniUp ? 344 : 396) - 56;
		return {16 + i * 154, y, 146, 56};
	}

	Ui::Rect Ui::docPanThumb() const
	{
		Rect pan = docPanRect();
		double a = docPan.w > 0 && docPan.h > 0 ? docPan.w / (double)docPan.h : 0.7;
		int w = pan.w, h = (int)std::lround(pan.w / a);
		if (h > pan.h)
		{
			h = pan.h;
			w = (int)std::lround(pan.h * a);
		}
		return {pan.x + (pan.w - w) / 2, pan.y + (pan.h - h) / 2, w, h};
	}

	bool Ui::docPointer(const Pointer& pt, int x, int y, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		if (docMenu != DOC_MENU_NONE)
		{
			// the list follows a finger's drag (as the manga reader's lists); a tap acts on the row as the finger lifts
			if (pt.kind != Pointer::Down) return mangaListPointer(pt, x, y);
			int n = docMenu == DOC_MENU_MARKS ? (int)docMarksHere().size() + 1
				: docMenu == DOC_MENU_CONTENTS ? (int)textStatus.contents.size() : (int)docMenuRows().size();
			int shown = std::max(1, (H - MENU_TOP - 40) / MENU_ROW_H);
			int r = (y - MENU_TOP) / MENU_ROW_H + docMenuScroll;
			if (y < 60) { docMenu = DOC_MENU_NONE; return false; }
			if (y < MENU_TOP || r < 0 || r >= n) r = -1;
			mangaListBegin(x, y, docMenuScroll, MENU_ROW_H, r,
				[this, n, shown](int want) {
					docMenuScroll = std::clamp(want, 0, std::max(0, n - shown));
					// the cursor stays among the rows shown (the drawing keeps it in view, and would pull the list back)
					docMenuCursor = std::clamp(docMenuCursor, docMenuScroll, std::min(n - 1, docMenuScroll + shown - 1));
					return docMenuScroll;
				},
				[this, x, pp = &p](int row) {
					docMenuCursor = row;
					// a tap left of the value steps back, right of it on (as the settings' rows)
					if (docMenu == DOC_MENU_OPTIONS && x >= SET_ARROW_X - 20) docMenuStep(x < 490 ? -1 : 1, *pp);
					else docMenuAct(*pp);
				});
			return false;
		}
		if (page == Page::DocPages && mangaPages) return mangaPagesPointer(pt, x, y, p);
		if (findJob) return false;
		// the find bar over the buttons: Previous, Next, Done
		if (page == Page::DocText && findOn && !textStatus.book && pt.kind == Pointer::Up)
			for (int i = 0; i < 3; i++)
			{
				Rect r = docBtn(0);
				Rect bb{16 + i * 206, r.y, 196, r.h};
				if (bb.hit(x, y))
				{
					if (i < 2) docKey(i == 0 ? Button::L : Button::R, p);
					else findOn = false;
					return false;
				}
			}
		if (page == Page::DocText && textStatus.book)
		{
			// the panel is a page: a swipe along the spread turns it (as the manga reader's Book); a tap near the
			// top or the bottom of the panel turns the way UP or DOWN would, a tap in the middle opens Text options
			if (pt.kind == Pointer::Down)
			{
				docTouch = true;
				docDownX = x;
				docDownY = y;
				return false;
			}
			if (pt.kind == Pointer::Up && docTouch)
			{
				docTouch = false;
				int dy = y - docDownY, dx = x - docDownX;
				int step = std::abs(dy) > 40 && std::abs(dy) > std::abs(dx) ? mangaBookSwipe(textStatus.book, false, false, dy) : 0;
				if (step) textCmds.push_back({TextReader::BookTurn, (double)step, (int64_t)now});
				else if (std::abs(dy) <= 20 && std::abs(dx) <= 20)
				{
					if (y < H * 3 / 10) docBookTurn(-1, p);
					else if (y > H * 7 / 10) docBookTurn(1, p);
					else { docMenu = DOC_MENU_OPTIONS; docMenuCursor = docMenuScroll = 0; }
				}
			}
			return false;
		}
		if (pt.kind == Pointer::Down && buttonRect(BACK_ICON).hit(x, y) && !(page == Page::DocPages && readerStatus.fullscreen))
		{
			docClose(p);
			return false;
		}
		bool text = page == Page::DocText;
		bool full = !text && readerStatus.fullscreen;
		bool single = !text && readerStatus.mode != MANGA_STRIP;
		int nBtn = text ? 4 : single ? 9 : 4;
		if (pt.kind == Pointer::Down)
		{
			docPressed = -1;
			docMoved = false;
			docDownX = x;
			docDownY = y;
			docLastY = y;
			docLastAt = now;
			docVel = 0;
			docTouchX = pt.x;
			docTouchY = pt.y;
			if (full || (!text && !single && docPadRect().hit(x, y)))
			{
				// the strip under the finger, as the manga reader's
				docTouch = true;
				readerCmds.push_back({ReaderCmd::TouchDown, 0, (int64_t)now});
				return false;
			}
			if (text && docPadRect().hit(x, y))
			{
				docTouch = true;
				textCmds.push_back({TextReader::Hold, 0, (int64_t)now});
				return false;
			}
			if ((text || !single) && docBarRect().hit(x, y))
			{
				docBar = true;
				return docPointer({Pointer::Move, pt.x, pt.y}, x, y, p);
			}
			if (single && docPanThumb().hit(x, y) && docPan.page >= 0)
			{
				docPanTouch = true;
				docPanStartX = (docPan.x0 + docPan.x1) / 2;
				docPanStartY = (docPan.y0 + docPan.y1) / 2;
				return false;
			}
			for (int i = 0; i < nBtn; i++)
				if (docBtn(i).hit(x, y)) docPressed = i;
			return false;
		}
		if (pt.kind == Pointer::Move)
		{
			if (std::abs(x - docDownX) > DRAG_SLOP || std::abs(y - docDownY) > DRAG_SLOP) docMoved = true;
			if (docTouch && text)
			{
				// the text follows the finger (a share of the panel's height: main scales it to the top's pixels)
				double dy = pt.y - docTouchY;
				docTouchY = pt.y;
				if (docMoved && dy != 0) textCmds.push_back({TextReader::Scroll, -dy, (int64_t)now});
				Uint32 dt = std::max<Uint32>(1, now - docLastAt);
				docVel = 0.6 * (-(y - docLastY) / (double)H / dt) + 0.4 * docVel;
				docLastY = y;
				docLastAt = now;
			}
			else if (docTouch)
			{
				double dy = pt.y - docTouchY, dx = pt.x - docTouchX;
				docTouchY = pt.y;
				docTouchX = pt.x;
				if (docMoved && (dy != 0 || dx != 0)) readerCmds.push_back({ReaderCmd::Drag, dy, (int64_t)now, dx});
			}
			else if (docBar)
			{
				Rect r = docBarRect();
				double f = std::clamp((x - r.x) / (double)r.w, 0.0, 1.0);
				if (text) textCmds.push_back({TextReader::Jump, f, (int64_t)now});
				else if (readerStatus.pages > 1) mangaSeek((int)std::lround(f * (readerStatus.pages - 1)));
			}
			else if (docPanTouch && docMoved)
			{
				// the box goes with the finger over the small page; the top panel shows what's in it
				Rect tr = docPanThumb();
				double fx = docPanStartX + (x - docDownX) / (double)tr.w, fy = docPanStartY + (y - docDownY) / (double)tr.h;
				readerCmds.push_back({ReaderCmd::Pan, fx, (int64_t)now, fy});
			}
			return false;
		}
		// lifted
		if (docTouch && text)
		{
			docTouch = false;
			if (docMoved && now - docLastAt < 80 && std::abs(docVel) > 0.0002)
				textCmds.push_back({TextReader::Fling, docVel, (int64_t)now});
			else if (!docMoved)
			{
				// a tap: the upper half a screen back, the lower half a screen on
				Rect r = docPadRect();
				textCmds.push_back({TextReader::Page, y < r.y + r.h / 2 ? -1.0 : 1.0, (int64_t)now});
			}
			return false;
		}
		if (docTouch)
		{
			docTouch = false;
			readerCmds.push_back({ReaderCmd::Release, 0, (int64_t)now});
			return false;
		}
		if (docBar)
		{
			docBar = false;
			return false;
		}
		if (docPanTouch)
		{
			docPanTouch = false;
			if (docMoved) return false;
			Rect tr = docPanThumb();
			double fx = std::clamp((x - tr.x) / (double)tr.w, 0.0, 1.0), fy = std::clamp((y - tr.y) / (double)tr.h, 0.0, 1.0);
			if (docTapAt && now - docTapAt < TAP_MS)
			{
				// a double-tap: in to 2x there, or (zoomed) back out
				docTapAt = 0;
				bool zoomed = readerStatus.zoomPct > 100;
				mangaZoomTo(zoomed ? 1.0 : 2.0);
				if (!zoomed) readerCmds.push_back({ReaderCmd::Pan, fx, (int64_t)now, fy});
				return false;
			}
			docTapAt = now;
			// a tap, zoomed in: the view there
			if (readerStatus.zoomPct > 100) readerCmds.push_back({ReaderCmd::Pan, fx, (int64_t)now, fy});
			return false;
		}
		int i = docPressed;
		docPressed = -1;
		if (i < 0 || !docBtn(i).hit(x, y)) return false;
		int64_t ms = now;
		if (text)
		{
			switch (i)
			{
			case 0: docKey(Button::Y, p); break;	// Contents
			case 1: docMarksOpen(); break;	// Bookmarks
			case 2: docKey(Button::Start, p); break;	// Text options
			case 3: docKey(Button::Select, p); break;	// Book
			}
			return false;
		}
		if (single)
		{
			// (4: the page fit, 5: the filter, each says its value; 8, under them: Vertical scroll, as X)
			static const Button KEYS[9] = {Button::L, Button::R, Button::Down, Button::Up, Button::None, Button::None, Button::Y, Button::Start, Button::X};
			if (i == 5)
			{
				Settings s = p.settings;
				s.pdfFilter = (s.pdfFilter + 1) % DOC_FILTERS;
				p.setSettings(s);
			}
			else if (i == 4)
			{
				Settings s = p.settings;
				s.mangaFit = s.mangaFit == MANGA_FIT_WIDTH ? MANGA_FIT_PAGE : MANGA_FIT_WIDTH;
				readerCmds.push_back({ReaderCmd::Fit, (double)s.mangaFit, (int64_t)SDL_GetTicks()});
				p.setSettings(s);
			}
			else docKey(KEYS[i], p);
			return false;
		}
		switch (i)
		{
		case 0: docKey(Button::X, p); break;	// Single page
		case 1: docKey(Button::Select, p); break;	// Fullscreen
		case 2: { Settings s = p.settings; s.pdfFilter = (s.pdfFilter + 1) % DOC_FILTERS; p.setSettings(s); break; }
		case 3: docKey(Button::Y, p); break;	// Pages
		}
		return false;
	}

	// ---- drawing ----

	void Ui::drawDocText(const Player& p)
	{
		if (textStatus.book && textStatus.bottom && docMenu == DOC_MENU_NONE)
		{
			// Book: this panel is the other page, already turned the way the handheld is held
			if (docBookTexGen != textStatus.bottomGen || !docBookTex)
			{
				if (docBookTex) SDL_DestroyTexture(docBookTex);
				docBookTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, textStatus.bottomW, textStatus.bottomH);
				if (docBookTex)
				{
					SDL_UpdateTexture(docBookTex, nullptr, textStatus.bottom->data(), textStatus.bottomW * 4);
					SDL_SetTextureScaleMode(docBookTex, SDL_ScaleModeLinear);
				}
				docBookTexGen = textStatus.bottomGen;
			}
			// the page before a turn, as a texture of its own while the turn lasts
			const auto& old = textStatus.bottomOld;
			if (old && old.get() != docBookOldSrc)
			{
				if (docBookOldTex) SDL_DestroyTexture(docBookOldTex);
				docBookOldTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, textStatus.bottomW, textStatus.bottomH);
				if (docBookOldTex)
				{
					SDL_UpdateTexture(docBookOldTex, nullptr, old->data(), textStatus.bottomW * 4);
					SDL_SetTextureScaleMode(docBookOldTex, SDL_ScaleModeLinear);
				}
				docBookOldSrc = old.get();
			}
			int filter = std::clamp(p.settings.txtFilter, 0, DOC_FILTERS - 1);
			int pw = (int)std::lround(W * sx), ph = (int)std::lround(H * sy);
			// a page squeezed to frac of the panel, against the spine (this panel's top edge, at the hinge)
			auto at = [&](SDL_Texture* t, double frac) {
				if (!t) return;
				tintTexture(t, filter);
				SDL_Rect d{0, 0, pw, (int)std::lround(ph * frac)};
				SDL_RenderCopy(ren, t, nullptr, &d);
			};
			auto ease = [](double t) { return t * t * (3 - 2 * t); };
			double tp = textStatus.turnDir ? std::clamp((SDL_GetTicks() - textStatus.turnAt) / (double)textStatus.turnMs, 0.0, 1.0) : 1;
			// (the top folds when it holds the page going first: the right-hand one going on, the left-hand going back)
			bool botFolds = !((textStatus.turnDir > 0) == textStatus.rightOnTop);
			if (textStatus.turnDir && old && docBookOldTex && tp < 1)
			{
				SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
				if (botFolds)
				{
					at(docBookTex, 1);
					if (tp < 0.5) at(docBookOldTex, 1 - ease(tp * 2));
				}
				else
				{
					at(docBookOldTex, 1);
					if (tp >= 0.5) at(docBookTex, ease((tp - 0.5) * 2));
				}
			}
			else at(docBookTex, 1);
			// the turn's notice: next page or previous page, and how far, upright as the pages are; it fades out. Find's
			// word (found, not found) takes its place while it's newer
			int64_t since = (int64_t)SDL_GetTicks() - textStatus.noticeAt;
			int64_t noteSince = (int64_t)SDL_GetTicks() - docNoteAt;
			bool note = !docNote.empty() && noteSince >= 0 && noteSince < DOC_NOTICE_MS && (since < 0 || noteSince <= since || !textStatus.noticeDir);
			if (note) since = noteSince;
			if ((note || textStatus.noticeDir) && since >= 0 && since < DOC_NOTICE_MS)
			{
				int alpha = since < DOC_NOTICE_MS - 600 ? 255 : (int)(255 * (DOC_NOTICE_MS - since) / 600);
				std::string text = note ? docNote + (findOn ? "  \xC2\xB7  R next, L back" : "")
					: std::string(textStatus.noticeDir > 0 ? "Next page" : "Previous page");	// (the page's % is just below it)
				if (SDL_Surface* sf = TTF_RenderUTF8_Blended(fonts[0], text.c_str(), TEXT))
				{
					if (SDL_Texture* t = SDL_CreateTextureFromSurface(ren, sf))
					{
						// a dark pill behind it, turned as the text is (its sides swapped on the panel)
						int tw = sf->w, th = sf->h, padX = (int)std::lround(14 * sx), padY = (int)std::lround(5 * sy);
						int bw = tw + 2 * padX, bh = th + 2 * padY;
						// just above the page's own % at its foot (not over the text in the middle): the page's foot is
						// this panel's left edge for a page turned clockwise, its right edge for one turned the other way
						int lift = (int)std::lround(pw * 0.06) + bh / 2;
						int cx = textStatus.book == MANGA_BOOK_RIGHT ? pw - lift : lift, cy = ph / 2;
						SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
						SDL_SetRenderDrawColor(ren, BG.r, BG.g, BG.b, (Uint8)(alpha * 220 / 255));
						SDL_Rect pill{cx - bh / 2, cy - bw / 2, bh, bw};
						SDL_RenderFillRect(ren, &pill);
						SDL_SetTextureAlphaMod(t, (Uint8)alpha);
						SDL_Rect d{cx - tw / 2, cy - th / 2, tw, th};
						SDL_RenderCopyEx(ren, t, nullptr, &d, textStatus.book == MANGA_BOOK_RIGHT ? 270 : 90, nullptr, SDL_FLIP_NONE);
						SDL_DestroyTexture(t);
					}
					SDL_FreeSurface(sf);
				}
			}
			if (SDL_GetTicks() < docBookToastUntil)
			{
				// turned as the pages are (it's read held sideways): a box, its lines stacked across the panel
				struct Line { std::string text; int size; SDL_Color ink; };
				Line lines[3] = {{"Book mode", 1, ACCENT},
					{std::string("Hold it with the top screen on the ") + (textStatus.book == MANGA_BOOK_RIGHT ? "right" : "left"), 0, TEXT},
					{"Tap the middle: options \xC2\xB7 B: back to scroll", 0, DIM}};
				bool cw = textStatus.book != MANGA_BOOK_RIGHT;	// (MANGA_BOOK_LEFT: the page turned clockwise)
				int cx = pw / 2, cy = ph / 2;
				// the box upright is 420 x 150 units: on the panel, its sides swapped
				int bw = (int)std::lround(420 * sx), bh = (int)std::lround(150 * sy);
				SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
				SDL_SetRenderDrawColor(ren, BG.r, BG.g, BG.b, 255);
				SDL_Rect box{cx - bh / 2, cy - bw / 2, bh, bw};
				SDL_RenderFillRect(ren, &box);
				// the lines' places as read: above the middle, at it, below it (upright y, in pixels)
				const int offs[3] = {-44, 2, 40};
				for (int i = 0; i < 3; i++)
				{
					SDL_Surface* sf = TTF_RenderUTF8_Blended(fonts[lines[i].size], lines[i].text.c_str(), lines[i].ink);
					if (!sf) continue;
					if (SDL_Texture* t = SDL_CreateTextureFromSurface(ren, sf))
					{
						// an upright line further down sits further left, turned clockwise (right, the other way)
						int dy = (int)std::lround(offs[i] * sy), dx = cw ? -dy : dy;
						int tw = std::min(sf->w, bw - (int)std::lround(24 * sx)), th = sf->h;
						SDL_Rect d{cx + dx - tw / 2, cy - th / 2, tw, th};
						SDL_RenderCopyEx(ren, t, nullptr, &d, cw ? 90 : 270, nullptr, SDL_FLIP_NONE);
						SDL_DestroyTexture(t);
					}
					SDL_FreeSurface(sf);
				}
			}
			drawFind();
			return;
		}
		drawBackIcon();
		std::string title = !textStatus.title.empty() ? textStatus.title : fileName(docPath);
		drawTextMid(title, 1, TEXT, 72, 24, 540);
		int pct = (int)std::lround(textStatus.fraction * 100);
		std::string sub = std::to_string(pct) + "%";
		if (!textStatus.encoding.empty()) sub += "  \xC2\xB7  " + textStatus.encoding;
		drawTextMid(sub, 0, DIM, 72, 50, 540);
		Rect bar = docBarRect(), track{bar.x, bar.y + 10, bar.w, 10};
		drawLevel(track, (int)std::lround(track.w * textStatus.fraction), TRACK, ACCENT);
		fillRound({track.x + (int)std::lround(track.w * textStatus.fraction) - 5, track.y - 4, 10, 18}, KNOB, R_KNOB);
		Rect pad = docPadRect();
		drawBox(pad, SURFACE, R_TILE);
		if (!textStatus.error.empty())
			drawTextMid(textStatus.error, 1, DANGER, W / 2, pad.y + pad.h / 2, pad.w - 40, 2);
		else if (!textStatus.open)
			drawTextMid("Opening" + std::string(SDL_GetTicks() / 400 % 4, '.'), 1, DIM, W / 2, pad.y + pad.h / 2, 0, 2);
		else
		{
			drawTextMid("Drag to scroll", 1, DIM, W / 2, pad.y + pad.h / 2 - 14, 0, 2);
			drawTextMid("Tap: the upper half back, the lower half on", 0, DIM_ON, W / 2, pad.y + pad.h / 2 + 14, 0, 2);
			if (textStatus.atEnd) drawTextMid("The end", 0, ACCENT, W / 2, pad.y + pad.h - 20, 0, 2);
		}
		// each with its key under it (as the photo viewer's), so Book is seen to be there
		struct { const char* label; const char* key; bool off; } btns[4] = {
			{"Contents", "Y", textStatus.contents.empty()}, {"Bookmarks", "X", false}, {"Options", "START", false},
			{"Book mode", "SELECT", false}};
		bool hints = p.settings.buttonHotkeys;
		for (int i = 0; i < 4; i++)
		{
			Rect b = docBtn(i);
			drawBox(b, docPressed == i ? PRESSED : BUTTON, R_BUTTON);
			bool hint = hints && *btns[i].key;
			drawText(btns[i].label, 1, btns[i].off ? DIM : TEXT, b.x + b.w / 2, b.y + (hint ? 5 : 15), b.w - 6, 2);
			if (hint) drawText(btns[i].key, 0, btns[i].off ? DIM : ACCENT, b.x + b.w / 2, b.y + 33, b.w - 6, 2);
		}
		if (findOn)
		{
			// Find: its bar over the buttons, the query and how to go on
			fill({0, docBtn(0).y - 4, W, H - docBtn(0).y + 4}, BG);
			const char* labels[3] = {"Previous", "Next", "Done"};
			const char* keys[3] = {"L", "R", "B"};
			for (int i = 0; i < 3; i++)
			{
				Rect r = docBtn(0);
				Rect bb{16 + i * 206, r.y, 196, r.h};
				drawBox(bb, BUTTON, R_BUTTON);
				drawText(labels[i], 1, TEXT, bb.x + bb.w / 2, bb.y + 5, bb.w - 6, 2);
				drawText(keys[i], 0, ACCENT, bb.x + bb.w / 2, bb.y + 33, bb.w - 6, 2);
			}
			drawTextMid("Find \u201C" + findQuery + "\u201D", 0, ACCENT, W / 2, docBtn(0).y + docBtn(0).h + 22, 560, 2);
		}
		else drawHints({{"UP/DOWN", "Scroll"}, {"L/R", "Page"}, {"L3", "Find"}, {"B", "Close"}});	// (the buttons above say their own keys)
		// Find's word (found, not found), over the scroll area a moment
		if (!docNote.empty() && SDL_GetTicks() - docNoteAt < 2000)
		{
			Rect pad = docPadRect();
			fillRound({pad.x + 40, pad.y + pad.h - 52, pad.w - 80, 40}, BG, R_CHIP);
			drawTextMid(docNote, 0, TEXT, W / 2, pad.y + pad.h - 32, pad.w - 100, 2);
		}
		if (docMenu != DOC_MENU_NONE) drawDocMenu(p);
		drawFind();
	}

	void Ui::drawDocPages(const Player& p)
	{
		int filter = std::clamp(p.settings.pdfFilter, 0, DOC_FILTERS - 1);
		if (mangaPages)
		{
			drawMangaPages(p);
			return;
		}
		if (readerStatus.fullscreen && readerBottom.on)
		{
			drawMangaFull();
			if (SDL_GetTicks() < docFullTagUntil)
			{
				fillRound({170, 420, 300, 40}, BG, R_CHIP);
				drawTextMid("SELECT or B: one screen", 0, TEXT, W / 2, 440, 0, 2);
			}
			if (docMenu != DOC_MENU_NONE) drawDocMenu(p);
			return;
		}
		drawBackIcon();
		std::string title = mangaReadChapterTitle.empty() ? fileName(docPath) : mangaReadChapterTitle;
		drawTextMid(title, 1, TEXT, 72, 24, 420);
		int pages = readerStatus.pages;
		if (pages > 0)
			drawTextMid(std::to_string(readerStatus.page + 1) + " / " + std::to_string(pages), 1, TEXT, W - 24, 24, 160, 1);
		bool single = readerStatus.mode != MANGA_STRIP;
		if (!readerStatus.error.empty())
		{
			drawTextMid(readerStatus.error, 1, DANGER, W / 2, 200, 560, 2);
			drawHints({{"B", "Close"}});
			return;
		}
		if (single)
		{
			Rect pan = docPanRect();
			fill(pan, SCREEN);
			if (docPan.page >= 0 && docPan.rgba && docPan.w > 0)
			{
				if (docPanTexPage != docPan.page || docPanTexSrc != docPan.rgba.get())
				{
					if (docPanTex) SDL_DestroyTexture(docPanTex);
					docPanTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, docPan.w, docPan.h);
					if (docPanTex)
					{
						SDL_UpdateTexture(docPanTex, nullptr, docPan.rgba->data(), docPan.w * 4);
						SDL_SetTextureScaleMode(docPanTex, SDL_ScaleModeLinear);
					}
					docPanTexPage = docPan.page;
					docPanTexSrc = docPan.rgba.get();
				}
				Rect tr = docPanThumb();
				if (docPanTex)
				{
					tintTexture(docPanTex, filter);
					SDL_Rect dst{(int)std::lround(tr.x * sx), (int)std::lround(tr.y * sy), (int)std::lround(tr.w * sx), (int)std::lround(tr.h * sy)};
					SDL_RenderCopy(ren, docPanTex, nullptr, &dst);
				}
				// the part on the top panel, boxed (all of it shown: no box)
				if (docPan.x1 - docPan.x0 < 0.995f || docPan.y1 - docPan.y0 < 0.995f)
				{
					int bx = tr.x + (int)std::lround(tr.w * docPan.x0), by = tr.y + (int)std::lround(tr.h * docPan.y0);
					int bw = std::max(4, (int)std::lround(tr.w * (docPan.x1 - docPan.x0)));
					int bh = std::max(4, (int)std::lround(tr.h * (docPan.y1 - docPan.y0)));
					fill({bx, by, bw, 2}, ACCENT);
					fill({bx, by + bh - 2, bw, 2}, ACCENT);
					fill({bx, by, 2, bh}, ACCENT);
					fill({bx + bw - 2, by, 2, bh}, ACCENT);
				}
			}
			else drawText(readerStatus.loading || !readerStatus.open ? "Loading" + std::string(SDL_GetTicks() / 400 % 4, '.') : "",
				1, DIM, pan.x + pan.w / 2, pan.y + pan.h / 2 - 12, 0, 2);
			drawText("Zoom " + std::to_string(readerStatus.zoomPct) + "%", 0, TEXT, pan.x, pan.y + pan.h + (miniUp ? 4 : 10), pan.w);
			drawText(readerStatus.zoomPct > 100 ? "Drag the box \xC2\xB7 double-tap: zoom out" : "Double-tap: zoom in", 0, DIM, pan.x,
				pan.y + pan.h + (miniUp ? 24 : 36), pan.w);
			const char* labels[9] = {"Prev", "Next", "Zoom -", "Zoom +", "Page fit", "Filter", "Pages", "Options", "Vertical scroll"};
			const char* keys[9] = {"L", "R", "DOWN", "UP", p.settings.mangaFit == MANGA_FIT_WIDTH ? "Width" : "Page", FILTER_NAMES[filter], "Y",
				"START", "X"};
			bool hints = p.settings.buttonHotkeys;
			for (int i = 0; i < 9; i++)
			{
				Rect b = docBtn(i);
				drawBox(b, docPressed == i ? PRESSED : BUTTON, R_BUTTON);
				bool off = (i == 0 && readerStatus.page <= 0) || (i == 1 && readerStatus.page + 1 >= pages) ||
					(i == 2 && readerStatus.zoomPct <= 100);
				std::string label = labels[i];
				bool hint = (hints && *keys[i]) || i == 4 || i == 5;	// (the fit's and the filter's say which one is on)
				drawText(label, 1, off ? DIM : TEXT, b.x + b.w / 2, b.y + (hint ? 5 : 15), b.w - 6, 2);
				if (hint) drawText(keys[i], 0, off ? DIM : ACCENT, b.x + b.w / 2, b.y + 33, b.w - 6, 2);
			}
			drawHints({{"LEFT/RIGHT", "Page"}, {"UP/DOWN", "Zoom"}, {"A", "2x"}, {"B", "Close"}});
		}
		else
		{
			Rect bar = docBarRect(), track{bar.x, bar.y + 10, bar.w, 10};
			int w = pages > 1 ? (int)std::lround(track.w * (readerStatus.page / (double)(pages - 1))) : 0;
			drawLevel(track, w, TRACK, ACCENT);
			fillRound({track.x + w - 5, track.y - 4, 10, 18}, KNOB, R_KNOB);
			Rect pad = docPadRect();
			drawBox(pad, SURFACE, R_TILE);
			if (readerStatus.failedHere) drawTextMid("A page couldn't be shown \xC2\xB7 A: try again", 1, DANGER, W / 2, pad.y + pad.h / 2, 560, 2);
			else drawTextMid(readerStatus.loading ? "Loading" + std::string(SDL_GetTicks() / 400 % 4, '.') : "Drag here to scroll", 1, DIM, W / 2,
				pad.y + pad.h / 2, 0, 2);
			// each with its key (or, the filter, its setting) under it, as the single page's
			const char* labels[4] = {"Single page", "Fullscreen", "Filter", "Pages"};
			const char* keys[4] = {"X", "SELECT", FILTER_NAMES[filter], "Y"};
			bool hints = p.settings.buttonHotkeys;
			for (int i = 0; i < 4; i++)
			{
				Rect b = docBtn(i);
				drawBox(b, docPressed == i ? PRESSED : BUTTON, R_BUTTON);
				bool hint = hints || i == 2;
				drawText(labels[i], 1, TEXT, b.x + b.w / 2, b.y + (hint ? 5 : 15), b.w - 6, 2);
				if (hint) drawText(keys[i], 0, ACCENT, b.x + b.w / 2, b.y + 33, b.w - 6, 2);
			}
			drawHints({{"UP/DOWN", "Zoom"}, {"LEFT/RIGHT", "Scroll"}, {"B", "Close"}});
		}
		if (docMenu != DOC_MENU_NONE) drawDocMenu(p);
	}

	std::string Ui::docSignature() const
	{
		char b[320];
		snprintf(b, sizeof b, "|doc%d %d,%d,%d %d%d%d%d %d|t%d %d %llu %d|r%d%d %d/%d m%d z%d f%d %d%d|pan%d %.3f,%.3f,%.3f,%.3f %p|%u",
			(int)page, docMenu, docMenuCursor, docMenuScroll, (int)docTouch, (int)docBar, (int)docPanTouch, docPressed,
			(int)docLive, (int)textStatus.open, (int)std::lround(textStatus.fraction * 1000), (unsigned long long)textStatus.place,
			(int)textStatus.atEnd, (int)readerStatus.open, (int)readerStatus.loading, readerStatus.page, readerStatus.pages,
			readerStatus.mode, readerStatus.zoomPct, (int)readerStatus.fullscreen, (int)readerStatus.failedHere, (int)mangaPages,
			docPan.page, docPan.x0, docPan.y0, docPan.x1, docPan.y1, (const void*)docPan.rgba.get(),
			SDL_GetTicks() < docFullTagUntil ? 1u : 0u);
		std::string s = b;
		s += findSignature();
		s += "|bk" + std::to_string(textStatus.book) + " " + std::to_string(textStatus.bottomGen) + (SDL_GetTicks() < docBookToastUntil ? "T" : "");
		// (a turn: a frame each step of it)
		if (docTurning()) s += "|turn" + std::to_string((SDL_GetTicks() - textStatus.turnAt) / 12) + " " + std::to_string((SDL_GetTicks() - textStatus.noticeAt) / 40);
		s += textStatus.error + "|" + textStatus.encoding + "|" + textEncoding + "|" + readerStatus.error;
		// (opening: the dots move)
		if ((page == Page::DocText && !textStatus.open) || (page == Page::DocPages && (readerStatus.loading || !readerStatus.open)))
			s += std::to_string(SDL_GetTicks() / 400);
		return s;
	}
}
