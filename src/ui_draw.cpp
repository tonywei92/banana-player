#include "ui.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include "theme.h"
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

	std::vector<std::pair<TTF_Font*, std::string>> Ui::runs(const std::string& s, int size) const
	{
		return unligated(shapeRuns(s, size), fonts[size]);
	}

	std::vector<std::pair<TTF_Font*, std::string>> Ui::shapeRuns(const std::string& s, int size) const
	{
		// the characters, each with its bytes
		std::vector<std::pair<Uint32, std::string>> chars;
		for (size_t i = 0; i < s.size();)
		{
			unsigned char c = s[i];
			Uint32 cp = c;
			int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
			if (n > 1)
			{
				cp = c & (0x7F >> n);
				for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
			}
			chars.push_back({cp, s.substr(i, n)});
			i += n;
		}
		auto hasAll = [&](TTF_Font* f) {
			for (auto& c : chars)
				if (c.first >= 0x20 && !TTF_GlyphIsProvided32(f, c.first)) return false;
			return true;
		};
		if (hasAll(fonts[size])) return {{fonts[size], s}};
		for (auto& f : fallbacks)
			if (hasAll(f[size])) return {{f[size], s}};
		// no one font has them all: Pixelify Sans where it can, else the first fallback that has the character
		std::vector<std::pair<TTF_Font*, std::string>> out;
		for (auto& c : chars)
		{
			TTF_Font* f = fonts[size];
			if (c.first >= 0x20 && !TTF_GlyphIsProvided32(f, c.first))
				for (auto& fb : fallbacks)
					if (TTF_GlyphIsProvided32(fb[size], c.first)) { f = fb[size]; break; }
			if (!out.empty() && out.back().first == f) out.back().second += c.second;
			else out.push_back({f, c.second});
		}
		return out;
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
		auto it = cache.find(k);
		if (it != cache.end()) return it->second;
		if (cache.size() > 256)
		{
			for (auto& t : cache) SDL_DestroyTexture(t.second);
			cache.clear();
		}
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
		cache[k] = t;
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

	void Ui::drawTextAt(const std::string& s, int size, SDL_Color c, int x, int y, int maxW, int align, bool middle)
	{
		std::string str = s;
		if (maxW > 0)
		{
			int limit = (int)(maxW * sx);
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
			for (auto& r : runs(str, size)) own = own && r.first == fonts[size];
			py -= own ? TTF_FontAscent(fonts[size]) - capHeight[size] / 2 : h / 2;
		}
		if (align == 1) px -= w;
		else if (align == 2) px -= w / 2;
		SDL_Rect dst{px, py, w, h};
		SDL_RenderCopy(ren, t, nullptr, &dst);
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

	void Ui::fillRound(Rect r, SDL_Color c, int rad, int mask)
	{
		roundDev(devRect(r), c, devRadius(rad), mask);
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
		mangaOn = p.settings.mangaOn;
		mangaHistOn = p.settings.mangaHistory;
		// before the signature: "Again to clear" goes back to how it was when the second tap would no longer clear
		if (clearArmed && SDL_GetTicks() - clearArmed >= QUIT_WINDOW_MS) clearArmed = 0;
		if (mangaCacheAt && SDL_GetTicks() - mangaCacheAt >= QUIT_WINDOW_MS) mangaCacheAt = 0;
		if (mangaJavaAt && SDL_GetTicks() - mangaJavaAt >= QUIT_WINDOW_MS) mangaJavaAt = 0;
		if (!batteryAt || SDL_GetTicks() - batteryAt >= 10000) readPower();
		// the lock held long enough: released (the finger still down presses nothing)
		if (touchLocked && lockHoldAt && SDL_GetTicks() - lockHoldAt >= UNLOCK_HOLD_MS)
		{
			touchLocked = false;
			lockHoldAt = 0;
			swallowTouch = true;	// the button shows "Lock" again
		}
		if (!touchLocked) lockHoldAt = 0;
		if (!panel.on())
		{
			// black once (a computer has no backlight to switch off), then nothing drawn till it is on again
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
		if (browseDir == FINDNET && page == Page::Browse && discovery && findWasRunning && !discovery->running())
		{
			findWasRunning = false;
			findRows();
		}
		if (dropArmed && SDL_GetTicks() - dropArmed >= QUIT_WINDOW_MS) dropArmed = 0;
		if (pinArmed && SDL_GetTicks() - pinArmed >= QUIT_WINDOW_MS) pinArmed = 0;
		// a finger held on a folder: its menu (the lift that follows chooses nothing). On a playlist's item, or a file:
		// selecting, that one ticked
		if (holdAt && listTouch && !listScrolled && SDL_GetTicks() - holdAt >= 600)
		{
			holdAt = 0;
			listTouch = false;
			swallowTouch = true;
			bool item = isPlDir(browseDir) && cursor >= 0 && cursor < (int)browseRows.size() && browseRows[cursor].id == PL_ITEM;
			bool file = !isPlDir(browseDir) && cursor >= 0 && cursor < (int)browseRows.size() && browseRows[cursor].id == 2;
			if ((item || file) && plStore)
			{
				selStart(item ? SEL_EDIT : SEL_ADD, "");
				swallowTouch = true;
				if (item) plSel.insert(cursor);
				else
				{
					picked.insert(browsePaths[cursor]);
					pickOrder.push_back(browsePaths[cursor]);
				}
			}
			else openBrowseMenu(cursor, MENU_HOLD);
		}
		// a finger held on a Recent card: its menu (the lift that follows chooses nothing)
		if (homeHoldAt && homeTouch && !homeMoved && SDL_GetTicks() - homeHoldAt >= 500)
		{
			int i = homePress;
			homeHoldAt = 0;
			homeTouch = false;
			homePress = -1;
			swallowTouch = true;
			if (i >= 0 && i < (int)recent.size())
			{
				homeZone = HOME_STRIP;
				setStrip(i);
				openRecentMenu(recent[i].path);
			}
		}
		plCheckShown();
		std::string sig = signature(p);
		if (page == Page::Info || page == Page::About)
			for (auto& l : infoLines) sig += l.label + "=" + l.text + ";";
		if (!force && sig == drawn) return;
		drawn = sig;

		renderPage(p);
		SDL_RenderPresent(ren);
	}

	void Ui::renderPage(const Player& p)
	{
		showHidden = p.settings.showHiddenFolders;
		SDL_SetRenderDrawColor(ren, BG.r, BG.g, BG.b, 255);
		SDL_RenderClear(ren);
		webInd = {0, 0, 0, 0};	// (drawBattery puts it back where it draws it)
		// what plays behind the browser: its bar along the bottom, its row's chip, the header's mark (drawBattery says
		// whether it moved)
		miniUp = miniBarShown(p);
		setChip(p);
		markAnim = false;
		if (page == Page::Info || page == Page::About) drawInfo(p);
		else if (page == Page::Web) drawWeb(p);
		else if (page == Page::Exit) drawExit(p);
		else if (page == Page::Settings) drawSettings(p);
		else if (page == Page::Chapters) drawChapters(p);
		else if (page == Page::Sound) drawSound(p);
		else if (page == Page::Playing) drawPlaying(p);
		else if (page == Page::UpNext) drawUpNext(p);
		else if (page == Page::Extensions) drawExtensions(p);
		else if (page == Page::Manga) drawManga(p);
		else if (page == Page::MangaSeries) drawMangaSeries(p);
		else if (page == Page::MangaRead) drawMangaRead(p);
		else if (page == Page::MangaSettings) drawMangaSettings(p);
		else if (page != Page::Main) drawTracks(p);
		else drawMain(p);
		if (miniUp) drawMiniBar(p);
		if (displayShown(p)) drawDisplay(p);
		if (inMenu) drawTabs(p);
		if (ctxOpen) drawBrowseMenu(p);	// (the mini bar's hold menu opens on pages of its own too)
		if (pickerOpen) drawPicker();
		if (playOnOpen && inMenu && page == Page::Playing) drawPlayOn(p);
		if (addPending.serial && page == Page::Browse) drawBusy();
		if (popupShown(p)) drawUpdatePopup();
		drawExtNotice();
		if (quickOpen) drawQuick(p);
		if (kbOpen) drawKeyboard();
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
		return left;
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
		auto clock = [&](int edge) {
			std::string c = clockText(p);
			int cr = edge - (edge == right ? 0 : 10);
			if (!c.empty())
			{
				if (cr - (int)std::lround(textWidth(c, 0) / sx) < minX) return;
				drawTextMid(c, 0, touchLocked && page == Page::Main ? DIM : TEXT, cr, y + 8, 0, 1);
				cr -= (int)std::lround(textWidth(c, 0) / sx) + 10;
			}
			if (markW(p) && cr - 18 >= minX) drawMark(cr, y + 8, p);
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
		bool cancel = page == Page::Browse && !inMenu && (selMode == SEL_ADD || selMode == SEL_PICK);
		if (cancel) drawGlyphMid(G_CROSS, b.x + b.w / 2, b.y + b.h / 2, TEXT, 2);
		else drawGlyphMid(G_BACK, b.x + b.w / 2 - 1, b.y + b.h / 2, TEXT, 3);
	}

	void Ui::drawHeader(const std::string& title, const Player& p, int titleRight)
	{
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
		drawBox(r, active ? ACCENT : BUTTON, R_BUTTON);
		drawTextMid(name, size, active ? ON_ACCENT : TEXT, r.x + r.w / 2, r.y + r.h / 2, r.w - 16, 2);
	}

	void Ui::drawHints(const std::vector<Hint>& hints)
	{
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

	void Ui::drawDialog(Rect box, const std::string& title)
	{
		// over the page, dimmed: a panel, its title on top; the footer is the dialog's (its hints drawn after)
		fill({0, 0, W, H}, {0, 0, 0, 170});
		fill({0, 436, W, 44}, BG);	// the page's hints gone (as dark as the rest): the dialog's go there
		drawBox(box, SURFACE, R_DIALOG);
		if (!title.empty()) drawTextMid(title, 1, ACCENT, box.x + box.w / 2, box.y + 30, box.w - 40, 2);
	}

	void Ui::drawDialogRow(Rect r, const Glyph* g, const std::string& label, bool at, bool pressed, SDL_Color ink,
		const std::string& note, SDL_Color noteInk)
	{
		if (pressed) drawBox(r, PRESSED, R_BUTTON);
		else if (at) drawFocus(r);
		SDL_Color c = at && ink.r == TEXT.r && ink.g == TEXT.g ? ACCENT : ink;
		int x = r.x + 20, noteW = 0;
		if (g)
		{
			drawGlyphMid(*g, x + 11, r.y + r.h / 2, c);
			x += 36;
		}
		if (!note.empty())
		{
			drawTextMid(note, 0, noteInk.a ? noteInk : at ? DIM_ON : DIM, r.x + r.w - 14, r.y + r.h / 2, 0, 1);
			noteW = (int)std::lround(textWidth(note, 0) / sx) + 20;
		}
		drawTextMid(label, 1, c, x, r.y + r.h / 2, r.x + r.w - x - 12 - noteW);
	}

	void Ui::drawTabs(const Player& p)
	{
		static const char* names[TABS] = {"Settings", "About", "Quit", "Playing"};
		fill({0, 0, W, 57}, BG);	// over whatever the page drew there (a settings group's neighbours start at 58)
		webInd = {0, 0, 0, 0};	// (the battery and its marks gone under it: drawn again)
		drawBackIcon();
		auto tabs = menuTabs(p);
		int n = (int)tabs.size();
		for (int t = 0; t < n; t++) drawTab(menuTabRect(t, n), names[tabs[t]], tabs[t] == menuTab);
		Rect last = menuTabRect(n - 1, n);
		drawBattery(616, 24, p, last.x + last.w + 8);
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
