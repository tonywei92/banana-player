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
	std::vector<int> Ui::settingRows() const
	{
		const int T = TOGGLE_ROW;
		switch (setGroup)
		{
		case GROUP_SUBS:
		{
			std::vector<int> out;
			for (int k = 0; k < STYLE_KEYS; k++) out.push_back(STYLE_ROW0 + k);
			return out;
		}
		case GROUP_MUSIC: return {MUSIC_ROW, VIS_ROW, SOUND_ROW};
		case GROUP_PHOTOS: return {T + 8, PAN_ROW, DRAG_ROW};
		case GROUP_REMEMBER: return {T + 0, T + 1, T + 2};
		case GROUP_EXT: return {T + 12, EXT_NOTE};
		case GROUP_FILES: return {T + 3, T + 6, T + 4, T + 11, CLEAR_ROW};
		case GROUP_CONTROLS:
		{
			// the brightness rows where there are backlights (the handheld)
			std::vector<int> out;
			// one row for both panels (Same for both screens), or one each
			bool both = backlight.present(Backlight::TOP) && backlight.present(Backlight::BOTTOM);
			if (backlight.present(Backlight::TOP)) out.push_back(TOP_LIGHT_ROW);
			if (backlight.present(Backlight::BOTTOM) && !(both && sameBrightness)) out.push_back(BOTTOM_LIGHT_ROW);
			if (both) out.push_back(T + 10);
			out.push_back(T + 5);
			out.push_back(T + 7);
			out.push_back(CLOCK_ROW);
			out.push_back(SCREEN_OFF_ROW);
			return out;
		}
		case GROUP_UPDATES:
		{
			// a Store install (or a computer's build): the status alone
			if (!updater || updater->state() == Updater::State::Off) return {CHECK_ROW};
			Updater::State st = updater->state();
			bool install = st == Updater::State::Available || st == Updater::State::Downloading || st == Updater::State::Ready ||
				(st == Updater::State::Failed && !updater->available().empty());
			if (install) return {T + 9, CHECK_ROW, INSTALL_ROW};
			return {T + 9, CHECK_ROW};
		}
		default: return {};
		}
	}

	std::string Ui::settingHelp(int row) const
	{
		const int T = TOGGLE_ROW;
		if (row >= STYLE_ROW0 && row < STYLE_ROW0 + STYLE_KEYS) return "How plain subtitles (SRT, VTT...) look. Styled ones (ASS) keep theirs.";
		if (row == MUSIC_ROW) return "What the top screen shows while a song plays (SELECT while playing).";
		if (row == VIS_ROW) return "How the music is drawn: Peaks, Peaks Banana or Circle (SELECT while playing).";
		if (row == SOUND_ROW) return "Equalizer and effects for music (not videos). A opens it";
		if (row == CLOCK_ROW) return "The time in the top corner of this screen";
		if (row == SCREEN_OFF_ROW) return "While a song or video plays, this screen goes dark after that long untouched. A touch brings it back.";
		if (row == PAN_ROW) return "Which side the pan area (the small photo) is on.";
		if (row == DRAG_ROW) return "The box: a drag moves the frame over the small photo. The photo: a drag moves the photo, as on a phone.";
		if (row == CLEAR_ROW) return "Empties Recent. Press A twice. The places you stopped at are kept.";
		if (row == TOP_LIGHT_ROW || row == BOTTOM_LIGHT_ROW) return "The backlight, 10% to 100%: the same levels as BananaOS's own settings.";
		if (row == CHECK_ROW) return "Looks for a new version now.";
		if (row == INSTALL_ROW) return "Downloads the new version, then Restart now.";
		switch (row - T)
		{
		case 0: return "A video opens where you stopped. Songs always start at the beginning.";
		case 1: return "Keeps each file's audio track. A new file starts in the language you chose last.";
		case 2: return "Keeps each file's subtitles. A new file starts in the language you chose last.";
		case 3: return "The file list opens where you last were.";
		case 4: return "Keeps Recent. Turning it off also deletes the history.";
		case 5: return "Off: the buttons can't pause, skip or close while something plays (touch still can).";
		case 6: return "Shows the folders you've hidden (X on a folder), so you can unhide them.";
		case 7: return "Shows the battery's percent beside its icon.";
		case 8: return "The next photo opens at the same zoom and place. Off: each opens whole.";
		case 9: return "Looks for a new version a few seconds after the start (Wi-Fi needed).";
		case 10: return "One brightness for both screens. Off: each its own.";
		case 11: return "Files played from a network folder go in Recent too. Off: they're left out, and the ones there now go.";
		case 12: return "Read comics and manga from your files, your servers and sources you add. Off: none of it shows or runs.";
		default: return "";
		}
	}

	void Ui::stepGroup(int dir)
	{
		setGroup = (setGroup + dir + GROUPS) % GROUPS;
		cursor = scroll = 0;
		styleFromSubs = false;
		// Web dashboard has no rows: its page
		if (setGroup == GROUP_WEB) openWeb();
	}

	void Ui::openGroup(int g)
	{
		page = Page::Settings;
		if (g == GROUP_WEB)
		{
			setGroup = -1;
			cursor = GROUP_WEB;
			openWeb();
			return;
		}
		setGroup = g;
		cursor = scroll = 0;
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

	void Ui::changeSetting(int row, int dir, Player& p)
	{
		if (row == CLEAR_ROW) { clearHistory(p); return; }
		// the manga reader's switch: its first time on shows the note over Manage extensions
		if (row == TOGGLE_ROW + 12) { extToggleManga(p); return; }
		// a note, not a value: nothing to change (and no switch beyond the last REMEMBER row)
		if (row == EXT_NOTE) return;
		if (row == CHECK_ROW) { if (updater) updater->check(); return; }
		if (row == TOP_LIGHT_ROW || row == BOTTOM_LIGHT_ROW)
		{
			Backlight::Panel panel = row == TOP_LIGHT_ROW ? Backlight::TOP : Backlight::BOTTOM;
			int pm = backlight.permille(panel);
			if (pm >= 0) setBrightness(panel, stepBrightness(pm, dir), p);
			return;
		}
		if (row == INSTALL_ROW)
		{
			if (!updater) return;
			if (updater->state() == Updater::State::Ready) restartRequested = true;
			else updater->download();
			return;
		}
		Settings s = p.settings;
		if (row - STYLE_ROW0 < STYLE_KEYS) stepStyle(s.style, (StyleKey)(row - STYLE_ROW0), dir);
		else if (row == MUSIC_ROW) s.musicShow = ((s.musicShow + dir) % SHOWS + SHOWS) % SHOWS;
		else if (row == VIS_ROW) s.visualizer = stepVisualizer(s.visualizer, dir);
		else if (row == CLOCK_ROW) s.clock = ((s.clock + dir) % CLOCKS + CLOCKS) % CLOCKS;
		// (not round: Never at one end, 5 minutes at the other)
		else if (row == SCREEN_OFF_ROW) s.screenOffAfter = std::clamp(s.screenOffAfter + dir, 0, SCREEN_OFFS - 1);
		else if (row == PAN_ROW) s.panRight = !s.panRight;
		else if (row == DRAG_ROW) s.dragBox = !s.dragBox;
		else s.*REMEMBER[row - TOGGLE_ROW] = !(s.*REMEMBER[row - TOGGLE_ROW]);
		p.setSettings(s);
	}

	void Ui::drawSettings(const Player& p)
	{
		if (setGroup < 0)
		{
			// the groups as tiles, two to a row: a mark, a name, what's in it
			static const Glyph* MARKS[GROUPS] = {&G_CC, &G_NOTE, &G_PICTURE, &G_BOOKMARK, &G_FOLDER, &G_SUN, &G_DOWNLOAD, &G_WEB,
				&G_BLOCKS};
			for (int g = 0; g < GROUPS; g++)
			{
				Rect t = groupRect(g);
				bool at = g == cursor;
				drawBox(t, pressedBtn == 400 + g ? PRESSED : at ? BUTTON : SURFACE, R_TILE, at);
				drawGlyphMid(*MARKS[g], t.x + 34, t.y + t.h / 2, at ? ACCENT : TEXT, 3);
				// (the lines' places by the tile's middle: they stay centred as the tile's height changes)
				drawTextMid(GROUP_NAMES[g].name, 1, at ? ACCENT : TEXT, t.x + 66, t.y + t.h / 2 - 12, t.w - 76);
				std::string about = GROUP_NAMES[g].about;
				if (g == GROUP_WEB)
				{
					int n = webClients();
					about = !p.settings.webDashboard ? "Off" : n == 0 ? "On \xC2\xB7 No browser yet"
						: "On \xC2\xB7 " + std::to_string(n) + (n == 1 ? " browser" : " browsers");
				}
				else if (g == GROUP_EXT) about = p.settings.mangaOn ? "Manga reader: On" : "Manga reader: Off";
				drawTextMid(about, 0, at ? DIM_ON : DIM, t.x + 66, t.y + t.h / 2 + 13, t.w - 76);
			}
			drawHints({{"D-PAD", "Move"}, {"A", "Open"}, {"B", "Close"}, {"L/R", "Tabs"}});
			return;
		}
		// the group in the middle, the ones before and after it either side: a tap (or X / Y) away
		static const char* SHORT[GROUPS] = {"Subtitles", "Music", "Photos", "Remember", "Files", "Controls", "Updates", "Web",
			"Extensions"};
		drawTextMid(GROUP_NAMES[setGroup].name, 1, ACCENT, W / 2, 73, 248, 2);
		for (int k = 0; k < 2; k++)
		{
			Rect c = buttonRect(k ? GROUP_NEXT : GROUP_PREV);
			int g = (setGroup + (k ? 1 : GROUPS - 1)) % GROUPS;
			drawBox(c, pressedBtn == (k ? GROUP_NEXT : GROUP_PREV) ? PRESSED : SURFACE, R_BUTTON);
			const char* key = k ? "X" : "Y";	// Y is on the left of the buttons, as its chip is
			int kw = (int)std::lround(textWidth(key, 0) / sx);
			if (k)
			{
				drawTextMid(key, 0, ACCENT, c.x + c.w - 10 - kw, c.y + c.h / 2);
				drawGlyphMid(G_RIGHT, c.x + c.w - 24 - kw, c.y + c.h / 2, TEXT);
				drawTextMid(SHORT[g], 0, TEXT, c.x + 12, c.y + c.h / 2, c.w - 44 - kw);
			}
			else
			{
				drawTextMid(key, 0, ACCENT, c.x + 10, c.y + c.h / 2);
				drawGlyphMid(G_LEFT, c.x + 24 + kw, c.y + c.h / 2, TEXT);
				drawTextMid(SHORT[g], 0, TEXT, c.x + c.w - 12, c.y + c.h / 2, c.w - 44 - kw, 1);
			}
		}
		auto rows = settingRows();
		for (int i = scroll; i < (int)rows.size() && i < scroll + SET_SHOWN; i++)
		{
			int row = rows[i];
			int y = SET_Y + (i - scroll) * SET_H;
			bool at = i == cursor;
			int mid = y + (SET_H - 4) / 2;
			if (at && row != EXT_NOTE) drawFocus({16, y, 608, SET_H - 4});
			std::string label, value;
			if (row == EXT_NOTE)
			{
				// a note, not a value: an info box, its lines wrapped (A does nothing there)
				drawBox({16, y, 608, SET_H - 4}, SURFACE, R_BUTTON, at);
				drawGlyphMid(G_INFO, 42, mid, at ? DIM_ON : DIM);
				auto lines = wrapText("Repositories are added by you. Banana Player doesn\xE2\x80\x99t provide or check their content.",
					0, 540, 2);
				for (size_t k = 0; k < lines.size(); k++)
					drawTextMid(lines[k], 0, at ? DIM_ON : DIM, 66, mid + (k == 0 ? (lines.size() > 1 ? -11 : 0) : 11), 540);
				continue;
			}
			int k = row - STYLE_ROW0;	// a style row's key
			if (row == CHECK_ROW || row == INSTALL_ROW)
			{
				// actions with a state: no arrows
				std::string label = "Check for updates", value = updateStatus();
				if (row == INSTALL_ROW)
				{
					label = "Update to " + updater->available();
					int pct = updater->progress();
					value = updater->state() == Updater::State::Ready ? "Restart now"
						: updater->state() == Updater::State::Downloading ? (pct >= 0 ? std::to_string(pct) + "%" : "Downloading")
						: updater->state() == Updater::State::Failed ? "Try again" : "Download";
				}
				drawTextMid(label, 1, at ? ACCENT : TEXT, 32, mid, 320);
				drawTextMid(value, 1, at ? ACCENT : DIM_ON, 608, mid, 260, 1);
				continue;
			}
			if (row == SOUND_ROW)
			{
				// a page of its own: what it's set to, and a chevron (no arrows: nothing to step)
				drawTextMid("Sound", 1, at ? ACCENT : TEXT, 32, mid, 320);
				drawTextMid(soundSummary(p.settings), 1, at ? ACCENT : p.settings.eqOn || p.settings.soundFx != SoundFx::None ? TEXT : DIM,
					584, mid, 240, 1);
				drawGlyphMid(G_RIGHT, 604, mid, at ? ACCENT : DIM);
				continue;
			}
			if (row == CLEAR_ROW)
			{
				// an action, not a value: no arrows
				size_t n = p.history().items().size();
				drawTextMid("Clear play history", 1, at ? ACCENT : TEXT, 32, mid, 320);
				drawTextMid(clearArmed ? "Press again to clear" : n ? std::to_string(n) + (n == 1 ? " file" : " files") : "Empty", 1,
					clearArmed ? ACCENT : at ? DIM_ON : DIM, 608, mid, 0, 1);
				continue;
			}
			if (k >= 0 && k < STYLE_KEYS)
			{
				label = subStyleSettings()[k].label;
				value = styleValueLabel(p.settings.style, (StyleKey)k);
			}
			else if (row == MUSIC_ROW)
			{
				label = "Music display";
				value = musicShowLabel(p.settings.musicShow);
			}
			else if (row == VIS_ROW)
			{
				label = "Visualizer";
				value = visualizerLabel(p.settings.visualizer);
			}
			else if (row == TOP_LIGHT_ROW || row == BOTTOM_LIGHT_ROW)
			{
				label = row == BOTTOM_LIGHT_ROW ? "Bottom brightness" : sameBrightness ? "Brightness" : "Top brightness";
				int pm = backlight.permille(row == TOP_LIGHT_ROW ? Backlight::TOP : Backlight::BOTTOM);
				value = pm < 0 ? "-" : std::to_string((pm + 5) / 10) + "%";
			}
			else if (row == CLOCK_ROW)
			{
				label = "Clock";
				value = p.settings.clock == CLOCK_12 ? "12-hour" : p.settings.clock == CLOCK_OFF ? "Off" : "24-hour";
			}
			else if (row == SCREEN_OFF_ROW)
			{
				label = "Screen off after";
				value = screenOffLabel(p.settings.screenOffAfter);
			}
			else if (row == PAN_ROW)
			{
				label = "Pan area";
				value = p.settings.panRight ? "Right" : "Left";
			}
			else if (row == DRAG_ROW)
			{
				label = "Drag moves";
				value = p.settings.dragBox ? "The box" : "The photo";
			}
			else
			{
				label = REMEMBER_LABELS[row - TOGGLE_ROW];
				value = p.settings.*REMEMBER[row - TOGGLE_ROW] ? "On" : "Off";
			}
			drawTextMid(label, 1, at ? ACCENT : TEXT, 32, mid, 330);
			// arrows on every row: they are where a finger steps back and on
			drawTextMid("<", 1, at ? ACCENT : DIM, SET_ARROW_X, mid);
			drawTextMid(value, 1, at ? ACCENT : TEXT, 490, mid, 196, 2);
			drawTextMid(">", 1, at ? ACCENT : DIM, 608, mid, 0, 1);
		}
		// what the row under the cursor does, in a line or two above the footer
		std::string help = cursor >= 0 && cursor < (int)rows.size() ? settingHelp(rows[cursor]) : "";
		SDL_Color helpInk = DIM;
		if (setGroup == GROUP_SUBS)
		{
			Rect box{24, SET_Y + (int)rows.size() * SET_H + 8, 592, 56};
			drawPreview(p.settings.style, box);
			const Track* s = p.selected(TrackType::Sub);
			if (s && (s->codec == "ass" || s->codec == "ssa"))
			{
				help = "This video's subtitles are styled (ASS): they keep their own look";
				helpInk = ACCENT;
			}
		}
		if (setGroup == GROUP_UPDATES && updater && cursor < (int)rows.size() && (rows[cursor] == CHECK_ROW || rows[cursor] == INSTALL_ROW))
		{
			help = updater->fromStore() ? "Installed from the BananaOS Store: the Store updates it"
				: updater->state() == Updater::State::Off ? "Updates are for the handheld's Ports install"
				: updater->state() == Updater::State::Failed ? updater->message()
				: updater->state() == Updater::State::Ready ? "Restart now, or it's put in place at the next start"
				: "Downloads from GitHub, checked before anything changes";
			helpInk = updater->state() == Updater::State::Failed ? ACCENT : DIM;
		}
		auto lines = wrapText(help, 0, 592, 2);
		for (size_t i = 0; i < lines.size(); i++) drawTextMid(lines[i], 0, helpInk, 24, HELP_Y + (int)i * 22, 592);
		bool action = cursor < (int)rows.size() && (rows[cursor] == CHECK_ROW || rows[cursor] == INSTALL_ROW || rows[cursor] == CLEAR_ROW ||
			rows[cursor] == SOUND_ROW);
		if (action)
			drawHints({{"A", rows[cursor] == CLEAR_ROW ? "Clear" : rows[cursor] == CHECK_ROW ? "Check" : rows[cursor] == SOUND_ROW ? "Open" : "Update"},
				{"UP/DOWN", "Choose"}, {"Y/X", "Group"}, {"B", "Back"}});
		else drawHints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"Y/X", "Group"}, {"B", "Back"}});
	}

	// a line in the chosen font, colour and border over a stand-in for the picture, at the size it has on the top
	// panel (mpv's sizes are in pixels of a 720-line picture)
	void Ui::drawPreview(const SubStyle& st, Rect box)
	{
		fillRound(box, SCREEN, R_BUTTON);
		SubStyleLook look = subStyleLook(st);
		double toPanel = H / 720.0 * sy;
		std::string path = fontsDir + "/" + look.fontFile;
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
		SDL_RenderSetClipRect(ren, &clip);
		for (SDL_Surface* surf : {edge, text})
		{
			if (!surf) continue;
			SDL_Texture* t = SDL_CreateTextureFromSurface(ren, surf);
			SDL_Rect dst{cx - surf->w / 2, cy - surf->h / 2, surf->w, surf->h};
			SDL_RenderCopy(ren, t, nullptr, &dst);
			SDL_DestroyTexture(t);
			SDL_FreeSurface(surf);
		}
		SDL_RenderSetClipRect(ren, nullptr);
		TTF_CloseFont(f);
	}
}
