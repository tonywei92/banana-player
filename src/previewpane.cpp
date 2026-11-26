// The Preview pane, composed in software at the top panel's pixels (previewpane.h). Its places are lay::previewPane
// and lay::swatches (kitlayout.h, tested); its primitives are the cards' (cardcanvas.h). The main tokens throughout:
// it is app UI, not a card over a picture, so its ground is the theme's BG. Only the small "Cards stay dark" card on
// the Theme row's pane is drawn in the TOP_ tokens, as every card over a picture is.
#include "previewpane.h"
#include "cardcanvas.h"
#include "kitlayout.h"
#include "textwrap.h"
#include "bookcover.h"
#include "theme.h"
#include "ui.h"
#include "ui_glyphs.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	namespace
	{
		using namespace card;
		using theme::Rgb;

		CardGlyph cg(const Ui::Glyph& g) { return {g.rows, g.h}; }

		// The pane's own drawing: the canvas plus where the panel's 640 x 480 units start (a panel wider than 4:3 has
		// the pane centred)
		struct PaneDraw
		{
			Canvas& c;
			float ox;
			float X(float u) const { return ox + u * c.k; }
			float Y(float u) const { return u * c.k; }
			float S(float u) const { return u * c.k; }
			int lineH(Face f, float u) const { return c.lineH(f, u); }

			// text at units (x, y) as the line box's top left; returns its width in units
			float text(Face f, float size, const std::string& s, float x, float y, Rgb ink)
			{
				c.text(f, size, s, (int)std::lround(X(x)), (int)std::lround(Y(y)), ink);
				return c.textW(f, size, s) / c.k;
			}
			// small capitals with the mock-up's letter spacing (.12em), a character at a time
			float spaced(Face f, float size, const std::string& s, float x, float y, Rgb ink, float em = 0.12f)
			{
				float x0 = x;
				for (size_t i = 0; i < s.size();)
				{
					size_t n = card::u8len(s, i);
					std::string ch = s.substr(i, n);
					x += text(f, size, ch, x, y, ink) + size * em;
					i += n;
				}
				return x - x0;
			}
			float textW(Face f, float size, const std::string& s) const { return c.textW(f, size, s) / c.k; }
			// the words in lines no wider than w units (at most `most`, the last cut with "…")
			std::vector<std::string> wrap(Face f, float size, const std::string& s, float w, int most)
			{
				// at spaces, then after - _ . /, then mid-word (textwrap.h); more than `most`: the last cut with "…"
				std::vector<std::string> lines =
					textwrap::lines(s, most, [&](const std::string& t) { return textW(f, size, t) <= w; });
				if (!lines.empty()) lines.back() = c.fit(f, size, lines.back(), (int)S(w));
				return lines;
			}
			void box(float x, float y, float w, float h, float r, Rgb fill, int alpha = 255)
			{
				c.box(X(x), Y(y), S(w), S(h), S(r), fill, alpha);
			}
			// a box with an edge (2 units) and no lip: a surface, a focused row
			void flat(float x, float y, float w, float h, float r, Rgb fill, Rgb edge)
			{
				c.ring(X(x), Y(y), S(w), S(h), S(r), S(theme::EDGE), edge, fill);
			}
			// a key: its edge, its fill, the darker band above its foot (theme.h's LIP)
			void key(float x, float y, float w, float h, Rgb fill)
			{
				box(x, y, w, h, theme::R_BUTTON, theme::edgeOf(fill));
				float e = theme::EDGE;
				box(x + e, y + e, w - 2 * e, h - 2 * e, theme::R_BUTTON - e, theme::lipOf(fill));
				box(x + e, y + e, w - 2 * e, h - 2 * e - theme::LIP, theme::R_BUTTON - e, fill);
			}
			// a key cap (the button's name in an outline), its height 19; returns its width
			float cap(const std::string& k, float x, float cy, Rgb ink, Rgb ground)
			{
				float w = std::max(19.f, textW(PIXEL, 12, k) + 10);
				flat(x, cy - 9.5f, w, 19, theme::R_CAP, ground, ink);
				text(PIXEL, 12, k, x + (w - textW(PIXEL, 12, k)) / 2, cy - lineH(PIXEL, 12) / c.k / 2, ink);
				return w;
			}
			// a pill (a row's mark): its right edge at `right`; returns its width
			float pill(const std::string& t, float right, float cy, Rgb fill, Rgb ink)
			{
				float w = textW(INTER_BOLD, 11, t) + 12;
				box(right - w, cy - 8.5f, w, 17, theme::R_CAP, fill);
				text(INTER_BOLD, 11, t, right - w + 6, cy - lineH(INTER_BOLD, 11) / c.k / 2, ink);
				return w;
			}
			void glyph(const Ui::Glyph& g, float size, float x, float y, Rgb ink) { c.glyph(cg(g), S(size), X(x), Y(y), ink); }
		};

		// a book's cover drawn from its title, in the box (units): the bottom panel's (Ui::Kit::bookPlaceholder) in the
		// pane's own type: ground by the title's hash, a spine, the title large, the author under it, the format's mark
		void generatedCover(PaneDraw& d, const PaneSpec& s, float x, float y, float w, float h)
		{
			const bookcover::Rgb g = bookcover::ground(s.genTitle), ik = bookcover::ink(), dm = bookcover::inkDim();
			const Rgb ground{g.r, g.g, g.b}, ink{ik.r, ik.g, ik.b}, dim{dm.r, dm.g, dm.b};
			const Rgb spine{(uint8_t)(g.r * 3 / 4), (uint8_t)(g.g * 3 / 4), (uint8_t)(g.b * 3 / 4)};
			const float r = std::max(0, theme::R_TILE - theme::EDGE);
			d.box(x, y, w, h, r, ground);
			d.box(x + 2, y + 4, w / 14, h - 8, 2, spine);
			float left = x + 2 + w / 14 + 14, tw = x + w - 14 - left;
			float top = y + 18;
			auto lines = d.wrap(PIXEL, 22, s.genTitle, tw, 6);
			float ty = top;
			for (auto& t : lines)
			{
				d.text(PIXEL, 22, t, left, ty, ink);
				ty += 22 * 1.2f;
			}
			if (!s.genAuthor.empty())
			{
				ty += 8;
				for (auto& t : d.wrap(INTER, 13, s.genAuthor, tw, 2))
				{
					d.text(INTER, 13, t, left, ty, dim);
					ty += 13 * 1.4f;
				}
			}
			d.glyph(bookKindGlyph(bookcover::kindOf(s.genFmt)), 22, x + w - 14 - 22, y + h - 14 - 20, dim);
		}

		// a picture fitted inside the box (units), the aspect kept, centred; a box filter at the picture's own rate (few
		// taps: a cover is 196 x 278 units on the panel); radius > 0 rounds its corners (the welcome's icon)
		void paintImage(PaneDraw& d, const PaneImage& img, float bx, float by, float bw, float bh, float radius)
		{
			Canvas& c = d.c;
			float aw = img.w, ah = img.h;
			float fit = std::min(bw / aw, bh / ah), dw = aw * fit, dh = ah * fit;
			int dx = (int)std::lround(d.X(bx + (bw - dw) / 2)), dy = (int)std::lround(d.Y(by + (bh - dh) / 2));
			int pw = std::max(1, (int)std::lround(d.S(dw))), ph = std::max(1, (int)std::lround(d.S(dh)));
			float r = d.S(radius);
			for (int y = 0; y < ph; y++)
			{
				int y0 = std::clamp((int)((y + 0.5f) * img.h / ph), 0, img.h - 1);
				float fy = std::clamp((y + 0.5f) * img.h / ph - 0.5f, 0.f, (float)img.h - 1);
				int y1 = std::min(img.h - 1, y0 + 1);
				float ty = fy - y0;
				for (int x = 0; x < pw; x++)
				{
					float fx = std::clamp((x + 0.5f) * img.w / pw - 0.5f, 0.f, (float)img.w - 1);
					int x0 = (int)fx, x1 = std::min(img.w - 1, x0 + 1);
					float tx = fx - x0;
					const uint8_t* p00 = &img.rgba[((size_t)y0 * img.w + x0) * 4];
					const uint8_t* p10 = &img.rgba[((size_t)y0 * img.w + x1) * 4];
					const uint8_t* p01 = &img.rgba[((size_t)y1 * img.w + x0) * 4];
					const uint8_t* p11 = &img.rgba[((size_t)y1 * img.w + x1) * 4];
					uint8_t px[3];
					for (int ch = 0; ch < 3; ch++)
						px[ch] = (uint8_t)std::lround((p00[ch] * (1 - tx) + p10[ch] * tx) * (1 - ty) + (p01[ch] * (1 - tx) + p11[ch] * tx) * ty);
					float a = 1;
					if (r > 0)
					{
						float qx = std::abs(x + 0.5f - pw / 2.f) - (pw / 2.f - r), qy = std::abs(y + 0.5f - ph / 2.f) - (ph / 2.f - r);
						float sd = std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - r;
						a = std::clamp(0.5f - sd, 0.f, 1.f);
						// the icon's own alpha too
						a *= (p00[3] * (1 - tx) + p10[3] * tx) / 255.f;
					}
					int sx = dx + x, sy = dy + y;
					if (sx < 0 || sy < 0 || sx >= c.W || sy >= c.H || a <= 0) continue;
					uint8_t* o = &c.v[((size_t)sy * c.W + sx) * 4];
					o[0] = (uint8_t)std::lround(px[0] * a + o[0] * (1 - a));
					o[1] = (uint8_t)std::lround(px[1] * a + o[1] * (1 - a));
					o[2] = (uint8_t)std::lround(px[2] * a + o[2] * (1 - a));
					o[3] = 255;
				}
			}
		}

		// a video's frame across the panel's whole width, from the top to the box's foot (units), cropped to fill it
		// (the middle kept), then faded into the ground from fadeFrom down (the spec: transparent to BG over 114 units)
		void paintHero(PaneDraw& d, const PaneImage& img, float bottom, float fadeFrom, theme::Rgb ground)
		{
			Canvas& c = d.c;
			int pw = c.W, ph = std::min(c.H, (int)std::lround(d.Y(bottom)));
			if (pw <= 0 || ph <= 0) return;
			// the picture's part that fills pw x ph: the whole width or the whole height, the middle of the other
			float scale = std::max((float)pw / img.w, (float)ph / img.h);
			float sx0 = (img.w - pw / scale) / 2, sy0 = (img.h - ph / scale) / 2;
			int fade0 = (int)std::lround(d.Y(fadeFrom));
			for (int y = 0; y < ph; y++)
			{
				float fy = std::clamp(sy0 + (y + 0.5f) / scale - 0.5f, 0.f, (float)img.h - 1);
				int y0 = (int)fy, y1 = std::min(img.h - 1, y0 + 1);
				float ty = fy - y0;
				// the fade: none above fade0, all ground at the foot (eased, so its end has no edge)
				float t = y < fade0 ? 0.f : std::clamp((float)(y - fade0) / std::max(1, ph - fade0), 0.f, 1.f);
				float g = t * t * (3 - 2 * t);
				for (int x = 0; x < pw; x++)
				{
					float fx = std::clamp(sx0 + (x + 0.5f) / scale - 0.5f, 0.f, (float)img.w - 1);
					int x0 = (int)fx, x1 = std::min(img.w - 1, x0 + 1);
					float tx = fx - x0;
					const uint8_t* p00 = &img.rgba[((size_t)y0 * img.w + x0) * 4];
					const uint8_t* p10 = &img.rgba[((size_t)y0 * img.w + x1) * 4];
					const uint8_t* p01 = &img.rgba[((size_t)y1 * img.w + x0) * 4];
					const uint8_t* p11 = &img.rgba[((size_t)y1 * img.w + x1) * 4];
					uint8_t* o = &c.v[((size_t)y * c.W + x) * 4];
					const uint8_t bg[3] = {ground.r, ground.g, ground.b};
					for (int ch = 0; ch < 3; ch++)
					{
						float v = (p00[ch] * (1 - tx) + p10[ch] * tx) * (1 - ty) + (p01[ch] * (1 - tx) + p11[ch] * tx) * ty;
						o[ch] = (uint8_t)std::lround(v * (1 - g) + bg[ch] * g);
					}
					o[3] = 255;
				}
			}
		}

		// the Brand mark (lay::brandMark: the clapperboard on a 22 grid) at (x, y) in units, `size` wide
		void brand(PaneDraw& d, float x, float y, float size)
		{
			using namespace theme;
			lay::BrandMark m = lay::brandMark(x, y, size);
			auto bx = [&](const float* b, float inset, float r, Rgb fill) {
				d.box(b[0] + inset, b[1] + inset, b[2] - 2 * inset, b[3] - 2 * inset, std::max(0.f, r - inset), fill);
			};
			bx(m.body, 0, m.bodyRadius, edgeOf(SURFACE));
			bx(m.body, size / 22.f, m.bodyRadius, BUTTON);
			bx(m.bar, 0, m.barRadius, BG);
			Canvas& c = d.c;
			for (int i = 0; i < 4; i++)
			{
				float pt[8];
				for (int k = 0; k < 8; k++) pt[k] = k % 2 ? d.Y(m.stripe[i][k]) : d.X(m.stripe[i][k]);
				c.poly(pt, 4, ACCENT);
			}
			bx(m.slate, 0, m.slateRadius, SURFACE);
			float pt[6];
			for (int k = 0; k < 6; k++) pt[k] = k % 2 ? d.Y(m.play[k]) : d.X(m.play[k]);
			c.poly(pt, 3, ACCENT);
			bx(m.track, 0, 0, DIM);
			bx(m.fill, 0, 0, ACCENT);
			bx(m.knob, 0, 0, TEXT);
		}

		// the State card: the app's own drawing of "Playing in your browser", Loading, Buffering (and a failure), in the
		// main tokens on the theme's ground (docs/redesign/components.md, State card)
		void stateCard(PaneDraw& d, const PaneSpec& s)
		{
			using namespace theme;
			Canvas& c = d.c;
			const bool hint = !s.keys.empty();
			lay::StateCard l = lay::stateCard(!s.text.empty(), s.share >= 0, s.share >= 0 && !s.timeLine.empty(), hint);
			const Rgb tone = s.danger ? DANGER : ACCENT;
			d.flat(l.tile.x, l.tile.y, l.tile.w, l.tile.h, 18, SURFACE, edgeOf(SURFACE));
			float cx = l.tile.x + l.tile.w / 2.f, cy = l.tile.y + l.tile.h / 2.f;
			if (s.spinner >= 0)
			{
				// eight dots round the tile's middle, one lit going round, the ones behind it fading
				for (int i = 0; i < 8; i++)
				{
					double a = i * 3.14159265358979 / 4;
					float r = 18, dd = 7;
					float dx = cx + (float)std::sin(a) * r, dy = cy - (float)std::cos(a) * r;
					int age = (s.spinner % 8 - i + 8) % 8;
					int alpha = age == 0 ? 255 : age == 1 ? 190 : age == 2 ? 125 : 70;
					d.box(dx - dd / 2, dy - dd / 2, dd, dd, dd / 2, tone, alpha);
				}
			}
			else if (s.iconRows)
			{
				float is = s.danger ? 30.f : 34.f;
				c.glyph(CardGlyph{s.iconRows, s.iconH}, d.S(is), d.X(cx - is / 2), d.Y(cy - is / 2), tone);
			}
			auto centred = [&](Face f, float size, const std::string& t, const lay::Box& b, Rgb ink) {
				std::string fit = c.fit(f, size, t, (int)d.S(b.w), f != PIXEL);
				d.text(f, size, fit, b.x + (b.w - d.textW(f, size, fit)) / 2, b.y + (b.h - d.lineH(f, size) / c.k) / 2, ink);
			};
			centred(PIXEL, 28, s.title, l.title, tone);
			if (l.line.w) centred(PIXEL, 15.5f, s.text, l.line, TEXT);
			if (l.bar.w)
			{
				d.box(l.bar.x, l.bar.y, l.bar.w, l.bar.h, 3, TRACK);
				if (s.share > 0) d.box(l.bar.x, l.bar.y, std::max(6.f, l.bar.w * std::min(1.f, s.share)), l.bar.h, 3, ACCENT);
				if (l.time.w) centred(PIXEL, 13, s.timeLine, l.time, DIM);
			}
			if (hint)
			{
				const auto& k = s.keys[0];
				float tw = d.textW(PIXEL, 14, k.second), capW = k.first.empty() ? 0 : std::max(19.f, d.textW(PIXEL, 12, k.first) + 10) + 7;
				float at = l.hint.x + (l.hint.w - tw - capW) / 2, my = l.hint.y + l.hint.h / 2.f;
				if (!k.first.empty()) at += d.cap(k.first, at, my, ACCENT, BG) + 7;
				d.text(PIXEL, 14, k.second, at, my - d.lineH(PIXEL, 14) / c.k / 2, DIM_ON);
			}
			if (!s.foot.empty()) centred(PIXEL, 13, s.foot, {40, 480 - 30 - 16, 560, 16}, DIM);
		}

		// one theme's swatch: its BG, a SURFACE box with its edge, an ACCENT box, a TEXT line and an ACCENT line (the
		// mock-up's thSwatch: a picture of that theme, whatever theme is drawn now)
		void swatch(PaneDraw& d, lay::Box b, const theme::Palette& t, bool chosen)
		{
			using namespace theme;
			// the edge: the current theme's surface edge; the chosen one ACCENT, a unit wider all round
			if (chosen) d.box(b.x - 1, b.y - 1, b.w + 2, b.h + 2, 10, ACCENT);
			d.box(b.x, b.y, b.w, b.h, 9, chosen ? ACCENT : edgeOf(SURFACE));
			d.box(b.x + EDGE, b.y + EDGE, b.w - 2 * EDGE, b.h - 2 * EDGE, 9 - EDGE, t.BG);
			float ix = b.x + EDGE, iy = b.y + EDGE, iw = b.w - 2 * EDGE, ih = b.h - 2 * EDGE;
			// (84 x 30: a surface and a key along the top, the text's line and an accent mark under them)
			d.box(ix + 6, iy + 4, 34, 11, 3, edgeOf(t.SURFACE, t.TEXT, t.EDGE_MIX));
			d.box(ix + 7, iy + 5, 32, 9, 2, t.SURFACE);
			d.box(ix + iw - 6 - 20, iy + 4, 20, 11, 3, t.ACCENT);
			d.box(ix + 6, iy + ih - 4 - 4, 38, 4, 2, t.TEXT);
			d.box(ix + iw - 6 - 12, iy + ih - 4 - 4, 12, 4, 2, t.ACCENT);
		}

		// the Theme row's block: every theme's swatch (eighteen: three rows of six), then "As it looks" and the pieces drawn in the theme now
		float themesBlock(PaneDraw& d, const PaneSpec& s, float x, float y, float w)
		{
			using namespace theme;
			const int n = themeCount();
			auto boxes = lay::swatches(n, (int)x, (int)y);
			for (int i = 0; i < n; i++)
			{
				const lay::Box& b = boxes[i];
				swatch(d, b, palette(i), i == s.chosen);
				std::string name = palette(i).name;
				float nw = d.textW(INTER, 10.5f, name);
				d.text(INTER, 10.5f, name, b.x + (b.w - nw) / 2, b.y + b.h + 3, i == s.chosen ? ACCENT : DIM_ON);
			}
			int rows = (n + lay::SWATCH_PER_ROW - 1) / lay::SWATCH_PER_ROW;
			y += rows * (lay::SWATCH_H + lay::SWATCH_NAME) + (rows - 1) * lay::SWATCH_ROW_GAP + 13;
			d.spaced(INTER_BOLD, 11, "AS IT LOOKS", x, y, DIM, 0.09f);
			y += d.lineH(INTER_BOLD, 11) / d.c.k + 7;
			const float h = 44, cy = y + h / 2 - 1.5f;	// (the text's middle above the lip)
			// a primary key: Play, with its cap
			{
				float kx = x, kw = 96;
				d.key(kx, y, kw, h, ACCENT);
				float cw = 19, inner = 13 + 6 + d.textW(PIXEL, 15, "Play") + 7 + cw, at = kx + (kw - inner) / 2;
				d.glyph(G_PLAY, 13, at, cy - 6.5f, ON_ACCENT);
				at += 13 + 6;
				at += d.text(PIXEL, 15, "Play", at, cy - d.lineH(PIXEL, 15) / d.c.k / 2, ON_ACCENT) + 7;
				d.cap("A", at, cy, ON_ACCENT, ACCENT);
			}
			// a key: Back, its cap in ACCENT
			{
				float kx = x + 96 + 10, kw = 84;
				d.key(kx, y, kw, h, BUTTON);
				float inner = d.textW(PIXEL, 15, "Back") + 7 + 19, at = kx + (kw - inner) / 2;
				at += d.text(PIXEL, 15, "Back", at, cy - d.lineH(PIXEL, 15) / d.c.k / 2, TEXT) + 7;
				d.cap("B", at, cy, ACCENT, BUTTON);
			}
			// a focused row: BUTTON, an ACCENT edge, its name in ACCENT, a pill at its end
			{
				float rx = x + 96 + 10 + 84 + 10, rw = 214, my = y + h / 2;
				d.flat(rx, y, rw, h, R_BUTTON, BUTTON, ACCENT);
				d.glyph(G_NOTE, 18, rx + 12, my - 9, TEXT);
				float pw = d.textW(INTER_BOLD, 11, "Playing") + 12;
				std::string name = d.c.fit(PIXEL, 15, "Sunny Peel", (int)d.S(rw - 12 - 18 - 11 - pw - 20));
				d.text(PIXEL, 15, name, rx + 12 + 18 + 11, my - d.lineH(PIXEL, 15) / d.c.k / 2, ACCENT);
				d.pill("Playing", rx + rw - 12, my, ACCENT, ON_ACCENT);
			}
			// two pills (a fact, a failure) over a bar
			{
				float cx = x + 96 + 10 + 84 + 10 + 214 + 10, cw = x + w - cx;
				float py = y + 9;
				float right = cx + d.textW(INTER_BOLD, 11, "On card") + 12;
				d.pill("On card", right, py, TRACK, TEXT);
				d.pill("Gone", right + 6 + d.textW(INTER_BOLD, 11, "Gone") + 12, py, mix(BG, DANGER, TINT), DANGER);
				float by = py + 8.5f + 7;
				d.box(cx, by, cw, 6, 3, TRACK);
				d.box(cx, by, std::max(6.f, cw * 0.62f), 6, 3, ACCENT);
			}
			return y + h;
		}

		// the key line: each key's cap, then what it does; cy its middle
		void keyLine(PaneDraw& d, const std::vector<std::pair<std::string, std::string>>& keys, float x, float cy)
		{
			using namespace theme;
			float at = x;
			for (auto& k : keys)
			{
				if (!k.first.empty()) at += d.cap(k.first, at, cy, ACCENT, BG) + 5;
				at += d.text(INTER, 12.5f, k.second, at, cy - d.lineH(INTER, 12.5f) / d.c.k / 2, DIM_ON) + 15;
			}
		}

		// a kind's colour in the storage pane's bar and beside its name: the tone (ACCENT; DANGER nearly full) at
		// full strength for the first kind and fainter for each after it, and DIM for what no kind accounts for
		Rgb kindInk(const StorageKind& k, int nth, bool full)
		{
			using namespace theme;
			static const int strength[] = {100, 66, 40};
			if (k.other) return DIM;
			return mix(BG, full ? DANGER : ACCENT, strength[std::min(nth, 2)]);
		}

		// the storage pane (lay::storagePane; docs/redesign/src/screens.js: files-place-top)
		void storagePane(PaneDraw& d, const PaneSpec& s)
		{
			using namespace theme;
			Canvas& c = d.c;
			lay::StoragePane l = lay::storagePane((int)s.fill.size());
			auto mid = [&](Face f, float size, const lay::Box& b) { return b.y + (b.h - d.lineH(f, size) / c.k) / 2; };
			auto fit = [&](Face f, float size, const std::string& t, float w) { return c.fit(f, size, t, (int)d.S(w)); };
			brand(d, l.brandTile.x, l.brandTile.y, l.brandTile.w);
			d.text(PIXEL, 15, "Banana Player", l.brandName.x, mid(PIXEL, 15, l.brandName), ACCENT);
			d.spaced(INTER_BOLD, 11, lay::caps(s.label), l.label.x, mid(INTER_BOLD, 11, l.label), s.labelDanger ? DANGER : ACCENT);
			d.text(PIXEL, 28, fit(PIXEL, 28, s.title, l.title.w), l.title.x, mid(PIXEL, 28, l.title), TEXT);
			d.text(INTER, 14.5f, fit(INTER, 14.5f, s.text, l.where.w), l.where.x, mid(INTER, 14.5f, l.where), DIM_ON);
			// the room: the free figure large (DANGER nearly full, as the place's card), the rest in words at the right
			d.text(PIXEL, 36, s.room.free, l.figure.x, mid(PIXEL, 36, l.figure), s.room.full ? DANGER : TEXT);
			std::string words = fit(INTER, 14.5f, s.room.detail, l.words.w);
			d.text(INTER, 14.5f, words, l.words.x + l.words.w - d.textW(INTER, 14.5f, words), mid(INTER, 14.5f, l.words), DIM_ON);
			// the bar: what is used cut into its kinds; nothing known of them: one segment of all that is used
			d.box(l.bar.x, l.bar.y, l.bar.w, l.bar.h, 6, TRACK);
			std::vector<double> shares;
			for (auto& k : s.fill) shares.push_back(k.share);
			if (shares.empty()) shares.push_back(s.room.used);
			std::vector<lay::Box> seg = lay::storageSegments(l.bar, shares);
			int nth = 0;
			for (size_t i = 0; i < seg.size(); i++)
			{
				Rgb ink = s.fill.empty() ? (s.room.full ? DANGER : ACCENT) : kindInk(s.fill[i], nth, s.room.full);
				if (!s.fill.empty() && !s.fill[i].other) nth++;
				if (seg[i].w > 0) d.box(seg[i].x, seg[i].y, seg[i].w, seg[i].h, std::min(6.f, seg[i].w / 2.f), ink);
			}
			if (l.head.w) d.spaced(INTER_BOLD, 11, "WHAT FILLS IT", l.head.x, mid(INTER_BOLD, 11, l.head), DIM);
			nth = 0;
			for (size_t i = 0; i < s.fill.size() && i < l.kinds.size(); i++)
			{
				const StorageKind& k = s.fill[i];
				const lay::Box& b = l.kinds[i];
				d.box(b.x, b.y + (b.h - 12) / 2.f, 12, 12, 3, kindInk(k, nth, s.room.full));
				if (!k.other) nth++;
				float sw = d.textW(INTER, 13.5f, k.size);
				d.text(INTER, 13.5f, k.size, b.x + b.w - sw, mid(INTER, 13.5f, b), TEXT);
				float at = b.x + 22, room = b.w - 22 - sw - 12;
				std::string name = fit(PIXEL, 15, k.name, room);
				float nw = d.text(PIXEL, 15, name, at, mid(PIXEL, 15, b), TEXT);
				if (!k.count.empty() && room - nw - 10 > 40)
					d.text(INTER, 12.5f, fit(INTER, 12.5f, k.count, room - nw - 10), at + nw + 10, mid(INTER, 12.5f, b), DIM);
			}
			float y = l.note.y;
			for (auto& t : d.wrap(INTER, 12.5f, s.note, l.note.w, 2))
			{
				d.text(INTER, 12.5f, t, l.note.x, y, DIM);
				y += 12.5f * 1.5f;
			}
			if (!s.keys.empty()) keyLine(d, s.keys, l.keys.x, l.keys.y + l.keys.h / 2.f);
		}
	}

	PaneImage composePane(const std::string& fontsDir, int w, int h, const PaneSpec& s)
	{
		using namespace theme;
		PaneImage out;
		if (w <= 0 || h <= 0) return out;
		Canvas c(fontsDir, h);
		c.size(w, h);
		PaneDraw d{c, std::max(0.f, (w - 640 * c.k) / 2)};
		// the ground, opaque (the pane is the panel): set, not blended, it's every pixel
		for (size_t i = 0; i < (size_t)w * h; i++)
		{
			uint8_t* px = &c.v[i * 4];
			px[0] = BG.r;
			px[1] = BG.g;
			px[2] = BG.b;
			px[3] = 255;
		}
		if (s.state)
		{
			stateCard(d, s);
			out.w = w;
			out.h = h;
			out.rgba = std::move(c.v);
			return out;
		}
		if (s.storage)
		{
			storagePane(d, s);
			out.w = w;
			out.h = h;
			out.rgba = std::move(c.v);
			return out;
		}
		lay::Pane l = lay::previewPane(s.cover, s.welcome, s.coverSquare, s.hero);
		if (s.hero)
		{
			// the frame, faded into the ground from 120 units down; none yet (or none at all): a band of SURFACE,
			// faded the same way, with the film's mark
			if (s.heroArt && s.heroArt->ok()) paintHero(d, *s.heroArt, l.hero.y + l.hero.h, 120, BG);
			else
			{
				PaneImage band;
				band.w = band.h = 1;
				band.rgba = {SURFACE.r, SURFACE.g, SURFACE.b, 255};
				paintHero(d, band, l.hero.y + l.hero.h, 120, BG);
				d.glyph(G_FILM, 44, l.hero.x + (l.hero.w - 44) / 2.f, 70, DIM);
			}
		}
		if (s.welcome)
		{
			// the app's icon (84, radius 20) beside the name, in place of the small mark
			if (s.icon.ok()) paintImage(d, s.icon, l.icon.x, l.icon.y, l.icon.w, l.icon.h, 20);
			else brand(d, l.icon.x, l.icon.y, l.icon.w);
		}
		else
		{
			brand(d, l.brandTile.x, l.brandTile.y, l.brandTile.w);
			d.text(PIXEL, 15, "Banana Player", l.brandName.x, l.brandName.y + (l.brandName.h - d.lineH(PIXEL, 15) / c.k) / 2, ACCENT);
		}
		if (s.cover)
		{
			// the cover's place, then the picture fitted inside it (none: the place alone, with the picture's mark)
			d.flat(l.cover.x, l.cover.y, l.cover.w, l.cover.h, R_TILE, SURFACE, edgeOf(SURFACE));
			if (s.coverArt && s.coverArt->ok())
			{
				// (its corners cut to the place's inner curve: the edge stays whole around it)
				paintImage(d, *s.coverArt, l.cover.x + EDGE, l.cover.y + EDGE, l.cover.w - 2 * EDGE, l.cover.h - 2 * EDGE,
					std::max(0, R_TILE - EDGE));
			}
			else if (!s.genTitle.empty()) generatedCover(d, s, l.cover.x + EDGE, l.cover.y + EDGE, l.cover.w - 2 * EDGE, l.cover.h - 2 * EDGE);
			else d.glyph(G_PICTURE, 44, l.cover.x + (l.cover.w - 44) / 2.f, l.cover.y + (l.cover.h - 44) / 2.f, DIM);
		}
		// the column: label, title, sentence, then the block, the rows, the keys
		float x = l.column.x, cw = l.column.w, y = s.extra == PaneSpec::THEMES ? l.column.y - 8 : l.column.y;
		if (!s.label.empty())
		{
			d.spaced(INTER_BOLD, 11, lay::caps(s.label), x, y, s.labelDanger ? DANGER : ACCENT);
			y += d.lineH(INTER_BOLD, 11) / c.k + 8;
		}
		if (!s.title.empty())
		{
			auto lines = d.wrap(PIXEL, 28, s.title, cw, 2);
			for (auto& t : lines)
			{
				d.text(PIXEL, 28, t, x, y, TEXT);
				y += 28 * 1.15f;
			}
			y += 8;
		}
		if (!s.text.empty())
		{
			int most = s.extra == PaneSpec::THEMES ? 3 : 5;
			if (s.wholeText)
			{
				// down to the foot's margin, less what follows it (the rows, the key line)
				float after = (s.rows.empty() ? 0 : 18 + (13.5f * 1.2f + 7) * s.rows.size()) + (s.keys.empty() ? 0 : 18 + 20);
				most = std::max(5, (int)((460 - y - after) / (14.5f * 1.5f)));
			}
			auto lines = d.wrap(INTER, 14.5f, s.text, cw, most);
			for (auto& t : lines)
			{
				d.text(INTER, 14.5f, t, x, y, DIM_ON);
				y += 14.5f * 1.5f;
			}
		}
		if (s.welcome)
		{
			// the ways to start sit under the icon, from the sides' margin
			x = l.below.x;
			cw = l.below.w;
			y = l.below.y - 18;
		}
		if (s.extra == PaneSpec::THEMES) y = themesBlock(d, s, x, y + 12, cw);
		if (!s.rows.empty() && s.hero)
		{
			// two to a line (the spec's four columns: name, value, name, value), each pair half the column
			y += 6;
			float half = (cw - 20) / 2, dt = 0;
			for (auto& r : s.rows) dt = std::max(dt, d.textW(INTER, 13.5f, r.first));
			dt = std::min(dt, half / 2);
			for (size_t i = 0; i < s.rows.size(); i++)
			{
				const auto& r = s.rows[i];
				float rx = x + (i % 2 ? half + 20 : 0);
				d.text(INTER, 13.5f, c.fit(INTER, 13.5f, r.first, (int)d.S(dt)), rx, y, DIM);
				d.text(INTER, 13.5f, c.fit(INTER, 13.5f, r.second, (int)d.S(half - dt - 12)), rx + dt + 12, y, TEXT);
				if (i % 2 || i + 1 == s.rows.size()) y += 13.5f * 1.2f + 7;
			}
		}
		else if (!s.rows.empty())
		{
			y += 18;
			float dt = 0;
			for (auto& r : s.rows) dt = std::max(dt, d.textW(INTER, 13.5f, r.first));
			dt = std::min(dt, cw / 2);
			for (auto& r : s.rows)
			{
				d.text(INTER, 13.5f, c.fit(INTER, 13.5f, r.first, (int)d.S(dt)), x, y, DIM);
				d.text(INTER, 13.5f, c.fit(INTER, 13.5f, r.second, (int)d.S(cw - dt - 16)), x + dt + 16, y, TEXT);
				y += 13.5f * 1.2f + 7;
			}
		}
		if (!s.keys.empty())
		{
			y += 18;
			keyLine(d, s.keys, x, y + 10);
		}
		if (s.extra == PaneSpec::THEMES)
		{
			// over a picture the cards stay dark: a small one in the TOP_ tokens, top right
			const std::string a = "Over a video", b = "Cards stay dark";
			float bw = std::max(d.textW(INTER, 11.5f, a), d.textW(PIXEL, 16, b)) + 28 + 4;
			float ah = d.lineH(INTER, 11.5f) / c.k, bh = d.lineH(PIXEL, 16) / c.k, bhh = 9 + ah + 3 + bh + 9 + 4;
			float bx = 640 - 20 - bw, by = 14;
			d.box(bx, by, bw, bhh, R_TOP_CARD, TOP_EDGE);
			d.box(bx + 2, by + 2, bw - 4, bhh - 4, R_TOP_CARD - 2, TOP_CARD);
			d.text(INTER, 11.5f, a, bx + 16, by + 11, TOP_SUB);
			d.text(PIXEL, 16, b, bx + 16, by + 11 + ah + 3, TOP_TITLE);
		}
		out.w = w;
		out.h = h;
		out.rgba = std::move(c.v);
		return out;
	}
}
