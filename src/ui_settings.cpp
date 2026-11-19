#include "ui.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include "manganet.h"
#include "theme.h"
#include "previewpane.h"
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
#include "ui_browse.h"
#include "ui_home.h"
#include "ui_kit.h"
#include "ui_reader.h"
#include "ui_manga.h"
#include "ui_settings.h"
#include "ui_shell.h"
#include "ui_sound.h"
#include "ui_player.h"

namespace bp
{
	namespace
	{
		// the strategies of the Reading rows (settings.cpp's ids, by index) and a group's short name
		const char* const DOC_FONTS[5] = {"Serif", "Sans", "Mono", "Pixel", "CJK pixel"};
		const char* const DOC_SIZES[5] = {"Small", "Medium", "Large", "Larger", "Largest"};
		const char* const DOC_THEMES[5] = {"Match theme", "Dark", "Light", "Sepia", "Black"};
		std::string docLabel(const char* const* labels, int count, int v)
		{
			return labels[v >= 0 && v < count ? v : 0];
		}
		std::string styleLikeLabel(const Settings& s)
		{
			return docLabel(DOC_FONTS, 5, s.txtFont) + " \xC2\xB7 " + docLabel(DOC_SIZES, 5, s.txtSize) + " \xC2\xB7 " +
				docLabel(DOC_THEMES, 5, s.txtTheme);
		}
	}

	// Settings from START: remember where it was pressed, so B or START there goes back to it (the player, a book's
	// page, a folder). The rail and L/R open it as a section (`fromStart` false), and B then goes Home.
	void Ui::openSettings(Player& p, bool fromStart)
	{
		if (page == Page::Settings) return;
		settingsOver = fromStart;
		if (fromStart)
		{
			settingsBackPage = page;
			settingsBackDir = browse->browseDir;
			settingsBackCursor = cursor;
			settingsBackScroll = scroll;
			settingsBackTab = browse->browseTab;
		}
		settingsPage->styleFromSubs = false;
		page = Page::Settings;
		setGroup = -1;
		cursor = scroll = 0;
		ctxOpen = false;
		pressedBtn = -1;
		fprintf(stderr, "ui: Settings, %s\n", fromStart ? "back to where it was opened from" : "a section");
		(void)p;
	}

	void Ui::closeSettings(Player& p)
	{
		bool over = settingsOver;
		settingsOver = false;
		if (!over)
		{
			// a section, as the rail opened it: B goes Home (it keeps playing behind the browser)
			page = Page::Browse;
			cursor = scroll = 0;
			browse->homeView->goHome(Home::HOME_GRID, 0, p);
			return;
		}
		Page back = settingsBackPage;
		cursor = settingsBackCursor;
		scroll = settingsBackScroll;
		if (back == Page::Browse)
		{
			page = Page::Browse;
			if (browse->browseDir != settingsBackDir)
			{
				browse->setSection(settingsBackTab);
				browse->enterFolder(settingsBackDir, p);
			}
		}
		else page = back;
	}

	Ui::SettingsPage::SettingsPage(Ui& u) : Area(u)
	{
		u.shell->adopt(Page::Settings);
	}

	std::vector<int> Ui::SettingsPage::settingRows() const
	{
		// the page's groups are settings.h's SettingsGroup: the table's rows name theirs
		static_assert((int)GROUP_PLAYBACK == (int)SG_PLAYBACK && (int)GROUP_SUBS == (int)SG_SUBS &&
			(int)GROUP_MUSIC == (int)SG_MUSIC && (int)GROUP_PHOTOS == (int)SG_PHOTOS && (int)GROUP_READING == (int)SG_READING &&
			(int)GROUP_FILES == (int)SG_FILES && (int)GROUP_SCREEN == (int)SG_SCREEN && (int)GROUP_UPDATES == (int)SG_UPDATES &&
			(int)GROUP_WEB == (int)SG_WEB, "Ui::SetGroup and SettingsGroup go together");
		switch (ui.setGroup)
		{
		case GROUP_SUBS:
		{
			std::vector<int> out;
			for (int k = 0; k < STYLE_KEYS; k++) out.push_back(STYLE_ROW0 + k);
			return out;
		}
		// (Sound opens a page of its own; Clear play history is an action)
		case GROUP_PLAYBACK: return pageRows(SG_PLAYBACK);
		case GROUP_MUSIC: return pageRows(SG_MUSIC, {{50, SOUND_ROW}});
		case GROUP_PHOTOS: return pageRows(SG_PHOTOS);
		case GROUP_READING: return pageRows(SG_READING);
		case GROUP_FILES: return pageRows(SG_FILES, {{50, CLEAR_ROW}});
		case GROUP_SCREEN:
		{
			// the brightness rows where there are backlights (the handheld): one row for both panels (Same for both
			// screens, which is there only with two), or one each
			std::vector<std::pair<int, int>> own;
			bool both = ui.backlight.present(Backlight::TOP) && ui.backlight.present(Backlight::BOTTOM);
			if (ui.backlight.present(Backlight::TOP)) own.push_back({10, TOP_LIGHT_ROW});
			if (ui.backlight.present(Backlight::BOTTOM) && !(both && ui.sameBrightness)) own.push_back({20, BOTTOM_LIGHT_ROW});
			std::vector<int> out = pageRows(SG_SCREEN, own);
			if (!both) out.erase(std::remove(out.begin(), out.end(), settingRow("same-brightness")), out.end());
			return out;
		}
		case GROUP_UPDATES:
		{
			// a Store install (or a computer's build): the status alone
			if (!ui.updater || ui.updater->state() == Updater::State::Off) return {CHECK_ROW};
			Updater::State st = ui.updater->state();
			bool install = st == Updater::State::Available || st == Updater::State::Downloading || st == Updater::State::Ready ||
				(st == Updater::State::Failed && !ui.updater->available().empty());
			if (install) return pageRows(SG_UPDATES, {{20, CHECK_ROW}, {30, INSTALL_ROW}});
			return pageRows(SG_UPDATES, {{20, CHECK_ROW}});
		}
		default: return {};
		}
	}

