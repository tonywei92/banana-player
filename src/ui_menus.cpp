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
#include "durations.h"

namespace bp
{
	std::vector<Ui::Row> Ui::rows(const Player& p) const
	{
		if (page == Page::Browse) return browseRows;
		std::vector<Row> out;
		if (page == Page::UpNext)
		{
			// a file a row (its place the id), the one playing as selected
			int cur = -1;
			auto list = upNextList(p, cur);
			for (size_t i = 0; i < list.size(); i++) out.push_back({(int64_t)i, entryTitle(list[i]), (int)i == cur});
			return out;
		}
		if (page == Page::Chapters)
		{
			// a chapter a row (its index the id), the one playing as selected
			int cur = shownChapter(p);
			for (size_t i = 0; i < p.chapters.size(); i++) out.push_back({(int64_t)i, chapterLabel(p.chapters[i], (int)i + 1), (int)i == cur});
			return out;
		}
		if (tracksTab == TRACKS_PICTURE)
		{
			// the picture's size, as the old SELECT went round them (the mode the id)
			static const char* NOTES[] = {"The whole picture", "Screen filled, edges cut", "Screen filled, stretched"};
			for (int m = 0; m < 3; m++)
				out.push_back({m, aspectName((AspectMode)m), p.aspect == (AspectMode)m, NOTES[m]});
			return out;
		}
		TrackType type = trackType();
		auto list = p.tracksOf(type);
		if (type == TrackType::Sub)
			out.push_back({0, "Off", p.selected(TrackType::Sub) == nullptr, list.empty() ? "No subtitles in this file" : ""});
		for (size_t i = 0; i < list.size(); i++) out.push_back({list[i].id, trackLabel(list[i], (int)i + 1), list[i].selected});
		if (type == TrackType::Sub) out.push_back({STYLE_ROW, "Subtitle style...", false});
		if (out.empty()) out.push_back({-1, "No audio in this file", false});
		return out;
	}

	void Ui::openMenu(Page pg, const Player& p)
	{
		page = pg;
		cursor = scroll = 0;
		if (pg == Page::Settings) { setGroup = -1; return; }
		auto r = rows(p);
		for (size_t i = 0; i < r.size(); i++)
			if (r[i].selected) cursor = (int)i;
		scroll = std::max(0, cursor - ROWS_SHOWN + 1);
	}

	std::vector<int> Ui::tracksTabs(const Player& p) const
	{
		// (a song's sound is its graph's: no track to choose; its chapters only)
		std::vector<int> t;
		if (!p.chapters.empty()) t.push_back(TRACKS_CHAPTERS);
		if (!p.playingAudio()) t.insert(t.end(), {TRACKS_SUBS, TRACKS_AUDIO, TRACKS_PICTURE});
		return t;
	}

	void Ui::openTracks(int tab, const Player& p)
	{
		tracksTab = tab;
		tracksTabUsed = true;
		if (tab == TRACKS_CHAPTERS) { openChapters(p); return; }
		openMenu(Page::Tracks, p);
	}

	void Ui::openTracksPage(const Player& p)
	{
		auto tabs = tracksTabs(p);
		if (tabs.empty()) return;
		int want = tracksTabUsed ? tracksTab : p.chapters.empty() ? TRACKS_SUBS : TRACKS_CHAPTERS;
		// (the last one shown isn't this file's: no chapters here) Subtitles, else the first there is
		if (std::find(tabs.begin(), tabs.end(), want) == tabs.end())
			want = std::find(tabs.begin(), tabs.end(), (int)TRACKS_SUBS) != tabs.end() ? TRACKS_SUBS : tabs[0];
		openTracks(want, p);
	}

	void Ui::stepTracksTab(int dir, const Player& p)
	{
		auto tabs = tracksTabs(p);
		int cur = page == Page::Chapters ? TRACKS_CHAPTERS : tracksTab;
		auto it = std::find(tabs.begin(), tabs.end(), cur);
		if (it == tabs.end()) return;
		int i = (int)(it - tabs.begin()) + dir;
		if (i >= 0 && i < (int)tabs.size()) openTracks(tabs[i], p);
	}

	void Ui::openChapters(const Player& p)
	{
		page = Page::Chapters;
		tracksTab = TRACKS_CHAPTERS;
		tracksTabUsed = true;
		// on the one playing, three rows above it showing where the list allows (what came before it, a little)
		int n = (int)p.chapters.size();
		cursor = std::clamp(shownChapter(p), 0, std::max(0, n - 1));
		scroll = std::clamp(cursor - 3, 0, std::max(0, n - ROWS_SHOWN));
	}

	void Ui::choose(int i, Player& p)
	{
		// ".." (row -1): up a folder: as B and "<" go, selecting or not
		if (page == Page::Browse && i < 0 && upRow()) { browseUp(p); return; }
		auto r = rows(p);
		if (i < 0 || i >= (int)r.size()) return;
		if (page == Page::Browse)
		{
			// no connections yet: A adds one (as the page's button does); no playlists, New playlist
			if (emptyAction()) { doEmptyAction(p); return; }
			if (r[i].id < 0) return;
			// selecting: a row ticked (a folder still opens)
			if (selMode != SEL_NONE && selChoose(i, p)) return;
			if (r[i].id == PL_NEW) { plNameOpen(false, "", ""); return; }
			if (r[i].id == PL_ROW)
			{
				// a playlist: its items (and the one kept for the next launch, as a folder gone into is)
				std::string file = browsePaths[i];
				enterFolder(plDirFor(file), p);
				p.setLastFolder(file);
				return;
			}
			if (r[i].id == PL_ITEM)
			{
				// an item that isn't there: what can be done about it (its menu, why under the title)
				if (plMissingAt(i)) { openBrowseMenu(i, MENU_Y); return; }
				// the item that plays (from this playlist): back to it, never from its start
				if (rowChip(i)) { showPlayer(); return; }
				plPlay(plCur, i, false, p);
				return;
			}
			if (browseDir == FORMDIR) { formChoose(r[i].id, p); return; }
			if (browseDir == FINDNET)
			{
				if (r[i].id == R_FOUND && i < (int)found.size()) { FoundHost h = found[i]; openForm(nullptr, &h, p); }
				else if (r[i].id == R_SEARCH) enterFolder(FINDNET, p);
				else if (r[i].id == R_TYPE_ADDRESS) openForm(nullptr, nullptr, p);
				return;
			}
			if (r[i].id == 4 || r[i].id == 6)
			{
				// a connection (its start folder), or Try again
				std::string path = browsePaths[i];
				NetPath np;
				if (r[i].id == 4 && netStore && parseNetPath(path, np)) netStore->touch(np.server);
				enterFolder(path, p);
				return;
			}
			std::string path = browsePaths[i];	// a copy: entering a folder rebuilds the list
			// the folder last gone into, or a video opened from, is the one kept (Remember last folder): backing out
			// to the first page to quit leaves it as it was
			if (r[i].id == 1)
			{
				enterFolder(path, p);
				if (path != RECENT) p.setLastFolder(path);
			}
			else
			{
				if (browseDir != RECENT) p.setLastFolder(browseDir);
				openFile(path, p);
			}
			return;
		}
		if (r[i].id == STYLE_ROW)
		{
			openMainMenu(p, TAB_SETTINGS);
			openGroup(GROUP_SUBS);
			styleFromSubs = true;
			return;
		}
		if (page == Page::UpNext)
		{
			playUpNext(i, p);
			return;
		}
		if (page == Page::Chapters)
		{
			// from there: playing (a paused one too), the card says where, back to the player
			int c = (int)r[i].id;
			p.seekChapter(c);
			if (!pageIsOut()) p.unpause();
			else if (c >= 0 && c < (int)p.chapters.size()) noteSeek(p, p.chapters[c].time);	// (a page playing it: the session's Seek, which the page follows)
			p.showChapterCard(c);
			page = Page::Main;
			return;
		}
		if (r[i].id < 0) return;
		if (tracksTab == TRACKS_PICTURE)
		{
			p.setAspect((AspectMode)std::clamp((int)r[i].id, 0, 2));
			p.osd(std::string("Aspect: ") + aspectName(p.aspect));
			page = Page::Main;
			return;
		}
		chooseTrack(p, trackType(), r[i].id);
		page = Page::Main;
	}

