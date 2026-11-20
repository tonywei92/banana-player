// Music's Equalizer and Effects actions (presets, bands, the stash of a Custom curve): shared by the player's sheets
// (PlayerPage's SongViewSheet, ui_player.cpp), which are drawn on the kit; Settings' Equalizer and Sound effect rows
// open the same sheets.
#pragma once
#include "ui.h"

namespace bp
{
	struct Ui::Sound
	{
		Ui& ui;
		explicit Sound(Ui& u) : ui(u) {}

		// ---- for the rest of Ui ----
		// Settings' Equalizer row (effects false) and Sound effect row: the player's sheet, over Settings
		void open(Player& p, bool effects);
		// the keyboard's Done on a preset's name: the player checks it first, then it closes or says what's wrong
		void nameDone(Player& p) { eqNameDone(p); }

		// the equalizer: the band the cursor (or a finger) is on; a finger on a column sets its gain, a double-tap 0 dB
		int eqBand = 0;
		int eqTapBand = -1;
		Uint32 eqTapAt = 0;
		void eqSetBand(int band, double gain, Player& p);	// an edit: Custom from the first one
		void eqPick(const std::string& name, Player& p);	// a preset (CUSTOM_NAME: Custom back)
		void eqStep(int dir, Player& p);					// the preset before or after, round (Y/X)
		std::vector<std::string> eqCycle(const Player& p) const;	// the presets, then Custom while there is one
		bool eqCustom(const Player& p) const { return p.settings.eqPreset.empty(); }
		bool eqCanReset(const Player& p) const;
		bool eqOriginIsUser(const Player& p) const;
		void eqReset(Player& p);
		void eqSave(Player& p);	// A: over the user preset it came from, or a new one named on the keyboard
		void eqToggle(Player& p);
		// Custom put aside when another preset is picked (Y/X, the list): still in the cycle for the run
		bool stashOn = false;
		std::string stashOrigin;
		EqGains stashGains{};
		enum EqOption { EQO_LIST, EQO_SAVE_TO, EQO_SAVE_AS, EQO_RENAME, EQO_DELETE, EQO_RESET, EQO_CANCEL };
		std::vector<int> eqOptions(const Player& p) const;
		// the preset list's entries: section headers (not chosen) and presets, each its height
		struct EqEntry { int kind; std::string name; };	// kind 0 header, 1 preset, 2 Custom
		std::vector<EqEntry> eqEntries(const Player& p) const;
		// naming a preset on the keyboard: Save as (a new one) or Rename (eqRenameFrom); a name refused keeps it up
		bool eqRenaming = false, eqNameSubmit = false;
		std::string eqRenameFrom, eqNameNote;
		void eqNameDone(Player& p);
		void eqNameOpen(bool rename, Player& p);
		bool eqIsUser(const Player& p, const std::string& name) const;	// one of yours (not a built-in, not Custom)
		int fxCursor = 0;
		void fxChoose(int i, Player& p);
	};
}
