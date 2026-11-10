#include "ui.h"
#include "ui_internal.h"
#include "manga_ui_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace bp
{
	// ---- the manga reader: the store under <state>/manga, the card's scan, the five tabs, a series and the reader ----

	namespace
	{
		const char* const MANGA_TAB_NAMES[5] = {"Library", "History", "Browse", "Sources", "Downloads"};	// Ui::MANGA_TABS

		int chapterIndexOf(const LocalSeries& s, const std::string& chapterId)
		{
			for (size_t i = 0; i < s.chapters.size(); i++)
				if (s.chapters[i].path == chapterId) return (int)i;
			return -1;
		}

		std::string chapterPageLine(int page, int pages)
		{
			if (pages <= 0) return "p. " + std::to_string(page + 1);
			return "p. " + std::to_string(page + 1) + "/" + std::to_string(pages);
		}

		// The tile's rounded corners in the cover's own alpha, at upload (the panel's pixels): the generated cover is
		// rounded by fillRound, and the real one is copied over whatever fill the tile sits on (a page's or a card's),
		// so squaring its corners would draw the wrong colour there. Only the four corner squares are touched.
		void roundCover(std::vector<uint8_t>& rgba, int w, int h)
		{
			int R = std::max(0, (int)std::lround(R_TILE * std::min(sx, sy)));
			R = std::min({R, w / 2, h / 2});
			for (int cy = 0; cy < 2; cy++)
				for (int cx = 0; cx < 2; cx++)
					for (int y = 0; y < R; y++)
						for (int x = 0; x < R; x++)
						{
							int in = 0;
							for (int j = 0; j < 4; j++)
								for (int i = 0; i < 4; i++)
								{
									float dx = R - (x + (i + 0.5f) / 4), dy = R - (y + (j + 0.5f) / 4);
									in += dx * dx + dy * dy <= (float)R * R;
								}
							int px = cx ? w - 1 - x : x, py = cy ? h - 1 - y : y;
							uint8_t* p = rgba.data() + ((size_t)py * (size_t)w + (size_t)px) * 4;
							p[3] = (uint8_t)(p[3] * in / 16);
						}
		}
	}

	void Ui::mangaOpen(Player& p)
	{
		(void)p;
		mangaTab = MT_LIBRARY;	// the app opens on the library, as the mock has it
		mangaSeriesId.clear();
		mBrowseList = false;
		mlibZone = mlibCard = mlibGrid = mlibScroll = 0;
		mhisCursor = mhisScroll = 0;
		mbrowseCursor = mListCursor = mListScroll = 0;
		mangaClearAt = 0;
		mangaPressed = -1;
		mangaTouch = mangaSlider = false;
		mangaSliderPage = -1;
		mangaScan();
		// a new open wants its own covers: what waited for the last one is dropped, and a cover that failed (a file
		// fixed meanwhile) is tried once more. The ones that arrived stay: a cache of textures is worth keeping
		if (coverWorker) coverWorker->cancel();
		coverAsked.clear();
		coverFailed.clear();
	}

	void Ui::mangaScan()
	{
		if (!mangaStore)
		{
			mangaStore = std::make_unique<MangaStore>();
			mangaStore->load(stateDirPath);
		}
		// the Files places' Manga folders, each read once. Every series found goes in the store; one the user took out
		// of the library stays out (the store's inLibrary wins over the scan)
		std::vector<std::string> places;
		places.reserve(roots.size());
		for (auto& r : roots) places.push_back(r.path);
		Uint32 t0 = SDL_GetTicks();
		std::vector<LocalSeries> found;
		for (auto& dir : mangaRoots(places))
		{
			std::vector<LocalSeries> one = scanMangaFolder(dir);
			found.insert(found.end(), one.begin(), one.end());
		}
		mangaLocal = std::move(found);
		mangaLocalById.clear();
		for (size_t i = 0; i < mangaLocal.size(); i++)
			mangaLocalById["local:" + mangaLocal[i].path] = i;
		int64_t now = (int64_t)time(nullptr);
		for (auto& s : mangaLocal)
		{
			std::string id = "local:" + s.path;
			MangaSeries m;
			m.id = id;
			m.title = s.title;
			m.source = "local";
			m.cover = s.cover;
			if (const MangaSeries* old = mangaStore->series(id))
			{
				m.added = old->added;
				m.lastRead = old->lastRead;
				m.inLibrary = old->inLibrary;
				m.readingMode = old->readingMode;
			}
			else m.added = now;
			mangaStore->putSeries(m);
		}
		fprintf(stderr, "manga: scanned %d series in %u ms\n", (int)mangaLocal.size(), (unsigned)(SDL_GetTicks() - t0));
		mangaCacheValid = false;
		mangaSave();
	}

	void Ui::mangaSave()
	{
		if (mangaStore) mangaStore->save();
		mangaDirty = false;
		mangaSavedAt = SDL_GetTicks();
	}

	void Ui::mangaCache() const
	{
		uint64_t serial = mangaStore ? mangaStore->serial() : 0;
		if (mangaCacheValid && mangaCacheSerial == serial && mangaCacheCount == (int)mangaLocal.size()) return;
		mangaCacheValid = true;
		mangaCacheSerial = serial;
		mangaCacheCount = (int)mangaLocal.size();
		mangaLib.clear();
		mangaLibUnread.clear();
		mangaCards.clear();
		mangaHistory.clear();
		if (!mangaStore) return;
		mangaLib = mangaStore->library();
		for (auto& s : mangaLib)
		{
			const LocalSeries* l = mangaLocalSeries(s.id);
			if (!l)
			{
				mangaLibUnread.push_back(0);
				continue;
			}
			std::vector<std::string> ids;
			ids.reserve(l->chapters.size());
			for (auto& c : l->chapters) ids.push_back(c.path);
			mangaLibUnread.push_back(mangaStore->unread(s.id, ids));
		}
		mangaCards = mangaStore->continueReading(3);
		mangaHistory = mangaStore->history(50);
	}

	const LocalSeries* Ui::mangaLocalSeries(const std::string& id) const
	{
		auto it = mangaLocalById.find(id);
		return it == mangaLocalById.end() || it->second >= mangaLocal.size() ? nullptr : &mangaLocal[it->second];
	}

	std::string Ui::mangaTitle(const std::string& id) const
	{
		if (mangaStore)
			if (const MangaSeries* s = mangaStore->series(id)) return s->title;
		const LocalSeries* l = mangaLocalSeries(id);
		return l ? l->title : "";
	}

	int Ui::mangaReadChapterIndex() const
	{
		const LocalSeries* s = mangaLocalSeries(mangaReadSeriesId);
		return s ? chapterIndexOf(*s, mangaReadChapterId) : -1;
	}

	void Ui::mangaRecordProgress()
	{
		// a record of where the reader is: the reader's own status is the only thing that knows the page
		if (!mangaStore || mangaReadChapterId.empty() || readerStatus.pages <= 0) return;
		ChapterProgress cp;
		cp.seriesId = mangaReadSeriesId;
		cp.chapterId = mangaReadChapterId;
		cp.chapterTitle = mangaReadChapterTitle;
		cp.page = readerStatus.page;
		cp.pages = readerStatus.pages;
		cp.read = readerStatus.page >= readerStatus.pages - 1;
		cp.at = (int64_t)time(nullptr);
		mangaStore->setProgress(cp);
		mangaProgPage = cp.page;
		mangaProgPages = cp.pages;
		mangaDirty = true;
	}

	void Ui::mangaTick()
	{
		// main turns the page; a change is this chapter's new place. The store is written once a second at most
		if (readerStatus.open) mangaReadLive = true;
		if (readerStatus.pages > 0 && (readerStatus.page != mangaProgPage || readerStatus.pages != mangaProgPages))
			mangaRecordProgress();
		if (mangaDirty && SDL_GetTicks() - mangaSavedAt >= 1000) mangaSave();
	}

	void Ui::mangaCloseReader()
	{
		mangaRecordProgress();
		mangaSave();
		readerCmds.push_back({ReaderCmd::Close, 0, (int64_t)SDL_GetTicks()});
		page = Page::MangaSeries;
		mangaReadLive = false;
		mangaProgPage = mangaProgPages = -1;
	}

	void Ui::openMangaSeries(const std::string& id, int from, Player& p)
	{
		(void)p;
		mangaSeriesId = id;
		mangaSeriesFrom = from;
		mserCursor = -1;
		mserScroll = 0;
		page = Page::MangaSeries;
	}

	void Ui::mangaOpenChapter(const LocalSeries& s, int i, int page0, Player& p)
	{
		(void)p;
		if (i < 0 || i >= (int)s.chapters.size()) return;
		// the chapter open so far is saved before the next one replaces it in readerStatus
		if (page == Page::MangaRead)
		{
			mangaRecordProgress();
			mangaSave();
		}
		const LocalChapter& c = s.chapters[i];
		mangaSeriesId = "local:" + s.path;
		mangaReadSeriesId = mangaSeriesId;
		mangaReadChapterId = c.path;
		mangaReadChapterTitle = c.title;
		mangaRead = {mangaSeriesId, s.title, c.path, c.title, c.path, c.archive, std::max(0, page0), mangaModeFor(mangaSeriesId, p)};
		// the status still holds the chapter before: its numbers aren't this chapter's, so only a change records
		mangaProgPage = readerStatus.page;
		mangaProgPages = readerStatus.pages;
		mangaReadLive = false;	// (main hasn't opened it yet: its open false isn't a close)
		mangaSliderPage = -1;
		page = Page::MangaRead;
	}

	void Ui::mangaChapterStep(int dir, Player& p)
	{
		const LocalSeries* s = mangaLocalSeries(mangaReadSeriesId);
		int i = s ? chapterIndexOf(*s, mangaReadChapterId) : -1;
		if (!s || i < 0) return;
		int j = i + dir;
		if (j < 0 || j >= (int)s->chapters.size()) return;
		mangaOpenChapter(*s, j, 0, p);
	}

	void Ui::mangaSeek(int page)
	{
		int pages = readerStatus.pages;
		if (pages <= 0) return;
		page = std::clamp(page, 0, pages - 1);
		if (page == mangaSliderPage) return;
		mangaSliderPage = page;
		readerCmds.push_back({ReaderCmd::Seek, (double)page, (int64_t)SDL_GetTicks()});
	}

	void Ui::mangaSeriesChoose(int i, Player& p)
	{
		const LocalSeries* s = mangaLocalSeries(mangaSeriesId);
		if (!s || i < 0 || i >= (int)s->chapters.size()) return;
		// its saved page, unless it was read to its end (then the start again)
		int page0 = 0;
		if (mangaStore)
			if (const ChapterProgress* pr = mangaStore->progress(s->chapters[i].path))
				if (!pr->read) page0 = pr->page;
		mangaOpenChapter(*s, i, page0, p);
	}

	void Ui::mangaSeriesResume(Player& p)
	{
		const LocalSeries* s = mangaLocalSeries(mangaSeriesId);
		if (!s || s->chapters.empty()) return;
		std::vector<MangaChapterMark> marks;
		marks.reserve(s->chapters.size());
		for (auto& c : s->chapters)
		{
			MangaChapterMark m;
			if (mangaStore)
				if (const ChapterProgress* pr = mangaStore->progress(c.path))
				{
					m.read = pr->read;
					m.started = pr->pages > 0 || pr->page > 0;
					m.page = pr->page;
				}
			marks.push_back(m);
		}
		MangaResumePoint r = mangaResume(marks);
		if (r.chapter >= 0) mangaOpenChapter(*s, r.chapter, r.page, p);
	}

	void Ui::mangaChoose(int tab, int i, Player& p)
	{
		mangaCache();
		if (tab == MT_LIBRARY)
		{
			if (mlibZone == 0)
			{
				// a Continue reading card: the chapter at the page it was left
				if (i < 0 || i >= (int)mangaCards.size()) return;
				const ChapterProgress& cp = mangaCards[i];
				const LocalSeries* s = mangaLocalSeries(cp.seriesId);
				int ci = s ? chapterIndexOf(*s, cp.chapterId) : -1;
				if (!s || ci < 0) return;
				mangaSeriesFrom = 0;
				mangaSeriesId = cp.seriesId;
				mangaOpenChapter(*s, ci, cp.page, p);
				return;
			}
			if (i >= 0 && i < (int)mangaLib.size()) openMangaSeries(mangaLib[i].id, 0, p);
			return;
		}
		if (tab == MT_HISTORY)
		{
			if (i < 0 || i >= (int)mangaHistory.size()) return;
			const ChapterProgress& cp = mangaHistory[i];
			const LocalSeries* s = mangaLocalSeries(cp.seriesId);
			int ci = s ? chapterIndexOf(*s, cp.chapterId) : -1;
			if (!s || ci < 0) return;
			mangaSeriesFrom = 2;
			mangaSeriesId = cp.seriesId;
			mangaOpenChapter(*s, ci, cp.page, p);
			return;
		}
		if (tab == MT_BROWSE)
		{
			if (mBrowseList)
			{
				if (i < 0 || i >= (int)mangaLocal.size()) return;
				openMangaSeries("local:" + mangaLocal[i].path, 1, p);
				return;
			}
			mBrowseList = true;
			mListCursor = 0;
			mListScroll = 0;
		}
	}

	// ---- the keys ----

	bool Ui::mangaKey(Button b, Player& p)
	{
		// the reader: its own controls (the mini bar isn't on this page)
		if (page == Page::MangaRead)
		{
			Uint32 now = SDL_GetTicks();
			switch (b)
			{
			case Button::Up:
			case Button::Left: readerCmds.push_back({ReaderCmd::Step, -1, (int64_t)now}); break;
			case Button::Down:
			case Button::Right:
				// forward at the end leaves for the next chapter; before it, the reader steps on
				if (readerStatus.atEnd) mangaChapterStep(1, p);
				else readerCmds.push_back({ReaderCmd::Step, 1, (int64_t)now});
				break;
			case Button::L: readerCmds.push_back({ReaderCmd::Page, -1, (int64_t)now}); break;
			case Button::R: readerCmds.push_back({ReaderCmd::Page, 1, (int64_t)now}); break;
			case Button::X: mangaCycleMode(p); break;
			case Button::L2: mangaChapterStep(-1, p); break;
			case Button::R2: mangaChapterStep(1, p); break;
			case Button::B: mangaCloseReader(); break;
			default: break;
			}
			return false;
		}
		// (the mini bar shown on the app's other pages says SELECT goes to what plays)
		if (b == Button::Select && nowPlayable(p)) { goNowPlaying(p); return false; }
		if (page == Page::MangaSeries)
		{
			const LocalSeries* s = mangaLocalSeries(mangaSeriesId);
			int n = s ? (int)s->chapters.size() : 0;
			if (b == Button::B)
			{
				// back to where it was opened from: the Library, Browse's list or History
				page = Page::Manga;
				mangaTab = mangaSeriesFrom == 1 ? MT_BROWSE : mangaSeriesFrom == 2 ? MT_HISTORY : MT_LIBRARY;
				mBrowseList = mangaSeriesFrom == 1;
				return false;
			}
			if (b == Button::Y)
			{
				if (mangaStore)
					if (const MangaSeries* ms = mangaStore->series(mangaSeriesId))
					{
						mangaStore->setInLibrary(mangaSeriesId, !ms->inLibrary);
						mangaSave();
					}
				return false;
			}
			if (b == Button::A)
			{
				if (mserCursor < 0) mangaSeriesResume(p);
				else mangaSeriesChoose(mserCursor, p);
				return false;
			}
			if (b == Button::X)
			{
				if (s && mserCursor >= 0 && mserCursor < n && mangaStore)
				{
					const std::string& cid = s->chapters[mserCursor].path;
					const ChapterProgress* pr = mangaStore->progress(cid);
					mangaStore->markRead(mangaSeriesId, {cid}, !(pr && pr->read));
					mangaSave();
				}
				return false;
			}
			// Resume is the row before the chapters
			int total = n + 1, cur = mserCursor + 1;
			switch (b)
			{
			case Button::Up: cur = (cur + total - 1) % total; break;
			case Button::Down: cur = (cur + 1) % total; break;
			case Button::Left: cur = std::max(0, cur - MG_SER_ROWS); break;
			case Button::Right: cur = std::min(total - 1, cur + MG_SER_ROWS); break;
			default: break;
			}
			mserCursor = cur - 1;
			if (mserCursor >= 0)
			{
				if (mserCursor < mserScroll) mserScroll = mserCursor;
				if (mserCursor >= mserScroll + MG_SER_ROWS) mserScroll = mserCursor - MG_SER_ROWS + 1;
			}
			return false;
		}
		// Page::Manga
		if (b == Button::B)
		{
			// Browse's "On this handheld": one step back to its rows first
			if (mangaTab == MT_BROWSE && mBrowseList) mBrowseList = false;
			else openExtensionsDrawer(p);
			return false;
		}
		if (b == Button::L || b == Button::R)
		{
			mangaTab = std::clamp(mangaTab + (b == Button::R ? 1 : -1), 0, (int)MANGA_TABS - 1);
			mangaClearAt = 0;
			mBrowseList = false;
			mangaPressed = -1;
			return false;
		}
		switch (mangaTab)
		{
		case MT_LIBRARY:
		{
			mangaCache();
			int n = (int)mangaLib.size(), cards = std::min(3, (int)mangaCards.size());
			if (n && mlibGrid >= n) mlibGrid = n - 1;
			if (mlibCard >= cards) mlibCard = std::max(0, cards - 1);
			// nothing started yet: there are no cards to be on, the grid has the focus
			if (mlibZone == 0 && cards == 0) mlibZone = 1;
			if (b == Button::A)
			{
				if (mlibZone == 0) mangaChoose(MT_LIBRARY, mlibCard, p);
				else if (n) mangaChoose(MT_LIBRARY, mlibGrid, p);
				break;
			}
			if (mlibZone == 0)
			{
				switch (b)
				{
				case Button::Left: if (mlibCard > 0) mlibCard--; break;
				case Button::Right: if (mlibCard + 1 < cards) mlibCard++; break;
				// down to the grid, in the card's column
				case Button::Down:
					if (n) { mlibZone = 1; mlibGrid = std::min(mlibCard, n - 1); mlibScroll = mlibGrid / MG_COLS; }
					break;
				default: break;
				}
				break;
			}
			int row = mlibGrid / MG_COLS, col = mlibGrid % MG_COLS, rows = (n + MG_COLS - 1) / MG_COLS;
			switch (b)
			{
			case Button::Left: if (col > 0) mlibGrid--; break;
			case Button::Right: if (col + 1 < MG_COLS && mlibGrid + 1 < n) mlibGrid++; break;
			// up from the grid's first row reaches the cards
			case Button::Up:
				if (row > 0) { mlibGrid -= MG_COLS; mlibScroll = mlibGrid / MG_COLS; }
				else if (cards) { mlibZone = 0; mlibCard = std::min(col, cards - 1); }
				break;
			case Button::Down:
				if (row + 1 < rows) { mlibGrid += MG_COLS; mlibScroll = mlibGrid / MG_COLS; }
				break;
			default: break;
			}
			break;
		}
		case MT_HISTORY:
		{
			mangaCache();
			int n = (int)mangaHistory.size();
			if (b == Button::X)
			{
				// Clear is asked twice, as the browser's clears are
				Uint32 now = SDL_GetTicks();
				if (mangaClearAt && now - mangaClearAt < QUIT_WINDOW_MS)
				{
					if (mangaStore) { mangaStore->clearHistory(); mangaSave(); }
					mangaClearAt = 0;
					mhisCursor = mhisScroll = 0;
				}
				else mangaClearAt = now;
				break;
			}
			if (b == Button::A) { mangaChoose(MT_HISTORY, mhisCursor, p); break; }
			if (!n) break;
			switch (b)
			{
			case Button::Up: mhisCursor = (mhisCursor + n - 1) % n; break;
			case Button::Down: mhisCursor = (mhisCursor + 1) % n; break;
			case Button::Left: mhisCursor = std::max(0, mhisCursor - MG_ROWS); break;
			case Button::Right: mhisCursor = std::min(n - 1, mhisCursor + MG_ROWS); break;
			default: break;
			}
			if (mhisCursor < mhisScroll) mhisScroll = mhisCursor;
			if (mhisCursor >= mhisScroll + MG_ROWS) mhisScroll = mhisCursor - MG_ROWS + 1;
			break;
		}
		case MT_BROWSE:
		{
			if (mBrowseList)
			{
				int n = (int)mangaLocal.size();
				if (b == Button::A) { mangaChoose(MT_BROWSE, mListCursor, p); break; }
				if (!n) break;
				switch (b)
				{
				case Button::Up: mListCursor = (mListCursor + n - 1) % n; break;
				case Button::Down: mListCursor = (mListCursor + 1) % n; break;
				case Button::Left: mListCursor = std::max(0, mListCursor - MG_ROWS); break;
				case Button::Right: mListCursor = std::min(n - 1, mListCursor + MG_ROWS); break;
				default: break;
				}
				if (mListCursor < mListScroll) mListScroll = mListCursor;
				if (mListCursor >= mListScroll + MG_ROWS) mListScroll = mListCursor - MG_ROWS + 1;
				break;
			}
			if (b == Button::A) mangaChoose(MT_BROWSE, 0, p);
			break;
		}
		default: break;	// Sources and Downloads: nothing to choose yet
		}
		return false;
	}

	// ---- a finger ----

	bool Ui::mangaPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (page == Page::MangaRead)
		{
			// the drag area and the slider follow the finger; the buttons act as it lifts
			if (pt.kind == Pointer::Down)
			{
				mangaPressed = -1;
				mangaTouch = mangaSlider = false;
				mangaSliderPage = -1;
				if (buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
				if (mangaAreaRect().hit(x, y))
				{
					mangaTouch = true;
					mangaTouchY = pt.y;
					mangaTouchX = pt.x;
					readerCmds.push_back({ReaderCmd::TouchDown, 0, (int64_t)SDL_GetTicks()});
					return false;
				}
				if (mangaSliderRect().hit(x, y))
				{
					mangaSlider = true;
					int pages = readerStatus.pages;
					if (pages > 1)
					{
						Rect tr = mangaSliderRect();
						mangaSeek((int)std::lround(std::clamp((x - tr.x) / (double)tr.w, 0.0, 1.0) * (pages - 1)));
					}
					return false;
				}
				const LocalSeries* s = mangaLocalSeries(mangaReadSeriesId);
				int ci = s ? chapterIndexOf(*s, mangaReadChapterId) : -1;
				if (s && ci > 0 && mangaPrevRect().hit(x, y)) mangaPressed = MG_PREV;
				else if (s && ci >= 0 && ci + 1 < (int)s->chapters.size() && mangaNextRect().hit(x, y)) mangaPressed = MG_NEXT;
				return false;
			}
			if (pt.kind == Pointer::Move)
			{
				if (mangaTouch)
				{
					// the finger's own y is already 0..1: its step goes as it is
					double dy = pt.y - mangaTouchY, dx = pt.x - mangaTouchX;
					mangaTouchY = pt.y;
					mangaTouchX = pt.x;
					// (both: the strip goes by dy, the paged modes by dx)
					if (dy != 0 || dx != 0) readerCmds.push_back({ReaderCmd::Drag, dy, (int64_t)SDL_GetTicks(), dx});
				}
				else if (mangaSlider && readerStatus.pages > 1)
				{
					Rect tr = mangaSliderRect();
					mangaSeek((int)std::lround(std::clamp((x - tr.x) / (double)tr.w, 0.0, 1.0) * (readerStatus.pages - 1)));
				}
				return false;
			}
			if (mangaTouch)
			{
				mangaTouch = false;
				readerCmds.push_back({ReaderCmd::Release, 0, (int64_t)SDL_GetTicks()});
				return false;
			}
			if (mangaSlider)
			{
				mangaSlider = false;
				if (readerStatus.pages > 1)
				{
					Rect tr = mangaSliderRect();
					mangaSeek((int)std::lround(std::clamp((x - tr.x) / (double)tr.w, 0.0, 1.0) * (readerStatus.pages - 1)));
				}
				return false;
			}
			if (mangaPressed >= 0)
			{
				int id = mangaPressed;
				mangaPressed = -1;
				if (id == MG_PREV && mangaPrevRect().hit(x, y)) mangaChapterStep(-1, p);
				else if (id == MG_NEXT && mangaNextRect().hit(x, y)) mangaChapterStep(1, p);
			}
			return false;
		}
		// the tabs, a card, a cover, a row: a tap acts on the spot, as the browser's tabs do
		if (pt.kind != Pointer::Down) return false;
		mangaPressed = -1;
		if (page == Page::Manga)
		{
			for (int i = 0; i < MANGA_TABS; i++)
				if (mangaTabRect(i).hit(x, y))
				{
					mangaTab = i;
					mangaClearAt = 0;
					mBrowseList = false;
					return false;
				}
			if (mangaTab == MT_LIBRARY)
			{
				mangaCache();
				int n = (int)mangaLib.size(), cards = std::min(3, (int)mangaCards.size());
				for (int i = 0; i < cards; i++)
					if (mangaCardRect(i).hit(x, y))
					{
						mlibZone = 0;
						mlibCard = i;
						mangaChoose(MT_LIBRARY, i, p);
						return false;
					}
				for (int i = mlibScroll * MG_COLS; i < n; i++)
				{
					Rect r = mangaCoverRect(i);
					if (r.y >= 416) break;
					if (r.hit(x, y))
					{
						mlibZone = 1;
						mlibGrid = i;
						mlibScroll = i / MG_COLS;
						mangaChoose(MT_LIBRARY, i, p);
						return false;
					}
				}
			}
			else if (mangaTab == MT_HISTORY)
			{
				mangaCache();
				int n = (int)mangaHistory.size();
				if (y >= MG_ROW_TOP && y < MG_ROW_TOP + MG_ROWS * ROW_H)
				{
					int i = mhisScroll + (y - MG_ROW_TOP) / ROW_H;
					if (i >= 0 && i < n)
					{
						mhisCursor = i;
						mangaChoose(MT_HISTORY, i, p);
					}
				}
			}
			else if (mangaTab == MT_BROWSE)
			{
				if (mBrowseList)
				{
					int n = (int)mangaLocal.size();
					if (y >= MG_ROW_TOP && y < MG_ROW_TOP + MG_ROWS * ROW_H)
					{
						int i = mListScroll + (y - MG_ROW_TOP) / ROW_H;
						if (i >= 0 && i < n)
						{
							mListCursor = i;
							mangaChoose(MT_BROWSE, i, p);
						}
					}
				}
				else if (Rect{16, MG_ROW_TOP, 608, ROW_H - 4}.hit(x, y)) mangaChoose(MT_BROWSE, 0, p);
			}
			return false;
		}
		// Page::MangaSeries: Resume, the heart, a chapter
		if (mangaResumeRect().hit(x, y))
		{
			mserCursor = -1;
			mangaSeriesResume(p);
			return false;
		}
		if (mangaHeartRect().hit(x, y))
		{
			if (mangaStore)
				if (const MangaSeries* ms = mangaStore->series(mangaSeriesId))
				{
					mangaStore->setInLibrary(mangaSeriesId, !ms->inLibrary);
					mangaSave();
				}
			return false;
		}
		const LocalSeries* s = mangaLocalSeries(mangaSeriesId);
		int n = s ? (int)s->chapters.size() : 0;
		if (n && y >= MG_SER_TOP && y < MG_SER_TOP + MG_SER_ROWS * ROW_H)
		{
			int i = mserScroll + (y - MG_SER_TOP) / ROW_H;
			if (i >= 0 && i < n)
			{
				mserCursor = i;
				mangaSeriesChoose(i, p);
			}
		}
		return false;
	}

	// ---- the drawing ----

	SDL_Texture* Ui::coverOf(const std::string& seriesId)
	{
		if (seriesId.empty()) return nullptr;
		auto it = coverTex.find(seriesId);
		if (it != coverTex.end())
		{
			// drawn now: the last to go when the cache is full
			it->second.drawnAt = ++coverDrawn;
			return it->second.tex;
		}
		if (!coverWorker || coverAsked.count(seriesId) || coverFailed.count(seriesId)) return nullptr;
		const LocalSeries* s = mangaLocalSeries(seriesId);
		// nothing to read is nothing to ask: the generated cover is the answer
		if (!s || s->cover.empty())
		{
			coverFailed.insert(seriesId);
			return nullptr;
		}
		coverWorker->want(seriesId, s->cover, std::max(1, (int)std::lround(MG_COVER_W * sx)),
			std::max(1, (int)std::lround(MG_COVER_H * sy)));
		coverAsked.insert(seriesId);
		return nullptr;
	}

	void Ui::coversReady(std::vector<CoverWorker::Result> results)
	{
		for (auto& r : results)
		{
			coverAsked.erase(r.key);
			SDL_Texture* t = nullptr;
			if (r.ok)
			{
				roundCover(r.rgba, r.w, r.h);
				t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, r.w, r.h);
				if (t)
				{
					SDL_UpdateTexture(t, nullptr, r.rgba.data(), r.w * 4);
					SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);	// (its corners are transparent)
				}
			}
			if (!t)
			{
				// the generated one stays, and stays for good: asking a broken file again each frame helps no one
				coverFailed.insert(r.key);
				coverSerial++;
				continue;
			}
			auto it = coverTex.find(r.key);
			if (it != coverTex.end() && it->second.tex) SDL_DestroyTexture(it->second.tex);
			coverTex[r.key] = {t, ++coverDrawn};
			coverSerial++;
		}
		// the design's cap: the covers drawn longest ago go first (what is on screen was drawn this frame)
		while (coverTex.size() > COVER_KEEP)
		{
			auto oldest = coverTex.end();
			for (auto i = coverTex.begin(); i != coverTex.end(); ++i)
				if (oldest == coverTex.end() || i->second.drawnAt < oldest->second.drawnAt) oldest = i;
			if (oldest->second.tex) SDL_DestroyTexture(oldest->second.tex);
			coverTex.erase(oldest);
		}
	}

	void Ui::drawMangaCover(Rect r, const std::string& title, bool focus, int bookCell, const std::string& seriesId)
	{
		Rect in = r;
		int rad = R_TILE;
		if (focus)
		{
			// the focused cover's ACCENT edge, the gradient inside it
			fillRound(r, ACCENT, R_TILE);
			in = {r.x + theme::EDGE, r.y + theme::EDGE, r.w - 2 * theme::EDGE, r.h - 2 * theme::EDGE};
			rad = R_TILE - theme::EDGE;
		}
		if (SDL_Texture* tex = coverOf(seriesId))
		{
			// the real page over the tile's place, its own corners rounded at upload: the ACCENT edge still shows
			SDL_Rect d = devRect(in);
			SDL_RenderCopy(ren, tex, nullptr, &d);
			return;
		}
		// a vertical gradient of bands, each drawn as part of the rounded box, so its corners stay round
		MangaCoverColors c = mangaCoverColors(title);
		const int step = 8;
		for (int y = 0; y < in.h; y += step)
		{
			int y1 = std::min(in.h, y + step);
			double t = in.h > 1 ? std::clamp((y + (y1 - y) / 2.0) / (in.h - 1), 0.0, 1.0) : 0;
			SDL_Color col{(Uint8)std::lround(c.topR + (c.bottomR - c.topR) * t),
				(Uint8)std::lround(c.topG + (c.bottomG - c.topG) * t),
				(Uint8)std::lround(c.topB + (c.bottomB - c.topB) * t), 255};
			fillRoundBand(in, rad, in.y + y, in.y + y1, col);
		}
		drawGlyphMid(G_BOOK, in.x + in.w / 2, in.y + in.h / 2, DIM, bookCell);
	}

	void Ui::drawMangaTabs()
	{
		Rect first = mangaTabRect(0), last = mangaTabRect(MANGA_TABS - 1);
		drawTextMid("L", 0, ACCENT, first.x - 12, first.y + first.h / 2, 0, 2);
		for (int i = 0; i < MANGA_TABS; i++)
			drawTab(mangaTabRect(i), MANGA_TAB_NAMES[i], i == mangaTab, 0);
		drawTextMid("R", 0, ACCENT, last.x + last.w + 12, last.y + last.h / 2, 0, 2);
	}

	void Ui::drawManga(const Player& p)
	{
		drawHeader("Manga", p);
		drawMangaTabs();
		switch (mangaTab)
		{
		case MT_LIBRARY: drawMangaLibrary(p); break;
		case MT_HISTORY: drawMangaHistory(p); break;
		case MT_BROWSE: drawMangaBrowse(p); break;
		case MT_SOURCES:
			// what the design says, in the empty page's look: repositories are the user's own
			drawEmpty(G_BLOCKS, "Sources come next",
				"Repositories are added by you. Banana Player doesn\xE2\x80\x99t\nprovide or check their content.", nullptr);
			drawHints({{"B", "Back"}});
			break;
		case MT_DOWNLOADS:
			drawEmpty(G_DOWNLOAD, "Downloads come next", "", nullptr);
			drawHints({{"B", "Back"}});
			break;
		}
	}

	void Ui::drawMangaLibrary(const Player& p)
	{
		(void)p;
		mangaCache();
		drawTextMid("Continue reading", 0, DIM, 16, 102);
		int cards = std::min(3, (int)mangaCards.size());
		for (int i = 0; i < cards; i++)
		{
			const ChapterProgress& cp = mangaCards[i];
			Rect r = mangaCardRect(i);
			bool at = mlibZone == 0 && mlibCard == i;
			drawBox(r, mangaPressed == MG_CARD + i ? PRESSED : at ? BUTTON : SURFACE, R_TILE, at);
			SDL_Color ink = at ? ACCENT : TEXT, soft = at ? DIM_ON : DIM;
			std::string title = mangaTitle(cp.seriesId);
			drawMangaCover({r.x + 6, r.y + 4, 40, 54}, title, false, 2, cp.seriesId);
			drawTextMid(title, 0, ink, r.x + 54, r.y + 18, r.w - 62);
			// ("Ch." only before a bare title: "Chapter 1" says it already)
			bool says = cp.chapterTitle.rfind("Ch", 0) == 0 || cp.chapterTitle.rfind("ch", 0) == 0;
			drawTextMid((says ? "" : "Ch. ") + cp.chapterTitle + " \xC2\xB7 " + chapterPageLine(cp.page, cp.pages), 0, soft, r.x + 54, r.y + 36, r.w - 62);
			double frac = cp.pages > 0 ? std::clamp((cp.page + 1) / (double)cp.pages, 0.0, 1.0) : 0;
			int w = (int)std::lround((r.w - 62) * frac);
			drawLevel({r.x + 54, r.y + 50, r.w - 62, 4}, w, TRACK, at ? ACCENT : DIM);
		}
		drawTextMid("Library \xC2\xB7 " + std::to_string(mangaLib.size()) + " series", 0, DIM, 16, 194);
		int n = (int)mangaLib.size();
		if (n == 0)
		{
			mlibZone = 0;
			drawEmpty(G_BOOK, "Your library is empty",
				"Put CBZ files or folders of pictures in a Manga folder on your card, or add series from Browse.", nullptr);
			drawHints({{"B", "Back"}});
			return;
		}
		int rows = (n + MG_COLS - 1) / MG_COLS;
		if (mlibGrid >= n) mlibGrid = n - 1;
		if (mlibScroll >= rows) mlibScroll = rows - 1;
		if (mlibCard >= cards) mlibCard = std::max(0, cards - 1);
		if (mlibZone == 0 && cards == 0) mlibZone = 1;	// (no cards: the grid has the focus, as the keys go)
		// the grid, a row at a time: the next row peeks under this one, cut at the mini bar
		SDL_Rect clip{0, (int)std::lround(200 * sy), (int)std::lround(W * sx), (int)std::lround((416 - 200) * sy)};
		SDL_RenderSetClipRect(ren, &clip);
		for (int i = mlibScroll * MG_COLS; i < n; i++)
		{
			Rect r = mangaCoverRect(i);
			if (r.y >= 416) break;
			bool at = mlibZone == 1 && mlibGrid == i;
			drawMangaCover(r, mangaLib[i].title, at, 4, mangaLib[i].id);
			drawTextMid(mangaLib[i].title, 0, at ? ACCENT : TEXT, r.x + r.w / 2, r.y + r.h + 13, r.w, 2);
			int unread = i < (int)mangaLibUnread.size() ? mangaLibUnread[i] : 0;
			if (unread > 0)
			{
				// its unread count, a chip at the cover's top right
				std::string t = std::to_string(unread);
				int tw = (int)std::lround(textWidth(t, 0) / sx) + 14;
				fillRound({r.x + r.w - tw - 4, r.y + 4, tw, 20}, ACCENT, R_CHIP);
				drawTextMid(t, 0, ON_ACCENT, r.x + r.w - tw / 2 - 4, r.y + 14, tw - 6, 2);
			}
		}
		SDL_RenderSetClipRect(ren, nullptr);
		drawScrollBar(212, 204, mlibScroll, 2, rows);
		drawHints({{"D-PAD", "Move"}, {"A", mlibZone == 0 ? "Continue" : "Open"}, {"B", "Back"}});
	}

	void Ui::drawMangaHistory(const Player& p)
	{
		(void)p;
		mangaCache();
		if (mangaClearAt && SDL_GetTicks() - mangaClearAt >= QUIT_WINDOW_MS) mangaClearAt = 0;
		int n = (int)mangaHistory.size();
		if (n == 0)
		{
			drawEmpty(G_CLOCK, "Nothing read yet", "Chapters you open show up here.", nullptr);
			drawHints({{"B", "Back"}});
			return;
		}
		if (mhisCursor >= n) mhisCursor = n - 1;
		for (int i = mhisScroll; i < n && i < mhisScroll + MG_ROWS; i++)
		{
			const ChapterProgress& cp = mangaHistory[i];
			int y = MG_ROW_TOP + (i - mhisScroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
			bool at = i == mhisCursor;
			if (at) drawFocus({16, y, 608, ROW_H - 4});
			SDL_Color ink = at ? ACCENT : TEXT, soft = at ? DIM_ON : DIM;
			drawTextMid(mangaTitle(cp.seriesId), 1, ink, 32, mid - 9, 470);
			drawTextMid(cp.chapterTitle + " \xC2\xB7 " + chapterPageLine(cp.page, cp.pages), 0, soft, 32, mid + 11, 470);
			drawTextMid(mangaAgo(cp.at, (int64_t)time(nullptr)), 0, soft, 608, mid, 120, 1);
		}
		drawScrollBar(MG_ROW_TOP, MG_ROWS * ROW_H - 4, mhisScroll, MG_ROWS, n);
		if (mangaClearAt) drawFooterText("Press X again to clear history", ACCENT);
		else drawHints({{"A", "Continue"}, {"X", "Clear history"}, {"B", "Back"}});
	}

	void Ui::drawMangaBrowse(const Player& p)
	{
		(void)p;
		mangaCache();
		int n = (int)mangaLocal.size();
		if (mBrowseList)
		{
			if (n == 0)
			{
				drawEmpty(G_FOLDER, "No series found",
					"Put CBZ files or folders of pictures in a Manga folder on your card.", nullptr);
				drawHints({{"B", "Back"}});
				return;
			}
			if (mListCursor >= n) mListCursor = n - 1;
			for (int i = mListScroll; i < n && i < mListScroll + MG_ROWS; i++)
			{
				const LocalSeries& s = mangaLocal[i];
				int y = MG_ROW_TOP + (i - mListScroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
				bool at = i == mListCursor;
				if (at) drawFocus({16, y, 608, ROW_H - 4});
				drawTextMid(s.title, 1, at ? ACCENT : TEXT, 32, mid, 470);
				drawTextMid(std::to_string(s.chapters.size()) + (s.chapters.size() == 1 ? " chapter" : " chapters"),
					0, at ? DIM_ON : DIM, 608, mid, 160, 1);
			}
			drawScrollBar(MG_ROW_TOP, MG_ROWS * ROW_H - 4, mListScroll, MG_ROWS, n);
			drawHints({{"A", "Open"}, {"B", "Back"}});
			return;
		}
		// the sources this version has, then the ones still to come (dim: nothing to choose in them)
		static const char* SOON[2] = {"Network folders \xC2\xB7 coming next",
			"Your servers (Komga, Kavita, OPDS) \xC2\xB7 coming next"};
		drawFocus({16, MG_ROW_TOP, 608, ROW_H - 4});
		drawTextMid("On this handheld", 1, ACCENT, 32, MG_ROW_TOP + (ROW_H - 4) / 2, 470);
		drawTextMid(n == 1 ? "1 series" : std::to_string(n) + " series", 0, DIM_ON, 608, MG_ROW_TOP + (ROW_H - 4) / 2, 160, 1);
		for (int i = 0; i < 2; i++)
		{
			int y = MG_ROW_TOP + (i + 1) * ROW_H;
			drawTextMid(SOON[i], 1, DIM, 32, y + (ROW_H - 4) / 2, 576);
		}
		drawHints({{"A", "Open"}, {"B", "Back"}});
	}

	void Ui::drawMangaSeries(const Player& p)
	{
		const MangaSeries* ms = mangaStore ? mangaStore->series(mangaSeriesId) : nullptr;
		const LocalSeries* s = mangaLocalSeries(mangaSeriesId);
		std::string title = ms && !ms->title.empty() ? ms->title : s ? s->title : "Series";
		drawHeader(title, p);
		if (!s)
		{
			drawEmpty(G_BOOK, "Not on this card", "The card was taken out, or the series moved.", nullptr);
			drawHints({{"B", "Back"}});
			return;
		}
		int n = (int)s->chapters.size();
		std::vector<std::string> ids;
		ids.reserve(n);
		for (auto& c : s->chapters) ids.push_back(c.path);
		int unread = mangaStore ? mangaStore->unread(mangaSeriesId, ids) : 0;
		drawTextMid(std::to_string(n) + (n == 1 ? " chapter" : " chapters") + " \xC2\xB7 " + std::to_string(unread) + " unread",
			0, DIM, 16, 68);
		// Resume: the first started-but-unread chapter, else the first unread, else the first
		std::vector<MangaChapterMark> marks;
		marks.reserve(n);
		for (auto& c : s->chapters)
		{
			MangaChapterMark m;
			if (mangaStore)
				if (const ChapterProgress* pr = mangaStore->progress(c.path))
				{
					m.read = pr->read;
					m.started = pr->pages > 0 || pr->page > 0;
					m.page = pr->page;
				}
			marks.push_back(m);
		}
		MangaResumePoint resume = mangaResume(marks);
		Rect rb = mangaResumeRect();
		bool onResume = mserCursor < 0;
		drawBox(rb, mangaPressed == MG_RESUME ? PRESSED : onResume ? BUTTON : SURFACE, R_BUTTON, onResume);
		drawButton(rb, &G_PLAY, n ? "Resume" : "No chapters", nullptr, n == 0, 1);	// (A: in the hints)
		Rect hb = mangaHeartRect();
		bool inLib = ms && ms->inLibrary;
		drawBox(hb, mangaPressed == MG_HEART ? PRESSED : BUTTON, R_BUTTON);
		drawGlyphMid(inLib ? G_HEART : G_HEART_O, hb.x + hb.w / 2, hb.y + 16, inLib ? ACCENT : DIM, 2);
		drawTextMid("Y", 0, ACCENT, hb.x + hb.w / 2, hb.y + 36, 0, 2);
		if (n == 0) { drawHints({{"B", "Back"}}); return; }
		if (mserCursor >= n) mserCursor = n - 1;
		for (int i = mserScroll; i < n && i < mserScroll + MG_SER_ROWS; i++)
		{
			const LocalChapter& c = s->chapters[i];
			int y = MG_SER_TOP + (i - mserScroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
			bool at = mserCursor == i;
			if (at) drawFocus({16, y, 608, ROW_H - 4});
			// the one Resume would open: a small ACCENT dot
			if (resume.chapter == i) fillRound({28, mid - 4, 8, 8}, ACCENT, 4);
			const ChapterProgress* pr = mangaStore ? mangaStore->progress(c.path) : nullptr;
			bool read = pr && pr->read;
			drawTextMid(c.title, 1, read ? DIM : at ? ACCENT : TEXT, 48, mid, 470);
			if (read) drawGlyphMid(G_CHECK, 596, mid, DIM);
			else if (pr && (pr->pages > 0 || pr->page > 0))
				drawTextMid(chapterPageLine(pr->page, pr->pages), 0, at ? DIM_ON : DIM, 608, mid, 150, 1);
		}
		drawScrollBar(MG_SER_TOP, MG_SER_ROWS * ROW_H - 4, mserScroll, MG_SER_ROWS, n);
		drawHints({{"A", "Read"}, {"X", "Read / unread"}, {"Y", "In library"}, {"B", "Back"}});
	}

	void Ui::drawMangaRead(const Player& p)
	{
		// main closed the reader (an error, or it stopped): back to the series page. Not before it has opened the
		// request (open false then isn't a close)
		if (readerStatus.open) mangaReadLive = true;
		else if (mangaReadLive)
		{
			mangaRecordProgress();
			mangaSave();
			page = Page::MangaSeries;
			mangaReadLive = false;
			mangaProgPage = mangaProgPages = -1;
			drawMangaSeries(p);
			return;
		}
		drawBackIcon();
		drawTextMid(mangaReadChapterTitle, 1, TEXT, 72, 24, 540);
		drawTextMid(mangaTitle(mangaReadSeriesId), 0, DIM, 72, 50, 540);
		// the big surface: it shows the pages' place and takes the finger's drag
		drawBox(mangaAreaRect(), SURFACE, R_TILE);
		static const char* MODES[MANGA_MODES] = {"Right to left", "Left to right", "Strip"};
		int m = readerStatus.mode >= 0 && readerStatus.mode < MANGA_MODES ? readerStatus.mode : 0;
		drawTextMid(m == MANGA_STRIP ? "Drag here to scroll" : "Swipe here to turn the page", 0, DIM, W / 2, 190, 0, 2);
		drawTextMid(std::string(MODES[m]) + " \xC2\xB7 X to change", 0, DIM_ON, W / 2, 216, 0, 2);
		int pages = readerStatus.pages;
		if (!readerStatus.error.empty())
			drawTextMid(readerStatus.error, 1, DANGER, W / 2, 340, 560, 2);
		else
		{
			drawTextMid(pages > 0 ? "Page " + std::to_string(readerStatus.page + 1) + " / " + std::to_string(pages) : "Page",
				1, TEXT, W / 2, 340, 380, 2);
			if (readerStatus.loading)
				drawTextMid("Loading" + std::string(SDL_GetTicks() / 400 % 4, '.'), 0, DIM, W / 2 + 150, 342, 0, 2);
		}
		// the slider: the ACCENT fill to the page, its knob; a Drag there is a Seek
		Rect tr = mangaSliderRect(), bar{tr.x, tr.y + 8, tr.w, 10};
		int w = pages > 1 ? (int)std::lround(bar.w * (readerStatus.page / (double)(pages - 1))) : 0;
		drawLevel(bar, w, TRACK, ACCENT);
		fillRound({bar.x + w - 5, bar.y - 4, 10, 18}, KNOB, R_KNOB);
		// its chapters' ends, and where it goes next
		const LocalSeries* s = mangaLocalSeries(mangaReadSeriesId);
		int ci = s ? chapterIndexOf(*s, mangaReadChapterId) : -1;
		bool prev = s && ci > 0, next = s && ci >= 0 && ci + 1 < (int)s->chapters.size();
		if (readerStatus.atEnd)
			drawTextMid(next ? "End of chapter \xC2\xB7 R2 or RIGHT for the next" : "You\xE2\x80\x99re caught up", 0,
				next ? ACCENT : DIM, W / 2, 384, 560, 2);
		Rect pb = mangaPrevRect(), nb = mangaNextRect();
		drawBox(pb, mangaPressed == MG_PREV ? PRESSED : BUTTON, R_BUTTON);
		drawButton(pb, &G_PREV, "Prev chapter", nullptr, !prev, 0);	// (L2/R2: in the hints)
		drawBox(nb, mangaPressed == MG_NEXT ? PRESSED : BUTTON, R_BUTTON);
		drawButton(nb, &G_NEXT, "Next chapter", nullptr, !next, 0);
		drawHints({{"D-PAD", "Step"}, {"L/R", "Page"}, {"X", "Mode"}, {"L2/R2", "Chapter"}, {"B", "Close"}});
	}

	int Ui::mangaModeFor(const std::string& seriesId, const Player& p) const
	{
		const MangaSeries* ms = mangaStore ? mangaStore->series(seriesId) : nullptr;
		if (ms && ms->readingMode == "rtl") return MANGA_RTL;
		if (ms && ms->readingMode == "ltr") return MANGA_LTR;
		if (ms && ms->readingMode == "strip") return MANGA_STRIP;
		int m = p.settings.mangaMode;
		return m >= 0 && m < MANGA_MODES ? m : MANGA_RTL;
	}

	void Ui::mangaCycleMode(Player& p)
	{
		// right to left, left to right, strip, round; kept for this series (the setting stays for the others)
		(void)p;
		int m = (readerStatus.mode + 1) % MANGA_MODES;
		readerCmds.push_back({ReaderCmd::Mode, (double)m, (int64_t)SDL_GetTicks()});
		if (mangaStore)
			if (const MangaSeries* ms = mangaStore->series(mangaReadSeriesId))
			{
				MangaSeries s = *ms;
				static const char* IDS[MANGA_MODES] = {"rtl", "ltr", "strip"};
				s.readingMode = IDS[m];
				mangaStore->putSeries(s);
				mangaSave();
			}
	}
}
