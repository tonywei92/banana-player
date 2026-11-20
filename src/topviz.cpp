// The native visualizers on the top panel (topscreen.h, docs/notes/2026-11-19-viz-native.md). A frame: the theme's
// ground cleared, the style's shapes in plain GL (one small shader, coloured triangles in one buffer a frame), then
// mpv's picture over them: mpv draws the art, the song's lines and the cards on a clear ground into a layer of ours
// (rendered again only when mpv has a new frame), composed over the shapes, so the visualizer passes under the lines
// with no box round them. Then the panel is presented. Nothing else swaps the top window while the style is up, so
// this path's own swap is what puts each frame on the glass (KMSDRM: Lite's lesson).
#include "player.h"
#include "artbackdrop.h"
#include "settings.h"
#include "theme.h"
#include "topscreen.h"
#include "viznative.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

namespace bp
{
	namespace
	{
		constexpr unsigned GL_TRIANGLES_ = 0x0004, GL_FLOAT_ = 0x1406, GL_UNSIGNED_BYTE_ = 0x1401, GL_ARRAY_BUFFER_ = 0x8892,
			GL_STREAM_DRAW_ = 0x88E0, GL_STATIC_DRAW_ = 0x88E4, GL_BLEND_ = 0x0BE2, GL_DEPTH_TEST_ = 0x0B71, GL_SCISSOR_TEST_ = 0x0C11,
			GL_CULL_FACE_ = 0x0B44, GL_VERTEX_SHADER_ = 0x8B31, GL_FRAGMENT_SHADER_ = 0x8B30, GL_COMPILE_STATUS_ = 0x8B81,
			GL_LINK_STATUS_ = 0x8B82, GL_ONE_ = 1, GL_SRC_ALPHA_ = 0x0302, GL_ONE_MINUS_SRC_ALPHA_ = 0x0303, GL_TEXTURE_2D_ = 0x0DE1,
			GL_RGBA_ = 0x1908, GL_TEXTURE_MIN_FILTER_ = 0x2801, GL_TEXTURE_MAG_FILTER_ = 0x2800, GL_NEAREST_ = 0x2600, GL_LINEAR_ = 0x2601,
			GL_TEXTURE_WRAP_S_ = 0x2802, GL_TEXTURE_WRAP_T_ = 0x2803, GL_CLAMP_TO_EDGE_ = 0x812F, GL_FRAMEBUFFER_ = 0x8D40,
			GL_COLOR_ATTACHMENT0_ = 0x8CE0, GL_FRAMEBUFFER_COMPLETE_ = 0x8CD5, GL_COLOR_BUFFER_BIT_ = 0x4000, GL_TEXTURE0_ = 0x84C0;

		// a vertex: the panel's place (GL's -1..1) and its colour as bytes (12 bytes: half the floats' upload)
		struct Vert
		{
			float x, y;
			unsigned char c[4];
		};

		struct VizGl
		{
			bool tried = false, ok = false;
			unsigned prog = 0, vbo = 0, vao = 0;
			int aPos = -1, aCol = -1;
			// mpv's layer: its picture on a clear ground, composed over the shapes (lines over the visualizer)
			unsigned layerProg = 0, layerVbo = 0, layerTex = 0, layerFbo = 0;
			int lPos = -1, lTex = -1, layerW = 0, layerH = 0;
			// Match album art's ground under the shapes: the backdrop page as a texture of the shapes' own, and a
			// program that paints a shape with it at the shape's place on the panel (its VI_BG ink: what hides the
			// rows behind Grid's ridges, the sun under Horizon's line) — the art shows there, not a band of BG
			unsigned groundProg = 0, groundTex = 0;
			int gPos = -1, gCol = -1, gTex = -1;
			uint64_t groundOf = 0;	// the page's serial in the texture (0: none)
			bool layerTried = false, layerOk = false, layerFresh = false;
			// kept from frame to frame: nothing allocated a frame once they've grown
			std::vector<VizRect> shapes;
			std::vector<Vert> verts;
#define VIZ_GL(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr;
			VIZ_GL(unsigned, CreateShader, unsigned)
			VIZ_GL(void, ShaderSource, unsigned, int, const char* const*, const int*)
			VIZ_GL(void, CompileShader, unsigned)
			VIZ_GL(void, GetShaderiv, unsigned, unsigned, int*)
			VIZ_GL(void, GetShaderInfoLog, unsigned, int, int*, char*)
			VIZ_GL(void, DeleteShader, unsigned)
			VIZ_GL(unsigned, CreateProgram)
			VIZ_GL(void, AttachShader, unsigned, unsigned)
			VIZ_GL(void, LinkProgram, unsigned)
			VIZ_GL(void, GetProgramiv, unsigned, unsigned, int*)
			VIZ_GL(void, UseProgram, unsigned)
			VIZ_GL(int, GetAttribLocation, unsigned, const char*)
			VIZ_GL(int, GetUniformLocation, unsigned, const char*)
			VIZ_GL(void, Uniform1i, int, int)
			VIZ_GL(void, GenBuffers, int, unsigned*)
			VIZ_GL(void, BindBuffer, unsigned, unsigned)
			VIZ_GL(void, BufferData, unsigned, long, const void*, unsigned)
			VIZ_GL(void, EnableVertexAttribArray, unsigned)
			VIZ_GL(void, DisableVertexAttribArray, unsigned)
			VIZ_GL(void, VertexAttribPointer, unsigned, int, unsigned, unsigned char, int, const void*)
			VIZ_GL(void, DrawArrays, unsigned, int, int)
			VIZ_GL(void, Viewport, int, int, int, int)
			VIZ_GL(void, Disable, unsigned)
			VIZ_GL(void, Enable, unsigned)
			VIZ_GL(void, BlendFunc, unsigned, unsigned)
			VIZ_GL(void, Scissor, int, int, int, int)
			VIZ_GL(void, ClearColor, float, float, float, float)
			VIZ_GL(void, Clear, unsigned)
			VIZ_GL(void, Finish)
			VIZ_GL(void, GenTextures, int, unsigned*)
			VIZ_GL(void, BindTexture, unsigned, unsigned)
			VIZ_GL(void, ActiveTexture, unsigned)
			VIZ_GL(void, TexImage2D, unsigned, int, int, int, int, int, unsigned, unsigned, const void*)
			VIZ_GL(void, TexParameteri, unsigned, unsigned, int)
			VIZ_GL(void, GenFramebuffers, int, unsigned*)
			VIZ_GL(void, BindFramebuffer, unsigned, unsigned)
			VIZ_GL(void, FramebufferTexture2D, unsigned, unsigned, unsigned, unsigned, int)
			VIZ_GL(unsigned, CheckFramebufferStatus, unsigned)
			VIZ_GL(void, GenVertexArrays, int, unsigned*)
			VIZ_GL(void, BindVertexArray, unsigned)
#undef VIZ_GL
		};

