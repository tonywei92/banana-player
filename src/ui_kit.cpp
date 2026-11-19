// The redesign's component kit: each piece drawn once (ui_kit.h says what each is; docs/redesign/components.md its
// anatomy). Nothing here keeps state: a component draws what it is given, where it is given.
#include "ui_kit.h"
#include "artph.h"
#include <cmath>

namespace bp
{
	namespace
	{
		const int CAP_H = 19, CAP_GAP = 3;
		// a D-pad direction's arrow in a cap (none: the name is written)
		const Ui::Glyph* capArrow(const std::string& key)
		{
			return key == "LEFT" ? &G_CAP_LEFT : key == "RIGHT" ? &G_CAP_RIGHT : key == "UP" ? &G_CAP_UP : key == "DOWN" ? &G_CAP_DOWN : nullptr;
		}
		std::vector<std::string> capNames(const std::string& keys)
		{
			std::vector<std::string> out;
			size_t from = 0;
			while (from <= keys.size())
			{
				size_t at = keys.find('/', from);
				if (at == std::string::npos) at = keys.size();
				if (at > from) out.push_back(keys.substr(from, at - from));
				from = at + 1;
			}
			return out;
		}
		theme::Rgb rgb(SDL_Color c) { return {c.r, c.g, c.b}; }
	}

	int Ui::Kit::textW(const std::string& s, int font) const
	{
		return s.empty() ? 0 : (int)std::lround(ui.textWidth(s, font) / sx);
	}

	SDL_Color Ui::Kit::off(SDL_Color c, bool isOff)
	{
		return isOff ? sdl(theme::mix(theme::BG, rgb(c), theme::OFF), c.a) : c;
	}

	SDL_Color Ui::Kit::edgeOf(SDL_Color fill) { return sdl(theme::edgeOf(rgb(fill))); }

	void Ui::Kit::artPlaceholder(Rect r, const std::string& key, int rad, bool edge)
	{
		// the sums are a square's: in a wide box (Home's Continue and Recent cards) the square of its height, centred
		const int side = std::min(r.w, r.h);
		const artph::Shape a = artph::shape(side, key);
		const int ox = (r.w - side) / 2, oy = (r.h - side) / 2;
		if (edge) flat(r, BUTTON, rad, edgeOf(BUTTON));
		else ui.fillRound(r, BUTTON, rad);
		// the discs reach past the square (half out): cut to it, inside its edge when it has one (else a disc at the
		// foot paints over the edge and the box looks cut off)
		const int in = edge ? theme::EDGE : 0;
		SDL_Rect old, d = ui.devRect({r.x + in, r.y + in, r.w - 2 * in, r.h - 2 * in});
		const bool had = SDL_RenderIsClipEnabled(ui.ren);
		if (had)
		{
			SDL_RenderGetClipRect(ui.ren, &old);
			if (!SDL_IntersectRect(&d, &old, &d)) return;
		}
		SDL_RenderSetClipRect(ui.ren, &d);
		auto disc = [&](int dia, SDL_Color c) { ui.fillRound({r.x + ox + a.cx - dia / 2, r.y + oy + a.cy - dia / 2, dia, dia}, c, dia / 2); };
		disc(a.record, BG);
		if (a.band) disc(a.band, SURFACE);
		disc(a.label, ACCENT);
		disc(a.hole, BG);
		SDL_RenderSetClipRect(ui.ren, had ? &old : nullptr);
	}

	void Ui::Kit::flat(Rect r, SDL_Color fill, int rad, SDL_Color edge)
	{
		// the edge as the whole box, the fill inside it: no lip (a surface, not a button)
		ui.fillRound(r, edge, rad);
		ui.fillRound({r.x + theme::EDGE, r.y + theme::EDGE, r.w - 2 * theme::EDGE, r.h - 2 * theme::EDGE}, fill, rad - theme::EDGE);
	}

	// ---- key caps and the hint bar ----

	int Ui::Kit::capsW(const std::string& keys) const
	{
		int w = 0;
		for (auto& k : capNames(keys))
			w += (w ? CAP_GAP : 0) + (capArrow(k) ? CAP_H : std::max(CAP_H, textW(k, F_P12) + 10));
		return w;
	}

