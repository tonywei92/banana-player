// The Strip prototype (docs/design/manga.md section 10: "prototype first, on the handheld"): `--manga FOLDER` opens a
// folder of pages as one continuous strip on the top panel, driven by the D-pad and the bottom touchscreen, with its
// decode times and frame rate in the log. It answers one question before the reader is built on it: is the strip
// smooth on the RK3568 (drag, fling, decode keeping up)? Its own loop; the player isn't involved.
#include "reader_proto.h"
#include "cbz.h"
#include "input.h"
#include "manga_names.h"
#include "pagedecode.h"
#include "pagerender.h"
#include "png.h"
#include "strip.h"
#include "touch.h"
#include <algorithm>
#include <cstring>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <dirent.h>
#include <mutex>
#include <set>
#include <thread>

namespace bp
{
	namespace
	{
		// two threads decode pages, the nearest first; the main thread only uploads and draws
		struct Decoder
		{
			struct Job { int index; std::string path; int width; };
			struct Done { int index; PageImage img; Uint32 ms; };
			std::mutex m;
			std::condition_variable cv;
			std::deque<Job> jobs;
			std::deque<Done> done;
			std::set<int> busy;	// taken by a thread, not handed back yet
			std::vector<std::thread> threads;
			bool quit = false;
			// a CBZ: one open archive for both threads, a read at a time (its file position is shared)
			Cbz* cbz = nullptr;
			std::mutex cbzLock;
			Uint32 wake = 0;

			void start(Uint32 wakeEvent)
			{
				wake = wakeEvent;
				for (int k = 0; k < 2; k++) threads.emplace_back([this] { run(); });
			}
			void run()
			{
				for (;;)
				{
					Job j;
					{
						std::unique_lock<std::mutex> l(m);
						cv.wait(l, [&] { return quit || !jobs.empty(); });
						if (quit) return;
						j = jobs.front();
						jobs.pop_front();
						busy.insert(j.index);
					}
					Uint32 t0 = SDL_GetTicks();
					PageImage img;
					if (cbz)
					{
						std::vector<uint8_t> bytes;
						bool got;
						{
							std::lock_guard<std::mutex> l(cbzLock);
							got = cbz->read(j.path, bytes);
							if (!got) img.error = cbz->error();
						}
						if (got) img = decodePage(bytes.data(), bytes.size(), j.width);
					}
					else img = decodePageFile(j.path, j.width);
					Uint32 took = SDL_GetTicks() - t0;
					{
						std::lock_guard<std::mutex> l(m);
						busy.erase(j.index);
						done.push_back({j.index, std::move(img), took});
					}
					// (never touches the reader: the main loop wakes and takes it)
					SDL_Event e{};
					e.type = wake;
					SDL_PushEvent(&e);
				}
			}
			// the queue as the strip wants it now: what's no longer wanted waits no more; what a thread has, or has
			// finished and not handed over, isn't asked twice
			void want(const std::vector<Job>& list)
			{
				std::lock_guard<std::mutex> l(m);
				jobs.clear();
				for (auto& j : list)
				{
					bool ready = std::any_of(done.begin(), done.end(), [&](const Done& d) { return d.index == j.index; });
					if (!busy.count(j.index) && !ready) jobs.push_back(j);
				}
				cv.notify_all();
			}
			void stop()
			{
				{
					std::lock_guard<std::mutex> l(m);
					quit = true;
					jobs.clear();
				}
				cv.notify_all();
				for (auto& t : threads) t.join();
			}
		};

		std::vector<std::string> listPages(const std::string& dir)
		{
			std::vector<std::string> names;
			if (DIR* d = opendir(dir.c_str()))
			{
				while (dirent* e = readdir(d))
					if (isPageImage(e->d_name)) names.push_back(e->d_name);
				closedir(d);
			}
			sortPages(names);
			for (auto& n : names) n = dir + "/" + n;
			return names;
		}
	}

