// The top panel's page reader, the bottom panel's side of it: what Files' PDF/comic viewer (Ui::Docs, ui_docs.h) and
// the Manga reader's page (Page::MangaRead) both use (docs/notes/ui-areas.md, "PageReader"). Main's MangaReader shows
// the pages on the top panel; this is its channel (the request, the commands, the status, fullscreen's and the
// grid's pixels) and the views both bottom panels share: the pages grid, fullscreen's drawing, the zoom, the seek,
// Try again, and the dragged lists. Included by ui_reader.cpp, main.cpp (the channel) and the files of Ui that read
// or drive the reader.
#pragma once
#include "ui.h"
#include "ui_smoothlist.h"

namespace bp
{
	// Not an Area: it has no page of its own (Page::DocPages is Docs', Page::MangaRead the Manga reader's). It is a
	// plain struct nested in Ui and owned by it (Ui::pageReader), so that both of those areas use one reader channel
	// and one set of views instead of two copies; they call it through `ui.pageReader->`.
	struct Ui::PageReader
	{
		Ui& ui;
		explicit PageReader(Ui& u) : ui(u) {}

		// ---- the channel: Ui (bottom panel) and main's reader (top panel) talk only through these ----
		// a chapter to open on the top panel (Ui sets it and shows Page::MangaRead; main takes it once and opens it).
		// path: a folder of pictures or a CBZ; page: where to start (0-based); fit: MANGA_FIT_*
		struct MangaReadRequest
		{
			std::string seriesId, seriesTitle, chapterId, chapterTitle, path;
			bool archive = false;
			int page = 0, mode = 0, fit = 0;
			int book = 0;	// MANGA_BOOK_*: the series' own (takeMangaRead fills it)
			float within = 0;	// the place inside `page` kept for this chapter (takeMangaRead fills it)
			// a chapter from a source or a server: its pages' URLs, in order, and the headers they need (the source's
			// Referer...); path is then the chapter's id (the cache's key, never opened). Empty: path is a folder or a CBZ
			std::vector<std::string> pageUrls;
			std::vector<std::pair<std::string, std::string>> pageHeaders;
			bool comicDirection = false;	// the series has no direction of the user's: a CBZ's ComicInfo may set right-to-left
		};
		MangaReadRequest mangaRead;	// (takeMangaRead)
		bool takeMangaRead(MangaReadRequest& out);
		// what the bottom panel asks of the reader, in order (main applies them each pass): Close; Step (v: screens, + down);
		// Page (v: +1 / -1, a whole page); Seek (v: the page index); TouchDown / Drag (v: the finger's dy as a fraction of
		// the panel's height, + down: the panels are the same size, so main scales it to the top panel) / Release, each
		// with the time in ms (Drag's v2: the finger's dx, as a fraction of the panel's width, + right); Mode (v: MANGA_RTL,
		// MANGA_LTR, MANGA_STRIP: the page under the view kept); Fit (v: MANGA_FIT_*: the page under the view kept)
		// Fullscreen (v: 1 on, 0 off; v2: the hinge gap in 480-line units: both panels are one tall view, the gap skipped)
		// Zoom (v: the factor, 1 = the fit, 1.5 … 8: the content under the view's middle stays there, and the pages in
		// view are decoded again at that width once the presses rest)
		// Rotate (paged modes only: each press turns the page 90° clockwise, 0 → 90 → 180 → 270 → 0; Strip ignores it;
		// kept per chapter, back to 0 on another chapter; the fit and zoom work on the turned page)
		// ZoomPoint (v: the factor; v2, v3: the tap point as fractions of the view width and height: zoom to that point
		// / back to the fit, as a double-tap on the page area does)
		// Book (v: MANGA_BOOK_*: held sideways, a page upright on each panel; Step turns a spread, Page moves one)
		// Stick (v: the left stick's LEFTY, -32768 up … 32767 down): the Strip scrolls, a paged page with room pans
		struct ReaderCmd { enum Kind { Close, Step, Page, Seek, TouchDown, Drag, Release, Mode, Fit, Fullscreen, Zoom, Rotate, ZoomPoint, Retry, Book, Pan, Stick, Thumbs } kind = Close; double v = 0; int64_t ms = 0; double v2 = 0; double v3 = 0; };
		std::vector<ReaderCmd> readerCmds;
		// what the reader shows, as main last set it (the bottom panel draws from it; it goes into signature())
		struct ReaderStatus
		{
			bool open = false, loading = false, atEnd = false, atStart = false;	// atEnd: the strip's bottom reached (the next chapter's card)
			int page = 0, pages = 0;	// the page under the view's middle (0-based), the chapter's page count
			float within = 0;	// Vertical scroll: how far through that page the view's middle is
			int mode = 0;	// the mode it reads in (MANGA_*)
			int modeSet = 0;	// the Reading mode chosen (differs from mode only in Book: a Vertical scroll read in pages)
			int zoomPct = 100;	// the zoom (1 = the fit, 100 % … 800 %)
			bool fullscreen = false;	// both panels show the pages
			int rotation = 0;	// the page turn in degrees (0, 90, 180, 270; paged modes only, 0 in Strip)
			int book = 0;	// MANGA_BOOK_*: held sideways, a page on each panel (page: the spread's first)
			int bookCount = 0;	// Book: the pages the screen shows (1: a lone page, or a wide page across both)
			std::string error;	// the chapter couldn't be opened ("" while fine)
			bool failedHere = false;	// a page in view failed (fetch, decode or no GPU memory): Try again shows
		};
		ReaderStatus readerStatus;
		// fullscreen: what the bottom panel shows of the view (main sets it each pass): each page's box as fractions
		// of the panel (x, y, w, h; y may run past 0..1), and the pages' pixels (RGBA, straight alpha) for those boxes;
		// gen changes with the chapter (the bottom panel's textures of the last one go)
		struct ReaderBox { int page; float x, y, w, h; bool wait = false; };
		struct ReaderPix { int page = -1, w = 0, h = 0; std::shared_ptr<const std::vector<uint8_t>> rgba; };
		struct ReaderBottom { bool on = false; uint64_t gen = 0; std::vector<ReaderBox> boxes; std::vector<ReaderPix> pages; };
		ReaderBottom readerBottom;
		// a page loading on this panel: the ring's step, so the signature changes as it turns (and only then)
		std::string spinKey() const
		{
			for (auto& b : readerBottom.boxes)
				if (b.wait) return "s" + std::to_string((int)(SDL_GetTicks() / 110 % 8));
			return "-";
		}
		// the pages grid's thumbnails (docs/design/manga.md section 3): one per decoded page, ~96 px wide RGBA,
		// made by the reader as each page decodes; pages not decoded yet are absent (the Ui draws a numbered
		// placeholder). gen changes with the chapter: the Ui's textures of the last one go
		struct ReaderThumb { int page = -1, w = 0, h = 0; std::shared_ptr<const std::vector<uint8_t>> rgba; };
		struct ReaderThumbs { bool on = false; uint64_t gen = 0; std::vector<ReaderThumb> pages; };
		ReaderThumbs readerThumbs;
		// the reader closed itself (an error, or main stopped it): the bottom panel leaves its page
		bool readerOpen() const { return readerStatus.open; }

