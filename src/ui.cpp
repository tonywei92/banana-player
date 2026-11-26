#include "ui.h"
#include "fontruns.h"
#include "ui_smoothlist.h"
#include "session_host.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include "theme.h"
#include "stickscroll.h"
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
#include "ui_sleep.h"
#include "ui_shell.h"
#include "ui_search.h"
#include "ui_music.h"
#include "ui_videos.h"
#include "ui_mediaart.h"
#include "ui_books.h"
#include "ui_catalogs.h"
#include "durworker.h"

namespace bp
{
	bool Ui::init(SDL_Window* window, const std::string& dir)
	{
		fontsDir = dir;
		panel.init();
		topPanel.init();
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
		static const char* FALLBACKS[] = {FONT_CJK, "NotoSansThai.ttf",
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
		shell = std::make_unique<Shell>(*this);
		pageReader = std::make_unique<PageReader>(*this);	// (not an area: no page of its own; Docs and Manga use it)
		docs = std::make_unique<Docs>(*this);
		areas.push_back(docs.get());
		sound = std::make_unique<Sound>(*this);
		webPage = std::make_unique<Web>(*this);
		areas.push_back(webPage.get());
		manga = std::make_unique<Manga>(*this);
		areas.push_back(manga.get());
		settingsPage = std::make_unique<SettingsPage>(*this);
		areas.push_back(settingsPage.get());
		browse = std::make_unique<Browse>(*this);
		areas.push_back(browse.get());
		// the rail's library sections, filled by themselves from the cards' index (main hands each the index)
		music = std::make_unique<Music>(*this);
		areas.push_back(music.get());
		videos = std::make_unique<Videos>(*this);
		areas.push_back(videos.get());
		books = std::make_unique<Books>(*this);
		areas.push_back(books.get());
		mediaArt = std::make_unique<MediaArt>(*this);
		kit = std::make_unique<Kit>(*this);	// (not an area: the redesign's components, drawn by the areas rebuilt on them)
		search = std::make_unique<Search>(*this);	// (its constructor adopts Page::Search: drawn in the shell's body)
		areas.push_back(search.get());
		playerPage = std::make_unique<PlayerPage>(*this);
		areas.push_back(playerPage.get());
		sleep = std::make_unique<Sleep>(*this);	// (not an area: the sleep timer, over every page)
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
		cache.clear();
		mediaArt.reset();	// (its thumbnails' textures, before the renderer goes)
		if (listShown) listShown->release();	// (its rows' textures, before the renderer goes)
		undim();	// (the quit: the bottom panel's level as it was)
		// and a list drawn under the Add to… picker (the page's, while the picker's is the one shown), and the picker's
		for (Area* a : areas)
			if (SmoothList* l = a->smoothList()) l->release();
		browse->list.release();
		if (browse->playlists->pickerRows) browse->playlists->pickerRows->release();
		for (auto& c : manga->coverTex)
			if (c.second.tex) SDL_DestroyTexture(c.second.tex);
		for (auto* t : corners)
			if (t) SDL_DestroyTexture(t);
		for (auto* f : fonts)
			if (f) TTF_CloseFont(f);
		if (pinFont) TTF_CloseFont(pinFont);
		for (auto& f : fallbacks)
			for (auto* x : f) if (x) TTF_CloseFont(x);
		for (auto* x : jaForms) if (x) TTF_CloseFont(x);
		if (ren) SDL_DestroyRenderer(ren);
	}

	void Ui::stickAxis(int value, Player& p, int valueX)
	{
		stickLY = value;
		stickLX = valueX;
		// a zoomed photo pans under the stick (a full push is a panel's width or height a second); lists scroll in draw()
		Uint32 now = SDL_GetTicks();
		double sx = stickScrollSpeed(valueX, 1.0), sy = stickScrollSpeed(value, 1.0);
		if ((sx != 0 || sy != 0) && page == Page::Main && p.playingPhoto() && !topOverlay() && !panTouch)
		{
			Uint32 dt = stickPhotoAt ? std::min<Uint32>(now - stickPhotoAt, 50) : 16;
			stickPhotoAt = now;
			PhotoScale sc = p.photoScaleNow();
			const Player::PhotoView& v = p.photo();
			if (dt && (sc.x > 1.001 || sc.y > 1.001))
			{
				flickAt = 0;
				// the view moves toward the push, so the photo (mpv's pan) goes the other way
				p.setPhotoView(v.zoom, v.panX - (sc.x > 1.001 ? sx * dt / 1000.0 / sc.x : 0),
					v.panY - (sc.y > 1.001 ? sy * dt / 1000.0 / sc.y : 0));
			}
		}
		else stickPhotoAt = 0;
		// one reader at a time; the other gets a 0, so a stick held as its page closes stops it at once
		// (both axes: a page or a strip zoomed wider than the screen pans sideways under LEFTX)
		if (page == Page::DocText || page == Page::DocPages) docs->stickAxis(value, p, valueX);
		else docs->stickAxis(0, p);
		if (page == Page::MangaRead) manga->stickAxis(value, p, valueX);
		else manga->stickAxis(0, p);
	}

	// START opens Settings everywhere in the shell and on the player. The exceptions are the places where START is a
	// page's own key: a form's Save/Done (and Find on the network), the on-screen keyboard (taken before this), and
	// every reader, where START is how the page is shown (Files' View, the text's options, the Manga reader's Reading
	// menu; the Book bar while it is held sideways): SELECT there is the full view (docs/redesign/README.md,
	// "The readers' keys").
	bool Ui::startOpensSettings(const Player& p) const
	{
		// (Books › Catalogs' form: START is its Save, as Network's form's)
		if (page == Page::Books && books->onCatalogs() && books->catalogs->view() == Books::Catalogs::V_FORM) return false;
		if (page == Page::Settings || page == Page::About || page == Page::Main || page == Page::ExtSection ||
			page == Page::Extensions || page == Page::Music || page == Page::Videos || page == Page::Books)
			return true;
		// Manga's pages in the shell: START is Manga settings (the gear key beside their tabs wears its cap), handled
		// by Manga itself; the reader's START is its View sheet
		if (manga->shellPage(page)) return false;
		// Search: START is Done to the results while typing, and its page's own key otherwise
		if (page == Page::Search) return false;
		if (page == Page::Browse)
		{
			if (browse->browseDir == FORMDIR || browse->browseDir == FINDNET) return false;
			if (browse->playlists->selMode != SEL_NONE || browse->playlists->addPending.serial) return false;
			return true;
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
		// the sleep timer's dark (it paused, both screens off): a key wakes the screens, calls the sleep off, and does
		// nothing else (it stays paused)
		if (sleep->wake())
		{
			webInputAt = SDL_GetTicks();
			return false;
		}
		// an overlay (the readers' Brightness sheet, a message's dialog, the quick settings, Files' Info card) takes the
		// keys while it shows
		// any key restarts Screen off's count, an overlay's too (Settings, a sheet): before, the B that closed Settings
		// after a while in it left the count where it was, and the bottom panel went dark at once
		// the dim before Screen off: a press only cancels it (the count restarts below)
		bool veiled = veilCancels && (veilAlpha > 0 || dimmed());
		webInputAt = SDL_GetTicks();
		if (veiled)
		{
			veilAlpha = 0;
			veilCancels = false;
			undim();
			return false;
		}
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
		// the shell's pages: L2 / R2 step the sections, L3 is Search
		if (shell->page(page) && shell->key(b, p)) return false;
		// L / R step a shell page's tabs (the owner, 2026-11-20: a page's tabs are L / R everywhere, the sections L2 / R2).
		// Settings and the Web page: the group before or after; Music's views (Albums / Artists / Playlists) and
		// Videos' (All / Folders / Not finished), Books' (Reading now / Shelf); the pages below them (an album's, an artist's, a folder's) keep the
		// keys. The rest (Manga's tabs, a source's, All recent, Files and Network) are their own areas' keys
		if (b == Button::L || b == Button::R)
		{
			if (page == Page::Settings || page == Page::Web) { area()->key(b, p); return false; }
			if (page == Page::Music || page == Page::Videos || page == Page::Books) { area()->key(b, p); return false; }
			// Music's Playlists view (the browser's own page): back to Albums / Artists
			if (page == Page::Browse && browse->browseDir == PLROOT && browse->playlists->selMode == SEL_NONE &&
				!browse->playlists->moving && !browse->playlists->addPending.serial)
			{
				music->viewStep(b == Button::R ? 1 : -1, p);
				return false;
			}
			// About is a settings group too (its page its own): back to its neighbours
			if (page == Page::About)
			{
				setGroup = GROUP_ABOUT;
				settingsPage->stepGroup(b == Button::R ? 1 : -1, &p);
				aboutQr = false;
				return false;
			}
		}
		// L2/R2: the chapter before or after, over what plays and its two pages (the Chapters page's mark follows).
		// Not in START's Settings, not over a photo (its keys are its own), and nothing with Button hotkeys off: they
		// have no touch button to stand for (the manga reader has its own: the chapter before or after)
		if (b == Button::L2 || b == Button::R2)
		{
			if (page != Page::Manga && page != Page::MangaSeries && page != Page::MangaRead && page != Page::MangaSources &&
				page != Page::DocText && page != Page::DocPages && page != Page::Search)	// (Search's keyboard: L2 is Clear)
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
		case Button::B: choosePopup(popupSafe(), p); break;
		// a failed update's Details: unfolded in place (the sheet stays up)
		case Button::Y:
			if (updater->state() == Updater::State::Failed) choosePopup(2, p);
			break;
		default: break;
		}
		return false;
	}

	bool Ui::listKey(Button b, Player& p)
	{
		// (the browser's Up row is row -1: first; n counts it)
		int first = browse->listFirst(), n = (int)rows(p).size() - first;
		cursor = std::min(cursor, n - 1 + first);
		// All recent's group headings are passed over: Up and Down land on an item
		bool recent = page == Page::Browse && browse->browseDir == RECENT;
		auto isHeading = [&](int i) { return recent && i >= 0 && i < (int)browse->browseRows.size() && browse->browseRows[i].id == -3; };
		switch (b)
		{
		case Button::Up:
			cursor = (cursor - first + n - 1) % n + first;
			for (int g = 0; n > 1 && isHeading(cursor) && g < n; g++) cursor = (cursor - first + n - 1) % n + first;
			break;
		case Button::Down:
			cursor = (cursor - first + 1) % n + first;
			for (int g = 0; n > 1 && isHeading(cursor) && g < n; g++) cursor = (cursor - first + 1) % n + first;
			break;
		// a page at a time (the browser): LEFT/RIGHT, and L/R as BananaOS's lists jump
		case Button::Left:
			cursor = std::max(first, cursor - ROWS_SHOWN);
			for (int g = 0; n > 1 && isHeading(cursor) && g < n && cursor > first; g++) cursor--;
			break;
		case Button::Right:
			cursor = std::min(n - 1 + first, cursor + ROWS_SHOWN);
			for (int g = 0; n > 1 && isHeading(cursor) && g < n && cursor < n - 1 + first; g++) cursor++;
			break;
		case Button::A: choose(cursor, p); break;
		case Button::B: page = home(p); break;
		default: break;
		}
		if (cursor < scroll) scroll = cursor;
		if (cursor >= scroll + ROWS_SHOWN) scroll = cursor - ROWS_SHOWN + 1;
		return false;
	}

	void Ui::slideshowTick(Player& p)
	{
		if (!slideshow) return;
		// over when the viewer is not what shows (closed, or the player of what plays took the screen)
		if (!p.playingPhoto() || page != Page::Main) { slideshow = false; return; }
		uint32_t now = SDL_GetTicks();
		if (now - slideAt < SLIDE_MS) return;
		slideAt = now;
		if (!p.next())
		{
			slideshow = false;
			p.osd("End of the slideshow", 2000);
		}
	}

	void Ui::tick(Player& p)
	{
		// a held slider whose target is told at a pace: a finger resting on a new value, sent when it's due
		slideLive(p);
		wifiTick();
		manga->mangaVideoGuard(p);
		slideshowTick(p);
		backdropTick(p);	// Match album art: the playing song's backdrop asked for once, or dropped
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
		// the last song or video that played, noted whatever page shows (Home's card after Stop)
		if (p.playing() && !p.playingPhoto() && (isAudioFile(p.currentPath()) || isVideoFile(p.currentPath()) || p.playingAudio()))
			browse->homeView->lastLive = p.currentPath();
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
		{
			undim();	// (its level back under the dark: it comes back lit at full)
			panel.set(false);
		}
		// the last 5 s before that Screen off: the panel dims as a phone's does (dimStep; a press only lifts it), or,
		// reading, the controls' own veil after its idle time (a touch lifts it and still turns the page)
		{
			Uint32 idle = SDL_GetTicks() - webInputAt;
			int a = 0;
			bool cancels = false;
			bool offWait = offSecs > 0 && panel.on() && p.playing() && !viewing && !p.paused && !p.loading() && p.error.empty() &&
				!kbOpen && !topOverlay() && !popupShown(p) && !slide.on;
			if (p.settings.dimBeforeOff && offWait && offSecs > 5 && idle + 5000 >= (Uint32)offSecs * 1000)
			{
				// eased in over DIM_RAMP_MS from the moment the last 5 s began
				Uint32 into = idle + 5000 - (Uint32)offSecs * 1000;
				double f = std::min(1.0, into / (double)DIM_RAMP_MS);
				f = f * f * (3 - 2 * f);
				if (!dimStep(f)) a = (int)std::lround(170 * f);	// (no backlight: the veil stands in, without words)
				cancels = true;
			}
			else
			{
				// (the sleep timer's last minute keeps a dimmed bottom screen dimmed: keys still work, the card is the way out)
				if (!(dimmed() && findOverlay("sleepwarn"))) undim();
				if (int rs = screenOffSeconds(p.settings.readDimAfter); rs > 0 && panel.on() &&
					(pageReader->readerStatus.open || (docs && docs->textStatus.open)) && !kbOpen && !topOverlay() && idle >= (Uint32)rs * 1000)
					a = 200;
			}
			veilAlpha = a;
			veilCancels = cancels;
		}
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
		if (id == BACK_ICON && page == Page::Browse && browse->fileSel) { browse->filesSelEnd(p); return false; }
		if (id == BACK_ICON && page == Page::Browse && (browse->playlists->selMode == SEL_ADD || browse->playlists->selMode == SEL_PICK)) { browse->playlists->selBack(p, true); return false; }
		// Search's < is always "back" (B on its keyboard deletes a letter first)
		if (id == BACK_ICON && page == Page::Search) { search->back(p); return false; }
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

	bool Ui::dimStep(double f)
	{
		if (!backlight.present(Backlight::BOTTOM)) return false;
		OffDim& d = offDim;
		if (d.from < 0)
		{
			int now = backlight.permille(Backlight::BOTTOM);
			if (now <= 0) return false;
			d.from = now;
			d.wrote = -1;
			d.since = SDL_GetTicks();
			fprintf(stderr, "screen off: dimming the bottom panel from %d\n", now);
		}
		// a third of the level, at least 3% (a phone's dim: still readable, plainly about to go)
		int target = std::min(d.from, std::max(30, d.from / 3));
		int pm = (int)std::lround(d.from + (target - d.from) * f);
		if (pm != d.wrote && backlight.setNow(Backlight::BOTTOM, pm)) d.wrote = pm;
		if (f >= 1) d.since = 0;	// (down: nothing eases any more, the loop can rest)
		return true;
	}

	void Ui::undim()
	{
		OffDim& d = offDim;
		if (d.from < 0) return;
		// put back unless the level is no longer ours (the brightness keys moved it meanwhile): one sysfs step is
		// a few thousandths, so near what was written is still ours
		int now = backlight.permille(Backlight::BOTTOM);
		bool ours = d.wrote < 0 || (now >= 0 && std::abs(now - d.wrote) <= 8);
		if (ours) backlight.setNow(Backlight::BOTTOM, d.from);
		fprintf(stderr, "screen off: dim lifted, bottom panel %s %d\n", ours ? "back to" : "left at (changed meanwhile)", ours ? d.from : now);
		d = OffDim{};
	}

	bool Ui::pointer(const Pointer& pt, Player& p)
	{
		int x = (int)(pt.x * W), y = (int)(pt.y * H);
		// the finger's time: the event's (a smooth list's speed comes from it), the harness's steps now
		touchMs = pointerMs ? pointerMs : SDL_GetTicks();
		pointerMs = 0;
		bool veiled = veilCancels && (veilAlpha > 0 || dimmed());
		webInputAt = SDL_GetTicks();
		if (veiled)
		{
			veilAlpha = 0;
			veilCancels = false;
			undim();
			if (pt.kind == Pointer::Down) swallowTouch = true;
			return false;
		}
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
		// the sleep timer's dark: the first touch wakes the screens (as a key does) and presses nothing
		if (pt.kind == Pointer::Down && sleep->wake())
		{
			swallowTouch = true;
			return false;
		}
		if (topOverlay()) return overlayPointer(pt, x, y, p);
		mangaOn = p.settings.mangaOn;
		if (fileJob) return fileJobPointer(pt, x, y);
		// the keyboard covers the whole panel: it takes the finger before the shell's rail and header under it (its
		// left keys sit over the rail: q, a and z were switching sections). A touch just after it closes is let go
		if (!kbOpen && kbClosedAt && SDL_GetTicks() - kbClosedAt < 400) return false;
		if (kbOpen && panel.on())
		{
			kbPointer(pt, x, y);
			if (sound->eqNameSubmit) sound->nameDone(p);
			if (browse->playlists->plNameSubmit) browse->playlists->plNameDone(p);
			if (browse->fileNameSubmit) browse->fileRenameDone(p);
			return false;
		}
		if (browse->clipPointer(pt, x, y, p)) return false;
		// the shell's rail, header and mini bar first (the page's body is under them)
		if (shell->page(page) && shell->pointer(pt, x, y, p)) return false;
		// Manga's pages: the kit's tabs take their touches first, and a sheet over the frame takes the finger at full size
		bool mangaSheet = manga->shellPage(page) && manga->shellModal();
		if (shell->page(page) && manga->shellPage(page) && !mangaSheet && manga->shellPointer(pt, x, y, p)) return false;
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
		// the header's sleep timer badge: its sheet (only the down, as the cluster beside it)
		if (sleepHit.w > 0 && sleepHit.hit(x, y) && pt.kind == Pointer::Down && !ctxOpen && !browse->playlists->pickerOpen &&
			!popupShown(p) && !kbOpen && sleep->running())
		{
			sleep->openSheet();
			swallowTouch = true;
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
			// a settings group's rows (and the Manga settings'): a smooth list (ui_smoothlist.h) that follows the
			// finger by the pixel and glides on after the lift; a tap acts as below, on the row the finger went down on
			SmoothList* smooth = set && !groups ? (page == Page::MangaSettings ? &settingsPage->mangaList : &settingsPage->list) : nullptr;
			if (smooth && pt.kind == Pointer::Move)
			{
				if (smooth->move(pt.y * H) && !listScrolled)
				{
					listScrolled = true;
					holdAt = 0;
					fprintf(stderr, "touch: list drag page %d y %d -> %d (smooth)\n", (int)page, touchStartY, y);
				}
				return false;
			}
			if (smooth && pt.kind == Pointer::Up)
			{
				smooth->lift();
				if (settingsPage->rowsCaught) listScrolled = true;	// (it stopped a glide: no tap)
			}
			if (!smooth && pt.kind == Pointer::Move && (listScrolled || std::abs(y - touchStartY) > 12))
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
					lay::SettingsRows l = page == Page::Settings || page == Page::MangaSettings ? lay::settingsRowsIn(lay::shell().body)
						: lay::settingsRows();
					Rect box = smooth ? smooth->rowBox(cursor) : Rect(l.row(cursor - scroll));
					box.h = lay::SET_ROW_H - 2;
					lay::SettingStepper st = lay::settingStepper(box);
					dir = lay::stepperBack(st, touchX) ? -1 : 1;
				}
				if (groups) settingsPage->openGroup(cursor, &p);
				else if (set && page == Page::Settings && (srows[cursor] == SOUND_ROW || srows[cursor] == SOUND_FX_ROW)) sound->open(p, srows[cursor] == SOUND_FX_ROW);	// a page, not a value
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
		photo += sleep->signature();	// the sleep timer's shown state: the key, the badge, More's tile (by the second in its last minute)
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
		// Match album art behind the browser: which answer, and the cross-fade's step while it runs
		if (browserBackdrop(p))
		{
			photo += "|bg" + std::to_string(backdropHash());
			if (backdropFading()) photo += "." + std::to_string((SDL_GetTicks() - backdropFadeAt()) / 33);
			if (backdropLeaving()) photo += "|bgout" + std::to_string((SDL_GetTicks() - bgLeaveAt) / 33);
		}
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
				" " + std::to_string(popupShown(p)) + std::to_string(popupCursor) + std::to_string(pressedBtn) + std::to_string(popupDetails);
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
				browse->playlists->pickerCursor, (int)browse->playlists->pickerScrolled, browse->playlists->removeArmed, browse->playlists->dropArmed != 0, browse->playlists->addPending.serial);
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
		photo += "|hb" + std::to_string(p.boost) + (wifiOff() ? "w" : "") + (SessionHost::get().pageIsOutput() ? "b" : "");	// the header's state badges
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
		return buf + photo + overlaySignature(p) + "|ld" + (loaderShown ? std::to_string(SDL_GetTicks() / 110) : std::string()) + "|xn" + manga->extNoticeText() + "|veil" + std::to_string(veilAlpha) + "|" + p.title + "|" + p.error + "|" + serializeSettings(p.settings) + "|bat" + std::to_string(battery.percent) + (battery.charging ? "c" : "") + (page == Page::Settings && settingsOver ? "|set1" : "") + (ctxOpen ? "|ctx" + std::to_string(ctxCursor) + std::to_string(pressedBtn) + ctxStatus + (ctxArmed ? "!" : "") : "") + upd
			+ (kbOpen ? "|kb" + sound->eqNameNote + std::to_string(kbCursor) + "," + std::to_string(kbPressed) + "," + std::to_string(kbPad.layer * 2 + kbPad.shift) + "," + std::to_string(kbEdit.caret) + kbEdit.text + (kbCaretShown() ? "|" : "") : "");
	}
}
