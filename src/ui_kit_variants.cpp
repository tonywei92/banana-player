// The kit's second set (declared in ui_kit.h's "KIT VARIANTS" block; layouts in kitlayout.h; docs/notes/
// 2026-11-18-kit-variants.md): status cluster and badges, mini bar, filter chips, form row, alert / confirm / busy
// sheets, keyboard, volume bar, tiles, Recent and Continue cards, empty state, notice row, select and clip bars.
// The list and page pieces of 2026-11-19 are here too: the state pills Home and the mini bar share, the Search
// field and result row, a Music/Videos row, a Books cover tile and a page's line (docs/notes/2026-11-19-kit-inventory.md).
// As the rest of the kit: a Rect from kitlayout and a small struct in, pixels out, nothing kept; tokens only.
#include "ui_glyphs.h"
#include <map>
#include "ui_kit.h"
#include "artph.h"
#include "session_host.h"
#include <algorithm>
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

	std::string Ui::Kit::elided(const std::string& s, int font, int room) const
	{
		if (room <= 0) return "...";
		if (textW(s, font) <= room) return s;
		std::vector<size_t> starts;
		for (size_t i = 0; i < s.size(); i++)
			if (((unsigned char)s[i] & 0xC0) != 0x80) starts.push_back(i);
		size_t lo = 0, hi = starts.size();	// prefixes of starts[0..n) chars: lo fits, hi doesn't
		while (hi - lo > 1)
		{
			size_t mid = (lo + hi) / 2;
			if (textW(s.substr(0, starts[mid]) + "...", font) <= room) lo = mid;
			else hi = mid;
		}
		return s.substr(0, lo < starts.size() ? starts[lo] : s.size()) + "...";
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

	void Ui::Kit::stateBadges(Status& s, const Player& p)
	{
		if (lay::boostBadgeShown(p.boost))
			s.badges.push_back({nullptr, std::to_string(p.boost) + "%", lay::boostBadgeDanger(p.boost)});
		// something plays in a dashboard page's browser: its name (the page's own, "Browser" unknown)
		if (SessionHost::get().pageIsOutput()) s.badges.push_back({&G_WEB, ui.pageName()});
		if (ui.wifiOff()) s.badges.push_back({&G_WIFI, "Off", true});
		if (ui.incognito) s.badges.push_back({&G_INCOGNITO, ""});
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
		if (s.wifi >= 0) items.push_back({WIFI, 22, 0});
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
				// the spec's mark: the dot is one bar of signal, the middle roof two, the outer roof three or more
				for (int r = 0; r < G_WIFI.h; r++)
				{
					int need = r < 3 ? 3 : r < 6 ? 2 : 1;
					for (int c = 0; c < 11; c++)
						if (G_WIFI.rows[r][c] == '#') ui.fill({x + c * 2, cy - 8 + r * 2, 2, 2}, s.wifi >= need ? DIM_ON : TRACK);
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
		sheet(l.sheet, {&G_ALERT, c.title, "", c.danger || c.titleDanger});
		for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], F_I14, TEXT, l.text.x, l.text.y + 10 + (int)i * 20, l.text.w);
		// the safe key on the left: yellow, focused, B; the action on the right, red ink when it destroys
		Key safe;
		safe.label = c.safe;
		safe.key = l.third.w > 0 ? "A" : "B";
		safe.tone = Tone::Primary;
		safe.focused = c.focus == 0;
		safe.pressed = c.pressed == 0;
		key(l.safe, safe);
		Key act;
		act.label = c.action;
		act.key = l.third.w > 0 ? "B" : c.focus == 1 ? "A" : "";
		act.tone = c.danger ? Tone::Danger : Tone::Normal;
		act.focused = c.focus == 1;
		act.pressed = c.pressed == 1;
		key(l.action, act);
		// a third key beside them (the update question's "Not until tomorrow": no cap of its own, chosen by touch)
		if (l.third.w > 0 && !c.third.empty())
		{
			Key alt;
			alt.label = c.third;
			alt.key = c.thirdKey;
			alt.focused = c.focus == 2;
			alt.pressed = c.pressed == 2;
			key(l.third, alt);
		}
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
		std::vector<Hint> h;
		if (l.third.w > 0)
		{
			h.push_back({"LEFT/RIGHT", "Choose"});
			h.push_back({"A", c.focus == 0 ? c.safe : c.focus == 1 ? c.action : c.third});
			h.push_back({"B", c.action});
			if (!c.thirdKey.empty()) h.push_back({c.thirdKey, c.third});
		}
		else
		{
			h.push_back({"LEFT/RIGHT", "Choose"});
			h.push_back({"A", c.focus == 0 ? c.safe : c.action});
			h.push_back({"B", c.safe});
		}
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
		k.tone = b.primary ? Tone::Primary : Tone::Normal;
		key(l.key, k);
		if (l.key2.w > 0 && !b.key2.empty())
		{
			Key k2;
			k2.label = b.key2;
			k2.key = b.cap2;
			k2.pressed = b.pressed2 == 1;
			k2.focused = b.pressed2 == 1;
			key(l.key2, k2);
			hints({{b.cap, b.key}, {b.cap2, b.key2}});
			return;
		}
		hints({{b.cap, b.key}});
	}

	// ---- the keyboard ----

	void Ui::Kit::KeyboardView::keys(const KeyPad& pad, bool empty)
	{
		rows.clear();
		bottom.clear();
		int key = 0;
		for (int n : KeyPad::ROWS)
		{
			std::vector<std::string> line;
			for (int i = 0; i < n; i++) line.push_back(pad.label(key++));
			rows.push_back(line);
		}
		for (; key < pad.keys(); key++)
		{
			BottomKey b;
			b.label = pad.label(key);
			b.cap = pad.button(key);
			b.lit = pad.lit(key);
			b.primary = pad.role(key) == KeyPad::Done;
			b.off = pad.role(key) == KeyPad::Clear && empty;	// nothing to empty
			bottom.push_back(b);
		}
	}

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
		// the bottom row: the toggles, Space, Clear, Del (TRACK) and Done (primary), each its word and its button's
		// cap. A toggle that is on is lit: the accent's tint, its word in ACCENT and a lamp bar at its top, until
		// it is pressed again (the focus is the ACCENT edge, so the two never read as one)
		for (size_t i = 0; i < l.bottom.size() && i < v.bottom.size(); i++, index++)
		{
			Rect r = l.bottom[i];
			const KeyboardView::BottomKey& b = v.bottom[i];
			SDL_Color ground = b.primary ? ACCENT : b.lit ? sdl(theme::tintOf(rgbOf(TRACK))) : TRACK;
			SDL_Color fill = index != v.pressed ? ground : b.primary ? sdl(theme::lipOf(theme::ACCENT)) : PRESSED;
			ui.drawBox(r, fill, R_BUTTON, index == v.cursor ? ACCENT : edgeOf(ground));
			if (b.lit) ui.fillRound({r.x + r.w / 2 - 9, r.y + 6, 18, 3}, ACCENT, 1);
			SDL_Color ink = b.primary ? ON_ACCENT : off(b.lit || index == v.cursor ? ACCENT : TEXT, b.off);
			// the word and its cap side by side, centred; a word too long for its key is cut, never its cap
			int capW = b.cap.empty() ? 0 : capsW(b.cap), by = r.y + (r.h - theme::LIP) / 2 + 1;
			int tw = std::min(r.w - 8 - (capW ? capW + 6 : 0), textW(b.label, F_P14));
			int w = tw + (capW ? 6 + capW : 0), x0 = r.x + r.w / 2 - w / 2;
			ui.drawTextMid(b.label, F_P14, ink, x0, by, tw + 1);
			if (capW) caps(b.cap, x0 + tw + 6, by, b.primary ? ON_ACCENT : off(ACCENT, b.off), fill);
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
		// a name too long for the tile at 16 takes 14 before it would be cut ("Web dashboard" in Home's third)
		int nameFont = textW(t.name, F_P16) > right - x ? F_P14 : F_P16;
		ui.drawTextMid(t.name, nameFont, ink, x, t.note.empty() ? cy : cy - 9, std::max(1, right - x));
		if (!t.note.empty()) ui.drawTextMid(t.note, F_I12, noteInk, x, cy + 11, std::max(1, right - x));
	}

	void Ui::Kit::choiceTile(Rect r, const Choice& c)
	{
		bool clean = theme::drawingClean();
		// (Clean: the chosen one the accent's tint, and a dot in its corner so it is found in a grid of fifteen; a
		// stronger wash was tried and the owner preferred the tint)
		SDL_Color fill = clean ? (c.on ? sdl(theme::tintOf(rgbOf(c.pressed ? PRESSED : BUTTON))) : c.pressed ? PRESSED : cardFill(SURFACE))
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

	void Ui::Kit::recentTile(const lay::AlbumTile& l, const Card& c)
	{
		// focused: the album tile's 3-unit ACCENT outline 2 clear of the picture; pressed: a PRESSED rim in its place
		if (c.focused || c.pressed)
		{
			ui.fillRound({l.cover.x - 5, l.cover.y - 5, l.cover.w + 10, l.cover.h + 10}, c.focused ? ACCENT : PRESSED, 10);
			ui.fillRound({l.cover.x - 2, l.cover.y - 2, l.cover.w + 4, l.cover.h + 4}, BG, 7);
		}
		pictureOf(l.cover, c, 5);
		// the kind's mark over the picture: a dark chip, light ink in every theme
		if (c.kind && c.picture)
		{
			Rect m{l.cover.x + 5, l.cover.y + 5, 22, 16};
			ui.fillRound(m, sdl(theme::TOP_CARD, 200), 5);
			ui.drawGlyphMid(*c.kind, m.x + m.w / 2, m.y + m.h / 2, sdl(theme::TOP_TITLE), 1);
		}
		if (c.progress >= 0) bar({l.cover.x + 6, l.cover.y + l.cover.h - 9, l.cover.w - 12, 3}, c.progress);
		ui.drawTextMid(c.title, F_P13, off(c.focused ? ACCENT : TEXT, c.off), l.title.x, l.title.y + l.title.h / 2, l.title.w);
		ui.drawTextMid(c.note, F_I12, off(c.focused ? DIM_ON : DIM, c.off), l.artist.x, l.artist.y + l.artist.h / 2, l.artist.w);
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
		{
			int cy = l.lines.y + 10 + (int)i * 20;
			// the accent words in this line, as the preview marks a folder an empty page names ("Music",
			// "Videos or Movies"); a range outside the line, or none given, draws the line plain
			std::vector<Empty::AccentWord> words;
			for (auto& w : e.accent)
				if (w.line == (int)i && w.at >= 0 && w.len > 0 && w.at + w.len <= (int)e.lines[i].size()) words.push_back(w);
			std::sort(words.begin(), words.end(), [](const Empty::AccentWord& a, const Empty::AccentWord& b) { return a.at < b.at; });
			std::vector<std::pair<std::string, bool>> seg;
			int at = 0;
			for (auto& w : words)
			{
				if (w.at > at) seg.push_back({e.lines[i].substr(at, w.at - at), false});
				seg.push_back({e.lines[i].substr(w.at, w.len), true});
				at = w.at + w.len;
			}
			if (at < (int)e.lines[i].size()) seg.push_back({e.lines[i].substr(at), false});
			int total = 0;
			for (auto& s : seg) total += textW(s.first, F_I14);
			if (words.empty() || total > l.lines.w)
			{
				ui.drawTextMid(e.lines[i], F_I14, DIM, l.lines.x + l.lines.w / 2, cy, l.lines.w, 2);
				continue;
			}
			// the pieces drawn one after another, the whole run centred as one line
			int x = l.lines.x + (l.lines.w - total) / 2;
			for (auto& s : seg)
			{
				ui.drawTextMid(s.first, F_I14, s.second ? ACCENT : DIM, x, cy, l.lines.w, 0);
				x += textW(s.first, F_I14);
			}
		}
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

	void Ui::Kit::selectBar(const lay::Box keys[4], const BarKey k[4], int n)
	{
		for (int i = 0; i < n; i++) key(keys[i], barKey(k[i]));
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
		if (r.art) r.art(lay::resultArt(l));
		else if (r.glyph) ui.drawGlyphMid(*r.glyph, l.icon.x + l.icon.w / 2, cy, off(r.focused ? ACCENT : DIM_ON, r.off));
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
		else bookPlaceholder(l.cover, c.key, 5, c.author, c.fmt);
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

	void Ui::Kit::bookPlaceholder(Rect r, const std::string& key, int rad, const std::string& author, const std::string& fmt)
	{
		// A cover drawn from the title (bookcover.h): a ground picked by the title's hash, a darker spine down the
		// left, the title in large type, the author under it, the format's mark in the corner. Too small for type
		// (a list row's 32 x 46): the title's first letter.
		const bookcover::Rgb g = bookcover::ground(key), ik = bookcover::ink(), dm = bookcover::inkDim();
		const SDL_Color ground{g.r, g.g, g.b, 255}, ink{ik.r, ik.g, ik.b, 255}, dim{dm.r, dm.g, dm.b, 255};
		const SDL_Color spineC{(Uint8)(g.r * 3 / 4), (Uint8)(g.g * 3 / 4), (Uint8)(g.b * 3 / 4), 255};
		ui.fillRound(r, ground, rad);
		int in = std::max(1, theme::EDGE);
		int spine = std::max(5, r.w / 14);
		ui.fillRound({r.x + in, r.y + in + 3, spine, r.h - 2 * in - 6}, spineC, std::max(2, rad - in));
		const int left = r.x + in + spine + lay::SP8, right = r.x + r.w - lay::SP8, w = std::max(10, right - left);
		if (r.w < 64)
		{
			std::string t = key.substr(0, key.empty() ? 0 : (((unsigned char)key[0] >= 0xC0) ? 2 : 1));
			for (char& c : t) if (c >= 'a' && c <= 'z') c = (char)(c - 32);
			ui.drawTextMid(t, F_P13, ink, left + w / 2, r.y + r.h / 2, w, 2);
			return;
		}
		const bool big = r.w >= 150;
		const int tf = big ? F_P20 : F_P13, tlh = big ? 24 : 16, af = F_I12, alh = 15;
		const int top = r.y + lay::SP12, foot = r.y + r.h - lay::SP8 - 22;
		int maxLines = std::max(1, std::min(big ? 6 : 5, (foot - top - (author.empty() ? 0 : 2 * alh + 6)) / tlh));
		// the wrap is measured text: kept per title and size (a shelf draws the same covers as it scrolls)
		static std::map<std::string, std::vector<std::string>> kept;
		if (kept.size() > 300) kept.clear();
		auto wrapped = [&](const std::string& text, int font, int w2, int most) -> const std::vector<std::string>& {
			std::string k = text + "|" + std::to_string(font) + "|" + std::to_string(w2) + "|" + std::to_string(most) + "|" +
				std::to_string(ui.textWidth("W", font));
			auto it = kept.find(k);
			if (it == kept.end()) it = kept.emplace(k, ui.wrapText(text, font, w2, most)).first;
			return it->second;
		};
		int y = top;
		for (const std::string& line : wrapped(key, tf, w, maxLines))
		{
			ui.drawTextMid(line, tf, ink, left, y + tlh / 2, w);
			y += tlh;
		}
		if (!author.empty())
		{
			y += 6;
			for (const std::string& line : wrapped(author, af, w, 2))
			{
				ui.drawTextMid(line, af, dim, left, y + alh / 2, w);
				y += alh;
			}
		}
		const Glyph& mark = bookKindGlyph(bookcover::kindOf(fmt));
		ui.drawGlyphMid(mark, right - 6, r.y + r.h - lay::SP8 - 8, dim, 1);
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
		// the thumbnail: the caller's frame (the videos' cache, ui_mediaart.h); else the kind's mark on TRACK
		if (v.art) v.art(l.thumb);
		else
		{
			ui.fillRound(l.thumb, off(TRACK, v.off), 5);
			const Glyph* g = v.glyph ? v.glyph : &G_FILM;
			ui.drawGlyphMid(*g, l.thumb.x + l.thumb.w / 2, l.thumb.y + l.thumb.h / 2, off(DIM_ON, v.off));
		}
		// the right end, leftwards: the length, a tick when the file is finished, then a Playing / Paused pill on
		// the row that plays
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
		if (v.playing || v.paused)
		{
			right -= pill(right, l.note.y + l.note.h / 2, v.paused ? "Paused" : "Playing",
				v.paused ? Pill::Fact : Pill::Live) + 8;
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

	// ---- the library's pages: an album's and an artist's head, a track row, a Playlists row, the backdrop ----

	void Ui::Kit::pageHead(const lay::PageHead& l, const PageHead& h)
	{
		if (h.art) h.art(l.art);
		else if (h.disc)
		{
			// the initial's disc, as an artist's row has it at 40 (a badge's pair: the ACCENT's tint, ACCENT ink)
			ui.fillRound(l.art, sdl(theme::mix(rgbOf(BUTTON), rgbOf(ACCENT), 17)), l.art.h / 2);
			ui.drawTextMid(firstLetter(h.title), F_P28, ACCENT, l.art.x + l.art.w / 2, l.art.y + l.art.h / 2, l.art.w - 8, 2);
		}
		else artPlaceholder(l.art, h.artKey.empty() ? h.title : h.artKey, 5, false);
		ui.drawTextMid(h.title, F_P28, TEXT, l.title.x, l.title.y + l.title.h / 2, l.title.w);
		if (l.lines >= 1 && h.lines.size() >= 1)
			ui.drawTextMid(h.lines[0], F_I14, DIM, l.line[0].x, l.line[0].y + l.line[0].h / 2, l.line[0].w);
		if (l.lines >= 2 && h.lines.size() >= 2)
			ui.drawTextMid(h.lines[1], F_I12, DIM, l.line[1].x, l.line[1].y + l.line[1].h / 2, l.line[1].w);
		Key play;
		play.glyph = &G_PLAY;
		play.label = "Play";
		play.key = h.hotkeys ? "A" : "";
		play.tone = Tone::Primary;
		play.focused = h.focused == 0;
		play.pressed = h.pressed == 0;
		key(l.key[0], play);
		Key shuffle;
		shuffle.glyph = &G_SHUFFLE;
		shuffle.label = "Shuffle";
		shuffle.key = h.hotkeys ? "Y" : "";
		shuffle.focused = h.focused == 1;
		shuffle.pressed = h.pressed == 1;
		key(l.key[1], shuffle);
		if (l.keys >= 3)
		{
			// Add is a key by touch only (its rows are in X · More by the buttons)
			Key add;
			add.glyph = &G_PLUS;
			add.focused = h.focused == 2;
			add.pressed = h.pressed == 2;
			key(l.key[2], add);
		}
	}

	void Ui::Kit::trackRow(const lay::TrackRow& l, const Track& t)
	{
		if (t.pressed) ui.fillRound(l.row, PRESSED, R_BUTTON);
		else if (t.focused) flat(l.row, BUTTON, R_BUTTON, ACCENT);
		int cy = l.row.y + l.row.h / 2;
		// the number's place: the playing (or paused) mark, or its number right-aligned in its slot
		if (t.playing || t.paused)
			ui.drawGlyphMid(t.paused ? G_PAUSE : G_PLAY, l.number.x + l.number.w / 2, cy, off(ACCENT, t.off));
		else if (!t.number.empty())
			ui.drawTextMid(t.number, F_P14, off(DIM, t.off), l.number.x + l.number.w, cy, l.number.w, 1);
		// the right end: the length, the heart before it, and the pill's room before that
		int noteW = 0;
		if (!t.note.empty())
		{
			noteW = std::min(l.row.w / 2, textW(t.note, F_I12));
			ui.drawTextMid(t.note, F_I12, off(t.focused ? DIM_ON : DIM, t.off), l.right, cy, l.row.w / 2, 1);
		}
		int noteLeft = l.right - noteW;
		if (t.heart) ui.drawGlyphMid(G_HEART_S, noteLeft - lay::SP6 - 7, cy, off(ACCENT, t.off));
		int limit = t.heart ? noteLeft - lay::SP6 - 14 - lay::SP6 : noteLeft - lay::SP6;
		int pw = t.pill.empty() ? 0 : pillW(t.pill);
		int room = std::max(1, limit - (pw ? pw + lay::SP6 : 0) - l.name.x);
		// the title, cut with "..." so the pill after it always fits, and the pill right after it (the mock-up
		// has the VIDEO pill follow the title, not sit at the row's end)
		std::string name = elided(t.name, F_SMALL, room);
		int nameW = textW(name, F_SMALL);
		SDL_Color ink = off(t.focused ? ACCENT : TEXT, t.off);
		if (t.focused && !t.off) ui.drawMarquee(name, F_SMALL, ink, l.name.x, cy, room, 1);
		else ui.drawTextMid(name, F_SMALL, ink, l.name.x, cy, room);
		if (pw) pill(l.name.x + nameW + lay::SP6 + pw, cy, t.pill, t.tone);
	}

	void Ui::Kit::playlistRow(const lay::PlaylistRow& l, const Playlist& p)
	{
		if (p.pressed) ui.fillRound(l.row, PRESSED, R_BUTTON);
		else if (p.focused) flat(l.row, BUTTON, R_BUTTON, ACCENT);
		int cy = l.row.y + l.row.h / 2;
		// the lead: Liked's filled heart, or the row's own icon, ACCENT when lit (Liked's and the playing row's are)
		const Glyph* g = p.liked ? &G_HEART : p.glyph;
		if (g) ui.drawGlyphMid(*g, l.icon.x + l.icon.w / 2, cy, off(p.liked || p.lit || p.focused ? ACCENT : DIM_ON, p.off));
		int noteW = 0;
		if (!p.note.empty())
		{
			noteW = std::min(l.row.w / 2, textW(p.note, F_I12));
			ui.drawTextMid(p.note, F_I12, off(p.focused ? DIM_ON : DIM, p.off), l.right, cy, l.row.w / 2, 1);
		}
		// the Now pill (a play mark and the word), held out of the name's room so it always shows after the name
		int nowW = p.playing ? 16 + ui.glyphW(G_PLAY, 1) + 4 + textW("Playing", F_I11B) : 0;
		int room = std::max(1, l.right - noteW - lay::SP8 - (nowW ? nowW + lay::SP6 : 0) - l.name.x);
		std::string name = elided(p.name, F_SMALL, room);
		int nameW = textW(name, F_SMALL);
		SDL_Color ink = off(p.focused || p.lit ? ACCENT : TEXT, p.off);
		if (p.focused && !p.off) ui.drawMarquee(name, F_SMALL, ink, l.name.x, cy, room, 1);
		else ui.drawTextMid(name, F_SMALL, ink, l.name.x, cy, room);
		if (nowW)
		{
			Rect pr{l.name.x + nameW + lay::SP6, cy - 9, nowW, 17};
			bool clean = theme::drawingClean();
			SDL_Color pillInk = clean ? ACCENT : ON_ACCENT;
			ui.fillRound(pr, clean ? sdl(theme::mix(rgbOf(BG), rgbOf(ACCENT), 17)) : ACCENT, theme::R_CAP);
			ui.drawGlyphMid(G_PLAY, pr.x + 8 + ui.glyphW(G_PLAY, 1) / 2, cy, pillInk, 1);
			ui.drawTextMid("Playing", F_I11B, pillInk, pr.x + 12 + ui.glyphW(G_PLAY, 1), cy);
		}
	}

	SDL_Texture* Ui::Kit::backdropPage(const artbg::Art& art, const artbg::Backdrop& b, artbg::Half half)
	{
		constexpr int PW = artbg::W, PH = 480;	// the panel's height in rows (the veil's stops are per row of it)
		std::vector<uint8_t> px = artbg::composePage(art, b, PW, PH, half);
		if (px.empty()) return nullptr;
		SDL_Texture* t = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, PW, PH);
		if (!t) return nullptr;
		SDL_UpdateTexture(t, nullptr, px.data(), PW * 4);
		SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);	// the blur's own smoothness at panel size
		return t;
	}

	void Ui::Kit::backdrop(Rect r, SDL_Texture* page, Uint8 fade)
	{
		if (r.w <= 0 || r.h <= 0 || !page) return;
		// opaque at full: no blending (the panel's widest copy, every redraw)
		SDL_SetTextureBlendMode(page, fade < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
		SDL_SetTextureAlphaMod(page, fade);
		SDL_Rect d = ui.devRect(r);
		SDL_RenderCopy(ui.ren, page, nullptr, &d);
		SDL_SetTextureAlphaMod(page, 255);
	}
}

namespace bp
{
	// ---- the gallery: every variant on a page, for a `shot:` (a test-only --press step, `gallery:variants[:PAGE]`) ----

	struct Ui::VariantsGallery : Ui::Overlay
	{
		int page;
		// page 20's synthetic arts' backdrop pages (an art in a dark and a light theme each), made once (the blur
		// is not cheap); they die with the overlay
		bool artMade = false;
		SDL_Texture* artPages[4] = {};
		explicit VariantsGallery(Ui& u, int pg) : Overlay(u), page(pg) {}
		~VariantsGallery()
		{
			for (SDL_Texture* t : artPages)
				if (t) SDL_DestroyTexture(t);
		}
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
			st.badges = {{&G_INCOGNITO, "Incognito", false}, {&G_QR, "Chrome", false}};
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
			k.hints({{"L/R", "Filter"}, {"A", "Open"}, {"B", "Back"}});
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
				k.recentTile(lay::albumTile({16 + i * (lay::RECENT_W + lay::RECENT_GAP), 278, lay::RECENT_W, lay::RECENT_H}), r);
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
			v.rows = {{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"}, {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"}, {"a", "s", "d", "f", "g", "h", "j", "k", "l", "-"}, {"z", "x", "c", "v", "b", "n", "m", ".", "/", ":"}};
			v.bottom = {{"Shift", "Y", true}, {"123", "L3"}, {"Space", "X"}, {"Clear", "L2"}, {"Del", "B"}, {"Done", "START", false, true}};
			v.cursor = 23;
			lay::Keyboard l = lay::keyboard({lay::MARGIN, 60, lay::PANEL_W - 2 * lay::MARGIN, lay::PANEL_H - 60 - lay::HINTS_H},
				{100, 90, 80, 90}, {10, 10, 10, 10}, 2);
			k.keyboard(l, v);
			k.iconKey({16, 8, 56, 46}, G_BACK, false, 3, true);
			ui.drawTextMid("Address", F_P28, TEXT, 84, 31, 400);
			k.hints({{"A", "Type"}, {"B", "Delete"}, {"L/R", "Move"}, {"START", "Done"}});
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
			k.hints({{"L/R", "View"}, {"A", "Open"}, {"X", "More"}, {"L3", "Search"}});
		}

		// Music: the view chips at the body's top (the from-line is the header's crumb), the album grid (one
		// playing), an artist's row
		void page11(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			std::vector<Kit::FilterItem> views = {{"Albums", 148}, {"Artists", 61}, {"Playlists", 5}};
			lay::MusicHeader mh = lay::musicHeader({16, 4, 608, 0}, k.filterWidths(views));
			k.filters(mh.chips, views, 0);
			lay::AlbumGrid grid = lay::albumGrid({16, 56, 608, 310}, 10);
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
			k.hints({{"L/R", "View"}, {"A", "Open"}, {"X", "More"}, {"L3", "Search"}});
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
			k.hints({{"L/R", "View"}, {"A", "Play"}, {"X", "More"}, {"LEFT/RIGHT", "Page"}});
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
			k.hints({{"A", "Open"}, {"X", "More"}, {"L/R", "Shelf"}, {"L3", "Search"}});
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

		// an album's page (screens.js music-album): the head with its art and the Play / Shuffle / Add keys, then
		// the track rows — the playing mark in the number's place, a VIDEO pill, a heart, focus and pressed
		void page15(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			lay::PageHead h = lay::pageHead({16, 0, 608, 450}, 2, 3);
			Kit::PageHead v;
			v.artKey = "Yellow Album";
			v.title = "Yellow Album";
			v.lines = {"The Plantains", "12 songs, 1 a video \xC2\xB7 48 min"};
			v.focused = 0;
			k.pageHead(h, v);
			auto row = [&](int i, const char* number, const char* name, const char* note, const char* pill,
				bool playing, bool heart, bool pressed, bool focused) {
				Kit::Track t;
				t.number = number;
				t.name = name;
				t.note = note;
				t.pill = pill;
				t.playing = playing;
				t.heart = heart;
				t.pressed = pressed;
				t.focused = focused;
				k.trackRow(lay::trackRow({16, h.content.y + i * lay::TRACK_ROW_H, 608, lay::TRACK_ROW_H}), t);
			};
			row(0, "", "Sunny Peel", "3:34", "", true, true, false, false);
			row(1, "2", "Green at the Tips", "4:02", "", false, false, false, false);
			row(2, "3", "Bunny Hop", "2:58", "VIDEO", false, false, true, false);
			row(3, "4", "Ripe", "3:12", "", false, true, false, true);
			row(4, "5", "Peel Slowly", "5:07", "", false, false, false, false);
			k.hints({{"A", "Play"}, {"Y", "Shuffle"}, {"DOWN", "Songs"}, {"X", "More"}, {"B", "Albums"}});
		}

		// an artist's page (screens.js music-artist): the head with the initial's disc (the same head, one line),
		// their albums as the grid (the artist pane's cells say the year and the count), then their songs
		void page16(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			lay::PageHead h = lay::pageHead({16, 0, 608, 450}, 1, 3);
			Kit::PageHead v;
			v.disc = true;
			v.title = "The Plantains";
			v.lines = {"3 albums \xC2\xB7 34 songs \xC2\xB7 2 h 6 min"};
			v.focused = 0;
			k.pageHead(h, v);
			Kit::SectionLine head;
			head.caps = true;
			head.text = "Albums \xC2\xB7 3";
			k.sectionLine(lay::sectionLine({16, h.content.y, 608, lay::LINE_H}, false), head);
			int gy = h.content.y + lay::LINE_H + lay::SP8;
			lay::AlbumGrid grid = lay::albumGrid({16, gy, 608, 450 - gy}, 3);
			const char* albums[3][2] = {{"Yellow Album", "2026 \xC2\xB7 12 songs"}, {"Grape", "2025 \xC2\xB7 11 songs"},
				{"Wheel", "2023 \xC2\xB7 11 songs"}};
			for (int i = 0; i < 3; i++)
			{
				Kit::Album a;
				a.artKey = albums[i][0];
				a.title = albums[i][0];
				a.artist = albums[i][1];
				a.focused = i == 0;
				k.albumTile(lay::albumTile(grid.cell[i]), a);
			}
			head.text = "Songs \xC2\xB7 34";
			k.sectionLine(lay::sectionLine({16, gy + lay::ALBUM_CELL_H + lay::SP12, 608, lay::LINE_H}, false), head);
			Kit::Track t;
			t.name = "Sunny Peel";
			t.note = "Yellow Album \xC2\xB7 3:34";
			t.focused = true;
			k.trackRow(lay::trackRow({16, gy + lay::ALBUM_CELL_H + lay::SP12 + lay::LINE_H + lay::SP8, 608, lay::TRACK_ROW_H}), t);
			k.hints({{"A", "Play"}, {"Y", "Shuffle"}, {"DOWN", "Albums, songs"}, {"X", "More"}, {"B", "Artists"}});
		}

		// Music's Playlists view (screens.js playlists): the view chips, then the rows — Liked's heart, the Now
		// pill on the one that plays, the counts, the dimmed New playlist row ("5 playlists · Liked first" is the
		// header's crumb there, not a line in the body)
		void page17(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			std::vector<Kit::FilterItem> views = {{"Albums", 148}, {"Artists", 61}, {"Playlists", 5}};
			lay::MusicHeader mh = lay::musicHeader({16, 4, 608, 0}, k.filterWidths(views));
			k.filters(mh.chips, views, 2);
			int y = mh.strip.y + lay::CHIP_STRIP + lay::SP6;
			auto row = [&](const Glyph* glyph, bool liked, const char* name, const char* note,
				bool playing, bool focused, bool off) {
				Kit::Playlist p;
				p.glyph = glyph;
				p.liked = liked;
				p.name = name;
				p.note = note;
				p.playing = playing;
				p.focused = focused;
				p.off = off;
				k.playlistRow(lay::playlistRow({16, y, 608, lay::PLAYLIST_ROW_H}), p);
				y += lay::PLAYLIST_ROW_H;
			};
			row(nullptr, true, "Liked", "42 items", false, false, false);
			row(&G_PLAYLIST, false, "Late night", "9 items", true, true, false);
			row(&G_PLAYLIST, false, "Audiobooks", "3 items", false, false, false);
			row(&G_PLAYLIST, false, "Road trip", "18 items \xC2\xB7 4 videos", false, false, false);
			row(&G_PLAYLIST, false, "Workout", "27 items", false, false, false);
			row(&G_PLUS, false, "New playlist", "", false, false, true);
			k.hints({{"A", "Open"}, {"X", "More"}, {"L/R", "View"}, {"L3", "Search"}});
		}

		// the library's empty states (screens.js music-empty, videos-empty): the how-to names the folders — the
		// folder's word in the accent — and Browse Files and the Web dashboard are the two keys
		void page18(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			Kit::Empty m;
			m.glyph = &G_NOTE;
			m.title = "No music here yet";
			m.lines = {"Put your music in a folder named Music on either",
				"card. Musics counts too, in any capitals. Albums",
				"and artists are made from the songs' tags."};
			m.accent = {{0, 33, 5}, {1, 6, 6}};
			m.key1 = "Browse Files";
			m.glyph1 = &G_FOLDER;
			m.key2 = "Web dashboard";
			m.glyph2 = &G_QR;
			k.emptyState(lay::emptyState({16, 2, 608, 238}, 3, 2), m);
			Kit::Empty v;
			v.glyph = &G_FILM;
			v.title = "No videos here yet";
			v.lines = {"Put your videos in a folder named Videos or",
				"Movies on either card. They are listed here by",
				"name, with your place in each kept."};
			v.accent = {{0, 34, 6}, {1, 0, 6}};
			v.key1 = "Browse Files";
			v.glyph1 = &G_FOLDER;
			v.key2 = "Web dashboard";
			v.glyph2 = &G_QR;
			k.emptyState(lay::emptyState({16, 240, 608, 240}, 3, 2), v);
		}

		// the library's first-read states (screens.js music-reading, videos-reading): the Notice row with its
		// bar over the list that is filling in (a few albums, two videos: what is already there)
		void page19(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			Kit::NoticeRow m;
			m.glyph = &G_REFRESH;
			m.text = "Reading Music on the second card";
			m.second = "First time on this card \xC2\xB7 1,180 of about 1,900 songs \xC2\xB7 albums fill in as it goes";
			m.share = 0.62;
			k.notice(lay::notice({16, 4, 608, 0}, true, true, 0), m);
			Kit::SectionLine line;
			line.text = "From Music on both cards \xC2\xB7 41 albums so far";
			k.sectionLine(lay::sectionLine({16, 60, 608, lay::LINE_H}, false), line);
			lay::AlbumGrid grid = lay::albumGrid({16, 90, 608, 134}, 5);
			const char* albums[5][2] = {{"Yellow Album", "The Plantains"}, {"Late Night", "The Carrots"},
				{"Pixel Sea", "Harbor Cats"}, {"Steel", "Tin Roof"}, {"Grape", "The Plantains"}};
			for (int i = 0; i < 5; i++)
			{
				Kit::Album a;
				a.artKey = albums[i][0];
				a.title = albums[i][0];
				a.artist = albums[i][1];
				a.focused = i == 0;
				k.albumTile(lay::albumTile(grid.cell[i]), a);
			}
			Kit::NoticeRow v;
			v.glyph = &G_REFRESH;
			v.text = "Reading Videos on the second card";
			v.second = "First time on this card \xC2\xB7 4 of 9 folders \xC2\xB7 the list fills in as it goes";
			v.share = 0.44;
			k.notice(lay::notice({16, 244, 608, 0}, true, true, 0), v);
			// the rows already there: their lengths are blank until the length cache has them
			Kit::Video r;
			r.name = "Big Buck Bunny.mkv";
			r.second = "Movies \xC2\xB7 2.1 GB \xC2\xB7 12 Nov";
			r.focused = true;
			k.videoRow(lay::videoRow({16, 306, 608, lay::VIDEO_ROW_H}), r);
			r = Kit::Video();
			r.name = "Caminandes 1.mkv";
			r.second = "Shorts \xC2\xB7 148 MB \xC2\xB7 3 Nov";
			k.videoRow(lay::videoRow({16, 372, 608, lay::VIDEO_ROW_H}), r);
			k.hints({{"A", "Open"}, {"X", "More"}, {"L/R", "View"}, {"L3", "Search"}});
		}

		// Match album art's backdrop (screens.js dyn-art): a synthetic art through artBackdrop to a page,
		// drawn by Kit::backdrop in a dark and a light theme, and both fallbacks (a grey art gives no hue)
		void page20(Kit& k)
		{
			if (!artMade)
			{
				artMade = true;
				// two generated pictures (as every shot must be): a warm radial with a hue, and a grey one
				std::vector<uint8_t> px(200 * 150 * 4);
				for (int y = 0; y < 150; y++)
					for (int x = 0; x < 200; x++)
					{
						uint8_t* p = px.data() + ((size_t)y * 200 + x) * 4;
						double dx = (x - 64) / 150.0, dy = (y - 45) / 150.0, r = std::sqrt(dx * dx + dy * dy);
						auto at = [&](double a, double b, double v0, double v1) {
							return v0 + (v1 - v0) * std::clamp((r - a) / (b - a), 0.0, 1.0);
						};
						p[0] = (uint8_t)(r < 0.42 ? at(0, 0.42, 255, 232) : at(0.42, 0.82, 232, 58));
						p[1] = (uint8_t)(r < 0.42 ? at(0, 0.42, 179, 69) : at(0.42, 0.82, 69, 13));
						p[2] = (uint8_t)(r < 0.42 ? at(0, 0.42, 71, 44) : at(0.42, 0.82, 44, 18));
						p[3] = 255;
					}
				artbg::Art emberArt = artbg::artBackdrop(px.data(), 200, 150);
				for (int y = 0; y < 150; y++)
					for (int x = 0; x < 200; x++)
					{
						uint8_t* p = px.data() + ((size_t)y * 200 + x) * 4;
						unsigned char g = ((x + y) / 18) % 2 ? 0x9a : 0x6f;
						p[0] = p[1] = p[2] = g;
						p[3] = 255;
					}
				artbg::Art greyArt = artbg::artBackdrop(px.data(), 200, 150);
				// the dark theme's colours from the theme in force; the light ones from the Light palette, so one
				// page shows both branches
				const artbg::ThemeColours dark{theme::current.BG, theme::current.SURFACE, theme::current.TEXT,
					theme::current.DIM_ON, theme::current.ACCENT};
				const artbg::ThemeColours light{{0xF7, 0xF4, 0xEC}, {0xFF, 0xFD, 0xF8}, {0x1B, 0x1A, 0x20},
					{0x62, 0x5E, 0x56}, {0x7A, 0x52, 0x00}};
				artPages[0] = k.backdropPage(emberArt, artbg::backdropFor(dark, emberArt));
				artPages[1] = k.backdropPage(emberArt, artbg::backdropFor(light, emberArt));
				artPages[2] = k.backdropPage(greyArt, artbg::backdropFor(dark, greyArt));
				artPages[3] = k.backdropPage(greyArt, artbg::backdropFor(light, greyArt));
			}
			k.backdrop({0, 0, 320, 240}, artPages[0]);
			k.backdrop({320, 0, 320, 240}, artPages[1]);
			k.backdrop({0, 240, 320, 240}, artPages[2]);
			k.backdrop({320, 240, 320, 240}, artPages[3]);
			// the labels where the veil is at its deepest, so they read; the light quadrants take the light
			// theme's ink (what its own text would be)
			const theme::Rgb lightInk{0x1B, 0x1A, 0x20};
			ui.drawTextMid("Dark \xC2\xB7 the art's hue", F_I12, TEXT, 8, 14, 200);
			ui.drawTextMid("Light \xC2\xB7 the art's hue", F_I12, sdl(lightInk), 328, 14, 200);
			ui.drawTextMid("Dark \xC2\xB7 the fallback", F_I12, TEXT, 8, 254, 200);
			ui.drawTextMid("Light \xC2\xB7 the fallback", F_I12, sdl(lightInk), 328, 254, 200);
		}

		// a tab page (lay::tabPage; screens.js manga): the tabs stepped by L2 / R2 over a body, in the shell's body,
		// then the same strip with L / R caps as a sheet draws it
		void page21(Kit& k)
		{
			ui.fill({0, 0, W, H}, BG);
			std::vector<std::string> labels = {"Library", "History", "Browse", "Downloads 2"};
			std::vector<int> widths;
			for (auto& l : labels) widths.push_back(k.tabW(l));
			lay::TabPage tp = lay::tabPage(lay::shell().body, widths, k.tabCapW("L/R"));
			k.tabs(tp.strip.area, labels, 0, "L/R", BG);
			ui.fill({tp.content.x, tp.content.y, tp.content.w, 1}, RULE);
			ui.drawTextMid("content", F_I12, DIM, tp.content.x + 12, tp.content.y + 16, 200);
			k.tabs({16, 300, 608, 54}, {"Up next", "Chapters"}, 1, "L/R", BG, 0);
			k.hints({{"A", "Open"}, {"X", "More"}, {"L/R", "Tab"}, {"L2/R2", "Sections"}});
		}

		void draw(const Player&) override
		{
			Kit& k = *ui.kit;
			switch (page)
			{
			case 21: page21(k); break;
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
			case 15: page15(k); break;
			case 16: page16(k); break;
			case 17: page17(k); break;
			case 18: page18(k); break;
			case 19: page19(k); break;
			case 20: page20(k); break;
			default: page1(k); break;
			}
		}
	};

	void Ui::openVariantsGallery(int page)
	{
		pushOverlay(std::make_unique<VariantsGallery>(*this, page));
	}
}