		// ---- what the top reader shows (the Manga reader sets it from its store; Docs from the document) ----
		// the chapter open (Page::MangaRead's; Docs clears the ids and names the document)
		std::string mangaReadSeriesId, mangaReadChapterId, mangaReadChapterTitle;
		// the page the slider (or the grid) last asked for: a scrub sends each page once
		int mangaSliderPage = -1;
		void mangaSeek(int page);		// the slider: a Seek command
		// the zoom the keys last asked for: key repeats polled in one pass must walk the ladder, not resend a step
		int mangaZoomAsked = 100;
		void mangaZoomStep(int dir);	// UP/DOWN, and the Zoom row: one of the photo viewer's steps
		void mangaZoomToggle();			// A: 200 % or back to the fit
		void mangaZoomTo(double z);
		// Try again, in the middle of the page area (or of the panel in fullscreen), while a page in view failed
		Rect mangaRetryRect(bool full) const { return full ? Rect{200, 216, 240, 48} : Rect{200, 176, 240, 48}; }
		void mangaRetry() { readerCmds.push_back({ReaderCmd::Retry, 0, (int64_t)SDL_GetTicks()}); }
		void drawMangaRetry(bool full);

		// a note over the pages on the top panel (a line, an optional second: "Page 4 bookmarked"), asked by the bottom
		// panel: main shows it for `ms` and clears this (mpv's note would be under the reader's pages)
		struct TopNote { std::string line1, line2; int ms = 0; };
		TopNote topNote;