	int runStripProto(SDL_Window* top, SDL_GLContext gl, bool gles, const std::string& dir, Uint32 touchEvent)
	{
		// a folder of pages, or a CBZ (its entries' names stand for the files)
		Cbz cbz;
		bool isCbz = dir.size() > 4 && (dir.compare(dir.size() - 4, 4, ".cbz") == 0 || dir.compare(dir.size() - 4, 4, ".zip") == 0);
		if (isCbz && !cbz.open(dir))
		{
			fprintf(stderr, "reader: %s: %s\n", dir.c_str(), cbz.error().c_str());
			return 1;
		}
		std::vector<std::string> files = isCbz ? cbz.pages() : listPages(dir);
		fprintf(stderr, "reader: %zu pages in %s\n", files.size(), dir.c_str());
		if (files.empty()) return 1;
		SDL_GL_MakeCurrent(top, gl);
		PageRenderer pr;
		if (!pr.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, gles)) return 1;
		int w = 0, h = 0;
		SDL_GL_GetDrawableSize(top, &w, &h);
		Strip strip;
		strip.setView(w, h);
		strip.setPages((int)files.size(), 1.5);
		Uint32 wake = SDL_RegisterEvents(1);
		Decoder dec;
		if (isCbz) dec.cbz = &cbz;
		dec.start(wake);

		// what the strip wants decoded and kept: the view's pages, 3 ahead, 1 behind (the design's Preload)
		auto refill = [&] {
			auto list = strip.wanted(3, 1);
			std::vector<Decoder::Job> jobs;
			for (int i : list)
				if (!pr.has(i)) jobs.push_back({i, files[i], w});
			// textures for a little more than what's wanted, so a page just behind doesn't decode again at once
			std::set<int> keep(list.begin(), list.end());
			auto vis = strip.visible();
			for (int i = vis.first - 2; i <= vis.second + 4; i++) keep.insert(i);
			pr.keepOnly(keep);
			dec.want(jobs);
		};
		refill();

