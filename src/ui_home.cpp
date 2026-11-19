// Home, and All recent: redesigned on the kit (ui_home.h says what is Home's and what stayed Ui's; docs/redesign:
// home, home-first, home-stopped, home-more, recent-all, card-out). A Continue card, the Recent row with its kind
// filters, three shortcuts; no section tiles (the rail has the sections). All recent is the same filters over every
// card in the history.
#include "ui.h"
#include "session_host.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include "theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
#include "ui_kit.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"
#include "ui_sound.h"
#include "ui_docs.h"
#include "ui_reader.h"
#include "ui_manga.h"
#include "pagebook.h"
#include <strings.h>

namespace bp
{
	namespace
	{
		const char* const FILTER_NAMES[4] = {"All", "Videos", "Music", "Books"};
		bool isDocument(const std::string& path) { return isDocumentName(path) || isComicName(path); }

		// a file's title without its extension, as a card's
		std::string cardTitle(const std::string& path)
		{
			std::string n = baseName(path);
			size_t dot = n.rfind('.');
			if (dot != std::string::npos && dot > 0) n.erase(dot);
			return n;
		}
	}

	// ---- listing Recent ----

	void Ui::Home::listRecent(Player& p)
	{
		recent.clear();
		for (int& c : recentCounts) c = 0;
		if (!p.settings.keepHistory) return;
		for (auto& path : p.history().items())
		{
			// a file still there (on a card taken out: not), or a network one whose connection is still saved (whether
			// the file is there is only known by asking its server: when it's opened)
			bool net = isNetPath(path);
			NetPath np;
			const NetServer* sv = net && ui.netStore && parseNetPath(path, np) ? ui.netStore->find(np.server) : nullptr;
			if (net ? !sv : !exists(path) || (!p.settings.showHiddenFolders && insideHiddenFolder(path))) continue;
			RecentItem it;
			it.path = path;
			it.net = net;
			it.name = cardTitle(path);
			it.kind = isAudioFile(path) ? &G_NOTE : isPhotoFile(path) ? &G_PICTURE : &G_FILM;
			if (isDocument(path))
			{
				// a document: the page it's at (a PDF, a comic) or how far through (a TXT, a book), from its own places
				bool epub = path.size() > 5 && strcasecmp(path.c_str() + path.size() - 5, ".epub") == 0;
				it.kind = isComicName(path) ? &G_COMIC : epub ? &G_EPUB : isPdfName(path) ? &G_PDF : &G_PAGE;
				const ResumeEntry* d = ui.docs->placeOf(path);
				bool pages = d && (d->aid == 2 || (d->aid != 1 && d->duration > 1));
				if (d && d->position > 0)
				{
					it.at = pages ? "page " + std::to_string((int)d->position + 1) : std::to_string((int)std::lround(d->duration * 100)) + "%";
					it.sub = pages ? "Page " + std::to_string((int)d->position + 1) + " / " + std::to_string((int)d->duration) : it.at + " read";
					it.frac = pages ? (d->position + 1) / d->duration : d->duration;
				}
				else
				{
					it.sub = pages ? std::to_string((int)d->duration) + " pages" : "Not started";
					it.frac = 0;
				}
				std::string w = ui.whereOf(parentFolder(path));
				it.where = w.substr(0, w.find(" / "));
				recent.push_back(it);
				recentCounts[0]++;
				recentCounts[PlayHistory::kindOf(path) + 1]++;
				continue;
			}
			// how far into it: where it was left (a song always starts at its start: played), of how long it is
			RecentProgress rp = recentProgress(p.resumeStore().find(path), isAudioFile(path));
			double len = rp.duration, pos = rp.position;
			if (!rp.played)
			{
				it.at = formatTime(pos);
				it.sub = len > 0 ? it.at + " / " + formatTime(len) : "at " + it.at;
				it.frac = rp.frac;
			}
			else
			{
				it.sub = len > 0 ? "Played \xC2\xB7 " + formatTime(len) : "Played";
				it.frac = 1;
			}
			// where it is: the server, or the place on the cards ("Videos", "This card")
			if (sv) it.where = serverName(*sv);
			else
			{
				std::string w = ui.whereOf(parentFolder(path));
				it.where = w.substr(0, w.find(" / "));
			}
			recent.push_back(it);
			recentCounts[0]++;
			recentCounts[PlayHistory::kindOf(path) + 1]++;
		}
		// a kind emptied while its filter was on: back to All rather than an empty page
		if (recentTab && !recentCounts[recentTab]) recentTab = 0;
	}

	const Ui::Home::RecentItem* Ui::Home::findRecent(const std::string& path) const
	{
		for (auto& it : recent)
			if (it.path == path) return &it;
		return nullptr;
	}

	bool Ui::Home::matchesFilter(int i) const
	{
		return i >= 0 && i < (int)recent.size() && (!recentTab || PlayHistory::kindOf(recent[i].path) + 1 == recentTab);
	}

	int Ui::Home::visibleCount() const
	{
		int n = 0;
		for (size_t i = 0; i < recent.size(); i++)
			if (matchesFilter((int)i)) n++;
		return n;
	}

	int Ui::Home::firstVisible() const
	{
		for (size_t i = 0; i < recent.size(); i++)
			if (matchesFilter((int)i)) return (int)i;
		return -1;
	}

	int Ui::Home::nextVisible(int from, int dir) const
	{
		for (int i = from + dir; i >= 0 && i < (int)recent.size(); i += dir)
			if (matchesFilter(i)) return i;
		return -1;
	}

	int Ui::Home::nthVisible(int n) const
	{
		int k = 0;
		for (size_t i = 0; i < recent.size(); i++)
			if (matchesFilter((int)i) && k++ == n) return (int)i;
		return -1;
	}

	std::string Ui::Home::tileSub(int g) const
	{
		// (Home no longer draws the section tiles; the browser's state line still asks for their lines)
		int key = (ui.browse->playlists->plStore ? ui.browse->playlists->plStore->serial() : -1) * 7919 + (ui.netStore ? ui.netStore->version() : -1) * 31 + (int)ui.roots.size() +
			(ui.mangaOn ? 1 : 0) * 1000003;
		if (key != tileSubsKey)
		{
			tileSubsKey = key;
			auto join = [](const std::vector<std::string>& parts) {
				std::string out;
				for (auto& s : parts) out += (out.empty() ? "" : " \xC2\xB7 ") + s;
				return out;
			};
			int cards = 0;
			for (auto& r : ui.roots)
				if (r.card) cards++;
			std::vector<std::string> parts;
			bool counted = false;
			for (auto& r : ui.roots)
			{
				bool card = r.card;
				if (!card || cards < 2) parts.push_back(r.label);
				else if (!counted)
				{
					parts.push_back(std::to_string(cards) + " cards");
					counted = true;
				}
			}
			tileSubs[TAB_LOCAL] = parts.empty() ? "No folders" : join(parts);
			size_t n = ui.browse->playlists->plStore ? ui.browse->playlists->plStore->addTargets().size() : 0;
			tileSubs[TAB_PLAYLISTS] = !ui.browse->playlists->plStore ? "Not available" : n == 0 ? "Liked"
				: "Liked \xC2\xB7 " + std::to_string(n) + (n == 1 ? " playlist" : " playlists");
			std::vector<NetServer> list = ui.netStore ? ui.netStore->servers() : std::vector<NetServer>();
			std::stable_sort(list.begin(), list.end(), [](const NetServer& a, const NetServer& b) { return a.lastUsed > b.lastUsed; });
			parts.clear();
			for (auto& sv : list) parts.push_back(serverName(sv));
			tileSubs[TAB_NETWORK] = parts.empty() ? "No connections yet" : join(parts);
		}
		return g >= 0 && g < BROWSE_TABS ? tileSubs[g] : "";
	}

	void Ui::Home::setRecentFilter(int tab)
	{
		if (tab < 0 || tab > 3 || (tab && !recentCounts[tab])) return;
		recentTab = tab;
		// the cursor moves onto a card the filter leaves (the chevron if there are more than the row shows)
		if (homeZone == HOME_CARDS)
		{
			int at = firstVisible();
			homeStrip = at >= 0 ? at : (int)recent.size();
		}
	}

