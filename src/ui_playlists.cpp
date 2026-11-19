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
#include "ui_kit.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"

namespace bp
{
	// ---- playlists ----

	void Ui::setStateDir(const std::string& stateDir)
	{
		stateDirPath = stateDir;	// the manga reader's store is kept here too (mangaOpen)
		if (stateDir.empty()) browse->playlists->plStore.reset();
		else browse->playlists->plStore = std::make_unique<PlaylistStore>(stateDir + "/playlists");
	}

	std::vector<std::string> Ui::Playlists::plNames() const
	{
		std::vector<std::string> out;
		if (plStore)
			for (auto& i : plStore->list()) out.push_back(i.name);
		return out;
	}

	void Ui::Playlists::plRows(Player& p)
	{
		ui.browse->browseRows.clear();
		ui.browse->browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& path, const std::string& note) {
			ui.browse->browseRows.push_back({id, label, false, note});
			ui.browse->browsePaths.push_back(path);
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
				const NetServer* sv = ui.netStore && parseNetPath(e.path, np) ? ui.netStore->find(np.server) : nullptr;
				if (sv) note = serverName(*sv);
			}
			else
			{
				// where it was left (as the folders show it), then the folder it's in. Liked: how long it is
				const ResumeEntry* r = p.resumeStore().find(e.path);
				if (ui.plIsLiked(plCur.file) && e.duration > 0)
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

	int Ui::Playlists::plMissingAt(int row) const
	{
		if (!isPlDir(ui.browse->browseDir) || row < 0 || row >= (int)ui.browse->browseRows.size() || ui.browse->browseRows[row].id != PL_ITEM) return 0;
		if (plMissing.size() != ui.browse->browseRows.size()) plMissing.assign(ui.browse->browseRows.size(), -1);
		int8_t& m = plMissing[row];
		if (m < 0)
		{
			const std::string& path = ui.browse->browsePaths[row];
			if (isNetPath(path))
			{
				// (whether the file is on the server is only known by asking it: as it opens)
				NetPath np;
				m = parseNetPath(path, np) && ui.netStore && ui.netStore->find(np.server) ? 0 : 2;
			}
			else m = access(path.c_str(), F_OK) == 0 ? 0 : 1;
			plMissingSerial++;
		}
		return m;
	}

	void Ui::Playlists::plCheckShown()
	{
		if (ui.page != Page::Browse || !isPlDir(ui.browse->browseDir)) return;
		for (int i = ui.scroll; i < ui.scroll + ROWS_SHOWN && i < (int)ui.browse->browseRows.size(); i++) plMissingAt(i);
	}

	void Ui::Playlists::plPlay(const Playlist& pl, int index, bool shuffle, Player& p)
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
		ui.playRequest = {};
		for (int i : order) ui.playRequest.paths.push_back(pl.entries[i].path);
		ui.playRequest.index = index;
		ui.playRequest.source.kind = SourceKind::Playlist;
		ui.playRequest.source.title = pl.name;
		ui.playRequest.source.id = pl.file;
		plOrder = order;
		plOrderId = pl.file;
		ui.sideAsked.clear();
		p.setLastFolder(pl.file);
	}

	void Ui::Playlists::plSaved(Player& p, const std::vector<int>& remap)
	{
		// written a second after the edits stop (a run of moves is one write): tick() or plFlush
		plDirty = true;
		plDirtyAt = SDL_GetTicks();
		plTellPlayer(plCur, remap, p);
		int c = remapIndex(ui.cursor, remap);
		plRefresh(p);
		ui.cursor = std::max(0, c);
		ui.browse->fitList();
	}

	void Ui::Playlists::plFlush(Player& p)
	{
		if (!plDirty) return;
		plDirty = false;
		if (!plStore || !plStore->save(plCur)) ui.alert("Couldn\xE2\x80\x99t save the playlist", "Is the card full or read only?");
	}

	void Ui::Playlists::plTellPlayer(const Playlist& pl, const std::vector<int>& remap, Player& p)
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

	void Ui::Playlists::plRefresh(Player& p)
	{
		int c = ui.cursor, s = ui.scroll;
		plRows(p);
		plMissing.clear();
		plMissingSerial++;
		ui.cursor = c;
		ui.scroll = s;
		ui.browse->fitList();
		ui.browse->browseVersion++;
	}

	// ---- the redesigned pages (docs/redesign: playlists, playlist, playlist-select, playlist-move, liked) ----

	bool Ui::Playlists::onPage() const
	{
		if (ui.page != Page::Browse || ui.inMenu) return false;
		return ui.browse->browseDir == PLROOT || isPlDir(ui.browse->browseDir);
	}

	lay::PlaylistPage Ui::Playlists::pageLayout(const Player& p) const
	{
		lay::Shell sh = lay::shell(ui.miniBarShown(p));
		bool inPl = isPlDir(ui.browse->browseDir);
		bool empty = !inPl || !plLoaded || plCur.entries.empty();
		int keys = inPl && !empty && selMode == SEL_NONE ? (ui.plIsLiked(plCur.file) ? 2 : 3) : 0;
		int bar = 0;
		if (selMode == SEL_EDIT && inPl && !empty) bar = moving ? 2 : 3;
		return lay::playlistPage(sh.body, keys, bar);
	}

	void Ui::Playlists::syncFocus(const Player& p)
	{
		// the page it was left on keeps its cursor; a playlist opened afresh: its playing row if it plays from
		// here, else the Play key (the mock-up's two openings)
		if (!isPlDir(ui.browse->browseDir)) { focusFor.clear(); return; }
		lay::PlaylistPage l = pageLayout(p);
		rowsY = l.rowsY;
		rowsShown = l.rows;
		if (focusFor == plCur.file)
		{
			ui.scroll = std::clamp(ui.scroll, 0, std::max(0, (int)plCur.entries.size() - l.rows));
			return;
		}
		focusFor = plCur.file;
		keyFocus = -1;
		keyPress = barPress = plPressRow = -1;
		int at = -1;
		for (int i = 0; i < (int)ui.browse->browsePaths.size() && at < 0; i++)
			if (ui.rowChip(i)) at = i;
		if (at >= 0)
		{
			ui.cursor = at;
			ui.scroll = std::clamp(at - l.rows + 1, 0, std::max(0, (int)plCur.entries.size() - l.rows));
		}
		else
		{
			ui.cursor = plCur.entries.empty() ? -1 : 0;
			ui.scroll = 0;
			if (!plCur.entries.empty()) keyFocus = 0;
		}
	}

	std::string Ui::Playlists::plTitle(const Player&) const
	{
		if (selMode == SEL_EDIT)
		{
			int n = (int)plSel.size();
			if (moving) return "Moving " + std::to_string(n);
			return n ? std::to_string(n) + " selected" : "Select items";
		}
		if (ui.browse->browseDir == PLROOT) return "Playlists";
		return plCur.name.empty() ? "Playlist" : plCur.name;
	}

	std::string Ui::Playlists::plCrumb(const Player&) const
	{
		auto items = [](size_t n) { return std::to_string(n) + (n == 1 ? " item" : " items"); };
		if (ui.browse->browseDir == PLROOT)
		{
			size_t n = 0;
			if (plStore)
				for (auto& i : plStore->list())
					if (!i.builtin) n++;
			return n ? std::to_string(n) + (n == 1 ? " playlist" : " playlists") : "No playlists yet";
		}
		std::string crumb = "Playlists  \xE2\x80\xBA  " + plCur.name;
		if (selMode == SEL_EDIT)
		{
			if (moving && !plSel.empty())
			{
				int first = *plSel.begin() + 1, last = *plSel.rbegin() + 1;
				crumb += first == last ? "  \xC2\xB7  to place " + std::to_string(first) + " of " + std::to_string(plCur.entries.size())
					: "  \xC2\xB7  to places " + std::to_string(first) + " and " + std::to_string(last) + " of " + std::to_string(plCur.entries.size());
			}
			return crumb;
		}
		double total = 0;
		for (auto& e : plCur.entries)
			if (e.duration > 0) total += e.duration;
		crumb += "  \xC2\xB7  " + items(plCur.entries.size());
		if (total > 0) crumb += "  \xC2\xB7  " + formatTime(total);
		return crumb;
	}

