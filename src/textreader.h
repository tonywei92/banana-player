#pragma once
// The text viewer on the top panel (Files: a TXT or a reflowable EPUB). It shows a TextSource (textsource.h) as rows
// laid out only around the place shown: a block (a line of a TXT, a paragraph of a book) is wrapped when it first
// comes near the screen, a row drawn into its own small texture when it first shows, and both are kept for a while
// for the way back. Nothing walks the text from its start, so a 1 GB file scrolls and jumps as a small one does.
// The place is the block at the top, the row in it, and the pixels scrolled into that row.
// Main thread only, in the top window's GL context (as the manga reader: its own PageRenderer).
#include "pagerender.h"
#include "textsource.h"
#include <SDL_ttf.h>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bp
{
	// the viewer's look (Settings: the Text viewer's rows)
	constexpr int TXT_FONT_SERIF = 0, TXT_FONT_SANS = 1, TXT_FONT_MONO = 2, TXT_FONT_PIXEL = 3, TXT_FONT_CJK = 4, TXT_FONTS = 5;
	constexpr int TXT_SIZES = 5;	// small … extra large
	constexpr int TXT_SPACINGS = 3, TXT_MARGINS = 3;	// tight, normal, loose; narrow, normal, wide
	constexpr int TXT_THEME_DARK = 0, TXT_THEME_LIGHT = 1, TXT_THEME_SEPIA = 2, TXT_THEME_BLACK = 3, TXT_THEMES = 4;
	// a reading filter over the page (the PDF viewer's too): off, warm, warmer, dim
	constexpr int DOC_FILTER_OFF = 0, DOC_FILTER_WARM = 1, DOC_FILTER_WARMER = 2, DOC_FILTER_DIM = 3, DOC_FILTERS = 4;
	void docFilterTint(int filter, float& r, float& g, float& b);

	class TextReader
	{
	public:
		~TextReader();
		struct Style
		{
			int font = TXT_FONT_SERIF, size = 2, theme = TXT_THEME_DARK, filter = DOC_FILTER_OFF, spacing = 1, margins = 1;
			bool operator==(const Style& o) const
			{
				return font == o.font && size == o.size && theme == o.theme && filter == o.filter && spacing == o.spacing && margins == o.margins;
			}
		};
		// once, with the top window's context current
		bool init(bool gles, const std::string& fontsDir);
		// a source at a place (a block id kept from before; 0: its start), in a view of w x h pixels
		bool open(std::unique_ptr<TextSource> src, uint64_t place, int w, int h, const Style& st);
		void close();
		bool isOpen() const { return src != nullptr; }
		void setStyle(const Style& st);
		void setEncoding(const std::string& charset);
		// Find's query, marked wherever it shows (ASCII letters in any case); "" marks nothing
		void setHighlight(const std::string& query);
		// the row of a block Find's query is first on, less one (its line before, for the context); 0 without a query
		int matchRow(uint64_t block, bool last = false);

		// the bottom panel's wishes. Scroll: v pixels (down > 0), at once (a finger). Lines: v rows, gliding (the
		// D-pad). Page: v screens, gliding. Jump: v a share of the text (0..1). JumpTo: v a block id (the contents).
		// Fling: v pixels a millisecond, slowing to a stop. Hold: a fling stops (a finger down)
		// BookTurn: v spreads on (+1) or back (-1), in Book. MatchStep: v +1 / -1, Find's next or previous match in
		// the block shown (a long paragraph's own); matchStepped() says whether there was one
		// JumpToLast: JumpTo, on the block's last match (Find going back)
		enum Cmd { Scroll, Lines, Page, Jump, JumpTo, Fling, Hold, BookTurn, MatchStep, JumpToLast };
		bool matchStepped() const { return stepped; }
		// Book (MANGA_BOOK_LEFT / _RIGHT: held sideways, the top panel on the left or the right; 0: off, the scroll):
		// a page on each panel, upright as the handheld is held, the text going on from the left page to the right
		void setBook(int hold);
		int book() const { return bookHold; }
		// Book's page on the bottom panel (RGBA, the top panel's size, already turned); gen changes with it
		std::shared_ptr<const std::vector<uint8_t>> bottomPage(int& w, int& h) const
		{
			w = botW;
			h = botH;
			return bottomPx;
		}
		uint64_t bottomGen() const { return bookGen; }
		// Book's page turn: the pages before it (the bottom panel's, for its own drawing), which way (+1 on, -1 back,
		// 0 none), when it began (SDL ticks) and which panel holds the right-hand page; it lasts TURN_MS
		// (BP_TURN_MS: a test's slow turn, to see it in a scripted run's shots)
		static int turnMs() { static int ms = getenv("BP_TURN_MS") ? std::max(50, atoi(getenv("BP_TURN_MS"))) : 340; return ms; }
		std::shared_ptr<const std::vector<uint8_t>> bottomBefore() const { return bottomOld; }
		int turnDir() const { return turnWay; }
		// the fold on (the default) or off (the spread changes at once); every turn is told either way (lastTurn*)
		void setTurnFold(bool on) { fold = on; }
		int lastTurnDir() const { return noticeWay; }
		int64_t lastTurnAt() const { return noticeT0; }
		int64_t turnAt() const { return turnT0; }
		bool rightOnTop() const { return bookHold == MANGA_BOOK_RIGHT_HOLD; }
		void command(int kind, double v, int64_t ms);
		// a pass of main's loop: the motion advanced; true when the panel needs a frame
		bool frame(int64_t now);
		void draw(int w, int h);
		bool busy() const { return flingV != 0 || glideLeft != 0 || forceFrames > 0; }
		void redrawSoon(int n = 3) { forceFrames = std::max(forceFrames, n); }

		// where it is: the top block (kept as the file's place) and how far through (0..1); at the end; the
		// source's own (the contents, the encoding, the title)
		uint64_t place() const { return bookHold ? spread.block : top.block; }
		double fraction() const;
		bool atEnd() const { return endShown; }
		std::vector<TextSource::Chapter> contents() const { return src ? src->contents() : std::vector<TextSource::Chapter>{}; }
		std::string encoding() const { return src ? src->encoding() : ""; }
		std::string title() const { return src ? src->title() : ""; }
		// the first words at the place (a bookmark's label)
		std::string snippet();
		const std::string& error() const { return err; }

	private:
		struct RowG { uint32_t from = 0, to = 0; int h = 0; int pad = 0; int imgY = 0; };	// pad: space above its text
		struct Laid
		{
			TextBlock b;
			uint64_t next = 0;
			std::vector<RowG> rows;
			std::shared_ptr<std::vector<uint8_t>> pic;	// an Image block's pixels at the column's width
			int picW = 0, picH = 0;
		};
		struct Pos { uint64_t block = 0; int row = 0; };
		const Laid* laid(uint64_t id);	// the block wrapped (from the cache, or now)
		bool nextRow(Pos& p);	// false at the end
		bool prevRow(Pos& p);	// false at the start
		int rowH(const Pos& p);
		void scrollBy(double px);
		int heightBelow(Pos p, int limit);	// the rows' height from p down, up to limit
		// Book: a page's rows from p, as many as fit in height (at least one); false in ended: the text ran out
		std::vector<Pos> fillPage(Pos p, int height, Pos& next, bool& ended);
		void buildSpread();	// both pages from spread: drawn, turned, the top's texture and the bottom's pixels
		std::shared_ptr<std::vector<uint8_t>> drawPage(const std::vector<Pos>& rows, double fraction);
		int layoutW() const { return bookHold ? viewH : viewW; }	// a Book page is the panel turned: its height wide
		int pageH() const { return viewW; }
		void relayout();	// a new size, font or width: the cache goes, the place stays
		void openFonts();
		void closeFonts();
		// the font for a run (slot: 0 body, 1 bold, 2 italic, 3 bold italic, 4-6 headings, 7 pre) and code point
		TTF_Font* fontFor(int slot, uint32_t cp);
		int slotAt(const Laid& l, size_t at) const;
		int advance(int slot, uint32_t cp);
		int rowKey(const Pos& p);
		std::shared_ptr<std::vector<uint8_t>> drawRow(const Laid& l, int row, int w);

		PageRenderer pr;
		bool ready = false;
		std::string fontsDir, err;
		std::unique_ptr<TextSource> src;
		Style style;
		int viewW = 0, viewH = 0, margin = 0, colW = 0, lineH = 0, ascent = 0;
		TTF_Font* fonts[8] = {};
		std::map<std::pair<int, int>, std::vector<TTF_Font*>> fallbacks;	// (file, px) -> the fallback fonts at that size
		int slotPx[8] = {};
		std::unordered_map<uint64_t, int> adv;	// slot << 32 | code point -> its advance
		std::unordered_map<uint64_t, Laid> cache;
		std::vector<uint64_t> cacheOrder;
		Pos top;
		double yoff = 0;	// pixels of the top row scrolled out of view
		bool endShown = false;
		// keys of rows drawn: (block, row) -> the texture's key; keys not shown for a while are freed
		std::map<std::pair<uint64_t, int>, int> keys;
		int nextKey = 1;
		double flingV = 0;	// pixels a ms
		double glideLeft = 0, glideV = 0;	// a step's pixels still to go, and its speed
		int64_t lastMs = 0;
		int forceFrames = 0;
		static constexpr int BG_KEY = 0;
		// Book
		int bookHold = 0;
		Pos spread;	// the first row of the left page
		Pos spreadNext;	// the row after the right page
		bool spreadEnd = false, spreadDirty = true;
		int topKey = -1;	// the top panel's page texture
		std::shared_ptr<const std::vector<uint8_t>> bottomPx;
		int botW = 0, botH = 0;
		uint64_t bookGen = 0;
		TTF_Font* small = nullptr;	// the page's foot: how far through
		// the turn under way: the spread before it (the top's texture and the bottom's pixels), its way and start
		int topOldKey = -1, pendingTurn = 0, turnWay = 0, noticeWay = 0;
		int64_t turnT0 = 0, noticeT0 = 0;
		bool fold = true;
		std::string highlight;	// lowered
		int matchAt = 0;	// the row of the block shown that Find's match is on
		bool stepped = false;
		std::shared_ptr<const std::vector<uint8_t>> bottomOld;
		static constexpr int MANGA_BOOK_RIGHT_HOLD = 2;	// (settings.h's MANGA_BOOK_RIGHT; this header stays apart from it)
	};
}
