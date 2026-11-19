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
#include "ui_kit.h"
#include "session_host.h"
#include "pagebook.h"
#include "durworker.h"
#include <sys/statvfs.h>

namespace bp
{
	namespace
	{
		std::string fileBytes(uint64_t b)
		{
			char out[32];
			if (b >= (1ull << 30)) snprintf(out, sizeof out, "%.1f GB", b / 1073741824.0);
			else if (b >= (1ull << 20)) snprintf(out, sizeof out, "%.1f MB", b / 1048576.0);
			else if (b >= 1024) snprintf(out, sizeof out, "%llu KB", (unsigned long long)(b / 1024));
			else snprintf(out, sizeof out, "%llu bytes", (unsigned long long)b);
			return out;
		}
	}
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
		// a folder changed under the Files select mode (a card pulled, another page): the ticks belong to the old one
		if (fileSel && dir != browseDir)
		{
			fileSel = false;
			if (playlists->selMode == SEL_PICK) playlists->selMode = SEL_NONE;
			fileSelPaths.clear();
			fileSelOrder.clear();
		}
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
				// the focus starts on Continue, else the newest card (nothing played yet: the empty state)
				homeView->homeZone = homeView->continueOf(p).shown ? Home::HOME_CONTINUE
					: homeView->recent.empty() ? Home::HOME_EMPTY : Home::HOME_CARDS;
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
					homeView->homeZone = homeView->continueOf(p).shown ? Home::HOME_CONTINUE : Home::HOME_EMPTY;
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
		filesFooterAt = 0;	// (the count and the free room: read again)
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
		// the redesigned Files page has no ".." row: B and the header's back key go up (the spec's change)
		if (filesPage()) return false;
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

	// ---- the Files page (docs/redesign: files, files-playing, select, actions, delete, empty-folder, readonly,
	// files-music) ----

	bool Ui::Browse::filesPage() const
	{
		// the cards' places and the folders under them; Home, All recent, the playlists, the network and the
		// Extensions drawer keep Ui's list
		return ui.page == Page::Browse && !ui.inMenu && (playlists->selMode == SEL_NONE || fileSel) && browseTab == TAB_LOCAL &&
			(browseDir.empty() || (!pseudoDir(browseDir) && !isNetPath(browseDir)));
	}

	bool Ui::Browse::filesEmpty() const
	{
		return browseRows.empty() || (browseRows.size() == 1 && browseRows[0].id < 0);
	}

	bool Ui::Browse::filesReadOnly() const
	{
		return !browseDir.empty() && !pseudoDir(browseDir) && !isNetPath(browseDir) && access(browseDir.c_str(), W_OK) != 0;
	}

	std::string Ui::Browse::filesTitle() const
	{
		if (browseDir.empty()) return "Files";
		for (auto& r : ui.roots)
			if (r.path == browseDir) return r.label;
		return baseName(browseDir);
	}

	std::string Ui::Browse::filesCrumb() const
	{
		if (browseDir.empty()) return "Your folders";
		std::string where = ui.whereOf(browseDir);
		std::string out = "Files \xE2\x80\xBA ";
		for (size_t i = 0; i < where.size(); i++)
		{
			if (where.compare(i, 3, " / ") == 0) { out += " \xE2\x80\xBA "; i += 2; }
			else out += where[i];
		}
		return out;
	}

	std::string Ui::Browse::filesFooter() const
	{
		// the folder's count and the free room on the card it is on (a card's place, in a folder's page)
		if (browseDir.empty() || filesEmpty()) return "";
		Uint32 now = SDL_GetTicks();
		if (filesFooterDir == browseDir && filesFooterAt && now - filesFooterAt < 2000) return filesFooterCache;
		int n = 0;
		for (auto& r : browseRows)
			if (r.id >= 0) n++;
		std::string text = std::to_string(n) + (n == 1 ? " item" : " items");
		struct statvfs vs;
		if (statvfs(browseDir.c_str(), &vs) == 0 && vs.f_frsize)
		{
			uint64_t free = (uint64_t)vs.f_bavail * (uint64_t)vs.f_frsize;
			if (free > 0) text += " \xC2\xB7 " + fileBytes(free) + " free";
		}
		filesFooterCache = text;
		filesFooterDir = browseDir;
		filesFooterAt = now;
		return text;
	}

	lay::FilesPage Ui::Browse::filesLayout(const Player& p) const
	{
		lay::Shell s = lay::shell(ui.miniBarShown(p));
		int n = (int)browseRows.size();
		bool footer = !ui.miniBarShown(p) && !filesEmpty();
		return lay::filesPage(s.body, n, fileSel, footer);
	}

	int Ui::Browse::filesShown(const Player& p) const { return filesLayout(p).shown; }

	int Ui::Browse::filesScroll(const Player& p) const
	{
		int n = (int)browseRows.size(), shown = filesShown(p);
		return lay::listScroll(std::max(0, ui.cursor), ui.scroll, shown, n);
	}

