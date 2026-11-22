#pragma once
// The manga reader on the top panel (docs/design/manga.md section 7): a chapter (a folder of pictures, a CBZ, or a
// source's pages by URL, fetched into the page cache) as one continuous Strip, its pages decoded on two threads and
// drawn by a PageRenderer in the top window's GL context. Main thread only, but for the decoder's threads, which never
// touch it: they hand pages back and push `wakeEvent`.
#include "bands.h"
#include "manga_ui_logic.h"
#include "pagebook.h"
#include "pagecache.h"
#include "pagedecode.h"
#include "pagerender.h"
#include "paged.h"
#include "readerfetchplan.h"
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
		// the fetcher's plan follows a jump through these: the target, when it committed, and whether it is in
		int jumpTarget() const { return planTarget; }
		int64_t jumpAt() const { return planAt; }
		bool jumpTargetIn() const;
		const Headers& pageHeaders() const { return headers; }
		void close();
		bool isOpen() const { return opened; }
		// the bottom panel's wishes (Ui::ReaderCmd's kinds, as ints so this stays apart from Ui)
		enum Cmd { Close, Step, Page, Seek, TouchDown, Drag, Release, Mode, Fit, Fullscreen, Zoom, Rotate, ZoomPoint, Retry, Book, Pan, Stick, Thumbs };
		// Drag: v the finger's dy as a fraction of the height, v2 its dx as a fraction of the width
		// Zoom: v the new factor (1 = the fit); the pages are decoded again at that width once it rests
		// Rotate: paged modes only; each press turns 90° clockwise
		// ZoomPoint: v the factor, v2/v3 the tap point as fractions of the view (0..1)
		// Book: v MANGA_BOOK_* (issue #7): held sideways, a page turned upright on each panel, a spread at a time.
		// Step turns a spread, Page moves one page (to realign it); zoom, fit, the turn (L3) and the finger's drag
		// are the Ui's to leave out (the reader ignores them). A Strip opens it in right to left.
		// Pan: the paged modes' page shown centred on (v, v2), shares of the page (the PDF viewer's pan area)
		// Stick: v the left stick's LEFTY (-32768 up … 32767 down), v2 its LEFTX (-32768 left … 32767 right): the Strip
		// scrolls (up/down), a paged page with room pans both ways, every frame while held (issue 16)
		// Thumbs: the pages grid shows pages v … v2 (v < 0: it closed): their thumbnails are decoded small, after
		// the reader's own pages, whether or not the view has been near them
		void command(int kind, double v, int64_t ms, double v2 = 0, double v3 = 0);
		// a paged mode's page in view and the part of it shown, as shares of the page; false in the Strip
		bool panBox(int& page, float& x0, float& y0, float& x1, float& y1) const;
		// every page times this colour (a reading filter; 1,1,1: none)
		void setTint(float r, float g, float b)
		{
			if (r == tint[0] && g == tint[1] && b == tint[2]) return;
			tint[0] = r; tint[1] = g; tint[2] = b;
			pr.setTint(r, g, b);
			redrawSoon(2);
		}
		// the pages decoded ahead of the one read (the Preload setting): applied at once, the queue made again
		void setPreload(int n);
		// each pass, before the fetcher: a rest of Seeks (a slider or grid drag) commits its target then
		void tick(int64_t now);
		// a Seek run is held (a drag): main keeps its loop short so the rest is noticed at once
		bool seekPending() const { return seeks.pending(); }
		// a pass of main's loop: decoded pages taken in, the motion advanced; true when the top panel needs a frame
		bool frame(int64_t now);
		// the frame, into the bound framebuffer
		void draw(int w, int h);
		// still moving or uploading: main keeps its loop at the display's rate
		bool busy() const
		{
			return strip.moving() || paged.moving() || uploads || forceFrames > 0 || (stickActive() && stickCanMove);
		}
		int mode() const { return readMode; }
		// the Strip's place inside page() (Strip::within); 0 in the paged modes
		double within() const { return !opened || bookHold || readMode != MANGA_STRIP ? 0 : strip.within(); }
		void scrollWithin(int page, double share) { if (opened && readMode == MANGA_STRIP && !bookHold) strip.scrollToWithin(page, share); }
		int page() const { return !opened ? 0 : bookHold ? bookS : std::max(0, readMode == MANGA_STRIP ? strip.currentPage() : paged.current()); }
		int pages() const { return (int)files.size(); }
		bool loading() const;
		bool atEnd() const;
		const std::string& error() const { return err; }
		// a page in view couldn't be fetched, decoded or put on the GPU: the Ui offers Try again (the Retry command)
		bool failedInView() const;
		void sizeFromFile(int index);	// a page on the card: its strip place sized from its file's header
		// the page turn in degrees (0, 90, 180, 270); paged modes only (Strip always 0)
		int rotation() const { return readMode != MANGA_STRIP ? angle : 0; }
		// fullscreen: the view is both panels' height and the hinge's gap; the top panel shows its first panel-height,
		// the bottom panel the rest (bottomBoxes, in the bottom panel's pixels, which match the top's)
		bool fullscreen() const { return full; }
		// Book (MANGA_BOOK_*): both panels show pages too (bottomBoxes), one each; fullscreen() is left as it was
		int book() const { return bookHold; }
		bool bothPanels() const { return full || bookHold != MANGA_BOOK_OFF; }
		// Book: how many pages the screen shows (2, or 1: a lone page before a spread, the last, a spread itself)
		int bookCount() const { return inBook() ? bookScreen().count : 0; }
		// Book: a wide page (a double-page spread) spans both panels, cut down the middle across the hinge (the
		// setting "Wide pages span both screens"; hinge480: the Hinge gap row, the strip the hinge hides)
		void setBookSpread(bool on, int hinge480);
		// page: a page's index, or a band's key (bands.h) for a long strip page drawn as its bands
		struct Box { int page; float x, y, w, h; };
		std::vector<Box> bottomBoxes() const;
		// a decoded page's (or band's: a Box's page) pixels (RGBA, straight alpha) for the bottom panel's own textures;
		// null when not decoded
		std::shared_ptr<const std::vector<uint8_t>> pixels(int page, int& w, int& h) const;
		// the pages grid's thumbnails (docs/design/manga.md section 3): ~96 px wide RGBA, made as each page decodes;
		// null for a page not decoded yet (the Ui draws a numbered placeholder). kept for the whole chapter so a
		// 200-page chapter's ~10 MB of thumbs stay bounded and don't need to be remade
		std::shared_ptr<const std::vector<uint8_t>> thumbPixels(int page, int& w, int& h) const;
		uint64_t chapterGen() const { return gen; }
		// 1 while the fit is shown, as the Zoom steps leave it (100 % … 800 %)
		double zoom() const { return zscale; }
		int zoomPct() const { return (int)(zscale * 100 + 0.5); }	// (z is always one of the steps, 100 … 800)
		// a card over the pages (the level card, levelcard.h: the volume, brightness or zoom while reading), RGBA,
		// until untilMs
		void showOverlay(std::shared_ptr<const std::vector<uint8_t>> rgba, int w, int h, int64_t untilMs);
		// a note over the pages' foot, centred (a bookmark made, the chapter's end: levelcard.h's note and end cards),
		// beside the level card and until untilMs the same way
		void showNote(std::shared_ptr<const std::vector<uint8_t>> rgba, int w, int h, int64_t untilMs);
		// a few frames more (KMSDRM drops the first after a start or a wake)
		void redrawSoon(int n = 5) { forceFrames = std::max(forceFrames, n); }
		// a page still loading shows a turning ring: main wakes this often to draw its next step
		static constexpr int SPIN_MS = 110;
		// decoded pages kept around the view, in bytes of pixels (the textures take as much again)
		static constexpr size_t DECODED_BUDGET = 32u << 20;
		// one page decoded at most this many pixels (32 MB of RGBA), never enlarged past its own width
		static constexpr long long PAGE_MAX_PIXELS = 8LL * 1000 * 1000;
		bool spinning() const { return opened && pr.spun(); }

	private:
		// band >= 0: that band of a long page (the image's rows y0 .. y0 + rows). band -1: the whole page, or, with
		// mayBand (the Strip), band 0 when the thread finds the page long (its size isn't known before its bytes are read)
		struct Job { int index; std::string path; int width; int band = -1; int y0 = 0, rows = 0; bool mayBand = false; bool thumb = false; };
		struct Done { int index; PageImage img; Uint32 ms; uint64_t gen; int width; int band; bool thumb = false; };
		void decodeLoop();
		// the stick's speed now (0 at rest, inside its dead zone, in Book, or on a paged page that fits the view)
		double stickSpeedNow() const;
		double stickSpeedXNow() const;
		bool stickActive() const { return stickSpeedNow() != 0 || stickSpeedXNow() != 0; }
		// files is ready: the models, the page and the first want()
		bool begin(const std::string& label, int page, int w, int h, int m, int f);
		void want();
		// the reader's live order (visible pages first, then the preload), from which want() queues
		std::vector<int> liveWanted() const;
		// a jump's target: from -> to, its preload held until it is in (or the grace)
		void commitJump(int from, int to, int seeksCount, int64_t now);
		const char* modeName() const;
		void applyFit();
		void applyZoom();
		void setFullscreen(bool on, int hinge480);
		// the hinge's gap as it applies: the Strip skips it (a picture runs on across the fold); the paged modes put
		// the panels edge to edge. (Book's wide pages skip it too, by bookGapNow)
		int hingeGap() const { return readMode == MANGA_STRIP ? gapPx : 0; }
		int layoutH() const { return full ? viewH * 2 + hingeGap() : viewH; }
		int decodeW() const;	// the width wanted now: z times the view's, up to 2048 (decodePage's 2x cap still applies)
		std::vector<Box> boxes() const;	// every page (or band) in view now, in view pixels (y from the top panel's top)
		// a long page's bands to have decoded now: the ones in view, and with margins the ones a screen above and two
		// below (the order to decode them in); empty for a page that isn't banded
		std::vector<int> bandsWanted(int page, bool margins = true) const;
		// the page's textures are there: for a banded page, those of its bands in view
		bool pageIn(int page) const;
		void makeThumb(int page, const std::vector<uint8_t>& rgba, int pw, int ph);
		// the page turn (L3): each press 90° clockwise; paged modes only; the fit and zoom work on the turned page
		void applyRotation();
		// a reading mode change, the page under the view kept (the Mode command, and Book leaving the Strip)
		void setMode(int m, int64_t ms);
		// Book: on (held as b) or off; and the spread to show (its first page; the last page may be alone)
		void setBook(int b);
		void bookGo(int first);
		std::vector<Box> bookBoxes() const;
		bool inBook() const { return bookHold && readMode != MANGA_STRIP; }
		// the pages' wide flags as pairing reads them (all clear with the setting off)
		const std::vector<char>& bookWide() const { return spreadOn ? wide : notWide; }
		MangaBookScreen bookScreen() const { return mangaBookScreenFrom(bookWide(), bookS); }
		// a page's size learnt (its file's header or its decode): its wide flag; true when it changed
		bool noteShape(int page, int w, int h);
		int decodeWFor(int page) const;	// decodeW(), but a spread in Book wants both panels' length
		int bookGapNow() const { return spreadOn ? bookGapPx : 0; }
		// rotate an RGBA buffer (w x h) by angle degrees clockwise; the returned buffer is (rw x rh), swapped if 90/270
		static std::shared_ptr<const std::vector<uint8_t>> rotatePixels(
			const std::shared_ptr<const std::vector<uint8_t>>& src, int w, int h, int angle);

		PageRenderer pr;
		bool ready = false, opened = false, uploads = false;
		Strip strip;
		Paged paged;	// the two paged modes (right to left, left to right); both models follow every page's size
		int readMode = MANGA_STRIP;
		int fit = MANGA_FIT_PAGE, preload = MANGA_PRELOAD_DEFAULT;
		double zscale = 1;	// the zoom of both models; 1 is the fit
		int64_t zoomAt = 0;	// when the last Zoom came; the re-decode waits for it to rest (0: none waiting)
		int angle = 0;	// the page turn in degrees (0, 90, 180, 270); paged modes only; back to 0 on a new chapter
		std::vector<std::string> files;
		// the fetch plan follows the view only once a Seek run has settled; the jump's target goes first, alone
		std::vector<int> fetchWanted;
		int planPage = -1;		// the page the last jump committed (the next log's "from")
		int planTarget = -1;	// the page a jump waits on before its neighbours may start
		int64_t planAt = 0;		// when that jump committed (the target's grace runs from here)
		SeekRun seeks;
		std::set<int> wantKeep;	// the pages and bands (keys) a decode result is still welcome for (want() sets it)
		// a URL chapter: files[i] = pageCache->pathFor(urls[i]); fetching and failed are the pages' requests and their
		// last failure, so a wanted page is asked for once and a dead URL doesn't spin
		std::vector<std::string> urls;
		Headers headers;
		PageCache* pageCache = nullptr;
		std::string chapterKey;
		std::set<int> fetching, failed;
		// the pages of a URL chapter whose file is on the card: filled at open and at each arrival, so a scroll never
		// stats the card once per wanted page per frame (want(), the fetch plan and jumpTargetIn read it)
		std::set<int> onCard;
		std::unique_ptr<PageBook> pbook;	// a chapter in one file (CBZ, CBR, PDF); null for a folder or URLs
		bool archive = false;
		int viewW = 0, viewH = 0, forceFrames = 0;
		// the left stick (issue 16): its last LEFTY value, when the last scrolling frame ran, and whether that
		// frame could still move the view (at an end it can't: main's loop needn't spin until the value changes)
		int stickValue = 0, stickValueX = 0;
		int64_t stickAt = 0;
		bool stickCanMove = false;
		int lastSpin = -1;
		int64_t spinNow = 0;
		bool full = false;	// (kept across chapters while the reader is open)
		int bookHold = 0;	// MANGA_BOOK_*: main sets it again for each chapter (the series' own)
		int bookS = 0;		// Book: the screen's first page (its count follows the pages' shapes: bookScreen)
		std::vector<char> wide, notWide;	// per page: a double-page spread (mangaBookWide); notWide all clear
		bool spreadOn = true;	// Wide pages span both screens
		int bookGapPx = 0;	// the hinge's hidden strip between the panels, in view pixels (a spread is cut across it)
		int bookPrevAngle = 0;	// the turn (L3) Book took over, given back when it ends
		CardOverlay card;	// the level card over the pages
		CardOverlay note;	// the note at their foot (showNote gives it its own texture key and place)
		int gapPx = 0;
		float tint[3] = {1, 1, 1};
		struct Pix { int w = 0, h = 0; std::shared_ptr<const std::vector<uint8_t>> rgba; };
		std::map<int, Pix> kept;	// the decoded pages near the view, for the bottom panel in fullscreen
		std::map<int, Pix> orig;	// the same pages unrotated: when the turn changes, kept is re-made from these
		// thumbnails for the pages grid: ~96 px wide, one per decoded page, kept for the whole chapter
		struct Thumb { int w = 0, h = 0; std::shared_ptr<const std::vector<uint8_t>> rgba; };
		std::map<int, Thumb> thumbs;
		// the grid's own asks (Thumbs): the pages it shows, the ones a thread is on, the ones that couldn't be decoded.
		// A page decoded only for its thumbnail is never pixels or textures of the view (THUMB_DECODE_W wide)
		static constexpr int THUMB_DECODE_W = 192;
		int thumbFrom = -1, thumbTo = -1;
		std::set<int> thumbBusy, thumbFailed;
		std::map<int, int> decodedW;	// each page's decoded width: a zoom in decodes it again, wider
		// The Strip's long pages (bands.h): found when a page's first band comes back. Their pixels, textures and
		// decoded widths are per band, under bandKey(page, band) in kept, decodedW, wantKeep, busySet and the renderer.
		// noBand: a page whose band couldn't be decoded (its whole-page decode took over). Both forgotten on a
		// mode change: the paged modes show whole pages.
		std::map<int, BandLayout> bands;
		std::set<int> noBand;
		std::string err;
		int errPage = -1;	// the page err is about (a URL page that failed); -1 for the chapter's own error
		bool fingerDown = false;
		// the decoder: its queue, its results, the chapter it works for (gen: a page of the last chapter is dropped)
		std::mutex m, cbzLock;	// cbzLock: the book (one read at a time: an archive's position is shared)
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
