// Files' document viewers, the bottom panel's side: a TXT or a book of text (EPUB) on main's TextReader
// (Page::DocText), and a PDF or a comic EPUB on its MangaReader (Page::DocPages), read a page at a time with the photo
// viewer's pan area, or down a strip (fullscreen across both panels). Each document's place is kept in
// doc-places.conf (the state folder), so it opens where it was left.
#include "ui_internal.h"
#include "ui_docs.h"
#include "ui_kit.h"
#include "ui_reader.h"
#include "filesave.h"
#include "manga_ui_logic.h"
#include "epub.h"
#include "pagebook.h"
#include "stickscroll.h"
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
		const char* THEME_NAMES[TXT_PAGES] = {"Match theme", "Dark", "Light", "Sepia", "Black"};
		const char* FILTER_NAMES[DOC_FILTERS] = {"Off", "Warm", "Warmer", "Dim"};

		// the menus' rows
		enum { DT_FONT, DT_SIZE, DT_SPACING, DT_MARGINS, DT_THEME, DT_FILTER, DT_ENCODING, DT_GOTO, DP_VIEW, DP_FULL, DP_FILTER,
			DP_FIT, DP_ZOOM, DP_PAGES, DT_VIEW, DT_HOLD, DT_MARKS, DT_TURN, DT_FIND };
		constexpr int MENU_TOP = 72, MENU_ROW_H = 44;
		constexpr Uint32 TAP_MS = 350;

		// the reading filter as SDL's colour mod (the bottom panel's copies of the pages)
		void tintTexture(SDL_Texture* t, int filter, int dim)
		{
			float r, g, b;
			docFilterTint(filter, r, g, b, dim);
			SDL_SetTextureColorMod(t, (Uint8)(r * 255), (Uint8)(g * 255), (Uint8)(b * 255));
		}
	}

	// a Find under way (below: Find)
	struct Ui::Docs::FindJob
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
		return docs->open(path, p);
	}

	bool Ui::Docs::open(const std::string& path, Player& p)
	{
		bool comic = isComicName(path);
		if (isNetPath(path) || (!isDocumentName(path) && !comic)) return false;
		// a comic (CBZ, CBR) reads with the Manga reader's pages: only with it turned on
		if (comic && !p.settings.mangaOn)
		{
			ui.alert("Comics need the Manga reader", "Turn it on in Extensions to read CBZ and CBR files.");
			return true;
		}
		// a comic takes the memory a video uses (as Manga does): asked first, unless turned off (then the video stops)
		if (comic && !comicVideoOk && p.playing() && !p.playingAudio() && !p.playingPhoto())
		{
			auto go = [this, path](Player& pl) {
				ui.stopKeepsPage = true;	// (main's Stop must not take the page back to where the video was opened from)
				ui.stopPressed = true;
				comicVideoOk = true;
				open(path, pl);
				comicVideoOk = false;
				fprintf(stderr, "docs: the video stopped for a comic\n");
			};
			if (p.settings.mangaAskVideo) ui.askMangaGuard(p, "the comic", go);
			else go(p);
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
		if (ui.page != Page::DocText && ui.page != Page::DocPages) docFrom = ui.page;
		docPath = path;
		docLive = false;
		docMenu = DOC_MENU_NONE;
		docPressed = -1;
		docTouch = docPanTouch = false;
		std::string name = fileName(path);
		fprintf(stderr, "docs: open %s (%s)\n", name.c_str(), pictures ? "pages" : "text");
		p.addRecent(path);
		if (!pictures)
		{
			textRead = {path, kept && kept->position > 0 ? (uint64_t)kept->position : 0};
			textStatus = {};
			textStatus.title = name;
			ui.page = Page::DocText;
			return true;
		}
		int page0 = kept && kept->position > 0 ? (int)kept->position : 0;
		ui.pageReader->mangaRead = {"", name, "", name, path, true, page0, p.settings.pdfStrip ? MANGA_STRIP : MANGA_LTR, MANGA_FIT_PAGE};
		ui.pageReader->mangaReadSeriesId.clear();
		ui.pageReader->mangaReadChapterId.clear();
		ui.pageReader->mangaReadChapterTitle = name;
		ui.pageReader->mangaZoomAsked = 100;
		ui.pageReader->mangaPages = false;
		ui.pageReader->readerStatus = {};
		docPan = {};
		ui.page = Page::DocPages;
		return true;
	}

	int Ui::Docs::docPagesFilter(const Player& p) const
	{
		return ui.page == Page::DocPages ? p.settings.pdfFilter : -1;
	}

	void Ui::Docs::docPlacesLoad()
	{
		if (docPlaces) return;
		docPlaces = std::make_unique<ResumeStore>();
		if (ui.stateDirPath.empty()) return;
		std::ifstream f(ui.stateDirPath + "/doc-places.conf");
		std::stringstream ss;
		ss << f.rdbuf();
		docPlaces->parse(ss.str());
	}

	const ResumeEntry* Ui::Docs::placeOf(const std::string& path)
	{
		docPlacesLoad();
		return docPlaces->find(path);
	}

	void Ui::Docs::forgetPlace(const std::string& path)
	{
		docPlacesLoad();
		ResumeEntry e;
		e.path = path;
		docPlaces->put(e);
		docPlacesDirty = true;
	}

	void Ui::Docs::pathMoved(const std::string& from, const std::string& to)
	{
		docPlacesLoad();
		if (docPlaces->moved(from, to)) { docPlacesDirty = true; docPlacesSave(true); }
		docMarksLoad();
		bool marks = false;
		for (auto& m : docMarks)
		{
			std::string n;
			if (movedPath(m.path, from, to, n)) { m.path = n; marks = true; }
		}
		if (marks) docMarksSave();
		if (!docPath.empty()) { std::string n; if (movedPath(docPath, from, to, n)) docPath = n; }
	}

	void Ui::Docs::docPlacesSave(bool now)
	{
		if (!docPlaces || !docPlacesDirty || ui.stateDirPath.empty()) return;
		if (!now && SDL_GetTicks() - docSavedAt < 2000) return;
		// durable (a cut mid-write once left font bytes in the bookmarks file); throttled above to one per 2 s, and
		// the fsync itself is the writer's thread (this tick runs while a document is read, so it can't wait on the card)
		std::string path = ui.stateDirPath + "/doc-places.conf", text = docPlaces->serialize();
		if (writer) writer->write({{path, text}});
		else writeFileDurable(path, text);
		docPlacesDirty = false;
		docSavedAt = SDL_GetTicks();
	}

	void Ui::Docs::docFlush()
	{
		docRemember();
		docPlacesSave(true);
		if (writer) writer->flush(1000);
	}

	void Ui::Docs::docRemember()
	{
		if (docPath.empty() || !docLive) return;
		docPlacesLoad();
		ResumeEntry e;
		e.path = docPath;
		if (ui.page == Page::DocText)
		{
			if (!textStatus.open) return;
			e.position = (double)textStatus.place;
			e.duration = textStatus.fraction;
			e.aid = 1;	// (the kind: a text's place and share, not a page of so many)
		}
		else
		{
			if (!ui.pageReader->readerStatus.open || ui.pageReader->readerStatus.pages <= 0) return;
			e.position = ui.pageReader->readerStatus.page;
			e.duration = ui.pageReader->readerStatus.pages;
			e.aid = 2;	// (the kind: a page of so many)
		}
		const ResumeEntry* was = docPlaces->find(docPath);
		if (was && was->position == e.position && was->aid == e.aid && was->duration == e.duration) return;
		docPlaces->put(e);
		docPlacesDirty = true;
	}

	void Ui::Docs::docClose(Player& p)
	{
		(void)p;
		docRemember();
		docPlacesSave(true);
		if (ui.page == Page::DocText) textCloseAsked = true;
		else
		{
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Close, 0, (int64_t)SDL_GetTicks()});
			ui.pageReader->mangaPages = false;
			ui.pageReader->mpFree();
			ui.pageReader->mfFree();
			ui.pageReader->mangaZoomAsked = 100;
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
		docBookBar = false;
		docBookBarPressed = -1;
		docLive = false;
		if (findJob) findJob->cancel = true;
		findOn = false;
		docNote.clear();
		fprintf(stderr, "docs: closed %s\n", fileName(docPath).c_str());
		docPath.clear();
		ui.page = docFrom == Page::DocText || docFrom == Page::DocPages ? Page::Browse : docFrom;
	}

	void Ui::Docs::tick(Player& p)
	{
		if (p.settings.txtBook) docBookHoldLast = p.settings.txtBook;
		findTick();
		bool open = ui.page == Page::DocText ? textStatus.open : ui.pageReader->readerStatus.open;
		if (open) docLive = true;
		docRemember();
		docPlacesSave(false);
	}

	// ---- Find ----

	void Ui::Docs::runFind(FindJob* j)
	{
		j->hit = findInFile(j->path, j->epub, j->charset, j->query, j->from, j->back, j->found,
			[j] { return j->cancel.load(); }, [j](double f) { j->permille = (int)(f * 1000); });
		j->done = true;
	}

	void Ui::Docs::findTyped(const std::string& query)
	{
		findQuery = query;
		findStart(false);
	}

	void Ui::Docs::findStart(bool back)
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

	std::string Ui::Docs::docHighlight() const
	{
		return findOn && ui.page == Page::DocText ? findQuery : std::string();
	}

	void Ui::Docs::findTick()
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

	std::string Ui::Docs::findSignature() const
	{
		std::string s = "|find" + std::to_string(findOn) + findQuery;
		if (findJob) s += "~" + std::to_string(findJob->permille / 20);
		Uint32 now = SDL_GetTicks();
		if (!docNote.empty() && now - docNoteAt < 2000) s += "|note" + docNote + std::to_string((now - docNoteAt) / 100);
		return s;
	}

	void Ui::Docs::drawFind()
	{
		// a search running: over everything, how far, B stops it
		if (findJob)
		{
			Kit::BusySpec b;
			b.title = "Finding\u2026";
			b.file = "\u201C" + findQuery + "\u201D";
			b.share = findJob->permille / 1000.0;
			b.left = std::to_string(findJob->permille / 10) + "%";
			b.key = "Stop";
			ui.kit->busySheet(lay::busySheet(), b);
		}
	}

	// ---- bookmarks ----

	void Ui::Docs::docMarksLoad()
	{
		if (docMarksLoaded) return;
		docMarksLoaded = true;
		if (ui.stateDirPath.empty()) return;
		std::ifstream f(ui.stateDirPath + "/doc-bookmarks.conf");
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

	void Ui::Docs::docMarksSave()
	{
		if (ui.stateDirPath.empty()) return;
		std::string out = "# banana-player: bookmarks in documents (place, share, file, label)\n";
		for (auto& m : docMarks)
		{
			std::string label = m.label;
			for (char& ch : label) if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
			char head[64];
			snprintf(head, sizeof head, "%llu\t%.4f\t", (unsigned long long)m.id, m.frac);
			out += head + m.path + "\t" + label + "\n";
		}
		// one write per bookmark added, renamed or removed by the user: durable
		writeFileDurable(ui.stateDirPath + "/doc-bookmarks.conf", out);
	}

	std::vector<int> Ui::Docs::docMarksHere() const
	{
		std::vector<int> v;
		for (int i = 0; i < (int)docMarks.size(); i++)
			if (docMarks[i].path == docPath) v.push_back(i);
		std::sort(v.begin(), v.end(), [&](int a, int b) { return docMarks[a].id < docMarks[b].id; });
		return v;
	}

	void Ui::Docs::docMarksOpen()
	{
		docMarksLoad();
		docMenu = DOC_MENU_MARKS;
		docMenuCursor = docMenuScroll = 0;
		docMarkArmed = false;
	}

	// ---- the menus ----

	std::vector<int> Ui::Docs::docMenuRows() const
	{
		if (ui.page == Page::DocText)
		{
			std::vector<int> r = {DT_FIND, DT_MARKS, DT_VIEW};
			if (textStatus.book) { r.push_back(DT_HOLD); r.push_back(DT_TURN); }
			for (int id : {DT_FONT, DT_SIZE, DT_SPACING, DT_MARGINS, DT_THEME, DT_FILTER}) r.push_back(id);
			if (!textStatus.encoding.empty()) r.push_back(DT_ENCODING);
			return r;
		}
		bool strip = ui.pageReader->readerStatus.mode == MANGA_STRIP;
		std::vector<int> r = {DP_VIEW};
		if (strip) r.push_back(DP_FULL);
		r.push_back(DP_FILTER);
		if (!strip) { r.push_back(DP_FIT); r.push_back(DP_ZOOM); }
		r.push_back(DP_PAGES);
		return r;
	}

	void Ui::Docs::docMenuStep(int dir, Player& p)
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
		case DT_THEME: cyc(s.txtTheme, TXT_PAGES); break;
		case DT_FILTER: case DP_FILTER: docMenu = DOC_MENU_NONE; ui.openLight(); return;	// the Brightness sheet
		case DT_VIEW:
			// Scroll or Book, held as Book was last (the top panel on the left the first time)
			s.txtBook = s.txtBook ? 0 : (docBookHoldLast ? docBookHoldLast : MANGA_BOOK_LEFT);
			break;
		case DT_TURN: cyc(s.txtTurn, 2); break;
		case DT_HOLD:
			s.txtBook = s.txtBook == MANGA_BOOK_LEFT ? MANGA_BOOK_RIGHT : MANGA_BOOK_LEFT;
			docBookHoldLast = s.txtBook;
			break;
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
			bool strip = ui.pageReader->readerStatus.mode != MANGA_STRIP;
			// (each view opens at its fit: a page's zoom would cut a strip's lines at both edges)
			if (ui.pageReader->readerStatus.zoomPct != 100) ui.pageReader->mangaZoomTo(1.0);
			if (!strip && ui.pageReader->readerStatus.fullscreen) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, 0, (int64_t)SDL_GetTicks()});
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Mode, (double)(strip ? MANGA_STRIP : MANGA_LTR), (int64_t)SDL_GetTicks()});
			s.pdfStrip = strip;
			break;
		}
		case DP_FULL:
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, ui.pageReader->readerStatus.fullscreen ? 0.0 : 1.0, (int64_t)SDL_GetTicks(), (double)p.settings.mangaHinge});
			if (!ui.pageReader->readerStatus.fullscreen) docFullTagUntil = SDL_GetTicks() + 2500;
			return;
		case DP_FIT:
			cyc(s.mangaFit, 2);	// Page and Width (Fill crops a document's margins away)
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fit, (double)s.mangaFit, (int64_t)SDL_GetTicks()});
			break;
		case DP_ZOOM: ui.pageReader->mangaZoomStep(dir); return;
		default: return;
		}
		p.setSettings(s);
	}

	void Ui::Docs::docBookmarkHere(Player& p)
	{
		for (int i : docMarksHere())
			if (docMarks[i].id == textStatus.place) { p.osd("There's a bookmark here already", 1500); return; }
		DocMark m;
		m.path = docPath;
		m.id = textStatus.place;
		m.frac = textStatus.fraction;
		m.label = textStatus.snippet.empty() ? "Bookmark" : textStatus.snippet;
		docMarks.push_back(m);
		docMarksSave();
		p.osd("Bookmarked", 1200);
	}

	void Ui::Docs::docMenuAct(Player& p)
	{
		if (docMenu == DOC_MENU_MARKS)
		{
			// the first row adds one at the place; the rest go to theirs
			auto here = docMarksHere();
			if (docMenuCursor == 0)
			{
				docBookmarkHere(p);
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
			ui.pageReader->mangaPagesOpen(p);
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
			ui.openKeyboard(FIND_FIELD, findQuery);
			return;
		}
		docMenuStep(1, p);
	}

	void Ui::Docs::drawDocMenu(const Player& p)
	{
		bool contents = docMenu == DOC_MENU_CONTENTS, marks = docMenu == DOC_MENU_MARKS;
		// over the whole panel, its title where the page's was
		ui.fill({0, 0, W, H}, BG);
		ui.fill({0, 60, W, 2}, RULE);
		ui.drawTextMid(marks ? "Bookmarks" : contents ? "Contents" : ui.page == Page::DocText ? "Text options" : "Options", 1, TEXT, 24, 32, 400);
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
			if (at) ui.drawFocus(r);
			int mid = r.y + r.h / 2;
			SDL_Color ink = at ? ACCENT : TEXT;
			if (marks)
			{
				if (i == 0)
				{
					ui.drawGlyphMid(G_PLUS, 40, mid, ink);
					ui.drawTextMid("Add a bookmark here", 1, ink, 64, mid, 520);
					continue;
				}
				const DocMark& m = docMarks[here[i - 1]];
				ui.drawGlyphMid(G_BOOKMARK, 40, mid, at ? ACCENT : DIM);
				ui.drawTextMid(std::to_string((int)std::lround(m.frac * 100)) + "%", 1, ink, 64, mid, 70);
				bool armedHere = at && docMarkArmed;
				ui.drawTextMid(armedHere ? "X again to delete it" : m.label, 0, armedHere ? ACCENT : at ? TEXT : DIM, 140, mid, 470);
				continue;
			}
			if (contents)
			{
				const auto& c = textStatus.contents[i];
				ui.drawTextMid(c.first, 1, ink, 32 + std::min(c.second.second, 4) * 20, mid, 560 - std::min(c.second.second, 4) * 20);
				continue;
			}
			int id = rows[i];
			static const char* LABELS[] = {"Font", "Text size", "Line spacing", "Margins", "Colours", "Brightness & filter", "Encoding",
				"Go to", "View", "Fullscreen", "Brightness & filter", "Page fit", "Zoom", "Pages", "View", "Hold", "Bookmarks", "Page turn", "Find\u2026"};
			ui.drawTextMid(LABELS[id], 1, ink, 32, mid, 300);
			if (id == DT_FIND)
			{
				if (!findQuery.empty()) ui.drawTextMid("\u201C" + findQuery + "\u201D", 0, at ? ACCENT : DIM, 580, mid, 220, 1);
				ui.drawGlyphMid(G_RIGHT, 604, mid, at ? ACCENT : DIM);
				continue;
			}
			if (id == DP_PAGES || id == DT_MARKS || id == DT_FILTER || id == DP_FILTER)
			{
				// Brightness & filter opens its sheet: the filter on now beside it
				if (id == DT_FILTER || id == DP_FILTER)
					ui.drawTextMid(FILTER_NAMES[std::clamp(id == DT_FILTER ? p.settings.txtFilter : p.settings.pdfFilter, 0, DOC_FILTERS - 1)], 0,
						at ? ACCENT : DIM, 580, mid, 160, 1);
				if (id == DT_MARKS)
				{
					int nm = (int)docMarksHere().size();
					ui.drawTextMid(nm ? std::to_string(nm) : "None yet", 0, at ? ACCENT : DIM, 580, mid, 160, 1);
				}
				ui.drawGlyphMid(G_RIGHT, 604, mid, at ? ACCENT : DIM);
				continue;
			}
			std::string v;
			switch (id)
			{
			case DT_FONT: v = FONT_NAMES[std::clamp(p.settings.txtFont, 0, TXT_FONTS - 1)]; break;
			case DT_SIZE: v = SIZE_NAMES[std::clamp(p.settings.txtSize, 0, TXT_SIZES - 1)]; break;
			case DT_SPACING: v = SPACING_NAMES[std::clamp(p.settings.txtSpacing, 0, TXT_SPACINGS - 1)]; break;
			case DT_MARGINS: v = MARGIN_NAMES[std::clamp(p.settings.txtMargins, 0, TXT_MARGINS - 1)]; break;
			case DT_THEME: v = THEME_NAMES[std::clamp(p.settings.txtTheme, 0, TXT_PAGES - 1)]; break;
			case DT_FILTER: v = FILTER_NAMES[std::clamp(p.settings.txtFilter, 0, DOC_FILTERS - 1)]; break;
			case DP_FILTER: v = FILTER_NAMES[std::clamp(p.settings.pdfFilter, 0, DOC_FILTERS - 1)]; break;
			case DT_ENCODING: v = textEncoding.empty() ? "Auto (" + textStatus.encoding + ")" : textStatus.encoding; break;
			case DT_GOTO: v = std::to_string((int)std::lround(textStatus.fraction * 100)) + "%"; break;
			case DT_VIEW: v = p.settings.txtBook ? "Book mode" : "Scroll"; break;
			case DT_TURN: v = p.settings.txtTurn ? "Off" : "Fold"; break;
			case DT_HOLD: v = p.settings.txtBook == MANGA_BOOK_RIGHT ? "Top screen right" : "Top screen left"; break;
			case DP_VIEW: v = ui.pageReader->readerStatus.mode == MANGA_STRIP ? "Vertical scroll" : "Single page"; break;
			case DP_FULL: v = ui.pageReader->readerStatus.fullscreen ? "On" : "Off"; break;
			case DP_FIT: v = p.settings.mangaFit == MANGA_FIT_WIDTH ? "Width" : "Page"; break;
			case DP_ZOOM: v = std::to_string(ui.pageReader->readerStatus.zoomPct) + "%"; break;
			default: break;
			}
			ui.drawTextMid("<", 1, at ? ACCENT : DIM, SET_ARROW_X, mid);
			ui.drawTextMid(v, 1, ink, 490, mid, 196, 2);
			ui.drawTextMid(">", 1, at ? ACCENT : DIM, 608, mid, 0, 1);
		}
		if (n > shown) ui.drawScrollBar(MENU_TOP, shown * MENU_ROW_H, docMenuScroll, shown, n);
		if (marks) ui.drawHints({{"A", docMenuCursor == 0 ? "Add" : "Go"}, {"X", "Delete"}, {"B", "Close"}});
		else if (contents) ui.drawHints({{"A", "Go"}, {"Y/B", "Close"}});
		else ui.drawHints({{"LEFT/RIGHT", "Change"}, {"START/B", "Close"}});
	}

	// ---- the keys ----

	bool Ui::Docs::key(Button b, Player& p)
	{
		int64_t now = SDL_GetTicks();
		// a search running: B stops it, nothing else answers meanwhile
		if (findJob)
		{
			if (b == Button::B) findJob->cancel = true;
			return false;
		}
		// a match shown: R / L the next / previous, B leaves Find (the text stays where it is)
		if (ui.page == Page::DocText && findOn && docMenu == DOC_MENU_NONE)
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
		if (ui.page == Page::DocText && b == Button::L3 && docMenu == DOC_MENU_NONE)
		{
			ui.openKeyboard(FIND_FIELD, findQuery);
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
		if (ui.page == Page::DocText && textStatus.book)
		{
			// held sideways: UP and DOWN point along the spread (as the manga reader's Book); the shoulders turn too
			if (b == Button::Start) docBookBar = !docBookBar;
			else docBookBar = false;	// any other key acts on the page, and the bar goes
			switch (b)
			{
			case Button::Up: docBookTurn(-1, p); break;
			case Button::Down: docBookTurn(1, p); break;
			case Button::L: textCmds.push_back({TextReader::BookTurn, -1, now}); break;
			case Button::R: case Button::A: textCmds.push_back({TextReader::BookTurn, 1, now}); break;
			case Button::X: docMarksOpen(); break;
			// START: the Book bar, upright as the panel is held (its Text key opens the options)
			case Button::Start: break;
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
		if (ui.page == Page::DocText)
		{
			switch (b)
			{
			case Button::Up: textCmds.push_back({TextReader::Lines, -3, now}); break;
			case Button::Down: textCmds.push_back({TextReader::Lines, 3, now}); break;
			case Button::Left: case Button::L: textCmds.push_back({TextReader::Page, -1, now}); break;
			case Button::Right: case Button::R: case Button::A: textCmds.push_back({TextReader::Page, 1, now}); break;
			case Button::X: docMarksOpen(); break;
			// (START is Settings, from Ui::press; the options are the on-screen Options key)
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
					break;
			}
			case Button::B: docClose(p); break;
			default: break;
			}
			return false;
		}
		// the PDF viewer
		if (ui.pageReader->mangaPages) return ui.pageReader->mangaPagesKey(b, p);
		bool strip = ui.pageReader->readerStatus.mode == MANGA_STRIP;
		if (ui.pageReader->readerStatus.fullscreen && (b == Button::B || b == Button::Select))
		{
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, 0, now});
			return false;
		}
		switch (b)
		{
		case Button::Left: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Step, -1, now}); break;
		case Button::Right: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Step, 1, now}); break;
		case Button::L: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Page, -1, now}); break;
		case Button::R: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Page, 1, now}); break;
		// zoom in either view, as the manga reader and the photo viewer do (the finger, LEFT/RIGHT and A move on)
		case Button::Up: ui.pageReader->mangaZoomStep(1); break;
		case Button::Down: ui.pageReader->mangaZoomStep(-1); break;
		case Button::A:
			if (ui.pageReader->readerStatus.failedHere) ui.pageReader->mangaRetry();
			else if (strip) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Step, 1, now});
			else ui.pageReader->mangaZoomToggle();
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
				ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, 1, now, (double)p.settings.mangaHinge});
				docFullTagUntil = SDL_GetTicks() + 2500;
			}
			break;
		case Button::Y: ui.pageReader->mangaPagesOpen(p); break;
		// START is Settings (Ui::press), except held sideways: there it is the Book bar's own key
		case Button::Start:
			if (ui.pageReader->readerStatus.book) docMenu = DOC_MENU_OPTIONS;
			docMenuCursor = docMenuScroll = 0;
			break;
		case Button::B: docClose(p); break;
		default: break;
		}
		return false;
	}

	void Ui::Docs::stickAxis(int value, Player& p)
	{
		int64_t now = SDL_GetTicks();
		bool text = ui.page == Page::DocText && textStatus.open && docMenu == DOC_MENU_NONE && !findJob;
		bool pages = ui.page == Page::DocPages && ui.pageReader->readerStatus.open && docMenu == DOC_MENU_NONE &&
			!ui.pageReader->mangaPages;
		// Book is held sideways: one push is one spread, turned the way its D-pad points (not scrolled)
		if (text && textStatus.book)
		{
			if (stickTarget == 1) textCmds.push_back({TextReader::Stick, 0, now});
			else if (stickTarget == 2) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Stick, 0, now});
			stickTarget = 0;
			double speed = stickScrollSpeed(value, 1.0);
			int dy = speed == 0 ? 0 : speed > 0 ? 1 : -1;
			if (dy != 0 && dy != stickDir) docBookTurn(dy, p);
			stickDir = dy;
			return;
		}
		stickDir = 0;
		int target = text ? 1 : pages ? 2 : 0;	// 1 the text, 2 the pages (its reader)
		if (target != stickTarget)
		{
			// the reader left stops at once, and the one taking over starts from the value held
			if (stickTarget == 1) textCmds.push_back({TextReader::Stick, 0, now});
			else if (stickTarget == 2) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Stick, 0, now});
			stickTarget = target;
		}
		// every pass while it is the stick's own reader: a chapter or a page that opens under a held stick is
		// picked up at once, and the reader ignores the same value as before
		if (target == 1) textCmds.push_back({TextReader::Stick, (double)value, now});
		else if (target == 2) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Stick, (double)value, now});
	}

	void Ui::Docs::docBookTurn(int dpadDy, Player& p)
	{
		int step = mangaBookStep(textStatus.book, false, false, dpadDy);
		if (step) textCmds.push_back({TextReader::BookTurn, (double)step, (int64_t)SDL_GetTicks()});
		(void)p;
	}

	// ---- the finger ----

	Ui::Rect Ui::Docs::docBarRect() const { return {24, 66, W - 48, 30}; }
	Ui::Rect Ui::Docs::docPadRect() const { return {16, 104, W - 32, 396 - 104 - 64}; }
	Ui::Rect Ui::Docs::docBtn(int i) const
	{
		if (ui.page == Page::DocPages && ui.pageReader->readerStatus.mode != MANGA_STRIP)
		{
			// beside the pan area, as the photo viewer's; the ninth, wide under them: the other view
			if (i == 8) return {368, 336, 248, 52};
			return gridCell({368, 80, 120, 56}, 2, 8, 8, i);
		}
		int y = 396 - 56;
		// the text reader: four across 554, then Brightness's sun (50, 4 clear of them, ending at 624)
		if (ui.page == Page::DocText) return i == 4 ? Rect{574, y, 50, 56} : rowCell({16, y, 554, 56}, 4, 6, i);
		return rowCell({16, y, 608, 56}, 4, 8, i);
	}

	Ui::Rect Ui::Docs::docPanThumb() const
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

	bool Ui::Docs::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		if (docMenu != DOC_MENU_NONE)
		{
			// the list follows a finger's drag (as the manga reader's lists); a tap acts on the row as the finger lifts
			if (pt.kind != Pointer::Down) return ui.pageReader->mangaListPointer(pt, x, y);
			int n = docMenu == DOC_MENU_MARKS ? (int)docMarksHere().size() + 1
				: docMenu == DOC_MENU_CONTENTS ? (int)textStatus.contents.size() : (int)docMenuRows().size();
			int shown = std::max(1, (H - MENU_TOP - 40) / MENU_ROW_H);
			int r = (y - MENU_TOP) / MENU_ROW_H + docMenuScroll;
			if (y < 60) { docMenu = DOC_MENU_NONE; return false; }
			if (y < MENU_TOP || r < 0 || r >= n) r = -1;
			ui.pageReader->mangaListBegin(x, y, docMenuScroll, MENU_ROW_H, r,
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
		if (ui.page == Page::DocPages && ui.pageReader->mangaPages) return ui.pageReader->mangaPagesPointer(pt, x, y, p);
		if (findJob) return false;
		// the find bar over the buttons: Previous, Next, Done
		if (ui.page == Page::DocText && findOn && !textStatus.book && pt.kind == Pointer::Up)
			for (int i = 0; i < 3; i++)
			{
				Rect r = docBtn(0);
				Rect bb{16 + i * 206, r.y, 196, r.h};
				if (bb.hit(x, y))
				{
					if (i < 2) key(i == 0 ? Button::L : Button::R, p);
					else findOn = false;
					return false;
				}
			}
		if (ui.page == Page::DocText && textStatus.book)
		{
			// the bar, when it shows, takes the touches first
			if (docBookBar) return docBookBarPointer(pt, x, y, p);
			// the panel is a page: a swipe along the spread turns it (as the manga reader's Book); a tap near the
			// top or the bottom of the panel turns the way UP or DOWN would, a tap in the middle shows the Book bar
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
					else docBookBar = true;	// the Book bar (its Text key opens the options)
				}
			}
			return false;
		}
		if (pt.kind == Pointer::Down && ui.buttonRect(BACK_ICON).hit(x, y) && !(ui.page == Page::DocPages && ui.pageReader->readerStatus.fullscreen))
		{
			docClose(p);
			return false;
		}
		{
			// the readers' header keys: a book's bookmark, and More (the options menu), as the header draws them
			lay::ReaderHeader h = lay::readerHeader({0, 0, W, lay::SHELL_HEAD}, ui.page == Page::DocText);
			if (pt.kind == Pointer::Down && h.moreHit.hit(x, y))
			{
				docMenu = DOC_MENU_OPTIONS;
				docMenuCursor = docMenuScroll = 0;
				return false;
			}
			if (pt.kind == Pointer::Down && ui.page == Page::DocText && h.bookmarkHit.hit(x, y))
			{
				docBookmarkHere(p);
				return false;
			}
		}
		bool text = ui.page == Page::DocText;
		bool full = !text && ui.pageReader->readerStatus.fullscreen;
		bool single = !text && ui.pageReader->readerStatus.mode != MANGA_STRIP;
		int nBtn = text ? 5 : single ? 9 : 4;
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
				ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::TouchDown, 0, (int64_t)now});
				return false;
			}
			if (text && docPadRect().hit(x, y))
			{
				docTouch = true;
				textCmds.push_back({TextReader::Hold, 0, (int64_t)now});
				return false;
			}
			if ((text || !single) && docBarHit().hit(x, y))
			{
				// the bar: the text goes with the finger; the pages (decoded as they come) at the scrub's pace, the
				// page it's let go on always
				if (text)
					ui.slideGrab(SL_DOC, 0, {docBarTrack(), {0, 1, 0}, false, 0, textStatus.fraction}, x, y, p, [this](double f, bool, Player&) {
						textCmds.push_back({TextReader::Jump, f, (int64_t)SDL_GetTicks()});
					});
				else if (ui.pageReader->readerStatus.pages > 1)
				{
					ui.pageReader->mangaSliderPage = -1;
					ui.slideGrab(SL_DOC, 0, {docBarTrack(), {0, (double)(ui.pageReader->readerStatus.pages - 1), 1}, false, SCRUB_MS, (double)ui.pageReader->readerStatus.page}, x, y, p,
						[this](double v, bool, Player&) { ui.pageReader->mangaSeek((int)v); });
				}
				return false;
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
				if (docMoved && (dy != 0 || dx != 0)) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Drag, dy, (int64_t)now, dx});
			}
			else if (docPanTouch && docMoved)
			{
				// the box goes with the finger over the small page; the top panel shows what's in it
				Rect tr = docPanThumb();
				double fx = docPanStartX + (x - docDownX) / (double)tr.w, fy = docPanStartY + (y - docDownY) / (double)tr.h;
				ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Pan, fx, (int64_t)now, fy});
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
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Release, 0, (int64_t)now});
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
				bool zoomed = ui.pageReader->readerStatus.zoomPct > 100;
				ui.pageReader->mangaZoomTo(zoomed ? 1.0 : 2.0);
				if (!zoomed) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Pan, fx, (int64_t)now, fy});
				return false;
			}
			docTapAt = now;
			// a tap, zoomed in: the view there
			if (ui.pageReader->readerStatus.zoomPct > 100) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Pan, fx, (int64_t)now, fy});
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
			case 0: key(Button::Y, p); break;	// Contents
			case 1: docMarksOpen(); break;	// Bookmarks
			case 2: key(Button::Start, p); break;	// Text options
			case 3: key(Button::Select, p); break;	// Book
			case 4: ui.openLight(); break;	// Brightness
			}
			return false;
		}
		if (single)
		{
			// (4: the page fit, saying its value; 5: Brightness; 8, under them: Vertical scroll, as X)
			static const Button KEYS[9] = {Button::L, Button::R, Button::Down, Button::Up, Button::None, Button::None, Button::Y, Button::Start, Button::X};
			if (i == 5) ui.openLight();
			else if (i == 4)
			{
				Settings s = p.settings;
				s.mangaFit = s.mangaFit == MANGA_FIT_WIDTH ? MANGA_FIT_PAGE : MANGA_FIT_WIDTH;
				ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fit, (double)s.mangaFit, (int64_t)SDL_GetTicks()});
				p.setSettings(s);
			}
			else key(KEYS[i], p);
			return false;
		}
		switch (i)
		{
		case 0: key(Button::X, p); break;	// Single page
		case 1: key(Button::Select, p); break;	// Fullscreen
		case 2: ui.openLight(); break;	// Brightness
		case 3: key(Button::Y, p); break;	// Pages
		}
		return false;
	}

	// ---- drawing ----

	void Ui::Docs::draw(const Player& p)
	{
		if (ui.page == Page::DocText) drawDocText(p);
		else drawDocPages(p);
	}

	void Ui::Docs::drawDocBookBar(const Player& p)
	{
		lay::BookBar b = lay::bookBar();
		KitNew::BookSpec s;
		s.title = !textStatus.title.empty() ? textStatus.title : fileName(docPath);
		int pct = (int)std::lround(textStatus.fraction * 100);
		s.value = std::to_string(pct) + "%";
		// the chapter at the place (a TXT has none: the bar's own line stays empty)
		int ci = -1;
		for (int i = 0; i < (int)textStatus.contents.size(); i++)
			if (textStatus.contents[i].second.first <= textStatus.place &&
				(ci < 0 || textStatus.contents[i].second.first > textStatus.contents[ci].second.first)) ci = i;
		if (ci >= 0) s.note = "Chapter " + std::to_string(ci + 1) + " of " + std::to_string((int)textStatus.contents.size());
		s.share = textStatus.fraction;
		s.keys[0] = {&G_CHAPTERS, "Contents", "Y", ""};
		s.keys[1] = {&G_PAGE, "Text", "SELECT", ""};
		s.keys[2] = {&G_SUN, "Light", "", ui.lightNote(p)};
		s.keys[3] = {&G_SEARCH, "Find", "L3", ""};
		s.leave = "Back to scrolling";
		s.leaveKey = "B";
		s.hints = {{"UP/DOWN", "Turn"}, {"A", "On"}, {"START", "Close"}};
		s.topLeft = textStatus.book == MANGA_BOOK_LEFT;
		s.pressed = docBookBarPressed;
		KitNew::bookBar(*ui.kit, b, s);
	}

	bool Ui::Docs::docBookBarPointer(const Pointer& pt, int x, int y, Player& p)
	{
		lay::BookBar b = lay::bookBar();
		bool topLeft = textStatus.book == MANGA_BOOK_LEFT;
		int ux = 0, uy = 0;
		lay::uprightPoint(x, y, topLeft, ux, uy);
		int at = lay::bookBarAt(b, ux, uy);
		if (pt.kind == Pointer::Down)
		{
			docBookBarPressed = at >= 0 && at <= 4 ? at : -1;
			if (at < 0) { docBookBar = false; docTouch = false; }	// a tap off the sheet puts it away
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			int was = docBookBarPressed;
			docBookBarPressed = -1;
			if (was >= 0 && at == was) docBookBarAct(was, p);
		}
		return false;
	}

	void Ui::Docs::docBookBarAct(int which, Player& p)
	{
		docBookBar = false;
		switch (which)
		{
		case 0:	// Contents: as Y
			if (!textStatus.contents.empty())
			{
				docMenu = DOC_MENU_CONTENTS;
				docMenuCursor = 0;
				for (int i = 0; i < (int)textStatus.contents.size(); i++)
					if (textStatus.contents[i].second.first <= textStatus.place &&
						(i == 0 || textStatus.contents[i].second.first > textStatus.contents[docMenuCursor].second.first))
						docMenuCursor = i;
				docMenuScroll = std::max(0, docMenuCursor - 3);
			}
			else docMarksOpen();
			break;
		case 1:	// Text: the options (as the key row's)
			docMenu = DOC_MENU_OPTIONS;
			docMenuCursor = docMenuScroll = 0;
			break;
		case 2: ui.openLight(); break;
		case 3: ui.openKeyboard(FIND_FIELD, findQuery); break;
		case 4:	// Back to scrolling
		{
			Settings s = p.settings;
			s.txtBook = 0;
			p.setSettings(s);
			break;
		}
		default: break;
		}
	}

	void Ui::Docs::drawDocText(const Player& p)
	{
		Kit& k = *ui.kit;
		if (textStatus.book && textStatus.bottom && docMenu == DOC_MENU_NONE)
		{
			// Book: this panel is the other page, already turned the way the handheld is held
			if (docBookTexGen != textStatus.bottomGen || !docBookTex)
			{
				if (docBookTex) SDL_DestroyTexture(docBookTex);
				docBookTex = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, textStatus.bottomW, textStatus.bottomH);
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
				docBookOldTex = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, textStatus.bottomW, textStatus.bottomH);
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
				tintTexture(t, filter, p.settings.readDim);
				SDL_Rect d{0, 0, pw, (int)std::lround(ph * frac)};
				SDL_RenderCopy(ui.ren, t, nullptr, &d);
			};
			auto ease = [](double t) { return t * t * (3 - 2 * t); };
			double tp = textStatus.turnDir ? std::clamp((SDL_GetTicks() - textStatus.turnAt) / (double)textStatus.turnMs, 0.0, 1.0) : 1;
			// (the top folds when it holds the page going first: the right-hand one going on, the left-hand going back)
			bool botFolds = !((textStatus.turnDir > 0) == textStatus.rightOnTop);
			if (textStatus.turnDir && old && docBookOldTex && tp < 1)
			{
				SDL_SetRenderDrawColor(ui.ren, 0, 0, 0, 255);
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
				if (SDL_Surface* sf = TTF_RenderUTF8_Blended(ui.fonts[0], text.c_str(), TEXT))
				{
					if (SDL_Texture* t = SDL_CreateTextureFromSurface(ui.ren, sf))
					{
						// a dark pill behind it, turned as the text is (its sides swapped on the panel)
						int tw = sf->w, th = sf->h, padX = (int)std::lround(14 * sx), padY = (int)std::lround(5 * sy);
						int bw = tw + 2 * padX, bh = th + 2 * padY;
						// just above the page's own % at its foot (not over the text in the middle): the page's foot is
						// this panel's left edge for a page turned clockwise, its right edge for one turned the other way
						int lift = (int)std::lround(pw * 0.06) + bh / 2;
						int cx = textStatus.book == MANGA_BOOK_RIGHT ? pw - lift : lift, cy = ph / 2;
						SDL_SetRenderDrawBlendMode(ui.ren, SDL_BLENDMODE_BLEND);
						SDL_SetRenderDrawColor(ui.ren, BG.r, BG.g, BG.b, (Uint8)(alpha * 220 / 255));
						SDL_Rect pill{cx - bh / 2, cy - bw / 2, bh, bw};
						SDL_RenderFillRect(ui.ren, &pill);
						SDL_SetTextureAlphaMod(t, (Uint8)alpha);
						SDL_Rect d{cx - tw / 2, cy - th / 2, tw, th};
						SDL_RenderCopyEx(ui.ren, t, nullptr, &d, textStatus.book == MANGA_BOOK_RIGHT ? 270 : 90, nullptr, SDL_FLIP_NONE);
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
				SDL_SetRenderDrawBlendMode(ui.ren, SDL_BLENDMODE_BLEND);
				SDL_SetRenderDrawColor(ui.ren, BG.r, BG.g, BG.b, 255);
				SDL_Rect box{cx - bh / 2, cy - bw / 2, bh, bw};
				SDL_RenderFillRect(ui.ren, &box);
				// the lines' places as read: above the middle, at it, below it (upright y, in pixels)
				const int offs[3] = {-44, 2, 40};
				for (int i = 0; i < 3; i++)
				{
					SDL_Surface* sf = TTF_RenderUTF8_Blended(ui.fonts[lines[i].size], lines[i].text.c_str(), lines[i].ink);
					if (!sf) continue;
					if (SDL_Texture* t = SDL_CreateTextureFromSurface(ui.ren, sf))
					{
						// an upright line further down sits further left, turned clockwise (right, the other way)
						int dy = (int)std::lround(offs[i] * sy), dx = cw ? -dy : dy;
						int tw = std::min(sf->w, bw - (int)std::lround(24 * sx)), th = sf->h;
						SDL_Rect d{cx + dx - tw / 2, cy - th / 2, tw, th};
						SDL_RenderCopyEx(ui.ren, t, nullptr, &d, cw ? 90 : 270, nullptr, SDL_FLIP_NONE);
						SDL_DestroyTexture(t);
					}
					SDL_FreeSurface(sf);
				}
			}
			drawFind();
			if (docBookBar) drawDocBookBar(p);
			return;
		}
		// the header (docs/redesign, reader): the book's name at the readers' 18 units beside the back key, its own
		// line under it, and at the right the bookmark key and More
		docBookBar = false;	// (a bar left open when Book went: the scroll view has no bar)
		lay::ReaderHeader h = lay::readerHeader({0, 0, W, lay::SHELL_HEAD}, true);
		Kit::Header hd;
		hd.title = !textStatus.title.empty() ? textStatus.title : fileName(docPath);
		int pct = (int)std::lround(textStatus.fraction * 100);
		hd.crumb = std::to_string(pct) + "%";
		if (!textStatus.encoding.empty()) hd.crumb += "  \xC2\xB7  " + textStatus.encoding;
		hd.titleFont = F_SMALL;
		hd.inlineTitle = true;
		hd.more = true;
		hd.right = [&](int, int) { return h.bookmark.x - lay::SP8; };
		ui.kit->header(h.strip, hd);
		ui.kit->iconKey(h.bookmark, G_BOOKMARK, docPressed == 200, 2, true);
		ui.drawGlyphMid(G_BOOKMARK, h.bookmark.x + h.bookmark.w / 2, h.bookmark.y + h.bookmark.h / 2, ACCENT, 2);
		{
			// under a finger: where the finger has it (the text follows at once, its own figure a moment behind)
			double share = ui.slideShare(SL_DOC, 0, textStatus.fraction);
			ui.drawSlider(docBarTrack(), share, ui.slideHeld(SL_DOC), std::to_string((int)std::lround(share * 100)) + "%");
		}
		Rect pad = docPadRect();
		ui.drawBox(pad, SURFACE, R_TILE);
		if (!textStatus.error.empty())
			ui.drawTextMid(textStatus.error, 1, DANGER, W / 2, pad.y + pad.h / 2, pad.w - 40, 2);
		else if (!textStatus.open)
			ui.drawTextMid("Opening" + std::string(SDL_GetTicks() / 400 % 4, '.'), 1, DIM, W / 2, pad.y + pad.h / 2, 0, 2);
		else
		{
			ui.drawTextMid("Drag to scroll", 1, DIM, W / 2, pad.y + pad.h / 2 - 14, 0, 2);
			ui.drawTextMid("Tap: the upper half back, the lower half on", 0, DIM_ON, W / 2, pad.y + pad.h / 2 + 14, 0, 2);
			if (textStatus.atEnd) ui.drawTextMid("The end", 0, ACCENT, W / 2, pad.y + pad.h - 20, 0, 2);
		}
		// each with its key under it (as the photo viewer's), so Book is seen to be there
		struct { const char* label; const char* key; bool off; } btns[5] = {
			{"Contents", "Y", textStatus.contents.empty()}, {"Bookmarks", "X", false}, {"Options", "", false},
			{"Book mode", "SELECT", false}, {"Brightness", "", false}};
		bool hints = p.settings.buttonHotkeys;
		for (int i = 0; i < 5; i++)
		{
			// a reader's key row: keys in Clean too (components.md "Clean style", rule 7), drawn by the kit
			Rect b = docBtn(i);
			Kit::Key kk;
			kk.label = btns[i].label;
			kk.key = hints ? btns[i].key : "";
			kk.off = btns[i].off;
			kk.pressed = docPressed == i;
			if (i == 4)
			{
				// Brightness: its sun, lit with a dot while Extra dim or a filter is on; the state under it when it fits
				bool tweaked = !ui.lightNote(p).empty();
				kk.label.clear();
				kk.key.clear();
				kk.glyph = &G_SUN;
				kk.lit = tweaked;
				k.key(b, kk);
				if (tweaked)
				{
					ui.fillRound({b.x + b.w - 12, b.y + 5, 7, 7}, ACCENT, 3);
					std::string note = ui.lightNote(p);
					if (k.textW(note, F_I11B) <= b.w - 4) ui.drawTextMid(note, F_I11B, ACCENT, b.x + b.w / 2, b.y + b.h - 8, b.w - 4, 2);
				}
				continue;
			}
			k.key(b, kk);
		}
		if (findOn)
		{
			// Find: its bar over the buttons, the query and how to go on
			ui.fill({0, docBtn(0).y - 4, W, H - docBtn(0).y + 4}, BG);
			const char* labels[3] = {"Previous", "Next", "Done"};
			const char* keys[3] = {"L", "R", "B"};
			for (int i = 0; i < 3; i++)
			{
				Rect r = docBtn(0);
				Rect bb = rowCell({16, r.y, 608, r.h}, 3, 10, i);
				Kit::Key kk;
				kk.label = labels[i];
				kk.key = keys[i];
				k.key(bb, kk);
			}
			ui.drawTextMid("Find \u201C" + findQuery + "\u201D", 0, ACCENT, W / 2, docBtn(0).y + docBtn(0).h + 22, 560, 2);
		}
		// (the buttons above say their own keys; START is Settings, as the redesign's text reader has it)
		else ui.drawHints({{"UP/DOWN", "Scroll"}, {"L/R", "Page"}, {"L3", "Find"}, {"B", "Close"}, {"START", "Settings"}});
		// Find's word (found, not found), over the scroll area a moment
		if (!docNote.empty() && SDL_GetTicks() - docNoteAt < 2000)
		{
			Rect pad = docPadRect();
			ui.fillRound({pad.x + 40, pad.y + pad.h - 52, pad.w - 80, 40}, BG, R_CHIP);
			ui.drawTextMid(docNote, 0, TEXT, W / 2, pad.y + pad.h - 32, pad.w - 100, 2);
		}
		if (docMenu != DOC_MENU_NONE) drawDocMenu(p);
		drawFind();
	}

	void Ui::Docs::drawDocPages(const Player& p)
	{
		Kit& k = *ui.kit;
		int filter = std::clamp(p.settings.pdfFilter, 0, DOC_FILTERS - 1);
		if (ui.pageReader->mangaPages)
		{
			ui.pageReader->drawMangaPages(p);
			return;
		}
		if (ui.pageReader->readerStatus.fullscreen && ui.pageReader->readerBottom.on)
		{
			ui.pageReader->drawMangaFull();
			if (SDL_GetTicks() < docFullTagUntil)
			{
				ui.fillRound({170, 420, 300, 40}, BG, R_CHIP);
				ui.drawTextMid("SELECT or B: one screen", 0, TEXT, W / 2, 440, 0, 2);
			}
			if (docMenu != DOC_MENU_NONE) drawDocMenu(p);
			return;
		}
		// the header: the document's name at 18 units, its line under it (the page, the zoom), More at the right
		lay::ReaderHeader h = lay::readerHeader({0, 0, W, lay::SHELL_HEAD}, false);
		Kit::Header hd;
		hd.title = ui.pageReader->mangaReadChapterTitle.empty() ? fileName(docPath) : ui.pageReader->mangaReadChapterTitle;
		int pages = ui.pageReader->readerStatus.pages;
		if (pages > 0)
			hd.crumb = "Page " + std::to_string((ui.slideHeld(SL_DOC) ? (int)ui.slide.value : ui.pageReader->readerStatus.page) + 1) +
				" of " + std::to_string(pages);
		if (ui.pageReader->readerStatus.zoomPct != 100)
			hd.crumb += std::string(hd.crumb.empty() ? "" : "  \xC2\xB7  ") + std::to_string(ui.pageReader->readerStatus.zoomPct) + "%";
		if (hd.crumb.empty()) hd.crumb = fileName(docPath);
		hd.titleFont = F_SMALL;
		hd.inlineTitle = true;
		hd.more = true;
		ui.kit->header(h.strip, hd);
		bool single = ui.pageReader->readerStatus.mode != MANGA_STRIP;
		if (!ui.pageReader->readerStatus.error.empty())
		{
			ui.drawTextMid(ui.pageReader->readerStatus.error, 1, DANGER, W / 2, 200, 560, 2);
			ui.drawHints({{"B", "Close"}});
			return;
		}
		if (single)
		{
			Rect pan = docPanRect();
			ui.fill(pan, SCREEN);
			if (docPan.page >= 0 && docPan.rgba && docPan.w > 0)
			{
				if (docPanTexPage != docPan.page || docPanTexSrc != docPan.rgba.get())
				{
					if (docPanTex) SDL_DestroyTexture(docPanTex);
					docPanTex = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, docPan.w, docPan.h);
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
					tintTexture(docPanTex, filter, p.settings.readDim);
					SDL_Rect dst{(int)std::lround(tr.x * sx), (int)std::lround(tr.y * sy), (int)std::lround(tr.w * sx), (int)std::lround(tr.h * sy)};
					SDL_RenderCopy(ui.ren, docPanTex, nullptr, &dst);
				}
				// the part on the top panel, boxed (all of it shown: no box)
				if (docPan.x1 - docPan.x0 < 0.995f || docPan.y1 - docPan.y0 < 0.995f)
				{
					int bx = tr.x + (int)std::lround(tr.w * docPan.x0), by = tr.y + (int)std::lround(tr.h * docPan.y0);
					int bw = std::max(4, (int)std::lround(tr.w * (docPan.x1 - docPan.x0)));
					int bh = std::max(4, (int)std::lround(tr.h * (docPan.y1 - docPan.y0)));
					ui.fill({bx, by, bw, 2}, ACCENT);
					ui.fill({bx, by + bh - 2, bw, 2}, ACCENT);
					ui.fill({bx, by, 2, bh}, ACCENT);
					ui.fill({bx + bw - 2, by, 2, bh}, ACCENT);
				}
			}
			else ui.drawText(ui.pageReader->readerStatus.loading || !ui.pageReader->readerStatus.open ? "Loading" + std::string(SDL_GetTicks() / 400 % 4, '.') : "",
				1, DIM, pan.x + pan.w / 2, pan.y + pan.h / 2 - 12, 0, 2);
			ui.drawText("Zoom " + std::to_string(ui.pageReader->readerStatus.zoomPct) + "%", 0, TEXT, pan.x, pan.y + pan.h + 10, pan.w);
			ui.drawText(ui.pageReader->readerStatus.zoomPct > 100 ? "Drag the box \xC2\xB7 double-tap: zoom out" : "Double-tap: zoom in", 0, DIM, pan.x,
				pan.y + pan.h + 36, pan.w);
			const char* labels[9] = {"Prev", "Next", "Zoom -", "Zoom +", "Page fit", "Brightness", "Pages", "Options", "Vertical scroll"};
			const char* keys[9] = {"L", "R", "DOWN", "UP", p.settings.mangaFit == MANGA_FIT_WIDTH ? "Width" : "Page", "", "Y",
				"", "X"};
			bool hints = p.settings.buttonHotkeys;
			for (int i = 0; i < 9; i++)
			{
				// the page reader's key row: keys in Clean too, drawn by the kit
				Rect b = docBtn(i);
				bool off = (i == 0 && ui.pageReader->readerStatus.page <= 0) || (i == 1 && ui.pageReader->readerStatus.page + 1 >= pages) ||
					(i == 2 && ui.pageReader->readerStatus.zoomPct <= 100);
				Kit::Key kk;
				kk.off = off;
				kk.pressed = docPressed == i;
				if (i == 5)
				{
					// Brightness: its sun, the state under it while Extra dim or a filter is on
					std::string note = ui.lightNote(p);
					kk.glyph = &G_SUN;
					kk.lit = !note.empty();
					k.key(b, kk);
					if (!note.empty() && k.textW(note, F_I11B) <= b.w - 4)
						ui.drawTextMid(note, F_I11B, ACCENT, b.x + b.w / 2, b.y + b.h - 8, b.w - 4, 2);
					continue;
				}
				kk.label = labels[i];
				kk.key = (hints || i == 4) ? keys[i] : "";	// (the fit's says which one is on)
				k.key(b, kk);
			}
			ui.drawHints({{"LEFT/RIGHT", "Page"}, {"UP/DOWN", "Zoom"}, {"A", "2x"}, {"B", "Close"}, {"START", "Settings"}});
		}
		else
		{
			// under a finger: the page the finger is at (the pages themselves come at the reader's pace)
			ui.drawSlider(docBarTrack(), ui.slideShare(SL_DOC, 0, pages > 1 ? ui.pageReader->readerStatus.page / (double)(pages - 1) : 0), ui.slideHeld(SL_DOC),
				ui.slideHeld(SL_DOC) ? "Page " + std::to_string((int)ui.slide.value + 1) + " / " + std::to_string(pages) : "");
			Rect pad = docPadRect();
			ui.drawBox(pad, SURFACE, R_TILE);
			if (ui.pageReader->readerStatus.failedHere) ui.drawTextMid("A page couldn't be shown \xC2\xB7 A: try again", 1, DANGER, W / 2, pad.y + pad.h / 2, 560, 2);
			else ui.drawTextMid(ui.pageReader->readerStatus.loading ? "Loading" + std::string(SDL_GetTicks() / 400 % 4, '.') : "Drag here to scroll", 1, DIM, W / 2,
				pad.y + pad.h / 2, 0, 2);
			// each with its key under it, as the single page's
			const char* labels[4] = {"Single page", "Fullscreen", "Brightness", "Pages"};
			const char* keys[4] = {"X", "SELECT", "", "Y"};
			bool hints = p.settings.buttonHotkeys;
			for (int i = 0; i < 4; i++)
			{
				// the strip's key row: keys in Clean too, drawn by the kit
				Rect b = docBtn(i);
				Kit::Key kk;
				kk.pressed = docPressed == i;
				if (i == 2)
				{
					// Brightness: its sun, with what's on under it while Extra dim or a filter is
					std::string note = ui.lightNote(p);
					kk.glyph = &G_SUN;
					kk.lit = !note.empty();
					k.key(b, kk);
					if (!note.empty() && k.textW(note, F_I11B) <= b.w - 4)
						ui.drawTextMid(note, F_I11B, ACCENT, b.x + b.w / 2, b.y + b.h - 8, b.w - 4, 2);
					continue;
				}
				kk.label = labels[i];
				kk.key = hints ? keys[i] : "";
				k.key(b, kk);
			}
			ui.drawHints({{"UP/DOWN", "Zoom"}, {"LEFT/RIGHT", "Scroll"}, {"B", "Close"}, {"START", "Settings"}});
		}
		if (docMenu != DOC_MENU_NONE) drawDocMenu(p);
	}

	std::string Ui::Docs::signature(const Player&) const
	{
		char b[320];
		snprintf(b, sizeof b, "|doc%d %d,%d,%d %d%d%d%d %d|t%d %d %llu %d|r%d%d %d/%d m%d z%d f%d %d%d|pan%d %.3f,%.3f,%.3f,%.3f %p|%u",
			(int)ui.page, docMenu, docMenuCursor, docMenuScroll, (int)docTouch, (int)ui.slideHeld(SL_DOC), (int)docPanTouch, docPressed,
			(int)docLive, (int)textStatus.open, (int)std::lround(textStatus.fraction * 1000), (unsigned long long)textStatus.place,
			(int)textStatus.atEnd, (int)ui.pageReader->readerStatus.open, (int)ui.pageReader->readerStatus.loading, ui.pageReader->readerStatus.page, ui.pageReader->readerStatus.pages,
			ui.pageReader->readerStatus.mode, ui.pageReader->readerStatus.zoomPct, (int)ui.pageReader->readerStatus.fullscreen, (int)ui.pageReader->readerStatus.failedHere, (int)ui.pageReader->mangaPages,
			docPan.page, docPan.x0, docPan.y0, docPan.x1, docPan.y1, (const void*)docPan.rgba.get(),
			SDL_GetTicks() < docFullTagUntil ? 1u : 0u);
		std::string s = b;
		s += findSignature();
		s += "|bk" + std::to_string(textStatus.book) + " " + std::to_string(textStatus.bottomGen) + (SDL_GetTicks() < docBookToastUntil ? "T" : "");
		s += "|bb" + std::to_string(docBookBar) + "," + std::to_string(docBookBarPressed);
		// (a turn: a frame each step of it)
		if (docTurning()) s += "|turn" + std::to_string((SDL_GetTicks() - textStatus.turnAt) / 12) + " " + std::to_string((SDL_GetTicks() - textStatus.noticeAt) / 40);
		s += textStatus.error + "|" + textStatus.encoding + "|" + textEncoding + "|" + ui.pageReader->readerStatus.error;
		// (opening: the dots move)
		if ((ui.page == Page::DocText && !textStatus.open) || (ui.page == Page::DocPages && (ui.pageReader->readerStatus.loading || !ui.pageReader->readerStatus.open)))
			s += std::to_string(SDL_GetTicks() / 400);
		// (the pages grid and fullscreen are the manga reader's own drawing: their state as it signs it)
		if (ui.page == Page::DocPages && ui.pageReader->mangaPages)
			s += "|pg" + std::to_string(ui.pageReader->mangaPagesCursor) + "," + std::to_string(ui.pageReader->mangaPagesScroll) + " " +
				std::to_string(ui.pageReader->mpTex.size()) + "," + std::to_string(ui.pageReader->readerThumbs.pages.size());
		if (ui.page == Page::DocPages && ui.pageReader->readerStatus.fullscreen)
		{
			s += "|full" + std::to_string(ui.pageReader->readerBottom.gen) + " " + std::to_string(ui.pageReader->mfTex.size()) + " " +
				std::to_string(ui.pageReader->mfWaiting) + " " + std::to_string(ui.pageReader->readerBottom.pages.size()) + (SDL_GetTicks() < ui.pageReader->mfTagUntil ? "T" : "");
			for (auto& b : ui.pageReader->readerBottom.boxes)
				s += " " + std::to_string(b.page) + ":" + std::to_string((int)std::lround(b.x * 4096)) + "," + std::to_string((int)std::lround(b.y * 4096));
		}
		return s;
	}
}
