#include "kitlayout.h"
#include "tracks.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	namespace lay
	{
		Box cell(Box area, int n, int gap, int i)
		{
			if (n <= 0) return {area.x, area.y, 0, area.h};
			int room = area.w - (n - 1) * gap, w = room / n, spare = room - w * n;
			// the spare units widen the last `spare` cells by one each
			int wider = std::max(0, i - (n - spare));
			return {area.x + i * (w + gap) + wider, area.y, w + (i >= n - spare ? 1 : 0), area.h};
		}

		std::vector<Box> gridCells(Box area, int n, int cols, int gap)
		{
			std::vector<Box> out;
			if (n <= 0 || cols <= 0) return out;
			int rows = (n + cols - 1) / cols;
			int h = (area.h - (rows - 1) * gap) / rows;
			out.reserve(n);
			for (int i = 0; i < n; i++)
			{
				Box line{area.x, area.y + (i / cols) * (h + gap), area.w, h};
				out.push_back(cell(line, cols, gap, i % cols));
			}
			return out;
		}

		Box transportCell(Box area, int i, int gap, int mid)
		{
			int side = (area.w - mid - 4 * gap) / 4;
			// (what four equal sides leave over goes to the play key, so the row ends on the area's edge)
			int play = area.w - 4 * gap - 4 * side;
			int x = area.x;
			for (int k = 0; k < i; k++) x += (k == 2 ? play : side) + gap;
			return {x, area.y, i == 2 ? play : side, area.h};
		}

		Box barKnob(Box track, double share, int w, int over)
		{
			int at = track.x + (int)std::lround(track.w * std::clamp(share, 0.0, 1.0));
			return {at - w / 2, track.y - over, w, track.h + 2 * over};
		}

		std::vector<int> barMarks(Box track, double duration, const std::vector<double>& starts, int minGap, size_t most)
		{
			std::vector<int> out;
			if (duration <= 0 || starts.size() > most) return out;
			int last = track.x - 1000;
			for (double t : starts)
			{
				if (t <= 0 || t >= duration) continue;
				int x = track.x + (int)(track.w * (t / duration));
				if (x - last < minGap) continue;
				out.push_back(x);
				last = x;
			}
			return out;
		}

		std::vector<int> spread(const std::vector<int>& widths, int gap, int left, int right, int minGap)
		{
			std::vector<int> xs;
			int n = (int)widths.size(), total = 0;
			for (int w : widths) total += w;
			if (n > 1 && total + gap * (n - 1) > right - left) gap = std::max(minGap, (right - left - total) / (n - 1));
			int all = total + gap * std::max(0, n - 1);
			int x = std::max(left, left + (right - left - all) / 2);
			for (int w : widths)
			{
				xs.push_back(x);
				x += w + gap;
			}
			return xs;
		}

		// ---- tabs, and the List sheet ----

		Tabs tabs(Box area, const std::vector<int>& widths, int gap, int capW)
		{
			Tabs out;
			out.area = area;
			// the strip 44 tall in the header it is given (54), its caps 19 tall in the strip's middle
			capW = std::max(TAB_CAP, capW);
			int y = area.y + (area.h - TAB_H) / 2, cy = y + (TAB_H - TAB_CAP) / 2;
			out.left = {area.x, cy, capW, TAB_CAP};
			int x = area.x + capW + (widths.empty() ? 0 : gap);
			for (int w : widths)
			{
				out.tab.push_back({x, y, w, TAB_H});
				x += w + gap;
			}
			out.right = {widths.empty() ? area.x + capW + gap : x, cy, capW, TAB_CAP};
			return out;
		}

		TabPage tabPage(Box body, const std::vector<int>& widths, int capW)
		{
			TabPage t;
			t.inner = {body.x + SP12, body.y, std::max(1, body.w - 2 * SP12), body.h};
			t.strip = tabs({t.inner.x, t.inner.y, t.inner.w, TAB_H}, widths, TAB_GAP, capW);
			int top = t.inner.y + TAB_H + SP8;
			t.content = {t.inner.x, top, t.inner.w, std::max(0, body.y + body.h - top)};
			return t;
		}

		int Sections::headRow(int k) const { return k < 0 || k >= (int)before.size() ? -1 : before[k] + k; }

		bool Sections::isHead(int r) const
		{
			for (int k = 0; k < (int)before.size(); k++)
				if (headRow(k) == r) return true;
			return false;
		}

		int Sections::item(int r) const
		{
			if (r < 0 || r >= rows() || isHead(r)) return -1;
			int heads = 0;
			for (int k = 0; k < (int)before.size(); k++)
				if (headRow(k) < r) heads++;
			return r - heads;
		}

		int Sections::row(int i) const
		{
			if (i < 0 || i >= items) return -1;
			int heads = 0;
			for (int b : before)
				if (b <= i) heads++;
			return i + heads;
		}

		int Sections::step(int r, int dir, bool wrap) const
		{
			int n = rows();
			if (items <= 0 || n <= 0) return -1;
			dir = dir < 0 ? -1 : 1;
			for (int k = 1; k <= n; k++)
			{
				int to = r + dir * k;
				if (wrap) to = ((to % n) + n) % n;
				else if (to < 0 || to >= n) return item(r) >= 0 ? r : step(r, -dir, false);
				if (!isHead(to)) return to;
			}
			return -1;
		}

		int tabAt(const Tabs& t, int x, int y)
		{
			for (size_t i = 0; i < t.tab.size(); i++)
				if (t.tab[i].hit(x, y)) return (int)i;
			return -1;
		}

		// the boxes every list sheet shares: the grab mark, the header, the rows (or a body) and the hint bar
		namespace
		{
			ListSheet listSheetOf(int rows, int header, int h)
			{
				ListSheet out;
				out.rows = rows;
				out.header = header;
				out.box = {0, PANEL_H - h, PANEL_W, h};
				out.grab = {PANEL_W / 2 - 22, out.box.y + 7, 44, 4};
				out.head = {SHEET_PAD, out.box.y + 11, PANEL_W - 2 * SHEET_PAD, header};
				out.rowsY = out.head.y + header;
				out.hints = {0, PANEL_H - HINTS_H, PANEL_W, HINTS_H};
				return out;
			}
		}

		ListSheet listSheet(int rows, int header)
		{
			// the grab mark's 11, the header, the rows, 8 clear of the hint bar
			int h = 11 + std::max(0, header) + std::max(0, rows) * LIST_ROW + 8 + HINTS_H;
			return listSheetOf(std::max(0, rows), std::max(0, header), h);
		}

		ListSheet listSheetBody(int body, int header)
		{
			int headerH = std::max(0, header);
			int h = 11 + headerH + std::max(0, body) + 8 + HINTS_H;
			return listSheetOf(0, headerH, std::clamp(h, SHEET_MIN, SHEET_MAX));
		}

		Box ListSheet::row(int i) const { return {SHEET_PAD, rowsY + i * LIST_ROW, PANEL_W - 2 * SHEET_PAD, LIST_ROW}; }

		int listScroll(int cursor, int scroll, int shown, int n)
		{
			if (n <= 0 || shown <= 0) return 0;
			cursor = std::clamp(cursor, 0, n - 1);
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + shown) scroll = cursor - shown + 1;
			return std::clamp(scroll, 0, std::max(0, n - shown));
		}

		int dragScroll(int scrollStart, int downY, int y, int rowH, int first, int n, int shown)
		{
			int maxScroll = std::max(first, n - shown);
			int moved = rowH > 0 ? (int)std::lround((downY - y) / (double)rowH) : 0;
			return std::clamp(scrollStart + moved, first, maxScroll);
		}

		int rowAt(int y, int rowsY, int scroll, int shown, int n)
		{
			if (n <= 0) return 0;
			if (shown <= 0) return std::clamp(scroll, 0, n - 1);
			// (above the rows `y - rowsY` is negative; truncation would round it to 0, and the clamp says first)
			int i = (y - rowsY) / LIST_ROW;
			i = std::clamp(i, 0, shown - 1);
			return std::clamp(scroll + i, 0, n - 1);
		}

		Sheet sheet(const SheetSpec& s)
		{
			Sheet out;
			out.spec = s;
			out.spec.cols = std::max(1, s.cols);
			int lines = (s.rows + out.spec.cols - 1) / out.spec.cols;
			// the grab mark's 11, the header, the content, 8 clear of the hint bar
			int h = 11 + SHEET_HEAD + s.body + (s.chips ? SHEET_CHIP_H + GAP : 0) + (s.tiles ? SHEET_TILE + GAP : 0) +
				lines * SHEET_ROW + 8 + HINTS_H;
			h = std::clamp(h, SHEET_MIN, SHEET_MAX);
			out.box = {0, PANEL_H - h, PANEL_W, h};
			out.grab = {PANEL_W / 2 - 22, out.box.y + 7, 44, 4};
			out.head = {SHEET_PAD, out.box.y + 11, PANEL_W - 2 * SHEET_PAD, SHEET_HEAD};
			out.bodyY = out.head.y + SHEET_HEAD;
			out.chipsY = out.bodyY + s.body;
			out.tilesY = out.chipsY + (s.chips ? SHEET_CHIP_H + GAP : 0);
			out.rowsY = out.tilesY + (s.tiles ? SHEET_TILE + GAP : 0);
			out.hints = {0, PANEL_H - HINTS_H, PANEL_W, HINTS_H};
			return out;
		}

		Box Sheet::chip(int i) const
		{
			return cell({SHEET_PAD, chipsY, PANEL_W - 2 * SHEET_PAD, SHEET_CHIP_H}, std::max(1, spec.chips), GAP, i);
		}

		Box Sheet::tile(int i) const
		{
			return cell({SHEET_PAD, tilesY, PANEL_W - 2 * SHEET_PAD, SHEET_TILE}, std::max(1, spec.tiles), GAP, i);
		}

		Box Sheet::row(int i) const
		{
			Box c = cell({SHEET_PAD, rowsY, PANEL_W - 2 * SHEET_PAD, SHEET_ROW}, spec.cols, SHEET_PAD, i % spec.cols);
			c.y += i / spec.cols * SHEET_ROW;
			return c;
		}

		int sheetMove(const SheetSpec& s, int cursor, Dir d)
		{
			int cols = std::max(1, s.cols), chips = s.chips, tiles = s.tiles, rows = s.rows;
			int n = chips + tiles + rows;
			if (n <= 0) return 0;
			cursor = std::clamp(cursor, 0, n - 1);
			if (cursor < chips)
			{
				if (d == Dir::Left) return std::max(0, cursor - 1);
				if (d == Dir::Right) return std::min(chips - 1, cursor + 1);
				if (d == Dir::Up) return cursor;
				// down: the tile (or row) under the chip's place in the line
				if (tiles) return chips + std::min(tiles - 1, cursor * tiles / chips);
				if (rows) return chips;
				return cursor;
			}
			if (cursor < chips + tiles)
			{
				int t = cursor - chips;
				if (d == Dir::Left) return chips + std::max(0, t - 1);
				if (d == Dir::Right) return chips + std::min(tiles - 1, t + 1);
				if (d == Dir::Up) return chips ? std::min(chips - 1, t) : cursor;
				if (!rows) return cursor;
				// down: the row under the tile (four tiles over two columns: the left two, the right two)
				int col = std::min(cols - 1, t * cols / tiles);
				return chips + tiles + std::min(rows - 1, col);
			}
			int r = cursor - chips - tiles, col = r % cols, line = r / cols;
			switch (d)
			{
			case Dir::Left: return col > 0 ? cursor - 1 : cursor;
			case Dir::Right: return col + 1 < cols && r + 1 < rows ? cursor + 1 : cursor;
			case Dir::Down: return r + cols < rows ? cursor + cols : cursor;
			default:
				if (line > 0) return cursor - cols;
				// up from the first line: the tile (or chip) over that column
				if (tiles) return chips + std::min(tiles - 1, (col * tiles + cols - 1) / cols);
				if (chips) return std::min(chips - 1, col);
				return cursor;
			}
		}

		// ---- View › Display on a song ----

		DisplayPage displayPage(int styles)
		{
			DisplayPage d;
			styles = std::max(0, styles);
			int lines = (styles + DISPLAY_COLS - 1) / DISPLAY_COLS;
			int grid = lines * DISPLAY_TILE_H + std::max(0, lines - 1) * DISPLAY_STYLE_GAP;
			// the label 16 and 4, the modes, 8, the style's line 16 and 4, the grid, then the foot row; as tall as
			// the sheet leaves, what there is to spare split above and below
			int body = DISPLAY_H - (11 + SHEET_HEAD + 8 + HINTS_H);
			d.sheet = sheet({0, 0, 1, body});
			int content = 16 + 4 + DISPLAY_TILE_H + 8 + 16 + 4 + grid + DISPLAY_FOOT_GAP + DISPLAY_FOOT_H;
			int y = d.sheet.bodyY + std::max(0, (body - content) / 2);
			const int modesW = DISPLAY_MODES * DISPLAY_MODE_W + (DISPLAY_MODES - 1) * DISPLAY_MODE_GAP;
			const int stylesW = DISPLAY_COLS * DISPLAY_STYLE_W + (DISPLAY_COLS - 1) * DISPLAY_STYLE_GAP;
			const int mx = (PANEL_W - modesW) / 2, sx = (PANEL_W - stylesW) / 2;
			d.modesLabel = {mx, y, modesW, 16};
			y += 16 + 4;
			for (int i = 0; i < DISPLAY_MODES; i++) d.mode[i] = {mx + i * (DISPLAY_MODE_W + DISPLAY_MODE_GAP), y, DISPLAY_MODE_W, DISPLAY_TILE_H};
			y += DISPLAY_TILE_H + 8;
			d.styleLine = {mx, y, modesW, 16};
			y += 16 + 4;
			for (int i = 0; i < styles; i++)
				d.style.push_back({sx + (i % DISPLAY_COLS) * (DISPLAY_STYLE_W + DISPLAY_STYLE_GAP),
					y + (i / DISPLAY_COLS) * (DISPLAY_TILE_H + DISPLAY_STYLE_GAP), DISPLAY_STYLE_W, DISPLAY_TILE_H});
			y += grid + DISPLAY_FOOT_GAP;
			// the foot row: the panel's own side margins (the modes' row is one unit wider either side of them)
			d.foot = content <= body ? Box{SHEET_PAD, y, PANEL_W - 2 * SHEET_PAD, DISPLAY_FOOT_H} : Box{0, y, 0, 0};
			return d;
		}

		// the one of `boxes` whose middle is nearest x
		static int nearestX(const Box* boxes, int n, int x)
		{
			int best = 0;
			for (int i = 1; i < n; i++)
				if (std::abs(boxes[i].x + boxes[i].w / 2 - x) < std::abs(boxes[best].x + boxes[best].w / 2 - x)) best = i;
			return best;
		}

		int displayMove(const DisplayPage& d, int cursor, Dir dir)
		{
			int styles = (int)d.style.size(), n = d.count();
			cursor = std::clamp(cursor, 0, n - 1);
			if (cursor < DISPLAY_MODES)
			{
				if (dir == Dir::Left) return std::max(0, cursor - 1);
				if (dir == Dir::Right) return std::min(DISPLAY_MODES - 1, cursor + 1);
				if (dir == Dir::Up || !styles) return cursor;
				const Box& m = d.mode[cursor];
				return DISPLAY_MODES + nearestX(d.style.data(), std::min(styles, DISPLAY_COLS), m.x + m.w / 2);
			}
			int s = cursor - DISPLAY_MODES;
			switch (dir)
			{
			case Dir::Left: return s > 0 ? cursor - 1 : cursor;
			case Dir::Right: return s + 1 < styles ? cursor + 1 : cursor;
			case Dir::Down: return s + DISPLAY_COLS < styles ? cursor + DISPLAY_COLS : cursor;
			default:
				if (s >= DISPLAY_COLS) return cursor - DISPLAY_COLS;
				return nearestX(d.mode, DISPLAY_MODES, d.style[s].x + d.style[s].w / 2);
			}
		}

		int displayAt(const DisplayPage& d, int x, int y)
		{
			for (int i = 0; i < d.count(); i++)
				if (d.item(i).hit(x, y)) return i;
			return -1;
		}

		// ---- the Settings page ----

		Box settingsTile(Box area, int n, int cols, int i)
		{
			if (n <= 0 || cols <= 0) return {area.x, area.y, 0, 0};
			int lines = (n + cols - 1) / cols;
			int h = (area.h - (lines - 1) * SET_TILE_GAP) / lines;
			Box line{area.x, area.y + (i / cols) * (h + SET_TILE_GAP), area.w, h};
			return cell(line, cols, SET_TILE_GAP, i % cols);
		}

		Box SettingsRows::row(int i) const
		{
			return {area.x, area.y + i * SET_ROW_H, area.w, SET_ROW_H - 2};
		}

		Box settingsBody(Box body)
		{
			// the page's own top margin is 4: with the shell's body 6 under the header the Settings page starts at 64,
			// where its non-shell variant does (SET_ROW_TOP)
			return {body.x + MARGIN, body.y + SP4, std::max(0, body.w - 2 * MARGIN), std::max(0, body.h - SP4)};
		}

		Box settingsTiles(Box body, int rows)
		{
			Box a = settingsBody(body);
			a.h = rows > 0 ? rows * SET_TILE_H + (rows - 1) * SET_TILE_GAP : 0;
			return a;
		}

		SettingsRows settingsRowsIn(Box body, int shown)
		{
			SettingsRows l;
			Box a = settingsBody(body);
			l.area = {a.x, a.y, a.w, std::max(0, shown * SET_ROW_H - 2)};
			l.help = {0, 0, 0, 0};
			// the help's two lines sit 50 above the body's foot, when the rows leave that much room
			int helpTop = body.y + body.h - 50;
			if (helpTop >= l.area.y + l.area.h + 2) l.help = {a.x + 8, helpTop, std::max(0, a.w - 16), 2 * 22};
			return l;
		}

		SettingsRows settingsRows(int shown)
		{
			SettingsRows l;
			l.area = {MARGIN, SET_ROW_TOP, PANEL_W - 2 * MARGIN, shown * SET_ROW_H - 2};
			l.help = {24, SET_HELP_Y, PANEL_W - 2 * 24, 2 * 22};
			return l;
		}

		SettingStepper settingStepper(Box row)
		{
			SettingStepper s;
			s.row = row;
			// the right arrow is the row's last 14 units in, as it always was; the left one 238 before it, and the
			// value's 196-unit box centred in between (its own box, not an offset from the old full-panel margin)
			s.nextX = row.x + row.w - 14;
			s.prevX = s.nextX - STEP_VALUE_W;
			int mid = (s.prevX + s.nextX) / 2;
			s.label = {row.x + 16, row.y, std::max(1, s.prevX - row.x - 32), row.h};
			s.value = {mid - STEP_VALUE_TEXT / 2, row.y, STEP_VALUE_TEXT, row.h};
			s.back = {s.prevX - 12, row.y, mid - (s.prevX - 12), row.h};
			s.fwd = {mid, row.y, row.x + row.w - mid, row.h};
			return s;
		}

		bool stepperBack(const SettingStepper& s, int x) { return x >= s.back.x && x < s.back.x + s.back.w; }

		PlayerLayout shellHeader(Box main)
		{
			PlayerLayout l;
			l.back = {main.x + 10, 3, 56, 48};
			l.backHit = {main.x, 0, 70, SHELL_HEAD};
			l.more = {main.x + main.w - 10 - 56, 3, 56, 48};
			l.moreHit = {main.x + main.w - 70, 0, 70, SHELL_HEAD};
			// (only its x and width are used: the header's title sits beside the back key, in the strip, and its right
			// slot ends at this box's right edge less 8)
			l.title = {main.x + MARGIN, 0, std::max(0, main.w - 2 * MARGIN), SHELL_HEAD};
			l.hints = {main.x, PANEL_H - HINTS_H, main.w, HINTS_H};
			return l;
		}

		ReaderHeader readerHeader(Box main, bool bookmark)
		{
			ReaderHeader h;
			h.strip.back = h.back = {main.x + 10, 3, 56, 48};
			h.strip.backHit = h.backHit = {main.x, 0, 70, SHELL_HEAD};
			h.strip.more = h.more = {main.x + main.w - 10 - 56, 3, 56, 48};
			h.strip.moreHit = h.moreHit = {main.x + main.w - 70, 0, 70, SHELL_HEAD};
			int right = h.more.x - SP8;
			if (bookmark)
			{
				h.bookmark = {right - 44, 5, 44, 44};
				h.bookmarkHit = {h.bookmark.x - 8, 0, h.bookmark.w + 8, SHELL_HEAD};
				right = h.bookmark.x - SP8;
			}
			else h.bookmark = h.bookmarkHit = Box{0, 0, 0, 0};
			// the inline title's room: from beside the back key to the leftmost key at the right
			int x = main.x + 72;
			h.title = {x, 4, std::max(0, right - x), 24};
			h.crumb = {x, 26, std::max(0, right - x), 22};
			h.strip.title = {x, 0, std::max(0, right - x), SHELL_HEAD};
			h.strip.hints = {main.x, PANEL_H - HINTS_H, main.w, HINTS_H};
			return h;
		}

		ReaderSurface readerSurface(Box area, bool strip)
		{
			ReaderSurface r;
			r.area = area;
			int cy = area.y + area.h / 2;
			r.pill = {area.x + SP24, cy - 20, area.w - 2 * SP24, 40};
			r.chevL = {area.x + SP24, cy - 12, 24, 24};
			r.chevR = {area.x + area.w - SP24 - 24, cy - 12, 24, 24};
			if (strip) r.chevL = r.chevR = Box{0, 0, 0, 0};
			r.note = {area.x, area.y + area.h - 36, area.w, 24};
			return r;
		}

		ReaderPage readerPage(bool strip)
		{
			ReaderPage l;
			Box zero{0, 0, 0, 0};
			l.pan = l.fit = l.help = l.surface = zero;
			for (Box& k : l.keys) k = zero;
			int sliderY, sliderH;
			if (strip)
			{
				// the slider under the header, the surface under it down to 8 above the row
				sliderY = READER_TOP;
				sliderH = 30;
				l.surface = {MARGIN, sliderY + sliderH + GAP, PANEL_W - 2 * MARGIN, READER_ROW_Y - GAP - (sliderY + sliderH + GAP)};
			}
			else
			{
				l.pan = {MARGIN, READER_TOP, 388, 262};
				Box side{l.pan.x + l.pan.w + SP12, READER_TOP, PANEL_W - MARGIN - (l.pan.x + l.pan.w + SP12), 0};
				for (int r = 0; r < 2; r++)
					for (int c = 0; c < 2; c++)
						l.keys[r * 2 + c] = cell({side.x, side.y + r * (62 + GAP), side.w, 62}, 2, GAP, c);
				l.fit = {side.x, side.y + 2 * (62 + GAP), side.w, 56};
				l.help = {side.x + SP4, l.fit.y + l.fit.h + SP6, side.w - SP8, l.pan.y + l.pan.h - (l.fit.y + l.fit.h + SP6)};
				sliderY = l.pan.y + l.pan.h + GAP;
				sliderH = 28;
			}
			l.from = {MARGIN, sliderY, 40, sliderH};
			l.to = {PANEL_W - MARGIN - 40, sliderY, 40, sliderH};
			l.track = {l.from.x + l.from.w + SP12, sliderY + (sliderH - 10) / 2, l.to.x - SP12 - (l.from.x + l.from.w + SP12), 10};
			l.sliderHit = {MARGIN, sliderY - SP6, PANEL_W - 2 * MARGIN, sliderH + 2 * SP6};
			for (int i = 0; i < 3; i++) l.row[i] = cell({MARGIN, READER_ROW_Y, PANEL_W - 2 * MARGIN, READER_ROW_H}, 3, GAP, i);
			return l;
		}

		PagesSheet pagesSheet()
		{
			PagesSheet p;
			// the tallest sheet: three rows a screenful under the header
			p.sheet = listSheetBody(SHEET_MAX);
			int top = p.sheet.rowsY + SP4, bottom = p.sheet.hints.y - SP8;
			p.view = {SHEET_PAD, top, PANEL_W - 2 * SHEET_PAD, bottom - top};
			return p;
		}

		PagesCell pagesCell(Box row, int col)
		{
			// the six cells spread across the row: the first at its left, the last at its right
			int spare = row.w - PAGES_COLS * PAGES_CELL_W;
			int x = row.x + (col * spare) / (PAGES_COLS - 1) + col * PAGES_CELL_W;
			PagesCell c;
			c.thumb = {x + (PAGES_CELL_W - PAGES_THUMB_W) / 2, row.y + SP4, PAGES_THUMB_W, PAGES_THUMB_H};
			c.number = {x, c.thumb.y + c.thumb.h + SP2, PAGES_CELL_W, 16};
			c.cell = {x, row.y, PAGES_CELL_W, c.number.y + c.number.h - row.y};
			return c;
		}

		Box viewChoices(Box row)
		{
			return {row.x + 150, row.y + 3, row.w - 150 - SP8, row.h - 6};
		}

		PlayerLayout playerLayout(int chips, bool repeatMark)
		{
			PlayerLayout l;
			Box page{MARGIN, 0, PANEL_W - 2 * MARGIN, 0};
			l.back = {10, 3, 56, 48};
			l.backHit = {0, 0, 70, 56};
			l.more = {PANEL_W - 10 - 56, 3, 56, 48};
			l.moreHit = {PANEL_W - 70, 0, 70, 56};
			l.title = {page.x, 58, page.w, 34};
			l.line = {page.x, 92, page.w, 40};
			l.lineHit = {page.x, 76, page.w, 56};	// (a key 40 tall: its touch area takes the title's lower half)
			l.bar = {page.x, 138, page.w, 14};
			l.barHit = {page.x - 8, 132, page.w + 16, 52};	// the whole row, down to the times
			l.times = {page.x, 160, page.w, 20};
			// the four rows 8 apart (transport 76, keys 56, chips 52, Next 50), the Next line 4 over the hint bar
			for (int i = 0; i < 5; i++) l.transport[i] = transportCell({page.x, 188, page.w, 76}, i);
			for (int i = 0; i < 4; i++) l.keys[i] = cell({page.x, 272, page.w, 56}, 4, GAP, i);
			l.chips = std::clamp(chips, 0, PLAYER_CHIPS_MAX);
			for (int i = 0; i < l.chips; i++) l.chip[i] = cell({page.x, 336, page.w, 52}, l.chips, GAP, i);
			l.next = {page.x, 396, page.w, 50};
			if (repeatMark)
			{
				// a song's five chips leave Repeat no place: its mark ends the Next line's row, a key's gap from the line
				l.next.w -= PLAYER_REPEAT_W + GAP;
				l.repeat = {page.x + page.w - PLAYER_REPEAT_W, l.next.y, PLAYER_REPEAT_W, l.next.h};
			}
			l.hints = {0, PANEL_H - HINTS_H, PANEL_W, HINTS_H};
			l.lock = {PANEL_W / 2 - 60, 150, 120, 120};
			return l;
		}

		QrPanel qrPanel(int w, int h)
		{
			QrPanel q;
			int margin = std::max(8, h / 24), gap = std::max(4, h / 60), lineH = std::max(12, h / 20);
			int side = std::max(0, std::min(w - 2 * margin, h - 2 * margin - gap - lineH));
			// the code is 76% of the white box: the ring round a 29-module code is nearly the four modules a phone's
			// camera wants to see the edges against (a code of 25 or 33 modules gets a little more or less)
			int pad = std::max(6, side * 3 / 25);
			q.quiet = {(w - side) / 2, margin, side, side};
			q.code = {q.quiet.x + pad, q.quiet.y + pad, std::max(0, side - 2 * pad), std::max(0, side - 2 * pad)};
			q.line = {0, q.quiet.y + side + gap, w, lineH};
			return q;
		}

		std::string chipTrack(const Track& t, int n, const std::vector<Track>& all)
		{
			std::string lang = languageName(t.lang);
			if (lang.empty()) return "Track " + std::to_string(n);
			int same = 0;
			for (auto& o : all)
				if (o.type == t.type && languageName(o.lang) == lang) same++;
			if (same < 2) return lang;
			// what tells it apart: its own title, else that it's forced, else its format
			std::string more = !t.title.empty() && t.title != lang && t.title != t.lang ? t.title : t.forced ? "Forced" : codecName(t.codec);
			return more.empty() ? lang : lang + " \xC2\xB7 " + more;
		}

		std::string caps(std::string s)
		{
			for (auto& c : s)
				if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
			return s;
		}

		// ==== the kit's variants ====

		StatusRow statusRow(int right, const std::vector<int>& widths, int gap)
		{
			StatusRow r;
			int x = right;
			for (int w : widths)
			{
				x -= w;
				r.x.push_back(x);
				x -= gap;
			}
			r.left = widths.empty() ? right : x + gap;
			return r;
		}

		int badgeW(int textW, bool glyph) { return 2 * BADGE_PAD + (glyph ? 16 + 4 : 0) + textW; }

		Shell shell()
		{
			Shell s;
			s.rail = {0, 0, RAIL_W, PANEL_H};
			s.main = {RAIL_W, 0, PANEL_W - RAIL_W, PANEL_H};
			s.header = {RAIL_W, 0, s.main.w, SHELL_HEAD};
			s.hints = {RAIL_W, PANEL_H - HINTS_H, s.main.w, HINTS_H};
			s.body = {RAIL_W, SHELL_HEAD + SP6, s.main.w, s.hints.y - SHELL_HEAD - SP6};
			return s;
		}

		PlaylistPage playlistPage(Box body, int keys, int barKeys)
		{
			PlaylistPage p;
			p.area = body;
			int x = body.x + MARGIN, w = body.w - 2 * MARGIN;
			int top = body.y + PL_PAD;
			p.keys = std::clamp(keys, 0, 3);
			if (p.keys == 2)
			{
				p.key[0] = cell({x, top, w, PL_KEYS_H}, 2, GAP, 0);
				p.key[1] = cell({x, top, w, PL_KEYS_H}, 2, GAP, 1);
			}
			else if (p.keys == 3)
			{
				// the Add key is 64 wide (the mock-up's ＋): Play and Shuffle share what is left equally, the
				// odd unit going to the last of them (lay::cell's rule)
				int addX = x + w - PL_ADD_W, rest = addX - GAP - x;
				p.key[0] = {x, top, (rest - GAP) / 2, PL_KEYS_H};
				p.key[1] = {x + p.key[0].w + GAP, top, rest - GAP - p.key[0].w, PL_KEYS_H};
				p.key[2] = {addX, top, PL_ADD_W, PL_KEYS_H};
			}
			p.rowsY = p.keys ? top + PL_KEYS_H + GAP : top;
			int rowsEnd = body.y + body.h;
			p.bar = {0, 0, 0, 0};
			p.barKeys = std::clamp(barKeys, 0, 3);
			if (p.barKeys)
			{
				p.bar = {x, body.y + body.h - PL_BAR_H, w, PL_BAR_H};
				rowsEnd = p.bar.y - PL_FOOT_GAP;
				if (p.barKeys == 2)
				{
					p.barKey[0] = cell({x, p.bar.y, w, PL_BAR_H}, 2, GAP, 0);
					p.barKey[1] = cell({x, p.bar.y, w, PL_BAR_H}, 2, GAP, 1);
				}
				else
				{
					// three keys, 6 apart, as the mock-up's bar
					int bw = (w - 2 * 6) / 3, last = w - 2 * (bw + 6);
					for (int i = 0; i < 3; i++)
						p.barKey[i] = {x + i * (bw + 6), p.bar.y, i == 2 ? last : bw, PL_BAR_H};
				}
			}
			p.rows = std::max(0, (rowsEnd - p.rowsY + (PL_ROW_PITCH - PL_ROW_H)) / PL_ROW_PITCH);
			int h = p.rows ? (p.rows - 1) * PL_ROW_PITCH + PL_ROW_H : 0;
			p.list = {x, p.rowsY, w, h};
			return p;
		}

		MiniBar miniBar()
		{
			return miniBar({MINI_SIDE, PANEL_H - MINI_FOOT - MINI_H, PANEL_W - 2 * MINI_SIDE, MINI_H});
		}

		MiniBar miniBar(Box box)
		{
			MiniBar m;
			m.box = box;
			m.mark = {box.x + 5, box.y + (box.h - MINI_MARK_H) / 2, MINI_MARK_W, MINI_MARK_H};
			m.line = {box.x + 2, box.y + box.h - 2 - 3, box.w - 4, 3};
			m.cy = box.y + (box.h - 3) / 2;
			m.kindX = m.mark.x + m.mark.w + 16;
			m.textX = m.kindX + 8 + 10;
			m.rightX = box.x + box.w - 12;
			return m;
		}

		FilterChips filterChips(Box area, const std::vector<int>& widths)
		{
			FilterChips c;
			int x = area.x, y = area.y + (area.h - CHIP_H) / 2;
			for (int w : widths)
			{
				c.chip.push_back({x, y, w, CHIP_H});
				// the touch area runs on to the next chip's start: no dead strip between two
				c.hit.push_back({x - CHIP_GAP / 2, area.y, w + CHIP_GAP, area.h});
				x += w + CHIP_GAP;
			}
			return c;
		}

		int filterAt(const FilterChips& c, int x, int y)
		{
			for (size_t i = 0; i < c.hit.size(); i++)
				if (c.hit[i].hit(x, y)) return (int)i;
			return -1;
		}

		FormRow formRow(Box row)
		{
			FormRow f;
			f.row = row;
			f.label = {row.x + 12, row.y, FORM_LABEL - 12, row.h};
			f.value = {row.x + FORM_LABEL + 12, row.y, row.w - FORM_LABEL - 24, row.h};
			return f;
		}

		void formKeys(Box out[2])
		{
			Box area{MARGIN, PANEL_H - HINTS_H - 8 - FORM_KEY_H, PANEL_W - 2 * MARGIN, FORM_KEY_H};
			for (int i = 0; i < 2; i++) out[i] = cell(area, 2, GAP, i);
		}

		AlertSheet alertSheet(int textLines)
		{
			// the sentence's lines (20 each), 14 clear, the key: the sheet's body is exactly them
			int textH = std::max(1, textLines) * 20;
			AlertSheet a;
			a.sheet = sheet({0, 0, 1, textH + 14 + FORM_KEY_H});
			a.key = {SHEET_PAD, a.sheet.hints.y - 8 - FORM_KEY_H, PANEL_W - 2 * SHEET_PAD, FORM_KEY_H};
			a.text = {SHEET_PAD + 2, a.sheet.bodyY + 4, PANEL_W - 2 * SHEET_PAD - 4, textH};
			return a;
		}

		ConfirmSheet confirmSheet(int textLines, bool foot, bool third)
		{
			int textH = std::max(1, textLines) * 20, footH = foot ? 28 : 0;
			ConfirmSheet c;
			// (240 at least: the confirm sheet is taller than the alert's)
			c.sheet = sheet({0, 0, 1, std::max(textH + 14 + FORM_KEY_H + footH, 240 - (11 + SHEET_HEAD + 8 + HINTS_H))});
			int keysY = c.sheet.hints.y - 8 - FORM_KEY_H - footH;
			Box area{SHEET_PAD, keysY, PANEL_W - 2 * SHEET_PAD, FORM_KEY_H};
			// three keys share the row (safe, action, third): the spare units to the last, as cell() does
			c.safe = cell(area, third ? 3 : 2, GAP, 0);
			c.action = cell(area, third ? 3 : 2, GAP, 1);
			c.third = third ? cell(area, 3, GAP, 2) : Box{0, 0, 0, 0};
			c.text = {SHEET_PAD + 2, c.sheet.bodyY + 4, PANEL_W - 2 * SHEET_PAD - 4, textH};
			c.foot = foot ? Box{SHEET_PAD, c.sheet.hints.y - 8 - footH + 4, PANEL_W - 2 * SHEET_PAD, footH - 4} : Box{0, 0, 0, 0};
			return c;
		}

		BusySheet busySheet(bool twoKeys)
		{
			BusySheet b;
			// the file line (20), the bar (12) 10 under it, the amounts (18) 8 under that, 14 clear, Cancel
			b.sheet = sheet({0, 0, 1, 20 + 10 + 12 + 8 + 18 + 14 + FORM_KEY_H});
			int y = b.sheet.bodyY + 4;
			b.file = {SHEET_PAD + 2, y, PANEL_W - 2 * SHEET_PAD - 4, 20};
			b.bar = {SHEET_PAD + 2, y + 30, PANEL_W - 2 * SHEET_PAD - 4, 12};
			b.amounts = {SHEET_PAD + 2, y + 50, PANEL_W - 2 * SHEET_PAD - 4, 18};
			Box row{SHEET_PAD, b.sheet.hints.y - 8 - FORM_KEY_H, PANEL_W - 2 * SHEET_PAD, FORM_KEY_H};
			b.key = twoKeys ? cell(row, 2, GAP, 0) : row;
			b.key2 = twoKeys ? cell(row, 2, GAP, 1) : Box{0, 0, 0, 0};
			return b;
		}

		SearchResults searchResults(Box body, const std::vector<int>& tabWidths, int capW, bool withTabs, bool withNotice)
		{
			SearchResults s;
			s.field = {body.x, body.y, body.w, SEARCH_FIELD_H};
			int y = body.y + SEARCH_FIELD_H;
			if (withTabs)
			{
				s.strip = tabs({body.x, y + SP6, body.w, TAB_H}, tabWidths, TAB_GAP, capW);
				y += SP6 + TAB_H;
			}
			if (withNotice)
			{
				s.notice = notice({body.x, y + SP6, body.w, 0}, true, true, 0).row;
				y = s.notice.y + s.notice.h;
			}
			y += SP4;
			s.list = {body.x, y, body.w, std::max(0, body.y + body.h - y)};
			return s;
		}

		Keyboard keyboard(Box area, const std::vector<int>& specialW, const std::vector<int>& counts, int toggles)
		{
			Keyboard k;
			int y = area.y;
			k.field = {area.x, y, area.w, KB_FIELD_H};
			y += KB_FIELD_H + 8;
			int x = area.x;
			for (int w : specialW)
			{
				k.special.push_back({x, y, w, KB_SPECIAL_H});
				x += w + GAP;
			}
			// no pieces: no special row (the mock-up's Search page goes straight from the field to the letters)
			if (!specialW.empty()) y += KB_SPECIAL_H + 10;	// (10, not the mock-up's 18: the number row needs the room)
			// the letters: every key the width the widest row fits in the area, so no row runs past its edge
			int widest = 1;
			for (int n : counts) widest = std::max(widest, n);
			int keyW = (area.w - (widest - 1) * KB_GAP) / widest;
			for (int n : counts)
			{
				std::vector<Box> row;
				int w = n * keyW + std::max(0, n - 1) * KB_GAP, x0 = area.x + (area.w - w) / 2;
				for (int i = 0; i < n; i++) row.push_back({x0 + i * (keyW + KB_GAP), y, keyW, KB_KEY_H});
				k.rows.push_back(row);
				y += KB_KEY_H + KB_GAP;
			}
			y += 6;
			// the toggles, then Space (the rest), Clear, Del and Done: the row ends on the area's right edge
			int left = area.x, right = area.x + area.w;
			int spaceW = right - left - toggles * (KB_TOGGLE_W + KB_GAP) - 2 * (KB_GAP + KB_TOGGLE_W) - (KB_GAP + KB_DONE_W);
			int bx = left;
			for (int i = 0; i < toggles; i++)
			{
				k.bottom.push_back({bx, y, KB_TOGGLE_W, KB_BOTTOM_H});
				bx += KB_TOGGLE_W + KB_GAP;
			}
			k.bottom.push_back({bx, y, spaceW, KB_BOTTOM_H});
			bx += spaceW + KB_GAP;
			for (int i = 0; i < 2; i++)
			{
				k.bottom.push_back({bx, y, KB_TOGGLE_W, KB_BOTTOM_H});
				bx += KB_TOGGLE_W + KB_GAP;
			}
			k.bottom.push_back({bx, y, right - bx, KB_BOTTOM_H});
			k.note = {area.x, y + KB_BOTTOM_H + 8, area.w, 20};
			return k;
		}

		namespace
		{
			// each row of keys as a list of boxes: the specials, the letter rows, the bottom row
			std::vector<const std::vector<Box>*> keyRows(const Keyboard& k)
			{
				std::vector<const std::vector<Box>*> rows;
				if (!k.special.empty()) rows.push_back(&k.special);
				for (auto& r : k.rows) rows.push_back(&r);
				rows.push_back(&k.bottom);
				return rows;
			}
		}

		int keyboardKeys(const Keyboard& k)
		{
			int n = 0;
			for (auto* r : keyRows(k)) n += (int)r->size();
			return n;
		}

		int keyboardHit(const Keyboard& k, int x, int y)
		{
			int i = 0;
			for (auto* r : keyRows(k))
				for (const Box& b : *r)
				{
					if (b.hit(x, y)) return i;
					i++;
				}
			return -1;
		}

		int keyboardMove(const Keyboard& k, int from, int dx, int dy)
		{
			auto rows = keyRows(k);
			int start = 0, g = 0;
			while (g + 1 < (int)rows.size() && from >= start + (int)rows[g]->size()) start += (int)rows[g++]->size();
			int n = (int)rows[g]->size(), at = std::clamp(from - start, 0, n - 1);
			if (dx) return start + ((at + dx) % n + n) % n;
			if (!dy) return from;
			int ng = std::clamp(g + dy, 0, (int)rows.size() - 1);
			if (ng == g) return from;
			const Box& cur = (*rows[g])[at];
			int cx = cur.x + cur.w / 2, best = 0, bestD = 1 << 30, nstart = 0;
			for (int r = 0; r < ng; r++) nstart += (int)rows[r]->size();
			for (int i = 0; i < (int)rows[ng]->size(); i++)
			{
				const Box& b = (*rows[ng])[i];
				int d = std::abs(b.x + b.w / 2 - cx);
				if (d < bestD) { bestD = d; best = i; }
			}
			return nstart + best;
		}

		VolumeBar volumeBar(Box area, int max)
		{
			VolumeBar v;
			v.number = {area.x + area.w - 64, area.y, 64, area.h};
			v.bar = {area.x, area.y + (area.h - 12) / 2, area.w - 64 - 12, 12};
			v.tickX = max > 100 ? volumeX(v.bar, 100, max) : -1;
			return v;
		}

		int volumeX(Box bar, int value, int max)
		{
			if (max <= 0) return bar.x;
			return bar.x + (int)((long long)bar.w * std::clamp(value, 0, max) / max);
		}

		int volumeZone(int value) { return value > 120 ? 2 : value > 100 ? 1 : 0; }

		ControlSheet controlSheet(const std::vector<int>& kinds)
		{
			auto heightOf = [](int k) { return k == LC_SLIDER ? LC_SLIDER_H : k == LC_CHOICE ? LC_CHOICE_H : SHEET_ROW; };
			int body = 4;
			for (int k : kinds) body += heightOf(k) + GAP;
			ControlSheet c;
			SheetSpec spec;
			spec.body = body;
			c.sheet = sheet(spec);
			int y = c.sheet.bodyY + 4, w = PANEL_W - 2 * SHEET_PAD;
			for (int k : kinds)
			{
				Box r{SHEET_PAD, y, w, heightOf(k)};
				c.row.push_back(r);
				c.track.push_back(k == LC_SLIDER ? Box{r.x + 2, r.y + 32, r.w - 4, 14} : Box{0, 0, 0, 0});
				c.seg.push_back(k == LC_CHOICE ? Box{r.x, r.y + 26, r.w, 40} : Box{0, 0, 0, 0});
				y += r.h + GAP;
			}
			return c;
		}

		Box tileCell(Box area, int n, int cols, int h, int i)
		{
			cols = std::max(1, std::min(cols, std::max(1, n)));
			Box c = cell({area.x, area.y, area.w, h}, cols, GAP, i % cols);
			c.y = area.y + i / cols * (h + GAP);
			return c;
		}

		ContinueCard continueCard(Box box)
		{
			ContinueCard c;
			c.box = box;
			c.pic = {box.x + 12, box.y + (box.h - CONTINUE_PIC) / 2, CONTINUE_PIC, CONTINUE_PIC};	// square
			c.key = {box.x + box.w - 12 - 118, box.y + (box.h - 52) / 2, 118, 52};
			int x = c.pic.x + c.pic.w + 14, w = std::max(0, c.key.x - 12 - x);
			c.label = {x, box.y + 14, w, 14};
			c.title = {x, box.y + 33, w, 28};
			c.note = {x, box.y + 63, w, 16};
			c.bar = {x, box.y + 88, w, 6};
			return c;
		}

		HomePage homePage(Box body, bool hasContinue, bool hasRecent, int cards, bool more, const std::vector<int>& chipWidths)
		{
			HomePage h;
			h.cont = h.chipStrip = h.more = {0, 0, 0, 0};
			for (auto& c : h.card) c = {0, 0, 0, 0};
			for (auto& s : h.shortcut) s = {0, 0, 0, 0};
			h.inner = {body.x + HOME_PAD, body.y, body.w - 2 * HOME_PAD, body.h};
			h.hasContinue = hasContinue;
			h.cards = std::clamp(cards, 0, HOME_CARDS_MAX);
			int y = h.inner.y;
			if (hasContinue)
			{
				h.cont = {h.inner.x, y, h.inner.w, HOME_CONTINUE_H};
				y += HOME_CONTINUE_H + SP12;
			}
			// a filter row over the cards (kept even when the chosen kind leaves none: that is how it is changed);
			// 44 tall, the chips' own height, a finger's size
			if (hasRecent)
			{
				h.chipStrip = {h.inner.x, y, h.inner.w, CHIP_STRIP};
				h.chips = filterChips(h.chipStrip, chipWidths);
				y += CHIP_STRIP + SP8;
			}
			for (int i = 0; i < h.cards; i++) h.card[i] = {h.inner.x + i * (RECENT_W + RECENT_GAP), y, RECENT_W, HOME_CARD_ROW_H};
			if (more && h.cards > 0)
			{
				// the last cell of the row: the way to all of them, on the column's right edge, as tall as the pictures
				h.more = {h.inner.x + h.inner.w - HOME_SHORTCUT_H, y, HOME_SHORTCUT_H, RECENT_W};
			}
			if (h.cards > 0 || more) y += HOME_CARD_ROW_H + SP8;
			// the three shortcuts, unless the mini bar has taken the room (the row then ends above it)
			int bottom = h.inner.y + h.inner.h;
			if (hasRecent && y + HOME_SHORTCUT_H <= bottom)
			{
				Box row{h.inner.x, y, h.inner.w, HOME_SHORTCUT_H};
				for (int i = 0; i < 3; i++) h.shortcut[i] = cell(row, 3, GAP, i);
				h.shortcuts = 3;
			}
			return h;
		}

		AllRecentPage allRecentPage(Box body)
		{
			AllRecentPage p;
			// this page is still the old list's: its first row must stay at Ui::ROW_Y (84), so the 28-tall kind chips
			// stand in the 6 between the header and the body rather than under it
			p.inner = {body.x + HOME_PAD, body.y - SP4, body.w - 2 * HOME_PAD, body.h + SP2};
			p.chipStrip = {p.inner.x, p.inner.y, p.inner.w, 28};
			p.rowY = SHELL_HEAD + 30;
			p.shown = std::clamp((p.inner.y + p.inner.h - p.rowY) / p.rowH, 0, 7);
			return p;
		}

		EmptyState emptyState(Box page, int lineCount, int keys)
		{
			EmptyState e;
			e.keys = std::clamp(keys, 0, 2);
			int linesH = std::max(0, lineCount) * 20;
			int total = 54 + 12 + 28 + (linesH ? 6 + linesH : 0) + (e.keys ? 18 + FORM_KEY_H : 0);
			int y = page.y + std::max(0, (page.h - total) / 2), cx = page.x + page.w / 2;
			e.icon = {cx - 27, y, 54, 54};
			y += 54 + 12;
			e.title = {cx - 200, y, 400, 28};
			y += 28 + 6;
			e.lines = {cx - 200, y, 400, linesH};
			y += linesH ? linesH + 18 : 12;
			if (e.keys == 1) e.key[0] = {cx - 100, y, 200, FORM_KEY_H};
			if (e.keys == 2)
			{
				e.key[0] = {cx - 206, y, 200, FORM_KEY_H};
				e.key[1] = {cx + 6, y, 200, FORM_KEY_H};
			}
			return e;
		}

		Notice notice(Box area, bool second, bool bar, int keyW)
		{
			Notice n;
			n.hasSecond = second;
			n.hasBar = bar;
			n.hasKey = keyW > 0;
			int h = second && keyW > 0 ? 60 : 52;
			n.row = {area.x, area.y, area.w, h};
			n.icon = {area.x + 12, area.y + (h - 20) / 2, 20, 20};
			if (keyW > 0) n.key = {area.x + area.w - 12 - keyW, area.y + (h - 44) / 2, keyW, 44};
			int x = area.x + 44, right = keyW > 0 ? n.key.x - 12 : area.x + area.w - 12, w = std::max(0, right - x);
			int block = 18 + (second ? 16 : 0) + (bar ? 5 + 6 : 0), top = area.y + (h - block) / 2;
			n.text = {x, top, w, 18};
			if (second) n.second = {x, top + 18, w, 16};
			if (bar) n.bar = {x, top + block - 5, w, 5};
			return n;
		}

		void selectBar(Box out[4])
		{
			Box area{MARGIN, PANEL_H - HINTS_H - 8 - SELBAR_H, PANEL_W - 2 * MARGIN, SELBAR_H};
			for (int i = 0; i < 4; i++) out[i] = cell(area, 4, GAP, i);
		}

		ClipBar clipBar()
		{
			ClipBar c;
			c.box = {MARGIN, PANEL_H - HINTS_H - 8 - CLIPBAR_H, PANEL_W - 2 * MARGIN, CLIPBAR_H};
			int y = c.box.y + (CLIPBAR_H - CLIP_KEY_H) / 2;
			c.key[1] = {c.box.x + c.box.w - 8 - 110, y, 110, CLIP_KEY_H};
			c.key[0] = {c.key[1].x - 8 - 170, y, 170, CLIP_KEY_H};
			c.text = {c.box.x + 14, c.box.y, c.key[0].x - 10 - (c.box.x + 14), CLIPBAR_H};
			return c;
		}

		MarqueeAt marquee(int ms, int textW, int boxW, int fontSize, int passes)
		{
			MarqueeAt m;
			const int dist = textW - boxW;
			if (dist <= 0 || boxW <= 0)
			{
				m.done = true;
				return m;
			}
			const int per = fontSize >= 24 ? 2 : 1;	// units a step
			const int moveSteps = (dist + per - 1) / per, moveMs = moveSteps * 1000 / MARQ_STEPS;
			const int pass = MARQ_REST + moveMs + MARQ_END_REST + 2 * MARQ_FADE;	// (then the gap before the next)
			ms = std::max(0, ms);
			int p = 0;
			while (p < passes - 1 && ms >= pass + MARQ_GAP - MARQ_REST)
			{
				// (the next pass's first rest is the gap's last 1.5 s: the 3 s rest counts from the fade's end)
				ms -= pass + MARQ_GAP - MARQ_REST;
				p++;
			}
			m.fadeRight = true;
			if (ms >= pass)
			{
				m.done = p >= passes - 1;
				m.step = (p + 1) * 100000;
				return m;	// at its start, resting (or done)
			}
			int t = ms - MARQ_REST;
			int step = 0;
			if (t >= 0)
			{
				step = std::min(moveSteps, t * MARQ_STEPS / 1000 + 1);
				m.offset = std::min(dist, step * per);
				m.offsetF = std::min((float)dist, (float)t * per * MARQ_STEPS / 1000.0f);
				if (t >= moveMs + MARQ_END_REST)
				{
					// the fade back: out at its end, in at its start
					const int f = t - moveMs - MARQ_END_REST;
					if (f < MARQ_FADE) m.alpha = 1 - (float)f / MARQ_FADE, m.offsetF = (float)dist;
					else
					{
						m.offset = 0;
						m.offsetF = 0;
						m.alpha = (float)(f - MARQ_FADE) / MARQ_FADE;
					}
					step = moveSteps + 1 + f / 25;	// (the fades in 25 ms steps)
				}
			}
			m.fadeLeft = m.offset > 0;
			m.fadeRight = m.offset < dist;
			m.step = p * 100000 + step;
			return m;
		}

		// ---- Files ----

		PlaceRow placeRow(Box row, bool capacity)
		{
			PlaceRow l{};
			l.card = {row.x, row.y + (row.h - PLACE_CARD_H) / 2, row.w, PLACE_CARD_H};
			int x = l.card.x + 14 + 20 + 12, right = l.card.x + l.card.w - 14;
			if (!capacity)
			{
				l.icon = {l.card.x + 14, l.card.y + (PLACE_CARD_H - 20) / 2, 20, 20};
				l.name = {x, l.card.y + 16, right - x, 22};
				l.detail = {x, l.card.y + 42, right - x, 16};
				return l;
			}
			l.icon = {l.card.x + 14, l.card.y + 11, 20, 20};
			l.free = {right - PLACE_FREE_W, l.card.y + 10, PLACE_FREE_W, 22};
			l.name = {x, l.card.y + 10, l.free.x - SP8 - x, 22};
			l.bar = {x, l.card.y + 40, right - x, PLACE_BAR_H};
			l.detail = {x, l.card.y + 51, right - x, 16};
			return l;
		}

		FilesPage filesPage(Box body, int rows, bool select, bool footer, int barKeys, int rowH)
		{
			FilesPage f{};
			f.body = body;
			f.rowH = std::max(1, rowH);
			// the page's own side margin inside the shell's main (the rail is already out)
			Box inner{body.x + MARGIN, body.y, body.w - 2 * MARGIN, body.h};
			f.rows = inner;
			if (select)
			{
				Box bar{inner.x, body.y + body.h - FILE_SELBAR_H, inner.w, FILE_SELBAR_H};
				for (int i = 0; i < std::clamp(barKeys, 1, 4); i++) f.bar[i] = cell(bar, std::clamp(barKeys, 1, 4), GAP, i);
				f.rows.h = bar.y - FILE_SELBAR_GAP - body.y;
			}
			if (footer && !select)
			{
				f.footer = {inner.x, body.y + body.h - FILE_FOOT_H, inner.w, FILE_FOOT_H};
				f.rows.h = body.h - FILE_FOOT_H;
			}
			f.shown = std::clamp(rows, 0, std::max(0, f.rows.h / f.rowH));
			return f;
		}

		FilesHeader filesHeader(Box header)
		{
			FilesHeader f{};
			int cy = header.y + header.h / 2;
			f.back = {header.x + 10, header.y + 3, 56, 48};
			f.backHit = {header.x, header.y, 70, header.h};
			f.more = {header.x + header.w - 66, header.y + 3, 56, 48};
			f.moreHit = {header.x + header.w - 70, header.y, 70, header.h};
			// the title's line and the crumb's under it, beside the back key (the shell's 54-tall strip)
			f.title = {f.back.x + f.back.w + 10, header.y + 4, f.more.x - (f.back.x + f.back.w + 10) - 8, 28};
			f.crumb = {f.title.x, cy + 2, f.title.w, 18};
			f.right = f.title.x + f.title.w;
			return f;
		}

		FilesClip filesClipBar(Box body)
		{
			FilesClip c{};
			c.box = {body.x + MARGIN, body.y + body.h - CLIPBAR_H, body.w - 2 * MARGIN, CLIPBAR_H};
			int y = c.box.y + (CLIPBAR_H - CLIP_KEY_H) / 2;
			c.key[1] = {c.box.x + c.box.w - 8 - 110, y, 110, CLIP_KEY_H};
			c.key[0] = {c.key[1].x - 8 - 170, y, 170, CLIP_KEY_H};
			c.text = {c.box.x + 14, c.box.y, c.key[0].x - 10 - (c.box.x + 14), CLIPBAR_H};
			return c;
		}

		Box fileInfoRow(Box body, int i)
		{
			return {body.x + MARGIN, body.y + 8 + i * FILE_INFO_ROW, body.w - 2 * MARGIN, FILE_INFO_ROW};
		}

		int fileInfoShown(Box body) { return std::max(0, (body.h - 16) / FILE_INFO_ROW); }

		// ---- Extensions, the rail's section ----

		ExtPage extPage(Box body, int extensions)
		{
			ExtPage p;
			p.area = {body.x + EXT_PAGE_PAD, body.y, body.w - 2 * EXT_PAGE_PAD, body.h};
			int y = p.area.y;
			for (int i = 0; i < extensions; i++)
			{
				p.row.push_back({p.area.x, y, p.area.w, EXT_ROW_H});
				y += EXT_ROW_H + EXT_ROW_GAP;
			}
			// the mock-up's rule (margin 10, 2 from the sides), then Manage extensions
			p.rule = {p.area.x + 2, y + 2, p.area.w - 4, 1};
			p.manage = {p.area.x, p.rule.y + 1 + EXT_RULE_GAP, p.area.w, EXT_MANAGE_H};
			return p;
		}

		ExtEmpty extEmpty(Box body)
		{
			// the empty state's stack, centred (as lay::emptyState centres its own) with the mock-up's 240-wide key
			ExtEmpty e;
			int total = 54 + 12 + 28 + 6 + 40 + 18 + FORM_KEY_H;
			int y = body.y + std::max(0, (body.h - total) / 2), cx = body.x + body.w / 2;
			e.icon = {cx - 27, y, 54, 54};
			y += 54 + 12;
			e.title = {cx - 200, y, 400, 28};
			y += 28 + 6;
			e.lines = {cx - 200, y, 400, 40};
			y += 40 + 18;
			e.key = {cx - EXT_EMPTY_KEY_W / 2, y, EXT_EMPTY_KEY_W, FORM_KEY_H};
			return e;
		}

		// ---- Quick settings (F) ----

		QuickPanel quickPanel(int levels, int tiles, int webTile, bool webKey)
		{
			QuickPanel q;
			int tileRows = (tiles + 1) / 2;
			// apart from the level rows' own heights: the padding, the title, the block, the tiles and Quit,
			// and one gap between each two sections (the block and Quit are always there, so sections + 2)
			int sections = (levels ? 1 : 0) + (tiles ? 1 : 0);
			int rest = 2 * QUICK_PAD + QUICK_TITLE_H + QUICK_NOW_H + QUICK_QUIT_H +
				tileRows * QUICK_TILE_H + (tileRows > 1 ? (tileRows - 1) * QUICK_TILE_GAP : 0) +
				(sections + 2) * QUICK_GAP;
			// the level rows share the height that is left: 52 while they fit, less when they don't
			q.rowH = levels ? std::clamp((PANEL_H - HINTS_H - rest - (levels - 1) * QUICK_GAP) / levels,
				QUICK_MIN_ROW, QUICK_ROW_H) : 0;
			int h = rest + levels * q.rowH + (levels > 1 ? (levels - 1) * QUICK_GAP : 0);
			q.box = {QUICK_SIDE, std::max(0, (PANEL_H - HINTS_H - h) / 2), QUICK_W, h};
			int x = q.box.x + QUICK_PAD, w = q.box.w - 2 * QUICK_PAD;
			q.title = {x, q.box.y + QUICK_PAD, w, QUICK_TITLE_H};
			q.now = {x, q.title.y + q.title.h + QUICK_GAP, w, QUICK_NOW_H};
			q.nowArt = {q.now.x + SP8, q.now.y + SP8, QUICK_NOW_ART, QUICK_NOW_ART};
			q.nowNext = {q.now.x + q.now.w - SP8 - QUICK_NOW_STEP_W, q.now.y + SP8, QUICK_NOW_STEP_W, QUICK_NOW_KEY_H};
			q.nowPlay = {q.nowNext.x - SP8 - QUICK_NOW_PLAY_W, q.now.y + SP8, QUICK_NOW_PLAY_W, QUICK_NOW_KEY_H};
			q.nowPrev = {q.nowPlay.x - SP8 - QUICK_NOW_STEP_W, q.now.y + SP8, QUICK_NOW_STEP_W, QUICK_NOW_KEY_H};
			int textX = q.nowArt.x + q.nowArt.w + SP12;
			q.nowText = {textX, q.nowArt.y, q.nowPrev.x - SP12 - textX, q.nowArt.h};
			q.nowLine = {textX, q.nowArt.y + q.nowArt.h - QUICK_NOW_LINE_H, q.nowText.w, QUICK_NOW_LINE_H};
			int y = q.now.y + q.now.h + QUICK_GAP;
			q.level.resize(levels);
			for (int i = 0; i < levels; i++)
			{
				QuickLevel& l = q.level[i];
				l.row = {x, y + i * (q.rowH + QUICK_GAP), w, q.rowH};
				int mid = l.row.y + l.row.h / 2, stepH = std::min(QUICK_STEP_H, l.row.h);
				l.icon = {l.row.x + 12, l.row.y, 20, l.row.h};	// (the icon's centre: x + 22)
				l.label = {l.row.x + 36, l.row.y, QUICK_LABEL_W, l.row.h};
				l.minus = {l.label.x + l.label.w + SP8, mid - stepH / 2, QUICK_STEP_W, stepH};
				l.number = {l.row.x + l.row.w - SP12 - QUICK_NUM_W, mid - 12, QUICK_NUM_W, 24};
				l.plus = {l.number.x - SP8 - QUICK_STEP_W, mid - stepH / 2, QUICK_STEP_W, stepH};
				l.bar = {l.minus.x + QUICK_STEP_W + SP8, l.row.y,
					l.plus.x - SP8 - (l.minus.x + QUICK_STEP_W + SP8), l.row.h};
			}
			if (tiles)
			{
				Box area{x, y + (levels ? levels * q.rowH + (levels - 1) * QUICK_GAP + QUICK_GAP : 0), w,
					tileRows * QUICK_TILE_H + (tileRows - 1) * QUICK_TILE_GAP};
				q.tile.reserve(tiles);
				for (int i = 0; i < tiles; i++) q.tile.push_back(tileCell(area, tiles, 2, QUICK_TILE_H, i));
				if (webTile >= 0 && webTile < tiles && webKey)
				{
					// the Web tile's own key, 40 x 40 between its name and its switch (the switch and its 12
					// of padding are the tile's last 52): RIGHT, then A reaches the dashboard's page from
					// here. A chevron alone, as any row's "this leads to a page" — its label "Web dashboard"
					// measures 121 and this leaves its name 122, where a labelled key would cut it to 73
					Box& t = q.tile[webTile];
					q.webKey = {t.x + t.w - SP12 - 40 - SP8 - 40, t.y + (QUICK_TILE_H - 40) / 2, 40, 40};
				}
			}
			q.quit = {x, q.box.y + q.box.h - QUICK_PAD - QUICK_QUIT_H, w, QUICK_QUIT_H};
			return q;
		}

		// ==== the library's pages (kitlayout.h's 2026-11-19 third block) ====

		PageHead pageHead(Box body, int lines, int keys)
		{
			PageHead h;
			h.lines = std::clamp(lines, 0, 2);
			h.keys = std::clamp(keys, 0, 3);
			h.art = {body.x, body.y, PAGEHEAD_ART, PAGEHEAD_ART};
			int tx = body.x + PAGEHEAD_ART + SP12, tw = std::max(1, body.x + body.w - tx);
			h.title = {tx, body.y, tw, PAGEHEAD_TITLE_H};
			h.line[0] = h.lines >= 1 ? Box{tx, body.y + PAGEHEAD_TITLE_H + SP4, tw, PAGEHEAD_LINE1_H} : Box{tx, body.y, tw, 0};
			h.line[1] = h.lines >= 2 ? Box{tx, h.line[0].y + PAGEHEAD_LINE1_H + SP4, tw, PAGEHEAD_LINE2_H} : Box{tx, body.y, tw, 0};
			// the keys, as lay::playlistPage lays its own: Play and Shuffle share what is left of the Add key
			// equally, the odd unit going to the last of them
			int ky = body.y + PAGEHEAD_ART + SP8, w = body.w;
			if (h.keys == 3)
			{
				int addX = body.x + w - PL_ADD_W, rest = addX - GAP - body.x;
				h.key[0] = {body.x, ky, (rest - GAP) / 2, PL_KEYS_H};
				h.key[1] = {body.x + h.key[0].w + GAP, ky, rest - GAP - h.key[0].w, PL_KEYS_H};
				h.key[2] = {addX, ky, PL_ADD_W, PL_KEYS_H};
			}
			else if (h.keys == 2)
			{
				h.key[0] = cell({body.x, ky, w, PL_KEYS_H}, 2, GAP, 0);
				h.key[1] = cell({body.x, ky, w, PL_KEYS_H}, 2, GAP, 1);
			}
			else for (auto& k : h.key) k = {0, 0, 0, 0};
			h.content = {body.x, ky + PL_KEYS_H + SP6, w, std::max(0, body.y + body.h - (ky + PL_KEYS_H + SP6))};
			return h;
		}

		TrackRow trackRow(Box row)
		{
			TrackRow t;
			t.row = row;
			t.number = {row.x + SP12, row.y, 20, row.h};
			int x = t.number.x + 20 + SP12;
			t.name = {x, row.y, std::max(1, row.x + row.w - SP12 - x), row.h};
			t.right = row.x + row.w - SP12;
			return t;
		}

		PlaylistRow playlistRow(Box row)
		{
			PlaylistRow p;
			p.row = row;
			p.icon = {row.x + SP12, row.y + (row.h - 20) / 2, 20, 20};
			int x = p.icon.x + p.icon.w + SP12;
			p.name = {x, row.y, std::max(1, row.x + row.w - SP12 - x), row.h};
			p.right = row.x + row.w - SP12;
			return p;
		}
	}
}