	bool Ui::SettingsPage::topPane(PaneSpec& out) const
	{
		// Power & screens › Theme focused: every theme's swatch and the pieces as they look now (the spec's set-theme
		// top). It follows LEFT / RIGHT: the theme is in the key, so the pane is composed again on each step
		if (ui.page != Page::Settings || ui.setGroup != GROUP_SCREEN) return false;
		std::vector<int> rows = settingRows();
		if (ui.cursor < 0 || ui.cursor >= (int)rows.size()) return false;
		const SettingDef* d = rowDef(rows[ui.cursor]);
		if (!d || strcmp(d->key, "theme") != 0) return false;
		int t = theme::currentTheme();
		out.key = "theme-row";	// (topscreen.cpp adds the theme to every pane's key)
		out.label = "Settings \xE2\x80\xBA Power & screens";
		out.title = std::string("Theme \xC2\xB7 ") + theme::palette(t).name;
		out.text = d->help;
		out.extra = PaneSpec::THEMES;
		out.chosen = t;
		return true;
	}

	std::string Ui::SettingsPage::settingHelp(int row) const
	{
		if (const SettingDef* d = rowDef(row)) return d->help;
		if (row == STYLE_ROW0 + ENCODING)
			return "How a subtitle file's letters are read. Auto works for most; if letters like š, č or ж come out wrong, pick the file's encoding.";
		if (row >= STYLE_ROW0 && row < STYLE_ROW0 + STYLE_KEYS) return "How plain subtitles (SRT, VTT...) look. Styled ones (ASS) keep theirs.";
		if (row == MANGA_CLEAR_ROW) return "Empties the pages kept on the card. Press A twice.";
		if (row == MANGA_JAVA_ROW)
			return "Needed for repositories whose address ends in index.pb or index.min.json: a 241 MB download, and about 300 MB of memory while one of their sources is open.";
		if (row == SOUND_ROW) return "Equalizer and effects for music (not videos). A opens it";
		if (row == CLEAR_ROW) return "Empties Recent. Press A twice. The places you stopped at are kept.";
		if (row == TOP_LIGHT_ROW || row == BOTTOM_LIGHT_ROW) return "The backlight, 10% to 100%: the same levels as BananaOS's own settings.";
		if (row == CHECK_ROW) return "Looks for a new version now.";
		if (row == INSTALL_ROW) return "Downloads the new version, then Restart now.";
		return "";
	}

	void Ui::SettingsPage::stepGroup(int dir, Player* p)
	{
		// from a row group, or from the tiles (or a page of its own: Web dashboard, About, which leave setGroup at -1
		// and the cursor on their tile)
		int was = ui.setGroup < 0 ? ui.cursor : ui.setGroup;
		ui.setGroup = (was + dir + GROUPS) % GROUPS;
		ui.cursor = ui.scroll = 0;
		styleFromSubs = false;
		ui.page = Page::Settings;
		// Web dashboard and About have pages of their own; the rest are row groups. B from either comes back to the
		// groups' tiles, its cursor on that one
		if (ui.setGroup == GROUP_WEB)
		{
			ui.setGroup = -1;
			ui.cursor = GROUP_WEB;
			ui.openWeb();
		}
		else if (ui.setGroup == GROUP_ABOUT)
		{
			ui.setGroup = -1;
			ui.cursor = GROUP_ABOUT;
			if (p) ui.openAbout(*p);
			else ui.page = Page::About;
		}
	}

