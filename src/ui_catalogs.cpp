// Books › Catalogs (ui_catalogs.h): the saved list, the form, a catalog's feeds, a book's page and its download.
// Everything is drawn inside lay::shell().body (Books' tabs over the list; a feed, a book and the form fill it) and
// the touches test the same Rects the drawing asked for.
#include "ui_catalogs.h"
#include "mangaremote.h"
#include "mediaindex.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_mediaart.h"
#include "artbackdrop.h"
#include <atomic>
#include <thread>
#include <sys/time.h>
#include "ui_smoothlist.h"
#include "theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include <memory>
#include <random>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		const char* GUTENBERG_URL = "https://www.gutenberg.org/ebooks.opds/";
		const int NAV_ROW_H = 52, BOOK_ROW_H = 60;
		const int64_t FEED_MAX = 8 << 20;	// a feed's answer (opds.cpp refuses more anyway)
		const int64_t COVER_MAX = 4 << 20;	// a cover picture
		const int64_t BOOK_MAX = 2000LL << 20;	// a book (a big comic is a few hundred MB)

		bool fileThere(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0;
		}

		long long fileBytes(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0 ? (long long)st.st_size : -1;
		}

		long long freeBytesAt(const std::string& dir)
		{
			struct statvfs v;
			if (statvfs(dir.c_str(), &v) != 0) return -1;
			return (long long)v.f_bavail * (long long)v.f_frsize;
		}

		std::string mb(long long bytes)
		{
			char b[32];
			if (bytes < 0) return "";
			if (bytes < 1024 * 1024) snprintf(b, sizeof b, "%lld KB", std::max(1LL, bytes / 1024));
			else snprintf(b, sizeof b, "%.1f MB", bytes / (1024.0 * 1024.0));
			return b;
		}

		std::string gb(long long bytes)
		{
			char b[32];
			if (bytes >= (1LL << 30)) snprintf(b, sizeof b, "%.0f GB", bytes / (double)(1LL << 30));
			else snprintf(b, sizeof b, "%.0f MB", bytes / (double)(1 << 20));
			return b;
		}

		std::string hostOf(const std::string& url)
		{
			size_t a = url.find("://");
			a = a == std::string::npos ? 0 : a + 3;
			size_t e = url.find_first_of("/?#", a);
			return url.substr(a, e == std::string::npos ? std::string::npos : e - a);
		}

		std::string lowerAscii(std::string s)
		{
			for (char& c : s) c = (char)std::tolower((unsigned char)c);
			return s;
		}

		std::string nameOfPath(const std::string& p) { return p.substr(p.rfind('/') + 1); }
		std::string dirOfPath(const std::string& p) { return p.substr(0, p.rfind('/')); }

		// the client's own word in a Digest answer: random hex
		std::string cnonce()
		{
			static std::mt19937_64 rng{std::random_device{}()};
			char b[17];
			snprintf(b, sizeof b, "%016llx", (unsigned long long)rng());
			return b;
		}

		std::string bodyText(const MangaFetch::Result& r) { return std::string(r.body.begin(), r.body.end()); }

		std::vector<std::string> challengesOf(const MangaFetch::Result& r)
		{
			std::vector<std::string> v;
			for (auto& h : r.headers)
				if (lowerAscii(h.first) == "www-authenticate") v.push_back(h.second);
			return v;
		}

		// the year out of a date ("1818", "2026-11-26T09:20:38+00:00")
		std::string yearOf(const std::string& d) { return d.size() >= 4 ? d.substr(0, 4) : d; }

		// a short reason out of a failed request, as a person reads it
		std::string reasonOf(const MangaFetch::Result& r)
		{
			if (r.status == 404) return "Not found (404)";
			if (r.status >= 500) return "The server had a problem (" + std::to_string(r.status) + ")";
			if (r.status >= 400) return "Refused (" + std::to_string(r.status) + ")";
			std::string e = lowerAscii(r.error);
			if (e.find("timed out") != std::string::npos || e.find("timeout") != std::string::npos) return "It didn't answer for 20 seconds";
			if (e.find("resolve") != std::string::npos) return "No such address on this network";
			if (e.find("connect") != std::string::npos) return "Nothing answers at that address";
			return r.error.empty() ? "No answer" : r.error;
		}
	}

	// ---- a small sheet that asks (Remove, a refused sign-in, a failed download, Save anyway) ----

	struct Ui::Books::Catalogs::Ask : Ui::Overlay
	{
		std::string title, text, safe, action;
		bool danger = false, failure = false;
		std::function<void(Player&)> act;	// the action key (the safe one only closes)
		int focus = 0, pressedKey = -1;
		Ask(Ui& u) : Overlay(u) {}
		const char* name() const override { return "catalogask"; }
		std::vector<std::string> lines() const { return ui.kit->sheetText(text); }
		void choose(int which, Player& p)
		{
			close();
			if (which == 1 && act) act(p);
		}
		bool key(Button b, Player& p) override
		{
			switch (b)
			{
			case Button::Left: case Button::Right: focus = 1 - focus; break;
			case Button::A: choose(focus, p); break;
			case Button::B: choose(0, p); break;
			default: break;
			}
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			lay::ConfirmSheet l = lay::confirmSheet((int)lines().size(), false);
			if (pt.kind == Pointer::Down)
			{
				pressedKey = -1;
				if (!l.sheet.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
				if (l.safe.hit(x, y)) pressedKey = 0;
				else if (l.action.hit(x, y)) pressedKey = 1;
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int at = pressedKey;
			pressedKey = -1;
			if (at == 0 && l.safe.hit(x, y)) choose(0, p);
			else if (at == 1 && l.action.hit(x, y)) choose(1, p);
			return false;
		}
		void draw(const Player&) override
		{
			auto ls = lines();
			Kit::ConfirmSpec c;
			c.title = title;
			c.text = text;
			c.safe = safe;
			c.action = action;
			c.danger = danger;
			c.titleDanger = failure;
			c.focus = focus;
			c.pressed = pressedKey;
			ui.kit->confirmSheet(lay::confirmSheet((int)ls.size(), false), c, ls);
		}
		std::string signature(const Player&) const override
		{
			return title + "|" + text + "|" + std::to_string(focus) + std::to_string(pressedKey);
		}
	};

	void Ui::Books::Catalogs::ask(const std::string& title, const std::string& text, const std::string& safe,
		const std::string& action, int focus, std::function<void(Player&)> act, bool danger, bool failure)
	{
		auto a = std::make_unique<Ask>(ui);
		a->title = title;
		a->text = text;
		a->safe = safe;
		a->action = action;
		a->focus = focus;
		a->act = std::move(act);
		a->danger = danger;
		a->failure = failure;
		ui.pushOverlay(std::move(a));
	}

	// ---- the saved catalogs ----

	void Ui::Books::Catalogs::load()
	{
		if (loaded || ui.stateDirPath.empty()) return;
		loaded = true;
		store.load(ui.stateDirPath, "catalogs");
		// a download cut off (a quit, a crash, a card pulled) leaves its hidden part file: it goes now, so nothing
		// half-saved stays on the card (each catalog's own folder under each Books folder)
		for (const std::string& books : bookFolders())
			for (const MangaServer& s : store.list())
			{
				std::string dir = books + "/" + catalogFolderName(s.name);
				DIR* d = opendir(dir.c_str());
				if (!d) continue;
				while (dirent* e = readdir(d))
				{
					std::string n = e->d_name;
					if (n.size() > 6 && n[0] == '.' && n.compare(n.size() - 5, 5, ".part") == 0)
					{
						fprintf(stderr, "catalogs: removed a part file left by a download that stopped: %s\n", n.c_str());
						std::remove((dir + "/" + n).c_str());
					}
				}
				closedir(d);
			}
	}

	CatalogGot& Ui::Books::Catalogs::got(const std::string& id)
	{
		auto it = gots.find(id);
		if (it != gots.end()) return it->second;
		CatalogGot& g = gots[id];
		if (!ui.stateDirPath.empty()) g.load(ui.stateDirPath + "/catalogs/" + id + ".got");
		return g;
	}

	void Ui::Books::Catalogs::addGutenberg(Player& p)
	{
		for (const MangaServer& s : store.list())
			if (s.url == GUTENBERG_URL) return;
		MangaServer s;
		s.kind = "opds";
		s.name = "Project Gutenberg";
		s.url = GUTENBERG_URL;
		if (store.add(s).empty()) p.osd("Couldn't save it", 2000);
		listCursor = 0;
	}

	void Ui::Books::Catalogs::removeCatalog(const std::string& id, Player& p)
	{
		const MangaServer* s = store.find(id);
		if (!s) return;
		std::string name = s->name;
		if (dl.active && dl.catalogId == id) cancelDownload("its catalog was removed");
		store.remove(id);
		logins.erase(id);
		// its list of what it gave the card goes too (the books themselves stay)
		gots.erase(id);
		if (!ui.stateDirPath.empty()) std::remove((ui.stateDirPath + "/catalogs/" + id + ".got").c_str());
		listCursor = std::clamp(listCursor, 0, (int)store.list().size());
		p.osd("Removed " + name + ". Its books stay on the Shelf", 2500);
	}

	// ---- the network ----

	uint64_t Ui::Books::Catalogs::send(const MangaServer& s, Pending pd)
	{
		if (!fetch) return 0;
		if (!fetchStarted)
		{
			fetch->start(fetchEvent);
			fetchStarted = true;
		}
		std::string pw = store.password(s.id);
		if (s.id.empty()) pw = fTestPassword;	// Test on a form not saved yet
		std::string url = remoteWithKey(s, pw, pd.url);
		MangaFetch::Headers h;
		h.emplace_back("Accept", pd.what == P_GET ? "*/*" : pd.what == P_COVER ? "image/*" : "application/atom+xml, application/xml;q=0.9, */*;q=0.5");
		// the login goes to the catalog's own origin only, and only once the catalog asked for it (a 401 told
		// which kind): a request that needs none never carries it
		auto login = logins.find(s.id);
		if (!s.user.empty() && login != logins.end() && sameOrigin(url, s.url))
		{
			Login& L = login->second;
			if (L.ch.scheme == "digest")
				h.emplace_back("Authorization", digestAuthorization(L.ch, s.user, pw, "GET", urlRequestTarget(url), cnonce(), ++L.nc));
			else h.emplace_back("Authorization", basicAuthorization(s.user, pw));
		}
		pd.sentAt = SDL_GetTicks();
		uint64_t id = pd.what == P_GET ? fetch->get(url, h, BOOK_MAX, dl.hidden)
			: fetch->get(url, h, pd.what == P_COVER ? COVER_MAX : FEED_MAX);
		if (id) pending[id] = pd;
		return id;
	}

	void Ui::Books::Catalogs::onFetched(Player& p)
	{
		if (!fetch) return;
		for (MangaFetch::Result& r : fetch->take())
		{
			auto it = pending.find(r.id);
			if (it == pending.end()) continue;
			Pending pd = it->second;
			pending.erase(it);
			answered(r, pd, p);
		}
	}

	void Ui::Books::Catalogs::answered(MangaFetch::Result& r, const Pending& pd, Player& p)
	{
		const MangaServer* saved = store.find(pd.catalogId);
		MangaServer s = saved ? *saved : pd.what == P_TEST ? fTestServer : MangaServer();
		if (s.url.empty()) return;	// removed while it came
		static const char* WHAT[] = {"feed", "next page", "search", "test", "download", "cover"};
		// the line that pins a handheld's failure: what, the status and the time (never the login)
		fprintf(stderr, "catalogs: %s %s -> %d in %u ms%s%s\n", WHAT[pd.what], pd.url.c_str(), r.status,
			(unsigned)(SDL_GetTicks() - pd.sentAt), r.error.empty() ? "" : ": ", r.error.c_str());
		// a sign-in asked for: answered once with the kind it named (a stale Digest nonce once more)
		if (r.status == 401 && !s.user.empty())
		{
			HttpChallenge ch;
			bool can = parseAuthChallenge(challengesOf(r), ch);
			if (can && (pd.tries == 0 || (ch.stale && pd.tries == 1)))
			{
				logins[s.id] = Login{ch, 0};
				Pending again = pd;
				again.tries++;
				uint64_t id = send(s, again);
				if ((pd.what == P_FEED || pd.what == P_SEARCHDESC) && pd.depth < (int)feeds.size()) feeds[pd.depth].req = id;
				else if (pd.what == P_MORE && pd.depth < (int)feeds.size()) feeds[pd.depth].moreReq = id;
				else if (pd.what == P_GET) dl.req = id;
				else if (pd.what == P_COVER) coverReq[pd.url] = id;
				else if (pd.what == P_TEST) fTestReq = id;
				return;
			}
		}
		switch (pd.what)
		{
		case P_FEED: case P_MORE: case P_SEARCHDESC: feedAnswered(r, pd, s); break;
		case P_TEST: testAnswered(r, pd, s, p); break;
		case P_GET: if (r.id == dl.req) getAnswered(r, s, p); break;
		case P_COVER: coverAnswered(r, pd); break;
		}
	}

	void Ui::Books::Catalogs::signInRefused(const MangaServer& s, bool askedForOne)
	{
		std::string id = s.id;
		ask(askedForOne ? "This catalog asks for a sign-in" : "The sign-in was refused",
			askedForOne ? s.name + " wants a user and a password. Type them in the catalog's form."
				: s.name + " didn't take the user and password saved for it.",
			"Close", "Edit sign-in", 1, [this, id](Player&) {
				const MangaServer* c = store.find(id);
				if (!c) return;
				openForm(c, 4);
				fAccount = true;
			}, false, true);
	}

	// ---- feeds ----

	bool Ui::Books::Catalogs::Feed::books() const
	{
		int n = 0;
		for (const OpdsEntry& e : feed.entries)
			if (e.isBook()) n++;
		return n > 0 && n * 2 >= (int)feed.entries.size();
	}

	void Ui::Books::Catalogs::openCatalogAt(const std::string& id, Player&)
	{
		const MangaServer* s = store.find(id);
		if (!s) return;
		openId = id;
		feeds.clear();
		bookOpen = false;
		openFeed(s->url, s->name, false);
	}

	void Ui::Books::Catalogs::openFeed(const std::string& url, const std::string& title, bool isSearch)
	{
		Feed f;
		f.url = url;
		f.title = title;
		f.search = isSearch;
		feeds.push_back(std::move(f));
		list.top();
		loadFeed((int)feeds.size() - 1);
	}

	void Ui::Books::Catalogs::loadFeed(int depth)
	{
		const MangaServer* s = openCatalog();
		if (!s || depth >= (int)feeds.size()) return;
		Feed& f = feeds[depth];
		f.loading = true;
		f.error = E_NONE;
		Pending pd;
		pd.what = P_FEED;
		pd.catalogId = s->id;
		pd.url = f.url;
		pd.depth = depth;
		f.req = send(*s, pd);
		if (!f.req)
		{
			f.loading = false;
			f.error = E_REACH;
			f.why = "No network in this build";
		}
	}

	void Ui::Books::Catalogs::loadMore(int depth)
	{
		const MangaServer* s = openCatalog();
		if (!s || depth >= (int)feeds.size()) return;
		Feed& f = feeds[depth];
		if (f.loadingMore || f.loading || f.feed.next.empty()) return;
		Pending pd;
		pd.what = P_MORE;
		pd.catalogId = s->id;
		pd.url = opdsResolve(f.url, f.feed.next);
		pd.depth = depth;
		f.moreReq = send(*s, pd);
		f.loadingMore = f.moreReq != 0;
	}

	void Ui::Books::Catalogs::feedAnswered(MangaFetch::Result& r, const Pending& pd, const MangaServer& s)
	{
		if (pd.depth >= (int)feeds.size() || openId != s.id) return;	// left before it came
		Feed& f = feeds[pd.depth];
		std::string pw = store.password(s.id);
		if (pd.what == P_SEARCHDESC)
		{
			if (r.id != f.req) return;
			// the OpenSearch description: its template, the words in it, then the results as this page's feed
			MangaServer anon = s;
			anon.user.clear();
			RemoteRequest rq = remoteOpdsSearch(anon, "", pd.url, remoteWithoutKey(s, pw, bodyText(r)), pd.words);
			if (!r.error.empty() || rq.url.empty())
			{
				f.loading = false;
				f.error = E_NOT_CATALOG;
				f.why = r.error.empty() ? "Its search can't be read" : reasonOf(r);
				return;
			}
			f.url = rq.url;
			loadFeed(pd.depth);
			return;
		}
		bool more = pd.what == P_MORE;
		if (more ? r.id != f.moreReq : r.id != f.req) return;
		if (more) f.loadingMore = false;
		else f.loading = false;
		if (r.status == 401)
		{
			if (!more)
			{
				f.error = E_SIGN_IN;
				f.why = s.user.empty() ? "It asks for a sign-in" : "The sign-in was refused";
			}
			signInRefused(s, s.user.empty());
			return;
		}
		OpdsFeed parsed;
		if (!r.error.empty() || !parseOpds(remoteWithoutKey(s, pw, bodyText(r)), parsed))
		{
			if (more) return;	// the page so far stays; the foot row offers nothing more
			bool answeredAtAll = r.error.empty() || r.status > 0;
			f.error = answeredAtAll && r.status < 400 ? E_NOT_CATALOG : E_REACH;
			f.why = f.error == E_NOT_CATALOG ? "It answers with something that is not a catalog" : reasonOf(r);
			return;
		}
		fprintf(stderr, "catalogs: %zu entries%s\n", parsed.entries.size(), parsed.next.empty() ? "" : ", more to come");
		if (more)
		{
			for (OpdsEntry& e : parsed.entries) f.feed.entries.push_back(std::move(e));
			f.feed.next = parsed.next;
			// a later page's links resolve against its own url, but servers write them root-relative: the first's
			// base serves (Calibre, Kavita, Komga, COPS all do)
			return;
		}
		// the top of a catalog lends its search to every page under it
		f.feed = std::move(parsed);
		f.cursor = std::clamp(f.cursor, 0, std::max(0, (int)f.feed.entries.size() - 1));
	}

	bool Ui::Books::Catalogs::canSearch() const
	{
		for (const Feed& f : feeds)
			if (!f.feed.search.empty()) return true;
		return false;
	}

	void Ui::Books::Catalogs::search(const std::string& words)
	{
		const MangaServer* s = openCatalog();
		if (!s || words.empty()) return;
		searchWords = words;
		// the nearest page's search link (the catalog's top names it; Calibre names it on every page)
		const Feed* from = nullptr;
		for (auto it = feeds.rbegin(); it != feeds.rend(); ++it)
			if (!it->feed.search.empty()) { from = &*it; break; }
		if (!from) return;
		std::string href = opdsResolve(from->url, from->feed.search);
		bool description = from->feed.searchType.find("opensearchdescription") != std::string::npos ||
			href.find("{searchTerms}") == std::string::npos;
		// a search from a search's results replaces them (B goes back to where Search was first pressed)
		if (!feeds.empty() && feeds.back().search) feeds.pop_back();
		std::string title = "\xE2\x80\x9C" + words + "\xE2\x80\x9D";
		if (!description)
		{
			openFeed(fillSearchTerms(href, words), title, true);
			return;
		}
		Feed f;
		f.url = href;
		f.title = title;
		f.search = true;
		feeds.push_back(std::move(f));
		list.top();
		Pending pd;
		pd.what = P_SEARCHDESC;
		pd.catalogId = s->id;
		pd.url = href;
		pd.depth = (int)feeds.size() - 1;
		pd.words = words;
		feeds.back().req = send(*s, pd);
	}

	void Ui::Books::Catalogs::feedBack()
	{
		if (!feeds.empty()) feeds.pop_back();
		list.top();
		if (feeds.empty()) openId.clear();
	}

	void Ui::Books::Catalogs::openEntry(Player& p)
	{
		if (feeds.empty()) return;
		Feed& f = feeds.back();
		if (f.cursor < 0 || f.cursor >= (int)f.feed.entries.size()) return;
		const OpdsEntry& e = f.feed.entries[f.cursor];
		if (e.isBook()) openBook(e, f.url);
		else if (!e.nav().empty()) openFeed(opdsResolve(f.url, e.nav()), e.title, false);
		else p.osd("It leads nowhere", 1500);
	}

	void Ui::Books::Catalogs::feedMore(Player& p)
	{
		if (feeds.empty()) return;
		ui.openMore([this](const Player&) {
			MoreSpec m;
			m.glyph = &G_WEB;
			const MangaServer* s = openCatalog();
			m.title = feeds.empty() ? "" : feeds.back().title;
			m.note = s ? s->name : "";
			m.rows.push_back({&G_REFRESH, "Refresh", "", "", [this](Player&) {
				if (!feeds.empty()) loadFeed((int)feeds.size() - 1);
				return false;
			}});
			if (!feeds.empty() && feeds.back().books())
				m.rows.push_back({&G_BOOK, "View", listView ? "List" : "Covers", "", [this](Player&) {
					listView = !listView;
					list.top();
					return false;
				}});
			if (canSearch())
				m.rows.push_back({&G_SEARCH, "Search this catalog", "", "Y", [this](Player&) {
					ui.openKeyboard(CAT_SEARCH_FIELD, searchWords);
					return false;
				}});
			m.rows.push_back({&G_PENCIL, "Edit catalog\xE2\x80\xA6", "", "", [this](Player&) {
				if (const MangaServer* c = openCatalog()) openForm(c);
				return false;
			}});
			return m;
		});
		(void)p;
	}

	// ---- a book's page ----

	std::string Ui::Books::Catalogs::onCard(const std::string& entryId) const
	{
		if (openId.empty() || entryId.empty()) return "";
		return const_cast<Catalogs*>(this)->got(openId).path(entryId);
	}

	void Ui::Books::Catalogs::openBook(const OpdsEntry& e, const std::string& base)
	{
		book = e;
		bookBase = base;
		offers = e.bookOffers();
		fmt = 0;
		bookOpen = true;
	}

	void Ui::Books::Catalogs::bookMore(Player& p)
	{
		std::string have = onCard(book.id);
		ui.openMore([this, have](const Player&) {
			MoreSpec m;
			m.glyph = &G_BOOK;
			m.title = book.title;
			m.note = book.author;
			if (!offers.empty() && !dl.active)
				m.rows.push_back({&G_DOWNLOAD, have.empty() ? "Download" : "Download again", "", "", [this](Player& pl) {
					if (fmt < (int)offers.size()) startDownload(book, offers[fmt], pl);
					return false;
				}});
			if (!have.empty())
			{
				m.rows.push_back({&G_FOLDER, "Show in Files", "", "", [this, have](Player& pl) {
					ui.showInFiles(have, pl);
					return false;
				}});
				m.rows.push_back({&G_INFO, "Info", "", "", [this, have](Player&) {
					ui.openFileInfo(have, "");
					return false;
				}});
				if (!(dl.active && dl.entryId == book.id))
				{
					MoreItem del{&G_TRASH, "Delete from the card\xE2\x80\xA6", "", "", [this](Player&) {
						std::vector<std::string> files = got(openId).paths(book.id);
						std::string names;
						for (const std::string& f : files) names += (names.empty() ? "" : ", ") + nameOfPath(f);
						ask("Delete from the card?", names + (files.size() > 1 ? " are" : " is") +
							" removed from the card. The catalog still has the book.", "Cancel", "Delete", 0,
							[this](Player& pl) { deleteFromCard(pl); }, true);
						return false;
					}};
					del.danger = true;
					m.rows.push_back(del);
				}
			}
			return m;
		});
		(void)p;
	}

	// ---- the download ----

	std::string Ui::Books::Catalogs::savesTo(const MangaServer& s, long long* freeBytes) const
	{
		// the cards' Books folders (named Books, any capitals; else any the shelf reads), the roomiest first
		std::vector<BooksPlace> places, any;
		for (const std::string& f : bookFolders())
		{
			BooksPlace bp{f, freeBytesAt(f)};
			any.push_back(bp);
			if (lowerAscii(nameOfPath(f)) == "books") places.push_back(bp);
		}
		std::vector<std::string> cards = mediaCards();
		std::string folder = chooseBooksFolder(places.empty() ? any : places, cards.empty() ? "" : cards.front());
		if (folder.empty()) return "";
		if (freeBytes) *freeBytes = freeBytesAt(fileThere(folder) ? folder : dirOfPath(folder));
		return folder + "/" + catalogFolderName(s.name);
	}

	void Ui::Books::Catalogs::deleteFromCard(Player& p)
	{
		// only the files this catalog gave the card (its list), never anything else in the folder
		CatalogGot& g = got(openId);
		int n = 0;
		for (const std::string& f : g.paths(book.id))
			if (std::remove(f.c_str()) == 0) n++;
		g.drop(book.id);
		fprintf(stderr, "catalogs: deleted %d file(s) of %s from the card\n", n, book.title.c_str());
		p.osd(n ? "Deleted from the card" : "Nothing was left to delete", 2000);
		ui.books->build();
		ui.rescanLibrary();
	}

	void Ui::Books::Catalogs::startDownload(const OpdsEntry& e, const OpdsLink& offer, Player& p)
	{
		const MangaServer* s = openCatalog();
		if (!s) return;
		if (dl.active)
		{
			p.osd("One at a time: " + dl.title + " is still coming", 2000);
			return;
		}
		long long room = -1;
		std::string dir = savesTo(*s, &room);
		if (dir.empty())
		{
			ui.alert("No card to save it on", "Banana Player found no card with a Books folder.");
			return;
		}
		if (offer.length > 0 && room >= 0 && room < offer.length + (1 << 20))
		{
			downloadFailed(e.title, "Not enough room: it needs " + mb(offer.length) + ", the card has " + mb(room) + " free.", p);
			return;
		}
		// <Books>/<catalog>/: made when it isn't there (and Books itself on the first card when no card has one)
		mkdir(dirOfPath(dir).c_str(), 0755);
		mkdir(dir.c_str(), 0755);
		const char* ext = opdsFormatExt(opdsBookFormat(offer.type, offer.href));
		std::string final = freeFileName(dir + "/" + catalogBookName(e.title, e.author, ext),
			[](const std::string& path) { return fileThere(path); });
		dl = Download();
		dl.active = true;
		dl.catalogId = s->id;
		dl.entryId = e.id;
		dl.title = e.title;
		dl.fmtName = opdsFormatName(offer.type, offer.href);
		dl.url = opdsResolve(bookBase, offer.href);
		dl.final = final;
		dl.hidden = dir + "/." + nameOfPath(final);	// (MangaFetch writes "<hidden>.part" and renames it to <hidden>)
		dl.length = offer.length;
		dl.startedAt = SDL_GetTicks();
		if (!sameOrigin(dl.url, s->url) && !s->user.empty())
			fprintf(stderr, "catalogs: the book's link is on another host; it is fetched without the sign-in\n");
		Pending pd;
		pd.what = P_GET;
		pd.catalogId = s->id;
		pd.url = dl.url;
		dl.req = send(*s, pd);
		fprintf(stderr, "catalogs: download %s (%s, %lld bytes said) into %s\n", dl.url.c_str(), dl.fmtName.c_str(),
			dl.length, dir.c_str());
		if (!dl.req)
		{
			dl.active = false;
			downloadFailed(e.title, "No network in this build.", p);
		}
	}

	void Ui::Books::Catalogs::cancelDownload(const char* why)
	{
		if (!dl.active) return;
		if (fetch && dl.req) fetch->cancel(dl.req);
		pending.erase(dl.req);
		std::remove((dl.hidden + ".part").c_str());
		std::remove(dl.hidden.c_str());
		fprintf(stderr, "catalogs: download cancelled (%s) at %lld bytes\n", why, dl.bytes);
		dl.active = false;
	}

	void Ui::Books::Catalogs::downloadFailed(const std::string& title, const std::string& why, Player& p)
	{
		lastFailure = why;
		// over Books a sheet with Try again; anywhere else a Note (the cell loses its bar either way)
		if (ui.page != Page::Books)
		{
			p.osd(title + " wasn't saved: " + why, 3000);
			return;
		}
		ask("Couldn\xE2\x80\x99t download it", title + " was not saved. Nothing half-downloaded is left on the card. " + why,
			"Close", "Try again", 1, [this](Player& pl) {
				if (bookOpen && fmt < (int)offers.size()) startDownload(book, offers[fmt], pl);
			}, false, true);
	}

	void Ui::Books::Catalogs::getAnswered(MangaFetch::Result& r, const MangaServer& s, Player& p)
	{
		Download d = dl;
		dl.active = false;
		long long bytes = fileBytes(d.hidden);
		std::string why;
		if (r.status == 401) why = s.user.empty() ? "The catalog asks for a sign-in." : "The sign-in was refused.";
		else if (!r.error.empty()) why = reasonOf(r) + ".";
		else if (bytes <= 0) why = "It came empty.";
		else
		{
			// a sign-in page or an error page served as 200: not a book (a book's first bytes are a zip's, a PDF's or text)
			std::string head = lowerAscii(r.bodyStart.substr(0, 64));
			std::string type = lowerAscii(r.contentType);
			bool page = type.find("text/html") != std::string::npos || head.find("<!doctype html") != std::string::npos ||
				head.find("<html") != std::string::npos;
			if (page) why = "The catalog sent a web page, not a book.";
		}
		fprintf(stderr, "catalogs: download %s: %lld bytes in %u ms%s%s\n", why.empty() ? "done" : "failed", bytes,
			(unsigned)(SDL_GetTicks() - d.startedAt), why.empty() ? "" : ": ", why.c_str());
		if (why.empty() && rename(d.hidden.c_str(), d.final.c_str()) != 0) why = "It couldn't be written on the card.";
		if (!why.empty())
		{
				std::remove(d.hidden.c_str());
			std::remove((d.hidden + ".part").c_str());
			if (r.status == 401) signInRefused(s, s.user.empty());
			else downloadFailed(d.title, why, p);
			return;
		}
		got(d.catalogId).set(d.entryId, d.final);
		// on the Shelf at once (the folders read again), and in the index
		ui.books->build();
		ui.rescanLibrary();
		p.osd("Saved to Books", 2000);
	}

	// ---- the covers ----

	std::string Ui::Books::Catalogs::coverDir() const
	{
		return ui.stateDirPath.empty() ? "" : ui.stateDirPath + "/cache/catalog-covers";
	}

	// a hit bumps the file's date: the eviction's oldest-first is then least recently shown. At most once a minute per
	// file (this runs for each row on screen each frame)
	static void touchCover(const std::string& path)
	{
		static std::map<std::string, long long> last;
		long long now = (long long)time(nullptr);
		long long& at = last[path];
		if (now - at < 60) return;
		at = now;
		struct timeval t[2];
		gettimeofday(&t[0], nullptr);
		t[1] = t[0];
		utimes(path.c_str(), t);
	}

	void Ui::Books::Catalogs::pruneCovers()
	{
		static std::atomic<bool> running{false};
		long long now = (long long)SDL_GetTicks();
		if (coverDir().empty() || !coverReq.empty() || now - lastPruneMs < 60000 || running.exchange(true)) return;
		for (const Feed& f : feeds)
			if (f.loading || f.loadingMore)
			{
				running = false;
				return;
			}
		lastPruneMs = now;
		std::string dir = coverDir();
		std::thread([dir] {
			std::vector<artbg::CacheFile> files;
			std::vector<std::string> names;
			if (DIR* d = opendir(dir.c_str()))
			{
				while (dirent* e = readdir(d))
				{
					std::string name = e->d_name;
					if (name.size() != 16) continue;	// only our own 16-hex names (never a .part)
					struct stat st;
					if (stat((dir + "/" + name).c_str(), &st) != 0) continue;
					files.push_back({name, (size_t)st.st_size, (uint64_t)st.st_mtime});
					names.push_back(name);
				}
				closedir(d);
			}
			for (size_t i : artbg::evict(files, COVER_CACHE_CAP)) unlink((dir + "/" + names[i]).c_str());
			running = false;
		}).detach();
	}

	std::string Ui::Books::Catalogs::coverFile(const OpdsEntry& e, const std::string& base, bool page)
	{
		const MangaServer* s = openCatalog();
		std::string href = page ? (e.cover().empty() ? e.thumbnail() : e.cover()) : (e.thumbnail().empty() ? e.cover() : e.thumbnail());
		if (!s || href.empty() || coverDir().empty()) return "";
		std::string url = opdsResolve(base, href);
		std::string file = coverDir() + "/" + coverCacheName(url);
		if (fileThere(file))
		{
			touchCover(file);
			return file;
		}
		if (!page) coverOnScreen.insert(url);
		if (coverFailed.count(url) || coverReq.count(url)) return "";
		// the catalog's own host only: a picture elsewhere is never fetched (no sign-in could go there anyway)
		if (!sameOrigin(url, s->url) || !fetch)
		{
			coverFailed.insert(url);
			return "";
		}
		Pending pd;
		pd.what = P_COVER;
		pd.catalogId = s->id;
		pd.url = url;
		uint64_t id = send(*s, pd);
		if (id) coverReq[url] = id;
		else coverFailed.insert(url);
		return "";
	}

	void Ui::Books::Catalogs::coverAnswered(MangaFetch::Result& r, const Pending& pd)
	{
		coverReq.erase(pd.url);
		// an image, not a sign-in page served as 200
		bool image = r.error.empty() && r.status >= 200 && r.status < 300 && !r.body.empty() &&
			lowerAscii(r.contentType).find("text/") == std::string::npos && r.body[0] != '<';
		std::string dir = coverDir(), file = dir + "/" + coverCacheName(pd.url);
		if (image)
		{
			mkdir((ui.stateDirPath + "/cache").c_str(), 0755);
			mkdir(dir.c_str(), 0755);
			std::string part = file + ".part";
			if (FILE* f = fopen(part.c_str(), "wb"))
			{
				bool ok = fwrite(r.body.data(), 1, r.body.size(), f) == r.body.size();
				ok = fclose(f) == 0 && ok;
				image = ok && rename(part.c_str(), file.c_str()) == 0;
				if (!image) std::remove(part.c_str());
			}
			else image = false;
		}
		if (!image) coverFailed.insert(pd.url);
		coverSerial++;
		pruneCovers();
	}

	void Ui::Books::Catalogs::coverCancelOffScreen()
	{
		for (auto it = coverReq.begin(); it != coverReq.end();)
		{
			if (coverOnScreen.count(it->first)) { ++it; continue; }
			if (fetch) fetch->cancel(it->second);
			pending.erase(it->second);
			it = coverReq.erase(it);
		}
	}

	// ---- the form ----

	void Ui::Books::Catalogs::openForm(const MangaServer* existing, int at)
	{
		formOpen = true;
		formId = existing ? existing->id : "";
		fAddress = existing ? existing->url : "";
		fName = existing ? existing->name : "";
		fUser = existing ? existing->user : "";
		fKind = existing ? existing->kind : "opds";
		fAccount = existing && (!existing->user.empty() || (existing->hasPassword() && existing->kind != "kavita"));
		fPassword.clear();
		fPasswordTyped = false;
		fTest.clear();
		fNote.clear();
		fTestOk = false;
		fSaveAfterTest = false;
		fTestReq = 0;
		fCopies = mangaCopies();
		fCursor = std::clamp(at, 0, formRows() + 1);
	}

	int Ui::Books::Catalogs::formRows() const
	{
		return formBase() + formFoundCount();
	}

	int Ui::Books::Catalogs::formBase() const
	{
		return 3 + (fAccount ? 2 : 0) + (fCopies.empty() ? 0 : 1);
	}

	int Ui::Books::Catalogs::formRowId(int i) const
	{
		// 0 Address, 1 Name, 2 Sign in, 3 User, 4 Password, 5 Copy from Manga's servers…, 6+ a found catalog
		if (i >= formBase()) return 6 + (i - formBase());
		if (i < 3) return i;
		if (fAccount && i < 5) return i;
		return 5;
	}

	std::vector<MangaServer> Ui::Books::Catalogs::mangaCopies() const
	{
		std::vector<MangaServer> out;
		// (read only when Manga has saved servers: loading makes the folder and its key otherwise)
		if (ui.stateDirPath.empty() || !fileThere(ui.stateDirPath + "/manga/servers.tsv")) return out;
		MangaServerStore manga;
		manga.load(ui.stateDirPath);
		for (const MangaServer& m : manga.list())
		{
			if (m.kind != "opds" && m.kind != "kavita") continue;
			bool have = false;
			for (const MangaServer& c : store.list())
				if (c.url == m.url) have = true;
			if (!have) out.push_back(m);
		}
		return out;
	}

	MangaServer Ui::Books::Catalogs::formServer() const
	{
		MangaServer s;
		s.id = formId;
		std::string a = fAddress;
		while (!a.empty() && a.back() == ' ') a.pop_back();
		if (!a.empty() && a.find("://") == std::string::npos) a = "http://" + a;	// a bare host
		s.url = a;
		s.kind = lowerAscii(a).find("/api/opds") != std::string::npos ? "kavita" : "opds";
		s.user = fAccount && s.kind != "kavita" ? fUser : "";
		s.name = fName.empty() ? hostOf(a) : fName;
		if (s.name.size() > 40) s.name.resize(40);
		return s;
	}

	void Ui::Books::Catalogs::formTest(bool thenSave)
	{
		MangaServer s = formServer();
		if (s.url.empty())
		{
			fNote = "Type the address first";
			fCursor = 0;
			return;
		}
		fTestServer = s;
		fTestServer.id.clear();	// (its answers find it by this, not in the store)
		fTestPassword = fPasswordTyped ? fPassword : !formId.empty() ? store.password(formId) : "";
		// a Kavita address carries its key in the address: it goes in as the password, as the store will keep it
		std::string stored, key;
		if (s.kind == "kavita" && readKavitaUrl(s.url, stored, key) && !key.empty())
		{
			fTestServer.url = stored;
			fTestPassword = key;
		}
		logins.erase("");
		fTest = "Trying\xE2\x80\xA6";
		fTestOk = false;
		fSaveAfterTest = thenSave;
		Pending pd;
		pd.what = P_TEST;
		pd.url = fTestServer.url;
		fTestReq = send(fTestServer, pd);
		if (!fTestReq) fTest = "No network in this build";
	}

	void Ui::Books::Catalogs::testAnswered(MangaFetch::Result& r, const Pending&, const MangaServer& s, Player& p)
	{
		if (r.id != fTestReq || !formOpen) return;
		OpdsFeed f;
		std::string why;
		if (r.status == 401) why = s.user.empty() ? "It asks for a sign-in: choose An account" : "The sign-in was refused";
		else if (!r.error.empty()) why = reasonOf(r);
		else if (!parseOpds(remoteWithoutKey(s, fTestPassword, bodyText(r)), f)) why = "Not a catalog: it answers with something else";
		fTestOk = why.empty();
		if (fTestOk)
		{
			std::string count = f.total > 0 ? std::to_string(f.total) + " books" : std::to_string(f.entries.size()) +
				(f.entries.size() == 1 ? " entry" : " entries");
			fTest = "It works \xC2\xB7 " + count;
			// the catalog's own title names it, unless one was typed
			if (fName.empty() && !f.title.empty()) fName = f.title.size() > 40 ? f.title.substr(0, 40) : f.title;
		}
		else fTest = why;
		if (fSaveAfterTest)
		{
			fSaveAfterTest = false;
			if (fTestOk) formSave(p, true);
			else
				ask("Save it anyway?", "The test didn't work: " + why + ".", "Edit", "Save anyway", 0,
					[this](Player& pl) { formSave(pl, true); });
		}
	}

	void Ui::Books::Catalogs::formSave(Player& p, bool anyway)
	{
		MangaServer s = formServer();
		if (s.url.empty() || !isMangaServerUrl(withoutUrlCredentials(s.url)))
		{
			fNote = s.url.empty() ? "Type the address first" : "That address can't be used";
			fCursor = 0;
			return;
		}
		if (!anyway && !fTestOk)
		{
			formTest(true);	// tests first; a failed test asks Save anyway / Edit
			return;
		}
		std::string id;
		if (formId.empty()) id = store.add(s);
		else id = store.update(s) ? formId : "";
		if (id.empty())
		{
			fNote = "Couldn't save it";
			return;
		}
		if (fPasswordTyped && s.kind != "kavita") store.setPassword(id, fAccount ? fPassword : "");
		else if (!fAccount && s.kind != "kavita") store.setPassword(id, "");
		logins.erase(id);	// a changed login is asked for again
		formOpen = false;
		auto list_ = store.list();
		for (size_t i = 0; i < list_.size(); i++)
			if (list_[i].id == id) listCursor = (int)i;
		if (openId == id) { feeds.clear(); openId.clear(); bookOpen = false; }
		p.osd("Saved " + s.name, 2000);
	}

	void Ui::Books::Catalogs::copyFromManga()
	{
		std::vector<MangaServer> v = fCopies;
		if (v.empty()) return;
		ui.openMore([this, v](const Player&) {
			MoreSpec m;
			m.glyph = &G_WEB;
			m.title = "Copy from Manga\xE2\x80\x99s servers";
			for (const MangaServer& s : v)
				m.rows.push_back({&G_WEB, s.name, hostOf(s.url), "", [this, s](Player&) {
					MangaServerStore manga;
					manga.load(ui.stateDirPath);
					fAddress = s.url;
					fName = s.name;
					fUser = s.user;
					fKind = s.kind;
					std::string pw = manga.password(s.id);
					fAccount = s.kind != "kavita" && (!s.user.empty() || !pw.empty());
					fPassword = pw;	// sealed again, with this list's key, on Save
					fPasswordTyped = !pw.empty();
					if (s.kind == "kavita" && !pw.empty())
					{
						// the key goes back where Kavita prints it, so Save seals it as the store does any address's
						size_t at = fAddress.find("{key}");
						if (at != std::string::npos) fAddress.replace(at, 5, pw);
						fPasswordTyped = false;
					}
					formRowsChanged();
					return false;
				}});
			return m;
		});
	}

	void Ui::Books::Catalogs::formChoose(int id, Player& p)
	{
		switch (id)
		{
		case 0: ui.openKeyboard(CAT_ADDR_FIELD, fAddress.empty() ? "http://" : fAddress); break;
		case 1: ui.openKeyboard(CAT_NAME_FIELD, fName); break;
		case 2: fAccount = !fAccount; formRowsChanged(); break;
		case 3: ui.openKeyboard(CAT_USER_FIELD, fUser); break;
		case 4: ui.openKeyboard(CAT_PASS_FIELD, fPasswordTyped ? fPassword : ""); break;
		case 5: copyFromManga(); break;
		default:
		{
			std::vector<FoundCatalog> o = offered();
			if (id - 6 < (int)o.size()) pickFound(o[id - 6]);
			break;
		}
		}
		(void)p;
	}

	void Ui::Books::Catalogs::fieldTyped(int field, const std::string& text)
	{
		auto trimmed_ = [](std::string s) {
			while (!s.empty() && s.back() == ' ') s.pop_back();
			size_t a = 0;
			while (a < s.size() && s[a] == ' ') a++;
			return s.substr(a);
		};
		switch (field)
		{
		case CAT_ADDR_FIELD:
		{
			std::string a = trimmed_(text);
			if (a == "http://" || a == "https://") a.clear();
			// a user:password@ part goes into the rows, never into the address that is kept
			size_t scheme = a.find("://");
			size_t start = scheme == std::string::npos ? 0 : scheme + 3;
			size_t end = a.find_first_of("/?#", start);
			size_t at = a.rfind('@', end == std::string::npos ? std::string::npos : end);
			if (at != std::string::npos && at >= start)
			{
				std::string cred = a.substr(start, at - start);
				size_t colon = cred.find(':');
				fUser = cred.substr(0, colon);
				if (colon != std::string::npos)
				{
					fPassword = cred.substr(colon + 1);
					fPasswordTyped = true;
				}
				fAccount = true;
				a.erase(start, at - start + 1);
			}
			fAddress = a;
			break;
		}
		case CAT_NAME_FIELD: fName = trimmed_(text).substr(0, 40); break;
		case CAT_USER_FIELD: fUser = trimmed_(text); break;
		case CAT_PASS_FIELD: fPassword = text; fPasswordTyped = true; break;	// (spaces kept: they can be a password's)
		case CAT_SEARCH_FIELD: search(trimmed_(text)); return;
		default: return;
		}
		formRowsChanged();
	}

	// ---- found on this network ----

	namespace
	{
		constexpr int FOUND_LABEL_H = 18;
		constexpr int FOUND_ROW_H = lay::FORM_ROW;
	}

	std::vector<FoundCatalog> Ui::Books::Catalogs::offered() const
	{
		// the same address however it was typed: no scheme, no trailing slash, any case
		auto plain = [](std::string u) {
			u = lowerAscii(u);
			if (u.compare(0, 7, "http://") == 0) u.erase(0, 7);
			while (!u.empty() && u.back() == '/') u.pop_back();
			return u;
		};
		std::vector<std::string> have;
		for (const MangaServer& s : store.list()) have.push_back(plain(s.url));
		std::vector<FoundCatalog> out;
		for (const FoundCatalog& c : found)
			if (std::find(have.begin(), have.end(), plain(c.url)) == have.end()) out.push_back(c);
		return out;
	}

	void Ui::Books::Catalogs::startFind()
	{
		if (finding || !findEvent) return;
		finding = true;
		uint64_t gen = ++findGen;
		Uint32 ev = findEvent;
		std::thread([gen, ev] {
			uint32_t t0 = SDL_GetTicks();
			std::vector<FoundCatalog> v;
			// BP_CATALOG_FOUND="name|url": a test stands in for the LAN (a shell without Local Network access can't browse)
			if (const char* fake = getenv("BP_CATALOG_FOUND"); fake && strchr(fake, '|'))
				v.push_back({std::string(fake, strchr(fake, '|')), strchr(fake, '|') + 1});
			else v = findCalibreCatalogs(3000);
			fprintf(stderr, "catalogs: found %zu on the network in %u ms\n", v.size(), (unsigned)(SDL_GetTicks() - t0));
			SDL_Event e;
			SDL_zero(e);
			e.type = ev;
			e.user.code = (Sint32)gen;
			e.user.data1 = new std::vector<FoundCatalog>(std::move(v));
			if (SDL_PushEvent(&e) <= 0) delete (std::vector<FoundCatalog>*)e.user.data1;
		}).detach();
	}

	void Ui::Books::Catalogs::onFound(const SDL_Event& e)
	{
		std::unique_ptr<std::vector<FoundCatalog>> v((std::vector<FoundCatalog>*)e.user.data1);
		if ((uint64_t)e.user.code != findGen || !v) return;
		finding = false;
		found = std::move(*v);
		foundCursor = std::min(foundCursor, emptyFoundCount() - 1);
	}

	int Ui::Books::Catalogs::emptyFoundCount() const
	{
		return std::min<int>(2, (int)offered().size());
	}

	lay::EmptyState Ui::Books::Catalogs::emptyLay() const
	{
		// found rows take the room under the keys, so the description line goes
		Rect a = listArea();
		int n = emptyFoundCount();
		if (n == 0) return lay::emptyState(a, 2, 2);
		a.h -= FOUND_LABEL_H + n * (FOUND_ROW_H + lay::SP2);
		return lay::emptyState(a, 0, 2);
	}

	Ui::Rect Ui::Books::Catalogs::emptyFoundRect(int i) const
	{
		Rect a = listArea();
		int n = emptyFoundCount();
		int top = a.y + a.h - n * (FOUND_ROW_H + lay::SP2);
		return {a.x, top + i * (FOUND_ROW_H + lay::SP2), a.w, FOUND_ROW_H};
	}

	int Ui::Books::Catalogs::formFoundCount() const
	{
		if (!formId.empty()) return 0;
		lay::Box in = body();
		int keysY = in.y + in.h - lay::KEY_ROW_H - lay::SP8;
		int free = keysY - 18 - (in.y + formBase() * (lay::FORM_ROW + lay::SP2) + FOUND_LABEL_H);	// 18: the note line
		int fit = std::max(0, free / (FOUND_ROW_H + lay::SP2));
		return std::min<int>({3, fit, (int)offered().size()});
	}

	void Ui::Books::Catalogs::pickFound(const FoundCatalog& c)
	{
		if (!formOpen) openForm(nullptr);
		fKind = "opds";
		fAddress = c.url;
		fName = c.name;
		fUser.clear();
		fPassword.clear();
		fPasswordTyped = false;
		fAccount = false;
		formRowsChanged();
		fCursor = formRows();	// on Test: the user still tests and saves
	}

	// ---- the layout ----

	lay::Box Ui::Books::Catalogs::body() const
	{
		lay::Shell s = lay::shell();
		return {s.body.x + lay::SP12, s.body.y, s.body.w - 2 * lay::SP12, s.body.h};
	}

	Ui::Rect Ui::Books::Catalogs::listArea() const
	{
		if (view() == V_LIST) return ui.books->layout().page.content;
		return body();
	}

	Ui::Rect Ui::Books::Catalogs::formRowRect(int i) const
	{
		lay::Box in = body();
		int n = formRows();
		if (i < n) return {in.x, in.y + i * (lay::FORM_ROW + lay::SP2) + (i >= formBase() ? FOUND_LABEL_H : 0), in.w, lay::FORM_ROW};
		auto keys = lay::keyRow({in.x, in.y + in.h - lay::KEY_ROW_H - lay::SP8, in.w, lay::KEY_ROW_H}, {0, 0});
		return keys[std::min(i - n, 1)];
	}

	int Ui::Books::Catalogs::feedCols() const
	{
		// (a search's results are the two-line list, as the spec draws them: title, author, formats)
		if (feeds.empty() || !feeds.back().books() || listView || feeds.back().search) return 1;
		return ui.books->layout().cols;
	}

	int Ui::Books::Catalogs::feedRowH() const
	{
		if (feeds.empty()) return NAV_ROW_H;
		const Feed& f = feeds.back();
		if (!f.books()) return NAV_ROW_H;
		return feedCols() > 1 ? lay::SHELF_CELL_H + lay::COVER_GAP : BOOK_ROW_H;
	}

	int Ui::Books::Catalogs::feedCount() const
	{
		if (feeds.empty()) return 0;
		const Feed& f = feeds.back();
		int items = (int)f.feed.entries.size() + (f.books() ? 0 : (canSearch() && !f.search && feeds.size() == 1 ? 1 : 0));
		int cols = feedCols();
		int rows = (items + cols - 1) / cols;
		return rows + (!f.feed.next.empty() ? 1 : 0);	// the foot row: "Loading the next page…"
	}

	Ui::Books::Catalogs::BookLayout Ui::Books::Catalogs::bookLayout() const
	{
		lay::Box in = body();
		BookLayout b;
		b.cover = {in.x, in.y + lay::SP4, 116, 164};
		int tx = b.cover.x + b.cover.w + lay::SP16, tw = in.x + in.w - tx;
		b.author = {tx, in.y + lay::SP4, tw, 24};
		b.facts = {tx, b.author.y + 28, tw, 18};
		b.summary = {tx, b.facts.y + 26, tw, 4 * 20};
		int y = b.cover.y + b.cover.h + lay::SP12;
		b.chips = {in.x, y, in.w, lay::CHIP_H};
		if (offers.size() > 1) y += lay::CHIP_H + lay::SP8;
		b.key = {in.x, y, in.w, 56};
		b.under = {in.x, y + 56 + lay::SP8, in.w, 20};
		return b;
	}

	Ui::SmoothList* Ui::Books::Catalogs::smoothList()
	{
		return view() == V_FEED && !feeds.back().loading && feeds.back().error == E_NONE && feedCount() > 0 ? &list : nullptr;
	}

	// ---- the head (Books' shell head asks) ----

	void Ui::Books::Catalogs::head(ShellHead& h) const
	{
		const MangaServer* s = store.find(openId);
		switch (view())
		{
		case V_LIST:
		{
			size_t n = store.list().size();
			h.crumb = n == 0 ? "Catalogs \xC2\xB7 none saved" : "Catalogs \xC2\xB7 " + std::to_string(n) + " saved \xC2\xB7 OPDS";
			h.more = n > 0;
			if (n == 0) h.hints = {{"A", "Add a catalog"}, {"Y", "Try Gutenberg"}, {"L/R", "Tab"}, {"B", "Quit"}};
			else h.hints = {{"A", "Open"}, {"Y", "Add"}, {"X", "More"}, {"L/R", "Tab"}, {"B", "Quit"}};
			break;
		}
		case V_FORM:
			h.title = "Add a catalog";
			if (const MangaServer* e = store.find(formId)) h.title = e->name;
			h.crumb = "Books \xE2\x80\xBA Catalogs \xE2\x80\xBA OPDS 1.2";
			h.back = true;
			h.more = false;
			h.hints = {{"A", "Change"}, {"Y", "Test"}, {"START", "Save"}, {"B", "Back"}};
			break;
		case V_FEED:
		{
			const Feed& f = feeds.back();
			h.title = f.title;
			// the top: where it is ("Books › Catalogs › Home Calibre"); deeper: the catalog, then this page
			std::string crumb = "Books \xE2\x80\xBA Catalogs \xE2\x80\xBA " + (s ? s->name : "");
			if (feeds.size() > 1) crumb = (s ? s->name : "") + "  \xE2\x80\xBA  " + (f.search ? "Search" : f.title);
			if (!f.loading && f.error == E_NONE && f.books())
			{
				crumb += " \xC2\xB7 " + std::to_string(f.feed.entries.size());
				if (f.feed.total > (int)f.feed.entries.size()) crumb += " of " + std::to_string(f.feed.total);
			}
			h.crumb = crumb;
			h.back = true;
			h.more = true;
			if (f.error != E_NONE) h.hints = {{"A", "Try again"}, {"Y", "Open the Shelf"}, {"X", "More"}, {"B", "Back"}};
			else if (canSearch()) h.hints = {{"A", "Open"}, {"Y", "Search"}, {"X", "More"}, {"B", "Back"}};
			else h.hints = {{"A", "Open"}, {"X", "More"}, {"B", "Back"}};
			break;
		}
		case V_BOOK:
		{
			h.title = book.title;
			h.crumb = (s ? s->name : "") + (feeds.empty() ? "" : "  \xE2\x80\xBA  " + feeds.back().title);
			h.back = true;
			h.more = true;
			bool getting = dl.active && dl.entryId == book.id && dl.catalogId == openId;
			if (getting) h.hints = {{"Y", "Cancel"}, {"B", "Back: it carries on"}};
			else if (!onCard(book.id).empty()) h.hints = {{"A", "Read"}, {"X", "More"}, {"B", "Back"}};
			else if (offers.empty()) h.hints = {{"A", "Why"}, {"B", "Back"}};
			else if (offers.size() > 1) h.hints = {{"A", "Download"}, {"LEFT/RIGHT", "Format"}, {"X", "More"}, {"B", "Back"}};
			else h.hints = {{"A", "Download"}, {"X", "More"}, {"B", "Back"}};
			break;
		}
		}
	}

	// ---- the keys ----

	bool Ui::Books::Catalogs::key(Button b, Player& p)
	{
		load();
		switch (view())
		{
		case V_LIST:
		{
			std::vector<MangaServer> v = store.list();
			int n = (int)v.size();
			if (n == 0)
			{
				int shown = emptyFoundCount();
				foundCursor = std::min(foundCursor, shown - 1);
				if (b == Button::Down) foundCursor = std::min(shown - 1, foundCursor + 1);
				else if (b == Button::Up) foundCursor = std::max(-1, foundCursor - 1);
				else if (b == Button::A && foundCursor >= 0) pickFound(offered()[foundCursor]);
				else if (b == Button::A) openForm(nullptr);
				else if (b == Button::Y) addGutenberg(p);
				return false;
			}
			listCursor = std::clamp(listCursor, 0, n);
			switch (b)
			{
			case Button::Up: listCursor = std::max(0, listCursor - 1); break;
			case Button::Down: listCursor = std::min(n, listCursor + 1); break;
			case Button::Y: openForm(nullptr); break;
			case Button::A:
				if (listCursor == n) openForm(nullptr);
				else openCatalogAt(v[listCursor].id, p);
				break;
			case Button::X:
			{
				if (listCursor >= n) break;
				std::string id = v[listCursor].id;
				ui.openMore([this, id](const Player& pl) {
					MoreSpec m;
					const MangaServer* s = store.find(id);
					m.glyph = &G_WEB;
					m.title = s ? s->name : "";
					m.note = s ? hostOf(s->url) : "";
					m.rows.push_back({&G_RIGHT, "Open", "", "", [this, id](Player& pp) { openCatalogAt(id, pp); return false; }});
					m.rows.push_back({&G_PENCIL, "Edit catalog\xE2\x80\xA6", "", "", [this, id](Player&) {
						if (const MangaServer* c = store.find(id)) openForm(c);
						return false;
					}});
					m.rows.push_back({&G_WIFI, "Test connection", "", "", [this, id](Player&) {
						if (const MangaServer* c = store.find(id))
						{
							openForm(c, 0);
							formTest(false);
						}
						return false;
					}});
					m.rows.push_back({&G_TRASH, "Remove catalog\xE2\x80\xA6", "", "", [this, id](Player&) {
						const MangaServer* c = store.find(id);
						if (!c) return false;
						ask("Remove " + c->name + "?", "Its address and sign-in go. The books it downloaded stay on the "
							"card and on the Shelf.", "Keep", "Remove", 0, [this, id](Player& pp) { removeCatalog(id, pp); }, true);
						return false;
					}, true});
					ui.moreTwoColumns(m, {{&G_PLUS, "Add a catalog\xE2\x80\xA6", "", "", [this](Player&) {
						openForm(nullptr);
						return false;
					}}, ui.sourceMoreRows(LibKind::Books, pl).front()});
					return m;
				});
				break;
			}
			default: break;
			}
			return false;
		}
		case V_FORM:
		{
			int n = formRows();
			switch (b)
			{
			case Button::Up: fCursor = std::max(0, fCursor - 1); break;
			case Button::Down: fCursor = std::min(n + 1, fCursor + 1); break;
			case Button::Left: case Button::Right:
				if (fCursor < n && formRowId(fCursor) == 2) formChoose(2, p);
				else if (fCursor >= n) fCursor = fCursor == n ? n + 1 : n;
				break;
			case Button::A:
				if (fCursor < n) formChoose(formRowId(fCursor), p);
				else if (fCursor == n) formTest(false);
				else formSave(p, false);
				break;
			case Button::Y: formTest(false); break;
			case Button::Start: formSave(p, false); break;
			case Button::B:
				formOpen = false;
				fTestReq = 0;
				break;
			default: break;
			}
			fCursor = std::clamp(fCursor, 0, formRows() + 1);
			return false;
		}
		case V_FEED:
		{
			Feed& f = feeds.back();
			if (b == Button::B) { feedBack(); return false; }
			if (f.loading) return false;
			if (f.error != E_NONE || f.feed.entries.empty())
			{
				// the Empty state's keys: Try again (Search again on a search that found nothing), the Shelf
				if (b == Button::A && f.error == E_NONE && f.search) ui.openKeyboard(CAT_SEARCH_FIELD, searchWords);
				else if (b == Button::A) loadFeed((int)feeds.size() - 1);
				else if (b == Button::Y) { feeds.clear(); openId.clear(); ui.books->setTab(lay::BOOKS_SHELF); }
				else if (b == Button::X) { if (const MangaServer* s = openCatalog()) openForm(s); }
				return false;
			}
			bool searchRow = !f.books() && canSearch() && !f.search && feeds.size() == 1;
			int items = (int)f.feed.entries.size() + (searchRow ? 1 : 0);
			int cols = feedCols();
			// the cursor runs over the rows shown (the search row first on a catalog's top page)
			int c = f.cursor + (searchRow ? 1 : 0);
			switch (b)
			{
			case Button::Up: c = std::max(0, c - cols); break;
			case Button::Down: c = std::min(items - 1, c + cols); break;
			case Button::Left: if (cols > 1) c = std::max(0, c - 1); break;
			case Button::Right: if (cols > 1) c = std::min(items - 1, c + 1); break;
			case Button::Y: if (canSearch()) ui.openKeyboard(CAT_SEARCH_FIELD, searchWords); return false;
			case Button::X: feedMore(p); return false;
			case Button::A:
				if (searchRow && c == 0) ui.openKeyboard(CAT_SEARCH_FIELD, searchWords);
				else openEntry(p);
				return false;
			default: break;
			}
			f.cursor = c - (searchRow ? 1 : 0);
			if (f.cursor < 0) f.cursor = searchRow ? -1 : 0;
			// within a row of the end: the next page comes by itself
			if (!f.feed.next.empty() && c / cols >= (items - 1) / cols - 1) loadMore((int)feeds.size() - 1);
			list.reveal(c / cols, listArea(), feedRowH(), feedCount());
			return false;
		}
		case V_BOOK:
		{
			bool getting = dl.active && dl.entryId == book.id && dl.catalogId == openId;
			std::string have = onCard(book.id);
			switch (b)
			{
			case Button::B: bookOpen = false; break;
			case Button::Left: if (!getting && offers.size() > 1) fmt = std::max(0, fmt - 1); break;
			case Button::Right: if (!getting && offers.size() > 1) fmt = std::min((int)offers.size() - 1, fmt + 1); break;
			case Button::Y: if (getting) cancelDownload("Y on the book's page"); break;
			case Button::X: bookMore(p); break;
			case Button::A:
				if (getting) break;	// a second A must not undo the first (Cancel is Y)
				if (!have.empty()) ui.openDocument(have, p);
				else if (offers.empty()) p.osd("No EPUB, PDF, TXT, CBZ or CBR of this book", 2500);
				else startDownload(book, offers[fmt], p);
				break;
			default: break;
			}
			return false;
		}
		}
		return false;
	}

	// ---- a finger ----

	bool Ui::Books::Catalogs::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		const float fy = pt.y * Ui::H;
		View v = view();
		bool feedList = v == V_FEED && smoothList();
		if (pt.kind == Pointer::Move)
		{
			if (onList) { if (list.move(fy)) touchMoved = true; }
			else if (!touchMoved && std::abs(y - touchY) > 6) touchMoved = true;
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			if (onList) list.lift();
			int was = pressed;
			bool moved = touchMoved || caught;
			pressed = -1;
			touchMoved = onList = caught = false;
			if (moved || was < 0) return false;
			switch (v)
			{
			case V_LIST:
			{
				int n = (int)store.list().size();
				if (n == 0)
				{
					lay::EmptyState e = emptyLay();
					if (was >= 200 && was - 200 < emptyFoundCount() && emptyFoundRect(was - 200).hit(x, y)) pickFound(offered()[was - 200]);
					else if (was == 100 && e.key[0].hit(x, y)) { foundCursor = -1; key(Button::A, p); }
					else if (was == 101 && e.key[1].hit(x, y)) key(Button::Y, p);
					return false;
				}
				Rect a = listArea();
				Rect row{a.x, a.y + was * (BOOK_ROW_H + lay::SP2), a.w, BOOK_ROW_H};
				if (row.hit(x, y)) { listCursor = was; key(Button::A, p); }
				return false;
			}
			case V_FORM:
				if (formRowRect(was).hit(x, y)) { fCursor = was; key(Button::A, p); }
				return false;
			case V_FEED:
			{
				Feed& f = feeds.back();
				if (f.error != E_NONE || f.loading)
				{
					lay::EmptyState e = lay::emptyState(listArea(), 2, 2);
					if (was == 100 && e.key[0].hit(x, y)) key(Button::A, p);
					else if (was == 101 && e.key[1].hit(x, y)) key(Button::Y, p);
					return false;
				}
				int cols = feedCols(), r = list.rowAt(x, y);
				if (r < 0 || r != was / cols) return false;
				bool searchRow = !f.books() && canSearch() && !f.search && feeds.size() == 1;
				f.cursor = was - (searchRow ? 1 : 0);
				key(Button::A, p);
				return false;
			}
			case V_BOOK:
			{
				BookLayout l = bookLayout();
				if (was == 100 && l.key.hit(x, y)) key(dl.active && dl.entryId == book.id ? Button::Y : Button::A, p);
				else if (was >= 200 && offers.size() > 1)
				{
					std::vector<Kit::FilterItem> items;
					for (const OpdsLink& o : offers) items.push_back({opdsFormatName(o.type, o.href)});
					lay::FilterChips c = lay::filterChips(l.chips, ui.kit->filterWidths(items));
					if (lay::filterAt(c, x, y) == was - 200) fmt = was - 200;
				}
				return false;
			}
			}
			return false;
		}
		// the down: what is under the finger
		pressed = -1;
		touchMoved = onList = caught = false;
		touchY = y;
		switch (v)
		{
		case V_LIST:
		{
			int n = (int)store.list().size();
			if (n == 0)
			{
				lay::EmptyState e = emptyLay();
				for (int i = 0; i < emptyFoundCount(); i++)
					if (emptyFoundRect(i).hit(x, y)) pressed = 200 + i;
				if (e.key[0].hit(x, y)) pressed = 100;
				else if (e.key[1].hit(x, y)) pressed = 101;
				return false;
			}
			Rect a = listArea();
			for (int i = 0; i <= n; i++)
				if (Rect{a.x, a.y + i * (BOOK_ROW_H + lay::SP2), a.w, BOOK_ROW_H}.hit(x, y)) pressed = i;
			return false;
		}
		case V_FORM:
			for (int i = 0; i < formRows() + 2; i++)
				if (formRowRect(i).hit(x, y)) pressed = i;
			return false;
		case V_FEED:
		{
			if (!feedList)
			{
				lay::EmptyState e = lay::emptyState(listArea(), 2, 2);
				if (e.key[0].hit(x, y)) pressed = 100;
				else if (e.key[1].hit(x, y)) pressed = 101;
				return false;
			}
			if (!listArea().hit(x, y)) return false;
			onList = true;
			caught = list.press(fy);
			int r = caught ? -1 : list.rowAt(x, y);
			if (r < 0) return false;
			int cols = feedCols();
			if (cols == 1) { pressed = r; return false; }
			Rect row = list.rowBox(r);
			std::vector<lay::Box> cells = lay::coverCells({row.x + lay::SP6, row.y + lay::SP6, row.w - 2 * lay::SP6, lay::SHELF_CELL_H},
				cols, cols, lay::SHELF_CELL_H);
			for (int c = 0; c < (int)cells.size(); c++)
				if (cells[c].hit(x, y)) pressed = r * cols + c;
			return false;
		}
		case V_BOOK:
		{
			BookLayout l = bookLayout();
			if (l.key.hit(x, y)) pressed = 100;
			else if (offers.size() > 1)
			{
				std::vector<Kit::FilterItem> items;
				for (const OpdsLink& o : offers) items.push_back({opdsFormatName(o.type, o.href)});
				int at = lay::filterAt(lay::filterChips(l.chips, ui.kit->filterWidths(items)), x, y);
				if (at >= 0) pressed = 200 + at;
			}
			return false;
		}
		}
		return false;
	}

	// ---- each loop pass ----

	void Ui::Books::Catalogs::tick(Player&)
	{
		// the browse for Calibre starts when the empty tab or the Add form comes up (again, on each re-entry)
		if (ui.books->onCatalogs())
		{
			load();
			bool on = formOpen ? formId.empty() : view() == V_LIST && store.list().empty();
			if (on && !findWasOn) startFind();
			findWasOn = on;
		}
		else findWasOn = false;
		// the download's progress: the part file's size, read a few times a second while a page that shows it is open
		if (!dl.active) return;
		uint32_t now = SDL_GetTicks();
		if (now - dl.statAt < 250) return;
		dl.statAt = now;
		long long b = fileBytes(dl.hidden + ".part");
		if (b >= 0) dl.bytes = b;
	}

	// ---- the drawing ----

	void Ui::Books::Catalogs::draw(const Player& p)
	{
		load();
		switch (view())
		{
		case V_LIST: drawList(p); break;
		case V_FORM: drawForm(p); break;
		case V_FEED: drawFeed(p); break;
		case V_BOOK: drawBook(p); break;
		}
	}

	void Ui::Books::Catalogs::drawList(const Player& p)
	{
		Kit& k = *ui.kit;
		Rect a = listArea();
		std::vector<MangaServer> v = store.list();
		if (v.empty())
		{
			// books-catalogs-empty: what a catalog is, the form on A, a public one on Y
			std::vector<std::string> lines = ui.wrapText("Download books from your Calibre library, or from any OPDS "
				"catalog, straight onto the card.", F_I14, 400, 3);
			std::vector<FoundCatalog> o = offered();
			if (!o.empty()) lines.clear();	// the found rows take its room
			lay::EmptyState e = emptyLay();
			Kit::Empty em;
			em.glyph = &G_WEB;
			em.title = "No catalogs yet";
			em.lines = lines;
			em.key1 = "Add a catalog";
			em.glyph1 = &G_PLUS;
			em.key2 = "Try Gutenberg";
			em.glyph2 = &G_BOOK;
			em.pressed1 = pressed == 100 && !touchMoved;
			em.pressed2 = pressed == 101 && !touchMoved;
			em.hotkeys = p.settings.buttonHotkeys;
			k.emptyState(e, em);
			int shown = emptyFoundCount();
			if (shown > 0)
			{
				Rect first = emptyFoundRect(0);
				ui.drawText("Found on this network", F_I12, DIM, first.x + lay::SP4, first.y - FOUND_LABEL_H + 2);
				foundCursor = std::min(foundCursor, shown - 1);
				for (int i = 0; i < shown; i++)
					drawFoundRow(emptyFoundRect(i), o[i], foundCursor == i, pressed == 200 + i && !touchMoved);
			}
			else if (finding)
				ui.drawTextMid("Looking on this network\xE2\x80\xA6", F_I12, DIM, a.x, a.y + a.h - 20, a.w);
			return;
		}
		int n = (int)v.size();
		listCursor = std::clamp(listCursor, 0, n);
		for (int i = 0; i <= n; i++)
		{
			Rect r{a.x, a.y + i * (BOOK_ROW_H + lay::SP2), a.w, BOOK_ROW_H};
			if (r.y + r.h > a.y + a.h) break;
			if (i == n)
			{
				Kit::Row add;
				add.glyph = &G_PLUS;
				add.name = "Add a catalog\xE2\x80\xA6";
				add.key = "Y";
				add.under = BG;
				add.focused = listCursor == n;
				add.pressed = pressed == n && !touchMoved;
				k.row({r.x, r.y + (BOOK_ROW_H - 52) / 2, r.w, 52}, add);
				break;
			}
			const MangaServer& s = v[i];
			Kit::Media m;
			m.glyph = &G_WEB;
			m.name = s.name;
			m.second = hostOf(s.url) + (s.user.empty() ? (s.kind == "kavita" ? " \xC2\xB7 key saved" : " \xC2\xB7 no sign-in")
				: " \xC2\xB7 signed in as " + s.user);
			if (dl.active && dl.catalogId == s.id) m.note = "Downloading 1";
			else
			{
				int got_ = got(s.id).count();
				if (got_ > 0) m.note = std::to_string(got_) + " downloaded";
			}
			m.focused = listCursor == i;
			m.pressed = pressed == i && !touchMoved;
			k.mediaRow(lay::mediaRow(r), m);
		}
	}

	void Ui::Books::Catalogs::drawFoundRow(Rect r, const FoundCatalog& c, bool focused, bool pressed)
	{
		Kit::Row row;
		row.glyph = &G_WEB;
		row.name = c.name;
		row.note = hostOf(c.url);
		row.under = BG;
		row.focused = focused;
		row.pressed = pressed;
		ui.kit->row(r, row);
	}

	void Ui::Books::Catalogs::drawForm(const Player&)
	{
		Kit& k = *ui.kit;
		int n = formRows();
		for (int i = 0; i < n; i++)
		{
			int id = formRowId(i);
			Kit::FormField f;
			f.focused = fCursor == i;
			f.pressed = pressed == i && !touchMoved;
			switch (id)
			{
			case 0:
				f.label = "Address";
				f.value = fAddress;
				f.placeholder = "http://computer:8080/opds";
				// a Kavita key in the address is never shown
				if (size_t at = lowerAscii(fAddress).find("/api/opds/"); at != std::string::npos && fAddress.size() > at + 10)
					f.value = fAddress.substr(0, at + 10) + "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2";
				break;
			case 1: f.label = "Name"; f.value = fName; f.placeholder = "Filled from the catalog"; break;
			case 2:
				f.label = "Sign in";
				f.value = std::string("\xE2\x97\x80 ") + (fAccount ? "An account" : "None") + " \xE2\x96\xB6";
				break;
			case 3: f.label = "User"; f.value = fUser; f.placeholder = "User name"; break;
			case 4:
				f.label = "Password";
				if (fPasswordTyped) f.value = std::string(std::min<size_t>(fPassword.size(), 12), '*');
				else if (!formId.empty() && store.find(formId) && store.find(formId)->hasPassword()) f.value = "Saved";
				f.placeholder = "None";
				f.secret = fPasswordTyped && !fPassword.empty();
				break;
			default: f.label = "Copy from Manga\xE2\x80\x99s servers\xE2\x80\xA6"; f.value = "\xE2\x96\xB6"; break;
			}
			if (id >= 6)
			{
				std::vector<FoundCatalog> o = offered();
				if (id - 6 < (int)o.size())
				{
					if (id == 6)
					{
						Rect r = formRowRect(i);
						ui.drawText("Found on this network", F_I12, DIM, r.x + lay::SP4, r.y - FOUND_LABEL_H + 2);
					}
					drawFoundRow(formRowRect(i), o[id - 6], f.focused, f.pressed);
				}
				continue;
			}
			k.formRow(lay::formRow(formRowRect(i)), f);
		}
		if (formId.empty() && finding && offered().empty())
		{
			Rect r = formRowRect(formBase() - 1);
			ui.drawText("Looking on this network\xE2\x80\xA6", F_I12, DIM, r.x + lay::SP4, r.y + r.h + 6);
		}
		Kit::Key test;
		test.glyph = &G_REFRESH;
		test.label = "Test";
		test.key = "Y";
		test.focused = fCursor == n;
		test.pressed = pressed == n && !touchMoved;
		k.key(formRowRect(n), test);
		Kit::Key save;
		save.glyph = &G_CHECK;
		save.label = "Save";
		save.key = "START";
		save.tone = Kit::Tone::Primary;
		save.focused = fCursor == n + 1;
		save.pressed = pressed == n + 1 && !touchMoved;
		k.key(formRowRect(n + 1), save);
		Rect keys = formRowRect(n);
		std::string line = !fNote.empty() ? fNote : !fTest.empty() ? fTest : "A password is sealed on Save and never shown again.";
		ui.drawTextMid(line, F_I12, !fNote.empty() ? DANGER : fTestOk ? ACCENT : DIM, keys.x, keys.y - 14, body().w);
	}

	void Ui::Books::Catalogs::drawFeed(const Player& p)
	{
		Kit& k = *ui.kit;
		Feed& f = feeds.back();
		Rect a = listArea();
		const MangaServer* s = openCatalog();
		std::string name = s ? s->name : "the catalog";
		if (f.loading)
		{
			ui.drawTextMid((f.search ? "Searching " : "Opening ") + name + "\xE2\x80\xA6", F_I14, DIM, a.x, a.y + 40, a.w);
			return;
		}
		if (f.error != E_NONE || f.feed.entries.empty())
		{
			// cat-offline: the page says what failed, A tries again, Y opens the Shelf
			std::string title = f.error == E_REACH ? "Can\xE2\x80\x99t reach " + name : f.error == E_NOT_CATALOG ? "Not a catalog"
				: f.error == E_SIGN_IN ? "Not signed in" : f.search ? "Nothing found for " + f.title : "Nothing here";
			// no network at all: that is the first line, curl's reason stays beneath it
			if (f.error == E_REACH && ui.wifiOff()) title = "No Wi-Fi";
			std::string why = f.error == E_NONE ? (f.search ? "The catalog's own search: what it matches is its choice."
				: "This folder of the catalog is empty.") : f.why + ".";
			std::vector<std::string> lines = ui.wrapText(why, F_I14, 400, 3);
			lay::EmptyState e = lay::emptyState(a, (int)lines.size(), 2);
			Kit::Empty em;
			em.glyph = f.error == E_NONE ? &G_SEARCH : &G_WIFI;
			em.title = title;
			em.lines = lines;
			em.key1 = f.error == E_NONE && f.search ? "Search again" : "Try again";
			em.glyph1 = &G_REFRESH;
			em.key2 = "Open the Shelf";
			em.glyph2 = &G_BOOK;
			em.pressed1 = pressed == 100 && !touchMoved;
			em.pressed2 = pressed == 101 && !touchMoved;
			em.hotkeys = p.settings.buttonHotkeys;
			k.emptyState(e, em);
			return;
		}
		const bool books = f.books(), searchRow = !books && canSearch() && !f.search && feeds.size() == 1;
		const int cols = feedCols(), items = (int)f.feed.entries.size() + (searchRow ? 1 : 0);
		const int rows = (items + cols - 1) / cols;
		const int cur = f.cursor + (searchRow ? 1 : 0);
		SmoothList::Spec spec;
		spec.view = a;
		spec.rowH = feedRowH();
		spec.count = feedCount();
		spec.refresh = [this](Player&) { if (!feeds.empty()) loadFeed((int)feeds.size() - 1); };
		auto entryAt = [this, searchRow](int i) -> const OpdsEntry* {
			int e = i - (searchRow ? 1 : 0);
			const Feed& ff = feeds.back();
			return e >= 0 && e < (int)ff.feed.entries.size() ? &ff.feed.entries[e] : nullptr;
		};
		// a book's mark: on the card, or its download's share
		auto markOf = [this](const OpdsEntry& e, double& share) {
			share = -1;
			if (dl.active && dl.entryId == e.id && dl.catalogId == openId)
			{
				share = dl.length > 0 ? std::min(0.99, (double)dl.bytes / dl.length) : 0;
				return std::string();
			}
			if (!onCard(e.id).empty()) return std::string("On card");
			if (e.bookOffers().empty()) return std::string("Can\xE2\x80\x99t open");
			return std::string();
		};
		const std::string gen = std::string(theme::drawingClean() ? "c" : "") + std::to_string(cols) + "|" +
			std::to_string(dl.active ? dl.bytes * 100 / std::max(1LL, dl.length) : -1) + "|";
		// a book's picture file and its state at the cover's size (the key asks too: it runs for each row on screen
		// each frame, so only those are fetched)
		const std::string feedUrl = f.url;
		const bool onGrid = books && cols > 1;
		auto coverRect = [this](int inRow, int c, bool share) {
			std::vector<lay::Box> cells = lay::coverCells({lay::SP6, lay::SP6, listArea().w - 2 * lay::SP6, lay::SHELF_CELL_H},
				inRow, feedCols(), lay::SHELF_CELL_H);
			return lay::coverTile(cells[c], true, share).cover;
		};
		spec.key = [this, rows, cols, items, cur, entryAt, markOf, gen, searchRow, feedUrl, onGrid, coverRect](int r) {
			if (r >= rows) return std::string("foot") + (feeds.back().loadingMore ? "1" : "0");
			std::string key = "cat" + gen + std::to_string(r);
			for (int c = 0; c < cols; c++)
			{
				int i = r * cols + c;
				if (i >= items) break;
				const OpdsEntry* e = entryAt(i);
				double share;
				std::string mark = e ? markOf(*e, share) : "";
				std::string pic;
				if (e && onGrid)
				{
					std::string file = coverFile(*e, feedUrl, false);
					pic = file.empty() ? "-" : std::to_string(ui.catalogCoverState(file, coverRect(std::clamp(items - r * cols, 0, cols), c, share >= 0), 5));
				}
				key += "." + (e ? e->id + e->title : std::string("search")) + mark + pic + std::to_string((int)(share * 100)) +
					(i == cur ? "f" : "") + (pressed == i && !touchMoved ? "d" : "");
			}
			(void)searchRow;
			return key;
		};
		spec.draw = [this, rows, cols, items, cur, entryAt, markOf, books, searchRow, feedUrl](int r, Rect box) {
			Kit& kk = *ui.kit;
			if (r >= rows)
			{
				ui.drawTextMid(feeds.back().loadingMore ? "Loading the next page\xE2\x80\xA6" : "More below", F_I12, DIM,
					box.x, box.y + box.h / 2, box.w, 2);
				return;
			}
			if (cols > 1)
			{
				int inRow = std::clamp(items - r * cols, 0, cols);
				std::vector<lay::Box> cells = lay::coverCells({box.x + lay::SP6, box.y + lay::SP6, box.w - 2 * lay::SP6,
					lay::SHELF_CELL_H}, inRow, cols, lay::SHELF_CELL_H);
				for (int c = 0; c < (int)cells.size(); c++)
				{
					int i = r * cols + c;
					const OpdsEntry* e = entryAt(i);
					if (!e) continue;
					double share;
					std::string mark = markOf(*e, share);
					Kit::Cover cv;
					cv.key = e->title;
					cv.title = e->title;
					std::vector<OpdsLink> o = e->bookOffers();
					std::string fmtName = o.empty() ? (e->formatNames().empty() ? "" : e->formatNames().front())
						: opdsFormatName(o.front().type, o.front().href);
					cv.author = e->author;
					cv.fmt = fmtName;
					cv.badge = !mark.empty() ? mark : fmtName != "EPUB" ? fmtName : "";
					std::string file = coverFile(*e, feedUrl, false);
					if (!file.empty())
						cv.picture = [this, file, t = e->title, a = e->author, fmtName](Rect cr) { ui.drawCatalogCover(file, t, a, fmtName, cr, 5); };
					cv.progress = share;
					cv.focused = i == cur;
					cv.pressed = pressed == i && !touchMoved;
					kk.coverTile(lay::coverTile(cells[c], true, share >= 0), cv);
				}
				return;
			}
			if (searchRow && r == 0)
			{
				Kit::Row sr;
				sr.glyph = &G_SEARCH;
				sr.name = "Search this catalog";
				sr.key = "Y";
				sr.under = BG;
				sr.focused = cur == 0;
				sr.pressed = pressed == 0 && !touchMoved;
				kk.row({box.x, box.y, box.w, NAV_ROW_H - lay::SP2}, sr);
				return;
			}
			const OpdsEntry* e = entryAt(r);
			if (!e) return;
			if (!books)
			{
				Kit::Row nr;
				nr.glyph = e->isBook() ? &G_BOOK : &G_FOLDER;
				nr.name = e->title;
				int count = -1;
				for (const OpdsLink& l : e->links)
					if (l.count >= 0) count = l.count;
				nr.note = count >= 0 ? std::to_string(count) : e->summary.size() < 28 ? e->summary : "";
				nr.trail = Kit::Trail::Chevron;
				nr.under = BG;
				nr.focused = r == cur;
				nr.pressed = pressed == r && !touchMoved;
				kk.row({box.x, box.y, box.w, NAV_ROW_H - lay::SP2}, nr);
				return;
			}
			double share;
			std::string mark = markOf(*e, share);
			Kit::Media m;
			// the drawn book cover at 32 x 46 in the art's place (the spec's two-line row)
			m.art = [this, title = e->title](Rect r) {
				ui.kit->bookPlaceholder({r.x + (r.w - 32) / 2, r.y + r.h / 2 - 23, 32, 46}, title, 3);
			};
			std::vector<std::string> names = e->formatNames();
			std::string fm;
			for (size_t j = 0; j < names.size(); j++) fm += (j ? ", " : "") + names[j];
			m.name = e->title;
			m.second = e->author.empty() ? fm : fm.empty() ? e->author : e->author + " \xC2\xB7 " + fm;
			m.pill = mark;
			m.tone = mark == "On card" ? Kit::Pill::Live : Kit::Pill::Fact;
			if (share >= 0) m.note = std::to_string((int)(share * 100)) + "%";
			m.focused = r == cur;
			m.pressed = pressed == r && !touchMoved;
			kk.mediaRow(lay::mediaRow({box.x, box.y, box.w, BOOK_ROW_H - lay::SP2}), m);
		};
		if (list.takeFresh()) list.reveal(std::max(0, cur) / cols, a, spec.rowH, spec.count, false);
		coverOnScreen.clear();
		list.draw(std::move(spec));
		coverCancelOffScreen();
	}

	void Ui::Books::Catalogs::drawBook(const Player&)
	{
		Kit& k = *ui.kit;
		BookLayout l = bookLayout();
		const MangaServer* s = openCatalog();
		std::string have = onCard(book.id);
		bool getting = dl.active && dl.entryId == book.id && dl.catalogId == openId;
		bool none = offers.empty();
		{
			std::vector<std::string> fns = book.formatNames();
			std::string file = coverFile(book, bookBase, true);
			if (file.empty()) k.bookPlaceholder(l.cover, book.title, 5, book.author, fns.empty() ? "" : fns[0]);
			else ui.drawCatalogCover(file, book.title, book.author, fns.empty() ? "" : fns[0], l.cover, 5);
		}
		if (!have.empty()) k.pill(l.cover.x + 6 + k.pillW("On card"), l.cover.y + l.cover.h - 14, "On card", Kit::Pill::Live);
		ui.drawTextMid(book.author.empty() ? "Unknown author" : book.author, F_P16, TEXT, l.author.x, l.author.y + l.author.h / 2, l.author.w);
		std::string facts;
		auto add = [&facts](const std::string& s) { if (!s.empty()) facts += (facts.empty() ? "" : " \xC2\xB7 ") + s; };
		add(yearOf(book.issued));
		add(book.language);
		std::string tags;
		for (size_t i = 0; i < book.tags.size() && i < 3; i++) tags += (i ? ", " : "") + book.tags[i];
		add(tags);
		ui.drawTextMid(facts, F_I12, DIM, l.facts.x, l.facts.y + l.facts.h / 2, l.facts.w);
		std::vector<std::string> lines = ui.wrapText(book.summary.empty() ? "No summary." : book.summary, F_I14, l.summary.w, 4);
		for (size_t i = 0; i < lines.size(); i++)
			ui.drawTextMid(lines[i], F_I14, DIM_ON, l.summary.x, l.summary.y + 10 + (int)i * 20, l.summary.w);
		// the formats, only when there are several (or, read nowhere, the ones it has at 45%)
		std::vector<Kit::FilterItem> items;
		if (!none)
			for (const OpdsLink& o : offers)
				items.push_back({opdsFormatName(o.type, o.href) + (o.length > 0 ? " \xC2\xB7 " + mb(o.length) : "")});
		if (items.size() > 1)
			k.filters(lay::filterChips(l.chips, k.filterWidths(items)), items, fmt, getting ? -1 : fmt,
				pressed >= 200 ? pressed - 200 : -1);
		std::string fmtName = none ? "" : opdsFormatName(offers[fmt].type, offers[fmt].href);
		if (getting)
		{
			// cat-book-get: the key becomes the bar; Cancel is Y
			double share = dl.length > 0 ? std::min(0.99, (double)dl.bytes / dl.length) : -1;
			std::string right = dl.length > 0 ? mb(dl.bytes) + " of " + mb(dl.length) + " \xC2\xB7 " + std::to_string((int)(share * 100)) + "%"
				: mb(dl.bytes);
			ui.drawTextMid("Downloading the " + dl.fmtName + "\xE2\x80\xA6", F_I12, DIM_ON, l.key.x, l.key.y + 10, l.key.w);
			ui.drawTextMid(right, F_I12, DIM_ON, l.key.x + l.key.w, l.key.y + 10, 0, 1);
			k.bar({l.key.x, l.key.y + 26, l.key.w, 12}, share >= 0 ? share : 0.5);
			ui.drawTextMid("Y cancels the download \xC2\xB7 B leaves it running", F_I12, DIM, l.under.x, l.under.y + 10, l.under.w);
			return;
		}
		Kit::Key key;
		key.glyph = !have.empty() ? &G_BOOK : &G_DOWNLOAD;
		key.label = !have.empty() ? "Read" : none ? "Download" : "Download the " + fmtName;
		key.key = none ? "" : "A";
		key.tone = none ? Kit::Tone::Normal : Kit::Tone::Primary;
		key.off = none;
		key.pressed = pressed == 100 && !touchMoved;
		k.key(l.key, key);
		std::string under;
		if (!have.empty()) under = "On the Shelf \xC2\xB7 " + sourceLabel(dirOfPath(have)) + " / " + nameOfPath(have);
		else if (none)
		{
			std::vector<std::string> names = book.formatNames();
			std::string has;
			for (size_t i = 0; i < names.size(); i++) has += (i ? ", " : "") + names[i];
			under = (has.empty() ? "It can't be downloaded here. " : "It comes as " + has + ". ") +
				"Banana Player reads EPUB, PDF, TXT, CBZ and CBR; Calibre converts it to EPUB.";
		}
		else if (s)
		{
			long long room = -1;
			std::string dir = savesTo(*s, &room);
			under = dir.empty() ? "No card to save it on" : "Saves to " + sourceLabel(dirOfPath(dir)) + " / " +
				nameOfPath(dir) + (room >= 0 ? " \xC2\xB7 " + gb(room) + " free" : "");
		}
		std::vector<std::string> ul = ui.wrapText(under, F_I12, l.under.w, 2);
		for (size_t i = 0; i < ul.size(); i++)
			ui.drawTextMid(ul[i], F_I12, DIM, l.under.x, l.under.y + 10 + (int)i * 18, l.under.w);
	}

	// a book's pane (the book page's and a grid's highlighted book): its picture when fetched, else the cover drawn from
	// the title, the author, the title and the whole summary
	void Ui::Books::Catalogs::bookPane(PaneSpec& out, const OpdsEntry& e, const std::string& base)
	{
		std::vector<std::string> names = e.formatNames();
		std::string have = onCard(e.id);
		out.cover = true;
		std::string file = coverFile(e, base, true);
		if (!file.empty() && ui.mediaArt) out.coverArt = ui.mediaArt->paneCover(file, true);
		if (!out.coverArt)
		{
			out.genTitle = e.title;
			out.genAuthor = e.author;
			out.genFmt = names.empty() ? "" : names[0];
		}
		out.label = e.author.empty() ? "Book" : e.author;
		out.title = e.title;
		out.text = e.summary.empty() ? "No summary." : e.summary;
		out.wholeText = true;
		out.keys = {{"A", have.empty() ? "Download" : "Read"}, {"X", "More"}};
		out.key = "catbook|" + e.id + "|" + e.title + "|" + std::to_string(e.summary.size()) + "|" + (have.empty() ? "" : "got") +
			(out.coverArt ? "|art" : "");
	}

	// the other views: a label, a title, a sentence (the catalog list's and a folder's; the spec's `top:` of each screen)
	static void catInfoPane(PaneSpec& out, const std::string& key, const std::string& label, const std::string& title,
		const std::string& text, std::vector<std::pair<std::string, std::string>> rows = {},
		std::vector<std::pair<std::string, std::string>> keys = {})
	{
		out.label = label;
		out.title = title;
		out.text = text;
		out.rows = std::move(rows);
		out.keys = std::move(keys);
		out.key = key + "|" + label + "|" + title + "|" + text;
		for (const auto& r : out.rows) out.key += "|" + r.first + r.second;
	}

	bool Ui::Books::Catalogs::topPane(PaneSpec& out)
	{
		if (!loaded) load();	// the pane can be asked before the tab's first draw
		const MangaServer* s = openCatalog();
		const std::string name = s ? s->name : "Catalog";
		if (view() == V_BOOK)
		{
			bookPane(out, book, bookBase);
			return true;
		}
		if (view() == V_FORM)
		{
			catInfoPane(out, "catform", "Books \xC2\xB7 Catalogs", formId.empty() ? "Add a catalog" : "Edit catalog",
				"Where the catalog answers, as its app shows it. Test says at once whether it is one.");
			return true;
		}
		if (view() == V_FEED)
		{
			const Feed& f = feeds.back();
			if (f.loading)
			{
				catInfoPane(out, "catload", name, f.search ? "Searching\xE2\x80\xA6" : "Opening\xE2\x80\xA6", f.title);
				return true;
			}
			if (f.error != E_NONE || f.feed.entries.empty())
			{
				std::string line = f.error == E_REACH ? "Can\xE2\x80\x99t reach it" : f.error == E_NOT_CATALOG ? "Not a catalog"
					: f.error == E_SIGN_IN ? "Not signed in" : f.search ? "Nothing found" : "Nothing here";
				catInfoPane(out, "caterr", name, line, f.error == E_NONE ? f.title : f.why);
				out.labelDanger = f.error != E_NONE;
				return true;
			}
			const bool searchRow = !f.books() && canSearch() && !f.search && feeds.size() == 1;
			if (f.cursor < 0 || f.cursor >= (int)f.feed.entries.size())
			{
				catInfoPane(out, "catsearch", name, searchRow ? "Search" : f.title,
					searchRow ? "Find a book by its title or author, with the catalog's own search." : "");
				return true;
			}
			const OpdsEntry& e = f.feed.entries[f.cursor];
			if (e.isBook())
			{
				bookPane(out, e, f.url);
				return true;
			}
			int n = -1;
			for (const OpdsLink& l : e.links)
				if (l.count >= 0) n = l.count;
			std::vector<std::pair<std::string, std::string>> rows;
			if (n >= 0) rows.push_back({"Books", std::to_string(n)});
			catInfoPane(out, "catfolder", name, e.title, e.summary.empty() ? "A folder of the catalog." : e.summary, rows);
			return true;
		}
		// the catalog list
		std::vector<MangaServer> v = saved();
		if (v.empty() || listCursor >= (int)v.size())
		{
			std::vector<FoundCatalog> offer = offered();
			if (foundCursor < 0 || foundCursor >= (int)offer.size() || !v.empty())
			{
				catInfoPane(out, "catadd", "Catalogs", v.empty() ? "Books from a catalog" : "Add a catalog",
					"A catalog is a library on your network or the web (Calibre, Calibre-Web, COPS, Kavita, Project Gutenberg). "
					"Its books are listed here and saved to Books on the card.");
				return true;
			}
			const FoundCatalog& c = offer[foundCursor];
			catInfoPane(out, "catfound", "Found on this network", c.name,
				"A catalog announcing itself on this network. A opens the Add form with its address filled in.",
				{{"Address", hostOf(c.url)}});
			return true;
		}
		const MangaServer& c = v[listCursor];
		catInfoPane(out, "catsaved" + c.id, "Catalog", c.name, "",
			{{"Address", hostOf(c.url)},
			 {"Sign-in", c.user.empty() ? (c.kind == "kavita" ? "Key saved" : "None") : c.user},
			 {"On the card", std::to_string(got(c.id).count()) + " books"}},
			{{"A", "Open"}, {"X", "More"}});
		return true;
	}

	std::string Ui::Books::Catalogs::signature() const
	{
		std::string s = "|cat" + std::to_string((int)view()) + "." + std::to_string(listCursor) + "." + std::to_string(pressed) +
			std::to_string(touchMoved) + "." + std::to_string(store.version()) + "." + std::to_string(listView);
		if (finding) s += "|find";
		if (formOpen ? formId.empty() : view() == V_LIST)
		{
			s += "|fd" + std::to_string(foundCursor);
			for (const FoundCatalog& c : offered()) s += c.url + "\x1F";
		}
		if (dl.active) s += "|dl" + dl.entryId + std::to_string(dl.bytes / 16384);
		if (formOpen)
			s += "|f" + std::to_string(fCursor) + fAddress + "\x1F" + fName + "\x1F" + fUser + "\x1F" + std::to_string(fPassword.size()) +
				std::to_string(fAccount) + std::to_string(fPasswordTyped) + fTest + fNote;
		for (const Feed& f : feeds)
			s += "|" + std::to_string(f.loading) + std::to_string(f.loadingMore) + std::to_string((int)f.error) + "." +
				std::to_string(f.feed.entries.size()) + "." + std::to_string(f.cursor);
		if (bookOpen) s += "|b" + book.id + "." + std::to_string(fmt) + onCard(book.id);
		s += "|cv" + std::to_string(coverSerial) + "." + std::to_string(ui.artSerial);
		return s;
	}
}