	bool Ui::chooseTrack(Player& p, TrackType type, int64_t id)
	{
		// the label as the page's row has it ("Off" first among the subtitles)
		std::string label;
		if (type == TrackType::Sub && id == 0) label = "Off";
		auto list = p.tracksOf(type);
		for (size_t i = 0; i < list.size() && label.empty(); i++)
			if (list[i].id == id) label = trackLabel(list[i], (int)i + 1);
		if (label.empty()) return false;
		p.selectTrack(type, id);
		SessionHost::get().handheldPicked(type, id);
		p.osd(std::string(type == TrackType::Audio ? "Audio: " : "Subtitles: ") + label);
		return true;
	}

	void Ui::openMainMenu(Player& p, int tab)
	{
		if (!inMenu)
		{
			menuReturnDir = browseDir;
			menuReturnCursor = cursor;
			menuReturnScroll = scroll;
			menuReturnPage = page == Page::Extensions ? page : Page::Browse;
		}
		inMenu = true;
		showTab(tab, p);
	}

	void Ui::showTab(int tab, Player& p)
	{
		menuTab = tab;
		styleFromSubs = false;
		cursor = scroll = 0;
		switch (tab)
		{
		case TAB_SETTINGS: page = Page::Settings; setGroup = -1; break;
		case TAB_ABOUT: openAbout(p); break;
		case TAB_PLAYING: page = Page::Playing; break;
		default: page = Page::Exit; break;
		}
	}

	void Ui::closeMenu(Player& p)
	{
		inMenu = false;
		playOnOpen = false;
		// (over what plays behind the browser: back to the browser)
		if (playerShown(p)) { page = Page::Main; return; }
		// Manage extensions, as it was
		if (menuReturnPage != Page::Browse)
		{
			page = menuReturnPage;
			menuReturnPage = Page::Browse;
			cursor = menuReturnCursor;
			scroll = menuReturnScroll;
			return;
		}
		// the browser, back in its folder (and on its row), listed again (a setting can change what it lists)
		page = Page::Browse;
		cursor = menuReturnCursor;
		scroll = menuReturnScroll;
		enterFolder(menuReturnDir, p);
	}

	std::vector<int> Ui::quickRows() const
	{
		std::vector<int> r;
		if (volume.present()) r.push_back(Q_VOLUME);
		bool top = backlight.present(Backlight::TOP), bottom = backlight.present(Backlight::BOTTOM);
		if (top || bottom) r.push_back(Q_BRIGHT);
		if (top && bottom && !sameBrightness) r.push_back(Q_BOTTOM);
		if (top && bottom) r.push_back(Q_SAME);
#ifdef BP_WEB
		r.push_back(Q_WEB);
#endif
		r.push_back(Q_INCOGNITO);
		r.push_back(Q_QUIT);
		return r;
	}

	void Ui::toggleQuick(Player& p)
	{
		quickOpen = !quickOpen;
		if (!quickOpen) return;
		if (!panel.on()) panel.set(true);	// the bottom panel back, to see them
		sameBrightness = p.settings.sameBrightness;
		volumePct = volume.get();
		quickCursor = 0;
		quickSub = 0;
		quickDrag = false;
		pressedBtn = -1;
	}

	void Ui::quickAdjust(int row, int dir, Player& p)
	{
		// Web dashboard's row: LEFT the switch, RIGHT its Settings button (while it's on)
		if (row == Q_WEB)
		{
			quickSub = dir > 0 && p.settings.webDashboard ? 1 : 0;
			return;
		}
		if (row == Q_VOLUME && volumePct >= 0)
		{
			// bananad's keys' step
			volumePct = std::clamp((volumePct + (dir > 0 ? 5 : -1)) / 5 * 5, 0, 100);	// to the next five either way
			volume.set(volumePct);
		}
		else if (row == Q_BRIGHT || row == Q_BOTTOM)
		{
			Backlight::Panel panelOf = row == Q_BOTTOM ? Backlight::BOTTOM : Backlight::TOP;
			int pm = backlight.permille(panelOf);
			if (pm >= 0) setBrightness(panelOf, stepBrightness(pm, dir), p);
		}
	}

	void Ui::quickSetAt(int row, int x, Player& p)
	{
		Rect bar = quickBar(row);
		double f = std::clamp((x - bar.x) / (double)bar.w, 0.0, 1.0);
		if (rowsIndexOf(quickRows(), Q_VOLUME) == row && volumePct >= 0)
		{
			int v = (int)std::lround(f * 20) * 5;
			if (v != volumePct)
			{
				volumePct = v;
				volume.set(v);
			}
			return;
		}
		int which = quickRows()[row];
		Backlight::Panel panelOf = which == Q_BOTTOM ? Backlight::BOTTOM : Backlight::TOP;
		int pm = std::clamp((int)std::lround(f * 10) * 100, 100, 1000);
		if (pm != backlight.permille(panelOf)) setBrightness(panelOf, pm, p);
	}

	void Ui::quickChoose(int row, Player& p)
	{
		if (row == Q_SAME)
		{
			Settings s = p.settings;
			s.sameBrightness = !s.sameBrightness;
			p.setSettings(s);
			sameBrightness = s.sameBrightness;
			// one level for both: the bottom to the top's
			int pm = backlight.permille(Backlight::TOP);
			if (sameBrightness && pm >= 100) backlight.set(Backlight::BOTTOM, pm);
		}
		else if (row == Q_WEB)
		{
			// its Settings button: the Web dashboard page (the QR code, the PIN, New PIN), the quick settings put away
			if (quickSub == 1 && p.settings.webDashboard) { quickOpen = false; openWeb(); }
			else webTurn(!p.settings.webDashboard, p);
		}
		else if (row == Q_INCOGNITO)
		{
			// the Ui's and the player's, never a setting saved: off at every start
			incognito = !incognito;
			p.incognito = incognito;
			fprintf(stderr, "incognito mode %s\n", incognito ? "on" : "off");
		}
		else if (row == Q_QUIT) quitRequested = true;
	}

	bool Ui::quickKey(Button b, Player& p)
	{
		auto rows = quickRows();
		int n = (int)rows.size();
		quickCursor = std::clamp(quickCursor, 0, n - 1);
		int row = rows[quickCursor];
		switch (b)
		{
		case Button::Up: quickCursor = (quickCursor + n - 1) % n; break;
		case Button::Down: quickCursor = (quickCursor + 1) % n; break;
		case Button::Left: case Button::Right: quickAdjust(row, b == Button::Right ? 1 : -1, p); break;
		case Button::A: quickChoose(row, p); break;
		case Button::B: case Button::Start: quickOpen = false; break;
		default: break;
		}
		return false;
	}

	bool Ui::quickPointer(const Pointer& pt, int x, int y, Player& p)
	{
		auto rows = quickRows();
		auto barRow = [&](int i) { return rows[i] == Q_VOLUME || rows[i] == Q_BRIGHT || rows[i] == Q_BOTTOM; };
		if (pt.kind == Pointer::Down)
		{
			pressedBtn = -1;
			// outside: closed (the finger lifting presses nothing)
			if (!quickPanel().hit(x, y))
			{
				quickOpen = false;
				swallowTouch = true;
				return false;
			}
			for (int i = 0; i < (int)rows.size(); i++)
			{
				if (!quickRowRect(i).hit(x, y)) continue;
				quickCursor = i;
				if (barRow(i) && quickMinus(i).hit(x, y)) pressedBtn = 300 + i * 4;
				else if (barRow(i) && quickPlus(i).hit(x, y)) pressedBtn = 301 + i * 4;
				else if (rows[i] == Q_WEB && p.settings.webDashboard && quickPinBtn(i).hit(x, y)) pressedBtn = 303 + i * 4;
				else if (barRow(i) && Rect{quickBar(i).x - 6, quickRowRect(i).y, quickBar(i).w + 12, 54}.hit(x, y))
				{
					// the bar, as the progress bar: at once, and while the finger drags
					quickDrag = true;
					quickDragRow = i;
					quickSetAt(i, x, p);
				}
				else if (!barRow(i)) pressedBtn = 302 + i * 4;
			}
			return false;
		}
		if (quickDrag)
		{
			quickSetAt(quickDragRow, x, p);
			if (pt.kind == Pointer::Up) quickDrag = false;
			return false;
		}
		if (pt.kind == Pointer::Up && pressedBtn >= 300)
		{
			int i = (pressedBtn - 300) / 4, what = (pressedBtn - 300) % 4;
			pressedBtn = -1;
			if (i >= (int)rows.size() || !quickRowRect(i).hit(x, y)) return false;
			if (what == 0) quickAdjust(rows[i], -1, p);
			else if (what == 1) quickAdjust(rows[i], 1, p);
			else
			{
				// Web dashboard's: its Settings button (3), or the rest of the row, its switch (2)
				if (rows[i] == Q_WEB) quickSub = what == 3 ? 1 : 0;
				quickChoose(rows[i], p);
			}
		}
		return false;
	}

