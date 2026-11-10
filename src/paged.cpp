#include "paged.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	namespace
	{
		// a release reads the flick from the last 100 ms of drag samples, the window Strip uses
		constexpr int64_t VELOCITY_WINDOW_MS = 100;
		// over this the flick means a page turn or one screenful further however short the drag was — a slow one
		// only turns when the finger actually carried the row past the middle, which nearest() decides
		constexpr double FLICK_PX_S = 400;
		constexpr int64_t SNAP_MS = 220;	// a finger's snap glides for this (manga.md section 7)
		constexpr int64_t STEP_MS = 180;	// the D-pad's and goTo's steps are quick slides (section 4)

		double clampD(double x, double lo, double hi)
		{
			if (!(x > lo)) return lo;	// also catches a NaN no layout should ever make
			return x < hi ? x : hi;
		}
	}

	void Paged::setView(int width, int height)
	{
		viewW = width > 0 ? width : 0;
		viewH = height > 0 ? height : 0;
		// every drawn size comes from the view, so another panel rescales the whole row
		relayout();
	}

	void Paged::setPages(int count, double placeholderAspect)
	{
		// a nonsense aspect would collapse the page to nothing; 1.5 is the page a comic mostly is
		double a = std::isfinite(placeholderAspect) && placeholderAspect > 0 ? placeholderAspect : 1.5;
		aspect.assign(count > 0 ? (size_t)count : 0, a);
		vbase.assign(aspect.size(), 0);
		rebuild();
		// a chapter starts at its first page, at the top
		off = center.empty() ? 0 : center.front();
		v = 0;
		stopAnim();
		drags.clear();
	}

	void Paged::setPageSize(int index, int w, int h)
	{
		if (index < 0 || index >= pageCount() || w <= 0 || h <= 0) return;
		aspect[index] = (double)h / w;
		// a page at fit Page changes drawn width with its shape, which slides every page after it along the row;
		// relayout keeps the one being shown centred through that
		relayout();
	}

	void Paged::setRtl(bool rtl_)
	{
		// the direction only mirrors where the neighbours fall about the centred page, so nothing moves: switching
		// mid-chapter keeps the reader on the page they are on
		rtl = rtl_;
	}

	void Paged::setFit(Fit f)
	{
		fit = f;
		relayout();
	}

	int Paged::pageCount() const
	{
		return (int)aspect.size();
	}

	int Paged::current() const
	{
		return nearest(off);
	}

	void Paged::rebuild()
	{
		size_t n = aspect.size();
		wide.assign(n, 0);
		center.assign(n, 0);
		for (size_t i = 0; i < n; i++)
		{
			// fit Page keeps the whole page inside the view; fit Width only its width, so a tall page runs over
			// the bottom and scrolls inside
			wide[i] = fit == FitWidth ? (double)viewW : std::min((double)viewW, (double)viewH / aspect[i]);
			// pages touch as printed: page i+1's centre sits half of each neighbour's width further along
			if (i > 0) center[i] = center[i - 1] + (wide[i - 1] + wide[i]) * 0.5;
		}
	}

	void Paged::relayout()
	{
		int c = nearest(off);
		int tp = anim ? animPage : -1;
		double wasC = c >= 0 ? center[c] : 0;
		double wasT = tp >= 0 ? center[tp] : 0;
		rebuild();
		if (pageCount() <= 0) return;
		// the page being shown keeps its place on the screen: the row shifts by how far its centre moved
		off += center[c] - wasC;
		if (anim)
		{
			animTo += center[tp] - wasT;
			animToV = clampD(animToV, 0, vMax(animPage));
		}
		// and a page that no longer overflows the view cannot keep its scroll, nor the pages waiting their turn
		v = clampD(v, 0, vMax(nearest(off)));
		for (size_t i = 0; i < vbase.size(); i++) vbase[i] = clampD(vbase[i], 0, vMax((int)i));
	}

	double Paged::pageHeight(int i) const
	{
		return i >= 0 && i < pageCount() ? wide[i] * aspect[i] : 0;
	}

	double Paged::vMax(int i) const
	{
		double over = pageHeight(i) - viewH;
		return over > 0 ? over : 0;
	}

	double Paged::screenful() const
	{
		return 0.9 * viewH;	// as Strip: the next step shows a sliver of what came before
	}

	int Paged::vSteps(double vmax) const
	{
		if (vmax <= 0 || screenful() <= 0) return 0;
		// the last step may be a part screenful: it ends at the page's bottom, so turning follows no later
		return (int)std::ceil(vmax / screenful());
	}

	double Paged::vBound(int k, double vmax) const
	{
		if (k <= 0) return 0;
		int steps = vSteps(vmax);
		if (k >= steps) return vmax;
		double y = k * screenful();
		return y < vmax ? y : vmax;
	}

	double Paged::snapV(double x, double vmax) const
	{
		if (vmax <= 0) return 0;
		if (screenful() <= 0) return clampD(x, 0, vmax);
		int k = (int)std::floor(clampD(x, 0, vmax) / screenful() + 0.5);
		double y = vBound(k, vmax);
		// the page's bottom is a bound too, and nearer than its rounded multiple when the last screenful is part
		// one; the rounding can't see it from below
		double next = vBound(k + 1, vmax);
		if (std::fabs(x - next) < std::fabs(x - y)) y = next;
		return y;
	}

	int Paged::nearest(double x) const
	{
		int n = pageCount();
		if (n <= 0) return -1;
		// the centres run in reading order, so the nearest is one of the two either side of x; an equal share
		// goes to the page before, so the row never hops forward on its own
		int lo = 0, hi = n;	// first i with center[i] > x
		while (lo < hi)
		{
			int mid = lo + (hi - lo) / 2;
			if (center[mid] <= x) lo = mid + 1;
			else hi = mid;
		}
		if (lo == 0) return 0;
		if (lo >= n) return n - 1;
		return x - center[lo - 1] <= center[lo] - x ? lo - 1 : lo;
	}

	std::vector<Paged::Placement> Paged::placements() const
	{
		std::vector<Placement> out;
		if (pageCount() <= 0 || viewW <= 0 || viewH <= 0) return out;
		int shown = nearest(off);
		double m = rtl ? -1 : 1;	// right to left mirrors the row about the view's middle
		for (int i = 0; i < pageCount(); i++)
		{
			// at rest page i's centre is the view's; the drag carries the row, so x may go negative at either end
			double cx = viewW * 0.5 + (center[i] - off) * m;
			double x = cx - wide[i] * 0.5;
			double h = pageHeight(i);
			if (x >= viewW || x + wide[i] <= 0 || wide[i] <= 0 || h <= 0) continue;	// not in the view
			// a page smaller than the view is centred; a taller one only comes at fit Width and scrolls inside,
			// and only the page being shown has scrolled
			double y = h <= viewH ? (viewH - h) * 0.5 : -(i == shown ? clampD(v, 0, h - viewH) : 0.0);
			out.push_back({i, x, y, wide[i], h});
		}
		return out;
	}

	void Paged::stopAnim()
	{
		anim = false;	// the finger or a new step takes the row where the glide had left it
	}

	void Paged::land(int page, double vTo)
	{
		int n = pageCount();
		if (n <= 0) return;
		if (page < 0) page = 0;
		if (page >= n) page = n - 1;
		// exactly on the page: its centre at the view's middle, and it keeps where it stands scrolled to
		off = center[page];
		v = clampD(vTo, 0, vMax(page));
		vbase[page] = v;
	}

	void Paged::slide(int page, double vTo, int64_t ms, int64_t durMs)
	{
		int n = pageCount();
		if (n <= 0) return;
		if (page < 0) page = 0;
		if (page >= n) page = n - 1;
		stopAnim();
		drags.clear();
		double to = center[page];
		double goal = clampD(vTo, 0, vMax(page));
		if (durMs <= 0 || (off == to && v == goal))
		{
			land(page, goal);	// nothing to travel: settle, and a caller watching moving() sees no animation
			return;
		}
		anim = true;
		animFrom = off;
		animTo = to;
		animFromV = v;
		animToV = goal;
		animPage = page;
		animAt = ms;
		animDur = durMs;
	}

	void Paged::goTo(int page, int64_t ms, bool animate)
	{
		if (pageCount() <= 0) return;
		if (page < 0) page = 0;
		if (page >= pageCount()) page = pageCount() - 1;
		// the Pages grid jumps as a reader flips a book to a page: open it where it was left, not at its top
		slide(page, vbase[page], ms, animate ? STEP_MS : 0);
	}

	void Paged::forward(int64_t ms)
	{
		int n = pageCount();
		if (n <= 0) return;
		int c = nearest(off);
		double vmax = vMax(c);
		// Right goes through the page before turning it: one screenful down (a part one at the page's bottom);
		// at its end, the next page opened at the top, and past the chapter's last page there is nothing
		if (v < vmax)
		{
			double b = screenful();
			double to = v + (b > 0 ? b : vmax);
			if (to > vmax) to = vmax;
			slide(c, to, ms, STEP_MS);
			return;
		}
		if (c + 1 < n) slide(c + 1, 0, ms, STEP_MS);
	}

	void Paged::back(int64_t ms)
	{
		if (pageCount() <= 0) return;
		int c = nearest(off);
		if (v > 0)
		{
			double b = screenful();
			double to = v - (b > 0 ? b : vMax(c));
			if (to < 0) to = 0;
			slide(c, to, ms, STEP_MS);
			return;
		}
		// the previous page, opened at its end so Left reads backwards smoothly
		if (c > 0) slide(c - 1, vMax(c - 1), ms, STEP_MS);
	}

	void Paged::nextPage(int64_t ms)
	{
		int c = nearest(off);
		if (c >= 0 && c + 1 < pageCount()) slide(c + 1, 0, ms, STEP_MS);	// R: straight to it, a tall one's rest unread
	}

	void Paged::prevPage(int64_t ms)
	{
		int c = nearest(off);
		if (c > 0) slide(c - 1, vMax(c - 1), ms, STEP_MS);	// L: the previous page at its bottom, as Left ends there
	}

	void Paged::touchDown(int64_t)
	{
		stopAnim();
		drags.clear();
	}

	void Paged::drag(double dx, double dy, int64_t ms)
	{
		if (pageCount() <= 0) return;
		stopAnim();
		// the row follows the finger 1:1: carried right it shows what sat to the left, which in rtl is the next
		// pages — off runs with reading order either way, so one sign flips between the modes
		off = clampD(off - dx * (rtl ? -1.0 : 1.0), center.front(), center.back());
		double vmax = vMax(nearest(off));
		if (vmax > 0)
			v = clampD(v - dy, 0, vmax);	// fingers move the content, so a downward drag walks up the page
		else
			v = 0;	// the page fits the view: there is nothing to pan, and a vertical drag on it does nothing
		drags.push_back({ms, off, v});
		// 200 ms is twice the release's window: pruning here can never starve the estimate, and a gesture's samples
		// stay small however long the finger drags
		size_t drop = 0;
		while (drop + 1 < drags.size() && drags[drop].ms < ms - 200) drop++;
		if (drop) drags.erase(drags.begin(), drags.begin() + drop);
	}

	void Paged::release(int64_t ms)
	{
		if (pageCount() <= 0)
		{
			drags.clear();
			return;
		}
		// the finger's velocity is how fast the row and the page's scroll moved in the last 100 ms before it let
		// go; a finger that rested there has no sample in that window and so no flick
		double vo = 0, vv = 0;
		size_t at = 0;
		while (at < drags.size() && drags[at].ms < ms - VELOCITY_WINDOW_MS) at++;
		if (at + 1 < drags.size())
		{
			const Drag& a = drags[at];
			const Drag& b = drags.back();
			int64_t dt = b.ms - a.ms;
			if (dt > 0)
			{
				vo = (b.off - a.off) / (double)dt * 1000;
				vv = (b.v - a.v) / (double)dt * 1000;
			}
		}
		int n = pageCount();
		int c = nearest(off);
		// which way the gesture meant to go is the axis the finger travelled further; a row held at an end stops
		// its samples moving there, so its flick can never turn past it
		if (drags.empty() || std::fabs(off - drags.front().off) >= std::fabs(v - drags.front().v))
		{
			// sideways: the nearest page, or the next or previous one when the flick is over the threshold in
			// that direction
			int t = c;
			if (vo > FLICK_PX_S && t + 1 < n) t++;
			else if (vo < -FLICK_PX_S && t > 0) t--;
			slide(t, snapV(v, vMax(t)), ms, SNAP_MS);
			return;
		}
		// up or down a tall page: snap its scroll to the nearest screenful's bound, and one bound further on a
		// fast flick
		double vmax = vMax(c);
		double to = snapV(v, vmax);
		if (vmax > 0 && screenful() > 0)
		{
			int k = (int)std::floor(clampD(v, 0, vmax) / screenful() + 0.5);
			if (vv > FLICK_PX_S && k < vSteps(vmax)) to = vBound(k + 1, vmax);	// a flick up carries down
			else if (vv < -FLICK_PX_S && k > 0) to = vBound(k - 1, vmax);		// and a flick down, back up
		}
		slide(c, to, ms, SNAP_MS);
	}

	bool Paged::step(int64_t ms)
	{
		if (!anim) return false;
		int64_t el = ms - animAt;
		if (el < 0) el = 0;
		if (el >= animDur)
		{
			// exactly the target at the end, whatever the frames did
			int page = animPage;
			double vTo = animToV;
			stopAnim();
			land(page, vTo);
			return false;
		}
		double u = (double)el / (double)animDur;
		double left = 1 - u;
		double e = 1 - left * left * left;	// ease-out cubic, as Strip's animations
		off = animFrom + (animTo - animFrom) * e;
		v = animFromV + (animToV - animFromV) * e;
		return true;
	}

	bool Paged::moving() const
	{
		return anim;
	}

	bool Paged::atEnd() const
	{
		int n = pageCount();
		if (n <= 0) return false;
		// only at rest: a turn still gliding to the last page hasn't finished it
		if (anim) return false;
		int c = nearest(off);
		return c == n - 1 && v >= vMax(c);
	}

	double Paged::vscroll() const
	{
		return v;
	}

	std::vector<int> Paged::wanted(int ahead, int behind) const
	{
		std::vector<int> out;
		int n = pageCount();
		if (n <= 0) return out;
		// the whole list is all any caller can use, and capping keeps the loops' sums in range
		if (ahead < 0) ahead = 0;
		if (behind < 0) behind = 0;
		if (ahead > n) ahead = n;
		if (behind > n) behind = n;
		int c = nearest(off);
		out.push_back(c);
		// ahead is where reading is going: the later pages, the same page numbers whatever way the row runs;
		// behind the ones before. Only a centred page is ever "current" here, so one page is enough to ask for
		for (int i = c + 1; i < n && i - c <= ahead; i++) out.push_back(i);
		for (int i = c - 1; i >= 0 && c - i <= behind; i--) out.push_back(i);
		return out;
	}
}
