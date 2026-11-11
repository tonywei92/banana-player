#include "ui.h"
#include "manganet.h"
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

		// the photo viewer's steps (FEATURES.md, "Looking at a photo"): 1.0 is the fit, and the reader draws its pages
		// from the same geometry, so Up/Down and the Reading menu's Zoom row walk the same ladder
		constexpr int MANGA_ZOOMS[] = {100, 150, 200, 300, 400, 600, 800};
		constexpr int MANGA_ZOOM_STEPS = (int)(sizeof MANGA_ZOOMS / sizeof MANGA_ZOOMS[0]);

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
		// the network side's threads and stores, the first time the app is used with the reader on
		if (mangaNet && p.settings.mangaOn) mangaNet->start();
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
		mangaCtrlUntil = mangaToastUntil = 0;
		mangaTap = false;
		mangaLastTapAt = 0;
		mangaCtrlPressed = -1;
		mangaMenu = false;
		mangaMenuCursor = 0;
		mangaPages = false;
		mangaZoomAsked = 100;
		mangaScan();
		// the History kept off while the app wasn't open is cleared here too: its list is empty either way
		mangaHistOn = p.settings.mangaHistory;
		if (mangaStore && !mangaHistOn && !mangaStore->history(1).empty())
		{
			mangaStore->clearHistory();
			mangaSave();
		}
		// the page cache: its cap from the setting, its oldest pages going over a smaller one at once (nothing puts a
		// page in it yet: it is the network sources' to fill)
		if (!stateDirPath.empty())
			mangaPageCache.open(stateDirPath + "/manga/cache", mangaCacheBytes(p.settings.mangaCacheSize));
		// a new open wants its own covers: what waited for the last one is dropped, and a cover that failed (a file
		// fixed meanwhile) is tried once more. The ones that arrived stay: a cache of textures is worth keeping
		if (coverWorker) coverWorker->cancel();
		coverAsked.clear();
		coverFailed.clear();
		// a source's page and its jobs don't outlive the app being opened (the worker answers are dropped)
		mangaBrowseReset(p);
		// the Downloads sheet's page lists wait for the worker again; a queued chapter is the queue's, not this
		mangaDownloadsReset();
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
		// a source series out of the library has no store entry: the one its page was opened with is its name
		if (isSourceId(id) && id == mangaSeriesId && !mangaSrcTitle.empty()) return mangaSrcTitle;
		const LocalSeries* l = mangaLocalSeries(id);
		return l ? l->title : "";
	}

	int Ui::mangaReadChapterIndex() const
	{
		// a saved server's chapters are the series page's own list
		if (isServerId(mangaReadSeriesId)) return mangaServerChapterIndex();
		if (isSourceId(mangaReadSeriesId))
		{
			// the source's chapters are the list the chapter was opened with: their ids are "src:<source>:<key>"
			std::string full;
			std::string key;
			splitSourceId(mangaReadSeriesId, full, key);
			for (size_t i = 0; i < mangaReadSrcChapters.size(); i++)
				if (sourceItemId(full, mangaReadSrcChapters[i].key) == mangaReadChapterId) return (int)i;
			return -1;
		}
		const LocalSeries* s = mangaLocalSeries(mangaReadSeriesId);
		return s ? chapterIndexOf(*s, mangaReadChapterId) : -1;
	}

	int Ui::mangaReadChapterTotal() const
	{
		if (isServerId(mangaReadSeriesId)) return mangaSrvSeriesId == mangaReadSeriesId ? (int)mangaSrvChapters.size() : 0;
		if (isSourceId(mangaReadSeriesId)) return (int)mangaReadSrcChapters.size();
		const LocalSeries* s = mangaLocalSeries(mangaReadSeriesId);
		return s ? (int)s->chapters.size() : 0;
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
		// with the History off the place is still kept: only the list stays empty, this entry hidden along with the rest
		if (!mangaHistOn) mangaStore->clearHistory();
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
		// a next chapter asked for (R2) and not waited for goes with the reader: its answer would open it again
		mangaSourceCancel();
		mangaRecordProgress();
		mangaSave();
		readerCmds.push_back({ReaderCmd::Close, 0, (int64_t)SDL_GetTicks()});
		page = Page::MangaSeries;
		mangaMenu = false;
		mangaPages = false;
		mpFree();
		mangaZoomAsked = 100;
		mangaReadLive = false;
		mangaProgPage = mangaProgPages = -1;
	}

	void Ui::openMangaSeries(const std::string& id, int from, Player& p)
	{
		// a series from a source: its page is this one, generalised (the details and chapters are the source's)
		if (isSourceId(id))
		{
			mangaOpenSourceSeries(id, from, "", "", "", p);
			return;
		}
		mangaSeriesId = id;
		mangaSeriesFrom = from;
		mserCursor = -1;
		mserScroll = 0;
		page = Page::MangaSeries;
	}

	void Ui::mangaOpenChapter(const LocalSeries& s, int i, int page0, Player& p)
	{
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
		mangaRead = {mangaSeriesId, s.title, c.path, c.title, c.path, c.archive, std::max(0, page0), mangaModeFor(mangaSeriesId, p),
			p.settings.mangaFit};
		// the status still holds the chapter before: its numbers aren't this chapter's, so only a change records
		mangaProgPage = readerStatus.page;
		mangaProgPages = readerStatus.pages;
		mangaReadLive = false;	// (main hasn't opened it yet: its open false isn't a close)
		mangaSliderPage = -1;
		mangaCtrlUntil = 0;
		mangaMenu = false;
		mangaZoomAsked = 100;	// a chapter opens at the fit (the reader resets its zoom too)
		page = Page::MangaRead;
		// Open fullscreen: the command SELECT sends, right after the chapter (main applies both in one pass)
		if (p.settings.mangaOpenFull) mangaFullscreen(true, p);
	}

	void Ui::mangaChapterStep(int dir, Player& p)
	{
		// a saved server's chapter steps through its series page's list
		if (isServerId(mangaReadSeriesId))
		{
			mangaServerChapterStep(dir, p);
			return;
		}
		// a source's chapter steps through the list it was opened with (its series page holds the same ones)
		if (isSourceId(mangaReadSeriesId))
		{
			mangaSourceChapterStep(dir, p);
			return;
		}
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
				// a server's series (not in the library, so not here) and a source's open their series page
				if (isServerId(cp.seriesId)) { mangaServerSeriesOpenFromId(cp.seriesId, p); return; }
				// a source's chapter is opened from its series page (Resume goes on at the page left)
				if (isSourceId(cp.seriesId)) { openMangaSeries(cp.seriesId, 0, p); return; }
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
			// the history keeps a server's series too: its series page, fetched from the server again
			if (isServerId(cp.seriesId)) { mangaServerSeriesOpenFromId(cp.seriesId, p); return; }
			// the history keeps a source series too: its page, which resumes at the page left
			if (isSourceId(cp.seriesId)) { openMangaSeries(cp.seriesId, 2, p); return; }
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
			mangaBrowseChoose(i, p);
		}
	}

	// ---- the keys ----

	bool Ui::mangaKey(Button b, Player& p)
	{
		// the worker's answers first: a source's list, a series' details, a chapter's pages (this page's or the
		// reading page's next one)
		mangaBrowsePoll(p);
		// the first fullscreen's flag, if a source's chapter opened with Open fullscreen on: this is where a Player
		// can be changed (the draw that opened it has only a const one)
		if (mangaFullSeenPending)
		{
			mangaFullSeenPending = false;
			if (!p.settings.mangaFullSeen)
			{
				Settings s = p.settings;
				s.mangaFullSeen = true;
				p.setSettings(s);
			}
		}
		// the reader: its own controls (the mini bar isn't on this page)
		if (page == Page::MangaRead)
		{
			Uint32 now = SDL_GetTicks();
			// the Reading menu takes the keys while it shows; B and START close it, reading going on behind
			if (mangaMenu) return mangaMenuKey(b, p);
			// the pages grid (Y): the D-pad moves the cursor, A jumps and closes, B or Y closes
			if (mangaPages) return mangaPagesKey(b, p);
			// RIGHT is forward unless Swap Left/Right is on (to match a right-to-left book); UP/DOWN zoom, and
			// scrolling is the finger's alone (a drag on the page below)
			bool flip = p.settings.mangaSwapLR;
			bool fwd = b == (flip ? Button::Left : Button::Right), back = b == (flip ? Button::Right : Button::Left);
			if (b == Button::Up) mangaZoomStep(1);
			else if (b == Button::Down) mangaZoomStep(-1);
			else if (back) readerCmds.push_back({ReaderCmd::Step, -1, (int64_t)now});
			else if (fwd)
			{
				// forward at the end leaves for the next chapter; before it, the reader steps on
				if (readerStatus.atEnd) mangaChapterStep(1, p);
				else readerCmds.push_back({ReaderCmd::Step, 1, (int64_t)now});
			}
			else switch (b)
			{
			case Button::L: readerCmds.push_back({ReaderCmd::Page, -1, (int64_t)now}); break;
			case Button::R: readerCmds.push_back({ReaderCmd::Page, 1, (int64_t)now}); break;
			case Button::A: mangaZoomToggle(); break;
			case Button::X: mangaCycleMode(p); break;
			case Button::L2: mangaChapterStep(-1, p); break;
			case Button::R2: mangaChapterStep(1, p); break;
			// SELECT: fullscreen on / off; B leaves fullscreen first, and only then closes the reader
			case Button::Select: mangaFullscreen(!readerStatus.fullscreen, p); break;
			// START: the Reading menu over the page (over the fullscreen view's lower part), not the settings page
			case Button::Start: mangaOpenReadingMenu(); break;
			case Button::Y: mangaPagesOpen(p); break;
			// L3: turn the page 90° clockwise (paged modes only; the Strip ignores it, as the design says).
			// each press cycles 0 → 90 → 180 → 270 → 0, kept per chapter, back to 0 on another chapter
			case Button::L3: readerCmds.push_back({ReaderCmd::Rotate, 0, (int64_t)now}); break;
			case Button::B:
				if (readerStatus.fullscreen) mangaFullscreen(false, p);
				else mangaCloseReader();
				break;
			default: break;
			}
			return false;
		}
		// the Sources tab's sheet (a source's details or options, a repository's Refresh / Remove) takes the keys first
		if (page == Page::Manga && mangaTab == MT_SOURCES && mangaSourcesModal()) { mangaSourcesKey(b, p); return false; }
		// (the mini bar shown on the app's other pages says SELECT goes to what plays)
		if (b == Button::Select && nowPlayable(p)) { goNowPlaying(p); return false; }
		if (page == Page::MangaSeries)
		{
			// a server's series page: its own keys (A a chapter, B the catalogue it came from)
			if (isServerId(mangaSeriesId)) return mangaServerSeriesKey(b, p);
			// a source's series page: its own keys (Y the library, A a chapter, B back to the source)
			if (mangaSourceSeriesOn()) return mangaSourceSeriesKey(b, p);
			const LocalSeries* s = mangaLocalSeries(mangaSeriesId);
			int n = s ? (int)s->chapters.size() : 0;
			// the app's settings (it comes back to this series)
			if (b == Button::Start) { openMangaSettings(); return false; }
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
		// the app's settings (any tab; B comes back to it, and to this tab)
		if (b == Button::Start) { openMangaSettings(); return false; }
		// a source's page has its own keys: its tabs, the grid, Search, and B back to Browse's rows
		if (mangaView == 1) return mangaSourceKey(b, p);
		// a saved server's pages (ui_manga_servers.cpp): its catalogue, its form, and B back
		if (mangaView >= 2) return mangaServersKey(b, p);
		if (b == Button::B)
		{
			// Browse's "On this handheld": one step back to its rows first; a card series' chapters: its series
			if (mangaTab == MT_BROWSE && mBrowseList) mBrowseList = false;
			else if (mangaTab == MT_DOWNLOADS && mangaDownloadsBack()) { }
			else openExtensionsDrawer(p);
			return false;
		}
		if (b == Button::L || b == Button::R)
		{
			mangaTab = std::clamp(mangaTab + (b == Button::R ? 1 : -1), 0, (int)MANGA_TABS - 1);
			mangaClearAt = 0;
			mBrowseList = false;
			mangaPressed = -1;
			if (mangaTab == MT_SOURCES) mangaSourcesOpen(p);
			if (mangaTab == MT_DOWNLOADS) mangaDownloadsOpen(p);
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
			// off: the list is empty by design (the History tab says so), so nothing there is chosen or cleared
			if (!p.settings.mangaHistory) break;
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
			// On this handheld's series, or a source's page: the rows and the grid (ui_manga_browse.cpp)
			mangaBrowseKey(b, p);
			break;
		case MT_SOURCES:
			mangaSourcesKey(b, p);
			break;
		case MT_DOWNLOADS:
			mangaDownloadsKey(b, p);
			break;
		}
		return false;
	}

	// ---- a finger ----

	bool Ui::mangaPointer(const Pointer& pt, int x, int y, Player& p)
	{
		// a source's answers land here too (a tap may be what asks for the next page)
		mangaBrowsePoll(p);
		if (page == Page::MangaRead)
		{
			// the pages grid takes the finger while it shows: a tap on a thumbnail jumps, a drag scrolls
			if (mangaPages) return mangaPagesPointer(pt, x, y, p);
			// the Reading menu, when it shows, takes the finger: a tap on a row acts as A (the page underneath
			// doesn't drag until it is closed)
			if (mangaMenu)
			{
				if (pt.kind == Pointer::Down && mangaMenuRect().hit(x, y))
				{
					int r = (y - MGM_TOP) / MGM_ROW_H;
					if (r >= 0 && r < MGM_ROWS && y < MGM_TOP + MGM_ROWS * MGM_ROW_H)
					{
						mangaMenuCursor = r;
						mangaMenuAct(p);
					}
				}
				return false;
			}
			// the drag area and the slider follow the finger; the buttons act as it lifts
			Uint32 now = SDL_GetTicks();
			bool full = readerStatus.fullscreen;
			// the slider's page for the finger's x (the reading page's and, in fullscreen, the sheet's)
			auto seekHere = [&]() {
				if (readerStatus.pages > 1)
				{
					Rect tr = mangaSliderRect();
					mangaSeek((int)std::lround(std::clamp((x - tr.x) / (double)tr.w, 0.0, 1.0) * (readerStatus.pages - 1)));
				}
			};
			if (pt.kind == Pointer::Down)
			{
				mangaPressed = -1;
				mangaTouch = mangaSlider = false;
				mangaSliderPage = -1;
				mangaTap = false;
				mangaLastTapAt = 0;
				mangaCtrlPressed = -1;
				// fullscreen's controls up: the sheet takes the finger (any touch there shows it 4 s more), and the
				// pages outside it only wait to become a drag or, lifted, a tap
				if (full && mangaCtrlUntil && now < mangaCtrlUntil && mangaSheetRect().hit(x, y))
				{
					mangaCtrlUntil = now + MG_CTRL_MS;
					if (mangaSliderRect().hit(x, y)) { mangaSlider = true; seekHere(); }
					else
						for (int i = 0; i < 4; i++)
							if (mangaCtrlBtnRect(i).hit(x, y)) mangaCtrlPressed = i;
					return false;
				}
				if (buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
				// fullscreen: the whole panel is the page, the finger drags it anywhere
				if (mangaAreaRect().hit(x, y) || full)
				{
					mangaTouch = true;
					mangaTouchY = pt.y;
					mangaTouchX = pt.x;
					// down and up, close by and soon, is a tap: a second one is a double-tap (zoom at the finger);
					// fullscreen's controls show on a single tap too
					mangaTap = true;
					mangaTapX = x;
					mangaTapY = y;
					mangaTapAt = now;
					readerCmds.push_back({ReaderCmd::TouchDown, 0, (int64_t)now});
					return false;
				}
				if (mangaSliderRect().hit(x, y))
				{
					mangaSlider = true;
					seekHere();
					return false;
				}
				int ci = mangaReadChapterIndex();
				if (ci > 0 && mangaPrevRect().hit(x, y)) mangaPressed = MG_PREV;
				else if (ci >= 0 && ci + 1 < mangaReadChapterTotal() && mangaNextRect().hit(x, y)) mangaPressed = MG_NEXT;
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
					// fullscreen: a finger within 12 units may still be a tap, and a tap doesn't scroll; past them it
					// is a drag, and the controls over the pages go at once
					if (mangaTap && (std::abs(x - mangaTapX) > MG_TAP_UNITS || std::abs(y - mangaTapY) > MG_TAP_UNITS))
					{
						mangaTap = false;
						mangaCtrlUntil = 0;
					}
					// (both: the strip goes by dy, the paged modes by dx)
					if (!mangaTap && (dy != 0 || dx != 0)) readerCmds.push_back({ReaderCmd::Drag, dy, (int64_t)now, dx});
				}
				else if (mangaSlider)
				{
					if (full) mangaCtrlUntil = now + MG_CTRL_MS;
					seekHere();
				}
				return false;
			}
			if (mangaCtrlPressed >= 0)
			{
				// the sheet's button: it acts as the key on it does (B leaves fullscreen first, as always)
				int i = mangaCtrlPressed;
				mangaCtrlPressed = -1;
				if (mangaCtrlBtnRect(i).hit(x, y))
				{
					mangaCtrlUntil = now + MG_CTRL_MS;
					static const Button KEYS[4] = {Button::L2, Button::R2, Button::Select, Button::B};
					mangaKey(KEYS[i], p);
				}
				return false;
			}
			if (mangaTouch)
			{
				mangaTouch = false;
				readerCmds.push_back({ReaderCmd::Release, 0, (int64_t)now});
				// a tap (no drag, soon enough and close by): a second one soon is a double-tap, which zooms to 200 %
				// at the tapped point / back to the fit (as A does, but at the finger); alone, fullscreen's controls show
				if (mangaTap && now - mangaTapAt <= MG_TAP_MS)
				{
					if (now - mangaLastTapAt <= 300 && std::abs(mangaTapX - mangaLastTapX) <= 20 &&
						std::abs(mangaTapY - mangaLastTapY) <= 20)
					{
						double z = mangaZoomAsked >= 200 ? 1.0 : 2.0;
						readerCmds.push_back({ReaderCmd::ZoomPoint, z, (int64_t)now,
							mangaTapX / (double)Ui::W, mangaTapY / (double)Ui::H});
						mangaLastTapAt = 0;
					}
					else
					{
						mangaLastTapAt = now;
						mangaLastTapX = mangaTapX;
						mangaLastTapY = mangaTapY;
					}
					if (full) mangaCtrlUntil = now + MG_CTRL_MS;
				}
				mangaTap = false;
				return false;
			}
			if (mangaSlider)
			{
				mangaSlider = false;
				seekHere();
				if (full) mangaCtrlUntil = now + MG_CTRL_MS;
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
			// a sheet over the tab takes the finger (a tap on a tab under it chooses nothing)
			if (mangaTab == MT_SOURCES && mangaSourcesModal()) return mangaSourcesPointer(pt, x, y, p);
			// a source's page: its tabs, its grid and Search (ui_manga_browse.cpp)
			if (mangaView == 1) return mangaSourcePointer(pt, x, y, p);
			// a saved server's catalogue or form (ui_manga_servers.cpp)
			if (mangaView >= 2) return mangaServersPointer(pt, x, y, p);
			for (int i = 0; i < MANGA_TABS; i++)
				if (mangaTabRect(i).hit(x, y))
				{
					mangaTab = i;
					mangaClearAt = 0;
					mBrowseList = false;
					if (i == MT_SOURCES) mangaSourcesOpen(p);
					if (i == MT_DOWNLOADS) mangaDownloadsOpen(p);
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
			else if (mangaTab == MT_SOURCES) return mangaSourcesPointer(pt, x, y, p);
			else if (mangaTab == MT_DOWNLOADS) return mangaDownloadsPointer(pt, x, y, p);
				return mangaBrowsePointer(pt, x, y, p);
			return false;
		}
		// Page::MangaSeries, a source's: Resume, the heart, a chapter (ui_manga_browse.cpp)
		if (isServerId(mangaSeriesId)) return mangaServerSeriesPointer(pt, x, y, p);
		if (mangaSourceSeriesOn()) return mangaSourceSeriesPointer(pt, x, y, p);
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
		// a source's answers, then its page (the app's own header and tabs aren't it: the source has its own)
		mangaBrowsePoll(p);
		if (mangaView == 1)
		{
			drawMangaSource(p);
			return;
		}
		// a saved server's catalogue or its form (its series page is Page::MangaSeries)
		if (mangaView >= 2)
		{
			drawMangaServers(p);
			return;
		}
		drawHeader("Manga", p);
		drawMangaTabs();
		switch (mangaTab)
		{
		case MT_LIBRARY: drawMangaLibrary(p); break;
		case MT_HISTORY: drawMangaHistory(p); break;
		case MT_BROWSE: drawMangaBrowse(p); break;
		case MT_SOURCES: drawMangaSources(p); break;
		case MT_DOWNLOADS: drawMangaDownloads(p); break;
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
		mangaCache();
		if (mangaClearAt && SDL_GetTicks() - mangaClearAt >= QUIT_WINDOW_MS) mangaClearAt = 0;
		if (!p.settings.mangaHistory)
		{
			// kept off: where it is turned on, and that the reading itself is still remembered
			drawEmpty(G_CLOCK, "Reading history is off",
				"Turn it on in Manga settings (START). Your places and read\nmarks are kept either way.", nullptr);
			drawHints({{"B", "Back"}});
			return;
		}
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
		// On this handheld, then the installed sources (and, still to come, the network's own)
		drawMangaBrowseRows(p);
	}

	void Ui::drawMangaSeries(const Player& p)
	{
		// a source's series page, then its details and chapters (ui_manga_browse.cpp)
		mangaBrowsePoll(p);
		// a saved server's series page (ui_manga_servers.cpp)
		if (isServerId(mangaSeriesId))
		{
			drawMangaServerSeries(p);
			return;
		}
		if (mangaSourceSeriesOn())
		{
			drawMangaSourceSeries(p);
			return;
		}
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
		// a source's next chapter, while this one reads on the top panel: its pages arrive here
		mangaBrowsePoll(p);
		// main closed the reader (an error, or it stopped): back to the series page. Not before it has opened the
		// request (open false then isn't a close)
		if (readerStatus.open) mangaReadLive = true;
		else if (mangaReadLive)
		{
			mangaRecordProgress();
			mangaSave();
			page = Page::MangaSeries;
			mangaMenu = false;
			mangaZoomAsked = 100;
			mangaReadLive = false;
			mangaProgPage = mangaProgPages = -1;
			drawMangaSeries(p);
			return;
		}
		if (readerStatus.fullscreen && readerBottom.on)
		{
			drawMangaFull();
			if (mangaMenu) drawMangaReadingMenu(p);	// over the view's lower part, as any sheet there
			return;
		}
		// the pages grid over the reader: this panel shows thumbnails, the top panel shows the page under the cursor
		if (mangaPages)
		{
			// the reader closed itself (an error, or main stopped it): close the grid and go to the series page
			if (!readerStatus.open && mangaReadLive)
			{
				mangaRecordProgress();
				mangaSave();
				mangaPages = false;
				mpFree();
				page = Page::MangaSeries;
				mangaReadLive = false;
				mangaProgPage = mangaProgPages = -1;
				drawMangaSeries(p);
				return;
			}
			drawMangaPages(p);
			return;
		}
		// out of fullscreen: this panel's copies of the pages, its controls and the first open's toast go
		mangaCtrlUntil = mangaToastUntil = 0;
		if (!mfTex.empty()) mfFree();
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
		int ci = mangaReadChapterIndex();
		bool prev = ci > 0, next = ci >= 0 && ci + 1 < mangaReadChapterTotal();
		if (readerStatus.atEnd)
			drawTextMid(next ? std::string("End of chapter \xC2\xB7 R2 or ") + (p.settings.mangaSwapLR ? "LEFT" : "RIGHT") + " for the next"
				: "You\xE2\x80\x99re caught up", 0, next ? ACCENT : DIM, W / 2, 384, 560, 2);
		Rect pb = mangaPrevRect(), nb = mangaNextRect();
		drawBox(pb, mangaPressed == MG_PREV ? PRESSED : BUTTON, R_BUTTON);
		drawButton(pb, &G_PREV, "Prev chapter", nullptr, !prev, 0);	// (L2/R2: in the hints)
		drawBox(nb, mangaPressed == MG_NEXT ? PRESSED : BUTTON, R_BUTTON);
		drawButton(nb, &G_NEXT, "Next chapter", nullptr, !next, 0);
		drawHints({{"UP/DOWN", "Zoom"}, {"LEFT/RIGHT", "Step"}, {"L/R", "Page"}, {"START", "Menu"}, {"SELECT", "Fullscreen"}, {"B", "Close"}});
		if (mangaMenu) drawMangaReadingMenu(p);
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
		// right to left, left to right, strip, round
		mangaSetMode((readerStatus.mode + 1) % MANGA_MODES, p);
	}

	void Ui::mangaSetMode(int m, Player& p)
	{
		// kept for this series (the setting stays for the others)
		(void)p;
		if (m < 0 || m >= MANGA_MODES || m == readerStatus.mode) return;
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

	void Ui::mangaZoomTo(double z)
	{
		mangaZoomAsked = (int)(z * 100 + 0.5);
		readerCmds.push_back({ReaderCmd::Zoom, z, (int64_t)SDL_GetTicks()});
	}

	void Ui::mangaZoomStep(int dir)
	{
		// the nearest step at or above what the keys last asked (the reader's own status lags a pass behind);
		// one further up or down from there, ends held
		int pct = mangaZoomAsked > 0 ? mangaZoomAsked : 100;
		int i = 0;
		while (i < MANGA_ZOOM_STEPS - 1 && MANGA_ZOOMS[i] < pct) i++;
		i = std::clamp(i + (dir > 0 ? 1 : -1), 0, MANGA_ZOOM_STEPS - 1);
		mangaZoomTo(MANGA_ZOOMS[i] / 100.0);
	}

	void Ui::mangaZoomToggle()
	{
		// as the photo viewer's A: twice the fit, or back to it from any zoom
		mangaZoomTo(mangaZoomAsked >= 200 ? 1.0 : 2.0);
	}

	void Ui::mangaOpenReadingMenu()
	{
		// over the page it is reading: nothing behind it moves, so closing it reads on exactly where it was
		mangaMenu = true;
		mangaMenuCursor = 0;
	}

	bool Ui::mangaMenuKey(Button b, Player& p)
	{
		switch (b)
		{
		case Button::Up: mangaMenuCursor = (mangaMenuCursor + MGM_ROWS - 1) % MGM_ROWS; break;
		case Button::Down: mangaMenuCursor = (mangaMenuCursor + 1) % MGM_ROWS; break;
		case Button::Left: mangaMenuStep(-1, p); break;
		case Button::Right: mangaMenuStep(1, p); break;
		case Button::A: mangaMenuAct(p); break;
		case Button::B:
		case Button::Start: mangaMenu = false; break;
		default: break;
		}
		return false;
	}

	void Ui::mangaMenuStep(int dir, Player& p)
	{
		switch (mangaMenuCursor)
		{
		// Fullscreen: the same as SELECT (both arrows toggle, as a settings switch does)
		case 0: mangaFullscreen(!readerStatus.fullscreen, p); break;
		case 1: mangaSetMode((readerStatus.mode + dir + MANGA_MODES) % MANGA_MODES, p); break;
		// the setting itself, so the next chapter opens the same way
		case 2: changeSetting(MANGA_FIT_ROW, dir, p); break;
		case 3: mangaZoomStep(dir); break;
		// the hinge only means something with both panels as one view
		case 4: if (readerStatus.fullscreen) changeSetting(MANGA_HINGE_ROW, dir, p); break;
		case 5: changeSetting(TOGGLE_ROW + 13, dir, p); break;	// Swap Left/Right
		// Pages: nothing to step
		case 6: break;
		default: break;
		}
	}

	void Ui::mangaMenuAct(Player& p)
	{
		switch (mangaMenuCursor)
		{
		case 0: mangaFullscreen(!readerStatus.fullscreen, p); break;
		case 1: mangaSetMode((readerStatus.mode + 1) % MANGA_MODES, p); break;
		case 2: changeSetting(MANGA_FIT_ROW, 1, p); break;
		case 3: mangaZoomToggle(); break;
		case 4: if (readerStatus.fullscreen) changeSetting(MANGA_HINGE_ROW, 1, p); break;
		case 5: changeSetting(TOGGLE_ROW + 13, 1, p); break;
		case 6:
			// the pages grid; the menu closes, Y or B in the grid comes back to reading
			mangaMenu = false;
			mangaPagesOpen(p);
			break;
		case 7:
			// the app's own settings page; B there comes back here, the menu closed
			mangaMenu = false;
			openMangaSettings();
			break;
		default: break;
		}
	}

	void Ui::drawMangaReadingMenu(const Player& p)
	{
		// a compact sheet over the page: its title and how to leave it, then a row per thing read with
		Rect sheet = mangaMenuRect();
		fillRound(sheet, SURFACE, R_DIALOG, ROUND_TL | ROUND_TR);
		drawTextMid("Reading", 1, TEXT, 24, 150, 300);
		drawTextMid("START \xC2\xB7 B Close", 0, DIM, W - 24, 150, 300, 1);
		static const char* MODES[MANGA_MODES] = {"Right to left", "Left to right", "Strip"};
		static const char* FITS[MANGA_FITS] = {"Page", "Width", "Fill"};
		int mode = readerStatus.mode >= 0 && readerStatus.mode < MANGA_MODES ? readerStatus.mode : 0;
		int fit = p.settings.mangaFit >= 0 && p.settings.mangaFit < MANGA_FITS ? p.settings.mangaFit : 0;
		int zoom = readerStatus.zoomPct > 0 ? readerStatus.zoomPct : 100;
		const char* LABELS[MGM_ROWS] = {"Fullscreen", "Reading mode", "Page fit", "Zoom", "Hinge gap",
			"Swap Left/Right", "Pages", "All Manga settings\xE2\x80\xA6"};
		for (int i = 0; i < MGM_ROWS; i++)
		{
			Rect r = mangaMenuRowRect(i);
			bool at = i == mangaMenuCursor;
			// the hinge is a fullscreen thing: dimmed wherever the pages fill one panel only
			bool dim = i == 4 && !readerStatus.fullscreen;
			if (at) drawFocus(r);
			int mid = r.y + r.h / 2;
			SDL_Color ink = at ? ACCENT : TEXT;
			drawTextMid(LABELS[i], 1, dim ? DIM : ink, 32, mid, 330);
			// the last two rows are doors (Pages, All Manga settings), not values: a chevron, no step arrows
			if (i == 6 || i == 7)
			{
				drawGlyphMid(G_RIGHT, 604, mid, at ? ACCENT : DIM);
				continue;
			}
			bool step = !dim;
			if (step) drawTextMid("<", 1, at ? ACCENT : DIM, SET_ARROW_X, mid);
			std::string value;
			switch (i)
			{
			case 0: value = readerStatus.fullscreen ? "On" : "Off"; break;
			case 1: value = MODES[mode]; break;
			case 2: value = FITS[fit]; break;
			case 3: value = std::to_string(zoom) + " %"; break;
			case 4: value = readerStatus.fullscreen ? std::to_string(p.settings.mangaHinge) + " px" : "Fullscreen only"; break;
			case 5: value = p.settings.mangaSwapLR ? "On" : "Off"; break;
			default: break;
			}
			if (!value.empty()) drawTextMid(value, 1, dim ? DIM : ink, 490, mid, 196, 2);
			if (step) drawTextMid(">", 1, at ? ACCENT : DIM, 608, mid, 0, 1);
		}
		// what the focused row does, in a line under them
		static const char* HELP[MGM_ROWS] = {
			"Both panels show the pages; SELECT does the same.",
			"How this series reads; X does the same.",
			"Page keeps it whole, Width fills the screen's width.",
			"UP/DOWN step the zoom; A goes twice or back to the fit.",
			"The gap between the panels, in fullscreen.",
			"Off: RIGHT is forward. On: LEFT is forward.",
			"The chapter's thumbnails; A jumps to a page.",
			"The reader's own settings page; B comes back here."};
		drawTextMid(HELP[mangaMenuCursor], 0, DIM, 24, 452, 592);
	}

	void Ui::mangaFullscreen(bool on, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		readerCmds.push_back({ReaderCmd::Fullscreen, on ? 1.0 : 0.0, (int64_t)now, (double)p.settings.mangaHinge});
		mfTagUntil = on ? now + 2500 : 0;
		if (!on) mangaCtrlUntil = 0;
		// the first fullscreen ever says how to leave it, here in the panel's middle; remembered so it says it once
		if (on && !p.settings.mangaFullSeen)
		{
			Settings s = p.settings;
			s.mangaFullSeen = true;
			p.setSettings(s);
			mangaToastUntil = now + MG_TOAST_MS;
		}
	}

	void Ui::mfFree()
	{
		for (auto& t : mfTex)
			for (SDL_Texture* x : t.second.tiles) SDL_DestroyTexture(x);
		mfTex.clear();
		mfWaiting = 0;
	}

	void Ui::drawMangaFull()
	{
		// the pages under the hinge: main gives each page's box (fractions of the panel) and its pixels; they become
		// this renderer's textures, one page a frame at most so a turn never stalls the panel
		Uint32 now = SDL_GetTicks();
		if (mangaCtrlUntil && now >= mangaCtrlUntil) mangaCtrlUntil = 0;	// the timer's end: the controls go
		if (mangaToastUntil && now >= mangaToastUntil) mangaToastUntil = 0;
		if (readerBottom.gen != mfGen) { mfFree(); mfGen = readerBottom.gen; }
		// a new page: its number shows a moment
		if (readerStatus.page != mfLastPage) { mfLastPage = readerStatus.page; mfTagUntil = now + 1500; }
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);
		int pw = (int)std::lround(W * sx), ph = (int)std::lround(H * sy);
		bool made = false;
		int waiting = 0;
		for (auto& b : readerBottom.boxes)
		{
			SDL_Rect r{(int)std::lround(b.x * pw), (int)std::lround(b.y * ph), (int)std::lround(b.w * pw), (int)std::lround(b.h * ph)};
			auto it = mfTex.find(b.page);
			if (it == mfTex.end() && !made)
			{
				for (auto& px : readerBottom.pages)
				{
					if (px.page != b.page || !px.rgba || px.w <= 0 || px.h <= 0) continue;
					FullTex ft;
					ft.w = px.w;
					ft.h = px.h;
					for (int y = 0; y < px.h; y += 2048)
					{
						int th = std::min(2048, px.h - y);
						SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, px.w, th);
						if (!t) break;
						SDL_UpdateTexture(t, nullptr, px.rgba->data() + (size_t)y * px.w * 4, px.w * 4);
						ft.tiles.push_back(t);
					}
					it = mfTex.emplace(b.page, std::move(ft)).first;
					made = true;
					break;
				}
			}
			if (it == mfTex.end())
			{
				// not decoded yet, or its turn to be made comes next frame: its place, dark
				SDL_SetRenderDrawColor(ren, 40, 40, 44, 255);
				SDL_RenderFillRect(ren, &r);
				waiting++;
				continue;
			}
			FullTex& ft = it->second;
			ft.used = now;
			for (size_t k = 0; k < ft.tiles.size(); k++)
			{
				int y0 = (int)k * 2048, th = std::min(2048, ft.h - y0);
				SDL_Rect d{r.x, r.y + (int)std::lround((double)r.h * y0 / ft.h), r.w, (int)std::lround((double)r.h * th / ft.h) + 1};
				if (d.y + d.h < 0 || d.y > ph) continue;
				SDL_RenderCopy(ren, ft.tiles[k], nullptr, &d);
			}
		}
		mfWaiting = waiting;
		// at most 8 pages kept here: the ones not drawn longest go
		while (mfTex.size() > 8)
		{
			auto old = mfTex.begin();
			for (auto i = mfTex.begin(); i != mfTex.end(); ++i)
				if (i->second.used < old->second.used) old = i;
			for (SDL_Texture* x : old->second.tiles) SDL_DestroyTexture(x);
			mfTex.erase(old);
		}
		// a thin page tag for a moment after a change
		if (now < mfTagUntil && readerStatus.pages > 0)
		{
			std::string tag = std::to_string(readerStatus.page + 1) + " / " + std::to_string(readerStatus.pages);
			int tw = (int)std::lround(textWidth(tag, 0) / sx) + 16;
			fillRound({W - tw - 12, H - 36, tw, 26}, SURFACE, R_CHIP);
			drawTextMid(tag, 0, TEXT, W - 12 - tw / 2, H - 23, 0, 2);
		}
		// the controls, while they show (over the tag), then the first open's toast over both
		if (now < mangaCtrlUntil) drawMangaControls();
		if (now < mangaToastUntil) drawMangaToast();
	}

	void Ui::drawMangaControls()
	{
		// a sheet at the panel's foot: the series and its page, the page slider, the four keys (its top corners round)
		Rect sheet = mangaSheetRect();
		fillRound(sheet, SURFACE, R_DIALOG, ROUND_TL | ROUND_TR);
		drawTextMid(mangaTitle(mangaReadSeriesId), 0, DIM, 20, 318, 380);
		int pages = readerStatus.pages;
		drawTextMid(pages > 0 ? "Page " + std::to_string(readerStatus.page + 1) + " of " + std::to_string(pages) : "Page", 0, TEXT,
			W - 20, 318, 220, 1);
		// the slider as the reading page draws it: a drag here is a Seek
		Rect tr = mangaSliderRect(), bar{tr.x, tr.y + 8, tr.w, 10};
		int w = pages > 1 ? (int)std::lround(bar.w * (readerStatus.page / (double)(pages - 1))) : 0;
		drawLevel(bar, w, TRACK, ACCENT);
		fillRound({bar.x + w - 5, bar.y - 4, 10, 18}, KNOB, R_KNOB);
		static const char* LABELS[4] = {"< Chapter", "Chapter >", "Leave fullscreen", "Close"};
		static const char* KEYS[4] = {"L2", "R2", "SELECT", "B"};
		for (int i = 0; i < 4; i++)
		{
			Rect b = mangaCtrlBtnRect(i);
			drawBox(b, mangaCtrlPressed == i ? PRESSED : BUTTON, R_BUTTON);
			drawButton(b, nullptr, LABELS[i], KEYS[i], false, 0);
		}
		drawTextMid("Hides in 4 s \xC2\xB7 drag to hide now", 0, DIM, W / 2, 460);
	}

	void Ui::drawMangaToast()
	{
		// how to leave fullscreen, the first time it opens: a box in the panel's middle for 2 s
		const char* text = "Fullscreen \xC2\xB7 SELECT or B to leave";
		int tw = (int)std::lround(textWidth(text, 0) / sx) + 32;
		fillRound({W / 2 - tw / 2, H / 2 - 22, tw, 44}, SURFACE, R_BUTTON);
		drawTextMid(text, 0, TEXT, W / 2, H / 2, 0, 2);
	}

	// ---- the pages grid (Y in the reader) ----

	void Ui::mangaPagesOpen(Player& p)
	{
		(void)p;
		if (!readerStatus.open || readerStatus.pages <= 0) return;
		mangaPages = true;
		mangaPagesCursor = std::clamp(readerStatus.page, 0, readerStatus.pages - 1);
		// the cursor's row centred in the grid
		int row = mangaPagesCursor / MPG_COLS;
		int rows = (readerStatus.pages + MPG_COLS - 1) / MPG_COLS;
		int shown = (H - MPG_TOP - 44) / MPG_ROW_H;	// 44: the hints footer
		mangaPagesScroll = std::clamp(row - shown / 2, 0, std::max(0, rows - shown));
		mangaPagesTouch = false;
		mangaPagesPressed = -1;
		mpMade = 0;
	}

	void Ui::mangaPagesClose(Player& p)
	{
		(void)p;
		mangaPages = false;
		mangaPagesTouch = false;
		mangaPagesPressed = -1;
	}

	void Ui::mpFree()
	{
		for (auto& t : mpTex)
			if (t.second.tex) SDL_DestroyTexture(t.second.tex);
		mpTex.clear();
		mpMade = 0;
	}

	bool Ui::mangaPagesKey(Button b, Player& p)
	{
		int pages = readerStatus.pages;
		if (pages <= 0) { mangaPagesClose(p); return false; }
		int cur = mangaPagesCursor;
		int row = cur / MPG_COLS, col = cur % MPG_COLS;
		int rows = (pages + MPG_COLS - 1) / MPG_COLS;
		switch (b)
		{
		case Button::Left: if (col > 0) cur--; break;
		case Button::Right: if (col + 1 < MPG_COLS && cur + 1 < pages) cur++; break;
		case Button::Up: if (row > 0) cur -= MPG_COLS; break;
		case Button::Down: if (row + 1 < rows) cur = std::min(cur + MPG_COLS, pages - 1); break;
		case Button::A:
			// jump to the page under the cursor and close the grid
			mangaSeek(cur);
			mangaPagesClose(p);
			return false;
		case Button::B:
		case Button::Y:
			mangaPagesClose(p);
			return false;
		default: break;
		}
		if (cur != mangaPagesCursor)
		{
			mangaPagesCursor = cur;
			// the top panel follows the cursor: a Seek, as the slider sends
			mangaSeek(cur);
			// keep the cursor's row in view
			row = cur / MPG_COLS;
			int shown = (H - MPG_TOP - 44) / MPG_ROW_H;
			if (row < mangaPagesScroll) mangaPagesScroll = row;
			if (row >= mangaPagesScroll + shown) mangaPagesScroll = row - shown + 1;
		}
		return false;
	}

	bool Ui::mangaPagesPointer(const Pointer& pt, int x, int y, Player& p)
	{
		int pages = readerStatus.pages;
		if (pages <= 0) { mangaPagesClose(p); return false; }
		if (pt.kind == Pointer::Down)
		{
			mangaPagesTouch = false;
			mangaPagesPressed = -1;
			// the back icon: same as B (close the grid)
			if (buttonRect(BACK_ICON).hit(x, y)) { mangaPagesClose(p); return false; }
			// a tap on a thumbnail: jump to it and close
			for (int i = mangaPagesScroll * MPG_COLS; i < pages; i++)
			{
				Rect r = mangaPageRect(i);
				if (r.y >= H - 44) break;	// below the hints
				if (r.hit(x, y))
				{
					mangaSeek(i);
					mangaPagesClose(p);
					return false;
				}
			}
			// elsewhere on the grid: a drag starts
			if (y >= MPG_TOP && y < H - 44)
			{
				mangaPagesTouch = true;
				mangaPagesTouchY = pt.y;
			}
			return false;
		}
		if (pt.kind == Pointer::Move && mangaPagesTouch)
		{
			// the list follows the finger: dy in 0..1 units, scaled to rows
			double dy = pt.y - mangaPagesTouchY;
			mangaPagesTouchY = pt.y;
			int rows = (pages + MPG_COLS - 1) / MPG_COLS;
			int shown = (H - MPG_TOP - 44) / MPG_ROW_H;
			// one panel height of drag = one screenful of rows; dy is in panel fractions
			int shift = (int)std::lround(-dy * shown);
			if (shift != 0)
			{
				mangaPagesScroll = std::clamp(mangaPagesScroll + shift, 0, std::max(0, rows - shown));
				// the cursor follows if it was left behind
				if (mangaPagesCursor < mangaPagesScroll * MPG_COLS)
					mangaPagesCursor = std::clamp(mangaPagesScroll * MPG_COLS, 0, pages - 1);
				if (mangaPagesCursor >= (mangaPagesScroll + shown) * MPG_COLS)
					mangaPagesCursor = std::clamp((mangaPagesScroll + shown - 1) * MPG_COLS + MPG_COLS - 1, 0, pages - 1);
				mangaSeek(mangaPagesCursor);
			}
			return false;
		}
		if (mangaPagesTouch)
		{
			mangaPagesTouch = false;
			return false;
		}
		return false;
	}

	void Ui::drawMangaPages(const Player& p)
	{
		(void)p;
		int pages = readerStatus.pages;
		if (pages <= 0) { mangaPagesClose(const_cast<Player&>(p)); return; }
		// the chapter changed: the old textures go
		if (readerThumbs.gen != mpGen) { mpFree(); mpGen = readerThumbs.gen; }
		drawBackIcon();
		drawTextMid("Pages", 1, TEXT, 72, 24, 540);
		drawTextMid(std::to_string(mangaPagesCursor + 1) + " / " + std::to_string(pages), 0, DIM, 72, 50, 540);
		// the grid: thumbnails with numbered placeholders for pages not decoded yet. at most a few textures per
		// frame so a turn never stalls the panel (the same pattern as drawMangaFull)
		bool made = false;
		int rows = (pages + MPG_COLS - 1) / MPG_COLS;
		int shown = (H - MPG_TOP - 44) / MPG_ROW_H;
		mangaPagesScroll = std::clamp(mangaPagesScroll, 0, std::max(0, rows - shown));
		SDL_Rect clip{0, (int)std::lround(MPG_TOP * sy), (int)std::lround(W * sx),
			(int)std::lround((H - MPG_TOP - 44) * sy)};
		SDL_RenderSetClipRect(ren, &clip);
		for (int i = mangaPagesScroll * MPG_COLS; i < pages; i++)
		{
			Rect r = mangaPageRect(i);
			if (r.y >= H - 44) break;
			bool at = i == mangaPagesCursor;
			// the focused thumbnail's ACCENT edge
			if (at) fillRound({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, ACCENT, R_TILE);
			drawBox(r, SURFACE, R_TILE - 2);
			// look for the texture, or make one from the RGBA main handed us
			auto it = mpTex.find(i);
			if (it == mpTex.end() && !made)
			{
				for (auto& th : readerThumbs.pages)
				{
					if (th.page != i || !th.rgba || th.w <= 0 || th.h <= 0) continue;
					SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, th.w, th.h);
					if (t)
					{
						SDL_UpdateTexture(t, nullptr, th.rgba->data(), th.w * 4);
						SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
					}
					mpTex[i] = {t, th.w, th.h, SDL_GetTicks()};
					it = mpTex.find(i);
					made = true;	// one per frame
					break;
				}
			}
			if (it != mpTex.end() && it->second.tex)
			{
				// the thumbnail drawn inside the box, aspect kept (centred)
				int bw = r.w - 4, bh = r.h - 24;	// 24: room for the page number below
				int sw = it->second.w, sh = it->second.h;
				int dw = bw, dh = bh;
				if (sw > 0 && sh > 0)
				{
					double aspect = (double)sw / sh;
					if (aspect > (double)bw / bh) { dw = bw; dh = (int)(bw / aspect + 0.5); }
					else { dh = bh; dw = (int)(bh * aspect + 0.5); }
				}
				SDL_Rect dst{(int)std::lround((r.x + 2 + (bw - dw) / 2) * sx), (int)std::lround((r.y + 2 + (bh - dh) / 2) * sy),
					(int)std::lround(dw * sx), (int)std::lround(dh * sy)};
				SDL_RenderCopy(ren, it->second.tex, nullptr, &dst);
				it->second.used = SDL_GetTicks();
			}
			else
			{
				// not decoded yet: a numbered placeholder
				std::string num = std::to_string(i + 1);
				drawTextMid(num, 0, at ? ACCENT : DIM, r.x + r.w / 2, r.y + r.h / 2, 0, 2);
				continue;	// the number is the placeholder: none again underneath
			}
			// the page number under the thumbnail, inside the box's bottom strip (the 24 kept for it above)
			drawTextMid(std::to_string(i + 1), 0, at ? ACCENT : DIM, r.x + r.w / 2, r.y + r.h - 11, 0, 2);
		}
		SDL_RenderSetClipRect(ren, nullptr);
		// the scrollbar
		drawScrollBar(MPG_TOP, shown * MPG_ROW_H - 4, mangaPagesScroll, shown, rows);
		// at most 60 thumbnails kept: the ones not drawn longest go (a 200-page chapter keeps 60 at a time)
		while (mpTex.size() > 60)
		{
			auto old = mpTex.begin();
			for (auto i = mpTex.begin(); i != mpTex.end(); ++i)
				if (i->second.used < old->second.used) old = i;
			if (old->second.tex) SDL_DestroyTexture(old->second.tex);
			mpTex.erase(old);
		}
		drawHints({{"D-PAD", "Move"}, {"A", "Go to page"}, {"B", "Close"}});
	}
}
