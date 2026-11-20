// Ui::SmoothList (ui_smoothlist.h): a list's rows drawn once into textures and placed at the scroll's offset by the
// device pixel, over a page drawn whole each frame. The motion is lay::Scroller's.
#include "ui_smoothlist.h"
#include "ui_internal.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	namespace
	{
		double msSince(uint64_t t0)
		{
			return (SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
		}

		// rows a few beyond the view keep their slots, so a list moved back and forth draws nothing again
		constexpr int SPARE_SLOTS = 4;
	}

	// ---- the frame's list ----

	void Ui::SmoothList::draw(Spec spec)
	{
		const uint64_t t0 = SDL_GetPerformanceCounter();
		last = std::move(spec);
		sc.setExtent((double)last.count * last.rowH, last.view.h);
		frame++;
		const int rowH = std::max(1, last.rowH);
		const int first = sc.firstRow(rowH);
		const int end = first + sc.rowsTouched(rowH, last.count);
		if (!SDL_RenderTargetSupported(ui.ren) || !last.draw)
		{
			drawDirect(first, end);
			ui.parts.list += msSince(t0);
			return;
		}
		// the rows whose look changed (or that had no slot) drawn into their slots first: each is a target switch
		SDL_Texture* target = SDL_GetRenderTarget(ui.ren);
		bool switched = false;
		std::vector<std::pair<int, Slot*>> placed;
		for (int i = first; i < end; i++)
		{
			std::string key = last.key ? last.key(i) : "";
			Slot* s = slotFor(i, first, end);
			if (!s) break;
			// a row that moves by itself ("" key) is drawn again every frame
			bool redraw = s->row != i || s->key != key || key.empty();
			if (redraw)
			{
				switched = true;
				if (!fillSlot(*s, i, key)) { s->row = -1; continue; }
				ui.parts.rowsDrawn++;
			}
			s->used = frame;
			placed.push_back({i, s});
		}
		if (switched) SDL_SetRenderTarget(ui.ren, target);
		if ((int)placed.size() < end - first)
		{
			// (a slot couldn't be made: every row drawn where it is, as without targets)
			drawDirect(first, end);
			ui.parts.list += msSince(t0);
			return;
		}
		// each slot at its row's place, cut at the view's edges
		SDL_Rect clip = ui.devRect(last.view);
		SDL_RenderSetClipRect(ui.ren, &clip);
		const int x = (int)std::lround(last.view.x * sx);
		for (auto& [i, s] : placed)
		{
			SDL_Rect d{x, (int)std::lround((last.view.y + (double)i * rowH - sc.offset()) * sy), s->w, s->h};
			SDL_RenderCopy(ui.ren, s->tex, nullptr, &d);
		}
		SDL_RenderSetClipRect(ui.ren, nullptr);
		ui.parts.list += msSince(t0);
	}

	Ui::SmoothList::Slot* Ui::SmoothList::slotFor(int i, int first, int end)
	{
		for (auto& s : slots)
			if (s->row == i) return s.get();
		// a new one while the pool is under the rows shown and the spares; else the one out of sight longest
		if ((int)slots.size() < end - first + SPARE_SLOTS)
		{
			slots.push_back(std::make_unique<Slot>());
			return slots.back().get();
		}
		Slot* pick = nullptr;
		for (auto& s : slots)
		{
			bool shown = s->row >= first && s->row < end;
			if (!shown && (!pick || s->used < pick->used)) pick = s.get();
		}
		return pick;
	}

	bool Ui::SmoothList::fillSlot(Slot& s, int i, const std::string& key)
	{
		// the slot as big as a row in device pixels (made again when the view's width changed)
		SDL_Rect size = ui.devRect({0, 0, last.view.w, last.rowH});
		if (s.tex && (s.w != size.w || s.h != size.h))
		{
			SDL_DestroyTexture(s.tex);
			s.tex = nullptr;
		}
		if (!s.tex)
		{
			s.tex = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, size.w, size.h);
			if (!s.tex) return false;
			s.w = size.w;
			s.h = size.h;
			// A row is drawn onto clear pixels, so its colours come out multiplied by their alpha (a rounded corner's
			// edge, a translucent fill): placed with the premultiplied blend they land as if drawn on the frame
			// itself. A renderer without custom blends gets the plain one (those edges a shade darker).
			static const SDL_BlendMode PREMULTIPLIED = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE,
				SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE,
				SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
			bool ok = SDL_SetTextureBlendMode(s.tex, PREMULTIPLIED) == 0;
			if (!ok) SDL_SetTextureBlendMode(s.tex, SDL_BLENDMODE_BLEND);
			if (!blendChecked)
			{
				blendChecked = true;
				fprintf(stderr, "smooth list: rows %dx%d px, %s blend\n", size.w, size.h, ok ? "premultiplied" : "plain");
			}
		}
		if (SDL_SetRenderTarget(ui.ren, s.tex) != 0) return false;
		SDL_SetRenderDrawBlendMode(ui.ren, SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(ui.ren, 0, 0, 0, 0);
		SDL_RenderClear(ui.ren);
		last.draw(i, {0, 0, last.view.w, last.rowH});
		s.row = i;
		s.key = key;
		return true;
	}

	void Ui::SmoothList::drawDirect(int first, int end)
	{
		SDL_Rect clip = ui.devRect(last.view);
		SDL_RenderSetClipRect(ui.ren, &clip);
		for (int i = first; i < end; i++)
		{
			last.draw(i, rowBox(i));
			ui.parts.rowsDrawn++;
		}
		SDL_RenderSetClipRect(ui.ren, nullptr);
	}

	void Ui::SmoothList::release()
	{
		for (auto& s : slots)
			if (s->tex) SDL_DestroyTexture(s->tex);
		slots.clear();
	}

	// ---- the finger and the cursor ----

	bool Ui::SmoothList::press(float y) { return sc.press(y, ui.touchMs); }

	bool Ui::SmoothList::move(float y) { return sc.move(y, ui.touchMs); }

	void Ui::SmoothList::lift()
	{
		sc.release(ui.touchMs);
		if (!sc.moving()) endMotion();	// (let go still: no glide follows)
	}

	void Ui::SmoothList::reveal(int i, Rect view, int rowH, int count, bool glide)
	{
		sc.setExtent((double)count * rowH, view.h);
		if (glide)
		{
			sc.reveal((double)i * rowH, rowH, SDL_GetTicks());
			return;
		}
		// at once: the least move that shows the whole row
		double off = sc.offset(), top = (double)i * rowH;
		if (top < off) off = top;
		else if (top + rowH > off + view.h) off = top + rowH - view.h;
		sc.jump(off);
	}

	// ---- where things are ----

	int Ui::SmoothList::rowAt(int x, int y) const
	{
		if (x < last.view.x || x >= last.view.x + last.view.w) return -1;
		return sc.rowAt(y, last.view.y, last.view.h, last.rowH, last.count);
	}

	Ui::Rect Ui::SmoothList::rowBox(int i) const
	{
		return {last.view.x, last.view.y + i * last.rowH - (int)std::lround(sc.offset()), last.view.w, last.rowH};
	}

	// ---- Ui's side ----

	void Ui::SmoothList::step(uint32_t ms)
	{
		bool was = sc.moving();
		sc.step(ms);
		if (was && !sc.moving() && !sc.dragging()) endMotion();
	}

	int Ui::SmoothList::shownPx() const { return (int)std::lround(sc.offset() * sy); }

	void Ui::SmoothList::noteFrame(const FrameParts& f)
	{
		// the frames drawn while the list moves (a finger, a glide): where a fling's frames go on the handheld
		// and those that came late
		if (!sc.moving() && !sc.dragging()) return;
		const uint64_t now = SDL_GetPerformanceCounter();
		const double gap = motion.frames ? (now - motion.lastAt) * 1000.0 / (double)SDL_GetPerformanceFrequency() : 0;
		if (!motion.frames) motion.since = now;
		if (gap > 25 && std::fabs(sc.speed()) > 0.1) motion.late++;
		motion.lastAt = now;
		motion.frames++;
		motion.rows += f.rowsDrawn;
		motion.misses += f.textMisses;
		motion.ms += f.total();
		motion.listMs += f.list;
		motion.maxMs = std::max(motion.maxMs, f.total());
	}

	void Ui::SmoothList::endMotion()
	{
		// one line when the list comes to rest (a glide of a few frames says nothing)
		const Motion& m = motion;
		double span = (m.lastAt - m.since) * 1000.0 / (double)SDL_GetPerformanceFrequency();
		if (m.frames >= 5)
			fprintf(stderr, "timing: list moved: %d frames in %.0f ms (%.0f a second), %.2f ms each (list %.2f), max %.2f, "
				"%d late (over 25 ms apart while fast); %d rows drawn, %d text misses\n", m.frames, span,
				span > 0 ? (m.frames - 1) * 1000.0 / span : 0.0, m.ms / m.frames, m.listMs / m.frames, m.maxMs, m.late,
				m.rows, m.misses);
		motion = Motion{};
	}
}