	std::vector<Ui::Hint> Ui::Browse::filesHints(const Player& p) const
	{
		if (fileSel)
			return {{"A", "Tick"}, {"SELECT", "All / none"}, {"X", "More"}, {"B", "Done"}};
		if (clipOp != CLIP_NONE)
			return {{"SELECT", "Put it here"}, {"X", "Cancel"}, {"A", "Open"}, {"B", "Up"}};
		if (filesEmpty())
		{
			if (browseDir.empty()) return {{"L/R", "Sections"}, {"START", "Settings"}};
			if (!browseRows.empty() && browseRows[0].label == "This folder can't be opened")
				return {{"A/B", "Go up"}, {"X", "More"}, {"L/R", "Sections"}};
			return {{"A/B", "Go up"}, {"Y", "Send files"}, {"X", "More"}, {"L/R", "Sections"}};
		}
		std::vector<Hint> h = {{"A", "Play / open"}, {"B", browseDir.empty() ? "Home" : "Up"}, {"X", "More"}, {"Y", "Select"}};
		if (ui.miniBarShown(p)) h.push_back({"SELECT", "Now playing"});
		else h.push_back({"LEFT/RIGHT", "Page"});
		h.push_back({"L/R", "Sections"});
		return h;
	}

	void Ui::Browse::frame(const Player& p)
	{
		// (until the shared shell lands: this page's header and hint bar; the merge swaps only this function)
		lay::Shell s = lay::shell(ui.miniBarShown(p));
		lay::FilesHeader h = lay::filesHeader(s.header);
		Kit& k = *ui.kit;
		k.iconKey(h.back, G_BACK, filesPressed == 200 || ui.pressedBtn == BACK_ICON, 2);
		if (!fileSel) k.iconKey(h.more, G_MORE, filesPressed == 201, 2);
		// the status cluster: what plays, the battery and the clock; the read-only badge left of them
		Kit::Status st;
		st.playing = ui.bgPlay && p.playing() && !p.playingPhoto();
		st.battery = ui.battery.percent;
		st.charging = ui.battery.charging;
		st.clock = ui.clockText(p);
		if (filesReadOnly()) st.badges.push_back({&G_LOCK, "Read only", false});
		int right = k.status(h.more.x - 10, s.header.y + s.header.h / 2, st);
		int room = std::max(40, right - 12 - h.title.x);
		std::string title = fileSel ? (fileSelCount() ? std::to_string(fileSelCount()) + " selected" : std::string("Select")) : filesTitle();
		ui.drawTextMid(title, F_P28, TEXT, h.title.x, h.title.y + h.title.h / 2, room);
		std::string crumb = filesCrumb();
		if (k.textW(crumb, F_I12) > room) crumb = ui.elideLeft(crumb, F_I12, room);
		ui.drawTextMid(crumb, F_I12, DIM, h.crumb.x, h.crumb.y + h.crumb.h / 2, room);
		k.hints(filesHints(p));
	}

	void Ui::Browse::drawFilesRow(int i, const lay::FilesPage& f, int onScreen, bool at, const Player& p)
	{
		Rect r = f.row(onScreen);
		Kit& k = *ui.kit;
		int cy = r.y + r.h / 2;
		bool ticked = fileTicked(i);
		if (at) k.flat(r, BUTTON, 10, ACCENT);
		else if (fileSel && ticked) k.flat(r, SURFACE, 10, k.edgeOf(SURFACE));
		SDL_Color ink = at ? ACCENT : TEXT;
		SDL_Color soft = at ? DIM_ON : DIM;
		int x = r.x + 12, right = r.x + r.w - 12;
		if (fileSel)
		{
			ui.drawGlyphMid(ticked ? G_BOX_ON : G_BOX, x + 11, cy, ticked ? ACCENT : soft);
			x += 22 + 11;
		}
		RowIcon icon = ui.rowIcon(i);
		const Glyph* g = icon == ICON_RECENT ? &G_CLOCK : icon == ICON_PLACE ? &G_CARD : icon == ICON_FOLDER ? &G_FOLDER
			: icon == ICON_VIDEO ? &G_FILM : icon == ICON_SONG ? &G_NOTE : icon == ICON_PHOTO ? &G_PICTURE
			: icon == ICON_DOC ? &G_PAGE : icon == ICON_BOOK ? &G_BOOK : icon == ICON_FILE ? &G_FILE
			: icon == ICON_PDF ? &G_PDF : icon == ICON_EPUB ? &G_EPUB : icon == ICON_COMIC ? &G_COMIC
			: icon == ICON_NETWORK ? &G_NETWORK : icon == ICON_REFRESH ? &G_REFRESH : icon == ICON_TYPE ? &G_PENCIL
			: icon == ICON_TEST ? &G_PLUG : icon == ICON_SAVE ? &G_CHECK : icon == ICON_PLAYLIST ? &G_CHECK
			: icon == ICON_LIKED ? &G_HEART : icon == ICON_NEW ? &G_PLUS : nullptr;
		if (g)
		{
			ui.drawGlyphMid(*g, x + 10, cy, at ? ACCENT : TEXT);
			x += 20 + 11;
		}
		// the right side: the playing / paused pill, a heart, then the length and size
		int noteW = 0;
		if (int kind = ui.rowChip(i))
		{
			static const char* WORDS[] = {"", "Playing", "Paused", "In browser", "Stopped"};
			Kit::Pill tone = kind == 1 || kind == 3 ? Kit::Pill::Live : Kit::Pill::Fact;
			int w = k.pill(right, cy, WORDS[std::clamp(kind, 0, 4)], tone);
			right -= w + 9;
		}
		if (i >= 0 && i < (int)browseRows.size() && browseRows[i].id == 2 && playlists->plStore && i < (int)browsePaths.size() &&
			playlists->plStore->isLiked(browsePaths[i]))
		{
			ui.drawGlyphMid(G_HEART_S, right - 5, cy, ACCENT);
			right -= 22;
		}
		if (i >= 0 && i < (int)browseRows.size() && !browseRows[i].note.empty())
		{
			std::string note = browseRows[i].note;
			int w = std::min(260, k.textW(note, F_I12));
			ui.drawTextMid(note, F_I12, soft, right, cy, std::min(260, right - x), 1);
			noteW = w + 10;
		}
		int room = std::max(1, right - noteW - x);
		std::string name = i >= 0 && i < (int)browseRows.size() ? browseRows[i].label : std::string();
		if (!fileSel && at) ui.drawMarquee(name, F_SMALL, ink, x, cy, room, BUTTON, 1);
		else
		{
			// a file's name keeps its ending when it is cut ("...Sintel.mkv"), as the browser always did
			if (!fileSel && i >= 0 && i < (int)browseRows.size() && browseRows[i].id == 2) name = ui.elideLeft(name, F_SMALL, room);
			ui.drawTextMid(name, F_SMALL, ink, x, cy, room);
		}
	}