	void Ui::drawQuick(const Player& p)
	{
		Rect pr = quickPanel();
		drawDialog(pr, "");
		drawTextMid("Quick settings", 1, ACCENT, pr.x + 24, pr.y + 28);
		auto rows = quickRows();
		for (int i = 0; i < (int)rows.size(); i++)
		{
			Rect r = quickRowRect(i);
			bool at = i == quickCursor;
			int row = rows[i], mid = r.y + r.h / 2;
			if (row == Q_QUIT)
			{
				bool down = pressedBtn == 302 + i * 4;
				drawBox(r, down ? PRESSED : DANGER, R_BUTTON, at);
				SDL_Color ink = down ? TEXT : ON_ACCENT;	// black on the red: cream reads 2.4:1 there
				int tw = (int)std::lround(textWidth("Quit Banana Player", 1) / sx), gw = glyphW(G_POWER), x = r.x + r.w / 2 - (tw + gw + 12) / 2;
				drawGlyphMid(G_POWER, x + gw / 2, mid, ink);
				drawTextMid("Quit Banana Player", 1, ink, x + gw + 12, mid);
				continue;
			}
			if (at) drawFocus(r);
			if (row == Q_SAME)
			{
				// a tick box
				Rect box{r.x + 16, mid - 13, 26, 26};
				fillRound(box, at ? ACCENT : TEXT, R_FIELD);
				fillRound({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, at ? BUTTON : SURFACE, R_FIELD - 2);
				if (p.settings.sameBrightness) drawGlyphMid(G_CHECK, box.x + box.w / 2, box.y + box.h / 2, ACCENT);
				drawTextMid("Same brightness for both screens", 1, at ? ACCENT : TEXT, r.x + 56, mid, r.w - 66);
				continue;
			}
			if (row == Q_INCOGNITO)
			{
				// a tick box, and what it keeps out of the history
				Rect box{r.x + 16, mid - 13, 26, 26};
				fillRound(box, at ? ACCENT : TEXT, R_FIELD);
				fillRound({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, at ? BUTTON : SURFACE, R_FIELD - 2);
				if (incognito) drawGlyphMid(G_CHECK, box.x + box.w / 2, box.y + box.h / 2, ACCENT);
				drawTextMid("Incognito mode", 1, at ? ACCENT : TEXT, r.x + 56, mid, 300);
				drawGlyphMid(G_INCOGNITO, r.x + r.w - 24, mid, incognito ? ACCENT : (at ? DIM_ON : DIM));
				continue;
			}
			if (row == Q_WEB)
			{
				// a tick box (on or off), its name, and while it's on a button to its page in Settings
				bool on = p.settings.webDashboard, onSwitch = at && (quickSub == 0 || !on);
				Rect box{r.x + 16, mid - 13, 26, 26};
				fillRound(box, onSwitch ? ACCENT : TEXT, R_FIELD);
				fillRound({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, at ? BUTTON : SURFACE, R_FIELD - 2);
				if (on) drawGlyphMid(G_CHECK, box.x + box.w / 2, box.y + box.h / 2, ACCENT);
				drawTextMid("Web dashboard", 1, onSwitch ? ACCENT : TEXT, r.x + 56, mid, 200);
				if (on)
				{
					Rect b = quickPinBtn(i);
					drawBox(b, pressedBtn == 303 + i * 4 ? PRESSED : at ? BG : BUTTON, R_BUTTON, at && quickSub == 1);
					drawButton({b.x, b.y, b.w - 20, b.h}, nullptr, "Settings", nullptr, false, 1);	// (centred in what the arrow leaves)
					drawGlyphMid(G_RIGHT, b.x + b.w - 18, b.y + b.h / 2, TEXT);
				}
				else drawTextMid("Off", 0, at ? DIM_ON : DIM, r.x + r.w - 16, mid, 0, 1);
				continue;
			}
			int pct;
			std::string label;
			if (row == Q_VOLUME)
			{
				pct = volumePct;
				label = "Volume";
			}
			else
			{
				int pm = backlight.permille(row == Q_BOTTOM ? Backlight::BOTTOM : Backlight::TOP);
				pct = pm < 0 ? -1 : (pm + 5) / 10;
				label = row == Q_BOTTOM ? "Bottom" : p.settings.sameBrightness ? "Brightness" : "Top";
			}
			SDL_Color ink = at ? ACCENT : TEXT;
			drawGlyphMid(row == Q_VOLUME ? G_SPEAKER : G_SUN, r.x + 25, mid, ink);
			drawTextMid(label, 1, ink, r.x + 46, mid, 146);
			drawTextMid(pct < 0 ? "-" : std::to_string(pct) + "%", 0, at ? DIM_ON : DIM, quickMinus(i).x - 6, mid, 0, 1);
			for (int k = 0; k < 2; k++)
			{
				Rect bt = k ? quickPlus(i) : quickMinus(i);
				drawBox(bt, pressedBtn == 300 + i * 4 + k ? PRESSED : at ? BG : BUTTON, R_BUTTON);
				drawGlyphMid(k ? G_PLUS : G_MINUS, bt.x + bt.w / 2, bt.y + bt.h / 2, TEXT);
			}
			Rect bar = quickBar(i);
			drawLevel(bar, pct > 0 ? bar.w * std::min(pct, 100) / 100 : 0, at ? BG : TRACK);
		}
		if (p.settings.buttonHotkeys) drawHints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"F/B", "Close"}});
		else drawFooterText("Tap outside to close", DIM);
	}

	bool Ui::popupShown(const Player& p) const
	{
		if (!updater || popupClosed || page != Page::Browse || inMenu || ctxOpen) return false;
		Updater::State st = updater->state();
		// a release found: unless "Not until tomorrow" holds; after "Update now", what became of it
		if (st == Updater::State::Available) return time(nullptr) >= (time_t)p.settings.updateRemindAfter;
		return popupFollow && (st == Updater::State::Downloading || st == Updater::State::Ready || st == Updater::State::Failed);
	}

	std::vector<const char*> Ui::popupItems() const
	{
		switch (updater->state())
		{
		case Updater::State::Available: return {"Update now", "Later", "Not until tomorrow"};
		case Updater::State::Downloading: return {"Hide"};
		case Updater::State::Ready: return {"Restart now", "Later"};
		default: return {"OK"};
		}
	}

	void Ui::choosePopup(int item, Player& p)
	{
		Updater::State st = updater->state();
		if (st == Updater::State::Available && item == 0)
		{
			updater->download();
			popupFollow = true;
			popupCursor = 0;
			return;
		}
		if (st == Updater::State::Available && item == 2)
		{
			Settings s = p.settings;
			s.updateRemindAfter = (long long)nextMidnight(time(nullptr));
			p.setSettings(s);
		}
		if (st == Updater::State::Ready && item == 0)
		{
			restartRequested = true;
			return;
		}
		// Later, Hide, OK, and Not until tomorrow: closed for this time (a download goes on; a ready update is put in
		// place at the next start)
		popupClosed = true;
		popupCursor = 0;
	}

