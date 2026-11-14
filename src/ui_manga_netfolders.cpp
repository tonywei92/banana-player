#include "ui.h"
#include "pagebook.h"
#include "manganet.h"
#include "manga_names.h"
#include "netpath.h"
#include "ui_internal.h"
#include "treecopy.h"
#include <sys/time.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>

namespace bp
{
	// ---- Browse › the saved network connections, their folders, and a folder read as a series ----
	//
	// Everything a server says goes through the manga worker (MangaWorker's NetList / NetScan / NetFetch jobs), which
	// has its own thread and pushes one event when an answer waits: no listing and no copy ever blocks a panel. The
	// listing's names are laid out by manganetfolders (bpcore), the same rules the card's Manga folders follow. A
	// chapter is copied into the page cache's own folder first, then opened by the reader as a local folder or CBZ.
	// The series and the progress are kept by the bpnet:// paths; a password only ever lives in the store and in the
	// job that needs it.

	namespace
	{
		// the page's rows, below the app's tabs (the network page draws the path or the series title above them)
		constexpr int MNF_ROW_TOP = 118, MNF_ROWS = 6;
		constexpr size_t MNF_MAX_ROWS = 4000;	// what a Browse page draws: a share with more folders is not a list

		std::string lower(std::string s)
		{
			for (char& c : s) c = (char)std::tolower((unsigned char)c);
			return s;
		}

		std::string extOf(const std::string& name)
		{
			size_t dot = name.rfind('.');
			if (dot == std::string::npos || dot == 0) return "";
			return lower(name.substr(dot + 1));
		}

		bool safeName(const std::string& name)
		{
			return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos &&
				name.find('\\') == std::string::npos;
		}

		// a stable, filesystem-safe name for one chapter's copy (a bpnet path is not a folder)
		std::string fnvHex(const std::string& s)
		{
			uint64_t h = 1469598103934665603ull;
			for (unsigned char c : s)
			{
				h ^= c;
				h *= 1099511628211ull;
			}
			char buf[32];
			snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
			return buf;
		}
	}

	std::vector<NetServer> Ui::mangaNetFoldersServers() const
	{
		// the Files browser's order: the most recently used first, the file's own order for a tie
		std::vector<NetServer> list = netStore ? netStore->servers() : std::vector<NetServer>();
		std::stable_sort(list.begin(), list.end(), [](const NetServer& a, const NetServer& b) {
			return a.lastUsed > b.lastUsed;
		});
		return list;
	}

	int Ui::mangaNetFoldersCount() const
	{
		return (int)mangaNetFoldersServers().size();
	}

	void Ui::mangaNetCancel()
	{
		if (mangaNet && mangaNet->started())
		{
			MangaWorker& w = mangaNet->worker();
			if (mangaNetSerial) w.cancel(mangaNetSerial);
			if (mangaNetFetchSerial) w.cancel(mangaNetFetchSerial);
		}
		mangaNetSerial = mangaNetFetchSerial = 0;
		mangaNetLoading = false;
	}

	void Ui::mangaNetFoldersReset(Player& p)
	{
		(void)p;
		mangaNetCancel();
		mangaNetView = MNF_OFF;
		mangaNetCursor = mangaNetScroll = 0;
		mangaNetServerId.clear();
		mangaNetStart.clear();
		mangaNetDir.clear();
		mangaNetBackDir.clear();
		mangaNetRows.clear();
		mangaNetError.clear();
		mangaNetChapters.clear();
		mangaNetSeriesPath.clear();
		mangaNetSeriesTitle.clear();
		mangaNetPendAt = -1;
		mangaNetPendPage = 0;
		mangaNetPendFull = false;
	}

	void Ui::mangaNetFoldersLeave()
	{
		mangaNetCancel();
		mangaNetView = MNF_OFF;
		mangaNetCursor = mangaNetScroll = 0;
		mangaNetServerId.clear();
		mangaNetStart.clear();
		mangaNetDir.clear();
		mangaNetBackDir.clear();
		mangaNetRows.clear();
		mangaNetError.clear();
		mangaNetChapters.clear();
		mangaNetSeriesPath.clear();
		mangaNetSeriesTitle.clear();
		mangaNetPendAt = -1;
	}