		unsigned compileProgram(VizGl& g, const char* vs, const char* fs)
		{
			auto compile = [&](unsigned kind, const char* src) -> unsigned {
				unsigned s = g.CreateShader(kind);
				g.ShaderSource(s, 1, &src, nullptr);
				g.CompileShader(s);
				int done = 0;
				g.GetShaderiv(s, GL_COMPILE_STATUS_, &done);
				if (!done)
				{
					char log[512] = {0};
					g.GetShaderInfoLog(s, sizeof log, nullptr, log);
					fprintf(stderr, "viz-native: shader: %s\n", log);
					g.DeleteShader(s);
					return 0;
				}
				return s;
			};
			unsigned v = compile(GL_VERTEX_SHADER_, vs), f = compile(GL_FRAGMENT_SHADER_, fs);
			if (!v || !f) return 0;
			unsigned p = g.CreateProgram();
			g.AttachShader(p, v);
			g.AttachShader(p, f);
			g.LinkProgram(p);
			g.DeleteShader(v);
			g.DeleteShader(f);
			int linked = 0;
			g.GetProgramiv(p, GL_LINK_STATUS_, &linked);
			if (!linked)
			{
				fprintf(stderr, "viz-native: a shader didn't link\n");
				return 0;
			}
			return p;
		}

