#include "pagerender.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <type_traits>

namespace bp
{
	// GL's numbers, as main.cpp writes them (no GL headers in this project)
	namespace
	{
		constexpr unsigned TEXTURE_2D = 0x0DE1, RGBA = 0x1908, UNSIGNED_BYTE = 0x1401, FLOAT = 0x1406, TRIANGLES = 0x0004,
			ARRAY_BUFFER = 0x8892, STREAM_DRAW = 0x88E0, LINEAR = 0x2601, CLAMP_TO_EDGE = 0x812F, MIN_FILTER = 0x2801,
			MAG_FILTER = 0x2800, WRAP_S = 0x2802, WRAP_T = 0x2803, VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30,
			COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82, COLOR_BUFFER_BIT = 0x4000, BLEND = 0x0BE2, MAX_TEXTURE_SIZE = 0x0D33,
			UNPACK_ALIGNMENT = 0x0CF5, TEXTURE0 = 0x84C0;
		// a texture is at most this many rows of a page: a whole page (9 MB at 1280 px) in one upload held a frame for
		// 150 ms on a computer, so a page goes up in slices, one per frame (a long webtoon page is many)
		constexpr int TILE_H = 512;
	}

	struct PageRenderer::Gl
	{
#define F(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr;
		F(void, GenTextures, int, unsigned*)
		F(void, DeleteTextures, int, const unsigned*)
		F(void, BindTexture, unsigned, unsigned)
		F(void, TexImage2D, unsigned, int, int, int, int, int, unsigned, unsigned, const void*)
		F(void, TexParameteri, unsigned, unsigned, int)
		F(void, PixelStorei, unsigned, int)
		F(void, ActiveTexture, unsigned)
		F(void, GenBuffers, int, unsigned*)
		F(void, DeleteBuffers, int, const unsigned*)
		F(void, BindBuffer, unsigned, unsigned)
		F(void, BufferData, unsigned, long, const void*, unsigned)
		F(unsigned, CreateShader, unsigned)
		F(void, ShaderSource, unsigned, int, const char* const*, const int*)
		F(void, CompileShader, unsigned)
		F(void, GetShaderiv, unsigned, unsigned, int*)
		F(void, GetShaderInfoLog, unsigned, int, int*, char*)
		F(void, DeleteShader, unsigned)
		F(unsigned, CreateProgram)
		F(void, AttachShader, unsigned, unsigned)
		F(void, LinkProgram, unsigned)
		F(void, GetProgramiv, unsigned, unsigned, int*)
		F(void, DeleteProgram, unsigned)
		F(void, UseProgram, unsigned)
		F(int, GetAttribLocation, unsigned, const char*)
		F(int, GetUniformLocation, unsigned, const char*)
		F(void, Uniform1i, int, int)
		F(void, Uniform4f, int, float, float, float, float)
		F(void, EnableVertexAttribArray, unsigned)
		F(void, DisableVertexAttribArray, unsigned)
		F(void, VertexAttribPointer, unsigned, int, unsigned, unsigned char, int, const void*)
		F(void, DrawArrays, unsigned, int, int)
		F(void, Viewport, int, int, int, int)
		F(void, ClearColor, float, float, float, float)
		F(void, Clear, unsigned)
		F(void, Disable, unsigned)
		F(void, Enable, unsigned)
		F(void, BlendFunc, unsigned, unsigned)
		F(void, GetIntegerv, unsigned, int*)
		F(unsigned, GetError)
		F(void, BindFramebuffer, unsigned, unsigned)
		// core profile only: a vertex array object must be bound to draw
		F(void, GenVertexArrays, int, unsigned*)
		F(void, BindVertexArray, unsigned)
		F(void, DeleteVertexArrays, int, const unsigned*)
#undef F
	};