	std::vector<Ui::Hint> Ui::Playlists::plHints(const Player& p) const
	{
		if (selMode == SEL_EDIT)
		{
			if (moving) return {{"UP/DOWN", "Move"}, {"LEFT/RIGHT", "Page"}, {"L2/R2", "Top/bottom"}, {"A", "Put here"}, {"B", "Cancel"}};
			return {{"A", "Tick"}, {"SELECT", "All"}, {"Y", "Move"}, {"X", "More"}, {"B", "Done"}};
		}
		if (ui.browse->browseDir == PLROOT) return {{"A", "Open"}, {"X", "More"}, {"B", "Home"}, {"L/R", "Sections"}};
		if (!plLoaded || plCur.entries.empty())
			return ui.plIsLiked(plCur.file) ? std::vector<Hint>{{"A", "Browse Files"}, {"B", "Playlists"}, {"L/R", "Sections"}}
				: std::vector<Hint>{{"A", "Add files"}, {"Y", "Add folder"}, {"B", "Playlists"}, {"L/R", "Sections"}};
		std::vector<Hint> h{{"A", "Play"}, {"X", "More"}, {"Y", "Select"}, {"SELECT", "Now playing"}, {"B", "Playlists"}};
		if (p.playing()) h[0] = {"A", "Play from here"};
		return h;
	}

	void Ui::Playlists::frame(const Player& p)
	{
		// the page's header and hint bar, in one place: the merge swaps this for the shared Ui::Shell
		lay::Shell sh = lay::shell(ui.miniBarShown(p));
		ui.drawTextMid(plTitle(p), F_P28, TEXT, sh.body.x + lay::MARGIN, 22, sh.main.w - 2 * lay::MARGIN);
		ui.drawTextMid(plCrumb(p), F_I12, DIM, sh.body.x + lay::MARGIN, 44, sh.main.w - 2 * lay::MARGIN);
		ui.kit->hints(plHints(p));
	}

	void Ui::Playlists::addSheet(Player& p)
	{
		// the Add key's two ways (docs/redesign "playlist": Add files… / Add folder…)
		(void)p;
		std::string file = plCur.file, name = plCur.name;
		ui.openMore([this, file, name](const Player&) {
			MoreSpec m;
			m.glyph = &G_PLUS;
			m.title = "Add to " + quoted(name);
			m.rows.push_back({&G_FOLDER, "Add files\xE2\x80\xA6", "Videos and songs you tick", "", [this, file](Player& p) {
				selStart(SEL_ADD, file);
				ui.browse->switchTab(TAB_LOCAL, p);
				return false;
			}});
			m.rows.push_back({&G_FOLDER, "Add folder\xE2\x80\xA6", "Its videos and songs", "", [this, file](Player& p) {
				selStart(SEL_PICK, file);
				ui.browse->switchTab(TAB_LOCAL, p);
				return false;
			}});
			return m;
		});
	}

	void Ui::Playlists::actKey(int i, Player& p)
	{
		if (i == 0 || i == 1)
		{
			if (plCur.entries.empty()) { p.osd(quoted(plCur.name) + " is empty"); return; }
			plPlay(plCur, 0, i == 1, p);
			return;
		}
		if (i == 2) addSheet(p);
	}

	void Ui::Playlists::barAct(int i, Player& p)
	{
		if (moving)
		{
			if (i == 0) moveEnd(true, p);
			else if (i == 1) moveEnd(false, p);
			return;
		}
		if (i == 0) selAct(p);	// Remove (twice), or Unlike in Liked
		else if (i == 1) { if (!plSel.empty()) { moveFromMenu = false; moveStart(p); } }
		else if (i == 2)
		{
			// Add to…: the picker, for the ticked items themselves
			if (plSel.empty()) return;
			pickerEdit = true;
			pickerSel.clear();
			for (int k : plSel)
				if (k >= 0 && k < (int)plCur.entries.size()) pickerSel.push_back(plCur.entries[k]);
			openPicker("", p);
		}
	}

