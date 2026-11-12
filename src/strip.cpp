#include "strip.h"
#include <cmath>

namespace bp
{
	namespace
	{
		// a fling's speed halves every 225 ms or so: slow enough that a hard flick still shows the pages on the way
		constexpr double TAU_MS = 325;
		constexpr double STOP_SPEED = 20;	// px/s: under this the eye can't tell a fling from still
		constexpr double MAX_FLING = 6000;	// px/s: a faster flick would only race through blank placeholders
		constexpr int64_t VELOCITY_WINDOW_MS = 100;
	}

	void Strip::setView(int width, int height)
	{
		viewW = width > 0 ? width : 0;
		viewH = height > 0 ? height : 0;
		// heights are kept as aspects, so a panel of another width rescales every page rather than cropping it
		rebuild();
		off = clampOff(off);
		panX = clampPan(panX);
	}

	void Strip::setPages(int count, double placeholderAspect)
	{
		// a nonsense aspect would collapse the column to nothing; 1.5 is the page a comic mostly is
		double a = std::isfinite(placeholderAspect) && placeholderAspect > 0 ? placeholderAspect : 1.5;
		aspect.assign(count > 0 ? (size_t)count : 0, a);
		rebuild();
		off = 0;
		panX = 0;
		stop();
		drags.clear();
		dir = 0;
	}

	void Strip::setZoom(double z, double fx, double fy)
	{
		double to = z;
		if (!(to >= 1)) to = 1;	// also catches a NaN
		if (to > 8) to = 8;
		if (to == zscale) return;
		double k = to / zscale;
		// the content under the focus keeps its place: the column's y at the view's fy is off + fy and its x at the
		// view's fx is panX + fx, and both scale with the pages
		zscale = to;
		rebuild();
		off = clampOff((off + fy) * k - fy);
		panX = clampPan((panX + fx) * k - fx);
		stop();
		drags.clear();
	}

	void Strip::setPageSize(int index, int w, int h)
	{
		if (index < 0 || index >= pageCount() || w <= 0 || h <= 0) return;
		double wasTop = pageTop(index);
		double wasHeight = pageHeight(index);
		double was = off;
		aspect[index] = (double)h / w;
		rebuild();
		// the page above the view grew or shrank: the offset follows by the same amount, so what is on screen keeps
		// its place (a page at or below the top doesn't move anything that is visible)
		if (wasTop < was)
		{
			off = clampOff(was + (pageHeight(index) - wasHeight));
			shiftMotion(off - was);
		}
		else
		{
			off = clampOff(off);
		}
	}

	int Strip::pageCount() const
	{
		return (int)aspect.size();
	}

	double Strip::pageTop(int i) const
	{
		if (top.empty() || i <= 0) return 0;
		if (i >= pageCount()) return top.back();
		return top[i];
	}

	double Strip::pageHeight(int i) const
	{
		if (i < 0 || i >= pageCount()) return 0;
		return aspect[i] * viewW * zscale;
	}

	double Strip::totalHeight() const
	{
		return top.empty() ? 0 : top.back();
	}

	double Strip::offset() const
	{
		return off;
	}

	void Strip::scrollTo(double y)
	{
		double to = clampOff(y);
		if (to != off) dir = to > off ? 1 : -1;
		off = to;
		stop();
		drags.clear();
	}

	void Strip::scrollBy(double dy)
	{
		scrollTo(off + dy);
	}

	std::pair<int, int> Strip::visible() const
	{
		if (aspect.empty() || viewH <= 0) return {-1, -1};
		double from = off, to = off + viewH;
		int first = -1, last = -1;
		for (int i = 0; i < pageCount(); i++)
		{
			if (top[i] >= to) break;
			// a zero-height page has nothing to show, even inside the view
			if (top[i + 1] > top[i] && top[i + 1] > from)
			{
				if (first < 0) first = i;
				last = i;
			}
		}
		return {first, last};
	}

