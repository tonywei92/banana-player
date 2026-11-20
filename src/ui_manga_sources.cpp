#include "ui.h"
#include "manganet.h"
#include "manga_ui_logic.h"
#include "ui_internal.h"
#include "ui_reader.h"
#include "ui_manga.h"
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
		std::vector<MangaRepos::AvailableSource> all = mangaNet->repos().available();
		// Adult sources are the setting's to show: hidden here above all, as the design has it
		if (!p.settings.mangaAdult)
			all.erase(std::remove_if(all.begin(), all.end(), [](const MangaRepos::AvailableSource& a) { return a.adult; }),
				all.end());
		if (mstInstalledOnly) return {};
		// and the languages Available keeps (X › Languages)
		all.erase(std::remove_if(all.begin(), all.end(), [&](const MangaRepos::AvailableSource& a) { return !mangaLangShown(p, a.lang); }),
			all.end());
		return all;
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
		mangaAvFreeze(p);
	}

	bool Ui::Manga::mangaSourcesPageKey(Button b, Player& p)
	{
		// a sheet takes the keys first (B closes it, as it always has)
		if (mangaSourcesModal()) return mangaSourcesKey(b, p);
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
		case MST_AVAILABLE: return (int)mangaSourcesInstalled().size() + mangaJavaAvailableRows(p) + (int)mangaSourcesAvailable(p).size();
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

	bool Ui::Manga::mangaSourcesKey(Button b, Player& p)
	{
		if (mstAvMenu) return mangaAvMenuKey(b, p);
		if (mstJavaModal != MJM_NONE) return mangaJavaModalKey(b, p);
		if (mstModal != MSM_NONE) return mangaSourcesModalKey(b, p);
		// L2/R2 step between Available and Repositories (the cursor of each kept; L / R are the shell's sections);
		// LEFT/RIGHT go a page up or down
		if (b == Button::L2 || b == Button::R2)
		{
			int was = mstTab;
			mstTab = b == Button::R2 ? MST_REPOSITORIES : MST_AVAILABLE;
			if (mstTab == MST_AVAILABLE && was != MST_AVAILABLE) mangaAvFreeze(p);
			if (mstTab != was) mstLocal++;
			return false;
		}
		int n = mangaSourcesRows(p);
		int& cur = mstCursor[mstTab], &sc = mstScroll[mstTab];
		if (n > 0 && cur >= n) cur = n - 1;
		if (b == Button::Y)
		{
			if (mangaNet && mangaNet->started())
			{
				mangaNet->repos().refresh(true);
				mangaJavaRefresh();
				p.osd("Checking repositories\xE2\x80\xA6", 1200);
			}
			else ui.alert("Can\xE2\x80\x99t check repositories", "The player has no folder to keep them in.");
			mstLocal++;
		}
		else switch (b)
		{
		case Button::Up: if (n > 0) cur = (cur + n - 1) % n; break;
		case Button::Down: if (n > 0) cur = (cur + 1) % n; break;
		case Button::Left: if (n > 0) cur = std::max(0, cur - MS_ROWS); break;
		case Button::Right: if (n > 0) cur = std::min(n - 1, cur + MS_ROWS); break;
		case Button::A: mangaSourcesAct(p); break;
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
			}
			else if (cur >= (int)list.size()) mangaJavaInstalledAct(cur - (int)list.size(), p);
			return;
		}
		if (mstTab == MST_AVAILABLE)
		{
			// the rows: the installed Lua sources (their details: Update, Uninstall), the extensions (installed on
			// top), then the Lua sources to install
			auto inst = mangaSourcesInstalled();
			auto list = mangaSourcesAvailable(p);
			int li = (int)inst.size(), jv = mangaJavaAvailableRows(p);
			if (cur >= 0 && cur < li)
			{
				mstModal = MSM_DETAILS;
				mstModalCursor = 0;
				mstModalId = inst[cur].fullId;
				mstUninstallArmed = false;
				mstLocal++;
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
	}

	void Ui::Manga::mangaSourcesCloseModal()
	{
		mstModal = MSM_NONE;
		mstModalCursor = 0;
		mstModalId.clear();
		mstUninstallArmed = false;
		mstLocal++;
	}

	bool Ui::Manga::mangaSourcesModalKey(Button b, Player& p)
	{
		int n = mangaSourcesModalRows();
		if (n <= 0) { mangaSourcesCloseModal(); return false; }
		if (mstModalCursor >= n) mstModalCursor = n - 1;
		switch (b)
		{
		case Button::Up: mstModalCursor = (mstModalCursor + n - 1) % n; break;
		case Button::Down: mstModalCursor = (mstModalCursor + 1) % n; break;
		case Button::Left: if (mstModal == MSM_REMOVE) mstModalCursor = std::max(0, mstModalCursor - 1); break;
		case Button::Right: if (mstModal == MSM_REMOVE) mstModalCursor = std::min(n - 1, mstModalCursor + 1); break;
		case Button::A: mangaSourcesModalAct(p); break;
		case Button::B: mangaSourcesCloseModal(); break;
		default: break;
		}
		return false;
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
		if (mstModal != MSM_NONE)
		{
			int n = mangaSourcesModalRows();
			for (int i = 0; i < n; i++)
				if (mangaSourcesModalRowRect(i).hit(x, y))
				{
					mstModalCursor = i;
					mangaSourcesModalAct(p);
					return false;
				}
			// a tap outside the sheet closes it (the back icon is outside it too)
			if (!mangaSourcesModalBox().hit(x, y)) mangaSourcesCloseModal();
			return false;
		}
		for (int i = 0; i < 2; i++)
			if (mangaSourcesSegRect(i).hit(x, y))
			{
				if (i == 0 && mstTab != MST_AVAILABLE) mangaAvFreeze(p);
				mstTab = i == 0 ? MST_AVAILABLE : MST_REPOSITORIES;
				mstModal = MSM_NONE;
				mstJavaModal = MJM_NONE;
				mstLocal++;
				return false;
			}
		// the rows follow a finger: a drag scrolls them, a tap (lifted where it went down) acts as A on its row. The
		// draw keeps the scroll on the cursor, so the cursor moves with the rows shown
		int n = mangaSourcesRows(p), sc = mstScroll[mstTab];
		if (!n || y < MS_ROW_TOP || y >= MS_ROW_TOP + MS_ROWS * MS_ROW_H) return false;
		int hit = -1;
		for (int i = sc; i < n && i < sc + MS_ROWS; i++)
			if (mangaSourcesRowRect(i - sc).hit(x, y)) hit = i;
		int tab = mstTab;
		ui.pageReader->mangaListBegin(x, y, sc, MS_ROW_H, hit, [this, &p, tab](int want) {
			if (mstTab != tab) return want;
			int n = mangaSourcesRows(p), &cur = mstCursor[tab], &sc = mstScroll[tab];
			sc = std::clamp(want, 0, std::max(0, n - MS_ROWS));
			cur = std::clamp(cur, sc, std::max(sc, std::min(n - 1, sc + MS_ROWS - 1)));
			return sc;
		}, [this, &p, tab](int i) {
			if (mstTab != tab || i >= mangaSourcesRows(p)) return;
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


	// an installed Lua source's row: name, language, version, its Update chip (Available lists these on top)
	void Ui::Manga::drawMangaLuaInstalledRow(const MangaRepos::InstalledSource& s, Rect r, bool at)
	{
		if (at) ui.drawFocus(r);
		int mid = r.y + r.h / 2;
		SDL_Color ink = at ? ACCENT : TEXT, soft = at ? DIM_ON : DIM;
		ui.drawTextMid(sourceName(s), 1, ink, 32, mid - 10, 450);
		int x = 32;
		if (!s.lang.empty())
		{
			int cw = (int)std::lround(ui.textWidth(s.lang, 0) / sx) + 14;
			ui.fillRound({x, mid + 3, cw, 18}, TRACK, R_CHIP);
			ui.drawTextMid(s.lang, 0, at ? DIM_ON : TEXT, x + cw / 2, mid + 12, cw - 6, 2);
			x += cw + 8;
		}
		ui.drawTextMid(s.version.empty() ? "version unknown" : s.version, 0, soft, x, mid + 12, 240);
		if (s.adult) drawAdultChip(x + std::min(240, (int)std::lround(ui.textWidth(s.version.empty() ? "version unknown" : s.version, 0) / sx)) + 8, mid + 12);
		if (mstInstalling.count(s.fullId)) ui.drawTextMid("Updating\xE2\x80\xA6", 0, ACCENT, 608, mid, 0, 1);
		else if (s.updateAvailable)
		{
			// the design's Update mark: a chip on the row's right
			int w = (int)std::lround(ui.textWidth("Update", 0) / sx) + 16;
			ui.fillRound({608 - w, mid - 10, w, 20}, ACCENT, R_CHIP);
			ui.drawTextMid("Update", 0, ON_ACCENT, 608 - w / 2, mid, w - 6, 2);
		}
	}

	void Ui::Manga::drawMangaSources(const Player& p)
	{
		if (mstTab == MST_INSTALLED) mstTab = MST_AVAILABLE;
		for (int i = 0; i < 2; i++)
		{
			int tab = i == 0 ? MST_AVAILABLE : MST_REPOSITORIES;
			Rect r = mangaSourcesSegRect(i);
			bool at = tab == mstTab;
			ui.drawBox(r, at ? ACCENT : SURFACE, R_CHIP);
			ui.drawTextMid(MST_NAMES[tab], 0, at ? ON_ACCENT : TEXT, r.x + r.w / 2, r.y + r.h / 2, r.w - 16, 2);
		}
		ui.drawTextMid("L2", 0, ACCENT, 12, mangaSourcesSegRect(0).y + 13, 0, 2);
		ui.drawTextMid("R2", 0, ACCENT, 628, mangaSourcesSegRect(1).y + 13, 0, 2);
		int n = mangaSourcesRows(p);
		int& cur = mstCursor[mstTab], &sc = mstScroll[mstTab];
		if (n > 0 && cur >= n) cur = n - 1;
		if (cur < 0) cur = 0;
		if (n > MS_ROWS) sc = std::clamp(sc, 0, n - MS_ROWS);
		else sc = 0;
		if (cur < sc) sc = cur;
		if (cur >= sc + MS_ROWS) sc = cur - MS_ROWS + 1;

		// (starting and downloading are the notice at the top of every page now; a failure stays said here)
		std::string javaStatus = mangaNet && mangaNet->javaExt().state() == JavaExtHost::State::Failed ? mangaJavaStatusLine() : "";
		if (mstTab == MST_INSTALLED)
		{
			auto list = mangaSourcesInstalled();
			int lua = (int)list.size(), java = mangaJavaInstalledRows(p);
			if (lua + java == 0 && javaStatus.empty())
				ui.drawEmpty(G_BLOCKS, "No sources yet", "Add a repository, then install from Available", nullptr);
			for (int i = sc; i < lua && i < sc + MS_ROWS; i++)
			{
				drawMangaLuaInstalledRow(list[i], mangaSourcesRowRect(i - sc), i == cur);
			}
			for (int i = std::max(sc, lua); i < lua + java && i < sc + MS_ROWS; i++)
				drawMangaJavaRow(MST_INSTALLED, i - lua, mangaSourcesRowRect(i - sc), i == cur, true, p);
			ui.drawScrollBar(MS_ROW_TOP, MS_ROWS * MS_ROW_H - 4, sc, MS_ROWS, lua + java);
		}
		else if (mstTab == MST_AVAILABLE)
		{
			// (a list that came after Available was opened: its order is kept from its first showing)
			if (mstAvOrder.empty() && mangaJavaAvailableRows(p) > 0) mangaAvFreeze(p);
			// installed Lua sources, the extensions (installed on top), then the Lua sources to install
			auto inst = mangaSourcesInstalled();
			auto list = mangaSourcesAvailable(p);
			int li = (int)inst.size(), jv = mangaJavaAvailableRows(p), total = li + jv + (int)list.size();
			// the extensions' list on its way (the engine starting, or the list asked): the loading mark, in the middle
			// when there's nothing to show yet, else a line at the foot over the last list kept
			bool listLoading = false;
			if (mangaNet && mangaNet->started() && mangaJavaInstalled() && mangaNet->javaSources().hasRepos())
			{
				auto st = mangaNet->javaExt().state();
				listLoading = mangaNet->javaSources().refreshPending() || st == JavaExtHost::State::Starting ||
					(!mangaNet->javaSources().extensionsKnown() && st != JavaExtHost::State::Failed);
			}
			if (total == 0 && listLoading) ui.drawLoader(W / 2, 270, "Loading sources\xE2\x80\xA6");
			else if (total == 0 && javaStatus.empty())
				ui.drawEmpty(G_DOWNLOAD, "Nothing to install", "Add a repository first", nullptr);
			for (int k = sc; k < total && k < sc + MS_ROWS; k++)
			{
				if (k < li) { drawMangaLuaInstalledRow(inst[k], mangaSourcesRowRect(k - sc), k == cur); continue; }
				if (k < li + jv) { drawMangaJavaRow(MST_AVAILABLE, k - li, mangaSourcesRowRect(k - sc), k == cur, true, p); continue; }
				const MangaRepos::AvailableSource& a = list[k - li - jv];
				Rect r = mangaSourcesRowRect(k - sc);
				bool at = k == cur;
				if (at) ui.drawFocus(r);
				int mid = r.y + r.h / 2;
				SDL_Color ink = at ? ACCENT : TEXT, soft = at ? DIM_ON : DIM;
				ui.drawTextMid(a.name.empty() ? a.id : a.name, 1, ink, 32, mid - 10, 450);
				int x = 32;
				if (!a.lang.empty())
				{
					int cw = (int)std::lround(ui.textWidth(a.lang, 0) / sx) + 14;
					ui.fillRound({x, mid + 3, cw, 18}, TRACK, R_CHIP);
					ui.drawTextMid(a.lang, 0, at ? DIM_ON : TEXT, x + cw / 2, mid + 12, cw - 6, 2);
					x += cw + 8;
				}
				ui.drawTextMid(a.version.empty() ? "version unknown" : a.version, 0, soft, x, mid + 12, 240);
				if (a.adult) drawAdultChip(x + std::min(240, (int)std::lround(ui.textWidth(a.version.empty() ? "version unknown" : a.version, 0) / sx)) + 8, mid + 12);
				if (mstInstalling.count(a.fullId)) ui.drawTextMid("Installing\xE2\x80\xA6", 0, ACCENT, 608, mid, 0, 1);
				else ui.drawTextMid(ui.elideLeft(a.repoName, 0, 150), 0, soft, 608, mid, 0, 1);
			}
			ui.drawScrollBar(MS_ROW_TOP, MS_ROWS * MS_ROW_H - 4, sc, MS_ROWS, total);
			if (total > 0 && listLoading)
			{
				std::string t = "Checking for updates\xE2\x80\xA6";
				int tw = (int)std::lround(ui.textWidth(t, 0) / sx) + 56;
				Rect r{W / 2 - tw / 2, MS_ROW_TOP + MS_ROWS * MS_ROW_H - 38, tw, 30};
				ui.drawBox(r, SURFACE, R_CHIP, ACCENT);
				// a small ring at its left, the same mark as the loading one
				ui.loaderShown = true;
				int step = (int)(SDL_GetTicks() / 110 % 8);
				for (int k = 0; k < 8; k++)
				{
					int age = ((step - k) % 8 + 8) % 8;
					double a = k * 3.14159265 / 4;
					int dx = r.x + 20 + (int)std::lround(8 * std::sin(a)), dy = r.y + 15 + (int)std::lround(-8 * std::cos(a));
					ui.fillRound({dx - 2, dy - 2, 4, 4}, age == 0 ? ACCENT : age < 3 ? TEXT : DIM, 2);
				}
				ui.drawTextMid(t, 0, TEXT, r.x + 36, r.y + r.h / 2, tw - 40);
			}
		}
		else
		{
			auto list = mangaSourcesRepos();
			int lua = (int)list.size(), java = mangaJavaRepoRows(), total = lua + java;
			for (int i = sc; i < lua && i < sc + MS_ROWS; i++)
			{
				const MangaRepos::RepoInfo& rp = list[i];
				Rect r = mangaSourcesRowRect(i - sc);
				bool at = i == cur;
				if (at) ui.drawFocus(r);
				int mid = r.y + r.h / 2;
				SDL_Color ink = at ? ACCENT : TEXT, soft = at ? DIM_ON : DIM;
				ui.drawTextMid(rp.name.empty() ? rp.url : rp.name, 1, ink, 32, mid - 10, 470);
				ui.drawTextMid(elideMiddle(rp.url, 0, 380), 0, soft, 32, mid + 10, 0);
				auto it = mstRepoError.find(rp.url);
				if (it != mstRepoError.end()) ui.drawTextMid(ui.elideLeft(it->second, 0, 180), 0, DANGER, 608, mid, 0, 1);
				else if (rp.lastFetched > 0)
					ui.drawTextMid(mangaAgo(rp.lastFetched, (int64_t)time(nullptr)), 0, soft, 608, mid, 0, 1);
				else ui.drawTextMid("Not checked yet", 0, soft, 608, mid, 0, 1);
			}
			for (int i = std::max(sc, lua); i < total && i < sc + MS_ROWS; i++)
				drawMangaJavaRow(MST_REPOSITORIES, i - lua, mangaSourcesRowRect(i - sc), i == cur, true, p);
			// the last row, Add repository…
			int i = total - sc;
			if (i >= 0 && i < MS_ROWS)
			{
				Rect r = mangaSourcesRowRect(i);
				bool at = cur == total;
				if (at) ui.drawFocus(r);
				int mid = r.y + r.h / 2;
				ui.drawGlyphMid(G_PLUS, 42, mid, at ? ACCENT : DIM);
				ui.drawTextMid("Add repository\xE2\x80\xA6", 1, at ? ACCENT : TEXT, 62, mid, 0);
			}
			// and the recommended way: the dashboard, where the address is pasted on a phone or a computer
			i = total + 1 - sc;
			if (i >= 0 && i < MS_ROWS)
			{
				Rect r = mangaSourcesRowRect(i);
				bool at = cur == total + 1;
				if (at) ui.drawFocus(r);
				int mid = r.y + r.h / 2;
				ui.drawGlyphMid(G_WEB, 42, mid, at ? ACCENT : DIM);
				ui.drawTextMid("Add from the web dashboard", 1, at ? ACCENT : TEXT, 62, mid - 10, 400);
				ui.drawTextMid("Paste it on your phone or computer", 0, at ? DIM_ON : DIM, 62, mid + 10, 400);
				int cw = (int)std::lround(ui.textWidth("Recommended", 0) / sx) + 16;
				ui.fillRound({608 - cw, mid - 10, cw, 20}, ACCENT, R_CHIP);
				ui.drawTextMid("Recommended", 0, ON_ACCENT, 608 - cw / 2, mid, cw - 6, 2);
			}
			// the engine, while it isn't on the card: an index.pb repository's sources need it (the offer again)
			bool engineRow = mangaEngineMissing();
			i = total + 2 - sc;
			if (engineRow && i >= 0 && i < MS_ROWS)
			{
				Rect r = mangaSourcesRowRect(i);
				bool at = cur == total + 2;
				if (at) ui.drawFocus(r);
				int mid = r.y + r.h / 2;
				ui.drawGlyphMid(G_DOWNLOAD, 42, mid, at ? ACCENT : DIM);
				ui.drawTextMid("Download manga engine", 1, at ? ACCENT : TEXT, 62, mid - 10, 400);
				ui.drawTextMid("Needed for index.pb repositories", 0, at ? DIM_ON : DIM, 62, mid + 10, 400);
				ui.drawTextMid("241 MB", 0, at ? DIM_ON : DIM, 608, mid, 0, 1);
			}
			ui.drawScrollBar(MS_ROW_TOP, MS_ROWS * MS_ROW_H - 4, sc, MS_ROWS, total + 2 + (engineRow ? 1 : 0));
		}

		if (!javaStatus.empty()) ui.drawTextMid(javaStatus, 0, DANGER, W / 2, 378, 560, 2);

		// the note at the foot of every sub-list (design 9's Repositories row)
		auto lines = ui.wrapText(NOTE, 0, 552, 2);
		ui.drawGlyphMid(G_INFO, 24, 396, DIM);
		for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], 0, DIM, 40, 396 + (int)i * 20, 552);

		if (mstTab == MST_INSTALLED)
			ui.drawHints({{"A", "Details"}, {"X", "Options"}, {"L2/R2", "List"}, {"Y", "Refresh"}, {"B", "Back"}});
		else if (mstTab == MST_AVAILABLE)
			ui.drawHints({{"A", "Install"}, {"X", "Filter"}, {"L2/R2", "List"}, {"Y", "Refresh"}, {"B", "Back"}});
		else if (mstCursor[MST_REPOSITORIES] < (int)mangaSourcesRepos().size() + mangaJavaRepoRows())
			ui.drawHints({{"A", "Options"}, {"X", "Remove"}, {"L2/R2", "List"}, {"B", "Back"}});
		else ui.drawHints({{"A", "Open"}, {"L2/R2", "List"}, {"Y", "Refresh"}, {"B", "Back"}});

		if (mstModal != MSM_NONE) later([this, &p] { drawMangaSourceModal(p); });
		if (mstJavaModal != MJM_NONE) later([this, &p] { drawMangaJavaModal(p); });
		if (mstAvMenu) later([this, &p] { drawMangaAvMenu(p); });
		if (mengSheet) later([this] { drawMangaEngineSheet(); });
	}

	void Ui::Manga::drawMangaSourceModal(const Player& p)
	{
		(void)p;
		auto installed = mangaSourcesInstalled();
		const MangaRepos::InstalledSource* s = nullptr;
		for (auto& one : installed)
			if (one.fullId == mstModalId) { s = &one; break; }
		if ((mstModal == MSM_DETAILS || mstModal == MSM_OPTIONS) && !s) { mstModal = MSM_NONE; return; }
		auto repos = mangaSourcesRepos();
		std::string repoName;
		if (s)
			for (auto& r : repos)
				if (r.url == s->repo) { repoName = r.name.empty() ? r.url : r.name; break; }
		Rect box = mangaSourcesModalBox();
		std::string title = mstModal == MSM_DETAILS ? sourceName(*s)
			: mstModal == MSM_OPTIONS ? "Options"
			: mstModal == MSM_REPO ? "Repository"
			: "Remove repository";
		ui.drawDialog(box, title);
		int rows = mangaSourcesModalRows();
		if (mstModal == MSM_DETAILS)
		{
			auto line = [&](const char* label, const std::string& value, int y) {
				ui.drawTextMid(label, 0, DIM, box.x + 24, y, 104);
				ui.drawTextMid(value, 0, TEXT, box.x + 132, y, box.w - 156);
			};
			line("Repository", repoName.empty() ? "Not in a repository" : repoName, box.y + 58);
			line("Version", s->version.empty() ? "unknown" : s->version, box.y + 80);
			line("Language", s->lang.empty() ? "unknown" : s->lang, box.y + 102);
		}
		else if (mstModal == MSM_OPTIONS)
			ui.drawTextMid(sourceName(*s), 0, DIM, box.x + box.w / 2, box.y + 62, box.w - 40, 2);
		else if (mstModal == MSM_REPO)
		{
			std::string name;
			for (auto& r : repos)
				if (r.url == mstModalId) { name = r.name.empty() ? r.url : r.name; break; }
			ui.drawTextMid(elideMiddle(name.empty() ? mstModalId : name, 0, box.w - 40), 0, DIM, box.x + box.w / 2,
				box.y + 62, 0, 2);
		}
		else if (mstModal == MSM_REMOVE)
		{
			std::string name;
			for (auto& r : repos)
				if (r.url == mstModalId) { name = r.name.empty() ? r.url : r.name; break; }
			ui.drawTextMid("Remove its sources too?", 0, TEXT, box.x + box.w / 2, box.y + 64, box.w - 40, 2);
			ui.drawTextMid(elideMiddle(name.empty() ? mstModalId : name, 0, box.w - 40), 0, DIM, box.x + box.w / 2,
				box.y + 86, 0, 2);
		}
		bool updateRow = mstModal == MSM_OPTIONS || (s && s->updateAvailable);
		int uninstallRow = updateRow ? 1 : 0;
		for (int i = 0; i < rows; i++)
		{
			Rect r = mangaSourcesModalRowRect(i);
			bool at = i == mstModalCursor;
			const Glyph* g = nullptr;
			std::string label, note;
			bool dim = false;
			if (mstModal == MSM_DETAILS || mstModal == MSM_OPTIONS)
			{
				if (updateRow && i == 0)
				{
					label = "Update";
					g = &G_REFRESH;
					dim = !s->updateAvailable;
					if (mstInstalling.count(s->fullId)) note = "Updating\xE2\x80\xA6";
					else if (dim) note = "Up to date";
				}
				else if (i == uninstallRow)
				{
					label = "Uninstall";
					g = &G_TRASH;
					if (mstUninstallArmed) note = "Press again";
				}
			}
			else if (mstModal == MSM_REPO)
			{
				label = i == 0 ? "Refresh" : "Remove";
				g = i == 0 ? &G_REFRESH : &G_TRASH;
			}
			else
			{
				if (i == 0) { label = "Yes"; note = "its sources go too"; }
				else if (i == 1) { label = "No"; note = "its sources stay"; }
				else label = "Cancel";
			}
			ui.drawDialogRow(r, g, label, at, false, dim ? DIM : TEXT, note, note == "Press again" ? ACCENT : SDL_Color{0, 0, 0, 0});
		}
		if (mstUninstallArmed) ui.drawFooterText("Press A again to uninstall", ACCENT);
		else if (mstModal == MSM_REMOVE) ui.drawHints({{"A", "Choose"}, {"LEFT/RIGHT", "Move"}, {"B", "Cancel"}});
		else ui.drawHints({{"A", "Choose"}, {"B", "Close"}});
	}

	// ---- Available's menu: Refresh, Languages ----

	namespace
	{
		std::string langName(const std::string& code)
		{
			static const std::pair<const char*, const char*> NAMES[] = {{"all", "Multi-language"}, {"en", "English"},
				{"id", "Indonesian"}, {"ja", "Japanese"}, {"ko", "Korean"}, {"zh", "Chinese"}, {"zh-Hans", "Chinese (simplified)"},
				{"zh-Hant", "Chinese (traditional)"}, {"es", "Spanish"}, {"es-419", "Spanish (Latin America)"}, {"pt", "Portuguese"},
				{"pt-BR", "Portuguese (Brazil)"}, {"fr", "French"}, {"de", "German"}, {"it", "Italian"}, {"ru", "Russian"},
				{"ar", "Arabic"}, {"tr", "Turkish"}, {"vi", "Vietnamese"}, {"th", "Thai"}, {"pl", "Polish"}, {"uk", "Ukrainian"},
				{"fil", "Filipino"}, {"ms", "Malay"}, {"hi", "Hindi"}, {"bn", "Bengali"}, {"fa", "Persian"}, {"he", "Hebrew"},
				{"cs", "Czech"}, {"bg", "Bulgarian"}, {"ca", "Catalan"}, {"da", "Danish"}, {"fi", "Finnish"}, {"no", "Norwegian"},
				{"my", "Burmese"}, {"km", "Khmer"}, {"lo", "Lao"}, {"mn", "Mongolian"}, {"ta", "Tamil"}, {"te", "Telugu"}, {"ur", "Urdu"},
				{"sr", "Serbian"}, {"hr", "Croatian"}, {"sk", "Slovak"}, {"lt", "Lithuanian"}, {"et", "Estonian"}, {"ka", "Georgian"}, {"hu", "Hungarian"}, {"ro", "Romanian"}, {"nl", "Dutch"}, {"sv", "Swedish"}, {"el", "Greek"}};
			for (auto& n : NAMES)
				if (code == n.first) return std::string(n.second) + " \xC2\xB7 " + code;
			return code;
		}

		std::vector<std::string> splitLangs(const std::string& s)
		{
			std::vector<std::string> out;
			size_t a = 0;
			while (a <= s.size())
			{
				size_t b = s.find(',', a);
				if (b == std::string::npos) b = s.size();
				std::string one = s.substr(a, b - a);
				if (!one.empty()) out.push_back(one);
				a = b + 1;
			}
			return out;
		}
	}

	void Ui::Manga::drawAdultChip(int x, int cy)
	{
		// a source marked adult (shown only with Adult sources on): said so, plainly
		int w = (int)std::lround(ui.textWidth("18+", 0) / sx) + 14;
		ui.fillRound({x, cy - 9, w, 18}, DANGER, R_CHIP);
		ui.drawTextMid("18+", 0, TEXT, x + w / 2, cy, w - 6, 2);
	}

	bool Ui::Manga::mangaLangShown(const Player& p, const std::string& lang) const
	{
		// (with a choice made it is the whole rule: a multi-language source is "all", a choice of its own)
		if (p.settings.mangaLangs.empty() || lang.empty()) return true;
		std::string l0 = lang == "multi" ? "all" : lang;
		for (const auto& l : splitLangs(p.settings.mangaLangs))
			if (l == l0) return true;
		return false;
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
	}

	Ui::Rect Ui::Manga::mangaAvBox() const
	{
		int rows = mstAvMenu == 1 ? 3 : std::min(MST_AV_SHOWN, (int)mstAvLangs.size() + 1);
		int h = 70 + rows * 44;
		return {112, (436 - h) / 2, 416, h};
	}

	Ui::Rect Ui::Manga::mangaAvRow(int i) const
	{
		Rect b = mangaAvBox();
		return {b.x + 16, b.y + 56 + i * 44, b.w - 32, 40};
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
		int n = mstAvMenu == 1 ? 3 : (int)mstAvLangs.size() + 1;
		switch (b)
		{
		case Button::Up: mstAvCursor = (mstAvCursor + n - 1) % n; break;
		case Button::Down: mstAvCursor = (mstAvCursor + 1) % n; break;
		case Button::A: mangaAvMenuAct(p); break;
		case Button::B:
			// the languages go back to the menu; the menu closes
			if (mstAvMenu == 2) { mstAvMenu = 1; mstAvCursor = 1; }
			else mstAvMenu = 0;
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
		if (!mangaAvBox().hit(x, y)) { mstAvMenu = 0; mstLocal++; return false; }
		int shown = mstAvMenu == 1 ? 3 : std::min(MST_AV_SHOWN, (int)mstAvLangs.size() + 1);
		for (int i = 0; i < shown; i++)
			if (mangaAvRow(i).hit(x, y))
			{
				mstAvCursor = (mstAvMenu == 2 ? mstAvScroll : 0) + i;
				mangaAvMenuAct(p);
				return false;
			}
		return false;
	}

	void Ui::Manga::drawMangaAvMenu(const Player& p)
	{
		Rect b = mangaAvBox();
		ui.drawDialog(b, mstAvMenu == 1 ? "Filter" : "Languages");
		if (mstAvMenu == 1)
		{
			const char* LABELS[3] = {"Refresh", "Languages", "Installed only"};
			for (int i = 0; i < 3; i++)
			{
				Rect r = mangaAvRow(i);
				bool at = i == mstAvCursor;
				if (at) ui.drawFocus(r);
				ui.drawTextMid(LABELS[i], 1, at ? ACCENT : TEXT, r.x + 12, r.y + r.h / 2, 220);
				if (i == 1) ui.drawTextMid(mangaLangsSummary(p), 0, at ? DIM_ON : DIM, r.x + r.w - 12, r.y + r.h / 2, 160, 1);
				if (i == 2) ui.drawTextMid(mstInstalledOnly ? "On" : "Off", 0, mstInstalledOnly ? ACCENT : at ? DIM_ON : DIM, r.x + r.w - 12, r.y + r.h / 2, 0, 1);
			}
			ui.drawHints({{"A", "Choose"}, {"B", "Close"}});
			return;
		}
		auto chosen = splitLangs(p.settings.mangaLangs);
		int n = (int)mstAvLangs.size() + 1;
		for (int i = 0; i < MST_AV_SHOWN && mstAvScroll + i < n; i++)
		{
			int k = mstAvScroll + i;
			Rect r = mangaAvRow(i);
			bool at = k == mstAvCursor;
			if (at) ui.drawFocus(r);
			bool on = k == 0 ? chosen.empty()
				: std::find(chosen.begin(), chosen.end(), mstAvLangs[k - 1]) != chosen.end();
			ui.drawTextMid(k == 0 ? "All languages" : langName(mstAvLangs[k - 1]), 1, at ? ACCENT : TEXT, r.x + 12, r.y + r.h / 2, 300);
			if (on) ui.drawGlyphMid(G_CHECK, r.x + r.w - 20, r.y + r.h / 2, at ? ACCENT : TEXT);
		}
		if (n > MST_AV_SHOWN) ui.drawScrollBar(mangaAvRow(0).y, MST_AV_SHOWN * 44 - 4, mstAvScroll, MST_AV_SHOWN, n);
		ui.drawHints({{"A", "Tick / untick"}, {"B", "Done"}});
	}
}
