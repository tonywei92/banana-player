// Java extensions in the reader's pages (docs/design/manga.md section 2): a Tachiyomi / Mihon repository's
// extensions beside the Lua ones, and its sources as Browse rows "java:<source id>". Every request is a GraphQL
// body from suwayomi.* handed to JavaExtHost's channel; the host's answers come back by serial and are matched
// here. Nothing in this file starts a thread or opens a socket: the host and MangaNet own those.
#include "ui.h"
#include "manganet.h"
#include <functional>
#include "ui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace bp
{
	namespace
	{
		// what A asks again after an error (ui_manga_browse.cpp's own constants aren't in this file's scope)
		constexpr int FAIL_NONE = 0, FAIL_LIST = 1, FAIL_SERIES = 2, FAIL_PAGES = 3;
		const char* const JAVA_MISSING =
			"Extension support isn\xE2\x80\x99t installed: Manga settings \xE2\x80\xBA Extension support";

		std::vector<const SuwayomiExt*> javaInstalledExtensions(const MangaJava& mgr)
		{
			std::vector<const SuwayomiExt*> out;
			for (const SuwayomiExt& e : mgr.extensions())
				if (e.isInstalled) out.push_back(&e);
			std::sort(out.begin(), out.end(), [](const SuwayomiExt* a, const SuwayomiExt* b) {
				if (a->name != b->name) return a->name < b->name;
				return a->lang < b->lang;
			});
			return out;
		}

		std::vector<const SuwayomiExt*> javaAvailableExtensions(const MangaJava& mgr, bool adult, const std::function<bool(const std::string&)>& langOk = nullptr)
		{
			std::vector<const SuwayomiExt*> out;
			// the installed ones too, on top: those with an update first, then the rest installed, then what isn't
			for (const SuwayomiExt& e : mgr.extensions())
				if (e.isInstalled || (!e.isObsolete && (adult || !e.isNsfw) && (!langOk || langOk(e.lang)))) out.push_back(&e);
			auto rank = [](const SuwayomiExt* e) { return e->isInstalled ? (e->hasUpdate ? 0 : 1) : 2; };
			std::sort(out.begin(), out.end(), [&](const SuwayomiExt* a, const SuwayomiExt* b) {
				if (rank(a) != rank(b)) return rank(a) < rank(b);
				if (a->name != b->name) return a->name < b->name;
				return a->lang < b->lang;
			});
			return out;
		}

		MangaItem javaItem(const SuwayomiManga& m)
		{
			MangaItem it;
			it.key = std::to_string(m.id);
			it.title = m.title;
			it.cover = m.thumbnailUrl;
			return it;
		}

		MangaChapter javaChapter(const SuwayomiChapter& c)
		{
			MangaChapter out;
			out.key = std::to_string(c.id);
			out.json = out.key;	// the chapter's Suwayomi id, what fetchChapterPages takes
			out.name = c.name;
			out.group = c.scanlator;
			out.number = c.chapterNumber > 0 ? c.chapterNumber : 0;
			out.date = c.uploadDate > 0 ? c.uploadDate / 1000 : 0;
			return out;
		}

		std::string javaExtVersion(const SuwayomiExt& e)
		{
			return e.versionName.empty() ? "version unknown" : e.versionName;
		}
	}

	uint64_t Ui::mangaJavaManagerSerial() const
	{
		return mangaNet && mangaNet->started() ? mangaNet->javaSources().serial() : 0;
	}

	bool Ui::mangaJavaInstalled() const
	{
		if (!mangaNet) return false;
		JavaExtHost& h = mangaNet->javaExt();
		switch (h.state())
		{
		case JavaExtHost::State::Installed:
		case JavaExtHost::State::Starting:
		case JavaExtHost::State::Running: return true;
		case JavaExtHost::State::Failed: return h.installedBytes() > 0;
		default: return false;
		}
	}

	void Ui::mangaJavaNeedHost()
	{
		if (mangaNet && mangaJavaInstalled()) mangaNet->javaExt().want();
	}

	std::string Ui::mangaJavaStatusLine() const
	{
		if (!mangaNet) return "";
		JavaExtHost& h = mangaNet->javaExt();
		switch (h.state())
		{
		case JavaExtHost::State::Starting: return "Loading manga engine\xE2\x80\xA6 about 25 s";
		case JavaExtHost::State::Downloading: return "Extension support is downloading";
		case JavaExtHost::State::Failed:
			return h.error().empty() ? "The manga engine didn\xE2\x80\x99t start" : "Manga engine: " + h.error();
		default: return "";
		}
	}

	const MangaJava::Source* Ui::mangaJavaSourceFor(const std::string& fullId) const
	{
		if (!mangaNet || !mangaNet->started()) return nullptr;
		return mangaNet->javaSources().source(fullId);
	}

	// ---- Sources: adding a store, its extensions, and their sheets ----

	void Ui::mangaJavaAddRepo(const std::string& url, Player& p)
	{
		if (!mangaNet || !mangaNet->started()) { p.osd("Repositories need a state folder", 2500); return; }
		if (!mangaJavaInstalled())
		{
			p.osd(JAVA_MISSING, 3500);
			return;
		}
		mangaJavaNeedHost();
		std::string error;
		if (!mangaNet->javaSources().addRepo(url, error))
		{
			p.osd("Repository not added: " + error, 3000);
			return;
		}
		mstJavaLocal++;
		p.osd("Adding repository\xE2\x80\xA6", 1500);
	}

	void Ui::mangaJavaRefresh()
	{
		if (!mangaNet || !mangaNet->started() || !mangaJavaInstalled()) return;
		mangaJavaNeedHost();
		mangaNet->javaSources().refreshExtensions();
		mangaJavaExtAt = SDL_GetTicks();
		mstJavaLocal++;
	}

	int Ui::mangaJavaInstalledRows(const Player& p) const
	{
		(void)p;
		if (!mangaNet || !mangaNet->started()) return 0;
		return (int)javaInstalledExtensions(mangaNet->javaSources()).size();
	}

	int Ui::mangaJavaAvailableRows(const Player& p) const
	{
		if (!mangaNet || !mangaNet->started()) return 0;
		return (int)javaAvailableExtensions(mangaNet->javaSources(), p.settings.mangaAdult, [&](const std::string& l) { return !mstInstalledOnly && mangaLangShown(p, l); }).size();
	}

	int Ui::mangaJavaRepoRows() const
	{
		if (!mangaNet || !mangaNet->started()) return 0;
		return (int)mangaNet->javaSources().repos().size();
	}

	void Ui::mangaJavaInstalledAct(int local, Player& p)
	{
		(void)p;
		if (!mangaNet || !mangaNet->started()) return;
		auto list = javaInstalledExtensions(mangaNet->javaSources());
		if (local < 0 || local >= (int)list.size()) return;
		mstJavaModal = MJM_DETAILS;
		mstJavaModalCursor = 0;
		mstJavaModalId = list[local]->pkgName;
		mstJavaUninstallArmed = false;
		mstJavaLocal++;
	}

	void Ui::mangaJavaAvailableAct(int local, Player& p)
	{
		if (!mangaNet || !mangaNet->started()) return;
		auto list = javaAvailableExtensions(mangaNet->javaSources(), p.settings.mangaAdult, [&](const std::string& l) { return !mstInstalledOnly && mangaLangShown(p, l); });
		if (local < 0 || local >= (int)list.size()) return;
		const std::string pkg = list[local]->pkgName;
		std::string error;
		// installed already: A updates it when there's a newer one (the row's Update chip), else says so
		if (list[local]->isInstalled)
		{
			if (mstJavaBusy.count(pkg)) return;
			// up to date: its details (Uninstall there), as the Installed list used to open
			if (!list[local]->hasUpdate)
			{
				mstJavaModal = MJM_DETAILS;
				mstJavaModalCursor = 0;
				mstJavaModalId = pkg;
				mstJavaUninstallArmed = false;
				mstJavaLocal++;
				return;
			}
			if (!mangaNet->javaSources().update(pkg, error))
			{
				p.osd(error.empty() ? "The extension couldn't be updated" : error, 3000);
				return;
			}
			mstJavaBusy[pkg] = "Updating\xE2\x80\xA6";
			mstJavaBusyAt[pkg] = SDL_GetTicks();
			mstJavaLocal++;
			return;
		}
		if (!mangaNet->javaSources().install(pkg, error))
		{
			p.osd(error.empty() ? "The extension couldn't be installed" : error, 3000);
			return;
		}
		mstJavaBusy[pkg] = "Installing\xE2\x80\xA6";
		mstJavaBusyAt[pkg] = SDL_GetTicks();
		mstJavaLocal++;
	}

	void Ui::mangaJavaRepoAct(int local, Player& p)
	{
		(void)p;
		if (!mangaNet || !mangaNet->started()) return;
		const auto& repos = mangaNet->javaSources().repos();
		if (local < 0 || local >= (int)repos.size()) return;
		mstJavaModal = MJM_REPO;
		mstJavaModalCursor = 0;
		mstJavaModalId = repos[local].url;
		mstJavaUninstallArmed = false;
		mstJavaLocal++;
	}

	void Ui::mangaJavaPoll(const Player& p)
	{
		if (!mangaNet || !mangaNet->started()) return;
		MangaJava& mgr = mangaNet->javaSources();
		// A Java page that is open, or a Java chapter in the download queue, is Java in use: the idle stop must
		// not take the server away from page URLs already in the reader's hands.
		bool inUse = false;
		{
			std::string full, key;
			if (page == Page::MangaRead && isSourceId(mangaReadSeriesId))
			{
				splitSourceId(mangaReadSeriesId, full, key);
				if (MangaJava::isSourceId(full)) inUse = true;
			}
			else if (page == Page::MangaSeries && isSourceId(mangaSeriesId))
			{
				splitSourceId(mangaSeriesId, full, key);
				if (MangaJava::isSourceId(full)) inUse = true;
			}
		}
		if (!inUse && page == Page::Manga && mangaView == 1 && MangaJava::isSourceId(mangaSrcId)) inUse = true;
		if (!inUse)
		{
			Uint32 now = SDL_GetTicks();
			if (!mangaJavaDlAt || now - mangaJavaDlAt >= 2000)
			{
				mangaJavaDlAt = now;
				for (const DlItem& it : mangaNet->queue().items())
					if (MangaJava::isSourceId(it.source) &&
						(it.state == DlItem::Waiting || it.state == DlItem::Running || it.state == DlItem::Paused))
					{ inUse = true; break; }
			}
		}
		if (inUse)
		{
			Uint32 now = SDL_GetTicks();
			JavaExtHost::State st = mangaNet->javaExt().state();
			bool up = st == JavaExtHost::State::Running || st == JavaExtHost::State::Starting;
			if (!up || !mangaJavaKeepAt || now - mangaJavaKeepAt >= 30000)
			{
				mangaJavaKeepAt = now;
				mangaJavaNeedHost();
			}
		}
		auto events = mgr.takeEvents();
		if (!events.empty())
		{
			for (const MangaJava::Event& e : events)
			{
				mstJavaBusy.erase(e.pkg);
				mstJavaBusyAt.erase(e.pkg);
				switch (e.kind)
				{
				case MangaJava::Event::RepoAdded:
					mangaNotes.push_back("Added " + (e.message.empty() ? e.url : e.message));
					mangaJavaRefresh();	// its extensions can show under Available now
					break;
				case MangaJava::Event::RepoFailed:
					mangaNotes.push_back(e.message.empty() ? "The repository wasn't added" : e.message);
					break;
				case MangaJava::Event::ExtensionsLoaded: break;	// the lists redraw on the manager's serial
				case MangaJava::Event::ExtensionsFailed:
					mangaNotes.push_back(e.message.empty() ? "The extension list didn't load" : e.message);
					break;
				case MangaJava::Event::Installed:
					mangaNotes.push_back("Installed " + (e.message.empty() ? e.pkg : e.message));
					break;
				case MangaJava::Event::Updated:
					mangaNotes.push_back("Updated " + (e.message.empty() ? e.pkg : e.message));
					break;
				case MangaJava::Event::Uninstalled:
					mangaNotes.push_back("Removed " + (e.message.empty() ? e.pkg : e.message));
					break;
				case MangaJava::Event::ActionFailed:
					mangaNotes.push_back(e.message.empty() ? "The extension didn't answer" : e.message);
					break;
				}
			}
			mgr.save();
			mstJavaLocal++;
		}
		// the answer that never came must not leave a row saying Installing… for good
		for (auto it = mstJavaBusyAt.begin(); it != mstJavaBusyAt.end();)
		{
			if (SDL_GetTicks() - it->second > 180000)
			{
				mstJavaBusy.erase(it->first);
				it = mstJavaBusyAt.erase(it);
				mstJavaLocal++;
			}
			else ++it;
		}
		if (mstJavaUninstallArmed && SDL_GetTicks() - mstJavaUninstallAt >= QUIT_WINDOW_MS)
		{
			mstJavaUninstallArmed = false;
			mstJavaLocal++;
		}
		// A Java repository's extensions are asked once the host is answering (and again after Y), not every tick
		if (mangaJavaInstalled() && mgr.hasRepos() && !mgr.extensionsKnown() &&
			mangaNet->javaExt().state() == JavaExtHost::State::Running)
		{
			Uint32 now = SDL_GetTicks();
			if (!mangaJavaExtAt || now - mangaJavaExtAt >= 60000)
			{
				mangaJavaExtAt = now;
				mgr.refreshExtensions();
			}
		}
		for (JavaExtHost::GraphQlResult& r : mangaNet->takeJava())
		{
			if (mangaJavaFilterAnswer(r)) continue;
			if (mangaJavaListAnswer(r)) continue;
			if (mangaJavaSeriesAnswer(r)) continue;
			if (mangaJavaPagesAnswer(r, p)) continue;
			if (mangaJavaDlAnswer(r)) continue;
		}
	}

	void Ui::mangaVideoGuard(Player& p)
	{
		// The handheld's 1 GB can't hold a video and the manga engine (its server takes about 400 MB). In the manga
		// reader a video is stopped (as Stop, everywhere, with the notice); outside it a video wins and the engine
		// is stopped. Music and photos go on either way.
		// entering the manga reader starts the engine (if installed): it is ready by the time a source is opened
		if (mangaNet && mangaNet->started() && mangaJavaInstalled() &&
			(page == Page::Manga || page == Page::MangaSeries || page == Page::MangaRead || page == Page::MangaSettings ||
				page == Page::MangaSources))
		{
			auto st = mangaNet->javaExt().state();
			if (st == JavaExtHost::State::Installed) mangaNet->javaExt().want();
		}
		if (p.playing() && !p.playingAudio() && !p.playingPhoto() && !p.loading())
		{
			bool inManga = page == Page::Manga || page == Page::MangaSeries || page == Page::MangaRead ||
				page == Page::MangaSettings || page == Page::MangaSources;
			auto st = mangaNet && mangaNet->started() ? mangaNet->javaExt().state() : JavaExtHost::State::NotInstalled;
			// outside the manga reader a video wins: the engine (loading or loaded) is stopped, and entering the
			// reader again loads it again, the same way every time
			// (asked once: it goes on saying Running until the process has gone, a moment)
			if (!inManga && (st == JavaExtHost::State::Starting || st == JavaExtHost::State::Running))
			{
				if (!engineStopAt || SDL_GetTicks() - engineStopAt > 5000)
				{
					engineStopAt = SDL_GetTicks();
					mangaNet->javaExt().stop();
					fprintf(stderr, "manga: the engine stopped (a video plays)\n");
				}
			}
			else if (inManga)
			{
				stopPressed = true;
				videoBlockedAt = SDL_GetTicks();
				fprintf(stderr, "manga: a video stopped (the manga reader)\n");
			}
		}
	}

	std::string Ui::extNoticeText() const
	{
		if (videoBlockedAt && SDL_GetTicks() - videoBlockedAt < 5000)
			return "Video stopped: no video in the manga reader";
		if (!mangaNet || !mangaNet->started()) return "";
		int st = (int)mangaNet->javaExt().state();
		Uint32 now = SDL_GetTicks();
		if (st != extNoticeState)
		{
			extNoticeState = st;
			extNoticeAt = now;
		}
		Uint32 secs = (now - extNoticeAt) / 1000;
		switch ((JavaExtHost::State)st)
		{
		case JavaExtHost::State::Starting: return "Loading manga engine\xE2\x80\xA6 " + std::to_string(secs) + " s";
		case JavaExtHost::State::Running: return secs < 2 ? "Manga engine ready" : "";
		case JavaExtHost::State::Failed: return secs < 4 ? "Manga engine didn\xE2\x80\x99t start" : "";
		default: return "";
		}
	}

	void Ui::drawExtNotice()
	{
		std::string t = extNoticeText();
		if (t.empty()) return;
		bool failed = extNoticeState == (int)JavaExtHost::State::Failed ||
			(videoBlockedAt && SDL_GetTicks() - videoBlockedAt < 5000);
		bool ready = extNoticeState == (int)JavaExtHost::State::Running;
		int tw = (int)std::lround(textWidth(t, 0) / sx);
		Rect r{W / 2 - (tw + 40) / 2, 8, tw + 40, 30};
		drawBox(r, SURFACE, R_CHIP, failed ? DANGER : ready ? ACCENT : DIM);
		fillRound({r.x + 12, r.y + 11, 8, 8}, failed ? DANGER : ACCENT, 4);
		drawTextMid(t, 0, failed ? DANGER : TEXT, r.x + 28, r.y + r.h / 2, tw + 4);
	}

	void Ui::mangaNotesTick(Player& p)
	{
		// answers come back where only a const Player is at hand (a draw), so the note waits here for a frame
		if (mangaNotes.empty()) return;
		p.osd(mangaNotes.front(), 2600);
		mangaNotes.erase(mangaNotes.begin());
	}

	// ---- drawing the Java rows ----

	void Ui::drawMangaJavaRow(int tab, int local, const Rect& r, bool at, bool focus, const Player& p)
	{
		if (focus && at) drawFocus(r);
		int mid = r.y + r.h / 2;
		SDL_Color ink = at ? ACCENT : TEXT, soft = at ? DIM_ON : DIM;
		if (tab == MST_REPOSITORIES)
		{
			if (!mangaNet || !mangaNet->started()) return;
			const auto& repos = mangaNet->javaSources().repos();
			if (local < 0 || local >= (int)repos.size()) return;
			const MangaJava::Repo& rp = repos[local];
			drawTextMid(rp.name.empty() ? rp.url : rp.name, 1, ink, 32, mid - 10, 450);
			drawTextMid(elideMiddle(rp.url, 0, 380), 0, soft, 32, mid + 10, 0);
			return;
		}
		const SuwayomiExt* e = nullptr;
		if (!mangaNet || !mangaNet->started()) return;
		if (tab == MST_INSTALLED)
		{
			auto list = javaInstalledExtensions(mangaNet->javaSources());
			if (local < 0 || local >= (int)list.size()) return;
			e = list[local];
		}
		else
		{
			auto list = javaAvailableExtensions(mangaNet->javaSources(), p.settings.mangaAdult, [&](const std::string& l) { return !mstInstalledOnly && mangaLangShown(p, l); });
			if (local < 0 || local >= (int)list.size()) return;
			e = list[local];
		}
		drawTextMid(e->name.empty() ? e->pkgName : e->name, 1, ink, 32, mid - 10, 420);
		int x = 32;
		if (!e->lang.empty())
		{
			int cw = (int)std::lround(textWidth(e->lang, 0) / sx) + 14;
			fillRound({x, mid + 3, cw, 18}, TRACK, R_CHIP);
			drawTextMid(e->lang, 0, at ? DIM_ON : TEXT, x + cw / 2, mid + 12, cw - 6, 2);
			x += cw + 8;
		}
		drawTextMid(javaExtVersion(*e), 0, soft, x, mid + 12, 200);
		if (e->isNsfw) drawAdultChip(x + std::min(200, (int)std::lround(textWidth(javaExtVersion(*e), 0) / sx)) + 8, mid + 12);
		auto busy = mstJavaBusy.find(e->pkgName);
		if (busy != mstJavaBusy.end())
			drawTextMid(busy->second, 0, ACCENT, 520, mid, 0, 1);
		else if (e->isInstalled && e->hasUpdate)
		{
			int w = (int)std::lround(textWidth("Update", 0) / sx) + 16;
			fillRound({608 - w, mid - 10, w, 20}, ACCENT, R_CHIP);
			drawTextMid("Update", 0, ON_ACCENT, 608 - w / 2, mid, w - 6, 2);
		}
		else if (tab == MST_AVAILABLE && e->isInstalled)
		{
			// (Available lists the installed on top: marked, so A's "up to date" isn't a surprise)
			int w = (int)std::lround(textWidth("Installed", 0) / sx) + 16;
			fillRound({608 - w, mid - 10, w, 20}, TRACK, R_CHIP);
			drawTextMid("Installed", 0, at ? DIM_ON : DIM, 608 - w / 2, mid, w - 6, 2);
		}
	}

	int Ui::mangaJavaModalRows() const
	{
		if (mstJavaModal == MJM_REPO) return 2;	// Refresh, Remove
		if (mstJavaModal != MJM_DETAILS || !mangaNet || !mangaNet->started()) return 0;
		const SuwayomiExt* e = mangaNet->javaSources().extension(mstJavaModalId);
		if (!e) return 0;
		return e->hasUpdate ? 2 : 1;	// Update (if any), then Uninstall
	}

	Ui::Rect Ui::mangaJavaModalBox() const
	{
		int rows = std::max(1, mangaJavaModalRows());
		int h = mstJavaModal == MJM_DETAILS ? 132 + rows * 50 : 100 + rows * 52;
		return {112, (436 - h) / 2, 416, h};
	}

	Ui::Rect Ui::mangaJavaModalRowRect(int i) const
	{
		Rect b = mangaJavaModalBox();
		int top = mstJavaModal == MJM_DETAILS ? b.y + 132 : b.y + 90;
		int step = mstJavaModal == MJM_DETAILS ? 50 : 52;
		return {b.x + 16, top + i * step, b.w - 32, step - 6};
	}

	bool Ui::mangaJavaModalKey(Button b, Player& p)
	{
		int n = mangaJavaModalRows();
		if (n <= 0) { mangaJavaModalClose(); return false; }
		if (mstJavaModalCursor >= n) mstJavaModalCursor = n - 1;
		switch (b)
		{
		case Button::Up: mstJavaModalCursor = (mstJavaModalCursor + n - 1) % n; break;
		case Button::Down: mstJavaModalCursor = (mstJavaModalCursor + 1) % n; break;
		case Button::A: mangaJavaModalAct(p); break;
		case Button::B: mangaJavaModalClose(); break;
		default: return false;
		}
		return false;
	}

	void Ui::mangaJavaModalAct(Player& p)
	{
		if (!mangaNet || !mangaNet->started()) { mangaJavaModalClose(); return; }
		MangaJava& mgr = mangaNet->javaSources();
		if (mstJavaModal == MJM_REPO)
		{
			if (mstJavaModalCursor == 0)
			{
				mangaJavaRefresh();
				p.osd("Checking extensions\xE2\x80\xA6", 1200);
				mangaJavaModalClose();
				return;
			}
			// Remove: the second press is the one that does it
			if (mstJavaUninstallArmed && SDL_GetTicks() - mstJavaUninstallAt < QUIT_WINDOW_MS)
			{
				std::string error;
				if (mgr.removeRepo(mstJavaModalId, error)) p.osd("Removed the repository", 2500);
				else p.osd(error.empty() ? "Couldn't remove the repository" : error, 3000);
				mgr.save();
				mangaJavaModalClose();
				return;
			}
			mstJavaUninstallArmed = true;
			mstJavaUninstallAt = SDL_GetTicks();
			mstJavaLocal++;
			return;
		}
		const SuwayomiExt* e = mgr.extension(mstJavaModalId);
		if (!e) { mangaJavaModalClose(); return; }
		bool updateRow = e->hasUpdate;
		int uninstallRow = updateRow ? 1 : 0;
		std::string error;
		if (mstJavaModalCursor == uninstallRow)
		{
			if (mstJavaUninstallArmed && SDL_GetTicks() - mstJavaUninstallAt < QUIT_WINDOW_MS)
			{
				if (!mgr.uninstall(e->pkgName, error))
				{
					p.osd(error.empty() ? "The extension couldn't be removed" : error, 3000);
					return;
				}
				mstJavaBusy[e->pkgName] = "Removing\xE2\x80\xA6";
				mstJavaBusyAt[e->pkgName] = SDL_GetTicks();
				mangaJavaModalClose();
				return;
			}
			mstJavaUninstallArmed = true;
			mstJavaUninstallAt = SDL_GetTicks();
			mstJavaLocal++;
			return;
		}
		if (!e->hasUpdate) { p.osd("Already up to date", 1500); return; }
		if (!mgr.update(e->pkgName, error))
		{
			p.osd(error.empty() ? "The extension couldn't be updated" : error, 3000);
			return;
		}
		mstJavaBusy[e->pkgName] = "Updating\xE2\x80\xA6";
		mstJavaBusyAt[e->pkgName] = SDL_GetTicks();
		mangaJavaModalClose();
	}

	void Ui::mangaJavaModalClose()
	{
		mstJavaModal = MJM_NONE;
		mstJavaModalCursor = 0;
		mstJavaModalId.clear();
		mstJavaUninstallArmed = false;
		mstJavaLocal++;
	}

	bool Ui::mangaJavaModalPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down || mstJavaModal == MJM_NONE) return false;
		if (!mangaJavaModalBox().hit(x, y)) { mangaJavaModalClose(); return false; }
		int n = mangaJavaModalRows();
		for (int i = 0; i < n; i++)
			if (mangaJavaModalRowRect(i).hit(x, y))
			{
				mstJavaModalCursor = i;
				mangaJavaModalAct(p);
				return false;
			}
		return false;
	}

	void Ui::drawMangaJavaModal(const Player& p)
	{
		(void)p;
		if (mstJavaModal == MJM_NONE) return;
		if (!mangaNet || !mangaNet->started()) { mstJavaModal = MJM_NONE; return; }
		MangaJava& mgr = mangaNet->javaSources();
		Rect box = mangaJavaModalBox();
		if (mstJavaModal == MJM_REPO)
		{
			const MangaJava::Repo* rp = nullptr;
			for (const MangaJava::Repo& r : mgr.repos())
				if (r.url == mstJavaModalId) { rp = &r; break; }
			if (!rp) { mstJavaModal = MJM_NONE; return; }
			drawDialog(box, rp->name.empty() ? "Java repository" : rp->name);
			drawTextMid(elideMiddle(rp->url, 0, box.w - 40), 0, DIM, box.x + box.w / 2, box.y + 62, 0, 2);
		}
		else
		{
			const SuwayomiExt* e = mgr.extension(mstJavaModalId);
			if (!e) { mstJavaModal = MJM_NONE; return; }
			drawDialog(box, e->name.empty() ? e->pkgName : e->name);
			auto line = [&](const char* label, const std::string& value, int y) {
				drawTextMid(label, 0, DIM, box.x + 24, y, 104);
				drawTextMid(value, 0, TEXT, box.x + 132, y, box.w - 156);
			};
			line("Package", e->pkgName, box.y + 58);
			line("Version", javaExtVersion(*e), box.y + 80);
			line("Language", e->lang.empty() ? "unknown" : e->lang, box.y + 102);
		}
		int n = mangaJavaModalRows();
		bool updateRow = false;
		if (mstJavaModal == MJM_DETAILS)
			if (const SuwayomiExt* e = mgr.extension(mstJavaModalId)) updateRow = e->hasUpdate;
		int uninstallRow = updateRow ? 1 : 0;
		for (int i = 0; i < n; i++)
		{
			bool at = i == mstJavaModalCursor;
			std::string label, note;
			const Glyph* g = nullptr;
			bool dim = false;
			if (mstJavaModal == MJM_DETAILS)
			{
				if (updateRow && i == 0)
				{
					label = "Update";
					g = &G_REFRESH;
					note = mstJavaBusy.count(mstJavaModalId) ? mstJavaBusy[mstJavaModalId] : "";
				}
				else
				{
					label = "Uninstall";
					g = &G_TRASH;
					if (mstJavaUninstallArmed) note = "Press again";
				}
			}
			else
			{
				label = i == 0 ? "Refresh" : "Remove";
				g = i == 0 ? &G_REFRESH : &G_TRASH;
				if (i == 1 && mstJavaUninstallArmed) note = "Press again";
			}
			drawDialogRow(mangaJavaModalRowRect(i), g, label, at, false, dim ? DIM : TEXT, note,
				note == "Press again" ? ACCENT : SDL_Color{0, 0, 0, 0});
		}
		if (mstJavaUninstallArmed) drawFooterText("Press A again to remove", ACCENT);
		else drawHints({{"A", "Choose"}, {"B", "Close"}});
	}

	// ---- a Java source's page (the hooks in ui_manga_browse.cpp call these) ----

	void Ui::mangaJavaSourceOpened(const MangaSrcRow& row, Player& p)
	{
		(void)p;
		if (const MangaJava::Source* s = mangaJavaSourceFor(row.id)) mangaSrcHasLatest = s->supportsLatest;
		mangaJavaNeedHost();
		if (mangaNet) mangaJavaBase = mangaNet->javaExt().baseUrl();
		mstJavaLocal++;
	}

	void Ui::mangaJavaCancelList()
	{
		if (mangaJavaListSerial && mangaNet && mangaNet->started())
			mangaNet->javaExt().cancelGraphQl(mangaJavaListSerial);
		mangaJavaListSerial = 0;
	}

	void Ui::mangaJavaCancel()
	{
		if (mangaNet && mangaNet->started())
		{
			JavaExtHost& h = mangaNet->javaExt();
			if (mangaJavaListSerial) h.cancelGraphQl(mangaJavaListSerial);
			if (mangaJavaSeriesSerial) h.cancelGraphQl(mangaJavaSeriesSerial);
			if (mangaJavaPagesSerial) h.cancelGraphQl(mangaJavaPagesSerial);
		}
		mangaJavaListSerial = mangaJavaSeriesSerial = mangaJavaPagesSerial = 0;
		mstJavaLocal++;
	}

	void Ui::mangaJavaListLoad(int page)
	{
		mangaJavaCancelList();
		mangaSrcError.clear();
		mangaSrcFailed = FAIL_LIST;
		if (!mangaNet || !mangaNet->started())
		{
			mangaSrcLoading = false;
			mangaSrcError = "Couldn\xE2\x80\x99t ask the source";
			return;
		}
		if (!mangaJavaInstalled())
		{
			mangaSrcLoading = false;
			mangaSrcError = "Extension support isn\xE2\x80\x99t installed";
			return;
		}
		std::string sid = MangaJava::suwayomiSourceId(mangaSrcId);
		if (sid.empty())
		{
			mangaSrcLoading = false;
			mangaSrcError = "This source isn\xE2\x80\x99t available";
			return;
		}
		mangaJavaNeedHost();
		mangaJavaBase = mangaNet->javaExt().baseUrl();
		std::string body;
		if (mangaSrcTab == SRC_LATEST) body = suwayomiFetchSourceMangaLatest(sid, page);
		else if (mangaSrcTab == SRC_SEARCH)
			body = suwayomiFetchSourceMangaSearch(sid, mangaSrcQuery, page, mangaJavaFiltersJson());
		else body = suwayomiFetchSourceMangaPopular(sid, page);
		mangaSrcPage = page;
		mangaSrcLoading = true;
		mangaJavaListSerial = mangaNet->javaExt().graphql(body);
		if (!mangaJavaListSerial)
		{
			mangaSrcLoading = false;
			mangaSrcError = "Couldn\xE2\x80\x99t ask the source";
		}
		mstJavaLocal++;
	}

	bool Ui::mangaJavaListAnswer(const JavaExtHost::GraphQlResult& r)
	{
		if (!mangaJavaListSerial || r.serial != mangaJavaListSerial) return false;
		mangaJavaListSerial = 0;
		mangaSrcLoading = false;
		if (!r.error.empty())
		{
			mangaSrcError = r.error;
			mangaSrcFailed = FAIL_LIST;
			return true;
		}
		std::string base = mangaJavaBase;
		if (base.empty() && mangaNet) base = mangaNet->javaExt().baseUrl();
		std::vector<SuwayomiManga> list;
		bool more = false;
		std::string error;
		if (!suwayomiParseSourceManga(base, r.body, list, more, error))
		{
			mangaSrcError = error.empty() ? "The source didn\xE2\x80\x99t answer" : error;
			mangaSrcFailed = FAIL_LIST;
			return true;
		}
		std::vector<MangaItem> items;
		items.reserve(list.size());
		for (const SuwayomiManga& m : list) items.push_back(javaItem(m));
		if (mangaSrcPage <= 1)
		{
			mangaSrcItems = std::move(items);
			mangaSrcCursor = mangaSrcScroll = 0;
		}
		else mangaSrcItems.insert(mangaSrcItems.end(), items.begin(), items.end());
		mangaSrcMore = more;
		mangaSrcError.clear();
		if (mangaSrcCursor >= (int)mangaSrcItems.size())
			mangaSrcCursor = std::max(0, (int)mangaSrcItems.size() - 1);
		mstJavaLocal++;
		return true;
	}

	void Ui::mangaJavaSeriesLoad(Player& p, bool refresh)
	{
		(void)p;
		// a refresh keeps the list on screen (the card's copy is the only thing asked again); a first load starts blank
		mangaSrcRefreshing = refresh;
		mangaSrcHadList = mangaSrcChaptersOn;
		mangaSrcFetchDetailsOk = mangaSrcFetchChaptersOk = false;
		mangaSrcError.clear();
		mangaSrcFailed = FAIL_SERIES;
		if (!refresh)
		{
			mangaSrcInfo = MangaDetails{};
			mangaSrcInfoOn = false;
			mangaSrcChapters.clear();
			mangaSrcChaptersOn = false;
		}
		if (mangaNet && mangaNet->started() && mangaJavaSeriesSerial)
			mangaNet->javaExt().cancelGraphQl(mangaJavaSeriesSerial);
		mangaJavaSeriesSerial = 0;
		if (!mangaNet || !mangaNet->started() || !mangaJavaInstalled())
		{
			mangaSourceSeriesFail("Couldn\xE2\x80\x99t ask the source");
			return;
		}
		int mangaId = std::atoi(mangaSrcKey.c_str());
		if (mangaId <= 0)
		{
			mangaSourceSeriesFail("This series isn\xE2\x80\x99t available");
			return;
		}
		mangaJavaNeedHost();
		mangaJavaBase = mangaNet->javaExt().baseUrl();
		mangaSrcLoading = true;
		mangaJavaSeriesSerial = mangaNet->javaExt().graphql(suwayomiFetchMangaAndChapters(mangaId));
		if (!mangaJavaSeriesSerial) mangaSourceSeriesFail("Couldn\xE2\x80\x99t ask the source");
		mstJavaLocal++;
	}

	bool Ui::mangaJavaSeriesAnswer(const JavaExtHost::GraphQlResult& r)
	{
		if (!mangaJavaSeriesSerial || r.serial != mangaJavaSeriesSerial) return false;
		mangaJavaSeriesSerial = 0;
		mangaSrcLoading = false;
		if (!r.error.empty())
		{
			// the cached list on the page stays; the toast at the end says the check didn't come
			if (!mangaSrcHadList && !mangaSrcChaptersOn)
			{
				mangaSrcError = r.error;
				mangaSrcFailed = FAIL_SERIES;
			}
			mangaSourceSeriesFetched();
			mstJavaLocal++;
			return true;
		}
		std::string base = mangaJavaBase;
		if (base.empty() && mangaNet) base = mangaNet->javaExt().baseUrl();
		SuwayomiMangaDetails manga;
		std::vector<SuwayomiChapter> chapters;
		std::string error;
		if (!suwayomiParseMangaAndChapters(base, r.body, manga, chapters, error))
		{
			if (!mangaSrcHadList && !mangaSrcChaptersOn)
			{
				mangaSrcError = error.empty() ? "The source didn\xE2\x80\x99t answer" : error;
				mangaSrcFailed = FAIL_SERIES;
			}
			mangaSourceSeriesFetched();
			mstJavaLocal++;
			return true;
		}
		mangaSrcInfo.title = manga.title;
		mangaSrcInfo.author = manga.author;
		mangaSrcInfo.artist = manga.artist;
		mangaSrcInfo.description = manga.description;
		mangaSrcInfo.status = manga.status;
		mangaSrcInfo.cover = manga.thumbnailUrl;
		mangaSrcInfo.genres = manga.genre;
		mangaSrcInfoOn = true;
		if (mangaSrcTitle.empty() && !manga.title.empty()) mangaSrcTitle = manga.title;
		mangaSrcChapters.clear();
		mangaSrcChapters.reserve(chapters.size());
		for (const SuwayomiChapter& c : chapters) mangaSrcChapters.push_back(javaChapter(c));
		mangaSrcChaptersOn = true;
		// the one query is the fetch's both halves
		mangaSrcFetchDetailsOk = mangaSrcFetchChaptersOk = true;
		mangaSrcError.clear();
		if (mserCursor >= (int)mangaSrcChapters.size()) mserCursor = (int)mangaSrcChapters.size() - 1;
		mangaSourceSeriesFetched();
		mstJavaLocal++;
		return true;
	}

	void Ui::mangaJavaPagesAsk(Player& p)
	{
		(void)p;
		mangaSrcError.clear();
		mangaSrcFailed = FAIL_PAGES;
		if (mangaNet && mangaNet->started() && mangaJavaPagesSerial)
			mangaNet->javaExt().cancelGraphQl(mangaJavaPagesSerial);
		mangaJavaPagesSerial = 0;
		if (mangaSrcPendAt < 0 || mangaSrcPendAt >= (int)mangaSrcPendChapters.size()) return;
		if (!mangaNet || !mangaNet->started() || !mangaJavaInstalled())
		{
			mangaSrcLoading = false;
			mangaSrcError = "Extension support isn\xE2\x80\x99t installed";
			return;
		}
		int chapterId = std::atoi(mangaSrcPendChapters[mangaSrcPendAt].key.c_str());
		if (chapterId <= 0)
		{
			mangaSrcLoading = false;
			mangaSrcError = "This chapter isn\xE2\x80\x99t available";
			return;
		}
		mangaJavaNeedHost();
		mangaJavaBase = mangaNet->javaExt().baseUrl();
		mangaSrcLoading = true;
		mangaJavaPagesSerial = mangaNet->javaExt().graphql(suwayomiFetchChapterPages(chapterId));
		mangaSrcPagesSerial = mangaSrcImageSerial = 0;
		if (!mangaJavaPagesSerial)
		{
			mangaSrcLoading = false;
			mangaSrcError = "Couldn\xE2\x80\x99t ask the source";
		}
		mstJavaLocal++;
	}

	bool Ui::mangaJavaPagesAnswer(const JavaExtHost::GraphQlResult& r, const Player& p)
	{
		if (!mangaJavaPagesSerial || r.serial != mangaJavaPagesSerial) return false;
		mangaJavaPagesSerial = 0;
		mangaSrcLoading = false;
		if (!r.error.empty())
		{
			mangaSrcError = r.error;
			mangaSrcFailed = FAIL_PAGES;
			return true;
		}
		std::string base = mangaJavaBase;
		if (base.empty() && mangaNet) base = mangaNet->javaExt().baseUrl();
		std::vector<std::string> urls;
		std::string error;
		if (!suwayomiParsePages(base, r.body, urls, error))
		{
			mangaSrcError = error.empty() ? "The chapter\xE2\x80\x99s pages didn\xE2\x80\x99t come" : error;
			mangaSrcFailed = FAIL_PAGES;
			return true;
		}
		if (urls.empty())
		{
			mangaSrcError = "This chapter has no pages";
			mangaSrcFailed = FAIL_PAGES;
			return true;
		}
		mangaSrcUrls = std::move(urls);
		mangaSrcHeaders.clear();
		mangaSrcPages.clear();
		mangaSourceChapterReady(p);
		mstJavaLocal++;
		return true;
	}

	void Ui::mangaJavaFiltersAsk()
	{
		if (mangaSrcId.empty() || !MangaJava::isSourceId(mangaSrcId)) return;
		MangaFilterSession& f = mangaFilters[mangaSrcId];
		if (f.known || f.loading || f.serial || f.javaSerial) return;
		if (!mangaNet || !mangaNet->started() || !mangaJavaInstalled()) return;
		mangaJavaNeedHost();
		f.javaSerial = mangaNet->javaExt().graphql(suwayomiFetchSourceFilters(MangaJava::suwayomiSourceId(mangaSrcId)));
		f.loading = f.javaSerial != 0;
		if (!f.javaSerial) f.error = "Couldn\xE2\x80\x99t ask the source";
		mangaFilterLocal++;
	}

	std::string Ui::mangaJavaFiltersJson() const
	{
		const MangaFilterSession* f = mangaFilterSession();
		if (!f || !f->known || f->defs.empty()) return "[]";
		return suwayomiFilterInputsJson(f->defs, f->values);
	}

	bool Ui::mangaJavaFilterAnswer(const JavaExtHost::GraphQlResult& r)
	{
		for (auto& kv : mangaFilters)
		{
			MangaFilterSession& f = kv.second;
			if (!f.javaSerial || f.javaSerial != r.serial) continue;
			f.javaSerial = 0;
			f.loading = false;
			if (!r.error.empty()) f.error = r.error;
			else
			{
				std::vector<MangaFilter> defs;
				std::string error;
				if (!suwayomiParseFilters(r.body, defs, error))
				{
					f.error = error;
					f.known = false;
				}
				else
				{
					f.error.clear();
					f.defs = std::move(defs);
					f.known = true;
					mangaFilterFit(f.defs, f.values);
					if (f.defs.empty()) f.open = false;
				}
			}
			mangaFilterLocal++;
			return true;
		}
		return false;
	}

	// ---- the Download sheet's page list ----

	uint64_t Ui::mangaJavaDlSubmit(const MangaDlAsk& a)
	{
		if (!mangaNet || !mangaNet->started() || !mangaJavaInstalled()) return 0;
		int chapterId = std::atoi(a.chapterJson.c_str());
		if (chapterId <= 0) return 0;
		mangaJavaNeedHost();
		mangaJavaBase = mangaNet->javaExt().baseUrl();
		return mangaNet->javaExt().graphql(suwayomiFetchChapterPages(chapterId));
	}

	bool Ui::mangaJavaDlAnswer(const JavaExtHost::GraphQlResult& r)
	{
		if (!mdlAskOn || !mdlPagesSerial || r.serial != mdlPagesSerial) return false;
		if (!MangaJava::isSourceId(mdlAsk.sourceId)) return false;
		mdlPagesSerial = 0;
		if (!r.error.empty())
		{
			mangaDlAskFinish(false, r.error);
			return true;
		}
		std::string base = mangaJavaBase;
		if (base.empty() && mangaNet) base = mangaNet->javaExt().baseUrl();
		std::vector<std::string> urls;
		std::string error;
		if (!suwayomiParsePages(base, r.body, urls, error))
		{
			mangaDlAskFinish(false, error.empty() ? "A chapter\xE2\x80\x99s pages didn\xE2\x80\x99t come" : error);
			return true;
		}
		if (urls.empty())
		{
			mangaDlAskFinish(false, "A chapter has no pages");
			return true;
		}
		mdlAskUrls = std::move(urls);
		mdlAskPages.clear();
		for (size_t i = 0; i < mdlAskUrls.size(); i++)
		{
			MangaPage p;
			p.url = mdlAskUrls[i];
			mdlAskPages.push_back(std::move(p));
		}
		mangaDlAskFinish(true, "");
		return true;
	}
}
