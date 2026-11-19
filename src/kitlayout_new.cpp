// The kit-new block of kitlayout.h: where the Section rail, the Preview pane, the Book bar, the away-from-normal dot
// and the Screen-off warning put their pieces (docs/redesign/components.md). Pure numbers, tested in core_test.cpp
// (testKitNew). The 2026-11-19 pieces are here too: a page's line, Search's field and result row, a Music/Videos row
// and a Books cover tile (testKitLayoutLists).
#include "kitlayout.h"
#include <algorithm>

namespace bp
{
	namespace lay
	{
		// ---- Section rail ----

		Rail rail(int sections, int h, int scroll)
		{
			Rail r;
			r.area = {0, 0, RAIL_W, h};
			const int x = (RAIL_W - 1 - RAIL_CELL_W + 1) / 2;	// centred left of the rule
			r.search = {x, RAIL_PAD, RAIL_CELL_W, RAIL_SEARCH_H};
			r.settings = {x, h - RAIL_PAD - RAIL_CELL_H, RAIL_CELL_W, RAIL_CELL_H};
			r.capStart = {0, r.settings.y - RAIL_GAP - RAIL_CAP_H, RAIL_W - 1, RAIL_CAP_H};
			r.view = {0, r.search.y + r.search.h + RAIL_GAP, RAIL_W - 1,
				std::max(0, r.capStart.y - RAIL_GAP - (r.search.y + r.search.h + RAIL_GAP))};
			// seven sections all in sight at 47; six or fewer take the roomier 56
			int n = std::max(0, sections);
			r.cellH = n >= 7 ? RAIL_CELL_H7 : RAIL_CELL_H;
			int content = n * r.cellH + std::max(0, n - 1) * RAIL_GAP;
			r.maxScroll = std::max(0, content - r.view.h);
			r.scroll = std::clamp(scroll, 0, r.maxScroll);
			for (int i = 0; i < n; i++)
				r.cell.push_back({x, r.view.y - r.scroll + i * (r.cellH + RAIL_GAP), RAIL_CELL_W, r.cellH});
			return r;
		}

		int railAt(const Rail& r, int x, int y)
		{
			// Settings has its own box, pinned below the view; a section's touch reaches the strip's sides (the units
			// either side of its cell are still the rail), but only inside the view (Search and the START cap are the
			// shell's own keys)
			if (r.settings.hit(x, y)) return (int)r.cell.size();
			if (!r.view.hit(x, y)) return -1;
			for (size_t i = 0; i < r.cell.size(); i++)
				if (Box{0, r.cell[i].y, RAIL_W, r.cell[i].h}.hit(x, y)) return (int)i;
			return -1;
		}

		int railStep(int at, int dir, int n)
		{
			if (n <= 0) return 0;
			return ((at + dir) % n + n) % n;
		}

		// ---- Preview pane ----

		Pane previewPane(bool cover)
		{
			Pane p;
			p.brandTile = {20, 16, 22, 22};
			p.brandName = {50, 16, 220, 22};
			if (cover)
			{
				p.cover = {36, 66, 196, 278};
				p.column = {262, 74, PANEL_W - 44 - 262, PANEL_H - 74 - 20};
			}
			else
			{
				p.cover = {0, 0, 0, 0};
				p.column = {44, 74, PANEL_W - 88, PANEL_H - 74 - 20};
			}
			return p;
		}

		std::vector<Box> swatches(int n, int left, int top, int perRow)
		{
			std::vector<Box> out;
			if (perRow <= 0) perRow = 1;
			for (int i = 0; i < n; i++)
			{
				int row = i / perRow, col = i % perRow;
				out.push_back({left + col * (SWATCH_W + SWATCH_GAP), top + row * (SWATCH_H + SWATCH_NAME + SWATCH_ROW_GAP), SWATCH_W, SWATCH_H});
			}
			return out;
		}

		// ---- Book bar ----

