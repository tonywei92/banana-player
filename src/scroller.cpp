#include "scroller.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	namespace lay
	{
		void Scroller::setExtent(double contentH, double viewH)
		{
			content = std::max(0.0, contentH);
			view = std::max(0.0, viewH);
			// a list that came back shorter: at its end at once when nothing moves it (a moving one gets there itself)
			if (mode == Mode::Still && !fingerDown) off = clampOff(off);
			if (mode == Mode::Spring) target = clampOff(target);
		}

		double Scroller::maxOffset() const { return std::max(0.0, content - view); }

		double Scroller::clampOff(double o) const { return std::clamp(o, minOffset(), std::max(minOffset(), maxOffset())); }

		// The rubber band (a phone's): past an end the content first follows at about half the finger's pace, then
		// less and less, never further than the view's height
		double Scroller::band(double pull) const
		{
			double d = std::max(1.0, view);
			return d * (1 - 1 / (pull * BAND_REACH / d + 1));
		}

		double Scroller::unband(double shown) const
		{
			double d = std::max(1.0, view);
			double s = std::min(shown, d * 0.999);
			return s / (BAND_REACH * (1 - s / d));
		}

		// ---- a finger ----

		bool Scroller::press(double y, int64_t ms)
		{
			// a moving list is caught where it is (that touch only stops it: it chooses nothing)
			bool caught = moving() && std::fabs(v) > STOP_SPEED;
			mode = Mode::Still;
			v = 0;
			fingerDown = true;
			past = caught;
			downY = y;
			downOff = off;
			samples.assign(1, {y, ms});
			if (caught) mode = Mode::Drag;
			return caught;
		}

		bool Scroller::move(double y, int64_t ms)
		{
			if (!fingerDown) return false;
			samples.push_back({y, ms});
			// (only the last moments make the lift's speed)
			size_t drop = 0;
			while (drop + 2 < samples.size() && samples[drop].ms < ms - 2 * VELOCITY_WINDOW_MS) drop++;
			if (drop) samples.erase(samples.begin(), samples.begin() + drop);
			if (!past)
			{
				if (std::fabs(y - downY) <= TAP_SLOP) return false;
				// a drag from here: the content starts with the finger, without the slop's jump
				past = true;
				mode = Mode::Drag;
				downY = y;
				downOff = off;
			}
			// where the finger would put the content with no ends, then the band past them
			double max = maxOffset();
			double pull0 = downOff < 0 ? -unband(-downOff) : downOff > max ? max + unband(downOff - max) : downOff;
			double pull = pull0 + (downY - y);
			off = pull < 0 ? -band(-pull) : pull > max ? max + band(pull - max) : pull;
			return true;
		}

		bool Scroller::release(int64_t ms)
		{
			if (!fingerDown) return false;
			bool refresh = armed();
			fingerDown = false;
			double speed = 0;
			if (past)
			{
				// how fast the finger went in its last 100 ms; one that rested there has no sample in it, and no glide
				size_t first = 0;
				while (first < samples.size() && samples[first].ms < ms - VELOCITY_WINDOW_MS) first++;
				if (first + 1 < samples.size())
				{
					const Sample& a = samples[first];
					const Sample& b = samples.back();
					if (b.ms > a.ms) speed = -(b.y - a.y) / (double)(b.ms - a.ms);
				}
				speed = std::clamp(speed, -MAX_SPEED, MAX_SPEED);
			}
			samples.clear();
			past = false;
			mode = Mode::Still;
			// a pull to refresh: the list rests with the refresh's row showing over its top
			if (refresh) holdTop = REFRESH_ROW;
			if (off < minOffset() || off > std::max(minOffset(), maxOffset())) springTo(clampOff(off), refresh ? 0 : speed, END_SPRING, ms);
			else if (std::fabs(speed) > STOP_SPEED) glide(speed, ms);
			return refresh;
		}

		void Scroller::unhold(int64_t ms)
		{
			if (holdTop <= 0) return;
			holdTop = 0;
			// (a finger on it: the drag goes on from where it is)
			if (!fingerDown && off < 0) springTo(0, mode == Mode::Still ? 0 : v, END_SPRING, ms);
		}

		// ---- keys ----

		void Scroller::reveal(double top, double h, int64_t ms)
		{
			if (fingerDown) return;
			// from where a cursor glide already goes (keys held: each step adds to it), else from here
			double ref = mode == Mode::Spring ? target : off;
			double want = ref;
			if (top < want) want = top;
			else if (top + h > want + view) want = top + h - view;
			want = clampOff(want);
			if (mode == Mode::Still && std::fabs(want - off) < 0.5) return;
			if (mode == Mode::Spring && want == target) return;
			springTo(want, mode == Mode::Still ? 0 : v, FOLLOW_SPRING, ms);
		}

		void Scroller::jump(double o)
		{
			mode = Mode::Still;
			fingerDown = past = false;
			samples.clear();
			v = 0;
			holdTop = 0;	// (a new list: no refresh's row over it)
			off = clampOff(o);
		}

		// ---- the motion ----

		void Scroller::glide(double speed, int64_t ms)
		{
			mode = Mode::Glide;
			from = off;
			v0 = v = speed;
			at = ms;
		}

		void Scroller::springTo(double to, double speed, double k, int64_t ms)
		{
			mode = Mode::Spring;
			from = off;
			v0 = v = speed;
			target = to;
			spring = k;
			at = ms;
		}

		bool Scroller::step(int64_t ms)
		{
			if (mode == Mode::Glide)
			{
				// the speed falls as exp(-t / tau); the way gone is its integral, so a 5 ms and a 50 ms stepper are
				// at the same place at the same time
				double t = std::max<int64_t>(0, ms - at);
				bool done = false;
				double stopT = TAU_MS * std::log(std::fabs(v0) / STOP_SPEED);
				if (t >= stopT) { t = stopT; done = true; }
				double pos = from + v0 * TAU_MS * (1 - std::exp(-t / TAU_MS));
				double max = std::max(minOffset(), maxOffset());
				if (pos < minOffset() || pos > max)
				{
					// past an end: from the moment it crossed, the spring brings it back (with the speed it had there,
					// so it runs on a little before it turns: the bounce)
					double edge = pos < minOffset() ? minOffset() : max;
					double share = 1 - (edge - from) / (v0 * TAU_MS);
					double tc = share > 0 ? -TAU_MS * std::log(share) : 0;
					off = edge;
					springTo(edge, v0 * std::exp(-tc / TAU_MS), END_SPRING, at + (int64_t)std::lround(tc));
					return step(ms);
				}
				off = pos;
				v = v0 * std::exp(-t / TAU_MS);
				if (done) { mode = Mode::Still; v = 0; }
				return moving();
			}
			if (mode == Mode::Spring)
			{
				// critically damped toward the target: e(t) = (e0 + (v0 + k e0) t) exp(-k t), never past it twice
				double t = std::max<int64_t>(0, ms - at);
				double k = spring;
				double e0 = from - target, b = v0 + k * e0;
				double decay = std::exp(-k * t);
				double e = (e0 + b * t) * decay;
				v = (b - k * (e0 + b * t)) * decay;
				off = target + e;
				if (std::fabs(e) < 0.25 && std::fabs(v) < STOP_SPEED)
				{
					off = target;
					v = 0;
					mode = Mode::Still;
				}
				return moving();
			}
			return false;
		}

		// ---- rows ----

		int Scroller::rowAt(double y, double viewY, double viewH, double rowH, int n) const
		{
			if (rowH <= 0 || y < viewY || y >= viewY + viewH) return -1;
			int i = (int)std::floor((y - viewY + off) / rowH);
			return i >= 0 && i < n ? i : -1;
		}

		int Scroller::firstRow(double rowH) const
		{
			return rowH > 0 ? std::max(0, (int)std::floor(off / rowH)) : 0;
		}

		int Scroller::rowsTouched(double rowH, int n) const
		{
			if (rowH <= 0) return 0;
			int first = firstRow(rowH);
			int end = std::min(n, (int)std::ceil((off + view) / rowH));
			return std::max(0, end - first);
		}
	}
}
