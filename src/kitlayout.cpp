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

		Tabs tabs(Box area, const std::vector<int>& widths, int gap)
		{
			Tabs out;
			out.area = area;
			// the strip 44 tall in the header it is given (54), its caps 19 tall in the strip's middle
			int y = area.y + (area.h - TAB_H) / 2, cy = y + (TAB_H - TAB_CAP) / 2;
			out.left = {area.x, cy, TAB_CAP, TAB_CAP};
			int x = area.x + TAB_CAP + (widths.empty() ? 0 : gap);
			for (int w : widths)
			{
				out.tab.push_back({x, y, w, TAB_H});
				x += w + gap;
			}
			out.right = {widths.empty() ? area.x + TAB_CAP + gap : x, cy, TAB_CAP, TAB_CAP};
			return out;
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
			int h = 11 + SHEET_HEAD + s.body + (s.tiles ? SHEET_TILE + GAP : 0) + lines * SHEET_ROW + 8 + HINTS_H;
			h = std::clamp(h, SHEET_MIN, SHEET_MAX);
			out.box = {0, PANEL_H - h, PANEL_W, h};
			out.grab = {PANEL_W / 2 - 22, out.box.y + 7, 44, 4};
			out.head = {SHEET_PAD, out.box.y + 11, PANEL_W - 2 * SHEET_PAD, SHEET_HEAD};
			out.bodyY = out.head.y + SHEET_HEAD;
			out.tilesY = out.bodyY + s.body;
			out.rowsY = out.tilesY + (s.tiles ? SHEET_TILE + GAP : 0);
			out.hints = {0, PANEL_H - HINTS_H, PANEL_W, HINTS_H};
			return out;
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
			int cols = std::max(1, s.cols), n = s.tiles + s.rows;
			if (n <= 0) return 0;
			cursor = std::clamp(cursor, 0, n - 1);
			if (cursor < s.tiles)
			{
				if (d == Dir::Left) return std::max(0, cursor - 1);
				if (d == Dir::Right) return std::min(s.tiles - 1, cursor + 1);
				if (d == Dir::Up || !s.rows) return cursor;
				// down: the column under the tile (four tiles over two columns: the left two, the right two)
				int col = std::min(cols - 1, cursor * cols / s.tiles);
				return s.tiles + std::min(s.rows - 1, col);
			}
			int r = cursor - s.tiles, col = r % cols, line = r / cols;
			switch (d)
			{
			case Dir::Left: return col > 0 ? cursor - 1 : cursor;
			case Dir::Right: return col + 1 < cols && r + 1 < s.rows ? cursor + 1 : cursor;
			case Dir::Down: return r + cols < s.rows ? cursor + cols : cursor;
			default:
				if (line > 0) return cursor - cols;
				// up from the first line: the first tile over that column
				return s.tiles ? std::min(s.tiles - 1, (col * s.tiles + cols - 1) / cols) : cursor;
			}
		}

		// ---- View › Display on a song ----

		DisplayPage displayPage(int styles)
		{
			DisplayPage d;
			styles = std::max(0, styles);
			int lines = (styles + DISPLAY_COLS - 1) / DISPLAY_COLS;
			int grid = lines * DISPLAY_TILE_H + std::max(0, lines - 1) * DISPLAY_STYLE_GAP;
			// the label 16 and 4, the modes, 8, the style's line 16 and 4, the grid: as tall as the sheet leaves
			int body = DISPLAY_H - (11 + SHEET_HEAD + 8 + HINTS_H);
			d.sheet = sheet({0, 0, 1, body});
			int y = d.sheet.bodyY + std::max(0, (body - (16 + 4 + DISPLAY_TILE_H + 8 + 16 + 4 + grid)) / 2);
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

		SettingsRows settingsRows(int shown)
		{
			SettingsRows l;
			l.area = {MARGIN, SET_ROW_TOP, PANEL_W - 2 * MARGIN, shown * SET_ROW_H - 2};
			l.help = {24, SET_HELP_Y, PANEL_W - 2 * 24, 2 * 22};
			return l;
		}

		PlayerLayout playerLayout(int chips)
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
			for (int i = 0; i < 5; i++) l.transport[i] = transportCell({page.x, 188, page.w, 74}, i);
			for (int i = 0; i < 4; i++) l.keys[i] = cell({page.x, 272, page.w, 56}, 4, GAP, i);
			l.chips = std::clamp(chips, 0, PLAYER_CHIPS_MAX);
			for (int i = 0; i < l.chips; i++) l.chip[i] = cell({page.x, 338, page.w, 52}, l.chips, GAP, i);
			l.next = {page.x, 398, page.w, 50};
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

		Shell shell(bool mini)
		{
			Shell s;
			s.rail = {0, 0, RAIL_W, PANEL_H};
			s.main = {RAIL_W, 0, PANEL_W - RAIL_W, PANEL_H};
			s.header = {RAIL_W, 0, s.main.w, SHELL_HEAD};
			s.hints = {RAIL_W, PANEL_H - HINTS_H, s.main.w, HINTS_H};
			int foot = s.hints.y;
			if (mini)
			{
				s.mini = {RAIL_W + MINI_SIDE, PANEL_H - MINI_FOOT - MINI_H, s.main.w - 2 * MINI_SIDE, MINI_H};
				foot = s.mini.y - 4;
			}
			s.body = {RAIL_W, SHELL_HEAD, s.main.w, foot - SHELL_HEAD};
			return s;
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

		ConfirmSheet confirmSheet(int textLines, bool foot)
		{
			int textH = std::max(1, textLines) * 20, footH = foot ? 28 : 0;
			ConfirmSheet c;
			// (240 at least: the confirm sheet is taller than the alert's)
			c.sheet = sheet({0, 0, 1, std::max(textH + 14 + FORM_KEY_H + footH, 240 - (11 + SHEET_HEAD + 8 + HINTS_H))});
			int keysY = c.sheet.hints.y - 8 - FORM_KEY_H - footH;
			Box area{SHEET_PAD, keysY, PANEL_W - 2 * SHEET_PAD, FORM_KEY_H};
			c.safe = cell(area, 2, GAP, 0);
			c.action = cell(area, 2, GAP, 1);
			c.text = {SHEET_PAD + 2, c.sheet.bodyY + 4, PANEL_W - 2 * SHEET_PAD - 4, textH};
			c.foot = foot ? Box{SHEET_PAD, c.sheet.hints.y - 8 - footH + 4, PANEL_W - 2 * SHEET_PAD, footH - 4} : Box{0, 0, 0, 0};
			return c;
		}

		BusySheet busySheet()
		{
			BusySheet b;
			// the file line (20), the bar (12) 10 under it, the amounts (18) 8 under that, 14 clear, Cancel
			b.sheet = sheet({0, 0, 1, 20 + 10 + 12 + 8 + 18 + 14 + FORM_KEY_H});
			int y = b.sheet.bodyY + 4;
			b.file = {SHEET_PAD + 2, y, PANEL_W - 2 * SHEET_PAD - 4, 20};
			b.bar = {SHEET_PAD + 2, y + 30, PANEL_W - 2 * SHEET_PAD - 4, 12};
			b.amounts = {SHEET_PAD + 2, y + 50, PANEL_W - 2 * SHEET_PAD - 4, 18};
			b.key = {SHEET_PAD, b.sheet.hints.y - 8 - FORM_KEY_H, PANEL_W - 2 * SHEET_PAD, FORM_KEY_H};
			return b;
		}

		Keyboard keyboard(const std::vector<int>& specialW, const std::vector<int>& counts, int layers)
		{
			Keyboard k;
			int y = 66;
			k.field = {MARGIN, y, PANEL_W - 2 * MARGIN, KB_FIELD_H};
			y += KB_FIELD_H + 8;
			int x = MARGIN;
			for (int w : specialW)
			{
				k.special.push_back({x, y, w, KB_SPECIAL_H});
				x += w + GAP;
			}
			y += KB_SPECIAL_H + 18;
			for (int n : counts)
			{
				std::vector<Box> row;
				int w = n * KB_KEY_W + std::max(0, n - 1) * KB_GAP, x0 = (PANEL_W - w) / 2;
				for (int i = 0; i < n; i++) row.push_back({x0 + i * (KB_KEY_W + KB_GAP), y, KB_KEY_W, KB_KEY_H});
				k.rows.push_back(row);
				y += KB_KEY_H + KB_GAP;
			}
			y += 6;
			// layer keys 64, then Space (the rest), backspace 76, Done 120: the row ends on the margin
			int left = MARGIN, right = PANEL_W - MARGIN;
			int doneW = 120, backW = 76, layerW = 76;
			int spaceW = right - left - layers * (layerW + KB_GAP) - (KB_GAP + backW) - (KB_GAP + doneW);
			int bx = left;
			for (int i = 0; i < layers; i++)
			{
				k.bottom.push_back({bx, y, layerW, KB_BOTTOM_H});
				bx += layerW + KB_GAP;
			}
			k.bottom.push_back({bx, y, spaceW, KB_BOTTOM_H});
			bx += spaceW + KB_GAP;
			k.bottom.push_back({bx, y, backW, KB_BOTTOM_H});
			bx += backW + KB_GAP;
			k.bottom.push_back({bx, y, right - bx, KB_BOTTOM_H});
			k.note = {MARGIN, y + KB_BOTTOM_H + 8, PANEL_W - 2 * MARGIN, 20};
			return k;
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

		Box tileCell(Box area, int n, int cols, int h, int i)
		{
			cols = std::max(1, std::min(cols, std::max(1, n)));
			Box c = cell({area.x, area.y, area.w, h}, cols, GAP, i % cols);
			c.y = area.y + i / cols * (h + GAP);
			return c;
		}

		RecentCard recentCard(Box box)
		{
			RecentCard r;
			r.box = box;
			r.pic = {box.x + 4, box.y + 4, box.w - 8, 48};
			r.title = {box.x + 8, box.y + 57, box.w - 16, 18};
			r.note = {box.x + 8, box.y + 75, box.w - 16, 14};
			r.line = {box.x + 6, box.y + box.h - 8, box.w - 12, 3};
			return r;
		}

		ContinueCard continueCard(Box box)
		{
			ContinueCard c;
			c.box = box;
			c.pic = {box.x + 12, box.y + (box.h - 84) / 2, 128, 84};
			c.key = {box.x + box.w - 12 - 118, box.y + (box.h - 52) / 2, 118, 52};
			int x = c.pic.x + c.pic.w + 14, w = std::max(0, c.key.x - 12 - x);
			c.label = {x, box.y + 14, w, 14};
			c.title = {x, box.y + 33, w, 28};
			c.note = {x, box.y + 63, w, 16};
			c.bar = {x, box.y + 88, w, 6};
			return c;
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
	}
}
