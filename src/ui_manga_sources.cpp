#include "ui.h"
#include "manganet.h"
#include "manga_ui_logic.h"
#include "ui_internal.h"
#include "ui_reader.h"
#include "ui_manga.h"
#include "ui_kit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace bp
{
	// ---- the manga reader's Sources page: installed sources, what the repositories offer, and the repositories ----

	namespace
	{
		const char* const MST_NAMES[3] = {"Installed", "Available", "Repositories"};	// Ui::MANGA_SOURCES_TABS
		const char* const NOTE = "Repositories are added by you. Banana Player doesn\xE2\x80\x99t provide or check their content.";

		std::string sourceName(const MangaRepos::InstalledSource& s) { return s.name.empty() ? s.id : s.name; }
	}

	uint64_t Ui::Manga::mangaReposSerial() const
	{
		// (ui.cpp's signature has no MangaNet's definition: the serial it follows is asked for here)
		return mangaNet && mangaNet->started() ? mangaNet->repos().serial() : 0;
	}

	std::vector<MangaRepos::InstalledSource> Ui::Manga::mangaSourcesInstalled() const
	{
		if (!mangaNet || !mangaNet->started()) return {};
		return mangaNet->repos().installed();
	}

	std::vector<MangaRepos::AvailableSource> Ui::Manga::mangaSourcesAvailable(const Player& p) const
	{
		if (!mangaNet || !mangaNet->started()) return {};
		return mangaAvLua(mangaAvFilter(p));
	}

	// ---- Available narrowed: Filter (X) and Search (L3), both at once ----

	Ui::Manga::AvFilter Ui::Manga::mangaAvFilter(const Player& p) const
	{
		AvFilter f;
		f.langs = p.settings.mangaLangs;
		f.query = mstAvQuery;
		f.adult = p.settings.mangaAdult;
		f.installedOnly = mstInstalledOnly;
		return f;
	}

	std::vector<MangaRepos::AvailableSource> Ui::Manga::mangaAvLua(const AvFilter& f) const
	{
		if (!mangaNet || !mangaNet->started() || f.installedOnly) return {};
		std::vector<MangaRepos::AvailableSource> all = mangaNet->repos().available();
		// Adult sources are the setting's to show: hidden here above all, as the design has it. Then the languages
		// Available keeps (X › Languages) and Search's words (a name, a language, the repository's name)
		all.erase(std::remove_if(all.begin(), all.end(), [&](const MangaRepos::AvailableSource& a) {
			return (a.adult && !f.adult) || !sourceListed(f.query, f.langs, a.name.empty() ? a.id : a.name, a.lang, a.repoName);
		}), all.end());
		return all;
	}

	std::vector<MangaRepos::InstalledSource> Ui::Manga::mangaAvInstalled(const AvFilter& f) const
	{
		// (what is installed shows whatever Filter says; Search's words narrow it as the rest)
		std::vector<MangaRepos::InstalledSource> all = mangaSourcesInstalled();
		all.erase(std::remove_if(all.begin(), all.end(), [&](const MangaRepos::InstalledSource& s) {
			return !sourceSearchMatch(f.query, {sourceName(s), mangaLangName(s.lang)});
		}), all.end());
		return all;
	}

	int Ui::Manga::mangaAvCount(const AvFilter& f) const
	{
		return (int)mangaAvInstalled(f).size() + mangaJavaAvCount(f) + (int)mangaAvLua(f).size();
	}

	void Ui::Manga::mangaAvSearchOpen(const Player& p)
	{
		mstAvTyping = mangaAvFilter(p);
		ui.openKeyboard(SRC_SEARCH_FIELD, mstAvQuery);
	}

	void Ui::Manga::mangaAvSearchTyped(const std::string& text)
	{
		// the list changed under its cursor: back to its top
		mstAvQuery = text;
		mstCursor[MST_AVAILABLE] = mstScroll[MST_AVAILABLE] = 0;
		mstLocal++;
	}

	void Ui::Manga::mangaAvSearchClear()
	{
		mangaAvSearchTyped("");
	}

	std::string Ui::Manga::mangaAvSearchNote(const std::string& typed) const
	{
		AvFilter all = mstAvTyping, f = mstAvTyping;
		all.query.clear();
		f.query = typed;
		int total = mangaAvCount(all), found = mangaAvCount(f);
		if (total == 0) return "";
		if (found == 0) return "No source has that in its name or language";
		bool filtered = !f.langs.empty() || f.installedOnly;
		return std::to_string(found) + " of " + std::to_string(total) + (total == 1 ? " source" : " sources") +
			(filtered ? " (Filter is on too)" : "");
	}

	void Ui::Manga::drawMangaAvSearch(const Player& p, const lay::TabPage& tp, int found)
	{
		Kit& k = *ui.kit;
		// Search and Filter at the strip's right end: compact keys (the glyph and its cap), lit while they narrow
		auto keys = lay::tabKeys(tp, 2);
		if (keys.size() == 2)
		{
			Kit::Key search, filter;
			search.glyph = &G_SEARCH;
			search.key = p.settings.buttonHotkeys ? "L3" : "";
			search.lit = !mstAvQuery.empty();
			filter.glyph = &G_EQ;
			filter.key = p.settings.buttonHotkeys ? "X" : "";
			filter.lit = !p.settings.mangaLangs.empty() || mstInstalledOnly;
			k.key(keys[0], search);
			k.key(keys[1], filter);
		}
		if (mstAvQuery.empty()) return;
		// the search line: the words and what they found (a touch types them again), Clear at its right
		lay::SearchLine sl = lay::searchLine(tp);
		int midY = sl.text.y + sl.text.h / 2;
		ui.drawGlyphMid(G_SEARCH, sl.text.x + 10, midY, ACCENT);
		std::string said = "\xE2\x80\x9C" + mstAvQuery + "\xE2\x80\x9D \xC2\xB7 " +
			(found == 0 ? "nothing found" : std::to_string(found) + " found");
		ui.drawTextMid(said, F_I14, TEXT, sl.text.x + 28, midY, sl.text.w - 28);
		Kit::Key clear;
		clear.label = "Clear";
		clear.key = p.settings.buttonHotkeys ? "B" : "";
		k.key(sl.clear, clear);
	}

	bool Ui::Manga::mangaAvSearchPointer(int x, int y, Player& p)
	{
		lay::TabPage tp = mangaSourcesTabPage(p);
		auto keys = lay::tabKeys(tp, 2);
		if (keys.size() == 2 && keys[0].hit(x, y)) { mangaAvSearchOpen(p); return true; }
		if (keys.size() == 2 && keys[1].hit(x, y)) { mangaAvMenuOpen(p); return true; }
		if (mstAvQuery.empty()) return false;
		lay::SearchLine sl = lay::searchLine(tp);
		if (sl.clear.hit(x, y)) { mangaAvSearchClear(); return true; }
		if (sl.text.hit(x, y)) { mangaAvSearchOpen(p); return true; }
		return false;
	}

	std::vector<MangaRepos::RepoInfo> Ui::Manga::mangaSourcesRepos() const
	{
		if (!mangaNet || !mangaNet->started()) return {};
		return mangaNet->repos().repos();
	}

	void Ui::Manga::mangaSourcesOpen(Player& p)
	{
		(void)p;
		mstModal = MSM_NONE;
		mstModalId.clear();
		mstJavaModal = MJM_NONE;
		mstUninstallArmed = false;
		mstLocal++;
		// the network side is idempotent: the first open of the app starts it, this only asks again
		if (!mangaNet) return;
		mangaNet->start();
		if (!mangaNet->started()) return;
		// at most hourly by the manager's own rule; Refresh (Y) forces it
		mangaNet->repos().refresh(false);
		// a Java repository's extensions are asked as soon as there is a host to ask (a use: it starts it)
		if (mangaJavaInstalled() && mangaNet->javaSources().hasRepos() && !mangaNet->javaSources().extensionsKnown())
			mangaJavaRefresh();
	}

	void Ui::Manga::openMangaSources(Player& p)
	{
		// X on Browse: a page of its own, so its lists keep their place while Browse is browsed again
		ui.page = Page::MangaSources;
		mangaTab = MT_BROWSE;
		mangaSourcesOpen(p);
		mstAvQuery.clear();	// (a search is for this visit: the page opens on its whole list)
		mangaAvFreeze(p);
	}

	bool Ui::Manga::mangaSourcesPageKey(Button b, Player& p)
	{
		// a sheet takes the keys first (B closes it, as it always has)
		if (mangaSourcesModal()) return mangaSourcesKey(b, p);
		// a search on Available: B takes it off first (the line's Clear), the next B leaves
		if (b == Button::B && mstTab == MST_AVAILABLE && !mstAvQuery.empty()) { mangaAvSearchClear(); return false; }
		if (b == Button::B)
		{
			// back to Browse, as a series page returns to the tab it came from
			ui.page = Page::Manga;
			mangaTab = MT_BROWSE;
			return false;
		}
		// the app's settings (it comes back to this page)
		if (b == Button::Start) { openMangaSettings(); return false; }
		// (the mini bar shown here says SELECT goes to what plays)
		if (b == Button::Select && ui.nowPlayable(p)) { ui.goNowPlaying(p); return false; }
		return mangaSourcesKey(b, p);
	}

	int Ui::Manga::mangaSourcesRows(const Player& p) const
	{
		switch (mstTab)
		{
		case MST_INSTALLED: return (int)mangaSourcesInstalled().size() + mangaJavaInstalledRows(p);
		case MST_AVAILABLE: return (int)mangaAvInstalled(mangaAvFilter(p)).size() + mangaJavaAvailableRows(p) + (int)mangaSourcesAvailable(p).size();
		// (+ Add repository…, the dashboard, and Download manga engine while it isn't there)
		default: return (int)mangaSourcesRepos().size() + mangaJavaRepoRows() + 2 + (mangaEngineMissing() ? 1 : 0);
		}
	}

	int Ui::Manga::mangaSourcesModalRows() const
	{
		switch (mstModal)
		{
		case MSM_DETAILS:
			for (auto& s : mangaSourcesInstalled())
				if (s.fullId == mstModalId) return s.updateAvailable ? 2 : 1;	// Update if any, then Uninstall
			return 1;
		case MSM_OPTIONS: return 2;	// Update, Uninstall
		case MSM_REPO: return 2;		// Refresh, Remove
		case MSM_REMOVE: return 3;		// Yes, No, Cancel
		default: return 0;
		}
	}

	std::string Ui::Manga::elideMiddle(const std::string& s, int size, int maxW) const
	{
		// a URL keeps both its host and its file (the middle, a long path, is what goes)
		if (maxW <= 0 || ui.textWidth(s, size) <= (int)(maxW * sx)) return s;
		for (size_t n = s.size(); n > 8; n--)
		{
			size_t head = n / 2, tail = n - head;
			std::string t = s.substr(0, head) + "\xE2\x80\xA6" + s.substr(s.size() - tail);
			if (ui.textWidth(t, size) <= (int)(maxW * sx)) return t;
		}
		return "\xE2\x80\xA6";
	}

	// ---- what the tab asks of MangaNet ----

	void Ui::Manga::mangaSourcesAddOpen(Player& p)
	{
		if (!mangaNet || !mangaNet->started()) { ui.alert("Can\xE2\x80\x99t add repositories", "The player has no folder to keep them in.");; return; }
		// the keyboard has no URL page of its own, so its letters page types a URL (its symbols page has / : .); the
		// field is prefilled, the caret after it: a host goes on the end
		ui.openKeyboard(SRC_URL_FIELD, "https://");
	}

	void Ui::Manga::mangaSourcesUrlTyped(const std::string& url)
	{
		// the keyboard's answer, added on the next tick (which has the Player for an error's toast)
		mstPendingUrl = url;
		mstLocal++;
	}

	void Ui::Manga::mangaSourcesInstall(const std::string& fullId, const std::string& repo, const std::string& id,
		const std::string& name, Player& p)
	{
		(void)p;
		if (!mangaNet || !mangaNet->started()) return;
		if (mangaNet->repos().install(repo, id))
		{
			mstInstalling.insert(fullId);
			mstInstallName[fullId] = name.empty() ? id : name;
			mstInstallAt = SDL_GetTicks();
		}
		// (false: its InstallFailed event is already queued, and says why)
		mstLocal++;
	}

	void Ui::Manga::mangaSourcesUpdate(const std::string& fullId, const std::string& repo, const std::string& id,
		const std::string& name, Player& p)
	{
		(void)p;
		if (!mangaNet || !mangaNet->started()) return;
		if (mangaNet->repos().update(repo, id))
		{
			mstInstalling.insert(fullId);
			mstInstallName[fullId] = name.empty() ? id : name;
			mstInstallAt = SDL_GetTicks();
		}
		mstLocal++;
	}

	void Ui::Manga::mangaSourcesUninstall(const std::string& fullId, const std::string& name, Player& p)
	{
		if (!mangaNet || !mangaNet->started()) return;
		if (mangaNet->repos().uninstall(fullId))
		{
			mangaNet->repos().save();
			p.osd("Removed " + (name.empty() ? std::string("the source") : name), 2000);
		}
		else ui.alert("Couldn\xE2\x80\x99t remove the source", "Is the card read only?");
		mstInstalling.erase(fullId);
		mangaSourcesCloseModal();
	}

	void Ui::Manga::mangaSourcesTick(Player& p)
	{
		// the keyboard's URL first (its toast needs the Player, which kbClose hasn't got)
		if (!mstPendingUrl.empty())
		{
			std::string url = mstPendingUrl;
			mstPendingUrl.clear();
			mstLocal++;
			if (!mangaNet || !mangaNet->started()) ui.alert("Can\xE2\x80\x99t add repositories", "The player has no folder to keep them in.");
			else if (MangaJava::isRepoUrl(url)) mangaJavaAddRepo(url, p);
			else
			{
				std::string error;
				if (!mangaNet->repos().addRepo(url, error)) ui.alert("The repository wasn\xE2\x80\x99t added", error);
				else
				{
					// its first index says Added: a load after a Refresh is silent
					mstAdding.insert(url);
					mangaNet->repos().save();
					p.osd("Adding repository\xE2\x80\xA6", 1500);
				}
			}
		}
		if (!mangaNet || !mangaNet->started()) return;
		mangaJavaPoll(p);
		MangaRepos& repos = mangaNet->repos();
		std::vector<MangaRepos::Event> events = repos.takeEvents();
		for (auto& e : events)
		{
			mstLocal++;
			switch (e.kind)
			{
			case MangaRepos::Event::RepoLoaded:
				mstRepoError.erase(e.repo);
				if (mstAdding.erase(e.repo)) p.osd("Added " + (e.message.empty() ? e.repo : e.message), 2500);
				break;
			case MangaRepos::Event::RepoFailed:
			{
				mstRepoError[e.repo] = e.message.empty() ? "couldn't load it" : e.message;
				std::string name = e.repo;
				for (auto& r : repos.repos())
					if (r.url == e.repo) { name = r.name.empty() ? r.url : r.name; break; }
				ui.alert("The repository wasn\xE2\x80\x99t added", name + ": " + mstRepoError[e.repo]);
				break;
			}
			case MangaRepos::Event::Installed:
			case MangaRepos::Event::Updated:
			{
				mstInstalling.erase(e.source);
				auto it = mstInstallName.find(e.source);
				std::string name = it != mstInstallName.end() && !it->second.empty() ? it->second : e.source;
				p.osd((e.kind == MangaRepos::Event::Installed ? "Installed " : "Updated ") + name, 2500);
				break;
			}
			case MangaRepos::Event::InstallFailed:
				mstInstalling.erase(e.source);
				ui.alert("The source couldn\xE2\x80\x99t be installed", e.message);
				break;
			}
		}
		if (!events.empty()) repos.save();
		// an answer that never came must not leave a row saying Installing… for good
		if (!mstInstalling.empty() && SDL_GetTicks() - mstInstallAt > 60000)
		{
			mstInstalling.clear();
			mstLocal++;
		}
		if (mstUninstallArmed && SDL_GetTicks() - mstUninstallAt >= QUIT_WINDOW_MS)
		{
			mstUninstallArmed = false;
			mstLocal++;
		}
	}

	// ---- the keys ----

	void Ui::Manga::mangaSourcesRefresh(Player& p)
	{
		// the repositories asked again (Y, or the list pulled down)
		if (mangaNet && mangaNet->started())
		{
			mangaNet->repos().refresh(true);
			mangaJavaRefresh();
			p.osd("Checking repositories\xE2\x80\xA6", 1200);
		}
		else ui.alert("Can\xE2\x80\x99t check repositories", "The player has no folder to keep them in.");
		mstLocal++;
	}

	bool Ui::Manga::mangaSourcesKey(Button b, Player& p)
	{
		if (mstAvMenu) return mangaAvMenuKey(b, p);
		if (mstJavaModal != MJM_NONE) return mangaJavaModalKey(b, p);
		// L2/R2 step between Available and Repositories (the cursor of each kept; L / R are the shell's sections);
		// LEFT/RIGHT go a page up or down
		if (b == Button::L || b == Button::R)
		{
			int was = mstTab;
			mstTab = b == Button::R ? MST_REPOSITORIES : MST_AVAILABLE;
			if (mstTab == MST_AVAILABLE && was != MST_AVAILABLE) mangaAvFreeze(p);
			if (mstTab != was) mstLocal++;
			return false;
		}
		int n = mangaSourcesRows(p);
		int& cur = mstCursor[mstTab], &sc = mstScroll[mstTab];
		if (n > 0 && cur >= n) cur = n - 1;
		if (b == Button::Y) mangaSourcesRefresh(p);
		else switch (b)
		{
		case Button::Up: if (n > 0) cur = (cur + n - 1) % n; break;
		case Button::Down: if (n > 0) cur = (cur + 1) % n; break;
		case Button::Left: if (n > 0) cur = std::max(0, cur - MS_ROWS); break;
		case Button::Right: if (n > 0) cur = std::min(n - 1, cur + MS_ROWS); break;
		case Button::A: mangaSourcesAct(p); break;
		// Search, as it is on the other lists: Available's sources by name
		case Button::L3: if (mstTab == MST_AVAILABLE) mangaAvSearchOpen(p); break;
		case Button::X:
			// Available: its menu (Refresh, Languages)
			if (mstTab == MST_AVAILABLE) { mangaAvMenuOpen(p); break; }
			// Options on an installed source (the design's X here, as the Series page's)
			if (mstTab == MST_INSTALLED)
			{
				auto list = mangaSourcesInstalled();
				if (cur >= 0 && cur < (int)list.size())
				{
					mstModal = MSM_OPTIONS;
					mstModalCursor = 0;
					mstModalId = list[cur].fullId;
					mstUninstallArmed = false;
					mstLocal++;
				mangaSourceSheetOpen();
				}
				else if (cur >= (int)list.size()) mangaJavaInstalledAct(cur - (int)list.size(), p);
			}
			// Repositories: X on a repository is its Remove (the sheet, on Remove: a second A does it); the rows
			// after the repositories (Add repository…, the dashboard, the engine) have no X
			if (mstTab == MST_REPOSITORIES)
			{
				auto list = mangaSourcesRepos();
				int lua = (int)list.size(), java = mangaJavaRepoRows();
				if (cur >= 0 && cur < lua)
				{
					mstModal = MSM_REPO;
					mstModalCursor = 1;
					mstModalId = list[cur].url;
					mstUninstallArmed = false;
					mstLocal++;
				mangaSourceSheetOpen();
				}
				else if (cur >= lua && cur < lua + java)
				{
					mangaJavaRepoAct(cur - lua, p);
					mstJavaModalCursor = 1;
				}
			}
			break;
		default: break;
		}
		if (n > 0)
		{
			if (cur < sc) sc = cur;
			if (cur >= sc + MS_ROWS) sc = cur - MS_ROWS + 1;
			sc = std::clamp(sc, 0, std::max(0, n - MS_ROWS));
		}
		return false;
	}

	void Ui::Manga::mangaSourcesAct(Player& p)
	{
		int& cur = mstCursor[mstTab];
		if (mstTab == MST_INSTALLED)
		{
			auto list = mangaSourcesInstalled();
			if (cur >= 0 && cur < (int)list.size())
			{
				mstModal = MSM_DETAILS;
				mstModalCursor = 0;
				mstModalId = list[cur].fullId;
				mstUninstallArmed = false;
				mstLocal++;
			mangaSourceSheetOpen();
			}
			else if (cur >= (int)list.size()) mangaJavaInstalledAct(cur - (int)list.size(), p);
			return;
		}
		if (mstTab == MST_AVAILABLE)
		{
			// the rows: the installed Lua sources (their details: Update, Uninstall), the extensions (installed on
			// top), then the Lua sources to install
			auto inst = mangaAvInstalled(mangaAvFilter(p));
			auto list = mangaSourcesAvailable(p);
			int li = (int)inst.size(), jv = mangaJavaAvailableRows(p);
			if (cur >= 0 && cur < li)
			{
				mstModal = MSM_DETAILS;
				mstModalCursor = 0;
				mstModalId = inst[cur].fullId;
				mstUninstallArmed = false;
				mstLocal++;
			mangaSourceSheetOpen();
			}
			else if (cur >= li && cur < li + jv) mangaJavaAvailableAct(cur - li, p);
			else if (cur >= li + jv && cur - li - jv < (int)list.size())
			{
				const MangaRepos::AvailableSource& a = list[cur - li - jv];
				if (mstInstalling.count(a.fullId)) return;
				mangaSourcesInstall(a.fullId, a.repo, a.id, a.name, p);
			}
			return;
		}
		auto list = mangaSourcesRepos();
		int lua = (int)list.size(), java = mangaJavaRepoRows();
		if (cur >= lua && cur < lua + java) { mangaJavaRepoAct(cur - lua, p); return; }
		if (cur == lua + java) { mangaSourcesAddOpen(p); return; }
		// a repository's long address is easier typed on a phone or a computer: the Web dashboard's page (its address
		// and PIN, or its switch), and B comes back here
		if (cur == lua + java + 1) { ui.openWeb(); return; }
		if (cur == lua + java + 2) { if (mangaEngineMissing()) mangaEngineOffer(); return; }
		if (cur < 0 || cur >= lua) return;
		mstModal = MSM_REPO;
		mstModalCursor = 0;
		mstModalId = list[cur].url;
		mstUninstallArmed = false;
		mstLocal++;
	mangaSourceSheetOpen();
	}

	void Ui::Manga::mangaSourcesCloseModal()
	{
		mstModal = MSM_NONE;
		mstModalCursor = 0;
		mstModalId.clear();
		mstUninstallArmed = false;
		mstLocal++;
	}

	void Ui::Manga::mangaSourcesModalAct(Player& p)
	{
		if (!mangaNet || !mangaNet->started()) { mangaSourcesCloseModal(); return; }
		MangaRepos& repos = mangaNet->repos();
		if (mstModal == MSM_REMOVE)
		{
			// Yes removes its sources with it, No keeps them installed (design 5.3), Cancel asks nothing
			int at = mstModalCursor;
			std::string url = mstModalId;
			if (at == 2) { mangaSourcesCloseModal(); return; }
			bool alsoSources = at == 0;
			if (repos.removeRepo(url, alsoSources))
			{
				repos.save();
				mstRepoError.erase(url);
				p.osd(alsoSources ? "Removed the repository and its sources" : "Removed the repository", 2500);
			}
			else ui.alert("Couldn\xE2\x80\x99t remove the repository", "Is the card read only?");
			mangaSourcesCloseModal();
			return;
		}
		if (mstModal == MSM_REPO)
		{
			if (mstModalCursor == 1)
			{
				mstModal = MSM_REMOVE;
				mstModalCursor = 2;	// Cancel: a repository's sources are a bigger thing to lose
				mstUninstallArmed = false;
				mstLocal++;
			}
			else
			{
				// the manager refreshes its repositories together (a Refresh with the hourly rule off)
				repos.refresh(true);
				p.osd("Checking repositories\xE2\x80\xA6", 1200);
				mangaSourcesCloseModal();
			}
			return;
		}
		auto list = mangaSourcesInstalled();
		const MangaRepos::InstalledSource* s = nullptr;
		for (auto& one : list)
			if (one.fullId == mstModalId) { s = &one; break; }
		if (!s) { mangaSourcesCloseModal(); return; }
		bool updateRow = mstModal == MSM_OPTIONS || s->updateAvailable;
		int uninstallRow = updateRow ? 1 : 0;
		if (mstModalCursor == uninstallRow)
		{
			// Uninstall is asked twice (the browser's clears are too)
			Uint32 now = SDL_GetTicks();
			if (mstUninstallArmed && now - mstUninstallAt < QUIT_WINDOW_MS) mangaSourcesUninstall(s->fullId, sourceName(*s), p);
			else { mstUninstallArmed = true; mstUninstallAt = now; mstLocal++; }
			return;
		}
		if (!s->updateAvailable)
		{
			p.osd("Already up to date", 1500);
			return;
		}
		if (mstInstalling.count(s->fullId)) return;
		mangaSourcesUpdate(s->fullId, s->repo, s->id, sourceName(*s), p);
		mangaSourcesCloseModal();
	}

	bool Ui::Manga::mangaSourcesPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down) return false;
		if (mstAvMenu) return mangaAvMenuPointer(x, y, p);
		if (mstJavaModal != MJM_NONE) return mangaJavaModalPointer(pt, x, y, p);
		// the two lists' tabs (the kit's, as drawMangaSources laid them out)
		int t = lay::tabAt(mangaSourcesTabPage(p).strip, x, y);
		if (t >= 0)
		{
			if (t == 0 && mstTab != MST_AVAILABLE) mangaAvFreeze(p);
			mstTab = t == 0 ? MST_AVAILABLE : MST_REPOSITORIES;
			mstModal = MSM_NONE;
			mstJavaModal = MJM_NONE;
			mstLocal++;
			return false;
		}
		// Available's Search and Filter keys, and the search line while there is one
		if (mstTab == MST_AVAILABLE && mangaAvSearchPointer(x, y, p)) return false;
		// the rows (the kit's): a drag scrolls them, a tap acts as A on its row
		int tab = mstTab;
		mangaRowsPointer(mlSources[tab], p, x, y, [this, &p] { return mangaSourcesRows(p); }, &mstScroll[tab], &mstCursor[tab],
			[this, &p](int i) { return mangaSourcesRowAt(p, i); }, [this, &p, tab](int i, bool) {
				if (mstTab != tab || i < 0 || i >= mangaSourcesRows(p)) return;
				mstCursor[tab] = i;
				mangaSourcesAct(p);
			});
		return false;
	}

	// ---- the drawing ----

	void Ui::Manga::drawMangaSourcesPage(const Player& p)
	{
		// the page has its own header (and so its "<", which is B): the segments start under it, as the tab's did
		ui.drawHeader("Sources", p);
		drawMangaSources(p);
	}


	Ui::Manga::MangaRow Ui::Manga::mangaSourcesRowAt(const Player& p, int k) const
	{
		// Sources' rows (screens.js manga-sources): Available (the installed Lua sources, the extensions with the
		// installed on top, then the Lua sources to install) and Repositories (each with its state in a line, then Add
		// repository…, the dashboard's way, and the engine while it isn't there)
		MangaRow m;
		auto upper = [](std::string s) { for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; };
		if (mstTab != MST_REPOSITORIES)
		{
			auto inst = mangaAvInstalled(mangaAvFilter(p));
			int li = (int)inst.size(), jv = mangaJavaAvailableRows(p);
			if (k < li)
			{
				const MangaRepos::InstalledSource& s = inst[k];
				m.glyph = &G_WEB;
				m.name = sourceName(s);
				m.second = (s.lang.empty() ? "" : upper(s.lang) + " \xC2\xB7 ") + (s.version.empty() ? "version unknown" : s.version) +
					(s.adult ? " \xC2\xB7 18+" : "");
				if (mstInstalling.count(s.fullId)) { m.pill = "Updating\xE2\x80\xA6"; m.pillLive = true; }
				else if (s.updateAvailable) { m.pill = "Update"; m.pillLive = true; }
				else m.pill = "Installed";
				return m;
			}
			if (k < li + jv) return mangaJavaRowAt(MST_AVAILABLE, k - li, p);
			auto list = mangaSourcesAvailable(p);
			int i = k - li - jv;
			if (i < 0 || i >= (int)list.size()) return m;
			const MangaRepos::AvailableSource& a = list[i];
			m.glyph = &G_DOWNLOAD;
			m.name = a.name.empty() ? a.id : a.name;
			m.second = (a.lang.empty() ? "" : upper(a.lang) + " \xC2\xB7 ") + (a.version.empty() ? "version unknown" : a.version) +
				(a.adult ? " \xC2\xB7 18+" : "");
			if (mstInstalling.count(a.fullId)) { m.pill = "Installing\xE2\x80\xA6"; m.pillLive = true; }
			else m.note = a.repoName;
			return m;
		}
		auto list = mangaSourcesRepos();
		int lua = (int)list.size(), java = mangaJavaRepoRows(), total = lua + java;
		if (k < lua)
		{
			const MangaRepos::RepoInfo& rp = list[k];
			m.glyph = &G_BLOCKS;
			m.name = rp.name.empty() ? rp.url : rp.name;
			m.second = elideMiddle(rp.url, F_I12, 380);
			auto it = mstRepoError.find(rp.url);
			if (it != mstRepoError.end()) { m.pill = "Failed"; m.pillBad = true; m.second = it->second; }
			else m.note = rp.lastFetched > 0 ? mangaAgo(rp.lastFetched, (int64_t)time(nullptr)) : "Not checked yet";
			return m;
		}
		if (k < total) return mangaJavaRowAt(MST_REPOSITORIES, k - lua, p);
		if (k == total)
		{
			m.glyph = &G_PLUS;
			m.name = "Add repository\xE2\x80\xA6";
			m.second = "Type its address here";
			return m;
		}
		if (k == total + 1)
		{
			m.glyph = &G_WEB;
			m.name = "Add from the web dashboard";
			m.second = "Paste it on your phone or computer";
			m.pill = "Recommended";
			m.pillLive = true;
			return m;
		}
		m.glyph = &G_DOWNLOAD;
		m.name = "Download manga engine";
		m.second = "Needed for index.pb repositories";
		m.note = "241 MB";
		return m;
	}

	void Ui::Manga::drawMangaSources(const Player& p)
	{
		if (mstTab == MST_INSTALLED) mstTab = MST_AVAILABLE;
		// its two lists as the kit's tabs (L2 / R2), the rows under them, the note that repositories are yours at the foot
		lay::TabPage tp = mangaSourcesTabPage(p);
		ui.kit->tabs(tp.strip.area, {MST_NAMES[MST_AVAILABLE], MST_NAMES[MST_REPOSITORIES]}, mstTab == MST_AVAILABLE ? 0 : 1, "L/R", BG);
		int n = mangaSourcesRows(p);
		int& cur = mstCursor[mstTab], &sc = mstScroll[mstTab];
		if (n > 0 && cur >= n) cur = n - 1;
		if (cur < 0) cur = 0;
		// (starting and downloading are the notice at the top of every page; a failure stays said, in the header's line)
		std::string javaStatus = mangaNet && mangaNet->javaExt().state() == JavaExtHost::State::Failed ? mangaJavaStatusLine() : "";
		if (!javaStatus.empty()) tabCrumb = javaStatus;
		lay::Box area = mangaListArea(p);
		if (mstTab == MST_AVAILABLE)
		{
			// (a list that came after Available was opened: its order is kept from its first showing)
			if (mstAvOrder.empty() && mangaJavaAvailableRows(p) > 0) mangaAvFreeze(p);
			bool listLoading = false;
			if (mangaNet && mangaNet->started() && mangaJavaInstalled() && mangaNet->javaSources().hasRepos())
			{
				auto st = mangaNet->javaExt().state();
				listLoading = mangaNet->javaSources().refreshPending() || st == JavaExtHost::State::Starting ||
					(!mangaNet->javaSources().extensionsKnown() && st != JavaExtHost::State::Failed);
			}
			drawMangaAvSearch(p, tp, n);
			bool filtered = !p.settings.mangaLangs.empty() || mstInstalledOnly;
			if (n == 0 && listLoading) ui.drawLoader(area.x + area.w / 2, area.y + area.h / 2, "Loading sources\xE2\x80\xA6");
			// a search that found nothing says which way out there is: other words, or Filter's choices as well
			else if (n == 0 && !mstAvQuery.empty())
				drawMangaTabEmpty(p, G_SEARCH, "No sources found", filtered ? "Try other words, or change Filter (X): it narrows the list too."
					: "Try other words: a source's name or its language.");
			else if (n == 0 && javaStatus.empty()) drawMangaTabEmpty(p, G_DOWNLOAD, "Nothing to install", "Add a repository first (Repositories, R2).");
			if (n > 0 && listLoading && javaStatus.empty()) tabCrumb = "Checking for updates\xE2\x80\xA6";
		}
		if (n > 0)
		{
			mlSources[mstTab].refresh = [this](Player& pl) { mangaSourcesRefresh(pl); };	// pull to refresh: as Y
			mangaRows(mlSources[mstTab], p, n, sc, cur, [this, &p](int i) { return mangaSourcesRowAt(p, i); });
		}
		// the note at the foot of both lists
		auto lines = ui.wrapText(NOTE, F_I12, area.w - 28, 2);
		int y = area.y + area.h + lay::SP12;
		ui.drawGlyphMid(G_INFO, area.x + 8, y + 2, DIM);
		for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], F_I12, DIM, area.x + 22, y + (int)i * 16, area.w - 28);

		if (mstTab == MST_AVAILABLE)
		{
			// (Y Refresh is left to Filter's sheet and a pull on the list: six hints don't fit the bar)
			if (mstAvQuery.empty()) ui.drawHints({{"A", "Install"}, {"L3", "Search"}, {"X", "Filter"}, {"L/R", "List"}, {"B", "Back"}});
			else ui.drawHints({{"A", "Install"}, {"L3", "Search"}, {"X", "Filter"}, {"B", "Clear search"}});
		}
		else if (mstCursor[MST_REPOSITORIES] < (int)mangaSourcesRepos().size() + mangaJavaRepoRows())
			ui.drawHints({{"A", "Options"}, {"X", "Remove"}, {"L/R", "List"}, {"B", "Back"}});
		else ui.drawHints({{"A", "Open"}, {"L/R", "List"}, {"Y", "Refresh"}, {"B", "Back"}});

		if (mstJavaModal != MJM_NONE) later([this, &p] { drawMangaJavaModal(p); });
		if (mstAvMenu) later([this, &p] { drawMangaAvMenu(p); });
		if (mengSheet) later([this] { drawMangaEngineSheet(); });
	}

	void Ui::Manga::mangaSourceSheetOpen()
	{
		ui.openMore([this](const Player&) { return mangaSourceSpec(); }, "mangasrc", [this] { return mstModal != MSM_NONE; },
			[this] { if (mstModal != MSM_NONE) mangaSourcesCloseModal(); }, mstModalCursor);
	}

	Ui::MoreSpec Ui::Manga::mangaSourceSpec()
	{
		MoreSpec m;
		m.focus = mstModalCursor;
		auto installed = mangaSourcesInstalled();
		const MangaRepos::InstalledSource* s = nullptr;
		for (auto& one : installed)
			if (one.fullId == mstModalId) { s = &one; break; }
		if ((mstModal == MSM_DETAILS || mstModal == MSM_OPTIONS) && !s) return m;
		if (mstModal == MSM_NONE) return m;
		auto repos = mangaSourcesRepos();
		std::string repoName;
		if (s)
			for (auto& r : repos)
				if (r.url == s->repo) { repoName = r.name.empty() ? r.url : r.name; break; }
		std::string rname;
		for (auto& r : repos)
			if (r.url == mstModalId) { rname = r.name.empty() ? r.url : r.name; break; }
		m.title = mstModal == MSM_DETAILS ? sourceName(*s) : mstModal == MSM_OPTIONS ? "Options" : mstModal == MSM_REPO ? "Repository" : "Remove repository";
		if (mstModal == MSM_DETAILS)
			m.note = (repoName.empty() ? "Not in a repository" : repoName) + " \xC2\xB7 " + (s->version.empty() ? "unknown" : s->version) + " \xC2\xB7 " +
				(s->lang.empty() ? "unknown" : s->lang);
		else if (mstModal == MSM_OPTIONS) m.note = sourceName(*s);
		else if (mstModal == MSM_REPO) m.note = elideMiddle(rname.empty() ? mstModalId : rname, 0, 360);
		else m.note = "Remove its sources too? " + elideMiddle(rname.empty() ? mstModalId : rname, 0, 200);
		if (mstUninstallArmed) m.note = "Press A again to uninstall";
		auto row = [&](const Glyph* g, const std::string& label, const std::string& note, bool danger, bool lit) {
			MoreItem it;
			it.glyph = g;
			it.label = label;
			it.note = note;
			it.danger = danger;
			it.lit = lit;
			it.stay = true;	// (the modal's own act ends it, or moves it to its next question)
			int at = (int)m.rows.size();
			it.act = [this, at](Player& p) { mstModalCursor = at; mangaSourcesModalAct(p); return false; };
			m.rows.push_back(it);
		};
		if (mstModal == MSM_DETAILS || mstModal == MSM_OPTIONS)
		{
			if (mstModal == MSM_OPTIONS || s->updateAvailable)
				row(&G_REFRESH, "Update", mstInstalling.count(s->fullId) ? "Updating\xE2\x80\xA6" : !s->updateAvailable ? "Up to date" : "", false, false);
			row(&G_TRASH, "Uninstall", mstUninstallArmed ? "Press again" : "", true, mstUninstallArmed);
		}
		else if (mstModal == MSM_REPO)
		{
			row(&G_REFRESH, "Refresh", "", false, false);
			row(&G_TRASH, "Remove", "", true, false);
		}
		else
		{
			row(&G_CHECK, "Yes", "its sources go too", true, false);
			row(&G_CROSS, "No", "its sources stay", false, false);
			row(&G_BACK, "Cancel", "", false, false);
		}
		return m;
	}

	// ---- Available's menu: Refresh, Languages ----

	bool Ui::Manga::mangaLangShown(const Player& p, const std::string& lang) const
	{
		return mangaLangKept(p.settings.mangaLangs, lang);
	}

	std::string Ui::Manga::mangaLangsSummary(const Player& p) const
	{
		auto list = splitLangs(p.settings.mangaLangs);
		if (list.empty()) return "All";
		std::string out;
		for (size_t i = 0; i < list.size() && i < 3; i++) out += (i ? ", " : "") + list[i];
		if (list.size() > 3) out += " +" + std::to_string(list.size() - 3);
		return out;
	}

	void Ui::Manga::mangaAvMenuOpen(Player& p)
	{
		(void)p;
		mstAvMenu = 1;
		mstAvCursor = mstAvScroll = 0;
		mstLocal++;
		mangaAvSheetOpen();
	}

	// Filter (X on Available): a More sheet over mstAvMenu == 1; Languages (2) is a list, a List sheet drawn by the page
	void Ui::Manga::mangaAvSheetOpen()
	{
		ui.openMore([this](const Player& p) { return mangaAvSpec(p); }, "mangaav", [this] { return mstAvMenu == 1; },
			[this] { if (mstAvMenu == 1) mstAvMenu = 0; }, mstAvCursor);
	}

	Ui::MoreSpec Ui::Manga::mangaAvSpec(const Player& p)
	{
		MoreSpec m;
		if (mstAvMenu != 1) return m;
		m.title = "Filter";
		const Glyph* G[3] = {&G_REFRESH, &G_WEB, &G_EYE};
		const char* LABELS[3] = {"Refresh", "Languages", "Installed only"};
		for (int i = 0; i < 3; i++)
		{
			MoreItem it;
			it.glyph = G[i];
			it.label = LABELS[i];
			it.note = i == 1 ? mangaLangsSummary(p) : i == 2 ? (mstInstalledOnly ? "On" : "Off") : "";
			it.lit = i == 2 && mstInstalledOnly;
			it.stay = true;	// (the keys' own act ends it, moves to the languages, or flips the switch)
			it.act = [this, i](Player& pl) { mstAvCursor = i; mangaAvMenuAct(pl); return false; };
			m.rows.push_back(it);
		}
		return m;
	}

	lay::ListSheet Ui::Manga::mangaAvLangSheet() const
	{
		return lay::listSheet(std::min(MST_AV_SHOWN, (int)mstAvLangs.size() + 1));
	}

	void Ui::Manga::mangaAvMenuAct(Player& p)
	{
		if (mstAvMenu == 1)
		{
			if (mstAvCursor == 2)
			{
				// Installed only: a switch, the menu stays
				mstInstalledOnly = !mstInstalledOnly;
				mstCursor[MST_AVAILABLE] = mstScroll[MST_AVAILABLE] = 0;
				mangaAvFreeze(p);
				mstLocal++;
				return;
			}
			if (mstAvCursor == 0)
			{
				// as Y does
				mstAvMenu = 0;
				if (mangaNet && mangaNet->started())
				{
					mangaNet->repos().refresh(true);
					mangaJavaRefresh();
					p.osd("Checking repositories\xE2\x80\xA6", 1200);
				}
			}
			else
			{
				// the languages on offer now: the Lua repositories' and the extensions', each once, sorted
				std::vector<std::string> langs;
				auto add = [&](const std::string& l) {
					if (!l.empty() && l != "multi" && std::find(langs.begin(), langs.end(), l) == langs.end())
						langs.push_back(l);
				};
				if (mangaNet && mangaNet->started())
				{
					for (const auto& a : mangaNet->repos().available()) add(a.lang);
					for (const auto& e : mangaNet->javaSources().extensions()) add(e.lang);
				}
				for (const auto& l : splitLangs(p.settings.mangaLangs)) add(l);
				std::sort(langs.begin(), langs.end());
				mstAvLangs = langs;
				mstAvMenu = 2;
				mstAvCursor = mstAvScroll = 0;
			}
			mstLocal++;
			return;
		}
		// the language list: All clears the choice; a language is ticked or unticked
		Settings s = p.settings;
		if (mstAvCursor == 0) s.mangaLangs.clear();
		else if (mstAvCursor - 1 < (int)mstAvLangs.size())
		{
			const std::string& l = mstAvLangs[mstAvCursor - 1];
			auto list = splitLangs(s.mangaLangs);
			auto it = std::find(list.begin(), list.end(), l);
			if (it != list.end()) list.erase(it);
			else list.push_back(l);
			std::sort(list.begin(), list.end());
			s.mangaLangs.clear();
			for (size_t i = 0; i < list.size(); i++) s.mangaLangs += (i ? "," : "") + list[i];
		}
		p.setSettings(s);
		// the Available list changed under its cursor: back to its top
		mstCursor[MST_AVAILABLE] = mstScroll[MST_AVAILABLE] = 0;
		mangaAvFreeze(p);
		mstLocal++;
	}

	bool Ui::Manga::mangaAvMenuKey(Button b, Player& p)
	{
		// (the languages: the Filter menu is its own sheet, which takes its keys)
		int n = (int)mstAvLangs.size() + 1;
		switch (b)
		{
		case Button::Up: mstAvCursor = (mstAvCursor + n - 1) % n; break;
		case Button::Down: mstAvCursor = (mstAvCursor + 1) % n; break;
		case Button::A: mangaAvMenuAct(p); break;
		case Button::B:
			// back to the menu
			mstAvMenu = 1;
			mstAvCursor = 1;
			mangaAvSheetOpen();
			break;
		case Button::X: mstAvMenu = 0; break;
		default: break;
		}
		if (mstAvMenu == 2)
		{
			if (mstAvCursor < mstAvScroll) mstAvScroll = mstAvCursor;
			if (mstAvCursor >= mstAvScroll + MST_AV_SHOWN) mstAvScroll = mstAvCursor - MST_AV_SHOWN + 1;
		}
		mstLocal++;
		return false;
	}

	bool Ui::Manga::mangaAvMenuPointer(int x, int y, Player& p)
	{
		lay::ListSheet sh = mangaAvLangSheet();
		if (!sh.box.hit(x, y)) { mstAvMenu = 0; mstLocal++; return false; }
		// the rows follow a finger (a smooth list); a tap is A on its row
		mangaSheetPointer(mlSrcAv, sh, (int)mstAvLangs.size() + 1, MST_AV_SHOWN, x, y, [this, &p](int i) {
			if (mstAvMenu != 2 || i < 0 || i > (int)mstAvLangs.size()) return;
			mstAvCursor = i;
			mangaAvMenuAct(p);
		});
		return false;
	}

	void Ui::Manga::drawMangaAvMenu(const Player& p)
	{
		// the languages: a List sheet, a tick on each kept
		if (mstAvMenu != 2) return;
		Kit& k = *ui.kit;
		lay::ListSheet sh = mangaAvLangSheet();
		k.sheet(sh, {&G_WEB, "Languages", "", false});
		auto chosen = splitLangs(p.settings.mangaLangs);
		int n = (int)mstAvLangs.size() + 1;
		mangaSheetRows(mlSrcAv, sh, n, MST_AV_SHOWN, mstAvScroll, mstAvCursor, [this, &chosen](int at) {
			bool on = at == 0 ? chosen.empty() : std::find(chosen.begin(), chosen.end(), mstAvLangs[at - 1]) != chosen.end();
			SheetRow r;
			r.glyph = on ? &G_CHECK : &G_BOX;
			r.name = at == 0 ? "All languages" : mangaLangName(mstAvLangs[at - 1]);
			r.note = on ? "On" : "";
			return r;
		});
		k.hints({{"A", "Tick / untick"}, {"B", "Done"}});
	}
}