	void Ui::drawUpdatePopup()
	{
		auto items = popupItems();
		int top = 150, bottom = popupRow((int)items.size() - 1).y + 60;
		drawDialog({112, top, 416, bottom - top}, "");
		std::string title, sub;
		switch (updater->state())
		{
		case Updater::State::Available:
			title = "Banana Player " + updater->available() + " is out";
			sub = "You have " + updater->current();
			break;
		case Updater::State::Downloading:
		{
			int pct = updater->progress();
			title = "Downloading " + updater->available() + "...";
			sub = pct >= 0 ? std::to_string(pct) + "%" : "Starting";
			break;
		}
		case Updater::State::Ready:
			title = updater->available() + " is ready";
			sub = "Restart now, or it's in place next time";
			break;
		default:
			title = "The update didn't work";
			sub = updater->message();
			break;
		}
		drawTextMid(title, 1, ACCENT, W / 2, top + 30, 392, 2);
		drawTextMid(sub, 0, TEXT, W / 2, top + 62, 392, 2);
		if (updater->state() == Updater::State::Downloading)
		{
			// the bar
			int pct = std::max(0, updater->progress());
			drawLevel({156, top + 84, 328, 8}, 328 * pct / 100, TRACK);
		}
		for (int i = 0; i < (int)items.size(); i++)
		{
			Rect r = popupRow(i);
			bool at = i == popupCursor;
			if (pressedBtn == 200 + i) drawBox(r, PRESSED, R_BUTTON);
			else if (at) drawFocus(r);
			drawTextMid(items[i], 1, at ? ACCENT : TEXT, W / 2, r.y + r.h / 2, r.w - 10, 2);
		}
		drawHints({{"A", "Choose"}, {"B", items.size() > 1 ? "Later" : "Close"}});
	}

	void Ui::drawBrowseMenu(const Player& p)
	{
		Rect box = ctxBox();
		// its title: the connection, the folder Hide is for, else the place it's over
		const NetServer* sv = netStore && !ctxServer.empty() ? netStore->find(ctxServer) : nullptr;
		std::string name = !ctxTitle.empty() ? ctxTitle : sv ? serverName(*sv) : !ctxPath.empty() ? baseName(ctxPath)
			: browseDir.empty() ? std::string("Your cards") : browseDir == NETROOT ? std::string("Your connections")
			: browseDir == RECENT ? std::string("Recent")
			: isNetPath(browseDir) && netBaseName(browseDir).empty() ? std::string("Shares")
			: isNetPath(browseDir) ? netBaseName(browseDir) : baseName(browseDir);
		drawDialog(box, name);
		// a playlist's Delete, pressed once: what it doesn't do
		bool plArmed = ctxArmed && ctxCursor < (int)ctxItems.size() && ctxItems[ctxCursor] == CTX_PL_DELETE;
		if (ctxExtra() && (!ctxNote.empty() || plArmed))
			drawTextMid(plArmed ? "Your files stay where they are" : ctxNote, 0, plArmed ? ACCENT : DIM, W / 2, box.y + 60, 368, 2);
		else if (ctxExtra())
			drawTextMid(ctxStatus.empty() ? std::string(sv ? std::string(netTypeLabel(sv->type)) + "  " + sv->host : "") : ctxStatus, 0,
				ctxStatus == "It works" ? ACCENT : DIM, W / 2, box.y + 60, 368, 2);
		for (int i = 0; i < (int)ctxItems.size(); i++)
		{
			int it = ctxItems[i];
			bool del = it == CTX_DELETE || it == CTX_PL_DELETE;
			bool armed = del && ctxArmed && i == ctxCursor;
			const RecentItem* ri = nullptr;
			for (auto& x : recent)
				if (x.path == ctxRecent) ri = &x;
			std::string note;
			SDL_Color noteInk = i == ctxCursor ? DIM_ON : DIM;
			if (it == CTX_R_SHOW && ri) note = ri->where;
			if (it == CTX_HIDDEN) note = showHidden ? "On" : "Off";
			if (it == CTX_CLEAR_RECENT)
			{
				bool armedClear = ctxArmed && i == ctxCursor;
				note = recent.empty() ? "Empty" : armedClear ? "Press again to clear" : "Press twice";
				if (armedClear) noteInk = ACCENT;
			}
			std::string resume = ri && !ri->at.empty() ? "Resume from " + ri->at : "Play";
			bool likedR = it == CTX_R_LIKE && plStore && plStore->isLiked(ctxRecent);
			bool inLiked = plIsLiked(ctxPlFile);	// Liked's Remove is Unlike
			const char* label = it == CTX_R_PLAY ? resume.c_str() : it == CTX_R_START ? "Play from the start"
				: it == CTX_R_SHOW ? "Show in folder" : it == CTX_R_ADD || it == CTX_PL_ADD_ONE ? "Add to playlist\xE2\x80\xA6"
				: it == CTX_R_LIKE ? (likedR ? "Unlike" : "Like") : it == CTX_R_REMOVE ? "Remove from Recent"
				: it == CTX_CLEAR_RECENT ? "Clear Recent" : it == CTX_HIDDEN ? "Show hidden folders" : it == CTX_SETTINGS ? "Settings"
				: it == CTX_HIDE ? (isHiddenFolder(ctxPath) ? "Unhide folder" : "Hide folder")
				: it == CTX_REFRESH ? "Refresh" : it == CTX_ADD ? "Add connection" : it == CTX_FIND ? "Find on the network"
				: it == CTX_EDIT ? "Edit connection" : it == CTX_TEST ? "Test connection"
				: armed ? "Press again to delete" : it == CTX_DELETE ? "Delete connection" : it == CTX_PL_DELETE ? "Delete playlist"
				: it == CTX_PL_NEW ? "New playlist\xE2\x80\xA6" : it == CTX_PL_PLAY ? (isPlDir(browseDir) ? "Play all" : "Play")
				: it == CTX_PL_SHUFFLE ? "Shuffle play" : it == CTX_PL_RENAME ? "Rename\xE2\x80\xA6" : it == CTX_PL_DEDUP ? "Remove duplicates"
				: it == CTX_PL_HERE ? "Play from here" : it == CTX_PL_REMOVE ? (inLiked ? "Unlike" : "Remove from playlist") : it == CTX_PL_SHOW ? "Show in folder"
				: it == CTX_PL_ADD_FILES ? "Add files\xE2\x80\xA6" : it == CTX_PL_ADD_FOLDER ? "Add folder\xE2\x80\xA6"
				: it == CTX_PL_SELECT ? "Select" : it == CTX_PL_MOVE ? "Move" : it == CTX_SELECT_FILES ? "Select files"
				: it == CTX_ADD_TO ? "Add to playlist\xE2\x80\xA6" : it == CTX_WEB ? "Web dashboard\xE2\x80\xA6" : "Cancel";
			const Glyph* g = it == CTX_R_PLAY ? &G_PLAY : it == CTX_R_START ? &G_REFRESH : it == CTX_R_SHOW ? &G_FOLDER
				: it == CTX_R_ADD || it == CTX_PL_ADD_ONE ? &G_PL_ADD : it == CTX_R_LIKE ? (likedR ? &G_HEART_O : &G_HEART)
				: it == CTX_PL_REMOVE && inLiked ? &G_HEART_O : it == CTX_R_REMOVE ? &G_MINUS : it == CTX_CLEAR_RECENT ? &G_TRASH
				: it == CTX_HIDDEN ? &G_EYE : it == CTX_SETTINGS ? &G_GEAR
				: it == CTX_HIDE ? &G_EYE : it == CTX_REFRESH ? &G_REFRESH : it == CTX_ADD ? &G_PLUS : it == CTX_FIND ? &G_SEARCH
				: it == CTX_EDIT ? &G_PENCIL : it == CTX_TEST ? &G_PLUG : del ? &G_TRASH
				: it == CTX_PL_NEW ? &G_PLUS : it == CTX_PL_PLAY || it == CTX_PL_HERE ? &G_PLAY : it == CTX_PL_SHUFFLE ? &G_SHUFFLE
				: it == CTX_PL_RENAME ? &G_PENCIL : it == CTX_PL_DEDUP || it == CTX_PL_REMOVE ? &G_MINUS : it == CTX_PL_SHOW ? &G_FOLDER
				: it == CTX_PL_ADD_FILES ? &G_PLUS : it == CTX_PL_ADD_FOLDER ? &G_FOLDER : it == CTX_PL_SELECT || it == CTX_SELECT_FILES ? &G_BOX_ON
				: it == CTX_PL_MOVE ? &G_MOVE : it == CTX_ADD_TO ? &G_PLAYLIST : it == CTX_WEB ? &G_WEB : &G_CROSS;
			// the mini bar's: what plays (or was stopped for a photo); main's player, also over a photo
			if (it >= CTX_NP_NOW)
			{
				const Player& np = barPlayer(p);
				bool live = np.playing() && !np.playingPhoto();
				bool on = live && (pageDrives(np) ? pagePlaying() : !np.paused);
				bool liked = live && likedNow(np);
				std::string where = np.currentPath();
				where = isNetPath(where) ? netBaseName(netParent(where)) : baseName(parentFolder(where));
				std::string npLabel = it == CTX_NP_NOW ? "Now playing" : it == CTX_NP_PLAY ? (on ? "Pause" : "Play")
					: it == CTX_NP_LIKE ? (liked ? "Unlike" : "Like") : it == CTX_NP_ADD ? "Add to playlist\xE2\x80\xA6"
					: it == CTX_NP_SHOW ? "Show in folder" : "Stop";
				const Glyph* ng = it == CTX_NP_NOW ? &G_RIGHT : it == CTX_NP_PLAY ? (on ? &G_PAUSE : &G_PLAY)
					: it == CTX_NP_LIKE ? (liked ? &G_HEART_O : &G_HEART) : it == CTX_NP_ADD ? &G_PL_ADD
					: it == CTX_NP_SHOW ? &G_FOLDER : &G_STOP;
				std::string npNote = it == CTX_NP_SHOW ? where : it == CTX_NP_NOW && np.settings.buttonHotkeys ? "SELECT" : "";
				drawDialogRow(ctxRow(i), ng, npLabel, i == ctxCursor, pressedBtn == 100 + i,
					it == CTX_NP_STOP ? DANGER : it == CTX_NP_NOW ? ACCENT : TEXT, npNote, noteInk);
				continue;
			}
			drawDialogRow(ctxRow(i), g, label, i == ctxCursor, pressedBtn == 100 + i, del ? (armed ? ACCENT : DANGER) : it == CTX_CANCEL ? DIM
				: it == CTX_R_LIKE && !likedR ? ACCENT : TEXT, note, noteInk);
		}
		// a menu too long for its line (a playlist's): Delete's word on the footer instead
		if (plArmed && !ctxExtra()) drawFooterText("Your files stay where they are", ACCENT);
		else drawHints({{"A", "Choose"}, {"B", "Close"}});
	}

