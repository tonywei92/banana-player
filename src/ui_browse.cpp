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
#include "ui_browse.h"
#include "ui_player.h"
#include "ui_playlists.h"
#include "ui_home.h"
#include "ui_docs.h"
#include "ui_manga.h"
#include "pagebook.h"
#include "durworker.h"

namespace bp
{
	void Ui::rootsCheck(Player& p)
	{
		Uint32 now = SDL_GetTicks();
		if (roots.empty() || now - rootsCheckedAt < 2000) return;
		rootsCheckedAt = now;
		std::vector<BrowseRoot> fresh = browseRoots();
		bool same = fresh.size() == roots.size();
		for (size_t i = 0; same && i < fresh.size(); i++) same = fresh[i].path == roots[i].path;
		if (same) return;
		std::vector<BrowseRoot> was = roots;
		roots = fresh;
		rootsChanged++;
		manga->mangaScanned = false;	// (the Manga folders read again: a card's came or went)
		bool gone = false;
		for (auto& r : was)
		{
			bool still = false;
			for (auto& f : fresh) still = still || f.path == r.path;
			if (!still && (browse->browseDir == r.path || browse->browseDir.rfind(r.path + "/", 0) == 0)) gone = true;
		}
		fprintf(stderr, "files: the cards changed (%zu places now)\n", fresh.size());
		// in a card that went: back to the places, said; on the places' page: listed again
		if (gone)
		{
			alert("The card was taken out", "Its folders come back when it\xE2\x80\x99s in again.");
			browse->enterFolder("", p);
		}
		else if (page == Page::Browse && browse->browseDir.empty()) browse->enterFolder("", p);
	}

