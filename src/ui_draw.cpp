#include "ui.h"
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
#include "ui_smoothlist.h"
#include "songlayout.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"
#include "ui_reader.h"
#include "ui_manga.h"
#include "ui_player.h"
#include "ui_sleep.h"
#include "ui_shell.h"
#include "fontruns.h"

namespace bp
{
	// Pixelify Sans's "fi" and "fl" ligatures come out as another letter ("file" read "Ale"): its runs are cut between
	// the two, each piece shaped on its own
	static std::vector<std::pair<TTF_Font*, std::string>> unligated(std::vector<std::pair<TTF_Font*, std::string>> in, TTF_Font* pixel)
	{
		std::vector<std::pair<TTF_Font*, std::string>> out;
		for (auto& r : in)
		{
			if (r.first != pixel) { out.push_back(r); continue; }
			size_t from = 0;
			for (size_t i = 0; i + 1 < r.second.size(); i++)
				if (r.second[i] == 'f' && (r.second[i + 1] == 'i' || r.second[i + 1] == 'l'))
				{
					out.push_back({pixel, r.second.substr(from, i + 1 - from)});
					from = i + 1;
				}
			out.push_back({pixel, r.second.substr(from)});
		}
		return out;
	}

	TTF_Font* Ui::font(int size) const
	{
		// the kit's sizes: opened the first time one is drawn (the three every page uses are opened by init)
		if (!fonts[size])
		{
			const FontDef& d = FONT_DEFS[size];
			fonts[size] = TTF_OpenFont((fontsDir + "/" + d.file).c_str(), (int)std::lround(d.units * sy));
			if (!fonts[size])
			{
				SDL_Log("font %s: %s", d.file, TTF_GetError());
				return fonts[F_SMALL];
			}
			int minx, maxx, miny, maxy, adv;
			capHeight[size] = TTF_GlyphMetrics(fonts[size], 'H', &minx, &maxx, &miny, &maxy, &adv) == 0 ? maxy : TTF_FontAscent(fonts[size]) * 3 / 4;
		}
		return fonts[size];
	}

	TTF_Font* Ui::fallbackFont(size_t k, int size) const
	{
		TTF_Font*& f = fallbacks[k][size];
		if (!f)
		{
			// (as init opens the first three: Fusion Pixel, a 12-pixel font, at whole multiples of 12)
			int px = (int)std::lround(FONT_DEFS[size].units * sy);
			if (k == 0) px = std::max(12, (int)std::lround(FONT_DEFS[size].units * sy / 12.0) * 12);
			f = TTF_OpenFont((fontsDir + "/" + fallbackFiles[k]).c_str(), px);
			if (!f) return fallbacks[k][F_SMALL];
		}
		return f;
	}

	std::vector<std::pair<TTF_Font*, std::string>> Ui::runs(const std::string& s, int size) const
	{
		// (only Pixelify Sans has the ligature to cut: Inter's sizes pass through)
		return unligated(shapeRuns(s, size), FONT_DEFS[size].pixel ? font(size) : nullptr);
	}

	TTF_Font* Ui::jaFormsFont(int size) const
	{
		if (!jaFormsTried[size])
		{
			jaFormsTried[size] = true;
			// (at Fusion Pixel's own size: the forms sit in its runs)
			int px = std::max(12, (int)std::lround(FONT_DEFS[size].units * sy / 12.0) * 12);
			jaForms[size] = TTF_OpenFont((fontsDir + "/" + FONT_JA_FORMS).c_str(), px);
			if (!jaForms[size]) SDL_Log("font %s: %s", FONT_JA_FORMS, TTF_GetError());
		}
		return jaForms[size];
	}

	std::vector<std::pair<TTF_Font*, std::string>> Ui::shapeRuns(const std::string& s, int size) const
	{
		// (fallback 0 is Fusion Pixel, the CJK font: init lists it first)
		TTF_Font* cjk = fallbacks.empty() ? nullptr : fallbackFont(0, size);
		TTF_Font* ja = cjk && hasKana(s) ? jaFormsFont(size) : nullptr;
		return fontRuns(s, font(size), fallbacks.size(), [&](size_t k) { return fallbackFont(k, size); }, cjk, ja);
	}

	int Ui::textWidth(const std::string& s, int size) const
	{
		std::string k = std::to_string(size) + "|" + s;
		auto it = widthCache.find(k);
		if (it != widthCache.end()) return it->second;
		int total = 0;
		for (auto& r : runs(s, size))
		{
			int w = 0;
			TTF_SizeUTF8(r.first, r.second.c_str(), &w, nullptr);
			total += w;
		}
		if (widthCache.size() > 4096) widthCache.clear();
		widthCache.emplace(std::move(k), total);
		return total;
	}

	SDL_Texture* Ui::text(const std::string& s, int size, SDL_Color c)
	{
		char key[32];
		snprintf(key, sizeof key, "%d %02x%02x%02x|", size, c.r, c.g, c.b);
		std::string k = key + s;
		if (SDL_Texture** hit = cache.find(k)) return *hit;
		parts.textMisses++;
		// each piece in its font, side by side on one baseline
		std::vector<SDL_Surface*> parts;
		int w = 0, ascent = 0, below = 0;
		auto pieces = runs(s.empty() ? " " : s, size);
		std::vector<TTF_Font*> partFonts;
		for (auto& r : pieces)
		{
			SDL_Surface* p = TTF_RenderUTF8_Blended(r.first, r.second.c_str(), c);
			if (!p) continue;
			partFonts.push_back(r.first);
			parts.push_back(p);
			w += p->w;
			ascent = std::max(ascent, TTF_FontAscent(r.first));
			below = std::max(below, p->h - TTF_FontAscent(r.first));
		}
		if (parts.empty()) return nullptr;
		SDL_Surface* surf = parts.size() == 1 ? parts[0] : SDL_CreateRGBSurfaceWithFormat(0, w, ascent + below, 32, SDL_PIXELFORMAT_ARGB8888);
		if (parts.size() > 1)
		{
			int x = 0;
			for (size_t i = 0; i < parts.size(); i++)
			{
				SDL_Surface* p = parts[i];
				SDL_SetSurfaceBlendMode(p, SDL_BLENDMODE_NONE);
				SDL_Rect at{x, ascent - TTF_FontAscent(partFonts[i]), p->w, p->h};
				SDL_BlitSurface(p, nullptr, surf, &at);
				x += p->w;
				SDL_FreeSurface(p);
			}
		}
		if (!surf) return nullptr;
		SDL_Texture* t = SDL_CreateTextureFromSurface(ren, surf);
		SDL_FreeSurface(surf);
		cache.put(k, t);
		return t;
	}

	// align: 0 left of x, 1 right of x, 2 centred on x; text wider than maxW (logical) is cut with "..."
	void Ui::drawText(const std::string& s, int size, SDL_Color c, int x, int y, int maxW, int align)
	{
		drawTextAt(s, size, c, x, y, maxW, align, false);
	}

	void Ui::drawTextMid(const std::string& s, int size, SDL_Color c, int x, int cy, int maxW, int align)
	{
		drawTextAt(s, size, c, x, cy, maxW, align, true);
	}

