#include "ui.h"
#include "manga_icon.h"
#include "ui_internal.h"

namespace bp
{
	// ---- the Extensions drawer, Manage extensions and the manga reader's page ----

	void Ui::openManga(Player& p)
	{
		page = Page::Manga;
		cursor = scroll = 0;
		extPressed = pressedBtn = -1;
		mangaOpen(p);	// the store loaded, the card scanned, on Library
	}

	void Ui::openExtensions()
	{
		page = Page::Extensions;
		cursor = scroll = 0;
		extPressed = pressedBtn = -1;
	}

	void Ui::openExtensionsDrawer(Player& p)
	{
		extSheet = false;
		page = Page::Browse;
		inMenu = false;
		setSection(TAB_EXT);
		enterFolder(EXTDIR, p);
		cursor = scroll = 0;
		extPressed = pressedBtn = -1;
	}

	void Ui::extToggleManga(Player& p)
	{
		Settings s = p.settings;
		s.mangaOn = !s.mangaOn;
		// the note about repositories is shown once, the first time it's turned on, wherever that was done
		bool first = s.mangaOn && !s.mangaIntroShown;
		if (first) s.mangaIntroShown = true;
		p.setSettings(s);
		mangaOn = s.mangaOn;
		if (!s.mangaOn) { extSheet = false; return; }
		if (!first) return;
		extSheet = true;
		extSheetCursor = 0;
		page = Page::Extensions;
		cursor = scroll = 0;
	}

	void Ui::extSheetChoose(int i, Player& p)
	{
		extSheet = false;
		if (i == 0) openManga(p);
	}

	bool Ui::extDrawerKey(Button b, Player& p)
	{
		if (b == Button::Start) { openExtensions(); return true; }
		if (b != Button::A) return false;
		// what's on (the manga reader); with none on, its button's page
		if (p.settings.mangaOn) openManga(p);
		else openExtensions();
		return true;
	}

	bool Ui::extKey(Button b, Player& p)
	{
		if (extSheet && page == Page::Extensions)
		{
			if (b == Button::Left) extSheetCursor = 0;
			else if (b == Button::Right) extSheetCursor = 1;
			else if (b == Button::A) extSheetChoose(extSheetCursor, p);
			else if (b == Button::B) extSheet = false;
			return false;
		}
		// the reader is an app of its own: B at its top goes back to the drawer
		if (page == Page::Manga) return mangaKey(b, p);
		// (the mini bar shown here says SELECT goes to what plays)
		if (b == Button::Select && nowPlayable(p)) { goNowPlaying(p); return false; }
		switch (b)
		{
		case Button::A: extToggleManga(p); break;
		case Button::Start: openMainMenu(p, playingTabShown(p) ? TAB_PLAYING : TAB_SETTINGS); break;
		case Button::B: openExtensionsDrawer(p); break;
		default: break;
		}
		return false;
	}