	void Ui::Browse::drawFilesEmpty(const lay::FilesPage& f, const Player& p)
	{
		bool folder = !browseDir.empty();
		bool broken = !browseRows.empty() && browseRows[0].label == "This folder can't be opened";
		std::string title = !browseRows.empty() ? browseRows[0].label : "Nothing to play here";
		std::string line = broken ? "Is the card still in?" : folder ? "No videos, music or photos in this folder." : "No cards or folders to show.";
		int keys = folder ? (broken ? 1 : 2) : 0;
		lay::EmptyState l = lay::emptyState(f.rows, 1, keys);
		Kit::Empty e;
		e.glyph = broken ? &G_ALERT : &G_FOLDER;
		e.title = title;
		e.lines = {line};
		if (keys)
		{
			e.key1 = "Go up";
			e.glyph1 = &G_BACK;
		}
		if (keys == 2)
		{
			e.key2 = "Send files";
			e.glyph2 = &G_NETWORK;
		}
		e.hotkeys = p.settings.buttonHotkeys;
		ui.kit->emptyState(l, e);
	}

	void Ui::Browse::drawFiles(const Player& p)
	{
		lay::FilesPage f = filesLayout(p);
		if (filesEmpty())
		{
			drawFilesEmpty(f, p);
			frame(p);
			return;
		}
		int scroll = filesScroll(p), n = (int)browseRows.size();
		for (int k = 0; k < f.shown && scroll + k < n; k++) drawFilesRow(scroll + k, f, k, scroll + k == ui.cursor, p);
		if (fileSel)
		{
			int media = 0;
			for (auto& path : fileSelOrder)
				if (isVideoFile(path) || isAudioFile(path)) media++;
			bool ro = filesReadOnly(), any = fileSelCount() > 0;
			Kit::BarKey keys[4] = {
				{&G_PLUS, "Playlist", "", Kit::Tone::Normal, filesPressed == 100, !any || !media},
				{&G_PAGE, "Copy", "", Kit::Tone::Normal, filesPressed == 101, !any},
				{&G_MOVE, "Move", "", Kit::Tone::Normal, filesPressed == 102, !any || ro},
				{&G_TRASH, "Delete", "", Kit::Tone::Danger, filesPressed == 103, !any || ro},
			};
			ui.kit->selectBar(f.bar, keys);
		}
		else if (f.footer.w > 0)
			ui.drawTextMid(filesFooter(), F_I12, DIM, f.footer.x, f.footer.y + f.footer.h / 2, f.footer.w);
		frame(p);
	}

	bool Ui::Browse::fileTickable(int row) const
	{
		return row >= 0 && row < (int)browseRows.size() && fileOpsOn(row);
	}

	bool Ui::Browse::fileTicked(int row) const
	{
		return fileTickable(row) && row < (int)browsePaths.size() && fileSelPaths.count(browsePaths[row]) > 0;
	}

	void Ui::Browse::filesTickRow(int row)
	{
		if (!fileTickable(row) || row >= (int)browsePaths.size()) return;
		const std::string& path = browsePaths[row];
		if (fileSelPaths.erase(path))
			fileSelOrder.erase(std::remove(fileSelOrder.begin(), fileSelOrder.end(), path), fileSelOrder.end());
		else
		{
			fileSelPaths.insert(path);
			fileSelOrder.push_back(path);
		}
		browseVersion++;
	}

	void Ui::Browse::filesMove(int to, Player& p)
	{
		int n = (int)browseRows.size();
		if (n <= 0) return;
		ui.cursor = std::clamp(to, 0, n - 1);
		ui.scroll = lay::listScroll(ui.cursor, ui.scroll, filesShown(p), n);
	}