	void Ui::drawTextAt(const std::string& s, int size, SDL_Color c, int x, int y, int maxW, int align, bool middle,
		const EdgeFade* fade)
	{
		std::string str = s;
		if (maxW > 0)
		{
			int limit = (int)std::lround(maxW * sx);	// (not cut down: maxW·sx of a width measured in units is the line's)
			std::string k = std::to_string(size) + " " + std::to_string(limit) + "|" + s;
			auto hit = elideCache.find(k);
			if (hit != elideCache.end()) str = hit->second;
			else
			{
				if (textWidth(str, size) > limit)
				{
					// the longest UTF-8 prefix whose "..." fits: a binary search over character starts (a long
					// line measured ~10 times, not once per character dropped)
					std::vector<size_t> starts;
					for (size_t i = 0; i < s.size(); i++)
						if (((unsigned char)s[i] & 0xC0) != 0x80) starts.push_back(i);
					size_t lo = 0, hi = starts.size();	// prefixes of starts[0..n) chars: lo fits, hi doesn't
					while (hi - lo > 1)
					{
						size_t mid = (lo + hi) / 2;
						if (textWidth(s.substr(0, starts[mid]) + "...", size) <= limit) lo = mid;
						else hi = mid;
					}
					str = s.substr(0, lo < starts.size() ? starts[lo] : s.size()) + "...";
					if (lo == 0) str = "...";
				}
				if (elideCache.size() > 2048) elideCache.clear();
				elideCache.emplace(std::move(k), str);
			}
		}
		SDL_Texture* t = text(str, size, c);
		if (!t) return;
		int w, h;
		SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
		int px = (int)std::lround(x * sx), py = (int)std::lround(y * sy);
		// middle: the capitals' middle on y (the same for every line in the font, descenders or not)
		if (middle)
		{
			// in another font (Arabic, Thai, CJK...): its box's middle (their capitals aren't Pixelify's)
			bool own = true;
			for (auto& r : runs(str, size)) own = own && r.first == font(size);
			py -= own ? TTF_FontAscent(font(size)) - capHeight[size] / 2 : h / 2;
		}
		if (align == 1) px -= w;
		else if (align == 2) px -= w / 2;
		SDL_Rect dst{px, py, w, h};
		if (fade) drawFaded(t, dst, *fade);
		else SDL_RenderCopy(ren, t, nullptr, &dst);
	}

	void Ui::drawFaded(SDL_Texture* t, SDL_Rect dst, const EdgeFade& fade)
	{
		// the texture in vertical slices: whole between the ramps, and FADE_SLICES slices a ramp, each at its share
		// of the alpha it already has (the marquee's own fade in and out), from nearly whole to nearly nothing
		constexpr int FADE_SLICES = 8;
		Uint8 base = 255;
		SDL_GetTextureAlphaMod(t, &base);
		const int bx0 = (int)std::lround(fade.x0 * sx), bx1 = (int)std::lround(fade.x1 * sx);
		const int rw = std::max(1, (int)std::lround(fade.w * sx));
		auto slice = [&](int a, int b, double share) {
			a = std::max(a, dst.x), b = std::min(b, dst.x + dst.w);
			if (b <= a || share <= 0) return;
			SDL_Rect src{a - dst.x, 0, b - a, dst.h}, d{a, dst.y, b - a, dst.h};
			SDL_SetTextureAlphaMod(t, (Uint8)std::lround(base * share));
			SDL_RenderCopy(ren, t, &src, &d);
		};
		const int midA = fade.left ? bx0 + rw : bx0, midB = fade.right ? bx1 - rw : bx1;
		slice(midA, midB, 1.0);
		for (int k = 0; k < FADE_SLICES; k++)
		{
			const int a = rw * k / FADE_SLICES, b = rw * (k + 1) / FADE_SLICES;
			const double share = (k + 0.5) / FADE_SLICES;	// (the slice's middle: the ramp's height there)
			if (fade.left) slice(bx0 + a, bx0 + b, share);
			if (fade.right) slice(bx1 - b, bx1 - a, share);
		}
		SDL_SetTextureAlphaMod(t, base);
	}

	std::string Ui::playingTitle(const Player& p, const std::string& title, int size, int boxW) const
	{
		const int limit = (int)(boxW * sx);
		std::vector<std::string> names;
		for (const std::string& path : p.paths()) names.push_back(baseName(path));
		return shownTitle(title, titleStarts(p.artist(), p.album(), p.currentPath(), names),
			[&](const std::string& t) { return textWidth(t, size) <= limit; });
	}

	lay::MarqueeAt Ui::marqNow() const
	{
		return lay::marquee((int)(SDL_GetTicks() - marqMover.start), marqMover.textW, marqMover.boxW, marqMover.size, marqMover.passes);
	}

	bool Ui::marqueeMoving() const { return marqUp && panel.on() && !marqNow().done; }

	void Ui::drawMarquee(const std::string& s, int size, SDL_Color c, int x, int cy, int boxW, int passes)
	{
		const int textW = (int)std::ceil(textWidth(s, size) / sx);
		if (!scrollTitles || textW <= boxW || boxW <= 16)
		{
			drawTextMid(s, size, c, x, cy, boxW);
			return;
		}
		const int px = (int)std::lround(FONT_DEFS[size].units);
		// held at its start under a sheet, a menu, the keyboard (and from the start again once they're gone)
		const bool held = !overlays.empty() || ctxOpen || kbOpen || browse->playlists->pickerOpen;
		if (held) marqStarts.clear();
		lay::MarqueeAt m = lay::marquee(0, textW, boxW, px, passes);
		if (!held && marqClaim != marqFrame)
		{
			const std::string key = std::to_string(size) + "|" + std::to_string(boxW) + "|" + std::to_string(passes) + "|" + s;
			if (marqStarts.size() > 64) marqStarts.clear();
			auto it = marqStarts.find(key);
			if (it == marqStarts.end()) it = marqStarts.emplace(key, SDL_GetTicks()).first;
			m = lay::marquee((int)(SDL_GetTicks() - it->second), textW, boxW, px, passes);
			if (!m.done)
			{
				marqClaim = marqFrame;
				marqMover = {key, textW, boxW, px, passes, it->second};
				marqUp = true;
			}
		}
		// the text in its box, moved; its ends fade out in its own alpha (16 units: lay::MARQ_FADE_W), so whatever
		// lies under it — the backdrop, a light theme, a card — shows through, not a band of one colour
		const int lh = (int)std::lround(FONT_DEFS[size].units);
		const Rect box{x, cy - lh, boxW, 2 * lh};
		SDL_Rect oldClip;
		const bool clipped = SDL_RenderIsClipEnabled(ren);
		SDL_RenderGetClipRect(ren, &oldClip);
		SDL_Rect d = devRect(box);
		SDL_RenderSetClipRect(ren, &d);
		SDL_Texture* t = text(s, size, c);
		if (t) SDL_SetTextureAlphaMod(t, (Uint8)std::lround(255 * std::clamp(m.alpha, 0.0f, 1.0f)));
		EdgeFade fade{x, x + boxW, lay::MARQ_FADE_W, m.fadeLeft, m.fadeRight};
		drawTextAt(s, size, c, x - m.offset, cy, 0, 0, true, &fade);
		if (t) SDL_SetTextureAlphaMod(t, 255);
		SDL_RenderSetClipRect(ren, clipped ? &oldClip : nullptr);
	}