	void Ui::startBrowser(Player& p, const std::string& folder)
	{
		roots = browseRoots();
		page = Page::Browse;
		onPlayer = false;
		if (!folder.empty())
		{
			browse->browseTab = isNetPath(folder) ? TAB_NETWORK : TAB_LOCAL;
			browse->enterFolder(folder, p);
			return;
		}
		const std::string& last = p.resumeStore().lastFolder;
		struct stat st;
		// a playlist kept: the Playlists tab, in it (one deleted since: the tab's list)
		if (browse->playlists->plStore && p.settings.rememberFolder && last.rfind(browse->playlists->plStore->dir() + "/", 0) == 0)
		{
			browse->browseTab = TAB_PLAYLISTS;
			browse->enterFolder(stat(last.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? plDirFor(last) : std::string(PLROOT), p);
			return;
		}
		bool there = p.settings.rememberFolder && !last.empty() && stat(last.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
			(p.settings.showHiddenFolders || (!isHiddenFolder(last) && !insideHiddenFolder(last)));
		// a folder kept: Files, in it; else Home
		if (there) browse->browseTab = TAB_LOCAL;
		browse->enterFolder(there ? last : std::string(HOMEDIR), p);
	}

	void Ui::backFromPlayer(Player& p, const std::string& lastPlayed, const PlaySource& from, int index)
	{
		// Close all in incognito asked for Home: Stop's own way back (the file's folder, the playlist) must not undo
		// it, wherever this runs after the stop
		if (homeAfterCloseAll)
		{
			homeAfterCloseAll = false;
			inMenu = false;
			showSourceAfterLeave = false;
			browse->homeView->goHome(Home::HOME_GRID, 0, p);
			page = Page::Browse;
			cursor = scroll = 0;
			ctxOpen = false;
			playerBack = PlayerBack();
			dropTouch();
			return;
		}
		PlayerBack b = playerBack;
		playerBack = PlayerBack();
		bool toSource = showSourceAfterLeave;
		showSourceAfterLeave = false;
		if (!b.set || !showAfterLeave.empty() || b.page == Page::Main || toSource)
		{
			if (toSource) inMenu = false;
			showBrowser(p, lastPlayed, from, index);
			return;
		}
		// the page as it was left: nothing of the browser's was touched meanwhile but the cursor and the scroll, which
		// the player's pages share (and the menu's way back, which START over the player set again)
		onPlayer = false;
		// (the list it was on: back in it if a play moved it, a playlist's page say; the cursor and scroll as below)
		if (b.page == Page::Browse && b.browseDir != browse->browseDir && (b.browseDir == HOMEDIR || pseudoDir(b.browseDir) || exists(b.browseDir)))
		{
			browse->setSection(b.browseTab);
			browse->enterFolder(b.browseDir, p);
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
		playerPage->unlock();	// the lock is the player's
		if (bgPlay) landFlashUntil = SDL_GetTicks() + 900;	// the mini bar's edge lit a moment: it's still playing
		dropTouch();
		// played from a playlist's page: back to the list of playlists, the cursor on that one (the owner's rule: Back
		// leaves the playlist; List on the player opens it, toSource above)
		if (page == Page::Browse && !inMenu && isPlDir(browse->browseDir))
		{
			std::string file = plFileOf(browse->browseDir);
			browse->enterFolder(PLROOT, p);
			for (size_t i = 0; i < browse->browsePaths.size(); i++)
				if (browse->browsePaths[i] == file) { cursor = (int)i; break; }
			browse->fitList();
		}
		// back in the folder of what played: the cursor on it (the photo last viewed, the song or video Next moved on to)
		else if (page == Page::Browse && !inMenu && !lastPlayed.empty())
			for (size_t i = 0; i < browse->browsePaths.size(); i++)
				if (browse->browsePaths[i] == lastPlayed)
				{
					cursor = (int)i;
					browse->fitList();
					break;
				}
		fprintf(stderr, "ui: back from the player to %s\n", page == Page::Browse && !inMenu ? (browse->atHome() ? "Home" : browse->browseDir.empty() ? "Files" : browse->browseDir.c_str())
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
		playerPage->unlock();	// the lock is the player's
		dropTouch();
		if (!showAfterLeave.empty())
		{
			// the Playing tab's Show in folder: that folder, wherever it played from
			std::string path;
			path.swap(showAfterLeave);
			inMenu = false;
			showInFolder(path, p);
			return;
		}
		if (from.kind == SourceKind::Playlist && browse->playlists->plStore)
		{
			// the playlist it played from, the cursor on the item (by the order it played in; else by its path)
			browse->setSection(TAB_PLAYLISTS);
			if (!exists(from.id)) { browse->enterFolder(PLROOT, p); return; }
			browse->enterFolder(plDirFor(from.id), p);
			int item = from.id == browse->playlists->plOrderId && index >= 0 && index < (int)browse->playlists->plOrder.size() ? browse->playlists->plOrder[index] : -1;
			if (item < 0 || item >= (int)browse->browsePaths.size() || browse->browsePaths[item] != lastPlayed)
			{
				item = -1;
				for (size_t i = 0; i < browse->browsePaths.size() && item < 0; i++)
					if (browse->browseRows[i].id == PL_ITEM && browse->browsePaths[i] == lastPlayed) item = (int)i;
			}
			if (item >= 0) cursor = item;
			browse->fitList();
			return;
		}
		// a file of a card's (from Home, Recent or a playlist too): its folder, in Files
		if (!isNetPath(lastPlayed) && !lastPlayed.empty()) browse->setSection(TAB_LOCAL);
		if (isNetPath(lastPlayed))
		{
			// its network folder, listed again (the cursor goes to it once the listing comes)
			browse->setSection(TAB_NETWORK);
			browse->netSelect = lastPlayed;
			browse->enterFolder(netParent(lastPlayed), p);
			return;
		}
		// listed again either way: positions changed, a file may have come or gone
		browse->enterFolder(lastPlayed.empty() ? browse->browseDir : parentFolder(lastPlayed), p);
		for (size_t i = 0; i < browse->browsePaths.size(); i++)
			if (browse->browsePaths[i] == lastPlayed) cursor = (int)i;
		browse->fitList();
	}

	void Ui::dropTouch()
	{
		// a finger still down on what's gone presses nothing on what comes (as it lifts); none down: nothing to drop
		if (panTouch || listTouch || slide.on || pressedBtn >= 0 || browse->homeView->homeTouch) swallowTouch = true;
		panTouch = panMoved = listTouch = browse->homeView->homeTouch = false;
		slideDrop();
		pressedBtn = browse->homeView->homePress = -1;
		playerPage->dropTouch();
		flickAt = tapAt = holdAt = browse->homeView->homeHoldAt = 0;
	}

	void Ui::Browse::fitList()
	{
		int n = (int)browseRows.size(), first = listFirst();
		ui.cursor = std::clamp(ui.cursor, first, std::max(first, n - 1));
		// the cursor in the window, and no empty rows below the list's end while there are rows above
		ui.scroll = std::clamp(ui.scroll, std::max(first, ui.cursor - ROWS_SHOWN + 1), std::min(ui.cursor, std::max(first, n - ROWS_SHOWN)));
	}

	// the rows' ids: 1 a folder to go into, 2 a video to open, -1 nothing (a message)
	void Ui::Browse::enterFolder(std::string dir, Player& p)
	{
		// the playlist's edits written before another page reads it (or the list counts it)
		playlists->plFlush(p);
		placeIn[browseDir] = {ui.cursor, ui.scroll};
		netOpenPending.clear();	// gone elsewhere first: not opened
		if (browseDir == FINDNET && dir != FINDNET && ui.discovery) ui.discovery->stop();
		browseDir = dir;
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& path, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back(path);
		};
		auto at = [&](const std::string& path) { return ui.resumeNote(path, p); };
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
		playlists->plMissing.clear();
		playlists->plMissingSerial++;
		if (dir == HOMEDIR)
		{
			homeView->homeListedFor = p.currentPath();
			// the Recent strip listed again, the focus kept on the file it was on (gone: the card that took its place)
			std::string was = homeView->homeStrip < homeView->stripCards() ? homeView->recent[homeView->homeStrip].path : "";
			int wasAt = homeView->homeStrip;
			std::vector<std::string> before;
			if (homeView->recentKeepOrder)
				for (auto& r : homeView->recent) before.push_back(r.path);
			homeView->listRecent(p);
			if (homeView->recentKeepOrder) homeView->recentInOrder(before);
			homeView->recentKeepOrder = false;
			if (homeView->homeFresh)
			{
				homeView->homeFresh = false;
				homeView->homeZone = homeView->recent.empty() ? Home::HOME_GRID : Home::HOME_STRIP;
				homeView->homeStrip = homeView->homeGrid = homeView->homeCol = 0;
			}
			else
			{
				int found = -1;
				for (size_t i = 0; i < homeView->recent.size() && !was.empty(); i++)
					if (homeView->recent[i].path == was) found = (int)i;
				homeView->homeStrip = was.empty() ? homeView->stripCount() - 1 : found >= 0 ? found : std::min(wasAt, std::max(0, homeView->stripCards() - 1));
				if (homeView->recent.empty() && homeView->homeZone == Home::HOME_STRIP)
				{
					homeView->homeZone = Home::HOME_GRID;
					homeView->homeGrid = homeView->homeCol = 0;
				}
			}
			homeView->homeStrip = std::clamp(homeView->homeStrip, 0, homeView->stripCount() - 1);
			homeView->stripOff = homeView->stripTarget(homeView->homeStrip);
			homeView->stripT0 = 0;
		}
		// the drawer draws itself (ui_ext.cpp): its one row only keeps the list's counts sane
		else if (dir == EXTDIR) add(-1, ui.mangaOn ? "1 on" : "None on", "", "");
		else if (dir == PLROOT)
		{
			// the saved playlists, by name
			// Liked first, then the user's by name, then New playlist
			if (!playlists->plStore) add(-1, "Playlists aren't available", "", "");
			else
			{
				for (auto& pl : playlists->plStore->list())
					add(PL_ROW, pl.name, pl.file, pl.builtin && !pl.count ? "Empty" : std::to_string(pl.count) + (pl.count == 1 ? " item" : " items"));
				add(PL_NEW, "New playlist", "", "");
			}
		}
		else if (isPlDir(dir))
		{
			// a playlist's items, in its order (read again each time: another page may have changed it)
			playlists->plCur = Playlist();
			int skipped = 0;
			playlists->plLoaded = playlists->plStore && playlists->plStore->load(plFileOf(dir), playlists->plCur, skipped);
			if (!playlists->plLoaded) playlists->plCur.file = plFileOf(dir);
			playlists->plRows(p);
		}
		else if (dir == FORMDIR) formRows();
		else if (dir == FINDNET)
		{
			// a search from scratch each time it's opened (Search again too)
			found.clear();
			if (ui.discovery && !ui.discovery->running()) ui.discovery->start(ui.onFound, 4);
			findWasRunning = ui.discovery && ui.discovery->running();
			findRows();
		}
		else if (dir == NETROOT)
		{
			// the saved connections, the most recently used first
			std::vector<NetServer> list = ui.netStore ? ui.netStore->servers() : std::vector<NetServer>();
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
			const NetServer* sv = ui.netStore ? ui.netStore->find(np.server) : nullptr;
			if (!sv) add(-1, "This connection is gone", "", "");
			else if (!ui.netWorker) add(-1, "No network support in this build", "", "");
			else
			{
				netLoading = true;
				netWritable = false;	// (until the listing says)
				add(-1, "Loading...", "", "");
				ui.netWorker->list(++netSerial, *sv, ui.netStore->password(*sv), dir, true);
			}
		}
		else if (dir.empty())
			for (auto& r : ui.roots) add(1, r.label, r.path, r.path);	// Files' top: the places (Recent is Home's)
		else if (dir == RECENT && !p.settings.keepHistory) add(-1, "Play history is off (Settings)", "", "");
		else if (dir == RECENT)
		{
			// All recent: the history (a file taken away or on a card taken out left out); X clears it
			std::vector<std::string> before;
			if (homeView->recentKeepOrder)
				for (auto& r : homeView->recent) before.push_back(r.path);
			homeView->listRecent(p);
			if (homeView->recentKeepOrder) homeView->recentInOrder(before);
			homeView->recentKeepOrder = false;
			// the tabs: how many of each, and only the chosen kind listed (PlayHistory keeps 100 of each)
			for (int& c : homeView->recentCounts) c = 0;
			for (auto& it : homeView->recent)
			{
				homeView->recentCounts[0]++;
				homeView->recentCounts[PlayHistory::kindOf(it.path) + 1]++;
			}
			// (a kind emptied meanwhile, its last one removed: back to All rather than an empty tab)
			if (homeView->recentTab && !homeView->recentCounts[homeView->recentTab]) homeView->recentTab = 0;
			for (auto& it : homeView->recent)
			{
				if (homeView->recentTab && homeView->recentTab != PlayHistory::kindOf(it.path) + 1) continue;
				add(2, baseName(it.path), it.path, it.net ? fileNote(at(it.path), -1) : localNote(it.path));
			}
			if (homeView->recent.empty()) add(-1, "Nothing played yet", "", "");
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
		ui.cursor = it == placeIn.end() ? firstCursor() : std::min(it->second.first, (int)browseRows.size() - 1);
		ui.scroll = it == placeIn.end() ? listFirst() : it->second.second;
		// Recent opens at its top: the most recent first, and a place in a list that changed since means little
		if (dir == RECENT) ui.cursor = ui.scroll = 0;
		fitList();
		browseVersion++;
		// (an empty list too: the last folder's probes stop)
		if (ui.durWorker) ui.durWorker->want(++durSerial, std::move(durWant));
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
			if (r.serial != browse->durSerial || r.secs <= 0 || r.row < 0 || r.row >= (int)browse->browseRows.size() || browse->browsePaths[r.row] != r.path) continue;
			browse->browseRows[r.row].note = fileNote(resumeNote(r.path, p), r.secs, fileSize(r.path));
			if (r.row >= scroll && r.row < scroll + ROWS_SHOWN) shown = true;
		}
		// only a row on screen needs a redraw: the others are drawn as they scroll in
		if (shown) browse->browseVersion++;
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
			if (l->serial != browse->playlists->addPending.serial) return;
			Playlists::AddPending a = browse->playlists->addPending;
			browse->playlists->addPending = {};
			if (!l->result.ok())
			{
				NetPath np;
				const NetServer* sv = netStore && parseNetPath(l->dir, np) ? netStore->find(np.server) : nullptr;
				// (not found: the folder, most likely; the server answered its parent's listing)
				alert("Couldn\xE2\x80\x99t add the folder", l->result.error == NetError::NotFound ? "Can't find " + quoted(browse->playlists->folderName(l->dir)) + "."
					: netErrorText(l->result.error, sv ? sv->host : "the server"));
				return;
			}
			if (browse->netNames.size() > 32) browse->netNames.clear();
			browse->netNames[l->dir] = l->names;
			int photos = 0;
			auto add = folderEntries(l->dir, l->names, photos);
			browse->playlists->addTo(a.dest, add, photos, browse->playlists->folderName(l->dir), a.back, p);
			return;
		}
		if (l->test)
		{
			// Test connection's answer (the form's row, a connection's menu); an older test's is nothing
			if (l->serial != browse->testSerial) return;
			browse->testText = l->result.ok() ? "It works" : netErrorText(l->result.error, "The server");
			ctxStatus = browse->testText;
			if (browse->browseDir == FORMDIR) browse->formRows();
			browse->browseVersion++;
			return;
		}
		// the folder of a file to open, listed first: opened now, with what the folder has (alone if it failed)
		if (!l->test && !browse->netOpenPending.empty() && l->serial == browse->netSerial && l->dir == netParent(browse->netOpenPending))
		{
			if (l->result.ok()) browse->netNames[l->dir] = l->names;
			openRequest = browse->netOpenPending;
			browse->netOpenPending.clear();
			return;
		}
		if (l->test || l->serial != browse->netSerial || l->dir != browse->browseDir || page != Page::Browse) return;
		browse->netLoading = false;
		browse->netWritable = l->writable;
		browse->netSizes.clear();
		for (auto& e : l->entries)
			if (!e.dir) browse->netSizes[netJoin(l->dir, e.name)] = e.size;
		browse->browseRows.clear();
		browse->browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& path, const std::string& note) {
			browse->browseRows.push_back({id, label, false, note});
			browse->browsePaths.push_back(path);
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
			if (browse->netNames.size() > 32) browse->netNames.clear();
			browse->netNames[l->dir] = l->names;
		}
		if (l->result.ok())
			for (auto& e : l->entries)
			{
				std::string path = netJoin(l->dir, e.name);
				const ResumeEntry* r = p.resumeStore().find(path);
				std::string note = !e.dir && r && r->position >= 0 && !isAudioFile(e.name) ? "at " + formatTime(r->position) : "";
				add(e.dir ? 1 : 2, e.name, path, e.dir ? note : fileNote(note, e.size));
			}
		if (browse->browseRows.empty()) add(-1, "Nothing to play here", "", "");
		auto it = browse->placeIn.find(l->dir);
		cursor = it == browse->placeIn.end() ? browse->firstCursor() : it->second.first;
		scroll = it == browse->placeIn.end() ? browse->listFirst() : it->second.second;
		if (!browse->netSelect.empty())
			for (size_t i = 0; i < browse->browsePaths.size(); i++)
				if (browse->browsePaths[i] == browse->netSelect) cursor = (int)i;
		browse->netSelect.clear();
		browse->fitList();
		browse->browseVersion++;
	}

	bool Ui::Browse::emptyAction() const
	{
		// (an empty playlist's: Add files)
		bool emptyPl = isPlDir(browseDir) && playlists->plLoaded && playlists->plStore && playlists->plCur.entries.empty() && !ui.plIsLiked(playlists->plCur.file);
		return ui.page == Page::Browse && !ui.inMenu && playlists->selMode == SEL_NONE &&
			((browseDir == NETROOT) || (browseDir == PLROOT && playlists->plStore) || emptyPl) && browseRows.size() == 1 && browseRows[0].id < 0;
	}

	void Ui::Browse::doEmptyAction(Player& p)
	{
		if (browseDir == PLROOT) playlists->plNameOpen(false, "", "");
		else if (isPlDir(browseDir))
		{
			// Add files: the cards (where the Local tab was), the files ticked there added to this one
			playlists->selStart(SEL_ADD, playlists->plCur.file);
			switchTab(TAB_LOCAL, p);
		}
		// no connections yet: the search first (nothing to type); Add connection is Y's, for FTP or one it can't find
		else enterFolder(FINDNET, p);
	}

	bool Ui::Browse::atFirstPage() const
	{
		return browseDir.empty() || browseDir == NETROOT || browseDir == PLROOT || browseDir == EXTDIR || browseDir == RECENT;
	}

	bool Ui::Browse::atHome() const
	{
		return browseDir == HOMEDIR;
	}

	void Ui::Browse::setSection(int tab)
	{
		if (tab == browseTab) return;
		// (Home and All recent are no section's)
		if (!atHome() && browseDir != RECENT) tabDir[browseTab] = browseDir;
		browseTab = tab;
	}

	void Ui::Browse::switchTab(int tab, Player& p)
	{
		if (tab == browseTab && !atHome() && browseDir != RECENT) return;
		setSection(tab);
		std::string dir = tabDir[tab];
		if (dir.empty()) dir = sectionTop(tab);
		enterFolder(dir, p);
	}

	void Ui::Browse::openSection(int tab, Player& p)
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
		if (gone && isPlDir(dir) && playlists->plStore && playlists->plStore->isBuiltinFile(plFileOf(dir))) gone = false;
		enterFolder(gone ? sectionTop(tab) : dir, p);
	}

