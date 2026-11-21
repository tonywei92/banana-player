#include "ui.h"
#include "ui_internal.h"
#include "format.h"
#include "ui_settings.h"
#include "ui_manga.h"
#include "ui_kit.h"
#include "manganet.h"
#include "ui_smoothlist.h"
#include <cmath>
#include <memory>

namespace bp
{
	// six rows: a seventh would run under the help line (Settings' groups never have seven)
	// the rows the shell's body holds, as Settings' groups (the list's finger in Ui::pointer counts the same)
	static constexpr int MSET_SHOWN = lay::SET_ROWS;

	static std::string javaMb(int64_t bytes)
	{
		return std::to_string((bytes + 500000) / 1000000) + " MB";
	}

	// ---- the manga reader's settings: a page of the app's own, not a group of Banana Player's Settings ----

	std::vector<int> Ui::Manga::mangaSettingRows() const
	{
		// (In Recent on Home, manga-recent, is kept in settings.conf and not shown: the reader stays off Home)
		auto rows = pageRows(SG_MANGA, {{80, MANGA_CLEAR_ROW}, {90, MANGA_JAVA_ROW}, {130, EXT_NOTE}});
		return rows;
	}

	Ui::SmoothList* Ui::Manga::smoothList()
	{
		if (ui.page == Page::MangaSettings) return &ui.settingsPage->mangaList;
		return mangaPageList();
	}

	void Ui::Manga::openMangaSettings()
	{
		// where it came from, and on the app the tab it was on (and Browse's list): B goes back to both. From the
		// reader, the reader stays open behind it
		mangaSettingsFrom = ui.page;
		mangaSettingsTab = mangaTab;
		mangaSettingsList = mBrowseList;
		ui.page = Page::MangaSettings;
		ui.cursor = ui.scroll = 0;
		ui.settingsPage->mangaList.top();
		ui.pressedBtn = mangaPressed = -1;
		mangaCacheAt = 0;
		mangaJavaAt = 0;
	}

	void Ui::Manga::closeMangaSettings()
	{
		ui.page = mangaSettingsFrom;
		mangaTab = mangaSettingsTab;
		mBrowseList = mangaSettingsList;
		ui.cursor = ui.scroll = 0;
		ui.pressedBtn = mangaPressed = -1;
		mangaCacheAt = 0;
		mangaJavaAt = 0;
	}

	bool Ui::Manga::mangaSettingsKey(Button b, Player& p)
	{
		// (the mini bar shown here says SELECT goes to what plays, as on the app's other pages)
		if (b == Button::Select && ui.nowPlayable(p)) { ui.goNowPlaying(p); return false; }
		auto rows = mangaSettingRows();
		int n = (int)rows.size();
		ui.cursor = std::min(ui.cursor, n - 1);
		switch (b)
		{
		case Button::Up: ui.cursor = (ui.cursor + n - 1) % n; break;
		case Button::Down: ui.cursor = (ui.cursor + 1) % n; break;
		case Button::Left: ui.settingsPage->changeSetting(rows[ui.cursor], -1, p); break;
		case Button::Right: case Button::A: ui.settingsPage->changeSetting(rows[ui.cursor], 1, p); break;
		case Button::B: closeMangaSettings(); return false;
		default: break;
		}
		lay::SettingsRows l = lay::settingsRowsIn(lay::shell().body);
		ui.settingsPage->mangaList.reveal(ui.cursor, {l.area.x, l.area.y, l.area.w, MSET_SHOWN * lay::SET_ROW_H}, lay::SET_ROW_H, n);
		return false;
	}

	uint64_t Ui::Manga::mangaJavaSerial() const
	{
		// (ui.cpp's signature has no MangaNet's definition: the serial it follows is asked for here)
		return mangaNet ? mangaNet->javaExt().serial() : 0;
	}