	void Ui::drawExit(const Player&)
	{
		drawTextMid("Quit Banana Player?", 2, TEXT, W / 2, 132, 0, 2);
		drawTextMid("Your place in what you were playing is kept.", 0, DIM, W / 2, 170, 0, 2);
		Rect b = buttonRect(EXIT_BTN);
		// it's the page's one choice: always the one A takes (a yellow edge; black on the red, cream reads 2.4:1 there)
		bool down = pressedBtn == EXIT_BTN;
		drawBox(b, down ? PRESSED : DANGER, R_BUTTON, true);
		SDL_Color quitInk = down ? TEXT : ON_ACCENT;
		int tw = (int)std::lround(textWidth("Quit Banana Player", 1) / sx), gw = glyphW(G_POWER);
		int x = b.x + b.w / 2 - (tw + gw + 12) / 2;
		drawGlyphMid(G_POWER, x + gw / 2, b.y + b.h / 2, quitInk);
		drawTextMid("Quit Banana Player", 1, quitInk, x + gw + 12, b.y + b.h / 2);
		drawHints({{"A", "Quit"}, {"B", "Close"}, {"L/R", "Tabs"}});
	}

	std::vector<int> Ui::menuTabs(const Player& p) const
	{
		// no Quit tab: quick settings (F) has Quit, and F + START quits from anywhere
		if (playingTabShown(p)) return {TAB_PLAYING, TAB_SETTINGS, TAB_ABOUT};
		return {TAB_SETTINGS, TAB_ABOUT};
	}

	bool Ui::playingTabShown(const Player& p) const { return p.playing() && !p.playingPhoto(); }

	Ui::Rect Ui::menuTabRect(int i, int n) const
	{
		if (n != 4) return tabRect(i);
		// four, as wide as their names need
		static const int WS[4] = {112, 120, 100, 84};
		int x = 72;
		for (int k = 0; k < i; k++) x += WS[k] + 8;
		return {x, 10, WS[i], 44};
	}

	Ui::RowIcon Ui::rowIcon(int i) const
	{
		if (i < 0 || i >= (int)browseRows.size() || i >= (int)browsePaths.size()) return ICON_NONE;
		const std::string& path = browsePaths[i];
		// the form's actions (its ids are its own: they meet the found servers')
		if (browseDir == FORMDIR) return browseRows[i].id == FORM_TEST ? ICON_TEST : browseRows[i].id == FORM_SAVE ? ICON_SAVE : ICON_NONE;
		if (browseDir == FINDNET)
			return browseRows[i].id == R_FOUND ? ICON_NETWORK : browseRows[i].id == R_SEARCH ? ICON_REFRESH
				: browseRows[i].id == R_TYPE_ADDRESS ? ICON_TYPE : ICON_NONE;
		switch (browseRows[i].id)
		{
		case PL_ROW: return plIsLiked(path) ? ICON_LIKED : ICON_PLAYLIST;
		case PL_NEW: return ICON_NEW;
		case 4: return ICON_NETWORK;
		case 6: return ICON_REFRESH;
		case 1:
			if (path == RECENT) return ICON_RECENT;
			return browseDir.empty() ? ICON_PLACE : ICON_FOLDER;	// the first page's: the places (this card...)
		case 2: return isPhotoFile(path) ? ICON_PHOTO : isAudioFile(path) ? ICON_SONG : ICON_VIDEO;
		default: return ICON_NONE;	// messages, Clear, Try again
		}
	}

	void Ui::drawChapters(const Player& p)
	{
		int n = (int)p.chapters.size();
		if (n == 0)
		{
			// (the list emptied under it: the next file has none)
			drawHeader("Chapters", p);
			drawEmpty(G_CHAPTERS, "No chapters in this file", "When a file has chapters, L2 and R2 jump between them", nullptr);
			drawHints({{"B", "Back"}});
			return;
		}
		drawTracksHeader(p);
		int cur = shownChapter(p), at = std::min(cursor, n - 1);
		for (int i = scroll; i < n && i < scroll + ROWS_SHOWN; i++)
		{
			int y = ROW_Y + (i - scroll) * ROW_H, mid = y + 22;
			bool focus = i == at, now = i == cur;
			if (focus) drawFocus({16, y, 608, ROW_H - 4});
			SDL_Color soft = focus ? DIM_ON : DIM;
			// the one playing: its mark, as the status line has it
			if (now) drawGlyphMid(p.paused ? G_PAUSE : G_PLAY, 40, mid, ACCENT);
			drawTextMid(std::to_string(i + 1), 0, now ? ACCENT : soft, 90, mid, 0, 1);
			const Chapter& c = p.chapters[i];
			std::string start = formatTime(c.time);
			drawTextMid(start, 0, soft, 608, mid, 0, 1);
			int timeW = (int)std::lround(textWidth(start, 0) / sx);
			int maxW = 608 - 16 - timeW - 104;
			if (chapterUntitled(c.title)) drawTextMid(chapterLabel(c, i + 1), 1, soft, 104, mid, maxW);
			else drawTextMid(c.title, 1, focus ? ACCENT : TEXT, 104, mid, maxW);
			if (now)
			{
				// how far into it: up to the next one's start, or the file's end
				double end = i + 1 < n ? p.chapters[i + 1].time : p.duration;
				double at = shownPosition(p);
				double f = end > c.time && at >= 0 ? std::clamp((at - c.time) / (end - c.time), 0.0, 1.0) : 0;
				drawLevel({104, y + 37, 504, 3}, (int)(504 * f), focus ? BG : TRACK);
			}
		}
		drawScrollBar(ROW_Y, ROWS_SHOWN * ROW_H - 4, scroll, ROWS_SHOWN, n);
		if (tracksTabs(p).size() > 1) drawHints({{"A", "Play from here"}, {"L2/R2", "Prev/Next"}, {"R", "Subtitles"}, {"B", "Back"}});
		else drawHints({{"A", "Play from here"}, {"L2/R2", "Prev/Next"}, {"B", "Back"}});
	}

	// ---- Up next: the list that plays ----

	std::vector<std::string> Ui::upNextList(const Player& p, int& current) const
	{
		current = -1;
		if (!p.playing() || p.playingPhoto()) return {};
		// the Session's queue when it has this file (in the list's own order: Shuffle's is hidden), else the player's own
		const Session& s = SessionHost::get().session();
		if (s.index >= 0 && s.index < (int)s.queue.size() && s.queue[s.index] == p.currentPath())
		{
			current = s.index;
			return s.queue;
		}
		current = p.index();
		return p.paths();
	}