		// made once, in the top panel's context (current when called)
		VizGl& vizGl()
		{
			static VizGl g;
			if (g.tried) return g;
			g.tried = true;
			bool ok = true;
			auto load = [&](auto& fn, const char* name, bool need) {
				fn = (std::remove_reference_t<decltype(fn)>)SDL_GL_GetProcAddress(name);
				if (!fn && need)
				{
					fprintf(stderr, "viz-native: no %s: the bars can't be drawn\n", name);
					ok = false;
				}
			};
#define VIZ_LOAD(name, need) load(g.name, "gl" #name, need);
			VIZ_LOAD(CreateShader, true)
			VIZ_LOAD(ShaderSource, true)
			VIZ_LOAD(CompileShader, true)
			VIZ_LOAD(GetShaderiv, true)
			VIZ_LOAD(GetShaderInfoLog, true)
			VIZ_LOAD(DeleteShader, true)
			VIZ_LOAD(CreateProgram, true)
			VIZ_LOAD(AttachShader, true)
			VIZ_LOAD(LinkProgram, true)
			VIZ_LOAD(GetProgramiv, true)
			VIZ_LOAD(UseProgram, true)
			VIZ_LOAD(GetAttribLocation, true)
			VIZ_LOAD(GetUniformLocation, true)
			VIZ_LOAD(Uniform1i, true)
			VIZ_LOAD(GenBuffers, true)
			VIZ_LOAD(BindBuffer, true)
			VIZ_LOAD(BufferData, true)
			VIZ_LOAD(EnableVertexAttribArray, true)
			VIZ_LOAD(DisableVertexAttribArray, true)
			VIZ_LOAD(VertexAttribPointer, true)
			VIZ_LOAD(DrawArrays, true)
			VIZ_LOAD(Viewport, true)
			VIZ_LOAD(Disable, true)
			VIZ_LOAD(Enable, true)
			VIZ_LOAD(BlendFunc, true)
			VIZ_LOAD(Scissor, true)
			VIZ_LOAD(ClearColor, true)
			VIZ_LOAD(Clear, true)
			VIZ_LOAD(Finish, true)
			VIZ_LOAD(GenTextures, true)
			VIZ_LOAD(BindTexture, true)
			VIZ_LOAD(ActiveTexture, true)
			VIZ_LOAD(TexImage2D, true)
			VIZ_LOAD(TexParameteri, true)
			// (the layer only: without them, mpv's picture is drawn under the shapes as before)
			VIZ_LOAD(GenFramebuffers, false)
			VIZ_LOAD(BindFramebuffer, false)
			VIZ_LOAD(FramebufferTexture2D, false)
			VIZ_LOAD(CheckFramebufferStatus, false)
#ifdef BP_GLES
			VIZ_LOAD(GenVertexArrays, false)
			VIZ_LOAD(BindVertexArray, false)
#else
			VIZ_LOAD(GenVertexArrays, true)	// (a core profile draws nothing without one)
			VIZ_LOAD(BindVertexArray, true)
#endif
#undef VIZ_LOAD
			if (!ok) return g;
#ifdef BP_GLES
			const char* vs = "#version 100\nattribute vec2 pos; attribute vec4 col; varying vec4 c;\n"
							 "void main() { c = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* fs = "#version 100\nprecision mediump float; varying vec4 c;\nvoid main() { gl_FragColor = c; }\n";
			const char* lvs = "#version 100\nattribute vec2 pos; varying vec2 uv;\nvoid main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* lfs = "#version 100\nprecision mediump float; varying vec2 uv; uniform sampler2D tex;\n"
							  "void main() { gl_FragColor = texture2D(tex, uv); }\n";
			const char* gvs = "#version 100\nattribute vec2 pos; attribute vec4 col; varying vec2 uv; varying float a;\n"
							  "void main() { uv = vec2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5); a = col.a; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* gfs = "#version 100\nprecision mediump float; varying vec2 uv; varying float a; uniform sampler2D tex;\n"
							  "void main() { gl_FragColor = vec4(texture2D(tex, uv).rgb, a); }\n";
#else
			const char* vs = "#version 150\nin vec2 pos; in vec4 col; out vec4 c;\nvoid main() { c = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* fs = "#version 150\nin vec4 c; out vec4 o;\nvoid main() { o = c; }\n";
			const char* lvs = "#version 150\nin vec2 pos; out vec2 uv;\nvoid main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* lfs = "#version 150\nin vec2 uv; uniform sampler2D tex; out vec4 o;\nvoid main() { o = texture(tex, uv); }\n";
			const char* gvs = "#version 150\nin vec2 pos; in vec4 col; out vec2 uv; out float a;\n"
							  "void main() { uv = vec2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5); a = col.a; gl_Position = vec4(pos, 0.0, 1.0); }\n";
			const char* gfs = "#version 150\nin vec2 uv; in float a; uniform sampler2D tex; out vec4 o;\nvoid main() { o = vec4(texture(tex, uv).rgb, a); }\n";
#endif
			g.prog = compileProgram(g, vs, fs);
			if (!g.prog) return g;
			g.aPos = g.GetAttribLocation(g.prog, "pos");
			g.aCol = g.GetAttribLocation(g.prog, "col");
			g.GenBuffers(1, &g.vbo);
			if (g.GenVertexArrays) g.GenVertexArrays(1, &g.vao);
			g.ok = g.aPos >= 0 && g.aCol >= 0;
			g.groundProg = compileProgram(g, gvs, gfs);
			if (g.groundProg)
			{
				g.gPos = g.GetAttribLocation(g.groundProg, "pos");
				g.gCol = g.GetAttribLocation(g.groundProg, "col");
				g.gTex = g.GetUniformLocation(g.groundProg, "tex");
				if (g.gPos < 0 || g.gCol < 0) g.groundProg = 0;
			}
			g.layerProg = compileProgram(g, lvs, lfs);
			if (g.layerProg)
			{
				g.lPos = g.GetAttribLocation(g.layerProg, "pos");
				g.lTex = g.GetUniformLocation(g.layerProg, "tex");
				const float quad[12] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
				g.GenBuffers(1, &g.layerVbo);
				g.BindBuffer(GL_ARRAY_BUFFER_, g.layerVbo);
				g.BufferData(GL_ARRAY_BUFFER_, sizeof quad, quad, GL_STATIC_DRAW_);
				g.BindBuffer(GL_ARRAY_BUFFER_, 0);
			}
			SDL_DisplayMode mode{};
			const int hz = SDL_GetCurrentDisplayMode(0, &mode) == 0 ? mode.refresh_rate : 0;
			fprintf(stderr, "viz-native: GL ready%s (display 0 at %d Hz)\n", g.ok ? "" : " but the shader has no attributes: no bars", hz);
			return g;
		}