	int Ui::Kit::caps(const std::string& keys, int x, int cy, SDL_Color ink, SDL_Color ground)
	{
		int x0 = x;
		for (auto& k : capNames(keys))
		{
			const Glyph* arrow = capArrow(k);
			int w = arrow ? CAP_H : std::max(CAP_H, textW(k, F_P12) + 10);
			Rect r{x, cy - CAP_H / 2, w, CAP_H};
			flat(r, ground, theme::R_CAP, ink);
			if (arrow) ui.drawGlyphMid(*arrow, r.x + r.w / 2, cy, ink, 1);
			else ui.drawTextMid(k, F_P12, ink, r.x + r.w / 2, cy, 0, 2);
			x += w + CAP_GAP;
		}
		return x > x0 ? x - CAP_GAP - x0 : 0;
	}

	void Ui::Kit::hints(const std::vector<Hint>& list)
	{
		hints({0, H - lay::HINTS_H, W, lay::HINTS_H}, list);
	}

	void Ui::Kit::hints(Rect bar, const std::vector<Hint>& list)
	{
		// a rule above; each item its caps then its word, the row centred in the box it was given
		const theme::Palette& th = theme::current;
		SDL_Color ground = sdl(th.HINT_BAR), word = sdl(th.ON_HINT), capInk = th.strongKeys ? word : ACCENT;
		ui.fill(bar, ground);
		if (!th.strongKeys) ui.fill({bar.x, bar.y, bar.w, 1}, RULE);
		std::vector<int> widths;
		for (auto& h : list) widths.push_back(capsW(h.key) + (h.key.empty() || h.verb.empty() ? 0 : 5) + textW(h.verb, F_I12));
		int left = bar.x + lay::SP8, right = bar.x + bar.w - lay::SP8;
		auto xs = lay::spread(widths, lay::SP16, left, right);
		int cy = bar.y + bar.h / 2 + 1;
		for (size_t i = 0; i < list.size(); i++)
		{
			int x = xs[i];
			if (!list[i].key.empty()) x += caps(list[i].key, x, cy, capInk, ground) + 5;
			ui.drawTextMid(list[i].verb, F_I12, word, x, cy, std::max(1, right - x));
		}
	}

	void Ui::Kit::brandMark(int x, int y, int size)
	{
		// the spec's 22 grid, scaled by size / 22 (whole units at 22)
		const double u = size / 22.0;
		auto X = [&](double v) { return x + (int)std::lround(v * u); };
		auto Y = [&](double v) { return y + (int)std::lround(v * u); };
		auto S = [&](double v) { return std::max(1, (int)std::lround(v * u)); };
		// the body, its 1-unit edgeOf(SURFACE) edge inside
		ui.fillRound({x, y, size, size}, edgeOf(SURFACE), S(5));
		int e = S(1);
		ui.fillRound({x + e, y + e, size - 2 * e, size - 2 * e}, BUTTON, std::max(0, S(5) - e));
		// the clapper bar: BG with four ACCENT stripes leaning left to right
		ui.fillRound({X(3), Y(4), S(16), S(4)}, BG, S(1));
		for (int i = 0; i < 4; i++)
		{
			int top = 5 + i * 4;
			ui.fill({X(top), Y(4), S(2), S(2)}, ACCENT);
			ui.fill({X(top - 1), Y(6), S(2), S(2)}, ACCENT);
		}
		// the slate, the play triangle and the progress bar
		ui.fillRound({X(3), Y(9), S(16), S(10)}, SURFACE, S(1.5));
		for (int i = 0; i < 4; i++) ui.fill({X(9), Y(10.5 + i), S(4.5 - i * 1.1), S(1.2)}, ACCENT);
		ui.fill({X(5), Y(16), S(12), S(1)}, DIM);
		ui.fill({X(5), Y(16), S(6), S(1)}, ACCENT);
		ui.fill({X(10), Y(15.5), S(2), S(2)}, TEXT);
	}

	// ---- the header ----