	void Ui::openUpNext(const Player& p)
	{
		page = Page::UpNext;
		// on the one playing, three rows above it showing where the list allows (what came before it, a little)
		int cur = -1;
		int n = (int)upNextList(p, cur).size();
		cursor = std::clamp(cur, 0, std::max(0, n - 1));
		scroll = std::clamp(cursor - 3, 0, std::max(0, n - ROWS_SHOWN));
	}

	void Ui::playUpNext(int i, Player& p)
	{
		int cur = -1;
		auto list = upNextList(p, cur);
		if (i < 0 || i >= (int)list.size()) return;
		page = Page::Main;
		if (i == cur) return;	// (the one playing: back to it, never from its start)
		// as a pick from that list: the same list from that row, where it came from (the Session's Open: the output, Shuffle
		// and Loop stay as they are; with Shuffle on, a new order from that row)
		playRequest = {};
		playRequest.paths = list;
		playRequest.index = i;
		if (list == p.paths()) playRequest.source = p.source();
		else
		{
			const Session& s = SessionHost::get().session();
			playRequest.source.kind = s.source.kind == 2 ? SourceKind::Playlist : s.source.kind == 1 ? SourceKind::NetFolder : SourceKind::Folder;
			playRequest.source.title = s.source.title;
		}
		if (isNetPath(list[i])) playRequest.names = netFolderNames(netParent(list[i]));
		fprintf(stderr, "ui: up next, row %d of %d (%s)\n", i + 1, (int)list.size(), entryTitle(list[i]).c_str());
	}

	void Ui::drawUpNext(const Player& p)
	{
		drawHeader("Up next", p);
		int cur = -1;
		auto list = upNextList(p, cur);
		int n = (int)list.size();
		if (n == 0)
		{
			drawEmpty(G_PLAYLIST, "Nothing is playing", "Play a folder or a playlist to fill it", nullptr);
			drawHints({{"B", "Back"}});
			return;
		}
		// where it comes from, and which of how many
		std::string from = p.source().title;
		int count = n;
		std::string place = std::to_string(cur + 1) + " / " + std::to_string(count);
		drawTextMid(place, 0, DIM, 616, SUB_Y, 0, 1);
		if (!from.empty()) drawTextMid("From " + from, 0, DIM, 16, SUB_Y, 600 - 24 - (int)std::lround(textWidth(place, 0) / sx));
		int at = std::min(cursor, n - 1);
		// the one playing: the chip the browser's row has (Now, Paused, Browser: a page plays it)
		int chip = pageDrives(p) ? 3 : p.paused ? 2 : 1;
		for (int i = scroll; i < n && i < scroll + ROWS_SHOWN; i++)
		{
			int y = ROW_Y + (i - scroll) * ROW_H, mid = y + 22;
			bool focus = i == at, now = i == cur, past = cur >= 0 && i < cur;
			if (focus) drawFocus({16, y, 608, ROW_H - 4});
			SDL_Color soft = focus ? DIM_ON : DIM;
			drawTextMid(std::to_string(i + 1), 0, now ? ACCENT : soft, 58, mid, 0, 1);
			drawGlyphMid(isAudioFile(list[i]) ? G_NOTE : G_FILM, 82, mid, now ? ACCENT : soft);
			// its length, where known (the one playing: mpv's; the others: the durations the lists probed)
			int right = 608;
			double d = now && p.duration > 0 ? p.duration : -1;
			if (d <= 0 && !isNetPath(list[i])) cachedDuration(list[i], d);
			if (d > 0)
			{
				std::string t = formatTime(d);
				drawTextMid(t, 0, soft, right, mid, 0, 1);
				right -= (int)std::lround(textWidth(t, 0) / sx) + 14;
			}
			if (now) right -= drawChip(right, mid, chip, true) + 10;
			drawTextMid(entryTitle(list[i]), 1, focus ? ACCENT : past ? soft : TEXT, 104, mid, std::max(40, right - 8 - 104));
		}
		drawScrollBar(ROW_Y, ROWS_SHOWN * ROW_H - 4, scroll, ROWS_SHOWN, n);
		drawHints({{"A", at == cur ? "Back to it" : "Play"}, {"B", "Back"}});
	}

	void Ui::readInfo(const Player& p)
	{
		setInfoLines(p.mediaInfo());
	}

	void Ui::openAbout(const Player& p)
	{
		SDL_version sdl;
		SDL_GetVersion(&sdl);
		char sdlVersion[32];
		snprintf(sdlVersion, sizeof sdlVersion, "%d.%d.%d", sdl.major, sdl.minor, sdl.patch);
		std::string mpv = p.property("mpv-version");
		if (mpv.rfind("mpv ", 0) == 0) mpv = mpv.substr(4);
		setInfoLines({
			{"Banana Player", "", true},
			{"Version", BP_VERSION},
			{"Made by", "tonywei92"},
			{"Source", "https://github.com/tonywei92/banana-player"},
			{"License", "MIT: copies and forks keep the copyright notice and the licence (LICENSE)"},
			{"Plays", "Video and music on BananaOS for the Anbernic RG DS and RG DS Plus"},
			{"Built with", "", true},
			{"mpv", mpv},
			{"FFmpeg", p.property("ffmpeg-version")},
			{"SDL", sdlVersion},
			{"Fonts", "", true},
			{"Pixelify Sans", "SIL Open Font License 1.1"},
			{"Inter", "SIL Open Font License 1.1"},
			{"Fusion Pixel", "SIL Open Font License 1.1"},
			{"Thanks", "", true},
			{"BananaOS", "https://github.com/JorreFog/BananaLinux"},
		});
		page = Page::About;
		scroll = 0;
	}

	void Ui::setInfoLines(const std::vector<Player::InfoRow>& rows)
	{
		infoLines.clear();
		int maxW = (int)((W - INFO_VALUE_X - 24) * sx);
		for (auto& r : rows)
		{
			if (r.header) { infoLines.push_back({r.label, "", true}); continue; }
			// a long value (a folder, a codec's full name) over a few lines: broken after a '/' or a space where it can
			std::string rest = r.value, label = r.label;
			for (int line = 0; !rest.empty() && line < 4; line++)
			{
				size_t fit = rest.size();
				int w = 0;
				TTF_SizeUTF8(fonts[0], rest.c_str(), &w, nullptr);
				if (w > maxW)
				{
					// the longest start that fits, whole UTF-8 characters
					size_t lo = 1;
					for (size_t i = 1; i <= rest.size(); i++)
					{
						if (i < rest.size() && ((unsigned char)rest[i] & 0xC0) == 0x80) continue;
						TTF_SizeUTF8(fonts[0], rest.substr(0, i).c_str(), &w, nullptr);
						if (w > maxW) break;
						lo = i;
					}
					fit = lo;
					size_t brk = rest.find_last_of("/ ", fit - 1);
					if (brk != std::string::npos && brk > fit / 2) fit = brk + 1;
				}
				infoLines.push_back({label, rest.substr(0, fit), false});
				rest = rest.substr(fit);
				label.clear();
			}
		}
		infoAt = SDL_GetTicks();
	}

	void Ui::drawInfo(const Player& p)
	{
		if (!inMenu) drawHeader("Info", p);
		for (int i = scroll; i < (int)infoLines.size() && i < scroll + INFO_SHOWN; i++)
		{
			int y = INFO_Y + (i - scroll) * INFO_H, mid = y + INFO_H / 2;
			const InfoLine& l = infoLines[i];
			if (l.header) drawTextMid(l.label, 1, ACCENT, 24, mid);
			else
			{
				drawTextMid(l.label, 0, DIM, 32, mid, INFO_VALUE_X - 40);
				drawTextMid(l.text, 0, TEXT, INFO_VALUE_X, mid);
			}
		}
		drawScrollBar(INFO_Y, INFO_SHOWN * INFO_H, scroll, INFO_SHOWN, (int)infoLines.size());
		if (inMenu) drawHints({{"UP/DOWN", "Scroll"}, {"B", "Close"}, {"L/R", "Tabs"}});
		else drawHints({{"UP/DOWN", "Scroll"}, {"A/B", "Back"}});
	}

