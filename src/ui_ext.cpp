#include "ui.h"
#include "format.h"
#include "manganet.h"
#include "pagedecode.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_shell.h"
#include "ui_manga.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"

namespace bp
{
	namespace
	{
		// a video is what the manga reader cannot share the memory with (a song or a photo goes on under it)
		bool videoPlays(const Player& p) { return p.playing() && !p.playingAudio() && !p.playingPhoto(); }

		std::string videoName(const Player& p)
		{
			return !p.title.empty() ? p.title : entryTitle(p.currentPath());
		}
	}

	// ---- the Extensions section's page ----

	void Ui::Manga::openManga(Player& p)
	{
		ui.page = Page::Manga;
		ui.cursor = ui.scroll = 0;
		extPressed = ui.pressedBtn = -1;
		mangaOpen(p);	// the store loaded, the card scanned, on Library
		// most sources need the manga engine: offered at each entry while it isn't on the card (Not now is for this
		// time only). A failed download with nothing kept counts as not there
		mengSheet = false;
		if (mangaNet && mangaNet->started())
		{
			JavaExtHost& h = mangaNet->javaExt();
			auto st = h.state();
			mengSheet = st == JavaExtHost::State::NotInstalled || (st == JavaExtHost::State::Failed && h.installedBytes() == 0);
			// an install on the old runtime (HotSpot): the lighter one offered the same way, each entry till it's taken
			auto up = h.upgrade();
			mengUpgrade = !mengSheet && h.runtimeOld() && (up == JavaExtHost::Upgrade::None || up == JavaExtHost::Upgrade::Failed);
			if (mengUpgrade) mengSheet = true;
			mengCursor = 0;
		}
	}

	void Ui::Manga::openExtensionsSection(Player& p)
	{
		// The rail's Extensions cell. Arriving starts and stops nothing: its list, or Manga again where it was left
		// (docs/redesign "Arriving stops nothing"), except while a video plays: then the list, so a stray L / R can
		// never stop a film. Adopted: the page draws in the shell's body on the kit (no bridged old body).
		ui.shell->adopt(Page::ExtSection);
		if (extReturn != Page::ExtSection && p.settings.mangaOn && !videoPlays(p))
		{
			ui.page = extReturn == Page::MangaSeries ? Page::MangaSeries : Page::Manga;
			extPressed = ui.pressedBtn = -1;
			if (ui.page == Page::Manga && !mangaReady) openManga(p);
			return;
		}
		openExtList(p);
	}

	void Ui::Manga::openExtList(Player& p)
	{
		ui.shell->adopt(Page::ExtSection);
		ui.page = Page::ExtSection;
		ui.cursor = ui.scroll = 0;
		extPageCursor = 0;
		extPagePressed = -1;
		extPressed = ui.pressedBtn = -1;
		extReturn = Page::ExtSection;
		(void)p;
	}

	void Ui::Manga::sectionLeft(Page from)
	{
		// Manga is where the Extensions section is left when a Manga page is the one being left: coming back lands
		// there again. Left on its list, it comes back to the list.
		extReturn = from == Page::Manga || from == Page::MangaSeries ? from : Page::ExtSection;
	}

	void Ui::Manga::enterManga(Player& p)
	{
		// the app where it was left once it has been opened (its tab, its cursor); opened afresh the first time
		if (mangaReady && p.settings.mangaOn)
		{
			ui.page = Page::Manga;
			ui.cursor = ui.scroll = 0;
			extPressed = ui.pressedBtn = -1;
			return;
		}
		openManga(p);
	}

	void Ui::Manga::extMangaRow(Player& p)
	{
		// A or a tap on Manga's row. With a video playing (or paused) the two can't run together: the question first,
		// unless it was turned off ("Don't ask again" is the setting). A song or a photo is never asked about.
		if (videoPlays(p) && p.settings.mangaAskVideo)
		{
			ui.askMangaGuard(p);
			return;
		}
		if (videoPlays(p)) { extStopVideo(p); return; }
		enterManga(p);
	}