		bool fingerDown = false;
		float lastY = 0;
		int draws = 0, frames = 0;
		Uint32 fpsFrom = SDL_GetTicks(), worst = 0, lastFrame = 0;
		bool dirty = true, running = true;
		// BP_MANGA_DEMO=1: a fixed run with no hands (a computer, or the handheld over SSH): D-pad steps, a fast flick,
		// R and L, then out; BP_SHOTS=DIR saves the top panel as DIR/manga-N.png at a few moments
		const bool demo = getenv("BP_MANGA_DEMO") != nullptr;
		const char* shots = getenv("BP_SHOTS");
		Uint32 demoAt = SDL_GetTicks();
		int demoStep = 0, shotN = 0;
		bool shotWanted = false;
		auto snapshot = [&] {
			std::vector<unsigned char> px((size_t)w * h * 4), flip((size_t)w * h * 4);
			using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void*);
			auto readPixels = (ReadPixels)SDL_GL_GetProcAddress("glReadPixels");
			if (!readPixels) return;
			readPixels(0, 0, w, h, 0x1908, 0x1401, px.data());
			for (int y = 0; y < h; y++) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
			std::string path = std::string(shots) + "/manga-" + std::to_string(++shotN) + ".png";
			savePng(path, w, h, flip.data());
			fprintf(stderr, "reader: saved %s (page %d)\n", path.c_str(), strip.currentPage() + 1);
		};
		auto finger = [&](Pointer::Kind k, float y01) {
			Uint32 now = SDL_GetTicks();
			// the panels are the same size: the finger's travel on the bottom is the strip's on the top, 1:1
			float y = y01 * h;
			if (k == Pointer::Down) { strip.touchDown(now); fingerDown = true; }
			else if (k == Pointer::Move && fingerDown) strip.drag(y - lastY, now);
			else if (k == Pointer::Up && fingerDown) { strip.release(now); fingerDown = false; }
			lastY = y;
			dirty = true;
		};
		while (running)
		{
			SDL_Event e;
			bool moving = strip.moving() || dirty;
			bool got = moving ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, 250);
			while (got)
			{
				if (e.type == SDL_QUIT) running = false;
				else if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) handlePadHotplug(e);
				else if (e.type == touchEvent)
				{
					Pointer* pt = (Pointer*)e.user.data1;
					finger(pt->kind, pt->y);
					delete pt;
				}
				else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK)))
				{
					// a computer: the mouse on the bottom window stands in for the finger
					SDL_Window* win = SDL_GetWindowFromID(e.type == SDL_MOUSEMOTION ? e.motion.windowID : e.button.windowID);
					if (win && win != top)
					{
						int ww, wh;
						SDL_GetWindowSize(win, &ww, &wh);
						int my = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
						finger(e.type == SDL_MOUSEBUTTONDOWN ? Pointer::Down : e.type == SDL_MOUSEBUTTONUP ? Pointer::Up : Pointer::Move,
							my / (float)wh);
					}
				}
				else if (e.type == wake) dirty = true;
				else if (Button b = buttonFromEvent(e); b != Button::None)
				{
					Uint32 now = SDL_GetTicks();
					double off = strip.offset();
					int cur = strip.currentPage();
					switch (b)
					{
					case Button::Down: case Button::Right: strip.animateTo(off + strip.screenful(), now); break;
					case Button::Up: case Button::Left: strip.animateTo(off - strip.screenful(), now); break;
					case Button::R: strip.animateTo(strip.pageTop(std::min(cur + 1, (int)files.size() - 1)), now, 220); break;
					case Button::L:
						// the top of this page, or the one before when already at its top
						strip.animateTo(off - strip.pageTop(cur) > 4 ? strip.pageTop(cur) : strip.pageTop(std::max(cur - 1, 0)), now, 220);
						break;
					case Button::B: case Button::Start: running = false; break;
					default: break;
					}
					dirty = true;
				}
				got = SDL_PollEvent(&e);
			}
			// decoded pages: to the renderer, their real sizes to the strip (which keeps what's on screen still)
			{
				std::lock_guard<std::mutex> l(dec.m);
				while (!dec.done.empty())
				{
					auto d = std::move(dec.done.front());
					dec.done.pop_front();
					if (!d.img.ok())
					{
						fprintf(stderr, "reader: page %d: %s\n", d.index + 1, d.img.error.c_str());
						continue;
					}
					fprintf(stderr, "timing: page %d decoded in %u ms (%dx%d from %dx%d)\n", d.index + 1, d.ms, d.img.w, d.img.h,
						d.img.srcW, d.img.srcH);
					strip.setPageSize(d.index, d.img.w, d.img.h);
					pr.addPage(d.index, d.img.w, d.img.h, std::move(d.img.rgba));
					dirty = true;
				}
			}
			Uint32 now = SDL_GetTicks();
			if (demo && now - demoAt >= 0)
			{
				// one step every 400 ms (the flick's samples 10 ms apart, as a finger's)
				Uint32 t = now - demoAt;
				if (demoStep < 8 && t >= 1500u + demoStep * 400u) { strip.animateTo(strip.offset() + strip.screenful(), now); demoStep++; dirty = true; }
				else if (demoStep == 8 && t >= 5200)
				{
					strip.touchDown(now);
					for (int k = 1; k <= 8; k++) strip.drag(-45, now + k * 10);
					strip.release(now + 80);
					demoStep++;
					shotWanted = true;
				}
				else if (demoStep == 9 && t >= 9000) { strip.animateTo(strip.pageTop(strip.currentPage() + 1), now, 220); demoStep++; }
				else if (demoStep == 10 && t >= 9800) { strip.animateTo(strip.pageTop(std::max(strip.currentPage() - 1, 0)), now, 220); demoStep++; }
				else if (demoStep == 11 && t >= 10800) { shotWanted = true; demoStep++; }
				else if (demoStep == 12 && t >= 11500) running = false;
				if (demoStep == 1 && t >= 1400 && !shotN) shotWanted = true;
			}
			bool still = strip.step(now);
			if (dirty || still)
			{
				refill();
				SDL_GL_MakeCurrent(top, gl);
				bool uploads = pr.draw(strip, w, h);
				if (shotWanted && shots) { snapshot(); shotWanted = false; }
				SDL_GL_SwapWindow(top);
				// KMSDRM drops the first frames: a few more at the start
				dirty = uploads || ++draws < 5;
				if (lastFrame && still) worst = std::max(worst, now - lastFrame);
				lastFrame = now;
				frames++;
			}
			else lastFrame = 0;
			// the frame rate while it moves, once a second, with the slowest frame (what a stutter would show)
			if (now - fpsFrom >= 1000)
			{
				if (frames > 1 && worst)
					fprintf(stderr, "timing: %d frames in %u ms, slowest %u ms, page %d/%zu, %zu MB of textures\n", frames,
						now - fpsFrom, worst, strip.currentPage() + 1, files.size(), pr.textureBytes() >> 20);
				fpsFrom = now;
				frames = 0;
				worst = 0;
			}
		}
		dec.stop();
		pr.shutdown();
		return 0;
	}
}
