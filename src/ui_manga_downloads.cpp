#include "ui.h"
#include "manganet.h"
#include "manga_names.h"
#include "manga_ui_logic.h"
#include "ui_internal.h"
#include "ui_reader.h"
#include "ui_manga.h"
#include "ui_kit.h"	// MoreSpec: Downloads' X
#include "cbz.h"
#include "comicinfo.h"
#include "mangaseriescache.h"
#include "format.h"
#include "playlist.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace bp
{
	// ---- the manga reader's Downloads: the queue, what is on the card, and the Download sheet (design 2, 3 and 7) ----
	//
	// The downloader owns the fetching; this page owns the decisions above it: a chapter picked on a source's series
	// page gets its page list from that source's worker first, and only then becomes a queue item. The card is read
	// for the series the downloads already wrote; a chapter found there opens as the .cbz it is, with no network.

	namespace
	{
		const char* const MDL_NAMES[4] = {"This chapter", "Next 5 unread", "All unread", "Choose chapters\xE2\x80\xA6"};
		constexpr int MDL_CHOOSE_ROWS = 7;

		std::string dlChapterName(const MangaChapter& c)
		{
			if (!c.name.empty()) return c.name;
			if (c.number > 0)
			{
				char buf[40];
				snprintf(buf, sizeof buf, "Chapter %g", c.number);
				return buf;
			}
			return "Chapter";
		}

		// a chapter the card holds is this one of the source's list: the number is the download's own (ComicInfo's),
		// the title is what writeChapter put there; either can be the one that matches when a source numbers its
		// chapters its own way
		bool dlNumberMatches(double cardNumber, const MangaChapter& c) { return cardNumber >= 0 && c.number > 0 && cardNumber == c.number; }

		std::string baseNoExt(const std::string& path)
		{
			size_t slash = path.rfind('/');
			std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
			size_t dot = name.rfind('.');
			return dot == std::string::npos ? name : name.substr(0, dot);
		}

		bool cbzName(const std::string& name)
		{
			size_t dot = name.rfind('.');
			if (dot == std::string::npos) return false;
			std::string e = name.substr(dot + 1);
			for (char& ch : e)
				if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
			return e == "cbz";
		}

		double infoNumber(const std::string& text)
		{
			// ComicInfo's Number is text ("10.5", "Extra"): a number only when the whole of it is one
			if (text.empty()) return -1;
			char* end = nullptr;
			double v = strtod(text.c_str(), &end);
			return end && *end == 0 ? v : -1;
		}

		// the downloads folder's free space; a folder not made yet answers through its nearest parent
		uint64_t freeBytesAt(const std::string& dir)
		{
			if (dir.empty()) return 0;
			std::string at = dir;
			for (;;)
			{
				struct statvfs sv;
				if (statvfs(at.c_str(), &sv) == 0) return (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
				if (at == "/") return 0;
				size_t slash = at.find_last_of('/');
				if (slash == std::string::npos) return 0;
				at = slash == 0 ? "/" : at.substr(0, slash);
			}
		}

		uint64_t stampText(uint64_t h, const std::string& s)
		{
			// FNV-1a: the scan's own cheap view of whether the card changed
			for (unsigned char c : s)
			{
				h ^= c;
				h *= 1099511628211ull;
			}
			return h;
		}
	}

	// ---- the card the downloads wrote ----

	void Ui::Manga::mangaDlEnsureCard(bool force)
	{
		Uint32 now = SDL_GetTicks();
		if (mdlCardStale) force = true;	// (files it waited for were read)
		if (!force && mdlCardOn && now - mdlScanAt < 5000) return;	// (a folder walk on the card: not every redraw)
		mdlCardOn = true;
		mdlCardStale = false;
		mdlScanAt = now;
		std::vector<MangaDlCardSeries> found;
		std::vector<ChapterFileFound> misses;
		std::string root = mangaNet ? mangaNet->downloadsRoot() : std::string();
		DIR* top = root.empty() ? nullptr : opendir(root.c_str());
		if (top)
		{
			while (struct dirent* e = readdir(top))
			{
				if (e->d_name[0] == '.') continue;	// . .. and hidden folders
				std::string dir = root + "/" + e->d_name;
				struct stat sb;
				if (stat(dir.c_str(), &sb) != 0 || !S_ISDIR(sb.st_mode)) continue;
				MangaDlCardSeries series;
				series.name = e->d_name;
				series.path = dir;
				DIR* d = opendir(dir.c_str());
				if (!d) continue;
				while (struct dirent* c = readdir(d))
				{
					std::string name = c->d_name;
					if (name.empty() || name[0] == '.' || !cbzName(name)) continue;
					std::string path = dir + "/" + name;
					if (stat(path.c_str(), &sb) != 0 || !S_ISREG(sb.st_mode)) continue;
					// only our own chapters: the same Manga folder may hold the card's own series, and those are
					// On this handheld's, not downloads (ComicInfo's BPSource / BPSeriesKey say which is which).
					// What a file holds comes from the library's index; one not read yet is read on the library's
					// worker and shows when it answers (a big card's every CBZ opened here froze the tab)
					ChapterFileMeta meta;
					if (libWorker)
					{
						if (!libWorker->cached(path, (int64_t)sb.st_size, (int64_t)sb.st_mtime, meta))
						{
							std::string key = path + "\n" + std::to_string((int64_t)sb.st_size) + " " + std::to_string((int64_t)sb.st_mtime);
							if (mdlCardAsked.insert(key).second) misses.push_back({path, (int64_t)sb.st_size, (int64_t)sb.st_mtime, {}});
							continue;
						}
					}
					else meta = readChapterFile(path);
					if (!meta.comicInfo || (meta.ciSourceId.empty() && meta.ciSeriesKey.empty())) continue;
					MangaDlCardChapter ch;
					ch.path = path;
					ch.title = meta.ciTitle.empty() ? baseNoExt(name) : meta.ciTitle;
					ch.sourceId = meta.ciSourceId;
					ch.seriesKey = meta.ciSeriesKey;
					ch.chapterId = meta.ciChapterId;
					ch.number = infoNumber(meta.ciNumber);
					if (ch.number < 0) ch.number = chapterNumber(ch.title);
					ch.pages = meta.ciPageCount > 0 ? meta.ciPageCount : std::max(0, meta.pages);
					ch.bytes = (uint64_t)sb.st_size;
					series.bytes += ch.bytes;
					series.chapters.push_back(std::move(ch));
				}
				closedir(d);
				if (series.chapters.empty()) continue;
				std::sort(series.chapters.begin(), series.chapters.end(), [](const MangaDlCardChapter& a, const MangaDlCardChapter& b) {
					if (a.number >= 0 && b.number >= 0 && a.number != b.number) return a.number < b.number;
					if ((a.number >= 0) != (b.number >= 0)) return a.number >= 0;
					return naturalLess(a.title, b.title);
				});
				found.push_back(std::move(series));
			}
			closedir(top);
		}
		if (libWorker && !misses.empty()) libWorker->files(misses);
		std::sort(found.begin(), found.end(), [](const MangaDlCardSeries& a, const MangaDlCardSeries& b) {
			return naturalLess(a.name, b.name);
		});
		uint64_t stamp = 1469598103934665603ull;
		for (const auto& s : found)
		{
			stamp = stampText(stamp, s.name);
			for (const auto& c : s.chapters)
				stamp = stampText(stamp, c.path + "\n" + std::to_string(c.bytes) + " " + std::to_string(c.pages));
		}
		if (stamp != mdlCardStamp)
		{
			mdlCardStamp = stamp;
			mdlLocal++;
		}
		mdlCard = std::move(found);
		mangaDlLinkCard();
	}

	void Ui::Manga::mangaDlLinkCard()
	{
		if (!mangaStore) return;
		bool changed = false;
		for (const auto& s : mdlCard)
			for (const auto& ch : s.chapters)
			{
				if (!isSourceId(ch.seriesKey) || ch.sourceId.empty()) continue;	// no source to link to: its own place
				std::string chapterId = ch.chapterId;
				if (chapterId.empty())
				{
					// an older download: its source chapter found in the series' list (the page's own when it is
					// open, else the list cached on the card), by title first, then by number
					const std::vector<MangaChapter>* list = nullptr;
					if (ch.seriesKey == mangaSeriesId && mangaSrcChaptersOn) list = &mangaSrcChapters;
					else
					{
						auto it = mdlLinkLists.find(ch.seriesKey);
						if (it == mdlLinkLists.end())
						{
							MangaSeriesCache c;
							if (!mangaSeriesCacheRead(ui.stateDirPath, ch.seriesKey, c)) c.chapters.clear();
							it = mdlLinkLists.emplace(ch.seriesKey, std::move(c.chapters)).first;
						}
						list = &it->second;
					}
					const MangaChapter* hit = nullptr;
					for (const auto& c : *list)
						if (dlChapterName(c) == ch.title) { hit = &c; break; }
					if (!hit)
						for (const auto& c : *list)
							if (dlNumberMatches(ch.number, c)) { hit = &c; break; }
					if (!hit) continue;	// not found (yet): the path keeps its own place
					chapterId = sourceItemId(ch.sourceId, hit->key);
				}
				MangaStore::Link l = mangaStore->linkChapter(ch.path, chapterId, ch.seriesKey);
				if (l == MangaStore::Link::Same || l == MangaStore::Link::Linked) continue;
				// (logged: a place that moved between ids is otherwise invisible until a tick is missing)
				fprintf(stderr, "manga: %s the downloaded copy's place %s into %s\n",
					l == MangaStore::Link::Merged ? "merged" : "moved", ch.path.c_str(), chapterId.c_str());
				changed = true;
			}
		if (changed) mangaDirty = true;
	}

	uint64_t Ui::Manga::mangaDlCardBytes() const
	{
		uint64_t n = 0;
		for (const auto& s : mdlCard) n += s.bytes;
		return n;
	}

	uint64_t Ui::Manga::mangaDlFreeBytes() const
	{
		return freeBytesAt(mangaNet ? mangaNet->downloadsRoot() : std::string());
	}

	int Ui::Manga::mangaDlQueueCount() const
	{
		if (!mangaNet || !mangaNet->started()) return 0;
		int n = 0;
		for (const auto& it : mangaNet->queue().items())
			if (it.state != DlItem::Done) n++;
		return n;
	}

	int Ui::Manga::mangaDlFailedCount() const
	{
		if (!mangaNet || !mangaNet->started()) return 0;
		int n = 0;
		for (const auto& it : mangaNet->queue().items())
			if (it.state == DlItem::Failed) n++;
		return n;
	}

	const Ui::Manga::MangaDlCardChapter* Ui::Manga::mangaDlCardFor(const std::string& sourceId, const std::string& seriesId,
		const MangaChapter& c) const
	{
		if (sourceId.empty() || seriesId.empty()) return nullptr;
		std::string title = dlChapterName(c);
		for (const auto& s : mdlCard)
			for (const auto& ch : s.chapters)
			{
				if (ch.sourceId != sourceId || ch.seriesKey != seriesId) continue;
				if (dlNumberMatches(ch.number, c)) return &ch;
				if (ch.title == title) return &ch;
			}
		return nullptr;
	}

	Ui::Manga::MangaDlMark Ui::Manga::mangaDlMarkFor(const std::string& sourceId, const std::string& seriesId, const MangaChapter& c) const
	{
		MangaDlMark m;
		std::string id = sourceItemId(sourceId, c.key);
		if (mangaNet && mangaNet->started())
		{
			for (const auto& it : mangaNet->queue().items())
			{
				if (it.chapterId != id) continue;
				if (it.state == DlItem::Done) { m.kind = MangaDlMark::OnCard; return m; }
				m.total = (int)it.pageUrls.size();
				m.done = it.done;
				m.kind = it.state == DlItem::Running ? MangaDlMark::Running
					: it.state == DlItem::Paused ? MangaDlMark::Paused
					: it.state == DlItem::Failed ? MangaDlMark::Failed
					: MangaDlMark::Queued;
				return m;
			}
			// its page list is still being asked for: it counts as queued
			if (mdlAskOn && mdlAsk.chapterId == id) { m.kind = MangaDlMark::Queued; return m; }
			for (const auto& a : mdlAsks)
				if (a.chapterId == id) { m.kind = MangaDlMark::Queued; return m; }
		}
		if (mangaDlCardFor(sourceId, seriesId, c)) m.kind = MangaDlMark::OnCard;
		return m;
	}

	void Ui::Manga::drawMangaDlMark(const MangaDlMark& m, int right, int cy, bool at)
	{
		if (m.kind == MangaDlMark::None) return;
		std::string text;
		SDL_Color ink = at ? DIM_ON : DIM;
		switch (m.kind)
		{
		case MangaDlMark::Running:
			text = m.total > 0 ? std::to_string(m.done) + "/" + std::to_string(m.total) : "Downloading\xE2\x80\xA6";
			ink = ACCENT;
			break;
		case MangaDlMark::Queued: text = "Queued"; break;
		case MangaDlMark::Paused: text = "Paused"; break;
		case MangaDlMark::Failed: text = "Failed"; ink = DANGER; break;
		case MangaDlMark::OnCard:
			// an icon, not words: the row's reading progress keeps its place beside it
			ui.drawGlyphMid(G_DOWNLOAD, right - 10, cy, at ? ACCENT : DIM, 1);
			return;
		default: return;
		}
		ui.drawTextMid(text, 0, ink, right, cy, 104, 1);
	}

	// ---- opening a chapter that is already a .cbz ----

	void Ui::Manga::mangaDlOpenCbz(const std::string& seriesId, const std::string& seriesTitle, const std::string& chapterId,
		const std::string& chapterTitle, const std::string& path, int page0, Player& p)
	{
		// the chapter open so far is saved before the next one replaces it in readerStatus
		if (ui.page == Page::MangaRead)
		{
			mangaRecordProgress();
			mangaSave();
		}
		mangaSeriesId = seriesId;
		ui.pageReader->mangaReadSeriesId = seriesId;
		ui.pageReader->mangaReadChapterId = chapterId;
		ui.pageReader->mangaReadChapterTitle = chapterTitle;
		// archive: the reader opens it through cbz.h, no page URLs and so no network
		ui.pageReader->mangaRead = {seriesId, seriesTitle, chapterId, chapterTitle, path, true, std::max(0, page0),
			mangaModeFor(seriesId, p), p.settings.mangaFit};
		ui.pageReader->mangaRead.comicDirection = !mangaModeChosen(seriesId);
		mangaProgPage = ui.pageReader->readerStatus.page;
		mangaProgPages = ui.pageReader->readerStatus.pages;
		mangaReadLive = false;
		ui.pageReader->mangaSliderPage = -1;
		mangaCtrlUntil = 0;
		ui.pageReader->mangaZoomAsked = 100;
		ui.page = Page::MangaRead;
		if (p.settings.mangaOpenFull) mangaFullscreen(true, p);
	}

	bool Ui::Manga::mangaDlOpenSourceChapter(const std::vector<MangaChapter>& chapters, int i, int page0,
		const std::string& seriesId, const std::string& seriesTitle, Player& p)
	{
		if (i < 0 || i >= (int)chapters.size()) return false;
		mangaDlEnsureCard(true);
		std::string full, key;
		splitSourceId(seriesId, full, key);
		const MangaDlCardChapter* card = mangaDlCardFor(full, seriesId, chapters[i]);
		if (!card) return false;
		// a page list still on its way to the reader would open another chapter when it lands
		mangaSourceCancel();
		// the same chapter ids the source's page uses, so its place and read mark carry over
		mangaReadSrcChapters = chapters;
		mangaDlOpenCbz(seriesId, seriesTitle, sourceItemId(full, chapters[i].key), dlChapterName(chapters[i]), card->path,
			page0, p);
		return true;
	}

	void Ui::Manga::mangaDlOpenCardAt(int seriesIndex, int chapterIndex, Player& p)
	{
		if (seriesIndex < 0 || seriesIndex >= (int)mdlCard.size()) return;
		const MangaDlCardSeries& s = mdlCard[seriesIndex];
		if (chapterIndex < 0 || chapterIndex >= (int)s.chapters.size()) return;
		const MangaDlCardChapter& c = s.chapters[chapterIndex];
		int page0 = 0;
		if (mangaStore)
			if (const ChapterProgress* pr = mangaStore->progress(c.path))
				if (!pr->read) page0 = pr->page;
		// A downloaded chapter also shows under On this handheld (the downloads live in the Manga folder), so it
		// opens as that local series where it is one: B from the reader lands on a series page with its chapters.
		// A source-linked one keeps its own synthetic id otherwise.
		std::string seriesId = !c.seriesKey.empty() ? c.seriesKey : "dl:" + s.path;
		std::string seriesTitle = s.name, chapterTitle = c.title;
		std::string localId = "local:" + s.path;
		const LocalSeries* ls = mangaLocalSeries(localId);
		bool listed = false;
		if (ls)
			for (const auto& lc : ls->chapters) listed = listed || lc.path == c.path;
		if (!listed)
		{
			// a chapter that finished while the app was open: the card's listing is from the app's start, so its
			// folder is listed again (names only, at once) before saying it isn't there (closing the reader comes back
			// to that page)
			ls = mangaLocalAddOne(s.path);
		}
		if (ls)
		{
			seriesId = localId;
			seriesTitle = ls->title;
			mangaSeriesFrom = 1;	// B on its page goes to Browse's On this handheld, where it lives
			mBrowseList = true;
			for (const auto& lc : ls->chapters)
				if (lc.path == c.path) { chapterTitle = lc.title; break; }
		}
		mangaReadSrcChapters.clear();
		mangaDlOpenCbz(seriesId, seriesTitle, c.path, chapterTitle, c.path, page0, p);
	}

	// ---- the tab's keys ----

	void Ui::Manga::mangaDownloadsOpen(Player& p)
	{
		(void)p;
		mdlCursor = mdlScroll = 0;
		mdlCardOpen = false;
		mdlCardAt = -1;
		mdlCardCursor = mdlCardScroll = 0;
		mdlDeletePath.clear();
		mdlDeleteAt = 0;
		mdlRemoveId.clear();
		mdlRemoveAt = 0;
		mangaDlEnsureCard(true);
		// a chapter that finished is on the card now: its memory item would only hide a later re-download
		if (mangaNet && mangaNet->started())
		{
			mangaNet->queue().clearDone();
			mangaNet->queue().save();
		}
		mangaDlAskPump();
	}

	void Ui::Manga::mangaDownloadsReset()
	{
		// the app was closed (or reopened): a page list the worker was on is asked again, and no sheet or card
		// series outlives it
		if (mdlAskOn)
		{
			mdlAsks.insert(mdlAsks.begin(), mdlAsk);
			mdlAsk = MangaDlAsk{};
		}
		mdlAskOn = false;
		mdlPagesSerial = mdlImageSerial = 0;
		mdlSheet = MDLS_NONE;
		mdlSheetCursor = 0;
		mdlChosen.clear();
		mdlCardOpen = false;
		mdlCardAt = -1;
		mdlCardCursor = mdlCardScroll = 0;
		mdlCursor = mdlScroll = 0;
		mdlDeletePath.clear();
		mdlDeleteAt = 0;
		mdlRemoveId.clear();
		mdlRemoveAt = 0;
		mdlLocal++;
	}

	void Ui::Manga::mangaDownloadsTick(Player& p)
	{
		(void)p;
		Uint32 now = SDL_GetTicks();
		if (mdlDeleteAt && now - mdlDeleteAt >= QUIT_WINDOW_MS)
		{
			mdlDeleteAt = 0;
			mdlDeletePath.clear();
			mdlLocal++;
		}
		if (mdlRemoveAt && now - mdlRemoveAt >= QUIT_WINDOW_MS)
		{
			mdlRemoveAt = 0;
			mdlRemoveId.clear();
			mdlLocal++;
		}
		if (mdlNoteUntil && now >= mdlNoteUntil) mdlNote.clear();
		// the reader turned off under an ask: keep it for when it is on again (the worker dropped its answer)
		if (mdlAskOn && (!mangaNet || !mangaNet->started()))
		{
			mdlAsks.insert(mdlAsks.begin(), mdlAsk);
			mdlAsk = MangaDlAsk{};
			mdlAskOn = false;
			mdlPagesSerial = mdlImageSerial = 0;
		}
		mangaDlAskPump();
	}

	bool Ui::Manga::mangaDownloadsBack()
	{
		if (!mdlCardOpen) return false;
		mdlCardOpen = false;
		mdlCardAt = -1;
		mdlCardCursor = mdlCardScroll = 0;
		mdlDeletePath.clear();
		mdlDeleteAt = 0;
		mdlRemoveId.clear();
		mdlRemoveAt = 0;
		mdlLocal++;
		return true;
	}

	bool Ui::Manga::mangaDownloadsKey(Button b, Player& p)
	{
		mangaDlEnsureCard();
		if (!mdlCardOpen)
		{
			int q = mangaDlQueueCount();
			// Y: pause or resume the chapter under the cursor, or all on Pause all's row (the base's X)
			if (b == Button::Y)
			{
				if (q > 0 && mdlCursor <= q) return mangaDlKeyBase(Button::X, p);
				return false;
			}
			if (b == Button::X)
			{
				mangaDlMore(p);
				return false;
			}
		}
		return mangaDlKeyBase(b, p);
	}

	void Ui::Manga::mangaDlMore(Player& p)
	{
		(void)p;
		mangaDlItemsRead();
		int q = (int)mdlItems.size(), off = q + (q > 0 ? 1 : 0);
		int at = mdlCursor;
		fprintf(stderr, "manga: Downloads' More on row %d (%d queued, %zu on the card)\n", at, q, mdlCard.size());
		if (q == 0 && mdlCard.empty()) return;	// (nothing to act on: no sheet)
		std::string queueId = at < q ? mdlItems[at].id : "";
		std::string cardPath = at >= off && at - off < (int)mdlCard.size() ? mdlCard[at - off].path : "";
		ui.openMore([this, at, queueId, cardPath](const Player& pl) {
			MoreSpec m;
			mangaDlItemsRead();
			int q = (int)mdlItems.size(), off = q + (q > 0 ? 1 : 0);
			// what the sheet was opened on: gone (finished, deleted), the sheet closes itself
			const DlItem* it = nullptr;
			for (auto& x : mdlItems) if (x.id == queueId) it = &x;
			const MangaDlCardSeries* series = nullptr;
			for (auto& s : mdlCard) if (s.path == cardPath) series = &s;
			if ((!queueId.empty() && !it) || (!cardPath.empty() && !series)) return m;
			m.glyph = &G_DOWNLOAD;
			m.title = it ? it->chapterTitle : series ? series->name : std::string("Downloads");
			auto cursorTo = [this](const std::string& qid, const std::string& path) {
				mangaDlItemsRead();
				int q = (int)mdlItems.size(), off = q + (q > 0 ? 1 : 0);
				for (int i = 0; i < q; i++) if (mdlItems[i].id == qid) mdlCursor = i;
				for (int i = 0; i < (int)mdlCard.size(); i++) if (!path.empty() && mdlCard[i].path == path) mdlCursor = off + i;
			};
			if (it)
			{
				bool paused = it->state == DlItem::Paused;
				m.rows.push_back({paused ? &G_PLAY : &G_PAUSE, paused ? "Resume" : "Pause", "", "Y", [this, cursorTo, queueId, cardPath](Player& p) {
					cursorTo(queueId, "");
					mangaDlKeyBase(Button::X, p);
					return false;
				}});
				if (it->state == DlItem::Failed || it->state == DlItem::Waiting)
				{
					// asked: the first choice arms it (the row says so), the second removes
					bool armed = mdlRemoveId == it->id && mdlRemoveAt && SDL_GetTicks() - mdlRemoveAt < QUIT_WINDOW_MS;
					MoreItem r{&G_TRASH, armed ? "Choose again to remove" : "Remove from the queue", "", "", [this, cursorTo, queueId, cardPath](Player& p) {
						cursorTo(queueId, "");
						mangaDlKeyBase(Button::Y, p);
						return false;
					}};
					r.danger = true;
					r.stay = !armed;
					m.rows.push_back(r);
				}
			}
			if (series)
			{
				bool armed = mdlDeletePath == series->path && mdlDeleteAt && SDL_GetTicks() - mdlDeleteAt < QUIT_WINDOW_MS;
				MoreItem r{&G_TRASH, armed ? "Choose again to delete it" : "Delete from the card", "", "", [this, cursorTo, queueId, cardPath](Player& p) {
					cursorTo("", cardPath);
					mangaDlKeyBase(Button::X, p);
					return false;
				}};
				r.danger = true;
				r.stay = !armed;
				m.rows.push_back(r);
			}
			if (q > 0)
			{
				bool paused = mangaNet && mangaNet->started() && mangaNet->queue().allPaused();
				m.rows.push_back({paused ? &G_PLAY : &G_PAUSE, paused ? "Resume all" : "Pause all", "", "", [this](Player& p) {
					mangaDlItemsRead();
					mdlCursor = (int)mdlItems.size();
					mangaDlKeyBase(Button::X, p);
					return false;
				}});
			}
			if (int failed = mangaDlFailedCount())
				m.rows.push_back({&G_TRASH, "Clear failed", std::to_string(failed), "", [this, off](Player& p) {
					// (the base's Y off the queue's rows: the failed ones go)
					mdlCursor = off;
					mangaDlKeyBase(Button::Y, p);
					return false;
				}});
			(void)pl;
			(void)at;
			return m;
		});
	}

	bool Ui::Manga::mangaDlKeyBase(Button b, Player& p)
	{
		mangaDlEnsureCard();
		if (mdlCardOpen)
		{
			if (mdlCardAt < 0 || mdlCardAt >= (int)mdlCard.size()) { mdlCardOpen = false; mdlCardAt = -1; return false; }
			const MangaDlCardSeries& s = mdlCard[mdlCardAt];
			int n = (int)s.chapters.size();
			if (mdlCardCursor >= n) mdlCardCursor = std::max(0, n - 1);
			if (b == Button::A)
			{
				mangaDlOpenCardAt(mdlCardAt, mdlCardCursor, p);
				return false;
			}
			if (b == Button::X && n)
			{
				std::string path = s.chapters[mdlCardCursor].path;
				Uint32 now = SDL_GetTicks();
				if (mdlDeletePath == path && mdlDeleteAt && now - mdlDeleteAt < QUIT_WINDOW_MS)
				{
					if (remove(path.c_str()) == 0)
					{
						p.osd("Deleted " + s.chapters[mdlCardCursor].title, 1800);
						mdlDeletePath.clear();
						mdlDeleteAt = 0;
						mangaDlEnsureCard(true);
						mangaLocalForget({path});	// On this handheld shows the files as they are now
						if (mdlCardAt >= (int)mdlCard.size()) mdlCardAt = (int)mdlCard.size() - 1;
						if (mdlCardAt < 0) mdlCardOpen = false;
						else if (mdlCardCursor >= (int)mdlCard[mdlCardAt].chapters.size())
							mdlCardCursor = std::max(0, (int)mdlCard[mdlCardAt].chapters.size() - 1);
					}
					else ui.alert("Couldn\xE2\x80\x99t delete the chapter", "Is the card read only?");
				}
				else
				{
					mdlDeletePath = path;
					mdlDeleteAt = now;
					mdlLocal++;
				}
				return false;
			}
			if (!n) return false;
			switch (b)
			{
			case Button::Up: mdlCardCursor = (mdlCardCursor + n - 1) % n; break;
			case Button::Down: mdlCardCursor = (mdlCardCursor + 1) % n; break;
			case Button::Left: mdlCardCursor = std::max(0, mdlCardCursor - MDL_CHOOSE_ROWS); break;
			case Button::Right: mdlCardCursor = std::min(n - 1, mdlCardCursor + MDL_CHOOSE_ROWS); break;
			default: break;
			}
			if (mdlCardCursor < mdlCardScroll) mdlCardScroll = mdlCardCursor;
			if (mdlCardCursor >= mdlCardScroll + 6) mdlCardScroll = mdlCardCursor - 5;
			return false;
		}
		int q = mangaDlQueueCount(), c = (int)mdlCard.size();
		// the queue, then Pause all (only while something is queued), then the card's series: with nothing queued
		// the first card row is 0, not an undrawn Pause all
		int off = q + (q > 0 ? 1 : 0), total = off + c;
		if (mdlCursor >= total) mdlCursor = std::max(0, total - 1);
		if (b == Button::A)
		{
			if (mdlCursor < q)
			{
				// A on a queue item does what X does: the touch's tap has to act on the spot
				auto items = mangaNet && mangaNet->started() ? mangaNet->queue().items() : std::vector<DlItem>();
				int at = 0;
				for (auto& it : items)
				{
					if (it.state == DlItem::Done) continue;
					if (at++ != mdlCursor) continue;
					// the item's state is about to change: an armed Y no longer describes it
					mdlRemoveId.clear();
					mdlRemoveAt = 0;
					if (it.state == DlItem::Paused) mangaNet->queue().resume(it.id);
					else if (it.state == DlItem::Failed) mangaNet->queue().retry(it.id);
					else if (mangaNet->downloader().status().runningId == it.id) mangaNet->downloader().cancel(it.id);
					else mangaNet->queue().pause(it.id);
					mangaNet->queue().save();
					break;
				}
				return false;
			}
			if (q > 0 && mdlCursor == q)
			{
				if (!mangaNet || !mangaNet->started()) return false;
				if (mangaNet->queue().allPaused())
				{
					mangaNet->queue().resumeAll();
					p.osd("Downloads resumed", 1500);
				}
				else
				{
					mangaNet->downloader().pauseAll();
					p.osd("Downloads paused", 1500);
				}
				mangaNet->queue().save();
				return false;
			}
			int at = mdlCursor - off;
			if (at >= 0 && at < c)
			{
				mdlCardAt = at;
				mdlCardOpen = true;
				mdlCardCursor = mdlCardScroll = 0;
				mdlDeletePath.clear();
				mdlDeleteAt = 0;
				mdlRemoveId.clear();
				mdlRemoveAt = 0;
				mdlLocal++;
			}
			return false;
		}
		if (b == Button::X)
		{
			if (mdlCursor < q)
			{
				// pause / resume (or retry a failed one), as the design's X does
				auto items = mangaNet && mangaNet->started() ? mangaNet->queue().items() : std::vector<DlItem>();
				int at = 0;
				for (auto& it : items)
				{
					if (it.state == DlItem::Done) continue;
					if (at++ != mdlCursor) continue;
					// the item's state is about to change: an armed Y no longer describes it
					mdlRemoveId.clear();
					mdlRemoveAt = 0;
					if (it.state == DlItem::Paused) mangaNet->queue().resume(it.id);
					else if (it.state == DlItem::Failed) mangaNet->queue().retry(it.id);
					else if (mangaNet->downloader().status().runningId == it.id) mangaNet->downloader().cancel(it.id);
					else mangaNet->queue().pause(it.id);
					mangaNet->queue().save();
					break;
				}
				return false;
			}
			if (q > 0 && mdlCursor == q)
			{
				if (!mangaNet || !mangaNet->started()) return false;
				if (mangaNet->queue().allPaused()) mangaNet->queue().resumeAll();
				else mangaNet->downloader().pauseAll();
				mangaNet->queue().save();
				return false;
			}
			int at = mdlCursor - off;
			if (at >= 0 && at < c)
			{
				const MangaDlCardSeries& s = mdlCard[at];
				Uint32 now = SDL_GetTicks();
				if (mdlDeletePath == s.path && mdlDeleteAt && now - mdlDeleteAt < QUIT_WINDOW_MS)
				{
					int gone = 0;
					std::vector<std::string> deleted{s.path};
					for (const auto& ch : s.chapters)
						if (remove(ch.path.c_str()) == 0)
						{
							gone++;
							deleted.push_back(ch.path);
						}
					// (the folder only goes when nothing of the card's own is left in it)
					if (rmdir(s.path.c_str()) != 0) deleted.erase(deleted.begin());
					if (gone) p.osd("Deleted " + s.name, 1800);
					else ui.alert("Couldn\xE2\x80\x99t delete the series", "Is the card read only?");
					mdlDeletePath.clear();
					mdlDeleteAt = 0;
					mangaDlEnsureCard(true);
					mangaLocalForget(deleted);	// (as for a chapter)
					int afterQ = mangaDlQueueCount();
					int after = afterQ + (afterQ > 0 ? 1 : 0) + (int)mdlCard.size();
					if (mdlCursor >= after) mdlCursor = std::max(0, after - 1);
					if (!mdlCard.empty() && mdlCardScroll >= (int)mdlCard.size()) mdlCardScroll = (int)mdlCard.size() - 1;
					if (mdlCard.empty()) mdlCardScroll = 0;
				}
				else
				{
					mdlDeletePath = s.path;
					mdlDeleteAt = now;
					mdlLocal++;
				}
			}
			return false;
		}
		if (b == Button::Y)
		{
			if (!mangaNet || !mangaNet->started()) return false;
			int failed = mangaDlFailedCount();
			if (mdlCursor < q)
			{
				// Remove, for a failed or waiting item: two presses, as the card's delete is. A running or paused
				// one keeps its state's action (X); its place is not lost to a stray Y.
				auto items = mangaNet->queue().items();
				int at = 0;
				for (auto& it : items)
				{
					if (it.state == DlItem::Done) continue;
					if (at++ != mdlCursor) continue;
					if (it.state != DlItem::Failed && it.state != DlItem::Waiting) break;
					Uint32 now = SDL_GetTicks();
					if (mdlRemoveId == it.id && mdlRemoveAt && now - mdlRemoveAt < QUIT_WINDOW_MS)
					{
						std::string name = it.chapterTitle.empty() ? it.chapterId : it.chapterTitle;
						mangaNet->downloader().remove(it.id);
						p.osd("Removed " + name, 1800);
						mdlRemoveId.clear();
						mdlRemoveAt = 0;
						int afterQ = mangaDlQueueCount();
						int after = afterQ + (afterQ > 0 ? 1 : 0) + c;
						if (mdlCursor >= after) mdlCursor = std::max(0, after - 1);
						if (mdlScroll > std::max(0, afterQ - MDL_QSHOWN)) mdlScroll = std::max(0, afterQ - MDL_QSHOWN);
						mdlLocal++;
					}
					else
					{
						mdlRemoveId = it.id;
						mdlRemoveAt = now;
						mdlLocal++;
					}
					break;
				}
				return false;
			}
			// Anywhere else Y clears the failed ones (the page's action, offered while one is failed)
			if (failed)
			{
				mangaNet->downloader().clearFailed();
				p.osd(failed == 1 ? "Removed the failed download"
								  : "Removed " + std::to_string(failed) + " failed downloads",
					1800);
				mdlRemoveId.clear();
				mdlRemoveAt = 0;
				int afterQ = mangaDlQueueCount();
				int after = afterQ + (afterQ > 0 ? 1 : 0) + c;
				if (mdlCursor >= after) mdlCursor = std::max(0, after - 1);
				if (mdlScroll > std::max(0, afterQ - MDL_QSHOWN)) mdlScroll = std::max(0, afterQ - MDL_QSHOWN);
				mdlLocal++;
			}
			return false;
		}
		if (total <= 0) return false;
		// LEFT/RIGHT jump a page of whatever is listed: the queue's three rows, or the card's rows as many as fit
		int pageStep = q > 0 ? (MDL_QSHOWN - 1) : std::max(1, mdlCardRows() - 1);
		switch (b)
		{
		case Button::Up: mdlCursor = (mdlCursor + total - 1) % total; break;
		case Button::Down: mdlCursor = (mdlCursor + 1) % total; break;
		case Button::Left: mdlCursor = std::max(0, mdlCursor - pageStep); break;
		case Button::Right: mdlCursor = std::min(total - 1, mdlCursor + pageStep); break;
		default: break;
		}
		if (mdlCursor < q)
		{
			if (mdlCursor < mdlScroll) mdlScroll = mdlCursor;
			if (mdlCursor >= mdlScroll + MDL_QSHOWN) mdlScroll = mdlCursor - MDL_QSHOWN + 1;
		}
		else if (mdlCursor >= off)
		{
			// the card's rows moved up with the empty queue: their window follows them there
			int at = mdlCursor - off, rows = mdlCardRows();
			if (at < mdlCardScroll) mdlCardScroll = at;
			if (at >= mdlCardScroll + rows) mdlCardScroll = at - rows + 1;
		}
		return false;
	}

	bool Ui::Manga::mangaDownloadsPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down) return false;
		mangaDlEnsureCard();
		// the kit's rows, as the draw laid them out: a drag scrolls, a tap is A on the row, a tap on the focused
		// row's key is that key (Pause / Resume X, Remove or Clear failed Y)
		if (mdlCardOpen)
		{
			if (mdlCardAt < 0 || mdlCardAt >= (int)mdlCard.size()) { mdlCardOpen = false; mdlCardAt = -1; return false; }
			mangaRowsPointer(mlDlCard, p, x, y, [this] {
				return mdlCardOpen && mdlCardAt >= 0 && mdlCardAt < (int)mdlCard.size() ? (int)mdlCard[mdlCardAt].chapters.size() : 0;
			}, &mdlCardScroll, &mdlCardCursor, [this](int i) { return mangaDlCardRowAt(i); }, [this, &p](int i, bool) {
				if (!mdlCardOpen || mdlCardAt < 0 || mdlCardAt >= (int)mdlCard.size()) return;
				if (i < 0 || i >= (int)mdlCard[mdlCardAt].chapters.size()) return;
				mdlCardCursor = i;
				mangaDlOpenCardAt(mdlCardAt, i, p);
			});
			return false;
		}
		mangaDlItemsRead();
		mangaRowsPointer(mlDl, p, x, y, [this] {
			int q = (int)mdlItems.size();
			return q + (q > 0 ? 1 : 0) + (int)mdlCard.size();
		}, &mdlScroll, &mdlCursor, [this](int i) { return mangaDlRowAt(i); }, [this, &p](int i, bool onKey) {
			if (i < 0) return;
			mdlCursor = i;
			MangaRow m = mangaDlRowAt(i);
			if (onKey && m.keyCap == "Y") mangaDownloadsKey(Button::Y, p);
			else mangaDownloadsKey(Button::A, p);
		});
		return false;
	}

	// ---- the Download sheet ----

	void Ui::Manga::mangaDlSheetOpen(Player& p)
	{
		if (mangaSrcChapters.empty())
		{
			p.osd("No chapters to download", 1800);
			return;
		}
		mangaDlEnsureCard(true);
		mdlSheet = MDLS_MENU;
		mdlSheetCursor = 0;
		mdlChosen.clear();
		mdlLocal++;
		mangaDlMenuOpen();
	}

	// the menu (This chapter, Next 5 unread, All unread, Choose chapters): a More sheet over mdlSheet == MDLS_MENU; Choose
	// chapters is a list, a List sheet the page draws
	void Ui::Manga::mangaDlMenuOpen()
	{
		ui.openMore([this](const Player&) { return mangaDlMenuSpec(); }, "mangadl", [this] { return mdlSheet == MDLS_MENU; },
			[this] { if (mdlSheet == MDLS_MENU) mangaDlSheetClose(); }, mdlSheetCursor);
	}

	lay::ListSheet Ui::Manga::mangaDlChooseSheet() const
	{
		return lay::listSheet(std::min(std::max((int)mangaSrcChapters.size(), 1), MDL_CHOOSE_ROWS));
	}

	void Ui::Manga::mangaDlSheetClose()
	{
		mdlSheet = MDLS_NONE;
		mdlSheetCursor = 0;
		mdlChosen.clear();
		mdlLocal++;
	}

	std::vector<int> Ui::Manga::mangaDlUnreadFrom(int from, int limit) const
	{
		std::vector<int> out;
		if (!mangaStore || mangaSrcChapters.empty()) return out;
		int n = (int)mangaSrcChapters.size();
		if (from < 0) from = 0;
		for (int i = from; i < n; i++)
		{
			const MangaChapter& c = mangaSrcChapters[i];
			std::string id = sourceItemId(mangaSrcId, c.key);
			const ChapterProgress* pr = mangaStore->progress(id);
			if (pr && pr->read) continue;
			if (mangaDlBlocked(mangaSrcId, mangaSeriesId, c)) continue;
			out.push_back(i);
			if (limit > 0 && (int)out.size() >= limit) break;
		}
		return out;
	}

	bool Ui::Manga::mangaDlBlocked(const std::string& sourceId, const std::string& seriesId, const MangaChapter& c) const
	{
		return mangaDlMarkFor(sourceId, seriesId, c).kind != MangaDlMark::None;
	}

	void Ui::Manga::mangaDlEnqueueChapter(const std::string& sourceId, const std::string& scriptPath, const std::string& seriesId,
		const std::string& seriesTitle, const MangaChapter& c)
	{
		if (c.key.empty()) return;
		std::string cid = sourceItemId(sourceId, c.key);
		if (mangaDlBlocked(sourceId, seriesId, c)) return;
		MangaDlAsk a;
		a.sourceId = sourceId;
		a.scriptPath = scriptPath;
		a.seriesId = seriesId;
		a.seriesTitle = seriesTitle;
		a.chapterId = cid;
		a.chapterTitle = dlChapterName(c);
		a.chapterJson = c.json;
		mdlAsks.push_back(a);
		mdlLocal++;
		mangaDlAskPump();
	}

	int Ui::Manga::mangaDlQueueChapters(const std::string& sourceId, const std::string& scriptPath, const std::string& seriesId,
		const std::string& seriesTitle, const std::vector<MangaChapter>& chapters)
	{
		// both the Download sheet and the dashboard queue here: one ask per chapter, its page list first
		int added = 0;
		for (const auto& c : chapters)
		{
			int before = (int)mdlAsks.size() + (mdlAskOn ? 1 : 0);
			mangaDlEnqueueChapter(sourceId, scriptPath, seriesId, seriesTitle, c);
			if ((int)mdlAsks.size() + (mdlAskOn ? 1 : 0) > before) added++;
		}
		return added;
	}

	void Ui::Manga::mangaDlAskPump()
	{
		if (mdlAskOn || mdlAsks.empty()) return;
		if (!mangaNet || !mangaNet->started()) return;
		// the worker replaces a waiting job of the same source and kind: a chapter being read keeps its page
		// fetch first, and the download's list is asked once it is done
		if (mangaSrcPagesSerial || mangaSrcImageSerial || mangaJavaPagesSerial) return;
		while (!mdlAsks.empty())
		{
			mdlAsk = mdlAsks.front();
			mdlAsks.erase(mdlAsks.begin());
			if (MangaJava::isSourceId(mdlAsk.sourceId))
			{
				mdlPagesSerial = mangaJavaDlSubmit(mdlAsk);
				if (mdlPagesSerial)
				{
					mdlAskOn = true;
					mdlAskPages.clear();
					mdlAskUrls.clear();
					mdlAskImageAt = 0;
					mdlLocal++;
					return;
				}
				mdlNote = "Couldn\xE2\x80\x99t ask the source for a chapter\xE2\x80\x99s pages";
				mdlNoteUntil = SDL_GetTicks() + 4000;
				mdlAsk = MangaDlAsk{};
				continue;
			}
			MangaWorker::Job job;
			job.sourceId = mdlAsk.sourceId;
			job.scriptPath = mdlAsk.scriptPath;
			job.kind = MangaWorker::Job::Pages;
			job.json = mdlAsk.chapterJson;
			mdlPagesSerial = mangaNet->worker().submit(job);
			if (mdlPagesSerial)
			{
				mdlAskOn = true;
				mdlAskPages.clear();
				mdlAskUrls.clear();
				mdlAskImageAt = 0;
				mdlLocal++;
				return;
			}
			mdlNote = "Couldn\xE2\x80\x99t ask the source for a chapter\xE2\x80\x99s pages";
			mdlNoteUntil = SDL_GetTicks() + 4000;
			mdlAsk = MangaDlAsk{};
		}
	}

	bool Ui::Manga::mangaDlImageAsk()
	{
		if (!mangaNet || !mangaNet->started()) return false;
		while (mdlAskImageAt < (int)mdlAskPages.size() && mdlAskImageAt < (int)mdlAskUrls.size() &&
			!mdlAskPages[mdlAskImageAt].url.empty())
		{
			mdlAskUrls[mdlAskImageAt] = mdlAskPages[mdlAskImageAt].url;
			mdlAskImageAt++;
		}
		if (mdlAskImageAt >= (int)mdlAskPages.size()) return true;
		MangaWorker::Job job;
		job.sourceId = mdlAsk.sourceId;
		job.scriptPath = mdlAsk.scriptPath;
		job.kind = MangaWorker::Job::Image;
		job.json = mdlAskPages[mdlAskImageAt].json;
		mdlImageSerial = mangaNet->worker().submit(job);
		return mdlImageSerial != 0;
	}

	void Ui::Manga::mangaDlYieldToSource()
	{
		// the worker replaces a waiting job of the same source and kind, and an answer whose serial is gone is
		// dropped: a chapter about to be read takes the worker, and the download's ask is asked again afterwards
		if (!mdlAskOn) return;
		if (mangaNet && mangaNet->started() && MangaJava::isSourceId(mdlAsk.sourceId) && mdlPagesSerial)
			mangaNet->javaExt().cancelGraphQl(mdlPagesSerial);
		mdlAsks.insert(mdlAsks.begin(), mdlAsk);
		mdlAsk = MangaDlAsk{};
		mdlAskOn = false;
		mdlPagesSerial = mdlImageSerial = 0;
		mdlLocal++;
	}

	void Ui::Manga::mangaDlAskFinish(bool queued, const std::string& note)
	{
		if (queued)
		{
			DlItem it;
			it.id = mdlAsk.seriesId + "\n" + mdlAsk.chapterId;
			it.seriesId = mdlAsk.seriesId;
			it.seriesTitle = mdlAsk.seriesTitle;
			it.chapterId = mdlAsk.chapterId;
			it.chapterTitle = mdlAsk.chapterTitle;
			it.source = mdlAsk.sourceId;
			it.pageUrls = mdlAskUrls;
			it.added = (int64_t)time(nullptr);
			it.state = DlItem::Waiting;
			if (mangaNet && mangaNet->started())
			{
				mangaNet->queue().add(it);
				mangaNet->queue().save();
			}
		}
		if (!note.empty())
		{
			mdlNote = note;
			mdlNoteUntil = SDL_GetTicks() + 4000;
		}
		mdlAsk = MangaDlAsk{};
		mdlAskPages.clear();
		mdlAskUrls.clear();
		mdlAskImageAt = 0;
		mdlAskOn = false;
		mdlPagesSerial = mdlImageSerial = 0;
		mdlLocal++;
		mangaDlAskPump();
	}

	void Ui::Manga::mangaDlWorker(MangaWorker::Result& r)
	{
		if (!r.serial || !mdlAskOn) return;
		if (r.serial == mdlPagesSerial)
		{
			mdlPagesSerial = 0;
			if (!r.ok) { mangaDlAskFinish(false, r.error.empty() ? "A chapter\xE2\x80\x99s pages didn\xE2\x80\x99t come" : r.error); return; }
			if (r.pages.empty()) { mangaDlAskFinish(false, "A chapter has no pages"); return; }
			mdlAskPages = r.pages;
			mdlAskUrls.assign(mdlAskPages.size(), "");
			bool needsImage = false;
			for (const auto& pg : mdlAskPages)
				if (pg.url.empty()) { needsImage = true; break; }
			mdlAskImageAt = 0;
			if (needsImage)
			{
				if (!mangaDlImageAsk()) { mangaDlAskFinish(false, "Couldn\xE2\x80\x99t ask the source for a page"); return; }
			}
			else
			{
				for (size_t i = 0; i < mdlAskPages.size(); i++) mdlAskUrls[i] = mdlAskPages[i].url;
				mangaDlAskFinish(true, "");
			}
			return;
		}
		if (r.serial == mdlImageSerial)
		{
			mdlImageSerial = 0;
			if (!r.ok) { mangaDlAskFinish(false, r.error.empty() ? "A page didn\xE2\x80\x99t come" : r.error); return; }
			if (mdlAskImageAt < (int)mdlAskUrls.size()) mdlAskUrls[mdlAskImageAt] = r.imageUrl;
			mdlAskImageAt++;
			if (mdlAskImageAt < (int)mdlAskPages.size())
			{
				if (!mangaDlImageAsk()) { mangaDlAskFinish(false, "Couldn\xE2\x80\x99t ask the source for a page"); return; }
				return;
			}
			mangaDlAskFinish(true, "");
			return;
		}
	}

	// ---- the sheet's keys and drawing ----

	void Ui::Manga::mangaDlSheetAct(Player& p)
	{
		if (mangaSrcChapters.empty()) { mangaDlSheetClose(); return; }
		std::vector<int> picks;
		if (mdlSheetCursor == 0)
		{
			if (mserCursor >= 0 && mserCursor < (int)mangaSrcChapters.size()) picks.push_back(mserCursor);
		}
		else if (mdlSheetCursor == 1) picks = mangaDlUnreadFrom(std::max(0, mserCursor), 5);
		else if (mdlSheetCursor == 2) picks = mangaDlUnreadFrom(0, 0);
		else
		{
			mdlSheet = MDLS_CHOOSE;
			mdlChooseCursor = std::clamp(std::max(0, mserCursor), 0, (int)mangaSrcChapters.size() - 1);
			mdlChooseScroll = std::max(0, mdlChooseCursor - MDL_CHOOSE_ROWS + 1);
			mdlChosen.clear();
			mdlLocal++;
			return;
		}
		int added = 0;
		std::string title = mangaSrcInfo.title.empty() ? mangaSrcTitle : mangaSrcInfo.title;
		if (!picks.empty())
		{
			std::vector<MangaChapter> chosen;
			for (int i : picks)
				if (i >= 0 && i < (int)mangaSrcChapters.size()) chosen.push_back(mangaSrcChapters[i]);
			added = mangaDlQueueChapters(mangaSrcId, mangaSrcFile, mangaSeriesId, title, chosen);
		}
		mangaDlSheetClose();
		if (added) p.osd(added == 1 ? "Download queued" : std::to_string(added) + " chapters queued", 1800);
		else p.osd("Nothing to download: already here or queued", 2200);
	}

	bool Ui::Manga::mangaDlSheetKey(Button b, Player& p)
	{
		if (mdlSheet == MDLS_CHOOSE)
		{
			int n = (int)mangaSrcChapters.size();
			if (n == 0) { mdlSheet = MDLS_MENU; mangaDlMenuOpen(); return false; }
			if (mdlChooseCursor >= n) mdlChooseCursor = n - 1;
			if (b == Button::B) { mdlSheet = MDLS_MENU; mdlSheetCursor = 3; mdlChosen.clear(); mdlLocal++; mangaDlMenuOpen(); return false; }
			if (b == Button::A)
			{
				if (mdlChosen.count(mdlChooseCursor)) mdlChosen.erase(mdlChooseCursor);
				else mdlChosen.insert(mdlChooseCursor);
				mdlLocal++;
				return false;
			}
			if (b == Button::Y)
			{
				std::string title = mangaSrcInfo.title.empty() ? mangaSrcTitle : mangaSrcInfo.title;
				std::vector<MangaChapter> chosen;
				for (int i : mdlChosen)
					if (i >= 0 && i < n) chosen.push_back(mangaSrcChapters[i]);
				int added = mangaDlQueueChapters(mangaSrcId, mangaSrcFile, mangaSeriesId, title, chosen);
				mangaDlSheetClose();
				if (added) p.osd(added == 1 ? "Download queued" : std::to_string(added) + " chapters queued", 1800);
				else p.osd("Nothing to download: already here or queued", 2200);
				return false;
			}
			switch (b)
			{
			case Button::Up: mdlChooseCursor = (mdlChooseCursor + n - 1) % n; break;
			case Button::Down: mdlChooseCursor = (mdlChooseCursor + 1) % n; break;
			case Button::Left: mdlChooseCursor = std::max(0, mdlChooseCursor - MDL_CHOOSE_ROWS); break;
			case Button::Right: mdlChooseCursor = std::min(n - 1, mdlChooseCursor + MDL_CHOOSE_ROWS); break;
			default: break;
			}
			if (mdlChooseCursor < mdlChooseScroll) mdlChooseScroll = mdlChooseCursor;
			if (mdlChooseCursor >= mdlChooseScroll + MDL_CHOOSE_ROWS) mdlChooseScroll = mdlChooseCursor - MDL_CHOOSE_ROWS + 1;
			return false;
		}
		// (the menu is a sheet that takes its own keys)
		return false;
	}

	bool Ui::Manga::mangaDlSheetPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down) return false;
		if (mdlSheet == MDLS_CHOOSE)
		{
			// the rows follow a finger (a smooth list); a tap ticks its row, as A
			lay::ListSheet sh = mangaDlChooseSheet();
			int n = (int)mangaSrcChapters.size();
			if (mangaSheetPointer(mlDlChoose, sh, n, MDL_CHOOSE_ROWS, x, y, [this, &p](int i) {
				if (mdlSheet != MDLS_CHOOSE || i < 0 || i >= (int)mangaSrcChapters.size()) return;
				mdlChooseCursor = i;
				mangaDlSheetKey(Button::A, p);
			}))
				return false;
		}
		if (!mangaDlChooseSheet().box.hit(x, y)) mangaDlSheetClose();
		return false;
	}

	namespace
	{
		// (a size the sheets quote: the card's pages and bytes say what one page of this source costs)
		struct DlSize { uint64_t bytes = 0; int unknown = 0; };
	}

	void Ui::Manga::drawMangaDlSheet(const Player& p)
	{
		(void)p;
		// Choose chapters: a List sheet, a tick on each, the room it needs in its header
		if (mdlSheet != MDLS_CHOOSE) return;
		Kit& k = *ui.kit;
		int n = (int)mangaSrcChapters.size();
		if (mdlChooseCursor >= n) mdlChooseCursor = std::max(0, n - 1);
		uint64_t cardBytes = 0, cardPages = 0, avg = 0;
		for (const auto& s : mdlCard)
			for (const auto& c : s.chapters) { cardBytes += c.bytes; cardPages += (uint64_t)std::max(0, c.pages); }
		// a chapter already partly here says what a page of this source costs too
		if (mangaNet && mangaNet->started())
			for (const auto& it : mangaNet->queue().items())
				if (it.state != DlItem::Done && it.done > 0) { cardBytes += it.bytes; cardPages += (uint64_t)it.done; }
		if (cardPages) avg = cardBytes / cardPages;
		auto sizeOf = [&](const std::vector<int>& list) {
			DlSize out;
			for (int i : list)
			{
				if (i < 0 || i >= n) continue;
				std::string cid = sourceItemId(mangaSrcId, mangaSrcChapters[i].key);
				int pages = -1;
				if (mangaNet && mangaNet->started())
					for (const auto& it : mangaNet->queue().items())
						if (it.chapterId == cid && !it.pageUrls.empty()) { pages = (int)it.pageUrls.size(); break; }
				if (pages < 0 && mangaStore)
					if (const ChapterProgress* pr = mangaStore->progress(cid))
						if (pr->pages > 0) pages = pr->pages;
				if (pages > 0 && avg) out.bytes += (uint64_t)pages * avg;
				else out.unknown++;
			}
			return out;
		};
		std::string line;
		if (mdlChosen.empty()) line = "Nothing ticked \xC2\xB7 " + formatBytes((long long)mangaDlFreeBytes()) + " free";
		else
		{
			DlSize sz = sizeOf(std::vector<int>(mdlChosen.begin(), mdlChosen.end()));
			line = sz.bytes ? "About " + formatBytes((long long)sz.bytes) : "Size unknown";
			if (sz.unknown) line += " \xC2\xB7 " + std::to_string(sz.unknown) + " unknown";
			line += " \xC2\xB7 " + formatBytes((long long)mangaDlFreeBytes()) + " free";
		}
		lay::ListSheet sh = mangaDlChooseSheet();
		k.sheet(sh, {&G_DOWNLOAD, "Choose chapters", line, false});
		mangaSheetRows(mlDlChoose, sh, n, MDL_CHOOSE_ROWS, mdlChooseScroll, mdlChooseCursor, [this](int i) {
			SheetRow r;
			r.glyph = mdlChosen.count(i) ? &G_BOX_ON : &G_BOX;
			r.name = dlChapterName(mangaSrcChapters[i]);
			MangaDlMark m = mangaDlMarkFor(mangaSrcId, mangaSeriesId, mangaSrcChapters[i]);
			switch (m.kind)
			{
			case MangaDlMark::Running: r.note = m.total > 0 ? std::to_string(m.done) + "/" + std::to_string(m.total) : "Downloading\xE2\x80\xA6"; break;
			case MangaDlMark::Queued: r.note = "Queued"; break;
			case MangaDlMark::Paused: r.note = "Paused"; break;
			case MangaDlMark::Failed: r.note = "Failed"; r.danger = true; break;
			case MangaDlMark::OnCard: r.note = "On card"; break;
			default: break;
			}
			return r;
		});
		k.hints({{"A", "Select"}, {"Y", "Download"}, {"B", "Back"}});
	}

	Ui::MoreSpec Ui::Manga::mangaDlMenuSpec()
	{
		MoreSpec m;
		if (mdlSheet != MDLS_MENU) return m;
		m.glyph = &G_DOWNLOAD;
		m.title = "Download";
		// the card's room, and what a page of this source costs (the card's pages and bytes say)
		uint64_t avg = 0, cardBytes = 0, cardPages = 0;
		for (const auto& s : mdlCard)
			for (const auto& c : s.chapters) { cardBytes += c.bytes; cardPages += (uint64_t)std::max(0, c.pages); }
		if (mangaNet && mangaNet->started())
			for (const auto& it : mangaNet->queue().items())
				if (it.state != DlItem::Done && it.done > 0) { cardBytes += it.bytes; cardPages += (uint64_t)it.done; }
		if (cardPages) avg = cardBytes / cardPages;
		m.note = formatBytes((long long)mangaDlFreeBytes()) + " free";
		int unread = 0;
		if (mangaStore)
			for (const auto& c : mangaSrcChapters)
			{
				const ChapterProgress* pr = mangaStore->progress(sourceItemId(mangaSrcId, c.key));
				if (!pr || !pr->read) unread++;
			}
		for (int i = 0; i < 4; i++)
		{
			MoreItem it;
			it.glyph = i == 3 ? &G_CHAPTERS : &G_DOWNLOAD;
			it.label = MDL_NAMES[i];
			if (i == 2) it.label = "All unread (" + std::to_string(unread) + ")";
			bool noPick = i == 0 && (mserCursor < 0 || mserCursor >= (int)mangaSrcChapters.size());
			if (i == 3) it.note = "Pick";
			else if (noPick) it.note = "Pick a chapter";
			else
			{
				uint64_t rowBytes = 0;
				int rowUnknown = 0;
				std::vector<int> one = i == 0 ? std::vector<int>{mserCursor}
					: i == 1 ? mangaDlUnreadFrom(std::max(0, mserCursor), 5) : mangaDlUnreadFrom(0, 0);
				for (int c : one)
				{
					if (c < 0 || c >= (int)mangaSrcChapters.size()) continue;
					std::string cid = sourceItemId(mangaSrcId, mangaSrcChapters[c].key);
					int pages = -1;
					if (mangaNet && mangaNet->started())
						for (const auto& q : mangaNet->queue().items())
							if (q.chapterId == cid && !q.pageUrls.empty()) { pages = (int)q.pageUrls.size(); break; }
					if (pages < 0 && mangaStore)
						if (const ChapterProgress* pr = mangaStore->progress(cid))
							if (pr->pages > 0) pages = pr->pages;
					if (pages > 0 && avg) rowBytes += (uint64_t)pages * avg;
					else rowUnknown++;
				}
				if (one.empty()) it.note = "None";
				else if (rowBytes) it.note = "~" + formatBytes((long long)rowBytes);
				else it.note = "size unknown";
				if (rowUnknown && rowBytes) it.note += " +" + std::to_string(rowUnknown);
			}
			it.stay = true;	// (the act closes it, or moves on to Choose chapters)
			it.act = [this, i](Player& p) { mdlSheetCursor = i; mangaDlSheetAct(p); return false; };
			m.rows.push_back(it);
		}
		return m;
	}

	// ---- the Downloads tab ----

	Ui::Manga::MangaRow Ui::Manga::mangaDlRowAt(int i) const
	{
		// the Downloads list (screens.js manga-downloads): the queue (each chapter's pages over a progress line), Pause
		// all / Resume all while anything is queued, then what is on the card by series. The queue is mdlItems, as
		// the draw or the finger last read it
		MangaRow m;
		int q = (int)mdlItems.size(), off = q + (q > 0 ? 1 : 0);
		if (i < q)
		{
			const DlItem& it = mdlItems[i];
			int total = (int)it.pageUrls.size();
			bool removable = it.state == DlItem::Failed || it.state == DlItem::Waiting;
			m.glyph = &G_DOWNLOAD;
			m.name = (it.seriesTitle.empty() ? it.seriesId : it.seriesTitle) + " \xC2\xB7 " + it.chapterTitle;
			std::string state = it.state == DlItem::Paused ? "Paused"
				: it.state == DlItem::Failed ? (it.error.empty() ? "Failed" : it.error)
				: it.state == DlItem::Running ? "Downloading" : "Queued";
			m.second = std::to_string(it.done) + " / " + std::to_string(total) + " pages \xC2\xB7 " + state;
			m.bar = total > 0 ? it.done / (double)total : 0;
			m.barBad = it.state == DlItem::Failed;
			// the row's key: Y pauses or resumes it (a failed one tries again); Remove is in X · More
			(void)removable;
			m.keyLabel = it.state == DlItem::Paused ? "Resume" : it.state == DlItem::Failed ? "Retry" : "Pause";
			m.keyCap = "Y";
			return m;
		}
		if (q > 0 && i == q)
		{
			bool paused = mangaNet && mangaNet->started() && mangaNet->queue().allPaused();
			m.glyph = paused ? &G_PLAY : &G_PAUSE;
			m.name = paused ? "Resume all" : "Pause all";
			m.note = mangaDlFailedCount() ? "Clear failed is in X \xC2\xB7 More" : paused ? "all paused" : "one at a time";
			return m;
		}
		int c = i - off;
		if (c < 0 || c >= (int)mdlCard.size()) return m;
		const MangaDlCardSeries& s = mdlCard[c];
		int n = (int)s.chapters.size();
		m.glyph = &G_BOOK;
		m.name = s.name;
		m.second = "On the card \xC2\xB7 " + std::to_string(n) + (n == 1 ? " chapter" : " chapters");
		m.note = formatBytes((long long)s.bytes);
		return m;
	}

	Ui::Manga::MangaRow Ui::Manga::mangaDlCardRowAt(int i) const
	{
		MangaRow m;
		if (mdlCardAt < 0 || mdlCardAt >= (int)mdlCard.size()) return m;
		const MangaDlCardSeries& s = mdlCard[mdlCardAt];
		if (i < 0 || i >= (int)s.chapters.size()) return m;
		m.glyph = &G_COMIC;
		m.name = s.chapters[i].title;
		m.note = formatBytes((long long)s.chapters[i].bytes);
		return m;
	}

	void Ui::Manga::mangaDlItemsRead() const
	{
		mdlItems.clear();
		if (!mangaNet || !mangaNet->started()) return;
		for (auto& it : mangaNet->queue().items())
			if (it.state != DlItem::Done) mdlItems.push_back(it);
	}

	void Ui::Manga::drawMangaDownloads(const Player& p)
	{
		tabCrumb = "Downloads";
		if (mdlCardOpen) { drawMangaDlCardSeries(p); return; }
		mangaDlEnsureCard();
		mangaDlAskPump();
		mangaDlItemsRead();
		// the storage line is the header's (screens.js: "Downloads · 412 MB on the card · 21 GB free"); a page list
		// that failed says so there instead
		uint64_t bytes = mangaDlCardBytes(), free = mangaDlFreeBytes();
		tabCrumb = "Downloads \xC2\xB7 " + formatBytes((long long)bytes) + " on the card \xC2\xB7 " + formatBytes((long long)free) + " free";
		if (free && free < MangaDownloader::kMinFreeBytes) tabCrumb = "Low on card space \xC2\xB7 " + formatBytes((long long)free) + " free";
		if (!mdlNote.empty()) tabCrumb = mdlNote;
		int q = (int)mdlItems.size(), c = (int)mdlCard.size();
		if (q == 0 && c == 0)
		{
			drawMangaTabEmpty(p, G_DOWNLOAD, "Nothing downloaded yet", "R2 on a source\xE2\x80\x99s series page downloads it to read offline.");
			ui.drawHints({{"B", "Back"}});
			return;
		}
		int off = q + (q > 0 ? 1 : 0), total = off + c;
		if (mdlCursor >= total) mdlCursor = total - 1;
		if (mdlCursor < 0) mdlCursor = 0;
		mangaRows(mlDl, p, total, mdlScroll, mdlCursor, [this](int i) { return mangaDlRowAt(i); });
		// the spec's keys: Y pause / resume, X More (Remove, Pause all, Clear failed, Delete from the card: asked there)
		bool paused = mangaNet && mangaNet->started() && mangaNet->queue().allPaused();
		if (mdlCursor < q) ui.drawHints({{"Y", "Pause / resume"}, {"X", "More"}, {"B", "Back"}});
		else if (q > 0 && mdlCursor == q) ui.drawHints({{"A", paused ? "Resume all" : "Pause all"}, {"X", "More"}, {"B", "Back"}});
		else ui.drawHints({{"A", "Open"}, {"X", "More"}, {"B", "Back"}});
	}

	void Ui::Manga::drawMangaDlCardSeries(const Player& p)
	{
		mangaDlEnsureCard();
		if (mdlCardAt < 0 || mdlCardAt >= (int)mdlCard.size())
		{
			mdlCardOpen = false;
			mdlCardAt = -1;
			drawMangaDownloads(p);
			return;
		}
		// a series on the card: its chapters as rows; its name, count and size are the header's line
		const MangaDlCardSeries& s = mdlCard[mdlCardAt];
		int n = (int)s.chapters.size();
		tabCrumb = s.name + " \xC2\xB7 " + std::to_string(n) + (n == 1 ? " chapter" : " chapters") + " \xC2\xB7 " +
			formatBytes((long long)s.bytes) + " on the card";
		if (mdlCardCursor >= n) mdlCardCursor = std::max(0, n - 1);
		mangaRows(mlDlCard, p, n, mdlCardScroll, mdlCardCursor, [this](int i) { return mangaDlCardRowAt(i); });
		if (mdlDeleteAt) ui.drawFooterText("Press X again to delete it", ACCENT);
		else ui.drawHints({{"A", "Read"}, {"X", "Delete"}, {"B", "Back"}});
	}

	// ---- signature() ----

	std::string Ui::Manga::mangaDlSignature() const
	{
		char buf[300];
		snprintf(buf, sizeof buf, "|dl%llu %d %d %d %d %d %d %d %d %d %d %zu %zu %zu %d %d %d %d",
			(unsigned long long)mdlLocal, mdlCursor, mdlScroll, (int)mdlCardOpen, mdlCardAt, mdlCardCursor, mdlCardScroll,
			mdlSheet, mdlSheetCursor, mdlChooseCursor, mdlChooseScroll, mdlChosen.size(), mdlAsks.size(),
			mdlCard.size(), (int)mdlAskOn, mdlDeleteAt != 0, mdlCardOn, (int)mdlCardStamp);
		std::string s = buf;
		s += "|" + mdlDeletePath + "|" + mdlNote;
		s += "|rm" + mdlRemoveId + (mdlRemoveAt ? "1" : "0");
		s += "|q" + std::to_string(mangaDlQueueCount());	// (the collapsed queue's room follows it)
		s += "|a" + mdlAsk.chapterId + " " + std::to_string((unsigned long long)mdlPagesSerial) + " " +
			std::to_string((unsigned long long)mdlImageSerial);
		for (int i : mdlChosen) s += "," + std::to_string(i);
		return s;
	}
}