	void Ui::fill(Rect r, SDL_Color c)
	{
		SDL_SetRenderDrawBlendMode(ren, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
		// by its edges on the panel's pixels: rects side by side meet, and the same width comes out the same everywhere
		SDL_Rect d = devRect(r);
		SDL_RenderFillRect(ren, &d);
	}

	SDL_Rect Ui::devRect(Rect r) const
	{
		int x0 = (int)std::lround(r.x * sx), y0 = (int)std::lround(r.y * sy);
		int x1 = (int)std::lround((r.x + r.w) * sx), y1 = (int)std::lround((r.y + r.h) * sy);
		if (r.w > 0 && x1 <= x0) x1 = x0 + 1;
		if (r.h > 0 && y1 <= y0) y1 = y0 + 1;
		return {x0, y0, x1 - x0, y1 - y0};
	}

	int Ui::devRadius(int rad) const
	{
		// whole pixels: on the RG DS Plus (x1.6) a 10 is 16, a 14 is 22
		return std::max(0, (int)std::lround(rad * std::min(sx, sy)));
	}

	SDL_Texture* Ui::corner(int R)
	{
		if (R < (int)corners.size() && corners[R]) return corners[R];
		if (R >= (int)corners.size()) corners.resize(R + 1, nullptr);
		// the top-left quarter of a circle centred on (R, R), white, its coverage (4x4 samples a pixel) as alpha:
		// smooth, as BananaOS's menu draws its corners
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, R, R, 32, SDL_PIXELFORMAT_RGBA32);
		if (!s) return nullptr;
		SDL_LockSurface(s);
		for (int y = 0; y < R; y++)
		{
			Uint8* row = (Uint8*)s->pixels + y * s->pitch;
			for (int x = 0; x < R; x++)
			{
				int in = 0;
				for (int j = 0; j < 4; j++)
					for (int i = 0; i < 4; i++)
					{
						float dx = R - (x + (i + 0.5f) / 4), dy = R - (y + (j + 0.5f) / 4);
						in += dx * dx + dy * dy <= (float)R * R;
					}
				row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
				row[x * 4 + 3] = (Uint8)(in * 255 / 16);
			}
		}
		SDL_UnlockSurface(s);
		SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
		SDL_FreeSurface(s);
		if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
		corners[R] = t;
		return t;
	}

	void Ui::roundDev(SDL_Rect d, SDL_Color c, int R, int mask)
	{
		R = std::min({R, d.w / 2, d.h / 2});
		SDL_SetRenderDrawBlendMode(ren, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
		SDL_Texture* t = R > 0 && mask ? corner(R) : nullptr;
		if (!t)
		{
			SDL_RenderFillRect(ren, &d);
			return;
		}
		// the middle full height; each side between its corners (a square one where a corner isn't rounded)
		SDL_Rect mid{d.x + R, d.y, d.w - 2 * R, d.h};
		SDL_Rect left{d.x, d.y + R, R, d.h - 2 * R}, right{d.x + d.w - R, d.y + R, R, d.h - 2 * R};
		SDL_Rect rects[7] = {mid, left, right};
		int n = 3;
		SDL_Rect at[4] = {{d.x, d.y, R, R}, {d.x + d.w - R, d.y, R, R}, {d.x, d.y + d.h - R, R, R}, {d.x + d.w - R, d.y + d.h - R, R, R}};
		for (int k = 0; k < 4; k++)
			if (!(mask & (1 << k))) rects[n++] = at[k];
		SDL_RenderFillRects(ren, rects, n);
		SDL_SetTextureColorMod(t, c.r, c.g, c.b);
		SDL_SetTextureAlphaMod(t, c.a);
		static const SDL_RendererFlip flips[4] = {SDL_FLIP_NONE, SDL_FLIP_HORIZONTAL, SDL_FLIP_VERTICAL,
			(SDL_RendererFlip)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)};
		for (int k = 0; k < 4; k++)
			if (mask & (1 << k)) SDL_RenderCopyEx(ren, t, nullptr, &at[k], 0, nullptr, flips[k]);
	}

