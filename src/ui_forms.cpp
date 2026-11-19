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
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"
#include "ui_sound.h"
#include "ui_docs.h"
#include "ui_manga.h"

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
		case FILE_NAME: return "New name";
		case FIND_FIELD: return "Find";
		case SRC_URL_FIELD: return "Repository URL";
		case MANGA_SEARCH_FIELD: return "Search";
		case MANGA_FILTER_FIELD: return "Filter";
		case MGSRV_NAME_FIELD: return "Server name";
		case MGSRV_URL_FIELD: return "Server address";
		case MGSRV_USER_FIELD: return "User";
		case MGSRV_PASS_FIELD: return "Password";
		default: return "";
		}
	}

	void Ui::Browse::openForm(const NetServer* existing, const FoundHost* host, Player& p)
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
			if (browseRows[i].id == want) ui.cursor = (int)i;
		fitList();
	}

	NetServer Ui::Browse::formServer() const
	{
		NetServer s = form;
		s.host = trimmed(s.host);
		if (s.port <= 0) s.port = defaultPort(s.type);
		return s;
	}

	void Ui::Browse::formRows()
	{
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back("");
		};
		auto value = [](const std::string& v, const std::string& placeholder) { return v.empty() ? std::string(1, DIM_NOTE) + placeholder : v; };
		bool ftp = form.type == NetType::Ftp;
		NetServer s = formServer();
		add(FORM_TYPE, "Type", ftp ? "< FTP >" : "< SMB (Windows, NAS) >");
		add(FORM_NAME, "Name", value(form.name, automaticServerName(s)));
		add(FORM_HOST, "Server", value(form.host, "Address or name"));
		add(FORM_PORT, "Port", form.port > 0 && form.port != defaultPort(form.type) ? std::to_string(form.port) : std::string(1, DIM_NOTE) + std::to_string(defaultPort(form.type)));
		if (ftp) add(FORM_FOLDER, "Folder", value(form.folder, "/"));
		else add(FORM_SHARE, "Share", value(form.share, "Any (choose after)"));
		add(FORM_SIGNIN, "Sign in", form.guest ? (ftp ? "< Anonymous >" : "< As a guest >") : "< With an account >");
		if (!form.guest)
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

	void Ui::Browse::formChange(int64_t id, int)
	{
		if (id == FORM_TYPE)
		{
			// the port follows the type, unless one was typed
			bool usual = form.port <= 0 || form.port == defaultPort(form.type);
			form.type = form.type == NetType::Smb ? NetType::Ftp : NetType::Smb;
			if (usual) form.port = defaultPort(form.type);
		}
		else if (id == FORM_SIGNIN) form.guest = !form.guest;
		else if (id == FORM_FTPS) form.ftps = !form.ftps;
		else return;
		testText.clear();
		formRows();
	}

	void Ui::Browse::formChoose(int64_t id, Player& p)
	{
		switch (id)
		{
		case FORM_TYPE: case FORM_SIGNIN: case FORM_FTPS: formChange(id, 1); break;
		case FORM_NAME: ui.openKeyboard(FORM_NAME, form.name); break;
		case FORM_HOST: ui.openKeyboard(FORM_HOST, form.host); break;
		case FORM_PORT: ui.openKeyboard(FORM_PORT, form.port > 0 && form.port != defaultPort(form.type) ? std::to_string(form.port) : ""); break;
		case FORM_SHARE: ui.openKeyboard(FORM_SHARE, form.share); break;
		case FORM_FOLDER: ui.openKeyboard(FORM_FOLDER, form.folder); break;
		case FORM_USER: ui.openKeyboard(FORM_USER, form.user); break;
		case FORM_PASS: ui.openKeyboard(FORM_PASS, formPasswordTyped ? formPassword : ""); break;
		case FORM_TEST:
		{
			NetServer s = formServer();
			if (s.host.empty()) { testText = "Type the server first"; formRows(); break; }
			const NetServer* saved = ui.netStore && !formId.empty() ? ui.netStore->find(formId) : nullptr;
			startTest(s, formPasswordTyped ? formPassword : saved ? ui.netStore->password(*saved) : "");
			formRows();
			break;
		}
		case FORM_SAVE: formSave(p); break;
		default: break;
		}
	}

	void Ui::Browse::formSave(Player& p)
	{
		NetServer s = formServer();
		if (s.host.empty())
		{
			formNote = "Type the server first";
			formRows();
			for (size_t i = 0; i < browseRows.size(); i++)
				if (browseRows[i].id == FORM_HOST) ui.cursor = (int)i;
			fitList();
			return;
		}
		if (!ui.netStore) return;
		s.lastUsed = (long long)time(nullptr);	// the top of the list: the one just made
		std::string id = ui.netStore->put(s, formPassword, !formId.empty() && !formPasswordTyped);
		if (ui.netWorker) ui.netWorker->forget(id);	// its kept connection was for what it was
		enterFolder(NETROOT, p);
		for (size_t i = 0; i < browsePaths.size(); i++)
		{
			NetPath np;
			if (parseNetPath(browsePaths[i], np) && np.server == id) ui.cursor = (int)i;
		}
		fitList();
	}

	void Ui::Browse::startTest(const NetServer& s, const std::string& password)
	{
		if (!ui.netWorker) { testText = "No network support in this build"; return; }
		testText = "Testing...";
		ui.netWorker->test(++testSerial, s, password);
	}

	void Ui::Browse::drawNetForm(const Player& p)
	{
		// the form on the kit: a form row per field (the keyboard or a choice), Test and Save as the two keys at the
		// foot (lay::formKeys' measure in the shell's body), the focused row's help in the header's crumb
		(void)p;
		Kit& k = *ui.kit;
		NetPageLayout l = netLayout(p);
		std::vector<int> cfg = formCfg();
		int n = (int)cfg.size();
		ui.cursor = std::clamp(ui.cursor, 0, n + 1);
		for (int i = ui.scroll; i < n && i < ui.scroll + l.shown; i++)
		{
			Rect r{l.list.x, l.rowsY + (i - ui.scroll) * l.rowH, l.list.w, lay::FORM_ROW};
			int id = browseRows[cfg[i]].id;
			Kit::FormField f;
			f.label = browseRows[cfg[i]].label;
			const std::string& note = browseRows[cfg[i]].note;
			if (!note.empty() && note[0] == DIM_NOTE) f.placeholder = note.substr(1);
			else f.value = note;
			f.secret = id == FORM_PASS && !f.value.empty();
			f.focused = ui.cursor == i;
			f.pressed = netPressed == i;
			k.formRow(lay::formRow(r), f);
		}
		Kit::Key test;
		test.glyph = &G_REFRESH;
		test.label = "Test connection";
		test.key = "Y";
		test.focused = ui.cursor == n;
		test.pressed = netPressed == 100;
		k.key(l.test, test);
		Kit::Key save;
		save.glyph = &G_CHECK;
		save.label = "Save";
		save.key = "START";
		save.tone = Kit::Tone::Primary;
		save.focused = ui.cursor == n + 1;
		save.pressed = netPressed == 101;
		k.key(l.save, save);
		// what Test answered, or what Save needs: the line over the keys
		if (!testText.empty() || !formNote.empty())
		{
			int cy = l.test.y - 14;
			if (!testText.empty()) ui.drawTextMid(testText, F_I12, testText == "It works" ? ACCENT : DIM, l.list.x, cy, l.list.w);
			else ui.drawTextMid(formNote, F_I12, DANGER, l.list.x, cy, l.list.w);
		}
	}

	// ---- the servers found on the network ----

	void Ui::hostFound(const FoundHost& h, Player&)
	{
		// one per address (a better name comes later for some)
		bool known = false;
		for (auto& f : browse->found)
			if (f.ip == h.ip) { f = h; known = true; }
		if (!known) browse->found.push_back(h);
		if (browse->browseDir == FINDNET && page == Page::Browse) browse->findRows();
	}

	void Ui::Browse::findRows()
	{
		// the cursor stays on its host as others come in above it
		std::string at = ui.cursor >= 0 && ui.cursor < (int)found.size() ? found[ui.cursor].ip : "";
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
			if (found[i].ip == at) ui.cursor = (int)i;
		}
		bool running = ui.discovery && ui.discovery->running();
		if (running) add(-1, "Searching...", "");
		else
		{
			if (found.empty()) add(-1, ui.discovery ? "No servers found" : "No network support in this build", "");
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
			if (keep) { sound->eqNameSubmit = true; return; }
			sound->eqNameNote.clear();
		}
		if (kbField == FIND_FIELD)
		{
			// Find: Done searches on from the place; B keeps the last query as it was
			kbOpen = false;
			kbPressed = -1;
			std::string q = trimmed(kbEdit.text);
			if (keep && !q.empty()) docs->findTyped(q);
			return;
		}
		if (kbField == FILE_NAME)
		{
			// a file's new name: checked and done by fileRenameDone, which closes it or says what's wrong
			if (keep) { browse->fileNameSubmit = true; return; }
			browse->fileNameNote.clear();
		}
		if (kbField == PL_NAME)
		{
			// the same for a playlist's name (plNameDone); cancelled, a new one for Add to... adds nothing
			if (keep) { browse->playlists->plNameSubmit = true; return; }
			browse->playlists->plNameNote.clear();
			browse->playlists->plNameThenAdd = false;
		}
		if (kbField == MANGA_SEARCH_FIELD)
		{
			// Done runs the source's search at once; B (a cancelled one) keeps the query that was there
			kbOpen = false;
			kbPressed = -1;
			if (keep) manga->mangaSourceSearch(kbEdit.text);
			else browse->browseVersion++;
			return;
		}
		if (kbField == MANGA_FILTER_FIELD)
		{
			// Done writes the text filter and the sheet is under it again; B keeps the value that was there
			kbOpen = false;
			kbPressed = -1;
			if (keep) manga->mangaFilterTextTyped(kbEdit.text);
			else browse->browseVersion++;
			return;
		}
		kbOpen = false;
		kbPressed = -1;
		if (!keep) { browse->browseVersion++; return; }
		std::string t = kbEdit.text;
		switch (kbField)
		{
		case FORM_NAME: browse->form.name = trimmed(t); break;
		case FORM_HOST: browse->form.host = trimmed(t); break;
		case FORM_PORT: browse->form.port = atoi(t.c_str()); if (browse->form.port <= 0 || browse->form.port > 65535) browse->form.port = defaultPort(browse->form.type); break;
		case FORM_SHARE:
			// "Media", "/Media/", "\\Media": the share's name (a path after it is the start folder)
			for (char& c : t) if (c == '\\') c = '/';
			t = trimmed(t);
			while (!t.empty() && t[0] == '/') t.erase(0, 1);
			while (!t.empty() && t.back() == '/') t.pop_back();
			if (size_t slash = t.find('/'); slash != std::string::npos) { browse->form.folder = t.substr(slash + 1); t = t.substr(0, slash); }
			browse->form.share = t;
			break;
		case FORM_FOLDER: browse->form.folder = trimmed(t); break;
		case FORM_USER: browse->form.user = trimmed(t); break;
		case FORM_PASS: browse->formPassword = t; browse->formPasswordTyped = true; break;	// (spaces kept: they can be a password's)
		// the Sources page's repository URL: added by the next tick, which has the Player for the answer's toast
		case SRC_URL_FIELD: manga->mangaSourcesUrlTyped(trimmed(t)); return;
		// a saved server's form (ui_manga_servers.cpp): the page's own, never a plain password kept anywhere else
		case MGSRV_NAME_FIELD:
		case MGSRV_URL_FIELD:
		case MGSRV_USER_FIELD:
		case MGSRV_PASS_FIELD: manga->mangaServerFieldTyped(kbField, t); return;
		default: break;
		}
		browse->formNote.clear();
		browse->testText.clear();
		browse->formRows();
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
		if (kbField == EQ_NAME && !sound->eqNameNote.empty()) drawTextMid(sound->eqNameNote, 0, ACCENT, 24, 176, 592);
		if (kbField == PL_NAME && !browse->playlists->plNameNote.empty()) drawTextMid(browse->playlists->plNameNote, 0, ACCENT, 24, 176, 592);
		if (kbField == FILE_NAME && !browse->fileNameNote.empty()) drawTextMid(browse->fileNameNote, 0, ACCENT, 24, 176, 592);
		// both kinds of repository take a plain address: a Lua one's, or an extensions' index (index.pb, index.min.json)
		if (kbField == SRC_URL_FIELD)
			drawTextMid("Any repository URL, including ones ending in index.pb or index.min.json", 0, DIM, 24, 176, 592);
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
