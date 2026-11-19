// The kit's second set (declared in ui_kit.h's "KIT VARIANTS" block; layouts in kitlayout.h; docs/notes/
// 2026-11-18-kit-variants.md): status cluster and badges, mini bar, filter chips, form row, alert / confirm / busy
// sheets, keyboard, volume bar, tiles, Recent and Continue cards, empty state, notice row, select and clip bars.
// The list and page pieces of 2026-11-19 are here too: the state pills Home and the mini bar share, the Search
// field and result row, a Music/Videos row, a Books cover tile and a page's line (docs/notes/2026-11-19-kit-inventory.md).
// As the rest of the kit: a Rect from kitlayout and a small struct in, pixels out, nothing kept; tokens only.
#include "ui_kit.h"
#include "artph.h"
#include <cmath>

namespace bp
{
	namespace
	{
		theme::Rgb rgbOf(SDL_Color c) { return {c.r, c.g, c.b}; }
		const char* const BOLT[7] = {"...##", "..##.", ".##..", "#####", "..##.", ".##..", "##..."};
		// the first letter of a name, for an artist's disc (a whole UTF-8 character, capitalised when ASCII)
		std::string firstLetter(const std::string& s)
		{
			if (s.empty()) return "";
			unsigned char c = (unsigned char)s[0];
			size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
			std::string out = s.substr(0, std::min(n, s.size()));
			if (n == 1 && out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
			return out;
		}
	}

	// ---- status cluster and badges ----

	int Ui::Kit::badge(int right, int cy, const Badge& b)
	{
		SDL_Color ink = b.danger ? DANGER : ACCENT;
		std::string t = lay::caps(b.text);
		int tw = textW(t, F_I11B), w = lay::badgeW(tw, b.glyph != nullptr);
		Rect r{right - w, cy - lay::BADGE_H / 2, w, lay::BADGE_H};
		// the badge's ink at 17% over the ground
		ui.fillRound(r, sdl(theme::mix(theme::BG, rgbOf(ink), 17)), theme::R_CAP);
		int x = r.x + lay::BADGE_PAD;
		if (b.glyph)
		{
			ui.drawGlyphMid(*b.glyph, x + 8, cy, ink, 1);
			x += 16 + 4;
		}
		if (tw) ui.drawTextMid(t, F_I11B, ink, x, cy, tw + 1);
		return w;
	}

	int Ui::Kit::status(int right, int cy, const Status& s)
	{
		// the items' widths, rightmost first: clock, battery (and its bolt), Wi-Fi, playing bars, badges
		enum Kind { CLOCK, BATTERY, BOLTK, WIFI, BARS, BADGE };
		struct Item { Kind kind; int w; size_t at; };
		std::vector<Item> items;
		if (!s.clock.empty()) items.push_back({CLOCK, textW(s.clock, F_P14), 0});
		if (s.battery >= 0)
		{
			items.push_back({BATTERY, 24, 0});
			if (s.charging) items.push_back({BOLTK, 10, 0});
		}
		if (s.wifi >= 0) items.push_back({WIFI, 16, 0});
		if (s.playing) items.push_back({BARS, 13, 0});
		for (size_t i = s.badges.size(); i-- > 0;)
		{
			const Badge& b = s.badges[i];
			items.push_back({BADGE, lay::badgeW(textW(lay::caps(b.text), F_I11B), b.glyph != nullptr), i});
		}
		std::vector<int> widths;
		for (auto& it : items) widths.push_back(it.w);
		lay::StatusRow row = lay::statusRow(right, widths);
		for (size_t i = 0; i < items.size(); i++)
		{
			int x = row.x[i];
			switch (items[i].kind)
			{
			case CLOCK: ui.drawTextMid(s.clock, F_P14, DIM_ON, x, cy); break;
			case BATTERY:
			{
				// 22 x 12 with a 2-unit outline and a nub; red when low, the fill in the charge's share
				bool low = s.battery <= 15 && !s.charging;
				SDL_Color ink = low ? DANGER : DIM_ON;
				Rect body{x, cy - 6, 22, 12};
				ui.fill(body, ink);
				ui.fill({body.x + 2, body.y + 2, body.w - 4, body.h - 4}, BG);
				int level = (body.w - 8) * std::clamp(s.battery, 0, 100) / 100;
				if (level > 0) ui.fill({body.x + 4, body.y + 4, level, body.h - 8}, s.charging ? ACCENT : ink);
				ui.fill({body.x + body.w, cy - 3, 2, 6}, ink);
				break;
			}
			case BOLTK:
				for (int r = 0; r < 7; r++)
					for (int c = 0; c < 5; c++)
						if (BOLT[r][c] == '#') ui.fill({x + c * 2, cy - 7 + r * 2, 2, 2}, ACCENT);
				break;
			case WIFI:
				for (int i2 = 0; i2 < 4; i2++)
				{
					int h = 4 + i2 * 3;
					ui.fill({x + i2 * 4, cy + 7 - h, 3, h}, i2 < s.wifi ? DIM_ON : TRACK);
				}
				break;
			case BARS:
				for (int i2 = 0; i2 < 3; i2++)
				{
					int h = 5 + ((s.tick * (i2 + 2) + i2 * 3) % 9);
					ui.fill({x + i2 * 5, cy + 7 - h, 3, h}, ACCENT);
				}
				break;
			case BADGE: badge(x + items[i].w, cy, s.badges[items[i].at]); break;
			}
		}
		return row.left;
	}

	// ---- the mini bar ----

	void Ui::Kit::miniBar(const lay::MiniBar& l, const Mini& m)
	{
		Rect in{l.box.x + theme::EDGE, l.box.y + theme::EDGE, l.box.w - 2 * theme::EDGE, l.box.h - 2 * theme::EDGE};
		flat(l.box, m.pressed ? PRESSED : SURFACE, R_BUTTON, ACCENT);
		// the progress line along the foot, inside the edge
		int fw = (int)std::lround(in.w * std::clamp(m.progress, 0.0, 1.0));
		if (fw > 0 && !m.err) ui.fillRoundBand(in, R_BUTTON - theme::EDGE, in.y + in.h - 3, in.y + in.h, ACCENT, in.x + fw);
		// the play mark, a key of its own (56 x 42); held: the darker fill growing across it
		ui.drawBox(l.mark, m.markPressed ? PRESSED : BUTTON, 8);
		if (m.hold >= 0)
		{
			Rect mi{l.mark.x + theme::EDGE, l.mark.y + theme::EDGE, l.mark.w - 2 * theme::EDGE, l.mark.h - 2 * theme::EDGE};
			int x1 = mi.x + (int)std::lround(mi.w * std::min(1.0, m.hold));
			if (x1 > mi.x) ui.fillRoundBand(mi, 6, mi.y, mi.y + mi.h, sdl(theme::BG, (Uint8)(255 * theme::HOLD_FILL / 100)), x1);
		}
		ui.drawGlyphMid(m.paused ? G_PLAY : G_PAUSE, l.mark.x + l.mark.w / 2, l.mark.y + (l.mark.h - theme::LIP) / 2 + 1, m.err ? DIM : ACCENT);
		if (m.kind) ui.drawGlyphMid(*m.kind, l.kindX, l.cy, DIM_ON);
		// the right end, leftwards: the cap, then the pills (or the words they replace)
		int x = l.rightX;
		if (!m.key.empty())
		{
			int w = capsW(m.key);
			caps(m.key, x - w, l.cy, ACCENT, m.pressed ? PRESSED : SURFACE);
			x -= w + 10;
		}
		if (m.pills.empty())
		{
			int w = textW("Now playing", F_I12);
			ui.drawTextMid("Now playing", F_I12, DIM, x, l.cy, w + 1, 1);
			x -= w + 10;
		}
		else
		{
			for (size_t i = m.pills.size(); i-- > 0;)
			{
				int w = pill(x, l.cy, m.pills[i].first, m.pills[i].second);
				if (w) x -= w + 5;
			}
			x -= 5;
		}
		// the title over the time
		int room = std::max(40, x - l.textX);
		if (m.err) ui.drawTextMid(m.title, F_P14, DANGER, l.textX, l.cy - 9, room);
		else ui.drawTextMid(m.title, F_P14, TEXT, l.textX, l.cy - 9, room);	// static: the mini bar takes no Title marquee
		ui.drawTextMid(m.time, F_I12, m.err ? DANGER : DIM, l.textX, l.cy + 9, room);
	}

	// ---- filter chips ----

	std::vector<int> Ui::Kit::filterWidths(const std::vector<FilterItem>& items) const
	{
		std::vector<int> w;
		for (auto& it : items)
			w.push_back(textW(it.label, F_P14) + (it.count >= 0 ? 8 + textW(std::to_string(it.count), F_P13) : 0) + 24);
		return w;
	}

