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
#include "ui_kit.h"
#include "ui_settings.h"
#include "ui_web.h"

namespace bp
{
	// ---- the web dashboard ----

	int Ui::webView(const Player& p) const
	{
		// incognito holds the dashboard off whatever its switch says (the switch keeps its saved value)
		if (incognito) return WEB_INCOGNITO_VIEW;
		if (!p.settings.webDashboard) return WEB_OFF_VIEW;
#ifdef BP_WEB
		if (!web || !web->running()) return WEB_FAILED_VIEW;
		// (the server listens all the same: it answers once the Wi-Fi is back)
		if (webIp.empty()) return WEB_NOWIFI_VIEW;
		return WEB_ON_VIEW;
#else
		return WEB_FAILED_VIEW;
#endif
	}

	int Ui::webClients() const
	{
#ifdef BP_WEB
		if (web && web->running()) return web->clients();
#endif
		return 0;
	}

	std::string Ui::webPin() const
	{
#ifdef BP_WEB
		if (web && web->running()) return web->pin();
#endif
		return "";
	}

	std::string Ui::webAddress() const
	{
#ifdef BP_WEB
		if (web && web->running() && !webIp.empty()) return webIp + ":" + std::to_string(web->port());
#endif
		return "";
	}

	void Ui::readWebIp()
	{
#ifdef BP_WEB
		webIp = deviceAddress();
		// tests: BP_WEB_IP stands in for the address ("none": no Wi-Fi)
		if (const char* ip = getenv("BP_WEB_IP")) webIp = strcmp(ip, "none") == 0 ? "" : ip;
#endif
		webIpAt = SDL_GetTicks();
	}

	bool Ui::webIndicatorShown(const Player& p) const
	{
		// (the photo player is left as it is: it has its own header, all its room taken; no globe in incognito)
#ifdef BP_WEB
		return p.settings.webDashboard && !incognito && !(page == Page::Main && p.playingPhoto());
#else
		(void)p;
		return false;
#endif
	}

	void Ui::drawWebIndicator(int right, int cy, const Player& p)
	{
		if (!webIndicatorShown(p)) return;
		// dim while no browser watches, yellow with one; never blinking (it's a state, not news)
		drawGlyphMid(G_WEB, right - 9, cy, webClients() > 0 ? ACCENT : DIM, 2);
		webInd = {right - 31, cy - 24, 44, 48};
	}

	std::string Ui::webSig(const Player& p) const
	{
		char b[112];
		snprintf(b, sizeof b, "|web%d %d %d %d %d %d %d %d", p.settings.webDashboard, webView(p), webClients(), pinArmed != 0, quickSub,
			webIndicatorShown(p), (int)webIndDown, (int)incognito);
		return b + webPin() + " " + webAddress();
	}

	void Ui::webTurn(bool on, Player& p)
	{
		Settings s = p.settings;
		s.webDashboard = on;
		p.setSettings(s);
		if (webPower) webPower(on);
		pinArmed = 0;
		readWebIp();
		// from the quick settings (the page shows it itself): what came of it, over the top panel
		if (page != Page::Web)
		{
			std::string addr = webAddress();
			p.osd(!on ? "Web dashboard off" : addr.empty() ? "Web dashboard on" : "Web dashboard on: " + addr, 2000);
		}
	}

	void Ui::openWeb() { webPage->open(); }

	// what draw() shows of its own: the page's state is in webSig (every page's: the header's globe, the quick
	// settings' row), and the pressed button is in Ui's signature
	std::string Ui::Web::signature(const Player&) const { return ""; }

	// the address can change (another Wi-Fi), and the page is left up: read again every 10 s; and, as the photo of a
	// QR code needs no screen after, the bottom panel dark after 90 s without a touch or a button
	void Ui::Web::tick(Player& p)
	{
		if (SDL_GetTicks() - ui.webIpAt >= 10000) ui.readWebIp();
		if (ui.panel.on() && ui.webView(p) == WEB_ON_VIEW && SDL_GetTicks() - ui.webInputAt >= 90000) ui.panel.set(false);
	}

	void Ui::Web::open()
	{
		if (ui.page == Page::Web) return;
		webBack = {ui.page, ui.settingsOver, ui.setGroup, ui.cursor, ui.scroll};
		// from Settings: back on its tile, among the groups
		if (ui.page == Page::Settings)
		{
			webBack.setGroup = -1;
			webBack.cursor = GROUP_WEB;
			webBack.scroll = 0;
		}
		// the page has a header of its own
		ui.ctxOpen = false;
		ui.settingsPage->styleFromSubs = false;
		ui.page = Page::Web;
		ui.cursor = ui.scroll = 0;
		ui.pressedBtn = -1;
		ui.pinArmed = 0;
		ui.webInputAt = SDL_GetTicks();
		ui.readWebIp();
	}