	void Ui::Kit::header(const lay::PlayerLayout& l, const Header& h)
	{
		if (h.back) iconKey(l.back, G_BACK, h.backPressed, 3);
		if (h.more) iconKey(l.more, G_MORE, h.morePressed);
		const int cy = l.back.y + l.back.h / 2;
		// no More: the right slot reaches the panel's own right margin (the Settings pages, About)
		int right = h.more ? l.more.x - lay::SP8 : l.title.x + l.title.w - lay::SP8;
		if (h.right) right = h.right(right, cy);
		int x = l.back.x + l.back.w + 10;
		if (h.inlineTitle)
		{
			// beside the back key: the title's line, the crumb's under it (the mock-up's 25px h1 and 12px p in the
			// same 54-tall strip). There is no title row under the strip, so the page below starts higher
			int tx = x;
			if (h.mark) { ui.drawGlyphMid(*h.mark, tx + 10, cy - 8, DIM); tx += 26; }
			if (right > tx && !h.title.empty()) ui.drawTextMid(h.title, F_P28, TEXT, tx, cy - 8, right - tx);
			if (right > tx && !h.crumb.empty())
				ui.drawTextMid(h.crumbEnd ? h.crumb : ui.elideLeft(h.crumb, F_I12, right - tx), F_I12, DIM, tx, cy + 12, right - tx);
			return;
		}
		if (h.mark) { ui.drawGlyphMid(*h.mark, x + 10, cy, DIM); x += 26; }
		// (a folder's path keeps its end, a playlist's name its start)
		if (right > x && !h.crumb.empty())
			ui.drawTextMid(h.crumbEnd ? h.crumb : ui.elideLeft(h.crumb, F_I12, right - x), F_I12, DIM, x, cy, right - x);
		// the title, and how it plays on its right
		int titleY = l.title.y + l.title.h / 2, titleRight = l.title.x + l.title.w;
		int stW = h.state.empty() ? 0 : std::min(300, textW(h.state, F_P16));
		if (!h.state.empty()) ui.drawTextMid(h.state, F_P16, h.stateInk, titleRight, titleY, 300, 1);
		if (h.stateMark)
		{
			int gw = ui.glyphW(*h.stateMark);
			ui.drawGlyphMid(*h.stateMark, titleRight - stW - 7 - gw / 2, titleY, h.stateInk);
			stW += 7 + gw;
		}
		if (!h.title.empty())
		{
			// the player's title (a song, a video): the Title marquee; another page's: cut with "…"
			const int room = l.title.w - stW - 14;
			if (h.marquee) ui.drawMarquee(h.titleFor ? h.titleFor(room) : h.title, F_P28, TEXT, l.title.x, titleY, room, BG);
			else ui.drawTextMid(h.title, F_P28, TEXT, l.title.x, titleY, room);
		}
	}

	// ---- keys ----

	void Ui::Kit::key(Rect r, const Key& k)
	{
		const theme::Palette& th = theme::current;
		bool primary = k.tone == Tone::Primary, strong = th.strongKeys;
		// a strong theme's keys: dark fills (a danger key filled DANGER) with ON_KEY lettering, focus a KEY_FOCUS edge
		theme::Rgb base = primary ? th.PRIMARY : strong && k.tone == Tone::Danger ? th.DANGER : th.KEY;
		SDL_Color fill = primary || strong ? sdl(k.pressed ? theme::lipOf(base) : base) : k.pressed ? PRESSED : BUTTON;
		SDL_Color ink = primary ? sdl(th.ON_PRIMARY) : strong ? sdl(th.ON_KEY) : k.tone == Tone::Danger ? DANGER : k.lit ? ACCENT : TEXT;
		SDL_Color capInk = primary ? sdl(th.ON_PRIMARY) : strong ? sdl(th.ON_KEY) : ACCENT;
		SDL_Color edge = k.focused && !k.off ? sdl(th.KEY_FOCUS) : edgeOf(fill);
		ui.drawBox(r, off(fill, k.off), R_BUTTON, off(edge, k.off));
		if (k.glow > 0 && !k.pressed)
		{
			SDL_Color glow = ACCENT;
			glow.a = (Uint8)std::lround(90 * std::min(1.0, k.glow));
			if (glow.a) ui.fillRound({r.x + 2, r.y + 2, r.w - 4, r.h - 6}, glow, R_BUTTON - 2);
		}
		if (k.focused && !k.off && !primary && !strong) ink = k.tone == Tone::Danger ? DANGER : ACCENT;
		ink = off(ink, k.off);
		capInk = off(capInk, k.off);
		SDL_Color ground = off(fill, k.off);
		int cy = r.y + (r.h - theme::LIP) / 2 + 1, cx = r.x + r.w / 2;
		int gw = k.glyph ? ui.glyphW(*k.glyph) : 0;
		if (k.stack)
		{
			// icon over label
			if (k.glyph) ui.drawGlyphMid(*k.glyph, cx, cy - 11, ink);
			ui.drawTextMid(k.label, F_P14, ink, cx, cy + (k.glyph ? 13 : 0), r.w - 12, 2);
			return;
		}
		bool under = r.h >= 60 && !k.key.empty();	// the cap under the label; else beside it
		int capW = k.key.empty() ? 0 : capsW(k.key);
		int room = r.w - lay::SP16 - (gw ? gw + lay::SP8 : 0) - (capW && !under ? capW + lay::SP8 : 0);
		int tw = std::min(std::max(0, room), textW(k.label, F_P16));
		int w = gw + (gw && tw ? lay::SP8 : 0) + tw + (capW && !under ? lay::SP8 + capW : 0);
		int x = cx - w / 2, y = under ? cy - 11 : cy;
		if (k.glyph)
		{
			ui.drawGlyphMid(*k.glyph, x + gw / 2, y, ink);
			x += gw + lay::SP8;
		}
		if (tw) ui.drawTextMid(k.label, F_P16, ink, x, y, tw + 1);
		if (capW && under) caps(k.key, cx - capW / 2, cy + 14, capInk, ground);
		else if (capW) caps(k.key, x + tw + lay::SP8, y, capInk, ground);
	}