	void Ui::mangaNetFoldersOpen(const std::string& serverId, Player& p)
	{
		(void)p;
		mangaNetCancel();
		const NetServer* sv = netStore ? netStore->find(serverId) : nullptr;
		if (!sv)
		{
			mangaNetView = MNF_DIRS;
			mangaNetServerId.clear();
			mangaNetStart.clear();
			mangaNetDir.clear();
			mangaNetBackDir.clear();
			mangaNetRows.clear();
			mangaNetError = "This connection is gone";
			return;
		}
		mangaNetServerId = sv->id;
		mangaNetStart = makeNetPath(sv->id, "/" + sv->share + (sv->share.empty() || sv->folder.empty() ? "" : "/" + sv->folder));
		mangaNetBackDir.clear();
		mangaNetView = MNF_DIRS;
		// the connection's own start folder may itself be a series: the scan opens it as one, else it lists it
		mangaNetScan(mangaNetStart);
	}

	void Ui::mangaNetList(const std::string& dir)
	{
		mangaNetCancel();
		mangaNetDir = dir;
		mangaNetRows.clear();
		mangaNetError.clear();
		mangaNetCursor = mangaNetScroll = 0;
		const NetServer* sv = netStore ? netStore->find(mangaNetServerId) : nullptr;
		if (!sv)
		{
			mangaNetError = "This connection is gone";
			return;
		}
		if (!mangaNet)
		{
			mangaNetError = "The reader\xE2\x80\x99s network side is off";
			return;
		}
		mangaNet->start();
		if (!mangaNet->started())
		{
			mangaNetError = "The reader\xE2\x80\x99s network side is off";
			return;
		}
		MangaWorker::Job job;
		job.kind = MangaWorker::Job::NetList;
		job.sourceId = "net:" + sv->id;	// (not a source; kept apart for the queue's own bookkeeping)
		job.replace = false;
		job.netServer = *sv;
		job.netPassword = netStore->password(*sv);
		job.netPath = dir;
		mangaNetLoading = true;
		mangaNetSerial = mangaNet->worker().submit(job);
		if (!mangaNetSerial)
		{
			mangaNetLoading = false;
			mangaNetError = "Couldn\xE2\x80\x99t ask the server";
		}
	}

	void Ui::mangaNetScan(const std::string& path)
	{
		mangaNetCancel();
		mangaNetDir = path;
		mangaNetRows.clear();
		mangaNetError.clear();
		mangaNetCursor = mangaNetScroll = 0;
		const NetServer* sv = netStore ? netStore->find(mangaNetServerId) : nullptr;
		if (!sv)
		{
			mangaNetError = "This connection is gone";
			return;
		}
		if (!mangaNet)
		{
			mangaNetError = "The reader\xE2\x80\x99s network side is off";
			return;
		}
		mangaNet->start();
		if (!mangaNet->started())
		{
			mangaNetError = "The reader\xE2\x80\x99s network side is off";
			return;
		}
		MangaWorker::Job job;
		job.kind = MangaWorker::Job::NetScan;
		job.sourceId = "net:" + sv->id;
		job.replace = false;
		job.netServer = *sv;
		job.netPassword = netStore->password(*sv);
		job.netPath = path;
		mangaNetLoading = true;
		mangaNetSerial = mangaNet->worker().submit(job);
		if (!mangaNetSerial)
		{
			mangaNetLoading = false;
			mangaNetError = "Couldn\xE2\x80\x99t ask the server";
		}
	}

	void Ui::mangaNetSeriesOpen(const NetMangaSeries& s)
	{
		mangaNetCancel();
		mangaNetView = MNF_SERIES;
		mangaNetSeriesPath = s.path;
		mangaNetSeriesTitle = s.title.empty() ? netBaseName(s.path) : s.title;
		mangaNetChapters = s.chapters;
		mangaNetCursor = mangaNetScroll = 0;
		mangaNetError.clear();
		mangaNetLoading = false;
		mangaNetPendAt = -1;
		page = Page::MangaSeries;
		mangaSeriesId = s.path;
		mangaSeriesFrom = 0;	// (our own key handler takes B: back to the folder it came from)
		mserCursor = -1;
		mserScroll = 0;
		// the store keeps the series by its bpnet:// path; only the reading record lives there, not the files
		if (mangaStore)
		{
			MangaSeries m;
			m.id = s.path;
			m.title = mangaNetSeriesTitle;
			m.source = "network";
			m.cover.clear();
			if (const MangaSeries* old = mangaStore->series(s.path))
			{
				m.added = old->added;
				m.lastRead = old->lastRead;
				m.inLibrary = old->inLibrary;
				m.readingMode = old->readingMode;
			}
			else
			{
				m.added = (int64_t)time(nullptr);
				m.inLibrary = false;	// only the Library's own heart puts it there
			}
			mangaStore->putSeries(m);
			mangaSave();
		}
	}

