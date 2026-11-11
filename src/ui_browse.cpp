#include "ui.h"
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
#include "durworker.h"

namespace bp
{
	void Ui::startBrowser(Player& p, const std::string& folder)
	{
		roots = browseRoots();
		page = Page::Browse;
		onPlayer = false;
		if (!folder.empty())
		{
			browseTab = isNetPath(folder) ? TAB_NETWORK : TAB_LOCAL;
			enterFolder(folder, p);
			return;
		}
		const std::string& last = p.resumeStore().lastFolder;
		struct stat st;
		// a playlist kept: the Playlists tab, in it (one deleted since: the tab's list)
		if (plStore && p.settings.rememberFolder && last.rfind(plStore->dir() + "/", 0) == 0)
		{
			browseTab = TAB_PLAYLISTS;
			enterFolder(stat(last.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? plDirFor(last) : std::string(PLROOT), p);
			return;
		}
		bool there = p.settings.rememberFolder && !last.empty() && stat(last.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
			(p.settings.showHiddenFolders || (!isHiddenFolder(last) && !insideHiddenFolder(last)));
		// a folder kept: Files, in it; else Home
		if (there) browseTab = TAB_LOCAL;
		enterFolder(there ? last : std::string(HOMEDIR), p);
	}

	void Ui::backFromPlayer(Player& p, const std::string& lastPlayed, const PlaySource& from, int index)
	{
		PlayerBack b = playerBack;
		playerBack = PlayerBack();
		bool menuGone = b.inMenu && b.menuTab == TAB_PLAYING && !(p.playing() && !p.playingPhoto());
		bool toSource = showSourceAfterLeave;
		showSourceAfterLeave = false;
		if (!b.set || !showAfterLeave.empty() || b.page == Page::Main || menuGone || toSource)
		{
			if (toSource) inMenu = false;
			showBrowser(p, lastPlayed, from, index);
			return;
		}
		// the page as it was left: nothing of the browser's was touched meanwhile but the cursor and the scroll, which
		// the player's pages share (and the menu's way back, which START over the player set again)
		onPlayer = false;
		// (the list it was on: back in it if a play moved it, a playlist's page say; the cursor and scroll as below)
		if (b.page == Page::Browse && b.browseDir != browseDir && (b.browseDir == HOMEDIR || pseudoDir(b.browseDir) || exists(b.browseDir)))
		{
			setSection(b.browseTab);
			enterFolder(b.browseDir, p);
		}
		page = b.page;
		inMenu = b.inMenu;
		menuTab = b.menuTab;
		setGroup = b.setGroup;
		cursor = b.cursor;
		scroll = b.scroll;
		menuReturnDir = b.menuReturnDir;
		menuReturnCursor = b.menuReturnCursor;
		menuReturnScroll = b.menuReturnScroll;
		bgPlay = p.playing();
		ctxOpen = false;
		touchLocked = false;	// the lock is the video's
		if (bgPlay) landFlashUntil = SDL_GetTicks() + 900;	// the mini bar's edge lit a moment: it's still playing
		dropTouch();
		// played from a playlist's page: back to the list of playlists, the cursor on that one (the owner's rule: Back
		// leaves the playlist; List on the player opens it, toSource above)
		if (page == Page::Browse && !inMenu && isPlDir(browseDir))
		{
			std::string file = plFileOf(browseDir);
			enterFolder(PLROOT, p);
			for (size_t i = 0; i < browsePaths.size(); i++)
				if (browsePaths[i] == file) { cursor = (int)i; break; }
			fitList();
		}
		// back in the folder of what played: the cursor on it (the photo last viewed, the song or video Next moved on to)
		else if (page == Page::Browse && !inMenu && !lastPlayed.empty())
			for (size_t i = 0; i < browsePaths.size(); i++)
				if (browsePaths[i] == lastPlayed)
				{
					cursor = (int)i;
					fitList();
					break;
				}
		fprintf(stderr, "ui: back from the player to %s\n", page == Page::Browse && !inMenu ? (atHome() ? "Home" : browseDir.empty() ? "Files" : browseDir.c_str())
			: inMenu ? "the menu" : "a page");
	}

	void Ui::showBrowser(Player& p, const std::string& lastPlayed, const PlaySource& from, int index)
	{
		page = Page::Browse;
		onPlayer = false;
		// (left while it plays: it goes on behind; stopped: nothing plays)
		bgPlay = p.playing();
		ctxOpen = false;
		if (roots.empty()) roots = browseRoots();	// (started with a file: the browser never read them)
		// its row lit a moment, and the mini bar's edge: it's still playing, here
		if (bgPlay) landFlashUntil = SDL_GetTicks() + 900;
		touchLocked = false;	// the lock is the video's
		dropTouch();
		if (inMenu && menuTab == TAB_PLAYING) inMenu = false;	// (its tab is what played: gone with it)
		if (!showAfterLeave.empty())
		{
			// the Playing tab's Show in folder: that folder, wherever it played from
			std::string path;
			path.swap(showAfterLeave);
			inMenu = false;
			showInFolder(path, p);
			return;
		}
		if (from.kind == SourceKind::Playlist && plStore)
		{
			// the playlist it played from, the cursor on the item (by the order it played in; else by its path)
			setSection(TAB_PLAYLISTS);
			if (!exists(from.id)) { enterFolder(PLROOT, p); return; }
			enterFolder(plDirFor(from.id), p);
			int item = from.id == plOrderId && index >= 0 && index < (int)plOrder.size() ? plOrder[index] : -1;
			if (item < 0 || item >= (int)browsePaths.size() || browsePaths[item] != lastPlayed)
			{
				item = -1;
				for (size_t i = 0; i < browsePaths.size() && item < 0; i++)
					if (browseRows[i].id == PL_ITEM && browsePaths[i] == lastPlayed) item = (int)i;
			}
			if (item >= 0) cursor = item;
			fitList();
			return;
		}
		// a file of a card's (from Home, Recent or a playlist too): its folder, in Files
		if (!isNetPath(lastPlayed) && !lastPlayed.empty()) setSection(TAB_LOCAL);
		if (isNetPath(lastPlayed))
		{
			// its network folder, listed again (the cursor goes to it once the listing comes)
			setSection(TAB_NETWORK);
			netSelect = lastPlayed;
			enterFolder(netParent(lastPlayed), p);
			return;
		}
		// listed again either way: positions changed, a file may have come or gone
		enterFolder(lastPlayed.empty() ? browseDir : parentFolder(lastPlayed), p);
		for (size_t i = 0; i < browsePaths.size(); i++)
			if (browsePaths[i] == lastPlayed) cursor = (int)i;
		fitList();
	}

	void Ui::dropTouch()
	{
		// a finger still down on what's gone presses nothing on what comes (as it lifts); none down: nothing to drop
		if (panTouch || listTouch || dragging || pressedBtn >= 0 || homeTouch) swallowTouch = true;
		panTouch = panMoved = listTouch = dragging = homeTouch = false;
		pressedBtn = homePress = -1;
		flickAt = tapAt = holdAt = homeHoldAt = 0;
	}

	void Ui::fitList()
	{
		int n = (int)browseRows.size(), first = listFirst();
		cursor = std::clamp(cursor, first, std::max(first, n - 1));
		// the cursor in the window, and no empty rows below the list's end while there are rows above
		scroll = std::clamp(scroll, std::max(first, cursor - ROWS_SHOWN + 1), std::min(cursor, std::max(first, n - ROWS_SHOWN)));
	}

	// the rows' ids: 1 a folder to go into, 2 a video to open, -1 nothing (a message)
	void Ui::enterFolder(std::string dir, Player& p)
	{
		// the playlist's edits written before another page reads it (or the list counts it)
		plFlush(p);
		placeIn[browseDir] = {cursor, scroll};
		netOpenPending.clear();	// gone elsewhere first: not opened
		if (browseDir == FINDNET && dir != FINDNET && discovery) discovery->stop();
		browseDir = dir;
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& path, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back(path);
		};
		auto at = [&](const std::string& path) { return resumeNote(path, p); };
		// a local file's note: its length too when it's known; else asked of the worker (the note redone as it answers)
		std::vector<std::pair<int, std::string>> durWant;
		auto localNote = [&](const std::string& path) {
			long long size = -1;
			double secs = -1;
			if (!cachedDuration(path, secs, &size) && size > 0 && (isAudioFile(path) || isVideoFile(path)) && durationInHeader(path))
				durWant.emplace_back((int)browseRows.size(), path);
			return fileNote(at(path), secs, size);
		};
		netLoading = false;
		plMissing.clear();
		plMissingSerial++;
		if (dir == HOMEDIR)
		{
			homeListedFor = p.currentPath();
			// the Recent strip listed again, the focus kept on the file it was on (gone: the card that took its place)
			std::string was = homeStrip < (int)recent.size() ? recent[homeStrip].path : "";
			int wasAt = homeStrip;
			listRecent(p, true);
			if (homeFresh)
			{
				homeFresh = false;
				homeZone = recent.empty() ? HOME_GRID : HOME_STRIP;
				homeStrip = homeGrid = homeCol = 0;
			}
			else
			{
				int found = -1;
				for (size_t i = 0; i < recent.size() && !was.empty(); i++)
					if (recent[i].path == was) found = (int)i;
				homeStrip = was.empty() ? stripCount() - 1 : found >= 0 ? found : std::min(wasAt, std::max(0, (int)recent.size() - 1));
				if (recent.empty() && homeZone == HOME_STRIP)
				{
					homeZone = HOME_GRID;
					homeGrid = homeCol = 0;
				}
			}
			homeStrip = std::clamp(homeStrip, 0, stripCount() - 1);
			stripOff = stripTarget(homeStrip);
			stripT0 = 0;
		}
		// the drawer draws itself (ui_ext.cpp): its one row only keeps the list's counts sane
		else if (dir == EXTDIR) add(-1, mangaOn ? "1 on" : "None on", "", "");
		else if (dir == PLROOT)
		{
			// the saved playlists, by name
			// Liked first, then the user's by name, then New playlist
			if (!plStore) add(-1, "Playlists aren't available", "", "");
			else
			{
				for (auto& pl : plStore->list())
					add(PL_ROW, pl.name, pl.file, pl.builtin && !pl.count ? "Empty" : std::to_string(pl.count) + (pl.count == 1 ? " item" : " items"));
				add(PL_NEW, "New playlist", "", "");
			}
		}
		else if (isPlDir(dir))
		{
			// a playlist's items, in its order (read again each time: another page may have changed it)
			plCur = Playlist();
			int skipped = 0;
			plLoaded = plStore && plStore->load(plFileOf(dir), plCur, skipped);
			if (!plLoaded) plCur.file = plFileOf(dir);
			plRows(p);
		}
		else if (dir == FORMDIR) formRows();
		else if (dir == FINDNET)
		{
			// a search from scratch each time it's opened (Search again too)
			found.clear();
			if (discovery && !discovery->running()) discovery->start(onFound, 4);
			findWasRunning = discovery && discovery->running();
			findRows();
		}
		else if (dir == NETROOT)
		{
			// the saved connections, the most recently used first
			std::vector<NetServer> list = netStore ? netStore->servers() : std::vector<NetServer>();
			std::stable_sort(list.begin(), list.end(), [](const NetServer& a, const NetServer& b) { return a.lastUsed > b.lastUsed; });
			for (auto& sv : list)
			{
				std::string start = "/" + sv.share + (sv.share.empty() || sv.folder.empty() ? "" : "/" + sv.folder);
				add(4, serverName(sv), makeNetPath(sv.id, start), std::string(netTypeLabel(sv.type)) + "  " + sv.host);
			}
			if (browseRows.empty()) add(-1, "No connections yet", "", "");
		}
		else if (isNetPath(dir))
		{
			// asked of the worker; "Loading..." till its answer (netListed)
			NetPath np;
			parseNetPath(dir, np);
			const NetServer* sv = netStore ? netStore->find(np.server) : nullptr;
			if (!sv) add(-1, "This connection is gone", "", "");
			else if (!netWorker) add(-1, "No network support in this build", "", "");
			else
			{
				netLoading = true;
				add(-1, "Loading...", "", "");
				netWorker->list(++netSerial, *sv, netStore->password(*sv), dir);
			}
		}
		else if (dir.empty())
			for (auto& r : roots) add(1, r.label, r.path, r.path);	// Files' top: the places (Recent is Home's)
		else if (dir == RECENT && !p.settings.keepHistory) add(-1, "Play history is off (Settings)", "", "");
		else if (dir == RECENT)
		{
			// All recent: the history (a file taken away or on a card taken out left out); X clears it. Manga's card
			// is Home's only: this list is the files played
			listRecent(p, false);
			for (auto& it : recent) add(2, baseName(it.path), it.path, it.net ? fileNote(at(it.path), -1) : localNote(it.path));
		}
		else
		{
			bool ok;
			for (auto& e : listFolder(dir, ok, p.settings.showHiddenFolders))
				add(e.dir ? 1 : 2, e.name, e.path, e.dir ? (isHiddenFolder(e.path) ? "hidden" : "") : localNote(e.path));
			if (!ok) add(-1, "This folder can't be opened", "", "");
		}
		if (browseRows.empty() && !isPlDir(dir) && dir != HOMEDIR) add(-1, "Nothing to play here", "", "");
		auto it = placeIn.find(dir);
		cursor = it == placeIn.end() ? firstCursor() : std::min(it->second.first, (int)browseRows.size() - 1);
		scroll = it == placeIn.end() ? listFirst() : it->second.second;
		// Recent opens at its top: the most recent first, and a place in a list that changed since means little
		if (dir == RECENT) cursor = scroll = 0;
		fitList();
		browseVersion++;
		// (an empty list too: the last folder's probes stop)
		if (durWorker) durWorker->want(++durSerial, std::move(durWant));
		durLo = -1;
	}

	std::string Ui::resumeNote(const std::string& path, Player& p)
	{
		const ResumeEntry* e = p.resumeStore().find(path);
		// a song's (kept by an earlier version) left out: songs start from their start
		return e && e->position >= 0 && !isAudioFile(path) ? "at " + formatTime(e->position) : std::string();
	}

	void Ui::durationsReady(Player& p)
	{
		if (!durWorker) return;
		bool shown = false;
		for (auto& r : durWorker->take())
		{
			// a folder left since, or its rows listed again: dropped (the cache has it for next time)
			if (r.serial != durSerial || r.secs <= 0 || r.row < 0 || r.row >= (int)browseRows.size() || browsePaths[r.row] != r.path) continue;
			browseRows[r.row].note = fileNote(resumeNote(r.path, p), r.secs, fileSize(r.path));
			if (r.row >= scroll && r.row < scroll + ROWS_SHOWN) shown = true;
		}
		// only a row on screen needs a redraw: the others are drawn as they scroll in
		if (shown) browseVersion++;
	}

	void Ui::netListed(NetListing* l, Player& p)
	{
		std::unique_ptr<NetListing> own(l);
		// a playlist item's folder (tick asked): the player's, before anything of the browser's
		if (auto side = sidePending.find(l->serial); !l->test && side != sidePending.end())
		{
			sidePending.erase(side);
			p.netFolderListed(l->dir, l->names, l->result.ok());
			return;
		}
		// a folder to add to a playlist, read first: added now (an older one, cancelled, is nothing)
		if (!l->test && l->serial > (1 << 29) && l->serial < (1 << 30))
		{
			if (l->serial != addPending.serial) return;
			AddPending a = addPending;
			addPending = {};
			if (!l->result.ok())
			{
				NetPath np;
				const NetServer* sv = netStore && parseNetPath(l->dir, np) ? netStore->find(np.server) : nullptr;
				// (not found: the folder, most likely; the server answered its parent's listing)
				p.osd(l->result.error == NetError::NotFound ? "Can't find " + quoted(folderName(l->dir))
					: netErrorText(l->result.error, sv ? sv->host : "the server"));
				return;
			}
			if (netNames.size() > 32) netNames.clear();
			netNames[l->dir] = l->names;
			int photos = 0;
			auto add = folderEntries(l->dir, l->names, photos);
			addTo(a.dest, add, photos, folderName(l->dir), a.back, p);
			return;
		}
		if (l->test)
		{
			// Test connection's answer (the form's row, a connection's menu); an older test's is nothing
			if (l->serial != testSerial) return;
			testText = l->result.ok() ? "It works" : netErrorText(l->result.error, "The server");
			ctxStatus = testText;
			if (browseDir == FORMDIR) formRows();
			browseVersion++;
			return;
		}
		// the folder of a file to open, listed first: opened now, with what the folder has (alone if it failed)
		if (!l->test && !netOpenPending.empty() && l->serial == netSerial && l->dir == netParent(netOpenPending))
		{
			if (l->result.ok()) netNames[l->dir] = l->names;
			openRequest = netOpenPending;
			netOpenPending.clear();
			return;
		}
		if (l->test || l->serial != netSerial || l->dir != browseDir || page != Page::Browse) return;
		netLoading = false;
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& path, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back(path);
		};
		NetPath np;
		parseNetPath(l->dir, np);
		const NetServer* sv = netStore ? netStore->find(np.server) : nullptr;
		if (!l->result.ok())
		{
			add(-1, netErrorText(l->result.error, sv ? sv->host : "the server"), "", "");
			add(6, "Try again", l->dir, "");
		}
		else
		{
			// a few folders' worth (Recent, Back to one): opening a file outside them plays it alone
			if (netNames.size() > 32) netNames.clear();
			netNames[l->dir] = l->names;
		}
		if (l->result.ok())
			for (auto& e : l->entries)
			{
				std::string path = netJoin(l->dir, e.name);
				const ResumeEntry* r = p.resumeStore().find(path);
				std::string note = !e.dir && r && r->position >= 0 && !isAudioFile(e.name) ? "at " + formatTime(r->position) : "";
				add(e.dir ? 1 : 2, e.name, path, e.dir ? note : fileNote(note, e.size));
			}
		if (browseRows.empty()) add(-1, "Nothing to play here", "", "");
		auto it = placeIn.find(l->dir);
		cursor = it == placeIn.end() ? firstCursor() : it->second.first;
		scroll = it == placeIn.end() ? listFirst() : it->second.second;
		if (!netSelect.empty())
			for (size_t i = 0; i < browsePaths.size(); i++)
				if (browsePaths[i] == netSelect) cursor = (int)i;
		netSelect.clear();
		fitList();
		browseVersion++;
	}