	bool PageRenderer::init(void* (*getProc)(const char*), bool es)
	{
		gles = es;
		gl = new Gl();
		bool ok = true;
		auto load = [&](auto& fn, const char* name, bool need = true) {
			fn = (std::remove_reference_t<decltype(fn)>)getProc(name);
			if (!fn && need)
			{
				fprintf(stderr, "reader: no %s\n", name);
				ok = false;
			}
		};
		load(gl->GenTextures, "glGenTextures");
		load(gl->GetError, "glGetError");
		load(gl->DeleteTextures, "glDeleteTextures");
		load(gl->BindTexture, "glBindTexture");
		load(gl->TexImage2D, "glTexImage2D");
		load(gl->TexParameteri, "glTexParameteri");
		load(gl->PixelStorei, "glPixelStorei");
		load(gl->ActiveTexture, "glActiveTexture");
		load(gl->GenBuffers, "glGenBuffers");
		load(gl->DeleteBuffers, "glDeleteBuffers");
		load(gl->BindBuffer, "glBindBuffer");
		load(gl->BufferData, "glBufferData");
		load(gl->CreateShader, "glCreateShader");
		load(gl->ShaderSource, "glShaderSource");
		load(gl->CompileShader, "glCompileShader");
		load(gl->GetShaderiv, "glGetShaderiv");
		load(gl->GetShaderInfoLog, "glGetShaderInfoLog");
		load(gl->DeleteShader, "glDeleteShader");
		load(gl->CreateProgram, "glCreateProgram");
		load(gl->AttachShader, "glAttachShader");
		load(gl->LinkProgram, "glLinkProgram");
		load(gl->GetProgramiv, "glGetProgramiv");
		load(gl->DeleteProgram, "glDeleteProgram");
		load(gl->UseProgram, "glUseProgram");
		load(gl->GetAttribLocation, "glGetAttribLocation");
		load(gl->GetUniformLocation, "glGetUniformLocation");
		load(gl->Uniform1i, "glUniform1i");
		load(gl->Uniform4f, "glUniform4f");
		load(gl->EnableVertexAttribArray, "glEnableVertexAttribArray");
		load(gl->DisableVertexAttribArray, "glDisableVertexAttribArray");
		load(gl->VertexAttribPointer, "glVertexAttribPointer");
		load(gl->DrawArrays, "glDrawArrays");
		load(gl->Viewport, "glViewport");
		load(gl->ClearColor, "glClearColor");
		load(gl->Clear, "glClear");
		load(gl->Disable, "glDisable");
		load(gl->Enable, "glEnable");
		load(gl->BlendFunc, "glBlendFunc");
		load(gl->GetIntegerv, "glGetIntegerv");
		load(gl->BindFramebuffer, "glBindFramebuffer");
		load(gl->GenVertexArrays, "glGenVertexArrays", !gles);
		load(gl->BindVertexArray, "glBindVertexArray", !gles);
		load(gl->DeleteVertexArrays, "glDeleteVertexArrays", !gles);
		if (!ok) return false;

		// GLES 2 on the handheld, a 3.2 core context on a computer: the same shader in each one's dialect
		const char* vs = gles
			? "#version 100\nattribute vec2 pos; attribute vec2 uv; varying vec2 v;\n"
			  "void main() { v = uv; gl_Position = vec4(pos, 0.0, 1.0); }\n"
			: "#version 150\nin vec2 pos; in vec2 uv; out vec2 v;\n"
			  "void main() { v = uv; gl_Position = vec4(pos, 0.0, 1.0); }\n";
		const char* fs = gles
			? "#version 100\nprecision mediump float; varying vec2 v; uniform sampler2D tex; uniform vec4 tint;\n"
			  "void main() { gl_FragColor = texture2D(tex, v) * tint; }\n"
			: "#version 150\nin vec2 v; out vec4 o; uniform sampler2D tex; uniform vec4 tint;\n"
			  "void main() { o = texture(tex, v) * tint; }\n";
		auto compile = [&](unsigned kind, const char* src) -> unsigned {
			unsigned s = gl->CreateShader(kind);
			gl->ShaderSource(s, 1, &src, nullptr);
			gl->CompileShader(s);
			int done = 0;
			gl->GetShaderiv(s, COMPILE_STATUS, &done);
			if (!done)
			{
				char log[512] = {0};
				gl->GetShaderInfoLog(s, sizeof log, nullptr, log);
				fprintf(stderr, "reader: shader: %s\n", log);
				gl->DeleteShader(s);
				return 0;
			}
			return s;
		};
		unsigned v = compile(VERTEX_SHADER, vs), f = compile(FRAGMENT_SHADER, fs);
		if (!v || !f) return false;
		prog = gl->CreateProgram();
		gl->AttachShader(prog, v);
		gl->AttachShader(prog, f);
		gl->LinkProgram(prog);
		gl->DeleteShader(v);
		gl->DeleteShader(f);
		int linked = 0;
		gl->GetProgramiv(prog, LINK_STATUS, &linked);
		if (!linked)
		{
			fprintf(stderr, "reader: the shader didn't link\n");
			return false;
		}
		aPos = gl->GetAttribLocation(prog, "pos");
		aUv = gl->GetAttribLocation(prog, "uv");
		uTint = gl->GetUniformLocation(prog, "tint");
		gl->GenBuffers(1, &vbo);
		if (!gles) gl->GenVertexArrays(1, &vao);
		gl->GetIntegerv(MAX_TEXTURE_SIZE, &maxTex);
		if (maxTex <= 0) maxTex = 2048;
		// a page not decoded yet: a dark grey block of its size, so the strip's length is there from the start
		unsigned char px[4] = {40, 40, 44, 255};
		gl->GenTextures(1, &grey);
		gl->BindTexture(TEXTURE_2D, grey);
		gl->TexImage2D(TEXTURE_2D, 0, RGBA, 1, 1, 0, RGBA, UNSIGNED_BYTE, px);
		gl->TexParameteri(TEXTURE_2D, MIN_FILTER, LINEAR);
		gl->TexParameteri(TEXTURE_2D, MAG_FILTER, LINEAR);
		// the loading ring's dots: white, coloured by the tint
		unsigned char wpx[4] = {255, 255, 255, 255};
		gl->GenTextures(1, &white);
		gl->BindTexture(TEXTURE_2D, white);
		gl->TexImage2D(TEXTURE_2D, 0, RGBA, 1, 1, 0, RGBA, UNSIGNED_BYTE, wpx);
		gl->TexParameteri(TEXTURE_2D, MIN_FILTER, LINEAR);
		gl->TexParameteri(TEXTURE_2D, MAG_FILTER, LINEAR);
		gl->BindTexture(TEXTURE_2D, 0);
		fprintf(stderr, "reader: GL ready (%s, textures up to %d px)\n", gles ? "GLES 2" : "GL 3.2 core", maxTex);
		return true;
	}

