// The overlay stack: the dialogs and sheets that show over a page and take its input (Ui::Overlay in ui.h). One
// that's open gets the keys and the touches (the topmost only), is drawn over the page (all of them, the oldest
// first) and has its state in signature(), from here: a new dialog is one class and the line that pushes it.
// (docs/notes/overlay-stack.md: how to add one, and how the older dialogs would move over.)
// Also here: the message dialog (Ui::alert), the smallest overlay there is, and the three dialogs that used to be a
// flag each in press/pointer/renderPage: the browser-check message, Files' Info card and the quick settings.
#include "ui_internal.h"
#include "ui_manga.h"
#include "ui_kit.h"
#include "session_host.h"
#ifdef BP_WEB
#include "web/server.h"
#endif

namespace bp
{
	Ui::Overlay* Ui::topOverlay() const
	{
		for (auto it = overlays.rbegin(); it != overlays.rend(); ++it)
			if (!(*it)->done) return it->get();
		return nullptr;
	}

	Ui::Overlay* Ui::findOverlay(const char* name) const
	{
		for (auto& o : overlays)
			if (!o->done && !strcmp(o->name(), name)) return o.get();
		return nullptr;
	}

	void Ui::pushOverlay(std::unique_ptr<Overlay> o)
	{
		// one of a kind: opened again, the new one takes the old one's place, on top. The old one is only marked (it
		// may be the one whose key() is running: an overlay that opens its own kind again)
		if (Overlay* old = findOverlay(o->name())) old->done = true;
		overlays.push_back(std::move(o));
	}

	void Ui::dropClosedOverlays()
	{
		overlays.erase(std::remove_if(overlays.begin(), overlays.end(), [](const std::unique_ptr<Overlay>& o) { return o->done; }),
			overlays.end());
		// the browse menu is its sheet: closed by B or a tap off it, its state goes too
		if (ctxOpen && !findOverlay("ctxmenu")) ctxOpen = false;
	}

	bool Ui::overlayKey(Button b, Player& p)
	{
		// (the pointer is kept: key() may push another, which moves the vector's own storage, not the overlays)
		Overlay* top = topOverlay();
		bool leave = top->key(b, p);
		dropClosedOverlays();
		return leave;
	}

	bool Ui::overlayPointer(const Pointer& pt, int x, int y, Player& p)
	{
		Overlay* top = topOverlay();
		bool leave = top->pointer(pt, x, y, p);
		dropClosedOverlays();
		return leave;
	}

	void Ui::drawOverlays(const Player& p)
	{
		for (auto& o : overlays)
			if (!o->done) o->draw(p);
	}

	std::string Ui::overlaySignature(const Player& p) const
	{
		// each one's name and state, in the stack's order (the order is drawn too)
		std::string s = "|ov";
		for (auto& o : overlays)
			if (!o->done) s += std::string("|") + o->name() + ":" + o->signature(p);
		return s;
	}

	// Turning incognito on cuts off every browser signed in to the dashboard: asked once, and only then (with nobody
	// signed in, incognito turns on at once). The safe key is Cancel (B, yellow); A turns it on. The sentence names the
	// browsers and what a playing one loses; a dim line under it names the lid choice. The word incognito never reaches
	// a browser (nothing here is sent; the server is stopped with the generic "off" goodbye).
	struct Ui::IncognitoAsk : Ui::Overlay
	{
		explicit IncognitoAsk(Ui& u) : Overlay(u) {}
		const char* name() const override { return "incogask"; }