	int Ui::volumeLevel()
	{
		// read once (the script runs): after that, kept as it changes
		if (volumePct == -1 && volume.present())
		{
			int v = volume.get();
			volumePct = v < 0 ? -2 : v;
		}
		return volumePct < 0 ? -1 : volumePct;
	}

	// ---- Like ----

	bool Ui::likeable(const Player& p) const { return plStore && p.playing() && PlaylistStore::likable(p.currentPath()); }

	bool Ui::likedNow(const Player& p) const { return plStore && p.playing() && plStore->isLiked(p.currentPath()); }

	bool Ui::headerActions(const Player& p) const
	{
		return plStore && p.playing() && !p.playingPhoto() && PlaylistStore::likable(p.currentPath());
	}

	void Ui::likeCard(Player& p, bool liked, const std::string& title)
	{
		// in the volume card's place: which file it was (a Like pressed as the song changes still says)
		if (liked) p.showNoteCard(G_HEART_ROWS, G_HEART.h, theme::ass(theme::ACCENT).c_str(), "Added to Liked", title);
		else p.showNoteCard(G_HEART_O_ROWS, G_HEART_O.h, theme::ass(theme::DIM).c_str(), "Removed from Liked", title);
	}

	void Ui::likeNow(Player& p)
	{
		if (!likeable(p) || p.playingPhoto()) return;
		bool was = likedNow(p);
		std::string title = !p.title.empty() ? p.title : entryTitle(p.currentPath());
		if (setLikedNow(p, !was) == was) return;	// (not saved: setLikedPath's card says so)
		likePopAt = SDL_GetTicks();
		likeCard(p, !was, title);
	}

	void Ui::openAddOne(const std::string& path, const std::string& title, double duration, Player& p)
	{
		if (!plStore || !PlaylistStore::likable(path)) return;	// (songs and videos: never a photo)
		pickerOne = path;
		pickerOneTitle = title.empty() ? entryTitle(path) : title;
		pickerOneDuration = duration;
		openPicker("", p, true);
	}

	// ---- START's Playing tab ----

	std::vector<int> Ui::playingRows() const
	{
		// behind the browser: Now playing first (back to the player); Play on while a page could play it; Stop last
		std::vector<int> r;
		if (bgPlay) r.push_back(PR_NOW);
		r.insert(r.end(), {PR_LIKE, PR_ADD});
		if (playOnShown()) r.push_back(PR_PLAYON);
		r.insert(r.end(), {PR_SHOW, PR_INFO, PR_STOP});
		return r;
	}

	// ---- Play on: where it plays (the Session's output), chosen here ----

	std::vector<Ui::OutChoice> Ui::outChoices() const
	{
		std::vector<OutChoice> out{{"", "Handheld"}};
#ifdef BP_WEB
		if (web && web->running())
			for (auto& t : web->playTabs()) out.push_back({t.id, t.name});
#endif
		// (the output's page gone a moment, in its grace: still there to pick, or to leave)
		const Session& s = SessionHost::get().session();
		if (s.out.kind == OutKind::Browser && std::none_of(out.begin(), out.end(), [&](const OutChoice& c) { return c.tab == s.out.tab; }))
			out.push_back({s.out.tab, pageName()});
		return out;
	}

	bool Ui::playOnShown() const { return outChoices().size() > 1; }

	std::string Ui::pageName() const
	{
		const Session& s = SessionHost::get().session();
#ifdef BP_WEB
		if (web && s.out.kind == OutKind::Browser) return web->tabName(s.out.tab);
#endif
		return "Browser";
	}

	std::string Ui::outputName() const { return SessionHost::get().pageIsOutput() ? pageName() : "Handheld"; }

	void Ui::openPlayOn()
	{
		playOnOpen = true;
		pressedBtn = -1;
		// on the first that isn't where it plays (the one wanted, most of the time)
		auto list = outChoices();
		const Session& s = SessionHost::get().session();
		std::string cur = SessionHost::get().pageIsOutput() ? s.out.tab : "";
		playOnCursor = 0;
		for (int i = 0; i < (int)list.size(); i++)
			if (list[i].tab != cur) { playOnCursor = i; break; }
		playOnScroll = std::max(0, playOnCursor - PLAYON_SHOWN + 1);
	}

	void Ui::playOnChoose(int i, Player& p)
	{
		auto list = outChoices();
		playOnOpen = false;
		pressedBtn = -1;
		if (i < 0 || i >= (int)list.size()) return;
		SessionHost& sh = SessionHost::get();
		std::string cur = sh.pageIsOutput() ? sh.session().out.tab : "";
		if (list[i].tab == cur) return;	// where it plays already: just closed
		fprintf(stderr, "ui: play on %s\n", list[i].tab.empty() ? "the handheld" : list[i].tab.c_str());
		sh.sendTo(list[i].tab.empty() ? handheldOut() : browserOut(list[i].tab));
		if (list[i].tab.empty()) p.osd(sh.session().playing ? "Playing on the handheld" : "On the handheld");
		// on the playing screen, where what it does next shows (the top panel names the page)
		closeMenu(p);
		showPlayer();
	}

	bool Ui::playOnKey(Button b, Player& p)
	{
		int n = (int)outChoices().size();
		playOnCursor = std::min(playOnCursor, n - 1);
		switch (b)
		{
		case Button::Up: playOnCursor = (playOnCursor + n - 1) % n; break;
		case Button::Down: playOnCursor = (playOnCursor + 1) % n; break;
		case Button::A: playOnChoose(playOnCursor, p); return true;
		case Button::B: case Button::Start: playOnOpen = false; return true;
		default: return true;
		}
		if (playOnCursor < playOnScroll) playOnScroll = playOnCursor;
		if (playOnCursor >= playOnScroll + PLAYON_SHOWN) playOnScroll = playOnCursor - PLAYON_SHOWN + 1;
		return true;
	}

	bool Ui::playOnPointer(const Pointer& pt, int x, int y, Player& p)
	{
		int n = (int)outChoices().size();
		if (pt.kind == Pointer::Down)
		{
			// outside the box: closed; a row: chosen as the finger lifts on it
			if (!playOnBox().hit(x, y)) { playOnOpen = false; swallowTouch = true; return false; }
			pressedBtn = -1;
			for (int i = playOnScroll; i < n && i < playOnScroll + PLAYON_SHOWN; i++)
				if (playOnRow(i).hit(x, y)) pressedBtn = PLAYON_ROW + i;
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			int id = pressedBtn;
			pressedBtn = -1;
			if (id >= PLAYON_ROW && id < PLAYON_ROW + n && playOnRow(id - PLAYON_ROW).hit(x, y))
			{
				playOnCursor = id - PLAYON_ROW;
				playOnChoose(playOnCursor, p);
			}
		}
		return false;
	}

	void Ui::drawPlayOn(const Player& p)
	{
		auto list = outChoices();
		int n = (int)list.size();
		playOnCursor = std::min(playOnCursor, n - 1);
		Rect box = playOnBox();
		drawDialog(box, "Play on");
		std::string title = !p.title.empty() ? p.title : entryTitle(p.currentPath());
		drawTextMid("Where " + title + " plays.", 0, DIM, box.x + box.w / 2, box.y + 58, box.w - 40, 2);
		drawTextMid("Its place and tracks come along.", 0, DIM, box.x + box.w / 2, box.y + 80, box.w - 40, 2);
		SessionHost& sh = SessionHost::get();
		std::string cur = sh.pageIsOutput() ? sh.session().out.tab : "";
		for (int i = playOnScroll; i < n && i < playOnScroll + PLAYON_SHOWN; i++)
		{
			Rect r = playOnRow(i);
			bool at = i == playOnCursor, here = list[i].tab == cur;
			if (pressedBtn == PLAYON_ROW + i) drawBox(r, PRESSED, R_BUTTON);
			else if (at) drawFocus(r);
			SDL_Color ink = at ? ACCENT : TEXT;
			int mid = r.y + r.h / 2;
			const Glyph& g = list[i].tab.empty() ? G_CARD : G_LAPTOP;
			drawGlyphMid(g, r.x + 30, mid, ink);
			int right = here ? 48 : 12;
			if (here)
			{
				// where it plays now: ticked, and said under its name
				drawGlyphMid(G_CHECK, r.x + r.w - 26, mid, ACCENT, 3);
				bool hh = list[i].tab.empty();
				std::string what = !hh && sh.pageWaits() ? "Waiting for a tap there"
					: (hh ? !p.paused : sh.session().playing) ? "Playing now" : hh ? "Paused here" : "Paused there";
				drawTextMid(list[i].name, 1, ink, r.x + 58, mid - 10, r.w - 58 - right);
				drawTextMid(what, 0, at ? DIM_ON : DIM, r.x + 58, mid + 14, r.w - 58 - right);
			}
			else drawTextMid(list[i].name, 1, ink, r.x + 58, mid, r.w - 58 - right);
		}
		drawScrollBar(box.y + 106, PLAYON_SHOWN * 60 - 6, playOnScroll, PLAYON_SHOWN, n);
		drawHints({{"UP/DOWN", "Move"}, {"A", "Play there"}, {"B", "Back"}});
	}

