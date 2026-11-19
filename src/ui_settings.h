// The Settings page (the groups' tiles, a group's rows, the subtitle style's preview): an area of Ui (Ui::Area, ui.h;
// docs/notes/ui-areas.md). Its keys, its touches, its drawing and the rows' stepping are here and in ui_settings.cpp.
// The groups are the redesign's (docs/redesign/src/screens.js, "settings" and "settings-group"): Playback, Subtitles,
// Music & sound, Photos, Reading, Files & history, Power & screens, Web dashboard, Updates, About; L2 / R2 step between
// them. Its content is laid out in lay::shell(mini).body and its frame(p) draws the shell's header and hints until
// Ui::Shell owns them (docs/notes/2026-11-19-redesign-settings.md). The manga reader's own settings page
// (Page::MangaSettings) is Manga's, but its rows are drawn, stepped and
// touched by this struct's settingsDown, drawSettingRow, settingHelp and changeSetting. Stayed Ui's: the group and row
// ids (SetGroup, STYLE_ROW0..MANGA_JAVA_ROW), the list's measures (the lay::settingsRows layout), setGroup (the open
// group: the way-back records of showPlayer, the dashboard and Browse keep it), the lift of a list's touch in
// Ui::pointer (one block for every list) and the Brightness rows' setBrightness (the quick settings' too).
#pragma once
#include "ui.h"

namespace bp
{
	struct Ui::SettingsPage : Ui::Area
	{
		explicit SettingsPage(Ui& u) : Area(u) {}

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Settings; }
		bool key(Button b, Player& p) override;
		// its downs come late, after the header's "<" and the menu's tabs, as they always did
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		bool pointerDownLate() const override { return true; }
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;

		// ---- the shared shell (lay::shell(mini)) ----
		// The page's header and hint bar, drawn in the shell's own strips until Ui::Shell owns them; the merge swaps
		// this function alone. The pieces it shows, for the shell to use then.
		void frame(const Player& p);
		std::string title() const;
		std::string crumb() const;
		std::vector<Hint> hints(const Player& p) const;

		// ---- for the rest of Ui and Manga's settings page ----
		// B in START's Settings on this page: out of the subtitle style back to its list, or out of a group to the tiles.
		// True: the key was taken (before the menu's own B closes it)
		bool menuBack(Player& p);
		std::vector<int> settingRows() const;	// the open group's rows, by id
		void changeSetting(int row, int dir, Player& p);
		std::string settingHelp(int row) const;	// what a setting does, under its rows
		// one settings row at y (at: the cursor's): its label, arrows and value, an action's or the note's own look.
		// Shared by Settings (draw) and the manga reader's own settings page (drawMangaSettings)
		void drawSettingRow(int row, int y, bool at, const Player& p);
		// a finger down on the settings' list (Settings', Manga's): its row, the touch begun
		bool settingsDown(int x, int y, bool mini = false);
		void openGroup(int g, Player* p = nullptr);
		void stepGroup(int dir, Player* p = nullptr);	// to the settings group before (-1) or after (1), round
		// a settings group's tile (Settings opens on them: two to a row, lay::settingsTile)
		Rect groupRect(int g, bool mini) const { return lay::settingsTile(groupArea(mini), GROUPS, 2, g); }
		bool styleFromSubs = false;	// the subtitle style opened from the Subtitles list: B goes back to it
		bool backDown = false;	// the shell header's back key under a finger (the page draws it: Ui::Shell will own it)
		// the top panel's Preview pane for the focused row (Ui::topPane): the Theme row's swatches; false: none
		bool topPane(PaneSpec& out) const;

	private:
		// the page's margins inside the shell's body; the tiles are five rows of two (ten groups)
		static Rect groupArea(bool mini)
		{
			return lay::settingsTiles(lay::shell(mini).body, (GROUPS + 1) / 2);
		}
		// the tile's live line ("Resume on · tracks remembered", "Off in incognito")
		std::string groupSummary(int g, const Player& p) const;
		// a group's name where the page's neighbours name it either side of the title ("Music", "Power")
		std::string shortGroupName(int g) const;
		void drawPreview(const SubStyle& st, Rect box);
	};
}