	void Ui::Kit::transport(Rect r, const Transport& t)
	{
		const theme::Palette& th = theme::current;
		SDL_Color fill = t.primary ? sdl(t.pressed && t.hold < 0 ? theme::lipOf(th.PRIMARY) : th.PRIMARY) : t.pressed ? PRESSED : BUTTON;
		SDL_Color ink = off(t.primary ? sdl(th.ON_PRIMARY) : TEXT, t.off), capInk = off(t.primary ? sdl(th.ON_PRIMARY) : ACCENT, t.off);
		ui.drawBox(r, off(fill, t.off), R_BUTTON, off(edgeOf(fill), t.off));
		if (t.hold >= 0)
		{
			// the hold, a darker part growing left to right, inside the edge
			Rect in{r.x + theme::EDGE, r.y + theme::EDGE, r.w - 2 * theme::EDGE, r.h - 2 * theme::EDGE};
			int x1 = in.x + (int)std::lround(in.w * std::min(1.0, t.hold));
			if (x1 > in.x) ui.fillRoundBand(in, R_BUTTON - theme::EDGE, in.y, in.y + in.h, sdl(theme::BG, (Uint8)(255 * theme::HOLD_FILL / 100)), x1);
		}
		// icon over label-with-cap (and a third line): the group centred above the lip
		int cell = 2;
		int gh = t.glyph ? t.glyph->h * cell : 0, lineH = 19, thirdH = t.third.empty() ? 0 : 14, gap = t.third.empty() ? 6 : 3;
		int total = gh + (gh ? gap : 0) + lineH + (thirdH ? gap + thirdH : 0);
		int y = r.y + (r.h - theme::LIP - total) / 2 + 1, cx = r.x + r.w / 2;
		if (t.glyph)
		{
			ui.drawGlyphMid(*t.glyph, cx, y + gh / 2, ink, cell);
			y += gh + gap;
		}
		int capW = t.key.empty() ? 0 : capsW(t.key), tw = std::min(r.w - 12 - (capW ? capW + 6 : 0), textW(t.label, F_P14));
		int x = cx - (tw + (capW ? 6 + capW : 0)) / 2;
		ui.drawTextMid(t.label, F_P14, ink, x, y + lineH / 2, tw + 1);
		// (the cap's inside is the key's own fill: under the hold's darker part it stays the plain one, a hair lighter)
		if (capW) caps(t.key, x + tw + 6, y + lineH / 2, capInk, off(fill, t.off));
		if (thirdH) ui.drawTextMid(t.third, F_I12, ink, cx, y + lineH + gap + thirdH / 2, r.w - 10, 2);
	}

	void Ui::Kit::iconKey(Rect r, const Glyph& g, bool pressed, int cell)
	{
		const theme::Palette& th = theme::current;
		SDL_Color fill = th.strongKeys ? sdl(pressed ? theme::lipOf(th.KEY) : th.KEY) : pressed ? PRESSED : BUTTON;
		ui.drawBox(r, fill, R_BUTTON);
		ui.drawGlyphMid(g, r.x + r.w / 2, r.y + (r.h - theme::LIP) / 2 + 1, sdl(th.ON_KEY), cell);
	}

	// ---- state chips and line keys ----

