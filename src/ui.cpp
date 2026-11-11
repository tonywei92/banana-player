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
#include "durworker.h"

namespace bp
{
	constexpr Ui::Rect Ui::BAR;

	bool Ui::init(SDL_Window* window, const std::string& dir)
	{
		fontsDir = dir;
		panel.init();
		// tests: a fake /sys/class/backlight and /data/banana/backlight
		if (const char* d = getenv("BP_BACKLIGHT_DIR")) backlight.sysDir = d;
		if (const char* f = getenv("BP_BACKLIGHT_STORE")) backlight.store = f;
		if (const char* v = getenv("BP_VOLUME_SCRIPT")) volume.script = v;
		ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
		if (!ren) ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
		if (!ren) return false;
		int ow, oh;
		SDL_GetRendererOutputSize(ren, &ow, &oh);
		sx = ow / (float)W;
		sy = oh / (float)H;
		// the text is drawn at the panel's own size rather than scaled up: Pixelify Sans stays sharp
		std::string path = fontsDir + "/PixelifySans-Regular.ttf";
		for (int i = 0; i < 3; i++)
		{
			fonts[i] = TTF_OpenFont(path.c_str(), (int)std::lround(SIZES[i] * sy));
			if (!fonts[i])
			{
				SDL_Log("font %s: %s", path.c_str(), TTF_GetError());
				return false;
			}
		}
		for (int i = 0; i < 3; i++)
		{
			int minx, maxx, miny, maxy, adv;
			capHeight[i] = TTF_GlyphMetrics(fonts[i], 'H', &minx, &maxx, &miny, &maxy, &adv) == 0 ? maxy : TTF_FontAscent(fonts[i]) * 3 / 4;
		}
		// the web dashboard's PIN, big (a font of its own: the three sizes are the page's text)
		pinFont = TTF_OpenFont(path.c_str(), (int)std::lround(56 * sy));
		static const char* FALLBACKS[] = {"fusion-pixel-12px-proportional-zh_hans.ttf", "NotoSansThai.ttf",
			"NotoSansArabic.ttf", "NotoSansHebrew.ttf", "NotoSansDevanagari.ttf"};
		for (const char* name : FALLBACKS)
		{
			std::array<TTF_Font*, 3> f = {};
			for (int i = 0; i < 3; i++)
			{
				// Fusion Pixel is a 12-pixel font: at whole multiples of 12 its pixels stay square
				int px = (int)std::lround(SIZES[i] * sy);
				if (name == FALLBACKS[0]) px = std::max(12, (int)std::lround(SIZES[i] * sy / 12.0) * 12);
				f[i] = TTF_OpenFont((fontsDir + "/" + name).c_str(), px);
			}
			if (f[0] && f[1] && f[2]) fallbacks.push_back(f);
			else for (auto* x : f) if (x) TTF_CloseFont(x);
		}
		return true;
	}

	Ui::~Ui()
	{
		if (photoTex) SDL_DestroyTexture(photoTex);
		for (auto& c : cache) SDL_DestroyTexture(c.second);
		for (auto& c : coverTex)
			if (c.second.tex) SDL_DestroyTexture(c.second.tex);
		for (auto* t : corners)
			if (t) SDL_DestroyTexture(t);
		for (auto* f : fonts)
			if (f) TTF_CloseFont(f);
		if (pinFont) TTF_CloseFont(pinFont);
		for (auto& f : fallbacks)
			for (auto* x : f) TTF_CloseFont(x);
		if (ren) SDL_DestroyRenderer(ren);
	}