	void PageRenderer::shutdown()
	{
		if (!gl) return;
		for (auto& p : pages) freePage(p);
		pages.clear();
		if (!dead.empty()) gl->DeleteTextures((int)dead.size(), dead.data());
		dead.clear();
		if (grey) gl->DeleteTextures(1, &grey);
		if (white) gl->DeleteTextures(1, &white);
		if (vbo) gl->DeleteBuffers(1, &vbo);
		if (vao) gl->DeleteVertexArrays(1, &vao);
		if (prog) gl->DeleteProgram(prog);
		grey = white = vbo = vao = prog = 0;
		delete gl;
		gl = nullptr;
	}

	PageRenderer::Page* PageRenderer::find(int index)
	{
		for (auto& p : pages)
			if (p.index == index && !p.incoming) return &p;
		return nullptr;
	}

	const PageRenderer::Page* PageRenderer::find(int index) const
	{
		for (auto& p : pages)
			if (p.index == index && !p.incoming) return &p;
		return nullptr;
	}

	bool PageRenderer::has(int index) const { return find(index) != nullptr; }

	void PageRenderer::addPage(int index, int w, int h, std::shared_ptr<const std::vector<uint8_t>> rgba)
	{
		if (w <= 0 || h <= 0 || !rgba || rgba->size() < (size_t)w * h * 4) return;
		// a page shown already: this one waits beside it (a sharper one still on its way is replaced by this one)
		bool replacing = find(index) != nullptr;
		for (auto it = pages.begin(); it != pages.end(); ++it)
			if (it->index == index && it->incoming) { freePage(*it); pages.erase(it); break; }
		Page p;
		p.incoming = replacing;
		p.index = index;
		p.w = w;
		p.h = h;
		p.rgba = std::move(rgba);
		int th = std::min(TILE_H, maxTex);
		for (int y = 0; y < h; y += th) p.tiles.push_back({0, y, std::min(th, h - y)});
		pages.push_back(std::move(p));
	}

	void PageRenderer::freePage(Page& p)
	{
		for (auto& t : p.tiles)
			if (t.tex)
			{
				// not now: this can run with the bottom panel's GL context current (its renderer has textures of the
				// same numbers: the cards' corners went to pieces). Freed at the next draw, with the top's current
				dead.push_back(t.tex);
				bytes -= (size_t)p.w * t.h * 4;
				t.tex = 0;
			}
	}

	void PageRenderer::flushDead()
	{
		if (!gl || dead.empty()) return;
		gl->DeleteTextures((int)dead.size(), dead.data());
		dead.clear();
	}

	void PageRenderer::keepOnly(const std::set<int>& keep)
	{
		for (auto it = pages.begin(); it != pages.end();)
		{
			if (keep.count(it->index)) { ++it; continue; }
			freePage(*it);
			it = pages.erase(it);
		}
	}

