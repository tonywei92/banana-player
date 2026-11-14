#pragma once
// The manga reader's pages on the top panel (docs/design/manga.md section 7): decoded RGBA pages as GL textures in
// the top window's own context (the one mpv draws with), placed by a Strip. Main thread only; it loads its GL calls
// at runtime as main.cpp does (no GL headers), and leaves the context as mpv expects it after each draw.
#include "strip.h"
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
		struct Placed { int page; float x, y, w, h; bool spin = false; bool blend = false; float alpha = 1; };	// blend: a card over the pages (alpha: its fade)
		bool draw(const std::vector<Placed>& placed, int w, int h);
		size_t textureBytes() const { return bytes; }
		// every pixel times this colour (a reading filter: warm, dim); 1,1,1 is none
		void setTint(float r, float g, float b) { tint[0] = r; tint[1] = g; tint[2] = b; }
		// all waiting tiles up in one draw, not one a draw (the text viewer's rows: small, and wanted at once)
		void setUploadAll(bool on) { uploadAll = on; }
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
		int ooms = 0;
		std::set<int> onScreen;	// the pages the last draw placed (an out-of-memory upload frees the rest)
		bool spunLast = false;
		int aPos = -1, aUv = -1, uTint = -1;
		int maxTex = 2048;
		std::deque<Page> pages;
		size_t bytes = 0;
		std::vector<unsigned> dead;	// textures to delete at the next draw, in the top window's context
	};
}
