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
#include "ui_settings.h"
#include "ui_sound.h"
#include "ui_player.h"

namespace bp
{

	// ---- music: the Equalizer's and Effects' actions (the sheets are PlayerPage's, ui_player.cpp) ----

	void Ui::Sound::open(Player& p, bool effects)
	{
		ui.settingsPage->styleFromSubs = false;
		fxCursor = std::clamp((int)p.settings.soundFx, 0, SOUNDFX_COUNT - 1);
		ui.pressedBtn = -1;
		ui.playerPage->openSound(effects, p);
	}

	bool Ui::Sound::eqIsUser(const Player& p, const std::string& name) const
	{
		for (auto& u : p.eqUserPresets())
			if (u.name == name) return true;
		return false;
	}

	bool Ui::Sound::eqOriginIsUser(const Player& p) const
	{
		return eqCustom(p) && !p.settings.eqOrigin.empty() && eqIsUser(p, p.settings.eqOrigin);
	}

	bool Ui::Sound::eqCanReset(const Player& p) const
	{
		if (!eqCustom(p) || p.settings.eqOrigin.empty()) return false;
		for (auto& e : p.eqPresets())
			if (e.name == p.settings.eqOrigin) return true;
		return false;
	}

	std::vector<std::string> Ui::Sound::eqCycle(const Player& p) const
	{
		std::vector<std::string> out;
		for (auto& e : p.eqPresets()) out.push_back(e.name);
		if (eqCustom(p) || stashOn) out.push_back(CUSTOM_NAME);
		return out;
	}

	void Ui::Sound::eqPick(const std::string& name, Player& p)
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

	void Ui::Sound::eqStep(int dir, Player& p)
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

	void Ui::Sound::eqSetBand(int band, double gain, Player& p)
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

	void Ui::Sound::eqReset(Player& p)
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

	void Ui::Sound::eqToggle(Player& p)
	{
		Settings s = p.settings;
		s.eqOn = !s.eqOn;
		p.setSettings(s);
		p.showEqCard();
	}

	void Ui::Sound::eqSave(Player& p)
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

	void Ui::Sound::eqNameOpen(bool rename, Player& p)
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
		ui.openKeyboard(EQ_NAME, text);
	}

	void Ui::Sound::eqNameDone(Player& p)
	{
		eqNameSubmit = false;
		std::string name = trimmed(ui.kbEdit.text);
		EqNameError e = eqRenaming ? p.renameEqPreset(eqRenameFrom, name) : p.saveEqPreset(name, p.settings.eqGains, false);
		switch (e)
		{
		case EqNameError::None:
			eqNameNote.clear();
			ui.kbOpen = false;
			ui.kbPressed = -1;
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
		ui.kbActiveAt = SDL_GetTicks();
	}

	std::vector<int> Ui::Sound::eqOptions(const Player& p) const
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

	std::vector<Ui::Sound::EqEntry> Ui::Sound::eqEntries(const Player& p) const
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

	void Ui::Sound::fxChoose(int i, Player& p)
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

}
