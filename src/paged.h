#pragma once
// The manga reader's paged modes (docs/design/manga.md, sections 1, 4 and 7): pages shown one at a time, fitted to
// the view and laid side by side so a finger drag moves the row 1:1 and a release snaps it to a page. Right to left
// puts page i+1 to the LEFT of page i, as printed manga; a page taller than the view at fit Width scrolls inside
// before it turns. Pure geometry and motion like Strip: no window, no drawing and no clock of its own (every time is
// the host's int64 milliseconds), so the same gestures and steps replay exactly in core-test.
#include <cstdint>
#include <vector>

namespace bp
{
	class Paged
	{
	public:
		// how a page is scaled to the view: FitPage keeps the whole page inside it, FitWidth only its width; that
		// is the fit that can leave a page taller than the view
		enum Fit { FitPage, FitWidth };

		// the screen in pixels; every drawn size comes from it, so a new view rescales the row and keeps the page
		// being shown in place
		void setView(int width, int height);

		// count pages whose real sizes aren't known yet (each placeholderAspect high for its drawn width)
		void setPages(int count, double placeholderAspect = 1.5);
		// a page's real pixel size is known. The page being shown does not move and stays put; a neighbour's new
		// width only slides it along the row
		void setPageSize(int index, int w, int h);

		// right to left: the row reads the other way, page i+1 to the left of page i; the page shown stays where it is
		void setRtl(bool rtl);
		void setFit(Fit f);

		int pageCount() const;
		// the page being shown at rest, or the one whose centre the view's middle is nearest while the row moves;
		// -1 with no pages
		int current() const;

		// a page in view pixels: its left edge (x may be negative while dragging) and top, drawn w by h; a page
		// smaller than the view is centred
		struct Placement { int page; double x, y, w, h; };
		// every page intersecting the view now, in page number order
		std::vector<Placement> placements() const;

		// the page to show, opened where it was last left; animate glides the row over
		void goTo(int page, int64_t ms, bool animate);

		// Right and Left: one screenful through a taller-than-view page first, then the page turn. forward opens
		// the next page at its top, back the previous one at its end. Past the last page's end forward does
		// nothing and atEnd() is true; at the first page's top back does nothing.
		void forward(int64_t ms);
		void back(int64_t ms);
		// R and L: whole pages straight to them, skipping the rest of a tall one
		void nextPage(int64_t ms);
		void prevPage(int64_t ms);

		// touch: the row follows the finger 1:1 (dx > 0 the finger moved right, which in rtl is toward the next
		// page; dy > 0 a finger moving down, which scrolls up inside a tall page — pages that fit the view never
		// pan)
		void touchDown(int64_t ms);
		void drag(double dx, double dy, int64_t ms);
		// lets go: snaps to the nearest page, or to the next or previous one when the drag's velocity over the
		// last 100 ms is over 400 px/s that way; a vertical drag snaps the page's scroll to a screenful's
		// multiple the same way
		void release(int64_t ms);

		// advance a snap or a step to ms; true while it is still moving afterward
		bool step(int64_t ms);
		bool moving() const;
		// at rest on the chapter's last page, all of it shown: forward has nothing left to do
		bool atEnd() const;
		// the vertical offset inside the page being shown
		double vscroll() const;

		// pages to decode: the current one first, then up to ahead more of them in reading order, then up to
		// behind before it; no duplicates, in range
		std::vector<int> wanted(int ahead, int behind) const;

	private:
		struct Drag
		{
			int64_t ms;
			double off;	// the row where the finger had it
			double v;
		};

		void rebuild();	// the pages' drawn widths and their centres along the row
		void relayout();	// rebuild, holding the page being shown in its place on screen
		double pageHeight(int i) const;	// drawn, in view pixels
		double vMax(int i) const;	// how far page i can scroll inside the view
		double screenful() const;
		int vSteps(double vmax) const;	// screenfuls a page of that overflow holds; the last may be part one
		double vBound(int k, double vmax) const;	// the bottom of screenful k
		double snapV(double x, double vmax) const;	// to the nearest of a page's bounds
		int nearest(double x) const;	// the page whose centre is nearest a row position
		void land(int page, double vTo);
		void slide(int page, double vTo, int64_t ms, int64_t durMs);
		void stopAnim();

		int viewW = 0, viewH = 0;
		Fit fit = FitPage;
		bool rtl = false;
		std::vector<double> aspect;	// each page's height / width
		std::vector<double> wide;	// each page drawn, in view pixels
		std::vector<double> center;	// page i's centre along the row, page 0's at 0; runs with reading order
		std::vector<double> vbase;	// where each page was last left scrolled to
		double off = 0;	// the view's middle along the row; at rest exactly a page's centre
		double v = 0;	// the vertical offset inside the page nearest off
		bool anim = false;
		double animFrom = 0, animTo = 0, animFromV = 0, animToV = 0;
		int animPage = 0;
		int64_t animAt = 0, animDur = 0;
		std::vector<Drag> drags;	// the current gesture's samples, for the release's velocity
	};
}