	void PageRenderer::uploadOne()
	{
		// the first page with a tile still down; one tile per frame keeps a frame under its 16 ms
		for (auto& p : pages)
		{
			if (p.uploaded >= p.tiles.size() || p.stalled) continue;
			Tile& t = p.tiles[p.uploaded];
			while (gl->GetError() != 0) {}	// (an error left by mpv or an earlier call isn't this upload's)
			gl->GenTextures(1, &t.tex);
			gl->BindTexture(TEXTURE_2D, t.tex);
			gl->PixelStorei(UNPACK_ALIGNMENT, 1);
			gl->TexImage2D(TEXTURE_2D, 0, RGBA, p.w, t.h, 0, RGBA, UNSIGNED_BYTE, p.rgba->data() + (size_t)t.y * p.w * 4);
			if (gl->GetError() == 0x0505 /* GL_OUT_OF_MEMORY */)
			{
				// The GPU's memory is the system's (Panfrost): with the extensions' server running it can run out.
				// Give back every page not on screen and try once more; after that the page waits for a Retry
				// (the reader's Try again) instead of staying grey with nothing said.
				ooms++;
				gl->PixelStorei(UNPACK_ALIGNMENT, 4);
				gl->BindTexture(TEXTURE_2D, 0);
				gl->DeleteTextures(1, &t.tex);
				t.tex = 0;
				size_t freed = 0;
				int index = p.index;
				for (auto it = pages.begin(); it != pages.end();)
				{
					if (it->index == index || onScreen.count(it->index)) { ++it; continue; }
					freed += it->uploaded;
					freePage(*it);
					it = pages.erase(it);
				}
				Page* q = find(index);
				if (!q) return;
				if (freed > 0 && q->oomTries == 0)
				{
					q->oomTries++;
					fprintf(stderr, "reader: GPU out of memory at page %d (%zu MB of textures): freed %zu tiles, trying again\n",
						index + 1, bytes >> 20, freed);
					return;
				}
				q->stalled = true;
				fprintf(stderr, "reader: GPU out of memory at page %d (%zu MB of textures): waiting for Try again\n", index + 1,
					bytes >> 20);
				return;
			}
			gl->TexParameteri(TEXTURE_2D, MIN_FILTER, LINEAR);
			gl->TexParameteri(TEXTURE_2D, MAG_FILTER, LINEAR);
			gl->TexParameteri(TEXTURE_2D, WRAP_S, CLAMP_TO_EDGE);
			gl->TexParameteri(TEXTURE_2D, WRAP_T, CLAMP_TO_EDGE);
			gl->PixelStorei(UNPACK_ALIGNMENT, 4);	// (GL's default, as mpv expects it)
			bytes += (size_t)p.w * t.h * 4;
			if (++p.uploaded == p.tiles.size()) p.rgba.reset();	// all up: the renderer lets go of the pixels
			return;
		}
	}

	void PageRenderer::quad(float x0, float y0, float x1, float y1, float v0, float v1, int w, int h)
	{
		// pixels (top-left origin) to GL's -1..1, y up
		float l = x0 / w * 2 - 1, r = x1 / w * 2 - 1, t = 1 - y0 / h * 2, b = 1 - y1 / h * 2;
		float d[24] = {l, t, 0, v0, r, t, 1, v0, l, b, 0, v1, r, t, 1, v0, r, b, 1, v1, l, b, 0, v1};
		gl->BufferData(ARRAY_BUFFER, sizeof d, d, STREAM_DRAW);
		gl->DrawArrays(TRIANGLES, 0, 6);
	}

	bool PageRenderer::draw(const Strip& strip, int w, int h)
	{
		std::vector<Placed> placed;
		auto vis = strip.visible();
		double off = strip.offset();
		for (int i = vis.first; i >= 0 && i <= vis.second; i++)
			placed.push_back({i, 0, (float)(strip.pageTop(i) - off), (float)w, (float)strip.pageHeight(i)});
		return draw(placed, w, h);
	}

