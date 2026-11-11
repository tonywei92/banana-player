#include "ui.h"
#include "manganet.h"
#include "manga_names.h"
#include "manga_ui_logic.h"
#include "ui_internal.h"
#include "cbz.h"
#include "comicinfo.h"
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

	void Ui::mangaDlEnsureCard(bool force)
	{
		Uint32 now = SDL_GetTicks();
		if (!force && mdlCardOn && now - mdlScanAt < 5000) return;	// (a folder walk on the card: not every redraw)
		mdlCardOn = true;
		mdlScanAt = now;
		std::vector<MangaDlCardSeries> found;
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
					// On this handheld's, not downloads (ComicInfo's BPSource / BPSeriesKey say which is which)
					ComicInfo ci;
					int pages = 0;
					Cbz z;
					if (!z.open(path)) continue;
					bool ours = false;
					for (const auto& entry : z.entries())
						if (entry.name == "ComicInfo.xml")
						{
							std::vector<uint8_t> raw;
							if (z.read(entry.name, raw)) ours = parseComicInfo(std::string(raw.begin(), raw.end()), ci);
							break;
						}
					if (!ours || (ci.sourceId.empty() && ci.seriesKey.empty())) continue;
					pages = ci.pageCount > 0 ? ci.pageCount : (int)z.pages().size();
					MangaDlCardChapter ch;
					ch.path = path;
					ch.title = ci.title.empty() ? baseNoExt(name) : ci.title;
					ch.sourceId = ci.sourceId;
					ch.seriesKey = ci.seriesKey;
					ch.number = infoNumber(ci.number);
					if (ch.number < 0) ch.number = chapterNumber(ch.title);
					ch.pages = pages;
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
	}

	uint64_t Ui::mangaDlCardBytes() const
	{
		uint64_t n = 0;
		for (const auto& s : mdlCard) n += s.bytes;
		return n;
	}

	uint64_t Ui::mangaDlFreeBytes() const
	{
		return freeBytesAt(mangaNet ? mangaNet->downloadsRoot() : std::string());
	}

	int Ui::mangaDlQueueCount() const
	{
		if (!mangaNet || !mangaNet->started()) return 0;
		int n = 0;
		for (const auto& it : mangaNet->queue().items())
			if (it.state != DlItem::Done) n++;
		return n;
	}

	const Ui::MangaDlCardChapter* Ui::mangaDlCardFor(const std::string& sourceId, const std::string& seriesId,
		const MangaChapter& c) const
	{
		if (sourceId.empty() || seriesId.empty()) return nullptr;
		std::string title = dlChapterName(c);
		for (const auto& s : mdlCard)
			for (const auto& ch : s.chapters)
			{
				if (ch.sourceId != sourceId || ch.seriesKey != seriesId) continue;
				// the number is the download's own (ComicInfo's), and the title is what writeChapter put there;
				// either can be the one that matches when a source numbers its chapters its own way
				if (ch.number >= 0 && c.number > 0 && ch.number == c.number) return &ch;
				if (ch.title == title) return &ch;
			}
		return nullptr;
	}

	Ui::MangaDlMark Ui::mangaDlMarkFor(const std::string& sourceId, const std::string& seriesId, const MangaChapter& c) const
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

	void Ui::drawMangaDlMark(const MangaDlMark& m, int right, int cy, bool at)
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
			drawGlyphMid(G_DOWNLOAD, right - 10, cy, at ? ACCENT : DIM, 1);
			return;
		default: return;
		}
		drawTextMid(text, 0, ink, right, cy, 104, 1);
	}

	// ---- opening a chapter that is already a .cbz ----

	void Ui::mangaDlOpenCbz(const std::string& seriesId, const std::string& seriesTitle, const std::string& chapterId,
		const std::string& chapterTitle, const std::string& path, int page0, Player& p)
	{
		// the chapter open so far is saved before the next one replaces it in readerStatus
		if (page == Page::MangaRead)
		{
			mangaRecordProgress();
			mangaSave();
		}
		mangaSeriesId = seriesId;
		mangaReadSeriesId = seriesId;
		mangaReadChapterId = chapterId;
		mangaReadChapterTitle = chapterTitle;
		// archive: the reader opens it through cbz.h, no page URLs and so no network
		mangaRead = {seriesId, seriesTitle, chapterId, chapterTitle, path, true, std::max(0, page0),
			mangaModeFor(seriesId, p), p.settings.mangaFit};
		mangaProgPage = readerStatus.page;
		mangaProgPages = readerStatus.pages;
		mangaReadLive = false;
		mangaSliderPage = -1;
		mangaCtrlUntil = 0;
		mangaMenu = false;
		mangaZoomAsked = 100;
		page = Page::MangaRead;
		if (p.settings.mangaOpenFull) mangaFullscreen(true, p);
	}

	bool Ui::mangaDlOpenSourceChapter(const std::vector<MangaChapter>& chapters, int i, int page0,
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

	void Ui::mangaDlOpenCardAt(int seriesIndex, int chapterIndex, Player& p)
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
		if (!ls)
		{
			// a chapter that finished while the app was open: the card's scan is from the app's start, so read it
			// again before saying the series isn't there (closing the reader comes back to that page)
			mangaScan();
			ls = mangaLocalSeries(localId);
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

	void Ui::mangaDownloadsOpen(Player& p)
	{
		(void)p;
		mdlCursor = mdlScroll = 0;
		mdlCardOpen = false;
		mdlCardAt = -1;
		mdlCardCursor = mdlCardScroll = 0;
		mdlDeletePath.clear();
		mdlDeleteAt = 0;
		mangaDlEnsureCard(true);
		// a chapter that finished is on the card now: its memory item would only hide a later re-download
		if (mangaNet && mangaNet->started())
		{
			mangaNet->queue().clearDone();
			mangaNet->queue().save();
		}
		mangaDlAskPump();
	}

	void Ui::mangaDownloadsReset()
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
		mdlLocal++;
	}

	void Ui::mangaDownloadsTick(Player& p)
	{
		(void)p;
		Uint32 now = SDL_GetTicks();
		if (mdlDeleteAt && now - mdlDeleteAt >= QUIT_WINDOW_MS)
		{
			mdlDeleteAt = 0;
			mdlDeletePath.clear();
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

	bool Ui::mangaDownloadsBack()
	{
		if (!mdlCardOpen) return false;
		mdlCardOpen = false;
		mdlCardAt = -1;
		mdlCardCursor = mdlCardScroll = 0;
		mdlDeletePath.clear();
		mdlDeleteAt = 0;
		mdlLocal++;
		return true;
	}

	bool Ui::mangaDownloadsKey(Button b, Player& p)
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
						if (mdlCardAt >= (int)mdlCard.size()) mdlCardAt = (int)mdlCard.size() - 1;
						if (mdlCardAt < 0) mdlCardOpen = false;
						else if (mdlCardCursor >= (int)mdlCard[mdlCardAt].chapters.size())
							mdlCardCursor = std::max(0, (int)mdlCard[mdlCardAt].chapters.size() - 1);
					}
					else p.osd("Couldn't delete the chapter", 2200);
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
					for (const auto& ch : s.chapters)
						if (remove(ch.path.c_str()) == 0) gone++;
					rmdir(s.path.c_str());
					p.osd(gone ? "Deleted " + s.name : "Couldn't delete the series", gone ? 1800 : 2200);
					mdlDeletePath.clear();
					mdlDeleteAt = 0;
					mangaDlEnsureCard(true);
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

	bool Ui::mangaDownloadsPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down) return false;
		mangaDlEnsureCard();
		if (mdlCardOpen)
		{
			if (mdlCardAt < 0 || mdlCardAt >= (int)mdlCard.size()) { mdlCardOpen = false; mdlCardAt = -1; return false; }
			const MangaDlCardSeries& s = mdlCard[mdlCardAt];
			int n = (int)s.chapters.size();
			for (int i = mdlCardScroll; i < n && i < mdlCardScroll + 6; i++)
			{
				Rect r{16, 138 + (i - mdlCardScroll) * 44, 608, 40};
				if (!r.hit(x, y)) continue;
				mdlCardCursor = i;
				mangaDlOpenCardAt(mdlCardAt, i, p);
				return false;
			}
			return false;
		}
		int q = mangaDlQueueCount(), c = (int)mdlCard.size();
		int off = q + (q > 0 ? 1 : 0);
		for (int i = mdlScroll; i < q && i < mdlScroll + MDL_QSHOWN; i++)
		{
			Rect r{16, MDL_TOP + (i - mdlScroll) * MDL_QROW, 608, MDL_QROW - 4};
			if (!r.hit(x, y)) continue;
			mdlCursor = i;
			mangaDownloadsKey(Button::A, p);
			return false;
		}
		if (q > 0 && Rect{16, MDL_ALL_Y, 608, 32}.hit(x, y))
		{
			mdlCursor = q;
			mangaDownloadsKey(Button::A, p);
			return false;
		}
		// the card's rows: right under the summary line when nothing is queued, their number following the room
		int top = mdlCardTop(), shown = mdlCardRows();
		for (int i = mdlCardScroll; i < c && i < mdlCardScroll + shown; i++)
		{
			Rect r{16, top + (i - mdlCardScroll) * MDL_CROW, 608, MDL_CROW - 4};
			if (!r.hit(x, y)) continue;
			mdlCursor = off + i;
			mangaDownloadsKey(Button::A, p);
			return false;
		}
		return false;
	}

	// ---- the Download sheet ----

	void Ui::mangaDlSheetOpen(Player& p)
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
	}

	void Ui::mangaDlSheetClose()
	{
		mdlSheet = MDLS_NONE;
		mdlSheetCursor = 0;
		mdlChosen.clear();
		mdlLocal++;
	}

	Ui::Rect Ui::mangaDlSheetRowRect(int i) const
	{
		Rect b = mangaDlSheetBox();
		if (mdlSheet == MDLS_CHOOSE) return {b.x + 16, b.y + 66 + i * 40, b.w - 32, 36};
		return {b.x + 16, b.y + 74 + i * 46, b.w - 32, 40};
	}

	std::vector<int> Ui::mangaDlUnreadFrom(int from, int limit) const
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

	bool Ui::mangaDlBlocked(const std::string& sourceId, const std::string& seriesId, const MangaChapter& c) const
	{
		return mangaDlMarkFor(sourceId, seriesId, c).kind != MangaDlMark::None;
	}

	void Ui::mangaDlEnqueueChapter(const std::string& sourceId, const std::string& scriptPath, const std::string& seriesId,
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

	int Ui::mangaDlQueueChapters(const std::string& sourceId, const std::string& scriptPath, const std::string& seriesId,
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

	void Ui::mangaDlAskPump()
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

	bool Ui::mangaDlImageAsk()
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

	void Ui::mangaDlYieldToSource()
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

	void Ui::mangaDlAskFinish(bool queued, const std::string& note)
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

	void Ui::mangaDlWorker(MangaWorker::Result& r)
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

	void Ui::mangaDlSheetAct(Player& p)
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

	bool Ui::mangaDlSheetKey(Button b, Player& p)
	{
		if (mdlSheet == MDLS_CHOOSE)
		{
			int n = (int)mangaSrcChapters.size();
			if (n == 0) { mdlSheet = MDLS_MENU; return false; }
			if (mdlChooseCursor >= n) mdlChooseCursor = n - 1;
			if (b == Button::B) { mdlSheet = MDLS_MENU; mdlChosen.clear(); mdlLocal++; return false; }
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
		// the menu: This chapter, Next 5 unread, All unread, Choose chapters…
		if (b == Button::B) { mangaDlSheetClose(); return false; }
		if (b == Button::A) { mangaDlSheetAct(p); return false; }
		switch (b)
		{
		case Button::Up: mdlSheetCursor = (mdlSheetCursor + 3) % 4; mdlLocal++; break;
		case Button::Down: mdlSheetCursor = (mdlSheetCursor + 1) % 4; mdlLocal++; break;
		default: break;
		}
		return false;
	}

	bool Ui::mangaDlSheetPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down) return false;
		if (mdlSheet == MDLS_MENU)
		{
			for (int i = 0; i < 4; i++)
				if (mangaDlSheetRowRect(i).hit(x, y))
				{
					mdlSheetCursor = i;
					mangaDlSheetAct(p);
					return false;
				}
		}
		else if (mdlSheet == MDLS_CHOOSE)
		{
			int n = (int)mangaSrcChapters.size();
			for (int i = mdlChooseScroll; i < n && i < mdlChooseScroll + MDL_CHOOSE_ROWS; i++)
				if (mangaDlSheetRowRect(i - mdlChooseScroll).hit(x, y))
				{
					mdlChooseCursor = i;
					mangaDlSheetKey(Button::A, p);
					return false;
				}
		}
		if (!mangaDlSheetBox().hit(x, y)) mangaDlSheetClose();
		return false;
	}

	void Ui::drawMangaDlSheet(const Player& p)
	{
		(void)p;
		Rect box = mangaDlSheetBox();
		if (mdlSheet == MDLS_CHOOSE)
		{
			drawDialog(box, "Choose chapters");
			int n = (int)mangaSrcChapters.size();
			if (mdlChooseCursor >= n) mdlChooseCursor = std::max(0, n - 1);
			// what is ticked, and the room it needs
			int unknown = 0;
			uint64_t bytes = 0;
			uint64_t avg = 0;
			uint64_t cardBytes = 0, cardPages = 0;
			for (const auto& s : mdlCard)
				for (const auto& c : s.chapters) { cardBytes += c.bytes; cardPages += (uint64_t)std::max(0, c.pages); }
			// a chapter already partly here says what a page of this source costs too
			if (mangaNet && mangaNet->started())
				for (const auto& it : mangaNet->queue().items())
					if (it.state != DlItem::Done && it.done > 0) { cardBytes += it.bytes; cardPages += (uint64_t)it.done; }
			if (cardPages) avg = cardBytes / cardPages;
			for (int i : mdlChosen)
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
				if (pages > 0 && avg) bytes += (uint64_t)pages * avg;
				else unknown++;
			}
			std::string line;
			if (mdlChosen.empty()) line = "Nothing ticked \xC2\xB7 " + formatBytes((long long)mangaDlFreeBytes()) + " free";
			else
			{
				line = bytes ? "About " + formatBytes((long long)bytes) : "Size unknown";
				if (unknown) line += " \xC2\xB7 " + std::to_string(unknown) + " unknown";
				line += " \xC2\xB7 " + formatBytes((long long)mangaDlFreeBytes()) + " free";
			}
			drawTextMid(line, 0, DIM, box.x + box.w / 2, box.y + 50, box.w - 36, 2);
			if (mdlChooseScroll > std::max(0, n - MDL_CHOOSE_ROWS)) mdlChooseScroll = std::max(0, n - MDL_CHOOSE_ROWS);
			if (mdlChooseCursor < mdlChooseScroll) mdlChooseScroll = mdlChooseCursor;
			if (mdlChooseCursor >= mdlChooseScroll + MDL_CHOOSE_ROWS) mdlChooseScroll = mdlChooseCursor - MDL_CHOOSE_ROWS + 1;
			for (int i = mdlChooseScroll; i < n && i < mdlChooseScroll + MDL_CHOOSE_ROWS; i++)
			{
				Rect r = mangaDlSheetRowRect(i - mdlChooseScroll);
				bool at = i == mdlChooseCursor;
				if (at) drawFocus(r);
				int mid = r.y + r.h / 2;
				bool ticked = mdlChosen.count(i) != 0;
				drawGlyphMid(ticked ? G_BOX_ON : G_BOX, r.x + 22, mid, at ? ACCENT : ticked ? TEXT : DIM);
				drawTextMid(dlChapterName(mangaSrcChapters[i]), 1, at ? ACCENT : TEXT, r.x + 44, mid, r.w - 170);
				MangaDlMark m = mangaDlMarkFor(mangaSrcId, mangaSeriesId, mangaSrcChapters[i]);
				drawMangaDlMark(m, r.x + r.w - 8, mid, at);
			}
			drawScrollBar(box.y + 62, 7 * 40 - 4, mdlChooseScroll, MDL_CHOOSE_ROWS, n);
			drawHints({{"A", "Select"}, {"Y", "Download"}, {"B", "Back"}});
			return;
		}
		drawDialog(box, "Download");
		// the highlighted row's size, where known, and the card's room: the design's top-panel line
		std::vector<int> picks;
		if (mdlSheetCursor == 0 && mserCursor >= 0 && mserCursor < (int)mangaSrcChapters.size()) picks.push_back(mserCursor);
		else if (mdlSheetCursor == 1) picks = mangaDlUnreadFrom(std::max(0, mserCursor), 5);
		else if (mdlSheetCursor == 2) picks = mangaDlUnreadFrom(0, 0);
		uint64_t avg = 0, cardBytes = 0, cardPages = 0;
		for (const auto& s : mdlCard)
			for (const auto& c : s.chapters) { cardBytes += c.bytes; cardPages += (uint64_t)std::max(0, c.pages); }
		if (mangaNet && mangaNet->started())
			for (const auto& it : mangaNet->queue().items())
				if (it.state != DlItem::Done && it.done > 0) { cardBytes += it.bytes; cardPages += (uint64_t)it.done; }
		if (cardPages) avg = cardBytes / cardPages;
		uint64_t bytes = 0;
		int unknown = 0;
		for (int i : picks)
		{
			if (i < 0 || i >= (int)mangaSrcChapters.size()) continue;
			std::string cid = sourceItemId(mangaSrcId, mangaSrcChapters[i].key);
			int pages = -1;
			if (mangaNet && mangaNet->started())
				for (const auto& it : mangaNet->queue().items())
					if (it.chapterId == cid && !it.pageUrls.empty()) { pages = (int)it.pageUrls.size(); break; }
			if (pages < 0 && mangaStore)
				if (const ChapterProgress* pr = mangaStore->progress(cid))
					if (pr->pages > 0) pages = pr->pages;
			if (pages > 0 && avg) bytes += (uint64_t)pages * avg;
			else unknown++;
		}
		std::string top = picks.empty() ? "No size yet" : bytes ? "About " + formatBytes((long long)bytes) : "Size unknown";
		if (unknown) top += " \xC2\xB7 " + std::to_string(unknown) + " unknown";
		uint64_t free = mangaDlFreeBytes();
		top += " \xC2\xB7 " + formatBytes((long long)free) + " free";
		drawTextMid(top, 0, free && free < MangaDownloader::kMinFreeBytes ? DANGER : DIM, box.x + box.w / 2, box.y + 54, box.w - 36, 2);
		// the unread count for All unread
		int unread = 0;
		if (mangaStore)
			for (const auto& c : mangaSrcChapters)
			{
				const ChapterProgress* pr = mangaStore->progress(sourceItemId(mangaSrcId, c.key));
				if (!pr || !pr->read) unread++;
			}
		for (int i = 0; i < 4; i++)
		{
			Rect r = mangaDlSheetRowRect(i);
			bool at = i == mdlSheetCursor;
			bool dim = (i == 0 && (mserCursor < 0 || mserCursor >= (int)mangaSrcChapters.size())) ||
				(i == 1 && picks.empty() && i == mdlSheetCursor) || (i == 2 && unread == 0);
			std::string label = MDL_NAMES[i];
			if (i == 2) label += " (" + std::to_string(unread) + ")";
			std::string note;
			if (i == 3) note = "Pick";
			else if (i == 0 && dim) note = "Pick a chapter";
			else
			{
				uint64_t rowBytes = 0;
				int rowUnknown = 0;
				std::vector<int> one = i == 0 && mserCursor >= 0 ? std::vector<int>{mserCursor}
					: i == 1 ? mangaDlUnreadFrom(std::max(0, mserCursor), 5) : mangaDlUnreadFrom(0, 0);
				for (int k : one)
				{
					if (k < 0 || k >= (int)mangaSrcChapters.size()) continue;
					std::string cid = sourceItemId(mangaSrcId, mangaSrcChapters[k].key);
					int pages = -1;
					if (mangaNet && mangaNet->started())
						for (const auto& it : mangaNet->queue().items())
							if (it.chapterId == cid && !it.pageUrls.empty()) { pages = (int)it.pageUrls.size(); break; }
					if (pages < 0 && mangaStore)
						if (const ChapterProgress* pr = mangaStore->progress(cid))
							if (pr->pages > 0) pages = pr->pages;
					if (pages > 0 && avg) rowBytes += (uint64_t)pages * avg;
					else rowUnknown++;
				}
				if (one.empty()) note = "None";
				else if (rowBytes) note = "~" + formatBytes((long long)rowBytes);
				else note = "size unknown";
				if (rowUnknown && rowBytes) note += " +" + std::to_string(rowUnknown);
			}
			const Glyph* g = i == 3 ? &G_CHAPTERS : &G_DOWNLOAD;
			drawDialogRow(r, g, label, at, false, dim ? DIM : TEXT, note, dim ? DIM : SDL_Color{0, 0, 0, 0});
		}
		drawHints({{"A", "Choose"}, {"B", "Close"}});
	}

	// ---- the Downloads tab ----

	void Ui::drawMangaDownloads(const Player& p)
	{
		(void)p;
		if (mdlCardOpen) { drawMangaDlCardSeries(p); return; }
		mangaDlEnsureCard();
		mangaDlAskPump();
		// the storage line: what the downloads take, and what the card has left
		uint64_t bytes = mangaDlCardBytes(), free = mangaDlFreeBytes();
		std::string line = "Downloads " + formatBytes((long long)bytes) + " \xC2\xB7 " + formatBytes((long long)free) + " free";
		drawTextMid(line, 0, free && free < MangaDownloader::kMinFreeBytes ? DANGER : DIM, 16, 108, 592);
		int q = mangaDlQueueCount(), c = (int)mdlCard.size();
		if (q == 0 && c == 0)
		{
			drawEmpty(G_DOWNLOAD, "Nothing downloaded yet",
				"R2 on a source\xE2\x80\x99s series page downloads it to read offline.",
				nullptr);
			drawHints({{"B", "Back"}});
			return;
		}
		// the queue's slot, then Pause all (only while something is queued), then the card's series
		int off = q + (q > 0 ? 1 : 0), total = off + c;
		if (mdlCursor >= total) mdlCursor = total - 1;
		if (mdlCursor < 0) mdlCursor = 0;
		// a chapter that finished leaves the window where it is valid; with nothing queued the card's rows take
		// the room the queue had, so their window is that many rows deep
		int cardRows = mdlCardRows(), cardTop = mdlCardTop();
		if (mdlScroll > std::max(0, q - MDL_QSHOWN)) mdlScroll = std::max(0, q - MDL_QSHOWN);
		if (mdlCardScroll > std::max(0, c - cardRows)) mdlCardScroll = std::max(0, c - cardRows);
		// a page list that failed says so where the queue's label would be; with nothing queued that line says so
		if (!mdlNote.empty()) drawTextMid(elideLeft(mdlNote, 0, 592), 0, DANGER, 16, MDL_NOTE_Y, 592);
		else if (q == 0) drawTextMid("Nothing queued", 0, DIM, 16, MDL_NOTE_Y);
		else
			drawTextMid("Queue \xC2\xB7 " + std::to_string(q) + (q == 1 ? " chapter" : " chapters"),
				0, DIM, 16, MDL_NOTE_Y);
		// the queue: series, chapter, pages done of total, a progress line
		auto items = mangaNet && mangaNet->started() ? mangaNet->queue().items() : std::vector<DlItem>();
		int shown = 0;
		for (const auto& it : items)
		{
			if (it.state == DlItem::Done) continue;
			if (shown < mdlScroll || shown >= mdlScroll + MDL_QSHOWN) { shown++; continue; }
			int y = MDL_TOP + (shown - mdlScroll) * MDL_QROW;
			Rect r{16, y, 608, MDL_QROW - 4};
			bool at = shown == mdlCursor;
			if (at) drawFocus(r);
			int mid = r.y + r.h / 2;
			std::string title = it.seriesTitle.empty() ? it.seriesId : it.seriesTitle;
			int totalPages = (int)it.pageUrls.size();
			std::string state = it.state == DlItem::Paused ? "Paused"
				: it.state == DlItem::Failed ? (it.error.empty() ? "Failed" : it.error)
				: it.state == DlItem::Running ? "Downloading" : "Queued";
			drawTextMid(title, 1, at ? ACCENT : TEXT, 32, mid - 10, 520);
			drawTextMid(it.chapterTitle + " \xC2\xB7 " + std::to_string(it.done) + "/" + std::to_string(totalPages) +
				" pages \xC2\xB7 " + state, 0, it.state == DlItem::Failed ? DANGER : at ? DIM_ON : DIM,
				32, mid + 10, 556);
			int w = totalPages > 0 ? (int)std::lround(560.0 * it.done / totalPages) : 0;
			drawLevel({32, r.y + r.h - 8, 560, 3}, w, TRACK, it.state == DlItem::Failed ? DANGER : at ? ACCENT : DIM);
			shown++;
		}
		drawScrollBar(MDL_TOP, MDL_QSHOWN * MDL_QROW - 4, mdlScroll, MDL_QSHOWN, q);
		// Pause all / Resume all, while anything is queued (after the last finishes there's nothing to pause)
		bool paused = mangaNet && mangaNet->started() && mangaNet->queue().allPaused();
		if (q > 0)
		{
			Rect all{16, MDL_ALL_Y, 608, 32};
			bool onAll = mdlCursor == q;
			if (onAll) drawFocus(all);
			drawGlyphMid(paused ? G_PLAY : G_PAUSE, 40, all.y + all.h / 2, onAll ? ACCENT : DIM);
			drawTextMid(paused ? "Resume all" : "Pause all", 1, onAll ? ACCENT : TEXT, 62, all.y + all.h / 2, 400);
			drawTextMid(paused ? "all paused" : "one at a time", 0, onAll ? DIM_ON : DIM, 608, all.y + all.h / 2, 0, 1);
		}
		// what is on the card, by series: right under the summary when nothing is queued, as many rows as fit
		drawTextMid("On the card" + (c ? " \xC2\xB7 " + std::to_string(c) + " series" : ""),
			0, DIM, 16, q > 0 ? 354 : MDL_CARD_LABEL_C);
		for (int i = mdlCardScroll; i < c && i < mdlCardScroll + cardRows; i++)
		{
			Rect r{16, cardTop + (i - mdlCardScroll) * MDL_CROW, 608, MDL_CROW - 4};
			bool at = mdlCursor == off + i;
			if (at) drawFocus(r);
			int mid = r.y + r.h / 2;
			const MangaDlCardSeries& s = mdlCard[i];
			drawGlyphMid(G_DOWNLOAD, 38, mid, at ? ACCENT : DIM, 1);
			drawTextMid(s.name, 1, at ? ACCENT : TEXT, 56, mid, 400);
			int n = (int)s.chapters.size();
			drawTextMid(std::to_string(n) + (n == 1 ? " chapter" : " chapters") + " \xC2\xB7 " +
				formatBytes((long long)s.bytes), 0, at ? DIM_ON : DIM, 608, mid, 180, 1);
		}
		drawScrollBar(cardTop, cardRows * MDL_CROW - 4, mdlCardScroll, cardRows, c);
		if (mdlDeleteAt && mdlDeletePath.find('/') != std::string::npos)
			drawFooterText("Press X again to delete it", ACCENT);
		else if (mdlCursor < q) drawHints({{"X", "Pause / resume"}, {"B", "Back"}});
		else if (q > 0 && mdlCursor == q) drawHints({{"A", paused ? "Resume all" : "Pause all"}, {"B", "Back"}});
		else drawHints({{"A", "Chapters"}, {"X", "Delete"}, {"B", "Back"}});
	}

	void Ui::drawMangaDlCardSeries(const Player& p)
	{
		(void)p;
		mangaDlEnsureCard();
		if (mdlCardAt < 0 || mdlCardAt >= (int)mdlCard.size())
		{
			mdlCardOpen = false;
			mdlCardAt = -1;
			drawMangaDownloads(p);
			return;
		}
		const MangaDlCardSeries& s = mdlCard[mdlCardAt];
		drawTextMid(s.name, 1, TEXT, 16, 104, 560);
		int n = (int)s.chapters.size();
		drawTextMid(std::to_string(n) + (n == 1 ? " chapter" : " chapters") + " \xC2\xB7 " +
			formatBytes((long long)s.bytes) + " \xC2\xB7 on the card", 0, DIM, 16, 124, 592);
		if (mdlCardCursor >= n) mdlCardCursor = std::max(0, n - 1);
		if (mdlCardScroll > std::max(0, n - 6)) mdlCardScroll = std::max(0, n - 6);
		for (int i = mdlCardScroll; i < n && i < mdlCardScroll + 6; i++)
		{
			Rect r{16, 138 + (i - mdlCardScroll) * 44, 608, 40};
			bool here = i == mdlCardCursor;
			if (here) drawFocus(r);
			int mid = r.y + r.h / 2;
			const MangaDlCardChapter& c = s.chapters[i];
			drawTextMid(c.title, 1, here ? ACCENT : TEXT, 32, mid, 460);
			drawTextMid(formatBytes((long long)c.bytes), 0, here ? DIM_ON : DIM, 608, mid, 120, 1);
		}
		drawScrollBar(138, 6 * 44 - 4, mdlCardScroll, 6, n);
		if (mdlDeleteAt) drawFooterText("Press X again to delete it", ACCENT);
		else drawHints({{"A", "Read"}, {"X", "Delete"}, {"B", "Back"}});
	}

	// ---- signature() ----

	std::string Ui::mangaDlSignature() const
	{
		char buf[300];
		snprintf(buf, sizeof buf, "|dl%llu %d %d %d %d %d %d %d %d %d %d %zu %zu %zu %d %d %d %d",
			(unsigned long long)mdlLocal, mdlCursor, mdlScroll, (int)mdlCardOpen, mdlCardAt, mdlCardCursor, mdlCardScroll,
			mdlSheet, mdlSheetCursor, mdlChooseCursor, mdlChooseScroll, mdlChosen.size(), mdlAsks.size(),
			mdlCard.size(), (int)mdlAskOn, mdlDeleteAt != 0, mdlCardOn, (int)mdlCardStamp);
		std::string s = buf;
		s += "|" + mdlDeletePath + "|" + mdlNote;
		s += "|q" + std::to_string(mangaDlQueueCount());	// (the collapsed queue's room follows it)
		s += "|a" + mdlAsk.chapterId + " " + std::to_string((unsigned long long)mdlPagesSerial) + " " +
			std::to_string((unsigned long long)mdlImageSerial);
		for (int i : mdlChosen) s += "," + std::to_string(i);
		return s;
	}
}
