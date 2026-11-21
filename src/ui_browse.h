// The browser (Page::Browse): Home, Files, Playlists, Network, All recent, the pages that
// are folders of one list (browseDir). An area of Ui (Ui::Area, ui.h; docs/notes/ui-areas.md): its keys, its late
// touches, the list's state (the folder, its rows and paths, the section, the network listing), Files' own operations
// (the clip, Rename, the network folders' actions) and the Network tab's form are here and in ui_browse.cpp,
// ui_fileops.cpp and ui_forms.cpp.
// Stayed Ui's: the shared list's drawing and keys (rows, choose, listKey, listDown), the cursor and the
// scroll (every list page's; View's sheet asks rows and choose for a file's tracks), the Options menu (ctx*: the mini
// bar's hold opens it on any
// page), the updater's pop-up, the file job's card and Info's sheet (over any page), the places (roots, rootsCheck:
// the dashboard's and the manga reader's too), and the Browse touches that come before the area's point in
// Ui::pointer (All recent's tabs, Home's quit dialog, the busy card, the picker, a grip's drag, a finger on Home): they
// call in here where they always were.
#pragma once
#include "ui.h"
#include "ui_smoothlist.h"

namespace bp
{
	struct XferEnd;

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
		// Show in Files from a library page: B in the folder it opened (backDir) goes back to that page, not up
		// (cleared by any other B and by the rail)
		Page backPage = Page::Browse;
		std::string backDir;
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

		// ---- the list shown: Files' rows, the Network tab's connections and Find, All recent, the playlists' pages
		// (the list of them and a playlist's items). One smooth list (ui_smoothlist.h; docs/notes/
		// 2026-11-21-smooth-scroll.md) serves whichever shows; its offset is the truth while it shows, and ui.scroll
		// (the first row shown) follows it, for what still counts in rows (each folder's place, the durations asked
		// for, the playlists' moves). A connection's form and Home don't scroll this way ----
		SmoothList list{ui};
		SmoothList* smoothList() override;
		bool listPage() const;	// one of the pages above, with rows to show
		struct ListGeom
		{
			Rect view{};
			int rowH = 0, count = 0;
			std::string id;		// which list (a new one: its place from ui.scroll, the cursor's row in view)
		};
		ListGeom listGeom(const Player& p) const;
		// at a page's draw, before list.draw: a new list (or one come back to) placed at ui.scroll with the cursor's
		// row shown; else a cursor that moved (a key, a refresh: not a finger) glides into view, and a scroll the
		// row-counting code moved (a dragged row at an edge, a move's block) glides there
		void listBegin(const ListGeom& g);
		void listEnd(const ListGeom& g);	// after list.draw: ui.scroll follows the offset
		std::string listId;
		int listCursor = -1, listScroll = 0;
		int listReveal = -1;	// a row to show instead of the cursor's at the next draw (a moved block's far end)
		bool listCaught = false;	// a finger went down on a gliding list: it stopped it, and is no tap
		bool listOnFinger = false;	// a finger went down on the list's view: its moves drive it
		// a finger on the list (each page's pointer calls these for its rows): down on the view presses it (true;
		// `row` the row under it, -1 none or a glide it caught), a move drags it (true once past the slop), the lift
		// lets it glide on (true when the finger was the list's)
		bool listPress(const ListGeom& g, const Pointer& pt, int x, int y, int& row);
		bool listMove(const Pointer& pt);
		bool listLift();

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

		// ---- a network folder listing: what's asked of the worker now, and what went wrong last ----
		int netSerial = 0;
		int durSerial = 0, durLo = -1;	// the worker's list (one a listing) and the rows on screen it was last told
		bool netLoading = false;
		std::string netSelect;	// a file to put the cursor on once its folder is listed (back from playing it)
		std::map<std::string, std::vector<std::string>> netNames;	// netFolderNames'
		std::string netOpenPending;	// a network file to open once its folder is listed

