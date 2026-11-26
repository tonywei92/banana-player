#include "ui.h"
#include "framewatch.h"
#include "textreader.h"
#include "manganet.h"
#include "ui_internal.h"
#include "ui_settings.h"
#include "ui_reader.h"
#include "ui_kit.h"
#include "ui_shell.h"
#include "ui_manga.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"
#include "pagebook.h"
#include "playlist.h"	// naturalLess: the listing's order kept when titles change
#include "manga_ui_logic.h"
#include "stickscroll.h"
#include <algorithm>
#include <map>
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
			return mangaChapterShort(title);
		}

		// The tile's rounded corners in the cover's own alpha, at upload (the panel's pixels): the generated cover is
		// rounded by fillRound, and the real one is copied over whatever fill the tile sits on (a page's or a card's),
		// so squaring its corners would draw the wrong colour there.
		void roundCover(std::vector<uint8_t>& rgba, int w, int h)
		{
			roundCorners(rgba, w, h, std::max(0, (int)std::lround(R_TILE * std::min(sx, sy))));
		}
	}

	void Ui::Manga::mangaOpen(Player& p)
	{
		Uint32 openAt = SDL_GetTicks();
		mangaReady = true;
		guardStoppedVideo = false;
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
				// (the kit grid's whole rows under the tabs: lay::tabPage's content, lay::coverGrid's pitch)
				lay::Box body = lay::shell().body;
				int h = body.h - lay::TAB_H - lay::SP8;
				int row = i / MG_COLS, shown = std::max(1, (h + lay::SP12) / (lay::COVER_GRID_CELL_H + lay::SP12));
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

	std::string Ui::Manga::mangaReadChapterName(int index) const
	{
		if (index < 0 || index >= mangaReadChapterTotal()) return "";
		if (isServerId(ui.pageReader->mangaReadSeriesId)) return mangaSrvChapters[index].name;
		if (isSourceId(ui.pageReader->mangaReadSeriesId)) return mangaReadSrcChapters[index].name;
		const LocalSeries* s = mangaLocalSeries(ui.pageReader->mangaReadSeriesId);
		return s ? s->chapters[index].title : "";
	}

	std::string Ui::Manga::mangaReadChapterTag() const
	{
		int ci = mangaReadChapterIndex(), total = mangaReadChapterTotal();
		double number = -1;
		const std::string& id = ui.pageReader->mangaReadSeriesId;
		if (ci >= 0 && ci < total)
		{
			if (isServerId(id)) number = mangaSrvChapters[ci].number;
			else if (isSourceId(id)) number = mangaReadSrcChapters[ci].number;
			else if (const LocalSeries* s = mangaLocalSeries(id)) number = s->chapters[ci].number;
		}
		std::string title = ci >= 0 ? mangaReadChapterName(ci) : ui.pageReader->mangaReadChapterTitle;
		return mangaChapterTag(number, title, ci >= 0 ? total : 0);
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
		mangaBookBar = false;
		mangaBookBarPressed = -1;
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
		ui.pageReader->mangaZoomAsked = 100;	// a chapter opens at the fit (the reader resets its zoom too)
		ui.page = Page::MangaRead;
		// Open fullscreen: the command SELECT sends, right after the chapter (main applies both in one pass)
		if (p.settings.mangaOpenFull) mangaFullscreen(true, p);
	}

	void Ui::Manga::mangaChapterStep(int dir, Player& p, int page0)
	{
		// a saved server's chapter steps through its series page's list
		if (isServerId(ui.pageReader->mangaReadSeriesId))
		{
			mangaServerChapterStep(dir, p, page0);
			return;
		}
		// a source's chapter steps through the list it was opened with (its series page holds the same ones)
		if (isSourceId(ui.pageReader->mangaReadSeriesId))
		{
			mangaSourceChapterStep(dir, p, page0);
			return;
		}
		const LocalSeries* s = mangaLocalSeries(ui.pageReader->mangaReadSeriesId);
		int i = s ? chapterIndexOf(*s, ui.pageReader->mangaReadChapterId) : -1;
		if (!s || i < 0) return;
		int j = i + dir;
		if (j < 0 || j >= (int)s->chapters.size()) return;
		mangaOpenChapter(*s, j, page0, p);
	}

	void Ui::Manga::mangaSlide(int x, int y, Player& p)
	{
		// the page slider under a finger: the page at the finger, the reader told at the scrub's pace (a page is a
		// decode, a fetch for a source's) and of the page it's let go on always. In fullscreen it is the controls
		// sheet's: the sheet stays up while it's held, and 4 s more from the last move
		if (ui.pageReader->readerStatus.pages <= 1) return;
		ui.pageReader->mangaSliderPage = -1;
		Rect track = ui.pageReader->readerStatus.fullscreen ? Rect(lay::readerFullNav(true).track) : mangaSliderTrack();
		ui.slideGrab(SL_MANGA, 0, {track, {0, (double)(ui.pageReader->readerStatus.pages - 1), 1}, false, SCRUB_MS, (double)ui.pageReader->readerStatus.page}, x, y, p,
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
		if (pages <= 0) return;
		lay::MangaReadPage l = lay::mangaReadPage();
		bool held = ui.slideHeld(SL_MANGA);
		int at = held ? (int)ui.slide.value : ui.pageReader->readerStatus.page;
		double share = ui.slideShare(SL_MANGA, 0, pages > 1 ? ui.pageReader->readerStatus.page / (double)(pages - 1) : 0);
		ui.drawTextMid(std::to_string(at + 1), F_SMALL, TEXT, l.from.x + l.from.w / 2, l.from.y + l.from.h / 2, l.from.w, 2);
		ui.kit->progress(l.track, share, {}, held, held ? "Page " + std::to_string(at + 1) : "");
		ui.drawTextMid(std::to_string(pages), F_SMALL, DIM, l.to.x + l.to.w / 2, l.to.y + l.to.h / 2, l.to.w, 2);
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
			// back at the first page's start leaves for the chapter before, at its last page (what forward at the end
			// does the other way); in page order, so Swap Left/Right is already in `back`
			else if (back && ui.pageReader->readerStatus.atStart) mangaChapterStep(-1, p, MANGA_LAST_PAGE);
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
			// X: More (this chapter, the series, Manga settings); the reading mode it stepped is a row of View
			case Button::X: mangaOpenReadMore(); break;
			case Button::L2: mangaChapterStep(-1, p); break;
			case Button::R2: mangaChapterStep(1, p); break;
			// SELECT: fullscreen on / off; B leaves fullscreen first, and only then closes the reader
			case Button::Select: mangaFullscreen(!ui.pageReader->readerStatus.fullscreen, p, true); break;
			// START: View, the sheet over the page (over the fullscreen view's lower part), not the settings page
			case Button::Start: mangaOpenView(); break;
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
				if (s && n > 0 && mangaStore)
				{
					std::string sid = mangaSeriesId;
					mangaOpenMarkMore(sid, [this, sid] {
						std::vector<std::string> ids;
						if (const LocalSeries* ls = mangaLocalSeries(sid))
							for (auto& c : ls->chapters) ids.push_back(c.path);
						return ids;
					}, [this] { return mserCursor; });
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
			else openExtList(p);
			return false;
		}
		// L / R step the tabs, round (L2 / R2 are the shell's: the sections)
		if (b == Button::L || b == Button::R)
		{
			mangaTabSet(mangaTab + (b == Button::R ? 1 : -1), p);
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
			// X: the Library's menu (Remove missing, Manga settings), empty or not
			if (b == Button::X) { mlibMenu = true; mlibArmed = false; mlibMenuCursor = 0; mangaLibSheetOpen(); break; }
			if (!n) break;
			if (b == Button::A) { mangaChoose(MT_LIBRARY, mlibGrid, p); break; }
			int row = mlibGrid / MG_COLS, col = mlibGrid % MG_COLS, rows = (n + MG_COLS - 1) / MG_COLS;
			switch (b)
			{
			case Button::Left: if (col > 0) mlibGrid--; break;
			case Button::Right: if (col + 1 < MG_COLS && mlibGrid + 1 < n) mlibGrid++; break;
			// up at the grid's first row does nothing: nothing sits above it any more
			case Button::Up: if (row > 0) mlibGrid -= MG_COLS; break;
			case Button::Down:
				// the row below (its last cover when it is shorter)
				if (row + 1 < rows) mlibGrid = std::min(n - 1, mlibGrid + MG_COLS);
				break;
			default: break;
			}
			{
				// whole rows: the cursor's kept in sight
				int shown = mangaLibGrid(p).shown, at = mlibGrid / MG_COLS;
				mlibScroll = std::clamp(mlibScroll, std::max(0, at - shown + 1), at);
			}
			break;
		}
		case MT_HISTORY:
		{
			mangaCache();
			// off: the list is empty by design (the History tab says so), so nothing there is chosen or cleared; X is
			// where it is turned on (Manga settings)
			if (!p.settings.mangaHistory)
			{
				if (b == Button::X) openMangaSettings();
				break;
			}
			int n = (int)mangaHistory.size();
			if (b == Button::X)
			{
				// what to clear: a menu (the last hour, today, the last 24 hours, all)
				mhisMenu = true;
				mhisMenuCursor = 0;
				mangaHisSheetOpen();
				break;
			}
			if (b == Button::A) { mangaChoose(MT_HISTORY, mhisCursor, p); break; }
			if (!n) break;
			// Y: the series' page (its chapters), not the page read last
			if (b == Button::Y)
			{
				if (mhisCursor < 0 || mhisCursor >= n) break;
				const ChapterProgress& cp = mangaHistory[mhisCursor];
				std::string gone = mangaHisGone(cp);
				if (!gone.empty()) { mangaNotice(gone == "source removed" ? "Its source isn\xE2\x80\x99t installed now" : "It\xE2\x80\x99s not on a card now", 2000); break; }
				if (isServerId(cp.seriesId)) mangaServerSeriesOpenFromId(cp.seriesId, p);
				else openMangaSeries(cp.seriesId, 2, p);
				break;
			}
			switch (b)
			{
			case Button::Up: mhisCursor = (mhisCursor + n - 1) % n; break;
			case Button::Down: mhisCursor = (mhisCursor + 1) % n; break;
			case Button::Left: mhisCursor = std::max(0, mhisCursor - mangaHisList(p).shown); break;
			case Button::Right: mhisCursor = std::min(n - 1, mhisCursor + mangaHisList(p).shown); break;
			default: break;
			}
			{
				int shown = std::max(1, mangaHisList(p).shown);
				if (mhisCursor < mhisScroll) mhisScroll = mhisCursor;
				if (mhisCursor >= mhisScroll + shown) mhisScroll = mhisCursor - shown + 1;
			}
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
			// the drag area and the slider follow the finger; the buttons act as it lifts
			Uint32 now = SDL_GetTicks();
			// Book: the panel is a page; a swipe along the spread (up or down the panel) turns it, a tap shows the
			// page numbers and the Book bar, a double-tap zooms to 200 % at the finger or back to the fit. Zoomed, a
			// drag pans the picture of both panels together (no turn: the turn keys pan to the end first)
			if (ui.pageReader->readerStatus.book)
			{
				bool zoomed = ui.pageReader->readerStatus.zoomPct > 100;
				// a second tap soon after the first, near it: the double-tap, even with the bar its first tap raised
				if (pt.kind == Pointer::Down && now - mangaLastTapAt <= 300 && std::abs(x - mangaLastTapX) <= 20 &&
					std::abs(y - mangaLastTapY) <= 20)
				{
					mangaBookBar = false;
					mangaBookBarPressed = -1;
					mangaBookDouble = true;
				}
				// the sideways bar, when it shows, takes the touches first
				else if (mangaBookBar && !mangaTouch) return mangaBookBarPointer(pt, x, y, p);
				if (pt.kind == Pointer::Down)
				{
					if (!mangaBookDouble && ui.pageReader->readerStatus.failedHere && ui.pageReader->mangaRetryRect(true).hit(x, y)) { ui.pageReader->mangaRetry(); return false; }
					mangaTouch = true;
					mangaTap = true;
					mangaTapX = x;
					mangaTapY = y;
					mangaTapAt = now;
					mangaTouchX = pt.x;
					mangaTouchY = pt.y;
				}
				else if (pt.kind == Pointer::Move && mangaTouch)
				{
					if (std::abs(x - mangaTapX) > MG_TAP_UNITS || std::abs(y - mangaTapY) > MG_TAP_UNITS) mangaTap = false;
					double dy = pt.y - mangaTouchY, dx = pt.x - mangaTouchX;
					mangaTouchX = pt.x;
					mangaTouchY = pt.y;
					if (zoomed && !mangaTap && (dx != 0 || dy != 0))
						ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Drag, dy, (int64_t)now, dx});
				}
				else if (pt.kind != Pointer::Move && mangaTouch)
				{
					mangaTouch = false;
					int dy = y - mangaTapY, dx = x - mangaTapX;
					bool tap = std::abs(dy) <= MG_TAP_UNITS && std::abs(dx) <= MG_TAP_UNITS;
					if (mangaBookDouble)
					{
						mangaBookDouble = false;
						mangaLastTapAt = 0;
						if (tap)
						{
							double z = zoomed ? 1.0 : 2.0;
							ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::ZoomPoint, z, (int64_t)now,
								x / (double)Ui::W, y / (double)Ui::H});
							fprintf(stderr, "manga: book double-tap, zoom %s\n", zoomed ? "back to the fit" : "200%");
						}
					}
					else if (zoomed)
					{
						// the drag's last bit to the lift; a tap raises the bar as at the fit
						double fy = pt.y - mangaTouchY, fx = pt.x - mangaTouchX;
						if (!tap && (fx != 0 || fy != 0)) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Drag, fy, (int64_t)now, fx});
						if (!tap) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Release, 0, (int64_t)now});
						if (tap) { mangaBookBar = true; mangaLastTapAt = now; mangaLastTapX = x; mangaLastTapY = y; }
					}
					else if (std::abs(dy) > 40 && std::abs(dy) > std::abs(dx))
						mangaBookTurn(mangaBookSwipe(ui.pageReader->readerStatus.book, ui.pageReader->readerStatus.mode == MANGA_RTL, p.settings.mangaSwapLR, dy), p);
					else if (tap)
					{
						// a tap: the page numbers a moment, and the Book bar (a second one soon is the double-tap)
						ui.pageReader->mfTagUntil = now + PageReader::BOOK_TAG_MS;
						mangaBookBar = true;
						mangaLastTapAt = now;
						mangaLastTapX = x;
						mangaLastTapY = y;
					}
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
					lay::ReaderFullNav nav = lay::readerFullNav(true);
					if (Rect(nav.sheet).hit(x, y))
					{
						mangaCtrlUntil = now + MG_CTRL_MS;
						if (Rect(nav.sliderHit).hit(x, y)) mangaSlide(x, y, p);
						else
						{
							int id = PageReader::fullNavKeyAt(x, y, true);
							if (id >= 0 && !mangaFullNavOff(id)) mangaCtrlPressed = id;
						}
						return false;
					}
					// outside the bar: the release decides, once the finger is known not to have dragged (a drag
					// goes on to pan the page and hides the bar as it always did)
					mangaTapDismiss = true;
				}
				// in fullscreen the panel is all page (the "<" isn't drawn): a tap anywhere shows the controls,
				// including where that icon would be
				if (!full && !mangaTapDismiss && ui.buttonRect(BACK_ICON).hit(x, y)) { ui.pressedBtn = BACK_ICON; return false; }
				// the header's More, as X
				if (!full && !mangaTapDismiss && lay::readerHeader({0, 0, W, lay::SHELL_HEAD}, false).moreHit.hit(x, y))
				{
					mangaOpenReadMore();
					return false;
				}
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
				for (int k = 0; k < lay::MANGA_READ_KEYS; k++)
					if (mangaReadKeyRect(k).hit(x, y) && !mangaReadKeyOff(k)) mangaPressed = MG_READ_KEY + k;
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
				if (PageReader::fullNavKey(i, true).hit(x, y))
				{
					mangaCtrlUntil = now + MG_CTRL_MS;
					static const Button KEYS[PageReader::FN_KEYS] = {Button::L2, Button::L, Button::R, Button::R2, Button::Y, Button::Start, Button::Select};
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
					// fullscreen: a tap at an edge turns the way the pages run (a strip's top and bottom, a page's
					// sides, as LEFT and RIGHT do), the navigation up or not (up, it stays its time again); only the
					// middle's raises it
					else if (full && mangaFullTapTurn(mangaTapX, mangaTapY, p))
					{
						if (mangaTapDismiss) mangaCtrlUntil = now + MG_CTRL_MS;
						verdict = "tap (edge: turned)";
					}
					// it was up and this tap was in the middle, outside it: it goes, and the tap does nothing else
					else if (mangaTapDismiss)
					{
						mangaCtrlUntil = 0;
						verdict = "tap (closed the navigation)";
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
				int k = mangaPressed - MG_READ_KEY;
				mangaPressed = -1;
				if (k >= 0 && k < lay::MANGA_READ_KEYS && mangaReadKeyRect(k).hit(x, y)) mangaReadKeyAct(k, p);
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
		if (ui.page == Page::MangaSources) return mangaSourcesPointer(pt, x, y, p);
		if (ui.page == Page::Manga)
		{
			// a source's page: its tabs, its grid and Search (ui_manga_browse.cpp)
			if (mangaView == 1) return mangaSourcePointer(pt, x, y, p);
			// a saved server's catalogue or form (ui_manga_servers.cpp)
			if (mangaView >= 2) return mangaServersPointer(pt, x, y, p);
			// (the tabs are the kit's, over the bridged body: shellPointer takes their touches before the mapping)
			// the lists below follow a finger: a drag scrolls them, a tap (lifted where it went down) opens the row
			if (mangaTab == MT_LIBRARY)
			{
				// the kit's grid, in the panel's own units (not bridged): the cells drawMangaLibrary drew
				mangaCache();
				int n = (int)mangaLib.size();
				// a drag moves the rows by the pixel and glides on (the D-pad keeps its column), a tap opens the cover
				mangaGridPointer(mlLib, mangaTabPage(p).content, x, y, [this] { return (int)mangaLib.size(); }, &mlibScroll,
					&mlibGrid, [this, &p](int i) {
						if (i < 0 || i >= (int)mangaLib.size()) return;
						mlibGrid = i;
						mangaChoose(MT_LIBRARY, i, p);
					});
				(void)n;
				return false;
			}
			if (mangaTab == MT_HISTORY)
			{
				// the kit's rows (lay::rowList): a tap on a row continues it
				mangaCache();
				int n = (int)mangaHistory.size();
				lay::RowList l = mangaHisList(p);
				if (!n || !p.settings.mangaHistory || !l.area.hit(x, y)) return false;
				int i = mlHis.list.rowAt(x, y);
				if (i >= n) i = -1;
				ui.pageReader->mangaListSmooth(mlHis.list, x, y, i, [this, &p](int i) {
					if (i < 0 || i >= (int)mangaHistory.size()) return;
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
		// the chapters (the kit's rows): a drag scrolls them (the cursor, unless on Resume, stays among them), a tap reads
		mangaRowsPointer(mlChapters, p, x, y, [this] {
			const LocalSeries* s = mangaLocalSeries(mangaSeriesId);
			return s ? (int)s->chapters.size() : 0;
		}, &mserScroll, &mserCursor, [](int) { return MangaRow(); }, [this, &p](int i, bool) {
			const LocalSeries* s = mangaLocalSeries(mangaSeriesId);
			if (!s || i < 0 || i >= (int)s->chapters.size()) return;
			mserCursor = i;
			mangaSeriesChoose(i, p);
		});
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

	std::string Ui::Manga::coverReady(const std::string& seriesId, const std::string& coverUrl,
		const std::vector<std::pair<std::string, std::string>>* coverHeaders)
	{
		// what drawMangaCover would find, asked as it asks (so a cover in a row's slot is "drawn" every frame and
		// fetched while on screen): "c" the real one, "" the generated one for now
		if (coverOf(seriesId)) return "c";
		if (coverUrl.empty()) return "";
		mangaCoverOnScreen.insert(mangaCoverKey(coverUrl));
		int pw = std::max(1, (int)std::lround(MG_COVER_W * sx));
		int ph = std::max(1, (int)std::lround(MG_COVER_H * sy));
		const std::vector<std::pair<std::string, std::string>> empty;
		return coverOfUrl(coverUrl, pw, ph, coverHeaders ? *coverHeaders : empty) ? "c" : "";
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

	// ---- inside the shell: the kit's tabs, the header, the bridged body's band, the sheets over the frame ----

	namespace
	{
		const char* const SHELL_DOT = " \xE2\x80\xBA ";	// the crumb's separator (" › ")
	}

	std::string Ui::mangaStateLine() const
	{
		switch (page)
		{
		case Page::Extensions: return "manage-extensions";
		case Page::Manga: return "manga tab=" + std::to_string(manga->mangaTab) + " view=" + std::to_string(manga->mangaView);
		case Page::MangaSeries: return "manga-series " + manga->mangaSeriesId;
		case Page::MangaRead: return "manga-read";
		case Page::MangaSources: return "manga-sources tab=" + std::to_string(manga->mstTab);
		case Page::MangaSettings: return "manga-settings";
		default: return "";
		}
	}

	lay::TabPage Ui::Manga::mangaTabPage(const Player& p) const
	{
		(void)p;
		Kit& k = *ui.kit;
		std::vector<int> widths;
		for (int i = 0; i < MANGA_TABS; i++) widths.push_back(k.tabW(mangaTabLabel(i)));
		return lay::tabPage(lay::shell().body, widths, k.tabCapW("L/R"), lay::TAB_H + 6 + k.capsW("START"));
	}

	std::string Ui::Manga::mangaTabLabel(int i) const
	{
		// Downloads carries what is queued ("Downloads 2", screens.js manga): the tab is where it is seen
		if (i == MT_DOWNLOADS)
		{
			int n = mangaDlQueueCount();
			return n ? std::string(MANGA_TAB_NAMES[i]) + " " + std::to_string(n) : std::string(MANGA_TAB_NAMES[i]);
		}
		return MANGA_TAB_NAMES[i];
	}

	void Ui::Manga::drawShellBody(const Player& p)
	{
		if (!tabView()) return;
		lay::TabPage tp = mangaTabPage(p);
		std::vector<std::string> labels;
		for (int i = 0; i < MANGA_TABS; i++) labels.push_back(mangaTabLabel(i));
		ui.kit->tabs(tp.strip.area, labels, mangaTab, "L/R", BG, mangaTabPressed);
		// the Manga settings key, right of the tabs (the same Rect its touch tests); START is the button
		ui.kit->iconKey({tp.key.x, tp.key.y, tp.key.h, tp.key.h}, G_GEAR, mangaSetPressed);
		ui.kit->caps("START", tp.key.x + tp.key.h + 6, tp.key.y + tp.key.h / 2, ACCENT, BG);
	}

	bool Ui::Manga::shellModal() const
	{
		return ui.kbOpen || mlibMenu || mhisMenu || mengSheet || mlocSheet != MLS_NONE || mangaSourcesModal() ||
			mangaFilterShown() || mdlSheet != MDLS_NONE || mangaSrvMenu ||
			(ui.page == Page::Extensions && extSheet);
	}

	void Ui::Manga::mangaTabSet(int tab, Player& p)
	{
		mangaTab = (tab % MANGA_TABS + MANGA_TABS) % MANGA_TABS;
		mangaClearAt = 0;
		mBrowseList = false;
		mangaPressed = -1;
		if (mangaTab == MT_DOWNLOADS) mangaDownloadsOpen(p);
	}

	bool Ui::Manga::shellPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (!tabView()) return false;
		lay::TabPage tp = mangaTabPage(p);
		lay::Box body = lay::shell().body;
		if (pt.kind == Pointer::Down)
		{
			mangaTabPressed = -1;
			mangaSetPressed = false;
			if (tp.key.w > 0 && x >= tp.key.x && x < tp.key.x + tp.key.w && y >= tp.key.y && y < tp.key.y + tp.key.h)
			{
				mangaSetPressed = true;
				return true;
			}
			// the bridged body is the old page's (mapped); under the body is the hint bar: nothing there is pressed
			if (y >= body.y + body.h) return true;
			if (y >= tp.content.y) return false;
			// the strip's row: a tab (the same Rects drawShellBody drew), or nothing
			mangaTabPressed = lay::tabAt(tp.strip, x, y);
			if (mangaTabPressed >= 0) fprintf(stderr, "touch: manga tab %d\n", mangaTabPressed);
			return true;
		}
		if (mangaSetPressed)
		{
			if (pt.kind == Pointer::Up)
			{
				mangaSetPressed = false;
				if (x >= tp.key.x && x < tp.key.x + tp.key.w && y >= tp.key.y && y < tp.key.y + tp.key.h) openMangaSettings();
			}
			return true;
		}
		if (mangaTabPressed < 0) return false;
		if (pt.kind == Pointer::Up)
		{
			int t = mangaTabPressed;
			mangaTabPressed = -1;
			if (lay::tabAt(tp.strip, x, y) == t && t != mangaTab) mangaTabSet(t, p);
		}
		return true;
	}

	Ui::ShellHead Ui::Manga::head(const Player& p) const
	{
		ShellHead h;
		h.back = true;
		h.more = false;	// (X is each page's own: a menu, Sources, Options; the header's ⋯ would press it blind)
		std::string ext = std::string("Extensions") + SHELL_DOT;
		if (ui.page == Page::Extensions)
		{
			h.title = "Manage extensions";
			h.crumb = ext + "Manage extensions";
			if (extSheet) return h;
			h.hints = {{"A", "On / Off"}};
			if (p.settings.mangaOn) h.hints.push_back({"X", "Its settings"});
			h.hints.push_back({"B", "Back"});
			h.hints.push_back({"L2/R2", "Sections"});
			return h;
		}
		h.hints = ui.capHints;
		if (ui.page == Page::MangaSettings)
		{
			h.title = "Manga settings";
			h.crumb = ext + "Manga" + SHELL_DOT + "Settings";
			return h;
		}
		if (tabView())
		{
			// the tab and what is in it (screens.js manga: "Library · 9 series")
			h.title = "Manga";
			std::string what = MANGA_TAB_NAMES[mangaTab];
			auto series = [](size_t n) { return std::to_string(n) + (n == 1 ? " series" : " series"); };
			if (mangaTab == MT_LIBRARY && !mangaLib.empty()) what += " \xC2\xB7 " + series(mangaLib.size());
			else if (mangaTab == MT_HISTORY && p.settings.mangaHistory && !mangaHistory.empty())
				what += " \xC2\xB7 " + series(mangaHistory.size());
			else if (mangaTab == MT_DOWNLOADS && mangaDlQueueCount())
				what += " \xC2\xB7 " + std::to_string(mangaDlQueueCount()) + " queued";
			h.crumb = mangaScanning && mangaTab == MT_LIBRARY ? mangaReadingLine() : what;
			// Browse's and Downloads' own line (where a folder is, the storage line), set as they drew
			if ((mangaTab == MT_BROWSE || mangaTab == MT_DOWNLOADS) && !tabCrumb.empty()) h.crumb = tabCrumb;
			// Library and History are the kit's (no old hints to catch): their own, as the spec draws them
			if (mangaTab == MT_LIBRARY)
				h.hints = mangaLib.empty() ? std::vector<Hint>{{"X", "More"}, {"B", "Back"}}
					: std::vector<Hint>{{"D-pad", "Move"}, {"A", "Open"}, {"X", "More"}, {"B", "Back"}};
			else if (mangaTab == MT_HISTORY)
				h.hints = !p.settings.mangaHistory ? std::vector<Hint>{{"X", "Manga settings"}, {"B", "Back"}}
					: mangaHistory.empty() ? std::vector<Hint>{{"X", "More"}, {"B", "Back"}}
					: std::vector<Hint>{{"A", "Continue"}, {"Y", "The series"}, {"X", "More"}, {"B", "Back"}};
			// the tabs step with L2 / R2 (L / R are the sections, as on every shell page)
			bool hasTab = false;
			for (auto& x : h.hints) hasTab |= x.key == "L/R";
			if (!hasTab) h.hints.push_back({"L/R", "Tab"});
			return h;
		}
		h.title = ui.capTitle.empty() ? std::string("Manga") : ui.capTitle;
		h.crumb = ext + "Manga";
		if (ui.page == Page::MangaSources) h.crumb += std::string(SHELL_DOT) + "Sources";
		// the page's own line, as it drew it (a series' chapters and source, a catalogue's place)
		if (!tabCrumb.empty()) h.crumb = tabCrumb;
		return h;
	}

	// ---- the area: Ui's press, pointer, tick, renderPage and signature while one of its pages shows ----

	bool Ui::Manga::key(Button b, Player& p)
	{
		// the Extensions page, Manage extensions and the manga reader's pages: their own keys (the sheet over the
		// first two)
		if (ui.page == Page::ExtSection || ui.page == Page::Extensions || ui.page == Page::Manga) return extKey(b, p);
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
			if (ui.page == Page::ExtSection || ui.page == Page::Extensions || ui.page == Page::Manga) return extPointer(pt, x, y, p);
			if (ui.page == Page::MangaSeries || ui.page == Page::MangaSources) return mangaPointer(pt, x, y, p);
			if (ui.page == Page::MangaSettings) return ui.settingsPage->settingsDown(pt, x, y);
			return false;
		}
		// the reader's page: its drag area and slider take the finger's moves too (the mini bar's taken before)
		if (ui.page == Page::MangaRead) return mangaPointer(pt, x, y, p);
		// a manga source's grid scrolls with the finger: its moves and lift go to it (its down comes late)
		if (((ui.page == Page::Manga && mangaView == 1) || (ui.page == Page::MangaSeries && mangaSourceSeriesOn())) && pt.kind != Pointer::Down)
			return mangaPointer(pt, x, y, p);
		// Manage extensions and the app's tabs: a row acts as the finger lifts (as Home's do), so the lift is taken
		// here, before Ui's down-only pages; the down comes late (the Extensions page's comes straight to extPointer)
		if (pt.kind == Pointer::Up && (ui.page == Page::ExtSection || ui.page == Page::Extensions || ui.page == Page::Manga))
			return extPointer(pt, x, y, p);
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
		tabCrumb.clear();	// (each page's draw says its line again)
		frameList = nullptr;	// (the lists drawn say which, mlSync; Ui steps it from the next pass)
		if (ui.page == Page::ExtSection) drawExtSection(p);
		else if (ui.page == Page::Extensions) drawExtensions(p);
		else if (ui.page == Page::Manga) drawManga(p);
		else if (ui.page == Page::MangaSeries) drawMangaSeries(p);
		else if (ui.page == Page::MangaRead) drawMangaRead(p);
		else if (ui.page == Page::MangaSettings) drawMangaSettings(p);
		else if (ui.page == Page::MangaSources) drawMangaSourcesPage(p);
		// the reader's pages grid (PageReader's: the Docs viewer shows it too)
		if (ui.page == Page::MangaRead && ui.pageReader->mangaPages) frameList = &ui.pageReader->pagesList;
		shownList = frameList;
	}

	std::string Ui::Manga::extSignature() const
	{
		return "|ext" + std::to_string(ui.mangaOn) + std::to_string(extPageCursor) + std::to_string(extSheet) +
			std::to_string(extSheetCursor) + " " + std::to_string(extPressed) + "." + std::to_string(extPagePressed) +
			(mangaStore ? "+" + std::to_string(mangaStore->serial()) : "-") +
			(ui.mangaOn ? "d" + std::to_string(mangaDlQueueCount()) : "");
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
		// Extensions: the page's cursor and a finger's row, the note's focus, the switch (the rows say it), a finger's
		// button down on them (mangaOn is in the settings too: a change redraws anywhere)
		if (ui.page == Page::ExtSection || ui.page == Page::Extensions || ui.page == Page::Manga) sig += extSignature();
		// the kit's tabs over the app's body: a finger on one
		if (ui.page == Page::Manga) sig += "|mtp" + std::to_string(mangaTabPressed);
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
					(int)mangaTouch, (int)ui.slideHeld(SL_MANGA), ui.pageReader->readerStatus.zoomPct, (int)mangaPageMarked(), ui.pageReader->mangaPagesTab,
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
					sig += "|bb" + std::to_string(mangaBookBar) + "," + std::to_string(mangaBookBarPressed);
					sig += "|full" + std::to_string(ui.pageReader->readerBottom.gen) + " " + std::to_string(ui.pageReader->mfTex.size()) + " " +
						std::to_string(ui.pageReader->mfWaiting) + " " + ui.pageReader->spinKey() + " " + std::to_string(ui.pageReader->readerBottom.pages.size()) + (SDL_GetTicks() < ui.pageReader->mfTagUntil ? "T" : "");
					// the controls over the pages and the first open's toast: how long they still show, in 100 ms
					// steps, so each hides by itself (no finger, no other change to draw for)
					Uint32 now = SDL_GetTicks();
					sig += "|fc" +
						std::to_string(mangaCtrlUntil && now < mangaCtrlUntil ? (int)((mangaCtrlUntil - now + 99) / 100) : 0) + " " +
						std::to_string(mangaToastUntil && now < mangaToastUntil ? (int)((mangaToastUntil - now + 99) / 100) : 0) + " " +
						std::to_string(mangaCtrlPressed);
					for (auto& b : ui.pageReader->readerBottom.boxes)
						sig += " " + std::to_string(b.page) + ":" + std::to_string((int)std::lround(b.x * 4096)) + "," + std::to_string((int)std::lround(b.y * 4096)) +
							"," + std::to_string((int)std::lround(b.w * 4096));
					// Book's tag: its setting and its fade's step (a frame each step as it goes)
					if (ui.pageReader->readerStatus.book)
						sig += "|tg" + std::to_string(p.settings.mangaBookTag) + "," + std::to_string(ui.pageReader->mangaBookTagAlpha(p, now) / 16);
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
		// (the header and the tabs are the shell's and the kit's: drawShellBody; the title is caught for nothing)
		switch (mangaTab)
		{
		case MT_LIBRARY: drawMangaLibrary(p); break;
		case MT_HISTORY: drawMangaHistory(p); break;
		case MT_BROWSE: drawMangaBrowse(p); break;
		case MT_DOWNLOADS: drawMangaDownloads(p); break;
		}
		if (mengSheet) later([this] { drawMangaEngineSheet(); });
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
		case Button::Up: case Button::Down: case Button::Left: case Button::Right: mengCursor = 1 - mengCursor; break;
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

	// what the offer says: the sentence, then the failure (or the note that cards work without it)
	std::vector<std::string> Ui::Manga::mangaEngineLines(bool& failed) const
	{
		static const char* TEXT_ = "Most online sources need it: a one-time download, kept on the card. It runs only while "
			"you use them.";
		static const char* UP_ = "It uses about 70 MB less memory, so it keeps running while you read. A one-time "
			"download; the engine you have works meanwhile.";
		std::vector<std::string> lines = ui.wrapText(mengUpgrade ? UP_ : TEXT_, F_I14, lay::PANEL_W - 2 * lay::SHEET_PAD - 4, 3);
		std::string err = !mangaNet ? "" : mengUpgrade
			? (mangaNet->javaExt().upgrade() == JavaExtHost::Upgrade::Failed ? mangaNet->javaExt().upgradeError() : "")
			: (mangaNet->javaExt().state() == JavaExtHost::State::Failed ? mangaNet->javaExt().error() : "");
		failed = !err.empty();
		if (failed)
			for (auto& l : ui.wrapText(err, F_I14, lay::PANEL_W - 2 * lay::SHEET_PAD - 4, 2)) lines.push_back(l);
		else if (!mengUpgrade) lines.push_back("Manga on your card works without it.");
		return lines;
	}

	// the kit's busy sheet while it downloads, its confirm sheet for the offer (Not now is the safe key)
	lay::BusySheet Ui::Manga::mangaEngineBusy() const { return lay::busySheet(); }
	lay::ConfirmSheet Ui::Manga::mangaEngineConfirm() const
	{
		bool failed = false;
		return lay::confirmSheet((int)mangaEngineLines(failed).size(), false);
	}
	Ui::Rect Ui::Manga::mangaEngineBox() const
	{
		lay::Box b = mengBusy ? mangaEngineBusy().sheet.box : mangaEngineConfirm().sheet.box;
		return {b.x, b.y, b.w, b.h};
	}
	// 0 Download, 1 Not now (Cancel while it downloads)
	Ui::Rect Ui::Manga::mangaEngineRow(int i) const
	{
		lay::Box b = mengBusy ? mangaEngineBusy().key : i == 0 ? mangaEngineConfirm().action : mangaEngineConfirm().safe;
		return {b.x, b.y, b.w, b.h};
	}

	void Ui::Manga::drawMangaEngineSheet()
	{
		Kit& k = *ui.kit;
		if (mengBusy && mangaNet)
		{
			JavaExtHost& h = mangaNet->javaExt();
			int64_t done = 0, total = 0;
			h.progress(done, total);
			if (total <= 0) total = mengUpgrade ? 58000000 : 241000000;
			Kit::BusySpec b;
			b.title = mengUpgrade ? "Downloading the lighter engine" : "Downloading the manga engine";
			// what it's doing: the runtime, then (an install) the server; between them the runtime is unpacked, no
			// bytes coming, so the spinner says it's working where the bar would stand still
			auto step = h.step();
			b.tick = (int)(SDL_GetTicks() / 120);
			if (step == JavaExtHost::Step::Unpacking) b.file = "Unpacking the runtime\xE2\x80\xA6";
			else
			{
				b.file = step == JavaExtHost::Step::Server ? "Downloading the server" : "Downloading the runtime";
				b.share = std::clamp((double)done / (double)total, 0.0, 1.0);
				b.left = std::to_string((long long)(done / 1000000)) + " of " + std::to_string((long long)(total / 1000000)) + " MB";	// (MB as the offer counts them: 241, 58)
			}
			b.right = "Keep Banana Player open";
			b.key = "Cancel";
			k.busySheet(mangaEngineBusy(), b);
			return;
		}
		bool failed = false;
		std::vector<std::string> lines = mangaEngineLines(failed);
		Kit::ConfirmSpec c;
		c.title = mengUpgrade ? "A lighter manga engine" : "Get the manga engine?";
		c.titleDanger = failed;
		c.safe = "Not now";
		c.action = std::string("Download ") + (mengUpgrade ? "58 MB" : "241 MB");
		c.focus = mengCursor == 0 ? 1 : 0;
		k.confirmSheet(lay::confirmSheet((int)lines.size(), false), c, lines);
	}

	lay::AlbumGrid Ui::Manga::mangaLibGrid(const Player& p) const
	{
		return lay::coverGrid(mangaTabPage(p).content, (int)mangaLib.size(), mlibScroll, MG_COLS);
	}

	lay::RowList Ui::Manga::mangaHisList(const Player& p) const
	{
		int n = p.settings.mangaHistory ? (int)mangaHistory.size() : 0;
		return lay::rowList(mangaTabPage(p).content, n, mhisScroll);
	}

	std::string Ui::Manga::mangaHisGone(const ChapterProgress& cp) const
	{
		if (cp.seriesId.rfind("local:", 0) == 0) return mangaScanned && !mangaLocalById.count(cp.seriesId) ? "not on a card" : "";
		// a source's series: its source not among those installed (asked only once the engine has said what it has,
		// so a slow start doesn't dim the whole list)
		if (isSourceId(cp.seriesId) && mangaNet && mangaNet->started() && mangaSourceLabel(cp.seriesId).empty())
			return "source removed";
		return "";
	}

	// ---- the smooth lists ----

	void Ui::Manga::mlFollow(MList& m, Rect view, int rowH, int n, int cols, int& scroll, int* cursor)
	{
		m.follow(view, rowH, n, cols, scroll, cursor);
	}

	void Ui::Manga::mlSync(MList& m, int& scroll)
	{
		bool changed = m.sync(scroll);
		frameList = &m.list;
		// a finger's list moved a row: the page's own follow-up (the next page asked near the end)
		if (changed && m.scrolled) m.scrolled();
		if (!m.list.moving() && !m.list.dragging()) m.scrolled = nullptr;
	}

	void Ui::Manga::mangaRows(MList& m, const Player& p, int n, int& scroll, int& cursor, const std::function<MangaRow(int)>& at)
	{
		lay::Box area = mangaListArea(p);
		const int pitch = lay::ROW2_H + lay::ROW2_GAP;
		mlFollow(m, area, pitch, n, 1, scroll, &cursor);
		// each row's look once a frame (the key and the draw ask the same)
		auto rows = std::make_shared<std::map<int, MangaRow>>();
		auto rowAt = [rows, at](int i) -> const MangaRow& {
			auto it = rows->find(i);
			if (it == rows->end()) it = rows->emplace(i, at(i)).first;
			return it->second;
		};
		SmoothList::Spec spec;
		spec.refresh = m.refresh;
		spec.view = area;
		spec.rowH = pitch;
		spec.count = n;
		spec.key = [rowAt, cursor](int i) {
			const MangaRow& m = rowAt(i);
			char b[96];
			snprintf(b, sizeof b, "|%p|%d%d%d%d%d%d%d|%.3f|", (const void*)m.glyph, m.off, m.pillBad, m.barBad, m.pillLive, i == cursor,
				m.dot, m.dotOn, m.bar);
			return m.name + "|" + m.second + "|" + m.note + "|" + m.pill + "|" + m.keyLabel + "|" + m.keyCap + b;
		};
		spec.draw = [this, rowAt, cursor](int i, Rect box) {
			Kit& k = *ui.kit;
			const MangaRow& m = rowAt(i);
			lay::Box r{box.x, box.y, box.w, lay::ROW2_H};
			bool focused = i == cursor;
			bool key = focused && !m.keyLabel.empty();
			Kit::Row row;
			row.under = BG;
			row.glyph = m.glyph;
			row.dot = m.dot;
			row.dotOn = m.dotOn;
			row.name = m.name;
			row.second = m.second;
			// (the focused row's key takes the right end: its note and pill would sit under it)
			if (!key) row.note = m.note;
			if (!key) row.pill = m.pill;
			row.tone = m.pillBad ? Kit::Pill::Bad : m.pillLive ? Kit::Pill::Live : Kit::Pill::Fact;
			row.off = m.off;
			row.focused = focused;
			k.row(r, row);
			if (m.bar >= 0)
			{
				int x = r.x + 52, w = r.w - 52 - lay::SP12 - (key ? k.keyW(m.keyLabel, m.keyCap) + lay::SP8 : 0);
				ui.drawLevel({x, r.y + r.h - 7, std::max(1, w), 3}, (int)std::lround(std::max(1, w) * std::clamp(m.bar, 0.0, 1.0)),
					TRACK, m.barBad ? DANGER : focused ? ACCENT : DIM);
			}
			if (key)
			{
				Kit::Key kk;
				kk.label = m.keyLabel;
				kk.key = m.keyCap;
				k.key(lay::rowKey(r, k.keyW(m.keyLabel, m.keyCap)), kk);
			}
		};
		m.list.draw(std::move(spec));
		mlSync(m, scroll);
		int fit = std::max(1, (area.h + lay::ROW2_GAP) / pitch);
		if (n > fit) ui.drawScrollBar(area.y, area.h, scroll, fit, n);
	}

	Ui::Rect Ui::Manga::mangaSheetView(const lay::ListSheet& sh, int n, int shown) const
	{
		lay::Box r0 = sh.row(0);
		return {r0.x, sh.rowsY, r0.w, std::min(n, shown) * lay::LIST_ROW};
	}

	void Ui::Manga::mangaSheetRows(MList& m, const lay::ListSheet& sh, int n, int shown, int& scroll, int& cursor,
		const std::function<SheetRow(int)>& row)
	{
		Rect view = mangaSheetView(sh, n, shown);
		mlFollow(m, view, lay::LIST_ROW, n, 1, scroll, &cursor);
		auto rows = std::make_shared<std::map<int, SheetRow>>();
		auto rowAt = [rows, row](int i) -> const SheetRow& {
			auto it = rows->find(i);
			if (it == rows->end()) it = rows->emplace(i, row(i)).first;
			return it->second;
		};
		SmoothList::Spec spec;
		spec.view = view;
		spec.rowH = lay::LIST_ROW;
		spec.count = n;
		spec.key = [rowAt, cursor](int i) {
			const SheetRow& r = rowAt(i);
			char b[64];
			snprintf(b, sizeof b, "|%p|%d%d", (const void*)r.glyph, r.danger, i == cursor);
			return r.name + "|" + r.note + b;
		};
		spec.draw = [this, rowAt, cursor](int i, Rect box) {
			const SheetRow& s = rowAt(i);
			Kit::Row r;
			r.font = F_P16;
			r.glyph = s.glyph;
			r.name = s.name;
			r.note = s.note;
			r.danger = s.danger;
			r.focused = i == cursor;
			ui.kit->row({box.x, box.y, box.w, lay::LIST_ROW}, r);
		};
		m.list.draw(std::move(spec));
		mlSync(m, scroll);
		if (n > shown) ui.drawScrollBar(sh.rowsY, std::min(n, shown) * lay::LIST_ROW, scroll, shown, n);
	}

	bool Ui::Manga::mangaSheetPointer(MList& m, const lay::ListSheet& sh, int n, int shown, int x, int y, std::function<void(int)> tap)
	{
		if (n <= 0 || !mangaSheetView(sh, n, shown).hit(x, y)) return false;
		int i = m.list.rowAt(x, y);
		if (i >= n) i = -1;
		m.scrolled = nullptr;
		ui.pageReader->mangaListSmooth(m.list, x, y, i, std::move(tap));
		return true;
	}

	void Ui::Manga::mangaRowsPointer(MList& m, const Player& p, int x, int y, std::function<int()> count, int* scroll, int* cursor,
		std::function<MangaRow(int)> at, std::function<void(int, bool)> tap, std::function<void()> scrolled)
	{
		(void)scroll;
		int n = count();
		if (!n || !mangaListArea(p).hit(x, y)) return;
		int i = m.list.rowAt(x, y);
		if (i >= n) i = -1;
		// the focused row's key, where it was drawn: its own action (the rest of the row is A)
		bool onKey = false;
		if (i >= 0 && i == *cursor)
		{
			MangaRow r = at(i);
			Rect b = m.list.rowBox(i);
			if (!r.keyLabel.empty())
				onKey = lay::rowKey({b.x, b.y, b.w, lay::ROW2_H}, ui.kit->keyW(r.keyLabel, r.keyCap)).hit(x, y);
		}
		m.scrolled = std::move(scrolled);
		ui.pageReader->mangaListSmooth(m.list, x, y, i, [tap, onKey](int i) { tap(i, onKey); });
	}

	lay::Box Ui::Manga::mangaListArea(const Player& p) const
	{
		lay::Box body = lay::shell().body;
		if (ui.page == Page::Manga)
		{
			if (mangaView == 0) return mangaTabPage(p).content;
			if (mangaView == 1) return mangaSrcTabPage(p).content;
			return lay::seriesPage(body, mangaSrvView == MGSRV_SERIES).rows;
		}
		if (ui.page == Page::MangaSources)
		{
			// (the note that repositories are yours keeps two lines at the foot)
			// (and Available's search line, while there is a search, sits over its rows)
			lay::TabPage tp = mangaSourcesTabPage(p);
			lay::Box c = mstTab == MST_AVAILABLE && !mstAvQuery.empty() ? lay::searchLine(tp).rows : tp.content;
			c.h = std::max(0, c.h - 44);
			return c;
		}
		if (ui.page == Page::MangaSeries) return lay::seriesPage(body, mangaNetView != MNF_SERIES).rows;
		return lay::seriesPage(body, false).rows;
	}

	std::vector<int> Ui::Manga::mangaSrcSlots() const
	{
		std::vector<int> s{SRC_POPULAR};
		if (mangaSrcHasLatest) s.push_back(SRC_LATEST);
		s.push_back(SRC_SEARCH);
		return s;
	}

	lay::TabPage Ui::Manga::mangaSrcTabPage(const Player& p) const
	{
		(void)p;
		static const char* NAMES[3] = {"Popular", "Latest", "Search"};
		std::vector<int> widths;
		for (int s : mangaSrcSlots()) widths.push_back(ui.kit->tabW(NAMES[s]));
		return lay::tabPage(lay::shell().body, widths, ui.kit->tabCapW("L/R"));
	}

	lay::TabPage Ui::Manga::mangaSourcesTabPage(const Player& p) const
	{
		(void)p;
		return lay::tabPage(lay::shell().body, {ui.kit->tabW("Available"), ui.kit->tabW("Repositories")},
			ui.kit->tabCapW("L/R"));
	}

	Ui::Rect Ui::Manga::mangaGridView(lay::Box area) const
	{
		// SP6 of room around the covers (a focused cover's ring reaches 5 past it); the foot stays the area's
		return {area.x - lay::SP6, area.y - lay::SP6, area.w + 2 * lay::SP6, area.h + lay::SP6};
	}

	void Ui::Manga::drawMangaGrid(MList& m, lay::Box area, int n, int& scroll, int& cursor, const std::function<GridCell(int)>& cell)
	{
		lay::AlbumGrid g0 = lay::coverGrid(area, n, 0, MG_COLS);
		const int pitch = g0.cellH + g0.rowGap, cols = g0.cols;
		Rect view = mangaGridView(area);
		mlFollow(m, view, pitch, n, cols, scroll, &cursor);
		auto cells = std::make_shared<std::map<int, GridCell>>();
		auto cellAt = [cells, cell](int i) -> const GridCell& {
			auto it = cells->find(i);
			if (it == cells->end()) it = cells->emplace(i, cell(i)).first;
			return it->second;
		};
		SmoothList::Spec spec;
		spec.refresh = m.refresh;
		spec.view = view;
		spec.rowH = pitch;
		spec.count = g0.rows;
		spec.key = [cellAt, cursor, cols, n](int r) {
			std::string k;
			for (int i = r * cols; i < n && i < (r + 1) * cols; i++)
			{
				const GridCell& c = cellAt(i);
				k += c.title + "|" + c.badge + "|" + c.coverKey + (i == cursor ? "|*|" : "||");
			}
			return k;
		};
		spec.draw = [this, cellAt, cursor, cols, n, g0](int r, Rect box) {
			for (int i = r * cols; i < n && i < (r + 1) * cols; i++)
			{
				const GridCell& gc = cellAt(i);
				Kit::Cover c;
				c.key = c.title = gc.title;
				c.badge = gc.badge;
				c.picture = gc.picture;
				c.focused = i == cursor;
				lay::Box b{box.x + lay::SP6 + (i % cols) * (g0.cellW + g0.colGap), box.y + lay::SP6, g0.cellW, g0.cellH};
				ui.kit->coverTile(lay::coverTile(b, true, false), c);
			}
		};
		m.list.draw(std::move(spec));
		mlSync(m, scroll);
		if (g0.rows > g0.shown) ui.drawScrollBar(area.y, area.h, scroll, g0.shown, g0.rows);
	}

	void Ui::Manga::mangaGridPointer(MList& m, lay::Box area, int x, int y, std::function<int()> count, int* scroll, int* cursor,
		std::function<void(int)> open, std::function<void()> scrolled)
	{
		(void)scroll;
		(void)cursor;
		int n = count();
		if (!n || !area.hit(x, y)) return;
		// the cover under the finger, in the row's box as the grid laid it (the gaps between are no cover)
		int hit = -1, r = m.list.rowAt(x, y);
		if (r >= 0)
		{
			lay::AlbumGrid g0 = lay::coverGrid(area, n, 0, MG_COLS);
			Rect box = m.list.rowBox(r);
			for (int i = r * g0.cols; i < n && i < (r + 1) * g0.cols; i++)
			{
				Rect c{box.x + lay::SP6 + (i % g0.cols) * (g0.cellW + g0.colGap), box.y + lay::SP6, g0.cellW, g0.cellH};
				if (c.hit(x, y)) hit = i;
			}
		}
		m.scrolled = std::move(scrolled);
		ui.pageReader->mangaListSmooth(m.list, x, y, hit, [open](int i) { open(i); });
	}

	void Ui::Manga::mangaOpenMarkMore(const std::string& seriesId, std::function<std::vector<std::string>()> ids, std::function<int()> focus)
	{
		ui.openMore([this, seriesId, ids, focus](const Player&) {
			MoreSpec m;
			if (!mangaStore) return m;
			std::vector<std::string> all = ids();
			int at = focus();
			std::vector<MangaChapterMark> marks;
			for (const auto& cid : all)
			{
				MangaChapterMark mk;
				if (const ChapterProgress* pr = mangaStore->progress(cid)) mk.read = pr->read;
				marks.push_back(mk);
			}
			bool here = at >= 0 && at < (int)all.size();
			auto mark = [this, seriesId, all](std::vector<int> idx, bool read) {
				std::vector<std::string> sel;
				for (int i : idx) sel.push_back(all[i]);
				mangaStore->markRead(seriesId, sel, read);
				mangaSave();
				return false;
			};
			m.glyph = &G_COMIC;
			m.title = mangaTitle(seriesId);
			m.note = std::to_string(all.size()) + (all.size() == 1 ? " chapter" : " chapters");
			MoreItem one;
			one.glyph = &G_CHECK;
			one.label = here && marks[at].read ? "Mark this chapter unread" : "Mark this chapter read";
			one.off = !here;
			one.act = [mark, at, here, marks](Player&) { return mark({at}, !(here && marks[at].read)); };
			m.rows.push_back(one);
			MoreItem prev;
			prev.glyph = &G_CHECK;
			prev.label = "Mark previous as read";
			prev.off = !here || at == 0;
			prev.act = [mark, at, n = (int)all.size()](Player&) { return mark(mangaMarkIndices(MangaMarkSet::Previous, at, n), true); };
			m.rows.push_back(prev);
			bool allRead = mangaAllRead(marks);
			MoreItem every;
			every.glyph = &G_CHECK;
			every.label = allRead ? "Mark all as unread" : "Mark all as read";
			every.off = all.empty();
			every.act = [mark, allRead, n = (int)all.size()](Player&) { return mark(mangaMarkIndices(MangaMarkSet::All, 0, n), !allRead); };
			m.rows.push_back(every);
			return m;
		}, "mangamark");
	}

	std::string Ui::Manga::mangaResumeKeyLabel(const std::string& chapterTitle, const ChapterProgress* pr, int keyW) const
	{
		bool started = pr && !pr->read && (pr->pages > 0 || pr->page > 0);
		int room = keyW - 70;	// the key's padding, its play glyph and the A cap
		return mangaResumeLabel(
			started, chapterShort(chapterTitle), pr ? pr->page : 0, pr ? pr->pages : 0, room,
			[this](const std::string& t) { return ui.kit->textW(t, F_P16); },
			[this](const std::string& t, int w) { return ui.kit->elided(t, F_P16, w); });
	}

	std::vector<lay::Box> Ui::Manga::mangaSeriesKeys(const std::vector<int>& widths) const
	{
		return lay::keyRow(lay::seriesPage(lay::shell().body, true).keys, widths);
	}

	void Ui::Manga::drawMangaTabEmpty(const Player& p, const Glyph& g, const std::string& title, const std::string& text)
	{
		// the kit's Empty state in the content under the tabs (no key: X and B are in the hint bar)
		Kit::Empty e;
		e.glyph = &g;
		e.title = title;
		e.lines = ui.wrapText(text, F_I14, 400, 3);
		ui.kit->emptyState(lay::emptyState(mangaListArea(p), (int)e.lines.size(), 0), e);
	}

	void Ui::Manga::drawMangaLibrary(const Player& p)
	{
		mangaCache();
		// (the count is the shell header's crumb, "Library · 3 series"; while the card is read, the crumb says so)
		int n = (int)mangaLib.size();
		if (n == 0)
		{
			if (mangaScanning) drawMangaTabEmpty(p, G_BOOK, mangaReadingLine(), "The series on your card show up here in a moment.");
			else drawMangaTabEmpty(p, G_BOOK, "Your library is empty",
				"Put CBZ files (or CBR, PDF) or folders of pictures in a Manga folder on your card, or add series from Browse.");
			return;
		}
		if (mlibGrid >= n) mlibGrid = n - 1;
		// the cover grid (screens.js manga: mcell): five across, the unread count a pill at a cover's top right, the
		// focused one ringed; a row of covers a row of the smooth list
		mlLib.refresh = [this](Player&) { mangaScan(); };	// pull to refresh: the Manga folders read again
		drawMangaGrid(mlLib, mangaTabPage(p).content, n, mlibScroll, mlibGrid, [this](int i) {
			GridCell c;
			const std::string id = mangaLib[i].id;
			c.title = mangaLib[i].title;
			int unread = i < (int)mangaLibUnread.size() ? mangaLibUnread[i] : 0;
			if (unread > 0) c.badge = std::to_string(unread);
			c.coverKey = coverReady(id, mangaSeriesCoverUrl(id));
			c.picture = [this, title = c.title, id](Rect r) {
				drawMangaCover(r, title, false, 4, id, mangaSeriesCoverUrl(id));
			};
			return c;
		});
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

	void Ui::Manga::mangaLibMenuAct(Player& p)
	{
		(void)p;
		if (mlibMenuCursor == 1)
		{
			mlibMenu = mlibArmed = false;
			openMangaSettings();
			return;
		}
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

	void Ui::Manga::mangaLibSheetOpen()
	{
		ui.openMore([this](const Player&) { return mangaLibSpec(); }, "mangalib", [this] { return mlibMenu; },
			[this] { mlibMenu = false; mlibArmed = false; });
	}

	Ui::MoreSpec Ui::Manga::mangaLibSpec()
	{
		MoreSpec m;
		if (!mlibMenu) return m;
		m.title = "Library";
		auto gone = mangaMissing();
		// (said under the title: what Remove missing takes, and the card that may only be out)
		m.note = mlibArmed ? "Their read marks go too. A card that's out? Put it back first"
			: gone.empty() ? "Every series is on a card" : "Series no longer on a card, and books Files now opens";
		MoreItem rm;
		rm.glyph = &G_TRASH;
		rm.label = "Remove missing";
		rm.note = gone.empty() ? "None" : mlibArmed ? "A again" : std::to_string(gone.size()) + " gone";
		rm.danger = !gone.empty();
		rm.lit = mlibArmed;
		rm.stay = true;	// (asked twice: the act ends the menu itself)
		rm.act = [this](Player& p) { mlibMenuCursor = 0; mangaLibMenuAct(p); return false; };
		m.rows.push_back(rm);
		MoreItem st;
		st.glyph = &G_GEAR;
		st.label = "Manga settings";
		st.stay = true;
		st.act = [this](Player& p) { mlibMenuCursor = 1; mangaLibMenuAct(p); return false; };
		m.rows.push_back(st);
		return m;
	}

	void Ui::Manga::mangaHisMenuAct(Player& p)
	{
		mhisMenu = false;
		// the last row: the app's settings (START is Settings now)
		if (mhisMenuCursor == 5) { openMangaSettings(); return; }
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

	void Ui::Manga::mangaHisSheetOpen()
	{
		ui.openMore([this](const Player&) { return mangaHisSpec(); }, "mangahis", [this] { return mhisMenu; }, [this] { mhisMenu = false; });
	}

	Ui::MoreSpec Ui::Manga::mangaHisSpec()
	{
		MoreSpec m;
		if (!mhisMenu) return m;
		m.title = "History";
		static const char* ROWS[MHIS_MENU_ROWS] = {"Clear the selected item", "Clear the last hour", "Clear today",
			"Clear the last 24 hours", "Clear all", "Manga settings"};
		for (int i = 0; i < MHIS_MENU_ROWS; i++)
		{
			MoreItem it;
			it.glyph = i == 5 ? &G_GEAR : &G_TRASH;
			it.label = ROWS[i];
			it.danger = i == 4;
			it.stay = true;
			it.act = [this, i](Player& p) { mhisMenuCursor = i; mangaHisMenuAct(p); return false; };
			m.rows.push_back(it);
		}
		return m;
	}

	void Ui::Manga::drawMangaHistory(const Player& p)
	{
		mangaCache();
		if (!p.settings.mangaHistory)
		{
			// kept off: where it is turned on, and that the reading itself is still remembered
			drawMangaTabEmpty(p, G_CLOCK, "Reading history is off",
				"Turn it on in Manga settings (X). Your places and read marks are kept either way.");
			return;
		}
		int n = (int)mangaHistory.size();
		if (n == 0)
		{
			drawMangaTabEmpty(p, G_CLOCK, "Nothing read yet", "Series you read show up here, newest first.");
			return;
		}
		if (mhisCursor >= n) mhisCursor = n - 1;
		// one row a series, newest first (screens.js manga-history): its cover, its title, then the chapter, the page
		// and when, and how far through the chapter as a bar along its foot. The row itself continues (A, a tap):
		// no key takes its end, so nothing is cut short. A smooth list
		lay::RowList l = mangaHisList(p);
		const int pitch = l.rowH + l.gap;
		mlFollow(mlHis, l.area, pitch, n, 1, mhisScroll, &mhisCursor);
		struct HisRow { std::string id, title, second, when, cover; bool off = false; double share = -1; };
		auto rows = std::make_shared<std::map<int, HisRow>>();
		int64_t now = (int64_t)time(nullptr);
		auto rowAt = [this, rows, now](int i) -> const HisRow& {
			auto it = rows->find(i);
			if (it != rows->end()) return it->second;
			const ChapterProgress& cp = mangaHistory[i];
			HisRow h;
			h.id = cp.seriesId;
			h.title = mangaTitle(cp.seriesId);
			std::string from = mangaSourceLabel(cp.seriesId);
			h.second = (from.empty() ? "" : from + " \xC2\xB7 ") + chapterShort(cp.chapterTitle) + " \xC2\xB7 p. " +
				std::to_string(cp.page + 1) + (cp.pages > 0 ? " of " + std::to_string(cp.pages) : "");
			h.when = mangaAgo(cp.at, now);
			if (cp.pages > 0) h.share = std::clamp((cp.page + 1) / (double)cp.pages, 0.0, 1.0);
			// one that can't open now stays, dimmed, and says why (spec: manga-history)
			std::string gone = mangaHisGone(cp);
			if (!gone.empty())
			{
				h.off = true;
				h.second = gone + " \xC2\xB7 " + chapterShort(cp.chapterTitle);
				h.share = -1;
			}
			h.cover = coverReady(h.id, mangaSeriesCoverUrl(h.id));
			return rows->emplace(i, std::move(h)).first->second;
		};
		SmoothList::Spec spec;
		spec.view = l.area;
		spec.rowH = pitch;
		spec.count = n;
		spec.key = [rowAt, cursor = mhisCursor](int i) {
			const HisRow& h = rowAt(i);
			return h.title + "|" + h.second + "|" + h.when + "|" + h.cover + (h.off ? "|o" : "|") + (i == cursor ? "*" : "") + std::to_string((int)(h.share * 1000));
		};
		spec.draw = [this, rowAt, cursor = mhisCursor, rowH = l.rowH](int i, Rect box) {
			Kit& k = *ui.kit;
			const HisRow& h = rowAt(i);
			lay::Box r{box.x, box.y, box.w, rowH};
			Kit::Row row;
			row.under = BG;
			row.mark = [this, title = h.title, id = h.id](Rect m) {
				drawMangaCover({m.x + (m.w - 32) / 2, m.y, 32, m.h}, title, false, 1, id, mangaSeriesCoverUrl(id));
			};
			row.name = h.title;
			row.second = h.second;
			row.note = h.when;
			row.off = h.off;
			row.focused = i == cursor;
			k.row(r, row);
			if (h.share >= 0)
			{
				Rect bar = lay::rowProgress(r);
				ui.drawLevel(bar, (int)std::lround(bar.w * h.share), TRACK, row.focused ? ACCENT : DIM);
			}
		};
		mlHis.list.draw(std::move(spec));
		mlSync(mlHis, mhisScroll);
		if (n > l.shown) ui.drawScrollBar(l.area.y, l.area.h, mhisScroll, l.shown, n);
	}

	Ui::Manga::MangaRow Ui::Manga::mangaLocalRowAt(int i) const
	{
		MangaRow m;
		if (i < 0 || i >= (int)mangaLocal.size()) return m;
		const LocalSeries& s = mangaLocal[i];
		// what its chapters are: comics (CBZ, CBR), PDFs, or folders of pictures
		m.glyph = &G_PICTURE;
		if (!s.chapters.empty() && s.chapters[0].archive) m.glyph = isPdfName(s.chapters[0].path) ? &G_PDF : &G_COMIC;
		m.name = s.title;
		m.note = std::to_string(s.chapters.size()) + (s.chapters.size() == 1 ? " chapter" : " chapters");
		return m;
	}

	void Ui::Manga::drawMangaBrowse(const Player& p)
	{
		tabCrumb.clear();
		mangaCache();
		int n = (int)mangaLocal.size();
		if (mBrowseList)
		{
			tabCrumb = "On this handheld";
			if (n == 0 && mangaScanning)
			{
				drawMangaTabEmpty(p, G_FOLDER, mangaReadingLine(), "The series on your card show up here in a moment.");
				ui.drawHints({{"B", "Back"}});
				return;
			}
			if (n == 0)
			{
				drawMangaTabEmpty(p, G_FOLDER, "No series found", "Copy manga into " + mangaLocalFolder() + ". X shows how.");
				ui.drawHints({{"X", "How to add"}, {"B", "Back"}});
				return;
			}
			if (mListCursor >= n) mListCursor = n - 1;
			// the card's series as the kit's rows: their kind, their name, how many chapters
			tabCrumb = "On this handheld \xC2\xB7 " + mangaLocalFolder();
			mlLocal.refresh = [this](Player&) { mangaScan(); };
			mangaRows(mlLocal, p, n, mListScroll, mListCursor, [this](int i) { return mangaLocalRowAt(i); });
			// while the card is read the header says so (the list may still change); its keys work meanwhile
			if (mangaScanning) tabCrumb = mangaReadingLine();
			ui.drawHints({{"A", "Open"}, {"Y", "Options"}, {"X", "How to add"}, {"B", "Back"}});
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
			drawMangaNetFoldersKit(p);
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
			drawMangaTabEmpty(p, G_BOOK, "Not on this card", "The card was taken out, or the series moved.");
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
		tabCrumb = "On this handheld \xC2\xB7 " + std::to_string(n) + (n == 1 ? " chapter" : " chapters") + " \xC2\xB7 " +
			std::to_string(unread) + " unread" + (filling ? " \xC2\xB7 Reading the chapters\xE2\x80\xA6" : "");
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
		// one row of keys (screens.js manga-series): Resume (A) and the heart (Y, said in the hints: the key is the
		// spec's 64, its picture alone)
		Kit& k = *ui.kit;
		Kit::Key rk;
		rk.glyph = &G_PLAY;
		rk.label = !n ? "No chapters"
			: mangaResumeKeyLabel(s->chapters[resume.chapter].title, mangaStore ? mangaStore->progress(s->chapters[resume.chapter].path) : nullptr, mangaResumeRect().w);
		rk.key = "A";
		rk.tone = Kit::Tone::Primary;
		rk.focused = mserCursor < 0;
		rk.pressed = mangaPressed == MG_RESUME;
		rk.off = n == 0;
		k.key(mangaResumeRect(), rk);
		bool inLib = ms && ms->inLibrary;
		Kit::Key hk;
		hk.glyph = inLib ? &G_HEART : &G_HEART_O;
		hk.lit = inLib;
		hk.pressed = mangaPressed == MG_HEART;
		k.key(mangaHeartRect(), hk);
		if (n == 0) { ui.drawHints({{"B", "Back"}}); return; }
		if (mserCursor >= n) mserCursor = n - 1;
		mlChapters.refresh = [this](Player&) { mangaScan(); };	// pull to refresh: the Manga folders read again
		mangaRows(mlChapters, p, n, mserScroll, mserCursor, [this, s, rc = resume.chapter](int i) { return mangaChapterRow(s->chapters[i].title, s->chapters[i].path, i == rc, -1); });
		ui.drawHints({{"A", "Read"}, {"X", "Read marks"}, {"Y", "In library"}, {"B", "Back"}});
	}

	Ui::Manga::MangaRow Ui::Manga::mangaChapterRow(const std::string& title, const std::string& progressId, bool resume, int pages) const
	{
		// a chapter's row (screens.js seriesB): no icon; a dot on the one Resume opens, read ones dimmed and saying
		// so, the page you were on
		MangaRow m;
		const ChapterProgress* pr = mangaStore ? mangaStore->progress(progressId) : nullptr;
		bool read = pr && pr->read;
		m.dot = true;
		m.dotOn = resume && !read;
		m.name = title;
		m.off = read;
		if (read) m.note = "Read";
		else if (pr && (pr->pages > 0 || pr->page > 0)) m.note = chapterPageLine(pr->page, pr->pages);
		else if (pages > 0) m.note = std::to_string(pages) + " pages";
		return m;
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
			ui.pageReader->mangaZoomAsked = 100;
			mangaReadLive = false;
			mangaProgPage = mangaProgPages = -1;
			drawMangaSeries(p);
			return;
		}
		// (the pages grid comes over Fullscreen and Book too: it is drawn below, never under the pages)
		if (!ui.pageReader->mangaPages && (ui.pageReader->readerStatus.fullscreen || ui.pageReader->readerStatus.book) && ui.pageReader->readerBottom.on)
		{
			ui.pageReader->drawMangaFull(p);
			if (ui.pageReader->readerStatus.book && mangaBookBar) drawMangaBookBar(p);
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
			// over Fullscreen and Book the sheet lies on the plain ground; over one screen, on the reader's page
			if (ui.pageReader->readerStatus.fullscreen || ui.pageReader->readerStatus.book)
			{
				ui.pageReader->drawMangaPages(p);
				return;
			}
		}
		// out of fullscreen: this panel's copies of the pages, its controls and the first open's toast go
		mangaCtrlUntil = mangaToastUntil = 0;
		if (!ui.pageReader->mfTex.empty()) ui.pageReader->mfFree();
		// the header (docs/redesign, manga-read): the series' name at 18 units beside the back key; under it the chapter,
		// the page and how it reads; More at the right (as X)
		const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
		static const char* MODES[MANGA_MODES] = {"Right to left", "Left to right", "Vertical scroll"};
		int m = st.mode >= 0 && st.mode < MANGA_MODES ? st.mode : 0;
		bool strip = m == MANGA_STRIP;
		int pages = st.pages, shown = ui.slideHeld(SL_MANGA) ? (int)ui.slide.value : st.page;
		lay::ReaderHeader h = lay::readerHeader({0, 0, W, lay::SHELL_HEAD}, false);
		Kit::Header hd;
		std::string series = mangaTitle(ui.pageReader->mangaReadSeriesId), chapter = ui.pageReader->mangaReadChapterTitle;
		hd.title = series.empty() ? chapter : series;
		if (!series.empty() && chapter != series) hd.crumb = chapter;
		auto crumb = [&](const std::string& part) { if (!part.empty()) hd.crumb += (hd.crumb.empty() ? "" : " \xC2\xB7 ") + part; };
		crumb(st.atEnd ? "the end" : pages > 0 ? "Page " + std::to_string(shown + 1) + " of " + std::to_string(pages) : "");
		crumb(MODES[m]);
		hd.titleFont = F_SMALL;
		hd.inlineTitle = true;
		hd.more = true;
		ui.kit->header(h.strip, hd);
		lay::MangaReadPage l = lay::mangaReadPage();
		// the slider: the page shown at its left, the chapter's last at its right; a finger there is a Seek (mangaSlide)
		drawMangaSlider();
		// the big surface: it takes the finger's drag (the redesign's pad: a strip says "Drag to scroll", a page its
		// turn chevrons and the double-tap)
		ui.kit->readerSurface(l.surface, strip, strip ? "Drag to scroll" : "Swipe here to turn the page",
			st.failedHere ? "" : strip ? "Double-tap: 200% and back" : "Double-tap: 200% \xC2\xB7 the page snaps when you let go");
		if (st.failedHere) ui.pageReader->drawMangaRetry(false);
		Rect top{l.surface.x, l.surface.y + 14, l.surface.w, 20};
		if (!st.error.empty()) ui.drawTextMid(st.error, F_I14, DANGER, W / 2, top.y + top.h / 2, top.w - 24, 2);
		else if (st.loading) ui.drawTextMid("Loading" + std::string(SDL_GetTicks() / 400 % 4, '.'), F_I12, DIM, W / 2, top.y + top.h / 2, 0, 2);
		// the chapter's end: what comes next by name, or that there is nothing after it (docs/redesign: manga-end;
		// the top panel's card says it too: mangaEndCard)
		if (st.atEnd)
		{
			int ci = mangaReadChapterIndex();
			bool next = ci >= 0 && ci + 1 < mangaReadChapterTotal();
			std::string nextName = next ? mangaReadChapterName(ci + 1) : "";
			Rect e = l.end;
			if (next)
				ui.drawTextMid(std::string("End of chapter \xC2\xB7 R2 or ") + (p.settings.mangaSwapLR ? "LEFT" : "RIGHT") + " for " +
					(nextName.empty() ? "the next" : nextName), F_I14, ACCENT, e.x + e.w / 2, e.y + e.h / 2, e.w, 2);
			else ui.drawTextMid("You\xE2\x80\x99re caught up \xC2\xB7 that was the last chapter", F_I14, DIM, e.x + e.w / 2, e.y + e.h / 2, e.w, 2);
		}
		drawMangaReadKeys(p);
		// what the keys do (the row above names Y, START and SELECT)
		if (strip) ui.kit->hints({{"LEFT/RIGHT", "Scroll"}, {"UP/DOWN", "Zoom"}, {"A", "200%"}, {"X", "More"}, {"B", "Chapters"}});
		else ui.kit->hints({{"LEFT/RIGHT", "Page"}, {"UP/DOWN", "Zoom"}, {"A", "200%"}, {"L3", "Turn"}, {"X", "More"}, {"B", "Chapters"}});
		if (ui.pageReader->mangaPages) ui.pageReader->drawMangaPages(p);
	}

	bool Ui::Manga::mangaReadKeyOff(int k) const
	{
		int ci = mangaReadChapterIndex();
		switch (k)
		{
		case lay::MRK_PREV: return ci <= 0;
		case lay::MRK_NEXT: return ci < 0 || ci + 1 >= mangaReadChapterTotal();
		case lay::MRK_PAGES: return ui.pageReader->readerStatus.pages <= 0;
		default: return false;
		}
	}

	void Ui::Manga::mangaReadKeyAct(int k, Player& p)
	{
		// each as the button its cap names
		switch (k)
		{
		case lay::MRK_PREV: mangaKey(Button::L2, p); break;
		case lay::MRK_PAGES: mangaKey(Button::Y, p); break;
		case lay::MRK_VIEW: mangaKey(Button::Start, p); break;
		case lay::MRK_FULL: mangaKey(Button::Select, p); break;
		case lay::MRK_LIGHT: ui.openLight(); break;
		case lay::MRK_NEXT: mangaKey(Button::R2, p); break;
		default: break;
		}
	}

	void Ui::Manga::drawMangaReadKeys(const Player& p)
	{
		// the row of six, each with its key under it (Button hotkeys on)
		bool caps = p.settings.buttonHotkeys;
		auto keyOf = [&](int k, const Glyph* g, const std::string& label, const char* cap, bool lit = false) {
			Kit::Key kk;
			kk.glyph = g;
			kk.label = label;
			kk.key = caps ? cap : "";
			kk.off = mangaReadKeyOff(k);
			kk.lit = lit;
			kk.pressed = mangaPressed == MG_READ_KEY + k;
			ui.kit->key(mangaReadKeyRect(k), kk);
		};
		// (the chapter keys are narrow: their mark and cap alone)
		keyOf(lay::MRK_PREV, &G_PREV, "", "L2");
		keyOf(lay::MRK_PAGES, &G_BLOCKS, "Pages", "Y");
		keyOf(lay::MRK_VIEW, &G_EYE, "View", "START");
		keyOf(lay::MRK_FULL, nullptr, "Fullscreen", "SELECT");
		// Light: its sun; while Extra dim or a filter is on, what's on in place of the name
		std::string note = ui.lightNote(p);
		keyOf(lay::MRK_LIGHT, &G_SUN, note.empty() ? "Light" : note, "", !note.empty());
		keyOf(lay::MRK_NEXT, &G_NEXT, "", "R2");
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
		// the series keeps both, each as chosen: Book never rewrites the Reading mode (a Vertical scroll comes back)
		int mode = ui.pageReader->readerStatus.modeSet;
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
		mangaBookBar = false;
		mangaBookBarPressed = -1;
		ui.pageReader->mfTagUntil = book ? now + 3000 : 0;
		ui.pageReader->mangaZoomAsked = 100;
	}

	void Ui::Manga::mangaBookTurn(int step, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		// the last spread: the next chapter (zoomed, the reader pans to the end, then gives the fit back first)
		if (step > 0 && ui.pageReader->readerStatus.atEnd && ui.pageReader->readerStatus.zoomPct <= 100) { mangaChapterStep(1, p); return; }
		if (step < 0 && ui.pageReader->readerStatus.atStart && ui.pageReader->readerStatus.zoomPct <= 100) { mangaChapterStep(-1, p, MANGA_LAST_PAGE); return; }
		if (step != 0) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Step, (double)step, (int64_t)now});
		ui.pageReader->mfTagUntil = now + 1500;
	}

	void Ui::Manga::stickAxis(int value, Player& p, int valueX)
	{
		bool reading = ui.page == Page::MangaRead && ui.pageReader->readerStatus.open && !ui.topOverlay() &&
			!ui.pageReader->mangaPages && !mangaReadWaiting && ui.pageReader->readerStatus.error.empty();
		// Book is held sideways, whole pages: one push is one spread, turned the way its D-pad points
		if (reading && ui.pageReader->readerStatus.book && ui.pageReader->readerStatus.zoomPct > 100)
		{
			// zoomed in: the stick pans the picture of both panels together (the reader moves it each frame)
			stickDir = 0;
			if (value != 0 || valueX != 0 || stickSent)
				ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Stick, (double)value, (int64_t)SDL_GetTicks(), (double)valueX});
			stickSent = value != 0 || valueX != 0;
			return;
		}
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
		if (value != 0 || valueX != 0)
		{
			// the reader scrolls the Strip, or pans a paged page with room (issue 16); every pass, so a chapter
			// opened under a held stick starts at once (the same value changes nothing)
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Stick, (double)value, (int64_t)SDL_GetTicks(), (double)valueX});
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
		// every key puts the sideways bar away (a tap in the middle shows it); START is View and X More, as in the other
		// layouts, both upright sheets over the pages
		mangaBookBar = false;
		// UP and DOWN point along the spread as it is held (a turn; zoomed, to the pages' end first, then a turn);
		// LEFT and RIGHT across it, up and down as held: the zoom, the one up as held in (fit, 150, 200, 300 %)
		if (b == Button::Up || b == Button::Down)
		{
			mangaBookTurn(mangaBookStep(ui.pageReader->readerStatus.book, rtl, p.settings.mangaSwapLR, b == Button::Up ? -1 : 1), p);
			return false;
		}
		if (b == Button::Left || b == Button::Right)
		{
			int dir = mangaBookZoomKey(ui.pageReader->readerStatus.book, b == Button::Right ? 1 : -1);
			int pct = ui.pageReader->readerStatus.zoomPct > 0 ? ui.pageReader->readerStatus.zoomPct : 100;
			ui.pageReader->mangaZoomTo(mangaBookZoomStep(pct / 100.0, dir));
			return false;
		}
		switch (b)
		{
		// one page, to realign a spread (a cover alone, a two-page picture split over the hinge)
		case Button::L: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Page, -1, (int64_t)now}); ui.pageReader->mfTagUntil = now + 1500; break;
		case Button::R: ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Page, 1, (int64_t)now}); ui.pageReader->mfTagUntil = now + 1500; break;
		case Button::A: if (ui.pageReader->readerStatus.failedHere) ui.pageReader->mangaRetry(); break;
		case Button::X: mangaOpenReadMore(); break;
		case Button::L2: mangaChapterStep(-1, p); break;
		case Button::R2: mangaChapterStep(1, p); break;
		// SELECT: fullscreen, the usual way up (Book goes)
		case Button::Select:
			mangaSetBook(MANGA_BOOK_OFF, p);
			if (!ui.pageReader->readerStatus.fullscreen) mangaFullscreen(true, p);
			break;
		case Button::Start: mangaOpenView(); break;
		case Button::Y: ui.pageReader->mangaPagesOpen(p); break;
		// B goes back to the chapters; Book stays the series' way to read (the menu's View leaves it)
		case Button::B: mangaCloseReader(); break;
		default: break;
		}
		return false;
	}

	void Ui::Manga::drawMangaBookBar(const Player& p)
	{
		lay::BookBar b = lay::bookBar();
		KitNew::BookSpec s;
		int pages = ui.pageReader->readerStatus.pages, pg = ui.pageReader->readerStatus.page;
		s.title = !ui.pageReader->mangaReadChapterTitle.empty() ? ui.pageReader->mangaReadChapterTitle
			: mangaTitle(ui.pageReader->mangaReadSeriesId);
		if (pages > 0)
		{
			s.value = std::to_string(pg + 1);
			if (pg + 1 < pages) s.value += "\xE2\x80\x93" + std::to_string(pg + 2);
			s.share = pages > 1 ? pg / (double)(pages - 1) : 0;
		}
		s.note = pages > 0 ? "of " + std::to_string(pages) : "";
		s.keys[0] = {&G_PAGE, "Pages", "Y", ""};
		s.keys[1] = {&G_EYE, "View", "START", ""};
		s.keys[2] = {&G_SUN, "Light", "", ui.lightNote(p)};
		s.keys[3] = {&G_NEXT, "Next chapter", "R2", ""};
		s.leave = "One screen";
		s.leaveKey = "B";
		s.hints = {{"UP/DOWN", "Turn"}, {"LEFT/RIGHT", "Zoom"}, {"L/R", "One page"}, {"X", "More"}};
		s.topLeft = ui.pageReader->readerStatus.book == MANGA_BOOK_LEFT;
		s.pressed = mangaBookBarPressed;
		KitNew::bookBar(*ui.kit, b, s);
	}

	bool Ui::Manga::mangaBookBarPointer(const Pointer& pt, int x, int y, Player& p)
	{
		lay::BookBar b = lay::bookBar();
		bool topLeft = ui.pageReader->readerStatus.book == MANGA_BOOK_LEFT;
		int ux = 0, uy = 0;
		lay::uprightPoint(x, y, topLeft, ux, uy);
		int at = lay::bookBarAt(b, ux, uy);
		auto seek = [&] {
			int pages = ui.pageReader->readerStatus.pages;
			if (pages <= 1) return;
			double share = std::clamp((ux - b.slider.x) / (double)std::max(1, b.slider.w), 0.0, 1.0);
			ui.pageReader->mangaSeek((int)std::lround(share * (pages - 1)));
		};
		if (pt.kind == Pointer::Down)
		{
			mangaBookBarPressed = at >= 0 && at <= 4 ? at : -1;
			if (at == 5) { mangaBookBarPressed = 5; seek(); }
			else if (at < 0) mangaBookBar = false;	// a tap off the sheet puts it away
			return false;
		}
		if (pt.kind == Pointer::Move && mangaBookBarPressed == 5)
		{
			seek();
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			int was = mangaBookBarPressed;
			mangaBookBarPressed = -1;
			if (was >= 0 && was <= 4 && at == was) mangaBookBarAct(was, p);
		}
		return false;
	}

	void Ui::Manga::mangaBookBarAct(int which, Player& p)
	{
		mangaBookBar = false;
		switch (which)
		{
		case 0: ui.pageReader->mangaPagesOpen(p); break;
		case 1: mangaOpenView(); break;
		case 2: ui.openLight(); break;
		case 3: mangaChapterStep(1, p); break;
		case 4: mangaSetBook(MANGA_BOOK_OFF, p); break;	// One screen: leave Book, stay in the chapter
		default: break;
		}
	}

	void Ui::Manga::mangaSetMode(int m, Player& p)
	{
		// kept for this series (the setting stays for the others)
		(void)p;
		if (m < 0 || m >= MANGA_MODES || m == ui.pageReader->readerStatus.modeSet) return;
		ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Mode, (double)m, (int64_t)SDL_GetTicks()});
		if (mangaStore)
			if (const MangaSeries* ms = mangaStore->series(ui.pageReader->mangaReadSeriesId))
			{
				MangaSeries s = *ms;
				s.readingMode = mangaReadingModeId(m, ui.pageReader->readerStatus.book);	// (the layout kept: Book stays Book)
				mangaStore->putSeries(s);
				mangaSave();
			}
	}

	bool Ui::Manga::mangaFullTapTurn(int x, int y, Player& p)
	{
		bool strip = ui.pageReader->readerStatus.mode == MANGA_STRIP;
		int edge = lay::readerFullTap(x, y, !strip);
		if (!edge) return false;
		// a page's sides are LEFT and RIGHT themselves (Swap Left/Right and the chapter's end as the keys have them);
		// down a strip the bottom is forward, whichever key that is
		bool right = strip ? (edge > 0) != p.settings.mangaSwapLR : edge > 0;
		mangaKey(right ? Button::Right : Button::Left, p);
		return true;
	}

	void Ui::Manga::mangaFullscreen(bool on, Player& p, bool asked)
	{
		Uint32 now = SDL_GetTicks();
		ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, on ? 1.0 : 0.0, (int64_t)now, (double)p.settings.mangaHinge});
		ui.pageReader->mfTagUntil = on ? now + 2500 : 0;
		mangaBookToast = 0;
		if (!on) mangaCtrlUntil = 0;
		// how to leave it, here in the panel's middle: each time SELECT or the Fullscreen key turns it on (the panel
		// is all page then, so the way back is said), and the first time ever a chapter opens in it by itself
		if (on && (asked || !p.settings.mangaFullSeen)) mangaToastUntil = now + MG_TOAST_MS;
		if (on && !p.settings.mangaFullSeen)
		{
			Settings s = p.settings;
			s.mangaFullSeen = true;
			p.setSettings(s);
		}
	}

	void Ui::Manga::drawMangaBookTag(int alpha)
	{
		if (alpha <= 0) return;
		int a = ui.pageReader->readerStatus.page + 1, n = ui.pageReader->readerStatus.pages;
		int b = std::min(a + std::max(1, ui.pageReader->readerStatus.bookCount) - 1, n);
		std::string ch = mangaReadChapterTag();
		std::string tag = (ch.empty() ? "" : ch + " \xC2\xB7 ") + (a == b ? std::to_string(a) : std::to_string(a) + "\xE2\x80\x93" + std::to_string(b)) + " / " + std::to_string(n);
		if (ui.pageReader->readerStatus.atEnd)
		{
			int ci = mangaReadChapterIndex();
			bool next = ci >= 0 && ci + 1 < mangaReadChapterTotal();
			std::string nextName = next ? mangaReadChapterName(ci + 1) : "";
			tag += next ? " \xC2\xB7 End of chapter: turn once more for " + (nextName.empty() ? std::string("the next") : nextName)
				: " \xC2\xB7 You\xE2\x80\x99re caught up";
		}
		SDL_Texture* t = ui.text(tag, 0, TEXT);
		if (!t) return;
		int tw, th;
		SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
		// the chip, in units: turned, the text's length runs down the panel
		int len = (int)std::lround(tw / sy) + 16, thick = (int)std::lround(th / sx) + 8;
		bool left = ui.pageReader->readerStatus.book != MANGA_BOOK_RIGHT;
		int cx = left ? 12 + thick / 2 : W - 12 - thick / 2, cy = H / 2;
		SDL_Color chip = SURFACE;
		chip.a = (Uint8)(chip.a * alpha / 255);
		ui.fillRound({cx - thick / 2, cy - len / 2, thick, len}, chip, R_CHIP);
		// the text upright as held: a quarter turn clockwise held top-left (the panel's right edge is up), back held top-right
		SDL_Rect dst{(int)std::lround(cx * sx) - tw / 2, (int)std::lround(cy * sy) - th / 2, tw, th};
		SDL_SetTextureAlphaMod(t, (Uint8)alpha);
		SDL_RenderCopyEx(ui.ren, t, nullptr, &dst, left ? 90.0 : 270.0, nullptr, SDL_FLIP_NONE);
		SDL_SetTextureAlphaMod(t, 255);	// (the texture is the text cache's: others draw it whole)
	}

	bool Ui::Manga::mangaFullNavOff(int id) const
	{
		const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
		int ci = mangaReadChapterIndex();
		switch (id)
		{
		case PageReader::FN_CH_BACK: return ci <= 0;
		case PageReader::FN_CH_ON: return ci < 0 || ci + 1 >= mangaReadChapterTotal();
		case PageReader::FN_PG_BACK: return st.page <= 0;
		case PageReader::FN_PG_ON: return st.page + 1 >= st.pages;
		case PageReader::FN_PAGES: return st.pages <= 0;
		default: return false;
		}
	}

	void Ui::Manga::drawMangaControls(const Player& p)
	{
		// fullscreen's navigation, as the Files reader's, with the chapter's steps: the series and its page, the
		// page slider (a finger there is a Seek), the keys
		PageReader::FullNav n;
		n.title = mangaTitle(ui.pageReader->mangaReadSeriesId);
		n.chapters = true;
		n.slider = SL_MANGA;
		n.chapter = mangaReadChapterTag();
		for (int id = 0; id < PageReader::FN_KEYS; id++) n.off[id] = mangaFullNavOff(id);
		n.pressed = mangaCtrlPressed;
		n.caps = p.settings.buttonHotkeys;
		ui.pageReader->drawFullNav(n);
	}

	void Ui::Manga::drawMangaToast()
	{
		// how to leave fullscreen, each time it is turned on: a line in the panel's middle for 2 s
		std::string text = "Fullscreen \xC2\xB7 SELECT: one screen \xC2\xB7 tap the middle: controls";
		if (mangaBookToast)
			text = std::string("Book \xC2\xB7 turn the handheld: top screen on the ") +
				(mangaBookToast == MANGA_BOOK_RIGHT ? "right" : "left");
		ui.pageReader->drawFullToast(text);
	}

}
