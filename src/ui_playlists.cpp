#include "ui.h"
#include "session_host.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include "theme.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#ifdef BP_WEB
#include "web/netif.h"
#include "web/server.h"
#include <qrcodegen.hpp>
#endif
#include "ui_internal.h"

namespace bp
{
	// ---- playlists ----

	void Ui::setStateDir(const std::string& stateDir)
	{
		stateDirPath = stateDir;	// the manga reader's store is kept here too (mangaOpen)
		if (stateDir.empty()) plStore.reset();
		else plStore = std::make_unique<PlaylistStore>(stateDir + "/playlists");
	}

	std::vector<std::string> Ui::plNames() const
	{
		std::vector<std::string> out;
		if (plStore)
			for (auto& i : plStore->list()) out.push_back(i.name);
		return out;
	}

	void Ui::plRows(Player& p)
	{
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& path, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back(path);
		};
		if (!plLoaded) { add(-1, "This playlist can't be opened", "", ""); return; }
		if (plCur.entries.empty()) { add(-1, quoted(plCur.name) + " is empty", "", ""); return; }
		// no stat of every item here (a long list on a slow card): whether it's there is found as its row shows
		for (auto& e : plCur.entries)
		{
			std::string note;
			if (isNetPath(e.path))
			{
				NetPath np;
				const NetServer* sv = netStore && parseNetPath(e.path, np) ? netStore->find(np.server) : nullptr;
				if (sv) note = serverName(*sv);
			}
			else
			{
				// where it was left (as the folders show it), then the folder it's in. Liked: how long it is
				const ResumeEntry* r = p.resumeStore().find(e.path);
				if (plIsLiked(plCur.file) && e.duration > 0)
				{
					add(PL_ITEM, e.title.empty() ? entryTitle(e.path) : e.title, e.path, formatTime(e.duration));
					continue;
				}
				std::string at = r && r->position >= 0 && !isAudioFile(e.path) ? "at " + formatTime(r->position) : "";
				std::string folder = baseName(parentFolder(e.path));
				note = at.empty() ? folder : folder.empty() ? at : at + " \xC2\xB7 " + folder;
			}
			add(PL_ITEM, e.title.empty() ? entryTitle(e.path) : e.title, e.path, note);
		}
	}

	int Ui::plMissingAt(int row) const
	{
		if (!isPlDir(browseDir) || row < 0 || row >= (int)browseRows.size() || browseRows[row].id != PL_ITEM) return 0;
		if (plMissing.size() != browseRows.size()) plMissing.assign(browseRows.size(), -1);
		int8_t& m = plMissing[row];
		if (m < 0)
		{
			const std::string& path = browsePaths[row];
			if (isNetPath(path))
			{
				// (whether the file is on the server is only known by asking it: as it opens)
				NetPath np;
				m = parseNetPath(path, np) && netStore && netStore->find(np.server) ? 0 : 2;
			}
			else m = access(path.c_str(), F_OK) == 0 ? 0 : 1;
			plMissingSerial++;
		}
		return m;
	}

	void Ui::plCheckShown()
	{
		if (page != Page::Browse || !isPlDir(browseDir)) return;
		for (int i = scroll; i < scroll + ROWS_SHOWN && i < (int)browseRows.size(); i++) plMissingAt(i);
	}

	void Ui::plPlay(const Playlist& pl, int index, bool shuffle, Player& p)
	{
		int n = (int)pl.entries.size();
		if (n == 0 || index < 0 || index >= n) return;
		plFlush(p);
		std::vector<int> order(n);
		for (int i = 0; i < n; i++) order[i] = i;
		if (shuffle)
		{
			// Shuffle play: the play mode Shuffle (it lasts: the songs after are picked at random, each once), from a
			// random item; the list keeps its own order (docs/design/session.md §14)
			std::random_device rd;
			index = (int)(rd() % (unsigned)n);
			SessionHost::get().setShuffle(true);
		}
		playRequest = {};
		for (int i : order) playRequest.paths.push_back(pl.entries[i].path);
		playRequest.index = index;
		playRequest.source.kind = SourceKind::Playlist;
		playRequest.source.title = pl.name;
		playRequest.source.id = pl.file;
		plOrder = order;
		plOrderId = pl.file;
		sideAsked.clear();
		p.setLastFolder(pl.file);
	}

	void Ui::plSaved(Player& p, const std::vector<int>& remap)
	{
		// written a second after the edits stop (a run of moves is one write): tick() or plFlush
		plDirty = true;
		plDirtyAt = SDL_GetTicks();
		plTellPlayer(plCur, remap, p);
		int c = remapIndex(cursor, remap);
		plRefresh(p);
		cursor = std::max(0, c);
		fitList();
	}

	void Ui::plFlush(Player& p)
	{
		if (!plDirty) return;
		plDirty = false;
		if (!plStore || !plStore->save(plCur)) alert("Couldn\xE2\x80\x99t save the playlist", "Is the card full or read only?");
	}

	void Ui::plTellPlayer(const Playlist& pl, const std::vector<int>& remap, Player& p)
	{
		// the session's list first (it plays here or in a page): Next reaches what was added, what went is skipped
		{
			std::vector<std::string> paths;
			for (auto& e : pl.entries) paths.push_back(e.path);
			SessionHost::get().playlistEdited(pl.file, paths, remap);
		}
		// the playlist that plays: its new items, in the order they play (shuffled: that order, less the ones gone,
		// and the ones added after)
		if (!p.playing() || p.source().kind != SourceKind::Playlist || p.source().id != pl.file) return;
		std::vector<int> order = plOrderId == pl.file ? plOrder : std::vector<int>();
		if (order.empty())
			for (int i = 0; i < (int)remap.size(); i++) order.push_back(i);
		if ((int)order.size() != p.count()) return;
		std::vector<int> now;
		std::vector<char> seen(pl.entries.size(), 0);
		int current = 0;
		for (int k = 0; k < (int)order.size(); k++)
		{
			int r = order[k] >= 0 && order[k] < (int)remap.size() ? remap[order[k]] : -1;
			if (r < 0 || r >= (int)pl.entries.size()) continue;
			if (k < p.index()) current++;
			now.push_back(r);
			seen[r] = 1;
		}
		for (int i = 0; i < (int)pl.entries.size(); i++)
			if (!seen[i]) now.push_back(i);
		std::vector<std::string> paths;
		for (int i : now) paths.push_back(pl.entries[i].path);
		plOrder = now;
		plOrderId = pl.file;
		p.updatePlaylist(paths, current);
	}

	void Ui::plRefresh(Player& p)
	{
		int c = cursor, s = scroll;
		plRows(p);
		plMissing.clear();
		plMissingSerial++;
		cursor = c;
		scroll = s;
		fitList();
		browseVersion++;
	}

	void Ui::plDelete(const std::string& file, Player& p)
	{
		if (!plStore || plIsLiked(file)) return;
		// its edits not written yet: never (written after, they'd bring it back)
		if (plDirty && plCur.file == file) plDirty = false;
		else plFlush(p);
		std::string name = baseName(file);
		for (auto& i : plStore->list())
			if (i.file == file) name = i.name;
		if (!plStore->remove(file)) { alert("Couldn\xE2\x80\x99t delete the playlist", "Is the card read only?"); return; }
		p.osd("Deleted " + quoted(name));
		if (p.resumeStore().lastFolder == file) p.setLastFolder("");
		int c = cursor, s = scroll;
		bool inList = browseDir == PLROOT;
		enterFolder(PLROOT, p);
		if (inList)
		{
			cursor = c;
			scroll = s;
			fitList();
		}
	}

	void Ui::plNameOpen(bool rename, const std::string& file, const std::string& name)
	{
		if (!plStore || (rename && plIsLiked(file))) return;
		plNameThenAdd = false;	// (Add to...'s New playlist sets it after)
		plRenaming = rename;
		plRenameFile = file;
		plNameNote.clear();
		plNameSubmit = false;
		std::string text = name;
		if (!rename)
		{
			// "New playlist" (or the folder's name), else the first number free
			auto names = plNames();
			std::string base = name.empty() ? std::string("New playlist") : name;
			// room for " 12" (cut between characters)
			if (base.size() > PL_NAME_MAX_BYTES - 4)
			{
				size_t cut = PL_NAME_MAX_BYTES - 4;
				while (cut > 0 && ((unsigned char)base[cut] & 0xC0) == 0x80) cut--;
				base = trimmed(base.substr(0, cut));
			}
			text = base;
			for (int n = 2; n < 1000 && checkPlaylistName(text, names) == PlNameError::Taken; n++) text = base + " " + std::to_string(n);
		}
		openKeyboard(PL_NAME, text);
	}

	void Ui::plNameDone(Player& p)
	{
		plNameSubmit = false;
		if (!plStore) return;
		// (a rename reads the file: the edits waiting go first)
		plFlush(p);
		std::string name = trimmed(kbEdit.text), file;
		auto names = plNames();
		PlNameError e = plRenaming ? plStore->rename(plRenameFile, name, file) : plStore->create(name, file);
		if (e == PlNameError::None)
		{
			plNameNote.clear();
			kbOpen = false;
			kbPressed = -1;
			if (!plRenaming && plNameThenAdd)
			{
				// Add to... > New playlist...: what was to be added goes in it (and the browser stays where it is)
				plNameThenAdd = false;
				pickerFolder = plNameAddFolder;
				pickerNewOne = true;
				pickerAdd(file, p);
				pickerNewOne = false;
				return;
			}
			if (!plRenaming)
			{
				// in it at once: what's next is adding to it
				enterFolder(plDirFor(file), p);
				p.setLastFolder(file);
				return;
			}
			p.osd("Renamed to " + quoted(name));
			std::string old = plRenameFile;
			if (p.playing() && p.source().kind == SourceKind::Playlist && p.source().id == old) p.renameSource(old, name, file);
			if (plOrderId == old) plOrderId = file;
			if (p.resumeStore().lastFolder == old) p.setLastFolder(file);
			if (tabDir[TAB_PLAYLISTS] == plDirFor(old)) tabDir[TAB_PLAYLISTS] = plDirFor(file);
			if (browseDir == plDirFor(old))
			{
				int c = cursor, s = scroll;
				enterFolder(plDirFor(file), p);
				cursor = c;
				scroll = s;
				fitList();
			}
			else if (browseDir == PLROOT)
			{
				enterFolder(PLROOT, p);
				for (size_t i = 0; i < browsePaths.size(); i++)
					if (browsePaths[i] == file) cursor = (int)i;
				fitList();
			}
			return;
		}
		// what's wrong, under the field; the keyboard stays up to put it right
		std::string taken = name;
		for (auto& n : names)
			if (checkPlaylistName(name, {n}) == PlNameError::Taken) taken = n;
		switch (e)
		{
		// (the store says Empty also when the file couldn't be written)
		case PlNameError::Empty: plNameNote = name.empty() ? "Type a name" : "Couldn't save the playlist"; break;
		case PlNameError::TooLong: plNameNote = "40 characters at most"; break;
		case PlNameError::BadChar: plNameNote = "A name can't have / in it"; break;
		case PlNameError::Taken: plNameNote = "You have one called " + quoted(taken); break;
		default: plNameNote = "That name can't be used"; break;
		}
		kbActiveAt = SDL_GetTicks();
	}

	// ---- playlists: selecting, moving, adding ----

	void Ui::selStart(int mode, const std::string& dest)
	{
		selEnd();
		selMode = mode;
		selDest = dest;
		selDestName.clear();
		if (plStore && !dest.empty())
			for (auto& i : plStore->list())
				if (i.file == dest) selDestName = i.name;
		dropTouch();
		holdAt = 0;
		fitList();	// editing has no Up row: off it
	}

	void Ui::selEnd()
	{
		selMode = SEL_NONE;
		plSel.clear();
		pickOrder.clear();
		picked.clear();
		selDest.clear();
		selDestName.clear();
		dropArmed = 0;
		removeArmed = false;
		moving = moveFromMenu = false;
		plDrag.on = false;
		selSerial++;
		browseVersion++;
	}

	void Ui::selLeave(Player& p)
	{
		std::string back = selDest;
		bool add = selMode == SEL_ADD || selMode == SEL_PICK;
		selEnd();
		if (add && !back.empty()) returnToPlaylist(back, -1, p);
	}

	void Ui::returnToPlaylist(const std::string& file, int item, Player& p)
	{
		setSection(TAB_PLAYLISTS);
		if (!exists(file)) { enterFolder(PLROOT, p); return; }
		enterFolder(plDirFor(file), p);
		if (item >= 0 && item < (int)browseRows.size())
		{
			cursor = item;
			// the first added in view, as near the top as the list allows (what came with it below it)
			scroll = std::min(item, std::max(0, (int)browseRows.size() - ROWS_SHOWN));
		}
		fitList();
	}

	bool Ui::selTickable(int row) const
	{
		if (row < 0 || row >= (int)browseRows.size()) return false;
		if (selMode == SEL_EDIT) return isPlDir(browseDir) && browseRows[row].id == PL_ITEM;
		// a video or a song (playlists are those: never a photo, a document or any other file Files lists)
		return selMode == SEL_ADD && browseRows[row].id == 2 && (isVideoFile(browsePaths[row]) || isAudioFile(browsePaths[row]));
	}

	bool Ui::selTicked(int row) const
	{
		if (!selTickable(row)) return false;
		return selMode == SEL_EDIT ? plSel.count(row) > 0 : picked.count(browsePaths[row]) > 0;
	}

	int Ui::selFolderFiles(int& ticked) const
	{
		int n = 0;
		ticked = 0;
		for (int i = 0; i < (int)browseRows.size(); i++)
			if (selTickable(i))
			{
				n++;
				if (selTicked(i)) ticked++;
			}
		return n;
	}

	bool Ui::pickable() const
	{
		// a folder (not a first page, Recent, or a server's list of shares)
		if (browseDir.empty() || pseudoDir(browseDir)) return false;
		return !isNetPath(browseDir) || (!netBaseName(browseDir).empty() && !netLoading);
	}

	std::string Ui::folderName(const std::string& dir) const
	{
		if (dir.empty() || pseudoDir(dir)) return "";
		if (isNetPath(dir)) return netBaseName(dir);
		for (auto& r : roots)
			if (r.path == dir) return r.label;
		return baseName(dir);
	}

	std::vector<PlaylistEntry> Ui::pickedEntries() const
	{
		std::vector<PlaylistEntry> out;
		for (auto& path : pickOrder)
			if (picked.count(path))
			{
				PlaylistEntry e;
				e.path = path;
				e.title = entryTitle(path);
				out.push_back(e);
			}
		return out;
	}

	bool Ui::selChoose(int row, Player& p)
	{
		if (row < 0 || row >= (int)browseRows.size()) return true;
		removeArmed = false;
		dropArmed = 0;
		if (selMode == SEL_EDIT)
		{
			if (selTickable(row))
			{
				if (!plSel.erase(row)) plSel.insert(row);
				selSerial++;
			}
			return true;
		}
		// Add and Pick: a folder, a connection, a place opens as ever; a file is ticked (Add), or nothing (Pick)
		if (browseRows[row].id != 2) return false;
		if (selMode == SEL_ADD && selTickable(row))
		{
			const std::string& path = browsePaths[row];
			if (picked.erase(path)) pickOrder.erase(std::remove(pickOrder.begin(), pickOrder.end(), path), pickOrder.end());
			else
			{
				picked.insert(path);
				pickOrder.push_back(path);
			}
			selSerial++;
		}
		else if (selMode == SEL_ADD) alert("Can\xE2\x80\x99t add this", "Playlists take videos and songs.");
		return true;
	}

	void Ui::selAll(Player&)
	{
		removeArmed = false;
		if (selMode == SEL_EDIT)
		{
			// All, or None when all are
			bool all = (int)plSel.size() == (int)plCur.entries.size();
			plSel.clear();
			if (!all)
				for (int i = 0; i < (int)plCur.entries.size(); i++) plSel.insert(i);
		}
		else if (selMode == SEL_ADD)
		{
			// this folder's files: all, or none when all are (the other folders' ticks stay)
			int ticked = 0, n = selFolderFiles(ticked);
			if (!n) return;
			for (int i = 0; i < (int)browseRows.size(); i++)
			{
				if (!selTickable(i)) continue;
				const std::string& path = browsePaths[i];
				if (ticked == n)
				{
					picked.erase(path);
					pickOrder.erase(std::remove(pickOrder.begin(), pickOrder.end(), path), pickOrder.end());
				}
				else if (picked.insert(path).second) pickOrder.push_back(path);
			}
		}
		selSerial++;
	}

	void Ui::selBack(Player& p, bool now)
	{
		removeArmed = false;
		if (selMode == SEL_EDIT) { selEnd(); return; }
		// X, Cancel, and B at a section's top leave the selection (B elsewhere goes up a folder). Files ticked ask
		// twice on a key, so one stray press doesn't lose them; a touch (the header's ✕) is meant: at once
		if (selMode == SEL_ADD && !now && !picked.empty() && !(dropArmed && SDL_GetTicks() - dropArmed < QUIT_WINDOW_MS))
		{
			dropArmed = SDL_GetTicks();
			return;
		}
		selLeave(p);
	}

	void Ui::selAct(Player& p)
	{
		if (selMode == SEL_EDIT)
		{
			if (plSel.empty()) return;
			// twice: the first press says how many go
			if (!removeArmed) { removeArmed = true; return; }
			int n = (int)plSel.size();
			auto remap = removeEntries(plCur.entries, plSel);
			plSel.clear();
			plSaved(p, remap);
			p.osd("Removed " + std::to_string(n) + " from " + quoted(plCur.name));
			selEnd();
			return;
		}
		if (selMode == SEL_ADD)
		{
			if (picked.empty()) return;
			if (selDest.empty()) { openPicker("", p); return; }
			addTo(selDest, pickedEntries(), 0, "", true, p);
			return;
		}
		if (selMode == SEL_PICK && pickable()) addFolderTo(browseDir, selDest, true, p);
	}

	bool Ui::selKey(Button b, Player& p)
	{
		bool adding = selMode == SEL_ADD || selMode == SEL_PICK;
		if (b != Button::X) removeArmed = false;
		// (adding, X cancels: its second press drops the ticks)
		if (b != (adding ? Button::X : Button::B)) dropArmed = 0;
		if (moving)
		{
			// the block: a row, a page; A drops it, B puts it back
			switch (b)
			{
			case Button::Up: case Button::Down: moveStep(b == Button::Up ? -1 : 1, 1, p); break;
			case Button::Left: case Button::Right: moveStep(b == Button::Left ? -1 : 1, ROWS_SHOWN, p); break;
			case Button::A: moveEnd(true, p); break;
			case Button::B: moveEnd(false, p); break;
			default: break;
			}
			return true;
		}
		switch (b)
		{
		// the list's own: moving the cursor, A (choose: a tick, or a folder opened)
		case Button::Up: case Button::Down: case Button::Left: case Button::Right: case Button::A: return false;
		case Button::Select: selAll(p); return true;
		case Button::Y:
			// adding: Y adds (X cancels, B goes up)
			if (adding) { selAct(p); return true; }
			if (selMode == SEL_EDIT && !plSel.empty())
			{
				moveFromMenu = false;
				moveStart(p);
			}
			return true;
		case Button::X:
			if (adding) selBack(p, false);
			else selAct(p);
			return true;
		// adding files: B goes up a folder, as browsing does (the ticks stay); at a section's top it leaves, as X does
		case Button::B:
			if (adding && !atFirstPage()) { browseUp(p); return true; }
			selBack(p, false);
			return true;
		case Button::L: case Button::R:
			// adding: the cards and the network, the playlists' tab left out (and the connection's form, the search:
			// the connections instead)
			if (selMode == SEL_EDIT) return true;
			if (tabDir[TAB_NETWORK] == FORMDIR || tabDir[TAB_NETWORK] == FINDNET) tabDir[TAB_NETWORK] = NETROOT;
			switchTab(b == Button::R ? TAB_NETWORK : TAB_LOCAL, p);
			return true;
		default: return true;	// START and the rest: nothing while selecting
		}
	}

	void Ui::moveFollow()
	{
		if (plSel.empty()) return;
		int first = *plSel.begin(), last = *plSel.rbegin(), n = (int)browseRows.size();
		cursor = first;
		if (last >= scroll + ROWS_SHOWN) scroll = last - ROWS_SHOWN + 1;
		if (first < scroll) scroll = first;
		scroll = std::clamp(scroll, 0, std::max(0, n - ROWS_SHOWN));
	}

	void Ui::moveStart(Player& p)
	{
		if (plSel.empty() || !plLoaded || plIsLiked(plCur.file)) return;
		moveOrig = plCur.entries;
		moveOrigSel = plSel;
		// the ticked ones together, at the first one's place, at once
		moveRemap = gatherSelection(plCur.entries, plSel);
		plSel = remapSelection(plSel, moveRemap);
		moving = true;
		removeArmed = false;
		plRefresh(p);
		moveFollow();
	}

	void Ui::moveStep(int dir, int times, Player& p)
	{
		if (plSel.empty()) return;
		int n = (int)plCur.entries.size();
		bool moved = false;
		for (int k = 0; k < times; k++)
		{
			if ((dir < 0 && *plSel.begin() == 0) || (dir > 0 && *plSel.rbegin() + 1 >= n)) break;
			auto r = moveSelection(plCur.entries, plSel, dir);
			for (int& m : moveRemap) m = m >= 0 && m < (int)r.size() ? r[m] : -1;
			plSel = remapSelection(plSel, r);
			moved = true;
		}
		if (!moved) return;
		plRefresh(p);
		moveFollow();
	}

	void Ui::moveEnd(bool keep, Player& p)
	{
		moving = false;
		if (keep)
		{
			bool same = true;
			for (int i = 0; i < (int)moveRemap.size(); i++) same = same && moveRemap[i] == i;
			// (the cursor stays on the block: plSaved would take it for an old index)
			int c = cursor, s = scroll;
			if (!same) plSaved(p, moveRemap);
			cursor = c;
			scroll = s;
			fitList();
		}
		else
		{
			plCur.entries = moveOrig;
			plSel = moveOrigSel;
			plRefresh(p);
			if (!plSel.empty()) cursor = *plSel.begin();
			fitList();
		}
		moveOrig.clear();
		moveRemap.clear();
		// from an item's menu: back to the list as it was; from Select: the same ticks
		if (moveFromMenu)
		{
			int c = cursor, s = scroll;
			selEnd();
			cursor = c;
			scroll = s;
		}
		selSerial++;
	}

	void Ui::dragBegin(int row, int y, Player& p)
	{
		if (plIsLiked(plCur.file)) return;	// Liked has no order of its own to change
		// a ticked row takes the ticked ones with it, gathered at once; else the row alone
		std::set<int> block = plSel.count(row) ? plSel : std::set<int>{row};
		moveRemap = gatherSelection(plCur.entries, block);
		dragSel = remapSelection(block, moveRemap);
		plSel = remapSelection(plSel, moveRemap);
		plRefresh(p);
		plDrag.on = true;
		plDrag.y = y;
		plDrag.scrollAt = SDL_GetTicks();
		removeArmed = false;
		holdAt = 0;
	}

	int Ui::dragSlot() const
	{
		int n = (int)browseRows.size();
		return std::clamp(scroll + (int)std::lround((plDrag.y - ROW_Y) / (double)ROW_H), 0, n);
	}

	int Ui::dragTarget() const
	{
		if (dragSel.empty()) return -1;
		int start = *dragSel.begin(), len = (int)dragSel.size(), s = dragSlot();
		return s <= start ? s : s >= start + len ? s - len : start;
	}

	void Ui::dragEnd(Player& p)
	{
		plDrag.on = false;
		if (dragSel.empty()) return;
		int start = *dragSel.begin(), to = dragTarget();
		int dir = to < start ? -1 : 1;
		for (int k = 0; k < std::abs(to - start); k++)
		{
			auto r = moveSelection(plCur.entries, dragSel, dir);
			for (int& m : moveRemap) m = m >= 0 && m < (int)r.size() ? r[m] : -1;
			dragSel = remapSelection(dragSel, r);
			plSel = remapSelection(plSel, r);
		}
		bool same = true;
		for (int i = 0; i < (int)moveRemap.size(); i++) same = same && moveRemap[i] == i;
		int first = *dragSel.begin(), s = scroll;
		if (!same) plSaved(p, moveRemap);
		moveRemap.clear();
		dragSel.clear();
		cursor = first;
		scroll = s;
		fitList();
		selSerial++;
	}

	void Ui::openPicker(const std::string& folder, Player& p, bool one)
	{
		if (!plStore) return;
		plFlush(p);
		if (!one) pickerOne.clear();
		pickerList = plStore->addTargets();	// not Liked: only the Like button adds there
		// the one changed last first (as good as the one last added to), then by name as listed
		std::vector<std::pair<long long, size_t>> order;
		for (size_t i = 0; i < pickerList.size(); i++)
		{
			struct stat st;
			order.push_back({stat(pickerList[i].file.c_str(), &st) == 0 ? (long long)st.st_mtime : 0LL, i});
		}
		std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		std::vector<PlaylistStore::Info> sorted;
		for (auto& o : order) sorted.push_back(pickerList[o.second]);
		pickerList = sorted;
		// one file: the playlists that have it already
		pickerHas.assign(pickerList.size(), 0);
		if (one)
			for (size_t i = 0; i < pickerList.size(); i++) pickerHas[i] = plStore->containsPath(pickerList[i].file, pickerOne);
		pickerArmed = -1;
		pickerFolder = folder;
		// none yet: the one to make is the only choice
		if (pickerList.empty() && !one) { pickerNew(p); return; }
		pickerOpen = true;
		pickerCursor = pickerScroll = 0;
		pressedBtn = -1;
	}

	void Ui::pickerNew(Player&)
	{
		// named after the folder (a ticked file's, the one the menu was for)
		pickerOpen = false;
		plNameOpen(false, "", pickerOne.empty() ? folderName(pickerFolder.empty() ? browseDir : pickerFolder) : "");
		plNameThenAdd = true;
		plNameAddFolder = pickerFolder;
	}

	void Ui::pickerChoose(int i, Player& p)
	{
		pressedBtn = -1;
		// one file a playlist has already: asked first (Add again?), a second A adds a copy
		if (i > 0 && !pickerOne.empty() && i - 1 < (int)pickerHas.size() && pickerHas[i - 1] && pickerArmed != i)
		{
			pickerArmed = i;
			return;
		}
		pickerOpen = false;
		if (i == 0) { pickerNew(p); return; }
		if (i - 1 < (int)pickerList.size()) pickerAdd(pickerList[i - 1].file, p);
	}

	void Ui::pickerAdd(const std::string& dest, Player& p)
	{
		if (!pickerOne.empty())
		{
			// the one file (addTo shows the card and lights the Add square)
			addTo(dest, {{pickerOne, pickerOneTitle, pickerOneDuration}}, 0, pickerOneTitle, false, p);
			pickerOne.clear();
			return;
		}
		// a folder's media (its menu's Add to playlist...), or the files ticked (then done selecting, still here)
		if (!pickerFolder.empty())
		{
			addFolderTo(pickerFolder, dest, false, p);
			return;
		}
		// (nothing added, the playlist full: the ticks stay, for another one)
		if (addTo(dest, pickedEntries(), 0, "", false, p)) selEnd();
	}

	bool Ui::pickerKey(Button b, Player& p)
	{
		int n = (int)pickerList.size() + 1;
		if (b != Button::A) pickerArmed = -1;	// moved off it: asked again next time
		switch (b)
		{
		case Button::Up: pickerCursor = (pickerCursor + n - 1) % n; break;
		case Button::Down: pickerCursor = (pickerCursor + 1) % n; break;
		case Button::Left: pickerCursor = std::max(0, pickerCursor - PICKER_SHOWN); break;
		case Button::Right: pickerCursor = std::min(n - 1, pickerCursor + PICKER_SHOWN); break;
		case Button::A: pickerChoose(pickerCursor, p); return true;
		case Button::B: pickerOpen = false; return true;
		default: return true;
		}
		if (pickerCursor < pickerScroll) pickerScroll = pickerCursor;
		if (pickerCursor >= pickerScroll + PICKER_SHOWN) pickerScroll = pickerCursor - PICKER_SHOWN + 1;
		return true;
	}

	bool Ui::pickerPointer(const Pointer& pt, int x, int y, Player& p)
	{
		int n = (int)pickerList.size() + 1;
		if (pt.kind == Pointer::Down)
		{
			// outside the box: closed; on a row, chosen as the finger lifts there (a drag scrolls)
			if (!pickerBox().hit(x, y)) { pickerOpen = false; swallowTouch = true; return false; }
			pressedBtn = -1;
			for (int i = pickerScroll; i < n && i < pickerScroll + PICKER_SHOWN; i++)
				if (pickerRow(i).hit(x, y)) pressedBtn = PICK_ROW + i;
			pickerTouchY = y;
			pickerScrollStart = pickerScroll;
			pickerScrolled = false;
			return false;
		}
		if (pt.kind == Pointer::Move && (pickerScrolled || std::abs(y - pickerTouchY) > 12))
		{
			pickerScrolled = true;
			pressedBtn = -1;
			pickerScroll = std::clamp(pickerScrollStart + (int)std::lround((pickerTouchY - y) / 46.0), 0, std::max(0, n - PICKER_SHOWN));
			pickerCursor = std::clamp(pickerCursor, pickerScroll, pickerScroll + PICKER_SHOWN - 1);
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			int id = pressedBtn;
			pressedBtn = -1;
			if (!pickerScrolled && id >= PICK_ROW && pickerRow(id - PICK_ROW).hit(x, y))
			{
				pickerCursor = id - PICK_ROW;
				pickerChoose(pickerCursor, p);
			}
		}
		return false;
	}

	void Ui::addFolderTo(const std::string& dir, const std::string& dest, bool back, Player& p)
	{
		// the folder's own videos and songs (not its folders'), in name order
		std::vector<std::string> names;
		if (isNetPath(dir))
		{
			auto it = netNames.find(dir);
			if (it == netNames.end())
			{
				// never listed: listed now (a moment; the busy dialog can cancel it), added when it comes
				NetPath np;
				const NetServer* sv = netStore && parseNetPath(dir, np) ? netStore->find(np.server) : nullptr;
				if (!sv || !netWorker) { alert("This connection is gone", "It was deleted. Add it again in Network."); return; }
				addPending = {dir, dest, ++addSerial, back};
				netWorker->list(addSerial, *sv, netStore->password(*sv), dir);
				return;
			}
			names = it->second;
		}
		else
		{
			bool ok;
			for (auto& e : listFolder(dir, ok, true))
				if (!e.dir) names.push_back(e.name);
			if (!ok) { alert("Can\xE2\x80\x99t open this folder", "Is the card still in?"); return; }
		}
		int photos = 0;
		auto add = folderEntries(dir, names, photos);
		addTo(dest, add, photos, folderName(dir), back, p);
	}

	bool Ui::addTo(const std::string& destRef, const std::vector<PlaylistEntry>& add, int photos, const std::string& from,
		bool back, Player& p)
	{
		std::string dest = destRef;	// (a copy: it can be selDest, which selEnd clears)
		auto many = [](int n, const char* one, const char* more) { return std::to_string(n) + " " + (n == 1 ? one : more); };
		std::string photoNote = photos ? " \xC2\xB7 skipped " + many(photos, "photo", "photos") : "";
		if (add.empty())
		{
			alert("Nothing to add", "There are no videos or songs in " + quoted(from) + photoNote + ".");
			return false;
		}
		Playlist pl;
		int skipped = 0;
		plFlush(p);
		if (plIsLiked(dest)) { alert("Liked fills itself", "Use the Like button (the heart) to add to Liked."); return false; }
		if (!plStore || !plStore->load(dest, pl, skipped)) { alert("Couldn\xE2\x80\x99t read the playlist", "Its file may be damaged, or the card read only."); return false; }
		int before = (int)pl.entries.size(), dropped = 0;
		bool again = !pickerOne.empty() && plStore->containsPath(dest, pickerOne);
		auto remap = addEntries(pl.entries, add, -1, PL_MAX_ENTRIES, dropped);
		int added = (int)add.size() - dropped;
		if (added <= 0) { alert("The playlist is full", quoted(pl.name) + " has " + std::to_string(PL_MAX_ENTRIES) + " items, as many as a playlist holds."); return false; }
		if (!plStore->save(pl)) { alert("Couldn\xE2\x80\x99t save the playlist", "Is the card full or read only?"); return false; }
		plTellPlayer(pl, remap, p);
		std::string note = "Added " + std::to_string(added) + " to " + quoted(pl.name) + photoNote;
		if (dropped) note += " \xC2\xB7 Playlist is full, " + std::to_string(dropped) + " left out";
		if (!pickerOne.empty())
		{
			// one file: the card over the top panel, the Add square's check
			p.showNoteCard(G_PLAYLIST_ROWS, G_PLAYLIST.h, theme::ass(theme::TEXT).c_str(), (again ? "Added again to " : "Added to ") + pl.name,
				pickerNewOne ? "New playlist \xC2\xB7 " + pickerOneTitle : pickerOneTitle);
			addFlashUntil = SDL_GetTicks() + 1200;
		}
		else p.osd(note);
		if (back)
		{
			selEnd();
			returnToPlaylist(dest, before, p);
		}
		else browseVersion++;
		return true;
	}

	void Ui::cancelAdd(Player& p)
	{
		addPending = {};
		pressedBtn = -1;
		if (!netWorker) return;
		// everything asked of the worker goes: the player's sidecar folders (its file opens without them), a file
		// waiting for its folder (opened alone), the folder being listed here (asked again)
		netWorker->cancelAll();
		for (auto& s : sidePending) p.netFolderListed(s.second, {}, false);
		sidePending.clear();
		if (!netOpenPending.empty())
		{
			openRequest = netOpenPending;
			netOpenPending.clear();
		}
		if (netLoading && isNetPath(browseDir)) enterFolder(browseDir, p);
	}

	// ---- playlists: drawing the select mode, the picker, the busy dialog, the Main page's source ----

	std::string Ui::whereOf(const std::string& dir) const
	{
		std::string where = dir;
		if (isNetPath(dir))
		{
			NetPath np;
			parseNetPath(dir, np);
			const NetServer* sv = netStore ? netStore->find(np.server) : nullptr;
			where = (sv ? serverName(*sv) : std::string("?")) + (np.path == "/" ? "" : np.path);
		}
		else
		{
			size_t best = 0;
			for (auto& r : roots)
				if (r.path.size() > best && (dir == r.path || dir.rfind(r.path + "/", 0) == 0))
				{
					best = r.path.size();
					where = r.label + dir.substr(r.path.size());
				}
		}
		// the folders apart: "share / Movies"
		std::string out;
		for (char c : where) out += c == '/' ? std::string(" / ") : std::string(1, c);
		return out;
	}

	void Ui::drawWhere(int x, int maxW)
	{
		// a network folder the account can't write to says so at the line's end: a lock and "Read only"
		if (isNetPath(browseDir) && netOpsHere() && !netLoading && !netWritable && page == Page::Browse)
		{
			const char* ro = "Read only";
			int tw = (int)std::lround(textWidth(ro, 0) / sx), gw = glyphW(G_LOCK);
			int right = x + maxW;
			drawTextMid(ro, 0, DIM, right - tw, SUB_Y);
			drawGlyphMid(G_LOCK, right - tw - 8 - gw / 2, SUB_Y, DIM);
			maxW -= tw + gw + 24;
		}
		std::string where = elideLeft(browseWhere(), 0, maxW);
		size_t cut = where.rfind(" / ");
		if (cut == std::string::npos) drawTextMid(where, 0, TEXT, x, SUB_Y);
		else
		{
			std::string head = where.substr(0, cut + 3);
			drawTextMid(head, 0, DIM, x, SUB_Y);
			drawTextMid(where.substr(cut + 3), 0, TEXT, x + (int)std::lround(textWidth(head, 0) / sx), SUB_Y);
		}
	}

	void Ui::drawSelHeader(const Player& p)
	{
		drawBackIcon();
		drawBattery(616, 24, p);
		int n = selCount();
		auto items = [](int k) { return std::to_string(k) + (k == 1 ? " item" : " items"); };
		std::string title, count;
		SDL_Color countInk = DIM;
		bool move = moving || plDrag.on;
		if (move)
		{
			// "Rows 5–7 of 12": where the block is now
			const std::set<int>& block = moving ? plSel : dragSel;
			int first = block.empty() ? 0 : *block.begin() + 1, last = block.empty() ? 0 : *block.rbegin() + 1;
			title = "Move " + items((int)block.size());
			count = first == last ? "Row " + std::to_string(first) + " of " + std::to_string(plCur.entries.size())
				: "Rows " + std::to_string(first) + "\xE2\x80\x93" + std::to_string(last) + " of " + std::to_string(plCur.entries.size());
		}
		else
		{
			title = selMode == SEL_EDIT ? std::string("Select items") : selMode == SEL_PICK ? "Add a folder to " + quoted(selDestName)
				: selDest.empty() ? std::string("Select files") : "Add to " + quoted(selDestName);
			if (selMode != SEL_PICK)
			{
				count = n ? std::to_string(n) + " selected" : "None yet";
				countInk = n ? ACCENT : DIM;
			}
		}
		int cw = count.empty() ? 0 : (int)std::lround(textWidth(count, 1) / sx);
		int end = std::min(496, statusLeft(p) - 12);	// (left of the clock)
		if (!count.empty()) drawTextMid(count, 1, countInk, end, 32, 0, 1);
		drawTextMid(title, 2, TEXT, 72, 32, std::max(40, end - (cw ? cw + 16 : 0) - 72));
		// the line under it: Move's keys; adding, where it is (a card or the network); a playlist's, as ever
		if (moving)
		{
			int x = 24;
			for (auto& part : std::vector<std::pair<std::string, std::string>>{{"UP/DOWN", " move"}, {" \xC2\xB7 ", ""}, {"A", " drop"},
					 {" \xC2\xB7 ", ""}, {"B", " cancel"}})
			{
				bool sep = part.second.empty();
				drawTextMid(part.first, 0, sep ? DIM : ACCENT, x, SUB_Y);
				x += (int)std::lround(textWidth(part.first, 0) / sx);
				if (!sep)
				{
					drawTextMid(part.second, 0, DIM, x, SUB_Y);
					x += (int)std::lround(textWidth(part.second, 0) / sx);
				}
			}
			return;
		}
		if (selMode == SEL_EDIT)
		{
			std::string c = items((int)plCur.entries.size());
			drawTextMid(c, 0, DIM, 616, SUB_Y, 0, 1);
			drawWhere(24, 592 - (int)std::lround(textWidth(c, 0) / sx) - 24);
			return;
		}
		bool net = browseTab == TAB_NETWORK;
		drawGlyphMid(net ? G_NETWORK : G_CARD, 33, SUB_Y, DIM);
		drawWhere(52, 564);
	}

	void Ui::drawBarButton(int id, const Glyph* g, const std::string& label, const char* key, SDL_Color ink, bool filled, bool off)
	{
		Rect r = buttonRect(id);
		bool down = pressedBtn == id && !off;
		bool lit = filled && !off;
		drawBox(r, down ? PRESSED : lit ? ACCENT : BUTTON, R_BUTTON);
		SDL_Color c = off ? DIM : lit && !down ? ON_ACCENT : ink;
		SDL_Color k = off ? DIM : lit && !down ? ON_ACCENT : ACCENT;
		// the key at the right; the glyph and the label in the middle of what's left
		int kw = key ? (int)std::lround(textWidth(key, 0) / sx) : 0;
		if (key) drawTextMid(key, 0, k, r.x + r.w - 12, r.y + r.h / 2, 0, 1);
		int room = r.w - 24 - (kw ? kw + 8 : 0);
		int gw = g ? glyphW(*g) : 0;
		int tw = std::min(room - (gw ? gw + 8 : 0), (int)std::lround(textWidth(label, 0) / sx));
		int x = r.x + 12 + std::max(0, (room - (gw ? gw + 8 : 0) - tw) / 2);
		if (g)
		{
			drawGlyphMid(*g, x + gw / 2, r.y + r.h / 2, c);
			x += gw + 8;
		}
		drawTextMid(label, 0, c, x, r.y + r.h / 2, tw + 1);
	}

	void Ui::drawSelBar()
	{
		auto many = [](int n) { return std::to_string(n); };
		int n = selCount();
		if (moving)
		{
			drawBarButton(MOVE_CANCEL, &G_CROSS, "Cancel", "B", TEXT, false, false);
			drawBarButton(MOVE_DONE, &G_CHECK, "Done", "A", TEXT, true, false);
			return;
		}
		if (selMode == SEL_EDIT)
		{
			bool all = !plCur.entries.empty() && (int)plSel.size() == (int)plCur.entries.size();
			drawBarButton(SEL_ALL, &G_BOX_ON, all ? "None" : "All", "SELECT", TEXT, false, plCur.entries.empty());
			// (Liked: no order of its own to move, and Remove is Unlike)
			bool liked = plIsLiked(plCur.file);
			if (!liked) drawBarButton(SEL_MOVE, &G_MOVE, "Move", "Y", TEXT, false, n == 0);
			// (armed: the words alone, room for them)
			drawBarButton(SEL_ACT, removeArmed ? nullptr : liked ? &G_HEART_O : &G_MINUS,
				removeArmed ? (liked ? "Again: unlike " : "Again: remove ") + many(n) : std::string(liked ? "Unlike" : "Remove"), "X",
				removeArmed ? ACCENT : DANGER, false, n == 0);
			return;
		}
		bool dropping = dropArmed && SDL_GetTicks() - dropArmed < QUIT_WINDOW_MS;
		if (selMode == SEL_ADD)
		{
			int ticked = 0, files = selFolderFiles(ticked);
			drawBarButton(SEL_ALL, &G_BOX_ON, files && ticked == files ? "None" : "All", "SELECT", TEXT, false, files == 0);
			// (X cancels; B goes up a folder)
			drawBarButton(SEL_CANCEL, dropping ? nullptr : &G_CROSS, dropping ? "Again: drop " + many(n) : std::string("Cancel"),
				"X", dropping ? ACCENT : TEXT, false, false);
			drawBarButton(SEL_ACT, &G_PLUS, selDest.empty() ? std::string("Add to\xE2\x80\xA6") : "Add " + many(n), "Y", TEXT, n > 0, n == 0);
			return;
		}
		drawBarButton(SEL_CANCEL, &G_CROSS, "Cancel", "X", TEXT, false, false);
		drawBarButton(SEL_ACT, &G_FOLDER, "Add this folder", "Y", TEXT, true, !pickable());
	}

	void Ui::drawPicker()
	{
		Rect box = pickerBox();
		int n = (int)pickerList.size() + 1;
		drawDialog(box, !pickerOne.empty() ? "Add " + quoted(pickerOneTitle) + " to\xE2\x80\xA6"
			: pickerFolder.empty() ? "Add " + std::to_string(picked.size()) + " to\xE2\x80\xA6"
			: "Add " + quoted(folderName(pickerFolder)) + " to\xE2\x80\xA6");
		for (int i = pickerScroll; i < n && i < pickerScroll + PICKER_SHOWN; i++)
		{
			Rect r = pickerRow(i);
			bool at = i == pickerCursor, down = pressedBtn == PICK_ROW + i;
			if (down) drawBox(r, PRESSED, R_BUTTON);
			else if (at) drawFocus(r);
			SDL_Color ink = at ? ACCENT : TEXT;
			int mid = r.y + r.h / 2;
			if (i == 0)
			{
				drawGlyphMid(G_PLUS, r.x + 31, mid, ink);
				drawTextMid("New playlist\xE2\x80\xA6", 1, ink, r.x + 56, mid, r.w - 68);
				continue;
			}
			auto& pl = pickerList[i - 1];
			// on the right: Add again? (asked), ✓ Added (it has the one file), else how many
			bool has = i - 1 < (int)pickerHas.size() && pickerHas[i - 1], armed = pickerArmed == i;
			std::string count = armed ? "Add again?" : has ? "Added" : std::to_string(pl.count) + (pl.count == 1 ? " item" : " items");
			int cw = (int)std::lround(textWidth(count, 0) / sx);
			drawTextMid(count, 0, armed ? ACCENT : at ? DIM_ON : DIM, 512, mid, 0, 1);
			if (has && !armed)
			{
				drawGlyphMid(G_CHECK, 512 - cw - 14, mid, ACCENT);
				cw += 24;
			}
			drawGlyphMid(G_PLAYLIST, r.x + 31, mid, ink);
			drawTextMid(pl.name, 1, ink, r.x + 56, mid, 512 - cw - 12 - (r.x + 56));
		}
		// where the list is, when it's longer than the box
		if (n > PICKER_SHOWN)
		{
			int top = pickerRow(pickerScroll).y, height = PICKER_SHOWN * 46 - 4;
			fillRound({532, top, 4, height}, SCREEN, 2);
			int h = std::max(16, height * PICKER_SHOWN / n);
			fillRound({532, top + (height - h) * pickerScroll / std::max(1, n - PICKER_SHOWN), 4, h}, DIM, 2);
		}
		drawHints({{"A", pickerArmed > 0 ? "Add again" : "Add"}, {"B", "Cancel"}});
	}

	void Ui::drawBusy()
	{
		Rect box{136, 132, 368, 168};
		drawDialog(box, "Reading folder\xE2\x80\xA6");
		drawTextMid(folderName(addPending.dir), 0, DIM, W / 2, box.y + 64, 328, 2);
		Rect c = buttonRect(BUSY_CANCEL);
		drawDialogRow(c, &G_CROSS, "Cancel", true, pressedBtn == BUSY_CANCEL, TEXT);
		drawHints({{"B", "Cancel"}});
	}

	void Ui::drawSource(const Player& p)
	{
		const PlaySource& src = p.source();
		if (src.id.empty()) return;
		// on the right where it comes from (drawn first: the left gets what's left): a playlist's item, the card or
		// the server it's on
		bool pl = src.kind == SourceKind::Playlist, net = isNetPath(p.currentPath());
		std::string right = !pl ? (src.kind == SourceKind::NetFolder ? "Network" : "Local") : !net ? std::string("Local") : "";
		if (pl && net)
		{
			NetPath np;
			const NetServer* sv = netStore && parseNetPath(p.currentPath(), np) ? netStore->find(np.server) : nullptr;
			right = sv ? serverName(*sv) : "Network";
		}
		int rw = std::min(200, (int)std::lround(textWidth(right, 0) / sx));
		drawTextMid(right, 0, DIM, 616, SOURCE_Y, 200, 1);
		int rightW = rw;
		if (pl)
		{
			const Glyph& g = net ? G_NETWORK : G_CARD;
			drawGlyphMid(g, 616 - rw - 8 - glyphW(g) / 2, SOURCE_Y, DIM);
			rightW += glyphW(g) + 8;
		}
		int maxW = 616 - 52 - rightW - 24;
		if (pl)
		{
			// the playlist's name, cut at its end (Liked: its heart)
			drawGlyphMid(plIsLiked(src.id) ? G_HEART : G_PLAYLIST, 35, SOURCE_Y, ACCENT);
			drawTextMid(src.title, 0, TEXT, 52, SOURCE_Y, maxW);
			return;
		}
		// the folder, as the browser says it, its end kept
		drawGlyphMid(src.kind == SourceKind::NetFolder ? G_NETWORK : G_CARD, 35, SOURCE_Y, DIM);
		std::string where = elideLeft(whereOf(src.id), 0, maxW);
		size_t cut = where.rfind(" / ");
		if (cut == std::string::npos) drawTextMid(where, 0, TEXT, 52, SOURCE_Y);
		else
		{
			std::string head = where.substr(0, cut + 3);
			drawTextMid(head, 0, DIM, 52, SOURCE_Y);
			drawTextMid(where.substr(cut + 3), 0, TEXT, 52 + (int)std::lround(textWidth(head, 0) / sx), SOURCE_Y);
		}
	}

	// ---- playlists edited from the web dashboard ----

	std::string Ui::plWebFile(const std::string& id) const
	{
		if (plStore && id == PlaylistStore::LIKED_ID) return plStore->likedFile();
		if (!plStore || id.empty() || id.find('/') != std::string::npos || id[0] == '.') return "";
		for (auto& i : plStore->list())
		{
			std::string stem = baseName(i.file);
			if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".m3u") == 0) stem.resize(stem.size() - 4);
			if (stem == id) return i.file;
		}
		return "";
	}

	void Ui::plWebEdited(const std::string& file, const std::vector<int>& remap, Player& p)
	{
		if (browseDir == plDirFor(file))
		{
			// a move or a drag under way here was of the list as it was: dropped, the list as it is now
			moving = moveFromMenu = false;
			plDrag.on = false;
			Playlist pl;
			int skipped = 0;
			if (plStore && plStore->load(file, pl, skipped))
			{
				plCur = pl;
				plLoaded = true;
				plDirty = false;
				plSel = remapSelection(plSel, remap);
				cursor = std::max(0, remapIndex(cursor, remap));
				plRefresh(p);
			}
			plTellPlayer(plCur, remap, p);
			selSerial++;
			return;
		}
		Playlist pl;
		int skipped = 0;
		if (plStore && plStore->load(file, pl, skipped)) plTellPlayer(pl, remap, p);
	}

	void Ui::plWebRenamed(const std::string& old, const std::string& file, const std::string& name, Player& p)
	{
		// as a rename on the handheld goes on (plNameDone)
		if (p.playing() && p.source().kind == SourceKind::Playlist && p.source().id == old) p.renameSource(old, name, file);
		if (plOrderId == old) plOrderId = file;
		if (p.resumeStore().lastFolder == old) p.setLastFolder(file);
		if (tabDir[TAB_PLAYLISTS] == plDirFor(old)) tabDir[TAB_PLAYLISTS] = plDirFor(file);
		if (page != Page::Browse) return;
		if (browseDir == plDirFor(old))
		{
			int c = cursor, s = scroll;
			enterFolder(plDirFor(file), p);
			cursor = c;
			scroll = s;
			fitList();
		}
		else plWebListChanged(p);
	}

	void Ui::plWebDeleted(const std::string& file, Player& p)
	{
		// (its edits not written: never, now)
		if (plCur.file == file) plDirty = false;
		if (p.resumeStore().lastFolder == file) p.setLastFolder("");
		if (tabDir[TAB_PLAYLISTS] == plDirFor(file)) tabDir[TAB_PLAYLISTS] = PLROOT;
		if (browseDir == plDirFor(file))
		{
			selEnd();
			moving = false;
			plDrag.on = false;
			ctxOpen = false;
			enterFolder(PLROOT, p);
		}
		else plWebListChanged(p);
	}

	void Ui::plWebListChanged(Player& p)
	{
		// the list of playlists on show: read again, the cursor on the same one
		if (browseDir != PLROOT || ctxOpen || kbOpen) return;
		std::string at = cursor >= 0 && cursor < (int)browsePaths.size() ? browsePaths[cursor] : "";
		int s = scroll;
		enterFolder(PLROOT, p);
		for (size_t i = 0; i < browsePaths.size(); i++)
			if (browsePaths[i] == at) cursor = (int)i;
		scroll = s;
		fitList();
	}
}
