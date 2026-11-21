// The playlists, the browser's side of them: the store (Liked built in), the playlist open (browseDir PLDIR + its
// file) and its rows, its edits and how they reach what plays (plSaved, plTellPlayer -> SessionHost::playlistEdited,
// plFlush), the select mode (Edit, Add files, Pick a folder), Move and the grip's drag, the Add to... picker and the
// busy card of a network folder added, naming a playlist, and the dashboard's edits (plWeb*). A plain struct owned by
// the browser's area (Ui::Browse::playlists, ui_browse.h; docs/notes/ui-areas.md), as PageReader is owned by Ui: the
// playlists are folders of Page::Browse, not pages, so nothing dispatches to them by page; Browse and Ui call in.
// Stayed Ui's: Like (setLikedNow, setLikedPath, plIsLiked: the heart over any page), the Options menu that names a
// playlist (ctxTitle, ctxNote), the play request to main (playRequest), SelMode's values, the Main page's source
// line (drawSource) and whereOf. The select mode and the picker are not overlays: the picker's keys are taken at two
// points of press (over the player before the keyboard's turn is over, on the browser after the updater's pop-up and
// the busy card), its New playlist... opens the keyboard over it, and the select mode is the list's own header and
// bar, not a dialog over it.
#pragma once
#include "ui.h"
#include "ui_smoothlist.h"

namespace bp
{
	struct Ui::Playlists
	{
		Ui& ui;
		explicit Playlists(Ui& u) : ui(u) {}

		// Playlists (browseDir PLROOT, the list; PLDIR + its file, a playlist's items). The store is null with no state
		// folder. plCur: the playlist open (its items are browseRows, in its order; PL_ITEM rows)
		std::unique_ptr<PlaylistStore> plStore;

		Playlist plCur;
		bool plLoaded = false;	// plCur read (false: the file couldn't be)

		// the order the last playlist played in (shuffled or not): the player's index -> the item's, for coming back
		std::vector<int> plOrder;
		std::string plOrderId;
		// a playlist item that isn't there: checked as its row first shows (access(), cached till the list is read
		// again). 0 there, 1 not found (a local file), 2 its connection deleted; -1 not checked yet
		mutable std::vector<int8_t> plMissing;
		mutable int plMissingSerial = 0;
		int plMissingAt(int row) const;
		void plCheckShown();	// the rows in view checked
		void plRows(Player& p);	// browseRows from plCur
		std::vector<std::string> plNames() const;	// the playlists' names (for a new one's checks)
		void plPlay(const Playlist& pl, int index, bool shuffle, Player& p);
		void plSaved(Player& p, const std::vector<int>& remap);	// plCur edited: saved (soon), the list again, the player told
		// plCur's edits wait a second for the next (a run of moves is one write): written by tick, or now by plFlush
		// (leaving the page, playing, stopping, quitting)
		bool plDirty = false;
		Uint32 plDirtyAt = 0;
		void plFlush(Player& p);
		// a playlist that plays was edited (remap: its old items' new indexes; any index none maps to is new, played
		// last): the player gets its new items in the order they play
		void plTellPlayer(const Playlist& pl, const std::vector<int>& remap, Player& p);
		void plRefresh(Player& p);	// browseRows from plCur again, the cursor and the scroll kept

		// Selecting, one mode with three uses: Edit (a playlist's items: remove, move), Add (files on the Local and
		// Network tabs, kept across folders and both tabs, added in the order ticked) and Pick (a folder for Add
		// folder...). The tab bar is a header then, and the footer a bar of buttons