	bool Ui::Home::drawRecentTabs(int x, int mid, int right, bool counts)
	{
		// (the browser's old in-header tabs: kept for its state line; All recent draws the kit's own strip)
		int ws[4], total = 0;
		for (int i = 0; i < 4; i++)
		{
			ws[i] = (int)std::lround(ui.textWidth(counts ? std::string(FILTER_NAMES[i]) + " " + std::to_string(recentCounts[i]) : std::string(FILTER_NAMES[i]), F_P13) / sx) + 20;
			total += ws[i] + (i ? 6 : 0);
		}
		if (x + total > right) return false;
		for (int i = 0; i < 4; i++)
		{
			std::string label = counts ? std::string(FILTER_NAMES[i]) + " " + std::to_string(recentCounts[i]) : std::string(FILTER_NAMES[i]);
			Rect r{x, mid - 14, ws[i], 28};
			recentTabRect[i] = r;
			bool on = recentTab == i;
			if (on) ui.fillRound(r, ACCENT, 8);
			else ui.fillRound(r, TRACK, 8);
			ui.drawTextMid(label, F_P13, on ? ON_ACCENT : recentCounts[i] || !i ? TEXT : DIM, r.x + 10, mid);
			x += ws[i] + 6;
		}
		return true;
	}

	void Ui::Home::stepRecentTab(int dir, Player& p)
	{
		for (int k = 0; k < 4; k++)
		{
			recentTab = (recentTab + dir + 4) % 4;
			if (!recentTab || recentCounts[recentTab]) break;
		}
		ui.browse->enterFolder(RECENT, p);
		ui.cursor = ui.browse->firstCursor();
		ui.scroll = 0;
		ui.browse->fitList();
	}

	void Ui::Home::recentInOrder(const std::vector<std::string>& before)
	{
		if (before.empty()) return;
		std::unordered_map<std::string, size_t> at;
		for (size_t i = 0; i < before.size(); i++) at.emplace(before[i], i);
		std::vector<RecentItem> kept;
		for (auto& it : recent)
			if (at.count(it.path)) kept.push_back(it);
		std::stable_sort(kept.begin(), kept.end(), [&](const RecentItem& a, const RecentItem& b) { return at[a.path] < at[b.path]; });
		recent = std::move(kept);
	}

	void Ui::Home::recentEdited(Player& p)
	{
		if (ui.browse->browseDir != HOMEDIR && ui.browse->browseDir != RECENT) return;
		recentKeepOrder = true;
		int c = ui.cursor, s = ui.scroll;
		ui.browse->enterFolder(ui.browse->browseDir, p);
		if (ui.browse->browseDir == RECENT)
		{
			ui.cursor = c;
			ui.scroll = s;
			ui.browse->fitList();
		}
	}

	void Ui::Home::setStrip(int i, bool)
	{
		homeStrip = recent.empty() ? 0 : std::clamp(i, 0, (int)recent.size());
		stripOff = 0;
		stripT0 = 0;
	}

	void Ui::Home::stripStep()
	{
		// the row no longer scrolls sideways (All recent is every card): the eased offset stays at zero
		if (stripOff != 0) stripOff = 0;
		// no finger down (a hold took the card's More sheet, or a lift was taken elsewhere): no pressed piece stays lit
		if (!homeTouch && !homeHoldAt)
		{
			resumeHoldAt = 0;
			pressedContinue = pressedResume = false;
			pressedCard = pressedShortcut = pressedChip = pressedEmptyKey = -1;
		}
	}

	// ---- the Continue card ----

	Ui::Home::Continue Ui::Home::continueOf(const Player& p) const
	{
		Continue c;
		// a video Manga stopped: remembered from the moment the reader took the screen (the card says why until it
		// plays again, or something else is played)
		if (p.playing() && !p.playingPhoto()) mangaStopped.clear();
		if (ui.manga && ui.manga->videoBlockedAt && ui.manga->videoBlockedAt != mangaStopSeen)
		{
			mangaStopSeen = ui.manga->videoBlockedAt;
			mangaStopped.clear();
			for (auto& it : recent)
				if (it.kind == &G_FILM) { mangaStopped = it.path; break; }
			if (mangaStopped.empty())
				for (auto& e : p.resumeStore().all())
					if (isVideoFile(e.path)) { mangaStopped = e.path; break; }
		}
		if (p.playing() && !p.playingPhoto())
		{
			c.shown = c.live = true;
			c.path = p.currentPath();
			c.label = "Now playing";
			// a page the output: the session's wish is the state shown (the player's pause follows it)
			c.paused = ui.predicting(p) ? !ui.pagePlaying() : p.paused;
			c.key = c.paused ? "Play" : "Pause";
			if (const RecentItem* it = findRecent(c.path))
			{
				// the tag's title when mpv has one (a song's "Song 2", not its file's "song2"), as the mini bar and
				// the player screen say it; the card's own name is the fallback
				c.title = p.playingAudio() && !p.title.empty() ? p.title : it->name;
				c.kind = it->kind;
				c.frac = it->frac;
				c.artKey = it->name;
			}
			else
			{
				c.title = !p.title.empty() ? p.title : cardTitle(c.path);
				c.kind = p.playingAudio() ? &G_NOTE : &G_FILM;
				c.artKey = c.title;
			}
			// the note is the time, then the mini bar's state pills (the card is Home's one place for what plays)
			double pos = std::max(0.0, ui.shownPosition(p));
			c.note = formatTime(pos);
			if (p.duration > 0)
			{
				c.note += " / " + formatTime(p.duration);
				c.frac = std::clamp(pos / p.duration, 0.0, 1.0);
			}
			c.pills = ui.kit->statePills(p);
			return c;
		}
		auto fromResume = [&](const ResumeEntry& e) {
			c.shown = true;
			c.path = e.path;
			c.title = cardTitle(e.path);
			c.kind = &G_FILM;
			c.artKey = c.title;
			c.frac = std::clamp(e.position / e.duration, 0.0, 1.0);
			c.note = "Stopped at " + formatTime(e.position) + " / " + formatTime(e.duration);
			std::string w = ui.whereOf(parentFolder(e.path));
			w = w.substr(0, w.find(" / "));
			if (!w.empty()) c.note += " \xC2\xB7 " + w;
			c.stopped = !mangaStopped.empty() && mangaStopped == e.path;
			c.label = c.stopped ? "Stopped for Manga" : "Continue";
			c.key = "Resume";
			return c;
		};
		// what was played last, if it was left in the middle: this is the one to carry on, even when its card has been
		// taken out and its file has gone from Recent (the place is kept in resume.tsv)
		const std::vector<ResumeEntry>& all = p.resumeStore().all();
		if (!all.empty())
		{
			const ResumeEntry& e = all.front();
			// the last thing played, or one whose file has gone (a card out): not an older video after a book was read
			bool last = recent.empty() || recent.front().path == e.path || !findRecent(e.path);
			if (last && isVideoFile(e.path) && e.position > 0 && e.duration > 0 && worthResuming(e.position, e.duration)) return fromResume(e);
		}
		// else the last thing played, whatever it is (the owner, 2026-11-19: a half-read book further down must not
		// stand in for the songs played since): Resume when it has a place to carry on from, else Play
		for (auto& it : recent)
		{
			c.shown = true;
			c.path = it.path;
			c.title = it.name;
			c.kind = it.kind;
			c.note = it.sub + (it.where.empty() ? "" : " \xC2\xB7 " + it.where);
			c.frac = it.frac;
			c.artKey = it.name;
			c.stopped = !mangaStopped.empty() && mangaStopped == it.path;
			bool part = it.frac > 0.0 && it.frac < 1.0 - 1e-6;
			c.label = c.stopped ? "Stopped for Manga" : part ? "Continue" : "Last played";
			c.key = part ? "Resume" : "Play";
			if (!part) c.frac = 0;	// (a played-through one has no place to show)
			return c;
		}
		// a card taken out with something older still in resume.tsv: its place is kept too
		for (auto& e : all)
		{
			if (!isVideoFile(e.path) || e.position <= 0 || e.duration <= 0) continue;
			if (!worthResuming(e.position, e.duration) || findRecent(e.path)) continue;
			return fromResume(e);
		}
		return c;
	}

	void Ui::Home::openContinue(const Continue& c, Player& p)
	{
		if (!c.shown || c.path.empty()) return;
		// what plays: the card is its way back to the player; nothing loaded: the place it was left at
		if (c.live) { ui.showPlayer(); return; }
		ui.openFile(c.path, p);
	}

	void Ui::Home::openCard(int i, Player& p)
	{
		if (i < 0 || i >= (int)recent.size())
		{
			ui.browse->enterFolder(RECENT, p);
			return;
		}
		ui.openFile(recent[i].path, p);
	}