	void Ui::Manga::extStopVideo(Player& p)
	{
		// Stop, everywhere: its place is kept (Home's Continue card reads "Stopped for Manga"), then the reader. The
		// page stays on Manga: main's Stop must not take it back to where the video was opened from.
		ui.stopKeepsPage = true;
		ui.stopPressed = true;
		enterManga(p);
		videoBlockedAt = SDL_GetTicks();
		guardStoppedVideo = true;	// (the sheet said so; the five-second note would say it twice)
		fprintf(stderr, "extensions: the video stopped for Manga\n");
	}

	void Ui::Manga::openExtensions(bool fromSettings)
	{
		ui.page = Page::Extensions;
		ui.cursor = ui.scroll = 0;
		extPressed = ui.pressedBtn = -1;
		extFromSettings = fromSettings;
	}

	void Ui::Manga::extToggleManga(Player& p)
	{
		Settings s = p.settings;
		s.mangaOn = !s.mangaOn;
		// the note about repositories is shown once, the first time it's turned on, wherever that was done
		bool first = s.mangaOn && !s.mangaIntroShown;
		if (first) s.mangaIntroShown = true;
		p.setSettings(s);
		ui.mangaOn = s.mangaOn;
		if (!s.mangaOn) { extSheet = false; mangaReady = false; extReturn = Page::ExtSection; return; }
		if (!first) return;
		extSheet = true;
		extSheetCursor = 0;
		ui.page = Page::Extensions;
		ui.cursor = ui.scroll = 0;
	}

	void Ui::Manga::extSheetChoose(int i, Player& p)
	{
		extSheet = false;
		if (i == 0) extMangaRow(p);
	}

	bool Ui::Manga::extSectionKey(Button b, Player& p)
	{
		int rows = p.settings.mangaOn ? 2 : 0;	// the extensions on, then Manage extensions
		if (!rows)
		{
			// the empty state: its one key, Manage extensions
			if (b == Button::A) openExtensions();
			else if (b == Button::B) ui.shell->openSection(Ui::Shell::Section::Home, p);
			return false;
		}
		if (b == Button::Up) extPageCursor = (extPageCursor + rows - 1) % rows;
		else if (b == Button::Down) extPageCursor = (extPageCursor + 1) % rows;
		else if (b == Button::A)
		{
			if (extPageCursor == 0) extMangaRow(p);
			else openExtensions();
		}
		else if (b == Button::B) ui.shell->openSection(Ui::Shell::Section::Home, p);
		return false;
	}

	bool Ui::Manga::extKey(Button b, Player& p)
	{
		if (ui.page == Page::ExtSection) return extSectionKey(b, p);
		if (ui.page == Page::Extensions && extSheet)
		{
			if (b == Button::Left) extSheetCursor = 0;
			else if (b == Button::Right) extSheetCursor = 1;
			else if (b == Button::A) extSheetChoose(extSheetCursor, p);
			else if (b == Button::B) extSheet = false;
			return false;
		}
		// the reader is an app of its own: B at its top goes back to the Extensions list
		if (ui.page == Page::Manga) return mangaKey(b, p);
		// Manage extensions (Page::Extensions)
		if (b == Button::Select && ui.nowPlayable(p)) { ui.goNowPlaying(p); return false; }
		switch (b)
		{
		case Button::A: extToggleManga(p); break;
		case Button::Start: ui.openSettings(p, true); break;
		case Button::B:
			// back to the door it came through: the Extensions page's row, or Settings › Extensions
			if (extFromSettings)
			{
				extFromSettings = false;
				ui.page = Page::Settings;
				ui.setGroup = -1;
				ui.cursor = 0;
			}
			else openExtList(p);
			break;
		default: break;
		}
		return false;
	}

