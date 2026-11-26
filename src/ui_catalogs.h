// Books › Catalogs (GitHub #22; docs/redesign/backlog.md › Catalogs (OPDS) in Books; screens.js books-catalogs, cat-*):
// Books' third tab. A catalog is an OPDS 1.2 feed (Calibre's content server, Calibre-Web, COPS, Kavita, Komga,
// Project Gutenberg): its folders are browsed, a book is downloaded into <card>/Books/<catalog>/ and is then on the
// Shelf like any other file (listBooks walks sub-folders). Owned by Books, which hands it everything while the tab
// is open; its own views are the list (the tab), the form, a feed (folders or books) and a book's page.
//
// - The list is a MangaServerStore loaded from <state>/catalogs (the same file and sealing as Manga's servers, its
//   own key), so the address, the user and the sealed password are kept exactly as Manga keeps them.
// - Requests go through a MangaFetch of its own (main makes it, never destroys it; its answers come as their own SDL
//   event and reach onFetched on the main thread). Nothing here waits on the network.
// - The sign-in: none first; a 401 says Basic or Digest (Calibre asks Digest by default) and the request is sent
//   again with it, then every later one carries it. A login goes only to the catalog's own origin (sameOrigin);
//   curl drops a custom Authorization header on a redirect to another host, so a book's link that leaves the host
//   is fetched without it.
// - One download at a time (the owner, 2026-11-26): written to a hidden "<dir>/.<name>.part", renamed to the hidden
//   name when whole, checked (not a web page), then renamed to "<Title> - <Author>.<ext>". B leaves it running; Y
//   on the book's page cancels it. Stale part files are removed when the catalogs are loaded.
//
// The form is a view of this page, not an overlay: the keyboard takes the keys after the overlays do (Ui::press), so
// a form under it must be a page, as Network's and Manga's server forms are. The sheets (More, Remove, a refused
// sign-in, a failed download) are overlays.
#pragma once
#include "ui_books.h"
#include "catalogs.h"
#include "mangafetch.h"
#include "mangaservers.h"
#include "opds.h"
#include "discovery.h"
#include <cstdint>
#include <functional>
#include <map>

namespace bp
{
	struct Ui::Books::Catalogs
	{
		Ui& ui;
		explicit Catalogs(Ui& u) : ui(u) {}

		// ---- what Books hands over while the tab is open ----
		bool key(Button b, Player& p);
		bool pointer(const Pointer& pt, int x, int y, Player& p);
		void tick(Player& p);
		void draw(const Player& p);
		std::string signature() const;
		void head(ShellHead& h) const;	// the title, crumb and hints of the view shown
		bool atTop() const { return view() == V_LIST; }	// the tab itself (Books' tabs are drawn over it)
		SmoothList* smoothList();

		// ---- the saved catalogs ----
		MangaServerStore store;
		bool loaded = false;
		void load();	// once, the first time the tab is shown (the state folder is known by then)
		std::map<std::string, CatalogGot> gots;	// by catalog id: what it already gave the card
		CatalogGot& got(const std::string& id);
		std::vector<MangaServer> saved() const { return store.list(); }
		const MangaServer* openCatalog() const { return store.find(openId); }
		void addGutenberg(Player& p);	// the empty tab's Y: a public catalog, no sign-in
		void removeCatalog(const std::string& id, Player& p);