	void Ui::Playlists::draw(const Player& p)
	{
		if (!onPage()) return;
		syncFocus(p);
		lay::PlaylistPage l = pageLayout(p);
		rowsY = l.rowsY;
		rowsShown = l.rows;
		frame(p);
		Kit& k = *ui.kit;
		bool inPl = isPlDir(ui.browse->browseDir);

		if (!inPl)
		{
			int n = (int)ui.browse->browseRows.size();
			if (n == 0 || (n == 1 && ui.browse->browseRows[0].id < 0))
			{
				lay::EmptyState es = lay::emptyState(l.area, 1, plStore ? 1 : 0);
				Kit::Empty e;
				e.glyph = &G_PLAYLIST;
				e.title = plStore ? "No playlists yet" : "Playlists aren't available";
				e.lines = {plStore ? "Gather videos and songs to play in your order." : "There's no folder to keep them in."};
				e.key1 = plStore ? "New playlist" : "";
				e.glyph1 = &G_PLUS;
				e.hotkeys = p.settings.buttonHotkeys;
				k.emptyState(es, e);
				return;
			}
			ui.scroll = std::clamp(ui.scroll, 0, std::max(0, n - l.rows));
			for (int i = ui.scroll; i < n && i < ui.scroll + l.rows; i++)
			{
				const auto& row = ui.browse->browseRows[i];
				const std::string& file = i < (int)ui.browse->browsePaths.size() ? ui.browse->browsePaths[i] : std::string();
				Kit::Row r;
				r.font = F_SMALL;
				r.under = BG;
				r.name = row.label;
				r.note = row.note;
				r.focused = ui.cursor == i;
				if (row.id == PL_NEW) { r.glyph = &G_PLUS; r.lit = false; }
				else if (ui.plIsLiked(file)) r.glyph = &G_HEART;
				else r.glyph = &G_PLAYLIST;
				if (row.id == PL_ROW && p.playing() && p.source().kind == SourceKind::Playlist && p.source().id == file)
				{
					r.pill = "Playing";
					r.tone = Kit::Pill::Live;
				}
				if (row.id == PL_NEW) r.note.clear();
				k.row(l.row(i - ui.scroll), r);
			}
			return;
		}

		if (!plLoaded)
		{
			lay::EmptyState es = lay::emptyState(l.area, 1, 0);
			Kit::Empty e;
			e.glyph = &G_PLAYLIST;
			e.title = "This playlist can't be opened";
			e.lines = {"Was it deleted?"};
			k.emptyState(es, e);
			return;
		}
		if (plCur.entries.empty())
		{
			bool liked = ui.plIsLiked(plCur.file);
			lay::EmptyState es = lay::emptyState(l.area, 2, liked ? 1 : 2);
			Kit::Empty e;
			e.glyph = liked ? &G_HEART_O : &G_PLAYLIST;
			e.title = liked ? "Nothing liked yet" : quoted(plCur.name) + " is empty";
			e.lines = liked ? std::vector<std::string>{"Press L3 on the player to like what plays.", "What you like is kept here, to play or shuffle."}
				: std::vector<std::string>{"Add videos and songs from your cards or the network."};
			e.key1 = liked ? "Browse Files" : "Add files";
			e.key2 = liked ? "" : "Add folder";
			e.glyph1 = &G_FOLDER;
			e.glyph2 = liked ? nullptr : &G_FOLDER;
			e.hotkeys = p.settings.buttonHotkeys;
			k.emptyState(es, e);
			return;
		}

		// the keys over the list
		for (int i = 0; i < l.keys; i++)
		{
			Kit::Key key;
			key.tone = i == 0 ? Kit::Tone::Primary : Kit::Tone::Normal;
			key.glyph = i == 0 ? &G_PLAY : i == 1 ? &G_SHUFFLE : &G_PLUS;
			key.label = i == 0 ? "Play" : i == 1 ? "Shuffle" : "";
			key.key = i == 0 && keyFocus == 0 ? "A" : i == 1 && keyFocus == 1 ? "Y" : "";
			key.focused = keyFocus == i;
			key.pressed = keyPress == i;
			k.key(l.key[i], key);
		}

		int n = (int)plCur.entries.size();
		ui.scroll = std::clamp(ui.scroll, 0, std::max(0, n - l.rows));
		bool likedPl = ui.plIsLiked(plCur.file);
		bool sel = selMode == SEL_EDIT;
		for (int i = ui.scroll; i < n && i < ui.scroll + l.rows; i++)
		{
			const PlaylistEntry& e = plCur.entries[i];
			Rect r = l.row(i - ui.scroll);
			bool lifted = moving && plSel.count(i);
			if (lifted)
			{
				// the block, set in 10 units: a shadow under it, a lifted ground, the grips in yellow
				ui.fillRound({r.x + 12, r.y + 5, r.w - 2, r.h}, PRESSED, R_BUTTON);
				r.x += 10;
			}
			Kit::Row row;
			row.font = F_SMALL;
			row.under = BG;
			row.lead = std::to_string(i + 1);
			row.numberThenIcon = true;
			row.glyph = isAudioFile(e.path) ? &G_NOTE : &G_FILM;
			row.name = e.title.empty() ? entryTitle(e.path) : e.title;
			row.focused = lifted || (!sel && keyFocus < 0 && ui.cursor == i);
			row.lit = lifted;
			int miss = plMissingAt(i);
			int chip = ui.rowChip(i);
			if (chip)
			{
				row.leadGlyph = &G_PLAY;
				static const char* NOW[] = {"", "Now", "Paused", "In browser", "Stopped"};
				row.pill = NOW[std::clamp(chip, 0, 4)];
				row.tone = chip == 1 ? Kit::Pill::Live : Kit::Pill::Fact;
				const PlaylistEntry& e2 = plCur.entries[i];
				double len = e2.duration > 0 ? e2.duration : p.duration;
				row.note = formatTime(ui.shownPosition(p)) + " / " + (len > 0 ? formatTime(len) : std::string("\xE2\x80\x93"));
			}
			else if (miss)
			{
				row.pill = "Gone";
				row.tone = Kit::Pill::Bad;
				row.off = true;
				row.note = miss == 2 ? "connection deleted" : "not found";
			}
			else if (e.duration > 0) row.note = formatTime(e.duration);
			else row.note = i < (int)ui.browse->browseRows.size() ? ui.browse->browseRows[i].note : "";
			if (plStore && plStore->isLiked(e.path) && !likedPl) row.heart = true;
			if (sel)
			{
				row.tick = true;
				row.tickOn = plSel.count(i) > 0;
				row.trail = Kit::Trail::Grip;
			}
			else if (likedPl) row.trail = Kit::Trail::Heart;
			k.row(r, row);
		}
		// the drag's landing line
		if (sel && plDrag.on && !dragSel.empty() && dragTarget() != *dragSel.begin())
		{
			int slot = dragSlot();
			if (slot >= ui.scroll && slot <= ui.scroll + l.rows)
				ui.fillRound({l.list.x, rowsY + (slot - ui.scroll) * lay::PL_ROW_PITCH - 3, l.list.w, 2}, ACCENT, 1);
		}

		// the select / move bar
		if (!l.barKeys) return;
		bool liked = ui.plIsLiked(plCur.file);
		for (int i = 0; i < l.barKeys; i++)
		{
			Kit::Key key;
			if (moving)
			{
				key.tone = i == 0 ? Kit::Tone::Primary : Kit::Tone::Normal;
				key.glyph = i == 0 ? &G_CHECK : &G_CROSS;
				key.label = i == 0 ? "Put here" : "Cancel";
				key.key = i == 0 ? "A" : "B";
			}
			else
			{
				key.tone = i == 0 ? Kit::Tone::Danger : Kit::Tone::Normal;
				key.glyph = i == 0 ? (liked ? &G_HEART_O : &G_MINUS) : i == 1 ? &G_MOVE : &G_PLUS;
				key.label = i == 0 ? (removeArmed ? (liked ? "Again: unlike " : "Again: remove ") + std::to_string((int)plSel.size())
						: std::string(liked ? "Unlike" : "Remove"))
					: i == 1 ? "Move" : "Add to\xE2\x80\xA6";
				key.key = i == 1 ? "Y" : "";
			}
			key.focused = false;
			key.pressed = barPress == i;
			key.off = !moving && i == 0 && plSel.empty();
			k.key(l.barKey[i], key);
		}
	}

	bool Ui::Playlists::key(Button b, Player& p)
	{
		if (!onPage()) return false;
		lay::PlaylistPage l = pageLayout(p);
		rowsY = l.rowsY;
		rowsShown = l.rows;
		bool inPl = isPlDir(ui.browse->browseDir);
		int n = inPl ? (int)plCur.entries.size() : (int)ui.browse->browseRows.size();
		if (n == 1 && !inPl && ui.browse->browseRows[0].id < 0) n = 0;
		if (n <= 0)
		{
			// the empty page's keys (A: its first key; Y: the second)
			if (b == Button::A)
			{
				if (inPl) { if (plLoaded) { selStart(SEL_ADD, plCur.file); ui.browse->switchTab(TAB_LOCAL, p); } }
				else if (plStore) plNameOpen(false, "", "");
				else ui.browse->doEmptyAction(p);
			}
			else if (b == Button::Y && inPl && plLoaded && !ui.plIsLiked(plCur.file))
			{
				selStart(SEL_PICK, plCur.file);
				ui.browse->switchTab(TAB_LOCAL, p);
			}
			else return false;
			return true;
		}

		if (selMode == SEL_EDIT)
		{
			// the bar's and Move's keys are selKey's; A ticks, the arrows walk the rows (the list's own keys come after)
			if (moving) return false;
			if (b == Button::A && ui.cursor >= 0) { selChoose(ui.cursor, p); return true; }
			if (b == Button::Up) ui.cursor = (std::max(ui.cursor, 0) + n - 1) % n;
			else if (b == Button::Down) ui.cursor = (std::max(ui.cursor, 0) + 1) % n;
			else if (b == Button::Left) ui.cursor = std::max(0, ui.cursor - std::max(1, rowsShown));
			else if (b == Button::Right) ui.cursor = std::min(n - 1, ui.cursor + std::max(1, rowsShown));
			else return false;
			if (ui.cursor < ui.scroll) ui.scroll = ui.cursor;
			if (ui.cursor >= ui.scroll + std::max(1, rowsShown)) ui.scroll = ui.cursor - std::max(1, rowsShown) + 1;
			ui.scroll = std::clamp(ui.scroll, 0, std::max(0, n - rowsShown));
			return true;
		}

		// a playlist's keys row (Liked has no Add)
		if (inPl && plLoaded && !plCur.entries.empty())
		{
			int kc = ui.plIsLiked(plCur.file) ? 2 : 3;
			if (keyFocus >= 0)
			{
				if (b == Button::Left) { keyFocus = (keyFocus + kc - 1) % kc; return true; }
				if (b == Button::Right) { keyFocus = (keyFocus + 1) % kc; return true; }
				if (b == Button::Up) return true;
				if (b == Button::A) { actKey(keyFocus, p); return true; }
				if (b == Button::Down) { keyFocus = -1; return true; }
				if (b == Button::Y) { selStart(SEL_EDIT, ""); return true; }
				return false;	// X, B, SELECT: the page's own
			}
			if (b == Button::Up && ui.cursor <= 0) { keyFocus = 0; return true; }
		}
		if (inPl)
		{
			// Y starts selecting (the item's own menu stays on X, or a hold)
			if (b == Button::Y) { selStart(SEL_EDIT, ""); return true; }
			if (b == Button::X)
			{
				bool overKeys = keyFocus >= 0 || ui.cursor < 0;
				ui.openBrowseMenu(overKeys ? -1 : ui.cursor, overKeys ? MENU_X : MENU_Y);
				return true;
			}
		}
		else if (!inPl && b == Button::X)
		{
			// the list of playlists: a row's menu (Play, Shuffle, Rename, Delete), else New playlist
			bool onRow = ui.cursor >= 0 && ui.cursor < (int)ui.browse->browseRows.size() && ui.browse->browseRows[ui.cursor].id == PL_ROW;
			ui.openBrowseMenu(onRow ? ui.cursor : -1, onRow ? MENU_Y : MENU_X);
			return true;
		}
		if (b == Button::A) { if (n) ui.choose(ui.cursor, p); return true; }
		// the rows: the cursor and the scroll, as many as the body shows
		int first = 0;
		if (b == Button::Up) ui.cursor = (std::max(ui.cursor, first) - first + n - 1) % n + first;
		else if (b == Button::Down) ui.cursor = (std::max(ui.cursor, first) - first + 1) % n + first;
		else if (b == Button::Left) ui.cursor = std::max(first, ui.cursor - std::max(1, rowsShown));
		else if (b == Button::Right) ui.cursor = std::min(n - 1 + first, ui.cursor + std::max(1, rowsShown));
		else return false;
		if (ui.cursor < ui.scroll) ui.scroll = ui.cursor;
		if (ui.cursor >= ui.scroll + std::max(1, rowsShown)) ui.scroll = ui.cursor - std::max(1, rowsShown) + 1;
		ui.scroll = std::clamp(ui.scroll, 0, std::max(0, n - rowsShown));
		return true;
	}