	int Strip::currentPage() const
	{
		if (aspect.empty()) return -1;
		return indexAt(off + viewH * 0.5);
	}

	std::vector<int> Strip::wanted(int ahead, int behind) const
	{
		std::vector<int> out;
		int n = pageCount();
		if (n <= 0) return out;
		// the whole list is all any caller can use, and capping keeps the loops' sums in range
		if (ahead < 0) ahead = 0;
		if (behind < 0) behind = 0;
		if (ahead > n) ahead = n;
		if (behind > n) behind = n;

		auto vis = visible();
		int first = vis.first, last = vis.second;
		if (first < 0) first = last = indexAt(off + viewH * 0.5);
		for (int i = first; i <= last; i++) out.push_back(i);

		// "ahead" is where reading is going: down the column (later pages) unless the last motion went up
		if (dir >= 0)
		{
			for (int i = last + 1; i < n && i - last <= ahead; i++) out.push_back(i);
			for (int i = first - 1; i >= 0 && first - i <= behind; i--) out.push_back(i);
		}
		else
		{
			for (int i = first - 1; i >= 0 && first - i <= ahead; i--) out.push_back(i);
			for (int i = last + 1; i < n && i - last <= behind; i++) out.push_back(i);
		}
		return out;
	}

	void Strip::touchDown(int64_t)
	{
		stop();
		drags.clear();
	}

	void Strip::drag(double dy, int64_t ms, double dx)
	{
		stop();
		double was = off;
		off = clampOff(off - dy);	// fingers move the content, so a downward drag walks up the column
		panX = clampPan(panX - dx);	// and a rightward one reveals what is left of the zoomed column
		if (off != was) dir = off > was ? 1 : -1;
		drags.push_back({ms, off, panX});
		// 200 ms is twice the release's window: pruning here can never starve the estimate, and a gesture's samples
		// stay small however long the finger drags
		size_t drop = 0;
		while (drop + 1 < drags.size() && drags[drop].ms < ms - 200) drop++;
		if (drop) drags.erase(drags.begin(), drags.begin() + drop);
	}

	void Strip::release(int64_t ms)
	{
		// the finger's velocity is how fast the content was moving in the last 100 ms before it let go; a finger
		// that rested there has no sample in that window and so no fling
		double v = 0, vx = 0;
		size_t at = 0;
		while (at < drags.size() && drags[at].ms < ms - VELOCITY_WINDOW_MS) at++;
		if (at + 1 < drags.size())
		{
			const Drag& a = drags[at];
			const Drag& b = drags.back();
			int64_t dt = b.ms - a.ms;
			if (dt > 0)
			{
				v = (b.off - a.off) / (double)dt * 1000;
				vx = (b.pan - a.pan) / (double)dt * 1000;	// (0 at the fit: the column can't pan)
			}
		}
		drags.clear();
		v = std::max(-MAX_FLING, std::min(MAX_FLING, v));
		vx = std::max(-MAX_FLING, std::min(MAX_FLING, vx));
		// a speed the fling would stop at on its first step is no motion at all
		if (std::fabs(v) < STOP_SPEED) v = 0;
		if (std::fabs(vx) < STOP_SPEED) vx = 0;
		if (v == 0 && vx == 0) return;
		stop();
		fling = true;
		flingV = v;
		flingFrom = off;
		flingVX = vx;
		flingFromX = panX;
		flingAt = ms;
		if (v != 0) dir = v > 0 ? 1 : -1;
	}

	void Strip::animateTo(double y, int64_t ms, int64_t durationMs)
	{
		double to = clampOff(y);
		stop();
		drags.clear();
		double delta = to - off;
		if (delta != 0) dir = delta > 0 ? 1 : -1;
		if (delta == 0 || durationMs <= 0)
		{
			off = to;
			return;
		}
		anim = true;
		animFrom = off;
		animTo = to;
		animAt = ms;
		animDur = durationMs;
	}

