#pragma once
// The manga reader's pages on the top panel (docs/design/manga.md section 7): decoded RGBA pages as GL textures in
// the top window's own context (the one mpv draws with), placed by a Strip. Main thread only; it loads its GL calls
// at runtime as main.cpp does (no GL headers), and leaves the context as mpv expects it after each draw.
#include "strip.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <set>
#include <vector>

namespace bp
{
	class PageRenderer
	{
	public:
		// the GL calls and the shader; false (and a line in the log) when this context can't
		bool init(void* (*getProc)(const char*), bool gles);
		void shutdown();
		// a decoded page (w x h RGBA, straight alpha): its texture tiles go up a tile per draw, so a turn never stutters
		// (the pixels are shared with the reader, not copied: a page is in memory once until its textures are up)
		void addPage(int index, int w, int h, std::shared_ptr<const std::vector<uint8_t>> rgba);
		int oomCount() const { return ooms; }	// uploads the GPU had no memory for, this chapter and before
		bool has(int index) const;
		// the textures outside keep (the pages near the view) freed, the farthest first
		void keepOnly(const std::set<int>& keep);
		// the textures put aside deleted now (the top window's context current)
		void flushDead();
		// one frame of the strip into the bound framebuffer (w x h pixels); true while tiles still wait to go up
		bool draw(const Strip& strip, int w, int h);
		// the same for any layout: each page's rectangle in view pixels (the paged modes)
		// spin: a page still on its way (not one that failed): its grey place shows a ring of dots turning
		// blend: a card over the pages (alpha: its fade); plain: drawn as it is, not through the reading filter's tint
		// (the level card looks the same on every screen)
		struct Placed { int page; float x, y, w, h; bool spin = false; bool blend = false; float alpha = 1; bool plain = false; };
		bool draw(const std::vector<Placed>& placed, int w, int h);
		size_t textureBytes() const { return bytes; }
		// every pixel times this colour (a reading filter: warm, dim); 1,1,1 is none
		void setTint(float r, float g, float b) { tint[0] = r; tint[1] = g; tint[2] = b; }
		// all waiting tiles up in one draw, not one a draw (the text viewer's rows: small, and wanted at once)
		void setUploadAll(bool on) { uploadAll = on; }
		// draw over what the screen holds (mpv's picture) instead of clearing it first: for a page that is blended in
		void setKeepScreen(bool on) { keepScreen = on; }
		// the ring's step (the caller's clock: one step a frame it asks for), and whether the last draw showed one
		void setSpinStep(int s) { spinStep = s; }
		bool spun() const { return spunLast; }
		// a page whose textures didn't fit in the GPU's memory (drawn grey); unstall: every such page tries again
		bool stalled(int index) const;
		void unstall();

	private:
		struct Tile
		{
			unsigned tex = 0;
			int y = 0, h = 0;	// its rows of the page
		};
		struct Page
		{
			int index = -1, w = 0, h = 0;
			std::shared_ptr<const std::vector<uint8_t>> rgba;	// until every tile is up
			std::vector<Tile> tiles;
			size_t uploaded = 0;	// tiles up so far
			// a sharper copy of a page already shown (a zoom): it goes up while the old one is still drawn, and
			// takes its place only once all of it is on the GPU (no grey frame between)
			bool incoming = false;
			// its upload ran the GPU out of memory (after one try with the off-screen pages freed): waits for unstall()
			bool stalled = false;
			int oomTries = 0;
		};
		Page* find(int index);
		const Page* find(int index) const;
		bool uploadOne();	// true: a tile went up
		void freePage(Page& p);
		void quad(float x0, float y0, float x1, float y1, float v0, float v1, int w, int h);

		struct Gl;
		Gl* gl = nullptr;
		bool gles = false;
		unsigned prog = 0, vbo = 0, vao = 0, grey = 0, white = 0;
		int spinStep = 0;
		float tint[3] = {1, 1, 1};
		bool uploadAll = false;
		bool keepScreen = false;
		int ooms = 0;
		std::set<int> onScreen;	// the pages the last draw placed (an out-of-memory upload frees the rest)
		bool spunLast = false;
		int aPos = -1, aUv = -1, uTint = -1;
		int maxTex = 2048;
		std::deque<Page> pages;
		size_t bytes = 0;
		std::vector<unsigned> dead;	// textures to delete at the next draw, in the top window's context
	};

	// A card over a renderer's pages for a while (the level card, levelcard.h: the manga and text readers draw the top
	// panel themselves, so mpv's card would be under them), fading out over its last moments. The reader that holds
	// it keeps KEY among its textures while it's up
	struct CardOverlay
	{
		static constexpr int KEY = 0x7ffffff0;	// its texture among the pages' (no page, band or row has it)
		static constexpr int FADE_MS = 400;
		// a second card beside the level card (a reader's note, a chapter's end): its own texture, at the foot
		int key = KEY;
		bool foot = false;	// bottom centre, 26 units up (the note's place) in place of top right
		int w = 0, h = 0;
		int64_t until = 0;
		bool drawn = false;	// the last draw showed it
		void show(PageRenderer& pr, std::shared_ptr<const std::vector<uint8_t>> rgba, int cw, int ch, int64_t untilMs)
		{
			if (!rgba || cw <= 0 || ch <= 0) return;
			w = cw;
			h = ch;
			until = untilMs;
			pr.addPage(key, cw, ch, std::move(rgba));
		}
		bool up() const { return until != 0; }
		// a pass of the loop: true when the panel needs a frame (it went up, it fades, or it's just down: then
		// `ended` is set, for the reader to let its texture go)
		bool frame(int64_t now, bool& ended)
		{
			ended = false;
			if (!until || (drawn && now < until - FADE_MS)) return false;
			if (now >= until)
			{
				until = 0;
				ended = true;
			}
			return true;
		}
		// placed over the rest, at whole pixels (the picture is drawn at the panel's size: 1:1, sharp)
		void place(const PageRenderer& pr, std::vector<PageRenderer::Placed>& placed, int vw, int vh, int64_t now)
		{
			drawn = until && now < until && pr.has(key);
			if (!drawn) return;
			float a = std::min(1.f, std::max(0.f, (float)(until - now) / FADE_MS));
			float m = (float)(int)(20 * vh / 480.f + 0.5f), x = (float)vw - m - w, y = m;	// top right, 20 units in
			if (foot)
			{
				x = (float)((vw - w) / 2);
				y = (float)vh - (float)(int)(26 * vh / 480.f + 0.5f) - h;
			}
			placed.push_back({key, x, y, (float)w, (float)h, false, true, a, true});
		}
	};
}
