#include "ui.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include "theme.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#ifdef BP_WEB
#include "web/netif.h"
#include "web/server.h"
#include <qrcodegen.hpp>
#endif
#include "ui_internal.h"

namespace bp
{
	// ---- music: the Display popup (SELECT over a song) ----

	void Ui::displaySet(int row, int value, Player& p)
	{
		Settings s = p.settings;
		if (row == 0) s.musicShow = (value % SHOWS + SHOWS) % SHOWS;
		else s.visualizer = value >= 0 && value < VISUALIZERS ? value : VIS_PEAKS;	// a visualizer, not a chip
		if (s.musicShow != p.settings.musicShow || s.visualizer != p.settings.visualizer) p.setSettings(s);
	}

	bool Ui::displayKey(Button b, Player& p)
	{
		int now = displayRow ? p.settings.visualizer : p.settings.musicShow;
		switch (b)
		{
		case Button::Up: case Button::Down: displayRow = 1 - displayRow; break;
		case Button::Left: case Button::Right:
			// Style steps through the offered ones only (Bars, Waves and Spectrum are retired)
			displaySet(displayRow, displayRow ? stepVisualizer(now, b == Button::Right ? 1 : -1) : now + (b == Button::Right ? 1 : -1), p);
			break;
		case Button::Select: displaySet(0, p.settings.musicShow + 1, p); break;
		case Button::A: case Button::B: displayOpen = false; break;
		default: break;
		}
		return false;
	}