		// ---- fullscreen: the pages on this panel too ----
		// the pages' textures on this panel (a page in slices of at most 2048 rows), the chapter they belong to, how
		// many still wait to be made, when the page tag fades
		struct FullTex { int w = 0, h = 0; std::vector<SDL_Texture*> tiles; Uint32 used = 0; const void* src = nullptr; };	// src: the pixels it was made from
		std::map<int, FullTex> mfTex;
		uint64_t mfGen = 0;
		int mfWaiting = 0;
		Uint32 mfTagUntil = 0;
		// Book's tag (the setting "Chapter and page in Book"): how long it shows after a screen's pages load or turn,
		// the fade at its end, and its alpha now (0: not drawn)
		bool mfTagShown = false;	// (its last pass: a log line as it comes and goes)
		static constexpr Uint32 BOOK_TAG_MS = 2500, BOOK_TAG_FADE_MS = 400;
		int mangaBookTagAlpha(const Player& p, Uint32 now) const;
		int mfLastPage = -1;
		// the pages, the page tag, Try again; then the Manga reader's controls and first-time toast while they show
		void drawMangaFull(const Player& p);
		void mfFree();
		int readFilterNow = 0, readDimNow = 0;	// (Ui::draw: the reading filter and Extra dim as the pages are drawn)
		// Fullscreen's navigation (lay::readerFullNav), the Files reader's and the Manga reader's alike: a see-through
		// sheet over the pages with the name, "Page N of M", the page slider, the steps, Pages, View and One screen.
		// Each reader says what its keys do and keeps its own timer; the boxes drawn are the ones its touches test
		enum { FN_CH_BACK, FN_PG_BACK, FN_PG_ON, FN_CH_ON, FN_PAGES, FN_VIEW, FN_LEAVE, FN_KEYS };
		struct FullNav
		{
			std::string title;
			bool chapters = false;	// a chapter of a series (the outer two steps) or a file alone
			int slider = 0;	// its slide id (SL_DOC, SL_MANGA): held, the page under the finger is the one named
			bool off[FN_KEYS] = {};
			int pressed = -1;
			bool caps = true;	// Button hotkeys
			std::string chapter;	// before the page line, "Ch. 12" ("": a file alone)
		};
		static Rect fullNavKey(int id, bool chapters);
		static int fullNavKeyAt(int x, int y, bool chapters);	// -1: none there
		void drawFullNav(const FullNav& n);
		// a line in the panel's middle for a moment (how to leave fullscreen, how Book is held)
		void drawFullToast(const std::string& text);

