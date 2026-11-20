// The More sheet (docs/redesign/components.md, "More sheet (menu)"): a bottom sheet of tiles and rows over a page,
// one overlay fed a list. What it lists is asked for each time it is drawn or pressed (build), so a row says what it
// is now (Like / Liked, where it plays) and a row whose reason has gone (a browser that closed) goes with it.
// X (or SELECT) on the player opens the player's (Ui::PlayerPage::moreSpec, ui_player.cpp).
#include "ui_kit.h"

namespace bp
{
	struct Ui::MoreSheet : Ui::Overlay
	{
		std::function<MoreSpec(const Player&)> build;
		const char* nm;
		// a menu of the older kind that keeps its state in a member (a flag, an enum): alive() says it still stands (the sheet
		// closes when it does not, as it asks it after each row), ended() resets it when the sheet closes
		std::function<bool()> alive;
		std::function<void()> ended;
		int cursor = 0, pressed = -1, lastFocus = -1;
		void follow(const MoreSpec& m)
		{
			if (m.focus >= 0 && m.focus != lastFocus) cursor = m.focus;
			lastFocus = m.focus;
		}

		MoreSheet(Ui& u, std::function<MoreSpec(const Player&)> b, const char* n) : Overlay(u), build(std::move(b)), nm(n) {}
		const char* name() const override { return nm; }

		static lay::Sheet layout(const MoreSpec& m)
		{
			return lay::sheet({(int)m.tiles.size(), (int)m.rows.size(), m.cols, 0, (int)m.chips.size()});
		}
		static const MoreItem& item(const MoreSpec& m, int i)
		{
			if (i < (int)m.chips.size()) return m.chips[i];
			i -= (int)m.chips.size();
			return i < (int)m.tiles.size() ? m.tiles[i] : m.rows[i - m.tiles.size()];
		}

		bool choose(const MoreSpec& m, int i, Player& p)
		{
			const MoreItem& it = item(m, i);
			cursor = i;
			if (it.off || !it.act) return false;
			if (!it.stay) close();	// (first: what it does may open another sheet, which goes on top)
			bool leave = it.act(p);
			if (alive && !alive()) close();
			return leave;
		}

		bool key(Button b, Player& p) override
		{
			bool leave = keyIn(b, p);
			if (done && ended) ended();
			return leave;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			bool leave = pointerIn(pt, x, y, p);
			if (done && ended) ended();
			return leave;
		}

		bool keyIn(Button b, Player& p)
		{
			MoreSpec m = build(p);
			follow(m);
			lay::Sheet s = layout(m);
			int n = s.count();
			if (!n) { close(); return false; }
			cursor = std::clamp(cursor, 0, n - 1);
			switch (b)
			{
			case Button::Up: cursor = lay::sheetMove(s.spec, cursor, lay::Dir::Up); break;
			case Button::Down: cursor = lay::sheetMove(s.spec, cursor, lay::Dir::Down); break;
			case Button::Left: cursor = lay::sheetMove(s.spec, cursor, lay::Dir::Left); break;
			case Button::Right: cursor = lay::sheetMove(s.spec, cursor, lay::Dir::Right); break;
			case Button::A: return choose(m, cursor, p);
			case Button::B: case Button::X: close(); break;
			// (on the player and a photo SELECT opens this sheet, so it closes it too; elsewhere it is not More's key)
			case Button::Select: if (!m.chips.empty()) close(); break;
			case Button::L3:
				// the row that shows L3's cap: that button's, sheet open or not
				for (int i = 0; i < n; i++)
					if (item(m, i).key == "L3") return choose(m, i, p);
				break;
			default: break;
			}
			return false;
		}

		bool pointerIn(const Pointer& pt, int x, int y, Player& p)
		{
			// touch locked (the player's lock): the sheet is the buttons' until it is unlocked
			if (ui.touchIsLocked()) return false;
			MoreSpec m = build(p);
			lay::Sheet s = layout(m);
			if (pt.kind == Pointer::Down)
			{
				pressed = -1;
				// the veil: closed (the lift that follows presses nothing)
				if (!s.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
				for (int i = 0; i < s.count(); i++)
					if (s.item(i).hit(x, y) && !item(m, i).off) pressed = i;
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int i = pressed;
			pressed = -1;
			// let go off it: nothing (a finger that slid away changed its mind)
			if (i < 0 || i >= s.count() || !s.item(i).hit(x, y)) return false;
			return choose(m, i, p);
		}

		void draw(const Player& p) override
		{
			MoreSpec m = build(p);
			follow(m);
			lay::Sheet s = layout(m);
			Kit& k = *ui.kit;
			// nothing to list any more (what it was about has gone: the last file ended under it): closed
			if (!s.count()) { if (!done) { close(); if (ended) ended(); } return; }
			cursor = std::clamp(cursor, 0, std::max(0, s.count() - 1));
			k.sheet(s, {m.glyph, m.title, m.note, false});
			for (int i = 0; i < (int)m.chips.size(); i++)
			{
				const MoreItem& it = m.chips[i];
				Kit::Chip chip;
				chip.glyph = it.glyph;
				chip.label = it.label;
				chip.value = it.value;
				chip.away = it.away;
				chip.off = it.off;
				chip.pressed = pressed == i;
				chip.focused = cursor == i;
				k.chip(s.chip(i), chip);
			}
			int base = (int)m.chips.size();
			for (int i = 0; i < (int)m.tiles.size(); i++)
			{
				const MoreItem& it = m.tiles[i];
				Kit::Key key;
				key.glyph = it.glyph;
				key.label = it.label;
				key.tone = it.danger ? Kit::Tone::Danger : Kit::Tone::Normal;
				key.pressed = pressed == base + i;
				key.focused = cursor == base + i;
				key.off = it.off;
				key.lit = it.lit;
				key.stack = true;
				k.key(s.tile(i), key);
			}
			base += (int)m.tiles.size();
			for (int i = 0; i < (int)m.rows.size(); i++)
			{
				const MoreItem& it = m.rows[i];
				int at = base + i;
				Kit::Row row;
				row.glyph = it.glyph;
				row.name = it.label;
				row.note = it.note;
				row.key = it.key;
				row.pressed = pressed == at;
				row.focused = cursor == at;
				row.off = it.off;
				row.danger = it.danger;
				row.lit = it.lit;
				row.font = F_P16;
				k.row(s.row(i), row);
			}
			k.hints({{s.spec.chips || s.spec.tiles || s.spec.cols > 1 ? "D-pad" : "UP/DOWN", "Move"}, {"A", "Choose"}, {"X/B", "Close"}});
		}

		std::string signature(const Player& p) const override
		{
			// the cursor and the finger, and everything the list says now
			MoreSpec m = build(p);
			std::string sig = std::to_string(cursor) + "," + std::to_string(pressed) + "," + std::to_string(ui.touchIsLocked()) + m.title + "|" + m.note;
			for (auto* list : {&m.chips, &m.tiles, &m.rows})
				for (auto& it : *list) sig += "|" + it.label + ":" + it.value + ":" + it.note + (it.off ? "o" : "") + (it.away ? "a" : "") + (it.lit ? "l" : "");
			return sig;
		}
	};

	void Ui::openMore(std::function<MoreSpec(const Player&)> build, const char* name, std::function<bool()> alive, std::function<void()> ended, int at)
	{
		auto sheet = std::make_unique<MoreSheet>(*this, std::move(build), name);
		sheet->cursor = at;
		sheet->alive = std::move(alive);
		sheet->ended = std::move(ended);
		pushOverlay(std::move(sheet));
	}
}