	bool Ui::emptyAction() const
	{
		// (an empty playlist's: Add files)
		bool emptyPl = isPlDir(browseDir) && plLoaded && plStore && plCur.entries.empty() && !plIsLiked(plCur.file);
		return page == Page::Browse && !inMenu && selMode == SEL_NONE &&
			((browseDir == NETROOT) || (browseDir == PLROOT && plStore) || emptyPl) && browseRows.size() == 1 && browseRows[0].id < 0;
	}

	void Ui::doEmptyAction(Player& p)
	{
		if (browseDir == PLROOT) plNameOpen(false, "", "");
		else if (isPlDir(browseDir))
		{
			// Add files: the cards (where the Local tab was), the files ticked there added to this one
			selStart(SEL_ADD, plCur.file);
			switchTab(TAB_LOCAL, p);
		}
		else openForm(nullptr, nullptr, p);
	}

	bool Ui::atFirstPage() const
	{
		return browseDir.empty() || browseDir == NETROOT || browseDir == PLROOT || browseDir == EXTDIR || browseDir == RECENT;
	}

	bool Ui::atHome() const
	{
		return browseDir == HOMEDIR;
	}

	void Ui::setSection(int tab)
	{
		if (tab == browseTab) return;
		// (Home and All recent are no section's)
		if (!atHome() && browseDir != RECENT) tabDir[browseTab] = browseDir;
		browseTab = tab;
	}

