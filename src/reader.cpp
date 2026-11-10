#include "reader.h"
#include "manga_names.h"
#include <algorithm>
#include <cstdio>
#include <dirent.h>

namespace bp
{
	MangaReader::~MangaReader()
	{
		{
			std::lock_guard<std::mutex> l(m);
			quit = true;
			jobs.clear();
		}
		cv.notify_all();
		for (auto& t : threads) t.join();
		pr.shutdown();
	}

	bool MangaReader::init(bool gles, Uint32 wakeEvent)
	{
		if (ready) return true;
		if (!pr.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, gles)) return false;
		wake = wakeEvent;
		for (int k = 0; k < 2; k++) threads.emplace_back([this] { decodeLoop(); });
		ready = true;
		return true;
	}

	void MangaReader::decodeLoop()
	{
		for (;;)
		{
			Job j;
			uint64_t g;
			{
				std::unique_lock<std::mutex> l(m);
				cv.wait(l, [&] { return quit || !jobs.empty(); });
				if (quit) return;
				j = jobs.front();
				jobs.pop_front();
				busySet.insert(j.index);
				g = gen;
			}
			Uint32 t0 = SDL_GetTicks();
			PageImage img;
			if (archive)
			{
				std::vector<uint8_t> bytes;
				bool got;
				{
					// one open archive for both threads: a read at a time (its position is shared)
					std::lock_guard<std::mutex> l(cbzLock);
					got = cbz.read(j.path, bytes);
					if (!got) img.error = cbz.error();
				}
				if (got) img = decodePage(bytes.data(), bytes.size(), j.width);
			}
			else img = decodePageFile(j.path, j.width);
			Uint32 took = SDL_GetTicks() - t0;
			{
				std::lock_guard<std::mutex> l(m);
				busySet.erase(j.index);
				done.push_back({j.index, std::move(img), took, g});
			}
			SDL_Event e{};
			e.type = wake;
			SDL_PushEvent(&e);
		}
	}

	bool MangaReader::open(const std::string& path, bool isArchive, int page, int w, int h, int m, int f)
	{
		close();
		err.clear();
		viewW = w;
		viewH = h;
		archive = isArchive;
		std::vector<std::string> names;
		if (archive)
		{
			std::lock_guard<std::mutex> l(cbzLock);
			if (!cbz.open(path)) { err = cbz.error(); return false; }
			names = cbz.pages();
		}
		else
		{
			if (DIR* d = opendir(path.c_str()))
			{
				while (dirent* e = readdir(d))
					if (isPageImage(e->d_name)) names.push_back(e->d_name);
				closedir(d);
			}
			sortPages(names);
			for (auto& n : names) n = path + "/" + n;
		}
		if (names.empty()) { err = "No pages in this chapter"; return false; }
		files = std::move(names);
		strip.setView(w, h);
		strip.setPages((int)files.size(), 1.5);
		page = std::clamp(page, 0, (int)files.size() - 1);
		// (placeholders are all the same height until pages come: their tops are exact for the first page wanted)
		strip.scrollTo(strip.pageTop(page));
		paged.setView(w, h);
		paged.setPages((int)files.size(), 1.5);
		fit = f >= 0 && f < MANGA_FITS ? f : MANGA_FIT_PAGE;
		applyFit();
		readMode = m >= 0 && m < MANGA_MODES ? m : MANGA_STRIP;
		paged.setRtl(readMode == MANGA_RTL);
		paged.goTo(page, 0, false);
		opened = true;
		redrawSoon();
		fprintf(stderr, "reader: opened %s, %zu pages, at page %d\n", path.substr(path.rfind('/') + 1).c_str(), files.size(), page + 1);
		want();
		return true;
	}

	void MangaReader::close()
	{
		{
			std::lock_guard<std::mutex> l(m);
			jobs.clear();
			done.clear();
			gen++;	// what a thread is decoding now is dropped when it comes back
		}
		pr.keepOnly({});
		if (archive)
		{
			std::lock_guard<std::mutex> l(cbzLock);
			cbz.close();
		}
		files.clear();
		opened = fingerDown = false;
	}

	void MangaReader::want()
	{
		if (!opened) return;
		bool s = readMode == MANGA_STRIP;
		auto list = s ? strip.wanted(preload, 1) : paged.wanted(preload, 1);
		std::set<int> keep(list.begin(), list.end());
		auto vis = strip.visible();
		if (!s) vis = {paged.current(), paged.current()};
		for (int i = vis.first - 2; i <= vis.second + 4; i++) keep.insert(i);
		pr.keepOnly(keep);
		std::lock_guard<std::mutex> l(m);
		jobs.clear();
		for (int i : list)
		{
			if (pr.has(i) || busySet.count(i)) continue;
			if (std::any_of(done.begin(), done.end(), [&](const Done& d) { return d.index == i; })) continue;
			jobs.push_back({i, files[i], viewW});
		}
		cv.notify_all();
	}

	void MangaReader::setPreload(int n)
	{
		// the row offers 2, 3 and 6; anything else is left alone
		if (n < 1 || n == preload) return;
		preload = n;
		want();
	}

	void MangaReader::applyFit()
	{
		// Fill covers the screen and crops; a page both taller and wider than the view would need a sideways pan the
		// paged row hasn't (it lays pages out side by side and scrolls them down), so it reads as Width there. The
		// Strip is always as wide as the screen.
		paged.setFit(fit == MANGA_FIT_PAGE ? Paged::FitPage : Paged::FitWidth);
	}

	void MangaReader::command(int kind, double v, int64_t ms, double v2)
	{
		if (!opened) return;
		if (kind == Fit)
		{
			// the page under the view kept, as a mode change keeps it
			int f = (int)v;
			if (f < 0 || f >= MANGA_FITS || f == fit) return;
			fit = f;
			applyFit();
			want();
			redrawSoon(2);
			return;
		}
		if (kind == Mode)
		{
			// the same page in the other mode, at rest
			int cur = page(), m = (int)v;
			if (m < 0 || m >= MANGA_MODES || m == readMode) return;
			readMode = m;
			strip.scrollTo(strip.pageTop(cur));
			paged.setRtl(m == MANGA_RTL);
			paged.goTo(cur, ms, false);
			want();
			redrawSoon(2);
			return;
		}
		if (readMode != MANGA_STRIP)
		{
			switch (kind)
			{
			case Step: if (v > 0) paged.forward(ms); else paged.back(ms); break;
			case Page: if (v > 0) paged.nextPage(ms); else paged.prevPage(ms); break;
			case Seek: paged.goTo(std::clamp((int)v, 0, (int)files.size() - 1), ms, false); break;
			case TouchDown: paged.touchDown(ms); fingerDown = true; break;
			case Drag: if (fingerDown) paged.drag(v2 * viewW, v * viewH, ms); break;
			case Release: if (fingerDown) paged.release(ms); fingerDown = false; break;
			default: break;
			}
			redrawSoon(1);
			return;
		}
		switch (kind)
		{
		case Step: strip.animateTo(strip.offset() + v * strip.screenful(), ms); break;
		case Page:
		{
			int cur = strip.currentPage();
			// back: this page's top first when it's above, as a book's previous page reads
			int to = v > 0 ? cur + 1 : strip.offset() - strip.pageTop(cur) > 4 ? cur : cur - 1;
			to = std::clamp(to, 0, (int)files.size() - 1);
			strip.animateTo(strip.pageTop(to), ms, 220);
			break;
		}
		case Seek: strip.scrollTo(strip.pageTop(std::clamp((int)v, 0, (int)files.size() - 1))); break;
		case TouchDown: strip.touchDown(ms); fingerDown = true; break;
		case Drag: if (fingerDown) strip.drag(v * viewH, ms); break;
		case Release: if (fingerDown) strip.release(ms); fingerDown = false; break;
		default: break;
		}
		redrawSoon(1);
	}

	bool MangaReader::frame(int64_t now)
	{
		if (!opened) return false;
		bool any = false;
		{
			std::lock_guard<std::mutex> l(m);
			while (!done.empty())
			{
				Done d = std::move(done.front());
				done.pop_front();
				if (d.gen != gen) continue;
				if (!d.img.ok())
				{
					fprintf(stderr, "reader: page %d: %s\n", d.index + 1, d.img.error.c_str());
					continue;
				}
				fprintf(stderr, "timing: page %d decoded in %u ms (%dx%d)\n", d.index + 1, d.ms, d.img.w, d.img.h);
				strip.setPageSize(d.index, d.img.w, d.img.h);
				paged.setPageSize(d.index, d.img.w, d.img.h);
				pr.addPage(d.index, d.img.w, d.img.h, std::move(d.img.rgba));
				any = true;
			}
		}
		bool moving = readMode == MANGA_STRIP ? strip.step(now) : paged.step(now);
		if (any || moving) want();
		bool need = any || moving || uploads || forceFrames > 0;
		return need;
	}

	void MangaReader::draw(int w, int h)
	{
		if (!opened) return;
		if (readMode == MANGA_STRIP) uploads = pr.draw(strip, w, h);
		else
		{
			std::vector<PageRenderer::Placed> placed;
			// at rest only the page read: its neighbours would peek in at the 4:3 panel's sides (they slide in on a swipe)
			bool rest = !paged.moving() && !fingerDown;
			for (auto& p : paged.placements())
				if (!rest || p.page == paged.current()) placed.push_back({p.page, (float)p.x, (float)p.y, (float)p.w, (float)p.h});
			uploads = pr.draw(placed, w, h);
		}
		if (forceFrames > 0) forceFrames--;
	}

	bool MangaReader::loading() const
	{
		if (!opened) return false;
		if (readMode != MANGA_STRIP) return !pr.has(paged.current());
		auto vis = strip.visible();
		for (int i = vis.first; i >= 0 && i <= vis.second; i++)
			if (!pr.has(i)) return true;
		return false;
	}

	bool MangaReader::atEnd() const
	{
		if (!opened) return false;
		if (readMode != MANGA_STRIP) return paged.atEnd();
		return strip.offset() >= strip.totalHeight() - strip.screenful() / 0.9 - 1;
	}
}
