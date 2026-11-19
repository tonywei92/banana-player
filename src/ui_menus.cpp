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
#include "ui_settings.h"
#include "ui_docs.h"
#include "ui_manga.h"
#include "pagebook.h"
#include "pagedecode.h"
#include <strings.h>
#include "durations.h"

namespace bp
{
	std::vector<Ui::Row> Ui::rows(const Player& p) const
	{
		if (page == Page::Browse) return browse->browseRows;
		std::vector<Row> out;
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

	void Ui::chapterStep(int dir, Player& p)
	{
		// L2/R2: the chapter before or after, over what plays and its two pages (the Chapters page's mark follows).
		// Here rather than in Ui::press so the List sheet, which takes the keys first, passes it the same way
		bool pred = predicting(p);
		int t = p.stepChapter(dir, pred ? shownPosition(p) : -1);
		if (pred && t >= 0 && t < (int)p.chapters.size()) noteSeek(p, p.chapters[t].time);
		else if (pred && t == CHAPTER_FILE_START) noteSeek(p, 0);
	}

	void Ui::choose(int i, Player& p)
	{
		// ".." (row -1): up a folder: as B and "<" go, selecting or not
		if (page == Page::Browse && i < 0 && browse->upRow()) { browse->browseUp(p); return; }
		auto r = rows(p);
		if (i < 0 || i >= (int)r.size()) return;
		if (page == Page::Browse)
		{
			// no connections yet: A adds one (as the page's button does); no playlists, New playlist
			if (browse->emptyAction()) { browse->doEmptyAction(p); return; }
			if (r[i].id < 0) return;
			// selecting: a row ticked (a folder still opens)
			if (browse->playlists->selMode != SEL_NONE && browse->playlists->selChoose(i, p)) return;
			if (r[i].id == PL_NEW) { browse->playlists->plNameOpen(false, "", ""); return; }
			if (r[i].id == PL_ROW)
			{
				// a playlist: its items (and the one kept for the next launch, as a folder gone into is)
				std::string file = browse->browsePaths[i];
				browse->enterFolder(plDirFor(file), p);
				p.setLastFolder(file);
				return;
			}
			if (r[i].id == PL_ITEM)
			{
				// an item that isn't there: what can be done about it (its menu, why under the title)
				if (browse->playlists->plMissingAt(i)) { openBrowseMenu(i, MENU_Y); return; }
				// the item that plays (from this playlist): back to it, never from its start
				if (rowChip(i)) { showPlayer(); return; }
				browse->playlists->plPlay(browse->playlists->plCur, i, false, p);
				return;
			}
			if (browse->browseDir == FORMDIR) { browse->formChoose(r[i].id, p); return; }
			if (browse->browseDir == FINDNET)
			{
				if (r[i].id == R_FOUND && i < (int)browse->found.size()) { FoundHost h = browse->found[i]; browse->openForm(nullptr, &h, p); }
				else if (r[i].id == R_SEARCH) browse->enterFolder(FINDNET, p);
				else if (r[i].id == R_TYPE_ADDRESS) browse->openForm(nullptr, nullptr, p);
				return;
			}
			if (r[i].id == 4 || r[i].id == 6)
			{
				// a connection (its start folder), or Try again
				std::string path = browse->browsePaths[i];
				NetPath np;
				if (r[i].id == 4 && netStore && parseNetPath(path, np)) netStore->touch(np.server);
				browse->enterFolder(path, p);
				return;
			}
			std::string path = browse->browsePaths[i];	// a copy: entering a folder rebuilds the list
			// the folder last gone into, or a video opened from, is the one kept (Remember last folder): backing out
			// to the first page to quit leaves it as it was
			if (r[i].id == 1)
			{
				browse->enterFolder(path, p);
				if (path != RECENT) p.setLastFolder(path);
			}
			else
			{
				if (browse->browseDir != RECENT) p.setLastFolder(browse->browseDir);
				openFile(path, p);
			}
			return;
		}
		if (r[i].id == STYLE_ROW)
		{
			openSettings(p, true);
			settingsPage->openGroup(GROUP_SUBS);
			settingsPage->styleFromSubs = true;
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

	void Ui::closeQuick()
	{
		quickHoldAt = 0;
		if (Overlay* o = findOverlay("quick")) o->close();
	}

	void Ui::toggleQuick(Player& p)
	{
		slideDrop();	// (a bar of these, or of the page under them, still under a finger)
		if (findOverlay("quick"))
		{
			closeQuick();
			return;
		}
		if (!panel.on()) panel.set(true);	// the bottom panel back, to see them
		sameBrightness = p.settings.sameBrightness;
		volumePct = volume.get();
		quickCursor = 0;
		quickSub = 0;
		pressedBtn = -1;
		openQuick();
	}

	void Ui::quickAdjust(int row, int dir, Player& p)
	{
		// Web dashboard's row: LEFT the switch, RIGHT its Settings button (while it's on); while incognito, nothing moves
		if (row == Q_WEB)
		{
			if (!incognito) quickSub = dir > 0 && p.settings.webDashboard ? 1 : 0;
			return;
		}
		if (row == Q_VOLUME && volumePct >= 0)
		{
			// bananad's keys' step
			// (the whole level: past 100 is the player's gain, as the volume bar shows it)
			setVolumeTotal(p, (volumeTotal(p) + (dir > 0 ? 5 : -1)) / 5 * 5, true);	// to the next five either way
		}
		else if (row == Q_BRIGHT || row == Q_BOTTOM)
		{
			Backlight::Panel panelOf = row == Q_BOTTOM ? Backlight::BOTTOM : Backlight::TOP;
			int pm = backlight.permille(panelOf);
			if (pm >= 0) setBrightness(panelOf, stepBrightness(pm, dir), p);
		}
	}

	void Ui::quickSlide(int row, int x, int y, Player& p)
	{
		// the row's bar under a finger: the volume in fives, a screen's brightness in tenths (never under 10%)
		int which = quickRows()[row];
		if (which == Q_VOLUME)
		{
			if (volumePct < 0) return;
			slideGrab(SL_QUICK, row, {quickBar(row), {0, (double)std::max(100, p.settings.volumeMax), 5}, false, 0, (double)volumeTotal(p)}, x, y, p, [this](double v, bool, Player& p) {
				if ((int)v == volumeTotal(p)) return;
				setVolumeTotal(p, (int)v, false);
			});
			return;
		}
		Backlight::Panel panelOf = which == Q_BOTTOM ? Backlight::BOTTOM : Backlight::TOP;
		slideGrab(SL_QUICK, row, {quickBar(row), {0, 1000, 100}, false, 0, (double)std::max(100, backlight.permille(panelOf))}, x, y, p, [this, panelOf](double v, bool, Player& p) {
			int pm = std::max(100, (int)v);
			if (pm != backlight.permille(panelOf)) setBrightness(panelOf, pm, p);
		});
	}

	void Ui::setIncognito(bool on, Player& p, bool note)
	{
		if (on == incognito) return;
		// turning it on cuts off a browser: asked once, and only when a browser is signed in (no question with nobody)
		if (on)
		{
#ifdef BP_WEB
			if (p.settings.webDashboard && web && web->running() && web->clients() > 0)
			{
				fprintf(stderr, "incognito: %d browser(s) signed in: asking\n", web->clients());
				askIncognito();
				return;
			}
#endif
		}
		applyIncognito(on, p, note);
	}

	void Ui::applyIncognito(bool on, Player& p, bool note)
	{
		incognito = on;
		p.incognito = on;
		// the dashboard's switch keeps its saved value; the server is held off while incognito is on and comes back
		// with the same PIN and sign-ins when it ends (the WebServer keeps both in the state folder)
		if (p.settings.webDashboard && webPower) webPower(!on);
		fprintf(stderr, "incognito mode %s%s\n", on ? "on" : "off", on ? " (the Web dashboard is off while it is on)" : "");
		if (note) p.osd(on ? "Incognito on" : "Incognito off", on ? 3000 : 2000);
	}

	void Ui::incognitoCloseAll(Player& p)
	{
		// Close all: Stop everywhere, a book / PDF / comic / the Manga reader and sheets close, Home shows, and
		// incognito ends. Places and read marks are kept as Stop keeps them; copies and downloads go on. main closes
		// its photo viewer (another player) before this.
		// The last gate: every trigger (lid, sleep, power hold) checks incognito on the main thread first and logs
		// an ignored one; this makes a stale copy harmless
		if (!incognito) return;
		fprintf(stderr, "incognito: close all: stop, close, Home, incognito off\n");
		stopPressed = true;
		homeAfterCloseAll = true;	// Stop's back-to-where-it-came-from must not undo Home
		if (page == Page::DocText || page == Page::DocPages) docs->docClose(p);
		if (page == Page::MangaRead) manga->mangaCloseReader();
		for (auto& o : overlays) o->done = true;
		dropClosedOverlays();
		settingsOver = false;
		ctxOpen = false;
		kbOpen = false;
		browse->playlists->pickerOpen = false;
		swallowTouch = true;
		browse->homeView->goHome(Home::HOME_GRID, 0, p);
		page = Page::Browse;
		setGroup = -1;
		cursor = scroll = 0;
		// leaving incognito starts the dashboard again when its switch says on; no note (it went to sleep or Home)
		applyIncognito(false, p, false);
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
			if (incognito)
			{
				// off in incognito: the switch keeps its saved value; a note says why it can't be changed
				p.osd("Leave incognito to use the Web dashboard", 2500);
				return;
			}
			if (quickSub == 1 && p.settings.webDashboard) { closeQuick(); openWeb(); }
			else webTurn(!p.settings.webDashboard, p);
		}
		else if (row == Q_INCOGNITO)
		{
			// the Ui's and the player's, never a setting saved: off at every start; with a browser signed in, it asks
			setIncognito(!incognito, p);
		}
		else if (row == Q_QUIT) quitRequested = true;
	}

	bool Ui::quickKey(Button b, Player& p)
	{
		auto rows = quickRows();
		int n = (int)rows.size();
		bool live = nowPlayable(barPlayer(p));
		quickCursor = std::clamp(quickCursor, -1, n - 1);
		if (quickCursor < 0 && !live) quickCursor = 0;
		switch (b)
		{
		case Button::Up:
			// the block is one stop above Volume, and only while something plays; the cursor opens on Volume
			if (quickCursor < 0) quickCursor = n - 1;
			else if (quickCursor == 0 && live) quickCursor = -1;
			else quickCursor = (quickCursor + n - 1) % n;
			break;
		case Button::Down:
			quickCursor = quickCursor < 0 ? 0 : (quickCursor + 1) % n;
			break;
		case Button::Left: case Button::Right:
			if (quickCursor < 0) quickNow(b, p);
			else quickAdjust(rows[quickCursor], b == Button::Right ? 1 : -1, p);
			break;
		case Button::A:
			if (quickCursor < 0) quickNow(b, p);
			else quickChoose(rows[quickCursor], p);
			break;
		// L / R: previous / next, from any row (the player's own keys)
		case Button::L: case Button::R: quickStep(b == Button::R ? 1 : -1, p); break;
		// SELECT: the player, as it does from every list; Quick settings closes and the page keeps its place
		case Button::Select:
			if (live) { closeQuick(); goNowPlaying(p); }
			break;
		case Button::B: case Button::Start: closeQuick(); break;
		default: break;
		}
		return false;
	}

	void Ui::quickNow(Button b, Player& p)
	{
		// the block's keys by the D-pad: A plays or pauses, LEFT / RIGHT seek (5 s back, 10 s on), as the player's
		Player& bp = barPlayer(p);
		if (!nowPlayable(bp)) return;
		if (b == Button::A) { miniToggle(p); return; }
		double delta = b == Button::Left ? -BACK_S : FORWARD_S;
		if (predicting(bp) && bp.duration > 0)
		{
			double t = std::clamp(shownPosition(bp) + delta, 0.0, bp.duration);
			bp.seekFraction(t / bp.duration);
			noteSeek(bp, t);
		}
		else bp.seekRelative(delta);
	}

	void Ui::quickNowKey(int what, Player& p)
	{
		if (what == QUICK_NOW_PREV) quickStep(-1, p);
		else if (what == QUICK_NOW_NEXT) quickStep(1, p);
		else miniToggle(p);
	}

	void Ui::quickStep(int dir, Player& p)
	{
		// the player's L / R: the session's Step; what it doesn't have (it disagrees), the list's previous / next
		Player& bp = barPlayer(p);
		if (!nowPlayable(bp)) return;
		if (SessionHost::get().stepsHere(bp)) { SessionHost::get().step(dir); return; }
		if (dir < 0 ? !bp.back() : !bp.next()) bp.osd(dir < 0 ? "This is the first one" : "This is the last one");
	}

	bool Ui::quickPointer(const Pointer& pt, int x, int y, Player& p)
	{
		auto rows = quickRows();
		auto barRow = [&](int i) { return rows[i] == Q_VOLUME || rows[i] == Q_BRIGHT || rows[i] == Q_BOTTOM; };
		bool live = nowPlayable(barPlayer(p));
		if (pt.kind == Pointer::Down)
		{
			pressedBtn = -1;
			quickHoldAt = 0;
			// outside: closed (the finger lifting presses nothing)
			if (!quickPanel().hit(x, y))
			{
				closeQuick();
				swallowTouch = true;
				return false;
			}
			// the Now playing block: its three keys do their jobs; anywhere else on it is the player, and it is not
			// pressed while nothing plays (the cursor skips it then)
			if (quickNowRect().hit(x, y))
			{
				if (!live) return false;
				quickCursor = -1;
				if (quickNowPlay().hit(x, y)) { pressedBtn = QUICK_NOW_PLAY; quickHoldAt = SDL_GetTicks(); }
				else if (quickNowPrev().hit(x, y)) pressedBtn = QUICK_NOW_PREV;
				else if (quickNowNext().hit(x, y)) pressedBtn = QUICK_NOW_NEXT;
				else pressedBtn = QUICK_NOW_CARD;
				return false;
			}
			for (int i = 0; i < (int)rows.size(); i++)
			{
				if (!quickRowRect(i).hit(x, y)) continue;
				quickCursor = i;
				if (barRow(i) && quickMinus(i).hit(x, y)) pressedBtn = 300 + i * 4;
				else if (barRow(i) && quickPlus(i).hit(x, y)) pressedBtn = 301 + i * 4;
				else if (rows[i] == Q_WEB && p.settings.webDashboard && quickPinBtn(i).hit(x, y)) pressedBtn = 303 + i * 4;
				else if (barRow(i) && Rect{quickBar(i).x - 6, quickRowRect(i).y, quickBar(i).w + 12, quickRowRect(i).h}.hit(x, y))
				{
					// the bar (anywhere in the row between its two buttons): to the finger at once, and with it
					quickSlide(i, x, y, p);
				}
				else if (!barRow(i)) pressedBtn = 302 + i * 4;
			}
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			quickHoldAt = 0;
			if (pressedBtn >= QUICK_NOW_CARD && pressedBtn <= QUICK_NOW_NEXT)
			{
				int what = pressedBtn;
				pressedBtn = -1;
				if (!quickNowRect().hit(x, y)) return false;
				// a tap on the card (art, title, line, progress) opens the player and closes the panel
				if (what == QUICK_NOW_CARD) { if (live) { closeQuick(); goNowPlaying(p); } }
				else if (live) quickNowKey(what, p);
				return false;
			}
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

	void Ui::drawQuickNow(const Player& p)
	{
		const Player& bp = barPlayer(p);
		bool live = nowPlayable(bp);
		bool err = live && !bp.error.empty();
		Rect card = quickNowRect();
		// a card: focused while the cursor is on it (only while something plays: the block is passed by when idle)
		if (quickCursor < 0 && live) kit->flat(card, BUTTON, 12, ACCENT);
		else kit->flat(card, SURFACE, 12, kit->edgeOf(SURFACE));
		Rect art = quickNowArt();
		if (live) kit->artPlaceholder(art, bp.artPlaceholderKey(), 6, true);
		else
		{
			fillRound(art, TRACK, 6);
			drawGlyphMid(G_NOTE, art.x + art.w / 2, art.y + art.h / 2, DIM);
		}
		int x0 = art.x + art.w + 12, xr = quickNowPrev().x - 12, room = std::max(1, xr - x0);
		if (!live)
		{
			drawTextMid("Nothing playing", F_P14, DIM, x0, card.y + 24, room);
			drawTextMid("What you play shows here", F_I12, DIM, x0, card.y + 44, room);
		}
		else
		{
			std::string title = !bp.title.empty() ? bp.title : entryTitle(bp.currentPath());
			drawTextMid(title, F_P14, TEXT, x0, card.y + 22, room);
			// artist and time, then the state marks the mini bar's pills said, in ACCENT
			std::string line = bp.artist();
			if (!line.empty()) line += " \xC2\xB7 ";
			line += err ? "Couldn't read" : formatTime(shownPosition(bp)) + " / " + formatTime(bp.duration);
			SDL_Color ink = err ? DANGER : DIM;
			drawTextMid(line, F_I12, ink, x0, card.y + 44, room);
			int px = x0 + kit->textW(line, F_I12);
			for (auto& s : kit->statePills(bp))
			{
				std::string t = " \xC2\xB7 " + s;
				int tw = kit->textW(t, F_I12);
				if (px + tw > xr) break;
				drawTextMid(t, F_I12, ACCENT, px, card.y + 44, tw + 1);
				px += tw;
			}
		}
		// the three keys: Previous 52 x 48, Play / Pause 68 x 48 (the primary), Next 52 x 48; dimmed when idle
		auto key = [&](Rect r, int id, const Glyph* g, bool primary) {
			bool down = pressedBtn == id;
			bool off = !live;
			SDL_Color ground = off ? Kit::off(BUTTON) : down ? PRESSED : primary ? ACCENT : BUTTON;
			drawBox(r, ground, R_BUTTON);
			SDL_Color ink = off ? Kit::off(TEXT) : primary ? (down ? TEXT : ON_ACCENT) : TEXT;
			drawGlyphMid(*g, r.x + r.w / 2, r.y + r.h / 2, ink, 2);
		};
		key(quickNowPrev(), QUICK_NOW_PREV, &G_PREV, false);
		key(quickNowPlay(), QUICK_NOW_PLAY, live && bp.paused ? &G_PLAY : &G_PAUSE, true);
		key(quickNowNext(), QUICK_NOW_NEXT, &G_NEXT, false);
		// the progress line along its foot (the mini bar's own piece); held Play fills the hold as the player's does
		lay::MiniBar m = lay::miniBar(card);
		if (live && !err && bp.duration > 0)
		{
			double f = std::clamp(shownPosition(bp) / bp.duration, 0.0, 1.0);
			if (f > 0) fillRound({m.line.x, m.line.y, (int)std::lround(m.line.w * f), m.line.h}, ACCENT, 1);
		}
		if (quickHoldAt && pressedBtn == QUICK_NOW_PLAY)
		{
			Rect r = quickNowPlay();
			double f = std::min(1.0, (SDL_GetTicks() - quickHoldAt) / (double)PLAY_HOLD_MS);
			fillRound({r.x + 6, r.y + r.h - 8, (int)((r.w - 12) * f), 3}, ACCENT, 1);
		}
	}

	void Ui::drawQuick(const Player& p)
	{
		Rect pr = quickPanel();
		drawDialog(pr, "");
		drawTextMid("Quick settings", 1, ACCENT, pr.x + 24, pr.y + 28);
		if (battery.percent >= 0)
			drawTextMid("Battery " + std::to_string(battery.percent) + "%" + (battery.charging ? " \xC2\xB7 charging" : ""), 0, DIM,
				pr.x + pr.w - 24, pr.y + 28, 0, 1);
		drawQuickNow(p);
		auto rows = quickRows();
		for (int i = 0; i < (int)rows.size(); i++)
		{
			Rect r = quickRowRect(i);
			bool at = i == quickCursor;
			int row = rows[i], mid = r.y + r.h / 2;
			if (row == Q_QUIT)
			{
				// an action key: Classic whatever the page (the one red-filled key)
				Kit::ClassicHere classic;
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
				// a tick box, and what it keeps out of the history; while it's on, what the lid or a sleep does
				Rect box{r.x + 16, mid - 13, 26, 26};
				fillRound(box, at ? ACCENT : TEXT, R_FIELD);
				fillRound({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, at ? BUTTON : SURFACE, R_FIELD - 2);
				if (incognito) drawGlyphMid(G_CHECK, box.x + box.w / 2, box.y + box.h / 2, ACCENT);
				drawTextMid("Incognito mode", 1, at ? ACCENT : TEXT, r.x + 56, incognito && p.settings.lidIncognito != LID_INCOGNITO_NOTHING ? mid - 8 : mid, 300);
				drawGlyphMid(G_INCOGNITO, r.x + r.w - 24, mid, incognito ? ACCENT : (at ? DIM_ON : DIM));
				if (incognito && p.settings.lidIncognito != LID_INCOGNITO_NOTHING)
					drawTextMid(p.settings.lidIncognito == LID_INCOGNITO_QUIT ? "Lid or sleep: quits" : "Lid or sleep: closes all", 0,
						DIM, r.x + 56, mid + 12, 320);
				continue;
			}
			if (row == Q_WEB)
			{
				// a tick box (on or off), its name, and while it's on a button to its page in Settings. Incognito holds
				// the dashboard off: the box at 45%, its reason in the note, and nothing changes
				bool off = incognito, on = p.settings.webDashboard && !off;
				bool onSwitch = !off && at && (quickSub == 0 || !on);
				Rect box{r.x + 16, mid - 13, 26, 26};
				fillRound(box, Kit::off(onSwitch ? ACCENT : TEXT, off), R_FIELD);
				fillRound({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, Kit::off(at ? BUTTON : SURFACE, off), R_FIELD - 2);
				if (on) drawGlyphMid(G_CHECK, box.x + box.w / 2, box.y + box.h / 2, ACCENT);
				drawTextMid("Web dashboard", 1, Kit::off(onSwitch ? ACCENT : TEXT, off), r.x + 56, off ? mid - 8 : mid, 200);
				if (off)
				{
					drawTextMid("Off in incognito", 0, DIM, r.x + 56, mid + 12, 240);
					continue;
				}
				if (on)
				{
					// the row's action key: Classic in Clean too
					Kit::ClassicHere classic;
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
				pct = volumePct >= 100 && p.boost > 100 ? p.boost : volumePct;
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
			// (the bar under a finger: its value, left of the bar, lit; a knob where the fill ends, as a held one's)
			bool held = slideHeld(SL_QUICK, i);
			if (row != Q_VOLUME) drawTextMid(pct < 0 ? "-" : std::to_string(pct) + "%", 0, held ? ACCENT : at ? DIM_ON : DIM, quickMinus(i).x - 6, mid, 0, 1);
			for (int k = 0; k < 2; k++)
			{
				// the steppers are the row's, not keys: a quiet look (no ground) in Clean, a key in Classic
				Rect bt = k ? quickPlus(i) : quickMinus(i);
				bool down = pressedBtn == 300 + i * 4 + k;
				if (theme::drawingClean())
				{
					if (down) fillRound(bt, PRESSED, R_BUTTON);
				}
				else drawBox(bt, down ? PRESSED : at ? BG : BUTTON, R_BUTTON);
				drawGlyphMid(k ? G_PLUS : G_MINUS, bt.x + bt.w / 2, bt.y + bt.h / 2, TEXT);
			}
			Rect bar = quickBar(i);
			if (row == Q_VOLUME)
			{
				// the kit's volume bar: cream to 100, yellow to 120, red past it, the 100 mark; its number left of the buttons
				int vmax = std::max(100, p.settings.volumeMax);
				lay::VolumeBar vb;
				vb.bar = {bar.x, bar.y + (bar.h - 12) / 2, bar.w, 12};
				vb.number = {quickMinus(i).x - 6 - 64, mid - 12, 64, 24};
				vb.tickX = vmax > 100 ? lay::volumeX(vb.bar, 100, vmax) : -1;
				Kit(*this).volumeBar(vb, std::max(0, pct), vmax);
				if (held) drawSlideKnob({lay::volumeX(vb.bar, std::max(0, pct), vmax) - 5, vb.bar.y - 4, 10, vb.bar.h + 8});
			}
			else
			{
				// the redesign's slider: 14 tall, the knob 8 taller; its share the screen's level (never under 10%)
				drawSlider({bar.x, bar.y + (bar.h - 14) / 2, bar.w, 14}, pct > 0 ? std::min(pct, 100) / 100.0 : 0, held);
			}
		}
		if (p.settings.buttonHotkeys)
		{
			// five items would run past the panel's width: L / R still step, they are the player's keys
			if (nowPlayable(barPlayer(p)))
				drawHints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"SELECT", "Player"}, {"F/B", "Close"}});
			else drawHints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"A", "Toggle"}, {"F/B", "Close"}});
		}
		else drawFooterText("Tap outside to close", DIM);
	}

	bool Ui::popupShown(const Player& p) const
	{
		if (!updater || popupClosed || page != Page::Browse || ctxOpen) return false;
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
			: browse->browseDir.empty() ? std::string("Your cards") : browse->browseDir == NETROOT ? std::string("Your connections")
			: browse->browseDir == RECENT ? std::string("Recent")
			: isNetPath(browse->browseDir) && netBaseName(browse->browseDir).empty() ? std::string("Shares")
			: isNetPath(browse->browseDir) ? netBaseName(browse->browseDir) : baseName(browse->browseDir);
		drawDialog(box, name);
		// a playlist's Delete, pressed once: what it doesn't do
		bool plArmed = ctxArmed && ctxCursor < (int)ctxItems.size() && ctxItems[ctxCursor] == CTX_PL_DELETE;
		bool missArmed = ctxArmed && ctxCursor < (int)ctxItems.size() && ctxItems[ctxCursor] == CTX_PL_MISSING;
		if (ctxExtra() && (!ctxNote.empty() || plArmed || missArmed))
			drawTextMid(missArmed ? "A card that\xE2\x80\x99s out? Put it back first" : plArmed ? "Your files stay where they are" : ctxNote, 0,
				plArmed || missArmed ? ACCENT : DIM, W / 2, box.y + 60, 368, 2);
		else if (ctxExtra())
			drawTextMid(ctxStatus.empty() ? std::string(sv ? std::string(netTypeLabel(sv->type)) + "  " + sv->host : "") : ctxStatus, 0,
				ctxStatus == "It works" ? ACCENT : DIM, W / 2, box.y + 60, 368, 2);
		for (int i = 0; i < (int)ctxItems.size(); i++)
		{
			int it = ctxItems[i];
			bool del = it == CTX_DELETE || it == CTX_PL_DELETE || it == CTX_F_DELETE;
			bool armed = del && ctxArmed && i == ctxCursor;
			const Home::RecentItem* ri = nullptr;
			for (auto& x : browse->homeView->recent)
				if (x.path == browse->homeView->ctxRecent) ri = &x;
			std::string note;
			SDL_Color noteInk = i == ctxCursor ? DIM_ON : DIM;
			if (it == CTX_R_SHOW && ri) note = ri->where;
			if (it == CTX_HIDDEN) note = showHidden ? "On" : "Off";
			if (it == CTX_PL_MISSING && ctxArmed && i == ctxCursor)
			{
				note = "Press again: " + std::to_string(ctxMissing);
				noteInk = ACCENT;
			}
			if (it == CTX_CLEAR_RECENT)
			{
				bool armedClear = ctxArmed && i == ctxCursor;
				note = browse->homeView->recent.empty() ? "Empty" : armedClear ? "Press again to clear" : "Press twice";
				if (armedClear) noteInk = ACCENT;
			}
			bool docR = isDocumentName(browse->homeView->ctxRecent) || isComicName(browse->homeView->ctxRecent);	// (a document is read, at a page)
			std::string resume = ri && !ri->at.empty() ? (docR ? "Resume at " : "Resume from ") + ri->at : docR ? "Open" : "Play";
			bool likedR = it == CTX_R_LIKE && browse->playlists->plStore && browse->playlists->plStore->isLiked(browse->homeView->ctxRecent);
			bool inLiked = plIsLiked(browse->playlists->ctxPlFile);	// Liked's Remove is Unlike
			const char* label = it == CTX_R_PLAY ? resume.c_str() : it == CTX_R_START ? (docR ? "Open from the start" : "Play from the start")
				: it == CTX_R_SHOW ? "Show in folder" : it == CTX_R_ADD || it == CTX_PL_ADD_ONE ? "Add to playlist\xE2\x80\xA6"
				: it == CTX_R_LIKE ? (likedR ? "Unlike" : "Like") : it == CTX_R_REMOVE ? "Remove from Recent"
				: it == CTX_CLEAR_RECENT ? "Clear Recent" : it == CTX_HIDDEN ? "Show hidden folders" : it == CTX_SETTINGS ? "Settings"
				: it == CTX_HIDE ? (isHiddenFolder(ctxPath) ? "Unhide folder" : "Hide folder")
				: it == CTX_REFRESH ? "Refresh" : it == CTX_ADD ? "Add connection" : it == CTX_FIND ? "Find on the network"
				: it == CTX_EDIT ? "Edit connection" : it == CTX_TEST ? "Test connection"
				: armed ? "Press again to delete" : it == CTX_DELETE ? "Delete connection" : it == CTX_PL_DELETE ? "Delete playlist"
				: it == CTX_PL_NEW ? "New playlist\xE2\x80\xA6" : it == CTX_PL_PLAY ? (isPlDir(browse->browseDir) ? "Play all" : "Play")
				: it == CTX_PL_SHUFFLE ? "Shuffle play" : it == CTX_PL_RENAME ? "Rename\xE2\x80\xA6" : it == CTX_PL_DEDUP ? "Remove duplicates" : it == CTX_PL_MISSING ? "Remove missing"
				: it == CTX_PL_HERE ? "Play from here" : it == CTX_PL_REMOVE ? (inLiked ? "Unlike" : "Remove from playlist") : it == CTX_PL_SHOW ? "Show in folder"
				: it == CTX_PL_ADD_FILES ? "Add files\xE2\x80\xA6" : it == CTX_PL_ADD_FOLDER ? "Add folder\xE2\x80\xA6"
				: it == CTX_PL_SELECT ? "Select" : it == CTX_PL_MOVE ? "Move" : it == CTX_SELECT_FILES ? "Select files"
				: it == CTX_ADD_TO ? "Add to playlist\xE2\x80\xA6" : it == CTX_WEB ? "Web dashboard\xE2\x80\xA6"
				: it == CTX_F_COPY ? "Copy\xE2\x80\xA6" : it == CTX_F_MOVE ? "Move\xE2\x80\xA6" : it == CTX_F_RENAME ? "Rename\xE2\x80\xA6"
				: it == CTX_F_DELETE ? "Delete" : it == CTX_F_INFO ? "Info" : "Cancel";
			const Glyph* g = it == CTX_R_PLAY ? &G_PLAY : it == CTX_R_START ? &G_REFRESH : it == CTX_R_SHOW ? &G_FOLDER
				: it == CTX_R_ADD || it == CTX_PL_ADD_ONE ? &G_PL_ADD : it == CTX_R_LIKE ? (likedR ? &G_HEART : &G_HEART_O)
				: it == CTX_PL_REMOVE && inLiked ? &G_HEART_O : it == CTX_R_REMOVE ? &G_MINUS : it == CTX_CLEAR_RECENT ? &G_TRASH
				: it == CTX_HIDDEN ? &G_EYE : it == CTX_SETTINGS ? &G_GEAR
				: it == CTX_HIDE ? &G_EYE : it == CTX_REFRESH ? &G_REFRESH : it == CTX_ADD ? &G_PLUS : it == CTX_FIND ? &G_SEARCH
				: it == CTX_EDIT ? &G_PENCIL : it == CTX_TEST ? &G_PLUG : del ? &G_TRASH
				: it == CTX_PL_NEW ? &G_PLUS : it == CTX_PL_PLAY || it == CTX_PL_HERE ? &G_PLAY : it == CTX_PL_SHUFFLE ? &G_SHUFFLE
				: it == CTX_PL_RENAME ? &G_PENCIL : it == CTX_PL_DEDUP || it == CTX_PL_REMOVE ? &G_MINUS : it == CTX_PL_MISSING ? &G_TRASH : it == CTX_PL_SHOW ? &G_FOLDER
				: it == CTX_PL_ADD_FILES ? &G_PLUS : it == CTX_PL_ADD_FOLDER ? &G_FOLDER : it == CTX_PL_SELECT || it == CTX_SELECT_FILES ? &G_BOX_ON
				: it == CTX_PL_MOVE || it == CTX_F_MOVE ? &G_MOVE : it == CTX_ADD_TO ? &G_PLAYLIST : it == CTX_WEB ? &G_WEB
				: it == CTX_F_COPY ? &G_PLUS : it == CTX_F_RENAME ? &G_PENCIL : it == CTX_F_INFO ? &G_INFO : &G_CROSS;
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
					: it == CTX_NP_LIKE ? (liked ? &G_HEART : &G_HEART_O) : it == CTX_NP_ADD ? &G_PL_ADD
					: it == CTX_NP_SHOW ? &G_FOLDER : &G_STOP;
				std::string npNote = it == CTX_NP_SHOW ? where : it == CTX_NP_NOW && np.settings.buttonHotkeys ? "SELECT" : "";
				drawDialogRow(ctxRow(i), ng, npLabel, i == ctxCursor, pressedBtn == 100 + i,
					it == CTX_NP_STOP ? DANGER : it == CTX_NP_NOW || (it == CTX_NP_LIKE && liked) ? ACCENT : TEXT, npNote, noteInk);
				continue;
			}
			// a read-only network folder's Move, Rename, Delete: dim, and why on the right
			bool ro = isNetPath(ctxPath) && !browse->netWritable && (it == CTX_F_MOVE || it == CTX_F_RENAME || it == CTX_F_DELETE);
			if (ro) { note = "Read only"; noteInk = DIM; }
			drawDialogRow(ctxRow(i), g, label, i == ctxCursor, pressedBtn == 100 + i, ro ? DIM : del ? (armed ? ACCENT : DANGER) : it == CTX_CANCEL ? DIM
				: it == CTX_R_LIKE && likedR ? ACCENT : TEXT, note, noteInk);	// (the heart lit only when it is liked)
		}
		// a menu too long for its line (a playlist's): Delete's word on the footer instead
		if (plArmed && !ctxExtra()) drawFooterText("Your files stay where they are", ACCENT);
		else drawHints({{"A", "Choose"}, {"B", "Close"}});
	}

	Ui::RowIcon Ui::rowIcon(int i) const
	{
		if (i < 0 || i >= (int)browse->browseRows.size() || i >= (int)browse->browsePaths.size()) return ICON_NONE;
		const std::string& path = browse->browsePaths[i];
		// the form's actions (its ids are its own: they meet the found servers')
		if (browse->browseDir == FORMDIR) return browse->browseRows[i].id == FORM_TEST ? ICON_TEST : browse->browseRows[i].id == FORM_SAVE ? ICON_SAVE : ICON_NONE;
		if (browse->browseDir == FINDNET)
			return browse->browseRows[i].id == R_FOUND ? ICON_NETWORK : browse->browseRows[i].id == R_SEARCH ? ICON_REFRESH
				: browse->browseRows[i].id == R_TYPE_ADDRESS ? ICON_TYPE : ICON_NONE;
		switch (browse->browseRows[i].id)
		{
		case PL_ROW: return plIsLiked(path) ? ICON_LIKED : ICON_PLAYLIST;
		case PL_NEW: return ICON_NEW;
		case 4: return ICON_NETWORK;
		case 6: return ICON_REFRESH;
		case 1:
			if (path == RECENT) return ICON_RECENT;
			return browse->browseDir.empty() ? ICON_PLACE : ICON_FOLDER;	// the first page's: the places (this card...)
		case 2:
			if (isComicName(path)) return ICON_COMIC;
			if (isPdfName(path)) return ICON_PDF;
			if (isDocumentName(path)) return path.size() > 5 && strcasecmp(path.c_str() + path.size() - 5, ".epub") == 0 ? ICON_EPUB : ICON_DOC;
			return isPhotoFile(path) ? ICON_PHOTO : isAudioFile(path) ? ICON_SONG : isVideoFile(path) ? ICON_VIDEO : ICON_FILE;
		default: return ICON_NONE;	// messages, Clear, Try again
		}
	}

	// ---- Up next and the chapters: the List sheet's lists (ListSheet, ui_player.cpp) ----

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
			// (the licence's own terms, said plainly: as the README's No warranty says them)
			{"Warranty", "None: provided as is. Use it at your own risk: the authors aren't responsible for any damage to your handheld or lost files"},
			{"Manga sources", "Third parties': Banana Player doesn't host or vouch for what they serve"},
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
		aboutQr = false;
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
		if (page == Page::About)
		{
			// the About group's own header (the kit's, as every settings page): back, its title and where it is, both
			// beside the back key
			lay::PlayerLayout pl = lay::playerLayout(0);
			Kit::Header h;
			h.title = "About";
			h.crumb = "Settings \xC2\xB7 " + std::to_string((int)GROUP_ABOUT + 1) + " of " + std::to_string(GROUPS);
			h.more = false;
			h.inlineTitle = true;
			h.backPressed = pressedBtn == BACK_ICON;
			h.right = [&](int right, int cy) {
				drawBattery(right, cy - 8, p, pl.back.x + pl.back.w + 130);
				return right - (616 - statusLeft(p)) - 10;
			};
			kit->header(pl, h);
		}
		else drawHeader("Info", p);
		int top = page == Page::About ? ABOUT_Y : INFO_Y;
		int shown = page == Page::About ? ABOUT_SHOWN : INFO_SHOWN;
		for (int i = scroll; i < (int)infoLines.size() && i < scroll + shown; i++)
		{
			int y = top + (i - scroll) * INFO_H, mid = y + INFO_H / 2;
			const InfoLine& l = infoLines[i];
			if (l.header) drawTextMid(l.label, 1, ACCENT, 24, mid);
			else
			{
				drawTextMid(l.label, 0, DIM, 32, mid, INFO_VALUE_X - 40);
				drawTextMid(l.text, 0, TEXT, INFO_VALUE_X, mid);
			}
		}
		drawScrollBar(top, shown * INFO_H, scroll, shown, (int)infoLines.size());
		if (page == Page::About)
		{
			drawAboutSupport();
			// Y puts the code up large on the top panel (a tap on the small one does too); while it is up, B and Y
			// put it down
			if (aboutQrUp()) kit->hints({{"B", "Close"}, {"Y", "Close"}});
			else kit->hints({{"UP/DOWN", "Scroll"}, {"Y", "QR"}, {"L2/R2", "Group"}, {"B", "Back"}});
			return;
		}
		drawHints({{"UP/DOWN", "Scroll"}, {"A/B", "Back"}});
	}