	void Ui::Browse::filesSelStart(Player& p)
	{
		int any = -1;
		for (int i = 0; i < (int)browseRows.size() && any < 0; i++)
			if (fileTickable(i)) any = i;
		if (any < 0) { p.osd("Nothing to select here"); return; }
		fileSel = true;
		// (miniBarShown still hides the bar while the browser selects: the bar would sit under this one)
		playlists->selMode = SEL_PICK;
		fileSelPaths.clear();
		fileSelOrder.clear();
		filesPressed = filesTouch = -1;
		if (fileTickable(ui.cursor)) filesTickRow(ui.cursor);
		else filesTickRow(any);
		ui.scroll = lay::listScroll(ui.cursor, ui.scroll, filesShown(p), (int)browseRows.size());
		browseVersion++;
	}

	void Ui::Browse::filesSelEnd(Player&)
	{
		fileSel = false;
		if (playlists->selMode == SEL_PICK) playlists->selMode = SEL_NONE;
		fileSelPaths.clear();
		fileSelOrder.clear();
		filesPressed = filesTouch = -1;
		browseVersion++;
	}

	void Ui::Browse::filesSelAll(Player&)
	{
		std::vector<int> rows;
		for (int i = 0; i < (int)browseRows.size(); i++)
			if (fileTickable(i)) rows.push_back(i);
		if (rows.empty()) return;
		bool all = true;
		for (int i : rows) all = all && fileTicked(i);
		for (int i : rows)
		{
			if (i >= (int)browsePaths.size()) continue;
			const std::string& path = browsePaths[i];
			if (all) fileSelPaths.erase(path);
			else if (fileSelPaths.insert(path).second) fileSelOrder.push_back(path);
		}
		if (all) fileSelOrder.erase(std::remove_if(fileSelOrder.begin(), fileSelOrder.end(),
			[&](const std::string& s) { return fileSelPaths.count(s) == 0; }), fileSelOrder.end());
		browseVersion++;
	}

	void Ui::Browse::filesClipStart(int op, Player&)
	{
		if (fileSelOrder.empty() || (op == CLIP_MOVE && filesReadOnly())) return;
		clipOp = op;
		clipPaths = fileSelOrder;
		clipPath = clipPaths.front();
		fileSel = false;
		if (playlists->selMode == SEL_PICK) playlists->selMode = SEL_NONE;
		fileSelPaths.clear();
		fileSelOrder.clear();
		browseVersion++;
	}

	void Ui::Browse::filesSelAct(int what, Player& p)
	{
		if (what == 3)
		{
			if (filesReadOnly()) { p.osd("This folder is read only"); return; }
			filesDeleteAsk(fileSelOrder, p);
			return;
		}
		if (what == 0)
		{
			std::vector<PlaylistEntry> add;
			for (auto& path : fileSelOrder)
				if (isVideoFile(path) || isAudioFile(path))
				{
					PlaylistEntry e;
					e.path = path;
					e.title = entryTitle(path);
					add.push_back(e);
				}
			if (add.empty()) { p.osd("Playlists take videos and songs"); return; }
			playlists->picked.clear();
			playlists->pickOrder.clear();
			for (auto& e : add)
			{
				playlists->picked.insert(e.path);
				playlists->pickOrder.push_back(e.path);
			}
			playlists->openPicker("", p);
			return;
		}
		if ((what == 1 || what == 2) && fileSelCount())
		{
			if (what == 2 && filesReadOnly()) { p.osd("This folder is read only"); return; }
			if (what == 2)
				for (auto& path : fileSelOrder)
					if (p.playing() && (p.currentPath() == path || p.currentPath().rfind(path + "/", 0) == 0))
					{
						ui.alert("It\xE2\x80\x99s playing", "Stop it first, then try again.");
						return;
					}
			filesClipStart(what == 1 ? CLIP_COPY : CLIP_MOVE, p);
			p.osd(std::string(what == 1 ? "Copy" : "Move") + ": open the folder it goes in, then SELECT", 2500);
		}
	}

	void Ui::Browse::filesSelMore(Player& p)
	{
		std::vector<std::string> paths = fileSelOrder;
		std::string title = paths.size() == 1 ? baseName(paths[0]) : std::to_string(paths.size()) + " selected";
		bool ro = filesReadOnly();
		ui.openMore([this, paths, title, ro](const Player& pl) {
			MoreSpec m;
			m.glyph = &G_BOX_ON;
			m.title = title;
			m.note = std::to_string(paths.size()) + (paths.size() == 1 ? " file" : " files");
			auto row = [](MoreItem it) { return it; };
			m.rows.push_back(row({&G_PLUS, "Add to playlist\xE2\x80\xA6", "", "", [this](Player& p) { filesSelAct(0, p); return false; }}));
			m.rows.push_back(row({&G_PAGE, "Copy\xE2\x80\xA6", "", "", [this](Player& p) { filesSelAct(1, p); return false; }}));
			m.rows.push_back(row({&G_MOVE, "Move\xE2\x80\xA6", ro ? "This folder is read only" : "", "", [this](Player& p) { filesSelAct(2, p); return false; }, false, ro}));
			m.rows.push_back(row({&G_TRASH, "Delete", ro ? "This folder is read only" : "Asks first", "", [this](Player& p) { filesSelAct(3, p); return false; }, true, ro}));
			MoreItem info{&G_INFO, "Info", "", "", [this](Player& p) { if (ui.cursor >= 0 && ui.cursor < (int)browsePaths.size()) ui.openFileInfo(browsePaths[ui.cursor], ""); return false; }};
			m.rows.push_back(info);
			return m;
		});
	}

