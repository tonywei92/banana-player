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
}
