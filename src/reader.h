#pragma once
// The manga reader on the top panel (docs/design/manga.md section 7): a chapter (a folder of pictures or a CBZ) as one
// continuous Strip, its pages decoded on two threads and drawn by a PageRenderer in the top window's GL context. Main
// thread only, but for the decoder's threads, which never touch it: they hand pages back and push `wakeEvent`.
#include "cbz.h"
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
		void close();
		bool isOpen() const { return opened; }
		// the bottom panel's wishes (Ui::ReaderCmd's kinds, as ints so this stays apart from Ui)
		enum Cmd { Close, Step, Page, Seek, TouchDown, Drag, Release, Mode, Fit, Fullscreen };
		// Drag: v the finger's dy as a fraction of the height, v2 its dx as a fraction of the width
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
		// a few frames more (KMSDRM drops the first after a start or a wake)
		void redrawSoon(int n = 5) { forceFrames = std::max(forceFrames, n); }

	private:
		struct Job { int index; std::string path; int width; };
		struct Done { int index; PageImage img; Uint32 ms; uint64_t gen; };
		void decodeLoop();
		void want();
		void applyFit();
		void setFullscreen(bool on, int hinge480);
		int layoutH() const { return full ? viewH * 2 + gapPx : viewH; }
		std::vector<Box> boxes() const;	// every page in view now, in view pixels (y from the top panel's top)

		PageRenderer pr;
		bool ready = false, opened = false, uploads = false;
		Strip strip;
		Paged paged;	// the two paged modes (right to left, left to right); both models follow every page's size
		int readMode = MANGA_STRIP;
		int fit = MANGA_FIT_PAGE, preload = MANGA_PRELOAD_DEFAULT;
		std::vector<std::string> files;
		Cbz cbz;
		bool archive = false;
		int viewW = 0, viewH = 0, forceFrames = 0;
		bool full = false;	// (kept across chapters while the reader is open)
		int gapPx = 0;
		struct Pix { int w = 0, h = 0; std::shared_ptr<const std::vector<uint8_t>> rgba; };
		std::map<int, Pix> kept;	// the decoded pages near the view, for the bottom panel in fullscreen
		std::string err;
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