	void Ui::Web::close()
	{
		ui.page = webBack.page;
		ui.settingsOver = webBack.settingsOver;
		ui.setGroup = webBack.setGroup;
		ui.cursor = webBack.cursor;
		ui.scroll = webBack.scroll;
		ui.pinArmed = 0;
		ui.pressedBtn = -1;
	}

	void Ui::Web::webNewPin(Player& p)
	{
#ifdef BP_WEB
		if (!ui.web || !ui.web->running()) return;
		if (!ui.pinArmed || SDL_GetTicks() - ui.pinArmed >= QUIT_WINDOW_MS)
		{
			ui.pinArmed = SDL_GetTicks();
			return;
		}
		ui.pinArmed = 0;
		std::string pin = ui.web->newPin();
		if (ui.page != Page::Web) p.osd("New PIN: " + pin, 2000);
#else
		(void)p;
#endif
	}

	void Ui::Web::webPrimary(Player& p)
	{
		switch (ui.webView(p))
		{
		case WEB_OFF_VIEW: ui.webTurn(true, p); break;
		case WEB_NOWIFI_VIEW: ui.readWebIp(); break;
		case WEB_FAILED_VIEW: if (ui.webPower) ui.webPower(true); ui.readWebIp(); break;
		default: break;
		}
	}

	bool Ui::Web::key(Button b, Player& p)
	{
		int v = ui.webView(p);
		// the mini bar's right end says SELECT: back to what plays (the page has no SELECT of its own)
		if (b == Button::Select && ui.nowPlayable(ui.barPlayer(p))) { ui.goNowPlaying(p); return false; }
		// it is a settings group: L2 / R2 step to the one before / after, back on the Settings page's frame (the
		// dashboard page was opened from a group or from elsewhere; this is the group navigation either way)
		if (b == Button::L2 || b == Button::R2)
		{
			ui.page = Page::Settings;
			ui.setGroup = GROUP_WEB;
			ui.settingsPage->stepGroup(b == Button::R2 ? 1 : -1, &p);
			ui.cursor = ui.scroll = 0;
			return false;
		}
		if (v == WEB_INCOGNITO_VIEW)
		{
			// the one key the page has in incognito: leave it (the dashboard comes back by itself)
			if (b == Button::Y) ui.setIncognito(false, p);
			else if (b == Button::B) { close(); if (ui.page == Page::Main && !p.playing()) ui.page = Page::Browse; }
			return false;
		}
		if (b == Button::B)
		{
			close();
			if (ui.page == Page::Main && !p.playing()) ui.page = Page::Browse;	// (what played is over)
			return false;
		}
		if (v == WEB_ON_VIEW)
		{
			if (b == Button::Y) webNewPin(p);
			else if (b == Button::X) ui.webTurn(false, p);
		}
		else if (b == Button::A) webPrimary(p);
		else if (b == Button::X && v != WEB_OFF_VIEW) ui.webTurn(false, p);
		return false;
	}