		// the layer mpv draws into at the panel's pixels (made again when they change); false: mpv draws on the panel
		bool layerFor(VizGl& g, int w, int h)
		{
			if (!g.GenFramebuffers || !g.BindFramebuffer || !g.FramebufferTexture2D || !g.CheckFramebufferStatus || !g.layerProg || g.lPos < 0)
			{
				if (!g.layerTried) fprintf(stderr, "viz-native: no layer (no framebuffers here): mpv's picture under the shapes\n");
				g.layerTried = true;
				return false;
			}
			if (g.layerTried && !g.layerOk) return false;
			if (g.layerOk && g.layerW == w && g.layerH == h) return true;
			if (!g.layerTex) g.GenTextures(1, &g.layerTex);
			if (!g.layerFbo) g.GenFramebuffers(1, &g.layerFbo);
			g.BindTexture(GL_TEXTURE_2D_, g.layerTex);
			g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_NEAREST_);
			g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_NEAREST_);
			g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_TO_EDGE_);
			g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_T_, GL_CLAMP_TO_EDGE_);
			g.TexImage2D(GL_TEXTURE_2D_, 0, GL_RGBA_, w, h, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, nullptr);
			g.BindTexture(GL_TEXTURE_2D_, 0);
			g.BindFramebuffer(GL_FRAMEBUFFER_, g.layerFbo);
			g.FramebufferTexture2D(GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, GL_TEXTURE_2D_, g.layerTex, 0);
			const unsigned st = g.CheckFramebufferStatus(GL_FRAMEBUFFER_);
			g.BindFramebuffer(GL_FRAMEBUFFER_, 0);
			g.layerTried = true;
			g.layerOk = st == GL_FRAMEBUFFER_COMPLETE_;
			g.layerW = w, g.layerH = h, g.layerFresh = false;
			fprintf(stderr, "viz-native: mpv's layer %dx%d%s\n", w, h, g.layerOk ? ": the lines over the shapes" : " incomplete: mpv's picture under the shapes");
			return g.layerOk;
		}

		// a disc's or a ring's sides at r pixels: the most a side's middle may sit inside the true circle ~0.35 px
		// (a big cloud 48 sides, not 256; a spark 6)
		int circleSides(float r)
		{
			if (r < 2) return 6;
			const int n = (int)std::ceil(M_PI / std::acos(1 - 0.35 / r));
			return std::clamp(n, 6, 64);
		}

		// a run of shapes drawn alike: blending on (some aren't opaque) or off (all opaque: the GPU may skip what a
		// later opaque one covers, and writes without reading the tile)
		struct Run
		{
			int first, count;
			bool blend;
			bool ground = false;	// painted with the backdrop's page (VizGl::groundProg), not its vertex colour
		};

		bool groundTexture(VizGl& g, const std::shared_ptr<const std::vector<uint8_t>>& page, uint64_t serial)
		{
			// the backdrop's newer page in the shapes' own texture, uploaded when it changed (a 160 x 480 page: once per
			// art and theme); false when there is none or no program to paint with it
			if (!g.groundProg || !page || page->size() != (size_t)artbg::W * 480 * 4) return false;
			if (g.groundOf != serial)
			{
				if (!g.groundTex) g.GenTextures(1, &g.groundTex);
				g.ActiveTexture(GL_TEXTURE0_);
				g.BindTexture(GL_TEXTURE_2D_, g.groundTex);
				g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MIN_FILTER_, GL_LINEAR_);
				g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_MAG_FILTER_, GL_LINEAR_);
				g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_S_, GL_CLAMP_TO_EDGE_);
				g.TexParameteri(GL_TEXTURE_2D_, GL_TEXTURE_WRAP_T_, GL_CLAMP_TO_EDGE_);
				g.TexImage2D(GL_TEXTURE_2D_, 0, GL_RGBA_, artbg::W, 480, 0, GL_RGBA_, GL_UNSIGNED_BYTE_, page->data());
				g.BindTexture(GL_TEXTURE_2D_, 0);
				g.groundOf = serial;
			}
			return true;
		}

		void useGroundProgram(VizGl& g, bool on)
		{
			// a ground run between the colour runs: its program, the same vertices (pos, and col for the alpha), the
			// page bound; then the colour program back as it was
			const unsigned prog = on ? g.groundProg : g.prog;
			const int pos = on ? g.gPos : g.aPos, col = on ? g.gCol : g.aCol;
			g.UseProgram(prog);
			g.EnableVertexAttribArray((unsigned)pos);
			g.EnableVertexAttribArray((unsigned)col);
			g.VertexAttribPointer((unsigned)pos, 2, GL_FLOAT_, 0, sizeof(Vert), nullptr);
			g.VertexAttribPointer((unsigned)col, 4, GL_UNSIGNED_BYTE_, 1, sizeof(Vert), (const void*)(2 * sizeof(float)));
			g.ActiveTexture(GL_TEXTURE0_);
			g.BindTexture(GL_TEXTURE_2D_, on ? g.groundTex : 0);
			if (on) g.Uniform1i(g.gTex, 0);
		}
	}

	int TopScreen::nativeWaitMs(Player& shown)
	{
		// the frames come from here, at the setting's rate (the tap's new levels are taken when a frame is due): the
		// loop waits till the next is due. Paused: none (mpv's own frames still draw)
		if (!nativeUp || !nativeActive || !shown.nativeShown() || shown.paused || !shown.playing()) return -1;
		const double left = (double)((int64_t)nativeNext - (int64_t)SDL_GetPerformanceCounter()) * 1000.0 / SDL_GetPerformanceFrequency() - NATIVE_EARLY_MS;
		return left <= 0 ? 0 : (int)std::ceil(left);
	}

	double TopScreen::nativePeriod(const Player& shown) const
	{
		return 1000.0 / std::clamp(shown.settings.vizFps, 30, 60);
	}

	void TopScreen::drawNative(Player& shown, std::atomic<bool>& renderPending, bool refresh)
	{
		// mpv's own news (the lines or a card changed) and the tap's (new levels)
		const bool mpvFrame = renderPending.exchange(false) && shown.frameDue();
		float got[VN_MAX_BARS];
		int vis = -1, n = 0;
		VizHint hint;
		bool fresh = shown.nativeTake(got, vis, n, hint);
		const bool first = !nativeUp;
		// another native style chosen: its own state at once (paused too), the tap's levels for it from its next look
		const bool restyled = nativeState.vis != shown.nativeStyle();
		if (first || restyled) nativeState.reset(shown.nativeStyle());
		if (fresh && vis == nativeState.vis && n == nativeState.n)
		{
			std::copy(got, got + n, nativeState.targets);
			nativeState.loud = hint.loud;
			nativeState.beats = hint.beats;
			nativeState.avg = hint.avg;
		}
		const double f = 1000.0 / SDL_GetPerformanceFrequency();
		const uint64_t pc = SDL_GetPerformanceCounter();
		// a frame when one is due (playing: the setting's rate, whether or not the tap has new levels; the shapes
		// move on by the real time), mpv has a new picture, or the panel must be drawn again. Due times on a grid
		// (the last due time plus a frame, not the last draw plus a frame): a late wake doesn't push every frame
		// after it later, so at 60 each frame meets its own refresh of the panel
		const bool moving = !shown.paused && shown.playing();
		const bool due = moving && (int64_t)(pc - nativeNext) * f >= -NATIVE_EARLY_MS;
		// mpv's new picture (the lines, the marquee's step, a card) goes into the layer with the next frame; drawn
		// at once only when no frame is coming (paused)
		if (mpvFrame) nativeLayerDirty = true;
		if (!(mpvFrame && !moving) && !due && !refresh && !first && !restyled) return;
		{
			const uint64_t period = (uint64_t)(nativePeriod(shown) / f);
			if (due) nativeNext += period;
			if ((int64_t)(nativeNext - pc) * f < -nativePeriod(shown) || (int64_t)(nativeNext - pc) * f > 2 * nativePeriod(shown)) nativeNext = pc + period;
		}
		const Uint32 now = SDL_GetTicks();
		const double dt = first ? 0 : (now - nativeAt) / 1000.0;
		nativeAt = now;
		if (first) fprintf(stderr, "viz-native: the top panel is the player's: the style, mpv's art and lines over it, swapped here\n");
		nativeUp = true;
		// paused: the shapes stay as they were (the graph's picture froze too); mpv may still ask for a frame (a card)
		nativeState.band = shown.nativeBand();
		const uint64_t b0 = SDL_GetPerformanceCounter();
		if (!shown.paused) vizStep(nativeState, dt);
		nativeCost.buildMs += (SDL_GetPerformanceCounter() - b0) * f;
		SDL_GL_MakeCurrent(top, gl);
		int w = 0, h = 0;
		SDL_GL_GetDrawableSize(top, &w, &h);
		nativeFrame(shown, w, h, nativeLayerDirty || first || refresh, true);
		nativeLayerDirty = false;
		const uint64_t s0 = SDL_GetPerformanceCounter();
		SDL_GL_SwapWindow(top);
		const uint64_t s1 = SDL_GetPerformanceCounter();
		shown.reportSwap();
		shown.nativeDrew();
		// the cost, in the log 5 s after a style starts and every 5 s for 20 s (a style that slows as it fills shows)
		NativeCost& c = nativeCost;
		const double sw = (s1 - s0) * f;
		c.swapMs += sw, c.swapMax = std::max(c.swapMax, sw);
		if (nativeLast && !first && !restyled) c.gapMax = std::max(c.gapMax, (s1 - nativeLast) * f);
		nativeLast = s1;
		c.frames++;
		if (first || restyled) c = NativeCost{s1};
		else if ((s1 - c.since) * f >= 5000)
		{
			const double fr = c.frames, secs = (s1 - c.since) * f / 1000;
			if (c.logs < 4)
			{
				char gpu[32] = "";
				if (c.gpuMs > 0) snprintf(gpu, sizeof gpu, ", GPU %.2f", c.gpuMs / fr);
				fprintf(stderr, "viz-native: draw %s %s %dx%d: %.1f frames a second; a frame %.2f ms ours (shapes %.2f, GL %.2f%s), mpv %.2f ms "
					"(%d renders), swap %.2f ms (max %.1f), the longest gap %.1f ms; %.0f vertices (max %d), %.1f draws, %.0fk px filled "
					"(%.0fk blended)\n",
					visualizerLabel(nativeState.vis), shown.nativeBand() ? "(band)" : "(alone)", w, h, fr / secs, (c.buildMs + c.glMs) / fr,
					c.buildMs / fr, c.glMs / fr, gpu, c.mpvRenders ? c.mpvMs / c.mpvRenders : 0.0, c.mpvRenders, c.swapMs / fr, c.swapMax, c.gapMax,
					c.verts / fr, c.vertsMax, c.draws / fr, c.fillPx / fr / 1000, c.blendPx / fr / 1000);
			}
			const int logs = c.logs + 1;
			c = NativeCost{s1};
			c.logs = logs;
		}
	}

	void TopScreen::nativeFrame(Player& shown, int w, int h, bool mpvNew, bool withShapes)
	{
		NativeCost& cost = nativeCost;
		const double f = 1000.0 / SDL_GetPerformanceFrequency();
		VizGl& g = vizGl();
		// mpv on a clear ground into the layer (only when it has something new: its lines, a card, the art), or, with
		// no layer (or mpv's ground not clear yet), on the panel under the shapes as before
		const bool layered = shown.groundClear() && g.ok && layerFor(g, w, h);
		// (no layer here: mpv's ground made the theme's again, its picture under the shapes)
		if (shown.groundClear() && !layered) shown.nativeLayerFailed();
		uint64_t t0 = SDL_GetPerformanceCounter();
		if (layered)
		{
			if (mpvNew || !g.layerFresh)
			{
				shown.render(w, h, g.layerFbo, false, true);
				g.layerFresh = true;
				cost.mpvMs += (SDL_GetPerformanceCounter() - t0) * f, cost.mpvRenders++;
			}
			g.BindFramebuffer(GL_FRAMEBUFFER_, 0);
		}
		else
		{
			shown.render(w, h, 0, false, true);
			cost.mpvMs += (SDL_GetPerformanceCounter() - t0) * f, cost.mpvRenders++;
		}
		if (!g.ok) return;
		bool groundArt = false;	// the backdrop is the ground: a VI_BG shape is painted with it (groundTexture)
		const bool gpuTimed = getenv("BP_VIZ_GPU") != nullptr;	// (a debug run: the GPU's time, waited for)
		if (gpuTimed) g.Finish();
		const uint64_t t1 = SDL_GetPerformanceCounter();
		g.Viewport(0, 0, w, h);
		g.Disable(GL_DEPTH_TEST_);
		g.Disable(GL_CULL_FACE_);
		g.Disable(GL_SCISSOR_TEST_);
		if (layered)
		{
			// the ground: the theme's BG (what mpv's picture had under the shapes), or the song's backdrop under
			// its veil when Match album art is on — its own pages drawn whole (the renderer clears the panel and
			// draws them stretched; the cross-fade's pair while it runs, the newer one at the fade's alpha)
			if (!bgUp || !bgReady || !drawBackdropPages(w, h))
			{
				g.ClearColor(theme::BG.r / 255.0f, theme::BG.g / 255.0f, theme::BG.b / 255.0f, 1);
				g.Clear(GL_COLOR_BUFFER_BIT_);
			}
			else groundArt = groundTexture(g, bgGroundPage, bgGroundSerial);
		}
		std::vector<Run> runs;
		if (withShapes)
		{
			std::vector<VizRect>& shapes = g.shapes;
			shapes.clear();
			vizShapes(nativeState, shown.nativeBand(), shapes);
			// the theme's inks, read each frame: a theme change shows at once (the styles' own derived inks too)
			const theme::Rgb vizInk = visualizerInk(), vizLine = visualizerLine();
			auto inkOf = [&vizInk, &vizLine](const VizRect& r, unsigned char* c) {
				theme::Rgb k;
				auto mixed = [](theme::Rgb a, theme::Rgb b, float t) {
					t = std::clamp(t, 0.0f, 1.0f);
					return theme::Rgb{(unsigned char)std::lround(a.r + (b.r - a.r) * t), (unsigned char)std::lround(a.g + (b.g - a.g) * t),
						(unsigned char)std::lround(a.b + (b.b - a.b) * t)};
				};
				switch (r.ink)
				{
				case VI_DIM: k = theme::DIM; break;
				case VI_BUTTON: k = theme::BUTTON; break;
				case VI_TEXT: k = theme::TEXT; break;
				case VI_BG: k = theme::BG; break;
				case VI_WHITE: k = {255, 255, 255}; break;
				case VI_RAMP: k = mixed(theme::BUTTON, theme::ACCENT, r.mix); break;
				case VI_INK: k = vizInk; break;
				case VI_LINE: k = vizLine; break;
				case VI_INK_RAMP: k = mixed(theme::BUTTON, vizInk, r.mix); break;
				case VI_INK_BG: k = mixed(theme::BG, vizInk, r.mix); break;
				case VI_DIM_BG: k = mixed(theme::BG, theme::DIM, r.mix); break;
				default: k = theme::ACCENT; break;
				}
				c[0] = k.r, c[1] = k.g, c[2] = k.b, c[3] = (unsigned char)std::lround(std::clamp(r.alpha, 0.0f, 1.0f) * 255);
			};
			std::vector<Vert>& v = g.verts;
			v.clear();
			const float sx = w / 640.0f, sy = h / 480.0f, nx = 2.0f / w, ny = 2.0f / h;
			auto vert = [&](float x, float y, const unsigned char* c) { v.push_back({x * nx - 1, 1 - y * ny, {c[0], c[1], c[2], c[3]}}); };
			for (const VizRect& r : shapes)
			{
				unsigned char c[4];
				inkOf(r, c);
				const size_t at = v.size();
				float area = 0;	// (px: only a large opaque shape starts a run with blending off)
				bool opaque = c[3] == 255;
				if (r.kind == 1 || r.kind == 3)
				{
					// a ring round (x0, y0) from radius x1 to y1 (a disc: x1 0), or (kind 3) an ellipse, radii x1 across and
					// y1 down: sides by the size on the panel
					const bool ell = r.kind == 3;
					const float cx = r.x0 * sx, cy = r.y0 * sy;
					const float rx1 = ell ? r.x1 * sx : r.y1 * sy, ry1 = ell ? r.y1 * sy : r.y1 * sy, r0 = ell ? 0 : r.x1 * sy;
					if (rx1 <= 0 || ry1 <= 0) continue;
					const int seg = circleSides(std::max(rx1, ry1));
					const float da = (float)(2 * M_PI / seg), cd = std::cos(da), sd = std::sin(da);
					float pc = 1, ps = 0;
					if (r0 > 0)
					{
						for (int i = 1; i <= seg; i++)
						{
							const float qc = i == seg ? 1 : pc * cd - ps * sd, qs = i == seg ? 0 : ps * cd + pc * sd;
							vert(cx + pc * rx1, cy + ps * rx1, c), vert(cx + qc * rx1, cy + qs * rx1, c), vert(cx + qc * r0, cy + qs * r0, c);
							vert(cx + pc * rx1, cy + ps * rx1, c), vert(cx + qc * r0, cy + qs * r0, c), vert(cx + pc * r0, cy + ps * r0, c);
							pc = qc, ps = qs;
						}
						area = (float)M_PI * (rx1 * rx1 - r0 * r0);
					}
					else
					{
						// a fan from the first point on the rim when it's small (seg - 2 triangles), from the middle when
						// it's large (no long slivers across it)
						const bool rim = seg <= 10;
						const float fx = cx + rx1, fy = cy;
						for (int i = 1; i <= seg; i++)
						{
							const float qc = i == seg ? 1 : pc * cd - ps * sd, qs = i == seg ? 0 : ps * cd + pc * sd;
							if (!rim) vert(cx, cy, c), vert(cx + pc * rx1, cy + ps * ry1, c), vert(cx + qc * rx1, cy + qs * ry1, c);
							else if (i >= 2 && i < seg) vert(fx, fy, c), vert(cx + pc * rx1, cy + ps * ry1, c), vert(cx + qc * rx1, cy + qs * ry1, c);
							pc = qc, ps = qs;
						}
						area = (float)M_PI * rx1 * ry1;
					}
				}
				else if (r.kind == 2)
				{
					// a quad: two triangles, its second pair of corners at alpha2
					unsigned char c2[4] = {c[0], c[1], c[2], (unsigned char)std::lround(std::clamp(r.alpha2, 0.0f, 1.0f) * 255)};
					opaque = opaque && c2[3] == 255;
					const float ax = r.x0 * sx, ay = r.y0 * sy, bx = r.x1 * sx, by = r.y1 * sy, qx = r.x2 * sx, qy = r.y2 * sy, dx = r.x3 * sx,
								dy = r.y3 * sy;
					vert(ax, ay, c), vert(bx, by, c), vert(qx, qy, c2);
					vert(ax, ay, c), vert(qx, qy, c2), vert(dx, dy, c2);
					area = std::fabs((bx - ax) * (qy - ay) - (qx - ax) * (by - ay) + (qx - ax) * (dy - ay) - (dx - ax) * (qy - ay)) / 2;
				}
				else
				{
					// whole pixels, so each bar keeps its width across the row
					const float x0 = std::round(r.x0 * sx), x1 = std::round(r.x1 * sx), y0 = std::round(r.y0 * sy), y1 = std::round(r.y1 * sy);
					if (x1 <= x0 || y1 <= y0) continue;
					vert(x0, y0, c), vert(x1, y0, c), vert(x1, y1, c), vert(x0, y0, c), vert(x1, y1, c), vert(x0, y1, c);
					area = (x1 - x0) * (y1 - y0);
				}
				const int first = (int)at, count = (int)(v.size() - at);
				if (!count) continue;
				cost.fillPx += (long)area;
				if (!opaque) cost.blendPx += (long)area;
				// the runs: a translucent shape needs blending; an opaque one joins whatever run it follows unless it's
				// large (then a run of its own with blending off)
				const bool own = !opaque || area >= 3000 * sx * sy;	// (3000 px on the 640x480 panel)
				// (the ground's shapes are runs of their own: another program draws them)
				const bool ground = groundArt && r.ink == VI_BG;
				if (runs.empty() || runs.back().ground != ground || (own && runs.back().blend == opaque))
					runs.push_back({first, count, !opaque, ground});
				else runs.back().count += count;
			}
			cost.verts += (long)v.size(), cost.vertsMax = std::max(cost.vertsMax, (int)v.size());
		}
		const uint64_t t2 = SDL_GetPerformanceCounter();
		cost.buildMs += (t2 - t1) * f;
		if (g.BindVertexArray) g.BindVertexArray(g.vao);
		if (!runs.empty())
		{
			// where they may go: the panel less mpv's cards (drawn by mpv: a card isn't crossed); in the band, nothing
			// over the art's part (Grid's far ridges, Ribbon's glow reach up) but for the styles that pass behind the
			// art there (Sparks, Starfield, Rocket's stars…: the art, opaque in mpv's layer, covers them)
			std::vector<VizRect> free = {{0, 0, 640, 480, 0}};
			for (const VizRect& keep : shown.nativeKeepClear()) vizCutOut(free, keep);
			if (shown.nativeBand() && !vizBehindArt(nativeState.vis)) vizCutOut(free, {0, 0, 640, 280, 0});
			std::vector<Vert>& v = g.verts;
			g.UseProgram(g.prog);
			g.BindBuffer(GL_ARRAY_BUFFER_, g.vbo);
			// one upload a frame (a new store each time: the driver needn't wait for the last frame's draws)
			g.BufferData(GL_ARRAY_BUFFER_, (long)(v.size() * sizeof(Vert)), v.data(), GL_STREAM_DRAW_);
			g.EnableVertexAttribArray((unsigned)g.aPos);
			g.EnableVertexAttribArray((unsigned)g.aCol);
			g.VertexAttribPointer((unsigned)g.aPos, 2, GL_FLOAT_, 0, sizeof(Vert), nullptr);
			g.VertexAttribPointer((unsigned)g.aCol, 4, GL_UNSIGNED_BYTE_, 1, sizeof(Vert), (const void*)(2 * sizeof(float)));
			g.BlendFunc(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_);
			const bool whole = free.size() == 1 && free[0].x0 == 0 && free[0].y0 == 0 && free[0].x1 == 640 && free[0].y1 == 480;
			if (!whole) g.Enable(GL_SCISSOR_TEST_);
			for (const VizRect& fr : free)
			{
				if (!whole)
				{
					// (GL's rows go up)
					const int x0 = (int)std::round(fr.x0 * w / 640.0f), x1 = (int)std::round(fr.x1 * w / 640.0f), y0 = (int)std::round(fr.y0 * h / 480.0f),
							  y1 = (int)std::round(fr.y1 * h / 480.0f);
					if (x1 <= x0 || y1 <= y0) continue;
					g.Scissor(x0, h - y1, x1 - x0, y1 - y0);
				}
				for (const Run& run : runs)
				{
					if (run.blend) g.Enable(GL_BLEND_);
					else g.Disable(GL_BLEND_);
					if (run.ground) useGroundProgram(g, true);
					g.DrawArrays(GL_TRIANGLES_, run.first, run.count);
					if (run.ground) useGroundProgram(g, false);
					cost.draws++;
				}
			}
			g.Disable(GL_SCISSOR_TEST_);
			g.DisableVertexAttribArray((unsigned)g.aPos);
			g.DisableVertexAttribArray((unsigned)g.aCol);
		}
		if (layered)
		{
			// mpv's layer over it all: premultiplied (mpv's clear ground is 0 0 0 0, its art and lines blended in)
			g.Enable(GL_BLEND_);
			g.BlendFunc(GL_ONE_, GL_ONE_MINUS_SRC_ALPHA_);
			g.UseProgram(g.layerProg);
			g.ActiveTexture(GL_TEXTURE0_);
			g.BindTexture(GL_TEXTURE_2D_, g.layerTex);
			g.Uniform1i(g.lTex, 0);
			g.BindBuffer(GL_ARRAY_BUFFER_, g.layerVbo);
			g.EnableVertexAttribArray((unsigned)g.lPos);
			g.VertexAttribPointer((unsigned)g.lPos, 2, GL_FLOAT_, 0, 2 * sizeof(float), nullptr);
			g.DrawArrays(GL_TRIANGLES_, 0, 6);
			cost.draws++;
			g.DisableVertexAttribArray((unsigned)g.lPos);
			g.BindTexture(GL_TEXTURE_2D_, 0);
		}
		if (gpuTimed)
		{
			g.Finish();
			cost.gpuMs += (SDL_GetPerformanceCounter() - t2) * f;
		}
		// (mpv's next render expects the default state)
		g.Disable(GL_BLEND_);
		g.BlendFunc(GL_SRC_ALPHA_, GL_ONE_MINUS_SRC_ALPHA_);
		g.BindBuffer(GL_ARRAY_BUFFER_, 0);
		if (g.BindVertexArray) g.BindVertexArray(0);
		g.UseProgram(0);
		cost.glMs += (SDL_GetPerformanceCounter() - t2) * f;
	}

	void TopScreen::groundFrame(Player& shown, int w, int h)
	{
		// mpv's ground is clear (a song with a native visualizer, or Match album art's backdrop) but the style
		// isn't up yet or any more: its picture composed on the ground, not on black
		nativeFrame(shown, w, h, true, false);
	}

	bool TopScreen::drawBackdropPages(int w, int h, bool over)
	{
		// the backdrop's pages into the panel (the context is current): what it fades from, then what plays at
		// the cross-fade's alpha (bgFadeStep, set each pass from Ui's clock). The pages are 160 x 480, the panel's
		// own shape: the stretch is the blur's own smoothing, linear filtered. (PageRenderer::draw's answer is
		// "pages still uploading", not "drawn": taken as the latter, every mode cleared the backdrop away again.
		// Its pages here go up whole in the frame that shows them, setUploadAll). over: blended onto what the panel
		// holds (a leaving song's page over mpv's idle picture), not onto a cleared ground
		std::vector<PageRenderer::Placed> placed;
		if (bgFading) placed.push_back({0, 0, 0, (float)w, (float)h, false, false, 1, true});
		placed.push_back({1, 0, 0, (float)w, (float)h, false, bgFadeStep < 1, bgFadeStep, true});
		bgRender.setKeepScreen(over);
		bgRender.draw(placed, w, h);
		bgRender.setKeepScreen(false);
		return true;
	}
}