		// ---- the pages grid (Y in the reader): a sheet over the reader (docs/redesign: pdf-pages) ----
		// the chapter's thumbnails six to a row (lay::pagesSheet), the D-pad moves the cursor, L2 / R2 a screenful, A
		// jumps and closes, B or Y (or a tap above the sheet) closes; the page it opened on is marked Here. The top panel
		// keeps showing the reader, moved to the page under the cursor (a Seek as the slider sends). The thumbnails
		// are made by the reader as each page decodes
		bool mangaPages = false;	// the grid is open
		int mangaPagesCursor = 0;	// the page under the cursor (0-based)
		int mangaPagesHere = 0;	// the page it was opened on (Here)
		int mangaPagesScroll = 0;	// the first row shown
		bool mangaPagesTouch = false;
		bool mangaPagesMoved = false;	// the finger moved far enough to be a drag: lifting it then doesn't open a page
		bool mangaPagesCaught = false;	// the finger stopped the grid moving: its lift opens nothing
		int mangaPagesPressed = -1;	// the thumbnail under the finger (-1: none)
		// the grid on a smooth list (ui_smoothlist.h): a row of thumbnails a row of it; mangaPagesScroll follows it
		SmoothList pagesList{ui};
		bool pagesMoving = false;	// it moved last frame (its stop puts the cursor among the rows shown)
		Rect mangaPagesView() const { return lay::pagesSheet().view; }
		// its two tabs: every page, or the bookmarked ones alone (L / R, a tap on a tab). The cursor, the rows and the
		// cells count slots: slot i is page i on Pages, the i-th bookmark on Bookmarks
		int mangaPagesTab = 0;
		std::vector<int> pageMarks;	// the bookmarked pages, ascending, as the grid opened (marksNow)
		std::vector<int> marksNow() const;	// asked of the reader's owner: the Manga store's, or Docs' for a file
		int mpCount() const { return mangaPagesTab ? (int)pageMarks.size() : readerStatus.pages; }
		int mpPage(int slot) const { return !mangaPagesTab ? slot : slot >= 0 && slot < (int)pageMarks.size() ? pageMarks[slot] : 0; }
		std::vector<std::string> mangaPagesTabLabels() const;
		void mangaPagesTabSet(int tab);
		void mpCentre();	// the cursor's row in the grid's middle, there at once
		SDL_Texture* mpThumb(int i);	// page i's thumbnail texture (one made a frame), marked as drawn now
		// the grid's own textures: one SDL_Texture per decoded page, made a few per frame so a turn never stalls
		struct ThumbTex { SDL_Texture* tex = nullptr; int w = 0, h = 0; Uint32 used = 0; };
		std::map<int, ThumbTex> mpTex;
		uint64_t mpGen = 0;
		int mpMade = 0;	// how many were made this frame (at most a few)
		// the pages the grid last asked thumbnails for (ReaderCmd::Thumbs: the rows in view and one each side)
		int mpAskFrom = -1, mpAskTo = -1;
		void mpAsk(int from, int to);
		void drawMangaPages(const Player& p);
		void mangaPagesOpen(Player& p);
		void mangaPagesClose(Player& p);
		bool mangaPagesKey(Button b, Player& p);
		bool mangaPagesPointer(const Pointer& pt, int x, int y, Player& p);
		void mpFree();
		static constexpr int MPG_COLS = lay::PAGES_COLS, MPG_ROW_H = lay::PAGES_ROW_H;
		// page i's cell (0-based), at the grid's offset now: what draw and the finger both use
		lay::PagesCell mangaPageCell(int i) const { return lay::pagesCell(pagesList.rowBox(i / MPG_COLS), i % MPG_COLS); }

		// ---- the dragged lists ----
		// a finger on one of the app's lists or grids (Library, History, Browse, Downloads, Sources, a server's, a
		// network folder's, a series' chapters, the sheets' lists; Docs' menu), each a smooth list (ui_smoothlist.h):
		// it goes down on a row (mangaListSmooth; a list still gliding is only caught); past the scroller's slop the
		// content follows the finger by the pixel and glides on from the lift; lifted without one, near where it went
		// down, it's a tap on that row (tap). Ui::pointer sends it the moves and the lift while it's on
		static constexpr int MG_LIST_SLOP = 14;	// a finger that slides sideways more is no tap (mangaListPointer)
		struct MangaListDrag
		{
			bool on = false, moved = false;
			int downX = 0, downY = 0;	// where the finger went down (640x480)
			int item = -1;	// the row under the finger (-1: none, only a drag)
			std::function<void(int)> tap;
			SmoothList* smooth = nullptr;
			bool caught = false;	// the finger stopped the list moving: its lift is no tap
		};
		MangaListDrag mlist;
		// A list (Manga's pages, Docs' menu) on a smooth list whose page keeps an int scroll (its first whole row shown)
		// and a cursor, which its keys set as they always did: follow() before it draws turns a change of either into
		// the list's motion (the cursor kept in sight by a glide; a scroll set elsewhere, a list opened or put back,
		// there at once), and at the end of a fling puts the cursor among the rows shown (its column kept, as the old
		// drags did); sync() after it drew writes the first whole row shown back
		struct FollowList
		{
			FollowList(Ui& u) : list(u) {}
			SmoothList list;
			int synced = -1, cursorWas = -2;
			bool wasMoving = false;
			std::function<void()> scrolled;	// a finger's: the next page asked near the end (Manga's grids, chapters)
			std::function<void(Player&)> refresh;	// pull to refresh (SmoothList::Spec::refresh; null: none)
			void follow(Rect view, int rowH, int n, int cols, int& scroll, int* cursor);
			bool sync(int& scroll);	// true: the first whole row changed
		};
		// `item`: the row (or cell) under the finger, found by the list's own boxes
		void mangaListSmooth(SmoothList& list, int x, int y, int item, std::function<void(int)> tap);
		bool mangaListPointer(const Pointer& pt, int x, int y);
	};
}