		int browsers() const
		{
#ifdef BP_WEB
			return ui.web ? ui.web->clients() : 0;
#else
			return 0;
#endif
		}
		std::string browserName() const
		{
#ifdef BP_WEB
			if (ui.web)
			{
				auto tabs = ui.web->playTabs();
				if (!tabs.empty()) return tabs.front().name;
			}
#endif
			return "";
		}
		std::string sentence() const
		{
			int n = browsers();
			std::string s = "The Web dashboard turns off while incognito is on.";
			if (n > 0)
			{
				std::string name = n == 1 ? browserName() : "";
				s += " " + std::to_string(n) + (n == 1 ? " browser is" : " browsers are") + " signed in" +
					(name.empty() ? "" : ": " + name) + ". It can\xE2\x80\x99t reach the handheld until you leave incognito.";
			}
			if (SessionHost::get().pageIsOutput()) s += " What it plays stops; its place is kept.";
			return s;
		}
		std::string lidLine(const Player& p) const
		{
			int v = p.settings.lidIncognito;
			return std::string("Lid or sleep in incognito: ") +
				(v == LID_INCOGNITO_NOTHING ? "nothing" : v == LID_INCOGNITO_QUIT ? "quits" : "closes all") +
				" (Settings \xE2\x80\xBA Power & screens).";
		}
		int focus = 0, pressed = -1;	// 0 Cancel (the safe key), 1 Turn on
		std::vector<std::string> lines(const Player& p) const
		{
			return ui.wrapText(sentence() + " " + lidLine(p), F_I14, lay::PANEL_W - 2 * lay::SHEET_PAD - 4, 5);
		}
		void turnOn(Player& p)
		{
			close();
			ui.applyIncognito(true, p, true);
		}
		bool key(Button b, Player& p) override
		{
			if (b == Button::Left || b == Button::Right) focus ^= 1;
			else if (b == Button::A) { if (focus == 1) turnOn(p); else close(); }
			else if (b == Button::B) close();
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			lay::ConfirmSheet l = lay::confirmSheet((int)lines(p).size(), false);
			if (pt.kind == Pointer::Down)
			{
				pressed = l.safe.hit(x, y) ? 0 : l.action.hit(x, y) ? 1 : -1;
				if (!l.sheet.box.hit(x, y)) { close(); ui.swallowTouch = true; }
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int at = pressed;
			pressed = -1;
			if (at == 0 && l.safe.hit(x, y)) close();
			else if (at == 1 && l.action.hit(x, y)) turnOn(p);
			return false;
		}
		void draw(const Player& p) override
		{
			auto ls = lines(p);
			Kit::ConfirmSpec c;
			c.title = "Turn on incognito?";
			c.safe = "Cancel";
			c.action = "Turn on";
			c.focus = focus;
			c.pressed = pressed;
			ui.kit->confirmSheet(lay::confirmSheet((int)ls.size(), false), c, ls);
		}
		std::string signature(const Player&) const override
		{
			return sentence() + "|" + std::to_string(ui.webClients()) + "|" + std::to_string(focus) + std::to_string(pressed);
		}
	};

	void Ui::askIncognito() { pushOverlay(std::make_unique<IncognitoAsk>(*this)); }

	// A message that needs reading: its title, up to four lines, OK. A or B or a tap anywhere closes it (nothing
	// else, so a press meant for the page isn't lost on it).
	struct Ui::AlertBox : Ui::Overlay
	{
		std::string title, text;

		AlertBox(Ui& u, const std::string& title, const std::string& text) : Overlay(u), title(title), text(text) {}
		const char* name() const override { return "alert"; }

		bool key(Button b, Player&) override
		{
			if (b == Button::A || b == Button::B) close();
			return false;
		}

		bool pointer(const Pointer& pt, int, int, Player&) override
		{
			if (pt.kind == Pointer::Up) close();	// OK, or a tap anywhere
			return false;
		}

		void draw(const Player&) override
		{
			auto lines = ui.kit->sheetText(text);
			Kit::AlertSpec a;
			a.title = title;
			a.text = text;
			ui.kit->alertSheet(lay::alertSheet((int)lines.size()), a, lines);
		}

		std::string signature(const Player&) const override { return title + "\n" + text; }
	};

	// The first start (no lid-close in settings.conf): what closing the lid does, asked once. Pause is first and is
	// what B or a tap outside choose, as the setting's default; either answer is saved, so it's never asked again.
	// The words say where it changes later (Settings, Power & screens: its seventh row)
	struct Ui::LidAskBox : Ui::Overlay
	{
		int cursor = 0;	// 0 Pause, 1 Keep playing

		explicit LidAskBox(Ui& u) : Overlay(u) {}
		const char* name() const override { return "lidask"; }

