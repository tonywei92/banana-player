// A list that scrolls as a phone's does (docs/notes/2026-11-21-smooth-scroll.md): the shared component a page's rows
// are drawn and touched through. The motion is lay::Scroller's (scroller.h, pure maths); this is its drawing.
//
// - Rows are drawn once into textures of their own (a "slot" per row, a few beyond the view kept for the way back)
//   and only drawn again when what they show changes: the page says what a row looks like as a key (`Spec::key`),
//   and the same key is the same pixels. A frame then only places the slots at the offset, by the device pixel.
// - Every frame is drawn whole (the page, then the slots placed over it): no frame is kept in a texture and copied
//   into another (that path flipped and flickered on the handheld's GLES2; 2026-11-21-smooth-scroll.md).
// - Ui::draw steps the glide each frame and the loop comes round each 16 ms while it moves (Ui::glideWaitMs).
//
// To put a list on it: the area owns one, returns it from Area::smoothList() while that list shows, calls draw()
// with a Spec from its draw(), and hands its finger to press / move / release and its cursor to reveal().
#pragma once
#include "ui.h"
#include "scroller.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bp
{
	struct Ui::SmoothList
	{
		explicit SmoothList(Ui& u) : ui(u) {}
		~SmoothList() { release(); }
		SmoothList(const SmoothList&) = delete;
		SmoothList& operator=(const SmoothList&) = delete;

		// ---- what the list is (the area fills one each time it draws) ----
		struct Spec
		{
			Rect view{};		// the rows' window on the panel: rows are cut at its edges
			int rowH = 60;		// a row's pitch (its box is rowH tall, a gap included)
			int count = 0;
			// what row i looks like now: the same key, the same pixels (its slot is used again). "" : a row that
			// moves by itself (a marquee), drawn again every frame
			std::function<std::string(int)> key;
			// row i drawn into `box` (view.w x rowH, at the slot's top left)
			std::function<void(int, Rect)> draw;
			// pull to refresh (null: the list has none): a finger pulling the top down past lay::Scroller's
			// PULL_REFRESH shows "Release to refresh"; its lift calls this (main's player) and the list rests a
			// "Refreshing" row down for REFRESH_HOLD_MS
			std::function<void(Player&)> refresh;
		};
		static constexpr uint32_t REFRESH_HOLD_MS = 700;
		// the list in the page's frame: its extent set, the rows shown drawn into slots where they changed, placed
		void draw(Spec spec);

		// ---- the finger (y in panel units, fractions kept; the event's time from ui.touchMs) ----
		// true: the touch caught a moving list (it stops it, and chooses nothing)
		bool press(float y);
		bool move(float y);		// true once it is a drag (past the slop): the content follows from then
		void lift();			// a glide at the finger's speed, or the band's spring back (or a pull's refresh)
		bool dragging() const { return sc.dragging(); }

		// ---- the cursor ----
		// keep row i of a list of `count` rows rowH apart in `view`: a short glide there, or there at once (`glide`
		// false: a page opened on it). The list's geometry is passed: a key can come before the list's next draw
		void reveal(int i, Rect view, int rowH, int count, bool glide = true);
		void top() { sc.jump(0); }		// a new list: at its top, nothing moving
		// true once after its slots were let go (the page left and came back, the theme changed) and when new: the
		// area puts its cursor back in view then (reveal(..., false)), a list shared by pages or groups at its top
		bool takeFresh() { bool f = fresh; fresh = false; return f; }

		// ---- where things are ----
		// the row under (x, y) on the panel (-1: outside the view, or past the last row)
		int rowAt(int x, int y) const;
		// row i's box on the panel at the offset now (whole units: what a touch tests)
		Rect rowBox(int i) const;
		const Spec& spec() const { return last; }
		bool moving() const { return sc.moving() || holdUntil != 0; }	// (a refresh's row held: the loop comes round)

		// ---- Ui's side ----
		void step(uint32_t ms);			// the glide to now (Ui::draw, before the signature)
		int shownPx() const;			// the offset in device pixels: Ui::draw's "did it move"
		void release();					// every texture let go (the page left, the theme changed)
		void noteFrame(const FrameParts& f);	// a frame's cost, for the glide's timing line

		Ui& ui;
		lay::Scroller sc;

	private:
		Spec last;
		bool fresh = true;
		uint32_t holdUntil = 0;	// a refresh's row shows until then (0: none)
		void drawPullRow();		// "Pull to refresh" / "Release to refresh" / "Refreshing" in the room over the top
		struct Slot
		{
			SDL_Texture* tex = nullptr;
			int w = 0, h = 0;	// its size in device pixels
			int row = -1;		// the row it holds (-1: none)
			std::string key;	// that row's look when it was drawn
			uint32_t used = 0;	// the frame it was last placed in
		};
		std::vector<std::unique_ptr<Slot>> slots;
		uint32_t frame = 0;
		bool blendChecked = false, premultiplied = false;

		// the slot row i is in, or one to draw it into (a row's out of sight, the longest unplaced; null: none)
		Slot* slotFor(int i, int first, int end);
		bool fillSlot(Slot& s, int i, const std::string& key);
		void drawDirect(int first, int end);	// no render targets: each row drawn where it is, by the unit

		// the glide's timing line (one per fling, when it settles): frames, their cost, the rows drawn afresh
		struct Motion
		{
			uint64_t since = 0, lastAt = 0;	// (performance counter)
			int frames = 0, rows = 0, misses = 0, late = 0;
			double ms = 0, maxMs = 0, listMs = 0;
		} motion;
		void endMotion();	// the line, then a new count
	};
}