	bool Ui::Playlists::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (!onPage()) return false;
		lay::PlaylistPage l = pageLayout(p);
		rowsY = l.rowsY;
		rowsShown = l.rows;
		bool inPl = isPlDir(ui.browse->browseDir);
		int n = inPl ? (int)plCur.entries.size() : (int)ui.browse->browseRows.size();

		// the empty page's keys
		if (inPl && (!plLoaded || plCur.entries.empty()))
		{
			if (pt.kind == Pointer::Down) return true;
			if (pt.kind != Pointer::Up) return false;
			bool liked = ui.plIsLiked(plCur.file);
			lay::EmptyState es = lay::emptyState(l.area, 2, liked ? 1 : 2);
			if (es.key[0].hit(x, y))
			{
				if (liked) ui.browse->openSection(TAB_LOCAL, p);
				else { selStart(SEL_ADD, plCur.file); ui.browse->switchTab(TAB_LOCAL, p); }
			}
			else if (es.keys > 1 && es.key[1].hit(x, y))
			{
				selStart(SEL_PICK, plCur.file);
				ui.browse->switchTab(TAB_LOCAL, p);
			}
			return true;
		}
		if (!inPl && (n == 0 || (n == 1 && ui.browse->browseRows[0].id < 0)))
		{
			if (pt.kind == Pointer::Down) return true;
			if (pt.kind != Pointer::Up || !plStore) return false;
			lay::EmptyState es = lay::emptyState(l.area, 1, 1);
			if (es.key[0].hit(x, y)) plNameOpen(false, "", "");
			return true;
		}

		// the select / move bar
		if (l.barKeys)
		{
			if (pt.kind == Pointer::Down)
			{
				barPress = -1;
				for (int i = 0; i < l.barKeys; i++)
					if (l.barKey[i].hit(x, y)) barPress = i;
				if (barPress >= 0) return true;
			}
			else if (pt.kind == Pointer::Up)
			{
				int i = barPress;
				barPress = -1;
				if (i >= 0 && l.barKey[i].hit(x, y)) barAct(i, p);
				return true;
			}
		}

		// the keys row
		if (l.keys)
		{
			if (pt.kind == Pointer::Down)
			{
				for (int i = 0; i < l.keys; i++)
					if (l.key[i].hit(x, y)) { keyPress = i; keyFocus = i; return true; }
			}
			else if (pt.kind == Pointer::Up && keyPress >= 0)
			{
				int i = keyPress;
				keyPress = -1;
				if (i < l.keys && l.key[i].hit(x, y)) actKey(i, p);
				return true;
			}
		}

