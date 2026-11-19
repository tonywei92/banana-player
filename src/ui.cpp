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
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "ui_docs.h"
#include "ui_reader.h"
#include "ui_manga.h"
#include "ui_sound.h"
#include "ui_web.h"
#include "ui_player.h"
#include "ui_shell.h"
#include "ui_search.h"
#include "durworker.h"

namespace bp
{
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
			std::array<TTF_Font*, FONT_COUNT> f = {};
			for (int i = 0; i < 3; i++)
			{
				// Fusion Pixel is a 12-pixel font: at whole multiples of 12 its pixels stay square
				int px = (int)std::lround(SIZES[i] * sy);
				if (name == FALLBACKS[0]) px = std::max(12, (int)std::lround(SIZES[i] * sy / 12.0) * 12);
				f[i] = TTF_OpenFont((fontsDir + "/" + name).c_str(), px);
			}
			if (f[0] && f[1] && f[2]) { fallbacks.push_back(f); fallbackFiles.push_back(name); }
			else for (auto* x : f) if (x) TTF_CloseFont(x);
		}
		return true;
	}

	// the areas: each made here and listed, which is all the wiring one needs (docs/notes/ui-areas.md)
	Ui::Ui()
	{
		// first, so an area's constructor can tell it its body is drawn in the shell's box (Shell::adopt)
		shell = std::make_unique<Shell>(*this);
		pageReader = std::make_unique<PageReader>(*this);	// (not an area: no page of its own; Docs and Manga use it)
		docs = std::make_unique<Docs>(*this);
		areas.push_back(docs.get());
		sound = std::make_unique<Sound>(*this);
		areas.push_back(sound.get());
		webPage = std::make_unique<Web>(*this);
		areas.push_back(webPage.get());
		manga = std::make_unique<Manga>(*this);
		areas.push_back(manga.get());
		settingsPage = std::make_unique<SettingsPage>(*this);
		areas.push_back(settingsPage.get());
		browse = std::make_unique<Browse>(*this);
		areas.push_back(browse.get());
		kit = std::make_unique<Kit>(*this);	// (not an area: the redesign's components, drawn by the areas rebuilt on them)
		search = std::make_unique<Search>(*this);	// (its constructor adopts Page::Search: drawn in the shell's body)
		areas.push_back(search.get());
		playerPage = std::make_unique<PlayerPage>(*this);
		areas.push_back(playerPage.get());
	}

	Ui::Area* Ui::area() const
	{
		for (Area* a : areas)
			if (a->has(page)) return a;
		return nullptr;
	}

	Ui::~Ui()
	{
		if (photoTex) SDL_DestroyTexture(photoTex);
		if (supportTex) SDL_DestroyTexture(supportTex);
		for (auto& c : cache) SDL_DestroyTexture(c.second);
		for (auto& c : manga->coverTex)
			if (c.second.tex) SDL_DestroyTexture(c.second.tex);
		for (auto* t : corners)
			if (t) SDL_DestroyTexture(t);
		for (auto* f : fonts)
			if (f) TTF_CloseFont(f);
		if (pinFont) TTF_CloseFont(pinFont);
		for (auto& f : fallbacks)
			for (auto* x : f) if (x) TTF_CloseFont(x);
		if (ren) SDL_DestroyRenderer(ren);
	}

	void Ui::stickAxis(int value, Player& p)
	{
		// one reader at a time; the other gets a 0, so a stick held as its page closes stops it at once
		if (page == Page::DocText || page == Page::DocPages) docs->stickAxis(value, p);
		else docs->stickAxis(0, p);
		if (page == Page::MangaRead) manga->stickAxis(value, p);
		else manga->stickAxis(0, p);
	}

	// START opens Settings everywhere in the shell, on the player and in the text and PDF readers. The exceptions are
	// the places where START is a page's own key: a form's Save/Done (and Find on the network), the Book bar while a
	// page is held sideways, a document's open menu, the on-screen keyboard (taken before this) and Manga's own pages.
	bool Ui::startOpensSettings(const Player& p) const
	{
		if (page == Page::Settings || page == Page::About || page == Page::Main || page == Page::ExtSection ||
			page == Page::Extensions)
			return true;
		// Search: START is Done to the results while typing, and its page's own key otherwise
		if (page == Page::Search) return false;
		if (page == Page::Browse)
		{
			if (browse->browseDir == FORMDIR || browse->browseDir == FINDNET) return false;
			if (browse->playlists->selMode != SEL_NONE || browse->playlists->addPending.serial) return false;
			// Home's quit question takes every key (START backs out of it, as B does)
			return !(browse->atHome() && browse->homeView->quitOpen);
		}
		if (page == Page::DocText || page == Page::DocPages)
		{
			bool book = page == Page::DocText ? docs->textStatus.book : pageReader->readerStatus.book;
			return !book && docs->docMenu == Docs::DOC_MENU_NONE;	// (a menu's START closes the menu)
		}
		(void)p;
		return false;
	}

	bool Ui::press(Button b, Player& p)
	{
		// a key while a finger holds a slider: the slider is let go where it is (the key may close what it's on)
		slideEnd(p);
		// About's support QR, large on the top panel: B or Y puts it down (a tap does too, in pointer)
		if (page == Page::About && aboutQr && (b == Button::B || b == Button::Y))
		{
			aboutQr = false;
			return false;
		}
		// leaving About closes it too (openAbout clears it as well: coming back finds it down)
		if (page != Page::About) aboutQr = false;
		// an overlay (the readers' Brightness sheet, a message's dialog, the quick settings, Files' Info card) takes the
		// keys while it shows
		// any key restarts Screen off's count, an overlay's too (Settings, a sheet): before, the B that closed Settings
		// after a while in it left the count where it was, and the bottom panel went dark at once
		webInputAt = SDL_GetTicks();
		if (topOverlay()) return overlayKey(b, p);
		sameBrightness = p.settings.sameBrightness;
		mangaOn = p.settings.mangaOn;
		// dark (Screen off, or off by itself): over what plays, the playback keys work blind and leave it dark (play
		// and pause, seek, next and previous, chapters, like: issue #3, watching with the bottom screen off; UP / DOWN
		// too, the volume, which the hardware keys move without waking it); any other button, or anywhere else (a list:
		// A there would open whatever the cursor is on), brings the panel back and does nothing else, as a touch does.
		// F + START still quits, in main
		if (!panel.on())
		{
			bool playKey = b == Button::A || b == Button::Left || b == Button::Right || b == Button::Up || b == Button::Down ||
						   b == Button::L || b == Button::R || b == Button::L2 || b == Button::R2 || b == Button::L3;
			bool overPlayer = page == Page::Main && !quickShown() && !kbOpen && p.playing() && !p.playingPhoto();
			if (!(playKey && overPlayer))
			{
				panel.set(true);
				return false;
			}
		}
		// a copy or move running: only its Cancel answers (ui_fileops.cpp)
		if (fileJob) return fileJobKey(b);
		if (kbOpen)
		{
			kbPress(b);
			if (sound->eqNameSubmit) sound->nameDone(p);
			if (browse->playlists->plNameSubmit) browse->playlists->plNameDone(p);
			if (browse->fileNameSubmit) browse->fileRenameDone(p);
			return false;
		}
		// Add to... over what plays; the browser's is taken below, in its turn
		if (browse->playlists->pickerOpen && page != Page::Browse)
		{
			browse->playlists->pickerKey(b, p);
			return false;
		}
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
		if (b == Button::B && (page == Page::Main || page == Page::Browse) && SessionHost::get().pageWaits() &&
			!ctxOpen && !browse->playlists->pickerOpen && !popupShown(p) && browse->playlists->selMode == SEL_NONE && !browse->playlists->addPending.serial)
		{
			takeOver(p);
			return false;
		}
		// START: Settings, everywhere (docs/redesign: no START menu; its places are the rail, F, L3, About in Settings).
		// Before the hotkeys guard: it is not a playback key, and a video with the buttons off still has Settings
		if (b == Button::Start && startOpensSettings(p))
		{
			if (page == Page::Settings || page == Page::About) closeSettings(p);
			else openSettings(p, true);
			return false;
		}
		// Button hotkeys off: the buttons do nothing over a playing video (touch does it all); the pages opened from
		// there by touch still take them. UP / DOWN stay the volume, as the hardware volume keys do (it isn't playback)
		if (page == Page::Main && !p.settings.buttonHotkeys)
		{
			if (!p.playingPhoto() && (b == Button::Up || b == Button::Down)) area()->key(b, p);
			return false;
		}
		// the shell's pages: L / R step the sections, L3 is Search (and All recent's tabs keep L2 / R2)
		if (shell->page(page) && shell->key(b, p)) return false;
		// L2/R2: the chapter before or after, over what plays and its two pages (the Chapters page's mark follows).
		// Not in START's Settings, not over a photo (its keys are its own), and nothing with Button hotkeys off: they
		// have no touch button to stand for. On Settings a group page takes them: the group before or after
		// (the manga pages have their own: the reader's chapter before or after, the series page's Download)
		if (b == Button::L2 || b == Button::R2)
		{
			if (page == Page::Settings || page == Page::Web) { area()->key(b, p); return false; }
			// About is a settings group too (its page its own): back to its neighbours
			if (page == Page::About)
			{
				setGroup = GROUP_ABOUT;
				settingsPage->stepGroup(b == Button::R2 ? 1 : -1, &p);
				aboutQr = false;
				return false;
			}
			if (page != Page::Manga && page != Page::MangaSeries && page != Page::MangaRead &&
				page != Page::DocText && page != Page::DocPages)
			{
				if (p.settings.buttonHotkeys && p.playing() && !p.playingPhoto() && p.error.empty() && page == Page::Main)
					chapterStep(b == Button::L2 ? -1 : 1, p);
				return false;
			}
		}
		// Files' Copy / Move held: SELECT pastes in this folder, X lets go
		if (browse->clipKey(b, p)) return false;
		// an area's page (Files' document viewers): its own keys
		if (Area* a = area()) return a->key(b, p);
		if (page == Page::Info || page == Page::About)
		{
			int maxScroll = std::max(0, (int)infoLines.size() - (page == Page::About ? ABOUT_SHOWN : INFO_SHOWN));
			switch (b)
			{
			case Button::Up: scroll = std::max(0, scroll - 1); break;
			case Button::Down: scroll = std::min(maxScroll, scroll + 1); break;
			case Button::Left: scroll = std::max(0, scroll - (page == Page::About ? ABOUT_SHOWN : INFO_SHOWN)); break;
			case Button::Right: scroll = std::min(maxScroll, scroll + (page == Page::About ? ABOUT_SHOWN : INFO_SHOWN)); break;
			case Button::B: case Button::A:
				// About is a settings group: back to the groups' tiles
				if (page == Page::About) { page = Page::Settings; setGroup = -1; cursor = GROUP_ABOUT; scroll = 0; aboutQr = false; }
				else page = home(p);
				break;
			case Button::Y:
				// About's support QR, large on the top panel (a tap on the small code does it too)
				if (page == Page::About) aboutQr = !aboutQrUp();
				break;
			default: break;
			}
			return false;
		}
		// (Page::Main, the player and a photo's viewer, is an area: PlayerPage::key, above)
		return listKey(b, p);
	}

	bool Ui::popupKey(Button b, Player& p)
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

	bool Ui::listKey(Button b, Player& p)
	{
		// (the browser's Up row is row -1: first; n counts it)
		int first = browse->listFirst(), n = (int)rows(p).size() - first;
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

	void Ui::tick(Player& p)
	{
		// a held slider whose target is told at a pace: a finger resting on a new value, sent when it's due
		slideLive(p);
		manga->mangaVideoGuard(p);
		if (Area* a = area()) a->tick(p);
		if (shell->page(page)) shell->tick(p);	// the rail's Now playing key's hold
		fileJobTick(p);
		rootsCheck(p);
		manga->mangaEngineWatch(p);	// the engine's download sheet: closed when done
		// the rows' durations: none probed while a file opens; the rows on screen first
		if (durWorker)
		{
			durWorker->setBusy(p.loading());
			if (page == Page::Browse && scroll != browse->durLo)
			{
				browse->durLo = scroll;
				durWorker->focus(scroll, scroll + ROWS_SHOWN);
			}
		}
		// a playlist's edits, written once they've stopped for a second
		if (browse->playlists->plDirty && SDL_GetTicks() - browse->playlists->plDirtyAt >= 1000) browse->playlists->plFlush(p);
		// the dashboard starts downloads and asks for a series' chapters: their worker answers are taken here too,
		// since no manga page may be drawn on the handheld while the browser is the one using the reader
		if (p.settings.mangaOn && manga->mangaNet)
		{
			manga->mangaBrowsePoll(p);
			manga->mangaDlAskPump();
			manga->mangaNotesTick(p);
		}
		// the web's equalizer: what mpv keeps, and settings.conf, once its sliders stop for a second
		if (eqWebDirtyAt && SDL_GetTicks() - eqWebDirtyAt >= 1000)
		{
			eqWebDirtyAt = 0;
			p.commitEq();
		}
		// A or the Play button held while a page plays the file: here instead, once, as the hold reaches its length (not
		// as it lets go). Gone off the player (B, the menu), or the page gone: nothing
		// (Home's Now playing card key holds too: Home's card is the way back to a page's file)
		bool holdHere = page == Page::Main || (playHoldMini && page == Page::Browse && browse->atHome());
		if (playHoldAt && (!holdHere || !pageDrives(barPlayer(p)))) playHoldAt = 0;
		if (!playHoldAt) playHoldMini = false;
		if (playHoldAt && SDL_GetTicks() - playHoldAt >= PLAY_HOLD_MS)
		{
			playHoldAt = 0;
			playHoldMini = false;
			if (playHoldTouch) { pressedBtn = -1; playerPage->dropTouch(); swallowTouch = true; }	// the finger's lift presses nothing
			takeOver(barPlayer(p));
		}
		// a finger held on Quick settings' Play: it takes playback over from a browser, as the mini bar's mark did
		// (the lift that follows chooses nothing)
		if (quickHoldAt && quickShown() && pressedBtn == QUICK_NOW_PLAY && SDL_GetTicks() - quickHoldAt >= 600)
		{
			quickHoldAt = 0;
			pressedBtn = -1;
			swallowTouch = true;
			takeOver(barPlayer(p));
		}
		if (!quickShown()) quickHoldAt = 0;
		if (landFlashUntil && SDL_GetTicks() >= landFlashUntil) landFlashUntil = 0;
		// Home's Recent: listed again when what plays changes (the next song started by itself: its card comes first)
		if (page == Page::Browse && browse->atHome() && !browse->homeView->homeTouch && !ctxOpen && p.currentPath() != browse->homeView->homeListedFor)
		{
			browse->homeView->recentKeepOrder = true;	// (its progress anew, its order as shown)
			browse->enterFolder(HOMEDIR, p);
		}
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
		// (a song's own timer, and a video's: Never by default, its controls are what one reaches for)
		int offSecs = screenOffSeconds(p.playingAudio() ? p.settings.screenOffAfter : p.settings.screenOffVideo);
		bool viewing = p.playingPhoto() || pageReader->readerStatus.open;
		if (offSecs > 0 && panel.on() && p.playing() && !viewing && !p.paused && !p.loading() && p.error.empty() &&
			!kbOpen && !topOverlay() && !popupShown(p) && !slide.on && SDL_GetTicks() - webInputAt >= (Uint32)offSecs * 1000)
			panel.set(false);
		// a dragged row near the list's top or bottom: the list scrolls under it, a row each 150 ms
		if (browse->playlists->plDrag.on && page == Page::Browse && isPlDir(browse->browseDir) && SDL_GetTicks() - browse->playlists->plDrag.scrollAt >= 150)
		{
			int n = (int)browse->browseRows.size();
			int was = scroll;
			if (browse->playlists->plDrag.y < ROW_Y + 24) scroll = std::max(0, scroll - 1);
			else if (browse->playlists->plDrag.y > ROW_Y + ROWS_SHOWN * ROW_H - 24) scroll = std::min(std::max(0, n - ROWS_SHOWN), scroll + 1);
			if (scroll != was) browse->playlists->plDrag.scrollAt = SDL_GetTicks();
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
		browse->homeView->stripStep();
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
		// "<" over a photo closes it at once (the player's own "<" is its area's: PlayerPage::act)
		if (id == BACK_ICON && page == Page::Main && p.playing()) return true;
		// elsewhere (a page over what plays, Settings): B, back a step
		if (id == BACK_ICON && ((page == Page::Settings && settingsOver) || (page != Page::Browse && page != Page::Main)))
			return pressAnyway(Button::B, p);
		// a photo's Close
		if (id == QUIT_BTN) return p.playingPhoto();
		// adding files: the header's key is a ✕ that cancels at once (B goes up a folder there)
		if (id == BACK_ICON && page == Page::Browse && (browse->playlists->selMode == SEL_ADD || browse->playlists->selMode == SEL_PICK)) { browse->playlists->selBack(p, true); return false; }
		if (id == BACK_ICON) return pressAnyway(Button::B, p);
		if (id == EMPTY_BTN) { browse->doEmptyAction(p); return false; }
		// the select mode's bar: its keys' (Cancel leaves from any folder)
		if (id == SEL_ALL) { browse->playlists->selKey(Button::Select, p); return false; }
		if (id == SEL_MOVE) { browse->playlists->selKey(Button::Y, p); return false; }
		// (the act itself, not its key: adding, X is Cancel and Y adds)
		if (id == SEL_ACT) { browse->playlists->removeArmed = browse->playlists->removeArmed && browse->playlists->selMode == SEL_EDIT; browse->playlists->dropArmed = 0; browse->playlists->selAct(p); return false; }
		// (Cancel shows X's key, so it asks twice as X does; the header's ✕ leaves at once)
		if (id == SEL_CANCEL) { browse->playlists->selBack(p, false); return false; }
		if (id == MOVE_CANCEL || id == MOVE_DONE) { browse->playlists->moveEnd(id == MOVE_DONE, p); return false; }
		if (id == BUSY_CANCEL) { browse->playlists->cancelAdd(p); return false; }
		if (id >= manga->EXT_ROW && id <= manga->EXT_DONE) return manga->extAct(id, p);
		if (id >= PHOTO_BTN && id < PHOTO_BTN + PHOTO_BTNS)
		{
			static const Button PHOTO_KEYS[PHOTO_BTNS] = {Button::L, Button::R, Button::Down, Button::Up, Button::L3,
				Button::None, Button::None, Button::Start};
			// Fit / Fill changes in place (SELECT opens the photo's More now); Info is the page it always was
			if (id == PHOTO_BTN + 5) { p.togglePhotoFill(); return false; }
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
		return false;
	}

	bool Ui::pointer(const Pointer& pt, Player& p)
	{
		int x = (int)(pt.x * W), y = (int)(pt.y * H);
		webInputAt = SDL_GetTicks();
		// The shell's pages in the handheld's log: where a touch began and which section it did (the rebuilt pages log
		// their drags and taps too, so a finger that does nothing leaves the line that says where it stopped)
		if (pt.kind == Pointer::Down && shell->page(page))
			fprintf(stderr, "touch: down %d,%d on page %d, section %d\n", x, y, (int)page, (int)shell->section(p));
		// a list's finger whose lift never came (a dialog took it): a new touch starts afresh
		if (pt.kind == Pointer::Down) pageReader->mlist = PageReader::MangaListDrag();
		// a slider under the finger: it keeps the finger until it lifts, whatever it passes over (a new touch with
		// one still held: its lift was lost, it's let go where it was)
		if (slide.on)
		{
			if (pt.kind != Pointer::Down) return slideFollow(pt, x, y, p);
			slideEnd(p);
		}
		if (topOverlay()) return overlayPointer(pt, x, y, p);
		mangaOn = p.settings.mangaOn;
		if (fileJob) return fileJobPointer(pt, x, y);
		if (browse->clipPointer(pt, x, y, p)) return false;
		// Home's quit question takes every finger, the shell's rail and header included: a row chooses as it lifts,
		// outside it cancels (it sits in the main strip, so the rail is no longer under its panel to block them).
		// Dark and the touch that woke the panel keep their turns, as they had when the question was a full panel.
		if (page == Page::Browse && browse->atHome() && browse->homeView->quitOpen && !popupShown(p) &&
			panel.on() && !swallowTouch && !kbOpen)
			return browse->homeView->homePointer(pt, x, y, p);
		// the shell's rail, header and mini bar first (the page's body is under them)
		if (shell->page(page) && shell->pointer(pt, x, y, p)) return false;
		// a section page's old body is drawn scaled into the shell's body box: its touches are mapped back
		if (shell->page(page))
		{
			int mx = x, my = y;
			if (shell->mapPoint(p, mx, my)) { x = mx; y = my; }
		}
		// All recent's tabs: a tap on one shows that kind
		if (page == Page::Browse && browse->browseDir == RECENT && !ctxOpen && p.settings.keepHistory && browse->homeView->recentCounts[0] && y < ROW_Y)
		{
			for (int i = 0; i < 4; i++)
				if (browse->homeView->recentTabRect[i].hit(x, y))
				{
					if (pt.kind == Pointer::Up && i != browse->homeView->recentTab && (i == 0 || browse->homeView->recentCounts[i]))
					{
						browse->homeView->recentTab = i;
						browse->enterFolder(RECENT, p);
						cursor = browse->firstCursor();
						scroll = 0;
						browse->fitList();
					}
					return false;
				}
		}
		if (kbOpen && panel.on())
		{
			kbPointer(pt, x, y);
			if (sound->eqNameSubmit) sound->nameDone(p);
			if (browse->playlists->plNameSubmit) browse->playlists->plNameDone(p);
			if (browse->fileNameSubmit) browse->fileRenameDone(p);
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
		// Add to... over what plays as over the browser
		if (browse->playlists->pickerOpen) return browse->playlists->pickerPointer(pt, x, y, p);
		// the header's web dashboard mark: its page, as the finger lifts on it
		if (webIndDown || (pt.kind == Pointer::Down && webInd.w && webInd.hit(x, y) && !ctxOpen && !browse->playlists->pickerOpen && !popupShown(p)))
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
		// the header's status cluster: one touch target on every header, the touch twin of F (the globe inside it
		// keeps its own tap, taken above). Only the down: the panel it opens takes the lift
		if (statusHit.w > 0 && statusHit.hit(x, y) && pt.kind == Pointer::Down && !ctxOpen && !browse->playlists->pickerOpen &&
			!popupShown(p) && !kbOpen)
		{
			toggleQuick(p);
			return false;
		}
		if (Area* a = area(); a && a->pointerFirst()) return a->pointer(pt, x, y, p);
		// locked, over a video or a song: a hold on the lock (draw() times it) releases it; nothing else answers a finger
		if (page == Page::Main && playerPage->locked(p)) return playerPage->lockPointer(pt, x, y, p);
		// About's support QR, up on the top panel: a tap anywhere puts it down (B and Y do too)
		if (pt.kind == Pointer::Down && page == Page::About && aboutQr)
		{
			aboutQr = false;
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
		if (page == Page::Browse && browse->playlists->addPending.serial)
		{
			if (pt.kind == Pointer::Down) pressedBtn = buttonRect(BUSY_CANCEL).hit(x, y) ? BUSY_CANCEL : -1;
			else if (pt.kind == Pointer::Up && pressedBtn == BUSY_CANCEL)
			{
				pressedBtn = -1;
				if (buttonRect(BUSY_CANCEL).hit(x, y)) browse->playlists->cancelAdd(p);
			}
			return false;
		}
		if (page == Page::Browse && browse->playlists->pickerOpen) return browse->playlists->pickerPointer(pt, x, y, p);
		// a playlist's row on the finger by its grip: the landing line follows, lifted it's dropped there
		if (browse->playlists->plDrag.on)
		{
			browse->playlists->plDrag.y = std::clamp(y, 0, H - 1);
			if (pt.kind == Pointer::Up) browse->playlists->dragEnd(p);
			return false;
		}
		if (panTouch) return photoPointer(pt, x, y, p);
		if (browse->homeView->homeTouch) return browse->homeView->homePointer(pt, x, y, p);
		if (listTouch)
		{
			// a list (the tracks, the browser, the settings, the manga's): a drag scrolls it, a tap acts as the finger
			// lifts
			bool set = page == Page::Settings || page == Page::MangaSettings, groups = page == Page::Settings && setGroup < 0;
			bool info = page == Page::Info || page == Page::About;
			auto srows = set && !groups ? (page == Page::MangaSettings ? manga->mangaSettingRows() : settingsPage->settingRows()) : std::vector<int>();
			int n = groups ? GROUPS : set ? (int)srows.size() : info ? (int)infoLines.size() : (int)rows(p).size();
			int rowH = groups ? lay::SET_TILE_H + lay::SET_TILE_GAP : set ? SET_H : info ? INFO_H : ROW_H,
				shown = groups ? GROUPS : set ? SET_SHOWN : info ? INFO_SHOWN : ROWS_SHOWN;
			if (pt.kind == Pointer::Move && (listScrolled || std::abs(y - touchStartY) > 12))
			{
				bool started = !listScrolled;
				listScrolled = true;
				holdAt = 0;
				int first = set || info ? 0 : browse->listFirst();
				scroll = lay::dragScroll(scrollStart, touchStartY, y, rowH, first, n, shown);
				if (started) fprintf(stderr, "touch: list drag page %d y %d -> %d, scroll %d -> %d of %d\n", (int)page,
					touchStartY, y, scrollStart, scroll, n);
				cursor = std::clamp(cursor, scroll, scroll + shown - 1);
			}
			else if (pt.kind == Pointer::Up)
			{
				listTouch = false;
				holdAt = 0;
				if (listScrolled) return false;
				// the info page only scrolls; About's small support QR takes a tap (its large view, as Y does)
				if (info)
				{
					if (page == Page::About && aboutQrRect().hit(x, y)) aboutQr = true;
					return false;
				}
				// a setting: the left of its value (its arrow) steps back, the rest of the row on. The stepper's
				// boxes come from lay::settingStepper, the same ones Kit::settingRow drew
				int dir = 1;
				if (set && !groups)
				{
					lay::SettingsRows l = page == Page::Settings ? lay::settingsRowsIn(lay::shell().body)
						: lay::settingsRows();
					lay::SettingStepper st = lay::settingStepper(l.row(cursor - scroll));
					dir = lay::stepperBack(st, touchX) ? -1 : 1;
				}
				if (groups) settingsPage->openGroup(cursor, &p);
				else if (set && page == Page::Settings && srows[cursor] == SOUND_ROW) sound->open(p, true);	// a page, not a value
				else if (set) settingsPage->changeSetting(srows[cursor], dir, p);
				else choose(cursor, p);
			}
			return false;
		}
		// a finger that went down on one of the manga app's lists: its moves scroll the list and its lift is the tap
		// (the down came through mangaPointer, after the sheets, the header and the mini bar had their turn)
		if (pageReader->mlist.on && pt.kind != Pointer::Down) return pageReader->mangaListPointer(pt, x, y);
		if (pressedBtn >= 0)
		{
			if (pt.kind != Pointer::Up) return false;
			int id = pressedBtn;
			pressedBtn = -1;
			// let go off the button: nothing (a finger that slid away changed its mind)
			Rect r = id >= PHOTO_BTN && id < PHOTO_BTN + PHOTO_BTNS ? photoBtn(id - PHOTO_BTN, p) : buttonRect(id);
			return r.hit(x, y) && act(id, p);
		}
		// an area's page (the readers, the Manga pages): its own touches. An area whose downs come late (Manga's
		// pages but the reader) has them below, after "<"
		if (Area* a = area(); a && !(pt.kind == Pointer::Down && a->pointerDownLate())) return a->pointer(pt, x, y, p);
		if (pt.kind != Pointer::Down) return false;

		// "<": B (up a folder; twice, out of what plays or, from the first page, out of the player)
		if (backIconShown(p) && buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
		if (browse->emptyAction() && buttonRect(EMPTY_BTN).hit(x, y)) { pressedBtn = EMPTY_BTN; return false; }
		// the browser's (Browse::pointer), and an area whose downs come late (the Manga pages): their own taps (the
		// mini bar's, the header's and "<" taken above)
		if (Area* a = area(); a && a->pointerDownLate()) return a->pointer(pt, x, y, p);
		if (page == Page::Info || page == Page::About)
		{
			if (y < (page == Page::About ? ABOUT_Y : INFO_Y)) return false;
			listTouch = true;
			listScrolled = false;
			touchStartY = y;
			scrollStart = scroll;
			return false;
		}
		// (Page::Main's touches are its area's: PlayerPage::pointer, above)
		return listDown(x, y, p);
	}

	bool Ui::listDown(int x, int y, Player& p)
	{
		if (y < ROW_Y || y >= ROW_Y + ROWS_SHOWN * ROW_H) return false;
		int i = scroll + (y - ROW_Y) / ROW_H;
		if (i >= (int)rows(p).size() || i < browse->listFirst()) return false;
		cursor = i;
		listTouch = true;
		listScrolled = false;
		touchStartY = y;
		scrollStart = scroll;
		holdAt = page == Page::Browse && browse->playlists->selMode == SEL_NONE && browse->holdRow(i) ? SDL_GetTicks() : 0;
		return false;
	}

	std::string Ui::signature(const Player& p) const
	{
		const Track* a = p.selected(TrackType::Audio);
		const Track* s = p.selected(TrackType::Sub);
		char buf[256];
		snprintf(buf, sizeof buf, "%d %d %d %d %ld %ld %d %d %lld %lld %zu %d %d %.2f %d %d %d %d|", (int)page, cursor, scroll,
			p.paused + setGroup * 2 + 2, (long)std::floor(shownPosition(p)) * 2 + (predicting(p) && pagePlaying()), (long)p.duration, (int)p.aspect, p.loaded, a ? (long long)a->id : 0LL,
			s ? (long long)s->id : 0LL, p.tracks.size(), (int)slideHeld(SL_SEEK), slideHeld(SL_SEEK) ? (int)(slide.value * 1000) : 0, p.speed,
			p.index(), p.count(), pressedBtn, listTouch * 1000 + browse->browseVersion + (clearArmed != 0) * 100000 + (int)p.history().items().size() * 1000000);
		std::string photo = "|th" + std::to_string(theme::currentTheme());	// (the browser's section and its listing: Browse::signature)
		// a slider under a finger: which, and where the finger has it (what draws differently while held reads these)
		if (slide.on) photo += "|sl" + std::to_string(slide.id) + "." + std::to_string(slide.sub) + ":" + std::to_string(slide.value);
		if (playHoldAt) photo += "|phold" + std::to_string((SDL_GetTicks() - playHoldAt) / 50);
		// the Title marquee moving: each of its steps (24 a second while it moves; nothing while it rests at its start)
		if (marqUp && panel.on())
		{
			const lay::MarqueeAt m = marqNow();
			photo += "|mq" + std::to_string(m.done ? -1 : m.step);
		}
		if (pageDrives(p)) photo += pagePlaying() ? "|pageplay" : "|pagepause";
		if (p.playingPhoto())
		{
			const Player::PhotoView& v = p.photo();
			char pb[160];
			snprintf(pb, sizeof pb, "|photo %.4f %.4f %.4f %d %d %d %d", v.zoom, v.panX, v.panY, v.rotate, v.fill, p.thumbId, p.photoWidth());
			photo = pb;
		}
		// the updater's pop-up: its state, and the button a finger is on
		std::string upd;
		if (updater)
			upd += "|upd" + std::to_string((int)updater->state()) + " " + std::to_string(updater->progress()) + " " + updater->available() +
				" " + std::to_string(popupShown(p)) + std::to_string(popupCursor) + std::to_string(pressedBtn);
		// the chapters: the list, the one playing, the tab View asks rows() for (the chapter card follows the position)
		photo += "|ch" + std::to_string(p.chapterSerial) + " " + std::to_string(p.chapters.size()) + " " +
			std::to_string(shownChapter(p)) + " t" + std::to_string(tracksTab);
		// the dots of "Loading..." go round
		if (p.loading() || p.thumbPending()) photo += "|loading" + std::to_string(SDL_GetTicks() / 400 % 4);
		// Playlists: the store, items found missing, a menu's title and line, a name refused
		photo += "|pl" + std::to_string(browse->playlists->plStore ? browse->playlists->plStore->serial() : -1) + " " + std::to_string(browse->playlists->plMissingSerial) + ctxTitle + "|" + ctxNote + "|" + browse->playlists->plNameNote;
		// selecting: the mode and where it adds, the ticks, Move's block, a dragged row's landing (the finger in steps
		// of 8), the picker, the arms, a folder being read
		if (browse->playlists->selMode != SEL_NONE || browse->playlists->pickerOpen || browse->playlists->addPending.serial)
		{
			char sb[200];
			snprintf(sb, sizeof sb, "|sel%d %d %d %d %d,%d %d %d,%d %d %d %d %d %d %d", browse->playlists->selMode, browse->playlists->selCount(), browse->playlists->selSerial, browse->playlists->moving,
				browse->playlists->plSel.empty() ? -1 : *browse->playlists->plSel.begin(), (int)browse->playlists->plSel.size(), browse->playlists->plDrag.on, browse->playlists->plDrag.on ? browse->playlists->dragSlot() : -1, browse->playlists->plDrag.y / 8, browse->playlists->pickerOpen,
				browse->playlists->pickerCursor, browse->playlists->pickerScroll, browse->playlists->removeArmed, browse->playlists->dropArmed != 0, browse->playlists->addPending.serial);
			photo += sb + browse->playlists->selDest;
		}
		// Main: what it plays from, and whether the file is a server's (a playlist's item says where it comes from)
		if (p.playing() && page == Page::Main)
		{
			const PlaySource& src = p.source();
			photo += "|src" + std::to_string((int)src.kind) + (src.shuffled ? "s" : "") + (isNetPath(p.currentPath()) ? "n" : "") +
				(p.netWaiting() ? "w" : "") + src.title + "|" + src.id;
		}
		photo += webSig(p);
		// About's support QR up: its hint bar says how to close it
		photo += aboutQrUp() ? "|qr" : "";
		if (galleryNew) photo += "|gal" + std::to_string(galleryNew);	// (test only: --press gallery:new)
		// where it plays (the player's Play on sheet and the status line)
		{
			SessionHost& sh = SessionHost::get();
			photo += "|out" + outputName() + (sh.pageWaits() ? "w" : "") + (sh.session().shuffle ? "S" : "");
		}
		// Like: the heart's pop, the Add square's check, the picker's one file and its question
		{
			Uint32 now = SDL_GetTicks();
			photo += "|like" + std::to_string(likePopAt && now - likePopAt < 110) + std::to_string(addFlashUntil && now < addFlashUntil) +
				std::to_string(browse->playlists->pickerArmed) + browse->playlists->pickerOne + "|" + std::to_string(likedNow(p));
		}
		// the clock: its minute
		if (!clockText(p).empty()) photo += "|clk" + std::to_string((long long)time(nullptr) / 60);
		if (incognito) photo += "|incognito";	// its mark beside the clock, its row in Manga settings
		// behind the browser: the rows' chip, the header's mark (its frame while it moves), what was stopped for a
		// photo
		{
			char bb[160];
			snprintf(bb, sizeof bb, "|bg%d %d %d %d %d %d %d %.0f", bgPlay, chipKind, landFlashUntil != 0 ? 1 : 0, playHoldMini,
				markW(p), markW(p) && markLive(p) && panel.on() ? (int)(SDL_GetTicks() / 166) : -1, p.loading(), 0.0);
			photo += bb + chipPath + "|" + p.currentPath();
		}
		// Files' copy or move: how far, and the file on its way; what's held for Copy / Move
		photo += fileJobSignature();
		photo += shell->signature(p);	// the rail's section and warning, the frame's pressed piece, the shell's mini bar
		if (Area* a = area()) photo += a->signature(p);
		return buf + photo + overlaySignature(p) + "|ld" + (loaderShown ? std::to_string(SDL_GetTicks() / 110) : std::string()) + "|xn" + manga->extNoticeText() + "|" + p.title + "|" + p.error + "|" + serializeSettings(p.settings) + "|bat" + std::to_string(battery.percent) + (battery.charging ? "c" : "") + (page == Page::Settings && settingsOver ? "|set1" : "") + (ctxOpen ? "|ctx" + std::to_string(ctxCursor) + std::to_string(pressedBtn) + ctxStatus + (ctxArmed ? "!" : "") : "") + upd
			+ (kbOpen ? "|kb" + sound->eqNameNote + std::to_string(kbCursor) + "," + std::to_string(kbPressed) + "," + std::to_string((int)kbShift) + (kbSymbols ? "s" : "") + std::to_string(kbEdit.caret) + kbEdit.text + (kbCaretShown() ? "|" : "") : "");
	}
}