	bool Ui::extPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind == Pointer::Down)
		{
			extPressed = -1;
			if (extSheet && page == Page::Extensions)
			{
				for (int i = 0; i < 2; i++)
					if (extSheetBtn(i).hit(x, y)) extPressed = EXT_OPEN + i;
				return false;
			}
			if (page == Page::Manga) return mangaPointer(pt, x, y, p);
			if (page == Page::Browse)
			{
				if (!p.settings.mangaOn) { if (extManageRect().hit(x, y)) extPressed = EXT_MANAGE; return false; }
				for (int i = 0; i < EXT_DRAWER_TILES; i++)
					if (extTileRect(i).hit(x, y)) { extCursor = i; extPressed = EXT_TILE; }
				return false;
			}
			if (extRowRect().hit(x, y)) extPressed = EXT_ROW;
			if (extChipRect().hit(x, y)) extPressed = EXT_CHIP;
			return false;
		}
		if (pt.kind != Pointer::Up || extPressed < 0) return false;
		int id = extPressed;
		extPressed = -1;
		Rect r = id == EXT_TILE ? extTileRect(extCursor) : id == EXT_MANAGE ? extManageRect() : id == EXT_ROW ? extRowRect()
			: id == EXT_CHIP ? extChipRect() : id == EXT_GEAR ? extGearRect() : extSheetBtn(id - EXT_OPEN);
		// a finger that slid away changed its mind
		return r.hit(x, y) && extAct(id, p);
	}

	void Ui::openExtSettings()
	{
		// the manga reader's own page (not Banana Player's Settings): B comes back here, as it was
		openMangaSettings();
	}

	bool Ui::extAct(int id, Player& p)
	{
		if (id == EXT_OPEN || id == EXT_DONE) { extSheetChoose(id - EXT_OPEN, p); return false; }
		if (id == EXT_TILE) openManga(p);
		else if (id == EXT_MANAGE) openExtensions();
		else if (id == EXT_ROW || id == EXT_CHIP) extToggleManga(p);
		else if (id == EXT_GEAR) openExtSettings();
		return false;
	}

	void Ui::drawExtensionsDrawer(const Player& p)
	{
		// the section header (its markers, "<") is drawn before this: the drawer is the section's top
		if (!p.settings.mangaOn)
		{
			// none on: what extensions are, and the way to turn one on
			drawGlyphMid(G_BLOCKS, W / 2, 160, DIM, 6);
			drawTextMid("No extensions are on", 1, TEXT, W / 2, 206, 560, 2);
			drawTextMid("Extensions add to Banana Player, like a manga reader.", 0, DIM, W / 2, 244, 560, 2);
			drawTextMid("Nothing of one shows or runs until you turn it on.", 0, DIM, W / 2, 264, 560, 2);
			Rect b = extManageRect();
			drawBox(b, extPressed == EXT_MANAGE ? PRESSED : BUTTON, R_BUTTON, true);
			drawTextMid("Manage extensions", 1, ACCENT, b.x + b.w / 2, b.y + b.h / 2, b.w - 16, 2);
			drawHints({{"START", "Manage extensions"}, {"B", "Home"}, {"L/R", "Sections"}});
			return;
		}
		// what's on: one tile each, its book, its name and what it does
		drawTextMid(std::to_string(EXT_DRAWER_TILES) + " on", 0, DIM, 16, 64);
		for (int i = 0; i < EXT_DRAWER_TILES; i++)
		{
			Rect t = extTileRect(i);
			bool at = extCursor == i;
			drawBox(t, extPressed == EXT_TILE ? PRESSED : at ? BUTTON : SURFACE, R_TILE, at);
			// (the name at the small size, as the mock has it: the large one doesn't fit a tile)
			drawMangaIcon({t.x + t.w / 2 - 38, 102, 76, 76});
			drawTextMid("Manga reader", 0, at ? ACCENT : TEXT, t.x + t.w / 2, 196, t.w - 16, 2);
			drawTextMid("Comics and manga", 0, at ? DIM_ON : DIM, t.x + t.w / 2, 218, t.w - 16, 2);
		}
		drawTextMid("Turn more on in Manage extensions (START).", 0, DIM, W / 2, 270, 560, 2);
		drawHints({{"A", "Open"}, {"START", "Manage extensions"}, {"B", "Home"}, {"L/R", "Sections"}});
	}

	void Ui::drawExtensions(const Player& p)
	{
		drawHeader("Manage extensions", p);
		drawTextMid(std::to_string(EXT_DRAWER_TILES) + " extension", 0, DIM, 16, 64);
		// the extension's row: its mark, its name and what it does, its switch, its settings
		Rect row = extRowRect();
		drawFocus(row);
		drawMangaIcon({30, 100, 46, 46});
		drawTextMid("Manga reader", 1, ACCENT, 92, 110, 360);
		drawTextMid("Comics and manga from files and servers", 0, DIM, 92, 136, 360);
		bool on = p.settings.mangaOn;
		Rect chip = extChipRect();
		fillRound(chip, on ? ACCENT : BUTTON, R_CHIP);
		drawTextMid(on ? "On" : "Off", 0, on ? ON_ACCENT : TEXT, chip.x + chip.w / 2, chip.y + chip.h / 2, chip.w - 8, 2);
		// (on and off only: the extension's settings are its own, START inside it)
		drawTextMid("More extensions come with later versions.", 0, DIM, W / 2, 190, 560, 2);
		if (extSheet)
		{
			drawExtSheet(p);
			return;
		}
		drawHints({{"A", "On / Off"}, {"B", "Back"}});
	}

	void Ui::drawExtSheet(const Player& p)
	{
		(void)p;
		// over the page, dimmed: the note about repositories, once, the first time the reader is turned on
		fill({0, 0, W, H}, {0, 0, 0, 170});
		drawBox(extSheetBox(), SURFACE, R_DIALOG);
		drawMangaIcon({88, 108, 40, 40});
		drawTextMid("Manga reader is on", 1, TEXT, 136, 128, 420);
		drawTextMid("It reads from your files and servers, and from", 0, TEXT, 92, 174, 456);
		drawTextMid("sources in repositories you add.", 0, TEXT, 92, 196, 456);
		drawTextMid("Repositories are added by you. Banana Player", 0, DIM, 92, 232, 456);
		drawTextMid("doesn\xE2\x80\x99t provide or check their content.", 0, DIM, 92, 254, 456);
		for (int i = 0; i < 2; i++)
		{
			Rect b = extSheetBtn(i);
			bool at = extSheetCursor == i;
			drawBox(b, extPressed == EXT_OPEN + i ? PRESSED : at ? ACCENT : BUTTON, R_BUTTON, at);
			drawTextMid(i ? "Done" : "Open Manga", 1, at ? ON_ACCENT : TEXT, b.x + b.w / 2, b.y + b.h / 2, b.w - 16, 2);
		}
		drawHints({{"A", "Choose"}, {"LEFT/RIGHT", "Move"}, {"B", "Done"}});
	}

	void Ui::drawMangaIcon(Rect r)
	{
		if (!mangaIconTex)
		{
			mangaIconTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, MANGA_ICON_SIZE, MANGA_ICON_SIZE);
			if (!mangaIconTex) return;
			SDL_UpdateTexture(mangaIconTex, nullptr, MANGA_ICON_RGBA, MANGA_ICON_SIZE * 4);
			SDL_SetTextureBlendMode(mangaIconTex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
			SDL_SetTextureScaleMode(mangaIconTex, SDL_ScaleModeNearest);	// pixel art: no blur
#endif
		}
		// a whole number of screen pixels per icon pixel (no uneven pixels, as a game not at integer scale would
		// show), the largest that fits, centred in the place given
		SDL_Rect d = devRect(r);
		int k = std::max(1, std::min(d.w, d.h) / MANGA_ICON_SIZE), s = k * MANGA_ICON_SIZE;
		SDL_Rect c{d.x + (d.w - s) / 2, d.y + (d.h - s) / 2, s, s};
		SDL_RenderCopy(ren, mangaIconTex, nullptr, &c);
	}
}