		// the rows
		if (pt.kind == Pointer::Down)
		{
			plPressRow = -1;
			plTouchScrolled = false;
			keyFocus = -1;
			if (y >= rowsY && y < rowsY + l.rows * lay::PL_ROW_PITCH)
			{
				int at = ui.scroll + (y - rowsY) / lay::PL_ROW_PITCH;
				if (at >= 0 && at < n && (y - rowsY) % lay::PL_ROW_PITCH < lay::PL_ROW_H)
				{
					plPressRow = at;
					plTouchY = y;
					plScrollStart = ui.scroll;
					ui.cursor = at;
					if (selMode == SEL_EDIT && x >= l.list.x + l.list.w - 34 && selTickable(at)) { dragBegin(at, y, p); return true; }
					ui.dropTouch();
				}
			}
			return true;	// (nothing else on the page answers a fresh finger)
		}
		if (pt.kind == Pointer::Move && plPressRow >= 0)
		{
			if (!plTouchScrolled && std::abs(y - plTouchY) > 12) plTouchScrolled = true;
			if (plTouchScrolled)
			{
				int moved = (int)std::lround((plTouchY - y) / (double)lay::PL_ROW_PITCH);
				ui.scroll = std::clamp(plScrollStart + moved, 0, std::max(0, n - l.rows));
			}
			return true;
		}
		if (pt.kind == Pointer::Up && plPressRow >= 0)
		{
			int at = plPressRow;
			plPressRow = -1;
			if (plTouchScrolled || at < 0 || at >= n) return true;
			Rect r = l.row(at - ui.scroll);
			if (at >= ui.scroll && at < ui.scroll + l.rows && !r.hit(x, y)) return true;
			if (inPl && ui.plIsLiked(plCur.file) && x >= r.x + r.w - 34)
			{
				// a Liked row's heart: unlike (L3's job on the player)
				const PlaylistEntry& e = plCur.entries[at];
				ui.setLikedPath(p, e.path, e.title, e.duration, false);
				return true;
			}
			ui.choose(at, p);
			return true;
		}
		return false;
	}

	void Ui::Playlists::plDelete(const std::string& file, Player& p)
	{
		if (!plStore || ui.plIsLiked(file)) return;
		// its edits not written yet: never (written after, they'd bring it back)
		if (plDirty && plCur.file == file) plDirty = false;
		else plFlush(p);
		std::string name = baseName(file);
		for (auto& i : plStore->list())
			if (i.file == file) name = i.name;
		if (!plStore->remove(file)) { ui.alert("Couldn\xE2\x80\x99t delete the playlist", "Is the card read only?"); return; }
		p.osd("Deleted " + quoted(name));
		if (p.resumeStore().lastFolder == file) p.setLastFolder("");
		int c = ui.cursor, s = ui.scroll;
		bool inList = ui.browse->browseDir == PLROOT;
		ui.browse->enterFolder(PLROOT, p);
		if (inList)
		{
			ui.cursor = c;
			ui.scroll = s;
			ui.browse->fitList();
		}
	}

	void Ui::Playlists::plNameOpen(bool rename, const std::string& file, const std::string& name)
	{
		if (!plStore || (rename && ui.plIsLiked(file))) return;
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
		ui.openKeyboard(PL_NAME, text);
	}

	void Ui::Playlists::plNameDone(Player& p)
	{
		plNameSubmit = false;
		if (!plStore) return;
		// (a rename reads the file: the edits waiting go first)
		plFlush(p);
		std::string name = trimmed(ui.kbEdit.text), file;
		auto names = plNames();
		PlNameError e = plRenaming ? plStore->rename(plRenameFile, name, file) : plStore->create(name, file);
		if (e == PlNameError::None)
		{
			plNameNote.clear();
			ui.kbOpen = false;
			ui.kbPressed = -1;
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
				ui.browse->enterFolder(plDirFor(file), p);
				p.setLastFolder(file);
				return;
			}
			p.osd("Renamed to " + quoted(name));
			std::string old = plRenameFile;
			if (p.playing() && p.source().kind == SourceKind::Playlist && p.source().id == old) p.renameSource(old, name, file);
			if (plOrderId == old) plOrderId = file;
			if (p.resumeStore().lastFolder == old) p.setLastFolder(file);
			if (ui.browse->tabDir[TAB_PLAYLISTS] == plDirFor(old)) ui.browse->tabDir[TAB_PLAYLISTS] = plDirFor(file);
			if (ui.browse->browseDir == plDirFor(old))
			{
				int c = ui.cursor, s = ui.scroll;
				ui.browse->enterFolder(plDirFor(file), p);
				ui.cursor = c;
				ui.scroll = s;
				ui.browse->fitList();
			}
			else if (ui.browse->browseDir == PLROOT)
			{
				ui.browse->enterFolder(PLROOT, p);
				for (size_t i = 0; i < ui.browse->browsePaths.size(); i++)
					if (ui.browse->browsePaths[i] == file) ui.cursor = (int)i;
				ui.browse->fitList();
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
		ui.kbActiveAt = SDL_GetTicks();
	}

	// ---- playlists: selecting, moving, adding ----

	void Ui::Playlists::selStart(int mode, const std::string& dest)
	{
		selEnd();
		selMode = mode;
		selDest = dest;
		selDestName.clear();
		if (plStore && !dest.empty())
			for (auto& i : plStore->list())
				if (i.file == dest) selDestName = i.name;
		ui.dropTouch();
		ui.holdAt = 0;
		ui.browse->fitList();	// editing has no Up row: off it
	}

	void Ui::Playlists::selEnd()
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
		ui.browse->browseVersion++;
	}

	void Ui::Playlists::selLeave(Player& p)
	{
		std::string back = selDest;
		bool add = selMode == SEL_ADD || selMode == SEL_PICK;
		selEnd();
		if (add && !back.empty()) returnToPlaylist(back, -1, p);
	}

	void Ui::Playlists::returnToPlaylist(const std::string& file, int item, Player& p)
	{
		ui.browse->setSection(TAB_PLAYLISTS);
		if (!exists(file)) { ui.browse->enterFolder(PLROOT, p); return; }
		ui.browse->enterFolder(plDirFor(file), p);
		if (item >= 0 && item < (int)ui.browse->browseRows.size())
		{
			ui.cursor = item;
			// the first added in view, as near the top as the list allows (what came with it below it)
			ui.scroll = std::min(item, std::max(0, (int)ui.browse->browseRows.size() - ROWS_SHOWN));
		}
		ui.browse->fitList();
	}

	bool Ui::Playlists::selTickable(int row) const
	{
		if (row < 0 || row >= (int)ui.browse->browseRows.size()) return false;
		if (selMode == SEL_EDIT) return isPlDir(ui.browse->browseDir) && ui.browse->browseRows[row].id == PL_ITEM;
		// a video or a song (playlists are those: never a photo, a document or any other file Files lists)
		return selMode == SEL_ADD && ui.browse->browseRows[row].id == 2 && (isVideoFile(ui.browse->browsePaths[row]) || isAudioFile(ui.browse->browsePaths[row]));
	}

	bool Ui::Playlists::selTicked(int row) const
	{
		if (!selTickable(row)) return false;
		return selMode == SEL_EDIT ? plSel.count(row) > 0 : picked.count(ui.browse->browsePaths[row]) > 0;
	}

	int Ui::Playlists::selFolderFiles(int& ticked) const
	{
		int n = 0;
		ticked = 0;
		for (int i = 0; i < (int)ui.browse->browseRows.size(); i++)
			if (selTickable(i))
			{
				n++;
				if (selTicked(i)) ticked++;
			}
		return n;
	}

	bool Ui::Playlists::pickable() const
	{
		// a folder (not a first page, Recent, or a server's list of shares)
		if (ui.browse->browseDir.empty() || pseudoDir(ui.browse->browseDir)) return false;
		return !isNetPath(ui.browse->browseDir) || (!netBaseName(ui.browse->browseDir).empty() && !ui.browse->netLoading);
	}

	std::string Ui::Playlists::folderName(const std::string& dir) const
	{
		if (dir.empty() || pseudoDir(dir)) return "";
		if (isNetPath(dir)) return netBaseName(dir);
		for (auto& r : ui.roots)
			if (r.path == dir) return r.label;
		return baseName(dir);
	}

	std::vector<PlaylistEntry> Ui::Playlists::pickedEntries() const
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

	bool Ui::Playlists::selChoose(int row, Player& p)
	{
		if (row < 0 || row >= (int)ui.browse->browseRows.size()) return true;
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
		if (ui.browse->browseRows[row].id != 2) return false;
		if (selMode == SEL_ADD && selTickable(row))
		{
			const std::string& path = ui.browse->browsePaths[row];
			if (picked.erase(path)) pickOrder.erase(std::remove(pickOrder.begin(), pickOrder.end(), path), pickOrder.end());
			else
			{
				picked.insert(path);
				pickOrder.push_back(path);
			}
			selSerial++;
		}
		else if (selMode == SEL_ADD) ui.alert("Can\xE2\x80\x99t add this", "Playlists take videos and songs.");
		return true;
	}

	void Ui::Playlists::selAll(Player&)
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
			for (int i = 0; i < (int)ui.browse->browseRows.size(); i++)
			{
				if (!selTickable(i)) continue;
				const std::string& path = ui.browse->browsePaths[i];
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

	void Ui::Playlists::selBack(Player& p, bool now)
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

	void Ui::Playlists::selAct(Player& p)
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
		if (selMode == SEL_PICK && pickable()) addFolderTo(ui.browse->browseDir, selDest, true, p);
	}

	bool Ui::Playlists::selKey(Button b, Player& p)
	{
		bool adding = selMode == SEL_ADD || selMode == SEL_PICK;
		if (b != Button::X) removeArmed = false;
		// (adding, X cancels: its second press drops the ticks)
		if (b != (adding ? Button::X : Button::B)) dropArmed = 0;
		if (moving)
		{
			// the block: a row, a page, or the ends; A drops it, B puts it back
			switch (b)
			{
			case Button::Up: case Button::Down: moveStep(b == Button::Up ? -1 : 1, 1, p); break;
			case Button::Left: case Button::Right: moveStep(b == Button::Left ? -1 : 1, ROWS_SHOWN, p); break;
			case Button::L2: moveStep(-1, (int)plCur.entries.size(), p); break;
			case Button::R2: moveStep(1, (int)plCur.entries.size(), p); break;
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
			if (adding && !ui.browse->atFirstPage()) { ui.browse->browseUp(p); return true; }
			selBack(p, false);
			return true;
		case Button::L: case Button::R:
			// adding: the cards and the network, the playlists' tab left out (and the connection's form, the search:
			// the connections instead)
			if (selMode == SEL_EDIT) return true;
			if (ui.browse->tabDir[TAB_NETWORK] == FORMDIR || ui.browse->tabDir[TAB_NETWORK] == FINDNET) ui.browse->tabDir[TAB_NETWORK] = NETROOT;
			ui.browse->switchTab(b == Button::R ? TAB_NETWORK : TAB_LOCAL, p);
			return true;
		default: return true;	// START and the rest: nothing while selecting
		}
	}

	void Ui::Playlists::moveFollow()
	{
		if (plSel.empty()) return;
		int first = *plSel.begin(), last = *plSel.rbegin(), n = (int)ui.browse->browseRows.size();
		ui.cursor = first;
		if (last >= ui.scroll + ROWS_SHOWN) ui.scroll = last - ROWS_SHOWN + 1;
		if (first < ui.scroll) ui.scroll = first;
		ui.scroll = std::clamp(ui.scroll, 0, std::max(0, n - ROWS_SHOWN));
	}

	void Ui::Playlists::moveStart(Player& p)
	{
		if (plSel.empty() || !plLoaded) return;
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

	void Ui::Playlists::moveStep(int dir, int times, Player& p)
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

	void Ui::Playlists::moveEnd(bool keep, Player& p)
	{
		moving = false;
		if (keep)
		{
			bool same = true;
			for (int i = 0; i < (int)moveRemap.size(); i++) same = same && moveRemap[i] == i;
			// (the cursor stays on the block: plSaved would take it for an old index)
			int c = ui.cursor, s = ui.scroll;
			if (!same) plSaved(p, moveRemap);
			ui.cursor = c;
			ui.scroll = s;
			ui.browse->fitList();
		}
		else
		{
			plCur.entries = moveOrig;
			plSel = moveOrigSel;
			plRefresh(p);
			if (!plSel.empty()) ui.cursor = *plSel.begin();
			ui.browse->fitList();
		}
		moveOrig.clear();
		moveRemap.clear();
		// from an item's menu: back to the list as it was; from Select: the same ticks
		if (moveFromMenu)
		{
			int c = ui.cursor, s = ui.scroll;
			selEnd();
			ui.cursor = c;
			ui.scroll = s;
		}
		selSerial++;
	}

	void Ui::Playlists::dragBegin(int row, int y, Player& p)
	{
		// (Liked is an ordinary .m3u in the state folder: its order is as safe to change as any playlist's)
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
		ui.holdAt = 0;
	}

	int Ui::Playlists::dragSlot() const
	{
		int n = (int)ui.browse->browseRows.size();
		return std::clamp(ui.scroll + (int)std::lround((plDrag.y - rowsY) / (double)lay::PL_ROW_PITCH), 0, n);
	}

	int Ui::Playlists::dragTarget() const
	{
		if (dragSel.empty()) return -1;
		int start = *dragSel.begin(), len = (int)dragSel.size(), s = dragSlot();
		return s <= start ? s : s >= start + len ? s - len : start;
	}

	void Ui::Playlists::dragEnd(Player& p)
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
		int first = *dragSel.begin(), s = ui.scroll;
		if (!same) plSaved(p, moveRemap);
		moveRemap.clear();
		dragSel.clear();
		ui.cursor = first;
		ui.scroll = s;
		ui.browse->fitList();
		selSerial++;
	}

	void Ui::Playlists::openPicker(const std::string& folder, Player& p, bool one)
	{
		if (!plStore) return;
		plFlush(p);
		pickerEdit = selMode == SEL_EDIT;
		if (pickerEdit)
		{
			// the ticked items themselves (Add to… on the select bar), not files ticked in Files
			pickerSel.clear();
			for (int k : plSel)
				if (k >= 0 && k < (int)plCur.entries.size()) pickerSel.push_back(plCur.entries[k]);
		}
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
		ui.pressedBtn = -1;
	}

	void Ui::Playlists::pickerNew(Player&)
	{
		// named after the folder (a ticked file's, the one the menu was for)
		pickerOpen = false;
		plNameOpen(false, "", pickerOne.empty() ? folderName(pickerFolder.empty() ? ui.browse->browseDir : pickerFolder) : "");
		plNameThenAdd = true;
		plNameAddFolder = pickerFolder;
	}

	void Ui::Playlists::pickerChoose(int i, Player& p)
	{
		ui.pressedBtn = -1;
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

	void Ui::Playlists::pickerAdd(const std::string& dest, Player& p)
	{
		if (pickerEdit)
		{
			// the playlist's ticked items, into another playlist; the ticks stay for another
			if (addTo(dest, pickerSel, 0, plCur.name, false, p))
			{
				pickerEdit = false;
				pickerSel.clear();
			}
			return;
		}
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

	bool Ui::Playlists::pickerKey(Button b, Player& p)
	{
		int n = (int)pickerList.size() + 1;
		int shown = pickerShown();
		if (b != Button::A) pickerArmed = -1;	// moved off it: asked again next time
		switch (b)
		{
		case Button::Up: pickerCursor = (pickerCursor + n - 1) % n; break;
		case Button::Down: pickerCursor = (pickerCursor + 1) % n; break;
		case Button::Left: pickerCursor = std::max(0, pickerCursor - shown); break;
		case Button::Right: pickerCursor = std::min(n - 1, pickerCursor + shown); break;
		case Button::A: pickerChoose(pickerCursor, p); return true;
		case Button::B: pickerOpen = false; return true;
		default: return true;
		}
		if (pickerCursor < pickerScroll) pickerScroll = pickerCursor;
		if (pickerCursor >= pickerScroll + shown) pickerScroll = pickerCursor - shown + 1;
		return true;
	}

	bool Ui::Playlists::pickerPointer(const Pointer& pt, int x, int y, Player& p)
	{
		int n = (int)pickerList.size() + 1;
		int shown = pickerShown();
		if (pt.kind == Pointer::Down)
		{
			// outside the box: closed; on a row, chosen as the finger lifts there (a drag scrolls)
			if (!pickerBox().hit(x, y)) { pickerOpen = false; ui.swallowTouch = true; return false; }
			ui.pressedBtn = -1;
			for (int i = pickerScroll; i < n && i < pickerScroll + shown; i++)
				if (pickerRow(i).hit(x, y)) ui.pressedBtn = PICK_ROW + i;
			pickerTouchY = y;
			pickerScrollStart = pickerScroll;
			pickerScrolled = false;
			return false;
		}
		if (pt.kind == Pointer::Move && (pickerScrolled || std::abs(y - pickerTouchY) > 12))
		{
			pickerScrolled = true;
			ui.pressedBtn = -1;
			pickerScroll = std::clamp(pickerScrollStart + (int)std::lround((pickerTouchY - y) / 46.0), 0, std::max(0, n - shown));
			pickerCursor = std::clamp(pickerCursor, pickerScroll, pickerScroll + shown - 1);
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			int id = ui.pressedBtn;
			ui.pressedBtn = -1;
			if (!pickerScrolled && id >= PICK_ROW && pickerRow(id - PICK_ROW).hit(x, y))
			{
				pickerCursor = id - PICK_ROW;
				pickerChoose(pickerCursor, p);
			}
		}
		return false;
	}

	void Ui::Playlists::addFolderTo(const std::string& dir, const std::string& dest, bool back, Player& p)
	{
		// the folder's own videos and songs (not its folders'), in name order
		std::vector<std::string> names;
		if (isNetPath(dir))
		{
			auto it = ui.browse->netNames.find(dir);
			if (it == ui.browse->netNames.end())
			{
				// never listed: listed now (a moment; the busy dialog can cancel it), added when it comes
				NetPath np;
				const NetServer* sv = ui.netStore && parseNetPath(dir, np) ? ui.netStore->find(np.server) : nullptr;
				if (!sv || !ui.netWorker) { ui.alert("This connection is gone", "It was deleted. Add it again in Network."); return; }
				addPending = {dir, dest, ++addSerial, back};
				ui.netWorker->list(addSerial, *sv, ui.netStore->password(*sv), dir);
				return;
			}
			names = it->second;
		}
		else
		{
			bool ok;
			for (auto& e : listFolder(dir, ok, true))
				if (!e.dir) names.push_back(e.name);
			if (!ok) { ui.alert("Can\xE2\x80\x99t open this folder", "Is the card still in?"); return; }
		}
		int photos = 0;
		auto add = folderEntries(dir, names, photos);
		addTo(dest, add, photos, folderName(dir), back, p);
	}

	bool Ui::Playlists::addTo(const std::string& destRef, const std::vector<PlaylistEntry>& add, int photos, const std::string& from,
		bool back, Player& p)
	{
		std::string dest = destRef;	// (a copy: it can be selDest, which selEnd clears)
		auto many = [](int n, const char* one, const char* more) { return std::to_string(n) + " " + (n == 1 ? one : more); };
		std::string photoNote = photos ? " \xC2\xB7 skipped " + many(photos, "photo", "photos") : "";
		if (add.empty())
		{
			ui.alert("Nothing to add", "There are no videos or songs in " + quoted(from) + photoNote + ".");
			return false;
		}
		Playlist pl;
		int skipped = 0;
		plFlush(p);
		if (ui.plIsLiked(dest)) { ui.alert("Liked fills itself", "Use the Like button (the heart) to add to Liked."); return false; }
		if (!plStore || !plStore->load(dest, pl, skipped)) { ui.alert("Couldn\xE2\x80\x99t read the playlist", "Its file may be damaged, or the card read only."); return false; }
		int before = (int)pl.entries.size(), dropped = 0;
		bool again = !pickerOne.empty() && plStore->containsPath(dest, pickerOne);
		auto remap = addEntries(pl.entries, add, -1, PL_MAX_ENTRIES, dropped);
		int added = (int)add.size() - dropped;
		if (added <= 0) { ui.alert("The playlist is full", quoted(pl.name) + " has " + std::to_string(PL_MAX_ENTRIES) + " items, as many as a playlist holds."); return false; }
		if (!plStore->save(pl)) { ui.alert("Couldn\xE2\x80\x99t save the playlist", "Is the card full or read only?"); return false; }
		plTellPlayer(pl, remap, p);
		std::string note = "Added " + std::to_string(added) + " to " + quoted(pl.name) + photoNote;
		if (dropped) note += " \xC2\xB7 Playlist is full, " + std::to_string(dropped) + " left out";
		if (!pickerOne.empty())
		{
			// one file: the card over the top panel, the Add square's check
			p.showNoteCard(G_PLAYLIST_ROWS, G_PLAYLIST.h, (again ? "Added again to " : "Added to ") + pl.name,
				pickerNewOne ? "New playlist \xC2\xB7 " + pickerOneTitle : pickerOneTitle);
			ui.addFlashUntil = SDL_GetTicks() + 1200;
		}
		else p.osd(note);
		if (back)
		{
			selEnd();
			returnToPlaylist(dest, before, p);
		}
		else ui.browse->browseVersion++;
		return true;
	}

	void Ui::Playlists::cancelAdd(Player& p)
	{
		addPending = {};
		ui.pressedBtn = -1;
		if (!ui.netWorker) return;
		// everything asked of the worker goes: the player's sidecar folders (its file opens without them), a file
		// waiting for its folder (opened alone), the folder being listed here (asked again)
		ui.netWorker->cancelAll();
		for (auto& s : ui.sidePending) p.netFolderListed(s.second, {}, false);
		ui.sidePending.clear();
		if (!ui.browse->netOpenPending.empty())
		{
			ui.openRequest = ui.browse->netOpenPending;
			ui.browse->netOpenPending.clear();
		}
		if (ui.browse->netLoading && isNetPath(ui.browse->browseDir)) ui.browse->enterFolder(ui.browse->browseDir, p);
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

	void Ui::Browse::drawWhere(int x, int maxW)
	{
		// a network folder the account can't write to says so at the line's end: a lock and "Read only"
		if (isNetPath(browseDir) && netOpsHere() && !netLoading && !netWritable && ui.page == Page::Browse)
		{
			const char* ro = "Read only";
			int tw = (int)std::lround(ui.textWidth(ro, 0) / sx), gw = ui.glyphW(G_LOCK);
			int right = x + maxW;
			ui.drawTextMid(ro, 0, DIM, right - tw, SUB_Y);
			ui.drawGlyphMid(G_LOCK, right - tw - 8 - gw / 2, SUB_Y, DIM);
			maxW -= tw + gw + 24;
		}
		std::string where = ui.elideLeft(browseWhere(), 0, maxW);
		size_t cut = where.rfind(" / ");
		if (cut == std::string::npos) ui.drawTextMid(where, 0, TEXT, x, SUB_Y);
		else
		{
			std::string head = where.substr(0, cut + 3);
			ui.drawTextMid(head, 0, DIM, x, SUB_Y);
			ui.drawTextMid(where.substr(cut + 3), 0, TEXT, x + (int)std::lround(ui.textWidth(head, 0) / sx), SUB_Y);
		}
	}

	void Ui::Playlists::drawSelHeader(const Player& p)
	{
		ui.drawBackIcon();
		ui.drawBattery(616, 24, p);
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
		int cw = count.empty() ? 0 : (int)std::lround(ui.textWidth(count, 1) / sx);
		int end = std::min(496, ui.statusLeft(p) - 12);	// (left of the clock)
		if (!count.empty()) ui.drawTextMid(count, 1, countInk, end, 32, 0, 1);
		ui.drawTextMid(title, 2, TEXT, 72, 32, std::max(40, end - (cw ? cw + 16 : 0) - 72));
		// the line under it: Move's keys; adding, where it is (a card or the network); a playlist's, as ever
		if (moving)
		{
			int x = 24;
			for (auto& part : std::vector<std::pair<std::string, std::string>>{{"UP/DOWN", " move"}, {" \xC2\xB7 ", ""}, {"A", " drop"},
					 {" \xC2\xB7 ", ""}, {"B", " cancel"}})
			{
				bool sep = part.second.empty();
				ui.drawTextMid(part.first, 0, sep ? DIM : ACCENT, x, SUB_Y);
				x += (int)std::lround(ui.textWidth(part.first, 0) / sx);
				if (!sep)
				{
					ui.drawTextMid(part.second, 0, DIM, x, SUB_Y);
					x += (int)std::lround(ui.textWidth(part.second, 0) / sx);
				}
			}
			return;
		}
		if (selMode == SEL_EDIT)
		{
			std::string c = items((int)plCur.entries.size());
			ui.drawTextMid(c, 0, DIM, 616, SUB_Y, 0, 1);
			ui.browse->drawWhere(24, 592 - (int)std::lround(ui.textWidth(c, 0) / sx) - 24);
			return;
		}
		bool net = ui.browse->browseTab == TAB_NETWORK;
		ui.drawGlyphMid(net ? G_NETWORK : G_CARD, 33, SUB_Y, DIM);
		ui.browse->drawWhere(52, 564);
	}

	void Ui::Playlists::drawBarButton(int id, const Glyph* g, const std::string& label, const char* key, SDL_Color ink, bool filled, bool off)
	{
		Rect r = ui.buttonRect(id);
		bool down = ui.pressedBtn == id && !off;
		bool lit = filled && !off;
		ui.drawBox(r, down ? PRESSED : lit ? ACCENT : BUTTON, R_BUTTON);
		SDL_Color c = off ? DIM : lit && !down ? ON_ACCENT : ink;
		SDL_Color k = off ? DIM : lit && !down ? ON_ACCENT : ACCENT;
		// the key at the right; the glyph and the label in the middle of what's left
		int kw = key ? (int)std::lround(ui.textWidth(key, 0) / sx) : 0;
		if (key) ui.drawTextMid(key, 0, k, r.x + r.w - 12, r.y + r.h / 2, 0, 1);
		int room = r.w - 24 - (kw ? kw + 8 : 0);
		int gw = g ? ui.glyphW(*g) : 0;
		int tw = std::min(room - (gw ? gw + 8 : 0), (int)std::lround(ui.textWidth(label, 0) / sx));
		int x = r.x + 12 + std::max(0, (room - (gw ? gw + 8 : 0) - tw) / 2);
		if (g)
		{
			ui.drawGlyphMid(*g, x + gw / 2, r.y + r.h / 2, c);
			x += gw + 8;
		}
		ui.drawTextMid(label, 0, c, x, r.y + r.h / 2, tw + 1);
	}

	void Ui::Playlists::drawSelBar()
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
			bool liked = ui.plIsLiked(plCur.file);
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

	Ui::Rect Ui::Playlists::pickerBox() const
	{
		lay::Sheet s = lay::sheet({0, (int)pickerList.size() + 1, 1, 0});
		return {s.box.x, s.box.y, s.box.w, s.box.h};
	}

	Ui::Rect Ui::Playlists::pickerRow(int i) const
	{
		lay::Sheet s = lay::sheet({0, (int)pickerList.size() + 1, 1, 0});
		lay::Box b = s.row(i - pickerScroll);
		return {b.x, b.y, b.w, b.h};
	}

	int Ui::Playlists::pickerShown() const
	{
		int n = (int)pickerList.size() + 1;
		lay::Sheet s = lay::sheet({0, n, 1, 0});
		int fit = (s.hints.y - s.rowsY) / lay::SHEET_ROW;
		return std::clamp(fit, 1, n);
	}

	void Ui::Playlists::drawPicker()
	{
		lay::Sheet sh = lay::sheet({0, (int)pickerList.size() + 1, 1, 0});
		std::string title = pickerEdit ? "Add " + std::to_string((int)pickerSel.size()) + (pickerSel.size() == 1 ? " item" : " items") + " to\xE2\x80\xA6"
			: !pickerOne.empty() ? "Add " + quoted(pickerOneTitle) + " to\xE2\x80\xA6"
			: pickerFolder.empty() ? "Add " + std::to_string(picked.size()) + " to\xE2\x80\xA6"
			: "Add " + quoted(folderName(pickerFolder)) + " to\xE2\x80\xA6";
		ui.kit->sheet(sh, {&G_PLUS, title, ""});
		int n = (int)pickerList.size() + 1, shown = pickerShown();
		for (int i = pickerScroll; i < n && i < pickerScroll + shown; i++)
		{
			Rect r = pickerRow(i);
			Kit::Row row;
			row.font = F_P16;
			row.under = SURFACE;
			row.focused = i == pickerCursor;
			row.pressed = ui.pressedBtn == PICK_ROW + i;
			if (i == 0)
			{
				row.glyph = &G_PLUS;
				row.name = "New playlist\xE2\x80\xA6";
			}
			else
			{
				auto& pl = pickerList[i - 1];
				bool has = i - 1 < (int)pickerHas.size() && pickerHas[i - 1], armed = pickerArmed == i;
				row.glyph = &G_PLAYLIST;
				row.name = pl.name;
				row.note = std::to_string(pl.count) + (pl.count == 1 ? " item" : " items");
				if (armed) { row.pill = "Add again?"; row.tone = Kit::Pill::Live; }
				else if (has) { row.pill = "Added"; row.tone = Kit::Pill::Fact; }
			}
			ui.kit->row(r, row);
		}
		// where the list is, when it's longer than the box
		if (n > shown)
		{
			Rect first = pickerRow(pickerScroll), last = pickerRow(pickerScroll + shown - 1);
			int top = first.y, height = last.y + last.h - top;
			ui.fillRound({sh.box.x + sh.box.w - 12, top, 4, height}, SCREEN, 2);
			int h = std::max(16, height * shown / n);
			ui.fillRound({sh.box.x + sh.box.w - 12, top + (height - h) * pickerScroll / std::max(1, n - shown), 4, h}, DIM, 2);
		}
		ui.kit->hints({{"A", pickerArmed > 0 ? "Add again" : "Add"}, {"B", "Close"}});
	}


	void Ui::Playlists::drawBusy()
	{
		Rect box{136, 132, 368, 168};
		ui.drawDialog(box, "Reading folder\xE2\x80\xA6");
		ui.drawTextMid(folderName(addPending.dir), 0, DIM, W / 2, box.y + 64, 328, 2);
		Rect c = ui.buttonRect(BUSY_CANCEL);
		ui.drawDialogRow(c, &G_CROSS, "Cancel", true, ui.pressedBtn == BUSY_CANCEL, TEXT);
		ui.drawHints({{"B", "Cancel"}});
	}

	// ---- playlists edited from the web dashboard ----

	std::string Ui::Playlists::plWebFile(const std::string& id) const
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

	void Ui::Playlists::plWebEdited(const std::string& file, const std::vector<int>& remap, Player& p)
	{
		if (ui.browse->browseDir == plDirFor(file))
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
				ui.cursor = std::max(0, remapIndex(ui.cursor, remap));
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

	void Ui::Playlists::plWebRenamed(const std::string& old, const std::string& file, const std::string& name, Player& p)
	{
		// as a rename on the handheld goes on (plNameDone)
		if (p.playing() && p.source().kind == SourceKind::Playlist && p.source().id == old) p.renameSource(old, name, file);
		if (plOrderId == old) plOrderId = file;
		if (p.resumeStore().lastFolder == old) p.setLastFolder(file);
		if (ui.browse->tabDir[TAB_PLAYLISTS] == plDirFor(old)) ui.browse->tabDir[TAB_PLAYLISTS] = plDirFor(file);
		if (ui.page != Page::Browse) return;
		if (ui.browse->browseDir == plDirFor(old))
		{
			int c = ui.cursor, s = ui.scroll;
			ui.browse->enterFolder(plDirFor(file), p);
			ui.cursor = c;
			ui.scroll = s;
			ui.browse->fitList();
		}
		else plWebListChanged(p);
	}

	void Ui::Playlists::plWebDeleted(const std::string& file, Player& p)
	{
		// (its edits not written: never, now)
		if (plCur.file == file) plDirty = false;
		if (p.resumeStore().lastFolder == file) p.setLastFolder("");
		if (ui.browse->tabDir[TAB_PLAYLISTS] == plDirFor(file)) ui.browse->tabDir[TAB_PLAYLISTS] = PLROOT;
		if (ui.browse->browseDir == plDirFor(file))
		{
			selEnd();
			moving = false;
			plDrag.on = false;
			ui.ctxOpen = false;
			ui.browse->enterFolder(PLROOT, p);
		}
		else plWebListChanged(p);
	}

	void Ui::Playlists::plWebListChanged(Player& p)
	{
		// the list of playlists on show: read again, the cursor on the same one
		if (ui.browse->browseDir != PLROOT || ui.ctxOpen || ui.kbOpen) return;
		std::string at = ui.cursor >= 0 && ui.cursor < (int)ui.browse->browsePaths.size() ? ui.browse->browsePaths[ui.cursor] : "";
		int s = ui.scroll;
		ui.browse->enterFolder(PLROOT, p);
		for (size_t i = 0; i < ui.browse->browsePaths.size(); i++)
			if (ui.browse->browsePaths[i] == at) ui.cursor = (int)i;
		ui.scroll = s;
		ui.browse->fitList();
	}

	// ---- the browser owns the playlists ----

	Ui::Browse::Browse(Ui& u) : Area(u), playlists(std::make_unique<Playlists>(u)), homeView(std::make_unique<Home>(u)) {}
	Ui::Browse::~Browse() = default;

	// ---- Ui's, out of ui.h (they read the playlists' state) ----

	bool Ui::animating() const { return (area() && area()->animating()) || marqueeMoving() || loaderShown || playHoldAt != 0 || flickAt != 0 || browse->playlists->plDrag.on || slideWaiting() || browse->homeView->stripT0 != 0 || likePopAt != 0 || addFlashUntil != 0 || landFlashUntil != 0; }
	void Ui::flushPlaylist(Player& p) { browse->playlists->plFlush(p); }
	int Ui::playlistSerial() const { return browse->playlists->plStore ? browse->playlists->plStore->serial() : -1; }
	bool Ui::plIsLiked(const std::string& file) const { return browse->playlists->plStore && browse->playlists->plStore->isBuiltinFile(file); }
}
