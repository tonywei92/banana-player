// The servers' catalogue (docs/design/manga.md 5.1, the "Read from my server" flow of section 2): it builds the
// requests a Komga, Kavita or OPDS server is asked with, and turns their answers into the shapes a source script
// returns (5.2) — lists of {key, title, cover} with "more", chapters, page URLs — so the Ui reads a server exactly
// as it reads a script. Pure: no network, no threads, no SDL; the caller fetches with MangaFetch and hands the
// bodies here. A key out of a list feeds back into the builders that ask about it: a Komga id, or an absolute URL
// (a Kavita one with {key} where the server's api key was, so no list, id or page url ever holds the key itself).
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "mangaservers.h"

namespace bp
{
	// One GET: the URL and the headers to send with it, ready for MangaFetch::get.
	struct RemoteRequest
	{
		std::string url;
		std::vector<std::pair<std::string, std::string>> headers;
	};

	// A listed entry: a library or a series. `folder` means opening it lists more entries and its key is the URL
	// to fetch for that; otherwise the key is the series' own id (Komga) or its file (OPDS).
	struct RemoteItem
	{
		std::string key, title, cover;
		bool folder = false;
	};

	// …what popular()/latest()/search() return: {items, more}. `next` is the server's own URL for the page after
	// this one when it names one (OPDS writes a next link); Komga pages by number, so there the caller just asks
	// for page + 1 and `next` stays empty.
	struct RemoteList
	{
		std::vector<RemoteItem> items;
		bool more = false;
		std::string next;
	};

	// …a chapter, from the shape chapters() returns. `pages` is the page count when the server gives one: a Komga
	// book from its media, a PSE chapter from its stream link. -1 when it doesn't: `url` then names a whole CBZ
	// the reader downloads and unpacks itself, and there are no page URLs to ask for.
	struct RemoteChapter
	{
		std::string key, name, url;
		double number = -1;
		int pages = -1;
	};

	// The request for url as this server needs it. A user set means a Basic header (RFC 7617: "Authorization:
	// Basic base64(user:password)") for Komga and plain OPDS servers. Kavita is the exception worth stating: its
	// stored OPDS url holds {key} where Kavita's api key was, so remoteWithKey puts the key back into the request
	// url and the password is never sent as a header for that kind. Every request gets the Accept its kind answers
	// with — JSON from Komga's REST API, Atom feeds everywhere else; the builders below replace it where an
	// endpoint answers with something else (a page, a thumbnail).
	RemoteRequest remoteAuth(const MangaServer& s, const std::string& password, const std::string& url);

	// A url as a request goes to it: for Kavita the stored url's {key} is filled with the opened key, every other
	// kind's url is already the one to ask. Only a request ever holds the result; nothing stored or logged does.
	std::string remoteWithKey(const MangaServer& s, const std::string& password, const std::string& url);
	// A Kavita answer with the key taken out: Kavita repeats its own address (key and all) in every feed, so the key
	// becomes {key} before anything reads it, and the ids, hrefs and page urls that are kept carry the placeholder.
	// Any other kind's text, and a Kavita one with no key to hide, comes back as it came.
	std::string remoteWithoutKey(const MangaServer& s, const std::string& password, const std::string& text);

	// ---- Komga, REST API v1 ----
	// `page` is Komga's own 0-based page number; the list converters take the same number to decide `more`.
	RemoteRequest remoteKomgaLibraries(const MangaServer& s, const std::string& password);
	// a library's series, A–Z by Komga's own title sort
	RemoteRequest remoteKomgaSeries(const MangaServer& s, const std::string& password, const std::string& libraryId, int page);
	// series matching the text, across every library the user can see
	RemoteRequest remoteKomgaSearch(const MangaServer& s, const std::string& password, const std::string& query, int page);
	// a series' books in reading order — its chapters
	RemoteRequest remoteKomgaBooks(const MangaServer& s, const std::string& password, const std::string& seriesId, int page);
	// one of a book's pages, numbered from 1 as Komga numbers them; the count comes from the chapter's `pages`.
	// page < 1 asks for nothing: the request's url stays empty.
	RemoteRequest remoteKomgaPage(const MangaServer& s, const std::string& password, const std::string& bookId, int page);
	// the covers the lists' items point their readers at
	RemoteRequest remoteKomgaSeriesCover(const MangaServer& s, const std::string& password, const std::string& seriesId);
	RemoteRequest remoteKomgaBookCover(const MangaServer& s, const std::string& password, const std::string& bookId);

	// the answers into the catalogue; false, and nothing usable left in `out`, for a body that isn't one
	// /api/v1/libraries: folders, no covers — Komga offers a thumbnail only for series and books. The list is not
	// paged, so `more` stays false.
	bool remoteKomgaLibraryList(const std::string& json, RemoteList& out);
	// one page of series (browse or search): items keyed by the Komga series id, `cover` the thumbnail's URL (the
	// fetcher sends it remoteAuth's headers). `baseUrl` is the server's own url as stored.
	bool remoteKomgaSeriesList(const std::string& baseUrl, const std::string& json, int page, RemoteList& out);
	// one page of a series' books: chapters keyed by the Komga book id, numbered by its own sort value where the
	// server gave one, `pages` from the book's media. `more` says a page follows: fetch it with the books request
	// for page + 1 and keep appending.
	bool remoteKomgaChapterList(const std::string& json, int page, std::vector<RemoteChapter>& out, bool& more);

	// ---- OPDS 1.2 (Kavita, Komga's /opds/v1.2, Calibre) ----
	// the catalogue's start: Komga is rooted under its api url at /opds/v1.2; the other kinds store the OPDS url
	// itself (Kavita's is stored with {key} where its api key was, put back only in the request)
	RemoteRequest remoteOpdsStart(const MangaServer& s, const std::string& password);
	// a feed into a list: navigation entries become folders whose key is the absolute URL they lead to; acquisition
	// entries become chapter-like items keyed by their file (or their stream url, where a server offers no file —
	// ask remoteOpdsChapters about the same feed for the real chapter shapes). Entries with neither lead nowhere
	// and are dropped. `more`/`next` come from the feed's own next link.
	bool remoteOpdsList(const std::string& feedUrl, const std::string& xml, RemoteList& out);
	// an acquisition feed into chapters: entries that stream (PSE and a count) carry the pages' count and keep the
	// stream template as their key, with the whole CBZ on `url` too where the server offers one (for downloads);
	// the rest are a whole file on `url`, pages -1. Shelves aren't chapters and are skipped.
	bool remoteOpdsChapters(const std::string& feedUrl, const std::string& xml, std::vector<RemoteChapter>& out);
	// a streaming chapter's page URLs: {pageNumber} runs 0..count-1 (the number PSE itself uses) and {maxWidth} is
	// the panel's width; empty for a whole-file chapter.
	std::vector<std::string> remoteOpdsPageUrls(const RemoteChapter& ch, int maxWidth);
	// a search request from the OpenSearch description the start feed points at: the description's URL template
	// carries {searchTerms} — percent-encoding the text into it is the whole query. No template in the description:
	// the request's url stays empty, and the caller shows "this server can't search".
	RemoteRequest remoteOpdsSearch(const MangaServer& s, const std::string& password, const std::string& descUrl,
	                               const std::string& descXml, const std::string& query);
}
