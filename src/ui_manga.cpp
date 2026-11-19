#include "ui.h"
#include "framewatch.h"
#include "textreader.h"
#include "manganet.h"
#include "ui_internal.h"
#include "ui_settings.h"
#include "ui_reader.h"
#include "ui_manga.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"
#include "pagebook.h"
#include "playlist.h"	// naturalLess: the listing's order kept when titles change
#include "manga_ui_logic.h"
#include "stickscroll.h"
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

	void Ui::Manga::mangaOpen(Player& p)
	{
		Uint32 openAt = SDL_GetTicks();
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
		mangaTouch = false;
		ui.pageReader->mangaSliderPage = -1;
		mangaCtrlUntil = mangaToastUntil = 0;
		mangaTap = false;
		mangaTapDismiss = false;
		mangaLastTapAt = 0;
		mangaCtrlPressed = -1;
		mangaMenu = false;
		mangaMenuCursor = 0;
		ui.pageReader->mangaPages = false;
		ui.pageReader->mangaZoomAsked = 100;
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
		if (!ui.stateDirPath.empty())
		{
			mangaPageCache.open(ui.stateDirPath + "/manga/cache", mangaCacheBytes(p.settings.mangaCacheSize));
			// the cover cache: 50 MB fixed, the oldest covers going first (docs/design/manga.md section 6)
			mangaCoverCache.open(ui.stateDirPath + "/manga/cache/covers", MANGA_COVER_CACHE_LIMIT);
			mangaNetTrim("", p);
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
		// the downloads on the card linked to their source chapters, so a copy's place is the online chapter's
		// whichever page opens it (issue #15): a folder walk of the downloads only, their files read on the worker
		mangaDlEnsureCard(true);
		// (what the panels waited for: a big card once froze them here for minutes)
		fprintf(stderr, "manga: app opened in %u ms on the main thread\n", (unsigned)(SDL_GetTicks() - openAt));
	}

	void Ui::Manga::mangaScan()
	{
		if (!mangaStore)
		{
			mangaStore = std::make_unique<MangaStore>();
			mangaStore->load(ui.stateDirPath);
		}
		// the Files places' Manga folders, each read once. Every series found goes in the store; one the user took out
		// of the library stays out (the store's inLibrary wins over the scan)
		std::vector<std::string> places;
		places.reserve(ui.roots.size());
		for (auto& r : ui.roots) places.push_back(r.path);
		std::vector<std::string> roots = mangaRoots(places);
		if (!libWorker)
		{
			// (no worker: every file opened here, as the scan always was)
			Uint32 t0 = SDL_GetTicks();
			std::vector<LocalSeries> found;
			for (auto& dir : roots)
			{
				std::vector<LocalSeries> one = scanMangaFolder(dir);
				found.insert(found.end(), one.begin(), one.end());
			}
			mangaLocalSet(std::move(found));
			fprintf(stderr, "manga: library read in %u ms (%d series, here, every file opened)\n", (unsigned)(SDL_GetTicks() - t0),
				(int)mangaLocal.size());
			return;
		}
		// in the background: the app shows the library as the store last knew it meanwhile, mangaLibReady takes the
		// listing and then the chapter files read. The index of what each file held is a file of its own, apart from
		// the page cache under manga/cache (Clear cache must neither throw it away nor count it)
		if (!ui.stateDirPath.empty()) libWorker->setCacheFile(ui.stateDirPath + "/manga/library-index.txt");
		mangaScanGen++;
		mangaScanning = true;
		mangaScanCount = 0;
		mangaScanAt = SDL_GetTicks();
		libWorker->scan(roots, mangaScanGen);
	}

	void Ui::Manga::mangaLibReady(std::vector<MangaLibWorker::Result> results)
	{
		for (auto& r : results)
		{
			if (r.kind == MangaLibWorker::Result::Progress)
			{
				if (r.gen == mangaScanGen) mangaScanCount = r.count;
				continue;
			}
			if (r.kind == MangaLibWorker::Result::Listed)
			{
				if (r.gen != mangaScanGen) continue;	// a scan replaced by a newer one
				fprintf(stderr, "manga: library listed in %u ms (%d series, %d chapters; %u ms after asking)\n", r.listMs, r.seriesN,
					r.chaptersN, (unsigned)(SDL_GetTicks() - mangaScanAt));
				mangaScanCount = r.series.size();
				mangaLocalSet(std::move(r.series));
				continue;
			}
			// chapter files read: by path, so an answer from before a newer listing still lands where it belongs
			mangaLocalMetas(r.metas);
			// the Downloads tab's card waited for some of these
			for (const auto& f : r.metas)
				if (f.series.empty()) { mdlCardStale = true; mangaLocalSerial++; break; }
			// (and their links to the source chapters made now, not when a page that shows the card is next drawn)
			if (mdlCardStale) mangaDlEnsureCard();
			if (r.scanDone && r.gen == mangaScanGen)
			{
				mangaScanning = false;
				fprintf(stderr, "manga: library read in %u ms (%d series, %d chapters, %d files opened)\n", r.ms, r.seriesN,
					r.chaptersN, r.opened);
				mangaLocalSerial++;
				if (mangaDirty) mangaSave();
			}
		}
	}

	void Ui::Manga::mangaLocalReindex()
	{
		mangaLocalById.clear();
		for (size_t i = 0; i < mangaLocal.size(); i++)
			mangaLocalById["local:" + mangaLocal[i].path] = i;
	}

	void Ui::Manga::mangaLocalSort()
	{
		// each Manga folder's series together, in the folders' order, each folder's by title (as the listing gives
		// them): a series is a child of its Manga folder, so its folder is its path's parent
		auto parent = [](const std::string& p) {
			size_t s = p.rfind('/');
			return s == std::string::npos ? std::string() : p.substr(0, s);
		};
		std::vector<std::string> order;
		for (const auto& s : mangaLocal)
			if (std::find(order.begin(), order.end(), parent(s.path)) == order.end()) order.push_back(parent(s.path));
		auto rank = [&](const LocalSeries& s) { return std::find(order.begin(), order.end(), parent(s.path)) - order.begin(); };
		std::stable_sort(mangaLocal.begin(), mangaLocal.end(), [&](const LocalSeries& a, const LocalSeries& b) {
			auto ra = rank(a), rb = rank(b);
			if (ra != rb) return ra < rb;
			if (naturalLess(a.title, b.title)) return true;
			return !naturalLess(b.title, a.title) && naturalLess(a.path, b.path);
		});
	}

	bool Ui::Manga::mangaLocalCommit(LocalSeries& s, int64_t now)
	{
		if (!mangaStore) return false;
		std::string id = "local:" + s.path;
		const MangaSeries* old = mangaStore->series(id);
		// a series not read to its end keeps the title the store knew (its ComicInfo's), rather than flapping to the
		// folder's name while its files are read again
		if (old && !old->title.empty() && !seriesKnown(s)) s.title = old->title;
		bool frontKnown = s.chapters.empty() || s.chapters.front().known;
		bool fromSource = false;
		for (const LocalChapter& c : s.chapters)
			if (c.known && isSourceId(c.ciSeriesKey)) { fromSource = true; break; }
		// a new series waits for its first chapter to be read: a folder of downloads must not go into the Library first
		if (!old && !frontKnown && !fromSource) return false;
		MangaSeries m;
		m.id = id;
		m.title = s.title;
		m.source = "local";
		m.cover = frontKnown || !old || old->cover.empty() ? s.cover : old->cover;
		if (old)
		{
			m.added = old->added;
			m.lastRead = old->lastRead;
			m.inLibrary = old->inLibrary;
			m.readingMode = old->readingMode;
		}
		else m.added = now;
		// a folder of downloads isn't in the Library by being on the card: its series is, when the heart says so
		if (fromSource) m.inLibrary = false;
		if (old && old->title == m.title && old->cover == m.cover && old->inLibrary == m.inLibrary && old->source == m.source) return false;
		mangaStore->putSeries(m);
		return true;
	}

	void Ui::Manga::mangaLocalSet(std::vector<LocalSeries> found)
	{
		if (!mangaStore)
		{
			mangaStore = std::make_unique<MangaStore>();
			mangaStore->load(ui.stateDirPath);
		}
		// the lists' cursors stay on what they were on: rows move under them
		std::string listPath = mListCursor >= 0 && mListCursor < (int)mangaLocal.size() ? mangaLocal[mListCursor].path : "";
		std::string chapterPath;
		if (const LocalSeries* s = mangaLocalSeries(mangaSeriesId))
			if (mserCursor >= 0 && mserCursor < (int)s->chapters.size()) chapterPath = s->chapters[mserCursor].path;
		mangaLocal = std::move(found);
		int64_t now = (int64_t)time(nullptr);
		bool changed = false;
		for (auto& s : mangaLocal) changed = mangaLocalCommit(s, now) || changed;
		mangaLocalSort();
		mangaLocalReindex();
		mangaScanned = true;
		mangaFillAsked.clear();
		for (int i = 0; i < (int)mangaLocal.size() && !listPath.empty(); i++)
			if (mangaLocal[i].path == listPath) { mListCursor = i; break; }
		if (mListCursor >= (int)mangaLocal.size()) mListCursor = std::max(0, (int)mangaLocal.size() - 1);
		mListScroll = std::clamp(mListScroll, std::max(0, mListCursor - MG_ROWS + 1), std::max(0, mListCursor));
		if (const LocalSeries* s = mangaLocalSeries(mangaSeriesId))
		{
			for (int i = 0; i < (int)s->chapters.size() && !chapterPath.empty(); i++)
				if (s->chapters[i].path == chapterPath) { mserCursor = i; break; }
			if (mserCursor >= (int)s->chapters.size()) mserCursor = (int)s->chapters.size() - 1;
			if (mserCursor >= 0) mserScroll = std::clamp(mserScroll, std::max(0, mserCursor - MG_SER_ROWS + 1), mserCursor);
			// the series page open: its chapters' files read where they're new or changed
			if (ui.page == Page::MangaSeries || ui.page == Page::MangaRead) mangaLocalWantFill(mangaSeriesId);
		}
		// covers that failed with the store's string, or had no series yet, are asked again with the listing's
		for (auto it = coverFailed.begin(); it != coverFailed.end();)
			it = it->rfind("local:", 0) == 0 ? coverFailed.erase(it) : std::next(it);
		mangaLocalSerial++;
		mangaCacheValid = false;
		if (changed) mangaSave();
	}

	void Ui::Manga::mangaLocalMetas(const std::vector<MangaLibWorker::Found>& metas)
	{
		std::unordered_set<size_t> touched;
		for (const auto& f : metas)
		{
			if (f.series.empty()) continue;
			auto it = mangaLocalById.find("local:" + f.series);
			if (it == mangaLocalById.end() || it->second >= mangaLocal.size()) continue;
			// the file as listed: one changed since is another answer's (a newer listing asks again)
			for (LocalChapter& c : mangaLocal[it->second].chapters)
				if (c.path == f.file.path && c.size == f.file.size && c.mtime == f.file.mtime)
				{
					applyChapterMeta(c, f.file.meta);
					touched.insert(it->second);
					break;
				}
		}
		if (touched.empty()) return;
		std::string listPath = mListCursor >= 0 && mListCursor < (int)mangaLocal.size() ? mangaLocal[mListCursor].path : "";
		std::string chapterPath;
		if (const LocalSeries* s = mangaLocalSeries(mangaSeriesId))
			if (mserCursor >= 0 && mserCursor < (int)s->chapters.size()) chapterPath = s->chapters[mserCursor].path;
		int64_t now = (int64_t)time(nullptr);
		bool changed = false, retitled = false;
		for (size_t i : touched)
		{
			LocalSeries& s = mangaLocal[i];
			std::string was = s.title;
			finishSeries(s);
			changed = mangaLocalCommit(s, now) || changed;
			retitled = retitled || s.title != was;
			coverFailed.erase("local:" + s.path);
		}
		if (retitled)
		{
			mangaLocalSort();
			mangaLocalReindex();
			for (int i = 0; i < (int)mangaLocal.size() && !listPath.empty(); i++)
				if (mangaLocal[i].path == listPath) { mListCursor = i; break; }
			mListScroll = std::clamp(mListScroll, std::max(0, mListCursor - MG_ROWS + 1), std::max(0, mListCursor));
		}
		if (const LocalSeries* s = mangaLocalSeries(mangaSeriesId))
			for (int i = 0; i < (int)s->chapters.size() && !chapterPath.empty(); i++)
				if (s->chapters[i].path == chapterPath)
				{
					mserCursor = i;
					mserScroll = std::clamp(mserScroll, std::max(0, i - MG_SER_ROWS + 1), i);
					break;
				}
		mangaLocalSerial++;
		mangaCacheValid = false;
		// (a scan's probes come in batches: the store is written once at its end; a series page's fill at once)
		if (changed)
		{
			mangaDirty = true;
			if (!mangaScanning) mangaSave();
		}
	}

	void Ui::Manga::mangaLocalWantFill(const std::string& id)
	{
		const LocalSeries* s = mangaLocalSeries(id);
		if (!s || seriesKnown(*s)) return;
		if (!libWorker)
		{
			LocalSeries& w = mangaLocal[mangaLocalById[id]];
			fillMangaSeries(w, nullptr, false);
			mangaLocalCommit(w, (int64_t)time(nullptr));
			mangaLocalSerial++;
			mangaCacheValid = false;
			return;
		}
		// asked once; again after a while only if an answer went missing (a newer listing replaced the chapters)
		Uint32 now = SDL_GetTicks();
		auto it = mangaFillAsked.find(s->path);
		if (it != mangaFillAsked.end() && now - it->second < 10000) return;
		mangaFillAsked[s->path] = now;
		libWorker->fill(*s);
	}

	bool Ui::Manga::mangaLocalFilling(const std::string& id) const
	{
		const LocalSeries* s = mangaLocalSeries(id);
		return s && !seriesKnown(*s);
	}

	void Ui::Manga::mangaLocalForget(const std::vector<std::string>& paths)
	{
		// files deleted here: their chapters (and a series left with none) leave the list now, no scan needed
		std::unordered_set<std::string> gone(paths.begin(), paths.end());
		std::string listPath = mListCursor >= 0 && mListCursor < (int)mangaLocal.size() ? mangaLocal[mListCursor].path : "";
		for (auto it = mangaLocal.begin(); it != mangaLocal.end();)
		{
			bool all = gone.count(it->path) > 0;
			if (!all)
			{
				auto& chs = it->chapters;
				size_t before = chs.size();
				chs.erase(std::remove_if(chs.begin(), chs.end(), [&](const LocalChapter& c) { return gone.count(c.path) > 0; }), chs.end());
				all = chs.empty();
				if (!all && chs.size() != before) finishSeries(*it);
			}
			it = all ? mangaLocal.erase(it) : std::next(it);
		}
		mangaLocalReindex();
		for (int i = 0; i < (int)mangaLocal.size() && !listPath.empty(); i++)
			if (mangaLocal[i].path == listPath) { mListCursor = i; break; }
		mangaLocalSerial++;
		mangaCacheValid = false;
	}

	const LocalSeries* Ui::Manga::mangaLocalAddOne(const std::string& path)
	{
		// one folder listed here and now: a few names and stats, no file opened (the worker reads them after)
		std::vector<std::string> places;
		for (auto& r : ui.roots) places.push_back(r.path);
		for (const std::string& root : mangaRoots(places))
		{
			LocalSeries one;
			if (!listMangaSeries(root, path, libWorker ? libWorker->cache() : nullptr, one)) continue;
			std::string id = "local:" + one.path;
			auto it = mangaLocalById.find(id);
			if (it != mangaLocalById.end() && it->second < mangaLocal.size()) mangaLocal[it->second] = std::move(one);
			else mangaLocal.push_back(std::move(one));
			mangaLocalSort();
			mangaLocalReindex();
			LocalSeries& s = mangaLocal[mangaLocalById[id]];
			if (mangaLocalCommit(s, (int64_t)time(nullptr))) mangaSave();
			mangaFillAsked.erase(s.path);
			mangaLocalWantFill(id);
			mangaLocalSerial++;
			mangaCacheValid = false;
			return mangaLocalSeries(id);
		}
		return nullptr;
	}

	std::string Ui::Manga::mangaReadingLine() const
	{
		std::string t = "Reading your library\xE2\x80\xA6";
		if (mangaScanCount > 0) t += " " + std::to_string(mangaScanCount) + " series";
		return t;
	}

	void Ui::Manga::mangaSave()
	{
		FrameWatchScope fw("progress save");	// (only the copy goes here now: the fsync is on the writer's thread)
		// the main thread copies the store's texts into the one slot the writer takes; a write in flight finishes,
		// the newest wins, and main never waits for the card
		if (mangaStore && writer) writer->write(mangaStore->filesToSave());
		else if (mangaStore) mangaStore->save();	// (before main made the writer: the same files, here)
		mangaDirty = false;
		mangaSavedAt = SDL_GetTicks();
	}

	void Ui::Manga::mangaFlush()
	{
		// leaving the chapter or the app: record where the reader is now, then write it once (the reader's saves are
		// debounced, so the last few pages would otherwise be a few seconds behind). At quit the write is waited for,
		// at most a second, so the last place isn't lost with the process.
		if (ui.pageReader && ui.pageReader->readerStatus.open) mangaRecordProgress();
		if (mangaDirty) mangaSave();
		if (writer) writer->flush(1000);
	}

	void Ui::Manga::mangaCache() const
	{
		uint64_t serial = mangaStore ? mangaStore->serial() : 0;
		if (mangaCacheValid && mangaCacheSerial == serial && mangaCacheCount == (int)mangaLocalSerial) return;
		mangaCacheValid = true;
		mangaCacheSerial = serial;
		mangaCacheCount = (int)mangaLocalSerial;	// (the chapters, for the unread counts: a listing or files read)
		// the series under the Library's cursor: the list is newest-read first, so the one just read goes to the
		// top, and the cursor goes with it rather than staying on the place it left
		std::string atId = mlibGrid >= 0 && mlibGrid < (int)mangaLib.size() ? mangaLib[mlibGrid].id : "";
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
		for (int i = 0; !atId.empty() && i < (int)mangaLib.size(); i++)
			if (mangaLib[i].id == atId)
			{
				mlibGrid = i;
				int row = i / MG_COLS, shown = std::max(1, (MG_LIB_BOTTOM - MG_LIB_TOP) / 186);
				if (row < mlibScroll) mlibScroll = row;
				else if (row >= mlibScroll + shown) mlibScroll = row - shown + 1;
				break;
			}
	}

	const LocalSeries* Ui::Manga::mangaLocalSeries(const std::string& id) const
	{
		auto it = mangaLocalById.find(id);
		return it == mangaLocalById.end() || it->second >= mangaLocal.size() ? nullptr : &mangaLocal[it->second];
	}

	std::string Ui::Manga::mangaTitle(const std::string& id) const
	{
		if (mangaStore)
			if (const MangaSeries* s = mangaStore->series(id)) return s->title;
		// a source series out of the library has no store entry: the one its page was opened with is its name
		if (isSourceId(id) && id == mangaSeriesId && !mangaSrcTitle.empty()) return mangaSrcTitle;
		const LocalSeries* l = mangaLocalSeries(id);
		return l ? l->title : "";
	}

	int Ui::Manga::mangaReadChapterIndex() const
	{
		// a saved server's chapters are the series page's own list
		if (isServerId(ui.pageReader->mangaReadSeriesId)) return mangaServerChapterIndex();
		if (isSourceId(ui.pageReader->mangaReadSeriesId))
		{
			// the source's chapters are the list the chapter was opened with: their ids are "src:<source>:<key>"
			std::string full;
			std::string key;
			splitSourceId(ui.pageReader->mangaReadSeriesId, full, key);
			for (size_t i = 0; i < mangaReadSrcChapters.size(); i++)
				if (sourceItemId(full, mangaReadSrcChapters[i].key) == ui.pageReader->mangaReadChapterId) return (int)i;
			return -1;
		}
		const LocalSeries* s = mangaLocalSeries(ui.pageReader->mangaReadSeriesId);
		return s ? chapterIndexOf(*s, ui.pageReader->mangaReadChapterId) : -1;
	}

	int Ui::Manga::mangaReadChapterTotal() const
	{
		if (isServerId(ui.pageReader->mangaReadSeriesId)) return mangaSrvSeriesId == ui.pageReader->mangaReadSeriesId ? (int)mangaSrvChapters.size() : 0;
		if (isSourceId(ui.pageReader->mangaReadSeriesId)) return (int)mangaReadSrcChapters.size();
		const LocalSeries* s = mangaLocalSeries(ui.pageReader->mangaReadSeriesId);
		return s ? (int)s->chapters.size() : 0;
	}

	float Ui::Manga::mangaWithinFor(const std::string& chapterId, int page) const
	{
		// the place inside the page, only when it opens at the page that place was kept for
		const ChapterProgress* cp = mangaStore && !chapterId.empty() ? mangaStore->progress(chapterId) : nullptr;
		return cp && cp->page == page ? cp->within : 0;
	}

	void Ui::Manga::mangaRecordProgress()
	{
		// a record of where the reader is: the reader's own status is the only thing that knows the page
		if (!mangaStore || ui.pageReader->mangaReadChapterId.empty() || ui.pageReader->readerStatus.pages <= 0)
		{
			// (logged: a chapter left without a record is a History that didn't move, and nothing else would say why)
			fprintf(stderr, "manga: no progress kept for %s (%s)\n", ui.pageReader->mangaReadChapterId.empty() ? "-" : ui.pageReader->mangaReadChapterId.c_str(),
				!mangaStore ? "no store" : ui.pageReader->mangaReadChapterId.empty() ? "no chapter" : "its pages not known yet");
			return;
		}
		ChapterProgress cp;
		cp.seriesId = ui.pageReader->mangaReadSeriesId;
		cp.chapterId = ui.pageReader->mangaReadChapterId;
		cp.chapterTitle = ui.pageReader->mangaReadChapterTitle;
		cp.page = ui.pageReader->readerStatus.page;
		cp.pages = ui.pageReader->readerStatus.pages;
		cp.read = ui.pageReader->readerStatus.page >= ui.pageReader->readerStatus.pages - 1;
		cp.within = ui.pageReader->readerStatus.within;
		cp.at = (int64_t)time(nullptr);
		mangaStore->setProgress(cp, !ui.incognito);
		fprintf(stderr, "manga: progress %s page %d/%d (%.0f%% in)%s%s\n", cp.chapterId.c_str(), cp.page + 1, cp.pages, cp.within * 100,
			ui.incognito ? " (incognito: not in History)" : "", mangaHistOn ? "" : " (History off)");
		// with the History off the place is still kept: only the list stays empty, this entry hidden along with the rest
		if (!mangaHistOn) mangaStore->clearHistory();
		mangaProgPage = cp.page;
		mangaProgPages = cp.pages;
		mangaProgWithin = cp.within;
		mangaProgAt = SDL_GetTicks();
		mangaDirty = true;
	}

	void Ui::Manga::mangaTick()
	{
		// main turns the page; a change is this chapter's new place. The store is written once a second at most
		if (ui.pageReader->readerStatus.open) mangaReadLive = true;
		if (ui.pageReader->readerStatus.pages > 0 && (ui.pageReader->readerStatus.page != mangaProgPage || ui.pageReader->readerStatus.pages != mangaProgPages))
			mangaRecordProgress();
		// a scroll inside the same (tall) page: its place kept too, every 2 s at most
		else if (ui.pageReader->readerStatus.pages > 0 && std::fabs(ui.pageReader->readerStatus.within - mangaProgWithin) > 0.02f &&
			SDL_GetTicks() - mangaProgAt >= 2000)
			mangaRecordProgress();
		if (mangaDirty && SDL_GetTicks() - mangaSavedAt >= MANGA_SAVE_MS) mangaSave();
	}

	void Ui::Manga::mangaCloseReader()
	{
		// a next chapter asked for (R2) and not waited for goes with the reader: its answer would open it again
		mangaSourceCancel();
		fprintf(stderr, "manga: reader closed on %s (status: %d pages, at %d)\n", ui.pageReader->mangaReadChapterId.c_str(), ui.pageReader->readerStatus.pages, ui.pageReader->readerStatus.page + 1);
		mangaRecordProgress();
		mangaSave();
		ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Close, 0, (int64_t)SDL_GetTicks()});
		// the series page comes back on the chapter last read: after L2/R2 that isn't the one opened from it, and
		// the cursor stayed on that one (a network folder's list is its own: left as it was)
		int at = mangaReadChapterIndex();
		if (at >= 0 && mangaSeriesId == ui.pageReader->mangaReadSeriesId && mangaNetView != MNF_SERIES)
		{
			int rows = isSourceId(mangaSeriesId) ? mserSrcRows() : isServerId(mangaSeriesId) ? mserRows() : MG_SER_ROWS;
			mserCursor = at;
			if (at < mserScroll) mserScroll = at;
			if (at >= mserScroll + rows) mserScroll = at - rows + 1;
		}
		ui.page = Page::MangaSeries;
		// a one-file series never showed its page: back to the list it was opened from
		if (mangaSingleFile(ui.pageReader->mangaReadSeriesId))
		{
			ui.page = Page::Manga;
			mangaTab = mangaSeriesFrom == 1 ? MT_BROWSE : mangaSeriesFrom == 2 ? MT_HISTORY : MT_LIBRARY;
			mBrowseList = mangaSeriesFrom == 1;
		}
		mangaMenu = false;
		ui.pageReader->mangaPages = false;
		ui.pageReader->mpFree();
		ui.pageReader->mfFree();	// (fullscreen's bottom-panel pages too: closed from fullscreen, nothing else would free them)
		ui.pageReader->mangaZoomAsked = 100;
		mangaReadLive = false;
		mangaProgPage = mangaProgPages = -1;
	}

	void Ui::Manga::openMangaSeries(const std::string& id, int from, Player& p)
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
		// its chapter files not read yet (their ComicInfo numbers order the list): read now, off this thread
		mangaLocalWantFill(id);
		// a series that is one file (a PDF, a CBZ on its own): no chapters to choose, so the reader at once, where it
		// was left (from the start again once it was read to its end); B goes back to the list (mangaCloseReader)
		if (const LocalSeries* s = mangaSingleFile(id))
		{
			const ChapterProgress* cp = mangaStore ? mangaStore->progress(s->chapters[0].path) : nullptr;
			int at = cp && !(cp->pages > 0 && cp->page >= cp->pages - 1) ? cp->page : 0;
			mangaOpenChapter(*s, 0, at, p);
			return;
		}
		ui.page = Page::MangaSeries;
	}

	const LocalSeries* Ui::Manga::mangaSingleFile(const std::string& id) const
	{
		const LocalSeries* s = mangaLocalSeries(id);
		return s && s->chapters.size() == 1 && s->chapters[0].path == s->path ? s : nullptr;
	}

	void Ui::Manga::mangaOpenChapter(const LocalSeries& s, int i, int page0, Player& p)
	{
		if (i < 0 || i >= (int)s.chapters.size()) return;
		// the chapter open so far is saved before the next one replaces it in readerStatus
		if (ui.page == Page::MangaRead)
		{
			mangaRecordProgress();
			mangaSave();
		}
		const LocalChapter& c = s.chapters[i];
		mangaSeriesId = "local:" + s.path;
		// (L2/R2 step through the chapters' order: the files that settle it are read meanwhile)
		mangaLocalWantFill(mangaSeriesId);
		ui.pageReader->mangaReadSeriesId = mangaSeriesId;
		ui.pageReader->mangaReadChapterId = c.path;
		ui.pageReader->mangaReadChapterTitle = c.title;
		ui.pageReader->mangaRead = {mangaSeriesId, s.title, c.path, c.title, c.path, c.archive, std::max(0, page0), mangaModeFor(mangaSeriesId, p),
			p.settings.mangaFit};
		// the status still holds the chapter before: its numbers aren't this chapter's, so only a change records
		mangaProgPage = ui.pageReader->readerStatus.page;
		mangaProgPages = ui.pageReader->readerStatus.pages;
		mangaReadLive = false;	// (main hasn't opened it yet: its open false isn't a close)
		ui.pageReader->mangaSliderPage = -1;
		mangaCtrlUntil = 0;
		mangaMenu = false;
		ui.pageReader->mangaZoomAsked = 100;	// a chapter opens at the fit (the reader resets its zoom too)
		ui.page = Page::MangaRead;
		// Open fullscreen: the command SELECT sends, right after the chapter (main applies both in one pass)
		if (p.settings.mangaOpenFull) mangaFullscreen(true, p);
	}

	void Ui::Manga::mangaChapterStep(int dir, Player& p)
	{
		// a saved server's chapter steps through its series page's list
		if (isServerId(ui.pageReader->mangaReadSeriesId))
		{
			mangaServerChapterStep(dir, p);
			return;
		}
		// a source's chapter steps through the list it was opened with (its series page holds the same ones)
		if (isSourceId(ui.pageReader->mangaReadSeriesId))
		{
			mangaSourceChapterStep(dir, p);
			return;
		}
		const LocalSeries* s = mangaLocalSeries(ui.pageReader->mangaReadSeriesId);
		int i = s ? chapterIndexOf(*s, ui.pageReader->mangaReadChapterId) : -1;
		if (!s || i < 0) return;
		int j = i + dir;
		if (j < 0 || j >= (int)s->chapters.size()) return;
		mangaOpenChapter(*s, j, 0, p);
	}

	void Ui::Manga::mangaSlide(int x, int y, Player& p)
	{
		// the page slider under a finger: the page at the finger, the reader told at the scrub's pace (a page is a
		// decode, a fetch for a source's) and of the page it's let go on always. In fullscreen it is the controls
		// sheet's: the sheet stays up while it's held, and 4 s more from the last move
		if (ui.pageReader->readerStatus.pages <= 1) return;
		ui.pageReader->mangaSliderPage = -1;
		ui.slideGrab(SL_MANGA, 0, {mangaSliderTrack(), {0, (double)(ui.pageReader->readerStatus.pages - 1), 1}, false, SCRUB_MS, (double)ui.pageReader->readerStatus.page}, x, y, p,
			[this](double v, bool, Player&) {
				if (ui.pageReader->readerStatus.fullscreen) mangaCtrlUntil = SDL_GetTicks() + MG_CTRL_MS;
				ui.pageReader->mangaSeek((int)v);
			});
	}

	void Ui::Manga::drawMangaSlider()
	{
		// the ACCENT fill to the page, its knob; under a finger, the page the finger is at (the pages themselves
		// come at the reader's pace), grown, its number over it
		int pages = ui.pageReader->readerStatus.pages;
		bool held = ui.slideHeld(SL_MANGA);
		ui.drawSlider(mangaSliderTrack(), ui.slideShare(SL_MANGA, 0, pages > 1 ? ui.pageReader->readerStatus.page / (double)(pages - 1) : 0), held,
			held ? "Page " + std::to_string((int)ui.slide.value + 1) + " / " + std::to_string(pages) : "");
	}

	void Ui::Manga::mangaSeriesChoose(int i, Player& p)
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

	void Ui::Manga::mangaSeriesResume(Player& p)
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

	void Ui::Manga::mangaChoose(int tab, int i, Player& p)
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
			if (!s || ci < 0)
			{
				// gone from the cards (or a card that's out, or a book Files now opens): said, not a silent A
				if (cp.seriesId.rfind("local:", 0) == 0)
					mangaNotice(!s ? "It\xE2\x80\x99s not on a card now" : "That chapter isn\xE2\x80\x99t there now", 2000);
				return;
			}
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

	bool Ui::Manga::mangaKey(Button b, Player& p)
	{
		if (ui.page == Page::Manga && mengSheet) return mangaEngineKey(b, p);
		// On this handheld's help or series menu takes every key, B too (B below would leave the list under it,
		// and the next A on On this handheld found the menu still open)
		if (ui.page == Page::Manga && mangaTab == MT_BROWSE && mBrowseList && mangaView == 0 && mlocSheet != MLS_NONE)
			return mangaLocalSheetKey(b, p);
		if (ui.page == Page::Manga && mhisMenu) return mangaHisMenuKey(b, p);
		if (ui.page == Page::Manga && mlibMenu) return mangaLibMenuKey(b, p);
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
		if (ui.page == Page::MangaRead && mangaReadWaiting)
		{
			if (b == Button::B) mangaReadWaitCancel();
			else if (b == Button::A && !mangaSrcLoading && !mangaSrcError.empty()) mangaSourceRetry(p);
			return false;	// (true would be "leave": with a song playing, main went to the file browser)
		}
		if (ui.page == Page::MangaRead)
		{
			Uint32 now = SDL_GetTicks();
			// the Reading menu takes the keys while it shows; B and START close it, reading going on behind
			if (mangaMenu) return mangaMenuKey(b, p);
			// the pages grid (Y): the D-pad moves the cursor, A jumps and closes, B or Y closes
			if (ui.pageReader->mangaPages) return ui.pageReader->mangaPagesKey(b, p);
			// Book: held sideways, the D-pad's UP and DOWN point along the spread
			if (ui.pageReader->readerStatus.book) return mangaBookKey(b, p);
			// RIGHT is forward unless Swap Left/Right is on (to match a right-to-left book); UP/DOWN zoom, and
			// scrolling is the finger's alone (a drag on the page below)
			bool flip = p.settings.mangaSwapLR;
			bool fwd = b == (flip ? Button::Left : Button::Right), back = b == (flip ? Button::Right : Button::Left);
			if (b == Button::Up) ui.pageReader->mangaZoomStep(1);
			else if (b == Button::Down) ui.pageReader->mangaZoomStep(-1);
			else if (back) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Step, -1, (int64_t)now});
			else if (fwd)
			{
				// forward at the end leaves for the next chapter; before it, the reader steps on
				if (ui.pageReader->readerStatus.atEnd) mangaChapterStep(1, p);
				else ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Step, 1, (int64_t)now});
			}
			else switch (b)
			{
			case Button::L: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Page, -1, (int64_t)now}); break;
			case Button::R: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Page, 1, (int64_t)now}); break;
			case Button::A: if (ui.pageReader->readerStatus.failedHere) ui.pageReader->mangaRetry(); else ui.pageReader->mangaZoomToggle(); break;
			case Button::X: mangaCycleMode(p); break;
			case Button::L2: mangaChapterStep(-1, p); break;
			case Button::R2: mangaChapterStep(1, p); break;
			// SELECT: fullscreen on / off; B leaves fullscreen first, and only then closes the reader
			case Button::Select: mangaFullscreen(!ui.pageReader->readerStatus.fullscreen, p); break;
			// START: the Reading menu over the page (over the fullscreen view's lower part), not the settings page
			case Button::Start: mangaOpenReadingMenu(); break;
			case Button::Y: ui.pageReader->mangaPagesOpen(p); break;
			// L3: turn the page 90° clockwise (paged modes only; the Strip ignores it, as the design says).
			// each press cycles 0 → 90 → 180 → 270 → 0, kept per chapter, back to 0 on another chapter
			case Button::L3: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Rotate, 0, (int64_t)now}); break;
			// B goes back to the chapters; fullscreen stays how the reader reads (SELECT leaves it)
			case Button::B: mangaCloseReader(); break;
			default: break;
			}
			return false;
		}
		// (the mini bar shown on the app's other pages says SELECT goes to what plays)
		if (b == Button::Select && ui.nowPlayable(p)) { ui.goNowPlaying(p); return false; }
		if (ui.page == Page::MangaSeries)
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
				ui.page = Page::Manga;
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
			if (b == Button::X) { mlibMenu = true; mlibArmed = false; break; }
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

	bool Ui::Manga::mangaPointer(const Pointer& pt, int x, int y, Player& p)
	{
		// a source's answers land here too (a tap may be what asks for the next page)
		mangaBrowsePoll(p);
		if (ui.page == Page::MangaRead && mangaReadWaiting)
		{
			if (pt.kind == Pointer::Down && ui.buttonRect(BACK_ICON).hit(x, y)) mangaReadWaitCancel();
			return false;
		}
		if (ui.page == Page::MangaRead)
		{
			// the pages grid takes the finger while it shows: a tap on a thumbnail jumps, a drag scrolls
			if (ui.pageReader->mangaPages) return ui.pageReader->mangaPagesPointer(pt, x, y, p);
			// the Reading menu, when it shows, takes the finger: a tap on a row acts as A (the page underneath
			// doesn't drag until it is closed)
			if (mangaMenu)
			{
				if (pt.kind == Pointer::Down && mangaMenuRect().hit(x, y))
				{
					auto rows = mangaMenuRows();
					int r = (y - MGM_TOP) / MGM_ROW_H;
					if (y >= MGM_TOP && r >= 0 && r < (int)rows.size())
					{
						mangaMenuCursor = rows[r];
						mangaMenuAct(p);
					}
				}
				return false;
			}
			// the drag area and the slider follow the finger; the buttons act as it lifts
			Uint32 now = SDL_GetTicks();
			// Book: the panel is a page; a swipe along the spread (up or down the panel) turns it, a tap shows the
			// page numbers. No drag of the page itself and no zoom: it is shown whole
			if (ui.pageReader->readerStatus.book)
			{
				if (pt.kind == Pointer::Down)
				{
					if (ui.pageReader->readerStatus.failedHere && ui.pageReader->mangaRetryRect(true).hit(x, y)) { ui.pageReader->mangaRetry(); return false; }
					mangaTouch = true;
					mangaTapX = x;
					mangaTapY = y;
					mangaTapAt = now;
				}
				else if (pt.kind != Pointer::Move && mangaTouch)
				{
					mangaTouch = false;
					int dy = y - mangaTapY, dx = x - mangaTapX;
					if (std::abs(dy) > 40 && std::abs(dy) > std::abs(dx))
						mangaBookTurn(mangaBookSwipe(ui.pageReader->readerStatus.book, ui.pageReader->readerStatus.mode == MANGA_RTL, p.settings.mangaSwapLR, dy), p);
					else ui.pageReader->mfTagUntil = now + 1500;
				}
				return false;
			}
			bool full = ui.pageReader->readerStatus.fullscreen;
			if (pt.kind == Pointer::Down)
			{
				mangaPressed = -1;
				mangaTouch = false;
				ui.pageReader->mangaSliderPage = -1;
				mangaTap = false;
				mangaTapDismiss = false;
				// (mangaLastTapAt is the last tap's: not cleared here, or a double-tap could never see its first)
				mangaCtrlPressed = -1;
				// a page that failed: its Try again, in the middle, before anything under it takes the finger
				if (ui.pageReader->readerStatus.failedHere && ui.pageReader->mangaRetryRect(full).hit(x, y))
				{
					ui.pageReader->mangaRetry();
					return false;
				}
				// fullscreen's controls up: the sheet takes the finger (any touch there shows it 4 s more), and the
				// pages outside it only wait to become a drag or, lifted, a tap that puts the bar away
				if (full && mangaCtrlUntil && now < mangaCtrlUntil)
				{
					if (mangaSheetRect().hit(x, y))
					{
						mangaCtrlUntil = now + MG_CTRL_MS;
						if (mangaSliderHit().hit(x, y)) mangaSlide(x, y, p);
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
				if (!full && !mangaTapDismiss && ui.buttonRect(BACK_ICON).hit(x, y)) { ui.pressedBtn = BACK_ICON; return false; }
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
					mangaTouchMoves = mangaTouchFar = 0;
					ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::TouchDown, 0, (int64_t)now});
					return false;
				}
				if (mangaSliderHit().hit(x, y))
				{
					mangaSlide(x, y, p);
					return false;
				}
				int ci = mangaReadChapterIndex();
				if (ci > 0 && mangaPrevRect().hit(x, y)) mangaPressed = MG_PREV;
				else if (ci >= 0 && ci + 1 < mangaReadChapterTotal() && mangaNextRect().hit(x, y)) mangaPressed = MG_NEXT;
				else if (mangaFullRect().hit(x, y)) mangaPressed = MG_FULL;
				else if (mangaLightRect().hit(x, y)) mangaPressed = MG_LIGHT;
				return false;
			}
			if (pt.kind == Pointer::Move)
			{
				if (mangaTouch)
				{
					// a finger within 12 units may still be a tap, and a tap doesn't scroll; past them it is a drag,
					// and the controls over the pages go at once
					mangaTouchMoves++;
					mangaTouchFar = std::max(mangaTouchFar, std::max(std::abs(x - mangaTapX), std::abs(y - mangaTapY)));
					if (mangaTap && (std::abs(x - mangaTapX) > MG_TAP_UNITS || std::abs(y - mangaTapY) > MG_TAP_UNITS))
					{
						mangaTap = false;
						mangaTapDismiss = false;
						mangaCtrlUntil = 0;
					}
					// the finger's own y is already 0..1: its step goes as it is. The touch point follows the finger
					// through the slop too, so a drag starts from where the finger is when it becomes one: the page
					// doesn't jump the slop's way to catch up (as the PDF viewer's drag starts)
					double dy = pt.y - mangaTouchY, dx = pt.x - mangaTouchX;
					mangaTouchY = pt.y;
					mangaTouchX = pt.x;
					// (both: the strip goes by dy, the paged modes by dx)
					if (!mangaTap && (dy != 0 || dx != 0)) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Drag, dy, (int64_t)now, dx});
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
				// a quick flick can lift past the slop with no move event beyond it in between (#11): the lift's own
				// place decides too, and such a lift is a drag
				if (mangaTap && (std::abs(x - mangaTapX) > MG_TAP_UNITS || std::abs(y - mangaTapY) > MG_TAP_UNITS))
				{
					mangaTap = false;
					mangaTapDismiss = false;
					mangaCtrlUntil = 0;
				}
				// a drag: the way from its last move to the lift is sent too, or a short swipe whose lift lands past
				// its last move stops short of the finger (#11)
				if (!mangaTap)
				{
					double dy = pt.y - mangaTouchY, dx = pt.x - mangaTouchX;
					if (dy != 0 || dx != 0) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Drag, dy, (int64_t)now, dx});
				}
				ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Release, 0, (int64_t)now});
				const char* verdict = !mangaTap ? "swipe" : now - mangaTapAt > MG_TAP_MS ? "held (no tap)" : "tap";
				// a tap (no drag, soon enough and close by): a second one soon is a double-tap, which zooms to 200 %
				// at the tapped point / back to the fit (as A does, but at the finger); alone, fullscreen's controls show
				if (mangaTap && now - mangaTapAt <= MG_TAP_MS)
				{
					// the double-tap is asked first: in fullscreen the first tap raised the bar, so the second one
					// is "outside the bar" too, and taken for the bar's dismissal it could never zoom
					if (now - mangaLastTapAt <= 300 && std::abs(mangaTapX - mangaLastTapX) <= 20 &&
						std::abs(mangaTapY - mangaLastTapY) <= 20)
					{
						double z = ui.pageReader->mangaZoomAsked >= 200 ? 1.0 : 2.0;
						ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::ZoomPoint, z, (int64_t)now,
							mangaTapX / (double)Ui::W, mangaTapY / (double)Ui::H});
						mangaLastTapAt = 0;
						mangaCtrlUntil = 0;	// the bar its first tap raised goes with the zoom
						verdict = "double-tap";
					}
					// the bar was up and this tap was outside it: it goes, and the tap does nothing else
					else if (mangaTapDismiss)
					{
						mangaCtrlUntil = 0;
						verdict = "tap (closed the bar)";
					}
					else
					{
						mangaLastTapAt = now;
						mangaLastTapX = mangaTapX;
						mangaLastTapY = mangaTapY;
						if (full) mangaCtrlUntil = now + MG_CTRL_MS;
					}
				}
				// one line a touch, to tell on the handheld whether a lift lands past the slop with no move beyond
				// it in between (#11)
				fprintf(stderr, "reader touch: down %d,%d lift %d,%d moves %d (furthest %d units) %u ms -> %s\n",
					mangaTapX, mangaTapY, x, y, mangaTouchMoves, mangaTouchFar, (unsigned)(now - mangaTapAt), verdict);
				mangaTap = false;
				mangaTapDismiss = false;
				return false;
			}
			if (mangaPressed >= 0)
			{
				int id = mangaPressed;
				mangaPressed = -1;
				if (id == MG_PREV && mangaPrevRect().hit(x, y)) mangaChapterStep(-1, p);
				else if (id == MG_NEXT && mangaNextRect().hit(x, y)) mangaChapterStep(1, p);
				else if (id == MG_FULL && mangaFullRect().hit(x, y)) mangaFullscreen(!ui.pageReader->readerStatus.fullscreen, p);
				else if (id == MG_LIGHT && mangaLightRect().hit(x, y)) ui.openLight();
			}
			return false;
		}
		// a source's grid follows the finger (a drag scrolls it), so it gets every move, not only the touch
		if (ui.page == Page::Manga && mangaView == 1)
			return mangaSourcePointer(pt, x, y, p);
		if (ui.page == Page::MangaSeries && mangaSourceSeriesOn() && pt.kind != Pointer::Down) return mangaSourceSeriesPointer(pt, x, y, p);
		// the tabs, a card, a cover, a row: a tap acts on the spot, as the browser's tabs do
		if (pt.kind != Pointer::Down) return false;
		mangaPressed = -1;
		if ((ui.page == Page::Manga || ui.page == Page::MangaSources) && mengSheet)
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
		if (ui.page == Page::Manga && mlibMenu)
		{
			if (pt.kind != Pointer::Down) return false;
			if (!mlibMenuBox().hit(x, y)) { mlibMenu = false; mlibArmed = false; return false; }
			if (mlibMenuRow().hit(x, y)) mangaLibMenuAct(p);
			return false;
		}
		if (ui.page == Page::Manga && mhisMenu)
		{
			if (!mhisMenuBox().hit(x, y)) { mhisMenu = false; return false; }
			for (int i = 0; i < MHIS_MENU_ROWS; i++)
				if (mhisMenuRow(i).hit(x, y)) { mhisMenuCursor = i; mangaHisMenuAct(p); }
			return false;
		}
		if (ui.page == Page::MangaSources) return mangaSourcesPointer(pt, x, y, p);
		if (ui.page == Page::Manga)
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
				ui.pageReader->mangaListBegin(x, y, mlibScroll, 186, hit, [this](int want) {
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
				ui.pageReader->mangaListBegin(x, y, mhisScroll, MG_HIST_ROW_H, i < n ? i : -1, [this](int want) {
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
			ui.pageReader->mangaListBegin(x, y, mserScroll, ROW_H, i < n ? i : -1, [this](int want) {
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

	SDL_Texture* Ui::Manga::coverOf(const std::string& seriesId)
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
		std::string source = s ? s->cover : "";
		if (!s && seriesId.rfind("local:", 0) == 0 && (!mangaScanned || mangaScanning))
		{
			// the library as last known while the card is read: the cover the store kept (asked again with the
			// listing's if it fails), else wait for the listing rather than give up on it
			const MangaSeries* ms = mangaStore ? mangaStore->series(seriesId) : nullptr;
			if (!ms || ms->cover.empty()) return nullptr;
			source = ms->cover;
		}
		// nothing to read is nothing to ask: the generated cover is the answer
		if (source.empty())
		{
			coverFailed.insert(seriesId);
			return nullptr;
		}
		coverWorker->want(seriesId, source, std::max(1, (int)std::lround(MG_COVER_W * sx)),
			std::max(1, (int)std::lround(MG_COVER_H * sy)));
		coverAsked.insert(seriesId);
		return nullptr;
	}

	// a cover by its URL: the cover cache holds the bytes (keyed by URL), the worker decodes them to the tile's
	// pixels. A card on screen asks once; a card that scrolls away cancels its fetch. A cover already in the cache
	// is decoded straight from there, with no network
	SDL_Texture* Ui::Manga::coverOfUrl(const std::string& coverUrl, int w, int h,
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

	std::string Ui::Manga::mangaSeriesCoverUrl(const std::string& seriesId) const
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

	std::string Ui::Manga::mangaCoverKey(const std::string& url) const
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

	std::string Ui::Manga::mangaCoverStored(const std::string& url) const
	{
		std::string k = mangaCoverKey(url);
		return k.rfind("javahost/", 0) == 0 ? k : url;
	}

	void Ui::Manga::mangaCoverCancelOffScreen()
	{
		if (!mangaNet || !mangaNet->started()) { mangaCoverFetches.clear(); return; }
		for (auto it = mangaCoverFetches.begin(); it != mangaCoverFetches.end(); )
		{
			if (mangaCoverOnScreen.count(it->first)) { ++it; continue; }
			mangaNet->fetchApi().cancel(it->second);
			it = mangaCoverFetches.erase(it);
		}
	}

	void Ui::Manga::mangaCoverCancelAll()
	{
		if (mangaNet && mangaNet->started())
			for (auto& kv : mangaCoverFetches) mangaNet->fetchApi().cancel(kv.second);
		mangaCoverFetches.clear();
		mangaCoverOnScreen.clear();
		coverUrlAsked.clear();
	}

	// one fetch answer: if its id matches a cover we asked for, put its bytes in the cover cache and hand the
	// cache file to the worker for decode. Returns true if it was one of ours (the caller skips its own handling)
	bool Ui::Manga::mangaCoverOnFetched(MangaFetch::Result& r)
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

	void Ui::Manga::coversReady(std::vector<CoverWorker::Result> results)
	{
		for (auto& r : results)
		{
			coverAsked.erase(r.key);
			coverUrlAsked.erase(r.key);	// a URL cover the worker just decoded: the next draw can ask again if scrolled back
			SDL_Texture* t = nullptr;
			if (r.ok)
			{
				roundCover(r.rgba, r.w, r.h);
				t = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, r.w, r.h);
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

	void Ui::Manga::drawMangaCover(Rect r, const std::string& title, bool focus, int bookCell, const std::string& seriesId,
		const std::string& coverUrl, const std::vector<std::pair<std::string, std::string>>* coverHeaders)
	{
		Rect in = r;
		int rad = R_TILE;
		if (focus)
		{
			// the focused cover's ACCENT edge, the gradient inside it
			ui.fillRound(r, ACCENT, R_TILE);
			in = {r.x + theme::EDGE, r.y + theme::EDGE, r.w - 2 * theme::EDGE, r.h - 2 * theme::EDGE};
			rad = R_TILE - theme::EDGE;
		}
		if (SDL_Texture* tex = coverOf(seriesId))
		{
			// the real page over the tile's place, its own corners rounded at upload: the ACCENT edge still shows
			SDL_Rect d = ui.devRect(in);
			SDL_RenderCopy(ui.ren, tex, nullptr, &d);
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
				SDL_Rect d = ui.devRect(in);
				SDL_RenderCopy(ui.ren, tex, nullptr, &d);
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
			ui.fillRoundBand(in, rad, in.y + y, in.y + y1, col);
		}
		ui.drawGlyphMid(G_BOOK, in.x + in.w / 2, in.y + in.h / 2, DIM, bookCell);
	}

	void Ui::Manga::drawMangaTabs()
	{
		Rect first = mangaTabRect(0), last = mangaTabRect(MANGA_TABS - 1);
		ui.drawTextMid("L", 0, ACCENT, first.x - 12, first.y + first.h / 2, 0, 2);
		for (int i = 0; i < MANGA_TABS; i++)
			ui.drawTab(mangaTabRect(i), MANGA_TAB_NAMES[i], i == mangaTab, 0);
		ui.drawTextMid("R", 0, ACCENT, last.x + last.w + 12, last.y + last.h / 2, 0, 2);
	}

	// ---- the area: Ui's press, pointer, tick, renderPage and signature while one of its pages shows ----

	bool Ui::Manga::key(Button b, Player& p)
	{
		// Manage extensions and the manga reader's pages: their own keys (the sheet over the first)
		if (ui.page == Page::Extensions || ui.page == Page::Manga) return extKey(b, p);
		if (ui.page == Page::MangaSeries || ui.page == Page::MangaRead) return mangaKey(b, p);
		if (ui.page == Page::MangaSources && mengSheet) return mangaEngineKey(b, p);
		if (ui.page == Page::MangaSources) return mangaSourcesPageKey(b, p);
		if (ui.page == Page::MangaSettings) return mangaSettingsKey(b, p);
		return false;
	}

	bool Ui::Manga::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		// a down (pointerDownLate: the reader's page aside), after the header's "<" and the empty page's button: the
		// pages' own taps; the Manga settings' rows are the settings list's
		if (pt.kind == Pointer::Down && ui.page != Page::MangaRead)
		{
			if (ui.page == Page::Extensions || ui.page == Page::Manga) return extPointer(pt, x, y, p);
			if (ui.page == Page::MangaSeries || ui.page == Page::MangaSources) return mangaPointer(pt, x, y, p);
			if (ui.page == Page::MangaSettings) return ui.settingsPage->settingsDown(x, y);
			return false;
		}
		// the reader's page: its drag area and slider take the finger's moves too (the mini bar's taken before)
		if (ui.page == Page::MangaRead) return mangaPointer(pt, x, y, p);
		// a manga source's grid scrolls with the finger: its moves and lift go to it (its down comes late)
		if (((ui.page == Page::Manga && mangaView == 1) || (ui.page == Page::MangaSeries && mangaSourceSeriesOn())) && pt.kind != Pointer::Down)
			return mangaPointer(pt, x, y, p);
		// Manage extensions and the app's tabs: a tile or a row acts as the finger lifts (as Home's do), so the lift is
		// taken here, before Ui's down-only pages; the down comes late
		if (pt.kind == Pointer::Up && (ui.page == Page::Extensions || ui.page == Page::Manga)) return extPointer(pt, x, y, p);
		return false;
	}

	void Ui::Manga::tick(Player& p)
	{
		// the manga reader's page: the place recorded, the store written once a second while reading
		if (ui.page == Page::MangaRead) mangaTick();
		// the Sources page: the repositories' events become toasts, and what was typed in is added
		else if (ui.page == Page::MangaSources) mangaSourcesTick(p);
		// the Downloads tab: an armed delete goes, a lost worker answer waits for the reader to be on again
		else if (ui.page == Page::Manga && mangaTab == MT_DOWNLOADS) mangaDownloadsTick(p);
	}

	void Ui::Manga::draw(const Player& p)
	{
		if (ui.page == Page::Extensions) drawExtensions(p);
		else if (ui.page == Page::Manga) drawManga(p);
		else if (ui.page == Page::MangaSeries) drawMangaSeries(p);
		else if (ui.page == Page::MangaRead) drawMangaRead(p);
		else if (ui.page == Page::MangaSettings) drawMangaSettings(p);
		else if (ui.page == Page::MangaSources) drawMangaSourcesPage(p);
	}

	std::string Ui::Manga::extSignature() const
	{
		return "|ext" + std::to_string(ui.mangaOn) + std::to_string(extCursor) + std::to_string(extSheet) +
			std::to_string(extSheetCursor) + " " + std::to_string(extPressed);
	}

	std::string Ui::Manga::signature(const Player& p) const
	{
		(void)p;
		std::string sig;
		// the manga reader's settings: the page cache's Clear (what it holds not being a setting, and the press waiting
		// for its second), and the Java extensions' host state and progress
		if (ui.page == Page::MangaSettings)
			sig += "|mc" + std::to_string(mangaPageCache.size() + mangaNetCacheBytes) + " " + std::to_string(mangaCacheAt != 0) +
				"|mj" + std::to_string(mangaJavaSerial()) + " " + std::to_string(mangaJavaAt != 0);
		// Extensions: the drawer's tile and the note's focus, the switch (its lines and rows say it), a finger's button
		// down on them (mangaOn is in the settings too: a change redraws anywhere)
		if (ui.page == Page::Extensions || ui.page == Page::Manga || ui.browse->browseDir == EXTDIR) sig += extSignature();
		// Manga: the tab, each page's cursors and scrolls, the store and the card's scan, the finger's article, the
		// History's Clear armed; the reader's page when it shows (its status is main's answer)
		if (ui.page == Page::Manga || ui.page == Page::MangaSeries || ui.page == Page::MangaRead || ui.page == Page::MangaSources)
		{
			char mb[200];
			snprintf(mb, sizeof mb, "|mg%d %d,%d %d,%d,%d,%d %d,%d %d,%d %llu %d %d %d %zu %llu",
				mangaTab, mlibGrid, mlibScroll, mhisCursor, mhisScroll, mbrowseCursor, mListCursor,
				mListScroll, (int)mBrowseList, mserCursor, mserScroll, (unsigned long long)(mangaStore ? mangaStore->serial() : 0),
				(int)mangaLocal.size(), mangaPressed, mangaClearAt != 0, coverTex.size(), (unsigned long long)coverSerial);
			// the card's library: a listing or chapter files read (mangaLocalSerial), the reading line's count
			sig += "|mlib" + std::to_string(mangaLocalSerial) + (mangaScanning ? " r" + std::to_string(mangaScanCount) : "");
			sig += std::string(mb) + "|" + mangaSeriesId + "|ml" + std::to_string(mlocSheet) + std::to_string(mlocCursor) +
				std::to_string((int)mlocDeleteArmed) + "|me" + std::to_string((int)mengSheet) + std::to_string(mengCursor) + std::to_string((int)mengBusy) + std::to_string((int)mengUpgrade) + mangaEngineSignature();
			// Sources: the sub-list and each list's place, a sheet, the sources installing, the manager's own changes
			// (repos().serial(): an index loaded, a source installed) and what waits to be added
			if (ui.page == Page::Manga || ui.page == Page::MangaSources)
			{
				char sb[240];
				snprintf(sb, sizeof sb, "|ms%d %d,%d %d,%d %d,%d %d %d %d %llu %llu %zu %d", mstTab, mstCursor[0],
					mstScroll[0], mstCursor[1], mstScroll[1], mstCursor[2], mstScroll[2], mstModal, mstModalCursor,
					(int)mstUninstallArmed, (unsigned long long)mangaReposSerial(), (unsigned long long)mstLocal,
					mstInstalling.size(), (int)mstAdding.size());
				sig += std::string(sb) + mstModalId + "|" + mstPendingUrl + "|mt" +
					std::to_string((long long)time(nullptr) / 60);	// (the repositories' "2 h ago")
			}
			// Browse's sources and the source series' details, chapters and jobs (ui_manga_browse.cpp)
			sig += mangaNetSignature();
			// Downloads: the tab's lists and the Download sheet (ui_manga_downloads.cpp)
			sig += mangaDlSignature();
			if (ui.page == Page::MangaRead)
			{
				char rb[160];
				snprintf(rb, sizeof rb, "|rs%d%d%d%d %d %d %d%d%d z%d %d%d%d r%d", (int)ui.pageReader->readerStatus.open, (int)ui.pageReader->readerStatus.loading,
					(int)ui.pageReader->readerStatus.atEnd, (int)mangaReadLive, ui.pageReader->readerStatus.page, ui.pageReader->readerStatus.pages, (int)mangaDirty,
					(int)mangaTouch, (int)ui.slideHeld(SL_MANGA), ui.pageReader->readerStatus.zoomPct, (int)mangaMenu, mangaMenuCursor,
					(int)ui.pageReader->mangaPages, ui.pageReader->readerStatus.rotation);
				sig += std::string(rb) + ui.pageReader->readerStatus.error + "|" + ui.pageReader->mangaReadChapterId + "|m" + std::to_string(ui.pageReader->readerStatus.mode) +
					"|w" + std::to_string((int)ui.pageReader->readerStatus.failedHere) + std::to_string((int)mangaReadWaiting) + std::to_string((int)mangaSrcLoading) + mangaSrcError;
				// the pages grid: the cursor, the scroll, the textures made, the thumbnails arrived
				if (ui.pageReader->mangaPages)
					sig += "|pg" + std::to_string(ui.pageReader->mangaPagesCursor) + "," + std::to_string(ui.pageReader->mangaPagesScroll) + " " +
						std::to_string(ui.pageReader->mpTex.size()) + "," + std::to_string(ui.pageReader->readerThumbs.pages.size());
				// fullscreen (or Book): where the pages are on this panel, which have their textures, the tag
				if (ui.pageReader->readerStatus.fullscreen || ui.pageReader->readerStatus.book)
				{
					sig += "|bk" + std::to_string(ui.pageReader->readerStatus.book) + (mangaBookToast ? "t" : "");
					sig += "|full" + std::to_string(ui.pageReader->readerBottom.gen) + " " + std::to_string(ui.pageReader->mfTex.size()) + " " +
						std::to_string(ui.pageReader->mfWaiting) + " " + std::to_string(ui.pageReader->readerBottom.pages.size()) + (SDL_GetTicks() < ui.pageReader->mfTagUntil ? "T" : "");
					// the controls over the pages and the first open's toast: how long they still show, in 100 ms
					// steps, so each hides by itself (no finger, no other change to draw for)
					Uint32 now = SDL_GetTicks();
					sig += "|fc" +
						std::to_string(mangaCtrlUntil && now < mangaCtrlUntil ? (int)((mangaCtrlUntil - now + 99) / 100) : 0) + " " +
						std::to_string(mangaToastUntil && now < mangaToastUntil ? (int)((mangaToastUntil - now + 99) / 100) : 0) + " " +
						std::to_string(mangaCtrlPressed);
					for (auto& b : ui.pageReader->readerBottom.boxes)
						sig += " " + std::to_string(b.page) + ":" + std::to_string((int)std::lround(b.x * 4096)) + "," + std::to_string((int)std::lround(b.y * 4096));
				}
			}
		}
		return sig;
	}

	void Ui::Manga::drawManga(const Player& p)
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
		ui.drawHeader("Manga", p);
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

	bool Ui::Manga::mangaEngineKey(Button b, Player& p)
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

	void Ui::Manga::mangaEngineChoose(int i, Player& p)
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

	bool Ui::Manga::mangaEngineMissing() const
	{
		if (!mangaNet || !mangaNet->started()) return false;
		const JavaExtHost& h = mangaNet->javaExt();
		auto st = h.state();
		return st == JavaExtHost::State::NotInstalled || (st == JavaExtHost::State::Failed && h.installedBytes() == 0);
	}

	void Ui::Manga::mangaEngineOffer()
	{
		mengSheet = true;
		mengBusy = false;
		mengCursor = 0;
	}

	std::string Ui::Manga::mangaEngineSignature() const
	{
		if (!mengBusy || !mangaNet) return "";
		// the download's progress, a step per MB, and its state (done, failed)
		int64_t d = 0, t = 0;
		mangaNet->javaExt().progress(d, t);
		return "," + std::to_string((int)mangaNet->javaExt().state()) + "," + std::to_string((int)mangaNet->javaExt().step()) +
			"," + std::to_string((int)mangaNet->javaExt().upgrade()) +
			"," + std::to_string(d >> 20);
	}

	void Ui::Manga::mangaEngineWatch(Player& p)
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

	void Ui::Manga::drawMangaEngineSheet()
	{
		Rect b = mangaEngineBox();
		if (mengBusy && mangaNet)
		{
			JavaExtHost& h = mangaNet->javaExt();
			int64_t done = 0, total = 0;
			h.progress(done, total);
			if (total <= 0) total = mengUpgrade ? 58000000 : 241000000;
			ui.drawDialog(b, mengUpgrade ? "Downloading the lighter engine" : "Downloading the manga engine");
			double f = std::clamp((double)done / (double)total, 0.0, 1.0);
			// what it's doing: the runtime, then (an install) the server; between them the runtime is unpacked, no
			// bytes coming, so a ring says it's working where the bar would stand still
			auto step = h.step();
			if (step == JavaExtHost::Step::Unpacking)
			{
				ui.drawLoader(b.x + b.w / 2, b.y + 118, "Unpacking the runtime\xE2\x80\xA6");
			}
			else
			{
				char line[96];
				snprintf(line, sizeof line, "%s  \xC2\xB7  %lld of %lld MB",
					step == JavaExtHost::Step::Server ? "Downloading the server" : "Downloading the runtime",
					(long long)(done / 1000000), (long long)(total / 1000000));	// (MB as the offer counts them: 241, 58)
				ui.drawTextMid(line, 1, TEXT, b.x + b.w / 2, b.y + 100, b.w - 64, 2);
				ui.drawLevel({b.x + 32, b.y + 132, b.w - 64, 8}, (int)std::lround((b.w - 64) * f), TRACK, ACCENT);
			}
			ui.drawTextMid("Keep Banana Player open until it's done.", 0, DIM, b.x + b.w / 2, b.y + 172, b.w - 64, 2);
			ui.drawDialogRow(mangaEngineRow(1), &G_CROSS, "Cancel", true, false, DANGER, "", {0, 0, 0, 0});
			ui.drawHints({{"A", "Cancel"}});
			return;
		}
		ui.drawDialog(b, mengUpgrade ? "A lighter manga engine" : "Get the manga engine?");
		static const char* TEXT_ = "Most online sources need it: a one-time download, kept on the card. It runs only while "
			"you use them.";
		static const char* UP_ = "It uses about 70 MB less memory, so it keeps running while you read. A one-time "
			"download; the engine you have works meanwhile.";
		auto lines = ui.wrapText(mengUpgrade ? UP_ : TEXT_, 0, b.w - 64, 3);
		for (size_t i = 0; i < lines.size(); i++)
			ui.drawTextMid(lines[i], 0, TEXT, b.x + 32, b.y + 84 + (int)i * 26, b.w - 64);
		// a download that failed says why, in place of the note
		std::string err = !mangaNet ? "" : mengUpgrade
			? (mangaNet->javaExt().upgrade() == JavaExtHost::Upgrade::Failed ? mangaNet->javaExt().upgradeError() : "")
			: (mangaNet->javaExt().state() == JavaExtHost::State::Failed ? mangaNet->javaExt().error() : "");
		if (!err.empty())
			ui.drawTextMid(err, 0, DANGER, b.x + 32, b.y + 84 + (int)lines.size() * 26 + 8, b.w - 64);
		else if (!mengUpgrade)
			ui.drawTextMid("Manga on your card works without it.", 0, DIM, b.x + 32, b.y + 84 + (int)lines.size() * 26 + 8, b.w - 64);
		ui.drawDialogRow(mangaEngineRow(0), &G_DOWNLOAD, "Download", mengCursor == 0, false, TEXT, mengUpgrade ? "58 MB" : "241 MB", DIM);
		ui.drawDialogRow(mangaEngineRow(1), nullptr, "Not now", mengCursor == 1, false, TEXT, "", {0, 0, 0, 0});
		ui.drawHints({{"A", "Choose"}, {"B", "Not now"}});
	}

	void Ui::Manga::drawMangaLibrary(const Player& p)
	{
		(void)p;
		mangaCache();
		// the label on the line under the tabs, then the cover grid right below it (no Continue reading strip)
		ui.drawTextMid("Library \xC2\xB7 " + std::to_string(mangaLib.size()) + " series", 0, DIM, 16, MG_LIB_TOP - 14);
		// the card read in the background: the grid is the library as last known until it ends, then updates in place
		if (mangaScanning && !mangaLib.empty()) ui.drawTextMid(mangaReadingLine(), 0, DIM, 624, MG_LIB_TOP - 14, 360, 1);
		int n = (int)mangaLib.size();
		if (n == 0 && mangaScanning)
		{
			ui.drawEmpty(G_BOOK, mangaReadingLine(), "The series on your card show up here in a moment.", nullptr);
			ui.drawHints({{"B", "Back"}});
			return;
		}
		if (n == 0)
		{
			ui.drawEmpty(G_BOOK, "Your library is empty",
				"Put CBZ files (or CBR, PDF) or folders of pictures in a Manga folder on your card, or add series from Browse.", nullptr);
			ui.drawHints({{"B", "Back"}});
			return;
		}
		int rows = (n + MG_COLS - 1) / MG_COLS;
		if (mlibGrid >= n) mlibGrid = n - 1;
		if (mlibScroll >= rows) mlibScroll = rows - 1;
		// the grid, a row at a time: the next row peeks under this one, cut at the mini bar
		SDL_Rect clip{0, (int)std::lround(MG_LIB_TOP * sy), (int)std::lround(W * sx),
			(int)std::lround((MG_LIB_BOTTOM - MG_LIB_TOP) * sy)};
		SDL_RenderSetClipRect(ui.ren, &clip);
		for (int i = mlibScroll * MG_COLS; i < n; i++)
		{
			Rect r = mangaCoverRect(i);
			if (r.y >= MG_LIB_BOTTOM) break;
			bool at = mlibGrid == i;
			drawMangaCover(r, mangaLib[i].title, at, 4, mangaLib[i].id, mangaSeriesCoverUrl(mangaLib[i].id));
			ui.drawTextMid(mangaLib[i].title, 0, at ? ACCENT : TEXT, r.x + r.w / 2, r.y + r.h + 13, r.w, 2);
			int unread = i < (int)mangaLibUnread.size() ? mangaLibUnread[i] : 0;
			if (unread > 0)
			{
				// its unread count, a chip at the cover's top right
				std::string t = std::to_string(unread);
				int tw = (int)std::lround(ui.textWidth(t, 0) / sx) + 14;
				ui.fillRound({r.x + r.w - tw - 4, r.y + 4, tw, 20}, ACCENT, R_CHIP);
				ui.drawTextMid(t, 0, ON_ACCENT, r.x + r.w - tw / 2 - 4, r.y + 14, tw - 6, 2);
			}
		}
		SDL_RenderSetClipRect(ui.ren, nullptr);
		ui.drawScrollBar(MG_LIB_TOP, MG_LIB_BOTTOM - MG_LIB_TOP, mlibScroll,
			std::max(1, (MG_LIB_BOTTOM - MG_LIB_TOP) / 186), rows);
		ui.drawHints({{"D-PAD", "Move"}, {"A", "Open"}, {"X", "Options"}, {"B", "Back"}});
		if (mlibMenu) drawMangaLibMenu();
	}

	static bool isEpubPath(const std::string& path)
	{
		size_t dot = path.rfind('.');
		if (dot == std::string::npos) return false;
		std::string e = path.substr(dot + 1);
		for (char& c : e) c = (char)tolower((unsigned char)c);
		return e == "epub";
	}

	std::vector<std::string> Ui::Manga::mangaMissing() const
	{
		// a series of the cards ("local:<its folder or file>") the reader can't open now: its folder or file gone
		// (or on a card taken out), or one it no longer reads as manga (a book: an EPUB or a TXT read before they
		// were left to Files). Every one the store knows, in the Library or only in History; the sources' and
		// servers' series are never "missing"
		std::vector<std::string> out;
		if (!mangaStore) return out;
		for (const std::string& id : mangaStore->seriesIds())
		{
			if (id.rfind("local:", 0) != 0) continue;
			std::string path = id.substr(6);
			bool gone;
			if (mangaScanned) gone = !mangaLocalById.count(id);	// what the last scan of the cards found
			else
			{
				struct stat st;
				gone = stat(path.c_str(), &st) != 0 || (S_ISREG(st.st_mode) && (!isPageBookName(path) || isEpubPath(path)));
			}
			if (gone) out.push_back(id);
		}
		return out;
	}

	bool Ui::Manga::mangaLibMenuKey(Button b, Player& p)
	{
		if (b == Button::A) mangaLibMenuAct(p);
		else if (b == Button::B || b == Button::X) { mlibMenu = false; mlibArmed = false; }
		return false;
	}

	void Ui::Manga::mangaLibMenuAct(Player& p)
	{
		(void)p;
		auto gone = mangaMissing();
		if (gone.empty()) { mlibMenu = false; mangaNotice("Nothing is missing", 1500); return; }
		// twice: their places and read marks go with them
		if (!mlibArmed) { mlibArmed = true; return; }
		for (const auto& id : gone) mangaStore->removeSeries(id);
		mangaSave();
		mangaCacheValid = false;
		mlibMenu = false;
		mlibArmed = false;
		mlibGrid = mlibScroll = 0;
		fprintf(stderr, "manga: removed %zu missing series\n", gone.size());
		mangaNotice("Removed " + std::to_string(gone.size()) + (gone.size() == 1 ? " missing series" : " missing series"), 2000);
	}

	void Ui::Manga::drawMangaLibMenu()
	{
		Rect b = mlibMenuBox();
		ui.drawDialog(b, "Library");
		auto gone = mangaMissing();
		Rect r = mlibMenuRow();
		ui.drawFocus(r);
		std::string note = gone.empty() ? "None" : mlibArmed ? "A again" : std::to_string(gone.size()) + " gone";
		ui.drawDialogRow(r, &G_TRASH, "Remove missing", true, false, gone.empty() ? DIM : DANGER, note, mlibArmed ? ACCENT : DIM);
		// (said under the row: what goes with them, and the card that may only be out)
		ui.drawTextMid(gone.empty() ? "Every series is on a card" : "Series no longer on a card, and books Files now opens", 0, DIM, W / 2, 278, 380, 2);
		if (mlibArmed) ui.drawFooterText("Their read marks go too. A card that's out? Put it back first", ACCENT);
		else ui.drawHints({{"A", "Remove"}, {"B", "Close"}});
	}

	bool Ui::Manga::mangaHisMenuKey(Button b, Player& p)
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

	void Ui::Manga::mangaHisMenuAct(Player& p)
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

	void Ui::Manga::drawMangaHisMenu()
	{
		Rect b = mhisMenuBox();
		ui.drawDialog(b, "Clear history");
		static const char* ROWS[MHIS_MENU_ROWS] = {"Selected item", "Last hour", "Today", "Last 24 hours", "All"};
		for (int i = 0; i < MHIS_MENU_ROWS; i++)
		{
			Rect r = mhisMenuRow(i);
			bool at = i == mhisMenuCursor;
			if (at) ui.drawFocus(r);
			ui.drawTextMid(ROWS[i], 1, at ? ACCENT : i == MHIS_MENU_ROWS - 1 ? DANGER : TEXT, r.x + 12, r.y + r.h / 2, r.w - 24);
		}
		ui.drawHints({{"A", "Clear"}, {"B", "Close"}});
	}

	void Ui::Manga::drawMangaHistory(const Player& p)
	{
		mangaCache();
		if (mangaClearAt && SDL_GetTicks() - mangaClearAt >= QUIT_WINDOW_MS) mangaClearAt = 0;
		if (!p.settings.mangaHistory)
		{
			// kept off: where it is turned on, and that the reading itself is still remembered
			ui.drawEmpty(G_CLOCK, "Reading history is off",
				"Turn it on in Manga settings (START). Your places and read\nmarks are kept either way.", nullptr);
			ui.drawHints({{"B", "Back"}});
			return;
		}
		int n = (int)mangaHistory.size();
		if (n == 0)
		{
			ui.drawEmpty(G_CLOCK, "Nothing read yet", "Series you read show up here.", nullptr);
			ui.drawHints({{"B", "Back"}});
			return;
		}
		if (mhisCursor >= n) mhisCursor = n - 1;
		for (int i = mhisScroll; i < n && i < mhisScroll + MG_HIST_ROWS; i++)
		{
			const ChapterProgress& cp = mangaHistory[i];
			int y = MG_ROW_TOP + (i - mhisScroll) * MG_HIST_ROW_H, mid = y + (MG_HIST_ROW_H - 4) / 2;
			bool at = i == mhisCursor;
			if (at) ui.drawFocus({16, y, 608, MG_HIST_ROW_H - 4});
			SDL_Color ink = at ? ACCENT : TEXT, soft = at ? DIM_ON : DIM;
			// one row a series: its cover, its title, and the chapter it was last at
			std::string title = mangaTitle(cp.seriesId);
			drawMangaCover({28, mid - 23, 32, 46}, title, false, 1, cp.seriesId, mangaSeriesCoverUrl(cp.seriesId));
			ui.drawTextMid(title, 1, ink, 72, mid - 10, 422);
			{
				std::string from = mangaSourceLabel(cp.seriesId);
				ui.drawTextMid((from.empty() ? "" : from + " \xC2\xB7 ") + chapterShort(cp.chapterTitle) + " \xC2\xB7 " + chapterPageLine(cp.page, cp.pages),
					0, soft, 72, mid + 10, 422);
			}
			// how far into that chapter, a thin bar under the text (ACCENT always: it is the place read)
			double frac = cp.pages > 0 ? std::clamp((cp.page + 1) / (double)cp.pages, 0.0, 1.0) : 0;
			ui.drawLevel({72, y + MG_HIST_ROW_H - 13, 422, 3}, (int)std::lround(422 * frac), TRACK, ACCENT);
			ui.drawTextMid(mangaAgo(cp.at, (int64_t)time(nullptr)), 0, soft, 604, mid, 120, 1);
		}
		ui.drawScrollBar(MG_ROW_TOP, MG_HIST_ROWS * MG_HIST_ROW_H - 4, mhisScroll, MG_HIST_ROWS, n);
		if (mangaClearAt) ui.drawFooterText("Press X again to clear history", ACCENT);
		else ui.drawHints({{"A", "Continue"}, {"X", "Clear\xE2\x80\xA6"}, {"B", "Back"}});
		if (mhisMenu) drawMangaHisMenu();
	}

	void Ui::Manga::drawMangaBrowse(const Player& p)
	{
		mangaCache();
		int n = (int)mangaLocal.size();
		if (mBrowseList)
		{
			if (n == 0 && mangaScanning)
			{
				ui.drawEmpty(G_FOLDER, mangaReadingLine(), "The series on your card show up here in a moment.", nullptr);
				ui.drawHints({{"B", "Back"}});
				return;
			}
			if (n == 0)
			{
				ui.drawEmpty(G_FOLDER, "No series found", "Copy manga into " + mangaLocalFolder() + ". X shows how.", nullptr);
				ui.drawHints({{"X", "How to add"}, {"B", "Back"}});
				if (mlocSheet == MLS_HELP) drawMangaLocalSheet(p);
				return;
			}
			if (mListCursor >= n) mListCursor = n - 1;
			for (int i = mListScroll; i < n && i < mListScroll + MG_ROWS; i++)
			{
				const LocalSeries& s = mangaLocal[i];
				int y = MG_ROW_TOP + (i - mListScroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
				bool at = i == mListCursor;
				if (at) ui.drawFocus({16, y, 608, ROW_H - 4});
				// what its chapters are: comics (CBZ, CBR), PDFs, or folders of pictures
				const Glyph* g = &G_PICTURE;
				if (!s.chapters.empty() && s.chapters[0].archive) g = isPdfName(s.chapters[0].path) ? &G_PDF : &G_COMIC;
				ui.drawGlyphMid(*g, 44, mid, at ? ACCENT : DIM);
				ui.drawTextMid(s.title, 1, at ? ACCENT : TEXT, 64, mid, 440);
				ui.drawTextMid(std::to_string(s.chapters.size()) + (s.chapters.size() == 1 ? " chapter" : " chapters"),
					0, at ? DIM_ON : DIM, 608, mid, 160, 1);
			}
			ui.drawScrollBar(MG_ROW_TOP, MG_ROWS * ROW_H - 4, mListScroll, MG_ROWS, n);
			// while the card is read the footer says so (the list may still change); its keys work meanwhile
			if (mangaScanning) ui.drawFooterText(mangaReadingLine(), DIM);
			else ui.drawHints({{"A", "Open"}, {"Y", "Options"}, {"X", "How to add"}, {"B", "Back"}});
			if (mlocSheet != MLS_NONE) drawMangaLocalSheet(p);
			return;
		}
		// On this handheld, then the installed sources (and, still to come, the network's own)
		drawMangaBrowseRows(p);
	}

	void Ui::Manga::drawMangaSeries(const Player& p)
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
		ui.drawHeader(title, p);
		if (!s)
		{
			ui.drawEmpty(G_BOOK, "Not on this card", "The card was taken out, or the series moved.", nullptr);
			ui.drawHints({{"B", "Back"}});
			return;
		}
		int n = (int)s->chapters.size();
		std::vector<std::string> ids;
		ids.reserve(n);
		for (auto& c : s->chapters) ids.push_back(c.path);
		int unread = mangaStore ? mangaStore->unread(mangaSeriesId, ids) : 0;
		// its chapter files still being read (their order may change): said, and asked for if nothing asked yet
		bool filling = !seriesKnown(*s);
		if (filling) mangaLocalWantFill(mangaSeriesId);
		ui.drawTextMid(std::to_string(n) + (n == 1 ? " chapter" : " chapters") + " \xC2\xB7 " + std::to_string(unread) + " unread" +
			(filling ? " \xC2\xB7 Reading the chapters\xE2\x80\xA6" : ""), 0, DIM, 16, 68);
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
		ui.drawBox(rb, mangaPressed == MG_RESUME ? PRESSED : onResume ? BUTTON : SURFACE, R_BUTTON, onResume);
		ui.drawButton(rb, &G_PLAY, n ? "Resume" : "No chapters", nullptr, n == 0, 1);	// (A: in the hints)
		Rect hb = mangaHeartRect();
		bool inLib = ms && ms->inLibrary;
		ui.drawBox(hb, mangaPressed == MG_HEART ? PRESSED : BUTTON, R_BUTTON);
		ui.drawGlyphMid(inLib ? G_HEART : G_HEART_O, hb.x + hb.w / 2, hb.y + 14, inLib ? ACCENT : DIM, 2);
		ui.drawTextMid("Y", 0, ACCENT, hb.x + hb.w / 2, hb.y + 35, 0, 2);
		if (n == 0) { ui.drawHints({{"B", "Back"}}); return; }
		if (mserCursor >= n) mserCursor = n - 1;
		for (int i = mserScroll; i < n && i < mserScroll + MG_SER_ROWS; i++)
		{
			const LocalChapter& c = s->chapters[i];
			int y = MG_SER_TOP + (i - mserScroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
			bool at = mserCursor == i;
			if (at) ui.drawFocus({16, y, 608, ROW_H - 4});
			// the one Resume would open: a small ACCENT dot
			if (resume.chapter == i) ui.fillRound({28, mid - 4, 8, 8}, ACCENT, 4);
			const ChapterProgress* pr = mangaStore ? mangaStore->progress(c.path) : nullptr;
			bool read = pr && pr->read;
			ui.drawTextMid(c.title, 1, read ? DIM : at ? ACCENT : TEXT, 48, mid, 470);
			if (read) ui.drawGlyphMid(G_CHECK, 596, mid, DIM);
			else if (pr && (pr->pages > 0 || pr->page > 0))
				ui.drawTextMid(chapterPageLine(pr->page, pr->pages), 0, at ? DIM_ON : DIM, 608, mid, 150, 1);
		}
		ui.drawScrollBar(MG_SER_TOP, MG_SER_ROWS * ROW_H - 4, mserScroll, MG_SER_ROWS, n);
		ui.drawHints({{"A", "Read"}, {"X", "Read / unread"}, {"Y", "In library"}, {"B", "Back"}});
	}

	void Ui::Manga::drawMangaRead(const Player& p)
	{
		// a source's next chapter, while this one reads on the top panel: its pages arrive here
		mangaBrowsePoll(p);
		mangaErrCheck(ui.pageReader->readerStatus.error, false, 1);	// a page behind a browser check: the same dialog
		if (mangaReadWaiting && ui.page == Page::MangaRead)
		{
			drawMangaReadWaiting();
			return;
		}
		if (ui.page != Page::MangaRead) { drawMangaSeries(p); return; }
		// main closed the reader (an error, or it stopped): back to the series page. Not before it has opened the
		// request (open false then isn't a close)
		if (ui.pageReader->readerStatus.open) mangaReadLive = true;
		else if (mangaReadLive)
		{
			mangaRecordProgress();
			mangaSave();
			ui.page = Page::MangaSeries;
			mangaMenu = false;
			ui.pageReader->mangaZoomAsked = 100;
			mangaReadLive = false;
			mangaProgPage = mangaProgPages = -1;
			drawMangaSeries(p);
			return;
		}
		// (the pages grid comes over Fullscreen and Book too: it is drawn below, never under the pages)
		if (!ui.pageReader->mangaPages && (ui.pageReader->readerStatus.fullscreen || ui.pageReader->readerStatus.book) && ui.pageReader->readerBottom.on)
		{
			ui.pageReader->drawMangaFull();
			if (mangaMenu) drawMangaReadingMenu(p);	// over the view's lower part, as any sheet there
			return;
		}
		// the pages grid over the reader: this panel shows thumbnails, the top panel shows the page under the cursor
		if (ui.pageReader->mangaPages)
		{
			// the reader closed itself (an error, or main stopped it): close the grid and go to the series page
			if (!ui.pageReader->readerStatus.open && mangaReadLive)
			{
				mangaRecordProgress();
				mangaSave();
				ui.pageReader->mangaPages = false;
				ui.pageReader->mpFree();
				ui.page = Page::MangaSeries;
				mangaReadLive = false;
				mangaProgPage = mangaProgPages = -1;
				drawMangaSeries(p);
				return;
			}
			ui.pageReader->drawMangaPages(p);
			return;
		}
		// out of fullscreen: this panel's copies of the pages, its controls and the first open's toast go
		mangaCtrlUntil = mangaToastUntil = 0;
		if (!ui.pageReader->mfTex.empty()) ui.pageReader->mfFree();
		ui.drawBackIcon();
		ui.drawTextMid(ui.pageReader->mangaReadChapterTitle, 1, TEXT, 72, 24, 540);
		ui.drawTextMid(mangaTitle(ui.pageReader->mangaReadSeriesId), 0, DIM, 72, 50, 540);
		// the big surface: it shows the pages' place and takes the finger's drag
		ui.drawBox(mangaAreaRect(), SURFACE, R_TILE);
		static const char* MODES[MANGA_MODES] = {"Right to left", "Left to right", "Vertical scroll"};
		int m = ui.pageReader->readerStatus.mode >= 0 && ui.pageReader->readerStatus.mode < MANGA_MODES ? ui.pageReader->readerStatus.mode : 0;
		if (ui.pageReader->readerStatus.failedHere) ui.pageReader->drawMangaRetry(false);
		else
		{
			ui.drawTextMid(m == MANGA_STRIP ? "Drag here to scroll" : "Swipe here to turn the page", 0, DIM, W / 2, 190, 0, 2);
			ui.drawTextMid(std::string(MODES[m]) + " \xC2\xB7 X to change", 0, DIM_ON, W / 2, 216, 0, 2);
		}
		int pages = ui.pageReader->readerStatus.pages;
		if (!ui.pageReader->readerStatus.error.empty())
			ui.drawTextMid(ui.pageReader->readerStatus.error, 1, DANGER, W / 2, ui.miniUp ? 310 : 340, 560, 2);
		else
		{
			ui.drawTextMid(pages > 0 ? "Page " + std::to_string((ui.slideHeld(SL_MANGA) ? (int)ui.slide.value : ui.pageReader->readerStatus.page) + 1) + " / " + std::to_string(pages) : "Page",
				1, TEXT, W / 2, ui.miniUp ? 310 : 340, 380, 2);
			if (ui.pageReader->readerStatus.loading)
				ui.drawTextMid("Loading" + std::string(SDL_GetTicks() / 400 % 4, '.'), 0, DIM, W / 2 + 150, ui.miniUp ? 312 : 342, 0, 2);
		}
		// the slider: the ACCENT fill to the page, its knob; a finger there is a Seek (mangaSlide)
		drawMangaSlider();
		// its chapters' ends, and where it goes next
		int ci = mangaReadChapterIndex();
		bool prev = ci > 0, next = ci >= 0 && ci + 1 < mangaReadChapterTotal();
		if (ui.pageReader->readerStatus.atEnd)
			ui.drawTextMid(next ? std::string("End of chapter \xC2\xB7 R2 or ") + (p.settings.mangaSwapLR ? "LEFT" : "RIGHT") + " for the next"
				: "You\xE2\x80\x99re caught up", 0, next ? ACCENT : DIM, W / 2, ui.miniUp ? 360 : 384, 560, 2);
		Rect pb = mangaPrevRect(), fb = mangaFullRect(), nb = mangaNextRect();
		ui.drawBox(pb, mangaPressed == MG_PREV ? PRESSED : BUTTON, R_BUTTON);
		// each with its key under it (Button hotkeys on), as the player's buttons
		bool keys = p.settings.buttonHotkeys;
		ui.drawButton(pb, &G_PREV, "Prev chapter", keys ? "L2" : nullptr, !prev, 0);
		// between them, as big as they are: fullscreen, exactly as SELECT turns it on
		ui.drawBox(fb, mangaPressed == MG_FULL ? PRESSED : BUTTON, R_BUTTON);
		ui.drawButton(fb, nullptr, "Fullscreen", keys ? "SELECT" : nullptr, false, 0);
		Rect lb = mangaLightRect();
		ui.drawBox(lb, mangaPressed == MG_LIGHT ? PRESSED : BUTTON, R_BUTTON);
		std::string lightOn = ui.lightNote(p);	// (while Extra dim or a filter is on: lit, what's on under it)
		ui.drawButton(lb, &G_SUN, "Brightness", lightOn.empty() ? nullptr : lightOn.c_str(), false, 0, !lightOn.empty());
		ui.drawBox(nb, mangaPressed == MG_NEXT ? PRESSED : BUTTON, R_BUTTON);
		ui.drawButton(nb, &G_NEXT, "Next chapter", keys ? "R2" : nullptr, !next, 0);
		// what the keys do: the useful ones, one line (the Reading menu, START, lists the rest); the fullscreen
		// button is on the row above, so SELECT needn't be named
		ui.drawHints({{"UP/DOWN", "Zoom"}, {"L/R", "Page"}, {"START", "Menu"}, {"B", "Close"}});
		if (mangaMenu) drawMangaReadingMenu(p);
	}

	int Ui::Manga::mangaModeFor(const std::string& seriesId, const Player& p) const
	{
		const MangaSeries* ms = mangaStore ? mangaStore->series(seriesId) : nullptr;
		int mode = 0, book = 0;
		if (ms && parseMangaReadingMode(ms->readingMode, mode, book)) return mode;
		int m = p.settings.mangaMode;
		return m >= 0 && m < MANGA_MODES ? m : MANGA_RTL;
	}

	int Ui::Manga::mangaBookFor(const std::string& seriesId) const
	{
		const MangaSeries* ms = mangaStore ? mangaStore->series(seriesId) : nullptr;
		int mode = 0, book = 0;
		if (ms && parseMangaReadingMode(ms->readingMode, mode, book)) return book;
		return MANGA_BOOK_OFF;
	}

	void Ui::Manga::mangaSetBook(int book, Player& p)
	{
		(void)p;
		if (book < 0 || book >= MANGA_BOOKS || book == ui.pageReader->readerStatus.book) return;
		Uint32 now = SDL_GetTicks();
		ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Book, (double)book, (int64_t)now});
		if (book) mangaHoldLast = book;	// (the View row turns Book on held the same way next time)
		// a Strip opens Book right to left (the reader does the same): the series keeps both
		int mode = ui.pageReader->readerStatus.mode == MANGA_STRIP && book ? MANGA_RTL : ui.pageReader->readerStatus.mode;
		if (mangaStore)
			if (const MangaSeries* ms = mangaStore->series(ui.pageReader->mangaReadSeriesId))
			{
				MangaSeries s = *ms;
				s.readingMode = mangaReadingModeId(mode, book);
				mangaStore->putSeries(s);
				mangaSave();
			}
		// no "how to hold it" note on entering Book mode (the owner's call): the View row already says it
		mangaBookToast = 0;
		mangaToastUntil = 0;
		mangaCtrlUntil = 0;
		ui.pageReader->mfTagUntil = book ? now + 3000 : 0;
		ui.pageReader->mangaZoomAsked = 100;
	}

	void Ui::Manga::mangaBookTurn(int step, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		if (step > 0 && ui.pageReader->readerStatus.atEnd) { mangaChapterStep(1, p); return; }	// the last spread: the next chapter
		if (step != 0) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Step, (double)step, (int64_t)now});
		ui.pageReader->mfTagUntil = now + 1500;
	}

	void Ui::Manga::stickAxis(int value, Player& p)
	{
		bool reading = ui.page == Page::MangaRead && ui.pageReader->readerStatus.open && !mangaMenu &&
			!ui.pageReader->mangaPages && !mangaReadWaiting && ui.pageReader->readerStatus.error.empty();
		// Book is held sideways, whole pages: one push is one spread, turned the way its D-pad points
		if (reading && ui.pageReader->readerStatus.book)
		{
			if (stickSent) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Stick, 0, (int64_t)SDL_GetTicks()});
			stickSent = false;
			double speed = stickScrollSpeed(value, 1.0);
			int dy = speed == 0 ? 0 : speed > 0 ? 1 : -1;
			if (dy != 0 && dy != stickDir)
			{
				int book = ui.pageReader->readerStatus.book;
				mangaBookTurn(mangaBookStep(book, ui.pageReader->readerStatus.mode == MANGA_RTL, p.settings.mangaSwapLR, dy), p);
			}
			stickDir = dy;
			return;
		}
		stickDir = 0;
		if (!reading)
		{
			// the reader left, or a menu took the keys: a held stick stops at once
			if (stickSent) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Stick, 0, (int64_t)SDL_GetTicks()});
			stickSent = false;
			return;
		}
		if (value != 0)
		{
			// the reader scrolls the Strip, or pans a paged page with room (issue 16); every pass, so a chapter
			// opened under a held stick starts at once (the same value changes nothing)
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Stick, (double)value, (int64_t)SDL_GetTicks()});
			stickSent = true;
		}
		else if (stickSent)
		{
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Stick, 0, (int64_t)SDL_GetTicks()});
			stickSent = false;
		}
	}

	bool Ui::Manga::mangaBookKey(Button b, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		bool rtl = ui.pageReader->readerStatus.mode == MANGA_RTL;
		// UP and DOWN point along the spread as it is held; LEFT and RIGHT across it, where there is nothing to do
		if (b == Button::Up || b == Button::Down)
		{
			mangaBookTurn(mangaBookStep(ui.pageReader->readerStatus.book, rtl, p.settings.mangaSwapLR, b == Button::Up ? -1 : 1), p);
			return false;
		}
		switch (b)
		{
		// one page, to realign a spread (a cover alone, a two-page picture split over the hinge)
		case Button::L: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Page, -1, (int64_t)now}); ui.pageReader->mfTagUntil = now + 1500; break;
		case Button::R: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Page, 1, (int64_t)now}); ui.pageReader->mfTagUntil = now + 1500; break;
		case Button::A: if (ui.pageReader->readerStatus.failedHere) ui.pageReader->mangaRetry(); break;
		// the two directions only: Book reads a page at a time
		case Button::X: mangaSetMode(rtl ? MANGA_LTR : MANGA_RTL, p); ui.pageReader->mfTagUntil = now + 1500; break;
		case Button::L2: mangaChapterStep(-1, p); break;
		case Button::R2: mangaChapterStep(1, p); break;
		// SELECT: fullscreen, the usual way up (Book goes)
		case Button::Select:
			mangaSetBook(MANGA_BOOK_OFF, p);
			if (!ui.pageReader->readerStatus.fullscreen) mangaFullscreen(true, p);
			break;
		case Button::Start: mangaOpenReadingMenu(); break;
		case Button::Y: ui.pageReader->mangaPagesOpen(p); break;
		// B goes back to the chapters; Book stays the series' way to read (the menu's View leaves it)
		case Button::B: mangaCloseReader(); break;
		default: break;
		}
		return false;
	}

	void Ui::Manga::mangaCycleMode(Player& p)
	{
		// right to left, left to right, strip, round
		mangaSetMode((ui.pageReader->readerStatus.mode + 1) % MANGA_MODES, p);
	}

	void Ui::Manga::mangaSetMode(int m, Player& p)
	{
		// kept for this series (the setting stays for the others)
		(void)p;
		if (m < 0 || m >= MANGA_MODES || m == ui.pageReader->readerStatus.mode) return;
		ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Mode, (double)m, (int64_t)SDL_GetTicks()});
		if (mangaStore)
			if (const MangaSeries* ms = mangaStore->series(ui.pageReader->mangaReadSeriesId))
			{
				MangaSeries s = *ms;
				s.readingMode = mangaReadingModeId(m, ui.pageReader->readerStatus.book);	// (Book kept: X switches the direction in it)
				mangaStore->putSeries(s);
				mangaSave();
			}
	}

	void Ui::Manga::mangaOpenReadingMenu()
	{
		// over the page it is reading: nothing behind it moves, so closing it reads on exactly where it was
		mangaMenu = true;
		mangaMenuCursor = MGM_VIEW;
		mangaViewAsked = -1;
		mangaHoldAsked = 0;
	}

	int Ui::Manga::mangaViewNow() const
	{
		// what was just asked, until the reader says it (its status comes a pass behind)
		if (mangaViewAsked >= 0) return mangaViewAsked;
		return ui.pageReader->readerStatus.book ? MV_BOOK : ui.pageReader->readerStatus.fullscreen ? MV_FULL : MV_ONE;
	}

	int Ui::Manga::mangaHoldNow() const
	{
		if (mangaHoldAsked) return mangaHoldAsked;
		return ui.pageReader->readerStatus.book ? ui.pageReader->readerStatus.book : mangaHoldLast;
	}

	std::vector<int> Ui::Manga::mangaMenuRows() const
	{
		// only what means something for the view and the mode: Hold in Book, the hinge in a fullscreen Strip (where
		// a picture runs on across the fold), no fit for the Strip (it always fits the width), no fit or zoom in Book
		// (a whole page on each screen)
		int v = mangaViewNow();
		std::vector<int> r{MGM_VIEW};
		if (v == MV_BOOK) r.push_back(MGM_HOLD);
		r.push_back(MGM_MODE);
		if (v != MV_BOOK)
		{
			if (ui.pageReader->readerStatus.mode != MANGA_STRIP) r.push_back(MGM_FIT);
			r.push_back(MGM_ZOOM);
		}
		if (v == MV_FULL && ui.pageReader->readerStatus.mode == MANGA_STRIP) r.push_back(MGM_HINGE);
		r.push_back(MGM_SWAP);
		r.push_back(MGM_LIGHT);
		r.push_back(MGM_PAGES);
		r.push_back(MGM_SETTINGS);
		return r;
	}

	void Ui::Manga::mangaSetView(int v, Player& p)
	{
		int cur = mangaViewNow();
		if (v < 0 || v >= MV_COUNT || v == cur) return;
		if (cur == MV_BOOK) mangaSetBook(MANGA_BOOK_OFF, p);
		if (v == MV_BOOK)
		{
			// Book stands alone: fullscreen goes (leaving Book later comes back to one screen)
			if (ui.pageReader->readerStatus.fullscreen) mangaFullscreen(false, p);
			mangaSetBook(mangaHoldNow(), p);
		}
		else mangaFullscreen(v == MV_FULL, p);
		// one screen or fullscreen is how the next chapter opens too (Book is the series' own)
		if (v != MV_BOOK && p.settings.mangaOpenFull != (v == MV_FULL))
		{
			Settings s = p.settings;
			s.mangaOpenFull = v == MV_FULL;
			p.setSettings(s);
		}
		mangaViewAsked = v;
	}

	bool Ui::Manga::mangaMenuKey(Button b, Player& p)
	{
		auto rows = mangaMenuRows();
		int n = (int)rows.size();
		int i = (int)(std::find(rows.begin(), rows.end(), mangaMenuCursor) - rows.begin());
		if (i >= n) i = 0;	// (its row went with the view: back to the first)
		switch (b)
		{
		case Button::Up: mangaMenuCursor = rows[(i + n - 1) % n]; break;
		case Button::Down: mangaMenuCursor = rows[(i + 1) % n]; break;
		case Button::Left: mangaMenuCursor = rows[i]; mangaMenuStep(-1, p); break;
		case Button::Right: mangaMenuCursor = rows[i]; mangaMenuStep(1, p); break;
		case Button::A: mangaMenuCursor = rows[i]; mangaMenuAct(p); break;
		case Button::B:
		case Button::Start: mangaMenu = false; mangaViewAsked = -1; mangaHoldAsked = 0; break;
		default: break;
		}
		return false;
	}

	void Ui::Manga::mangaMenuStep(int dir, Player& p)
	{
		bool book = mangaViewNow() == MV_BOOK;
		switch (mangaMenuCursor)
		{
		// One screen, Fullscreen, Book, round
		case MGM_VIEW: mangaSetView((mangaViewNow() + dir + MV_COUNT) % MV_COUNT, p); break;
		// the top screen on the left or the right as held
		case MGM_HOLD:
		{
			int h = mangaHoldNow() == MANGA_BOOK_LEFT ? MANGA_BOOK_RIGHT : MANGA_BOOK_LEFT;
			mangaSetBook(h, p);
			mangaHoldAsked = h;
			break;
		}
		// (Book reads a page at a time: its two directions only)
		case MGM_MODE:
			if (book) mangaSetMode(ui.pageReader->readerStatus.mode == MANGA_LTR ? MANGA_RTL : MANGA_LTR, p);
			else mangaSetMode((ui.pageReader->readerStatus.mode + dir + MANGA_MODES) % MANGA_MODES, p);
			break;
		// the setting itself, so the next chapter opens the same way
		case MGM_FIT: ui.settingsPage->changeSetting(settingRow("manga-fit"), dir, p); break;
		case MGM_ZOOM: ui.pageReader->mangaZoomStep(dir); break;
		case MGM_HINGE: ui.settingsPage->changeSetting(settingRow("manga-hinge"), dir, p); break;
		case MGM_SWAP: ui.settingsPage->changeSetting(settingRow("manga-swap-lr"), dir, p); break;	// Swap Left/Right
		default: break;	// Pages and All Manga settings: nothing to step
		}
	}

	void Ui::Manga::mangaMenuAct(Player& p)
	{
		switch (mangaMenuCursor)
		{
		case MGM_ZOOM: ui.pageReader->mangaZoomToggle(); break;
		case MGM_PAGES:
			// the pages grid; the menu closes, Y or B in the grid comes back to reading
			mangaMenu = false;
			ui.pageReader->mangaPagesOpen(p);
			break;
		case MGM_LIGHT:
			mangaMenu = false;
			ui.openLight();
			break;
		case MGM_SETTINGS:
			// the app's own settings page; B there comes back here, the menu closed
			mangaMenu = false;
			openMangaSettings();
			break;
		default: mangaMenuStep(1, p); break;	// a value: A steps it on, as RIGHT does
		}
	}

	void Ui::Manga::drawMangaReadingMenu(const Player& p)
	{
		// the reader caught up with what was asked: its own status from here
		if (mangaViewAsked >= 0 && mangaViewAsked == (ui.pageReader->readerStatus.book ? MV_BOOK : ui.pageReader->readerStatus.fullscreen ? MV_FULL : MV_ONE))
			mangaViewAsked = -1;
		if (mangaHoldAsked && mangaHoldAsked == ui.pageReader->readerStatus.book) mangaHoldAsked = 0;
		// a compact sheet over the page: its title and how to leave it, then a row per thing read with
		Rect sheet = mangaMenuRect();
		ui.fillRound(sheet, SURFACE, R_DIALOG, ROUND_TL | ROUND_TR);
		ui.drawTextMid("Reading", 1, TEXT, 24, 150, 300);
		ui.drawTextMid("START \xC2\xB7 B Close", 0, DIM, W - 24, 150, 300, 1);
		static const char* VIEWS[MV_COUNT] = {"One screen", "Fullscreen", "Book"};
		static const char* HOLDS[MANGA_BOOKS] = {"", "Top screen left", "Top screen right"};
		static const char* MODES[MANGA_MODES] = {"Right to left", "Left to right", "Vertical scroll"};
		static const char* FITS[MANGA_FITS] = {"Page", "Width", "Fill"};
		int view = mangaViewNow();
		bool book = view == MV_BOOK;
		int mode = ui.pageReader->readerStatus.mode >= 0 && ui.pageReader->readerStatus.mode < MANGA_MODES ? ui.pageReader->readerStatus.mode : 0;
		if (book && mode == MANGA_STRIP) mode = MANGA_RTL;	// (Book opens a Strip right to left; the status follows)
		int fit = p.settings.mangaFit >= 0 && p.settings.mangaFit < MANGA_FITS ? p.settings.mangaFit : 0;
		int zoom = ui.pageReader->readerStatus.zoomPct > 0 ? ui.pageReader->readerStatus.zoomPct : 100;
		static const char* LABELS[MGM_COUNT] = {"View", "Hold", "Reading mode", "Page fit", "Zoom", "Hinge gap",
			"Swap Left/Right", "Brightness & filter\xE2\x80\xA6", "Pages", "All Manga settings\xE2\x80\xA6"};
		auto rows = mangaMenuRows();
		if (std::find(rows.begin(), rows.end(), mangaMenuCursor) == rows.end()) mangaMenuCursor = MGM_VIEW;
		for (int i = 0; i < (int)rows.size(); i++)
		{
			int id = rows[i];
			Rect r = mangaMenuRowRect(i);
			bool at = id == mangaMenuCursor;
			if (at) ui.drawFocus(r);
			int mid = r.y + r.h / 2;
			SDL_Color ink = at ? ACCENT : TEXT;
			ui.drawTextMid(LABELS[id], 1, ink, 32, mid, 330);
			// the last two rows are doors (Pages, All Manga settings), not values: a chevron, no step arrows
			if (id == MGM_PAGES || id == MGM_SETTINGS || id == MGM_LIGHT)
			{
				ui.drawGlyphMid(G_RIGHT, 604, mid, at ? ACCENT : DIM);
				continue;
			}
			ui.drawTextMid("<", 1, at ? ACCENT : DIM, SET_ARROW_X, mid);
			std::string value;
			switch (id)
			{
			case MGM_VIEW: value = VIEWS[view]; break;
			case MGM_HOLD: value = HOLDS[mangaHoldNow() == MANGA_BOOK_RIGHT ? MANGA_BOOK_RIGHT : MANGA_BOOK_LEFT]; break;
			case MGM_MODE: value = MODES[mode]; break;
			case MGM_FIT: value = FITS[fit]; break;
			case MGM_ZOOM: value = std::to_string(zoom) + " %"; break;
			case MGM_HINGE: value = std::to_string(p.settings.mangaHinge) + " px"; break;
			case MGM_SWAP: value = p.settings.mangaSwapLR ? "On" : "Off"; break;
			default: break;
			}
			ui.drawTextMid(value, 1, ink, 490, mid, 196, 2);
			ui.drawTextMid(">", 1, at ? ACCENT : DIM, 608, mid, 0, 1);
		}
		// what the focused row does, in a line under them
		const char* help = "";
		switch (mangaMenuCursor)
		{
		case MGM_VIEW: help = "One screen, both as one tall view, or Book (held sideways)."; break;
		case MGM_HOLD: help = "Which side the top screen is on as you hold it."; break;
		case MGM_MODE: help = book ? "Which page of a spread comes first; X does the same." : "How this series reads; X does the same."; break;
		case MGM_FIT: help = "Page keeps it whole, Width fills the screen's width."; break;
		case MGM_ZOOM: help = "UP/DOWN step the zoom; A goes twice or back to the fit."; break;
		case MGM_HINGE: help = "The gap skipped under the hinge, so pictures line up."; break;
		case MGM_SWAP: help = book ? "Turns round which way UP and DOWN turn the spread." : "Off: RIGHT is forward. On: LEFT is forward."; break;
		case MGM_LIGHT: help = "The screens' brightness, darker than its lowest, and a warm filter."; break;
		case MGM_PAGES: help = "The chapter's thumbnails; A jumps to a page."; break;
		case MGM_SETTINGS: help = "The reader's own settings page; B comes back here."; break;
		default: break;
		}
		ui.drawTextMid(help, 0, DIM, 24, 452, 592);
	}

	void Ui::Manga::mangaFullscreen(bool on, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, on ? 1.0 : 0.0, (int64_t)now, (double)p.settings.mangaHinge});
		ui.pageReader->mfTagUntil = on ? now + 2500 : 0;
		mangaBookToast = 0;
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

	void Ui::Manga::drawMangaBookTag()
	{
		int a = ui.pageReader->readerStatus.page + 1, n = ui.pageReader->readerStatus.pages, b = std::min(a + 1, n);
		std::string tag = (a == b ? std::to_string(a) : std::to_string(a) + "\xE2\x80\x93" + std::to_string(b)) + " / " + std::to_string(n);
		if (ui.pageReader->readerStatus.atEnd)
		{
			int ci = mangaReadChapterIndex();
			bool next = ci >= 0 && ci + 1 < mangaReadChapterTotal();
			tag += next ? " \xC2\xB7 End of chapter: turn once more for the next" : " \xC2\xB7 You\xE2\x80\x99re caught up";
		}
		SDL_Texture* t = ui.text(tag, 0, TEXT);
		if (!t) return;
		int tw, th;
		SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
		// the chip, in units: turned, the text's length runs down the panel
		int len = (int)std::lround(tw / sy) + 16, thick = (int)std::lround(th / sx) + 8;
		bool left = ui.pageReader->readerStatus.book != MANGA_BOOK_RIGHT;
		int cx = left ? 12 + thick / 2 : W - 12 - thick / 2, cy = H / 2;
		ui.fillRound({cx - thick / 2, cy - len / 2, thick, len}, SURFACE, R_CHIP);
		// the text upright as held: a quarter turn clockwise held top-left (the panel's right edge is up), back held top-right
		SDL_Rect dst{(int)std::lround(cx * sx) - tw / 2, (int)std::lround(cy * sy) - th / 2, tw, th};
		SDL_RenderCopyEx(ui.ren, t, nullptr, &dst, left ? 90.0 : 270.0, nullptr, SDL_FLIP_NONE);
	}

	void Ui::Manga::drawMangaControls()
	{
		// a sheet at the panel's foot: the series and its page, the page slider, the four keys (its top corners round)
		Rect sheet = mangaSheetRect();
		ui.fillRound(sheet, SURFACE, R_DIALOG, ROUND_TL | ROUND_TR);
		ui.drawTextMid(mangaTitle(ui.pageReader->mangaReadSeriesId), 0, DIM, 20, 318, 380);
		int pages = ui.pageReader->readerStatus.pages;
		ui.drawTextMid(pages > 0 ? "Page " + std::to_string((ui.slideHeld(SL_MANGA) ? (int)ui.slide.value : ui.pageReader->readerStatus.page) + 1) + " of " + std::to_string(pages) : "Page", 0, TEXT,
			W - 20, 318, 220, 1);
		// the slider as the reading page draws it: a finger here is a Seek
		drawMangaSlider();
		static const char* LABELS[4] = {"< Chapter", "Chapter >", "Leave fullscreen", "Close"};
		static const char* KEYS[4] = {"L2", "R2", "SELECT", "B"};
		for (int i = 0; i < 4; i++)
		{
			Rect b = mangaCtrlBtnRect(i);
			ui.drawBox(b, mangaCtrlPressed == i ? PRESSED : BUTTON, R_BUTTON);
			ui.drawButton(b, nullptr, LABELS[i], KEYS[i], false, 0);
		}
		ui.drawTextMid("Hides in 4 s \xC2\xB7 drag to hide now", 0, DIM, W / 2, ui.miniUp ? 468 : 460);
	}

	void Ui::Manga::drawMangaToast()
	{
		// how to leave fullscreen, the first time it opens: a box in the panel's middle for 2 s
		std::string text = "Fullscreen \xC2\xB7 SELECT to leave";
		if (mangaBookToast)
			text = std::string("Book \xC2\xB7 turn the handheld: top screen on the ") +
				(mangaBookToast == MANGA_BOOK_RIGHT ? "right" : "left");
		int tw = (int)std::lround(ui.textWidth(text, 0) / sx) + 32;
		ui.fillRound({W / 2 - tw / 2, H / 2 - 22, tw, 44}, SURFACE, R_BUTTON);
		ui.drawTextMid(text, 0, TEXT, W / 2, H / 2, 0, 2);
	}

}