		// one paragraph a choice, then where it changes
		static std::vector<std::string> paragraphs()
		{
			return {"Pause: playback pauses and the handheld sleeps, as usual.",
				"Keep playing: the sound goes on with the lid shut. Then the power key pauses and plays, and holding a volume key skips.",
				"Change it any time in Settings \xE2\x80\xBA Power & screens."};
		}
		std::vector<std::string> lines() const
		{
			std::vector<std::string> out;
			for (auto& t : paragraphs())
				for (auto& l : ui.wrapText(t, F_I14, lay::PANEL_W - 2 * lay::SHEET_PAD - 4, 3)) out.push_back(l);
			return out;
		}
		int pressed = -1;
		void choose(int i, Player& p)
		{
			close();
			Settings s = p.settings;
			s.lidKeepPlaying = i == 1;
			s.lidAsk = false;
			p.setSettings(s);	// (saved: the line is in settings.conf from now on)
			fprintf(stderr, "lid: asked at start, chose %s\n", i ? "Keep playing" : "Pause");
		}
		bool key(Button b, Player& p) override
		{
			if (b == Button::Left || b == Button::Right || b == Button::Up || b == Button::Down) cursor ^= 1;
			else if (b == Button::A) choose(cursor, p);
			else if (b == Button::B) choose(0, p);
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			lay::ConfirmSheet l = lay::confirmSheet((int)lines().size(), false);
			if (pt.kind == Pointer::Down)
			{
				pressed = l.safe.hit(x, y) ? 0 : l.action.hit(x, y) ? 1 : -1;
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int at = pressed;
			pressed = -1;
			if (at == 0 && l.safe.hit(x, y)) choose(0, p);
			else if (at == 1 && l.action.hit(x, y)) choose(1, p);
			else if (at < 0 && !l.sheet.box.hit(x, y)) choose(0, p);	// outside: as B
			return false;
		}
		void draw(const Player&) override
		{
			auto ls = lines();
			Kit::ConfirmSpec c;
			c.title = "When you close the lid";
			c.safe = "Pause";
			c.action = "Keep playing";
			c.focus = cursor;
			c.pressed = pressed;
			ui.kit->confirmSheet(lay::confirmSheet((int)ls.size(), false), c, ls);
		}
		std::string signature(const Player&) const override { return std::to_string(cursor) + std::to_string(pressed); }
	};

	// Home's B: Cancel is the safe key (B, or a tap outside), Quit the red action
	struct Ui::QuitAsk : Ui::Overlay
	{
		int focus = 0, pressed = -1;	// 0 Cancel, 1 Quit

		explicit QuitAsk(Ui& u) : Overlay(u) {}
		const char* name() const override { return "quitask"; }
		std::vector<std::string> lines(const Player& p) const
		{
			return ui.kit->sheetText(ui.nowPlayable(p) ? "What's playing stops." : "Banana Player closes.");
		}
		void act()
		{
			close();
			ui.quitRequested = true;
		}
		bool key(Button b, Player&) override
		{
			if (b == Button::Left || b == Button::Right || b == Button::Up || b == Button::Down) focus ^= 1;
			else if (b == Button::A) { if (focus == 1) act(); else close(); }
			else if (b == Button::B || b == Button::Start) close();
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			lay::ConfirmSheet l = lay::confirmSheet((int)lines(p).size(), false);
			if (pt.kind == Pointer::Down)
			{
				pressed = l.safe.hit(x, y) ? 0 : l.action.hit(x, y) ? 1 : -1;
				if (!l.sheet.box.hit(x, y)) { close(); ui.swallowTouch = true; }
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int at = pressed;
			pressed = -1;
			if (at == 0 && l.safe.hit(x, y)) close();
			else if (at == 1 && l.action.hit(x, y)) act();
			return false;
		}
		void draw(const Player& p) override
		{
			auto ls = lines(p);
			Kit::ConfirmSpec c;
			c.title = "Quit Banana Player?";
			c.safe = "Cancel";
			c.action = "Quit";
			c.danger = true;
			c.focus = focus;
			c.pressed = pressed;
			ui.kit->confirmSheet(lay::confirmSheet((int)ls.size(), false), c, ls);
		}
		std::string signature(const Player& p) const override
		{
			return std::to_string(focus) + std::to_string(pressed) + std::to_string(ui.nowPlayable(p));
		}
	};

	void Ui::askQuit() { pushOverlay(std::make_unique<QuitAsk>(*this)); }

	void Ui::askLid()
	{
		pushOverlay(std::make_unique<LidAskBox>(*this));
	}

