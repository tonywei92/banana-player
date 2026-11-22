// Extensions and the manga reader: an area of Ui (Ui::Area, ui.h; docs/notes/ui-areas.md). The Extensions section's
// page (Page::ExtSection: a row per extension that is on, then Manage extensions), Manage extensions
// (Page::Extensions), and the Manga pages: the app's four tabs (Page::Manga, with a source's and a server's
// grids as its views), a series (Page::MangaSeries), the reader's bottom panel (Page::MangaRead, on PageReader:
// ui_reader.h), the Sources page and the reader's own settings. Its state and handlers are here and in ui_ext.cpp and
// ui_manga*.cpp: a feature inside them touches these files, not ui.h. Included by those files, by main.cpp (the
// workers' answers, the network side, the page cache) and the dashboard's routes (src/web/api_manga.cpp), and by the
// few files of Ui that ask the area something (Settings' Extensions group, the keyboard's fields, Files' moves).
#pragma once
#include "ui.h"
#include "ui_reader.h"
#include "javaext.h"
#include "mangafilters.h"
#include "mangajava.h"
#include "manga_ui_logic.h"
#include "mangalocal.h"
#include "manganetfolders.h"
#include "mangaremote.h"
#include "mangarepos.h"
#include "mangasource.h"
#include "mangastore.h"
#include "writequeue.h"	// DurableWriter: the store's saves off the main thread
#include "mangaworker.h"
#include "dlqueue.h"	// DlItem: the Downloads list's rows
#include "mangafetch.h"
#include "pagecache.h"
#include "coverworker.h"
#include "mangalibworker.h"
#include <unordered_set>

namespace bp
{
	// One area, not one per page: the Extensions page, Manage extensions and the app share the extension's switch,
	// its sheet and their touch ids (extSheet, extPageCursor, extPressed); the app's tabs, a source's and a server's pages, the
	// Downloads tab and the Download sheet share the store, the worker's jobs, the covers and the finger (a source's
	// page is a view of Page::Manga, not a page of its own). It is cut where that state is shared.
	struct Ui::Manga : Ui::Area
	{
		explicit Manga(Ui& u) : Area(u) {}

		// ---- the area (Ui's press, pointer, tick, renderPage and signature come here while one of its pages shows) ----
		bool has(Page pg) const override
		{
			return pg == Page::ExtSection || pg == Page::Extensions || pg == Page::Manga || pg == Page::MangaSeries ||
				pg == Page::MangaRead || pg == Page::MangaSettings || pg == Page::MangaSources;
		}
		bool key(Button b, Player& p) override;
		// its moves and lifts where the readers' are taken; its downs (but the reader's and the Extensions page's,
		// which is a shell page) after the header's "<" and the empty page's button (pointerDownLate)
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		bool pointerDownLate() const override { return ui.page != Page::MangaRead && ui.page != Page::ExtSection; }
		void tick(Player& p) override;
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;
		// the shown page's smooth list (Manga settings': SettingsPage's mangaList)
		SmoothList* smoothList() override;
		// the Manga pages' own: the last list a page drew (a sheet's over its page's), null on a page with none
		SmoothList* mangaPageList() { return shownList; }
		using MList = PageReader::FollowList;	// one per list (ui_reader.h)
		// the Extensions page's and Manage extensions' part of the signature
		std::string extSignature() const;

		// ---- inside the shell (docs/notes/2026-11-20-manga-shell.md) ----
		// Every page of the area but the reader's (Page::MangaRead keeps its own frame) is a shell page: the rail,
		// the header and the hint bar are the shell's, Manage extensions and the four tabs are drawn on the kit, and
		// the rest of each page's body is drawn in the shell's body.
		bool shellPage(Page pg) const { return has(pg) && pg != Page::MangaRead; }
		// the app's tab view (Page::Manga, neither a source's nor a server's view): the kit's tabs over its body
		bool tabView() const { return ui.page == Page::Manga && mangaView == 0; }
		// Library and History are drawn on the kit in the content box under the tabs (not bridged, not scaled):
		// the cover grid (lay::coverGrid, Kit::coverTile) and the two-line rows (lay::rowList, Kit::row + a Continue key)
		bool kitBody() const { return tabView(); }	// (all four tabs, Browse's and Downloads' own pages included)
		// Browse's and Downloads' lists (screens.js manga-browse, manga-downloads): rows of the kit (lay::rowList,
		// Kit::row) in the content under the tabs; the row under the cursor may carry a key (Edit · X, Pause · X)
		struct MangaRow
		{
			const Glyph* glyph = nullptr;
			std::string name, second, note, pill;
			bool off = false, pillBad = false;
			double bar = -1;	// a progress line along the row's foot (a chapter downloading); -1 none
			bool barBad = false;
			std::string keyLabel, keyCap;	// the focused row's key ("" none)
			bool pillLive = false;	// the pill yellow (Update, Recommended), not a fact
			bool dot = false, dotOn = false;	// a chapter's row: no icon, a dot on the one Resume opens (Kit::Row::dot)
		};
		// where a Manga page's list or grid goes in the shell's body: under the app's tabs, a source's tabs or Sources'
		// lists, under a series' keys, or the inner body (a server's catalogue, a network folder's series)
		lay::Box mangaListArea(const Player& p) const;
		std::vector<int> mangaSrcSlots() const;	// a source's tabs: Popular, Latest (when it has one), Search
		lay::TabPage mangaSrcTabPage(const Player& p) const;
		lay::TabPage mangaSourcesTabPage(const Player& p) const;	// Sources: Available · Repositories
		// a cover grid in `area` (lay::coverGrid, Kit::coverTile): the source's and the server's catalogues
		// a grid row is one smooth row: its view SP6 wider than `area` all round but the foot (the focus ring), cover(i)
		// the picture's key ("" none yet) asked as the row's key is (a cover asked counts as drawn: Music's fix)
		struct GridCell { std::string title, badge, coverKey; std::function<void(Rect)> picture; };
		void drawMangaGrid(MList& m, lay::Box area, int n, int& scroll, int& cursor, const std::function<GridCell(int)>& cell);
		Rect mangaGridView(lay::Box area) const;
		void mangaGridPointer(MList& m, lay::Box area, int x, int y, std::function<int()> count, int* scroll, int* cursor,
			std::function<void(int)> open, std::function<void()> scrolled = nullptr);
		// a series page's keys (lay::seriesPage + lay::keyRow): their boxes for `widths`
		std::vector<lay::Box> mangaSeriesKeys(const std::vector<int>& widths) const;
		// ---- the lists on smooth lists (ui_smoothlist.h; docs/notes/2026-11-21-smooth-scroll.md) ----
		// One per list (MList: the pages keep their int scroll and cursor, which their keys set as they always did;
		// follow and sync turn those into the list's motion and back)
		void mlFollow(MList& m, Rect view, int rowH, int n, int cols, int& scroll, int* cursor);
		void mlSync(MList& m, int& scroll);
		SmoothList* frameList = nullptr;	// the list the page drew this frame (the last one drawn: a sheet's)
		SmoothList* shownList = nullptr;	// last frame's: the one Ui steps
		MList mlLib{ui}, mlHis{ui}, mlBrowse{ui}, mlLocal{ui}, mlNet{ui}, mlDl{ui}, mlDlCard{ui}, mlChapters{ui},
			mlSrcGrid{ui}, mlSrvGrid{ui}, mlSrcAv{ui}, mlFilter{ui}, mlFilterCheck{ui}, mlDlChoose{ui};
		// a page's kit rows (lay::rowList's pitch) in mangaListArea: drawn on `m`, the cursor's row focused
		void mangaRows(MList& m, const Player& p, int n, int& scroll, int& cursor, const std::function<MangaRow(int)>& at);
		// a sheet's rows (lay::ListSheet, LIST_ROW apart, `shown` of them in sight): row(i) without its focus, drawn on `m`
		Rect mangaSheetView(const lay::ListSheet& sh, int n, int shown) const;
		struct SheetRow { const Glyph* glyph = nullptr; std::string name, note; bool danger = false; };
		void mangaSheetRows(MList& m, const lay::ListSheet& sh, int n, int shown, int& scroll, int& cursor,
			const std::function<SheetRow(int)>& row);
		// a finger down on them: true when it is in their view (the touch begun: a drag, or a tap on row i)
		bool mangaSheetPointer(MList& m, const lay::ListSheet& sh, int n, int shown, int x, int y, std::function<void(int)> tap);
		// a finger on the rows: a drag moves them by the pixel and glides, a tap is tap(i, onKey)
		void mangaRowsPointer(MList& m, const Player& p, int x, int y, std::function<int()> count, int* scroll, int* cursor,
			std::function<MangaRow(int)> at, std::function<void(int, bool)> tap, std::function<void()> scrolled = nullptr);
		std::string tabCrumb;	// Browse's and Downloads' header line, set as they draw (where, the storage line)
		MangaRow mangaBrowseRowAt(const Player& p, int i) const;
		MangaRow mangaLocalRowAt(int i) const;
		MangaRow mangaNetRowAt(int i) const;
		void drawMangaNetFoldersKit(const Player& p);	// Browse's network folder (Page::Manga), on the kit
		MangaRow mangaDlRowAt(int i) const;
		MangaRow mangaDlCardRowAt(int i) const;
		// a series' chapter row (a card's, a source's, a server's, a network folder's): its read mark, Resume's, its page
		MangaRow mangaChapterRow(const std::string& title, const std::string& progressId, bool resume, int pages) const;
		mutable std::vector<DlItem> mdlItems;	// the queue's chapters not done, as the Downloads list last read them
		void mangaDlItemsRead() const;
		lay::AlbumGrid mangaLibGrid(const Player& p) const;
		lay::RowList mangaHisList(const Player& p) const;
		// a History row whose series can't be opened now: "source removed" (a source no longer installed, once the
		// sources are known), "not on a card" (a card series the last scan didn't find); "" when it can
		std::string mangaHisGone(const ChapterProgress& cp) const;
		void drawMangaTabEmpty(const Player& p, const Glyph& g, const std::string& title, const std::string& text);
		lay::TabPage mangaTabPage(const Player& p) const;
		// the header the shell draws: the page's title, its crumb, the hints its old body drew (captured while it was
		// bridged: Ui::capHints), plus L2 / R2 on the tabs
		ShellHead head(const Player& p) const;
		// a sheet, a menu or the keyboard is up: keys and touches go to it as they always did, the old way (no
		// sections on L / R, no mapping of the touches: the sheet is drawn at full size over the shell)
		bool shellModal() const;
		// the kit's part of the body (the tabs), drawn after the bridged body; its touches, before they are mapped
		void drawShellBody(const Player& p);
		bool shellPointer(const Pointer& pt, int x, int y, Player& p);
		// a sheet over a shell page is drawn after the shell (full size, over its frame): the page's draw hands it
		// here while sheetsLater is set, and renderPage runs them once the frame is drawn
		bool sheetsLater = false;
		std::vector<std::function<void()>> sheetQueue;
		void later(std::function<void()> f)
		{
			if (sheetsLater) sheetQueue.push_back(std::move(f));
			else f();
		}
		void runLater()
		{
			auto q = std::move(sheetQueue);
			sheetQueue.clear();
			sheetsLater = false;
			for (auto& f : q) f();
		}
		bool mangaSetPressed = false;	// a finger on the settings key beside the tabs
	int mangaTabPressed = -1;	// a finger on one of the kit's tabs
		void mangaTabSet(int tab, Player& p);	// L2 / R2 or a tap: the tab shown

