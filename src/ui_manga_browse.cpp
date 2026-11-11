#include "ui.h"
#include "manganet.h"
#include "manga_ui_logic.h"
#include "mangaseriescache.h"
#include "ui_internal.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace bp
{
	// ---- Browse's sources, a source's page and a source series (docs/design/manga.md 2, 3 and 5.2) ----
	//
	// The worker does the talking: one job at a time per page, each with a serial kept here, and an answer whose
	// serial is no longer the current one (the page, the tab or the cursor moved on) is dropped. Nothing here opens
	// a socket or touches the top panel: a chapter's pages are handed to the reader through MangaReadRequest.

	namespace
	{
		// what A asks again after an error
		constexpr int FAIL_NONE = 0, FAIL_LIST = 1, FAIL_SERIES = 2, FAIL_PAGES = 3;
		// an error that is a site's browser check (said by the dialog, never as a line on the page)
		bool browserCheckText(const std::string& error, bool* ddos = nullptr)
		{
			std::string l;
			for (char c : error) l += (char)std::tolower((unsigned char)c);
			bool d = l.find("ddos-guard") != std::string::npos;
			if (ddos) *ddos = d;
			// (a site's refusal by status is one too: 403, 428 and 503 are what a check answers; 429, too many asks)
			return d || l.find("cloudflare") != std::string::npos || l.find("browser check") != std::string::npos ||
				l.find("cf_clearance") != std::string::npos || l.find("just a moment") != std::string::npos ||
				l.find("cf-mitigated") != std::string::npos || l.find("http error 403") != std::string::npos ||
				l.find("http error 428") != std::string::npos || l.find("http error 429") != std::string::npos ||
				l.find("http error 503") != std::string::npos;
		}
		// the grid: two rows of the Library's covers in view under the tabs (its rows are 186 apart); cut above the
		// status line, which is above the mini bar's place
		constexpr int SRC_ROWS_SHOWN = 2;
		constexpr int SRC_CLIP_TOP = 96;

		std::string pageLine(int page, int pages)
		{
			if (pages <= 0) return "p. " + std::to_string(page + 1);
			return "p. " + std::to_string(page + 1) + "/" + std::to_string(pages);
		}

		std::string chapterName(const MangaChapter& c)
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

	}

	// ---- Browse's rows ----

	void Ui::mangaBrowseReset(Player& p)
	{
		(void)p;
		mangaSourceCancel();
		mangaView = 0;
		mangaSrcId.clear();
		mangaSrcName.clear();
		mangaSrcFile.clear();
		mangaSrcQuery.clear();
		mangaSrcItems.clear();
		mangaSrcCursor = mangaSrcScroll = mangaSrcPage = 0;
		mangaSrcMore = false;
		mangaSrcError.clear();
		mangaSrcFailed = FAIL_NONE;
		mangaSrcKey.clear();
		mangaSrcItemJson.clear();
		mangaSrcCover.clear();
		mangaSrcTitle.clear();
		mangaSrcInfo = MangaDetails{};
		mangaSrcInfoOn = false;
		mangaSrcChapters.clear();
		mangaSrcChaptersOn = false;
		mangaReadSrcChapters.clear();
		mangaSrcPendAt = -1;
		mangaSrcPendChapters.clear();
		mangaSrcCoverHeaders.clear();
		mbrowseScroll = 0;
		// the saved network connections and their folder pages (ui_manga_netfolders.cpp)
		mangaNetFoldersReset(p);
		// the saved servers' own pages and jobs (ui_manga_servers.cpp)
		mangaServersReset();
		// the installed sources are read again: the list may have changed while the app was away
		mangaSrcRowsSerial = 0;
		mangaJavaRowsSerial = 0;
		mangaSrcRowsOn = false;
		mangaSrcRows.clear();
		// a filter sheet closes with the page; the values and definitions stay for this run (5.2.1), but a job that
		// belonged to a worker that stopped is forgotten so opening the source asks again
		for (auto& kv : mangaFilters)
		{
			kv.second.open = false;
			kv.second.path.clear();
			kv.second.cursor = kv.second.scroll = 0;
			kv.second.checkAt = -1;
			kv.second.checkCursor = kv.second.checkScroll = 0;
			kv.second.textAt = -1;
			kv.second.serial = 0;
			kv.second.javaSerial = 0;
			kv.second.loading = false;
		}
		mangaFilterLocal++;
	}

	void Ui::mangaBrowseRepos(const Player& p)
	{
		(void)p;
		// the Browse tab is the first thing that needs them: the worker starts here if the app's own page didn't
		if (mangaNet) mangaNet->start();
		if (!mangaNet || !mangaNet->started())
		{
			if (mangaSrcRowsOn)
			{
				mangaSrcRows.clear();
				mangaSrcRowsSerial = 0;
				mangaSrcRowsOn = false;
			}
			return;
		}
		uint64_t serial = mangaNet->repos().serial();
		uint64_t javaSerial = mangaNet->javaSources().serial();
		if (mangaSrcRowsOn && serial == mangaSrcRowsSerial && javaSerial == mangaJavaRowsSerial) return;
		mangaSrcRowsOn = true;
		mangaSrcRowsSerial = serial;
		mangaJavaRowsSerial = javaSerial;
		mangaSrcRows.clear();
		for (const auto& s : mangaNet->repos().installed())
			mangaSrcRows.push_back({s.fullId, s.name.empty() ? s.id : s.name, s.lang, s.file, s.adult, false});
		// the Java sources an install returned: one row per source and language, as the Lua sources are
		for (const auto& s : mangaNet->javaSources().sources())
			mangaSrcRows.push_back({MangaJava::sourceId(s.id), s.title(), s.lang, "", s.nsfw, true});
	}

	int Ui::mangaBrowseServerCount() const
	{
		// the saved servers, by name (MangaNet loads the store when it starts; nothing is listed without it)
		if (!mangaNet || !mangaNet->started()) return 0;
		return (int)mangaNet->servers().list().size();
	}

	int Ui::mangaBrowseCount(const Player& p) const
	{
		int n = 1 + mangaNetFoldersCount() + mangaBrowseServerCount();	// On this handheld, the network connections, the servers
		if (mangaSrcRows.empty()) n++;	// the Add sources row: A opens the Sources page
		for (const auto& r : mangaSrcRows)
			if (!r.adult || p.settings.mangaAdult) n++;
		return n;
	}

	const Ui::MangaSrcRow* Ui::mangaBrowseSourceAt(int i, const Player& p) const
	{
		if (i < 0) return nullptr;
		int k = 0;
		for (const auto& r : mangaSrcRows)
		{
			if (r.adult && !p.settings.mangaAdult) continue;
			if (k == i) return &r;
			k++;
		}
		return nullptr;
	}

	const Ui::MangaSrcRow* Ui::mangaSrcRowFor(const std::string& fullId) const
	{
		for (const auto& r : mangaSrcRows)
			if (r.id == fullId) return &r;
		return nullptr;
	}

	void Ui::mangaBrowseChoose(int i, Player& p)
	{
		if (i <= 0)
		{
			// On this handheld: its series, as it was
			mBrowseList = true;
			mListCursor = mListScroll = 0;
			return;
		}
		int nets = mangaNetFoldersCount();
		if (i <= nets)
		{
			// a saved network connection: its folders, through the worker
			std::vector<NetServer> list = mangaNetFoldersServers();
			if (i - 1 < (int)list.size()) mangaNetFoldersOpen(list[i - 1].id, p);
			return;
		}
		int servers = mangaBrowseServerCount();
		if (i <= nets + servers)
		{
			// a saved server: its libraries or catalogue
			if (mangaNet && mangaNet->started())
			{
				std::vector<MangaServer> list = mangaNet->servers().list();
				int at = i - nets - 1;
				if (at >= 0 && at < (int)list.size()) mangaServerOpen(list[at].id, p);
			}
			return;
		}
		// no source installed: the row after the servers is Add sources (A opens the Sources page)
		if (mangaSrcRows.empty()) { openMangaSources(p); return; }
		if (const MangaSrcRow* r = mangaBrowseSourceAt(i - mangaBrowseSourceStart(), p)) mangaOpenSource(*r, p);
	}

	bool Ui::mangaBrowseKey(Button b, Player& p)
	{
		// a network folder's page (its connections and a folder's series) takes the keys while it shows
		if (mangaNetView != MNF_OFF) return mangaNetFoldersKey(b, p);
		// the server row's Edit / Delete sheet takes the keys first (B closes it)
		if (mangaSrvMenu) return mangaServerModalKey(b, p);
		if (mBrowseList)
		{
			int n = (int)mangaLocal.size();
			// X opens the Sources page here too (the lists it holds, repositories added)
			if (b == Button::X) { openMangaSources(p); return false; }
			if (b == Button::A) { mangaChoose(MT_BROWSE, mListCursor, p); return false; }
			if (!n) return false;
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
			return false;
		}
		mangaBrowseRepos(p);
		int n = mangaBrowseCount(p);
		int nets = mangaNetFoldersCount();
		if (b == Button::A) { mangaBrowseChoose(mbrowseCursor, p); return false; }
		if (b == Button::X)
		{
			// X on a server: Edit / Delete (as the Sources page's Options); anywhere else, the Sources page
			if (mbrowseCursor > nets && mbrowseCursor <= nets + mangaBrowseServerCount())
				mangaServerModalOpen(mbrowseCursor - nets, p);
			else openMangaSources(p);
			return false;
		}
		if (b == Button::Y) { mangaServerAddOpen(p); return false; }
		if (!n) return false;
		switch (b)
		{
		case Button::Up: mbrowseCursor = (mbrowseCursor + n - 1) % n; break;
		case Button::Down: mbrowseCursor = (mbrowseCursor + 1) % n; break;
		case Button::Left: mbrowseCursor = std::max(0, mbrowseCursor - MG_ROWS); break;
		case Button::Right: mbrowseCursor = std::min(n - 1, mbrowseCursor + MG_ROWS); break;
		default: break;
		}
		if (mbrowseCursor < mbrowseScroll) mbrowseScroll = mbrowseCursor;
		if (mbrowseCursor >= mbrowseScroll + MG_ROWS) mbrowseScroll = mbrowseCursor - MG_ROWS + 1;
		return false;
	}

	bool Ui::mangaBrowsePointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down) return false;
		// a network folder's page is the whole list while it shows
		if (mangaNetView != MNF_OFF) return mangaNetFoldersPointer(pt, x, y, p);
		if (mangaSrvMenu)
		{
			if (mangaServerModalBox().hit(x, y))
			{
				for (int i = 0; i < 2; i++)
					if (mangaServerModalRowRect(i).hit(x, y))
					{
						mangaSrvMenuCursor = i;
						mangaServerModalKey(Button::A, p);
						return false;
					}
			}
			else
			{
				mangaSrvMenu = false;
				mangaSrvDeleteArmed = false;
				mangaSrvLocal++;
			}
			return false;
		}
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
			return false;
		}
		mangaBrowseRepos(p);
		int n = mangaBrowseCount(p);
		int nets = mangaNetFoldersCount();
		if (y >= MG_ROW_TOP && y < MG_ROW_TOP + MG_ROWS * ROW_H)
		{
			int i = mbrowseScroll + (y - MG_ROW_TOP) / ROW_H;
			if (i >= 0 && i < n)
			{
				mbrowseCursor = i;
				// the pencil at a server row's end is its X (Edit / Delete); anywhere else on it opens the server
				if (i > nets && i <= nets + mangaBrowseServerCount() && x >= 570) mangaServerModalOpen(i - nets, p);
				else mangaBrowseChoose(i, p);
			}
		}
		return false;
	}

	void Ui::drawMangaBrowseRows(const Player& p)
	{
		// a network folder's own page (its folders, or a folder read as a series) draws in its place
		if (mangaNetView != MNF_OFF)
		{
			drawMangaNetFolders(p);
			return;
		}
		mangaBrowseRepos(p);
		int n = mangaBrowseCount(p);
		if (mbrowseCursor >= n) mbrowseCursor = n - 1;
		if (mbrowseCursor < 0) mbrowseCursor = 0;
		if (mbrowseCursor < mbrowseScroll) mbrowseScroll = mbrowseCursor;
		if (mbrowseCursor >= mbrowseScroll + MG_ROWS) mbrowseScroll = mbrowseCursor - MG_ROWS + 1;
		auto rowY = [&](int i) { return MG_ROW_TOP + (i - mbrowseScroll) * ROW_H; };
		auto inView = [&](int y) { return y >= MG_ROW_TOP && y < MG_ROW_TOP + MG_ROWS * ROW_H; };
		std::vector<NetServer> netServers = mangaNetFoldersServers();
		int nets = (int)netServers.size();
		std::vector<MangaServer> servers;
		if (mangaNet && mangaNet->started()) servers = mangaNet->servers().list();
		int srcStart = nets + (int)servers.size() + 1;
		for (int i = mbrowseScroll; i < n && i < mbrowseScroll + MG_ROWS; i++)
		{
			int y = rowY(i), mid = y + (ROW_H - 4) / 2;
			bool at = i == mbrowseCursor;
			if (at) drawFocus({16, y, 608, ROW_H - 4});
			// each row's kind at its left: the card, a network folder, a server, a source
			SDL_Color ic = at ? ACCENT : DIM;
			if (i == 0)
			{
				int count = (int)mangaLocal.size();
				drawGlyphMid(G_CARD, 44, mid, ic);
				drawTextMid("On this handheld", 1, at ? ACCENT : TEXT, 68, mid, 434);
				drawTextMid(count == 1 ? "1 series" : std::to_string(count) + " series", 0, at ? DIM_ON : DIM, 608, mid, 160, 1);
				continue;
			}
			if (i <= nets)
			{
				// a saved network connection: its name and what kind it is (SMB · host, FTP · host)
				const NetServer& s = netServers[i - 1];
				drawGlyphMid(G_NETWORK, 44, mid, ic);
				drawTextMid(serverName(s), 1, at ? ACCENT : TEXT, 68, mid, 394);
				drawTextMid(std::string(netTypeLabel(s.type)) + "  " + s.host, 0, at ? DIM_ON : DIM, 608, mid, 180, 1);
				continue;
			}
			if (i <= nets + (int)servers.size())
			{
				// a saved server: its name, what kind it is, and the pencil its X (or a tap there) opens
				const MangaServer& s = servers[i - nets - 1];
				drawGlyphMid(G_LAPTOP, 44, mid, ic);
				drawTextMid(s.name, 1, at ? ACCENT : TEXT, 68, mid, 414);
				std::string kind = s.kind;
				for (char& c : kind) c = (char)std::toupper((unsigned char)c);
				int tw = (int)std::lround(textWidth(kind, 0) / sx) + 14;
				fillRound({562 - tw, mid - 10, tw, 20}, TRACK, R_CHIP);
				drawTextMid(kind, 0, at ? ACCENT : DIM_ON, 562 - tw / 2, mid, tw - 6, 2);
				drawGlyphMid(G_PENCIL, 600, mid, at ? ACCENT : DIM);
				continue;
			}
			if (mangaSrcRows.empty() && i == srcStart)
			{
				// no source installed: the row after the servers says where they come from, and A (or a tap) opens it
				drawGlyphMid(G_PLUS, 44, mid, ic);
				drawTextMid("Add sources in Sources (X on Browse)", 1, at ? ACCENT : TEXT, 68, mid, 540);
				continue;
			}
			const MangaSrcRow* r = mangaBrowseSourceAt(i - srcStart, p);
			if (!r) continue;
			drawGlyphMid(G_BLOCKS, 44, mid, ic);
			drawTextMid(r->name, 1, at ? ACCENT : TEXT, 68, mid, 434);
			// its language, a chip at the row's end (as a cover's unread count is)
			int right = 608;
			std::string lang = r->lang;
			for (char& c : lang) c = (char)std::toupper((unsigned char)c);
			if (!lang.empty())
			{
				int tw = (int)std::lround(textWidth(lang, 0) / sx) + 14;
				fillRound({right - tw, mid - 10, tw, 20}, TRACK, R_CHIP);
				drawTextMid(lang, 0, at ? ACCENT : DIM_ON, right - tw / 2, mid, tw - 6, 2);
			}
		}
		// no saved connection: where they would be (the Files browser's Network adds one)
		if (nets == 0 && inView(rowY(n)))
		{
			drawGlyphMid(G_NETWORK, 44, rowY(n) + (ROW_H - 4) / 2, DIM);
			drawTextMid("Network folders \xC2\xB7 none saved", 1, DIM, 68, rowY(n) + (ROW_H - 4) / 2, 540);
		}
		bool onServer = mbrowseCursor > nets && mbrowseCursor <= nets + (int)servers.size();
		if (onServer) drawHints({{"A", "Open"}, {"X", "Edit"}, {"Y", "Add server"}, {"B", "Back"}});
		else drawHints({{"A", "Open"}, {"X", "Sources"}, {"Y", "Add server"}, {"B", "Back"}});
		if (mangaSrvMenu) drawMangaServerModal(p);
	}

	// ---- a source's page: Popular · Latest · Search, a cover grid, Search ----

	std::string Ui::mangaSrcFiltersNow() const
	{
		return MangaJava::isSourceId(mangaSrcId) ? mangaJavaFiltersJson() : mangaSourceFiltersJson();
	}

	void Ui::mangaSrcTabSave()
	{
		if (mangaSrcItems.empty() || !mangaSrcError.empty()) return;
		if (mangaSrcTabCache.size() > 12) mangaSrcTabCache.clear();
		// a page still coming is kept as not come: back on the tab, it is asked for again (mangaSourceMaybeMore)
		int page = mangaSrcLoading ? std::max(1, mangaSrcPage - 1) : mangaSrcPage;
		bool more = mangaSrcLoading ? true : mangaSrcMore;
		mangaSrcTabCache[mangaSrcId + "|" + std::to_string(mangaSrcTab)] =
			{mangaSrcItems, page, mangaSrcCursor, mangaSrcScroll, more, mangaSrcQuery, mangaSrcFiltersNow()};
	}

	bool Ui::mangaSrcTabRestore(int tab)
	{
		// a tab as it was left: Popular and Latest as they were, Search while its words and filters are the same
		auto it = mangaSrcTabCache.find(mangaSrcId + "|" + std::to_string(tab));
		if (it == mangaSrcTabCache.end()) return false;
		if (tab == SRC_SEARCH && (it->second.query != mangaSrcQuery || it->second.filters != mangaSrcFiltersNow())) return false;
		mangaSrcTab = tab;
		mangaSrcItems = it->second.items;
		mangaSrcPage = it->second.page;
		mangaSrcCursor = it->second.cursor;
		mangaSrcScroll = it->second.scroll;
		mangaSrcMore = it->second.more;
		mangaSrcError.clear();
		mangaSrcLoading = false;
		return true;
	}

	void Ui::mangaOpenSource(const MangaSrcRow& row, Player& p)
	{
		(void)p;
		// the source's script and its prefs live behind the worker: it starts here if the app's own page didn't
		if (mangaNet) mangaNet->start();
		mangaSourceCancel();
		mangaSrcId = row.id;
		mangaSrcName = row.name;
		mangaSrcFile = row.file;
		mangaSrcQuery.clear();
		mangaSrcItems.clear();
		mangaSrcCursor = mangaSrcScroll = mangaSrcPage = 0;
		mangaSrcMore = false;
		mangaSrcHasLatest = true;
		mangaSrcTab = SRC_POPULAR;
		mangaSrcError.clear();
		mangaSrcFailed = FAIL_NONE;
		mangaView = 1;
		// a Java source starts the host on open (the Starting line shows while it comes up) and says whether its
		// Latest tab exists; a Lua source's latest() is asked by its first answer instead
		if (MangaJava::isSourceId(row.id)) mangaJavaSourceOpened(row, p);
		// a source's own filters (5.2.1): asked once a run, kept for the sheet and the Search jobs
		mangaFilterAsk();
		// opened again: its Popular as it was left (no ask), else asked
		if (!mangaSrcTabRestore(SRC_POPULAR)) mangaSourceLoad(1);
	}

	void Ui::mangaSourceBack(Player& p)
	{
		(void)p;
		mangaSrcTabSave();	// (kept: opened again, the tab is as it was)
		mangaSourceCancel();
		mangaView = 0;
		mangaSrcError.clear();
		mangaSrcItems.clear();
		mangaSrcCursor = mangaSrcScroll = 0;
	}

	void Ui::mangaSourceTabSet(int tab, Player& p)
	{
		if (!mangaNet || !mangaNet->started())
		{
			mangaSrcError = "Couldn\xE2\x80\x99t ask the source";
			return;
		}
		if (mangaSrcListSerial)
		{
			mangaNet->worker().cancel(mangaSrcListSerial);
			mangaSrcListSerial = 0;
		}
		if (MangaJava::isSourceId(mangaSrcId)) mangaJavaCancelList();
		// the tab left is kept as it is; the tab chosen comes back as it was left, if it was
		mangaSrcTabSave();
		if (mangaSrcTabRestore(tab)) return;
		mangaSrcTab = tab;
		mangaSrcItems.clear();
		mangaSrcCursor = mangaSrcScroll = mangaSrcPage = 0;
		mangaSrcMore = false;
		mangaSrcError.clear();
		mangaSrcLoading = false;
		if (tab == SRC_SEARCH)
		{
			// the tab is the search: the keyboard comes up (as Y does), its Done runs search("", 1, "{}")
			openKeyboard(MANGA_SEARCH_FIELD, mangaSrcQuery);
			return;
		}
		mangaSourceLoad(1);
	}

	void Ui::mangaSourceTabStep(int dir, Player& p)
	{
		int slots[3], count = 0;
		slots[count++] = SRC_POPULAR;
		if (mangaSrcHasLatest) slots[count++] = SRC_LATEST;
		slots[count++] = SRC_SEARCH;
		int at = 0;
		for (int i = 0; i < count; i++)
			if (slots[i] == mangaSrcTab) at = i;
		int next = std::clamp(at + dir, 0, count - 1);
		if (next != at) mangaSourceTabSet(slots[next], p);
	}

	void Ui::mangaSourceLoad(int page)
	{
		if (MangaJava::isSourceId(mangaSrcId)) { mangaJavaListLoad(page); return; }
		mangaSrcError.clear();
		mangaSrcFailed = FAIL_LIST;
		if (mangaNet && mangaNet->started() && mangaSrcListSerial)
		{
			mangaNet->worker().cancel(mangaSrcListSerial);
			mangaSrcListSerial = 0;
		}
		if (!mangaNet || !mangaNet->started() || mangaSrcId.empty())
		{
			mangaSrcLoading = false;
			mangaSrcError = "Couldn\xE2\x80\x99t ask the source";
			return;
		}
		MangaWorker::Job job;
		job.sourceId = mangaSrcId;
		job.scriptPath = mangaSrcFile;
		job.page = page;
		job.kind = mangaSrcTab == SRC_LATEST ? MangaWorker::Job::Latest : mangaSrcTab == SRC_SEARCH ? MangaWorker::Job::Search
			: MangaWorker::Job::Popular;
		if (job.kind == MangaWorker::Job::Search)
		{
			job.query = mangaSrcQuery;
			job.json = mangaSourceFiltersJson();
		}
		mangaSrcPage = page;
		mangaSrcLoading = true;
		mangaSrcListSerial = mangaNet->worker().submit(job);
		if (!mangaSrcListSerial)
		{
			mangaSrcLoading = false;
			mangaSrcError = "Couldn\xE2\x80\x99t ask the source";
		}
	}

	void Ui::mangaSourceSearch(const std::string& query)
	{
		mangaSourceCancel();
		mangaSrcQuery = trimmed(query);
		mangaSrcTab = SRC_SEARCH;
		mangaSrcItems.clear();
		mangaSrcCursor = mangaSrcScroll = mangaSrcPage = 0;
		mangaSrcMore = false;
		mangaSrcError.clear();
		mangaSourceLoad(1);
	}

	void Ui::mangaSourceRetry(Player& p)
	{
		if (mangaSrcFailed == FAIL_SERIES) { mangaSourceSeriesLoad(p); return; }
		if (mangaSrcFailed == FAIL_PAGES)
		{
			mangaSourcePagesAsk(p);
			return;
		}
		// a list: from its first page (nothing above it was kept in a shape a page could be inserted back into)
		mangaSrcItems.clear();
		mangaSrcCursor = mangaSrcScroll = 0;
		mangaSrcMore = false;
		mangaSourceLoad(1);
	}

	bool Ui::mangaSourceKey(Button b, Player& p)
	{
		// the filter sheet takes every key while it shows
		if (mangaFilterShown()) return mangaFilterKey(b, p);
		// X, the page's own action: 5.2.1 named Y, but Y is the grid's Search, and SELECT goes to Now Playing
		// whenever something plays (music usually does while reading)
		if (b == Button::X && mangaFilterAvailable())
		{
			mangaFilterShow(p);
			return false;
		}
		if (b == Button::B) { mangaSourceBack(p); return false; }
		// Y, and the Search tab, ask for the query; Done is the keyboard's START
		if (b == Button::Y)
		{
			if (mangaSrcTab == SRC_SEARCH) openKeyboard(MANGA_SEARCH_FIELD, mangaSrcQuery);
			else mangaSourceTabSet(SRC_SEARCH, p);
			return false;
		}
		if (b == Button::L) { mangaSourceTabStep(-1, p); return false; }
		if (b == Button::R) { mangaSourceTabStep(1, p); return false; }
		if (b == Button::A && !mangaSrcError.empty())
		{
			// the error is what A can do something about, before the grid means anything (the hints say so)
			mangaSourceRetry(p);
			return false;
		}
		int n = (int)mangaSrcItems.size();
		int rows = (n + MG_COLS - 1) / MG_COLS;
		if (b == Button::A)
		{
			if (mangaSrcCursor >= 0 && mangaSrcCursor < n) mangaSourceOpenSeriesAt(mangaSrcCursor, p);
			else if (mangaSrcTab == SRC_SEARCH) openKeyboard(MANGA_SEARCH_FIELD, mangaSrcQuery);
			return false;
		}
		if (!n) return false;
		if (mangaSrcCursor >= n) mangaSrcCursor = n - 1;
		switch (b)
		{
		case Button::Up: if (mangaSrcCursor >= MG_COLS) mangaSrcCursor -= MG_COLS; break;
		case Button::Down: if (mangaSrcCursor + MG_COLS < n) mangaSrcCursor += MG_COLS; break;
		case Button::Left: if (mangaSrcCursor % MG_COLS > 0) mangaSrcCursor--; break;
		case Button::Right: if (mangaSrcCursor % MG_COLS + 1 < MG_COLS && mangaSrcCursor + 1 < n) mangaSrcCursor++; break;
		default: break;
		}
		mangaSrcScroll = std::max(0, mangaSrcCursor / MG_COLS - (SRC_ROWS_SHOWN - 1));
		mangaSourceMaybeMore();
		return false;
	}

	void Ui::mangaSourceMaybeMore()
	{
		int n = (int)mangaSrcItems.size(), rows = (n + MG_COLS - 1) / MG_COLS;
		int lastRow = std::max(mangaSrcCursor / MG_COLS, mangaSrcScroll + SRC_ROWS_SHOWN - 1);
		// the first three pages at once (a source's page is often only a row or two), then two rows before the end;
		// one at a time. Not after a failure (A or a tap tries again)
		if (!mangaSrcMore || mangaSrcLoading || !mangaSrcError.empty() || rows == 0) return;
		if (mangaSrcPage < 3 || lastRow >= rows - 2) mangaSourceLoad(mangaSrcPage + 1);
	}

	bool Ui::mangaSourcePointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (mangaFilterShown()) return mangaFilterPointer(pt, x, y, p);
		// the grid follows a finger: a drag scrolls it by rows, a tap without one opens the cover it went down on
		{
			int n = (int)mangaSrcItems.size(), rows = (n + MG_COLS - 1) / MG_COLS;
			if (pt.kind == Pointer::Move && mangaSrcDragY >= 0)
			{
				int dy = y - mangaSrcDragY;
				if (std::abs(dy) > 12 && !mangaSrcDragMoved)
				{
					mangaSrcDragMoved = true;
					fprintf(stderr, "manga: grid drag from y %d (scroll %d of %d rows)\n", mangaSrcDragY, mangaSrcScroll, rows);
				}
				if (mangaSrcDragMoved && rows > 0)
				{
					int s = std::clamp(mangaSrcDragScroll - (int)std::lround(dy / (double)MG_SRC_STEP), 0,
						std::max(0, rows - SRC_ROWS_SHOWN));
					if (s != mangaSrcScroll)
					{
						mangaSrcScroll = s;
						// the cursor stays on screen (the draw would otherwise scroll back to it)
						int row = mangaSrcCursor / MG_COLS, col = mangaSrcCursor % MG_COLS;
						if (row < s) row = s;
						if (row > s + SRC_ROWS_SHOWN - 1) row = s + SRC_ROWS_SHOWN - 1;
						mangaSrcCursor = std::min(n - 1, row * MG_COLS + col);
						mangaSourceMaybeMore();
					}
				}
				return false;
			}
			if (pt.kind == Pointer::Up && mangaSrcDragY >= 0)
			{
				int tap = mangaSrcDragTap;
				bool moved = mangaSrcDragMoved;
				mangaSrcDragY = mangaSrcDragTap = -1;
				mangaSrcDragMoved = false;
				if (!moved && tap >= 0 && tap < n)
				{
					mangaSrcCursor = tap;
					mangaSourceOpenSeriesAt(tap, p);
				}
				return false;
			}
		}
		if (pt.kind != Pointer::Down) return false;
		// the status beside the tabs: an error there is a Try again
		if (!mangaSrcError.empty() && mangaSrcStatusRect().hit(x, y)) { mangaSourceRetry(p); return false; }
		// the tabs: a tap on one steps straight to it (Search asks for the query)
		int slots[3], count = 0;
		slots[count++] = SRC_POPULAR;
		if (mangaSrcHasLatest) slots[count++] = SRC_LATEST;
		slots[count++] = SRC_SEARCH;
		for (int i = 0; i < count; i++)
			if (mangaSrcTabRect(i).hit(x, y))
			{
				if (slots[i] != mangaSrcTab) mangaSourceTabSet(slots[i], p);
				// the Search tab again: the keyboard, the list it ran kept under it
				else if (slots[i] == SRC_SEARCH) openKeyboard(MANGA_SEARCH_FIELD, mangaSrcQuery);
				return false;
			}
		// a cover: its series page, as a tap in the Library's grid opens one
		int n = (int)mangaSrcItems.size();
		for (int i = mangaSrcScroll * MG_COLS; i < n; i++)
		{
			Rect r = mangaSrcCoverRect(i);
			if (r.y >= mangaSrcClipBottom()) break;
			// (the part cut at the grid's foot isn't on screen, so a tap there isn't on it)
			if (y < mangaSrcClipBottom() && r.hit(x, y))
			{
				// opened as the finger lifts, if it didn't drag
				mangaSrcDragTap = i;
				break;
			}
		}
		// anywhere on the grid: a drag may start here
		if (y >= SRC_CLIP_TOP && y < mangaSrcClipBottom())
		{
			mangaSrcDragY = y;
			mangaSrcDragScroll = mangaSrcScroll;
			mangaSrcDragMoved = false;
		}
		return false;
	}

	void Ui::drawMangaSource(const Player& p)
	{
		drawHeader(mangaSrcName.empty() ? "Source" : mangaSrcName, p);
		int slots[3], count = 0;
		slots[count++] = SRC_POPULAR;
		if (mangaSrcHasLatest) slots[count++] = SRC_LATEST;
		slots[count++] = SRC_SEARCH;
		static const char* NAMES[3] = {"Popular", "Latest", "Search"};
		Rect first = mangaSrcTabRect(0), last = mangaSrcTabRect(count - 1);
		drawTextMid("L", 0, ACCENT, first.x - 12, first.y + first.h / 2, 0, 2);
		for (int i = 0; i < count; i++) drawTab(mangaSrcTabRect(i), NAMES[slots[i]], mangaSrcTab == slots[i], 0);
		drawTextMid("R", 0, ACCENT, last.x + last.w + 12, last.y + last.h / 2, 0, 2);
		int n = (int)mangaSrcItems.size();
		int rows = (n + MG_COLS - 1) / MG_COLS;
		if (n && mangaSrcCursor >= n) mangaSrcCursor = n - 1;
		if (mangaSrcScroll > rows - 1) mangaSrcScroll = std::max(0, rows - 1);
		if (mangaSrcCursor / MG_COLS < mangaSrcScroll) mangaSrcScroll = mangaSrcCursor / MG_COLS;
		if (mangaSrcCursor / MG_COLS >= mangaSrcScroll + SRC_ROWS_SHOWN) mangaSrcScroll = mangaSrcCursor / MG_COLS - SRC_ROWS_SHOWN + 1;
		// the grid, as the Library's: each cover a generated one until the real one arrives, its title under it.
		// covers are fetched only while their card is on screen and cancelled when it scrolls away
		mangaCoverOnScreen.clear();
		SDL_Rect clip{0, (int)std::lround(SRC_CLIP_TOP * sy), (int)std::lround(W * sx),
			(int)std::lround((mangaSrcClipBottom() - SRC_CLIP_TOP) * sy)};
		SDL_RenderSetClipRect(ren, &clip);
		for (int i = mangaSrcScroll * MG_COLS; i < n; i++)
		{
			Rect r = mangaSrcCoverRect(i);
			if (r.y >= mangaSrcClipBottom()) break;
			bool at = i == mangaSrcCursor;
			drawMangaCover(r, mangaSrcItems[i].title, at, 4, "", mangaSrcItems[i].cover, &mangaSrcCoverHeaders);
			drawTextMid(mangaSrcItems[i].title, 0, at ? ACCENT : TEXT, r.x + r.w / 2, r.y + r.h + 13, r.w, 2);
		}
		SDL_RenderSetClipRect(ren, nullptr);
		// anything still in flight but no longer on screen: the card scrolled away, its fetch goes
		mangaCoverCancelOffScreen();
		// a next page of the grid on its way: a notice over the grid's foot (the first page's is the status line)
		if (mangaSrcLoading && mangaSrcPage > 1 && mangaSrcError.empty())
		{
			std::string t = "Loading more\xE2\x80\xA6";
			int tw = (int)std::lround(textWidth(t, 0) / sx) + 32;
			Rect r{W / 2 - tw / 2, mangaSrcClipBottom() - 40, tw, 30};
			drawBox(r, SURFACE, R_CHIP, ACCENT);
			drawTextMid(t, 0, TEXT, W / 2, r.y + r.h / 2, 0, 2);
		}
		if (rows > 1) drawScrollBar(MG_SRC_TOP, mangaSrcClipBottom() - MG_SRC_TOP - 4, mangaSrcScroll, SRC_ROWS_SHOWN, rows);
		// the state under the grid: the source's error, a job running, or the query the Search tab ran
		// (beside the tabs, right-aligned: the grid has the panel down to the hint bar)
		std::string javaStatus = mangaJavaStatusLine();
		Rect st = mangaSrcStatusRect();
		int sx1 = st.x + st.w, smid = st.y + st.h / 2;
		// (a source's error is the dialog's, not a line here; starting is the notice at the top, on every page; only
		// the engine's failure stays said here)
		if (!javaStatus.empty() && mangaNet && mangaNet->javaExt().state() == JavaExtHost::State::Failed)
			drawTextMid(javaStatus, 0, DANGER, sx1, smid, st.w, 1);
		else if (mangaSrcLoading && n > 0) drawTextMid("Loading\xE2\x80\xA6", 0, DIM, sx1, smid, st.w, 1);
		else if (mangaSrcTab == SRC_SEARCH && !mangaSrcQuery.empty())
			drawTextMid("Search: " + mangaSrcQuery, 0, DIM, sx1, smid, st.w, 1);
		// the grid's first page on its way: the loading mark in the middle (later pages: Loading more… at the foot)
		if (n == 0 && mangaSrcError.empty() && mangaSrcLoading) drawLoader(W / 2, 270, "Loading\xE2\x80\xA6");
		if (n == 0 && mangaSrcError.empty() && !mangaSrcLoading && javaStatus.empty())
		{
			std::string line = mangaSrcTab == SRC_SEARCH ? (mangaSrcQuery.empty() ? "Y: search this source" : "No results")
				: "Nothing here yet";
			drawTextMid(line, 1, DIM, W / 2, 210, 0, 2);
		}
		if (!mangaSrcError.empty())
		{
			if (mangaFilterAvailable()) drawHints({{"A", "Try again"}, {"X", "Filters"}, {"L/R", "Tabs"}, {"B", "Back"}});
			else drawHints({{"A", "Try again"}, {"L/R", "Tabs"}, {"B", "Back"}});
		}
		else if (mangaFilterAvailable())
			drawHints({{"A", "Open"}, {"Y", "Search"}, {"X", "Filters"}, {"L/R", "Tabs"}, {"B", "Back"}});
		else drawHints({{"A", "Open"}, {"Y", "Search"}, {"L/R", "Tabs"}, {"B", "Back"}});
		if (mangaFilterShown()) drawMangaFilters(p);
	}

	void Ui::mangaSourceOpenSeriesAt(int i, Player& p)
	{
		if (i < 0 || i >= (int)mangaSrcItems.size()) return;
		const MangaItem& it = mangaSrcItems[i];
		mangaOpenSourceSeries(sourceItemId(mangaSrcId, it.key), 3, it.json, it.cover, it.title, p);
	}

	// ---- the worker's answers ----

	void Ui::mangaErrCheck(const std::string& error, bool anyError)
	{
		// (each error once: the same one again, from the source or the reader, isn't asked about twice)
		if (error.empty() || error == mangaErrFor) return;
		mangaErrFor = error;
		bool ddos = false;
		if (!browserCheckText(error, &ddos))
		{
			if (!anyError) return;
			// any other error of a source's: said in the dialog, never as a line beside the tabs
			// (the first line only, without the engine's "Exception while fetching data (…) :" before it)
			std::string e = error.substr(0, error.find_first_of("\r\n"));
			size_t c = e.find(") : ");
			if (e.compare(0, 9, "Exception") == 0 && c != std::string::npos) e = e.substr(c + 4);
			mangaErrText = "The source didn\xE2\x80\x99t answer: " + e;
			mangaErrOpen = true;
			fprintf(stderr, "manga: source error (%s)\n", error.c_str());
			return;
		}
		std::string l;
		for (char c : error) l += (char)std::tolower((unsigned char)c);
		size_t h = l.find("http error ");
		if (!ddos && l.find("cloudflare") == std::string::npos && h != std::string::npos)
			mangaErrText = "The site refused (HTTP " + error.substr(h + 11, 3) +
				"). It may be behind a browser check Banana Player can\xE2\x80\x99t pass, or busy: try another source, or later.";
		else
			mangaErrText = std::string("This source is behind a ") + (ddos ? "DDoS-Guard" : "Cloudflare") +
				" check that Banana Player can\xE2\x80\x99t pass. Try another source, or try again later.";
		mangaErrOpen = true;
		fprintf(stderr, "manga: a browser check (%s)\n", error.c_str());
	}

	void Ui::drawMangaErr()
	{
		Rect b = mangaErrBox();
		drawDialog(b, "Can\xE2\x80\x99t open this");
		std::vector<std::string> lines = wrapText(mangaErrText, 0, b.w - 48, 4);
		for (size_t i = 0; i < lines.size(); i++) drawTextMid(lines[i], 0, TEXT, W / 2, b.y + 78 + (int)i * 22, b.w - 40, 2);
		Rect ok = mangaErrOk();
		drawBox(ok, ACCENT, R_BUTTON);
		drawTextMid("OK", 1, ON_ACCENT, ok.x + ok.w / 2, ok.y + ok.h / 2, ok.w - 16, 2);
		drawHints({{"A", "OK"}});
	}

	void Ui::mangaBrowsePoll(const Player& p)
	{
		// a source's error that is a browser check: the dialog (once per error). On a grid that has covers it is
		// no error of the grid's: A opens the series picked, not Try again
		mangaErrCheck(mangaSrcError, true);
		if (page == Page::Manga && mangaView == 1 && !mangaSrcItems.empty() && !mangaSrcError.empty())
		{
			mangaSrcError.clear();
			mangaSrcFailed = FAIL_NONE;
		}
		if (!mangaNet || !mangaNet->started()) return;
		// a source's grid fills itself: its first pages, and the next near its end (the answer of the last landed)
		if (page == Page::Manga && mangaView == 1) mangaSourceMaybeMore();
		for (auto& r : mangaNet->takeWorker())
		{
			// a source's filters (5.2.1): kept under the source the answer names, and dropped when that source's later
			// ask replaced it (a stale serial, as the grid's own jobs are)
			if (r.kind == MangaWorker::Job::Filters && r.serial)
			{
				auto it = mangaFilters.find(r.sourceId);
				if (it != mangaFilters.end() && it->second.serial == r.serial)
				{
					MangaFilterSession& f = it->second;
					f.serial = 0;
					f.loading = false;
					if (!r.ok) f.error = r.error.empty() ? "The source didn\xE2\x80\x99t answer" : r.error;
					else
					{
						std::vector<MangaFilter> defs;
						std::string error;
						if (!mangaFiltersParse(r.filtersJson, defs, error))
						{
							f.error = error;
							f.known = false;
						}
						else
						{
							f.error.clear();
							f.defs = std::move(defs);
							f.known = true;
							mangaFilterFit(f.defs, f.values);
							if (f.defs.empty()) f.open = false;
						}
					}
					mangaFilterLocal++;
				}
				continue;
			}
			if (r.serial && r.serial == mangaSrcListSerial)
			{
				mangaSrcListSerial = 0;
				mangaSrcLoading = false;
				if (!r.ok)
				{
					// a source without latest(): its tab goes, as the design has it (Latest "if the source has
					// latest"), and Popular comes up instead
					if (r.kind == MangaWorker::Job::Latest && r.error == "the source has no latest function")
					{
						mangaSrcHasLatest = false;
						if (mangaSrcTab == SRC_LATEST)
						{
							mangaSrcTab = SRC_POPULAR;
							mangaSourceLoad(1);
						}
						continue;
					}
					mangaSrcError = r.error.empty() ? "The source didn\xE2\x80\x99t answer" : r.error;
					mangaSrcFailed = FAIL_LIST;
					continue;
				}
				const std::vector<MangaItem>& items = r.list.items;
				if (mangaSrcPage <= 1)
				{
					mangaSrcItems = items;
					mangaSrcCursor = mangaSrcScroll = 0;
					// the source's own headers for cover fetches (its Referer and the like): the same for every
					// call to this source, kept from the first list and used for every cover this page asks for
					mangaSrcCoverHeaders = r.sourceHeaders;
				}
				else mangaSrcItems.insert(mangaSrcItems.end(), items.begin(), items.end());
				mangaSrcMore = r.list.more;
				mangaSrcError.clear();
				if (mangaSrcCursor >= (int)mangaSrcItems.size()) mangaSrcCursor = std::max(0, (int)mangaSrcItems.size() - 1);
				continue;
			}
			if (r.serial && r.serial == mangaSrcDetailsSerial)
			{
				mangaSrcDetailsSerial = 0;
				if (!r.ok)
				{
					// a page with a cached list keeps it (mangaSourceSeriesFetched toasts at the end); one without
					// shows the source's line as it always has
					if (!mangaSrcHadList && !mangaSrcChaptersOn)
					{
						mangaSrcError = r.error.empty() ? "The source didn\xE2\x80\x99t answer" : r.error;
						mangaSrcFailed = FAIL_SERIES;
					}
				}
				else
				{
					mangaSrcInfo = r.details;
					mangaSrcInfoOn = true;
					mangaSrcFetchDetailsOk = true;
					if (mangaSrcTitle.empty() && !r.details.title.empty()) mangaSrcTitle = r.details.title;
				}
				mangaSrcLoading = mangaSrcDetailsSerial != 0 || mangaSrcChaptersSerial != 0;
				if (!mangaSrcLoading) mangaSourceSeriesFetched();
				continue;
			}
			if (r.serial && r.serial == mangaSrcChaptersSerial)
			{
				mangaSrcChaptersSerial = 0;
				if (!r.ok)
				{
					if (!mangaSrcHadList && !mangaSrcChaptersOn)
					{
						mangaSrcError = r.error.empty() ? "The source didn\xE2\x80\x99t answer" : r.error;
						mangaSrcFailed = FAIL_SERIES;
					}
				}
				else
				{
					// the source gives them newest first; the card's series page lists oldest first, and this is
					// that page for a source, so they are turned round once here
					mangaSrcChapters.clear();
					for (auto i = r.chapters.rbegin(); i != r.chapters.rend(); ++i)
						if (!i->key.empty()) mangaSrcChapters.push_back(*i);
					mangaSrcChaptersOn = true;
					mangaSrcFetchChaptersOk = true;
					if (mserCursor >= (int)mangaSrcChapters.size()) mserCursor = (int)mangaSrcChapters.size() - 1;
				}
				mangaSrcLoading = mangaSrcDetailsSerial != 0 || mangaSrcChaptersSerial != 0;
				if (!mangaSrcLoading) mangaSourceSeriesFetched();
				continue;
			}
			if (r.serial && r.serial == mangaSrcPagesSerial)
			{
				mangaSrcPagesSerial = 0;
				mangaSrcLoading = false;
				if (!r.ok)
				{
					mangaSrcError = r.error.empty() ? "The chapter\xE2\x80\x99s pages didn\xE2\x80\x99t come" : r.error;
					mangaSrcFailed = FAIL_PAGES;
					continue;
				}
				if (r.pages.empty())
				{
					mangaSrcError = "This chapter has no pages";
					mangaSrcFailed = FAIL_PAGES;
					continue;
				}
				mangaSrcPages = r.pages;
				mangaSrcUrls.clear();
				mangaSrcHeaders.clear();
				bool needsImage = false;
				for (const auto& pg : r.pages)
					if (pg.url.empty()) { needsImage = true; break; }
				if (needsImage)
				{
					// a page given as {key}: image() resolves it. v1 does all of them, in order, before opening
					// (an Image job per page as the reader needs it would stall the first page on every chapter)
					mangaSrcUrls.assign(r.pages.size(), "");
					mangaSrcImageAt = 0;
					mangaSrcLoading = true;
					mangaSourceImageAsk();
					continue;
				}
				for (const auto& pg : r.pages) mangaSrcUrls.push_back(pg.url);
				mangaSourceChapterReady(p);
				continue;
			}
			if (r.serial && r.serial == mangaSrcImageSerial)
			{
				mangaSrcImageSerial = 0;
				if (!r.ok)
				{
					mangaSrcLoading = false;
					mangaSrcError = r.error.empty() ? "A page didn\xE2\x80\x99t come" : r.error;
					mangaSrcFailed = FAIL_PAGES;
					continue;
				}
				if (mangaSrcImageAt < (int)mangaSrcUrls.size()) mangaSrcUrls[mangaSrcImageAt] = r.imageUrl;
				// the source's own headers come with image() (its Referer and the like), plus this page's
				for (const auto& h : r.imageHeaders)
				{
					bool has = false;
					for (const auto& o : mangaSrcHeaders)
						if (o.first == h.first) has = true;
					if (!has) mangaSrcHeaders.push_back(h);
				}
				mangaSrcImageAt++;
				if (mangaSrcImageAt < (int)mangaSrcPages.size()) mangaSourceImageAsk();
				else
				{
					mangaSrcLoading = false;
					mangaSourceChapterReady(p);
				}
				continue;
			}
			// the network folders' listings and a chapter's copy (ui_manga_netfolders.cpp)
			if (mangaNetFoldersResult(r, p)) continue;
			// the dashboard's series view asked for a chapter list: its own cache takes that answer
			if (mangaWebWorker(r)) continue;
			// an answer nothing here asked for: a Downloads sheet chapter's page list (ui_manga_downloads.cpp), or
			// one for a page that moved on (dropped)
			mangaDlWorker(r);
		}
		// the fetcher's answers (covers, a server's catalogue) have one reader, the servers' poll, which hands covers
		// to mangaCoverOnFetched: a second loop here would take, and drop, the answers a server page waits for
		mangaServersPoll();
		// the Java host's answers (a Java source's list, series, filters and pages) and the manager's events
		mangaJavaPoll(p);
	}

	// ---- a chapter: its pages, then the reader ----

	void Ui::mangaSourceChapterOpen(const std::vector<MangaChapter>& chapters, int i, int page0,
		const std::string& seriesId, const std::string& seriesTitle, Player& p)
	{
		if (i < 0 || i >= (int)chapters.size()) return;
		// a downloaded chapter opens from its CBZ: no page list asked, no network (ui_manga_downloads.cpp)
		if (mangaDlOpenSourceChapter(chapters, i, page0, seriesId, seriesTitle, p)) return;
		mangaSrcPendChapters = chapters;
		mangaSrcPendAt = i;
		mangaSrcPendSeriesId = seriesId;
		mangaSrcPendSeriesTitle = seriesTitle;
		mangaSrcPendPage = std::max(0, page0);
		// the settings are read here, where the Player is: the request is filled where the answer lands (a draw)
		mangaSrcPendMode = mangaModeFor(seriesId, p);
		mangaSrcPendFit = p.settings.mangaFit;
		mangaSrcPendFull = p.settings.mangaOpenFull;
		mangaSrcPendHinge = p.settings.mangaHinge;
		mangaSrcPages.clear();
		mangaSrcUrls.clear();
		mangaSrcHeaders.clear();
		mangaSrcImageAt = 0;
		// from the series page: into the reader page now, its pages loading there (a next chapter picked while
		// reading keeps the one open until the new one is ready)
		// the next or previous chapter while reading: the one open closes (its progress kept) and the page waits the
		// same way, so a step is never a silent pause on the old chapter; fullscreen stays as it was
		if (page == Page::MangaRead)
		{
			mangaRecordProgress();
			mangaSave();
			mangaSrcPendFull = readerStatus.fullscreen;
			readerCmds.push_back({ReaderCmd::Close, 0, (int64_t)SDL_GetTicks()});
			mpFree();
			mfFree();
			mangaZoomAsked = 100;
			mangaProgPage = mangaProgPages = -1;
			page = Page::MangaSeries;	// (so the block below takes it as an open from the series page)
		}
		if (page != Page::MangaRead)
		{
			mangaReadWaiting = true;
			mangaReadSeriesId = seriesId;
			mangaReadChapterTitle = chapterName(chapters[i]);
			mangaMenu = mangaPages = false;
			mangaReadLive = false;
			page = Page::MangaRead;
		}
		mangaSourcePagesAsk(p);
	}

	void Ui::mangaReadWaitCancel()
	{
		// only the chapter's page list goes: the series page keeps what it had
		if (mangaNet && mangaNet->started())
		{
			if (mangaSrcPagesSerial) mangaNet->worker().cancel(mangaSrcPagesSerial);
			if (mangaSrcImageSerial) mangaNet->worker().cancel(mangaSrcImageSerial);
			if (mangaJavaPagesSerial) mangaNet->javaExt().cancelGraphQl(mangaJavaPagesSerial);
		}
		mangaSrcPagesSerial = mangaSrcImageSerial = mangaJavaPagesSerial = 0;
		mangaSrcLoading = false;
		mangaSrcError.clear();
		mangaSrcPendAt = -1;
		mangaReadWaiting = false;
		page = Page::MangaSeries;
	}

	void Ui::drawMangaReadWaiting()
	{
		drawBackIcon();
		drawTextMid(mangaReadChapterTitle, 1, TEXT, 72, 24, 540);
		drawTextMid(mangaTitle(mangaReadSeriesId), 0, DIM, 72, 50, 540);
		Rect area = mangaAreaRect();
		drawBox(area, SURFACE, R_TILE);
		int mid = area.y + area.h / 2;
		if (!mangaSrcLoading && !mangaSrcError.empty())
		{
			drawTextMid("The chapter didn\xE2\x80\x99t open", 1, DANGER, W / 2, mid - 14, 560, 2);
			drawTextMid(mangaSrcError, 0, DIM, W / 2, mid + 16, 560, 2);
			drawHints({{"A", "Try again"}, {"B", "Back"}});
		}
		else
		{
			drawLoader(W / 2, mid, "Loading chapter\xE2\x80\xA6");
			drawHints({{"B", "Back"}});
		}
	}

	void Ui::mangaSourcePagesAsk(Player& p)
	{
		mangaSrcError.clear();
		mangaSrcFailed = FAIL_PAGES;
		if (mangaNet && mangaNet->started())
		{
			if (mangaSrcPagesSerial) mangaNet->worker().cancel(mangaSrcPagesSerial);
			if (mangaSrcImageSerial) mangaNet->worker().cancel(mangaSrcImageSerial);
		}
		mangaSrcPagesSerial = mangaSrcImageSerial = 0;
		if (mangaSrcPendAt < 0 || mangaSrcPendAt >= (int)mangaSrcPendChapters.size()) return;
		{
			std::string full, key;
			splitSourceId(mangaSrcPendSeriesId, full, key);
			if (MangaJava::isSourceId(full)) { mangaJavaPagesAsk(p); return; }
		}
		// a download page list waiting on the same source gives the worker to the chapter about to be read
		mangaDlYieldToSource();
		if (!mangaNet || !mangaNet->started())
		{
			mangaSrcLoading = false;
			mangaSrcError = "Couldn\xE2\x80\x99t ask the source";
			return;
		}
		std::string full, key;
		splitSourceId(mangaSrcPendSeriesId, full, key);
		mangaBrowseRepos(p);
		const MangaSrcRow* row = mangaSrcRowFor(full);
		if (!row)
		{
			mangaSrcLoading = false;
			mangaSrcError = "Source not installed";
			return;
		}
		MangaWorker::Job job;
		job.sourceId = full;
		job.scriptPath = row->file;
		job.kind = MangaWorker::Job::Pages;
		job.json = mangaSrcPendChapters[mangaSrcPendAt].json;
		mangaSrcLoading = true;
		mangaSrcPagesSerial = mangaNet->worker().submit(job);
		if (!mangaSrcPagesSerial)
		{
			mangaSrcLoading = false;
			mangaSrcError = "Couldn\xE2\x80\x99t ask the source";
		}
	}

	void Ui::mangaSourceImageAsk()
	{
		if (!mangaNet || !mangaNet->started() || mangaSrcImageAt < 0 || mangaSrcImageAt >= (int)mangaSrcPages.size()) return;
		// as a chapter's page list does: the reader's own Image job first, the download's ask again after it
		mangaDlYieldToSource();
		std::string full, key;
		splitSourceId(mangaSrcPendSeriesId, full, key);
		const MangaSrcRow* row = mangaSrcRowFor(full);
		MangaWorker::Job job;
		job.sourceId = full;
		job.scriptPath = row ? row->file : "";
		job.kind = MangaWorker::Job::Image;
		job.json = mangaSrcPages[mangaSrcImageAt].json;
		mangaSrcImageSerial = mangaNet->worker().submit(job);
		if (!mangaSrcImageSerial)
		{
			mangaSrcLoading = false;
			mangaSrcError = "Couldn\xE2\x80\x99t ask the source";
			mangaSrcFailed = FAIL_PAGES;
		}
	}

	void Ui::mangaSourceCancel()
	{
		if (mangaNet && mangaNet->started())
		{
			MangaWorker& w = mangaNet->worker();
			if (mangaSrcListSerial) w.cancel(mangaSrcListSerial);
			if (mangaSrcDetailsSerial) w.cancel(mangaSrcDetailsSerial);
			if (mangaSrcChaptersSerial) w.cancel(mangaSrcChaptersSerial);
			if (mangaSrcPagesSerial) w.cancel(mangaSrcPagesSerial);
			if (mangaSrcImageSerial) w.cancel(mangaSrcImageSerial);
		}
		mangaSrcListSerial = mangaSrcDetailsSerial = mangaSrcChaptersSerial = mangaSrcPagesSerial = mangaSrcImageSerial = 0;
		mangaSrcLoading = false;
		mangaJavaCancel();
		mangaCoverCancelAll();
	}

	void Ui::mangaSourceChapterReady(const Player& p)
	{
		if (mangaSrcPendAt < 0 || mangaSrcPendAt >= (int)mangaSrcPendChapters.size()) return;
		// a page that resolved to nothing would open the reader on a hole: ask again instead
		if (mangaSrcUrls.empty())
		{
			mangaSrcLoading = false;
			mangaSrcError = "This chapter has no pages";
			mangaSrcFailed = FAIL_PAGES;
			return;
		}
		for (const auto& u : mangaSrcUrls)
			if (u.empty())
			{
				mangaSrcLoading = false;
				mangaSrcError = "A page didn\xE2\x80\x99t come";
				mangaSrcFailed = FAIL_PAGES;
				return;
			}
		const MangaChapter& c = mangaSrcPendChapters[mangaSrcPendAt];
		mangaReadWaiting = false;
		std::string full, key;
		splitSourceId(mangaSrcPendSeriesId, full, key);
		std::string chapterId = sourceItemId(full, c.key);
		// the chapter open so far is saved before the next one replaces it in readerStatus
		if (page == Page::MangaRead)
		{
			mangaRecordProgress();
			mangaSave();
		}
		mangaSeriesId = mangaSrcPendSeriesId;
		mangaReadSrcChapters = mangaSrcPendChapters;
		mangaReadSeriesId = mangaSrcPendSeriesId;
		mangaReadChapterId = chapterId;
		mangaReadChapterTitle = chapterName(c);
		// path: the chapter's own id, the cache's key (never a folder to open); the pages are the URLs, in order
		mangaRead = {mangaSrcPendSeriesId, mangaSrcPendSeriesTitle, chapterId, mangaReadChapterTitle, chapterId, false,
			mangaSrcPendPage, mangaSrcPendMode, mangaSrcPendFit};
		mangaRead.pageUrls = mangaSrcUrls;
		mangaRead.pageHeaders = mangaSrcHeaders;
		// in History at once: the chapter as opened (its length is the list's), not only after a page turns
		if (mangaStore)
		{
			const ChapterProgress* old = mangaStore->progress(chapterId);
			ChapterProgress cp = old ? *old : ChapterProgress{};
			cp.seriesId = mangaSrcPendSeriesId;
			cp.chapterId = chapterId;
			cp.chapterTitle = mangaReadChapterTitle;
			if (!old)
			{
				cp.page = mangaSrcPendPage;
				cp.pages = (int)mangaSrcUrls.size();
			}
			cp.hidden = !mangaHistOn;
			cp.at = (int64_t)time(nullptr);
			mangaStore->setProgress(cp);
			mangaSave();
		}
		// the status still holds the chapter before: only a change records
		mangaProgPage = readerStatus.page;
		mangaProgPages = readerStatus.pages;
		mangaReadLive = false;
		mangaSliderPage = -1;
		mangaCtrlUntil = 0;
		mangaMenu = false;
		mangaZoomAsked = 100;
		page = Page::MangaRead;
		if (mangaSrcPendFull)
		{
			// Open fullscreen: the command SELECT sends right after the chapter (mangaOpenChapter's), with the
			// hinge it was asked with; the toast is the first fullscreen's, and the setting is written on the next
			// key (mangaKey), where a Player can be changed
			Uint32 now = SDL_GetTicks();
			readerCmds.push_back({ReaderCmd::Fullscreen, 1.0, (int64_t)now, (double)mangaSrcPendHinge});
			mfTagUntil = now + 2500;
			if (!p.settings.mangaFullSeen) mangaToastUntil = now + MG_TOAST_MS;
			mangaFullSeenPending = !p.settings.mangaFullSeen;
		}
	}

	void Ui::mangaSourceChapterStep(int dir, Player& p)
	{
		int i = mangaReadChapterIndex();
		if (i < 0) return;
		int j = i + dir;
		if (j < 0 || j >= (int)mangaReadSrcChapters.size()) return;
		mangaSourceChapterOpen(mangaReadSrcChapters, j, 0, mangaReadSeriesId, mangaTitle(mangaReadSeriesId), p);
	}

	// ---- a source series: its details, its chapters, In library ----

	bool Ui::mangaSourceSeriesOn() const
	{
		return page == Page::MangaSeries && isSourceId(mangaSeriesId);
	}

	void Ui::mangaOpenSourceSeries(const std::string& id, int from, const std::string& json, const std::string& cover,
		const std::string& title, Player& p)
	{
		std::string full, key;
		splitSourceId(id, full, key);
		if (mangaNet) mangaNet->start();
		mangaBrowseRepos(p);
		mangaSourceCancel();
		mangaSrcId = full;
		mangaSrcKey = key;
		const MangaSrcRow* row = mangaSrcRowFor(full);
		mangaSrcName = row ? row->name : "";
		mangaSrcFile = row ? row->file : "";
		std::string name = title;
		std::string cov = cover;
		if (mangaStore)
			if (const MangaSeries* old = mangaStore->series(id))
			{
				if (name.empty()) name = old->title;
				if (cov.empty()) cov = old->cover;
			}
		mangaSrcTitle = name.empty() ? key : name;
		mangaSrcCover = cov;
		mangaSrcItemJson = json.empty() ? mangaSourceItemJson(key, mangaSrcTitle) : json;
		mangaSrcInfo = MangaDetails{};
		mangaSrcInfoOn = false;
		mangaSrcChapters.clear();
		mangaSrcChaptersOn = false;
		mangaSrcError.clear();
		mangaSrcLoading = false;
		mangaSrcFailed = FAIL_NONE;
		mangaSeriesId = id;
		mangaSeriesFrom = from;
		mserCursor = -1;
		mserBtn = 0;
		mserScroll = 0;
		page = Page::MangaSeries;
		// the store is the only place a source series' title and cover live: History and the Library read them from
		// there (inLibrary false until Y says so, so it doesn't show in the Library before that)
		if (mangaStore)
		{
			MangaSeries m;
			m.id = id;
			m.title = mangaSrcTitle;
			m.source = full;
			m.cover = mangaSrcCover;
			if (const MangaSeries* old = mangaStore->series(id))
			{
				m.added = old->added;
				m.lastRead = old->lastRead;
				m.inLibrary = old->inLibrary;
				m.readingMode = old->readingMode;
			}
			else
			{
				m.added = (int64_t)time(nullptr);
				m.inLibrary = false;	// only Y puts it in the Library
			}
			mangaStore->putSeries(m);
			mangaSave();
		}
		// the card's copy first (design 6): the page opens with its details and chapters at once, and nothing is asked
		// of the source; only Refresh (L2) does that (a Java source's host starts then, not for a kept list)
		mangaSrcFetchedAt = 0;
		mangaSrcRefreshing = mangaSrcHadList = false;
		mangaSrcFetchDetailsOk = mangaSrcFetchChaptersOk = false;
		if (!mangaSourceSeriesCacheLoad()) mangaSourceSeriesLoad(p);
	}

	void Ui::mangaSourceSeriesLoad(Player& p, bool refresh)
	{
		if (MangaJava::isSourceId(mangaSrcId)) { mangaJavaSeriesLoad(p, refresh); return; }
		(void)p;
		// a refresh keeps the list on screen; the first load (no cache) starts from nothing, as it always has
		mangaSrcRefreshing = refresh;
		mangaSrcHadList = mangaSrcChaptersOn;
		mangaSrcFetchDetailsOk = mangaSrcFetchChaptersOk = false;
		mangaSrcError.clear();
		mangaSrcFailed = FAIL_SERIES;
		if (!refresh)
		{
			mangaSrcInfo = MangaDetails{};
			mangaSrcInfoOn = false;
			mangaSrcChapters.clear();
			mangaSrcChaptersOn = false;
		}
		if (mangaNet && mangaNet->started())
		{
			if (mangaSrcDetailsSerial) mangaNet->worker().cancel(mangaSrcDetailsSerial);
			if (mangaSrcChaptersSerial) mangaNet->worker().cancel(mangaSrcChaptersSerial);
		}
		mangaSrcDetailsSerial = mangaSrcChaptersSerial = 0;
		if (!mangaNet || !mangaNet->started())
		{
			mangaSourceSeriesFail("Couldn\xE2\x80\x99t ask the source");
			return;
		}
		if (mangaSrcFile.empty())
		{
			mangaSourceSeriesFail("Source not installed");
			return;
		}
		MangaWorker::Job job;
		job.sourceId = mangaSrcId;
		job.scriptPath = mangaSrcFile;
		job.json = mangaSrcItemJson;
		job.kind = MangaWorker::Job::Details;
		mangaSrcDetailsSerial = mangaNet->worker().submit(job);
		job.kind = MangaWorker::Job::Chapters;
		mangaSrcChaptersSerial = mangaNet->worker().submit(job);
		mangaSrcLoading = mangaSrcDetailsSerial != 0 || mangaSrcChaptersSerial != 0;
		if (!mangaSrcDetailsSerial && !mangaSrcChaptersSerial) mangaSourceSeriesFail("Couldn\xE2\x80\x99t ask the source");
	}

	void Ui::mangaSourceSeriesRefresh(Player& p)
	{
		// one check at a time; the page's line says "Checking for new chapters…" while it runs
		if (mangaSrcRefreshing) return;
		mangaSourceSeriesLoad(p, true);
	}

	void Ui::mangaSourceSeriesFail(const std::string& why)
	{
		// before any job: a page with a cached list keeps it and says so in a toast; one without shows today's line
		mangaSrcLoading = false;
		if (mangaSrcHadList || mangaSrcChaptersOn)
		{
			if (mangaSrcRefreshing) mangaNotes.push_back("Couldn\xE2\x80\x99t check for new chapters");
			else if (!why.empty()) mangaNotes.push_back(why);
		}
		else
		{
			mangaSrcError = why;
			mangaSrcFailed = FAIL_SERIES;
		}
		mangaSrcRefreshing = false;
	}

	void Ui::mangaSourceSeriesFetched()
	{
		// both halves are in (the Lua jobs, or the Java query's one answer): a good fetch replaces the card's copy,
		// a failed one keeps what shows and says so in a short toast rather than wiping the list
		if (mangaSrcDetailsSerial || mangaSrcChaptersSerial) return;
		mangaSrcLoading = false;
		if (mangaSrcFetchDetailsOk && mangaSrcFetchChaptersOk)
		{
			mangaSrcFetchedAt = (int64_t)time(nullptr);
			mangaSourceSeriesCacheSave();
			mangaSrcError.clear();
			mangaSrcFailed = FAIL_NONE;
		}
		else if (mangaSrcHadList || mangaSrcChaptersOn)
		{
			mangaNotes.push_back(mangaSrcRefreshing ? "Couldn\xE2\x80\x99t check for new chapters"
				: "Couldn\xE2\x80\x99t refresh this series");
			mangaSrcError.clear();
			mangaSrcFailed = FAIL_NONE;
		}
		mangaSrcRefreshing = false;
	}

	bool Ui::mangaSourceSeriesCacheLoad()
	{
		MangaSeriesCache c;
		if (!mangaSeriesCacheRead(stateDirPath, mangaSeriesId, c) || !c.chaptersOn) return false;
		mangaSrcInfo = std::move(c.details);
		mangaSrcInfoOn = c.detailsOn;
		mangaSrcChapters = std::move(c.chapters);
		mangaSrcChaptersOn = true;
		mangaSrcFetchedAt = c.fetchedAt;
		mangaSrcLoading = false;
		mangaSrcError.clear();
		mangaSrcFailed = FAIL_NONE;
		if (mangaSrcTitle.empty() && !mangaSrcInfo.title.empty()) mangaSrcTitle = mangaSrcInfo.title;
		return true;
	}

	void Ui::mangaSourceSeriesCacheSave()
	{
		if (stateDirPath.empty() || mangaSeriesId.empty()) return;
		MangaSeriesCache c;
		c.details = mangaSrcInfo;
		c.chapters = mangaSrcChapters;
		c.fetchedAt = mangaSrcFetchedAt;
		c.detailsOn = mangaSrcInfoOn;
		c.chaptersOn = mangaSrcChaptersOn;
		mangaSeriesCacheWrite(stateDirPath, mangaSeriesId, c);
	}

	void Ui::mangaSourceSeriesChoose(int i, Player& p)
	{
		// (an extension's chapter, as its Resume, waits for the manga engine; a downloaded one reads from the card)
		if (!mangaSourceEngineReady() && i >= 0 && i < (int)mangaSrcChapters.size() &&
			mangaDlMarkFor(mangaSrcId, mangaSeriesId, mangaSrcChapters[i]).kind != MangaDlMark::OnCard)
		{
			mangaNotice("The manga engine is still loading\xE2\x80\xA6", 2000);
			return;
		}
		if (i < 0 || i >= (int)mangaSrcChapters.size()) return;
		std::string chapterId = sourceItemId(mangaSrcId, mangaSrcChapters[i].key);
		// its saved page, unless it was read to its end (then the start again), as the card's chapters do
		int page0 = 0;
		if (mangaStore)
			if (const ChapterProgress* pr = mangaStore->progress(chapterId))
				if (!pr->read) page0 = pr->page;
		mangaSourceChapterOpen(mangaSrcChapters, i, page0, mangaSeriesId, mangaSrcInfo.title.empty() ? mangaSrcTitle : mangaSrcInfo.title, p);
	}

	bool Ui::mangaSourceEngineReady() const
	{
		// a series from an extension reads through the manga engine: Resume waits until it answers
		std::string full, key;
		splitSourceId(mangaSeriesId, full, key);
		if (!MangaJava::isSourceId(full)) return true;
		return mangaNet && mangaNet->started() && mangaNet->javaExt().state() == JavaExtHost::State::Running;
	}

	void Ui::mangaSourceSeriesResume(Player& p)
	{
		if (!mangaSourceEngineReady())
		{
			mangaNotice("The manga engine is still loading\xE2\x80\xA6", 2000);
			return;
		}
		if (mangaSrcChapters.empty()) return;
		std::vector<MangaChapterMark> marks;
		marks.reserve(mangaSrcChapters.size());
		for (const auto& c : mangaSrcChapters)
		{
			MangaChapterMark m;
			if (mangaStore)
				if (const ChapterProgress* pr = mangaStore->progress(sourceItemId(mangaSrcId, c.key)))
				{
					m.read = pr->read;
					m.started = pr->pages > 0 || pr->page > 0;
					m.page = pr->page;
				}
			marks.push_back(m);
		}
		MangaResumePoint r = mangaResume(marks);
		if (r.chapter >= 0)
			mangaSourceChapterOpen(mangaSrcChapters, r.chapter, r.page, mangaSeriesId,
				mangaSrcInfo.title.empty() ? mangaSrcTitle : mangaSrcInfo.title, p);
	}

	void Ui::mangaSourceSeriesInLibrary(Player& p)
	{
		(void)p;
		if (!mangaStore) return;
		if (const MangaSeries* ms = mangaStore->series(mangaSeriesId))
		{
			mangaStore->setInLibrary(mangaSeriesId, !ms->inLibrary);
			mangaSave();
			return;
		}
		// the full source id is the series' source, its cover the URL the source gave (no cover is fetched by the
		// Library yet: a source series keeps its generated one)
		MangaSeries m;
		m.id = mangaSeriesId;
		m.title = mangaSrcTitle;
		m.source = mangaSrcId;
		m.cover = mangaSrcCover;
		m.added = (int64_t)time(nullptr);
		m.inLibrary = true;
		mangaStore->putSeries(m);
		mangaSave();
	}

	bool Ui::mangaSourceSeriesKey(Button b, Player& p)
	{
		// the Download sheet takes the keys while it shows
		if (mdlSheet != MDLS_NONE) return mangaDlSheetKey(b, p);
		if (b == Button::Start) { openMangaSettings(); return false; }
		if (b == Button::R2) { mangaDlSheetOpen(p); return false; }
		// L2 checks the source again (it is free on this page: the reader's L2 steps chapters, not this page)
		if (b == Button::L2) { mangaSourceSeriesRefresh(p); return false; }
		if (b == Button::B)
		{
			// back to where it was opened: the source's grid, the Library or History (a chapter still coming goes)
			mangaSourceCancel();
			page = Page::Manga;
			mangaTab = mangaSeriesFrom == 2 ? MT_HISTORY : mangaSeriesFrom == 3 ? MT_BROWSE : MT_LIBRARY;
			mangaView = mangaSeriesFrom == 3 ? 1 : 0;
			// back on the grid: its filters are still the run's, or are asked for now if the first ask was lost
			if (mangaView == 1) mangaFilterAsk();
			return false;
		}
		// (In library is its button alone: touched, not a key)
		// the button row (cursor -1): LEFT/RIGHT go along it, A presses the one lit
		if (mserCursor < 0 && (b == Button::Left || b == Button::Right))
		{
			mserBtn = std::clamp(mserBtn + (b == Button::Right ? 1 : -1), 0, 3);
			return false;
		}
		if (b == Button::A)
		{
			if (!mangaSrcError.empty() && !mangaSrcChaptersOn) mangaSourceRetry(p);
			else if (mserCursor < 0)
			{
				if (mserBtn == 1) mangaDlSheetOpen(p);
				else if (mserBtn == 2) mangaSourceSeriesRefresh(p);
				else if (mserBtn == 3) mangaSourceSeriesInLibrary(p);
				else mangaSourceSeriesResume(p);
			}
			else mangaSourceSeriesChoose(mserCursor, p);
			return false;
		}
		int n = (int)mangaSrcChapters.size();
		if (b == Button::X && mserCursor >= 0 && mserCursor < n && mangaStore)
		{
			// the chapter's read mark, as X does on the card's series page
			std::string cid = sourceItemId(mangaSrcId, mangaSrcChapters[mserCursor].key);
			const ChapterProgress* pr = mangaStore->progress(cid);
			mangaStore->markRead(mangaSeriesId, {cid}, !(pr && pr->read));
			mangaSave();
			return false;
		}
		// Resume is the row before the chapters
		int total = n + 1, cur = mserCursor + 1;
		switch (b)
		{
		case Button::Up: cur = (cur + total - 1) % total; break;
		case Button::Down: cur = (cur + 1) % total; break;
		case Button::Left: cur = std::max(0, cur - mserRows()); break;
		case Button::Right: cur = std::min(total - 1, cur + mserRows()); break;
		default: break;
		}
		mserCursor = cur - 1;
		if (mserCursor >= 0)
		{
			if (mserCursor < mserScroll) mserScroll = mserCursor;
			if (mserCursor >= mserScroll + mserRows()) mserScroll = mserCursor - mserRows() + 1;
		}
		return false;
	}

	bool Ui::mangaSourceSeriesPointer(const Pointer& pt, int x, int y, Player& p)
	{
		int n = (int)mangaSrcChapters.size();
		// the chapter list follows a finger: a drag scrolls it by rows, a tap without one reads the row it went down on
		if (pt.kind == Pointer::Move && mserDragY >= 0)
		{
			int dy = y - mserDragY;
			if (std::abs(dy) > 12) mserDragMoved = true;
			if (mserDragMoved)
			{
				int s = std::clamp(mserDragScroll - (int)std::lround(dy / (double)ROW_H), 0, std::max(0, n - mserRows()));
				if (s != mserScroll)
				{
					mserScroll = s;
					// the cursor stays among the rows shown (the keys go on from there)
					if (mserCursor >= 0) mserCursor = std::clamp(mserCursor, s, std::min(n - 1, s + mserRows() - 1));
				}
			}
			return false;
		}
		if (pt.kind == Pointer::Up && mserDragY >= 0)
		{
			int tap = mserDragTap;
			bool moved = mserDragMoved;
			mserDragY = mserDragTap = -1;
			mserDragMoved = false;
			if (!moved && tap >= 0 && tap < n)
			{
				mserCursor = tap;
				mangaSourceSeriesChoose(tap, p);
			}
			return false;
		}
		if (pt.kind != Pointer::Down) return false;
		if (mdlSheet != MDLS_NONE) return mangaDlSheetPointer(pt, x, y, p);
		if (mangaDlButtonRect().hit(x, y))
		{
			mangaDlSheetOpen(p);
			return false;
		}
		if (mangaSrcRefreshRect().hit(x, y))
		{
			mangaSourceSeriesRefresh(p);
			return false;
		}
		if (mangaSrcResumeRect().hit(x, y))
		{
			mserCursor = -1;
			mangaSourceSeriesResume(p);
			return false;
		}
		if (mangaSrcHeartRect().hit(x, y))
		{
			mangaSourceSeriesInLibrary(p);
			return false;
		}
		if (n && y >= MG_SRC_SER_TOP && y < MG_SRC_SER_TOP + mserRows() * ROW_H)
		{
			// opened as the finger lifts, if it didn't drag
			int i = mserScroll + (y - MG_SRC_SER_TOP) / ROW_H;
			mserDragY = y;
			mserDragScroll = mserScroll;
			mserDragMoved = false;
			mserDragTap = i >= 0 && i < n ? i : -1;
		}
		return false;
	}

	void Ui::drawMangaSourceSeries(const Player& p)
	{
		// the downloads' marks and the Download sheet need the card's own list (read at most every 1.5 s), and a
		// page list left waiting (the app reopened) goes on
		mangaDlEnsureCard();
		mangaDlAskPump();
		std::string title = mangaSrcInfoOn && !mangaSrcInfo.title.empty() ? mangaSrcInfo.title : mangaSrcTitle;
		drawHeader(title.empty() ? "Series" : title, p);
		const MangaSeries* ms = mangaStore ? mangaStore->series(mangaSeriesId) : nullptr;
		int n = (int)mangaSrcChapters.size();
		// the count and Resume walk every chapter (and the store): made again only when either changed, not on every
		// cursor move (a long list lagged on the handheld)
		std::string sumKey = mangaSrcId + "|" + mangaSeriesId + "|" + std::to_string(n) + "|" +
			std::to_string((unsigned long long)(mangaStore ? mangaStore->serial() : 0)) +
			(n ? "|" + mangaSrcChapters.front().key + "|" + mangaSrcChapters.back().key : std::string());
		if (sumKey != mserSumKey)
		{
			mserSumKey = sumKey;
			std::vector<std::string> ids;
			ids.reserve(n);
			for (const auto& c : mangaSrcChapters) ids.push_back(sourceItemId(mangaSrcId, c.key));
			mserSumUnread = mangaStore ? mangaStore->unread(mangaSeriesId, ids) : 0;
			std::vector<MangaChapterMark> marks;
			marks.reserve(n);
			for (const auto& id : ids)
			{
				MangaChapterMark m;
				if (mangaStore)
					if (const ChapterProgress* pr = mangaStore->progress(id))
					{
						m.read = pr->read;
						m.started = pr->pages > 0 || pr->page > 0;
						m.page = pr->page;
					}
				marks.push_back(m);
			}
			mserSumResume = mangaResume(marks);
		}
		int unread = mserSumUnread;
		// opened from History: the cursor on that chapter, once the list is in
		if (!mserFocusChapter.empty() && n > 0)
		{
			for (int i = 0; i < n; i++)
				if (sourceItemId(mangaSrcId, mangaSrcChapters[i].key) == mserFocusChapter)
				{
					mserCursor = i;
					mserScroll = std::max(0, std::min(i - 1, n - mserRows()));
					break;
				}
			mserFocusChapter.clear();
		}
		drawTextMid(std::to_string(n) + (n == 1 ? " chapter" : " chapters") + " \xC2\xB7 " + std::to_string(unread) + " unread",
			0, DIM, 16, 68);
		// the details the Library's series card would put on the top panel: here they are on this page (the reader
		// holds the top panel, and the app has no card of its own). A failure or a running job says so on that line
		std::string line;
		bool bad = !mangaSrcError.empty();
		if (bad) line = mangaSrcError;
		else if (mangaSrcLoading && n == 0 && !mangaSrcRefreshing) line = "Loading\xE2\x80\xA6";
		else
		{
			if (mangaSrcInfoOn && !mangaSrcInfo.author.empty()) line = mangaSrcInfo.author;
			if (mangaSrcInfoOn && !mangaSrcInfo.status.empty())
				line += (line.empty() ? "" : " \xC2\xB7 ") + mangaSrcInfo.status;
		}
		if (!line.empty()) drawTextMid(line, 0, bad ? DANGER : DIM, 16, 86, 592);
		if (mangaSrcInfoOn && !mangaSrcInfo.description.empty())
		{
			std::vector<std::string> lines = wrapText(mangaSrcInfo.description, 0, 592, 2);
			for (size_t i = 0; i < lines.size(); i++) drawTextMid(lines[i], 0, DIM_ON, 16, 104 + (int)i * 18, 592);
		}
		// the cached list's age, one line under the meta: only when it is more than a day old (a fresh one adds
		// nothing, and nothing refreshes by itself); a running Refresh says what it is doing instead
		int64_t now = (int64_t)time(nullptr);
		if (mangaSrcRefreshing) drawTextMid("Checking for new chapters\xE2\x80\xA6", 0, DIM, W / 2, 140, 592, 2);
		else if (mangaCheckedStale(mangaSrcFetchedAt, now))
		{
			// "Chapters checked 3 days ago · " in grey, its key in yellow
			std::string stem = "Chapters checked " + mangaCheckedAgo(mangaSrcFetchedAt, now) + " ago \xC2\xB7 ";
			const std::string key = "L2 Refresh";
			int w1 = (int)std::lround(textWidth(stem, 0) / sx), w2 = (int)std::lround(textWidth(key, 0) / sx);
			int x = W / 2 - (w1 + w2) / 2;
			drawTextMid(stem, 0, DIM, x, 140);
			drawTextMid(key, 0, ACCENT, x + w1, 140);
		}
		// Resume: the first started-but-unread chapter, else the first unread, else the first
		MangaResumePoint resume = mserSumResume;
		// one row of four: each a single line of glyph + label, the keys only in the hints under it (nothing in the
		// buttons: Resume with no key, Download's R2 on a second line and the heart's Y under it read as three kinds)
		// (the row is one stop for UP/DOWN; LEFT/RIGHT light each button in turn, mserBtn)
		bool onRow = mserCursor < 0;
		auto lit = [&](int i) { return onRow && mserBtn == i; };
		Rect rb = mangaSrcResumeRect();
		drawBox(rb, mangaPressed == MG_RESUME ? PRESSED : lit(0) ? BUTTON : SURFACE, R_BUTTON, lit(0));
		drawButton(rb, &G_PLAY, "Resume", nullptr, n == 0 || !mangaSourceEngineReady(), 1);
		// Download: the sheet of This chapter / Next 5 unread / All unread / Choose chapters
		Rect db = mangaDlButtonRect();
		drawBox(db, lit(1) ? BUTTON : SURFACE, R_BUTTON, lit(1));
		drawButton(db, &G_DOWNLOAD, "Download", nullptr, n == 0, 1);
		// Refresh (L2): ask the source again and replace the card's copy on success
		Rect fb = mangaSrcRefreshRect();
		drawBox(fb, lit(2) ? BUTTON : SURFACE, R_BUTTON, lit(2));
		drawButton(fb, &G_REFRESH, "Refresh", nullptr, mangaSrcRefreshing, 1);
		Rect hb = mangaSrcHeartRect();
		bool inLib = ms && ms->inLibrary;
		drawBox(hb, mangaPressed == MG_HEART ? PRESSED : lit(3) ? BUTTON : SURFACE, R_BUTTON, lit(3));
		drawButton(hb, inLib ? &G_HEART : &G_HEART_O, "Library", nullptr, false, 1, inLib);
		if (n == 0)
		{
			if (!mangaSrcError.empty())
			{
				drawTextMid(browserCheckText(mangaSrcError) ? "This source is behind a browser check" : mangaSrcError, 1, DANGER, W / 2, 208, 560, 2);
				drawHints({{"A", "Try again"}, {"L2", "Refresh"}, {"B", "Back"}});
			}
			else
			{
				// (a running job says "Loading…" or "Checking for new chapters…" above)
				if (!mangaSrcLoading) drawTextMid("No chapters", 0, DIM, W / 2, 208, 0, 2);
				else drawLoader(W / 2, 300, "Loading chapters\xE2\x80\xA6");
				drawHints({{"L2", "Refresh"}, {"B", "Back"}});
			}
			return;
		}
		if (mserCursor >= n) mserCursor = n - 1;
		for (int i = mserScroll; i < n && i < mserScroll + mserRows(); i++)
		{
			const MangaChapter& c = mangaSrcChapters[i];
			int y = MG_SRC_SER_TOP + (i - mserScroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
			bool at = mserCursor == i;
			if (at) drawFocus({16, y, 608, ROW_H - 4});
			// the one Resume would open: a small ACCENT dot
			if (resume.chapter == i) fillRound({28, mid - 4, 8, 8}, ACCENT, 4);
			const ChapterProgress* pr = mangaStore ? mangaStore->progress(sourceItemId(mangaSrcId, c.key)) : nullptr;
			bool read = pr && pr->read;
			// a download mark (queued, progress, on the card) takes the row's right: the page line makes room for it
			// (on the card: a small icon at the very right, the reading progress still beside it)
			MangaDlMark dm = mangaDlMarkFor(mangaSrcId, mangaSeriesId, c);
			bool onCard = dm.kind == MangaDlMark::OnCard;
			bool marked = dm.kind != MangaDlMark::None && !onCard;
			int right = onCard ? 580 : 608;
			drawTextMid(chapterName(c), 1, read ? DIM : at ? ACCENT : TEXT, 48, mid, marked ? 420 : onCard ? 440 : 470);
			if (onCard) drawMangaDlMark(dm, 608, mid, at);
			if (read) drawGlyphMid(G_CHECK, marked ? 490 : right - 12, mid, DIM);
			else if (!marked && pr && (pr->pages > 0 || pr->page > 0))
				drawTextMid(pageLine(pr->page, pr->pages), 0, at ? DIM_ON : DIM, right, mid, 150, 1);
			if (marked) drawMangaDlMark(dm, 608, mid, at);
		}
		drawScrollBar(MG_SRC_SER_TOP, mserRows() * ROW_H - 4, mserScroll, mserRows(), n);
		if (!mangaSrcError.empty()) drawTextMid(mangaSrcError, 0, DANGER, W / 2, MG_SRC_STATUS_Y, 560, 2);
		// X still marks a chapter read, but six hints don't fit this panel's line: its action stays (the row's marks
		// show read and unread), and the four buttons carry the rest
		if (mserCursor < 0)
		{
			static const char* ROW[4] = {"Resume", "Download", "Refresh", "Library"};
			drawHints({{"A", ROW[std::clamp(mserBtn, 0, 3)]}, {"LEFT/RIGHT", "Buttons"}, {"DOWN", "Chapters"}, {"B", "Back"}});
		}
		else if (!mangaSourceEngineReady()) drawHints({{"", "Loading manga engine\xE2\x80\xA6"}, {"X", "Read / unread"}, {"B", "Back"}});
		else drawHints({{"A", "Read"}, {"X", "Read / unread"}, {"R2", "Download"}, {"L2", "Refresh"}, {"B", "Back"}});
		if (mdlSheet != MDLS_NONE) drawMangaDlSheet(p);
	}

	// ---- signature() ----

	std::string Ui::mangaNetSignature() const
	{
		char buf[320];
		snprintf(buf, sizeof buf, "|bs%d %d %d %d %d %d %d %d %zu %zu %zu %d %d %d %d %d",
			mangaView, mangaSrcTab, (int)mangaSrcHasLatest, mbrowseCursor, mbrowseScroll, mangaSrcCursor, mangaSrcScroll,
			mangaSrcPage, mangaSrcItems.size(), mangaSrcChapters.size(), mangaSrcRows.size(), (int)mangaSrcMore,
			(int)mangaSrcLoading, (int)mangaSrcInfoOn, (int)mangaSrcChaptersOn, (int)mangaSrcPendFull);
		std::string s = buf;
		s += "|" + mangaSrcId + "|" + mangaSrcQuery + "|" + mangaSrcError + "|" + mangaSrcKey + "|" + mangaSrcTitle;
		// the cached list's age line (the day count moves at each 24 h) and a running Refresh's "Checking…"
		s += "|cs" + std::to_string((int)mangaSrcRefreshing) + std::to_string((int)mangaSrcChaptersOn) +
			" " + std::to_string((long long)mangaSrcFetchedAt) + " " +
			std::to_string(mangaSrcFetchedAt > 0 ? ((long long)time(nullptr) - mangaSrcFetchedAt) / 86400 : -1LL);
		s += "|mf" + std::to_string((unsigned long long)mangaFilterLocal) + "|sb" + std::to_string(mserBtn) + "|hm" + std::to_string((int)mhisMenu) + std::to_string(mhisMenuCursor);
		// the lists' own ends: page 2 appended after page 1 changes the count, but a page's items replaced with as
		// many others (a search for another word, a retry) would not show otherwise
		if (!mangaSrcItems.empty())
			s += "|i" + mangaSrcItems.front().key + mangaSrcItems.back().key + mangaSrcItems.front().title;
		if (!mangaSrcChapters.empty())
			s += "|c" + mangaSrcChapters.front().key + mangaSrcChapters.back().key + mangaSrcChapters.front().name;
		if (mangaSrcInfoOn)
			s += "|" + mangaSrcInfo.title + mangaSrcInfo.author + mangaSrcInfo.status +
				std::to_string(mangaSrcInfo.description.size());
		// the saved servers: Browse's rows, their own pages and the form (ui_manga_servers.cpp)
		s += mangaServersSignature();
		// the network folders' page and series (ui_manga_netfolders.cpp)
		{
			char nf[160];
			snprintf(nf, sizeof nf, "|nf%d %d %d %zu %zu %d %d %llu %llu", mangaNetView, mangaNetCursor, mangaNetScroll,
				mangaNetRows.size(), mangaNetChapters.size(), (int)mangaNetLoading, mangaNetPendAt,
				(unsigned long long)mangaNetSerial, (unsigned long long)mangaNetFetchSerial);
			s += nf;
			s += "|" + mangaNetServerId + "|" + mangaNetDir + "|" + mangaNetSeriesPath + "|" + mangaNetError;
			if (!mangaNetRows.empty()) s += "|nr" + mangaNetRows.front().name + mangaNetRows.back().name;
			if (!mangaNetChapters.empty()) s += "|nc" + mangaNetChapters.front().path + mangaNetChapters.back().path;
		}
		if (mangaNet) s += "|mn" + std::to_string((unsigned long long)mangaNet->serial()) + std::to_string((int)mangaNet->started());
		// the Java extensions' rows and sheets (ui_manga_java.cpp): the host's state and the page's own changes
		{
			char js[240];
			snprintf(js, sizeof js, "|jv%llu %d %d %d %llu %llu %llu %llu",
				(unsigned long long)mstJavaLocal, mstJavaModal, mstJavaModalCursor, (int)mstJavaUninstallArmed,
				(unsigned long long)mangaJavaListSerial, (unsigned long long)mangaJavaSeriesSerial,
				(unsigned long long)mangaJavaPagesSerial, (unsigned long long)mangaJavaManagerSerial());
			s += js;
			s += "|jh" + std::to_string((unsigned long long)mangaJavaSerial());
			s += "|jsta" + mangaJavaStatusLine() + "|" + mstJavaModalId;
			for (const auto& kv : mstJavaBusy) s += "|jb" + kv.first + kv.second;
		}
		return s;
	}
}
