// Files' document viewers, the bottom panel's side: a TXT or a book of text (EPUB) on main's TextReader
// (Page::DocText), and a PDF or a comic EPUB on its MangaReader (Page::DocPages), read a page at a time with the photo
// viewer's pan area, or down a strip (fullscreen across both panels). Each document's place is kept in
// doc-places.conf (the state folder), so it opens where it was left.
#include "ui_internal.h"
#include <map>
#include "ui_docs.h"
#include "web/server.h"
#include "json.h"
#include "ui_kit.h"
#include "ui_reader.h"
#include "ui_settings.h"
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
	static bool isPdfPath(const std::string& path)
	{
		size_t dot = path.rfind('.');
		std::string e = dot == std::string::npos ? "" : path.substr(dot);
		for (char& c : e) c = (char)tolower((unsigned char)c);
		return e == ".pdf";
	}

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
		enum { DT_FONT, DT_SIZE, DT_SPACING, DT_MARGINS, DT_THEME, DT_FILTER, DT_ENCODING, DT_GOTO, DT_VIEW, DT_HOLD, DT_MARKS,
			DT_TURN, DT_FIND };
		constexpr int MENU_TOP = 72, MENU_ROW_H = 50;	// (the kit's rows are 46 tall)
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
		docOwnLast = {};
		foreignPlace = {};
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
		// a PDF and a comic each keep their own way in (a comic's default is the strip)
		const bool strip = isPdfPath(path) ? p.settings.pdfStrip : p.settings.comicStrip;
		pagesPath = path;
		ui.pageReader->mangaRead = {"", name, "", name, path, true, page0, strip ? MANGA_STRIP : MANGA_LTR, MANGA_FIT_PAGE};
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
			e.aid = PLACE_TEXT;	// (the kind: a text's place and share, not a page of so many)
		}
		else
		{
			if (!ui.pageReader->readerStatus.open || ui.pageReader->readerStatus.pages <= 0) return;
			e.position = ui.pageReader->readerStatus.page;
			e.duration = ui.pageReader->readerStatus.pages;
			e.aid = PLACE_PAGES;	// (the kind: a page of so many)
		}
		auto same = [](const ResumeEntry& a, const ResumeEntry& b) { return a.path == b.path && a.position == b.position && a.aid == b.aid && a.duration == b.duration; };
		// only this reader's own moves are written: a browser's write since then stays (the stored place follows the
		// last turn, on either device)
		if (same(docOwnLast, e)) return;
		const ResumeEntry* was = docPlaces->find(docPath);
		bool first = docOwnLast.path != docPath;
		docOwnLast = e;
		if (first && was && was->position == e.position && was->aid == e.aid && was->duration == e.duration) return;
		if (!foreignPlace.path.empty() && samePlace(foreignPlace, e)) foreignPlace = {};
		ResumeEntry w = stampPlace(was, e, "the handheld", (int64_t)time(nullptr));
		docPlaces->put(w);
		docPlacesDirty = true;
		docPlaceSent(w);
	}

	void Ui::Docs::docPlaceSent(const ResumeEntry& e)
	{
		if (!ui.web || !ui.web->running()) return;
		std::string v = ui.web->servedVirtual(e.path);
		if (v.empty()) return;
		JsonWriter j;
		j.obj().key("p").str(v).key("place").obj().key("t").str(e.aid == PLACE_PAGES ? "pages" : "text").key("pos").num(e.position)
			.key("share").num(e.duration).key("rev").num((double)e.rev).key("when").num((double)e.when).key("by").str(e.by).end().end();
		ui.web->notify("book-place", j.out());
	}

	PlaceVerdict Ui::Docs::webPlace(const PlaceWrite& w, ResumeEntry& out)
	{
		docPlacesLoad();
		PlaceVerdict v = mergePlace(docPlaces->find(w.path), w, out);
		if (v != PlaceVerdict::Written) return v;
		docPlaces->put(out);
		docPlacesDirty = true;
		docPlacesSave(true);
		docPlaceSent(out);
		// the book is open here: its reader stays where it is, and Contents (Y) offers the browser's place
		if (w.path == docPath && docLive) foreignPlace = out;
		fprintf(stderr, "docs: %s wrote the place of %s (rev %llu)\n", out.by.c_str(), fileName(w.path).c_str(), (unsigned long long)out.rev);
		return v;
	}

	void Ui::Docs::contentsOpen()
	{
		docMenu = DOC_MENU_CONTENTS;
		docMenuCursor = 0;
		// the cursor on a browser's place while it shows (row 0), else on the chapter being read (the first of entries
		// that open the same place: a chapter before its own sections)
		int off = foreignShown() ? 1 : 0;
		if (!off)
			for (int i = 0; i < (int)textStatus.contents.size(); i++)
				if (textStatus.contents[i].second.first <= textStatus.place &&
					(i == 0 || textStatus.contents[i].second.first > textStatus.contents[docMenuCursor].second.first))
					docMenuCursor = i;
		docMenuScroll = std::max(0, docMenuCursor - 3);
	}

	bool Ui::Docs::foreignShown() const
	{
		if (foreignPlace.path.empty() || foreignPlace.path != docPath) return false;
		if (ui.page == Page::DocText) return foreignPlace.aid == PLACE_TEXT && (uint64_t)foreignPlace.position != textStatus.place;
		return foreignPlace.aid == PLACE_PAGES && (int)foreignPlace.position != ui.pageReader->readerStatus.page;
	}

	std::string Ui::Docs::foreignLabel() const
	{
		std::string at = foreignPlace.aid == PLACE_PAGES ? "page " + std::to_string((int)foreignPlace.position + 1)
			: std::to_string((int)std::lround(std::clamp(foreignPlace.duration, 0.0, 1.0) * 100)) + "%";
		return "Where " + foreignPlace.by + " is \u00b7 " + at;
	}

	void Ui::Docs::foreignGo()
	{
		int64_t now = SDL_GetTicks();
		if (ui.page == Page::DocText) textCmds.push_back({TextReader::JumpTo, foreignPlace.position, now});
		else ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Seek, foreignPlace.position, now});
		foreignPlace = {};
		docMenu = DOC_MENU_NONE;
	}

	void Ui::Docs::webMarkAdd(const std::string& path, uint64_t id, double frac, const std::string& label)
	{
		docMarksLoad();
		for (auto& m : docMarks)
			if (m.path == path && m.id == id) { m.label = label; docMarksSave(); return; }
		docMarks.push_back({path, id, frac, label});
		docMarksSave();
	}

	bool Ui::Docs::webMarkRemove(const std::string& path, uint64_t id)
	{
		docMarksLoad();
		for (size_t i = 0; i < docMarks.size(); i++)
			if (docMarks[i].path == path && docMarks[i].id == id)
			{
				docMarks.erase(docMarks.begin() + i);
				docMarksSave();
				return true;
			}
		return false;
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
		docOwnLast = {};
		foreignPlace = {};
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
		// the text viewer's options (a PDF's are its View and More sheets: below, "the page reader")
		std::vector<int> r = {DT_FIND, DT_MARKS, DT_VIEW};
		if (textStatus.book) { r.push_back(DT_HOLD); r.push_back(DT_TURN); }
		for (int id : {DT_FONT, DT_SIZE, DT_SPACING, DT_MARGINS, DT_THEME, DT_FILTER}) r.push_back(id);
		if (!textStatus.encoding.empty()) r.push_back(DT_ENCODING);
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
		case DT_FILTER: docMenu = DOC_MENU_NONE; ui.openLight(); return;	// the Brightness sheet
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
			// a browser's place first, while it differs
			int k = docMenuCursor - (foreignShown() ? 1 : 0);
			if (k < 0) { foreignGo(); return; }
			if (k < (int)textStatus.contents.size())
				textCmds.push_back({TextReader::JumpTo, (double)textStatus.contents[k].second.first, (int64_t)SDL_GetTicks()});
			docMenu = DOC_MENU_NONE;
			return;
		}
		auto rows = docMenuRows();
		int id = docMenuCursor >= 0 && docMenuCursor < (int)rows.size() ? rows[docMenuCursor] : -1;
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

	Ui::Rect Ui::Docs::docMenuView(int n) const
	{
		int shown = (H - MENU_TOP - 40) / MENU_ROW_H;
		return {16, MENU_TOP, 608, std::min(n, shown) * MENU_ROW_H};
	}

	Ui::SmoothList* Ui::Docs::smoothList()
	{
		if (docMenu != DOC_MENU_NONE) return &menuList.list;
		if (ui.page == Page::DocPages && ui.pageReader->mangaPages) return &ui.pageReader->pagesList;
		return nullptr;
	}

	void Ui::Docs::drawDocMenu(const Player& p)
	{
		bool contents = docMenu == DOC_MENU_CONTENTS, marks = docMenu == DOC_MENU_MARKS;
		// over the whole panel, its title where the page's was
		ui.fill({0, 0, W, H}, BG);
		ui.fill({0, 60, W, 2}, RULE);
		Kit& k = *ui.kit;
		ui.drawTextMid(marks ? "Bookmarks" : contents ? "Contents" : "Text options", F_P20, ACCENT, 24, 32, 400);
		auto here = docMarksHere();
		int shown = (H - MENU_TOP - 40) / MENU_ROW_H;
		int n = marks ? (int)here.size() + 1 : contents ? contentsCount() : (int)docMenuRows().size();
		auto rows = docMenuRows();
		// a smooth list (ui_smoothlist.h): another menu starts it again where its cursor is, at once
		if (menuListKind != docMenu) { menuList.synced = -1; menuListKind = docMenu; }
		docMenuScroll = std::clamp(docMenuScroll, 0, std::max(0, n - shown));
		Rect view = docMenuView(n);
		menuList.follow(view, MENU_ROW_H, n, 1, docMenuScroll, &docMenuCursor);
		// each row's look: a plain row (Bookmarks, Contents) or a setting's (Options)
		struct MenuRow { bool setting = false; const Glyph* glyph = nullptr; std::string name, note, value; bool danger = false, action = false; };
		auto look = std::make_shared<std::map<int, MenuRow>>();
		auto rowAt = [this, look, marks, contents, here, rows, &p](int i) -> const MenuRow& {
			auto it = look->find(i);
			if (it != look->end()) return it->second;
			MenuRow m;
			bool at = i == docMenuCursor;
			if (marks && i == 0)
			{
				m.glyph = &G_PLUS;
				m.name = "Add a bookmark here";
			}
			else if (marks)
			{
				const DocMark& dm = docMarks[here[i - 1]];
				m.glyph = &G_BOOKMARK;
				m.name = dm.label;
				m.note = std::to_string((int)std::lround(dm.frac * 100)) + "%";
				if (at && docMarkArmed) { m.note = "X again to delete it"; m.danger = true; }
			}
			else if (contents && foreignShown() && i == 0)
			{
				m.glyph = &G_BOOKMARK;
				m.name = foreignLabel();
			}
			else if (contents)
			{
				const auto& c = textStatus.contents[i - (foreignShown() ? 1 : 0)];
				m.name = std::string(std::min(c.second.second, 4) * 2, ' ') + c.first;
			}
			else
			{
				int id = rows[i];
				static const char* LABELS[] = {"Font", "Text size", "Line spacing", "Margins", "Colours", "Brightness & filter", "Encoding",
					"Go to", "View", "Hold", "Bookmarks", "Page turn", "Find\u2026"};
				m.setting = true;
				m.name = LABELS[id];
				std::string& v = m.value;
				if (id == DT_FIND || id == DT_MARKS || id == DT_FILTER)
				{
					// an action: opens something (Brightness & filter its sheet: the filter on now beside it)
					m.action = true;
					if (id == DT_FIND && !findQuery.empty()) v = "\u201C" + findQuery + "\u201D";
					if (id == DT_FILTER) v = FILTER_NAMES[std::clamp(p.settings.txtFilter, 0, DOC_FILTERS - 1)];
					if (id == DT_MARKS) v = docMarksHere().empty() ? "None yet" : std::to_string((int)docMarksHere().size());
				}
				else switch (id)
				{
				case DT_FONT: v = FONT_NAMES[std::clamp(p.settings.txtFont, 0, TXT_FONTS - 1)]; break;
				case DT_SIZE: v = SIZE_NAMES[std::clamp(p.settings.txtSize, 0, TXT_SIZES - 1)]; break;
				case DT_SPACING: v = SPACING_NAMES[std::clamp(p.settings.txtSpacing, 0, TXT_SPACINGS - 1)]; break;
				case DT_MARGINS: v = MARGIN_NAMES[std::clamp(p.settings.txtMargins, 0, TXT_MARGINS - 1)]; break;
				case DT_THEME: v = THEME_NAMES[std::clamp(p.settings.txtTheme, 0, TXT_PAGES - 1)]; break;
				case DT_ENCODING: v = textEncoding.empty() ? "Auto (" + textStatus.encoding + ")" : textStatus.encoding; break;
				case DT_GOTO: v = std::to_string((int)std::lround(textStatus.fraction * 100)) + "%"; break;
				case DT_VIEW: v = p.settings.txtBook ? "Book mode" : "Scroll"; break;
				case DT_TURN: v = p.settings.txtTurn ? "Off" : "Fold"; break;
				case DT_HOLD: v = p.settings.txtBook == MANGA_BOOK_RIGHT ? "Top screen right" : "Top screen left"; break;
				default: break;
				}
			}
			return look->emplace(i, std::move(m)).first->second;
		};
		SmoothList::Spec spec;
		spec.view = view;
		spec.rowH = MENU_ROW_H;
		spec.count = n;
		spec.key = [rowAt, cursor = docMenuCursor](int i) {
			const MenuRow& m = rowAt(i);
			char b[64];
			snprintf(b, sizeof b, "|%p|%d%d%d%d", (const void*)m.glyph, m.setting, m.danger, m.action, i == cursor);
			return m.name + "|" + m.note + "|" + m.value + b;
		};
		spec.draw = [this, rowAt, cursor = docMenuCursor](int i, Rect box) {
			Kit& k = *ui.kit;
			const MenuRow& m = rowAt(i);
			Rect r{box.x, box.y, box.w, 46};
			if (!m.setting)
			{
				Kit::Row row;
				row.font = F_P16;
				row.under = BG;
				row.focused = i == cursor;
				row.glyph = m.glyph;
				row.name = m.name;
				row.note = m.note;
				row.danger = m.danger;
				k.row(r, row);
				return;
			}
			Kit::SettingRow sr;
			sr.label = m.name;
			sr.value = m.value;
			sr.action = m.action;
			sr.focused = i == cursor;
			k.settingRow(r, sr);
		};
		menuList.list.draw(std::move(spec));
		menuList.sync(docMenuScroll);
		if (n > shown) ui.drawScrollBar(MENU_TOP, shown * MENU_ROW_H, docMenuScroll, shown, n);
		if (marks) k.hints({{"A", docMenuCursor == 0 ? "Add" : "Go"}, {"X", "Delete"}, {"B", "Close"}});
		else if (contents) k.hints({{"A", "Go"}, {"Y/B", "Close"}});
		else k.hints({{"LEFT/RIGHT", "Change"}, {"START/B", "Close"}});
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
				: docMenu == DOC_MENU_CONTENTS ? contentsCount() : (int)docMenuRows().size();
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
			docBookBar = false;	// every key acts on the page, and the bar goes (a tap in the middle shows it)
			switch (b)
			{
			case Button::Up: docBookTurn(-1, p); break;
			case Button::Down: docBookTurn(1, p); break;
			case Button::L: textCmds.push_back({TextReader::BookTurn, -1, now}); break;
			case Button::R: case Button::A: textCmds.push_back({TextReader::BookTurn, 1, now}); break;
			case Button::X: docMarksOpen(); break;
			// START: the options, an upright sheet over the pages, as in the scroll
			case Button::Start:
				docMenu = DOC_MENU_OPTIONS;
				docMenuCursor = docMenuScroll = 0;
				break;
			// SELECT or B: back to the scroll (B there closes the document)
			case Button::Select: case Button::B: { Settings s = p.settings; s.txtBook = 0; p.setSettings(s); break; }
			default: break;
			}
			if (b == Button::Y && contentsCount() > 0)
			{
				contentsOpen();
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
			// START: how the text looks (the options), as View in the page reader; SELECT below is the full view
			case Button::Start:
				docMenu = DOC_MENU_OPTIONS;
				docMenuCursor = docMenuScroll = 0;
				break;
			case Button::Y:
				if (contentsCount() > 0)
				{
					// the cursor on the chapter being read
					contentsOpen();
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
		// B in Fullscreen: back to one screen, as SELECT (START opens View there too, whose Layout says so)
		if (ui.pageReader->readerStatus.fullscreen && b == Button::B)
		{
			togglePagesFull(p);
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
		// X: More, as everywhere (Reading settings is its row). The readers' three keys, the same in the Manga reader:
		// SELECT is Fullscreen on and off in one press, START is View (the layout, the fit, the zoom), L3 turns the
		// page a quarter (paged only: the strip shows it unturned)
		case Button::X: openPagesMore(p); break;
		case Button::Select: togglePagesFull(p); break;
		case Button::Start: openPagesView(); break;
		case Button::L3: if (!strip) stepPagesTurn(1); break;
		case Button::Y: ui.pageReader->mangaPagesOpen(p); break;
		case Button::B: docClose(p); break;
		default: break;
		}
		return false;
	}

	void Ui::Docs::stickAxis(int value, Player& p, int valueX)
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
		else if (target == 2) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Stick, (double)value, now, (double)valueX});
	}

	void Ui::Docs::docBookTurn(int dpadDy, Player& p)
	{
		int step = mangaBookStep(textStatus.book, false, false, dpadDy);
		if (step) textCmds.push_back({TextReader::BookTurn, (double)step, (int64_t)SDL_GetTicks()});
		(void)p;
	}

	// ---- the finger ----

	// the text reader's boxes are lay::readerText's: the pad takes all the height between the slider and the keys
	Ui::Rect Ui::Docs::docBarRect() const { return lay::readerText().bar; }
	Ui::Rect Ui::Docs::docPadRect() const { return lay::readerText().pad; }
	Ui::Rect Ui::Docs::docBtn(int i) const { return lay::readerText().row[i]; }

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
				: docMenu == DOC_MENU_CONTENTS ? contentsCount() : (int)docMenuRows().size();
			if (y < 60) { docMenu = DOC_MENU_NONE; return false; }
			// the list follows a finger by the pixel and glides on (a smooth list); a tap acts on its row
			int r = menuList.list.rowAt(x, y);
			if (r >= n) r = -1;
			ui.pageReader->mangaListSmooth(menuList.list, x, y, r,
				[this, x, rowY = y, pp = &p](int row) {
					docMenuCursor = row;
					// a stepper row: a tap left of its value steps back, right of it on (lay::settingStepper, as the
					// settings' rows); an action row, or anywhere else on the row, acts
					auto ids = docMenuRows();
					bool stepper = docMenu == DOC_MENU_OPTIONS && row >= 0 && row < (int)ids.size() && ids[row] != DT_FIND &&
						ids[row] != DT_MARKS && ids[row] != DT_FILTER;
					lay::SettingStepper st = lay::settingStepper(docMenuRowRect(row));
					if (stepper && (st.back.hit(x, rowY) || st.fwd.hit(x, rowY))) docMenuStep(st.back.hit(x, rowY) ? -1 : 1, *pp);
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
				if (Rect(lay::readerText().findKeys[i]).hit(x, y))
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
			if (pt.kind == Pointer::Down && h.moreHit.hit(x, y) && !(ui.page == Page::DocPages && ui.pageReader->readerStatus.fullscreen))
			{
				// the text's options; a PDF's More, as X
				if (ui.page == Page::DocPages) openPagesMore(p);
				else
				{
					docMenu = DOC_MENU_OPTIONS;
					docMenuCursor = docMenuScroll = 0;
				}
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
		lay::ReaderPage pl = pagesLayout();	// (a PDF's boxes; the text's are docBtn and docPadRect)
		if (pt.kind == Pointer::Down)
		{
			docPressed = -1;
			docMoved = false;
			docDownX = x;
			docDownY = y;
			docFlick.press(pt.y * H, ui.touchMs);
			docTouchX = pt.x;
			docTouchY = pt.y;
			// Fullscreen's navigation, while it is up: the sheet takes the finger (the rest of the panel is the page),
			// and a touch on it keeps it
			if (full && pagesFullNavShown() && Rect(lay::readerFullNav(false).sheet).hit(x, y))
			{
				docFullTagUntil = now + FULL_NAV_MS;
				if (Rect(lay::readerFullNav(false).sliderHit).hit(x, y)) grabPagesSlider(lay::readerFullNav(false).track, x, y, p);
				else
					for (int id = 0; id < DP_KEYS; id++)
						if (pagesKeyRect(id).hit(x, y) && !pagesKeyOff(id)) docPressed = id;
				return false;
			}
			if (full || (!text && !single && pl.surface.hit(x, y)))
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
			// the bar: the text goes with the finger; the pages (decoded as they come) at the scrub's pace, the page it's
			// let go on always
			if (text && docBarHit().hit(x, y))
			{
				ui.slideGrab(SL_DOC, 0, {docBarTrack(), {0, 1, 0}, false, 0, textStatus.fraction}, x, y, p, [this](double f, bool, Player&) {
					textCmds.push_back({TextReader::Jump, f, (int64_t)SDL_GetTicks()});
				});
				return false;
			}
			if (!text && pl.sliderHit.hit(x, y))
			{
				grabPagesSlider(pl.track, x, y, p);
				return false;
			}
			if (single && docPanThumb().hit(x, y) && docPan.page >= 0)
			{
				docPanTouch = true;
				docPanStartX = (docPan.x0 + docPan.x1) / 2;
				docPanStartY = (docPan.y0 + docPan.y1) / 2;
				return false;
			}
			if (text)
			{
				for (int i = 0; i < 5; i++)
					if (docBtn(i).hit(x, y)) docPressed = i;
			}
			else
				for (int id = 0; id < DP_KEYS; id++)
					if (pagesKeyRect(id).hit(x, y) && !pagesKeyOff(id)) docPressed = id;
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
				docFlick.move(pt.y * H, ui.touchMs);
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
			// only a deliberate flick glides on (a share of the panel a ms, as the scroll's moves); a drag stops there
			const double flick = docFlick.lift(ui.touchMs);
			if (docMoved && flick != 0)
				textCmds.push_back({TextReader::Fling, flick / H, (int64_t)now});
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
			// Fullscreen: a tap in the panel's middle shows the navigation (with it up, hides it again), one at the
			// top or the bottom goes a screenful back or on, as LEFT / RIGHT
			if (full && !docMoved)
			{
				int edge = lay::readerFullTap(x, y, false);
				if (edge) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Step, (double)edge, (int64_t)now});
				else docFullTagUntil = pagesFullNavShown() ? 0 : now + FULL_NAV_MS;
				if (!edge) docFullToastUntil = 0;
				fprintf(stderr, "docs: fullscreen tap at %d,%d -> %s\n", x, y, edge < 0 ? "back" : edge > 0 ? "on" : pagesFullNavShown() ? "the navigation" : "the navigation away");
			}
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
		if (i < 0) return false;
		if (!text)
		{
			// a PDF's key: acted on where the finger lifts, as long as it is still on it
			if (pagesKeyRect(i).hit(x, y)) pagesKeyAct(i, p);
			return false;
		}
		if (!docBtn(i).hit(x, y)) return false;
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
		s.keys[1] = {&G_PAGE, "Text", "START", ""};
		s.keys[2] = {&G_SUN, "Light", "", ui.lightNote(p)};
		s.keys[3] = {&G_SEARCH, "Find", "L3", ""};
		s.leave = "Back to scrolling";
		s.leaveKey = "B";
		s.hints = {{"UP/DOWN", "Turn"}, {"A", "On"}, {"X", "Bookmarks"}};
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
			if (contentsCount() > 0)
			{
				contentsOpen();
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
			// a page turned says nothing (the page itself shows it turned, and its % is at its foot): only a note does
			if (note && since >= 0 && since < DOC_NOTICE_MS)
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
			{"Contents", "Y", textStatus.contents.empty()}, {"Bookmarks", "X", false}, {"Options", "START", false},
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
			// (the line takes the hint bar's place: the hints are not drawn while Find is on)
			lay::ReaderText fl = lay::readerText();
			ui.fill(fl.findCover, BG);
			const char* labels[3] = {"Previous", "Next", "Done"};
			const char* keys[3] = {"L", "R", "B"};
			for (int i = 0; i < 3; i++)
			{
				Rect bb = fl.findKeys[i];
				Kit::Key kk;
				kk.label = labels[i];
				kk.key = keys[i];
				k.key(bb, kk);
			}
			ui.drawTextMid("Find \u201C" + findQuery + "\u201D", 0, ACCENT, fl.findLine.x + fl.findLine.w / 2, fl.findLine.y + fl.findLine.h / 2, fl.findLine.w, 2);
		}
		// (the buttons above say their own keys: START the options, SELECT Book mode)
		else ui.drawHints({{"UP/DOWN", "Scroll"}, {"L/R", "Page"}, {"L3", "Find"}, {"B", "Close"}});
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
		if (ui.pageReader->readerStatus.fullscreen && ui.pageReader->readerBottom.on)
		{
			// the pages on this panel too; the Pages sheet over them lies on the plain ground
			if (ui.pageReader->mangaPages)
			{
				ui.pageReader->drawMangaPages(p);
				return;
			}
			ui.pageReader->drawMangaFull(p);
			if (pagesFullNavShown()) drawPagesFullNav(p);
			else if (SDL_GetTicks() < docFullToastUntil)
				ui.pageReader->drawFullToast("Fullscreen \xC2\xB7 SELECT or B: one screen \xC2\xB7 tap the middle: controls");
			return;
		}
		// the header: the document's name at 18 units, its line under it (the page, the zoom, the fit), More at the right
		lay::ReaderHeader h = lay::readerHeader({0, 0, W, lay::SHELL_HEAD}, false);
		Kit::Header hd;
		hd.title = ui.pageReader->mangaReadChapterTitle.empty() ? fileName(docPath) : ui.pageReader->mangaReadChapterTitle;
		int pages = ui.pageReader->readerStatus.pages;
		bool strip = ui.pageReader->readerStatus.mode == MANGA_STRIP;
		if (pages > 0)
			hd.crumb = "Page " + std::to_string(pagesShown() + 1) + " of " + std::to_string(pages);
		if (ui.pageReader->readerStatus.zoomPct != 100)
			hd.crumb += std::string(hd.crumb.empty() ? "" : "  \xC2\xB7  ") + std::to_string(ui.pageReader->readerStatus.zoomPct) + "%";
		if (!strip && pages > 0) hd.crumb += std::string("  \xC2\xB7  ") + (p.settings.mangaFit == MANGA_FIT_WIDTH ? "Fit width" : "Fit page");
		if (hd.crumb.empty()) hd.crumb = fileName(docPath);
		hd.titleFont = F_SMALL;
		hd.inlineTitle = true;
		hd.more = true;
		ui.kit->header(h.strip, hd);
		if (!ui.pageReader->readerStatus.error.empty())
		{
			ui.drawTextMid(ui.pageReader->readerStatus.error, 1, DANGER, W / 2, 200, 560, 2);
			k.hints({{"B", "Close"}});
			return;
		}
		lay::ReaderPage l = pagesLayout();
		if (!strip) drawPagesPan(p, l);
		else
		{
			// the drag surface: the strip goes with the finger; a page that failed says so with Try again
			ui.kit->readerSurface(l.surface, true, ui.pageReader->readerStatus.failedHere ? "" : "Drag to scroll");
			if (ui.pageReader->readerStatus.failedHere)
				ui.drawTextMid("A page didn\xE2\x80\x99t load \xC2\xB7 A: try again", 1, DANGER, W / 2, l.surface.y + l.surface.h / 2, 560, 2);
			else if (ui.pageReader->readerStatus.loading)
				ui.drawTextMid("Loading" + std::string(SDL_GetTicks() / 400 % 4, '.'), 1, DIM, W / 2, l.surface.y + l.surface.h / 2 + 40, 0, 2);
		}
		drawPagesSlider(l);
		drawPagesKeys(p);
		if (strip) k.hints({{"LEFT/RIGHT", "Scroll"}, {"L/R", "Page"}, {"UP/DOWN", "Zoom"}, {"X", "More"}, {"B", "Close"}});
		else k.hints({{"LEFT/RIGHT", "Through the page"}, {"L/R", "Page"}, {"UP/DOWN", "Zoom"}, {"A", "200%"}, {"X", "More"}, {"B", "Close"}});
		// the Pages sheet over all of it
		if (ui.pageReader->mangaPages) ui.pageReader->drawMangaPages(p);
	}

	// ---- the page reader: a PDF, a comic (docs/redesign: pdf, pdf-view, pdf-more, pdf-pages) ----

	int Ui::Docs::pagesShown() const
	{
		// under a finger on the slider: the page the finger is at (the pages come at the reader's pace)
		return ui.slideHeld(SL_DOC) ? (int)ui.slide.value : ui.pageReader->readerStatus.page;
	}

	Ui::Rect Ui::Docs::pagesKeyRect(int id) const
	{
		// in Fullscreen the keys are the navigation's (what it has of them), the same ids doing the same things
		if (ui.pageReader->readerStatus.fullscreen)
			switch (id)
			{
			case DP_KEY_BACK: return PageReader::fullNavKey(PageReader::FN_PG_BACK, false);
			case DP_KEY_ON: return PageReader::fullNavKey(PageReader::FN_PG_ON, false);
			case DP_KEY_PAGES: return PageReader::fullNavKey(PageReader::FN_PAGES, false);
			case DP_KEY_VIEW: return PageReader::fullNavKey(PageReader::FN_VIEW, false);
			case DP_KEY_FULL: return PageReader::fullNavKey(PageReader::FN_LEAVE, false);
			default: return {0, 0, 0, 0};
			}
		lay::ReaderPage l = pagesLayout();
		switch (id)
		{
		case DP_KEY_BACK: case DP_KEY_ON: case DP_KEY_OUT: case DP_KEY_IN: return l.keys[id - DP_KEY_BACK];
		case DP_KEY_FIT: return l.fit;
		case DP_KEY_PAGES: return l.row[0];
		case DP_KEY_VIEW: return l.row[1];
		case DP_KEY_FULL: return l.row[2];
		case DP_KEY_LIGHT: return l.row[3];
		default: return {0, 0, 0, 0};
		}
	}

	bool Ui::Docs::pagesKeyOff(int id) const
	{
		const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
		switch (id)
		{
		case DP_KEY_BACK: return st.page <= 0;
		case DP_KEY_ON: return st.page + 1 >= st.pages;
		case DP_KEY_OUT: return st.zoomPct <= 100;
		case DP_KEY_PAGES: return st.pages <= 0;
		default: return false;
		}
	}

	void Ui::Docs::pagesKeyAct(int id, Player& p)
	{
		switch (id)
		{
		case DP_KEY_BACK: key(Button::L, p); break;
		case DP_KEY_ON: key(Button::R, p); break;
		case DP_KEY_OUT: key(Button::Down, p); break;
		case DP_KEY_IN: key(Button::Up, p); break;
		case DP_KEY_FIT: stepPagesFit(1, p); break;
		case DP_KEY_PAGES: key(Button::Y, p); break;
		case DP_KEY_VIEW: key(Button::Start, p); break;
		case DP_KEY_FULL: key(Button::Select, p); break;
		case DP_KEY_LIGHT: ui.openLight(); break;
		default: break;
		}
	}

	void Ui::Docs::drawPagesPan(const Player& p, const lay::ReaderPage& l)
	{
		// the page small, the part on the top panel boxed: a finger drags the box
		int filter = std::clamp(p.settings.pdfFilter, 0, DOC_FILTERS - 1);
		Rect pan = l.pan;
		ui.fillRound(pan, BUTTON, R_TILE);
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
				ui.kit->ring({bx, by, bw, bh}, theme::R_GRIP, ACCENT);
			}
		}
		else ui.drawTextMid(ui.pageReader->readerStatus.loading || !ui.pageReader->readerStatus.open ? "Loading" + std::string(SDL_GetTicks() / 400 % 4, '.') : "",
			1, DIM, pan.x + pan.w / 2, pan.y + pan.h / 2, 0, 2);
		if (ui.pageReader->readerStatus.failedHere)
			ui.drawTextMid("A page didn\xE2\x80\x99t load \xC2\xB7 A: try again", F_I12, DANGER, pan.x + pan.w / 2, pan.y + pan.h - 16, pan.w - 16, 2);
		// how the finger works it, under the keys
		ui.drawTextMid("Drag the box to look around.", F_I12, DIM, l.help.x, l.help.y + 10, l.help.w);
		ui.drawTextMid("Double-tap: 200% and back.", F_I12, DIM, l.help.x, l.help.y + 30, l.help.w);
	}

	void Ui::Docs::drawPagesSlider(const lay::ReaderPage& l)
	{
		int pages = ui.pageReader->readerStatus.pages;
		if (pages <= 0) return;
		int at = pagesShown();
		double share = ui.slideShare(SL_DOC, 0, pages > 1 ? at / (double)(pages - 1) : 0);
		ui.drawTextMid(std::to_string(at + 1), F_SMALL, TEXT, l.from.x + l.from.w / 2, l.from.y + l.from.h / 2, l.from.w, 2);
		ui.kit->progress(l.track, share, {}, ui.slideHeld(SL_DOC), ui.slideHeld(SL_DOC) ? "Page " + std::to_string(at + 1) : "");
		ui.drawTextMid(std::to_string(pages), F_SMALL, DIM, l.to.x + l.to.w / 2, l.to.y + l.to.h / 2, l.to.w, 2);
	}

	void Ui::Docs::drawPagesKeys(const Player& p)
	{
		Kit& k = *ui.kit;
		bool caps = p.settings.buttonHotkeys;
		bool strip = ui.pageReader->readerStatus.mode == MANGA_STRIP;
		auto keyOf = [&](int id, const Glyph* g, const char* label, const char* cap) {
			Kit::Key kk;
			kk.glyph = g;
			kk.label = label;
			kk.key = caps ? cap : "";
			kk.off = pagesKeyOff(id);
			kk.pressed = docPressed == id;
			k.key(pagesKeyRect(id), kk);
		};
		if (!strip)
		{
			// the hand's keys beside the page, then the fit: its value stepped by a tap
			keyOf(DP_KEY_BACK, &G_LEFT, "Page", "L");
			keyOf(DP_KEY_ON, &G_RIGHT, "Page", "R");
			keyOf(DP_KEY_OUT, &G_MINUS, "Zoom", "DOWN");
			keyOf(DP_KEY_IN, &G_PLUS, "Zoom", "UP");
			Rect f = pagesKeyRect(DP_KEY_FIT);
			k.flat(f, docPressed == DP_KEY_FIT ? PRESSED : BUTTON, R_BUTTON, Kit::edgeOf(BUTTON));
			int cy = f.y + f.h / 2, right = f.x + f.w - 12;
			ui.drawTextMid("Page fit", F_P16, DIM, f.x + 12, cy, 90);
			ui.drawGlyphMid(G_LEFT, right - 76, cy, DIM);
			ui.drawTextMid(p.settings.mangaFit == MANGA_FIT_WIDTH ? "Width" : "Page", F_P16, TEXT, right - 40, cy, 56, 2);
			ui.drawGlyphMid(G_RIGHT, right - 4, cy, DIM);
		}
		keyOf(DP_KEY_PAGES, &G_BLOCKS, "Pages", "Y");
		keyOf(DP_KEY_VIEW, &G_EYE, "View", "START");
		keyOf(DP_KEY_FULL, &G_FULL, "Fullscreen", "SELECT");
		// Light: its sun; while Extra dim or a filter is on, what's on in place of the name
		std::string note = ui.lightNote(p);
		Kit::Key light;
		light.glyph = &G_SUN;
		light.label = note.empty() ? "Light" : note;
		light.lit = !note.empty();
		light.pressed = docPressed == DP_KEY_LIGHT;
		k.key(pagesKeyRect(DP_KEY_LIGHT), light);
	}

	void Ui::Docs::stepPagesFit(int dir, Player& p)
	{
		// Page and Width (Fill would crop a document's margins away); the page under the view kept
		(void)dir;
		Settings s = p.settings;
		s.mangaFit = s.mangaFit == MANGA_FIT_WIDTH ? MANGA_FIT_PAGE : MANGA_FIT_WIDTH;
		ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fit, (double)s.mangaFit, (int64_t)SDL_GetTicks()});
		p.setSettings(s);
	}

	void Ui::Docs::stepPagesTurn(int dir)
	{
		// each Rotate turns a quarter clockwise: back a quarter is three of them
		int n = dir > 0 ? 1 : 3;
		for (int i = 0; i < n; i++) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Rotate, 0, (int64_t)SDL_GetTicks()});
	}

	int Ui::Docs::pagesLayoutNow() const
	{
		const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
		return st.fullscreen ? 2 : st.mode == MANGA_STRIP ? 1 : 0;
	}

	void Ui::Docs::setPagesLayout(int layout, Player& p)
	{
		layout = std::clamp(layout, 0, 2);
		if (layout == pagesLayoutNow()) return;
		int64_t now = SDL_GetTicks();
		PageReader& r = *ui.pageReader;
		bool strip = layout >= 1, full = layout == 2;
		// Fullscreen is the strip over both panels: off first, then the mode, then on again if asked
		if (!full && r.readerStatus.fullscreen) r.readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, 0, now});
		if (strip != (r.readerStatus.mode == MANGA_STRIP))
		{
			// (each view opens at its fit: a page's zoom would cut a strip's lines at both edges)
			if (r.readerStatus.zoomPct != 100) r.mangaZoomTo(1.0);
			r.readerCmds.push_back({PageReader::ReaderCmd::Mode, (double)(strip ? MANGA_STRIP : MANGA_LTR), now});
			Settings s = p.settings;
			(isPdfPath(pagesPath) ? s.pdfStrip : s.comicStrip) = strip;
			p.setSettings(s);
		}
		if (full && !r.readerStatus.fullscreen)
		{
			docFullFrom = r.readerStatus.mode == MANGA_STRIP ? 1 : 0;
			r.readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, 1, now, (double)p.settings.mangaHinge});
			// (the panel is all page now: the line says the way back and where the controls are)
			docFullTagUntil = 0;
			docFullToastUntil = SDL_GetTicks() + FULL_TOAST_MS;
		}
		else docFullTagUntil = docFullToastUntil = 0;
	}

	void Ui::Docs::togglePagesFull(Player& p)
	{
		const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
		if (!st.open || !st.error.empty()) return;
		fprintf(stderr, "docs: fullscreen %s (from layout %d)\n", st.fullscreen ? "off" : "on", st.fullscreen ? docFullFrom : pagesLayoutNow());
		setPagesLayout(st.fullscreen ? docFullFrom : 2, p);
	}

	bool Ui::Docs::pagesFullNavShown() const { return SDL_GetTicks() < docFullTagUntil || ui.slideHeld(SL_DOC); }

	void Ui::Docs::grabPagesSlider(Rect track, int x, int y, Player& p)
	{
		// the pages (decoded as they come) at the scrub's pace, the page it's let go on always; in Fullscreen the
		// navigation stays while it is held and its time again from the last move
		if (ui.pageReader->readerStatus.pages <= 1) return;
		ui.pageReader->mangaSliderPage = -1;
		ui.slideGrab(SL_DOC, 0, {track, {0, (double)(ui.pageReader->readerStatus.pages - 1), 1}, false, SCRUB_MS, (double)ui.pageReader->readerStatus.page}, x, y, p,
			[this](double v, bool, Player&) {
				if (ui.pageReader->readerStatus.fullscreen) docFullTagUntil = SDL_GetTicks() + FULL_NAV_MS;
				ui.pageReader->mangaSeek((int)v);
			});
	}

	void Ui::Docs::drawPagesFullNav(const Player& p)
	{
		PageReader::FullNav n;
		n.title = ui.pageReader->mangaReadChapterTitle.empty() ? fileName(docPath) : ui.pageReader->mangaReadChapterTitle;
		n.slider = SL_DOC;
		n.off[PageReader::FN_PG_BACK] = pagesKeyOff(DP_KEY_BACK);
		n.off[PageReader::FN_PG_ON] = pagesKeyOff(DP_KEY_ON);
		n.off[PageReader::FN_PAGES] = pagesKeyOff(DP_KEY_PAGES);
		static const int IDS[PageReader::FN_KEYS] = {-1, DP_KEY_BACK, DP_KEY_ON, -1, DP_KEY_PAGES, DP_KEY_VIEW, DP_KEY_FULL};
		for (int id = 0; id < PageReader::FN_KEYS; id++)
			if (IDS[id] >= 0 && IDS[id] == docPressed) n.pressed = id;
		n.caps = p.settings.buttonHotkeys;
		ui.pageReader->drawFullNav(n);
	}

	std::vector<int> Ui::Docs::pagesMarksHere()
	{
		docMarksLoad();
		std::vector<int> pages;
		for (int i : docMarksHere()) pages.push_back((int)docMarks[i].id);	// (docMarksHere: in the places' order)
		return pages;
	}

	void Ui::Docs::pagesBookmarkToggle()
	{
		const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
		if (!st.open || st.pages <= 0) return;
		docMarksLoad();
		std::string page = "Page " + std::to_string(st.page + 1);
		for (int i : docMarksHere())
			if ((int)docMarks[i].id == st.page)
			{
				docMarks.erase(docMarks.begin() + i);
				docMarksSave();
				ui.pageReader->topNote = {"Bookmark removed", page, 1500};
				return;
			}
		DocMark m;
		m.path = docPath;
		m.id = (uint64_t)st.page;
		m.frac = st.pages > 1 ? st.page / (double)(st.pages - 1) : 0;
		m.label = page;
		docMarks.push_back(m);
		docMarksSave();
		// said over the page on the top panel (the reader's own note: mpv's is under the pages)
		ui.pageReader->topNote = {page + " bookmarked", "Y \xC2\xB7 Pages lists the bookmarks", 2400};
	}

	void Ui::Docs::openPagesMore(Player&)
	{
		std::string path = docPath;
		ui.openMore([this, path](const Player& pl) {
			MoreSpec m;
			// (the document closed under it: nothing left to say)
			if (ui.page != Page::DocPages || docPath != path) return m;
			m.glyph = isPdfName(path) ? &G_PDF : &G_COMIC;
			m.title = ui.pageReader->mangaReadChapterTitle.empty() ? fileName(path) : ui.pageReader->mangaReadChapterTitle;
			if (ui.pageReader->readerStatus.pages > 0) m.note = std::to_string(ui.pageReader->readerStatus.pages) + " pages";
			// a browser's page first, while it differs from this one
			if (foreignShown()) m.rows.push_back({&G_BOOKMARK, foreignLabel(), "", "", [this](Player&) { foreignGo(); return false; }});
			{
				// this page's bookmark, put or taken away (the Pages grid marks it and lists it on its second tab)
				std::vector<int> marks = pagesMarksHere();
				MoreItem mark;
				mark.glyph = &G_BOOKMARK;
				mark.lit = std::binary_search(marks.begin(), marks.end(), ui.pageReader->readerStatus.page);
				mark.label = mark.lit ? "Remove this bookmark" : "Bookmark this page";
				mark.note = "page " + std::to_string(ui.pageReader->readerStatus.page + 1);
				mark.act = [this](Player&) { pagesBookmarkToggle(); return false; };
				m.rows.push_back(mark);
			}
			m.rows.push_back({&G_INFO, "Info", "", "", [this, path](Player&) { ui.openFileInfo(path, ""); return false; }});
			m.rows.push_back({&G_FOLDER, "Show in folder", "", "", [this, path](Player& p) {
				docClose(p);
				ui.showInFiles(path, p);
				return false;
			}});
			std::string light = ui.lightNote(pl);
			m.rows.push_back({&G_SUN, "Light & filter", light.empty() ? "Off" : light, "", [this](Player&) { ui.openLight(); return false; }});
			m.rows.push_back({&G_GEAR, "Reading settings", "", "", [this](Player& p) {
				ui.openSettings(p, true);	// (B comes back to the page)
				ui.settingsPage->openGroup(GROUP_READING, &p);
				return false;
			}});
			return m;
		}, "pdfmore");
	}

	// View (START): how the page is shown, one sheet (docs/redesign: pdf-view). Layout first (Single page, Vertical
	// scroll, Fullscreen: the strip over both panels), then the fit, the zoom, the turn, and Light & filter. UP / DOWN
	// choose a row, LEFT / RIGHT change it at once (the page above follows), A steps it on (Light opens its sheet), START
	// or B close; a tap on a chip or a stepper's side changes it, outside the sheet closes. Going Fullscreen closes the
	// sheet: the screen under it has become the page.
	struct Ui::Docs::PagesView : Ui::Overlay
	{
		enum { ROW_LAYOUT, ROW_HINGE, ROW_FIT, ROW_ZOOM, ROW_TURN, ROW_LIGHT, ROWS };
		int cursor = ROW_LAYOUT;
		explicit PagesView(Ui& u) : Overlay(u) {}
		const char* name() const override { return "pdfview"; }
		static lay::ListSheet layout() { return lay::listSheet(ROWS); }
		Docs& docs() const { return *ui.docs; }
		bool gone() const { return ui.page != Page::DocPages || !ui.pageReader->readerStatus.open; }
		bool strip() const { return ui.pageReader->readerStatus.mode == MANGA_STRIP; }
		// Hinge gap is Fullscreen's (the Manga reader's own setting, the same gap): changed here it moves the pages
		// above at once
		bool rowOff(int row) const
		{
			if (row == ROW_HINGE) return !ui.pageReader->readerStatus.fullscreen;
			return (row == ROW_FIT || row == ROW_TURN) && strip();
		}
		void change(int row, int dir, Player& p)
		{
			if (rowOff(row)) return;
			switch (row)
			{
			case ROW_LAYOUT:
			{
				int to = std::clamp(docs().pagesLayoutNow() + dir, 0, 2);
				docs().setPagesLayout(to, p);
				break;
			}
			case ROW_HINGE: ui.settingsPage->changeSetting(settingRow("manga-hinge"), dir, p); break;
			case ROW_FIT: docs().stepPagesFit(dir, p); break;
			case ROW_ZOOM: ui.pageReader->mangaZoomStep(dir); break;
			case ROW_TURN: docs().stepPagesTurn(dir); break;
			case ROW_LIGHT: close(); ui.openLight(); break;
			default: break;
			}
		}
		bool key(Button b, Player& p) override
		{
			if (gone()) { close(); return false; }
			switch (b)
			{
			case Button::Up: cursor = (cursor + ROWS - 1) % ROWS; break;
			case Button::Down: cursor = (cursor + 1) % ROWS; break;
			case Button::Left: change(cursor, -1, p); break;
			case Button::Right: change(cursor, 1, p); break;
			case Button::A:
				// Layout steps round; the zoom goes to 200% or back, as A on the page
				if (cursor == ROW_LAYOUT) { int to = (docs().pagesLayoutNow() + 1) % 3; docs().setPagesLayout(to, p); }
				else if (cursor == ROW_ZOOM) ui.pageReader->mangaZoomToggle();
				else change(cursor, 1, p);
				break;
			case Button::B: case Button::Start: close(); break;
			// SELECT under the sheet is still Fullscreen: the sheet goes, as when its Layout row turns it on
			case Button::Select: close(); docs().togglePagesFull(p); break;
			default: break;
			}
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			if (gone()) { close(); return false; }
			lay::ListSheet s = layout();
			if (pt.kind != Pointer::Down) return false;
			if (!s.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
			for (int row = 0; row < ROWS; row++)
			{
				Rect r = s.row(row);
				if (!r.hit(x, y)) continue;
				cursor = row;
				if (row == ROW_LAYOUT)
				{
					Rect seg = lay::viewChoices(r);
					for (int i = 0; i < 3; i++)
						if (lay::cell(seg, 3, 6, i).hit(x, y))
						{
							docs().setPagesLayout(i, p);
						}
				}
				else if (row == ROW_LIGHT) change(row, 1, p);
				else
				{
					lay::SettingStepper st = lay::settingStepper(r);
					if (st.back.hit(x, y) || st.fwd.hit(x, y)) change(row, st.back.hit(x, y) ? -1 : 1, p);
				}
			}
			return false;
		}
		void draw(const Player& p) override
		{
			if (gone()) { close(); return; }
			Kit& k = *ui.kit;
			lay::ListSheet s = layout();
			Kit::SheetHead head;
			head.glyph = &G_EYE;
			head.title = "View";
			head.note = "Kept for the next document";
			k.sheet(s, head);
			const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
			{
				// Layout: its label, the three choices as chips
				Rect r = s.row(ROW_LAYOUT);
				Kit::Row row;
				row.font = F_P16;
				row.name = "Layout";
				row.focused = cursor == ROW_LAYOUT;
				k.row(r, row);
				k.segment(lay::viewChoices(r), {"Single page", "Vertical scroll", "Fullscreen"}, docs().pagesLayoutNow());
			}
			auto stepper = [&](int id, const char* label, const std::string& value) {
				Kit::SettingRow sr;
				sr.label = label;
				sr.off = rowOff(id);
				sr.value = !sr.off ? value : id == ROW_HINGE ? "Fullscreen only" : "Single page only";
				sr.focused = cursor == id;
				k.settingRow(s.row(id), sr);
			};
			stepper(ROW_HINGE, "Hinge gap", std::to_string(p.settings.mangaHinge) + " px");
			stepper(ROW_FIT, "Page fit", p.settings.mangaFit == MANGA_FIT_WIDTH ? "Width" : "Page");
			stepper(ROW_ZOOM, "Zoom", std::to_string(st.zoomPct) + "%");
			stepper(ROW_TURN, "Turn", std::to_string(st.rotation) + "\xC2\xB0");
			Kit::SettingRow light;
			light.label = "Light & filter";
			std::string note = ui.lightNote(p);
			light.value = note.empty() ? "Off" : note;
			light.action = true;
			light.focused = cursor == ROW_LIGHT;
			k.settingRow(s.row(ROW_LIGHT), light);
			k.hints(s.hints, {{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"START/B", "Close"}});
		}
		std::string signature(const Player& p) const override
		{
			const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
			char b[96];
			snprintf(b, sizeof b, "%d|%d %d %d %d %d", cursor, st.mode, (int)st.fullscreen, st.zoomPct, st.rotation, p.settings.mangaHinge);
			return b;
		}
	};

	void Ui::Docs::openPagesView()
	{
		if (!ui.pageReader->readerStatus.open) return;
		ui.pushOverlay(std::make_unique<PagesView>(ui));
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
			(pagesFullNavShown() ? 1u : 0u) + (SDL_GetTicks() < docFullToastUntil ? 2u : 0u));
		std::string s = b;
		s += findSignature();
		s += "|bk" + std::to_string(textStatus.book) + " " + std::to_string(textStatus.bottomGen) + (SDL_GetTicks() < docBookToastUntil ? "T" : "");
		s += "|bb" + std::to_string(docBookBar) + "," + std::to_string(docBookBarPressed);
		// (a browser's place: Contents' first row, the options' first)
		if (foreignShown()) s += "|fp" + foreignLabel();
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