		// ---- for main, the dashboard and the page reader ----
		// the place inside a page kept for a chapter, and the series' own Book (PageReader::takeMangaRead fills them in)
		float mangaWithinFor(const std::string& chapterId, int page) const;
		// the dashboard's series view (design 8): a chapter with what the reader knows of it. download is "", or the
		// mark the handheld's series page gives it ("queued", "running", "paused", "failed", "onCard")
		struct MangaWebChapter
		{
			std::string id, title, download;
			double number = -1;
			bool read = false;
			int page = 0, pages = 0, done = 0, total = 0;
		};
		struct MangaWebSeries
		{
			std::string id, title, source, error;
			bool local = false, pending = false;
			std::vector<MangaWebChapter> items;
		};
		// GET /api/manga/chapters: true with the list; false with pending (its source is being asked; try again) or
		// error. A local series comes from the card's scan, a source series from its worker as the handheld asks it.
		bool mangaWebChapters(const std::string& seriesId, MangaWebSeries& out);
		// POST /api/manga/download: the chapters queued as the Download sheet queues them (their page lists asked of
		// the source first); added counts the new asks. False (with error) when they can't be queued.
		bool mangaWebDownload(const std::string& seriesId, const std::vector<std::string>& chapterIds, int& added,
			std::string& error);
		// the Download sheet's queueing, shared with the dashboard: each chapter waits for its source's page list
		// before it becomes a queue item. added: how many new asks (already queued or on the card: none).
		int mangaDlQueueChapters(const std::string& sourceId, const std::string& scriptPath,
			const std::string& seriesId, const std::string& seriesTitle,
			const std::vector<MangaChapter>& chapters);
		// the manga reader's network side (main's; started when the reader is first used with it on)
		class MangaNet* mangaNet = nullptr;
		// the manga reader's library and progress store (public for the web dashboard's manga routes)
		class MangaStore* mangaStorePublic() const { return mangaStore.get(); }
		// the dashboard changed the store: written as the reader's own saves are, off the main thread
		void mangaStoreChanged() { mangaSave(); }
		// main's URL chapters put their pages in this same cache (the Manga settings' Clear cache empties it), not in
		// a second PageCache over the same folder
		PageCache& mangaPageCacheRef() { return mangaPageCache; }
		// the cover cache (the web dashboard's GET /api/manga/cover serves from it; the reader fills it)
		PageCache& mangaCoverCacheRef() { return mangaCoverCache; }
		// the manga library's covers, decoded on main's own worker (null: the generated ones only); main forwards its
		// event, and the answers become textures here, on the main thread
		CoverWorker* coverWorker = nullptr;
		void coversReady(std::vector<CoverWorker::Result> results);
		// the card's library, listed and its chapter files read on main's own worker (null: read here, all at once, as
		// before); main forwards its event, and the answers go into mangaLocal here, on the main thread
		MangaLibWorker* libWorker = nullptr;
		void mangaLibReady(std::vector<MangaLibWorker::Result> results);
		// the store's durable writes (a progress save's fsync is tens of ms); main creates it once and never
		// destroys it, its thread may be inside an fsync at exit
		DurableWriter* writer = nullptr;

		// ---- the pages ----
		// A source's filter sheet (ui_manga_filters.cpp): the definitions filters() gave, the values Apply sends,
		// and where the sheet is. Kept per source while the app runs (5.2.1), not written to the state dir.
		struct MangaFilterSession
		{
			bool known = false, loading = false;
			std::string error;
			uint64_t serial = 0;		// the Filters job in flight (another serial's answer is stale)
			uint64_t javaSerial = 0;	// the Java host's filter request (a separate serial space from the worker's)
			std::vector<MangaFilter> defs;
			std::vector<MangaFilterValue> values;
			bool open = false;			// the sheet is up
			std::vector<int> path;		// group indices from the top, for the level shown
			int cursor = 0, scroll = 0;
			int checkAt = -1;			// the filter whose multi/tri option list is open (-1: the rows)
			int checkCursor = 0, checkScroll = 0;
			int textAt = -1;			// the text filter the keyboard is editing
		};
		// ---- extensions (ui_ext.cpp) ----
		// The Extensions section's page (Page::ExtSection, docs/redesign: ext-section, ext-section-video,
		// ext-section-empty), Manage extensions (Page::Extensions) and the manga reader's page (Page::Manga): a row per
		// extension that is on, its switch, and the first-time note about repositories over Manage extensions
		bool extSheet = false;	// the note, over Manage extensions
		int extSheetCursor = 0, extPressed = -1;	// the sheet's two buttons; the touch's button
		void openExtensions(bool fromSettings = false);	// the Extensions page's Manage extensions row, Settings › Extensions
		bool extFromSettings = false;					// Manage extensions was opened from Settings: B goes back there
		void openManga(Player& p);	// the app: its store loaded and the card scanned (ui_manga.cpp)
		// the rail's Extensions section: its list, or Manga again where it was left unless a video plays; the list
		// alone for B on Manga (openExtList). Adopts the page into the shell's body (drawn on the kit, not bridged)
		void openExtensionsSection(Player& p);
		void openExtList(Player& p);
		void sectionLeft(Page from);	// where the section is being left: Manga, or its list
		void enterManga(Player& p);	// its page where it was left, else openManga
		void extMangaRow(Player& p);	// A or a tap on the Manga row: the guard first while a video plays
		void extStopVideo(Player&);	// "Stop video & open Manga": Stop's place kept, then Manga
		void extToggleManga(Player& p);	// A on the row or the chip: on / off (the first time, the sheet)
		void extSheetChoose(int i, Player& p);	// Open Manga, Done
		bool extSectionKey(Button b, Player& p);
		bool extKey(Button b, Player& p);
		bool extPointer(const Pointer& pt, int x, int y, Player& p);
		bool extAct(int id, Player& p);
		void openExtSettings();	// X or the gear: the manga reader's own settings page (Page::MangaSettings)
		void drawExtSection(const Player& p);
		void drawExtensions(const Player& p);
		void drawManga(const Player& p);
		void drawExtSheet(const Player& p);
		// the touch ids: apart from the grid's, the buttons' and the pages'
		static constexpr int EXT_ROW = 562, EXT_MANAGE_ROW = 567, EXT_EMPTY_KEY = 568, EXT_CHIP = 563, EXT_GEAR = 564,
			EXT_OPEN = 565, EXT_DONE = 566;
		Rect extSheetBox() const { return {60, 90, 520, 300}; }
		Rect extSheetBtn(int i) const { return {i ? 328 : 92, 310, 220, 50}; }
		// the Extensions page (Page::ExtSection): its cursor over the rows (the extensions on, then Manage extensions),
		// a finger's row, and where the section goes back to. Its Rects are lay::extPage's and lay::emptyState's.
		int extPageCursor = 0, extPagePressed = -1;
		Page extReturn = Page::ExtSection;	// the section's page when it is left: Manga, or its list
		bool mangaReady = false;			// the Manga page has been opened once: entering it again keeps its tab and cursor
		bool guardStoppedVideo = false;		// the guard's stop: the five-second "Video stopped" note is left out