	bool Ui::playingKey(Button b, Player& p)
	{
		int n = (int)playingRows().size();
		switch (b)
		{
		case Button::Up: cursor = (cursor + n - 1) % n; break;
		case Button::Down: cursor = (cursor + 1) % n; break;
		case Button::A: return playingChoose(cursor, p);
		default: break;
		}
		return false;
	}

	bool Ui::playingChoose(int row, Player& p)
	{
		auto rows = playingRows();
		if (row < 0 || row >= (int)rows.size() || !p.playing()) return false;
		cursor = row;
		switch (rows[row])
		{
		case PR_LIKE: likeNow(p); return false;	// (the tab stays: the row says what it is now)
		case PR_ADD: openAddOne(p.currentPath(), p.title, p.loaded ? p.duration : -1, p); return false;
		case PR_NOW: showPlayer(); return false;
		case PR_PLAYON: openPlayOn(); return false;
		case PR_STOP:
			// main stops it (the place kept); the menu closed first, so it lands where Stop leads
			closeMenu(p);
			stopPressed = true;
			return false;
		case PR_SHOW:
			// its folder: the player left first (it plays on behind the browser), then the folder (showBrowser)
			if (bgPlay)
			{
				inMenu = false;
				page = Page::Browse;
				showInFolder(p.currentPath(), p);
				return false;
			}
			showAfterLeave = p.currentPath();
			inMenu = false;
			page = Page::Main;
			return true;
		default:
			inMenu = false;
			page = Page::Info;
			cursor = scroll = 0;
			infoAt = 0;
			return false;
		}
	}

	void Ui::drawPlaying(const Player& p)
	{
		std::string path = p.currentPath();
		bool audio = p.playingAudio();
		// what plays: its kind, its title, the artist and album (a video: where it is)
		Rect card{16, 68, 608, 62};
		drawBox(card, SURFACE, R_TILE);
		drawGlyphMid(audio ? G_NOTE : G_FILM, 46, card.y + 31, DIM, 3);
		drawTextMid(!p.title.empty() ? p.title : entryTitle(path), 1, TEXT, 80, card.y + 20, 520);
		std::string sub = audio && !p.artist().empty() ? p.artist() + (p.album().empty() ? "" : " \xC2\xB7 " + p.album())
			: whereOf(isNetPath(path) ? netParent(path) : parentFolder(path));
		drawTextMid(sub, 0, DIM, 80, card.y + 45, 520);
		// the playlists it's in, read again when they change
		if (plStore && (playingInSerial != plStore->serial() || playingInPath != path))
		{
			playingInSerial = plStore->serial();
			playingInPath = path;
			std::vector<std::string> in;
			for (auto& pl : plStore->addTargets())
				if (plStore->containsPath(pl.file, path)) in.push_back(pl.name);
			playingIn = in.empty() ? "" : in.size() == 1 ? "In " + in[0] : "In " + std::to_string(in.size()) + " playlists";
		}
		bool liked = likedNow(p);
		auto rows = playingRows();
		for (int i = 0; i < (int)rows.size(); i++)
		{
			Rect r = playingRowRect(i);
			bool at = i == cursor;
			if (pressedBtn == PLAY_ROW + i) drawBox(r, PRESSED, R_BUTTON);
			else if (at) drawFocus(r);
			const Glyph* g = &G_INFO;
			std::string label = "Info", note = "Size, codecs, tags";
			SDL_Color ink = at ? ACCENT : TEXT;
			switch (rows[i])
			{
			case PR_LIKE:
				g = liked ? &G_HEART : &G_HEART_O;
				label = liked ? "Liked" : "Like";
				note = liked ? "In Liked" : "Adds it to Liked";
				if (liked) ink = ACCENT;
				break;
			case PR_ADD: g = &G_PL_ADD; label = "Add to playlist\xE2\x80\xA6"; note = plStore ? playingIn : ""; break;
			case PR_NOW:
				g = &G_RIGHT;
				label = "Now playing";
				note = p.settings.buttonHotkeys ? "also SELECT, from any list" : "";
				ink = ACCENT;
				break;
			case PR_STOP:
				g = &G_STOP;
				label = "Stop";
				note.clear();	// (the default note is Info's)
				ink = DANGER;
				break;
			case PR_PLAYON:
				g = &G_LAPTOP;
				label = "Play on";
				note = outputName() + " \xE2\x80\xBA";
				break;
			case PR_SHOW:
				g = &G_FOLDER;
				label = "Show in folder";
				note = isNetPath(path) ? netBaseName(netParent(path)) : baseName(parentFolder(path));
				break;
			default: break;
			}
			int mid = r.y + r.h / 2;
			drawGlyphMid(*g, r.x + 31, mid, ink);
			int nw = 0;
			if (!note.empty())
			{
				drawTextMid(note, 0, at ? DIM_ON : DIM, r.x + r.w - 14, mid, 240, 1);
				nw = std::min(240, (int)std::lround(textWidth(note, 0) / sx)) + 24;
			}
			drawTextMid(label, 1, at ? ACCENT : rows[i] == PR_STOP ? DANGER : TEXT, r.x + 56, mid, r.w - 70 - nw);
		}
		drawHints({{"UP/DOWN", "Move"}, {"A", "Choose"}, {"B", "Close"}, {"L/R", "Tabs"}});
	}

	bool Ui::setLikedNow(Player& p, bool on)
	{
		if (!p.playing()) return false;
		std::string path = p.currentPath();
		std::string title = p.title.empty() || !p.loaded ? "" : p.title;
		return setLikedPath(p, path, title, p.loaded ? p.duration : -1, on);
	}

	bool Ui::setLikedPath(Player& p, const std::string& path, const std::string& title, double duration, bool on, int at,
		std::string* dropped)
	{
		if (dropped) dropped->clear();
		if (!plStore) return false;
		if (plStore->isLiked(path) != on)
		{
			// (Liked's own edits waiting, if it's open, are written first so this one doesn't lose them)
			plFlush(p);
			Playlist before;
			int skipped = 0;
			bool playsLiked = p.playing() && p.source().kind == SourceKind::Playlist && p.source().id == plStore->likedFile();
			if (playsLiked) plStore->load(plStore->likedFile(), before, skipped);
			if (!plStore->setLiked(path, title, duration, on, at, dropped))
			{
				if (plStore->likable(path))
					p.showNoteCard(G_ALERT_ROWS, G_ALERT.h, theme::ass(theme::DANGER).c_str(), "Couldn't save Liked", "The card may be full or locked");
				return plStore->isLiked(path);
			}
			// Liked is what plays: its order moved (a new one goes first), the player told as an edit
			if (playsLiked)
			{
				Playlist after;
				if (plStore->load(plStore->likedFile(), after, skipped))
				{
					std::vector<int> remap(before.entries.size(), -1);
					std::vector<char> used(after.entries.size(), 0);
					for (size_t i = 0; i < before.entries.size(); i++)
						for (size_t j = 0; j < after.entries.size(); j++)
							if (!used[j] && after.entries[j].path == before.entries[i].path) { remap[i] = int(j); used[j] = 1; break; }
					plTellPlayer(after, remap, p);
				}
			}
			// the pages that show Liked (the list's count, its items) read it again
			selSerial++;
			if (browseDir == plDirFor(plStore->likedFile()))
			{
				plCur = Playlist();
				plLoaded = plStore->load(plStore->likedFile(), plCur, skipped);
				plRefresh(p);
			}
			else plWebListChanged(p);
		}
		return plStore->isLiked(path);
	}
}