		// ---- the network (main: attach once; onFetched on each of its events) ----
		MangaFetch* fetch = nullptr;
		Uint32 fetchEvent = 0;
		bool fetchStarted = false;
		void attach(MangaFetch* f, Uint32 ev) { fetch = f; fetchEvent = ev; }
		void onFetched(Player& p);
		// the sign-in a catalog asked for this visit (a Digest challenge and its count, or Basic)
		struct Login
		{
			HttpChallenge ch;
			unsigned nc = 0;
		};
		std::map<std::string, Login> logins;
		enum Purpose { P_FEED, P_MORE, P_SEARCHDESC, P_TEST, P_GET };
		struct Pending
		{
			Purpose what = P_FEED;
			std::string catalogId, url;	// url: as stored (a Kavita one keeps {key})
			int depth = 0;			// P_FEED / P_MORE: the feed page it fills
			int tries = 0;			// sent again after a 401 with the sign-in asked for
			std::string words;		// P_SEARCHDESC: the search's words
			uint32_t sentAt = 0;
		};
		std::map<uint64_t, Pending> pending;
		uint64_t send(const MangaServer& s, Pending pd);	// 0: no fetcher
		void answered(MangaFetch::Result& r, const Pending& pd, Player& p);
		void feedAnswered(MangaFetch::Result& r, const Pending& pd, const MangaServer& s);
		void getAnswered(MangaFetch::Result& r, const MangaServer& s, Player& p);
		void testAnswered(MangaFetch::Result& r, const Pending& pd, const MangaServer& s, Player& p);
		void signInRefused(const MangaServer& s, bool askedForOne);
		// a question as the Confirm sheet (an overlay): the safe key closes, the action key acts. focus 1: the
		// action is first (a retry), 0: the safe one is (a removal)
		struct Ask;
		void ask(const std::string& title, const std::string& text, const std::string& safe, const std::string& action,
			int focus, std::function<void(Player&)> act, bool danger = false, bool failure = false);

		// ---- found on this network (Calibre announces itself over mDNS: discovery.h) ----
		// Browsed on a worker when the empty tab or the Add form opens; its list comes back as one SDL event. Rows
		// whose address is already saved are not offered.
		std::vector<FoundCatalog> found;	// the last browse's
		bool finding = false, findWasOn = false;
		Uint32 findEvent = 0;
		uint64_t findGen = 0;	// the browse that counts (an older one's answer is dropped)
		int foundCursor = -1;	// the empty tab: the found row focused (-1: its two keys)
		void attachFind(Uint32 ev) { findEvent = ev; }
		void startFind();
		void onFound(const SDL_Event& e);
		std::vector<FoundCatalog> offered() const;	// found, less what is saved
		int emptyFoundCount() const;	// rows under the empty tab's keys (at most 2)
		lay::EmptyState emptyLay() const;	// the empty tab's layout (shorter when found rows share it)
		Rect emptyFoundRect(int i) const;
		int formFoundCount() const;	// rows after the form's own (only when adding, and as many as fit)
		int formBase() const;	// the form's own rows
		void pickFound(const FoundCatalog& c);
		void drawFoundRow(Rect r, const FoundCatalog& c, bool focused, bool pressed);

		// ---- the views ----
		enum View { V_LIST, V_FORM, V_FEED, V_BOOK };
		View view() const { return formOpen ? V_FORM : bookOpen ? V_BOOK : !feeds.empty() ? V_FEED : V_LIST; }
		int listCursor = 0;	// the tab's rows: the catalogs, then Add a catalog…
		int pressed = -1;	// the row or key a finger is on (-1 none)
		bool touchMoved = false;
		int touchY = 0;
		bool onList = false, caught = false;

		// a feed page: folders (navigation) or books (acquisition), one per level opened, the catalog's top first
		enum FeedError { E_NONE, E_REACH, E_NOT_CATALOG, E_SIGN_IN };
		struct Feed
		{
			std::string url, title;
			OpdsFeed feed;
			bool loading = true, loadingMore = false, search = false;
			FeedError error = E_NONE;
			std::string why;	// the error's own words (an HTTP status, curl's reason)
			int cursor = 0;
			uint64_t req = 0, moreReq = 0;
			bool books() const;	// an acquisition feed: drawn as covers (or the two-line list)
		};
		std::string openId;	// the catalog being browsed
		std::vector<Feed> feeds;
		bool listView = false;	// More › View: the books as the two-line list instead of covers
		std::string searchWords;	// the last search's (Y again keeps them)
		void openCatalogAt(const std::string& id, Player& p);
		void openFeed(const std::string& url, const std::string& title, bool search);
		void loadFeed(int depth);
		void loadMore(int depth);
		void search(const std::string& words);
		bool canSearch() const;	// the catalog named a search (OpenSearch or a {searchTerms} feed)
		void feedBack();
		void openEntry(Player& p);	// A on a feed's row: a folder or a book's page
		void feedMore(Player& p);	// X on a feed