	void Ui::Browse::filesRefresh(Player& p)
	{
		std::string at = ui.cursor >= 0 && ui.cursor < (int)browsePaths.size() ? browsePaths[ui.cursor] : "";
		int c = ui.cursor, s = ui.scroll;
		enterFolder(browseDir, p);
		ui.cursor = c;
		ui.scroll = s;
		for (size_t i = 0; i < browsePaths.size(); i++)
			if (!at.empty() && browsePaths[i] == at) ui.cursor = (int)i;
		fitList();
	}

	void Ui::Browse::filesPlayAll(const std::string& dir, bool shuffle, Player& p)
	{
		bool ok = false;
		std::vector<std::string> paths;
		for (auto& e : listFolder(dir, ok, p.settings.showHiddenFolders))
			if (!e.dir && isMediaFile(e.path)) paths.push_back(e.path);
		if (!ok || paths.empty()) { p.osd("Nothing to play here"); return; }
		int index = 0;
		if (shuffle)
		{
			std::random_device rd;
			index = (int)(rd() % (unsigned)paths.size());
			SessionHost::get().setShuffle(true);
		}
		ui.playRequest = {};
		ui.playRequest.paths = paths;
		ui.playRequest.index = index;
		ui.playRequest.source.kind = SourceKind::Folder;
		ui.playRequest.source.title = baseName(dir);
		p.setLastFolder(dir);
	}

	void Ui::Browse::filesToggleHidden(const std::string& path, Player& p)
	{
		std::string marker = path + "/.hidden";
		bool ok;
		if (isHiddenFolder(path)) ok = unlink(marker.c_str()) == 0;
		else
		{
			FILE* f = fopen(marker.c_str(), "w");
			ok = f && fclose(f) == 0;
		}
		if (!ok) { ui.alert("Couldn\xE2\x80\x99t change the folder", "Is the card read only?"); return; }
		filesRefresh(p);
	}

	void Ui::Browse::filesToggleShowHidden(Player& p)
	{
		Settings st = p.settings;
		st.showHiddenFolders = !st.showHiddenFolders;
		p.setSettings(st);
		filesRefresh(p);
	}