	bool Ui::Web::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		int v = ui.webView(p);
		if (pt.kind == Pointer::Down)
		{
			ui.pressedBtn = -1;
			if (ui.buttonRect(BACK_ICON).hit(x, y)) ui.pressedBtn = BACK_ICON;
			else if (v == WEB_ON_VIEW)
			{
				for (int id : {WEB_NEWPIN, WEB_OFF})
					if (ui.buttonRect(id).hit(x, y)) ui.pressedBtn = id;
			}
			else if (ui.buttonRect(EMPTY_BTN).hit(x, y)) ui.pressedBtn = EMPTY_BTN;
			return false;
		}
		if (pt.kind != Pointer::Up || ui.pressedBtn < 0) return false;
		int id = ui.pressedBtn;
		ui.pressedBtn = -1;
		if (!ui.buttonRect(id).hit(x, y)) return false;
		if (id == BACK_ICON) return key(Button::B, p);
		if (id == WEB_NEWPIN && v == WEB_ON_VIEW) webNewPin(p);
		else if (id == WEB_OFF && v == WEB_ON_VIEW) ui.webTurn(false, p);
		else if (id == EMPTY_BTN && v == WEB_INCOGNITO_VIEW) ui.setIncognito(false, p);
		else if (id == EMPTY_BTN && v != WEB_ON_VIEW) webPrimary(p);
		return false;
	}

	void Ui::Web::draw(const Player& p)
	{
		ui.drawHeader("Web dashboard", p);
		int v = ui.webView(p);
		if (v == WEB_INCOGNITO_VIEW)
		{
			// held off by incognito: the switch at 45% with its reason, one sentence, and the way out (Y). No QR,
			// address or PIN (there is nothing to sign in to while it is off)
			Rect row{16, 72, 608, 52};
			ui.fillRound(row, SURFACE, R_BUTTON);
			ui.drawTextMid("Web dashboard", 1, DIM, 32, row.y + row.h / 2, 260);
			Rect sw{row.x + row.w - 16 - 40, row.y + row.h / 2 - 11, 40, 22};
			ui.drawTextMid("Off in incognito", 0, DIM, sw.x - 12, row.y + row.h / 2, 240, 1);
			ui.fillRound(sw, Kit::off(TRACK), 11);
			ui.fillRound({sw.x + 4, sw.y + 4, 14, 14}, Kit::off(DIM), 7);
			auto lines = ui.wrapText("The Web dashboard is off while incognito is on. It comes back by itself when you leave incognito.",
				0, 560, 3);
			for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], 0, DIM_ON, W / 2, 156 + (int)i * 24, 560, 2);
			Rect b = ui.buttonRect(EMPTY_BTN);
			ui.drawBox(b, ui.pressedBtn == EMPTY_BTN ? PRESSED : ACCENT, R_BUTTON, true);
			int gw = ui.glyphW(G_INCOGNITO), tw = (int)std::lround(ui.textWidth("Leave incognito", 0) / sx);
			int x = b.x + b.w / 2 - (gw + 10 + tw) / 2;
			ui.drawGlyphMid(G_INCOGNITO, x + gw / 2, b.y + b.h / 2, ON_ACCENT);
			ui.drawTextMid("Leave incognito", 0, ON_ACCENT, x + gw + 10, b.y + b.h / 2, 300);
			ui.drawHints({{"Y", "Leave incognito"}, {"L2/R2", "Group"}, {"B", "Back"}});
			return;
		}
		if (v != WEB_ON_VIEW)
		{
			// one of the empty pages: a mark, what it is, the lines under it (two at most), the button
			const Glyph& g = v == WEB_OFF_VIEW ? G_WEB : G_ALERT;
			std::string title = v == WEB_OFF_VIEW ? "Web dashboard is off" : v == WEB_NOWIFI_VIEW ? "No Wi-Fi" : "Couldn't start the web dashboard";
			std::string sub = v == WEB_OFF_VIEW ? "Manage files and playlists, and control the music, from a phone or computer on your Wi-Fi."
				: v == WEB_NOWIFI_VIEW ? "Connect to Wi-Fi in BananaOS first. It comes back on with the Wi-Fi."
				: "Something else is using port 8080. Try again in a moment.";
			ui.drawEmpty(g, title, "", v == WEB_OFF_VIEW ? "Turn on" : "Try again", v == WEB_OFF_VIEW ? &G_POWER : &G_REFRESH);
			auto lines = ui.wrapText(sub, 0, 520, 2);
			for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], 0, DIM, W / 2, 242 + (int)i * 24, 560, 2);
			if (v == WEB_OFF_VIEW) ui.drawHints({{"A", "Turn on"}, {"L2/R2", "Group"}, {"B", "Back"}});
			else ui.drawHints({{"A", "Try again"}, {"X", "Turn off"}, {"L2/R2", "Group"}, {"B", "Back"}});
			return;
		}
		std::string addr = ui.webAddress(), pin = ui.webPin();
#ifdef BP_WEB
		// the QR code: http://<address>/#pin=XXXX (the fragment never reaches the server; the page signs in with it)
		std::string url = "http://" + addr + "/#pin=" + pin;
		if (url != qrText)
		{
			qrText = url;
			qrMods.clear();
			qrSize = 0;
			try
			{
				qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
				qrSize = qr.getSize();
				qrMods.resize((size_t)qrSize * qrSize);
				for (int yy = 0; yy < qrSize; yy++)
					for (int xx = 0; xx < qrSize; xx++) qrMods[(size_t)yy * qrSize + xx] = qr.getModule(xx, yy) ? 1 : 0;
			}
			catch (const std::exception&)
			{
				qrSize = 0;
			}
		}