		// a book's page
		bool bookOpen = false;
		OpdsEntry book;
		std::string bookBase;	// the feed it came from (its links resolve against it)
		int fmt = 0;		// the chosen offer
		std::vector<OpdsLink> offers;
		void openBook(const OpdsEntry& e, const std::string& base);
		std::string onCard(const std::string& entryId) const;	// the file this catalog gave for it ("" none)
		void bookMore(Player& p);

		// ---- the form (Add a catalog / Edit catalog) ----
		bool formOpen = false;
		std::string formId;	// the catalog edited ("" a new one)
		std::string fAddress, fName, fUser, fPassword, fKind = "opds";
		bool fAccount = false, fPasswordTyped = false;
		int fCursor = 0;
		std::string fTest, fNote;	// what Test said; what Save needs
		bool fTestOk = false, fSaveAfterTest = false;
		uint64_t fTestReq = 0;
		MangaServer fTestServer;	// the form as Test sent it (not saved: it has no id in the store)
		std::string fTestPassword;
		std::vector<MangaServer> mangaCopies() const;	// Manga's OPDS and Kavita servers this list has not
		std::vector<MangaServer> fCopies;	// (read when the form opens)
		void openForm(const MangaServer* existing, int at = 0);
		void formRowsChanged() { fTest.clear(); fTestOk = false; fNote.clear(); }
		int formRows() const;	// Address, Name, Sign in, [User, Password], [Copy from Manga's servers…]
		int formRowId(int i) const;
		MangaServer formServer() const;	// the form as a server (its address made whole)
		void formChoose(int id, Player& p);
		void formTest(bool thenSave);
		void formSave(Player& p, bool anyway);
		void copyFromManga();
		void fieldTyped(int field, const std::string& text);	// the keyboard's Done (ui_keyboard.cpp)

		// ---- the download: one at a time ----
		struct Download
		{
			bool active = false;
			std::string catalogId, entryId, title, url, fmtName;
			std::string hidden, final;	// "<dir>/.<name>" while it comes; "<dir>/<name>" when whole
			long long length = -1, bytes = 0;	// the link's length (an estimate); what the part file holds
			uint64_t req = 0;
			uint32_t startedAt = 0, statAt = 0;
		};
		Download dl;
		// base: the feed the offer's link resolves against (the book page's when none is given)
		void startDownload(const OpdsEntry& e, const OpdsLink& offer, Player& p, const std::string* base = nullptr);
		// More › Download every format: the offers after the one coming, one at a time through startDownload, all
		// into the same folder. A failure or a cancel drops the rest
		struct Queue
		{
			OpdsEntry entry;
			std::string base;
			std::vector<OpdsLink> rest;
			int total = 0;
		};
		Queue queued;
		void downloadAll(Player& p);
		void deleteFromCard(Player& p);	// More › Delete from the card…: its Confirm sheet's action
		void cancelDownload(const char* why);
		std::string savesTo(const MangaServer& s, long long* freeBytes = nullptr) const;	// the folder a book goes in
		std::string lastFailure;	// the failed download's words, for its sheet
		void downloadFailed(const std::string& title, const std::string& why, Player& p);

		// ---- the drawing ----
		SmoothList list{ui};
		lay::Box body() const;	// the shell's body less its side padding
		Rect listArea() const;	// the tab's rows (under Books' tabs) or a feed's
		void drawList(const Player& p);
		void drawForm(const Player& p);
		void drawFeed(const Player& p);
		void drawBook(const Player& p);
		struct BookLayout
		{
			Rect cover, author, facts, summary, chips, key, under;
		};
		BookLayout bookLayout() const;
		Rect formRowRect(int i) const;	// i < formRows(): a row; formRows(), +1: Test, Save
		int feedCols() const;	// covers across (5), or 1 for a list
		int feedRowH() const;
		int feedCount() const;	// the smooth list's rows
	};
}