		// ---- the manga reader (ui_manga.cpp) ----
		// Page::Manga's four tabs (Library, History, Browse, Downloads), each its own cursor and scroll;
		// Page::MangaSeries; and Page::MangaRead, the bottom panel while main's reader shows pages on top. Sources is a
		// page of its own (Page::MangaSources), opened with X on Browse. The store is <state>/manga (setStateDir keeps
		// the path), loaded and scanned the first time the app opens.
		enum MangaTab { MT_LIBRARY, MT_HISTORY, MT_BROWSE, MT_DOWNLOADS, MANGA_TABS };
		int mangaTab = MT_LIBRARY;
		std::unique_ptr<MangaStore> mangaStore;
		std::vector<LocalSeries> mangaLocal;	// On this handheld: each Manga folder's series, in reading order
		std::unordered_map<std::string, size_t> mangaLocalById;
		bool mangaScanned = false;	// the card was read at least once (the dashboard's local series can ask for it first)
		// the library read in the background (docs/notes/2026-11-16-manga-library-scan.md): a scan under way (its gen, the
		// series listed so far: the "Reading your library" line), the series whose chapter files were asked for, and a
		// serial that grows with every change to mangaLocal (signature() follows it)
		bool mangaScanning = false;
		size_t mangaScanCount = 0;
		uint64_t mangaScanGen = 0, mangaLocalSerial = 0;
		Uint32 mangaScanAt = 0;
		std::unordered_map<std::string, Uint32> mangaFillAsked;
		bool mangaDirty = false;	// the store changed and wasn't written yet (mangaTick writes it every MANGA_SAVE_MS)
		Uint32 mangaSavedAt = 0;
		// A page's progress is recorded as the reader moves, but written to the card every few seconds only: an fsync
		// on the SD card can take tens of ms, and while a chapter is flicked through that was a hitch every second.
		// Leaving the chapter or the app flushes it (mangaFlush)
		static constexpr Uint32 MANGA_SAVE_MS = 5000;
		std::vector<std::string> mangaNotes;	// toasts made where no Player was at hand (mangaNotesTick shows them)
		// the Library and History lists made again when the store changes: the library, its unread counts, the
		// History's series rows
		mutable uint64_t mangaCacheSerial = 0;
		mutable int mangaCacheCount = -1;
		mutable bool mangaCacheValid = false;
		mutable std::vector<MangaSeries> mangaLib;
		// a cover's cache key: the URL, but the manga engine's as "javahost/<install token>/<path>" (its port changes at
		// every start, and its numbers start over at every fresh install: the token keeps covers of two installs apart)
		std::string mangaCoverKey(const std::string& url) const;
		// a cover as the store keeps it: the engine's by its key (its install known), any other by its URL
		std::string mangaCoverStored(const std::string& url) const;
		std::string mangaSeriesCoverUrl(const std::string& seriesId) const;	// a saved series' cover by URL ("" for a local one)
		mutable std::vector<int> mangaLibUnread;
		mutable std::vector<ChapterProgress> mangaHistory;	// one row per series (historyBySeries)
		mutable int mlibGrid = 0, mlibScroll = 0;	// (mutable: the cursor follows its series when the list is sorted again)
		int mhisCursor = 0, mhisScroll = 0;
		int mbrowseCursor = 0, mListCursor = 0, mListScroll = 0;
		bool mBrowseList = false;	// Browse's "On this handheld": the series list open
		std::string mangaSeriesId;	// the series shown (Page::MangaSeries)
		int mangaSeriesFrom = 0;	// 0 Library, 1 Browse's list, 2 History (B goes back there)
		int mserCursor = -1, mserScroll = 0;	// -1 the Resume button, else a chapter
		Uint32 mangaClearAt = 0;	// History's Clear pressed once: again within 2 s clears it
	// the page cache (the setting's cap), opened when the app does: the network sources put their pages in it
		PageCache mangaPageCache;
		// the cover cache (50 MB fixed; docs/design/manga.md section 6): the cover images the reader fetched, keyed
		// by their URL, so a cover already downloaded isn't asked for again and the web dashboard can serve one
		PageCache mangaCoverCache;
		static constexpr uint64_t MANGA_COVER_CACHE_LIMIT = 50ull * 1024 * 1024;
		Uint32 mangaCacheAt = 0;	// Clear cache pressed once: again within 2 s empties it
		// the network folders' chapters copied to <state>/manga/cache/net: under the same cap as the pages, the
		// oldest going first (never `keep`, the one open); their bytes join the Clear cache row's size
		uint64_t mangaNetCacheBytes = 0;
		void mangaNetTrim(const std::string& keep, const Player& p);
		Uint32 mangaJavaAt = 0;		// Java extensions' Remove pressed once: again within 2 s removes it
		bool mangaHistOn = true;	// the setting, kept here for the reader's records (mangaRecordProgress has no Player)
		int mangaPressed = -1;		// the card, cover, row or button under the finger
		// Page::MangaRead: what was last recorded, and the finger on it (the chapter open: PageReader's)
		bool mangaReadLive = false;	// main has had this request open (a close before that isn't main's)
		// a source's chapter picked from its series page: the reader page shows at once, "Loading chapter…" until its
		// page list comes (then the request goes to main), or why it didn't (A tries again, B goes back)
		bool mangaReadWaiting = false;
		// the source series page's unread count and Resume point, and what they were made from (drawMangaSourceSeries)
		std::string mserSumKey;
		std::string mserFocusChapter;
		int mserBtn = 0;
		// the series page's button row: 0 Resume, 1 Download, 2 Refresh, 3 Library (LEFT/RIGHT)	// a chapter the series page puts its cursor on once its list shows (from History)
		int mserSumUnread = 0;
		MangaResumePoint mserSumResume;
		// the extensions starting (about 25 s on the handheld), on every page: a pill at the top, its seconds counting,
		// then "Extensions ready" a moment. Its text is in signature(), so it redraws as the seconds change
		mutable int extNoticeState = -1;
		Uint32 videoBlockedAt = 0;
		Uint32 engineStopAt = 0;
		// the engine is asked for once per entry into the Manga pages (mangaVideoGuard): in them now, and the wish
		// still to be made (it waits while a stop is ending or the engine is still downloading)
		bool engineInManga = false, engineEntryWant = false;
		Uint32 engineUseAt = 0;	// the last time a Manga page counted as the engine's use (every 30 s while one shows)
		// the manga extension's picture (assets/icons/manga.png), decoded at the size drawn
		SDL_Texture* mangaIconTex = nullptr;
		int mangaIconPx = 0;	// the size it was decoded at
		void drawMangaIcon(Rect r);
		// a short line in the notice at the top of the panel, gone by itself (the manga engine's other notices' place)
		std::string noticeText;
		Uint32 noticeUntil = 0;
		void mangaNotice(const std::string& text, Uint32 ms) { noticeText = text; noticeUntil = SDL_GetTicks() + ms; }
		// a manga source that stands behind a browser check (Cloudflare, DDoS-Guard): a dialog says so (the overlay
		// "mangaerr", Ui::openMangaErr), over any page. Opened once per error (mangaErrFor)
		// the error each asker last had a dialog for (0: a source's lists and series, 1: the reader's pages); forgotten
		// once that asker's error clears (the source asked again), so the same error after a reopen is said again
		std::string mangaErrFor[2], mangaErrText;
		void mangaErrCheck(const std::string& error, bool anyError = false, int asker = 0);	// anyError: not only a browser check
		void drawMangaErr();	// a video outside the reader asked the manga engine to stop (once)
		void mangaVideoGuard(Player& p);	// (ui_manga_java.cpp)	// a video stopped because the extensions run: the notice says so a moment
		mutable Uint32 extNoticeAt = 0;
		std::string extNoticeText() const;
		void drawExtNotice();
		void drawMangaReadWaiting();
		// the top panel's State card while a source's chapter is asked for (Ui::topPane); false: not waiting
		bool waitingPane(PaneSpec& out) const;
		void mangaReadWaitCancel();
		int mangaProgPage = -1, mangaProgPages = -1;
		float mangaProgWithin = 0;	// the place inside that page last kept (Vertical scroll)
		Uint32 mangaProgAt = 0;
		bool mangaTouch = false;
		double mangaTouchY = 0, mangaTouchX = 0;
		// the left stick (issue 16): whether it last commanded the reader (the Stop was sent), and Book's last push
		// direction (so one push is one spread, not one a frame)
		bool stickSent = false;
		int stickDir = 0;
		void drawMangaControls(const Player& p);
		bool mangaFullNavOff(int id) const;	// a PageReader::FN_* key with nothing to do here (the first chapter, the last page)
		void drawMangaToast();
		// fullscreen's tap at an edge (lay::readerFullTap): the page turned, true; false in the middle (the bar's)
		bool mangaFullTapTurn(int x, int y, Player& p);
		// SELECT and the Fullscreen key (asked: the toast says how to leave, every time), and a chapter opening in it
		void mangaFullscreen(bool on, Player& p, bool asked = false);
		int mangaModeFor(const std::string& seriesId, const Player& p) const;	// the series' own, else the setting's
		int mangaBookFor(const std::string& seriesId) const;	// the series' own Book (MANGA_BOOK_*; off by default)
		void mangaSetBook(int book, Player& p);	// on (held as book) or off, kept for the series
		bool mangaBookKey(Button b, Player& p);	// the reader's keys while in Book
		// Book's sideways bar (docs/redesign: book-comic-end): a tap, or START, shows it; its keys, the spread's
		// slider and "One screen" are its own, upright as the panel is held
		bool mangaBookBar = false;
		int mangaBookBarPressed = -1;
		void drawMangaBookBar(const Player& p);
		bool mangaBookBarPointer(const Pointer& pt, int x, int y, Player& p);
		void mangaBookBarAct(int which, Player& p);
		// fullscreen's navigation over the pages (lay::readerFullNav, PageReader::drawFullNav: the Files reader's too): a
		// tap in the middle shows it 4 s (a drag, the 4 s or another such tap hides it), and the finger that may still
		// be a tap; the sheet's key under it (mangaCtrlPressed: a PageReader::FN_* id) and its slider
		Uint32 mangaCtrlUntil = 0;	// when they hide (0: hidden)
		bool mangaTap = false;
		int mangaTapX = 0, mangaTapY = 0;
		int mangaTouchMoves = 0, mangaTouchFar = 0;	// this touch's move events, and the furthest (units) from its down (the log line)
		Uint32 mangaTapAt = 0;
		// the finger came down outside the sheet while it showed: lifted as a tap at an edge it turns the page, in the
		// middle it puts the sheet away and does nothing else; moved, it drags the page and hides it as any drag does
		bool mangaTapDismiss = false;
		int mangaCtrlPressed = -1;
		// double-tap on the page area: zoom to 200 % at the tapped point / back to the fit, as A does (but at the
		// finger). the first tap is recorded; a second tap within 300 ms and 20 units sends a ZoomPoint command
		Uint32 mangaLastTapAt = 0;
		int mangaLastTapX = 0, mangaLastTapY = 0;
		// ---- the reader's sheets, bookmarks and end card (ui_manga_reading.cpp) ----
		// START while reading: View (docs/redesign: comic-view), a sheet over the page (over the pages in Fullscreen and
		// Book, upright). Its Layout row: the pages on one screen, on both as one tall view (fullscreen), or Book
		enum { MV_ONE, MV_FULL, MV_BOOK, MV_COUNT };
		int mangaHoldLast = MANGA_BOOK_LEFT;	// how Book is held when the Layout row turns it on
		int mangaViewNow() const;
		void mangaSetView(int v, Player& p);
		struct ComicView;
		void mangaOpenView();
		// X (and the header's key): More (comic-more): this chapter at the left, the series and the app at the right
		void mangaOpenReadMore();
		bool mangaReadIsRead() const;
		void mangaReadMarkRead();	// Mark read / Mark unread
		// Get this chapter: what the row says (`note`), and false when there is nothing to get (on the card, queued,
		// not a source's)
		bool mangaReadCanGet(std::string& note) const;
		void mangaReadGet(Player& p);
		void mangaReadInfo();
		// the page bookmarks of the chapter open (the store's, kept with its place), and the one on this page
		std::vector<int> mangaMarksHere() const;
		bool mangaPageMarked() const;
		void mangaBookmarkToggle(Player& p);
		// the top panel's card at a chapter's end (docs/redesign: manga-end): false while there is none to show
		struct EndCard { std::string label, title, line; };
		bool mangaEndCard(EndCard& out, const Player& p) const;
		void mangaSetMode(int m, Player& p);	// this series' reading mode (View's row)
		// the first fullscreen ever: a toast in the panel's middle for 2 s (mangaFullSeen remembers it)
		Uint32 mangaToastUntil = 0;
		int mangaBookToast = 0;	// MANGA_BOOK_*: the toast says how to hold the handheld for Book (0: how to leave fullscreen)
		void mangaBookTurn(int step, Player& p);	// a spread on (+1, the next chapter at the end) or back (-1)
		void drawMangaBookTag();
		// the left stick (issue 16): the pages scroll or pan while it is held; Book turns a spread the way its D-pad
		// points (one push, one turn)
		void stickAxis(int value, Player& p, int valueX = 0);
	// the page numbers, turned as the pages are, at the foot of the spread
		static constexpr Uint32 MG_TAP_MS = 250, MG_CTRL_MS = 4000, MG_TOAST_MS = 2000;
		static constexpr int MG_TAP_UNITS = 12;	// a finger that moves less (640x480 units) may still be a tap
		void mangaOpen(Player& p);		// openManga: the store loaded, the card scanned, the tabs reset
		void mangaScan();				// each Manga folder read again (in the background) and its series put in the store
		void mangaLocalSet(std::vector<LocalSeries> found);	// a listing in place: the cursors stay on their series
		void mangaLocalMetas(const std::vector<MangaLibWorker::Found>& metas);	// chapter files read: lists re-sorted
		bool mangaLocalCommit(LocalSeries& s, int64_t now);	// its store entry; true when the store changed
		void mangaLocalWantFill(const std::string& id);	// its chapter files read, off the main thread
		bool mangaLocalFilling(const std::string& id) const;	// asked and not all known yet
		void mangaLocalForget(const std::vector<std::string>& paths);	// files deleted: their chapters and series leave
		const LocalSeries* mangaLocalAddOne(const std::string& path);	// one series listed now (a download just written)
		void mangaLocalReindex();
		std::string mangaReadingLine() const;	// "Reading your library… N series" while the card is read
		void mangaLocalSort();
		void mangaCache() const;
		void mangaSave();				// the store copied into the writer's slot (the reader's: every few seconds, and on closing/leaving)
		void mangaTick();				// the reader's page recorded, the store written when MANGA_SAVE_MS has passed
		void mangaFlush();				// the place now and the store written, at once (leaving the chapter, quitting)
		void mangaRecordProgress();		// the reader's place into the store
		void mangaCloseReader();		// B: Close pushed, progress saved, the series page
		void openMangaSeries(const std::string& id, int from, Player& p);
		void mangaOpenChapter(const LocalSeries& s, int i, int page, Player& p);
		void mangaChapterStep(int dir, Player& p);	// the chapter before (-1) or after (+1), at page 0
		const LocalSeries* mangaLocalSeries(const std::string& id) const;
		std::string mangaTitle(const std::string& id) const;
		int mangaReadChapterIndex() const;
		void mangaChoose(int tab, int i, Player& p);	// A on what a tab's cursor is on
		void mangaSeriesChoose(int i, Player& p);		// A on a chapter
		void mangaSeriesResume(Player& p);				// A on Resume
		bool mangaKey(Button b, Player& p);
		bool mangaPointer(const Pointer& pt, int x, int y, Player& p);
		std::string mangaTabLabel(int i) const;	// the kit tab's label ("Downloads 2" while some are queued)
		void drawMangaLibrary(const Player& p);
		void drawMangaHistory(const Player& p);
		void drawMangaBrowse(const Player& p);
		void drawMangaSeries(const Player& p);
		void drawMangaRead(const Player& p);
		void drawMangaCover(Rect r, const std::string& title, bool focus, int bookCell, const std::string& seriesId,
			const std::string& coverUrl = "",
			const std::vector<std::pair<std::string, std::string>>* coverHeaders = nullptr);
		// the series' real cover once it arrived, null until then (and for one that couldn't be read): the first draw
		// asks the worker once. Keyed by the series id
		SDL_Texture* coverOf(const std::string& seriesId);
		// a cover's state for a row's key ("c" the real one): asked as a draw asks, so it counts as drawn
		std::string coverReady(const std::string& seriesId, const std::string& coverUrl = "",
			const std::vector<std::pair<std::string, std::string>>* coverHeaders = nullptr);
		// a cover by its URL (a source's or a server's): the bytes in the cover cache, decoded by the worker, turned
		// into a texture on the main thread. Fetched only when the card is on screen; cancelled when it scrolls away.
		// A cover already in the cache is used with no fetch. headers: the source's image headers, or the server's
		// auth header via remoteAuth. coverKey is the texture-cache key (the URL; unique per image)
		// while a server's grid draws: its covers' addresses as a request needs them (Kavita's {key} filled in); the
		// cover's own address, its cache key, keeps the placeholder. Empty the rest of the time
		std::function<std::string(const std::string&)> coverFetchUrl;
		SDL_Texture* coverOfUrl(const std::string& coverUrl, int w, int h,
			const std::vector<std::pair<std::string, std::string>>& headers);
		struct CoverTex { SDL_Texture* tex = nullptr; uint64_t drawnAt = 0; };
		std::unordered_map<std::string, CoverTex> coverTex;	// at most COVER_KEEP, least recently drawn freed
		std::set<std::string> coverAsked, coverFailed;		// (a failed one is left alone until the app reopens)
		uint64_t coverDrawn = 0, coverSerial = 0;	// the LRU's order and what signature() follows
		static constexpr size_t COVER_KEEP = 80;
		// the cover fetches in flight: URL → fetch serial. A card scrolling away cancels its fetch; an answer that
		// arrives is put in the cover cache and handed to the worker for decode
		std::unordered_map<std::string, uint64_t> mangaCoverFetches;
		// the fetches the worker is decoding (asked but no result yet): so a draw doesn't ask twice
		std::set<std::string> coverUrlAsked;
		// cover URLs on screen this frame: anything in mangaCoverFetches but not here is cancelled at the end of the
		// draw (a card scrolled away)
		std::set<std::string> mangaCoverOnScreen;
		void mangaCoverCancelOffScreen();	// cancel fetches for URLs that scrolled away
		void mangaCoverCancelAll();			// the page left: every in-flight cover fetch goes
		bool mangaCoverOnFetched(MangaFetch::Result& r);	// true if the result was one of ours (a cover fetch)
		// the grids' columns (Library's is lay::coverGrid on the kit; a source's and a server's keep the old covers)
		static constexpr int MG_COLS = 5, MG_COVER_W = 112, MG_COVER_H = 150;	// the old grids' covers, 124 apart
		// a card series' keys (lay::keyRow on lay::seriesPage): Resume at the left, the heart at the right
		Rect mangaResumeRect() const { return mangaSeriesKeys({150, 0, 64})[0]; }
		Rect mangaHeartRect() const { return mangaSeriesKeys({150, 0, 64})[2]; }
		// the reading page's own pieces
		// (lay::mangaReadPage: the slider under the header, the finger's surface, the row of six)
		Rect mangaAreaRect() const { return lay::mangaReadPage().surface; }
		Rect mangaSliderTrack() const { return lay::mangaReadPage().track; }
		Rect mangaSliderHit() const { return lay::mangaReadPage().sliderHit; }
		void mangaSlide(int x, int y, Player& p);	// the slider grabbed (the reading page's, fullscreen's sheet's)
		void drawMangaSlider();
		// the row's keys (lay::MRK_*): what draw and the finger both ask; off: nothing to do there now
		Rect mangaReadKeyRect(int k) const { return lay::mangaReadPage().row[k]; }
		bool mangaReadKeyOff(int k) const;
		void mangaReadKeyAct(int k, Player& p);
		void drawMangaReadKeys(const Player& p);
		static constexpr int MG_ROWS = 6;	// the keys' page step on the old lists
		// the touch ids: their own space (mangaPointer acts on them; act() never sees them)
		static constexpr int MG_RESUME = 3080, MG_HEART = 3081, MG_READ_KEY = 3100;	// MG_READ_KEY + lay::MRK_*