	bool Ui::Browse::upRow() const
	{
		if (ui.page != Page::Browse || atHome() || atFirstPage() || browseDir == FORMDIR || browseDir == FINDNET) return false;
		// editing a playlist's items, moving them: the list is what's being worked on
		return playlists->selMode != SEL_EDIT && !playlists->moving;
	}

	int Ui::Browse::firstCursor() const
	{
		// a folder opened for the first time: on its first item; an empty one (a message alone): on Up, unless the
		// page's own button (Add files) is what A should do there
		bool empty = browseRows.empty() || (browseRows.size() == 1 && browseRows[0].id < 0);
		return empty && !emptyAction() ? listFirst() : 0;
	}

	bool Ui::Browse::folderRow(int row) const
	{
		// a folder of the cards' (not the first page's places, nor Recent)
		return !browseDir.empty() && !pseudoDir(browseDir) && !isNetPath(browseDir) && row >= 0 && row < (int)browseRows.size() && browseRows[row].id == 1;
	}

	std::string Ui::browseSelection() const
	{
		if (browse->atHome())
		{
			if (browse->homeView->homeZone == Home::HOME_GRID) return SECTION_NAMES[browse->homeView->homeGrid];
			return browse->homeView->homeStrip < browse->homeView->stripCards() ? browse->homeView->recent[browse->homeView->homeStrip].path : "All recent";
		}
		if (cursor < 0) return browse->upRow() ? ".." : "";
		return cursor < (int)browse->browsePaths.size() ? browse->browsePaths[cursor] : "";
	}

	std::vector<std::string> Ui::browseLabels() const
	{
		std::vector<std::string> out;
		if (browse->atHome())
		{
			// the Recent cards (shown: in the strip's view now), then the tiles: "card" or "tile", its name, its line
			int off = (int)std::lround(browse->homeView->stripOff);
			if (browse->homeView->recent.empty()) out.push_back("shown    (no Recent: a line instead of the strip)");
			for (int i = 0; !browse->homeView->recent.empty() && i < browse->homeView->stripCount(); i++)
			{
				int x = 16 + i * Home::STRIDE - off;
				bool at = browse->homeView->homeZone == Home::HOME_STRIP && browse->homeView->homeStrip == i;
				std::string l = std::string(x + Home::CARD_W > 0 && x < W ? "shown  " : "hidden ") + (at ? "> " : "  ") + "card ";
				if (i < browse->homeView->stripCards())
					l += browse->homeView->recent[i].name + "  | " + browse->homeView->recent[i].sub + (browse->homeView->recent[i].net ? " [net]" : "") + " \xC2\xB7 " + browse->homeView->recent[i].where;
				else l += "All recent  | " + std::to_string(browse->homeView->recent.size()) + " played";
				out.push_back(l);
			}
			for (int g = 0; g < BROWSE_TABS; g++)
				out.push_back(std::string("shown  ") + (browse->homeView->homeZone == Home::HOME_GRID && browse->homeView->homeGrid == g ? "> " : "  ") + "tile " + SECTION_NAMES[g] +
					"  | " + browse->homeView->tileSub(g));
			return out;
		}
		if (browse->upRow())
			out.push_back(std::string(scroll < 0 ? "shown  " : "hidden ") + (cursor < 0 ? "> " : "  ") + "[up] ..");
		for (size_t i = 0; i < browse->browseRows.size(); i++)
		{
			std::string l = std::string((int)i >= scroll && (int)i < scroll + ROWS_SHOWN ? "shown  " : "hidden ") +
				(i == (size_t)cursor ? "> " : "  ") + browse->browseRows[i].label;
			// a playlist's and its items' notes; an item that's missing is "[Gone] name", with none (checked here for
			// all of them)
			if (browse->browseRows[i].id == PL_ROW || browse->browseRows[i].id == PL_ITEM)
			{
				if (browse->playlists->plMissingAt((int)i))
				{
					size_t at = l.size() - browse->browseRows[i].label.size();
					l.insert(at, "[Gone] ");
					l += "  |";
				}
				else l += "  | " + browse->browseRows[i].note;
			}
			// selecting: a ticked row (Edit's item, Add's file)
			if (browse->playlists->selTicked((int)i)) l.insert(9, "[x] ");	// after "shown  > "
			out.push_back(l);
		}
		return out;
	}

	bool Ui::Browse::holdRow(int row) const
	{
		if (folderRow(row)) return true;
		if (row < 0 || row >= (int)browseRows.size()) return false;
		int64_t id = browseRows[row].id;
		if (id == PL_ROW || id == PL_ITEM) return true;
		// a file: selecting (it ticked), to add to a playlist; a server's folder: its menu (Add to playlist...)
		if (!playlists->plStore) return false;
		return (id == 2 && !isPlDir(browseDir) && !isPhotoFile(browsePaths[row])) || (id == 1 && isNetPath(browseDir));
	}

