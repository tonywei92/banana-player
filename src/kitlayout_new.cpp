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
			r.settings = {x, h - RAIL_PAD - RAIL_SETTINGS_H, RAIL_CELL_W, RAIL_SETTINGS_H};
			// the Now playing key is pinned just above Settings, and its slot is kept while nothing plays: the cells
			// never change height when playback starts (the owner, 2026-11-19)
			r.np = {x, r.settings.y - RAIL_GAP - RAIL_NP_H, RAIL_CELL_W, RAIL_NP_H};
			// (no START cap over Settings: the owner, 2026-11-19; START is taught by Settings' own hint bar)
			r.view = {0, r.search.y + r.search.h + RAIL_GAP, RAIL_W - 1,
				std::max(0, r.np.y - RAIL_GAP - (r.search.y + r.search.h + RAIL_GAP))};
			// seven sections all in sight at 45; six or fewer take the roomier 52
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
			// either side of its cell are still the rail), but only inside the view (Search and Now playing are the
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

		BrandMark brandMark(float x, float y, float size)
		{
			const float u = size / 22.f;
			BrandMark m{};
			auto box = [&](float* o, float bx, float by, float bw, float bh) { o[0] = x + bx * u; o[1] = y + by * u; o[2] = bw * u; o[3] = bh * u; };
			box(m.body, 0, 0, 22, 22);
			box(m.bar, 3, 4, 16, 4);
			box(m.slate, 3, 9, 16, 10);
			box(m.track, 5, 16, 12, 1);
			box(m.fill, 5, 16, 6, 1);
			box(m.knob, 10, 15.5f, 2, 2);
			// the stripes lean 2 units: top edge 4 -> 6 wide at x0 .. x0 + 2, foot 2 to the left
			for (int i = 0; i < 4; i++)
			{
				float x0 = 5 + 4 * i;	// top left of the stripe's upper edge
				float pts[8] = {x0, 4, x0 + 2, 4, x0, 8, x0 - 2, 8};
				for (int k = 0; k < 8; k++) m.stripe[i][k] = (k % 2 ? y : x) + pts[k] * u;
			}
			const float pl[6] = {9, 10.5f, 9, 15, 13.5f, 12.75f};
			for (int k = 0; k < 6; k++) m.play[k] = (k % 2 ? y : x) + pl[k] * u;
			m.bodyRadius = 5 * u;
			m.barRadius = u;
			m.slateRadius = 1.5f * u;
			return m;
		}

		StateCard stateCard(bool line, bool bar, bool time, bool hint)
		{
			StateCard c;
			const int side = 40, colW = PANEL_W - 2 * side;
			int y = STATE_TOP;
			c.tile = {(PANEL_W - STATE_TILE) / 2, y, STATE_TILE, STATE_TILE};
			y += STATE_TILE + 16;
			c.title = {side, y, colW, 34};
			y += 34;
			c.line = c.bar = c.time = c.hint = {0, 0, 0, 0};
			if (line)
			{
				y += 9;
				c.line = {side, y, colW, 22};
				y += 22;
			}
			if (bar)
			{
				y += 16;
				c.bar = {(PANEL_W - STATE_BAR_W) / 2, y, STATE_BAR_W, 6};
				y += 6;
				if (time)
				{
					y += 8;
					c.time = {side, y, colW, 16};
					y += 16;
				}
			}
			if (hint)
			{
				y += 18;
				c.hint = {side, y, colW, 22};
				y += 22;
			}
			c.bottom = y;
			return c;
		}

		Pane previewPane(bool cover, bool welcome, bool square, bool hero)
		{
			Pane p;
			p.brandTile = {20, 16, 22, 22};
			p.brandName = {50, 16, 220, 22};
			p.icon = {0, 0, 0, 0};
			p.below = {0, 0, 0, 0};
			p.hero = {0, 0, 0, 0};
			if (hero && !welcome)
			{
				p.cover = {0, 0, 0, 0};
				p.hero = {0, 0, PANEL_W, 232};
				p.column = {28, 196, PANEL_W - 56, PANEL_H - 196 - 16};
				return p;
			}
			if (welcome)
			{
				p.brandTile = p.brandName = {0, 0, 0, 0};
				p.icon = {44, 34, 84, 84};
				p.cover = {0, 0, 0, 0};
				p.column = {44 + 84 + 18, 34, PANEL_W - 44 - 146, 100};
				p.below = {44, 142, PANEL_W - 88, PANEL_H - 142 - 20};
				return p;
			}
			if (cover)
			{
				p.cover = {36, 66, 196, square ? 196 : 278};
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

		// ---- the Sleep timer ----

		SleepSheet sleepSheet()
		{
			SleepSheet l;
			// the body: the label 26, two rows of tiles 56 + 8 + 56 + 8, the foot line 25 (282 in all with the head)
			l.sheet = sheet({0, 0, 1, 26 + SLEEP_TILE_H + GAP + SLEEP_TILE_H + GAP + 25});
			int x = SHEET_PAD, w = PANEL_W - 2 * SHEET_PAD, y = l.sheet.bodyY;
			l.label = {x, y, w, 26};
			Box row1{x, y + 26, w, SLEEP_TILE_H};
			for (int i = 0; i < 5; i++) l.tile[i] = cell(row1, 5, GAP, i);
			// End of this song takes three fifths, Off two (the row's gap between them)
			int y2 = row1.y + SLEEP_TILE_H + GAP, w5 = w - GAP, first = w5 * 3 / 5;
			l.tile[5] = {x, y2, first, SLEEP_TILE_H};
			l.tile[6] = {x + first + GAP, y2, w - first - GAP, SLEEP_TILE_H};
			l.foot = {x, y2 + SLEEP_TILE_H + GAP, w, 25};
			return l;
		}

		int sleepMove(int cursor, Dir d)
		{
			cursor = std::clamp(cursor, 0, SLEEP_TILES - 1);
			switch (d)
			{
			case Dir::Left: return cursor == 0 || cursor == 5 ? cursor : cursor - 1;
			case Dir::Right: return cursor == 4 || cursor == 6 ? cursor : cursor + 1;
			// down from a length to the one under it (the first three over End of this song); up to the length over it
			case Dir::Down: return cursor < 5 ? (cursor < 3 ? 5 : 6) : cursor;
			case Dir::Up: return cursor == 5 ? 1 : cursor == 6 ? 4 : cursor;
			}
			return cursor;
		}

		SleepWarn sleepWarn(Box panel)
		{
			SleepWarn l;
			l.card = {panel.x + MARGIN, panel.y + panel.h - WARN_FOOT - SLEEP_WARN_H, panel.w - 2 * MARGIN, SLEEP_WARN_H};
			// its line, 30 tall 12 in; the keys under it, 12 from the card's foot
			l.icon = {l.card.x + 14, l.card.y + 12 + 5, 20, 20};
			l.line = {l.icon.x + l.icon.w + 10, l.card.y + 12, l.card.x + l.card.w - 14 - (l.icon.x + l.icon.w + 10), 30};
			Box keys{l.card.x + 12, l.card.y + l.card.h - 12 - SLEEP_TILE_H, l.card.w - 24, SLEEP_TILE_H};
			l.snooze = cell(keys, 2, GAP, 0);
			l.off = cell(keys, 2, GAP, 1);
			return l;
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

		// ---- an album, an artist, Music's head, a video and a folder, the Books shelf, Search's page ----

		AlbumTile albumTile(Box box)
		{
			AlbumTile a;
			a.box = box;
			a.cover = {box.x, box.y, std::min(ALBUM_ART, box.w), ALBUM_ART};
			a.title = {box.x, a.cover.y + a.cover.h + ALBUM_TITLE_GAP, box.w, ALBUM_TITLE_H};
			a.artist = {box.x, a.title.y + a.title.h + ALBUM_ARTIST_GAP, box.w, ALBUM_ARTIST_H};
			a.mark = {a.cover.x + a.cover.w - ALBUM_MARK_INSET - ALBUM_MARK, a.cover.y + ALBUM_MARK_INSET, ALBUM_MARK, ALBUM_MARK};
			return a;
		}

		Box AlbumGrid::rowBox(int r) const
		{
			int w = cols > 0 ? cols * cellW + (cols - 1) * colGap : 0;
			return {area.x, area.y + (r - first) * (cellH + rowGap), w, cellH};
		}

		AlbumGrid albumGrid(Box area, int albums, int first, int cols)
		{
			AlbumGrid g;
			g.area = area;
			g.cols = std::max(1, cols);
			g.albums = std::max(0, albums);
			g.rows = (g.albums + g.cols - 1) / g.cols;
			const int pitch = g.cellH + g.rowGap;
			// a row may hang over the area's foot (a scroll's next row is not shown), but at least one is laid
			g.shown = std::max(1, (area.h + g.rowGap) / std::max(1, pitch));
			g.first = std::clamp(first, 0, std::max(0, g.rows - g.shown));
			g.cell.reserve(g.albums);
			for (int i = 0; i < g.albums; i++)
				g.cell.push_back({area.x + (i % g.cols) * (g.cellW + g.colGap),
					area.y + (i / g.cols - g.first) * pitch, g.cellW, g.cellH});
			return g;
		}

		AlbumGrid coverGrid(Box area, int n, int first, int cols)
		{
			AlbumGrid g;
			g.area = area;
			g.cols = std::max(1, cols);
			g.cellW = COVER_W;
			g.cellH = COVER_GRID_CELL_H;
			g.colGap = COVER_GAP;
			g.rowGap = SP12;
			g.albums = std::max(0, n);
			g.rows = (g.albums + g.cols - 1) / g.cols;
			const int pitch = g.cellH + g.rowGap;
			g.shown = std::max(1, (area.h + g.rowGap) / pitch);
			g.first = std::clamp(first, 0, std::max(0, g.rows - g.shown));
			for (int i = 0; i < g.albums; i++)
				g.cell.push_back({area.x + (i % g.cols) * (g.cellW + g.colGap), area.y + (i / g.cols - g.first) * pitch,
					g.cellW, g.cellH});
			return g;
		}

		RowList rowList(Box area, int n, int first, int rowH, int gap)
		{
			RowList l;
			l.area = area;
			l.n = std::max(0, n);
			l.rowH = std::max(1, rowH);
			l.gap = std::max(0, gap);
			int fit = std::max(1, (area.h + l.gap) / (l.rowH + l.gap));
			l.first = std::clamp(first, 0, std::max(0, l.n - fit));
			l.shown = std::min(fit, l.n - l.first);
			for (int k = 0; k < l.shown; k++) l.row.push_back({area.x, area.y + k * (l.rowH + l.gap), area.w, l.rowH});
			return l;
		}

		int RowList::at(int x, int y) const
		{
			for (int k = 0; k < (int)row.size(); k++)
				if (row[k].hit(x, y)) return first + k;
			return -1;
		}

		Box rowKey(Box row, int keyW)
		{
			int h = std::min(44, row.h);
			return {row.x + row.w - SP8 - keyW, row.y + (row.h - h) / 2, keyW, h};
		}

		std::vector<Box> keyRow(Box area, const std::vector<int>& widths, int gap)
		{
			std::vector<Box> out;
			int n = (int)widths.size(), fixed = 0, flex = 0;
			for (int w : widths)
			{
				if (w > 0) fixed += w;
				else flex++;
			}
			int room = std::max(0, area.w - fixed - gap * std::max(0, n - 1));
			int share = flex ? room / flex : 0, extra = flex ? room - share * flex : 0;
			int x = area.x, h = std::min(KEY_ROW_H, area.h);
			for (int i = 0; i < n; i++)
			{
				int w = widths[i] > 0 ? widths[i] : share + (extra-- > 0 ? 1 : 0);
				out.push_back({x, area.y, w, h});
				x += w + gap;
			}
			return out;
		}

		SeriesPage seriesPage(Box body, bool keys)
		{
			SeriesPage s;
			s.inner = {body.x + SP12, body.y, std::max(1, body.w - 2 * SP12), body.h};
			s.keys = keys ? Box{s.inner.x, s.inner.y, s.inner.w, KEY_ROW_H} : Box{s.inner.x, s.inner.y, s.inner.w, 0};
			int top = keys ? s.keys.y + KEY_ROW_H + SP8 : s.inner.y;
			s.rows = {s.inner.x, top, s.inner.w, std::max(0, body.y + body.h - top)};
			return s;
		}

		ArtistRow artistRow(Box row)
		{
			ArtistRow a;
			a.row = row;
			a.art = {row.x + SP12, row.y + (row.h - ARTIST_ART) / 2, ARTIST_ART, ARTIST_ART};
			a.right = row.x + row.w - SP12;
			int x = a.art.x + a.art.w + ARTIST_ART_GAP;
			a.name = {x, row.y + SP8, std::max(1, a.right - x), 22};
			a.second = {x, row.y + row.h - SP8 - 18, std::max(1, a.right - x), 18};
			a.chevronX = a.right - 7;	// the chevron's centre (10 wide)
			return a;
		}

		MusicHeader musicHeader(Box body, const std::vector<int>& chipWidths)
		{
			MusicHeader h;
			h.strip = {body.x, body.y, body.w, CHIP_STRIP};
			h.chips = filterChips(h.strip, chipWidths);
			return h;
		}

		PlaylistsRoot playlistsRoot(Box body, const std::vector<int>& chipWidths)
		{
			// the same inner box as Music's own views (12 in from the body's sides)
			Box inner{body.x + SP12, body.y, body.w - 2 * SP12, body.h};
			PlaylistsRoot r;
			r.head = musicHeader(inner, chipWidths);
			r.rowsY = r.head.strip.y + CHIP_STRIP + SP6;
			int room = std::max(0, body.y + body.h - r.rowsY);
			r.rows = std::max(1, room / PLAYLIST_ROW_H);
			r.list = {inner.x, r.rowsY, inner.w, r.rows * PLAYLIST_ROW_H};
			r.below = {inner.x, r.rowsY, inner.w, room};
			return r;
		}

		VideoRow videoRow(Box row)
		{
			VideoRow v;
			v.row = row;
			v.thumb = {row.x + SP12, row.y + (row.h - VIDEO_THUMB_H) / 2, VIDEO_THUMB_W, VIDEO_THUMB_H};
			v.right = row.x + row.w - SP12;
			int x = v.thumb.x + v.thumb.w + SP12;
			v.name = {x, row.y + SP8, std::max(1, v.right - x), 22};
			v.note = {v.right - 160, row.y + SP8, 160, 22};
			v.second = {x, row.y + row.h - SP8 - 18, std::max(1, v.right - x), 18};
			// the resume line at the foot, from the title's left to the note's right (the mock-up's 3-unit `.ab` line)
			v.bar = {x, row.y + row.h - SP6, std::max(1, v.right - x), VIDEO_BAR_H};
			return v;
		}

		FolderRow folderRow(Box row)
		{
			FolderRow f;
			f.row = row;
			f.tile = {row.x + SP12, row.y + (row.h - FOLDER_TILE) / 2, FOLDER_TILE, FOLDER_TILE};
			f.right = row.x + row.w - SP12;
			int x = f.tile.x + f.tile.w + SP12;
			f.name = {x, row.y + SP8, std::max(1, f.right - 10 - x), 22};
			f.second = {x, row.y + row.h - SP8 - 18, std::max(1, f.right - 10 - x), 18};
			f.chevron = {f.right - 14, row.y + row.h / 2 - 7, 14, 14};
			return f;
		}

		BooksShelf booksShelf(Box body, int readingNow, int shelfCount, int first)
		{
			BooksShelf b;
			b.area = body;
			b.readingHead = {body.x, body.y, body.w, LINE_H};
			b.readingStrip = {body.x, body.y + LINE_H + SHELF_HEAD_GAP, body.w, COVER_CELL_H};
			b.reading = coverCells(b.readingStrip, std::min(std::max(0, readingNow), b.cols), b.cols, COVER_CELL_H);
			b.shelfHead = {body.x, b.readingStrip.y + b.readingStrip.h + SHELF_GROUP_GAP, body.w, LINE_H};
			int gridY = b.shelfHead.y + LINE_H + SHELF_HEAD_GAP;
			b.shelfGrid = {body.x, gridY, body.w, std::max(0, body.y + body.h - gridY)};
			b.rows = (std::max(0, shelfCount) + b.cols - 1) / b.cols;
			const int pitch = SHELF_CELL_H + COVER_GAP;
			// a row may hang over the grid's foot (over the hint bar, which paints after the page), but at least
			// one is laid — the shelf's heading always has books under it, as the album grid guarantees its own
			b.shown = std::max(1, (b.shelfGrid.h + COVER_GAP) / std::max(1, pitch));
			b.first = std::clamp(first, 0, std::max(0, b.rows - b.shown));
			b.shelf.reserve(std::max(0, shelfCount));
			for (int i = 0; i < std::max(0, shelfCount); i++)
				b.shelf.push_back({body.x + (i % b.cols) * (COVER_W + COVER_GAP),
					gridY + (i / b.cols - b.first) * pitch, COVER_W, SHELF_CELL_H});
			return b;
		}

		SearchPage searchPage(Box body, const std::vector<int>& groupRows, int recentCount, bool clear)
		{
			SearchPage p;
			p.area = body;
			p.field = {body.x, body.y, body.w, SEARCH_FIELD_H};
			int y = body.y + SEARCH_FIELD_H + SEARCH_GROUP_GAP;
			for (int n : groupRows)
			{
				if (n <= 0) continue;	// a group with nothing is not shown
				SearchGroup g;
				g.head = {body.x, y, body.w, LINE_H};
				y += LINE_H + SEARCH_HEAD_GAP;
				g.firstRow = (int)p.row.size();
				g.rows = n;
				for (int i = 0; i < n; i++)
				{
					p.row.push_back({body.x, y, body.w, RESULT_ROW_H});
					y += RESULT_ROW_H;
				}
				y += SEARCH_GROUP_GAP;
				p.group.push_back(g);
			}
			if (recentCount > 0)
			{
				p.recentHead = {body.x, y, body.w, LINE_H};
				y += LINE_H + SEARCH_HEAD_GAP;
				for (int i = 0; i < recentCount; i++)
				{
					p.recent.push_back({body.x, y, body.w, SEARCH_RECENT_H});
					y += SEARCH_RECENT_H;
				}
				if (clear)
				{
					y += SEARCH_CLEAR_GAP;
					p.clear = {body.x, y, body.w, SEARCH_CLEAR_H};
				}
			}
			if (p.recentHead.w == 0) p.recentHead = {body.x, y, 0, 0};
			if (p.clear.w == 0) p.clear = {body.x, y, 0, 0};
			return p;
		}
	}
}