	void Ui::Manga::drawExtSection(const Player& p)
	{
		Kit& k = *ui.kit;
		lay::Box body = ui.shell->layout(p).body;
		if (!p.settings.mangaOn)
		{
			// the Empty state (docs/redesign: ext-section-empty): what an extension is, and the one place to turn
			// one on; its one key is A, and its Rect is the one the touch tests
			lay::ExtEmpty e = lay::extEmpty(body);
			ui.drawGlyphMid(G_BLOCKS, e.icon.x + e.icon.w / 2, e.icon.y + e.icon.h / 2, DIM, 6);
			ui.drawTextMid("No extensions are on", 1, TEXT, e.title.x + e.title.w / 2, e.title.y + e.title.h / 2, e.title.w, 2);
			auto lines = ui.wrapText("An extension adds to Banana Player, like the Manga reader. Nothing of one shows or runs "
				"until you turn it on.", F_I12, e.lines.w, 2);
			for (size_t i = 0; i < lines.size(); i++)
				ui.drawTextMid(lines[i], F_I12, DIM, e.lines.x + e.lines.w / 2, e.lines.y + 10 + (int)i * 20, e.lines.w, 2);
			Kit::Key key;
			key.glyph = &G_GEAR;
			key.label = "Manage extensions";
			key.key = "A";
			key.tone = Kit::Tone::Primary;
			key.pressed = extPagePressed == EXT_EMPTY_KEY;
			k.key(e.key, key);
			return;
		}
		// one row per extension that is on (the Manga reader), then a rule and Manage extensions
		lay::ExtPage page = lay::extPage(body, 1);
		Kit::Row row;
		row.under = BG;
		row.mark = [this](lay::Box m) { drawMangaIcon(m); };
		row.name = "Manga reader";
		// what is in it once the reader has been opened (the store read); else what the extension is. Unread counts
		// aren't cheap without each series' chapters, so the line names the series and the downloads only.
		row.second = "Comics and manga from files, servers and sources";
		if (mangaStore && !mangaStore->library().empty())
		{
			int series = (int)mangaStore->library().size(), dl = mangaDlQueueCount();
			row.second = std::to_string(series) + " series" +
				(dl ? " \xC2\xB7 " + std::to_string(dl) + " downloading" : "");
		}
		row.pill = videoPlays(p) ? "Stops the video" : "";
		row.tone = Kit::Pill::Bad;
		row.trail = Kit::Trail::Chevron;
		row.focused = extPageCursor == 0;
		row.pressed = extPagePressed == 0;
		k.row(page.row[0], row);
		ui.fill(page.rule, RULE);
		Kit::Row manage;
		manage.under = BG;
		manage.glyph = &G_GEAR;
		manage.name = "Manage extensions";
		manage.note = "On / off \xC2\xB7 their settings";
		manage.trail = Kit::Trail::Chevron;
		manage.focused = extPageCursor == 1;
		manage.pressed = extPagePressed == 1;
		k.row(page.manage, manage);
		// the line under them: what arriving costs (nothing), or who keeps playing and what would stop it
		std::string note = videoPlays(p)
			? videoName(p) + " keeps playing while you are here. Only opening Manga stops it, and it asks first."
			: "Coming here starts nothing. An extension runs only once you open it.";
		std::vector<std::string> lines = ui.wrapText(note, F_I12, page.area.w - 24, 2);
		int y = page.manage.y + page.manage.h + 12;
		for (auto& line : lines)
		{
			ui.drawTextMid(line, F_I12, DIM, page.area.x + 12, y, page.area.w - 24);
			y += 18;
		}
	}