		// ---- the manga reader's Sources page (ui_manga_sources.cpp) ----
		// Installed / Available / Repositories, three sub-lists of a page of its own (X on Browse): LEFT/RIGHT step
		// them, UP/DOWN the list. The repositories are the user's own URL indexes (design 5.3); MangaNet owns them and
		// the events their fetches leave (mangaSourcesTick turns them into toasts). None of it runs while the page is
		// closed.
		enum MangaSourcesTab { MST_INSTALLED, MST_AVAILABLE, MST_REPOSITORIES, MANGA_SOURCES_TABS };
		// (Installed is no tab of its own now: Available lists the installed on top; the segments are Available and
		// Repositories, L1/R1 between them)
		int mstTab = MST_AVAILABLE;
		MangaRow mangaSourcesRowAt(const Player& p, int k) const;	// Sources' row k of the list shown
		int mstCursor[MANGA_SOURCES_TABS] = {}, mstScroll[MANGA_SOURCES_TABS] = {};
		MList mlSources[MANGA_SOURCES_TABS]{{ui}, {ui}, {ui}};
		std::set<std::string> mstInstalling;		// full ids on their way (Installing… on the row), and Updating…
		std::map<std::string, std::string> mstInstallName;	// full id -> its name, for the answer's toast
		std::map<std::string, std::string> mstRepoError;		// repository URL -> its last failure
		std::set<std::string> mstAdding;			// typed in: its first index loaded says Added
		std::string mstPendingUrl;					// typed on the keyboard; mangaSourcesTick adds it
		Uint32 mstInstallAt = 0;					// when the first download started (a lost answer doesn't stick)
		// a row's sheet (A: details, X: options), a repository's (A: Refresh / Remove) and Remove's question
		enum MangaSourceModal { MSM_NONE, MSM_DETAILS, MSM_OPTIONS, MSM_REPO, MSM_REMOVE };
		int mstModal = MSM_NONE, mstModalCursor = 0;
		std::string mstModalId;						// a source's full id, or a repository's URL
		bool mstUninstallArmed = false;				// Uninstall pressed once: again within 2 s removes it
		Uint32 mstUninstallAt = 0;
		uint64_t mstLocal = 0;						// the page's own changes (an event taken, a row acted on)
		// ---- Java extensions in Sources and Browse (ui_manga_java.cpp) ----
		// A Java repository's extensions are listed after the Lua ones with a Java badge; a Java source is a Browse
		// row "java:<source id>" and its series "src:java:<source id>:<manga id>". Its requests go to the host's
		// GraphQL channel, never to the Lua worker.
		struct MangaSrcRow;	// (defined below, with the other source rows)
		struct MangaDlAsk;	// (defined below, with the Download sheet)
		uint64_t mstJavaLocal = 0;					// the Java rows' own changes, for signature()
		std::map<std::string, std::string> mstJavaBusy;	// package -> Installing… / Updating… / Removing…
		std::map<std::string, Uint32> mstJavaBusyAt;	// when each started (a lost answer doesn't stick)
		// a Java extension's sheet (A/X on an installed row) or a Java repository's (A on its row)
		enum MangaJavaModal { MJM_NONE, MJM_DETAILS, MJM_REPO };
		int mstJavaModal = MJM_NONE, mstJavaModalCursor = 0;
		std::string mstJavaModalId;					// a package name, or a repository URL
		bool mstJavaUninstallArmed = false;
		Uint32 mstJavaUninstallAt = 0;
		uint64_t mangaJavaListSerial = 0, mangaJavaSeriesSerial = 0, mangaJavaPagesSerial = 0;
		std::string mangaJavaBase;					// the host URL a request was built on (its port can change)
		Uint32 mangaJavaExtAt = 0;					// the last automatic fetchExtensions (not every tick)
		Uint32 mangaJavaDlAt = 0;					// the last scan of the download queue for Java work
		Uint32 mangaJavaKeepAt = 0;					// the last want() that kept an open Java page alive
		uint64_t mangaJavaManagerSerial() const;	// javaSources().serial(): the rows follow it (a source installed)
		bool mangaJavaInstalled() const;				// the add-on is on the card (Installed / Starting / Running)
		void mangaJavaNeedHost();					// want(), when the add-on is there
		std::string mangaJavaStatusLine() const;		// "Starting extensions… about 25 s", or a failure
		void mangaJavaAddRepo(const std::string& url, Player& p);
		void mangaJavaRefresh();						// fetchExtensions (Y, and the first Contacts with a store)
		void mangaJavaPoll(const Player& p);				// the manager's events and the host's answers
		void mangaNotesTick(Player& p);				// the queued toasts, one at a time
		int mangaJavaInstalledRows(const Player& p) const;
		int mangaJavaAvailableRows(const Player& p) const;
		int mangaJavaRepoRows() const;
		const MangaJava::Source* mangaJavaSourceFor(const std::string& fullId) const;
		void mangaJavaInstalledAct(int local, Player& p);	// A/X on an installed extension
		void mangaJavaAvailableAct(int local, Player& p);	// A: install
		void mangaJavaRepoAct(int local, Player& p);		// A: the repository's sheet
		MangaRow mangaJavaRowAt(int tab, int local, const Player& p) const;
		bool mangaJavaModalKey(Button b, Player& p);
		void mangaJavaModalAct(Player& p);
		void mangaJavaModalClose();
		bool mangaJavaModalPointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaJavaModal(const Player& p);
		int mangaJavaModalRows() const;
		lay::Sheet mangaJavaModalSheet() const;
		Rect mangaJavaModalBox() const;
		Rect mangaJavaModalRowRect(int i) const;
		// a Java source's page (ui_manga_browse.cpp's hooks)
		void mangaJavaSourceOpened(const MangaSrcRow& row, Player& p);
		void mangaJavaListLoad(int page);
		void mangaJavaSeriesLoad(Player& p, bool refresh = false);
		void mangaJavaPagesAsk(Player& p);
		void mangaJavaFiltersAsk();
		std::string mangaJavaFiltersJson() const;
		bool mangaJavaFilterAnswer(const JavaExtHost::GraphQlResult& r);
		bool mangaJavaListAnswer(const JavaExtHost::GraphQlResult& r);
		bool mangaJavaSeriesAnswer(const JavaExtHost::GraphQlResult& r);
		// A saved extension series whose id the engine doesn't know, or now gives to another series (the engine was
		// installed afresh: its ids start over): found again by its title at its source, once per opening, and its
		// heart, History and chapters' places moved to the new ids
		std::string mangaJavaRelinkFrom, mangaJavaRelinkTitle, mangaJavaLoadedFor;
		uint64_t mangaJavaRelinkSerial = 0;
		bool mangaJavaRelinkTried = false;
		bool mangaJavaRelinkStart(const std::string& why);
		bool mangaJavaRelinkAnswer(const JavaExtHost::GraphQlResult& r);
		void mangaJavaRelinkChapters();
		// the check of a series opened from the card's copy (title, source, chapter ids, from the engine's database)
		bool mangaJavaChecking = false;
		bool mangaJavaCheckPending = false;	// the open's check put off until a chapter is wanted (mangaSourceEngineWant)
		void mangaSourceEngineWant();
		uint64_t mangaJavaCheckSerial = 0;
		void mangaJavaCheckStart();
		bool mangaJavaCheckAnswer(const JavaExtHost::GraphQlResult& r);
		std::string mangaSourceWaitText() const;	// why an extension's chapter waits: the engine, or the check
		bool mangaJavaPagesAnswer(const JavaExtHost::GraphQlResult& r, const Player& p);
		uint64_t mangaJavaDlSubmit(const MangaDlAsk& a);	// a chapter's page list for the Download sheet
		bool mangaJavaDlAnswer(const JavaExtHost::GraphQlResult& r);
		void mangaJavaCancel();						// the source's Java requests dropped (its page or series left)
		void mangaJavaCancelList();					// just the list in flight
		uint64_t mangaReposSerial() const;			// repos().serial(): signature() follows it (ui.cpp has no MangaNet)
		uint64_t mangaJavaSerial() const;			// javaExt().serial(): the Java row's state and progress
		std::string mangaJavaValue() const;			// the Java row's right side (the host's state)
		void mangaJavaAct();						// A: Install, Cancel, Remove (twice), Try again
		void mangaSourcesOpen(Player& p);			// the page opened: the network side started, the indexes refreshed
		void openMangaSources(Player& p);			// X on Browse: the Sources page
		bool mangaSourcesPageKey(Button b, Player& p);	// its keys, B back to Browse
		void mangaSourcesTick(Player& p);			// the manager's events into toasts, a typed URL added
		bool mangaSourcesKey(Button b, Player& p);
		bool mangaSourcesPointer(const Pointer& pt, int x, int y, Player& p);
		bool mangaSourcesModal() const { return mstModal != MSM_NONE || mstJavaModal != MJM_NONE || mstAvMenu != 0; }
		// Available's X: a menu (Refresh, Languages) and the language list (a tick on each kept; Settings::mangaLangs)
		int mstAvMenu = 0, mstAvCursor = 0, mstAvScroll = 0;
		bool mstInstalledOnly = false;
		// the extensions' order Available was opened with (an install doesn't move its row until Available is opened
		// again: entering Sources, L1/R1 onto it, the Filter menu)
		std::unordered_map<std::string, int> mstAvOrder;
		void mangaAvFreeze(const Player& p);	// Filter › Installed only: Available lists only what is installed
			// 0 none, 1 the menu, 2 the languages
		std::vector<std::string> mstAvLangs;	// the languages the repositories offer, when the list was opened
		static constexpr int MST_AV_SHOWN = lay::LIST_ROWS;
		bool mangaLangShown(const Player& p, const std::string& lang) const;
		std::string mangaLangsSummary(const Player& p) const;
		void mangaAvMenuOpen(Player& p);
		bool mangaAvMenuKey(Button b, Player& p);
		void mangaAvMenuAct(Player& p);
		bool mangaAvMenuPointer(int x, int y, Player& p);
		void drawMangaAvMenu(const Player& p);
		void mangaAvSheetOpen();
		MoreSpec mangaAvSpec(const Player& p);
		lay::ListSheet mangaAvLangSheet() const;
		void mangaSourceSheetOpen();
		MoreSpec mangaSourceSpec();	// the source / repository modal is a More sheet over mstModal
		void mangaSourcesModalAct(Player& p);
		void mangaSourcesCloseModal();
		void mangaSourcesAct(Player& p);				// A (or a tap) on the tab's cursor's row
		void mangaSourcesRefresh(Player& p);			// Y (or the list pulled down): the repositories asked again
		void mangaSourcesInstall(const std::string& fullId, const std::string& repo, const std::string& id,
			const std::string& name, Player& p);
		void mangaSourcesUpdate(const std::string& fullId, const std::string& repo, const std::string& id,
			const std::string& name, Player& p);
		void mangaSourcesUninstall(const std::string& fullId, const std::string& name, Player& p);
		void mangaSourcesAddOpen(Player& p);			// Add repository…: the keyboard on https://
		void mangaSourcesUrlTyped(const std::string& url);	// the keyboard's answer (ui_forms.cpp's kbClose)
		std::vector<MangaRepos::InstalledSource> mangaSourcesInstalled() const;
		std::vector<MangaRepos::AvailableSource> mangaSourcesAvailable(const Player& p) const;
		std::vector<MangaRepos::RepoInfo> mangaSourcesRepos() const;
		int mangaSourcesRows(const Player& p) const;	// the tab's rows (Repositories: the Add row last)
		int mangaSourcesModalRows() const;
		std::string elideMiddle(const std::string& s, int size, int maxW) const;	// a URL: "...repository/index.json"
		void drawMangaSources(const Player& p);
		void drawMangaSourcesPage(const Player& p);	// its header ("Sources"), then the page
		static constexpr int MS_ROWS = 5;	// the keys' paging (the list is the kit's rows)
		// ---- browsing a source (ui_manga_browse.cpp) ----
		// Browse lists the saved servers and the installed sources after On this handheld; A opens one, with
		// Popular · Latest · Search (L/R) and a cover grid. A source's page is a view of Page::Manga and its series
		// page is Page::MangaSeries (the id says whose: "src:<full source id>:<key>"), because a Page of their own
		// would need its keys, taps and drawing dispatched in ui.cpp, ui_draw.cpp and ui_ext.cpp.
		struct MangaSrcRow { std::string id, name, lang, file; bool adult = false, java = false; };
		std::vector<MangaSrcRow> mangaSrcRows;	// every installed source, as repos() last listed them
		uint64_t mangaSrcRowsSerial = 0;		// repos()'s serial when they were read
		uint64_t mangaJavaRowsSerial = 0;		// javaSources()'s serial when the Java rows were read
		bool mangaSrcRowsOn = false;			// they were read at least once (a source list can be empty)
		int mangaView = 0;						// 0 the app's tabs, 1 a source's page, 2+ a saved server's pages
		std::string mangaSrcId, mangaSrcName, mangaSrcFile;	// the source open: full id, name, its script
		std::string mangaSrcQuery;
		// the source's own headers (its Referer and the like) for cover image fetches: filled from the worker's
		// answer for the first list, the same for every cover this source page asks for
		std::vector<std::pair<std::string, std::string>> mangaSrcCoverHeaders;
		// a source's grid's tabs, in the order they show (Latest only while the source has latest())
		static constexpr int SRC_POPULAR = 0, SRC_LATEST = 1, SRC_SEARCH = 2;
		int mangaSrcTab = 0;					// the source's tabs, in their order: Popular, Latest, Search
		bool mangaSrcHasLatest = true;			// a source without latest() loses its tab once the first ask says so
		std::vector<MangaItem> mangaSrcItems;
		// each tab's grid as it was left (source and tab; Search with its query): switching back shows it again, no
		// ask. A few sources' worth, then forgotten
		struct SrcTabState { std::vector<MangaItem> items; int page = 0, cursor = 0, scroll = 0; bool more = false; std::string query, filters; };
		void mangaSrcTabSave();
		bool mangaSrcTabRestore(int tab);
		std::string mangaSrcFiltersNow() const;
		std::map<std::string, SrcTabState> mangaSrcTabCache;
		int mangaSrcCursor = 0, mangaSrcScroll = 0, mangaSrcPage = 0;
		// a finger on the grid: where it went down, the scroll then, whether it has moved (a drag, not a tap)
		void mangaSourceMaybeMore();
		// a series page's chapter rows shown: six down to the hint bar
		bool mangaSourceEngineReady() const;	// an extension's series: the manga engine answering (Resume waits for it)
		// History's X: Clear last hour / today / last 24 hours / all (the places to resume stay)
		// the Library's X: Remove missing (series of the card whose files are on no card now), asked twice
		bool mlibMenu = false, mlibArmed = false;
		std::vector<std::string> mangaMissing() const;
		const LocalSeries* mangaSingleFile(const std::string& id) const;	// a local series that is one file, or null	// their ids
		void mangaLibSheetOpen();
		MoreSpec mangaLibSpec();	// the Library and History menus are More sheets over mlibMenu / mhisMenu
		void mangaLibMenuAct(Player& p);
		// two rows: Remove missing, Manga settings (START is Settings now: the app's settings are here and in History's)
		int mlibMenuCursor = 0;
		bool mhisMenu = false;
		int mhisMenuCursor = 0;
		static constexpr int MHIS_MENU_ROWS = 6;	// Selected item, Last hour, Today, Last 24 hours, All, Manga settings
		void mangaHisSheetOpen();
		MoreSpec mangaHisSpec();
		void mangaHisMenuAct(Player& p);
		std::string mangaSourceDownloadsId(const std::string& sourceSeriesId) const;
		std::string mangaSourceLabel(const std::string& seriesId) const;	// its source by name ("" unknown)	// "local:<folder>" or ""
		bool mangaSourceSeriesInLib() const;	// the series or its downloads folder in the Library
		int mserRows() const { return 5; }
		// the grid's foot: down to the hint bar
		bool mangaSrcMore = false, mangaSrcLoading = false;
		std::string mangaSrcError;				// the source's one-line message, while it shows
		int mangaSrcFailed = 0;					// what A asks again after an error: 0 none, 1 a list, 2 details, 3 a chapter
		// the jobs in flight: each answer is dropped unless its serial is still the one here (the page or tab changed)
		uint64_t mangaSrcListSerial = 0, mangaSrcDetailsSerial = 0, mangaSrcChaptersSerial = 0, mangaSrcPagesSerial = 0,
			mangaSrcImageSerial = 0;
		std::vector<MangaPage> mangaSrcPages;	// a chapter's pages while its {key} ones are resolved by image()
		int mangaSrcImageAt = 0;
		std::vector<std::string> mangaSrcUrls;
		std::vector<std::pair<std::string, std::string>> mangaSrcHeaders;
		// the chapter a Pages job is for; the read request is filled from these once its pages are known
		int mangaSrcPendAt = -1;
		std::vector<MangaChapter> mangaSrcPendChapters;
		std::string mangaSrcPendSeriesId, mangaSrcPendSeriesTitle;
		int mangaSrcPendPage = 0, mangaSrcPendMode = 0, mangaSrcPendFit = 0, mangaSrcPendHinge = 36;
		bool mangaSrcPendFull = false;
		// the source series shown (Page::MangaSeries): the item as the source gave it, its details and its chapters
		std::string mangaSrcKey, mangaSrcItemJson, mangaSrcCover, mangaSrcTitle;
		MangaDetails mangaSrcInfo;
		bool mangaSrcInfoOn = false;
		std::vector<MangaChapter> mangaSrcChapters;		// oldest first, as the card's series page lists them
		bool mangaSrcChaptersOn = false;
		// the page's cached list (design 6): opened from <state>/manga/series/<hash>.json, so its details and chapters
		// show at once and only Refresh asks the source. fetchedAt: when the list on screen was fetched (0: no cache).
		// hadList: the fetch began with a list on screen, so a failure keeps it and says so in a toast instead.
		int64_t mangaSrcFetchedAt = 0;
		bool mangaSrcRefreshing = false, mangaSrcHadList = false;
		bool mangaSrcFetchDetailsOk = false, mangaSrcFetchChaptersOk = false;	// this fetch's two halves
		std::vector<MangaChapter> mangaReadSrcChapters;	// the reading chapter's series, for the chapter buttons
		// a source's ids: the series' and a chapter's are "src:<full source id>:<key>" (docs/design/manga.md 6)
		static bool isSourceId(const std::string& id) { return id.rfind("src:", 0) == 0; }
		static std::string sourceItemId(const std::string& sourceId, const std::string& key)
		{
			return "src:" + sourceId + ":" + key;
		}
		// saved servers' ids are "srv:<server id>:<key>" as sources' are "src:" (a server's key is a Komga id, an
		// OPDS feed url or a chapter's url): they never collide with a local series or an installed source's
		static bool isServerId(const std::string& id) { return id.rfind("srv:", 0) == 0; }
		static std::string serverItemId(const std::string& serverId, const std::string& key)
		{
			return "srv:" + serverId + ":" + key;
		}
		static void splitServerId(const std::string& id, std::string& server, std::string& key)
		{
			server.clear();
			key.clear();
			if (!isServerId(id)) return;
			size_t colon = id.find(':', 4);
			if (colon == std::string::npos) { server = id.substr(4); return; }
			server = id.substr(4, colon - 4);
			key = id.substr(colon + 1);
		}
		// the two parts of "src:<source>:<key>"; full is "" when the id is malformed. A Java source's full id has
		// a colon of its own ("java:<source id>"), so its series split after it, not at the first colon.
		static void splitSourceId(const std::string& id, std::string& full, std::string& key)
		{
			full.clear();
			key.clear();
			if (!isSourceId(id)) return;
			size_t colon = id.compare(4, 5, "java:") == 0 ? id.find(':', 9) : id.find(':', 4);
			if (colon == std::string::npos) { full = id.substr(4); return; }
			full = id.substr(4, colon - 4);
			key = id.substr(colon + 1);
		}
		int mbrowseScroll = 0;							// Browse's rows (On this handheld and the sources)
		bool mangaFullSeenPending = false;				// the first fullscreen's flag waits for a key (no Player in a draw)
		void mangaBrowseReset(Player& p);				// the app opened or left: the source's jobs dropped
		void mangaBrowsePoll(const Player& p);			// the worker's answers (the pages' draws and keys call it)
		void mangaBrowseRepos(const Player& p);			// the installed sources, read again when repos() changed
		int mangaBrowseCount(const Player& p) const;	// On this handheld, the servers, the sources
		const MangaSrcRow* mangaBrowseSourceAt(int i, const Player& p) const;	// i: the sources the setting shows
		int mangaBrowseServerCount() const;				// the saved servers, as Browse lists them
		int mangaBrowseSourceStart() const { return mangaNetFoldersCount() + mangaBrowseServerCount() + 3; }	// past the network folders, servers, Sources… and Add a server…
		const MangaSrcRow* mangaSrcRowFor(const std::string& fullId) const;
		bool mangaBrowseKey(Button b, Player& p);
		bool mangaBrowsePointer(const Pointer& pt, int x, int y, Player& p);
		void mangaBrowseChoose(int i, Player& p);
		void drawMangaBrowseRows(const Player& p);
		void mangaOpenSource(const MangaSrcRow& row, Player& p);
		void mangaSourceBack(Player& p);
		bool mangaSourceKey(Button b, Player& p);
		bool mangaSourcePointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaSource(const Player& p);
		void mangaSourceTabStep(int dir, Player& p);
		void mangaSourceTabSet(int tab, Player& p);		// (the Search tab asks for the query)
		void mangaSourceLoad(int page);					// the tab's list: Popular / Latest / Search page 1, 2, …
		void mangaSourceSearch(const std::string& query);	// the keyboard's Done
		void mangaSourceRetry(Player& p);				// A after the source's error
		void mangaSourceOpenSeriesAt(int i, Player& p);
		void mangaOpenSourceSeries(const std::string& id, int from, const std::string& itemJson,
			const std::string& cover, const std::string& title, Player& p);
		bool mangaSourceSeriesOn() const;
		bool mangaSourceSeriesKey(Button b, Player& p);
		bool mangaSourceSeriesPointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaSourceSeries(const Player& p);
		void mangaSourceSeriesLoad(Player& p, bool refresh = false);
		void mangaSourceSeriesRefresh(Player& p);		// L, or the chapters pulled down: ask the source again; a failure keeps what shows
		void mangaSourceSeriesFetched();				// both halves are in: save, or keep the list and toast
		void mangaSourceSeriesFail(const std::string& why);	// before the jobs: what shows stays
		bool mangaSourceSeriesCacheLoad();				// the card's copy into the page; false: none to show
		void mangaSourceSeriesCacheSave();
		void mangaSourceSeriesChoose(int i, Player& p);
		void mangaSourceSeriesResume(Player& p);
		void mangaSourceSeriesInLibrary(Player& p);
		void mangaSourceChapterOpen(const std::vector<MangaChapter>& chapters, int i, int page,
			const std::string& seriesId, const std::string& seriesTitle, Player& p);
		void mangaSourcePagesAsk(Player& p);			// the chapter's Pages job
		void mangaSourceImageAsk();					// the next page's Image job, while one has no url
		void mangaSourceCancel();						// the source's jobs dropped (its page or series left)
		void mangaSourceChapterReady(const Player& p);	// its pages are known: the read request, the reader page
		void mangaSourceChapterStep(int dir, Player& p);
		int mangaReadChapterTotal() const;
		std::string mangaReadChapterName(int index) const;	// that chapter's title ("": not known)
		std::string mangaNetSignature() const;			// this part of the page for signature()
		// ---- the network folders (ui_manga_netfolders.cpp) ----
		// Browse lists the saved connections; A on one lists its folders through the worker. A folder laid out as a
		// series (chapter folders of pictures, or CBZ/ZIP archives) opens as its series page; reading a chapter copies
		// it into the page cache's own folder first, then opens that with MangaReadRequest. The series and the
		// progress are keyed by their bpnet:// paths, never by a credential.
		enum MangaNetView { MNF_OFF, MNF_DIRS, MNF_SERIES };
		struct MangaNetRow
		{
			std::string name, path;
			bool dir = false;
			bool archive = false;
		};
		int mangaNetView = MNF_OFF;
		int mangaNetCursor = 0, mangaNetScroll = 0;
		std::string mangaNetServerId, mangaNetStart, mangaNetDir;
		std::string mangaNetBackDir;	// the folder a series was opened from (B returns there; "" the connection's own)
		std::vector<MangaNetRow> mangaNetRows;
		std::string mangaNetError;
		bool mangaNetLoading = false;
		uint64_t mangaNetSerial = 0;			// the listing or scan in flight
		std::vector<NetMangaChapter> mangaNetChapters;
		std::string mangaNetSeriesPath, mangaNetSeriesTitle;
		uint64_t mangaNetFetchSerial = 0;		// a chapter's copy in flight
		int mangaNetPendAt = -1, mangaNetPendPage = 0, mangaNetPendMode = 0, mangaNetPendFit = 0, mangaNetPendHinge = 36;
		bool mangaNetPendFull = false;
		std::vector<NetServer> mangaNetFoldersServers() const;	// the saved connections, most recently used first
		int mangaNetFoldersCount() const;
		void mangaNetFoldersReset(Player& p);
		void mangaNetFoldersOpen(const std::string& serverId, Player& p);
		void mangaNetFoldersLeave();
		void mangaNetList(const std::string& dir);
		void mangaNetScan(const std::string& path);
		void mangaNetSeriesOpen(const NetMangaSeries& s);
		void mangaNetChapterOpen(int i, int page0, Player& p);
		void mangaNetFetchStart(const NetMangaChapter& c);
		void mangaNetChapterReady(const std::string& localPath, const Player& p);
		std::string mangaNetCachePath(const NetMangaChapter& c) const;
		bool mangaNetCached(const std::string& localPath, bool archive) const;
		void mangaNetCancel();
		bool mangaNetFoldersKey(Button b, Player& p);
		bool mangaNetFoldersPointer(const Pointer& pt, int x, int y, Player& p);
		void mangaNetFoldersTap(int i, Player& p);	// a tap on row i: what A does there
		bool mangaNetFoldersResult(MangaWorker::Result& r, const Player& p);
		// ---- a source's filters (ui_manga_filters.cpp) ----
		// filters() is asked once a run per source; SELECT opens its sheet, Apply runs search("", 1, values) on the
		// Search tab. A source with none (or one whose list failed) shows no hint and no sheet.
		std::map<std::string, MangaFilterSession> mangaFilters;	// full source id -> its values and sheet
		uint64_t mangaFilterLocal = 0;							// any filter change (signature() follows it)
		void mangaFilterAsk();									// its filters(), once a run
		bool mangaFilterAvailable() const;						// it declared some: SELECT opens the sheet
		bool mangaFilterShown() const;							// the sheet takes the keys and taps
		MangaFilterSession* mangaFilterSession();
		const MangaFilterSession* mangaFilterSession() const;
		void mangaFilterShow(Player& p);
		void mangaFilterClose();
		bool mangaFilterKey(Button b, Player& p);
		bool mangaFilterPointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaFilters(const Player& p);
		std::string mangaSourceFiltersJson() const;				// what a Search job sends ("{}" without filters)
		void mangaFilterTextTyped(const std::string& text);		// the keyboard's Done
		static constexpr int MF_ROWS = lay::LIST_ROWS;
		// the sheet for a list of n rows (the kit's list sheet: its rows are the touches too)
		lay::ListSheet mangaFilterSheet(int n) const { return lay::listSheet(std::min(std::max(n, 1), MF_ROWS)); }
		int mserSrcRows() const { return 5; }	// (the keys' paging; the list is the kit's rows)
		static constexpr int MG_SER_ROWS = 5;	// a card series' chapter rows the body holds (lay::seriesPage, lay::rowList)
		// a source series' row of four: Resume, Get, Info, Library (mserBtn 0..3). screens.js seriesB: Resume 150, Info
		// and the heart 64 each at the right; Get as wide as Resume, the room between them left open (checking for
		// new chapters is a pull on the list and L, not a key here: its label was cut at the handheld's size)
		static constexpr int MG_SRC_KEYS = 4;
		Rect mangaSrcBtn(int i) const
		{
			static const int BOX[MG_SRC_KEYS] = {0, 1, 3, 4};	// (box 2 is the open room)
			return mangaSeriesKeys({150, 150, 0, 64, 64})[BOX[std::clamp(i, 0, MG_SRC_KEYS - 1)]];
		}
		Rect mangaSrcResumeRect() const { return mangaSrcBtn(0); }
		Rect mangaSrcInfoRect() const { return mangaSrcBtn(2); }
		Rect mangaSrcHeartRect() const { return mangaSrcBtn(3); }
		// Info: the series' details (its description) in a dialog, off the page (it took two lines above the row)
		void mangaSrcInfoOpen();	// the series' details: Info's page (Ui::openInfoGroups)