	void Ui::openBrowseMenu(int row, MenuHow how)
	{
		// (Home's and All recent's are their own: openHomeOptions, openRecentMenu; Extensions has none yet)
		if (browse->browseDir == FORMDIR || browse->browseDir == EXTDIR || browse->atHome()) return;
		bool onRow = row >= 0 && row < (int)browse->browseRows.size();
		if (browse->browseDir == PLROOT || isPlDir(browse->browseDir))
		{
			// Playlists: the tab's menu (X), a playlist's (Y or a hold on it, or X in it), an item's (Y or a hold)
			std::vector<int> items;
			std::string title, note, file;
			int item = -1;
			auto count = [](size_t n) { return std::to_string(n) + (n == 1 ? " item" : " items"); };
			if (browse->browseDir == PLROOT && how == MENU_X)
			{
				if (!browse->playlists->plStore) return;
				items = {CTX_PL_NEW, CTX_CANCEL};
				title = "Your playlists";
			}
			else if (browse->browseDir == PLROOT)
			{
				if (!onRow || browse->browseRows[row].id != PL_ROW) return;
				file = browse->browsePaths[row];
				title = browse->browseRows[row].label;
				note = browse->browseRows[row].note;
				if (plIsLiked(file)) items = {CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_CANCEL};	// no rename or delete
				else items = {CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_RENAME, CTX_PL_DELETE, CTX_CANCEL};
			}
			else if (how == MENU_X)
			{
				if (!browse->playlists->plLoaded) return;
				file = browse->playlists->plCur.file;
				title = browse->playlists->plCur.name;
				note = count(browse->playlists->plCur.entries.size());
				// eight with items (the box steps its rows closer to fit, the count left to the page), four without
				if (plIsLiked(file))
				{
					// only the Like button adds to Liked; here: play, and remove (Select)
					if (browse->playlists->plCur.entries.empty()) return;
					items = {CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_SELECT, CTX_PL_MISSING};
				}
				else
				{
					if (!browse->playlists->plCur.entries.empty())
						items = {CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_ADD_FILES, CTX_PL_ADD_FOLDER, CTX_PL_SELECT, CTX_PL_DEDUP, CTX_PL_MISSING};
					else items = {CTX_PL_ADD_FILES, CTX_PL_ADD_FOLDER};
					items.push_back(CTX_PL_RENAME);
					items.push_back(CTX_PL_DELETE);
				}
			}
			else
			{
				if (!onRow || browse->browseRows[row].id != PL_ITEM) return;
				file = browse->playlists->plCur.file;
				item = row;
				title = browse->browseRows[row].label;
				int miss = browse->playlists->plMissingAt(row);
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
			browse->playlists->ctxPlFile = file;
			browse->playlists->ctxPlItem = item;
			return;
		}
		ctxTitle.clear();
		ctxNote.clear();
		browse->playlists->ctxPlFile.clear();
		browse->playlists->ctxPlItem = -1;
		ctxFolder.clear();
		bool folder = browse->folderRow(row);
		bool server = browse->browseDir == NETROOT && row >= 0 && row < (int)browse->browseRows.size() && browse->browseRows[row].id == 4;
		bool netFolder = isNetPath(browse->browseDir) && onRow && browse->browseRows[row].id == 1;
		// X on the cards' pages and a server's folders: Select files (to add to a playlist)
		bool selectFiles = browse->playlists->plStore && how == MENU_X && browse->browseDir != NETROOT && browse->browseDir != FINDNET && browse->browseTab != TAB_PLAYLISTS;
		std::vector<int> items;
		std::string id;
		if (server && how != MENU_X)
		{
			// the connection's own: Edit, Test, Delete
			NetPath np;
			if (!parseNetPath(browse->browsePaths[row], np)) return;
			id = np.server;
			items = {CTX_EDIT, CTX_TEST, CTX_DELETE, CTX_CANCEL};
		}
		else if (browse->browseDir == NETROOT && how == MENU_X)
		{
			items = {CTX_ADD, CTX_FIND, CTX_REFRESH, CTX_CANCEL};
#ifdef BP_WEB
			items.insert(items.end() - 1, CTX_WEB);
#endif
		}
		else if (folder) items = {CTX_HIDE, CTX_REFRESH, CTX_CANCEL};
		// a file of the cards: Copy..., Move..., Rename..., Delete (a folder's come before its Hide)
		else if (how != MENU_X && browse->fileOpsOn(row) && !netFolder) items = {CTX_CANCEL};
		else if (netFolder && how != MENU_X && browse->playlists->plStore)
		{
			// a server's folder: its videos and songs added to a playlist
			items = {CTX_ADD_TO, CTX_CANCEL};
			ctxTitle = browse->browseRows[row].label;
		}
		else if (how == MENU_X) items = {CTX_REFRESH, CTX_CANCEL};
		else return;	// Y, or a hold, on what has no menu of its own
		// a card's folder (Y, a hold): Add to playlist... first
		if (folder && how != MENU_X && browse->playlists->plStore) items.insert(items.begin(), CTX_ADD_TO);
		// X on a card's pages: Show hidden folders, before Cancel
		if (how == MENU_X && !isNetPath(browse->browseDir) && (browse->browseDir.empty() || !pseudoDir(browse->browseDir)))
			items.insert(items.end() - 1, CTX_HIDDEN);
		if (folder || netFolder) ctxFolder = browse->browsePaths[row];
		bool fileOps = how != MENU_X && browse->fileOpsOn(row);
		if (fileOps) items.insert(items.begin() + ((folder || netFolder) && browse->playlists->plStore ? 1 : 0), {CTX_F_INFO, CTX_F_COPY, CTX_F_MOVE, CTX_F_RENAME, CTX_F_DELETE});
		// X: the highlighted file's or folder's Info (first, above Select files)
		bool infoX = how == MENU_X && browse->fileOpsOn(row);
		if (selectFiles) items.insert(items.begin(), CTX_SELECT_FILES);
		if (infoX) items.insert(items.begin(), CTX_F_INFO);
		ctxOpen = true;
		ctxCursor = 0;
		ctxItems = items;
		ctxServer = id;
		ctxStatus.clear();
		ctxArmed = false;
		ctxPath = (folder || fileOps || infoX) && id.empty() ? browse->browsePaths[row] : "";
	}

	void Ui::chooseBrowseMenu(int item, Player& p)
	{
		int what = item >= 0 && item < (int)ctxItems.size() ? ctxItems[item] : CTX_CANCEL;
		// Files' own: Copy..., Move..., Rename..., Delete (ui_fileops.cpp)
		if (what == CTX_F_INFO) { ctxOpen = false; openFileInfo(ctxPath, ""); return; }
		if (what >= CTX_F_COPY && what <= CTX_F_DELETE) { browse->fileOpsAct(what, ctxPath, p); return; }
		// (before the playlists' below: their numbers come after them)
		if (what == CTX_WEB) { ctxOpen = false; openWeb(); return; }
		if (what >= CTX_R_PLAY)
		{
			std::string path = browse->homeView->ctxRecent;
			if (what == CTX_CLEAR_RECENT)
			{
				// twice; nothing to clear, nothing
				if (browse->homeView->recent.empty()) return;
				if (!ctxArmed) { ctxArmed = true; return; }
				ctxOpen = false;
				p.clearHistory();
				p.osd("Recent cleared");
				browse->homeView->goHome(Home::HOME_GRID, 0, p);
				return;
			}
			if (what == CTX_HIDDEN)
			{
				// the menu stays up (its value changes); the page listed again under it, on the same row
				Settings st = p.settings;
				st.showHiddenFolders = !st.showHiddenFolders;
				p.setSettings(st);
				std::string at = cursor >= 0 && cursor < (int)browse->browsePaths.size() ? browse->browsePaths[cursor] : "";
				int c = cursor, s = scroll;
				browse->enterFolder(browse->browseDir, p);
				cursor = c;
				scroll = s;
				for (size_t i = 0; i < browse->browsePaths.size(); i++)
					if (!at.empty() && browse->browsePaths[i] == at) cursor = (int)i;
				browse->fitList();
				return;
			}
			ctxOpen = false;
			switch (what)
			{
			case CTX_SETTINGS: openMainMenu(p, TAB_SETTINGS); break;
			case CTX_R_START:
				// a document: its place forgotten, then opened
				if (isDocumentName(path) || isComicName(path))
				{
					docs->forgetPlace(path);
					openFile(path, p);
					break;
				}
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
			// the mini bar's: main's player (over a photo the photo viewer's p is not what they act on)
			case CTX_NP_NOW: goNowPlaying(p); break;
			case CTX_NP_PLAY: miniToggle(p); break;
			case CTX_NP_LIKE: likeNow(barPlayer(p)); break;
			case CTX_NP_ADD:
				if (Player& bp = barPlayer(p); bp.playing() && !bp.playingPhoto())
					browse->playlists->openAddOne(bp.currentPath(), bp.title, bp.loaded ? bp.duration : -1, p);
				break;
			case CTX_NP_SHOW:
				if (Player& bp = barPlayer(p); bp.playing() && !bp.playingPhoto()) showInFolder(bp.currentPath(), p);
				break;
			case CTX_NP_STOP:
				if (Player& bp = barPlayer(p); bp.playing() && !bp.playingPhoto()) stopPressed = true;
				break;
			case CTX_R_PLAY: openFile(path, p); break;
			case CTX_R_SHOW: showInFolder(path, p); break;
			case CTX_R_ADD:
			{
				// the one file: the playlists to add it to (none yet: a new one)
				std::string title;
				for (auto& x : browse->homeView->recent)
					if (x.path == path) title = x.name;
				browse->playlists->openAddOne(path, title, -1, p);
				break;
			}
			case CTX_R_LIKE:
			{
				std::string title;
				for (auto& x : browse->homeView->recent)
					if (x.path == path) title = x.name;
				bool was = browse->playlists->plStore && browse->playlists->plStore->isLiked(path);
				if (setLikedPath(p, path, title, -1, !was) != was) likeCard(p, !was, title);
				break;
			}
			case CTX_R_REMOVE:
			{
				p.removeFromHistory(path);
				p.osd("Removed from Recent");
				// (the list again: Home keeps its place, the card after it in its slot)
				int c = cursor, s = scroll;
				browse->enterFolder(browse->browseDir, p);
				if (browse->browseDir == RECENT)
				{
					cursor = c;
					scroll = s;
					browse->fitList();
				}
				break;
			}
			default: break;
			}
			return;
		}
		if (what == CTX_PL_MISSING)
		{
			// the items whose file is on no card now (network ones are never judged: the server may only be off).
			// Twice, the first saying how many: a card that's out makes its files look gone
			std::set<int> gone;
			for (size_t i = 0; i < browse->playlists->plCur.entries.size(); i++)
			{
				const std::string& e = browse->playlists->plCur.entries[i].path;
				struct stat st;
				if (!isNetPath(e) && stat(e.c_str(), &st) != 0) gone.insert((int)i);
			}
			if (gone.empty()) { ctxOpen = false; p.osd("Nothing is missing"); return; }
			if (!ctxArmed) { ctxArmed = true; ctxMissing = (int)gone.size(); return; }
			ctxOpen = false;
			auto remap = removeEntries(browse->playlists->plCur.entries, gone);
			browse->playlists->plSaved(p, remap);
			fprintf(stderr, "playlists: removed %zu missing from %s\n", gone.size(), browse->playlists->plCur.name.c_str());
			p.osd("Removed " + std::to_string(gone.size()) + " missing");
			return;
		}
		if (what == CTX_PL_DELETE)
		{
			// twice (the files themselves stay where they are: the menu says so)
			if (!ctxArmed) { ctxArmed = true; return; }
			ctxOpen = false;
			browse->playlists->plDelete(browse->playlists->ctxPlFile, p);
			return;
		}
		if (what >= CTX_PL_NEW)
		{
			ctxOpen = false;
			std::string file = browse->playlists->ctxPlFile;
			int at = browse->playlists->ctxPlItem;
			// the playlist the menu is for: the one open, or one of the list's (read now)
			Playlist pl;
			int skipped = 0;
			bool open = isPlDir(browse->browseDir) && browse->playlists->plLoaded && file == browse->playlists->plCur.file;
			if (open) pl = browse->playlists->plCur;
			else if (!file.empty() && (!browse->playlists->plStore || !browse->playlists->plStore->load(file, pl, skipped))) { alert("Couldn\xE2\x80\x99t read the playlist", "Its file may be damaged, or the card read only."); return; }
			if (plIsLiked(file) && (what == CTX_PL_RENAME || what == CTX_PL_DELETE || what == CTX_PL_DEDUP || what == CTX_PL_ADD_FILES ||
				what == CTX_PL_ADD_FOLDER || what == CTX_PL_MOVE))
				return;	// Liked is managed by the Like button; Remove and Select there are Unlike
			switch (what)
			{
			case CTX_PL_NEW: browse->playlists->plNameOpen(false, "", ""); break;
			case CTX_PL_RENAME: browse->playlists->plNameOpen(true, file, pl.name); break;
			case CTX_PL_PLAY: case CTX_PL_SHUFFLE:
				if (pl.entries.empty()) p.osd(quoted(pl.name) + " is empty");
				else browse->playlists->plPlay(pl, 0, what == CTX_PL_SHUFFLE, p);
				break;
			case CTX_PL_HERE: if (open && at >= 0 && at < (int)pl.entries.size()) browse->playlists->plPlay(pl, at, false, p); break;
			case CTX_PL_REMOVE:
				if (open && at >= 0 && at < (int)browse->playlists->plCur.entries.size())
				{
					auto remap = removeEntries(browse->playlists->plCur.entries, {at});
					browse->playlists->plSaved(p, remap);
				}
				break;
			case CTX_PL_DEDUP:
				if (open)
				{
					int removed = 0;
					auto remap = removeDuplicates(browse->playlists->plCur.entries, removed);
					if (!removed) { p.osd("No duplicates"); break; }
					browse->playlists->plSaved(p, remap);
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
					browse->playlists->selStart(what == CTX_PL_ADD_FILES ? SEL_ADD : SEL_PICK, file);
					browse->switchTab(TAB_LOCAL, p);
				}
				break;
			case CTX_PL_SELECT: if (open) browse->playlists->selStart(SEL_EDIT, ""); break;
			case CTX_PL_ADD_ONE:
				if (open && at >= 0 && at < (int)pl.entries.size())
					browse->playlists->openAddOne(pl.entries[at].path, pl.entries[at].title, pl.entries[at].duration, p);
				break;
			case CTX_PL_MOVE:
				// the one item, moved at once (and back to the list as it was, after)
				if (open && at >= 0 && at < (int)browse->playlists->plCur.entries.size())
				{
					browse->playlists->selStart(SEL_EDIT, "");
					browse->playlists->plSel = {at};
					browse->playlists->moveFromMenu = true;
					browse->playlists->moveStart(p);
				}
				break;
			case CTX_SELECT_FILES: browse->playlists->selStart(SEL_ADD, ""); break;
			case CTX_ADD_TO: if (!ctxFolder.empty()) browse->playlists->openPicker(ctxFolder, p); break;
			default: break;
			}
			return;
		}
		const NetServer* sv = netStore && !ctxServer.empty() ? netStore->find(ctxServer) : nullptr;
		if (what == CTX_TEST && sv)
		{
			// the menu stays up, the answer under its title
			browse->startTest(*sv, netStore->password(*sv));
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
			browse->enterFolder(NETROOT, p);
			return;
		}
		ctxOpen = false;
		if (what == CTX_EDIT && sv) { browse->openForm(sv, nullptr, p); return; }
		if (what == CTX_ADD) { browse->openForm(nullptr, nullptr, p); return; }
		if (what == CTX_FIND) { browse->enterFolder(FINDNET, p); return; }
		if (what == CTX_REFRESH)
		{
			// read again where it stands, the cursor kept (on the same file, if it's still there)
			std::string at = cursor >= 0 && cursor < (int)browse->browsePaths.size() ? browse->browsePaths[cursor] : "";
			int c = cursor, s = scroll;
			if (isNetPath(browse->browseDir)) browse->netSelect = at;	// (the listing comes later: netListed puts it there)
			browse->enterFolder(browse->browseDir, p);
			cursor = c;
			scroll = s;
			for (size_t i = 0; i < browse->browsePaths.size(); i++)
				if (!at.empty() && browse->browsePaths[i] == at) cursor = (int)i;
			browse->fitList();
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
		if (!ok) alert("Couldn\xE2\x80\x99t change the folder", "Is the card read only?");
		int c = cursor, s = scroll;
		browse->enterFolder(browse->browseDir, p);	// again: it's gone from the list, or marked
		cursor = c;
		scroll = s;
		browse->fitList();
	}

	void Ui::Browse::browseUp(Player& p)
	{
		if (atFirstPage()) return;
		if (isPlDir(browseDir))
		{
			// back to the playlists, on this one
			std::string file = plFileOf(browseDir);
			enterFolder(PLROOT, p);
			for (size_t i = 0; i < browsePaths.size(); i++)
				if (browsePaths[i] == file) ui.cursor = (int)i;
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
		for (auto& r : ui.roots)
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

	std::string Ui::Browse::browseWhere() const
	{
		// where it is: "Videos / Show / Season 1" (from the first page's place it's in), "MY-PC / Media / Movies"
		std::string where = browseDir.empty() ? "Your cards" : browseDir == RECENT ? "Recent"
			: browseDir == NETROOT ? "Your connections" : browseDir == FORMDIR ? (formId.empty() ? "Add connection" : "Edit connection")
			: browseDir == FINDNET ? "On your network" : browseDir == PLROOT ? "Your playlists"
			: isPlDir(browseDir) ? "Playlists/" + playlists->plCur.name : browseDir;
		if (isNetPath(browseDir) || (!browseDir.empty() && !pseudoDir(browseDir))) return ui.whereOf(browseDir);
		// the folders apart: "share / Movies"
		std::string out;
		for (char c : where) out += c == '/' ? std::string(" / ") : std::string(1, c);
		return out;
	}

	void Ui::drawTracks(const Player& p)
	{
		bool inBrowse = page == Page::Browse;
		bool sel = inBrowse && !inMenu && browse->playlists->selMode != SEL_NONE;
		if (inBrowse && !inMenu && !sel && browse->atHome()) { browse->homeView->drawHome(p); return; }
		if (sel) browse->playlists->drawSelHeader(p);
		else if (inBrowse && !inMenu)
		{
			// All recent: a page of Home's; else a section's header (its markers in place of tabs)
			if (browse->browseDir == RECENT)
			{
				// its tabs on the title's line when they fit (the line under it left as room above the list)
				int titleW = (int)std::lround(textWidth("Recent", 2) / sx);
				drawHeader("Recent", p, 72 + titleW + 20);
				// (with the counts, else without them: a battery and two-digit counts don't leave room for both)
				int tx = 72 + titleW + 20, right = statusLeft(p) - 12;
				browse->homeView->recentTabsUp = p.settings.keepHistory && browse->homeView->recentCounts[0] && (browse->homeView->drawRecentTabs(tx, 32, right) || browse->homeView->drawRecentTabs(tx, 32, right, false));
			}
			else browse->drawSectionHeader(p);
			if (browse->browseDir == EXTDIR)
			{
				// the drawer: what's on, or none (its own drawing, ui_ext.cpp)
				manga->drawExtensionsDrawer(p);
				return;
			}
			// where it is, its end kept; the folder it's in brighter. A playlist: how many items, on the right. A
			// section's top: what's in it
			if (browse->atFirstPage())
			{
				int n = 0;
				for (auto& row : browse->browseRows) n += row.id == PL_ROW || row.id == 4;
				bool cards = false, videos = false;
				for (auto& rt : roots)
				{
					cards = cards || (rt.label.size() >= 4 && rt.label.compare(rt.label.size() - 4, 4, "card") == 0);
					videos = videos || rt.label == "Videos";
				}
				if (browse->browseDir == RECENT && p.settings.keepHistory && browse->homeView->recentCounts[0] && !browse->homeView->recentTabsUp) browse->homeView->drawRecentTabs(16, SUB_Y, 624);
				std::string top = browse->browseDir == RECENT ? ""
					: browse->browseDir.empty() ? (videos && cards ? "Videos and your cards" : cards ? "Your cards" : "Your folders")
					: browse->browseDir == PLROOT ? (!browse->playlists->plStore ? "" : n <= 1 ? "Liked"
						: "Liked \xC2\xB7 " + std::to_string(n - 1) + (n == 2 ? " playlist" : " playlists"))
					: !n ? ""
					: std::to_string(n) + (n == 1 ? " connection" : " connections");
				if (!top.empty()) drawTextMid(top, 0, DIM, 16, SUB_Y);
			}
			else
			{
				int whereW = 600;
				if (isPlDir(browse->browseDir) && browse->playlists->plLoaded && plIsLiked(browse->playlists->plCur.file))
				{
					// Liked: its order, and how many (empty: the page says so)
					size_t n = browse->playlists->plCur.entries.size();
					if (n) drawTextMid("Newest first \xC2\xB7 " + std::to_string(n) + (n == 1 ? " item" : " items"), 0, DIM, 16, SUB_Y);
					whereW = 0;
				}
				else if (isPlDir(browse->browseDir) && browse->playlists->plLoaded)
				{
					size_t n = browse->playlists->plCur.entries.size();
					std::string count = std::to_string(n) + (n == 1 ? " item" : " items");
					drawTextMid(count, 0, DIM, 616, SUB_Y, 0, 1);
					whereW -= (int)std::lround(textWidth(count, 0) / sx) + 24;
				}
				if (whereW) browse->drawWhere(16, whereW);
			}
		}
		auto r = rows(p);
		bool form = inBrowse && browse->browseDir == FORMDIR;
		// ".." (up to the folder above): row -1, over the list (and over a message alone)
		bool up = inBrowse && browse->upRow();
		if (up && scroll < 0) browse->drawUpRow(ROW_Y, cursor < 0);
		// a message alone (nothing here, loading, no connections): in the middle of the page, not as a row to choose
		if (inBrowse && !form && r.size() == 1 && r[0].id < 0)
		{
			const std::string& m = r[0].label;
			bool net = browse->browseDir == NETROOT || isNetPath(browse->browseDir);
			bool pl = browse->browseDir == PLROOT || isPlDir(browse->browseDir);
			const Glyph& g = net ? G_NETWORK : browse->browseDir == RECENT ? G_CLOCK : G_FOLDER;
			std::string sub = m == "No connections yet" ? "Play from a computer or a NAS on your Wi-Fi"
				: m == "Nothing played yet" ? "What you play or read shows up here, with where you left off"
				: m == "Nothing to play here" ? "No videos, music or photos in this folder"
				: m == "This folder can't be opened" ? "Is the card still in?"
				: m == "Play history is off (Settings)" ? "Turn it on in Settings, Files" : "";
			std::string title = m == "Play history is off (Settings)" ? "Play history is off" : m;
			if (pl && isPlDir(browse->browseDir) && plIsLiked(browse->playlists->plCur.file) && browse->playlists->plLoaded)
				drawEmpty(G_HEART_O, "Nothing liked yet", "Tap the heart at the top while a song or video plays,\n"
					"or press START \xE2\x80\xBA Like. Liked things gather here.", nullptr);
			else if (pl)
				drawEmpty(G_PLAYLIST, title, m == "No playlists yet" ? "Gather videos and songs to play in your order"
					: m == "Playlists aren't available" ? "There's no folder to keep them in"
					: m == "This playlist can't be opened" ? "Was it deleted?" : "Add videos and songs from your cards or the network",
					!browse->emptyAction() ? nullptr : isPlDir(browse->browseDir) ? "Add files" : "New playlist");
			else if (browse->emptyAction() && browse->browseDir == NETROOT)
				drawEmpty(g, title, "Find the computers sharing files on your Wi-Fi.\nOr Y: add one by its address", "Find on the network", &G_SEARCH);
			else drawEmpty(g, title, sub, browse->emptyAction() ? "Add connection" : nullptr);
		}
		else
			for (int i = std::max(0, scroll); i < (int)r.size() && i < scroll + ROWS_SHOWN; i++)
			{
				int y = ROW_Y + (i - scroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
				bool at = i == cursor;
				if (form && r[i].id == FORM_TEST) fill({32, y - 3, 576, 1}, RULE);
				// selecting: a ticked row on a lighter ground; Move's block lit as the cursor is
				bool block = sel && ((browse->playlists->moving && browse->playlists->plSel.count(i)) || (browse->playlists->plDrag.on && browse->playlists->dragSel.count(i)));
				if (block) drawFocus({16, y, 608, ROW_H - 4});
				// back from the player: the playing row lit a moment (still playing, here)
				else if (at && inBrowse && landFlashUntil && rowChip(i)) drawBox({16, y, 608, ROW_H - 4}, PRESSED, R_BUTTON, true);
				else if (at && !(sel && browse->playlists->plDrag.on)) drawFocus({16, y, 608, ROW_H - 4});
				else if (sel && browse->playlists->selTicked(i)) drawBox({16, y, 608, ROW_H - 4}, SURFACE, R_BUTTON);
				if (inBrowse && r[i].id == PL_ITEM)
				{
					browse->playlists->drawPlRow(i, mid, at || block);
					continue;
				}
				int noteW = 0;
				// Recent's Clear row says how it stands now (the list was made before the first press expired)
				std::string note = inBrowse && r[i].id == 3 ? (clearArmed ? "Press again to clear" : "Press twice") : r[i].note;
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
				bool boxes = sel && browse->playlists->selMode == SEL_ADD;
				bool inert = sel && r[i].id == 2 && !browse->playlists->selTickable(i);
				if (boxes && browse->playlists->selTickable(i)) drawGlyphMid(browse->playlists->selTicked(i) ? G_BOX_ON : G_BOX, 40, mid, browse->playlists->selTicked(i) ? ACCENT : DIM);
				if (inBrowse)
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
					if (r[i].id == 2 && browse->playlists->plStore && i < (int)browse->browsePaths.size() && browse->playlists->plStore->isLiked(browse->browsePaths[i]))
					{
						drawGlyphMid(G_HEART_S, noteW ? 608 - noteW : 601, mid, ACCENT);
						noteW += 22;
					}
					// the file that plays: its chip, as the dashboard's lists mark it
					if (!sel)
						if (int k = rowChip(i)) noteW += drawChip(608 - noteW, mid, k) + 10;
				}
				SDL_Color ink = r[i].id < 0 || inert ? soft : at ? ACCENT : r[i].id == PL_NEW ? DIM_ON : TEXT;
				drawTextMid(r[i].label, 1, ink, textX, mid, 600 - textX - noteW);
			}
		// a dragged row: where it lands, a line in the gap (none while it would stay where it is)
		if (sel && browse->playlists->plDrag.on && !browse->playlists->dragSel.empty() && browse->playlists->dragTarget() != *browse->playlists->dragSel.begin())
		{
			int slot = browse->playlists->dragSlot();
			if (slot >= scroll && slot <= scroll + ROWS_SHOWN) fillRound({16, ROW_Y + (slot - scroll) * ROW_H - 3, 608, 2}, ACCENT, 1);
		}
		int n = (int)r.size(), first = browse->listFirst();
		drawScrollBar(ROW_Y, ROWS_SHOWN * ROW_H - 4, scroll - first, ROWS_SHOWN, n - first);
		if (sel) { browse->playlists->drawSelBar(); return; }
		// B at a section's top goes Home; L/R to the sections either side
		const char* back = browse->atFirstPage() ? "Home" : "Back";
		bool plEmpty = r.size() == 1 && r[0].id < 0;
		// a row with a menu of its own (Y): a folder, a connection, a playlist, its item
		bool yMenu = cursor >= 0 && cursor < n && (browse->folderRow(cursor) || (browse->browseDir == NETROOT && r[cursor].id == 4) || r[cursor].id == PL_ROW ||
			r[cursor].id == PL_ITEM || (isNetPath(browse->browseDir) && r[cursor].id == 1 && browse->playlists->plStore) || browse->fileOpsOn(cursor));
		bool file = cursor >= 0 && cursor < n && (r[cursor].id == 2 || r[cursor].id == PL_ITEM);
		// a book, a document, any file that doesn't play: A opens it (or says it can't)
		const char* aFile = file && cursor < (int)browse->browsePaths.size() && !isMediaFile(browse->browsePaths[cursor]) ? "Open" : "Play";
		if (cursor < 0 && browse->upRow()) drawHints({{"A", "Up"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}});
		else if (form) drawHints({{"A", "Change"}, {"LEFT/RIGHT", "Choose"}, {"B", "Cancel"}});
		else if (browse->browseDir == FINDNET) drawHints({{"A", "Add this one"}, {"B", "Back"}});
		else if (browse->browseDir == RECENT && plEmpty) drawHints({{"B", "Home"}, {"X", "Options"}, {"START", "Settings"}});
		else if (browse->browseDir == RECENT) drawHints({{"A", file && cursor < (int)browse->browsePaths.size() && !isMediaFile(browse->browsePaths[cursor]) ? "Open" : "Play"}, {"Y", "Menu"}, {"L/R", "Kind"}, {"B", "Home"}, {"X", "Options"}});
		else if (plEmpty && isPlDir(browse->browseDir) && plIsLiked(browse->playlists->plCur.file)) drawHints({{"B", back}, {"L/R", "Sections"}, {"START", "Settings"}});
		else if (browse->browseDir == PLROOT && cursor >= 0 && cursor < n && r[cursor].id == PL_NEW)
			drawHints({{"A", "New playlist"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}});
		else if (browse->emptyAction() && browse->browseDir == PLROOT)
			drawHints({{"A", "New playlist"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}, {"START", "Settings"}});
		else if (browse->emptyAction() && isPlDir(browse->browseDir)) drawHints({{"A", "Add files"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}});
		else if (browse->emptyAction())
			drawHints({{"A", "Find"}, {"Y", "Add connection"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}});
		else if (plEmpty) drawHints({{"B", back}, {"X", "Options"}, {"L/R", "Sections"}, {"START", "Settings"}});
		else if (yMenu) drawHints({{"A", file ? (rowChip(cursor) ? "Go to it" : aFile) : "Open"}, {"B", back}, {"X", "Options"}, {"Y", "Menu"}, {"L/R", "Sections"}});
		else drawHints({{"A", file ? (rowChip(cursor) ? "Go to it" : aFile) : "Open"}, {"B", back}, {"X", "Options"}, {"L/R", "Sections"}});
	}

	void Ui::Browse::drawUpRow(int y, bool at)
	{
		int mid = y + (ROW_H - 4) / 2;
		if (at) ui.drawFocus({16, y, 608, ROW_H - 4});
		ui.drawGlyphMid(G_UP, 43, mid, at ? ACCENT : DIM);
		ui.drawTextMid("..", 1, at ? ACCENT : TEXT, 66, mid, 534);
	}

	void Ui::Playlists::drawPlRow(int i, int mid, bool at)
	{
		const std::string& path = ui.browse->browsePaths[i];
		int miss = plMissingAt(i);
		SDL_Color soft = at ? DIM_ON : DIM;
		// selecting: its box where its number was, its grip on the right (Move's block: its arrows, yellow)
		bool edit = selMode == SEL_EDIT && !ui.inMenu;
		bool block = edit && ((moving && plSel.count(i)) || (plDrag.on && dragSel.count(i)));
		// (Liked has no numbers: its order is the likes', newest first)
		bool liked = ui.plIsLiked(plCur.file);
		if (edit) ui.drawGlyphMid(plSel.count(i) ? G_BOX_ON : G_BOX, 40, mid, plSel.count(i) ? ACCENT : soft);
		else if (!liked) ui.drawTextMid(std::to_string(i + 1), 0, soft, 56, mid, 0, 1);
		if (edit) ui.drawGlyphMid(block ? G_MOVE : G_GRIP, 600, mid, block ? ACCENT : soft);
		int right = edit ? 572 : 608;
		// its number in the playlist, its mark (an alert when it isn't there), its name, where it is
		const Glyph& g = miss ? G_ALERT : isAudioFile(path) ? G_NOTE : G_FILM;
		int gx = liked && !edit ? 43 : 64 + 11;
		ui.drawGlyphMid(g, gx, mid, miss ? DIM : at ? ACCENT : TEXT);
		// a missing one says so in its name, "[Gone]" (why: its menu's line), not on the right
		std::string note = miss ? "" : ui.browse->browseRows[i].note;
		int noteW = 0;
		if (!note.empty())
		{
			ui.drawTextMid(note, 0, miss ? DANGER : soft, right, mid, 220, 1);
			int w = std::min(220, (int)std::lround(ui.textWidth(note, 0) / sx));
			noteW = w + 16;
			// a network item: the server's name, marked as one
			if (!miss && isNetPath(path))
			{
				ui.drawGlyphMid(G_NETWORK, right - w - 19, mid, DIM);
				noteW += 30;
			}
		}
		// the item that plays (this playlist playing): its chip
		if (!edit)
			if (int k = ui.rowChip(i)) noteW += ui.drawChip(right - noteW, mid, k) + 10;
		int x = liked && !edit ? 66 : 96;
		if (miss)
		{
			static const std::string GONE = "[Gone] ";
			ui.drawTextMid(GONE, 1, DANGER, x, mid);
			x += (int)std::lround(ui.textWidth(GONE, 1) / sx);
		}
		ui.drawTextMid(ui.browse->browseRows[i].label, 1, miss ? soft : at ? ACCENT : TEXT, x, mid, std::max(0, right - 8 - x - noteW));
	}

	void Ui::folderChanged(const std::string& dir, Player& p)
	{
		std::string d = dir;
		while (d.size() > 1 && d.back() == '/') d.pop_back();
		if (page != Page::Browse || browse->browseDir != d || pseudoDir(d) || isNetPath(d)) return;
		// not under a menu, the picker or a move (their rows are the list's as it was)
		if (ctxOpen || browse->playlists->pickerOpen || browse->playlists->addPending.serial || browse->playlists->moving || browse->playlists->plDrag.on || kbOpen) return;
		std::string at = cursor >= 0 && cursor < (int)browse->browsePaths.size() ? browse->browsePaths[cursor] : "";
		int s = scroll;
		browse->enterFolder(d, p);
		for (size_t i = 0; i < browse->browsePaths.size(); i++)
			if (browse->browsePaths[i] == at) cursor = (int)i;
		scroll = s;
		browse->fitList();
		browse->browseVersion++;
	}

	// ---- the area ----

	bool Ui::Browse::key(Button b, Player& p)
	{
		// the updater's pop-up over the file list takes the keys while it shows
		if (ui.popupShown(p)) return ui.popupKey(b, p);
		// a network folder read before it's added: B cancels, nothing else
		if (playlists->addPending.serial)
		{
			if (b == Button::B) playlists->cancelAdd(p);
			return false;
		}
		if (playlists->pickerOpen)
		{
			playlists->pickerKey(b, p);
			return false;
		}
		// something playing behind the browser: SELECT goes back to it, L3 likes it (a page's own keys don't use them)
		if (!ui.inMenu && playlists->selMode == SEL_NONE && browseDir != FORMDIR)
		{
			if (b == Button::Select && ui.nowPlayable(p)) { ui.goNowPlaying(p); return false; }
			if (b == Button::L3 && p.playing() && !p.playingPhoto()) { ui.likeNow(p); return false; }
		}
		if (!ui.inMenu && playlists->selMode != SEL_NONE && playlists->selKey(b, p)) return false;
		// the redesigned playlist pages (a playlist's rows, its keys row, Liked): their own keys; the rest (X, Y,
		// B, L/R, START) is the browser's below
		if (!ui.inMenu && playlists->onPage() && playlists->key(b, p)) return false;
		if (!ui.inMenu && atHome()) return homeView->homeKey(b, p);
		// All recent: X Home's options, Y a file's menu (as on its card)
		if (!ui.inMenu && browseDir == RECENT && b == Button::X) { homeView->openHomeOptions(); return false; }
		if (!ui.inMenu && browseDir == RECENT && b == Button::Y)
		{
			if (ui.cursor >= 0 && ui.cursor < (int)browseRows.size() && browseRows[ui.cursor].id == 2) homeView->openRecentMenu(browsePaths[ui.cursor]);
			return false;
		}
		if (!ui.inMenu && b == Button::X) { ui.openBrowseMenu(ui.cursor, MENU_X); return false; }
		// no connections yet: Y adds one by hand (A, the page's button, finds them)
		if (!ui.inMenu && b == Button::Y && emptyAction() && browseDir == NETROOT) { openForm(nullptr, nullptr, p); return false; }
		if (!ui.inMenu && b == Button::Y) { ui.openBrowseMenu(ui.cursor, MENU_Y); return false; }
		// the form: LEFT/RIGHT step a choice (Type, Sign in, FTPS); L/R don't leave it for the other tab
		if (!ui.inMenu && browseDir == FORMDIR)
		{
			if (b == Button::L || b == Button::R) return false;
			if ((b == Button::Left || b == Button::Right) && ui.cursor >= 0 && ui.cursor < (int)browseRows.size())
			{
				formChange(browseRows[ui.cursor].id, b == Button::Left ? -1 : 1);
				return false;
			}
		}
		if (!ui.inMenu && browseDir == FINDNET && (b == Button::L || b == Button::R)) return false;
		if (!ui.inMenu && (b == Button::L || b == Button::R))
		{
			// All recent: its tabs (All, Videos, Music, Books)
			if (browseDir == RECENT)
			{
				if (p.settings.keepHistory && homeView->recentCounts[0]) homeView->stepRecentTab(b == Button::R ? 1 : -1, p);
				return false;
			}
			// a section each way, no wrap, at its top (All recent is none's)
			int t = browseTab + (b == Button::R ? 1 : -1);
			if (browseDir != RECENT && t >= 0 && t < BROWSE_TABS) openSection(t, p);
			return false;
		}
		// the Extensions drawer: A opens what's on (or Manage extensions with none on), START Manage extensions
		if (!ui.inMenu && browseDir == EXTDIR && ui.manga->extDrawerKey(b, p)) return false;
		if (b == Button::B)
		{
			// a section's top: Home, on its tile (All recent: on its card)
			if (atFirstPage())
			{
				if (browseDir == RECENT) homeView->goHome(Home::HOME_STRIP, homeView->stripCards(), p);
				else homeView->goHome(Home::HOME_GRID, browseTab, p);
				return false;
			}
			browseUp(p);
			return false;
		}
		// (on what plays behind it, as over the player)
		if (b == Button::Start) { ui.openMainMenu(p); return false; }
		// the list's own (UP/DOWN, a page with LEFT/RIGHT, A)
		return ui.listKey(b, p);
	}

	bool Ui::Browse::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		// the redesigned playlist pages: their rows, keys and bars test the Rects their draw used
		if (!ui.inMenu && playlists->onPage() && playlists->pointer(pt, x, y, p)) return false;
		// (its downs come late: after the header's "<" and the empty page's button; the rest where the readers' do)
		if (pt.kind != Pointer::Down)
		{
			// the Extensions drawer: a tile acts as the finger lifts (as Home's do)
			if (pt.kind == Pointer::Up && !ui.inMenu && browseDir == EXTDIR) return ui.manga->extPointer(pt, x, y, p);
			return false;
		}
		// the drawer: its own taps
		if (!ui.inMenu && browseDir == EXTDIR) return ui.manga->extPointer(pt, x, y, p);
		if (!ui.inMenu && playlists->selMode != SEL_NONE)
		{
			// selecting: no tabs (the header's "<" was taken above); the bar's buttons, those that can act now
			if (y < 60) return false;
			if (y >= 424)
			{
				int n = playlists->selCount(), ticked = 0, files = playlists->selFolderFiles(ticked);
				std::vector<std::pair<int, bool>> ids;
				if (playlists->moving) ids = {{MOVE_CANCEL, true}, {MOVE_DONE, true}};
				else if (playlists->selMode == SEL_EDIT) ids = {{SEL_ALL, !playlists->plCur.entries.empty()}, {SEL_MOVE, n > 0 && !ui.plIsLiked(playlists->plCur.file)}, {SEL_ACT, n > 0}};
				else if (playlists->selMode == SEL_ADD) ids = {{SEL_ALL, files > 0}, {SEL_CANCEL, true}, {SEL_ACT, n > 0}};
				else ids = {{SEL_CANCEL, true}, {SEL_ACT, playlists->pickable()}};
				for (auto& id : ids)
					if (id.second && ui.buttonRect(id.first).hit(x, y)) ui.pressedBtn = id.first;
				return false;
			}
			if (playlists->moving) return false;
			// Edit: a finger on a row's grip drags it at once
			if (playlists->selMode == SEL_EDIT && x >= 576 && y >= ROW_Y && y < ROW_Y + ROWS_SHOWN * ROW_H)
			{
				int i = ui.scroll + (y - ROW_Y) / ROW_H;
				if (playlists->selTickable(i)) { playlists->dragBegin(i, y, p); return false; }
			}
		}
		if (!ui.inMenu && atHome()) return homeView->homePointer(pt, x, y, p);
		if (!ui.inMenu && y < 60)
		{
			// a section's marker: that section (Home and All recent have none)
			if (browseDir != RECENT)
				for (int t = 0; t < BROWSE_TABS; t++)
					if (pipRect(t, p).hit(x, y)) openSection(t, p);
			return false;
		}
		// a row: the cursor on it, the touch begun
		return ui.listDown(x, y, p);
	}

	void Ui::Browse::draw(const Player& p)
	{
		// a playlist and Liked draw their own redesigned pages (inside lay::shell's body)
		if (!ui.inMenu && (browseDir == PLROOT || isPlDir(browseDir))) { playlists->draw(p); return; }
		// the list's page, as the track lists' (drawTracks: Home, a section's header, the rows, the footer)
		ui.drawTracks(p);
	}

	std::string Ui::Browse::signature(const Player&) const
	{
		// the section and its listing; what Copy / Move holds (the bar along the foot); Home's focus and strip
		std::string s = "|tab" + std::to_string(browseTab) + (netLoading ? "L" : "");
		// the playlist pages' focus over the rows (the keys row, a bar's key under a finger)
		s += "|plf" + std::to_string(playlists->keyFocus) + "," + std::to_string(playlists->barPress) + "," + std::to_string(playlists->keyPress);
		if (clipOp != CLIP_NONE) s += "|clip" + std::to_string(clipOp) + clipPath + (clipCanPaste() ? "p" : "");
		// Home: the focus, the strip's scroll, a finger on a card or a tile, the tiles' lines (the connections)
		if (atHome())
		{
			char hb[120];
			snprintf(hb, sizeof hb, "|home%d %d %d %ld %d %zu %d", homeView->homeZone, homeView->homeStrip, homeView->homeGrid, std::lround(homeView->stripOff), homeView->homePress,
				homeView->recent.size(), ui.netStore ? ui.netStore->version() : -1);
			s += hb;
			// B's quit dialog over it, and the row it has the focus on
			s += std::string("|quit") + std::to_string(homeView->quitOpen) + std::to_string(homeView->quitCursor);
		}
		return s;
	}

	// ---- Ui's, out of ui.h (they read the browser's state) ----

	void Ui::showPlayer()
	{
		if (!onPlayer) playerBack = {true, page, inMenu, menuTab, setGroup, cursor, scroll, menuReturnDir, menuReturnCursor, menuReturnScroll, browse->browseDir, browse->browseTab};
		if (!onPlayer) playerPage->listPulseArm = true;	// (a playlist's song: the List key glows twice, PlayerPage::draw)
		onPlayer = true;
		page = Page::Main;
		inMenu = false;
		bgPlay = false;
		ctxOpen = false;
		dropTouch();
	}

	std::string Ui::browseFolder() const { return browse->browseDir; }

	std::vector<std::string> Ui::netFolderNames(const std::string& dir) const
	{
		auto it = browse->netNames.find(dir);
		return it == browse->netNames.end() ? std::vector<std::string>() : it->second;
	}

	bool Ui::backIconShown(const Player&) const { return !(page == Page::Browse && !inMenu && browse->atHome()); }
}
