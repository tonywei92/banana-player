// The browser (Page::Browse): Home, Files, Playlists, Network, All recent and the Extensions drawer, the pages that
// are folders of one list (browseDir). An area of Ui (Ui::Area, ui.h; docs/notes/ui-areas.md): its keys, its late
// touches, the list's state (the folder, its rows and paths, the section, the network listing), Files' own operations
// (the clip, Rename, the network folders' actions) and the Network tab's form are here and in ui_browse.cpp,
// ui_fileops.cpp and ui_forms.cpp.
// Stayed Ui's: the shared list's drawing and keys (drawTracks, rows, choose, listKey, listDown), the cursor and the
// scroll (every list page's; View's sheet asks rows and choose for a file's tracks), the Options menu (ctx*: the mini
// bar's hold opens it on any
// page), the updater's pop-up, the file job's card and Info's sheet (over any page), the places (roots, rootsCheck:
// the dashboard's and the manga reader's too), and the Browse touches that come before the area's point in
// Ui::pointer (All recent's tabs, Home's quit dialog, the busy card, the picker, a grip's drag, a finger on Home): they
// call in here where they always were.
#pragma once
#include "ui.h"

namespace bp
{
	struct Ui::Browse : Ui::Area
	{
		explicit Browse(Ui& u);
		~Browse() override;

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Browse; }
		bool key(Button b, Player& p) override;
		// its downs come late (after the header's "<" and the empty page's button), its lifts where the readers' do
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		bool pointerDownLate() const override { return true; }
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;

		// ---- the folder shown ----
		// HOMEDIR Home, "" the cards (Files' top), RECENT the recent videos (All recent)
		std::string browseDir;
		std::vector<Row> browseRows;
		std::vector<std::string> browsePaths;	// beside browseRows: what each row opens
		std::map<std::string, std::pair<int, int>> placeIn;	// each folder's cursor and scroll, for coming back
		int browseVersion = 0;
		void enterFolder(std::string dir, Player& p);
		void browseUp(Player& p);
		// a folder below a section's top (a card's folder, a server's, a playlist): a ".." row (up a folder) above the
		// list. It isn't one of browseRows (a playlist's rows are its items' indices): it's row -1, where the cursor and
		// the scroll can be (listFirst()), so nothing that counts, selects or plays rows ever sees it
		bool upRow() const;
		int listFirst() const { return upRow() ? -1 : 0; }
		void drawUpRow(int y, bool at);
		// the browser's cursor on a row and its window as full as the list allows (a list that came back shorter:
		// a scroll from before would show its last rows only)
		void fitList();
		bool folderRow(int row) const;
		bool holdRow(int row) const;	// a finger held on it opens its menu
		// the Network tab with no connections: its page offers Add connection; Playlists with none, New playlist
		bool emptyAction() const;
		int firstCursor() const;	// where a folder opened afresh puts the cursor
		void doEmptyAction(Player& p);
		std::string browseWhere() const;
		void drawWhere(int x, int maxW);	// the browser's where line: its end kept, the last folder brighter

		// ---- the sections (Ui::BrowseTab) ----
		int browseTab = TAB_LOCAL;
		std::string tabDir[BROWSE_TABS];
		void switchTab(int tab, Player& p);	// to where the section was left
		void openSection(int tab, Player& p);	// to its top
		void setSection(int tab);	// the section a page about to be entered is in (where this one was kept)
		bool atFirstPage() const;	// a section's top (or All recent): B goes Home from it
		bool atHome() const;
		// the section markers in a section's header (L, four marks, R), left of the battery (and the web's globe)
		Rect pipRect(int k, const Player& p) const;
		void drawSectionHeader(const Player& p);

		// ---- a network folder listing: what's asked of the worker now, and what went wrong last ----
		int netSerial = 0;
		int durSerial = 0, durLo = -1;	// the worker's list (one a listing) and the rows on screen it was last told
		bool netLoading = false;
		std::string netSelect;	// a file to put the cursor on once its folder is listed (back from playing it)
		std::map<std::string, std::vector<std::string>> netNames;	// netFolderNames'
		std::string netOpenPending;	// a network file to open once its folder is listed

