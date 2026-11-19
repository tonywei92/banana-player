// kit-new's bottom-panel components (ui_kit.h's kit-new block: Section rail, Book bar, Away-from-normal, Screen-off
// warning), Ui::topPane (which Preview pane the top panel shows: previewpane.h), and the test gallery that draws them
// all (--press gallery:new). Their places are kitlayout.h's kit-new block (tested in core_test.cpp: testKitNew).
#include "ui_kit.h"
#include "previewpane.h"
#include "ui_settings.h"
#include <cmath>

namespace bp
{
	// ---- Section rail ----

	void Ui::KitNew::rail(Kit& k, const lay::Rail& r, const std::vector<RailItem>& sections, const RailItem& settings, int active,
		int pressed, bool searchLit, bool searchPressed, bool npPlaying, bool npPaused, const std::string& npArt)
	{
		Ui& ui = k.ui;
		bool clean = theme::drawingClean(), strong = theme::current.strongKeys;
		// Classic: RAIL ground and a rule; Clean: none, except a strong theme's rail (Gundam's navy frame), whose ink
		// (ON_RAIL) is light and would vanish on the page's ground
		bool ground = !clean || strong;
		if (ground)
		{
			ui.fill(r.area, RAIL);
			if (!clean) ui.fill({r.area.x + r.area.w - 1, r.area.y, 1, r.area.h}, RULE);
		}
		// the active cell: a strong theme (Gundam) fills it KEY_FOCUS with TEXT ink; otherwise Classic's solid ACCENT,
		// or Clean's accent tint
		SDL_Color activeFill = strong ? sdl(theme::KEY_FOCUS) : clean ? sdl(theme::tintOf(theme::BG)) : ACCENT;
		SDL_Color activeInk = strong ? TEXT : clean ? ACCENT : ON_ACCENT;
		auto lit = [&](Ui::Rect c) {
			if (strong) ui.fillRound(c, activeFill, theme::kitRadius(R_BUTTON));
			else if (clean) ui.fillRound(c, activeFill, theme::kitRadius(R_BUTTON));
			else
			{
				// lit as a key: ACCENT with its darker lip along the foot, no edge
				ui.fillRound(c, sdl(theme::lipOf(theme::ACCENT)), R_BUTTON);
				ui.fillRound({c.x, c.y, c.w, c.h - theme::LIP}, ACCENT, R_BUTTON);
			}
		};
		auto cell = [&](Ui::Rect c, const RailItem& it, bool on, bool held) {
			SDL_Color ink = on ? activeInk : sdl(theme::ON_RAIL);
			if (on) lit(c);
			else if (held) ui.fillRound(c, PRESSED, theme::kitRadius(R_BUTTON));
			// icon 22 over the label, the pair centred in the cell (a seven-section rail's 50-tall cells sit closer)
			int cx = c.x + c.w / 2, iconCy = c.h >= lay::RAIL_CELL_H ? c.y + 21 : c.y + 16;
			int labelY = c.h >= lay::RAIL_CELL_H ? c.y + 42 : c.y + 35;
			if (it.glyph) ui.drawGlyphMid(*it.glyph, cx, iconCy, ink, 2);
			ui.drawTextMid(it.label, F_I11B, ink, cx, labelY, c.w - 4, 2);
			if (it.warn)
			{
				// red, 11 units, ringed in the rail's ground so it stands off the cell
				Ui::Rect m{c.x + c.w - 9 - 11, c.y + 5, 11, 11};
				ui.fillRound({m.x - 2, m.y - 2, m.w + 4, m.h + 4}, on ? activeFill : ground ? RAIL : BG, 5);
				ui.fillRound(m, DANGER, 3);
			}
		};
		// Search, pinned at the top: the magnifier alone; the accent while Search shows (Clean: a glyph, no ground)
		if (searchLit) lit(r.search);
		else if (clean)
		{
			if (searchPressed) ui.fillRound(r.search, PRESSED, theme::kitRadius(R_BUTTON));
		}
		else ui.drawBox(r.search, searchPressed ? PRESSED : BUTTON, R_BUTTON);
		ui.drawGlyphMid(G_SEARCH, r.search.x + r.search.w / 2, r.search.y + r.search.h / 2 + 1,
			searchLit ? activeInk : clean ? sdl(theme::ON_RAIL) : TEXT, 2);
		// the sections: only the part inside their view (a drag scrolls them under it)
		SDL_Rect clip = ui.devRect(r.view), previous;
		bool hadClip = SDL_RenderIsClipEnabled(ui.ren) == SDL_TRUE;
		if (hadClip) SDL_RenderGetClipRect(ui.ren, &previous);
		SDL_RenderSetClipRect(ui.ren, &clip);
		size_t n = std::min(sections.size(), r.cell.size());
		for (size_t i = 0; i < n; i++) cell(r.cell[i], sections[i], (int)i == active, (int)i == pressed);
		SDL_RenderSetClipRect(ui.ren, hadClip ? &previous : nullptr);
		// Settings pinned at the foot
		cell(r.settings, settings, active == (int)r.cell.size(), pressed == (int)r.cell.size());
		// Now playing, pinned above Settings: its slot is kept while nothing plays (nothing drawn then). While
		// something is loaded: the art at 30, then three bars in ACCENT (a pause mark when paused)
		if (npArt.empty()) return;
		if (pressed == NP_PRESSED) ui.fillRound(r.np, PRESSED, R_BUTTON);
		Rect art{r.np.x + 8, r.np.y + (r.np.h - 30) / 2, 30, 30};
		k.artPlaceholder(art, npArt, 5, true);
		int cx = art.x + art.w + (r.np.x + r.np.w - 6 - (art.x + art.w)) / 2;
		if (npPaused)
		{
			// the pause mark, as the player's: two bars in DIM_ON
			ui.fillRound({cx - 7, r.np.y + (r.np.h - 14) / 2, 5, 14}, DIM_ON, 1);
			ui.fillRound({cx + 2, r.np.y + (r.np.h - 14) / 2, 5, 14}, DIM_ON, 1);
		}
		else
		{
			// three bars, moving while it plays (a frame about 6 times a second, as the header's mark)
			double t = (SDL_GetTicks() / 166) * 166 / 160.0;
			for (int i = 0; i < 3; i++)
			{
				int h = npPlaying ? 5 + (int)std::lround(std::fabs(std::sin(t + i * 1.7)) * 11) : 5;
				ui.fillRound({cx - 6 + i * 6, r.np.y + r.np.h / 2 + 7 - h, 4, h}, ACCENT, 1);
			}
		}
	}

