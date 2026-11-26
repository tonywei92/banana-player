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
	namespace
	{
		// The page's boxes (net-web: the QR at the left, the address and PIN large at the right, the keys under
		// them). The switch row is at the top in every state; its trail is the switch itself.
		struct WebPageLayout
		{
			lay::Box body, sw, qr, addrHead, addr, pinHead, pin, newPin, off, empty;
		};

		WebPageLayout webPageLayout()
		{
			WebPageLayout l;
			l.body = {16, 64, 608, 376};
			l.sw = {16, 64, 608, 52};
			l.qr = {16, 128, 196, 196};
			l.addrHead = {228, 126, 396, 22};
			l.addr = {228, 150, 396, 44};
			l.pinHead = {228, 206, 396, 22};
			l.pin = {228, 228, 396, 64};
			l.newPin = {16, 384, 300, 56};
			l.off = {324, 384, 300, 56};
			l.empty = {16, 248, 240, 56};
			return l;
		}
	}

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

	bool Ui::wifiOff() const
	{
#ifdef BP_WEB
		return webIpAt != 0 && webIp.empty();
#else
		return false;
#endif
	}

	void Ui::wifiTick()
	{
#ifdef BP_WEB
		if (webIpAt == 0 || SDL_GetTicks() - webIpAt >= 10000) readWebIp();
#endif
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
		drawGlyphMid(G_QR, right - 9, cy, webClients() > 0 ? ACCENT : DIM, 2);
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
		fprintf(stderr, "web: a new PIN from the handheld; every browser signed out\n");
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
		// it is a settings group: L / R step to the one before / after, back on the Settings page's frame (the
		// dashboard page was opened from a group or from elsewhere; this is the group navigation either way)
		if (b == Button::L || b == Button::R)
		{
			ui.page = Page::Settings;
			ui.setGroup = GROUP_WEB;
			ui.settingsPage->stepGroup(b == Button::R ? 1 : -1, &p);
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
			// A is the switch itself (the spec: on the switch, on / off); Y a new PIN, X also turns it off
			if (b == Button::A) ui.webTurn(false, p);
			else if (b == Button::Y) webNewPin(p);
			else if (b == Button::X) ui.webTurn(false, p);
		}
		else if (b == Button::A) webPrimary(p);
		else if (b == Button::X && v != WEB_OFF_VIEW) ui.webTurn(false, p);
		return false;
	}

	bool Ui::Web::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		int v = ui.webView(p);
		WebPageLayout l = webPageLayout();
		lay::PlayerLayout header = lay::shellHeader({0, 0, W, lay::SHELL_HEAD});
		// (the button the finger went down on is taken before pressedBtn is cleared at the lift, so it is not compared with it)
		auto on = [&](int, const lay::Box& b) { return b.hit(x, y); };
		if (pt.kind == Pointer::Down)
		{
			ui.pressedBtn = -1;
			if (header.backHit.hit(x, y)) ui.pressedBtn = BACK_ICON;
			else if (l.sw.hit(x, y)) ui.pressedBtn = WEB_SWITCH;
			else if (v == WEB_ON_VIEW)
			{
				if (l.newPin.hit(x, y)) ui.pressedBtn = WEB_NEWPIN;
				else if (l.off.hit(x, y)) ui.pressedBtn = WEB_OFF;
			}
			else if (l.empty.hit(x, y)) ui.pressedBtn = EMPTY_BTN;
			return false;
		}
		if (pt.kind != Pointer::Up || ui.pressedBtn < 0) return false;
		int id = ui.pressedBtn;
		ui.pressedBtn = -1;
		if (id == BACK_ICON) { if (header.backHit.hit(x, y)) return key(Button::B, p); return false; }
		if (id == WEB_SWITCH) { if (on(id, l.sw)) key(Button::A, p); return false; }	// the switch: on / off
		if (id == WEB_NEWPIN && v == WEB_ON_VIEW) { if (on(id, l.newPin)) webNewPin(p); }
		else if (id == WEB_OFF && v == WEB_ON_VIEW) { if (on(id, l.off)) ui.webTurn(false, p); }
		else if (id == EMPTY_BTN && v == WEB_INCOGNITO_VIEW) { if (on(id, l.empty)) ui.setIncognito(false, p); }
		else if (id == EMPTY_BTN && v != WEB_ON_VIEW) { if (on(id, l.empty)) webPrimary(p); }
		return false;
	}

	void Ui::Web::draw(const Player& p)
	{
		Kit& k = *ui.kit;
		WebPageLayout l = webPageLayout();
		int v = ui.webView(p);
		// the header: the back key, the title beside it and the crumb, then the status cluster (its globe marks the
		// dashboard); the page has no rail (it is a settings group opened from anywhere)
		lay::PlayerLayout header = lay::shellHeader({0, 0, W, lay::SHELL_HEAD});
		Kit::Header hd;
		hd.title = "Web dashboard";
		hd.crumb = "Settings";
		hd.inlineTitle = true;
		hd.back = true;
		hd.backPressed = ui.pressedBtn == BACK_ICON;
		hd.more = false;
		hd.right = [this, &p](int right, int cy) {
			Kit::Status st;
			st.clock = ui.clockText(p);
			st.battery = ui.battery.percent;
			st.charging = ui.battery.charging;
			ui.kit->stateBadges(st, p);
			if (ui.webIndicatorShown(p)) st.badges.push_back({&G_QR, ""});
			return ui.kit->status(right, cy, st);
		};
		k.header(header, hd);
		// the switch row, on every state: the one control a finger needs (the trail is drawn by Kit::row; the row is
		// the touch target, as a switch's always is)
		Kit::Row sw;
		sw.name = "Web dashboard";
		sw.trail = v == WEB_INCOGNITO_VIEW || v == WEB_OFF_VIEW ? Kit::Trail::SwitchOff : Kit::Trail::SwitchOn;
		sw.off = v == WEB_INCOGNITO_VIEW;
		sw.note = v == WEB_ON_VIEW ? (ui.webClients() ? std::to_string(ui.webClients()) + (ui.webClients() == 1 ? " browser" : " browsers")
			: "On") : v == WEB_OFF_VIEW ? "Off" : v == WEB_INCOGNITO_VIEW ? "Off in incognito" : "On, no address";
		sw.pressed = ui.pressedBtn == WEB_SWITCH;
		sw.under = BG;
		k.row(l.sw, sw);
		if (v == WEB_INCOGNITO_VIEW)
		{
			// held off by incognito: the switch at 45% with its reason (above), one sentence and the way out. No QR,
			// address or PIN: there is nothing to sign in to while it is off
			auto lines = ui.wrapText("The Web dashboard is off while incognito is on. It comes back by itself when you "
				"leave incognito.", F_I14, 560, 3);
			int y = l.body.y + 70;
			for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], F_I14, DIM_ON, l.body.x, y + (int)i * 22, 560);
			Kit::Key leave;
			leave.glyph = &G_INCOGNITO;
			leave.label = "Leave incognito";
			leave.key = "Y";
			leave.pressed = ui.pressedBtn == EMPTY_BTN;
			k.key(l.empty, leave);
			k.hints({0, lay::PANEL_H - lay::HINTS_H, W, lay::HINTS_H}, {{"Y", "Leave incognito"}, {"L/R", "Group"}, {"B", "Back"}});
			return;
		}
		if (v != WEB_ON_VIEW)
		{
			// off: the switch off, one line on what it is for, nothing else; no Wi-Fi or a failed start: the same
			// switch stays on and a notice says why, with Try again
			if (v == WEB_OFF_VIEW)
			{
				auto lines = ui.wrapText("Manage files and playlists, and control the music, from a phone or computer "
					"on your Wi-Fi.", F_I14, 560, 3);
				int y = l.body.y + 70;
				for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], F_I14, DIM_ON, l.body.x, y + (int)i * 22, 560);
				k.hints({0, lay::PANEL_H - lay::HINTS_H, W, lay::HINTS_H}, {{"A", "Turn on"}, {"B", "Back"}});
				return;
			}
			lay::Notice nl = lay::notice({l.body.x, l.body.y + 58, l.body.w}, true, false, 0);
			Kit::NoticeRow n;
			n.tone = Kit::NoticeTone::Failed;
			n.glyph = v == WEB_NOWIFI_VIEW ? &G_NETWORK : &G_ALERT;
			n.text = v == WEB_NOWIFI_VIEW ? "No Wi-Fi" : "The dashboard couldn't start";
			n.second = v == WEB_NOWIFI_VIEW ? "Connect to Wi-Fi in BananaOS first. It comes back on with the Wi-Fi."
				: "Something else is using port 8080. Try again in a moment.";
			k.notice(nl, n);
			Kit::Key again;
			again.glyph = &G_REFRESH;
			again.label = "Try again";
			again.key = "A";
			again.pressed = ui.pressedBtn == EMPTY_BTN;
			k.key(l.empty, again);
			k.hints({0, lay::PANEL_H - lay::HINTS_H, W, lay::HINTS_H},
				{{"A", "Try again"}, {"X", "Turn off"}, {"L/R", "Group"}, {"B", "Back"}});
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
		lay::Box card = l.qr;
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
		ui.drawTextMid("Scan with your phone", F_I12, TEXT, card.x + card.w / 2, card.y + card.h + 12, card.w, 2);
		ui.drawTextMid("It signs you in too", F_I12, DIM, card.x + card.w / 2, card.y + card.h + 30, card.w, 2);

		// Address and PIN, large at the right of the code
		Kit::SectionLine sl;
		sl.text = "Address";
		sl.caps = true;
		k.sectionLine(lay::sectionLine(l.addrHead, false), sl);
		ui.drawTextMid(addr, F_P28, ACCENT, l.addr.x, l.addr.y + l.addr.h / 2, l.addr.w);
		Kit::SectionLine pl2;
		pl2.text = "PIN";
		pl2.caps = true;
		k.sectionLine(lay::sectionLine(l.pinHead, false), pl2);
		for (int i = 0; i < 4; i++)
		{
			lay::Box bx{l.pin.x + i * 60, l.pin.y, 52, l.pin.h};
			ui.fillRound(bx, TRACK, R_BUTTON);
			if (i >= (int)pin.size() || !ui.pinFont) continue;
			std::string key = "pin|" + pin.substr(i, 1);
			SDL_Texture* t = nullptr;
			if (SDL_Texture** hit = ui.cache.find(key)) t = *hit;
			else if (SDL_Surface* sf = TTF_RenderUTF8_Blended(ui.pinFont, pin.substr(i, 1).c_str(), TEXT))
			{
				t = SDL_CreateTextureFromSurface(ui.ren, sf);
				SDL_FreeSurface(sf);
				ui.cache.put(key, t);
			}
			if (!t) continue;
			int tw, th;
			SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
			// the capitals' middle on the box's (the font's box has room for descenders under them)
			int minx, maxx, miny, maxy, adv, cap = th / 2;
			if (TTF_GlyphMetrics(ui.pinFont, 'H', &minx, &maxx, &miny, &maxy, &adv) == 0) cap = TTF_FontAscent(ui.pinFont) - maxy / 2;
			int cx = (int)std::lround((bx.x + bx.w / 2.0) * sx), cy = (int)std::lround((bx.y + bx.h / 2.0) * sy);
			SDL_Rect src{0, 0, tw, th}, d{cx - tw / 2, cy - cap, tw, th};
			SDL_RenderCopy(ui.ren, t, &src, &d);
		}
		int n = ui.webClients();
		int cy = l.pin.y + l.pin.h + 14;
		ui.drawGlyphMid(G_QR, l.pin.x + 9, cy, n ? ACCENT : DIM);
		if (n) ui.drawTextMid(std::to_string(n) + (n == 1 ? " browser connected" : " browsers connected"), F_I12, TEXT,
			l.pin.x + 28, cy, l.pin.w - 28);
		else ui.drawTextMid("No browser connected yet", F_I12, DIM, l.pin.x + 28, cy, l.pin.w - 28);
		// what New PIN's second press does, or what the phone needs
		auto note = ui.wrapText(ui.pinArmed ? "A new PIN signs out every browser" : "Your phone needs the same Wi-Fi",
			F_I12, l.pin.w, 2);
		int noteY = l.newPin.y - 10 - (int)note.size() * 18;
		for (size_t i = 0; i < note.size(); i++)
			ui.drawTextMid(note[i], F_I12, ui.pinArmed ? ACCENT : DIM, l.pin.x, noteY + (int)i * 18, l.pin.w);

		Kit::Key np;
		np.glyph = ui.pinArmed ? nullptr : &G_REFRESH;
		np.label = ui.pinArmed ? "Press again" : "New PIN";
		np.key = "Y";
		np.tone = ui.pinArmed ? Kit::Tone::Danger : Kit::Tone::Normal;
		np.pressed = ui.pressedBtn == WEB_NEWPIN;
		k.key(l.newPin, np);
		Kit::Key off;
		off.glyph = &G_POWER;
		off.label = "Turn off";
		off.key = "X";
		off.pressed = ui.pressedBtn == WEB_OFF;
		k.key(l.off, off);
		k.hints({0, lay::PANEL_H - lay::HINTS_H, W, lay::HINTS_H},
			{{"A", "On / off"}, {"Y", "New PIN"}, {"X", "Turn off"}, {"L/R", "Group"}, {"B", "Back"}});
	}
}
