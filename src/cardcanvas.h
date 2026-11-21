#pragma once
// A picture drawn in software at a panel's own pixels, for the top panel (mpv's image overlays, the readers' card, the
// Preview pane): straight-alpha RGBA, sizes in the spec's 640x480 units scaled by the panel's height, round boxes with
// smoothed edges, text in Pixelify Sans or Inter, the G_* glyphs. Moved out of levelcard.cpp so the Preview pane
// (previewpane.cpp) draws with the same primitives as the cards. Main thread only (the fonts are opened once and kept).
#include "fontruns.h"
#include "levelcard.h"
#include "theme.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace bp
{
	namespace card
	{
		using theme::Rgb;

		enum Face { PIXEL, INTER, INTER_BOLD };

		// a font at `units * k` em pixels (SDL_ttf's point size at 72 dpi), opened once a size
		inline TTF_Font* faceFont(const std::string& fontsDir, Face face, float px)
		{
			static std::map<std::string, TTF_Font*> fonts;
			std::string path = fontsDir + (face == PIXEL ? "/PixelifySans-Regular.ttf" : face == INTER_BOLD ? "/PixelifySans-Regular.ttf" : "/PixelifySans-Regular.ttf");
			int pt = std::max(6, (int)std::lround(px));
			std::string key = path + "@" + std::to_string(pt);
			auto it = fonts.find(key);
			if (it != fonts.end()) return it->second;
			TTF_Font* f = TTF_OpenFont(path.c_str(), pt);
			if (f) fonts[key] = f;	// (a handful of sizes at most, the panel's)
			return f;
		}

		// a fallback font's file at `pt`, opened once a size (the cards' few sizes)
		inline TTF_Font* fileFont(const std::string& fontsDir, const char* file, int pt)
		{
			static std::map<std::string, TTF_Font*> fonts;
			std::string key = std::string(file) + "@" + std::to_string(pt);
			auto it = fonts.find(key);
			if (it != fonts.end()) return it->second;
			TTF_Font* f = TTF_OpenFont((fontsDir + "/" + file).c_str(), pt);
			fonts[key] = f;	// (null kept too: a missing file isn't looked for again each card)
			return f;
		}

		// the length in bytes of the UTF-8 character at s[i]
		inline size_t u8len(const std::string& s, size_t i)
		{
			unsigned char c = s[i];
			size_t n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
			return std::min(n, s.size() - i);
		}

		// One card being drawn: pixels, the unit's scale, and the primitives (a rounded box, text, a glyph).
		struct Canvas
		{
			std::string fonts;
			float k;
			int W = 0, H = 0;
			std::vector<uint8_t> v;

			Canvas(const std::string& fontsDir, int panelH) : fonts(fontsDir), k((panelH > 0 ? panelH : 480) / 480.f) {}
			int px(float u) const { return (int)std::lround(u * k); }
			void size(int w, int h)
			{
				W = w;
				H = h;
				v.assign((size_t)w * h * 4, 0);
			}
			// one source over what's drawn (straight alpha)
			void blend(int x, int y, Rgb c, float a)
			{
				if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
				uint8_t* p = &v[((size_t)y * W + x) * 4];
				float da = p[3] / 255.f, oa = a + da * (1 - a);
				if (oa <= 0) return;
				p[0] = (uint8_t)std::lround((c.r * a + p[0] * da * (1 - a)) / oa);
				p[1] = (uint8_t)std::lround((c.g * a + p[1] * da * (1 - a)) / oa);
				p[2] = (uint8_t)std::lround((c.b * a + p[2] * da * (1 - a)) / oa);
				p[3] = (uint8_t)std::lround(oa * 255);
			}
			// a box with round corners, in pixels, edges smoothed by each pixel's cover (as libass drew the ASS cards)
			void box(float x0, float y0, float bw, float bh, float r, Rgb c, int alpha = 255)
			{
				r = std::min({r, bw / 2, bh / 2});
				float cx = x0 + bw / 2, cy = y0 + bh / 2, hx = bw / 2 - r, hy = bh / 2 - r;
				for (int y = std::max(0, (int)std::floor(y0)); y < std::min(H, (int)std::ceil(y0 + bh)); y++)
					for (int x = std::max(0, (int)std::floor(x0)); x < std::min(W, (int)std::ceil(x0 + bw)); x++)
					{
						float qx = std::abs(x + 0.5f - cx) - hx, qy = std::abs(y + 0.5f - cy) - hy;
						float d = std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - r;
						blend(x, y, c, std::clamp(0.5f - d, 0.f, 1.f) * alpha / 255.f);
					}
			}
			// a convex polygon (pixels), edges smoothed by supersampling 4 x 4
			void poly(const float* pt, int n, Rgb c)
			{
				float x0 = pt[0], x1 = pt[0], y0 = pt[1], y1 = pt[1];
				for (int i = 1; i < n; i++) { x0 = std::min(x0, pt[2 * i]); x1 = std::max(x1, pt[2 * i]); y0 = std::min(y0, pt[2 * i + 1]); y1 = std::max(y1, pt[2 * i + 1]); }
				for (int y = std::max(0, (int)std::floor(y0)); y < std::min(H, (int)std::ceil(y1)); y++)
					for (int x = std::max(0, (int)std::floor(x0)); x < std::min(W, (int)std::ceil(x1)); x++)
					{
						int in = 0;
						for (int sy = 0; sy < 4; sy++)
							for (int sx = 0; sx < 4; sx++)
							{
								float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
								bool pos = false, neg = false;
								for (int i = 0; i < n; i++)
								{
									int j = (i + 1) % n;
									float cr = (pt[2 * j] - pt[2 * i]) * (py - pt[2 * i + 1]) - (pt[2 * j + 1] - pt[2 * i + 1]) * (px - pt[2 * i]);
									if (cr > 0) pos = true; else if (cr < 0) neg = true;
								}
								if (!(pos && neg)) in++;
							}
						if (in) blend(x, y, c, in / 16.f);
					}
			}
			// a box with an edge `t` thick (the key cap and the tile): the edge, then the fill inside it
			void ring(float x0, float y0, float bw, float bh, float r, float t, Rgb edge, Rgb fill)
			{
				box(x0, y0, bw, bh, r, edge);
				box(x0 + t, y0 + t, bw - 2 * t, bh - 2 * t, std::max(0.f, r - t), fill);
			}
			TTF_Font* font(Face f, float units) const { return faceFont(fonts, f, units * k); }
			// the text in pieces, each in a font that has its characters (a Japanese file name in the Preview pane):
			// Pixelify Sans, else Fusion Pixel (CJK, at a multiple of its 12 pixels as on the bottom panel), Noto Sans
			std::vector<FontRun> runs(Face f, float units, const std::string& s) const
			{
				static const char* FALLBACKS[] = {FONT_CJK, "NotoSansThai.ttf", "NotoSansArabic.ttf", "NotoSansHebrew.ttf",
					"NotoSansDevanagari.ttf"};
				const int pt = std::max(6, (int)std::lround(units * k));
				const int cjkPt = std::max(12, (int)std::lround(units * k / 12.0f) * 12);
				auto fallback = [&](size_t i) { return fileFont(fonts, FALLBACKS[i], i == 0 ? cjkPt : pt); };
				TTF_Font* ja = hasKana(s) ? fileFont(fonts, FONT_JA_FORMS, cjkPt) : nullptr;
				return fontRuns(s, font(f, units), sizeof FALLBACKS / sizeof FALLBACKS[0], fallback, fallback(0), ja);
			}
			int textW(Face f, float units, const std::string& s) const
			{
				int total = 0;
				if (!font(f, units) || s.empty()) return 0;
				for (auto& r : runs(f, units, s))
				{
					int w = 0, h = 0;
					TTF_SizeUTF8(r.first, r.second.c_str(), &w, &h);
					total += w;
				}
				return total;
			}
			int lineH(Face f, float units) const
			{
				TTF_Font* fn = font(f, units);
				return fn ? TTF_FontHeight(fn) : px(units * 1.3f);
			}
			// the text cut with "…" to fit maxPx; `middle`: the cut in the middle (a file name keeps its end)
			std::string fit(Face f, float units, std::string s, int maxPx, bool middle = false) const
			{
				for (char& c : s) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
				if (textW(f, units, s) <= maxPx) return s;
				std::vector<size_t> at;	// the characters' starts
				for (size_t i = 0; i < s.size(); i += u8len(s, i)) at.push_back(i);
				at.push_back(s.size());
				const std::string dots = "\xe2\x80\xa6";
				auto build = [&](size_t keep) {
					if (!middle) return s.substr(0, at[keep]) + dots;
					size_t tail = keep / 2, head = keep - tail;
					return s.substr(0, at[head]) + dots + s.substr(at[at.size() - 1 - tail]);
				};
				size_t lo = 0, hi = at.size() - 1;	// the most characters kept that fit
				while (lo < hi)
				{
					size_t mid = (lo + hi + 1) / 2;
					if (textW(f, units, build(mid)) <= maxPx) lo = mid;
					else hi = mid - 1;
				}
				return build(lo);
			}
			// text with its line box's top left at (x, y) pixels
			void text(Face f, float units, const std::string& s, int x, int y, Rgb c)
			{
				TTF_Font* fn = font(f, units);
				if (!fn || s.empty()) return;
				// each piece on the own font's baseline, side by side
				const int base = y + TTF_FontAscent(fn);
				for (auto& r : runs(f, units, s))
					x += piece(r.first, r.second, x, base - TTF_FontAscent(r.first), c);
			}
			// one piece of text in one font, its line box's top left at (x, y); its width
			int piece(TTF_Font* fn, const std::string& s, int x, int y, Rgb c)
			{
				SDL_Color ink{c.r, c.g, c.b, 255};
				SDL_Surface* t = TTF_RenderUTF8_Blended(fn, s.c_str(), ink);
				if (!t) return 0;
				SDL_Surface* ts = SDL_ConvertSurfaceFormat(t, SDL_PIXELFORMAT_RGBA32, 0);
				SDL_FreeSurface(t);
				if (!ts) return 0;
				for (int yy = 0; yy < ts->h; yy++)
					for (int xx = 0; xx < ts->w; xx++)
					{
						const uint8_t* q = (const uint8_t*)ts->pixels + yy * ts->pitch + xx * 4;
						blend(x + xx, y + yy, Rgb{q[0], q[1], q[2]}, q[3] / 255.f);
					}
				int w = ts->w;
				SDL_FreeSurface(ts);
				return w;
			}
			// a glyph scaled so its longer side is `size` pixels, its box's top left at (x, y)
			void glyph(CardGlyph g, float size, float x, float y, Rgb c)
			{
				if (!g.rows || g.h <= 0) return;
				int gw = 0;
				for (int r = 0; r < g.h; r++) gw = std::max(gw, (int)strlen(g.rows[r]));
				float cell = size / std::max(gw, g.h);
				x += (size - gw * cell) / 2;
				y += (size - g.h * cell) / 2;
				for (int r = 0; r < g.h; r++)
					for (int col = 0; g.rows[r][col]; col++)
						if (g.rows[r][col] == '#') box(x + col * cell, y + r * cell, cell, cell, 0, c);
			}
		};
	}
}