		// ---- the Files page (docs/redesign: files, files-playing, select, actions, delete, empty-folder, info,
		// readonly, files-music): the local folders only (the cards' places and the folders under them). It draws in
		// lay::shell().body; the other views have their own pages. ----
		bool filesPage() const;	// this page, a local folder (or the places), no menu
		// Files' tabs (Files | Network): a strip on the two tops, the places and the connections' list; the folders
		// under them and the form have none. L / R step them, a tap too (lay::tabPage, Kit::tabs)
		bool filesBroken() const;	// the empty page is a folder that can't be shown, not an empty one
		int filesEmptyKeys() const;	// the empty page's keys: 0 (the places), 1 (Go up), 2 (and Send files)
		bool tabsOn() const;
		lay::TabPage browseTabPage() const;
		lay::Box tabsBody() const;	// the shell's body less the strip when it shows
		void drawBrowseTabs();
		bool tabsPointer(const Pointer& pt, int x, int y, Player& p);
		int browseTabPressed = -1;
		std::string filesTitle() const;
		std::string filesCrumb() const;
		std::vector<Hint> filesHints(const Player& p) const;
		void drawFiles(const Player& p);
		lay::FilesPage filesLayout(const Player& p) const;
		int filesShown(const Player& p) const;
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
		// Adding files to a playlist (the Playlists area's SEL_ADD with a destination, SEL_PICK: a folder to add whole)
		// is drawn here too, on the same rows and a bar of its own: its ticks are the playlists', across folders. The
		// Files select mode (fileSel) also keeps SEL_PICK set as a sentinel, so these two tell them apart
		bool addMode() const;
		bool pickMode() const;
		bool selecting() const { return fileSel || addMode() || pickMode(); }	// the bar takes the body's foot
		int barKeys() const { return addMode() ? 3 : pickMode() ? 2 : 4; }
		void drawAddBar(const lay::Box* bar);	// the select bar's keys (lay::FilesPage::bar, or NetPageLayout::bar)
		bool addKey(Button b, Player& p);
		void addBarAct(int i, Player& p);	// the bar's key i: All, Cancel, Add / Cancel, Add this folder
		bool addBarOn(int i) const;
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
		// photoViewer: p is the photo viewer's player and the file is the photo it shows (deleting it closes the viewer)
		void filesDeleteAsk(std::vector<std::string> paths, Player& p, bool photoViewer = false);
		// the photo viewer's Copy / Move (the folder to go to is opened with the clipboard set) and Delete
		void photoFileOp(int what, const std::string& path, Player& p);
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
		bool filesScrolled = false;
		mutable std::string filesFooterCache, filesFooterDir;
		mutable Uint32 filesFooterAt = 0;
		void filesMove(int to, Player& p);
		void filesTickRow(int row);
		void drawFilesRow(int i, Rect r, bool at, const Player& p);	// row i in its box r
		std::string filesRowKey(int i) const;	// what row i shows (the smooth list's key; "" the focused one's marquee)
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
		void startNetGroupJob(int op, const std::vector<std::string>& srcs, const std::string& dest, Player& p);
		bool netEnd(const std::string& path, XferEnd& e) const;
		void fileOpsAct(int what, const std::string& path, Player& p);
		bool clipKey(Button b, Player& p);	// SELECT pastes here, X lets the clip go
		bool clipPointer(const Pointer& pt, int x, int y, Player& p);
		bool clipCanPaste() const;
		void filePaste(Player& p);
		void drawClipBar();
		void fileRenameDone(Player& p);
		Rect clipBtn(int i) const { lay::FilesClip c = lay::filesClipBar(lay::shell().body); return c.key[i]; }

		// ---- the Network pages on the kit (docs/redesign: net-list, net-find, net-form) ----
		// The connections, Find on the network and a connection's form draw in lay::shell().body with Kit::row,
		// mediaRow, formRow, busySheet and emptyState; their touches test the same boxes. Keys stay as they were
		// (X's menu, Y's Add, START's Settings off the form) and gain the foot rows the spec draws.
		bool netPage() const;	// the Network tab, Find on the network or the form
		struct NetPageLayout
		{
			Rect body, list;
			int rowsY = 0, rowH = 60, shown = 0;
			Rect find, add;		// NETROOT's foot rows (w 0: none)
			Rect test, save;	// FORMDIR's two keys
			Rect bar[4];		// NETROOT adding or picking: the add / pick bar's keys (w 0: none)
		};
		NetPageLayout netLayout(const Player& p) const;
		void drawNet(const Player& p);
		void drawNetList(const Player& p);
		void drawNetForm(const Player& p);
		void drawNetFind(const Player& p);
		bool netKey(Button b, Player& p);
		bool netPointer(const Pointer& pt, int x, int y, Player& p);
		std::vector<int> formCfg() const;	// browseRows' config rows (Test and Save are the foot keys)
		int formShown(const Player& p) const;
		int netFoot = -1;	// NETROOT: -1 the list, 0 Find on the network, 1 Add connection
		int netPressed = -1;	// a finger on a row, a foot row or a form key
		bool netFinger = false;
		int netFingerY = 0, netFingerScroll = 0;	// (the form's rows: they still step)
		bool netFingerMoved = false;

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