	bool PageRenderer::draw(const std::vector<Placed>& placed, int w, int h)
	{
		if (!gl || !prog) return false;
		// (draw runs with the top window's context current: what freePage put aside goes here)
		if (!dead.empty())
		{
			gl->DeleteTextures((int)dead.size(), dead.data());
			dead.clear();
		}
		uploadOne();
		// a sharper page all up: it takes the old one's place now, in one frame
		for (auto it = pages.begin(); it != pages.end();)
		{
			if (!it->incoming || it->uploaded < it->tiles.size()) { ++it; continue; }
			int index = it->index;
			it->incoming = false;
			for (auto old = pages.begin(); old != pages.end(); ++old)
				if (old->index == index && &*old != &*it) { freePage(*old); pages.erase(old); break; }
			it = pages.begin();	// (the erase moved things: start over, nothing incoming is left complete twice)
		}
		// the screen's: mpv may have left its hidden target bound (what plays under the reader goes on there)
		gl->BindFramebuffer(0x8D40 /* GL_FRAMEBUFFER */, 0);
		gl->Viewport(0, 0, w, h);
		gl->ClearColor(0, 0, 0, 1);
		gl->Clear(COLOR_BUFFER_BIT);
		gl->Disable(BLEND);	// pages are opaque; a transparent PNG shows on black as it would on paper's absence
		gl->UseProgram(prog);
		if (vao) gl->BindVertexArray(vao);
		gl->BindBuffer(ARRAY_BUFFER, vbo);
		gl->EnableVertexAttribArray(aPos);
		gl->EnableVertexAttribArray(aUv);
		gl->VertexAttribPointer(aPos, 2, FLOAT, 0, 16, (const void*)0);
		gl->VertexAttribPointer(aUv, 2, FLOAT, 0, 16, (const void*)8);
		gl->ActiveTexture(TEXTURE0);
		gl->Uniform4f(uTint, 1, 1, 1, 1);
		bool pending = false;
		spunLast = false;
		onScreen.clear();
		for (auto& pl : placed) onScreen.insert(pl.page);
		for (auto& pl : placed)
		{
			const Page* p = find(pl.page);
			if (!p || p->uploaded < p->tiles.size())
			{
				// not here yet (or not all up): its place, grey, so nothing around it moves when it comes
				gl->BindTexture(TEXTURE_2D, grey);
				quad(pl.x, pl.y, pl.x + pl.w, pl.y + pl.h, 0, 1, w, h);
				pending = pending || (p && !p->stalled);
				if (pl.spin)
				{
					// a ring of 8 dots in the middle of the place's part on screen, the brightest one going round
					float vx0 = std::max(pl.x, 0.f), vx1 = std::min(pl.x + pl.w, (float)w);
					float vy0 = std::max(pl.y, 0.f), vy1 = std::min(pl.y + pl.h, (float)h);
					float s = (float)std::min(w, h);
					float ring = s * 0.045f, dot = std::max(3.f, s * 0.011f);
					if (vx1 - vx0 > 2 * (ring + dot) && vy1 - vy0 > 2 * (ring + dot))
					{
						float cx = (vx0 + vx1) / 2, cy = (vy0 + vy1) / 2;
						gl->BindTexture(TEXTURE_2D, white);
						for (int k = 0; k < 8; k++)
						{
							int age = ((spinStep - k) % 8 + 8) % 8;	// 0: the head
							float lum = 0.85f - age * 0.08f;
							float a = (float)(k * 3.14159265 / 4);
							float dx = cx + ring * std::sin(a), dy = cy - ring * std::cos(a);
							gl->Uniform4f(uTint, lum, lum, lum * 0.95f, 1);
							quad(dx - dot, dy - dot, dx + dot, dy + dot, 0, 1, w, h);
						}
						gl->Uniform4f(uTint, 1, 1, 1, 1);
						spunLast = true;
					}
				}
				continue;
			}
			// (a card over the pages, not a page: its round corners blended over what's under it, straight alpha)
			if (pl.blend && gl->Enable && gl->BlendFunc)
			{
				gl->Enable(BLEND);
				gl->BlendFunc(0x0302 /* GL_SRC_ALPHA */, 0x0303 /* GL_ONE_MINUS_SRC_ALPHA */);
			}
			for (auto& t : p->tiles)
			{
				// the tile's rows, scaled to the page's height on screen
				float y0 = pl.y + pl.h * t.y / p->h, y1 = pl.y + pl.h * (t.y + t.h) / p->h;
				if (y1 < 0 || y0 > h || pl.x + pl.w < 0 || pl.x > w) continue;
				gl->BindTexture(TEXTURE_2D, t.tex);
				quad(pl.x, y0, pl.x + pl.w, y1, 0, 1, w, h);
			}
			if (pl.blend) gl->Disable(BLEND);
		}
		// mpv draws next in this context: what it might rely on goes back to nothing bound
		gl->DisableVertexAttribArray(aPos);
		gl->DisableVertexAttribArray(aUv);
		gl->BindTexture(TEXTURE_2D, 0);
		gl->BindBuffer(ARRAY_BUFFER, 0);
		if (vao) gl->BindVertexArray(0);
		gl->UseProgram(0);
		for (auto& p : pages)
			if (p.uploaded < p.tiles.size() && !p.stalled) pending = true;
		return pending;
	}

	bool PageRenderer::stalled(int index) const
	{
		const Page* p = find(index);
		return p && p->stalled;
	}

	void PageRenderer::unstall()
	{
		for (auto& p : pages)
		{
			p.stalled = false;
			p.oomTries = 0;
		}
	}
}