	// ---- Away-from-normal ----

	SDL_Color Ui::KitNew::awayInk(lay::AwayTone t)
	{
		return t == lay::AwayTone::Danger ? DANGER : t == lay::AwayTone::Away ? ACCENT : TEXT;
	}

	void Ui::KitNew::awayDot(Kit& k, Rect control, lay::AwayTone t)
	{
		if (t == lay::AwayTone::Normal) return;
		k.ui.fillRound(lay::awayDot(control), awayInk(t), lay::AWAY_DOT / 2);
	}

	// ---- Book bar ----

	bool Ui::KitNew::bookBar(Kit& k, const lay::BookBar& b, const BookSpec& s)
	{
		Ui& ui = k.ui;
		if (s.veil) ui.fill({0, 0, Ui::W, Ui::H}, sdl(theme::BG, 128));
		// drawn upright into a texture of its own, then turned a quarter onto the panel (SDL_RenderCopyEx); a
		// renderer with no target textures gets it unturned at the panel's foot
		const int tw = (int)std::lround(b.box.w * sx), th = (int)std::lround(b.box.h * sy);
		SDL_Texture* prev = SDL_GetRenderTarget(ui.ren);
		SDL_Texture* tex = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, tw, th);
		bool turned = tex && SDL_SetRenderTarget(ui.ren, tex) == 0;
		static bool logged = false;
		if (!turned && !logged)
		{
			logged = true;
			fprintf(stderr, "kit: no target texture for the Book bar (%s): drawn unturned\n", SDL_GetError());
		}
		int ox = 0, oy = 0;
		if (turned)
		{
			SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawBlendMode(ui.ren, SDL_BLENDMODE_NONE);
			SDL_SetRenderDrawColor(ui.ren, 0, 0, 0, 0);
			SDL_RenderClear(ui.ren);
		}
		else
		{
			ox = (Ui::W - b.box.w) / 2;
			oy = Ui::H - b.box.h;
		}
		auto at = [&](Ui::Rect r) { return Ui::Rect{ox + r.x, oy + r.y, r.w, r.h}; };
		// the sheet: Classic's edge on three sides, or Clean's fill and 1-unit RULE hairline; the grab mark
		Ui::Rect box = at({0, 0, b.box.w, b.box.h});
		if (theme::drawingClean())
		{
			ui.fillRound(box, SURFACE, theme::R_SHEET, Ui::ROUND_TL | Ui::ROUND_TR);
			ui.fill({box.x + theme::R_SHEET, box.y, box.w - 2 * theme::R_SHEET, 1}, RULE);
		}
		else
		{
			ui.fillRound(box, k.edgeOf(SURFACE), theme::R_SHEET, Ui::ROUND_TL | Ui::ROUND_TR);
			ui.fillRound({box.x + theme::EDGE, box.y + theme::EDGE, box.w - 2 * theme::EDGE, box.h - theme::EDGE}, SURFACE,
				theme::R_SHEET - theme::EDGE, Ui::ROUND_TL | Ui::ROUND_TR);
		}
		ui.fillRound(at(b.grab), PRESSED, 2);
		Ui::Rect t = at(b.title);
		ui.drawTextMid(s.title, F_SMALL, ACCENT, t.x, t.y + t.h / 2, t.w);
		// the slider row: where, the bar, how far
		Ui::Rect v = at(b.value), n = at(b.note);
		ui.drawTextMid(s.value, F_P14, TEXT, v.x, v.y + v.h / 2, v.w);
		k.progress(at(b.slider), s.share, {}, s.pressed == 5);
		ui.drawTextMid(s.note, F_I12, DIM, n.x + n.w, n.y + n.h / 2, n.w, 1);
		for (int i = 0; i < 4; i++)
		{
			// the Book bar's keys are keys, in Clean too (components.md "Clean style", rule 7)
			Kit::ClassicHere classic;
			const BookKey& bk = s.keys[i];
			Ui::Rect r = at(b.keys[i]);
			bool lit = !bk.lit.empty(), held = s.pressed == i;
			ui.drawBox(r, held ? PRESSED : BUTTON, R_BUTTON, lit ? ACCENT : k.edgeOf(held ? PRESSED : BUTTON));
			// the name (its icon before it) on the first line; the key's cap under it, or a lit key's state
			int cx = r.x + r.w / 2, cy1 = r.y + r.h / 2 - 10, cy2 = r.y + r.h / 2 + 12;
			int lw = k.textW(bk.label, F_P16) + (bk.glyph ? 18 + lay::SP8 : 0), x = cx - lw / 2;
			SDL_Color ink = lit ? ACCENT : TEXT;
			if (bk.glyph) { ui.drawGlyphMid(*bk.glyph, x + 9, cy1, ink, 2); x += 18 + lay::SP8; }
			ui.drawTextMid(bk.label, F_P16, ink, x, cy1, r.x + r.w - 8 - x);
			if (lit)
			{
				ui.drawTextMid(bk.lit, F_I11B, ACCENT, cx, cy2, r.w - 16, 2);
				awayDot(k, r, lay::AwayTone::Away);
			}
			else if (!bk.key.empty()) k.caps(bk.key, cx - k.capsW(bk.key) / 2, cy2, ACCENT, held ? PRESSED : BUTTON);
		}
		// the key that leaves Book mode: its name and its cap, centred
		{
			Kit::ClassicHere classic;	// the leaving key too
			Ui::Rect r = at(b.leave);
			bool held = s.pressed == 4;
			ui.drawBox(r, held ? PRESSED : BUTTON, R_BUTTON);
			int cw = s.leaveKey.empty() ? 0 : k.capsW(s.leaveKey) + 8, lw = k.textW(s.leave, F_P16);
			int x = r.x + (r.w - lw - cw) / 2, cy = r.y + r.h / 2 - 1;
			ui.drawTextMid(s.leave, F_P16, TEXT, x, cy, r.w - 16);
			if (cw) k.caps(s.leaveKey, x + lw + 8, cy, ACCENT, held ? PRESSED : BUTTON);
		}
		// its own hint bar (the panel's is along the other edge as held): a rule, then caps and words
		{
			Ui::Rect h = at(b.hints);
			if (!theme::drawingClean()) ui.fill({h.x + theme::EDGE, h.y, h.w - 2 * theme::EDGE, 1}, RULE);
			std::vector<int> widths;
			for (auto& hn : s.hints)
				widths.push_back(k.capsW(hn.key) + (hn.key.empty() || hn.verb.empty() ? 0 : 5) + k.textW(hn.verb, F_I12));
			auto xs = lay::spread(widths, lay::SP16, h.x + lay::SP8, h.x + h.w - lay::SP8);
			int cy = h.y + h.h / 2 + 1;
			for (size_t i = 0; i < s.hints.size() && i < xs.size(); i++)
			{
				int x = xs[i];
				if (!s.hints[i].key.empty()) x += k.caps(s.hints[i].key, x, cy, ACCENT, SURFACE) + 5;
				ui.drawTextMid(s.hints[i].verb, F_I12, DIM_ON, x, cy, std::max(1, h.x + h.w - 8 - x));
			}
		}
		if (!turned)
		{
			if (tex) SDL_DestroyTexture(tex);
			return false;
		}
		SDL_SetRenderTarget(ui.ren, prev);
		lay::Box pb = lay::sideways(b.box, s.topLeft);
		// turned about its middle: the texture's own size, centred on the panel box's middle
		double pcx = (pb.x + pb.w / 2.0) * sx, pcy = (pb.y + pb.h / 2.0) * sy;
		SDL_Rect dst{(int)std::lround(pcx - tw / 2.0), (int)std::lround(pcy - th / 2.0), tw, th};
		SDL_RenderCopyEx(ui.ren, tex, nullptr, &dst, s.topLeft ? 90.0 : 270.0, nullptr, SDL_FLIP_NONE);
		SDL_DestroyTexture(tex);
		return true;
	}

	// ---- Screen-off warning ----

	void Ui::KitNew::screenOffWarn(Kit& k, int msLeft, Rect over)
	{
		Ui& ui = k.ui;
		ui.fill(over, {0, 0, 0, (Uint8)(255 * lay::WARN_VEIL / 100)});
		lay::ScreenOffWarn w = lay::screenOffWarn(over);
		k.flat(w.row, SURFACE, R_BUTTON, ACCENT);
		ui.drawGlyphMid(G_MOON, w.icon.x + w.icon.w / 2, w.icon.y + w.icon.h / 2, ACCENT, 2);
		std::string words = lay::screenOffWords(msLeft);
		int cy = w.row.y + w.row.h / 2;
		ui.drawTextMid(words, F_P14, ACCENT, w.text.x, cy, w.text.w);
		// the way out on the right, when it fits beside the count
		const std::string why = "touch, or any key, keeps it on";
		int room = w.text.w - k.textW(words, F_P14) - 16;
		if (k.textW(why, F_I12) <= room) ui.drawTextMid(why, F_I12, DIM_ON, w.text.x + w.text.w, cy, room, 1);
	}

	// ---- which Preview pane the top panel shows ----

	bool Ui::topPane(PaneSpec& out) const
	{
		if (galleryNew)
		{
			// the gallery's: the text layout, then (gallery:new:2) the cover layout
			out = PaneSpec{};
			out.key = "gallery:" + std::to_string(galleryNew);
			if (galleryNew == 1)
			{
				out.label = "Connection";
				out.title = "Media on NAS";
				out.text = "A shared folder on your Wi-Fi. Opened from Network, it lists like any folder on the card.";
				out.rows = {{"Kind", "SMB"}, {"Address", "nas.local / media"}, {"Last opened", "Today"}};
				out.keys = {{"A", "Open"}, {"Y", "Edit"}, {"X", "More"}};
			}
			else
			{
				out.cover = true;
				out.label = "Book";
				out.title = "Alice's Adventures in Wonderland";
				out.text = "Lewis Carroll. Twelve chapters, read to chapter one.";
				out.rows = {{"Progress", "7%"}, {"Opened", "Today"}};
				out.keys = {{"A", "Read"}, {"X", "More"}};
			}
			return true;
		}
		if (page == Page::Settings && settingsPage) return settingsPage->topPane(out);
		return false;
	}

	// ---- the test gallery (--press gallery:new) ----

	void Ui::drawGalleryNew()
	{
		Kit& k = *kit;
		// the rail at the left: Books open, Manga marked (it would stop what plays)
		lay::Rail r = lay::rail(6);
		std::vector<KitNew::RailItem> secs = {{&G_HOME, "Home"}, {&G_FOLDER, "Files"}, {&G_PLAYLIST, "Playlists"}, {&G_BOOK, "Books"},
			{&G_COMIC, "Manga", true}, {&G_NETWORK, "Network"}};
		KitNew::rail(k, r, secs, {&G_GEAR, "Settings"}, 3);
		// away from normal: a chip at its normal and away, a key that carries its state, the pills
		const int x0 = 92, col = 100;
		drawTextMid("AWAY FROM NORMAL", F_I11B, DIM, x0, 22, 210);
		lay::Away sp0 = lay::away("Speed", "1\xC3\x97", "1\xC3\x97"), sp1 = lay::away("Speed", "1.5\xC3\x97", "1\xC3\x97");
		Kit::Chip c0, c1;
		c0.glyph = c1.glyph = &G_SPEED;
		c0.label = c1.label = "Speed";
		c0.value = "1\xC3\x97";
		c1.value = "1.5\xC3\x97";
		c0.away = sp0.tone != lay::AwayTone::Normal;
		c1.away = sp1.tone != lay::AwayTone::Normal;
		k.chip({x0, 38, col, 52}, c0);
		k.chip({x0 + col + 8, 38, col, 52}, c1);
		lay::Away l0 = lay::away("Light", "Off", "Off"), l1 = lay::away("Light", "Warm", "Off");
		Kit::Key k0, k1;
		k0.glyph = k1.glyph = &G_SUN;
		k0.label = l0.words;
		k1.label = l1.words;
		k1.lit = true;
		Rect kr0{x0, 100, col, 56}, kr1{x0 + col + 8, 100, col, 56};
		k.key(kr0, k0);
		k.key(kr1, k1);
		KitNew::awayDot(k, kr1, l1.tone);
		lay::Away vol = lay::awayLevel("Volume", 140, 100, 120);
		int px = k.pill(x0 + 60, 182, "1.5\xC3\x97", Kit::Pill::Live);
		(void)px;
		k.pill(x0 + 130, 182, vol.words, vol.tone == lay::AwayTone::Danger ? Kit::Pill::Bad : Kit::Pill::Live);
		drawTextMid("SECTION RAIL \xC2\xB7 BOOK BAR", F_I11B, DIM, x0, 214, 210);
		drawTextMid("L / R step, round; tap opens", F_I12, DIM_ON, x0, 236, 210);
		drawTextMid("Held sideways, top screen right", F_I12, DIM_ON, x0, 256, 210);
		// the Book bar, top screen on the right: the sheet along the panel's right edge
		KitNew::BookSpec bs;
		bs.title = "Alice\xE2\x80\x99s Adventures in Wonderland";
		bs.value = "7%";
		bs.note = "Chapter 1 of 12";
		bs.share = 0.07;
		bs.keys[0] = {&G_CHAPTERS, "Contents", "Y", ""};
		bs.keys[1] = {&G_PAGE, "Text", "SELECT", ""};
		bs.keys[2] = {&G_SUN, "Light", "", "Warm \xC2\xB7 \xE2\x88\x92" "30%"};
		bs.keys[3] = {&G_SEARCH, "Find", "L3", ""};
		bs.leave = "Back to scrolling";
		bs.leaveKey = "B";
		bs.hints = {{"LEFT/RIGHT", "Turn"}, {"A", "Next"}, {"START", "Close"}};
		bs.topLeft = false;
		bs.veil = false;
		KitNew::bookBar(k, lay::bookBar(), bs);
		// gallery:new:2: the screen-off warning over all of it
		if (galleryNew == 2) KitNew::screenOffWarn(k, 4000);
	}
}
