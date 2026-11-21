// Files' document viewers, the bottom panel's side: an area of Ui (Ui::Area, ui.h; docs/notes/ui-areas.md).
// Everything the two readers keep and do is here and in ui_docs.cpp: a feature inside them touches these two files,
// not ui.h. Included by ui_docs.cpp, by main.cpp (the channel to the top panel's readers, below) and by the few
// files of Ui that ask the readers something (a place forgotten, a file moved, Find's keyboard).
#pragma once
#include "ui.h"
#include "ui_reader.h"	// PageReader::FollowList: the menu's smooth list
#include "writequeue.h"	// DurableWriter: a document's place off the main thread
#include "bookplace.h"	// PlaceWrite: the dashboard's reader writes the same place

namespace bp
{
	// Page::DocText: a TXT or a book of text (EPUB) on main's TextReader. Page::DocPages: a PDF, a comic (CBZ, CBR)
	// or a comic EPUB on main's MangaReader, which it shares with the Manga reader's page: what it asks of that
	// reader and what it hears back are Ui's (readerCmds, readerStatus, the pages grid, fullscreen's textures),
	// used here through `ui`.
	struct Ui::Docs : Ui::Area
	{
		explicit Docs(Ui& u) : Area(u) {}

		// ---- the area (Ui's press, pointer, tick, renderPage and signature come here while a reader shows) ----
		bool has(Page pg) const override { return pg == Page::DocText || pg == Page::DocPages; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		void tick(Player& p) override;
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;
		// the menu's smooth list while it shows, else the pages grid's (PageReader's) while that shows
		SmoothList* smoothList() override;
		// Book's page turn and its notice: a frame each step
		bool animating() const override { return docTurning(); }

		// ---- for the rest of Ui ----
		// a document from Files: opened in its viewer; false when the path isn't one (Ui::openDocument)
		bool open(const std::string& path, Player& p);
		// each document's place (a TXT's byte, a book's block, a PDF's page), kept in doc-places.conf: Recent's
		// "Open from the start" forgets one, the dashboard reads them
		const ResumeEntry* placeOf(const std::string& path);
		void forgetPlace(const std::string& path);
		// a file or folder renamed or moved: its place, its bookmarks and the document shown follow it
		void pathMoved(const std::string& from, const std::string& to);
		// the dashboard's reader (api_books.cpp, through WebServer::call): its place written against the stored one
		// (bookplace.h), the event sent, and a reader open on that book told (foreignPlace); out: what is stored now
		PlaceVerdict webPlace(const PlaceWrite& w, ResumeEntry& out);
		// the dashboard's bookmarks: one added at a place, one removed (false: none there)
		void webMarkAdd(const std::string& path, uint64_t id, double frac, const std::string& label);
		bool webMarkRemove(const std::string& path, uint64_t id);
		// the keyboard's Done on Find's field: searched on from the place
		void findTyped(const std::string& query);
		// the left stick (issue 16): the text scrolls, the pages pan; Book turns a spread the way its D-pad points
		void stickAxis(int value, Player& p, int valueX = 0);
		// the place now, queued, and the writer waited for (quit: at most a second)
		void docFlush();
		// main creates it once and never destroys it (its thread may be inside an fsync at exit)
		DurableWriter* writer = nullptr;

		// ---- for main: the top panel's readers ----
		// A text: Docs asks (takeTextRead), main opens it on its TextReader, takes textCmds each pass and tells
		// textStatus back. Pages go to main's MangaReader as a manga chapter does (Ui::mangaRead)
		struct TextReadRequest { std::string path; uint64_t place = 0; };
		bool takeTextRead(TextReadRequest& out)
		{
			if (textRead.path.empty()) return false;
			out = std::move(textRead);
			textRead = {};
			return true;
		}
		struct TextCmd { int kind; double v; int64_t ms; };
		std::vector<TextCmd> textCmds;
		bool textCloseAsked = false;	// main closes the text viewer
		bool textEncodingAsked = false;	// main gives the source this code page ("" the detected one)
		std::string textEncoding;
		struct TextStatus
		{
			bool open = false, atEnd = false;
			double fraction = 0;
			uint64_t place = 0;
			std::string error, encoding, title;
			std::vector<std::pair<std::string, std::pair<uint64_t, int>>> contents;	// title, (block id, depth)
			int book = 0;	// Book's hold (0: the scroll)
			std::string snippet;	// the first words at the place (a new bookmark's label)
			std::shared_ptr<const std::vector<uint8_t>> bottom;	// Book: the bottom panel's page (RGBA, turned)
			int bottomW = 0, bottomH = 0;
			uint64_t bottomGen = 0;
			// Book's page turn (textreader.h): the bottom's page before it, its way (0: none), its start, and whether
			// the top panel holds the right-hand page
			std::shared_ptr<const std::vector<uint8_t>> bottomOld;
			int turnDir = 0;
			int64_t turnAt = 0;
			bool rightOnTop = false;
			int turnMs = 340;
			int matchStep = 0;	// Find's step within the block shown: 1 found one, -1 none (the search goes on), 0 none asked
			int noticeDir = 0;	// the last turn's way, and when: its notice (next page, previous page) fades out
			int64_t noticeAt = 0;
		};
		TextStatus textStatus;
		// the reading filter for the PDF viewer's pages (DOC_FILTER_*), or -1 while no document of Files is shown
		int docPagesFilter(const Player& p) const;
		// Find's query while a match is shown (the text viewer marks it), else ""
		std::string docHighlight() const;
		// the PDF viewer's pan area: the page small, and the part on the top panel (main sets them each pass)
		struct DocPan
		{
			int page = -1, w = 0, h = 0;
			std::shared_ptr<const std::vector<uint8_t>> rgba;
			float x0 = 0, y0 = 0, x1 = 1, y1 = 1;	// the view's box in the page, as fractions
		};
		DocPan docPan;

		// ---- the readers' own ----
		TextReadRequest textRead;
		std::string docPath;	// the document shown
		Page docFrom = Page::Browse;	// where B goes back to
		std::unique_ptr<ResumeStore> docPlaces;	// (placeOf)
		bool docPlacesDirty = false;
		Uint32 docSavedAt = 0;
		bool comicVideoOk = false;	// the video-stop question was answered (or turned off): the comic opens now
		bool docLive = false;	// main has opened it (a close after that is the viewer closing)
		// the place this reader last wrote (another device's write since is not this reader's move: rule 3-4)
		ResumeEntry docOwnLast;
		// a browser's place in the book shown, while it differs from this reader's: the first row of Contents (Y) in a
		// text, of the options in a book of pages; no note over the page (the owner's call, 2026-11-21)
		ResumeEntry foreignPlace;
		bool foreignShown() const;
		std::string foreignLabel() const;
		void foreignGo();
		void contentsOpen();
		int contentsCount() const { return (int)textStatus.contents.size() + (foreignShown() ? 1 : 0); }
		// a place written (the handheld's or a browser's): the dashboard's pages hear it ("book-place")
		void docPlaceSent(const ResumeEntry& e);
		void docPlacesLoad();
		void docPlacesSave(bool now);
		void docRemember();
		void docClose(Player& p);
		void drawDocText(const Player& p);
		void drawDocPages(const Player& p);
		// Find (TXT, EPUB): the query typed, searched on a thread of its own (on its own copy of the file), the place
		// jumped to; then R / L the next / previous match while the bar shows, B leaves it
		struct FindJob;
		std::shared_ptr<FindJob> findJob;
		std::string findQuery;
		bool findOn = false;
		int findStepWay = 0;	// a step asked of the block shown (+1 / -1): none there, the search goes on that way
		std::string docNote;	// a word over the text a moment (found, not found): Book's turned as its pages
		Uint32 docNoteAt = 0;
		void findStart(bool back);
		void findTick();
		static void runFind(FindJob* j);
		std::string findSignature() const;
		void drawFind();
		// the menus: the text viewer's Text options and Contents, the PDF viewer's options
		enum { DOC_MENU_NONE, DOC_MENU_OPTIONS, DOC_MENU_CONTENTS, DOC_MENU_MARKS };
		// bookmarks (a TXT's or a book's): each document's, kept in doc-bookmarks.conf (path, place, share, label)
		struct DocMark { std::string path; uint64_t id = 0; double frac = 0; std::string label; };
		std::vector<DocMark> docMarks;	// every document's
		bool docMarksLoaded = false, docMarkArmed = false;
		void docMarksLoad();
		void docMarksSave();
		std::vector<int> docMarksHere() const;	// this document's, in reading order (indexes into docMarks)
		void docMarksOpen();
		void docBookmarkHere(Player& p);	// the header's bookmark key: one tap marks the place
		int docMenu = DOC_MENU_NONE, docMenuCursor = 0, docMenuScroll = 0;
		std::vector<int> docMenuRows() const;
		// the menu's rows on a smooth list (16, 72; 50 apart, the kit's 46-tall rows): the view, and row i's box now
		PageReader::FollowList menuList{ui};
		int menuListKind = DOC_MENU_NONE;	// the menu it last drew (another: its list starts again, at once)
		Rect docMenuView(int n) const;
		Rect docMenuRowRect(int i) const { Rect b = menuList.list.rowBox(i); return {b.x, b.y, b.w, 46}; }
		void docMenuStep(int dir, Player& p);
		void docMenuAct(Player& p);
		void drawDocMenu(const Player& p);
		// the left stick (issue 16): which reader it last commanded (0 none, 1 the text, 2 the pages), and Book's
		// last push direction (so one push is one spread, not one a frame)
		int stickTarget = 0, stickDir = 0;
		// the finger: on the text's scroll pad, the progress bar, the pan area, a button
		bool docTouch = false, docPanTouch = false, docMoved = false;
		int docDownX = 0, docDownY = 0, docLastY = 0, docPressed = -1;
		Uint32 docLastAt = 0, docTapAt = 0;
		double docVel = 0, docPanStartX = 0, docPanStartY = 0;
		float docTouchY = 0, docTouchX = 0;
		Uint32 docFullTagUntil = 0, docFullToastUntil = 0;	// fullscreen's navigation, and the line on the way in
		SDL_Texture* docPanTex = nullptr;
		SDL_Texture* docBookTex = nullptr;	// Book: the bottom panel's page
		SDL_Texture* docBookOldTex = nullptr;	// and the one before a turn, folding away (or opened over)
		const void* docBookOldSrc = nullptr;
		static constexpr int DOC_NOTICE_MS = 1800;	// a turn's notice: shown, then fading out over its last 600 ms
		bool docTurning() const
		{
			int64_t now = SDL_GetTicks();
			return ui.page == Page::DocText && ((textStatus.turnDir && now - textStatus.turnAt < textStatus.turnMs) ||
				(textStatus.noticeDir && now - textStatus.noticeAt < DOC_NOTICE_MS));
		}
		uint64_t docBookTexGen = 0;
		Uint32 docBookToastUntil = 0;
		// Book's sideways bar (docs/redesign: book-text-bar): a tap in the middle of the page shows it; its keys and
		// "Back to scrolling" are its own, upright as the panel is held
		bool docBookBar = false;
		int docBookBarPressed = -1;
		void drawDocBookBar(const Player& p);
		bool docBookBarPointer(const Pointer& pt, int x, int y, Player& p);
		void docBookBarAct(int which, Player& p);
		int docBookHoldLast = 0;	// the way Book was last held (the View row turns it on so again)
		void docBookTurn(int dpadDy, Player& p);
		int docPanTexPage = -1;
		const void* docPanTexSrc = nullptr;
		Rect docBarRect() const;
		Rect docBarTrack() const { Rect b = docBarRect(); return {b.x, b.y + 10, b.w, 10}; }
		// where a finger takes the bar: from under the header's lines down to the drag area
		Rect docBarHit() const { return {16, 60, W - 32, 44}; }
		Rect docPadRect() const;
		Rect docPanRect() const { return pagesLayout().pan; }
		Rect docBtn(int i) const;	// the text reader's row of keys
		Rect docPanThumb() const;
		// ---- the page reader's screen (a PDF, a comic: docs/redesign pdf): lay::readerPage's boxes, drawn and touched
		// alike. The keys by id (docPressed while a finger is on one) ----
		enum { DP_KEY_BACK, DP_KEY_ON, DP_KEY_OUT, DP_KEY_IN, DP_KEY_FIT, DP_KEY_PAGES, DP_KEY_VIEW, DP_KEY_FULL, DP_KEY_LIGHT, DP_KEYS };
		lay::ReaderPage pagesLayout() const { return lay::readerPage(ui.pageReader->readerStatus.mode == MANGA_STRIP); }
		int pagesShown() const;	// the page the header and the slider say (the finger's on the slider)
		Rect pagesKeyRect(int id) const;
		bool pagesKeyOff(int id) const;
		void pagesKeyAct(int id, Player& p);
		void drawPagesPan(const Player& p, const lay::ReaderPage& l);
		void drawPagesSlider(const lay::ReaderPage& l);
		void drawPagesKeys(const Player& p);
		void stepPagesFit(int dir, Player& p);	// Page / Width
		void stepPagesTurn(int dir);	// a quarter turn of the page
		// View's Layout: 0 Single page, 1 Vertical scroll, 2 Fullscreen (the strip over both panels)
		int pagesLayoutNow() const;
		std::string pagesPath;	// the PDF or comic the page reader has open (its layout is kept per kind)
		void setPagesLayout(int layout, Player& p);
		// Fullscreen in one step (SELECT, the Fullscreen key; B leaves too): on from either layout, and off back to the
		// layout it was turned on from
		void togglePagesFull(Player& p);
		int docFullFrom = 1;	// the layout Fullscreen was entered from (0 Single page, 1 Vertical scroll)
		// Fullscreen's navigation (lay::readerFullNav, PageReader::drawFullNav): up after a tap in the middle while
		// docFullTagUntil runs or the slider is held; on the way in a line says how to leave and that the tap is there
		static constexpr Uint32 FULL_TOAST_MS = 2500, FULL_NAV_MS = 4000;
		bool pagesFullNavShown() const;
		void drawPagesFullNav(const Player& p);
		void grabPagesSlider(Rect track, int x, int y, Player& p);	// the page slider under a finger (the row's, the navigation's)
		// X · More (pdf-more) and START · View (pdf-view, an overlay of its own)
		void openPagesMore(Player& p);
		struct PagesView;
		void openPagesView();
	};
}