	SDL_Texture* Ui::ringCorner(int R, int E)
	{
		int key = R * 256 + E;
		auto it = ringCorners.find(key);
		if (it != ringCorners.end()) return it->second;
		// the top-left quarter of a ring centred on (R, R): outer radius R, inner R - E, coverage as alpha (4x4 samples)
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, R, R, 32, SDL_PIXELFORMAT_RGBA32);
		if (!s) return ringCorners[key] = nullptr;
		SDL_LockSurface(s);
		float ro = (float)R, ri = (float)std::max(0, R - E);
		for (int y = 0; y < R; y++)
		{
			Uint8* row = (Uint8*)s->pixels + y * s->pitch;
			for (int x = 0; x < R; x++)
			{
				int in = 0;
				for (int j = 0; j < 4; j++)
					for (int i = 0; i < 4; i++)
					{
						float dx = R - (x + (i + 0.5f) / 4), dy = R - (y + (j + 0.5f) / 4), d2 = dx * dx + dy * dy;
						in += d2 <= ro * ro && d2 > ri * ri;
					}
				row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
				row[x * 4 + 3] = (Uint8)(in * 255 / 16);
			}
		}
		SDL_UnlockSurface(s);
		SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
		SDL_FreeSurface(s);
		if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
		return ringCorners[key] = t;
	}

	void Ui::strokeRound(Rect r, SDL_Color c, int rad, int e)
	{
		SDL_Rect d = devRect(r);
		int E = std::max(1, (int)std::lround(e * std::min(sx, sy)));
		int R = std::min({devRadius(theme::kitRadius(rad)), d.w / 2, d.h / 2});
		R = std::max(R, E);
		SDL_SetRenderDrawBlendMode(ren, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
		SDL_Rect sides[4] = {{d.x + R, d.y, d.w - 2 * R, E}, {d.x + R, d.y + d.h - E, d.w - 2 * R, E},
			{d.x, d.y + R, E, d.h - 2 * R}, {d.x + d.w - E, d.y + R, E, d.h - 2 * R}};
		SDL_RenderFillRects(ren, sides, 4);
		SDL_Texture* t = ringCorner(R, E);
		if (!t) return;
		SDL_SetTextureColorMod(t, c.r, c.g, c.b);
		SDL_SetTextureAlphaMod(t, c.a);
		SDL_Rect at[4] = {{d.x, d.y, R, R}, {d.x + d.w - R, d.y, R, R}, {d.x, d.y + d.h - R, R, R}, {d.x + d.w - R, d.y + d.h - R, R, R}};
		static const SDL_RendererFlip flips[4] = {SDL_FLIP_NONE, SDL_FLIP_HORIZONTAL, SDL_FLIP_VERTICAL,
			(SDL_RendererFlip)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)};
		for (int k = 0; k < 4; k++) SDL_RenderCopyEx(ren, t, nullptr, &at[k], 0, nullptr, flips[k]);
	}

	void Ui::fillRound(Rect r, SDL_Color c, int rad, int mask)
	{
		roundDev(devRect(r), c, devRadius(theme::kitRadius(rad)), mask);
	}

	void Ui::fillRoundBand(Rect box, int rad, int y0, int y1, SDL_Color c, int x1)
	{
		// the rounded box drawn only between y0 and y1 (and left of x1): a line along its edge keeps its corners
		SDL_Rect d = devRect(box), keep;
		bool had = SDL_RenderIsClipEnabled(ren);
		if (had) SDL_RenderGetClipRect(ren, &keep);
		int top = (int)std::lround(y0 * sy), bottom = (int)std::lround(y1 * sy);
		int right = x1 >= 0 ? (int)std::lround(x1 * sx) : d.x + d.w;
		SDL_Rect band{d.x, top, right - d.x, bottom - top};
		if (had && !SDL_IntersectRect(&band, &keep, &band)) return;
		if (band.w <= 0 || band.h <= 0) return;
		SDL_RenderSetClipRect(ren, &band);
		roundDev(d, c, devRadius(rad), ROUND_ALL);
		SDL_RenderSetClipRect(ren, had ? &keep : nullptr);
	}

	static Ink inkOf(const theme::Palette& t)
	{
		return {sdl(t.BG), sdl(t.TEXT), sdl(t.ACCENT), sdl(t.BUTTON), sdl(t.DIM), sdl(t.DANGER), sdl(t.BUTTON), sdl(t.PRESSED), sdl(t.SURFACE),
			sdl(t.DIM_ON), sdl(t.ON_ACCENT), sdl(t.TRACK), sdl(t.TEXT), sdl(t.RULE), sdl(t.RAIL)};
	}
	Ink ink = inkOf(theme::current);
	void syncInk() { ink = inkOf(theme::current); }

	void Ui::themeChanged()
	{
		syncInk();
		if (listShown) listShown->release();
		int dropped = 0;
		cache.eraseIf([&dropped](const std::string& k) {
			bool pin = k.compare(0, 4, "pin|") == 0;
			dropped += pin;
			return pin;
		});
		if (supportTex)
		{
			SDL_DestroyTexture(supportTex);
			supportTex = nullptr;
			dropped++;
		}
		supportPx = 0;
		fprintf(stderr, "theme: bottom panel recoloured, %d cached textures dropped\n", dropped);
	}

	static theme::Rgb rgb(SDL_Color c) { return {c.r, c.g, c.b}; }

	void Ui::drawLevel(Rect track, int w, SDL_Color trackInk, SDL_Color fillInk)
	{
		// a bar: its track rounded, its fill rounded on its left end only (as the track's corner there)
		fillRound(track, trackInk, R_BAR);
		if (!fillInk.a) fillInk = ACCENT;
		if (w > 0) fillRound({track.x, track.y, std::min(w, track.w), track.h}, fillInk, R_BAR, w >= track.w ? ROUND_ALL : ROUND_TL | ROUND_BL);
	}

	void Ui::drawBox(Rect r, SDL_Color c, int rad, bool focus)
	{
		drawBox(r, c, rad, focus ? ACCENT : sdl(theme::edgeOf(rgb(c))));
	}

	void Ui::drawBox(Rect r, SDL_Color c, int rad, SDL_Color edge)
	{
		if (theme::drawingClean())
		{
			// a fill carries the shape; only an edge that is not the fill's own is the focus ring
			SDL_Color own = sdl(theme::edgeOf(rgb(c)));
			bool focus = edge.r != own.r || edge.g != own.g || edge.b != own.b || edge.a != own.a;
			int R = devRadius(theme::kitRadius(rad));
			SDL_Rect d = devRect(r);
			roundDev(d, c, R, ROUND_ALL);
			if (!focus) return;
			int e = std::max(1, devRadius(theme::EDGE));
			roundDev(d, edge, R, ROUND_ALL);
			SDL_Rect in{d.x + e, d.y + e, d.w - 2 * e, d.h - 2 * e};
			if (in.w > 0 && in.h > 0) roundDev(in, c, std::max(0, R - e), ROUND_ALL);
			return;
		}
		// the edge as the whole box, the lip inside it, the fill over all but the lip's band: 3 rounded fills
		SDL_Rect d = devRect(r);
		int R = devRadius(rad), e = std::max(1, devRadius(theme::EDGE)), lip = devRadius(theme::LIP);
		roundDev(d, edge, R, ROUND_ALL);
		SDL_Rect in{d.x + e, d.y + e, d.w - 2 * e, d.h - 2 * e};
		if (in.w <= 0 || in.h <= 0) return;
		int Ri = std::max(0, R - e);
		roundDev(in, sdl(theme::lipOf(rgb(c))), Ri, ROUND_ALL);
		in.h -= lip;
		if (in.h > 0) roundDev(in, c, Ri, ROUND_ALL);
	}

	void Ui::draw(const Player& p, bool force)
	{
		sameBrightness = p.settings.sameBrightness;
		// (the readers' filter and Extra dim, for the drawing that has no Player: the Manga reader's bottom pages)
		pageReader->readFilterNow = page == Page::DocPages ? p.settings.pdfFilter : p.settings.mangaFilter;
		pageReader->readDimNow = p.settings.readDim;
		mangaOn = p.settings.mangaOn;
		manga->mangaHistOn = p.settings.mangaHistory;
		// before the signature: "Again to clear" goes back to how it was when the second tap would no longer clear
		if (clearArmed && SDL_GetTicks() - clearArmed >= QUIT_WINDOW_MS) clearArmed = 0;
		if (manga->mangaCacheAt && SDL_GetTicks() - manga->mangaCacheAt >= QUIT_WINDOW_MS) manga->mangaCacheAt = 0;
		if (manga->mangaJavaAt && SDL_GetTicks() - manga->mangaJavaAt >= QUIT_WINDOW_MS) manga->mangaJavaAt = 0;
		if (!batteryAt || SDL_GetTicks() - batteryAt >= 10000) readPower();
		playerPage->lockTimer();	// the lock held long enough: released
		if (!panel.on())
		{
			// black once (a computer has no backlight to switch off), then nothing drawn till it is on again
			marqStarts.clear();	// (the Title marquee: from its start again when the panel wakes)
			if (drawn != "off")
			{
				SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
				SDL_RenderClear(ren);
				SDL_RenderPresent(ren);
				drawn = "off";
			}
			return;
		}
		if (page == Page::Info && (infoAt == 0 || SDL_GetTicks() - infoAt >= 1000))
		{
			readInfo(p);
			scroll = std::min(scroll, std::max(0, (int)infoLines.size() - INFO_SHOWN));
		}
		// the search for servers over: its rows say so (Search again)
		if (browse->browseDir == FINDNET && page == Page::Browse && discovery && browse->findWasRunning && !discovery->running())
		{
			browse->findWasRunning = false;
			browse->findRows();
		}
		if (browse->playlists->dropArmed && SDL_GetTicks() - browse->playlists->dropArmed >= QUIT_WINDOW_MS) browse->playlists->dropArmed = 0;
		if (pinArmed && SDL_GetTicks() - pinArmed >= QUIT_WINDOW_MS) pinArmed = 0;
		// a finger held on a folder: its menu (the lift that follows chooses nothing). On a playlist's item, or a file:
		// selecting, that one ticked
		if (holdAt && listTouch && !listScrolled && SDL_GetTicks() - holdAt >= 600)
		{
			holdAt = 0;
			listTouch = false;
			swallowTouch = true;
			bool item = isPlDir(browse->browseDir) && cursor >= 0 && cursor < (int)browse->browseRows.size() && browse->browseRows[cursor].id == PL_ITEM;
			bool file = !isPlDir(browse->browseDir) && cursor >= 0 && cursor < (int)browse->browseRows.size() && browse->browseRows[cursor].id == 2;
			if ((item || file) && browse->playlists->plStore)
			{
				browse->playlists->selStart(item ? SEL_EDIT : SEL_ADD, "");
				swallowTouch = true;
				if (item) browse->playlists->plSel.insert(cursor);
				else
				{
					browse->playlists->picked.insert(browse->browsePaths[cursor]);
					browse->playlists->pickOrder.push_back(browse->browsePaths[cursor]);
				}
			}
			else openBrowseMenu(cursor, MENU_HOLD);
		}
		// a finger held on a Recent card: its menu (the lift that follows chooses nothing)
		if (browse->homeView->homeHoldAt && browse->homeView->homeTouch && !browse->homeView->homeMoved && SDL_GetTicks() - browse->homeView->homeHoldAt >= 500)
		{
			int i = browse->homeView->homePress;
			browse->homeView->homeHoldAt = 0;
			browse->homeView->homeTouch = false;
			browse->homeView->homePress = -1;
			swallowTouch = true;
			if (i == Home::HOME_PRESS_CONTINUE)
			{
				// the Continue / Now playing card's own More (its file's sheet, Home's when it's not in Recent)
				Home::Continue c = browse->homeView->continueOf(p);
				if (c.shown && !c.path.empty() && browse->homeView->findRecent(c.path)) browse->homeView->openRecentMenu(c.path);
				else browse->homeView->openHomeOptions();
			}
			else if (i >= 0 && i < browse->homeView->stripCards())
			{
				browse->homeView->homeZone = Home::HOME_STRIP;
				browse->homeView->setStrip(i);
				browse->homeView->openRecentMenu(browse->homeView->recent[i].path);
			}
		}
		browse->playlists->plCheckShown();
		// the top sheet's smooth list, else the Add to… picker's while it is open over the page, else the page's, its
		// glide stepped to now (its offset is not in the signature: drawnList). A page's list under a sheet stays
		// still: its slots are let go and drawn again once
		Overlay* top = topOverlay();
		SmoothList* list = top ? top->smoothList() : nullptr;
		if (!list && browse->playlists->pickerOpen) list = &browse->playlists->pickerSmooth();
		if (!list && area()) list = area()->smoothList();
		if (list != listShown)
		{
			if (listShown) listShown->release();
			listShown = list;
		}
		if (list) list->step(SDL_GetTicks());
		// the left stick scrolls a list (a full push is the view's height a second); a finger on it wins
		{
			double sv = list && !list->sc.held() ? stickScrollSpeed(stickLY, list->spec().view.h) : 0;
			Uint32 now = SDL_GetTicks();
			if (sv != 0)
			{
				Uint32 dt = stickListAt ? std::min<Uint32>(now - stickListAt, 50) : 16;
				stickListAt = now;
				list->sc.jump(list->sc.offset() + sv * dt / 1000.0);
			}
			else stickListAt = 0;
			stickListOn = sv != 0;
		}
		std::string sig = signature(p);
		if (page == Page::Info || page == Page::About)
			for (auto& l : infoLines) sig += l.label + "=" + l.text + ";";
		std::string listSig = list ? std::to_string(list->shownPx()) : "";
		if (!force && sig == drawn && listSig == drawnList) return;
		drawn = sig;
		drawnList = listSig;

		// every frame is drawn whole, a smooth list's rows placed from their slots: no frame kept in a texture with
		// only the rows composed over it (a texture copied into a texture is where GLES drivers turn pictures upside
		// down, and the handheld's full frames fit the 16 ms anyway: 2026-11-21-smooth-scroll.md)
		parts = FrameParts{};
		frameAtMs = SDL_GetTicks();
		const uint64_t costT0 = SDL_GetPerformanceCounter();
		renderPage(p);
		// the dark veil (ui.cpp's step): over everything (no words: before Screen off it stands in for the backlight's dim)
		if (veilAlpha > 0)
		{
			SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(ren, 0, 0, 0, (Uint8)veilAlpha);
			SDL_RenderFillRect(ren, nullptr);
			SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
		}
		const uint64_t costT1 = SDL_GetPerformanceCounter();
		SDL_RenderPresent(ren);
		const uint64_t costT2 = SDL_GetPerformanceCounter();
		parts.present = (costT2 - costT1) * 1000.0 / (double)SDL_GetPerformanceFrequency();
		noteDrawCost(p, costT0, costT1, costT2);
		if (list) list->noteFrame(parts);
	}

	int Ui::glideWaitMs() const
	{
		if (!listShown || !listShown->moving() || !panel.on()) return -1;
		// a frame every 16 ms from the last one's start (the loop's own work comes out of the wait, not on top)
		uint32_t since = SDL_GetTicks() - frameAtMs;
		return since >= 16 ? 0 : (int)(16 - since);
	}

	void Ui::noteDrawCost(const Player& p, uint64_t t0, uint64_t t1, uint64_t t2)
	{
		// the player page's redraws with Match album art on, in the log every 5 s: what a marquee step or a slider
		// frame costs the loop (the top panel's visualizer waits on it), so a slow handheld names its own number
		if (page != Page::Main || !backdropOn(p)) { drawCost = DrawCost{}; return; }
		const double f = 1000.0 / (double)SDL_GetPerformanceFrequency();
		DrawCost& c = drawCost;
		if (!c.since) c.since = t0;
		c.draws++;
		c.pageMs += (t1 - t0) * f;
		c.presentMs += (t2 - t1) * f;
		c.maxMs = std::max(c.maxMs, (t2 - t0) * f);
		if ((t2 - c.since) * f < 5000) return;
		if (drawCostOf != bgNow.hash) drawCostOf = bgNow.hash, drawCostLogs = 0;
		if (drawCostLogs >= 3) { c = DrawCost{}; return; }
		drawCostLogs++;
		fprintf(stderr, "timing: player page %d redraws in 5 s with the backdrop: %.2f ms each (the ground %.2f, present %.2f), max %.2f\n",
			c.draws, (c.pageMs + c.presentMs) / c.draws, c.groundMs / c.draws, c.presentMs / c.draws, c.maxMs);
		c = DrawCost{};
	}

	void Ui::renderPage(const Player& p)
	{
		// (the Clean style follows the theme: setTheme sets theme::clean; High contrast alone is Classic)
		theme::classicDepth = 0;
		// the Title marquee: a new frame (the first title drawn claims the panel); another page: titles start again
		marqFrame++;
		marqUp = false;
		if ((int)page != marqPage)
		{
			marqPage = (int)page;
			marqStarts.clear();
		}
		scrollTitles = p.settings.scrollTitles;
		showHidden = p.settings.showHiddenFolders;
		// (each part's cost, for the timing lines: Ui::parts)
		uint64_t at = SDL_GetPerformanceCounter();
		auto lap = [&at](double& part) {
			uint64_t now = SDL_GetPerformanceCounter();
			part += (now - at) * 1000.0 / (double)SDL_GetPerformanceFrequency();
			at = now;
		};
		SDL_SetRenderDrawColor(ren, BG.r, BG.g, BG.b, 255);
		SDL_RenderClear(ren);
		// Match album art behind the browser: the canvas's lower half, one copy (the player page draws its own)
		groundIsBackdrop = !galleryNew && browserBackdrop(p);
		if (groundIsBackdrop) drawBackdrop(barPlayer(p), BG_LIST);
		lap(parts.ground);
		if (galleryNew) { statusHit = {0, 0, 0, 0}; drawGalleryNew(); return; }	// (test only: --press gallery:new)
		webInd = {0, 0, 0, 0};	// (drawBattery puts it back where it draws it)
		statusHit = {0, 0, 0, 0};
		sleepHit = {0, 0, 0, 0};	// (the header that draws the badge puts it back)
		// what plays behind the browser: its row's chip, the header's mark (drawBattery says whether it moved)
		setChip(p);
		markAnim = false;
		loaderShown = false;
		// a section page's body is drawn by its area in the shell's body box
		bool shellPage = shell->page(page);
		// Manga's pages: their sheets wait for the frame (drawn full size over it), their old header's title and
		// hints are caught for the shell's
		bool mangaShell = shellPage && manga->shellPage(page);
		manga->sheetsLater = mangaShell;
		manga->sheetQueue.clear();
		hdrCapture = mangaShell;
		capTitle.clear();
		capHints.clear();
		if (page == Page::Info || page == Page::About) drawInfo(p);
		else if (Area* a = area()) a->draw(p);
		hdrCapture = false;
		if (mangaShell) manga->drawShellBody(p);
		lap(parts.body);
		if (shellPage) shell->draw(shell->section(p), shell->head(p), p);
		lap(parts.shell);
		if (mangaShell) manga->runLater();
		manga->sheetsLater = false;
		browse->drawClipBar();
		// (the browse menu is a More sheet: drawn with the overlays)
			// (the mini bar's hold menu opens on pages of its own too)
		if (browse->playlists->pickerOpen) browse->playlists->drawPicker();
		if (browse->playlists->addPending.serial && page == Page::Browse) browse->playlists->drawBusy();
		if (popupShown(p)) drawUpdatePopup();
		manga->drawExtNotice();
		if (kbOpen) drawKeyboard();
		drawOverlays(p);
		drawFileJob();	// (over everything: it holds the panel until it ends)
		lap(parts.rest);
	}

	std::string Ui::partsLine() const
	{
		const FrameParts& f = parts;
		char buf[240];
		snprintf(buf, sizeof buf, "ground %.1f, body %.1f (list %.1f, %d rows drawn), shell %.1f, rest %.1f, present %.1f; "
			"%d text misses", f.ground, f.body, f.list, f.rowsDrawn, f.shell, f.rest, f.present, f.textMisses);
		return buf;
	}

	void Ui::drawLoader(int cx, int cy, const std::string& text)
	{
		loaderShown = true;
		int step = (int)(SDL_GetTicks() / 110 % 8);
		for (int k = 0; k < 8; k++)
		{
			int age = ((step - k) % 8 + 8) % 8;	// 0: the head
			double a = k * 3.14159265 / 4;
			int dx = cx + (int)std::lround(16 * std::sin(a)), dy = cy - 14 + (int)std::lround(-16 * std::cos(a));
			SDL_Color c = age == 0 ? ACCENT : age < 3 ? TEXT : DIM;
			fillRound({dx - 3, dy - 3, 6, 6}, c, 3);
		}
		if (!text.empty()) drawTextMid(text, 0, DIM, cx, cy + 22, 560, 2);
	}

	void Ui::readPower()
	{
		const char* base = getenv("BP_POWER_SUPPLY");
		battery = readBattery(base ? base : "/sys/class/power_supply");
		batteryAt = SDL_GetTicks();
	}

	std::string Ui::clockText(const Player& p) const
	{
		// (not over a photo: its page is left as it is, as the web's globe is)
		if (p.settings.clock == CLOCK_OFF || (page == Page::Main && p.playingPhoto())) return "";
		time_t now = time(nullptr);
		struct tm t;
		if (!localtime_r(&now, &t)) return "";
		return formatClock(t.tm_hour, t.tm_min, p.settings.clock == CLOCK_12);
	}

	int Ui::statusLeft(const Player& p) const
	{
		// as drawBattery lays them out, right to left: the battery (its percent, a bolt), the globe, the clock
		int left = 616;
		if (battery.percent >= 0)
		{
			left = 616 - 33;
			if (p.settings.batteryPercent) left -= 6 + (int)(textWidth(std::to_string(battery.percent) + "%", 0) / sx) + 4;
			if (battery.charging) left -= 14;
		}
		if (webIndicatorShown(p)) left = (battery.percent >= 0 ? left - 8 : left) - 18;
		std::string c = clockText(p);
		if (!c.empty()) left -= (left == 616 ? 0 : 10) + (int)std::lround(textWidth(c, 0) / sx);
		left -= markW(p);
		left -= incognitoW();
		if (int bw = sleepBadgeW(p)) left -= bw + 8;
		return left;
	}

	int Ui::sleepBadgeW(const Player& p) const
	{
		// a running sleep timer's badge, left of the status marks on every page (not over a photo, as the clock)
		if (!sleep->running() || (page == Page::Main && p.playingPhoto())) return 0;
		return lay::badgeW(kit->textW(lay::caps(sleep->label()), F_I11B), true);
	}

	int Ui::markW(const Player& p) const
	{
		// something plays behind the browser (not on the player itself, nor a photo)
		return bgPlay && page != Page::Main && p.playing() && !p.playingPhoto() ? 18 + 10 : 0;
	}

	bool Ui::markLive(const Player& p) const
	{
		if (pageDrives(p)) return pagePlaying();
		return !p.paused && !p.loading() && p.error.empty();
	}

	void Ui::drawMark(int right, int cy, const Player& p)
	{
		// three bars, moving while it plays (a frame about 6 times a second), dim and still while paused
		bool live = markLive(p);
		double t = (SDL_GetTicks() / 166) * 166 / 160.0;
		for (int i = 0; i < 3; i++)
		{
			int h = live ? 5 + (int)std::lround(std::fabs(std::sin(t + i * 1.7)) * 11) : 5;
			fillRound({right - 18 + i * 7, cy + 8 - h, 4, h}, live ? ACCENT : DIM, 1);
		}
		markAnim = live;
	}

	void Ui::drawBattery(int right, int y, const Player& p, int minX)
	{
		// the clock, left of the rest (as statusLeft has it); dim while touch is locked
		// (then the playing mark, left of it)
		// the whole cluster is one touch target (F opens Quick settings); the globe inside keeps its own tap
		// (statusLeft lays the cluster out from 616; a header whose cluster ends elsewhere, the player's, shifts it)
		int loud = statusLeft(p) - (616 - right);
		// the sleep timer's badge first (its own tap: the sheet), the cluster's touch target after it
		if (int bw = sleepBadgeW(p))
		{
			if (loud >= minX) kit->badge(loud + bw, y + 8, sleep->badge());
			sleepHit = {loud - 4, 0, bw + 8, 54};
			loud += bw + 8;
		}
		statusHit = loud < right ? Rect{loud - 8, 0, right - (loud - 8), 54} : Rect{0, 0, 0, 0};
		auto clock = [&](int edge) {
			std::string c = clockText(p);
			int cr = edge - (edge == right ? 0 : 10);
			if (!c.empty())
			{
				if (cr - (int)std::lround(textWidth(c, 0) / sx) < minX) return;
				drawTextMid(c, 0, TEXT, cr, y + 8, 0, 1);
				cr -= (int)std::lround(textWidth(c, 0) / sx) + 10;
			}
			if (markW(p) && cr - 18 >= minX)
			{
				drawMark(cr, y + 8, p);
				cr -= 18 + 10;
			}
			// Incognito mode, while it's on (Quick settings)
			if (incognitoW() && cr - 18 >= minX) drawGlyphMid(G_INCOGNITO, cr - 9, y + 8, ACCENT);
		};
		if (battery.percent < 0)
		{
			drawWebIndicator(right, y + 8, p);
			clock(webIndicatorShown(p) ? right - 18 : right);
			return;
		}
		// the body (an outline, filled to the charge) and its nub; the percent before it, the bolt before that
		const int bw = 30, bh = 16;
		int x = right - bw - 3;
		SDL_Color ink = battery.charging ? ACCENT : battery.percent <= 15 ? DANGER : TEXT;
		fill({x, y, bw, bh}, TEXT);
		fill({x + 2, y + 2, bw - 4, bh - 4}, BG);
		int level = (bw - 6) * battery.percent / 100;
		if (level > 0) fill({x + 3, y + 3, level, bh - 6}, ink);
		fill({x + bw, y + 5, 3, bh - 10}, TEXT);
		int left = x;
		if (p.settings.batteryPercent)
		{
			std::string pct = std::to_string(battery.percent) + "%";
			drawText(pct, 0, battery.percent <= 15 && !battery.charging ? DANGER : TEXT, x - 6, y - 2, 0, 1);
			left = x - 6 - (int)(textWidth(pct, 0) / sx) - 4;
		}
		if (battery.charging)
		{
			// a lightning bolt, 5x7 cells of 2
			static const char* BOLT[7] = {"...##", "..##.", ".##..", "#####", "..##.", ".##..", "##..."};
			int bx = left - 14, by = y + 1;
			for (int r = 0; r < 7; r++)
				for (int c = 0; c < 5; c++)
					if (BOLT[r][c] == '#') fill({bx + c * 2, by + r * 2, 2, 2}, ACCENT);
			left = bx;
		}
		drawWebIndicator(left - 8, y + 8, p);
		clock(webIndicatorShown(p) ? left - 8 - 18 : left);
	}

	std::string Ui::updateStatus() const
	{
		if (!updater || updater->state() == Updater::State::Off)
			return updater && updater->fromStore() ? "From the Store" : std::string("This is ") + BP_VERSION;
		switch (updater->state())
		{
		case Updater::State::Checking: return "Checking...";
		case Updater::State::UpToDate: return "Up to date (" + updater->current() + ")";
		case Updater::State::Available: case Updater::State::Downloading: return updater->available() + " is out";
		case Updater::State::Ready: return updater->available() + " ready";
		case Updater::State::Failed: return "Didn't work";
		default: return "This is " + updater->current();
		}
	}

	SDL_Surface* Ui::capture(const Player& p)
	{
		// drawn afresh and read back before it's shown (after a present, the frame's contents are gone)
		renderPage(p);
		int w, h;
		SDL_GetRendererOutputSize(ren, &w, &h);
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
		if (s && SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ABGR8888, s->pixels, s->pitch) != 0)
		{
			SDL_FreeSurface(s);
			s = nullptr;
		}
		SDL_RenderPresent(ren);
		drawn.clear();
		return s;
	}

	// ---- the pieces every page is made of ----

	void Ui::drawBackIcon()
	{
		Rect b = buttonRect(BACK_ICON);
		drawBox(b, pressedBtn == BACK_ICON ? PRESSED : BUTTON, R_BUTTON);
		// adding files to a playlist: a ✕, it cancels (B goes up a folder there)
		bool cancel = page == Page::Browse && (browse->playlists->selMode == SEL_ADD || browse->playlists->selMode == SEL_PICK);
		if (cancel) drawGlyphMid(G_CROSS, b.x + b.w / 2, b.y + b.h / 2, TEXT, 2);
		else drawGlyphMid(G_BACK, b.x + b.w / 2 - 1, b.y + b.h / 2, TEXT, 3);
	}

	void Ui::drawHeader(const std::string& title, const Player& p, int titleRight)
	{
		// (an old body in the shell: its title is the shell header's; nothing of the old header is drawn)
		if (hdrCapture)
		{
			if (capTitle.empty()) capTitle = title;
			return;
		}
		// (Home has no "<": its title takes the place, lined up with the page's 16-unit margin)
		bool back = backIconShown(p);
		if (back) drawBackIcon();
		int x = back ? 72 : 16;
		int room = 616 - x - (battery.percent >= 0 ? BATTERY_W : 0) - (webIndicatorShown(p) ? 26 : 0);
		// the clock's room too (none over a photo: its title as it always was)
		if (!clockText(p).empty() || markW(p)) room = std::min(room, statusLeft(p) - 12 - x);
		if (titleRight) room = std::min(room, titleRight - 12 - x);
		drawTextMid(title, 2, TEXT, x, 32, room);
		drawBattery(616, 24, p);
	}

	void Ui::drawTab(Rect r, const char* name, bool active, int size)
	{
		if (theme::drawingClean())
		{
			SDL_Color fill = active ? sdl(theme::tintOf(theme::BG)) : BUTTON;
			fillRound(r, fill, R_BUTTON);
			drawTextMid(name, size, active ? ACCENT : TEXT, r.x + r.w / 2, r.y + r.h / 2, r.w - 16, 2);
			return;
		}
		drawBox(r, active ? ACCENT : BUTTON, R_BUTTON);
		drawTextMid(name, size, active ? ON_ACCENT : TEXT, r.x + r.w / 2, r.y + r.h / 2, r.w - 16, 2);
	}

	void Ui::drawHints(const std::vector<Hint>& hints)
	{
		if (hdrCapture)
		{
			capHints = hints;	// (the shell's hint bar draws them)
			return;
		}
		// the button's name in yellow (as on the buttons), what it does in grey; centred along the bottom
		auto w = [&](const std::string& t) { return t.empty() ? 0 : (int)std::lround(textWidth(t, 0) / sx); };
		int gap = 24, total = 0;
		for (auto& h : hints) total += w(h.key) + (h.key.empty() ? 0 : 7) + w(h.verb);
		if (total + gap * ((int)hints.size() - 1) > 612) gap = 14;
		total += gap * ((int)hints.size() - 1);
		int x = W / 2 - total / 2;
		for (auto& h : hints)
		{
			if (!h.key.empty())
			{
				drawTextMid(h.key, 0, ACCENT, x, footY());
				x += w(h.key) + 7;
			}
			drawTextMid(h.verb, 0, DIM, x, footY());
			x += w(h.verb) + gap;
		}
	}

	void Ui::drawFooterText(const std::string& text, SDL_Color c)
	{
		if (hdrCapture)
		{
			capHints = {{"", text}};
			return;
		}
		drawTextMid(text, 0, c, W / 2, footY(), 612, 2);
	}

	void Ui::drawFocus(Rect r)
	{
		// the cursor: the row lit as a button, its edge yellow
		drawBox(r, BUTTON, R_BUTTON, true);
	}

	void Ui::drawScrollBar(int top, int height, int first, int shown, int total)
	{
		// along the right edge: where the window is in the list (nothing when it all shows)
		if (total <= shown) return;
		fillRound({630, top, 4, height}, SURFACE, 2);
		int h = std::max(16, height * shown / total);
		int y = top + (height - h) * first / std::max(1, total - shown);
		fillRound({630, y, 4, h}, DIM, 2);
	}

	void Ui::drawEmpty(const Glyph& g, const std::string& title, const std::string& sub, const char* action, const Glyph* btn)
	{
		const Glyph& bg = btn ? *btn : G_PLUS;
		// in the middle of the page: a big mark, what it is, what to do
		int cy = action ? 196 : 216;
		drawGlyphMid(g, W / 2, cy - 36, DIM, 4);
		drawTextMid(title, 1, TEXT, W / 2, cy + 16, 560, 2);
		for (size_t a = 0, k = 0; a <= sub.size() && !sub.empty(); k++)
		{
			size_t e = std::min(sub.find('\n', a), sub.size());
			drawTextMid(sub.substr(a, e - a), 0, DIM, W / 2, cy + 46 + (int)k * 24, 560, 2);
			a = e + 1;
		}
		if (action)
		{
			Rect b = buttonRect(EMPTY_BTN);
			bool at = cursor == 0;
			drawBox(b, pressedBtn == EMPTY_BTN ? PRESSED : BUTTON, R_BUTTON, at);
			int tw = (int)std::lround(textWidth(action, 1) / sx), gw = glyphW(bg);
			int x = b.x + b.w / 2 - (tw + gw + 10) / 2;
			drawGlyphMid(bg, x + gw / 2, b.y + b.h / 2, at ? ACCENT : TEXT);
			drawTextMid(action, 1, at ? ACCENT : TEXT, x + gw + 10, b.y + b.h / 2);
		}
	}

	std::string Ui::elideLeft(const std::string& s, int size, int maxW) const
	{
		// the end kept (a path's last folders): "...Movies/Season 1"
		int limit = (int)(maxW * sx);
		if (textWidth(s, size) <= limit) return s;
		std::string t = s;
		while (!t.empty() && textWidth("..." + t, size) > limit)
		{
			size_t n = 1;
			while (n < t.size() && ((unsigned char)t[n] & 0xC0) == 0x80) n++;
			t.erase(0, n);
		}
		return "..." + t;
	}

	int Ui::glyphW(const Glyph& g, int cell) const
	{
		int w = 0;
		for (int r = 0; r < g.h; r++) w = std::max(w, (int)strlen(g.rows[r]));
		return w * cell;
	}

	void Ui::drawGlyph(const Glyph& g, int x, int y, SDL_Color c, int cell)
	{
		// on the panel's own pixels: every cell the same size (at 1.6x a cell of 2 is 3 pixels, never 3 and 4 in turn)
		int cs = std::max(1, (int)std::lround(cell * sx)), cy = std::max(1, (int)std::lround(cell * sy));
		int px = (int)std::lround(x * sx), py = (int)std::lround(y * sy);
		// the picture centred where its logical box would be (its pixel size can be a little smaller)
		int gw = glyphW(g, 1);
		px += (int)std::lround((gw * cell * sx - gw * cs) / 2);
		py += (int)std::lround((g.h * cell * sy - g.h * cy) / 2);
		SDL_SetRenderDrawBlendMode(ren, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
		for (int r = 0; r < g.h; r++)
		{
			const char* row = g.rows[r];
			// runs of cells as one rect
			for (int i = 0; row[i];)
			{
				if (row[i] != '#') { i++; continue; }
				int j = i;
				while (row[j] == '#') j++;
				SDL_Rect d{px + i * cs, py + r * cy, (j - i) * cs, cy};
				SDL_RenderFillRect(ren, &d);
				i = j;
			}
		}
	}

	void Ui::drawGlyphMid(const Glyph& g, int cx, int cy, SDL_Color c, int cell)
	{
		drawGlyph(g, cx - glyphW(g, cell) / 2, cy - g.h * cell / 2, c, cell);
	}

	void Ui::drawRowIcon(RowIcon icon, int x, int y, SDL_Color c)
	{
		// each in a 22 x 20 box (logical units)
		const Glyph* g = icon == ICON_RECENT ? &G_CLOCK : icon == ICON_PLACE ? &G_CARD : icon == ICON_FOLDER ? &G_FOLDER
			: icon == ICON_VIDEO ? &G_FILM : icon == ICON_SONG ? &G_NOTE : icon == ICON_PHOTO ? &G_PICTURE
			: icon == ICON_DOC ? &G_PAGE : icon == ICON_BOOK ? &G_BOOK : icon == ICON_FILE ? &G_FILE
			: icon == ICON_PDF ? &G_PDF : icon == ICON_EPUB ? &G_EPUB : icon == ICON_COMIC ? &G_COMIC
			: icon == ICON_NETWORK ? &G_NETWORK : icon == ICON_REFRESH ? &G_REFRESH : icon == ICON_TYPE ? &G_PENCIL
			: icon == ICON_TEST ? &G_PLUG : icon == ICON_SAVE ? &G_CHECK : icon == ICON_PLAYLIST ? &G_PLAYLIST
			: icon == ICON_LIKED ? &G_HEART : icon == ICON_NEW ? &G_PLUS : nullptr;
		if (g) drawGlyphMid(*g, x + 11, y + 10, c);
	}

	std::vector<std::string> Ui::wrapText(const std::string& text, int size, int maxW, int maxLines) const
	{
		// at spaces where it can, the last line cut with "..." when there's more
		std::vector<std::string> lines;
		std::string rest = text;
		int limit = (int)(maxW * sx);
		while (!rest.empty() && (int)lines.size() < maxLines)
		{
			// the last line: what fits of it is all that's drawn ("..." after), so a long rest is cut first
			if ((int)lines.size() == maxLines - 1)
			{
				lines.push_back(rest.size() > 400 ? rest.substr(0, 400) : rest);
				break;
			}
			if (textWidth(rest, size) <= limit) { lines.push_back(rest); break; }
			// the longest prefix that fits, by a binary search over character starts (measuring every prefix of a
			// long description, on every redraw, took a quarter of a second on the handheld)
			std::vector<size_t> ends;	// byte length of each prefix of 1, 2, ... characters
			for (size_t i = 1; i <= rest.size(); i++)
				if (i == rest.size() || ((unsigned char)rest[i] & 0xC0) != 0x80) ends.push_back(i);
			size_t lo = 0, hi = ends.size();	// prefixes of lo characters fit (0 trivially), of hi don't
			while (hi - lo > 1)
			{
				size_t mid = (lo + hi) / 2;
				if (textWidth(rest.substr(0, ends[mid - 1]), size) <= limit) lo = mid;
				else hi = mid;
			}
			size_t fit = lo ? ends[lo - 1] : 0;
			if (fit == 0) fit = ends.empty() ? rest.size() : ends[0];
			size_t brk = rest.find_last_of(' ', fit);
			if (brk != std::string::npos && brk > fit / 2) fit = brk + 1;
			std::string line = rest.substr(0, fit);
			while (!line.empty() && line.back() == ' ') line.pop_back();
			lines.push_back(line);
			rest = rest.substr(fit);
		}
		return lines;
	}

	void Ui::drawButton(Rect b, const Glyph* g, const std::string& label, const char* key, bool off, int size, bool lit)
	{
		// a label (a glyph before it), the button that does the same under it in yellow (Button hotkeys on)
		bool hint = key && *key;
		int mid = hint ? b.y + b.h * 2 / 5 : b.y + b.h / 2;
		SDL_Color ink = off ? DIM : lit ? ACCENT : TEXT;
		int tw = label.empty() ? 0 : std::min(b.w - 12, (int)std::lround(textWidth(label, size) / sx));
		int gw = g ? glyphW(*g) : 0, gap = g && tw ? 8 : 0;
		int x = b.x + b.w / 2 - (gw + gap + tw) / 2;
		if (g) drawGlyphMid(*g, x + gw / 2, mid, ink);
		if (tw) drawTextMid(label, size, ink, x + gw + gap, mid, b.w - 12 - gw - gap);
		if (hint) drawTextMid(key, 0, off ? DIM : ACCENT, b.x + b.w / 2, b.y + b.h * 4 / 5 - 2, b.w - 6, 2);
	}
}