	void Ui::Home::openShortcut(int i, Player& p)
	{
		bool liked = ui.browse->playlists->plStore && ui.browse->playlists->plStore->likedCount() > 0;
		if (!liked)
		{
			// nothing liked: the first shortcut is Browse Files until a heart fills the Liked list
			if (i == 0) { ui.browse->openSection(TAB_LOCAL, p); return; }
			if (i == 1) { ui.openWeb(); return; }
			ui.browse->enterFolder(RECENT, p);
			return;
		}
		if (i == 0)
		{
			// Shuffle Liked: the built-in list, shuffled, as its own page's Shuffle play does
			PlaylistStore* store = ui.browse->playlists->plStore.get();
			Playlist pl;
			int skipped = 0;
			if (store && store->load(store->likedFile(), pl, skipped) && !pl.entries.empty())
				ui.browse->playlists->plPlay(pl, 0, true, p);
			else p.osd("Nothing liked yet");
			return;
		}
		if (i == 1) { ui.openWeb(); return; }
		ui.browse->enterFolder(RECENT, p);
	}

	void Ui::Home::goHome(int zone, int idx, Player& p)
	{
		ui.browse->enterFolder(HOMEDIR, p);
		homeGrid = homeCol = 0;
		homeShortcut = 0;
		stripOff = 0;
		if (recent.empty())
		{
			homeZone = HOME_EMPTY;
			homeStrip = 0;
			return;
		}
		Continue c = continueOf(p);
		if (zone == HOME_CARDS)
		{
			// back from All recent: on the card it was opened from (or the chevron)
			homeStrip = std::clamp(idx, 0, (int)recent.size());
			if (homeStrip >= (int)recent.size() && !moreShown()) homeStrip = std::max(0, firstVisible());
			if (homeStrip < (int)recent.size() && !matchesFilter(homeStrip)) homeStrip = std::max(0, firstVisible());
			homeZone = HOME_CARDS;
		}
		else homeZone = c.shown ? HOME_CONTINUE : HOME_CARDS;
	}

	void Ui::Home::homeFocusFromPointer()
	{
		// (touch never shows the focus; it follows what was touched)
		if (homeZone == HOME_CARDS && !matchesFilter(homeStrip)) homeStrip = std::max(0, firstVisible());
	}

	void Ui::Home::homeOpen(Player& p)
	{
		if (homeZone == HOME_CONTINUE) { openContinue(continueOf(p), p); return; }
		if (homeZone == HOME_SHORTCUTS) { openShortcut(homeShortcut, p); return; }
		if (homeZone == HOME_EMPTY) { ui.browse->openSection(TAB_LOCAL, p); return; }
		openCard(homeStrip, p);
	}

	bool Ui::Home::homeKey(Button b, Player& p)
	{
		if (quitOpen)
		{
			// its own question: UP/DOWN the rows, A chooses, B (or START, as the Sound page's dialogs) backs out
			switch (b)
			{
			case Button::Up: case Button::Down: quitCursor = (quitCursor + 1) % 2; break;
			case Button::A:
				if (quitCursor == 0) ui.quitRequested = true;
				quitOpen = false;
				break;
			case Button::B: case Button::Start: quitOpen = false; break;
			default: break;
			}
			return false;
		}
		if (b == Button::B)
		{
			// it asks first, playing or not (the dialog says what quitting stops); the cursor on Cancel
			quitOpen = true;
			quitCursor = 1;
			return false;
		}
		// (START is Settings, from Ui::press, on every screen)
		if (b == Button::X)
		{
			// the focused card's More sheet; with no card, Home's own
			if ((homeZone == HOME_CARDS || homeZone == HOME_CONTINUE) && !recent.empty())
			{
				Continue c = continueOf(p);
				if (homeZone == HOME_CONTINUE && c.shown && findRecent(c.path)) openRecentMenu(c.path);
				else if (homeStrip < (int)recent.size()) openRecentMenu(recent[homeStrip].path);
				else openHomeOptions();
			}
			else openHomeOptions();
			return false;
		}
		if (b == Button::Y)
		{
			if (homeZone == HOME_EMPTY) { ui.openWeb(); return false; }
			Continue c = continueOf(p);
			if (homeZone == HOME_CONTINUE && c.shown && findRecent(c.path)) openRecentMenu(c.path);
			else if ((homeZone == HOME_CARDS) && homeStrip < (int)recent.size()) openRecentMenu(recent[homeStrip].path);
			else openHomeOptions();
			return false;
		}
		if (b == Button::L2 || b == Button::R2)
		{
			// (a step of the kind; the shared L2/R2 handler sends these here once the shell owns the rail)
			setRecentFilter((recentTab + (b == Button::R2 ? 1 : 3) + 4) % 4);
			return false;
		}
		if (homeZone == HOME_EMPTY)
		{
			if (b == Button::A) ui.browse->openSection(TAB_LOCAL, p);
			return false;
		}
		Continue c = continueOf(p);
		int vis = visibleCount();
		if (homeZone == HOME_CONTINUE)
		{
			if (b == Button::A) { openContinue(c, p); return false; }
			if (b == Button::Down)
			{
				if (vis > 0) { homeZone = HOME_CARDS; homeStrip = std::max(0, firstVisible()); }
				else if (vis >= 0) homeZone = HOME_SHORTCUTS;
			}
			return false;
		}
		if (homeZone == HOME_CARDS)
		{
			bool more = moreShown();
			int shown = std::min(vis, HOME_RECENT_MAX);
			switch (b)
			{
			case Button::Left:
				if (homeStrip >= (int)recent.size() && shown > 0) homeStrip = nthVisible(shown - 1);
				else
				{
					for (int k = shown - 1; k >= 0; k--)
						if (nthVisible(k) < homeStrip) { homeStrip = nthVisible(k); break; }
				}
				break;
			case Button::Right:
				if (homeStrip >= (int)recent.size()) break;
				if (more)
				{
					int rank = -1;
					for (int k = 0; k < shown; k++) if (nthVisible(k) == homeStrip) rank = k;
					if (rank + 1 >= shown) homeStrip = (int)recent.size();
					else homeStrip = nthVisible(rank + 1);
				}
				break;
			case Button::Up: if (c.shown) homeZone = HOME_CONTINUE; break;
			case Button::Down: if (vis > 0) { homeZone = HOME_SHORTCUTS; homeShortcut = 0; } break;
			case Button::A: openCard(homeStrip, p); break;
			default: break;
			}
			return false;
		}
		// the shortcuts
		switch (b)
		{
		case Button::Left: homeShortcut = std::max(0, homeShortcut - 1); break;
		case Button::Right: homeShortcut = std::min(2, homeShortcut + 1); break;
		case Button::Up: homeZone = c.shown ? HOME_CONTINUE : HOME_CARDS; if (!c.shown) homeStrip = std::max(0, firstVisible()); break;
		case Button::A: openShortcut(homeShortcut, p); break;
		default: break;
		}
		return false;
	}

