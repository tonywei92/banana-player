#pragma once
// The manga reader's Strip (docs/design/manga.md, section 4): one continuous column of pages, drawn at the view's
// width one under another, that a finger drags 1:1 and that glides on with inertia. Pure geometry and motion: no
// window, no drawing and no clock of its own (every time is the host's int64 milliseconds), so the same gestures
// and steps replay exactly in core-test.
#include <cstdint>
#include <utility>
#include <vector>

namespace bp
{
	class Strip
	{
	public:
		// the screen in pixels; pages are fitted to its width, so a new width rescales every page
		void setView(int width, int height);

		// count pages whose real sizes aren't known yet (each placeholderAspect * width tall). Starts at the top
		void setPages(int count, double placeholderAspect = 1.5);
		// a page's real pixel size is known: its height becomes width * h / w. What is on screen stays put: when the
		// page starts above the view's top, the offset moves by the height change
		void setPageSize(int index, int w, int h);

		int pageCount() const;
		double pageTop(int i) const;
		double pageHeight(int i) const;
		double totalHeight() const;

		// the view's top inside the column, always within [0, max(0, totalHeight - viewHeight)]
		double offset() const;
		void scrollTo(double y);
		void scrollBy(double dy);

		// the first and last page that intersect the view, both inclusive; {-1, -1} with no pages
		std::pair<int, int> visible() const;
		// the page under the view's vertical middle ("page 3 of 20"), -1 with no pages
		int currentPage() const;
		// pages to decode, most urgent first: visible ones top to bottom, then `ahead` in the direction of the last
		// motion (down if there was none), then `behind` on the other side; no duplicates
		std::vector<int> wanted(int ahead, int behind) const;

		// touch: a drag follows the finger (dy > 0 is a finger moving down, so the offset falls, as on a phone)
		void touchDown(int64_t ms);
		void drag(double dy, int64_t ms);
		void release(int64_t ms);	// starts a fling from the last 100 ms of drags

		// a D-pad screenful step: ease-out cubic from here to y
		void animateTo(double y, int64_t ms, int64_t durationMs = 180);

		// advance a fling or an animation to ms; true while it is still moving afterward
		bool step(int64_t ms);
		bool moving() const;

		// the D-pad step's size
		double screenful() const;

	private:
		struct Drag
		{
			int64_t ms;
			double off;
		};

		void rebuild();
		double maxOffset() const;
		double clampOff(double y) const;
		int indexAt(double y) const;
		void stop();
		void shiftMotion(double delta);

		int viewW = 0, viewH = 0;
		std::vector<double> aspect;	// each page's height / width
		std::vector<double> top;	// page i's top; top[n] is the total height
		double off = 0;
		int dir = 0;	// 1 the last motion went down the column (toward later pages), -1 up, 0 none yet

		bool fling = false;
		double flingV = 0;	// px/s at the release
		double flingFrom = 0;
		int64_t flingAt = 0;

		bool anim = false;
		double animFrom = 0, animTo = 0;
		int64_t animAt = 0, animDur = 0;

		std::vector<Drag> drags;	// the current gesture's samples, for the release's velocity
	};
}
