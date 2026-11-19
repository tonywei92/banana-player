#include "levelcard.h"
#include "cardcanvas.h"
#include "theme.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace bp
{
	// the TOP_* tokens only: a card stands over a picture or in its place, dark in every theme (theme.h)
	using namespace theme;

	// the cards' body is see-through (the owner, 2026-11-19: the picture shows through every note, level and chapter
	// card; it was 223, barely so). The text keeps its contrast: TOP_TEXT on TOP_CARD is high, and the card darkens
	// what is under it by two thirds
	static const int CARD_ALPHA = 165;

	int levelTop(const std::string& label, int volumeMax)
	{
		return label == "Volume" ? std::max(100, volumeMax) : 100;
	}

	void levelCardPos(int pw, int ph, const LevelCardImage& card, int& x, int& y)
	{
		int m = (int)std::lround(20 * ph / 480.0);
		x = pw - m - card.w;
		y = m;
	}

	void noteCardPos(int pw, int ph, const LevelCardImage& card, int& x, int& y, bool centre)
	{
		x = (pw - card.w) / 2;
		y = centre ? (ph - card.h) / 2 : ph - (int)std::lround(26 * ph / 480.0) - card.h;
	}

	namespace
	{
		using namespace card;

		LevelCardImage finish(Canvas& c)
		{
			LevelCardImage out;
			out.w = c.W;
			out.h = c.H;
			out.rgba = std::make_shared<std::vector<uint8_t>>(std::move(c.v));
			return out;
		}

		const float NAME_SIZE = 19, SMALL = 12;
	}

	LevelCardImage drawLevelCard(const std::string& fontsDir, int panelH, const std::string& label, int percent, int top, CardGlyph icon)
	{
		Canvas c(fontsDir, panelH);
		const bool bar = top > 0;
		if (bar) percent = std::clamp(percent, 0, top);
		const bool volume = label == "Volume" && top > 100;
		// the tone: cream at the usual levels, yellow boosted past 100, red past 120 (the hearing warning)
		Rgb tone = !bar || percent <= 100 ? TOP_TEXT : percent > 120 ? TOP_DANGER : TOP_TITLE;
		std::string value = percent >= 0 ? std::to_string(percent) + "%" : "";
		std::string third;
		if (volume)
			third = percent > 120 ? "Very loud: can harm hearing and the speakers"
				: percent > 100 ? "Boosted \xc2\xb7 Volume \xe2\x88\x92 takes it off first" : "";	// (up to 100: no line, the owner)
		const int padX = c.px(18), padY = c.px(14), w = c.px(bar ? 290 : 200);
		const int line1 = std::max(c.lineH(PIXEL, NAME_SIZE), c.px(20)), line3 = c.lineH(INTER, SMALL);
		int h = padY * 2 + line1 + (bar ? c.px(12) + c.px(8) : 0) + (!third.empty() ? c.px(9) + line3 : 0);
		c.size(w, h);
		c.box(0, 0, (float)w, (float)h, (float)c.px(R_TOP_CARD), TOP_CARD, CARD_ALPHA);
		int x = padX;
		if (icon.rows)
		{
			c.glyph(icon, (float)c.px(20), (float)x, padY + (line1 - c.px(20)) / 2.f, tone);
			x += c.px(20) + c.px(10);
		}
		int ty = padY + (line1 - c.lineH(PIXEL, NAME_SIZE)) / 2;
		int vw = c.textW(PIXEL, NAME_SIZE, value);
		c.text(PIXEL, NAME_SIZE, c.fit(PIXEL, NAME_SIZE, label, w - padX - x - vw - c.px(10)), x, ty, tone);
		c.text(PIXEL, NAME_SIZE, value, w - padX - vw, ty, tone);
		if (bar)
		{
			float bx = (float)padX, bw = (float)(w - 2 * padX), by = (float)(padY + line1 + c.px(12)), bh = (float)c.px(8),
				r = (float)c.px(R_TOP_BAR);
			c.box(bx, by, bw, bh, r, TOP_TRACK);
			float fill = bw * percent / top, at = bw * 100 / top;
			if (top > 100 && percent > 100)
			{
				// up to the mark in cream, the part past it yellow, red past 120
				float red = bw * 120 / top;
				c.box(bx, by, at, bh, r, TOP_TEXT);
				c.box(bx + at, by, std::min(fill, red) - at, bh, 0, TOP_TITLE);
				if (percent > 120) c.box(bx + red, by, fill - red, bh, 0, TOP_DANGER);
			}
			else if (fill > 0) c.box(bx, by, fill, bh, r, TOP_TEXT);
			if (top > 100) c.box(bx + at - c.px(1), by - c.px(5), (float)c.px(2), bh + c.px(10), 0, TOP_TEXT);
		}
		if (!third.empty())
		{
			// (the warning is the longest: smaller to fit the card rather than cut)
			float sz = SMALL;
			while (sz > 9 && c.textW(INTER, sz, third) > w - 2 * padX) sz -= 0.5f;
			c.text(INTER, sz, third, padX, h - padY - line3, percent > 100 ? tone : TOP_SUB);
		}
		return finish(c);
	}

	LevelCardImage drawLabelCard(const std::string& fontsDir, int panelH, const std::string& label, const std::string& value,
		const std::vector<float>& starts, int active)
	{
		Canvas c(fontsDir, panelH);
		const int padX = c.px(18), padY = c.px(14), w = c.px(290);
		const int l1 = c.lineH(INTER, SMALL), l2 = c.lineH(PIXEL, NAME_SIZE);
		const bool bar = !starts.empty();
		int h = padY * 2 + l1 + c.px(4) + l2 + (bar ? c.px(12) + c.px(6) : 0);
		c.size(w, h);
		c.box(0, 0, (float)w, (float)h, (float)c.px(R_TOP_CARD), TOP_CARD, CARD_ALPHA);
		c.text(INTER, SMALL, c.fit(INTER, SMALL, label, w - 2 * padX), padX, padY, TOP_SUB);
		c.text(PIXEL, NAME_SIZE, c.fit(PIXEL, NAME_SIZE, value, w - 2 * padX), padX, padY + l1 + c.px(4), TOP_TITLE);
		if (bar)
		{
			// a segment a chapter by its start with a gap at each boundary, the shown one yellow
			float bx = (float)padX, bw = (float)(w - 2 * padX), by = (float)(h - padY - c.px(6)), bh = (float)c.px(6);
			int n = (int)starts.size();
			for (int i = 0; i < n; i++)
			{
				float x0 = bx + bw * std::clamp(starts[i], 0.f, 1.f), x1 = i + 1 < n ? bx + bw * std::clamp(starts[i + 1], 0.f, 1.f) : bx + bw;
				if (i + 1 < n && x1 - x0 >= c.px(4)) x1 -= c.px(2);
				c.box(x0, by, std::max(1.f, x1 - x0), bh, 0, i == active ? TOP_TITLE : TOP_TRACK);
			}
		}
		return finish(c);
	}

	LevelCardImage drawNoteCard(const std::string& fontsDir, int panelH, CardGlyph icon, const std::string& line1,
		const std::string& line2, bool danger)
	{
		Canvas c(fontsDir, panelH);
		Rgb tone = danger ? TOP_DANGER : TOP_TITLE;
		const int padX = c.px(18), padY = c.px(12), maxW = c.px(560);
		const int ico = icon.rows ? c.px(20) + c.px(10) : 0;
		std::string a = c.fit(PIXEL, NAME_SIZE, line1, maxW - 2 * padX - ico);
		std::string b = c.fit(INTER, 13, line2, maxW - 2 * padX, true);
		const int h1 = std::max(c.lineH(PIXEL, NAME_SIZE), c.px(20)), h2 = c.lineH(INTER, 13);
		int rowW = ico + c.textW(PIXEL, NAME_SIZE, a);
		int w = std::max(std::max(rowW, c.textW(INTER, 13, b)) + 2 * padX, c.px(120)), h = padY * 2 + h1 + (b.empty() ? 0 : c.px(6) + h2);
		c.size(w, h);
		c.box(0, 0, (float)w, (float)h, (float)c.px(R_TOP_CARD), TOP_CARD, CARD_ALPHA);
		// (the first line and its icon centred on the card)
		int x = (w - rowW) / 2;
		if (icon.rows) c.glyph(icon, (float)c.px(20), (float)x, padY + (h1 - c.px(20)) / 2.f, tone);
		c.text(PIXEL, NAME_SIZE, a, x + ico, padY + (h1 - c.lineH(PIXEL, NAME_SIZE)) / 2, tone);
		if (!b.empty()) c.text(INTER, 13, b, (w - c.textW(INTER, 13, b)) / 2, padY + h1 + c.px(6), TOP_SUB);
		return finish(c);
	}

	LevelCardImage drawStateCard(const std::string& fontsDir, int panelW, int panelH, const StateCardSpec& s)
	{
		Canvas c(fontsDir, panelH);
		if (panelW <= 0) panelW = c.px(640);
		if (panelH <= 0) panelH = c.px(480);
		const Rgb tone = s.danger ? TOP_DANGER : TOP_TITLE;
		const int tile = c.px(72), side = c.px(40), colW = panelW - 2 * side;
		std::string title = c.fit(PIXEL, 29, s.title, colW), line = c.fit(INTER, 16, s.line, colW, true),
			hint = c.fit(INTER, 14, s.hint, colW);
		const std::string& time = s.timeLine;
		const int hT = c.lineH(PIXEL, 29), hL = c.lineH(INTER, 16), hTi = c.lineH(INTER, 13), hH = std::max(c.lineH(INTER, 14), c.px(22));
		const int cap = s.key.empty() ? 0 : c.px(22) + c.px(8);
		// the column's height, tile to hint
		int colH = tile + c.px(16) + hT + (line.empty() ? 0 : c.px(9) + hL) +
			(s.share >= 0 ? c.px(16) + c.px(6) + (time.empty() ? 0 : c.px(8) + hTi) : 0) + (hint.empty() ? 0 : c.px(18) + hH);
		int top = s.full ? c.px(104) : c.px(18);
		int w = panelW, h = s.full ? panelH : colH + 2 * top;
		if (!s.full) w = std::min(panelW, std::max(tile, std::max(c.textW(PIXEL, 29, title), c.textW(INTER, 16, line))) + 2 * c.px(40));
		c.size(w, h);
		if (s.full) c.box(0, 0, (float)w, (float)h, 0, TOP_CARD);	// (opaque: the page's picture under it isn't the card's)
		else c.box(0, 0, (float)w, (float)h, (float)c.px(R_TOP_CARD), TOP_CARD, 240);
		float cx = w / 2.f, y = (float)top;
		c.ring(cx - tile / 2.f, y, (float)tile, (float)tile, (float)c.px(18), (float)c.px(2), TOP_TRACK, TOP_SQUARE);
		if (s.spinner >= 0)
		{
			// eight dots round the tile's middle, one lit going round, the ones behind it fading
			for (int i = 0; i < 8; i++)
			{
				double a = i * M_PI / 4;
				float r = (float)c.px(18), d = (float)c.px(7);
				float dx = cx + (float)std::sin(a) * r, dy = y + tile / 2.f - (float)std::cos(a) * r;
				int age = (s.spinner % 8 - i + 8) % 8;
				int alpha = age == 0 ? 255 : age == 1 ? 190 : age == 2 ? 125 : 70;
				c.box(dx - d / 2, dy - d / 2, d, d, d / 2, tone, alpha);
			}
		}
		else
		{
			float is = (float)c.px(s.danger ? 30 : 34);
			c.glyph(s.icon, is, cx - is / 2, y + (tile - is) / 2, tone);
		}
		y += tile + c.px(16);
		c.text(PIXEL, 29, title, (int)(cx - c.textW(PIXEL, 29, title) / 2.f), (int)y, tone);
		y += hT;
		if (!line.empty())
		{
			y += c.px(9);
			c.text(INTER, 16, line, (int)(cx - c.textW(INTER, 16, line) / 2.f), (int)y, TOP_TEXT);
			y += hL;
		}
		if (s.share >= 0)
		{
			y += c.px(16);
			float bw = (float)c.px(300), bh = (float)c.px(6);
			c.box(cx - bw / 2, y, bw, bh, bh / 2, TOP_TRACK);
			if (s.share > 0) c.box(cx - bw / 2, y, std::max(bh, bw * std::min(1.f, s.share)), bh, bh / 2, TOP_TITLE);
			y += bh;
			if (!time.empty())
			{
				y += c.px(8);
				c.text(INTER, 13, time, (int)(cx - c.textW(INTER, 13, time) / 2.f), (int)y, TOP_SUB);
				y += hTi;
			}
		}
		if (!hint.empty())
		{
			y += c.px(18);
			int x = (int)(cx - (c.textW(INTER, 14, hint) + cap) / 2.f);
			if (!s.key.empty())
			{
				// the key's cap: a small square with its letter
				float cs = (float)c.px(22);
				c.ring((float)x, y + (hH - cs) / 2, cs, cs, (float)c.px(6), c.px(1.5f), TOP_SUB, TOP_TRACK);
				c.text(INTER, 12, s.key, (int)(x + (cs - c.textW(INTER, 12, s.key)) / 2), (int)(y + (hH - c.lineH(INTER, 12)) / 2), TOP_TEXT);
				x += cap;
			}
			c.text(INTER, 14, hint, x, (int)(y + (hH - c.lineH(INTER, 14)) / 2), TOP_SUB);
		}
		if (s.full && !s.foot.empty())
		{
			std::string f = c.fit(INTER, 13, s.foot, colW);
			c.text(INTER, 13, f, (int)(cx - c.textW(INTER, 13, f) / 2.f), h - c.px(30) - c.lineH(INTER, 13), TOP_SUB);
		}
		return finish(c);
	}

	std::vector<uint8_t> premultipliedBgra(const LevelCardImage& card)
	{
		std::vector<uint8_t> bgra((size_t)card.w * card.h * 4);
		if (!card.ok()) return bgra;
		const uint8_t* s = card.rgba->data();
		for (size_t p = 0; p < (size_t)card.w * card.h; p++)
		{
			const uint8_t* c = s + p * 4;
			int a = c[3];
			bgra[p * 4 + 0] = (uint8_t)((c[2] * a + 127) / 255);
			bgra[p * 4 + 1] = (uint8_t)((c[1] * a + 127) / 255);
			bgra[p * 4 + 2] = (uint8_t)((c[0] * a + 127) / 255);
			bgra[p * 4 + 3] = (uint8_t)a;
		}
		return bgra;
	}
}