	bool Ui::press(Button b, Player& p)
	{
		sameBrightness = p.settings.sameBrightness;
		mangaOn = p.settings.mangaOn;
		webInputAt = SDL_GetTicks();
		// dark (Screen off, or off by itself): over what plays, the playback keys work blind and leave it dark (play
		// and pause, seek, next and previous, chapters, speed, like: issue #3, watching with the bottom screen off);
		// any other button, or anywhere else (a list: A there would open whatever the cursor is on), brings the
		// panel back and does nothing else, as a touch does. F + START still quits, in main
		if (!panel.on())
		{
			bool playKey = b == Button::A || b == Button::Left || b == Button::Right || b == Button::L || b == Button::R ||
						   b == Button::L2 || b == Button::R2 || b == Button::Up || b == Button::Down || b == Button::L3;
			bool overPlayer = page == Page::Main && !inMenu && !quickOpen && !kbOpen && p.playing() && !p.playingPhoto();
			if (!(playKey && overPlayer))
			{
				panel.set(true);
				return false;
			}
		}
		if (quickOpen) return quickKey(b, p);
		if (kbOpen)
		{
			kbPress(b);
			if (eqNameSubmit) eqNameDone(p);
			if (plNameSubmit) plNameDone(p);
			return false;
		}
		// Add to... over what plays (or the Playing tab); the browser's is taken below, in its turn
		if (pickerOpen && page != Page::Browse)
		{
			pickerKey(b, p);
			return false;
		}
		// Play on, over START › Playing
		playOnOpen = playOnOpen && inMenu && page == Page::Playing && playingTabShown(p);
		if (playOnOpen)
		{
			playOnKey(b, p);
			return false;
		}
		displayOpen = displayOpen && displayShown(p);	// (the song gone under it: closed)
		if (displayOpen) return displayKey(b, p);
		// a menu (the Options one, a row's hold, the mini bar's) takes the buttons first, on whatever page it is over
		// (the mini bar's opens on pages whose own keys would otherwise eat them)
		if (ctxOpen)
		{
			switch (b)
			{
			case Button::Up: ctxCursor = (ctxCursor + (int)ctxItems.size() - 1) % (int)ctxItems.size(); ctxArmed = false; break;
			case Button::Down: ctxCursor = (ctxCursor + 1) % (int)ctxItems.size(); ctxArmed = false; break;
			case Button::A: chooseBrowseMenu(ctxCursor, p); break;
			case Button::B: case Button::X: case Button::Y: ctxOpen = false; break;
			default: break;
			}
			return false;
		}
		// a page the output waits for a tap there to start the sound (the top panel says so): B plays here instead
		if (b == Button::B && !inMenu && (page == Page::Main || page == Page::Browse) && SessionHost::get().pageWaits() &&
			!ctxOpen && !pickerOpen && !popupShown(p) && selMode == SEL_NONE && !addPending.serial)
		{
			takeOver(p);
			return false;
		}
		// Button hotkeys off: the buttons do nothing over a playing video (touch does it all); the pages opened from
		// there by touch still take them
		if (page == Page::Main && !p.settings.buttonHotkeys) return false;
		// L2/R2: the chapter before or after, over what plays and its two pages (the Chapters page's mark follows).
		// Not in START's menu, not over a photo (its keys are its own), and nothing with Button hotkeys off: they
		// have no touch button to stand for
		// (the manga pages have their own: the reader's chapter before or after, the series page's Download)
		if ((b == Button::L2 || b == Button::R2) && page != Page::Manga && page != Page::MangaSeries && page != Page::MangaRead)
		{
			if (p.settings.buttonHotkeys && !inMenu && p.playing() && !p.playingPhoto() && p.error.empty() &&
				(page == Page::Main || page == Page::Tracks || page == Page::Chapters))
			{
				bool pred = predicting(p);
				int t = p.stepChapter(b == Button::L2 ? -1 : 1, pred ? shownPosition(p) : -1);
				if (pred && t >= 0 && t < (int)p.chapters.size()) noteSeek(p, p.chapters[t].time);
				else if (pred && t == CHAPTER_FILE_START) noteSeek(p, 0);
			}
			return false;
		}
		if (inMenu)
		{
			if (b == Button::L || b == Button::R)
			{
				// round, in the order shown
				auto tabs = menuTabs(p);
				int n = (int)tabs.size(), at = 0;
				for (int i = 0; i < n; i++)
					if (tabs[i] == menuTab) at = i;
				showTab(tabs[(at + (b == Button::R ? 1 : n - 1)) % n], p);
				return false;
			}
			// B in the subtitle style opened from the Subtitles list: back to that list
			if (b == Button::B && page == Page::Settings && setGroup == GROUP_SUBS && styleFromSubs)
			{
				styleFromSubs = false;
				inMenu = false;
				openTracks(TRACKS_SUBS, p);
				cursor = (int)rows(p).size() - 1;	// on Subtitle style, where it was
				scroll = std::max(0, cursor - ROWS_SHOWN + 1);
				return false;
			}
			// B in a settings group: back to the groups
			if (b == Button::B && page == Page::Settings && setGroup >= 0)
			{
				cursor = setGroup;
				setGroup = -1;
				return false;
			}
			if (b == Button::Start || b == Button::B) { closeMenu(p); return false; }
			if (page == Page::Exit)
			{
				if (b == Button::A) quitRequested = true;
				return false;
			}
			// the tab's page takes the rest (Recent's list, the settings' rows, About's scrolling)
		}
		if (page == Page::Sound) return soundKey(b, p);
		if (page == Page::Web) return webKey(b, p);
		if (page == Page::Playing) return playingKey(b, p);
		// Manage extensions and the manga reader's pages: their own keys (the sheet over the first)
		if (page == Page::Extensions || page == Page::Manga) return extKey(b, p);
		if (page == Page::MangaSeries || page == Page::MangaRead) return mangaKey(b, p);
		if (page == Page::MangaSources) return mangaSourcesPageKey(b, p);
		if (page == Page::MangaSettings) return mangaSettingsKey(b, p);
		if (page == Page::Settings)
		{
			auto rows = settingRows();
			int n = setGroup < 0 ? GROUPS : (int)rows.size();
			cursor = std::min(cursor, n - 1);	// a group's rows can change (the updater's)
			// in a group: Y (the west button) to the one before, X to the one after (L/R are the menu's tabs)
			if (setGroup >= 0 && (b == Button::X || b == Button::Y))
			{
				stepGroup(b == Button::X ? 1 : -1);
				return false;
			}
			// the groups: tiles two to a row, the cursor moved as on a grid
			if (setGroup < 0 && (b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right))
			{
				if (b == Button::Up && cursor >= 2) cursor -= 2;
				else if (b == Button::Down) cursor = cursor + 2 < n ? cursor + 2 : cursor / 2 < (n - 1) / 2 ? n - 1 : cursor;
				else if (b == Button::Left && cursor % 2 == 1) cursor--;
				else if (b == Button::Right && cursor % 2 == 0 && cursor + 1 < n) cursor++;
				return false;
			}
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			case Button::Left: if (setGroup >= 0 && rows[cursor] != SOUND_ROW) changeSetting(rows[cursor], -1, p); break;
			case Button::Right: case Button::A:
				if (setGroup < 0) openGroup(cursor);
				else if (rows[cursor] == SOUND_ROW) { openSound(p, true); return false; }
				else changeSetting(rows[cursor], 1, p);
				break;
			// the mini bar's right end says SELECT: back to what plays (Settings has no SELECT of its own)
			case Button::Select: if (nowPlayable(barPlayer(p))) goNowPlaying(p); break;
			case Button::B:
				if (setGroup >= 0) { cursor = setGroup; setGroup = -1; }
				else page = home(p);
				break;
			case Button::Start: page = home(p); break;
			default: break;
			}
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + SET_SHOWN) scroll = cursor - SET_SHOWN + 1;
			return false;
		}
		if (page == Page::Info || page == Page::About)
		{
			int maxScroll = std::max(0, (int)infoLines.size() - INFO_SHOWN);
			switch (b)
			{
			case Button::Up: scroll = std::max(0, scroll - 1); break;
			case Button::Down: scroll = std::min(maxScroll, scroll + 1); break;
			case Button::Left: scroll = std::max(0, scroll - INFO_SHOWN); break;
			case Button::Right: scroll = std::min(maxScroll, scroll + INFO_SHOWN); break;
			case Button::B: case Button::A:
				// About as the menu's tab: nothing to go back to (B closes the menu)
				if (page == Page::About && inMenu) break;
				if (page == Page::About) { page = Page::Settings; setGroup = -1; cursor = scroll = 0; }
				else page = home(p);
				break;
			default: break;
			}
			return false;
		}
		if (popupShown(p))
		{
			int n = (int)popupItems().size();
			popupCursor = std::min(popupCursor, n - 1);
			switch (b)
			{
			case Button::Up: popupCursor = (popupCursor + n - 1) % n; break;
			case Button::Down: popupCursor = (popupCursor + 1) % n; break;
			case Button::A: choosePopup(popupCursor, p); break;
			case Button::B: choosePopup(n > 1 ? 1 : 0, p); break;	// Later (or Hide, OK)
			default: break;
			}
			return false;
		}
		// a network folder read before it's added: B cancels, nothing else
		if (page == Page::Browse && addPending.serial)
		{
			if (b == Button::B) cancelAdd(p);
			return false;
		}
		if (page == Page::Browse && pickerOpen)
		{
			pickerKey(b, p);
			return false;
		}
		// something playing behind the browser: SELECT goes back to it, L3 likes it (a page's own keys don't use them)
		if (page == Page::Browse && !inMenu && selMode == SEL_NONE && browseDir != FORMDIR)
		{
			if (b == Button::Select && nowPlayable(p)) { goNowPlaying(p); return false; }
			if (b == Button::L3 && p.playing() && !p.playingPhoto()) { likeNow(p); return false; }
		}
		if (page == Page::Browse && !inMenu && selMode != SEL_NONE && selKey(b, p)) return false;
		if (page == Page::Browse && !inMenu && atHome()) return homeKey(b, p);
		// All recent: X Home's options, Y a file's menu (as on its card)
		if (page == Page::Browse && !inMenu && browseDir == RECENT && b == Button::X) { openHomeOptions(); return false; }
		if (page == Page::Browse && !inMenu && browseDir == RECENT && b == Button::Y)
		{
			if (cursor >= 0 && cursor < (int)browseRows.size() && browseRows[cursor].id == 2) openRecentMenu(browsePaths[cursor]);
			return false;
		}
		if (page == Page::Browse && !inMenu && b == Button::X) { openBrowseMenu(cursor, MENU_X); return false; }
		if (page == Page::Browse && !inMenu && b == Button::Y) { openBrowseMenu(cursor, MENU_Y); return false; }
		// the form: LEFT/RIGHT step a choice (Type, Sign in, FTPS); L/R don't leave it for the other tab
		if (page == Page::Browse && !inMenu && browseDir == FORMDIR)
		{
			if (b == Button::L || b == Button::R) return false;
			if ((b == Button::Left || b == Button::Right) && cursor >= 0 && cursor < (int)browseRows.size())
			{
				formChange(browseRows[cursor].id, b == Button::Left ? -1 : 1);
				return false;
			}
		}
		if (page == Page::Browse && !inMenu && browseDir == FINDNET && (b == Button::L || b == Button::R)) return false;
		if (page == Page::Browse && !inMenu && (b == Button::L || b == Button::R))
		{
			// a section each way, no wrap, at its top (All recent is none's)
			int t = browseTab + (b == Button::R ? 1 : -1);
			if (browseDir != RECENT && t >= 0 && t < BROWSE_TABS) openSection(t, p);
			return false;
		}
		// the Extensions drawer: A opens what's on (or Manage extensions with none on), START Manage extensions
		if (page == Page::Browse && !inMenu && browseDir == EXTDIR && extDrawerKey(b, p)) return false;
		if (page == Page::Browse && b == Button::B)
		{
			// a section's top: Home, on its tile (All recent: on its card)
			if (atFirstPage())
			{
				if (browseDir == RECENT) goHome(HOME_STRIP, (int)recent.size(), p);
				else goHome(HOME_GRID, browseTab, p);
				return false;
			}
			browseUp(p);
			return false;
		}
		// (on what plays behind it, as over the player)
		if (page == Page::Browse && b == Button::Start) { openMainMenu(p, playingTabShown(p) ? TAB_PLAYING : TAB_SETTINGS); return false; }
		if (page == Page::Tracks || (page == Page::Chapters && !p.chapters.empty()))
		{
			// the tabs: L the one before, R the one after, as the browser's sections go; Y and X as well (the west and
			// east buttons), X having opened the page
			if (b == Button::Y || b == Button::L) { stepTracksTab(-1, p); return false; }
			if (b == Button::X || b == Button::R) { stepTracksTab(1, p); return false; }
		}
		if (page == Page::Chapters && p.chapters.empty())
		{
			// (the list can also empty under it: the next file has none)
			if (b == Button::B) page = home(p);
			return false;
		}
		if (page != Page::Main)
		{
			// (the browser's Up row is row -1: first; n counts it)
			int first = listFirst(), n = (int)rows(p).size() - first;
			cursor = std::min(cursor, n - 1 + first);
			switch (b)
			{
			case Button::Up: cursor = (cursor - first + n - 1) % n + first; break;
			case Button::Down: cursor = (cursor - first + 1) % n + first; break;
			// a page at a time (the browser): LEFT/RIGHT, and L/R as BananaOS's lists jump
			case Button::Left: cursor = std::max(first, cursor - ROWS_SHOWN); break;
			case Button::Right: cursor = std::min(n - 1 + first, cursor + ROWS_SHOWN); break;
			case Button::A: choose(cursor, p); break;
			case Button::B: page = home(p); break;
			default: break;
			}
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + ROWS_SHOWN) scroll = cursor - ROWS_SHOWN + 1;
			return false;
		}
		if (p.playingPhoto() && photoKey(b, p)) return false;
		// a file that didn't open: nothing to pause, choose or speed up (Prev, Next, Up next, the menu and leaving still go)
		if (!p.error.empty() && (b == Button::A || b == Button::X || b == Button::Select || b == Button::Up || b == Button::Down))
			return false;
		switch (b)
		{
		case Button::A:
			// a page plays it: the page's play/pause as A lets go (release()), here only once held (tick())
			if (pageDrives(p)) { if (!playHoldAt) { playHoldAt = SDL_GetTicks(); playHoldTouch = false; } break; }
			// the page is the output but can't hear (its tab gone a moment): play here
			if (pageIsOut()) { takeOver(p); break; }
			SessionHost::get().play(p.paused);	// (mpv answers the flip later: paused is still the old state)
			p.togglePause();
			break;
		// out of the player at once: what plays goes on behind the browser (nothing is lost, so a pocket's press costs
		// nothing); a photo closes
		case Button::B: return true;
		// the play mode (a song or a video)
		case Button::X: musicKey(b, p); break;
		// List: a playlist's song opens that playlist's own page (its edits, its order), what plays going on behind;
		// a folder, Recent or a selection: Up next
		case Button::Y:
			if (p.source().kind == SourceKind::Playlist && !p.source().id.empty() && !p.playingPhoto())
			{
				showSourceAfterLeave = true;
				return true;
			}
			openUpNext(p);
			break;
		// the menu opens on what plays (Like, Add to playlist...): Settings is a tab along
		case Button::Start: openMainMenu(p, playingTabShown(p) ? TAB_PLAYING : TAB_SETTINGS); break;
		// the stick pressed in (the RG DS Plus): Like, at once (over a photo it turns it, above)
		case Button::L3: likeNow(p); break;
		// a song: the Display popup; a video: Tracks (Chapters, Subtitles, Audio, Picture)
		case Button::Select: if (!musicKey(b, p)) openTracksPage(p); break;
		// (a page playing it: from where it should be by now; the jump is passed on to the page)
		case Button::Left:
		case Button::Right:
			if (predicting(p))
			{
				double t = std::clamp(shownPosition(p) + (b == Button::Left ? -BACK_S : FORWARD_S), 0.0, p.duration);
				p.seekFraction(t / p.duration);
				noteSeek(p, t);
			}
			else p.seekRelative(b == Button::Left ? -BACK_S : FORWARD_S);
			break;
		case Button::L:
		case Button::R:
			// the session's Step (Previous RESTART_S in restarts; a burst counts every press; a page playing it moves on
			// there, the copy here following held). What the session doesn't have (it disagrees: logged), as before
			if (SessionHost::get().stepsHere(p)) { SessionHost::get().step(b == Button::R ? 1 : -1); break; }
			if (b == Button::L ? !p.back() : !p.next()) p.osd(b == Button::L ? "This is the first one" : "This is the last one");
			break;
		case Button::Up: case Button::Down:
		{
			double s = stepSpeed(p.speed, b == Button::Up ? 1 : -1);
			p.setSpeed(s);
			p.osd("Speed " + speedLabel(s));
			break;
		}
		default: break;
		}
		return false;
	}

	void Ui::tick(Player& p)
	{
		mangaVideoGuard(p);
		// the rows' durations: none probed while a file opens; the rows on screen first
		if (durWorker)
		{
			durWorker->setBusy(p.loading());
			if (page == Page::Browse && scroll != durLo)
			{
				durLo = scroll;
				durWorker->focus(scroll, scroll + ROWS_SHOWN);
			}
		}
		// a playlist's edits, written once they've stopped for a second
		if (plDirty && SDL_GetTicks() - plDirtyAt >= 1000) plFlush(p);
		// the manga reader's page: the place recorded, the store written once a second while reading
		if (page == Page::MangaRead) mangaTick();
		// the Sources page: the repositories' events become toasts, and what was typed in is added
		else if (page == Page::MangaSources) mangaSourcesTick(p);
		// the Downloads tab: an armed delete goes, a lost worker answer waits for the reader to be on again
		else if (page == Page::Manga && mangaTab == MT_DOWNLOADS) mangaDownloadsTick(p);
		// the dashboard starts downloads and asks for a series' chapters: their worker answers are taken here too,
		// since no manga page may be drawn on the handheld while the browser is the one using the reader
		if (p.settings.mangaOn && mangaNet)
		{
			mangaBrowsePoll(p);
			mangaDlAskPump();
			mangaNotesTick(p);
		}
		// the web's equalizer: what mpv keeps, and settings.conf, once its sliders stop for a second
		if (eqWebDirtyAt && SDL_GetTicks() - eqWebDirtyAt >= 1000)
		{
			eqWebDirtyAt = 0;
			p.commitEq();
		}
		// A or the Play button held while a page plays the file: here instead, once, as the hold reaches its length (not
		// as it lets go). Gone off the player (B, the menu), or the page gone: nothing
		// (the mini bar's play mark holds the same way, on any page that has the bar: what it plays is main's player)
		bool holdHere = (page == Page::Main && !inMenu) || (playHoldMini && miniBarShown(p));
		if (playHoldAt && (!holdHere || !pageDrives(barPlayer(p)))) playHoldAt = 0;
		if (!playHoldAt) playHoldMini = false;
		if (playHoldAt && SDL_GetTicks() - playHoldAt >= PLAY_HOLD_MS)
		{
			playHoldAt = 0;
			playHoldMini = false;
			if (playHoldTouch) { pressedBtn = -1; swallowTouch = true; }	// the finger's lift presses nothing
			takeOver(barPlayer(p));
		}
		// a finger held on the mini bar: its menu (the lift that follows chooses nothing)
		if (miniHoldAt && (pressedBtn == MINI_BAR || pressedBtn == MINI_PP) && SDL_GetTicks() - miniHoldAt >= 600)
		{
			miniHoldAt = 0;
			pressedBtn = -1;
			swallowTouch = true;
			if (miniBarShown(p)) openNowMenu(p);
		}
		if (landFlashUntil && SDL_GetTicks() >= landFlashUntil) landFlashUntil = 0;
		if (listPulseAt && SDL_GetTicks() - listPulseAt >= LIST_PULSE_MS) listPulseAt = 0;
		// Home's Recent: listed again when what plays changes (the next song started by itself: its card comes first)
		if (page == Page::Browse && !inMenu && atHome() && !homeTouch && !ctxOpen && p.currentPath() != homeListedFor)
			enterFolder(HOMEDIR, p);
		// the hand-off from a browser: its position, once the file it opened knows its length (5 s at most)
		if (!seekPath.empty())
		{
			// (a play request still to be opened by main: not yet the file; a photo up: this is the photo viewer, the seek
			// waits for the player)
			if (!playRequest.paths.empty() || p.playingPhoto()) {}
			else if (SDL_GetTicks() > seekUntil || p.currentPath() != seekPath || !p.error.empty()) seekPath.clear();
			else if (p.loaded && p.duration > 0)
			{
				p.seekFraction(std::clamp(seekTo / p.duration, 0.0, 1.0));
				seekPath.clear();
			}
		}
		// Screen off after (Settings): a song or video playing, nothing touched or pressed for that long, nothing open
		// that waits for an answer: the bottom panel dark, as its Screen off button does (a touch or a page's button
		// brings it back). Paused, it stays: someone is about to act. Never while a picture is looked at, whatever
		// plays meanwhile: a photo (panned here) or a comic in the manga reader (its pages are dragged here, and a
		// reader reads longer than any timeout without touching)
		int offSecs = screenOffSeconds(p.settings.screenOffAfter);
		bool viewing = p.playingPhoto() || readerStatus.open;
		if (offSecs > 0 && panel.on() && p.playing() && !viewing && !p.paused && !p.loading() && p.error.empty() &&
			!kbOpen && !quickOpen && !popupShown(p) && !dragging && SDL_GetTicks() - webInputAt >= (Uint32)offSecs * 1000)
			panel.set(false);
		if (page == Page::Web)
		{
			// the address can change (another Wi-Fi), and the page is left up: read again every 10 s; and, as the
			// photo of a QR code needs no screen after, the bottom panel dark after 90 s without a touch or a button
			if (SDL_GetTicks() - webIpAt >= 10000) readWebIp();
			if (panel.on() && webView(p) == WEB_ON_VIEW && SDL_GetTicks() - webInputAt >= 90000) panel.set(false);
		}
		// a dragged row near the list's top or bottom: the list scrolls under it, a row each 150 ms
		if (plDrag.on && page == Page::Browse && isPlDir(browseDir) && SDL_GetTicks() - plDrag.scrollAt >= 150)
		{
			int n = (int)browseRows.size();
			int was = scroll;
			if (plDrag.y < ROW_Y + 24) scroll = std::max(0, scroll - 1);
			else if (plDrag.y > ROW_Y + ROWS_SHOWN * ROW_H - 24) scroll = std::min(std::max(0, n - ROWS_SHOWN), scroll + 1);
			if (scroll != was) plDrag.scrollAt = SDL_GetTicks();
		}
		// a playlist's network item: its folder listed for its subtitles and cover (once a play; netListed answers)
		if (std::string dir = p.netFolderWanted(); !dir.empty() && !sideAsked.count(dir))
		{
			sideAsked.insert(dir);
			NetPath np;
			const NetServer* sv = netStore && parseNetPath(dir, np) ? netStore->find(np.server) : nullptr;
			if (sv && netWorker)
			{
				sidePending[++sideSerial] = dir;
				netWorker->list(sideSerial, *sv, netStore->password(*sv), dir);
			}
			else p.netFolderListed(dir, {}, false);
		}
		stripStep();
		if (likePopAt && SDL_GetTicks() - likePopAt >= 200) likePopAt = 0;
		if (addFlashUntil && SDL_GetTicks() >= addFlashUntil) addFlashUntil = 0;
		if (!flickAt) return;
		if (!p.playingPhoto() || page != Page::Main || panTouch)
		{
			flickAt = 0;
			return;
		}
		Uint32 now = SDL_GetTicks(), dt = now - flickAt;
		if (!dt) return;
		flickAt = now;
		PhotoScale sc = p.photoScaleNow();
		const Player::PhotoView& v = p.photo();
		double x0 = v.panX, y0 = v.panY;
		Rect tr = thumbRect(p);
		if (p.settings.dragBox) p.setPhotoView(v.zoom, x0 - flickVX * dt / tr.w, y0 - flickVY * dt / tr.h);
		else p.setPhotoView(v.zoom, x0 + flickVX * dt / PAN_W / sc.x, y0 + flickVY * dt / PAN_H / sc.y);
		// slowing, and stopped at an edge
		double decay = std::exp(-(double)dt / 300);
		flickVX = p.photo().panX == x0 ? 0 : flickVX * decay;
		flickVY = p.photo().panY == y0 ? 0 : flickVY * decay;
		if (std::hypot(flickVX, flickVY) < 0.02) flickAt = 0;
	}

	bool Ui::act(int id, Player& p)
	{
		// "<" over what plays closes it at once (the place kept); B and Close ask twice, a pocket's press being one
		if (id == BACK_ICON && page == Page::Main && p.playing()) return true;
		// elsewhere (a page over what plays, START's menu): B, back a step
		if (id == BACK_ICON && (inMenu || (page != Page::Browse && page != Page::Main))) return pressAnyway(Button::B, p);
		// Main's Stop (over a photo: Close); "<" elsewhere: B
		if (id == QUIT_BTN)
		{
			if (p.playingPhoto()) return true;
			stopPressed = true;
			return false;
		}
		// adding files: the header's key is a ✕ that cancels at once (B goes up a folder there)
		if (id == BACK_ICON && page == Page::Browse && (selMode == SEL_ADD || selMode == SEL_PICK)) { selBack(p, true); return false; }
		if (id == BACK_ICON) return pressAnyway(Button::B, p);
		// the mini bar: the player, or its play mark
		if (id == MINI_BAR) { goNowPlaying(p); return false; }
		if (id == MINI_PP) { miniToggle(p); return false; }
		if (id == EMPTY_BTN) { doEmptyAction(p); return false; }
		// the select mode's bar: its keys' (Cancel leaves from any folder)
		if (id == SEL_ALL) { selKey(Button::Select, p); return false; }
		if (id == SEL_MOVE) { selKey(Button::Y, p); return false; }
		// (the act itself, not its key: adding, X is Cancel and Y adds)
		if (id == SEL_ACT) { removeArmed = removeArmed && selMode == SEL_EDIT; dropArmed = 0; selAct(p); return false; }
		// (Cancel shows X's key, so it asks twice as X does; the header's ✕ leaves at once)
		if (id == SEL_CANCEL) { selBack(p, false); return false; }
		if (id == MOVE_CANCEL || id == MOVE_DONE) { moveEnd(id == MOVE_DONE, p); return false; }
		if (id == BUSY_CANCEL) { cancelAdd(p); return false; }
		if (id >= EXT_TILE && id <= EXT_DONE) return extAct(id, p);
		if (id == LIKE_BTN) { likeNow(p); return false; }
		if (id == ADD_BTN)
		{
			if (p.playing()) openAddOne(p.currentPath(), p.title, p.loaded ? p.duration : -1, p);
			return false;
		}
		if (id >= PLAY_ROW && id < PLAY_ROW + 7) return playingChoose(id - PLAY_ROW, p);
		// a song's line under the buttons: the Sound page
		if (page == Page::Main && p.playingAudio() && id == LINE1_BTN)
		{
			openSound(p, false);
			return false;
		}
		// a video's lines and the chapter line: their tab of Tracks
		if (id == LINE1_BTN || id == LINE2_BTN) { openTracks(id == LINE1_BTN ? TRACKS_AUDIO : TRACKS_SUBS, p); return false; }
		if (id == CHAPTER_BTN) { openTracks(TRACKS_CHAPTERS, p); return false; }
		if (id == GROUP_PREV || id == GROUP_NEXT) { stepGroup(id == GROUP_NEXT ? 1 : -1); return false; }
		if (id == SCREEN_BTN) { panel.set(false); return false; }
		if (id == EXIT_BTN) { quitRequested = true; return false; }
		if (id == LOCK_BTN)
		{
			touchLocked = true;
			p.osd("Touch locked: hold the lock 1 s to unlock");
			return false;
		}
		if (id >= PHOTO_BTN && id < PHOTO_BTN + PHOTO_BTNS)
		{
			static const Button PHOTO_KEYS[PHOTO_BTNS] = {Button::L, Button::R, Button::Down, Button::Up, Button::L3,
				Button::Select, Button::None, Button::Start};
			Button b = PHOTO_KEYS[id - PHOTO_BTN];
			if (b != Button::None) return pressAnyway(b, p);
			id = INFO_BTN;
		}
		if (id == INFO_BTN)
		{
			page = Page::Info;
			cursor = scroll = 0;
			infoAt = 0;	// read at once
			return false;
		}
		// the grid's Like and Add (L3's place and the one with no key)
		if (page == Page::Main && (id == 1 || id == 3)) return act(id == 1 ? LIKE_BTN : ADD_BTN, p);
		Button b = GRID[id / 5][id % 5].button;
		if (b != Button::Up) return pressAnyway(b, p);
		if (!p.error.empty()) return false;
		// a tap on the speed goes round its choices
		double s = stepSpeed(p.speed, 1, true);
		p.setSpeed(s);
		p.osd("Speed " + speedLabel(s));
		return false;
	}

	bool Ui::pointer(const Pointer& pt, Player& p)
	{
		int x = (int)(pt.x * W), y = (int)(pt.y * H);
		webInputAt = SDL_GetTicks();
		mangaOn = p.settings.mangaOn;
		if (kbOpen && panel.on())
		{
			kbPointer(pt, x, y);
			if (eqNameSubmit) eqNameDone(p);
			if (plNameSubmit) plNameDone(p);
			return false;
		}
		// dark: a touch anywhere brings the panel back, and does nothing else (the finger can't see what's there)
		if (!panel.on())
		{
			if (pt.kind == Pointer::Down)
			{
				panel.set(true);
				swallowTouch = true;
			}
			return false;
		}
		if (swallowTouch)
		{
			if (pt.kind == Pointer::Up) swallowTouch = false;
			return false;
		}
		auto barFraction = [&] { return std::clamp((x - BAR.x) / (double)BAR.w, 0.0, 1.0); };

		if (quickOpen) return quickPointer(pt, x, y, p);
		// Add to... over what plays (or the Playing tab) as over the browser
		if (pickerOpen) return pickerPointer(pt, x, y, p);
		playOnOpen = playOnOpen && inMenu && page == Page::Playing && playingTabShown(p);
		if (playOnOpen) return playOnPointer(pt, x, y, p);
		displayOpen = displayOpen && displayShown(p);
		if (displayOpen) return displayPointer(pt, x, y, p);
		// Home's quit dialog takes every finger: a row chooses as it lifts, outside it cancels
		if (page == Page::Browse && !inMenu && atHome() && quitOpen && !popupShown(p)) return homePointer(pt, x, y, p);
		// the header's web dashboard mark: its page, as the finger lifts on it
		if (webIndDown || (pt.kind == Pointer::Down && webInd.w && webInd.hit(x, y) && !ctxOpen && !pickerOpen && !popupShown(p)))
		{
			if (pt.kind == Pointer::Down) webIndDown = true;
			else if (pt.kind == Pointer::Up)
			{
				webIndDown = false;
				if (webInd.hit(x, y) && page != Page::Web) openWeb();
			}
			return false;
		}
		if (ctxOpen)
		{
			// the folder's menu: a row tapped (as the finger lifts), anywhere else closes it
			if (pt.kind == Pointer::Down)
			{
				pressedBtn = -1;
				for (int i = 0; i < (int)ctxItems.size(); i++)
					if (ctxRow(i).hit(x, y)) pressedBtn = 100 + i;
				if (pressedBtn < 0) ctxOpen = false;
			}
			else if (pt.kind == Pointer::Up && pressedBtn >= 100)
			{
				int item = pressedBtn - 100;
				pressedBtn = -1;
				if (item != ctxCursor) ctxArmed = false;
				ctxCursor = item;
				if (ctxRow(item).hit(x, y)) chooseBrowseMenu(item, p);
			}
			return false;
		}
		if (page == Page::Sound) return soundPointer(pt, x, y, p);
		if (page == Page::Web) return webPointer(pt, x, y, p);
		// locked, over a video: Screen off goes through; a hold on the lock (draw() times it) releases it; nothing else
		if (touchLocked && p.playing() && !p.playingPhoto() && page == Page::Main && pressedBtn != SCREEN_BTN)
		{
			if (pt.kind == Pointer::Down && buttonRect(LOCK_BTN).hit(x, y)) lockHoldAt = SDL_GetTicks();
			else if (pt.kind == Pointer::Down && buttonRect(SCREEN_BTN).hit(x, y)) pressedBtn = SCREEN_BTN;
			else if (pt.kind == Pointer::Up) lockHoldAt = 0;	// let go too soon: still locked
			else if (pt.kind == Pointer::Down && SDL_GetTicks() - lockNoticeAt > 3000)
			{
				lockNoticeAt = SDL_GetTicks();
				p.osd("Touch locked: hold the lock 1 s to unlock");
			}
			return false;
		}

		if (popupShown(p))
		{
			// the updater's pop-up: a row tapped (as the finger lifts); elsewhere, nothing (it's a question)
			int n = (int)popupItems().size();
			if (pt.kind == Pointer::Down)
			{
				pressedBtn = -1;
				for (int i = 0; i < n; i++)
					if (popupRow(i).hit(x, y)) pressedBtn = 200 + i;
			}
			else if (pt.kind == Pointer::Up && pressedBtn >= 200)
			{
				int item = pressedBtn - 200;
				pressedBtn = -1;
				if (item < n && popupRow(item).hit(x, y)) choosePopup(item, p);
			}
			return false;
		}
		// a network folder read before it's added: its dialog's Cancel, nothing else
		if (page == Page::Browse && addPending.serial)
		{
			if (pt.kind == Pointer::Down) pressedBtn = buttonRect(BUSY_CANCEL).hit(x, y) ? BUSY_CANCEL : -1;
			else if (pt.kind == Pointer::Up && pressedBtn == BUSY_CANCEL)
			{
				pressedBtn = -1;
				if (buttonRect(BUSY_CANCEL).hit(x, y)) cancelAdd(p);
			}
			return false;
		}
		if (page == Page::Browse && pickerOpen) return pickerPointer(pt, x, y, p);
		// a playlist's row on the finger by its grip: the landing line follows, lifted it's dropped there
		if (plDrag.on)
		{
			plDrag.y = std::clamp(y, 0, H - 1);
			if (pt.kind == Pointer::Up) dragEnd(p);
			return false;
		}
		if (dragging)
		{
			dragFraction = barFraction();
			if (pt.kind == Pointer::Up)
			{
				// the drag's last place, to the frame
				dragging = false;
				p.seekFraction(dragFraction);
				noteSeek(p, dragFraction * p.duration);
			}
			else if (SDL_GetTicks() - scrubAt >= SCRUB_MS && std::fabs(dragFraction - scrubbed) > 0.0005)
			{
				// the picture follows the finger: keyframes only, and no more often than the decoder keeps up with
				scrubAt = SDL_GetTicks();
				scrubbed = dragFraction;
				p.scrub(dragFraction);
			}
			return false;
		}
		if (panTouch) return photoPointer(pt, x, y, p);
		if (homeTouch) return homePointer(pt, x, y, p);
		if (listTouch)
		{
			// a list (the tracks, the browser, the settings, the manga's): a drag scrolls it, a tap acts as the finger
			// lifts
			bool set = page == Page::Settings || page == Page::MangaSettings, groups = page == Page::Settings && setGroup < 0;
			bool info = page == Page::Info || page == Page::About;
			auto srows = set && !groups ? (page == Page::MangaSettings ? mangaSettingRows() : settingRows()) : std::vector<int>();
			int n = groups ? GROUPS : set ? (int)srows.size() : info ? (int)infoLines.size() : (int)rows(p).size();
			int rowH = groups ? 84 : set ? SET_H : info ? INFO_H : ROW_H, shown = set ? SET_SHOWN : info ? INFO_SHOWN : ROWS_SHOWN;
			if (pt.kind == Pointer::Move && (listScrolled || std::abs(y - touchStartY) > 12))
			{
				listScrolled = true;
				holdAt = 0;
				int rowsMoved = (int)std::lround((touchStartY - y) / (double)rowH);
				int first = set || info ? 0 : listFirst();
				scroll = std::clamp(scrollStart + rowsMoved, first, std::max(first, n - shown));
				cursor = std::clamp(cursor, scroll, scroll + shown - 1);
			}
			else if (pt.kind == Pointer::Up)
			{
				listTouch = false;
				holdAt = 0;
				if (listScrolled || info) return false;	// the info page only scrolls
				// a setting: the left of its value (its arrow) steps back, the rest of the row on
				int dir = touchX >= SET_ARROW_X - 12 && touchX < 490 ? -1 : 1;
				if (groups) openGroup(cursor);
				else if (set && page == Page::Settings && srows[cursor] == SOUND_ROW) openSound(p, true);	// a page, not a value
				else if (set) changeSetting(srows[cursor], dir, p);
				else choose(cursor, p);
			}
			return false;
		}
		if (pressedBtn >= 0)
		{
			if (pt.kind != Pointer::Up) return false;
			int id = pressedBtn;
			pressedBtn = -1;
			miniHoldAt = 0;
			// the mini bar's play mark while a page plays the file: let go before the hold took it over here, the
			// page's play / pause
			if (id == MINI_PP && playHoldMini)
			{
				bool on = playHoldAt != 0;
				playHoldAt = 0;
				playHoldMini = false;
				if (on && buttonRect(id).hit(x, y) && pageDrives(barPlayer(p))) toggleBrowser(barPlayer(p));
				return false;
			}
			if (id == PLAY_GRID && page == Page::Main && !inMenu && ((playHoldTouch && playHoldAt) || pageDrives(p)))
			{
				playHoldAt = 0;
				if (buttonRect(id).hit(x, y) && pageDrives(p)) toggleBrowser(p);
				return false;
			}
			// let go off the button: nothing (a finger that slid away changed its mind)
			Rect r = id >= PHOTO_BTN && id < PHOTO_BTN + PHOTO_BTNS ? photoBtn(id - PHOTO_BTN, p) : buttonRect(id);
			return r.hit(x, y) && act(id, p);
		}
		// the mini bar: acts as the finger lifts (a hold: its menu, tick()); its play mark while a page plays the file
		// holds as Main's Play does (here instead). Its down is taken here for every page, the reader's included
		if (pt.kind == Pointer::Down && miniBarShown(p) && MINI_BOX.hit(x, y))
		{
			Player& bp = barPlayer(p);
			pressedBtn = x < MINI_BOX.x + 44 ? MINI_PP : MINI_BAR;
			miniHoldAt = SDL_GetTicks();
			// in fullscreen the bar is the controls sheet's: a touch on it shows the sheet 4 s more
			if (page == Page::MangaRead && readerStatus.fullscreen) mangaCtrlUntil = SDL_GetTicks() + MG_CTRL_MS;
			if (pressedBtn == MINI_PP && pageDrives(bp))
			{
				miniHoldAt = 0;
				playHoldAt = SDL_GetTicks();
				playHoldTouch = true;
				playHoldMini = true;
			}
			return false;
		}
		// the reader's page: its drag area and slider take the finger's moves too (the mini bar's taken above)
		if (page == Page::MangaRead) return mangaPointer(pt, x, y, p);
		// a manga source's grid scrolls with the finger: its moves and lift go to it (its down comes below)
		if (((page == Page::Manga && mangaView == 1) || (page == Page::MangaSeries && mangaSourceSeriesOn())) && pt.kind != Pointer::Down)
			return mangaPointer(pt, x, y, p);
		// the extensions' pages, and the drawer: their own taps. A tile or a row acts as the finger lifts (as Home's
		// do), so the lift is taken before the down-only pages below; the down is taken after the header and mini bar.
		if (pt.kind == Pointer::Up &&
			(page == Page::Extensions || page == Page::Manga || (page == Page::Browse && !inMenu && browseDir == EXTDIR)))
			return extPointer(pt, x, y, p);
		if (pt.kind != Pointer::Down) return false;
		if (inMenu && y < 60)
		{
			// the tab bar: a tab, or "<" (it closes the menu as the finger lifts)
			if (buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
			auto tabs = menuTabs(p);
			for (int t = 0; t < (int)tabs.size(); t++)
				if (menuTabRect(t, (int)tabs.size()).hit(x, y)) showTab(tabs[t], p);
			return false;
		}
		if (page == Page::Exit)
		{
			if (buttonRect(EXIT_BTN).hit(x, y)) pressedBtn = EXIT_BTN;
			return false;
		}
		if (page == Page::Playing)
		{
			// a row acts as the finger lifts on it
			auto rows = playingRows();
			for (int i = 0; i < (int)rows.size(); i++)
				if (buttonRect(PLAY_ROW + i).hit(x, y)) { cursor = i; pressedBtn = PLAY_ROW + i; }
			return false;
		}

		// "<": B (up a folder; twice, out of what plays or, from the first page, out of the player)
		if (backIconShown(p) && buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
		if (emptyAction() && buttonRect(EMPTY_BTN).hit(x, y)) { pressedBtn = EMPTY_BTN; return false; }
		// the extensions' pages, and the drawer: their own taps (the mini bar's, the header's and "<" taken above)
		if (page == Page::Extensions || page == Page::Manga || (page == Page::Browse && !inMenu && browseDir == EXTDIR))
			return extPointer(pt, x, y, p);
		if (page == Page::MangaSeries) return mangaPointer(pt, x, y, p);
		if (page == Page::MangaSources) return mangaPointer(pt, x, y, p);
		if (page == Page::Info || page == Page::About)
		{
			if (y < INFO_Y) return false;
			listTouch = true;
			listScrolled = false;
			touchStartY = y;
			scrollStart = scroll;
			return false;
		}
		if (page == Page::Settings && setGroup >= 0)
			for (int id : {GROUP_PREV, GROUP_NEXT})
				if (buttonRect(id).hit(x, y)) { pressedBtn = id; return false; }
		if (page == Page::Settings || page == Page::MangaSettings)
		{
			int top = SET_Y, h = SET_H;
			bool groups = page == Page::Settings && setGroup < 0;
			auto srows = groups ? std::vector<int>() : page == Page::MangaSettings ? mangaSettingRows() : settingRows();
			int n = groups ? GROUPS : (int)srows.size();
			int row = scroll + (y - top) / h;
			if (groups)
			{
				// a tile
				row = -1;
				for (int g = 0; g < GROUPS; g++)
					if (groupRect(g).hit(x, y)) row = g;
				if (row < 0) return false;
			}
			else if (y < top || y >= top + SET_SHOWN * h || row >= n) return false;
			cursor = row;
			listTouch = true;
			listScrolled = false;
			touchStartY = y;
			touchX = x;
			scrollStart = scroll;
			return false;
		}
		if (page == Page::Browse && !inMenu && selMode != SEL_NONE)
		{
			// selecting: no tabs (the header's "<" was taken above); the bar's buttons, those that can act now
			if (y < 60) return false;
			if (y >= 424)
			{
				int n = selCount(), ticked = 0, files = selFolderFiles(ticked);
				std::vector<std::pair<int, bool>> ids;
				if (moving) ids = {{MOVE_CANCEL, true}, {MOVE_DONE, true}};
				else if (selMode == SEL_EDIT) ids = {{SEL_ALL, !plCur.entries.empty()}, {SEL_MOVE, n > 0 && !plIsLiked(plCur.file)}, {SEL_ACT, n > 0}};
				else if (selMode == SEL_ADD) ids = {{SEL_ALL, files > 0}, {SEL_CANCEL, true}, {SEL_ACT, n > 0}};
				else ids = {{SEL_CANCEL, true}, {SEL_ACT, pickable()}};
				for (auto& id : ids)
					if (id.second && buttonRect(id.first).hit(x, y)) pressedBtn = id.first;
				return false;
			}
			if (moving) return false;
			// Edit: a finger on a row's grip drags it at once
			if (selMode == SEL_EDIT && x >= 576 && y >= ROW_Y && y < ROW_Y + ROWS_SHOWN * ROW_H)
			{
				int i = scroll + (y - ROW_Y) / ROW_H;
				if (selTickable(i)) { dragBegin(i, y, p); return false; }
			}
		}
		if (page == Page::Browse && !inMenu && atHome()) return homePointer(pt, x, y, p);
		if (page == Page::Browse && !inMenu && y < 60)
		{
			// a section's marker: that section (Home and All recent have none)
			if (browseDir != RECENT)
				for (int t = 0; t < BROWSE_TABS; t++)
					if (pipRect(t, p).hit(x, y)) openSection(t, p);
			return false;
		}
		if ((page == Page::Tracks || page == Page::Chapters) && y < 60)
		{
			// a tab switches as the finger lands (as the browser's do)
			auto tabs = tracksTabs(p);
			int cur = page == Page::Chapters ? TRACKS_CHAPTERS : tracksTab;
			for (int i = 0; i < (int)tabs.size(); i++)
				if (tracksTabRect(i, (int)tabs.size()).hit(x, y) && tabs[i] != cur) openTracks(tabs[i], p);
			return false;
		}
		if (page != Page::Main)
		{
			if (y < ROW_Y || y >= ROW_Y + ROWS_SHOWN * ROW_H) return false;
			int i = scroll + (y - ROW_Y) / ROW_H;
			if (i >= (int)rows(p).size() || i < listFirst()) return false;
			cursor = i;
			listTouch = true;
			listScrolled = false;
			touchStartY = y;
			scrollStart = scroll;
			holdAt = page == Page::Browse && !inMenu && selMode == SEL_NONE && holdRow(i) ? SDL_GetTicks() : 0;
			return false;
		}
		if (p.playingPhoto())
		{
			if (panRect(p).hit(x, y))
			{
				panTouch = true;
				panMoved = false;
				flickAt = 0;
				flickVX = flickVY = 0;
				panDownX = lastX = x;
				panDownY = lastY = y;
				lastAt = SDL_GetTicks();
				panStartX = p.photo().panX;
				panStartY = p.photo().panY;
				return false;
			}
			for (int i = 0; i < PHOTO_BTNS; i++)
				if (photoBtn(i, p).hit(x, y)) pressedBtn = PHOTO_BTN + i;
			if (buttonRect(QUIT_BTN).hit(x, y)) pressedBtn = QUIT_BTN;
			return false;
		}
		// the chapter line first: the bar's room around it reaches up over its lower part
		if (chapterLineShown(p) && buttonRect(CHAPTER_BTN).hit(x, y)) { pressedBtn = CHAPTER_BTN; return false; }
		// a video's track lines: each opens its tab
		if (!p.playingAudio() && p.error.empty())
			for (int id : {LINE1_BTN, LINE2_BTN})
				if (buttonRect(id).hit(x, y)) { pressedBtn = id; return false; }
		// a song's: its first line opens Sound
		if (p.playingAudio() && p.error.empty() && buttonRect(LINE1_BTN).hit(x, y)) { pressedBtn = LINE1_BTN; return false; }
		// the bar is easier to hit with some room around it
		if (Rect{BAR.x - 8, BAR.y - 20, BAR.w + 16, BAR.h + 40}.hit(x, y) && p.duration > 0)
		{
			// the bar acts as the finger lands (not as it lifts, as the buttons do): the picture goes there at once
			// and follows the finger while it holds and drags
			dragging = true;
			dragFraction = scrubbed = barFraction();
			scrubAt = SDL_GetTicks();
			p.scrub(dragFraction);
			return false;
		}
		for (int id : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, QUIT_BTN, SCREEN_BTN, INFO_BTN, LOCK_BTN})
			if (buttonRect(id).hit(x, y)) pressedBtn = id;
		// Play while a page plays it: held, it plays here (tick() times it); let go sooner, the page's play/pause
		if (pressedBtn == PLAY_GRID && page == Page::Main && !inMenu && p.error.empty() && pageDrives(p))
		{
			playHoldAt = SDL_GetTicks();
			playHoldTouch = true;
		}
		return false;
	}

	std::string Ui::signature(const Player& p) const
	{
		const Track* a = p.selected(TrackType::Audio);
		const Track* s = p.selected(TrackType::Sub);
		char buf[256];
		snprintf(buf, sizeof buf, "%d %d %d %d %ld %ld %d %d %lld %lld %zu %d %d %.2f %d %d %d %d|", (int)page, cursor, scroll,
			p.paused + setGroup * 2 + 2, (long)std::floor(shownPosition(p)) * 2 + (predicting(p) && pagePlaying()), (long)p.duration, (int)p.aspect, p.loaded, a ? (long long)a->id : 0LL,
			s ? (long long)s->id : 0LL, p.tracks.size(), dragging, (int)(dragFraction * 1000), p.speed,
			p.index(), p.count(), pressedBtn + touchLocked * 100 + (lockHoldAt != 0) * 1000, listTouch * 1000 + browseVersion + (clearArmed != 0) * 100000 + (int)p.history().items().size() * 1000000);
		std::string photo = "|tab" + std::to_string(browseTab) + (netLoading ? "L" : "");
		if (touchLocked && lockHoldAt) photo += "|hold" + std::to_string((SDL_GetTicks() - lockHoldAt) / 100);
		if (playHoldAt) photo += "|phold" + std::to_string((SDL_GetTicks() - playHoldAt) / 50);
		if (pageDrives(p)) photo += pagePlaying() ? "|pageplay" : "|pagepause";
		if (p.playingPhoto())
		{
			const Player::PhotoView& v = p.photo();
			char pb[160];
			snprintf(pb, sizeof pb, "|photo %.4f %.4f %.4f %d %d %d %d", v.zoom, v.panX, v.panY, v.rotate, v.fill, p.thumbId, p.photoWidth());
			photo = pb;
		}
		// the backlights' levels while their rows show (the keys change them too)
		std::string upd = quickOpen ? "|quick" + std::to_string(quickCursor) + " " + std::to_string(volumePct) + " " + std::to_string(pressedBtn) +
			" " + std::to_string(backlight.permille(Backlight::TOP)) + "," + std::to_string(backlight.permille(Backlight::BOTTOM)) : "";
		upd += page == Page::Settings && setGroup == GROUP_CONTROLS
			? "|bl" + std::to_string(backlight.permille(Backlight::TOP)) + "," + std::to_string(backlight.permille(Backlight::BOTTOM)) : "";
		// the manga reader's settings: the page cache's Clear (what it holds not being a setting, and the press waiting
		// for its second), and the Java extensions' host state and progress
		if (page == Page::MangaSettings)
			upd += "|mc" + std::to_string(mangaPageCache.size()) + " " + std::to_string(mangaCacheAt != 0) +
				"|mj" + std::to_string(mangaJavaSerial()) + " " + std::to_string(mangaJavaAt != 0);
		if (updater)
			upd += "|upd" + std::to_string((int)updater->state()) + " " + std::to_string(updater->progress()) + " " + updater->available() +
				" " + std::to_string(popupShown(p)) + std::to_string(popupCursor) + std::to_string(pressedBtn);
		// the chapters: the list, the one playing (the Chapters page's progress goes by the position, above), the tab
		photo += "|ch" + std::to_string(p.chapterSerial) + " " + std::to_string(p.chapters.size()) + " " +
			std::to_string(shownChapter(p)) + " t" + std::to_string(tracksTab);
		// Up next: the list and the one playing (the Session's version moves with both)
		if (page == Page::UpNext) photo += "|un" + std::to_string(SessionHost::get().version()) + " " + std::to_string((int)p.paths().size());
		// Sound: its tab, the band, the effect under the cursor, its dialogs and the presets' list; the Display popup
		if (page == Page::Sound || displayOpen)
		{
			char sb[160];
			snprintf(sb, sizeof sb, "|snd%d %d %d %d %d %d %d %d %d %d %d", soundTab, fxCursor, eqBand, eqDialog, eqDlgCursor, eqListScroll,
				displayShown(p) ? displayRow : -1, p.eqPresetSerial(), p.playingAudio(), stashOn, eqTouch);
			photo += sb;
		}
		// the dots of "Loading..." go round
		if (p.loading() || p.thumbPending()) photo += "|loading" + std::to_string(SDL_GetTicks() / 400 % 4);
		// Playlists: the store, items found missing, a menu's title and line, a name refused
		photo += "|pl" + std::to_string(plStore ? plStore->serial() : -1) + " " + std::to_string(plMissingSerial) + ctxTitle + "|" + ctxNote + "|" + plNameNote;
		// selecting: the mode and where it adds, the ticks, Move's block, a dragged row's landing (the finger in steps
		// of 8), the picker, the arms, a folder being read
		if (selMode != SEL_NONE || pickerOpen || addPending.serial)
		{
			char sb[200];
			snprintf(sb, sizeof sb, "|sel%d %d %d %d %d,%d %d %d,%d %d %d %d %d %d %d", selMode, selCount(), selSerial, moving,
				plSel.empty() ? -1 : *plSel.begin(), (int)plSel.size(), plDrag.on, plDrag.on ? dragSlot() : -1, plDrag.y / 8, pickerOpen,
				pickerCursor, pickerScroll, removeArmed, dropArmed != 0, addPending.serial);
			photo += sb + selDest;
		}
		// Main: what it plays from, and whether the file is a server's (a playlist's item says where it comes from)
		if (p.playing() && page == Page::Main)
		{
			const PlaySource& src = p.source();
			photo += "|src" + std::to_string((int)src.kind) + (src.shuffled ? "s" : "") + (isNetPath(p.currentPath()) ? "n" : "") +
				(p.netWaiting() ? "w" : "") + src.title + "|" + src.id;
		}
		photo += webSig(p);
		// where it plays (the Playing tab's Play on row, the picker, the status line), and the pages it could
		{
			SessionHost& sh = SessionHost::get();
			// (the play mode's Shuffle: the music key, the status line)
			photo += "|out" + outputName() + (sh.pageWaits() ? "w" : "") + (sh.session().shuffle ? "S" : "") + " " + std::to_string(playOnOpen) + std::to_string(playOnCursor) +
				" " + std::to_string(playOnScroll);
			if (page == Page::Playing || playOnOpen)
				for (auto& c : outChoices()) photo += "," + c.name;
		}
		// Like: the heart's pop, the Add square's check, the picker's one file and its question
		{
			Uint32 now = SDL_GetTicks();
			photo += "|like" + std::to_string(likePopAt && now - likePopAt < 110) + std::to_string(addFlashUntil && now < addFlashUntil) +
				std::to_string(pickerArmed) + pickerOne + "|" + std::to_string(likedNow(p));
		}
		// the clock: its minute
		if (!clockText(p).empty()) photo += "|clk" + std::to_string((long long)time(nullptr) / 60);
		// behind the browser: the mini bar (what it shows, its press, its flash), the rows' chip, the header's mark (its
		// frame while it moves), what was stopped for a photo
		{
			char bb[160];
			snprintf(bb, sizeof bb, "|bg%d %d %d %d %d %d %d %d %.0f", bgPlay, miniBarShown(p), chipKind, landFlashUntil != 0 ? 1 : listPulseAt ? 2 + (int)((SDL_GetTicks() - listPulseAt) / 33) : 0, playHoldMini,
				markW(p), markW(p) && markLive(p) && panel.on() ? (int)(SDL_GetTicks() / 166) : -1, p.loading(), 0.0);
			photo += bb + chipPath + "|" + p.currentPath();
		}
		// the mini bar over a photo: it shows main's player, whose title, place and state are not the photo viewer's
		if (miniBarShown(p) && &barPlayer(p) != &p)
		{
			const Player& bp = barPlayer(p);
			char nb[240];
			snprintf(nb, sizeof nb, "|np%d %d %d %ld %ld %d %d", bp.paused, (int)bp.loading(), bp.error.empty() ? 0 : 1,
				(long)std::floor(shownPosition(bp)), (long)bp.duration, (int)bp.playingAudio(), (int)bp.playing());
			photo += std::string(nb) + bp.title + "|" + bp.currentPath();
		}
		// Home: the focus, the strip's scroll, a finger on a card or a tile, the tiles' lines (the connections)
		if (atHome())
		{
			char hb[120];
			snprintf(hb, sizeof hb, "|home%d %d %d %ld %d %zu %d", homeZone, homeStrip, homeGrid, std::lround(stripOff), homePress,
				recent.size(), netStore ? netStore->version() : -1);
			photo += hb;
			// B's quit dialog over it, and the row it has the focus on
			photo += std::string("|quit") + std::to_string(quitOpen) + std::to_string(quitCursor);
		}
		// Extensions: the drawer's tile and the note's focus, the switch (its lines and rows say it), a finger's button
		// down on them (mangaOn is in the settings too: a change redraws anywhere)
		if (page == Page::Extensions || page == Page::Manga || browseDir == EXTDIR)
			photo += "|ext" + std::to_string(mangaOn) + std::to_string(extCursor) + std::to_string(extSheet) +
				std::to_string(extSheetCursor) + " " + std::to_string(extPressed);
		// Manga: the tab, each page's cursors and scrolls, the store and the card's scan, the finger's article, the
		// History's Clear armed; the reader's page when it shows (its status is main's answer)
		if (page == Page::Manga || page == Page::MangaSeries || page == Page::MangaRead || page == Page::MangaSources)
		{
			char mb[200];
			snprintf(mb, sizeof mb, "|mg%d %d,%d,%d,%d %d,%d %d,%d,%d,%d %d,%d %llu %d %d %d %zu %llu",
				mangaTab, mlibZone, mlibCard, mlibGrid, mlibScroll, mhisCursor, mhisScroll, mbrowseCursor, mListCursor,
				mListScroll, (int)mBrowseList, mserCursor, mserScroll, (unsigned long long)(mangaStore ? mangaStore->serial() : 0),
				(int)mangaLocal.size(), mangaPressed, mangaClearAt != 0, coverTex.size(), (unsigned long long)coverSerial);
			photo += std::string(mb) + "|" + mangaSeriesId;
			// Sources: the sub-list and each list's place, a sheet, the sources installing, the manager's own changes
			// (repos().serial(): an index loaded, a source installed) and what waits to be added
			if (page == Page::Manga || page == Page::MangaSources)
			{
				char sb[240];
				snprintf(sb, sizeof sb, "|ms%d %d,%d %d,%d %d,%d %d %d %d %llu %llu %zu %d", mstTab, mstCursor[0],
					mstScroll[0], mstCursor[1], mstScroll[1], mstCursor[2], mstScroll[2], mstModal, mstModalCursor,
					(int)mstUninstallArmed, (unsigned long long)mangaReposSerial(), (unsigned long long)mstLocal,
					mstInstalling.size(), (int)mstAdding.size());
				photo += std::string(sb) + mstModalId + "|" + mstPendingUrl + "|mt" +
					std::to_string((long long)time(nullptr) / 60);	// (the repositories' "2 h ago")
			}
			// Browse's sources and the source series' details, chapters and jobs (ui_manga_browse.cpp)
			photo += mangaNetSignature();
			// Downloads: the tab's lists and the Download sheet (ui_manga_downloads.cpp)
			photo += mangaDlSignature();
			if (page == Page::MangaRead)
			{
				char rb[160];
				snprintf(rb, sizeof rb, "|rs%d%d%d%d %d %d %d%d%d z%d %d%d%d r%d", (int)readerStatus.open, (int)readerStatus.loading,
					(int)readerStatus.atEnd, (int)mangaReadLive, readerStatus.page, readerStatus.pages, (int)mangaDirty,
					(int)mangaTouch, (int)mangaSlider, readerStatus.zoomPct, (int)mangaMenu, mangaMenuCursor,
					(int)mangaPages, readerStatus.rotation);
				photo += std::string(rb) + readerStatus.error + "|" + mangaReadChapterId + "|m" + std::to_string(readerStatus.mode) +
					"|w" + std::to_string((int)readerStatus.failedHere) + std::to_string((int)mangaReadWaiting) + std::to_string((int)mangaSrcLoading) + mangaSrcError;
				// the pages grid: the cursor, the scroll, the textures made, the thumbnails arrived
				if (mangaPages)
					photo += "|pg" + std::to_string(mangaPagesCursor) + "," + std::to_string(mangaPagesScroll) + " " +
						std::to_string(mpTex.size()) + "," + std::to_string(readerThumbs.pages.size());
				// fullscreen: where the pages are on this panel, which have their textures, the tag
				if (readerStatus.fullscreen)
				{
					photo += "|full" + std::to_string(readerBottom.gen) + " " + std::to_string(mfTex.size()) + " " +
						std::to_string(mfWaiting) + " " + std::to_string(readerBottom.pages.size()) + (SDL_GetTicks() < mfTagUntil ? "T" : "");
					// the controls over the pages and the first open's toast: how long they still show, in 100 ms
					// steps, so each hides by itself (no finger, no other change to draw for)
					Uint32 now = SDL_GetTicks();
					photo += "|fc" +
						std::to_string(mangaCtrlUntil && now < mangaCtrlUntil ? (int)((mangaCtrlUntil - now + 99) / 100) : 0) + " " +
						std::to_string(mangaToastUntil && now < mangaToastUntil ? (int)((mangaToastUntil - now + 99) / 100) : 0) + " " +
						std::to_string(mangaCtrlPressed);
					for (auto& b : readerBottom.boxes)
						photo += " " + std::to_string(b.page) + ":" + std::to_string((int)std::lround(b.x * 4096)) + "," + std::to_string((int)std::lround(b.y * 4096));
				}
			}
		}
		return buf + photo + "|ld" + (loaderShown ? std::to_string(SDL_GetTicks() / 110) : std::string()) + "|xn" + extNoticeText() + "|" + p.title + "|" + p.error + "|" + serializeSettings(p.settings) + "|bat" + std::to_string(battery.percent) + (battery.charging ? "c" : "") + (inMenu ? "|menu" + std::to_string(menuTab) : "") + (ctxOpen ? "|ctx" + std::to_string(ctxCursor) + std::to_string(pressedBtn) + ctxStatus + (ctxArmed ? "!" : "") : "") + upd
			+ (kbOpen ? "|kb" + eqNameNote + std::to_string(kbCursor) + "," + std::to_string(kbPressed) + "," + std::to_string((int)kbShift) + (kbSymbols ? "s" : "") + std::to_string(kbEdit.caret) + kbEdit.text + (kbCaretShown() ? "|" : "") : "");
	}
}
