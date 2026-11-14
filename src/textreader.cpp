#include "textreader.h"
#include "pagedecode.h"
#include "textdoc.h"
#include "settings.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace bp
{
	namespace
	{
		struct Theme { uint8_t bg[3], fg[3]; };
		const Theme THEMES[TXT_THEMES] = {
			{{30, 28, 34}, {226, 220, 206}},	// dark: warm grey on a dark plum-grey (the player's own dark)
			{{246, 243, 236}, {36, 34, 32}},	// light: ink on paper
			{{240, 226, 197}, {76, 58, 36}},	// sepia
			{{0, 0, 0}, {176, 176, 176}},		// black: the panel's pixels off around the letters
		};
		// the body text's size in the panels' 480-line units, small … extra large
		const int SIZE_UNITS[TXT_SIZES] = {16, 19, 22, 26, 31};
		const double SPACING[TXT_SPACINGS] = {1.12, 1.32, 1.6};
		const double MARGIN[TXT_MARGINS] = {0.03, 0.06, 0.11};
		// each family's files: regular, and a bold of its own where it has one (else drawn bold)
		struct Family { const char* regular; const char* bold; bool pixel12; };
		const Family FAMILIES[TXT_FONTS] = {
			{"Lora.ttf", nullptr, false},
			{"Inter-Regular.ttf", "Inter-Bold.ttf", false},
			{"JetBrainsMono.ttf", nullptr, false},
			{"PixelifySans-Regular.ttf", nullptr, false},
			{"fusion-pixel-12px-proportional-zh_hans.ttf", nullptr, true},
		};
		// for what a family hasn't (Cyrillic in a pixel font, CJK, Thai, Arabic...): the first of these that has it
		const char* FALLBACK_FILES[] = {"Inter-Regular.ttf", "fusion-pixel-12px-proportional-zh_hans.ttf", "NotoSansThai.ttf",
			"NotoSansArabic.ttf", "NotoSansHebrew.ttf", "NotoSansDevanagari.ttf"};
		constexpr int FALLBACK_COUNT = (int)(sizeof FALLBACK_FILES / sizeof FALLBACK_FILES[0]);
		constexpr size_t CACHE_BLOCKS = 400;
	}

	void docFilterTint(int filter, float& r, float& g, float& b)
	{
		switch (filter)
		{
		case DOC_FILTER_WARM: r = 1.0f; g = 0.9f; b = 0.72f; break;		// less blue: yellowish, as a night light
		case DOC_FILTER_WARMER: r = 1.0f; g = 0.8f; b = 0.55f; break;
		case DOC_FILTER_DIM: r = g = b = 0.62f; break;
		default: r = g = b = 1.0f; break;
		}
	}

	TextReader::~TextReader()
	{
		closeFonts();
		pr.shutdown();
	}

	bool TextReader::init(bool gles, const std::string& dir)
	{
		if (ready) return true;
		fontsDir = dir;
		if (!pr.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, gles)) return false;
		pr.setUploadAll(true);	// (a row is small: every new one goes up in the frame that shows it)
		ready = true;
		return true;
	}

	void TextReader::closeFonts()
	{
		for (auto*& f : fonts)
		{
			if (f) TTF_CloseFont(f);
			f = nullptr;
		}
		for (auto& fb : fallbacks)
			for (auto* f : fb.second)
				if (f) TTF_CloseFont(f);
		fallbacks.clear();
		adv.clear();
		if (small) TTF_CloseFont(small);
		small = nullptr;
	}

	void TextReader::openFonts()
	{
		closeFonts();
		const Family& fam = FAMILIES[std::clamp(style.font, 0, TXT_FONTS - 1)];
		double scale = viewH / 480.0;
		int body = (int)std::lround(SIZE_UNITS[std::clamp(style.size, 0, TXT_SIZES - 1)] * scale);
		// a 12-pixel font stays crisp at whole multiples of 12
		auto px = [&](double p, bool pixel12) { return pixel12 ? std::max(12, (int)std::lround(p / 12.0) * 12) : std::max(8, (int)std::lround(p)); };
		const double headings[3] = {1.5, 1.28, 1.12};
		for (int s = 0; s < 8; s++)
		{
			bool pre = s == 7;
			const char* file = pre ? FAMILIES[TXT_FONT_MONO].regular : fam.regular;
			bool pixel12 = !pre && fam.pixel12;
			bool bold = s == 1 || s == 3 || (s >= 4 && s <= 6);
			bool italic = s == 2 || s == 3;
			if (bold && fam.bold && !pre) file = fam.bold;
			double size = s >= 4 && s <= 6 ? body * headings[s - 4] : pre ? body * 0.9 : body;
			slotPx[s] = px(size, pixel12);
			fonts[s] = TTF_OpenFont((fontsDir + "/" + file).c_str(), slotPx[s]);
			if (!fonts[s])
			{
				fprintf(stderr, "text: font %s: %s\n", file, TTF_GetError());
				continue;
			}
			int st = (bold && !(fam.bold && !pre) ? TTF_STYLE_BOLD : 0) | (italic ? TTF_STYLE_ITALIC : 0);
			TTF_SetFontStyle(fonts[s], st);
			TTF_SetFontHinting(fonts[s], TTF_HINTING_LIGHT);
		}
		lineH = fonts[0] ? (int)std::lround(TTF_FontHeight(fonts[0]) * SPACING[std::clamp(style.spacing, 0, TXT_SPACINGS - 1)]) : 20;
		ascent = fonts[0] ? TTF_FontAscent(fonts[0]) : 16;
		margin = (int)std::lround(layoutW() * MARGIN[std::clamp(style.margins, 0, TXT_MARGINS - 1)]);
		colW = std::max(40, layoutW() - 2 * margin);
		small = TTF_OpenFont((fontsDir + "/" + FAMILIES[TXT_FONT_SANS].regular).c_str(), std::max(8, body * 6 / 10));
	}

	TTF_Font* TextReader::fontFor(int slot, uint32_t cp)
	{
		TTF_Font* f = fonts[slot] ? fonts[slot] : fonts[0];
		if (!f || cp < 0x20 || TTF_GlyphIsProvided32(f, cp)) return f;
		for (int i = 0; i < FALLBACK_COUNT; i++)
		{
			bool pixel12 = i == 1;
			int px = pixel12 ? std::max(12, (int)std::lround(slotPx[slot] / 12.0) * 12) : slotPx[slot];
			auto& list = fallbacks[{i, px}];
			if (list.empty())
			{
				TTF_Font* fb = TTF_OpenFont((fontsDir + "/" + FALLBACK_FILES[i]).c_str(), px);
				if (fb && (slot == 1 || slot == 3 || (slot >= 4 && slot <= 6))) TTF_SetFontStyle(fb, TTF_STYLE_BOLD);
				list.push_back(fb);
			}
			if (list[0] && TTF_GlyphIsProvided32(list[0], cp)) return list[0];
		}
		return f;
	}

	int TextReader::advance(int slot, uint32_t cp)
	{
		uint64_t k = (uint64_t)slot << 32 | cp;
		auto it = adv.find(k);
		if (it != adv.end()) return it->second;
		int a = 0, mnx, mxx, mny, mxy;
		TTF_Font* f = fontFor(slot, cp);
		if (!f || TTF_GlyphMetrics32(f, cp, &mnx, &mxx, &mny, &mxy, &a) != 0) a = slotPx[slot] / 2;
		adv[k] = a;
		return a;
	}

	int TextReader::slotAt(const Laid& l, size_t at) const
	{
		switch (l.b.kind)
		{
		case TextBlock::H1: return 4;
		case TextBlock::H2: return 5;
		case TextBlock::H3: return 6;
		case TextBlock::Pre: return 7;
		default: break;
		}
		uint8_t st = 0;
		for (const auto& s : l.b.spans)
			if (at >= s.from && at < s.to) st |= s.style;
		return (st & TextBlock::Bold ? 1 : 0) + (st & TextBlock::Italic ? 2 : 0);
	}

	const TextReader::Laid* TextReader::laid(uint64_t id)
	{
		auto it = cache.find(id);
		if (it != cache.end()) return &it->second;
		Laid l;
		if (!src || id >= src->end() || !src->block(id, l.b, l.next)) return nullptr;
		bool paras = src->paragraphs();
		int quote = l.b.kind == TextBlock::Quote ? colW / 12 : 0;
		// a book's paragraph has a little space above it, a heading more; a TXT's lines sit close
		int pad = !paras ? 0 : l.b.kind == TextBlock::H1 || l.b.kind == TextBlock::H2 ? lineH : l.b.kind == TextBlock::H3 ? lineH * 2 / 3
			: lineH * 2 / 5;
		if (l.b.kind == TextBlock::Image)
		{
			std::vector<uint8_t> bytes;
			PageImage img;
			if (src->image(l.b.image, bytes))
				img = decodePage(bytes.data(), bytes.size(), colW, viewH * 9 / 10);
			if (img.ok())
			{
				l.pic = std::make_shared<std::vector<uint8_t>>(std::move(img.rgba));
				l.picW = img.w;
				l.picH = img.h;
				// slices of it as rows, so it scrolls through as text does and no texture is ever large
				for (int y = 0; y < img.h; y += 128)
				{
					RowG r;
					r.imgY = y;
					r.h = std::min(128, img.h - y);
					if (y == 0) { r.pad = pad; r.h += pad; }
					l.rows.push_back(r);
				}
			}
			else
			{
				// (a picture that can't be read: its words, if it has any, else a mark of it)
				l.b.kind = TextBlock::Para;
				if (l.b.text.empty()) l.b.text = "[picture]";
			}
		}
		if (l.rows.empty())
		{
			int slot0 = slotAt(l, 0);
			int h = slot0 >= 4 && slot0 <= 6 ? (int)std::lround(lineH * slotPx[slot0] / (double)slotPx[0]) : lineH;
			if (l.b.kind == TextBlock::Rule) l.rows.push_back({0, 0, lineH + pad, pad, 0});
			else
			{
				std::vector<uint32_t> starts = wrapRows(l.b.text, colW - quote, [&](uint32_t cp, size_t at) { return advance(slotAt(l, at), cp); });
				for (size_t i = 0; i < starts.size(); i++)
				{
					RowG r;
					r.from = starts[i];
					r.to = i + 1 < starts.size() ? starts[i + 1] : (uint32_t)l.b.text.size();
					r.h = h;
					if (i == 0) { r.pad = pad; r.h += pad; }
					l.rows.push_back(r);
				}
			}
		}
		if (cache.size() >= CACHE_BLOCKS)
		{
			// the oldest half goes (the place shown is always among the newest)
			size_t drop = cacheOrder.size() / 2;
			for (size_t i = 0; i < drop; i++) cache.erase(cacheOrder[i]);
			cacheOrder.erase(cacheOrder.begin(), cacheOrder.begin() + drop);
		}
		cacheOrder.push_back(id);
		return &(cache[id] = std::move(l));
	}

	bool TextReader::nextRow(Pos& p)
	{
		const Laid* l = laid(p.block);
		if (!l) return false;
		if (p.row + 1 < (int)l->rows.size())
		{
			p.row++;
			return true;
		}
		if (l->next >= src->end() || !laid(l->next)) return false;
		p = {l->next, 0};
		return true;
	}

	bool TextReader::prevRow(Pos& p)
	{
		if (p.row > 0)
		{
			p.row--;
			return true;
		}
		uint64_t prev;
		if (!src->before(p.block, prev)) return false;
		const Laid* l = laid(prev);
		if (!l || l->rows.empty()) return false;
		p = {prev, (int)l->rows.size() - 1};
		return true;
	}

	int TextReader::rowH(const Pos& p)
	{
		const Laid* l = laid(p.block);
		return l && p.row < (int)l->rows.size() ? std::max(1, l->rows[p.row].h) : lineH;
	}

	int TextReader::heightBelow(Pos p, int limit)
	{
		int h = rowH(p);
		while (h < limit && nextRow(p)) h += rowH(p);
		return h;
	}

	void TextReader::scrollBy(double px)
	{
		if (!src || px == 0) return;
		yoff += px;
		while (yoff < 0)
		{
			Pos p = top;
			if (!prevRow(p))
			{
				yoff = 0;
				flingV = glideLeft = 0;	// (the start: the motion stops there)
				break;
			}
			top = p;
			yoff += rowH(top);
		}
		while (yoff >= rowH(top))
		{
			Pos p = top;
			if (!nextRow(p)) break;
			yoff -= rowH(top);
			top = p;
		}
		// the end: the last row at the panel's foot, not above it (unless the whole text is shorter)
		if (px > 0)
		{
			int below = heightBelow(top, (int)yoff + viewH);
			double over = viewH - (below - yoff);
			if (over > 0)
			{
				flingV = glideLeft = 0;
				// (back up, without the end check again)
				yoff -= over;
				while (yoff < 0)
				{
					Pos p = top;
					if (!prevRow(p)) { yoff = 0; break; }
					top = p;
					yoff += rowH(top);
				}
			}
		}
		redrawSoon(1);
	}

	bool TextReader::open(std::unique_ptr<TextSource> s, uint64_t place, int w, int h, const Style& st)
	{
		close();
		err.clear();
		if (!ready || !s) return false;
		src = std::move(s);
		viewW = w;
		viewH = h;
		style = st;
		openFonts();
		float r, g, b;
		docFilterTint(style.filter, r, g, b);
		pr.setTint(r, g, b);
		if (src->first() >= src->end())
		{
			err = "This file is empty";
			top = {src->first(), 0};
		}
		else top = {place ? src->snap(place) : src->first(), 0};
		yoff = 0;
		spread = top;
		spreadDirty = true;
		topKey = -1;
		scrollBy(0.0001);	// (the end check: a place kept near the end shows the last screen whole)
		redrawSoon();
		return true;
	}

	void TextReader::close()
	{
		src.reset();
		cache.clear();
		cacheOrder.clear();
		keys.clear();
		pr.keepOnly({});
		pr.flushDead();
		flingV = glideLeft = 0;
		endShown = false;
	}

	void TextReader::relayout()
	{
		// the place is the block at the top: it stays; the rows are made again at the new size
		cache.clear();
		cacheOrder.clear();
		keys.clear();
		pr.keepOnly({});
		top.row = 0;
		yoff = 0;
		spread.row = 0;
		spreadDirty = true;
		topKey = -1;
		if (!bookHold) scrollBy(0.0001);
		redrawSoon();
	}

	void TextReader::setStyle(const Style& st)
	{
		if (st == style) return;
		bool layout = st.font != style.font || st.size != style.size || st.spacing != style.spacing || st.margins != style.margins;
		style = st;
		float r, g, b;
		docFilterTint(style.filter, r, g, b);
		pr.setTint(r, g, b);
		if (layout) openFonts();
		if (src) relayout();	// (a theme only: the rows drawn again in its colours)
	}

	void TextReader::setEncoding(const std::string& charset)
	{
		if (!src) return;
		src->setEncoding(charset);
		relayout();
	}

	void TextReader::command(int kind, double v, int64_t ms)
	{
		if (!src) return;
		lastMs = lastMs ? lastMs : ms;
		if (kind == MatchStep)
		{
			// the next (previous) row of this block that holds the query, past the one the match was on
			stepped = false;
			Pos& p = bookHold ? spread : top;
			const Laid* l = laid(p.block);
			if (!l || highlight.empty()) return;
			std::string low = l->b.text;
			for (char& c : low) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
			std::vector<int> rows;
			for (size_t at = low.find(highlight); at != std::string::npos; at = low.find(highlight, at + highlight.size()))
			{
				int row = 0;
				for (int r = 0; r < (int)l->rows.size(); r++)
					if (l->rows[r].from <= at) row = r;
				if (rows.empty() || rows.back() != row) rows.push_back(row);
			}
			int to = -1;
			if (v > 0) { for (int r : rows) if (r > matchAt) { to = r; break; } }
			else for (int k = (int)rows.size() - 1; k >= 0; k--) if (rows[k] < matchAt) { to = rows[k]; break; }
			if (to < 0) return;
			matchAt = to;
			p.row = std::max(0, to - 1);
			stepped = true;
			if (bookHold) spreadDirty = true;
			else
			{
				yoff = 0;
				flingV = glideLeft = 0;
				scrollBy(0.0001);
			}
			redrawSoon(1);
			return;
		}
		if (bookHold)
		{
			// a spread at a time: the D-pad's and the finger's steps are turns; a jump is a spread from there
			int turn = kind == BookTurn || kind == Lines || kind == Page ? (v > 0 ? 1 : v < 0 ? -1 : 0) : 0;
			if (turn > 0 && !spreadEnd)
			{
				spread = spreadNext;
				spreadDirty = true;
				pendingTurn = 1;
			}
			else if (turn < 0)
			{
				// back: the rows of two pages before, filled backwards (a spread may split a little differently
				// on the way back than on the way on, as in any reader that reflows)
				int room = 2 * (pageH() - lineH - lineH / 2);
				Pos q = spread;
				int used = 0;
				for (;;)
				{
					Pos p = q;
					if (!prevRow(p)) break;
					int h = rowH(p);
					if (used + h > room) break;
					used += h;
					q = p;
				}
				if (q.block != spread.block || q.row != spread.row) { spread = q; spreadDirty = true; pendingTurn = -1; }
			}
			else if (kind == Jump || kind == JumpTo || kind == JumpToLast)
			{
				spread = {kind == Jump ? src->at(v) : src->snap((uint64_t)v), 0};
				if (kind != Jump) spread.row = matchRow(spread.block, kind == JumpToLast);
				spreadDirty = true;
				pendingTurn = 0;	// (a jump shows its spread at once)
			}
			redrawSoon(1);
			return;
		}
		switch (kind)
		{
		case Scroll: flingV = glideLeft = 0; scrollBy(v); break;
		case Lines: glideLeft += v * lineH; glideV = std::max(0.6, std::abs(glideLeft) / 110.0); break;
		case Page: glideLeft += v * std::max(lineH, viewH - lineH * 2); glideV = std::max(1.5, std::abs(glideLeft) / 160.0); break;
		case Jump:
		case JumpTo:
		case JumpToLast:
			flingV = glideLeft = 0;
			top = {kind == Jump ? src->at(v) : src->snap((uint64_t)v), 0};
			// a Find's jump: the row the match is on (a line above it for its context), not the paragraph's start,
			// which may run past the screen's foot
			if (kind != Jump) top.row = matchRow(top.block, kind == JumpToLast);
			yoff = 0;
			scrollBy(0.0001);
			break;
		case Fling: glideLeft = 0; flingV = v; break;
		case Hold: flingV = glideLeft = 0; break;
		default: break;
		}
		redrawSoon(1);
	}

	bool TextReader::frame(int64_t now)
	{
		if (!src) return false;
		if (bookHold && spreadDirty && viewW > 0)
		{
			buildSpread();
			return true;
		}
		if (bookHold && turnWay)
		{
			if (now - turnT0 >= turnMs())
			{
				// done: the spread before it goes
				turnWay = 0;
				topOldKey = -1;
				bottomOld.reset();
				bookGen++;
			}
			return true;
		}
		double dt = lastMs ? std::clamp<double>((double)(now - lastMs), 0, 50) : 16;
		lastMs = now;
		bool moved = false;
		if (flingV != 0)
		{
			scrollBy(flingV * dt);
			flingV *= std::exp(-dt / 325.0);
			if (std::abs(flingV) < 0.02) flingV = 0;
			moved = true;
		}
		if (glideLeft != 0)
		{
			double step = std::min(std::abs(glideLeft), glideV * dt) * (glideLeft > 0 ? 1 : -1);
			glideLeft -= step;
			if (std::abs(glideLeft) < 0.5) glideLeft = 0;
			scrollBy(step);
			moved = true;
		}
		bool need = moved || forceFrames > 0;
		return need;
	}

	int TextReader::rowKey(const Pos& p)
	{
		auto k = std::make_pair(p.block, p.row);
		auto it = keys.find(k);
		if (it != keys.end()) return it->second;
		const Laid* l = laid(p.block);
		int key = nextKey++;
		if (nextKey > 0x7ff00000) nextKey = 1;
		keys[k] = key;
		if (l)
			if (auto px = drawRow(*l, p.row, viewW)) pr.addPage(key, viewW, l->rows[p.row].h, px);
		return key;
	}

	std::shared_ptr<std::vector<uint8_t>> TextReader::drawRow(const Laid& l, int row, int w)
	{
		const RowG& r = l.rows[row];
		int h = std::max(1, r.h);
		auto px = std::make_shared<std::vector<uint8_t>>((size_t)w * h * 4, 0);
		auto& v = *px;
		const Theme& th = THEMES[std::clamp(style.theme, 0, TXT_THEMES - 1)];
		// a pixel of ink over what's there (straight alpha, as the renderer blends)
		auto put = [&](int x, int y, const uint8_t* c, int a) {
			if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
			uint8_t* p = &v[((size_t)y * w + x) * 4];
			int da = p[3], oa = a + da * (255 - a) / 255;
			if (oa <= 0) return;
			for (int k = 0; k < 3; k++) p[k] = (uint8_t)((c[k] * a + p[k] * da * (255 - a) / 255) / oa);
			p[3] = (uint8_t)oa;
		};
		if (l.b.kind == TextBlock::Image && l.pic)
		{
			int x0 = margin + (colW - l.picW) / 2;
			for (int y = 0; y < r.h - r.pad; y++)
			{
				int sy = r.imgY + y;
				if (sy >= l.picH) break;
				uint8_t* d = &v[((size_t)(y + r.pad) * w + x0) * 4];
				const uint8_t* s = l.pic->data() + (size_t)sy * l.picW * 4;
				std::copy(s, s + (size_t)std::min(l.picW, w - x0) * 4, d);
			}
			return px;
		}
		if (l.b.kind == TextBlock::Rule)
		{
			int y = r.pad + (r.h - r.pad) / 2;
			for (int x = margin + colW / 3; x < margin + colW * 2 / 3; x++) put(x, y, th.fg, 90);
			return px;
		}
		int x = margin, quote = 0;
		if (l.b.kind == TextBlock::Quote)
		{
			quote = colW / 12;
			for (int y = r.pad; y < h; y++)
				for (int k = 0; k < std::max(2, viewH / 240); k++) put(margin + k, y, th.fg, 70);
			x += quote;
		}
		// the row's characters, in runs of one font (a style, or a fallback for what the font hasn't)
		const std::string& t = l.b.text;
		uint32_t end = r.to;
		while (end > r.from && t[end - 1] == ' ') end--;
		int rowTextH = r.h - r.pad;
		// Find's query: a mark behind each place it shows in this row (at the glyphs' advances, as the text is laid)
		if (!highlight.empty() && end > r.from)
		{
			std::string low = t.substr(r.from, end - r.from);
			for (char& c : low) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
			const uint8_t mark[3] = {255, 210, 60};
			for (size_t m = low.find(highlight); m != std::string::npos; m = low.find(highlight, m + highlight.size()))
			{
				int x0 = x, x1;
				for (size_t q = r.from; q < r.from + m;) x0 += advance(slotAt(l, q), nextCodePoint(t, q));
				x1 = x0;
				for (size_t q = r.from + m; q < r.from + m + highlight.size() && q < end;) x1 += advance(slotAt(l, q), nextCodePoint(t, q));
				for (int yy = r.pad + 2; yy < h - 2; yy++)
					for (int xx = x0 - 2; xx < x1 + 2; xx++) put(xx, yy, mark, 95);
			}
		}
		size_t i = r.from;
		while (i < end)
		{
			size_t runStart = i;
			size_t j = i;
			uint32_t cp = nextCodePoint(t, j);
			int slot = slotAt(l, runStart);
			TTF_Font* f = fontFor(slot, cp);
			i = j;
			while (i < end)
			{
				size_t k = i;
				uint32_t c2 = nextCodePoint(t, k);
				if (slotAt(l, i) != slot || fontFor(slot, c2) != f) break;
				i = k;
			}
			if (!f) continue;
			std::string run = t.substr(runStart, i - runStart);
			int fh = TTF_FontHeight(f);
			int baseline = r.pad + (rowTextH - fh) / 2 + TTF_FontAscent(f);
			if (SDL_Surface* s = TTF_RenderUTF8_Blended(f, run.c_str(), SDL_Color{255, 255, 255, 255}))
			{
				if (SDL_Surface* c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0))
				{
					int y0 = baseline - TTF_FontAscent(f);
					for (int yy = 0; yy < c->h; yy++)
					{
						const uint8_t* q = (const uint8_t*)c->pixels + yy * c->pitch;
						for (int xx = 0; xx < c->w; xx++)
							if (q[xx * 4 + 3]) put(x + xx, y0 + yy, th.fg, q[xx * 4 + 3]);
					}
					SDL_FreeSurface(c);
				}
				SDL_FreeSurface(s);
			}
			// (on by the glyphs' advances, as the wrap measured them: a drawn-italic run's slant isn't room)
			for (size_t q = 0; q < run.size();) x += advance(slot, nextCodePoint(run, q));
		}
		return px;
	}

	void TextReader::draw(int w, int h)
	{
		if (!src) return;
		if (w != viewW || h != viewH)
		{
			viewW = w;
			viewH = h;
			openFonts();
			relayout();
		}
		const Theme& th = THEMES[std::clamp(style.theme, 0, TXT_THEMES - 1)];
		if (!pr.has(BG_KEY))
		{
			auto bg = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{th.bg[0], th.bg[1], th.bg[2], 255});
			pr.addPage(BG_KEY, 1, 1, bg);
		}
		std::vector<PageRenderer::Placed> placed;
		placed.push_back({BG_KEY, 0, 0, (float)w, (float)h});
		std::set<int> shown{BG_KEY};
		if (bookHold)
		{
			if (spreadDirty) buildSpread();
			// the turn: each page folds at the spine, the hinge (the top panel's foot). Forward, the right-hand page
			// folds away first, then the new left-hand one opens out over the old; back, the other way round
			double p = turnWay ? std::clamp((SDL_GetTicks() - turnT0) / (double)turnMs(), 0.0, 1.0) : 1;
			bool topRight = bookHold == MANGA_BOOK_RIGHT;
			bool topFolds = (turnWay > 0) == topRight;	// the top's page folds away (first half); else it opens (second)
			auto ease = [](double t) { return t * t * (3 - 2 * t); };
			auto at = [&](int key, double frac) {	// the page squeezed to frac of the panel, against the spine
				float hh = (float)(h * frac);
				placed.push_back({key, 0, (float)h - hh, (float)w, hh});
				shown.insert(key);
			};
			if (turnWay && topOldKey >= 0 && p < 1)
			{
				// (both pages kept for the whole turn: one not drawn yet in its first half is still to come)
				shown.insert(topOldKey);
				if (topKey >= 0) shown.insert(topKey);
				if (topFolds)
				{
					// first half: the old page folds down into the spine over the new; then the new alone
					if (topKey >= 0) at(topKey, 1);
					if (p < 0.5) at(topOldKey, 1 - ease(p * 2));
				}
				else
				{
					// first half: the old page waits; then the new one opens up from the spine over it
					at(topOldKey, 1);
					if (p >= 0.5 && topKey >= 0) at(topKey, ease((p - 0.5) * 2));
				}
			}
			else if (topKey >= 0) at(topKey, 1);
			keys.clear();
			pr.keepOnly(shown);
			pr.draw(placed, w, h);
			if (forceFrames > 0) forceFrames--;
			return;
		}
		std::map<std::pair<uint64_t, int>, int> keep;
		if (err.empty())
		{
			Pos p = top;
			double y = -yoff;
			bool more = true;
			while (y < h && more)
			{
				int key = rowKey(p);
				int rh = rowH(p);
				placed.push_back({key, 0, (float)y, (float)w, (float)rh, false, true});
				shown.insert(key);
				keep[{p.block, p.row}] = key;
				y += rh;
				more = nextRow(p);
			}
			endShown = !more && y <= h + 1;
		}
		// the rows gone from view are let go (they're quick to draw again)
		keys = std::move(keep);
		pr.keepOnly(shown);
		pr.draw(placed, w, h);
		if (forceFrames > 0) forceFrames--;
	}

	double TextReader::fraction() const
	{
		if (!src) return 0;
		if (bookHold) return spreadEnd ? 1 : src->fraction(spread.block);
		if (endShown) return 1;
		return src->fraction(top.block);
	}

	void TextReader::setBook(int hold)
	{
		hold = hold == MANGA_BOOK_LEFT || hold == MANGA_BOOK_RIGHT ? hold : 0;
		if (hold == bookHold) return;
		bool wasBook = bookHold != 0;
		// the place carries over: the scroll's top row is the spread's first, and back
		if (!wasBook && hold) spread = top;
		if (wasBook && !hold) top = spread;
		bookHold = hold;
		bottomPx.reset();
		bookGen++;
		if (!src) return;
		openFonts();	// (a page is the panel turned: another width)
		relayout();
	}

	std::vector<TextReader::Pos> TextReader::fillPage(Pos p, int height, Pos& next, bool& ended)
	{
		std::vector<Pos> rows;
		int used = 0;
		ended = false;
		Pos q = p;
		if (!laid(q.block)) { ended = true; next = q; return rows; }
		for (;;)
		{
			int h = rowH(q);
			if (!rows.empty() && used + h > height)
			{
				next = q;
				return rows;
			}
			rows.push_back(q);
			used += h;
			Pos n = q;
			if (!nextRow(n))
			{
				ended = true;
				next = q;
				return rows;
			}
			q = n;
		}
	}

	std::shared_ptr<std::vector<uint8_t>> TextReader::drawPage(const std::vector<Pos>& rows, double frac)
	{
		int w = layoutW(), h = pageH();
		const Theme& th = THEMES[std::clamp(style.theme, 0, TXT_THEMES - 1)];
		auto px = std::make_shared<std::vector<uint8_t>>((size_t)w * h * 4);
		auto& v = *px;
		for (size_t i = 0; i < v.size(); i += 4)
		{
			v[i] = th.bg[0];
			v[i + 1] = th.bg[1];
			v[i + 2] = th.bg[2];
			v[i + 3] = 255;
		}
		// a row's ink over the page (its pixels are straight alpha over nothing)
		auto over = [&](const std::vector<uint8_t>& src, int sw, int sh, int x0, int y0) {
			for (int y = 0; y < sh; y++)
			{
				int dy = y0 + y;
				if (dy < 0 || dy >= h) continue;
				for (int x = 0; x < sw; x++)
				{
					int dx = x0 + x;
					if (dx < 0 || dx >= w) continue;
					const uint8_t* s = &src[((size_t)y * sw + x) * 4];
					if (!s[3]) continue;
					uint8_t* d = &v[((size_t)dy * w + dx) * 4];
					for (int k = 0; k < 3; k++) d[k] = (uint8_t)((s[k] * s[3] + d[k] * (255 - s[3])) / 255);
				}
			}
		};
		int y = lineH / 2;
		for (const Pos& p : rows)
		{
			const Laid* l = laid(p.block);
			if (!l || p.row >= (int)l->rows.size()) continue;
			if (auto r = drawRow(*l, p.row, w)) over(*r, w, l->rows[p.row].h, 0, y);
			y += l->rows[p.row].h;
		}
		// the foot: how far through the text this page starts
		if (small)
		{
			std::string foot = std::to_string((int)std::lround(frac * 100)) + "%";
			if (SDL_Surface* s = TTF_RenderUTF8_Blended(small, foot.c_str(), SDL_Color{th.fg[0], th.fg[1], th.fg[2], 255}))
			{
				if (SDL_Surface* c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0))
				{
					std::vector<uint8_t> buf((size_t)c->w * c->h * 4);
					for (int yy = 0; yy < c->h; yy++)
						for (int xx = 0; xx < c->w; xx++)
						{
							const uint8_t* q = (const uint8_t*)c->pixels + yy * c->pitch + xx * 4;
							uint8_t* b = &buf[((size_t)yy * c->w + xx) * 4];
							b[0] = q[0]; b[1] = q[1]; b[2] = q[2];
							b[3] = (uint8_t)(q[3] * 55 / 100);	// (dim: it's no part of the text)
						}
					over(buf, c->w, c->h, (w - c->w) / 2, h - c->h - lineH / 3);
					SDL_FreeSurface(c);
				}
				SDL_FreeSurface(s);
			}
		}
		return px;
	}

	namespace
	{
		// a page turned a quarter: clockwise (the top panel held on the left) or the other way
		std::shared_ptr<std::vector<uint8_t>> turned(const std::vector<uint8_t>& src, int w, int h, bool cw)
		{
			auto out = std::make_shared<std::vector<uint8_t>>((size_t)w * h * 4);
			const uint32_t* s = (const uint32_t*)src.data();
			uint32_t* d = (uint32_t*)out->data();
			// out is h wide and w tall
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
				{
					int ox = cw ? h - 1 - y : y, oy = cw ? x : w - 1 - x;
					d[(size_t)oy * h + ox] = s[(size_t)y * w + x];
				}
			return out;
		}
	}

	void TextReader::buildSpread()
	{
		spreadDirty = false;
		int room = pageH() - lineH - lineH / 2;	// (a half line above, the foot below)
		Pos nb;
		bool endA = false, endB = false;
		std::vector<Pos> a = fillPage(spread, room, nb, endA), b;
		if (!endA) b = fillPage(nb, room, spreadNext, endB);
		else spreadNext = nb;
		spreadEnd = endA || endB;
		auto pa = drawPage(a, src->fraction(spread.block));
		auto pb = drawPage(b, b.empty() ? 1.0 : src->fraction(nb.block));
		// held with the top panel on the left, the left page (the first) is the top panel's
		bool cw = bookHold == MANGA_BOOK_LEFT;
		auto ta = turned(*pa, layoutW(), pageH(), cw), tb = turned(*pb, layoutW(), pageH(), cw);
		auto topPx = bookHold == MANGA_BOOK_LEFT ? ta : tb;
		// every turn, folded or not: its way and time (the bottom panel's notice: next page, previous page)
		if (pendingTurn)
		{
			noticeWay = pendingTurn;
			noticeT0 = SDL_GetTicks();
		}
		// a turn: the spread before it stays, to fold away as the new one comes (a jump, a restyle, the fold off: no turn)
		if (pendingTurn && fold && topKey >= 0 && bottomPx)
		{
			if (topOldKey >= 0 && topOldKey != topKey) pr.keepOnly({BG_KEY, topKey});
			topOldKey = topKey;
			bottomOld = bottomPx;
			turnWay = pendingTurn;
			turnT0 = SDL_GetTicks();
		}
		else
		{
			turnWay = 0;
			topOldKey = -1;
			bottomOld.reset();
		}
		pendingTurn = 0;
		bottomPx = bookHold == MANGA_BOOK_LEFT ? tb : ta;
		botW = viewW;
		botH = viewH;
		topKey = nextKey++;
		pr.addPage(topKey, viewW, viewH, topPx);
		bookGen++;
		endShown = spreadEnd;
		redrawSoon(2);
	}

	std::string TextReader::snippet()
	{
		if (!src) return "";
		const Laid* l = laid(place());
		if (!l) return "";
		std::string t = l->b.kind == TextBlock::Image ? (l->b.text.empty() ? "[picture]" : l->b.text) : l->b.text;
		// the row shown at the top, if the place is inside a long block
		Pos p = bookHold ? spread : top;
		if (p.block == place() && p.row > 0 && p.row < (int)l->rows.size() && l->b.kind != TextBlock::Image)
			t = t.substr(l->rows[p.row].from);
		if (t.size() > 90)
		{
			size_t cut = t.rfind(' ', 90);
			cut = cut == std::string::npos || cut < 40 ? 90 : cut;
			while (cut > 0 && ((unsigned char)t[cut] & 0xC0) == 0x80) cut--;
			t = t.substr(0, cut) + "\xE2\x80\xA6";
		}
		return t;
	}

	void TextReader::setHighlight(const std::string& query)
	{
		std::string q = query;
		for (char& c : q) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
		if (q == highlight) return;
		highlight = q;
		// the rows drawn again (the marks are in their pixels); the layout stays
		keys.clear();
		pr.keepOnly({});
		if (bookHold) spreadDirty = true;
		redrawSoon();
	}

	int TextReader::matchRow(uint64_t block, bool last)
	{
		matchAt = 0;
		if (highlight.empty()) return 0;
		const Laid* l = laid(block);
		if (!l || l->rows.size() < 2) return 0;
		std::string low = l->b.text;
		for (char& c : low) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
		size_t at = last ? low.rfind(highlight) : low.find(highlight);
		if (at == std::string::npos) return 0;
		int row = 0;
		for (int r = 0; r < (int)l->rows.size(); r++)
			if (l->rows[r].from <= at) row = r;
		matchAt = row;
		return std::max(0, row - 1);
	}
}