	void Ui::alert(const std::string& title, const std::string& text)
	{
		pushOverlay(std::make_unique<AlertBox>(*this, title, text));
		fprintf(stderr, "ui: said \"%s\"%s%s\n", title.c_str(), text.empty() ? "" : ": ", text.c_str());
	}

	// The browser-check message: what a manga source said when it stood behind a browser check (Cloudflare,
	// DDoS-Guard) or failed it. A or B or a tap anywhere closes it, nothing else. Its text is the signature, so a new
	// error under it (a source asked again) is drawn.
	struct Ui::MangaErrDialog : Ui::Overlay
	{
		explicit MangaErrDialog(Ui& u) : Overlay(u) {}
		const char* name() const override { return "mangaerr"; }

		bool key(Button b, Player&) override
		{
			if (b == Button::A || b == Button::B) close();
			return false;
		}

		bool pointer(const Pointer& pt, int, int, Player&) override
		{
			if (pt.kind == Pointer::Up) close();	// OK, or a tap anywhere
			return false;
		}

		void draw(const Player&) override { ui.manga->drawMangaErr(); }

		std::string signature(const Player&) const override { return ui.manga->mangaErrText; }
	};

	void Ui::openMangaErr()
	{
		pushOverlay(std::make_unique<MangaErrDialog>(*this));
	}

	// The quick settings: F's rows (volume, the screens' brightness, Same for both screens, the web dashboard, incognito
	// mode, Quit). The rows, their drawing and their keys are Ui's (ui_menus.cpp): this only puts them on the stack and
	// keeps the page's rules that ran before them.
	struct Ui::QuickSheet : Ui::Overlay
	{
		explicit QuickSheet(Ui& u) : Overlay(u) {}
		const char* name() const override { return "quick"; }

		bool key(Button b, Player& p) override
		{
			// dark (Screen off, or off by itself): the key brings the panel back and does nothing else
			if (!ui.panel.on())
			{
				ui.panel.set(true);
				return false;
			}
			// a copy or move running (F opened the sheet under its card): only its Cancel answers, as on a page
			if (ui.fileJob) return ui.fileJobKey(b);
			return ui.quickKey(b, p);
		}

		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			// the page's rules before the sheet's own: a copy or move running (its card holds the panel), dark (a touch
			// brings the panel back and presses nothing), then the touch that woke it
			if (ui.fileJob) return ui.fileJobPointer(pt, x, y);
			if (!ui.panel.on())
			{
				if (pt.kind == Pointer::Down)
				{
					ui.panel.set(true);
					ui.swallowTouch = true;
				}
				return false;
			}
			if (ui.swallowTouch)
			{
				if (pt.kind == Pointer::Up) ui.swallowTouch = false;
				return false;
			}
			return ui.quickPointer(pt, x, y, p);
		}

		void draw(const Player& p) override { ui.drawQuick(p); }

		std::string signature(const Player& p) const override
		{
			// the cursor, the volume, a button under a finger and the web row's half aren't settings; nor are the
			// screens' levels (the keys change them too), which the backlight holds. The Now playing block's state,
			// its art (arriving: the serial), its title and its place are (its progress line moves with the seconds)
			const Player& bp = ui.barPlayer(p);
			bool live = ui.nowPlayable(bp);
			char now[320];
			snprintf(now, sizeof now, "|np%d %d %d %d %ld %ld %s|%s|", live, live && bp.paused, (int)bp.playingAudio(),
				bp.error.empty() ? 0 : 1, (long)std::floor(ui.shownPosition(bp)), (long)bp.duration,
				live ? bp.artPlaceholderKey().c_str() : "", live ? bp.title.c_str() : "");
			return std::to_string(ui.quickCursor) + " " + std::to_string(ui.volumePct) + " " + std::to_string(ui.pressedBtn) +
				" " + std::to_string(ui.backlight.permille(Backlight::TOP)) + "," + std::to_string(ui.backlight.permille(Backlight::BOTTOM)) +
				" " + std::to_string(ui.quickSub) + " " + std::to_string(ui.artSerial) + now +
				(live && !bp.paused ? std::to_string(SDL_GetTicks() / 166) : "");
		}
	};

	void Ui::openQuick()
	{
		pushOverlay(std::make_unique<QuickSheet>(*this));
	}
}