		// ---- the saved servers (ui_manga_servers.cpp) ----
		// Browse › Y opens the Add server form: kind, name, address, user, password; Test, Save. A server's
		// libraries or catalogue, then its series, are a cover grid here, and a series is Page::MangaSeries with an
		// id "srv:<server id>:<key>" so the reader and the History find their way back. The catalogue and the series
		// page are views of Page::Manga (mangaView 2 and up), as a source's page is.
		static constexpr int MGSRV_NONE = 0, MGSRV_CATALOGUE = 1, MGSRV_SERIES = 2, MGSRV_FORM = 3;
		static constexpr int MGSRV_LOAD_LIBRARIES = 1, MGSRV_LOAD_SERIES = 2, MGSRV_LOAD_FEED = 3;
		static constexpr int MGSRV_FROM_LIST = 0, MGSRV_FROM_FOLDER = 1, MGSRV_FROM_HISTORY = 2;
		int mangaSrvView = MGSRV_NONE;				// what the server's own page shows (mangaView is 2 while it does)
		std::string mangaSrvId, mangaSrvName, mangaSrvKind;	// the server open
		std::string mangaSrvRootUrl;				// an OPDS server's start feed (Kavita's key is in it)
		std::string mangaSrvHere;					// the heading: the server, or the folder open
		std::vector<std::string> mangaSrvPath;		// the folders opened: Komga library ids, OPDS feed urls
		std::vector<RemoteItem> mangaSrvItems;		// the libraries, series or feed entries in view
		int mangaSrvCursor = 0, mangaSrvScroll = 0, mangaSrvPage = 0;
		bool mangaSrvMore = false, mangaSrvLoading = false, mangaSrvAppend = false;
		std::string mangaSrvNext;					// a feed's own next link, when it names one
		std::string mangaSrvError;
		int mangaSrvLoad = 0;						// what the request in flight is: MGSRV_LOAD_*
		std::string mangaSrvLoadUrl;				// the url asked for (a feed's relative links resolve on it)
		uint64_t mangaSrvFetchSerial = 0;
		// a server's series: its chapters as the server gave them (oldest first for Komga, the feed's order for OPDS)
		std::string mangaSrvSeriesId, mangaSrvSeriesKey, mangaSrvSeriesTitle;
		std::vector<RemoteChapter> mangaSrvChapters;
		bool mangaSrvChaptersOn = false;
		int mangaSrvSerCursor = 0, mangaSrvSerScroll = 0, mangaSrvSerPage = 0, mangaSrvSeriesFrom = MGSRV_FROM_LIST;
		bool mangaSrvSerMore = false, mangaSrvSerLoading = false, mangaSrvSerAppend = false;
		std::string mangaSrvSerError;
		uint64_t mangaSrvSerSerial = 0;
		// the form (Add server, Edit): the four typed fields, the kind and what Test answered
		std::string mangaSrvFormId;					// "" adding; else the server edited
		std::string mangaSrvFormKind = "komga", mangaSrvFormName, mangaSrvFormUrl = "https://", mangaSrvFormUser,
			mangaSrvFormPassword;
		bool mangaSrvFormPasswordTyped = false;		// a new password typed: it is sealed on Save, never shown again
		int mangaSrvFormCursor = 0;
		std::string mangaSrvFormNote, mangaSrvTestText;
		uint64_t mangaSrvTestSerial = 0;
		// X on a server row: Edit / Delete, the delete asked twice (as the other clears)
		bool mangaSrvMenu = false;
		std::string mangaSrvMenuId;
		// On this handheld's list: Y, where to put manga (MLS_HELP); X, a series' Open / Delete, the delete asked twice
		// entering Manga without the manga engine: Download it / Not now (asked again at the next entry)
		bool mengSheet = false;
		int mengCursor = 0;
		bool mengBusy = false;
		bool mengUpgrade = false;	// the sheet offers the lighter runtime (an install on HotSpot), not the engine itself	// Download chosen: the sheet stays with its progress until it's done; Cancel only
		Rect mangaEngineBox() const;
		Rect mangaEngineRow(int i) const;
		std::vector<std::string> mangaEngineLines(bool& failed) const;
		lay::BusySheet mangaEngineBusy() const;
		lay::ConfirmSheet mangaEngineConfirm() const;
		bool mangaEngineKey(Button b, Player& p);
		void mangaEngineChoose(int i, Player& p);
		void mangaEngineWatch(Player& p);
		bool mangaEngineMissing() const;	// not on the card and not downloading: the offer and its row show
		void mangaEngineOffer();			// the offer, again (the Repositories row)
		std::string mangaEngineSignature() const;	// while downloading: its state and MB so far	// the download done (the sheet closes) or failed (back to its offer)
		void drawMangaEngineSheet();
		enum { MLS_NONE, MLS_HELP, MLS_MENU };
		int mlocSheet = MLS_NONE, mlocCursor = 0;
		bool mlocDeleteArmed = false;
		Uint32 mlocDeleteAt = 0;
		void mangaLocalSheetOpen();
		MoreSpec mangaLocalSpec();	// the series' menu: a More sheet over mlocSheet; the help is Info's page
		void mangaLocalMenuAct(int row, Player& p);
		void mangaLocalHelpOpen();
		std::string mangaLocalFolder() const;	// the Manga folder to name: the first on the card, else the downloads'
		void mangaLocalDeleteNow(Player& p);
		int mangaSrvMenuCursor = 0;
		bool mangaSrvDeleteArmed = false;
		Uint32 mangaSrvDeleteAt = 0;
		uint64_t mangaSrvLocal = 0;					// the pages' own changes (a field typed, a row acted on)
		void mangaServersReset();					// the app opened or left: every job dropped and the pages closed
		void mangaServersPoll();					// the fetcher's answers (the draws and keys call it)
		bool mangaServersKey(Button b, Player& p);
		bool mangaServersPointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaServers(const Player& p);
		void mangaServerOpen(const std::string& id, Player& p);
		void mangaServerBack(Player& p);
		void mangaServerListLoad(int load);			// the libraries, a library's series or a feed
		void mangaServerFeedLoad(const std::string& url);	// a feed's next page, or a folder just opened
		void mangaServerMore();						// the next page of what shows
		void mangaServerReload();					// A after the server's error: the same list again
		void mangaServerItemOpen(int i, Player& p);
		void mangaServerSeriesOpen(const std::string& key, const std::string& title, int from);
		void mangaServerSeriesOpenFromId(const std::string& id, Player& p);	// the History's way in
		void mangaServerSeriesLoad();
		void mangaServerSeriesReload();				// A after the chapters' error
		void mangaServerSeriesMore();
		void mangaServerSeriesBack(Player& p);
		bool mangaServerSeriesKey(Button b, Player& p);
		bool mangaServerSeriesPointer(const Pointer& pt, int x, int y, Player& p);
		void drawMangaServerSeries(const Player& p);
		void mangaServerSeriesChoose(int i, Player& p);
		void mangaServerSeriesResume(Player& p);
		void mangaServerChapterOpen(int i, Player& p);
		void mangaServerChapterStep(int dir, Player& p);
		int mangaServerChapterIndex() const;
		void mangaServerAddOpen(Player& p);
		void mangaServerEditOpen(const std::string& id, Player& p);
		bool mangaServerFormKey(Button b, Player& p);
		void drawMangaServerForm(const Player& p);
		void mangaServerFormChoose(Player& p);
		void mangaServerFormSave(Player& p);
		void mangaServerTest();
		void mangaServerFieldTyped(int field, const std::string& text);	// ui_forms.cpp's kbClose
		void mangaServerCancel();
		void mangaServerModalOpen(int row, Player& p);
		bool mangaServerModalKey(Button b, Player& p);
		MoreSpec mangaServerSpec();	// the Server menu is a More sheet over the flag mangaSrvMenu
		std::string mangaServersSignature() const;
		// a server's series page keeps its own Resume button: the cards' pages and a source's row differ (the source's
		// shares its row with Download, Refresh and the heart)
		Rect mangaServerResumeRect() const { return mangaSeriesKeys({150, 0})[0]; }
		static constexpr int MGSRV_FORM_ROWS = 7;	// Kind, Name, Address, User, Password, Test, Save
		static constexpr int MGSRV_GRID_ROWS_SHOWN = 2;	// (lay::coverGrid's whole rows in the body)
		// the server form on the kit, in the shell's body: Kind…Password as form rows (lay::formRow), Test and Save the
		// two keys at its foot (lay::keyRow), the note over them
		Rect mangaSrvFormRowRect(int i) const
		{
			lay::Box in = lay::seriesPage(lay::shell().body, false).inner;
			if (i < 5) return {in.x, in.y + i * (lay::FORM_ROW + lay::SP2), in.w, lay::FORM_ROW};
			auto keys = lay::keyRow({in.x, in.y + in.h - lay::KEY_ROW_H - lay::SP8, in.w, lay::KEY_ROW_H}, {0, 0});
			return keys[i - 5];
		}
		// On this handheld's series menu: Open, Info, Delete
		// ---- the dashboard's chapters for a source series (api_manga.cpp, ui_manga_web.cpp) ----
		// The worker's answer is kept by series: the page polls while it comes (202), then reads it at once. The
		// read marks and download states are filled in on every read, since the queue moves underneath the list.
		struct MangaWebChaptersState
		{
			std::vector<MangaChapter> chapters;
			std::string full, key, itemJson, title, source, error;
			uint64_t serial = 0;
			Uint32 askedAt = 0;
			bool have = false;
		};
		std::map<std::string, MangaWebChaptersState> mangaWebCache;
		bool mangaWebWorker(MangaWorker::Result& r);	// true: the answer was one of the dashboard's lists
		// ---- the manga reader's Downloads (ui_manga_downloads.cpp) ----
		// Page::Manga's Downloads tab: the queue, then what the downloads put on the card; and the Download sheet a
		// source's series page opens (R2, or its button): This chapter / Next 5 unread / All unread / Choose chapters.
		// A selected chapter's page list is asked of the source's worker first (Pages, then Image while a page has
		// only a key), so the downloader never takes an item without pages. Only archives carrying our ComicInfo
		// (BPSource / BPSeriesKey) count as downloads: the same Manga folder also holds the card's own series.
		struct MangaDlCardChapter
		{
			std::string title, path;	// the .cbz, its title as ComicInfo has it (else its name)
			std::string sourceId, seriesKey;	// what the download wrote (ComicInfo BPSource / BPSeriesKey)
			std::string chapterId;	// its BPChapter: the source chapter's id ("" in an older download)
			int pages = 0;
			uint64_t bytes = 0;
			double number = -1;	// ComicInfo's Number, when it is one
		};
		struct MangaDlCardSeries
		{
			std::string name, path;	// the folder's name and path
			std::vector<MangaDlCardChapter> chapters;
			uint64_t bytes = 0;
		};
		// a chapter's download state, as a series page's rows and the Choose list mark it
		struct MangaDlMark
		{
			enum Kind { None, Queued, Running, Paused, Failed, OnCard } kind = None;
			int done = 0, total = 0;
		};
		// one chapter waiting for its page list: the worker asks the source, then the queue gets the item
		struct MangaDlAsk
		{
			std::string sourceId, scriptPath, seriesId, seriesTitle, chapterId, chapterTitle, chapterJson;
		};
		enum MangaDlSheet { MDLS_NONE, MDLS_MENU, MDLS_CHOOSE };
		mutable std::vector<MangaDlCardSeries> mdlCard;	// the card's downloads, scanned when the page or tab needs them
		// the source chapter lists an older download (no BPChapter) is matched in to link its place to the source's
		// (issue #15): read once per series from its cache file, the series page's own list when it is open
		std::unordered_map<std::string, std::vector<MangaChapter>> mdlLinkLists;
		void mangaDlLinkCard();	// every chapter on the card linked to its source chapter in the store
		uint64_t mdlCardStamp = 0;	// what the last scan found, so an unchanged card doesn't redraw every frame
		Uint32 mdlScanAt = 0;		// the scan is slow enough to be worth throttling
		bool mdlCardOn = false;
		bool mdlCardStale = false;	// chapter files the card's read waited for arrived: read again at once
		std::unordered_set<std::string> mdlCardAsked;	// files (path, size, mtime) asked of the library worker once
		uint64_t mdlLocal = 0;		// the tab's own changes (a scan, a delete), for signature()
		int mdlCursor = 0, mdlScroll = 0;	// the queue, then Pause all, then the card's series
		bool mdlCardOpen = false;	// A on a card series: its chapters
		int mdlCardAt = -1;			// the card series open (its index; the queue's length may change under it)
		int mdlCardCursor = 0, mdlCardScroll = 0;
		std::string mdlDeletePath;	// X once: the series folder or chapter armed; again within 2 s deletes
		Uint32 mdlDeleteAt = 0;
		std::string mdlRemoveId;	// Y once: a failed or queued item armed; again within 2 s removes it
		Uint32 mdlRemoveAt = 0;
		std::string mdlNote;		// a page list that didn't come, or what the card couldn't do
		Uint32 mdlNoteUntil = 0;
		// the Download sheet over a source's series page
		int mdlSheet = MDLS_NONE, mdlSheetCursor = 0;
		int mdlChooseCursor = 0, mdlChooseScroll = 0;
		std::set<int> mdlChosen;	// chapter indices ticked in Choose chapters
		std::vector<MangaDlAsk> mdlAsks;	// chapters waiting for their page list, in the order they were picked
		MangaDlAsk mdlAsk;			// the one in the worker now
		bool mdlAskOn = false;
		uint64_t mdlPagesSerial = 0, mdlImageSerial = 0;
		std::vector<MangaPage> mdlAskPages;
		std::vector<std::string> mdlAskUrls;
		int mdlAskImageAt = 0;
		void mangaDownloadsOpen(Player& p);		// the tab opened: its lists read, finished queue items cleared
		void mangaDownloadsReset();				// the app opened: an ask the last session left waits for the worker again
		void mangaDownloadsTick(Player& p);		// the armed delete and the note time out; a lost worker answer resumes
		// the spec's keys (screens.js manga-downloads): Y pauses or resumes the row (or all), X is More (Remove from the
		// queue, Pause all / Resume all, Clear failed; on a series: Delete from the card, asked); a series' chapters
		// keep theirs. mangaDlKeyBase is the list's own handling, by its older keys (X pause, Y remove)
		bool mangaDownloadsKey(Button b, Player& p);
		bool mangaDlKeyBase(Button b, Player& p);
		void mangaDlMore(Player& p);
		void mangaSourceMore(Player& p);	// a source page's X: Search this source, Load this list again
		bool mangaDownloadsPointer(const Pointer& pt, int x, int y, Player& p);
		bool mangaDownloadsBack();	// B: out of a card series' chapters (true when it went somewhere)
		void drawMangaDownloads(const Player& p);
		void drawMangaDlCardSeries(const Player& p);
		void mangaDlEnsureCard(bool force = false);	// the downloads folder read (at most every 1.5 s)
		uint64_t mangaDlCardBytes() const;
		uint64_t mangaDlFreeBytes() const;			// statvfs of the downloads folder, or its nearest parent
		int mangaDlQueueCount() const;				// items still waiting, running, paused or failed
		int mangaDlFailedCount() const;				// items whose download failed (Clear failed offers itself)
		const MangaDlCardChapter* mangaDlCardFor(const std::string& sourceId, const std::string& seriesId,
			const MangaChapter& c) const;
		MangaDlMark mangaDlMarkFor(const std::string& sourceId, const std::string& seriesId, const MangaChapter& c) const;
		void drawMangaDlMark(const MangaDlMark& m, int right, int cy, bool at);
		void mangaDlOpenCbz(const std::string& seriesId, const std::string& seriesTitle, const std::string& chapterId,
			const std::string& chapterTitle, const std::string& path, int page0, Player& p);
		bool mangaDlOpenSourceChapter(const std::vector<MangaChapter>& chapters, int i, int page0,
			const std::string& seriesId, const std::string& seriesTitle, Player& p);	// true: it was on the card
		void mangaDlOpenCardAt(int seriesIndex, int chapterIndex, Player& p);
		bool mangaDlBlocked(const std::string& sourceId, const std::string& seriesId, const MangaChapter& c) const;
		void mangaDlEnqueueChapter(const std::string& sourceId, const std::string& scriptPath, const std::string& seriesId,
			const std::string& seriesTitle, const MangaChapter& c);
		void mangaDlAskPump();		// the next chapter's Pages job
		bool mangaDlImageAsk();		// the next page's Image job, while one has only a key
		void mangaDlYieldToSource();	// a chapter being read needs the worker first: the ask waits its turn
		void mangaDlAskFinish(bool queued, const std::string& note);	// the chapter's page list is done (or didn't come)
		void mangaDlWorker(MangaWorker::Result& r);	// its answers (the rest of mangaBrowsePoll's are the browse page's)
		std::vector<int> mangaDlUnreadFrom(int from, int limit) const;	// unread source chapters, in list order
		// the Download sheet
		Rect mangaDlButtonRect() const { return mangaSrcBtn(1); }
		void mangaDlMenuOpen();
		MoreSpec mangaDlMenuSpec();	// the Download menu: a More sheet over mdlSheet == MDLS_MENU
		lay::ListSheet mangaDlChooseSheet() const;	// Choose chapters: a List sheet the page draws
		void mangaDlSheetOpen(Player& p);
		void mangaDlSheetClose();
		bool mangaDlSheetKey(Button b, Player& p);
		bool mangaDlSheetPointer(const Pointer& pt, int x, int y, Player& p);
		void mangaDlSheetAct(Player& p);
		void drawMangaDlSheet(const Player& p);
		std::string mangaDlSignature() const;		// this part of the page for signature()
		// the Downloads tab's rows: the queue (up to 3), Pause all, then the card's series (up to 2). With nothing
		// queued the queue's room goes to the card: one dim line, then the series from MDL_CARD_TOP_C down to the
		// hint line, as many as fit
		// the keys' paging of the queue and the card's series (the list itself is the kit's: lay::rowList, five rows)
		static constexpr int MDL_QSHOWN = 3;
		int mdlCardRows() const { return 5; }

		// ---- the manga reader's settings (ui_manga_settings.cpp) ----
		// A page of the app's own, not a group of Banana Player's Settings: START on the app's pages (any tab) or over
		// the reader opens it, and so does Manage extensions' X / gear. Its rows are the same ids as Settings' (drawn
		// and stepped as its rows are: drawSettingRow, changeSetting); they scroll as its lists do.
		// mangaSettingsFrom: the page B goes back to (the reader stays open behind it)
		Page mangaSettingsFrom = Page::Manga;
		int mangaSettingsTab = MT_LIBRARY;	// the app's tab it came from (B goes back to it)
		bool mangaSettingsList = false;		// Browse's series list was open (B goes back to it too)
		void openMangaSettings();	// from where it is now (mangaSettingsFrom)
		void closeMangaSettings();
		bool mangaSettingsKey(Button b, Player& p);
		void drawMangaSettings(const Player& p);
		std::vector<int> mangaSettingRows() const;	// the page's rows, by id (the note last)
	};
}