	void Ui::SettingsPage::openGroup(int g, Player* p)
	{
		ui.page = Page::Settings;
		if (g == GROUP_WEB)
		{
			ui.setGroup = -1;
			ui.cursor = GROUP_WEB;
			ui.openWeb();
			return;
		}
		if (g == GROUP_ABOUT)
		{
			ui.setGroup = -1;
			ui.cursor = GROUP_ABOUT;
			ui.scroll = 0;
			if (p) ui.openAbout(*p);
			else ui.page = Page::About;
			return;
		}
		ui.setGroup = g;
		ui.cursor = ui.scroll = 0;
	}

	void Ui::setBrightness(Backlight::Panel panel, int pm, Player& p)
	{
		// Same for both screens: the other panel to the same level
		if (p.settings.sameBrightness && backlight.present(Backlight::TOP) && backlight.present(Backlight::BOTTOM))
		{
			backlight.set(Backlight::TOP, pm);
			backlight.set(Backlight::BOTTOM, pm);
		}
		else backlight.set(panel, pm);
	}

	void Ui::SettingsPage::changeSetting(int row, int dir, Player& p)
	{
		if (row == CLEAR_ROW) { ui.clearHistory(p); return; }
		// with the lid set to Pause, the three key rows say "The lid pauses" and change nothing
		const SettingDef* d = rowDef(row);
		if (d && (d->intField == &Settings::lidPower || d->intField == &Settings::lidVolume ||
			d->intField == &Settings::lidShoulder) && !p.settings.lidKeepPlaying) return;
		// the cache's Clear asked twice, as the history's is
		if (row == MANGA_CLEAR_ROW)
		{
			Uint32 now = SDL_GetTicks();
			if (ui.manga->mangaCacheAt && now - ui.manga->mangaCacheAt < QUIT_WINDOW_MS)
			{
				ui.manga->mangaPageCache.clear();
				ui.manga->mangaNetCacheBytes = 0;	// (clear() empties the whole folder, the network chapters too)
				ui.manga->mangaCacheAt = 0;
			}
			else ui.manga->mangaCacheAt = now;
			return;
		}
		// a note, not a value: nothing to change
		if (row == EXT_NOTE) return;
		if (row == MANGA_JAVA_ROW) { ui.manga->mangaJavaAct(); return; }
		if (row == CHECK_ROW) { if (ui.updater) ui.updater->check(); return; }
		if (row == TOP_LIGHT_ROW || row == BOTTOM_LIGHT_ROW)
		{
			Backlight::Panel panel = row == TOP_LIGHT_ROW ? Backlight::TOP : Backlight::BOTTOM;
			int pm = ui.backlight.permille(panel);
			if (pm >= 0) ui.setBrightness(panel, stepBrightness(pm, dir), p);
			return;
		}
		if (row == INSTALL_ROW)
		{
			if (!ui.updater) return;
			if (ui.updater->state() == Updater::State::Ready) ui.restartRequested = true;
			else ui.updater->download();
			return;
		}
		Settings s = p.settings;
		if (const SettingDef* d = rowDef(row))
		{
			stepSetting(*d, s, dir);
			// what a few settings do besides keeping the value (the rest are the table's alone)
			if (d->intField == &Settings::mangaFit)
			{
				// a chapter open on the top panel takes it at once, as X takes the mode
				if (ui.pageReader->readerStatus.open) ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fit, (double)s.mangaFit, (int64_t)SDL_GetTicks()});
			}
			else if (d->intField == &Settings::mangaHinge)
			{
				// both panels are one view while reading fullscreen: the reader relayouts now, not at the next open
				if (ui.pageReader->readerStatus.open && ui.pageReader->readerStatus.fullscreen)
					ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, 1.0, (int64_t)SDL_GetTicks(), (double)s.mangaHinge});
			}
			else if (d->intField == &Settings::mangaCacheSize)
			{
				// over a smaller cap the cache gives its oldest pages up at once
				ui.manga->mangaPageCache.setLimit(mangaCacheBytes(s.mangaCacheSize));
				ui.manga->mangaNetTrim(ui.pageReader->mangaRead.path, p);
			}
			else if (d->flagField == &Settings::mangaHistory)
			{
				ui.manga->mangaHistOn = s.mangaHistory;
				// off: the list goes; the places and the read marks stay (progress isn't history)
				if (!s.mangaHistory && ui.manga->mangaStore)
				{
					ui.manga->mangaStore->clearHistory();
					ui.manga->mangaSave();
				}
			}
			else if (d->intField == &Settings::volumeMax && s.volumeMax == 150)
				ui.alert("Up to 150%", "Over 120% can damage your hearing, and the speakers (headphones too). The volume keys go there only one step at a time past 100%.");
		}
		else if (row - STYLE_ROW0 < STYLE_KEYS) stepStyle(s.style, (StyleKey)(row - STYLE_ROW0), dir);
		p.setSettings(s);
		// a lower top than the volume now: down to it
		if (rowDef(row) && rowDef(row)->intField == &Settings::volumeMax && p.boost > std::max(100, s.volumeMax)) p.setBoost(s.volumeMax);
	}

	// a settings row: its label on the left, the arrows and the value; an action's or the repositories' note's own look
	void Ui::SettingsPage::drawSettingRow(int row, int y, bool at, const Player& p)
	{
		int mid = y + (SET_H - 4) / 2;
		if (at && row != EXT_NOTE) ui.drawFocus({16, y, 608, SET_H - 4});
		std::string label, value;
		if (row == EXT_NOTE)
		{
			// a note, not a value: an info box, its lines wrapped (A does nothing there)
			ui.drawBox({16, y, 608, SET_H - 4}, SURFACE, R_BUTTON, at);
			ui.drawGlyphMid(G_INFO, 42, mid, at ? DIM_ON : DIM);
			auto lines = ui.wrapText("Repositories are added by you. Banana Player doesn\xE2\x80\x99t provide or check their content.",
				0, 540, 2);
			for (size_t k = 0; k < lines.size(); k++)
				ui.drawTextMid(lines[k], 0, at ? DIM_ON : DIM, 66, mid + (k == 0 ? (lines.size() > 1 ? -11 : 0) : 11), 540);
			return;
		}
		int k = row - STYLE_ROW0;	// a style row's key
		if (row == CHECK_ROW || row == INSTALL_ROW)
		{
			// actions with a state: no arrows
			std::string label = "Check for updates", value = ui.updateStatus();
			if (row == INSTALL_ROW)
			{
				label = "Update to " + ui.updater->available();
				int pct = ui.updater->progress();
				value = ui.updater->state() == Updater::State::Ready ? "Restart now"
					: ui.updater->state() == Updater::State::Downloading ? (pct >= 0 ? std::to_string(pct) + "%" : "Downloading")
					: ui.updater->state() == Updater::State::Failed ? "Try again" : "Download";
			}
			ui.drawTextMid(label, 1, at ? ACCENT : TEXT, 32, mid, 320);
			ui.drawTextMid(value, 1, at ? ACCENT : DIM_ON, 608, mid, 260, 1);
			return;
		}
		if (row == SOUND_ROW)
		{
			// a page of its own: what it's set to, and a chevron (no arrows: nothing to step)
			ui.drawTextMid("Sound FX", 1, at ? ACCENT : TEXT, 32, mid, 320);
			ui.drawTextMid(soundSummary(p.settings), 1, at ? ACCENT : p.settings.eqOn || p.settings.soundFx != SoundFx::None ? TEXT : DIM,
				584, mid, 240, 1);
			ui.drawGlyphMid(G_RIGHT, 604, mid, at ? ACCENT : DIM);
			return;
		}
		if (row == CLEAR_ROW)
		{
			// an action, not a value: no arrows
			size_t n = p.history().items().size();
			ui.drawTextMid("Clear play history", 1, at ? ACCENT : TEXT, 32, mid, 320);
			ui.drawTextMid(ui.clearArmed ? "Press again to clear" : n ? std::to_string(n) + (n == 1 ? " file" : " files") : "Empty", 1,
				ui.clearArmed ? ACCENT : at ? DIM_ON : DIM, 608, mid, 0, 1);
			return;
		}
		if (row == MANGA_CLEAR_ROW)
		{
			// the cache's Clear: an action too (its size from the cache itself)
			uint64_t size = ui.manga->mangaPageCache.size() + ui.manga->mangaNetCacheBytes;
			ui.drawTextMid("Clear cache", 1, at ? ACCENT : TEXT, 32, mid, 320);
			ui.drawTextMid(ui.manga->mangaCacheAt ? "Press again to clear" : size ? "Clear \xC2\xB7 " + formatBytes((long long)size) : "Empty", 1,
				ui.manga->mangaCacheAt ? ACCENT : at ? DIM_ON : DIM, 608, mid, 0, 1);
			return;
		}
		if (row == MANGA_JAVA_ROW)
		{
			// an action with the host's state as its value (installed size, download progress, why it failed)
			ui.drawTextMid("Extension support", 1, at ? ACCENT : TEXT, 32, mid, 250);
			ui.drawTextMid(ui.manga->mangaJavaValue(), 1, at ? ACCENT : TEXT, 608, mid, 330, 1);
			return;
		}
		if (k >= 0 && k < STYLE_KEYS)
		{
			label = subStyleSettings()[k].label;
			value = styleValueLabel(p.settings.style, (StyleKey)k);
		}
		else if (row == TOP_LIGHT_ROW || row == BOTTOM_LIGHT_ROW)
		{
			label = row == BOTTOM_LIGHT_ROW ? "Bottom brightness" : ui.sameBrightness ? "Brightness" : "Top brightness";
			int pm = ui.backlight.permille(row == TOP_LIGHT_ROW ? Backlight::TOP : Backlight::BOTTOM);
			value = pm < 0 ? "-" : std::to_string((pm + 5) / 10) + "%";
		}
		else if (const SettingDef* d = rowDef(row))
		{
			label = d->label;
			value = settingValueText(*d, p.settings);
		}
		ui.drawTextMid(label, 1, at ? ACCENT : TEXT, 32, mid, 330);
		// arrows on every row: they are where a finger steps back and on
		ui.drawTextMid("<", 1, at ? ACCENT : DIM, SET_ARROW_X, mid);
		ui.drawTextMid(value, 1, at ? ACCENT : TEXT, 490, mid, 196, 2);
		ui.drawTextMid(">", 1, at ? ACCENT : DIM, 608, mid, 0, 1);
	}

	std::string Ui::SettingsPage::groupSummary(int g, const Player& p) const
	{
		const Settings& s = p.settings;
		switch (g)
		{
		case GROUP_PLAYBACK:
			return std::string(s.rememberPosition ? "Resume on" : "Resume off") + " \xC2\xB7 " +
				(s.rememberAudio || s.rememberSubs ? "tracks remembered" : "tracks reset");
		case GROUP_SUBS:
			return styleValueLabel(s.style, FONT) + " \xC2\xB7 " + styleValueLabel(s.style, SIZE) + " \xC2\xB7 " +
				styleValueLabel(s.style, COLOR);
		case GROUP_MUSIC: return std::string(musicShowLabel(s.musicShow)) + " \xC2\xB7 " + visualizerLabel(s.visualizer);
		case GROUP_PHOTOS:
			return std::string(s.keepPhotoZoom ? "Zoom kept" : "Each opens whole") + " \xC2\xB7 " +
				(s.panRight ? "pan area right" : "pan area left");
		case GROUP_READING:
			return styleLikeLabel(s);
		case GROUP_FILES:
			return std::string(s.keepHistory ? "History on" : "History off") + " \xC2\xB7 " +
				(s.showHiddenFolders ? "hidden folders on" : "hidden folders off");
		case GROUP_SCREEN:
			return std::string(theme::palette(s.theme).name) + " \xC2\xB7 screen off " + screenOffLabel(s.screenOffAfter) + " \xC2\xB7 " +
				(s.lidKeepPlaying ? "the lid keeps playing" : "the lid pauses");
		case GROUP_WEB:
			if (ui.incognito) return "Off in incognito";
			if (!s.webDashboard) return "Off";
			return ui.webClients() == 0 ? "On \xC2\xB7 No browser yet"
				: "On \xC2\xB7 " + std::to_string(ui.webClients()) + (ui.webClients() == 1 ? " browser" : " browsers");
		case GROUP_UPDATES: return ui.updateStatus();
		case GROUP_ABOUT: return std::string("Version ") + BP_VERSION;
		default: return "";
		}
	}

	std::string Ui::SettingsPage::title() const
	{
		return ui.setGroup >= 0 ? GROUP_NAMES[ui.setGroup] : "Settings";
	}

	std::string Ui::SettingsPage::crumb() const
	{
		if (ui.setGroup >= 0) return "Settings \xC2\xB7 " + std::to_string(ui.setGroup + 1) + " of " + std::to_string(GROUPS);
		return std::string("Banana Player ") + BP_VERSION;
	}

	std::vector<Ui::Hint> Ui::SettingsPage::hints(const Player&) const
	{
		// B closes Settings back to where START was pressed; Quit lives in Quick settings (F), which the hint keeps in
		// sight now that the Menu's keys are gone
		if (ui.setGroup < 0) return {{"D-PAD", "Move"}, {"A", "Open"}, {"F", "Quick settings"}, {"B/START", "Back"}};
		auto rows = settingRows();
		bool action = ui.cursor >= 0 && ui.cursor < (int)rows.size() && (rows[ui.cursor] == CHECK_ROW || rows[ui.cursor] == INSTALL_ROW ||
			rows[ui.cursor] == CLEAR_ROW || rows[ui.cursor] == SOUND_ROW);
		if (action)
			return {{"A", rows[ui.cursor] == CLEAR_ROW ? "Clear" : rows[ui.cursor] == CHECK_ROW ? "Check" : rows[ui.cursor] == SOUND_ROW ? "Open" : "Update"},
				{"UP/DOWN", "Choose"}, {"L2/R2", "Group"}, {"B", "Back"}};
		return {{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"L2/R2", "Group"}, {"B", "Back"}};
	}

	void Ui::SettingsPage::draw(const Player& p)
	{
		Kit& k = *ui.kit;
		lay::Shell shell = lay::shell();
		if (ui.setGroup < 0)
		{
			// the groups as tiles, two to a row (the mock-up's Settings tile: a mark, a name, what it is set to now)
			static const Glyph* MARKS[GROUPS] = {&G_PLAY, &G_CC, &G_NOTE, &G_PICTURE, &G_BOOK, &G_FOLDER, &G_SUN, &G_WEB,
				&G_DOWNLOAD, &G_INFO};
			for (int g = 0; g < GROUPS; g++)
			{
				Kit::SettingTile t;
				t.glyph = MARKS[g];
				t.name = GROUP_NAMES[g];
				t.summary = groupSummary(g, p);
				t.focused = g == ui.cursor;
				t.pressed = ui.pressedBtn == 400 + g;
				k.settingTile(groupRect(g), t);
			}
			return;
		}
		// the group's rows: the layout starts under the header's strip (the title is in it, beside the back key)
		lay::SettingsRows l = lay::settingsRowsIn(shell.body);
		auto rows = settingRows();
		for (int i = ui.scroll; i < (int)rows.size() && i < ui.scroll + SET_SHOWN; i++)
		{
			int row = rows[i];
			bool at = i == ui.cursor;
			Kit::SettingRow r;
			r.focused = at;
			r.pressed = ui.pressedBtn == 400 + row;
			std::string value;
			bool action = false;
			int kk = row - STYLE_ROW0;
			if (kk >= 0 && kk < STYLE_KEYS)
			{
				r.label = subStyleSettings()[kk].label;
				value = styleValueLabel(p.settings.style, (StyleKey)kk);
			}
			else if (row == TOP_LIGHT_ROW || row == BOTTOM_LIGHT_ROW)
			{
				r.label = row == BOTTOM_LIGHT_ROW ? "Bottom brightness" : p.settings.sameBrightness ? "Brightness" : "Top brightness";
				int pm = ui.backlight.permille(row == TOP_LIGHT_ROW ? Backlight::TOP : Backlight::BOTTOM);
				value = pm < 0 ? "-" : std::to_string((pm + 5) / 10) + "%";
			}
			else if (row == SOUND_ROW)
			{
				r.label = "Sound";
				value = soundSummary(p.settings);
				action = true;
			}
			else if (row == CLEAR_ROW)
			{
				r.label = "Clear play history";
				size_t n = p.history().items().size();
				value = ui.clearArmed ? "Press again to clear" : n ? std::to_string(n) + (n == 1 ? " file" : " files") : "Empty";
				action = true;
			}
			else if (row == CHECK_ROW || row == INSTALL_ROW)
			{
				r.label = "Check for updates";
				value = ui.updateStatus();
				if (row == INSTALL_ROW)
				{
					r.label = "Update to " + ui.updater->available();
					int pct = ui.updater->progress();
					value = ui.updater->state() == Updater::State::Ready ? "Restart now"
						: ui.updater->state() == Updater::State::Downloading ? (pct >= 0 ? std::to_string(pct) + "%" : "Downloading")
						: ui.updater->state() == Updater::State::Failed ? "Try again" : "Download";
				}
				action = true;
			}
			else if (const SettingDef* d = rowDef(row))
			{
				r.label = d->label;
				value = settingValueText(*d, p.settings);
				// the three key rows while the lid pauses: unavailable, with the reason in the value's place
				if ((d->intField == &Settings::lidPower || d->intField == &Settings::lidVolume ||
					d->intField == &Settings::lidShoulder) && !p.settings.lidKeepPlaying)
				{
					r.off = true;
					value = "The lid pauses";
				}
			}
			r.value = value;
			r.action = action;
			k.settingRow(l.row(i - ui.scroll), r);
		}
		// more rows than the page holds (Power & screens with its brightness rows): a bar says so
		if ((int)rows.size() > SET_SHOWN) ui.drawScrollBar(l.area.y, SET_SHOWN * lay::SET_ROW_H - 4, ui.scroll, SET_SHOWN, (int)rows.size());
		// what the row under the cursor does, in a line or two above the footer
		std::string help = l.help.w > 0 && ui.cursor >= 0 && ui.cursor < (int)rows.size() ? settingHelp(rows[ui.cursor]) : "";
		SDL_Color helpInk = DIM;
		// the style's rows: the preview under them, where the help would be (it says more), or for styled subtitles a
		// line that says they keep their look; Text encoding: its help (the preview has nothing to show for it).
		if (l.help.w > 0 && ui.setGroup == GROUP_SUBS && ui.cursor >= 0 && ui.cursor < (int)rows.size() && rows[ui.cursor] != STYLE_ROW0 + ENCODING)
		{
			const Track* s = p.selected(TrackType::Sub);
			if (s && (s->codec == "ass" || s->codec == "ssa"))
			{
				help = "This video's subtitles are styled (ASS): they keep their own look";
				helpInk = ACCENT;
			}
			else
			{
				drawPreview(p.settings.style, Rect{l.area.x + 8, l.area.y + std::min((int)rows.size(), SET_SHOWN) * lay::SET_ROW_H + 4,
					l.area.w - 16, 46});
				help.clear();
			}
		}
		if (l.help.w > 0 && ui.setGroup == GROUP_UPDATES && ui.updater && ui.cursor < (int)rows.size() && (rows[ui.cursor] == CHECK_ROW || rows[ui.cursor] == INSTALL_ROW))
		{
			help = ui.updater->fromStore() ? "Installed from the BananaOS Store: the Store updates it"
				: ui.updater->state() == Updater::State::Off ? "Updates are for the handheld's Ports install"
				: ui.updater->state() == Updater::State::Failed ? ui.updater->message()
				: ui.updater->state() == Updater::State::Ready ? "Restart now, or it's put in place at the next start"
				: "Downloads from GitHub, checked before anything changes";
			helpInk = ui.updater->state() == Updater::State::Failed ? ACCENT : DIM;
		}
		if (l.help.w > 0)
		{
			auto lines = ui.wrapText(help, 0, l.help.w, 2);
			for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], 0, helpInk, l.help.x, l.help.y + (int)i * 22, l.help.w);
		}
	}

	// a line in the chosen font, colour and border over a stand-in for the picture, at the size it has on the top
	// panel (mpv's sizes are in pixels of a 720-line picture)
	void Ui::SettingsPage::drawPreview(const SubStyle& st, Rect box)
	{
		ui.fillRound(box, SCREEN, R_BUTTON);
		SubStyleLook look = subStyleLook(st);
		double toPanel = H / 720.0 * sy;
		std::string path = ui.fontsDir + "/" + look.fontFile;
		TTF_Font* f = TTF_OpenFont(path.c_str(), std::max(8, (int)std::lround(look.size * toPanel)));
		if (!f) return;
		const char* sample = "Subtitles look like this";
		SDL_Color fg{(Uint8)(look.color >> 16), (Uint8)(look.color >> 8), (Uint8)look.color, 255};
		SDL_Color bc{(Uint8)(look.borderColor >> 16), (Uint8)(look.borderColor >> 8), (Uint8)look.borderColor, 255};
		int outline = look.border > 0 ? std::max(1, (int)std::lround(look.border * toPanel)) : 0;
		SDL_Surface* text = TTF_RenderUTF8_Blended(f, sample, fg);
		SDL_Surface* edge = nullptr;
		if (outline)
		{
			TTF_SetFontOutline(f, outline);
			edge = TTF_RenderUTF8_Blended(f, sample, bc);
		}
		int cx = (int)((box.x + box.w / 2) * sx), cy = (int)((box.y + box.h / 2) * sy);
		SDL_Rect clip{(int)(box.x * sx), (int)(box.y * sy), (int)(box.w * sx), (int)(box.h * sy)};
		SDL_RenderSetClipRect(ui.ren, &clip);
		for (SDL_Surface* surf : {edge, text})
		{
			if (!surf) continue;
			SDL_Texture* t = SDL_CreateTextureFromSurface(ui.ren, surf);
			SDL_Rect dst{cx - surf->w / 2, cy - surf->h / 2, surf->w, surf->h};
			SDL_RenderCopy(ui.ren, t, nullptr, &dst);
			SDL_DestroyTexture(t);
			SDL_FreeSurface(surf);
		}
		SDL_RenderSetClipRect(ui.ren, nullptr);
		TTF_CloseFont(f);
	}

	// B in START's Settings on this page, taken before the menu's own B (which closes it)
	bool Ui::SettingsPage::menuBack(Player& p)
	{
		// B in the subtitle style opened from View › Subtitles: back to that sheet, on Subtitles (the old Tracks page
		// is gone). The file may have ended while the style was open: then there is no sheet to go back to and the
		// menu closes as it would from any settings row
		if (ui.setGroup == GROUP_SUBS && styleFromSubs)
		{
			styleFromSubs = false;
			if (!ui.playerShown(p) || p.playingAudio() || p.playingPhoto()) return false;
			// the sheet takes over from Settings: back on the player, not to where START opened it
			ui.settingsOver = false;
			ui.page = Page::Main;
			ui.playerPage->openView(0, p);
			return true;
		}
		// B in a settings group: back to the groups
		if (ui.setGroup >= 0)
		{
			ui.cursor = ui.setGroup;
			ui.setGroup = -1;
			return true;
		}
		return false;
	}

	bool Ui::SettingsPage::key(Button b, Player& p)
	{
		auto rows = settingRows();
		int n = ui.setGroup < 0 ? GROUPS : (int)rows.size();
		ui.cursor = std::min(ui.cursor, n - 1);	// a group's rows can change (the updater's)
		// in a group: L2 / R2 to the group before / after (the mock-up's "L2/R2 Group")
		if (ui.setGroup >= 0 && (b == Button::L2 || b == Button::R2))
		{
			stepGroup(b == Button::R2 ? 1 : -1, &p);
			return false;
		}
		// the groups: tiles two to a row, the cursor moved as on a grid
		if (ui.setGroup < 0 && (b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right))
		{
			if (b == Button::Up && ui.cursor >= 2) ui.cursor -= 2;
			else if (b == Button::Down) ui.cursor = ui.cursor + 2 < n ? ui.cursor + 2 : ui.cursor / 2 < (n - 1) / 2 ? n - 1 : ui.cursor;
			else if (b == Button::Left && ui.cursor % 2 == 1) ui.cursor--;
			else if (b == Button::Right && ui.cursor % 2 == 0 && ui.cursor + 1 < n) ui.cursor++;
			return false;
		}
		bool action = ui.setGroup >= 0 && ui.cursor < n && (rows[ui.cursor] == SOUND_ROW || rows[ui.cursor] == CLEAR_ROW ||
			rows[ui.cursor] == CHECK_ROW || rows[ui.cursor] == INSTALL_ROW);
		switch (b)
		{
		case Button::Up: ui.cursor = (ui.cursor + n - 1) % n; break;
		case Button::Down: ui.cursor = (ui.cursor + 1) % n; break;
		case Button::Left: if (ui.setGroup >= 0 && !action) changeSetting(rows[ui.cursor], -1, p); break;
		case Button::Right:
			if (ui.setGroup < 0) openGroup(ui.cursor, &p);
			else if (!action) changeSetting(rows[ui.cursor], 1, p);
			break;
		case Button::A:
			if (ui.setGroup < 0) openGroup(ui.cursor, &p);
			else if (rows[ui.cursor] == SOUND_ROW) { ui.sound->open(p, true); return false; }
			else changeSetting(rows[ui.cursor], 1, p);
			break;
		// SELECT: back to what plays (Settings has no SELECT of its own)
		case Button::Select: if (ui.nowPlayable(ui.barPlayer(p))) ui.goNowPlaying(p); break;
		case Button::B:
			// a group goes back to the tiles; the tiles close Settings back to where it was opened (START's origin,
			// or Home when the rail or L/R made it a section)
			if (ui.setGroup >= 0) { ui.cursor = ui.setGroup; ui.setGroup = -1; }
			else ui.closeSettings(p);
			break;
		case Button::Start: ui.closeSettings(p); break;
		default: break;
		}
		// (a group's rows scroll; the tiles and the pages of their own keep their own scroll)
		if (ui.page == Page::Settings && ui.setGroup >= 0)
		{
			if (ui.cursor < ui.scroll) ui.scroll = ui.cursor;
			if (ui.cursor >= ui.scroll + SET_SHOWN) ui.scroll = ui.cursor - SET_SHOWN + 1;
		}
		return false;
	}

	// a finger's down is the page's own (a row or a tile); its moves and lifts are the list's, in Ui::pointer. The
	// shell's header owns the back key, so the page sees only the body.
	bool Ui::SettingsPage::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind == Pointer::Down) return settingsDown(x, y);
		return false;
	}

	std::string Ui::SettingsPage::signature(const Player&) const
	{
		// the backlights' levels while their rows show (the keys change them too) and the shoulder rows' availability
		std::string out;
		if (ui.setGroup == GROUP_SCREEN) out += "|bl" + std::to_string(ui.backlight.permille(Backlight::TOP)) + "," +
			std::to_string(ui.backlight.permille(Backlight::BOTTOM));
		return out;
	}

	// a finger down on the settings' list (Settings' groups or a group's rows, the Manga settings' rows): the row it
	// is on, and the list's touch begun (its lift acts, its moves scroll)
	bool Ui::SettingsPage::settingsDown(int x, int y)
	{
		// the Settings page's list is inside the shell's body; the manga reader's own settings page keeps the old page
		lay::SettingsRows l = ui.page == Page::Settings ? lay::settingsRowsIn(lay::shell().body) : lay::settingsRows();
		bool groups = ui.page == Page::Settings && ui.setGroup < 0;
		auto srows = groups ? std::vector<int>() : ui.page == Page::MangaSettings ? ui.manga->mangaSettingRows() : settingRows();
		int n = groups ? GROUPS : (int)srows.size();
		int row = ui.scroll + (y - l.area.y) / lay::SET_ROW_H;
		if (groups)
		{
			// a tile
			row = -1;
			for (int g = 0; g < GROUPS; g++)
				if (groupRect(g).hit(x, y)) row = g;
			if (row < 0) return false;
		}
		else if (y < l.area.y || y >= l.area.y + SET_SHOWN * lay::SET_ROW_H || row >= n) return false;
		ui.cursor = row;
		ui.listTouch = true;
		ui.listScrolled = false;
		ui.touchStartY = y;
		ui.touchX = x;
		ui.scrollStart = ui.scroll;
		return false;
	}
}