		// ---- the Files page (docs/redesign: files, files-playing, select, actions, delete, empty-folder, info,
		// readonly, files-music): the local folders only (the cards' places and the folders under them). It draws in
		// lay::shell(p).body; Home, Playlists, Network and All recent keep Ui's list (drawTracks). ----
		bool filesPage() const;	// this page, a local folder (or the places), no menu
		std::string filesTitle() const;
		std::string filesCrumb() const;
		std::vector<Hint> filesHints(const Player& p) const;
		// (until the shared shell lands: the page's own header and hint bar, so the merge swaps only this)
		void frame(const Player& p);
		void drawFiles(const Player& p);
		lay::FilesPage filesLayout(const Player& p) const;
		int filesShown(const Player& p) const;
		int filesScroll(const Player& p) const;	// the scroll that keeps the cursor in view
		bool filesEmpty() const;
		bool filesReadOnly() const;	// the folder can't be written by this account
		std::string filesFooter() const;	// "7 items · 21 GB free"
		void filesRefresh(Player& p);
		void filesPlayAll(const std::string& dir, bool shuffle, Player& p);
		void filesToggleHidden(const std::string& path, Player& p);
		void filesToggleShowHidden(Player& p);
		// X, and a header's More: the More sheet (ui_more.cpp), built from the focused row and the folder it is in
		void filesOpenMore(Player& p);
		// Y: select mode (the Files one: any file or folder, for Playlist, Copy, Move, Delete)
		bool fileTickable(int row) const;
		bool fileTicked(int row) const;
		int fileSelCount() const { return (int)fileSelPaths.size(); }
		void filesSelStart(Player& p);
		void filesSelEnd(Player& p);
		void filesSelAll(Player& p);
		void filesSelAct(int what, Player& p);	// 0 Playlist, 1 Copy, 2 Move, 3 Delete
		void filesSelMore(Player& p);
		void filesClipStart(int op, Player& p);
		// the delete question (a Confirm sheet) and Info's page (an overlay of the browser's)
		void filesDeleteAsk(std::vector<std::string> paths, Player& p);
		struct FileDeleteAsk;
		struct FileInfoSheet;
		// a finger on the redesigned page: the header's keys, a row, the select bar, an empty page's keys
		bool filesKey(Button b, Player& p);
		bool filesPointer(const Pointer& pt, int x, int y, Player& p);
		// Files' new state (the redraws): select mode and its ticks, the touch, the read-only mark and the footer
		bool fileSel = false;
		std::set<std::string> fileSelPaths;
		std::vector<std::string> fileSelOrder;
		std::vector<std::string> clipPaths;	// a group Copy / Move (empty: clipPath alone)
		int filesTouch = -1, filesPressed = -1;
		int filesTouchY = 0, filesScrollStart = 0;
		bool filesScrolled = false;
		mutable std::string filesFooterCache, filesFooterDir;
		mutable Uint32 filesFooterAt = 0;
		void filesMove(int to, Player& p);
		void filesTickRow(int row);
		void drawFilesRow(int i, const lay::FilesPage& f, int onScreen, bool at, const Player& p);
		void drawFilesEmpty(const lay::FilesPage& f, const Player& p);

		// ---- Files' own file operations (ui_fileops.cpp; the job's card and Info's sheet are Ui's) ----
		// Copy / Move: the item is held (clip) while you go to the folder it goes in, and pasted there (SELECT, or the
		// bar's button); clipOp is one of Ui's CLIP_* (the file job's codes)
		int clipOp = CLIP_NONE;
		std::string clipPath;
		bool fileNameSubmit = false;
		std::string fileNameNote, fileRenamePath;
		bool fileOpsOn(int row) const;	// the row is a file or folder of the cards that can be copied, moved...
		// a network folder's: the file actions there (inside a share; not an SMB server's list of shares), whether
		// the account can write in it (the listing tried), and the files' sizes for Info
		bool netOpsHere() const;
		bool netWritable = false;
		std::unordered_map<std::string, int64_t> netSizes;
		bool netReadOnlySaid(const std::string& path, Player& p);	// a change in a read-only network folder: said, true
		// a network document or comic: copied to <state>/netdocs first (a progress sheet), then opened from there, so
		// a network that drops while reading takes nothing away
		void netOpenCopy(const std::string& path, Player& p);
		void startNetJob(int op, const std::string& src, const std::string& dest, const std::string& name, Player& p);
		void fileOpsAct(int what, const std::string& path, Player& p);
		bool clipKey(Button b, Player& p);	// SELECT pastes here, X lets the clip go
		bool clipPointer(const Pointer& pt, int x, int y, Player& p);
		bool clipCanPaste() const;
		void filePaste(Player& p);
		void drawClipBar();
		void fileRenameDone(Player& p);
		Rect clipBtn(int i) const { lay::FilesClip c = lay::filesClipBar(lay::shell(false).body); return c.key[i]; }

		// ---- the Network tab's other pages (ui_forms.cpp), as the browser's (browseDir FORMDIR, FINDNET) ----
		// A connection's form: its rows (form*), each opening the keyboard or stepping through its choices
		NetServer form;
		std::string formId;			// the connection edited, "" a new one
		std::string formPassword;	// typed this time
		bool formPasswordTyped = false;
		std::string formNote;		// Save's complaint (no server yet)
		void openForm(const NetServer* existing, const FoundHost* found, Player& p);
		void formRows();			// browseRows from form
		void formChoose(int64_t id, Player& p);
		void formChange(int64_t id, int dir);
		void formSave(Player& p);
		NetServer formServer() const;	// the form as a server (the port filled in)
		// Test connection (the form's row, a connection's menu): the answer comes as a NetListing (test)
		int testSerial = 0;
		std::string testText;
		void startTest(const NetServer& s, const std::string& password);
		// the servers found on the network (Find on the network)
		std::vector<FoundHost> found;
		bool findWasRunning = false;
		void findRows();

		// ---- the playlists: a plain struct of the browser's (Ui::Playlists, ui_playlists.h) ----
		std::unique_ptr<Playlists> playlists;
		// ---- Home and All recent: a plain struct of the browser's (Ui::Home, ui_home.h) ----
		std::unique_ptr<Home> homeView;
	};
}
