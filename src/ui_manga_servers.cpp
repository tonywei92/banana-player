#include "ui.h"
#include "manganet.h"
#include "mangaremote.h"
#include "manga_ui_logic.h"
#include "ui_internal.h"
#include "ui_reader.h"
#include "ui_manga.h"
#include "ui_kit.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace bp
{
	// ---- the saved servers: Browse › Y (Add server), a server's libraries or catalogue, a series and its chapters ----
	//
	// The parsers and the request builders are bpcore's (mangaremote.h, opds.h); this file only fetches through
	// MangaNet and shows what came back. Each answer carries a serial: one whose serial is no longer the one here (a
	// page moved on, another list was asked for) is dropped, as Browse does with the worker's answers. A typed
	// password is never sent in a url, shown on screen after Save or written to a message: MangaServerStore seals it.
	// Kavita's api key is kept the same way — the store holds its address with {key} where Kavita prints the key, the
	// key goes back only into a request, and every answer's body (and the url it came from) has the key put back to
	// {key} before it is read, so no id, href or page url kept anywhere carries it.

	namespace
	{
		const char* const SRV_KIND_IDS[3] = {"komga", "kavita", "opds"};
		const char* const SRV_KIND_NAMES[3] = {"Komga", "Kavita", "OPDS"};
		constexpr int SRV_FORM_KIND = 0, SRV_FORM_NAME = 1, SRV_FORM_URL = 2, SRV_FORM_USER = 3, SRV_FORM_PASS = 4,
			SRV_FORM_TEST = 5, SRV_FORM_SAVE = 6;

		int kindIndex(const std::string& kind)
		{
			for (int i = 0; i < 3; i++)
				if (kind == SRV_KIND_IDS[i]) return i;
			return 0;
		}

		const char* kindName(const std::string& kind) { return SRV_KIND_NAMES[kindIndex(kind)]; }

		// "Chapter 12", "-" or "Chapter": what a row says when the server named no title
		std::string chapterLabel(const RemoteChapter& c)
		{
			if (!c.name.empty()) return c.name;
			if (c.number > 0)
			{
				char buf[48];
				snprintf(buf, sizeof buf, "Chapter %g", c.number);
				return buf;
			}
			return "Chapter";
		}

		// a typed password as the form draws it: never the text itself
		std::string passwordBullets(const std::string& pw)
		{
			if (pw.empty()) return "None";
			return std::string(std::min<size_t>(pw.size(), 12), '*');
		}

		std::string bodyOf(const MangaFetch::Result& r) { return std::string(r.body.begin(), r.body.end()); }

		// What a request's answer means to the reader: the fetch's own error wins (it says why), else the status.
		bool answerOk(const MangaFetch::Result& r)
		{
			return r.error.empty() && r.status >= 200 && r.status < 300;
		}

		std::string answerError(const MangaFetch::Result& r)
		{
			if (!r.error.empty()) return "The server didn\u2019t answer";
			if (r.status == 401 || r.status == 403) return "The server refused the sign-in";
			if (r.status == 404) return "Nothing answered at that address";
			if (r.status == 0) return "The server didn\u2019t answer";
			return "The server answered HTTP " + std::to_string(r.status);
		}

		// The server a form describes, with a Kavita address read the way the store reads it: {key} in the url and the
		// key it carried handed back. Test and Save then ask about exactly what would be kept.
		MangaServer mangaFormServer(const std::string& kind, const std::string& name, const std::string& url,
			const std::string& user, std::string& urlKey)
		{
			MangaServer s;
			s.kind = kind;
			s.name = name;
			s.url = url;
			s.user = user;
			urlKey.clear();
			if (kind == "kavita")
			{
				std::string stored, key;
				if (readKavitaUrl(s.url, stored, key))
				{
					s.url = stored;
					urlKey = key;
				}
			}
			return s;
		}

		// the key a Kavita form asks with: the typed one, else the one its address carried, else the saved one
		std::string mangaFormKavitaKey(const MangaServerStore& store, const std::string& formId, bool typed,
			const std::string& typedKey, const std::string& urlKey)
		{
			if (typed) return typedKey;
			if (!urlKey.empty()) return urlKey;
			return formId.empty() ? std::string() : store.password(formId);
		}
	}

	// ---- the pages' state ----

	void Ui::Manga::mangaServersReset()
	{
		mangaServerCancel();
		mangaSrvView = MGSRV_NONE;
		mangaSrvMenu = false;
		mangaSrvDeleteArmed = false;
		mangaSrvMenuId.clear();
		mangaSrvMenuCursor = 0;
		mangaSrvId.clear();
		mangaSrvName.clear();
		mangaSrvKind.clear();
		mangaSrvRootUrl.clear();
		mangaSrvHere.clear();
		mangaSrvPath.clear();
		mangaSrvItems.clear();
		mangaSrvCursor = mangaSrvScroll = mangaSrvPage = 0;
		mangaSrvMore = mangaSrvLoading = mangaSrvAppend = false;
		mangaSrvNext.clear();
		mangaSrvError.clear();
		mangaSrvLoad = 0;
		mangaSrvLoadUrl.clear();
		mangaSrvSeriesId.clear();
		mangaSrvSeriesKey.clear();
		mangaSrvSeriesTitle.clear();
		mangaSrvChapters.clear();
		mangaSrvChaptersOn = false;
		mangaSrvSerCursor = mangaSrvSerScroll = mangaSrvSerPage = 0;
		mangaSrvSerMore = mangaSrvSerLoading = mangaSrvSerAppend = false;
		mangaSrvSerError.clear();
		mangaSrvSeriesFrom = MGSRV_FROM_LIST;
		mangaSrvFormId.clear();
		mangaSrvFormKind = "komga";
		mangaSrvFormName.clear();
		mangaSrvFormUrl = "https://";
		mangaSrvFormUser.clear();
		mangaSrvFormPassword.clear();
		mangaSrvFormPasswordTyped = false;
		mangaSrvFormCursor = 0;
		mangaSrvFormNote.clear();
		mangaSrvTestText.clear();
		mangaView = 0;
		mangaSrvLocal++;
	}

	void Ui::Manga::mangaServerCancel()
	{
		if (mangaNet && mangaNet->started())
		{
			if (mangaSrvFetchSerial) mangaNet->fetchApi().cancel(mangaSrvFetchSerial);
			if (mangaSrvSerSerial) mangaNet->fetchApi().cancel(mangaSrvSerSerial);
			if (mangaSrvTestSerial) mangaNet->fetchApi().cancel(mangaSrvTestSerial);
		}
		mangaSrvFetchSerial = mangaSrvSerSerial = mangaSrvTestSerial = 0;
		mangaSrvLoading = mangaSrvSerLoading = false;
		mangaCoverCancelAll();
	}

	// ---- the answers ----

	void Ui::Manga::mangaServersPoll()
	{
		if (!mangaNet || !mangaNet->started()) return;
		MangaServerStore& store = mangaNet->servers();
		for (MangaFetch::Result& r : mangaNet->takeFetched())
		{
			if (mangaSrvTestSerial && r.id == mangaSrvTestSerial)
			{
				// Test: the address, the sign-in and a catalogue we can read, never the password
				mangaSrvTestSerial = 0;
				mangaSrvLocal++;
				if (!answerOk(r)) { mangaSrvTestText = answerError(r); continue; }
				std::string urlKey;
				MangaServer fs = mangaFormServer(mangaSrvFormKind, trimmed(mangaSrvFormName), mangaSrvFormUrl,
					trimmed(mangaSrvFormUser), urlKey);
				std::string key = mangaFormKavitaKey(store, mangaSrvFormId, mangaSrvFormPasswordTyped, mangaSrvFormPassword,
					urlKey);
				std::string body = remoteWithoutKey(fs, key, bodyOf(r));
				RemoteList list;
				std::string base = r.finalUrl.empty() ? mangaSrvLoadUrl : r.finalUrl;
				base = remoteWithoutKey(fs, key, base);
				bool parsed = mangaSrvFormKind == "komga" ? remoteKomgaLibraryList(body, list)
					: remoteOpdsList(base, body, list);
				if (!parsed) mangaSrvTestText = "The server answered, but not like a catalogue";
				else if (!list.items.empty())
					mangaSrvTestText = "Connected \xC2\xB7 " + std::to_string(list.items.size()) +
						(list.items.size() == 1 ? " entry" : " entries");
				else mangaSrvTestText = "Connected";
				continue;
			}
			if (mangaSrvFetchSerial && r.id == mangaSrvFetchSerial)
			{
				mangaSrvFetchSerial = 0;
				mangaSrvLoading = false;
				mangaSrvLocal++;
				if (!answerOk(r))
				{
					mangaSrvError = answerError(r);
					mangaSrvAppend = false;
					continue;
				}
				const MangaServer* sv = store.find(mangaSrvId);
				std::string pw = store.password(mangaSrvId);
				std::string body = bodyOf(r);
				std::string base = r.finalUrl.empty() ? mangaSrvLoadUrl : r.finalUrl;
				if (sv)
				{
					// a Kavita answer repeats the key it was asked with, in its body and in the url it came from
					body = remoteWithoutKey(*sv, pw, body);
					base = remoteWithoutKey(*sv, pw, base);
				}
				RemoteList list;
				bool parsed = false;
				if (mangaSrvLoad == MGSRV_LOAD_LIBRARIES) parsed = remoteKomgaLibraryList(body, list);
				else if (mangaSrvLoad == MGSRV_LOAD_SERIES)
					// covers are absolute urls off the server's own url, as stored
					parsed = remoteKomgaSeriesList(sv ? sv->url : "", body, mangaSrvPage, list);
				else parsed = remoteOpdsList(base, body, list);
				if (!parsed)
				{
					mangaSrvError = "The server\xE2\x80\x99s catalogue couldn\xE2\x80\x99t be read";
					mangaSrvAppend = false;
					continue;
				}
				if (mangaSrvAppend) mangaSrvItems.insert(mangaSrvItems.end(), list.items.begin(), list.items.end());
				else
				{
					mangaSrvItems = list.items;
					mangaSrvCursor = mangaSrvScroll = 0;
				}
				bool appended = mangaSrvAppend;
				mangaSrvAppend = false;
				mangaSrvMore = list.more;
				mangaSrvNext = list.next;
				mangaSrvError.clear();
				// a feed of chapters is a series' own page: a series folder in an OPDS catalogue opens straight to
				// its chapters, rather than a grid of its chapters to tap again (its first page only)
				if (!appended && mangaSrvLoad == MGSRV_LOAD_FEED && !mangaSrvPath.empty() && !mangaSrvItems.empty())
				{
					bool chapters = true;
					for (const RemoteItem& it : mangaSrvItems)
						if (it.folder) { chapters = false; break; }
					if (chapters)
					{
						std::string feed = mangaSrvPath.back();
						mangaServerSeriesOpen(feed, mangaSrvHere, MGSRV_FROM_FOLDER);
						continue;
					}
				}
				if (mangaSrvCursor >= (int)mangaSrvItems.size())
					mangaSrvCursor = std::max(0, (int)mangaSrvItems.size() - 1);
				continue;
			}
			if (mangaSrvSerSerial && r.id == mangaSrvSerSerial)
			{
				mangaSrvSerSerial = 0;
				mangaSrvSerLoading = false;
				mangaSrvLocal++;
				if (!answerOk(r))
				{
					mangaSrvSerError = answerError(r);
					mangaSrvSerAppend = false;
					continue;
				}
				const MangaServer* sv = store.find(mangaSrvId);
				std::string pw = store.password(mangaSrvId);
				std::string body = bodyOf(r);
				std::string base = r.finalUrl.empty() ? mangaSrvSeriesKey : r.finalUrl;
				if (sv)
				{
					body = remoteWithoutKey(*sv, pw, body);
					base = remoteWithoutKey(*sv, pw, base);
				}
				std::vector<RemoteChapter> chapters;
				bool more = false, parsed = false;
				if (mangaSrvKind == "komga")
					parsed = remoteKomgaChapterList(body, mangaSrvSerPage, chapters, more);
				else parsed = remoteOpdsChapters(base, body, chapters);
				if (!parsed)
				{
					mangaSrvSerError = "The server\xE2\x80\x99s chapters couldn\xE2\x80\x99t be read";
					mangaSrvSerAppend = false;
					continue;
				}
				if (mangaSrvSerAppend) mangaSrvChapters.insert(mangaSrvChapters.end(), chapters.begin(), chapters.end());
				else
				{
					mangaSrvChapters = chapters;
					mangaSrvSerCursor = mangaSrvSerScroll = 0;
				}
				mangaSrvSerAppend = false;
				mangaSrvSerMore = more;
				mangaSrvChaptersOn = !mangaSrvChapters.empty();
				mangaSrvSerError.clear();
				if (mangaSrvSerCursor >= (int)mangaSrvChapters.size())
					mangaSrvSerCursor = std::max(0, (int)mangaSrvChapters.size() - 1);
				continue;
			}
			// a cover fetch answer: the grid asked for it; if it is one of ours, it is processed and the loop
			// moves on (the catalogue handlers above don't see it)
			if (mangaCoverOnFetched(r)) continue;
			// an answer nothing here asks for any more (the page, the server or the list moved on): dropped
		}
	}

	// ---- a server: its libraries or catalogue ----

	void Ui::Manga::mangaServerOpen(const std::string& id, Player& p)
	{
		(void)p;
		if (!mangaNet || !mangaNet->started()) return;
		const MangaServer* s = mangaNet->servers().find(id);
		if (!s) return;
		mangaServerCancel();
		mangaSrvId = id;
		mangaSrvName = s->name;
		mangaSrvKind = s->kind;
		mangaSrvHere = s->name;
		mangaSrvRootUrl.clear();
		mangaSrvPath.clear();
		mangaSrvItems.clear();
		mangaSrvCursor = mangaSrvScroll = mangaSrvPage = 0;
		mangaSrvMore = mangaSrvLoading = mangaSrvAppend = false;
		mangaSrvNext.clear();
		mangaSrvError.clear();
		mangaSrvSeriesId.clear();
		mangaSrvSeriesKey.clear();
		mangaSrvSeriesTitle.clear();
		mangaSrvChapters.clear();
		mangaSrvChaptersOn = false;
		mangaSrvView = MGSRV_CATALOGUE;
		mangaView = 2;
		mangaSrvLocal++;
		if (s->kind == "komga") mangaServerListLoad(MGSRV_LOAD_LIBRARIES);
		else
		{
			// the OPDS start is the server's own url (a Komga server's is under /opds/v1.2). Kavita's is kept with
			// {key} where its api key was: it stays that way here, and only the request gets the key
			mangaSrvRootUrl = s->url;
			mangaServerFeedLoad(mangaSrvRootUrl);
		}
	}

	void Ui::Manga::mangaServerListLoad(int load)
	{
		mangaSrvError.clear();
		if (!mangaNet || !mangaNet->started())
		{
			mangaSrvLoading = false;
			mangaSrvError = "The reader\xE2\x80\x99s network side is off";
			return;
		}
		const MangaServer* s = mangaNet->servers().find(mangaSrvId);
		if (!s)
		{
			mangaSrvLoading = false;
			mangaSrvError = "This server isn\xE2\x80\x99t saved";
			return;
		}
		std::string pw = mangaNet->servers().password(mangaSrvId);
		RemoteRequest rq;
		if (load == MGSRV_LOAD_LIBRARIES) rq = remoteKomgaLibraries(*s, pw);
		else if (load == MGSRV_LOAD_SERIES)
		{
			if (mangaSrvPath.empty())
			{
				mangaSrvLoading = false;
				mangaSrvError = "No library is open";
				return;
			}
			rq = remoteKomgaSeries(*s, pw, mangaSrvPath.back(), mangaSrvPage);
		}
		else
		{
			mangaSrvLoading = false;
			return;
		}
		if (mangaSrvFetchSerial) mangaNet->fetchApi().cancel(mangaSrvFetchSerial);
		mangaSrvLoad = load;
		mangaSrvLoadUrl = rq.url;
		mangaSrvLoading = true;
		mangaSrvFetchSerial = mangaNet->fetchApi().get(rq.url, rq.headers, 8 * 1024 * 1024);
		if (!mangaSrvFetchSerial)
		{
			mangaSrvLoading = false;
			mangaSrvError = "Couldn\xE2\x80\x99t ask the server";
		}
		mangaSrvLocal++;
	}

	void Ui::Manga::mangaServerFeedLoad(const std::string& url)
	{
		mangaSrvError.clear();
		if (!mangaNet || !mangaNet->started())
		{
			mangaSrvLoading = false;
			mangaSrvError = "The reader\xE2\x80\x99s network side is off";
			return;
		}
		const MangaServer* s = mangaNet->servers().find(mangaSrvId);
		if (!s || url.empty())
		{
			mangaSrvLoading = false;
			mangaSrvError = "This server isn\xE2\x80\x99t saved";
			return;
		}
		RemoteRequest rq = remoteAuth(*s, mangaNet->servers().password(mangaSrvId), url);
		if (mangaSrvFetchSerial) mangaNet->fetchApi().cancel(mangaSrvFetchSerial);
		mangaSrvLoad = MGSRV_LOAD_FEED;
		// the url as kept (a Kavita one with {key}), not the request's own copy: the key never rests here
		mangaSrvLoadUrl = url;
		mangaSrvLoading = true;
		mangaSrvFetchSerial = mangaNet->fetchApi().get(rq.url, rq.headers, 8 * 1024 * 1024);
		if (!mangaSrvFetchSerial)
		{
			mangaSrvLoading = false;
			mangaSrvError = "Couldn\xE2\x80\x99t ask the server";
		}
		mangaSrvLocal++;
	}

	void Ui::Manga::mangaServerMore()
	{
		if (mangaSrvLoading || !mangaSrvMore) return;
		mangaSrvAppend = true;
		if (mangaSrvLoad == MGSRV_LOAD_SERIES)
		{
			mangaSrvPage++;
			mangaServerListLoad(MGSRV_LOAD_SERIES);
		}
		else if (mangaSrvLoad == MGSRV_LOAD_FEED && !mangaSrvNext.empty()) mangaServerFeedLoad(mangaSrvNext);
		else mangaSrvAppend = false;
	}

	void Ui::Manga::mangaServerReload()
	{
		// A after the error: the list that failed, from its first page
		mangaSrvItems.clear();
		mangaSrvCursor = mangaSrvScroll = mangaSrvPage = 0;
		mangaSrvMore = false;
		mangaSrvNext.clear();
		mangaSrvError.clear();
		if (mangaSrvKind == "komga")
			mangaServerListLoad(mangaSrvPath.empty() ? MGSRV_LOAD_LIBRARIES : MGSRV_LOAD_SERIES);
		else mangaServerFeedLoad(mangaSrvPath.empty() ? mangaSrvRootUrl : mangaSrvPath.back());
	}

	void Ui::Manga::mangaServerItemOpen(int i, Player& p)
	{
		if (i < 0 || i >= (int)mangaSrvItems.size()) return;
		const RemoteItem item = mangaSrvItems[i];
		if (item.folder)
		{
			if (mangaSrvKind != "komga")
			{
				mangaSrvPath.push_back(item.key);
				mangaSrvHere = item.title.empty() ? mangaSrvName : item.title;
				mangaSrvItems.clear();
				mangaSrvCursor = mangaSrvScroll = mangaSrvPage = 0;
				mangaSrvMore = mangaSrvAppend = false;
				mangaSrvNext.clear();
				mangaServerFeedLoad(item.key);
				return;
			}
			// a Komga library: its series, A–Z
			mangaSrvPath.push_back(item.key);
			mangaSrvHere = item.title.empty() ? mangaSrvName : item.title;
			mangaSrvItems.clear();
			mangaSrvCursor = mangaSrvScroll = mangaSrvPage = 0;
			mangaSrvMore = mangaSrvAppend = false;
			mangaSrvNext.clear();
			mangaServerListLoad(MGSRV_LOAD_SERIES);
			return;
		}
		(void)p;
		if (mangaSrvKind == "komga") mangaServerSeriesOpen(item.key, item.title, MGSRV_FROM_LIST);
		else
		{
			// an entry of an acquisition feed: this feed is its series' chapter list
			std::string feed = mangaSrvPath.empty() ? mangaSrvRootUrl : mangaSrvPath.back();
			mangaServerSeriesOpen(feed, mangaSrvHere, MGSRV_FROM_LIST);
		}
	}

	void Ui::Manga::mangaServerBack(Player& p)
	{
		(void)p;
		if (mangaSrvView == MGSRV_FORM)
		{
			mangaServerCancel();
			mangaSrvFormNote.clear();
			mangaSrvTestText.clear();
			mangaSrvFormPassword.clear();
			mangaSrvFormPasswordTyped = false;
			mangaSrvView = MGSRV_NONE;
			mangaView = 0;
			mangaSrvLocal++;
			return;
		}
		if (mangaSrvView != MGSRV_CATALOGUE) return;
		if (!mangaSrvPath.empty())
		{
			mangaSrvPath.pop_back();
			mangaSrvItems.clear();
			mangaSrvCursor = mangaSrvScroll = mangaSrvPage = 0;
			mangaSrvMore = mangaSrvAppend = false;
			mangaSrvNext.clear();
			mangaSrvError.clear();
			mangaSrvHere = mangaSrvName;
			if (mangaSrvKind == "komga")
				mangaServerListLoad(mangaSrvPath.empty() ? MGSRV_LOAD_LIBRARIES : MGSRV_LOAD_SERIES);
			else mangaServerFeedLoad(mangaSrvPath.empty() ? mangaSrvRootUrl : mangaSrvPath.back());
			return;
		}
		mangaServerCancel();
		mangaSrvView = MGSRV_NONE;
		mangaView = 0;
		mangaSrvPath.clear();
		mangaSrvItems.clear();
		mangaSrvHere.clear();
		mangaSrvLocal++;
	}

	bool Ui::Manga::mangaServersKey(Button b, Player& p)
	{
		mangaServersPoll();
		if (mangaSrvView == MGSRV_FORM) return mangaServerFormKey(b, p);
		if (ui.page == Page::MangaSeries || mangaSrvView == MGSRV_SERIES) return mangaServerSeriesKey(b, p);
		if (b == Button::B) { mangaServerBack(p); return false; }
		int n = (int)mangaSrvItems.size();
		int rows = (n + MG_COLS - 1) / MG_COLS;
		if (b == Button::A)
		{
			if (!mangaSrvError.empty()) mangaServerReload();
			else if (mangaSrvCursor >= 0 && mangaSrvCursor < n) mangaServerItemOpen(mangaSrvCursor, p);
			return false;
		}
		if (!n) return false;
		if (mangaSrvCursor >= n) mangaSrvCursor = n - 1;
		switch (b)
		{
		case Button::Up: if (mangaSrvCursor >= MG_COLS) mangaSrvCursor -= MG_COLS; break;
		case Button::Down: if (mangaSrvCursor + MG_COLS < n) mangaSrvCursor += MG_COLS; break;
		case Button::Left: if (mangaSrvCursor % MG_COLS > 0) mangaSrvCursor--; break;
		case Button::Right: if (mangaSrvCursor % MG_COLS + 1 < MG_COLS && mangaSrvCursor + 1 < n) mangaSrvCursor++; break;
		default: break;
		}
		mangaSrvScroll = std::max(0, mangaSrvCursor / MG_COLS - (MGSRV_GRID_ROWS_SHOWN - 1));
		// two rows before the end, the next page is asked for; one at a time
		if (mangaSrvMore && !mangaSrvLoading && rows > 0 && mangaSrvCursor / MG_COLS >= rows - 2) mangaServerMore();
		return false;
	}

	bool Ui::Manga::mangaServersPointer(const Pointer& pt, int x, int y, Player& p)
	{
		mangaServersPoll();
		if (pt.kind != Pointer::Down) return false;
		if (mangaSrvView == MGSRV_FORM)
		{
			for (int i = 0; i < MGSRV_FORM_ROWS; i++)
				if (mangaSrvFormRowRect(i).hit(x, y))
				{
					mangaSrvFormCursor = i;
					mangaServerFormChoose(p);
					return false;
				}
			return false;
		}
		// the grid (the kit's, as drawMangaServers laid it out): a drag scrolls it by rows (the next page asked near
		// the end, as the D-pad asks it), a tap opens the cover; an error in the empty grid: a tap is Try again
		if (!mangaSrvError.empty() && mangaSrvItems.empty() && mangaListArea(p).hit(x, y)) { mangaServerReload(); return false; }
		mangaGridPointer(mlSrvGrid, mangaListArea(p), x, y, [this] { return (int)mangaSrvItems.size(); }, &mangaSrvScroll, &mangaSrvCursor,
			[this, &p](int i) {
				if (i < 0 || i >= (int)mangaSrvItems.size()) return;
				mangaSrvCursor = i;
				mangaServerItemOpen(i, p);
			}, [this] {
				int rows = ((int)mangaSrvItems.size() + MG_COLS - 1) / MG_COLS;
				if (mangaSrvMore && !mangaSrvLoading && rows > 0 && mangaSrvScroll + MGSRV_GRID_ROWS_SHOWN >= rows - 1)
					mangaServerMore();
			});
		return false;
	}

	void Ui::Manga::drawMangaServers(const Player& p)
	{
		mangaServersPoll();
		if (mangaSrvView == MGSRV_FORM) { drawMangaServerForm(p); return; }
		if (ui.page == Page::MangaSeries || mangaSrvView == MGSRV_SERIES) { drawMangaServerSeries(p); return; }
		ui.drawHeader(mangaSrvHere.empty() ? (mangaSrvName.empty() ? "Server" : mangaSrvName) : mangaSrvHere, p);
		int n = (int)mangaSrvItems.size();
		if (n && mangaSrvCursor >= n) mangaSrvCursor = n - 1;
		// server cover headers: the server's auth (Basic or Kavita's API key in the URL) for every cover on this page
		std::vector<std::pair<std::string, std::string>> srvCoverHeaders;
		if (mangaNet && mangaNet->started())
		{
			const MangaServer* sv = mangaNet->servers().find(mangaSrvId);
			if (sv)
			{
				std::string pw = mangaNet->servers().password(mangaSrvId);
				srvCoverHeaders = remoteAuth(*sv, pw, "").headers;
				// a cover is a picture: the API's Accept (Komga's JSON) made its thumbnail answer 406
				for (auto& h : srvCoverHeaders)
					if (h.first == "Accept") h.second = "image/*";
				// Kavita's cover addresses carry {key} where its api key goes (put in only for the request)
				if (sv->kind == "kavita")
				{
					MangaServer copy = *sv;
					coverFetchUrl = [copy, pw](const std::string& u) { return remoteWithKey(copy, pw, u); };
				}
			}
		}
		// the catalogue as the kit's cover grid in the body (the place it is in, the header's line)
		lay::Box area = mangaListArea(p);
		mangaCoverOnScreen.clear();
		// a generated cover until the real one arrives; the grid never waits on a server
		drawMangaGrid(mlSrvGrid, area, n, mangaSrvScroll, mangaSrvCursor, [this, &srvCoverHeaders](int i) {
			GridCell c;
			c.title = mangaSrvItems[i].title;
			c.coverKey = coverReady("", mangaSrvItems[i].cover, &srvCoverHeaders);
			c.picture = [this, title = c.title, cover = mangaSrvItems[i].cover, &srvCoverHeaders](Rect r) {
				drawMangaCover(r, title, false, 4, "", cover, &srvCoverHeaders);
			};
			return c;
		});
		mangaCoverCancelOffScreen();
		coverFetchUrl = nullptr;
		tabCrumb = mangaSrvName + " \xC2\xB7 " + kindName(mangaSrvKind);
		if (!mangaSrvError.empty() && n) tabCrumb = mangaSrvError;
		else if (mangaSrvLoading && n) tabCrumb = "Loading\xE2\x80\xA6";
		if (n == 0 && !mangaSrvError.empty()) drawMangaTabEmpty(p, G_LAPTOP, "Couldn\xE2\x80\x99t reach it", mangaSrvError + ". A: try again");
		else if (n == 0 && mangaSrvLoading) ui.drawLoader(area.x + area.w / 2, area.y + area.h / 2, "Loading\xE2\x80\xA6");
		else if (n == 0) drawMangaTabEmpty(p, G_LAPTOP, mangaSrvKind == "komga" ? "This server has no libraries" : "Nothing in this catalogue", "");
		if (!mangaSrvError.empty()) ui.drawHints({{"A", "Try again"}, {"B", "Back"}});
		else ui.drawHints({{"A", "Open"}, {"B", "Back"}});
	}

	// ---- a server's series and its chapters ----

	void Ui::Manga::mangaServerSeriesOpen(const std::string& key, const std::string& title, int from)
	{
		if (!mangaNet || !mangaNet->started() || key.empty()) return;
		mangaServerCancel();
		mangaSrvSeriesKey = key;
		mangaSrvSeriesTitle = title.empty() ? "Series" : title;
		mangaSrvSeriesId = serverItemId(mangaSrvId, key);
		mangaSrvSeriesFrom = from;
		mangaSrvChapters.clear();
		mangaSrvChaptersOn = false;
		mangaSrvSerCursor = mangaSrvSerScroll = mangaSrvSerPage = 0;
		mangaSrvSerMore = mangaSrvSerLoading = mangaSrvSerAppend = false;
		mangaSrvSerError.clear();
		mangaSrvView = MGSRV_SERIES;
		mangaView = 2;
		// the store is where a series' title lives for the reader and the History (as a source's is); it stays out
		// of the Library: a server's series is added there only when that is a thing of its own
		if (mangaStore)
		{
			MangaSeries m;
			m.id = mangaSrvSeriesId;
			m.title = mangaSrvSeriesTitle;
			m.source = "server:" + mangaSrvId;
			if (const MangaSeries* old = mangaStore->series(mangaSrvSeriesId))
			{
				m.added = old->added;
				m.lastRead = old->lastRead;
				m.readingMode = old->readingMode;
			}
			else m.added = (int64_t)time(nullptr);
			// never in the Library: it is read from the server, and a Library card has no way back here
			m.inLibrary = false;
			mangaStore->putSeries(m);
			mangaSave();
		}
		mangaSeriesId = mangaSrvSeriesId;
		mangaSeriesFrom = 3;	// Browse's grid (an id that says whose the page is keeps it apart)
		mserCursor = -1;
		mserScroll = 0;
		ui.page = Page::MangaSeries;
		mangaSrvLocal++;
		mangaServerSeriesLoad();
	}

	void Ui::Manga::mangaServerSeriesOpenFromId(const std::string& id, Player& p)
	{
		(void)p;
		std::string server, key;
		splitServerId(id, server, key);
		if (server.empty() || key.empty() || !mangaNet || !mangaNet->started()) return;
		const MangaServer* s = mangaNet->servers().find(server);
		if (!s) return;
		mangaServerCancel();
		mangaSrvId = server;
		mangaSrvName = s->name;
		mangaSrvKind = s->kind;
		mangaSrvHere = s->name;
		mangaSrvRootUrl.clear();
		mangaSrvPath.clear();
		mangaSrvItems.clear();
		mangaSrvCursor = mangaSrvScroll = mangaSrvPage = 0;
		mangaSrvMore = mangaSrvAppend = false;
		mangaSrvNext.clear();
		mangaSrvError.clear();
		std::string title;
		if (mangaStore)
			if (const MangaSeries* ms = mangaStore->series(id)) title = ms->title;
		mangaServerSeriesOpen(key, title, MGSRV_FROM_HISTORY);
	}

	void Ui::Manga::mangaServerSeriesLoad()
	{
		mangaSrvSerError.clear();
		if (!mangaNet || !mangaNet->started())
		{
			mangaSrvSerLoading = false;
			mangaSrvSerError = "The reader\xE2\x80\x99s network side is off";
			return;
		}
		const MangaServer* s = mangaNet->servers().find(mangaSrvId);
		if (!s || mangaSrvSeriesKey.empty())
		{
			mangaSrvSerLoading = false;
			mangaSrvSerError = "This server isn\xE2\x80\x99t saved";
			return;
		}
		std::string pw = mangaNet->servers().password(mangaSrvId);
		RemoteRequest rq = s->kind == "komga" ? remoteKomgaBooks(*s, pw, mangaSrvSeriesKey, mangaSrvSerPage)
			: remoteAuth(*s, pw, mangaSrvSeriesKey);
		if (mangaSrvSerSerial) mangaNet->fetchApi().cancel(mangaSrvSerSerial);
		mangaSrvSerLoading = true;
		mangaSrvSerSerial = mangaNet->fetchApi().get(rq.url, rq.headers, 8 * 1024 * 1024);
		if (!mangaSrvSerSerial)
		{
			mangaSrvSerLoading = false;
			mangaSrvSerError = "Couldn\xE2\x80\x99t ask the server";
		}
		mangaSrvLocal++;
	}

	void Ui::Manga::mangaServerSeriesReload()
	{
		mangaSrvChapters.clear();
		mangaSrvChaptersOn = false;
		mangaSrvSerCursor = mangaSrvSerScroll = mangaSrvSerPage = 0;
		mangaSrvSerMore = false;
		mangaSrvSerError.clear();
		mangaServerSeriesLoad();
	}

	void Ui::Manga::mangaServerSeriesMore()
	{
		if (mangaSrvSerLoading || !mangaSrvSerMore || mangaSrvKind != "komga") return;
		mangaSrvSerAppend = true;
		mangaSrvSerPage++;
		mangaServerSeriesLoad();
	}

	void Ui::Manga::mangaServerSeriesBack(Player& p)
	{
		(void)p;
		mangaServerCancel();
		ui.page = Page::Manga;
		if (mangaSrvSeriesFrom == MGSRV_FROM_HISTORY)
		{
			mangaTab = MT_HISTORY;
			mangaSrvView = MGSRV_NONE;
			mangaView = 0;
			mangaSrvLocal++;
			return;
		}
		mangaSrvView = MGSRV_CATALOGUE;
		mangaView = 2;
		if (mangaSrvSeriesFrom == MGSRV_FROM_FOLDER)
		{
			// the series' folder was opened from the catalogue: its parent's list again
			if (!mangaSrvPath.empty()) mangaSrvPath.pop_back();
			mangaSrvItems.clear();
			mangaSrvCursor = mangaSrvScroll = mangaSrvPage = 0;
			mangaSrvMore = mangaSrvAppend = false;
			mangaSrvNext.clear();
			mangaSrvError.clear();
			mangaSrvHere = mangaSrvName;
			if (mangaSrvKind == "komga")
				mangaServerListLoad(mangaSrvPath.empty() ? MGSRV_LOAD_LIBRARIES : MGSRV_LOAD_SERIES);
			else mangaServerFeedLoad(mangaSrvPath.empty() ? mangaSrvRootUrl : mangaSrvPath.back());
		}
		mangaSrvLocal++;
	}

	bool Ui::Manga::mangaServerSeriesKey(Button b, Player& p)
	{
		mangaServersPoll();
		if (b == Button::Start) { openMangaSettings(); return false; }
		if (b == Button::B) { mangaServerSeriesBack(p); return false; }
		int n = (int)mangaSrvChapters.size();
		if (b == Button::A)
		{
			if (!mangaSrvSerError.empty() && !mangaSrvChaptersOn) mangaServerSeriesReload();
			else if (mserCursor < 0) mangaServerSeriesResume(p);
			else mangaServerSeriesChoose(mserCursor, p);
			return false;
		}
		if (!n) return false;
		// Resume is the row before the chapters
		int total = n + 1, cur = mserCursor + 1;
		switch (b)
		{
		case Button::Up: cur = (cur + total - 1) % total; break;
		case Button::Down: cur = (cur + 1) % total; break;
		case Button::Left: cur = std::max(0, cur - mserRows()); break;
		case Button::Right: cur = std::min(total - 1, cur + mserRows()); break;
		default: break;
		}
		mserCursor = cur - 1;
		if (mserCursor >= 0)
		{
			if (mserCursor < mserScroll) mserScroll = mserCursor;
			if (mserCursor >= mserScroll + mserRows()) mserScroll = mserCursor - mserRows() + 1;
		}
		// the last rows of a long chapter list ask for the book page after it
		if (mangaSrvSerMore && !mangaSrvSerLoading && mserCursor >= n - 2) mangaServerSeriesMore();
		return false;
	}

	bool Ui::Manga::mangaServerSeriesPointer(const Pointer& pt, int x, int y, Player& p)
	{
		mangaServersPoll();
		if (pt.kind != Pointer::Down) return false;
		if (mangaServerResumeRect().hit(x, y))
		{
			mserCursor = -1;
			mangaServerSeriesResume(p);
			return false;
		}
		// the chapters (the kit's rows): a drag scrolls them (the next book page asked near the end, as the D-pad
		// asks it), a tap reads the row
		mangaRowsPointer(mlChapters, p, x, y, [this] { return (int)mangaSrvChapters.size(); }, &mserScroll, &mserCursor,
			[](int) { return MangaRow(); }, [this, &p](int i, bool) {
				if (i < 0 || i >= (int)mangaSrvChapters.size()) return;
				mserCursor = i;
				mangaServerSeriesChoose(i, p);
			}, [this] {
				int n = (int)mangaSrvChapters.size();
				if (mangaSrvSerMore && !mangaSrvSerLoading && mserScroll + mserRows() >= n - 2) mangaServerSeriesMore();
			});
		return false;
	}

	void Ui::Manga::drawMangaServerSeries(const Player& p)
	{
		mangaServersPoll();
		ui.drawHeader(mangaSrvSeriesTitle.empty() ? "Series" : mangaSrvSeriesTitle, p);
		int n = (int)mangaSrvChapters.size();
		std::vector<std::string> ids;
		ids.reserve(n);
		for (const auto& c : mangaSrvChapters) ids.push_back(serverItemId(mangaSrvId, c.key));
		int unread = mangaStore ? mangaStore->unread(mangaSrvSeriesId, ids) : 0;
		tabCrumb = (mangaSrvSerError.empty() ? mangaSrvName : mangaSrvSerError) + " \xC2\xB7 " + std::to_string(n) +
			(n == 1 ? " chapter" : " chapters") + " \xC2\xB7 " + std::to_string(unread) + " unread";
		// Resume: the first started-but-unread chapter, else the first unread, else the first
		std::vector<MangaChapterMark> marks;
		marks.reserve(n);
		for (const auto& c : mangaSrvChapters)
		{
			MangaChapterMark m;
			if (mangaStore)
				if (const ChapterProgress* pr = mangaStore->progress(serverItemId(mangaSrvId, c.key)))
				{
					m.read = pr->read;
					m.started = pr->pages > 0 || pr->page > 0;
					m.page = pr->page;
				}
			marks.push_back(m);
		}
		MangaResumePoint resume = mangaResume(marks);
		Kit::Key rk;
		rk.glyph = &G_PLAY;
		rk.label = !n ? "No chapters"
			: mangaResumeKeyLabel(mangaSrvChapters[resume.chapter].name,
				mangaStore ? mangaStore->progress(serverItemId(mangaSrvId, mangaSrvChapters[resume.chapter].key)) : nullptr, mangaServerResumeRect().w);
		rk.key = "A";
		rk.tone = Kit::Tone::Primary;
		rk.focused = mserCursor < 0;
		rk.pressed = mangaPressed == MG_RESUME;
		rk.off = n == 0;
		ui.kit->key(mangaServerResumeRect(), rk);
		if (n == 0)
		{
			if (!mangaSrvSerError.empty())
			{
				drawMangaTabEmpty(p, G_LAPTOP, "Couldn\xE2\x80\x99t read it", mangaSrvSerError + ". A: try again");
				ui.drawHints({{"A", "Try again"}, {"B", "Back"}});
			}
			else
			{
				if (!mangaSrvSerLoading) drawMangaTabEmpty(p, G_BOOK, "No chapters", "");
				ui.drawHints({{"B", "Back"}});
			}
			return;
		}
		if (mserCursor >= n) mserCursor = n - 1;
		mlChapters.refresh = nullptr;	// (the list is shared with the other series pages: a server's has no pull)
		mangaRows(mlChapters, p, n, mserScroll, mserCursor, [this, rc = resume.chapter](int i) {
			const RemoteChapter& c = mangaSrvChapters[i];
			return mangaChapterRow(chapterLabel(c), serverItemId(mangaSrvId, c.key), i == rc, c.pages);
		});
		ui.drawHints({{"A", "Read"}, {"B", "Back"}});
	}

	// ---- reading a chapter ----

	int Ui::Manga::mangaServerChapterIndex() const
	{
		if (!isServerId(ui.pageReader->mangaReadSeriesId) || mangaSrvSeriesId != ui.pageReader->mangaReadSeriesId) return -1;
		for (size_t i = 0; i < mangaSrvChapters.size(); i++)
			if (serverItemId(mangaSrvId, mangaSrvChapters[i].key) == ui.pageReader->mangaReadChapterId) return (int)i;
		return -1;
	}

	void Ui::Manga::mangaServerSeriesChoose(int i, Player& p)
	{
		if (i < 0 || i >= (int)mangaSrvChapters.size()) return;
		mangaServerChapterOpen(i, p);
	}

	void Ui::Manga::mangaServerSeriesResume(Player& p)
	{
		if (mangaSrvChapters.empty()) return;
		std::vector<MangaChapterMark> marks;
		marks.reserve(mangaSrvChapters.size());
		for (const auto& c : mangaSrvChapters)
		{
			MangaChapterMark m;
			if (mangaStore)
				if (const ChapterProgress* pr = mangaStore->progress(serverItemId(mangaSrvId, c.key)))
				{
					m.read = pr->read;
					m.started = pr->pages > 0 || pr->page > 0;
					m.page = pr->page;
				}
			marks.push_back(m);
		}
		MangaResumePoint r = mangaResume(marks);
		if (r.chapter >= 0) mangaServerChapterOpen(r.chapter, p);
	}

	void Ui::Manga::mangaServerChapterOpen(int i, Player& p)
	{
		if (i < 0 || i >= (int)mangaSrvChapters.size()) return;
		if (!mangaNet || !mangaNet->started()) { mangaSrvSerError = "The reader\xE2\x80\x99s network side is off"; return; }
		const MangaServer* s = mangaNet->servers().find(mangaSrvId);
		if (!s) { mangaSrvSerError = "This server isn\xE2\x80\x99t saved"; return; }
		const RemoteChapter& c = mangaSrvChapters[i];
		std::string pw = mangaNet->servers().password(mangaSrvId);
		std::vector<std::string> urls;
		std::vector<std::pair<std::string, std::string>> headers;
		if (s->kind == "komga")
		{
			// a Komga book's page URLs, numbered from 1 as Komga numbers them; every page carries the book's auth
			if (c.pages <= 0) { mangaSrvSerError = "This chapter has no pages to read"; return; }
			for (int n = 1; n <= c.pages; n++)
			{
				RemoteRequest rq = remoteKomgaPage(*s, pw, c.key, n);
				if (rq.url.empty()) continue;
				if (headers.empty()) headers = rq.headers;
				urls.push_back(rq.url);
			}
		}
		else
		{
			// OPDS PSE: the template becomes one URL a page, and the auth header goes with them (the feed's Accept
			// is for an image here, not for a feed). A Kavita chapter's urls hold {key}: the key goes back into
			// each one only here, as the header goes with the other kinds' pages.
			urls = remoteOpdsPageUrls(c, W);
			if (urls.empty()) { mangaSrvSerError = "This chapter is a whole file: reading it here isn\xE2\x80\x99t supported yet"; return; }
			for (std::string& u : urls) u = remoteWithKey(*s, pw, u);
			headers = remoteAuth(*s, pw, urls.front()).headers;
			bool accept = false;
			for (auto& h : headers)
				if (h.first == "Accept") { h.second = "image/*"; accept = true; }
			if (!accept) headers.emplace_back("Accept", "image/*");
		}
		if (urls.empty()) { mangaSrvSerError = "This chapter has no pages to read"; return; }
		std::string chapterId = serverItemId(mangaSrvId, c.key);
		int page0 = 0;
		if (mangaStore)
			if (const ChapterProgress* pr = mangaStore->progress(chapterId))
				if (!pr->read) page0 = pr->page;
		// the chapter open so far is saved before the next one replaces it in readerStatus
		if (ui.page == Page::MangaRead)
		{
			mangaRecordProgress();
			mangaSave();
		}
		mangaSrvSerError.clear();
		mangaSeriesId = mangaSrvSeriesId;
		mangaReadSrcChapters.clear();	// not an installed source's
		ui.pageReader->mangaReadSeriesId = mangaSrvSeriesId;
		ui.pageReader->mangaReadChapterId = chapterId;
		ui.pageReader->mangaReadChapterTitle = chapterLabel(c);
		// path: the chapter's own id, the cache's key; the pages are the URLs, in order, with the server's header
		ui.pageReader->mangaRead = {mangaSrvSeriesId, mangaSrvSeriesTitle, chapterId, ui.pageReader->mangaReadChapterTitle, chapterId, false, page0,
			mangaModeFor(mangaSrvSeriesId, p), p.settings.mangaFit};
		ui.pageReader->mangaRead.pageUrls = urls;
		ui.pageReader->mangaRead.pageHeaders = headers;
		mangaProgPage = ui.pageReader->readerStatus.page;
		mangaProgPages = ui.pageReader->readerStatus.pages;
		mangaReadLive = false;
		ui.pageReader->mangaSliderPage = -1;
		mangaCtrlUntil = 0;
		ui.pageReader->mangaZoomAsked = 100;
		ui.page = Page::MangaRead;
		if (p.settings.mangaOpenFull)
		{
			// Open fullscreen: the command SELECT sends right after the chapter, as a source's chapter does
			Uint32 now = SDL_GetTicks();
			ui.pageReader->readerCmds.push_back({PageReader::ReaderCmd::Fullscreen, 1.0, (int64_t)now, (double)p.settings.mangaHinge});
			ui.pageReader->mfTagUntil = now + 2500;
			if (!p.settings.mangaFullSeen) mangaToastUntil = now + MG_TOAST_MS;
			mangaFullSeenPending = !p.settings.mangaFullSeen;
		}
	}

	void Ui::Manga::mangaServerChapterStep(int dir, Player& p)
	{
		int i = mangaServerChapterIndex();
		if (i < 0) return;
		int j = i + dir;
		if (j < 0 || j >= (int)mangaSrvChapters.size()) return;
		mangaServerChapterOpen(j, p);
	}

	// ---- the form: Add server / Edit ----

	void Ui::Manga::mangaServerAddOpen(Player& p)
	{
		if (!mangaNet || !mangaNet->started()) { ui.alert("Can\xE2\x80\x99t add servers", "The player has no folder to keep them in.");; return; }
		mangaServerCancel();
		mangaSrvFormId.clear();
		mangaSrvFormKind = "komga";
		mangaSrvFormName.clear();
		mangaSrvFormUrl = "https://";
		mangaSrvFormUser.clear();
		mangaSrvFormPassword.clear();
		mangaSrvFormPasswordTyped = false;
		mangaSrvFormCursor = SRV_FORM_NAME;
		mangaSrvFormNote.clear();
		mangaSrvTestText.clear();
		mangaSrvView = MGSRV_FORM;
		mangaView = 2;
		mangaSrvLocal++;
	}

	void Ui::Manga::mangaServerEditOpen(const std::string& id, Player& p)
	{
		(void)p;
		if (!mangaNet || !mangaNet->started()) return;
		const MangaServer* s = mangaNet->servers().find(id);
		if (!s) return;
		mangaServerCancel();
		mangaSrvFormId = id;
		mangaSrvFormKind = s->kind;
		mangaSrvFormName = s->name;
		mangaSrvFormUrl = s->url;
		mangaSrvFormUser = s->user;
		mangaSrvFormPassword.clear();
		mangaSrvFormPasswordTyped = false;
		mangaSrvFormCursor = SRV_FORM_KIND;
		mangaSrvFormNote.clear();
		mangaSrvTestText.clear();
		mangaSrvView = MGSRV_FORM;
		mangaView = 2;
		mangaSrvLocal++;
	}

	bool Ui::Manga::mangaServerFormKey(Button b, Player& p)
	{
		if (b == Button::B) { mangaServerBack(p); return false; }
		switch (b)
		{
		case Button::Up: mangaSrvFormCursor = (mangaSrvFormCursor + MGSRV_FORM_ROWS - 1) % MGSRV_FORM_ROWS; break;
		case Button::Down: mangaSrvFormCursor = (mangaSrvFormCursor + 1) % MGSRV_FORM_ROWS; break;
		case Button::Left:
		case Button::Right:
			// the kind is the one row the arrows change in place
			if (mangaSrvFormCursor == SRV_FORM_KIND)
			{
				int k = (kindIndex(mangaSrvFormKind) + (b == Button::Right ? 1 : 2)) % 3;
				mangaSrvFormKind = SRV_KIND_IDS[k];
				mangaSrvTestText.clear();
				mangaSrvFormNote.clear();
				mangaSrvLocal++;
			}
			break;
		case Button::A: mangaServerFormChoose(p); break;
		default: break;
		}
		return false;
	}

	void Ui::Manga::mangaServerFormChoose(Player& p)
	{
		switch (mangaSrvFormCursor)
		{
		case SRV_FORM_KIND:
		{
			int k = (kindIndex(mangaSrvFormKind) + 1) % 3;
			mangaSrvFormKind = SRV_KIND_IDS[k];
			mangaSrvTestText.clear();
			mangaSrvFormNote.clear();
			mangaSrvLocal++;
			break;
		}
		case SRV_FORM_NAME: ui.openKeyboard(MGSRV_NAME_FIELD, mangaSrvFormName); break;
		case SRV_FORM_URL: ui.openKeyboard(MGSRV_URL_FIELD, mangaSrvFormUrl); break;
		case SRV_FORM_USER: ui.openKeyboard(MGSRV_USER_FIELD, mangaSrvFormUser); break;
		case SRV_FORM_PASS: ui.openKeyboard(MGSRV_PASS_FIELD, mangaSrvFormPasswordTyped ? mangaSrvFormPassword : ""); break;
		case SRV_FORM_TEST: mangaServerTest(); break;
		case SRV_FORM_SAVE: mangaServerFormSave(p); break;
		default: break;
		}
	}

	void Ui::Manga::mangaServerFieldTyped(int field, const std::string& text)
	{
		switch (field)
		{
		case MGSRV_NAME_FIELD: mangaSrvFormName = trimmed(text); break;
		// a credential typed into an address is taken out at once: it is never kept, shown again or sent in a url.
		// Kavita's api key rides in its address the same way: it moves to the API key field, and {key} stays behind
		case MGSRV_URL_FIELD:
			mangaSrvFormUrl = withoutUrlCredentials(trimmed(text));
			if (mangaSrvFormKind == "kavita")
			{
				std::string stored, key;
				if (readKavitaUrl(mangaSrvFormUrl, stored, key) && !key.empty())
				{
					mangaSrvFormUrl = stored;
					mangaSrvFormPassword = key;
					mangaSrvFormPasswordTyped = true;
				}
			}
			break;
		case MGSRV_USER_FIELD: mangaSrvFormUser = trimmed(text); break;
		case MGSRV_PASS_FIELD: mangaSrvFormPassword = text; mangaSrvFormPasswordTyped = true; break;
		default: return;
		}
		// the answer on screen was for what the field held before
		mangaSrvFormNote.clear();
		mangaSrvTestText.clear();
		if (mangaSrvTestSerial && mangaNet && mangaNet->started()) mangaNet->fetchApi().cancel(mangaSrvTestSerial);
		mangaSrvTestSerial = 0;
		mangaSrvLocal++;
	}

	void Ui::Manga::mangaServerTest()
	{
		mangaSrvTestText.clear();
		if (!mangaNet || !mangaNet->started()) { mangaSrvTestText = "The reader\xE2\x80\x99s network side is off"; return; }
		std::string urlKey;
		MangaServer s = mangaFormServer(mangaSrvFormKind, trimmed(mangaSrvFormName),
			withoutUrlCredentials(trimmed(mangaSrvFormUrl)), trimmed(mangaSrvFormUser), urlKey);
		if (!isMangaServerKind(s.kind)) { mangaSrvTestText = "Choose a kind"; return; }
		if (!isMangaServerUrl(s.url)) { mangaSrvTestText = "Type an http:// or https:// address"; return; }
		// a typed password (Kavita's api key) is the one tried; then the address's own key, then the store's
		std::string pw = mangaFormKavitaKey(mangaNet->servers(), mangaSrvFormId, mangaSrvFormPasswordTyped,
			mangaSrvFormPassword, urlKey);
		RemoteRequest rq = s.kind == "komga" ? remoteKomgaLibraries(s, pw) : remoteOpdsStart(s, pw);
		if (mangaSrvTestSerial) mangaNet->fetchApi().cancel(mangaSrvTestSerial);
		mangaSrvTestSerial = mangaNet->fetchApi().get(rq.url, rq.headers, 4 * 1024 * 1024);
		mangaSrvTestText = mangaSrvTestSerial ? "Testing\xE2\x80\xA6" : "Couldn\xE2\x80\x99t ask the server";
		mangaSrvFormNote.clear();
		mangaSrvLocal++;
	}

	void Ui::Manga::mangaServerFormSave(Player& p)
	{
		if (!mangaNet || !mangaNet->started())
		{
			mangaSrvFormNote = "Servers need a state folder";
			mangaSrvLocal++;
			return;
		}
		MangaServerStore& store = mangaNet->servers();
		std::string url = withoutUrlCredentials(trimmed(mangaSrvFormUrl));
		std::string urlKey;
		MangaServer s = mangaFormServer(mangaSrvFormKind, trimmed(mangaSrvFormName), url,
			trimmed(mangaSrvFormUser), urlKey);
		// the store takes a Kavita key out of the address itself and seals it: the address is handed over as typed
		s.url = url;
		if (!isMangaServerKind(s.kind)) { mangaSrvFormNote = "Choose a kind"; mangaSrvLocal++; return; }
		if (s.name.empty()) { mangaSrvFormNote = "Type a name"; mangaSrvFormCursor = SRV_FORM_NAME; mangaSrvLocal++; return; }
		if (s.name.size() > 40) { mangaSrvFormNote = "A name of up to 40 characters"; mangaSrvLocal++; return; }
		if (!isMangaServerUrl(s.url))
		{
			mangaSrvFormNote = "Type an http:// or https:// address, with a host";
			mangaSrvFormCursor = SRV_FORM_URL;
			mangaSrvLocal++;
			return;
		}
		std::string id = mangaSrvFormId;
		if (id.empty())
		{
			id = store.add(s);
			if (id.empty()) { mangaSrvFormNote = "The server wasn\xE2\x80\x99t saved"; mangaSrvLocal++; return; }
			mangaSrvFormId = id;
		}
		else
		{
			s.id = id;
			if (!store.update(s)) { mangaSrvFormNote = "The server wasn\xE2\x80\x99t saved"; mangaSrvLocal++; return; }
		}
		// an empty field with a key still in the address keeps that key: the address still names one
		if (mangaSrvFormPasswordTyped && !(mangaSrvFormPassword.empty() && s.kind == "kavita" && !urlKey.empty()))
		{
			// the store seals it with secret.*; a plain one never reaches the file
			if (!store.setPassword(id, mangaSrvFormPassword) && !mangaSrvFormPassword.empty())
			{
				mangaSrvFormNote = "Saved, but the password couldn\xE2\x80\x99t be kept";
				mangaSrvLocal++;
				return;
			}
		}
		std::string name = s.name;
		mangaServerCancel();
		mangaSrvView = MGSRV_NONE;
		mangaView = 0;
		mangaSrvFormId.clear();
		mangaSrvFormKind = "komga";
		mangaSrvFormName.clear();
		mangaSrvFormUrl = "https://";
		mangaSrvFormUser.clear();
		mangaSrvFormPassword.clear();
		mangaSrvFormPasswordTyped = false;
		mangaSrvFormCursor = 0;
		mangaSrvFormNote.clear();
		mangaSrvTestText.clear();
		mangaSrvLocal++;
		p.osd("Saved " + name, 2000);
	}

	void Ui::Manga::drawMangaServerForm(const Player& p)
	{
		// the same form as a network connection (screens.js manga-server): a form row per field, Test and Save the keys
		// at the foot, what Test said (or Save needs) over them
		ui.drawHeader(mangaSrvFormId.empty() ? "Add server" : "Edit server", p);
		tabCrumb = "Manga \xE2\x80\xBA Browse \xE2\x80\xBA Komga, Kavita, OPDS";
		Kit& k = *ui.kit;
		std::string pass;
		if (mangaSrvFormPasswordTyped) pass = passwordBullets(mangaSrvFormPassword);
		else if (!mangaSrvFormId.empty() && mangaNet && mangaNet->started() &&
			mangaNet->servers().find(mangaSrvFormId) && mangaNet->servers().find(mangaSrvFormId)->hasPassword())
			pass = "Saved";
		const char* LABELS[5] = {"Kind", "Name", "Address", "User", mangaSrvFormKind == "kavita" ? "API key" : "Password"};
		std::string values[5] = {std::string("\xE2\x97\x80 ") + kindName(mangaSrvFormKind) + " \xE2\x96\xB6", mangaSrvFormName,
			mangaSrvFormUrl, mangaSrvFormUser, pass};
		const char* HOLD[5] = {"", "A name to know it by", "http://server:port", "None", "None"};
		for (int i = 0; i < 5; i++)
		{
			Kit::FormField f;
			f.label = LABELS[i];
			f.value = values[i];
			f.placeholder = HOLD[i];
			f.focused = i == mangaSrvFormCursor;
			k.formRow(lay::formRow(mangaSrvFormRowRect(i)), f);
		}
		Kit::Key test;
		test.glyph = &G_REFRESH;
		test.label = "Test";
		test.focused = mangaSrvFormCursor == SRV_FORM_TEST;
		k.key(mangaSrvFormRowRect(SRV_FORM_TEST), test);
		Kit::Key save;
		save.glyph = &G_CHECK;
		save.label = "Save";
		save.tone = Kit::Tone::Primary;
		save.focused = mangaSrvFormCursor == SRV_FORM_SAVE;
		k.key(mangaSrvFormRowRect(SRV_FORM_SAVE), save);
		// the line over the keys: Test's answer, what Save needs, or that the secret is sealed
		Rect keys = mangaSrvFormRowRect(SRV_FORM_TEST);
		std::string line = !mangaSrvFormNote.empty() ? mangaSrvFormNote : !mangaSrvTestText.empty() ? mangaSrvTestText
			: mangaSrvFormKind == "kavita" ? "The API key is sealed on Save and never shown again."
			: "A password is sealed on Save and never shown again.";
		ui.drawTextMid(line, F_I12, !mangaSrvFormNote.empty() ? DANGER : DIM, keys.x, keys.y - 14, lay::shell().body.w - 24);
		ui.drawHints({{"A", "Change"}, {"LEFT/RIGHT", "Kind"}, {"B", "Back"}});
	}

	// ---- X on a server row: Edit / Delete, the delete asked twice ----

	void Ui::Manga::mangaServerModalOpen(int row, Player& p)
	{
		(void)p;
		if (!mangaNet || !mangaNet->started()) return;
		std::vector<MangaServer> list = mangaNet->servers().list();
		if (row < 1 || row - 1 >= (int)list.size()) return;
		mangaSrvMenu = true;
		mangaSrvMenuId = list[row - 1].id;
		mangaSrvMenuCursor = 0;
		mangaSrvDeleteArmed = false;
		mangaSrvLocal++;
		ui.openMore([this](const Player&) { return mangaServerSpec(); }, "mangasrv", [this] { return mangaSrvMenu; },
			[this] { mangaSrvMenu = false; mangaSrvDeleteArmed = false; });
	}

	bool Ui::Manga::mangaServerModalKey(Button b, Player& p)
	{
		if (b == Button::B)
		{
			mangaSrvMenu = false;
			mangaSrvDeleteArmed = false;
			mangaSrvLocal++;
			return false;
		}
		if (b == Button::Up || b == Button::Down)
		{
			mangaSrvMenuCursor = 1 - mangaSrvMenuCursor;
			mangaSrvDeleteArmed = false;
			mangaSrvLocal++;
			return false;
		}
		if (b != Button::A) return false;
		if (mangaSrvMenuCursor == 0)
		{
			std::string id = mangaSrvMenuId;
			mangaSrvMenu = false;
			mangaSrvDeleteArmed = false;
			mangaServerEditOpen(id, p);
			return false;
		}
		// Delete: A again within the window removes it (its password's sealed copy goes with it)
		Uint32 now = SDL_GetTicks();
		if (!mangaSrvDeleteArmed || now - mangaSrvDeleteAt >= QUIT_WINDOW_MS)
		{
			mangaSrvDeleteArmed = true;
			mangaSrvDeleteAt = now;
			mangaSrvLocal++;
			return false;
		}
		if (mangaNet && mangaNet->started())
		{
			std::string id = mangaSrvMenuId;
			std::string name = id;
			if (const MangaServer* s = mangaNet->servers().find(id)) name = s->name;
			mangaNet->servers().remove(id);
			// a server whose page was left open has nothing saved behind it any more
			if (id == mangaSrvId) mangaServersReset();
			p.osd("Removed " + name, 2500);
		}
		mangaSrvMenu = false;
		mangaSrvDeleteArmed = false;
		mangaSrvLocal++;
		return false;
	}

	Ui::MoreSpec Ui::Manga::mangaServerSpec()
	{
		MoreSpec m;
		if (!mangaSrvMenu) return m;
		std::string name = mangaSrvMenuId;
		if (mangaNet && mangaNet->started())
			if (const MangaServer* s = mangaNet->servers().find(mangaSrvMenuId)) name = s->name;
		m.title = "Server";
		m.note = mangaSrvDeleteArmed ? "Press A again to delete" : name;
		for (int i = 0; i < 2; i++)
		{
			MoreItem it;
			it.glyph = i == 0 ? &G_PENCIL : &G_TRASH;
			it.label = i == 0 ? "Edit" : "Delete";
			it.danger = i == 1;
			it.lit = i == 1 && mangaSrvDeleteArmed;
			it.note = i == 1 && mangaSrvDeleteArmed ? "Press again" : "";
			it.stay = true;	// (Delete is asked twice: the keys' own handler ends the menu)
			it.act = [this, i](Player& p) { mangaSrvMenuCursor = i; return mangaServerModalKey(Button::A, p); };
			m.rows.push_back(it);
		}
		return m;
	}

	// ---- signature() ----

	std::string Ui::Manga::mangaServersSignature() const
	{
		// the typed password never comes near this string: only whether one was typed (the field draws bullets)
		char buf[320];
		snprintf(buf, sizeof buf, "|sv%d,%d %d,%d,%d %d,%d,%d %zu %zu %d,%d,%d,%d %d,%d %d,%d,%d %llu %d %d",
			mangaView, mangaSrvView, mangaSrvCursor, mangaSrvScroll, mangaSrvPage, mangaSrvSerCursor, mangaSrvSerScroll,
			mangaSrvSerPage, mangaSrvItems.size(), mangaSrvChapters.size(), (int)mangaSrvMore, (int)mangaSrvLoading,
			(int)mangaSrvSerMore, (int)mangaSrvSerLoading, mangaSrvFormCursor, (int)mangaSrvFormPasswordTyped,
			(int)mangaSrvMenu, mangaSrvMenuCursor, (int)mangaSrvDeleteArmed, (unsigned long long)mangaSrvLocal,
			(int)mangaSrvChaptersOn, (int)mangaSrvAppend);
		std::string s = buf;
		s += "|" + mangaSrvId + mangaSrvName + mangaSrvKind + mangaSrvHere + mangaSrvError + mangaSrvSerError;
		s += "|" + mangaSrvSeriesId + mangaSrvSeriesTitle;
		s += "|" + mangaSrvFormId + mangaSrvFormKind + mangaSrvFormName + mangaSrvFormUrl + mangaSrvFormUser +
			mangaSrvFormNote + mangaSrvTestText;
		if (!mangaSrvItems.empty())
			s += "|i" + mangaSrvItems.front().key + mangaSrvItems.back().key + mangaSrvItems.front().title;
		if (!mangaSrvChapters.empty())
			s += "|c" + mangaSrvChapters.front().key + mangaSrvChapters.back().key + chapterLabel(mangaSrvChapters.front());
		if (mangaNet && mangaNet->started())
			s += "|svr" + std::to_string(mangaNet->servers().version()) + std::to_string(mangaNet->servers().list().size());
		return s;
	}
}
