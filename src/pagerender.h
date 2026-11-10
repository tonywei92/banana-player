#pragma once
// The manga reader's pages on the top panel (docs/design/manga.md section 7): decoded RGBA pages as GL textures in
// the top window's own context (the one mpv draws with), placed by a Strip. Main thread only; it loads its GL calls
// at runtime as main.cpp does (no GL headers), and leaves the context as mpv expects it after each draw.
#include "strip.h"
#include <cstddef>
#include <cstdint>
#include <deque>
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
		void addPage(int index, int w, int h, std::vector<uint8_t>&& rgba);
		bool has(int index) const;
		// the textures outside keep (the pages near the view) freed, the farthest first
		void keepOnly(const std::set<int>& keep);
		// one frame of the strip into the bound framebuffer (w x h pixels); true while tiles still wait to go up
		bool draw(const Strip& strip, int w, int h);
		// the same for any layout: each page's rectangle in view pixels (the paged modes)
		struct Placed { int page; float x, y, w, h; };
		bool draw(const std::vector<Placed>& placed, int w, int h);
		size_t textureBytes() const { return bytes; }

	private:
		struct Tile
		{
			unsigned tex = 0;
			int y = 0, h = 0;	// its rows of the page
		};
		struct Page
		{
			int index = -1, w = 0, h = 0;
			std::vector<uint8_t> rgba;	// until every tile is up
			std::vector<Tile> tiles;
			size_t uploaded = 0;	// tiles up so far
			// a sharper copy of a page already shown (a zoom): it goes up while the old one is still drawn, and
			// takes its place only once all of it is on the GPU (no grey frame between)
			bool incoming = false;
		};
		Page* find(int index);
		const Page* find(int index) const;
		void uploadOne();
		void freePage(Page& p);
		void quad(float x0, float y0, float x1, float y1, float v0, float v1, int w, int h);

		struct Gl;
		Gl* gl = nullptr;
		bool gles = false;
		unsigned prog = 0, vbo = 0, vao = 0, grey = 0;
		int aPos = -1, aUv = -1, uTint = -1;
		int maxTex = 2048;
		std::deque<Page> pages;
		size_t bytes = 0;
		std::vector<unsigned> dead;	// textures to delete at the next draw, in the top window's context
	};
}
