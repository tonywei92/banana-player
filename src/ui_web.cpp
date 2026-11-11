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
	// ---- the web dashboard ----

	int Ui::webView(const Player& p) const
	{
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
		// (the photo player is left as it is: it has its own header, all its room taken)
#ifdef BP_WEB
		return p.settings.webDashboard && !(page == Page::Main && p.playingPhoto());
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
		char b[96];
		snprintf(b, sizeof b, "|web%d %d %d %d %d %d %d", p.settings.webDashboard, webView(p), webClients(), pinArmed != 0, quickSub,
			webIndicatorShown(p), (int)webIndDown);
		return b + webPin() + " " + webAddress();
	}

	void Ui::openWeb()
	{
		if (page == Page::Web) return;
		webBack = {page, inMenu, menuTab, setGroup, cursor, scroll};
		// from Settings: back on its tile, among the groups
		if (page == Page::Settings)
		{
			webBack.setGroup = -1;
			webBack.cursor = GROUP_WEB;
			webBack.scroll = 0;
		}
		// the page has a header of its own: the menu's tabs put away
		inMenu = false;
		ctxOpen = false;
		styleFromSubs = false;
		page = Page::Web;
		cursor = scroll = 0;
		pressedBtn = -1;
		pinArmed = 0;
		webInputAt = SDL_GetTicks();
		readWebIp();
	}

	void Ui::closeWeb()
	{
		page = webBack.page;
		inMenu = webBack.inMenu;
		menuTab = webBack.menuTab;
		setGroup = webBack.setGroup;
		cursor = webBack.cursor;
		scroll = webBack.scroll;
		pinArmed = 0;
		pressedBtn = -1;
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

	void Ui::webNewPin(Player& p)
	{
#ifdef BP_WEB
		if (!web || !web->running()) return;
		if (!pinArmed || SDL_GetTicks() - pinArmed >= QUIT_WINDOW_MS)
		{
			pinArmed = SDL_GetTicks();
			return;
		}
		pinArmed = 0;
		std::string pin = web->newPin();
		if (page != Page::Web) p.osd("New PIN: " + pin, 2000);
#else
		(void)p;
#endif
	}

	void Ui::webPrimary(Player& p)
	{
		switch (webView(p))
		{
		case WEB_OFF_VIEW: webTurn(true, p); break;
		case WEB_NOWIFI_VIEW: readWebIp(); break;
		case WEB_FAILED_VIEW: if (webPower) webPower(true); readWebIp(); break;
		default: break;
		}
	}

	bool Ui::webKey(Button b, Player& p)
	{
		int v = webView(p);
		// the mini bar's right end says SELECT: back to what plays (the page has no SELECT of its own)
		if (b == Button::Select && nowPlayable(barPlayer(p))) { goNowPlaying(p); return false; }
		if (b == Button::B)
		{
			closeWeb();
			if (page == Page::Main && !p.playing()) page = Page::Browse;	// (what played is over)
			return false;
		}
		if (v == WEB_ON_VIEW)
		{
			if (b == Button::Y) webNewPin(p);
			else if (b == Button::X) webTurn(false, p);
		}
		else if (b == Button::A) webPrimary(p);
		else if (b == Button::X && v != WEB_OFF_VIEW) webTurn(false, p);
		return false;
	}

	bool Ui::webPointer(const Pointer& pt, int x, int y, Player& p)
	{
		int v = webView(p);
		if (pt.kind == Pointer::Down)
		{
			pressedBtn = -1;
			if (buttonRect(BACK_ICON).hit(x, y)) pressedBtn = BACK_ICON;
			else if (v == WEB_ON_VIEW)
			{
				for (int id : {WEB_NEWPIN, WEB_OFF})
					if (buttonRect(id).hit(x, y)) pressedBtn = id;
			}
			else if (buttonRect(EMPTY_BTN).hit(x, y)) pressedBtn = EMPTY_BTN;
			return false;
		}
		if (pt.kind != Pointer::Up || pressedBtn < 0) return false;
		int id = pressedBtn;
		pressedBtn = -1;
		if (!buttonRect(id).hit(x, y)) return false;
		if (id == BACK_ICON) return webKey(Button::B, p);
		if (id == WEB_NEWPIN && v == WEB_ON_VIEW) webNewPin(p);
		else if (id == WEB_OFF && v == WEB_ON_VIEW) webTurn(false, p);
		else if (id == EMPTY_BTN && v != WEB_ON_VIEW) webPrimary(p);
		return false;
	}

	void Ui::drawWeb(const Player& p)
	{
		drawHeader("Web dashboard", p);
		int v = webView(p);
		if (v != WEB_ON_VIEW)
		{
			// one of the empty pages: a mark, what it is, the lines under it (two at most), the button
			const Glyph& g = v == WEB_OFF_VIEW ? G_WEB : G_ALERT;
			std::string title = v == WEB_OFF_VIEW ? "Web dashboard is off" : v == WEB_NOWIFI_VIEW ? "No Wi-Fi" : "Couldn't start the web dashboard";
			std::string sub = v == WEB_OFF_VIEW ? "Manage files and playlists, and control the music, from a phone or computer on your Wi-Fi."
				: v == WEB_NOWIFI_VIEW ? "Connect to Wi-Fi in BananaOS first. It comes back on with the Wi-Fi."
				: "Something else is using port 8080. Try again in a moment.";
			drawEmpty(g, title, "", v == WEB_OFF_VIEW ? "Turn on" : "Try again", v == WEB_OFF_VIEW ? &G_POWER : &G_REFRESH);
			auto lines = wrapText(sub, 0, 520, 2);
			for (size_t i = 0; i < lines.size(); i++) drawTextMid(lines[i], 0, DIM, W / 2, 242 + (int)i * 24, 560, 2);
			if (v == WEB_OFF_VIEW) drawHints({{"A", "Turn on"}, {"B", "Back"}});
			else drawHints({{"A", "Try again"}, {"X", "Turn off"}, {"B", "Back"}});
			return;
		}
		std::string addr = webAddress(), pin = webPin();
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
		fillRound(card, TEXT, R_TILE);
		if (qrSize > 0)
		{
			int x0 = (int)std::lround(card.x * sx), y0 = (int)std::lround(card.y * sy);
			int cw = (int)std::lround((card.x + card.w) * sx) - x0, ch = (int)std::lround((card.y + card.h) * sy) - y0;
			int m = std::max(1, std::min(cw, ch) / (qrSize + 6));
			int ox = x0 + (cw - m * qrSize) / 2, oy = y0 + (ch - m * qrSize) / 2;
			SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
			SDL_SetRenderDrawColor(ren, BG.r, BG.g, BG.b, 255);
			for (int yy = 0; yy < qrSize; yy++)
				for (int xx = 0; xx < qrSize;)
				{
					if (!qrMods[(size_t)yy * qrSize + xx]) { xx++; continue; }
					int e = xx;
					while (e < qrSize && qrMods[(size_t)yy * qrSize + e]) e++;
					SDL_Rect d{ox + xx * m, oy + yy * m, (e - xx) * m, m};
					SDL_RenderFillRect(ren, &d);
					xx = e;
				}
		}
		drawTextMid("Scan with your phone", 0, TEXT, 148, 340, 248, 2);
		drawTextMid("It signs you in too", 0, DIM, 148, 362, 248, 2);

		drawTextMid("Or type this address", 0, DIM, 296, 84, 320);
		bool fits = textWidth(addr, 2) / sx <= 320;
		drawTextMid(addr, fits ? 2 : 1, ACCENT, 296, 116, 320);
		drawTextMid("PIN", 0, DIM, 296, 160);
		for (int i = 0; i < 4; i++)
		{
			Rect bx{296 + i * 84, 176, 68, 80};
			fillRound(bx, BUTTON, R_BUTTON);
			if (i >= (int)pin.size() || !pinFont) continue;
			std::string key = "pin|" + pin.substr(i, 1);
			SDL_Texture* t = nullptr;
			auto it = cache.find(key);
			if (it != cache.end()) t = it->second;
			else if (SDL_Surface* sf = TTF_RenderUTF8_Blended(pinFont, pin.substr(i, 1).c_str(), TEXT))
			{
				t = SDL_CreateTextureFromSurface(ren, sf);
				SDL_FreeSurface(sf);
				cache[key] = t;
			}
			if (!t) continue;
			int tw, th;
			SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
			// the capitals' middle on the box's (the font's box has room for descenders under them)
			int minx, maxx, miny, maxy, adv, cap = th / 2;
			if (TTF_GlyphMetrics(pinFont, 'H', &minx, &maxx, &miny, &maxy, &adv) == 0) cap = TTF_FontAscent(pinFont) - maxy / 2;
			int cx = (int)std::lround((bx.x + bx.w / 2.0) * sx), cy = (int)std::lround((bx.y + bx.h / 2.0) * sy);
			SDL_Rect d{cx - tw / 2, cy - cap, tw, th};
			SDL_RenderCopy(ren, t, nullptr, &d);
		}
		int n = webClients();
		drawGlyphMid(G_WEB, 305, 292, n ? ACCENT : DIM);
		if (n) drawTextMid(std::to_string(n) + (n == 1 ? " browser connected" : " browsers connected"), 1, TEXT, 324, 292, 300);
		else drawTextMid("No browser connected yet", 0, DIM, 324, 292, 300);

		// New PIN pressed once: what the second press does, where the note is (up a line while the mini bar is there)
		auto note = wrapText(pinArmed ? "A new PIN signs out every browser" : "Your phone needs the same Wi-Fi", 0, 248, 2);
		int noteY = miniUp && note.size() > 1 ? 390 : 410;
		for (size_t i = 0; i < note.size(); i++)
			drawTextMid(note[i], 0, pinArmed ? ACCENT : DIM, 148, noteY - (int)(note.size() - 1) * 11 + (int)i * 22, 248, 2);
		Rect np = buttonRect(WEB_NEWPIN), off = buttonRect(WEB_OFF);
		bool npDown = pressedBtn == WEB_NEWPIN;
		drawBox(np, npDown ? PRESSED : pinArmed ? DANGER : BUTTON, R_BUTTON);
		if (pinArmed && !npDown)
		{
			// black on the red (cream reads 2.4:1 there), its key too
			drawTextMid("Press again", 1, ON_ACCENT, np.x + np.w / 2, np.y + np.h * 2 / 5, np.w - 12, 2);
			drawTextMid("Y", 0, ON_ACCENT, np.x + np.w / 2, np.y + np.h * 4 / 5 - 2, np.w - 6, 2);
		}
		else drawButton(np, pinArmed ? nullptr : &G_REFRESH, pinArmed ? "Press again" : "New PIN", "Y", false, 1);
		drawBox(off, pressedBtn == WEB_OFF ? PRESSED : BUTTON, R_BUTTON);
		drawButton(off, &G_POWER, "Turn off", "X", false, 1);
		drawHints({{"Y", "New PIN"}, {"X", "Turn off"}, {"B", "Back"}});
	}
}
