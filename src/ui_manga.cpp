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
	// ---- the manga reader: the store under <state>/manga, the card's scan, the four tabs, a series and the reader ----

	namespace
	{
		const char* const MANGA_TAB_NAMES[4] = {"Library", "History", "Browse", "Downloads"};	// Ui::MANGA_TABS

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

		// "Chapter 12" as "Ch. 12", and a bare title gets its "Ch. ": the History row keeps the chapter's line short
		// ("Ch." only before a bare title: "Chapter 1" says it already)
		std::string chapterShort(const std::string& title)
		{
			std::string ch = title;
			if (ch.rfind("Chapter ", 0) == 0) ch = "Ch. " + ch.substr(8);
			bool says = ch.rfind("Ch", 0) == 0 || ch.rfind("ch", 0) == 0;
			return (says ? "" : "Ch. ") + ch;
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
		mlibGrid = mlibScroll = 0;
		mhisCursor = mhisScroll = 0;
		mbrowseCursor = mListCursor = mListScroll = 0;
		mangaClearAt = 0;
		mlocSheet = MLS_NONE;
		mangaPressed = -1;
		mangaTouch = mangaSlider = false;
		mangaSliderPage = -1;
		mangaCtrlUntil = mangaToastUntil = 0;
		mangaTap = false;
		mangaTapDismiss = false;
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
		{
			mangaPageCache.open(stateDirPath + "/manga/cache", mangaCacheBytes(p.settings.mangaCacheSize));
			// the cover cache: 50 MB fixed, the oldest covers going first (docs/design/manga.md section 6)
			mangaCoverCache.open(stateDirPath + "/manga/cache/covers", MANGA_COVER_CACHE_LIMIT);
		}
		// a new open wants its own covers: what waited for the last one is dropped, and a cover that failed (a file
		// fixed meanwhile) is tried once more. The ones that arrived stay: a cache of textures is worth keeping
		if (coverWorker) coverWorker->cancel();
		coverAsked.clear();
		coverFailed.clear();
		coverUrlAsked.clear();
		mangaCoverCancelAll();
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
		mangaScanned = true;
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
			// a folder of downloads isn't in the Library by being on the card: its series is, when the heart says so
			for (const LocalChapter& c : s.chapters)
				if (isSourceId(c.ciSeriesKey)) { m.inLibrary = false; break; }
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
		mangaHistory = mangaStore->historyBySeries(50);
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
		mangaStore->setProgress(cp, !incognito);
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
		mfFree();	// (fullscreen's bottom-panel pages too: closed from fullscreen, nothing else would free them)
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
		// a folder of downloads: the source's series it came from, so every chapter shows (the downloaded ones marked
		// and opened from the card), not only those downloaded
		if (const LocalSeries* s = mangaLocalSeries(id))
			for (const LocalChapter& c : s->chapters)
				if (isSourceId(c.ciSeriesKey))
				{
					// (the folder's name is the series' title until the source's details come)
					mangaOpenSourceSeries(c.ciSeriesKey, from, "", "", s->title, p);
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
			// the cover grid: a series page (the Continue reading cards are gone)
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
			if (isSourceId(cp.seriesId))
			{
				openMangaSeries(cp.seriesId, 2, p);
				// the series page opens on the chapter the History row was (once its list is there)
				mserFocusChapter = cp.chapterId;
				return;
			}
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
		if (page == Page::Manga && mengSheet) return mangaEngineKey(b, p);
		// On this handheld's help or series menu takes every key, B too (B below would leave the list under it,
		// and the next A on On this handheld found the menu still open)
		if (page == Page::Manga && mangaTab == MT_BROWSE && mBrowseList && mangaView == 0 && mlocSheet != MLS_NONE)
			return mangaLocalSheetKey(b, p);
		if (page == Page::Manga && mhisMenu) return mangaHisMenuKey(b, p);
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
		if (page == Page::MangaRead && mangaReadWaiting)
		{
			if (b == Button::B) mangaReadWaitCancel();
			else if (b == Button::A && !mangaSrcLoading && !mangaSrcError.empty()) mangaSourceRetry(p);
			return false;	// (true would be "leave": with a song playing, main went to the file browser)
		}
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
			case Button::A: if (readerStatus.failedHere) mangaRetry(); else mangaZoomToggle(); break;
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
		// (the mini bar shown on the app's other pages says SELECT goes to what plays)
		if (b == Button::Select && nowPlayable(p)) { goNowPlaying(p); return false; }
		if (page == Page::MangaSeries)
		{
			// a network folder read as a series (ui_manga_netfolders.cpp)
			if (mangaNetView == MNF_SERIES) return mangaNetFoldersKey(b, p);
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
			// a network folder's page (or a folder read as a series) steps back inside Browse first
			if (mangaTab == MT_BROWSE && mangaNetView != MNF_OFF) { mangaNetFoldersKey(b, p); return false; }
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
			if (mangaTab == MT_DOWNLOADS) mangaDownloadsOpen(p);
			return false;
		}
		switch (mangaTab)
		{
		case MT_LIBRARY:
		{
			mangaCache();
			// the cover grid only (the Continue reading cards are gone): A opens the series
			int n = (int)mangaLib.size();
			if (mlibGrid >= n) mlibGrid = std::max(0, n - 1);
			if (!n) break;
			if (b == Button::A) { mangaChoose(MT_LIBRARY, mlibGrid, p); break; }
			int row = mlibGrid / MG_COLS, col = mlibGrid % MG_COLS, rows = (n + MG_COLS - 1) / MG_COLS;
			switch (b)
			{
			case Button::Left: if (col > 0) mlibGrid--; break;
			case Button::Right: if (col + 1 < MG_COLS && mlibGrid + 1 < n) mlibGrid++; break;
			// up at the grid's first row does nothing: nothing sits above it any more
			case Button::Up:
				if (row > 0) { mlibGrid -= MG_COLS; mlibScroll = mlibGrid / MG_COLS; }
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
				// what to clear: a menu (the last hour, today, the last 24 hours, all)
				mhisMenu = true;
				mhisMenuCursor = 0;
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
			if (mhisCursor >= mhisScroll + MG_HIST_ROWS) mhisScroll = mhisCursor - MG_HIST_ROWS + 1;
			break;
		}
		case MT_BROWSE:
			// On this handheld's series, or a source's page: the rows and the grid (ui_manga_browse.cpp)
			mangaBrowseKey(b, p);
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
		if (page == Page::MangaRead && mangaReadWaiting)
		{
			if (pt.kind == Pointer::Down && buttonRect(BACK_ICON).hit(x, y)) mangaReadWaitCancel();
			return false;
		}
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
				mangaTapDismiss = false;
				// (mangaLastTapAt is the last tap's: not cleared here, or a double-tap could never see its first)
				mangaCtrlPressed = -1;
				// a page that failed: its Try again, in the middle, before anything under it takes the finger
				if (readerStatus.failedHere && mangaRetryRect(full).hit(x, y))
				{
					mangaRetry();
					return false;
				}
				// fullscreen's controls up: the sheet takes the finger (any touch there shows it 4 s more), and the
				// pages outside it only wait to become a drag or, lifted, a tap that puts the bar away
				if (full && mangaCtrlUntil && now < mangaCtrlUntil)
				{
					if (mangaSheetRect().hit(x, y))
					{
						mangaCtrlUntil = now + MG_CTRL_MS;
						if (mangaSliderRect().hit(x, y)) { mangaSlider = true; seekHere(); }
						else
							for (int i = 0; i < 4; i++)
								if (mangaCtrlBtnRect(i).hit(x, y)) mangaCtrlPressed = i;
						return false;
					}
					// outside the bar: the release decides, once the finger is known not to have dragged (a drag
					// goes on to pan the page and hides the bar as it always did)
					mangaTapDismiss = true;
				}
				// in fullscreen the panel is all page (the "<" isn't drawn): a tap anywhere shows the controls,
				// including where that icon would be
				if (!full && !mangaTapDismiss && buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
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
				else if (mangaFullRect().hit(x, y)) mangaPressed = MG_FULL;
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
						mangaTapDismiss = false;
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
					// the bar was up and this tap was outside it: it goes, and the tap does nothing else
					if (mangaTapDismiss) mangaCtrlUntil = 0;
					else
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
				}
				mangaTap = false;
				mangaTapDismiss = false;
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
				else if (id == MG_FULL && mangaFullRect().hit(x, y)) mangaFullscreen(!readerStatus.fullscreen, p);
			}
			return false;
		}
		// a source's grid follows the finger (a drag scrolls it), so it gets every move, not only the touch
		if (page == Page::Manga && mangaView == 1)
			return mangaSourcePointer(pt, x, y, p);
		if (page == Page::MangaSeries && mangaSourceSeriesOn() && pt.kind != Pointer::Down) return mangaSourceSeriesPointer(pt, x, y, p);
		// the tabs, a card, a cover, a row: a tap acts on the spot, as the browser's tabs do
		if (pt.kind != Pointer::Down) return false;
		mangaPressed = -1;
		if ((page == Page::Manga || page == Page::MangaSources) && mengSheet)
		{
			if (mengBusy)
			{
				if (mangaEngineRow(1).hit(x, y)) mangaEngineChoose(1, p);
				return false;
			}
			for (int i = 0; i < 2; i++)
				if (mangaEngineRow(i).hit(x, y)) { mengCursor = i; mangaEngineChoose(i, p); return false; }
			if (!mangaEngineBox().hit(x, y)) mengSheet = false;
			return false;
		}
		if (page == Page::Manga && mhisMenu)
		{
			if (!mhisMenuBox().hit(x, y)) { mhisMenu = false; return false; }
			for (int i = 0; i < MHIS_MENU_ROWS; i++)
				if (mhisMenuRow(i).hit(x, y)) { mhisMenuCursor = i; mangaHisMenuAct(p); }
			return false;
		}
		if (page == Page::MangaSources) return mangaSourcesPointer(pt, x, y, p);
		if (page == Page::Manga)
		{
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
					if (i == MT_DOWNLOADS) mangaDownloadsOpen(p);
					return false;
				}
			// the lists below follow a finger: a drag scrolls them, a tap (lifted where it went down) opens the row
			if (mangaTab == MT_LIBRARY)
			{
				mangaCache();
				int n = (int)mangaLib.size();
				if (!n || y < MG_LIB_TOP || y >= MG_LIB_BOTTOM) return false;
				int hit = -1;
				for (int i = mlibScroll * MG_COLS; i < n; i++)
				{
					Rect r = mangaCoverRect(i);
					if (r.y >= MG_LIB_BOTTOM) break;
					if (r.hit(x, y)) hit = i;
				}
				// the grid shows a row and the next one's top: the D-pad keeps its cursor on the first row shown,
				// and so does the finger (same column)
				mangaListBegin(x, y, mlibScroll, 186, hit, [this](int want) {
					int n = (int)mangaLib.size(), rows = (n + MG_COLS - 1) / MG_COLS;
					mlibScroll = std::clamp(want, 0, std::max(0, rows - 1));
					if (n) mlibGrid = std::min(n - 1, mlibScroll * MG_COLS + mlibGrid % MG_COLS);
					return mlibScroll;
				}, [this, &p](int i) {
					if (i >= (int)mangaLib.size()) return;
					mlibGrid = i;
					mlibScroll = i / MG_COLS;
					mangaChoose(MT_LIBRARY, i, p);
				});
				return false;
			}
			if (mangaTab == MT_HISTORY)
			{
				mangaCache();
				int n = (int)mangaHistory.size();
				if (!n || !p.settings.mangaHistory || y < MG_ROW_TOP || y >= MG_ROW_TOP + MG_HIST_ROWS * MG_HIST_ROW_H)
					return false;
				int i = mhisScroll + (y - MG_ROW_TOP) / MG_HIST_ROW_H;
				mangaListBegin(x, y, mhisScroll, MG_HIST_ROW_H, i < n ? i : -1, [this](int want) {
					int n = (int)mangaHistory.size();
					mhisScroll = std::clamp(want, 0, std::max(0, n - MG_HIST_ROWS));
					mhisCursor = std::clamp(mhisCursor, mhisScroll, std::max(mhisScroll, std::min(n - 1, mhisScroll + MG_HIST_ROWS - 1)));
					return mhisScroll;
				}, [this, &p](int i) {
					if (i >= (int)mangaHistory.size()) return;
					mhisCursor = i;
					mangaChoose(MT_HISTORY, i, p);
				});
				return false;
			}
			// Browse's rows, On this handheld and a network folder's page (ui_manga_browse.cpp)
			if (mangaTab == MT_BROWSE) return mangaBrowsePointer(pt, x, y, p);
			if (mangaTab == MT_DOWNLOADS) return mangaDownloadsPointer(pt, x, y, p);
			return false;
		}
		// Page::MangaSeries, a source's: Resume, the heart, a chapter (ui_manga_browse.cpp)
		if (mangaNetView == MNF_SERIES) return mangaNetFoldersPointer(pt, x, y, p);
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
			// the chapters follow a finger: a drag scrolls them (the cursor, unless on Resume, stays among them)
			int i = mserScroll + (y - MG_SER_TOP) / ROW_H;
			mangaListBegin(x, y, mserScroll, ROW_H, i < n ? i : -1, [this](int want) {
				const LocalSeries* s = mangaLocalSeries(mangaSeriesId);
				int n = s ? (int)s->chapters.size() : 0;
				mserScroll = std::clamp(want, 0, std::max(0, n - MG_SER_ROWS));
				if (mserCursor >= 0) mserCursor = std::clamp(mserCursor, mserScroll, std::max(mserScroll, std::min(n - 1, mserScroll + MG_SER_ROWS - 1)));
				return mserScroll;
			}, [this, &p](int i) {
				const LocalSeries* s = mangaLocalSeries(mangaSeriesId);
				if (!s || i >= (int)s->chapters.size()) return;
				mserCursor = i;
				mangaSeriesChoose(i, p);
			});
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

	// a cover by its URL: the cover cache holds the bytes (keyed by URL), the worker decodes them to the tile's
	// pixels. A card on screen asks once; a card that scrolls away cancels its fetch. A cover already in the cache
	// is decoded straight from there, with no network
	SDL_Texture* Ui::coverOfUrl(const std::string& coverUrl, int w, int h,
		const std::vector<std::pair<std::string, std::string>>& headers)
	{
		if (coverUrl.empty()) return nullptr;
		// the extensions' covers are on 127.0.0.1 at a port chosen at each start: they are known (and cached) by
		// their path, and fetched at today's port, so a saved series' cover shows after a restart too
		const std::string key = mangaCoverKey(coverUrl);
		// the texture cache is shared with the local covers: a URL key never collides with a series id (those are
		// paths, "src:…" or "srv:…"; a URL starts with http)
		auto it = coverTex.find(key);
		if (it != coverTex.end())
		{
			it->second.drawnAt = ++coverDrawn;
			return it->second.tex;
		}
		if (coverFailed.count(key) || !coverWorker) return nullptr;
		// the cover cache is the disk side: a hit here means no fetch, only a decode
		if (mangaCoverCache.has(key))
		{
			if (coverUrlAsked.count(key)) return nullptr;	// already being decoded
			coverWorker->want(key, mangaCoverCache.pathFor(key), w, h);
			coverUrlAsked.insert(key);
			return nullptr;
		}
		// not cached: a fetch is in flight or needs starting. the draw loop records this URL as on-screen; anything
		// left over at the end of the frame is cancelled (the card scrolled away)
		if (mangaCoverFetches.count(key)) return nullptr;	// fetching already
		if (!mangaNet || !mangaNet->started()) return nullptr;
		std::string fetchUrl = coverUrl;
		if (key.rfind("javahost/", 0) == 0)
		{
			// fetched only from the install its key names: another install's number is another series' now
			size_t s = key.find('/', 9);
			if (s == std::string::npos || key.substr(9, s - 9) != mangaNet->javaExt().installToken()) return nullptr;
			std::string base = mangaNet->javaExt().baseUrl();
			if (base.empty()) return nullptr;	// not running: no fetch (it isn't started for a cover)
			fetchUrl = base + key.substr(s);
		}
		if (coverFetchUrl) fetchUrl = coverFetchUrl(fetchUrl);
		uint64_t id = mangaNet->fetchApi().get(fetchUrl, headers, 4 * 1024 * 1024);
		if (id) mangaCoverFetches[key] = id;
		else coverFailed.insert(key);	// the fetcher stopped: nothing will come
		return nullptr;
	}

	std::string Ui::mangaSeriesCoverUrl(const std::string& seriesId) const
	{
		// a source's or a server's series keeps its cover's URL in the store; a local one's is read from the card
		const MangaSeries* s = mangaStore ? mangaStore->series(seriesId) : nullptr;
		if (!s) return "";
		if (s->cover.rfind("javahost/", 0) == 0) return s->cover;
		if (s->cover.compare(0, 4, "http") != 0) return "";
		// an engine cover saved as its URL (before keys had a token): its install isn't known, so it's never fetched
		// by that number (it may be another series' now); a cached one under the old key still shows
		static const std::string local = "http://127.0.0.1:", engine = "/api/v1/manga/";
		if (s->cover.compare(0, local.size(), local) == 0)
		{
			size_t slash = s->cover.find('/', local.size());
			if (slash != std::string::npos && s->cover.compare(slash, engine.size(), engine) == 0)
				return "javahost/legacy" + s->cover.substr(slash);
		}
		return s->cover;
	}

	std::string Ui::mangaCoverKey(const std::string& url) const
	{
		if (url.rfind("javahost/", 0) == 0) return url;	// a key already (a saved series' cover)
		// only the manga engine's own covers (/api/v1/manga/<id>/thumbnail): a comic server the user runs on the
		// handheld itself is on 127.0.0.1 too, at a port that stays, and its covers are fetched where they are
		static const std::string local = "http://127.0.0.1:", engine = "/api/v1/manga/";
		if (url.compare(0, local.size(), local) != 0) return url;
		size_t slash = url.find('/', local.size());
		if (slash == std::string::npos || url.compare(slash, engine.size(), engine) != 0) return url;
		std::string tok = mangaNet && mangaNet->started() ? mangaNet->javaExt().installToken() : "";
		return "javahost/" + (tok.empty() ? std::string("none") : tok) + url.substr(slash);
	}

	std::string Ui::mangaCoverStored(const std::string& url) const
	{
		std::string k = mangaCoverKey(url);
		return k.rfind("javahost/", 0) == 0 ? k : url;
	}

	void Ui::mangaCoverCancelOffScreen()
	{
		if (!mangaNet || !mangaNet->started()) { mangaCoverFetches.clear(); return; }
		for (auto it = mangaCoverFetches.begin(); it != mangaCoverFetches.end(); )
		{
			if (mangaCoverOnScreen.count(it->first)) { ++it; continue; }
			mangaNet->fetchApi().cancel(it->second);
			it = mangaCoverFetches.erase(it);
		}
	}

	void Ui::mangaCoverCancelAll()
	{
		if (mangaNet && mangaNet->started())
			for (auto& kv : mangaCoverFetches) mangaNet->fetchApi().cancel(kv.second);
		mangaCoverFetches.clear();
		mangaCoverOnScreen.clear();
		coverUrlAsked.clear();
	}

	// one fetch answer: if its id matches a cover we asked for, put its bytes in the cover cache and hand the
	// cache file to the worker for decode. Returns true if it was one of ours (the caller skips its own handling)
	bool Ui::mangaCoverOnFetched(MangaFetch::Result& r)
	{
		if (mangaCoverFetches.empty()) return false;
		std::string url;
		for (auto& kv : mangaCoverFetches)
			if (kv.second == r.id) { url = kv.first; break; }
		if (url.empty()) return false;
		mangaCoverFetches.erase(url);
		bool ok = r.error.empty() && r.status >= 200 && r.status < 300 && !r.body.empty();
		if (ok && mangaCoverCache.put(url, r.body.data(), r.body.size()))
		{
			if (coverWorker && !coverUrlAsked.count(url))
			{
				int pw = std::max(1, (int)std::lround(MG_COVER_W * sx));
				int ph = std::max(1, (int)std::lround(MG_COVER_H * sy));
				coverWorker->want(url, mangaCoverCache.pathFor(url), pw, ph);
				coverUrlAsked.insert(url);
			}
		}
		else
		{
			coverFailed.insert(url);
			// in the log: why a cover stayed generated (the address without its query: a server's key can sit there)
			fprintf(stderr, "manga: cover failed (%s, status %d, %zu bytes) %s\n", r.error.empty() ? "answered" : r.error.c_str(),
				r.status, r.body.size(), url.substr(0, url.find('?')).c_str());
		}
		return true;
	}

	void Ui::coversReady(std::vector<CoverWorker::Result> results)
	{
		for (auto& r : results)
		{
			coverAsked.erase(r.key);
			coverUrlAsked.erase(r.key);	// a URL cover the worker just decoded: the next draw can ask again if scrolled back
			SDL_Texture* t = nullptr;
			if (r.ok)
			{
				roundCover(r.rgba, r.w, r.h);
				t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, r.w, r.h);
				if (t)
				{
					SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);	// shrunk to its tile: smooth, not jagged
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

	void Ui::drawMangaCover(Rect r, const std::string& title, bool focus, int bookCell, const std::string& seriesId,
		const std::string& coverUrl, const std::vector<std::pair<std::string, std::string>>* coverHeaders)
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
		// a source's or a server's cover: fetched through the cover cache and decoded by the worker as a local one
		// is. the generated cover stays until the picture arrives (and for any that fail)
		if (!coverUrl.empty())
		{
			mangaCoverOnScreen.insert(mangaCoverKey(coverUrl));
			// (always the grid's size: one texture per cover, drawn smaller in History and the cards)
			int pw = std::max(1, (int)std::lround(MG_COVER_W * sx));
			int ph = std::max(1, (int)std::lround(MG_COVER_H * sy));
			const std::vector<std::pair<std::string, std::string>> empty;
			SDL_Texture* tex = coverOfUrl(coverUrl, pw, ph, coverHeaders ? *coverHeaders : empty);
			if (tex)
			{
				SDL_Rect d = devRect(in);
				SDL_RenderCopy(ren, tex, nullptr, &d);
				return;
			}
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
		case MT_DOWNLOADS: drawMangaDownloads(p); break;
		}
		if (mengSheet) drawMangaEngineSheet();
	}

	// ---- entering without the manga engine: Download / Not now ----

	bool Ui::mangaEngineKey(Button b, Player& p)
	{
		if (mengBusy)
		{
			// downloading: nothing closes it but Cancel (A on it, or B)
			if (b == Button::A || b == Button::B) mangaEngineChoose(1, p);
			return false;
		}
		switch (b)
		{
		case Button::Up: case Button::Down: mengCursor = 1 - mengCursor; break;
		case Button::A: mangaEngineChoose(mengCursor, p); break;
		case Button::B: mengSheet = false; break;
		default: break;
		}
		return false;
	}

	void Ui::mangaEngineChoose(int i, Player& p)
	{
		if (!mangaNet) { mengSheet = mengBusy = false; return; }
		JavaExtHost& h = mangaNet->javaExt();
		if (mengBusy)
		{
			// Cancel: the download stops (what came so far is kept, a later Download goes on from there)
			if (mengUpgrade) h.cancelUpgrade();
			else if (h.state() == JavaExtHost::State::Downloading) h.cancelInstall();
			fprintf(stderr, "manga: the engine's download cancelled from its sheet\n");
			mengSheet = mengBusy = false;
			p.osd("Download cancelled", 1500);
			return;
		}
		if (i != 0) { mengSheet = false; return; }
		if (mengUpgrade)
		{
			h.upgradeRuntime();
			fprintf(stderr, "manga: the lighter runtime asked for on entering\n");
			mengBusy = true;
			mengCursor = 1;
			return;
		}
		auto st = h.state();
		if (st == JavaExtHost::State::NotInstalled || st == JavaExtHost::State::Failed) h.install();
		fprintf(stderr, "manga: the engine's download asked for on entering\n");
		// the sheet stays: its progress, then it closes by itself when the engine is on the card
		mengBusy = true;
		mengCursor = 1;
	}

	bool Ui::mangaEngineMissing() const
	{
		if (!mangaNet || !mangaNet->started()) return false;
		const JavaExtHost& h = mangaNet->javaExt();
		auto st = h.state();
		return st == JavaExtHost::State::NotInstalled || (st == JavaExtHost::State::Failed && h.installedBytes() == 0);
	}

	void Ui::mangaEngineOffer()
	{
		mengSheet = true;
		mengBusy = false;
		mengCursor = 0;
	}

	std::string Ui::mangaEngineSignature() const
	{
		if (!mengBusy || !mangaNet) return "";
		// the download's progress, a step per MB, and its state (done, failed)
		int64_t d = 0, t = 0;
		mangaNet->javaExt().progress(d, t);
		return "," + std::to_string((int)mangaNet->javaExt().state()) + "," + std::to_string((int)mangaNet->javaExt().step()) +
			"," + std::to_string((int)mangaNet->javaExt().upgrade()) +
			"," + std::to_string(d >> 20);
	}

	void Ui::mangaEngineWatch(Player& p)
	{
		if (!mengBusy || !mangaNet) return;
		JavaExtHost& h = mangaNet->javaExt();
		if (mengUpgrade)
		{
			auto up = h.upgrade();
			if (up == JavaExtHost::Upgrade::Downloading) return;
			if (up == JavaExtHost::Upgrade::Failed)
			{
				mengBusy = false;	// back to the offer, the reason under it
				mengCursor = 0;
				return;
			}
			// Ready (in place at the next start), or None after it was already put in place (the engine wasn't up)
			mengSheet = mengBusy = mengUpgrade = false;
			p.osd(up == JavaExtHost::Upgrade::Ready ? "Lighter engine ready: used from its next start" : "Lighter engine ready", 2500);
			return;
		}
		auto st = h.state();
		if (st == JavaExtHost::State::Downloading) return;
		if (st == JavaExtHost::State::Failed)
		{
			// the sheet goes back to its offer, the reason under it (Download tries again)
			mengBusy = false;
			mengCursor = 0;
			return;
		}
		if (st == JavaExtHost::State::NotInstalled) return;	// (the install's thread hasn't said Downloading yet)
		// Installed (or already starting): done
		mengSheet = mengBusy = false;
		p.osd("Manga engine ready", 2000);
		h.want();	// on its way up for the sources
	}

	void Ui::drawMangaEngineSheet()
	{
		Rect b = mangaEngineBox();
		if (mengBusy && mangaNet)
		{
			JavaExtHost& h = mangaNet->javaExt();
			int64_t done = 0, total = 0;
			h.progress(done, total);
			if (total <= 0) total = mengUpgrade ? 58000000 : 241000000;
			drawDialog(b, mengUpgrade ? "Downloading the lighter engine" : "Downloading the manga engine");
			double f = std::clamp((double)done / (double)total, 0.0, 1.0);
			// what it's doing: the runtime, then (an install) the server; between them the runtime is unpacked, no
			// bytes coming, so a ring says it's working where the bar would stand still
			auto step = h.step();
			if (step == JavaExtHost::Step::Unpacking)
			{
				drawLoader(b.x + b.w / 2, b.y + 118, "Unpacking the runtime\xE2\x80\xA6");
			}
			else
			{
				char line[96];
				snprintf(line, sizeof line, "%s  \xC2\xB7  %lld of %lld MB",
					step == JavaExtHost::Step::Server ? "Downloading the server" : "Downloading the runtime",
					(long long)(done / 1000000), (long long)(total / 1000000));	// (MB as the offer counts them: 241, 58)
				drawTextMid(line, 1, TEXT, b.x + b.w / 2, b.y + 100, b.w - 64, 2);
				drawLevel({b.x + 32, b.y + 132, b.w - 64, 8}, (int)std::lround((b.w - 64) * f), TRACK, ACCENT);
			}
			drawTextMid("Keep Banana Player open until it's done.", 0, DIM, b.x + b.w / 2, b.y + 172, b.w - 64, 2);
			drawDialogRow(mangaEngineRow(1), &G_CROSS, "Cancel", true, false, DANGER, "", {0, 0, 0, 0});
			drawHints({{"A", "Cancel"}});
			return;
		}
		drawDialog(b, mengUpgrade ? "A lighter manga engine" : "Get the manga engine?");
		static const char* TEXT_ = "Most online sources need it: a one-time download, kept on the card. It runs only while "
			"you use them.";
		static const char* UP_ = "It uses about 70 MB less memory, so it keeps running while you read. A one-time "
			"download; the engine you have works meanwhile.";
		auto lines = wrapText(mengUpgrade ? UP_ : TEXT_, 0, b.w - 64, 3);
		for (size_t i = 0; i < lines.size(); i++)
			drawTextMid(lines[i], 0, TEXT, b.x + 32, b.y + 84 + (int)i * 26, b.w - 64);
		// a download that failed says why, in place of the note
		std::string err = !mangaNet ? "" : mengUpgrade
			? (mangaNet->javaExt().upgrade() == JavaExtHost::Upgrade::Failed ? mangaNet->javaExt().upgradeError() : "")
			: (mangaNet->javaExt().state() == JavaExtHost::State::Failed ? mangaNet->javaExt().error() : "");
		if (!err.empty())
			drawTextMid(err, 0, DANGER, b.x + 32, b.y + 84 + (int)lines.size() * 26 + 8, b.w - 64);
		else if (!mengUpgrade)
			drawTextMid("Manga on your card works without it.", 0, DIM, b.x + 32, b.y + 84 + (int)lines.size() * 26 + 8, b.w - 64);
		drawDialogRow(mangaEngineRow(0), &G_DOWNLOAD, "Download", mengCursor == 0, false, TEXT, mengUpgrade ? "58 MB" : "241 MB", DIM);
		drawDialogRow(mangaEngineRow(1), nullptr, "Not now", mengCursor == 1, false, TEXT, "", {0, 0, 0, 0});
		drawHints({{"A", "Choose"}, {"B", "Not now"}});
	}

	void Ui::drawMangaLibrary(const Player& p)
	{
		(void)p;
		mangaCache();
		// the label on the line under the tabs, then the cover grid right below it (no Continue reading strip)
		drawTextMid("Library \xC2\xB7 " + std::to_string(mangaLib.size()) + " series", 0, DIM, 16, MG_LIB_TOP - 14);
		int n = (int)mangaLib.size();
		if (n == 0)
		{
			drawEmpty(G_BOOK, "Your library is empty",
				"Put CBZ files or folders of pictures in a Manga folder on your card, or add series from Browse.", nullptr);
			drawHints({{"B", "Back"}});
			return;
		}
		int rows = (n + MG_COLS - 1) / MG_COLS;
		if (mlibGrid >= n) mlibGrid = n - 1;
		if (mlibScroll >= rows) mlibScroll = rows - 1;
		// the grid, a row at a time: the next row peeks under this one, cut at the mini bar
		SDL_Rect clip{0, (int)std::lround(MG_LIB_TOP * sy), (int)std::lround(W * sx),
			(int)std::lround((MG_LIB_BOTTOM - MG_LIB_TOP) * sy)};
		SDL_RenderSetClipRect(ren, &clip);
		for (int i = mlibScroll * MG_COLS; i < n; i++)
		{
			Rect r = mangaCoverRect(i);
			if (r.y >= MG_LIB_BOTTOM) break;
			bool at = mlibGrid == i;
			drawMangaCover(r, mangaLib[i].title, at, 4, mangaLib[i].id, mangaSeriesCoverUrl(mangaLib[i].id));
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
		drawScrollBar(MG_LIB_TOP, MG_LIB_BOTTOM - MG_LIB_TOP, mlibScroll,
			std::max(1, (MG_LIB_BOTTOM - MG_LIB_TOP) / 186), rows);
		drawHints({{"D-PAD", "Move"}, {"A", "Open"}, {"B", "Back"}});
	}

	bool Ui::mangaHisMenuKey(Button b, Player& p)
	{
		switch (b)
		{
		case Button::Up: mhisMenuCursor = (mhisMenuCursor + MHIS_MENU_ROWS - 1) % MHIS_MENU_ROWS; break;
		case Button::Down: mhisMenuCursor = (mhisMenuCursor + 1) % MHIS_MENU_ROWS; break;
		case Button::A: mangaHisMenuAct(p); break;
		case Button::B: case Button::X: mhisMenu = false; break;
		default: break;
		}
		return false;
	}

	void Ui::mangaHisMenuAct(Player& p)
	{
		mhisMenu = false;
		if (!mangaStore) return;
		int64_t now = (int64_t)time(nullptr);
		static const char* SAID[5] = {"Removed from History", "Cleared the last hour", "Cleared today", "Cleared the last 24 hours", "History cleared"};
		if (mhisMenuCursor == 0)
		{
			// the series the cursor was on
			if (mhisCursor >= 0 && mhisCursor < (int)mangaHistory.size()) mangaStore->clearHistorySeries(mangaHistory[mhisCursor].seriesId);
		}
		else if (mhisMenuCursor == 4) mangaStore->clearHistory();
		else
		{
			int64_t since = now - 3600;
			if (mhisMenuCursor == 3) since = now - 24 * 3600;
			else if (mhisMenuCursor == 2)
			{
				// today: from this morning's midnight, the handheld's own clock
				time_t t = (time_t)now;
				struct tm lt;
				localtime_r(&t, &lt);
				lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
				since = (int64_t)mktime(&lt);
			}
			mangaStore->clearHistorySince(since);
		}
		mangaSave();
		if (mhisMenuCursor != 0) mhisCursor = mhisScroll = 0;	// (one series gone: the cursor stays where it was)
		mangaNotice(SAID[std::clamp(mhisMenuCursor, 0, 4)], 1500);
	}

	void Ui::drawMangaHisMenu()
	{
		Rect b = mhisMenuBox();
		drawDialog(b, "Clear history");
		static const char* ROWS[MHIS_MENU_ROWS] = {"Selected item", "Last hour", "Today", "Last 24 hours", "All"};
		for (int i = 0; i < MHIS_MENU_ROWS; i++)
		{
			Rect r = mhisMenuRow(i);
			bool at = i == mhisMenuCursor;
			if (at) drawFocus(r);
			drawTextMid(ROWS[i], 1, at ? ACCENT : i == MHIS_MENU_ROWS - 1 ? DANGER : TEXT, r.x + 12, r.y + r.h / 2, r.w - 24);
		}
		drawHints({{"A", "Clear"}, {"B", "Close"}});
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
			drawEmpty(G_CLOCK, "Nothing read yet", "Series you read show up here.", nullptr);
			drawHints({{"B", "Back"}});
			return;
		}
		if (mhisCursor >= n) mhisCursor = n - 1;
		for (int i = mhisScroll; i < n && i < mhisScroll + MG_HIST_ROWS; i++)
		{
			const ChapterProgress& cp = mangaHistory[i];
			int y = MG_ROW_TOP + (i - mhisScroll) * MG_HIST_ROW_H, mid = y + (MG_HIST_ROW_H - 4) / 2;
			bool at = i == mhisCursor;
			if (at) drawFocus({16, y, 608, MG_HIST_ROW_H - 4});
			SDL_Color ink = at ? ACCENT : TEXT, soft = at ? DIM_ON : DIM;
			// one row a series: its cover, its title, and the chapter it was last at
			std::string title = mangaTitle(cp.seriesId);
			drawMangaCover({28, mid - 23, 32, 46}, title, false, 1, cp.seriesId, mangaSeriesCoverUrl(cp.seriesId));
			drawTextMid(title, 1, ink, 72, mid - 10, 422);
			{
				std::string from = mangaSourceLabel(cp.seriesId);
				drawTextMid((from.empty() ? "" : from + " \xC2\xB7 ") + chapterShort(cp.chapterTitle) + " \xC2\xB7 " + chapterPageLine(cp.page, cp.pages),
					0, soft, 72, mid + 10, 422);
			}
			// how far into that chapter, a thin bar under the text (ACCENT always: it is the place read)
			double frac = cp.pages > 0 ? std::clamp((cp.page + 1) / (double)cp.pages, 0.0, 1.0) : 0;
			drawLevel({72, y + MG_HIST_ROW_H - 13, 422, 3}, (int)std::lround(422 * frac), TRACK, ACCENT);
			drawTextMid(mangaAgo(cp.at, (int64_t)time(nullptr)), 0, soft, 604, mid, 120, 1);
		}
		drawScrollBar(MG_ROW_TOP, MG_HIST_ROWS * MG_HIST_ROW_H - 4, mhisScroll, MG_HIST_ROWS, n);
		if (mangaClearAt) drawFooterText("Press X again to clear history", ACCENT);
		else drawHints({{"A", "Continue"}, {"X", "Clear\xE2\x80\xA6"}, {"B", "Back"}});
		if (mhisMenu) drawMangaHisMenu();
	}

	void Ui::drawMangaBrowse(const Player& p)
	{
		mangaCache();
		int n = (int)mangaLocal.size();
		if (mBrowseList)
		{
			if (n == 0)
			{
				drawEmpty(G_FOLDER, "No series found", "Copy manga into " + mangaLocalFolder() + ". Y shows how.", nullptr);
				drawHints({{"Y", "How to add"}, {"B", "Back"}});
				if (mlocSheet == MLS_HELP) drawMangaLocalSheet(p);
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
			drawHints({{"A", "Open"}, {"X", "Options"}, {"Y", "How to add"}, {"B", "Back"}});
			if (mlocSheet != MLS_NONE) drawMangaLocalSheet(p);
			return;
		}
		// On this handheld, then the installed sources (and, still to come, the network's own)
		drawMangaBrowseRows(p);
	}

	void Ui::drawMangaSeries(const Player& p)
	{
		// a network folder read as a series (ui_manga_netfolders.cpp): its own header, chapters and hints
		if (mangaNetView == MNF_SERIES)
		{
			drawMangaNetFolders(p);
			return;
		}
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
		drawGlyphMid(inLib ? G_HEART : G_HEART_O, hb.x + hb.w / 2, hb.y + 14, inLib ? ACCENT : DIM, 2);
		drawTextMid("Y", 0, ACCENT, hb.x + hb.w / 2, hb.y + 35, 0, 2);
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
		mangaErrCheck(readerStatus.error, false, 1);	// a page behind a browser check: the same dialog
		if (mangaReadWaiting && page == Page::MangaRead)
		{
			drawMangaReadWaiting();
			return;
		}
		if (page != Page::MangaRead) { drawMangaSeries(p); return; }
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
		if (readerStatus.failedHere) drawMangaRetry(false);
		else
		{
			drawTextMid(m == MANGA_STRIP ? "Drag here to scroll" : "Swipe here to turn the page", 0, DIM, W / 2, 190, 0, 2);
			drawTextMid(std::string(MODES[m]) + " \xC2\xB7 X to change", 0, DIM_ON, W / 2, 216, 0, 2);
		}
		int pages = readerStatus.pages;
		if (!readerStatus.error.empty())
			drawTextMid(readerStatus.error, 1, DANGER, W / 2, miniUp ? 310 : 340, 560, 2);
		else
		{
			drawTextMid(pages > 0 ? "Page " + std::to_string(readerStatus.page + 1) + " / " + std::to_string(pages) : "Page",
				1, TEXT, W / 2, miniUp ? 310 : 340, 380, 2);
			if (readerStatus.loading)
				drawTextMid("Loading" + std::string(SDL_GetTicks() / 400 % 4, '.'), 0, DIM, W / 2 + 150, miniUp ? 312 : 342, 0, 2);
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
				: "You\xE2\x80\x99re caught up", 0, next ? ACCENT : DIM, W / 2, miniUp ? 360 : 384, 560, 2);
		Rect pb = mangaPrevRect(), fb = mangaFullRect(), nb = mangaNextRect();
		drawBox(pb, mangaPressed == MG_PREV ? PRESSED : BUTTON, R_BUTTON);
		drawButton(pb, &G_PREV, "Prev chapter", nullptr, !prev, 0);	// (L2/R2: in the hints)
		// between them, as big as they are: fullscreen, exactly as SELECT turns it on
		drawBox(fb, mangaPressed == MG_FULL ? PRESSED : BUTTON, R_BUTTON);
		drawButton(fb, nullptr, "Fullscreen", nullptr, false, 0);
		drawBox(nb, mangaPressed == MG_NEXT ? PRESSED : BUTTON, R_BUTTON);
		drawButton(nb, &G_NEXT, "Next chapter", nullptr, !next, 0);
		// what the keys do: the useful ones, one line (the Reading menu, START, lists the rest); the fullscreen
		// button is on the row above, so SELECT needn't be named
		drawHints({{"UP/DOWN", "Zoom"}, {"L/R", "Page"}, {"START", "Menu"}, {"B", "Close"}});
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

	void Ui::drawMangaRetry(bool full)
	{
		Rect r = mangaRetryRect(full);
		fillRound(r, ACCENT, R_TILE);
		drawTextMid("Try again", 1, ON_ACCENT, r.x + r.w / 2, r.y + r.h / 2, r.w - 16, 2);
		drawTextMid("A page didn\xE2\x80\x99t load \xC2\xB7 A", 0, full ? TEXT : DIM, W / 2, r.y + r.h + 18, 0, 2);
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
						// filtered like the top panel's pages (GL_LINEAR): SDL's default, nearest, drops whole rows
						// and columns when a page is shrunk to the panel, and its lines and letters go jagged (#6)
						SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
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
		while (mfTex.size() > 2)	// (each a page's textures on the GPU, a long page's tens of MB: the memory is the system's)
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
		if (readerStatus.failedHere) drawMangaRetry(true);
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
		drawTextMid("Hides in 4 s \xC2\xB7 drag to hide now", 0, DIM, W / 2, miniUp ? 468 : 460);
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
		int rows = (pages + MPG_COLS - 1) / MPG_COLS;
		int shown = std::max(1, (H - MPG_TOP - 44) / MPG_ROW_H);
		// the thumbnail under a point, -1 for none
		auto thumbAt = [&](int px, int py) {
			for (int i = mangaPagesScroll * MPG_COLS; i < pages; i++)
			{
				Rect r = mangaPageRect(i);
				if (r.y >= H - 44) break;	// below the hints
				if (r.hit(px, py)) return i;
			}
			return -1;
		};
		if (pt.kind == Pointer::Down)
		{
			mangaPagesTouch = false;
			mangaPagesMoved = false;
			mangaPagesPressed = -1;
			// the back icon: same as B (close the grid)
			if (buttonRect(BACK_ICON).hit(x, y)) { mangaPagesClose(p); return false; }
			// anywhere on the grid, a thumbnail too: a drag or a tap, told apart when the finger lifts (a thumbnail
			// opening on the touch left nothing to start a drag from: the grid is all thumbnails)
			if (y >= MPG_TOP && y < H - 44)
			{
				mangaPagesTouch = true;
				mangaPagesTouchY = mangaPagesTouchStartY = pt.y;
				mangaPagesPressed = thumbAt(x, y);
			}
			return false;
		}
		if (pt.kind == Pointer::Move && mangaPagesTouch)
		{
			// past a small slop it's a drag (pt.y is in panel heights: 0.03 is about 14 px)
			if (!mangaPagesMoved && std::fabs(pt.y - mangaPagesTouchStartY) > 0.03)
			{
				mangaPagesMoved = true;
				mangaPagesPressed = -1;
			}
			if (!mangaPagesMoved) return false;
			// the grid follows the finger, a row per row's height of drag (pt.y is in panel heights). What's under a
			// whole row stays owed (the finger's place moves on only by the rows taken), so a slow drag scrolls too;
			// the small epsilon keeps a drag of exactly one row from rounding down to none
			const double rowH = (double)MPG_ROW_H / H;
			double owed = -(pt.y - mangaPagesTouchY) / rowH;
			int shift = (int)(owed + (owed > 0 ? 1e-6 : -1e-6));
			if (shift != 0)
			{
				int was = mangaPagesScroll;
				mangaPagesScroll = std::clamp(mangaPagesScroll + shift, 0, std::max(0, rows - shown));
				mangaPagesTouchY -= shift * rowH;
				if (mangaPagesScroll != was)
				{
					// the cursor follows if it was left behind
					if (mangaPagesCursor < mangaPagesScroll * MPG_COLS)
						mangaPagesCursor = std::clamp(mangaPagesScroll * MPG_COLS, 0, pages - 1);
					if (mangaPagesCursor >= (mangaPagesScroll + shown) * MPG_COLS)
						mangaPagesCursor = std::clamp((mangaPagesScroll + shown - 1) * MPG_COLS + MPG_COLS - 1, 0, pages - 1);
					mangaSeek(mangaPagesCursor);
				}
			}
			return false;
		}
		if (pt.kind == Pointer::Up && mangaPagesTouch)
		{
			mangaPagesTouch = false;
			// a tap (no drag) that lifts on the thumbnail it touched: that page, and the grid closes
			int i = mangaPagesPressed;
			mangaPagesPressed = -1;
			if (!mangaPagesMoved && i >= 0 && thumbAt(x, y) == i)
			{
				mangaSeek(i);
				mangaPagesClose(p);
			}
			return false;
		}
		return false;
	}

	void Ui::mangaListBegin(int x, int y, int scroll, int rowH, int item, std::function<int(int)> scrollTo,
		std::function<void(int)> tap)
	{
		mlist = MangaListDrag();
		mlist.on = true;
		mlist.downX = x;
		mlist.downY = mlist.fromY = y;
		mlist.fromScroll = scroll;
		mlist.rowH = std::max(1, rowH);
		mlist.item = item;
		mlist.scrollTo = std::move(scrollTo);
		mlist.tap = std::move(tap);
	}

	bool Ui::mangaListPointer(const Pointer& pt, int x, int y)
	{
		if (!mlist.on) return false;
		if (pt.kind == Pointer::Move)
		{
			// past the slop (as the pages grid's, about 0.03 of the panel) it's a drag, and the lift is no tap
			if (!mlist.moved && std::abs(y - mlist.downY) > MG_LIST_SLOP) mlist.moved = true;
			// a finger that slid sideways off its cover changed its mind: no tap (and no scroll: the lists go up and down)
			if (!mlist.moved && std::abs(x - mlist.downX) > MG_LIST_SLOP) mlist.item = -1;
			if (!mlist.moved || !mlist.scrollTo) return false;
			// counted from where the finger went down, so a slow drag loses nothing to rounding move by move
			int want = mlist.fromScroll + (int)std::lround((mlist.fromY - y) / (double)mlist.rowH);
			int took = mlist.scrollTo(want);
			// stopped at an end: the count starts again here, so the way back moves the list at once
			if (took != want)
			{
				mlist.fromScroll = took;
				mlist.fromY = y;
			}
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			// (moved out first: the tap may open a page that starts another list's finger)
			MangaListDrag d = std::move(mlist);
			mlist = MangaListDrag();
			if (!d.moved && d.item >= 0 && d.tap && std::abs(x - d.downX) <= MG_LIST_SLOP &&
				std::abs(y - d.downY) <= MG_LIST_SLOP)
				d.tap(d.item);
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
						SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);	// scaled to its tile: smooth, not jagged
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
