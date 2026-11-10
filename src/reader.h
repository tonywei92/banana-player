#pragma once
// The manga reader on the top panel (docs/design/manga.md section 7): a chapter (a folder of pictures, a CBZ, or a
// source's pages by URL, fetched into the page cache) as one continuous Strip, its pages decoded on two threads and
// drawn by a PageRenderer in the top window's GL context. Main thread only, but for the decoder's threads, which never
// touch it: they hand pages back and push `wakeEvent`.
#include "cbz.h"
#include "pagecache.h"
#include "pagedecode.h"
#include "pagerender.h"
#include "paged.h"
#include "settings.h"
#include "strip.h"
#include <SDL.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace bp
{
	class MangaReader
	{
	public:
		~MangaReader();
		// once, with the top window's context current; false where GL can't (then it never opens)
		bool init(bool gles, Uint32 wakeEvent);
		// a chapter at a page (0-based); fit: MANGA_FIT_* (the Strip always fits width); false (error() says why)
		// when it can't be listed
		bool open(const std::string& path, bool archive, int page, int viewW, int viewH, int mode = MANGA_STRIP,
			int fit = MANGA_FIT_PAGE);
		// A chapter from a source or a server: its pages' URLs, in order, and the headers they need. files[i] is the
		// page's cache path (the cache's key is the URL), so a page not fetched yet is "waiting": no decode job is
		// queued for it until its file is there. chapterId is only the chapter's name in the log.
		using Headers = std::vector<std::pair<std::string, std::string>>;
		bool open(const std::vector<std::string>& pageUrls, const Headers& headers, PageCache& cache,
			const std::string& chapterId, int page, int viewW, int viewH, int mode = MANGA_STRIP,
			int fit = MANGA_FIT_PAGE);
		bool remote() const { return !urls.empty(); }
		// the wanted pages (in want()'s order) whose files are missing and are not in flight, at most max; the ones
		// handed out count as in flight until pageArrived / pageFailed / pageDropped
		std::vector<std::pair<int, std::string>> pagesToFetch(int max);
		// main's fetcher, as one finishes: arrival queues its decode at once; a failure shows the grey placeholder,
		// puts the error in error(), and the page is asked for again the next time it is wanted
		void pageArrived(int index);
		void pageFailed(int index, const std::string& error);
		// a request let go because the reader moved away (not a failure: no error, no retry until wanted again)
		void pageDropped(int index);
		std::vector<int> wantedPages() const;
		const Headers& pageHeaders() const { return headers; }
		void close();
		bool isOpen() const { return opened; }
		// the bottom panel's wishes (Ui::ReaderCmd's kinds, as ints so this stays apart from Ui)
		enum Cmd { Close, Step, Page, Seek, TouchDown, Drag, Release, Mode, Fit, Fullscreen, Zoom };
		// Drag: v the finger's dy as a fraction of the height, v2 its dx as a fraction of the width
		// Zoom: v the new factor (1 = the fit); the pages are decoded again at that width once it rests
		void command(int kind, double v, int64_t ms, double v2 = 0);
		// the pages decoded ahead of the one read (the Preload setting): applied at once, the queue made again
		void setPreload(int n);
		// a pass of main's loop: decoded pages taken in, the motion advanced; true when the top panel needs a frame
		bool frame(int64_t now);
		// the frame, into the bound framebuffer
		void draw(int w, int h);
		// still moving or uploading: main keeps its loop at the display's rate
		bool busy() const { return strip.moving() || paged.moving() || uploads || forceFrames > 0; }
		int mode() const { return readMode; }
		int page() const { return !opened ? 0 : std::max(0, readMode == MANGA_STRIP ? strip.currentPage() : paged.current()); }
		int pages() const { return (int)files.size(); }
		bool loading() const;
		bool atEnd() const;
		const std::string& error() const { return err; }
		// fullscreen: the view is both panels' height and the hinge's gap; the top panel shows its first panel-height,
		// the bottom panel the rest (bottomBoxes, in the bottom panel's pixels, which match the top's)
		bool fullscreen() const { return full; }
		struct Box { int page; float x, y, w, h; };
		std::vector<Box> bottomBoxes() const;
		// a decoded page's pixels (RGBA, straight alpha) for the bottom panel's own textures; null when not decoded
		std::shared_ptr<const std::vector<uint8_t>> pixels(int page, int& w, int& h) const;
		uint64_t chapterGen() const { return gen; }
		// 1 while the fit is shown, as the Zoom steps leave it (100 % … 800 %)
		double zoom() const { return zscale; }
		int zoomPct() const { return (int)(zscale * 100 + 0.5); }	// (z is always one of the steps, 100 … 800)
		// a few frames more (KMSDRM drops the first after a start or a wake)
		void redrawSoon(int n = 5) { forceFrames = std::max(forceFrames, n); }

	private:
		struct Job { int index; std::string path; int width; };
		struct Done { int index; PageImage img; Uint32 ms; uint64_t gen; int width; };
		void decodeLoop();
		// files is ready: the models, the page and the first want()
		bool begin(const std::string& label, int page, int w, int h, int m, int f);
		void want();
		void applyFit();
		void applyZoom();
		void setFullscreen(bool on, int hinge480);
		int layoutH() const { return full ? viewH * 2 + gapPx : viewH; }
		int decodeW() const;	// the width wanted now: z times the view's, up to 2048 (decodePage's 2x cap still applies)
		std::vector<Box> boxes() const;	// every page in view now, in view pixels (y from the top panel's top)

		PageRenderer pr;
		bool ready = false, opened = false, uploads = false;
		Strip strip;
		Paged paged;	// the two paged modes (right to left, left to right); both models follow every page's size
		int readMode = MANGA_STRIP;
		int fit = MANGA_FIT_PAGE, preload = MANGA_PRELOAD_DEFAULT;
		double zscale = 1;	// the zoom of both models; 1 is the fit
		int64_t zoomAt = 0;	// when the last Zoom came; the re-decode waits for it to rest (0: none waiting)
		std::vector<std::string> files;
		// a URL chapter: files[i] = pageCache->pathFor(urls[i]); fetching and failed are the pages' requests and their
		// last failure, so a wanted page is asked for once and a dead URL doesn't spin
		std::vector<std::string> urls;
		Headers headers;
		PageCache* pageCache = nullptr;
		std::string chapterKey;
		std::set<int> fetching, failed;
		Cbz cbz;
		bool archive = false;
		int viewW = 0, viewH = 0, forceFrames = 0;
		bool full = false;	// (kept across chapters while the reader is open)
		int gapPx = 0;
		struct Pix { int w = 0, h = 0; std::shared_ptr<const std::vector<uint8_t>> rgba; };
		std::map<int, Pix> kept;	// the decoded pages near the view, for the bottom panel in fullscreen
		std::map<int, int> decodedW;	// each page's decoded width: a zoom in decodes it again, wider
		std::string err;
		int errPage = -1;	// the page err is about (a URL page that failed); -1 for the chapter's own error
		bool fingerDown = false;
		// the decoder: its queue, its results, the chapter it works for (gen: a page of the last chapter is dropped)
		std::mutex m, cbzLock;
		std::condition_variable cv;
		std::deque<Job> jobs;
		std::deque<Done> done;
		std::set<int> busySet;
		std::vector<std::thread> threads;
		bool quit = false;
		uint64_t gen = 0;
		Uint32 wake = 0;
	};
}