	void Ui::Kit::filters(const lay::FilterChips& c, const std::vector<FilterItem>& items, int on, int focused, int pressed)
	{
		for (size_t i = 0; i < c.chip.size() && i < items.size(); i++)
		{
			Rect r = c.chip[i];
			bool at = (int)i == on, focus = (int)i == focused;
			if (theme::drawingClean())
			{
				// a word off; the chosen one the accent's tint with ACCENT ink; a ring for the cursor
				if (at) ui.fillRound(r, sdl(theme::tintOf(rgbOf(BG))), theme::kitRadius(R_BUTTON));
				else if ((int)i == pressed) ui.fillRound(r, PRESSED, theme::kitRadius(R_BUTTON));
				if (focus) ring(r, R_BUTTON, ACCENT);
				SDL_Color ink = at ? ACCENT : DIM_ON;
				int x0 = r.x + 12, cy0 = r.y + r.h / 2;
				ui.drawTextMid(items[i].label, F_P14, ink, x0, cy0, r.w - 24);
				if (items[i].count >= 0)
					ui.drawTextMid(std::to_string(items[i].count), F_P13, at ? ACCENT : DIM, r.x + r.w - 12, cy0, 0, 1);
				continue;
			}
			if (focus) ui.fillRound({r.x - 1, r.y - 1, r.w + 2, r.h + 2}, ACCENT, 10);
			SDL_Color fill = (int)i == pressed ? PRESSED : at ? ACCENT : TRACK;
			flat(r, fill, 9, focus ? ACCENT : at ? edgeOf(ACCENT) : edgeOf(BUTTON));
			SDL_Color ink = at ? ON_ACCENT : DIM_ON;
			int x = r.x + 12, cy = r.y + r.h / 2;
			ui.drawTextMid(items[i].label, F_P14, ink, x, cy, r.w - 24);
			if (items[i].count >= 0)
				ui.drawTextMid(std::to_string(items[i].count), F_P13, at ? ON_ACCENT : DIM, r.x + r.w - 12, cy, 0, 1);
		}
	}

	// ---- the form row ----

	void Ui::Kit::formRow(const lay::FormRow& l, const FormField& f)
	{
		Rect r = l.row;
		if (f.pressed) ui.fillRound(r, PRESSED, R_BUTTON);
		else if (f.focused) flat(r, BUTTON, R_BUTTON, ACCENT);
		int cy = r.y + r.h / 2;
		ui.drawTextMid(f.label, F_P16, off(f.focused ? ACCENT : DIM, f.off), l.label.x, cy, l.label.w);
		std::string v = f.secret ? std::string(f.value.size(), '*') : f.value;
		if (v.empty()) ui.drawTextMid(f.placeholder, F_P16, off(DIM, f.off), l.value.x, cy, l.value.w);
		else ui.drawTextMid(f.secret ? v : ui.elideLeft(v, F_P16, l.value.w), F_P16, off(TEXT, f.off), l.value.x, cy, l.value.w);
	}

	// ---- the alert, confirm and busy sheets ----

	std::vector<std::string> Ui::Kit::sheetText(const std::string& text) const
	{
		return ui.wrapText(text, F_I14, lay::PANEL_W - 2 * lay::SHEET_PAD - 4, 3);
	}