	bool Strip::step(int64_t ms)
	{
		if (fling)
		{
			double t = (double)(ms - flingAt);
			if (t < 0) t = 0;
			bool done = false;
			// both axes slow on the one curve: the faster one says when it ends
			double v0 = std::max(std::fabs(flingV), std::fabs(flingVX));
			double speed = v0 * std::exp(-t / TAU_MS);
			if (speed < STOP_SPEED)
			{
				// stop where the curve itself ends, not where this caller's frame happens to land, so a 5 ms and a
				// 50 ms stepper finish on the same pixel
				t = TAU_MS * std::log(v0 / STOP_SPEED);
				done = true;
			}
			// the integral of v0 * exp(-t / tau): v0 * tau * (1 - exp(-t / tau)), with tau in seconds
			double k = (TAU_MS / 1000.0) * (1 - std::exp(-t / TAU_MS));
			double y = flingFrom + flingV * k, x = flingFromX + flingVX * k;
			double max = maxOffset();
			// an axis that meets its end stops there; the fling goes on while the other still moves
			bool yEnd = flingV == 0 || (flingV > 0 && y >= max) || (flingV < 0 && y <= 0);
			off = std::max(0.0, std::min(max, y));
			double px = clampPan(x);
			bool xEnd = flingVX == 0 || px != x;
			panX = px;
			if (done || (yEnd && xEnd))
			{
				stop();
				return false;
			}
			return true;
		}
		if (anim)
		{
			int64_t el = ms - animAt;
			if (el < 0) el = 0;
			if (el >= animDur)
			{
				off = clampOff(animTo);	// exactly the target at the end, whatever the frames did
				stop();
				return false;
			}
			double u = (double)el / (double)animDur;
			double left = 1 - u;
			off = clampOff(animFrom + (animTo - animFrom) * (1 - left * left * left));
			return true;
		}
		return false;
	}

	bool Strip::moving() const
	{
		return fling || anim;
	}

	double Strip::screenful() const
	{
		return 0.9 * viewH;
	}

	void Strip::rebuild()
	{
		top.assign(aspect.size() + 1, 0);
		for (size_t i = 0; i < aspect.size(); i++) top[i + 1] = top[i] + aspect[i] * viewW * zscale;
	}

	double Strip::maxOffset() const
	{
		double m = totalHeight() - viewH;
		return m > 0 ? m : 0;
	}

	double Strip::clampOff(double y) const
	{
		if (!(y > 0)) return 0;	// also catches a NaN no layout should ever make
		double max = maxOffset();
		return y < max ? y : max;
	}

	double Strip::maxPan() const
	{
		double m = pageWidth() - viewW;
		return m > 0 ? m : 0;
	}

	double Strip::clampPan(double x) const
	{
		if (!(x > 0)) return 0;
		double max = maxPan();
		return x < max ? x : max;
	}

	int Strip::indexAt(double y) const
	{
		int n = pageCount();
		if (n <= 0) return -1;
		double total = totalHeight();
		if (total <= 0) return 0;
		if (y <= 0) return 0;
		if (y >= total) return n - 1;
		// the last page whose top is at or before y (a zero-height page can't hold the point)
		int lo = 0, hi = n - 1;
		while (lo < hi)
		{
			int mid = lo + (hi - lo) / 2;
			if (top[mid + 1] <= y) lo = mid + 1;
			else hi = mid;
		}
		return lo;
	}

	void Strip::stop()
	{
		fling = false;
		flingV = flingVX = 0;
		anim = false;
	}

	void Strip::shiftMotion(double delta)
	{
		if (delta == 0) return;
		// the offset moved with a page's height; a fling or an animation under way must start/target the same
		// visual place, or it would jump by that change on its next step
		if (fling) flingFrom += delta;
		if (anim)
		{
			animFrom += delta;
			animTo = clampOff(animTo + delta);
		}
	}
}