	void Ui::Kit::chip(Rect r, const Chip& c)
	{
		SDL_Color fill = off(c.pressed ? PRESSED : SURFACE, c.off);
		bool focus = c.focused && !c.off;
		// focused: its edge yellow, a unit wider all round so it reads at a glance among five
		if (focus) ui.fillRound({r.x - 1, r.y - 1, r.w + 2, r.h + 2}, ACCENT, R_BUTTON + 1);
		flat(r, fill, R_BUTTON, focus ? ACCENT : off(edgeOf(SURFACE), c.off));
		bool narrow = r.w < 140;
		int pad = narrow ? 7 : 9, gap = narrow ? 6 : 8, cy = r.y + r.h / 2;
		int x = r.x + pad;
		if (c.glyph)
		{
			// every chip's mark in a box of 20, so the texts line up along the row
			ui.drawGlyphMid(*c.glyph, x + 10, cy, off(c.away ? ACCENT : DIM_ON, c.off));
			x += 20 + gap;
		}
		int room = r.x + r.w - pad - x;
		ui.drawTextMid(lay::caps(c.label), F_I11B, off(DIM, c.off), x, cy - 9, room - (c.away ? 6 : 0));
		ui.drawTextMid(c.value, F_P13, off(c.away ? ACCENT : TEXT, c.off), x, cy + 9, room);
		if (c.away && !c.off) ui.fillRound({r.x + r.w - 11, r.y + 5, 6, 6}, ACCENT, 3);
	}

	void Ui::Kit::line(Rect r, const Line& l)
	{
		flat(r, l.pressed ? PRESSED : SURFACE, R_BUTTON, l.focused ? ACCENT : edgeOf(SURFACE));
		int cy = r.y + r.h / 2, x = r.x + (r.h < 46 ? 10 : 12), right = r.x + r.w - (r.h < 46 ? 10 : 12);
		if (l.chevron)
		{
			ui.drawGlyphMid(G_RIGHT, right - 5, cy, DIM);
			right -= 10 + 8;
		}
		if (!l.keys.empty())
		{
			int w = capsW(l.keys);
			caps(l.keys, right - w, cy, ACCENT, l.pressed ? PRESSED : SURFACE);
			right -= w + 8;
		}
		if (!l.note.empty())
		{
			int w = std::min(220, textW(l.note, F_I12));
			ui.drawTextMid(l.note, F_I12, DIM, right, cy, 220, 1);
			right -= w + 10;
		}
		if (!l.label.empty())
		{
			std::string label = lay::caps(l.label);
			ui.drawTextMid(label, F_I11B, DIM, x, cy);
			x += textW(label, F_I11B) + 10;
		}
		if (l.glyph)
		{
			ui.drawGlyphMid(*l.glyph, x + 10, cy, DIM_ON);
			x += 20 + 8;
		}
		ui.drawTextMid(l.text, l.font, l.dim ? DIM : l.focused ? ACCENT : TEXT, x, cy, std::max(1, right - x));
	}

	// ---- bars ----

	void Ui::Kit::progress(Rect track, double share, const std::vector<int>& marks, bool held, const std::string& tag)
	{
		share = std::clamp(share, 0.0, 1.0);
		int w = (int)std::lround(track.w * share);
		ui.fillRound(track, TRACK, theme::R_TRACK);
		if (w > 0) ui.fillRound({track.x, track.y, w, track.h}, ACCENT, theme::R_TRACK, w >= track.w ? ROUND_ALL : ROUND_TL | ROUND_BL);
		// where each chapter starts: a notch through the bar
		for (int x : marks) ui.fill({x - 1, track.y, 2, track.h}, BG);
		Rect knob = lay::barKnob(track, share);
		if (!held)
		{
			ui.fillRound(knob, KNOB, theme::R_GRIP);
			return;
		}
		ui.drawSlideKnob(knob);
		if (!tag.empty()) ui.drawSlideTag(track, share, tag);
	}

	void Ui::Kit::bar(Rect r, double share)
	{
		int w = (int)std::lround(r.w * std::clamp(share, 0.0, 1.0));
		ui.fillRound(r, TRACK, r.h / 2);
		if (w > 0) ui.fillRound({r.x, r.y, w, r.h}, ACCENT, r.h / 2, w >= r.w ? ROUND_ALL : ROUND_TL | ROUND_BL);
	}

	// ---- sheets and rows ----

	void Ui::Kit::sheet(const lay::Sheet& s, const SheetHead& h)
	{
		sheetBack(s.box, s.grab);
		SDL_Color ink = h.danger ? DANGER : ACCENT;
		int cy = s.head.y + s.head.h / 2, x = s.head.x, right = s.head.x + s.head.w;
		if (h.glyph)
		{
			ui.drawGlyphMid(*h.glyph, x + 10, cy, ink);
			x += 20 + 10;
		}
		int noteW = std::min(260, textW(h.note, F_I12));
		if (noteW) ui.drawTextMid(h.note, F_I12, DIM, right, cy, 260, 1);
		ui.drawTextMid(h.title, F_P20, ink, x, cy, std::max(1, right - x - (noteW ? noteW + 12 : 0)));
	}

