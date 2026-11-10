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
		hbase.assign(aspect.size(), 0);
		rebuild();
		// a chapter starts at its first page, at its top and start edge
		off = center.empty() ? 0 : center.front();
		v = 0;
		h = 0;
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

	void Paged::setZoom(double z, double fx, double fy)
	{
		double to = z;
		if (!(to >= 1)) to = 1;	// also catches a NaN
		if (to > 8) to = 8;
		if (to == zscale) return;
		int c = nearest(off);
		if (c < 0)
		{
			zscale = to;
			rebuild();
			return;
		}
		double wasW = wide[c], wasH = pageHeight(c), oldH = h, oldV = v;
		zscale = to;
		rebuild();
		// off runs with the centres, so it moves with the page shown; the page stays the one read
		off = center[c];
		// the content under the focus keeps its place. Across, the reading-order offset runs from the start edge
		// (the left for ltr, the right for rtl, as h itself does); down, a page shorter than the view is centred,
		// a taller one top-aligned
		double kx = rtl ? viewW - fx : fx;
		double newW = wide[c], newH = pageHeight(c);
		if (wasW > 0) h = clampD((oldH + kx) * newW / wasW - kx, 0, hMax(c));
		double topY = wasH <= viewH ? (viewH - wasH) * 0.5 : -oldV;
		double fracY = wasH > 0 ? (fy - topY) / wasH : 0;
		v = newH <= viewH ? 0 : clampD(fracY * newH - fy, 0, vMax(c));
		vbase[c] = v;
		hbase[c] = h;
		stopAnim();
		drags.clear();
		// a neighbour's remembered place may no longer fit too
		for (size_t i = 0; i < vbase.size(); i++)
		{
			vbase[i] = clampD(vbase[i], 0, vMax((int)i));
			hbase[i] = clampD(hbase[i], 0, hMax((int)i));
		}
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
			// the bottom and scrolls inside; fit Fill takes the larger scale of the two (max(viewW/w, viewH/h)),
			// covering the view, and whichever way the page overshoots is the axis that crops and pans
			if (fit == FitWidth) wide[i] = viewW;
			else if (fit == FitFill) wide[i] = std::max((double)viewW, (double)viewH / aspect[i]);
			else wide[i] = std::min((double)viewW, (double)viewH / aspect[i]);
			wide[i] *= zscale;
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
			animToH = clampD(animToH, 0, hMax(animPage));
		}
		// and a page that no longer overshoots the view cannot keep its scroll or its pan, nor the pages waiting
		// their turn
		v = clampD(v, 0, vMax(nearest(off)));
		h = clampD(h, 0, hMax(nearest(off)));
		for (size_t i = 0; i < vbase.size(); i++)
		{
			vbase[i] = clampD(vbase[i], 0, vMax((int)i));
			hbase[i] = clampD(hbase[i], 0, hMax((int)i));
		}
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

	double Paged::hMax(int i) const
	{
		if (i < 0 || i >= pageCount()) return 0;
		double over = wide[i] - viewW;
		return over > 0 ? over : 0;
	}

	double Paged::screenful() const
	{
		return 0.9 * viewH;	// as Strip: the next step shows a sliver of what came before
	}

	double Paged::screenW() const
	{
		return 0.9 * viewW;	// the same sliver from one column of a wide page to the next
	}

	int Paged::vSteps(double vmax) const
	{
		if (vmax <= 0 || screenful() <= 0) return 0;
		// the last step may be a part screenful: it ends at the page's bottom, so turning follows no later
		return (int)std::ceil(vmax / screenful());
	}

	int Paged::hSteps(double hmax) const
	{
		if (hmax <= 0 || screenW() <= 0) return 0;
		return (int)std::ceil(hmax / screenW());
	}

	double Paged::vBound(int k, double vmax) const
	{
		if (k <= 0) return 0;
		int steps = vSteps(vmax);
		if (k >= steps) return vmax;
		double y = k * screenful();
		return y < vmax ? y : vmax;
	}

	double Paged::hBound(int k, double hmax) const
	{
		if (k <= 0) return 0;
		int steps = hSteps(hmax);
		if (k >= steps) return hmax;
		double x = k * screenW();
		return x < hmax ? x : hmax;
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

	double Paged::snapH(double x, double hmax) const
	{
		if (hmax <= 0) return 0;
		if (screenW() <= 0) return clampD(x, 0, hmax);
		int k = (int)std::floor(clampD(x, 0, hmax) / screenW() + 0.5);
		double y = hBound(k, hmax);
		double next = hBound(k + 1, hmax);
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
		// the shown page's pan carries the whole row, so pages keep touching while the view walks across a wide
		// one and its neighbours wait at its reading edges; the pan runs with the reading order, so rtl counts it
		// from the page's far edge
		double shift = 0;
		if (shown >= 0 && wide[shown] > viewW)
		{
			double d = wide[shown] - viewW;
			shift = d * 0.5 - (rtl ? d - clampD(h, 0, d) : clampD(h, 0, d));
		}
		for (int i = 0; i < pageCount(); i++)
		{
			// at rest page i's centre is the view's; the drag carries the row, so x may go negative at either end
			double cx = viewW * 0.5 + (center[i] - off) * m + shift;
			double x = cx - wide[i] * 0.5;
			double ph = pageHeight(i);
			if (x >= viewW || x + wide[i] <= 0 || wide[i] <= 0 || ph <= 0) continue;	// not in the view
			// a page smaller than the view is centred; a taller one only comes at fit Width or Fill and scrolls
			// inside, and only the page being shown has scrolled
			double y = ph <= viewH ? (viewH - ph) * 0.5 : -(i == shown ? clampD(v, 0, ph - viewH) : 0.0);
			out.push_back({i, x, y, wide[i], ph});
		}
		return out;
	}

	void Paged::stopAnim()
	{
		anim = false;	// the finger or a new step takes the row where the glide had left it
	}

	void Paged::land(int page, double vTo, double hTo)
	{
		int n = pageCount();
		if (n <= 0) return;
		if (page < 0) page = 0;
		if (page >= n) page = n - 1;
		// exactly on the page: its centre at the view's middle, and it keeps where it stands scrolled and panned to
		off = center[page];
		v = clampD(vTo, 0, vMax(page));
		h = clampD(hTo, 0, hMax(page));
		vbase[page] = v;
		hbase[page] = h;
	}

	void Paged::slide(int page, double vTo, double hTo, int64_t ms, int64_t durMs)
	{
		int n = pageCount();
		if (n <= 0) return;
		if (page < 0) page = 0;
		if (page >= n) page = n - 1;
		stopAnim();
		drags.clear();
		double to = center[page];
		double goal = clampD(vTo, 0, vMax(page));
		double goalH = clampD(hTo, 0, hMax(page));
		if (durMs <= 0 || (off == to && v == goal && h == goalH))
		{
			land(page, goal, goalH);	// nothing to travel: settle, and a caller watching moving() sees no animation
			return;
		}
		anim = true;
		animFrom = off;
		animTo = to;
		animFromV = v;
		animToV = goal;
		animFromH = h;
		animToH = goalH;
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
		slide(page, vbase[page], hbase[page], ms, animate ? STEP_MS : 0);
	}

	void Paged::forward(int64_t ms)
	{
		int n = pageCount();
		if (n <= 0) return;
		int c = nearest(off);
		double vmax = vMax(c);
		// Right goes through the page in reading order: one screenful down the current column (a part one at the
		// page's bottom), then one screen width across and back to the top — the pan runs with the reading order,
		// so this is leftwards inside a rtl spread without further mirroring; at the page's end, the next page
		// opened at its top and start edge, and past the chapter's last page there is nothing
		if (v < vmax)
		{
			double b = screenful();
			double to = v + (b > 0 ? b : vmax);
			if (to > vmax) to = vmax;
			slide(c, to, h, ms, STEP_MS);
			return;
		}
		double hmax = hMax(c);
		if (h < hmax)
		{
			double b = screenW();
			double to = h + (b > 0 ? b : hmax);
			if (to > hmax) to = hmax;
			slide(c, 0, to, ms, STEP_MS);
			return;
		}
		if (c + 1 < n) slide(c + 1, 0, 0, ms, STEP_MS);
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
			slide(c, to, h, ms, STEP_MS);
			return;
		}
		// at the top: one column back up the reading order, and to the bottom of that one
		if (h > 0)
		{
			double b = screenW();
			double to = h - (b > 0 ? b : hMax(c));
			if (to < 0) to = 0;
			slide(c, vMax(c), to, ms, STEP_MS);
			return;
		}
		// the previous page, opened at its end (its bottom row's far column) so Left reads backwards smoothly
		if (c > 0) slide(c - 1, vMax(c - 1), hMax(c - 1), ms, STEP_MS);
	}

	void Paged::nextPage(int64_t ms)
	{
		int c = nearest(off);
		// R: straight to it, the rest of a tall or wide one unread, opened at its top and start edge
		if (c >= 0 && c + 1 < pageCount()) slide(c + 1, 0, 0, ms, STEP_MS);
	}

	void Paged::prevPage(int64_t ms)
	{
		int c = nearest(off);
		// L: the previous page at its end, as Left ends there
		if (c > 0) slide(c - 1, vMax(c - 1), hMax(c - 1), ms, STEP_MS);
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
		// pages — off and the in-page pan both run with reading order, so one sign flips between the modes
		double f = -dx * (rtl ? -1.0 : 1.0);
		// a page wider than the view pans under the finger first, as photo viewers' do; only what its pan
		// couldn't take carries the row on
		double panned = 0;
		double hmax = hMax(nearest(off));
		if (hmax > 0)
		{
			double to = clampD(h + f, 0, hmax);
			panned = to - h;
			h = to;
		}
		else
			h = 0;
		off = clampD(off + f - panned, center.front(), center.back());
		int now = nearest(off);
		double vmax = vMax(now);
		if (vmax > 0)
			v = clampD(v - dy, 0, vmax);	// fingers move the content, so a downward drag walks up the page
		else
			v = 0;	// the page fits the view: there is nothing to pan, and a vertical drag on it does nothing
		// the row may have arrived at a page with a different room to pan in
		hmax = hMax(now);
		h = hmax > 0 ? clampD(h, 0, hmax) : 0;
		drags.push_back({ms, off, v, h});
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
		// the finger's velocity is how fast the row and the page's offsets moved in the last 100 ms before it let
		// go; a finger that rested there has no sample in that window and so no flick
		double vo = 0, vv = 0, vh = 0;
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
				vh = (b.h - a.h) / (double)dt * 1000;
			}
		}
		int n = pageCount();
		int c = nearest(off);
		// which way the gesture meant to go is the axis the finger travelled further; a row held at an end stops
		// its samples moving there, so its flick can never turn past it
		double doff = drags.empty() ? 0 : std::fabs(off - drags.front().off);
		double dv = drags.empty() ? 0 : std::fabs(v - drags.front().v);
		double dh = drags.empty() ? 0 : std::fabs(h - drags.front().h);
		if (drags.empty() || (doff >= dv && doff >= dh))
		{
			// sideways: the nearest page, or the next or previous one when the flick is over the threshold in
			// that direction; the row centring snaps the page's offsets to their bounds
			int t = c;
			if (vo > FLICK_PX_S && t + 1 < n) t++;
			else if (vo < -FLICK_PX_S && t > 0) t--;
			slide(t, snapV(v, vMax(t)), snapH(h, hMax(t)), ms, SNAP_MS);
			return;
		}
		if (dh >= dv)
		{
			// across a wide page: the same snap the vertical one makes, along the reading order the pan runs in
			double hmax = hMax(c);
			double to = snapH(h, hmax);
			if (hmax > 0 && screenW() > 0)
			{
				int k = (int)std::floor(clampD(h, 0, hmax) / screenW() + 0.5);
				if (vh > FLICK_PX_S && k < hSteps(hmax)) to = hBound(k + 1, hmax);	// a flick on carries on
				else if (vh < -FLICK_PX_S && k > 0) to = hBound(k - 1, hmax);		// and a flick back, back
			}
			slide(c, snapV(v, vMax(c)), to, ms, SNAP_MS);
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
		slide(c, to, snapH(h, hMax(c)), ms, SNAP_MS);
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
			double vTo = animToV, hTo = animToH;
			stopAnim();
			land(page, vTo, hTo);
			return false;
		}
		double u = (double)el / (double)animDur;
		double left = 1 - u;
		double e = 1 - left * left * left;	// ease-out cubic, as Strip's animations
		off = animFrom + (animTo - animFrom) * e;
		v = animFromV + (animToV - animFromV) * e;
		h = animFromH + (animToH - animFromH) * e;
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
		return c == n - 1 && v >= vMax(c) && h >= hMax(c);
	}

	double Paged::vscroll() const
	{
		return v;
	}

	double Paged::hscroll() const
	{
		return h;
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