	void Ui::Kit::alertSheet(const lay::AlertSheet& l, const AlertSpec& a, const std::vector<std::string>& lines)
	{
		sheet(l.sheet, {&G_ALERT, a.title, "", a.danger});
		for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], F_I14, TEXT, l.text.x, l.text.y + 10 + (int)i * 20, l.text.w);
		Key k;
		k.label = a.key;
		k.key = "A";
		k.tone = Tone::Primary;
		k.pressed = a.pressed;
		key(l.key, k);
		hints({{"A", a.key}, {"B", "Close"}});
	}

	void Ui::Kit::confirmSheet(const lay::ConfirmSheet& l, const ConfirmSpec& c, const std::vector<std::string>& lines)
	{
		sheet(l.sheet, {&G_ALERT, c.title, "", c.danger});
		for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], F_I14, TEXT, l.text.x, l.text.y + 10 + (int)i * 20, l.text.w);
		// the safe key on the left: yellow, focused, B; the action on the right, red ink when it destroys
		Key safe;
		safe.label = c.safe;
		safe.key = "B";
		safe.tone = Tone::Primary;
		safe.focused = c.focus == 0;
		safe.pressed = c.pressed == 0;
		key(l.safe, safe);
		Key act;
		act.label = c.action;
		act.key = c.focus == 1 ? "A" : "";
		act.tone = c.danger ? Tone::Danger : Tone::Normal;
		act.focused = c.focus == 1;
		act.pressed = c.pressed == 1;
		key(l.action, act);
		if (l.foot.w > 0)
		{
			int cy = l.foot.y + l.foot.h / 2;
			ui.drawGlyphMid(c.footOn ? G_BOX_ON : G_BOX, l.foot.x + 12, cy, c.footOn ? ACCENT : DIM);
			int right = l.foot.x + l.foot.w;
			if (!c.footKey.empty())
			{
				int w = capsW(c.footKey);
				caps(c.footKey, right - w, cy, ACCENT, SURFACE);
				right -= w + 9;
			}
			ui.drawTextMid(c.foot, F_I12, DIM, l.foot.x + 30, cy, std::max(1, right - (l.foot.x + 30)));
		}
		std::vector<Hint> h = {{"LEFT/RIGHT", "Choose"}, {"A", c.focus == 0 ? c.safe : c.action}, {"B", c.safe}};
		if (!c.footKey.empty()) h.push_back({c.footKey, "Don't ask again"});
		hints(h);
	}

	void Ui::Kit::busySheet(const lay::BusySheet& l, const BusySpec& b)
	{
		sheetBack(l.sheet.box, l.sheet.grab);
		int cy = l.sheet.head.y + l.sheet.head.h / 2, x = l.sheet.head.x;
		if (b.glyph) ui.drawGlyphMid(*b.glyph, x + 11, cy, ACCENT);
		else
		{
			// the spinner (22): eight dots, the head and two behind it ACCENT, the rest TRACK
			int step = b.tick % 8;
			for (int k = 0; k < 8; k++)
			{
				int age = ((step - k) % 8 + 8) % 8;
				double a = k * 3.14159265 / 4;
				int dx = x + 11 + (int)std::lround(8 * std::sin(a)), dy = cy + (int)std::lround(-8 * std::cos(a));
				ui.fillRound({dx - 2, dy - 2, 4, 4}, age < 3 ? ACCENT : TRACK, 2);
			}
		}
		ui.drawTextMid(b.title, F_P20, ACCENT, x + 32, cy, l.sheet.head.w - 32);
		ui.drawTextMid(b.file, F_I14, TEXT, l.file.x, l.file.y + l.file.h / 2, l.file.w);
		if (b.share >= 0) bar(l.bar, b.share);
		ui.drawTextMid(b.left, F_I12, DIM, l.amounts.x, l.amounts.y + l.amounts.h / 2, l.amounts.w / 2);
		ui.drawTextMid(b.right, F_I12, DIM, l.amounts.x + l.amounts.w, l.amounts.y + l.amounts.h / 2, l.amounts.w / 2, 1);
		Key k;
		k.label = b.key;
		k.key = b.cap;
		k.pressed = b.pressed;
		key(l.key, k);
		hints({{b.cap, b.key}});
	}

	// ---- the keyboard ----

	void Ui::Kit::keyboard(const lay::Keyboard& l, const KeyboardView& v)
	{
		// every key of it is a key, in Clean too (components.md "Clean style", rule 7), and its caps are outlined
		ClassicHere classic;
		// the field: SURFACE with an ACCENT outline, the text, the caret a bar of its own (room kept when it is off)
		flat(l.field, SURFACE, theme::R_FIELD, ACCENT);
		std::string shown = v.secret ? std::string(v.value.size(), '*') : v.value;
		size_t caret = std::min(v.caret, shown.size());
		std::string before = shown.substr(0, caret), after = shown.substr(caret);
		int cy = l.field.y + l.field.h / 2, room = l.field.w - 28;
		int aw = textW(after, F_SMALL);
		before = ui.elideLeft(before, F_SMALL, std::max(20, room - aw - 6));
		int bw = textW(before, F_SMALL), x = l.field.x + 14;
		if (!before.empty()) ui.drawTextMid(before, F_SMALL, TEXT, x, cy, bw + 1);
		if (v.caretOn) ui.fill({x + bw + 1, cy - 12, 2, 24}, ACCENT);
		if (!after.empty()) ui.drawTextMid(after, F_SMALL, TEXT, x + bw + 6, cy, std::max(1, room - bw - 6));
		if (!v.note.empty()) ui.drawTextMid(v.note, F_I12, DIM, l.note.x, l.note.y + l.note.h / 2, l.note.w);
		int index = 0;
		auto edgeFor = [&](int i, SDL_Color fill) { return i == v.cursor ? ACCENT : edgeOf(fill); };
		auto fillFor = [&](int i, SDL_Color fill) { return i == v.pressed ? PRESSED : fill; };
		// the field's own pieces, then the letters
		for (size_t i = 0; i < l.special.size() && i < v.special.size(); i++, index++)
		{
			Rect r = l.special[i];
			flat(r, fillFor(index, TRACK), 6, edgeFor(index, TRACK));
			ui.drawTextMid(v.special[i], F_P14, index == v.cursor ? ACCENT : TEXT, r.x + r.w / 2, r.y + r.h / 2, r.w - 8, 2);
		}
		for (size_t row = 0; row < l.rows.size(); row++)
			for (size_t i = 0; i < l.rows[row].size(); i++, index++)
			{
				Rect r = l.rows[row][i];
				std::string label = row < v.rows.size() && i < v.rows[row].size() ? v.rows[row][i] : "";
				ui.drawBox(r, fillFor(index, BUTTON), 6, edgeFor(index, BUTTON));
				ui.drawTextMid(label, F_SMALL, index == v.cursor ? ACCENT : TEXT, r.x + r.w / 2, r.y + (r.h - theme::LIP) / 2 + 1, r.w - 6, 2);
			}
		// the bottom row: the layer keys, Space, backspace (TRACK), Done (primary), each with its button
		size_t layers = v.layers.size();
		for (size_t i = 0; i < l.bottom.size(); i++, index++)
		{
			Rect r = l.bottom[i];
			bool done = i + 1 == l.bottom.size(), layer = i < layers;
			std::string label = layer ? v.layers[i] : i == layers ? "Space" : done ? "Done" : "Del";
			std::string cap = i < v.bottomCaps.size() ? v.bottomCaps[i] : "";
			SDL_Color fill = done ? ACCENT : TRACK;
			if (done) ui.drawBox(r, index == v.pressed ? sdl(theme::lipOf(theme::ACCENT)) : ACCENT, R_BUTTON, index == v.cursor ? ACCENT : edgeOf(ACCENT));
			else ui.drawBox(r, fillFor(index, TRACK), R_BUTTON, edgeFor(index, TRACK));
			SDL_Color ink = done ? ON_ACCENT : (int)i == v.layerOn || index == v.cursor ? ACCENT : TEXT;
			int capW = cap.empty() ? 0 : capsW(cap), by = r.y + (r.h - theme::LIP) / 2 + 1;
			int tw = std::min(r.w - 16 - (capW ? capW + 7 : 0), textW(label, F_P14));
			int w = tw + (capW ? 7 + capW : 0), x0 = r.x + r.w / 2 - w / 2;
			ui.drawTextMid(label, F_P14, ink, x0, by, tw + 1);
			if (capW) caps(cap, x0 + tw + 7, by, done ? ON_ACCENT : ACCENT, index == v.pressed ? PRESSED : fill);
		}
	}

	// ---- the volume bar ----

	SDL_Color Ui::Kit::volumeInk(int value)
	{
		int z = lay::volumeZone(value);
		return z == 2 ? DANGER : z == 1 ? ACCENT : TEXT;
	}

	void Ui::Kit::volumeBar(const lay::VolumeBar& l, int value, int max, bool isOff)
	{
		Rect b = l.bar;
		ui.fillRound(b, off(TRACK, isOff), b.h / 2);
		// each part's colour from the top down: the longest part first, the shorter ones over it
		auto part = [&](int to, SDL_Color c) {
			int x1 = lay::volumeX(b, std::min(value, to), max);
			if (x1 > b.x) ui.fillRound({b.x, b.y, x1 - b.x, b.h}, off(c, isOff), b.h / 2, x1 >= b.x + b.w ? ROUND_ALL : ROUND_TL | ROUND_BL);
		};
		if (value > 120) part(max, DANGER);
		if (value > 100) part(120, ACCENT);
		part(100, TEXT);
		if (l.tickX >= 0) ui.fill({l.tickX - 1, b.y - 3, 2, b.h + 6}, off(DIM_ON, isOff));
		ui.drawTextMid(std::to_string(value) + "%", F_P16, off(volumeInk(value), isOff), l.number.x + l.number.w, l.number.y + l.number.h / 2, l.number.w, 1);
	}

	// ---- tiles ----

	void Ui::Kit::tile(Rect r, const Tile& t)
	{
		if (t.classic)
		{
			// Home's three shortcuts are keys' pieces: the Classic flat piece in Clean too, SURFACE and its edge at
			// radius 10 (components.md "Clean style", rule 7)
			ClassicHere classic;
			SDL_Color fill = t.pressed ? PRESSED : SURFACE;
			flat(r, off(fill, t.off), R_BUTTON, t.focused && !t.off ? ACCENT : off(edgeOf(SURFACE), t.off));
		}
		else
		{
			SDL_Color fill = t.pressed ? PRESSED : cardFill(SURFACE);
			flat(r, off(fill, t.off), theme::R_TILE, t.focused && !t.off ? ACCENT : off(edgeOf(SURFACE), t.off));
		}
		SDL_Color ink = off(t.focused || t.lit ? ACCENT : TEXT, t.off);
		SDL_Color noteInk = off(DIM, t.off);
		if (r.h >= 70)
		{
			// a menu tile: icon over name over the note, centred
			int cx = r.x + r.w / 2;
			int top = r.y + (r.h - (26 + 8 + 18 + (t.note.empty() ? 0 : 16))) / 2;
			if (t.glyph) ui.drawGlyphMid(*t.glyph, cx, top + 13, ink, 3);
			ui.drawTextMid(t.name, F_P16, ink, cx, top + 26 + 8 + 9, r.w - 16, 2);
			if (!t.note.empty()) ui.drawTextMid(t.note, F_I12, noteInk, cx, top + 26 + 8 + 18 + 8, r.w - 12, 2);
			if (!t.mark.empty()) pill(r.x + r.w - 8, r.y + 14, t.mark);
			return;
		}
		// a shortcut or toggle tile, 56 tall: the icon at the left, the name, and its state in words under it
		int cy = r.y + r.h / 2, x = r.x + 14, right = r.x + r.w - 12;
		if (t.glyph)
		{
			ui.drawGlyphMid(*t.glyph, x + 11, cy, ink);
			x += 22 + 10;
		}
		if (!t.mark.empty()) right -= pill(right, cy, t.mark) + 8;
		ui.drawTextMid(t.name, F_P16, ink, x, t.note.empty() ? cy : cy - 9, std::max(1, right - x));
		if (!t.note.empty()) ui.drawTextMid(t.note, F_I12, noteInk, x, cy + 11, std::max(1, right - x));
	}

	void Ui::Kit::choiceTile(Rect r, const Choice& c)
	{
		bool clean = theme::drawingClean();
		// (Clean: the chosen one a strong wash of the accent and a dot in its corner: the 17% tint alone was lost in a
		// grid of fifteen, and in Dark the accent is near the text's colour)
		SDL_Color fill = clean ? (c.on ? sdl(theme::mix(rgbOf(c.pressed ? PRESSED : BUTTON), theme::current.ACCENT, 34)) : c.pressed ? PRESSED : cardFill(SURFACE))
			: c.on ? ACCENT : c.pressed ? PRESSED : SURFACE;
		SDL_Color ink = clean ? (c.on || c.focused ? ACCENT : TEXT) : c.on ? ON_ACCENT : c.focused ? ACCENT : TEXT;
		if (clean)
		{
			ui.fillRound(r, fill, theme::kitRadius(R_BUTTON));
			if (c.on) ui.fillRound({r.x + r.w - 14, r.y + 8, 6, 6}, ACCENT, 3);
			if (c.focused) ring(r, R_BUTTON, ACCENT);
		}
		else
		{
			if (c.on && c.focused)
			{
				ui.fillRound({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, ACCENT, theme::R_BUTTON + 4);
				ui.fillRound({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, SURFACE, theme::R_BUTTON + 2);
			}
			flat(r, fill, theme::R_BUTTON, c.focused && !c.on ? ACCENT : edgeOf(fill));
		}
		int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
		if (c.over)
		{
			// the icon 22 over the name
			if (c.glyph) ui.drawGlyphMid(*c.glyph, cx, cy - 10, ink);
			ui.drawTextMid(c.name, c.font, ink, cx, cy + 14, r.w - 8, 2);
			return;
		}
		// the icon beside the name, the pair centred
		int gw = c.glyph ? ui.glyphW(*c.glyph) : 0, gap = c.glyph ? 8 : 0;
		int tw = std::min(textW(c.name, c.font), r.w - 12 - gw - gap);
		int x = r.x + (r.w - gw - gap - tw) / 2;
		if (c.glyph) ui.drawGlyphMid(*c.glyph, x + gw / 2, cy, ink);
		ui.drawTextMid(c.name, c.font, ink, x + gw + gap, cy, tw);
	}

	// ---- the cards ----

	// the picture's place: the caller's own drawing, or a TRACK square with the kind's mark
	void Ui::Kit::pictureOf(Rect r, const Card& c, int rad)
	{
		if (c.picture) { c.picture(r); return; }
		ui.fillRound(r, off(TRACK, c.off), rad);
		if (c.kind) ui.drawGlyphMid(*c.kind, r.x + r.w / 2, r.y + r.h / 2, off(DIM_ON, c.off), 3);
	}

	void Ui::Kit::recentCard(const lay::RecentCard& l, const Card& c)
	{
		flat(l.box, off(c.pressed ? PRESSED : cardFill(SURFACE), c.off), 12, c.focused && !c.off ? ACCENT : off(edgeOf(SURFACE), c.off));
		pictureOf(l.pic, c, 8);
		// the kind's mark over the picture: a dark chip, light ink in every theme
		if (c.kind && c.picture)
		{
			Rect m{l.pic.x + 4, l.pic.y + l.pic.h - 20, 22, 16};
			ui.fillRound(m, sdl(theme::TOP_CARD, 200), 5);
			ui.drawGlyphMid(*c.kind, m.x + m.w / 2, m.y + m.h / 2, sdl(theme::TOP_TITLE), 1);
		}
		ui.drawTextMid(c.title, F_P14, off(c.focused ? ACCENT : TEXT, c.off), l.title.x, l.title.y + l.title.h / 2, l.title.w);
		ui.drawTextMid(c.note, F_I12, off(DIM, c.off), l.note.x, l.note.y + l.note.h / 2, l.note.w);
		if (c.progress >= 0) bar(l.line, c.progress);
	}

	void Ui::Kit::continueCard(const lay::ContinueCard& l, const Card& c)
	{
		flat(l.box, off(c.pressed ? PRESSED : cardFill(SURFACE), c.off), theme::R_TILE, c.focused && !c.off ? ACCENT : off(edgeOf(SURFACE), c.off));
		pictureOf(l.pic, c, 10);
		ui.drawTextMid(lay::caps(c.label), F_I11B, off(DIM, c.off), l.label.x, l.label.y + l.label.h / 2, l.label.w);
		ui.drawTextMid(c.title, F_NORMAL, off(TEXT, c.off), l.title.x, l.title.y + l.title.h / 2, l.title.w);
		ui.drawTextMid(c.note, F_I12, off(DIM, c.off), l.note.x, l.note.y + l.note.h / 2, l.note.w);
		if (c.progress >= 0) bar(l.bar, c.progress);
		Key k;
		k.glyph = c.keyGlyph;
		k.label = c.key;
		k.tone = Tone::Primary;
		k.pressed = c.keyPressed;
		k.off = c.off;
		key(l.key, k);
	}

	// ---- the empty state ----

	void Ui::Kit::emptyState(const lay::EmptyState& l, const Empty& e)
	{
		if (e.glyph)
		{
			int cell = std::clamp(54 / std::max(1, e.glyph->h), 2, 8);
			ui.drawGlyphMid(*e.glyph, l.icon.x + l.icon.w / 2, l.icon.y + l.icon.h / 2, DIM, cell);
		}
		ui.drawTextMid(e.title, F_NORMAL, TEXT, l.title.x + l.title.w / 2, l.title.y + l.title.h / 2, l.title.w, 2);
		for (size_t i = 0; i < e.lines.size(); i++)
			ui.drawTextMid(e.lines[i], F_I14, DIM, l.lines.x + l.lines.w / 2, l.lines.y + 10 + (int)i * 20, l.lines.w, 2);
		if (l.keys >= 1 && !e.key1.empty())
		{
			Key k;
			k.glyph = e.glyph1;
			k.label = e.key1;
			k.key = e.hotkeys ? "A" : "";
			k.tone = Tone::Primary;
			k.pressed = e.pressed1;
			key(l.key[0], k);
		}
		if (l.keys >= 2 && !e.key2.empty())
		{
			Key k;
			k.glyph = e.glyph2;
			k.label = e.key2;
			k.key = e.hotkeys ? "Y" : "";
			k.pressed = e.pressed2;
			key(l.key[1], k);
		}
	}

	// ---- the notice row ----

	void Ui::Kit::notice(const lay::Notice& l, const NoticeRow& n)
	{
		SDL_Color edge = n.tone == NoticeTone::Ready ? ACCENT : n.tone == NoticeTone::Failed ? DANGER : edgeOf(SURFACE);
		flat(l.row, cardFill(SURFACE), 12, edge);
		SDL_Color ink = n.tone == NoticeTone::Ready ? ACCENT : n.tone == NoticeTone::Failed ? DANGER : DIM_ON;
		if (n.glyph) ui.drawGlyphMid(*n.glyph, l.icon.x + l.icon.w / 2, l.icon.y + l.icon.h / 2, ink);
		ui.drawTextMid(n.text, F_P14, TEXT, l.text.x, l.text.y + l.text.h / 2, l.text.w);
		if (l.hasSecond) ui.drawTextMid(n.second, F_I12, DIM, l.second.x, l.second.y + l.second.h / 2, l.second.w);
		if (l.hasBar && n.share >= 0) bar(l.bar, n.share);
		if (l.hasKey)
		{
			Key k;
			k.label = n.key;
			k.key = n.cap;
			k.pressed = n.pressed;
			key(l.key, k);
		}
	}

	// ---- the select bar and the clip bar ----

	Ui::Kit::Key Ui::Kit::barKey(const BarKey& b)
	{
		Key k;
		k.glyph = b.glyph;
		k.label = b.label;
		k.key = b.key;
		k.tone = b.primary ? Tone::Primary : b.tone;
		k.pressed = b.pressed;
		k.off = b.off;
		return k;
	}

	void Ui::Kit::selectBar(const lay::Box keys[4], const BarKey k[4])
	{
		for (int i = 0; i < 4; i++) key(keys[i], barKey(k[i]));
	}

	void Ui::Kit::clipBar(const lay::ClipBar& l, const Clip& c)
	{
		if (theme::drawingClean()) ui.fillRound(l.box, cardFill(SURFACE), theme::kitRadius(12));
		else flat(l.box, SURFACE, 12, ACCENT);
		int cy = l.box.y + l.box.h / 2;
		ui.drawTextMid(c.what, F_P14, TEXT, l.text.x, cy - 9, l.text.w);
		ui.drawTextMid(c.size, F_I12, DIM, l.text.x, cy + 10, l.text.w);
		key(l.key[0], barKey(c.put));
		key(l.key[1], barKey(c.cancel));
	}

	// ---- the state pills Home and the mini bar share ----
	// (moved here from ui_home.cpp's homeStatePills, so both places say the same thing)

	std::vector<std::string> Ui::Kit::statePills(const Player& p) const
	{
		std::vector<std::string> out;
		const Settings& st = p.settings;
		if (std::fabs(p.speed - 1.0) > 1e-6) out.push_back(speedLabel(p.speed));
		if (p.playingAudio())
		{
			if (st.shuffle) out.push_back("Shuffle");
			if (st.eqOn) out.push_back(st.eqPreset.empty() ? CUSTOM_NAME : st.eqPreset);
			if (st.soundFx != SoundFx::None) out.push_back(FX_SHORT[std::clamp((int)st.soundFx, 0, SOUNDFX_COUNT - 1)]);
		}
		else if (st.loop == LOOP_ONE) out.push_back("Repeat one");
		if (ui.pageIsOut() && !ui.pageName().empty()) out.push_back("Playing in " + ui.pageName());
		return out;
	}

	// ---- the Search field, its result rows, a Music/Videos row, a Books cover tile and a page's line ----

	void Ui::Kit::searchField(const lay::SearchField& l, const SearchFieldView& f)
	{
		if (theme::drawingClean())
		{
			// a text field keeps its edge: 2 units, edgeOf(SURFACE), ACCENT while the keyboard has it
			SDL_Color edge = f.focused ? ACCENT : edgeOf(SURFACE);
			ui.fillRound(l.box, edge, 8);
			ui.fillRound({l.box.x + theme::EDGE, l.box.y + theme::EDGE, l.box.w - 2 * theme::EDGE, l.box.h - 2 * theme::EDGE},
				SURFACE, 8 - theme::EDGE);
		}
		else flat(l.box, SURFACE, 8, f.focused ? ACCENT : edgeOf(SURFACE));
		int cy = l.box.y + l.box.h / 2;
		ui.drawGlyphMid(G_SEARCH, l.icon.x + l.icon.w / 2, cy, DIM, 1);
		int tw = l.text.w;
		if (f.value.empty()) ui.drawTextMid(f.placeholder, F_SMALL, DIM, l.text.x, cy, tw);
		else ui.drawTextMid(f.secret ? std::string(f.value.size(), '*') : f.value, F_SMALL, TEXT, l.text.x, cy, tw);
		if (!f.key.empty()) caps(f.key, l.cap.x + l.cap.w - capsW(f.key), cy, ACCENT, SURFACE);
	}

	void Ui::Kit::resultRow(const lay::ResultRow& l, const Result& r)
	{
		if (r.pressed) ui.fillRound(l.row, PRESSED, R_BUTTON);
		else if (r.focused) flat(l.row, BUTTON, R_BUTTON, ACCENT);
		int cy = l.row.y + l.row.h / 2;
		if (r.glyph) ui.drawGlyphMid(*r.glyph, l.icon.x + l.icon.w / 2, cy, off(r.focused ? ACCENT : DIM_ON, r.off));
		// the right end, leftwards: the note, then a kind pill; the name gets what is left
		int right = l.note.x + l.note.w;
		if (!r.note.empty())
		{
			ui.drawTextMid(r.note, F_I12, off(r.focused ? DIM_ON : DIM, r.off), right, cy, 0, 1);
			right -= textW(r.note, F_I12) + 8;
		}
		if (!r.pill.empty()) right -= pill(right, cy, r.pill, Pill::Fact) + 8;
		int room = std::max(1, right - 8 - l.name.x);
		SDL_Color ink = off(r.focused ? ACCENT : TEXT, r.off);
		// the part that matched in ACCENT (the design: "the part of the name that matched is ACCENT")
		bool marked = r.matchAt >= 0 && r.matchLen > 0 && r.matchAt + r.matchLen <= (int)r.name.size();
		if (marked)
		{
			std::string a = r.name.substr(0, r.matchAt), b = r.name.substr(r.matchAt, r.matchLen), c = r.name.substr(r.matchAt + r.matchLen);
			int aw = textW(a, F_P16), bw = textW(b, F_P16), cw = textW(c, F_P16);
			if (aw + bw + cw <= room)
			{
				int x = l.name.x;
				if (!a.empty()) { ui.drawTextMid(a, F_P16, ink, x, cy, room); x += aw; }
				ui.drawTextMid(b, F_P16, off(ACCENT, r.off), x, cy, std::max(1, room - aw));
				x += bw;
				if (!c.empty()) ui.drawTextMid(c, F_P16, ink, x, cy, std::max(1, room - aw - bw));
				return;
			}
		}
		ui.drawTextMid(r.name, F_P16, ink, l.name.x, cy, room);
	}

	void Ui::Kit::mediaRow(const lay::MediaRow& l, const Media& m)
	{
		if (m.pressed) ui.fillRound(l.row, PRESSED, R_BUTTON);
		else if (m.focused) flat(l.row, BUTTON, R_BUTTON, ACCENT);
		int cy = l.row.y + l.row.h / 2;
		// the art: the caller's picture, the record in its sleeve, or the kind's icon in a TRACK square
		if (m.art) m.art(l.art);
		else if (!m.artKey.empty()) artPlaceholder(l.art, m.artKey, 5, true);
		else
		{
			ui.fillRound(l.art, off(TRACK, m.off), 5);
			if (m.glyph) ui.drawGlyphMid(*m.glyph, l.art.x + l.art.w / 2, l.art.y + l.art.h / 2, off(DIM_ON, m.off));
		}
		// the right end, leftwards: the note, the heart, a kind pill
		int right = l.right;
		if (!m.note.empty())
		{
			ui.drawTextMid(m.note, F_I12, off(m.focused ? DIM_ON : DIM, m.off), right, cy, 0, 1);
			right -= textW(m.note, F_I12) + 8;
		}
		if (m.heart)
		{
			ui.drawGlyphMid(G_HEART_S, right - 7, cy, off(ACCENT, m.off), 1);
			right -= 16 + 4;
		}
		if (!m.pill.empty()) right -= pill(right, cy, m.pill, m.tone) + 8;
		int room = std::max(1, right - 8 - l.name.x);
		ui.drawTextMid(m.name, F_P16, off(m.focused ? ACCENT : TEXT, m.off), l.name.x, cy - 9, room);
		if (!m.second.empty())
		{
			// the second line under the title, in the room the same right end leaves
			ui.drawTextMid(m.second, F_I12, off(DIM, m.off), l.name.x, cy + 9, room);
		}
	}

	void Ui::Kit::coverTile(const lay::CoverTile& l, const Cover& c)
	{
		// focused: a 3-unit ACCENT outline 2 clear of the cover (components.md, "Cover cell")
		if (c.focused)
		{
			ui.fillRound({l.cover.x - 5, l.cover.y - 5, l.cover.w + 10, l.cover.h + 10}, ACCENT, 10);
			ui.fillRound({l.cover.x - 2, l.cover.y - 2, l.cover.w + 4, l.cover.h + 4}, BG, 7);
		}
		if (c.picture) c.picture(l.cover);
		else bookPlaceholder(l.cover, c.key, 5);
		if (!c.badge.empty()) pill(l.cover.x + l.cover.w + 4, l.cover.y + 4, c.badge, Pill::Live);
		if (l.title.h > 0) ui.drawTextMid(c.title, F_P13, off(c.focused ? ACCENT : TEXT, c.off), l.title.x, l.title.y + l.title.h / 2, l.title.w);
		if (l.bar.h > 0 && c.progress >= 0)
		{
			bar(l.bar, c.progress);
			int pct = (int)std::lround(std::clamp(c.progress, 0.0, 1.0) * 100);
			ui.drawTextMid(std::to_string(pct) + "%", F_I12, off(DIM_ON, c.off), l.percent.x + l.percent.w, l.percent.y + l.percent.h / 2, 0, 1);
		}
	}

	void Ui::Kit::sectionLine(const lay::SectionLine& l, const SectionLine& s)
	{
		int cy = l.line.y + l.line.h / 2;
		if (s.caps) ui.drawTextMid(lay::caps(s.text), F_I11B, DIM, l.line.x, cy, l.line.w);
		else ui.drawTextMid(s.text, F_I12, DIM, l.line.x, cy, l.line.w);
		if (l.note.w > 0 && !s.note.empty())
			ui.drawTextMid(s.note, F_I12, DIM_ON, l.note.x + l.note.w, cy, 0, 1);
	}

	// ---- a book's drawn cover, an album cell, an artist's row, a video's and a folder's row ----

	void Ui::Kit::bookPlaceholder(Rect r, const std::string& key, int rad)
	{
		// A book, not a record: the sleeve's ground, a spine down the left, and the title's blocks on the front.
		// The widths come from the name's hash, so five books without covers are five different spines.
		const uint32_t h = artph::hash(key);
		ui.fillRound(r, BUTTON, rad);
		int in = std::max(1, theme::EDGE);
		int spine = std::max(8, r.w / 10);
		// the spine stops short of the rounded corners so the fill keeps the box's shape
		ui.fillRound({r.x + in, r.y + in + 3, spine, r.h - 2 * in - 6}, PRESSED, std::max(2, rad - in));
		int sx = r.x + in;
		// the ACCENT mark on the spine near its foot (the theme's one solid colour, as a label is on a record)
		ui.fillRound({sx + spine / 2 - 1, r.y + r.h - 24, 3, 14}, ACCENT, 1);
		// the front: two or three lines of type, their lengths keyed by the name
		int left = r.x + in + spine + lay::SP12, right = r.x + r.w - lay::SP8;
		int w = std::max(10, right - left);
		int barH = std::max(3, r.h / 30), gap = std::max(3, barH + 2);
		int y = r.y + r.h / 4;
		int b1 = w * (52 + (int)(h % 28)) / 100;
		int b2 = w * (34 + (int)((h >> 8) % 30)) / 100;
		int b3 = w * (44 + (int)((h >> 16) % 24)) / 100;
		ui.fillRound({left, y, b1, barH}, TEXT, 1);
		ui.fillRound({left, y + barH + gap, b2, barH}, DIM, 1);
		ui.fillRound({left, y + 2 * (barH + gap), b3, barH}, DIM, 1);
	}

	void Ui::Kit::albumTile(const lay::AlbumTile& l, const Album& a)
	{
		// focused: the Cover cell's 3-unit ACCENT outline 2 clear of the art
		if (a.focused)
		{
			ui.fillRound({l.cover.x - 5, l.cover.y - 5, l.cover.w + 10, l.cover.h + 10}, ACCENT, 10);
			ui.fillRound({l.cover.x - 2, l.cover.y - 2, l.cover.w + 4, l.cover.h + 4}, BG, 7);
		}
		if (a.art) a.art(l.cover);
		else artPlaceholder(l.cover, a.artKey.empty() ? a.title : a.artKey, 5, false);
		if (a.playing || a.paused)
		{
			ui.fillRound(l.mark, ACCENT, 6);
			ui.drawGlyphMid(a.paused ? G_PAUSE : G_PLAY, l.mark.x + l.mark.w / 2, l.mark.y + l.mark.h / 2, ON_ACCENT, 1);
		}
		if (l.title.h > 0)
			ui.drawTextMid(a.title, F_P13, off(a.focused ? ACCENT : TEXT, a.off), l.title.x, l.title.y + l.title.h / 2, l.title.w);
		if (l.artist.h > 0)
			ui.drawTextMid(a.artist, F_I12, off(a.focused ? DIM_ON : DIM, a.off), l.artist.x, l.artist.y + l.artist.h / 2, l.artist.w);
	}

	void Ui::Kit::artistRow(const lay::ArtistRow& l, const Artist& a)
	{
		if (a.pressed) ui.fillRound(l.row, PRESSED, R_BUTTON);
		else if (a.focused) flat(l.row, BUTTON, R_BUTTON, ACCENT);
		int cy = l.row.y + l.row.h / 2;
		if (a.art) a.art(l.art);
		else
		{
			// the initial's disc: the ACCENT's tint over BUTTON, the letter in ACCENT (a badge's pair)
			ui.fillRound(l.art, off(sdl(theme::mix(rgbOf(BUTTON), rgbOf(ACCENT), 17)), a.off), l.art.h / 2);
			ui.drawTextMid(firstLetter(a.name), F_P16, off(ACCENT, a.off), l.art.x + l.art.w / 2, cy, l.art.w - 4, 2);
		}
		ui.drawGlyphMid(G_RIGHT, l.chevronX, cy, off(a.focused ? ACCENT : DIM, a.off));
		ui.drawTextMid(a.name, F_P16, off(a.focused ? ACCENT : TEXT, a.off), l.name.x, l.name.y + l.name.h / 2, l.name.w);
		ui.drawTextMid(a.second, F_I12, off(a.focused ? DIM_ON : DIM, a.off), l.second.x, l.second.y + l.second.h / 2, l.second.w);
	}

	void Ui::Kit::videoRow(const lay::VideoRow& l, const Video& v)
	{
		if (v.pressed) ui.fillRound(l.row, PRESSED, R_BUTTON);
		else if (v.focused) flat(l.row, BUTTON, R_BUTTON, ACCENT);
		int cy = l.row.y + l.row.h / 2;
		// the thumbnail: the caller's frame; else the kind's mark on TRACK (no thumbnails are made in the list)
		if (v.art) v.art(l.thumb);
		else
		{
			ui.fillRound(l.thumb, off(TRACK, v.off), 5);
			const Glyph* g = v.glyph ? v.glyph : &G_FILM;
			ui.drawGlyphMid(*g, l.thumb.x + l.thumb.w / 2, l.thumb.y + l.thumb.h / 2, off(DIM_ON, v.off));
		}
		// the right end, leftwards: the length, and a tick when the file is finished
		int right = l.right;
		if (!v.note.empty())
		{
			ui.drawTextMid(v.note, F_I12, off(v.focused ? DIM_ON : DIM, v.off), right, l.note.y + l.note.h / 2, 160, 1);
			right -= std::min(160, textW(v.note, F_I12)) + 8;
		}
		if (v.done)
		{
			ui.drawGlyphMid(G_CHECK, right - 7, l.note.y + l.note.h / 2, off(v.focused ? ACCENT : DIM, v.off), 1);
			right -= 14 + 8;
		}
		ui.drawTextMid(v.name, F_P16, off(v.focused ? ACCENT : TEXT, v.off), l.name.x, l.name.y + l.name.h / 2, std::max(1, right - l.name.x));
		ui.drawTextMid(v.second, F_I12, off(v.focused ? DIM_ON : DIM, v.off), l.second.x, l.second.y + l.second.h / 2, l.second.w);
		// the thin resume line at the foot (none when not started)
		if (v.resume >= 0)
		{
			double share = std::clamp(v.resume, 0.0, 1.0);
			ui.fillRound(l.bar, off(TRACK, v.off), 1);
			int w = (int)std::lround(l.bar.w * share);
			if (w > 0) ui.fillRound({l.bar.x, l.bar.y, w, l.bar.h}, off(ACCENT, v.off), 1);
		}
	}

	void Ui::Kit::folderRow(const lay::FolderRow& l, const Folder& f)
	{
		if (f.pressed) ui.fillRound(l.row, PRESSED, R_BUTTON);
		else if (f.focused) flat(l.row, BUTTON, R_BUTTON, ACCENT);
		int cy = l.row.y + l.row.h / 2;
		ui.fillRound(l.tile, off(TRACK, f.off), 10);
		ui.drawGlyphMid(G_FOLDER, l.tile.x + l.tile.w / 2, cy, off(f.focused ? ACCENT : DIM_ON, f.off));
		ui.drawGlyphMid(G_RIGHT, l.chevron.x + l.chevron.w / 2, cy, off(f.focused ? ACCENT : DIM, f.off));
		ui.drawTextMid(f.name, F_P16, off(f.focused ? ACCENT : TEXT, f.off), l.name.x, l.name.y + l.name.h / 2, l.name.w);
		ui.drawTextMid(f.second, F_I12, off(f.focused ? DIM_ON : DIM, f.off), l.second.x, l.second.y + l.second.h / 2, l.second.w);
	}
}

namespace bp
{
	// ---- the gallery: every variant on a page, for a `shot:` (a test-only --press step, `gallery:variants[:PAGE]`) ----

	struct Ui::VariantsGallery : Ui::Overlay
	{
		int page;
		explicit VariantsGallery(Ui& u, int pg) : Overlay(u), page(pg) {}
		const char* name() const override { return "gallery"; }
		bool key(Button b, Player&) override
		{
			if (b == Button::B) close();
			return false;
		}
		bool pointer(const Pointer&, int, int, Player&) override { return false; }
		std::string signature(const Player&) const override { return std::to_string(page); }

		// a few rows of a page behind a sheet, so the veil has something to darken
		void backdrop(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			for (int i = 0; i < 6; i++)
			{
				Kit::Row r;
				r.glyph = i % 2 ? &G_FILM : &G_NOTE;
				r.name = i % 2 ? "Big Buck Bunny.mp4" : "Test tones.flac";
				r.note = "3:42";
				k.row({16, 70 + i * 46, 608, 46}, r);
			}
		}

		void page1(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			// the status cluster and the badges
			Kit::Status st;
			st.playing = true;
			st.wifi = 3;
			st.battery = 64;
			st.clock = "9:41";
			st.badges = {{&G_INCOGNITO, "Incognito", false}, {&G_WEB, "Chrome", false}};
			int left = k.status(624, 22, st);
			ui.drawTextMid("Header status", F_I12, DIM, 16, 22, left - 24);
			Kit::Status low;
			low.battery = 9;
			low.wifi = 0;
			low.clock = "23:05";
			low.badges = {{nullptr, "Read only", true}, {nullptr, "Boost", false}};
			left = k.status(624, 48, low);
			ui.drawTextMid("Low battery, red badge", F_I12, DIM, 16, 48, left - 24);
			Kit::Status chg;
			chg.battery = 40;
			chg.charging = true;
			chg.clock = "7:15";
			left = k.status(624, 74, chg);
			ui.drawTextMid("Charging", F_I12, DIM, 16, 74, left - 24);
			// filter chips: the chosen one, the D-pad's, a count
			std::vector<Kit::FilterItem> items = {{"All", 24}, {"Videos", 9}, {"Songs", 12}, {"Books", 3}};
			lay::FilterChips fc = lay::filterChips({16, 90, 608, lay::CHIP_STRIP}, k.filterWidths(items));
			k.filters(fc, items, 0, 2);
			// form rows
			Kit::FormField f1{"Name", "Home server", "", false, false, false, false};
			Kit::FormField f2{"Address", "", "192.168.1.10", false, true, false, false};
			Kit::FormField f3{"Password", "hunter22", "", true, false, false, false};
			k.formRow(lay::formRow({16, 150, 608, lay::FORM_ROW}), f1);
			k.formRow(lay::formRow({16, 196, 608, lay::FORM_ROW}), f2);
			k.formRow(lay::formRow({16, 242, 608, lay::FORM_ROW}), f3);
			// the volume bar at 80, 110 and 135
			int vals[3] = {80, 110, 135};
			for (int i = 0; i < 3; i++)
			{
				lay::VolumeBar vb = lay::volumeBar({16, 292 + i * 26, 608, 24}, 150);
				k.volumeBar(vb, vals[i], 150);
			}
			// notice rows: ready, working with a bar, failed with a key
			Kit::NoticeRow n1;
			n1.tone = Kit::NoticeTone::Ready;
			n1.glyph = &G_CHECK;
			n1.text = "Sources are ready";
			k.notice(lay::notice({16, 376, 300, 0}, false, false, 0), n1);
			Kit::NoticeRow n2;
			n2.glyph = &G_REFRESH;
			n2.text = "Reading your library";
			n2.second = "12 of 40 series";
			n2.share = 0.3;
			k.notice(lay::notice({324, 376, 300, 0}, true, true, 0), n2);
			k.hints({{"L2/R2", "Filter"}, {"A", "Open"}, {"B", "Back"}});
		}

		void page2(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			// tiles: menu tiles in a row of four, then a shortcut and two toggles (one off with its reason)
			const Glyph* gl[4] = {&G_FOLDER, &G_PLAYLIST, &G_NETWORK, &G_GEAR};
			const char* names[4] = {"Files", "Playlists", "Network", "Settings"};
			const char* notes[4] = {"3 cards", "5 saved", "Not set up", "Yellow"};
			for (int i = 0; i < 4; i++)
			{
				Kit::Tile t;
				t.glyph = gl[i];
				t.name = names[i];
				t.note = notes[i];
				t.focused = i == 1;
				t.mark = i == 2 ? "New" : "";
				k.tile(lay::tileCell({16, 8, 608, 0}, 4, 4, 84, i), t);
			}
			Kit::Tile a;
			a.glyph = &G_MOON;
			a.name = "Screen off";
			a.note = "Lid or sleep: closes all";
			a.lit = true;
			k.tile(lay::tileCell({16, 100, 608, 0}, 3, 3, 56, 0), a);
			Kit::Tile b;
			b.glyph = &G_INCOGNITO;
			b.name = "Incognito";
			b.note = "Off in incognito";
			b.off = true;
			k.tile(lay::tileCell({16, 100, 608, 0}, 3, 3, 56, 1), b);
			Kit::Tile c;
			c.glyph = &G_SUN;
			c.name = "Brightness";
			c.note = "70%";
			k.tile(lay::tileCell({16, 100, 608, 0}, 3, 3, 56, 2), c);
			// the Continue card and four Recent cards
			Kit::Card cc;
			cc.kind = &G_FILM;
			cc.label = "Continue";
			cc.title = "Big Buck Bunny";
			cc.note = "Video \xC2\xB7 4:12 left";
			cc.key = "Resume";
			cc.keyGlyph = &G_PLAY;
			cc.progress = 0.62;
			k.continueCard(lay::continueCard({16, 164, 608, lay::CONTINUE_H}), cc);
			for (int i = 0; i < 4; i++)
			{
				Kit::Card r;
				r.kind = i % 2 ? &G_NOTE : &G_FILM;
				r.title = i % 2 ? "Test tones" : "Sintel";
				r.note = i % 2 ? "Song" : "Video";
				r.progress = i == 3 ? -1 : 0.25 * (i + 1) - 0.1;
				r.focused = i == 1;
				k.recentCard(lay::recentCard({16 + i * (lay::RECENT_W + 8), 278, lay::RECENT_W, lay::RECENT_H}), r);
			}
			// the clip bar over the select bar's place (a held file and, below, select mode's four keys)
			Kit::Clip clip;
			clip.what = "Holding 3 files";
			clip.size = "148 MB";
			clip.put = {nullptr, "Move here", "SELECT", Kit::Tone::Normal, false, false, true};
			clip.cancel = {nullptr, "Cancel", "X", Kit::Tone::Normal, false, false, false};
			lay::ClipBar cb = lay::clipBar();
			k.clipBar(cb, clip);
		}

		void page3(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			Kit::Mini mini;
			mini.kind = &G_FILM;
			mini.title = "Big Buck Bunny";
			mini.time = "1:12 / 9:56";
			mini.progress = 0.18;
			lay::MiniBar mb = lay::miniBar();
			// three states: plain, with pills, held (taking over from a browser)
			auto at = [&](int dy) {
				lay::MiniBar m = mb;
				m.box.y -= dy;
				m.mark.y -= dy;
				m.line.y -= dy;
				m.cy -= dy;
				return m;
			};
			k.miniBar(at(150), mini);
			mini.pills = {{"2x", Kit::Pill::Live}, {"Playing in Chrome", Kit::Pill::Live}, {"Loop", Kit::Pill::Fact}};
			mini.paused = true;
			k.miniBar(at(80), mini);
			mini.pills.clear();
			mini.hold = 0.5;
			mini.markPressed = true;
			k.miniBar(at(10), mini);
			// the select bar
			Kit::BarKey keys[4] = {
				{&G_PLAYLIST, "Playlist", "", Kit::Tone::Normal, false, false, false},
				{&G_FILE, "Copy", "", Kit::Tone::Normal, false, false, false},
				{&G_MOVE, "Move", "", Kit::Tone::Normal, false, false, false},
				{&G_TRASH, "Delete", "", Kit::Tone::Danger, false, false, false},
			};
			lay::Box sb[4];
			lay::selectBar(sb);
			for (auto& b : sb) b.y -= 232;
			k.selectBar(sb, keys);
			Kit::NoticeRow n3;
			n3.tone = Kit::NoticeTone::Failed;
			n3.glyph = &G_ALERT;
			n3.text = "No Wi-Fi";
			n3.second = "Connect to browse";
			n3.key = "Retry";
			n3.cap = "Y";
			k.notice(lay::notice({16, 64, 608, 0}, true, false, 110), n3);
			ui.drawTextMid("Notice row with a key; select bar; mini bar in three states", F_I12, DIM, 16, 40, 600);
		}

		void page4(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			Kit::Empty e;
			e.glyph = &G_BOOK;
			e.title = "No books yet";
			e.lines = {"Put CBZ, EPUB or PDF files on the card,", "then open the folder here."};
			e.key1 = "Open Files";
			e.key2 = "Add";
			k.emptyState(lay::emptyState({0, 54, W, H - 54 - lay::HINTS_H}, 2, 2), e);
			k.hints({{"A", "Open Files"}, {"Y", "Add"}, {"B", "Back"}});
		}

		void page5(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			Kit::KeyboardView v;
			v.value = "http://192.168.1.10";
			v.caret = v.value.size();
			v.note = "Any address, with or without http://";
			v.special = {"https://", "http://", ".json", ".local"};
			v.rows = {{"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"}, {"a", "s", "d", "f", "g", "h", "j", "k", "l", "-"}, {"z", "x", "c", "v", "b", "n", "m", ".", "/", ":"}};
			v.layers = {"abc", "123"};
			v.layerOn = 0;
			v.bottomCaps = {"L3", "", "Y", "X", "START"};
			v.cursor = 13;
			lay::Keyboard l = lay::keyboard({100, 90, 80, 90}, {10, 10, 10}, 2);
			k.keyboard(l, v);
			k.iconKey({16, 8, 56, 46}, G_BACK, false, 3, true);
			ui.drawTextMid("Address", F_P28, TEXT, 84, 31, 400);
			k.hints({{"A", "Type"}, {"B", "Delete"}, {"X", "Space"}, {"Y", "Shift"}, {"START", "Done"}});
		}

		void page6(Kit& k)
		{
			backdrop(k);
			Kit::AlertSpec a;
			a.title = "Couldn't save the playlist";
			a.text = "The card is read-only. Put it back in the handheld, or free some space, and try again.";
			a.danger = true;
			a.key = "OK";
			auto lines = k.sheetText(a.text);
			k.alertSheet(lay::alertSheet((int)lines.size()), a, lines);
		}

		void page7(Kit& k)
		{
			backdrop(k);
			Kit::ConfirmSpec c;
			c.title = "Delete 3 files?";
			c.text = "They are removed from the card. This can't be undone.";
			c.safe = "Keep them";
			c.action = "Delete";
			c.danger = true;
			c.foot = "Don't ask again";
			c.focus = 0;
			auto lines = k.sheetText(c.text);
			k.confirmSheet(lay::confirmSheet((int)lines.size(), true), c, lines);
		}

		void page8(Kit& k)
		{
			backdrop(k);
			Kit::BusySpec b;
			b.title = "Copying";
			b.file = "Big Buck Bunny.mp4";
			b.left = "3 of 12 \xC2\xB7 25%";
			b.right = "About 2 min left";
			b.share = 0.25;
			b.tick = 2;
			k.busySheet(lay::busySheet(), b);
		}

		// Search: the field, a group heading and result rows (a kind icon, the matched part in ACCENT, a note)
		void page9(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			ui.drawTextMid("Search", F_P28, TEXT, 16, 20, 400);
			Kit::SearchFieldView f;
			f.value = "bunn";
			k.searchField(lay::searchField({16, 40, 608, lay::SEARCH_FIELD_H}), f);
			Kit::SectionLine h;
			h.caps = true;
			h.text = "Videos \xC2\xB7 2";
			k.sectionLine(lay::sectionLine({16, 98, 608, lay::LINE_H}, false), h);
			Kit::Result r;
			r.glyph = &G_FILM;
			r.name = "Big Buck Bunny.mkv";
			r.note = "Videos / Movies \xC2\xB7 9:56";
			r.matchAt = 9;
			r.matchLen = 5;
			r.focused = true;
			k.resultRow(lay::resultRow({16, 120, 608, lay::RESULT_ROW_H}), r);
			r = Kit::Result();
			r.glyph = &G_FILM;
			r.name = "Bunny trailer.mp4";
			r.note = "Videos / Clips \xC2\xB7 0:33";
			r.matchAt = 0;
			r.matchLen = 3;
			k.resultRow(lay::resultRow({16, 170, 608, lay::RESULT_ROW_H}), r);
			h.text = "Music \xC2\xB7 3";
			k.sectionLine(lay::sectionLine({16, 224, 608, lay::LINE_H}, false), h);
			r = Kit::Result();
			r.glyph = &G_NOTE;
			r.name = "Bunny Hop";
			r.note = "The Plantains \xC2\xB7 Yellow Album";
			r.matchAt = 0;
			r.matchLen = 5;
			r.pill = "VIDEO";
			k.resultRow(lay::resultRow({16, 246, 608, lay::RESULT_ROW_H}), r);
			r = Kit::Result();
			r.glyph = &G_BLOCKS;
			r.name = "Bunny Songs";
			r.note = "Album \xC2\xB7 The Carrots \xC2\xB7 9 songs";
			r.off = true;
			k.resultRow(lay::resultRow({16, 296, 608, lay::RESULT_ROW_H}), r);
			h.text = "Books \xC2\xB7 1";
			k.sectionLine(lay::sectionLine({16, 350, 608, lay::LINE_H}, false), h);
			r = Kit::Result();
			r.glyph = &G_BOOK;
			r.name = "The Velveteen Bunny.epub";
			r.note = "Books \xC2\xB7 12% read";
			r.matchAt = 14;
			r.matchLen = 5;
			k.resultRow(lay::resultRow({16, 372, 608, lay::RESULT_ROW_H}), r);
			k.hints({{"A", "Open"}, {"X", "More"}, {"Y", "Edit"}, {"B", "Back"}});
		}

		// Music and Videos: the page's line, three list rows (album, artist, video) and the Books shelf
		void page10(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			Kit::SectionLine sl;
			sl.text = "From Music on both cards \xC2\xB7 148 albums \xC2\xB7 1,912 songs";
			k.sectionLine(lay::sectionLine({16, 8, 608, lay::LINE_H}, true), sl);
			Kit::Media m;
			m.artKey = "Yellow Album";
			m.name = "Sunny Peel";
			m.second = "The Plantains \xC2\xB7 Yellow Album";
			m.note = "3:34";
			m.pill = "VIDEO";
			m.tone = Kit::Pill::Fact;
			m.focused = true;
			k.mediaRow(lay::mediaRow({16, 36, 608, lay::MEDIA_ROW_H}), m);
			m = Kit::Media();
			m.glyph = &G_NOTE;
			m.name = "The Plantains";
			m.second = "3 albums \xC2\xB7 34 songs";
			m.note = "2 h 6 min";
			k.mediaRow(lay::mediaRow({16, 100, 608, lay::MEDIA_ROW_H}), m);
			m = Kit::Media();
			m.artKey = "Big Buck Bunny";
			m.name = "Big Buck Bunny";
			m.second = "Videos / Movies \xC2\xB7 9:56";
			m.pill = "VIDEO";
			m.heart = true;
			k.mediaRow(lay::mediaRow({16, 164, 608, lay::MEDIA_ROW_H}), m);
			Kit::SectionLine shelf;
			shelf.caps = true;
			shelf.text = "Reading now \xC2\xB7 3";
			k.sectionLine(lay::sectionLine({16, 238, 608, lay::LINE_H}, false), shelf);
			std::vector<lay::Box> cells = lay::coverCells({16, 264, 608, lay::COVER_CELL_H}, 5, 5, lay::COVER_CELL_H);
			const char* titles[5] = {"Alice's Adventures", "The Time Machine", "Field Guide", "Frankenstein", "Krazy Kat"};
			for (int i = 0; i < 5 && i < (int)cells.size(); i++)
			{
				Kit::Cover c;
				c.key = titles[i];
				c.title = titles[i];
				c.focused = i == 0;
				c.badge = i == 1 ? "3" : "";
				c.progress = i == 0 ? 0.07 : i == 1 ? 0.58 : -1;
				k.coverTile(lay::coverTile(cells[i], true, c.progress >= 0), c);
			}
			k.hints({{"L2/R2", "View"}, {"A", "Open"}, {"X", "More"}, {"L3", "Search"}});
		}

		// Music: the page's line and its view chips, the album grid (one playing), an artist's row
		void page11(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			std::vector<Kit::FilterItem> views = {{"Albums", 148}, {"Artists", 61}, {"Playlists", 5}};
			lay::MusicHeader mh = lay::musicHeader({16, 4, 608, 0}, k.filterWidths(views));
			Kit::SectionLine line;
			line.text = "From Music on both cards \xC2\xB7 148 albums \xC2\xB7 1,912 songs";
			k.sectionLine(lay::sectionLine(mh.line, true), line);
			k.filters(mh.chips, views, 0);
			lay::AlbumGrid grid = lay::albumGrid({16, 90, 608, 300}, 10);
			const char* albums[10][2] = {{"Yellow Album", "The Plantains"}, {"Late Night", "The Carrots"},
				{"Pixel Sea", "Harbor Cats"}, {"Field Recordings", "Various"}, {"Steel", "Tin Roof"},
				{"Grape", "The Plantains"}, {"Meadow", "Bunny Hop Band"}, {"Red Room", "Tin Roof"},
				{"Honey", "The Carrots"}, {"Unknown album", "Unknown artist"}};
			for (int i = 0; i < 10 && i < (int)grid.cell.size(); i++)
			{
				Kit::Album a;
				a.artKey = albums[i][0];
				a.title = albums[i][0];
				a.artist = albums[i][1];
				a.focused = i == 0;
				a.playing = i == 2;
				a.paused = i == 8;
				k.albumTile(lay::albumTile(grid.cell[i]), a);
			}
			Kit::Artist artist;
			artist.name = "The Plantains";
			artist.second = "3 albums \xC2\xB7 34 songs \xC2\xB7 2 h 6 min";
			artist.focused = true;
			k.artistRow(lay::artistRow({16, 380, 608, lay::ARTIST_ROW_H}), artist);
			k.hints({{"L2/R2", "View"}, {"A", "Open"}, {"X", "More"}, {"L3", "Search"}});
		}

		// Videos: a row left part way (its resume line), one finished (a tick), a fresh one, and a folder
		void page12(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			Kit::Video v;
			v.name = "Big Buck Bunny.mkv";
			v.note = "6:12 / 9:56";
			v.second = "Movies \xC2\xB7 2.1 GB \xC2\xB7 12 Nov";
			v.resume = 0.62;
			v.focused = true;
			k.videoRow(lay::videoRow({16, 6, 608, lay::VIDEO_ROW_H}), v);
			v = Kit::Video();
			v.name = "Caminandes 1.mkv";
			v.note = "1:30";
			v.second = "Shorts \xC2\xB7 148 MB \xC2\xB7 3 Nov";
			v.done = true;
			k.videoRow(lay::videoRow({16, 72, 608, lay::VIDEO_ROW_H}), v);
			v = Kit::Video();
			v.name = "Sintel.mkv";
			v.note = "4:12 / 14:48";
			v.second = "Movies \xC2\xB7 1.4 GB \xC2\xB7 8 Nov";
			v.resume = 0.28;
			k.videoRow(lay::videoRow({16, 138, 608, lay::VIDEO_ROW_H}), v);
			Kit::Folder f;
			f.name = "Trip 2026";
			f.second = "42 videos \xC2\xB7 6.2 GB";
			f.note = "12 Nov";
			k.folderRow(lay::folderRow({16, 204, 608, lay::FOLDER_ROW_H}), f);
			Kit::SectionLine line;
			line.text = "From Videos and Movies on both cards \xC2\xB7 86 videos \xC2\xB7 9 folders";
			k.sectionLine(lay::sectionLine({16, 280, 608, lay::LINE_H}, false), line);
			k.hints({{"L2/R2", "View"}, {"A", "Play"}, {"X", "More"}, {"LEFT/RIGHT", "Page"}});
		}

		// Books: the Reading now strip (title and progress) over the shelf's grid (title only), the book's own drawn cover
		void page13(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			lay::BooksShelf sh = lay::booksShelf({16, 4, 608, 440}, 5, 10);
			Kit::SectionLine head;
			head.caps = true;
			head.text = "Reading now \xC2\xB7 5";
			k.sectionLine(lay::sectionLine(sh.readingHead, false), head);
			const char* reading[5] = {"Alice's Adventures", "The Time Machine", "Handheld Field Guide", "Frankenstein", "Krazy Kat"};
			const double progress[5] = {0.07, 0.58, 0.06, 0.33, 0.81};
			for (int i = 0; i < 5 && i < (int)sh.reading.size(); i++)
			{
				Kit::Cover c;
				c.key = reading[i];
				c.title = reading[i];
				c.focused = i == 0;
				c.badge = i == 1 ? "3" : "";
				c.progress = progress[i];
				k.coverTile(lay::coverTile(sh.reading[i], true, true), c);
			}
			head.text = "Shelf \xC2\xB7 10";
			k.sectionLine(lay::sectionLine(sh.shelfHead, false), head);
			const char* shelf[10] = {"Frankenstein", "The Wind in the Willows", "Treasure Island", "Krazy Kat", "Notes",
				"Alice's Adventures", "The Time Machine", "Field Guide", "Moby Dick", "The Yellow Kid"};
			int last = std::min((int)sh.shelf.size(), (sh.first + sh.shown) * sh.cols);
			for (int i = 0; i < last; i++)
			{
				Kit::Cover c;
				c.key = shelf[i];
				c.title = shelf[i];
				k.coverTile(lay::coverTile(sh.shelf[i], true, false), c);
			}
			k.hints({{"A", "Open"}, {"X", "More"}, {"L2/R2", "Shelf"}, {"L3", "Search"}});
		}

		// Search: the field, two groups' headings and rows, Recent searches and its Clear row
		void page14(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			lay::SearchPage p = lay::searchPage({16, 4, 608, 440}, {1, 1}, 2);
			Kit::SearchFieldView f;
			f.value = "bunn";
			k.searchField(lay::searchField(p.field), f);
			Kit::SectionLine head;
			head.caps = true;
			head.text = "Videos \xC2\xB7 2";
			k.sectionLine(lay::sectionLine(p.group[0].head, false), head);
			Kit::Result r;
			r.glyph = &G_FILM;
			r.name = "Big Buck Bunny.mkv";
			r.note = "Videos / Movies \xC2\xB7 9:56";
			r.matchAt = 9;
			r.matchLen = 5;
			r.focused = true;
			k.resultRow(lay::resultRow(p.row[p.group[0].firstRow]), r);
			head.text = "Music \xC2\xB7 4";
			k.sectionLine(lay::sectionLine(p.group[1].head, false), head);
			r = Kit::Result();
			r.glyph = &G_NOTE;
			r.name = "Bunny Hop";
			r.note = "The Plantains \xC2\xB7 Yellow Album";
			r.matchAt = 0;
			r.matchLen = 5;
			k.resultRow(lay::resultRow(p.row[p.group[1].firstRow]), r);
			head.text = "Recent searches";
			k.sectionLine(lay::sectionLine(p.recentHead, false), head);
			const char* recent[2] = {"bunn", "late night"};
			for (int i = 0; i < 2 && i < (int)p.recent.size(); i++)
			{
				r = Kit::Result();
				r.glyph = &G_CLOCK;
				r.name = recent[i];
				k.resultRow(lay::resultRow(p.recent[i]), r);
			}
			r = Kit::Result();
			r.glyph = &G_TRASH;
			r.name = "Clear recent searches";
			k.resultRow(lay::resultRow(p.clear), r);
			k.hints({{"A", "Open"}, {"Y", "Edit"}, {"B", "Back"}});
		}

		void draw(const Player&) override
		{
			Kit& k = *ui.kit;
			switch (page)
			{
			case 2: page2(k); break;
			case 3: page3(k); break;
			case 4: page4(k); break;
			case 5: page5(k); break;
			case 6: page6(k); break;
			case 7: page7(k); break;
			case 8: page8(k); break;
			case 9: page9(k); break;
			case 10: page10(k); break;
			case 11: page11(k); break;
			case 12: page12(k); break;
			case 13: page13(k); break;
			case 14: page14(k); break;
			default: page1(k); break;
			}
		}
	};

	void Ui::openVariantsGallery(int page)
	{
		pushOverlay(std::make_unique<VariantsGallery>(*this, page));
	}
}
