// The readers' Brightness sheet (the text viewer, the PDF/comic viewer, the Manga reader): the screens' backlight as
// BananaOS sets it, Extra dim past its lowest (the pages' colours scaled down: for reading in the dark when 10% is
// still too bright), and the reader's own filter (warm, warmer, dim). Over the bottom panel; any reader opens it.
// An overlay (Ui::Overlay): openLight() pushes it, and the stack gives it the keys, the touches, its drawing and
// its part of signature().
#include "ui_internal.h"
#include "textreader.h"
#include "ui_kit.h"

namespace bp
{
	namespace
	{
		const char* LIGHT_FILTERS[DOC_FILTERS] = {"Off", "Warm", "Warmer", "Dim"};

		enum { LR_SCREENS, LR_TOP, LR_BOTTOM, LR_SAME, LR_DIM, LR_FILTER };

		// a row's value as 0..1 along its bar (screens 10-100%, Extra dim 0-80%)
		double lightShare(int value, int lo, int hi) { return std::clamp((value - lo) / (double)(hi - lo), 0.0, 1.0); }
	}

	int& Ui::lightFilter(Settings& s) const
	{
		// the reader's own: the text viewer's, the PDF/comic viewer's, the Manga reader's
		return page == Page::DocText ? s.txtFilter : page == Page::DocPages ? s.pdfFilter : s.mangaFilter;
	}

	std::string Ui::lightNote(const Player& p) const
	{
		Settings s = p.settings;
		int f = std::clamp(lightFilter(s), 0, DOC_FILTERS - 1), dim = s.readDim;
		std::string n = f ? LIGHT_FILTERS[f] : "";
		if (dim) n += (n.empty() ? "" : " \xC2\xB7 ") + std::string("\xE2\x88\x92") + std::to_string(dim) + "%";
		return n;
	}

	struct Ui::LightSheet : Ui::Overlay
	{
		int cursor = 0;	// the row the keys are on

		explicit LightSheet(Ui& u) : Overlay(u) {}
		const char* name() const override { return "light"; }

		std::vector<int> rows() const
		{
			std::vector<int> r;
			bool top = ui.backlight.present(Backlight::TOP), bottom = ui.backlight.present(Backlight::BOTTOM);
			if (top && bottom && ui.sameBrightness) r.push_back(LR_SCREENS);
			else
			{
				if (top) r.push_back(LR_TOP);
				if (bottom) r.push_back(LR_BOTTOM);
			}
			if (top && bottom) r.push_back(LR_SAME);	// the tick box, as the quick settings' (one level, or each its own)
			r.push_back(LR_DIM);
			r.push_back(LR_FILTER);
			return r;
		}

		// the kit's control sheet: its rows' boxes are the touches too
		lay::ControlSheet layout() const
		{
			std::vector<int> kinds;
			for (int row : rows()) kinds.push_back(row == LR_SAME ? lay::LC_SWITCH : row == LR_FILTER ? lay::LC_CHOICE : lay::LC_SLIDER);
			return lay::controlSheet(kinds);
		}

		void same(Player& p)
		{
			// as the quick settings' box: one level for both (the bottom to the top's), or each its own; the cursor
			// stays on the box as the rows above it change
			Settings s = p.settings;
			s.sameBrightness = !s.sameBrightness;
			p.setSettings(s);
			ui.sameBrightness = s.sameBrightness;
			int pm = ui.backlight.permille(Backlight::TOP);
			if (ui.sameBrightness && pm >= 100) ui.backlight.set(Backlight::BOTTOM, pm);
			auto rs = rows();
			for (int i = 0; i < (int)rs.size(); i++)
				if (rs[i] == LR_SAME) cursor = i;
		}

		void set(int row, int value, Player& p)
		{
			Settings s = p.settings;
			switch (row)
			{
			case LR_SCREENS: case LR_TOP: case LR_BOTTOM:
			{
				int pm = std::clamp(value, 10, 100) * 10;
				if (row != LR_BOTTOM) ui.backlight.set(Backlight::TOP, pm);
				if (row != LR_TOP) ui.backlight.set(Backlight::BOTTOM, pm);
				return;
			}
			case LR_DIM: s.readDim = std::clamp(value, 0, 80) / 10 * 10; break;
			case LR_FILTER: ui.lightFilter(s) = std::clamp(value, 0, DOC_FILTERS - 1); break;
			}
			p.setSettings(s);
		}

		int value(int row, const Player& p) const
		{
			Settings s = p.settings;
			switch (row)
			{
			case LR_SCREENS: case LR_TOP: return std::max(10, (ui.backlight.permille(Backlight::TOP) + 5) / 10);
			case LR_BOTTOM: return std::max(10, (ui.backlight.permille(Backlight::BOTTOM) + 5) / 10);
			case LR_DIM: return s.readDim;
			case LR_FILTER: return ui.lightFilter(s);
			}
			return 0;
		}