	void Ui::Kit::sheet(const lay::ListSheet& s, const SheetHead& h)
	{
		lay::Sheet sh;
		sh.box = s.box;
		sh.grab = s.grab;
		sh.head = s.head;
		sh.hints = s.hints;
		sheet(sh, h);
	}

	void Ui::Kit::sheetBack(Rect box, Rect grab)
	{
		// the page behind under a black veil; the sheet from the foot, its top corners round, an edge on three sides
		ui.fill({0, 0, W, H}, sdl(theme::BG, (Uint8)(255 * theme::VEIL_SHEET / 100)));
		ui.fillRound(box, edgeOf(SURFACE), theme::R_SHEET, ROUND_TL | ROUND_TR);
		ui.fillRound({box.x + theme::EDGE, box.y + theme::EDGE, box.w - 2 * theme::EDGE, box.h - theme::EDGE}, SURFACE,
			theme::R_SHEET - theme::EDGE, ROUND_TL | ROUND_TR);
		ui.fillRound(grab, PRESSED, 2);
	}

	// ---- tabs and a segmented choice ----

	std::vector<Ui::Rect> Ui::Kit::tabRects(Rect area, const std::vector<std::string>& labels, bool caps) const
	{
		std::vector<int> widths;
		for (auto& l : labels) widths.push_back(std::max(44, textW(l, F_P14) + 2 * lay::TAB_PAD));
		lay::Tabs t = lay::tabs(area, widths);
		int shift = caps ? 0 : lay::TAB_CAP + lay::TAB_GAP;
		for (auto& b : t.tab) b.x -= shift;
		return t.tab;
	}

	void Ui::Kit::tabs(Rect area, const std::vector<std::string>& labels, int on, const std::string& keys)
	{
		std::vector<int> widths;
		for (auto& l : labels) widths.push_back(std::max(44, textW(l, F_P14) + 2 * lay::TAB_PAD));
		lay::Tabs t = lay::tabs(area, widths);
		int shift = keys.empty() ? lay::TAB_CAP + lay::TAB_GAP : 0;
		for (size_t i = 0; i < t.tab.size(); i++)
		{
			Rect r = t.tab[i];
			r.x -= shift;
			bool at = (int)i == on;
			// (off: a button; on: yellow with its lip, as the mock-up's tab)
			ui.drawBox(r, at ? ACCENT : BUTTON, R_BUTTON);
			ui.drawTextMid(labels[i], F_P14, at ? ON_ACCENT : TEXT, r.x + r.w / 2, r.y + r.h / 2, r.w - 8, 2);
		}
		if (keys.empty()) return;
		// the caps that step it: the first name at the left end, the last at the right
		auto names = capNames(keys);
		int cy = area.y + area.h / 2;
		if (!names.empty()) caps(names.front(), t.left.x, cy, ACCENT, SURFACE);
		if (names.size() > 1) caps(names.back(), t.right.x, cy, ACCENT, SURFACE);
	}

	void Ui::Kit::segment(Rect area, const std::vector<std::string>& options, int on)
	{
		int n = (int)options.size();
		for (int i = 0; i < n; i++)
		{
			Rect r = lay::cell(area, n, 6, i);
			bool at = i == on;
			flat(r, at ? ACCENT : TRACK, 9, at ? edgeOf(ACCENT) : edgeOf(BUTTON));
			ui.drawTextMid(options[i], n >= 3 ? F_P13 : F_P14, at ? ON_ACCENT : DIM, r.x + r.w / 2, r.y + r.h / 2, r.w - 6, 2);
		}
	}

	int Ui::Kit::pillW(const std::string& text) const
	{
		return textW(text, F_I11B) + 12;
	}

	int Ui::Kit::pill(int right, int cy, const std::string& text, Pill tone)
	{
		if (text.empty() || tone == Pill::None) return 0;
		int w = pillW(text);
		Rect r{right - w, cy - 9, w, 17};
		SDL_Color fill = tone == Pill::Live ? ACCENT : tone == Pill::Fact ? TRACK : sdl(theme::mix(theme::BG, theme::DANGER, theme::TINT));
		ui.fillRound(r, fill, theme::R_CAP);
		ui.drawTextMid(text, F_I11B, tone == Pill::Live ? ON_ACCENT : tone == Pill::Fact ? TEXT : DANGER, r.x + r.w / 2, cy, 0, 2);
		return w;
	}

	void Ui::Kit::toggle(int x, int cy, bool on)
	{
		Rect r{x, cy - 11, 40, 22};
		flat(r, on ? ACCENT : TRACK, 11, on ? edgeOf(ACCENT) : edgeOf(BUTTON));
		ui.fillRound({on ? x + 22 : x + 4, cy - 7, 14, 14}, on ? ON_ACCENT : DIM, 7);
	}

