#pragma once
// A list that scrolls as a phone's does (docs/notes/2026-11-21-smooth-scroll.md): the content follows the finger by
// the pixel, glides on after the lift and slows by friction, gives a little past either end (a rubber band) and
// springs back; the D-pad's cursor is kept in view by a short glide. Pure maths, no window: positions in the panel's
// units (fractions kept, so the 1.6x handheld panel moves by its own pixels), time in real milliseconds (a slow frame
// moves the list further, never slower). Ui::SmoothList (ui_smoothlist.h) draws a list with one. Tests: core_test.
#include <cstdint>
#include <vector>

namespace bp
{
	namespace lay
	{
		class Scroller
		{
		public:
			// a finger's drift before it is a drag (a tap moves less than this)
			static constexpr double TAP_SLOP = 8;
			// the glide: its speed falls by e every TAU_MS; under STOP_SPEED (units per ms) it is still
			static constexpr double TAU_MS = 325;
			static constexpr double STOP_SPEED = 0.02;
			static constexpr double MAX_SPEED = 6;		// units per ms: a harder flick only races past blank rows
			static constexpr int64_t VELOCITY_WINDOW_MS = 100;	// the finger's last 100 ms make the lift's speed
			// the springs (critically damped, so they never bounce twice): back from past an end, and to a cursor
			static constexpr double END_SPRING = 0.03;	// per ms: ~150 ms back from an end
			static constexpr double FOLLOW_SPRING = 0.025;
			// how far past an end a finger can pull: the band's give tends to this share of the view
			static constexpr double BAND_REACH = 0.55;
			// pull to refresh (a list that has it: setPull): the band pulled this far past the top, a lift refreshes;
			// the list then rests that far down (REFRESH_ROW: the "Refreshing" row) until unhold()
			static constexpr double PULL_REFRESH = 64;
			static constexpr double REFRESH_ROW = 44;

			// the content's height and the view's (a shorter content can't scroll; the offset is kept inside)
			void setExtent(double content, double view);
			double maxOffset() const;

			// ---- a finger ----
			// down: a glide under way is caught (stopped where it is). true when it was moving: that touch is not a tap
			bool press(double y, int64_t ms);
			// a move: true once the finger has gone past TAP_SLOP (from then on, the content follows it)
			bool move(double y, int64_t ms);
			// the lift: a glide at the finger's last speed, or the spring back from past an end. true: it was a pull
			// to refresh (pulled past PULL_REFRESH at the top of a list with setPull on): the list holds REFRESH_ROW
			// down until unhold()
			bool release(int64_t ms);
			bool dragging() const { return mode == Mode::Drag; }
			bool held() const { return fingerDown; }

			// ---- pull to refresh ----
			void setPull(bool on) { pullOn = on; }
			double pulled() const { return off < 0 ? -off : 0; }	// how far the top shows past its end (the band)
			bool armed() const { return pullOn && fingerDown && past && pulled() >= PULL_REFRESH; }	// a lift refreshes
			bool holding() const { return holdTop > 0; }
			void unhold(int64_t ms);	// the refresh's row goes: back to the top

			// ---- keys ----
			// keep [top, top + h) in view (the cursor's row): a short glide there, nothing when it already shows
			void reveal(double top, double h, int64_t ms);
			// to this offset at once (a new list, a view changed): nothing moving
			void jump(double offset);

			// advance a glide or a spring to `ms`; true while something still moves
			bool step(int64_t ms);
			bool moving() const { return mode == Mode::Glide || mode == Mode::Spring; }
			double offset() const { return off; }
			double speed() const { return v; }	// units per ms, + scrolls down the list

			// the row under a finger at `y` (rows `rowH` apart from `viewY`, the list scrolled to offset()): -1 when
			// outside the view or past the last of n
			int rowAt(double y, double viewY, double viewH, double rowH, int n) const;
			// the first row any part of which shows, and how many rows touch the view
			int firstRow(double rowH) const;
			int rowsTouched(double rowH, int n) const;

		private:
			enum class Mode { Still, Drag, Glide, Spring };
			Mode mode = Mode::Still;
			double content = 0, view = 0;
			double off = 0, v = 0;
			// a drag: where the finger and the content were when it went down, and its recent samples
			bool fingerDown = false, past = false;
			bool pullOn = false;
			double holdTop = 0;	// a refresh's rest: the offset's least is -holdTop
			double downY = 0, downOff = 0;
			struct Sample { double y; int64_t ms; };
			std::vector<Sample> samples;
			// a glide or a spring: where it started, its speed, its target (a spring's), and when
			double from = 0, v0 = 0, target = 0, spring = END_SPRING;
			int64_t at = 0, last = 0;

			double band(double past) const;		// a finger `past` beyond an end: how far the content goes
			double unband(double shown) const;	// the inverse: what pull shows that far
			double minOffset() const { return -holdTop; }
			double clampOff(double o) const;
			void glide(double speed, int64_t ms);
			void springTo(double to, double speed, double k, int64_t ms);
		};

		// A finger's lift on the text reader (Docs' TXT/EPUB view): whether it glides on, and how fast. Stricter than
		// a list's: a drag that ends at a reading pace stops where the finger lifts; only a deliberate flick glides,
		// at a share of the finger's speed. Positions in the panel's units, time in ms.
		class Flick
		{
		public:
			static constexpr double MIN_SPEED = 0.9;	// units per ms over the last 100 ms: a slower lift doesn't glide
			static constexpr double GAIN = 0.6;	// the glide starts at this share of the finger's speed
			static constexpr int64_t REST_MS = 40;	// a finger still this long before the lift: no glide
			static constexpr double TAU_MS = 250;	// the text's glide: its speed falls by e every TAU_MS (a list's: 325)

			void press(double y, int64_t ms);
			void move(double y, int64_t ms);
			// the glide's start speed (units per ms, the content's way: positive when the finger went up), 0 for none
			double lift(int64_t ms);

		private:
			struct Sample { double y; int64_t ms; };
			std::vector<Sample> samples;
		};
	}
}
