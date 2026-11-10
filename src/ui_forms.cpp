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
	// ---- a connection's form ----

	static const char* fieldName(int f)
	{
		switch (f)
		{
		case FORM_NAME: return "Name";
		case FORM_HOST: return "Server";
		case FORM_PORT: return "Port";
		case FORM_SHARE: return "Share";
		case FORM_FOLDER: return "Folder";
		case FORM_USER: return "User";
		case FORM_PASS: return "Password";
		case EQ_NAME: return "Preset name";
		case PL_NAME: return "Playlist name";
		default: return "";
		}
	}

	void Ui::openForm(const NetServer* existing, const FoundHost* host, Player& p)
	{
		form = existing ? *existing : NetServer();
		formId = existing ? existing->id : "";
		if (!existing)
		{
			form.type = NetType::Smb;
			form.port = defaultPort(form.type);
			form.guest = false;	// most shares (a Windows PC's) want an account
		}
		// one found on the network: by its name (an address that changes with DHCP would lose it); asked for at
		// each connection (resolveHost)
		if (host) form.host = !host->name.empty() ? host->name : host->ip;
		formPassword.clear();
		formPasswordTyped = false;
		formNote.clear();
		testText.clear();
		testSerial++;	// an answer still coming is for another form
		enterFolder(FORMDIR, p);
		// on the first thing to fill in
		int want = host ? FORM_USER : existing ? FORM_NAME : FORM_HOST;
		for (size_t i = 0; i < browseRows.size(); i++)
			if (browseRows[i].id == want) cursor = (int)i;
		fitList();
	}

	NetServer Ui::formServer() const
	{
		NetServer s = form;
		s.host = trimmed(s.host);
		if (s.port <= 0) s.port = defaultPort(s.type);
		if (s.type == NetType::Dlna) { s.share.clear(); s.folder.clear(); s.user.clear(); s.guest = true; }
		return s;
	}

	void Ui::formRows()
	{
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back("");
		};
		auto value = [](const std::string& v, const std::string& placeholder) { return v.empty() ? std::string(1, DIM_NOTE) + placeholder : v; };
		bool ftp = form.type == NetType::Ftp, dlna = form.type == NetType::Dlna;
		NetServer s = formServer();
		add(FORM_TYPE, "Type", ftp ? "< FTP >" : dlna ? "< DLNA (Plex, Jellyfin, NAS) >" : "< SMB (Windows, NAS) >");
		add(FORM_NAME, "Name", value(form.name, automaticServerName(s)));
		add(FORM_HOST, "Server", value(form.host, dlna ? "Address, name or description URL" : "Address or name"));
		std::string usualPort = defaultPort(form.type) > 0 ? std::to_string(defaultPort(form.type)) : std::string("Automatic");
		add(FORM_PORT, "Port", form.port > 0 && form.port != defaultPort(form.type) ? std::to_string(form.port) : std::string(1, DIM_NOTE) + usualPort);
		if (ftp) add(FORM_FOLDER, "Folder", value(form.folder, "/"));
		else if (!dlna) add(FORM_SHARE, "Share", value(form.share, "Any (choose after)"));
		if (!dlna) add(FORM_SIGNIN, "Sign in", form.guest ? (ftp ? "< Anonymous >" : "< As a guest >") : "< With an account >");
		if (!dlna && !form.guest)
		{
			add(FORM_USER, "User", value(form.user, "User name"));
			std::string pw = formPasswordTyped ? (formPassword.empty() ? std::string(1, DIM_NOTE) + "None" : std::string(std::min<size_t>(formPassword.size(), 12), '*'))
				: !formId.empty() && !form.secret.empty() ? std::string(1, DIM_NOTE) + "Saved" : std::string(1, DIM_NOTE) + "None";
			add(FORM_PASS, "Password", pw);
		}
		if (ftp) add(FORM_FTPS, "Encryption (FTPS)", form.ftps ? "< On >" : "< Off >");
		add(FORM_TEST, "Test connection", testText);
		add(FORM_SAVE, "Save", formNote);
		fitList();
		browseVersion++;
	}

	void Ui::formChange(int64_t id, int)
	{
		if (id == FORM_TYPE)
		{
			// the port follows the type, unless one was typed
			bool usual = form.port <= 0 || form.port == defaultPort(form.type);
			form.type = form.type == NetType::Smb ? NetType::Ftp : form.type == NetType::Ftp ? NetType::Dlna : NetType::Smb;
			if (usual) form.port = defaultPort(form.type);
			if (form.type == NetType::Dlna)
			{
				// DLNA has no share and no sign-in: what the hidden rows kept would make the saved place a folder that does not exist (Sign in is left as it was: going back to SMB must not find it changed)
				form.share.clear();
				form.folder.clear();
				form.user.clear();
				formPassword.clear();
				formPasswordTyped = false;
				form.secret.clear();
			}
		}
		else if (id == FORM_SIGNIN) form.guest = !form.guest;
		else if (id == FORM_FTPS) form.ftps = !form.ftps;
		else return;
		testText.clear();
		formRows();
	}

	void Ui::formChoose(int64_t id, Player& p)
	{
		switch (id)
		{
		case FORM_TYPE: case FORM_SIGNIN: case FORM_FTPS: formChange(id, 1); break;
		case FORM_NAME: openKeyboard(FORM_NAME, form.name); break;
		case FORM_HOST: openKeyboard(FORM_HOST, form.host); break;
		case FORM_PORT: openKeyboard(FORM_PORT, form.port > 0 && form.port != defaultPort(form.type) ? std::to_string(form.port) : ""); break;
		case FORM_SHARE: openKeyboard(FORM_SHARE, form.share); break;
		case FORM_FOLDER: openKeyboard(FORM_FOLDER, form.folder); break;
		case FORM_USER: openKeyboard(FORM_USER, form.user); break;
		case FORM_PASS: openKeyboard(FORM_PASS, formPasswordTyped ? formPassword : ""); break;
		case FORM_TEST:
		{
			NetServer s = formServer();
			if (s.host.empty()) { testText = "Type the server first"; formRows(); break; }
			const NetServer* saved = netStore && !formId.empty() ? netStore->find(formId) : nullptr;
			startTest(s, formPasswordTyped ? formPassword : saved ? netStore->password(*saved) : "");
			formRows();
			break;
		}
		case FORM_SAVE: formSave(p); break;
		default: break;
		}
	}

	void Ui::formSave(Player& p)
	{
		NetServer s = formServer();
		if (s.host.empty())
		{
			formNote = "Type the server first";
			formRows();
			for (size_t i = 0; i < browseRows.size(); i++)
				if (browseRows[i].id == FORM_HOST) cursor = (int)i;
			fitList();
			return;
		}
		if (!netStore) return;
		s.lastUsed = (long long)time(nullptr);	// the top of the list: the one just made
		std::string id = netStore->put(s, formPassword, !formId.empty() && !formPasswordTyped);
		if (netWorker) netWorker->forget(id);	// its kept connection was for what it was
		enterFolder(NETROOT, p);
		for (size_t i = 0; i < browsePaths.size(); i++)
		{
			NetPath np;
			if (parseNetPath(browsePaths[i], np) && np.server == id) cursor = (int)i;
		}
		fitList();
	}

	void Ui::startTest(const NetServer& s, const std::string& password)
	{
		if (!netWorker) { testText = "No network support in this build"; return; }
		testText = "Testing...";
		netWorker->test(++testSerial, s, password);
	}

	// ---- the servers found on the network ----

	void Ui::hostFound(const FoundHost& h, Player&)
	{
		// one per address (a better name comes later for some)
		bool known = false;
		for (auto& f : found)
			if (f.ip == h.ip) { f = h; known = true; }
		if (!known) found.push_back(h);
		if (browseDir == FINDNET && page == Page::Browse) findRows();
	}

	void Ui::findRows()
	{
		// the cursor stays on its host as others come in above it
		std::string at = cursor >= 0 && cursor < (int)found.size() ? found[cursor].ip : "";
		std::sort(found.begin(), found.end(), [](const FoundHost& a, const FoundHost& b) {
			return naturalLess(a.name.empty() ? a.ip : a.name, b.name.empty() ? b.ip : b.name);
		});
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back("");
		};
		for (size_t i = 0; i < found.size(); i++)
		{
			add(R_FOUND, found[i].name.empty() ? found[i].ip : found[i].name, found[i].ip);
			if (found[i].ip == at) cursor = (int)i;
		}
		bool running = discovery && discovery->running();
		if (running) add(-1, "Searching...", "");
		else
		{
			if (found.empty()) add(-1, discovery ? "No servers found" : "No network support in this build", "");
			add(R_SEARCH, "Search again", "");
		}
		add(R_TYPE_ADDRESS, "Type the address instead", "");
		fitList();
		browseVersion++;
	}

	// ---- the on-screen keyboard ----

	std::vector<KeyDef> Ui::kbKeys() const
	{
		return keyboardLayout(kbSymbols, kbShift, 8, 192, 624, 240);
	}

	void Ui::openKeyboard(int field, const std::string& text)
	{
		kbActiveAt = SDL_GetTicks();
		kbOpen = true;
		kbField = field;
		kbEdit.set(text);
		kbSymbols = false;
		kbShift = ShiftState::Off;
		kbCursor = keyboardHome(kbKeys());
		kbPressed = -1;
		pressedBtn = -1;
	}

	void Ui::kbClose(bool keep)
	{
		if (kbField == EQ_NAME)
		{
			// Done: checked by the player first (eqNameDone), which closes it or says what's wrong
			if (keep) { eqNameSubmit = true; return; }
			eqNameNote.clear();
		}
		if (kbField == PL_NAME)
		{
			// the same for a playlist's name (plNameDone); cancelled, a new one for Add to... adds nothing
			if (keep) { plNameSubmit = true; return; }
			plNameNote.clear();
			plNameThenAdd = false;
		}
		kbOpen = false;
		kbPressed = -1;
		if (!keep) { browseVersion++; return; }
		std::string t = kbEdit.text;
		switch (kbField)
		{
		case FORM_NAME: form.name = trimmed(t); break;
		case FORM_HOST: form.host = trimmed(t); break;
		case FORM_PORT: form.port = atoi(t.c_str()); if (form.port <= 0 || form.port > 65535) form.port = defaultPort(form.type); break;
		case FORM_SHARE:
			// "Media", "/Media/", "\\Media": the share's name (a path after it is the start folder)
			for (char& c : t) if (c == '\\') c = '/';
			t = trimmed(t);
			while (!t.empty() && t[0] == '/') t.erase(0, 1);
			while (!t.empty() && t.back() == '/') t.pop_back();
			if (size_t slash = t.find('/'); slash != std::string::npos) { form.folder = t.substr(slash + 1); t = t.substr(0, slash); }
			form.share = t;
			break;
		case FORM_FOLDER: form.folder = trimmed(t); break;
		case FORM_USER: form.user = trimmed(t); break;
		case FORM_PASS: formPassword = t; formPasswordTyped = true; break;	// (spaces kept: they can be a password's)
		default: break;
		}
		formNote.clear();
		testText.clear();
		formRows();
	}

	bool Ui::kbCaretShown() const
	{
		// on for 530 ms, off for 530, from the last key (a caret that's mid-blink as you type looks lost)
		return (SDL_GetTicks() - kbActiveAt) % 1060 < 530;
	}

	void Ui::kbKey(int i)
	{
		auto keys = kbKeys();
		if (i < 0 || i >= (int)keys.size()) return;
		const KeyDef& k = keys[i];
		switch (k.action)
		{
		case KeyAction::Text:
			// a port: digits only
			if (kbField == FORM_PORT && !(k.text.size() == 1 && isdigit((unsigned char)k.text[0]))) break;
			kbEdit.insert(k.text);
			if (kbShift == ShiftState::Once) kbShift = ShiftState::Off;
			break;
		case KeyAction::Shift:
			kbShift = kbShift == ShiftState::Off ? ShiftState::Once : kbShift == ShiftState::Once ? ShiftState::Locked : ShiftState::Off;
			break;
		case KeyAction::Back: kbEdit.backspace(); break;
		case KeyAction::Space: if (kbField != FORM_PORT) kbEdit.insert(" "); break;
		case KeyAction::Page:
			kbSymbols = !kbSymbols;
			kbCursor = std::min(kbCursor, (int)kbKeys().size() - 1);
			break;
		case KeyAction::Done: kbClose(true); break;
		case KeyAction::Left: kbEdit.left(); break;
		case KeyAction::Right: kbEdit.right(); break;
		}
	}

	void Ui::kbPress(Button b)
	{
		kbActiveAt = SDL_GetTicks();
		auto keys = kbKeys();
		switch (b)
		{
		case Button::Up: kbCursor = keyboardMove(keys, kbCursor, 0, -1); break;
		case Button::Down: kbCursor = keyboardMove(keys, kbCursor, 0, 1); break;
		case Button::Left: kbCursor = keyboardMove(keys, kbCursor, -1, 0); break;
		case Button::Right: kbCursor = keyboardMove(keys, kbCursor, 1, 0); break;
		case Button::A: kbKey(kbCursor); break;
		// B deletes; with nothing left to delete, it closes (what was there before is kept)
		case Button::B:
			if (kbEdit.text.empty()) kbClose(false);
			else kbEdit.backspace();
			break;
		case Button::X: if (kbField != FORM_PORT) kbEdit.insert(" "); break;
		case Button::Y: kbShift = kbShift == ShiftState::Off ? ShiftState::Once : ShiftState::Off; break;
		case Button::L: kbEdit.left(); break;
		case Button::R: kbEdit.right(); break;
		case Button::Select:
			kbSymbols = !kbSymbols;
			kbCursor = std::min(kbCursor, (int)kbKeys().size() - 1);
			break;
		case Button::Start: kbClose(true); break;
		default: break;
		}
	}

	void Ui::kbPointer(const Pointer& pt, int x, int y)
	{
		if (pt.kind == Pointer::Down) kbActiveAt = SDL_GetTicks();
		auto keys = kbKeys();
		Rect back = buttonRect(BACK_ICON);
		if (pt.kind == Pointer::Down)
		{
			kbPressed = back.hit(x, y) ? -2 : keyboardHit(keys, x, y);
			if (kbPressed >= 0) kbCursor = kbPressed;
		}
		else if (pt.kind == Pointer::Up)
		{
			int was = kbPressed;
			kbPressed = -1;
			// "<": what was typed is let go, the field as it was
			if (was == -2 && back.hit(x, y)) kbClose(false);
			else if (was >= 0 && keyboardHit(keys, x, y) == was) kbKey(was);
		}
	}

	void Ui::drawKeyboard()
	{
		fill({0, 0, W, H}, BG);
		Rect back = buttonRect(BACK_ICON);
		drawBox(back, kbPressed == -2 ? PRESSED : BUTTON, R_BUTTON);
		drawGlyphMid(G_BACK, back.x + back.w / 2 - 1, back.y + back.h / 2, TEXT, 3);
		drawTextMid(fieldName(kbField), 2, TEXT, 72, 32, 540);
		// the field: the text and its caret, its end in view when it's longer than the box
		fillRound({16, 92, 608, 64}, ACCENT, R_FIELD);
		fillRound({18, 94, 604, 60}, SURFACE, R_FIELD - 2);
		// the caret is a bar of its own between the two halves (a '|' in the text would move it as it blinks), with
		// room kept for it whether it shows or not
		std::string before = kbEdit.text.substr(0, kbEdit.caret), after = kbEdit.text.substr(kbEdit.caret);
		auto logical = [&](const std::string& t) { return t.empty() ? 0 : (int)std::lround(textWidth(t, 1) / sx); };
		while (logical(before) + 6 + logical(after) > 580 && !before.empty())
		{
			size_t n = 1;
			while (n < before.size() && ((unsigned char)before[n] & 0xC0) == 0x80) n++;
			before.erase(0, n);
		}
		int caretX = 32 + logical(before) + 1;
		if (!before.empty()) drawTextMid(before, 1, TEXT, 32, 124, 580);
		if (kbCaretShown()) fill({caretX, 108, 3, 32}, ACCENT);
		if (!after.empty()) drawTextMid(after, 1, TEXT, caretX + 5, 124, std::max(0, 580 - (caretX + 5 - 32)));
		if (kbField == EQ_NAME && !eqNameNote.empty()) drawTextMid(eqNameNote, 0, ACCENT, 24, 176, 592);
		if (kbField == PL_NAME && !plNameNote.empty()) drawTextMid(plNameNote, 0, ACCENT, 24, 176, 592);
		drawHints({{"A", "Type"}, {"B", "Delete"}, {"X", "Space"}, {"Y", "Shift"}, {"L/R", "Move"}, {"START", "Done"}});
		auto keys = kbKeys();
		for (int i = 0; i < (int)keys.size(); i++)
		{
			const KeyDef& k = keys[i];
			Rect r{k.x + 3, k.y + 3, k.w - 6, k.h - 6};
			bool at = i == kbCursor, on = k.action == KeyAction::Shift && kbShift != ShiftState::Off;
			bool special = k.action != KeyAction::Text;
			fillRound(r, kbPressed == i ? PRESSED : at ? ACCENT : special ? TRACK : BUTTON, R_FIELD);
			drawTextMid(k.label, 1, at ? ON_ACCENT : on ? ACCENT : TEXT, r.x + r.w / 2, r.y + r.h / 2, r.w - 4, 2);
		}
	}
}
