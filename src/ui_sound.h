// Music's Sound page (the Equalizer and Effects tabs), the bottom panel's side: an area of Ui (Ui::Area, ui.h;
// docs/notes/ui-areas.md). Its state, the preset dialogs and its handlers are here and in ui_sound.cpp. A song's
// Equalizer and Effects sheets (its chips, or More then the chip) call these same methods; this page stays for
// Settings' Music › Sound row.
#pragma once
#include "ui.h"

namespace bp
{
	struct Ui::Sound : Ui::Area
	{
		explicit Sound(Ui& u) : Area(u) {}

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Sound; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;
		// Ui::pointer hands this page its touches before the updater's pop-up, as it always did
		bool pointerFirst() const override { return true; }

		// ---- for the rest of Ui ----
		// opened from Settings' Music row, or the song's line or Sound button
		void open(Player& p, bool fromSettings);
		// the keyboard's Done on a preset's name: the player checks it first, then it closes or says what's wrong
		void nameDone(Player& p) { eqNameDone(p); }
		bool dialogOpen() const { return eqDialog != EQD_NONE; }
		Rect tabRect(int t) const { return soundTabRect(t); }

		// two tabs, Equalizer and Effects (L/R), opened from the song's Sound button, its line, or Settings > Music
		// (B goes back there, on its row); the tab last open kept for the run
		enum SoundTab { SOUND_EQ, SOUND_FX };
		int soundTab = SOUND_EQ;
		bool soundFromSettings = false;
		void closeSound(Player& p);
		void drawSoundHeader(const Player& p);
		void drawEqualizer(const Player& p);
		void drawEffects(const Player& p);
		Rect soundTabRect(int t) const { return {t == SOUND_EQ ? 72 : 248, 10, 168, 44}; }
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
		// its dialogs: Options (START), the preset list, Delete's question
		enum EqDialog { EQD_NONE, EQD_OPTIONS, EQD_LIST, EQD_DELETE };
		int eqDialog = EQD_NONE, eqDlgCursor = 0, eqListScroll = 0;
		bool eqListTouch = false;
		enum EqOption { EQO_LIST, EQO_SAVE_TO, EQO_SAVE_AS, EQO_RENAME, EQO_DELETE, EQO_RESET, EQO_CANCEL };
		std::vector<int> eqOptions(const Player& p) const;
		Rect eqOptBox(const Player& p) const { int h = 80 + (int)eqOptions(p).size() * 52; return {112, (436 - h) / 2, 416, h}; }
		Rect eqOptRow(int i, const Player& p) const { return {128, eqOptBox(p).y + 62 + i * 52, 384, 46}; }
		void eqChooseOption(int item, Player& p);
		// the preset list's entries: section headers (not chosen) and presets, each its height
		struct EqEntry { int kind; std::string name; };	// kind 0 header, 1 preset, 2 Custom
		std::vector<EqEntry> eqEntries(const Player& p) const;
		void eqListFit(const Player& p);	// the cursor's entry in view
		int eqListBottom(const std::vector<EqEntry>& e, int first) const;	// the last entry shown from first
		void eqListMove(int dir, const Player& p);
		void openEqList(const Player& p);
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
