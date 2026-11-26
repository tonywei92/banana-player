#include "ui.h"
#include "manganet.h"
#include "manga_ui_logic.h"
#include "mangalocal.h"
#include "mangaseriescache.h"
#include "ui_internal.h"
#include "ui_reader.h"
#include "ui_manga.h"
#include "ui_kit.h"
#include "ui_glyphs.h"
#include "previewpane.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ftw.h>
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

	void Ui::Manga::mangaBrowseReset(Player& p)
	{
		(void)p;
		mangaSourceCancel();
		mangaOpenWaitDrop();
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

	void Ui::Manga::mangaBrowseRepos(const Player& p)
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
			mangaSrcRows.push_back({MangaJava::sourceId(s.id), s.title(), s.lang, "", s.nsfw, true, mangaJavaSourceObsolete(MangaJava::sourceId(s.id))});
		// the ones whose extension left its repository go to the bottom (the order inside each part stays)
		std::stable_partition(mangaSrcRows.begin(), mangaSrcRows.end(), [](const MangaSrcRow& r) { return !r.obsolete; });
	}

	int Ui::Manga::mangaBrowseServerCount() const
	{
		// the saved servers, by name (MangaNet loads the store when it starts; nothing is listed without it)
		if (!mangaNet || !mangaNet->started()) return 0;
		return (int)mangaNet->servers().list().size();
	}

	int Ui::Manga::mangaBrowseCount(const Player& p) const
	{
		// On this handheld, the network connections, the servers, Sources…, Add a server…
		int n = 3 + mangaNetFoldersCount() + mangaBrowseServerCount();
		if (mangaSrcRows.empty()) n++;	// the Add sources row: A opens the Sources page
		for (const auto& r : mangaSrcRows)
			if (!r.adult || p.settings.mangaAdult) n++;
		return n;
	}

	const Ui::Manga::MangaSrcRow* Ui::Manga::mangaBrowseSourceAt(int i, const Player& p) const
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

	const Ui::Manga::MangaSrcRow* Ui::Manga::mangaSrcRowFor(const std::string& fullId) const
	{
		for (const auto& r : mangaSrcRows)
			if (r.id == fullId) return &r;
		return nullptr;
	}

	void Ui::Manga::mangaBrowseChoose(int i, Player& p)
	{
		if (i <= 0)
		{
			// On this handheld: its series, as it was
			mBrowseList = true;
			mListCursor = mListScroll = 0;
			mlocSheet = MLS_NONE;
			mlocDeleteArmed = false;
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
		// Manage sources: the Sources page on Available (the sources to install, the installed ones on top)
		if (i == nets + servers + 1)
		{
			openMangaSources(p);
			mstTab = MST_AVAILABLE;
			return;
		}
		// Add a server… (screens.js manga-browse): the form Y opens
		if (i == nets + servers + 2)
		{
			mangaServerAddOpen(p);
			return;
		}
		// no source installed: the row after Manage sources is Add repository (A opens the Sources page on its
		// Repositories, where one is added: the way to a first source)
		if (mangaSrcRows.empty())
		{
			openMangaSources(p);
			mstTab = MST_REPOSITORIES;
			return;
		}
		if (const MangaSrcRow* r = mangaBrowseSourceAt(i - mangaBrowseSourceStart(), p)) mangaOpenSource(*r, p);
	}

	bool Ui::Manga::mangaBrowseKey(Button b, Player& p)
	{
		// a network folder's page (its connections and a folder's series) takes the keys while it shows
		if (mangaNetView != MNF_OFF) return mangaNetFoldersKey(b, p);
		if (mBrowseList)
		{
			int n = (int)mangaLocal.size();
			// Y: the series' Open / Info / Delete; X: where to put manga for it to show here
			if (b == Button::X) { mangaLocalHelpOpen(); return false; }
			if (b == Button::Y)
			{
				if (n > 0)
				{
					mlocSheet = MLS_MENU;
					mlocCursor = 0;
					mlocDeleteArmed = false;
					mangaLocalSheetOpen();
				}
				return false;
			}
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

	bool Ui::Manga::mangaBrowsePointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down) return false;
		// a network folder's page is the whole list while it shows
		if (mangaNetView != MNF_OFF) return mangaNetFoldersPointer(pt, x, y, p);
		if (mBrowseList)
		{
			// the series follow a finger: a drag scrolls them, a tap (lifted where it went down) opens one
			mangaRowsPointer(mlLocal, p, x, y, [this] { return (int)mangaLocal.size(); }, &mListScroll, &mListCursor,
				[this](int i) { return mangaLocalRowAt(i); }, [this, &p](int i, bool) {
					if (i < 0 || i >= (int)mangaLocal.size()) return;
					mListCursor = i;
					mangaChoose(MT_BROWSE, i, p);
				});
			return false;
		}
		mangaBrowseRepos(p);
		// the rows follow a finger (the kit's rows, as drawMangaBrowseRows laid them out); a server's Edit key is its X
		mangaRowsPointer(mlBrowse, p, x, y, [this, &p] { return mangaBrowseCount(p); }, &mbrowseScroll, &mbrowseCursor,
			[this, &p](int i) { return mangaBrowseRowAt(p, i); }, [this, &p](int i, bool onKey) {
				if (i < 0 || i >= mangaBrowseCount(p)) return;
				int nets = mangaNetFoldersCount();
				mbrowseCursor = i;
				if (onKey && i > nets && i <= nets + mangaBrowseServerCount()) mangaServerModalOpen(i - nets, p);
				else mangaBrowseChoose(i, p);
			});
		return false;
	}

	Ui::Manga::MangaRow Ui::Manga::mangaBrowseRowAt(const Player& p, int i) const
	{
		// Browse's rows (screens.js manga-browse): the card, the network folders, the servers, Sources, the sources
		MangaRow m;
		int nets = mangaNetFoldersCount(), servers = mangaBrowseServerCount();
		int manageAt = nets + servers + 1, srcStart = manageAt + 2;
		if (i == 0)
		{
			// (the card's listing not in yet: what it has found so far, not a "0 series")
			int count = mangaScanning && !mangaScanned ? (int)mangaScanCount : (int)mangaLocal.size();
			m.glyph = &G_CARD;
			m.name = "On this handheld";
			m.note = mangaScanning && !mangaScanned && count == 0 ? std::string("Reading\xE2\x80\xA6") :
				count == 1 ? "1 series" : std::to_string(count) + " series";
			return m;
		}
		if (i <= nets)
		{
			// a saved network connection: its name and what kind it is (SMB · host)
			std::vector<NetServer> ns = mangaNetFoldersServers();
			if (i - 1 < (int)ns.size())
			{
				m.glyph = &G_NETWORK;
				m.name = serverName(ns[i - 1]);
				m.note = std::string(netTypeLabel(ns[i - 1].type)) + " \xC2\xB7 " + ns[i - 1].host;
			}
			return m;
		}
		if (i <= nets + servers)
		{
			// a saved server: its name and kind; its Edit is X, a key on the focused row
			std::vector<MangaServer> sv;
			if (mangaNet && mangaNet->started()) sv = mangaNet->servers().list();
			if (i - nets - 1 < (int)sv.size())
			{
				const MangaServer& s = sv[i - nets - 1];
				m.glyph = &G_LAPTOP;
				m.name = s.name;
				m.note = s.kind;
				if (!m.note.empty()) m.note[0] = (char)std::toupper((unsigned char)m.note[0]);
				m.keyLabel = "Edit";
				m.keyCap = "X";
			}
			return m;
		}
		if (i == manageAt)
		{
			m.glyph = &G_GEAR;
			m.name = "Sources\xE2\x80\xA6";
			m.note = "install, update, repositories";
			return m;
		}
		if (i == manageAt + 1)
		{
			m.glyph = &G_PLUS;
			m.name = "Add a server\xE2\x80\xA6";
			m.note = "Komga, Kavita, OPDS";
			return m;
		}
		if (mangaSrcRows.empty() && i == srcStart)
		{
			// no source installed: where they come from (A opens it)
			m.glyph = &G_PLUS;
			m.name = "Add repository";
			m.note = "sources come from repositories you add";
			return m;
		}
		if (const MangaSrcRow* r = mangaBrowseSourceAt(i - srcStart, p))
		{
			m.glyph = &G_WEB;
			m.name = r->name;
			m.note = r->lang;
			for (char& c : m.note) c = (char)std::toupper((unsigned char)c);
			if (r->obsolete) { m.pill = "Obsolete"; m.pillBad = true; m.obsolete = true; }
		}
		return m;
	}

	void Ui::Manga::drawMangaBrowseRows(const Player& p)
	{
		// a network folder's own page (its folders, or a folder read as a series) draws in its place
		if (mangaNetView != MNF_OFF)
		{
			drawMangaNetFoldersKit(p);
			return;
		}
		mangaBrowseRepos(p);
		int n = mangaBrowseCount(p);
		if (mbrowseCursor >= n) mbrowseCursor = n - 1;
		if (mbrowseCursor < 0) mbrowseCursor = 0;
		tabCrumb = "Browse";
		mangaRows(mlBrowse, p, n, mbrowseScroll, mbrowseCursor, [this, &p](int i) { return mangaBrowseRowAt(p, i); });
		int nets = mangaNetFoldersCount();
		bool onServer = mbrowseCursor > nets && mbrowseCursor <= nets + mangaBrowseServerCount();
		if (onServer) ui.drawHints({{"A", "Open"}, {"X", "Edit"}, {"Y", "Add server"}, {"B", "Back"}});
		else ui.drawHints({{"A", "Open"}, {"X", "Sources"}, {"Y", "Add server"}, {"B", "Back"}});
	}

	// ---- a source's page: Popular · Latest · Search, a cover grid, Search ----

	std::string Ui::Manga::mangaSrcFiltersNow() const
	{
		return MangaJava::isSourceId(mangaSrcId) ? mangaJavaFiltersJson() : mangaSourceFiltersJson();
	}

	void Ui::Manga::mangaSrcTabSave()
	{
		if (mangaSrcItems.empty() || !mangaSrcError.empty()) return;
		if (mangaSrcTabCache.size() > 12) mangaSrcTabCache.clear();
		// a page still coming is kept as not come: back on the tab, it is asked for again (mangaSourceMaybeMore)
		int page = mangaSrcLoading ? std::max(1, mangaSrcPage - 1) : mangaSrcPage;
		bool more = mangaSrcLoading ? true : mangaSrcMore;
		mangaSrcTabCache[mangaSrcId + "|" + std::to_string(mangaSrcTab)] =
			{mangaSrcItems, page, mangaSrcCursor, mangaSrcScroll, more, mangaSrcQuery, mangaSrcFiltersNow()};
	}

	bool Ui::Manga::mangaSrcTabRestore(int tab)
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

	void Ui::Manga::mangaOpenSource(const MangaSrcRow& row, Player& p)
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

	void Ui::Manga::mangaSourceBack(Player& p)
	{
		(void)p;
		mangaSrcTabSave();	// (kept: opened again, the tab is as it was)
		mangaSourceCancel();
		mangaView = 0;
		mangaSrcError.clear();
		mangaSrcItems.clear();
		mangaSrcCursor = mangaSrcScroll = 0;
	}

	void Ui::Manga::mangaSourceTabSet(int tab, Player& p)
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
			ui.openKeyboard(MANGA_SEARCH_FIELD, mangaSrcQuery);
			return;
		}
		mangaSourceLoad(1);
	}

	void Ui::Manga::mangaSourceTabStep(int dir, Player& p)
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

	void Ui::Manga::mangaSourceLoad(int page)
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

	void Ui::Manga::mangaSourceSearch(const std::string& query)
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

	void Ui::Manga::mangaSourceRetry(Player& p)
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

	bool Ui::Manga::mangaSourceKey(Button b, Player& p)
	{
		// the filter sheet takes every key while it shows
		if (mangaFilterShown()) return mangaFilterKey(b, p);
		// the spec's keys (screens.js manga-source): Y is Filters (when the source has them), X is More; Search is the
		// third tab, asked for by A on it or by More
		if (b == Button::Y && mangaFilterAvailable())
		{
			mangaFilterShow(p);
			return false;
		}
		if (b == Button::X) { mangaSourceMore(p); return false; }
		if (b == Button::B) { mangaSourceBack(p); return false; }
		// its tabs step with L / R (L2 / R2 are the shell's sections)
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
			else if (mangaSrcTab == SRC_SEARCH) ui.openKeyboard(MANGA_SEARCH_FIELD, mangaSrcQuery);
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

	void Ui::Manga::mangaSourceMore(Player& p)
	{
		(void)p;
		ui.openMore([this](const Player&) {
			MoreSpec m;
			m.glyph = &G_BOOK;
			m.title = mangaSrcName.empty() ? std::string("Source") : mangaSrcName;
			m.rows.push_back({&G_SEARCH, "Search this source", "", "", [this](Player& p) {
				if (mangaSrcTab == SRC_SEARCH) ui.openKeyboard(MANGA_SEARCH_FIELD, mangaSrcQuery);
				else mangaSourceTabSet(SRC_SEARCH, p);
				return false;
			}});
			m.rows.push_back({&G_REFRESH, "Load this list again", "", "", [this](Player& p) {
				mangaSourceRetry(p);
				return false;
			}});
			return m;
		});
	}

	void Ui::Manga::mangaSourceMaybeMore()
	{
		int n = (int)mangaSrcItems.size(), rows = (n + MG_COLS - 1) / MG_COLS;
		int lastRow = std::max(mangaSrcCursor / MG_COLS, mangaSrcScroll + SRC_ROWS_SHOWN - 1);
		// the first three pages at once (a source's page is often only a row or two), then two rows before the end;
		// one at a time. Not after a failure (A or a tap tries again)
		if (!mangaSrcMore || mangaSrcLoading || !mangaSrcError.empty() || rows == 0) return;
		if (mangaSrcPage < 3 || lastRow >= rows - 2) mangaSourceLoad(mangaSrcPage + 1);
	}

	bool Ui::Manga::mangaSourcePointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (mangaFilterShown()) return mangaFilterPointer(pt, x, y, p);
		if (pt.kind != Pointer::Down) return false;
		// the tabs (the kit's, as drawMangaSource laid them out): a tap on one steps straight to it (Search asks for
		// the query; the Search tab again: the keyboard, the list it ran kept under it)
		lay::TabPage tp = mangaSrcTabPage(p);
		std::vector<int> slots = mangaSrcSlots();
		int t = lay::tabAt(tp.strip, x, y);
		if (t >= 0 && t < (int)slots.size())
		{
			if (slots[t] != mangaSrcTab) mangaSourceTabSet(slots[t], p);
			else if (slots[t] == SRC_SEARCH) ui.openKeyboard(MANGA_SEARCH_FIELD, mangaSrcQuery);
			return false;
		}
		// an error in the empty grid: a tap is Try again (A)
		if (!mangaSrcError.empty() && mangaSrcItems.empty() && tp.content.hit(x, y)) { mangaSourceRetry(p); return false; }
		// the grid: a drag scrolls it by rows (the next page asked near its end), a tap opens the cover's series
		mangaGridPointer(mlSrcGrid, tp.content, x, y, [this] { return (int)mangaSrcItems.size(); }, &mangaSrcScroll, &mangaSrcCursor,
			[this, &p](int i) {
				if (i < 0 || i >= (int)mangaSrcItems.size()) return;
				mangaSrcCursor = i;
				mangaSourceOpenSeriesAt(i, p);
			}, [this] { mangaSourceMaybeMore(); });
		return false;
	}

	void Ui::Manga::drawMangaSource(const Player& p)
	{
		// a source's catalogue (screens.js manga-source): its name the header's title, where it is in the crumb, the
		// tabs (L / R), and a cover grid that fills as you move
		ui.drawHeader(mangaSrcName.empty() ? "Source" : mangaSrcName, p);
		static const char* NAMES[3] = {"Popular", "Latest", "Search"};
		std::vector<int> slots = mangaSrcSlots();
		lay::TabPage tp = mangaSrcTabPage(p);
		std::vector<std::string> labels;
		int on = -1;
		for (size_t i = 0; i < slots.size(); i++)
		{
			labels.push_back(NAMES[slots[i]]);
			if (slots[i] == mangaSrcTab) on = (int)i;
		}
		ui.kit->tabs(tp.strip.area, labels, on, "L/R", BG);
		int n = (int)mangaSrcItems.size();
		if (n && mangaSrcCursor >= n) mangaSrcCursor = n - 1;
		// a smooth grid (a row of covers a row of the list), the cursor's row kept in sight
		// covers are fetched only while their card is on screen and cancelled when it scrolls away
		mangaCoverOnScreen.clear();
		drawMangaGrid(mlSrcGrid, tp.content, n, mangaSrcScroll, mangaSrcCursor, [this](int i) {
			GridCell c;
			c.title = mangaSrcItems[i].title;
			c.coverKey = coverReady("", mangaSrcItems[i].cover, &mangaSrcCoverHeaders);
			c.picture = [this, title = c.title, cover = mangaSrcItems[i].cover](Rect r) {
				drawMangaCover(r, title, false, 4, "", cover, &mangaSrcCoverHeaders);
			};
			return c;
		});
		mangaCoverCancelOffScreen();
		// the line under the title: where it is, then what it is doing (an error, loading, the query Search ran)
		std::string javaStatus = mangaJavaStatusLine();
		tabCrumb = "Manga \xE2\x80\xBA Browse";
		for (const MangaSrcRow& r : mangaSrcRows)
			if (r.id == mangaSrcId && !r.lang.empty())
			{
				std::string lang = r.lang;
				for (char& c : lang) c = (char)std::toupper((unsigned char)c);
				tabCrumb += " \xE2\x80\xBA " + lang;
			}
		if (!javaStatus.empty() && mangaNet && mangaNet->javaExt().state() == JavaExtHost::State::Failed) tabCrumb = javaStatus;
		else if (mangaSrcLoading && n > 0) tabCrumb = mangaSrcPage > 1 ? "Loading more\xE2\x80\xA6" : "Loading\xE2\x80\xA6";
		else if (mangaSrcTab == SRC_SEARCH && !mangaSrcQuery.empty()) tabCrumb = "Search: " + mangaSrcQuery;
		// the grid's first page on its way: the loading mark in the middle; an empty one says why
		if (n == 0 && mangaSrcError.empty() && mangaSrcLoading)
			ui.drawLoader(tp.content.x + tp.content.w / 2, tp.content.y + tp.content.h / 2, "Loading\xE2\x80\xA6");
		else if (n == 0 && !mangaSrcError.empty())
			drawMangaTabEmpty(p, G_WEB, browserCheckText(mangaSrcError) ? "This source is behind a browser check" : "Couldn\xE2\x80\x99t load it",
				browserCheckText(mangaSrcError) ? "A: try again" : mangaSrcError + ". A: try again");
		else if (n == 0 && javaStatus.empty())
			drawMangaTabEmpty(p, G_SEARCH, mangaSrcTab == SRC_SEARCH ? (mangaSrcQuery.empty() ? "Search this source" : "No results")
				: "Nothing here yet", mangaSrcTab == SRC_SEARCH && mangaSrcQuery.empty() ? "A asks for the words." : "Try another tab.");
		if (!mangaSrcError.empty())
		{
			if (mangaFilterAvailable()) ui.drawHints({{"A", "Try again"}, {"Y", "Filters"}, {"X", "More"}, {"L/R", "Tab"}, {"B", "Back"}});
			else ui.drawHints({{"A", "Try again"}, {"X", "More"}, {"L/R", "Tab"}, {"B", "Back"}});
		}
		else if (mangaFilterAvailable())
			ui.drawHints({{"A", "Open"}, {"Y", "Filters"}, {"X", "More"}, {"L/R", "Tab"}, {"B", "Back"}});
		else ui.drawHints({{"A", "Open"}, {"X", "More"}, {"L/R", "Tab"}, {"B", "Back"}});
		if (mangaFilterShown()) later([this, &p] { drawMangaFilters(p); });
	}

	void Ui::Manga::mangaSourceOpenSeriesAt(int i, Player& p)
	{
		if (i < 0 || i >= (int)mangaSrcItems.size()) return;
		const MangaItem& it = mangaSrcItems[i];
		mangaOpenSourceSeries(sourceItemId(mangaSrcId, it.key), 3, it.json, it.cover, it.title, p);
	}

	// ---- the worker's answers ----

	void Ui::Manga::mangaErrCheck(const std::string& error, bool anyError, int asker)
	{
		// each error once while it stands (the checks run every pass); once it's gone, the same one coming back (the
		// source opened again) is said again: kept for good, a reopened source's Cloudflare error never showed
		std::string& shown = mangaErrFor[asker == 1 ? 1 : 0];
		if (error.empty())
		{
			shown.clear();
			return;
		}
		if (error == shown) return;
		shown = error;
		bool ddos = false;
		std::string low;
		for (char c : error) low += (char)std::tolower((unsigned char)c);
		// an extension that needs the engine's built-in web browser (its Chromium, CEF, for a site's own scripts):
		// the handheld can't run one (no X11, and 1 GB), so it fails loading libjawt. Said plainly, not as Java's text
		if (low.find("cefmanager") != std::string::npos || low.find("libjawt") != std::string::npos ||
			low.find("kcef") != std::string::npos)
		{
			mangaErrText = "This source needs a web browser to open its pages, and the handheld can\xE2\x80\x99t run one. "
				"Try another source.";
			ui.openMangaErr();
			fprintf(stderr, "manga: a source that needs a browser engine (%s)\n", error.substr(0, error.find_first_of("\r\n")).c_str());
			return;
		}
		if (!browserCheckText(error, &ddos))
		{
			if (!anyError) return;
			// any other error of a source's: said in the dialog, never as a line beside the tabs
			// (the first line only, without the engine's "Exception while fetching data (…) :" before it)
			std::string e = error.substr(0, error.find_first_of("\r\n"));
			size_t c = e.find(") : ");
			if (e.compare(0, 9, "Exception") == 0 && c != std::string::npos) e = e.substr(c + 4);
			mangaErrText = "The source didn\xE2\x80\x99t answer: " + e;
			ui.openMangaErr();
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
		ui.openMangaErr();
		fprintf(stderr, "manga: a browser check (%s)\n", error.c_str());
	}

	void Ui::Manga::drawMangaErr()
	{
		std::vector<std::string> lines = ui.kit->sheetText(mangaErrText);
		Kit::AlertSpec a;
		a.title = "Can\xE2\x80\x99t open this";
		a.text = mangaErrText;
		ui.kit->alertSheet(lay::alertSheet((int)lines.size()), a, lines);
	}

	void Ui::Manga::mangaBrowsePoll(const Player& p)
	{
		// a source's error that is a browser check: the dialog (once per error). On a grid that has covers it is
		// no error of the grid's: A opens the series picked, not Try again
		mangaErrCheck(mangaSrcError, true);
		if (ui.page == Page::Manga && mangaView == 1 && !mangaSrcItems.empty() && !mangaSrcError.empty())
		{
			mangaSrcError.clear();
			mangaSrcFailed = FAIL_NONE;
			// and no more pages asked by themselves: the next would fail the same way, again and again (with its
			// dialog each time now that a cleared error can be said again). Reopening the source asks afresh
			mangaSrcMore = false;
		}
		if (!mangaNet || !mangaNet->started()) return;
		// a source's grid fills itself: its first pages, and the next near its end (the answer of the last landed)
		if (ui.page == Page::Manga && mangaView == 1) mangaSourceMaybeMore();
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

	void Ui::Manga::mangaSourceChapterOpen(const std::vector<MangaChapter>& chapters, int i, int page0,
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
		if (ui.page == Page::MangaRead)
		{
			mangaRecordProgress();
			mangaSave();
			mangaSrcPendFull = ui.pageReader->readerStatus.fullscreen;
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Close, 0, (int64_t)SDL_GetTicks()});
			ui.pageReader->mpFree();
			ui.pageReader->mfFree();
			ui.pageReader->mangaZoomAsked = 100;
			mangaProgPage = mangaProgPages = -1;
			ui.page = Page::MangaSeries;	// (so the block below takes it as an open from the series page)
		}
		if (ui.page != Page::MangaRead)
		{
			mangaReadWaiting = true;
			ui.pageReader->mangaReadSeriesId = seriesId;
			ui.pageReader->mangaReadChapterTitle = chapterName(chapters[i]);
			ui.pageReader->mangaPages = false;
			mangaReadLive = false;
			ui.page = Page::MangaRead;
		}
		mangaSourcePagesAsk(p);
	}

	void Ui::Manga::mangaReadWaitCancel()
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
		ui.page = Page::MangaSeries;
	}

	void Ui::Manga::drawMangaReadWaiting()
	{
		// the reader's header (back, the chapter, the series under it) over its surface, which says what it waits for;
		// the top panel says it large (waitingPane)
		lay::ReaderHeader h = lay::readerHeader({0, 0, W, lay::SHELL_HEAD}, false);
		Kit::Header hd;
		hd.title = ui.pageReader->mangaReadChapterTitle;
		hd.crumb = mangaTitle(ui.pageReader->mangaReadSeriesId);
		hd.titleFont = F_SMALL;
		hd.inlineTitle = true;
		ui.kit->header(h.strip, hd);
		Rect area = mangaAreaRect();
		bool failed = !mangaSrcLoading && !mangaSrcError.empty();
		ui.kit->readerSurface(area, true, "", "", failed);
		int mid = area.y + area.h / 2;
		if (failed)
		{
			ui.drawTextMid("The chapter didn\xE2\x80\x99t open", F_P20, DANGER, W / 2, mid - 14, 560, 2);
			ui.drawTextMid(mangaSrcError, F_I14, TEXT, W / 2, mid + 18, 560, 2);
			ui.kit->hints({{"A", "Try again"}, {"B", "Back"}});
		}
		else
		{
			ui.drawLoader(W / 2, mid - 10, "Getting the chapter\xE2\x80\xA6");
			ui.drawTextMid("Asking the source for its pages", F_I12, DIM, W / 2, mid + 52, 560, 2);
			ui.kit->hints({{"B", "Back"}});
		}
	}

	bool Ui::Manga::waitingPane(PaneSpec& out) const
	{
		// the top panel while a source's chapter is asked for (docs/redesign: manga-end, "Waiting for a chapter"): the
		// State card with the spinner, or the failure with its reason and A; not while a chapter still reads up there
		if (mangaOpenWaiting())
		{
			// the series page's waiting state (docs/redesign manga-series, "Waiting for the engine"): the same State
			// card, with the engine's seconds over a bar
			MangaWaitView v = mangaOpenWaitView();
			out = PaneSpec{};
			out.state = true;
			out.spinner = (int)(SDL_GetTicks() / 100 % 8);
			out.title = v.title;
			out.text = mangaTitle(mangaSeriesId);
			if (!mangaResumePending && !mangaChoosePendName.empty())
				out.text += (out.text.empty() ? "" : " \xC2\xB7 ") + mangaChoosePendName;
			out.share = (float)v.share;
			out.timeLine = v.line;
			out.keys = {{"B", "Cancel"}};
			out.foot = "Opens by itself when it\xE2\x80\x99s ready";
			if (!mangaOpenWaitFail.empty())
			{
				// ended: the reason, A to try again (the same look as a chapter that didn't open)
				out.spinner = -1;
				out.danger = true;
				out.iconRows = G_NETWORK_ROWS;
				out.iconH = G_NETWORK.h;
				out.text = v.line + " \xC2\xB7 " + out.text;
				out.timeLine.clear();
				out.keys = {{"A", "Try again"}, {"B", "Back"}};
				out.foot.clear();
			}
			out.key = "mangaenginewait|" + out.title + "|" + out.text + "|" + out.timeLine + "|" + std::to_string(out.spinner);
			return true;
		}
		if (!mangaReadWaiting || ui.page != Page::MangaRead) return false;
		bool failed = !mangaSrcLoading && !mangaSrcError.empty();
		out = PaneSpec{};
		out.state = true;
		std::string what = mangaTitle(ui.pageReader->mangaReadSeriesId);
		if (!ui.pageReader->mangaReadChapterTitle.empty())
			what += (what.empty() ? "" : " \xC2\xB7 ") + ui.pageReader->mangaReadChapterTitle;
		if (failed)
		{
			out.danger = true;
			out.iconRows = G_NETWORK_ROWS;
			out.iconH = G_NETWORK.h;
			out.title = "The chapter didn\xE2\x80\x99t open";
			out.text = mangaSrcError;
			out.keys = {{"A", "Try again"}};
		}
		else
		{
			int step = (int)(SDL_GetTicks() / 100 % 8);
			out.spinner = step;
			out.title = "Getting the chapter\xE2\x80\xA6";
			out.text = what;
			out.keys = {{"B", "Back"}};
		}
		out.key = "mangawait|" + out.title + "|" + out.text + "|" + std::to_string(out.spinner);
		return true;
	}

	void Ui::Manga::mangaSourcePagesAsk(Player& p)
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

	void Ui::Manga::mangaSourceImageAsk()
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

	void Ui::Manga::mangaSourceCancel()
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

	void Ui::Manga::mangaSourceChapterReady(const Player& p)
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
		if (ui.page == Page::MangaRead)
		{
			mangaRecordProgress();
			mangaSave();
		}
		mangaSeriesId = mangaSrcPendSeriesId;
		mangaReadSrcChapters = mangaSrcPendChapters;
		ui.pageReader->mangaReadSeriesId = mangaSrcPendSeriesId;
		ui.pageReader->mangaReadChapterId = chapterId;
		ui.pageReader->mangaReadChapterTitle = chapterName(c);
		// path: the chapter's own id, the cache's key (never a folder to open); the pages are the URLs, in order
		ui.pageReader->mangaRead = {mangaSrcPendSeriesId, mangaSrcPendSeriesTitle, chapterId, ui.pageReader->mangaReadChapterTitle, chapterId, false,
			mangaSrcPendPage, mangaSrcPendMode, mangaSrcPendFit};
		ui.pageReader->mangaRead.pageUrls = mangaSrcUrls;
		ui.pageReader->mangaRead.pageHeaders = mangaSrcHeaders;
		// in History at once: the chapter as opened (its length is the list's), not only after a page turns
		if (mangaStore)
		{
			const ChapterProgress* old = mangaStore->progress(chapterId);
			ChapterProgress cp = old ? *old : ChapterProgress{};
			cp.seriesId = mangaSrcPendSeriesId;
			cp.chapterId = chapterId;
			cp.chapterTitle = ui.pageReader->mangaReadChapterTitle;
			if (!old)
			{
				cp.page = mangaSrcPendPage;
				cp.pages = (int)mangaSrcUrls.size();
			}
			cp.hidden = !mangaHistOn || ui.incognito;
			cp.at = (int64_t)time(nullptr);
			mangaStore->setProgress(cp, !ui.incognito);
			mangaSave();
		}
		// the status still holds the chapter before: only a change records
		mangaProgPage = ui.pageReader->readerStatus.page;
		mangaProgPages = ui.pageReader->readerStatus.pages;
		mangaReadLive = false;
		ui.pageReader->mangaSliderPage = -1;
		mangaCtrlUntil = 0;
		ui.pageReader->mangaZoomAsked = 100;
		ui.page = Page::MangaRead;
		if (mangaSrcPendFull)
		{
			// Open fullscreen: the command SELECT sends right after the chapter (mangaOpenChapter's), with the
			// hinge it was asked with; the toast is the first fullscreen's, and the setting is written on the next
			// key (mangaKey), where a Player can be changed
			Uint32 now = SDL_GetTicks();
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, 1.0, (int64_t)now, (double)mangaSrcPendHinge});
			ui.pageReader->mfTagUntil = now + 2500;
			if (!p.settings.mangaFullSeen) mangaToastUntil = now + MG_TOAST_MS;
			mangaFullSeenPending = !p.settings.mangaFullSeen;
		}
	}

	void Ui::Manga::mangaSourceChapterStep(int dir, Player& p)
	{
		int i = mangaReadChapterIndex();
		if (i < 0) return;
		int j = i + dir;
		if (j < 0 || j >= (int)mangaReadSrcChapters.size()) return;
		mangaSourceChapterOpen(mangaReadSrcChapters, j, 0, ui.pageReader->mangaReadSeriesId, mangaTitle(ui.pageReader->mangaReadSeriesId), p);
	}

	// ---- a source series: its details, its chapters, In library ----

	bool Ui::Manga::mangaSourceSeriesOn() const
	{
		return ui.page == Page::MangaSeries && isSourceId(mangaSeriesId);
	}

	void Ui::Manga::mangaOpenSourceSeries(const std::string& id, int from, const std::string& json, const std::string& cover,
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
		ui.page = Page::MangaSeries;
		// the store is the only place a source series' title and cover live: History and the Library read them from
		// there (inLibrary false until Y says so, so it doesn't show in the Library before that)
		if (mangaStore)
		{
			MangaSeries m;
			m.id = id;
			m.title = mangaSrcTitle;
			m.source = full;
			m.cover = mangaCoverStored(mangaSrcCover);
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
		// of the source; only Refresh (L) does that (a Java source's host starts then, not for a kept list)
		mangaSrcFetchedAt = 0;
		mangaSrcRefreshing = mangaSrcHadList = false;
		mangaSrcFetchDetailsOk = mangaSrcFetchChaptersOk = false;
		// an extension's series from the card's copy is checked against the engine's database before its chapters
		// open, but not now: that starts the engine (~25 s of the cores) and can redo the list, and opening the page
		// (from History, say) read as a refresh nobody asked for. Resume or a chapter starts it (mangaSourceEngineReady)
		mangaJavaCheckPending = false;
		if (!mangaSourceSeriesCacheLoad()) mangaSourceSeriesLoad(p);
		else if (MangaJava::isSourceId(mangaSrcId)) mangaJavaCheckPending = true;
	}

	void Ui::Manga::mangaSourceSeriesLoad(Player& p, bool refresh)
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

	void Ui::Manga::mangaSourceSeriesRefresh(Player& p)
	{
		// one check at a time; the page's line says "Checking for new chapters…" while it runs
		if (mangaSrcRefreshing) return;
		mangaJavaCheckPending = false;	// (a refresh asks the engine for the list itself)
		mangaSourceSeriesLoad(p, true);
	}

	void Ui::Manga::mangaSourceSeriesFail(const std::string& why)
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

	void Ui::Manga::mangaSourceSeriesFetched()
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

	bool Ui::Manga::mangaSourceSeriesCacheLoad()
	{
		MangaSeriesCache c;
		// a copy with no chapters is no copy: a site's browser check (Cloudflare) can come back as a good answer with an
		// empty list, and kept, it showed the empty list at every open and the source was never asked again (nor its
		// error shown)
		if (!mangaSeriesCacheRead(ui.stateDirPath, mangaSeriesId, c) || !c.chaptersOn || c.chapters.empty()) return false;
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

	void Ui::Manga::mangaSourceSeriesCacheSave()
	{
		// (an empty chapter list isn't kept: see mangaSourceSeriesCacheLoad)
		if (ui.stateDirPath.empty() || mangaSeriesId.empty() || mangaSrcChapters.empty()) return;
		MangaSeriesCache c;
		c.details = mangaSrcInfo;
		c.chapters = mangaSrcChapters;
		c.fetchedAt = mangaSrcFetchedAt;
		c.detailsOn = mangaSrcInfoOn;
		c.chaptersOn = mangaSrcChaptersOn;
		mangaSeriesCacheWrite(ui.stateDirPath, mangaSeriesId, c);
	}

	void Ui::Manga::mangaSourceSeriesChoose(int i, Player& p)
	{
		if (i < 0 || i >= (int)mangaSrcChapters.size()) return;
		// (an extension's chapter, as its Resume, waits for the manga engine in the page's waiting state and opens
		// when it is ready; a downloaded one reads from the card at once)
		if (mangaOpenWay(mangaSourceEngineReady(), mangaSourceChapterOnCard(i)) == MangaOpenWay::Wait)
		{
			mangaResumePending = false;
			mangaChoosePendKey = mangaSrcChapters[i].key;
			mangaChoosePendName = chapterName(mangaSrcChapters[i]);
			mangaOpenWaitAt = SDL_GetTicks();
			mangaSourceResumeStep(p);
			return;
		}
		std::string chapterId = sourceItemId(mangaSrcId, mangaSrcChapters[i].key);
		// its saved page, unless it was read to its end (then the start again), as the card's chapters do
		int page0 = 0;
		if (mangaStore)
			if (const ChapterProgress* pr = mangaStore->progress(chapterId))
				if (!pr->read) page0 = pr->page;
		mangaSourceChapterOpen(mangaSrcChapters, i, page0, mangaSeriesId, mangaSrcInfo.title.empty() ? mangaSrcTitle : mangaSrcInfo.title, p);
	}

	bool Ui::Manga::mangaSourceEngineReady() const
	{
		// a series from an extension reads through the manga engine: Resume waits until it answers
		std::string full, key;
		splitSourceId(mangaSeriesId, full, key);
		if (!MangaJava::isSourceId(full)) return true;
		// and the series checked against the engine's database (its saved ids may be an earlier install's)
		return mangaNet && mangaNet->started() && mangaNet->javaExt().state() == JavaExtHost::State::Running &&
			!mangaJavaCheckPending && !mangaJavaChecking && !mangaJavaRelinkSerial;
	}

	void Ui::Manga::mangaSourceEngineWant()
	{
		// the check put off at the page's open, started now that a chapter is wanted (the engine with it)
		if (!mangaJavaCheckPending) return;
		mangaJavaCheckPending = false;
		mangaJavaCheckStart();
	}

	bool Ui::Manga::mangaSourceChapterOnCard(int i) const
	{
		return i >= 0 && i < (int)mangaSrcChapters.size() &&
			mangaDlMarkFor(mangaSrcId, mangaSeriesId, mangaSrcChapters[i]).kind == MangaDlMark::OnCard;
	}

	MangaResumePoint Ui::Manga::mangaSourceResumePoint() const
	{
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
		return mangaResume(marks);
	}

	bool Ui::Manga::mangaOpenWaiting() const
	{
		return ui.page == Page::MangaSeries && (mangaResumePending || !mangaChoosePendKey.empty());
	}

	void Ui::Manga::mangaOpenWaitDrop()
	{
		mangaResumePending = mangaResumeLogged = false;
		mangaChoosePendKey.clear();
		mangaChoosePendName.clear();
		mangaOpenWaitBusyAt = 0;
		mangaOpenWaitFail.clear();
	}

	void Ui::Manga::mangaOpenWaitEnd(const std::string& why)
	{
		// the page keeps the waiting state, now saying why it stopped, with A Retry and B Back
		fprintf(stderr, "manga: wait ends: %s\n", why.c_str());
		mangaOpenWaitFail = why;
	}

	void Ui::Manga::mangaOpenWaitRetry(Player& p)
	{
		// a series without a list asks for it again; one with a list is checked again (a check stuck on its way dropped)
		fprintf(stderr, "manga: the wait is tried again\n");
		mangaOpenWaitFail.clear();
		mangaOpenWaitBusyAt = 0;
		mangaOpenWaitAt = SDL_GetTicks();
		mangaResumeLogged = false;
		bool java = MangaJava::isSourceId(mangaSrcId);
		if (java && mangaNet && mangaNet->started())
		{
			if (mangaJavaCheckSerial) mangaNet->javaExt().cancelGraphQl(mangaJavaCheckSerial);
			mangaJavaNeedHost();	// (a failed engine starts again)
		}
		mangaJavaCheckSerial = 0;
		mangaJavaChecking = false;
		if (mangaSrcChapters.empty()) mangaSourceSeriesLoad(p, mangaSrcChaptersOn);
		else if (java) mangaJavaCheckPending = true;
		mangaSourceResumeStep(p);
	}

	MangaWaitView Ui::Manga::mangaOpenWaitView() const
	{
		bool java = MangaJava::isSourceId(mangaSrcId);
		auto st = mangaNet && mangaNet->started() ? mangaNet->javaExt().state() : JavaExtHost::State::NotInstalled;
		Uint32 now = SDL_GetTicks();
		// the seconds since the engine began to start, as the Ui saw it (extNoticeText); else since the press
		Uint32 since = extNoticeState == (int)JavaExtHost::State::Starting && extNoticeAt ? extNoticeAt : mangaOpenWaitAt;
		if (!mangaOpenWaitFail.empty())
		{
			MangaWaitView f;
			f.title = mangaResumePending ? "Couldn\xE2\x80\x99t resume" : "Couldn\xE2\x80\x99t open the chapter";
			f.line = mangaOpenWaitFail;
			return f;
		}
		return mangaWaitView(mangaWaitStage(!java || st == JavaExtHost::State::Running, mangaSrcLoading || mangaSrcRefreshing),
			(int64_t)(now - since));
	}

	void Ui::Manga::mangaSourceSeriesResume(Player& p)
	{
		// a downloaded chapter never waits for the engine: the card's copy of the list says which chapter, its file
		// opens now (the series' check stays put off, as when a downloaded chapter is picked)
		if (!mangaSourceEngineReady())
		{
			MangaResumePoint r = mangaSourceResumePoint();
			if (r.chapter >= 0 && mangaOpenWay(false, mangaSourceChapterOnCard(r.chapter)) == MangaOpenWay::FromCard)
			{
				fprintf(stderr, "manga: resumes chapter %s page %d from the card (the engine not waited for)\n",
					mangaSrcChapters[r.chapter].key.c_str(), r.page);
				mangaSourceChapterOpen(mangaSrcChapters, r.chapter, r.page, mangaSeriesId,
					mangaSrcInfo.title.empty() ? mangaSrcTitle : mangaSrcInfo.title, p);
				return;
			}
		}
		// one press resumes: not ready yet, it is remembered and goes on when the series is (mangaNotesTick), the page
		// in its waiting state meanwhile; B drops it
		mangaChoosePendKey.clear();
		mangaChoosePendName.clear();
		mangaResumePending = true;
		mangaOpenWaitAt = SDL_GetTicks();
		mangaSourceResumeStep(p);
	}

	void Ui::Manga::mangaSourceResumeStep(Player& p)
	{
		// a Resume or a picked chapter that waits: looked at every pass until the series is ready
		if (!mangaResumePending && mangaChoosePendKey.empty()) return;
		// (the page left some other way, a page's intent say: nothing opens by itself later)
		if (ui.page != Page::MangaSeries) { mangaOpenWaitDrop(); return; }
		if (!mangaOpenWaitFail.empty()) return;	// (ended: A tries again, B goes back)
		bool java = MangaJava::isSourceId(mangaSrcId);
		auto st = mangaNet && mangaNet->started() ? mangaNet->javaExt().state() : JavaExtHost::State::NotInstalled;
		const char* what = mangaResumePending ? "resume" : "open the chapter";
		// (an engine that was removed, or never installed here, would be waited for for ever)
		if (java && !mangaJavaInstalled())
		{
			mangaOpenWaitDrop();
			mangaNotice(std::string("Couldn\xE2\x80\x99t ") + what + ": the manga engine isn\xE2\x80\x99t installed", 3000);
			fprintf(stderr, "manga: %s failed (the engine isn't installed)\n", what);
			return;
		}
		Uint32 now = SDL_GetTicks();
		MangaWaitIn in;
		in.engineRunning = !java || st == JavaExtHost::State::Running;
		in.engineFailed = java && st == JavaExtHost::State::Failed;
		in.checkWanted = java && mangaJavaCheckPending;
		in.checkInFlight = java && (mangaJavaChecking || mangaJavaRelinkSerial);
		in.listLoading = mangaSrcLoading || mangaSrcRefreshing;
		in.listFailed = !mangaSrcError.empty() && !mangaSrcChaptersOn;
		in.haveChapters = !mangaSrcChapters.empty();
		if (in.engineRunning && !mangaOpenWaitBusyAt) mangaOpenWaitBusyAt = now;
		in.busyMs = mangaOpenWaitBusyAt ? (int64_t)(now - mangaOpenWaitBusyAt) : 0;
		switch (mangaWaitAct(in))
		{
		case MangaWaitAct::SendCheck:
			mangaSourceEngineWant();
			if (in.engineRunning) mangaOpenWaitBusyAt = now;
			[[fallthrough]];
		case MangaWaitAct::Wait:
			if (!mangaResumeLogged)
				fprintf(stderr, "manga: %s waits for the series (engine state %d)\n", what, java ? (int)st : -1);
			mangaResumeLogged = true;
			return;	// (the waiting state says why: drawMangaOpenWait, waitingPane)
		case MangaWaitAct::Fail:
			mangaOpenWaitEnd(in.engineFailed ? "The manga engine didn\xE2\x80\x99t start" : mangaSrcError);
			return;
		case MangaWaitAct::TimedOut:
			mangaOpenWaitEnd(in.checkInFlight ? "The manga engine didn\xE2\x80\x99t answer about this series"
				: "The chapter list didn\xE2\x80\x99t come");
			return;
		case MangaWaitAct::Nothing:
			fprintf(stderr, "manga: wait ends: no chapters\n");
			mangaOpenWaitDrop();
			return;
		case MangaWaitAct::Go: break;
		}
		bool resume = mangaResumePending;
		std::string key = mangaChoosePendKey;
		mangaOpenWaitDrop();
		if (!resume)
		{
			// the chapter picked, by its key: the check may have put the list in another order, or dropped it
			for (int i = 0; i < (int)mangaSrcChapters.size(); i++)
				if (mangaSrcChapters[i].key == key)
				{
					fprintf(stderr, "manga: opens chapter %s after the wait\n", key.c_str());
					mangaSourceSeriesChoose(i, p);
					return;
				}
			mangaNotice("That chapter isn\xE2\x80\x99t in the list any more", 3000);
			fprintf(stderr, "manga: the chapter waited for (%s) left the list\n", key.c_str());
			return;
		}
		MangaResumePoint r = mangaSourceResumePoint();
		if (r.chapter >= 0)
			fprintf(stderr, "manga: resumes chapter %s page %d\n", mangaSrcChapters[r.chapter].key.c_str(), r.page);
		if (r.chapter >= 0)
			mangaSourceChapterOpen(mangaSrcChapters, r.chapter, r.page, mangaSeriesId,
				mangaSrcInfo.title.empty() ? mangaSrcTitle : mangaSrcInfo.title, p);
	}

	void Ui::Manga::drawMangaOpenWait(const Player& p)
	{
		// the series page while a Resume or a chapter waits for the engine: what it waits for, the seconds against the
		// usual time, and that it goes on by itself; the top panel says it large (waitingPane)
		std::string title = mangaSrcInfoOn && !mangaSrcInfo.title.empty() ? mangaSrcInfo.title : mangaSrcTitle;
		ui.drawHeader(title.empty() ? "Series" : title, p);
		MangaWaitView v = mangaOpenWaitView();
		Rect area = mangaAreaRect();
		int mid = area.y + area.h / 2;
		if (!mangaOpenWaitFail.empty())
		{
			ui.drawTextMid(v.title, F_P20, TEXT, W / 2, mid - 20, 560, 2);
			ui.drawTextMid(v.line, F_I14, DIM, W / 2, mid + 14, 560, 2);
			ui.drawHints({{"A", "Retry"}, {"B", "Back"}});
			return;
		}
		ui.drawLoader(W / 2, mid - 48, "");
		ui.drawTextMid(v.title, F_P20, TEXT, W / 2, mid - 6, 560, 2);
		if (v.share >= 0)
		{
			Rect bar{W / 2 - 150, mid + 18, 300, 6};
			ui.fillRound(bar, SURFACE, 3);
			ui.fillRound({bar.x, bar.y, std::max(6, (int)std::lround(bar.w * v.share)), bar.h}, ACCENT, 3);
			ui.drawTextMid(v.line, F_I14, TEXT, W / 2, mid + 44, 560, 2);
		}
		std::string next = mangaResumePending ? std::string("Resume") : mangaChoosePendName;
		ui.drawTextMid((next.empty() ? std::string("It") : next) + " opens by itself when it\xE2\x80\x99s ready", F_I12, DIM, W / 2,
			mid + (v.share >= 0 ? 72 : 28), 560, 2);
		ui.drawHints({{"B", "Cancel"}});
	}

	std::string Ui::Manga::mangaSourceLabel(const std::string& seriesId) const
	{
		// where a series comes from, by name: an extension's or a Lua source's, a server's, or the card
		if (seriesId.compare(0, 6, "local:") == 0) return "On this handheld";
		if (isServerId(seriesId)) return "Server";
		if (!isSourceId(seriesId)) return "";
		std::string full, key;
		splitSourceId(seriesId, full, key);
		for (const MangaSrcRow& r : mangaSrcRows)
			if (r.id == full) return r.name;
		if (mangaNet && mangaNet->started())
		{
			if (const MangaJava::Source* s = mangaNet->javaSources().source(full)) return s->name.empty() ? s->title() : s->name;
			for (const auto& s : mangaNet->repos().installed())
				if (s.fullId == full) return s.name.empty() ? s.id : s.name;
		}
		return "";
	}

	std::string Ui::Manga::mangaSourceDownloadsId(const std::string& sourceSeriesId) const
	{
		// the card's folder of this series' downloads (its chapters' ComicInfo name it), as the Library knows it
		for (const LocalSeries& s : mangaLocal)
			for (const LocalChapter& c : s.chapters)
				if (c.ciSeriesKey == sourceSeriesId) return "local:" + s.path;
		return "";
	}

	bool Ui::Manga::mangaSourceSeriesInLib() const
	{
		// the series itself (its downloads folder is never in the Library by being on the card)
		if (!mangaStore) return false;
		const MangaSeries* ms = mangaStore->series(mangaSeriesId);
		return ms && ms->inLibrary;
	}

	void Ui::Manga::mangaSourceSeriesInLibrary(Player& p)
	{
		(void)p;
		if (!mangaStore) return;
		bool now = !mangaSourceSeriesInLib();
		if (mangaStore->series(mangaSeriesId))
		{
			mangaStore->setInLibrary(mangaSeriesId, now);
			mangaSave();
			return;
		}
		if (!now) { mangaSave(); return; }
		// the full source id is the series' source, its cover the URL the source gave (no cover is fetched by the
		// Library yet: a source series keeps its generated one)
		MangaSeries m;
		m.id = mangaSeriesId;
		m.title = mangaSrcTitle;
		m.source = mangaSrcId;
		m.cover = mangaCoverStored(mangaSrcCover);
		m.added = (int64_t)time(nullptr);
		m.inLibrary = true;
		mangaStore->putSeries(m);
		mangaSave();
	}

	bool Ui::Manga::mangaSourceSeriesKey(Button b, Player& p)
	{
		// the Download sheet takes the keys while it shows
		if (mdlSheet != MDLS_NONE) return mangaDlSheetKey(b, p);
		// the waiting state: B drops the Resume or the chapter that waits and the page is back; no other key acts
		if (mangaOpenWaiting())
		{
			if (b == Button::B)
			{
				mangaOpenWaitDrop();
				fprintf(stderr, "manga: the wait was cancelled\n");
			}
			else if (b == Button::A && !mangaOpenWaitFail.empty()) mangaOpenWaitRetry(p);
			return false;
		}
		if (b == Button::Start) { openMangaSettings(); return false; }
		// R gets the chapters, L checks the source again as a pull on the chapters does (free on this page: L / R are the shell's sections)
		if (b == Button::R) { mangaDlSheetOpen(p); return false; }
		if (b == Button::L) { mangaSourceSeriesRefresh(p); return false; }
		if (b == Button::B)
		{
			// back to where it was opened: the source's grid, the Library or History (a chapter still coming goes)
			mangaSourceCancel();
			ui.page = Page::Manga;
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
			mserBtn = std::clamp(mserBtn + (b == Button::Right ? 1 : -1), 0, MG_SRC_KEYS - 1);
			return false;
		}
		if (b == Button::A)
		{
			if (!mangaSrcError.empty() && !mangaSrcChaptersOn) mangaSourceRetry(p);
			else if (mserCursor < 0)
			{
				if (mserBtn == 1) mangaDlSheetOpen(p);
				else if (mserBtn == 2) mangaSrcInfoOpen();
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
		case Button::Left: cur = std::max(0, cur - mserSrcRows()); break;
		case Button::Right: cur = std::min(total - 1, cur + mserSrcRows()); break;
		default: break;
		}
		mserCursor = cur - 1;
		if (mserCursor >= 0)
		{
			if (mserCursor < mserScroll) mserScroll = mserCursor;
			if (mserCursor >= mserScroll + mserSrcRows()) mserScroll = mserCursor - mserSrcRows() + 1;
		}
		return false;
	}

	bool Ui::Manga::mangaSourceSeriesPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down) return false;
		if (mdlSheet != MDLS_NONE) return mangaDlSheetPointer(pt, x, y, p);
		if (mangaOpenWaiting())
		{
			// (the header's back cancels the wait, as B; nothing under the waiting state takes a tap)
			if (ui.buttonRect(BACK_ICON).hit(x, y)) mangaOpenWaitDrop();
			return false;
		}
		if (mangaSrcInfoRect().hit(x, y))
		{
			mangaSrcInfoOpen();
			return false;
		}
		if (mangaDlButtonRect().hit(x, y))
		{
			mangaDlSheetOpen(p);
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
		// the chapters (the kit's rows): a drag scrolls them, a tap reads the row
		mangaRowsPointer(mlChapters, p, x, y, [this] { return (int)mangaSrcChapters.size(); }, &mserScroll, &mserCursor,
			[](int) { return MangaRow(); }, [this, &p](int i, bool) {
				if (i < 0 || i >= (int)mangaSrcChapters.size()) return;
				mserCursor = i;
				mangaSourceSeriesChoose(i, p);
			});
		return false;
	}

	void Ui::Manga::drawMangaSourceSeries(const Player& p)
	{
		// the downloads' marks and the Download sheet need the card's own list (read at most every 1.5 s), and a
		// page list left waiting (the app reopened) goes on
		mangaDlEnsureCard();
		mangaDlAskPump();
		if (mangaOpenWaiting()) { drawMangaOpenWait(p); return; }
		std::string title = mangaSrcInfoOn && !mangaSrcInfo.title.empty() ? mangaSrcInfo.title : mangaSrcTitle;
		ui.drawHeader(title.empty() ? "Series" : title, p);
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
					mserScroll = std::max(0, std::min(i - 1, n - mserSrcRows()));
					break;
				}
			mserFocusChapter.clear();
		}
		// the source's details came: a title saved as the series' key (opened before its name was known) or a missing
		// cover takes theirs, so History and the Library say its name
		if (mangaStore && mangaSrcInfoOn && !mangaSrcInfo.title.empty())
			if (const MangaSeries* ms0 = mangaStore->series(mangaSeriesId))
			{
				// an extension series' cover keyed for another install (or saved before keys had one): this install's
				std::string cur = mangaCoverStored(mangaSrcInfo.cover);
				bool staleCover = MangaJava::isSourceId(mangaSrcId) && !mangaSrcInfo.cover.empty() && ms0->cover != cur;
				if (ms0->title.empty() || ms0->title == mangaSrcKey || (ms0->cover.empty() && !mangaSrcInfo.cover.empty()) || staleCover)
				{
					MangaSeries fixed = *ms0;
					if (fixed.title.empty() || fixed.title == mangaSrcKey) fixed.title = mangaSrcInfo.title;
					if (fixed.cover.empty() || staleCover) fixed.cover = cur;
					if (fixed.title != ms0->title || fixed.cover != ms0->cover)
					{
						mangaStore->putSeries(fixed);
						mangaSave();
					}
				}
			}
		std::string from = mangaSourceLabel(mangaSeriesId);
		// the header's line (screens.js seriesB: "Public domain comics · 24 chapters · 2 unread · Checked 7 h ago"): its
		// source, the counts, then a failure, a running job, its author and status, the cached list's age
		std::string crumb = (from.empty() ? "" : from + " \xC2\xB7 ") + std::to_string(n) + (n == 1 ? " chapter" : " chapters") +
			" \xC2\xB7 " + std::to_string(unread) + " unread";
		std::string line;
		bool bad = !mangaSrcError.empty();
		if (bad) line = browserCheckText(mangaSrcError) ? "Behind a browser check" : mangaSrcError;
		else if (mangaSrcLoading && n == 0 && !mangaSrcRefreshing) line = "Loading\xE2\x80\xA6";
		int64_t now = (int64_t)time(nullptr);
		bool stale = !bad && !mangaSrcRefreshing && mangaCheckedStale(mangaSrcFetchedAt, now);
		if (!bad && mangaSrcRefreshing) line = "Checking for new chapters\xE2\x80\xA6";
		else if (stale)
		{
			int64_t a = std::max<int64_t>(0, now - mangaSrcFetchedAt);
			line += (line.empty() ? "" : " \xC2\xB7 ") + std::string("Checked ") +
				(a < 86400 ? std::to_string(a / 3600) + " h" : std::to_string(a / 86400) + " d") + " ago \xC2\xB7 L checks again";
		}
		tabCrumb = crumb + (line.empty() ? "" : " \xC2\xB7 " + line);
		// its extension left its repository: the series and its downloads stay, new chapters may not come. Said in the
		// header's line unless something else is happening there (a failure, a check)
		if (!bad && !mangaSrcRefreshing && mangaJavaSourceObsolete(mangaSrcId))
			tabCrumb = "Its source was removed from the repository; new chapters may not come.";
		MangaResumePoint resume = mserSumResume;
		// one row of keys (screens.js seriesB, less its Check: that is a pull on the chapters, and L): Resume (A),
		// Get (R), Info, the heart. The row is one stop for UP/DOWN; LEFT/RIGHT light each key in turn (mserBtn)
		Kit& k = *ui.kit;
		bool onRow = mserCursor < 0;
		bool inLib = mangaSourceSeriesInLib();
		struct K { const Glyph* g; const char* label; const char* cap; bool off, lit; };
		const K keys[MG_SRC_KEYS] = {{&G_PLAY, "Resume", "A", n == 0, false}, {&G_DOWNLOAD, "Get", "R", n == 0, false},
			{&G_INFO, "", "", false, false}, {inLib ? &G_HEART : &G_HEART_O, "", "", false, inLib}};
		for (int i = 0; i < MG_SRC_KEYS; i++)
		{
			Kit::Key kk;
			kk.glyph = keys[i].g;
			kk.label = keys[i].label;
			kk.key = keys[i].cap;
			kk.off = keys[i].off;
			kk.lit = keys[i].lit;
			kk.tone = i == 0 ? Kit::Tone::Primary : Kit::Tone::Normal;
			kk.focused = onRow && mserBtn == i;
			kk.pressed = (i == 0 && mangaPressed == MG_RESUME) || (i == MG_SRC_KEYS - 1 && mangaPressed == MG_HEART);
			k.key(mangaSrcBtn(i), kk);
		}
		if (n == 0)
		{
			if (!mangaSrcError.empty())
			{
				drawMangaTabEmpty(p, G_WEB, browserCheckText(mangaSrcError) ? "This source is behind a browser check" : "Couldn\xE2\x80\x99t load it",
					"The list you had is kept. A: try again");
				ui.drawHints({{"A", "Try again"}, {"L", "Check"}, {"B", "Back"}});
			}
			else
			{
				lay::Box a = mangaListArea(p);
				if (!mangaSrcLoading) drawMangaTabEmpty(p, G_BOOK, "No chapters", "");
				else ui.drawLoader(a.x + a.w / 2, a.y + a.h / 2, "Loading chapters\xE2\x80\xA6");
				ui.drawHints({{"L", "Check"}, {"B", "Back"}});
			}
			return;
		}
		if (mserCursor >= n) mserCursor = n - 1;
		// the chapters, each carrying its own state: read, Resume's, the page, its download (Queued, 12 / 40, On card).
		// Pulled down past the top, the source is asked for new chapters (what L does)
		mlChapters.refresh = [this](Player& pl) {
			fprintf(stderr, "manga: the chapters pulled down: checking the source for new ones\n");
			mangaSourceSeriesRefresh(pl);
		};
		mangaRows(mlChapters, p, n, mserScroll, mserCursor, [this, rc = resume.chapter](int i) {
			const MangaChapter& c = mangaSrcChapters[i];
			MangaRow m = mangaChapterRow(chapterName(c), sourceItemId(mangaSrcId, c.key), i == rc, -1);
			MangaDlMark dm = mangaDlMarkFor(mangaSrcId, mangaSeriesId, c);
			switch (dm.kind)
			{
			case MangaDlMark::Running:
				m.pill = dm.total > 0 ? std::to_string(dm.done) + " / " + std::to_string(dm.total) : "Downloading";
				m.pillLive = true;
				m.bar = dm.total > 0 ? dm.done / (double)dm.total : 0;
				break;
			case MangaDlMark::Queued: m.pill = "Queued"; m.pillLive = false; break;
			case MangaDlMark::Paused: m.pill = "Paused"; m.pillLive = false; break;
			case MangaDlMark::Failed: m.pill = "Failed \xC2\xB7 R retries"; m.pillBad = true; m.pillLive = false; break;
			case MangaDlMark::OnCard: m.pill = "On card"; m.pillLive = false; break;
			default: break;
			}
			return m;
		});
		if (mserCursor < 0)
		{
			static const char* ROW[MG_SRC_KEYS] = {"Resume", "Get", "Info", "Library"};
			ui.drawHints({{"A", ROW[std::clamp(mserBtn, 0, MG_SRC_KEYS - 1)]}, {"LEFT/RIGHT", "Keys"}, {"DOWN", "Chapters"}, {"B", "Back"}});
		}
		else if (!mangaSourceEngineReady() && !mangaJavaCheckPending) ui.drawHints({{"", "Loading manga engine\xE2\x80\xA6"}, {"X", "Read / unread"}, {"B", "Back"}});
		else ui.drawHints({{"A", "Read"}, {"X", "Read / unread"}, {"R", "Get"}, {"L", "Check"}, {"B", "Back"}});
		if (mdlSheet != MDLS_NONE) later([this, &p] { drawMangaDlSheet(p); });
	}

	// ---- signature() ----

	std::string Ui::Manga::mangaNetSignature() const
	{
		char buf[320];
		snprintf(buf, sizeof buf, "|bs%d %d %d %d %d %d %d %d %zu %zu %zu %d %d %d %d %d",
			mangaView, mangaSrcTab, (int)mangaSrcHasLatest, mbrowseCursor, mbrowseScroll, mangaSrcCursor, mangaSrcScroll,
			mangaSrcPage, mangaSrcItems.size(), mangaSrcChapters.size(), mangaSrcRows.size(), (int)mangaSrcMore,
			(int)mangaSrcLoading, (int)mangaSrcInfoOn, (int)mangaSrcChaptersOn, (int)mangaSrcPendFull);
		std::string s = buf;
		// the series page's waiting state: on or off, its words and seconds (the spinner's steps: drawLoader's)
		if (mangaOpenWaiting())
		{
			MangaWaitView v = mangaOpenWaitView();
			s += "|wait" + v.title + v.line + mangaChoosePendName;
		}
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

namespace bp
{
	// ---- On this handheld: Y's help, X's Open / Delete ----

	std::string Ui::Manga::mangaLocalFolder() const
	{
		std::vector<std::string> places;
		for (auto& r : ui.roots) places.push_back(r.path);
		auto found = mangaRoots(places);
		if (!found.empty()) return found.front();
		if (mangaNet && !mangaNet->downloadsRoot().empty()) return mangaNet->downloadsRoot();
		return "/data/Manga";
	}

	// the series' menu (Y on On this handheld): a More sheet over mlocSheet; a row is A on it
	void Ui::Manga::mangaLocalMenuAct(int row, Player& p)
	{
		if (mlocCursor != row) mlocDeleteArmed = false;
		mlocCursor = row;
		if (mlocCursor == 0)
		{
			mlocSheet = MLS_NONE;
			mangaChoose(MT_BROWSE, mListCursor, p);
			return;
		}
		if (mlocCursor == 1)
		{
			// Info: the series' folder (or its one file), with its chapters counted
			mlocSheet = MLS_NONE;
			if (mListCursor >= 0 && mListCursor < (int)mangaLocal.size())
			{
				const LocalSeries& s = mangaLocal[mListCursor];
				int n = (int)s.chapters.size();
				ui.openFileInfo(s.path, s.title, {{"Chapters", std::to_string(n)}}, &p);
			}
			return;
		}
		// Delete: A again within the window takes the files off the card
		Uint32 now = SDL_GetTicks();
		if (!mlocDeleteArmed || now - mlocDeleteAt >= QUIT_WINDOW_MS)
		{
			mlocDeleteArmed = true;
			mlocDeleteAt = now;
			return;
		}
		mangaLocalDeleteNow(p);
	}

	void Ui::Manga::mangaLocalDeleteNow(Player& p)
	{
		mlocSheet = MLS_NONE;
		mlocDeleteArmed = false;
		if (mListCursor < 0 || mListCursor >= (int)mangaLocal.size()) return;
		const LocalSeries s = mangaLocal[mListCursor];
		// only ever a series inside one of the card's Manga folders, never such a folder itself
		std::vector<std::string> places;
		for (auto& r : ui.roots) places.push_back(r.path);
		bool inside = false;
		for (auto& root : mangaRoots(places))
			if (s.path.size() > root.size() + 1 && s.path.compare(0, root.size() + 1, root + "/") == 0) inside = true;
		if (!inside || s.path.find("/../") != std::string::npos)
		{
			fprintf(stderr, "manga: not deleting %s (outside the Manga folders)\n", s.path.c_str());
			ui.alert("Couldn\xE2\x80\x99t delete it", "\"" + s.title + "\" couldn\xE2\x80\x99t be deleted. Is the card read only?");
			return;
		}
		Uint32 t0 = SDL_GetTicks();
		// deepest first, links not followed (a link is removed, never what it points to)
		int failed = 0;
		static int* failedAt;
		failedAt = &failed;
		nftw(s.path.c_str(), [](const char* f, const struct stat*, int, struct FTW*) {
			if (::remove(f) != 0) (*failedAt)++;
			return 0;
		}, 16, FTW_DEPTH | FTW_PHYS);
		fprintf(stderr, "manga: deleted %s (%d chapters) in %u ms, %d left\n", s.path.c_str(), (int)s.chapters.size(),
			(unsigned)(SDL_GetTicks() - t0), failed);
		if (mangaStore)
		{
			mangaStore->removeSeries("local:" + s.path);
			mangaSave();
		}
		// the series leaves the list now (a scan of the whole card for one folder gone would be the slow way)
		mangaLocalForget({s.path});
		if (mListCursor >= (int)mangaLocal.size()) mListCursor = std::max(0, (int)mangaLocal.size() - 1);
		if (mListScroll > mListCursor) mListScroll = mListCursor;
		if (failed) ui.alert("Not all of it was deleted", "Some files of \"" + s.title + "\" couldn\xE2\x80\x99t be deleted.");
		else p.osd("Deleted " + s.title, 2500);
	}

	void Ui::Manga::mangaLocalSheetOpen()
	{
		ui.openMore([this](const Player&) { return mangaLocalSpec(); }, "mangaloc", [this] { return mlocSheet == MLS_MENU; },
			[this] { mlocSheet = MLS_NONE; mlocDeleteArmed = false; });
	}

	Ui::MoreSpec Ui::Manga::mangaLocalSpec()
	{
		MoreSpec m;
		if (mlocSheet != MLS_MENU || mListCursor < 0 || mListCursor >= (int)mangaLocal.size()) return m;
		const LocalSeries& s = mangaLocal[mListCursor];
		m.glyph = &G_FOLDER;
		m.title = s.title;
		m.note = mlocDeleteArmed ? "Press A again to delete its files" : "";
		std::string count = std::to_string(s.chapters.size()) + (s.chapters.size() == 1 ? " chapter" : " chapters");
		auto row = [&](const Glyph* g, const char* label, const std::string& note, bool danger, bool lit) {
			MoreItem it;
			it.glyph = g;
			it.label = label;
			it.note = note;
			it.danger = danger;
			it.lit = lit;
			it.stay = true;
			int at = (int)m.rows.size();
			it.act = [this, at](Player& p) { mangaLocalMenuAct(at, p); return false; };
			m.rows.push_back(it);
		};
		row(&G_PLAY, "Open", "", false, false);
		row(&G_INFO, "Info", "", false, false);
		row(&G_TRASH, "Delete", mlocDeleteArmed ? "Press again" : count, true, mlocDeleteArmed);
		return m;
	}

	// X on On this handheld (and on its empty page): where to put manga, as Info's page
	void Ui::Manga::mangaLocalHelpOpen()
	{
		auto lines = [&](InfoGroup& g, const std::string& text) {
			for (auto& l : ui.wrapText(text, F_I12, 410, 6)) g.rows.push_back({"", l});
		};
		InfoGroup where;
		where.title = "Copy manga into this folder";
		where.rows.push_back({"", elideMiddle(mangaLocalFolder(), F_I12, 410)});
		lines(where, "or into a Manga folder at the top of the second card (Manga, manga or MANGA):");
		where.rows.push_back({"", "Card 2 \xE2\x80\xBA Manga"});
		InfoGroup mac;
		mac.title = "A card set up on a Mac";
		lines(mac, "Choose Master Boot Record and exFAT (or FAT32), or the handheld may not see it.");
		InfoGroup how;
		how.title = "Each series is a folder";
		lines(how, "Its chapters are .cbz files (or .cbr, .pdf), or folders of pictures (Chapter 1...).");
		lines(how, "Copy them with Files, the web dashboard's Files (Upload), or with the card in a computer.");
		lines(how, "Downloaded chapters are kept there too.");
		std::vector<InfoGroup> groups;
		groups.push_back(std::move(where));
		groups.push_back(std::move(mac));
		groups.push_back(std::move(how));
		ui.openInfoGroups("Adding manga", std::move(groups));
	}
}

namespace bp
{
	void Ui::Manga::mangaSrcInfoOpen()
	{
		// the series' details: Info's page (the one Files shows), its description in the lines it wraps to
		std::string title = mangaSrcInfoOn && !mangaSrcInfo.title.empty() ? mangaSrcInfo.title : mangaSrcTitle;
		InfoGroup g;
		g.title = "Series";
		if (mangaSrcInfoOn && !mangaSrcInfo.author.empty()) g.rows.push_back({"Author", mangaSrcInfo.author});
		if (mangaSrcInfoOn && !mangaSrcInfo.status.empty()) g.rows.push_back({"Status", mangaSrcInfo.status});
		std::string from = mangaSourceLabel(mangaSeriesId);
		if (!from.empty()) g.rows.push_back({"Source", from});
		InfoGroup d;
		d.title = "Description";
		if (mangaSrcInfoOn && !mangaSrcInfo.description.empty())
			for (auto& line : ui.wrapText(mangaSrcInfo.description, F_I12, 410, 60)) d.rows.push_back({"", line});
		else d.rows.push_back({"", mangaSrcInfoOn ? "The source gives no description." : "No details yet: Refresh (L) asks the source."});
		std::vector<InfoGroup> groups;
		if (!g.rows.empty()) groups.push_back(std::move(g));
		groups.push_back(std::move(d));
		ui.openInfoGroups(title.empty() ? "Series" : title, std::move(groups));
	}
}