	void Ui::Browse::filesOpenMore(Player& p)
	{
		// the focused row (or the folder it is in, on an empty page): the item's actions on the left, this folder's
		// on the right, as the mock-up's More sheet
		bool onRow = ui.cursor >= 0 && ui.cursor < (int)browseRows.size() && ui.cursor < (int)browsePaths.size() && browseRows[ui.cursor].id >= 0;
		std::string path = onRow ? browsePaths[ui.cursor] : browseDir;
		int id = onRow ? browseRows[ui.cursor].id : 1;
		std::string label = onRow ? browseRows[ui.cursor].label : filesTitle();
		std::string note = onRow ? browseRows[ui.cursor].note : std::string();
		std::string dir = browseDir.empty() ? path : browseDir;
		bool ro = filesReadOnly();
		bool ops = onRow && fileOpsOn(ui.cursor);
		bool media = onRow && id == 2 && isMediaFile(path);
		bool likable = onRow && id == 2 && playlists->plStore && PlaylistStore::likable(path);
		bool liked = likable && playlists->plStore->isLiked(path);
		bool song = onRow && id == 2 && isAudioFile(path);
		bool doc = onRow && id == 2 && !isMediaFile(path);
		ui.openMore([this, path, id, label, note, dir, ro, ops, media, likable, song, doc, onRow](const Player& pl) {
			bool liked = playlists->plStore && playlists->plStore->isLiked(path);
			MoreSpec m;
			m.glyph = id == 2 ? (isAudioFile(path) ? &G_NOTE : isVideoFile(path) ? &G_FILM : &G_FILE) : &G_FOLDER;
			m.title = label.empty() ? filesTitle() : label;
			m.note = note;
			std::vector<MoreItem> left, right;
			auto item = [](const Glyph* g, std::string text, std::string nt, std::function<bool(Player&)> act, bool danger = false,
				bool off = false, bool lit = false, bool stay = false, std::string key = "") {
				MoreItem i;
				i.glyph = g;
				i.label = std::move(text);
				i.note = std::move(nt);
				i.act = std::move(act);
				i.danger = danger;
				i.off = off;
				i.lit = lit;
				i.stay = stay;
				i.key = std::move(key);
				return i;
			};
			if (id == 2 && media)
				left.push_back(item(&G_PLAY, "Play from the start", "", [this, path](Player& p) {
					if (p.playing() && p.currentPath() == path)
					{
						p.seekFraction(0);
						if (!ui.pageIsOut()) p.unpause();
						else ui.noteSeek(p, 0);
						ui.showPlayer();
					}
					else
					{
						p.forgetPosition(path);
						ui.openFile(path, p);
					}
					return false;
				}));
			if (doc)
				left.push_back(item(&G_FILE, "Open", "", [this, path](Player& p) { ui.openFile(path, p); return false; }));
			if (id == 1 && onRow)
				left.push_back(item(&G_FOLDER, "Open", "", [this, path](Player& p) {
					if (isNetPath(path)) { ui.browse->netSelect.clear(); enterFolder(path, p); }
					else { setSection(TAB_LOCAL); enterFolder(path, p); }
					p.setLastFolder(path);
					return false;
				}));
			if (onRow && id == 2 && (isVideoFile(path) || isAudioFile(path)) && playlists->plStore)
				left.push_back(item(&G_PLUS, "Add to playlist\xE2\x80\xA6", "", [this, path, label](Player& p) {
					playlists->openAddOne(path, label, -1, p);
					return false;
				}));
			if (likable)
				left.push_back(item(liked ? &G_HEART : &G_HEART_O, liked ? "Unlike" : "Like", "", [this, path, label, liked](Player& p) {
					bool was = playlists->plStore && playlists->plStore->isLiked(path);
					if (ui.setLikedPath(p, path, label, -1, !was) != was) ui.likeCard(p, !was, label);
					return false;
				}, false, false, liked, true));
			if (song)
			{
				left.push_back(item(&G_CARD, "Go to album", "Comes with Music", nullptr, false, true));
				left.push_back(item(&G_NOTE, "Go to artist", "Comes with Music", nullptr, false, true));
			}
			if (id == 2)
				left.push_back(item(&G_FOLDER, "Show in folder", "", [this, path](Player& p) { ui.showInFolder(path, p); return false; }));
			if (onRow)
				left.push_back(item(&G_INFO, "Info", "", [this, path](Player& p) { ui.openFileInfo(path, ""); return false; }));
			if (ops)
			{
				left.push_back(item(&G_PAGE, "Copy\xE2\x80\xA6", "", [this, path](Player& p) { fileOpsAct(CTX_F_COPY, path, p); return false; }));
				left.push_back(item(&G_MOVE, "Move\xE2\x80\xA6", ro ? "This folder is read only" : "", [this, path](Player& p) { fileOpsAct(CTX_F_MOVE, path, p); return false; }, false, ro));
				left.push_back(item(&G_PENCIL, "Rename\xE2\x80\xA6", ro ? "This folder is read only" : "", [this, path](Player& p) { fileOpsAct(CTX_F_RENAME, path, p); return false; }, false, ro));
				left.push_back(item(&G_TRASH, "Delete", ro ? "This folder is read only" : "", [this, path](Player& p) {
					std::vector<std::string> one{path};
					filesDeleteAsk(one, p);
					return false;
				}, true, ro));
			}
			// this folder's own
			if (!fileSel) right.push_back(item(&G_BOX, "Select files", "", [this](Player& p) { filesSelStart(p); return false; }, false, false, false, false, "Y"));
			int n = 0;
			if (dir == browseDir)
			{
				for (size_t i = 0; i < browseRows.size() && i < browsePaths.size(); i++)
					if (browseRows[i].id == 2 && isMediaFile(browsePaths[i])) n++;
			}
			else
			{
				bool ok = false;
				for (auto& e : listFolder(dir, ok, pl.settings.showHiddenFolders))
					if (!e.dir && isMediaFile(e.path)) n++;
			}
			if (n)
			{
				right.push_back(item(&G_PLAY, "Play all " + std::to_string(n), "", [this, dir](Player& p) { filesPlayAll(dir, false, p); return false; }));
				right.push_back(item(&G_SHUFFLE, "Shuffle play", "", [this, dir](Player& p) { filesPlayAll(dir, true, p); return false; }));
			}
			if (playlists->plStore && !dir.empty() && !pseudoDir(dir))
				right.push_back(item(&G_PLUS, "Add folder to playlist\xE2\x80\xA6", "", [this, dir](Player& p) { playlists->openPicker(dir, p); return false; }));
			right.push_back(item(&G_REFRESH, "Refresh", "", [this](Player& p) { filesRefresh(p); return false; }));
			if (!dir.empty() && !pseudoDir(dir))
				right.push_back(item(&G_EYE, isHiddenFolder(dir) ? "Show folder" : "Hide folder", "",
					[this, dir](Player& p) { filesToggleHidden(dir, p); return false; }, false, ro));
			right.push_back(item(&G_EYE, "Show hidden folders", pl.settings.showHiddenFolders ? "On" : "Off",
				[this](Player& p) { filesToggleShowHidden(p); return false; }));
			int max = std::max(left.size(), right.size());
			for (int i = 0; i < max; i++)
			{
				if (i < (int)left.size()) m.rows.push_back(left[i]);
				if (i < (int)right.size()) m.rows.push_back(right[i]);
			}
			m.cols = left.empty() || right.empty() ? 1 : 2;
			return m;
		});
	}