#endif
		// the card: cream, dark modules (never the other way round: phones' readers want dark on light), each module a
		// whole number of the panel's pixels, the quiet zone (3 modules at least) the card's own margin
		Rect card{24, 72, 248, 248};
		ui.fillRound(card, TEXT, R_TILE);
		if (qrSize > 0)
		{
			int x0 = (int)std::lround(card.x * sx), y0 = (int)std::lround(card.y * sy);
			int cw = (int)std::lround((card.x + card.w) * sx) - x0, ch = (int)std::lround((card.y + card.h) * sy) - y0;
			int m = std::max(1, std::min(cw, ch) / (qrSize + 6));
			int ox = x0 + (cw - m * qrSize) / 2, oy = y0 + (ch - m * qrSize) / 2;
			SDL_SetRenderDrawBlendMode(ui.ren, SDL_BLENDMODE_NONE);
			SDL_SetRenderDrawColor(ui.ren, BG.r, BG.g, BG.b, 255);
			for (int yy = 0; yy < qrSize; yy++)
				for (int xx = 0; xx < qrSize;)
				{
					if (!qrMods[(size_t)yy * qrSize + xx]) { xx++; continue; }
					int e = xx;
					while (e < qrSize && qrMods[(size_t)yy * qrSize + e]) e++;
					SDL_Rect d{ox + xx * m, oy + yy * m, (e - xx) * m, m};
					SDL_RenderFillRect(ui.ren, &d);
					xx = e;
				}
		}
		ui.drawTextMid("Scan with your phone", 0, TEXT, 148, 340, 248, 2);
		ui.drawTextMid("It signs you in too", 0, DIM, 148, 362, 248, 2);

		ui.drawTextMid("Or type this address", 0, DIM, 296, 84, 320);
		bool fits = ui.textWidth(addr, 2) / sx <= 320;
		ui.drawTextMid(addr, fits ? 2 : 1, ACCENT, 296, 116, 320);
		ui.drawTextMid("PIN", 0, DIM, 296, 160);
		for (int i = 0; i < 4; i++)
		{
			Rect bx{296 + i * 84, 176, 68, 80};
			ui.fillRound(bx, BUTTON, R_BUTTON);
			if (i >= (int)pin.size() || !ui.pinFont) continue;
			std::string key = "pin|" + pin.substr(i, 1);
			SDL_Texture* t = nullptr;
			auto it = ui.cache.find(key);
			if (it != ui.cache.end()) t = it->second;
			else if (SDL_Surface* sf = TTF_RenderUTF8_Blended(ui.pinFont, pin.substr(i, 1).c_str(), TEXT))
			{
				t = SDL_CreateTextureFromSurface(ui.ren, sf);
				SDL_FreeSurface(sf);
				ui.cache[key] = t;
			}
			if (!t) continue;
			int tw, th;
			SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
			// the capitals' middle on the box's (the font's box has room for descenders under them)
			int minx, maxx, miny, maxy, adv, cap = th / 2;
			if (TTF_GlyphMetrics(ui.pinFont, 'H', &minx, &maxx, &miny, &maxy, &adv) == 0) cap = TTF_FontAscent(ui.pinFont) - maxy / 2;
			int cx = (int)std::lround((bx.x + bx.w / 2.0) * sx), cy = (int)std::lround((bx.y + bx.h / 2.0) * sy);
			SDL_Rect d{cx - tw / 2, cy - cap, tw, th};
			SDL_RenderCopy(ui.ren, t, nullptr, &d);
		}
		int n = ui.webClients();
		ui.drawGlyphMid(G_WEB, 305, 292, n ? ACCENT : DIM);
		if (n) ui.drawTextMid(std::to_string(n) + (n == 1 ? " browser connected" : " browsers connected"), 1, TEXT, 324, 292, 300);
		else ui.drawTextMid("No browser connected yet", 0, DIM, 324, 292, 300);

		// New PIN pressed once: what the second press does, where the note is
		auto note = ui.wrapText(ui.pinArmed ? "A new PIN signs out every browser" : "Your phone needs the same Wi-Fi", 0, 248, 2);
		int noteY = 410;
		for (size_t i = 0; i < note.size(); i++)
			ui.drawTextMid(note[i], 0, ui.pinArmed ? ACCENT : DIM, 148, noteY - (int)(note.size() - 1) * 11 + (int)i * 22, 248, 2);
		Rect np = ui.buttonRect(WEB_NEWPIN), off = ui.buttonRect(WEB_OFF);
		bool npDown = ui.pressedBtn == WEB_NEWPIN;
		ui.drawBox(np, npDown ? PRESSED : ui.pinArmed ? DANGER : BUTTON, R_BUTTON);
		if (ui.pinArmed && !npDown)
		{
			// black on the red (cream reads 2.4:1 there), its key too
			ui.drawTextMid("Press again", 1, ON_ACCENT, np.x + np.w / 2, np.y + np.h * 2 / 5, np.w - 12, 2);
			ui.drawTextMid("Y", 0, ON_ACCENT, np.x + np.w / 2, np.y + np.h * 4 / 5 - 2, np.w - 6, 2);
		}
		else ui.drawButton(np, ui.pinArmed ? nullptr : &G_REFRESH, ui.pinArmed ? "Press again" : "New PIN", "Y", false, 1);
		ui.drawBox(off, ui.pressedBtn == WEB_OFF ? PRESSED : BUTTON, R_BUTTON);
		ui.drawButton(off, &G_POWER, "Turn off", "X", false, 1);
		ui.drawHints({{"Y", "New PIN"}, {"X", "Turn off"}, {"L2/R2", "Group"}, {"B", "Back"}});
	}
}