		bool key(Button b, Player& p) override
		{
			auto rs = rows();
			int n = (int)rs.size();
			cursor = std::clamp(cursor, 0, n - 1);
			int row = rs[cursor];
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			case Button::Left: case Button::Right: case Button::A:
			{
				if (row == LR_SAME) { same(p); break; }
				if (b == Button::A) { close(); break; }
				int dir = b == Button::Right ? 1 : -1, v = value(row, p);
				if (row == LR_FILTER) set(row, (v + dir + DOC_FILTERS) % DOC_FILTERS, p);
				else if (row == LR_DIM) set(row, v + dir * 10, p);
				else set(row, stepBrightness(v * 10, dir) / 10, p);
				break;
			}
			case Button::B: case Button::Start: close(); break;
			default: break;
			}
			return false;
		}

		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			auto rs = rows();
			lay::ControlSheet l = layout();
			// a tap outside: closed (a bar's finger lifting out there never comes here: the slider has it)
			if (!l.sheet.box.hit(x, y))
			{
				if (pt.kind == Pointer::Up) close();
				return false;
			}
			for (int i = 0; i < (int)rs.size(); i++)
			{
				int row = rs[i];
				if (row == LR_FILTER)
				{
					for (int k = 0; k < DOC_FILTERS; k++)
						if (lay::cell(l.seg[i], DOC_FILTERS, 6, k).hit(x, y) && pt.kind == Pointer::Up) { cursor = i; set(row, k, p); }
					continue;
				}
				if (row == LR_SAME)
				{
					if (pt.kind == Pointer::Up && l.row[i].hit(x, y)) { same(p); return false; }
					continue;
				}
				// a bar: a touch anywhere in its row takes it, in tenths (the screens 10-100%, Extra dim 0-80%)
				if (pt.kind == Pointer::Down && l.row[i].hit(x, y))
				{
					cursor = i;
					SliderRange range = row == LR_DIM ? SliderRange{0, 80, 10} : SliderRange{10, 100, 10};
					ui.slideGrab(SL_LIGHT, i, {l.track[i], range, false, 0, (double)value(row, p)}, x, y, p, [this, row](double v, bool, Player& p) {
						if ((int)v != value(row, p)) set(row, (int)v, p);
					}, this);
					return false;
				}
			}
			return false;
		}

		void draw(const Player& p) override
		{
			auto rs = rows();
			lay::ControlSheet l = layout();
			Kit& k = *ui.kit;
			k.sheet(l.sheet, {&G_SUN, "Brightness", "", false});
			for (int i = 0; i < (int)rs.size(); i++)
			{
				int row = rs[i], v = value(row, p);
				bool at = i == cursor;
				if (row == LR_SAME)
				{
					Kit::Row r;
					r.font = F_P16;
					r.name = "Same brightness for both screens";
					r.trail = p.settings.sameBrightness ? Kit::Trail::SwitchOn : Kit::Trail::SwitchOff;
					r.focused = at;
					k.row(l.row[i], r);
					continue;
				}
				const char* label = row == LR_SCREENS ? "Screens" : row == LR_TOP ? "Top screen" : row == LR_BOTTOM ? "Bottom screen"
					: row == LR_DIM ? "Extra dim" : "Filter";
				if (row == LR_FILTER)
				{
					k.choiceRow(l.row[i], l.seg[i], label, std::vector<std::string>(LIGHT_FILTERS, LIGHT_FILTERS + DOC_FILTERS), v, at);
					continue;
				}
				Kit::SliderRow s;
				s.label = label;
				// the value on the right; Extra dim says what it is for when it's off
				s.value = row == LR_DIM ? (v ? std::to_string(v) + "% darker" : "Off") : std::to_string(v) + "%";
				s.share = row == LR_DIM ? lightShare(v, 0, 80) : lightShare(v, 10, 100);
				s.focused = at;
				s.held = ui.slideHeld(SL_LIGHT, i);
				s.away = row == LR_DIM && v;
				k.sliderRow(l.row[i], l.track[i], s);
			}
			k.hints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"B", "Close"}});
		}

		std::string signature(const Player&) const override
		{
			// the screens' levels aren't settings (Extra dim, the filter and the tick box are, and signature() has
			// those): the backlight's own
			return std::to_string(cursor) + " " + std::to_string(ui.backlight.permille(Backlight::TOP)) + " " +
				std::to_string(ui.backlight.permille(Backlight::BOTTOM));
		}
	};

	void Ui::openLight()
	{
		pushOverlay(std::make_unique<LightSheet>(*this));
	}
}