		int selMode = SEL_NONE;
		std::set<int> plSel;	// Edit's: plCur's indexes
		std::vector<std::string> pickOrder;	// Add's: full paths, in the order ticked
		std::set<std::string> picked;
		std::string selDest, selDestName;	// the playlist adding goes to ("": Add to... asks)
		int selSerial = 0;	// a tick changed
		Uint32 dropArmed = 0;	// Add's B at a first page with files ticked: again within 2 s drops them
		bool removeArmed = false;	// Edit's Remove pressed once
		void selStart(int mode, const std::string& dest);
		void selEnd();
		void selLeave(Player& p);	// out of it; a playlist's Add files... and Add folder... go back to it
		bool selKey(Button b, Player& p);	// true: it was the mode's
		bool selChoose(int row, Player& p);	// A or a tap on a row: true when it was the mode's (a tick)
		void selBack(Player& p, bool now);	// now: no second press asked (the header's ✕)
		void selAct(Player& p);	// X: Remove, Add, Add this folder
		void selAll(Player& p);	// SELECT
		bool selTickable(int row) const;
		bool selTicked(int row) const;
		int selCount() const { return selMode == SEL_EDIT ? (int)plSel.size() : (int)picked.size(); }
		int selFolderFiles(int& ticked) const;	// this folder's files that can be ticked (and how many are)
		bool pickable() const;	// Pick: a folder of a card's or a server's (Add this folder)
		std::string folderName(const std::string& dir) const;
		// Move (Edit's Y, an item's menu): the ticked items gathered into a block, moved a row (UP/DOWN) or a page
		// (LEFT/RIGHT), A drops it (saved), B puts everything back. moveRemap: the order before -> now
		bool moving = false, moveFromMenu = false;
		std::vector<PlaylistEntry> moveOrig;
		std::set<int> moveOrigSel;
		std::vector<int> moveRemap;
		void moveStart(Player& p);
		void moveStep(int dir, int times, Player& p);
		void moveEnd(bool keep, Player& p);
		void moveFollow();	// the block in view
		// a finger on a row's grip (Edit): the row (or the ticked items, gathered) dragged; a line where it lands, the
		// list scrolling near its edges; lifted, dropped and saved
		struct PlDrag { bool on = false; int y = 0; Uint32 scrollAt = 0; } plDrag;
		std::set<int> dragSel;
		void dragBegin(int row, int y, Player& p);
		int dragSlot() const;	// the gap the finger is at (0..n, before that row)
		int dragTarget() const;	// where the block's first row goes
		void dragEnd(Player& p);
		// adding: "Add to..." (the playlists, New playlist... first), then the files or a folder's media added
		bool pickerOpen = false;
		int pickerCursor = 0;
		bool pickerScrolled = false, pickerCaught = false;
		// its rows: a smooth list of their own (ui_smoothlist.h), the one Ui::draw steps while the picker is open
		std::unique_ptr<SmoothList> pickerRows;
		SmoothList& pickerSmooth();
		void pickerReveal(bool glide);	// the cursor's row in view
		std::vector<PlaylistStore::Info> pickerList;
		std::string pickerFolder;	// a folder's media ("Add to playlist..." on a folder); "" the ticked files
		// Music's Add to playlist… (an album's or an artist's songs): these very entries into a playlist chosen
		// in the picker, and a new one starts from `pickerNewName` ("" none: the folder's or the file's own rule)
		std::string pickerNewName;
		void openPickerEntries(std::vector<PlaylistEntry> entries, const std::string& newName, Player& p);
		// one file (Main's Add square, the Playing tab, a Recent card, a Liked item): "Add “title” to…", the playlists
		// last added to first, ✓ Added on those that have it (pickerHas), whose A asks again first (pickerArmed)
		std::string pickerOne, pickerOneTitle;
		double pickerOneDuration = -1;
		std::vector<char> pickerHas;
		int pickerArmed = -1;
		void openPicker(const std::string& folder, Player& p, bool one = false);
		void openAddOne(const std::string& path, const std::string& title, double duration, Player& p);
		void pickerChoose(int i, Player& p);
		void pickerNew(Player& p);	// New playlist...: named on the keyboard, then added to
		void pickerAdd(const std::string& dest, Player& p);
		bool pickerKey(Button b, Player& p);
		bool pickerPointer(const Pointer& pt, int x, int y, Player& p);
		void drawPicker();
		// the picker's sheet: lay::sheet over the page, 46-tall rows; the header names what goes where
		static constexpr int PICKER_SHOWN = 7;
		Rect pickerBox() const;
		Rect pickerView() const;	// where the rows are (the box's rows, to the hints)
		bool plNameThenAdd = false;	// the name typed is a new playlist to add to (pickerFolder's, the one file, or the ticked files)
		bool pickerNewOne = false;	// the playlist added to was just made (the card says so)
		std::string plNameAddFolder;
		void addFolderTo(const std::string& dir, const std::string& dest, bool back, Player& p);
		// added to dest (the end; up to the cap): what was added said, and back: to the playlist, on the first added.
		// False: nothing was (none to add, the playlist full, a file that couldn't be read or written)
		bool addTo(const std::string& destRef, const std::vector<PlaylistEntry>& add, int photos, const std::string& from,
			bool back, Player& p);
		std::vector<PlaylistEntry> pickedEntries() const;
		void returnToPlaylist(const std::string& file, int item, Player& p);
		// a network folder added before it was ever listed: listed first, a busy dialog meanwhile (B cancels)
		struct AddPending { std::string dir, dest; int serial = 0; bool back = false; } addPending;
		int addSerial = 1 << 29;
		void cancelAdd(Player& p);
		void drawBusy();