		BookBar bookBar()
		{
			// the mock-up's (screens.js bookBar): a 14 pad; the grab mark; the title's row 54; the slider row 36; two
			// rows of keys 60 tall, 8 apart; the leave key 56; the hint bar 30
			BookBar b;
			const int pad = 14, inner = BOOK_W - 2 * pad;
			b.grab = {BOOK_W / 2 - 22, 7, 44, 4};
			b.title = {pad, 11, inner, 54};
			b.progress = {pad, b.title.y + b.title.h, inner, 36};
			b.value = {pad, b.progress.y, 44, 36};
			b.note = {pad + inner - 110, b.progress.y, 110, 36};
			b.slider = {b.value.x + b.value.w + 10, b.progress.y + 11, b.note.x - 12 - (b.value.x + b.value.w + 10), 14};
			int ky = b.progress.y + b.progress.h + 8, kw = (inner - GAP) / 2;
			for (int i = 0; i < 4; i++)
				b.keys[i] = {pad + (i % 2) * (inner - kw), ky + (i / 2) * (60 + GAP), kw, 60};
			b.leave = {pad, b.keys[2].y + 60 + GAP, inner, 56};
			b.hints = {0, b.leave.y + b.leave.h + GAP, BOOK_W, HINTS_H};
			int h = b.hints.y + b.hints.h;
			b.box = {0, BOOK_H - h, BOOK_W, h};
			return b;
		}

		Box sideways(Box u, bool topLeft)
		{
			// held top-left the panel's right edge is up: upright (u, v) is the panel's (640 - v, u); top-right its left
			// edge is up: (v, 480 - u)
			if (topLeft) return {PANEL_W - u.y - u.h, u.x, u.h, u.w};
			return {u.y, PANEL_H - u.x - u.w, u.h, u.w};
		}

		void uprightPoint(int x, int y, bool topLeft, int& ux, int& uy)
		{
			// (the unit's own square: the inverse of sideways, so a box's every unit comes back inside it)
			if (topLeft) { ux = y; uy = PANEL_W - 1 - x; }
			else { ux = PANEL_H - 1 - y; uy = x; }
		}

		int bookBarAt(const BookBar& b, int ux, int uy)
		{
			if (!b.box.hit(ux, uy)) return -1;
			int x = ux - b.box.x, y = uy - b.box.y;
			for (int i = 0; i < 4; i++)
				if (b.keys[i].hit(x, y)) return i;
			if (b.leave.hit(x, y)) return 4;
			if (b.progress.hit(x, y)) return 5;
			return 6;
		}

		// ---- Away-from-normal ----

		Away away(const std::string& name, const std::string& value, const std::string& normal, bool danger)
		{
			Away a;
			if (danger) { a.tone = AwayTone::Danger; a.words = value; a.dot = true; }
			else if (value == normal) { a.tone = AwayTone::Normal; a.words = name; }
			else { a.tone = AwayTone::Away; a.words = value; a.dot = true; }
			return a;
		}

		Away awayLevel(const std::string& name, int value, int normal, int dangerAbove, const std::string& unit)
		{
			return away(name, std::to_string(value) + unit, std::to_string(normal) + unit, dangerAbove > 0 && value > dangerAbove);
		}

		Box awayDot(Box c)
		{
			return {c.x + c.w - AWAY_INSET - AWAY_DOT, c.y + AWAY_INSET, AWAY_DOT, AWAY_DOT};
		}

		// ---- Screen-off warning ----

		ScreenOffWarn screenOffWarn(Box panel)
		{
			ScreenOffWarn w;
			w.row = {panel.x + MARGIN, panel.y + panel.h - WARN_FOOT - WARN_ROW_H, panel.w - 2 * MARGIN, WARN_ROW_H};
			w.icon = {w.row.x + 14, w.row.y + (WARN_ROW_H - 20) / 2, 20, 20};
			int tx = w.icon.x + w.icon.w + 10;
			w.text = {tx, w.row.y, w.row.x + w.row.w - 14 - tx, WARN_ROW_H};
			return w;
		}