	bool Ui::displayPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind == Pointer::Down)
		{
			pressedBtn = -1;
			// outside: closed ("<" too), the finger lifting presses nothing
			if (!Rect{32, 84, 576, 300}.hit(x, y))
			{
				displayOpen = false;
				swallowTouch = true;
				return false;
			}
			for (int r = 0; r < 2; r++)
				for (int i = 0; i < displayChips(r); i++)
					if (displayChip(r, i).hit(x, y))
					{
						pressedBtn = CHIP_BTN + r * 8 + i;
						displayRow = r;
					}
			return false;
		}
		if (pt.kind == Pointer::Up && pressedBtn >= CHIP_BTN && pressedBtn < CHIP_BTN + 16)
		{
			int id = pressedBtn - CHIP_BTN;
			pressedBtn = -1;
			if (displayChip(id / 8, id % 8).hit(x, y)) displaySet(id / 8, id / 8 ? VIS_SHOWN[id % 8] : id % 8, p);
		}
		return false;
	}

	void Ui::drawDisplay(const Player& p)
	{
		static const char* SHOW_NAMES[SHOWS] = {"Album art", "Art + viz", "Visualizer", "None"};
		static const char* STYLE_NAMES[VISUALIZERS] = {"Bars", "Waves", "Spectrum", "Circle", "Peaks", "Peaks Banana", "Pocket"};
		drawDialog({32, 84, 576, 300}, "Display");
		int show = std::clamp(p.settings.musicShow, 0, SHOWS - 1), vis = std::clamp(p.settings.visualizer, 0, VISUALIZERS - 1);
		// the art alone, or the title alone: no visualizer, its style muted (still changeable, for later)
		bool muted = show == SHOW_ART || show == SHOW_NONE;
		for (int r = 0; r < 2; r++)
		{
			int rowY = displayChip(r, 0).y;
			bool focus = r == displayRow;
			drawTextMid(r ? "Style" : "Show", 0, focus ? ACCENT : TEXT, 52, rowY - 14, 200);
			if (focus) fillRound({36, rowY, 4, 64}, ACCENT, 2);
			for (int i = 0; i < displayChips(r); i++)
			{
				Rect c = displayChip(r, i);
				// Style's chips are the offered visualizers (VIS_SHOWN), not every one
				int item = r ? VIS_SHOWN[i] : i;
				bool sel = item == (r ? vis : show), dim = r && muted;
				SDL_Color bg = pressedBtn == CHIP_BTN + r * 8 + i ? PRESSED : sel ? (dim ? DIM : ACCENT) : dim ? TRACK : BUTTON;
				SDL_Color ink = sel ? ON_ACCENT : dim ? DIM : TEXT;
				drawBox(c, bg, R_BUTTON);
				const Glyph* g = r ? VIS_GLYPHS[item] : i == SHOW_BOTH ? &G_ARTVIZ : showGlyph(i, vis);
				drawGlyphMid(*g, c.x + c.w / 2, c.y + 20, ink);
				drawTextMid(r ? STYLE_NAMES[item] : SHOW_NAMES[i], 0, ink, c.x + c.w / 2, c.y + 46, c.w - 8, 2);
			}
		}
		const char* help = show == SHOW_ART ? "The style shows with Art + viz or Visualizer"
			: show == SHOW_BOTH ? (vis == VIS_CIRCLE ? "Bars round a ring, the art inside it" : "The visualizer over the bottom of the art")
			: show == SHOW_VIS ? (vis == VIS_PEAKS || vis == VIS_PEAKS_BANANA ? "Bars with caps that hold their highest and fall back"
				: vis == VIS_POCKET ? "Peaks on an old handheld's screen: four greens, chunky dots" : "The visualizer alone")
			: "Just the title: the lightest on the battery";
		drawTextMid(help, 0, DIM, W / 2, 352, 544, 2);
		drawHints({{"D-PAD", "Change"}, {"SELECT", "Show"}, {"B", "Done"}});
	}

	// ---- music: the Sound page (Equalizer, Effects) ----

	void Ui::openSound(Player& p, bool fromSettings)
	{
		// from Settings (START's menu or not): B goes back there; the page has tabs of its own, so the menu's are put away
		soundFromSettings = fromSettings;
		soundFromMenu = fromSettings && inMenu;
		inMenu = false;
		styleFromSubs = false;
		page = Page::Sound;
		eqDialog = EQD_NONE;
		eqTouch = eqListTouch = false;
		pressedBtn = -1;
		fxCursor = std::clamp((int)p.settings.soundFx, 0, SOUNDFX_COUNT - 1);
	}

	void Ui::closeSound(Player& p)
	{
		// the curve as it stands made what mpv keeps, and written
		p.commitEq();
		eqDialog = EQD_NONE;
		eqTouch = false;
		if (!soundFromSettings)
		{
			page = home(p);
			return;
		}
		page = Page::Settings;
		inMenu = soundFromMenu;
		if (inMenu) menuTab = TAB_SETTINGS;
		setGroup = GROUP_MUSIC;
		cursor = std::max(0, rowsIndexOf(settingRows(), SOUND_ROW));
		scroll = 0;
	}

	bool Ui::eqIsUser(const Player& p, const std::string& name) const
	{
		for (auto& u : p.eqUserPresets())
			if (u.name == name) return true;
		return false;
	}

	bool Ui::eqOriginIsUser(const Player& p) const
	{
		return eqCustom(p) && !p.settings.eqOrigin.empty() && eqIsUser(p, p.settings.eqOrigin);
	}

	bool Ui::eqCanReset(const Player& p) const
	{
		if (!eqCustom(p) || p.settings.eqOrigin.empty()) return false;
		for (auto& e : p.eqPresets())
			if (e.name == p.settings.eqOrigin) return true;
		return false;
	}

	std::vector<std::string> Ui::eqCycle(const Player& p) const
	{
		std::vector<std::string> out;
		for (auto& e : p.eqPresets()) out.push_back(e.name);
		if (eqCustom(p) || stashOn) out.push_back(CUSTOM_NAME);
		return out;
	}

	void Ui::eqPick(const std::string& name, Player& p)
	{
		Settings s = p.settings;
		if (name == CUSTOM_NAME)
		{
			// Custom back as it was put aside
			if (!eqCustom(p) && stashOn)
			{
				s.eqPreset.clear();
				s.eqOrigin = stashOrigin;
				s.eqGains = stashGains;
				stashOn = false;
			}
		}
		else
		{
			const EqPreset* pr = nullptr;
			for (auto& e : p.eqPresets())
				if (e.name == name) pr = &e;
			if (!pr) return;
			// Custom put aside, not lost: still in the cycle and the list
			if (eqCustom(p))
			{
				stashOn = true;
				stashOrigin = s.eqOrigin;
				stashGains = s.eqGains;
			}
			s.eqPreset = pr->name;
			s.eqOrigin.clear();
			s.eqGains = pr->gains;
		}
		s.eqOn = true;	// picking one is wanting it on
		p.setSettings(s);
		p.showEqCard();
	}

	void Ui::eqStep(int dir, Player& p)
	{
		auto c = eqCycle(p);
		int n = (int)c.size(), at = -1;
		if (eqCustom(p)) at = n - 1;
		else
			for (int i = 0; i < n; i++)
				if (c[i] == p.settings.eqPreset) at = i;
		if (n == 0) return;
		eqPick(c[at < 0 ? (dir > 0 ? 0 : n - 1) : (at + dir + n) % n], p);
	}

	void Ui::eqSetBand(int band, double gain, Player& p)
	{
		if (band < 0 || band >= EQ_BANDS) return;
		gain = clampEqGain(gain);
		Settings s = p.settings;
		if (s.eqGains[band] == gain) return;
		// the first edit of a preset: Custom, made from it (a Custom put aside before is replaced: there's one)
		if (!eqCustom(p))
		{
			s.eqOrigin = s.eqPreset;
			s.eqPreset.clear();
			stashOn = false;
		}
		s.eqOn = true;
		s.eqGains[band] = gain;
		// heard at once, written when the page closes (commitEq): a drag is many of these
		p.setSettings(s, false);
	}

	void Ui::eqReset(Player& p)
	{
		if (!eqCanReset(p)) return;
		Settings s = p.settings;
		for (auto& e : p.eqPresets())
			if (e.name == s.eqOrigin) s.eqGains = e.gains;
		s.eqPreset = s.eqOrigin;
		s.eqOrigin.clear();
		stashOn = false;
		p.setSettings(s);
		p.showEqCard();
	}

	void Ui::eqToggle(Player& p)
	{
		Settings s = p.settings;
		s.eqOn = !s.eqOn;
		p.setSettings(s);
		p.showEqCard();
	}

	void Ui::eqSave(Player& p)
	{
		if (!eqCustom(p)) return;	// Saved: nothing to save
		if (eqOriginIsUser(p))
		{
			// over the preset of yours it came from: Custom goes, that one plays
			std::string name = p.settings.eqOrigin;
			if (p.saveEqPreset(name, p.settings.eqGains, true) == EqNameError::None) p.osd("Saved " + quoted(name));
			return;
		}
		eqNameOpen(false, p);
	}

	void Ui::eqNameOpen(bool rename, Player& p)
	{
		eqRenaming = rename;
		eqNameNote.clear();
		eqNameSubmit = false;
		std::string text;
		if (rename)
		{
			eqRenameFrom = p.settings.eqPreset;
			text = eqRenameFrom;
		}
		else
		{
			// "Rock 2" (or the first number free); a curve from nowhere, "My preset"
			std::string base = eqCustom(p) ? p.settings.eqOrigin : p.settings.eqPreset;
			bool none = base.empty();
			if (none) base = "My preset";
			text = none ? base : base + " 2";
			for (int n = 2; n < 100 && checkEqPresetName(text, p.eqUserPresets()) == EqNameError::Taken; n++)
				text = base + " " + std::to_string(none ? n : n + 1);
		}
		openKeyboard(EQ_NAME, text);
	}

	void Ui::eqNameDone(Player& p)
	{
		eqNameSubmit = false;
		std::string name = trimmed(kbEdit.text);
		EqNameError e = eqRenaming ? p.renameEqPreset(eqRenameFrom, name) : p.saveEqPreset(name, p.settings.eqGains, false);
		switch (e)
		{
		case EqNameError::None:
			eqNameNote.clear();
			kbOpen = false;
			kbPressed = -1;
			if (eqRenaming && stashOrigin == eqRenameFrom) stashOrigin = name;
			p.osd(eqRenaming ? "Renamed to " + quoted(name) : "Saved " + quoted(name));
			return;
		// what's wrong, under the field; the keyboard stays up to put it right
		case EqNameError::Empty: eqNameNote = "Type a name"; break;
		case EqNameError::TooLong: eqNameNote = "24 characters at most"; break;
		case EqNameError::Builtin: eqNameNote = quoted(name) + " is a built-in name"; break;
		case EqNameError::Taken: eqNameNote = "You have one called " + quoted(name); break;
		default: eqNameNote = "That name can't be used"; break;
		}
		kbActiveAt = SDL_GetTicks();
	}

	std::vector<int> Ui::eqOptions(const Player& p) const
	{
		if (eqCustom(p))
		{
			std::vector<int> o = {EQO_LIST};
			if (eqOriginIsUser(p)) o.push_back(EQO_SAVE_TO);
			o.push_back(EQO_SAVE_AS);
			if (eqCanReset(p)) o.push_back(EQO_RESET);
			o.push_back(EQO_CANCEL);
			return o;
		}
		if (eqIsUser(p, p.settings.eqPreset)) return {EQO_LIST, EQO_SAVE_AS, EQO_RENAME, EQO_DELETE, EQO_CANCEL};
		return {EQO_LIST, EQO_SAVE_AS, EQO_CANCEL};
	}

	void Ui::eqChooseOption(int item, Player& p)
	{
		auto items = eqOptions(p);
		int what = item >= 0 && item < (int)items.size() ? items[item] : EQO_CANCEL;
		eqDialog = EQD_NONE;
		switch (what)
		{
		case EQO_LIST: openEqList(p); break;
		case EQO_SAVE_TO: eqSave(p); break;
		case EQO_SAVE_AS: eqNameOpen(false, p); break;
		case EQO_RENAME: eqNameOpen(true, p); break;
		case EQO_DELETE:
			eqDialog = EQD_DELETE;
			eqDlgCursor = 1;	// on Cancel: a preset gone for good wants a step more
			break;
		case EQO_RESET: eqReset(p); break;
		default: break;
		}
	}

	std::vector<Ui::EqEntry> Ui::eqEntries(const Player& p) const
	{
		std::vector<EqEntry> e;
		e.push_back({0, "Built-in"});
		for (auto& pr : p.eqPresets())
			if (pr.builtin) e.push_back({1, pr.name});
		if (!p.eqUserPresets().empty())
		{
			e.push_back({0, "Yours"});
			for (auto& pr : p.eqUserPresets()) e.push_back({1, pr.name});
		}
		if (eqCustom(p) || stashOn)
		{
			e.push_back({0, "Unsaved"});
			e.push_back({2, CUSTOM_NAME});
		}
		return e;
	}

	// the list's room: y 84 to 404, a header 28 tall, a preset 38
	static const int EQ_LIST_TOP = 84, EQ_LIST_END = 404;

	int Ui::eqListBottom(const std::vector<EqEntry>& e, int first) const
	{
		int y = EQ_LIST_TOP, i = first;
		for (; i < (int)e.size(); i++)
		{
			int h = e[i].kind ? 38 : 28;
			if (y + h > EQ_LIST_END) break;
			y += h;
		}
		return i;
	}

	void Ui::eqListFit(const Player& p)
	{
		auto e = eqEntries(p);
		int n = (int)e.size(), c = std::clamp(eqDlgCursor, 0, std::max(0, n - 1));
		int maxScroll = 0;
		while (maxScroll < n && eqListBottom(e, maxScroll) < n) maxScroll++;
		// its section's header in view with it, where it can be
		if (c < eqListScroll + 1) eqListScroll = c > 0 && e[c - 1].kind == 0 ? c - 1 : c;
		while (eqListScroll < c && eqListBottom(e, eqListScroll) <= c) eqListScroll++;
		eqListScroll = std::clamp(eqListScroll, 0, maxScroll);
	}

	void Ui::eqListMove(int dir, const Player& p)
	{
		auto e = eqEntries(p);
		int n = (int)e.size();
		for (int k = 0, c = eqDlgCursor; k < n; k++)
		{
			c = (c + dir + n) % n;
			if (e[c].kind) { eqDlgCursor = c; break; }
		}
		eqListFit(p);
	}

	void Ui::openEqList(const Player& p)
	{
		eqDialog = EQD_LIST;
		eqListScroll = 0;
		eqDlgCursor = 1;
		auto e = eqEntries(p);
		for (int i = 0; i < (int)e.size(); i++)
			if ((e[i].kind == 2 && eqCustom(p)) || (e[i].kind == 1 && !eqCustom(p) && e[i].name == p.settings.eqPreset)) eqDlgCursor = i;
		eqListFit(p);
	}

	void Ui::fxChoose(int i, Player& p)
	{
		if (i < 0 || i >= SOUNDFX_COUNT) return;
		fxCursor = i;
		Settings s = p.settings;
		if (s.soundFx != (SoundFx)i)
		{
			s.soundFx = (SoundFx)i;
			p.setSettings(s);
		}
		p.showEqCard();
	}

	bool Ui::soundKey(Button b, Player& p)
	{
		if (eqDialog == EQD_OPTIONS || eqDialog == EQD_DELETE)
		{
			int n = eqDialog == EQD_OPTIONS ? (int)eqOptions(p).size() : 2;
			eqDlgCursor = std::clamp(eqDlgCursor, 0, n - 1);
			switch (b)
			{
			case Button::Up: eqDlgCursor = (eqDlgCursor + n - 1) % n; break;
			case Button::Down: eqDlgCursor = (eqDlgCursor + 1) % n; break;
			case Button::A:
				if (eqDialog == EQD_OPTIONS) eqChooseOption(eqDlgCursor, p);
				else
				{
					eqDialog = EQD_NONE;
					if (eqDlgCursor == 0)
					{
						// the one playing: Custom now, the same curve (nothing heard changes)
						std::string name = p.settings.eqPreset;
						if (p.deleteEqPreset(name))
						{
							if (stashOrigin == name) stashOrigin.clear();
							p.showEqCard();
							p.osd("Deleted " + quoted(name));
						}
					}
				}
				break;
			case Button::B: case Button::Start: eqDialog = EQD_NONE; break;
			default: break;
			}
			return false;
		}
		if (eqDialog == EQD_LIST)
		{
			switch (b)
			{
			case Button::Up: case Button::Down: eqListMove(b == Button::Down ? 1 : -1, p); break;
			case Button::A:
			{
				auto e = eqEntries(p);
				eqDialog = EQD_NONE;
				if (eqDlgCursor >= 0 && eqDlgCursor < (int)e.size() && e[eqDlgCursor].kind) eqPick(e[eqDlgCursor].name, p);
				break;
			}
			case Button::B: case Button::Start: eqDialog = EQD_NONE; break;
			default: break;
			}
			return false;
		}
		// L/R: the tabs (L the left one, R the right one), on either tab
		if (b == Button::L || b == Button::R)
		{
			soundTab = b == Button::R ? SOUND_FX : SOUND_EQ;
			return false;
		}
		if (b == Button::B)
		{
			closeSound(p);
			return false;
		}
		if (soundTab == SOUND_FX)
		{
			switch (b)
			{
			case Button::Up: fxCursor = (fxCursor + SOUNDFX_COUNT - 1) % SOUNDFX_COUNT; break;
			case Button::Down: fxCursor = (fxCursor + 1) % SOUNDFX_COUNT; break;
			case Button::A: fxChoose(fxCursor, p); break;
			default: break;
			}
			return false;
		}
		switch (b)
		{
		case Button::Left: eqBand = std::max(0, eqBand - 1); break;
		case Button::Right: eqBand = std::min(EQ_BANDS - 1, eqBand + 1); break;
		case Button::Up: case Button::Down: eqSetBand(eqBand, p.settings.eqGains[eqBand] + (b == Button::Up ? 1 : -1), p); break;
		// Y (west) the preset before, X the one after
		case Button::Y: case Button::X: eqStep(b == Button::X ? 1 : -1, p); break;
		case Button::Select: eqToggle(p); break;
		case Button::A: eqSave(p); break;
		case Button::Start:
			eqDialog = EQD_OPTIONS;
			eqDlgCursor = 0;
			break;
		default: break;
		}
		return false;
	}

	bool Ui::soundPointer(const Pointer& pt, int x, int y, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		auto offBox = [&](Rect box) {
			// a finger outside a dialog closes it (and its lift presses nothing)
			if (box.hit(x, y)) return false;
			eqDialog = EQD_NONE;
			pressedBtn = -1;
			swallowTouch = true;
			return true;
		};
		if (eqDialog == EQD_LIST)
		{
			auto e = eqEntries(p);
			auto entryAt = [&](int py) {
				int ey = EQ_LIST_TOP, end = eqListBottom(e, eqListScroll);
				for (int i = eqListScroll; i < end; i++)
				{
					int h = e[i].kind ? 38 : 28;
					if (py >= ey && py < ey + h) return e[i].kind ? i : -1;
					ey += h;
				}
				return -1;
			};
			if (pt.kind == Pointer::Down)
			{
				if (offBox({96, 22, 448, 392})) return false;
				pressedBtn = -1;
				eqListTouch = true;
				listScrolled = false;
				touchStartY = y;
				scrollStart = eqListScroll;
				int i = entryAt(y);
				if (i >= 0) pressedBtn = DLG_ROW + i;
				return false;
			}
			if (!eqListTouch) return false;
			if (pt.kind == Pointer::Move && (listScrolled || std::abs(y - touchStartY) > 12))
			{
				// a drag scrolls it
				listScrolled = true;
				pressedBtn = -1;
				int maxScroll = 0, n = (int)e.size();
				while (maxScroll < n && eqListBottom(e, maxScroll) < n) maxScroll++;
				eqListScroll = std::clamp(scrollStart + (int)std::lround((touchStartY - y) / 38.0), 0, maxScroll);
			}
			else if (pt.kind == Pointer::Up)
			{
				eqListTouch = false;
				int i = pressedBtn - DLG_ROW;
				pressedBtn = -1;
				if (!listScrolled && i >= 0 && entryAt(y) == i)
				{
					eqDlgCursor = i;
					eqDialog = EQD_NONE;
					eqPick(e[i].name, p);
				}
			}
			return false;
		}
		if (eqDialog == EQD_OPTIONS || eqDialog == EQD_DELETE)
		{
			bool opts = eqDialog == EQD_OPTIONS;
			int n = opts ? (int)eqOptions(p).size() : 2;
			auto row = [&](int i) { return opts ? eqOptRow(i, p) : Rect{128, 196 + i * 52, 384, 46}; };
			if (pt.kind == Pointer::Down)
			{
				if (offBox(opts ? eqOptBox(p) : Rect{112, 116, 416, 204})) return false;
				pressedBtn = -1;
				for (int i = 0; i < n; i++)
					if (row(i).hit(x, y)) pressedBtn = DLG_ROW + i;
			}
			else if (pt.kind == Pointer::Up && pressedBtn >= DLG_ROW)
			{
				int i = pressedBtn - DLG_ROW;
				pressedBtn = -1;
				if (i < n && row(i).hit(x, y))
				{
					eqDlgCursor = i;
					soundKey(Button::A, p);
				}
			}
			return false;
		}
		if (eqTouch)
		{
			// a finger on a band: the gain follows it
			if (pt.kind == Pointer::Move)
			{
				if (std::abs(y - eqDownY) > 8) eqMoved = true;
				eqSetBand(eqBand, std::round((264 - y) / 8.0), p);
			}
			else if (pt.kind == Pointer::Up)
			{
				eqTouch = false;
				// two taps on it, no drag: back to 0 dB
				if (!eqMoved && eqTapAt && now - eqTapAt < 350 && eqTapBand == eqBand)
				{
					eqSetBand(eqBand, 0, p);
					eqTapAt = 0;
				}
				else
				{
					eqTapAt = eqMoved ? 0 : now;
					eqTapBand = eqBand;
				}
			}
			return false;
		}
		if (pressedBtn >= 0)
		{
			// a button acts as the finger lifts on it
			if (pt.kind != Pointer::Up) return false;
			int id = pressedBtn;
			pressedBtn = -1;
			if (!buttonRect(id).hit(x, y)) return false;
			if (id == BACK_ICON) closeSound(p);
			else if (id == SOUND_TAB0 || id == SOUND_TAB1) soundTab = id - SOUND_TAB0;
			else if (id >= FX_ROW && id < FX_ROW + SOUNDFX_COUNT) fxChoose(id - FX_ROW, p);
			else if (id == EQ_SWITCH) eqToggle(p);
			else if (id == EQ_PREV || id == EQ_NEXT) eqStep(id == EQ_NEXT ? 1 : -1, p);
			else if (id == EQ_NAME_ZONE) openEqList(p);
			else if (id == EQ_RESET) eqReset(p);
			else if (id == EQ_SAVE) eqSave(p);
			else if (id == EQ_OPTIONS)
			{
				eqDialog = EQD_OPTIONS;
				eqDlgCursor = 0;
			}
			return false;
		}
		if (pt.kind != Pointer::Down) return false;
		if (buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
		for (int t = 0; t < 2; t++)
			if (t != soundTab && soundTabRect(t).hit(x, y)) { pressedBtn = SOUND_TAB0 + t; return false; }
		if (soundTab == SOUND_FX)
		{
			for (int i = 0; i < SOUNDFX_COUNT; i++)
				if (buttonRect(FX_ROW + i).hit(x, y))
				{
					fxCursor = i;
					pressedBtn = FX_ROW + i;
				}
			return false;
		}
		for (int id : {EQ_SWITCH, EQ_PREV, EQ_NEXT, EQ_NAME_ZONE, EQ_RESET, EQ_SAVE, EQ_OPTIONS})
			if (buttonRect(id).hit(x, y)) { pressedBtn = id; return false; }
		// a band's column: focused, its gain where the finger is (and follows it)
		if (y >= 134 && y < 396)
			for (int i = 0; i < EQ_BANDS; i++)
			{
				int cx = 88 + 56 * i;
				if (x < cx - 28 || x >= cx + 28) continue;
				if (eqTapBand != i) eqTapAt = 0;
				eqBand = i;
				eqTouch = true;
				eqMoved = false;
				eqDownY = y;
				eqSetBand(i, std::round((264 - y) / 8.0), p);
			}
		return false;
	}

	void Ui::drawSoundHeader(const Player& p)
	{
		drawBackIcon();
		// as Audio & Subtitles' tabs: the open one lit; the other with its key on its outer side (L left, R right)
		static const char* NAMES[2] = {"Equalizer", "Effects"};
		for (int t = 0; t < 2; t++)
		{
			Rect r = soundTabRect(t);
			if (t == soundTab) { drawTab(r, NAMES[t], true); continue; }
			drawBox(r, pressedBtn == SOUND_TAB0 + t ? PRESSED : BUTTON, R_BUTTON);
			const char* key = t == SOUND_EQ ? "L" : "R";
			int kw = (int)std::lround(textWidth(key, 0) / sx), mid = r.y + r.h / 2;
			int left = r.x, right = r.x + r.w;
			if (t == SOUND_EQ)
			{
				drawTextMid(key, 0, ACCENT, r.x + 14, mid);
				left = r.x + 14 + kw;
			}
			else
			{
				drawTextMid(key, 0, ACCENT, r.x + r.w - 14, mid, 0, 1);
				right = r.x + r.w - 14 - kw;
			}
			drawTextMid(NAMES[t], 1, TEXT, (left + right) / 2, mid, right - left - 8, 2);
		}
		drawBattery(616, 24, p);
	}

	// a band's gain as its label: "+3", "0", "-4" ("+2.5" between whole ones)
	static std::string gainText(double g)
	{
		char b[16];
		int whole = (int)std::lround(g);
		if (std::fabs(g - whole) >= 0.05) snprintf(b, sizeof b, "%+.1f", g);
		else if (whole) snprintf(b, sizeof b, "%+d", whole);
		else return "0";
		return b;
	}

	void Ui::drawEqualizer(const Player& p)
	{
		static const char* FREQS[EQ_BANDS] = {"31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
		const Settings& st = p.settings;
		bool on = st.eqOn, custom = eqCustom(p);
		// the switch: a dark track, the knob lit yellow on the right when on, grey on the left when off (the colour
		// sits where the switch points, as a phone's does)
		drawBox(buttonRect(EQ_SWITCH), pressedBtn == EQ_SWITCH ? PRESSED : BUTTON, R_BUTTON);
		fillRound({30, 84, 48, 24}, BG, R_CHIP);
		fillRound({on ? 54 : 32, 86, 22, 20}, on ? ACCENT : DIM, R_CHIP - 2);
		drawTextMid(on ? "On" : "Off", 1, on ? TEXT : DIM, 92, 96);
		// the preset: Y and X either side of its name, what it is under it
		drawBox({160, 70, 464, 52}, BUTTON, R_BUTTON);
		for (int id : {EQ_PREV, EQ_NEXT, EQ_NAME_ZONE})
			if (pressedBtn == id) drawBox(buttonRect(id), PRESSED, R_BUTTON);
		drawTextMid("Y", 0, ACCENT, 174, 96);
		drawGlyphMid(G_LEFT, 196, 96, TEXT);
		drawGlyphMid(G_RIGHT, 588, 96, TEXT);
		drawTextMid("X", 0, ACCENT, 610, 96);
		drawTextMid(custom ? CUSTOM_NAME : st.eqPreset, 1, on ? TEXT : DIM, 392, 88, 340, 2);
		if (custom)
			drawTextMid(st.eqOrigin.empty() ? std::string("Unsaved") : "Unsaved \xC2\xB7 from " + st.eqOrigin, 0, ACCENT, 392, 110, 340, 2);
		else drawTextMid(eqIsUser(p, st.eqPreset) ? "Yours" : "Built-in", 0, DIM_ON, 392, 110, 340, 2);
		// the scale, and the band the cursor is on lit behind it (the box's foot comes up for the mini bar)
		int fx = 88 + 56 * eqBand;
		int scaleH = miniUp ? 232 : 263;	// 134 … 397 (over the bar: 134 … 366, its −12 knob inside)
		fillRound({fx - 27, 134, 54, scaleH}, SURFACE, R_BUTTON);
		fillRoundBand({fx - 27, 134, 54, scaleH}, R_BUTTON, 134 + scaleH - 3, 134 + scaleH, ACCENT);
		drawTextMid("dB", 0, DIM, 54, 148, 0, 1);
		drawTextMid("Hz", 0, DIM, 54, miniUp ? 374 : 382, 0, 1);
		static const struct { const char* label; int y; } SCALE[] = {{"+12", 168}, {"+6", 216}, {"0", 264}, {"-6", 312}, {"-12", 360}};
		for (auto& m : SCALE)
		{
			drawTextMid(m.label, 0, DIM, 54, m.y, 0, 1);
			if (m.y != 264) fill({60, m.y, 560, 1}, RULE);
		}
		fill({60, 263, 560, 2}, SCREEN);
		for (int i = 0; i < EQ_BANDS; i++)
		{
			int cx = 88 + 56 * i;
			double g = st.eqGains[i];
			int ky = 264 - (int)std::lround(g * 8);
			bool focus = i == eqBand;
			fillRound({cx - 2, 168, 4, 192}, SCREEN, 2);
			// from 0 dB to the gain
			SDL_Color bar = !on ? DIM : focus ? ACCENT : PRESSED;
			if (ky < 264) fill({cx - 4, ky, 8, 264 - ky}, bar);
			else if (ky > 264) fill({cx - 4, 264, 8, ky - 264}, bar);
			fillRound({cx - 16, ky - 6, 32, 12}, !on ? DIM : focus ? ACCENT : KNOB, R_KNOB);
			fill({cx - 10, ky - 1, 20, 2}, BG);
			drawTextMid(gainText(g), 0, focus ? ACCENT : std::fabs(g) >= 0.05 ? TEXT : DIM, cx, 148, 0, 2);
			drawTextMid(FREQS[i], 0, focus ? ACCENT : DIM, cx, miniUp ? 374 : 382, 0, 2);
		}
		// Reset (touch, or Options), Save, Options: each its key on its right
		auto action = [&](int id, const Glyph& g, const std::string& label, const char* key, bool dim) {
			Rect r = buttonRect(id);
			drawBox(r, pressedBtn == id && !dim ? PRESSED : BUTTON, R_BUTTON);
			SDL_Color ink = dim ? DIM : TEXT;
			int gw = glyphW(g), kw = key ? (int)std::lround(textWidth(key, 0) / sx) : 0, mid = r.y + r.h / 2;
			drawGlyphMid(g, r.x + 14 + gw / 2, mid, ink);
			drawTextMid(label, 0, ink, r.x + 14 + gw + 8, mid, r.w - 14 - gw - 8 - (key ? kw + 20 : 10));
			if (key) drawTextMid(key, 0, dim ? DIM : ACCENT, r.x + r.w - 12, mid, 0, 1);
		};
		action(EQ_RESET, G_REFRESH, "Reset", nullptr, !eqCanReset(p));
		action(EQ_SAVE, G_CHECK, !custom ? "Saved" : eqOriginIsUser(p) ? "Save" : "Save as...", "A", !custom);
		action(EQ_OPTIONS, G_GEAR, "Options", "START", false);
		drawHints({{"D-PAD", "Adjust"}, {"Y/X", "Preset"}, {"SELECT", "On/Off"}, {"R", "Effects"}, {"B", "Back"}});
	}

	void Ui::drawEffects(const Player& p)
	{
		int inUse = (int)p.settings.soundFx;
		for (int i = 0; i < SOUNDFX_COUNT; i++)
		{
			Rect r = buttonRect(FX_ROW + i);
			int mid = r.y + r.h / 2;
			bool at = i == fxCursor;
			if (pressedBtn == FX_ROW + i) drawBox(r, PRESSED, R_BUTTON);
			else if (at) drawFocus(r);
			// the one in use: a filled ring (the cursor is only where it is)
			drawGlyphMid(i == inUse ? G_RADIO_ON : G_RADIO, 41, mid, i == inUse ? ACCENT : at ? TEXT : DIM);
			drawTextMid(soundFxName((SoundFx)i), 1, at ? ACCENT : TEXT, 66, mid, 270);
			drawTextMid(FX_ABOUT[i], 0, at ? DIM_ON : DIM, 608, mid, 260, 1);
		}
		// (no room for the note under the bar: the rows reach the bar's top)
		if (!p.playingAudio() && !miniUp) drawTextMid("Plays on music, not videos", 0, DIM, W / 2, 436, 0, 2);
		drawHints({{"A", "Choose"}, {"L", "Equalizer"}, {"B", "Back"}});
	}

	void Ui::drawSound(const Player& p)
	{
		drawSoundHeader(p);
		if (soundTab == SOUND_FX) drawEffects(p);
		else drawEqualizer(p);
		const Settings& st = p.settings;
		std::string name = eqCustom(p) ? CUSTOM_NAME : st.eqPreset;
		if (eqDialog == EQD_OPTIONS)
		{
			// as the browser's menu: its rows, the preset's name over them
			drawDialog(eqOptBox(p), name);
			auto items = eqOptions(p);
			for (int i = 0; i < (int)items.size(); i++)
			{
				int it = items[i];
				std::string label = it == EQO_LIST ? "All presets..." : it == EQO_SAVE_TO ? "Save to " + quoted(st.eqOrigin)
					: it == EQO_SAVE_AS ? "Save as new preset..." : it == EQO_RENAME ? "Rename..." : it == EQO_DELETE ? "Delete..."
					: it == EQO_RESET ? "Reset to " + quoted(st.eqOrigin) : "Cancel";
				const Glyph* g = it == EQO_LIST ? &G_NOTE : it == EQO_SAVE_TO ? &G_CHECK : it == EQO_SAVE_AS ? &G_PLUS
					: it == EQO_RENAME ? &G_PENCIL : it == EQO_DELETE ? &G_TRASH : it == EQO_RESET ? &G_REFRESH : &G_CROSS;
				drawDialogRow(eqOptRow(i, p), g, label, i == eqDlgCursor, pressedBtn == DLG_ROW + i,
					it == EQO_DELETE ? DANGER : it == EQO_CANCEL ? DIM : TEXT);
			}
			drawHints({{"A", "Choose"}, {"B", "Close"}});
		}
		else if (eqDialog == EQD_DELETE)
		{
			drawDialog({112, 116, 416, 204}, "Delete " + quoted(name) + "?");
			drawTextMid("Your preset goes for good", 0, DIM, W / 2, 176, 380, 2);
			drawDialogRow({128, 196, 384, 46}, &G_TRASH, "Delete", eqDlgCursor == 0, pressedBtn == DLG_ROW, DANGER);
			drawDialogRow({128, 248, 384, 46}, &G_CROSS, "Cancel", eqDlgCursor == 1, pressedBtn == DLG_ROW + 1, DIM);
			drawHints({{"A", "Choose"}, {"B", "Cancel"}});
		}
		else if (eqDialog == EQD_LIST)
		{
			drawDialog({96, 22, 448, 392}, "Presets");
			auto e = eqEntries(p);
			int y = EQ_LIST_TOP, end = eqListBottom(e, eqListScroll);
			for (int i = eqListScroll; i < end; i++)
			{
				if (!e[i].kind)
				{
					// a section's name: not a row to choose
					drawTextMid(e[i].name, 0, DIM, 120, y + 14);
					y += 28;
					continue;
				}
				Rect r{112, y, 416, 38};
				bool at = i == eqDlgCursor;
				bool active = e[i].kind == 2 ? eqCustom(p) : !eqCustom(p) && e[i].name == st.eqPreset;
				if (pressedBtn == DLG_ROW + i) drawBox(r, PRESSED, R_BUTTON);
				else if (at) drawFocus(r);
				drawGlyphMid(active ? G_RADIO_ON : G_RADIO, 132, r.y + r.h / 2, active ? ACCENT : at ? TEXT : DIM);
				drawTextMid(e[i].name, 1, at ? ACCENT : TEXT, 156, r.y + r.h / 2, 356);
				y += 38;
			}
			// a long list: where its window is
			int n = (int)e.size(), maxScroll = 0;
			while (maxScroll < n && eqListBottom(e, maxScroll) < n) maxScroll++;
			if (maxScroll > 0)
			{
				fillRound({532, EQ_LIST_TOP, 4, EQ_LIST_END - EQ_LIST_TOP}, SCREEN, 2);
				int shown = end - eqListScroll, h = std::max(16, (EQ_LIST_END - EQ_LIST_TOP) * shown / n);
				fillRound({532, EQ_LIST_TOP + (EQ_LIST_END - EQ_LIST_TOP - h) * eqListScroll / maxScroll, 4, h}, DIM, 2);
			}
			drawHints({{"A", "Choose"}, {"B", "Close"}});
		}
	}
}