	std::string Ui::mangaNetCachePath(const NetMangaChapter& c) const
	{
		if (stateDirPath.empty()) return "";
		std::string dir = stateDirPath + "/manga/cache/net/" + fnvHex(c.path);
		if (!c.archive) return dir;
		std::string name = netDecode(baseName(c.path));
		// (its own extension kept: it says which kind of book the reader opens)
		if (!safeName(name) || !isPageBookName(name)) name = "chapter.cbz";
		return dir + "/" + name;
	}

	void Ui::mangaNetTrim(const std::string& keep, const Player& p)
	{
		if (stateDirPath.empty()) return;
		uint64_t limit = mangaCacheBytes(p.settings.mangaCacheSize), pages = mangaPageCache.size();
		mangaNetCacheBytes = trimCacheFolder(stateDirPath + "/manga/cache/net", limit > pages ? limit - pages : 0, keep);
	}

	bool Ui::mangaNetCached(const std::string& localPath, bool archive) const
	{
		if (localPath.empty()) return false;
		struct stat st;
		if (archive) return stat(localPath.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
		DIR* d = opendir(localPath.c_str());
		if (!d) return false;
		bool found = false;
		while (dirent* e = readdir(d))
			if (isPageImage(e->d_name)) { found = true; break; }
		closedir(d);
		return found;
	}

	void Ui::mangaNetChapterOpen(int i, int page0, Player& p)
	{
		if (i < 0 || i >= (int)mangaNetChapters.size()) return;
		const NetMangaChapter& c = mangaNetChapters[i];
		mangaNetError.clear();
		mangaNetPendAt = i;
		mangaNetPendPage = std::max(0, page0);
		mangaNetPendMode = mangaModeFor(mangaNetSeriesPath, p);
		mangaNetPendFit = p.settings.mangaFit;
		mangaNetPendFull = p.settings.mangaOpenFull;
		mangaNetPendHinge = p.settings.mangaHinge;
		std::string local = mangaNetCachePath(c);
		if (mangaNetCached(local, c.archive))
		{
			mangaNetChapterReady(local, p);
			return;
		}
		mangaNetFetchStart(c);
	}

	void Ui::mangaNetFetchStart(const NetMangaChapter& c)
	{
		if (mangaNet && mangaNet->started() && mangaNetFetchSerial) mangaNet->worker().cancel(mangaNetFetchSerial);
		mangaNetFetchSerial = 0;
		mangaNetError.clear();
		std::string dest = mangaNetCachePath(c);
		if (dest.empty())
		{
			mangaNetError = "No state folder for the chapter";
			return;
		}
		const NetServer* sv = netStore ? netStore->find(mangaNetServerId) : nullptr;
		if (!sv || !mangaNet)
		{
			mangaNetError = "This connection is gone";
			return;
		}
		mangaNet->start();
		if (!mangaNet->started())
		{
			mangaNetError = "The reader\xE2\x80\x99s network side is off";
			return;
		}
		MangaWorker::Job job;
		job.kind = MangaWorker::Job::NetFetch;
		job.sourceId = "net:" + sv->id;
		job.replace = false;
		job.netServer = *sv;
		job.netPassword = netStore->password(*sv);
		job.netPath = c.path;
		job.netDest = dest;
		job.netArchive = c.archive;
		mangaNetLoading = true;
		mangaNetFetchSerial = mangaNet->worker().submit(job);
		if (!mangaNetFetchSerial)
		{
			mangaNetLoading = false;
			mangaNetError = "Couldn\xE2\x80\x99t ask the server";
		}
	}

	void Ui::mangaNetChapterReady(const std::string& localPath, const Player& p)
	{
		if (mangaNetPendAt < 0 || mangaNetPendAt >= (int)mangaNetChapters.size()) return;
		const NetMangaChapter& c = mangaNetChapters[mangaNetPendAt];
		// the chapter open so far is saved before the next one replaces it in readerStatus
		if (page == Page::MangaRead)
		{
			mangaRecordProgress();
			mangaSave();
		}
		mangaSeriesId = mangaNetSeriesPath;
		mangaReadSeriesId = mangaNetSeriesPath;
		mangaReadChapterId = c.path;
		mangaReadChapterTitle = c.title;
		// path: the local copy the worker made (a folder of pictures or an archive); the ids stay the bpnet://
		// paths, so progress and the store never see a machine's cache folder
		mangaRead = {mangaNetSeriesPath, mangaNetSeriesTitle, c.path, c.title, localPath, c.archive, mangaNetPendPage,
			mangaNetPendMode, mangaNetPendFit};
		// its folder marked as just used (the cache's oldest go first), then the cache held under the cap
		{
			std::string item = c.archive ? localPath.substr(0, localPath.rfind('/')) : localPath;
			utimes(item.c_str(), nullptr);
			mangaNetTrim(item, p);
		}
		mangaProgPage = readerStatus.page;
		mangaProgPages = readerStatus.pages;
		mangaReadLive = false;
		mangaSliderPage = -1;
		mangaCtrlUntil = 0;
		mangaMenu = false;
		mangaZoomAsked = 100;
		mangaPages = false;
		page = Page::MangaRead;
		// Open fullscreen: the command SELECT sends right after the chapter, with the hinge it was asked with; the
		// setting is written on the next key (mangaKey), where a Player can be changed
		if (mangaNetPendFull)
		{
			Uint32 now = SDL_GetTicks();
			readerCmds.push_back({ReaderCmd::Fullscreen, 1.0, (int64_t)now, (double)mangaNetPendHinge});
			mfTagUntil = now + 2500;
			if (!p.settings.mangaFullSeen) mangaToastUntil = now + MG_TOAST_MS;
			mangaFullSeenPending = !p.settings.mangaFullSeen;
		}
	}

	bool Ui::mangaNetFoldersResult(MangaWorker::Result& r, const Player& p)
	{
		if (mangaNetFetchSerial && r.serial == mangaNetFetchSerial)
		{
			mangaNetFetchSerial = 0;
			mangaNetLoading = false;
			// the page moved on (Manga settings, another tab): the copy is kept for the next A, but the reader isn't
			// opened under a page that didn't ask for it
			if (page != Page::MangaSeries || mangaNetView != MNF_SERIES) return true;
			if (!r.ok || r.netFile.empty())
			{
				mangaNetError = r.error.empty() ? "The chapter didn\xE2\x80\x99t come" : r.error;
				return true;
			}
			mangaNetChapterReady(r.netFile, p);
			return true;
		}
		if (!mangaNetSerial || r.serial != mangaNetSerial) return false;
		if (r.kind != MangaWorker::Job::NetList && r.kind != MangaWorker::Job::NetScan) return false;
		mangaNetSerial = 0;
		mangaNetLoading = false;
		// a listing belongs to Browse: a tab change while it was in flight drops it (the page starts again)
		if (page != Page::Manga || mangaTab != MT_BROWSE || mangaNetView == MNF_OFF)
		{
			mangaNetFoldersLeave();
			return true;
		}
		if (!r.ok)
		{
			mangaNetError = r.error.empty() ? "The server didn\xE2\x80\x99t answer" : r.error;
			return true;
		}
		// a folder laid out as a series opens its page; one that isn't stays a folder to browse
		if (r.kind == MangaWorker::Job::NetScan && !r.netSeries.chapters.empty())
		{
			mangaNetSeriesOpen(r.netSeries);
			return true;
		}
		mangaNetRows.clear();
		NetMangaListing listing = netMangaListing(r.netEntries);
		for (const std::string& d : listing.dirs)
		{
			if (mangaNetRows.size() >= MNF_MAX_ROWS) break;
			MangaNetRow row;
			row.name = d;
			row.path = netJoin(mangaNetDir, d);
			row.dir = true;
			mangaNetRows.push_back(row);
		}
		for (const std::string& a : listing.archives)
		{
			if (mangaNetRows.size() >= MNF_MAX_ROWS) break;
			MangaNetRow row;
			row.name = a;
			row.path = netJoin(mangaNetDir, a);
			row.archive = true;
			mangaNetRows.push_back(row);
		}
		mangaNetCursor = 0;
		mangaNetScroll = 0;
		mangaNetError.clear();
		return true;
	}

	bool Ui::mangaNetFoldersKey(Button b, Player& p)
	{
		if (mangaNetView == MNF_SERIES)
		{
			if (b == Button::Start) { openMangaSettings(); return false; }
			if (b == Button::B)
			{
				// back to the folder the series was found in; one opened from a connection's own start goes back
				// to Browse's rows (there is no folder above it to show)
				page = Page::Manga;
				mangaTab = MT_BROWSE;
				mangaView = 0;
				mBrowseList = false;
				if (mangaNetBackDir.empty())
				{
					mangaNetFoldersLeave();
					return false;
				}
				mangaNetView = MNF_DIRS;
				mangaNetCursor = mangaNetScroll = 0;
				std::string back = mangaNetBackDir;
				mangaNetBackDir.clear();
				mangaNetList(back);
				return false;
			}
			int n = (int)mangaNetChapters.size();
			if (b == Button::A)
			{
				if (!mangaNetLoading && mangaNetCursor >= 0 && mangaNetCursor < n)
				{
					int page0 = 0;
					if (mangaStore)
						if (const ChapterProgress* pr = mangaStore->progress(mangaNetChapters[mangaNetCursor].path))
							if (!pr->read) page0 = pr->page;
					mangaNetChapterOpen(mangaNetCursor, page0, p);
				}
				return false;
			}
			if (b == Button::X && mangaNetCursor >= 0 && mangaNetCursor < n && mangaStore)
			{
				// the chapter's read mark, as X does on the card's series page
				const std::string& cid = mangaNetChapters[mangaNetCursor].path;
				const ChapterProgress* pr = mangaStore->progress(cid);
				mangaStore->markRead(mangaNetSeriesPath, {cid}, !(pr && pr->read));
				mangaSave();
				return false;
			}
			if (!n) return false;
			switch (b)
			{
			case Button::Up: mangaNetCursor = (mangaNetCursor + n - 1) % n; break;
			case Button::Down: mangaNetCursor = (mangaNetCursor + 1) % n; break;
			case Button::Left: mangaNetCursor = std::max(0, mangaNetCursor - MG_ROWS); break;
			case Button::Right: mangaNetCursor = std::min(n - 1, mangaNetCursor + MG_ROWS); break;
			default: break;
			}
			if (mangaNetCursor < mangaNetScroll) mangaNetScroll = mangaNetCursor;
			if (mangaNetCursor >= mangaNetScroll + MNF_ROWS) mangaNetScroll = mangaNetCursor - MNF_ROWS + 1;
			return false;
		}
		if (b == Button::B)
		{
			mangaNetCancel();
			mangaNetBackDir.clear();
			std::string parent = netParent(mangaNetDir);
			if (mangaNetDir.empty() || mangaNetDir == mangaNetStart || parent == mangaNetDir) mangaNetFoldersLeave();
			else mangaNetList(parent);
			return false;
		}
		int n = (int)mangaNetRows.size();
		if (b == Button::A)
		{
			if (mangaNetLoading || mangaNetCursor < 0 || mangaNetCursor >= n) return false;
			const MangaNetRow& row = mangaNetRows[mangaNetCursor];
			mangaNetBackDir = mangaNetDir;
			if (row.dir) mangaNetScan(row.path);
			else if (row.archive)
			{
				// a CBZ/ZIP opened here is a one-chapter series: its page has the one chapter
				NetMangaSeries s;
				s.path = row.path;
				NetMangaChapter c;
				c.title = row.name;
				size_t dot = c.title.rfind('.');
				if (dot != std::string::npos && dot > 0) c.title = c.title.substr(0, dot);
				s.title = c.title;
				c.path = row.path;
				c.archive = true;
				c.pages = -1;
				s.chapters.push_back(c);
				mangaNetSeriesOpen(s);
			}
			return false;
		}
		if (!n) return false;
		switch (b)
		{
		case Button::Up: mangaNetCursor = (mangaNetCursor + n - 1) % n; break;
		case Button::Down: mangaNetCursor = (mangaNetCursor + 1) % n; break;
		case Button::Left: mangaNetCursor = std::max(0, mangaNetCursor - MNF_ROWS); break;
		case Button::Right: mangaNetCursor = std::min(n - 1, mangaNetCursor + MNF_ROWS); break;
		default: break;
		}
		if (mangaNetCursor < mangaNetScroll) mangaNetScroll = mangaNetCursor;
		if (mangaNetCursor >= mangaNetScroll + MNF_ROWS) mangaNetScroll = mangaNetCursor - MNF_ROWS + 1;
		return false;
	}

	bool Ui::mangaNetFoldersPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down) return false;
		if (y < MNF_ROW_TOP || y >= MNF_ROW_TOP + MNF_ROWS * ROW_H) return false;
		// the rows follow a finger: a drag scrolls them (the draw keeps the scroll on the cursor, so the cursor moves
		// with the rows shown), a tap (lifted where it went down) is what A does on the row
		int n = mangaNetView == MNF_SERIES ? (int)mangaNetChapters.size() : (int)mangaNetRows.size();
		int i = mangaNetScroll + (y - MNF_ROW_TOP) / ROW_H;
		mangaListBegin(x, y, mangaNetScroll, ROW_H, i < n ? i : -1, [this](int want) {
			int n = mangaNetView == MNF_SERIES ? (int)mangaNetChapters.size() : (int)mangaNetRows.size();
			mangaNetScroll = std::clamp(want, 0, std::max(0, n - MNF_ROWS));
			mangaNetCursor = std::clamp(mangaNetCursor, mangaNetScroll, std::max(mangaNetScroll, std::min(n - 1, mangaNetScroll + MNF_ROWS - 1)));
			return mangaNetScroll;
		}, [this, &p](int i) { mangaNetFoldersTap(i, p); });
		return false;
	}

	void Ui::mangaNetFoldersTap(int i, Player& p)
	{
		if (mangaNetView == MNF_SERIES)
		{
			if (i < 0 || i >= (int)mangaNetChapters.size() || mangaNetLoading) return;
			mangaNetCursor = i;
			int page0 = 0;
			if (mangaStore)
				if (const ChapterProgress* pr = mangaStore->progress(mangaNetChapters[i].path))
					if (!pr->read) page0 = pr->page;
			mangaNetChapterOpen(i, page0, p);
			return;
		}
		if (i < 0 || i >= (int)mangaNetRows.size()) return;
		mangaNetCursor = i;
		// what a tap on the row does is what A does (a folder scans, an archive opens as a one-chapter series)
		const MangaNetRow row = mangaNetRows[i];
		if (mangaNetLoading) return;
		mangaNetBackDir = mangaNetDir;
		if (row.dir) mangaNetScan(row.path);
		else if (row.archive)
		{
			NetMangaSeries s;
			s.path = row.path;
			NetMangaChapter c;
			c.title = row.name;
			size_t dot = c.title.rfind('.');
			if (dot != std::string::npos && dot > 0) c.title = c.title.substr(0, dot);
			s.title = c.title;
			c.path = row.path;
			c.archive = true;
			c.pages = -1;
			s.chapters.push_back(c);
			mangaNetSeriesOpen(s);
		}
	}

	void Ui::drawMangaNetFolders(const Player& p)
	{
		if (mangaNetView == MNF_SERIES)
		{
			std::string title = mangaNetSeriesTitle.empty() ? netBaseName(mangaNetSeriesPath) : mangaNetSeriesTitle;
			drawHeader(title.empty() ? "Series" : title, p);
			int n = (int)mangaNetChapters.size();
			int unread = 0;
			if (mangaStore)
			{
				std::vector<std::string> ids;
				ids.reserve(n);
				for (const auto& c : mangaNetChapters) ids.push_back(c.path);
				unread = mangaStore->unread(mangaNetSeriesPath, ids);
			}
			drawTextMid(std::to_string(n) + (n == 1 ? " chapter" : " chapters") + " \xC2\xB7 " + std::to_string(unread) + " unread",
				0, DIM, 16, 96, 592);
			if (mangaNetCursor >= n) mangaNetCursor = n - 1;
			if (mangaNetCursor < 0 && n) mangaNetCursor = 0;
			if (mangaNetCursor < mangaNetScroll) mangaNetScroll = mangaNetCursor;
			if (mangaNetCursor >= mangaNetScroll + MNF_ROWS) mangaNetScroll = mangaNetCursor - MNF_ROWS + 1;
			for (int i = mangaNetScroll; i < n && i < mangaNetScroll + MNF_ROWS; i++)
			{
				const NetMangaChapter& c = mangaNetChapters[i];
				int y = MNF_ROW_TOP + (i - mangaNetScroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
				bool at = i == mangaNetCursor;
				if (at) drawFocus({16, y, 608, ROW_H - 4});
				const ChapterProgress* pr = mangaStore ? mangaStore->progress(c.path) : nullptr;
				bool read = pr && pr->read;
				drawTextMid(c.title.empty() ? "Chapter" : c.title, 1, read ? DIM : at ? ACCENT : TEXT, 48, mid, 440);
				if (read) drawGlyphMid(G_CHECK, 596, mid, DIM);
				else if (pr && (pr->pages > 0 || pr->page > 0))
				{
					std::string line = "p. " + std::to_string(pr->page + 1);
					if (pr->pages > 0) line += "/" + std::to_string(pr->pages);
					drawTextMid(line, 0, at ? DIM_ON : DIM, 608, mid, 140, 1);
				}
				if (c.archive) drawTextMid("CBZ", 0, at ? DIM_ON : DIM, 208, mid, 60, 1);
			}
			if (n) drawScrollBar(MNF_ROW_TOP, MNF_ROWS * ROW_H - 4, mangaNetScroll, MNF_ROWS, n);
			if (!mangaNetError.empty()) drawTextMid(mangaNetError, 1, DANGER, W / 2, MG_SRC_STATUS_Y, 560, 2);
			else if (mangaNetLoading) drawTextMid("Fetching the chapter\xE2\x80\xA6", 0, DIM, W / 2, MG_SRC_STATUS_Y, 0, 2);
			else if (n == 0) drawTextMid("No chapters", 0, DIM, W / 2, MG_SRC_STATUS_Y, 0, 2);
			drawHints({{"A", "Read"}, {"X", "Read / unread"}, {"B", "Back"}});
			return;
		}
		// a folder of the server: its path, then its folders and archives
		const NetServer* sv = netStore ? netStore->find(mangaNetServerId) : nullptr;
		std::string where = sv ? serverName(*sv) : "Network";
		std::string path = netDecode(mangaNetDir);
		if (!path.empty()) where += " \xC2\xB7 " + path;
		drawTextMid(where, 0, DIM, 16, 96, 592);
		int n = (int)mangaNetRows.size();
		if (mangaNetCursor >= n) mangaNetCursor = n - 1;
		if (mangaNetCursor < 0 && n) mangaNetCursor = 0;
		if (mangaNetCursor < mangaNetScroll) mangaNetScroll = mangaNetCursor;
		if (mangaNetCursor >= mangaNetScroll + MNF_ROWS) mangaNetScroll = mangaNetCursor - MNF_ROWS + 1;
		for (int i = mangaNetScroll; i < n && i < mangaNetScroll + MNF_ROWS; i++)
		{
			const MangaNetRow& row = mangaNetRows[i];
			int y = MNF_ROW_TOP + (i - mangaNetScroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
			bool at = i == mangaNetCursor;
			if (at) drawFocus({16, y, 608, ROW_H - 4});
			drawTextMid(row.name, 1, at ? ACCENT : TEXT, 48, mid, 470);
			drawTextMid(row.dir ? "Folder" : "Archive", 0, at ? DIM_ON : DIM, 608, mid, 120, 1);
		}
		if (n) drawScrollBar(MNF_ROW_TOP, MNF_ROWS * ROW_H - 4, mangaNetScroll, MNF_ROWS, n);
		if (!mangaNetError.empty()) drawTextMid(mangaNetError, 1, DANGER, W / 2, MG_SRC_STATUS_Y, 560, 2);
		else if (mangaNetLoading) drawTextMid("Loading\xE2\x80\xA6", 0, DIM, W / 2, MG_SRC_STATUS_Y, 0, 2);
		else if (n == 0)
			drawTextMid("Nothing here", 0, DIM, W / 2, MG_SRC_STATUS_Y, 0, 2);
		drawHints({{"A", "Open"}, {"B", "Back"}});
	}
}