	bool Ui::Manga::extPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (ui.page == Page::ExtSection)
		{
			// its rows and its one key test the Rects drawExtSection drew (lay::extPage, lay::emptyState)
			lay::Box body = ui.shell->layout(p).body;
			if (!p.settings.mangaOn)
			{
				lay::ExtEmpty e = lay::extEmpty(body);
				if (pt.kind == Pointer::Down) extPagePressed = e.key.hit(x, y) ? EXT_EMPTY_KEY : -1;
				else if (pt.kind == Pointer::Up && extPagePressed == EXT_EMPTY_KEY)
				{
					extPagePressed = -1;
					if (e.key.hit(x, y)) openExtensions();
				}
				return false;
			}
			lay::ExtPage page = lay::extPage(body, 1);
			if (pt.kind == Pointer::Down)
			{
				extPagePressed = -1;
				if (page.row[0].hit(x, y)) { extPageCursor = 0; extPagePressed = 0; }
				else if (page.manage.hit(x, y)) { extPageCursor = 1; extPagePressed = 1; }
				return false;
			}
			if (pt.kind != Pointer::Up || extPagePressed < 0) return false;
			int id = extPagePressed;
			extPagePressed = -1;
			if (id == 0 && page.row[0].hit(x, y)) extMangaRow(p);
			else if (id == 1 && page.manage.hit(x, y)) openExtensions();
			return false;
		}
		if (pt.kind == Pointer::Down)
		{
			extPressed = -1;
			if (ui.page == Page::Manga) return mangaPointer(pt, x, y, p);
			if (ui.page == Page::Extensions && extSheet)
			{
				for (int i = 0; i < 2; i++)
					if (extSheetBtn(i).hit(x, y)) extPressed = EXT_OPEN + i;
				return false;
			}
			if (extRowRect().hit(x, y)) extPressed = EXT_ROW;
			if (extChipRect().hit(x, y)) extPressed = EXT_CHIP;
			return false;
		}
		if (pt.kind != Pointer::Up || extPressed < 0) return false;
		int id = extPressed;
		extPressed = -1;
		Rect r = id == EXT_ROW ? extRowRect() : id == EXT_CHIP ? extChipRect() : id == EXT_GEAR ? extGearRect()
			: extSheetBtn(id - EXT_OPEN);
		// a finger that slid away changed its mind
		return r.hit(x, y) && extAct(id, p);
	}

	void Ui::Manga::openExtSettings()
	{
		// the manga reader's own page (not Banana Player's Settings): B comes back here, as it was
		openMangaSettings();
	}

	bool Ui::Manga::extAct(int id, Player& p)
	{
		if (id == EXT_OPEN || id == EXT_DONE) { extSheetChoose(id - EXT_OPEN, p); return false; }
		if (id == EXT_MANAGE_ROW) openExtensions();
		else if (id == EXT_ROW || id == EXT_CHIP) extToggleManga(p);
		else if (id == EXT_GEAR) openExtSettings();
		return false;
	}

	void Ui::Manga::drawExtensions(const Player& p)
	{
		ui.drawHeader("Manage extensions", p);
		ui.drawTextMid("1 extension", 0, DIM, 16, 64);
		// the extension's row: its mark, its name and what it does, its switch, its settings
		Rect row = extRowRect();
		ui.drawFocus(row);
		drawMangaIcon({30, 100, 46, 46});
		ui.drawTextMid("Manga reader", 1, ACCENT, 92, 110, 360);
		ui.drawTextMid("Comics and manga from files and servers", 0, DIM, 92, 136, 360);
		bool on = p.settings.mangaOn;
		Rect chip = extChipRect();
		ui.fillRound(chip, on ? ACCENT : BUTTON, R_CHIP);
		ui.drawTextMid(on ? "On" : "Off", 0, on ? ON_ACCENT : TEXT, chip.x + chip.w / 2, chip.y + chip.h / 2, chip.w - 8, 2);
		// (on and off only: the extension's settings are its own, START inside it)
		ui.drawTextMid("More extensions come with later versions.", 0, DIM, W / 2, 190, 560, 2);
		if (extSheet)
		{
			drawExtSheet(p);
			return;
		}
		ui.drawHints({{"A", "On / Off"}, {"B", "Back"}});
	}

	void Ui::Manga::drawExtSheet(const Player& p)
	{
		(void)p;
		// over the page, dimmed: the note about repositories, once, the first time the reader is turned on
		ui.fill({0, 0, W, H}, sdl(theme::BG, (Uint8)(255 * theme::VEIL_SHEET / 100)));	// (the veil: BG, as a sheet's)
		ui.drawBox(extSheetBox(), SURFACE, R_DIALOG);
		drawMangaIcon({88, 108, 40, 40});
		ui.drawTextMid("Manga reader is on", 1, TEXT, 136, 128, 420);
		ui.drawTextMid("It reads from your files and servers, and from", 0, TEXT, 92, 174, 456);
		ui.drawTextMid("sources in repositories you add.", 0, TEXT, 92, 196, 456);
		ui.drawTextMid("Repositories are added by you. Banana Player", 0, DIM, 92, 232, 456);
		ui.drawTextMid("doesn\xE2\x80\x99t provide or check their content.", 0, DIM, 92, 254, 456);
		for (int i = 0; i < 2; i++)
		{
			Rect b = extSheetBtn(i);
			bool at = extSheetCursor == i;
			ui.drawBox(b, extPressed == EXT_OPEN + i ? PRESSED : at ? ACCENT : BUTTON, R_BUTTON, at);
			ui.drawTextMid(i ? "Done" : "Open Manga", 1, at ? ON_ACCENT : TEXT, b.x + b.w / 2, b.y + b.h / 2, b.w - 16, 2);
		}
		ui.drawHints({{"A", "Choose"}, {"LEFT/RIGHT", "Move"}, {"B", "Done"}});
	}

	// ---- the guard: Manga while a video plays (docs/redesign: manga-guard) ----

	struct Ui::MangaGuard : Ui::Overlay
	{
		std::string title, at;	// the video and where it stops ("Sintel", "4:12")
		int cursor = 0;			// 0 Stay (the safe key), 1 Stop video & open Manga
		bool dontAsk = false;

		MangaGuard(Ui& u, const Player& p) : Overlay(u)
		{
			title = videoName(p);
			at = formatTime(ui.shownPosition(p));
		}
		const char* name() const override { return "mangaguard"; }

		std::vector<std::string> lines() const
		{
			std::vector<std::string> out = ui.kit->sheetText("Manga needs the memory the video is using, so the two can\xE2\x80\x99t run together.");
			for (auto& l : ui.kit->sheetText(title + " stops at " + at + " and waits in Continue on Home. Music and photos are not affected."))
				out.push_back(l);
			return out;
		}
		lay::ConfirmSheet sheet() const { return lay::confirmSheet((int)lines().size(), true); }

		void open(Player& p)
		{
			close();
			if (dontAsk)
			{
				Settings s = p.settings;
				s.mangaAskVideo = false;
				p.setSettings(s);
				fprintf(stderr, "extensions: Don't ask again, so a video stops without the question\n");
			}
			ui.manga->extStopVideo(p);
		}
		bool key(Button b, Player& p) override
		{
			if (b == Button::Left || b == Button::Right || b == Button::Up || b == Button::Down) cursor ^= 1;
			else if (b == Button::Y) dontAsk = !dontAsk;
			else if (b == Button::A) { if (cursor == 0) close(); else open(p); }
			else if (b == Button::B) close();	// Stay
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			if (pt.kind != Pointer::Up) return false;
			lay::ConfirmSheet l = sheet();
			if (l.safe.hit(x, y)) { close(); return false; }
			if (l.action.hit(x, y)) { open(p); return false; }
			if (l.foot.hit(x, y)) dontAsk = !dontAsk;
			return false;
		}
		void draw(const Player&) override
		{
			Kit::ConfirmSpec c;
			c.title = "Stop the video to open Manga?";
			c.safe = "Stay";
			c.action = "Stop video & open Manga";
			c.danger = true;	// (red ink: the video stops)
			c.focus = cursor;
			c.foot = "Don\xE2\x80\x99t ask again: stop the video without asking";
			c.footOn = dontAsk;
			c.footKey = "Y";
			ui.kit->confirmSheet(sheet(), c, lines());
		}
		std::string signature(const Player&) const override
		{
			return title + "|" + at + "|" + std::to_string(cursor) + (dontAsk ? "1" : "0");
		}
	};

	void Ui::askMangaGuard(const Player& p)
	{
		pushOverlay(std::make_unique<MangaGuard>(*this, p));
	}

	void Ui::Manga::drawMangaIcon(Rect r)
	{
		// the manga reader's picture (assets/icons/manga.png beside the fonts), decoded at the size it's drawn (smooth,
		// as drawn; again when a place of another size asks)
		SDL_Rect d = ui.devRect(r);
		int s = std::min(d.w, d.h);
		if (s != mangaIconPx)
		{
			if (mangaIconTex) SDL_DestroyTexture(mangaIconTex);
			mangaIconTex = nullptr;
			mangaIconPx = s;
			PageImage img = decodePageFile(ui.fontsDir + "/../icons/manga.png", s);
			if (img.ok())
			{
				mangaIconTex = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
				if (mangaIconTex)
				{
					SDL_UpdateTexture(mangaIconTex, nullptr, img.rgba.data(), img.w * 4);
					SDL_SetTextureBlendMode(mangaIconTex, SDL_BLENDMODE_BLEND);
				}
			}
		}
		SDL_Rect c{d.x + (d.w - s) / 2, d.y + (d.h - s) / 2, s, s};
		if (mangaIconTex) SDL_RenderCopy(ui.ren, mangaIconTex, nullptr, &c);
		else ui.drawGlyphMid(G_BOOK, r.x + r.w / 2, r.y + r.h / 2, ACCENT, 3);	// (no picture: the book, as before)
	}
}