	std::string Ui::Manga::mangaJavaValue() const
	{
		if (!mangaNet) return "Not installed \xC2\xB7 241 MB";
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

	void Ui::Manga::mangaJavaAct()
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

	void Ui::Manga::drawMangaSettings(const Player& p)
	{
		// the reader's own settings (screens.js manga-settings), as Settings draws a group: the kit's setting rows in
		// the shell's body, the focused row's help under them
		ui.drawHeader("Manga settings", p);
		Kit& k = *ui.kit;
		lay::SettingsRows l = lay::settingsRowsIn(lay::shell().body);
		auto rows = mangaSettingRows();
		int n = (int)rows.size();
		if (ui.cursor >= n) ui.cursor = n - 1;
		// the rows as a smooth list (ui_smoothlist.h), the cursor in view at once when the page comes back
		SmoothList& list = ui.settingsPage->mangaList;
		Rect view{l.area.x, l.area.y, l.area.w, MSET_SHOWN * lay::SET_ROW_H};
		if (list.takeFresh() && ui.cursor >= 0) list.reveal(ui.cursor, view, lay::SET_ROW_H, n, false);
		// each row's look first: the note's (a Kit::Row) or a setting's
		struct Look { bool note = false; Kit::SettingRow s; };
		auto looks = std::make_shared<std::vector<Look>>();
		for (int i = 0; i < n; i++)
		{
			int row = rows[i];
			Look lk;
			lk.note = row == EXT_NOTE;
			Kit::SettingRow& s = lk.s;
			s.focused = i == ui.cursor;
			s.pressed = ui.pressedBtn == 400 + row && !list.dragging();
			if (row == MANGA_CLEAR_ROW)
			{
				// the cache's Clear: an action (its size from the cache itself), asked twice
				uint64_t size = mangaPageCache.size() + mangaNetCacheBytes;
				s.label = "Clear cache";
				s.value = mangaCacheAt ? "Press again to clear" : size ? "Clear \xC2\xB7 " + formatBytes((long long)size) : "Empty";
				s.action = true;
			}
			else if (row == MANGA_JAVA_ROW)
			{
				s.label = "Extension support";
				s.value = mangaJavaValue();
				s.action = true;
			}
			else if (const SettingDef* d = rowDef(row))
			{
				s.label = d->label;
				s.value = settingValueText(*d, p.settings);
			}
			looks->push_back(lk);
		}
		SmoothList::Spec spec;
		spec.view = view;
		spec.rowH = lay::SET_ROW_H;
		spec.count = n;
		spec.key = [looks](int i) {
			const Look& lk = (*looks)[i];
			return std::to_string(lk.note) + lk.s.label + "\x1f" + lk.s.value + "\x1f" + std::to_string(lk.s.focused) +
				std::to_string(lk.s.pressed) + std::to_string(lk.s.action);
		};
		spec.draw = [&k, looks](int i, Rect box) {
			const Look& lk = (*looks)[i];
			Rect r{box.x, box.y, box.w, lay::SET_ROW_H - 2};
			if (lk.note)
			{
				// a note, not a value: the info mark and its line (A does nothing there)
				Kit::Row note;
				note.under = BG;
				note.glyph = &G_INFO;
				note.name = "Repositories are added by you";
				note.second = "Banana Player doesn\xE2\x80\x99t provide or check their content.";
				note.focused = lk.s.focused;
				note.off = true;
				k.row(r, note);
				return;
			}
			k.settingRow(r, lk.s);
		};
		list.draw(std::move(spec));
		if (n > MSET_SHOWN) ui.drawScrollBar(l.area.y, MSET_SHOWN * lay::SET_ROW_H - 4,
			(int)std::lround(list.sc.offset() / lay::SET_ROW_H), MSET_SHOWN, n);
		// what the row under the cursor does, in a line or two above the hint bar
		std::string help = ui.cursor >= 0 && ui.cursor < n ? ui.settingsPage->settingHelp(rows[ui.cursor]) : "";
		if (l.help.w > 0)
		{
			auto lines = ui.wrapText(help, 0, l.help.w, 2);
			for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], 0, DIM, l.help.x, l.help.y + (int)i * 22, l.help.w);
		}
		std::vector<Hint> hints = {{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"B", "Back"}};
		if (mangaNet && ui.cursor >= 0 && ui.cursor < n && rows[ui.cursor] == MANGA_JAVA_ROW)
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
		ui.drawHints(hints);
	}
}