	void Ui::Kit::row(Rect r, const Row& row)
	{
		// focused: lit as a button, its edge and its name yellow; held: the pressed fill
		if (row.pressed) ui.fillRound(r, PRESSED, R_BUTTON);
		else if (row.focused) flat(r, BUTTON, R_BUTTON, ACCENT);
		SDL_Color ground = row.pressed ? PRESSED : row.focused ? BUTTON : row.under;
		SDL_Color ink = off(row.danger ? DANGER : row.focused || row.lit ? ACCENT : TEXT, row.off);
		int cy = r.y + r.h / 2, x = r.x + 12, right = r.x + r.w - 12;
		if (row.mark)
		{
			// the extension's picture in its 44 box, the row's left; the name and its line start after it
			Rect m{r.x + 12, cy - 22, 44, 44};
			row.mark(m);
			x = m.x + m.w + 12;
		}
		if (row.tick)
		{
			// a select mode's box, in the place before the row's own lead
			ui.drawGlyphMid(row.tickOn ? G_BOX_ON : G_BOX, x + 11, cy, off(row.tickOn ? ACCENT : DIM, row.off));
			x += 22 + lay::SP12;
		}
		if (row.numberThenIcon && row.glyph)
		{
			// a playlist's row: its place, then what it is, then its name (the mock-up's "3  ♪  Sunny Peel")
			if (row.leadGlyph) ui.drawGlyphMid(*row.leadGlyph, x + 10, cy, off(ACCENT, row.off));
			else ui.drawTextMid(row.lead, F_P14, off(DIM, row.off), x + 10, cy, 20, 2);
			x += 20 + lay::SP8;
			ui.drawGlyphMid(*row.glyph, x + 10, cy, ink);
			x += 20 + lay::SP12;
		}
		else if (row.glyph)
		{
			ui.drawGlyphMid(*row.glyph, x + 10, cy, ink);
			x += 20 + lay::SP12;
		}
		else if (row.radio || !row.lead.empty())
		{
			// a list's place, in the icon's slot ("17"), dim as the mock-up's numbers; a choice's radio, yellow
			// when it is the one in use (the sheet's tracks, sizes and speeds)
			if (row.radio)
				ui.drawGlyphMid(row.radioOn ? G_RADIO_ON : G_RADIO, x + 10, cy,
					off(row.radioOn ? ACCENT : row.focused ? DIM_ON : DIM, row.off));
			else ui.drawTextMid(row.lead, F_P14, off(DIM, row.off), x + 10, cy, 20, 2);
			x += 20 + lay::SP12;
		}
		if (row.trail == Trail::Chevron)
		{
			ui.drawGlyphMid(G_RIGHT, right - 5, cy, off(DIM, row.off));
			right -= 10 + 9;
		}
		else if (row.trail == Trail::SwitchOff || row.trail == Trail::SwitchOn)
		{
			toggle(right - 40, cy, row.trail == Trail::SwitchOn);
			right -= 40 + 9;
		}
		else if (row.trail == Trail::Grip)
		{
			// 30 x 44 at the row's right: a finger's hold, yellow on a lifted row
			ui.drawGlyphMid(G_GRIP, right - 15, cy, off(row.lit || row.focused ? ACCENT : DIM, row.off));
			right -= 30 + 3;
		}
		else if (row.trail == Trail::Heart)
		{
			// a Liked item's heart ends the row; a tap on it unlikes (the row's touch decides)
			ui.drawGlyphMid(G_HEART_S, right - 9, cy, off(ACCENT, row.off));
			right -= 18 + 9;
		}
		if (!row.key.empty())
		{
			int w = capsW(row.key);
			caps(row.key, right - w, cy, off(ACCENT, row.off), ground);
			right -= w + 9;
		}
		int noteW = 0;
		if (!row.note.empty())
		{
			noteW = std::min(r.w / 2, textW(row.note, F_I12));
			ui.drawTextMid(row.note, F_I12, off(row.focused ? DIM_ON : DIM, row.off), right, cy, r.w / 2, 1);
		}
		if (row.heart)
		{
			// the note's left, as the mock-up's liked row shows it
			ui.drawGlyphMid(G_HEART_S, right - noteW - 10, cy, off(ACCENT, row.off));
			right -= noteW + 10 + 9;
		}
		else if (noteW) right -= noteW + 9;
		if (!row.pill.empty()) right -= pill(right, cy, row.pill, row.tone) + 9;
		// the focused row's name: the Title marquee, one pass (the cursor rested 1.5 s first: its rest)
		int nameY = row.second.empty() ? cy : cy - 8;
		if (row.focused && !row.off) ui.drawMarquee(row.name, row.font, ink, x, nameY, std::max(1, right - x), BUTTON, 1);
		else ui.drawTextMid(row.name, row.font, ink, x, nameY, std::max(1, right - x));
		if (!row.second.empty())
			ui.drawTextMid(row.second, F_I12, off(row.focused ? DIM_ON : DIM, row.off), x, cy + 9, std::max(1, right - x));
	}

