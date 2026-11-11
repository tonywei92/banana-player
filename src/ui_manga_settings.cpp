#include "ui.h"
#include "ui_internal.h"
#include "manganet.h"

namespace bp
{
	// six rows: a seventh would run under the help line (Settings' groups never have seven)
	static constexpr int MSET_SHOWN = 6;

	static std::string javaMb(int64_t bytes)
	{
		return std::to_string((bytes + 500000) / 1000000) + " MB";
	}

	// ---- the manga reader's settings: a page of the app's own, not a group of Banana Player's Settings ----

	std::vector<int> Ui::mangaSettingRows() const
	{
		const int T = TOGGLE_ROW;
		return {MANGA_MODE_ROW, MANGA_FIT_ROW, T + 13, MANGA_HINGE_ROW, T + 18, MANGA_PRELOAD_ROW, MANGA_CACHE_ROW,
			MANGA_CLEAR_ROW, MANGA_JAVA_ROW, T + 14, T + 15, EXT_NOTE};	// (T + 16, In Recent on Home: kept in settings.conf, not shown: the reader stays off Home)
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
		mangaJavaAt = 0;
	}

	void Ui::closeMangaSettings()
	{
		page = mangaSettingsFrom;
		mangaTab = mangaSettingsTab;
		mBrowseList = mangaSettingsList;
		cursor = scroll = 0;
		pressedBtn = mangaPressed = -1;
		mangaCacheAt = 0;
		mangaJavaAt = 0;
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

	uint64_t Ui::mangaJavaSerial() const
	{
		// (ui.cpp's signature has no MangaNet's definition: the serial it follows is asked for here)
		return mangaNet ? mangaNet->javaExt().serial() : 0;
	}

	std::string Ui::mangaJavaValue() const
	{
		if (!mangaNet) return "Not installed \xC2\xB7 234 MB";
		JavaExtHost& h = mangaNet->javaExt();
		switch (h.state())
		{
		case JavaExtHost::State::NotInstalled:
			return "Not installed \xC2\xB7 " + javaMb(JavaExtPins::JRE_BYTES + JavaExtPins::SERVER_BYTES);
		case JavaExtHost::State::Downloading:
		{
			int64_t done = 0, total = 0;
			h.progress(done, total);
			int pct = total > 0 ? (int)(done * 100 / total) : 0;
			return "Downloading " + std::to_string(pct) + " %";
		}
		case JavaExtHost::State::Installed:
		{
			int64_t bytes = h.installedBytes();
			return bytes > 0 ? "Installed \xC2\xB7 " + javaMb(bytes) : "Installed";
		}
		case JavaExtHost::State::Starting: return "Starting\xE2\x80\xA6";
		case JavaExtHost::State::Running: return "Running";
		case JavaExtHost::State::Failed: return h.error().empty() ? "Failed" : "Failed: " + h.error();
		}
		return "";
	}

	void Ui::mangaJavaAct()
	{
		if (!mangaNet) return;
		JavaExtHost& h = mangaNet->javaExt();
		switch (h.state())
		{
		case JavaExtHost::State::NotInstalled:
			h.install();
			mangaJavaAt = 0;
			break;
		case JavaExtHost::State::Downloading:
			h.cancelInstall();
			mangaJavaAt = 0;
			break;
		case JavaExtHost::State::Installed:
		{
			// as Clear cache is: the first press says what the second does
			Uint32 now = SDL_GetTicks();
			if (mangaJavaAt && now - mangaJavaAt < QUIT_WINDOW_MS)
			{
				h.remove();
				mangaJavaAt = 0;
			}
			else mangaJavaAt = now;
			break;
		}
		case JavaExtHost::State::Failed:
			// a process that ended: start it again; a download or unpack that failed: try the install again
			if (h.installedBytes() > 0) h.want();
			else h.install();
			mangaJavaAt = 0;
			break;
		default: break;
		}
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
		// what the row under the cursor does, in up to three lines above the footer
		std::string help = cursor >= 0 && cursor < n ? settingHelp(rows[cursor]) : "";
		auto lines = wrapText(help, 0, 592, 3);
		// a third line would reach into the footer: the block starts one step higher
		int helpY = HELP_Y - (lines.size() > 2 ? 22 : 0);
		for (size_t i = 0; i < lines.size(); i++) drawTextMid(lines[i], 0, DIM, 24, helpY + (int)i * 22, 592);
		std::vector<Hint> hints = {{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"B", "Back"}};
		if (mangaNet && cursor >= 0 && cursor < n && rows[cursor] == MANGA_JAVA_ROW)
		{
			const char* verb = nullptr;
			switch (mangaNet->javaExt().state())
			{
			case JavaExtHost::State::NotInstalled: verb = "Install"; break;
			case JavaExtHost::State::Downloading: verb = "Cancel"; break;
			case JavaExtHost::State::Installed: verb = mangaJavaAt ? "Remove again" : "Remove"; break;
			case JavaExtHost::State::Failed: verb = "Try again"; break;
			default: break;
			}
			if (verb) hints.insert(hints.begin() + 2, Hint{"A", verb});
		}
		drawHints(hints);
	}
}