	// About's foot: one dim line, the static QR image of the same address (packaging/support/kofi-qr.png, shipped beside
	// icon.png in the bundle) and the address written out. Nothing here is a key or takes a touch.
	void Ui::drawAboutSupport()
	{
		int y = 340;
		fill({24, y, 592, 1}, RULE);
		Rect qr = aboutQrRect();	// (the same box a tap takes for Y's large view)
		SDL_Rect d = devRect(qr);
		int s = std::min(d.w, d.h);
		if (s != supportPx)
		{
			if (supportTex) SDL_DestroyTexture(supportTex);
			supportTex = nullptr;
			supportPx = s;
			// the bundle's share/banana-player/kofi-qr.png (the fonts' folder one up); a desktop build's source tree
			PageImage img = decodePageFile(fontsDir + "/../kofi-qr.png", s);
			if (!img.ok()) img = decodePageFile(fontsDir + "/../../packaging/support/kofi-qr.png", s);
			if (img.ok())
			{
				supportTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
				if (supportTex)
				{
					SDL_UpdateTexture(supportTex, nullptr, img.rgba.data(), img.w * 4);
					SDL_SetTextureBlendMode(supportTex, SDL_BLENDMODE_BLEND);
				}
			}
			else fprintf(stderr, "ui: About's QR image not read: %s\n", img.error.c_str());
		}
		if (supportTex)
		{
			SDL_Rect c{d.x + (d.w - s) / 2, d.y + (d.h - s) / 2, s, s};
			SDL_RenderCopy(ren, supportTex, nullptr, &c);
		}
		int x = qr.x + qr.w + 16;
		drawTextMid("Banana Player is free. If you\xE2\x80\x99" "d like to support it:", F_I12, DIM, x, y + 34, 592 - (x - 24));
		drawTextMid("ko-fi.com/tonysong", F_I14, DIM_ON, x, y + 64);
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

	int Ui::volumeTotal(Player& p)
	{
		int s = volumeLevel();
		return p.boost > 100 && s >= 100 ? p.boost : s;
	}

	void Ui::setVolumeTotal(Player& p, int v, bool force)
	{
		v = std::clamp(v, 0, std::max(100, p.settings.volumeMax));
		int sys = std::min(v, 100);
		if ((force || volumePct != sys) && volume.present()) volume.set(sys);
		volumePct = sys;
		p.setBoost(v > 100 ? v : 100);
		levelLabel = "Volume";	// (main shows it: on mpv's top panel or over a reader's pages)
		levelPct = v;
		levelMs = 1500;
		if (v > 120 && !loudWarned)
		{
			loudWarned = true;
			alert("That's very loud", "Over 120% can damage your hearing, and the speakers (headphones too). Keep it short.");
		}
	}

	// ---- Like ----

	bool Ui::likeable(const Player& p) const { return browse->playlists->plStore && p.playing() && PlaylistStore::likable(p.currentPath()); }

	bool Ui::likedNow(const Player& p) const { return browse->playlists->plStore && p.playing() && browse->playlists->plStore->isLiked(p.currentPath()); }

	bool Ui::headerActions(const Player& p) const
	{
		return browse->playlists->plStore && p.playing() && !p.playingPhoto() && PlaylistStore::likable(p.currentPath());
	}

	void Ui::likeCard(Player& p, bool liked, const std::string& title)
	{
		// in the volume card's place: which file it was (a Like pressed as the song changes still says)
		if (liked) p.showNoteCard(G_HEART_ROWS, G_HEART.h, "Added to Liked", title);
		else p.showNoteCard(G_HEART_O_ROWS, G_HEART_O.h, "Removed from Liked", title);
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

	void Ui::Playlists::openAddOne(const std::string& path, const std::string& title, double duration, Player& p)
	{
		if (!plStore || !PlaylistStore::likable(path)) return;	// (songs and videos: never a photo)
		pickerOne = path;
		pickerOneTitle = title.empty() ? entryTitle(path) : title;
		pickerOneDuration = duration;
		openPicker("", p, true);
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

	bool Ui::playOnShown() const { return !incognito && outChoices().size() > 1; }

	std::string Ui::pageName() const
	{
		const Session& s = SessionHost::get().session();
#ifdef BP_WEB
		if (web && s.out.kind == OutKind::Browser) return web->tabName(s.out.tab);
#endif
		return "Browser";
	}

	std::string Ui::outputName() const { return SessionHost::get().pageIsOutput() ? pageName() : "Handheld"; }

	void Ui::playOnSend(const OutChoice& c, Player& p)
	{
		SessionHost& sh = SessionHost::get();
		std::string cur = sh.pageIsOutput() ? sh.session().out.tab : "";
		if (c.tab == cur) return;
		fprintf(stderr, "ui: play on %s\n", c.tab.empty() ? "the handheld" : c.tab.c_str());
		sh.sendTo(c.tab.empty() ? handheldOut() : browserOut(c.tab));
		if (c.tab.empty()) p.osd(sh.session().playing ? "Playing on the handheld" : "On the handheld");
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
		if (!browse->playlists->plStore) return false;
		if (browse->playlists->plStore->isLiked(path) != on)
		{
			// (Liked's own edits waiting, if it's open, are written first so this one doesn't lose them)
			browse->playlists->plFlush(p);
			Playlist before;
			int skipped = 0;
			bool playsLiked = p.playing() && p.source().kind == SourceKind::Playlist && p.source().id == browse->playlists->plStore->likedFile();
			if (playsLiked) browse->playlists->plStore->load(browse->playlists->plStore->likedFile(), before, skipped);
			if (!browse->playlists->plStore->setLiked(path, title, duration, on, at, dropped))
			{
				if (browse->playlists->plStore->likable(path))
					p.showNoteCard(G_ALERT_ROWS, G_ALERT.h, "Couldn't save Liked", "The card may be full or locked", true);
				return browse->playlists->plStore->isLiked(path);
			}
			// Liked is what plays: its order moved (a new one goes first), the player told as an edit
			if (playsLiked)
			{
				Playlist after;
				if (browse->playlists->plStore->load(browse->playlists->plStore->likedFile(), after, skipped))
				{
					std::vector<int> remap(before.entries.size(), -1);
					std::vector<char> used(after.entries.size(), 0);
					for (size_t i = 0; i < before.entries.size(); i++)
						for (size_t j = 0; j < after.entries.size(); j++)
							if (!used[j] && after.entries[j].path == before.entries[i].path) { remap[i] = int(j); used[j] = 1; break; }
					browse->playlists->plTellPlayer(after, remap, p);
				}
			}
			// the pages that show Liked (the list's count, its items) read it again
			browse->playlists->selSerial++;
			if (browse->browseDir == plDirFor(browse->playlists->plStore->likedFile()))
			{
				browse->playlists->plCur = Playlist();
				browse->playlists->plLoaded = browse->playlists->plStore->load(browse->playlists->plStore->likedFile(), browse->playlists->plCur, skipped);
				browse->playlists->plRefresh(p);
			}
			else browse->playlists->plWebListChanged(p);
		}
		return browse->playlists->plStore->isLiked(path);
	}
}
