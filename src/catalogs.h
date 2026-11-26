// Books › Catalogs (GitHub #22; docs/notes/2026-11-26-opds-books.md): what a catalog's download needs that is not
// the OPDS parser (opds.h) or the saved list (MangaServerStore, loaded from <state>/catalogs): the sign-in a server
// asks for (Basic, or Digest as Calibre's content server asks by default), whose host a login may go to, the file
// name a book is saved under, the Books folder it goes in, and the list of books a catalog already gave the card.
// No window, no threads, no network: core-test feeds it captured answers.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	// ---- the sign-in ----

	// One WWW-Authenticate challenge, the parts a client answers with
	struct HttpChallenge
	{
		std::string scheme;	// "basic" or "digest" (lower case); "" none read
		std::string realm, nonce, opaque, qop, algorithm;
		bool stale = false;	// Digest: the nonce ran out, the password was right
	};
	// The challenge to answer among an answer's WWW-Authenticate values: Digest (MD5, MD5-sess) before Basic, as
	// the stronger. False when none is one we can answer (another scheme, a Digest without a nonce, SHA-256 only).
	bool parseAuthChallenge(const std::vector<std::string>& values, HttpChallenge& out);
	// "Basic base64(user:password)" (RFC 7617)
	std::string basicAuthorization(const std::string& user, const std::string& password);
	// The Digest header (RFC 7616 / 2617) for one request: `uri` the request target as sent (path and query),
	// `nc` the count of requests made with this nonce (from 1), `cnonce` the client's own random word.
	std::string digestAuthorization(const HttpChallenge& c, const std::string& user, const std::string& password,
		const std::string& method, const std::string& uri, const std::string& cnonce, unsigned nc);

	// "http://host:port" in lower case with the scheme's port written out: two URLs whose origins match are the
	// same server ("" for a URL that is not http(s) with a host). A login only ever goes to the catalog's own.
	std::string urlOrigin(const std::string& url);
	bool sameOrigin(const std::string& a, const std::string& b);
	// the request target a Digest answer names: the path and the query ("/" when there is none)
	std::string urlRequestTarget(const std::string& url);
	// a search template's {searchTerms} filled with the words, percent-encoded (also where a server wrote %7B…%7D)
	std::string fillSearchTerms(const std::string& templ, const std::string& words);

	// ---- the file a book is saved as ----

	// A name an exFAT card takes: no control characters and none of " * / : < > ? \ |, no leading or trailing
	// dots and spaces, runs of spaces as one, at most maxBytes of UTF-8 (cut at a character's edge). "" in gives "".
	std::string exfatName(const std::string& s, size_t maxBytes = 120);
	// "<Title> - <Author>.<ext>", each part made safe; no author: "<Title>.<ext>"; no title: "Book.<ext>"
	std::string catalogBookName(const std::string& title, const std::string& author, const std::string& ext);
	// a catalog's sub-folder under Books: its name made safe ("Catalog" when nothing is left)
	std::string catalogFolderName(const std::string& catalogName);
	// path, or "path (2)", "path (3)"… before the extension, the first that `taken` says is free
	std::string freeFileName(const std::string& path, bool (*taken)(const std::string&));

	// ---- a catalog's cover pictures ----

	// The file name a cover's URL is cached under (<state>/cache/catalog-covers/<name>): 16 hex digits of a hash of
	// the whole URL, so one picture is one file whatever page asked for it
	std::string coverCacheName(const std::string& url);

	// The cover cache's size cap (about 40 MB: roughly 400 thumbnails). What to delete is artbg::evict's pure choice,
	// least recently used first (a file's date is touched on each hit)
	constexpr size_t COVER_CACHE_CAP = 40u * 1024u * 1024u;

	// ---- where it goes ----

	struct BooksPlace
	{
		std::string folder;	// a card's Books folder (one named Books, any capitals)
		long long freeBytes = 0;
	};
	// The Books folder a download goes in: the one with the most room among the cards' Books folders; with none,
	// "<firstCard>/Books" (made by the caller). "" when there is no card at all.
	std::string chooseBooksFolder(const std::vector<BooksPlace>& places, const std::string& firstCard);

	// ---- what a catalog already gave the card ----

	// <state>/catalogs/<catalog id>.got: one line per file, "entry id <TAB> path"; a book has one line for each format
	// it was downloaded in. A book whose files are all gone loses its mark (`path` answers "" for it). "Delete from the
	// card" removes the files listed here (`paths`) and no other.
	class CatalogGot
	{
	public:
		bool load(const std::string& file);	// "" : kept in memory only (a test)
		std::string path(const std::string& entryId) const;	// the first file still there; "" when none is
		std::vector<std::string> paths(const std::string& entryId) const;	// every file of it still there
		void set(const std::string& entryId, const std::string& path);	// one more file of the book (once each)
		void drop(const std::string& entryId);
		int count() const;	// the books still on the card
		const std::vector<std::pair<std::string, std::string>>& all() const { return items_; }

	private:
		void save() const;
		std::string file_;
		std::vector<std::pair<std::string, std::string>> items_;
	};
}