		void plDelete(const std::string& file, Player& p);
		// naming a playlist on the keyboard (PL_NAME): New, or Rename (plRenameFile); a name refused keeps it up
		bool plRenaming = false, plNameSubmit = false;
		std::string plRenameFile, plNameNote;
		// name: Rename's, or a new one's to start from ("" New playlist, the first number free)
		void plNameOpen(bool rename, const std::string& file, const std::string& name);
		void plNameDone(Player& p);
		// the item a menu is for (a playlist's file and the item's index; -1 the playlist itself)
		std::string ctxPlFile;
		int ctxPlItem = -1;


		// ---- the redesigned pages (docs/redesign: playlists, playlist, playlist-select, playlist-move, liked) ----
		// The area's own drawing, inside lay::shell().body only; Ui::Shell draws the header and hints from
		// plTitle, plCrumb and plHints.
		bool onPage() const;	// the list of playlists, or a playlist's own page
		lay::PlaylistPage pageLayout(const Player& p) const;
		lay::PlaylistsRoot rootLayout() const;	// the list of playlists: Music's chips, then 46-tall rows
		void syncRows(const Player& p);	// rowsY and rowsShown from whichever page shows
		void drawRoot(const Player& p);
		bool rootPointer(const Pointer& pt, int x, int y, Player& p);
		int chipPress = -1;	// the view chip under a finger
		void draw(const Player& p);
		std::string plTitle(const Player& p) const;
		std::string plCrumb(const Player& p) const;
		std::vector<Hint> plHints(const Player& p) const;
		int keyFocus = -1;	// the keys row over a playlist's rows (-1: the rows)
		int keyPress = -1;	// the key under a finger
		int barPress = -1;	// the select / move bar's key under a finger
		int plPressRow = -1;	// the row under a finger
		bool plTouchScrolled = false;
		int rowsY = 0, rowsShown = 0;	// the last layout's rows (the drag's maths)
		std::string focusFor;	// the playlist the focus was put in (a playing one opens on its row)
		void syncFocus(const Player& p);
		bool key(Button b, Player& p);	// the page's own; false: the browser's (X, Y, B, L/R, the menu)
		bool pointer(const Pointer& pt, int x, int y, Player& p);
		void actKey(int i, Player& p);	// Play, Shuffle, Add
		void barAct(int i, Player& p);	// select: Remove, Move, Add to; move: Put here, Cancel
		void addSheet(Player& p);	// the Add key's two rows (Add files…, Add folder…)
		bool pickerEdit = false;	// the picker is adding a playlist's ticked items (not files ticked in Files)
		std::vector<PlaylistEntry> pickerSel;	// those items, as they were when it opened
		int pickerShown() const;	// the picker's rows that fit its box

		// playlists edited from the web (WebPlaylistGlue), on the main thread. plWebFile: a playlist's file from its
		// id (its file's name less .m3u), "" none. plWebEdited: the file was saved with remap (old -> new): the page
		// showing it reads it again, the player told. Renamed, deleted, made: the pages that show them follow
		std::string plWebFile(const std::string& id) const;
		void plWebEdited(const std::string& file, const std::vector<int>& remap, Player& p);
		void plWebRenamed(const std::string& old, const std::string& file, const std::string& name, Player& p);
		void plWebDeleted(const std::string& file, Player& p);
		void plWebListChanged(Player& p);

	};
}
