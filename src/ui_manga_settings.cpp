#include "ui.h"
#include "ui_internal.h"

namespace bp
{
	// six rows: a seventh would run under the help line (Settings' groups never have seven)
	static constexpr int MSET_SHOWN = 6;

	// ---- the manga reader's settings: a page of the app's own, not a group of Banana Player's Settings ----

	std::vector<int> Ui::mangaSettingRows() const
	{
		const int T = TOGGLE_ROW;
		return {MANGA_MODE_ROW, MANGA_FIT_ROW, T + 13, MANGA_HINGE_ROW, T + 18, MANGA_PRELOAD_ROW, MANGA_CACHE_ROW,
			MANGA_CLEAR_ROW, T + 14, T + 15, EXT_NOTE};	// (T + 16, In Recent on Home: kept in settings.conf, not shown: the reader stays off Home)
	}

	void Ui::openMangaSettings()
	{
		// where it came from, and on the app the tab it was on (and Browse's list): B goes back to both. From the
		// reader, the reader stays open behind it
		mangaSettingsFrom = page;
		mangaSettingsTab = mangaTab;
		mangaSettingsList = mBrowseList;
		page = Page::MangaSettings;
		cursor = scroll = 0;
		pressedBtn = mangaPressed = -1;
		mangaCacheAt = 0;
	}

	void Ui::closeMangaSettings()
	{
		page = mangaSettingsFrom;
		mangaTab = mangaSettingsTab;
		mBrowseList = mangaSettingsList;
		cursor = scroll = 0;
		pressedBtn = mangaPressed = -1;
		mangaCacheAt = 0;
	}

	bool Ui::mangaSettingsKey(Button b, Player& p)
	{
		// (the mini bar shown here says SELECT goes to what plays, as on the app's other pages)
		if (b == Button::Select && nowPlayable(p)) { goNowPlaying(p); return false; }
		auto rows = mangaSettingRows();
		int n = (int)rows.size();
		cursor = std::min(cursor, n - 1);
		switch (b)
		{
		case Button::Up: cursor = (cursor + n - 1) % n; break;
		case Button::Down: cursor = (cursor + 1) % n; break;
		case Button::Left: changeSetting(rows[cursor], -1, p); break;
		case Button::Right: case Button::A: changeSetting(rows[cursor], 1, p); break;
		case Button::B: closeMangaSettings(); return false;
		default: break;
		}
		if (cursor < scroll) scroll = cursor;
		if (cursor >= scroll + MSET_SHOWN) scroll = cursor - MSET_SHOWN + 1;
		return false;
	}

	void Ui::drawMangaSettings(const Player& p)
	{
		drawHeader("Manga settings", p);
		auto rows = mangaSettingRows();
		int n = (int)rows.size();
		if (cursor >= n) cursor = n - 1;
		if (scroll > n - MSET_SHOWN) scroll = std::max(0, n - MSET_SHOWN);
		// the rows as Settings draws its group's: the note last
		for (int i = scroll; i < n && i < scroll + MSET_SHOWN; i++)
			drawSettingRow(rows[i], SET_Y + (i - scroll) * SET_H, i == cursor, p);
		// what the row under the cursor does, in a line or two above the footer
		std::string help = cursor >= 0 && cursor < n ? settingHelp(rows[cursor]) : "";
		auto lines = wrapText(help, 0, 592, 2);
		for (size_t i = 0; i < lines.size(); i++) drawTextMid(lines[i], 0, DIM, 24, HELP_Y + (int)i * 22, 592);
		drawHints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"B", "Back"}});
	}
}