		std::string screenOffWords(int msLeft)
		{
			int s = std::max(1, (msLeft + 999) / 1000);
			return "Screen off in " + std::to_string(s) + " s";
		}

		// ---- a page's own line, the Search field and result row, a Music/Videos row, a Books cover tile ----

		SectionLine sectionLine(Box area, bool note)
		{
			SectionLine l;
			int right = area.x + area.w;
			l.note = note ? Box{right - 160, area.y, 160, LINE_H} : Box{right, area.y, 0, LINE_H};
			l.line = {area.x, area.y, (note ? l.note.x - 8 : right) - area.x, LINE_H};
			return l;
		}

		SearchField searchField(Box row)
		{
			SearchField f;
			f.box = row;
			f.icon = {row.x + SEARCH_FIELD_PAD, row.y + (row.h - SEARCH_ICON) / 2, SEARCH_ICON, SEARCH_ICON};
			// the cap is 32 wide at the right; the text runs from after the magnifier to 10 before it
			f.cap = {row.x + row.w - SEARCH_FIELD_PAD - 32, row.y + (row.h - 19) / 2, 32, 19};
			int tx = f.icon.x + f.icon.w + 10;
			f.text = {tx, row.y, std::max(1, f.cap.x - 8 - tx), row.h};
			return f;
		}

		ResultRow resultRow(Box row)
		{
			ResultRow r;
			r.row = row;
			r.icon = {row.x + MEDIA_ROW_PAD, row.y + (row.h - RESULT_ICON) / 2, RESULT_ICON, RESULT_ICON};
			int right = row.x + row.w - MEDIA_ROW_PAD;
			r.note = {right - RESULT_NOTE_W, row.y, RESULT_NOTE_W, row.h};
			int x = r.icon.x + r.icon.w + 11;
			r.name = {x, row.y, std::max(1, r.note.x - 8 - x), row.h};
			return r;
		}

		MediaRow mediaRow(Box row)
		{
			MediaRow m;
			m.row = row;
			m.art = {row.x + MEDIA_ROW_PAD, row.y + (row.h - MEDIA_ART) / 2, MEDIA_ART, MEDIA_ART};
			m.right = row.x + row.w - MEDIA_ROW_PAD;
			int x = m.art.x + m.art.w + MEDIA_ART_GAP;
			// the two lines sit on one centre line either side of the row's: title above, the second line below
			m.name = {x, row.y + 8, std::max(1, m.right - x), 22};
			m.second = {x, row.y + 30, std::max(1, m.right - x), 18};
			return m;
		}

		CoverTile coverTile(Box box, bool title, bool progress)
		{
			CoverTile c;
			c.box = box;
			c.cover = {box.x, box.y, std::min(COVER_W, box.w), COVER_H};
			int y = c.cover.y + c.cover.h;
			c.title = title ? Box{box.x, y + COVER_TITLE_GAP, box.w, COVER_TITLE_H} : Box{box.x, y, box.w, 0};
			if (title) y += COVER_TITLE_GAP + COVER_TITLE_H;
			if (progress)
			{
				int py = y + COVER_BAR_GAP;
				c.percent = {box.x + box.w - COVER_PERCENT_W, py - 5, COVER_PERCENT_W, COVER_BAR_H + 10};
				c.bar = {box.x, py, std::max(1, box.w - COVER_PERCENT_W - 6), COVER_BAR_H};
			}
			else
			{
				c.bar = {box.x, y, 0, 0};
				c.percent = {box.x, y, 0, 0};
			}
			return c;
		}

		std::vector<Box> coverCells(Box area, int n, int cols, int cellH)
		{
			std::vector<Box> out;
			if (n <= 0 || cols <= 0) return out;
			out.reserve(n);
			for (int i = 0; i < n; i++)
				out.push_back({area.x + (i % cols) * (COVER_W + COVER_GAP), area.y + (i / cols) * (cellH + COVER_GAP),
					COVER_W, cellH});
			return out;
		}
	}
}