	bool Ui::Browse::filesKey(Button b, Player& p)
	{
		if (fileSel)
		{
			// the sentinel that keeps the mini bar away while ticking (a picker's selEnd may have cleared it)
			if (playlists->selMode == SEL_NONE) playlists->selMode = SEL_PICK;
			int shown = std::max(1, filesShown(p));
			switch (b)
			{
			case Button::Up: filesMove(ui.cursor - 1, p); return true;
			case Button::Down: filesMove(ui.cursor + 1, p); return true;
			case Button::Left: filesMove(ui.cursor - shown, p); return true;
			case Button::Right: filesMove(ui.cursor + shown, p); return true;
			case Button::A: filesTickRow(ui.cursor); return true;
			case Button::Select: filesSelAll(p); return true;
			case Button::X: filesSelMore(p); return true;
			case Button::B: filesSelEnd(p); return true;
			default: return true;	// (START, L/R: nothing while ticking)
			}
		}
		if (b == Button::Y && filesEmpty() && !browseDir.empty() &&
			(browseRows.empty() || browseRows[0].label != "This folder can't be opened")) { ui.openWeb(); return true; }
		int shown = std::max(1, filesShown(p));
		switch (b)
		{
		case Button::Up: filesMove(ui.cursor - 1, p); return true;
		case Button::Down: filesMove(ui.cursor + 1, p); return true;
		case Button::Left: filesMove(ui.cursor - shown, p); return true;
		case Button::Right: filesMove(ui.cursor + shown, p); return true;
		case Button::A:
			if (filesEmpty() || ui.cursor < 0 || ui.cursor >= (int)browseRows.size() || browseRows[ui.cursor].id < 0) browseUp(p);
			else ui.choose(ui.cursor, p);
			return true;
		case Button::Y: filesSelStart(p); return true;
		case Button::X: filesOpenMore(p); return true;
		default: return false;
		}
	}

	bool Ui::Browse::filesPointer(const Pointer& pt, int x, int y, Player& p)
	{
		lay::Shell sh = lay::shell(ui.miniBarShown(p));
		lay::FilesHeader h = lay::filesHeader(sh.header);
		lay::FilesPage f = filesLayout(p);
		int n = (int)browseRows.size(), scroll = filesScroll(p);
		auto emptyKeys = [&](int& keys) {
			bool folder = !browseDir.empty();
			bool broken = !browseRows.empty() && browseRows[0].label == "This folder can't be opened";
			keys = folder ? (broken ? 1 : 2) : 0;
			return lay::emptyState(f.rows, 1, keys);
		};
		if (pt.kind == Pointer::Down)
		{
			filesTouch = filesPressed = -1;
			filesScrolled = false;
			// the header's keys
			if (h.backHit.hit(x, y)) { filesPressed = 200; return false; }
			if (!fileSel && h.moreHit.hit(x, y)) { filesPressed = 201; return false; }
			if (fileSel)
			{
				for (int i = 0; i < 4; i++)
					if (f.bar[i].hit(x, y)) { filesPressed = 100 + i; return false; }
			}
			if (filesEmpty())
			{
				int keys = 0;
				lay::EmptyState e = emptyKeys(keys);
				for (int i = 0; i < keys; i++)
					if (e.key[i].hit(x, y)) { filesPressed = 300 + i; return false; }
				return false;
			}
			if (y >= f.rows.y && y < f.rows.y + f.rows.h)
			{
				int k = (y - f.rows.y) / lay::FILE_ROW_H;
				if (k >= 0 && k < f.shown && scroll + k < n)
				{
					ui.cursor = scroll + k;
					filesTouch = ui.cursor;
					filesTouchY = y;
					filesScrollStart = scroll;
					return false;
				}
			}
			return false;
		}
		if (pt.kind == Pointer::Move && filesPressed < 0 && filesTouch >= 0)
		{
			if (filesScrolled || std::abs(y - filesTouchY) > 12)
			{
				filesScrolled = true;
				int moved = (int)std::lround((filesTouchY - y) / (double)lay::FILE_ROW_H);
				int maxScroll = std::max(0, n - f.shown);
				ui.scroll = std::clamp(filesScrollStart + moved, 0, maxScroll);
				ui.cursor = std::clamp(ui.cursor, ui.scroll, ui.scroll + std::max(0, f.shown - 1));
			}
			return false;
		}
		if (pt.kind != Pointer::Up) return false;
		int pressed = filesPressed, touch = filesTouch;
		filesPressed = -1;
		filesTouch = -1;
		if (pressed == 200)
		{
			if (h.backHit.hit(x, y))
			{
				if (fileSel) filesSelEnd(p);
				else if (atFirstPage()) homeView->goHome(Home::HOME_GRID, browseTab, p);
				else browseUp(p);
			}
			return false;
		}
		if (pressed == 201) { if (h.moreHit.hit(x, y)) filesOpenMore(p); return false; }
		if (pressed >= 100 && pressed < 104)
		{
			int i = pressed - 100, media = 0;
			for (auto& path : fileSelOrder)
				if (isVideoFile(path) || isAudioFile(path)) media++;
			bool any = fileSelCount() > 0, ro = filesReadOnly();
			bool on = i == 0 ? any && media : i == 1 ? any : any && !ro;
			if (on && f.bar[i].hit(x, y)) filesSelAct(i, p);
			return false;
		}
		if (pressed >= 300)
		{
			int keys = 0;
			lay::EmptyState e = emptyKeys(keys);
			int i = pressed - 300;
			if (i < keys && e.key[i].hit(x, y))
			{
				if (i == 0) browseUp(p);
				else ui.openWeb();
			}
			return false;
		}
		if (touch >= 0 && !filesScrolled && touch < n)
		{
			Rect r = f.row(touch - scroll);
			if (filesScrolled || !r.hit(x, y)) return false;
			if (fileSel) filesTickRow(touch);
			else { ui.cursor = touch; ui.choose(touch, p); }
		}
		return false;
	}