	bool Ui::Home::homePointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (quitOpen)
		{
			// its rows act as the finger lifts; anywhere else cancels as it lands (its lift presses nothing)
			if (pt.kind == Pointer::Down)
			{
				ui.pressedBtn = -1;
				if (!quitBox().hit(x, y))
				{
					quitOpen = false;
					ui.swallowTouch = true;
					return false;
				}
				for (int i = 0; i < 2; i++)
					if (quitRow(i).hit(x, y)) ui.pressedBtn = DLG_ROW + i;
			}
			else if (pt.kind == Pointer::Up && ui.pressedBtn >= DLG_ROW)
			{
				int i = ui.pressedBtn - DLG_ROW;
				ui.pressedBtn = -1;
				if (i < 2 && quitRow(i).hit(x, y))
				{
					quitCursor = i;
					if (i == 0) ui.quitRequested = true;
					quitOpen = false;
				}
			}
			return false;
		}
		lay::Shell sh = lay::shell();
		Continue c = continueOf(p);
		int vis = visibleCount();
		bool has = !recent.empty();
		std::vector<Kit::FilterItem> items;
		for (int i = 0; i < 4; i++) items.push_back({FILTER_NAMES[i], recentCounts[i]});
		lay::HomePage hp = lay::homePage(sh.body, c.shown, has, std::min(vis, HOME_RECENT_MAX), moreShown(), ui.kit->filterWidths(items));
		lay::EmptyState es = lay::emptyState(sh.body, (int)wrapEmpty(emptyText(p)).size(), 2);
		if (pt.kind == Pointer::Down)
		{
			homeTouch = true;
			homeMoved = false;
			homeDownX = x;
			homeDownOff = 0;
			homePress = -1;
			homeHoldAt = 0;
			resumeHoldAt = 0;
			pressedContinue = pressedResume = false;
			pressedCard = pressedShortcut = pressedChip = pressedEmptyKey = -1;
			if (c.shown && hp.cont.hit(x, y))
			{
				homeZone = HOME_CONTINUE;
				pressedContinue = true;
				pressedResume = lay::continueCard(hp.cont).key.hit(x, y);
				// the live card's key held takes over from a page that plays the file (as the mini bar's mark does);
				// otherwise a hold anywhere on the card, the key included, opens its More
				resumeHoldAt = pressedResume && c.live ? SDL_GetTicks() : 0;
				if (!(pressedResume && c.live && ui.pageDrives(ui.barPlayer(p))))
				{
					homePress = HOME_PRESS_CONTINUE;
					homeHoldAt = SDL_GetTicks();
				}
				else
				{
					ui.playHoldAt = SDL_GetTicks();
					ui.playHoldMini = true;
					ui.playHoldTouch = false;
				}
				return false;
			}
			if (has && hp.chipStrip.hit(x, y))
			{
				int k = lay::filterAt(hp.chips, x, y);
				if (k >= 0) { pressedChip = k; return false; }
			}
			for (int i = 0; i < hp.cards; i++)
				if (hp.card[i].hit(x, y))
				{
					int idx = nthVisible(i);
					if (idx < 0) break;
					homeZone = HOME_CARDS;
					homeStrip = idx;
					homePress = idx;
					pressedCard = idx;
					homeHoldAt = SDL_GetTicks();
					return false;
				}
			if (moreShown() && hp.more.hit(x, y))
			{
				homeZone = HOME_CARDS;
				homeStrip = (int)recent.size();
				homePress = homeStrip;
				pressedCard = homeStrip;
				return false;
			}
			for (int i = 0; i < hp.shortcuts; i++)
				if (hp.shortcut[i].hit(x, y))
				{
					homeZone = HOME_SHORTCUTS;
					homeShortcut = i;
					pressedShortcut = i;
					return false;
				}
			if (!has && !c.shown)
			{
				homeZone = HOME_EMPTY;
				for (int i = 0; i < es.keys; i++)
					if (es.key[i].hit(x, y)) pressedEmptyKey = i;
				return false;
			}
			// a finger on nothing: no drag, no tap
			homeTouch = false;
			return false;
		}
		if (!homeTouch) return false;
		if (pt.kind == Pointer::Move)
		{
			if (!homeMoved && std::abs(x - homeDownX) > DRAG_SLOP)
			{
				homeMoved = true;
				homeHoldAt = 0;
			}
			return false;
		}
		if (pt.kind != Pointer::Up) return false;
		homeTouch = false;
		homeHoldAt = 0;
		homePress = -1;
		bool moved = homeMoved;
		homeMoved = false;
		if (moved)
		{
			resumeHoldAt = 0;
			ui.playHoldAt = 0;
			ui.playHoldMini = false;
			pressedContinue = pressedResume = false;
			pressedCard = pressedShortcut = pressedChip = pressedEmptyKey = -1;
			return false;
		}
		if (pressedResume && hp.cont.hit(x, y))
		{
			bool key = lay::continueCard(hp.cont).key.hit(x, y);
			Uint32 at = resumeHoldAt;
			resumeHoldAt = 0;
			// the hold fired in tick() at the threshold (a browser's file is the handheld's now): its lift does nothing
			bool held = c.live && at && SDL_GetTicks() - at >= PLAY_HOLD_MS;
			ui.playHoldAt = 0;
			ui.playHoldMini = false;
			if (key && c.live && held)
			{
				// the hold fired in tick() already (a page's file is the handheld's now); a lift in the same frame it
				// came due takes over here instead
				Player& bp = ui.barPlayer(p);
				if (ui.pageDrives(bp)) ui.takeOver(bp);
			}
			else if (key && c.live) ui.miniToggle(p);
			else if (key && !c.live) openContinue(c, p);
			else if (!key) openContinue(c, p);	// the rest of the card: the player
		}
		else if (pressedContinue && hp.cont.hit(x, y)) { openContinue(c, p); }
		else if (pressedChip >= 0 && lay::filterAt(hp.chips, x, y) == pressedChip) setRecentFilter(pressedChip);
		else if (pressedCard >= 0)
		{
			if (pressedCard >= (int)recent.size()) { if (hp.more.hit(x, y)) ui.browse->enterFolder(RECENT, p); }
			else if (pressedCard < (int)recent.size())
			{
				// still on the same card (a touch that slid to another presses nothing)
				bool on = false;
				for (int i = 0; i < hp.cards; i++)
					if (nthVisible(i) == pressedCard && hp.card[i].hit(x, y)) on = true;
				if (on) openCard(pressedCard, p);
			}
		}
		else if (pressedShortcut >= 0 && pressedShortcut < hp.shortcuts && hp.shortcut[pressedShortcut].hit(x, y))
		{
			homeShortcut = pressedShortcut;
			openShortcut(pressedShortcut, p);
		}
		else if (pressedEmptyKey >= 0 && pressedEmptyKey < es.keys && es.key[pressedEmptyKey].hit(x, y))
		{
			if (pressedEmptyKey == 0) ui.browse->openSection(TAB_LOCAL, p);
			else ui.openWeb();
		}
		pressedContinue = pressedResume = false;
		pressedCard = pressedShortcut = pressedChip = pressedEmptyKey = -1;
		return false;
	}

	bool Ui::Home::recentPointer(const Pointer& pt, int x, int y, Player& p)
	{
		// All recent's rows: the generic list's pointers take over from here (its Up chooses the cursor this sets)
		if (pt.kind != Pointer::Down) return false;
		lay::Shell sh = lay::shell();
		lay::AllRecentPage lp = lay::allRecentPage(sh.body);
		// its empty state's one key (Browse Files), as the key says
		if (ui.browse->browseRows.empty() || (ui.browse->browseRows.size() == 1 && ui.browse->browseRows[0].id < 0))
		{
			lay::EmptyState es = lay::emptyState(sh.body, (int)wrapEmpty(recentEmptyText(p)).size(), 1);
			if (es.key[0].hit(x, y)) ui.browse->openSection(TAB_LOCAL, p);
			return false;
		}
		for (int i = 0; i < lp.shown; i++)
		{
			int idx = ui.scroll + i;
			if (!lp.row(i).hit(x, y)) continue;
			if (idx < 0 || idx >= (int)ui.browse->browseRows.size() || ui.browse->browseRows[idx].id < 0) return false;
			ui.cursor = idx;
			ui.listTouch = true;
			ui.listScrolled = false;
			ui.touchStartY = y;
			ui.scrollStart = ui.scroll;
			return false;
		}
		return false;
	}

	// ---- the frame (the shell step replaces this one function) ----

	std::string Ui::Home::title() const
	{
		return ui.browse->browseDir == RECENT ? "All recent" : "Home";
	}

	std::string Ui::Home::crumb(const Player&) const
	{
		if (ui.browse->browseDir != RECENT) return "";
		size_t n = recent.size();
		return "Home  \xE2\x80\xBA  All recent \xC2\xB7 " + std::to_string(n) + (n == 1 ? " item" : " items");
	}

	std::vector<Ui::Hint> Ui::Home::hintList(const Player& p) const
	{
		// the quit question's own keys: it has the panel until it closes
		if (quitOpen) return {{"UP/DOWN", "Choose"}, {"A", "OK"}, {"B", "Cancel"}};
		std::vector<Hint> h;
		if (ui.browse->browseDir == RECENT)
		{
			bool empty = ui.browse->browseRows.empty() || (ui.browse->browseRows.size() == 1 && ui.browse->browseRows[0].id < 0);
			h = empty ? std::vector<Hint>{{"A", "Browse Files"}, {"B", "Home"}, {"L/R", "Sections"}}
				: std::vector<Hint>{{"A", "Resume"}, {"X", "More"}, {"B", "Home"}, {"L/R", "Sections"}};
			return h;
		}
		if (recent.empty() && !continueOf(p).shown)
		{
			h = {{"A", "Browse Files"}, {"Y", "Web dashboard"}, {"B", "Quit"}, {"L/R", "Sections"}};
			return h;
		}
		Continue c = continueOf(p);
		// A's word follows what has the focus: the card goes back to what plays, a card that is what plays too (A
		// there is openFile's way back), any other plays a file
		bool onPlaying = homeZone == HOME_CARDS && homeStrip >= 0 && homeStrip < (int)recent.size() && p.playing() &&
			!p.playingPhoto() && recent[homeStrip].path == p.currentPath();
		if (homeZone == HOME_CONTINUE && c.shown) h.push_back({"A", c.live ? "Go to it" : c.key});
		else if (homeZone == HOME_SHORTCUTS) h.push_back({"A", "Open"});
		else h.push_back({"A", onPlaying ? "Go to it" : "Play"});
		// SELECT's way back to what plays is named here (the rail's Now playing key says it too)
		if (c.live) h.push_back({"SELECT", "Now playing"});
		h.push_back({"X", "More"});
		h.push_back({"B", "Quit"});
		h.push_back({"L/R", "Sections"});
		return h;
	}

	std::vector<std::string> Ui::Home::emptyText(const Player& p) const
	{
		return p.settings.keepHistory
			? std::vector<std::string>{"What you play shows up here, so you can carry on.", "Start with the files on your card, or send some from a phone."}
			: std::vector<std::string>{"What you play isn't kept, so there is nothing here to carry on from.", "Turn the history on in Settings \xE2\x80\xBA Files & history."};
	}

	std::vector<std::string> Ui::Home::recentEmptyText(const Player& p) const
	{
		return p.settings.keepHistory ? std::vector<std::string>{"What you play shows up here, so you can carry on."}
			: std::vector<std::string>{"Play history is off (Settings \xE2\x80\xBA Files & history)."};
	}

	std::vector<std::string> Ui::Home::wrapEmpty(const std::vector<std::string>& text) const
	{
		// the empty state's lines are 400 wide at Inter 14; wrapping here lets lay::emptyState place the keys under
		// however many lines the sentences take (the kit draws one line per string)
		std::vector<std::string> out;
		for (const auto& s : text)
			for (auto& line : ui.wrapText(s, F_I14, 400, 99)) out.push_back(line);
		return out;
	}

	std::string Ui::Home::sig(const Player& p) const
	{
		Continue c = continueOf(p);
		char b[320];
		snprintf(b, sizeof b, "|hm%d %d %d %d %d %d %d %d %d %d %d %zu %d", homeZone, homeStrip, homeShortcut, homePress, recentTab,
			(int)pressedContinue, (int)pressedResume, pressedCard, pressedShortcut, pressedChip, pressedEmptyKey, recent.size(),
			clearArmedAt && SDL_GetTicks() < clearArmedAt ? 1 : 0);
		std::string s = b;
		s += "|ha" + std::to_string(artSerial);	// a song's art arrived: the cards draw it now
		if (c.shown)
		{
			s += "|hc" + c.label + c.path + std::to_string((int)std::lround(c.frac * 100)) + c.key + (c.paused ? "p" : "");
			for (auto& pill : c.pills) s += "|hp" + pill;	// a speed, Repeat or EQ changed: the note line draws anew
		}
		if (!morePath.empty()) s += "|mp" + morePath;
		return s;
	}

	// ---- a song's real art ----

	void Ui::Home::drawSongArt(const std::string& path, const std::string& key, Rect r, int rad)
	{
		// the placeholder is the ground either way: its edge and rounded corners stay whole under the picture, and a
		// song without art (or before its decode) is exactly what Home drew before
		Kit& k = *ui.kit;
		k.artPlaceholder(r, key, rad, true);
		if (path.empty() || artFailed.count(path)) return;
		// inside the placeholder's edge, cut to the same rounded shape: the edge is not covered
		Rect in{r.x + theme::EDGE, r.y + theme::EDGE, r.w - 2 * theme::EDGE, r.h - 2 * theme::EDGE};
		SDL_Rect d = ui.devRect(in);
		std::string artKey = std::string(HOME_COVER_KEY) + path + "|" + std::to_string(d.w) + "x" + std::to_string(d.h) +
			"r" + std::to_string(rad);
		auto it = artTex.find(artKey);
		if (it != artTex.end())
		{
			it->second.drawnAt = ++artDrawn;
			SDL_RenderCopy(ui.ren, it->second.tex, nullptr, &d);
			return;
		}
		if (artAsked.count(artKey)) return;
		CoverWorker* worker = ui.manga ? ui.manga->coverWorker : nullptr;
		if (!worker) return;
		artPath[artKey] = path;
		artRad[artKey] = rad;
		worker->wantSong(artKey, path, d.w, d.h);
		artAsked.insert(artKey);
	}

	void Ui::Home::coversReady(std::vector<CoverWorker::Result> results)
	{
		for (auto& r : results)
		{
			artAsked.erase(r.key);
			auto pi = artPath.find(r.key);
			std::string path = pi == artPath.end() ? r.key : pi->second;
			auto ri = artRad.find(r.key);
			int rad = ri == artRad.end() ? 8 : ri->second;
			artPath.erase(r.key);
			artRad.erase(r.key);
			if (!r.ok)
			{
				// the placeholder stays for good: asking a file with no art again each frame helps no one
				fprintf(stderr, "home: no art for %s\n", path.c_str());
				artFailed.insert(path);
				continue;
			}
			roundCorners(r.rgba, r.w, r.h, ui.devRadius(std::max(0, rad - theme::EDGE)));
			SDL_Texture* t = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, r.w, r.h);
			if (!t)
			{
				artFailed.insert(path);
				continue;
			}
			SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
			SDL_UpdateTexture(t, nullptr, r.rgba.data(), r.w * 4);
			SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);	// (its corners are transparent)
			auto old = artTex.find(r.key);
			if (old != artTex.end() && old->second.tex) SDL_DestroyTexture(old->second.tex);
			artTex[r.key] = {t, ++artDrawn};
			artSerial++;
			fprintf(stderr, "home: art %dx%d for %s\n", r.w, r.h, path.c_str());
		}
		// the cap: the ones drawn longest ago go first (what is on screen was drawn this frame)
		while (artTex.size() > ART_KEEP)
		{
			auto oldest = artTex.end();
			for (auto i = artTex.begin(); i != artTex.end(); ++i)
				if (oldest == artTex.end() || i->second.drawnAt < oldest->second.drawnAt) oldest = i;
			if (oldest->second.tex) SDL_DestroyTexture(oldest->second.tex);
			artTex.erase(oldest);
		}
	}

	// ---- drawing ----

	void Ui::Home::drawHome(const Player& p)
	{
		Kit& k = *ui.kit;
		lay::Shell sh = lay::shell();
		Continue c = continueOf(p);
		bool has = !recent.empty();
		std::vector<Kit::FilterItem> items;
		for (int i = 0; i < 4; i++) items.push_back({FILTER_NAMES[i], recentCounts[i]});
		int vis = visibleCount(), shown = std::min(vis, HOME_RECENT_MAX);
		lay::HomePage hp = lay::homePage(sh.body, c.shown, has, shown, moreShown(), k.filterWidths(items));
		if (!has && !c.shown)
		{
			// home-first: nothing played yet (or the history off): what goes here and the two quickest ways to start
			Kit::Empty e;
			e.glyph = &G_HOME;
			e.title = p.settings.keepHistory ? "Nothing played yet" : "Play history is off";
			e.lines = wrapEmpty(emptyText(p));
			lay::EmptyState es = lay::emptyState(sh.body, (int)e.lines.size(), 2);
			e.key1 = "Browse Files";
			e.key2 = "Web dashboard";
			e.glyph1 = &G_CARD;
			e.glyph2 = &G_WEB;
			e.hotkeys = p.settings.buttonHotkeys;
			e.pressed1 = pressedEmptyKey == 0;
			e.pressed2 = pressedEmptyKey == 1;
			k.emptyState(es, e);
			if (quitOpen) drawQuitDialog(ui.nowPlayable(p));
			return;
		}
		if (c.shown)
		{
			lay::ContinueCard l = lay::continueCard(hp.cont);
			Kit::Card card;
			card.kind = c.kind;
			card.label = c.label;
			card.title = c.title;
			card.note = c.note;
			card.key = c.key;
			card.keyGlyph = c.live && !c.paused ? &G_PAUSE : &G_PLAY;
			card.progress = c.frac;
			card.focused = homeZone == HOME_CONTINUE;
			card.pressed = pressedContinue;
			card.keyPressed = pressedResume;
			card.picture = [this, &k, c](Rect r) {
				// a song's real art (its placeholder while it decodes, or forever when it has none); a video's and a
				// book's kind mark
				if (c.kind == &G_NOTE) drawSongArt(c.path, c.artKey.empty() ? c.title : c.artKey, r, 10);
				else
				{
					Kit::Card plain;
					plain.kind = c.kind;
					k.pictureOf(r, plain, 10);
				}
				if (c.live)
				{
					// the NOW pill over the picture's top left (the mock-up's Now playing card): a small tint in Clean,
					// a solid accent in Classic (components.md "Clean style", rule 6)
					int gw = ui.glyphW(G_PLAY, 1), tw = k.textW("NOW", F_I11B);
					Rect pr{r.x + 6, r.y + 7, 8 + gw + 4 + tw + 8, 17};
					bool clean = theme::drawingClean();
					SDL_Color pillInk = clean ? ACCENT : ON_ACCENT;
					ui.fillRound(pr, clean ? sdl(theme::tintOf(theme::BG)) : ACCENT, theme::R_CAP);
					ui.drawGlyphMid(G_PLAY, pr.x + 8 + gw / 2, pr.y + pr.h / 2, pillInk, 1);
					ui.drawTextMid("NOW", F_I11B, pillInk, pr.x + 8 + gw + 4, pr.y + pr.h / 2);
				}
				else
				{
					// the kind over the picture's top left, as the mock-up's Continue card has it
					const char* label = c.kind == &G_NOTE ? "MUSIC" : c.kind == &G_PICTURE ? "PHOTO"
						: c.kind == &G_EPUB || c.kind == &G_PDF || c.kind == &G_PAGE || c.kind == &G_COMIC ? "BOOK" : "VIDEO";
					int w = k.textW(label, F_I11B) + 12;
					k.pill(r.x + 6 + w, r.y + 16, label, Kit::Pill::Fact);
				}
			};
			k.continueCard(l, card);
			// the state pills after the time on the note line (their words are the mini bar's; nothing when normal)
			if (c.live && !c.pills.empty())
			{
				int cy = l.note.y + l.note.h / 2, right = l.note.x + l.note.w;
				int nw = c.note.empty() ? 0 : std::min(l.note.w, (int)k.textW(c.note, F_I12));
				int x = l.note.x + nw + (nw ? 8 : 0);
				for (auto& pill : c.pills)
				{
					int w = k.pillW(pill);
					if (x + w > right) break;	// the time and the ones that fit (the key's side is not covered)
					k.pill(x + w, cy, pill, Kit::Pill::Live);
					x += w + 5;
				}
			}
			// the marks the old strip carried: a liked file's heart, a network one's server
			int rx = l.pic.x + l.pic.w - 10;
			if (!c.path.empty() && isNetPath(c.path)) { ui.drawGlyphMid(G_NETWORK, rx, l.pic.y + l.pic.h - 11, DIM_ON); rx -= 16; }
			if (!c.path.empty() && ui.browse->playlists->plStore && ui.browse->playlists->plStore->isLiked(c.path))
				ui.drawGlyphMid(G_HEART_S, rx, l.pic.y + l.pic.h - 11, ACCENT);
		}
		if (hp.chipStrip.h > 0)
			k.filters(hp.chips, items, recentTab, -1, pressedChip);
		for (int i = 0; i < hp.cards; i++)
		{
			int idx = nthVisible(i);
			if (idx < 0) continue;
			const RecentItem& it = recent[idx];
			lay::RecentCard l = lay::recentCard(hp.card[i]);
			Kit::Card card;
			card.kind = it.kind;
			card.title = it.name;
			card.note = it.sub;
			card.progress = it.frac;
			card.focused = homeZone == HOME_CARDS && homeStrip == idx;
			card.pressed = pressedCard == idx;
			if (it.kind == &G_NOTE) card.picture = [this, path = it.path, key = it.name](Rect r) { drawSongArt(path, key, r, 8); };
			k.recentCard(l, card);
			// a liked one's heart and a network one's mark, over the picture's right corner (as the strip had them)
			int rx = l.pic.x + l.pic.w - 9;
			if (it.net) { ui.drawGlyphMid(G_NETWORK, rx, l.pic.y + l.pic.h - 10, DIM_ON); rx -= 15; }
			if (ui.browse->playlists->plStore && ui.browse->playlists->plStore->isLiked(it.path))
				ui.drawGlyphMid(G_HEART_S, rx, l.pic.y + l.pic.h - 10, ACCENT);
		}
		if (moreShown())
		{
			bool at = homeZone == HOME_CARDS && homeStrip >= (int)recent.size();
			k.flat(hp.more, pressedCard >= (int)recent.size() ? PRESSED : Kit::cardFill(SURFACE), 12, at ? ACCENT : k.edgeOf(SURFACE));
			ui.drawGlyphMid(G_RIGHT, hp.more.x + hp.more.w / 2, hp.more.y + hp.more.h / 2, at ? ACCENT : DIM, 3);
		}
		if (hp.shortcuts > 0)
		{
			bool liked = ui.browse->playlists->plStore && ui.browse->playlists->plStore->likedCount() > 0;
			const char* names[3] = {liked ? "Shuffle Liked" : "Browse Files", "Send from phone", "All recent"};
			const Glyph* glyphs[3] = {liked ? &G_HEART_S : &G_CARD, &G_WEB, &G_CLOCK};
			for (int i = 0; i < hp.shortcuts; i++)
			{
				Kit::Tile t;
				t.glyph = glyphs[i];
				t.name = names[i];
				t.classic = true;	// (the Classic flat piece: SURFACE and its edge, in Clean too)
				t.lit = i == 0 && liked;
				t.focused = homeZone == HOME_SHORTCUTS && homeShortcut == i;
				t.pressed = pressedShortcut == i;
				k.tile(hp.shortcut[i], t);
			}
		}
		if (quitOpen) drawQuitDialog(ui.nowPlayable(p));
	}

	void Ui::Home::drawAllRecent(const Player& p)
	{
		Kit& k = *ui.kit;
		lay::Shell sh = lay::shell();
		lay::AllRecentPage lp = lay::allRecentPage(sh.body);
		// the kind chips on the line under the header (28 tall: the list's own row maths starts at 84)
		int x = lp.chipStrip.x, cy = lp.chipStrip.y + lp.chipStrip.h / 2;
		for (int i = 0; i < 4; i++)
		{
			std::string label = std::string(FILTER_NAMES[i]) + " " + std::to_string(recentCounts[i]);
			int w = (int)std::lround(ui.textWidth(label, F_P13) / sx) + 20;
			if (x + w > lp.chipStrip.x + lp.chipStrip.w) { recentTabRect[i] = {0, 0, 0, 0}; continue; }
			Rect r{x, cy - 14, w, 28};
			recentTabRect[i] = r;
			bool on = recentTab == i;
			ui.fillRound(r, on ? ACCENT : TRACK, 9);
			ui.drawTextMid(label, F_P13, on ? ON_ACCENT : recentCounts[i] || !i ? TEXT : DIM, r.x + 10, cy);
			x += w + 6;
		}
		if (ui.browse->browseRows.empty() || (ui.browse->browseRows.size() == 1 && ui.browse->browseRows[0].id < 0))
		{
			std::string msg = ui.browse->browseRows.empty() ? "" : ui.browse->browseRows[0].label;
			Kit::Empty e;
			e.glyph = &G_CLOCK;
			e.title = msg.empty() || msg == "Nothing played yet" ? "Nothing played yet" : msg;
			e.lines = wrapEmpty(recentEmptyText(p));
			lay::EmptyState es = lay::emptyState(sh.body, (int)e.lines.size(), 1);
			e.key1 = "Browse Files";
			e.glyph1 = &G_CARD;
			e.hotkeys = p.settings.buttonHotkeys;
			e.pressed1 = false;
			k.emptyState(es, e);
			return;
		}
		for (int i = 0; i < lp.shown; i++)
		{
			int idx = ui.scroll + i;
			if (idx < 0 || idx >= (int)ui.browse->browseRows.size()) break;
			const auto& br = ui.browse->browseRows[idx];
			if (br.id < 0) continue;
			const RecentItem* it = findRecent(ui.browse->browsePaths[idx]);
			Kit::Row row;
			row.font = F_SMALL;
			row.under = BG;
			row.glyph = it ? it->kind : &G_FILM;
			row.name = it ? it->name : br.label;
			row.note = it ? it->sub : br.note;
			if (it && !it->where.empty()) { row.pill = it->where; row.tone = Kit::Pill::Fact; }
			else if (it && it->net) { row.pill = "Network"; row.tone = Kit::Pill::Fact; }
			row.focused = ui.cursor == idx;
			row.lit = false;
			k.row(lp.row(i), row);
		}
	}

	void Ui::Home::drawQuitDialog(bool quitPlaying)
	{
		// the same sheet as Quick settings (F): its panel and title, Quit as its red button, Cancel a row as its rows
		Rect box = quitBox();
		ui.drawDialog(box, "");
		ui.drawTextMid("Quit Banana Player?", 1, ACCENT, box.x + 24, box.y + 28);
		if (quitPlaying) ui.drawTextMid("What's playing stops.", 0, DIM, box.x + box.w - 24, box.y + 28, 0, 1);
		Rect q = quitRow(0);
		bool down = ui.pressedBtn == DLG_ROW, at = quitCursor == 0;
		{
			// the dialog's action key: Classic in Clean too (the one red-filled key)
			Kit::ClassicHere classic;
			ui.drawBox(q, down ? PRESSED : DANGER, R_BUTTON, at);
		}
		SDL_Color ink = down ? TEXT : ON_ACCENT;	// black on the red, as Quick settings' Quit
		int tw = (int)std::lround(ui.textWidth("Quit", 1) / sx), gw = ui.glyphW(G_POWER), x = q.x + q.w / 2 - (tw + gw + 12) / 2;
		ui.drawGlyphMid(G_POWER, x + gw / 2, q.y + q.h / 2, ink);
		ui.drawTextMid("Quit", 1, ink, x + gw + 12, q.y + q.h / 2);
		Rect c = quitRow(1);
		at = quitCursor == 1;
		if (ui.pressedBtn == DLG_ROW + 1) ui.drawBox(c, PRESSED, R_BUTTON);
		else if (at) ui.drawFocus(c);
		ui.drawTextMid("Cancel", 1, at ? ACCENT : TEXT, c.x + c.w / 2, c.y + c.h / 2, 0, 2);
		// its hints are Home::hintList's while the question is up: the shell draws them after this body (a full-panel
		// hint bar here would be painted over, and over the rail with it)
	}

	// ---- the More sheet on a card (X or a hold), Home's own things beside it ----

	void Ui::Home::openRecentMenu(const std::string& path)
	{
		morePath = path;
		ctxRecent = path;
		clearArmedAt = 0;
		ui.openMore([this](const Player& p) { return moreSpec(p); });
	}

	void Ui::Home::openHomeOptions()
	{
		morePath.clear();
		ctxRecent.clear();
		clearArmedAt = 0;
		ui.openMore([this](const Player& p) { return moreSpec(p); });
	}

	Ui::MoreSpec Ui::Home::moreSpec(const Player& p)
	{
		MoreSpec m;
		std::vector<MoreItem> left, right;
		bool forItem = !morePath.empty();
		if (forItem)
		{
			const RecentItem* it = findRecent(morePath);
			if (!it) return m;	// it has gone: the sheet closes itself (MoreSheet draws nothing)
			std::string path = it->path, title = it->name;
			bool doc = isDocument(path);
			bool playing = p.playing() && p.currentPath() == path;
			bool liked = ui.browse->playlists->plStore && ui.browse->playlists->plStore->isLiked(path);
			m.glyph = it->kind;
			m.title = title;
			m.note = it->sub + (it->where.empty() ? "" : " \xC2\xB7 " + it->where);
			auto row = [](const Glyph* g, const std::string& label, const std::string& note, std::string key = "", bool danger = false, bool off = false, bool stay = false) {
				MoreItem i;
				i.glyph = g;
				i.label = label;
				i.note = note;
				i.key = std::move(key);
				i.danger = danger;
				i.off = off;
				i.stay = stay;
				return i;
			};
			{
				MoreItem i = row(&G_PLAY, playing ? "Go to it" : "Resume", it->at, "", false, false, false);
				i.act = [this, path](Player& pl) { ui.openFile(path, pl); return false; };
				left.push_back(i);
			}
			{
				MoreItem i = row(&G_REFRESH, "Play from the start", "", "", false, false, false);
				i.act = [this, path, doc, playing](Player& pl) {
					if (doc) ui.docs->forgetPlace(path);
					if (playing)
					{
						pl.seekFraction(0);
						if (!ui.pageIsOut()) pl.unpause();
						else ui.noteSeek(pl, 0);
						ui.showPlayer();
					}
					else
					{
						pl.forgetPosition(path);
						ui.openFile(path, pl);
					}
					return false;
				};
				left.push_back(i);
			}
			{
				MoreItem i = row(&G_CARD, "Show in folder", "", "", false, false, false);
				i.act = [this, path](Player& pl) { ui.showInFolder(path, pl); return false; };
				left.push_back(i);
			}
			{
				MoreItem i = row(&G_PLUS, "Add to playlist\xE2\x80\xA6", doc ? "Songs and videos" : "", "", false, doc, false);
				i.act = [this, path, title](Player& pl) {
					if (ui.browse->playlists->plStore) ui.browse->playlists->openAddOne(path, title, -1, pl);
					return false;
				};
				left.push_back(i);
			}
			{
				MoreItem i = row(liked ? &G_HEART : &G_HEART_O, liked ? "Unlike" : "Like", doc ? "Songs and videos" : "", "L3", false, doc, true);
				i.lit = liked;
				i.act = [this, path, title, liked](Player& pl) {
					if (!ui.browse->playlists->plStore) return false;
					if (ui.setLikedPath(pl, path, title, -1, !liked) != liked) ui.likeCard(pl, !liked, title);
					return false;
				};
				left.push_back(i);
			}
			{
				MoreItem i = row(&G_INFO, "Info", "", "", false, false, false);
				i.act = [this, path](Player&) { ui.openFileInfo(path, ""); return false; };
				left.push_back(i);
			}
			{
				MoreItem i = row(&G_MINUS, "Remove from Recent", "", "", false, false, false);
				i.act = [this, path](Player& pl) {
					pl.removeFromHistory(path);
					pl.osd("Removed from Recent");
					ui.browse->enterFolder(ui.browse->browseDir, pl);
					if (ui.browse->browseDir == HOMEDIR) homeStrip = std::max(0, std::min(homeStrip, (int)recent.size()));
					return false;
				};
				left.push_back(i);
			}
		}
		// Home's own things, one column; on a card they are the sheet's second column
		{
			MoreItem i;
			i.glyph = &G_CLOCK;
			i.label = "All recent";
			i.note = std::to_string(recent.size());
			i.act = [this](Player& pl) { ui.browse->enterFolder(RECENT, pl); return false; };
			right.push_back(i);
		}
		{
			MoreItem i;
			i.glyph = &G_CARD;
			i.label = "Browse Files";
			i.act = [this](Player& pl) { ui.browse->openSection(TAB_LOCAL, pl); return false; };
			right.push_back(i);
		}
		{
			MoreItem i;
			i.glyph = &G_EYE;
			i.label = "Show hidden folders";
			i.note = p.settings.showHiddenFolders ? "On" : "Off";
			i.stay = true;
			i.act = [this](Player& pl) {
				Settings st = pl.settings;
				st.showHiddenFolders = !st.showHiddenFolders;
				pl.setSettings(st);
				ui.browse->enterFolder(ui.browse->browseDir, pl);
				return false;
			};
			right.push_back(i);
		}
		{
			bool armed = clearArmedAt && SDL_GetTicks() < clearArmedAt;
			MoreItem i;
			i.glyph = &G_TRASH;
			i.label = "Clear recent\xE2\x80\xA6";
			i.note = armed ? "Press again" : "";
			i.danger = armed;
			i.stay = true;
			i.act = [this](Player& pl) {
				if (!clearArmedAt || SDL_GetTicks() >= clearArmedAt) { clearArmedAt = SDL_GetTicks() + 3000; return false; }
				clearArmedAt = 0;
				pl.clearHistory();
				pl.osd("Recent cleared");
				ui.browse->enterFolder(HOMEDIR, pl);
				homeZone = HOME_EMPTY;
				homeStrip = 0;
				return false;
			};
			right.push_back(i);
		}
		{
			MoreItem i;
			i.glyph = &G_GEAR;
			i.label = "Settings";
			i.act = [this](Player& pl) { ui.openSettings(pl, true); return false; };
			right.push_back(i);
		}
		{
			MoreItem i;
			i.glyph = &G_SUN;
			i.label = "Quick settings";
			i.act = [this](Player& pl) { ui.toggleQuick(pl); return false; };
			right.push_back(i);
		}
		if (!forItem)
		{
			m.glyph = &G_HOME;
			m.title = "Home";
			m.note = std::to_string(recent.size()) + (recent.size() == 1 ? " item" : " items") + " in Recent";
			m.rows = right;
			m.cols = 1;
			return m;
		}
		// two columns, a pair a row: the card's actions on the left, Home's on the right
		for (size_t i = 0; i < left.size() || i < right.size(); i++)
		{
			if (i < left.size()) m.rows.push_back(left[i]);
			if (i < right.size()) m.rows.push_back(right[i]);
		}
		m.cols = 2;
		return m;
	}

	// ---- what plays a file, and where it is (Ui's; Home and the dashboard call these) ----

	void Ui::openFile(const std::string& path, Player& p)
	{
		// a document (TXT, EPUB, PDF, a comic): its viewer, not the player
		if (openDocument(path, p)) return;
		// one on the network: copied to the card first, then opened from there
		if (isNetPath(path) && !isMediaFile(path))
		{
			std::string n = netBaseName(path);
			if (isComicName(n) && !p.settings.mangaOn) { alert("Comics need the Manga reader", "Turn it on in Extensions to read CBZ and CBR files."); return; }
			if (isDocumentName(n) || isComicName(n)) { browse->netOpenCopy(path, p); return; }
			alert("Can\xE2\x80\x99t open this file", "\"" + n + "\" isn\xE2\x80\x99t a video, song, photo, book or document the player can open.");
			return;
		}
		// a file of the cards nothing here opens: said, not handed to the player
		if (!isNetPath(path) && !isMediaFile(path))
		{
			alert("Can\xE2\x80\x99t open this file", "\"" + baseName(path) + "\" isn\xE2\x80\x99t a video, song, photo, book or document the player can open.");
			return;
		}
		// what plays: back to it, never from its start
		if (p.playing() && !p.playingPhoto() && p.currentPath() == path) { showPlayer(); return; }
		// a network file whose folder wasn't listed (Recent): listed first, a moment, for the files that play after it,
		// its subtitles and its cover (netListed opens it)
		NetPath np;
		const NetServer* sv = isNetPath(path) && parseNetPath(path, np) && netStore ? netStore->find(np.server) : nullptr;
		if (sv && netWorker && browse->netNames.find(netParent(path)) == browse->netNames.end())
		{
			browse->netOpenPending = path;
			netWorker->list(++browse->netSerial, *sv, netStore->password(*sv), netParent(path));
		}
		else openRequest = path;
	}

	void Ui::showInFolder(const std::string& path, Player& p)
	{
		// its folder, in Files or Network, the cursor on it (a network one's once the listing comes)
		if (isNetPath(path))
		{
			browse->setSection(TAB_NETWORK);
			browse->netSelect = path;
			browse->enterFolder(netParent(path), p);
			return;
		}
		browse->setSection(TAB_LOCAL);
		browse->enterFolder(parentFolder(path), p);
		for (size_t i = 0; i < browse->browsePaths.size(); i++)
			if (browse->browsePaths[i] == path) cursor = (int)i;
		browse->fitList();
	}

	Ui::Rect Ui::Browse::pipRect(int k, const Player& p) const
	{
		// as the mock-up has them (x 344..500, R at 508), moved left only when the battery, the web's globe and the
		// clock need it
		int left = ui.statusLeft(p);
		int rw = (int)std::lround(ui.textWidth("R", 0) / sx);
		int x0 = std::min(344, left - 8 - rw - 8 - 156);
		return {x0 + k * 40, 14, 36, 36};
	}

	void Ui::Browse::drawSectionHeader(const Player& p)
	{
		// "<", the section's name, its markers (L, Files Playlists Network Extensions, R: this one lit), the battery
		ui.drawBackIcon();
		Rect first = pipRect(0, p);
		bool liked = isPlDir(browseDir) && ui.plIsLiked(plFileOf(browseDir));
		ui.drawTextMid(liked ? PlaylistStore::LIKED_NAME : SECTION_NAMES[browseTab], 2, TEXT, 72, 32, std::max(40, first.x - 22 - 72));
		ui.drawTextMid("L", 0, ACCENT, first.x - 10, 32, 0, 1);
		for (int k = 0; k < BROWSE_TABS; k++)
		{
			Rect r = pipRect(k, p);
			bool on = k == browseTab;
			ui.drawBox(r, on ? ACCENT : SURFACE, R_BUTTON);
			ui.drawGlyphMid(*SECTION_GLYPHS[k], r.x + r.w / 2, r.y + r.h / 2, on ? ON_ACCENT : DIM);
		}
		ui.drawTextMid("R", 0, ACCENT, first.x + 164, 32);
		ui.drawBattery(616, 24, p);
	}

	// ---- what plays behind the browser: the playing row's chip, what a photo stopped ----

	bool Ui::goNowPlaying(Player& p)
	{
		// main's player's, also over a photo: from the rail's key, Quick settings' block or SELECT, it closes the
		// photo and opens the player, once (main does it: only it can close the photo viewer)
		const Player& bp = barPlayer(p);
		if (!bp.playing() || bp.playingPhoto()) return false;
		if (p.playingPhoto()) { playerOverPhoto = true; return false; }
		showPlayer();
		return true;
	}

	void Ui::miniToggle(Player& p)
	{
		// the Now playing block's / Home card's Play: main's player, never the photo
		Player& bp = barPlayer(p);
		if (!bp.playing() || bp.playingPhoto()) return;
		// a page plays it: the page's play / pause (as A on the player)
		if (pageDrives(bp)) { toggleBrowser(bp); return; }
		if (!bp.error.empty()) return;
		if (pageIsOut()) { takeOver(bp); return; }
		SessionHost::get().play(bp.paused);
		bp.togglePause();
	}

	void Ui::openNowMenu(const Player& p)
	{
		const Player& bp = barPlayer(p);
		bool live = bp.playing() && !bp.playingPhoto();
		std::vector<int> items = {CTX_NP_NOW, CTX_NP_PLAY};
		if (live && likeable(bp)) items.push_back(CTX_NP_LIKE);
		if (live && browse->playlists->plStore && PlaylistStore::likable(bp.currentPath())) items.push_back(CTX_NP_ADD);
		items.push_back(CTX_NP_SHOW);
		items.push_back(CTX_NP_STOP);
		browse->homeView->ctxRecent.clear();
		ctxOpen = true;
		ctxCursor = 0;
		ctxItems = items;
		ctxServer.clear();
		ctxStatus.clear();
		ctxArmed = false;
		ctxPath.clear();
		ctxFolder.clear();
		ctxTitle = live ? (!bp.title.empty() ? bp.title : entryTitle(bp.currentPath())) : "";
		ctxNote.clear();
		browse->playlists->ctxPlFile.clear();
		browse->playlists->ctxPlItem = -1;
	}

	void Ui::setChip(const Player& p)
	{
		chipKind = 0;
		chipPath.clear();
		chipPlDir.clear();
		const PlaySource* src = nullptr;
		if (p.playing() && !p.playingPhoto())
		{
			chipPath = p.currentPath();
			chipKind = pageDrives(p) ? 3 : p.paused ? 2 : 1;
			src = &p.source();
		}
		if (src && src->kind == SourceKind::Playlist && !src->id.empty()) chipPlDir = plDirFor(src->id);
	}

	int Ui::rowChip(int i) const
	{
		if (!chipKind || i < 0 || i >= (int)browse->browsePaths.size() || i >= (int)browse->browseRows.size() || browse->browsePaths[i] != chipPath) return 0;
		// in a playlist's page only when it plays from that playlist; in a folder (or All recent) wherever it's from
		if (browse->browseRows[i].id == PL_ITEM) return browse->browseDir == chipPlDir ? chipKind : 0;
		return browse->browseRows[i].id == 2 ? chipKind : 0;
	}

	int Ui::drawChip(int right, int mid, int kind, bool card)
	{
		static const char* ROW[] = {"", "Playing", "Paused", "In browser", "Stopped"};
		static const char* CARD[] = {"", "Now", "Paused", "Browser", "Stopped"};
		const Glyph* g = kind == 1 ? &G_PLAY : kind == 2 ? &G_PAUSE : kind == 3 ? &G_LAPTOP : nullptr;
		std::string t = (card ? CARD : ROW)[std::clamp(kind, 0, 4)];
		int tw = (int)std::lround(textWidth(t, 0) / sx), gw = g ? glyphW(*g, 1) + 5 : 0;
		int w = 8 + gw + tw + 8;
		bool off = kind == 4;
		SDL_Color ink = off ? DIM : ON_ACCENT;
		fillRound({right - w, mid - 11, w, 22}, off ? SURFACE : ACCENT, R_CHIP);
		if (g) drawGlyphMid(*g, right - w + 8 + (gw - 5) / 2, mid, ink, 1);
		drawTextMid(t, 0, ink, right - w + 8 + gw, mid);
		return w;
	}

}