	// ---- the Settings page's pieces ----

	void Ui::Kit::settingRow(Rect r, const SettingRow& s)
	{
		// focused: a surface with a yellow edge and the label and value yellow; held: the pressed fill
		if (s.pressed) ui.fillRound(r, PRESSED, R_BUTTON);
		else if (s.focused) flat(r, BUTTON, R_BUTTON, ACCENT);
		SDL_Color ink = off(s.focused ? ACCENT : TEXT, s.off);
		int cy = r.y + r.h / 2;
		// the stepper's own places, shared with the page's taps (lay::settingStepper): the value's box is centred
		// between the arrows, so it stays centred whatever the row's width right of the rail
		lay::SettingStepper st = lay::settingStepper(r);
		ui.drawTextMid(s.label, F_P16, ink, st.label.x, cy, st.label.w);
		if (s.action)
		{
			// a note and a chevron: nothing to step
			int right = r.x + r.w - 16;
			ui.drawGlyphMid(G_RIGHT, right - 5, cy, off(s.focused ? ACCENT : DIM, s.off));
			ui.drawTextMid(s.value, F_I12, off(s.focused ? DIM_ON : DIM, s.off), right - 18, cy, r.w / 2, 1);
			return;
		}
		SDL_Color arrow = off(s.focused ? ACCENT : DIM, s.off);
		ui.drawGlyphMid(G_LEFT, st.prevX, cy, arrow);
		ui.drawTextMid(s.value, F_P16, ink, st.value.x + st.value.w / 2, cy, st.value.w, 2);
		ui.drawGlyphMid(G_RIGHT, st.nextX, cy, arrow);
	}

	void Ui::Kit::settingTile(Rect r, const SettingTile& t)
	{
		// the mock-up's Settings tile: icon, name, and what it is set to now (a dim line under the name)
		SDL_Color fill = t.pressed ? PRESSED : t.focused ? BUTTON : SURFACE;
		if (t.focused) flat(r, fill, R_TILE, ACCENT);
		else flat(r, fill, R_TILE, edgeOf(SURFACE));
		SDL_Color ink = t.focused ? ACCENT : TEXT;
		if (t.glyph) ui.drawGlyphMid(*t.glyph, r.x + 28, r.y + r.h / 2, ink);
		ui.drawTextMid(t.name, F_P16, ink, r.x + 52, r.y + 22, r.w - 64);
		if (!t.summary.empty()) ui.drawTextMid(t.summary, F_I12, t.focused ? DIM_ON : DIM, r.x + 52, r.y + 42, r.w - 64);
	}

	// ---- the lock over a screen ----

	void Ui::Kit::lock(Rect key, double held)
	{
		// the controls under a black veil; one large key to hold, and what it is, said once
		ui.fill({0, 0, W, H}, sdl(theme::BG, (Uint8)(255 * theme::VEIL_LOCK / 100)));
		ui.drawBox(key, held > 0 ? PRESSED : BUTTON, theme::R_LOCK, ACCENT);
		ui.drawGlyphMid(G_LOCK, key.x + key.w / 2, key.y + key.h / 2 - 4, ACCENT, 6);
		// the hold, filling along the key's foot
		if (held > 0) ui.fillRound({key.x + 18, key.y + key.h - 20, (int)std::lround((key.w - 36) * std::min(1.0, held)), 6}, ACCENT, 3);
		int cx = key.x + key.w / 2;
		// (its two lines on a plate of their own: the veil lets the keys under them show through)
		ui.fillRound({cx - 250, key.y + key.h + 8, 500, 64}, BG, R_TILE);
		ui.drawTextMid(held > 0 ? "Keep holding..." : "Touch is locked", F_P20, ACCENT, cx, key.y + key.h + 26, W - 32, 2);
		ui.drawTextMid("Hold the lock 1 s to unlock \xC2\xB7 the buttons still work", F_I14, DIM_ON, cx, key.y + key.h + 54, W - 32, 2);
	}
}