	void Ui::switchTab(int tab, Player& p)
	{
		if (tab == browseTab && !atHome() && browseDir != RECENT) return;
		setSection(tab);
		std::string dir = tabDir[tab];
		if (dir.empty()) dir = sectionTop(tab);
		enterFolder(dir, p);
	}

	void Ui::openSection(int tab, Player& p)
	{
		if (tab < 0 || tab >= BROWSE_TABS) return;
		setSection(tab);
		// where it was left (L/R, a tile, a marker), as switching back and forth shouldn't lose the place; its top if
		// that folder or playlist is gone. A network place is kept as it was: its listing says if it's not there
		std::string dir = tabDir[tab];
		struct stat st;
		bool gone = dir.empty() || (isPlDir(dir) && (stat(plFileOf(dir).c_str(), &st) != 0 || !S_ISREG(st.st_mode))) ||
			(!pseudoDir(dir) && !isNetPath(dir) && (stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)));
		// (Liked is a playlist whose file may not be written yet)
		if (gone && isPlDir(dir) && plStore && plStore->isBuiltinFile(plFileOf(dir))) gone = false;
		enterFolder(gone ? sectionTop(tab) : dir, p);
	}

	bool Ui::upRow() const
	{
		if (page != Page::Browse || atHome() || atFirstPage() || browseDir == FORMDIR || browseDir == FINDNET) return false;
		// editing a playlist's items, moving them: the list is what's being worked on
		return selMode != SEL_EDIT && !moving;
	}

	int Ui::firstCursor() const
	{
		// a folder opened for the first time: on its first item; an empty one (a message alone): on Up, unless the
		// page's own button (Add files) is what A should do there
		bool empty = browseRows.empty() || (browseRows.size() == 1 && browseRows[0].id < 0);
		return empty && !emptyAction() ? listFirst() : 0;
	}

	bool Ui::folderRow(int row) const
	{
		// a folder of the cards' (not the first page's places, nor Recent)
		return !browseDir.empty() && !pseudoDir(browseDir) && !isNetPath(browseDir) && row >= 0 && row < (int)browseRows.size() && browseRows[row].id == 1;
	}

	std::string Ui::browseSelection() const
	{
		if (atHome())
		{
			if (homeZone == HOME_GRID) return SECTION_NAMES[homeGrid];
			return homeStrip < (int)recent.size() ? recent[homeStrip].path : "All recent";
		}
		if (cursor < 0) return upRow() ? ".." : "";
		return cursor < (int)browsePaths.size() ? browsePaths[cursor] : "";
	}

	std::vector<std::string> Ui::browseLabels() const
	{
		std::vector<std::string> out;
		if (atHome())
		{
			// the Recent cards (shown: in the strip's view now), then the tiles: "card" or "tile", its name, its line
			int off = (int)std::lround(stripOff);
			if (recent.empty()) out.push_back("shown    (no Recent: a line instead of the strip)");
			for (int i = 0; !recent.empty() && i < (int)recent.size() + 1; i++)
			{
				int x = 16 + i * STRIDE - off;
				bool at = homeZone == HOME_STRIP && homeStrip == i;
				std::string l = std::string(x + CARD_W > 0 && x < W ? "shown  " : "hidden ") + (at ? "> " : "  ") + "card ";
				if (i < (int)recent.size())
					l += recent[i].name + "  | " + recent[i].sub + (recent[i].net ? " [net]" : "") + " \xC2\xB7 " + recent[i].where;
				else l += "All recent  | " + std::to_string(recentPlayed()) + " played";
				out.push_back(l);
			}
			for (int g = 0; g < BROWSE_TABS; g++)
				out.push_back(std::string("shown  ") + (homeZone == HOME_GRID && homeGrid == g ? "> " : "  ") + "tile " + SECTION_NAMES[g] +
					"  | " + tileSub(g));
			return out;
		}
		if (upRow())
			out.push_back(std::string(scroll < 0 ? "shown  " : "hidden ") + (cursor < 0 ? "> " : "  ") + "[up] ..");
		for (size_t i = 0; i < browseRows.size(); i++)
		{
			std::string l = std::string((int)i >= scroll && (int)i < scroll + ROWS_SHOWN ? "shown  " : "hidden ") +
				(i == (size_t)cursor ? "> " : "  ") + browseRows[i].label;
			// a playlist's and its items' notes; an item that's missing is "[Gone] name", with none (checked here for
			// all of them)
			if (browseRows[i].id == PL_ROW || browseRows[i].id == PL_ITEM)
			{
				if (plMissingAt((int)i))
				{
					size_t at = l.size() - browseRows[i].label.size();
					l.insert(at, "[Gone] ");
					l += "  |";
				}
				else l += "  | " + browseRows[i].note;
			}
			// selecting: a ticked row (Edit's item, Add's file)
			if (selTicked((int)i)) l.insert(9, "[x] ");	// after "shown  > "
			out.push_back(l);
		}
		return out;
	}

	bool Ui::holdRow(int row) const
	{
		if (folderRow(row)) return true;
		if (row < 0 || row >= (int)browseRows.size()) return false;
		int64_t id = browseRows[row].id;
		if (id == PL_ROW || id == PL_ITEM) return true;
		// a file: selecting (it ticked), to add to a playlist; a server's folder: its menu (Add to playlist...)
		if (!plStore) return false;
		return (id == 2 && !isPlDir(browseDir) && !isPhotoFile(browsePaths[row])) || (id == 1 && isNetPath(browseDir));
	}

	void Ui::openBrowseMenu(int row, MenuHow how)
	{
		// (Home's and All recent's are their own: openHomeOptions, openRecentMenu; Extensions has none yet)
		if (browseDir == FORMDIR || browseDir == EXTDIR || atHome()) return;
		bool onRow = row >= 0 && row < (int)browseRows.size();
		if (browseDir == PLROOT || isPlDir(browseDir))
		{
			// Playlists: the tab's menu (X), a playlist's (Y or a hold on it, or X in it), an item's (Y or a hold)
			std::vector<int> items;
			std::string title, note, file;
			int item = -1;
			auto count = [](size_t n) { return std::to_string(n) + (n == 1 ? " item" : " items"); };
			if (browseDir == PLROOT && how == MENU_X)
			{
				if (!plStore) return;
				items = {CTX_PL_NEW, CTX_CANCEL};
				title = "Your playlists";
			}
			else if (browseDir == PLROOT)
			{
				if (!onRow || browseRows[row].id != PL_ROW) return;
				file = browsePaths[row];
				title = browseRows[row].label;
				note = browseRows[row].note;
				if (plIsLiked(file)) items = {CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_CANCEL};	// no rename or delete
				else items = {CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_RENAME, CTX_PL_DELETE, CTX_CANCEL};
			}
			else if (how == MENU_X)
			{
				if (!plLoaded) return;
				file = plCur.file;
				title = plCur.name;
				note = count(plCur.entries.size());
				// eight with items (the box steps its rows closer to fit, the count left to the page), four without
				if (plIsLiked(file))
				{
					// only the Like button adds to Liked; here: play, and remove (Select)
					if (plCur.entries.empty()) return;
					items = {CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_SELECT};
				}
				else
				{
					if (!plCur.entries.empty())
						items = {CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_ADD_FILES, CTX_PL_ADD_FOLDER, CTX_PL_SELECT, CTX_PL_DEDUP};
					else items = {CTX_PL_ADD_FILES, CTX_PL_ADD_FOLDER};
					items.push_back(CTX_PL_RENAME);
					items.push_back(CTX_PL_DELETE);
				}
			}
			else
			{
				if (!onRow || browseRows[row].id != PL_ITEM) return;
				file = plCur.file;
				item = row;
				title = browseRows[row].label;
				int miss = plMissingAt(row);
				if (miss)
				{
					note = plIsLiked(file) && miss == 1 ? "It's gone from the card" : miss == 1 ? "Not found. Is the card in?" : "Its connection was deleted";
					items = {CTX_PL_REMOVE, CTX_CANCEL};
				}
				else if (plIsLiked(file)) items = {CTX_PL_HERE, CTX_PL_REMOVE, CTX_PL_ADD_ONE, CTX_PL_SHOW, CTX_CANCEL};	// (Remove is Unlike)
				else items = {CTX_PL_HERE, CTX_PL_MOVE, CTX_PL_REMOVE, CTX_PL_SHOW, CTX_CANCEL};
			}
			ctxOpen = true;
			ctxCursor = 0;
			ctxItems = items;
			ctxServer.clear();
			ctxStatus.clear();
			ctxArmed = false;
			ctxPath.clear();
			ctxTitle = title;
			ctxNote = note;
			ctxPlFile = file;
			ctxPlItem = item;
			return;
		}
		ctxTitle.clear();
		ctxNote.clear();
		ctxPlFile.clear();
		ctxPlItem = -1;
		ctxFolder.clear();
		bool folder = folderRow(row);
		bool server = browseDir == NETROOT && row >= 0 && row < (int)browseRows.size() && browseRows[row].id == 4;
		bool netFolder = isNetPath(browseDir) && onRow && browseRows[row].id == 1;
		// X on the cards' pages and a server's folders: Select files (to add to a playlist)
		bool selectFiles = plStore && how == MENU_X && browseDir != NETROOT && browseDir != FINDNET && browseTab != TAB_PLAYLISTS;
		std::vector<int> items;
		std::string id;
		if (server && how != MENU_X)
		{
			// the connection's own: Edit, Test, Delete
			NetPath np;
			if (!parseNetPath(browsePaths[row], np)) return;
			id = np.server;
			items = {CTX_EDIT, CTX_TEST, CTX_DELETE, CTX_CANCEL};
		}
		else if (browseDir == NETROOT && how == MENU_X)
		{
			items = {CTX_ADD, CTX_FIND, CTX_REFRESH, CTX_CANCEL};
#ifdef BP_WEB
			items.insert(items.end() - 1, CTX_WEB);
#endif
		}
		else if (folder) items = {CTX_HIDE, CTX_REFRESH, CTX_CANCEL};
		else if (netFolder && how != MENU_X && plStore)
		{
			// a server's folder: its videos and songs added to a playlist
			items = {CTX_ADD_TO, CTX_CANCEL};
			ctxTitle = browseRows[row].label;
		}
		else if (how == MENU_X) items = {CTX_REFRESH, CTX_CANCEL};
		else return;	// Y, or a hold, on what has no menu of its own
		// a card's folder (Y, a hold): Add to playlist... first
		if (folder && how != MENU_X && plStore) items.insert(items.begin(), CTX_ADD_TO);
		// X on a card's pages: Show hidden folders, before Cancel
		if (how == MENU_X && !isNetPath(browseDir) && (browseDir.empty() || !pseudoDir(browseDir)))
			items.insert(items.end() - 1, CTX_HIDDEN);
		if (folder || netFolder) ctxFolder = browsePaths[row];
		if (selectFiles) items.insert(items.begin(), CTX_SELECT_FILES);
		ctxOpen = true;
		ctxCursor = 0;
		ctxItems = items;
		ctxServer = id;
		ctxStatus.clear();
		ctxArmed = false;
		ctxPath = folder && id.empty() ? browsePaths[row] : "";
	}

	void Ui::chooseBrowseMenu(int item, Player& p)
	{
		int what = item >= 0 && item < (int)ctxItems.size() ? ctxItems[item] : CTX_CANCEL;
		// (before the playlists' below: their numbers come after them)
		if (what == CTX_WEB) { ctxOpen = false; openWeb(); return; }
		if (what >= CTX_R_PLAY)
		{
			std::string path = ctxRecent;
			if (what == CTX_CLEAR_RECENT)
			{
				// twice; nothing to clear, nothing
				if (recent.empty()) return;
				if (!ctxArmed) { ctxArmed = true; return; }
				ctxOpen = false;
				p.clearHistory();
				p.osd("Recent cleared");
				goHome(HOME_GRID, 0, p);
				return;
			}
			if (what == CTX_HIDDEN)
			{
				// the menu stays up (its value changes); the page listed again under it, on the same row
				Settings st = p.settings;
				st.showHiddenFolders = !st.showHiddenFolders;
				p.setSettings(st);
				std::string at = cursor >= 0 && cursor < (int)browsePaths.size() ? browsePaths[cursor] : "";
				int c = cursor, s = scroll;
				enterFolder(browseDir, p);
				cursor = c;
				scroll = s;
				for (size_t i = 0; i < browsePaths.size(); i++)
					if (!at.empty() && browsePaths[i] == at) cursor = (int)i;
				fitList();
				return;
			}
			ctxOpen = false;
			switch (what)
			{
			case CTX_SETTINGS: openMainMenu(p, TAB_SETTINGS); break;
			case CTX_R_START:
				// (what plays: from its start, on the player)
				if (p.playing() && p.currentPath() == path)
				{
					// (a page playing it: from its start there, as the handheld's seeks reach it)
					p.seekFraction(0);
					if (!pageIsOut()) p.unpause();
					else noteSeek(p, 0);
					showPlayer();
					break;
				}
				p.forgetPosition(path);
				openFile(path, p);
				break;
			// the mini bar's
			case CTX_NP_NOW: goNowPlaying(p); break;
			case CTX_NP_PLAY: miniToggle(p); break;
			case CTX_NP_LIKE: likeNow(p); break;
			case CTX_NP_ADD: if (p.playing() && !p.playingPhoto()) openAddOne(p.currentPath(), p.title, p.loaded ? p.duration : -1, p); break;
			case CTX_NP_SHOW: if (p.playing() && !p.playingPhoto()) showInFolder(p.currentPath(), p); break;
			case CTX_NP_STOP: if (p.playing() && !p.playingPhoto()) stopPressed = true; break;
			case CTX_R_PLAY: openFile(path, p); break;
			case CTX_R_SHOW: showInFolder(path, p); break;
			case CTX_R_ADD:
			{
				// the one file: the playlists to add it to (none yet: a new one)
				std::string title;
				for (auto& x : recent)
					if (x.path == path) title = x.name;
				openAddOne(path, title, -1, p);
				break;
			}
			case CTX_R_LIKE:
			{
				std::string title;
				for (auto& x : recent)
					if (x.path == path) title = x.name;
				bool was = plStore && plStore->isLiked(path);
				if (setLikedPath(p, path, title, -1, !was) != was) likeCard(p, !was, title);
				break;
			}
			case CTX_R_REMOVE:
			{
				p.removeFromHistory(path);
				p.osd("Removed from Recent");
				// (the list again: Home keeps its place, the card after it in its slot)
				int c = cursor, s = scroll;
				enterFolder(browseDir, p);
				if (browseDir == RECENT)
				{
					cursor = c;
					scroll = s;
					fitList();
				}
				break;
			}
			default: break;
			}
			return;
		}
		if (what == CTX_PL_DELETE)
		{
			// twice (the files themselves stay where they are: the menu says so)
			if (!ctxArmed) { ctxArmed = true; return; }
			ctxOpen = false;
			plDelete(ctxPlFile, p);
			return;
		}
		if (what >= CTX_PL_NEW)
		{
			ctxOpen = false;
			std::string file = ctxPlFile;
			int at = ctxPlItem;
			// the playlist the menu is for: the one open, or one of the list's (read now)
			Playlist pl;
			int skipped = 0;
			bool open = isPlDir(browseDir) && plLoaded && file == plCur.file;
			if (open) pl = plCur;
			else if (!file.empty() && (!plStore || !plStore->load(file, pl, skipped))) { p.osd("Couldn't read the playlist"); return; }
			if (plIsLiked(file) && (what == CTX_PL_RENAME || what == CTX_PL_DELETE || what == CTX_PL_DEDUP || what == CTX_PL_ADD_FILES ||
				what == CTX_PL_ADD_FOLDER || what == CTX_PL_MOVE))
				return;	// Liked is managed by the Like button; Remove and Select there are Unlike
			switch (what)
			{
			case CTX_PL_NEW: plNameOpen(false, "", ""); break;
			case CTX_PL_RENAME: plNameOpen(true, file, pl.name); break;
			case CTX_PL_PLAY: case CTX_PL_SHUFFLE:
				if (pl.entries.empty()) p.osd(quoted(pl.name) + " is empty");
				else plPlay(pl, 0, what == CTX_PL_SHUFFLE, p);
				break;
			case CTX_PL_HERE: if (open && at >= 0 && at < (int)pl.entries.size()) plPlay(pl, at, false, p); break;
			case CTX_PL_REMOVE:
				if (open && at >= 0 && at < (int)plCur.entries.size())
				{
					auto remap = removeEntries(plCur.entries, {at});
					plSaved(p, remap);
				}
				break;
			case CTX_PL_DEDUP:
				if (open)
				{
					int removed = 0;
					auto remap = removeDuplicates(plCur.entries, removed);
					if (!removed) { p.osd("No duplicates"); break; }
					plSaved(p, remap);
					p.osd("Removed " + std::to_string(removed) + (removed == 1 ? " duplicate" : " duplicates"));
				}
				break;
			case CTX_PL_SHOW:
				if (open && at >= 0 && at < (int)pl.entries.size())
				{
					// its folder, in Files or Network, the cursor on it
					showInFolder(pl.entries[at].path, p);
				}
				break;
			case CTX_PL_ADD_FILES: case CTX_PL_ADD_FOLDER:
				// to the cards (where the Local tab was), ticking files or picking a folder for this one
				if (open)
				{
					selStart(what == CTX_PL_ADD_FILES ? SEL_ADD : SEL_PICK, file);
					switchTab(TAB_LOCAL, p);
				}
				break;
			case CTX_PL_SELECT: if (open) selStart(SEL_EDIT, ""); break;
			case CTX_PL_ADD_ONE:
				if (open && at >= 0 && at < (int)pl.entries.size())
					openAddOne(pl.entries[at].path, pl.entries[at].title, pl.entries[at].duration, p);
				break;
			case CTX_PL_MOVE:
				// the one item, moved at once (and back to the list as it was, after)
				if (open && at >= 0 && at < (int)plCur.entries.size())
				{
					selStart(SEL_EDIT, "");
					plSel = {at};
					moveFromMenu = true;
					moveStart(p);
				}
				break;
			case CTX_SELECT_FILES: selStart(SEL_ADD, ""); break;
			case CTX_ADD_TO: if (!ctxFolder.empty()) openPicker(ctxFolder, p); break;
			default: break;
			}
			return;
		}
		const NetServer* sv = netStore && !ctxServer.empty() ? netStore->find(ctxServer) : nullptr;
		if (what == CTX_TEST && sv)
		{
			// the menu stays up, the answer under its title
			startTest(*sv, netStore->password(*sv));
			ctxStatus = "Testing...";
			return;
		}
		if (what == CTX_DELETE && sv)
		{
			// twice: a saved password isn't typed again in a moment
			if (!ctxArmed) { ctxArmed = true; return; }
			std::string id = sv->id;
			netStore->remove(id);
			if (netWorker) netWorker->forget(id);
			ctxOpen = false;
			enterFolder(NETROOT, p);
			return;
		}
		ctxOpen = false;
		if (what == CTX_EDIT && sv) { openForm(sv, nullptr, p); return; }
		if (what == CTX_ADD) { openForm(nullptr, nullptr, p); return; }
		if (what == CTX_FIND) { enterFolder(FINDNET, p); return; }
		if (what == CTX_REFRESH)
		{
			// read again where it stands, the cursor kept (on the same file, if it's still there)
			std::string at = cursor >= 0 && cursor < (int)browsePaths.size() ? browsePaths[cursor] : "";
			int c = cursor, s = scroll;
			if (isNetPath(browseDir)) netSelect = at;	// (the listing comes later: netListed puts it there)
			enterFolder(browseDir, p);
			cursor = c;
			scroll = s;
			for (size_t i = 0; i < browsePaths.size(); i++)
				if (!at.empty() && browsePaths[i] == at) cursor = (int)i;
			fitList();
			return;
		}
		if (what != CTX_HIDE) return;	// Cancel
		std::string marker = ctxPath + "/.hidden";
		bool ok;
		if (isHiddenFolder(ctxPath)) ok = unlink(marker.c_str()) == 0;
		else
		{
			FILE* f = fopen(marker.c_str(), "w");
			ok = f && fclose(f) == 0;
		}
		if (!ok) p.osd("Couldn't change the folder (is the card read-only?)");
		int c = cursor, s = scroll;
		enterFolder(browseDir, p);	// again: it's gone from the list, or marked
		cursor = c;
		scroll = s;
		fitList();
	}

	void Ui::browseUp(Player& p)
	{
		if (atFirstPage()) return;
		if (isPlDir(browseDir))
		{
			// back to the playlists, on this one
			std::string file = plFileOf(browseDir);
			enterFolder(PLROOT, p);
			for (size_t i = 0; i < browsePaths.size(); i++)
				if (browsePaths[i] == file) cursor = (int)i;
			fitList();
			return;
		}
		// the form, the found servers: back to the connections (a form left unsaved)
		if (browseDir == FORMDIR || browseDir == FINDNET)
		{
			enterFolder(NETROOT, p);
			return;
		}
		if (isNetPath(browseDir))
		{
			// up a folder; from a server's top, its list of connections
			NetPath np;
			parseNetPath(browseDir, np);
			enterFolder(np.path == "/" ? std::string(NETROOT) : netParent(browseDir), p);
			return;
		}
		bool atRoot = browseDir == RECENT;
		for (auto& r : roots)
			if (r.path == browseDir) atRoot = true;
		enterFolder(atRoot ? "" : parentFolder(browseDir), p);
	}

	bool Ui::clearHistory(Player& p)
	{
		if (clearArmed && SDL_GetTicks() - clearArmed < QUIT_WINDOW_MS)
		{
			clearArmed = 0;
			p.clearHistory();
			p.osd("Play history cleared");
			return true;
		}
		clearArmed = SDL_GetTicks();
		return false;
	}

	std::string Ui::browseWhere() const
	{
		// where it is: "Videos / Show / Season 1" (from the first page's place it's in), "MY-PC / Media / Movies"
		std::string where = browseDir.empty() ? "Your cards" : browseDir == RECENT ? "Recent"
			: browseDir == NETROOT ? "Your connections" : browseDir == FORMDIR ? (formId.empty() ? "Add connection" : "Edit connection")
			: browseDir == FINDNET ? "On your network" : browseDir == PLROOT ? "Your playlists"
			: isPlDir(browseDir) ? "Playlists/" + plCur.name : browseDir;
		if (isNetPath(browseDir) || (!browseDir.empty() && !pseudoDir(browseDir))) return whereOf(browseDir);
		// the folders apart: "share / Movies"
		std::string out;
		for (char c : where) out += c == '/' ? std::string(" / ") : std::string(1, c);
		return out;
	}

	void Ui::drawTracks(const Player& p)
	{
		bool browse = page == Page::Browse;
		bool sel = browse && !inMenu && selMode != SEL_NONE;
		if (browse && !inMenu && !sel && atHome()) { drawHome(p); return; }
		if (sel) drawSelHeader(p);
		else if (browse && !inMenu)
		{
			// All recent: a page of Home's; else a section's header (its markers in place of tabs)
			if (browseDir == RECENT) drawHeader("Recent", p);
			else drawSectionHeader(p);
			if (browseDir == EXTDIR)
			{
				// the drawer: what's on, or none (its own drawing, ui_ext.cpp)
				drawExtensionsDrawer(p);
				return;
			}
			// where it is, its end kept; the folder it's in brighter. A playlist: how many items, on the right. A
			// section's top: what's in it
			if (atFirstPage())
			{
				int n = 0;
				for (auto& row : browseRows) n += row.id == PL_ROW || row.id == 4;
				bool cards = false, videos = false;
				for (auto& rt : roots)
				{
					cards = cards || (rt.label.size() >= 4 && rt.label.compare(rt.label.size() - 4, 4, "card") == 0);
					videos = videos || rt.label == "Videos";
				}
				std::string top = browseDir == RECENT ? "Newest first \xC2\xB7 the last 30"
					: browseDir.empty() ? (videos && cards ? "Videos and your cards" : cards ? "Your cards" : "Your folders")
					: browseDir == PLROOT ? (!plStore ? "" : n <= 1 ? "Liked"
						: "Liked \xC2\xB7 " + std::to_string(n - 1) + (n == 2 ? " playlist" : " playlists"))
					: !n ? ""
					: std::to_string(n) + (n == 1 ? " connection" : " connections");
				if (!top.empty()) drawTextMid(top, 0, DIM, 16, SUB_Y);
			}
			else
			{
				int whereW = 600;
				if (isPlDir(browseDir) && plLoaded && plIsLiked(plCur.file))
				{
					// Liked: its order, and how many (empty: the page says so)
					size_t n = plCur.entries.size();
					if (n) drawTextMid("Newest first \xC2\xB7 " + std::to_string(n) + (n == 1 ? " item" : " items"), 0, DIM, 16, SUB_Y);
					whereW = 0;
				}
				else if (isPlDir(browseDir) && plLoaded)
				{
					size_t n = plCur.entries.size();
					std::string count = std::to_string(n) + (n == 1 ? " item" : " items");
					drawTextMid(count, 0, DIM, 616, SUB_Y, 0, 1);
					whereW -= (int)std::lround(textWidth(count, 0) / sx) + 24;
				}
				if (whereW) drawWhere(16, whereW);
			}
		}
		else if (!browse) drawTracksHeader(p);
		auto r = rows(p);
		bool form = browse && browseDir == FORMDIR;
		bool noAudio = !browse && trackType() == TrackType::Audio && r.size() == 1 && r[0].id < 0;
		// ".." (up to the folder above): row -1, over the list (and over a message alone)
		bool up = browse && upRow();
		if (up && scroll < 0) drawUpRow(ROW_Y, cursor < 0);
		// a message alone (nothing here, loading, no connections): in the middle of the page, not as a row to choose
		if (browse && !form && r.size() == 1 && r[0].id < 0)
		{
			const std::string& m = r[0].label;
			bool net = browseDir == NETROOT || isNetPath(browseDir);
			bool pl = browseDir == PLROOT || isPlDir(browseDir);
			const Glyph& g = net ? G_NETWORK : browseDir == RECENT ? G_CLOCK : G_FOLDER;
			std::string sub = m == "No connections yet" ? "Play from a computer or a NAS on your Wi-Fi"
				: m == "Nothing to play here" ? "No videos, music or photos in this folder"
				: m == "This folder can't be opened" ? "Is the card still in?"
				: m == "Play history is off (Settings)" ? "Turn it on in Settings, Files" : "";
			std::string title = m == "Play history is off (Settings)" ? "Play history is off" : m;
			if (pl && isPlDir(browseDir) && plIsLiked(plCur.file) && plLoaded)
				drawEmpty(G_HEART_O, "Nothing liked yet", "Tap the heart at the top while a song or video plays,\n"
					"or press START \xE2\x80\xBA Like. Liked things gather here.", nullptr);
			else if (pl)
				drawEmpty(G_PLAYLIST, title, m == "No playlists yet" ? "Gather videos and songs to play in your order"
					: m == "Playlists aren't available" ? "There's no folder to keep them in"
					: m == "This playlist can't be opened" ? "Was it deleted?" : "Add videos and songs from your cards or the network",
					!emptyAction() ? nullptr : isPlDir(browseDir) ? "Add files" : "New playlist");
			else drawEmpty(g, title, sub, emptyAction() ? "Add connection" : nullptr);
		}
		else if (noAudio) drawEmpty(G_SPEAKER, "No audio in this file", "This video has no sound track", nullptr);
		else
			for (int i = std::max(0, scroll); i < (int)r.size() && i < scroll + ROWS_SHOWN; i++)
			{
				int y = ROW_Y + (i - scroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
				bool at = i == cursor;
				bool style = r[i].id == STYLE_ROW;
				// the subtitle style: apart from the tracks, a line above it
				if (style || (form && r[i].id == FORM_TEST)) fill({32, y - 3, 576, 1}, RULE);
				// selecting: a ticked row on a lighter ground; Move's block lit as the cursor is
				bool block = sel && ((moving && plSel.count(i)) || (plDrag.on && dragSel.count(i)));
				if (block) drawFocus({16, y, 608, ROW_H - 4});
				// back from the player: the playing row lit a moment (still playing, here)
				else if (at && browse && landFlashUntil && rowChip(i)) drawBox({16, y, 608, ROW_H - 4}, PRESSED, R_BUTTON, true);
				else if (at && !(sel && plDrag.on)) drawFocus({16, y, 608, ROW_H - 4});
				else if (sel && selTicked(i)) drawBox({16, y, 608, ROW_H - 4}, SURFACE, R_BUTTON);
				if (browse && r[i].id == PL_ITEM)
				{
					drawPlRow(i, mid, at || block);
					continue;
				}
				int noteW = 0;
				// Recent's Clear row says how it stands now (the list was made before the first press expired)
				std::string note = browse && r[i].id == 3 ? (clearArmed ? "Press again to clear" : "Press twice") : r[i].note;
				bool placeholder = !note.empty() && note[0] == DIM_NOTE;
				if (placeholder) note.erase(0, 1);
				SDL_Color soft = at ? DIM_ON : DIM;
				if (!note.empty())
				{
					// a resume time and a size, a count, a root's path: right-aligned and short; a form's value, longer
					int w = form ? 380 : 260;
					SDL_Color c = clearArmed && r[i].id == 3 ? ACCENT : form && !placeholder ? (r[i].id == FORM_TEST && note == "It works" ? ACCENT : TEXT) : soft;
					// a path keeps its end
					std::string shown = !form && note[0] == '/' ? elideLeft(note, 0, w) : note;
					drawTextMid(shown, form ? 1 : 0, c, 608, mid, w, 1);
					noteW = form ? std::min(w, (int)std::lround(textWidth(shown, 1) / sx)) + 20 : std::min(w, (int)std::lround(textWidth(shown, 0) / sx)) + 16;
				}
				int textX = 32;
				// adding: a file's box (none for a photo: it can't go in, dimmed; nor a folder), the marks after it
				bool boxes = sel && selMode == SEL_ADD;
				bool inert = sel && r[i].id == 2 && !selTickable(i);
				if (boxes && selTickable(i)) drawGlyphMid(selTicked(i) ? G_BOX_ON : G_BOX, 40, mid, selTicked(i) ? ACCENT : DIM);
				if (browse)
				{
					// what the row is, as a mark before its name
					RowIcon icon = rowIcon(i);
					if (icon != ICON_NONE)
					{
						drawRowIcon(icon, boxes ? 60 : 32, mid - 10, icon == ICON_LIKED ? ACCENT : inert || (icon == ICON_NEW && !at) ? DIM
							: at ? ACCENT : TEXT);
						textX = boxes ? 94 : 66;
					}
					// a liked file: a small heart before its note
					if (r[i].id == 2 && plStore && i < (int)browsePaths.size() && plStore->isLiked(browsePaths[i]))
					{
						drawGlyphMid(G_HEART_S, noteW ? 608 - noteW : 601, mid, ACCENT);
						noteW += 22;
					}
					// the file that plays: its chip, as the dashboard's lists mark it
					if (!sel)
						if (int k = rowChip(i)) noteW += drawChip(608 - noteW, mid, k) + 10;
				}
				else if (style)
				{
					drawGlyphMid(G_GEAR, 43, mid, at ? ACCENT : TEXT);
					drawGlyphMid(G_RIGHT, 604, mid, at ? ACCENT : DIM);
					textX = 66;
				}
				else if (r[i].id >= 0)
				{
					// the track in use: a filled ring (the cursor is only where it is)
					drawGlyphMid(r[i].selected ? G_RADIO_ON : G_RADIO, 41, mid, r[i].selected ? ACCENT : at ? TEXT : DIM);
					textX = 66;
				}
				SDL_Color ink = (r[i].id < 0 && !style) || inert ? soft : at ? ACCENT : r[i].id == PL_NEW ? DIM_ON : TEXT;
				drawTextMid(r[i].label, 1, ink, textX, mid, 600 - textX - noteW);
			}
		// a dragged row: where it lands, a line in the gap (none while it would stay where it is)
		if (sel && plDrag.on && !dragSel.empty() && dragTarget() != *dragSel.begin())
		{
			int slot = dragSlot();
			if (slot >= scroll && slot <= scroll + ROWS_SHOWN) fillRound({16, ROW_Y + (slot - scroll) * ROW_H - 3, 608, 2}, ACCENT, 1);
		}
		int n = (int)r.size(), first = browse ? listFirst() : 0;
		drawScrollBar(ROW_Y, ROWS_SHOWN * ROW_H - 4, scroll - first, ROWS_SHOWN, n - first);
		if (!browse)
		{
			// the neighbours' keys, as on their tabs; on Subtitle style A opens it
			static const char* TAB_NAMES[4] = {"Chapters", "Subtitles", "Audio", "Picture"};
			const char* verb = cursor >= 0 && cursor < n && r[cursor].id == STYLE_ROW ? "Style" : "Choose";
			std::vector<Hint> h;
			if (!noAudio) h.push_back({"A", verb});
			auto tabs = tracksTabs(p);
			auto it = std::find(tabs.begin(), tabs.end(), tracksTab);
			if (it != tabs.end() && it != tabs.begin()) h.push_back({"L", TAB_NAMES[*(it - 1)]});
			if (it != tabs.end() && it + 1 != tabs.end()) h.push_back({"R", TAB_NAMES[*(it + 1)]});
			h.push_back({"B", "Back"});
			drawHints(h);
			return;
		}
		if (sel) { drawSelBar(); return; }
		// B at a section's top goes Home; L/R to the sections either side
		const char* back = atFirstPage() ? "Home" : "Back";
		bool plEmpty = r.size() == 1 && r[0].id < 0;
		// a row with a menu of its own (Y): a folder, a connection, a playlist, its item
		bool yMenu = cursor >= 0 && cursor < n && (folderRow(cursor) || (browseDir == NETROOT && r[cursor].id == 4) || r[cursor].id == PL_ROW ||
			r[cursor].id == PL_ITEM || (isNetPath(browseDir) && r[cursor].id == 1 && plStore));
		bool file = cursor >= 0 && cursor < n && (r[cursor].id == 2 || r[cursor].id == PL_ITEM);
		if (cursor < 0 && upRow()) drawHints({{"A", "Up"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}});
		else if (form) drawHints({{"A", "Change"}, {"LEFT/RIGHT", "Choose"}, {"B", "Cancel"}});
		else if (browseDir == FINDNET) drawHints({{"A", "Add this one"}, {"B", "Back"}});
		else if (browseDir == RECENT && plEmpty) drawHints({{"B", "Home"}, {"X", "Options"}, {"START", "Menu"}});
		else if (browseDir == RECENT) drawHints({{"A", "Play"}, {"Y", "Menu"}, {"B", "Home"}, {"X", "Options"}, {"START", "Menu"}});
		else if (plEmpty && isPlDir(browseDir) && plIsLiked(plCur.file)) drawHints({{"B", back}, {"L/R", "Sections"}, {"START", "Menu"}});
		else if (browseDir == PLROOT && cursor >= 0 && cursor < n && r[cursor].id == PL_NEW)
			drawHints({{"A", "New playlist"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}});
		else if (emptyAction() && browseDir == PLROOT)
			drawHints({{"A", "New playlist"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}, {"START", "Menu"}});
		else if (emptyAction() && isPlDir(browseDir)) drawHints({{"A", "Add files"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}});
		else if (emptyAction())
			drawHints({{"A", "Add connection"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}, {"START", "Menu"}});
		else if (plEmpty) drawHints({{"B", back}, {"X", "Options"}, {"L/R", "Sections"}, {"START", "Menu"}});
		else if (yMenu) drawHints({{"A", file ? (rowChip(cursor) ? "Go to it" : "Play") : "Open"}, {"B", back}, {"X", "Options"}, {"Y", "Menu"}, {"L/R", "Sections"}});
		else drawHints({{"A", file ? (rowChip(cursor) ? "Go to it" : "Play") : "Open"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}});
	}

	void Ui::drawUpRow(int y, bool at)
	{
		int mid = y + (ROW_H - 4) / 2;
		if (at) drawFocus({16, y, 608, ROW_H - 4});
		drawGlyphMid(G_UP, 43, mid, at ? ACCENT : DIM);
		drawTextMid("..", 1, at ? ACCENT : TEXT, 66, mid, 534);
	}

	void Ui::drawPlRow(int i, int mid, bool at)
	{
		const std::string& path = browsePaths[i];
		int miss = plMissingAt(i);
		SDL_Color soft = at ? DIM_ON : DIM;
		// selecting: its box where its number was, its grip on the right (Move's block: its arrows, yellow)
		bool edit = selMode == SEL_EDIT && !inMenu;
		bool block = edit && ((moving && plSel.count(i)) || (plDrag.on && dragSel.count(i)));
		// (Liked has no numbers: its order is the likes', newest first)
		bool liked = plIsLiked(plCur.file);
		if (edit) drawGlyphMid(plSel.count(i) ? G_BOX_ON : G_BOX, 40, mid, plSel.count(i) ? ACCENT : soft);
		else if (!liked) drawTextMid(std::to_string(i + 1), 0, soft, 56, mid, 0, 1);
		if (edit) drawGlyphMid(block ? G_MOVE : G_GRIP, 600, mid, block ? ACCENT : soft);
		int right = edit ? 572 : 608;
		// its number in the playlist, its mark (an alert when it isn't there), its name, where it is
		const Glyph& g = miss ? G_ALERT : isAudioFile(path) ? G_NOTE : G_FILM;
		int gx = liked && !edit ? 43 : 64 + 11;
		drawGlyphMid(g, gx, mid, miss ? DIM : at ? ACCENT : TEXT);
		// a missing one says so in its name, "[Gone]" (why: its menu's line), not on the right
		std::string note = miss ? "" : browseRows[i].note;
		int noteW = 0;
		if (!note.empty())
		{
			drawTextMid(note, 0, miss ? DANGER : soft, right, mid, 220, 1);
			int w = std::min(220, (int)std::lround(textWidth(note, 0) / sx));
			noteW = w + 16;
			// a network item: the server's name, marked as one
			if (!miss && isNetPath(path))
			{
				drawGlyphMid(G_NETWORK, right - w - 19, mid, DIM);
				noteW += 30;
			}
		}
		// the item that plays (this playlist playing): its chip
		if (!edit)
			if (int k = rowChip(i)) noteW += drawChip(right - noteW, mid, k) + 10;
		int x = liked && !edit ? 66 : 96;
		if (miss)
		{
			static const std::string GONE = "[Gone] ";
			drawTextMid(GONE, 1, DANGER, x, mid);
			x += (int)std::lround(textWidth(GONE, 1) / sx);
		}
		drawTextMid(browseRows[i].label, 1, miss ? soft : at ? ACCENT : TEXT, x, mid, std::max(0, right - 8 - x - noteW));
	}

	void Ui::drawTracksHeader(const Player& p)
	{
		tabsRight = std::min(630, statusLeft(p) - 10);
		drawBackIcon();
		// the tabs, in their order: the open one as START's menu draws its tab; its neighbours with their key on their
		// outer side (L the one before, R the one after: Y and X do the same), the name centred in the rest
		static const char* FULL[4] = {"Chapters", "Subtitles", "Audio", "Picture"};
		static const char* SHORT[4] = {"Chap.", "Subs", "Audio", "Picture"};
		auto tabs = tracksTabs(p);
		int cur = page == Page::Chapters ? TRACKS_CHAPTERS : tracksTab, at = 0;
		for (int i = 0; i < (int)tabs.size(); i++)
			if (tabs[i] == cur) at = i;
		// (four tabs beside the header's status: their names a size smaller where one wouldn't fit, then short ones,
		// all alike)
		int size = 1;
		const char* const* NAMES = FULL;
		auto fits = [&](const char* const* names, int sz) {
			for (int i = 0; i < (int)tabs.size(); i++)
				if (textWidth(names[std::clamp(tabs[i], 0, 3)], sz) / sx > tracksTabRect(i, (int)tabs.size()).w - 12) return false;
			return true;
		};
		if (!fits(FULL, 1)) size = 0;
		if (!fits(FULL, size)) NAMES = SHORT;
		for (int i = 0; i < (int)tabs.size(); i++)
		{
			Rect r = tracksTabRect(i, (int)tabs.size());
			const char* name = NAMES[std::clamp(tabs[i], 0, 3)];
			if (i == at) { drawTab(r, name, true, size); continue; }
			drawBox(r, BUTTON, R_BUTTON);
			const char* key = i == at - 1 ? "L" : i == at + 1 ? "R" : nullptr;
			int mid = r.y + r.h / 2, left = r.x, right = r.x + r.w;
			// (the key where the name leaves room for it: the footer's hints name it too)
			int kw = key ? (int)std::lround(textWidth(key, 0) / sx) : 0, nw = (int)std::lround(textWidth(name, size) / sx);
			if (key && p.settings.buttonHotkeys && 14 + kw + 8 + nw + 8 <= r.w)
			{
				if (i < at)
				{
					drawTextMid(key, 0, ACCENT, r.x + 14, mid);
					left = r.x + 14 + kw;
				}
				else
				{
					drawTextMid(key, 0, ACCENT, r.x + r.w - 14, mid, 0, 1);
					right = r.x + r.w - 14 - kw;
				}
			}
			drawTextMid(name, size, TEXT, (left + right) / 2, mid, right - left - 8, 2);
		}
		drawBattery(616, 24, p);
	}

	void Ui::folderChanged(const std::string& dir, Player& p)
	{
		std::string d = dir;
		while (d.size() > 1 && d.back() == '/') d.pop_back();
		if (page != Page::Browse || browseDir != d || pseudoDir(d) || isNetPath(d)) return;
		// not under a menu, the picker or a move (their rows are the list's as it was)
		if (ctxOpen || pickerOpen || addPending.serial || moving || plDrag.on || kbOpen) return;
		std::string at = cursor >= 0 && cursor < (int)browsePaths.size() ? browsePaths[cursor] : "";
		int s = scroll;
		enterFolder(d, p);
		for (size_t i = 0; i < browsePaths.size(); i++)
			if (browsePaths[i] == at) cursor = (int)i;
		scroll = s;
		fitList();
		browseVersion++;
	}
}