	void Ui::drawTracks(const Player& p)
	{
		bool inBrowse = page == Page::Browse;
		bool sel = inBrowse && !inMenu && browse->playlists->selMode != SEL_NONE;
		if (inBrowse && !inMenu && !sel && browse->atHome()) { browse->homeView->drawHome(p); return; }
		// All recent is Home's own page (its filter chips, every card, the kit's rows)
		if (inBrowse && !inMenu && !sel && browse->browseDir == RECENT) { browse->homeView->drawAllRecent(p); return; }
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
		// the Files page's own keys (the redesign): the D-pad, A, Y and X; a select mode takes every key, before
		// SELECT's Now playing and L3's Like
		if (!ui.inMenu && filesPage() && filesKey(b, p)) return false;
		// something playing behind the browser: SELECT goes back to it, L3 likes it (a page's own keys don't use them)
		if (!ui.inMenu && playlists->selMode == SEL_NONE && browseDir != FORMDIR)
		{
			if (b == Button::Select && ui.nowPlayable(p)) { ui.goNowPlaying(p); return false; }
			if (b == Button::L3 && p.playing() && !p.playingPhoto()) { ui.likeNow(p); return false; }
		}
		if (!ui.inMenu && playlists->selMode != SEL_NONE && playlists->selKey(b, p)) return false;
		if (!ui.inMenu && atHome()) return homeView->homeKey(b, p);
		// All recent: X the focused card's More sheet (Home's own things are its right column), else Home's; Y the same
		// card menu (kept, as it was the key for it)
		if (!ui.inMenu && browseDir == RECENT && b == Button::X)
		{
			if (ui.cursor >= 0 && ui.cursor < (int)browseRows.size() && browseRows[ui.cursor].id == 2) homeView->openRecentMenu(browsePaths[ui.cursor]);
			else homeView->openHomeOptions();
			return false;
		}
		if (!ui.inMenu && browseDir == RECENT && b == Button::Y)
		{
			if (ui.cursor >= 0 && ui.cursor < (int)browseRows.size() && browseRows[ui.cursor].id == 2) homeView->openRecentMenu(browsePaths[ui.cursor]);
			return false;
		}
		// All recent with nothing in it: its empty state's key (Browse Files)
		if (!ui.inMenu && browseDir == RECENT && b == Button::A && (browseRows.empty() || (browseRows.size() == 1 && browseRows[0].id < 0)))
		{
			openSection(TAB_LOCAL, p);
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
		// (its downs come late: after the header's "<" and the empty page's button; the rest where the readers' do)
		if (pt.kind != Pointer::Down)
		{
			// the Extensions drawer: a tile acts as the finger lifts (as Home's do)
			if (pt.kind == Pointer::Up && !ui.inMenu && browseDir == EXTDIR) return ui.manga->extPointer(pt, x, y, p);
			return false;
		}
		// the drawer: its own taps
		if (!ui.inMenu && browseDir == EXTDIR) return ui.manga->extPointer(pt, x, y, p);
		// the Files page: its rows, header keys, select bar and empty page's keys
		if (!ui.inMenu && filesPage()) return filesPointer(pt, x, y, p);
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
		// All recent: its rows are Home's Rects (48 tall from body.y + 30, clear of the mini bar); the list's lift and
		// drag then act on the cursor it sets
		if (!ui.inMenu && browseDir == RECENT) return homeView->recentPointer(pt, x, y, p);
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
		// a hold on a file of the cards started the old Add-files select mode (Ui::draw): the redesigned Files
		// page's own select mode is the same thing now, with its bar, so it takes that tick over
		if (!ui.inMenu && !fileSel && playlists->selMode == SEL_ADD && playlists->selDest.empty() && browseTab == TAB_LOCAL &&
			!browseDir.empty() && !pseudoDir(browseDir) && !isNetPath(browseDir))
		{
			fileSel = true;
			fileSelPaths = playlists->picked;
			fileSelOrder = playlists->pickOrder;
			playlists->selEnd();
			playlists->selMode = SEL_PICK;	// (miniBarShown hides the bar while selecting)
		}
		// Files has its own page (the redesign); Home, Playlists, Network and All recent keep Ui's list
		if (filesPage()) { drawFiles(p); return; }
		ui.drawTracks(p);
	}

	std::string Ui::Browse::signature(const Player& p) const
	{
		// the section and its listing; what Copy / Move holds (the bar along the foot); Home's focus and strip
		std::string s = "|tab" + std::to_string(browseTab) + (netLoading ? "L" : "");
		if (filesPage())
		{
			// Files' own: select mode and its ticks, the finger, the read-only mark, the footer's count and room,
			// and a group Copy / Move held
			s += "|files" + std::to_string(fileSel) + ":" + std::to_string(filesPressed) + ":" + std::to_string(filesTouch) +
				(filesReadOnly() ? "ro" : "") + "|" + filesFooter() + "|" + filesTitle();
			for (auto& path : fileSelPaths) s += "|" + path;
			for (auto& path : clipPaths) s += "|c" + path;
		}
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
		// Home's and All recent's own new state (the kind filter, what a finger is on, the Continue card's words)
		if (atHome() || browseDir == RECENT) s += homeView->sig(p);
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

	bool Ui::backIconShown(const Player&) const
	{
		// Home has none; the redesigned Files page draws its own (frame), so the old one is not drawn nor hit
		return !(page == Page::Browse && !inMenu && (browse->atHome() || browse->filesPage()));
	}
}
