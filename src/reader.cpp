#include "reader.h"
#include "manga_names.h"
#include "readerfetchplan.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		// a zoom's wider decode waits for the presses to rest: a run 100 → 800 % decodes the pages once, at the end
		constexpr int64_t ZOOM_SETTLE_MS = 300;

		bool fileThere(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
		}
	}

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
				done.push_back({j.index, std::move(img), took, g, j.width});
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
		errPage = -1;
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
		return begin(path.substr(path.rfind('/') + 1), page, w, h, m, f);
	}

	bool MangaReader::open(const std::vector<std::string>& pageUrls, const Headers& pageHeaders, PageCache& cache,
		const std::string& chapterId, int page, int w, int h, int m, int f)
	{
		close();
		err.clear();
		errPage = -1;
		archive = false;	// (close kept the last chapter's: a URL chapter has no archive)
		if (pageUrls.empty()) { err = "No pages in this chapter"; return false; }
		urls = pageUrls;
		headers = pageHeaders;
		pageCache = &cache;
		chapterKey = chapterId;
		files.reserve(urls.size());
		for (const auto& u : urls) files.push_back(pageCache->pathFor(u));
		return begin(chapterKey.empty() ? "chapter" : chapterKey, page, w, h, m, f);
	}

	bool MangaReader::begin(const std::string& label, int page, int w, int h, int m, int f)
	{
		viewW = w;
		viewH = h;
		zscale = 1;	// a chapter opens at the fit (the photo viewer keeps its zoom; a page of a new chapter is a new view)
		zoomAt = 0;
		strip.setView(w, layoutH());
		strip.setPages((int)files.size(), 1.5);
		strip.setZoom(1, 0, 0);	// the models outlive a chapter: the last one's zoom goes
		page = std::clamp(page, 0, (int)files.size() - 1);
		// (placeholders are all the same height until pages come: their tops are exact for the first page wanted)
		strip.scrollTo(strip.pageTop(page));
		paged.setView(w, layoutH());
		paged.setPages((int)files.size(), 1.5);
		paged.setZoom(1, 0, 0);
		fit = f >= 0 && f < MANGA_FITS ? f : MANGA_FIT_PAGE;
		applyFit();
		readMode = m >= 0 && m < MANGA_MODES ? m : MANGA_STRIP;
		paged.setRtl(readMode == MANGA_RTL);
		paged.goTo(page, 0, false);
		opened = true;
		redrawSoon();
		fprintf(stderr, "reader: opened %s, %zu pages, at page %d\n", label.c_str(), files.size(), page + 1);
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
		kept.clear();
		thumbs.clear();
		decodedW.clear();
		zscale = 1;
		zoomAt = 0;
		if (archive)
		{
			std::lock_guard<std::mutex> l(cbzLock);
			cbz.close();
		}
		files.clear();
		urls.clear();
		headers.clear();
		pageCache = nullptr;
		chapterKey.clear();
		fetching.clear();
		failed.clear();
		errPage = -1;
		opened = fingerDown = false;
	}

	int MangaReader::decodeW() const
	{
		// a zoomed page is drawn wider than the view, so it is decoded at that width (a 2048 px cap keeps the
		// textures within the panels' limit; decodePage adds its own 2x of the image's own width)
		if (!(zscale > 1.0001)) return viewW;
		double w = zscale * viewW;
		if (w > 2048) w = 2048;
		return std::max(viewW, (int)(w + 0.5));
	}

	std::vector<int> MangaReader::wantedPages() const
	{
		// the one list want() queues from and pagesToFetch() hands out: the reader's own order (visible pages first,
		// then the preload ahead and behind)
		if (!opened) return {};
		return readMode == MANGA_STRIP ? strip.wanted(preload, 1) : paged.wanted(preload, 1);
	}

	void MangaReader::want()
	{
		if (!opened) return;
		bool s = readMode == MANGA_STRIP;
		auto list = wantedPages();
		std::set<int> keep(list.begin(), list.end());
		auto vis = strip.visible();
		if (!s) vis = {paged.current(), paged.current()};
		for (int i = vis.first - 2; i <= vis.second + 4; i++) keep.insert(i);
		pr.keepOnly(keep);
		for (auto it = kept.begin(); it != kept.end();) it = keep.count(it->first) ? std::next(it) : kept.erase(it);
		// a failure sticks only while the page is wanted: moving away and back asks for it again
		for (auto it = failed.begin(); it != failed.end();)
		{
			if (std::find(list.begin(), list.end(), *it) != list.end()) { ++it; continue; }
			if (*it == errPage) { err.clear(); errPage = -1; }	// (the page the error was about has left the view)
			it = failed.erase(it);
		}
		int width = decodeW();
		std::lock_guard<std::mutex> l(m);
		jobs.clear();
		for (int i : list)
		{
			// a page of a URL chapter that isn't fetched yet waits: its decode is queued when pageArrived says so
			if (i < 0 || i >= (int)files.size()) continue;
			if (remote() && !fileThere(files[i])) continue;
			// a page the last zoom decoded narrower is asked for again, and goes on showing scaled until it comes
			auto it = decodedW.find(i);
			if (pr.has(i) && it != decodedW.end() && it->second >= width) continue;
			if (busySet.count(i)) continue;
			if (std::any_of(done.begin(), done.end(), [&](const Done& d) { return d.index == i; })) continue;
			jobs.push_back({i, files[i], width});
		}
		cv.notify_all();
	}

	std::vector<std::pair<int, std::string>> MangaReader::pagesToFetch(int max)
	{
		std::vector<std::pair<int, std::string>> out;
		if (!opened || urls.empty() || max <= 0) return out;
		std::vector<int> list = wantedPages();
		// a page that failed while wanted is not asked for again until it has left the list (want() drops it there)
		for (auto it = failed.begin(); it != failed.end();)
		{
			if (std::find(list.begin(), list.end(), *it) != list.end()) { ++it; continue; }
			if (*it == errPage) { err.clear(); errPage = -1; }
			it = failed.erase(it);
		}
		ReaderFetchPlan::State st;
		st.wanted = std::move(list);
		st.inFlight = fetching;
		st.failed = failed;
		// max is how many may be started now: the plan's cap counts what is already out, so the two add up
		st.max = (int)fetching.size() + max;
		for (int i : st.wanted)
			if (i >= 0 && i < (int)files.size() && fileThere(files[i])) st.done.insert(i);
		for (int i : ReaderFetchPlan::next(st)) out.push_back({i, urls[i]});
		// these are out now: pagesToFetch won't hand the same one out twice while its request runs
		for (auto& p : out) fetching.insert(p.first);
		return out;
	}

	void MangaReader::pageArrived(int index)
	{
		if (!opened || index < 0 || index >= (int)files.size()) return;
		fetching.erase(index);
		failed.erase(index);
		if (index == errPage) { err.clear(); errPage = -1; }
		want();	// its decode goes in now: the file is there
		redrawSoon(1);
	}

	void MangaReader::pageFailed(int index, const std::string& error)
	{
		if (!opened) return;
		fetching.erase(index);
		if (index >= 0) failed.insert(index);
		err = "Page " + std::to_string(index + 1) + ": " + (error.empty() ? "couldn't be fetched" : error);
		errPage = index;
		fprintf(stderr, "reader: %s\n", err.c_str());
		want();
		redrawSoon(1);
	}

	void MangaReader::pageDropped(int index)
	{
		// the reader moved away before the request finished: not a failure, so no error and no failed mark
		fetching.erase(index);
	}

	void MangaReader::setPreload(int n)
	{
		// the row offers 2, 3 and 6; anything else is left alone
		if (n < 1 || n == preload) return;
		preload = n;
		want();
	}

	std::vector<MangaReader::Box> MangaReader::boxes() const
	{
		std::vector<Box> out;
		if (readMode == MANGA_STRIP)
		{
			auto vis = strip.visible();
			double off = strip.offset();
			for (int i = vis.first; i >= 0 && i <= vis.second; i++)
				out.push_back({i, (float)-strip.pan(), (float)(strip.pageTop(i) - off), (float)strip.pageWidth(),
					(float)strip.pageHeight(i)});
			return out;
		}
		// at rest only the page read: its neighbours would peek in at the 4:3 panel's sides (they slide in on a swipe)
		bool rest = !paged.moving() && !fingerDown;
		for (auto& p : paged.placements())
			if (!rest || p.page == paged.current()) out.push_back({p.page, (float)p.x, (float)p.y, (float)p.w, (float)p.h});
		return out;
	}

	std::vector<MangaReader::Box> MangaReader::bottomBoxes() const
	{
		// the view below the top panel and the hinge, in the bottom panel's own pixels
		std::vector<Box> out;
		if (!opened || !full) return out;
		float shift = (float)(viewH + gapPx);
		for (auto b : boxes())
		{
			b.y -= shift;
			if (b.y + b.h > 0 && b.y < viewH) out.push_back(b);
		}
		return out;
	}

	std::shared_ptr<const std::vector<uint8_t>> MangaReader::pixels(int page, int& w, int& h) const
	{
		auto it = kept.find(page);
		if (it == kept.end()) return nullptr;
		w = it->second.w;
		h = it->second.h;
		return it->second.rgba;
	}

	std::shared_ptr<const std::vector<uint8_t>> MangaReader::thumbPixels(int page, int& w, int& h) const
	{
		auto it = thumbs.find(page);
		if (it == thumbs.end()) return nullptr;
		w = it->second.w;
		h = it->second.h;
		return it->second.rgba;
	}

	void MangaReader::setFullscreen(bool on, int hinge480)
	{
		int gap = viewH > 0 ? (int)std::lround(std::clamp(hinge480, 0, 120) * viewH / 480.0) : 0;
		if (on == full && gap == gapPx) return;
		// the page read stays: the strip keeps its place (only its end moves), the paged row opens it again
		int cur = page();
		full = on;
		gapPx = gap;
		strip.setView(viewW, layoutH());
		paged.setView(viewW, layoutH());
		if (readMode != MANGA_STRIP) paged.goTo(cur, 0, false);
		fprintf(stderr, "reader: fullscreen %s (hinge %d px)\n", on ? "on" : "off", gapPx);
		want();
		redrawSoon(3);
	}

	void MangaReader::applyFit()
	{
		// (the Strip is always as wide as the screen; the paged modes fit as set, Fill panning a cropped page)
		paged.setFit(fit == MANGA_FIT_PAGE ? Paged::FitPage : fit == MANGA_FIT_FILL ? Paged::FitFill : Paged::FitWidth);
	}

	void MangaReader::applyZoom()
	{
		// the focus is the view's middle: the whole tall view in fullscreen, as the fit is there too
		if (readMode == MANGA_STRIP) strip.setZoom(zscale, viewW * 0.5, layoutH() * 0.5);
		else paged.setZoom(zscale, viewW * 0.5, layoutH() * 0.5);
	}

	void MangaReader::command(int kind, double v, int64_t ms, double v2)
	{
		if (!opened) return;
		if (kind == Fullscreen) { setFullscreen(v > 0, (int)v2); return; }
		if (kind == Zoom)
		{
			// the drawn view zooms at once; a wider page is decoded only once the presses rest (frame's 300 ms),
			// so a run through the steps decodes once
			double z = std::clamp(v, 1.0, 8.0);
			if (z == zscale) return;
			zscale = z;
			applyZoom();
			zoomAt = ms;
			fprintf(stderr, "reader: zoom %d%%\n", zoomPct());
			redrawSoon(2);
			return;
		}
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
			applyZoom();	// the model taking over reads at the zoom already set
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
		case Drag: if (fingerDown) strip.drag(v * viewH, ms, v2 * viewW); break;
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
				auto was = decodedW.find(d.index);
				// (a wider decode of a page on screen goes up beside the old one, which stays drawn till it's all there)
				(void)was;
				decodedW[d.index] = std::max(d.width, d.img.w);
				strip.setPageSize(d.index, d.img.w, d.img.h);
				paged.setPageSize(d.index, d.img.w, d.img.h);
				// (a copy for the bottom panel: the renderer drops its own once the page is on the GPU)
				kept[d.index] = {d.img.w, d.img.h, std::make_shared<const std::vector<uint8_t>>(d.img.rgba)};
				// a thumbnail for the pages grid: 96 px wide, aspect kept, nearest-neighbour (small enough that
				// the blockiness doesn't show on the 4:3 panel; a 1400×2000 source is 13 K iterations)
				{
					int tw = 96;
					int th = std::max(1, (int)((double)d.img.h * tw / d.img.w + 0.5));
					auto buf = std::make_shared<std::vector<uint8_t>>(tw * th * 4);
					const uint8_t* src = d.img.rgba.data();
					uint8_t* dst = buf->data();
					for (int ty = 0; ty < th; ty++)
						for (int tx = 0; tx < tw; tx++)
						{
							int sx = tx * d.img.w / tw;
							int sy = ty * d.img.h / th;
							const uint8_t* s = src + ((size_t)sy * d.img.w + sx) * 4;
							uint8_t* t = dst + ((size_t)ty * tw + tx) * 4;
							t[0] = s[0]; t[1] = s[1]; t[2] = s[2]; t[3] = s[3];
						}
					thumbs[d.index] = {tw, th, std::move(buf)};
				}
				pr.addPage(d.index, d.img.w, d.img.h, std::move(d.img.rgba));
				any = true;
			}
		}
		bool moving = readMode == MANGA_STRIP ? strip.step(now) : paged.step(now);
		// the zoom rests: the pages in view are wanted again at its width (want reads the width itself)
		if (zoomAt && now - zoomAt >= ZOOM_SETTLE_MS)
		{
			zoomAt = 0;
			want();
		}
		if (any || moving) want();
		bool need = any || moving || uploads || forceFrames > 0;
		return need;
	}

	void MangaReader::draw(int w, int h)
	{
		if (!opened) return;
		std::vector<PageRenderer::Placed> placed;
		for (auto& b : boxes()) placed.push_back({b.page, b.x, b.y, b.w, b.h});
		uploads = pr.draw(placed, w, h);
		if (forceFrames > 0) forceFrames--;
	}

	bool MangaReader::loading() const
	{
		if (!opened) return false;
		// a page that failed isn't waiting: it shows its grey placeholder and the error (error()); a page still to
		// be fetched or decoded is
		if (readMode != MANGA_STRIP)
			return !pr.has(paged.current()) && !failed.count(paged.current());
		auto vis = strip.visible();
		for (int i = vis.first; i >= 0 && i <= vis.second; i++)
			if (!pr.has(i) && !failed.count(i)) return true;
		return false;
	}

	bool MangaReader::atEnd() const
	{
		if (!opened) return false;
		if (readMode != MANGA_STRIP) return paged.atEnd();
		return strip.offset() >= strip.totalHeight() - strip.screenful() / 0.9 - 1;
	}
}
